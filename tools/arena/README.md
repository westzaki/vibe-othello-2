# Engine Regression Arena

`vibe_othello_arena` runs deterministic baseline vs candidate regression matches.
It launches each engine as an external one-shot command and uses the current
checkout's board core as the referee for opening replay, legal move validation,
pass handling, terminal detection, and final disc counts.

## Example

```sh
build/tools/arena/vibe-othello-arena \
  --baseline-cmd "build/tools/engine-cli/vibe-othello-engine-cli bestmove --depth 4" \
  --candidate-cmd "build/tools/engine-cli/vibe-othello-engine-cli bestmove --depth 4" \
  --openings tools/arena/openings/smoke.txt \
  --out out/matches/smoke \
  --swap-colors \
  --timeout-ms 5000
```

The baseline and candidate commands are shell command fragments. The arena
appends `--moves "<move sequence>"` for each engine call.
Engine commands return one line in the form
`bestmove <coordinate|pass|none> score <score> depth <depth>`. `pass` means the
searched root is a legal forced pass, while `none` means search found no root
move, such as a terminal root or depth-zero query.

`vibe-othello-engine-cli` defaults to the disc-difference evaluator. For local
learned-pattern artifact checks, pass pattern evaluator options inside the
engine command:

```sh
build/tools/engine-cli/vibe-othello-engine-cli bestmove \
  --depth 3 \
  --eval pattern \
  --pattern-set pattern-v2-endgame-lite \
  --pattern-weights /path/to/v0b.weights.bin
```

## Openings

Opening files are plain text:

```text
# comment
start:
f5:
custom: d3 c3
```

Comments start with `#`. Blank lines are ignored. Each non-comment line may use
`id: moves...`. If the right side is empty and the id itself is a legal move
sequence, the id is also used as the sequence. Invalid openings are rejected at
startup.

## Output

The `--out` directory receives:

```text
manifest.json
summary.json
games.jsonl
```

`manifest.json` records the commands, openings path, color-swap flag, and
timeout. `games.jsonl` contains one JSON object per game with final disc counts,
winner, candidate result, candidate disc differential, and reason. `summary.json`
aggregates candidate wins, draws, losses, score, win rate, average disc
differential, and invalid game count.

## Color Swaps

With `--swap-colors`, each opening is played twice: candidate as black, then
candidate as white. Without it, candidate plays black for every opening.

## Timeout And Invalid Moves

`--timeout-ms` applies to each one-shot engine call. A timeout, crash, parse
error, or illegal move ends that game immediately and records a loss for the
offending engine.

## Scope

This tool is for engine regression and match evaluation. Human playable UI, Web
UI, WASM integration, production learned weights, and external Edax/Egaroucid
adapters are future work. Process launching currently targets Mac/Linux; Windows
portability is outside the initial scope.
