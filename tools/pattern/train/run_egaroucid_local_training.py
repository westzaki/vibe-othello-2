#!/usr/bin/env python3
"""Run local-only Egaroucid subset training through export and smoke checks."""

from __future__ import annotations

import argparse
import csv
import hashlib
import heapq
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


NORMALIZED_HEADER_V1 = [
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
NORMALIZED_HEADER_V2 = [
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
]
SUPPORTED_NORMALIZED_HEADERS = (NORMALIZED_HEADER_V1, NORMALIZED_HEADER_V2)
SPLITS = ("train", "validation", "test")
LOCAL_NOTES = [
    "local run only",
    "not production benchmark",
    "not strength claim",
    "Egaroucid-derived artifacts are not committed",
    "publication remains gated / unknown",
]
SEQUENCE_CACHE_SCHEMA_VERSION = 1
SEQUENCE_NORMALIZED_SCHEMA_VERSION = 2
LOCAL_ONLY_CACHE_WARNING = "sequence replay cache is local-only and must not be committed"

try:
    import resource
except ImportError:  # pragma: no cover - Windows fallback for local-only tooling.
    resource = None  # type: ignore[assignment]


@dataclass(frozen=True)
class NormalizedRow:
    fields: list[str]
    line_text: str
    schema_version: int
    record_id: str
    position_id: str
    game_group_id: str | None
    board_id: str | None
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
    board_splits: dict[str, set[str]] | None = None

    def __post_init__(self) -> None:
        if self.counts_by_split is None:
            self.counts_by_split = {split: 0 for split in SPLITS}
        if self.counts_by_phase is None:
            self.counts_by_phase = {}
        if self.source_dataset_ids is None:
            self.source_dataset_ids = set()
        if self.board_splits is None:
            self.board_splits = {}


@dataclass
class StageRecorder:
    stages: dict[str, dict[str, Any]] = field(default_factory=dict)

    def begin(self, name: str, **fields: Any) -> "StageTimer":
        stage = self.stages.setdefault(name, {})
        stage.update(fields)
        return StageTimer(self, name)

    def update(self, name: str, **fields: Any) -> None:
        stage = self.stages.setdefault(name, {})
        stage.update(fields)


class StageTimer:
    def __init__(self, recorder: StageRecorder, name: str) -> None:
        self.recorder = recorder
        self.name = name
        self.start_wall = 0.0
        self.start_cpu = 0.0

    def __enter__(self) -> "StageTimer":
        self.start_wall = time.perf_counter()
        self.start_cpu = time.process_time()
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.recorder.update(
            self.name,
            wall_time_sec=round(time.perf_counter() - self.start_wall, 6),
            process_cpu_time_sec=round(time.process_time() - self.start_cpu, 6),
            peak_rss_bytes=peak_rss_bytes(),
            peak_rss_note=peak_rss_note(),
            status="failed" if exc_type is not None else self.recorder.stages[self.name].get("status", "ok"),
        )


@dataclass(frozen=True)
class SequenceCacheInfo:
    enabled: bool
    cache_dir: str | None
    cache_key: str | None
    status: str
    metadata_path: str | None = None
    normalized_tsv_sha256: str | None = None
    import_report_sha256: str | None = None
    notes: list[str] = field(default_factory=list)


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
    parser.add_argument(
        "--strict-board-disjoint-splits",
        action="store_true",
        help="Fail local measurement when a v2 board_id appears across train/validation/test.",
    )
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.1)
    parser.add_argument("--l2", type=float, default=0.0)
    parser.add_argument(
        "--trainer-mode",
        choices=("pattern-sgd-v0b", "pattern-sgd-v0c"),
        default="pattern-sgd-v0b",
    )
    parser.add_argument("--weight-decay", type=float)
    parser.add_argument(
        "--lr-schedule",
        choices=("constant", "inverse-sqrt"),
        default="constant",
    )
    parser.add_argument("--gradient-clip", type=float)
    parser.add_argument("--early-stop-patience", type=int)
    parser.add_argument("--pattern-set", default="fixed-pattern-fixture-v1")
    parser.add_argument(
        "--dataset-output-format",
        choices=("expanded-tsv", "compact-tsv"),
        default="expanded-tsv",
    )
    parser.add_argument("--skip-eval-smoke", action="store_true")
    parser.add_argument("--skip-search-smoke", action="store_true")
    parser.add_argument("--skip-v0a-baseline", action="store_true")
    parser.add_argument("--sequence-min-ply", type=int, default=8)
    parser.add_argument("--sequence-max-ply", type=int)
    parser.add_argument("--sequence-ply-stride", type=int, default=1)
    parser.add_argument("--sequence-max-positions", type=int)
    parser.add_argument(
        "--sequence-sampling-mode",
        choices=("full-scan-topk", "bounded-dev"),
        default="full-scan-topk",
    )
    parser.add_argument("--sequence-max-files", type=int)
    parser.add_argument("--sequence-max-games", type=int)
    parser.add_argument("--sequence-file-sample-rate", type=float)
    parser.add_argument("--sequence-game-sample-rate", type=float)
    parser.add_argument("--sequence-file-order", choices=("path", "hash"), default="path")
    parser.add_argument("--sequence-progress-every-games", type=int)
    parser.add_argument("--sequence-progress-every-files", type=int)
    parser.add_argument(
        "--sequence-cache-dir",
        type=Path,
        help="Local-only content-addressed cache for sequence normalized TSV imports.",
    )
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
    if args.sequence_cache_dir is not None and args.sequence_input is None:
        parser.error("--sequence-cache-dir requires --sequence-input")
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
    if args.weight_decay is not None and args.weight_decay < 0.0:
        parser.error("--weight-decay must be non-negative")
    if args.gradient_clip is not None and args.gradient_clip <= 0.0:
        parser.error("--gradient-clip must be positive")
    if args.early_stop_patience is not None and args.early_stop_patience < 0:
        parser.error("--early-stop-patience must be non-negative")
    if args.sequence_min_ply < 0:
        parser.error("--sequence-min-ply must be non-negative")
    if args.sequence_max_ply is not None and args.sequence_max_ply < args.sequence_min_ply:
        parser.error("--sequence-max-ply must be >= --sequence-min-ply")
    if args.sequence_ply_stride <= 0:
        parser.error("--sequence-ply-stride must be positive")
    if args.sequence_max_positions is not None and args.sequence_max_positions <= 0:
        parser.error("--sequence-max-positions must be positive")
    if args.sequence_max_files is not None and args.sequence_max_files <= 0:
        parser.error("--sequence-max-files must be positive")
    if args.sequence_max_games is not None and args.sequence_max_games <= 0:
        parser.error("--sequence-max-games must be positive")
    for name in ("sequence_file_sample_rate", "sequence_game_sample_rate"):
        value = getattr(args, name)
        if value is not None and not (0.0 < value <= 1.0):
            parser.error(f"--{name.replace('_', '-')} must be > 0.0 and <= 1.0")
    bounded_sequence_controls = (
        args.sequence_max_files is not None
        or args.sequence_max_games is not None
        or args.sequence_file_sample_rate is not None
        or args.sequence_game_sample_rate is not None
    )
    if bounded_sequence_controls and args.sequence_sampling_mode != "bounded-dev":
        parser.error(
            "--sequence-max-files, --sequence-max-games, --sequence-file-sample-rate, "
            "and --sequence-game-sample-rate require --sequence-sampling-mode bounded-dev"
        )
    if args.sequence_progress_every_games is not None and args.sequence_progress_every_games <= 0:
        parser.error("--sequence-progress-every-games must be positive")
    if args.sequence_progress_every_files is not None and args.sequence_progress_every_files <= 0:
        parser.error("--sequence-progress-every-files must be positive")
    if args.eval_smoke_max_positions is not None and args.eval_smoke_max_positions <= 0:
        parser.error("--eval-smoke-max-positions must be positive")
    if args.search_smoke_max_positions is not None and args.search_smoke_max_positions <= 0:
        parser.error("--search-smoke-max-positions must be positive")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def peak_rss_bytes() -> int | None:
    if resource is None:
        return None
    usage = resource.getrusage(resource.RUSAGE_SELF)
    child_usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    scale = 1024 if sys.platform != "darwin" else 1
    return max(usage.ru_maxrss, child_usage.ru_maxrss) * scale


