#!/usr/bin/env python3
"""Run local-only Egaroucid subset training through export and smoke checks."""

from __future__ import annotations

import argparse
import csv
import hashlib
import heapq
import json
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


NORMALIZED_HEADER = [
    "record_id",
    "position_id",
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
]
SPLITS = ("train", "validation", "test")
LOCAL_NOTES = [
    "local run only",
    "not production benchmark",
    "not strength claim",
    "Egaroucid-derived artifacts are not committed",
    "publication remains gated / unknown",
]


@dataclass(frozen=True)
class NormalizedRow:
    fields: list[str]
    line_text: str
    record_id: str
    position_id: str
    source_dataset_id: str
    split: str
    label: int
    phase: int


class TopKPositionIds:
    def __init__(self, capacity: int | None) -> None:
        self.capacity = capacity
        self.entries: dict[str, str] = {}
        self.heap: list[tuple[int, str, str]] = []

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

    def consider(self, position_id: str, key: str) -> None:
        if self.capacity is None:
            return
        if self.capacity <= 0:
            return
        current = self.entries.get(position_id)
        if current is not None:
            if key < current:
                self.push(position_id, key)
            return
        if len(self.entries) < self.capacity:
            self.push(position_id, key)
            return
        self.discard_stale_worst_entries()
        if not self.heap:
            self.push(position_id, key)
            return
        _, worst_position_id, worst_key = self.heap[0]
        if key < worst_key:
            del self.entries[worst_position_id]
            heapq.heappop(self.heap)
            self.push(position_id, key)

    def allows(self, position_id: str) -> bool:
        return self.capacity is None or position_id in self.entries


