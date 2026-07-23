#!/usr/bin/env python3
"""Tiny CI smoke coverage for the shadow calibration analyzer."""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


def load_analyzer(path: Path):
    spec = importlib.util.spec_from_file_location("shadow_analyzer", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load analyzer")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sample(index: int = 0) -> dict[str, object]:
    shallow = index - 2
    deep = 2 * shallow + 1
    alpha = -20
    beta = 20
    return {
        "schema_version": 5,
        "repo_sha": "0123456789abcdef",
        "search_config_id": "tiny-v1",
        "evaluator_id": "fixture-eval",
        "artifact_id": "none",
        "collection_config_id": "1234567890abcdef",
        "canonical_position_hash": f"{index + 1:016x}",
        "phase": 3,
        "occupied_count": 20,
        "empties": 44,
        "ply": index,
        "search_role": "pv",
        "node_type": "pv",
        "pv_node": True,
        "cut_node": False,
        "all_node": False,
        "collection_pair_index": 0,
        "collection_pair_count": 1,
        "same_deep_pair_index": 0,
        "same_deep_pair_count": 1,
        "deep_depth": 6,
        "shallow_depth": 3,
        "official_alpha": alpha,
        "official_beta": beta,
        "official_deep_score": deep,
        "official_deep_bound": "exact",
        "shallow_verification_score": shallow,
        "deep_verification_score": deep,
        "shallow_verification_bound": "exact",
        "deep_verification_bound": "exact",
        "shallow_verification_best_move": "d3",
        "deep_verification_best_move": "d3" if index % 2 == 0 else "c4",
        "verification_best_move_agreement": index % 2 == 0,
        "pass_state": False,
        "terminal_state": False,
        "search_mode": "move",
        "exact_handoff_enabled": False,
        "exact_handoff_threshold": 0,
        "exact_handoff_distance": 0,
        "exact_handoff_eligible": False,
        "actual_official_deep_result": "exact",
        "hypothetical_cut_high": shallow >= beta,
        "hypothetical_cut_low": shallow <= alpha,
        "false_cut_high_candidate": shallow >= beta and deep < beta,
        "false_cut_low_candidate": shallow <= alpha and deep > alpha,
        "sampling_seed": 7,
        "search_identity": "fedcba9876543210",
    }


def non_pv_scout(row: dict[str, object]) -> dict[str, object]:
    row = dict(row)
    alpha = 0
    beta = 1
    deep = int(row["official_deep_score"])
    result = "fail_low" if deep <= alpha else "fail_high"
    node_type = "all" if result == "fail_low" else "cut"
    shallow = int(row["shallow_verification_score"])
    verified_deep = int(row["deep_verification_score"])
    row.update(
        {
            "search_role": "non_pv_scout",
            "node_type": node_type,
            "pv_node": False,
            "cut_node": node_type == "cut",
            "all_node": node_type == "all",
            "official_alpha": alpha,
            "official_beta": beta,
            "official_deep_bound": "upper" if result == "fail_low" else "lower",
            "actual_official_deep_result": result,
            "hypothetical_cut_high": shallow >= beta,
            "hypothetical_cut_low": shallow <= alpha,
            "false_cut_high_candidate": shallow >= beta and verified_deep < beta,
            "false_cut_low_candidate": shallow <= alpha and verified_deep > alpha,
        }
    )
    return row


class AnalyzerTests(unittest.TestCase):
    analyzer_path: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.analyzer = load_analyzer(cls.analyzer_path)

    def test_output_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "samples.jsonl"
            input_path.write_text("".join(json.dumps(sample(index)) + "\n" for index in range(6)), encoding="utf-8")
            first_json, first_md = root / "first.json", root / "first.md"
            second_json, second_md = root / "second.json", root / "second.md"
            self.analyzer.analyze([input_path], first_json, first_md)
            self.analyzer.analyze([input_path], second_json, second_md)
            self.assertEqual(first_json.read_bytes(), second_json.read_bytes())
            self.assertEqual(first_md.read_bytes(), second_md.read_bytes())
            report = json.loads(first_json.read_text(encoding="utf-8"))
            self.assertEqual(report["sample_count"], 6)
            self.assertEqual(report["exact_pair_count"], 6)
            self.assertEqual(report["group_count"], 1)
            self.assertEqual(
                report["groups"][0]["linear_regression"],
                {"intercept": 1.0, "slope": 2.0},
            )
            self.assertTrue(report["groups"][0]["insufficient_samples"])
            self.assertFalse(report["groups"][0]["recommendation_eligible"])
            self.assertIsNone(report["groups"][0]["recommended_conservative_margin"])
            self.assertEqual(report["provenance_inventory"][0]["sample_count"], 6)
            self.assertEqual(len(report["input_checksum_sha256"]), 64)

    def test_empty_samples_produce_empty_report(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            empty = root / "empty.jsonl"
            empty.write_text("", encoding="utf-8")
            report = self.analyzer.analyze([empty], root / "report.json", root / "summary.md")
            self.assertEqual(report["sample_count"], 0)
            self.assertEqual(report["exact_pair_count"], 0)
            self.assertTrue(report["insufficient_samples"])
            self.assertFalse(report["recommendation_eligible"])
            self.assertEqual(report["groups"], [])
            self.assertIn("No valid samples", (root / "summary.md").read_text(encoding="utf-8"))

    def test_invalid_schema_is_rejected(self) -> None:
        bad = sample()
        bad["schema_version"] = 2
        with self.assertRaises(self.analyzer.CalibrationInputError):
            self.analyzer.validate_sample(bad, "fixture")

    def test_bound_observations_do_not_change_value_regression(self) -> None:
        exact_rows = [
            self.analyzer.validate_sample(non_pv_scout(sample(index)), f"exact[{index}]")
            for index in range(6)
        ]
        fail_high = non_pv_scout(sample(100))
        fail_high.update(
            {
                "canonical_position_hash": "0000000000000100",
                "ply": 10,
                "shallow_verification_score": 30_000,
                "deep_verification_score": 30_000,
                "shallow_verification_bound": "lower",
                "deep_verification_bound": "lower",
                "hypothetical_cut_high": True,
                "hypothetical_cut_low": False,
                "false_cut_high_candidate": False,
                "false_cut_low_candidate": False,
            }
        )
        fail_low = non_pv_scout(sample(101))
        fail_low.update(
            {
                "canonical_position_hash": "0000000000000101",
                "ply": 11,
                "shallow_verification_score": -30_000,
                "deep_verification_score": -30_000,
                "shallow_verification_bound": "upper",
                "deep_verification_bound": "upper",
                "hypothetical_cut_high": False,
                "hypothetical_cut_low": True,
                "false_cut_high_candidate": False,
                "false_cut_low_candidate": False,
            }
        )
        bound_rows = [
            self.analyzer.validate_sample(fail_high, "fail_high"),
            self.analyzer.validate_sample(fail_low, "fail_low"),
        ]
        exact_report = self.analyzer.build_report(exact_rows, "a" * 64, 1, 2)
        mixed_report = self.analyzer.build_report(exact_rows + bound_rows, "b" * 64, 1, 2)
        exact_group = exact_report["groups"][0]
        mixed_group = mixed_report["groups"][0]
        self.assertEqual(mixed_group["linear_regression"], exact_group["linear_regression"])
        self.assertEqual(mixed_group["absolute_residual_percentiles"], exact_group["absolute_residual_percentiles"])
        self.assertTrue(exact_group["recommendation_eligible"])
        self.assertEqual(exact_group["recommended_conservative_margin"], 0)
        self.assertEqual(mixed_group["exact_pair_count"], 6)
        self.assertEqual(mixed_group["bound_observation_count"], 2)

    def test_mixed_provenance_is_rejected(self) -> None:
        first = self.analyzer.validate_sample(sample(0), "first")
        second = sample(1)
        second["artifact_id"] = "different-artifact"
        second = self.analyzer.validate_sample(second, "second")
        with self.assertRaisesRegex(self.analyzer.CalibrationInputError, "mixed provenance"):
            self.analyzer.build_report([first, second], "a" * 64, 1)

    def test_non_pv_null_window_uses_exact_verification_pairs(self) -> None:
        rows = []
        for index in range(6):
            row = sample(index)
            row.update(
                {
                    "node_type": "cut",
                    "search_role": "non_pv_scout",
                    "pv_node": False,
                    "cut_node": True,
                    "all_node": False,
                    "official_alpha": 0,
                    "official_beta": 1,
                    "official_deep_score": 1,
                    "official_deep_bound": "lower",
                    "actual_official_deep_result": "fail_high",
                }
            )
            shallow = int(row["shallow_verification_score"])
            deep = int(row["deep_verification_score"])
            row["hypothetical_cut_high"] = shallow >= 1
            row["hypothetical_cut_low"] = shallow <= 0
            row["false_cut_high_candidate"] = shallow >= 1 and deep < 1
            row["false_cut_low_candidate"] = shallow <= 0 and deep > 0
            rows.append(self.analyzer.validate_sample(row, f"cut[{index}]"))

        report = self.analyzer.build_report(rows, "c" * 64, 1, 4)
        group = report["groups"][0]
        self.assertEqual(group["search_role"], "non_pv_scout")
        self.assertEqual(group["sample_count"], 6)
        self.assertEqual(group["exact_pair_count"], 6)
        self.assertEqual(group["bound_observation_count"], 0)
        self.assertTrue(group["recommendation_eligible"])
        self.assertEqual(
            group["linear_regression"], {"intercept": 1.0, "slope": 2.0}
        )

    def test_probcut_group_includes_scout_fail_high_and_fail_low(self) -> None:
        rows = [
            self.analyzer.validate_sample(non_pv_scout(sample(index)), f"scout[{index}]")
            for index in range(6)
        ]
        report = self.analyzer.build_report(rows, "d" * 64, 1, 2)
        group = report["groups"][0]
        self.assertEqual(group["search_role"], "non_pv_scout")
        self.assertEqual(group["sample_count"], 6)
        self.assertTrue(group["recommendation_eligible"])
        diagnostic_types = {
            item["node_type"]: item["sample_count"]
            for item in report["result_node_type_groups"]
        }
        self.assertGreater(diagnostic_types["cut"], 0)
        self.assertGreater(diagnostic_types["all"], 0)

    def test_mixed_collection_policy_is_rejected(self) -> None:
        first = self.analyzer.validate_sample(sample(0), "first")
        second = sample(1)
        second["collection_config_id"] = "abcdef0123456789"
        second = self.analyzer.validate_sample(second, "second")
        with self.assertRaisesRegex(self.analyzer.CalibrationInputError, "collection_config_id"):
            self.analyzer.build_report([first, second], "a" * 64, 1)

    def test_same_deep_multiple_shallow_pairs_form_one_scheduler_observation(self) -> None:
        rows = []
        for node_index in range(6):
            for pair_index, shallow_depth in enumerate((3, 4)):
                row = non_pv_scout(sample(node_index))
                shallow_score = node_index - 2 + pair_index
                deep_score = 2 * (node_index - 2) + 1
                row.update(
                    {
                        "collection_pair_index": pair_index,
                        "collection_pair_count": 2,
                        "same_deep_pair_index": pair_index,
                        "same_deep_pair_count": 2,
                        "shallow_depth": shallow_depth,
                        "shallow_verification_score": shallow_score,
                        "deep_verification_score": deep_score,
                    }
                )
                deep = deep_score
                shallow = int(row["shallow_verification_score"])
                row.update(
                    {
                        "official_deep_score": deep,
                        "official_deep_bound": "upper" if deep <= 0 else "lower",
                        "node_type": "all" if deep <= 0 else "cut",
                        "cut_node": deep > 0,
                        "all_node": deep <= 0,
                        "actual_official_deep_result": "fail_low" if deep <= 0 else "fail_high",
                        "hypothetical_cut_high": shallow >= 1,
                        "hypothetical_cut_low": shallow <= 0,
                        "false_cut_high_candidate": shallow >= 1 and deep < 1,
                        "false_cut_low_candidate": shallow <= 0 and deep > 0,
                    }
                )
                rows.append(self.analyzer.validate_sample(row, f"node[{node_index}].pair[{pair_index}]"))

        report = self.analyzer.build_report(rows, "e" * 64, 1, 2)
        self.assertEqual(
            report["collection_depth_pairs"],
            [
                {"pair_index": 0, "deep_depth": 6, "shallow_depth": 3},
                {"pair_index": 1, "deep_depth": 6, "shallow_depth": 4},
            ],
        )
        self.assertEqual(len(report["scheduler_observations"]), 6)
        self.assertTrue(all(len(node["pairs"]) == 2 for node in report["scheduler_observations"]))
        self.assertTrue(
            all("canonical_position_hash" in node for node in report["scheduler_observations"])
        )

    def test_incomplete_same_deep_pair_population_is_rejected(self) -> None:
        first = sample(0)
        first.update(
            {
                "collection_pair_count": 2,
                "same_deep_pair_count": 2,
            }
        )
        second = sample(1)
        second.update(
            {
                "collection_pair_index": 1,
                "collection_pair_count": 2,
                "same_deep_pair_count": 2,
                "same_deep_pair_index": 1,
                "shallow_depth": 4,
            }
        )
        rows = [
            self.analyzer.validate_sample(first, "first"),
            self.analyzer.validate_sample(second, "second"),
        ]
        with self.assertRaisesRegex(self.analyzer.CalibrationInputError, "complete same-deep"):
            self.analyzer.build_report(rows, "f" * 64, 1)

    def test_exact_handoff_domain_is_observed_not_inferred(self) -> None:
        row = sample(0)
        row.update(
            {
                "occupied_count": 52,
                "empties": 12,
                "exact_handoff_enabled": True,
                "exact_handoff_threshold": 8,
                "exact_handoff_distance": 4,
            }
        )
        validated = self.analyzer.validate_sample(row, "handoff")
        report = self.analyzer.build_report([validated], "1" * 64, 1, 2)
        group = report["groups"][0]
        self.assertTrue(group["exact_handoff_enabled"])
        self.assertEqual(group["exact_handoff_threshold"], 8)
        self.assertEqual(group["observed_domain"]["empties_bucket"]["observed_values"], [12])
        self.assertEqual(
            group["observed_domain"]["exact_handoff_distance_bucket"]["observed_values"],
            [4],
        )

    def test_single_exact_pair_has_no_fit_or_margin(self) -> None:
        row = self.analyzer.validate_sample(sample(), "single")
        report = self.analyzer.build_report([row], "a" * 64, 1, 2)
        group = report["groups"][0]
        self.assertEqual(group["sample_count"], 1)
        self.assertEqual(group["exact_pair_count"], 1)
        self.assertTrue(group["insufficient_samples"])
        self.assertFalse(group["recommendation_eligible"])
        self.assertIsNone(group["linear_regression"])
        self.assertIsNone(group["recommended_conservative_margin"])

    def test_missing_field_is_rejected(self) -> None:
        bad = sample()
        del bad["deep_verification_score"]
        with self.assertRaises(self.analyzer.CalibrationInputError):
            self.analyzer.validate_sample(bad, "fixture")

    def test_malformed_json_is_rejected_by_cli(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            malformed = root / "bad.jsonl"
            malformed.write_text("{not json}\n", encoding="utf-8")
            result = subprocess.run(
                [
                    sys.executable,
                    str(self.analyzer_path),
                    str(malformed),
                    "--json-output",
                    str(root / "report.json"),
                    "--markdown-output",
                    str(root / "summary.md"),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("malformed JSON", result.stderr)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--analyzer", type=Path, required=True)
    args = parser.parse_args()
    AnalyzerTests.analyzer_path = args.analyzer
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(AnalyzerTests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
