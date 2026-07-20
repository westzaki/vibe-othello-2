# Pattern Learning Progress

This document is the short current-status entry point for pattern learning.

Intended design and stable contracts live in architecture and data-policy docs.
Detailed experiment notes live in `docs/experiments/README.md` and the archive
documents linked from it.

## Current State

`pattern-v2-egaroucid-lv17-full-value-v1` is the current learned experimental
default. The previous `pattern-v2-wthor-full-policy-v1` artifact remains
committed as the rollback and comparison baseline.

Its reviewed learning coverage is all phases `[0..12]`, recorded in both
runtime manifest and provenance. Phases `0..9` add an Egaroucid board-score
residual to the deterministic fallback. Phases `10..12` use Egaroucid-trained
replacement weights. Coverage metadata is explicit rather than inferred from
zero or nonzero weights.

The default pointer is `data/eval/default-artifact.json`, with status
`experimental-default`, resolving to:

```text
data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json
```

The committed artifact payload is limited to final runtime files:

* `data/eval/default-artifact.json`
* `data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/weights.bin`
* `data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json`
* `data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/provenance.json`
* `data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/README.md`
* `data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/NOTICE.md`

Raw transcripts, normalized corpora, selected TSVs, teacher labels,
move-teacher TSVs, child-normalized TSVs, caches, local reports, logs,
temporary datasets, and sweep outputs are local-only and must not be committed.

The current trainer/runtime route is:

* `pattern-v2-endgame-lite` runtime pattern set
* all 25,514,097 Egaroucid board-score positions
* 1,514,097 Egaroucid 7.4.0 lv17 enumeration/evaluation/negamax values for
  4-15 occupied squares
* 24,000,000 Egaroucid 7.5.1 lv17 self-play terminal outcomes for 16-63
  occupied squares
* neutral side-to-move `teacher_value_disc_diff` targets
* five full-corpus passes for phases `0..9` and one full-corpus pass for phases
  `10..12`
* streaming training without materializing an expanded dataset
* deterministic fallback plus learned residual through phase 9
* independent paired fixed-depth and fixed-time arena validation directly
  against the prior default
* zero audited promotion-opening board overlap with all 25,514,097 training
  boards
* short-opening direct arena gates exercising updated phases `0..2`
* committed Egaroucid lv17 full-value artifact v1 as an experimental default

This is an experimental default, not an Elo result, not a self-play result,
not a production strength claim, and not publication readiness.

Artifact safety, promotion, rollback, and commit-policy details belong to:

* `docs/architecture/evaluation-artifacts.md`
* `data/eval/README.md`
* `tools/pattern/artifacts/check_eval_artifact_commit_policy.py`

For normal development, use this doc for current status, architecture docs for
current contracts, and `docs/experiments/README.md` for historical evidence.

## Adopted Route

The adopted path is the progressive move-teacher route that led to the current
experimental default artifact:

1. Sequence/transcript importer for local Egaroucid-derived game transcripts.
2. Normalized TSV schema v2 with explicit game, board, occurrence, split, and
   side-to-move label identity.
3. Connected-board-game split for more honest sequence-derived validation.
4. Compact pattern dataset rows for scalable local training and diagnostics.
5. Trainer v0c/v0d value diagnostics and the local `pattern-rank-v0e` pairwise
   ranking trainer on `pattern-v2-endgame-lite`, all keeping runtime artifact
   shape compatible with the existing pattern evaluator.
6. Exact move-teacher child labels for legal after-move boards.
7. Move-teacher cache and partial-miss solve flow to reuse exact solves without
   committing labels or generated datasets.
8. Fair 100k exact-root baseline derived from the same move-teacher source.
9. Repeated 100k move-teacher validation against the fair exact-root baseline.
10. Broader bounded arena validation against exact-root v2 and v1 artifacts.
11. Baseline-regret hard-root selection with phase-balanced quotas.
12. Depth-5 relabeling of 5,000 difficult roots and conflict-safe overlay over
    the depth-4 corpus.
13. Residual rank training for phases `5..9`, with late phases frozen and only
    the first six pattern families active in the residual.
14. Paired arena gates: 52.88% over 1,024 fixed-depth games and 53.71% over
    512 fixed-time games, both with bootstrap intervals above 50%.
15. Learned progressive-search artifact v1 committed as the experimental
    default.
16. WHTOR importer and deterministic played-move policy sidecar covering a
    local 1977-2025 snapshot.
17. WHTOR policy/search bootstrap that beat the previous default at depth 3,
    depth 5, and 10 ms per move.
18. Full-corpus streaming pairwise training over 137,548 games and 8.23 million
    recorded moves.
19. Paired arena gates against that bootstrap: 71.78% over 2,000 depth-3
    games, 66.89% over 512 depth-5 games, and 66.02% over 512 fixed-time games.