@dataclass
class SampleSummary:
    input_rows: int = 0
    sampled_rows: int = 0
    counts_by_split: dict[str, int] | None = None
    counts_by_phase: dict[str, int] | None = None
    source_dataset_ids: set[str] | None = None
    label_min: int | None = None
    label_max: int | None = None
    label_sum: int = 0
    checksum: str = ""

    def __post_init__(self) -> None:
        if self.counts_by_split is None:
            self.counts_by_split = {split: 0 for split in SPLITS}
        if self.counts_by_phase is None:
            self.counts_by_phase = {}
        if self.source_dataset_ids is None:
            self.source_dataset_ids = set()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument("--normalized-tsv", type=Path)
    input_group.add_argument("--raw-input", type=Path)
    input_group.add_argument("--sequence-input", type=Path)
    parser.add_argument("--manifest", type=Path, help="Required with --raw-input.")
    parser.add_argument("--sequence-manifest", type=Path, help="Required with --sequence-input.")
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--run-id", help="Stable run id. Defaults to UTC timestamp plus input hash.")
    parser.add_argument("--created-at-utc", help="Override timestamp for deterministic smoke tests.")
    parser.add_argument("--max-examples", type=int)
    parser.add_argument("--max-per-phase", type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--split-policy", choices=("preserve",), default="preserve")
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.1)
    parser.add_argument("--l2", type=float, default=0.0)
    parser.add_argument("--skip-eval-smoke", action="store_true")
    parser.add_argument("--skip-search-smoke", action="store_true")
    parser.add_argument("--skip-v0a-baseline", action="store_true")
    parser.add_argument("--sequence-min-ply", type=int, default=8)
    parser.add_argument("--sequence-max-ply", type=int)
    parser.add_argument("--sequence-ply-stride", type=int, default=1)
    parser.add_argument("--sequence-max-positions", type=int)
    parser.add_argument(
        "--sequence-emit-terminal",
        dest="sequence_emit_terminal",
        action="store_true",
        default=False,
    )
    parser.add_argument("--sequence-no-emit-terminal", dest="sequence_emit_terminal", action="store_false")
    parser.add_argument("--eval-smoke-max-positions", type=int)
    parser.add_argument("--search-smoke-max-positions", type=int)
    parser.add_argument(
        "--importer",
        type=Path,
        default=root / "tools/data-import/import_egaroucid_train_data.py",
    )
    parser.add_argument(
        "--sequence-importer",
        type=Path,
        default=root / "tools/data-import/import_egaroucid_sequences.py",
    )
    parser.add_argument(
        "--trainer",
        type=Path,
        default=root / "tools/pattern/train/train_v0a.py",
    )
    parser.add_argument(
        "--v0a-exporter",
        type=Path,
        default=root / "tools/pattern/export/export_v0a.py",
    )
    parser.add_argument(
        "--v0b-exporter",
        type=Path,
        default=root / "tools/pattern/export/export_v0b.py",
    )
    parser.add_argument(
        "--dataset-exe",
        type=Path,
        default=root / "build/tools/pattern/dataset/vibe-othello-pattern-dataset-smoke",
    )
    parser.add_argument(
        "--eval-smoke-exe",
        type=Path,
        default=root / "build/tools/pattern/export/vibe-othello-pattern-evaluation-bench-smoke",
    )
    parser.add_argument(
        "--search-smoke-exe",
        type=Path,
        default=root / "build/tools/pattern/export/vibe-othello-pattern-search-bench-smoke",
    )
    args = parser.parse_args()
    if args.raw_input is not None and args.manifest is None:
        parser.error("--raw-input requires --manifest")
    if args.sequence_input is not None and args.sequence_manifest is None:
        parser.error("--sequence-input requires --sequence-manifest")
    if args.max_examples is not None and args.max_examples <= 0:
        parser.error("--max-examples must be positive")
    if args.max_per_phase is not None and args.max_per_phase <= 0:
        parser.error("--max-per-phase must be positive")
    if args.epochs < 0:
        parser.error("--epochs must be non-negative")
    if args.learning_rate < 0.0:
        parser.error("--learning-rate must be non-negative")
    if args.l2 < 0.0:
        parser.error("--l2 must be non-negative")
    if args.sequence_min_ply < 0:
        parser.error("--sequence-min-ply must be non-negative")
    if args.sequence_max_ply is not None and args.sequence_max_ply < args.sequence_min_ply:
        parser.error("--sequence-max-ply must be >= --sequence-min-ply")
    if args.sequence_ply_stride <= 0:
        parser.error("--sequence-ply-stride must be positive")
    if args.sequence_max_positions is not None and args.sequence_max_positions <= 0:
        parser.error("--sequence-max-positions must be positive")
    if args.eval_smoke_max_positions is not None and args.eval_smoke_max_positions <= 0:
        parser.error("--eval-smoke-max-positions must be positive")
    if args.search_smoke_max_positions is not None and args.search_smoke_max_positions <= 0:
        parser.error("--search-smoke-max-positions must be positive")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def run_capture(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def run_or_fail(command: list[str]) -> subprocess.CompletedProcess[str]:
    result = run_capture(command)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        raise RuntimeError(f"command failed: {' '.join(command)}")
    return result


def parse_key_values(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        key, separator, value = line.partition("=")
        if not separator:
            raise RuntimeError(f"summary line is missing '=': {line}")
        if key in values:
            raise RuntimeError(f"duplicate summary key: {key}")
        values[key] = value
    return values


def load_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(data, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return data


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def git_commit() -> str | None:
    result = run_capture(["git", "rev-parse", "HEAD"])
    if result.returncode != 0:
        return None
    return result.stdout.strip() or None


def created_at_utc(args: argparse.Namespace) -> str:
    if args.created_at_utc:
        return args.created_at_utc
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def make_run_id(args: argparse.Namespace, timestamp: str) -> str:
    if args.run_id:
        return args.run_id
    source = str(args.normalized_tsv or args.raw_input)
    digest = hashlib.sha256(f"{args.seed}\t{source}".encode("utf-8")).hexdigest()[:10]
    compact_time = timestamp.replace("-", "").replace(":", "").replace("Z", "")
    return f"egaroucid-local-{compact_time}-{digest}"


def prepare_output_dir(args: argparse.Namespace, run_id: str) -> Path:
    if args.output_dir is not None:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        return args.output_dir
    return Path(tempfile.mkdtemp(prefix=f"vibe-othello-{run_id}-"))


def import_raw(args: argparse.Namespace, output_dir: Path) -> Path:
    normalized = output_dir / "normalized.tsv"
    with normalized.open("w", encoding="utf-8") as output:
        result = subprocess.run(
            [
                sys.executable,
                str(args.importer),
                "--input",
                str(args.raw_input),
                "--manifest",
                str(args.manifest),
            ],
            check=False,
            stdout=output,
            stderr=subprocess.PIPE,
            text=True,
        )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"command failed: {sys.executable} {args.importer}")
    return normalized


def import_sequence(args: argparse.Namespace, output_dir: Path) -> Path:
    normalized = output_dir / "sequence-normalized.tsv"
    report = output_dir / "sequence-import-report.json"
    command = [
        sys.executable,
        str(args.sequence_importer),
        "--input",
        str(args.sequence_input),
        "--manifest",
        str(args.sequence_manifest),
        "--report",
        str(report),
        "--seed",
        str(args.seed),
        "--min-ply",
        str(args.sequence_min_ply),
        "--ply-stride",
        str(args.sequence_ply_stride),
    ]
    if args.sequence_max_ply is not None:
        command.extend(["--max-ply", str(args.sequence_max_ply)])
    if args.sequence_max_positions is not None:
        command.extend(["--max-positions", str(args.sequence_max_positions)])
    command.append("--emit-terminal" if args.sequence_emit_terminal else "--no-emit-terminal")
    with normalized.open("w", encoding="utf-8") as output:
        result = subprocess.run(
            command,
            check=False,
            stdout=output,
            stderr=subprocess.PIPE,
            text=True,
        )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"command failed: {sys.executable} {args.sequence_importer}")
    return normalized


def parse_normalized_row(row: dict[str, str], line_number: int) -> NormalizedRow:
    if set(row) != set(NORMALIZED_HEADER) or None in row:
        raise RuntimeError(f"line {line_number}: unexpected normalized TSV shape")
    fields = [row[field] for field in NORMALIZED_HEADER]
    try:
        label = int(row["label_score_side_to_move"])
        phase = int(row["phase"])
    except ValueError as error:
        raise RuntimeError(f"line {line_number}: numeric field is invalid: {error}") from error
    split = row["split"]
    if split not in SPLITS:
        raise RuntimeError(f"line {line_number}: split must be train, validation, or test")
    if phase < 0 or phase > 12:
        raise RuntimeError(f"line {line_number}: phase must be in [0, 12]")
    return NormalizedRow(
        fields=fields,
        line_text="\t".join(fields),
        record_id=row["record_id"],
        position_id=row["position_id"],
        source_dataset_id=row["source_dataset_id"],
        split=split,
        label=label,
        phase=phase,
    )


def sample_key(seed: int, position_id: str) -> str:
    return hashlib.sha256(f"{seed}\t{position_id}".encode("utf-8")).hexdigest()


def build_position_selections(
    normalized_tsv: Path, max_examples: int | None, max_per_phase: int | None, seed: int
) -> tuple[TopKPositionIds, dict[int, TopKPositionIds], int]:
    global_selection = TopKPositionIds(max_examples)
    phase_selections = {phase: TopKPositionIds(max_per_phase) for phase in range(13)}
    input_rows = 0
    with normalized_tsv.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != NORMALIZED_HEADER:
            raise RuntimeError("unexpected normalized TSV header")
        for line_number, row in enumerate(reader, start=2):
            parsed = parse_normalized_row(row, line_number)
            input_rows += 1
            key = sample_key(seed, parsed.position_id)
            global_selection.consider(parsed.position_id, key)
            phase_selections[parsed.phase].consider(parsed.position_id, key)
    if input_rows == 0:
        raise RuntimeError("normalized TSV has no rows")
    return global_selection, phase_selections, input_rows


def write_sampled_normalized(
    input_path: Path,
    output_path: Path,
    sample_report_path: Path,
    args: argparse.Namespace,
) -> dict[str, Any]:
    global_selection, phase_selections, input_rows = build_position_selections(
        input_path, args.max_examples, args.max_per_phase, args.seed
    )
    summary = SampleSummary(input_rows=input_rows)
    digest = hashlib.sha256()

    with input_path.open(newline="", encoding="utf-8") as input_handle:
        reader = csv.DictReader(input_handle, delimiter="\t")
        with output_path.open("w", newline="", encoding="utf-8") as output_handle:
            writer = csv.writer(output_handle, delimiter="\t", lineterminator="\n")
            writer.writerow(NORMALIZED_HEADER)
            for line_number, row in enumerate(reader, start=2):
                parsed = parse_normalized_row(row, line_number)
                if not global_selection.allows(parsed.position_id):
                    continue
                if not phase_selections[parsed.phase].allows(parsed.position_id):
                    continue

                writer.writerow(parsed.fields)
                digest.update(parsed.line_text.encode("utf-8"))
                digest.update(b"\n")
                summary.sampled_rows += 1
                assert summary.counts_by_split is not None
                assert summary.counts_by_phase is not None
                assert summary.source_dataset_ids is not None
                summary.counts_by_split[parsed.split] = (
                    summary.counts_by_split.get(parsed.split, 0) + 1
                )
                phase_key = str(parsed.phase)
                summary.counts_by_phase[phase_key] = (
                    summary.counts_by_phase.get(phase_key, 0) + 1
                )
                summary.source_dataset_ids.add(parsed.source_dataset_id)
                summary.label_min = (
                    parsed.label
                    if summary.label_min is None
                    else min(summary.label_min, parsed.label)
                )
                summary.label_max = (
                    parsed.label
                    if summary.label_max is None
                    else max(summary.label_max, parsed.label)
                )
                summary.label_sum += parsed.label

    if summary.sampled_rows == 0:
        raise RuntimeError("sampling produced no rows")
    label_mean = summary.label_sum / summary.sampled_rows
    summary.checksum = f"sha256:{digest.hexdigest()}"
    report = {
        "schema_version": 1,
        "input_rows": summary.input_rows,
        "sampled_rows": summary.sampled_rows,
        "source_dataset_ids": sorted(summary.source_dataset_ids or []),
        "counts_by_split": summary.counts_by_split,
        "counts_by_phase": summary.counts_by_phase,
        "label_min": summary.label_min,
        "label_max": summary.label_max,
        "label_mean": float(f"{label_mean:.12g}"),
        "checksum": summary.checksum,
        "sample_policy": {
            "method": "deterministic position_id sha256 top-k",
            "max_examples": args.max_examples,
            "max_per_phase": args.max_per_phase,
            "seed": args.seed,
            "split_policy": args.split_policy,
            "unit": "position_id groups; duplicate labels for a selected position are preserved",
            "memory_policy": "bounded position_id maps with heapq O(log K) candidate replacement",
        },
    }
    sample_report_path.write_text(stable_json(report), encoding="utf-8")
    return report


def count_normalized_rows(path: Path) -> int:
    count = 0
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != NORMALIZED_HEADER:
            raise RuntimeError("unexpected normalized TSV header")
        for _ in reader:
            count += 1
    return count


def write_smoke_positions_tsv(
    input_path: Path,
    output_path: Path,
    max_positions: int | None,
    seed: int,
) -> dict[str, Any]:
    input_rows = count_normalized_rows(input_path)
    if max_positions is None or input_rows <= max_positions:
        return {
            "input_positions": input_rows,
            "used_positions": input_rows,
            "path": input_path,
            "policy": {
                "method": "all sampled positions",
                "max_positions": max_positions,
                "seed": seed,
            },
        }

    selection = TopKPositionIds(max_positions)
    with input_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames != NORMALIZED_HEADER:
            raise RuntimeError("unexpected normalized TSV header")
        for line_number, row in enumerate(reader, start=2):
            parsed = parse_normalized_row(row, line_number)
            selection.consider(parsed.position_id, sample_key(seed, parsed.position_id))

    used_rows = 0
    with input_path.open(newline="", encoding="utf-8") as input_handle:
        reader = csv.DictReader(input_handle, delimiter="\t")
        with output_path.open("w", newline="", encoding="utf-8") as output_handle:
            writer = csv.writer(output_handle, delimiter="\t", lineterminator="\n")
            writer.writerow(NORMALIZED_HEADER)
            for line_number, row in enumerate(reader, start=2):
                parsed = parse_normalized_row(row, line_number)
                if selection.allows(parsed.position_id):
                    writer.writerow(parsed.fields)
                    used_rows += 1

    if used_rows == 0:
        raise RuntimeError("smoke position sampling produced no rows")
    return {
        "input_positions": input_rows,
        "used_positions": used_rows,
        "path": output_path,
        "policy": {
            "method": "deterministic position_id sha256 top-k",
            "max_positions": max_positions,
            "seed": seed,
            "unit": "position rows",
        },
    }


def run_dataset_builder(
    args: argparse.Namespace, normalized_tsv: Path, output_dir: Path
) -> tuple[Path, Path, dict[str, Any]]:
    dataset_tsv = output_dir / "pattern-dataset.tsv"
    dataset_report = output_dir / "dataset-report.json"
    with dataset_tsv.open("w", encoding="utf-8") as output:
        result = subprocess.run(
            [
                str(args.dataset_exe),
                "--normalized-tsv",
                str(normalized_tsv),
                "--report",
                str(dataset_report),
            ],
            check=False,
            stdout=output,
            stderr=subprocess.PIPE,
            text=True,
        )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"command failed: {args.dataset_exe}")
    return dataset_tsv, dataset_report, load_json(dataset_report)


