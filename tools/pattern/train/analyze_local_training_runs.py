#!/usr/bin/env python3
"""Compare and summarize local training run reports."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REQUIRED_REPORT_FIELDS = (
    "schema_version",
    "run_id",
    "source_kind",
    "input_mode",
    "sample_policy",
    "sample_counts_by_split",
    "sample_counts_by_phase",
    "trainer_version",
    "trainer_args",
    "trainer_report_checksum",
    "weights_checksum",
    "artifact_checksum",
    "notes",
)
SPLITS = ("train", "validation", "test")
PHASE_COVERAGE_RATIO_LIMIT = 10.0
KNOWN_SOURCE_KINDS = {"egaroucid-local", "egaroucid-sequence-local"}


@dataclass(frozen=True)
class LoadedReport:
    path: Path
    data: dict[str, Any]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--report",
        action="append",
        type=Path,
        default=[],
        help="Path to one local-training-run-report.json. May be repeated.",
    )
    parser.add_argument(
        "--reports-dir",
        action="append",
        type=Path,
        default=[],
        help="Directory searched recursively for local-training-run-report.json files.",
    )
    parser.add_argument("--json-out", type=Path, help="Write machine-readable summary JSON.")
    parser.add_argument("--markdown-out", type=Path, help="Write Markdown comparison summary.")
    parser.add_argument(
        "--min-train-rows",
        type=int,
        default=10_000,
        help="Warn when train sampled rows are below this threshold.",
    )
    args = parser.parse_args()
    if not args.report and not args.reports_dir:
        parser.error("at least one --report or --reports-dir is required")
    if args.min_train_rows < 0:
        parser.error("--min-train-rows must be non-negative")
    return args


def stable_json(data: Any) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def load_json_object(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"{path}: could not read JSON object: {error}") from error
    if not isinstance(data, dict):
        raise RuntimeError(f"{path}: JSON root must be an object")
    return data


def discover_reports(report_paths: list[Path], report_dirs: list[Path]) -> list[Path]:
    paths = list(report_paths)
    for directory in report_dirs:
        paths.extend(directory.rglob("local-training-run-report.json"))
    unique_paths = {path.resolve(): path for path in paths}
    return [unique_paths[key] for key in sorted(unique_paths, key=lambda path: str(path))]


def load_reports(paths: list[Path]) -> list[LoadedReport]:
    reports = [LoadedReport(path=path, data=load_json_object(path)) for path in paths]
    reports.sort(key=lambda item: (str(item.path), str(item.data.get("run_id", ""))))
    return reports


def validate_string(value: Any) -> bool:
    return isinstance(value, str) and bool(value)


def validate_report(report: LoadedReport) -> list[str]:
    errors: list[str] = []
    data = report.data
    for key in REQUIRED_REPORT_FIELDS:
        if key not in data:
            errors.append(f"missing required field: {key}")
    if "evaluation_smoke_summary" in data and data.get("evaluation_smoke_summary") is not None:
        if not isinstance(data.get("evaluation_smoke_summary"), dict):
            errors.append("evaluation_smoke_summary must be an object or null")
    if "search_smoke_summary" in data and data.get("search_smoke_summary") is not None:
        if not isinstance(data.get("search_smoke_summary"), dict):
            errors.append("search_smoke_summary must be an object or null")
    if "sequence_cache" in data and data.get("sequence_cache") is not None:
        cache = data.get("sequence_cache")
        if not isinstance(cache, dict):
            errors.append("sequence_cache must be an object or null")
        elif cache.get("status") not in (None, "hit", "miss", "invalidated", "bypassed"):
            errors.append("sequence_cache.status must be hit, miss, invalidated, or bypassed")
    if "stage_timings" in data and data.get("stage_timings") is not None:
        stages = data.get("stage_timings")
        if not isinstance(stages, dict):
            errors.append("stage_timings must be an object or null")
        else:
            for stage_name, stage_data in stages.items():
                if not isinstance(stage_name, str) or not isinstance(stage_data, dict):
                    errors.append("stage_timings entries must be named objects")
                    break
                wall = stage_data.get("wall_time_sec")
                if wall is not None and not isinstance(wall, (int, float)):
                    errors.append(f"stage_timings.{stage_name}.wall_time_sec must be numeric or null")
    if "dataset_output_format" in data and data.get("dataset_output_format") is not None:
        if data.get("dataset_output_format") not in ("expanded-tsv", "compact-tsv"):
            errors.append("dataset_output_format must be expanded-tsv or compact-tsv")
    if not isinstance(data.get("schema_version"), int):
        errors.append("schema_version must be an integer")
    for key in ("run_id", "source_kind", "input_mode", "trainer_version"):
        if not validate_string(data.get(key)):
            errors.append(f"{key} must be a non-empty string")
    for key in ("sample_policy", "sample_counts_by_split", "sample_counts_by_phase", "trainer_args"):
        if not isinstance(data.get(key), dict):
            errors.append(f"{key} must be an object")
    for key in ("trainer_report_checksum", "weights_checksum", "artifact_checksum"):
        value = data.get(key)
        if value is not None and not isinstance(value, str):
            errors.append(f"{key} must be a string or null")
    if not isinstance(data.get("notes"), list):
        errors.append("notes must be an array")
    sample_policy = data.get("sample_policy")
    if isinstance(sample_policy, dict) and "strict_board_disjoint_splits" in sample_policy:
        if not isinstance(sample_policy.get("strict_board_disjoint_splits"), bool):
            errors.append("sample_policy.strict_board_disjoint_splits must be a boolean")
    return errors


def int_or_none(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value)
        except ValueError:
            return None
    return None


def count_from_summary(summary: Any, key: str) -> int | None:
    if not isinstance(summary, dict):
        return None
    nested = summary.get("summary")
    if isinstance(nested, dict) and key in nested:
        return int_or_none(nested.get(key))
    return int_or_none(summary.get(key))


def metric_value(metrics: dict[str, Any] | None, key: str) -> Any:
    if not isinstance(metrics, dict):
        return None
    return metrics.get(key)


def empty_split_metrics() -> dict[str, dict[str, Any]]:
    return {
        split: {"MAE": None, "RMSE": None, "sign_accuracy": None}
        for split in SPLITS
    }


def extract_metrics(trainer_report: dict[str, Any] | None) -> dict[str, Any]:
    metrics_by_split = empty_split_metrics()
    metrics_by_phase: dict[str, dict[str, Any]] = {}
    if trainer_report is None:
        return {
            "train": metrics_by_split["train"],
            "validation": metrics_by_split["validation"],
            "test": metrics_by_split["test"],
            "by_phase": metrics_by_phase,
        }

    metrics_root = trainer_report.get("final_pattern_sgd_metrics")
    if not isinstance(metrics_root, dict):
        metrics_root = trainer_report
    split_metrics = metrics_root.get("metrics_by_split")
    if isinstance(split_metrics, dict):
        for split in SPLITS:
            split_data = split_metrics.get(split)
            metrics_by_split[split] = {
                "MAE": metric_value(split_data, "MAE"),
                "RMSE": metric_value(split_data, "RMSE"),
                "sign_accuracy": metric_value(split_data, "sign_accuracy"),
            }
    phase_metrics = metrics_root.get("metrics_by_phase")
    if isinstance(phase_metrics, dict):
        for phase in sorted(phase_metrics, key=lambda value: int(value) if str(value).isdigit() else str(value)):
            phase_data = phase_metrics.get(phase)
            metrics_by_phase[str(phase)] = {
                "MAE": metric_value(phase_data, "MAE"),
                "RMSE": metric_value(phase_data, "RMSE"),
                "sign_accuracy": metric_value(phase_data, "sign_accuracy"),
            }
    return {
        "train": metrics_by_split["train"],
        "validation": metrics_by_split["validation"],
        "test": metrics_by_split["test"],
        "by_phase": metrics_by_phase,
    }


def trainer_report_path(run_report: LoadedReport) -> Path | None:
    output_files = run_report.data.get("output_files")
    if not isinstance(output_files, dict):
        return None
    relative = output_files.get("v0b_trainer_report_json")
    if not isinstance(relative, str) or not relative:
        return None
    path = Path(relative)
    if path.is_absolute():
        return path
    return run_report.path.parent / path


def load_trainer_report(run_report: LoadedReport) -> dict[str, Any] | None:
    path = trainer_report_path(run_report)
    if path is None or not path.exists():
        return None
    return load_json_object(path)


def warning(code: str, message: str) -> dict[str, str]:
    return {"code": code, "message": message}


def warnings_for_run(data: dict[str, Any], min_train_rows: int) -> list[dict[str, str]]:
    warnings: list[dict[str, str]] = []
    counts_by_split = data.get("sample_counts_by_split")
    counts_by_phase = data.get("sample_counts_by_phase")
    train_rows = int_or_none(counts_by_split.get("train")) if isinstance(counts_by_split, dict) else None
    validation_rows = (
        int_or_none(counts_by_split.get("validation")) if isinstance(counts_by_split, dict) else None
    )
    test_rows = int_or_none(counts_by_split.get("test")) if isinstance(counts_by_split, dict) else None
    if train_rows is None or train_rows < min_train_rows:
        warnings.append(
            warning(
                "train_rows_too_small",
                f"train rows are below {min_train_rows}: {train_rows}",
            )
        )
    if validation_rows in (None, 0):
        warnings.append(warning("validation_empty", "validation split is empty"))
    if test_rows in (None, 0):
        warnings.append(warning("test_empty", "test split is empty"))
    if isinstance(counts_by_phase, dict):
        phase_counts = [
            count
            for count in (int_or_none(value) for value in counts_by_phase.values())
            if count is not None and count > 0
        ]
        if len(phase_counts) < 3:
            warnings.append(
                warning("phase_coverage_biased", "fewer than three phases have sampled rows")
            )
        elif min(phase_counts) == 0 or max(phase_counts) / min(phase_counts) > PHASE_COVERAGE_RATIO_LIMIT:
            warnings.append(
                warning(
                    "phase_coverage_biased",
                    f"phase row ratio exceeds {PHASE_COVERAGE_RATIO_LIMIT:g}",
                )
            )
    else:
        warnings.append(warning("phase_coverage_biased", "phase counts are missing"))

    eval_summary = data.get("evaluation_smoke_summary")
    search_summary = data.get("search_smoke_summary")
    if not isinstance(eval_summary, dict):
        warnings.append(warning("evaluation_smoke_missing", "evaluation smoke summary is missing"))
    elif count_from_summary(eval_summary, "v0a_v0b_different_count") == 0:
        warnings.append(
            warning("evaluation_score_difference_zero", "evaluation v0a/v0b difference count is zero")
        )
    if not isinstance(search_summary, dict):
        warnings.append(warning("search_smoke_missing", "search smoke summary is missing"))
    elif count_from_summary(search_summary, "v0a_v0b_score_different_count") == 0:
        warnings.append(
            warning("search_score_difference_zero", "search v0a/v0b score difference count is zero")
        )
    if not validate_string(data.get("artifact_checksum")):
        warnings.append(warning("artifact_checksum_missing", "artifact checksum is missing"))
    if data.get("source_kind") not in KNOWN_SOURCE_KINDS:
        warnings.append(
            warning("source_kind_unknown", "source_kind is not a known local Egaroucid source")
        )
    audit = data.get("board_leakage_audit")
    if isinstance(audit, dict) and int_or_none(audit.get("cross_split_board_collision_count")):
        warnings.append(
            warning(
                "exact_board_cross_split_collision",
                "exact side-to-move-relative boards appear in more than one split",
            )
        )
    if "stage_timings" not in data:
        warnings.append(warning("telemetry_missing", "stage timing telemetry is missing"))
    elif not isinstance(data.get("stage_timings"), dict):
        warnings.append(warning("telemetry_invalid", "stage timing telemetry is not an object"))
    if data.get("input_mode") == "sequence-input":
        cache = data.get("sequence_cache")
        if not isinstance(cache, dict):
            warnings.append(warning("sequence_cache_missing", "sequence cache summary is missing"))
    return warnings


def cache_status(data: dict[str, Any]) -> str | None:
    cache = data.get("sequence_cache")
    if not isinstance(cache, dict):
        return None
    value = cache.get("status")
    return value if isinstance(value, str) else None


def dataset_output_format(data: dict[str, Any]) -> str:
    value = data.get("dataset_output_format")
    return value if value in ("expanded-tsv", "compact-tsv") else "unknown"


def stage_wall(data: dict[str, Any], *names: str) -> float | None:
    stages = data.get("stage_timings")
    if not isinstance(stages, dict):
        return None
    total = 0.0
    seen = False
    for name in names:
        stage = stages.get(name)
        if not isinstance(stage, dict):
            continue
        value = stage.get("wall_time_sec")
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            total += float(value)
            seen = True
    if not seen:
        return None
    return float(f"{total:.6g}")


def run_summary(run_report: LoadedReport, min_train_rows: int) -> dict[str, Any]:
    data = run_report.data
    trainer_report = load_trainer_report(run_report)
    counts_by_split = data.get("sample_counts_by_split")
    counts_by_phase = data.get("sample_counts_by_phase")
    train_rows = int_or_none(counts_by_split.get("train")) if isinstance(counts_by_split, dict) else None
    sampled_rows = (
        sum(value for value in (int_or_none(item) for item in counts_by_split.values()) if value is not None)
        if isinstance(counts_by_split, dict)
        else None
    )
    eval_summary = data.get("evaluation_smoke_summary")
    search_summary = data.get("search_smoke_summary")
    board_leakage_audit = data.get("board_leakage_audit")
    return {
        "artifact_checksum": data.get("artifact_checksum"),
        "board_leakage_audit": board_leakage_audit if isinstance(board_leakage_audit, dict) else None,
        "counts_by_phase": counts_by_phase if isinstance(counts_by_phase, dict) else None,
        "counts_by_split": counts_by_split if isinstance(counts_by_split, dict) else None,
        "created_at_utc": data.get("created_at_utc"),
        "dataset_output_format": dataset_output_format(data),
        "evaluation_smoke": {
            "positions_count": count_from_summary(eval_summary, "positions_count"),
            "v0a_v0b_different_count": count_from_summary(
                eval_summary, "v0a_v0b_different_count"
            ),
            "input_positions": int_or_none(data.get("eval_smoke_input_positions")),
            "used_positions": int_or_none(data.get("eval_smoke_used_positions")),
        },
        "metrics": extract_metrics(trainer_report),
        "path": str(run_report.path),
        "run_id": data.get("run_id"),
        "sampled_rows": sampled_rows,
        "cache_status": cache_status(data),
        "stage_wall_times": {
            "import_or_cache_restore": stage_wall(data, "sequence_import_or_cache_restore"),
            "dataset": stage_wall(data, "pattern_dataset_generation"),
            "trainer": stage_wall(data, "trainer_v0b"),
            "export": stage_wall(data, "v0b_export"),
            "evaluation_search_smoke": stage_wall(data, "evaluation_smoke", "search_smoke"),
            "total_recorded": stage_wall(
                data,
                "source_hashing",
                "sequence_cache_lookup",
                "sequence_import_or_cache_restore",
                "normalized_sampling",
                "pattern_dataset_generation",
                "trainer_v0b",
                "v0b_export",
                "v0a_baseline_training_export",
                "evaluation_smoke",
                "search_smoke",
            ),
        },
        "search_smoke": {
            "positions_count": count_from_summary(search_summary, "positions_count"),
            "v0a_v0b_best_move_different_count": count_from_summary(
                search_summary, "v0a_v0b_best_move_different_count"
            ),
            "v0a_v0b_score_different_count": count_from_summary(
                search_summary, "v0a_v0b_score_different_count"
            ),
            "input_positions": int_or_none(data.get("search_smoke_input_positions")),
            "used_positions": int_or_none(data.get("search_smoke_used_positions")),
        },
        "source_kind": data.get("source_kind"),
        "train_rows": train_rows,
        "trainer_args": data.get("trainer_args"),
        "trainer_report_checksum": data.get("trainer_report_checksum"),
        "warnings": warnings_for_run(data, min_train_rows),
    }


def sort_run_summaries(summaries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return sorted(summaries, key=lambda item: (str(item.get("path", "")), str(item.get("run_id", ""))))


def add_comparison_warnings(runs: list[dict[str, Any]]) -> None:
    statuses = {
        run.get("cache_status")
        for run in runs
        if run.get("cache_status") in {"hit", "miss", "invalidated"}
    }
    if "hit" in statuses and ("miss" in statuses or "invalidated" in statuses):
        for run in runs:
            if run.get("cache_status") in {"hit", "miss", "invalidated"}:
                run.setdefault("warnings", []).append(
                    warning(
                        "cache_status_mixed_comparison",
                        "cache-hit and cache-miss/invalidated runs are being compared directly",
                    )
                )
    dataset_formats = {
        run.get("dataset_output_format")
        for run in runs
        if run.get("dataset_output_format") in {"expanded-tsv", "compact-tsv"}
    }
    if len(dataset_formats) > 1:
        for run in runs:
            if run.get("dataset_output_format") in dataset_formats:
                run.setdefault("warnings", []).append(
                    warning(
                        "dataset_output_format_mixed_comparison",
                        "expanded and compact pattern datasets are being compared directly",
                    )
                )


def markdown_table_row(values: list[Any]) -> str:
    return "| " + " | ".join("" if value is None else str(value) for value in values) + " |"


def render_markdown(summary: dict[str, Any]) -> str:
    runs = summary["runs"]
    lines = [
        "# Local Training Run Analysis",
        "",
        f"Run count: {summary['run_count']}",
        "",
        "## Comparison",
        "",
        markdown_table_row(
            [
                "run_id",
                "sampled_rows",
                "dataset_format",
                "cache_status",
                "import/cache_restore_sec",
                "dataset_sec",
                "trainer_sec",
                "export_sec",
                "eval/search_sec",
                "artifact_checksum",
                "warnings",
            ]
        ),
        markdown_table_row(["---"] * 11),
    ]
    for run in runs:
        stage_times = run.get("stage_wall_times") or {}
        lines.append(
            markdown_table_row(
                [
                    run.get("run_id"),
                    run.get("sampled_rows"),
                    run.get("dataset_output_format"),
                    run.get("cache_status"),
                    stage_times.get("import_or_cache_restore"),
                    stage_times.get("dataset"),
                    stage_times.get("trainer"),
                    stage_times.get("export"),
                    stage_times.get("evaluation_search_smoke"),
                    run.get("artifact_checksum"),
                    ", ".join(item["code"] for item in run.get("warnings", [])),
                ]
            )
        )
    lines.extend(["", "## Trainer Args", ""])
    for run in runs:
        lines.append(f"### {run.get('run_id')}")
        lines.append("")
        lines.append("```json")
        lines.append(stable_json(run.get("trainer_args")).rstrip())
        lines.append("```")
        lines.append("")
    lines.extend(["## Warnings", ""])
    for run in runs:
        lines.append(f"### {run.get('run_id')}")
        lines.append("")
        warnings = run.get("warnings", [])
        if not warnings:
            lines.append("- none")
        else:
            for item in warnings:
                lines.append(f"- {item['code']}: {item['message']}")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def write_output(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def main() -> int:
    args = parse_args()
    try:
        paths = discover_reports(args.report, args.reports_dir)
        if not paths:
            raise RuntimeError("no local-training-run-report.json files found")
        reports = load_reports(paths)
        validation_errors = {
            str(report.path): validate_report(report) for report in reports
        }
        validation_errors = {
            path: errors for path, errors in validation_errors.items() if errors
        }
        if validation_errors:
            raise RuntimeError(stable_json({"validation_errors": validation_errors}).rstrip())
        runs = sort_run_summaries(
            [run_summary(report, args.min_train_rows) for report in reports]
        )
        add_comparison_warnings(runs)
        summary = {
            "run_count": len(runs),
            "runs": runs,
            "schema_version": 1,
            "warning_count": sum(len(run["warnings"]) for run in runs),
        }
        if args.json_out is not None:
            write_output(args.json_out, stable_json(summary))
        if args.markdown_out is not None:
            write_output(args.markdown_out, render_markdown(summary))
        if args.json_out is None and args.markdown_out is None:
            sys.stdout.write(stable_json(summary))
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
