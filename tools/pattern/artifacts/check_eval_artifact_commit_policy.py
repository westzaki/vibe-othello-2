#!/usr/bin/env python3
"""Validate committed evaluation artifact policy."""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path
from typing import Any


ARTIFACT_ROOT = Path("data/eval/artifacts")
DEFAULT_POINTER = Path("data/eval/default-artifact.json")

REQUIRED_ARTIFACT_FILES = {
    "weights.bin",
    "manifest.json",
    "provenance.json",
    "README.md",
    "NOTICE.md",
}

ALLOWED_EVAL_ROOT_FILES = {
    Path("data/eval/.gitignore"),
    Path("data/eval/README.md"),
    DEFAULT_POINTER,
}

LOCAL_PATH_MARKERS = (
    "/Users/",
    "/home/",
    "C:\\",
    "$VIBE_OTHELLO_MEASUREMENTS",
    "VIBE_OTHELLO_MEASUREMENTS",
)

RUNTIME_PHASE_COUNT = 13
WEIGHTS_HEADER_FORMAT = "<HHHHHHHHI"
WEIGHTS_HEADER_SIZE = 8 + struct.calcsize(WEIGHTS_HEADER_FORMAT)

FORBIDDEN_ARCHIVE_PATTERNS = (
    "*.zip",
    "*.7z",
    "*.tar",
    "*.tar.gz",
)

FORBIDDEN_DATA_PATTERNS = (
    "*.tsv",
    "*.log",
)

FORBIDDEN_GENERATED_MARKERS = (
    "normalized",
    "selected",
    "teacher-label",
    "teacher_labels",
    "move-teacher",
    "move_teacher",
    "child-normalized",
    "child_normalized",
    "cache",
    "report.json",
    "trainer-report",
    "dataset",
)

EXCEPTION_PREFIXES = (
    "tools/",
    "tests/",
    "engine/fixtures/",
    "data/corpora/samples/",
)


