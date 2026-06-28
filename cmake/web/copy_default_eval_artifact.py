#!/usr/bin/env python3
"""Copy the committed default evaluation artifact into Web public assets."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_POINTER_NAME = "default-artifact.json"
DEFAULT_SOURCE_DIR = Path("data/eval")
DEFAULT_DESTINATION_DIR = Path("apps/web/public/eval")
OPTIONAL_ARTIFACT_FILES = ("README.md", "NOTICE.md")


@dataclass(frozen=True)
class CopyItem:
    source: Path
    destination_relative: Path


def load_json(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: invalid JSON: {exc}") from exc
    except OSError as exc:
        raise ValueError(f"{path}: cannot read file: {exc}") from exc
    if not isinstance(payload, dict):
        raise ValueError(f"{path}: root must be an object")
    return payload


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise ValueError(f"{label} is missing: {path}")


def require_relative_file_path(payload: dict[str, Any], field: str, owner: Path) -> Path:
    value = payload.get(field)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{owner}: {field} must be a non-empty relative path")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"{owner}: {field} must stay within its source directory: {value}")
    return path


def resolve_under(base: Path, relative: Path, field: str, owner: Path) -> Path:
    base_resolved = base.resolve()
    resolved = (base / relative).resolve()
    try:
        resolved.relative_to(base_resolved)
    except ValueError as exc:
        raise ValueError(f"{owner}: {field} escapes {base}") from exc
    return resolved


def build_copy_plan(source_dir: Path) -> list[CopyItem]:
    default_pointer = source_dir / DEFAULT_POINTER_NAME
    require_file(default_pointer, "default artifact pointer")
    default_payload = load_json(default_pointer)

    artifact_manifest_rel = require_relative_file_path(
        default_payload, "artifact_manifest", default_pointer
    )
    artifact_manifest = resolve_under(
        source_dir, artifact_manifest_rel, "artifact_manifest", default_pointer
    )
    require_file(artifact_manifest, "default artifact manifest")
    manifest_payload = load_json(artifact_manifest)

    weights_file_rel = require_relative_file_path(
        manifest_payload, "weights_file", artifact_manifest
    )
    weights_file = resolve_under(
        artifact_manifest.parent, weights_file_rel, "weights_file", artifact_manifest
    )
    require_file(weights_file, "default artifact weights")

    artifact_dir_rel = artifact_manifest_rel.parent
    plan = [
        CopyItem(default_pointer, Path(DEFAULT_POINTER_NAME)),
        CopyItem(artifact_manifest, artifact_manifest_rel),
        CopyItem(weights_file, artifact_dir_rel / weights_file_rel),
    ]

    provenance_value = default_payload.get("artifact_provenance")
    if provenance_value is not None:
        provenance_rel = require_relative_file_path(
            default_payload, "artifact_provenance", default_pointer
        )
        provenance = resolve_under(
            source_dir, provenance_rel, "artifact_provenance", default_pointer
        )
        require_file(provenance, "default artifact provenance")
        plan.append(CopyItem(provenance, provenance_rel))
    else:
        provenance = artifact_manifest.parent / "provenance.json"
        if provenance.is_file():
            plan.append(CopyItem(provenance, artifact_dir_rel / "provenance.json"))

    for name in OPTIONAL_ARTIFACT_FILES:
        optional_source = artifact_manifest.parent / name
        if optional_source.is_file():
            plan.append(CopyItem(optional_source, artifact_dir_rel / name))

    return plan


def clean_destination(destination_dir: Path) -> None:
    if destination_dir.exists() and not destination_dir.is_dir():
        raise ValueError(f"destination is not a directory: {destination_dir}")
    destination_dir.mkdir(parents=True, exist_ok=True)

    for relative in (Path(DEFAULT_POINTER_NAME), Path("artifacts")):
        path = destination_dir / relative
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()


def copy_items(plan: list[CopyItem], destination_dir: Path) -> None:
    clean_destination(destination_dir)
    for item in plan:
        target = destination_dir / item.destination_relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item.source, target)


def check_items(plan: list[CopyItem], destination_dir: Path) -> None:
    errors = []
    for item in plan:
        target = destination_dir / item.destination_relative
        if not target.is_file():
            errors.append(f"copied artifact asset is missing: {target}")
    if errors:
        raise ValueError("\n".join(errors))


def resolve_cli_path(path: Path, repo_root: Path) -> Path:
    if path.is_absolute():
        return path.resolve()
    return (repo_root / path).resolve()


def parse_args() -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description="Copy the committed default evaluation artifact for the Web app."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=repo_root,
        help="Repository root used to resolve relative paths.",
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=DEFAULT_SOURCE_DIR,
        help="Evaluation artifact source directory.",
    )
    parser.add_argument(
        "--destination-dir",
        type=Path,
        default=DEFAULT_DESTINATION_DIR,
        help="Web public eval asset destination directory.",
    )
    parser.add_argument(
        "--check-only",
        action="store_true",
        help="Only verify that the expected copied files exist.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    source_dir = resolve_cli_path(args.source_dir, repo_root)
    destination_dir = resolve_cli_path(args.destination_dir, repo_root)

    try:
        plan = build_copy_plan(source_dir)
        if args.check_only:
            check_items(plan, destination_dir)
            print(f"Verified {len(plan)} Web evaluation artifact asset(s) in {destination_dir}")
        else:
            copy_items(plan, destination_dir)
            print(f"Copied {len(plan)} Web evaluation artifact asset(s) to {destination_dir}")
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
