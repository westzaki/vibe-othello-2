# Progressive Search-Teacher Training

The promoted v2 route extends an existing artifact without changing its
pattern schema:

```text
runtime artifact
  -> trainer-weight import
  -> phase-balanced hard-root selection
  -> deeper search teaching
  -> complete-root overlay
  -> warm-start residual rank training
  -> runtime export and independent arenas
```

All corpora, teacher rows, trainer weights, and reports are local-only. Store
them under `local/` or another ignored directory. Only a reviewed runtime
artifact with its manifest, provenance, README, and NOTICE belongs under
`data/eval/artifacts/`.

## Reproducible stages

1. `tools/pattern/export/import_artifact_weights.py` validates a runtime
   artifact against its manifest and compiled pattern catalog, then emits
   deterministic trainer-weight JSON. Its smoke test requires byte-identical
   re-export.
2. `tools/pattern/labels/select_hard_search_teacher_roots.py` ranks complete
   roots by baseline regret and takes deterministic per-phase quotas.
3. `tools/pattern/labels/run_search_move_teacher_generation.py` generates the
   deeper labels with explicit artifact, checksum, search configuration, and
   resume fingerprints.
4. `tools/pattern/labels/overlay_search_move_teacher.py` replaces complete
   shallow roots, excludes cross-depth child conflicts, and records both input
   provenances and checksums.
5. `train_pattern.py --mode pattern-rank-v0e` warm-starts the imported
   weights. Use `--residual-baseline-through-phase` for early/midgame residual
   training, repeat `--trainable-pattern-id` to bound updated pattern families,
   and repeat `--freeze-phase` for retained late phases. An aggregate WTHOR
   played-move sidecar may be joined with `--played-move-policy`; its
   cross-entropy coefficient is explicit in `--policy-loss-weight`.
6. `tools/pattern/export/export_v0b.py` exports fixed-point runtime weights.
   The export must use the same residual boundary as training.
7. Rank diagnostics and paired fixed-depth/fixed-time arenas decide promotion.
   Training loss alone is not a strength result.

Every stage writes a machine-readable report with source and output checksums.
The hard-root selector preserves the teacher's explicit split assignments, then
rejects board or game-group cross-split collisions before output and records
both audit counts. The generator and overlay reject incomplete roots and
cross-split child collisions.
Progressive training passes the retained shallow roots and deeper overlay roots
as separate, disjoint sidecars so each input retains one search configuration
in the trainer report.

Played-move policy is a secondary empirical objective. For a matched root it
normalizes aggregate occurrence counts into a target move distribution and
applies cross-entropy to the same root logits, `-V(child) / rank_temperature`.
Search pairwise loss and optional child-value calibration remain unchanged.
Unmatched policy rows are reported and ignored; matched split, phase, and legal
move metadata must agree with the move-teacher root. Validation/test policy
roots are diagnostic only and never update weights.

## Full WTHOR Policy Pretraining

`vibe-othello-wthor-policy-pretrain` is the bounded-memory route for using
every decision in extracted WTHOR `.wtb` files. It loads an existing
`pattern-v2-endgame-lite` runtime artifact, replays games in a deterministic
shuffled order, and treats each recorded move as a provisional ranking target.
No expanded root/child TSV is created.

For a non-forced root, the trainer compares the played child with a
deterministic set of other legal children. `--negative-count 0` uses all
alternatives; a small positive count gives a sampled pairwise approximation
while still visiting every recorded decision. The root move score is
`-V(child)`, matching `pattern-rank-v0e`. The source artifact's
`fallback_additive_through_phase` is included in logits, but gradients update
only learned pattern terms. Phases 10 through 12 are frozen by default.

Transcript identity drives the optional validation split, keeping duplicate
games together. Validation roots evaluate every legal child and never update
weights. After hyperparameter selection, `--validation-modulus 0` enables an
all-game refit. `--epochs 0` evaluates an initial artifact without training.
The report records per-phase pairwise metrics, validation top-1 and
cross-entropy, source-file checksums, visited decisions, forced choices,
updates, and frozen phases.

The output is `pattern-eval-weights-v2` JSON for `export_v0b.py`. WTHOR moves
remain empirical human-game evidence, not exact labels; search-teacher
correction and paired fixed-depth/fixed-time arenas decide whether a candidate
is stronger.

Run each tool with `--help` for its complete interface. The smoke tests wired
through `tools/pattern/{export,labels,train}/CMakeLists.txt` cover artifact
round-tripping, hard-root selection, overlay conflict handling, provenance
validation, and residual training.
