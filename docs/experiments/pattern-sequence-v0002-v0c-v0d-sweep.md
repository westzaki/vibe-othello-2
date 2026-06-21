# Pattern Sequence v0002 v0c/v0d Sweep

This is a local diagnostic note for the Egaroucid sequence v0002
pattern-learning pipeline. It is not a strength claim, Elo result, match bench,
self-play result, production artifact, publication gate, or learned-weight
release decision.

Generated suite reports, normalized TSVs, pattern datasets, learned weights,
exported artifacts, logs, raw corpus payloads, and local absolute paths remain
local-only and must not be committed.

## Scope

The campaign tested whether the leakage-safe connected-board-game 100k
measurement split gives a useful local fitting diagnostic for optimizer
selection. Selection used validation MAE first. Test MAE is reported only as a
diagnostic and tie-break signal.

The run used the local-only directory convention:

* Corpus input: `$VIBE_OTHELLO_CORPORA`, or `$VIBE_OTHELLO_LOCAL/corpora` when
  `VIBE_OTHELLO_CORPORA` is unset.
* Sequence cache: `$VIBE_OTHELLO_SEQUENCE_CACHE`, or
  `$VIBE_OTHELLO_LOCAL/sequence-cache` when unset.
* Measurement output: `$VIBE_OTHELLO_MEASUREMENTS`, or
  `$VIBE_OTHELLO_LOCAL/measurements` when unset.

The checked-in sequence manifest was
`data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json`.

## Commands

Connected 100k baseline dataset:

```sh
python3 tools/pattern/train/run_pattern_measurement_suite.py \
  --sequence-input "${VIBE_OTHELLO_CORPORA:-$VIBE_OTHELLO_LOCAL/corpora}" \
  --sequence-manifest data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json \
  --preset 100k \
  --run-prefix connected-100k-v0c-baseline-corpus \
  --measurement-split-policy connected-board-game \
  --resume
```

v0c sweep:

```sh
python3 tools/pattern/train/run_pattern_trainer_sweep.py \
  --source-local-run-report "${VIBE_OTHELLO_MEASUREMENTS:-$VIBE_OTHELLO_LOCAL/measurements}/connected-100k-v0c-baseline-corpus/runs/connected-100k-v0c-baseline-corpus-100k/local-training-run-report.json" \
  --output-dir "${VIBE_OTHELLO_MEASUREMENTS:-$VIBE_OTHELLO_LOCAL/measurements}/sweeps/connected-100k-v0c-core" \
  --sweep-preset v0c-100k-core \
  --resume
```

v0d phase-balanced sweep:

```sh
python3 tools/pattern/train/run_pattern_trainer_sweep.py \
  --source-local-run-report "${VIBE_OTHELLO_MEASUREMENTS:-$VIBE_OTHELLO_LOCAL/measurements}/connected-100k-v0c-baseline-corpus/runs/connected-100k-v0c-baseline-corpus-100k/local-training-run-report.json" \
  --output-dir "${VIBE_OTHELLO_MEASUREMENTS:-$VIBE_OTHELLO_LOCAL/measurements}/sweeps/connected-100k-v0d-phase-core" \
  --sweep-preset v0d-100k-phase-core \
  --resume
```

## Connected 100k Baseline

| item | value |
| --- | ---: |
| sampled rows | 100,000 |
| train rows | 80,845 |
| validation rows | 9,930 |
| test rows | 9,225 |
| dataset output format | compact TSV |
| measurement split policy | connected-board-game |
| board cross-split collisions after resplit | 0 |
| game-group cross-split collisions after resplit | 0 |
| final validation MAE | 14.2129554353 |
| test MAE | 14.6427411139 |
| validation sign accuracy | 0.7339375629 |
| test sign accuracy | 0.7465582656 |

The connected split removed the exact-board leakage seen before resplitting.
The analyzer still reported phase-level MAE regressions versus the phase-bias
baseline for phase 2 on validation and test.

## v0c Sweep

The v0c sweep selected `isqrt_lr0.1_wd1e-4` by validation MAE.

