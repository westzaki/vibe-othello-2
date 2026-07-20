#!/usr/bin/env python3
"""Run local Egaroucid board-score import, dataset build, and value training."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


class TrainingRunError(RuntimeError):
    """Raised when a local training run cannot safely continue."""


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def atomic_write_json(path: Path, data: Any) -> None:
    temporary = path.with_name(f".{path.name}.tmp-{os.getpid()}")
    try:
        temporary.write_text(stable_json(data), encoding="utf-8")
        temporary.replace(path)
    finally:
        if temporary.exists():
            temporary.unlink()


def is_within(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
    except ValueError:
        return False
    return True


def environment_path(name: str) -> Path | None:
    value = os.environ.get(name)
    return Path(value).expanduser() if value else None


def parse_args() -> argparse.Namespace:
    root = repo_root()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--local-root",
        type=Path,
        help="External local root. Defaults to VIBE_OTHELLO_LOCAL.",
    )
    parser.add_argument(
        "--corpus",
        type=Path,
        help=(
            "Egaroucid_Train_Data.zip. Defaults to "
            "VIBE_OTHELLO_CORPORA/Egaroucid_Train_Data.zip or "
            "<local-root>/corpora/Egaroucid_Train_Data.zip."
        ),
    )
    parser.add_argument(
        "--training-root",
        type=Path,
        help="External training root. Defaults to VIBE_OTHELLO_TRAINING or <local-root>/training.",
    )
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--output-dir", type=Path)
    output.add_argument("--run-id")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=(
            root
            / "data/corpora/manifests/"
            "egaroucid-train-data-board-score-v2025-02-02.manifest.json"
        ),
    )
    parser.add_argument("--positions-per-phase", type=int, default=10_000)
    parser.add_argument("--max-source-files", type=int)
    parser.add_argument("--pattern-set", default="pattern-v2-endgame-lite")
    parser.add_argument(
        "--trainer-mode",
        choices=("pattern-sgd-v0c", "pattern-sgd-v0d"),
        default="pattern-sgd-v0d",
    )
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.05)
    parser.add_argument("--weight-decay", type=float, default=0.0001)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--progress-every-rows", type=int, default=1_000_000)
    parser.add_argument("--progress-every-examples", type=int, default=100_000)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--importer",
        type=Path,
        default=root / "tools/data-import/import_egaroucid_board_scores.py",
    )
    parser.add_argument(
        "--dataset-exe",
        type=Path,
        default=root / "build/tools/pattern/dataset/vibe-othello-pattern-dataset-smoke",
    )
    parser.add_argument(
        "--trainer",
        type=Path,
        default=root / "tools/pattern/train/train_pattern.py",
    )
    args = parser.parse_args()

    if args.positions_per_phase <= 0:
        parser.error("--positions-per-phase must be positive")
    if args.max_source_files is not None and args.max_source_files <= 0:
        parser.error("--max-source-files must be positive")
    if args.epochs < 0:
        parser.error("--epochs must be non-negative")
    if args.learning_rate < 0.0:
        parser.error("--learning-rate must be non-negative")
    if args.weight_decay < 0.0:
        parser.error("--weight-decay must be non-negative")
    if args.progress_every_rows <= 0:
        parser.error("--progress-every-rows must be positive")
    if args.progress_every_examples <= 0:
        parser.error("--progress-every-examples must be positive")
    if args.run_id is not None:
        if not args.run_id or args.run_id in {".", ".."} or "/" in args.run_id:
            parser.error("--run-id must be one non-empty path component")
    return args


def default_run_id(args: argparse.Namespace) -> str:
    bounded_suffix = (
        "" if args.max_source_files is None else f"-files{args.max_source_files}-bounded"
    )
    return (
        f"phase-p{args.positions_per_phase}-seed{args.seed}-"
        f"{args.trainer_mode}{bounded_suffix}"
    )


def resolve_layout(args: argparse.Namespace) -> dict[str, Path]:
    local_root = args.local_root or environment_path("VIBE_OTHELLO_LOCAL")
    if local_root is None:
        raise TrainingRunError("set VIBE_OTHELLO_LOCAL or pass --local-root")
    local_root = local_root.expanduser().resolve()

    corpora_root = environment_path("VIBE_OTHELLO_CORPORA") or local_root / "corpora"
    corpora_root = corpora_root.expanduser().resolve()
    corpus = (args.corpus or corpora_root / "Egaroucid_Train_Data.zip").expanduser().resolve()

    training_root = (
        args.training_root
        or environment_path("VIBE_OTHELLO_TRAINING")
        or local_root / "training"
    )
    training_root = training_root.expanduser().resolve()
    run_id = args.run_id or default_run_id(args)
    output_dir = (
        args.output_dir.expanduser().resolve()
        if args.output_dir is not None
        else training_root / "egaroucid-board-score" / run_id
    )

    root = repo_root().resolve()
    if is_within(output_dir, root):
        raise TrainingRunError("training output must stay outside the git repository")
    if is_within(output_dir, corpora_root):
        raise TrainingRunError("training output must not be placed inside the raw corpus directory")
    if output_dir == local_root:
        raise TrainingRunError(
            "training output must be a dedicated subdirectory, not the local root"
        )
    if not corpus.is_file():
        raise TrainingRunError(f"corpus archive does not exist: {corpus}")
    if not args.manifest.is_file():
        raise TrainingRunError(f"manifest does not exist: {args.manifest}")
    if not args.importer.is_file():
        raise TrainingRunError(f"importer does not exist: {args.importer}")
    if not args.dataset_exe.is_file():
        raise TrainingRunError(
            f"dataset executable does not exist: {args.dataset_exe}\n"
            "build it with: cmake --build build --target vibe_othello_pattern_dataset_smoke"
        )
    if not args.trainer.is_file():
        raise TrainingRunError(f"trainer does not exist: {args.trainer}")

    return {
        "local_root": local_root,
        "corpora_root": corpora_root,
        "training_root": training_root,
        "corpus": corpus,
        "output_dir": output_dir,
    }


def path_for_report(path: Path, layout: dict[str, Path]) -> str:
    resolved = path.resolve()
    roots = (
        ("<repo>", repo_root().resolve()),
        ("<local>", layout["local_root"]),
    )
    for prefix, root in roots:
        if is_within(resolved, root):
            return f"{prefix}/{resolved.relative_to(root)}"
    return resolved.name


def command_for_report(command: list[str], layout: dict[str, Path]) -> list[str]:
    result: list[str] = []
    for part in command:
        path = Path(part)
        if "/" in part and path.is_absolute():
            result.append(path_for_report(path, layout))
        else:
            result.append(part)
    return result


def load_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise TrainingRunError(f"cannot read stage report {path.name}: {error}") from error
    if not isinstance(data, dict):
        raise TrainingRunError(f"stage report root must be an object: {path.name}")
    return data


def run_logged_stage(
    *,
    name: str,
    command: list[str],
    log_path: Path,
    stdout_path: Path | None = None,
) -> None:
    print(f"[{name}] starting; log={log_path}")
    temporary_stdout = (
        stdout_path.with_name(f".{stdout_path.name}.tmp-{os.getpid()}")
        if stdout_path is not None
        else None
    )
    try:
        with log_path.open("w", encoding="utf-8") as log:
            if temporary_stdout is None:
                result = subprocess.run(
                    command,
                    check=False,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    text=True,
                )
            else:
                with temporary_stdout.open("w", encoding="utf-8", newline="") as output:
                    result = subprocess.run(
                        command,
                        check=False,
                        stdout=output,
                        stderr=log,
                        text=True,
                    )
        if result.returncode != 0:
            raise TrainingRunError(
                f"{name} failed with exit code {result.returncode}; see logs/{log_path.name}"
            )
        if temporary_stdout is not None:
            temporary_stdout.replace(stdout_path)
    finally:
        if temporary_stdout is not None and temporary_stdout.exists():
            temporary_stdout.unlink()
    print(f"[{name}] complete")


def build_commands(
    args: argparse.Namespace,
    layout: dict[str, Path],
    paths: dict[str, Path],
) -> dict[str, list[str]]:
    import_command = [
        sys.executable,
        str(args.importer.resolve()),
        "--input",
        str(layout["corpus"]),
        "--manifest",
        str(paths["source_manifest"]),
        "--output",
        str(paths["normalized"]),
        "--report",
        str(paths["import_report"]),
        "--max-positions-per-phase",
        str(args.positions_per_phase),
        "--seed",
        str(args.seed),
        "--progress-every-rows",
        str(args.progress_every_rows),
    ]
    if args.max_source_files is not None:
        import_command.extend(["--max-source-files", str(args.max_source_files)])

    dataset_command = [
        str(args.dataset_exe.resolve()),
        "--normalized-tsv",
        str(paths["normalized"]),
        "--report",
        str(paths["dataset_report"]),
        "--pattern-set",
        args.pattern_set,
        "--output-format",
        "compact-tsv",
    ]
    train_command = [
        sys.executable,
        str(args.trainer.resolve()),
        "--dataset",
        str(paths["dataset"]),
        "--mode",
        args.trainer_mode,
        "--epochs",
        str(args.epochs),
        "--learning-rate",
        str(args.learning_rate),
        "--weight-decay",
        str(args.weight_decay),
        "--lr-schedule",
        "inverse-sqrt",
        "--seed",
        str(args.seed),
        "--progress-every-examples",
        str(args.progress_every_examples),
        "--emit-trained-phases",
        "--dataset-report",
        str(paths["dataset_report"]),
        "--weights-out",
        str(paths["weights"]),
        "--report-out",
        str(paths["train_report"]),
    ]
    return {
        "import": import_command,
        "dataset": dataset_command,
        "train": train_command,
    }


def run_training(args: argparse.Namespace) -> Path:
    layout = resolve_layout(args)
    output_dir = layout["output_dir"]
    paths = {
        "source_manifest": output_dir / "source-manifest.json",
        "normalized": output_dir / "normalized/positions.tsv",
        "import_report": output_dir / "normalized/report.json",
        "dataset": output_dir / "dataset/pattern-dataset.tsv",
        "dataset_report": output_dir / "dataset/report.json",
        "weights": output_dir / "training/weights.json",
        "train_report": output_dir / "training/report.json",
        "run_report": output_dir / "run-report.json",
        "import_log": output_dir / "logs/import.log",
        "dataset_log": output_dir / "logs/dataset.log",
        "train_log": output_dir / "logs/train.log",
    }
    commands = build_commands(args, layout, paths)

    if args.dry_run:
        print(f"output_dir={output_dir}")
        for name, command in commands.items():
            print(f"{name}={shlex.join(command)}")
        return output_dir

    if output_dir.exists():
        raise TrainingRunError(
            f"output directory already exists: {output_dir}\n"
            "choose a new --run-id or --output-dir; existing runs are never overwritten"
        )
    for directory in (
        paths["normalized"].parent,
        paths["dataset"].parent,
        paths["weights"].parent,
        paths["import_log"].parent,
    ):
        directory.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.manifest, paths["source_manifest"])

    report: dict[str, Any] = {
        "schema_version": 1,
        "runner": "egaroucid-board-score-training-v1",
        "status": "running",
        "created_at_utc": datetime.now(UTC).replace(microsecond=0).isoformat(),
        "source": {
            "corpus": path_for_report(layout["corpus"], layout),
            "manifest": path_for_report(paths["source_manifest"], layout),
        },
        "configuration": {
            "positions_per_phase": args.positions_per_phase,
            "max_source_files": args.max_source_files,
            "pattern_set": args.pattern_set,
            "trainer_mode": args.trainer_mode,
            "epochs": args.epochs,
            "learning_rate": args.learning_rate,
            "weight_decay": args.weight_decay,
            "seed": args.seed,
        },
        "commands": {
            name: command_for_report(command, layout) for name, command in commands.items()
        },
        "stages": {
            "import": {"status": "pending"},
            "dataset": {"status": "pending"},
            "train": {"status": "pending"},
        },
        "outputs": {
            name: path_for_report(path, layout)
            for name, path in paths.items()
            if name not in {"import_log", "dataset_log", "train_log"}
        },
        "notes": [
            "all outputs are local-only and outside the git repository",
            "the raw source archive is read directly and is never extracted or modified",
            "this training run is not a strength or publication claim",
        ],
    }
    atomic_write_json(paths["run_report"], report)

    try:
        run_logged_stage(
            name="import",
            command=commands["import"],
            log_path=paths["import_log"],
        )
        import_report = load_json(paths["import_report"])
        report["stages"]["import"] = {
            "status": "complete",
            "emitted_positions": import_report.get("emitted_positions"),
            "source_checksum": import_report.get("source_checksum"),
            "output_checksum": import_report.get("output_checksum"),
            "source_scan_complete": import_report.get("source_scan_complete"),
        }
        atomic_write_json(paths["run_report"], report)

        run_logged_stage(
            name="dataset",
            command=commands["dataset"],
            log_path=paths["dataset_log"],
            stdout_path=paths["dataset"],
        )
        dataset_report = load_json(paths["dataset_report"])
        report["stages"]["dataset"] = {
            "status": "complete",
            "example_rows": dataset_report.get("example_rows"),
            "pattern_set_id": dataset_report.get("pattern_set_id"),
            "pattern_contract_digest": dataset_report.get("pattern_contract_digest"),
            "checksum": dataset_report.get("checksum"),
        }
        atomic_write_json(paths["run_report"], report)

        run_logged_stage(
            name="train",
            command=commands["train"],
            log_path=paths["train_log"],
        )
        train_report = load_json(paths["train_report"])
        report["stages"]["train"] = {
            "status": "complete",
            "trainer_algorithm": train_report.get("trainer_algorithm"),
            "trained_phases": train_report.get("trained_phases"),
        }
        report["status"] = "complete"
        report["completed_at_utc"] = datetime.now(UTC).replace(microsecond=0).isoformat()
        atomic_write_json(paths["run_report"], report)
    except (OSError, TrainingRunError) as error:
        report["status"] = "failed"
        report["error"] = str(error)
        atomic_write_json(paths["run_report"], report)
        raise

    print(f"training run complete: {output_dir}")
    return output_dir


def main() -> int:
    args = parse_args()
    try:
        run_training(args)
    except (OSError, TrainingRunError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
