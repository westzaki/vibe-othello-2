#!/usr/bin/env python3
"""Import local Egaroucid v0002 sequence transcripts as normalized TSV rows.

Format assumptions:

* inputs are local-only external transcript files, directories of ``.txt``
  files, or zip archives containing ``.txt`` files
* each non-empty line is one Othello game transcript
* moves are either whitespace-separated coordinates/tokens or a compact
  coordinate string such as ``d3c3b3...``
* coordinates are a1..h8; explicit pass tokens are ``pass`` or ``p``
* transcripts may omit pass tokens; when the side to move has no legal move,
  an implicit pass is inserted before reading the next move
* emitted labels are final-disc-difference labels derived from the transcript
  final board, converted to side-to-move perspective; they are not teacher
  search labels and are not production strength evidence
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import heapq
import io
import json
import re
import sys
import time
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, TextIO


DATASET_ID = "egaroucid-sequence-v0002-local"
IMPORTER_VERSION = "egaroucid-sequence-v1"
IDENTITY_POLICY_VERSION = "egaroucid-sequence-identity-v1"
SOURCE_KIND = "egaroucid-sequence-local"
LABEL_KIND = "observed_final_disc_diff"
LABEL_UNIT = "final_disc_diff"
LABEL_PERSPECTIVE = "side_to_move"
PHASE_COUNT = 13
MIN_OCCUPIED_COUNT = 4
MAX_EGAROUCID_OCCUPIED_COUNT = 64
SPLITS = ("train", "validation", "test")
COORD_RE = re.compile(r"^[a-h][1-8]$", re.IGNORECASE)
COMPACT_RE = re.compile(r"^(?:[a-h][1-8])+$", re.IGNORECASE)
HEADER = (
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
)
DIRECTIONS = (
    (-1, -1),
    (-1, 0),
    (-1, 1),
    (0, -1),
    (0, 1),
    (1, -1),
    (1, 0),
    (1, 1),
)
NOTES = (
    "local-only external corpus",
    "generated normalized TSV must not be committed",
    "labels are final-disc-diff side-to-move derived from transcript final board",
    "not a teacher-search label",
    "not production strength evidence",
    "game_group_id is derived from canonical replayed move/pass sequence",
    "source paths, archive members, and line numbers only affect source_occurrence_id and record_id",
    "split is derived from dataset_id + game_group_id",
    "board_id is derived from the side-to-move-relative board only",
)
BOUNDED_DEV_NOTES = (
    "bounded-dev mode is for local measurement iteration",
    "bounded-dev is not a full-corpus exact top-k sample",
    "bounded-dev is not a production strength claim",
)
BOUNDED_DEV_NOTES = (
    "bounded-dev mode is for local measurement iteration",
    "bounded-dev is not a full-corpus exact top-k sample",
    "bounded-dev is not a production strength claim",
)


class ImportErrorWithLocation(ValueError):
    pass


@dataclass
class CandidateRow:
    key: str
    ordinal: int
    fields: tuple[str, ...]
    line_text: str
    position_id: str
    game_group_id: str
    board_id: str
    split: str
    phase: int
    label: int


@dataclass
class TopKRows:
    capacity: int | None
    heap: list[tuple[int, str, str]] = field(default_factory=list)
    entries: dict[str, str] = field(default_factory=dict)
    all_rows: list[CandidateRow] = field(default_factory=list)

    @staticmethod
    def heap_key(key: str) -> int:
        return -int(key, 16)

    def discard_stale_worst_entries(self) -> None:
        while self.heap:
            _, position_id, key = self.heap[0]
            if self.entries.get(position_id) == key:
                return
            heapq.heappop(self.heap)

    def push(self, position_id: str, key: str) -> None:
        self.entries[position_id] = key
        heapq.heappush(self.heap, (self.heap_key(key), position_id, key))

    def consider(self, row: CandidateRow) -> None:
        self.all_rows.append(row)
        if self.capacity is None:
            return
        if self.capacity <= 0:
            return
        current = self.entries.get(row.position_id)
        if current is not None:
            if row.key < current:
                self.push(row.position_id, row.key)
            return
        if len(self.entries) < self.capacity:
            self.push(row.position_id, row.key)
            return
        self.discard_stale_worst_entries()
        if not self.heap:
            self.push(row.position_id, row.key)
            return
        _, worst_position_id, worst_key = self.heap[0]
        if row.key < worst_key:
            del self.entries[worst_position_id]
            heapq.heappop(self.heap)
            self.push(row.position_id, row.key)

    def rows(self) -> list[CandidateRow]:
        if self.capacity is None:
            return sorted(self.all_rows, key=lambda row: row.ordinal)
        return sorted(
            (row for row in self.all_rows if row.position_id in self.entries),
            key=lambda row: row.ordinal,
        )

    def kept_count(self) -> int:
        if self.capacity is None:
            return len(self.all_rows)
        return len(self.entries)


@dataclass
class Summary:
    input_games: int = 0
    accepted_games: int = 0
    rejected_games: int = 0
    emitted_positions: int = 0
    invalid_move_count: int = 0
    pass_count: int = 0
    terminal_count: int = 0
    split_counts: dict[str, int] = field(default_factory=lambda: {split: 0 for split in SPLITS})
    phase_counts: dict[str, int] = field(default_factory=dict)
    label_min: int | None = None
    label_max: int | None = None
    label_sum: int = 0
    rejected_reasons: list[str] = field(default_factory=list)
    source_files: int = 0
    source_files_seen: int = 0
    source_files_processed: int = 0
    games_seen: int = 0
    games_replayed: int = 0
    replay_skip_count: int = 0
    candidate_positions: int = 0
    game_group_occurrences: dict[str, int] = field(default_factory=dict)


@dataclass
class ProgressState:
    start_time: float = field(default_factory=time.monotonic)
    last_games_report: int = 0
    last_files_report: int = 0


@dataclass
class ProgressState:
    start_time: float = field(default_factory=time.monotonic)
    last_games_report: int = 0
    last_files_report: int = 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--dataset-id", default=DATASET_ID)
    parser.add_argument("--report", type=Path, help="Write JSON import report.")
    parser.add_argument("--min-ply", type=int, default=8)
    parser.add_argument("--max-ply", type=int)
    parser.add_argument("--ply-stride", type=int, default=1)
    parser.add_argument("--max-positions", type=int)
    parser.add_argument("--sampling-mode", choices=("full-scan-topk", "bounded-dev"), default="full-scan-topk")
    parser.add_argument("--max-games", type=int)
    parser.add_argument("--max-files", type=int)
    parser.add_argument("--file-sample-rate", type=float)
    parser.add_argument("--game-sample-rate", type=float)
    parser.add_argument("--file-order", choices=("path", "hash"), default="path")
    parser.add_argument("--progress-every-games", type=int)
    parser.add_argument("--progress-every-files", type=int)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--emit-terminal", dest="emit_terminal", action="store_true", default=False)
    parser.add_argument("--no-emit-terminal", dest="emit_terminal", action="store_false")
    parser.add_argument(
        "--strict-board-disjoint-splits",
        action="store_true",
        help="Fail if an exact side-to-move-relative board appears in more than one split.",
    )
    args = parser.parse_args()
    if args.min_ply < 0:
        parser.error("--min-ply must be non-negative")
    if args.max_ply is not None and args.max_ply < args.min_ply:
        parser.error("--max-ply must be >= --min-ply")
    if args.ply_stride <= 0:
        parser.error("--ply-stride must be positive")
    if args.max_positions is not None and args.max_positions <= 0:
        parser.error("--max-positions must be positive")
    if args.max_games is not None and args.max_games <= 0:
        parser.error("--max-games must be positive")
    if args.max_files is not None and args.max_files <= 0:
        parser.error("--max-files must be positive")
    for name in ("file_sample_rate", "game_sample_rate"):
        value = getattr(args, name)
        if value is not None and not (0.0 < value <= 1.0):
            parser.error(f"--{name.replace('_', '-')} must be > 0.0 and <= 1.0")
    bounded_controls = (
        args.max_games is not None
        or args.max_files is not None
        or args.file_sample_rate is not None
        or args.game_sample_rate is not None
    )
    if bounded_controls and args.sampling_mode != "bounded-dev":
        parser.error(
            "--max-games, --max-files, --file-sample-rate, and --game-sample-rate "
            "require --sampling-mode bounded-dev"
        )
    if args.progress_every_games is not None and args.progress_every_games <= 0:
        parser.error("--progress-every-games must be positive")
    if args.progress_every_files is not None and args.progress_every_files <= 0:
        parser.error("--progress-every-files must be positive")
    return args


def stable_json(data: object) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def digest_for_parts(*parts: object) -> str:
    return hashlib.sha256("\t".join(str(part) for part in parts).encode("ascii")).hexdigest()


def digest_fraction(digest: str) -> float:
    return int(digest, 16) / float(1 << 256)


def split_for_digest(digest: str) -> str:
    bucket = int(digest[:16], 16) % 10
    if bucket == 0:
        return "validation"
    if bucket == 1:
        return "test"
    return "train"


def prefixed_id(prefix: str, digest: str) -> str:
    return f"{prefix}-{digest[:16]}"


def phase_for_occupied_count(occupied_count: int) -> int:
    phase = (occupied_count - MIN_OCCUPIED_COUNT) * PHASE_COUNT // 60
    return min(PHASE_COUNT - 1, max(0, phase))


def file_digest(args: argparse.Namespace, source_ref: str) -> str:
    return digest_for_parts(args.seed, "file", source_ref)


def game_digest(args: argparse.Namespace, source_ref: str, line_number: int, text: str) -> str:
    return digest_for_parts(args.dataset_id, source_ref, line_number, text.strip())


def include_by_rate(digest: str, rate: float | None) -> bool:
    return rate is None or digest_fraction(digest) < rate


def selected_file_refs(args: argparse.Namespace, refs: list[str]) -> list[str]:
    ordered = sorted(refs, key=lambda ref: ref if args.file_order == "path" else file_digest(args, ref))
    filtered = [ref for ref in ordered if include_by_rate(file_digest(args, ref), args.file_sample_rate)]
    if args.max_files is not None:
        return filtered[: args.max_files]
    return filtered


def should_replay_game(args: argparse.Namespace, summary: Summary, game_key: str) -> bool:
    if not include_by_rate(game_key, args.game_sample_rate):
        summary.replay_skip_count += 1
        return False
    return True


def replay_limit_reached(args: argparse.Namespace, summary: Summary) -> bool:
    return args.max_games is not None and summary.games_replayed >= args.max_games


def maybe_report_progress(
    args: argparse.Namespace,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
    *,
    force: bool = False,
) -> None:
    if args.progress_every_games is None and args.progress_every_files is None:
        return
    games_due = (
        args.progress_every_games is not None
        and summary.games_seen - progress.last_games_report >= args.progress_every_games
    )
    files_due = (
        args.progress_every_files is not None
        and summary.source_files_processed - progress.last_files_report >= args.progress_every_files
    )
    if not force and not games_due and not files_due:
        return
    elapsed = max(time.monotonic() - progress.start_time, 1e-9)
    games_per_sec = summary.games_replayed / elapsed
    print(
        "progress "
        f"elapsed_sec={elapsed:.3f} "
        f"source_files_seen={summary.source_files_seen} "
        f"source_files_processed={summary.source_files_processed} "
        f"games_seen={summary.games_seen} "
        f"games_replayed={summary.games_replayed} "
        f"accepted_games={summary.accepted_games} "
        f"rejected_games={summary.rejected_games} "
        f"candidate_positions={summary.candidate_positions} "
        f"kept_positions={rows.kept_count()} "
        f"games_per_sec={games_per_sec:.3f}",
        file=sys.stderr,
    )
    progress.last_games_report = summary.games_seen
    progress.last_files_report = summary.source_files_processed


def load_manifest_dataset_id(path: Path) -> str:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ImportErrorWithLocation(f"{path}: manifest is not valid JSON: {error}") from error
    except OSError as error:
        raise ImportErrorWithLocation(f"{path}: cannot read manifest: {error}") from error
    if not isinstance(manifest, dict):
        raise ImportErrorWithLocation(f"{path}: manifest root must be a JSON object")
    dataset_id = manifest.get("dataset_id")
    if not isinstance(dataset_id, str) or not dataset_id:
        raise ImportErrorWithLocation(f"{path}: manifest dataset_id must be a non-empty string")
    return dataset_id


def initial_board() -> list[str]:
    board = ["-"] * 64
    board[3 * 8 + 3] = "O"
    board[4 * 8 + 4] = "O"
    board[3 * 8 + 4] = "X"
    board[4 * 8 + 3] = "X"
    return board


def opponent(side: str) -> str:
    return "O" if side == "X" else "X"


def legal_moves(board: list[str], side: str) -> list[int]:
    other = opponent(side)
    moves: list[int] = []
    for index, value in enumerate(board):
        if value != "-":
            continue
        x = index % 8
        y = index // 8
        legal = False
        for dx, dy in DIRECTIONS:
            nx = x + dx
            ny = y + dy
            seen_other = False
            while 0 <= nx < 8 and 0 <= ny < 8 and board[ny * 8 + nx] == other:
                seen_other = True
                nx += dx
                ny += dy
            if seen_other and 0 <= nx < 8 and 0 <= ny < 8 and board[ny * 8 + nx] == side:
                legal = True
                break
        if legal:
            moves.append(index)
    return moves


def apply_move(board: list[str], side: str, move_index: int) -> None:
    if board[move_index] != "-":
        raise ImportErrorWithLocation("move targets an occupied square")
    x = move_index % 8
    y = move_index // 8
    other = opponent(side)
    flips: list[int] = []
    for dx, dy in DIRECTIONS:
        nx = x + dx
        ny = y + dy
        line: list[int] = []
        while 0 <= nx < 8 and 0 <= ny < 8 and board[ny * 8 + nx] == other:
            line.append(ny * 8 + nx)
            nx += dx
            ny += dy
        if line and 0 <= nx < 8 and 0 <= ny < 8 and board[ny * 8 + nx] == side:
            flips.extend(line)
    if not flips:
        raise ImportErrorWithLocation("move flips no discs")
    board[move_index] = side
    for index in flips:
        board[index] = side


def parse_transcript(text: str) -> list[str]:
    tokens = text.strip().split()
    if len(tokens) == 1 and COMPACT_RE.fullmatch(tokens[0]) is not None:
        compact = tokens[0].lower()
        return [compact[index : index + 2] for index in range(0, len(compact), 2)]
    return [token.lower() for token in tokens]


def move_index(token: str) -> int | None:
    if COORD_RE.fullmatch(token) is None:
        return None
    return (int(token[1]) - 1) * 8 + (ord(token[0].lower()) - ord("a"))


def should_emit(ply: int, is_terminal: bool, args: argparse.Namespace) -> bool:
    if ply < args.min_ply:
        return False
    if args.max_ply is not None and ply > args.max_ply:
        return False
    if (ply - args.min_ply) % args.ply_stride != 0:
        return False
    if is_terminal and not args.emit_terminal:
        return False
    return True


def relative_board(board: list[str], side_to_move: str) -> str:
    if side_to_move == "X":
        return "".join(board)
    return "".join("X" if value == "O" else "O" if value == "X" else "-" for value in board)


def make_row(
    args: argparse.Namespace,
    game_group_id: str,
    source_occurrence_id: str,
    split: str,
    board: list[str],
    side_to_move: str,
    ply: int,
    occurrence: int,
    black_final_diff: int,
) -> CandidateRow:
    board_text = relative_board(board, side_to_move)
    label = black_final_diff if side_to_move == "X" else -black_final_diff
    board_id = prefixed_id("board", digest_for_parts("board-v1", board_text))
    position_digest = digest_for_parts(args.dataset_id, game_group_id, ply, board_id)
    record_digest = digest_for_parts(
        args.dataset_id, source_occurrence_id, game_group_id, ply, board_id, label, occurrence
    )
    record_id = f"{args.dataset_id}-{record_digest[:16]}-{occurrence:06d}"
    position_id = f"{args.dataset_id}-{game_group_id}-{ply:03d}-{position_digest[:16]}"
    player_count = board_text.count("X")
    opponent_count = board_text.count("O")
    empty_count = board_text.count("-")
    occupied_count = player_count + opponent_count
    phase = phase_for_occupied_count(occupied_count)
    fields = (
        record_id,
        position_id,
        game_group_id,
        board_id,
        source_occurrence_id,
        args.dataset_id,
        split,
        board_text,
        LABEL_KIND,
        LABEL_UNIT,
        LABEL_PERSPECTIVE,
        str(label),
        str(occupied_count),
        str(phase),
        str(player_count),
        str(opponent_count),
        str(empty_count),
    )
    line_text = "\t".join(fields)
    sample_key = digest_for_parts(args.seed, "sample-v1", game_group_id, position_id)
    return CandidateRow(
        key=sample_key,
        ordinal=occurrence,
        fields=fields,
        line_text=line_text,
        position_id=position_id,
        game_group_id=game_group_id,
        board_id=board_id,
        split=split,
        phase=phase,
        label=label,
    )


def replay_game(
    args: argparse.Namespace, source_ref: str, line_number: int, text: str
) -> tuple[list[CandidateRow], int, bool, str]:
    tokens = parse_transcript(text)
    if not tokens:
        raise ImportErrorWithLocation("transcript is empty")
    source_occurrence_id = prefixed_id(
        "occ", digest_for_parts(args.dataset_id, source_ref, line_number, text.strip())
    )
    board = initial_board()
    side = "X"
    snapshots: list[tuple[int, list[str], str]] = []
    canonical_moves: list[str] = []
    move_ply = 0
    pass_count = 0

    for token_index, token in enumerate(tokens, start=1):
        side_moves = legal_moves(board, side)
        if token in {"pass", "p"}:
            if side_moves:
                raise ImportErrorWithLocation(f"token {token_index}: pass is illegal with legal moves")
            if not legal_moves(board, opponent(side)):
                raise ImportErrorWithLocation(f"token {token_index}: pass after terminal position")
            side = opponent(side)
            canonical_moves.append("pass")
            pass_count += 1
            continue

        index = move_index(token)
        if index is None:
            raise ImportErrorWithLocation(f"token {token_index}: invalid move token {token!r}")
        if index not in side_moves:
            other = opponent(side)
            if not side_moves and index in legal_moves(board, other):
                side = other
                canonical_moves.append("pass")
                pass_count += 1
            else:
                raise ImportErrorWithLocation(f"token {token_index}: illegal move {token!r}")

        apply_move(board, side, index)
        canonical_moves.append(f"{chr(ord('a') + (index % 8))}{(index // 8) + 1}")
        move_ply += 1
        side = opponent(side)
        snapshots.append((move_ply, board.copy(), side))

    while not legal_moves(board, side) and legal_moves(board, opponent(side)):
        side = opponent(side)
        pass_count += 1
    terminal = not legal_moves(board, side) and not legal_moves(board, opponent(side))
    if not terminal:
        raise ImportErrorWithLocation("transcript ended before terminal position")

    canonical_game = " ".join(canonical_moves)
    game_group_id = prefixed_id(
        "game", digest_for_parts(args.dataset_id, IDENTITY_POLICY_VERSION, canonical_game)
    )
    split = split_for_digest(digest_for_parts(args.dataset_id, game_group_id))
    black_final_diff = board.count("X") - board.count("O")
    rows: list[CandidateRow] = []
    occurrence = 0
    for ply, snapshot, side_to_move in snapshots:
        is_terminal_snapshot = ply == move_ply
        if not should_emit(ply, is_terminal_snapshot, args):
            continue
        occurrence += 1
        rows.append(
            make_row(
                args,
                game_group_id,
                source_occurrence_id,
                split,
                snapshot,
                side_to_move,
                ply,
                occurrence,
                black_final_diff,
            )
        )
    return rows, pass_count, terminal, game_group_id


def process_text_stream(
    args: argparse.Namespace,
    source_ref: str,
    lines: Iterable[str],
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    summary.source_files += 1
    summary.source_files_processed += 1
    for line_number, line in enumerate(lines, start=1):
        if replay_limit_reached(args, summary):
            break
        text = line.strip()
        if not text:
            continue
        summary.games_seen += 1
        game_key = game_digest(args, source_ref, line_number, text)
        if not should_replay_game(args, summary, game_key):
            maybe_report_progress(args, summary, rows, progress)
            continue
        summary.input_games += 1
        summary.games_replayed += 1
        try:
            emitted, pass_count, terminal, game_group_id = replay_game(
                args, source_ref, line_number, text
            )
        except ImportErrorWithLocation as error:
            summary.rejected_games += 1
            summary.invalid_move_count += 1
            if len(summary.rejected_reasons) < 20:
                summary.rejected_reasons.append(f"{source_ref}:{line_number}: {error}")
            continue
        summary.accepted_games += 1
        summary.game_group_occurrences[game_group_id] = (
            summary.game_group_occurrences.get(game_group_id, 0) + 1
        )
        summary.pass_count += pass_count
        if terminal:
            summary.terminal_count += 1
        for row in emitted:
            summary.candidate_positions += 1
            row.ordinal = summary.candidate_positions
            rows.consider(row)
        maybe_report_progress(args, summary, rows, progress)
    maybe_report_progress(args, summary, rows, progress)


def process_zip(
    args: argparse.Namespace,
    path: Path,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile as error:
        raise ImportErrorWithLocation(f"{path}: invalid zip archive") from error
    with archive:
        text_entries = [
            info
            for info in archive.infolist()
            if not info.is_dir() and info.filename.lower().endswith(".txt")
        ]
        if not text_entries:
            raise ImportErrorWithLocation(f"{path}: zip archive contains no .txt files")
        source_refs = [f"{path}!{info.filename}" for info in text_entries]
        summary.source_files_seen += len(source_refs)
        info_by_ref = {f"{path}!{info.filename}": info for info in text_entries}
        for source_ref in selected_file_refs(args, source_refs):
            if replay_limit_reached(args, summary):
                break
            info = info_by_ref[source_ref]
            with archive.open(info) as raw:
                with io.TextIOWrapper(raw, encoding="utf-8", newline="") as text:
                    process_text_stream(args, source_ref, text, summary, rows, progress)


def process_plain_file(
    args: argparse.Namespace,
    path: Path,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
    *,
    counted: bool = False,
) -> None:
    if not counted:
        summary.source_files_seen += 1
    with path.open("r", encoding="utf-8", newline="") as handle:
        process_text_stream(args, str(path), handle, summary, rows, progress)


def process_directory(
    args: argparse.Namespace,
    path: Path,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    text_paths = [child for child in path.rglob("*.txt") if child.is_file()]
    if not text_paths:
        raise ImportErrorWithLocation(f"{path}: directory contains no .txt files")
    source_refs = [str(text_path) for text_path in text_paths]
    summary.source_files_seen += len(source_refs)
    path_by_ref = {str(text_path): text_path for text_path in text_paths}
    for source_ref in selected_file_refs(args, source_refs):
        if replay_limit_reached(args, summary):
            break
        process_plain_file(args, path_by_ref[source_ref], summary, rows, progress, counted=True)


def process_input(
    args: argparse.Namespace,
    path: Path,
    summary: Summary,
    rows: TopKRows,
    progress: ProgressState,
) -> None:
    if not path.exists():
        raise ImportErrorWithLocation(f"{path}: input does not exist")
    if path.is_dir():
        process_directory(args, path, summary, rows, progress)
    elif path.suffix.lower() == ".zip":
        process_zip(args, path, summary, rows, progress)
    else:
        source_refs = [str(path)]
        summary.source_files_seen += 1
        if str(path) in selected_file_refs(args, source_refs):
            process_plain_file(args, path, summary, rows, progress, counted=True)


def split_pair(left: str, right: str) -> str:
    return "__".join(sorted((left, right)))


def leakage_audit(selected_rows: list[CandidateRow]) -> dict[str, object]:
    board_splits: dict[str, set[str]] = {}
    for row in selected_rows:
        board_splits.setdefault(row.board_id, set()).add(row.split)
    pair_counts: dict[str, int] = {}
    collision_count = 0
    for splits in board_splits.values():
        if len(splits) < 2:
            continue
        collision_count += 1
        ordered = sorted(splits)
        for left_index, left in enumerate(ordered):
            for right in ordered[left_index + 1 :]:
                pair = split_pair(left, right)
                pair_counts[pair] = pair_counts.get(pair, 0) + 1
    return {
        "unique_board_count": len(board_splits),
        "cross_split_board_collision_count": collision_count,
        "cross_split_board_collision_counts_by_pair": dict(sorted(pair_counts.items())),
    }


def write_rows(selected_rows: list[CandidateRow], output: TextIO, summary: Summary) -> str:
    writer = csv.writer(output, delimiter="\t", lineterminator="\n")
    writer.writerow(HEADER)
    digest = hashlib.sha256()
    for row in selected_rows:
        writer.writerow(row.fields)
        digest.update(row.line_text.encode("utf-8"))
        digest.update(b"\n")
        summary.emitted_positions += 1
        summary.split_counts[row.split] = summary.split_counts.get(row.split, 0) + 1
        phase_key = str(row.phase)
        summary.phase_counts[phase_key] = summary.phase_counts.get(phase_key, 0) + 1
        summary.label_min = row.label if summary.label_min is None else min(summary.label_min, row.label)
        summary.label_max = row.label if summary.label_max is None else max(summary.label_max, row.label)
        summary.label_sum += row.label
    return f"sha256:{digest.hexdigest()}"


def report_for(
    args: argparse.Namespace, summary: Summary, checksum: str, audit: dict[str, object]
) -> dict[str, object]:
    label_mean = None
    if summary.emitted_positions:
        label_mean = float(f"{summary.label_sum / summary.emitted_positions:.12g}")
    duplicate_game_occurrences = sum(
        max(0, count - 1) for count in summary.game_group_occurrences.values()
    )
    sampling_frame_notes = list(BOUNDED_DEV_NOTES) if args.sampling_mode == "bounded-dev" else []
    notes = list(NOTES)
    notes.extend(sampling_frame_notes)
    return {
        "schema_version": 2,
        "importer_version": IMPORTER_VERSION,
        "identity_policy_version": IDENTITY_POLICY_VERSION,
        "source_dataset_id": args.dataset_id,
        "source_kind": SOURCE_KIND,
        "input_kind": "sequence-transcript",
        "sampling_mode": args.sampling_mode,
        "file_order": args.file_order,
        "max_files": args.max_files,
        "max_games": args.max_games,
        "file_sample_rate": args.file_sample_rate,
        "game_sample_rate": args.game_sample_rate,
        "source_files_seen": summary.source_files_seen,
        "source_files_processed": summary.source_files_processed,
        "games_seen": summary.games_seen,
        "games_replayed": summary.games_replayed,
        "replay_skip_count": summary.replay_skip_count,
        "sampling_frame_notes": sampling_frame_notes,
        "input_games": summary.input_games,
        "accepted_games": summary.accepted_games,
        "rejected_games": summary.rejected_games,
        "emitted_positions": summary.emitted_positions,
        "invalid_move_count": summary.invalid_move_count,
        "pass_count": summary.pass_count,
        "terminal_count": summary.terminal_count,
        "split_counts": summary.split_counts,
        "phase_counts": summary.phase_counts,
        "label_min": summary.label_min,
        "label_max": summary.label_max,
        "label_mean": label_mean,
        "checksum": checksum,
        "source_files": summary.source_files,
        "game_group_count": len(summary.game_group_occurrences),
        "duplicate_game_occurrence_count": duplicate_game_occurrences,
        "unique_board_count": audit["unique_board_count"],
        "cross_split_board_collision_count": audit["cross_split_board_collision_count"],
        "cross_split_board_collision_counts_by_pair": audit[
            "cross_split_board_collision_counts_by_pair"
        ],
        "rejected_reasons": summary.rejected_reasons,
        "sampling_policy": {
            "mode": args.sampling_mode,
            "file_order": args.file_order,
            "max_files": args.max_files,
            "max_games": args.max_games,
            "file_sample_rate": args.file_sample_rate,
            "game_sample_rate": args.game_sample_rate,
            "seed": args.seed,
            "game_filter_stage": "before legal replay",
        },
        "emit_policy": {
            "min_ply": args.min_ply,
            "max_ply": args.max_ply,
            "ply_stride": args.ply_stride,
            "max_positions": args.max_positions,
            "seed": args.seed,
            "emit_terminal": args.emit_terminal,
            "sample_policy": "deterministic position_id group sha256 top-k when max_positions is set; duplicate records for selected position_id are preserved",
        },
        "split_policy": {
            "method": "dataset_id + game_group_id sha256",
            "ratio": "80/10/10 by hash bucket",
            "game_group_id": "sha256 of dataset_id + identity_policy_version + canonical replayed move/pass sequence",
            "source_occurrence_id": "sha256 of dataset_id + source path/member + line number + transcript text",
            "position_id": "dataset_id + game_group_id + ply + board_id hash",
            "board_id": "sha256 of side-to-move-relative 64-cell board only",
            "game_leakage_policy": "all emitted positions from one semantic game stay in one split",
            "duplicate_position_policy": "duplicate source occurrences share game_group_id, position_id, board_id, and split; record_id remains occurrence-scoped",
        },
        "leakage_policy": {
            "exact_board_audit": "side-to-move-relative board_id collisions across splits are counted",
            "strict_board_disjoint_splits": args.strict_board_disjoint_splits,
        },
        "notes": notes,
    }


def main() -> int:
    args = parse_args()
    summary = Summary()
    rows = TopKRows(args.max_positions)
    progress = ProgressState()
    try:
        manifest_dataset_id = load_manifest_dataset_id(args.manifest)
        if manifest_dataset_id != args.dataset_id:
            raise ImportErrorWithLocation(
                f"{args.manifest}: manifest dataset_id {manifest_dataset_id!r} does not match "
                f"--dataset-id {args.dataset_id!r}"
            )
        for input_path in args.input:
            if replay_limit_reached(args, summary):
                break
            process_input(args, input_path, summary, rows, progress)
        selected_rows = rows.rows()
        if not selected_rows:
            raise ImportErrorWithLocation("no positions emitted")
        audit = leakage_audit(selected_rows)
        if args.strict_board_disjoint_splits and audit["cross_split_board_collision_count"] != 0:
            raise ImportErrorWithLocation(
                "exact board leakage detected across splits: "
                f"{audit['cross_split_board_collision_count']} board_id collision(s)"
            )
        checksum = write_rows(selected_rows, sys.stdout, summary)
        report = report_for(args, summary, checksum, audit)
        if args.report is not None:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(stable_json(report), encoding="utf-8")
        maybe_report_progress(args, summary, rows, progress, force=True)
    except (OSError, UnicodeDecodeError, ImportErrorWithLocation) as error:
        print(error, file=sys.stderr)
        return 1

    print(
        "summary "
        f"source_files={summary.source_files} "
        f"source_files_seen={summary.source_files_seen} "
        f"source_files_processed={summary.source_files_processed} "
        f"games_seen={summary.games_seen} "
        f"games_replayed={summary.games_replayed} "
        f"replay_skip_count={summary.replay_skip_count} "
        f"input_games={summary.input_games} "
        f"accepted_games={summary.accepted_games} "
        f"rejected_games={summary.rejected_games} "
        f"emitted_positions={summary.emitted_positions} "
        f"invalid_move_count={summary.invalid_move_count} "
        f"pass_count={summary.pass_count} "
        f"terminal_count={summary.terminal_count} "
        f"source_kind={SOURCE_KIND} checksum={checksum}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
