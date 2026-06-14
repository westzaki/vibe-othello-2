# Engine Benchmarks

Engine benchmarks are small local executables for tracking hot-path performance.
They are not correctness tests and are not a CI pass/fail gate.

## Running

Configure a Release build with benchmarks enabled:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DVIBE_OTHELLO_BUILD_BENCHMARKS=ON
cmake --build build-bench --config Release
./build-bench/engine/benchmarks/vibe_othello_board_core_bench
./build-bench/engine/benchmarks/vibe_othello_endgame_bench --tsv --max-empties 12
./build-bench/engine/benchmarks/vibe_othello_search_bench
```

Run benchmarks on an otherwise quiet machine and compare results from the same
machine, compiler, build type, and command whenever possible.

Search benchmark output is TSV by default and runs depth 6..8 unless `--depth`
is specified. Use `--depth 8`, `--depth 6..8`, or `--depth 6-8` to choose search
depths. Use `--csv` for comma-separated output, `--jsonl` for JSON Lines output,
and `--mode fixed|iterative|all` to restrict the search mode column. Use
`--tt off|ordering|midgame|both` to choose iterative-search transposition-table
options.

Use the search benchmark matrix options to compare search features without
changing code:

```sh
./build-bench/engine/benchmarks/vibe_othello_search_bench \
  --mode iterative \
  --depth 6 \
  --pvs both \
  --aspiration both \
  --history off \
  --killers off \
  --iid off \
  --eval disc \
  --exact-endgame 8 \
  --endgame-tt both \
  --endgame-parity both \
  --corpus engine/fixtures/search/positions.tsv \
  --jsonl
```

`--pvs`, `--aspiration`, `--history`, `--killers`, `--iid`, `--endgame-tt`,
and `--endgame-parity` accept `off|on|both`. `--eval` accepts
`disc|simple|all`; `disc` emits the existing `disc_difference` evaluator name
for output compatibility. `--exact-endgame N` enables exact endgame cutover when
`N > 0`; `0` keeps it disabled. Search-option matrix values apply to iterative
search rows. Fixed-depth rows use the public fixed-depth API, so only the
evaluator choice applies there.

Use `--corpus engine/fixtures/search/positions.tsv` to run the checked-in search
corpus. If `--corpus` is omitted, the executable uses the built-in corpus for
backward-compatible local runs. Corpus rows are TSV with:

```text
id	category	position	depths	notes
```

`position` uses the board-core serialized position format. `depths` accepts the
same syntax as `--depth`, such as `6`, `6..8`, or `6-8`. A command-line `--depth`
overrides per-row corpus depths.

The search fixture corpus currently covers:

- `initial`
- `early_balanced`
- `early_high_mobility`
- `midgame_balanced`
- `midgame_high_mobility`
- `corner_available`
- `pass_position`
- `late_midgame`

Endgame benchmark output is TSV by default and measures the root-only exact
endgame solver through `solve_exact_endgame`. Use
`--csv` for comma-separated output, `--jsonl` for JSON Lines output,
`--parity on|off|both` to choose exact endgame parity ordering,
`--tt off|on|both` to choose exact endgame transposition-table use,
`--root-mode all|best` to choose root reporting behavior, `--repeat N` to repeat
each position, and `--max-empties N` to cap the default or external corpus by
empty count. Defaults are `--parity on`, `--tt off`, and `--root-mode all`,
which preserve the original non-TT all-root benchmark shape except for newly
emitted columns.

Use `--corpus path/to/endgame.tsv` to run an external exact endgame corpus. If
`--corpus` is omitted, the executable first uses the checked-in
`engine/fixtures/endgame/positions.tsv` corpus when run from the repository
root. If that file is unavailable, it falls back to a deterministic built-in
corpus with the same 0/1/4/6/8/10/12-empty positions, including a forced-pass
case. External corpus rows are TSV with:

```text
id	category	position	expected_empties	notes
```

`position` uses the board-core serialized position format.

Use `--corpus engine/fixtures/endgame/positions.tsv` to run the checked-in
endgame corpus used by deterministic golden checks.

For small-empty exact-score changes, use a low cap to isolate the shared
0/1/2/3-empty path:

```sh
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --repeat 10 \
  --max-empties 3 \
  --corpus engine/fixtures/endgame/positions.tsv
```

Compare the existing `nodes`, `endgame_nodes`, `elapsed_ms`, and `nps` fields
against the previous baseline or a same-machine before run. TSV and CSV outputs
now include additional option and TT-stat columns; prefer header-based parsing
for local scripts.

For parity-ordering changes, keep TT fixed and compare node counts by empty
count:

```sh
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --parity both \
  --tt off \
  --repeat 3 \
  --max-empties 12 \
  --corpus engine/fixtures/endgame/positions.tsv
