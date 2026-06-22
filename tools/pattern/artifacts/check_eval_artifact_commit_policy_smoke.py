#!/usr/bin/env python3
"""Smoke tests for the evaluation artifact commit-policy checker."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, check=False, capture_output=True, text=True)


def write_json(path: Path, payload: object) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


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

    weights = b"abcd"
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
            "weights_checksum": "0x00000000",
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
            "runtime_checksum": "0x00000000",
        },
    )
    (artifact / "README.md").write_text("# tiny\n", encoding="utf-8")
    (artifact / "NOTICE.md").write_text("not official\n", encoding="utf-8")


def run_checker(checker: Path, repo: Path) -> subprocess.CompletedProcess[str]:
    return run([sys.executable, str(checker), "--repo-root", str(repo)], cwd=repo)


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

        local_payload = repo / "data/eval/local/foo.bin"
        local_payload.parent.mkdir(parents=True)
        local_payload.write_bytes(b"local")
        force_add = run(["git", "add", "-f", "data/eval/local/foo.bin"], cwd=repo)
        if force_add.returncode != 0:
            print(force_add.stderr, file=sys.stderr)
            return 1

        invalid = run_checker(checker, repo)
        combined_output = invalid.stdout + invalid.stderr
        if invalid.returncode == 0:
            print("force-added data/eval/local payload unexpectedly passed", file=sys.stderr)
            return 1
        if "data/eval/local/foo.bin: not allowed by evaluation artifact commit policy" not in combined_output:
            print("force-added local payload failed for an unexpected reason", file=sys.stderr)
            print(combined_output, file=sys.stderr)
            return 1

    print("ok: evaluation artifact commit policy smoke")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
