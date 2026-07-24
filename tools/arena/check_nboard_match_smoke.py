#!/usr/bin/env python3
"""CTest smoke coverage for NBoard dialects and report identity."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


def make_tiny_artifact(weights_path: Path, manifest_path: Path) -> None:
    pattern_set_id = "fixed-pattern-fixture-v1"
    phase_count = 13
    weight_count = (1 + 3**8 + 3**9) * phase_count
    payload = bytearray(b"VOPWGT\0\0")
    payload.extend(
        struct.pack(
            "<HHHHHHHHI",
            1,
            1,
            1,
            1,
            phase_count,
            2,
            len(pattern_set_id),
            0,
            weight_count,
        )
    )
    payload.extend(pattern_set_id.encode("utf-8"))
    payload.extend(b"\0" * (weight_count * 4))
    checksum = f"0x{zlib.crc32(payload) & 0xFFFFFFFF:08x}"
    payload.extend(struct.pack("<I", int(checksum, 16)))
    weights_path.write_bytes(payload)
    manifest_path.write_text(
        json.dumps(
            {
                "format_version": 1,
                "bit_order": "a1-lsb",
                "score_unit": "disc-diff",
                "score_scale": 1,
                "phase_count": phase_count,
                "trained_phases": [10, 11, 12],
                "pattern_set_id": pattern_set_id,
                "weights_file": weights_path.name,
                "weights_checksum": checksum,
                "notes": "temporary NBoard match smoke artifact",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def run_match(exe: str, fake_engine: str, temp_dir: Path, protocol: int) -> None:
    engine_name = f"fake-nboard-v{protocol}"
    runtime_id = f"strict-smoke-v{protocol}"
    report_path = temp_dir / f"report-v{protocol}.json"
    stderr_path = temp_dir / f"fake-v{protocol}.stderr"
    command = [
        exe,
        "--nboard-exe",
        fake_engine,
        "--nboard-working-directory",
        str(temp_dir),
        "--nboard-stderr",
        str(stderr_path),
        "--nboard-name",
        engine_name,
        "--nboard-runtime-id",
        runtime_id,
        "--nboard-protocol",
        str(protocol),
        "--nboard-arg",
        "--expect-protocol",
        "--nboard-arg",
        str(protocol),
        "--nboard-depth",
        "2",
        "--artifact-manifest",
        str(temp_dir / "fixture.manifest.json"),
        "--openings",
        str(temp_dir / "openings.txt"),
        "--max-openings",
        "1",
        "--time-ms",
        "5",
        "--tt-bytes",
        "1048576",
        "--exact-endgame-empties",
        "0",
        "--report-out",
        str(report_path),
        "--no-warmup",
    ]
    completed = run(command)
    if completed.returncode != 0:
        fake_stderr = stderr_path.read_text(encoding="utf-8") if stderr_path.exists() else ""
        raise AssertionError(
            f"{command} exited {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}\n"
            f"fake stderr:\n{fake_stderr}"
        )

    report = json.loads(report_path.read_text(encoding="utf-8"))
    expected_identity = {
        "engine_name": engine_name,
        "executable": fake_engine,
        "runtime_id": runtime_id,
        "protocol_version": protocol,
        "depth": 2,
        "arguments": ["--expect-protocol", str(protocol)],
    }
    if report.get("schema_version") != 3 or report.get("baseline") != engine_name:
        raise AssertionError(f"unexpected report header: {report!r}")
    if report.get("nboard") != expected_identity:
        raise AssertionError(f"unexpected NBoard identity: {report.get('nboard')!r}")
    if report.get("summary", {}).get("games") != 2 or len(report.get("games", [])) != 2:
        raise AssertionError(f"paired games missing from report: {report!r}")
    for game in report["games"]:
        if {game["black"], game["white"]} != {"vibe", engine_name}:
            raise AssertionError(f"engine identity missing from game: {game!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--fake-engine", required=True)
    args = parser.parse_args()

    invalid_protocol = run([args.exe, "--nboard-protocol", "3"])
    if invalid_protocol.returncode != 2 or "expected 1 or 2" not in invalid_protocol.stderr:
        raise AssertionError(f"invalid protocol was not rejected: {invalid_protocol!r}")

    with tempfile.TemporaryDirectory(prefix="vibe-nboard-smoke-") as temp:
        temp_dir = Path(temp)
        make_tiny_artifact(temp_dir / "fixture.weights.bin", temp_dir / "fixture.manifest.json")
        (temp_dir / "openings.txt").write_text("start:\n", encoding="utf-8")
        for protocol in (1, 2):
            run_match(args.exe, args.fake_engine, temp_dir, protocol)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
