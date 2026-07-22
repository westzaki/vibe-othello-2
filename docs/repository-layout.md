# Repository Layout

The repository is organized by responsibility, not by programming language.

## Initial Layout

```text
repo/
├─ AGENTS.md
├─ README.md
├─ .github/
├─ docs/
├─ cmake/
├─ data/
├─ engine/
├─ wasm/
├─ tools/
├─ apps/
├─ .clang-format
└─ .clang-tidy
```

## Responsibilities

| Path | Purpose |
| --- | --- |
| `AGENTS.md` | Agent working rules |
| `README.md` | Project entry point |
| `.github/` | Pull request templates, CI workflows, and GitHub Pages deployment workflow |
| `docs/` | Architecture, layout, style, and historical experiment documents |
| `cmake/` | Shared CMake helper modules, including shared test dependency setup and repo-level build integration |
| `data/` | Dataset manifest policy, evaluation artifact policy, and local-only data placement |
| `engine/` | Native C++ Othello engine static library |
| `wasm/` | Native-buildable C ABI, JavaScript wrapper, and opt-in Emscripten module for browser-facing engine boundaries |
| `tools/` | Developer and validation command-line tools |
| `apps/` | User-facing applications |
| `apps/web/` | Browser app using React, Vite, TypeScript, and a Web Worker boundary to the WASM adapter |
| `.clang-format` | C++ formatting rules |
| `.clang-tidy` | C++ static analysis rules |

## Documentation Layout

Documentation is indexed from `docs/README.md`.

Architecture documents describe the implemented system: current ownership,
semantics, invariants, important flows, and known limitations. Historical
measurements and superseded implementation routes live under `experiments/`.

```text
docs/
├─ README.md
├─ architecture/
├─ experiments/
├─ repository-layout.md
└─ cpp-coding-style.md
```

## CMake Helper Layout

`cmake/` owns shared CMake modules and repo-level build integration that should
not belong to a single source adapter or application.

```text
cmake/
├─ testing/
└─ web/
```

`cmake/web/` owns Web build integration helpers, including targets that copy
generated WASM runtime assets and the committed default evaluation artifact into
Web app static asset directories.

## Engine Layout

The engine owns the board core, runtime evaluation, and search. It builds as a
static library. Human-facing executable entry points live in `tools/` or
`apps/`; engine-specific benchmark executables and benchmark management scripts
live under `engine/benchmarks/`.

```text
engine/
├─ README.md
├─ benchmarks/
│  ├─ baselines/
│  ├─ results/
│  └─ scripts/
├─ fixtures/
├─ include/
├─ src/
└─ tests/
   └─ support/
```

Engine tests use Catch2 and run through CTest.
`engine/tests/` owns test sources and test-only helpers. Shared test-only
helpers live in `engine/tests/support/` and are linked only by test targets.

`engine/fixtures/` owns checked-in data shared by tests, benchmarks, and golden
generation scripts. Keep reusable validation corpora and expected-output files
there instead of under `engine/tests/` when non-test executables also consume
them.

`engine/benchmarks/` owns the benchmark suite: C++ benchmark executables,
checked-in aggregate baselines, ignored local result scratch files, and
golden/baseline helper scripts. Benchmark helper scripts are part of the engine
benchmark suite, not generic top-level tools.

## Data Layout

`data/` owns repository policy and manifests for pattern-learning inputs and
evaluation artifacts.

```text
data/
├─ corpora/
│  ├─ README.md
│  ├─ dataset-manifest.schema.json
│  ├─ manifests/
│  └─ samples/
├─ eval/
│  ├─ default-artifact.json
│  ├─ artifacts/
│  └─ README.md
└─ labels/
   └─ README.md
```

Raw third-party corpora, derived datasets, learned binary weights, and large
generated artifacts stay out of normal git history. Checked-in manifests and
tiny synthetic samples are allowed when they contain no restricted payload data
and no personal local paths.

## Tools Layout

Tools link against engine libraries and provide small command-line entry points
for development, smoke checks, and validation. Keep this area focused on
human-run development CLIs rather than benchmark suite management scripts.

```text
tools/
├─ arena/
│  ├─ openings/
│  └─ README.md
├─ data-import/
├─ data-policy/
├─ engine-cli/
│  └─ main.cc
├─ pattern/
│  ├─ artifacts/
│  ├─ common/
│  ├─ dataset/
│  ├─ export/
│  ├─ labels/
│  └─ train/
├─ search-calibration/
└─ self-play/
```

## WASM Adapter Layout

`wasm/` owns the native-buildable C ABI, opt-in Emscripten module, and the
Node/browser-neutral JavaScript `WasmCore` wrapper. It links against engine
public APIs and must not duplicate engine rules. The adapter exposes board
operations, in-memory evaluation-artifact loading, evaluation, bounded preset
search, and explicit search-session reuse/reset. It does not own Web Worker
protocols, React, Vite, or GitHub Pages policy.

```text
wasm/
├─ README.md
├─ CMakeLists.txt
├─ include/
├─ js/
├─ smoke/
├─ src/
└─ tests/
```

## Apps Layout

Applications live under apps/.

Application subdirectories own user-facing entry points.

```text
apps/
├─ README.md
└─ web/
   ├─ README.md
   ├─ package.json
   ├─ public/
   │  ├─ eval/
   │  └─ wasm/
   └─ src/
      ├─ engine/
      └─ workers/
```

`apps/web` owns the browser UI, Vite configuration, Worker protocol, Worker
client, and app-specific runtime asset convention. Generated Emscripten `.mjs`
and `.wasm` files may be copied into `apps/web/public/wasm/` for local browser
runs by repo-level CMake integration, but they remain ignored build artifacts.
The committed default evaluation artifact may also be copied from `data/eval/`
into `apps/web/public/eval/` for local browser runs, Web CI, and GitHub Pages
builds. `data/eval/` remains the source of truth; copied eval payloads are
ignored runtime assets and must not be committed under `apps/web`.

The Worker imports `wasm/js/wasmCore.mjs` through a Vite alias, owns the live
position and loaded evaluator, serializes commands, and returns domain
snapshots. React never imports the Emscripten module or accesses WASM memory.
