#!/usr/bin/env python3
"""Import a runtime pattern artifact into deterministic trainer weights JSON."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import sys
import zlib
from pathlib import Path
from typing import Any


HEADER = struct.Struct("<HHHHHHHHI")
MAGIC = b"VOPWGT\0\0"
FORMAT_VERSION = 1
PHASE_MAPPING_ID = "disc-count-13-v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--weights", type=Path)
    parser.add_argument("--weights-json-out", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    parser.add_argument("--catalog-dump-exe", required=True, type=Path)
    args = parser.parse_args()
    if args.weights_json_out == args.report_out:
        parser.error("--weights-json-out and --report-out must differ")
    return args


def load_object(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read JSON object {path}: {error}") from error
    if not isinstance(payload, dict):
        raise RuntimeError(f"JSON root must be an object: {path}")
    return payload


def require_int(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise RuntimeError(f"{field} must be an integer")
    return value


def require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"{field} must be a non-empty string")
    return value


def selected_contract(catalog: dict[str, Any], pattern_set_id: str) -> dict[str, Any]:
    pattern_sets = catalog.get("pattern_sets")
    if not isinstance(pattern_sets, list):
        raise RuntimeError("catalog dump has no pattern_sets array")
    matches = [
        item
        for item in pattern_sets
        if isinstance(item, dict) and item.get("pattern_set_id") == pattern_set_id
    ]
    if len(matches) != 1:
        raise RuntimeError(f"catalog dump did not contain exactly one {pattern_set_id!r}")
    return matches[0]


def load_contract(executable: Path, pattern_set_id: str) -> dict[str, Any]:
    result = subprocess.run(
        [str(executable), "--pattern-set", pattern_set_id, "--index-mode", "raw"],
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"pattern catalog dump failed ({result.returncode}): {result.stderr.strip()}"
        )
    try:
        catalog = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"pattern catalog dump is invalid JSON: {error}") from error
    if not isinstance(catalog, dict):
        raise RuntimeError("pattern catalog dump root must be an object")
    return selected_contract(catalog, pattern_set_id)


def resolved_weights_path(manifest_path: Path, manifest: dict[str, Any], override: Path | None) -> Path:
    if override is not None:
        return override
    relative = Path(require_string(manifest.get("weights_file"), "manifest.weights_file"))
    if relative.is_absolute() or ".." in relative.parts:
        raise RuntimeError("manifest weights_file must be a safe relative path")
    return manifest_path.parent / relative


def parse_artifact(
    manifest: dict[str, Any], artifact: bytes, contract: dict[str, Any]
) -> tuple[dict[str, Any], dict[str, Any]]:
    minimum_size = len(MAGIC) + HEADER.size + 4
    if len(artifact) < minimum_size or artifact[: len(MAGIC)] != MAGIC:
        raise RuntimeError("runtime weights have invalid magic or are truncated")
    (
        format_version,
        bit_order,
        score_unit,
        score_scale,
        phase_count,
        pattern_count,
        name_length,
        reserved,
        weight_count,
    ) = HEADER.unpack_from(artifact, len(MAGIC))
    if (
        format_version != FORMAT_VERSION
        or bit_order != 1
        or score_unit != 1
        or score_scale <= 0
        or phase_count != 13
        or reserved != 0
    ):
        raise RuntimeError("runtime weights header is incompatible with trainer import")
    name_offset = len(MAGIC) + HEADER.size
    weights_offset = name_offset + name_length
    expected_size = weights_offset + weight_count * 4 + 4
    if expected_size != len(artifact):
        raise RuntimeError("runtime weights size does not match header")
    pattern_set_id = artifact[name_offset:weights_offset].decode("utf-8")
    if pattern_set_id != require_string(manifest.get("pattern_set_id"), "manifest.pattern_set_id"):
        raise RuntimeError("runtime and manifest pattern_set_id differ")
    if pattern_set_id != contract.get("pattern_set_id"):
        raise RuntimeError("runtime and catalog pattern_set_id differ")
    if require_int(manifest.get("score_scale"), "manifest.score_scale") != score_scale:
        raise RuntimeError("runtime and manifest score_scale differ")
    if require_int(manifest.get("phase_count"), "manifest.phase_count") != phase_count:
        raise RuntimeError("runtime and manifest phase_count differ")
    expected_checksum = zlib.crc32(artifact[:-4]) & 0xFFFFFFFF
    embedded_checksum = struct.unpack_from("<I", artifact, len(artifact) - 4)[0]
    if embedded_checksum != expected_checksum:
        raise RuntimeError("runtime weights checksum mismatch")
    manifest_checksum = require_string(manifest.get("weights_checksum"), "manifest.weights_checksum")
    if manifest_checksum != f"0x{expected_checksum:08x}":
        raise RuntimeError("manifest weights_checksum does not match runtime payload")

    patterns = manifest.get("patterns")
    if not isinstance(patterns, list) or len(patterns) != pattern_count:
        raise RuntimeError("manifest pattern layout does not match runtime header")
    catalog_patterns = contract.get("patterns")
    if not isinstance(catalog_patterns, list) or len(catalog_patterns) != pattern_count:
        raise RuntimeError("catalog pattern layout does not match runtime header")
    pattern_specs: list[tuple[str, int, int]] = []
    stride = 1
    for index, (item, catalog_item) in enumerate(
        zip(patterns, catalog_patterns, strict=True)
    ):
        if not isinstance(item, dict) or not isinstance(catalog_item, dict):
            raise RuntimeError(f"manifest/catalog patterns[{index}] must be objects")
        pattern_id = require_string(item.get("pattern_id"), f"manifest.patterns[{index}].pattern_id")
        length = require_int(item.get("length"), f"manifest.patterns[{index}].length")
        if length <= 0 or length > 12:
            raise RuntimeError(f"manifest.patterns[{index}].length is unsupported")
        if (
            pattern_id
            != require_string(
                catalog_item.get("pattern_id"),
                f"catalog.patterns[{index}].pattern_id",
            )
            or length
            != require_int(
                catalog_item.get("length"), f"catalog.patterns[{index}].length"
            )
        ):
            raise RuntimeError(
                f"manifest and catalog pattern layout differ at table {index}"
            )
        table_size = 3**length
        pattern_specs.append((pattern_id, stride, table_size))
        stride += table_size
    if stride * phase_count != weight_count:
        raise RuntimeError("manifest pattern sizes do not match runtime weight count")

    raw_weights = struct.unpack_from(f"<{weight_count}i", artifact, weights_offset)
    phase_bias: dict[str, float] = {}
    pattern_weights: list[dict[str, int | float | str]] = []
    for phase in range(phase_count):
        base = phase * stride
        phase_bias[str(phase)] = raw_weights[base] / score_scale
        for pattern_id, offset, table_size in pattern_specs:
            for ternary_index in range(table_size):
                value = raw_weights[base + offset + ternary_index]
                if value != 0:
                    pattern_weights.append(
                        {
                            "phase": phase,
                            "pattern_id": pattern_id,
                            "ternary_index": ternary_index,
                            "weight": value / score_scale,
                        }
                    )

    weights_json = {
        "weights_schema_version": "pattern-eval-weights-v2",
        "phase_bias": phase_bias,
        "pattern_weights": pattern_weights,
        "pattern_set_id": pattern_set_id,
        "pattern_contract_digest": require_string(
            contract.get("pattern_contract_digest"), "catalog.pattern_contract_digest"
        ),
        "index_mode": "raw",
        "phase_count": phase_count,
        "phase_mapping_id": PHASE_MAPPING_ID,
        "score_unit": "disc-diff",
    }
    report = {
        "schema_version": 1,
        "source_artifact_id": manifest.get("artifact_id"),
        "pattern_set_id": pattern_set_id,
        "source_score_scale": score_scale,
        "source_runtime_checksum": f"0x{expected_checksum:08x}",
        "source_trained_phases": manifest.get("trained_phases"),
        "phase_count": phase_count,
        "pattern_count": pattern_count,
        "phase_stride": stride,
        "nonzero_pattern_weight_count": len(pattern_weights),
        "weights_json_schema": "pattern-eval-weights-v2",
        "notes": [
            "deterministic runtime-artifact to trainer-weights import",
            "weights are dequantized by source score_scale",
            "local workflow intermediate; not an artifact promotion",
        ],
    }
    return weights_json, report


def stable_json(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def main() -> int:
    args = parse_args()
    try:
        manifest = load_object(args.manifest)
        source_pattern_set_id = require_string(
            manifest.get("pattern_set_id"), "manifest.pattern_set_id"
        )
        contract = load_contract(args.catalog_dump_exe, source_pattern_set_id)
        weights_path = resolved_weights_path(args.manifest, manifest, args.weights)
        artifact = weights_path.read_bytes()
        weights_json, report = parse_artifact(manifest, artifact, contract)
        weights_text = stable_json(weights_json)
        report["source_manifest"] = args.manifest.name
        report["source_weights"] = weights_path.name
        report["source_weights_sha256"] = f"sha256:{hashlib.sha256(artifact).hexdigest()}"
        report["weights_json_sha256"] = (
            f"sha256:{hashlib.sha256(weights_text.encode('utf-8')).hexdigest()}"
        )
        args.weights_json_out.parent.mkdir(parents=True, exist_ok=True)
        args.report_out.parent.mkdir(parents=True, exist_ok=True)
        args.weights_json_out.write_text(weights_text, encoding="utf-8")
        args.report_out.write_text(stable_json(report), encoding="utf-8")
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"weights_json={args.weights_json_out}")
    print(f"report={args.report_out}")
    print(f"nonzero_pattern_weights={len(weights_json['pattern_weights'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
