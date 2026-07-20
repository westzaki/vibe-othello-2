#!/usr/bin/env python3
"""Minimal dataset manifest validator for checked-in policy smoke tests."""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import date
from pathlib import Path
from typing import Any


REQUIRED_FIELDS = (
    "dataset_id",
    "source_name",
    "source_url",
    "retrieved_at",
    "license_or_terms",
    "redistribution_allowed",
    "commercial_use_allowed",
    "derived_weights_allowed",
    "required_attribution",
    "local_path",
    "sha256",
    "notes",
)

OPTIONAL_FIELDS = (
    "label_kind",
    "label_generation_by_occupied_range",
)

PERMISSION_FIELDS = (
    "redistribution_allowed",
    "commercial_use_allowed",
    "derived_weights_allowed",
)

DATASET_ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,127}$")
SHA256_RE = re.compile(r"^[a-f0-9]{64}$")
UNKNOWN_TERMS = {"unknown", "unclear", "not-reviewed", "unreviewed"}


def load_json(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: invalid JSON: {exc}") from exc
    except OSError as exc:
        raise ValueError(f"{path}: cannot read file: {exc}") from exc


def require_string(
    path: Path, manifest: dict[str, Any], field: str, errors: list[str]
) -> None:
    value = manifest.get(field)
    if not isinstance(value, str) or not value.strip():
        errors.append(f"{path}: {field} must be a non-empty string")


def validate_schema_shape(schema_path: Path) -> list[str]:
    errors: list[str] = []
    schema = load_json(schema_path)
    if not isinstance(schema, dict):
        return [f"{schema_path}: schema root must be an object"]

    required = schema.get("required")
    if not isinstance(required, list):
        errors.append(f"{schema_path}: schema required must be a list")
    else:
        missing = sorted(set(REQUIRED_FIELDS).difference(required))
        if missing:
            errors.append(f"{schema_path}: schema missing required fields: {', '.join(missing)}")

    properties = schema.get("properties")
    if not isinstance(properties, dict):
        errors.append(f"{schema_path}: schema properties must be an object")
    else:
        missing = sorted(set(REQUIRED_FIELDS).difference(properties))
        if missing:
            errors.append(f"{schema_path}: schema missing properties: {', '.join(missing)}")

    return errors


def validate_manifest(path: Path) -> list[str]:
    errors: list[str] = []
    manifest = load_json(path)
    if not isinstance(manifest, dict):
        return [f"{path}: manifest root must be an object"]

    extra = sorted(set(manifest).difference(REQUIRED_FIELDS + OPTIONAL_FIELDS))
    if extra:
        errors.append(f"{path}: unexpected fields: {', '.join(extra)}")

    for field in REQUIRED_FIELDS:
        if field not in manifest:
            errors.append(f"{path}: missing required field: {field}")

    for field in (
        "source_name",
        "source_url",
        "license_or_terms",
        "required_attribution",
        "local_path",
        "notes",
    ):
        require_string(path, manifest, field, errors)

    dataset_id = manifest.get("dataset_id")
    if not isinstance(dataset_id, str) or DATASET_ID_RE.fullmatch(dataset_id) is None:
        errors.append(
            f"{path}: dataset_id must match {DATASET_ID_RE.pattern}"
        )

    retrieved_at = manifest.get("retrieved_at")
    if not isinstance(retrieved_at, str):
        errors.append(f"{path}: retrieved_at must be a YYYY-MM-DD string")
    else:
        try:
            date.fromisoformat(retrieved_at)
        except ValueError:
            errors.append(f"{path}: retrieved_at must be a valid YYYY-MM-DD date")

    sha256 = manifest.get("sha256")
    if not isinstance(sha256, str) or SHA256_RE.fullmatch(sha256) is None:
        errors.append(f"{path}: sha256 must be 64 lowercase hex characters")

    for field in PERMISSION_FIELDS:
        value = manifest.get(field)
        if not isinstance(value, bool) and value != "unknown":
            errors.append(f"{path}: {field} must be true, false, or unknown")

    if "label_kind" in manifest:
        require_string(path, manifest, "label_kind", errors)

    if "label_generation_by_occupied_range" in manifest:
        ranges = manifest["label_generation_by_occupied_range"]
        if not isinstance(ranges, list) or not ranges:
            errors.append(
                f"{path}: label_generation_by_occupied_range must be a non-empty list"
            )
        else:
            range_fields = {
                "occupied_count_min",
                "occupied_count_max",
                "position_count",
                "generation_kind",
                "engine",
                "procedure",
            }
            for index, item in enumerate(ranges):
                prefix = f"{path}: label_generation_by_occupied_range[{index}]"
                if not isinstance(item, dict):
                    errors.append(f"{prefix} must be an object")
                    continue
                if set(item) != range_fields:
                    errors.append(f"{prefix} must contain exactly the documented fields")
                    continue
                minimum = item["occupied_count_min"]
                maximum = item["occupied_count_max"]
                count = item["position_count"]
                if (
                    not isinstance(minimum, int)
                    or not isinstance(maximum, int)
                    or minimum < 4
                    or maximum > 64
                    or minimum > maximum
                ):
                    errors.append(f"{prefix} has an invalid occupied-count range")
                if not isinstance(count, int) or isinstance(count, bool) or count <= 0:
                    errors.append(f"{prefix}.position_count must be a positive integer")
                for field in ("generation_kind", "engine", "procedure"):
                    value = item[field]
                    if not isinstance(value, str) or not value.strip():
                        errors.append(f"{prefix}.{field} must be a non-empty string")

    local_path = manifest.get("local_path")
    if isinstance(local_path, str):
        if Path(local_path).is_absolute() or local_path.startswith("~"):
            errors.append(f"{path}: local_path must not be a personal absolute path")
        if ".." in Path(local_path).parts:
            errors.append(f"{path}: local_path must not contain parent traversal")

    terms = manifest.get("license_or_terms")
    if isinstance(terms, str) and terms.strip().lower() in UNKNOWN_TERMS:
        for field in PERMISSION_FIELDS:
            if manifest.get(field) is True:
                errors.append(
                    f"{path}: {field} cannot be true when license_or_terms is unknown"
                )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifests", nargs="+", type=Path)
    parser.add_argument("--schema", type=Path)
    args = parser.parse_args()

    errors: list[str] = []
    try:
        if args.schema is not None:
            errors.extend(validate_schema_shape(args.schema))

        for manifest_path in args.manifests:
            errors.extend(validate_manifest(manifest_path))
    except ValueError as exc:
        print(exc, file=sys.stderr)
        return 1

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    for manifest_path in args.manifests:
        print(f"ok: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
