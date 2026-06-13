# Engine Benchmarks

Engine benchmarks are small local executables for tracking hot-path performance.
They are not correctness tests and are not a CI pass/fail gate.

## Running

Configure a Release build with benchmarks enabled:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DVIBE_OTHELLO_BUILD_BENCHMARKS=ON
cmake --build build-bench --config Release
./build-bench/engine/benchmarks/vibe_othello_board_core_bench
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

## Layout

| Path | Role |
| --- | --- |
| `board_core_bench.cc` | Board-core hot-path benchmark executable. |
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

JSONL output emits one JSON object per position/mode/depth result. The schema is:

- `position_id`, `category`
- `mode`, `tt_mode`, `depth`, `evaluator`
- `score`, `best_move`, `pv`
- `root_moves`, with `move`, `score`, `bound`, `depth`, `exact`, `selective`
- `nodes`, `eval_calls`, `beta_cutoffs`, `alpha_updates`
- `tt_probes`, `tt_hits`, `tt_stores`, `tt_cutoffs`
- `elapsed_ns`, `nps`

Checksum stability helps confirm that benchmarked behavior did not change.
Timing values are environment-dependent and should be treated as local comparison
data, not universal truth. Timing and NPS values are not intended to be CI gates;
future golden checks should compare deterministic fields such as score, best
move, PV, root move scores, and search statistics separately from timing.
