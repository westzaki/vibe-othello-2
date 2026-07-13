#!/usr/bin/env python3
"""Exercise two same-deep MPC pairs from samples through the native loader."""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path
from types import ModuleType


def load_module(name: str, path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def samples(namespace: int, pair_scores: list[tuple[int, int]]) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for node_index, scores in enumerate(pair_scores):
        deep_score = scores[1]
        official_result = "fail_high" if deep_score >= 1 else "fail_low"
        node_type = "cut" if official_result == "fail_high" else "all"
        official_bound = "lower" if official_result == "fail_high" else "upper"
        for pair_index, (shallow_depth, shallow_score) in enumerate(zip((3, 4), scores)):
            result.append(
                {
                    "schema_version": 5,
                    "repo_sha": "0123456789abcdef",
                    "search_config_id": "same-population-multi-pair-v1",
                    "evaluator_id": "fixed-pattern-fixture-v1",
                    "artifact_id": "candidate.manifest",
                    "collection_config_id": "1234567890abcdef",
                    "canonical_position_hash": f"{namespace + node_index:016x}",
                    "phase": 8,
                    "occupied_count": 44,
                    "empties": 20,
                    "ply": node_index,
                    "search_role": "non_pv_scout",
                    "node_type": node_type,
                    "pv_node": False,
                    "cut_node": node_type == "cut",
                    "all_node": node_type == "all",
                    "collection_pair_index": pair_index,
                    "collection_pair_count": 2,
                    "same_deep_pair_index": pair_index,
                    "same_deep_pair_count": 2,
                    "deep_depth": 8,
                    "shallow_depth": shallow_depth,
                    "official_alpha": 0,
                    "official_beta": 1,
                    "official_deep_score": deep_score,
                    "official_deep_bound": official_bound,
                    "shallow_verification_score": shallow_score,
                    "deep_verification_score": deep_score,
                    "shallow_verification_bound": "exact",
                    "deep_verification_bound": "exact",
                    "shallow_verification_best_move": "d3",
                    "deep_verification_best_move": "d3",
                    "verification_best_move_agreement": True,
                    "pass_state": False,
                    "terminal_state": False,
                    "search_mode": "move",
                    "exact_handoff_enabled": False,
                    "exact_handoff_threshold": 0,
                    "exact_handoff_distance": 0,
                    "exact_handoff_eligible": False,
                    "actual_official_deep_result": official_result,
                    "hypothetical_cut_high": shallow_score >= 1,
                    "hypothetical_cut_low": shallow_score <= 0,
                    "false_cut_high_candidate": shallow_score >= 1 and deep_score < 1,
                    "false_cut_low_candidate": shallow_score <= 0 and deep_score > 0,
                    "sampling_seed": 7 if namespace == 0x1000 else 11,
                    "search_identity": "fedcba9876543210",
                }
            )
    return result


def adoption() -> dict[str, object]:
    base_entry = {
        "phase": 8,
        "search_mode": "move",
        "minimum_empties": 20,
        "maximum_empties": 20,
        "deep_depth": 8,
        "shallow_depth": 3,
        "exact_handoff_enabled": False,
        "exact_handoff_threshold": 0,
        "minimum_exact_handoff_distance": 0,
        "maximum_exact_handoff_distance": 0,
        "confidence_multiplier": 3.5,
        "minimum_shallow_score": -100,
        "maximum_shallow_score": 100,
        "minimum_beta": -80,
        "maximum_beta": 80,
    }
    second_entry = dict(base_entry)
    second_entry["shallow_depth"] = 4
    return {
        "schema_version": "probcut-profile-adoption-v2",
        "profile_id": "same-population-multi-pair-e2e",
        "evaluator_family": "fixed-pattern-fixture-v1",
        "artifact_family": "candidate.manifest",
        "node_class": "non_pv_scout_beta_only",
        "validated_pair_order": [
            {"deep_depth": 8, "shallow_depth": 3},
            {"deep_depth": 8, "shallow_depth": 4},
        ],
        "validated_maximum_probes_per_node": 2,
        "minimum_joint_cut_candidates": 3,
        "maximum_joint_false_cut_rate_upper_bound": 0.8,
        "entries": [base_entry, second_entry],
    }


def write_jsonl(path: Path, rows: list[dict[str, object]]) -> None:
    path.write_text(
        "".join(json.dumps(row, sort_keys=True) + "\n" for row in rows),
        encoding="utf-8",
    )


def run(args: argparse.Namespace) -> None:
    analyzer = load_module("multi_probcut_e2e_analyzer", args.analyzer)
    converter = load_module("multi_probcut_e2e_converter", args.converter)
    artifact_fixture = load_module("multi_probcut_e2e_artifact", args.artifact_fixture)

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        training_samples = root / "training.jsonl"
        holdout_samples = root / "holdout.jsonl"
        write_jsonl(training_samples, samples(0x1000, [(-2, -2), (0, 0), (2, 2), (4, 4)]))
        write_jsonl(holdout_samples, samples(0x2000, [(0, 2), (0, 3), (0, 4)]))

        training_report = root / "training-report.json"
        holdout_report = root / "holdout-report.json"
        training = analyzer.analyze(
            [training_samples], training_report, root / "training-report.md", 4
        )
        holdout = analyzer.analyze(
            [holdout_samples], holdout_report, root / "holdout-report.md", 2
        )
        expected_pairs = [
            {"pair_index": 0, "deep_depth": 8, "shallow_depth": 3},
            {"pair_index": 1, "deep_depth": 8, "shallow_depth": 4},
        ]
        if training["collection_depth_pairs"] != expected_pairs:
            raise AssertionError(f"analyzer lost ordered pairs: {training!r}")
        if any(len(node["pairs"]) != 2 for node in holdout["scheduler_observations"]):
            raise AssertionError("holdout scheduler population is incomplete")

        adoption_path = root / "adoption.json"
        adoption_path.write_text(json.dumps(adoption(), sort_keys=True), encoding="utf-8")
        profile_path = root / "reviewed-profile.tsv"
        converter.convert(training_report, adoption_path, holdout_report, profile_path)
        lines = [line.split("\t") for line in profile_path.read_text(encoding="utf-8").splitlines()]
        header, profile_rows = lines[0], lines[1:]
        pair_columns = (header.index("deep_depth"), header.index("shallow_depth"))
        if [(row[pair_columns[0]], row[pair_columns[1]]) for row in profile_rows] != [
            ("8", "3"),
            ("8", "4"),
        ]:
            raise AssertionError(f"converter lost ordered pairs: {profile_rows!r}")
        if {row[header.index("joint_cut_candidate_count")] for row in profile_rows} != {"3"}:
            raise AssertionError("joint first-success replay did not audit all holdout candidates")

        artifact_fixture.make_tiny_artifact(
            root / "candidate.weights.bin", root / "candidate.manifest.json"
        )
        (root / "openings.txt").write_text("start:\n", encoding="utf-8")
        arena_report = root / "arena.json"
        command = [
            str(args.arena),
            "--candidate-manifest",
            str(root / "candidate.manifest.json"),
            "--baseline-manifest",
            str(root / "candidate.manifest.json"),
            "--openings",
            str(root / "openings.txt"),
            "--opening-limit",
            "1",
            "--report-out",
            str(arena_report),
            "--limit-mode",
            "depth",
            "--depth",
            "1",
            "--candidate-probcut",
            "multi",
            "--baseline-probcut",
            "off",
            "--probcut-profile",
            str(profile_path),
            "--probcut-maximum-margin",
            "10",
            "--probcut-maximum-probes",
            "2",
        ]
        completed = subprocess.run(command, check=False, capture_output=True, text=True)
        if completed.returncode != 0:
            raise AssertionError(
                f"native runtime rejected generated profile\nstdout:\n{completed.stdout}"
                f"\nstderr:\n{completed.stderr}"
            )
        report = json.loads(arena_report.read_text(encoding="utf-8"))
        resolved = report["search_config"]["candidate_resolved_options"]["multi_probcut"]
        if resolved["ordered_depth_pairs"] != [
            {"deep_depth": 8, "shallow_depth": 3},
            {"deep_depth": 8, "shallow_depth": 4},
        ]:
            raise AssertionError(f"runtime lost reviewed scheduler order: {resolved!r}")
        if resolved["joint_holdout_checksum_sha256"] != profile_rows[0][
            header.index("joint_holdout_checksum_sha256")
        ]:
            raise AssertionError(f"runtime lost joint holdout identity: {resolved!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--analyzer", type=Path, required=True)
    parser.add_argument("--converter", type=Path, required=True)
    parser.add_argument("--arena", type=Path, required=True)
    parser.add_argument("--artifact-fixture", type=Path, required=True)
    run(parser.parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
