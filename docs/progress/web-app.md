# Web App Progress

## Purpose

This document tracks current implementation status for the browser-playable web
app and WASM adapter path.

The intended design lives in `docs/architecture/web-app.md`.

This file may change frequently as implementation progresses.

## Design sources

Relevant design documents:

* `docs/architecture/web-app.md`
* `docs/repository-layout.md`
* `docs/architecture/board-core.md`
* `docs/architecture/search.md`
* `docs/architecture/evaluation.md`
* `apps/README.md`

## Current implementation

The current repository has architecture documentation for the intended web app
and WASM adapter boundaries.

The current repository has a native-buildable top-level `wasm/` adapter for
board-core C ABI calls. It exposes ABI versioning, initial position, position
query, checked move application, and checked pass application. Native adapter
tests cover this surface.

The current repository also has an opt-in Emscripten module target for the same
C ABI adapter. The generated `.mjs` and `.wasm` files are build artifacts and
are not committed. When Node is available in an Emscripten build, Node smokes
load the module, call the C ABI functions, and exercise a minimal plain ESM
`WasmCore` wrapper for board-core operations plus artifact-backed evaluation
and bounded best-move search. Repo-level CMake integration defines dedicated
copy targets that place the generated runtime assets into
`apps/web/public/wasm/` and the committed default evaluation artifact into
`apps/web/public/eval/` for local browser runs and Web CI integration.

The current repository has a dedicated GitHub Pages deployment workflow. It runs
on pushes to `main` and `workflow_dispatch`, builds the Emscripten runtime
module, copies the generated runtime assets and the committed default evaluation
artifact into the Web app, builds `apps/web`, uploads `apps/web/dist` as a
Pages artifact, and deploys it through GitHub Pages. Repository Pages settings
must use GitHub Actions as the Pages source. The generated `.mjs` and `.wasm`
runtime files, copied eval payloads, and `apps/web/dist/` remain ignored build
artifacts and are not committed.

The current repository has a minimal browser runtime skeleton under `apps/web`:

* React + Vite + TypeScript app metadata and build configuration
* a simple Othello board UI consuming serializable board snapshots
* a Worker protocol with `init`, `reset`, `applyMove`, `applyPass`, and
  `cpuMove` commands
* an Engine Web Worker that imports the plain ESM `WasmCore` wrapper
* a Worker client used by React so components do not import or call WASM
* runtime loading of generated Emscripten `.mjs/.wasm` assets from
  `apps/web/public/wasm/`
* an ignored static asset path for copied default evaluation artifacts under
  `apps/web/public/eval/`
* Worker fetching of the copied default evaluation pointer from
  `/eval/default-artifact.json`, manifest and weights resolution from the copied
  static asset tree, and `WasmCore.loadEvaluationArtifact()` loading
* a manual React `CPU move` button that asks the Worker to run one normal-preset
  bounded best-move search for the current side to move and apply the result
* a simple CPU opponent mode where the human is fixed to Black, the CPU is fixed
  to White, and the app automatically asks the Worker to run one CPU move after
  human moves
* a small UI summary for the most recent CPU move search result

The current repository still does not have the full browser runtime roadmap:

* no TypeScript `WasmCore` wrapper exists, though a plain JavaScript wrapper
  exists as an implementation stepping stone
* no web-specific review adapter exists

Native engine functionality is implemented separately under `engine/` and is the
future source of truth for board rules, search, and evaluation.

The engine has an in-memory pattern evaluation artifact loader that accepts
manifest text and weights bytes. The WASM C ABI and plain JavaScript `WasmCore`
wrapper can now load those bytes into an opaque WASM-side phase-aware evaluator
handle, evaluate positions, and run bounded best-move search through that
loaded evaluator. The browser Worker fetches the copied default artifact assets,
loads the artifact through `WasmCore.loadEvaluationArtifact()`, and uses its
`normal` preset for a CPU move with depth 8, no node cap, a 500 ms cap, and an
8-empty exact-score threshold. React can either expose that as a manual CPU
move when CPU opponent mode is off, or reuse it as an automatic White response
in the minimal CPU opponent mode. Side selection, difficulty selection,
advanced cancellation, threaded WASM, review UI, evaluation explanation UI,
and strength or Elo claims are still not implemented.

## Current gaps

The current implementation does not yet have:

* detailed WASM parity smoke tests
* TypeScript WASM wrapper
* review/report adapters
* React evaluation display UI
* advanced cancellation
* threaded WASM support

## Implementation status

Status values:

* `done` means implemented in the repository
* `not started` means no production implementation exists yet
* `deferred` means intentionally left for a later phase

