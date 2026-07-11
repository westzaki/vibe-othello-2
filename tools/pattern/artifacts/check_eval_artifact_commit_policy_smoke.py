#!/usr/bin/env python3
"""Smoke tests for the evaluation artifact commit-policy checker."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, check=False, capture_output=True, text=True)


def write_json(path: Path, payload: object) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def tiny_weights() -> tuple[bytes, list[dict[str, int | bool]]]:
    pattern_set_id = b"tiny"
    phase_count = 13
    phase_stride = 2
    weights = [0] * (phase_count * phase_stride)
    weights[10 * phase_stride] = 1
    weights[11 * phase_stride + 1] = -3
    payload = bytearray(b"VOPWGT\0\0")
    payload.extend(
        struct.pack(
            "<HHHHHHHHI",
            1,
            1,
            1,
            1,
            phase_count,
            1,
            len(pattern_set_id),
            0,
            len(weights),
        )
    )
    payload.extend(pattern_set_id)
    payload.extend(struct.pack(f"<{len(weights)}i", *weights))
    payload.extend(struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF))
    diagnostics = [
        {
            "phase": phase,
            "nonzero_pattern_weights": int(phase == 11),
            "nonzero_phase_bias": phase == 10,
            "max_absolute_weight": 3 if phase == 11 else 0,
        }
        for phase in range(phase_count)
    ]
    return bytes(payload), diagnostics


def make_valid_repo(repo: Path) -> None:
    artifact = repo / "data/eval/artifacts/tiny"
    artifact.mkdir(parents=True)
    (repo / "data/eval/.gitignore").write_text("/local/\n", encoding="utf-8")
    (repo / "data/eval/README.md").write_text("# Evaluation Artifacts\n", encoding="utf-8")
    write_json(
        repo / "data/eval/default-artifact.json",
        {
            "schema_version": 1,
            "default_artifact_id": "tiny",
            "status": "experimental-default",
            "artifact_manifest": "artifacts/tiny/manifest.json",
            "artifact_provenance": "artifacts/tiny/provenance.json",
        },
    )

    weights, diagnostics = tiny_weights()
    (artifact / "weights.bin").write_bytes(weights)
    weights_sha256 = f"sha256:{hashlib.sha256(weights).hexdigest()}"
    write_json(
        artifact / "manifest.json",
        {
            "artifact_id": "tiny",
            "format": "vibe-othello-pattern-eval",
            "format_version": 1,
            "bit_order": "a1-lsb",
            "score_unit": "disc-diff",
            "score_scale": 1,
            "phase_count": 13,
            "pattern_set_id": "fixed-pattern-fixture-v1",
            "weights_file": "weights.bin",
            "weights_checksum": f"0x{zlib.crc32(weights[:-4]) & 0xFFFFFFFF:08x}",
            "trained_phases": [10, 11],
            "nonzero_pattern_weights": 1,
            "phase_weight_diagnostics": diagnostics,
        },
    )
    write_json(
        artifact / "provenance.json",
        {
            "schema_version": 1,
            "artifact_id": "tiny",
            "raw_data_redistributed": False,
            "teacher_labels_redistributed": False,
            "not_official_egaroucid_artifact": True,
            "weights_sha256": weights_sha256,
            "runtime_checksum": f"0x{zlib.crc32(weights[:-4]) & 0xFFFFFFFF:08x}",
            "trained_phases": [10, 11],
        },
    )
    (artifact / "README.md").write_text("# tiny\n", encoding="utf-8")
    (artifact / "NOTICE.md").write_text("not official\n", encoding="utf-8")


def run_checker(checker: Path, repo: Path) -> subprocess.CompletedProcess[str]:
    return run([sys.executable, str(checker), "--repo-root", str(repo)], cwd=repo)


def require_failure(checker: Path, repo: Path, expected: str) -> bool:
    result = run_checker(checker, repo)
    combined_output = result.stdout + result.stderr
    if result.returncode == 0:
        print(f"invalid fixture unexpectedly passed: {expected}", file=sys.stderr)
        return False
    if expected not in combined_output:
        print(f"invalid fixture failed for an unexpected reason: {expected}", file=sys.stderr)
        print(combined_output, file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checker", required=True, type=Path)
    args = parser.parse_args()
    checker = args.checker.resolve()

    with tempfile.TemporaryDirectory() as temp_dir:
        repo = Path(temp_dir) / "repo"
        repo.mkdir()
        init = run(["git", "init"], cwd=repo)
        if init.returncode != 0:
            print(init.stderr, file=sys.stderr)
            return 1

        make_valid_repo(repo)
        add_valid = run(["git", "add", "-A"], cwd=repo)
        if add_valid.returncode != 0:
            print(add_valid.stderr, file=sys.stderr)
            return 1
        valid = run_checker(checker, repo)
        if valid.returncode != 0:
            print("valid fixture unexpectedly failed", file=sys.stderr)
            print(valid.stdout, file=sys.stderr)
            print(valid.stderr, file=sys.stderr)
            return 1

        manifest_path = repo / "data/eval/artifacts/tiny/manifest.json"
        provenance_path = repo / "data/eval/artifacts/tiny/provenance.json"
        valid_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        valid_provenance = json.loads(provenance_path.read_text(encoding="utf-8"))

        mismatch_provenance = dict(valid_provenance)
        mismatch_provenance["trained_phases"] = [10]
        write_json(provenance_path, mismatch_provenance)
        if not require_failure(checker, repo, "manifest and provenance trained_phases do not match"):
            return 1
        write_json(provenance_path, valid_provenance)

        missing_provenance = dict(valid_provenance)
        missing_provenance.pop("trained_phases")
        write_json(provenance_path, missing_provenance)
        if not require_failure(checker, repo, "trained_phases must be a non-empty array"):
            return 1
        write_json(provenance_path, valid_provenance)

        duplicate_manifest = dict(valid_manifest)
        duplicate_manifest["trained_phases"] = [10, 10]
        write_json(manifest_path, duplicate_manifest)
        if not require_failure(checker, repo, "trained_phases entries must be unique"):
            return 1
        write_json(manifest_path, valid_manifest)

        out_of_range_manifest = dict(valid_manifest)
        out_of_range_manifest["trained_phases"] = [13]
        write_json(manifest_path, out_of_range_manifest)
        if not require_failure(checker, repo, "trained_phases entries must be in [0, 12]"):
            return 1
        write_json(manifest_path, valid_manifest)

        mismatched_diagnostics = dict(valid_manifest)
        mismatched_diagnostics["phase_weight_diagnostics"] = []
        write_json(manifest_path, mismatched_diagnostics)
        if not require_failure(checker, repo, "phase_weight_diagnostics does not match weights.bin"):
            return 1
        write_json(manifest_path, valid_manifest)

        local_payload = repo / "data/eval/local/foo.bin"
        local_payload.parent.mkdir(parents=True)
        local_payload.write_bytes(b"local")
        force_add = run(["git", "add", "-f", "data/eval/local/foo.bin"], cwd=repo)
        if force_add.returncode != 0:
            print(force_add.stderr, file=sys.stderr)
            return 1

        if not require_failure(
            checker, repo, "data/eval/local/foo.bin: not allowed by evaluation artifact commit policy"
        ):
            return 1

    print("ok: evaluation artifact commit policy smoke")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
