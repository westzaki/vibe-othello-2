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
        "schema_version": 2,
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
        "node_type": "pv",
        "pv_node": True,
        "cut_node": False,
        "all_node": False,
        "deep_depth": 6,
        "shallow_depth": 3,
        "alpha": alpha,
        "beta": beta,
        "shallow_score": shallow,
        "deep_score": deep,
        "shallow_bound": "exact",
        "deep_bound": "exact",
        "shallow_best_move": "d3",
        "deep_best_move": "d3" if index % 2 == 0 else "c4",
        "best_move_agreement": index % 2 == 0,
        "pass_state": False,
        "terminal_state": False,
        "exact_handoff_eligible": False,
        "actual_shallow_result": "exact",
        "actual_deep_result": "exact",
        "hypothetical_cut_high": shallow >= beta,
        "hypothetical_cut_low": shallow <= alpha,
        "false_cut_high_candidate": shallow >= beta and deep < beta,
        "false_cut_low_candidate": shallow <= alpha and deep > alpha,
        "sampling_seed": 7,
        "search_identity": "fedcba9876543210",
    }


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
            self.assertEqual(report["groups"][0]["linear_regression"], {"a": 1.0, "b": 2.0})
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
        bad["schema_version"] = 1
        with self.assertRaises(self.analyzer.CalibrationInputError):
            self.analyzer.validate_sample(bad, "fixture")

    def test_bound_observations_do_not_change_value_regression(self) -> None:
        exact_rows = [self.analyzer.validate_sample(sample(index), f"exact[{index}]") for index in range(6)]
        fail_high = sample(100)
        fail_high.update(
            {
                "canonical_position_hash": "0000000000000100",
                "ply": 10,
                "shallow_score": 1000,
                "deep_score": 3000,
                "shallow_bound": "lower",
                "deep_bound": "lower",
                "actual_shallow_result": "fail_high",
                "actual_deep_result": "fail_high",
                "hypothetical_cut_high": True,
                "hypothetical_cut_low": False,
                "false_cut_high_candidate": False,
                "false_cut_low_candidate": False,
            }
        )
        fail_low = sample(101)
        fail_low.update(
            {
                "canonical_position_hash": "0000000000000101",
                "ply": 11,
                "shallow_score": -1000,
                "deep_score": -3000,
                "shallow_bound": "upper",
                "deep_bound": "upper",
                "actual_shallow_result": "fail_low",
                "actual_deep_result": "fail_low",
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

    def test_mixed_collection_policy_is_rejected(self) -> None:
        first = self.analyzer.validate_sample(sample(0), "first")
        second = sample(1)
        second["collection_config_id"] = "abcdef0123456789"
        second = self.analyzer.validate_sample(second, "second")
        with self.assertRaisesRegex(self.analyzer.CalibrationInputError, "collection_config_id"):
            self.analyzer.build_report([first, second], "a" * 64, 1)

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
        del bad["deep_score"]
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
