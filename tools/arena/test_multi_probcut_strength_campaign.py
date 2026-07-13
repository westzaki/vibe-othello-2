#!/usr/bin/env python3
"""Unit coverage for Multi-ProbCut strength-campaign evidence gates."""

from __future__ import annotations

import argparse
import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


def load_runner(path: Path):
    spec = importlib.util.spec_from_file_location("multi_probcut_campaign", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load campaign runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def profile_text() -> str:
    header = (
        "schema_version\tprofile_id\tsource_checksum_sha256\tjoint_holdout_checksum_sha256\t"
        "evaluator_family\tartifact_family\tnode_class\tvalidated_maximum_probes_per_node\t"
        "joint_false_cut_count\tjoint_cut_candidate_count\tjoint_false_cut_rate_upper_bound\t"
        "phase\tsearch_mode\tminimum_empties\tmaximum_empties\tdeep_depth\tshallow_depth\t"
        "exact_handoff_enabled\texact_handoff_threshold\tminimum_exact_handoff_distance\t"
        "maximum_exact_handoff_distance\tregression_slope\tintercept\tresidual_sigma\t"
        "confidence_multiplier\tminimum_shallow_score\tmaximum_shallow_score\tminimum_beta\tmaximum_beta"
    )
    identity = (
        "2\tfixture-v2\t" + "1" * 64 + "\t" + "2" * 64
        + "\tfixture-eval\tfixture-artifact\tnon_pv_scout_beta_only\t2\t0\t100\t0.04"
    )
    domain = "\t3\tmove\t20\t20\t8\t{}\tfalse\t0\t0\t0\t1\t0\t1\t3\t-100\t100\t-80\t80"
    return "\n".join((header, identity + domain.format(3), identity + domain.format(4))) + "\n"


class CampaignTests(unittest.TestCase):
    runner_path: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.runner = load_runner(cls.runner_path)

    def test_matrix_contains_multi_vs_single_and_shadow_swaps(self) -> None:
        self.assertEqual(self.runner.COMPARISONS["multi_forward"], ("multi", "single"))
        self.assertEqual(self.runner.COMPARISONS["multi_reverse"], ("single", "multi"))
        self.assertEqual(self.runner.COMPARISONS["shadow_multi_forward"], ("shadow", "off"))
        self.assertEqual(self.runner.COMPARISONS["shadow_multi_reverse"], ("off", "shadow"))

    def test_profile_identity_loads_reviewed_scheduler_evidence_and_order(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.tsv"
            path.write_text(profile_text(), encoding="utf-8")
            identity = self.runner.profile_identity(path)
        self.assertEqual(identity["validated_maximum_probes_per_node"], 2)
        self.assertEqual(identity["joint_cut_candidate_count"], 100)
        self.assertEqual(
            identity["validated_pair_order"],
            [
                {"deep_depth": 8, "shallow_depth": 3},
                {"deep_depth": 8, "shallow_depth": 4},
            ],
        )

    def test_reverse_summary_uses_baseline_as_tested_policy(self) -> None:
        report = {
            "results": {
                "paired_score": {
                    "point_estimate": 0.4,
                    "lower_95": 0.3,
                    "upper_95": 0.5,
                    "opening_pair_count": 100,
                    "game_count": 200,
                }
            },
            "telemetry": {
                "candidate": {
                    "overall": {
                        "completed_depth_percentiles": {"p50": 8},
                        "nodes_per_sec": 10,
                        "probcut": {"by_phase_depth_pair": []},
                    }
                },
                "baseline": {
                    "overall": {
                        "completed_depth_percentiles": {"p50": 9},
                        "nodes_per_sec": 11,
                        "probcut": {"by_phase_depth_pair": []},
                    }
                },
            },
        }
        summary = self.runner.stage_summary(report, "multi_reverse")
        self.assertAlmostEqual(summary["tested_policy_score_rate"], 0.6)
        self.assertEqual(summary["completed_depth_p50_delta"], 1)

    def test_later_pair_and_shadow_telemetry_are_aggregated_by_pair(self) -> None:
        stages = [
            {
                "summary": {
                    "tested_probcut": {
                        "by_phase_depth_pair": [
                            {
                                "phase": 3,
                                "deep_depth": 8,
                                "shallow_depth": 4,
                                "attempts": 17,
                                "successes": 3,
                                "shadow_candidates": 2,
                            }
                        ]
                    }
                }
            },
            {
                "summary": {
                    "tested_probcut": {
                        "by_phase_depth_pair": [
                            {
                                "phase": 4,
                                "deep_depth": 8,
                                "shallow_depth": 4,
                                "attempts": 13,
                                "successes": 1,
                                "shadow_candidates": 4,
                            }
                        ]
                    }
                }
            },
        ]
        pair = [{"deep_depth": 8, "shallow_depth": 4}]
        self.assertEqual(self.runner.pair_telemetry_total(stages, pair, "attempts"), 30)
        self.assertEqual(self.runner.pair_telemetry_total(stages, pair, "successes"), 4)
        self.assertGreater(self.runner.wilson_upper_bound(0, 100), 0.0)

    def test_report_validation_binds_limits_openings_bootstrap_artifact_and_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile_path = root / "profile.tsv"
            profile_path.write_text(profile_text(), encoding="utf-8")
            profile = self.runner.profile_identity(profile_path)
            manifest = root / "artifact.json"
            manifest.write_text(
                json.dumps(
                    {
                        "artifact_id": "fixture-artifact",
                        "pattern_set_id": "fixture-eval",
                    }
                ),
                encoding="utf-8",
            )
            weights = root / "weights.bin"
            weights.write_bytes(b"fixture-weights")
            openings = root / "openings.txt"
            openings.write_text("fixture openings\n", encoding="utf-8")
            args = argparse.Namespace(
                search_preset="full",
                fixed_depth=8,
                fixed_nodes=100_000,
                persistent_session=True,
                tt_bytes=1024,
                bootstrap_seed=9,
                bootstrap_samples=10_000,
                minimum_opening_pairs=100,
                opening_limit=100,
                artifact_manifest=manifest,
                artifact_weights=weights,
                maximum_probes=2,
                minimum_confidence=0.0,
                minimum_margin=0,
                maximum_margin=10,
                maximum_shallow_overhead_ratio=0.25,
            )
            enabled = {
                "enabled": True,
                "profile_id": profile["profile_id"],
                "source_checksum_sha256": profile["source_checksum_sha256"],
                "joint_holdout_checksum_sha256": profile[
                    "joint_holdout_checksum_sha256"
                ],
                "validated_maximum_probes_per_node": 2,
                "joint_false_cut_count": 0,
                "joint_cut_candidate_count": 100,
                "joint_false_cut_rate_upper_bound": 0.04,
                "evaluator_family": "fixture-eval",
                "artifact_family": "fixture-artifact",
                "ordered_depth_pairs": profile["validated_pair_order"],
                "maximum_probes_per_node": 2,
                "stop_after_first_success": True,
                "minimum_confidence": 0.0,
                "minimum_margin": 0,
                "maximum_margin": 10,
                "maximum_shallow_overhead_ratio": 0.25,
                "near_exact_disable_empties": 8,
                "shadow_verify": False,
            }
            manifest_checksum = self.runner.fnv1a64_file(manifest)
            weights_checksum = self.runner.fnv1a64_file(weights)
            artifact = {
                "artifact_id": "fixture-artifact",
                "pattern_set_id": "fixture-eval",
                "runtime_identity_checksum": "runtime-fixture",
                "manifest_content_checksum": manifest_checksum,
                "weights_file_checksum": weights_checksum,
            }
            report = {
                "schema_version": 4,
                "arena_version": "full-game-artifact-arena-v4",
                "seed": 7,
                "search_config": {
                    "candidate_probcut_mode": "multi",
                    "baseline_probcut_mode": "off",
                    "preset": "full",
                    "limit_mode": "fixed_wall_time",
                    "depth": 0,
                    "nodes": 0,
                    "time_ms": 500,
                    "exact_endgame_empties": 8,
                    "persistent_session": True,
                    "tt_requested_bytes": 1024,
                    "candidate_resolved_options": {"multi_probcut": enabled},
                    "baseline_resolved_options": {
                        "multi_probcut": {"enabled": False}
                    },
                },
                "failed_games": 0,
                "illegal_games": 0,
                "games": 200,
                "opening_count": 100,
                "input_opening_count": 100,
                "opening_limit": 100,
                "opening_source_checksum": self.runner.fnv1a64_file(openings),
                "results": {
                    "paired_score": {
                        "bootstrap_seed": 9,
                        "bootstrap_samples": 10_000,
                        "opening_pair_count": 100,
                        "game_count": 200,
                    }
                },
                "paired_sanity": {"incomplete_pairs": 0},
                "strength_gate": {
                    "eligible": True,
                    "minimum_opening_pairs": 100,
                    "reasons": [],
                },
                "candidate": artifact,
                "baseline": dict(artifact),
                "inputs": {
                    "candidate_manifest_checksum": manifest_checksum,
                    "baseline_manifest_checksum": manifest_checksum,
                    "candidate_weights_checksum": weights_checksum,
                    "baseline_weights_checksum": weights_checksum,
                },
            }
            self.runner.validate_report(
                report,
                root / "report.json",
                args,
                profile,
                "multi",
                "off",
                "fixed-time-500ms",
                8,
                7,
                openings,
            )

            mutations = (
                ("requested limit", lambda value: value["search_config"].update(time_ms=100)),
                (
                    "bootstrap",
                    lambda value: value["results"]["paired_score"].update(
                        bootstrap_samples=999
                    ),
                ),
                ("opening", lambda value: value.update(opening_source_checksum="wrong")),
                (
                    "artifact",
                    lambda value: value["baseline"].update(
                        runtime_identity_checksum="different"
                    ),
                ),
                (
                    "strength gate",
                    lambda value: value["strength_gate"].update(eligible=False),
                ),
            )
            for label, mutate in mutations:
                invalid = copy.deepcopy(report)
                mutate(invalid)
                with self.subTest(label=label), self.assertRaises(self.runner.CampaignError):
                    self.runner.validate_report(
                        invalid,
                        root / "report.json",
                        args,
                        profile,
                        "multi",
                        "off",
                        "fixed-time-500ms",
                        8,
                        7,
                        openings,
                    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    args = parser.parse_args()
    CampaignTests.runner_path = args.runner
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(CampaignTests)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
