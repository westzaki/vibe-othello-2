# Data Import

## WTHOR

`import_wthor.py` reads the Fédération Française d'Othello (FFO) WTHOR 8x8
game files (`.wtb`) and official zip archives. It validates the binary header
and record sizes, converts WTHOR coordinates, and delegates every legal replay,
implicit pass, and board serialization operation to the engine board core.

The supported local source snapshot is the FFO database last updated
2026-02-24:

* source page: `https://www.ffothello.org/informatique/la-base-wthor/`
* format: `https://www.ffothello.org/wthor/Format_WThor.pdf`
* years: 1977 through 2025
* files: 49 `.wtb` files
* games: 137,548

Raw WTHOR files and generated import or training outputs are local-only.
Reviewed runtime artifacts follow `data/eval/README.md`.

### Build

```sh
cmake -S . -B build
cmake --build build --target vibe_othello_wthor_sequence_replay
```

### Import the 24-empty Cutoff

The following produces one normalized observed-outcome row for each eligible
terminal transcript at 24 empties, plus a separate exact theoretical WLD
sidecar:

```sh
WTHOR_DIR=/path/to/wthor
OUT_DIR=/path/to/local/wthor-derived

mkdir -p "$OUT_DIR"

python3 tools/data-import/import_wthor.py \
  --input "$WTHOR_DIR" \
  --manifest data/corpora/manifests/wthor-ffo-2026-02-24-local.manifest.json \
  --replay-helper build/tools/data-import/vibe-othello-wthor-sequence-replay \
  --output "$OUT_DIR/wthor-normalized-ply36.tsv" \
  --theoretical-wld-out "$OUT_DIR/wthor-theoretical-wld.tsv" \
  --report "$OUT_DIR/wthor-import-report.json" \
  --min-ply 36 \
  --max-ply 36 \
  --ply-stride 1 \
  --emit-terminal \
  --strict-board-disjoint-splits \
  --progress-every-games 25000
```

`--min-ply`, `--max-ply`, and `--ply-stride` may instead select observed
training positions across the game. `--max-games` and `--max-positions` are
available for bounded local experiments. The default connected-board-game
split keeps identical side-to-move-relative boards and their semantic games
in one measurement split.

For move-ranking residual training across the currently learned phases, exact
cutoffs can be selected without materializing every intervening ply:

```sh
python3 tools/data-import/import_wthor.py \
  --input "$WTHOR_DIR" \
  --manifest data/corpora/manifests/wthor-ffo-2026-02-24-local.manifest.json \
  --replay-helper build/tools/data-import/vibe-othello-wthor-sequence-replay \
  --output "$OUT_DIR/wthor-normalized-phase-boundaries.tsv" \
  --policy-out "$OUT_DIR/wthor-played-move-policy.tsv" \
  --report "$OUT_DIR/wthor-phase-boundaries-report.json" \
  --selected-ply 23 --selected-ply 27 --selected-ply 32 \
  --selected-ply 36 --selected-ply 41 \
  --strict-board-disjoint-splits
```

Repeated `--selected-ply` values replace the range/stride selection. These
roots enter child phases 5 through 9 after one legal move.

`--policy-out` aggregates the next move actually played for every emitted
decision root by `(root_board_id, move)`. It retains occurrence and
win/draw/loss counts from the root side-to-move perspective. If the next
canonical token is a forced pass, that root is counted in the import report
but excluded from the policy sidecar because it has no move choice. The
sidecar is empirical play frequency, not a claim that every recorded move is
optimal.

### Label Semantics

The normalized TSV is schema v2 and can be consumed by the existing pattern
dataset builder. Its label is:

```text
label_kind = observed_final_disc_diff
label_unit = final_disc_diff
label_perspective = side_to_move
```

Only transcripts that reach an engine terminal position receive this observed
label. A legal WTHOR transcript that stops early, for example because of a
resignation, can still contribute a theoretical WLD cutoff row but is never
given a fabricated observed terminal score.

WTHOR stores actual and theoretical Black scores using a 64-square convention.
That value can differ from this engine's terminal actual-disc difference when
a game ends with empty squares. The importer therefore does not relabel the
WTHOR numeric score as `teacher_exact_final_disc_diff`.

The optional theoretical sidecar emits only the scoring-convention-independent
sign at the header's configured empty-count cutoff:

```text
label_kind = wld
label_unit = wld
label_perspective = side_to_move
label_score_side_to_move = -1, 0, or 1
teacher_source = wthor-theoretical-score
```

Equal boards are de-duplicated, identical labels accumulate
`occurrence_count`, and conflicting exact WLD values reject the import. The
current disc-difference pattern dataset/trainer does not accept this WLD
sidecar; it is intended for WLD validation or a future explicitly WLD-aware
training objective.

