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
depths. Use `--csv` for comma-separated output and `--mode fixed|iterative|all`
to restrict the search mode column. Use `--tt off|ordering|midgame|both` to
choose iterative-search transposition-table options.

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

Checksum stability helps confirm that benchmarked behavior did not change.
Timing values are environment-dependent and should be treated as local comparison
data, not universal truth.
