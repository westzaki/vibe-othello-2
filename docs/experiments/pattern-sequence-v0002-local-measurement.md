# Pattern Sequence v0002 Local Measurement Notes

This is a local exploratory measurement note for the Egaroucid sequence v0002
pattern-learning pipeline. It is not a strength claim, Elo result, match bench,
self-play result, production artifact, or publication gate.

Generated suite reports, normalized TSVs, pattern datasets, learned weights,
exported artifacts, logs, and raw corpus payloads stay under local-only paths
and must not be committed.

## Local Setup

The run used the local-only directory convention from `README.md` and
`data/corpora/README.md`:

* Input zips: `$VIBE_OTHELLO_LOCAL/corpora`
* Shared sequence cache: `$VIBE_OTHELLO_LOCAL/sequence-cache`
* Suite output root: `$VIBE_OTHELLO_LOCAL/measurements`
* Suite prefix: `sequence-v0002-real-20260621`

The checked-in sequence manifest was
`data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json`.

The suite command was:

```sh
python3 tools/pattern/train/run_pattern_measurement_suite.py \
  --sequence-input "$VIBE_OTHELLO_LOCAL/corpora" \
  --sequence-manifest data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json \
  --preset all \
  --run-prefix sequence-v0002-real-20260621 \
  --resume
```

For follow-up held-out diagnostics on sequence-derived data, prefer the
leakage-safe measurement split mode:

```sh
python3 tools/pattern/train/run_pattern_measurement_suite.py \
  --sequence-input "$VIBE_OTHELLO_LOCAL/corpora" \
  --sequence-manifest data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json \
  --preset all \
  --run-prefix sequence-v0002-real-<date>-connected-split \
  --measurement-split-policy connected-board-game \
  --resume
```

`connected-board-game` groups sampled normalized schema v2 rows by connected
components of `game_group_id` and side-to-move-relative `board_id`, then assigns
each component to train/validation/test with a deterministic 80/10/10 hash. It
may change split counts relative to importer-preserved splits. It improves
holdout leakage hygiene for local validation/test diagnostics, but it does not
fix noisy observed-final-disc-diff labels and is not a strength, Elo, match
bench, self-play, production artifact, or publication claim.

Do not add `--strict-board-disjoint-splits` to routine connected-split suite
commands; `--measurement-split-policy connected-board-game` is the option that
requests the leakage-safe resplit. The runner keeps strict board checks as an
after-resplit guard when both options are supplied, but the practical rerun
command only needs the measurement split policy.

## Measurement Results

The first suite run was a cold-cache run, so every preset reported
`cache_status=miss`. All three presets completed successfully.

| preset | sampled rows | train rows | validation rows | test rows | cache | final validation MAE | test MAE | test sign accuracy | nonzero weights | weight L2 norm | eval smoke positions | search smoke positions | wall time | warnings | artifact checksum |
| --- | ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 10k | 10,000 | 7,869 | 1,040 | 1,091 | miss | 17.9102556233 | 17.6070219778 | 0.6434463795 | 51,320 | 235.216587106 | 1,000 | 200 | 30.1365s | 8 | `0x545c573b` |
| 100k | 100,000 | 79,279 | 9,665 | 11,056 | miss | 14.4580384779 | 15.6703515176 | 0.7413169320 | 172,279 | 529.654184920 | 5,000 | 500 | 65.6784s | 6 | `0x7c6c5d6c` |
| 1m | 1,000,000 | 800,945 | 100,144 | 98,911 | miss | 15.0032595430 | 14.9702063051 | 0.6990931241 | 393,253 | 1051.930457900 | 10,000 | 1,000 | 873.783s | 4 | `0x24de68a9` |

## Baseline Comparison

The v0c pattern-SGD trainer improved split-level MAE and sign accuracy against
the phase-bias baseline on all measured presets.

| preset | baseline validation MAE | v0c validation MAE | baseline test MAE | v0c test MAE | baseline test sign accuracy | v0c test sign accuracy |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 10k | 20.9513669753 | 17.9102556233 | 20.9544063451 | 17.6070219778 | 0.3831347388 | 0.6434463795 |
| 100k | 21.3160436367 | 14.4580384779 | 24.2734061831 | 15.6703515176 | 0.4360528220 | 0.7413169320 |
| 1m | 20.7042558570 | 15.0032595430 | 20.9925548654 | 14.9702063051 | 0.4545601601 | 0.6990931241 |

The 1m run did not cleanly dominate the 100k run on validation MAE, although it
did improve test MAE slightly. Treat this as a tuning signal rather than a
production quality claim.

## Cache Hit Check

After the cold-cache suite completed, a lightweight cache check reran the same
sequence importer options for 10k, 100k, and 1m while limiting downstream
training work. All three sequence cache entries restored successfully.

| preset | cache status | cache restore wall time | source hashing wall time |
| --- | --- | ---: | ---: |
| 10k | hit | 0.003441s | 11.22025s |
| 100k | hit | 0.011218s | 11.278334s |
| 1m | hit | 0.121387s | 12.859693s |

The cache avoids sequence replay/import work. Cache key calculation still hashes
the input zips, so each run pays roughly 11-13 seconds of source hashing in this
local setup.

## Warnings

The suite succeeded, but analyzer warnings remain useful review signals:

* 10k reported `train_rows_too_small` because the train split contained 7,869
  rows.
* 100k and 1m reported `exact_board_cross_split_collision`; the 100k run had 7
  cross-split board collisions, and the 1m run had 539.
* All presets reported phase-level MAE regressions versus the phase-bias
  baseline, concentrated in early phases.

These warnings do not fail the local measurement suite, but they limit how much
confidence to place in held-out MAE and suggest the next investigation areas.

## Interpretation

The measurement pipeline is working: local-only directories, shared sequence
cache, suite reports, analyzer summaries, compact pattern datasets, v0c
training, export, evaluation smoke, and search smoke all completed.

The model signal is promising but not production-ready. More data improves the
phase-bias comparison, and learned pattern weights materially change evaluation
and depth-1 search smoke outputs. However, the 100k-to-1m validation movement is
not monotonic, early phases still regress against the phase-bias baseline, and
cross-split exact-board collisions need attention before treating held-out
metrics as clean generalization measurements.

## Recommended Follow-Ups

1. Run a 100k tuning sweep before spending more 1m runtime:
   `learning-rate`, `weight-decay`, `lr-schedule inverse-sqrt`, more epochs,
   and early stopping are the first knobs to try.
2. Add or use a split policy that prevents exact board leakage across
   train/validation/test for sequence-derived data, then rerun at least 100k.
   The current recommended option is
   `--measurement-split-policy connected-board-game`.
3. Investigate early-phase regressions with phase-by-phase diagnostics and
   consider phase-specific weighting or feature/trainer changes.
4. Rerun 1m only after the 100k sweep finds a better setting.
5. Keep match bench, Elo, self-play, production artifact publication, and
   derived-weight publication out of scope until provenance and measurement
   quality gates are stronger.