def run_trainer(
    args: argparse.Namespace, dataset_tsv: Path, output_dir: Path
) -> tuple[Path, Path, dict[str, Any]]:
    weights = output_dir / "v0b-weights.json"
    report = output_dir / "v0b-trainer-report.json"
    run_or_fail(
        [
            sys.executable,
            str(args.trainer),
            "--dataset",
            str(dataset_tsv),
            "--mode",
            "pattern-sgd-v0b",
            "--epochs",
            str(args.epochs),
            "--learning-rate",
            str(args.learning_rate),
            "--l2",
            str(args.l2),
            "--seed",
            str(args.seed),
            "--weights-out",
            str(weights),
            "--report-out",
            str(report),
        ]
    )
    return weights, report, load_json(report)


def run_v0a_baseline(
    args: argparse.Namespace, dataset_tsv: Path, output_dir: Path
) -> tuple[Path, Path, Path, Path, dict[str, Any], dict[str, str]] | None:
    if args.skip_v0a_baseline:
        return None
    weights_tsv = output_dir / "v0a-weights.tsv"
    trainer_report = output_dir / "v0a-trainer-report.json"
    run_or_fail(
        [
            sys.executable,
            str(args.trainer),
            "--dataset",
            str(dataset_tsv),
            "--mode",
            "phase-bias-v0a",
            "--weights-out",
            str(weights_tsv),
            "--report-out",
            str(trainer_report),
        ]
    )
    artifact = output_dir / "v0a.weights.bin"
    manifest = output_dir / "v0a.manifest.json"
    export = run_or_fail(
        [
            sys.executable,
            str(args.v0a_exporter),
            "--weights-tsv",
            str(weights_tsv),
            "--weights-out",
            str(artifact),
            "--manifest-out",
            str(manifest),
        ]
    )
    return (
        weights_tsv,
        trainer_report,
        artifact,
        manifest,
        load_json(trainer_report),
        parse_key_values(export.stdout),
    )


