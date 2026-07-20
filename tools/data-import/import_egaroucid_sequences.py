#!/usr/bin/env python3
"""Import local Egaroucid v0002 sequence transcripts as normalized TSV rows.

Format assumptions:

* inputs are local-only external transcript files, directories containing
  ``.txt`` files and/or ``.zip`` archives, or zip archives containing ``.txt``
  files
* each non-empty line is one Othello game transcript
* moves are either whitespace-separated coordinates/tokens or a compact
  coordinate string such as ``d3c3b3...``
* coordinates are a1..h8; explicit pass tokens are ``pass`` or ``p``
* transcripts may omit pass tokens; when the side to move has no legal move,
  an implicit pass is inserted before reading the next move
* emitted labels are final-disc-difference labels derived from the transcript
  final board, converted to side-to-move perspective; they are not teacher
  search labels and are not production strength evidence
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import heapq
import io
import json
import os
import subprocess
import sys
import time
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, TextIO


DATASET_ID = "egaroucid-sequence-v0002-local"
IMPORTER_VERSION = "egaroucid-sequence-v1"
IDENTITY_POLICY_VERSION = "egaroucid-sequence-identity-v1"
SOURCE_KIND = "egaroucid-sequence-local"
LABEL_KIND = "observed_final_disc_diff"
LABEL_UNIT = "final_disc_diff"
LABEL_PERSPECTIVE = "side_to_move"
PHASE_COUNT = 13
MIN_OCCUPIED_COUNT = 4
MAX_EGAROUCID_OCCUPIED_COUNT = 64
SPLITS = ("train", "validation", "test")
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
NOTES = (
    "local-only external corpus",
    "generated normalized TSV must not be committed",
    "labels are final-disc-diff side-to-move derived from transcript final board",
    "not a teacher-search label",
    "not production strength evidence",
    "game_group_id is derived from canonical replayed move/pass sequence",
    "source paths, archive members, and line numbers are provenance only and do not affect normalized identity",
    "default split is derived from dataset_id + game_group_id",
    "board_id is derived from the side-to-move-relative board only",
)
SPLIT_POLICIES = ("game-group", "connected-board-game")
SPLIT_POLICY_VERSIONS = {
    "game-group": "game-group-v1",
    "connected-board-game": "connected-board-game-v1",
}
BOUNDED_DEV_NOTES = (
    "bounded-dev mode is for local measurement iteration",
    "bounded-dev is not a full-corpus exact top-k sample",
    "bounded-dev is not a production strength claim",
)
STREAMING_TARGET_NOTES = (
    "streaming-target mode stops after the requested retained position target",
    "streaming-target source traversal is content-addressed and path/member-name independent",
    "streaming-target fingerprints candidate sources before target-bounded replay",
    "streaming-target replay cost scales with target size and is not a full-corpus exact top-k sample",
    "streaming-target is a local measurement sampling frame, not a production strength claim",
)


class ImportErrorWithLocation(ValueError):
    pass


@dataclass
class CandidateRow:
    key: str
    ordinal: int
    fields: tuple[str, ...]
    line_text: str
    position_id: str
    game_group_id: str
    board_id: str
    split: str
    phase: int
    label: int


@dataclass
class TopKRows:
    capacity: int | None
    mode: str = "topk"
    heap: list[tuple[int, str, str]] = field(default_factory=list)
    entries: dict[str, str] = field(default_factory=dict)
    all_rows: list[CandidateRow] = field(default_factory=list)
    retained_rows: dict[str, list[CandidateRow]] = field(default_factory=dict)

    @staticmethod
    def heap_key(key: str) -> int:
        return -int(key, 16)

    def discard_stale_worst_entries(self) -> None:
        while self.heap:
            _, position_id, key = self.heap[0]
            if self.entries.get(position_id) == key:
                return
            heapq.heappop(self.heap)

    def push(self, position_id: str, key: str) -> None:
        self.entries[position_id] = key
        heapq.heappush(self.heap, (self.heap_key(key), position_id, key))

    def consider(self, row: CandidateRow) -> None:
        if self.mode == "streaming":
            if self.capacity is None or len(self.all_rows) < self.capacity:
                self.all_rows.append(row)
            return
        if self.capacity is None:
            self.all_rows.append(row)
            return
        if self.capacity <= 0:
            return
        current = self.entries.get(row.position_id)
        if current is not None:
            if row.key < current:
                self.push(row.position_id, row.key)
            self.retained_rows.setdefault(row.position_id, []).append(row)
            return
        if len(self.entries) < self.capacity:
            self.push(row.position_id, row.key)
            self.retained_rows[row.position_id] = [row]
            return
        self.discard_stale_worst_entries()
        if not self.heap:
            self.push(row.position_id, row.key)
            self.retained_rows[row.position_id] = [row]
            return
        _, worst_position_id, worst_key = self.heap[0]
        if row.key < worst_key:
            del self.entries[worst_position_id]
            self.retained_rows.pop(worst_position_id, None)
            heapq.heappop(self.heap)
            self.push(row.position_id, row.key)
            self.retained_rows[row.position_id] = [row]

    def rows(self) -> list[CandidateRow]:
        if self.mode == "streaming" or self.capacity is None:
            return sorted(self.all_rows, key=lambda row: row.ordinal)
        return sorted(
            (
                row
                for position_id, rows in self.retained_rows.items()
                if position_id in self.entries
                for row in rows
            ),
            key=lambda row: row.ordinal,
        )

    def kept_count(self) -> int:
        if self.mode == "streaming":
            return len(self.all_rows)
        if self.capacity is None:
            return len(self.all_rows)
        return len(self.entries)

    def limit_reached(self) -> bool:
        return self.capacity is not None and self.kept_count() >= self.capacity


@dataclass
class Summary:
    input_games: int = 0
    accepted_games: int = 0
    rejected_games: int = 0
    emitted_positions: int = 0
    invalid_move_count: int = 0
    pass_count: int = 0
    terminal_count: int = 0
    split_counts: dict[str, int] = field(default_factory=lambda: {split: 0 for split in SPLITS})
    phase_counts: dict[str, int] = field(default_factory=dict)
    label_min: int | None = None
    label_max: int | None = None
    label_sum: int = 0
    rejected_reasons: list[str] = field(default_factory=list)
    source_files: int = 0
    source_files_seen: int = 0
    source_files_processed: int = 0
    games_seen: int = 0
    games_replayed: int = 0
    replay_skip_count: int = 0
    candidate_positions: int = 0
    game_group_occurrences: dict[str, int] = field(default_factory=dict)
    transcript_occurrences: dict[str, int] = field(default_factory=dict)
    provenance_samples: list[dict[str, object]] = field(default_factory=list)


@dataclass
class ComponentInfo:
    row_count: int = 0
    nodes: set[str] = field(default_factory=set)
    source_dataset_ids: set[str] = field(default_factory=set)


class UnionFind:
    def __init__(self) -> None:
        self.parent: dict[str, str] = {}

    def add(self, value: str) -> None:
        self.parent.setdefault(value, value)

    def find(self, value: str) -> str:
        self.add(value)
        root = value
        while self.parent[root] != root:
            root = self.parent[root]
        while self.parent[value] != value:
            parent = self.parent[value]
            self.parent[value] = root
            value = parent
        return root

    def union(self, left: str, right: str) -> None:
        left_root = self.find(left)
        right_root = self.find(right)
        if left_root == right_root:
            return
        if right_root < left_root:
            left_root, right_root = right_root, left_root
        self.parent[right_root] = left_root


@dataclass
class ProgressState:
    start_time: float = field(default_factory=time.monotonic)
    last_games_report: int = 0
    last_files_report: int = 0


@dataclass(frozen=True)
class ReplaySnapshot:
    ply: int
    board_text: str
    label: int
    player_disc_count: int
    opponent_disc_count: int
    empty_count: int


@dataclass(frozen=True)
class ReplayResult:
    accepted: bool
    error: str = ""
    pass_count: int = 0
    terminal: bool = False
    canonical_moves: str = ""
    snapshots: tuple[ReplaySnapshot, ...] = ()


def default_replay_helper_path() -> Path | None:
    env_path = os.environ.get("VIBE_OTHELLO_EGAROUCID_REPLAY_HELPER")
    if env_path:
        return Path(env_path)
    sibling = Path(__file__).with_name("vibe-othello-egaroucid-sequence-replay")
    if sibling.exists():
        return sibling
    return None


class ReplayHelper:
    def __init__(self, path: Path | None) -> None:
        self.path = path or default_replay_helper_path()
        self.process: subprocess.Popen[str] | None = None

    def __enter__(self) -> "ReplayHelper":
        if self.path is None:
            raise ImportErrorWithLocation(
                "Egaroucid replay helper is required; pass --replay-helper or set "
                "VIBE_OTHELLO_EGAROUCID_REPLAY_HELPER"
            )
        self.process = subprocess.Popen(
            [str(self.path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        return self

    def __exit__(self, _exc_type: object, _exc: object, _traceback: object) -> None:
        if self.process is None:
            return
        if self.process.stdin is not None:
            self.process.stdin.close()
        return_code = self.process.wait()
        if return_code != 0 and _exc_type is None:
            stderr = self.process.stderr.read() if self.process.stderr is not None else ""
            raise ImportErrorWithLocation(
                f"Egaroucid replay helper exited with {return_code}: {stderr.strip()}"
            )

    def replay(self, source_occurrence_id: str, text: str) -> ReplayResult:
        if self.process is None or self.process.stdin is None or self.process.stdout is None:
            raise ImportErrorWithLocation("Egaroucid replay helper is not running")
        self.process.stdin.write(f"{source_occurrence_id}\t{text}\n")
        self.process.stdin.flush()

        begin = self.process.stdout.readline()
        if begin == "":
            stderr = self.process.stderr.read() if self.process.stderr is not None else ""
            raise ImportErrorWithLocation(f"Egaroucid replay helper stopped: {stderr.strip()}")
        begin_fields = begin.rstrip("\n").split("\t")
        if begin_fields != ["begin", source_occurrence_id]:
            raise ImportErrorWithLocation(f"Egaroucid replay helper protocol error: {begin!r}")

        status = self.process.stdout.readline()
        if status == "":
            raise ImportErrorWithLocation("Egaroucid replay helper ended mid-response")
        status_fields = status.rstrip("\n").split("\t")
        snapshots: list[ReplaySnapshot] = []
        if status_fields[0] == "error":
            error = status_fields[1] if len(status_fields) > 1 else "replay failed"
            self._expect_end()
            return ReplayResult(accepted=False, error=error)
        if len(status_fields) != 4 or status_fields[0] != "ok":
            raise ImportErrorWithLocation(f"Egaroucid replay helper protocol error: {status!r}")

        pass_count = int(status_fields[1])
        terminal = status_fields[2] == "1"
        canonical_moves = status_fields[3]
        while True:
            line = self.process.stdout.readline()
            if line == "":
                raise ImportErrorWithLocation("Egaroucid replay helper ended mid-response")
            line = line.rstrip("\n")
            if line == "end":
                break
            fields = line.split("\t")
            if len(fields) != 7 or fields[0] != "row":
                raise ImportErrorWithLocation(f"Egaroucid replay helper protocol error: {line!r}")
            snapshots.append(
                ReplaySnapshot(
                    ply=int(fields[1]),
                    board_text=fields[2],
                    label=int(fields[3]),
                    player_disc_count=int(fields[4]),
                    opponent_disc_count=int(fields[5]),
                    empty_count=int(fields[6]),
                )
            )
        return ReplayResult(
            accepted=True,
            pass_count=pass_count,
            terminal=terminal,
            canonical_moves=canonical_moves,
            snapshots=tuple(snapshots),
        )

    def _expect_end(self) -> None:
        if self.process is None or self.process.stdout is None:
            raise ImportErrorWithLocation("Egaroucid replay helper is not running")
        line = self.process.stdout.readline()
        if line.rstrip("\n") != "end":
            raise ImportErrorWithLocation(f"Egaroucid replay helper protocol error: {line!r}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument(
        "--replay-helper",
        type=Path,
        help=(
            "Path to vibe-othello-egaroucid-sequence-replay. Defaults to "
            "VIBE_OTHELLO_EGAROUCID_REPLAY_HELPER or a sibling executable."
        ),
    )
    parser.add_argument("--dataset-id", default=DATASET_ID)
    parser.add_argument("--report", type=Path, help="Write JSON import report.")
    parser.add_argument("--min-ply", type=int, default=8)
    parser.add_argument("--max-ply", type=int)
    parser.add_argument("--ply-stride", type=int, default=1)
    parser.add_argument("--max-positions", type=int)
    parser.add_argument(
        "--sampling-mode",
        choices=("full-scan-topk", "streaming-target", "bounded-dev"),
        default="full-scan-topk",
    )
    parser.add_argument("--max-games", type=int)
    parser.add_argument("--max-files", type=int)
    parser.add_argument("--file-sample-rate", type=float)
    parser.add_argument("--game-sample-rate", type=float)
    parser.add_argument("--file-order", choices=("path", "hash"), default="path")
    parser.add_argument("--progress-every-games", type=int)
    parser.add_argument("--progress-every-files", type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--emit-terminal", dest="emit_terminal", action="store_true", default=False)
    parser.add_argument("--no-emit-terminal", dest="emit_terminal", action="store_false")
    parser.add_argument(
        "--split-policy",
        choices=SPLIT_POLICIES,
        default="game-group",
        help=(
            "Measurement split policy. connected-board-game assigns connected "
            "components of game_group_id and board_id to one split."
        ),
    )
    parser.add_argument(
        "--strict-board-disjoint-splits",
        action="store_true",
        help="Fail if an exact side-to-move-relative board appears in more than one output split.",
    )
    args = parser.parse_args()
    if args.min_ply < 0:
        parser.error("--min-ply must be non-negative")
    if args.max_ply is not None and args.max_ply < args.min_ply:
        parser.error("--max-ply must be >= --min-ply")
    if args.ply_stride <= 0:
        parser.error("--ply-stride must be positive")
    if args.max_positions is not None and args.max_positions <= 0:
        parser.error("--max-positions must be positive")
    if args.max_games is not None and args.max_games <= 0:
        parser.error("--max-games must be positive")
    if args.max_files is not None and args.max_files <= 0:
        parser.error("--max-files must be positive")
    for name in ("file_sample_rate", "game_sample_rate"):
        value = getattr(args, name)
        if value is not None and not (0.0 < value <= 1.0):
            parser.error(f"--{name.replace('_', '-')} must be > 0.0 and <= 1.0")
    bounded_controls = (
        args.max_games is not None
        or args.max_files is not None
        or args.file_sample_rate is not None
        or args.game_sample_rate is not None
    )
    if bounded_controls and args.sampling_mode not in {"bounded-dev", "streaming-target"}:
        parser.error(
            "--max-games, --max-files, --file-sample-rate, and --game-sample-rate "
            "require --sampling-mode bounded-dev or streaming-target"
        )
    if args.progress_every_games is not None and args.progress_every_games <= 0:
        parser.error("--progress-every-games must be positive")
    if args.progress_every_files is not None and args.progress_every_files <= 0:
        parser.error("--progress-every-files must be positive")
    return args


def stable_json(data: object) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def digest_for_parts(*parts: object) -> str:
    return hashlib.sha256("\t".join(str(part) for part in parts).encode("ascii")).hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def digest_fraction(digest: str) -> float:
    return int(digest, 16) / float(1 << 256)


def split_for_digest(digest: str) -> str:
    bucket = int(digest[:16], 16) % 10
    if bucket == 0:
        return "validation"
    if bucket == 1:
        return "test"
    return "train"


def prefixed_id(prefix: str, digest: str) -> str:
    return f"{prefix}-{digest[:16]}"


def phase_for_occupied_count(occupied_count: int) -> int:
    phase = (occupied_count - MIN_OCCUPIED_COUNT) * PHASE_COUNT // 60
    return min(PHASE_COUNT - 1, max(0, phase))


def file_digest(args: argparse.Namespace, source_ref: str) -> str:
    return digest_for_parts(args.seed, "file", source_ref)


def include_by_rate(digest: str, rate: float | None) -> bool:
    return rate is None or digest_fraction(digest) < rate


def selected_file_refs(args: argparse.Namespace, refs: list[str]) -> list[str]:
    ordered = sorted(refs, key=lambda ref: ref if args.file_order == "path" else file_digest(args, ref))
    filtered = [ref for ref in ordered if include_by_rate(file_digest(args, ref), args.file_sample_rate)]
    if args.max_files is not None:
        return filtered[: args.max_files]
    return filtered


@dataclass(frozen=True)
class TextSource:
    source_ref: str
    provenance_ref: str
    content: str
    content_sha256: str
    size_bytes: int


@dataclass(frozen=True)
class TextSourceHandle:
    provenance_ref: str
    content_sha256: str
    size_bytes: int
    load_bytes: Callable[[], bytes]


def canonical_sources(sources: list[TextSource]) -> list[TextSource]:
    canonical: list[TextSource] = []
    for index, source in enumerate(
        sorted(sources, key=lambda item: (item.content_sha256, item.size_bytes, item.content)),
        start=1,
    ):
        canonical.append(
            TextSource(
                source_ref=f"content-source-{source.content_sha256[:16]}-{index:06d}",
                provenance_ref=source.provenance_ref,
                content=source.content,
                content_sha256=source.content_sha256,
                size_bytes=source.size_bytes,
            )
        )
    return canonical


def selected_sources(args: argparse.Namespace, sources: list[TextSource]) -> list[TextSource]:
    ordered = sorted(
        sources,
        key=lambda source: (
            source.source_ref if args.file_order == "path" else file_digest(args, source.source_ref),
            source.content_sha256,
        ),
    )
    filtered = [
        source
        for source in ordered
        if include_by_rate(file_digest(args, source.source_ref), args.file_sample_rate)
    ]
    if args.max_files is not None:
        return filtered[: args.max_files]
    return filtered


def can_stream_sources(args: argparse.Namespace) -> bool:
    return args.sampling_mode == "streaming-target"


def text_source_from_bytes(
    display_ref: str,
    data: bytes,
    source_index: int,
    *,
    source_ref: str | None = None,
) -> TextSource:
    content_sha256 = sha256_bytes(data)
    try:
        content = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ImportErrorWithLocation(f"{display_ref}: cannot decode UTF-8") from error
    return TextSource(
        source_ref=source_ref or f"content-source-{content_sha256[:16]}-{source_index:06d}",
        provenance_ref=display_ref,
        content=content,
        content_sha256=content_sha256,
        size_bytes=len(data),
    )


def sha256_stream(stream: object) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    while True:
        chunk = stream.read(1024 * 1024)  # type: ignore[attr-defined]
        if not chunk:
            break
        digest.update(chunk)
        size += len(chunk)
    return digest.hexdigest(), size


def text_file_handle(path: Path, provenance_ref: str) -> TextSourceHandle:
    with path.open("rb") as handle:
        content_sha256, size_bytes = sha256_stream(handle)
    return TextSourceHandle(
        provenance_ref=provenance_ref,
        content_sha256=content_sha256,
        size_bytes=size_bytes,
        load_bytes=lambda path=path: path.read_bytes(),
    )


def load_zip_member_bytes(path: Path, index: int) -> bytes:
    with zipfile.ZipFile(path) as archive:
        info = archive.infolist()[index]
        with archive.open(info) as raw:
            return raw.read()


def zip_text_source_handles(path: Path, display_prefix: str) -> list[TextSourceHandle]:
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile as error:
        raise ImportErrorWithLocation(f"{path}: invalid zip archive") from error
    handles: list[TextSourceHandle] = []
    with archive:
        for index, info in enumerate(archive.infolist()):
            if info.is_dir() or not info.filename.lower().endswith(".txt"):
                continue
            with archive.open(info) as raw:
                content_sha256, size_bytes = sha256_stream(raw)
            handles.append(
                TextSourceHandle(
                    provenance_ref=f"{display_prefix}!{info.filename}",
                    content_sha256=content_sha256,
                    size_bytes=size_bytes,
                    load_bytes=lambda path=path, index=index: load_zip_member_bytes(path, index),
                )
            )
    if not handles:
        raise ImportErrorWithLocation(f"{path}: zip archive contains no .txt files")
    return handles


def input_source_handles(path: Path) -> list[TextSourceHandle]:
    if not path.exists():
        raise ImportErrorWithLocation(f"{path}: input does not exist")
    if path.is_dir():
        handles: list[TextSourceHandle] = []
        text_paths = [child for child in path.rglob("*.txt") if child.is_file()]
        zip_paths = [child for child in path.rglob("*.zip") if child.is_file()]
        if not text_paths and not zip_paths:
            raise ImportErrorWithLocation(f"{path}: directory contains no .txt or .zip files")
        for text_path in text_paths:
            handles.append(text_file_handle(text_path, str(text_path.relative_to(path))))
        for zip_path in zip_paths:
            handles.extend(zip_text_source_handles(zip_path, str(zip_path.relative_to(path))))
        return handles
    if path.suffix.lower() == ".zip":
        return zip_text_source_handles(path, path.name)
    return [text_file_handle(path, path.name)]


def selected_canonical_source_handles(
    args: argparse.Namespace,
    handles: list[TextSourceHandle],
) -> list[tuple[str, TextSourceHandle]]:
    canonical = [
        (f"content-source-{handle.content_sha256[:16]}-{index:06d}", handle)
        for index, handle in enumerate(
            sorted(handles, key=lambda item: (item.content_sha256, item.size_bytes)),
            start=1,
        )
    ]
    ordered = sorted(
        canonical,
        key=lambda item: item[0] if args.file_order == "path" else file_digest(args, item[0]),
    )
    filtered = [
        item for item in ordered if include_by_rate(file_digest(args, item[0]), args.file_sample_rate)
    ]
    if args.max_files is not None:
        return filtered[: args.max_files]
    return filtered


def process_streaming_target_inputs(
    args: argparse.Namespace,
    input_paths: list[Path],
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    handles: list[TextSourceHandle] = []
    for path in input_paths:
        handles.extend(input_source_handles(path))
    summary.source_files_seen += len(handles)
    for source_ref, handle in selected_canonical_source_handles(args, handles):
        if processing_limit_reached(args, summary, rows):
            break
        data = handle.load_bytes()
        source = text_source_from_bytes(
            handle.provenance_ref,
            data,
            summary.source_files_processed + 1,
            source_ref=source_ref,
        )
        process_text_source(args, source, summary, rows, progress)


def process_text_source(
    args: argparse.Namespace,
    source: TextSource,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    process_text_stream(
        args,
        source.source_ref,
        io.StringIO(source.content),
        summary,
        rows,
        progress,
    )


def should_replay_game(args: argparse.Namespace, summary: Summary, game_key: str) -> bool:
    if not include_by_rate(game_key, args.game_sample_rate):
        summary.replay_skip_count += 1
        return False
    return True


def replay_limit_reached(args: argparse.Namespace, summary: Summary) -> bool:
    return args.max_games is not None and summary.games_replayed >= args.max_games


def processing_limit_reached(args: argparse.Namespace, summary: Summary, rows: TopKRows) -> bool:
    if replay_limit_reached(args, summary):
        return True
    return args.sampling_mode == "streaming-target" and rows.limit_reached()


def maybe_report_progress(
    args: argparse.Namespace,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
    *,
    force: bool = False,
) -> None:
    if args.progress_every_games is None and args.progress_every_files is None:
        return
    games_due = (
        args.progress_every_games is not None
        and summary.games_seen - progress.last_games_report >= args.progress_every_games
    )
    files_due = (
        args.progress_every_files is not None
        and summary.source_files_processed - progress.last_files_report >= args.progress_every_files
    )
    if not force and not games_due and not files_due:
        return
    elapsed = max(time.monotonic() - progress.start_time, 1e-9)
    games_per_sec = summary.games_replayed / elapsed
    print(
        "progress "
        f"elapsed_sec={elapsed:.3f} "
        f"source_files_seen={summary.source_files_seen} "
        f"source_files_processed={summary.source_files_processed} "
        f"games_seen={summary.games_seen} "
        f"games_replayed={summary.games_replayed} "
        f"accepted_games={summary.accepted_games} "
        f"rejected_games={summary.rejected_games} "
        f"candidate_positions={summary.candidate_positions} "
        f"kept_positions={rows.kept_count()} "
        f"games_per_sec={games_per_sec:.3f}",
        file=sys.stderr,
    )
    progress.last_games_report = summary.games_seen
    progress.last_files_report = summary.source_files_processed


def load_manifest_dataset_id(path: Path) -> str:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ImportErrorWithLocation(f"{path}: manifest is not valid JSON: {error}") from error
    except OSError as error:
        raise ImportErrorWithLocation(f"{path}: cannot read manifest: {error}") from error
    if not isinstance(manifest, dict):
        raise ImportErrorWithLocation(f"{path}: manifest root must be a JSON object")
    dataset_id = manifest.get("dataset_id")
    if not isinstance(dataset_id, str) or not dataset_id:
        raise ImportErrorWithLocation(f"{path}: manifest dataset_id must be a non-empty string")
    return dataset_id


def should_emit(ply: int, is_terminal: bool, args: argparse.Namespace) -> bool:
    if ply < args.min_ply:
        return False
    if args.max_ply is not None and ply > args.max_ply:
        return False
    if (ply - args.min_ply) % args.ply_stride != 0:
        return False
    if is_terminal and not args.emit_terminal:
        return False
    return True


def make_row(
    args: argparse.Namespace,
    game_group_id: str,
    source_occurrence_id: str,
    split: str,
    snapshot: ReplaySnapshot,
    ply: int,
    occurrence: int,
) -> CandidateRow:
    board_text = snapshot.board_text
    label = snapshot.label
    board_id = prefixed_id("board", digest_for_parts("board-v1", board_text))
    position_digest = digest_for_parts(args.dataset_id, game_group_id, ply, board_id)
    record_digest = digest_for_parts(
        args.dataset_id, source_occurrence_id, game_group_id, ply, board_id, label, occurrence
    )
    record_id = f"{args.dataset_id}-{record_digest[:16]}-{occurrence:06d}"
    position_id = f"{args.dataset_id}-{game_group_id}-{ply:03d}-{position_digest[:16]}"
    player_count = snapshot.player_disc_count
    opponent_count = snapshot.opponent_disc_count
    empty_count = snapshot.empty_count
    occupied_count = player_count + opponent_count
    phase = phase_for_occupied_count(occupied_count)
    fields = (
        record_id,
        position_id,
        game_group_id,
        board_id,
        source_occurrence_id,
        args.dataset_id,
        split,
        board_text,
        LABEL_KIND,
        LABEL_UNIT,
        LABEL_PERSPECTIVE,
        str(label),
        str(occupied_count),
        str(phase),
        str(player_count),
        str(opponent_count),
        str(empty_count),
    )
    line_text = "\t".join(fields)
    sample_key = digest_for_parts(args.seed, "sample-v1", game_group_id, position_id)
    return CandidateRow(
        key=sample_key,
        ordinal=occurrence,
        fields=fields,
        line_text=line_text,
        position_id=position_id,
        game_group_id=game_group_id,
        board_id=board_id,
        split=split,
        phase=phase,
        label=label,
    )


def replay_game(
    args: argparse.Namespace, source_occurrence_id: str, text: str
) -> tuple[list[CandidateRow], int, bool, str]:
    replay_helper = getattr(args, "replay_helper_client", None)
    if replay_helper is None:
        raise ImportErrorWithLocation("Egaroucid replay helper is not configured")
    result = replay_helper.replay(source_occurrence_id, text)
    if not result.accepted:
        raise ImportErrorWithLocation(result.error)

    game_group_id = prefixed_id(
        "game", digest_for_parts(args.dataset_id, IDENTITY_POLICY_VERSION, result.canonical_moves)
    )
    split = split_for_digest(digest_for_parts(args.dataset_id, game_group_id))
    rows: list[CandidateRow] = []
    occurrence = 0
    final_ply = result.snapshots[-1].ply if result.snapshots else 0
    for snapshot in result.snapshots:
        is_terminal_snapshot = snapshot.ply == final_ply
        if not should_emit(snapshot.ply, is_terminal_snapshot, args):
            continue
        occurrence += 1
        rows.append(
            make_row(
                args,
                game_group_id,
                source_occurrence_id,
                split,
                snapshot,
                snapshot.ply,
                occurrence,
            )
        )
    return rows, result.pass_count, result.terminal, game_group_id


def process_text_stream(
    args: argparse.Namespace,
    source_ref: str,
    lines: Iterable[str],
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    summary.source_files += 1
    summary.source_files_processed += 1
    for line_number, line in enumerate(lines, start=1):
        if processing_limit_reached(args, summary, rows):
            break
        text = line.strip()
        if not text:
            continue
        summary.games_seen += 1
        transcript_sha = sha256_text(text)
        transcript_key = digest_for_parts(args.dataset_id, "transcript-v1", transcript_sha, text)
        game_key = digest_for_parts(args.seed, "game", transcript_key)
        if not should_replay_game(args, summary, game_key):
            maybe_report_progress(args, summary, rows, progress)
            continue
        summary.input_games += 1
        summary.games_replayed += 1
        occurrence_index = summary.transcript_occurrences.get(transcript_key, 0) + 1
        summary.transcript_occurrences[transcript_key] = occurrence_index
        source_occurrence_id = prefixed_id(
            "occ",
            digest_for_parts(
                args.dataset_id,
                "content-occurrence-v1",
                transcript_key,
                occurrence_index,
            ),
        )
        if len(summary.provenance_samples) < 20:
            summary.provenance_samples.append(
                {
                    "source_occurrence_id": source_occurrence_id,
                    "source_ref": source_ref,
                    "line_number": line_number,
                    "transcript_sha256": transcript_sha,
                }
            )
        try:
            emitted, pass_count, terminal, game_group_id = replay_game(args, source_occurrence_id, text)
        except ImportErrorWithLocation as error:
            summary.rejected_games += 1
            summary.invalid_move_count += 1
            if len(summary.rejected_reasons) < 20:
                summary.rejected_reasons.append(f"{source_ref}:{line_number}: {error}")
            continue
        summary.accepted_games += 1
        summary.game_group_occurrences[game_group_id] = (
            summary.game_group_occurrences.get(game_group_id, 0) + 1
        )
        summary.pass_count += pass_count
        if terminal:
            summary.terminal_count += 1
        for row in emitted:
            if processing_limit_reached(args, summary, rows):
                break
            summary.candidate_positions += 1
            row.ordinal = summary.candidate_positions
            rows.consider(row)
        maybe_report_progress(args, summary, rows, progress)
    maybe_report_progress(args, summary, rows, progress)


def process_zip(
    args: argparse.Namespace,
    path: Path,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    if can_stream_sources(args):
        process_zip_streaming(args, path, path.name, summary, rows, progress)
        return
    sources = canonical_sources(zip_text_sources(path, path.name))
    summary.source_files_seen += len(sources)
    for source in selected_sources(args, sources):
        if replay_limit_reached(args, summary):
            break
        process_text_stream(
            args,
            source.source_ref,
            io.StringIO(source.content),
            summary,
            rows,
            progress,
        )


def process_zip_streaming(
    args: argparse.Namespace,
    path: Path,
    display_prefix: str,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile as error:
        raise ImportErrorWithLocation(f"{path}: invalid zip archive") from error
    with archive:
        text_entries = [
            info for info in archive.infolist() if not info.is_dir() and info.filename.lower().endswith(".txt")
        ]
        if not text_entries:
            raise ImportErrorWithLocation(f"{path}: zip archive contains no .txt files")
        info_by_ref = {f"{display_prefix}!{info.filename}": info for info in text_entries}
        selected_refs = selected_file_refs(args, list(info_by_ref))
        for source_ref in selected_refs:
            if processing_limit_reached(args, summary, rows):
                break
            info = info_by_ref[source_ref]
            with archive.open(info) as raw:
                data = raw.read()
            summary.source_files_seen += 1
            source = text_source_from_bytes(source_ref, data, summary.source_files_seen)
            process_text_source(args, source, summary, rows, progress)


def zip_text_sources(path: Path, display_prefix: str) -> list[TextSource]:
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile as error:
        raise ImportErrorWithLocation(f"{path}: invalid zip archive") from error
    with archive:
        text_entries = [info for info in archive.infolist() if not info.is_dir() and info.filename.lower().endswith(".txt")]
        if not text_entries:
            raise ImportErrorWithLocation(f"{path}: zip archive contains no .txt files")
        raw_sources: list[TextSource] = []
        for info in text_entries:
            with archive.open(info) as raw:
                data = raw.read()
            try:
                content = data.decode("utf-8")
            except UnicodeDecodeError as error:
                raise ImportErrorWithLocation(f"{path}!{info.filename}: cannot decode UTF-8") from error
            raw_sources.append(
                TextSource(
                    source_ref="",
                    provenance_ref=f"{display_prefix}!{info.filename}",
                    content=content,
                    content_sha256=sha256_bytes(data),
                    size_bytes=len(data),
                )
            )
    return raw_sources


def process_plain_file(
    args: argparse.Namespace,
    path: Path,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
    *,
    counted: bool = False,
) -> None:
    if not counted:
        summary.source_files_seen += 1
    data = path.read_bytes()
    if can_stream_sources(args):
        source = text_source_from_bytes(path.name, data, summary.source_files_seen)
    else:
        try:
            content = data.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ImportErrorWithLocation(f"{path}: cannot decode UTF-8") from error
        source = canonical_sources(
            [
                TextSource(
                    source_ref="",
                    provenance_ref=path.name,
                    content=content,
                    content_sha256=sha256_bytes(data),
                    size_bytes=len(data),
                )
            ]
        )[0]
    process_text_source(args, source, summary, rows, progress)


def process_directory(
    args: argparse.Namespace,
    path: Path,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    text_paths = [child for child in path.rglob("*.txt") if child.is_file()]
    zip_paths = [child for child in path.rglob("*.zip") if child.is_file()]
    if not text_paths and not zip_paths:
        raise ImportErrorWithLocation(f"{path}: directory contains no .txt or .zip files")
    if can_stream_sources(args):
        text_by_ref = {str(text_path.relative_to(path)): text_path for text_path in text_paths}
        for source_ref in selected_file_refs(args, list(text_by_ref)):
            if processing_limit_reached(args, summary, rows):
                break
            text_path = text_by_ref[source_ref]
            data = text_path.read_bytes()
            summary.source_files_seen += 1
            source = text_source_from_bytes(
                source_ref,
                data,
                summary.source_files_seen,
            )
            process_text_source(args, source, summary, rows, progress)
        zip_by_ref = {str(zip_path.relative_to(path)): zip_path for zip_path in zip_paths}
        for source_ref in selected_file_refs(args, list(zip_by_ref)):
            if processing_limit_reached(args, summary, rows):
                break
            zip_path = zip_by_ref[source_ref]
            process_zip_streaming(
                args,
                zip_path,
                source_ref,
                summary,
                rows,
                progress,
            )
        return
    raw_sources: list[TextSource] = []
    for text_path in text_paths:
        data = text_path.read_bytes()
        try:
            content = data.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ImportErrorWithLocation(f"{text_path}: cannot decode UTF-8") from error
        raw_sources.append(
            TextSource(
                source_ref="",
                provenance_ref=str(text_path.relative_to(path)),
                content=content,
                content_sha256=sha256_bytes(data),
                size_bytes=len(data),
            )
        )
    for zip_path in zip_paths:
        raw_sources.extend(zip_text_sources(zip_path, str(zip_path.relative_to(path))))
    sources = canonical_sources(raw_sources)
    summary.source_files_seen += len(sources)
    for source in selected_sources(args, sources):
        if replay_limit_reached(args, summary):
            break
        process_text_stream(
            args,
            source.source_ref,
            io.StringIO(source.content),
            summary,
            rows,
            progress,
        )


def process_input(
    args: argparse.Namespace,
    path: Path,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    if not path.exists():
        raise ImportErrorWithLocation(f"{path}: input does not exist")
    if path.is_dir():
        process_directory(args, path, summary, rows, progress)
    elif path.suffix.lower() == ".zip":
        process_zip(args, path, summary, rows, progress)
    else:
        process_plain_file(args, path, summary, rows, progress)


def split_pair(left: str, right: str) -> str:
    return "__".join(sorted((left, right)))


def cross_split_audit(
    entity_splits: dict[str, set[str]],
    *,
    entity_label: str,
    collision_label: str,
) -> dict[str, object]:
    pair_counts: dict[str, int] = {}
    collision_count = 0
    for splits in entity_splits.values():
        if len(splits) < 2:
            continue
        collision_count += 1
        ordered = sorted(splits)
        for left_index, left in enumerate(ordered):
            for right in ordered[left_index + 1 :]:
                pair = split_pair(left, right)
                pair_counts[pair] = pair_counts.get(pair, 0) + 1
    return {
        f"unique_{entity_label}_count": len(entity_splits),
        f"cross_split_{collision_label}_collision_count": collision_count,
        f"cross_split_{collision_label}_collision_counts_by_pair": dict(sorted(pair_counts.items())),
    }


def leakage_audits(selected_rows: list[CandidateRow]) -> dict[str, dict[str, object]]:
    board_splits: dict[str, set[str]] = {}
    game_splits: dict[str, set[str]] = {}
    for row in selected_rows:
        board_splits.setdefault(row.board_id, set()).add(row.split)
        game_splits.setdefault(row.game_group_id, set()).add(row.split)
    return {
        "board": cross_split_audit(
            board_splits,
            entity_label="board",
            collision_label="board",
        ),
        "game_group": cross_split_audit(
            game_splits,
            entity_label="game_group",
            collision_label="game_group",
        ),
    }


def component_split(source_dataset_ids: set[str], representative: str, policy_version: str) -> str:
    dataset_id = ",".join(sorted(source_dataset_ids))
    digest = hashlib.sha256(
        f"{dataset_id}\t{policy_version}\t{representative}".encode("utf-8")
    ).hexdigest()
    bucket = int(digest[:16], 16) % 100
    if bucket < 80:
        return "train"
    if bucket < 90:
        return "validation"
    return "test"


def with_split(row: CandidateRow, split: str) -> CandidateRow:
    fields = list(row.fields)
    fields[6] = split
    return CandidateRow(
        key=row.key,
        ordinal=row.ordinal,
        fields=tuple(fields),
        line_text="\t".join(fields),
        position_id=row.position_id,
        game_group_id=row.game_group_id,
        board_id=row.board_id,
        split=split,
        phase=row.phase,
        label=row.label,
    )


def connected_board_game_resplit(selected_rows: list[CandidateRow]) -> tuple[list[CandidateRow], dict[str, object]]:
    policy = "connected-board-game"
    policy_version = SPLIT_POLICY_VERSIONS[policy]
    union_find = UnionFind()
    for row in selected_rows:
        union_find.union(f"game:{row.game_group_id}", f"board:{row.board_id}")

    component_infos: dict[str, ComponentInfo] = {}
    for row in selected_rows:
        game_node = f"game:{row.game_group_id}"
        board_node = f"board:{row.board_id}"
        root = union_find.find(game_node)
        info = component_infos.setdefault(root, ComponentInfo())
        info.row_count += 1
        info.nodes.add(game_node)
        info.nodes.add(board_node)
        info.source_dataset_ids.add(row.fields[5])

    component_splits: dict[str, str] = {}
    largest_component_row_count = 0
    largest_component_node_count = 0
    for root, info in component_infos.items():
        representative = min(info.nodes)
        component_splits[root] = component_split(
            info.source_dataset_ids,
            representative,
            policy_version,
        )
        largest_component_row_count = max(largest_component_row_count, info.row_count)
        largest_component_node_count = max(largest_component_node_count, len(info.nodes))

    resplit_rows: list[CandidateRow] = []
    for row in selected_rows:
        root = union_find.find(f"game:{row.game_group_id}")
        resplit_rows.append(with_split(row, component_splits[root]))

    return resplit_rows, {
        "connected_component_count": len(component_infos),
        "largest_connected_component_row_count": largest_component_row_count,
        "largest_connected_component_node_count": largest_component_node_count,
    }


def apply_split_policy(selected_rows: list[CandidateRow], args: argparse.Namespace) -> tuple[list[CandidateRow], dict[str, object]]:
    if args.split_policy == "game-group":
        return selected_rows, {
            "measurement_split_policy": "game-group",
            "measurement_split_policy_version": SPLIT_POLICY_VERSIONS["game-group"],
            "connected_component_count": None,
            "largest_connected_component_row_count": None,
            "largest_connected_component_node_count": None,
        }
    return connected_board_game_resplit(selected_rows)


def write_rows(selected_rows: list[CandidateRow], output: TextIO, summary: Summary) -> str:
    writer = csv.writer(output, delimiter="\t", lineterminator="\n")
    writer.writerow(HEADER)
    digest = hashlib.sha256()
    for row in selected_rows:
        writer.writerow(row.fields)
        digest.update(row.line_text.encode("utf-8"))
        digest.update(b"\n")
        summary.emitted_positions += 1
        summary.split_counts[row.split] = summary.split_counts.get(row.split, 0) + 1
        phase_key = str(row.phase)
        summary.phase_counts[phase_key] = summary.phase_counts.get(phase_key, 0) + 1
        summary.label_min = row.label if summary.label_min is None else min(summary.label_min, row.label)
        summary.label_max = row.label if summary.label_max is None else max(summary.label_max, row.label)
        summary.label_sum += row.label
    return f"sha256:{digest.hexdigest()}"


def report_for(
    args: argparse.Namespace,
    summary: Summary,
    checksum: str,
    audit_before: dict[str, dict[str, object]],
    audit_after: dict[str, dict[str, object]],
    split_report: dict[str, object],
) -> dict[str, object]:
    label_mean = None
    if summary.emitted_positions:
        label_mean = float(f"{summary.label_sum / summary.emitted_positions:.12g}")
    duplicate_game_occurrences = sum(
        max(0, count - 1) for count in summary.game_group_occurrences.values()
    )
    if args.sampling_mode == "bounded-dev":
        sampling_frame_notes = list(BOUNDED_DEV_NOTES)
    elif args.sampling_mode == "streaming-target":
        sampling_frame_notes = list(STREAMING_TARGET_NOTES)
    else:
        sampling_frame_notes = []
    if args.sampling_mode == "streaming-target":
        sample_policy = (
            "deterministic content-addressed source traversal after source fingerprinting "
            "until max_positions retained rows; "
            "not full-corpus exact top-k"
        )
    else:
        sample_policy = (
            "deterministic position_id group sha256 top-k when max_positions is set; "
            "duplicate records for selected position_id are preserved"
        )
    notes = list(NOTES)
    notes.extend(sampling_frame_notes)
    board_after = audit_after["board"]
    game_after = audit_after["game_group"]
    if args.split_policy == "connected-board-game":
        split_method = "connected-board-game component sha256"
        game_leakage_policy = (
            "all emitted positions from connected components of semantic games and exact boards "
            "stay in one split"
        )
        exact_board_audit = (
            "side-to-move-relative board_id collisions are audited before and after connected resplit"
        )
    else:
        split_method = "dataset_id + game_group_id sha256"
        game_leakage_policy = "all emitted positions from one semantic game stay in one split"
        exact_board_audit = "side-to-move-relative board_id collisions across splits are counted"
    return {
        "schema_version": 2,
        "importer_version": IMPORTER_VERSION,
        "identity_policy_version": IDENTITY_POLICY_VERSION,
        "source_dataset_id": args.dataset_id,
        "source_kind": SOURCE_KIND,
        "input_kind": "sequence-transcript",
        "sampling_mode": args.sampling_mode,
        "file_order": args.file_order,
        "max_files": args.max_files,
        "max_games": args.max_games,
        "file_sample_rate": args.file_sample_rate,
        "game_sample_rate": args.game_sample_rate,
        "source_files_seen": summary.source_files_seen,
        "source_files_processed": summary.source_files_processed,
        "games_seen": summary.games_seen,
        "games_replayed": summary.games_replayed,
        "replay_skip_count": summary.replay_skip_count,
        "candidate_positions": summary.candidate_positions,
        "retained_positions": summary.emitted_positions,
        "target_limit_reached": args.sampling_mode == "streaming-target"
        and args.max_positions is not None
        and summary.emitted_positions >= args.max_positions,
        "sampling_frame_notes": sampling_frame_notes,
        "input_games": summary.input_games,
        "accepted_games": summary.accepted_games,
        "rejected_games": summary.rejected_games,
        "emitted_positions": summary.emitted_positions,
        "invalid_move_count": summary.invalid_move_count,
        "pass_count": summary.pass_count,
        "terminal_count": summary.terminal_count,
        "split_counts": summary.split_counts,
        "phase_counts": summary.phase_counts,
        "label_min": summary.label_min,
        "label_max": summary.label_max,
        "label_mean": label_mean,
        "checksum": checksum,
        "source_files": summary.source_files,
        "game_group_count": len(summary.game_group_occurrences),
        "duplicate_game_occurrence_count": duplicate_game_occurrences,
        "unique_board_count": board_after["unique_board_count"],
        "cross_split_board_collision_count": board_after["cross_split_board_collision_count"],
        "cross_split_board_collision_counts_by_pair": board_after[
            "cross_split_board_collision_counts_by_pair"
        ],
        "unique_game_group_count": game_after["unique_game_group_count"],
        "cross_split_game_group_collision_count": game_after[
            "cross_split_game_group_collision_count"
        ],
        "cross_split_game_group_collision_counts_by_pair": game_after[
            "cross_split_game_group_collision_counts_by_pair"
        ],
        "board_leakage_audit_before": audit_before["board"],
        "board_leakage_audit_after": board_after,
        "game_group_leakage_audit_before": audit_before["game_group"],
        "game_group_leakage_audit_after": game_after,
        "connected_component_count": split_report.get("connected_component_count"),
        "largest_connected_component_row_count": split_report.get(
            "largest_connected_component_row_count"
        ),
        "largest_connected_component_node_count": split_report.get(
            "largest_connected_component_node_count"
        ),
        "rejected_reasons": summary.rejected_reasons,
        "source_provenance_samples": summary.provenance_samples,
        "sampling_policy": {
            "mode": args.sampling_mode,
            "file_order": args.file_order,
            "max_files": args.max_files,
            "max_games": args.max_games,
            "file_sample_rate": args.file_sample_rate,
            "game_sample_rate": args.game_sample_rate,
            "seed": args.seed,
            "game_filter_stage": "before legal replay",
        },
        "emit_policy": {
            "min_ply": args.min_ply,
            "max_ply": args.max_ply,
            "ply_stride": args.ply_stride,
            "max_positions": args.max_positions,
            "seed": args.seed,
            "emit_terminal": args.emit_terminal,
            "sample_policy": sample_policy,
        },
        "split_policy": {
            "method": split_method,
            "measurement_split_policy": args.split_policy,
            "measurement_split_policy_version": SPLIT_POLICY_VERSIONS[args.split_policy],
            "ratio": "80/10/10 by hash bucket",
            "game_group_id": "sha256 of dataset_id + identity_policy_version + canonical replayed move/pass sequence",
            "source_occurrence_id": "sha256 of dataset_id + transcript content digest + content occurrence index",
            "position_id": "dataset_id + game_group_id + ply + board_id hash",
            "board_id": "sha256 of side-to-move-relative 64-cell board only",
            "game_leakage_policy": game_leakage_policy,
            "duplicate_position_policy": "duplicate semantic games share game_group_id, position_id, board_id, and split; content occurrences keep distinct source_occurrence_id and record_id",
        },
        "leakage_policy": {
            "exact_board_audit": exact_board_audit,
            "strict_board_disjoint_splits": args.strict_board_disjoint_splits,
        },
        "notes": notes,
    }


def main() -> int:
    args = parse_args()
    summary = Summary()
    row_mode = "streaming" if args.sampling_mode == "streaming-target" else "topk"
    rows = TopKRows(args.max_positions, mode=row_mode)
    progress = ProgressState()
    try:
        manifest_dataset_id = load_manifest_dataset_id(args.manifest)
        if manifest_dataset_id != args.dataset_id:
            raise ImportErrorWithLocation(
                f"{args.manifest}: manifest dataset_id {manifest_dataset_id!r} does not match "
                f"--dataset-id {args.dataset_id!r}"
            )
        with ReplayHelper(args.replay_helper) as replay_helper:
            args.replay_helper_client = replay_helper
            if args.sampling_mode == "streaming-target":
                process_streaming_target_inputs(args, args.input, summary, rows, progress)
            else:
                for input_path in args.input:
                    if processing_limit_reached(args, summary, rows):
                        break
                    process_input(args, input_path, summary, rows, progress)
        selected_rows = rows.rows()
        if not selected_rows:
            raise ImportErrorWithLocation("no positions emitted")
        audit_before = leakage_audits(selected_rows)
        selected_rows, split_report = apply_split_policy(selected_rows, args)
        audit_after = leakage_audits(selected_rows)
        board_after = audit_after["board"]
        game_after = audit_after["game_group"]
        if args.split_policy == "connected-board-game":
            if board_after["cross_split_board_collision_count"] != 0:
                raise ImportErrorWithLocation(
                    "connected-board-game failed to eliminate board_id cross-split leakage"
                )
            if game_after["cross_split_game_group_collision_count"] != 0:
                raise ImportErrorWithLocation(
                    "connected-board-game failed to eliminate game_group_id cross-split leakage"
                )
        if args.strict_board_disjoint_splits and board_after["cross_split_board_collision_count"] != 0:
            raise ImportErrorWithLocation(
                "exact board leakage detected across splits: "
                f"{board_after['cross_split_board_collision_count']} board_id collision(s)"
            )
        checksum = write_rows(selected_rows, sys.stdout, summary)
        report = report_for(args, summary, checksum, audit_before, audit_after, split_report)
        if args.report is not None:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(stable_json(report), encoding="utf-8")
        maybe_report_progress(args, summary, rows, progress, force=True)
    except (OSError, UnicodeDecodeError, ImportErrorWithLocation) as error:
        print(error, file=sys.stderr)
        return 1

    print(
        "summary "
        f"source_files={summary.source_files} "
        f"source_files_seen={summary.source_files_seen} "
        f"source_files_processed={summary.source_files_processed} "
        f"games_seen={summary.games_seen} "
        f"games_replayed={summary.games_replayed} "
        f"replay_skip_count={summary.replay_skip_count} "
        f"input_games={summary.input_games} "
        f"accepted_games={summary.accepted_games} "
        f"rejected_games={summary.rejected_games} "
        f"emitted_positions={summary.emitted_positions} "
        f"invalid_move_count={summary.invalid_move_count} "
        f"pass_count={summary.pass_count} "
        f"terminal_count={summary.terminal_count} "
        f"source_kind={SOURCE_KIND} checksum={checksum}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
