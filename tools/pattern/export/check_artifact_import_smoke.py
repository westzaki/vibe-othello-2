#!/usr/bin/env python3
"""Check runtime artifact to trainer JSON import and byte-identical re-export."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--exporter", required=True, type=Path)
    parser.add_argument("--catalog-dump-exe", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--weights", required=True, type=Path)
    return parser.parse_args()


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def main() -> int:
    args = parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix="vibe-othello-artifact-import-") as temp:
            root = Path(temp)
            weights_json = root / "weights.json"
            import_report = root / "import-report.json"
            run(
                [
                    sys.executable,
                    str(args.importer),
                    "--manifest",
                    str(args.manifest),
                    "--weights",
                    str(args.weights),
                    "--weights-json-out",
                    str(weights_json),
                    "--report-out",
                    str(import_report),
                    "--catalog-dump-exe",
                    str(args.catalog_dump_exe),
                ]
            )
            source_manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
            imported = json.loads(weights_json.read_text(encoding="utf-8"))
            report = json.loads(import_report.read_text(encoding="utf-8"))
            if (
                imported.get("weights_schema_version") != "pattern-eval-weights-v2"
                or imported.get("pattern_set_id") != source_manifest.get("pattern_set_id")
                or report.get("source_trained_phases") != source_manifest.get("trained_phases")
                or report.get("nonzero_pattern_weight_count", 0) <= 0
            ):
                raise RuntimeError(f"artifact import metadata is incomplete: {report!r}")

            reexported = root / "reexported.weights.bin"
            reexported_manifest = root / "reexported.manifest.json"
            command = [
                sys.executable,
                str(args.exporter),
                "--weights-json",
                str(weights_json),
                "--weights-out",
                str(reexported),
                "--manifest-out",
                str(reexported_manifest),
                "--pattern-set",
                str(source_manifest["pattern_set_id"]),
                "--score-scale",
                str(source_manifest["score_scale"]),
                "--catalog-dump-exe",
                str(args.catalog_dump_exe),
            ]
            trained_phases = source_manifest.get("trained_phases")
            if isinstance(trained_phases, list):
                command.extend(["--trained-phases", *[str(value) for value in trained_phases]])
            fallback = source_manifest.get("fallback_additive_through_phase")
            if isinstance(fallback, int):
                command.extend(["--fallback-additive-through-phase", str(fallback)])
            run(command)
            if reexported.read_bytes() != args.weights.read_bytes():
                raise RuntimeError("artifact import and re-export were not byte-identical")

            invalid_manifest = dict(source_manifest)
            invalid_manifest["patterns"] = list(reversed(source_manifest["patterns"]))
            invalid_manifest_path = root / "invalid-layout.manifest.json"
            invalid_manifest_path.write_text(
                json.dumps(invalid_manifest), encoding="utf-8"
            )
            rejected = subprocess.run(
                [
                    sys.executable,
                    str(args.importer),
                    "--manifest",
                    str(invalid_manifest_path),
                    "--weights",
                    str(args.weights),
                    "--weights-json-out",
                    str(root / "invalid.weights.json"),
                    "--report-out",
                    str(root / "invalid.report.json"),
                    "--catalog-dump-exe",
                    str(args.catalog_dump_exe),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            if (
                rejected.returncode == 0
                or "manifest and catalog pattern layout differ" not in rejected.stderr
            ):
                raise RuntimeError("artifact import accepted a reordered manifest layout")

    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
