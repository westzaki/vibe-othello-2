# Pattern Artifact Arena v2 vs v1 Late-Game 1000 Diagnostic

## Scope

This is a local-only artifact-vs-artifact late-game diagnostic using the
persistent pattern artifact arena. It is not an Elo result, not self-play, not a
production strength claim, not a promotion/publication gate, and not a reason to
publish learned weights.

The candidate is `pattern-v2-endgame-lite`. The baseline is
`pattern-v1-buro-lite`. Both artifacts were exported from local PR #159
diagnostic runs and were loaded once per arena run, then reused across all
games. Generated reports, logs, weights, artifacts, and corpus payloads remain
local-only and are not committed.

## Command Shape

The run used the 5,000-board low-empty normalized schema v2 selection from the
exact-teacher campaign and selected a deterministic 1,000-board subset with
`--seed 0`.

```sh
build/tools/arena/vibe-othello-pattern-artifact-arena \
  --positions-tsv "$VIBE_OTHELLO_MEASUREMENTS/exact-teacher-late-phase-v2/<run>/selected-low-empty-normalized.tsv" \
  --candidate-weights "$LOCAL_ARTIFACTS/v2-<label-source>.weights.bin" \
  --candidate-manifest "$LOCAL_ARTIFACTS/v2-<label-source>.manifest.json" \
  --candidate-name pattern-v2-endgame-lite \
  --baseline-weights "$LOCAL_ARTIFACTS/v1-<label-source>.weights.bin" \
  --baseline-manifest "$LOCAL_ARTIFACTS/v1-<label-source>.manifest.json" \
  --baseline-name pattern-v1-buro-lite \
  --max-empty 12 \
  --max-positions 1000 \
  --seed 0 \
  --side-swap \
  --depth 3 \
  --report-out "$LOCAL_ARENA_OUT/arena-report.json" \
  --summary-out "$LOCAL_ARENA_OUT/arena-summary.md" \
  --progress-every 100
```

## Results

Each selected board was played twice with side-swapped assignments, for 2,000
games per label source. Score interval is the arena's normal-approximation
interval over game scores.

| Label source | Positions | Games | Candidate W-L-D | Candidate score | Score rate | Approx. 95% interval | Avg disc diff | Median disc diff | Failed games |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| exact teacher | 1,000 | 2,000 | 976-974-50 | 1001/2000 | 0.500500 | [0.478587, 0.522413] | 0.028500 | 0.000000 | 0 |
| observed | 1,000 | 2,000 | 975-973-52 | 1001/2000 | 0.500500 | [0.478587, 0.522413] | -0.011000 | 0.000000 | 0 |

Side assignment buckets were strongly asymmetric and mostly canceled after
side-swap:

| Label source | Candidate as side-to-move score rate | Candidate as opponent score rate |
| --- | ---: | ---: |
| exact teacher | 0.453000 | 0.548000 |
| observed | 0.450000 | 0.551000 |

## Interpretation

The prior 48-game late-game arena weak positive signal did not survive this
larger deterministic side-swapped diagnostic. This result is best described as
inconclusive, not positive and not a production strength claim. The v2 fitting
metrics from PR #159 may still be useful as fitting diagnostics, but this arena
does not show a clear local play signal for v2 over v1 under these settings.

## Next Recommendation

Use the persistent arena as the local artifact diagnostic gate before making any
strength-facing claims. If v2 is revisited, repeat with larger selected
position counts, multiple deterministic seeds, and side-assignment review, then
compare against trainer/sampling changes before adding more pattern families.
