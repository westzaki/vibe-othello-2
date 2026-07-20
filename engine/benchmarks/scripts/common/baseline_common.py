#!/usr/bin/env python3
"""Shared helpers for benchmark baseline tooling."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as input_file:
            data = json.load(input_file)
    except json.JSONDecodeError as error:
        raise SystemExit(f"{path}: invalid JSON: {error}") from error

    if not isinstance(data, dict):
        raise SystemExit(f"{path}: expected a JSON object")
    return data


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as input_file:
        for line_number, line in enumerate(input_file, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                record = json.loads(stripped)
            except json.JSONDecodeError as error:
                raise SystemExit(f"{path}:{line_number}: invalid JSON: {error}") from error
            if not isinstance(record, dict):
                raise SystemExit(f"{path}:{line_number}: expected a JSON object")
            records.append(record)
    return records


def write_pretty_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as output_file:
        json.dump(data, output_file, ensure_ascii=False, indent=2)
        output_file.write("\n")


def require_string(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, str) or not value:
        errors.append(f"{label}.{field}: expected non-empty string")


def require_int(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, int):
        errors.append(f"{label}.{field}: expected integer")


def require_number(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, int | float):
        errors.append(f"{label}.{field}: expected number")


def require_bool(record: dict[str, Any], field: str, label: str, errors: list[str]) -> None:
    value = record.get(field)
    if not isinstance(value, bool):
        errors.append(f"{label}.{field}: expected boolean")


def check_common_envelope(
    data: dict[str, Any], *, required_fields: tuple[str, ...], benchmark: str | None = None
) -> list[str]:
    errors: list[str] = []

    for field in required_fields:
        if field not in data:
            errors.append(f"{field}: missing required field")

    if data.get("schema_version") != 1:
        errors.append("schema_version: expected 1")
    if benchmark is not None and data.get("benchmark") != benchmark:
        errors.append(f"benchmark: expected {benchmark!r}")

    return errors
