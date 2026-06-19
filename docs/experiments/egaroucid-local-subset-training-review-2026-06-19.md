# Egaroucid Local Subset Training Review 2026-06-19

## Summary

Egaroucid local subset training for 10k / 100k / 1M was attempted on latest
`main`, but the required local corpus was not present in this checkout or nearby
local project/worktree locations.

No raw Egaroucid data, normalized TSV, generated pattern dataset, learned
weights, runtime artifacts, local-training-run reports, or analyzer summaries
are committed by this review.

Most important conclusion:

* The local training runner and analyzer smoke tests pass on latest `main`.
* The actual 10k / 100k / 1M measurements were not executed because the local
  Egaroucid corpus was missing.
* Therefore this review cannot claim that the engine became stronger. In
  Japanese: 今回は実データでの 10k / 100k / 1M 学習が走っていないため、強くなったとは判断できません。

## Environment Check

| Check | Result |
| --- | --- |
| Starting worktree status | clean |
| Latest main checked | yes |
| Local artifact path ignored | `data/corpora/local/**` is ignored by `data/corpora/.gitignore` |
| Normalized TSV found | no |
| Raw Egaroucid zip found | no |

Checked local corpus paths:

* `data/corpora/local/Egaroucid_Train_Data.zip`
* `data/corpora/local/**/normalized.tsv`
* `data/corpora/local/**/egaroucid-normalized.tsv`
* nearby worktrees under `/Users/mnishizaki/.codex/worktrees`
* nearby projects under `/Users/mnishizaki/Project`

## Build And Smoke

| Command | Result |
| --- | --- |
| `cmake -S . -B build` | passed |
| `cmake --build build` | passed |
| `ctest --test-dir build -R 'vibe_othello_pattern_(local_training_runner\|local_training_analyzer)_smoke' --output-on-failure` | passed, 2/2 tests |

## Metrics Comparison

The requested subset runs were not executed because no local Egaroucid input was
available.

| Run | Status | Runtime | Train MAE | Validation MAE | Test MAE | Eval smoke diff | Search score diff | Search best-move diff |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 10k | not run: missing local corpus | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| 100k | not run: missing local corpus | n/a | n/a | n/a | n/a | n/a | n/a | n/a |
| 1M | not run: missing local corpus | n/a | n/a | n/a | n/a | n/a | n/a | n/a |

## Warnings

| Run | Blocker warnings | Non-blocker warnings |
| --- | --- | --- |
| 10k | local corpus missing | none |
| 100k | local corpus missing | none |
| 1M | local corpus missing | none |

Analyzer warnings are unavailable because no `local-training-run-report.json`
files were produced for the requested subset sizes.

## Interpretation

The toolchain is ready enough to run the requested local measurement once the
local Egaroucid corpus is available. The runner/analyzer smoke coverage confirms
that the PR140/PR141 paths are wired and deterministic on the checked-in tiny
fixtures.

This review does not provide strength evidence. The fixed-position evaluation
and search smoke tests are signal-propagation checks, not Elo, self-play, match
bench, or production-strength validation. Since no real local subset training
completed, there is no basis to say tiny pattern set + trainer v0b improved
playing strength.

## Recommended Next PR

Recommended next PR: add a local measurement note or helper that fails early
with a clearer corpus discovery message before launching the expensive subset
workflow.

The next implementation work should still be measurement-first once the corpus
is present: run the same 10k / 100k / 1M plan and only then choose between
trainer improvement, production-ish pattern-set design, or deeper
evaluation/search smoke coverage.
