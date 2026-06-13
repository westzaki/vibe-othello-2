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

Use `--corpus engine/testdata/search/positions.tsv` to run the checked-in search
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
endgame solver through `search_iterative` with `exact_endgame = true`. Use
`--csv` for comma-separated output, `--jsonl` for JSON Lines output,
`--repeat N` to repeat each position, and `--max-empties N` to cap the built-in
or external corpus by empty count.

Use `--corpus path/to/endgame.tsv` to run an external exact endgame corpus. If
`--corpus` is omitted, the executable uses a deterministic built-in corpus with
0/1/4/6/8/10/12-empty positions, including a forced-pass case. External corpus
rows are TSV with:

```text
id	category	position	expected_empties	notes
```

`position` uses the board-core serialized position format.

## Layout

| Path | Role |
| --- | --- |
| `board_core_bench.cc` | Board-core hot-path benchmark executable. |
| `endgame_bench.cc` | Exact endgame benchmark executable with built-in corpus. |
| `search_bench.cc` | Search benchmark executable with configurable fixed depths. |
| `../testdata/search/positions.tsv` | Search benchmark corpus for repeatable local and future golden checks. |
| `baselines/` | Optional checked-in reference measurements and their documentation. |
| `results/` | Local scratch space for benchmark outputs. Contents are ignored by Git. |

## Reporting Results

Performance PRs should include relevant local measurements or explain why
measurement was not possible.

Report:

- benchmark command
- machine/compiler/build type when relevant
- before/after `ns/op`
- checksum values

For search benchmarks, report the depth argument and the emitted columns:
`position_name`, `mode`, `tt_mode`, `depth`, `score`, `best_move`, `nodes`,
`eval_calls`, `beta_cutoffs`, `alpha_updates`, `tt_probes`, `tt_hits`,
`tt_stores`, `tt_cutoffs`, `elapsed_ms`, and `nps`.

For endgame benchmarks, report the max-empty cap and the emitted columns:
`position_id`, `category`, `empties`, `repeat`, `score`, `best_move`, `exact`,
`stopped`, `completed_depth`, `nodes`, `endgame_nodes`, `terminal_nodes`,
`pass_nodes`, `beta_cutoffs`, `alpha_updates`, `root_moves_searched`,
`elapsed_ms`, and `nps`.

JSONL output emits one JSON object per position/mode/depth result. The schema is:

- `position_id`, `category`
- `mode`, `tt_mode`, `depth`, `evaluator`
- `score`, `best_move`, `pv`
- `root_moves`, with `move`, `score`, `bound`, `depth`, `exact`, `selective`
- `nodes`, `eval_calls`, `beta_cutoffs`, `alpha_updates`
- `tt_probes`, `tt_hits`, `tt_stores`, `tt_cutoffs`
- `elapsed_ns`, `nps`

## Search Golden Checks

The checked-in deterministic search golden is:

```text
engine/testdata/search/golden/discdiff_depth_1_4.jsonl
```

It is generated from the checked-in corpus with iterative search, disc-difference
evaluation, depths 1..4, TT mode `both`, and JSONL output:

```sh
./build-bench/engine/benchmarks/vibe_othello_search_bench \
  --mode iterative \
  --depth 1..4 \
  --tt both \
  --corpus engine/testdata/search/positions.tsv \
  --jsonl > /tmp/search_actual.jsonl
tools/search/check_golden.py \
  /tmp/search_actual.jsonl \
  engine/testdata/search/golden/discdiff_depth_1_4.jsonl
```

`tools/search/check_golden.py` normalizes both actual and golden JSONL before
comparing deterministic result fields only: position metadata, mode, TT mode,
depth, evaluator, score, best move, PV, and root move
score/bound/depth/exact/selective values keyed by move. It intentionally does
not gate `nodes`, eval/search statistics, TT statistics, `elapsed_ns`, or `nps`.
Those values can move with implementation details, machine load, and benchmark
environment, so they should be reviewed separately when relevant.

Golden updates are manual. Regenerate with:

```sh
tools/search/generate_golden.sh
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