```

For exact endgame TT changes, keep parity fixed and compare the `tt_mode`,
`tt_probes`, `tt_hits`, `tt_cutoffs`, and `tt_stores` fields alongside `nodes`
and `elapsed_ms`:

```sh
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --parity on \
  --tt both \
  --repeat 3 \
  --max-empties 12 \
  --corpus engine/fixtures/endgame/positions.tsv
```

For exact endgame root reporting changes, keep parity and TT fixed and compare
`root_mode`, `score`, `best_move`, `nodes`, `elapsed_ms`, and
`root_moves_searched`:

```sh
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --root-mode all \
  --max-empties 12 \
  --corpus engine/fixtures/endgame/positions.tsv
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --root-mode best \
  --max-empties 12 \
  --corpus engine/fixtures/endgame/positions.tsv
```

For interaction checks before changing root mode, small-empty paths, WLD, or TT
behavior, run the full parity/TT matrix. The deterministic run order is
`parity_ordering=off,tt_mode=off`, `off,on`, `on,off`, then `on,on` for each
position and repeat:

```sh
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --parity both \
  --tt both \
  --repeat 2 \
  --max-empties 12 \
  --corpus engine/fixtures/endgame/positions.tsv
```

Parity and TT are correctness-neutral options for exact-score search. Compare
`score`, `best_move`, `pv`, `exact`, and `stopped` first; performance analysis
should then look at nodes, timing, and TT hit/cutoff rates. `tt_mode=off` should
emit zero TT counters.

## Layout

| Path | Role |
| --- | --- |
| `board_core_bench.cc` | Board-core hot-path benchmark executable. |
| `endgame_bench.cc` | Exact endgame benchmark executable with checked-in corpus default and built-in fallback. |
| `search_bench.cc` | Search benchmark executable with configurable fixed depths. |
| `../fixtures/search/positions.tsv` | Search benchmark corpus for repeatable local and future golden checks. |
| `../fixtures/endgame/positions.tsv` | Exact endgame benchmark corpus for repeatable local and future golden checks. |
| `baselines/` | Optional checked-in reference measurements and their documentation. |
| `results/` | Local scratch space for benchmark outputs. Contents are ignored by Git. |
| `scripts/common/` | Low-level shared Python helpers for benchmark management scripts. |
| `scripts/search/` | Search golden and aggregate baseline helper scripts. |
| `scripts/endgame/` | Endgame golden and aggregate baseline helper scripts. |

Benchmark helper scripts are part of the engine benchmark suite, not generic
top-level developer tools. Keep benchmark-specific schema validation in the
search, endgame, or board-core script that owns that output shape. Shared
helpers should stay low-level: JSON loading/writing, common envelope checks,
metadata helpers, and primitive type checks.

## Reporting Results

Performance PRs should include relevant local measurements or explain why
measurement was not possible.

Report:

- benchmark command
- machine/compiler/build type when relevant
- before/after `ns/op`
- checksum values

For search benchmarks, report the depth argument and the emitted columns:
`position_name`, `mode`, `variant_id`, `tt_mode`, `evaluator`, `pvs`,
`aspiration`, `history`, `killers`, `iid`, `exact_endgame`,
`endgame_exact_empties`, `endgame_tt`, `endgame_parity`, `depth`, `score`,
`best_move`, `nodes`, `eval_calls`, `terminal_nodes`, `pass_nodes`,
`beta_cutoffs`, `alpha_updates`, `pvs_researches`, `aspiration_fail_lows`,
`aspiration_fail_highs`, `iid_searches`, `endgame_nodes`, `tt_probes`,
`tt_hits`, `tt_stores`, `tt_cutoffs`, `tt_overwrites`, `tt_collisions`,
`tt_rejected_stores`, `tt_invalid_best_move_stores`, `elapsed_ms`, and `nps`.

For endgame benchmarks, report the max-empty cap and the emitted columns:
`position_id`, `category`, `empties`, `repeat`, `parity_ordering`, `tt_mode`,
`root_mode`, `score`, `best_move`, `exact`, `stopped`, `completed_depth`, `nodes`,
`endgame_nodes`, `terminal_nodes`, `pass_nodes`, `beta_cutoffs`,
`alpha_updates`, `root_moves_searched`, `tt_probes`, `tt_hits`, `tt_cutoffs`,
`tt_stores`, `tt_overwrites`, `tt_collisions`, `tt_rejected_stores`,
`tt_invalid_best_move_stores`, `elapsed_ms`, and `nps`.

Search JSONL output emits one JSON object per position/mode/depth result. The
schema is:

- `position_id`, `category`
- `mode`, `variant_id`, `tt_mode`, `depth`, `evaluator`
- `pvs`, `aspiration`, `history`, `killers`, `iid`
- `exact_endgame`, `endgame_exact_empties`, `endgame_tt`, `endgame_parity`
- `score`, `best_move`, `pv`
- `root_moves`, with `move`, `score`, `bound`, `depth`, `exact`, `selective`
- `nodes`, `eval_calls`, `terminal_nodes`, `pass_nodes`
- `beta_cutoffs`, `alpha_updates`
- `pvs_researches`, `aspiration_fail_lows`, `aspiration_fail_highs`,
  `iid_searches`, `endgame_nodes`
- `tt_probes`, `tt_hits`, `tt_stores`, `tt_cutoffs`
- `tt_overwrites`, `tt_collisions`, `tt_rejected_stores`,
  `tt_invalid_best_move_stores`
- `elapsed_ns`, `nps`

Endgame JSONL output emits one JSON object per position/repeat result. The
schema is:

- `position_id`, `category`, `position`
- `mode`, currently `exact_score`
- `empties`, `repeat`
- `parity_ordering`, currently `on` or `off`
- `tt_mode`, currently `off` or `on`
- `root_mode`, currently `all` or `best`
- `score`, `best_move`, `exact`, `stopped`, `completed_depth`
- `nodes`, `endgame_nodes`, `terminal_nodes`, `pass_nodes`
- `beta_cutoffs`, `alpha_updates`, `root_moves_searched`
- `tt_probes`, `tt_hits`, `tt_cutoffs`, `tt_stores`
- `tt_overwrites`, `tt_collisions`, `tt_rejected_stores`,
  `tt_invalid_best_move_stores`
- `elapsed_ms`, `nps`
- `pv`
- `root_moves`, with `move`, `score`, `bound`, `depth`, `exact`, `selective`
- `notes`

## Search Golden Checks

The checked-in deterministic search golden is:

```text
engine/fixtures/search/golden/discdiff_depth_1_4.jsonl
```

It is generated from the checked-in corpus with iterative search, disc-difference
evaluation, depths 1..4, TT mode `both`, and JSONL output:

```sh
./build-bench/engine/benchmarks/vibe_othello_search_bench \
  --mode iterative \
  --depth 1..4 \
  --tt both \
  --corpus engine/fixtures/search/positions.tsv \
  --jsonl > /tmp/search_actual.jsonl