def peak_rss_note() -> str | None:
    if resource is None:
        return "resource module unavailable"
    return "process maximum resident set size observed by resource.getrusage"


def file_size(path: Path | None) -> int | None:
    if path is None or not path.exists():
        return None
    return path.stat().st_size


def sum_file_sizes(paths: list[Path] | tuple[Path, ...]) -> int:
    return sum(file_size(path) or 0 for path in paths)


def throughput(count: Any, wall_time_sec: Any) -> float | None:
    if isinstance(count, bool) or not isinstance(count, (int, float)):
        return None
    if isinstance(wall_time_sec, bool) or not isinstance(wall_time_sec, (int, float)):
        return None
    if wall_time_sec <= 0:
        return None
    return round(count / wall_time_sec, 6)


def count_lines_after_header(path: Path | None) -> int | None:
    if path is None or not path.exists():
        return None
    with path.open("r", encoding="utf-8", newline="") as handle:
        count = sum(1 for _ in handle)
    return max(0, count - 1)


def repo_relative_or_name(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(repo_root()))
    except ValueError:
        return path.name


def sha256_file_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_text_hex(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


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


def sequence_importer_constants(importer: Path) -> dict[str, str]:
    text = importer.read_text(encoding="utf-8")
    values: dict[str, str] = {}
    for name in ("IMPORTER_VERSION", "IDENTITY_POLICY_VERSION", "SOURCE_KIND"):
        match = re.search(rf"^{name}\s*=\s*['\"]([^'\"]+)['\"]", text, flags=re.MULTILINE)
        if match is not None:
            values[name.lower()] = match.group(1)
    return values


def load_manifest_dataset_id(path: Path) -> str:
    data = load_json(path)
    dataset_id = data.get("dataset_id")
    if not isinstance(dataset_id, str) or not dataset_id:
        raise RuntimeError(f"manifest dataset_id must be a non-empty string: {path}")
    return dataset_id


def input_fingerprints(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        raise RuntimeError(f"sequence input does not exist: {path}")
    if path.is_dir():
        paths = sorted(child for child in path.rglob("*.txt") if child.is_file())
        if not paths:
            raise RuntimeError(f"sequence input directory contains no .txt files: {path}")
        fingerprints = []
        for child in paths:
            fingerprints.append(
                {
                    "path_display": str(child.relative_to(path)),
                    "size_bytes": child.stat().st_size,
                    "sha256": sha256_file_hex(child),
                }
            )
        return fingerprints
    return [
        {
            "path_display": path.name,
            "size_bytes": path.stat().st_size,
            "sha256": sha256_file_hex(path),
        }
    ]


def aggregate_input_sha256(fingerprints: list[dict[str, Any]]) -> str:
    canonical = [
        {
            "sha256": item["sha256"],
            "size_bytes": item["size_bytes"],
        }
        for item in fingerprints
    ]
    return sha256_text_hex(stable_json(sorted(canonical, key=lambda item: (item["sha256"], item["size_bytes"]))))


def sequence_importer_options(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "dataset_id": load_manifest_dataset_id(args.sequence_manifest),
        "emit_terminal": args.sequence_emit_terminal,
        "file_order": args.sequence_file_order,
        "file_sample_rate": args.sequence_file_sample_rate,
        "game_sample_rate": args.sequence_game_sample_rate,
        "max_files": args.sequence_max_files,
        "max_games": args.sequence_max_games,
        "max_ply": args.sequence_max_ply,
        "max_positions": args.sequence_max_positions,
        "min_ply": args.sequence_min_ply,
        "ply_stride": args.sequence_ply_stride,
        "sampling_mode": args.sequence_sampling_mode,
        "seed": args.seed,
        "strict_board_disjoint_splits": args.strict_board_disjoint_splits,
    }


def sequence_cache_plan(args: argparse.Namespace) -> tuple[str, dict[str, Any], dict[str, Any]]:
    assert args.sequence_manifest is not None
    assert args.sequence_input is not None
    constants = sequence_importer_constants(args.sequence_importer)
    input_files = input_fingerprints(args.sequence_input)
    manifest_sha = sha256_file_hex(args.sequence_manifest)
    importer_sha = sha256_file_hex(args.sequence_importer)
    options = sequence_importer_options(args)
    source_fingerprints = {
        "manifest_sha256": manifest_sha,
        "input_files": input_files,
        "aggregate_input_sha256": aggregate_input_sha256(input_files),
    }
    key_payload = {
        "cache_schema_version": SEQUENCE_CACHE_SCHEMA_VERSION,
        "importer_script_sha256": importer_sha,
        "importer_version": constants.get("importer_version"),
        "identity_policy_version": constants.get("identity_policy_version"),
        "manifest_sha256": manifest_sha,
        "normalized_schema_version": SEQUENCE_NORMALIZED_SCHEMA_VERSION,
        "input_file_sha256": sorted(
            (
                {
                    "sha256": item["sha256"],
                    "size_bytes": item["size_bytes"],
                }
                for item in input_files
            ),
            key=lambda item: (item["sha256"], item["size_bytes"]),
        ),
        "aggregate_input_sha256": source_fingerprints["aggregate_input_sha256"],
        "importer_options": options,
    }
    key_payload_digest = sha256_text_hex(stable_json(key_payload))
    cache_key = key_payload_digest
    return cache_key, source_fingerprints, {
        "constants": constants,
        "importer_options": options,
        "key_payload": key_payload,
        "key_payload_sha256": key_payload_digest,
        "manifest_sha256": manifest_sha,
        "importer_sha256": importer_sha,
        "input_files": input_files,
        "aggregate_input_sha256": source_fingerprints["aggregate_input_sha256"],
    }


def cache_entry_paths(cache_dir: Path, cache_key: str) -> dict[str, Path]:
    entry_dir = cache_dir / cache_key
    return {
        "dir": entry_dir,
        "normalized": entry_dir / "sequence-normalized.tsv",
        "report": entry_dir / "sequence-import-report.json",
        "metadata": entry_dir / "metadata.json",
    }


def cache_metadata_valid(
    paths: dict[str, Path],
    expected_cache_key: str,
    plan: dict[str, Any],
) -> tuple[bool, dict[str, Any] | None]:
    try:
        metadata = load_json(paths["metadata"])
    except RuntimeError:
        return False, None
    expected_fields = {
        "cache_key": expected_cache_key,
        "cache_schema_version": SEQUENCE_CACHE_SCHEMA_VERSION,
        "dataset_id": plan["importer_options"].get("dataset_id"),
        "identity_policy_version": plan["constants"].get("identity_policy_version"),
        "importer_version": plan["constants"].get("importer_version"),
        "key_payload_sha256": plan["key_payload_sha256"],
        "manifest_sha256": plan["manifest_sha256"],
        "normalized_schema_version": SEQUENCE_NORMALIZED_SCHEMA_VERSION,
    }
    for key, expected in expected_fields.items():
        if metadata.get(key) != expected:
            return False, metadata
    if metadata.get("importer_options") != plan["importer_options"]:
        return False, metadata
    normalized = paths["normalized"]
    report = paths["report"]
    if not normalized.exists() or not report.exists():
        return False, metadata
    if metadata.get("normalized_tsv_sha256") != sha256_file(normalized):
        return False, metadata
    if metadata.get("import_report_sha256") != sha256_file(report):
        return False, metadata
    if metadata.get("normalized_tsv_size_bytes") != normalized.stat().st_size:
        return False, metadata
    if metadata.get("import_report_size_bytes") != report.stat().st_size:
        return False, metadata
    return True, metadata


def install_sequence_cache_entry(temp_entry: dict[str, Path], entry: dict[str, Path]) -> None:
    entry["dir"].mkdir(parents=True, exist_ok=True)
    for name in ("normalized", "report"):
        temp_target = entry["dir"] / f".{entry[name].name}.tmp-{os.getpid()}"
        shutil.copyfile(temp_entry[name], temp_target)
        os.replace(temp_target, entry[name])
    temp_metadata = entry["dir"] / f".{entry['metadata'].name}.tmp-{os.getpid()}"
    shutil.copyfile(temp_entry["metadata"], temp_metadata)
    os.replace(temp_metadata, entry["metadata"])


def restore_sequence_cache_entry(paths: dict[str, Path], output_dir: Path) -> tuple[Path, Path]:
    normalized = output_dir / "sequence-normalized.tsv"
    report = output_dir / "sequence-import-report.json"
    shutil.copyfile(paths["normalized"], normalized)
    shutil.copyfile(paths["report"], report)
    return normalized, report


def write_sequence_cache_metadata(
    metadata_path: Path,
    cache_key: str,
    args: argparse.Namespace,
    plan: dict[str, Any],
    normalized: Path,
    report: Path,
) -> dict[str, Any]:
    metadata = {
        "cache_schema_version": SEQUENCE_CACHE_SCHEMA_VERSION,
        "cache_key": cache_key,
        "created_at_utc": datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "repo_git_commit": git_commit(),
        "importer_path": repo_relative_or_name(args.sequence_importer),
        "importer_version": plan["constants"].get("importer_version"),
        "identity_policy_version": plan["constants"].get("identity_policy_version"),
        "normalized_schema_version": SEQUENCE_NORMALIZED_SCHEMA_VERSION,
        "dataset_id": plan["importer_options"].get("dataset_id"),
        "manifest_sha256": plan["manifest_sha256"],
        "input_file_sha256": plan["input_files"],
        "aggregate_input_sha256": plan["aggregate_input_sha256"],
        "importer_options": plan["importer_options"],
        "key_payload_sha256": plan["key_payload_sha256"],
        "normalized_tsv_sha256": sha256_file(normalized),
        "import_report_sha256": sha256_file(report),
        "normalized_tsv_size_bytes": normalized.stat().st_size,
        "import_report_size_bytes": report.stat().st_size,
        "source_kind": plan["constants"].get("source_kind", "egaroucid-sequence-local"),
        "sampling_mode": plan["importer_options"].get("sampling_mode"),
        "sampling_notes": "full-scan-topk is a full replay path; bounded-dev is for local iteration only",
        "strict_board_disjoint_splits": plan["importer_options"].get("strict_board_disjoint_splits"),
        "warning": LOCAL_ONLY_CACHE_WARNING,
    }
    metadata_path.write_text(stable_json(metadata), encoding="utf-8")
    return metadata


def created_at_utc(args: argparse.Namespace) -> str:
    if args.created_at_utc:
        return args.created_at_utc
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def make_run_id(args: argparse.Namespace, timestamp: str) -> str:
    if args.run_id:
        return args.run_id
    source = str(args.normalized_tsv or args.raw_input or args.sequence_input)
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


def sequence_import_command(
    args: argparse.Namespace,
    normalized_report: Path,
) -> list[str]:
    command = [
        sys.executable,
        str(args.sequence_importer),
        "--input",
        str(args.sequence_input),
        "--manifest",
        str(args.sequence_manifest),
        "--report",
        str(normalized_report),
        "--seed",
        str(args.seed),
        "--min-ply",
        str(args.sequence_min_ply),
        "--ply-stride",
        str(args.sequence_ply_stride),
        "--sampling-mode",
        args.sequence_sampling_mode,
        "--file-order",
        args.sequence_file_order,
    ]
    if args.sequence_max_ply is not None:
        command.extend(["--max-ply", str(args.sequence_max_ply)])
    if args.sequence_max_positions is not None:
        command.extend(["--max-positions", str(args.sequence_max_positions)])
    if args.sequence_max_files is not None:
        command.extend(["--max-files", str(args.sequence_max_files)])
    if args.sequence_max_games is not None:
        command.extend(["--max-games", str(args.sequence_max_games)])
    if args.sequence_file_sample_rate is not None:
        command.extend(["--file-sample-rate", str(args.sequence_file_sample_rate)])
    if args.sequence_game_sample_rate is not None:
        command.extend(["--game-sample-rate", str(args.sequence_game_sample_rate)])
    if args.sequence_progress_every_games is not None:
        command.extend(["--progress-every-games", str(args.sequence_progress_every_games)])
    if args.sequence_progress_every_files is not None:
        command.extend(["--progress-every-files", str(args.sequence_progress_every_files)])
    if args.strict_board_disjoint_splits:
        command.append("--strict-board-disjoint-splits")
    command.append("--emit-terminal" if args.sequence_emit_terminal else "--no-emit-terminal")
    return command


def run_sequence_import_command(
    command: list[str],
    temp_normalized: Path,
    stderr_log: Path,
    stage: StageRecorder | None,
    stage_name: str,
) -> None:
    with temp_normalized.open("w", encoding="utf-8") as output:
        with stderr_log.open("w", encoding="utf-8") as log:
            with stage.begin(stage_name) if stage is not None else null_stage():
                process = subprocess.Popen(
                    command,
                    stdout=output,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                assert process.stderr is not None
                for line in process.stderr:
                    sys.stderr.write(line)
                    log.write(line)
                returncode = process.wait()
            if stage is not None:
                stage.update(stage_name, return_code=returncode)
    if returncode != 0:
        temp_normalized.unlink(missing_ok=True)
        if stage is not None:
            stage.update(stage_name, status="failed")
        raise RuntimeError(f"command failed: {command[0]} {command[1]}")


class null_stage:
    def __enter__(self) -> None:
        return None

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        return None


def import_sequence(
    args: argparse.Namespace,
    output_dir: Path,
    stage: StageRecorder | None = None,
) -> tuple[Path, SequenceCacheInfo, dict[str, Any] | None]:
    normalized = output_dir / "sequence-normalized.tsv"
    temp_normalized = output_dir / "sequence-normalized.tsv.tmp"
    report = output_dir / "sequence-import-report.json"
    temp_report = output_dir / "sequence-import-report.json.tmp"
    stderr_log = output_dir / "sequence-import-stderr.log"
    cache_info = SequenceCacheInfo(
        enabled=args.sequence_cache_dir is not None,
        cache_dir=str(args.sequence_cache_dir) if args.sequence_cache_dir is not None else None,
        cache_key=None,
        status="bypassed",
        notes=[LOCAL_ONLY_CACHE_WARNING] if args.sequence_cache_dir is not None else [],
    )
    source_fingerprints = None
    cache_key = None
    plan = None

    if stage is not None:
        with stage.begin("source_hashing"):
            cache_key, source_fingerprints, plan = sequence_cache_plan(args)
    else:
        cache_key, source_fingerprints, plan = sequence_cache_plan(args)

    if args.sequence_cache_dir is not None:
        args.sequence_cache_dir.mkdir(parents=True, exist_ok=True)
        entry = cache_entry_paths(args.sequence_cache_dir, cache_key)
        metadata = None
        status = "miss"
        if stage is not None:
            with stage.begin("sequence_cache_lookup"):
                valid, metadata = cache_metadata_valid(entry, cache_key, plan)
        else:
            valid, metadata = cache_metadata_valid(entry, cache_key, plan)
        if valid:
            if stage is not None:
                with stage.begin("sequence_import_or_cache_restore", status="cache-hit"):
                    restore_sequence_cache_entry(entry, output_dir)
                    stderr_log.write_text("sequence cache hit; importer was not run\n", encoding="utf-8")
            else:
                restore_sequence_cache_entry(entry, output_dir)
                stderr_log.write_text("sequence cache hit; importer was not run\n", encoding="utf-8")
            cache_info = SequenceCacheInfo(
                enabled=True,
                cache_dir=str(args.sequence_cache_dir),
                cache_key=cache_key,
                status="hit",
                metadata_path=str(entry["metadata"]),
                normalized_tsv_sha256=metadata.get("normalized_tsv_sha256") if metadata else None,
                import_report_sha256=metadata.get("import_report_sha256") if metadata else None,
                notes=[LOCAL_ONLY_CACHE_WARNING],
            )
            if stage is not None:
                stage.update(
                    "sequence_import_or_cache_restore",
                    status="cache-hit",
                    input_bytes=entry["normalized"].stat().st_size + entry["report"].stat().st_size,
                    output_bytes=normalized.stat().st_size + report.stat().st_size,
                    output_rows=count_lines_after_header(normalized),
                )
            return normalized, cache_info, source_fingerprints
        if metadata is not None:
            status = "invalidated"

        temp_entry_dir = Path(
            tempfile.mkdtemp(prefix=f".{cache_key}.tmp-", dir=str(args.sequence_cache_dir))
        )
        temp_entry = {
            "dir": temp_entry_dir,
            "normalized": temp_entry_dir / "sequence-normalized.tsv",
            "report": temp_entry_dir / "sequence-import-report.json",
            "metadata": temp_entry_dir / "metadata.json",
        }
        try:
            command = sequence_import_command(args, temp_entry["report"])
            run_sequence_import_command(
                command,
                temp_entry["normalized"],
                stderr_log,
                stage,
                "sequence_import_or_cache_restore",
            )
            write_sequence_cache_metadata(
                temp_entry["metadata"],
                cache_key,
                args,
                plan,
                temp_entry["normalized"],
                temp_entry["report"],
            )
            valid, metadata = cache_metadata_valid(temp_entry, cache_key, plan)
            if not valid or metadata is None:
                raise RuntimeError("sequence cache metadata validation failed after import")
            install_sequence_cache_entry(temp_entry, entry)
            restore_sequence_cache_entry(entry, output_dir)
            cache_info = SequenceCacheInfo(
                enabled=True,
                cache_dir=str(args.sequence_cache_dir),
                cache_key=cache_key,
                status=status,
                metadata_path=str(entry["metadata"]),
                normalized_tsv_sha256=metadata.get("normalized_tsv_sha256"),
                import_report_sha256=metadata.get("import_report_sha256"),
                notes=[LOCAL_ONLY_CACHE_WARNING],
            )
            if stage is not None:
                stage.update(
                    "sequence_import_or_cache_restore",
                    status=status,
                    output_bytes=normalized.stat().st_size + report.stat().st_size,
                    output_rows=count_lines_after_header(normalized),
                )
            return normalized, cache_info, source_fingerprints
        except Exception:
            shutil.rmtree(temp_entry_dir, ignore_errors=True)
            temp_normalized.unlink(missing_ok=True)
            temp_report.unlink(missing_ok=True)
            raise

    command = sequence_import_command(args, report)
    if stage is not None:
        stage.update("sequence_cache_lookup", status="bypassed")
    run_sequence_import_command(command, temp_normalized, stderr_log, stage, "sequence_import_or_cache_restore")
    temp_normalized.replace(normalized)
    cache_info = SequenceCacheInfo(enabled=False, cache_dir=None, cache_key=None, status="bypassed")
    if stage is not None:
        stage.update(
            "sequence_import_or_cache_restore",
            status="bypassed",
            output_bytes=normalized.stat().st_size + report.stat().st_size,
            output_rows=count_lines_after_header(normalized),
        )
    return normalized, cache_info, source_fingerprints


def normalized_header_version(fieldnames: list[str] | None) -> int:
    if fieldnames == NORMALIZED_HEADER_V1:
        return 1
    if fieldnames == NORMALIZED_HEADER_V2:
        return 2
    return 0


def parse_normalized_row(row: dict[str, str], line_number: int, schema_version: int) -> NormalizedRow:
    header = NORMALIZED_HEADER_V2 if schema_version == 2 else NORMALIZED_HEADER_V1
    if set(row) != set(header) or None in row:
        raise RuntimeError(f"line {line_number}: unexpected normalized TSV shape")
    fields = [row[field] for field in header]
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
        schema_version=schema_version,
        record_id=row["record_id"],
        position_id=row["position_id"],
        game_group_id=row.get("game_group_id"),
        board_id=row.get("board_id"),
        source_dataset_id=row["source_dataset_id"],
        split=split,
        label=label,
        phase=phase,
    )


def sample_key(seed: int, position_id: str) -> str:
    return hashlib.sha256(f"{seed}\t{position_id}".encode("utf-8")).hexdigest()


def split_pair(left: str, right: str) -> str:
    return "__".join(sorted((left, right)))


def board_leakage_audit(board_splits: dict[str, set[str]]) -> dict[str, Any]:
    pair_counts: dict[str, int] = {}
    collision_count = 0
    for splits in board_splits.values():
        if len(splits) < 2:
            continue
        collision_count += 1
        ordered = sorted(splits)
        for left_index, left in enumerate(ordered):
            for right in ordered[left_index + 1 :]:
                pair = split_pair(left, right)
                pair_counts[pair] = pair_counts.get(pair, 0) + 1
    return {
        "unique_board_count": len(board_splits),
        "cross_split_board_collision_count": collision_count,
        "cross_split_board_collision_counts_by_pair": dict(sorted(pair_counts.items())),
    }


def build_position_selections(
    normalized_tsv: Path, max_examples: int | None, max_per_phase: int | None, seed: int
) -> tuple[TopKPositionIds, dict[int, TopKPositionIds], int]:
    global_selection = TopKPositionIds(max_examples)
    phase_selections = {phase: TopKPositionIds(max_per_phase) for phase in range(13)}
    input_rows = 0
    with normalized_tsv.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        schema_version = normalized_header_version(reader.fieldnames)
        if schema_version == 0:
            raise RuntimeError("unexpected normalized TSV header")
        for line_number, row in enumerate(reader, start=2):
            parsed = parse_normalized_row(row, line_number, schema_version)
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
        schema_version = normalized_header_version(reader.fieldnames)
        if schema_version == 0:
            raise RuntimeError("unexpected normalized TSV header")
        header = NORMALIZED_HEADER_V2 if schema_version == 2 else NORMALIZED_HEADER_V1
        with output_path.open("w", newline="", encoding="utf-8") as output_handle:
            writer = csv.writer(output_handle, delimiter="\t", lineterminator="\n")
            writer.writerow(header)
            for line_number, row in enumerate(reader, start=2):
                parsed = parse_normalized_row(row, line_number, schema_version)
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
                if parsed.schema_version == 2 and parsed.board_id is not None:
                    assert summary.board_splits is not None
                    summary.board_splits.setdefault(parsed.board_id, set()).add(parsed.split)
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
    assert summary.board_splits is not None
    leakage_audit = board_leakage_audit(summary.board_splits) if schema_version == 2 else None
    if (
        args.strict_board_disjoint_splits
        and leakage_audit is not None
        and leakage_audit["cross_split_board_collision_count"] != 0
    ):
        raise RuntimeError(
            "exact board leakage detected across sampled splits: "
            f"{leakage_audit['cross_split_board_collision_count']} board_id collision(s)"
        )
    report = {
        "schema_version": 2,
        "normalized_schema_version": schema_version,
        "input_rows": summary.input_rows,
        "sampled_rows": summary.sampled_rows,
        "source_dataset_ids": sorted(summary.source_dataset_ids or []),
        "counts_by_split": summary.counts_by_split,
        "counts_by_phase": summary.counts_by_phase,
        "label_min": summary.label_min,
        "label_max": summary.label_max,
        "label_mean": float(f"{label_mean:.12g}"),
        "checksum": summary.checksum,
        "board_leakage_audit": leakage_audit,
        "sample_policy": {
            "method": "deterministic position_id sha256 top-k",
            "max_examples": args.max_examples,
            "max_per_phase": args.max_per_phase,
            "seed": args.seed,
            "split_policy": args.split_policy,
            "unit": "position_id groups; duplicate labels for a selected position are preserved",
            "memory_policy": "bounded position_id maps with heapq O(log K) candidate replacement",
            "strict_board_disjoint_splits": args.strict_board_disjoint_splits,
        },
    }
    sample_report_path.write_text(stable_json(report), encoding="utf-8")
    return report


def count_normalized_rows(path: Path) -> int:
    count = 0
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if normalized_header_version(reader.fieldnames) == 0:
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
        schema_version = normalized_header_version(reader.fieldnames)
        if schema_version == 0:
            raise RuntimeError("unexpected normalized TSV header")
        for line_number, row in enumerate(reader, start=2):
            parsed = parse_normalized_row(row, line_number, schema_version)
            selection.consider(parsed.record_id, sample_key(seed, parsed.record_id))

    used_rows = 0
    with input_path.open(newline="", encoding="utf-8") as input_handle:
        reader = csv.DictReader(input_handle, delimiter="\t")
        schema_version = normalized_header_version(reader.fieldnames)
        if schema_version == 0:
            raise RuntimeError("unexpected normalized TSV header")
        header = NORMALIZED_HEADER_V2 if schema_version == 2 else NORMALIZED_HEADER_V1
        with output_path.open("w", newline="", encoding="utf-8") as output_handle:
            writer = csv.writer(output_handle, delimiter="\t", lineterminator="\n")
            writer.writerow(header)
            for line_number, row in enumerate(reader, start=2):
                parsed = parse_normalized_row(row, line_number, schema_version)
                if selection.allows(parsed.record_id):
                    writer.writerow(parsed.fields)
                    used_rows += 1

    if used_rows == 0:
        raise RuntimeError("smoke position sampling produced no rows")
    return {
        "input_positions": input_rows,
        "used_positions": used_rows,
        "path": output_path,
        "policy": {
            "method": "deterministic record_id sha256 top-k",
            "selection_key": "record_id",
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
                "--pattern-set",
                args.pattern_set,
                "--output-format",
                args.dataset_output_format,
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
    mode_label = "v0c" if args.trainer_mode == "pattern-sgd-v0c" else "v0b"
    weights = output_dir / f"{mode_label}-weights.json"
    report = output_dir / f"{mode_label}-trainer-report.json"
    command = [
        sys.executable,
        str(args.trainer),
        "--dataset",
        str(dataset_tsv),
        "--mode",
        args.trainer_mode,
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
    if args.trainer_mode == "pattern-sgd-v0c":
        command.extend(["--lr-schedule", args.lr_schedule])
        if args.weight_decay is not None:
            command.extend(["--weight-decay", str(args.weight_decay)])
        if args.gradient_clip is not None:
            command.extend(["--gradient-clip", str(args.gradient_clip)])
        if args.early_stop_patience is not None:
            command.extend(["--early-stop-patience", str(args.early_stop_patience)])
    run_or_fail(command)
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
            "--pattern-set",
            args.pattern_set,
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
            "--pattern-set",
            args.pattern_set,
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
    pattern_set: str,
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
            "--pattern-set",
            pattern_set,
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
    sequence_cache: SequenceCacheInfo,
    source_fingerprints: dict[str, Any] | None,
    stages: dict[str, dict[str, Any]],
    file_sizes: dict[str, int | None],
) -> Path:
    source_ids = sample_report.get("source_dataset_ids")
    source_dataset_id = (
        source_ids[0] if isinstance(source_ids, list) and len(source_ids) == 1 else source_ids
    )
    output_files: dict[str, str] = {
        name: rel(path, output_dir) for name, path in sorted(paths.items())
    }
    sequence_import_report = None
    sequence_import_policy = None
    sequence_report_path = paths.get("sequence_import_report_json")
    if sequence_report_path is not None:
        sequence_import_report = load_json(sequence_report_path)
        sequence_import_policy = {
            "sampling_mode": sequence_import_report.get("sampling_mode"),
            "file_order": sequence_import_report.get("file_order"),
            "max_files": sequence_import_report.get("max_files"),
            "max_games": sequence_import_report.get("max_games"),
            "file_sample_rate": sequence_import_report.get("file_sample_rate"),
            "game_sample_rate": sequence_import_report.get("game_sample_rate"),
            "source_files_seen": sequence_import_report.get("source_files_seen"),
            "source_files_processed": sequence_import_report.get("source_files_processed"),
            "games_seen": sequence_import_report.get("games_seen"),
            "games_replayed": sequence_import_report.get("games_replayed"),
            "replay_skip_count": sequence_import_report.get("replay_skip_count"),
            "sampling_frame_notes": sequence_import_report.get("sampling_frame_notes"),
        }
    trainer_args = {
        "epochs": args.epochs,
        "learning_rate": args.learning_rate,
        "l2": args.l2,
        "seed": args.seed,
    }
    if args.trainer_mode == "pattern-sgd-v0c":
        trainer_args.update(
            {
                "weight_decay": args.weight_decay,
                "lr_schedule": args.lr_schedule,
                "gradient_clip": args.gradient_clip,
                "early_stop_patience": args.early_stop_patience,
            }
        )
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
        "board_leakage_audit": sample_report.get("board_leakage_audit"),
        "sample_report_checksum": sample_report.get("checksum"),
        "sequence_import_policy": sequence_import_policy,
        "sequence_cache": {
            "enabled": sequence_cache.enabled,
            "cache_dir": sequence_cache.cache_dir,
            "cache_key": sequence_cache.cache_key,
            "status": sequence_cache.status,
            "metadata_path": sequence_cache.metadata_path,
            "normalized_tsv_sha256": sequence_cache.normalized_tsv_sha256,
            "import_report_sha256": sequence_cache.import_report_sha256,
            "notes": sequence_cache.notes,
        },
        "source_fingerprints": source_fingerprints,
        "stage_timings": stages,
        "file_sizes": file_sizes,
        "dataset_output_format": dataset_report.get(
            "output_format", args.dataset_output_format
        ),
        "dataset_feature_occurrence_count": dataset_report.get("feature_occurrence_count"),
        "dataset_example_rows": dataset_report.get("example_rows"),
        "dataset_report_checksum": dataset_report.get("checksum"),
        "trainer_mode": args.trainer_mode,
        "trainer_version": trainer_report.get("trainer_version"),
        "trainer_args": trainer_args,
        "dataset_args": {
            "output_format": args.dataset_output_format,
            "pattern_set": args.pattern_set,
        },
        "pattern_set_id": v0b_export_summary.get("pattern_set_id"),
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
    stages = StageRecorder()
    sequence_cache = SequenceCacheInfo(
        enabled=args.sequence_cache_dir is not None,
        cache_dir=str(args.sequence_cache_dir) if args.sequence_cache_dir is not None else None,
        cache_key=None,
        status="bypassed",
    )
    source_fingerprints = None
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

        if args.normalized_tsv is not None:
            source_normalized = args.normalized_tsv
            stages.update(
                "source_hashing",
                status="skipped",
                wall_time_sec=0.0,
                process_cpu_time_sec=0.0,
                peak_rss_bytes=peak_rss_bytes(),
                peak_rss_note=peak_rss_note(),
                input_bytes=file_size(source_normalized),
            )
            stages.update("sequence_cache_lookup", status="skipped")
            stages.update("sequence_import_or_cache_restore", status="skipped")
        elif args.raw_input is not None:
            stages.update("source_hashing", status="skipped")
            stages.update("sequence_cache_lookup", status="skipped")
            with stages.begin("raw_import"):
                source_normalized = import_raw(args, output_dir)
            stages.update(
                "raw_import",
                output_bytes=file_size(source_normalized),
                output_rows=count_lines_after_header(source_normalized),
            )
            stages.update("sequence_import_or_cache_restore", status="skipped")
        else:
            source_normalized, sequence_cache, source_fingerprints = import_sequence(
                args, output_dir, stages
            )
        sampled_normalized = output_dir / "sampled-normalized.tsv"
        sample_report_path = output_dir / "sample-report.json"
        with stages.begin("normalized_sampling", input_bytes=file_size(source_normalized)):
            sample_report = write_sampled_normalized(
                source_normalized, sampled_normalized, sample_report_path, args
            )
        stages.update(
            "normalized_sampling",
            input_rows=sample_report.get("input_rows"),
            output_rows=sample_report.get("sampled_rows"),
            output_bytes=file_size(sampled_normalized),
            throughput_rows_per_sec=throughput(
                sample_report.get("sampled_rows"),
                stages.stages["normalized_sampling"].get("wall_time_sec"),
            ),
        )

        with stages.begin("pattern_dataset_generation", input_bytes=file_size(sampled_normalized)):
            dataset_tsv, dataset_report_path, dataset_report = run_dataset_builder(
                args, sampled_normalized, output_dir
            )
        stages.update(
            "pattern_dataset_generation",
            input_rows=sample_report.get("sampled_rows"),
            output_rows=count_lines_after_header(dataset_tsv),
            output_format=dataset_report.get("output_format", args.dataset_output_format),
            output_examples=dataset_report.get("example_rows"),
            output_feature_occurrences=dataset_report.get("feature_occurrence_count"),
            output_bytes=file_size(dataset_tsv),
            return_code=0,
        )
        with stages.begin("trainer", input_bytes=file_size(dataset_tsv)):
            v0b_weights_json, v0b_trainer_report_path, trainer_report = run_trainer(
                args, dataset_tsv, output_dir
            )
        stages.update(
            "trainer",
            output_bytes=sum_file_sizes((v0b_weights_json, v0b_trainer_report_path)),
            return_code=0,
        )
        stages.update("trainer_v0b", **stages.stages["trainer"])
        with stages.begin("v0b_export", input_bytes=file_size(v0b_weights_json)):
            v0b_artifact, v0b_manifest, v0b_export_summary = export_v0b(
                args, v0b_weights_json, output_dir
            )
        stages.update(
            "v0b_export",
            output_bytes=sum_file_sizes((v0b_artifact, v0b_manifest)),
            return_code=0,
        )
        if args.skip_v0a_baseline:
            stages.update("v0a_baseline_training_export", status="skipped")
            v0a_data = None
        else:
            with stages.begin("v0a_baseline_training_export", input_bytes=file_size(dataset_tsv)):
                v0a_data = run_v0a_baseline(args, dataset_tsv, output_dir)
            if v0a_data is not None:
                stages.update(
                    "v0a_baseline_training_export",
                    output_bytes=sum_file_sizes(v0a_data[:4]),
                    return_code=0,
                )

        eval_summary = None
        search_summary = None
        if v0a_data is not None:
            v0a_artifact = v0a_data[2]
            v0a_checksum = v0a_data[5]["weights_checksum"]
            v0b_checksum = v0b_export_summary["weights_checksum"]
            if not args.skip_eval_smoke:
                with stages.begin("evaluation_smoke", input_bytes=file_size(sampled_normalized)):
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
                        args.pattern_set,
                    )
                stages.update(
                    "evaluation_smoke",
                    input_rows=eval_positions.get("input_positions"),
                    output_rows=eval_positions.get("used_positions"),
                    return_code=0,
                )
            else:
                stages.update("evaluation_smoke", status="skipped")
                eval_positions = {}
            if not args.skip_search_smoke:
                with stages.begin("search_smoke", input_bytes=file_size(sampled_normalized)):
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
                        args.pattern_set,
                    )
                stages.update(
                    "search_smoke",
                    input_rows=search_positions.get("input_positions"),
                    output_rows=search_positions.get("used_positions"),
                    return_code=0,
                )
            else:
                stages.update("search_smoke", status="skipped")
                search_positions = {}
        else:
            stages.update("evaluation_smoke", status="skipped")
            stages.update("search_smoke", status="skipped")
            eval_positions = {}
            search_positions = {}

        paths = {
            "sampled_normalized_tsv": sampled_normalized,
            "sample_report_json": sample_report_path,
            "pattern_dataset_tsv": dataset_tsv,
            "dataset_report_json": dataset_report_path,
            "trainer_weights_json": v0b_weights_json,
            "trainer_report_json": v0b_trainer_report_path,
            "v0b_weights_json": v0b_weights_json,
            "v0b_trainer_report_json": v0b_trainer_report_path,
            "v0b_artifact_weights": v0b_artifact,
            "v0b_artifact_manifest": v0b_manifest,
        }
        if args.raw_input is not None or args.sequence_input is not None:
            paths["normalized_tsv"] = source_normalized
        if args.sequence_input is not None:
            paths["sequence_import_report_json"] = output_dir / "sequence-import-report.json"
            paths["sequence_import_stderr_log"] = output_dir / "sequence-import-stderr.log"
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

        file_sizes = {
            "source_normalized_tsv": file_size(source_normalized),
            "sampled_normalized_tsv": file_size(sampled_normalized),
            "pattern_dataset_tsv": file_size(dataset_tsv),
            "pattern_dataset": file_size(dataset_tsv),
            "trainer_weights_json": file_size(v0b_weights_json),
            "v0b_weights_json": file_size(v0b_weights_json),
            "v0b_artifact_weights": file_size(v0b_artifact),
            "reports": sum(file_size(path) or 0 for name, path in paths.items() if name.endswith("_json")),
        }
        with stages.begin("run_report_writing"):
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
                sequence_cache,
                source_fingerprints,
                stages.stages,
                file_sizes,
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
