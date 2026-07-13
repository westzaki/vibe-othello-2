#!/usr/bin/env python3
"""CTest smoke coverage for the persistent full-game artifact arena."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


FORCED_PASS_MOVES = (
    "e6 d6 c5 f6 c4 c3 d3 b4 c6 b3 b2 a1 f5 c7 a3 g5 b7 a2 c1 "
    "a7 a8 e2 d2 e1 e7 d7 h5 e8 a6 b5 c2 f4 f2 f1 g7 h4 d8 h8 "
    "c8 d1 f3 g2 h1 a4 g6 b1 g8 h6 g3 g4 f8 h2 h3 f7 e3 b8 h7 b6 a5"
)


def make_tiny_artifact(weights_path: Path, manifest_path: Path) -> None:
    pattern_set_id = "fixed-pattern-fixture-v1"
    phase_count = 13
    weight_count = (1 + 3**8 + 3**9) * phase_count
    payload = bytearray(b"VOPWGT\0\0")
    payload.extend(
        struct.pack(
            "<HHHHHHHHI",
            1,
            1,
            1,
            1,
            phase_count,
            2,
            len(pattern_set_id),
            0,
            weight_count,
        )
    )
    payload.extend(pattern_set_id.encode("utf-8"))
    payload.extend(b"\0" * (weight_count * 4))
    checksum = f"0x{zlib.crc32(payload) & 0xFFFFFFFF:08x}"
    payload.extend(struct.pack("<I", int(checksum, 16)))
    weights_path.write_bytes(payload)
    manifest_path.write_text(
        json.dumps(
            {
                "format_version": 1,
                "bit_order": "a1-lsb",
                "score_unit": "disc-diff",
                "score_scale": 1,
                "phase_count": phase_count,
                "trained_phases": [10, 11, 12],
                "pattern_set_id": pattern_set_id,
                "weights_file": weights_path.name,
                "weights_checksum": checksum,
                "notes": "temporary full-game arena smoke artifact",
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, capture_output=True, text=True)


def run_arena(
    exe: str, temp_dir: Path, report_name: str, extra_args: tuple[str, ...] = ()
) -> dict[str, object]:
    command = [
        exe,
        "--candidate-manifest",
        str(temp_dir / "candidate.manifest.json"),
        "--candidate-name",
        "smoke-candidate",
        "--baseline-manifest",
        str(temp_dir / "baseline.manifest.json"),
        "--baseline-name",
        "smoke-baseline",
        "--openings",
        str(temp_dir / "openings.txt"),
        "--report-out",
        str(temp_dir / report_name),
        "--depth",
        "1",
        "--search-preset",
        "full",
        "--seed",
        "7",
        *extra_args,
    ]
    completed = run(command)
    if completed.returncode != 0:
        raise AssertionError(
            f"{command} exited {completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    report_path = temp_dir / report_name
    if not report_path.exists():
        raise AssertionError("full-game arena did not produce a report")
    return json.loads(report_path.read_text(encoding="utf-8"))


def assert_report(report: dict[str, object]) -> None:
    expected = {
        "candidate",
        "baseline",
        "search_config",
        "input_opening_count",
        "opening_count",
        "selected_openings",
        "games",
        "game_records",
        "results",
        "failed_games",
        "illegal_games",
        "elapsed_sec",
        "same_artifact_sanity",
        "report_checksum",
        "non_claim_notes",
        "inputs",
        "telemetry",
        "paired_sanity",
        "selected_openings_checksum",
        "strength_gate",
    }
    missing = sorted(expected - set(report))
    if missing:
        raise AssertionError(f"report missing keys: {missing}")
    if report["candidate"]["evaluator_policy"] != "phase-aware-covered-phases":
        raise AssertionError(f"candidate did not use phase-aware evaluator: {report['candidate']!r}")
    if report["baseline"]["evaluator_policy"] != "phase-aware-covered-phases":
        raise AssertionError(f"baseline did not use phase-aware evaluator: {report['baseline']!r}")
    if report["search_config"]["entrypoint"] != "search_iterative":
        raise AssertionError(f"unexpected search entrypoint: {report['search_config']!r}")
    if report["search_config"]["limit_scope"] != "per-move":
        raise AssertionError(f"unexpected limit scope: {report['search_config']!r}")
    if report["search_config"]["resolved_options"]["use_pvs"] is not True:
        raise AssertionError(f"full preset did not enable PVS: {report['search_config']!r}")
    if report["input_opening_count"] != 4 or report["opening_count"] != 4:
        raise AssertionError(f"unexpected opening counts: {report!r}")
    if report["games"] != 8 or len(report["game_records"]) != 8:
        raise AssertionError(f"unexpected paired game count: {report!r}")
    overall = report["results"]["overall"]
    if overall["candidate_wins"] + overall["candidate_losses"] + overall["draws"] != 8:
        raise AssertionError(f"W/L/D does not sum to games: {overall!r}")
    if overall["passes_after_opening"] <= 0:
        raise AssertionError(f"forced-pass opening did not exercise pass handling: {overall!r}")
    if report["failed_games"] != 0 or report["illegal_games"] != 0:
        raise AssertionError(f"unexpected failed or illegal games: {report!r}")
    side_results = report["results"]["by_side_assignment"]
    if set(side_results) != {"candidate_black", "candidate_white"}:
        raise AssertionError(f"missing color-swap buckets: {side_results!r}")
    if side_results["candidate_black"]["games"] != 4 or side_results["candidate_white"]["games"] != 4:
        raise AssertionError(f"unpaired color swap: {side_results!r}")
    if len(report["results"]["by_opening"]) != 4:
        raise AssertionError(f"opening breakdown missing: {report['results']['by_opening']!r}")
    sanity = report["same_artifact_sanity"]
    if sanity != {
        "same_runtime_artifact": True,
        "same_search_configuration": True,
        "applicable": True,
        "paired_color_swap": True,
        "neutral": True,
    }:
        raise AssertionError(f"same-artifact sanity failed: {sanity!r}")
    if overall["candidate_score_rate"] != 0.5 or overall["average_disc_diff_candidate_perspective"] != 0.0:
        raise AssertionError(f"same artifact was not neutral: {overall!r}")
    if report["schema_version"] != 4 or report["arena_version"] != "full-game-artifact-arena-v4":
        raise AssertionError(f"unexpected v4 schema: {report!r}")
    if report["search_config"]["limit_mode"] != "fixed_depth":
        raise AssertionError(f"fixed-depth mode was not recorded: {report['search_config']!r}")
    paired = report["results"].get("paired_score")
    if not isinstance(paired, dict) or paired.get("method") != "deterministic-cluster-bootstrap-opening-pair":
        raise AssertionError(f"paired bootstrap missing: {paired!r}")
    if paired.get("opening_pair_count") != 4 or paired.get("game_count") != 8:
        raise AssertionError(f"wrong pair accounting: {paired!r}")
    if paired.get("descriptive_only") is not False:
        raise AssertionError(f"valid paired result was marked descriptive-only: {paired!r}")
    paired_sanity = report["paired_sanity"]
    if paired_sanity.get("paired_color_swap_complete") is not True or paired_sanity.get(
        "same_artifact_neutral"
    ) is not True:
        raise AssertionError(f"v4 paired sanity failed: {paired_sanity!r}")
    telemetry = report["telemetry"]
    for role in ("candidate", "baseline"):
        role_report = telemetry.get(role)
        if not isinstance(role_report, dict) or role_report["overall"]["search_calls"] <= 0:
            raise AssertionError(f"missing {role} telemetry: {role_report!r}")
        overall_telemetry = role_report["overall"]
        if overall_telemetry["elapsed_ns"] <= 0 or overall_telemetry["nodes_per_sec"] is None:
            raise AssertionError(f"missing high-resolution {role} timing: {overall_telemetry!r}")
        if "engine_elapsed_ms" not in overall_telemetry or "timer_accounting_delta_ns" not in overall_telemetry:
            raise AssertionError(f"missing {role} timer accounting: {overall_telemetry!r}")
        for field in (
            "incremental_eval_enabled",
            "incremental_eval_enabled_searches",
            "incremental_state_initializations",
            "incremental_eval_calls",
            "stateless_eval_calls",
            "incremental_updates",
            "incremental_touched_instances",
        ):
            if field not in overall_telemetry:
                raise AssertionError(f"missing {role} backend telemetry {field}: {overall_telemetry!r}")
        if not role_report["by_phase"] or not role_report["by_side_to_move"]:
            raise AssertionError(f"missing {role} telemetry buckets: {role_report!r}")
        if not isinstance(overall_telemetry.get("probcut"), dict):
            raise AssertionError(f"missing {role} ProbCut telemetry: {overall_telemetry!r}")
    if any(not game.get("search_calls") for game in report["game_records"]):
        raise AssertionError("game record lacks per-search telemetry")
    first_search = report["game_records"][0]["search_calls"][0]
    for field in (
        "elapsed_ns",
        "elapsed_ms",
        "engine_elapsed_ms",
        "timer_accounting_delta_ns",
        "exact_handoff_used",
        "incremental_eval_enabled",
        "incremental_state_initializations",
        "incremental_eval_calls",
        "stateless_eval_calls",
        "incremental_updates",
        "incremental_touched_instances",
        "probcut",
    ):
        if field not in first_search:
            raise AssertionError(f"per-search telemetry lacks {field}: {first_search!r}")
    gate = report["strength_gate"]
    if gate != {"eligible": True, "minimum_opening_pairs": 1, "reasons": []}:
        raise AssertionError(f"valid smoke run was not gate eligible: {gate!r}")
    repository = report["inputs"].get("repository")
    if not isinstance(repository, dict) or "configure_time_dirty" not in repository:
        raise AssertionError(f"repository fingerprint lacks dirty state: {repository!r}")


def assert_sanity_runner(exe: str, temp_dir: Path) -> None:
    helper = Path(__file__).with_name("run_full_game_artifact_arena_sanity.py")
    output_dir = temp_dir / "sanity"
    command = [
        sys.executable,
        str(helper),
        "--exe",
        exe,
        "--candidate-manifest",
        str(temp_dir / "candidate.manifest.json"),
        "--candidate-weights",
        str(temp_dir / "candidate.weights.bin"),
        "--baseline-manifest",
        str(temp_dir / "baseline.manifest.json"),
        "--baseline-weights",
        str(temp_dir / "baseline.weights.bin"),
        "--openings",
        str(temp_dir / "openings.txt"),
        "--output-dir",
        str(output_dir),
        "--limit-mode",
        "depth",
        "--depth",
        "1",
        "--opening-limit",
        "4",
    ]
    completed = run(command)
    if completed.returncode != 0:
        raise AssertionError(f"sanity runner failed:\n{completed.stdout}\n{completed.stderr}")
    summary = json.loads((output_dir / "sanity-summary.json").read_text(encoding="utf-8"))
    if not all(summary.get(key) is True for key in ("color_swap_passed", "same_artifact_passed", "argument_order_passed")):
        raise AssertionError(f"sanity summary failed: {summary!r}")


def assert_disabled_tt_without_persistence(exe: str, temp_dir: Path) -> None:
    report = run_arena(
        exe,
        temp_dir,
        "tt-disabled.json",
        ("--tt-bytes", "0", "--opening-limit", "1"),
    )
    config = report["search_config"]
    if config["persistent_session"] is not False:
        raise AssertionError(f"TT-off smoke unexpectedly retained sessions: {config!r}")
    if config["tt_requested_bytes"] != 0 or config["tt_actual_bytes"] != 0:
        raise AssertionError(f"TT-off allocation was not reported accurately: {config!r}")
    if config["tt_enabled"] is not False or config["tt_allocation_succeeded"] is not True:
        raise AssertionError(f"TT-off allocation state was not reported accurately: {config!r}")
    calls = [
        call
        for game in report["game_records"]
        for call in game.get("search_calls", [])
    ]
    if not calls:
        raise AssertionError("TT-off smoke produced no search calls")
    if any(call["tt_probes"] != 0 or call["tt_stores"] != 0 for call in calls):
        raise AssertionError("--tt-bytes 0 used TT while persistent sessions were disabled")


def assert_exact_guard(exe: str, temp_dir: Path) -> None:
    command = [
        exe,
        "--candidate-manifest",
        str(temp_dir / "candidate.manifest.json"),
        "--baseline-manifest",
        str(temp_dir / "baseline.manifest.json"),
        "--openings",
        str(temp_dir / "openings.txt"),
        "--report-out",
        str(temp_dir / "bad.json"),
        "--depth",
        "1",
        "--exact-endgame-empties",
        "8",
    ]
    completed = run(command)
    if completed.returncode == 0:
        raise AssertionError("depth-only exact endgame request unexpectedly succeeded")
    if "requires --nodes or --time-ms" not in completed.stderr:
        raise AssertionError(f"exact guard error missing:\n{completed.stderr}")


def assert_probcut_profile_v3_loader(exe: str, temp_dir: Path) -> None:
    header = (
        "schema_version\tprofile_id\tsource_checksum_sha256\tjoint_holdout_checksum_sha256\t"
        "evaluator_family\tartifact_family\tnode_class\tvalidated_maximum_probes_per_node\t"
        "joint_false_cut_count\tjoint_cut_candidate_count\tjoint_false_cut_rate_upper_bound\t"
        "scheduler_domain_evidence\t"
        "phase\tsearch_mode\tminimum_empties\tmaximum_empties\tdeep_depth\tshallow_depth\t"
        "exact_handoff_enabled\texact_handoff_threshold\tminimum_exact_handoff_distance\t"
        "maximum_exact_handoff_distance\tregression_slope\tintercept\tresidual_sigma\t"
        "confidence_multiplier\tminimum_shallow_score\tmaximum_shallow_score\tminimum_beta\tmaximum_beta"
    )
    evidence = ";".join(
        f"2:2:{phase}:move:0:60:3:false:0:0:0:100:0:100:0.05"
        for phase in range(13)
    )
    identity = (
        "3\tsynthetic-loader-fixture\t" + "0" * 64 + "\t" + "1" * 64
        + "\tfixed-pattern-fixture-v1\tcandidate.manifest\tnon_pv_scout_beta_only\t2\t0\t100\t0.05\t"
        + evidence
    )
    rows = [header]
    for shallow_depth in (1, 2):
        for phase in range(13):
            rows.append(
                identity
                + f"\t{phase}\tmove\t0\t60\t3\t{shallow_depth}\tfalse\t0\t0\t0"
                "\t1\t100\t1\t1\t-200\t200\t-200\t200"
            )
    profile = temp_dir / "synthetic-probcut-profile.tsv"
    profile.write_text("\n".join(rows) + "\n", encoding="utf-8")
    report_path = temp_dir / "probcut-profile-v3.json"
    command = [
        exe,
        "--candidate-manifest",
        str(temp_dir / "candidate.manifest.json"),
        "--baseline-manifest",
        str(temp_dir / "candidate.manifest.json"),
        "--openings",
        str(temp_dir / "openings.txt"),
        "--opening-limit",
        "1",
        "--report-out",
        str(report_path),
        "--limit-mode",
        "depth",
        "--depth",
        "4",
        "--candidate-probcut",
        "multi",
        "--baseline-probcut",
        "off",
        "--probcut-profile",
        str(profile),
        "--probcut-maximum-margin",
        "10",
        "--probcut-maximum-probes",
        "2",
    ]
    completed = run(command)
    if completed.returncode != 0:
        raise AssertionError(
            f"profile-v3 loader failed:\n{completed.stdout}\n{completed.stderr}"
        )
    report = json.loads(report_path.read_text(encoding="utf-8"))
    resolved = report["search_config"]["candidate_resolved_options"]["multi_probcut"]
    if (
        resolved.get("enabled") is not True
        or resolved.get("joint_holdout_checksum_sha256") != "1" * 64
        or resolved.get("validated_maximum_probes_per_node") != 2
        or resolved.get("ordered_depth_pairs")
        != [
            {"deep_depth": 3, "shallow_depth": 1},
            {"deep_depth": 3, "shallow_depth": 2},
        ]
    ):
        raise AssertionError(f"profile-v3 evidence/order was not resolved: {resolved!r}")


def assert_explicit_limit_modes(exe: str, temp_dir: Path) -> None:
    base = [
        exe,
        "--candidate-manifest",
        str(temp_dir / "candidate.manifest.json"),
        "--baseline-manifest",
        str(temp_dir / "baseline.manifest.json"),
        "--openings",
        str(temp_dir / "openings.txt"),
        "--opening-limit",
        "1",
        "--search-preset",
        "basic",
    ]
    cases = (
        ("depth", "--depth", "1", "fixed_depth", False),
        ("nodes", "--nodes", "1", "fixed_nodes", True),
        ("time", "--time-ms", "1", "fixed_wall_time", True),
    )
    for mode, flag, value, expected_mode, expected_infinite in cases:
        report_path = temp_dir / f"{mode}.json"
        completed = run(
            [
                *base,
                "--report-out",
                str(report_path),
                "--limit-mode",
                mode,
                flag,
                value,
            ]
        )
        if completed.returncode != 0:
            raise AssertionError(f"{mode} mode failed:\n{completed.stdout}\n{completed.stderr}")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        config = report["search_config"]
        if config["limit_mode"] != expected_mode or config["infinite"] is not expected_infinite:
            raise AssertionError(f"incorrect {mode} mode config: {config!r}")
        if mode == "nodes":
            gate = report["strength_gate"]
            paired = report["results"]["paired_score"]
            if gate["eligible"] is not False or "failed_games_nonzero" not in gate["reasons"]:
                raise AssertionError(f"failed node-limited run remained gate eligible: {gate!r}")
            if paired["descriptive_only"] is not True:
                raise AssertionError(f"invalid run CI was not descriptive-only: {paired!r}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    args = parser.parse_args(argv)
    with tempfile.TemporaryDirectory() as temp:
        temp_dir = Path(temp)
        make_tiny_artifact(temp_dir / "candidate.weights.bin", temp_dir / "candidate.manifest.json")
        make_tiny_artifact(temp_dir / "baseline.weights.bin", temp_dir / "baseline.manifest.json")
        (temp_dir / "openings.txt").write_text(
            "# empty start, shorthand, id form, and a position whose continuation includes a pass\n"
            "start:\n"
            "f5 d6:\n"
            "named: f5 d6\n"
            f"forced-pass: {FORCED_PASS_MOVES}\n",
            encoding="utf-8",
        )
        first = run_arena(args.exe, temp_dir, "report-a.json")
        second = run_arena(args.exe, temp_dir, "report-b.json")
        assert_report(first)
        assert_report(second)
        if first["report_checksum"] != second["report_checksum"]:
            raise AssertionError(
                f"deterministic report checksum changed: {first['report_checksum']!r}, "
                f"{second['report_checksum']!r}"
            )
        assert_exact_guard(args.exe, temp_dir)
        assert_probcut_profile_v3_loader(args.exe, temp_dir)
        assert_explicit_limit_modes(args.exe, temp_dir)
        assert_disabled_tt_without_persistence(args.exe, temp_dir)
        assert_sanity_runner(args.exe, temp_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
