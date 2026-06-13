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

Pull requests run the same configure, build, test, and smoke-tool checks in
GitHub Actions.

Optional board-core benchmarks can be run locally:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVIBE_OTHELLO_BUILD_BENCHMARKS=ON
cmake --build build --config Release
./build/engine/benchmarks/vibe_othello_board_core_bench
```

Sanitizer checks can be run locally with Clang or GCC:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DVIBE_OTHELLO_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

See docs/README.md for the documentation index.
