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
are not committed. When Node is available in an Emscripten build, a minimal Node
smoke loads the module and calls the existing C ABI functions.

The current repository does not yet have production browser runtime
implementation:

* `apps/` currently has only `README.md`
* no `apps/web` React app exists
* no Vite setup exists
* no TypeScript `WasmCore` wrapper exists
* no Engine Web Worker exists
* no Worker protocol exists
* no GitHub Pages deployment workflow exists
* no web-specific review adapter exists

Native engine functionality is implemented separately under `engine/` and is the
future source of truth for board rules, search, and evaluation.

## Current gaps

The current implementation does not yet have:

* detailed WASM parity smoke tests
* TypeScript WASM wrapper
* `apps/web` Vite project
* React board UI
* Engine Web Worker
* worker client or React hooks
* bounded browser search flow
* GitHub Pages deployment
* review/report adapters
* evaluator artifact loading for web
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
| Node module smoke | done | Minimal module loading and C ABI execution smoke when Node is available |
| WASM parity smoke tests | not started | Detailed native-vs-WASM output comparison is still future work |
| TypeScript `WasmCore` wrapper | not started | Should be the only TypeScript layer that knows raw WASM ABI |
| `apps/web` Vite project | not started | Planned user-facing application |
| React board UI | not started | Should consume domain objects |
| Engine Web Worker | not started | Should isolate engine calls from UI thread |
| Worker protocol | not started | Should expose request/response messages only |
| Worker client and React hooks | not started | Should hide C ABI and Emscripten details |
| Legal move and applyMove browser flow | not started | First engine-backed app milestone |
| Bounded search/evaluation display | not started | Uses existing public search APIs only |
| GitHub Pages workflow | not started | Should publish `apps/web/dist` |
| Review/report adapters | deferred | Policy layer on top of search results and board history |
| Evaluator artifact loading for web | deferred | Publication policy is separate from v0 app |
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
* legal move and apply move flows are backed by board_core through the adapter
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