20. A second WHTOR pass and three depth-6 correction candidates rejected after
    failing to reproduce a positive result.
21. A board-core-only generator produced 1,000 random 16-ply promotion
    openings; all 137,548 WHTOR games were replayed and showed zero opening
    board or transcript-prefix overlap.
22. Direct gates against the prior default passed at 73.35% depth 3, 69.14%
    depth 5, and 66.99% at 10 ms plus exact8, with clean games and depth-3
    argument-order/same-artifact sanity.
23. Full-corpus WHTOR artifact v1 committed as the experimental default.
24. Full-corpus mixed Egaroucid 7.4.0/7.5.1 board-score training updated all
    phases and passed direct depth-3, depth-5, and fixed-time gates against the
    full-WHTOR policy default.
25. Short-opening depth-3 gates directly exercised phases 0-2 and passed from
    every unique 4-ply board plus 256 8-ply and 256 11-ply openings.

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

Pattern-set capacity experiments remain separate from default-artifact
promotion. The promoted artifact stays on the stable v2 schema.

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

The committed WHTOR artifact is acceptable as an experimental engineering
default because direct independent-opening validation selected it, the payload
is minimal, provenance is explicit, and source/intermediate WHTOR material is
not redistributed.

Future strength claims need separate validation. The current default should be
treated as a runtime/default-selection milestone, not as production strength.

## Next Work

Current next actions:

* Run NTest or another external engine match before making production-strength
  or Elo claims.
* Expand independent fixed-time openings if a narrower confidence interval is
  required.
* Continue hard-root refresh with new independent positions; do not repeatedly
  tune on the committed arena openings.
* Harden artifact promotion gates around manifest/provenance/default-pointer
  consistency and rollback review.
* Run bounded local v0e versus v0c/v0d ranking comparisons before considering
  any artifact-promotion decision.
* Keep future pattern-set capacity work separate from default maintenance.
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
| Import and normalization | Sequence transcripts can be replayed into normalized TSV schema v2 with deterministic identity, connected-board-game split support, and leakage diagnostics. Board-score archives can be checksum-validated and streamed into normalized schema v2 with deterministic board-disjoint splits and bounded per-phase sampling. |
| Root selection | Local phase-stratified root selection can take a uniform base quota plus explicit per-phase quota overrides, preserve connected splits by default, or explicitly re-hash a board/game-unique cap-1 selected set when early-board components make the source split unusably imbalanced. It reports effective coverage and post-selection leakage without generating labels. |
| Search move-teacher labels | A local-only artifact-search generator emits move-teacher TSV v3, ranks every legal move for phases `0..9`, records static fallback values for residual training, and uses either explicit full coverage or a provenance-visible phase-aware bootstrap; exact move teaching remains the late-game route. |
| Dataset shape | Compact TSV example rows are the scalable path for local pattern training diagnostics. |
| Pattern sets | `pattern-v1-buro-lite` is the earlier production-ish schema; `pattern-v2-endgame-lite` remains the current default schema. |
| Trainer diagnostics | The streaming Egaroucid board-score trainer supports all-position multi-pass value training, per-epoch learning rates, runtime-equivalent inter-epoch quantization, phase freezing, and canonical-board holdouts. v0c/v0d and v0e remain available for sampled value and move-ranking experiments. |
| Exact teacher labels | Exact root-label experiments were useful diagnostics but did not become the adopted default route. |
| Move-teacher labels | Exact child labels made root move-ranking and decision leverage visible with the existing value trainer. |
| Cache/materialization | Move-teacher cache and materialization flows support large local reruns while keeping labels and child-normalized TSVs local-only. |
| Growth cycle | 50k and 100k local growth-cycle validations selected the move-teacher route for broader validation. |
| Full-phase local cycle | `tools/pattern/train/run_full_phase_growth_cycle.py` connects phase-stratified selection, explicit-artifact search teaching, late exact teaching, warm-start/frozen-phase v0e training, fixed-point residual export, ranking, and bounded late/full-game local arenas without changing the default artifact; phase quota and split exceptions remain visible in reports. |
| Hard-root progression | Baseline-regret selection, complete-root depth overlay, cross-depth child-conflict exclusion, and disjoint per-provenance trainer sidecars are checked local-only operations. |
| Arena validation | Independent paired fixed-depth and fixed-time arenas supported promoting the Egaroucid lv17 full-value artifact over the full-WHTOR policy default; short-opening gates directly exercise updated phases 0-2. |
| Artifact v2 | `pattern-v2-egaroucid-lv17-full-value-v1` is the experimental default; the full-WHTOR policy artifact remains available for rollback and comparison. |
| Archive policy | Long logs, tables, command examples, and obsolete next actions belong in `docs/experiments/README.md` and linked experiment docs. |

Update this document when the current status, adopted route, artifact default,
promotion policy, or active next work changes. Update architecture docs when
the intended contract changes.
