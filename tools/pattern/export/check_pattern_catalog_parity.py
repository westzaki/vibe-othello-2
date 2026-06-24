#!/usr/bin/env python3
"""Check runtime-owned pattern catalog parity with exporter metadata."""

from __future__ import annotations

import argparse
import copy
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

from pattern_sets import PATTERN_SETS


SCHEMA_VERSION = 1
FNV1A64_OFFSET = 14695981039346656037
FNV1A64_PRIME = 1099511628211
FNV1A64_MASK = (1 << 64) - 1


def fail(message: str) -> None:
    raise RuntimeError(message)


def checked_pattern_size(length: int) -> int:
    return 3**length


def run_dump(dump_exe: Path) -> dict[str, Any]:
    result = subprocess.run(
        [str(dump_exe)],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        fail(f"pattern catalog dump failed with exit code {result.returncode}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        sys.stderr.write(result.stdout)
        fail(f"pattern catalog dump did not emit JSON: {error}")
    if not isinstance(payload, dict):
        fail("pattern catalog dump JSON root must be an object")
    return payload


def require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{field} must be a non-empty string")
    return value


def require_int(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        fail(f"{field} must be an integer")
    return value


def require_list(value: Any, field: str) -> list[Any]:
    if not isinstance(value, list):
        fail(f"{field} must be an array")
    return value


def require_string_list(value: Any, field: str) -> list[str]:
    items = require_list(value, field)
    result: list[str] = []
    for index, item in enumerate(items):
        result.append(require_string(item, f"{field}[{index}]"))
    return result


def join_strings(values: list[str]) -> str:
    return ",".join(values)


def append_digest_line(lines: list[str], line: str) -> None:
    lines.append(line)


def digest_input(contract: dict[str, Any]) -> str:
    lines: list[str] = []
    append_digest_line(lines, "pattern-contract-digest-v1")
    append_digest_line(lines, f"pattern_set_id={require_string(contract.get('pattern_set_id'), 'pattern_set_id')}")
    append_digest_line(lines, f"index_mode={require_string(contract.get('index_mode'), 'index_mode')}")
    ordered_pattern_ids = require_string_list(
        contract.get("ordered_pattern_ids"), "ordered_pattern_ids"
    )
    append_digest_line(lines, f"ordered_pattern_ids={join_strings(ordered_pattern_ids)}")
    patterns = require_list(contract.get("patterns"), "patterns")
    append_digest_line(lines, f"pattern_count={len(patterns)}")

    for pattern_index, pattern_any in enumerate(patterns):
        if not isinstance(pattern_any, dict):
            fail(f"patterns[{pattern_index}] must be an object")
        pattern = pattern_any
        prefix = f"pattern[{pattern_index}]."
        pattern_id = require_string(pattern.get("pattern_id"), f"{prefix}id")
        length = require_int(pattern.get("length"), f"{prefix}length")
        symmetry_policy = require_string(
            pattern.get("symmetry_policy"), f"{prefix}symmetry_policy"
        )
        squares = require_string_list(pattern.get("squares"), f"{prefix}squares")
        feature_instances = require_list(
            pattern.get("feature_instances"), f"{prefix}feature_instances"
        )
        table_size = require_int(pattern.get("table_size"), f"{prefix}table_size")

        append_digest_line(lines, f"{prefix}id={pattern_id}")
        append_digest_line(lines, f"{prefix}length={length}")
        append_digest_line(lines, f"{prefix}symmetry_policy={symmetry_policy}")
        append_digest_line(lines, f"{prefix}squares={join_strings(squares)}")
        append_digest_line(lines, f"{prefix}feature_instance_count={len(feature_instances)}")
        for instance_index, instance_any in enumerate(feature_instances):
            if not isinstance(instance_any, dict):
                fail(f"{prefix}feature_instance[{instance_index}] must be an object")
            instance_squares = require_string_list(
                instance_any.get("squares"),
                f"{prefix}feature_instance[{instance_index}].squares",
            )
            append_digest_line(
                lines,
                f"{prefix}feature_instance[{instance_index}].squares="
                f"{join_strings(instance_squares)}",
            )
        append_digest_line(lines, f"{prefix}table_size={table_size}")
    return "\n".join(lines) + "\n"


def fnv1a64_digest(text: str) -> str:
    value = FNV1A64_OFFSET
    for byte in text.encode("utf-8"):
        value ^= byte
        value = (value * FNV1A64_PRIME) & FNV1A64_MASK
    return f"fnv1a64:{value:016x}"


def digest_for_contract(contract: dict[str, Any]) -> str:
    return fnv1a64_digest(digest_input(contract))


def contracts_by_id(payload: dict[str, Any]) -> dict[str, dict[str, Any]]:
    if payload.get("schema_version") != SCHEMA_VERSION:
        fail(f"schema_version must be {SCHEMA_VERSION}")
    contracts = require_list(payload.get("pattern_sets"), "pattern_sets")
    by_id: dict[str, dict[str, Any]] = {}
    for index, contract_any in enumerate(contracts):
        if not isinstance(contract_any, dict):
            fail(f"pattern_sets[{index}] must be an object")
        pattern_set_id = require_string(
            contract_any.get("pattern_set_id"), f"pattern_sets[{index}].pattern_set_id"
        )
        if pattern_set_id in by_id:
            fail(f"duplicate pattern_set_id in dump: {pattern_set_id}")
        by_id[pattern_set_id] = contract_any
    return by_id


def check_dump_stability(first: dict[str, Any], second: dict[str, Any]) -> None:
    first_contracts = contracts_by_id(first)
    second_contracts = contracts_by_id(second)
    if set(first_contracts) != set(second_contracts):
        fail("pattern catalog dump changed pattern set ids between runs")
    for pattern_set_id, first_contract in first_contracts.items():
        second_contract = second_contracts[pattern_set_id]
        first_digest = require_string(
            first_contract.get("pattern_contract_digest"),
            f"{pattern_set_id}.pattern_contract_digest",
        )
        second_digest = require_string(
            second_contract.get("pattern_contract_digest"),
            f"{pattern_set_id}.pattern_contract_digest second run",
        )
        if first_digest != second_digest:
            fail(f"{pattern_set_id}: pattern_contract_digest changed between identical runs")


def validate_digest(contract: dict[str, Any]) -> None:
    pattern_set_id = require_string(contract.get("pattern_set_id"), "pattern_set_id")
    digest = require_string(
        contract.get("pattern_contract_digest"), f"{pattern_set_id}.pattern_contract_digest"
    )
    if not digest.startswith("fnv1a64:") or len(digest) != len("fnv1a64:") + 16:
        fail(f"{pattern_set_id}: pattern_contract_digest is not a readable fnv1a64 digest")
    recomputed = digest_for_contract(contract)
    if recomputed != digest:
        fail(f"{pattern_set_id}: pattern_contract_digest does not match semantic payload")


def check_pattern_order_in_digest(contract: dict[str, Any]) -> None:
    pattern_set_id = require_string(contract.get("pattern_set_id"), "pattern_set_id")
    if pattern_set_id != "fixed-pattern-fixture-v1":
        return

    ordered_ids = require_string_list(contract.get("ordered_pattern_ids"), "ordered_pattern_ids")
    if ordered_ids != ["edge-8", "corner-3x3"]:
        fail("fixed-pattern-fixture-v1 fixture order changed unexpectedly")

    original_digest = require_string(
        contract.get("pattern_contract_digest"), "fixed pattern_contract_digest"
    )
    swapped = copy.deepcopy(contract)
    swapped["ordered_pattern_ids"] = list(reversed(ordered_ids))
    swapped["patterns"] = list(reversed(require_list(swapped.get("patterns"), "patterns")))
    if digest_for_contract(swapped) == original_digest:
        fail("fixed-pattern-fixture-v1 digest does not include pattern order")


def check_feature_geometry_shape(contract: dict[str, Any]) -> None:
    pattern_set_id = require_string(contract.get("pattern_set_id"), "pattern_set_id")
    patterns = require_list(contract.get("patterns"), f"{pattern_set_id}.patterns")
    total_instances = 0
    for pattern_index, pattern_any in enumerate(patterns):
        if not isinstance(pattern_any, dict):
            fail(f"{pattern_set_id}.patterns[{pattern_index}] must be an object")
        pattern = pattern_any
        length = require_int(pattern.get("length"), f"{pattern_set_id}.patterns[{pattern_index}].length")
        squares = require_string_list(
            pattern.get("squares"), f"{pattern_set_id}.patterns[{pattern_index}].squares"
        )
        if len(squares) != length:
            fail(f"{pattern_set_id}: pattern definition square count does not match length")
        feature_instances = require_list(
            pattern.get("feature_instances"),
            f"{pattern_set_id}.patterns[{pattern_index}].feature_instances",
        )
        feature_instance_count = require_int(
            pattern.get("feature_instance_count"),
            f"{pattern_set_id}.patterns[{pattern_index}].feature_instance_count",
        )
        if feature_instance_count != len(feature_instances):
            fail(f"{pattern_set_id}: feature_instance_count does not match instances")
        if feature_instance_count <= 0:
            fail(f"{pattern_set_id}: pattern has no feature instances")
        for instance_index, instance_any in enumerate(feature_instances):
            if not isinstance(instance_any, dict):
                fail(f"{pattern_set_id}: feature instance must be an object")
            instance = require_int(
                instance_any.get("instance"),
                f"{pattern_set_id}.patterns[{pattern_index}].feature_instances[{instance_index}].instance",
            )
            if instance != instance_index:
                fail(f"{pattern_set_id}: feature instance order marker mismatch")
            instance_squares = require_string_list(
                instance_any.get("squares"),
                f"{pattern_set_id}.patterns[{pattern_index}].feature_instances[{instance_index}].squares",
            )
            if len(instance_squares) != length:
                fail(f"{pattern_set_id}: feature instance square count does not match length")
        total_instances += feature_instance_count

    if require_int(contract.get("feature_instance_count"), f"{pattern_set_id}.feature_instance_count") != total_instances:
        fail(f"{pattern_set_id}: total feature_instance_count mismatch")


def check_exporter_parity(contract: dict[str, Any]) -> None:
    pattern_set_id = require_string(contract.get("pattern_set_id"), "pattern_set_id")
    if require_string(contract.get("index_mode"), f"{pattern_set_id}.index_mode") != "raw":
        fail(f"{pattern_set_id}: exporter parity only covers raw index mode")

    try:
        spec = PATTERN_SETS[pattern_set_id]
    except KeyError:
        fail(f"{pattern_set_id}: dump emitted a pattern set missing from exporter catalog")
    if spec.pattern_set_id != pattern_set_id:
        fail(f"{pattern_set_id}: exporter catalog uses a non-canonical pattern_set_id")

    expected_ids = [pattern_id for pattern_id, _ in spec.patterns]
    expected_lengths = [length for _, length in spec.patterns]
    expected_table_entries = sum(checked_pattern_size(length) for length in expected_lengths)

    ordered_ids = require_string_list(
        contract.get("ordered_pattern_ids"), f"{pattern_set_id}.ordered_pattern_ids"
    )
    if ordered_ids != expected_ids:
        fail(f"{pattern_set_id}: ordered pattern ids drifted from exporter catalog")

    patterns = require_list(contract.get("patterns"), f"{pattern_set_id}.patterns")
    actual_lengths: list[int] = []
    actual_table_entries = 0
    for index, pattern_any in enumerate(patterns):
        if not isinstance(pattern_any, dict):
            fail(f"{pattern_set_id}.patterns[{index}] must be an object")
        pattern_id = require_string(pattern_any.get("pattern_id"), f"{pattern_set_id}.patterns[{index}].pattern_id")
        length = require_int(pattern_any.get("length"), f"{pattern_set_id}.patterns[{index}].length")
        table_size = require_int(
            pattern_any.get("table_size"), f"{pattern_set_id}.patterns[{index}].table_size"
        )
        if pattern_id != expected_ids[index]:
            fail(f"{pattern_set_id}: pattern table order drifted at index {index}")
        if table_size != checked_pattern_size(length):
            fail(f"{pattern_set_id}: table_size does not equal 3 ** length")
        actual_lengths.append(length)
        actual_table_entries += table_size

    if actual_lengths != expected_lengths:
        fail(f"{pattern_set_id}: pattern lengths drifted from exporter catalog")
    if actual_table_entries != expected_table_entries:
        fail(f"{pattern_set_id}: per-pattern table entries drifted from exporter catalog")
    if (
        require_int(contract.get("total_table_entries"), f"{pattern_set_id}.total_table_entries")
        != expected_table_entries
    ):
        fail(f"{pattern_set_id}: total table entries drifted from exporter catalog")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump-exe", required=True, type=Path)
    args = parser.parse_args()

    try:
        first = run_dump(args.dump_exe)
        second = run_dump(args.dump_exe)
        check_dump_stability(first, second)
        by_id = contracts_by_id(first)

        missing = sorted(set(PATTERN_SETS) - set(by_id))
        if missing:
            fail(f"runtime dump is missing exporter pattern sets: {', '.join(missing)}")

        for pattern_set_id in sorted(PATTERN_SETS):
            contract = by_id[pattern_set_id]
            validate_digest(contract)
            check_pattern_order_in_digest(contract)
            check_feature_geometry_shape(contract)
            check_exporter_parity(contract)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