def export_v0b(
    args: argparse.Namespace, weights_json: Path, output_dir: Path
) -> tuple[Path, Path, dict[str, str]]:
    artifact = output_dir / "v0b.weights.bin"
    manifest = output_dir / "v0b.manifest.json"
    export = run_or_fail(
        [
            sys.executable,
            str(args.v0b_exporter),
            "--weights-json",
            str(weights_json),
            "--weights-out",
            str(artifact),
            "--manifest-out",
            str(manifest),
        ]
    )
    return artifact, manifest, parse_key_values(export.stdout)


def run_smoke(
    exe: Path,
    normalized_tsv: Path,
    v0a_artifact: Path,
    v0b_artifact: Path,
    v0a_checksum: str,
    v0b_checksum: str,
    report_path: Path,
) -> dict[str, Any]:
    result = run_or_fail(
        [
            str(exe),
            "--positions-tsv",
            str(normalized_tsv),
            "--v0a-weights",
            str(v0a_artifact),
            "--v0b-weights",
            str(v0b_artifact),
            "--v0a-artifact-checksum",
            v0a_checksum,
            "--v0b-artifact-checksum",
            v0b_checksum,
            "--report-out",
            str(report_path),
        ]
    )
    return {
        "summary": parse_key_values(result.stdout),
        "report_checksum": load_json(report_path).get("checksum"),
    }


