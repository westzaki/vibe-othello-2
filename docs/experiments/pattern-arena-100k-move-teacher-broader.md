# Pattern Arena 100k Move-Teacher Broader Validation

## Scope

This note records a local-only bounded artifact arena matrix for the fair
connected 100k move-teacher child-value artifact from the 100k growth cycle.

The goal was to check whether the 100k move-teacher artifact remains
non-negative or supportive across broader arena seeds, all requested depths,
and 1,000 sampled positions before a later PR considers adding an experimental
default artifact.

This PR does not add a default artifact and does not commit generated weights,
artifacts, TSVs, teacher labels, datasets, raw reports, logs, caches, or
corpora.

## Non-Claims

This is not an Elo result, not self-play, not a production strength claim, not
publication readiness, and not artifact-publication readiness.

The result does not claim that the artifact is production-ready. It only says
the local bounded arena validation supports using the artifact as the candidate
for a later experimental-default PR.

## Source And Artifacts

The selected source is the fair connected 100k low-empty normalized TSV from
PR #170:

| Item | Value |
| --- | --- |
| selected source checksum | `sha256:260102a58ead4522169d7298ba828fa983930c902c261c49d18da4b11b6d0ce7` |
| selected source size | 36,888,016 bytes |
| source rows | 100,000 selected low-empty roots |

Artifacts:

| Artifact | Pattern set | Weights SHA-256 | Manifest SHA-256 | Runtime checksum |
| --- | --- | --- | --- | --- |
| move-teacher v2 100k seed 0 | `pattern-v2-endgame-lite` | `sha256:fd567e21ea655af2a1287d0eef0b9647229ccc479c2224fd8039323549b4951e` | `sha256:9ce5bdfba6bcaeeb47fb5e20baac32038dc290e9cbfc10698cf030afa6dd8891` | `0x3d50ed72` |
| fair exact-root v2 100k | `pattern-v2-endgame-lite` | `sha256:5c881edb9d72bcb05b97a1b7c739d86e51e7d89c308c3295d51df06a7ddb2c14` | `sha256:077023941a9d94dc9432b3c02212b2c68993eb47dd6edc5b8b2b2e53148b9308` | `0x0b7da21d` |
| v1 exact-teacher | `pattern-v1-buro-lite` | `sha256:10dbe56b12ffd8197a5e35be08f920bc308f2ffcddeaba00324da4491556343f` | `sha256:7cc8718e1e5afd0c3b0fc36c04382d267de523d6d1032d6beb3edb3982d0a4d3` | `0x2f2dbe19` |

The exact-root v2 baseline is the fair same-source 100k exact-root v2 baseline
from PR #170. No comparison below uses a mismatched 50k exact-root baseline.

## Comparison Matrix

The run used `tools/arena/run_pattern_artifact_arena_matrix.py`, which wraps
the existing persistent pattern artifact arena and validates per-run resume
metadata with input and output checksums.

| Setting | Value |
| --- | --- |
| depths | 3, 5, 7 |
| seeds | 0, 10, 20, 30, 40 |
| max positions | 1,000 |
| side swap | enabled |
| primary comparisons | move-teacher v2 vs exact-root v2; move-teacher v2 vs v1; exact-root v2 vs v1 |
| sanity comparisons | same-artifact candidate sanity; candidate/baseline swap sanity for move-teacher v2 vs exact-root v2 |
| completed runs | 75 |
| total games | 150,000 |
| failed games | 0 |

## Aggregate Summary

| Comparison | Runs | Games | Failed | Non-negative runs | Mean score rate | Min score rate | Max score rate | Mean avg disc diff |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| move-teacher v2 vs exact-root v2 | 15 | 30,000 | 0 | 14 | 0.503517 | 0.499500 | 0.509750 | +0.2020 |
| move-teacher v2 vs v1 | 15 | 30,000 | 0 | 15 | 0.526150 | 0.510000 | 0.543000 | +1.6194 |
| exact-root v2 vs v1 | 15 | 30,000 | 0 | 15 | 0.523250 | 0.508500 | 0.538750 | +1.4444 |
| same-artifact sanity | 15 | 30,000 | 0 | 15 | 0.500000 | 0.500000 | 0.500000 | +0.0000 |
| swap sanity: exact-root v2 vs move-teacher v2 | 15 | 30,000 | 0 | 2 | 0.496483 | 0.490250 | 0.500500 | -0.2020 |