def load_json(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: invalid JSON: {exc}") from exc
    except OSError as exc:
        raise ValueError(f"{path}: cannot read file: {exc}") from exc


def git_ls_files(repo_root: Path) -> list[Path]:
    completed = subprocess.run(
        ["git", "-C", str(repo_root), "ls-files"],
        check=False,
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise ValueError(f"git ls-files failed:\n{completed.stderr}")
    return [Path(line) for line in completed.stdout.splitlines() if line]


def is_fixture_or_sample(path: Path) -> bool:
    text = path.as_posix()
    return any(text.startswith(prefix) for prefix in EXCEPTION_PREFIXES)


def is_source_or_doc(path: Path) -> bool:
    return path.suffix in {".cc", ".h", ".py", ".md", ".cmake"} or path.name == "CMakeLists.txt"


def matches_any(path: Path, patterns: tuple[str, ...]) -> bool:
    text = path.as_posix()
    return any(
        fnmatch.fnmatch(text, pattern) or fnmatch.fnmatch(path.name, pattern)
        for pattern in patterns
    )


def check_forbidden_tracked_files(tracked_files: list[Path]) -> list[str]:
    errors: list[str] = []
    for path in tracked_files:
        text = path.as_posix()
        lower_text = text.lower()
        lower_name = path.name.lower()
        if is_fixture_or_sample(path):
            continue
        if matches_any(path, FORBIDDEN_ARCHIVE_PATTERNS):
            errors.append(f"{path}: archive payloads must not be committed")
            continue
        if matches_any(path, FORBIDDEN_DATA_PATTERNS):
            errors.append(f"{path}: generated TSV/log files must not be committed")
            continue
        if is_source_or_doc(path):
            continue
        if text.startswith("data/eval/") and any(
            marker in lower_text for marker in FORBIDDEN_GENERATED_MARKERS
        ):
            errors.append(f"{path}: generated evaluation intermediates must not be committed")
            continue
        if lower_name.endswith("report.json") or "trainer-report" in lower_name:
            errors.append(f"{path}: generated reports must not be committed")
    return errors


def relative_to_repo(path: Path, repo_root: Path) -> Path:
    return path.relative_to(repo_root)


def check_eval_tree(repo_root: Path) -> list[str]:
    errors: list[str] = []
    eval_root = repo_root / "data/eval"
    if not eval_root.is_dir():
        return [f"{eval_root}: missing evaluation data directory"]

    for path in eval_root.rglob("*"):
        if path.is_dir():
            continue
        rel = relative_to_repo(path, repo_root)
        if rel in ALLOWED_EVAL_ROOT_FILES:
            continue
        parts = rel.parts
        if (
            len(parts) == 5
            and Path(*parts[:3]) == ARTIFACT_ROOT
            and parts[4] in REQUIRED_ARTIFACT_FILES
        ):
            continue
        errors.append(f"{rel}: not allowed by evaluation artifact commit policy")

    artifact_root = repo_root / ARTIFACT_ROOT
    if not artifact_root.is_dir():
        errors.append(f"{ARTIFACT_ROOT}: missing artifact directory")
        return errors

    artifact_dirs = sorted(path for path in artifact_root.iterdir() if path.is_dir())
    if not artifact_dirs:
        errors.append(f"{ARTIFACT_ROOT}: at least one artifact directory is required")
    for artifact_dir in artifact_dirs:
        present = {path.name for path in artifact_dir.iterdir() if path.is_file()}
        missing = sorted(REQUIRED_ARTIFACT_FILES.difference(present))
        extra = sorted(present.difference(REQUIRED_ARTIFACT_FILES))
        if missing:
            errors.append(
                f"{relative_to_repo(artifact_dir, repo_root)}: missing files: {', '.join(missing)}"
            )
        if extra:
            errors.append(
                f"{relative_to_repo(artifact_dir, repo_root)}: unexpected files: {', '.join(extra)}"
            )
    return errors


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def runtime_checksum(path: Path) -> str:
    payload = path.read_bytes()
    if len(payload) < 4:
        raise ValueError(f"{path}: weights.bin is too small")
    return f"0x{zlib.crc32(payload[:-4]) & 0xFFFFFFFF:08x}"


def trained_phases(path: Path, payload: dict[str, Any], errors: list[str]) -> list[int] | None:
    value = payload.get("trained_phases")
    if not isinstance(value, list):
        errors.append(f"{path}: trained_phases must be a non-empty array")
        return None
    if not value:
        errors.append(f"{path}: trained_phases must not be empty")
        return None
    if any(isinstance(phase, bool) or not isinstance(phase, int) for phase in value):
        errors.append(f"{path}: trained_phases entries must be integers")
        return None
    if any(phase < 0 or phase >= RUNTIME_PHASE_COUNT for phase in value):
        errors.append(f"{path}: trained_phases entries must be in [0, 12]")
        return None
    if len(set(value)) != len(value):
        errors.append(f"{path}: trained_phases entries must be unique")
        return None
    return sorted(value)


def phase_weight_diagnostics(path: Path) -> list[dict[str, int | bool]]:
    payload = path.read_bytes()
    if len(payload) < WEIGHTS_HEADER_SIZE + 4:
        raise ValueError(f"{path}: weights.bin is too small for phase diagnostics")
    if payload[:8] != b"VOPWGT\0\0":
        raise ValueError(f"{path}: weights.bin has an invalid magic header")

    (
        format_version,
        bit_order,
        score_unit,
        score_scale,
        phase_count,
        _pattern_count,
        pattern_set_id_size,
        _reserved,
        weight_count,
    ) = struct.unpack_from(WEIGHTS_HEADER_FORMAT, payload, 8)
    if (format_version, bit_order, score_unit, score_scale) != (1, 1, 1, 1):
        raise ValueError(f"{path}: weights.bin header is not a runtime v1 artifact")
    if phase_count != RUNTIME_PHASE_COUNT:
        raise ValueError(f"{path}: weights.bin phase_count must be {RUNTIME_PHASE_COUNT}")
    if weight_count % phase_count != 0:
        raise ValueError(f"{path}: weights.bin weight_count is not divisible by phase_count")

    weights_offset = WEIGHTS_HEADER_SIZE + pattern_set_id_size
    expected_size = weights_offset + weight_count * 4 + 4
    if len(payload) != expected_size:
        raise ValueError(f"{path}: weights.bin size does not match its header")

    weights = struct.unpack_from(f"<{weight_count}i", payload, weights_offset)
    phase_stride = weight_count // phase_count
    diagnostics: list[dict[str, int | bool]] = []
    for phase in range(phase_count):
        start = phase * phase_stride
        phase_bias = weights[start]
        pattern_weights = weights[start + 1 : start + phase_stride]
        diagnostics.append(
            {
                "phase": phase,
                "nonzero_pattern_weights": sum(weight != 0 for weight in pattern_weights),
                "nonzero_phase_bias": phase_bias != 0,
                "max_absolute_weight": max((abs(weight) for weight in pattern_weights), default=0),
            }
        )
    return diagnostics


def check_phase_weight_diagnostics(
    path: Path, manifest: dict[str, Any], actual: list[dict[str, int | bool]], errors: list[str]
) -> None:
    value = manifest.get("phase_weight_diagnostics")
    if value != actual:
        errors.append(f"{path}: phase_weight_diagnostics does not match weights.bin")
    expected_nonzero = sum(int(diagnostic["nonzero_pattern_weights"]) for diagnostic in actual)
    if manifest.get("nonzero_pattern_weights") != expected_nonzero:
        errors.append(
            f"{path}: nonzero_pattern_weights {manifest.get('nonzero_pattern_weights')!r} "
            f"does not match {expected_nonzero}"
        )


def require_bool(
    path: Path, payload: dict[str, Any], field: str, expected: bool, errors: list[str]
) -> None:
    if payload.get(field) is not expected:
        errors.append(f"{path}: {field} must be {str(expected).lower()}")


def check_artifact_metadata(repo_root: Path) -> list[str]:
    errors: list[str] = []
    for artifact_dir in sorted((repo_root / ARTIFACT_ROOT).iterdir()):
        if not artifact_dir.is_dir():
            continue
        rel_dir = relative_to_repo(artifact_dir, repo_root)
        weights = artifact_dir / "weights.bin"
        manifest_path = artifact_dir / "manifest.json"
        provenance_path = artifact_dir / "provenance.json"
        if not weights.is_file():
            continue
        if weights.stat().st_size <= 0:
            errors.append(f"{rel_dir / 'weights.bin'}: weights.bin must be non-empty")

        try:
            manifest = load_json(manifest_path)
            provenance = load_json(provenance_path)
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if not isinstance(manifest, dict):
            errors.append(f"{relative_to_repo(manifest_path, repo_root)}: root must be an object")
            continue
        if not isinstance(provenance, dict):
            errors.append(f"{relative_to_repo(provenance_path, repo_root)}: root must be an object")
            continue

        manifest_trained_phases = trained_phases(manifest_path, manifest, errors)
        provenance_trained_phases = trained_phases(provenance_path, provenance, errors)
        if (
            manifest_trained_phases is not None
            and provenance_trained_phases is not None
            and manifest_trained_phases != provenance_trained_phases
        ):
            errors.append(
                f"{rel_dir}: manifest and provenance trained_phases do not match "
                f"({manifest_trained_phases!r} != {provenance_trained_phases!r})"
            )

        if manifest.get("weights_file") != "weights.bin":
            errors.append(
                f"{relative_to_repo(manifest_path, repo_root)}: weights_file must be weights.bin"
            )

        actual_runtime_checksum = runtime_checksum(weights)
        manifest_checksum = manifest.get("weights_checksum")
        if manifest_checksum != actual_runtime_checksum:
            errors.append(
                f"{relative_to_repo(manifest_path, repo_root)}: weights_checksum {manifest_checksum!r} "
                f"does not match {actual_runtime_checksum}"
            )
        provenance_runtime_checksum = provenance.get("runtime_checksum")
        if provenance_runtime_checksum != actual_runtime_checksum:
            errors.append(
                f"{relative_to_repo(provenance_path, repo_root)}: runtime_checksum {provenance_runtime_checksum!r} "
                f"does not match {actual_runtime_checksum}"
            )

        actual_sha256 = sha256_file(weights)
        provenance_sha256 = provenance.get("weights_sha256")
        if provenance_sha256 != actual_sha256:
            errors.append(
                f"{relative_to_repo(provenance_path, repo_root)}: weights_sha256 {provenance_sha256!r} "
                f"does not match {actual_sha256}"
            )

        try:
            diagnostics = phase_weight_diagnostics(weights)
        except ValueError as exc:
            errors.append(str(exc))
        else:
            check_phase_weight_diagnostics(manifest_path, manifest, diagnostics, errors)

        require_bool(provenance_path, provenance, "raw_data_redistributed", False, errors)
        require_bool(provenance_path, provenance, "teacher_labels_redistributed", False, errors)
        require_bool(provenance_path, provenance, "not_official_egaroucid_artifact", True, errors)

    return errors


def check_default_pointer(repo_root: Path) -> list[str]:
    errors: list[str] = []
    pointer_path = repo_root / DEFAULT_POINTER
    try:
        pointer = load_json(pointer_path)
    except ValueError as exc:
        return [str(exc)]
    if not isinstance(pointer, dict):
        return [f"{DEFAULT_POINTER}: root must be an object"]
    manifest = pointer.get("artifact_manifest")
    if not isinstance(manifest, str) or not manifest:
        errors.append(f"{DEFAULT_POINTER}: artifact_manifest must be a non-empty string")
        return errors
    manifest_path = Path(manifest)
    if manifest_path.is_absolute() or ".." in manifest_path.parts:
        errors.append(f"{DEFAULT_POINTER}: artifact_manifest must be relative and stay inside data/eval")
        return errors
    if not (repo_root / "data/eval" / manifest_path).is_file():
        errors.append(f"{DEFAULT_POINTER}: artifact_manifest target does not exist: {manifest}")
    return errors


def check_local_path_markers(repo_root: Path) -> list[str]:
    errors: list[str] = []
    paths = [
        repo_root / DEFAULT_POINTER,
        repo_root / "data/eval/README.md",
        repo_root / "docs/architecture/evaluation-artifacts.md",
    ]
    paths.extend((repo_root / ARTIFACT_ROOT).glob("*/*.json"))
    paths.extend((repo_root / ARTIFACT_ROOT).glob("*/*.md"))
    for path in paths:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        for marker in LOCAL_PATH_MARKERS:
            if marker in text:
                errors.append(f"{relative_to_repo(path, repo_root)}: contains local path marker {marker!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    args = parser.parse_args()

    repo_root = args.repo_root.resolve()
    errors: list[str] = []
    try:
        tracked_files = git_ls_files(repo_root)
        errors.extend(check_forbidden_tracked_files(tracked_files))
        errors.extend(check_eval_tree(repo_root))
        errors.extend(check_artifact_metadata(repo_root))
        errors.extend(check_default_pointer(repo_root))
        errors.extend(check_local_path_markers(repo_root))
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print("ok: evaluation artifact commit policy")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
