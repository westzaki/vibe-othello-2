#!/usr/bin/env python3
"""Replace selected roots in a search move-teacher corpus with a deeper overlay."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path


class OverlayError(RuntimeError):
    """Raised when teacher corpora cannot be overlaid without ambiguity."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-move-teacher", required=True, type=Path)
    parser.add_argument("--base-children", required=True, type=Path)
    parser.add_argument("--overlay-move-teacher", required=True, type=Path)
    parser.add_argument("--overlay-children", required=True, type=Path)
    parser.add_argument("--move-teacher-out", required=True, type=Path)
    parser.add_argument("--retained-base-move-teacher-out", type=Path)
    parser.add_argument("--children-out", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    args = parser.parse_args()
    outputs = {
        args.move_teacher_out.resolve(),
        args.children_out.resolve(),
        args.report_out.resolve(),
    }
    if args.retained_base_move_teacher_out is not None:
        outputs.add(args.retained_base_move_teacher_out.resolve())
    expected_output_count = 4 if args.retained_base_move_teacher_out is not None else 3
    if len(outputs) != expected_output_count:
        parser.error("output paths must be distinct")
    inputs = {
        args.base_move_teacher.resolve(),
        args.base_children.resolve(),
        args.overlay_move_teacher.resolve(),
        args.overlay_children.resolve(),
    }
    if outputs & inputs:
        parser.error("outputs must not overwrite inputs")
    return args


def read_tsv(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        if reader.fieldnames is None:
            raise OverlayError(f"missing TSV header: {path}")
        rows = list(reader)
    if any(None in row for row in rows):
        raise OverlayError(f"malformed TSV row: {path}")
    return reader.fieldnames, rows


def write_tsv(path: Path, header: list[str], rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", fieldnames=header, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def validate_headers(
    base_move_header: list[str],
    overlay_move_header: list[str],
    base_child_header: list[str],
    overlay_child_header: list[str],
) -> None:
    if base_move_header != overlay_move_header:
        raise OverlayError("base and overlay move-teacher headers differ")
    if base_child_header != overlay_child_header:
        raise OverlayError("base and overlay normalized-child headers differ")
    for field in ("root_board_id", "child_board_id", "move", "root_split", "teacher_depth"):
        if field not in base_move_header:
            raise OverlayError(f"move-teacher header is missing {field}")
    for field in ("record_id", "board_id", "board_a1_to_h8", "split", "phase"):
        if field not in base_child_header:
            raise OverlayError(f"normalized-child header is missing {field}")


def unique_moves(rows: list[dict[str, str]], label: str) -> None:
    seen: set[tuple[str, str]] = set()
    for row in rows:
        key = (row["root_board_id"], row["move"])
        if key in seen:
            raise OverlayError(f"{label} contains duplicate root/move row: {key}")
        seen.add(key)


def roots_by_id(
    rows: list[dict[str, str]], label: str
) -> dict[str, list[dict[str, str]]]:
    roots: dict[str, list[dict[str, str]]] = {}
    for row in rows:
        root_id = row["root_board_id"]
        if not root_id or not row["move"]:
            raise OverlayError(f"{label} contains an empty root_board_id or move")
        roots.setdefault(root_id, []).append(row)
    for root_id, root_rows in roots.items():
        first = root_rows[0]
        for field in ("root_record_id", "root_split", "root_phase"):
            if any(row[field] != first[field] for row in root_rows[1:]):
                raise OverlayError(
                    f"{label} root metadata differs within {root_id}: {field}"
                )
    return roots


def child_map(rows: list[dict[str, str]], label: str) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    for row in rows:
        record_id = row["record_id"]
        if row["board_id"] != record_id:
            raise OverlayError(f"{label} child record_id and board_id differ: {record_id}")
        prior = result.get(record_id)
        if prior is not None and prior != row:
            raise OverlayError(f"{label} contains conflicting child rows: {record_id}")
        result[record_id] = row
    return result


def provenance(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    fields = (
        "teacher_kind",
        "teacher_source",
        "teacher_artifact_id",
        "teacher_artifact_checksum",
        "teacher_search_config_id",
    )
    return [
        dict(zip(fields, values, strict=True))
        for values in sorted({tuple(row[field] for field in fields) for row in rows})
    ]


def main() -> int:
    args = parse_args()
    try:
        base_move_header, base_moves = read_tsv(args.base_move_teacher)
        overlay_move_header, overlay_moves = read_tsv(args.overlay_move_teacher)
        base_child_header, base_children = read_tsv(args.base_children)
        overlay_child_header, overlay_children = read_tsv(args.overlay_children)
        validate_headers(
            base_move_header, overlay_move_header, base_child_header, overlay_child_header
        )
        if not overlay_moves:
            raise OverlayError("overlay move-teacher has no rows")
        unique_moves(base_moves, "base")
        unique_moves(overlay_moves, "overlay")

        base_by_root = roots_by_id(base_moves, "base")
        overlay_by_root = roots_by_id(overlay_moves, "overlay")
        overlay_roots = set(overlay_by_root)
        unknown_overlay_roots = sorted(overlay_roots - set(base_by_root))
        if unknown_overlay_roots:
            raise OverlayError(
                "overlay roots are absent from the base corpus: "
                + ", ".join(unknown_overlay_roots[:3])
            )
        for root_id, overlay_rows in overlay_by_root.items():
            base_rows = base_by_root[root_id]
            for field in ("root_record_id", "root_split", "root_phase"):
                if overlay_rows[0][field] != base_rows[0][field]:
                    raise OverlayError(
                        f"base/overlay root metadata differs for {root_id}: {field}"
                    )
            if {row["move"] for row in overlay_rows} != {
                row["move"] for row in base_rows
            }:
                raise OverlayError(
                    f"overlay does not contain the complete base move set for {root_id}"
                )
        candidate_base_moves = [
            row for row in base_moves if row["root_board_id"] not in overlay_roots
        ]
        overlay_label_by_child: dict[str, str] = {}
        for row in overlay_moves:
            prior = overlay_label_by_child.setdefault(
                row["child_board_id"], row["child_label_score_side_to_move"]
            )
            if prior != row["child_label_score_side_to_move"]:
                raise OverlayError(
                    f"overlay has conflicting labels for child {row['child_board_id']}"
                )
        conflicting_base_roots = {
            row["root_board_id"]
            for row in candidate_base_moves
            if row["child_board_id"] in overlay_label_by_child
            and row["child_label_score_side_to_move"]
            != overlay_label_by_child[row["child_board_id"]]
        }
        retained_base_moves = [
            row
            for row in candidate_base_moves
            if row["root_board_id"] not in conflicting_base_roots
        ]
        merged_moves = [*retained_base_moves, *overlay_moves]
        unique_moves(merged_moves, "merged")
        if len({row["root_board_id"] for row in overlay_moves}) != len(
            {
                (row["root_board_id"], row["root_split"])
                for row in overlay_moves
            }
        ):
            raise OverlayError("overlay roots cross split assignments")

        base_by_child = child_map(base_children, "base")
        overlay_by_child = child_map(overlay_children, "overlay")
        structural_fields = ("board_id", "board_a1_to_h8", "split", "phase")
        for board_id in set(base_by_child) & set(overlay_by_child):
            if any(
                base_by_child[board_id][field] != overlay_by_child[board_id][field]
                for field in structural_fields
            ):
                raise OverlayError(f"base/overlay child structure conflicts: {board_id}")
        merged_by_child = {**base_by_child, **overlay_by_child}
        required_children = {row["child_board_id"] for row in merged_moves}
        missing_children = sorted(required_children - set(merged_by_child))
        if missing_children:
            raise OverlayError(
                f"merged move-teacher is missing {len(missing_children)} child rows: "
                + ", ".join(missing_children[:3])
            )
        merged_children = [merged_by_child[board_id] for board_id in sorted(required_children)]
        split_by_board: dict[str, str] = {}
        for row in merged_children:
            prior = split_by_board.setdefault(row["board_id"], row["split"])
            if prior != row["split"]:
                raise OverlayError(f"merged child board crosses splits: {row['board_id']}")

        write_tsv(args.move_teacher_out, base_move_header, merged_moves)
        if args.retained_base_move_teacher_out is not None:
            write_tsv(
                args.retained_base_move_teacher_out, base_move_header, retained_base_moves
            )
        write_tsv(args.children_out, base_child_header, merged_children)
        split_by_root = {
            row["root_board_id"]: row["root_split"] for row in merged_moves
        }
        report = {
            "schema_version": 1,
            "merge_policy": "replace-complete-roots-overlay-children-win-v1",
            "inputs": {
                "base_move_teacher": {
                    "path": args.base_move_teacher.name,
                    "checksum": sha256_file(args.base_move_teacher),
                },
                "base_children": {
                    "path": args.base_children.name,
                    "checksum": sha256_file(args.base_children),
                },
                "overlay_move_teacher": {
                    "path": args.overlay_move_teacher.name,
                    "checksum": sha256_file(args.overlay_move_teacher),
                },
                "overlay_children": {
                    "path": args.overlay_children.name,
                    "checksum": sha256_file(args.overlay_children),
                },
            },
            "counts": {
                "base_roots": len({row["root_board_id"] for row in base_moves}),
                "overlay_roots": len(overlay_roots),
                "base_roots_excluded_for_cross_depth_child_label_conflict": len(
                    conflicting_base_roots
                ),
                "retained_base_roots": len(
                    {row["root_board_id"] for row in retained_base_moves}
                ),
                "merged_roots": len({row["root_board_id"] for row in merged_moves}),
                "base_move_rows": len(base_moves),
                "overlay_move_rows": len(overlay_moves),
                "merged_move_rows": len(merged_moves),
                "base_child_rows": len(base_by_child),
                "overlay_child_rows": len(overlay_by_child),
                "overlapping_child_rows": len(set(base_by_child) & set(overlay_by_child)),
                "merged_child_rows": len(merged_children),
            },
            "merged_roots_by_split": dict(
                sorted(Counter(split_by_root.values()).items())
            ),
            "teacher_provenance": provenance(merged_moves),
            "output": {
                "move_teacher": {
                    "path": args.move_teacher_out.name,
                    "checksum": sha256_file(args.move_teacher_out),
                },
                "retained_base_move_teacher": (
                    None
                    if args.retained_base_move_teacher_out is None
                    else {
                        "path": args.retained_base_move_teacher_out.name,
                        "checksum": sha256_file(args.retained_base_move_teacher_out),
                    }
                ),
                "children": {
                    "path": args.children_out.name,
                    "checksum": sha256_file(args.children_out),
                },
            },
            "non_claim_notes": [
                "local-only teacher overlay",
                "overlay replaces complete roots; it is never appended as duplicate supervision",
                "base roots sharing a differently labeled overlay child are excluded completely",
                "generated teacher data and reports must not be committed",
            ],
        }
        args.report_out.parent.mkdir(parents=True, exist_ok=True)
        args.report_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (OSError, OverlayError, KeyError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"merged_roots={report['counts']['merged_roots']}")
    print(f"move_teacher={args.move_teacher_out}")
    if args.retained_base_move_teacher_out is not None:
        print(f"retained_base_move_teacher={args.retained_base_move_teacher_out}")
    print(f"children={args.children_out}")
    print(f"report={args.report_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
