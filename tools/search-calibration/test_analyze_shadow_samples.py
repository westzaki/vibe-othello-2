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
        "schema_version": 1,
        "repo_sha": "0123456789abcdef",
        "search_config_id": "tiny-v1",
        "evaluator_id": "fixture-eval",
        "artifact_id": "none",
        "canonical_position_hash": f"{index + 1:016x}",
        "phase": 3,
        "occupied_count": 20,
        "empties": 44,
        "ply": index,
        "node_type": "all",
        "pv_node": False,
        "cut_node": False,
        "all_node": True,
        "deep_depth": 6,
        "shallow_depth": 3,
        "alpha": alpha,
        "beta": beta,
        "shallow_score": shallow,
        "deep_score": deep,
        "deep_bound": "exact",
        "shallow_best_move": "d3",
        "deep_best_move": "d3" if index % 2 == 0 else "c4",
        "best_move_agreement": index % 2 == 0,
        "pass_state": False,
        "terminal_state": False,
        "exact_handoff_eligible": False,
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
            self.assertEqual(report["group_count"], 1)
            self.assertEqual(report["groups"][0]["linear_regression"], {"a": 1.0, "b": 2.0})
            self.assertEqual(len(report["input_checksum_sha256"]), 64)

    def test_empty_samples_produce_empty_report(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            empty = root / "empty.jsonl"
            empty.write_text("", encoding="utf-8")
            report = self.analyzer.analyze([empty], root / "report.json", root / "summary.md")
            self.assertEqual(report["sample_count"], 0)
            self.assertEqual(report["groups"], [])
            self.assertIn("No valid samples", (root / "summary.md").read_text(encoding="utf-8"))

    def test_invalid_schema_is_rejected(self) -> None:
        bad = sample()
        bad["schema_version"] = 2
        with self.assertRaises(self.analyzer.CalibrationInputError):
            self.analyzer.validate_sample(bad, "fixture")

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
