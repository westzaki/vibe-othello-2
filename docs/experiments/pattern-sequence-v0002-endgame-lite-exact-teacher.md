# Pattern Sequence v0002 Endgame-Lite Exact-Teacher Diagnostic

## Scope

This is a local-only fitting diagnostic for `pattern-v2-endgame-lite` on a
bounded late-phase exact-teacher subset. It is not a strength claim, Elo
result, promotion-grade match bench, self-play result, production artifact, or
publication gate.

The diagnostic compares observed transcript labels with exact endgame teacher
labels, and compares the new pattern set with the existing
`pattern-v1-buro-lite` baseline on the same selected low-empty boards.

Generated teacher labels, datasets, reports, logs, weights, and artifacts stay
outside the repository.

## Pattern Set

`pattern-v2-endgame-lite` preserves the `pattern-v1-buro-lite` ordered
families, then appends bounded endgame-oriented raw tables.

| Family | Length | Instances | Table size |
| --- | ---: | ---: | ---: |
| `edge-8` | 8 | 4 | 6,561 |
| `near-edge-8` | 8 | 4 | 6,561 |
| `diagonal-8` | 8 | 2 | 6,561 |
| `diagonal-7` | 7 | 4 | 2,187 |
| `corner-2x5` | 10 | 8 | 59,049 |
| `corner-3x3` | 9 | 4 | 19,683 |
| `corner-2x4-8` | 8 | 8 | 6,561 |
| `edge-plus-x-10` | 10 | 4 | 59,049 |
| `corner-wing-8` | 8 | 8 | 6,561 |
| `near-edge-segment-8` | 8 | 8 | 6,561 |
| `diagonal-corner-8` | 8 | 4 | 6,561 |

Summary:

* feature occurrences per example: 58
* table entries per phase, excluding bias: 185,895
* phase stride, including bias: 185,896
* 13-phase runtime weight slots: 2,416,648
* approximate binary artifact size: 9.2 MiB

## Commands

The v1 baseline report was generated with the same campaign helper and
`--pattern-set buro-lite`. The v2 run used:

```sh
python3 tools/pattern/labels/run_exact_teacher_late_phase_campaign.py \
  --local-training-run-report "$VIBE_OTHELLO_MEASUREMENTS/connected-100k-v0c-baseline-corpus/runs/connected-100k-v0c-baseline-corpus-100k/local-training-run-report.json" \
  --output-dir "$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v2/connected-100k-corpus-maxempty12-cap5000" \
  --max-empty 12 \
  --max-positions 5000 \
  --seed 0 \
  --trainer-mode pattern-sgd-v0c \
  --epochs 4 \
  --learning-rate 0.05 \
  --weight-decay 0.0 \
  --dataset-output-format compact-tsv \
  --pattern-set pattern-v2-endgame-lite
```

The input run used connected-board-game split policy and a 100k
sequence-derived corpus sample. The campaign selected 5,000 unique boards from
22,954 eligible low-empty boards. Exact teacher generation solved all selected
boards with 58,756,120 total teacher nodes and no solve failures.

## Results

Primary metric: best validation MAE. Test MAE is reporting only.

| Case | Best validation MAE | Test MAE | Train MAE | Validation sign accuracy | Test sign accuracy |
| --- | ---: | ---: | ---: | ---: | ---: |
| v1 observed | 9.982284 | 10.419655 | 8.727216 | 0.828185 | 0.827004 |
| v1 exact teacher | 9.982500 | 10.429630 | 8.731454 | 0.828185 | 0.829113 |
| v2 observed | 9.617534 | 9.930017 | 8.112899 | 0.824324 | 0.879747 |
| v2 exact teacher | 9.615624 | 9.940936 | 8.116274 | 0.824324 | 0.879747 |

Decision checks:

* v2 exact teacher beats v1 exact teacher by 0.366876 validation MAE, about
  3.68 percent relative.
* v2 exact teacher beats v2 observed by 0.001910 validation MAE, about
  0.020 percent relative.
* The v2 exact-over-observed improvement does not meet the meaningful
  threshold of 0.2 MAE absolute or 1 percent relative.
* Test MAE moves with v2 versus v1, but exact teacher is slightly worse than
  observed within v2 on test MAE.

Late phase validation MAE:

| Pattern set / label | Phase 10 | Phase 11 | Phase 12 |
| --- | ---: | ---: | ---: |
| v1 observed | 12.123083 | 10.306777 | 8.253351 |
| v1 exact teacher | 12.137311 | 10.300275 | 8.252127 |
| v2 observed | 11.626187 | 9.776956 | 8.157337 |
| v2 exact teacher | 11.644805 | 9.762637 | 8.156185 |

## Short Late-Game Arena Check

A follow-up regression arena used exported local artifacts from the same v1/v2
observed and exact-teacher trainer JSON files. It generated 24 deterministic
random legal 48-ply openings, played each with swapped colors, and searched the
remaining late game at fixed depth 3 through `vibe-othello-engine-cli` using
pattern artifact evaluation.

Candidate was `pattern-v2-endgame-lite`; baseline was `pattern-v1-buro-lite`.

| Label source | Games | Candidate W-D-L | Candidate score | Win rate | Avg disc diff | Invalid games |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| observed | 48 | 25-1-22 | 25.5/48 | 0.520833 | 0.9375 | 0 |
| exact teacher | 48 | 25-1-22 | 25.5/48 | 0.520833 | 0.9375 | 0 |

This is a weak positive late-game regression signal for v2, not a strength
claim. The observed and exact-teacher artifacts produced the same game outcomes
in this small check, so the arena result supports the pattern-set capacity jump
more than an exact-label jump.

A later persistent artifact-loaded arena over 1,000 deterministic selected
late-game boards with side-swapped pairings did not preserve this small positive
signal; see
`docs/experiments/pattern-artifact-arena-v2-vs-v1-late-game-1000.md`.

## Interpretation

The pattern-set jump happened: `pattern-v2-endgame-lite` is a better fitting
schema than `pattern-v1-buro-lite` on this bounded diagnostic, the test metrics
move in the same broad direction, and a short late-game arena was weakly
positive.

The exact-teacher jump did not happen: exact labels barely improved validation
MAE within v2 and did not meet the configured threshold. This suggests the new
bounded endgame capacity helps the fitting problem, but the current v0c trainer
and selected label overlay still do not show a meaningful exact-label benefit.

## Caveats

* This is one bounded local diagnostic, not a full-corpus result.
* Metrics are fitting diagnostics only.
* The trainer is still local research infrastructure.
* The arena check is small, deterministic, and late-game-only.
* No learned weights or teacher labels from this run are committed.
* The v2 feature set increases feature occurrences from 26 to 58 per example.

## Next Action

Prefer a trainer or sampling follow-up before adding more pattern families:
repeat v2 on a larger or all-eligible low-empty selection, compare v0c and v0d,
and inspect whether the exact-label signal appears with stronger train/validation
coverage. For strength-facing work, run a larger opening suite through a
persistent artifact-loaded engine command before considering further schema
growth.
