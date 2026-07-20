#!/usr/bin/env python3
"""Import local Egaroucid board-score data into normalized TSV schema v2.

The source archive stays untouched. The importer streams ``.txt`` members from
the zip file and writes only normalized, local-only training rows and a
machine-readable report.
"""

from __future__ import annotations

import argparse
import hashlib
import heapq
import io
import json
import os
import re
import sys
import time
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, TextIO


IMPORTER_VERSION = "egaroucid-board-score-v1"
IDENTITY_POLICY_VERSION = "egaroucid-board-score-identity-v1"
SOURCE_KIND = "egaroucid-board-score-local"
LABEL_KIND = "teacher_value_disc_diff"
LABEL_UNIT = "disc"
LABEL_PERSPECTIVE = "side_to_move"
PHASE_COUNT = 13
ENUMERATED_NEGAMAX = "enumerated_static_eval_negamax"
SELFPLAY_TERMINAL = "selfplay_terminal_outcome"
HEADER = (
    "record_id",
    "position_id",
    "game_group_id",
    "board_id",
    "source_occurrence_id",
    "source_dataset_id",
    "split",
    "board_a1_to_h8",
    "label_kind",
    "label_unit",
    "label_perspective",
    "label_score_side_to_move",
    "occupied_count",
    "phase",
    "player_disc_count",
    "opponent_disc_count",
    "empty_count",
)
INTEGER_RE = re.compile(r"-?[0-9]+")
SHA256_RE = re.compile(r"[a-f0-9]{64}")
NOTES = (
    "local-only external corpus",
    "raw Egaroucid archives remain untouched and must not be committed",
    "generated normalized TSV, reports, datasets, and weights must not be committed",
    "labels use the neutral teacher_value_disc_diff contract in disc units from the side-to-move perspective",
    "4-15 occupied rows come from Egaroucid 7.4.0 lv17 enumerated progress, static evaluation, and negamax",
    "16-63 occupied rows come from Egaroucid 7.5.1 lv17 self-play terminal outcomes",
    "board-score rows have no transcript identity; game_group_id is the canonical board group",
    "split is derived from dataset_id and the canonical board group",
    "not an Elo result, production strength claim, or publication gate",
)


class BoardScoreImportError(RuntimeError):
    """Raised when a source or output violates the importer contract."""


@dataclass(frozen=True)
class CandidateRow:
    sample_key: int
    board_id: str
    record_id: str
    phase: int
    split: str
    label: int
    label_generation: str
    line_text: str


@dataclass
class SampleBucket:
    capacity: int
    entries: dict[str, CandidateRow] = field(default_factory=dict)
    heap: list[tuple[int, str, str]] = field(default_factory=list)

    def _discard_stale(self) -> None:
        while self.heap:
            negative_key, board_id, record_id = self.heap[0]
            current = self.entries.get(board_id)
            if (
                current is not None
                and current.sample_key == -negative_key
                and current.record_id == record_id
            ):
                return
            heapq.heappop(self.heap)

    def consider(self, row: CandidateRow) -> tuple[bool, bool]:
        """Return ``(retained, duplicate_board)``."""
        current = self.entries.get(row.board_id)
        if current is not None:
            if (row.record_id, row.line_text) < (current.record_id, current.line_text):
                self.entries[row.board_id] = row
                heapq.heappush(self.heap, (-row.sample_key, row.board_id, row.record_id))
            return True, True

        if len(self.entries) < self.capacity:
            self.entries[row.board_id] = row
            heapq.heappush(self.heap, (-row.sample_key, row.board_id, row.record_id))
            return True, False

        self._discard_stale()
        if not self.heap:
            raise BoardScoreImportError("internal sampler heap is unexpectedly empty")
        worst_key = -self.heap[0][0]
        worst_board_id = self.heap[0][1]
        if (row.sample_key, row.board_id) >= (worst_key, worst_board_id):
            return False, False

        heapq.heappop(self.heap)
        del self.entries[worst_board_id]
        self.entries[row.board_id] = row
        heapq.heappush(self.heap, (-row.sample_key, row.board_id, row.record_id))
        return True, False


