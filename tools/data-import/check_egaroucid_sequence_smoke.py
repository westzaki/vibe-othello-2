#!/usr/bin/env python3
"""CTest wrapper for the Egaroucid sequence transcript importer."""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import subprocess
import sys
import tempfile
import zipfile
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from typing import Any


DATASET_ID = "egaroucid-sequence-v0002-local"
EXPECTED_HEADER = [
    "record_id",
    "position_id",
    "game_group_id",
    "board_id",
    "source_occurrence_id",
    "source_dataset_id",
    "split",
    "board_a1_to_h8",
    "label_kind",
    "label_unit",
    "label_perspective",
    "label_score_side_to_move",
    "occupied_count",
    "phase",
    "player_disc_count",
    "opponent_disc_count",
    "empty_count",
]


def run_importer(
    importer: Path,
    fixture: Path,
    manifest: Path,
    report: Path,
    seed: int = 7,
    extra_args: list[str] | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(importer),
            "--input",
            str(fixture),
            "--manifest",
            str(manifest),
            "--report",
            str(report),
            "--min-ply",
            "4",
            "--max-ply",
            "12",
            "--ply-stride",
            "4",
            "--seed",
            str(seed),
            "--emit-terminal",
            *(extra_args or []),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def parse_rows(text: str) -> list[dict[str, str]] | None:
    reader = csv.DictReader(StringIO(text), delimiter="\t")
    if reader.fieldnames != EXPECTED_HEADER:
        print(f"unexpected TSV header: {reader.fieldnames}", file=sys.stderr)
        return None
    rows = list(reader)
    if not rows:
        print("expected emitted rows", file=sys.stderr)
        return None
    return rows


def load_report(path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"could not read report: {error}", file=sys.stderr)
        return None
    if not isinstance(data, dict):
        print("report root must be object", file=sys.stderr)
        return None
    return data


def check_rows(rows: list[dict[str, str]]) -> bool:
    if len(rows) != 9:
        print(f"unexpected emitted row count: {len(rows)}", file=sys.stderr)
        return False
    labels = {int(row["label_score_side_to_move"]) for row in rows}
    if len(labels) < 2:
        print(f"expected multiple final-disc-diff labels, got {labels!r}", file=sys.stderr)
        return False
    source_ids = {row["source_dataset_id"] for row in rows}
    if source_ids != {DATASET_ID}:
        print(f"unexpected source ids: {source_ids}", file=sys.stderr)
        return False
    if {row["label_kind"] for row in rows} != {"observed_final_disc_diff"}:
        print("unexpected label kind", file=sys.stderr)
        return False
    if {row["label_unit"] for row in rows} != {"final_disc_diff"}:
        print("unexpected label unit", file=sys.stderr)
        return False
    if {row["label_perspective"] for row in rows} != {"side_to_move"}:
        print("unexpected label perspective", file=sys.stderr)
        return False
    splits_by_game_group_id: dict[str, set[str]] = {}
    for row in rows:
        board = row["board_a1_to_h8"]
        if len(board) != 64 or set(board).difference({"X", "O", "-"}):
            print(f"bad board: {board!r}", file=sys.stderr)
            return False
        occupied = board.count("X") + board.count("O")
        if int(row["occupied_count"]) != occupied:
            print(f"bad occupied count: {row}", file=sys.stderr)
            return False
        if int(row["player_disc_count"]) != board.count("X"):
            print(f"bad player count: {row}", file=sys.stderr)
            return False
        if int(row["opponent_disc_count"]) != board.count("O"):
            print(f"bad opponent count: {row}", file=sys.stderr)
            return False
        if int(row["empty_count"]) != board.count("-"):
            print(f"bad empty count: {row}", file=sys.stderr)
            return False
        if int(row["label_score_side_to_move"]) < -64 or int(row["label_score_side_to_move"]) > 64:
            print(f"unexpected label: {row}", file=sys.stderr)
            return False
        if not row["game_group_id"].startswith("game-"):
            print(f"unexpected game group id: {row['game_group_id']!r}", file=sys.stderr)
            return False
        if not row["board_id"].startswith("board-"):
            print(f"unexpected board id: {row['board_id']!r}", file=sys.stderr)
            return False
        if not row["source_occurrence_id"].startswith("occ-"):
            print(f"unexpected source occurrence id: {row['source_occurrence_id']!r}", file=sys.stderr)
            return False
        prefix = f"{DATASET_ID}-game-"
        if not row["position_id"].startswith(prefix):
            print(f"unexpected position id: {row['position_id']!r}", file=sys.stderr)
            return False
        splits_by_game_group_id.setdefault(row["game_group_id"], set()).add(row["split"])
    if any(len(splits) != 1 for splits in splits_by_game_group_id.values()):
        print("positions from one game did not stay in one split", file=sys.stderr)
        return False
    return True


def check_report(report: dict[str, Any], rows: list[dict[str, str]]) -> bool:
    expected = {
        "schema_version": 2,
        "importer_version": "egaroucid-sequence-v1",
        "identity_policy_version": "egaroucid-sequence-identity-v1",
        "source_dataset_id": DATASET_ID,
        "source_kind": "egaroucid-sequence-local",
        "input_kind": "sequence-transcript",
        "input_games": 4,
        "accepted_games": 3,
        "rejected_games": 1,
        "emitted_positions": len(rows),
        "invalid_move_count": 1,
        "terminal_count": 3,
        "game_group_count": 2,
        "duplicate_game_occurrence_count": 1,
    }
    for key, value in expected.items():
        if report.get(key) != value:
            print(f"report mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False
    if not isinstance(report.get("checksum"), str) or not report["checksum"].startswith("sha256:"):
        print(f"bad checksum: {report.get('checksum')!r}", file=sys.stderr)
        return False
    if not isinstance(report.get("pass_count"), int) or report["pass_count"] < 2:
        print(f"bad pass count: {report.get('pass_count')!r}", file=sys.stderr)
        return False
    if not isinstance(report.get("unique_board_count"), int) or report["unique_board_count"] <= 0:
        print(f"bad unique board count: {report.get('unique_board_count')!r}", file=sys.stderr)
        return False
    if not isinstance(report.get("cross_split_board_collision_count"), int):
        print("missing cross split collision count", file=sys.stderr)
        return False
    for key in ("board_leakage_audit_before", "board_leakage_audit_after"):
        audit = report.get(key)
        if not isinstance(audit, dict) or not isinstance(
            audit.get("cross_split_board_collision_count"), int
        ):
            print(f"missing board audit: {key}={audit!r}", file=sys.stderr)
            return False
    for key in ("game_group_leakage_audit_before", "game_group_leakage_audit_after"):
        audit = report.get(key)
        if not isinstance(audit, dict) or not isinstance(
            audit.get("cross_split_game_group_collision_count"), int
        ):
            print(f"missing game group audit: {key}={audit!r}", file=sys.stderr)
            return False
    split_policy = report.get("split_policy")
    if not isinstance(split_policy, dict) or split_policy.get("measurement_split_policy") != "game-group":
        print(f"unexpected split policy: {split_policy!r}", file=sys.stderr)
        return False
    expected_sampling = {
        "sampling_mode": "full-scan-topk",
        "file_order": "path",
        "max_files": None,
        "max_games": None,
        "file_sample_rate": None,
        "game_sample_rate": None,
        "source_files_seen": 1,
        "source_files_processed": 1,
        "games_seen": 4,
        "games_replayed": 4,
        "replay_skip_count": 0,
        "sampling_frame_notes": [],
    }
    for key, value in expected_sampling.items():
        if report.get(key) != value:
            print(f"sampling report mismatch for {key}: {report.get(key)!r}", file=sys.stderr)
            return False
    notes = report.get("notes")
    if not isinstance(notes, list) or "not a teacher-search label" not in notes:
        print(f"missing notes: {notes!r}", file=sys.stderr)
        return False
    return True


def load_importer_module(importer: Path) -> Any:
    spec = importlib.util.spec_from_file_location("egaroucid_sequence_importer_under_test", importer)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load importer module from {importer}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def coordinate(index: int) -> str:
    return f"{chr(ord('a') + (index % 8))}{(index // 8) + 1}"


def transcript_with_shared_prefix(module: Any, base_transcript: str, seed: int) -> str:
    base_tokens = module.parse_transcript(base_transcript)
    prefix = base_tokens[:4]
    board = module.initial_board()
    side = "X"
    emitted: list[str] = []
    for token in prefix:
        index = module.move_index(token)
        if index is None or index not in module.legal_moves(board, side):
            raise RuntimeError("base prefix is not a legal explicit sequence")
        module.apply_move(board, side, index)
        emitted.append(token)
        side = module.opponent(side)

    base_next = module.move_index(base_tokens[4])
    legal = [move for move in module.legal_moves(board, side) if move != base_next]
    if not legal:
        raise RuntimeError("base prefix has no alternate legal continuation")
    first = legal[seed % len(legal)]
    module.apply_move(board, side, first)
    emitted.append(coordinate(first))
    side = module.opponent(side)

    step = 0
    while True:
        legal = module.legal_moves(board, side)
        if legal:
            move = legal[(seed + step) % len(legal)]
            module.apply_move(board, side, move)
            emitted.append(coordinate(move))
            side = module.opponent(side)
            step += 1
            continue
        other = module.opponent(side)
        if module.legal_moves(board, other):
            emitted.append("pass")
            side = other
            step += 1
            continue
        return " ".join(emitted)


def semantic_split(module: Any, transcript: str) -> tuple[str, str]:
    args = SimpleNamespace(
        dataset_id=DATASET_ID,
        min_ply=4,
        max_ply=4,
        ply_stride=1,
        emit_terminal=True,
        seed=0,
    )
    _rows, _pass_count, _terminal, game_group_id = module.replay_game(args, "occ-test", transcript)
    split = module.split_for_digest(module.digest_for_parts(DATASET_ID, game_group_id))
    return game_group_id, split


def collision_fixture_text(importer: Path, fixture: Path) -> str | None:
    module = load_importer_module(importer)
    base = fixture.read_text(encoding="utf-8").splitlines()[0]
    base_group, base_split = semantic_split(module, base)
    for seed in range(200):
        candidate = transcript_with_shared_prefix(module, base, seed)
        candidate_group, candidate_split = semantic_split(module, candidate)
        if candidate_group != base_group and candidate_split != base_split:
            return f"{base}\n{candidate}\n"
    print("could not generate connected split collision fixture", file=sys.stderr)
    return None


def check_connected_split_policy(importer: Path, fixture: Path, manifest: Path) -> bool:
    fixture_text = collision_fixture_text(importer, fixture)
    if fixture_text is None:
        return False
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        collision_fixture = temp_path / "collision-sequences.txt"
        collision_fixture.write_text(fixture_text, encoding="utf-8")

        preserve_report = temp_path / "preserve-report.json"
        preserve = run_importer(
            importer,
            collision_fixture,
            manifest,
            preserve_report,
            extra_args=["--min-ply", "4", "--max-ply", "4", "--ply-stride", "1"],
        )
        if preserve.returncode != 0:
            sys.stderr.write(preserve.stderr)
            sys.stderr.write(preserve.stdout)
            return False
        preserve_rows = parse_rows(preserve.stdout)
        preserve_data = load_report(preserve_report)
        if preserve_rows is None or preserve_data is None:
            return False
        if len({row["board_id"] for row in preserve_rows}) != 1:
            print("collision fixture did not emit the shared board", file=sys.stderr)
            return False
        if len({row["game_group_id"] for row in preserve_rows}) != 2:
            print("collision fixture did not emit distinct game groups", file=sys.stderr)
            return False
        if preserve_data.get("cross_split_board_collision_count") != 1:
            print(f"preserve split did not expose board leakage: {preserve_data!r}", file=sys.stderr)
            return False

        strict = run_importer(
            importer,
            collision_fixture,
            manifest,
            temp_path / "strict-report.json",
            extra_args=[
                "--min-ply",
                "4",
                "--max-ply",
                "4",
                "--ply-stride",
                "1",
                "--strict-board-disjoint-splits",
            ],
        )
        if strict.returncode == 0 or "exact board leakage detected" not in strict.stderr:
            print(f"strict preserve split did not reject leakage: {strict.stderr!r}", file=sys.stderr)
            return False

        connected_report = temp_path / "connected-report.json"
        connected = run_importer(
            importer,
            collision_fixture,
            manifest,
            connected_report,
            extra_args=[
                "--min-ply",
                "4",
                "--max-ply",
                "4",
                "--ply-stride",
                "1",
                "--split-policy",
                "connected-board-game",
                "--strict-board-disjoint-splits",
            ],
        )
        if connected.returncode != 0:
            sys.stderr.write(connected.stderr)
            sys.stderr.write(connected.stdout)
            return False
        connected_rows = parse_rows(connected.stdout)
        connected_data = load_report(connected_report)
        if connected_rows is None or connected_data is None:
            return False
        if len({row["split"] for row in connected_rows}) != 1:
            print("connected split did not keep the component in one split", file=sys.stderr)
            return False
        before = connected_data.get("board_leakage_audit_before")
        after = connected_data.get("board_leakage_audit_after")
        game_after = connected_data.get("game_group_leakage_audit_after")
        if (
            not isinstance(before, dict)
            or before.get("cross_split_board_collision_count") != 1
            or not isinstance(after, dict)
            or after.get("cross_split_board_collision_count") != 0
            or not isinstance(game_after, dict)
            or game_after.get("cross_split_game_group_collision_count") != 0
            or connected_data.get("connected_component_count") != 1
        ):
            print(f"connected split report mismatch: {connected_data!r}", file=sys.stderr)
            return False
    return True


def check_bounded_sampling(importer: Path, fixture: Path, manifest: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        first_report = temp_path / "bounded-first-report.json"
        second_report = temp_path / "bounded-second-report.json"
        extra_args = [
            "--sampling-mode",
            "bounded-dev",
            "--max-games",
            "2",
            "--max-files",
            "1",
            "--game-sample-rate",
            "1.0",
            "--file-order",
            "hash",
            "--progress-every-games",
            "1",
            "--progress-every-files",
            "1",
        ]
        first = run_importer(importer, fixture, manifest, first_report, extra_args=extra_args)
        second = run_importer(importer, fixture, manifest, second_report, extra_args=extra_args)
        if first.returncode != 0 or second.returncode != 0:
            sys.stderr.write(first.stderr)
            sys.stderr.write(first.stdout)
            sys.stderr.write(second.stderr)
            sys.stderr.write(second.stdout)
            return False
        if first.stdout != second.stdout:
            print("bounded import output is not deterministic", file=sys.stderr)
            return False
        first_data = load_report(first_report)
        second_data = load_report(second_report)
        if first_data is None or second_data is None:
            return False
        if first_data != second_data:
            print("bounded report is not deterministic", file=sys.stderr)
            return False
        expected = {
            "sampling_mode": "bounded-dev",
            "file_order": "hash",
            "max_files": 1,
            "max_games": 2,
            "file_sample_rate": None,
            "game_sample_rate": 1.0,
            "source_files_seen": 1,
            "source_files_processed": 1,
            "games_seen": 2,
            "games_replayed": 2,
            "replay_skip_count": 0,
            "accepted_games": 2,
            "rejected_games": 0,
            "input_games": 2,
        }
        for key, value in expected.items():
            if first_data.get(key) != value:
                print(f"bounded report mismatch for {key}: {first_data.get(key)!r}", file=sys.stderr)
                return False
        notes = first_data.get("sampling_frame_notes")
        if not isinstance(notes, list) or "not a full-corpus exact top-k sample" not in " ".join(notes):
            print(f"missing bounded notes: {notes!r}", file=sys.stderr)
            return False
        if "progress elapsed_sec=" not in first.stderr:
            print(f"missing progress output: {first.stderr!r}", file=sys.stderr)
            return False
    return True


def check_streaming_target(importer: Path, fixture: Path, manifest: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        report_path = temp_path / "streaming-target-report.json"
        result = run_importer(
            importer,
            fixture,
            manifest,
            report_path,
            extra_args=[
                "--sampling-mode",
                "streaming-target",
                "--max-positions",
                "5",
                "--file-order",
                "hash",
                "--progress-every-games",
                "1",
                "--progress-every-files",
                "1",
            ],
        )
        if result.returncode != 0:
            sys.stderr.write(result.stderr)
            sys.stderr.write(result.stdout)
            return False
        rows = parse_rows(result.stdout)
        data = load_report(report_path)
        if rows is None or data is None:
            return False
        if len(rows) != 5 or data.get("emitted_positions") != 5:
            print(
                f"streaming-target did not stop at target: rows={len(rows)} report={data!r}",
                file=sys.stderr,
            )
            return False
        if data.get("sampling_mode") != "streaming-target" or data.get("target_limit_reached") is not True:
            print(f"streaming-target policy mismatch: {data!r}", file=sys.stderr)
            return False
        notes = data.get("sampling_frame_notes")
        if not isinstance(notes, list) or "not a full-corpus exact top-k sample" not in " ".join(notes):
            print(f"missing streaming-target notes: {notes!r}", file=sys.stderr)
            return False
        if "progress elapsed_sec=" not in result.stderr:
            print(f"missing streaming-target progress output: {result.stderr!r}", file=sys.stderr)
            return False
    return True


def check_sampling_mode_validation(importer: Path, fixture: Path, manifest: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        result = run_importer(
            importer,
            fixture,
            manifest,
            temp_path / "bounded-without-mode-report.json",
            extra_args=["--max-games", "1"],
        )
        if result.returncode == 0:
            print("bounded controls without bounded-dev mode were not rejected", file=sys.stderr)
            return False
        if "require --sampling-mode bounded-dev" not in result.stderr:
            print(f"unexpected validation error: {result.stderr!r}", file=sys.stderr)
            return False
    return True


def check_zip_and_directory(importer: Path, fixture: Path, manifest: Path, expected_rows: int) -> bool:
    fixture_text = fixture.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        zip_path = temp_path / "Egaroucid_Train_Data_v0002_0.zip"
        with zipfile.ZipFile(zip_path, "w") as archive:
            archive.writestr("0002/10/0000000.txt", fixture_text)
            archive.writestr("0002/10/0000001.txt", fixture_text)
        zip_report = temp_path / "zip-report.json"
        zip_result = run_importer(
            importer,
            zip_path,
            manifest,
            zip_report,
            extra_args=["--sampling-mode", "bounded-dev", "--max-files", "1"],
        )
        zip_rows = parse_rows(zip_result.stdout) if zip_result.returncode == 0 else None
        zip_report_data = load_report(zip_report) if zip_result.returncode == 0 else None
        if (
            zip_result.returncode != 0
            or zip_rows is None
            or zip_report_data is None
            or len(zip_rows) != expected_rows
            or zip_report_data.get("source_files_seen") != 2
            or zip_report_data.get("source_files_processed") != 1
            or zip_report_data.get("accepted_games") != 3
        ):
            sys.stderr.write(zip_result.stderr)
            sys.stderr.write(zip_result.stdout)
            print("zip import did not produce the expected rows/report", file=sys.stderr)
            return False

        nested = temp_path / "extracted" / "0002" / "10"
        nested.mkdir(parents=True)
        (nested / "0000000.txt").write_text(fixture_text, encoding="utf-8")
        (nested / "0000001.txt").write_text(fixture_text, encoding="utf-8")
        dir_report = temp_path / "dir-report.json"
        dir_result = run_importer(
            importer,
            temp_path / "extracted",
            manifest,
            dir_report,
            extra_args=["--sampling-mode", "bounded-dev", "--max-files", "1"],
        )
        dir_rows = parse_rows(dir_result.stdout) if dir_result.returncode == 0 else None
        dir_report_data = load_report(dir_report) if dir_result.returncode == 0 else None
        if (
            dir_result.returncode != 0
            or dir_rows is None
            or dir_report_data is None
            or len(dir_rows) != expected_rows
            or dir_report_data.get("source_files_seen") != 2
            or dir_report_data.get("source_files_processed") != 1
            or dir_report_data.get("accepted_games") != 3
        ):
            sys.stderr.write(dir_result.stderr)
            sys.stderr.write(dir_result.stdout)
            print("directory import did not produce the expected rows/report", file=sys.stderr)
            return False

        zip_dir = temp_path / "zip-directory"
        zip_dir.mkdir()
        nested_zip_path = zip_dir / "nested-sequences.zip"
        with zipfile.ZipFile(nested_zip_path, "w") as archive:
            archive.writestr("0002/10/0000000.txt", fixture_text)
            archive.writestr("0002/10/0000001.txt", fixture_text)
        zip_dir_report = temp_path / "zip-dir-report.json"
        zip_dir_result = run_importer(
            importer,
            zip_dir,
            manifest,
            zip_dir_report,
            extra_args=["--sampling-mode", "bounded-dev", "--max-files", "1"],
        )
        zip_dir_rows = parse_rows(zip_dir_result.stdout) if zip_dir_result.returncode == 0 else None
        zip_dir_report_data = load_report(zip_dir_report) if zip_dir_result.returncode == 0 else None
        if (
            zip_dir_result.returncode != 0
            or zip_dir_rows is None
            or zip_dir_report_data is None
            or len(zip_dir_rows) != expected_rows
            or zip_dir_report_data.get("source_files_seen") != 2
            or zip_dir_report_data.get("source_files_processed") != 1
            or zip_dir_report_data.get("accepted_games") != 3
        ):
            sys.stderr.write(zip_dir_result.stderr)
            sys.stderr.write(zip_dir_result.stdout)
            print("zip-directory import did not produce the expected rows/report", file=sys.stderr)
            return False
    return True


def check_storage_independent_identity(importer: Path, fixture: Path, manifest: Path) -> bool:
    fixture_text = fixture.read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        first_zip = temp_path / "first-name.zip"
        second_zip = temp_path / "second-name.zip"
        with zipfile.ZipFile(first_zip, "w") as archive:
            archive.writestr("a/0000000.txt", fixture_text)
        with zipfile.ZipFile(second_zip, "w") as archive:
            archive.writestr("renamed/member.txt", "\n" + fixture_text)

        first = run_importer(importer, first_zip, manifest, temp_path / "first-report.json")
        second = run_importer(importer, second_zip, manifest, temp_path / "second-report.json")
        if first.returncode != 0 or second.returncode != 0:
            sys.stderr.write(first.stderr)
            sys.stderr.write(second.stderr)
            return False
        first_rows = parse_rows(first.stdout)
        second_rows = parse_rows(second.stdout)
        if first_rows is None or second_rows is None:
            return False

        def stable_identity(rows: list[dict[str, str]]) -> list[tuple[str, str, str, str, str, str, str]]:
            return sorted(
                (
                    row["record_id"],
                    row["game_group_id"],
                    row["position_id"],
                    row["board_id"],
                    row["source_occurrence_id"],
                    row["split"],
                    row["label_score_side_to_move"],
                )
                for row in rows
            )

        if stable_identity(first_rows) != stable_identity(second_rows):
            print("semantic identity changed after zip/member/path/line-number change", file=sys.stderr)
            return False

        if {
            row["source_occurrence_id"] for row in first_rows
        } != {row["source_occurrence_id"] for row in second_rows}:
            print("source occurrence ids changed after provenance-only changes", file=sys.stderr)
            return False
        if first.stdout != second.stdout:
            print("normalized TSV changed after provenance-only changes", file=sys.stderr)
            return False
    return True


def check_seed_does_not_change_identity(importer: Path, fixture: Path, manifest: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        first = run_importer(importer, fixture, manifest, temp_path / "seed-7.json", seed=7)
        second = run_importer(importer, fixture, manifest, temp_path / "seed-99.json", seed=99)
        if first.returncode != 0 or second.returncode != 0:
            sys.stderr.write(first.stderr)
            sys.stderr.write(second.stderr)
            return False
        first_rows = parse_rows(first.stdout)
        second_rows = parse_rows(second.stdout)
        if first_rows is None or second_rows is None:
            return False
        identity_fields = (
            "record_id",
            "position_id",
            "game_group_id",
            "board_id",
            "source_occurrence_id",
            "split",
        )
        if [
            tuple(row[field] for field in identity_fields) for row in first_rows
        ] != [tuple(row[field] for field in identity_fields) for row in second_rows]:
            print("seed changed identity or split fields without bounded sampling", file=sys.stderr)
            return False
    return True


def check_manifest_validation(importer: Path, fixture: Path, manifest: Path) -> bool:
    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        mismatch = temp_path / "mismatch.manifest.json"
        mismatch.write_text(
            manifest.read_text(encoding="utf-8").replace(DATASET_ID, "other-sequence", 1),
            encoding="utf-8",
        )
        result = run_importer(importer, fixture, mismatch, temp_path / "bad-report.json")
        if result.returncode == 0 or "does not match --dataset-id" not in result.stderr:
            print("manifest mismatch was not rejected", file=sys.stderr)
            sys.stderr.write(result.stderr)
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--importer", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--dataset-exe", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as temp:
        temp_path = Path(temp)
        first_report_path = temp_path / "first-report.json"
        second_report_path = temp_path / "second-report.json"
        first = run_importer(args.importer, args.fixture, args.manifest, first_report_path)
        second = run_importer(args.importer, args.fixture, args.manifest, second_report_path)
        if first.returncode != 0:
            sys.stderr.write(first.stderr)
            sys.stderr.write(first.stdout)
            return 1
        if second.returncode != 0:
            sys.stderr.write(second.stderr)
            sys.stderr.write(second.stdout)
            return 1
        if first.stdout != second.stdout:
            print("import output is not deterministic", file=sys.stderr)
            return 1
        rows = parse_rows(first.stdout)
        first_report = load_report(first_report_path)
        second_report = load_report(second_report_path)
        if rows is None or first_report is None or second_report is None:
            return 1
        if first_report != second_report:
            print("report is not deterministic", file=sys.stderr)
            return 1
        if not check_rows(rows) or not check_report(first_report, rows):
            return 1

        normalized = temp_path / "sequence-normalized.tsv"
        dataset_report = temp_path / "dataset-report.json"
        normalized.write_text(first.stdout, encoding="utf-8")
        dataset = subprocess.run(
            [
                str(args.dataset_exe),
                "--normalized-tsv",
                str(normalized),
                "--report",
                str(dataset_report),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if dataset.returncode != 0:
            sys.stderr.write(dataset.stderr)
            sys.stderr.write(dataset.stdout)
            return 1
        if not dataset.stdout.strip():
            print("dataset builder emitted no pattern rows", file=sys.stderr)
            return 1

        if not check_zip_and_directory(args.importer, args.fixture, args.manifest, len(rows)):
            return 1
        if not check_bounded_sampling(args.importer, args.fixture, args.manifest):
            return 1
        if not check_streaming_target(args.importer, args.fixture, args.manifest):
            return 1
        if not check_sampling_mode_validation(args.importer, args.fixture, args.manifest):
            return 1
        if not check_connected_split_policy(args.importer, args.fixture, args.manifest):
            return 1
        if not check_storage_independent_identity(args.importer, args.fixture, args.manifest):
            return 1
        if not check_seed_does_not_change_identity(args.importer, args.fixture, args.manifest):
            return 1
        if not check_manifest_validation(args.importer, args.fixture, args.manifest):
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