engine/benchmarks/scripts/search/check_golden.py \
  /tmp/search_actual.jsonl \
  engine/fixtures/search/golden/discdiff_depth_1_4.jsonl
```

`engine/benchmarks/scripts/search/check_golden.py` normalizes both actual and
golden JSONL before comparing deterministic result fields only: position
metadata, mode, TT mode, depth, evaluator, score, best move, PV, and root move
score/bound/depth/exact/selective values keyed by move. It intentionally does
not gate `nodes`, eval/search statistics, TT statistics, `elapsed_ns`, or
`nps`. Those values can move with implementation details, machine load, and
benchmark environment, so they should be reviewed separately when relevant.

Golden updates are manual. Regenerate with:

```sh
engine/benchmarks/scripts/search/generate_golden.sh
```

Review the JSONL diff before committing it. Do not auto-update golden files in
CI. The checked-in golden file is deterministic-only normalized JSONL, so it
does not contain timing, node count, eval/search statistics, or TT statistics
even though the raw `search_bench --jsonl` output includes them.

Checksum stability helps confirm that benchmarked behavior did not change.
Timing values are environment-dependent and should be treated as local comparison
data, not universal truth. Timing and NPS values are not intended to be CI gates;
golden checks compare deterministic fields such as score, best move, PV, and
root move scores separately from timing and search statistics.

## Search Baselines

The checked-in search benchmark aggregate baseline is:

```text
engine/benchmarks/baselines/search/2026-06-14-<short-sha>-<machine>-<compiler>-release.json
```

It is generated from the checked-in search corpus with iterative search,
disc-difference evaluation, depth 5, TT mode `both`, and raw JSONL output:

```sh
./build-bench/engine/benchmarks/vibe_othello_search_bench \
  --mode iterative \
  --depth 5 \
  --tt both \
  --corpus engine/fixtures/search/positions.tsv \
  --jsonl \
  > engine/benchmarks/results/search-iterative-discdiff-depth5-raw.jsonl
