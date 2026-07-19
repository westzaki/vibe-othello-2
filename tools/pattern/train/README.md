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
   and repeat `--freeze-phase` for retained late phases.
6. `tools/pattern/export/export_v0b.py` exports fixed-point runtime weights.
   The export must use the same residual boundary as training.
7. Rank diagnostics and paired fixed-depth/fixed-time arenas decide promotion.
   Training loss alone is not a strength result.

Every stage writes a machine-readable report with source and output checksums.
The hard-root selector preserves the teacher's explicit split assignments; the
generator and overlay reject incomplete roots and cross-split child collisions.
Progressive training passes the retained shallow roots and deeper overlay roots
as separate, disjoint sidecars so each input retains one search configuration
in the trainer report.

Run each tool with `--help` for its complete interface. The smoke tests wired
through `tools/pattern/{export,labels,train}/CMakeLists.txt` cover artifact
round-tripping, hard-root selection, overlay conflict handling, provenance
validation, and residual training.
