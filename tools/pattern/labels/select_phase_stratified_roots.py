#!/usr/bin/env python3
"""Select deterministic, phase-stratified roots from normalized schema v2 TSV."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import sys
from collections import Counter, defaultdict, deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import normalized_v2_contract as normalized_contract


SCHEMA_VERSION = 1
PHASES = tuple(range(13))
LOCAL_ONLY_NOTES = [
    "local-only phase-stratified root selection",
    "preserves input split assignments and selected source rows",
    "generated selected TSVs and reports must not be committed",
    "not an Elo result",
    "not self-play",
    "not a production strength claim",
    "does not generate teacher labels or train an artifact",
]


class SelectorError(RuntimeError):
    pass


@dataclass(frozen=True)
class Root:
    row: dict[str, str]
    input_index: int
    sample_key: int


@dataclass
class FlowEdge:
    destination: int
    reverse_index: int
    capacity: int


class CapacityMatcher:
    """Small deterministic Dinic matcher for the optional per-game cap."""

    def __init__(self, node_count: int) -> None:
        self.graph: list[list[FlowEdge]] = [[] for _ in range(node_count)]

    def add_edge(self, source: int, destination: int, capacity: int) -> FlowEdge:
        forward = FlowEdge(destination=destination, reverse_index=len(self.graph[destination]), capacity=capacity)
        reverse = FlowEdge(destination=source, reverse_index=len(self.graph[source]), capacity=0)
        self.graph[source].append(forward)
        self.graph[destination].append(reverse)
        return forward

    def build_levels(self, source: int) -> list[int]:
        levels = [-1] * len(self.graph)
        levels[source] = 0
        queue: deque[int] = deque([source])
        while queue:
            node = queue.popleft()
            for edge in self.graph[node]:
                if edge.capacity <= 0 or levels[edge.destination] >= 0:
                    continue
                levels[edge.destination] = levels[node] + 1
                queue.append(edge.destination)
        return levels

    def send_blocking_flow(
        self, node: int, sink: int, available: int, levels: list[int], next_edge: list[int]
    ) -> int:
        if node == sink:
            return available
        while next_edge[node] < len(self.graph[node]):
            edge = self.graph[node][next_edge[node]]
            if edge.capacity > 0 and levels[edge.destination] == levels[node] + 1:
                sent = self.send_blocking_flow(
                    edge.destination,
                    sink,
                    min(available, edge.capacity),
                    levels,
                    next_edge,
                )
                if sent:
                    edge.capacity -= sent
                    self.graph[edge.destination][edge.reverse_index].capacity += sent
                    return sent
            next_edge[node] += 1
        return 0

    def max_flow(self, source: int, sink: int) -> int:
        total = 0
        while (levels := self.build_levels(source))[sink] >= 0:
            next_edge = [0] * len(self.graph)
            while sent := self.send_blocking_flow(source, sink, sys.maxsize, levels, next_edge):
                total += sent
        return total


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalized-tsv", required=True, type=Path)
    parser.add_argument("--output-tsv", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    parser.add_argument("--roots-per-phase", required=True, type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--max-roots-per-game-group", type=int)
    parser.add_argument("--require-all-phases", action="store_true")
    args = parser.parse_args()
    if args.roots_per_phase <= 0:
        parser.error("--roots-per-phase must be positive")
    if args.seed < 0:
        parser.error("--seed must be non-negative")
    if args.max_roots_per_game_group is not None and args.max_roots_per_game_group <= 0:
        parser.error("--max-roots-per-game-group must be positive")
    resolved_paths = {
        "normalized TSV": args.normalized_tsv.resolve(),
        "selected TSV": args.output_tsv.resolve(),
        "report": args.report_out.resolve(),
    }
    if len(set(resolved_paths.values())) != len(resolved_paths):
        parser.error("--normalized-tsv, --output-tsv, and --report-out must name different files")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def report_path(path: Path) -> str:
    return path.name if path.is_absolute() else str(path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return f"sha256:{digest.hexdigest()}"


def sha256_text(text: str) -> str:
    return f"sha256:{hashlib.sha256(text.encode('utf-8')).hexdigest()}"


def counter_dict(counter: Counter[str]) -> dict[str, int]:
    return {key: counter[key] for key in sorted(counter)}


def root_order(root: Root) -> tuple[int, str]:
    return root.sample_key, root.row["board_id"]


def cross_split_entity_count(splits_by_entity: dict[str, set[str]]) -> int:
    return sum(1 for splits in splits_by_entity.values() if len(splits) > 1)


def load_roots(path: Path, seed: int) -> tuple[list[Root], dict[str, Any]]:
    try:
        rows = normalized_contract.load_normalized_rows(path)
    except (OSError, UnicodeDecodeError, normalized_contract.NormalizedV2ContractError) as error:
        raise SelectorError(str(error)) from error

    input_split_counts: Counter[str] = Counter()
    input_phase_counts: Counter[str] = Counter()
    board_splits: dict[str, set[str]] = {}
    game_splits: dict[str, set[str]] = {}
    board_contents: dict[str, str] = {}
    roots_by_board: dict[str, Root] = {}
    duplicate_board_rows = 0
    duplicate_board_ids: set[str] = set()

    for input_index, row in enumerate(rows):
        board_id = row["board_id"]
        game_group_id = row["game_group_id"]
        input_split_counts[row["split"]] += 1
        input_phase_counts[row["phase"]] += 1
        board_splits.setdefault(board_id, set()).add(row["split"])
        game_splits.setdefault(game_group_id, set()).add(row["split"])
        previous_contents = board_contents.get(board_id)
        if previous_contents is not None and previous_contents != row["board_a1_to_h8"]:
            raise SelectorError(f"{board_id}: same board_id has different board contents")
        board_contents.setdefault(board_id, row["board_a1_to_h8"])
        if board_id in roots_by_board:
            duplicate_board_rows += 1
            duplicate_board_ids.add(board_id)
            continue
        roots_by_board[board_id] = Root(
            row=dict(row),
            input_index=input_index,
            sample_key=normalized_contract.fnv_sample_key(board_id, seed),
        )

    board_collision_count = cross_split_entity_count(board_splits)
    if board_collision_count:
        raise SelectorError(
            "input normalized TSV has board_id cross-split leakage: "
            f"{board_collision_count} board_id collision(s)"
        )
    game_collision_count = cross_split_entity_count(game_splits)
    if game_collision_count:
        raise SelectorError(
            "input normalized TSV has game_group_id cross-split leakage: "
            f"{game_collision_count} game_group_id collision(s)"
        )

    roots = list(roots_by_board.values())
    unique_split_counts: Counter[str] = Counter(root.row["split"] for root in roots)
    unique_phase_counts: Counter[str] = Counter(root.row["phase"] for root in roots)
    return roots, {
        "input_rows": len(rows),
        "eligible_rows": len(rows),
        "input_split_counts": counter_dict(input_split_counts),
        "input_phase_counts": counter_dict(input_phase_counts),
        "input_game_group_count": len(game_splits),
        "unique_eligible_boards": len(roots),
        "unique_eligible_split_counts": counter_dict(unique_split_counts),
        "unique_eligible_phase_counts": counter_dict(unique_phase_counts),
        "unique_eligible_game_group_count": len({root.row["game_group_id"] for root in roots}),
        "duplicate_board_rows": duplicate_board_rows,
        "duplicate_board_id_count": len(duplicate_board_ids),
        "cross_split_board_collision_count": board_collision_count,
        "cross_split_game_group_collision_count": game_collision_count,
    }


def roots_by_phase_and_game(roots: list[Root]) -> dict[tuple[int, str], list[Root]]:
    grouped: dict[tuple[int, str], list[Root]] = defaultdict(list)
    for root in roots:
        phase = normalized_contract.parse_int(root.row["phase"], "phase")
        grouped[(phase, root.row["game_group_id"])].append(root)
    for candidates in grouped.values():
        candidates.sort(key=root_order)
    return grouped


def select_without_game_cap(
    grouped: dict[tuple[int, str], list[Root]], roots_per_phase: int
) -> list[Root]:
    selected: list[Root] = []
    for phase in PHASES:
        candidates = [root for (candidate_phase, _), values in grouped.items() if candidate_phase == phase for root in values]
        selected.extend(sorted(candidates, key=root_order)[:roots_per_phase])
    return selected


def select_with_game_cap(
    grouped: dict[tuple[int, str], list[Root]], roots_per_phase: int, max_roots_per_game_group: int, seed: int
) -> tuple[list[Root], int]:
    games = sorted(
        {game_group_id for _, game_group_id in grouped},
        key=lambda game_group_id: (normalized_contract.fnv_sample_key(f"game:{game_group_id}", seed), game_group_id),
    )
    phase_order = sorted(
        PHASES,
        key=lambda phase: (
            sum(len(values) for (candidate_phase, _), values in grouped.items() if candidate_phase == phase),
            phase,
        ),
    )
    source = 0
    phase_nodes = {phase: index + 1 for index, phase in enumerate(phase_order)}
    game_nodes = {game: len(phase_nodes) + index + 1 for index, game in enumerate(games)}
    sink = len(phase_nodes) + len(game_nodes) + 1
    matcher = CapacityMatcher(sink + 1)
    cell_edges: dict[tuple[int, str], tuple[FlowEdge, int]] = {}

    for phase in phase_order:
        matcher.add_edge(source, phase_nodes[phase], roots_per_phase)
    for phase in phase_order:
        for game in games:
            candidates = grouped.get((phase, game), [])
            if not candidates:
                continue
            edge = matcher.add_edge(phase_nodes[phase], game_nodes[game], len(candidates))
            cell_edges[(phase, game)] = (edge, len(candidates))
    for game in games:
        matcher.add_edge(game_nodes[game], sink, max_roots_per_game_group)

    max_selected = matcher.max_flow(source, sink)
    selected: list[Root] = []
    for key, (edge, initial_capacity) in cell_edges.items():
        selected.extend(grouped[key][: initial_capacity - edge.capacity])
    return selected, max_selected


def render_selected_rows(rows: list[Root]) -> str:
    output = io.StringIO()
    writer = csv.DictWriter(
        output,
        delimiter="\t",
        lineterminator="\n",
        fieldnames=normalized_contract.NORMALIZED_HEADER_V2,
    )
    writer.writeheader()
    for root in sorted(rows, key=lambda item: item.row["board_id"]):
        writer.writerow(root.row)
    return output.getvalue()


def command_args(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "normalized_tsv": report_path(args.normalized_tsv),
        "output_tsv": report_path(args.output_tsv),
        "report_out": report_path(args.report_out),
        "roots_per_phase": args.roots_per_phase,
        "seed": args.seed,
        "max_roots_per_game_group": args.max_roots_per_game_group,
        "require_all_phases": bool(args.require_all_phases),
    }


def build_report(
    args: argparse.Namespace,
    input_report: dict[str, Any],
    roots: list[Root],
    selected: list[Root],
    output_checksum: str,
    cap_max_selected: int | None,
) -> dict[str, Any]:
    unique_phase_counts: Counter[str] = Counter(root.row["phase"] for root in roots)
    selected_phase_counts: Counter[str] = Counter(root.row["phase"] for root in selected)
    selected_split_counts: Counter[str] = Counter(root.row["split"] for root in selected)
    selected_phase_split_counts_by_key: Counter[tuple[str, str]] = Counter(
        (root.row["phase"], root.row["split"]) for root in selected
    )
    selected_phase_split_counts: dict[str, dict[str, int]] = {}
    phase_coverage: dict[str, dict[str, Any]] = {}
    shortage_phases: list[int] = []
    for phase in PHASES:
        key = str(phase)
        selected_count = selected_phase_counts[key]
        shortage = max(0, args.roots_per_phase - selected_count)
        if shortage:
            shortage_phases.append(phase)
        phase_coverage[key] = {
            "eligible_rows": input_report["input_phase_counts"].get(key, 0),
            "unique_eligible_boards": unique_phase_counts[key],
            "requested_roots": args.roots_per_phase,
            "selected_roots": selected_count,
            "shortage_roots": shortage,
            "quota_satisfied": shortage == 0,
        }
        selected_phase_split_counts[key] = {
            split: selected_phase_split_counts_by_key[(key, split)]
            for split in normalized_contract.VALID_SPLITS
        }

    quotas_satisfied = not shortage_phases
    cap_policy = (
        "none"
        if args.max_roots_per_game_group is None
        else "deterministic phase-to-game-group capacity matching"
    )
    return {
        "schema_version": SCHEMA_VERSION,
        "normalized_schema_version": 2,
        "selection_policy": {
            "name": "phase-stratified-board-id-fnv1a64-v1",
            "phases": list(PHASES),
            "roots_per_phase": args.roots_per_phase,
            "board_dedupe": "first normalized input row per board_id; conflicting board contents fail",
            "split_policy": "validate connected game/board split; preserve input split values",
            "game_group_cap_policy": cap_policy,
            "output_order": "board_id ascending",
        },
        "input_path": report_path(args.normalized_tsv),
        "output_path": report_path(args.output_tsv),
        "input_checksum": sha256_file(args.normalized_tsv),
        "output_checksum": output_checksum,
        "input_rows": input_report["input_rows"],
        "eligible_rows": input_report["eligible_rows"],
        "unique_eligible_boards": input_report["unique_eligible_boards"],
        "selected_rows": len(selected),
        "input_split_counts": input_report["input_split_counts"],
        "unique_eligible_split_counts": input_report["unique_eligible_split_counts"],
        "selected_split_counts": counter_dict(selected_split_counts),
        "selected_phase_split_counts": selected_phase_split_counts,
        "input_game_group_count": input_report["input_game_group_count"],
        "unique_eligible_game_group_count": input_report["unique_eligible_game_group_count"],
        "selected_game_group_count": len({root.row["game_group_id"] for root in selected}),
        "duplicate_board_rows": input_report["duplicate_board_rows"],
        "duplicate_board_id_count": input_report["duplicate_board_id_count"],
        "cross_split_board_collision_count": input_report["cross_split_board_collision_count"],
        "cross_split_game_group_collision_count": input_report["cross_split_game_group_collision_count"],
        "phase_coverage": phase_coverage,
        "shortage_phases": shortage_phases,
        "all_phase_quotas_satisfied": quotas_satisfied,
        "partial_selection": not quotas_satisfied,
        "seed": args.seed,
        "max_roots_per_game_group": args.max_roots_per_game_group,
        "maximum_selected_rows_under_game_cap": cap_max_selected,
        "command_args": command_args(args),
        "local_only_notes": LOCAL_ONLY_NOTES,
    }


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main() -> int:
    args = parse_args()
    try:
        roots, input_report = load_roots(args.normalized_tsv, args.seed)
        grouped = roots_by_phase_and_game(roots)
        if args.max_roots_per_game_group is None:
            selected = select_without_game_cap(grouped, args.roots_per_phase)
            cap_max_selected = None
        else:
            selected, cap_max_selected = select_with_game_cap(
                grouped,
                args.roots_per_phase,
                args.max_roots_per_game_group,
                args.seed,
            )
        selected_text = render_selected_rows(selected)
        output_checksum = sha256_text(selected_text)
        report = build_report(args, input_report, roots, selected, output_checksum, cap_max_selected)
        write_text(args.output_tsv, selected_text)
        write_text(args.report_out, stable_json(report))
        print(f"selected_rows={len(selected)}")
        print(f"all_phase_quotas_satisfied={report['all_phase_quotas_satisfied']}")
        print(f"report={args.report_out}")
        if args.require_all_phases and not report["all_phase_quotas_satisfied"]:
            print("phase quota shortage; --require-all-phases requested complete coverage", file=sys.stderr)
            return 1
    except SelectorError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