| Area | Status | Notes |
| --- | --- | --- |
| Architecture docs | done | `docs/architecture/web-app.md` |
| Progress docs | done | This document |
| Top-level `wasm/` adapter directory | done | Adapter layer parallel to `engine/` and `apps/` |
| Board-core C ABI adapter | done | Native-buildable adapter consuming engine public C++ API |
| ABI versioning | done | `VIBE_OTHELLO_WASM_ABI_VERSION` and version query |
| Native adapter tests | done | Cover board-core C ABI status and parity with board_core calls |
| Opt-in Emscripten module target | done | `VIBE_OTHELLO_BUILD_WASM_MODULE=ON`; emits build-tree `.mjs` and `.wasm` artifacts |
| Web WASM runtime asset copy target | done | Repo-level CMake target `vibe_othello_copy_web_wasm_assets` copies generated `.mjs/.wasm` into `apps/web/public/wasm/` |
| Web eval artifact asset copy target | done | Repo-level CMake target `vibe_othello_copy_web_eval_artifact_assets` copies the current `data/eval/default-artifact.json` artifact into ignored `apps/web/public/eval/` runtime assets |
| Node module smoke | done | Minimal module loading and C ABI execution smoke when Node is available |
| Plain JavaScript `WasmCore` wrapper | done | Minimal ESM wrapper for board-core calls under `wasm/js` |
| JS wrapper Node smoke | done | Exercises `WasmCore` against the generated Emscripten module, including artifact loading, evaluation, and bounded search when Node is available |
| WASM parity smoke tests | not started | Detailed native-vs-WASM output comparison is still future work |
| TypeScript `WasmCore` wrapper | not started | Plain JavaScript exists; typed app-facing wrapper remains future work |
| WASM artifact loading | done | C ABI can load manifest text plus weights bytes through the engine in-memory loader into an opaque phase-aware evaluator handle |
| JavaScript artifact loading | done | Plain `WasmCore` can load caller-provided manifest text and weights bytes; Worker fetching from `/eval/default-artifact.json` is wired for CPU moves |
| `apps/web` Vite project | done | Minimal React + Vite + TypeScript project under `apps/web` |
| React board UI | done | Minimal 8x8 board consuming Worker snapshots |
| Engine Web Worker | done | Worker imports `WasmCore`, loads generated runtime assets, owns current position, fetches copied default eval assets, and runs manual bounded CPU move search |
| Worker protocol | done | Serializable `init`, `reset`, `applyMove`, `applyPass`, and manual `cpuMove` request/response types |
| Worker client and React hooks | done | Worker client used by React state hooks; React does not import WASM |
| Legal move, applyMove, and pass browser flow | done | Implemented when generated WASM runtime assets are present under `apps/web/public/wasm/`; pass is user-triggered through Worker -> `WasmCore` -> board_core |
| Web CI with generated WASM and eval assets | done | Web job builds the Emscripten module, copies WASM runtime assets and eval runtime assets, verifies eval asset readiness, installs app dependencies, typechecks, and runs Vite build |
| Engine in-memory evaluation artifact loader | done | Native engine API can load pattern artifacts from manifest text and weights bytes; browser/WASM consumption is wired for CPU moves |
| Search best-move bindings | done | Legacy WASM C ABI behavior is preserved; a versioned C ABI and plain `WasmCore` expose `easy`/`normal`/`hard` preset search with independent limits and exact threshold through a loaded evaluation artifact; Worker protocol uses it through `cpuMove` |
| Manual CPU move | done | React exposes a manual `CPU move` button when CPU opponent mode is off; Worker runs normal-preset depth 8 / 500 ms search with an 8-empty exact threshold through the copied default eval artifact and applies one move or pass |
| CPU opponent | done | Minimal human-vs-CPU mode fixes the human to Black and CPU to White, automatically reusing the existing Worker `cpuMove` command after human moves |
| Bounded search/evaluation display | done | CPU moves show a small search summary; continuous position evaluation display is not implemented |
| GitHub Pages workflow | done | Dedicated Pages workflow builds generated WASM runtime assets, copies eval runtime assets, builds `apps/web`, uploads `apps/web/dist`, and deploys on pushes to `main` or manual dispatch |
| Review/report adapters | deferred | Policy layer on top of search results and board history |
| Evaluator artifact loading for web | done | Worker fetches copied `/eval/default-artifact.json`, resolves manifest and `weights.bin`, and loads the artifact through `WasmCore.loadEvaluationArtifact()` |
| Advanced cancellation | deferred | Requires separate design beyond queued `postMessage` |
| Threaded WASM | deferred | Requires hosting support for browser threading headers |

## Completion bar

The web app path is ready for first playable browser use when:

* `wasm/` exists as a top-level adapter layer
* the adapter depends on engine public headers only
* the native default build does not require Node or Emscripten
* Emscripten build steps are opt-in
* WASM ABI versioning exists
* WASM parity smoke tests cover fixed board positions beyond the minimal module
  loading smoke
* `apps/web` builds with Vite
* generated WASM runtime assets are app-owned build artifacts, not adapter
  source of truth
* the app runs engine calls inside a Web Worker
* React consumes domain objects and worker APIs only
* legal move, apply move, and pass flows are backed by board_core through the
  adapter
* bounded search uses public search APIs and `SearchLimits`
* browser smoke coverage verifies boot, worker init, legal moves, and bounded
  search
* GitHub Pages deployment publishes `apps/web/dist`

## Progress update rules

Update this document when:

* a web app or WASM implementation milestone changes status
* a known browser, worker, ABI, or deployment gap is discovered
* a deferred item moves into scope
* test coverage is added or intentionally omitted for a boundary change
* the implementation diverges from `docs/architecture/web-app.md`

Update `docs/architecture/web-app.md` only when the intended design, boundary,
dependency direction, ABI ownership, or deployment architecture changes.
