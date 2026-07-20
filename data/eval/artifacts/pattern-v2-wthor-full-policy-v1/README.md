# pattern-v2-wthor-full-policy-v1

Status: experimental-default.

This artifact learns from the actual moves in all 137,548 games in the local
1977-2025 WHTOR snapshot. Each recorded non-forced move is treated as a
provisional best move and ranked against deterministic legal alternatives.
The full pass visited 8,231,199 recorded moves and trained on 7,892,880
non-forced decisions.

The training starts from the earlier WHTOR played-policy bootstrap, updates all
11 `pattern-v2-endgame-lite` feature families in phases 0 through 9, and keeps
the exact-teacher phase 10 through 12 weights frozen. Runtime evaluation keeps
the existing deterministic fallback plus learned-residual boundary through
phase 9.

For the default-promotion gate, a board-core-only generator produced 1,000
unique random 16-ply opening pairs with seed `20260727`. A complete engine
replay audit found zero final-opening-board overlaps and zero canonical
transcript-prefix overlaps against all 137,548 WHTOR training games.

The final artifact was then compared directly with the prior default,
`pattern-v2-progressive-search-d5-fast6-p5-v1`. It scored 73.35% over 2,000
depth-3 games (95% paired interval 71.68-75.08%), 69.14% over 512 depth-5
games (65.53-72.56%), and 66.99% over 512 games at 10 ms per move with exact
solving from 8 empties (63.67-70.31%). Every run had zero failed and illegal
games. The depth-3 argument-order swap was an exact complement, while both
same-artifact controls scored exactly 50% with zero paired disc difference.
These are paired engineering arenas, not Elo measurements.

Against the WHTOR bootstrap during learning, the selected full-corpus model
also scored 71.78% at depth 3, 66.89% at depth 5, and 66.02% at 10 ms with
exact8. Those bootstrap comparisons are retained as learning evidence, not as
the default-promotion proof.

A second full WHTOR pass was rejected after scoring 49.12% at depth 5. Three
depth-6 search-correction candidates were also rejected: they scored 49.50%,
49.10%, and 48.55% at depth 3, and the best small correction was neutral at
50.98% at depth 5. This is the measured saturation point for the current
features, corpus, and training route.

The WHTOR source is the
[Fédération Française d'Othello WTHOR database](https://www.ffothello.org/informatique/la-base-wthor/),
snapshot last updated 2026-02-24 and retrieved 2026-07-19. Source records,
generated training data, and local reports are not included. Source attribution
and lineage are recorded in `NOTICE.md` and `provenance.json`.

Use this artifact explicitly with:

```sh
./build/tools/engine-cli/vibe-othello-engine-cli bestmove \
  --moves "" --depth 4 \
  --eval-artifact data/eval/artifacts/pattern-v2-wthor-full-policy-v1/manifest.json
```