The swap-sanity non-negative count is not a promotion signal. It is expected
to reverse the primary move-teacher v2 vs exact-root v2 direction. All 15
paired swap checks complemented the forward score rate and average disc
difference.

## Per-Depth Summary

| Comparison | Depth | Runs | Non-negative runs | Mean score rate | Min score rate | Max score rate | Mean avg disc diff |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| move-teacher v2 vs exact-root v2 | 3 | 5 | 5 | 0.506400 | 0.502750 | 0.509750 | +0.4005 |
| move-teacher v2 vs exact-root v2 | 5 | 5 | 5 | 0.503200 | 0.500750 | 0.504750 | +0.1594 |
| move-teacher v2 vs exact-root v2 | 7 | 5 | 4 | 0.500950 | 0.499500 | 0.502250 | +0.0462 |
| move-teacher v2 vs v1 | 3 | 5 | 5 | 0.537500 | 0.533750 | 0.543000 | +2.4807 |
| move-teacher v2 vs v1 | 5 | 5 | 5 | 0.526300 | 0.523500 | 0.529750 | +1.5565 |
| move-teacher v2 vs v1 | 7 | 5 | 5 | 0.514650 | 0.510000 | 0.517250 | +0.8211 |
| exact-root v2 vs v1 | 3 | 5 | 5 | 0.533450 | 0.529250 | 0.538750 | +2.2178 |
| exact-root v2 vs v1 | 5 | 5 | 5 | 0.523450 | 0.518500 | 0.529250 | +1.3876 |
| exact-root v2 vs v1 | 7 | 5 | 5 | 0.512850 | 0.508500 | 0.515750 | +0.7279 |

Depth 7 narrowed the move-teacher v2 vs exact-root v2 margin. One run was
slightly negative by score rate (`0.499500`), and one neutral-score run had a
slightly negative average disc difference (`-0.0255`). The aggregate depth-7
score rate and average disc difference remained non-negative.

## Per-Seed Summary

| Seed | Move-teacher v2 vs exact-root v2 mean score / diff / non-negative | Move-teacher v2 vs v1 mean score / diff / non-negative | Exact-root v2 vs v1 mean score / diff / non-negative |
| ---: | --- | --- | --- |
| 0 | 0.505083 / +0.2342 / 3 of 3 | 0.529833 / +1.7648 / 3 of 3 | 0.527583 / +1.5218 / 3 of 3 |
| 10 | 0.504250 / +0.2360 / 3 of 3 | 0.526833 / +1.5775 / 3 of 3 | 0.523333 / +1.3810 / 3 of 3 |
| 20 | 0.502500 / +0.2127 / 2 of 3 | 0.524500 / +1.5993 / 3 of 3 | 0.522250 / +1.4847 / 3 of 3 |
| 30 | 0.501833 / +0.1663 / 3 of 3 | 0.525333 / +1.5182 / 3 of 3 | 0.523000 / +1.3678 / 3 of 3 |
| 40 | 0.503917 / +0.1610 / 3 of 3 | 0.524250 / +1.6373 / 3 of 3 | 0.520083 / +1.4668 / 3 of 3 |

## Sanity Results

Same-artifact sanity used move-teacher v2 100k seed 0 against itself across all
depth/seed combinations. It was exactly neutral in every run:

| Runs | Games | Failed | Score rate | Avg disc diff |
| ---: | ---: | ---: | ---: | ---: |
| 15 | 30,000 | 0 | 0.500000 | +0.0000 |

Candidate/baseline swap sanity reversed the primary move-teacher v2 vs
exact-root v2 comparison across all depth/seed combinations:

| Runs | Games | Failed | Paired checks passed | Mean score rate | Mean avg disc diff |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 15 | 30,000 | 0 | 15 of 15 | 0.496483 | -0.2020 |

## Scorecard

Scorecard category: `promote_to_experimental_default_candidate`.

Rationale:

* move-teacher v2 vs exact-root v2 was non-negative in 14 of 15 runs
* mean score rate vs exact-root v2 was `0.503517`
* mean average disc difference vs exact-root v2 was `+0.2020`
* move-teacher v2 vs v1 was supportive in all 15 runs
* fair exact-root v2 vs v1 was supportive in all 15 runs
* same-artifact sanity was exactly neutral in all 15 runs
* swap sanity moved in the expected reverse direction in all 15 paired checks
* failed games were zero
* no resume metadata mismatch occurred

