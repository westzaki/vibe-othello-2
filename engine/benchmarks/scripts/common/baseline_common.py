#!/usr/bin/env python3
"""Shared helpers for benchmark baseline tooling."""

from __future__ import annotations

import json
import subprocess
from datetime import date
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


def _run_text(command: list[str]) -> str:
    return subprocess.check_output(command, text=True).strip()


def git_commit() -> str:
    return _run_text(["git", "rev-parse", "HEAD"])


def git_revision() -> str:
    return _run_text(["git", "rev-parse", "--short", "HEAD"])


def measured_at_today() -> str:
    return date.today().isoformat()


def platform_os_string(uname_s: str, uname_r: str, uname_m: str) -> str:
    return f"{uname_s} {uname_r} {uname_m}"


def _tokenize(value: str) -> str:
    token_chars = [ch.lower() if ch.isalnum() else "-" for ch in value]
    return "-".join(part for part in "".join(token_chars).split("-") if part)


def machine_token_from_uname(uname_s: str, uname_r: str, uname_m: str) -> str:
    if uname_s == "Darwin" and uname_m == "arm64":
        return "apple-silicon-macos-arm64"
    return _tokenize(platform_os_string(uname_s, uname_r, uname_m))


def compiler_summary() -> str:
    return _run_text(["c++", "--version"]).splitlines()[0]


def compiler_token(compiler: str) -> str:
    prefix = "Apple clang version "
    if compiler.startswith(prefix):
        version = compiler[len(prefix) :].split(".", maxsplit=1)[0]
        return f"apple-clang-{version or 'unknown'}"
    return _tokenize(compiler)
