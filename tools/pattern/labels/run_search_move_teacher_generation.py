#!/usr/bin/env python3
"""Run the local artifact-search move-teacher generator with checksum-guarded resume."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", required=True, type=Path)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--teacher-manifest", required=True, type=Path)
    parser.add_argument("--teacher-weights", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--max-depth", required=True, type=int)
    parser.add_argument("--max-nodes", required=True, type=int)
    parser.add_argument("--max-time-ms", required=True, type=int)
    parser.add_argument("--search-preset", required=True, choices=("basic", "full"))
    parser.add_argument("--exact-endgame-empties", required=True, type=int)
    parser.add_argument("--min-phase", default=0, type=int)
    parser.add_argument("--max-phase", default=9, type=int)
    parser.add_argument("--progress-every", default=0, type=int)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.max_depth < 1 or args.max_nodes < 0 or args.max_time_ms < 0:
        parser.error("search limits must be non-negative and max-depth must be positive")
    if args.max_nodes == 0 and args.max_time_ms != 0:
        parser.error("wall-clock-only teacher search is not supported")
    if not 0 <= args.exact_endgame_empties <= 60 or not 0 <= args.min_phase <= args.max_phase <= 12:
        parser.error("phase and exact-endgame bounds are invalid")
    return args


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def fingerprint(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError(f"missing required file: {path}")
    return {"path": path.name if path.is_absolute() else str(path), "sha256": sha256_file(path)}


def stable_json(data: Any) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def command(args: argparse.Namespace, move_teacher: Path, child_normalized: Path, report: Path) -> list[str]:
    return [
        str(args.generator),
        "--normalized-tsv",
        str(args.normalized_tsv),
        "--teacher-manifest",
        str(args.teacher_manifest),
        "--teacher-weights",
        str(args.teacher_weights),
        "--move-teacher-out",
        str(move_teacher),
        "--child-normalized-out",
        str(child_normalized),
        "--report-out",
        str(report),
        "--max-depth",
        str(args.max_depth),
        "--max-nodes",
        str(args.max_nodes),
        "--max-time-ms",
        str(args.max_time_ms),
        "--search-preset",
        args.search_preset,
        "--exact-endgame-empties",
        str(args.exact_endgame_empties),
        "--min-phase",
        str(args.min_phase),
        "--max-phase",
        str(args.max_phase),
        "--progress-every",
        str(args.progress_every),
    ]


def expected_metadata(args: argparse.Namespace, cmd: list[str]) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "command": cmd,
        "inputs": {
            "generator": fingerprint(args.generator),
            "normalized_tsv": fingerprint(args.normalized_tsv),
            "teacher_manifest": fingerprint(args.teacher_manifest),
            "teacher_weights": fingerprint(args.teacher_weights),
        },
    }


def complete_metadata(expected: dict[str, Any], outputs: list[Path]) -> dict[str, Any]:
    return expected | {"outputs": {path.name: fingerprint(path) for path in outputs}}


def first_mismatch(expected: Any, actual: Any, path: str = "$") -> str | None:
    if type(expected) is not type(actual):
        return path
    if isinstance(expected, dict):
        if set(expected) != set(actual):
            return path
        for key in sorted(expected):
            mismatch = first_mismatch(expected[key], actual[key], f"{path}.{key}")
            if mismatch is not None:
                return mismatch
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            return path
        for index, (left, right) in enumerate(zip(expected, actual)):
            mismatch = first_mismatch(left, right, f"{path}[{index}]")
            if mismatch is not None:
                return mismatch
    elif expected != actual:
        return path
    return None


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    move_teacher = args.output_dir / "search-move-teacher.tsv"
    child_normalized = args.output_dir / "search-move-teacher-child-normalized.tsv"
    report = args.output_dir / "search-move-teacher-report.json"
    sidecar = args.output_dir / "search-move-teacher.resume.json"
    outputs = [move_teacher, child_normalized, report]
    cmd = command(args, move_teacher, child_normalized, report)
    try:
        expected = expected_metadata(args, cmd)
        if args.resume and all(path.is_file() for path in outputs):
            if not sidecar.is_file():
                raise RuntimeError(f"resume metadata missing: {sidecar}")
            actual = json.loads(sidecar.read_text(encoding="utf-8"))
            mismatch = first_mismatch(complete_metadata(expected, outputs), actual)
            if mismatch is not None:
                raise RuntimeError(f"resume metadata mismatch at {mismatch}; rerun without --resume")
            print(f"move_teacher={move_teacher}")
            print(f"child_normalized={child_normalized}")
            print(f"report={report}")
            print("status=skipped-resume-validated")
            return 0
        if args.resume and any(path.exists() for path in outputs):
            raise RuntimeError("partial prior outputs found; rerun without --resume after inspecting them")
        result = subprocess.run(cmd, check=False)
        if result.returncode != 0:
            return result.returncode
        if not all(path.is_file() for path in outputs):
            raise RuntimeError("generator succeeded without producing every required output")
        sidecar.write_text(stable_json(complete_metadata(expected, outputs)), encoding="utf-8")
        return 0
    except (OSError, json.JSONDecodeError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