@dataclass
class CandidateSampler:
    global_capacity: int | None
    per_phase_capacity: int | None
    global_bucket: SampleBucket | None = None
    phase_buckets: dict[int, SampleBucket] = field(default_factory=dict)
    duplicate_retained_board_rows: int = 0

    def __post_init__(self) -> None:
        if self.global_capacity is not None:
            self.global_bucket = SampleBucket(self.global_capacity)
        if self.per_phase_capacity is not None:
            self.phase_buckets = {
                phase: SampleBucket(self.per_phase_capacity) for phase in range(PHASE_COUNT)
            }

    @property
    def enabled(self) -> bool:
        return self.global_capacity is not None or self.per_phase_capacity is not None

    def consider(self, row: CandidateRow) -> None:
        if self.global_bucket is not None:
            _retained, duplicate = self.global_bucket.consider(row)
        elif self.per_phase_capacity is not None:
            _retained, duplicate = self.phase_buckets[row.phase].consider(row)
        else:
            raise BoardScoreImportError("sampler is not configured")
        if duplicate:
            self.duplicate_retained_board_rows += 1

    def rows(self) -> list[CandidateRow]:
        if self.global_bucket is not None:
            rows = list(self.global_bucket.entries.values())
        else:
            rows = [
                row
                for phase in range(PHASE_COUNT)
                for row in self.phase_buckets[phase].entries.values()
            ]
        return sorted(rows, key=lambda row: (row.phase, row.sample_key, row.board_id))


@dataclass
class Summary:
    source_files_seen: int = 0
    source_files_processed: int = 0
    input_rows: int = 0
    valid_rows: int = 0
    rejected_rows: int = 0
    emitted_positions: int = 0
    duplicate_retained_board_rows: int = 0
    candidate_counts_by_phase: dict[str, int] = field(default_factory=dict)
    candidate_counts_by_label_generation: dict[str, int] = field(default_factory=dict)
    counts_by_phase: dict[str, int] = field(default_factory=dict)
    counts_by_label_generation: dict[str, int] = field(default_factory=dict)
    counts_by_split: dict[str, int] = field(
        default_factory=lambda: {"train": 0, "validation": 0, "test": 0}
    )
    label_min: int | None = None
    label_max: int | None = None
    rejected_reasons: list[str] = field(default_factory=list)


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def digest_for_parts(*parts: object) -> str:
    return hashlib.sha256("\t".join(str(part) for part in parts).encode("utf-8")).hexdigest()


