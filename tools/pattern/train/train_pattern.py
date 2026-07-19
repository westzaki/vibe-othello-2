#!/usr/bin/env python3
"""Train tiny pattern-learning baselines from pattern rows TSV."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from dataset_contract import (
    PHASE_COUNT,
    TRAINER_ALGORITHM_V0A,
    TRAINER_ALGORITHM_V0B,
    TRAINER_ALGORITHM_V0C,
    TRAINER_ALGORITHM_V0D,
    TRAINER_ALGORITHM_V0E,
    WEIGHTS_SCHEMA_VERSION_V1,
    WEIGHTS_SCHEMA_VERSION_V2,
)
from dataset_io import load_examples, load_move_teacher_roots_many, validate_training_examples
from reports import (
    run_pattern_sgd_v0b,
    run_pattern_sgd_v0c,
    run_pattern_sgd_v0d,
    run_pattern_rank_v0e,
    run_phase_bias_v0a,
)

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", required=True, type=Path)
    parser.add_argument(
        "--mode",
        choices=(
            TRAINER_ALGORITHM_V0A,
            TRAINER_ALGORITHM_V0B,
            TRAINER_ALGORITHM_V0C,
            TRAINER_ALGORITHM_V0D,
            TRAINER_ALGORITHM_V0E,
        ),
        default=TRAINER_ALGORITHM_V0A,
    )
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.05)
    parser.add_argument("--l2", type=float, default=0.0)
    parser.add_argument(
        "--weight-decay",
        type=float,
        help="Alias for --l2; applies to pattern weights, not phase bias.",
    )
    parser.add_argument(
        "--lr-schedule",
        choices=("constant", "inverse-sqrt"),
        default="constant",
        help="Pattern-SGD learning-rate schedule.",
    )
    parser.add_argument(
        "--gradient-clip",
        type=float,
        help="Optional absolute clip applied to per-feature error gradients.",
    )
    parser.add_argument(
        "--early-stop-patience",
        type=int,
        help="Pattern-SGD validation MAE patience in epochs.",
    )
    parser.add_argument(
        "--eval-every-epoch",
        dest="eval_every_epoch",
        action="store_true",
        default=True,
        help="Evaluate train/validation diagnostics after every epoch.",
    )
    parser.add_argument(
        "--no-eval-every-epoch",
        dest="eval_every_epoch",
        action="store_false",
        help="Keep only final epoch diagnostics.",
    )
    parser.add_argument(
        "--shuffle-policy",
        choices=("deterministic",),
        default="deterministic",
        help="Deterministic shuffle is the only supported policy.",
    )
    parser.add_argument(
        "--progress-every-examples",
        type=int,
        help="Write training progress to stderr after this many train examples.",
    )
    parser.add_argument(
        "--phase-balance",
        choices=("none", "inverse-count", "sqrt-inverse-count"),
        help="Phase weighting scheme for pattern-sgd-v0d. Defaults to sqrt-inverse-count.",
    )
    parser.add_argument(
        "--max-phase-weight",
        type=float,
        help="Phase weight cap for pattern-sgd-v0d. Defaults to 4.0.",
    )
    parser.add_argument(
        "--min-phase-weight",
        type=float,
        help="Phase weight floor for pattern-sgd-v0d. Defaults to 0.25.",
    )
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument(
        "--emit-trained-phases",
        action="store_true",
        help="Include train-split child phases in non-v0e reports for artifact export workflows.",
    )
    parser.add_argument(
        "--move-teacher",
        type=Path,
        action="append",
        help="Move-teacher TSV sidecar joined to child pattern dataset by child_board_id; repeat for disjoint teacher sources; required for pattern-rank-v0e.",
    )
    parser.add_argument("--initial-weights", type=Path)
    parser.add_argument("--initial-trained-phases", nargs="+", type=int)
    parser.add_argument("--freeze-phase", action="append", default=[], type=int)
    parser.add_argument(
        "--trainable-pattern-id",
        action="append",
        default=[],
        help=(
            "Restrict pattern-rank-v0e updates to this pattern table; repeat for "
            "multiple tables. Phase bias remains trainable."
        ),
    )
    parser.add_argument(
        "--rank-temperature",
        type=float,
        help="Positive pairwise logistic-loss temperature for pattern-rank-v0e. Defaults to 1.0.",
    )
    parser.add_argument(
        "--value-loss-weight",
        type=float,
        help="Optional Huber child-value calibration coefficient for pattern-rank-v0e. Defaults to 0.05; set 0 to disable.",
    )
    parser.add_argument(
        "--pair-sampling-cap",
        type=int,
        help="Maximum non-tie pairs per root for pattern-rank-v0e. Defaults to 64; 0 keeps all pairs.",
    )
    parser.add_argument(
        "--tie-margin",
        type=float,
        help="Teacher-score difference treated as a tie for pattern-rank-v0e. Defaults to 0.",
    )
    parser.add_argument(
        "--residual-baseline-through-phase",
        type=int,
        help="Train learned values as additive residuals over the move-teacher v3 baseline through this inclusive phase.",
    )
    parser.add_argument("--weights-out", required=True, type=Path)
    parser.add_argument("--report-out", required=True, type=Path)
    parser.add_argument(
        "--weights-schema-version",
        choices=(WEIGHTS_SCHEMA_VERSION_V1, WEIGHTS_SCHEMA_VERSION_V2),
        default=WEIGHTS_SCHEMA_VERSION_V2,
    )
    parser.add_argument(
        "--dataset-report",
        type=Path,
        help="Pattern dataset report carrying pattern_set_id, pattern_contract_digest, and index_mode.",
    )
    parser.add_argument("--pattern-set-id")
    parser.add_argument("--pattern-contract-digest")
    parser.add_argument("--index-mode", choices=("raw", "canonical"))
    parser.add_argument("--phase-count", type=int)
    parser.add_argument("--phase-mapping-id")
    parser.add_argument("--score-unit")
    args = parser.parse_args()
    if args.epochs < 0:
        parser.error("--epochs must be non-negative")
    if args.learning_rate < 0.0:
        parser.error("--learning-rate must be non-negative")
    if args.l2 < 0.0:
        parser.error("--l2 must be non-negative")
    if args.weight_decay is not None and args.weight_decay < 0.0:
        parser.error("--weight-decay must be non-negative")
    if args.gradient_clip is not None and args.gradient_clip <= 0.0:
        parser.error("--gradient-clip must be positive")
    if args.early_stop_patience is not None and args.early_stop_patience < 0:
        parser.error("--early-stop-patience must be non-negative")
    if args.progress_every_examples is not None and args.progress_every_examples <= 0:
        parser.error("--progress-every-examples must be positive")
    v0d_only_args = (
        args.phase_balance is not None
        or args.max_phase_weight is not None
        or args.min_phase_weight is not None
    )
    if args.mode != TRAINER_ALGORITHM_V0D and v0d_only_args:
        parser.error("--phase-balance, --max-phase-weight, and --min-phase-weight require --mode pattern-sgd-v0d")
    if args.mode == TRAINER_ALGORITHM_V0D:
        if args.phase_balance is None:
            args.phase_balance = "sqrt-inverse-count"
        if args.max_phase_weight is None:
            args.max_phase_weight = 4.0
        if args.min_phase_weight is None:
            args.min_phase_weight = 0.25
        if args.max_phase_weight <= 0.0:
            parser.error("--max-phase-weight must be positive")
        if args.min_phase_weight <= 0.0:
            parser.error("--min-phase-weight must be positive")
        if args.min_phase_weight > args.max_phase_weight:
            parser.error("--min-phase-weight must be <= --max-phase-weight")
    v0e_only_args = (
        args.move_teacher is not None
        or args.initial_weights is not None
        or args.initial_trained_phases is not None
        or bool(args.freeze_phase)
        or bool(args.trainable_pattern_id)
        or args.rank_temperature is not None
        or args.value_loss_weight is not None
        or args.pair_sampling_cap is not None
        or args.tie_margin is not None
        or args.residual_baseline_through_phase is not None
    )
    if args.mode != TRAINER_ALGORITHM_V0E and v0e_only_args:
        parser.error(
            "--move-teacher, warm-start, and rank-objective arguments "
            "require --mode pattern-rank-v0e"
        )
    if args.mode == TRAINER_ALGORITHM_V0E:
        if args.move_teacher is None:
            parser.error("--move-teacher is required for --mode pattern-rank-v0e")
        if args.rank_temperature is None:
            args.rank_temperature = 1.0
        if args.value_loss_weight is None:
            args.value_loss_weight = 0.05
        if args.pair_sampling_cap is None:
            args.pair_sampling_cap = 64
        if args.tie_margin is None:
            args.tie_margin = 0.0
        if args.rank_temperature <= 0.0:
            parser.error("--rank-temperature must be positive")
        if args.value_loss_weight < 0.0:
            parser.error("--value-loss-weight must be non-negative")
        if args.pair_sampling_cap < 0:
            parser.error("--pair-sampling-cap must be non-negative")
        if args.tie_margin < 0.0:
            parser.error("--tie-margin must be non-negative")
        if args.residual_baseline_through_phase is not None and not (
            0 <= args.residual_baseline_through_phase < PHASE_COUNT
        ):
            parser.error("--residual-baseline-through-phase must be in [0, 12]")
        if args.initial_weights is None and (
            args.initial_trained_phases is not None or args.freeze_phase
        ):
            parser.error("--initial-trained-phases and --freeze-phase require --initial-weights")
        if args.initial_weights is not None and args.initial_trained_phases is None:
            parser.error("--initial-weights requires --initial-trained-phases")
        initial_phases = [] if args.initial_trained_phases is None else args.initial_trained_phases
        if any(phase < 0 or phase >= PHASE_COUNT for phase in initial_phases):
            parser.error("--initial-trained-phases entries must be in [0, 12]")
        if len(set(initial_phases)) != len(initial_phases):
            parser.error("--initial-trained-phases entries must be unique")
        if any(phase < 0 or phase >= PHASE_COUNT for phase in args.freeze_phase):
            parser.error("--freeze-phase entries must be in [0, 12]")
        if len(set(args.freeze_phase)) != len(args.freeze_phase):
            parser.error("--freeze-phase must not repeat a phase")
        if not set(args.freeze_phase).issubset(initial_phases):
            parser.error("--freeze-phase entries must be included in --initial-trained-phases")
        if any(not pattern_id for pattern_id in args.trainable_pattern_id):
            parser.error("--trainable-pattern-id must be non-empty")
        if len(set(args.trainable_pattern_id)) != len(args.trainable_pattern_id):
            parser.error("--trainable-pattern-id must not repeat a pattern")
        args.initial_trained_phases = sorted(initial_phases)
        args.frozen_phases = frozenset(args.freeze_phase)
        args.trainable_pattern_ids = frozenset(args.trainable_pattern_id)
    if args.phase_count is not None and args.phase_count != PHASE_COUNT:
        parser.error(f"--phase-count must be {PHASE_COUNT}")
    return args

def main() -> int:
    args = parse_args()
    try:
        load_result = load_examples(args.dataset)
        validate_training_examples(load_result)
        if args.mode == TRAINER_ALGORITHM_V0A:
            run_phase_bias_v0a(load_result, args)
        elif args.mode == TRAINER_ALGORITHM_V0B:
            run_pattern_sgd_v0b(load_result, args)
        elif args.mode == TRAINER_ALGORITHM_V0C:
            run_pattern_sgd_v0c(load_result, args)
        elif args.mode == TRAINER_ALGORITHM_V0D:
            run_pattern_sgd_v0d(load_result, args)
        elif args.mode == TRAINER_ALGORITHM_V0E:
            assert args.move_teacher is not None
            move_teacher_result = load_move_teacher_roots_many(args.move_teacher, load_result.accepted_examples)
            run_pattern_rank_v0e(load_result, move_teacher_result, args)
        else:
            raise RuntimeError(f"unsupported mode: {args.mode}")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    print(
        "summary "
        f"input_format={load_result.input_format} "
        f"input_feature_rows={load_result.input_feature_rows} "
        f"accepted_examples={len(load_result.accepted_examples)} "
        f"rejected_examples={load_result.rejected_examples} "
        f"rejected_feature_rows={load_result.rejected_feature_rows} "
        f"trainer_algorithm={args.mode}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
