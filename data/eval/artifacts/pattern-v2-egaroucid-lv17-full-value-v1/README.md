# pattern-v2-egaroucid-lv17-full-value-v1

Status: experimental-default.

This artifact uses all 25,514,097 board-score positions in
`Egaroucid_Train_Data.zip`, produced by Egaroucid 7.5.1 lv17. Each board is
paired with its side-to-move final disc-difference estimate.

Training starts from `pattern-v2-wthor-full-policy-v1`. Phases 0 through 9 use
five full-corpus passes with learning rates `0.02, 0.05, 0.02, 0.05, 0.1`.
Phases 10 through 12 then use one full-corpus pass at learning rate `0.05`.
The complete route visits 153,084,582 source positions. Runtime evaluation
keeps the deterministic fallback plus learned residual through phase 9.

Promotion used 1,000 independently generated random 16-ply opening pairs.
An exact scan of all 25,514,097 training boards found zero overlap with the
promotion opening boards.

Against the previous default, `pattern-v2-wthor-full-policy-v1`, the artifact
scored 68.05% over 2,000 depth-3 games (95% paired interval 66.38-69.75%),
67.97% over 512 depth-5 games (64.84-71.09%), and 68.85% over 512 games at
10 ms per move with exact solving from 8 empties (65.63-72.07%). Every run had
zero failed and illegal games. The depth-3 argument-order swap was an exact
complement, and both same-artifact controls scored exactly 50%.

Use this artifact explicitly with:

```sh
./build/tools/engine-cli/vibe-othello-engine-cli bestmove \
  --moves "" --depth 4 \
  --eval-artifact \
  data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json
```

Source attribution and reproducible training and promotion metadata are in
`NOTICE.md` and `provenance.json`.
