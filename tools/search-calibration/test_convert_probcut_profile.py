#!/usr/bin/env python3
"""Unit coverage for reviewed report to ProbCut TSV conversion."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


def load_converter(path: Path):
    spec = importlib.util.spec_from_file_location("probcut_converter", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load converter")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def report() -> dict[str, object]:
    return {
        "schema_version": "mpc-shadow-calibration-report-v4",
        "provenance_inventory": [
            {"evaluator_id": "fixture-eval", "artifact_id": "none", "sample_count": 40}
        ],
        "groups": [
            {
                "phase": 3,
                "deep_depth": 6,
                "shallow_depth": 3,
                "search_role": "non_pv_scout",
                "recommendation_eligible": True,
                "linear_regression": {"intercept": -1.25, "slope": 1.5},
                "residual_standard_deviation": 2.75,
            }
        ],
    }


def adoption() -> dict[str, object]:
    return {
        "schema_version": "probcut-profile-adoption-v1",
        "profile_id": "fixture-reviewed-v1",
        "evaluator_family": "fixture-eval",
        "artifact_family": "none",
        "node_class": "non_pv_scout_beta_only",
        "entries": [
            {
                "phase": 3,
                "deep_depth": 6,
                "shallow_depth": 3,
                "confidence_multiplier": 3.5,
                "minimum_shallow_score": -100,
                "maximum_shallow_score": 100,
                "minimum_beta": -80,
                "maximum_beta": 80,
            }
        ],
    }


class ConverterTests(unittest.TestCase):
    converter_path: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.converter = load_converter(cls.converter_path)

    def test_maps_named_coefficients_and_exact_report_checksum(self) -> None:
        raw = (json.dumps(report(), indent=2, sort_keys=True) + "\n").encode()
        rendered = self.converter.render_profile(report(), raw, adoption())
        lines = rendered.splitlines()
        self.assertIn("node_class", lines[0].split("\t"))
        fields = lines[1].split("\t")
        self.assertEqual(fields[2], hashlib.sha256(raw).hexdigest())
        self.assertEqual(fields[5], "non_pv_scout_beta_only")
        self.assertEqual(fields[9], "1.5")
        self.assertEqual(fields[10], "-1.25")

    def test_rejects_post_result_cut_group(self) -> None:
        bad_report = report()
        bad_report["groups"][0]["search_role"] = "cut"
        with self.assertRaisesRegex(self.converter.ProfileConversionError, "non_pv_scout"):
            self.converter.render_profile(bad_report, b"{}\n", adoption())

    def test_cli_conversion_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report_path = root / "report.json"
            adoption_path = root / "adoption.json"
            report_path.write_text(json.dumps(report(), sort_keys=True), encoding="utf-8")
            adoption_path.write_text(json.dumps(adoption(), sort_keys=True), encoding="utf-8")
            first = root / "first.tsv"
            second = root / "second.tsv"
            self.converter.convert(report_path, adoption_path, first)
            self.converter.convert(report_path, adoption_path, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--converter", type=Path, required=True)
    args = parser.parse_args()
    ConverterTests.converter_path = args.converter
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ConverterTests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
