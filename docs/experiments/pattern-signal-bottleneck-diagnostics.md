# Pattern Signal Bottleneck Diagnostics

## Scope

This note records a local-only diagnostic pass for the gap between
`pattern-v2-endgame-lite` fitting metrics and persistent artifact arena play
signal.

It is a bottleneck diagnostic only. It is not Elo, not self-play, not a
production strength claim, and not a publication gate. Generated artifacts,
reports, logs, and raw corpus payloads remain local-only and are not committed.

## Inputs

The diagnostic used local exact-teacher v0c-compatible JSON weights exported
to runtime artifacts with `tools/pattern/export/export_v0b.py`.

Sanitized command shape:

```sh
export VIBE_OTHELLO_LOCAL="<local-root>"
export VIBE_OTHELLO_MEASUREMENTS="$VIBE_OTHELLO_LOCAL/measurements"

python3 tools/pattern/export/export_v0b.py \
  --weights-json "$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v1/<run>/exact-teacher-weights.json" \
  --pattern-set pattern-v1-buro-lite \
  --weights-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.weights.bin" \
  --manifest-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.manifest.json"

python3 tools/pattern/export/export_v0b.py \
  --weights-json "$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v2/<run>/exact-teacher-weights.json" \
  --pattern-set pattern-v2-endgame-lite \
  --weights-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v2-exact-teacher.weights.bin" \
  --manifest-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v2-exact-teacher.manifest.json"
```

The arena diagnostics used the connected 100k normalized position pool with
`--max-empty 12`, `--max-positions 1000`, `--seed 0`, `--side-swap`, and
`--depth 3`. The v2/v1 run also enabled `--depth-sweep 1,3,5` and
`--exact-adjudicate-disagreements --max-disagreements 200`.

Sanitized command shape:

```sh
build/tools/arena/vibe-othello-pattern-artifact-arena \
  --positions-tsv "$VIBE_OTHELLO_MEASUREMENTS/<connected-100k-run>/runs/<run-id>/resplit-normalized.tsv" \
  --candidate-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v2-exact-teacher.weights.bin" \
  --candidate-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v2-exact-teacher.manifest.json" \
  --candidate-name pattern-v2-endgame-lite \
  --baseline-weights "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.weights.bin" \
  --baseline-manifest "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts/v1-exact-teacher.manifest.json" \
  --baseline-name pattern-v1-buro-lite \
  --max-empty 12 \
  --max-positions 1000 \
  --seed 0 \
  --side-swap \
  --depth 3 \
  --diagnostics-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/swap-v2-v1/diagnostics.json" \
  --compare-static-scores \
  --compare-best-moves \
  --depth-sweep 1,3,5 \
  --exact-adjudicate-disagreements \
  --max-disagreements 200 \
  --report-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/swap-v2-v1/arena-report.json" \
  --summary-out "$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/swap-v2-v1/arena-summary.md"
```

## Same-Artifact Sanity

Both same-artifact mirror runs tied exactly under side swap.

| Run | Games | Score rate | Avg disc diff | Static diffs | Best-move disagreements |
| --- | ---: | ---: | ---: | ---: | ---: |
| v1 vs v1 | 2000 | 0.500000 | 0.000000 | 0 / 1000 nonzero | 0 / 1000 |
| v2 vs v2 | 2000 | 0.500000 | 0.000000 | 0 / 1000 nonzero | 0 / 1000 |

The side-assignment buckets were not individually 0.5 even for identical
artifacts. For v1/v1, candidate-side-to-move scored 0.437 and
candidate-opponent scored 0.563; v2/v2 showed the same pattern. Because the
aggregate tied exactly and candidate/baseline swaps complemented, this is
interpreted as selected-position side-assignment structure rather than a
candidate/baseline perspective bug.

## Swap Check

The v2/v1 and v1/v2 runs complemented exactly at depth 3.

| Run | Games | Score rate | Avg disc diff |
| --- | ---: | ---: | ---: |
| v2 vs v1 | 2000 | 0.501750 | 0.167000 |
| v1 vs v2 | 2000 | 0.498250 | -0.167000 |

