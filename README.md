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
```

Pull requests run release, sanitizer, and lint checks in GitHub Actions.
The release job runs the same configure, build, and test checks.
Benchmark executables are built in CI but run locally. The lint job enforces
clang-format and runs clang-tidy as an advisory check.

## Learned Evaluation Data

The experimental default evaluator uses all 25,514,097 board-score positions
in `Egaroucid_Train_Data.zip`. The 1,514,097 positions with 4-15 occupied
squares come from Egaroucid 7.4.0 lv17 enumeration, evaluation, and negamax;
the 24,000,000 positions with 16-63 occupied squares use terminal outcomes
from Egaroucid 7.5.1 lv17 self-play. Training starts from the previous
full-WHTOR policy artifact, uses five full-corpus passes for phases 0 through
9, and one full-corpus late-phase pass.

Promotion used a separately generated 1,000-pair random opening suite. Its
opening boards had zero overlap with all 25,514,097 Egaroucid training boards.
Against the prior default, the selected artifact scored 68.05% at depth 3,
67.97% at depth 5, and 68.85% at 10 ms per move with exact solving from
8 empties; all paired 95% intervals excluded 50%.

Short-opening depth-3 gates also exercised the updated early phases. The
candidate scored 69.92% from every unique 4-ply board, 71.48% over 256
8-ply openings, and 66.70% over 256 11-ply openings; every paired interval
excluded 50% and all games were clean.

See
`data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/README.md`
for the learning route, validation results, and source notice.

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

Reviewed search calibration profiles use the same narrow exception under
`data/search/`; raw samples and complete measurement reports remain local-only.

```sh
export VIBE_OTHELLO_LOCAL="${VIBE_OTHELLO_LOCAL:-$HOME/vibe-othello-local}"
export VIBE_OTHELLO_CORPORA="${VIBE_OTHELLO_CORPORA:-$VIBE_OTHELLO_LOCAL/corpora}"
export VIBE_OTHELLO_TRAINING="${VIBE_OTHELLO_TRAINING:-$VIBE_OTHELLO_LOCAL/training}"
export VIBE_OTHELLO_MEASUREMENTS="${VIBE_OTHELLO_MEASUREMENTS:-$VIBE_OTHELLO_LOCAL/measurements}"

mkdir -p "$VIBE_OTHELLO_CORPORA"
mkdir -p "$VIBE_OTHELLO_TRAINING"
mkdir -p "$VIBE_OTHELLO_MEASUREMENTS"
```

Keep downloaded source archives under `corpora/`, generated normalized data,
datasets, trainer weights, and trainer reports under `training/`, and arena or
benchmark evidence under `measurements/`. Training tools read source archives
in place; they must not extract generated files into `corpora/`.

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
