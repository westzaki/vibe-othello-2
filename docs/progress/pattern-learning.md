# Pattern Learning Progress

This document is the short current-status entry point for pattern learning.

Intended design and stable contracts live in architecture and data-policy docs.
Detailed experiment notes live in `docs/experiments/README.md` and the archive
documents linked from it.

## Current State

`pattern-v2-endgame-lite-100k-mt-v0` is the committed learned evaluation
artifact v0 and is the current experimental default.

The default pointer is `data/eval/default-artifact.json`, with status
`experimental-default`, resolving to:

```text
data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/manifest.json
```

The committed artifact payload is limited to final runtime files:

* `data/eval/default-artifact.json`
* `data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/weights.bin`
* `data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/manifest.json`
* `data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/provenance.json`
* `data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/README.md`
* `data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/NOTICE.md`

Raw transcripts, normalized corpora, selected TSVs, teacher labels,
move-teacher TSVs, child-normalized TSVs, caches, local reports, logs,
temporary datasets, and sweep outputs are local-only and must not be committed.

The current trainer/runtime route is:

* `pattern-v2-endgame-lite` runtime pattern set
* move-teacher child-label training
* fair same-source exact-root baseline comparison
* repeated 100k move-teacher validation
* broader bounded arena validation
* committed learned artifact v0 as an experimental default

This is an experimental default, not an Elo result, not a self-play result,
not a production strength claim, and not publication readiness.

Artifact safety, promotion, rollback, and commit-policy details belong to:

* `docs/architecture/evaluation-artifacts.md`
* `data/eval/README.md`
* `tools/pattern/artifacts/check_eval_artifact_commit_policy.py`

For normal development, use this doc for current status, architecture docs for
current contracts, and `docs/experiments/README.md` for historical evidence.

## Adopted Route

The adopted path is the move-teacher child-label route that led to the
experimental default artifact:

1. Sequence/transcript importer for local Egaroucid-derived game transcripts.
2. Normalized TSV schema v2 with explicit game, board, occurrence, split, and
   side-to-move label identity.
3. Connected-board-game split for more honest sequence-derived validation.
4. Compact pattern dataset rows for scalable local training and diagnostics.
5. Trainer v0c/v0d diagnostics on `pattern-v2-endgame-lite`, keeping runtime
   artifact shape compatible with the existing pattern evaluator.
6. Exact move-teacher child labels for legal after-move boards.
7. Move-teacher cache and partial-miss solve flow to reuse exact solves without
   committing labels or generated datasets.
8. Fair 100k exact-root baseline derived from the same move-teacher source.
9. Repeated 100k move-teacher validation against the fair exact-root baseline.
10. Broader bounded arena validation against exact-root v2 and v1 artifacts.
11. Learned artifact v0 committed as the experimental default.

Detailed PR-by-PR logs, command lines, sweep settings, and metric tables are in
the archive starting at `docs/experiments/README.md`.

The current route does not rely on the old observed-label MAE-only path as a
promotion signal. Fitting metrics remain useful diagnostics, but the adopted
route uses move-ranking and bounded arena evidence before default selection.

## Key Decisions

Fitting MAE alone is not enough. Review pattern-learning changes with
move-ranking metrics, decision leverage, bounded arena checks, and sign/sanity
diagnostics where applicable.

Exact root-label training is not the current adopted default route.
Move-teacher child-label training is the adopted route for the current
experimental default.

`pattern-v3`, a pairwise rank trainer, and larger objective changes should not
be mixed with artifact default changes. Keep trainer/objective investigations
separate from default-artifact promotion PRs.

Artifact promotion and rollback must go through the committed manifest,
provenance, default pointer, validation summary, and commit-policy checker.
Do not overwrite an existing artifact directory with semantically different
weights.

Local-only runner outputs are not repository artifacts. Do not commit raw
external data, normalized corpora, selected TSVs, generated teacher labels,
move-teacher outputs, child-normalized outputs, caches, sweep reports, trainer
reports, temporary datasets, or local logs.

Architecture docs describe the current contract. Experiment docs preserve
historical evidence. Progress docs summarize current repository state and next
work.

The committed v0 artifact is acceptable as an experimental engineering default
because the validation route selected it, the payload is minimal, provenance is
explicit, and local-only source material is not redistributed.

Future strength claims need separate validation. The current default should be
treated as a runtime/default-selection milestone, not as production strength.

## Next Work

Current next actions:

* Run local human-play and default-engine checks against the experimental
  default to catch obvious play-quality or integration issues.
* Add larger bounded validation only if the current margin or review feedback
  needs more evidence.
* Add NTest or external-engine match validation before making strength claims.
* Harden artifact promotion gates around manifest/provenance/default-pointer
  consistency and rollback review.
* Continue trainer v0e or move-ranking objective work as a separate
  investigation.
* Investigate `pattern-v3` separately from artifact default maintenance.
* Confirm the default artifact rollback path remains simple and documented.

Do not treat old local run recommendations as current action items. In
particular, the 100k partial-miss move-teacher solve, fair 100k exact-root
baseline, broader 100k bounded arena validation, and learned artifact v0
experimental-default PR are already complete for the current route.

## Completed History

Historical details are intentionally not repeated here. Use
`docs/experiments/README.md` as the archive index when exact metrics, command
lines, or PR-specific context are needed.

| Area | Current summary |
| --- | --- |
| Import and normalization | Sequence transcripts can be replayed into normalized TSV schema v2 with deterministic identity, split, and leakage diagnostics. |
| Dataset shape | Compact TSV example rows are the scalable path for local pattern training diagnostics. |
| Pattern sets | `pattern-v1-buro-lite` is the earlier production-ish schema; `pattern-v2-endgame-lite` is the bounded endgame-oriented pattern set used by the current experimental default. |
| Trainer diagnostics | v0c/v0d provide local residual pattern-SGD diagnostics and reports; they are not standalone strength claims. |
| Exact teacher labels | Exact root-label experiments were useful diagnostics but did not become the adopted default route. |
| Move-teacher labels | Exact child labels made root move-ranking and decision leverage visible with the existing value trainer. |
| Cache/materialization | Move-teacher cache and materialization flows support large local reruns while keeping labels and child-normalized TSVs local-only. |
| Growth cycle | 50k and 100k local growth-cycle validations selected the move-teacher route for broader validation. |
| Arena validation | Broader bounded validation supported promoting the 100k move-teacher artifact to experimental default. |
| Artifact v0 | `pattern-v2-endgame-lite-100k-mt-v0` is committed as the first learned experimental default with manifest, provenance, default pointer, and policy checks. |
| Archive policy | Long logs, tables, command examples, and obsolete next actions belong in `docs/experiments/README.md` and linked experiment docs. |

Update this document when the current status, adopted route, artifact default,
promotion policy, or active next work changes. Update architecture docs when
the intended contract changes.