```

Regenerate the aggregate baseline with:

```sh
engine/benchmarks/scripts/search/generate_baseline.sh
```

The `--jsonl` output is raw benchmark output: one JSON object per
position/mode/depth result. The checked-in baseline is an aggregate JSON
document built from that raw output. It stores environment metadata,
`measured_commit` and `measured_revision` for the measured engine commit,
deterministic result fields, PV and root move summaries, search statistics, TT
statistics, and local timing summaries. It is comparison data only, not a
performance gate. Prefer comparing runs from the same machine, compiler, build
type, command, and corpus.

Sanity-check the checked-in aggregate baseline JSON with:

```sh
engine/benchmarks/scripts/search/check_baseline.py \
  engine/benchmarks/baselines/search/2026-06-14-<short-sha>-<machine>-<compiler>-release.json
```

Search golden checks and search baselines have different purposes. Golden
checks compare deterministic behavior and intentionally omit nodes, search
statistics, TT statistics, timing, and NPS. Search baselines keep those
aggregate measurement fields available for local review, but timing and NPS are
not CI gates.

See `engine/benchmarks/baselines/search/README.md` for the aggregate JSON shape
and regeneration checklist.

## Endgame Baselines

The checked-in exact endgame benchmark baseline is:

```text
engine/benchmarks/baselines/endgame/2026-06-14-8f89540-apple-silicon-macos-arm64-apple-clang-17-release.json
```

It is generated from the checked-in endgame corpus with exact endgame search,
repeat count 3, a 12-empty cap, default parity ordering, TT disabled, and raw
JSONL output:

```sh
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --repeat 3 \
  --max-empties 12 \
  --corpus engine/fixtures/endgame/positions.tsv \
  > engine/benchmarks/results/endgame-exact-score-raw.jsonl
```

The `--jsonl` output is raw benchmark output: one JSON object per
position/repeat. The checked-in baseline is an aggregate JSON document built
from that raw output. It stores environment metadata, `measured_commit` and
`measured_revision` for the measured engine commit, deterministic search
statistics, the selected repeat timing, and raw repeat timing summaries. It is
comparison data only, not a performance gate. Prefer comparing runs from the
same machine, compiler, build type, command, and corpus.

There is currently no endgame `generate_baseline.sh` helper. Regenerate endgame
baselines manually from the raw JSONL command above, then validate the aggregate
JSON with the schema check below. Add a generator only when the baseline
selection policy is automated enough to make the helper unambiguous.

Sanity-check the checked-in aggregate baseline JSON with:

```sh
engine/benchmarks/scripts/endgame/check_baseline.py \
  engine/benchmarks/baselines/endgame/2026-06-14-8f89540-apple-silicon-macos-arm64-apple-clang-17-release.json
```

See `engine/benchmarks/baselines/endgame/README.md` for the aggregate JSON shape
and regeneration checklist.

## Endgame Golden Checks

The checked-in deterministic exact endgame golden is:

```text
engine/fixtures/endgame/golden/exact_score.jsonl
```

It is generated from the checked-in endgame corpus with exact endgame search,
repeat count 1, a 12-empty cap, default parity ordering, TT disabled, and JSONL
output:

```sh
./build-bench/engine/benchmarks/vibe_othello_endgame_bench \
  --jsonl \
  --repeat 1 \
  --max-empties 12 \
  --corpus engine/fixtures/endgame/positions.tsv > /tmp/endgame_actual.jsonl
engine/benchmarks/scripts/endgame/check_golden.py \
  /tmp/endgame_actual.jsonl \
  engine/fixtures/endgame/golden/exact_score.jsonl
```

`engine/benchmarks/scripts/endgame/check_golden.py` normalizes both actual and
golden JSONL before comparing deterministic result fields only: position
metadata, mode, empty count, score, best move, exact/stopped flags, completed
depth, PV, and root move score/bound/depth/exact/selective values keyed by
move. It intentionally does not gate `repeat`, option columns, node counts,
search statistics, `elapsed_ms`, or `nps`. Those values are expected to move
when exact endgame TT, parity ordering, or small-empty paths are introduced.
Matrix benchmark output from `--parity both` or `--tt both` has extra records
and is not the input shape for the checked-in single-policy golden file.

Golden updates are manual. Regenerate with:

```sh
engine/benchmarks/scripts/endgame/generate_golden.sh
```

Review the JSONL diff before committing it. Do not auto-update golden files in
CI. The checked-in golden file is deterministic-only normalized JSONL, so it
does not contain timing, node count, or search statistics even though the raw
`endgame_bench --jsonl` output includes them.