This does not show a candidate/baseline side assignment bug.

## Depth Sweep

| Depth | v2 score rate | Avg disc diff | Best-move disagreement rate |
| ---: | ---: | ---: | ---: |
| 1 | 0.496000 | -0.156000 | 0.272 |
| 3 | 0.501750 | 0.167000 | 0.195 |
| 5 | 0.501000 | 0.082500 | 0.146 |

Depth changes the amount of root disagreement, but none of these depths shows a
clear positive arena signal. The signal is not hidden only by depth 3.

## Static Score And Move Disagreement

On the connected 1000-position arena sample:

| Metric | Value |
| --- | ---: |
| static score diff nonzero count | 890 / 1000 |
| static score diff abs mean | 3.055 discs |
| static score diff abs median | 3.0 discs |
| v2 static score range | [-80, 80] |
| v1 static score range | [-82, 86] |
| best-move disagreement count | 195 / 1000 |
| search score diff abs mean | 2.204 discs |
| static/search ordering agreement rate | 0.405 |

Static scores differ on most selected positions, so the signal is not lost by
artifact loading, pattern-set selection, or all-zero runtime scoring. However,
roughly 80% of selected roots keep the same best move at depth 3.

When best moves differ, exact adjudication favored v2 but not strongly enough
to move the full arena result:

| Disagreement exact result | Count |
| --- | ---: |
| v2 move better | 100 |
| v1 move better | 71 |
| draw | 24 |

The exact margin over disagreements averaged +1.55 discs for v2. Spread across
all 1000 roots, the decision-level advantage is small.

## Exact-Label Sanity

The exact-teacher-overlaid low-empty fixture was also checked with 1000 selected
positions. It reproduced the inconclusive arena signal:

| Metric | Value |
| --- | ---: |
| v2 score rate | 0.500500 |
| avg disc diff | 0.028500 |
| static score diff nonzero count | 899 / 1000 |
| static score diff abs mean | 2.921 discs |
| best-move disagreement count | 201 / 1000 |
| exact checked disagreements | 200 |
| v2 better / v1 better / draw | 96 / 71 / 33 |
| v2 static sign agreement with exact labels | 861 / 948 checked |
| v2 static sign opposition with exact labels | 87 / 948 checked |

This does not look like an obvious static score sign inversion against exact
labels.

## Feature Activation

All v2-added families activated on the selected late-game positions.

| v2-added family | Instances evaluated | Non-empty activations | Distinct ternary indices |
| --- | ---: | ---: | ---: |
| corner-2x4-8 | 8000 | 7996 | 1414 |
| edge-plus-x-10 | 4000 | 4000 | 1866 |
| corner-wing-8 | 8000 | 7994 | 1465 |
| near-edge-segment-8 | 8000 | 8000 | 1023 |
| diagonal-corner-8 | 4000 | 4000 | 842 |

The v2-added features are wired into runtime geometry and active on the
late-game arena distribution.

## Conclusion

No small confirmed bug was found in export/load compatibility, manifest/runtime
pattern-set selection, phase mapping, static score sign convention,
side-assignment swap accounting, or v2-added feature activation.

The most likely bottleneck is decision leverage: v2 produces real static score
differences, and those differences sometimes choose better exact moves, but the
score deltas are small and only about 20% of selected roots change best move at
depth 3. The disagreement subset has a mild v2 edge, yet the advantage is too
small and too sparse to become a clear side-swapped arena signal.

The side-assignment asymmetry seen in individual buckets is expected for this
selected-position setup because identical-artifact mirror runs show the same
bucket skew while tying exactly in aggregate.

## Next Recommended Action

Do not add more pattern families blindly. The next useful diagnostic is to rank
positions by expected decision leverage:

* bucket positions by root move margin and static score delta
* compare fitting MAE improvement separately for unchanged-best-move and
  changed-best-move positions
* inspect whether training improvements concentrate in positions where the root
  best move is stable under v1/v2 and under depth 1/3/5
* if optimizing, prefer objectives or sampling that increase correct
  high-leverage move changes rather than only reducing average static MAE
