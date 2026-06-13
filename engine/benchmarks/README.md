# Engine Benchmarks

Engine benchmarks are small local executables for tracking hot-path performance.
They are not correctness tests and are not a CI pass/fail gate.

## Running

Configure a Release build with benchmarks enabled:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DVIBE_OTHELLO_BUILD_BENCHMARKS=ON
cmake --build build-bench --config Release
./build-bench/engine/benchmarks/vibe_othello_board_core_bench
```

Run benchmarks on an otherwise quiet machine and compare results from the same
machine, compiler, build type, and command whenever possible.

## Layout

| Path | Role |
| --- | --- |
| `board_core_bench.cc` | Board-core hot-path benchmark executable. |
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

Checksum stability helps confirm that benchmarked behavior did not change.
Timing values are environment-dependent and should be treated as local comparison
data, not universal truth.