| config | best val MAE | final val MAE | test MAE | train MAE | val sign | test sign | weights | L2 | max abs | warnings |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| baseline_const_lr0.1 | 14.2129554353 | 14.2129554353 | 14.6427411139 | 9.6717651935 | 0.7339375629 | 0.7465582656 | 173,714 | 534.3888134840 | 12.5558640020 | none |
| const_lr0.05 | 14.2304169148 | 14.2304169148 | 14.8419641839 | 11.2604270830 | 0.7340382679 | 0.7437398374 | 173,714 | 392.7237838840 | 11.0335476773 | none |
| const_lr0.03 | 14.4444375745 | 14.4444375745 | 15.2394696741 | 12.5161336797 | 0.7317220544 | 0.7365853659 | 173,714 | 308.4873976960 | 10.8416506008 | none |
| const_lr0.2 | 14.5554500858 | 14.5554500858 | 14.8622682692 | 8.3713100538 | 0.7290030211 | 0.7476422764 | 173,714 | 713.0518622430 | 15.4857068475 | train_validation_gap_large |
| isqrt_lr0.1 | 14.1995569097 | 14.1995569097 | 14.6948040267 | 10.4919016448 | 0.7339375629 | 0.7475338753 | 173,714 | 456.3534616870 | 11.5888589179 | none |
| isqrt_lr0.05 | 14.3893956832 | 14.3893956832 | 15.1087089137 | 12.1570576289 | 0.7344410876 | 0.7404878049 | 173,714 | 331.9605241350 | 10.7659810724 | none |
| isqrt_lr0.03 | 14.6995168451 | 14.6995168451 | 15.6074461616 | 13.4223706551 | 0.7306143001 | 0.7293224932 | 173,714 | 258.4051643230 | 10.7840935617 | none |
| isqrt_lr0.1_wd1e-4 | 14.1978155715 | 14.1978155715 | 14.6939543274 | 10.4931908668 | 0.7339375629 | 0.7471002710 | 173,714 | 456.1131764510 | 11.5761184663 | none |
| isqrt_lr0.05_wd1e-4 | 14.3885299242 | 14.3885299242 | 15.1083768874 | 12.1581499203 | 0.7343403827 | 0.7407046070 | 173,714 | 331.8150406370 | 10.7498189725 | none |
| isqrt_lr0.05_clip2 | 14.3905066581 | 14.3905066581 | 15.1135670736 | 12.1587709332 | 0.7351460222 | 0.7415718157 | 173,714 | 331.4722864160 | 10.7639627721 | none |

## v0d Phase-Balanced Sweep

The v0d sweep selected `v0d_sqrt_bal_lr0.1` by validation MAE.

| config | best val MAE | final val MAE | test MAE | train MAE | val sign | test sign | weights | L2 | max abs | warnings |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| v0d_sqrt_bal_lr0.05 | 14.3964597615 | 14.3964597615 | 15.1238865008 | 12.2090275262 | 0.7356495468 | 0.7391869919 | 173,714 | 329.6530967040 | 10.7920615989 | none |
| v0d_sqrt_bal_lr0.1 | 14.1985360468 | 14.1985360468 | 14.6947363573 | 10.5358917590 | 0.7349446123 | 0.7465582656 | 173,714 | 453.6513950730 | 11.6797913843 | none |
| v0d_inv_bal_lr0.05 | 14.4108001386 | 14.4108001386 | 15.1507622443 | 12.2891344782 | 0.7351460222 | 0.7377777778 | 173,714 | 326.5336092580 | 10.8159219366 | none |
| v0d_none_lr0.05 | 14.3893956832 | 14.3893956832 | 15.1087089137 | 12.1570576289 | 0.7344410876 | 0.7404878049 | 173,714 | 331.9605241350 | 10.7659810724 | none |
| v0d_sqrt_bal_lr0.05_wd1e-4 | 14.3957063507 | 14.3957063507 | 15.1235467884 | 12.2101409027 | 0.7355488419 | 0.7389701897 | 173,714 | 329.5045401390 | 10.7753671541 | none |
| v0d_sqrt_bal_lr0.05_clip2 | 14.3999312438 | 14.3999312438 | 15.1296930037 | 12.2106384048 | 0.7350453172 | 0.7395121951 | 173,710 | 328.8917497720 | 10.7903199955 | none |

Best v0d diagnostics:

| item | value |
| --- | --- |
| phase balance | `sqrt-inverse-count` |
| weighted train residual MAE | 10.7276645876 |
| phase balance notes | active train phases: 12; train example weighted average phase weight: 1; phase 0 had no train examples and received weight 0 |
| validation phase regressions vs phase-bias | phase 2 regressed by 0.0502570390 MAE |
| test phase regressions vs phase-bias | none |

Phase train counts were balanced around the connected split distribution:
phase 1 had 3,114 train examples, phases 2/5/7/10/12 had about 6.1k-6.2k, and
phases 3/4/6/8/9/11 had about 7.7k-7.8k. Phase 0 had no train examples.

## Comparison

| family | config | best val MAE | final val MAE | test MAE | train MAE | val sign | test sign | weights | L2 | max abs | warnings | decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| v0c | isqrt_lr0.1_wd1e-4 | 14.1978155715 | 14.1978155715 | 14.6939543274 | 10.4931908668 | 0.7339375629 | 0.7471002710 | 173,714 | 456.1131764510 | 11.5761184663 | none | nominal best, not a meaningful win |
| v0d | v0d_sqrt_bal_lr0.1 | 14.1985360468 | 14.1985360468 | 14.6947363573 | 10.5358917590 | 0.7349446123 | 0.7465582656 | 173,714 | 453.6513950730 | 11.6797913843 | none | effectively tied |

The validation MAE gap between the best v0c and best v0d configs was
0.0007204753 MAE in favor of v0c, far below the promotion threshold of 0.2 MAE
absolute improvement or 1 percent relative improvement. The campaign is
therefore inconclusive between v0c and v0d.

Compared with the connected 100k default v0c baseline, the best v0c sweep
improved validation MAE from 14.2129554353 to 14.1978155715, an improvement of
0.0151398638 MAE, or about 0.11 percent. Treat this only as a fitting
diagnostic.

## 1m Promotion

No 1m promotion was run because there was no meaningful validation-selected
winner between v0c and v0d.

If the team wants a deterministic next 1m diagnostic anyway, the most defensible
candidate is the nominal validation winner:

```sh
python3 tools/pattern/train/run_pattern_measurement_suite.py \
  --sequence-input "${VIBE_OTHELLO_CORPORA:-$VIBE_OTHELLO_LOCAL/corpora}" \
  --sequence-manifest data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json \
  --preset 1m \
  --run-prefix promoted-1m-isqrt_lr0.1_wd1e-4 \
  --measurement-split-policy connected-board-game \
  --trainer-mode pattern-sgd-v0c \
  --learning-rate 0.1 \
  --lr-schedule inverse-sqrt \
  --weight-decay 0.0001 \
  --resume
```

## Warnings And Caveats

* The result is a local fitting diagnostic only.
* v0c and v0d are effectively tied by validation MAE.
* The best v0d config still had a small validation phase 2 MAE regression
  versus the phase-bias baseline.
* No match bench, Elo, self-play, production artifact, publication artifact,
  or release-weight gate was run.
* Generated local outputs remain under `$VIBE_OTHELLO_MEASUREMENTS` and are not
  part of this note.

## Next Recommended Action

Do not promote this exact v0d phase-balanced preset as better than v0c yet.
The next useful PR should widen the 100k optimizer diagnostic around the tied
region: compare v0c and v0d at `learning-rate 0.08`, `0.1`, and `0.12` with
the same inverse-sqrt schedule, include `weight-decay 1e-4` for both families,
and keep validation MAE as the primary selector.

An orthogonal follow-up diagnostic is to replace only low-empty late-phase
observed labels with exact endgame teacher labels and compare fitting metrics
on the same selected boards:

```sh
python3 tools/pattern/labels/run_exact_teacher_late_phase_campaign.py \
  --normalized-tsv "$RUN_DIR/resplit-normalized.tsv" \
  --output-dir "$RUN_DIR/exact-teacher-late-phase" \
  --max-empty 12 \
  --max-positions 5000 \
  --seed 0 \
  --trainer-mode pattern-sgd-v0c
```

Use validation MAE as the primary decision metric. Test MAE is reporting and
tie-break only, and the result is still not a strength, Elo, match bench,
self-play, or production artifact claim.