def phase_for_occupied_count(occupied_count: int) -> int:
    return min(PHASE_COUNT - 1, ((occupied_count - 4) * PHASE_COUNT) // 60)


def label_generation_for_occupied_count(occupied_count: int) -> str:
    return ENUMERATED_NEGAMAX if occupied_count <= 15 else SELFPLAY_TERMINAL


def split_for_group(dataset_id: str, game_group_id: str) -> str:
    digest = digest_for_parts(dataset_id, game_group_id)
    bucket = int(digest[:16], 16) % 10
    if bucket == 0:
        return "validation"
    if bucket == 1:
        return "test"
    return "train"


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        temporary.write_text(text, encoding="utf-8")
        temporary.replace(path)
    finally:
        if temporary.exists():
            temporary.unlink()


def output_temporary_path(path: Path) -> Path:
    return path.with_name(f".{path.name}.tmp-{os.getpid()}")


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BoardScoreImportError(f"cannot read manifest: {path.name}: {error}") from error
    if not isinstance(data, dict):
        raise BoardScoreImportError("manifest root must be an object")
    dataset_id = data.get("dataset_id")
    if not isinstance(dataset_id, str) or not dataset_id:
        raise BoardScoreImportError("manifest dataset_id must be a non-empty string")
    checksum = data.get("sha256")
    if not isinstance(checksum, str) or SHA256_RE.fullmatch(checksum) is None:
        raise BoardScoreImportError("manifest sha256 must be 64 lowercase hexadecimal characters")
    if data.get("derived_weights_allowed") is not True:
        raise BoardScoreImportError(
            "manifest derived_weights_allowed must be true before this source can feed training"
        )
    return data


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--dataset-id")
    limits = parser.add_mutually_exclusive_group()
    limits.add_argument(
        "--max-positions",
        type=int,
        help="Keep a deterministic global min-hash sample of at most this many unique boards.",
    )
    limits.add_argument(
        "--max-positions-per-phase",
        type=int,
        help="Keep a deterministic min-hash sample of at most this many unique boards per phase.",
    )
    parser.add_argument(
        "--max-source-files",
        type=int,
        help=(
            "Process only the first N sorted .txt members; intended for bounded "
            "smoke/development."
        ),
    )
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--progress-every-rows", type=int, default=1_000_000)
    args = parser.parse_args()
    for name in ("max_positions", "max_positions_per_phase", "max_source_files"):
        value = getattr(args, name)
        if value is not None and value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.progress_every_rows <= 0:
        parser.error("--progress-every-rows must be positive")
    if args.output.resolve() == args.report.resolve():
        parser.error("--output and --report must be different paths")
    if args.input.resolve() in {args.output.resolve(), args.report.resolve()}:
        parser.error("input path must not be used as an output path")
    return args


def parse_source_line(text: str) -> tuple[str, int, int, int, int]:
    fields = text.split()
    if len(fields) != 2:
        raise ValueError("expected '<64-character-board> <integer-score>'")
    board, score_text = fields
    if len(board) != 64 or set(board).difference({"X", "O", "-"}):
        raise ValueError("board must contain exactly 64 X/O/- characters")
    if INTEGER_RE.fullmatch(score_text) is None:
        raise ValueError("score must be an integer")
    score = int(score_text)
    if score < -64 or score > 64:
        raise ValueError("score must be in [-64, 64]")
    player_count = board.count("X")
    opponent_count = board.count("O")
    empty_count = board.count("-")
    occupied_count = player_count + opponent_count
    if occupied_count < 4 or occupied_count > 63:
        raise ValueError("occupied disc count must be in [4, 63]")
    return board, score, player_count, opponent_count, empty_count


def make_candidate(
    *,
    dataset_id: str,
    member_name: str,
    line_number: int,
    board: str,
    score: int,
    player_count: int,
    opponent_count: int,
    empty_count: int,
    seed: int,
) -> CandidateRow:
    board_digest = digest_for_parts("board-v1", board)
    board_id = f"board-{board_digest[:16]}"
    group_id = f"board-group-{board_digest[:16]}"
    position_id = f"{dataset_id}-{board_id}"
    occurrence_digest = digest_for_parts(
        dataset_id,
        IDENTITY_POLICY_VERSION,
        member_name,
        line_number,
        board,
        score,
    )
    source_occurrence_id = f"occ-{occurrence_digest[:16]}"
    record_id = f"{dataset_id}-{occurrence_digest[:24]}"
    split = split_for_group(dataset_id, group_id)
    occupied_count = player_count + opponent_count
    phase = phase_for_occupied_count(occupied_count)
    label_generation = label_generation_for_occupied_count(occupied_count)
    fields = (
        record_id,
        position_id,
        group_id,
        board_id,
        source_occurrence_id,
        dataset_id,
        split,
        board,
        LABEL_KIND,
        LABEL_UNIT,
        LABEL_PERSPECTIVE,
        str(score),
        str(occupied_count),
        str(phase),
        str(player_count),
        str(opponent_count),
        str(empty_count),
    )
    sample_key = int(digest_for_parts(seed, "board-score-sample-v1", board_id), 16)
    return CandidateRow(
        sample_key=sample_key,
        board_id=board_id,
        record_id=record_id,
        phase=phase,
        split=split,
        label=score,
        label_generation=label_generation,
        line_text="\t".join(fields),
    )


def write_output_line(handle: TextIO, digest: Any, line: str) -> None:
    text = f"{line}\n"
    handle.write(text)
    digest.update(text.encode("utf-8"))


def record_emitted(summary: Summary, row: CandidateRow) -> None:
    summary.emitted_positions += 1
    phase_key = str(row.phase)
    summary.counts_by_phase[phase_key] = summary.counts_by_phase.get(phase_key, 0) + 1
    summary.counts_by_label_generation[row.label_generation] = (
        summary.counts_by_label_generation.get(row.label_generation, 0) + 1
    )
    summary.counts_by_split[row.split] += 1
    summary.label_min = (
        row.label if summary.label_min is None else min(summary.label_min, row.label)
    )
    summary.label_max = (
        row.label if summary.label_max is None else max(summary.label_max, row.label)
    )


def sampling_mode(args: argparse.Namespace) -> str:
    bounded = args.max_source_files is not None
    if args.max_positions_per_phase is not None:
        return "bounded-phase-topk" if bounded else "full-scan-phase-topk"
    if args.max_positions is not None:
        return "bounded-global-topk" if bounded else "full-scan-global-topk"
    return "bounded-stream" if bounded else "full-stream"


def report_data(
    *,
    args: argparse.Namespace,
    dataset_id: str,
    source_checksum: str,
    manifest_checksum: str,
    output_checksum: str,
    summary: Summary,
    elapsed_seconds: float,
) -> dict[str, Any]:
    return {
        "schema_version": 2,
        "importer_version": IMPORTER_VERSION,
        "identity_policy_version": IDENTITY_POLICY_VERSION,
        "source_dataset_id": dataset_id,
        "source_kind": SOURCE_KIND,
        "input_kind": "board-score-archive",
        "input_archive": args.input.name,
        "source_checksum": f"sha256:{source_checksum}",
        "manifest": args.manifest.name,
        "manifest_checksum": f"sha256:{manifest_checksum}",
        "output": args.output.name,
        "output_checksum": f"sha256:{output_checksum}",
        "sampling_mode": sampling_mode(args),
        "seed": args.seed,
        "max_positions": args.max_positions,
        "max_positions_per_phase": args.max_positions_per_phase,
        "max_source_files": args.max_source_files,
        "source_scan_complete": args.max_source_files is None,
        "source_files_seen": summary.source_files_seen,
        "source_files_processed": summary.source_files_processed,
        "input_rows": summary.input_rows,
        "valid_rows": summary.valid_rows,
        "rejected_rows": summary.rejected_rows,
        "emitted_positions": summary.emitted_positions,
        "duplicate_retained_board_rows": summary.duplicate_retained_board_rows,
        "candidate_counts_by_phase": summary.candidate_counts_by_phase,
        "counts_by_phase": summary.counts_by_phase,
        "counts_by_split": summary.counts_by_split,
        "counts_by_label_kind": {LABEL_KIND: summary.emitted_positions},
        "candidate_counts_by_label_generation": (
            summary.candidate_counts_by_label_generation
        ),
        "counts_by_label_generation": summary.counts_by_label_generation,
        "label_generation_by_occupied_range": [
            {
                "occupied_count_min": 4,
                "occupied_count_max": 15,
                "generation_kind": ENUMERATED_NEGAMAX,
                "engine": "Egaroucid for Console 7.4.0 lv17",
                "procedure": (
                    "enumerate every progression through move 11, evaluate with "
                    "Egaroucid, then negamax the results"
                ),
                "candidate_rows": summary.candidate_counts_by_label_generation.get(
                    ENUMERATED_NEGAMAX, 0
                ),
                "emitted_rows": summary.counts_by_label_generation.get(
                    ENUMERATED_NEGAMAX, 0
                ),
            },
            {
                "occupied_count_min": 16,
                "occupied_count_max": 63,
                "generation_kind": SELFPLAY_TERMINAL,
                "engine": "Egaroucid for Console 7.5.1 lv17",
                "procedure": (
                    "self-play terminal score after a randomized opening length "
                    "with 7 <= N <= 59"
                ),
                "candidate_rows": summary.candidate_counts_by_label_generation.get(
                    SELFPLAY_TERMINAL, 0
                ),
                "emitted_rows": summary.counts_by_label_generation.get(
                    SELFPLAY_TERMINAL, 0
                ),
            },
        ],
        "label_min": summary.label_min,
        "label_max": summary.label_max,
        "elapsed_seconds": round(elapsed_seconds, 3),
        "rejected_reasons": summary.rejected_reasons,
        "notes": list(NOTES),
    }


def run_import(args: argparse.Namespace) -> Summary:
    started = time.monotonic()
    if not args.input.is_file():
        raise BoardScoreImportError(f"input archive does not exist: {args.input.name}")
    manifest = load_manifest(args.manifest)
    dataset_id = args.dataset_id or manifest["dataset_id"]
    if args.dataset_id is not None and args.dataset_id != manifest["dataset_id"]:
        raise BoardScoreImportError("--dataset-id must match manifest dataset_id")

    source_checksum = sha256_file(args.input)
    if source_checksum != manifest["sha256"]:
        raise BoardScoreImportError(
            "source checksum does not match manifest: "
            f"expected sha256:{manifest['sha256']}, got sha256:{source_checksum}"
        )
    manifest_checksum = sha256_file(args.manifest)

    try:
        archive = zipfile.ZipFile(args.input)
    except zipfile.BadZipFile as error:
        raise BoardScoreImportError(f"invalid zip archive: {args.input.name}") from error

    summary = Summary()
    sampler = CandidateSampler(args.max_positions, args.max_positions_per_phase)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    output_temporary = output_temporary_path(args.output)
    output_digest = hashlib.sha256()
    try:
        with output_temporary.open("w", encoding="utf-8", newline="") as output:
            write_output_line(output, output_digest, "\t".join(HEADER))
            with archive:
                members = sorted(
                    (
                        info
                        for info in archive.infolist()
                        if not info.is_dir() and info.filename.lower().endswith(".txt")
                    ),
                    key=lambda info: info.filename,
                )
                if not members:
                    raise BoardScoreImportError("zip archive contains no .txt members")
                summary.source_files_seen = len(members)
                if args.max_source_files is not None:
                    members = members[: args.max_source_files]

                for info in members:
                    summary.source_files_processed += 1
                    with archive.open(info) as raw:
                        with io.TextIOWrapper(raw, encoding="utf-8", newline="") as source:
                            for line_number, raw_line in enumerate(source, start=1):
                                text = raw_line.strip()
                                if not text:
                                    continue
                                summary.input_rows += 1
                                try:
                                    board, score, player, opponent, empty = parse_source_line(text)
                                except ValueError as error:
                                    summary.rejected_rows += 1
                                    if len(summary.rejected_reasons) < 20:
                                        summary.rejected_reasons.append(
                                            f"{info.filename}:{line_number}: {error}"
                                        )
                                    continue
                                summary.valid_rows += 1
                                occupied_count = player + opponent
                                phase = phase_for_occupied_count(occupied_count)
                                phase_key = str(phase)
                                summary.candidate_counts_by_phase[phase_key] = (
                                    summary.candidate_counts_by_phase.get(phase_key, 0) + 1
                                )
                                generation = label_generation_for_occupied_count(
                                    occupied_count
                                )
                                summary.candidate_counts_by_label_generation[generation] = (
                                    summary.candidate_counts_by_label_generation.get(
                                        generation, 0
                                    )
                                    + 1
                                )
                                row = make_candidate(
                                    dataset_id=dataset_id,
                                    member_name=info.filename,
                                    line_number=line_number,
                                    board=board,
                                    score=score,
                                    player_count=player,
                                    opponent_count=opponent,
                                    empty_count=empty,
                                    seed=args.seed,
                                )
                                if sampler.enabled:
                                    sampler.consider(row)
                                else:
                                    write_output_line(output, output_digest, row.line_text)
                                    record_emitted(summary, row)
                                if summary.input_rows % args.progress_every_rows == 0:
                                    print(
                                        "progress "
                                        f"rows={summary.input_rows} valid={summary.valid_rows} "
                                        f"rejected={summary.rejected_rows} "
                                        f"files={summary.source_files_processed}",
                                        file=sys.stderr,
                                    )

            if sampler.enabled:
                for row in sampler.rows():
                    write_output_line(output, output_digest, row.line_text)
                    record_emitted(summary, row)
                summary.duplicate_retained_board_rows = sampler.duplicate_retained_board_rows

        if summary.emitted_positions == 0:
            raise BoardScoreImportError("no normalized positions were emitted")
        output_temporary.replace(args.output)
    finally:
        if output_temporary.exists():
            output_temporary.unlink()

    report = report_data(
        args=args,
        dataset_id=dataset_id,
        source_checksum=source_checksum,
        manifest_checksum=manifest_checksum,
        output_checksum=output_digest.hexdigest(),
        summary=summary,
        elapsed_seconds=time.monotonic() - started,
    )
    atomic_write_text(args.report, stable_json(report))
    print(
        "summary "
        f"input_rows={summary.input_rows} valid_rows={summary.valid_rows} "
        f"rejected_rows={summary.rejected_rows} emitted_positions={summary.emitted_positions} "
        f"sampling_mode={sampling_mode(args)}",
        file=sys.stderr,
    )
    return summary


def main() -> int:
    args = parse_args()
    try:
        run_import(args)
    except (BoardScoreImportError, OSError, UnicodeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
