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

## Learned Evaluation Data

The experimental default evaluator was trained from the actual played moves in
all 137,548 games in a local 1977-2025 snapshot of the
[FFO WHTOR database](https://www.ffothello.org/informatique/la-base-wthor/).
The committed payload contains only the final derived runtime weights and
review metadata. Raw WHTOR files, normalized data, policy targets, teacher
labels, and local reports are not included.

Promotion used a separately generated 1,000-pair random opening suite. Its
opening boards and replayed transcript prefixes had zero overlap with all
137,548 WHTOR training games. Against the prior default, the selected artifact
scored 73.35% at depth 3, 69.14% at depth 5, and 66.99% at 10 ms per move with
exact solving from 8 empties; all paired 95% intervals excluded 50%.

See
`data/eval/artifacts/pattern-v2-wthor-full-policy-v1/README.md`
for the learning route, validation results, saturation experiments, and source
notice.

The browser app under `apps/web` is built for GitHub Pages by a dedicated Pages
workflow. It runs on pushes to `main` and manual dispatch, builds the generated
WASM runtime assets, copies the committed default evaluation artifact from
`data/eval/` into ignored Web runtime assets, builds the Vite app, uploads
`apps/web/dist`, and deploys through GitHub Pages. Repository Pages settings
must use GitHub Actions as the Pages source; after merge and a successful
workflow, the expected URL is `https://westzaki.github.io/vibe-othello-2/`.

## Local-only Measurement Directories

Generated corpora, measurements, TSVs, candidate weights, candidate artifacts,
logs, and suite reports are local-only. Keep them outside the git repository
and outside disposable worktrees so they survive worktree deletion and do not
appear in git status. Only a reviewed final runtime artifact may use the narrow
commit exception documented in `data/corpora/README.md` and
`data/eval/README.md`.

```sh
export VIBE_OTHELLO_LOCAL="${VIBE_OTHELLO_LOCAL:-$HOME/vibe-othello-local}"
export VIBE_OTHELLO_CORPORA="${VIBE_OTHELLO_CORPORA:-$VIBE_OTHELLO_LOCAL/corpora}"
export VIBE_OTHELLO_MEASUREMENTS="${VIBE_OTHELLO_MEASUREMENTS:-$VIBE_OTHELLO_LOCAL/measurements}"

mkdir -p "$VIBE_OTHELLO_CORPORA"
mkdir -p "$VIBE_OTHELLO_MEASUREMENTS"
```

Do not commit personal paths, `.env` files with real values, generated
measurement outputs, or external corpus payloads.

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