The played-move policy sidecar is separate from both label contracts. It joins
to search move-teacher roots by `root_board_id`; the rank trainer can use its
aggregate frequency as an auxiliary cross-entropy objective without replacing
search scores or child disc-difference labels.

### Stream Every Played Decision

The selected-ply sidecar is useful when search has already materialized every
legal child for a bounded root set. It intentionally does not expand all
WHTOR decisions. To use every recorded decision without creating a
multi-gigabyte child TSV, run the streaming policy pretrainer:

```sh
./build/tools/pattern/train/vibe-othello-wthor-policy-pretrain \
  --input "$WTHOR_DIR" \
  --initial-artifact /path/to/base/manifest.json \
  --weights-out "$OUT_DIR/wthor-policy.weights.json" \
  --report-out "$OUT_DIR/wthor-policy.report.json" \
  --epochs 1 \
  --learning-rate 0.03 \
  --negative-count 2 \
  --freeze-phase 10 --freeze-phase 11 --freeze-phase 12 \
  --validation-modulus 20 \
  --seed 20260720
```

The executable validates and replays extracted `.wtb` files directly with the
engine board core. Every recorded decision is visited. Forced single choices
are counted but need no ranking update; every other played move is paired with
deterministically selected legal alternatives. Validation games never update
weights and score all legal moves. `--validation-modulus 0` performs a final
all-game refit after hyperparameters have been selected. WTHOR moves are
provisional empirical targets, not solved best moves, so deeper search labels
remain a reviewed correction campaign. A neutral or regressive correction is
not adopted; independent direct paired arenas remain the promotion gate.

### Audit Independent Promotion Openings

Generate the opening suite without reading WHTOR, then replay both the suite
and every WHTOR game through the engine helper:

```sh
cmake --build build --target \
  vibe_othello_generate_independent_openings \
  vibe_othello_wthor_sequence_replay

build/tools/arena/vibe-othello-generate-independent-openings \
  --output "$OUT_DIR/promotion-openings.txt" \
  --report-out "$OUT_DIR/promotion-openings-generator.json" \
  --count 1000 --plies 16 --seed 20260727

python3 tools/data-import/audit_wthor_openings.py \
  --input "$WTHOR_DIR" \
  --openings "$OUT_DIR/promotion-openings.txt" \
  --replay-helper build/tools/data-import/vibe-othello-wthor-sequence-replay \
  --report "$OUT_DIR/promotion-openings-wthor-audit.json" \
  --opening-source independent-board-core-random \
  --opening-generation-seed 20260727 \
  --opening-generation-plies 16 \
  --require-zero-overlap
```

The audit compares the final board id of every opening with every replayed
WHTOR training board. It also uses a stricter game check: a WHTOR canonical
transcript must not begin with any promotion opening transcript. The report
binds the opening SHA-256, generation seed and count, WHTOR aggregate source
checksum, board identity policy, and both overlap counts.

### Build a Pattern Dataset

The normalized observed-outcome TSV enters the existing compact pattern
dataset path:

```sh
./build/tools/pattern/dataset/vibe-othello-pattern-dataset-smoke \
  --normalized-tsv "$OUT_DIR/wthor-normalized-ply36.tsv" \
  --report "$OUT_DIR/wthor-pattern-dataset-report.json" \
  --pattern-set pattern-v2-endgame-lite \
  --index-mode raw \
  --output-format compact-tsv \
  > "$OUT_DIR/wthor-pattern-dataset.tsv"
```

This makes the data mechanically usable by the current trainer. It does not
establish label quality, playing strength, redistribution permission, or
permission to publish derived weights.

## Egaroucid Sequences

`import_egaroucid_sequences.py` remains the importer for local Egaroucid
sequence transcripts. Its existing manifest, replay helper, sampling modes,
and smoke tests are independent of the WTHOR binary adapter.

## Egaroucid Board Scores

`import_egaroucid_board_scores.py` streams `Egaroucid_Train_Data.zip` into
normalized TSV schema v2. The normalized label is the neutral
`teacher_value_disc_diff` in disc units from the side-to-move perspective.
Its generation procedure depends on occupied count:

* 4-15 occupied: 1,514,097 positions from Egaroucid for Console 7.4.0 lv17;
  enumerate all progressions through move 11, evaluate them, and apply negamax
* 16-63 occupied: 24,000,000 positions labeled with terminal scores from
  Egaroucid for Console 7.5.1 lv17 self-play

The corpus manifest and import report preserve both ranges and generation
procedures. Raw archives, normalized rows, and reports remain local-only.

```sh
python3 tools/data-import/import_egaroucid_board_scores.py \
  --input "$VIBE_OTHELLO_CORPORA/Egaroucid_Train_Data.zip" \
  --manifest \
  data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json \
  --output "$VIBE_OTHELLO_TRAINING/egaroucid-board-scores.tsv" \
  --report "$VIBE_OTHELLO_TRAINING/egaroucid-board-scores.report.json"
```
