#!/usr/bin/env python3
"""Select phase-balanced roots where a search teacher corrects its baseline evaluator."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import statistics
import tempfile
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


class SelectionError(RuntimeError):
    """Raised when the teacher or normalized input violates the selection contract."""


@dataclass(frozen=True)
class HardRoot:
    board_id: str
    record_id: str
    split: str
    phase: int
    baseline_regret: int
    best_score_margin: int
    tie_break: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--move-teacher-tsv", required=True, type=Path)
    parser.add_argument("--output-tsv", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    parser.add_argument("--max-roots", required=True, type=int)
    parser.add_argument("--minimum-baseline-regret", type=int, default=1)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    if args.max_roots <= 0:
        parser.error("--max-roots must be positive")
    if args.minimum_baseline_regret < 0:
        parser.error("--minimum-baseline-regret must be non-negative")
    if args.seed < 0:
        parser.error("--seed must be non-negative")
    if args.output_tsv.resolve() == args.normalized_tsv.resolve():
        parser.error("--output-tsv must not overwrite --normalized-tsv")
    return args


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def stable_hash(text: str, seed: int) -> str:
    return hashlib.sha256(f"{seed}:{text}".encode()).hexdigest()


def require_fields(fieldnames: list[str] | None, required: set[str], path: Path) -> None:
    if fieldnames is None:
        raise SelectionError(f"missing TSV header: {path}")
    missing = sorted(required - set(fieldnames))
    if missing:
        raise SelectionError(f"missing TSV columns in {path.name}: {', '.join(missing)}")


def parse_int(row: dict[str, str], field: str, path: Path, line_number: int) -> int:
    try:
        return int(row[field])
    except ValueError as error:
        raise SelectionError(
            f"{path.name}:{line_number}: {field} must be an integer: {row[field]!r}"
        ) from error


def load_hard_roots(path: Path, minimum_regret: int, seed: int) -> tuple[list[HardRoot], dict]:
    required = {
        "root_board_id",
        "root_record_id",
        "root_split",
        "root_phase",
        "move",
        "root_move_score_side_to_move",
        "child_baseline_score_side_to_move",
        "best_score_margin",
        "teacher_kind",
        "teacher_source",
        "teacher_artifact_id",
        "teacher_artifact_checksum",
        "teacher_search_config_id",
    }
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    teacher_identity: tuple[str, str, str, str, str] | None = None
    row_count = 0
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        require_fields(reader.fieldnames, required, path)
        for line_number, row in enumerate(reader, start=2):
            row_count += 1
            board_id = row["root_board_id"]
            if not board_id:
                raise SelectionError(f"{path.name}:{line_number}: empty root_board_id")
            identity = (
                row["teacher_kind"],
                row["teacher_source"],
                row["teacher_artifact_id"],
                row["teacher_artifact_checksum"],
                row["teacher_search_config_id"],
            )
            if teacher_identity is None:
                teacher_identity = identity
            elif teacher_identity != identity:
                raise SelectionError(f"{path.name}:{line_number}: mixed teacher identity")
            grouped[board_id].append(row)

    hard_roots: list[HardRoot] = []
    baseline_miss_roots = 0
    for board_id, rows in grouped.items():
        first = rows[0]
        moves = [row["move"] for row in rows]
        if any(not move for move in moves) or len(set(moves)) != len(moves):
            raise SelectionError(
                f"{path.name}: empty or duplicate move within root {board_id}"
            )
        invariant_fields = ("root_record_id", "root_split", "root_phase")
        if any(row[field] != first[field] for row in rows for field in invariant_fields):
            raise SelectionError(f"{path.name}: inconsistent root metadata for {board_id}")

        teacher_scores = [
            parse_int(row, "root_move_score_side_to_move", path, 0) for row in rows
        ]
        baseline_scores = [
            -parse_int(row, "child_baseline_score_side_to_move", path, 0) for row in rows
        ]
        teacher_best = max(teacher_scores)
        baseline_best = max(baseline_scores)
        baseline_tied_teacher_best = max(
            teacher_score
            for teacher_score, baseline_score in zip(
                teacher_scores, baseline_scores, strict=True
            )
            if baseline_score == baseline_best
        )
        regret = teacher_best - baseline_tied_teacher_best
        if regret > 0:
            baseline_miss_roots += 1
        if regret < minimum_regret:
            continue
        best_score_margin = min(
            parse_int(row, "best_score_margin", path, 0)
            for row, score in zip(rows, teacher_scores, strict=True)
            if score == teacher_best
        )
        phase = parse_int(first, "root_phase", path, 0)
        if phase not in range(13) or first["root_split"] not in {
            "train",
            "validation",
            "test",
        }:
            raise SelectionError(f"{path.name}: invalid phase or split for {board_id}")
        hard_roots.append(
            HardRoot(
                board_id=board_id,
                record_id=first["root_record_id"],
                split=first["root_split"],
                phase=phase,
                baseline_regret=regret,
                best_score_margin=best_score_margin,
                tie_break=stable_hash(board_id, seed),
            )
        )

    kind, source, artifact_id, artifact_checksum, search_config_id = (
        teacher_identity or ("", "", "", "", "")
    )
    return hard_roots, {
        "move_rows": row_count,
        "teacher_roots": len(grouped),
        "baseline_miss_roots": baseline_miss_roots,
        "eligible_hard_roots": len(hard_roots),
        "teacher_kind": kind,
        "teacher_source": source,
        "teacher_artifact_id": artifact_id,
        "teacher_artifact_checksum": artifact_checksum,
        "teacher_search_config_id": search_config_id,
    }


def phase_balanced_selection(roots: list[HardRoot], maximum: int) -> list[HardRoot]:
    by_phase: dict[int, list[HardRoot]] = defaultdict(list)
    for root in roots:
        by_phase[root.phase].append(root)
    for phase_roots in by_phase.values():
        phase_roots.sort(
            key=lambda root: (
                -root.baseline_regret,
                root.best_score_margin,
                root.tie_break,
                root.board_id,
            )
        )

    selected: list[HardRoot] = []
    offset = 0
    phases = sorted(by_phase)
    while len(selected) < maximum:
        added = False
        for phase in phases:
            if offset < len(by_phase[phase]):
                selected.append(by_phase[phase][offset])
                added = True
                if len(selected) == maximum:
                    break
        if not added:
            break
        offset += 1
    return selected


def cross_split_entity_count(rows: list[dict[str, str]], field: str) -> int:
    splits_by_entity: dict[str, set[str]] = defaultdict(set)
    for row in rows:
        splits_by_entity[row[field]].add(row["split"])
    return sum(len(splits) > 1 for splits in splits_by_entity.values())


def materialize_normalized(
    source: Path, output: Path, selected: list[HardRoot]
) -> tuple[Counter[str], Counter[str], int, int, int]:
    by_record = {root.record_id: root for root in selected}
    if len(by_record) != len(selected):
        raise SelectionError("selected roots contain duplicate root_record_id values")
    rows: list[dict[str, str]] = []
    fieldnames: list[str] | None = None
    seen_records: set[str] = set()
    changed_split_assignments = 0
    with source.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        required = {
            "record_id",
            "game_group_id",
            "board_id",
            "source_dataset_id",
            "split",
            "phase",
        }
        require_fields(reader.fieldnames, required, source)
        fieldnames = reader.fieldnames
        for line_number, row in enumerate(reader, start=2):
            root = by_record.get(row["record_id"])
            if root is None:
                continue
            for field in ("game_group_id", "source_dataset_id"):
                if not row[field]:
                    raise SelectionError(
                        f"{source.name}:{line_number}: empty required field {field}"
                    )
            if (
                row["board_id"] != root.board_id
                or int(row["phase"]) != root.phase
            ):
                raise SelectionError(
                    f"{source.name}:{line_number}: normalized/teacher root metadata mismatch"
                )
            if row["split"] != root.split:
                row["split"] = root.split
                changed_split_assignments += 1
            if row["record_id"] in seen_records:
                raise SelectionError(
                    f"{source.name}:{line_number}: duplicate selected record_id"
                )
            seen_records.add(row["record_id"])
            rows.append(row)

    found = {row["record_id"] for row in rows}
    missing = sorted(set(by_record) - found)
    if missing:
        preview = ", ".join(missing[:3])
        raise SelectionError(f"{len(missing)} selected roots are absent from normalized input: {preview}")
    assert fieldnames is not None

    board_collision_count = cross_split_entity_count(rows, "board_id")
    game_group_collision_count = cross_split_entity_count(rows, "game_group_id")
    if board_collision_count or game_group_collision_count:
        raise SelectionError(
            "selected roots have cross-split leakage after teacher split assignment: "
            f"board_id={board_collision_count}, game_group_id={game_group_collision_count}"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", newline="", encoding="utf-8", dir=output.parent, delete=False
    ) as handle:
        temporary = Path(handle.name)
        writer = csv.DictWriter(handle, delimiter="\t", fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    os.replace(temporary, output)
    return (
        Counter(row["phase"] for row in rows),
        Counter(row["split"] for row in rows),
        changed_split_assignments,
        board_collision_count,
        game_group_collision_count,
    )


def main() -> int:
    args = parse_args()
    try:
        hard_roots, teacher_stats = load_hard_roots(
            args.move_teacher_tsv, args.minimum_baseline_regret, args.seed
        )
        selected = phase_balanced_selection(hard_roots, args.max_roots)
        (
            phase_counts,
            split_counts,
            changed_split_assignments,
            board_collision_count,
            game_group_collision_count,
        ) = materialize_normalized(args.normalized_tsv, args.output_tsv, selected)
        regrets = [root.baseline_regret for root in selected]
        report = {
            "schema_version": 1,
            "selection_policy": "phase-balanced-baseline-regret-v1",
            "inputs": {
                "normalized_tsv": args.normalized_tsv.name,
                "normalized_checksum": sha256_file(args.normalized_tsv),
                "move_teacher_tsv": args.move_teacher_tsv.name,
                "move_teacher_checksum": sha256_file(args.move_teacher_tsv),
            },
            "teacher": teacher_stats,
            "config": {
                "max_roots": args.max_roots,
                "minimum_baseline_regret": args.minimum_baseline_regret,
                "seed": args.seed,
                "phase_balance": "round-robin over per-phase difficulty rankings",
                "difficulty_order": [
                    "baseline_regret descending",
                    "teacher best-score margin ascending",
                    "seeded board hash",
                ],
                "baseline_tie_policy": "optimistic minimum regret among baseline-best ties",
                "split_policy": "preserve the move-teacher root split",
            },
            "selected_roots": len(selected),
            "partial_selection": len(selected) < args.max_roots,
            "changed_split_assignments_from_normalized_input": changed_split_assignments,
            "selected_cross_split_board_collision_count": board_collision_count,
            "selected_cross_split_game_group_collision_count": game_group_collision_count,
            "counts_by_phase": dict(sorted(phase_counts.items(), key=lambda item: int(item[0]))),
            "counts_by_split": dict(sorted(split_counts.items())),
            "baseline_regret": {
                "minimum": min(regrets) if regrets else None,
                "maximum": max(regrets) if regrets else None,
                "mean": statistics.fmean(regrets) if regrets else None,
                "median": statistics.median(regrets) if regrets else None,
            },
            "output": {
                "normalized_tsv": args.output_tsv.name,
                "checksum": sha256_file(args.output_tsv),
            },
            "non_claim_notes": [
                "local-only hard-root selection",
                "selection is for deeper teacher relabeling, not a strength claim",
                "generated teacher data and reports must not be committed",
            ],
        }
        args.report_out.parent.mkdir(parents=True, exist_ok=True)
        args.report_out.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except (OSError, SelectionError, ValueError) as error:
        print(f"error: {error}", file=os.sys.stderr)
        return 1

    print(f"selected_roots={len(selected)}")
    print(f"output={args.output_tsv}")
    print(f"report={args.report_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
