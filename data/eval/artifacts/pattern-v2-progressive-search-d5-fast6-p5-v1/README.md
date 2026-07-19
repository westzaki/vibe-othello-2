# pattern-v2-progressive-search-d5-fast6-p5-v1

Status: experimental-default.

This artifact extends the previous exact late-game default with a search-teacher
residual for phases 5 through 9. It keeps the previous phase 10 through 12
weights frozen and score-equivalent after exact fixed-point rescaling.

The early/midgame residual uses the first six contiguous
`pattern-v2-endgame-lite` families:

* `edge-8`
* `near-edge-8`
* `diagonal-8`
* `diagonal-7`
* `corner-2x5`
* `corner-3x3`

Phases 0 through 4 continue to use the deterministic fallback. Phases 5
through 9 add the learned residual to that fallback. This cutoff was selected
by a fixed-depth ablation before fixed-time validation.

The search-teacher corpus starts from 18,236 phase-stratified roots. A
phase-balanced set of 5,000 roots where the depth-4 teacher corrected the
baseline evaluator was relabeled at depth 5. The deeper rows replace their
shallower roots; they are not appended as duplicate supervision. Twelve
additional shallow roots sharing a differently labeled deep child were
excluded completely.

The committed payload contains only the final runtime artifact. Raw transcripts,
normalized positions, teacher rows, pattern datasets, trainer reports, and
arena reports remain local-only.

Use this artifact explicitly with:

```sh
./build/tools/engine-cli/vibe-othello-engine-cli bestmove \
  --moves "" --depth 4 \
  --eval-artifact data/eval/artifacts/pattern-v2-progressive-search-d5-fast6-p5-v1/manifest.json
```

This is an engineering strength result, not an Elo rating, publication claim,
or official Egaroucid artifact.