def rel(path: Path, base: Path) -> str:
    return str(path.relative_to(base))


def write_run_report(
    args: argparse.Namespace,
    output_dir: Path,
    run_id: str,
    timestamp: str,
    input_mode: str,
    sample_report: dict[str, Any],
    dataset_report: dict[str, Any],
    trainer_report: dict[str, Any],
    v0b_export_summary: dict[str, str],
    paths: dict[str, Path],
    v0a_data: tuple[Path, Path, Path, Path, dict[str, Any], dict[str, str]] | None,
    eval_summary: dict[str, Any] | None,
    search_summary: dict[str, Any] | None,
    source_kind: str,
    smoke_positions: dict[str, dict[str, Any]],
) -> Path:
    source_ids = sample_report.get("source_dataset_ids")
    source_dataset_id = (
        source_ids[0] if isinstance(source_ids, list) and len(source_ids) == 1 else source_ids
    )
    output_files: dict[str, str] = {
        name: rel(path, output_dir) for name, path in sorted(paths.items())
    }
    report = {
        "schema_version": 1,
        "run_id": run_id,
        "created_at_utc": timestamp,
        "git_commit": git_commit(),
        "source_dataset_id": source_dataset_id,
        "source_kind": source_kind,
        "input_mode": input_mode,
        "sample_policy": sample_report.get("sample_policy"),
        "sample_counts_by_split": sample_report.get("counts_by_split"),
        "sample_counts_by_phase": sample_report.get("counts_by_phase"),
        "sample_report_checksum": sample_report.get("checksum"),
        "dataset_report_checksum": dataset_report.get("checksum"),
        "trainer_version": trainer_report.get("trainer_version"),
        "trainer_args": {
            "epochs": args.epochs,
            "learning_rate": args.learning_rate,
            "l2": args.l2,
            "seed": args.seed,
        },
        "trainer_report_checksum": trainer_report.get("checksum"),
        "weights_checksum": trainer_report.get("weights_checksum")
        or sha256_file(paths["v0b_weights_json"]),
        "artifact_checksum": v0b_export_summary.get("weights_checksum"),
        "evaluation_smoke_summary": eval_summary,
        "search_smoke_summary": search_summary,
        "eval_smoke_input_positions": smoke_positions.get("evaluation", {}).get(
            "input_positions"
        ),
        "eval_smoke_used_positions": smoke_positions.get("evaluation", {}).get("used_positions"),
        "search_smoke_input_positions": smoke_positions.get("search", {}).get("input_positions"),
        "search_smoke_used_positions": smoke_positions.get("search", {}).get("used_positions"),
        "smoke_position_sample_policy": {
            "evaluation": smoke_positions.get("evaluation", {}).get("policy"),
            "search": smoke_positions.get("search", {}).get("policy"),
        },
        "v0a_baseline": None
        if v0a_data is None
        else {
            "trainer_report_checksum": v0a_data[4].get("checksum"),
            "artifact_checksum": v0a_data[5].get("weights_checksum"),
        },
        "output_files": output_files,
        "notes": LOCAL_NOTES,
    }
    report_path = output_dir / "local-training-run-report.json"
    report_path.write_text(stable_json(report), encoding="utf-8")
    return report_path