Recommendation: the next PR may add the learned eval artifact v0 as an
experimental default candidate, with the appropriate artifact files, policy
checks, and loader smoke. This PR intentionally does not commit the artifact.

## What Was Not Run

No 2,000-position or 5,000-position subset was run. The full all-depth,
all-seed 1,000-position matrix already covered 75 runs and 150,000 games,
including depth 7 for every requested seed. The larger-position subset is left
for a follow-up only if reviewers want more margin on the close depth-7
move-teacher v2 vs exact-root v2 rows.

No fixed-opening-style arena was run. The existing checked-in opening fixture
is for the process arena smoke path, not a large validated opening-position
fixture for this artifact matrix. This PR does not create an opening book.

No NTest comparison was run, and no artifact was committed.

## Resume Commands

Use local measurement paths through `VIBE_OTHELLO_MEASUREMENTS`; do not replace
these examples with personal absolute paths in committed docs.

```sh
export MT_SRC="$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-source-v1"
export MT_RUN="$VIBE_OTHELLO_MEASUREMENTS/pattern-growth-cycle-100k-connected-v1/decision-leverage-matrix/roots-100000-seed-0"
export V1_ARTIFACTS="$VIBE_OTHELLO_MEASUREMENTS/pattern-signal-bottleneck-diagnostics/artifacts"
export ARENA_OUT="$VIBE_OTHELLO_MEASUREMENTS/pattern-arena-100k-mt-v0-broader-v1"

python3 tools/arena/run_pattern_artifact_arena_matrix.py \
  --positions-tsv "$MT_SRC/selected-low-empty-normalized.tsv" \
  --output-dir "$ARENA_OUT" \
  --comparison-name move_teacher_v2_vs_exact_root_v2 \
  --candidate-weights "$MT_RUN/move-teacher-child.weights.bin" \
  --candidate-manifest "$MT_RUN/move-teacher-child.manifest.json" \
  --candidate-name "move-teacher-v2-100k-seed0" \
  --baseline-weights "$MT_SRC/exact-root-v2.weights.bin" \
  --baseline-manifest "$MT_SRC/exact-root-v2.manifest.json" \
  --baseline-name "exact-root-v2-100k" \
  --pair-json "{\"comparison\":\"move_teacher_v2_vs_v1\",\"candidate_weights\":\"$MT_RUN/move-teacher-child.weights.bin\",\"candidate_manifest\":\"$MT_RUN/move-teacher-child.manifest.json\",\"candidate_name\":\"move-teacher-v2-100k-seed0\",\"baseline_weights\":\"$V1_ARTIFACTS/v1-exact-teacher.weights.bin\",\"baseline_manifest\":\"$V1_ARTIFACTS/v1-exact-teacher.manifest.json\",\"baseline_name\":\"v1-exact-teacher\"}" \
  --pair-json "{\"comparison\":\"exact_root_v2_vs_v1\",\"candidate_weights\":\"$MT_SRC/exact-root-v2.weights.bin\",\"candidate_manifest\":\"$MT_SRC/exact-root-v2.manifest.json\",\"candidate_name\":\"exact-root-v2-100k\",\"baseline_weights\":\"$V1_ARTIFACTS/v1-exact-teacher.weights.bin\",\"baseline_manifest\":\"$V1_ARTIFACTS/v1-exact-teacher.manifest.json\",\"baseline_name\":\"v1-exact-teacher\"}" \
  --depths 3,5,7 \
  --seeds 0,10,20,30,40 \
  --max-positions 1000 \
  --side-swap \
  --same-artifact-sanity candidate \
  --swap-sanity primary \
  --arena-exe build/tools/arena/vibe-othello-pattern-artifact-arena \
  --resume
```

The helper writes local-only per-run arena reports under `$ARENA_OUT/runs/`
and aggregate `arena-matrix-report.json` plus `arena-matrix-summary.md` under
`$ARENA_OUT`.

## Next Action

Open a separate PR to add the learned eval artifact v0 as an experimental
default candidate. That follow-up PR may commit the artifact payload and
policy/loader metadata, but it should keep this validation PR free of generated
artifacts.
