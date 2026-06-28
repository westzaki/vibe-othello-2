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
| `.github/` | Pull request templates and CI workflows |
| `docs/` | Architecture, progress, layout, style, and review documents |
| `cmake/` | Shared CMake helper modules, including shared test dependency setup |
| `data/` | Dataset manifest policy, evaluation artifact policy, and local-only data placement |
| `engine/` | Native C++ Othello engine static library |
| `wasm/` | Native-buildable C ABI adapters and opt-in Emscripten module smoke for browser/WASM-facing engine boundaries |
| `tools/` | Developer and validation command-line tools |
| `apps/` | User-facing applications |
| `apps/web/` | Browser app skeleton using React, Vite, TypeScript, and a Web Worker boundary to the WASM adapter |
| `.clang-format` | C++ formatting rules |
| `.clang-tidy` | C++ static analysis rules |

## Documentation Layout

Documentation is indexed from `docs/README.md`.

Architecture documents describe intended boundaries, semantics, and invariants.

Progress documents describe current implementation state, milestones, benchmark
notes, temporary gaps, and deferred work.

Progress documents must not redefine architecture.

```text
docs/
├─ README.md
├─ architecture/
├─ progress/
├─ repository-layout.md
└─ cpp-coding-style.md
```

## Engine Layout

The engine owns Othello rules and state transitions. It builds as a static
library. Human-facing executable entry points live in `tools/` or `apps/`;
engine-specific benchmark executables and benchmark management scripts live
under `engine/benchmarks/`.

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
│  └─ samples/
└─ eval/
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
├─ data-policy/
├─ data-import/
├─ pattern/
│  ├─ common/
│  ├─ dataset/
│  ├─ features/
│  ├─ train/
│  └─ export/
├─ arena/
│  ├─ openings/
│  └─ README.md
├─ engine-cli/
│  └─ main.cc
└─ engine-smoke/
   └─ main.cc
```

## WASM Adapter Layout

`wasm/` owns native-buildable C ABI adapters and opt-in Emscripten module smoke
for browser/WASM-facing engine boundaries. It links against engine public APIs
and must not duplicate engine rules.

The current adapter exposes board-core operations only. It can build generated
`.wasm` and `.mjs` outputs as local build artifacts, but does not own committed
runtime output, TypeScript wrappers, Web Worker protocols, React, Vite, GitHub
Pages workflows, search bindings, or evaluation bindings.

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
   │  └─ wasm/
   └─ src/
      ├─ engine/
      └─ workers/
```

`apps/web` owns the browser UI, Vite configuration, Worker protocol, Worker
client, and app-specific runtime asset convention. Generated Emscripten `.mjs`
and `.wasm` files may be copied into `apps/web/public/wasm/` for local browser
runs, but they remain ignored build artifacts.