def main() -> int:
    args = parse_args()
    try:
        timestamp = created_at_utc(args)
        run_id = make_run_id(args, timestamp)
        output_dir = prepare_output_dir(args, run_id)
        if args.normalized_tsv is not None:
            input_mode = "normalized-tsv"
            source_kind = "egaroucid-local"
        elif args.raw_input is not None:
            input_mode = "raw-input"
            source_kind = "egaroucid-local"
        else:
            input_mode = "sequence-input"
            source_kind = "egaroucid-sequence-local"

        source_normalized = (
            args.normalized_tsv
            if args.normalized_tsv is not None
            else import_raw(args, output_dir)
            if args.raw_input is not None
            else import_sequence(args, output_dir)
        )
        sampled_normalized = output_dir / "sampled-normalized.tsv"
        sample_report_path = output_dir / "sample-report.json"
        sample_report = write_sampled_normalized(
            source_normalized, sampled_normalized, sample_report_path, args
        )

        dataset_tsv, dataset_report_path, dataset_report = run_dataset_builder(
            args, sampled_normalized, output_dir
        )
        v0b_weights_json, v0b_trainer_report_path, trainer_report = run_trainer(
            args, dataset_tsv, output_dir
        )
        v0b_artifact, v0b_manifest, v0b_export_summary = export_v0b(
            args, v0b_weights_json, output_dir
        )
        v0a_data = run_v0a_baseline(args, dataset_tsv, output_dir)

        eval_summary = None
        search_summary = None
        if v0a_data is not None:
            v0a_artifact = v0a_data[2]
            v0a_checksum = v0a_data[5]["weights_checksum"]
            v0b_checksum = v0b_export_summary["weights_checksum"]
            if not args.skip_eval_smoke:
                eval_positions = write_smoke_positions_tsv(
                    sampled_normalized,
                    output_dir / "evaluation-smoke-positions.tsv",
                    args.eval_smoke_max_positions,
                    args.seed,
                )
                eval_summary = run_smoke(
                    args.eval_smoke_exe,
                    eval_positions["path"],
                    v0a_artifact,
                    v0b_artifact,
                    v0a_checksum,
                    v0b_checksum,
                    output_dir / "evaluation-smoke-report.json",
                )
            else:
                eval_positions = {}
            if not args.skip_search_smoke:
                search_positions = write_smoke_positions_tsv(
                    sampled_normalized,
                    output_dir / "search-smoke-positions.tsv",
                    args.search_smoke_max_positions,
                    args.seed,
                )
                search_summary = run_smoke(
                    args.search_smoke_exe,
                    search_positions["path"],
                    v0a_artifact,
                    v0b_artifact,
                    v0a_checksum,
                    v0b_checksum,
                    output_dir / "search-smoke-report.json",
                )
            else:
                search_positions = {}
        else:
            eval_positions = {}
            search_positions = {}

        paths = {
            "sampled_normalized_tsv": sampled_normalized,
            "sample_report_json": sample_report_path,
            "pattern_dataset_tsv": dataset_tsv,
            "dataset_report_json": dataset_report_path,
            "v0b_weights_json": v0b_weights_json,
            "v0b_trainer_report_json": v0b_trainer_report_path,
            "v0b_artifact_weights": v0b_artifact,
            "v0b_artifact_manifest": v0b_manifest,
        }
        if args.raw_input is not None or args.sequence_input is not None:
            paths["normalized_tsv"] = source_normalized
        if args.sequence_input is not None:
            paths["sequence_import_report_json"] = output_dir / "sequence-import-report.json"
        if v0a_data is not None:
            paths.update(
                {
                    "v0a_weights_tsv": v0a_data[0],
                    "v0a_trainer_report_json": v0a_data[1],
                    "v0a_artifact_weights": v0a_data[2],
                    "v0a_artifact_manifest": v0a_data[3],
                }
            )
        if eval_summary is not None:
            paths["evaluation_smoke_report_json"] = output_dir / "evaluation-smoke-report.json"
            if eval_positions.get("path") != sampled_normalized:
                paths["evaluation_smoke_positions_tsv"] = eval_positions["path"]
        if search_summary is not None:
            paths["search_smoke_report_json"] = output_dir / "search-smoke-report.json"
            if search_positions.get("path") != sampled_normalized:
                paths["search_smoke_positions_tsv"] = search_positions["path"]

        report_path = write_run_report(
            args,
            output_dir,
            run_id,
            timestamp,
            input_mode,
            sample_report,
            dataset_report,
            trainer_report,
            v0b_export_summary,
            paths,
            v0a_data,
            eval_summary,
            search_summary,
            source_kind,
            {"evaluation": eval_positions, "search": search_positions},
        )
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    print(f"run_id={run_id}")
    print(f"output_dir={output_dir}")
    print(f"report={report_path}")
    print(f"sampled_rows={sample_report['sampled_rows']}")
    print(f"trainer_report_checksum={trainer_report.get('checksum')}")
    print(f"artifact_checksum={v0b_export_summary.get('weights_checksum')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
