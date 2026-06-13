# Vibe Othello 2

Full-scratch Othello/Reversi project.

This repository is built around a small, correct, fast board core.

Search, evaluation, pattern learning, WASM, tools, and UI features are layered on top of the
engine static library.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tools/engine-smoke/vibe_othello_engine_smoke
```

Pull requests run release, sanitizer, and lint checks in GitHub Actions.
The release job runs the same configure, build, test, and smoke-tool checks.
Benchmark executables are built in CI but run locally. The lint job enforces
clang-format and runs clang-tidy as an advisory check.

Optional engine benchmarks can be built and run locally:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DVIBE_OTHELLO_BUILD_BENCHMARKS=ON
cmake --build build-bench --config Release
./build-bench/engine/benchmarks/vibe_othello_board_core_bench
./build-bench/engine/benchmarks/vibe_othello_endgame_bench --tsv --max-empties 12
./build-bench/engine/benchmarks/vibe_othello_search_bench
```

See engine/benchmarks/README.md for benchmark layout and result handling.

Sanitizer checks can be run locally with Clang or GCC:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DVIBE_OTHELLO_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

See docs/README.md for the documentation index.
