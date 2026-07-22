#!/usr/bin/env python3
"""Unit coverage for reviewed reports to scheduler-safe ProbCut TSV conversion."""

from __future__ import annotations

import argparse
import copy
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


def provenance(sample_count: int) -> dict[str, object]:
    return {
        "repo_sha": "0123456789abcdef",
        "search_config_id": "fixture-search-v1",
        "evaluator_id": "fixture-eval",
        "artifact_id": "none",
        "sample_count": sample_count,
    }


def group(shallow_depth: int) -> dict[str, object]:
    return {
        "phase": 3,
        "deep_depth": 8,
        "shallow_depth": shallow_depth,
        "search_role": "non_pv_scout",
        "search_mode": "move",
        "exact_handoff_enabled": False,
        "exact_handoff_threshold": 0,
        "observed_domain": {
            "empties_bucket": {"minimum": 20, "maximum": 23, "observed_values": [20]},
            "exact_handoff_distance_bucket": {
                "minimum": 0,
                "maximum": 0,
                "observed_values": [0],
            },
        },
        "recommendation_eligible": True,
        "linear_regression": {"intercept": 0.0, "slope": 1.0},
        "residual_standard_deviation": 0.0,
    }


def observation(node_id: str, pair_scores: tuple[int, int], deep_score: int = 2) -> dict[str, object]:
    return {
        "node_id": node_id,
        "phase": 3,
        "empties": 20,
        "search_mode": "move",
        "exact_handoff_enabled": False,
        "exact_handoff_threshold": 0,
        "exact_handoff_distance": 0,
        "deep_depth": 8,
        "official_alpha": 0,
        "official_beta": 1,
        "deep_verification_score": deep_score,
        "deep_verification_bound": "exact",
        "pairs": [
            {
                "collection_pair_index": 0,
                "same_deep_pair_index": 0,
                "deep_depth": 8,
                "shallow_depth": 3,
                "shallow_verification_score": pair_scores[0],
                "shallow_verification_bound": "exact",
            },
            {
                "collection_pair_index": 1,
                "same_deep_pair_index": 1,
                "deep_depth": 8,
                "shallow_depth": 4,
                "shallow_verification_score": pair_scores[1],
                "shallow_verification_bound": "exact",
            },
        ],
    }


def report(*, holdout: bool = False) -> dict[str, object]:
    observations = (
        [
            observation("holdout-first", (2, -100)),
            observation("holdout-second-a", (0, 2)),
            observation("holdout-second-b", (0, 3)),
        ]
        if holdout
        else [observation("training-node", (0, 2))]
    )
    return {
        "schema_version": "mpc-shadow-calibration-report-v5",
        "provenance_inventory": [provenance(len(observations) * 2)],
        "collection_config_inventory": [
            {"collection_config_id": "1234567890abcdef", "sample_count": len(observations) * 2}
        ],
        "collection_depth_pairs": [
            {"pair_index": 0, "deep_depth": 8, "shallow_depth": 3},
            {"pair_index": 1, "deep_depth": 8, "shallow_depth": 4},
        ],
        "groups": [group(3), group(4)],
        "scheduler_observations": observations,
    }


def adoption() -> dict[str, object]:
    entry = {
        "phase": 3,
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
    second = dict(entry)
    second["shallow_depth"] = 4
    return {
        "schema_version": "probcut-profile-adoption-v3",
        "profile_id": "fixture-reviewed-v3",
        "evaluator_family": "fixture-eval",
        "artifact_family": "none",
        "node_class": "non_pv_scout_beta_only",
        "validated_pair_order": [
            {"deep_depth": 8, "shallow_depth": 3},
            {"deep_depth": 8, "shallow_depth": 4},
        ],
        "validated_maximum_probes_per_node": 2,
        "minimum_joint_cut_candidates": 3,
        "maximum_joint_false_cut_rate_upper_bound": 0.8,
        "entries": [entry, second],
    }


class ConverterTests(unittest.TestCase):
    converter_path: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.converter = load_converter(cls.converter_path)

    def render(self, training=None, reviewed=None, holdout=None, raw=b"training\n", holdout_raw=b"holdout\n") -> str:
        return self.converter.render_profile(
            report() if training is None else training,
            raw,
            adoption() if reviewed is None else reviewed,
            report(holdout=True) if holdout is None else holdout,
            holdout_raw,
        )

    def test_maps_coefficients_checksums_and_joint_evidence(self) -> None:
        rendered = self.render()
        header, first, second = [line.split("\t") for line in rendered.splitlines()]
        self.assertEqual(first[header.index("source_checksum_sha256")], hashlib.sha256(b"training\n").hexdigest())
        self.assertEqual(first[header.index("joint_holdout_checksum_sha256")], hashlib.sha256(b"holdout\n").hexdigest())
        self.assertEqual(first[header.index("joint_false_cut_count")], "0")
        self.assertEqual(first[header.index("joint_cut_candidate_count")], "3")
        self.assertEqual(first[header.index("validated_maximum_probes_per_node")], "2")
        evidence = first[header.index("scheduler_domain_evidence")].split(";")
        self.assertEqual(len(evidence), 1)
        self.assertTrue(evidence[0].startswith("2:2:3:move:20:20:8:false:0:0:0:"))
        self.assertEqual(
            [(row[header.index("deep_depth")], row[header.index("shallow_depth")]) for row in (first, second)],
            [("8", "3"), ("8", "4")],
        )

    def test_first_success_policy_does_not_evaluate_later_false_pair(self) -> None:
        holdout = report(holdout=True)
        holdout["scheduler_observations"] = [observation("safe-first", (2, 2), deep_score=2)]
        holdout["provenance_inventory"] = [provenance(2)]
        holdout["collection_config_inventory"][0]["sample_count"] = 2
        reviewed = adoption()
        reviewed["minimum_joint_cut_candidates"] = 1
        rendered = self.render(reviewed=reviewed, holdout=holdout)
        header, row = [line.split("\t") for line in rendered.splitlines()[:2]]
        self.assertEqual(row[header.index("joint_cut_candidate_count")], "1")
        self.assertEqual(row[header.index("joint_false_cut_count")], "0")

    def test_full_scheduler_can_pass_while_unsafe_prefix_is_not_authorized(self) -> None:
        rendered = self.render()
        header, row = [line.split("\t") for line in rendered.splitlines()[:2]]
        evidence = row[header.index("scheduler_domain_evidence")].split(";")
        self.assertTrue(any(record.startswith("2:2:") for record in evidence))
        self.assertFalse(any(record.startswith("1:1:") for record in evidence))

    def test_rejects_enabled_domain_without_holdout_candidates(self) -> None:
        training = report()
        reviewed = adoption()
        for source in list(training["groups"]):
            extra_group = copy.deepcopy(source)
            extra_group["phase"] = 4
            training["groups"].append(extra_group)
        for source in list(reviewed["entries"]):
            extra_entry = copy.deepcopy(source)
            extra_entry["phase"] = 4
            reviewed["entries"].append(extra_entry)
        with self.assertRaisesRegex(
            self.converter.ProfileConversionError, "unaudited exact profile domain"
        ):
            self.render(training=training, reviewed=reviewed)

    def test_all_enabled_domains_are_saved_when_each_passes(self) -> None:
        training = report()
        reviewed = adoption()
        reviewed["minimum_joint_cut_candidates"] = 1
        holdout = report(holdout=True)
        for source in list(training["groups"]):
            extra_group = copy.deepcopy(source)
            extra_group["phase"] = 4
            training["groups"].append(extra_group)
        for source in list(reviewed["entries"]):
            extra_entry = copy.deepcopy(source)
            extra_entry["phase"] = 4
            reviewed["entries"].append(extra_entry)
        extra_observations = copy.deepcopy(holdout["scheduler_observations"])
        for index, node in enumerate(extra_observations):
            node["node_id"] = f"holdout-phase-4-{index}"
            node["phase"] = 4
        holdout["scheduler_observations"].extend(extra_observations)
        rendered = self.render(training=training, reviewed=reviewed, holdout=holdout)
        header, row = [line.split("\t") for line in rendered.splitlines()[:2]]
        evidence = row[header.index("scheduler_domain_evidence")].split(";")
        full_domains = [record for record in evidence if record.startswith("2:2:")]
        single_domains = [record for record in evidence if record.startswith("1:1:")]
        self.assertEqual(len(full_domains), 2)
        self.assertEqual(len(single_domains), 2)

    def test_rejects_unobserved_adoption_domain(self) -> None:
        reviewed = adoption()
        reviewed["entries"][0]["maximum_empties"] = 21
        with self.assertRaisesRegex(self.converter.ProfileConversionError, "fully observed"):
            self.render(reviewed=reviewed)

    def test_selects_the_adopted_observed_bucket(self) -> None:
        training = report()
        for source in list(training["groups"]):
            other_bucket = copy.deepcopy(source)
            other_bucket["observed_domain"]["empties_bucket"] = {
                "minimum": 24,
                "maximum": 27,
                "observed_values": [24],
            }
            training["groups"].append(other_bucket)
        rendered = self.render(training=training)
        header, first = [line.split("\t") for line in rendered.splitlines()[:2]]
        self.assertEqual(first[header.index("minimum_empties")], "20")
        self.assertEqual(first[header.index("maximum_empties")], "20")

    def test_rejects_order_not_identical_to_collection(self) -> None:
        reviewed = adoption()
        reviewed["validated_pair_order"] = list(reversed(reviewed["validated_pair_order"]))
        with self.assertRaisesRegex(self.converter.ProfileConversionError, "collected pair order"):
            self.render(reviewed=reviewed)

    def test_rejects_duplicate_training_holdout_node(self) -> None:
        holdout = report(holdout=True)
        holdout["scheduler_observations"][0]["node_id"] = "training-node"
        with self.assertRaisesRegex(self.converter.ProfileConversionError, "duplicate sampled nodes"):
            self.render(holdout=holdout)

    def test_rejects_joint_false_cut_bound(self) -> None:
        holdout = report(holdout=True)
        holdout["scheduler_observations"] = [observation("false-cut", (2, 2), deep_score=0)]
        holdout["provenance_inventory"] = [provenance(2)]
        reviewed = adoption()
        reviewed["minimum_joint_cut_candidates"] = 1
        reviewed["maximum_joint_false_cut_rate_upper_bound"] = 0.99
        with self.assertRaisesRegex(self.converter.ProfileConversionError, "false-cut upper bound"):
            self.render(reviewed=reviewed, holdout=holdout)

    def test_cli_conversion_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report_path = root / "report.json"
            adoption_path = root / "adoption.json"
            holdout_path = root / "holdout.json"
            report_path.write_text(json.dumps(report(), sort_keys=True), encoding="utf-8")
            adoption_path.write_text(json.dumps(adoption(), sort_keys=True), encoding="utf-8")
            holdout_path.write_text(
                json.dumps(report(holdout=True), sort_keys=True), encoding="utf-8"
            )
            first = root / "first.tsv"
            second = root / "second.tsv"
            self.converter.convert(report_path, adoption_path, holdout_path, first)
            self.converter.convert(report_path, adoption_path, holdout_path, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_json_conversion_is_compact_and_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report_path = root / "report.json"
            adoption_path = root / "adoption.json"
            holdout_path = root / "holdout.json"
            report_path.write_text(json.dumps(report(), sort_keys=True), encoding="utf-8")
            adoption_path.write_text(json.dumps(adoption(), sort_keys=True), encoding="utf-8")
            holdout_path.write_text(json.dumps(report(holdout=True), sort_keys=True), encoding="utf-8")
            first = root / "first.json"
            second = root / "second.json"
            self.converter.convert(report_path, adoption_path, holdout_path, first, "json")
            self.converter.convert(report_path, adoption_path, holdout_path, second, "json")
            self.assertEqual(first.read_bytes(), second.read_bytes())

            payload = json.loads(first.read_text(encoding="utf-8"))
            self.assertEqual(payload["schema_version"], 3)
            self.assertEqual(
                payload["validated_pair_order"],
                [
                    {"deep_depth": 8, "shallow_depth": 3},
                    {"deep_depth": 8, "shallow_depth": 4},
                ],
            )
            self.assertEqual(len(payload["scheduler_domain_evidence"]), 1)
            self.assertEqual(len(payload["entries"]), 2)
            self.assertNotIn("scheduler_domain_evidence", payload["entries"][0])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--converter", type=Path, required=True)
    args = parser.parse_args()
    ConverterTests.converter_path = args.converter
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ConverterTests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
