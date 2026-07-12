# Web App Architecture

## Purpose

Define the intended architecture for a browser-playable app distributed through
GitHub Pages.

The app uses:

* React + Vite for the browser UI
* Web Worker for engine isolation
* TypeScript wrapper for WASM loading and raw ABI conversion
* top-level `wasm/` adapter for Emscripten, flat C ABI, ABI versioning, and
  native-vs-WASM parity
* existing C++ engine public API for board rules, search, and evaluation

Implementation status and milestone tracking live in
`docs/progress/web-app.md`.

## Non-goals

This document does not implement:

* React app
* Vite setup
* WASM adapter source
* Emscripten build
* Web Worker protocol implementation
* GitHub Pages deployment workflow
* game review logic
* opening books
* training workflows
* ProbCut
* parallel search
* evaluator artifact publication

## Component graph

```text
React components
  -> React hooks / app services
  -> Worker client
  -> postMessage protocol
  -> Engine Web Worker
  -> TypeScript WasmCore wrapper
  -> WASM C ABI adapter
  -> engine public C++ API
  -> board_core / search / evaluation
```

Forbidden dependency directions:

* `engine/` must not include or depend on React, Vite, DOM APIs, Web Worker APIs,
  Emscripten headers outside the WASM adapter, JSON UI protocols, or GitHub
  Pages workflow details.
* `board_core` must not depend on WASM, apps, search, evaluation, tools, or UI.
* `wasm/` may depend on engine public headers, but `engine/` must not depend on
  `wasm/`.
* `apps/web` may depend on the TypeScript WASM wrapper or worker-facing API, but
  must not own C++ WASM adapter source.
* React components must not directly call Emscripten exports or manipulate WASM
  memory.
* Web Worker code must not duplicate Othello rules.
* TypeScript code must not reimplement legal move generation, flip calculation,
  pass rules, terminal detection, or search scoring.
* Search and evaluation internals must not leak into UI state.

## Proposed top-level layout

```text
repo/
  engine/
    include/
    src/
    tests/
  wasm/
    README.md
    CMakeLists.txt
    include/
    src/
    ts/
    tests/
      smoke/
  apps/
    web/
      README.md
      package metadata
      app entry and Vite config
      public/runtime assets
      src/
        app/
        components/
        game/
        engine/
        workers/
        review/
```

`wasm/` is a top-level adapter layer because it is not a user-facing
application and should not live under `apps/`.

`wasm/` is also not part of `engine/` because `engine/` must remain
browser-agnostic and Emscripten-agnostic.

`apps/web` owns UI, worker protocol, app state, rendering, and user-facing
formatting.

`wasm/` owns the Emscripten build, C ABI, ABI versioning, packed wire structs,
low-level WASM TypeScript wrapper, and WASM parity tests.

Generated `.mjs` and `.wasm` files may be copied into `apps/web` public/runtime
assets during the web build, but the source of truth for the adapter lives under
`wasm/`.

If more bindings are added later, a future architecture PR may rename `wasm/` to
`bindings/wasm` or `adapters/wasm`. Do not introduce that abstraction now.

## Ownership boundaries

### engine public C++ API

Owns:

* board rules
* move application
* pass and terminal semantics
* search
* evaluation
* result semantics

Does not own:

* browser concepts
* Emscripten build flags
* Web Worker protocol
* React state
* UI formatting
* GitHub Pages deployment

### WASM C ABI adapter under wasm/

Owns:

* flat exported functions
* ABI version function
* packed wire structs
* memory-safe input validation
* conversion between C ABI data and engine public C++ types
* bounded adapter-level smoke surfaces
* evaluator-owned single-thread search sessions with explicit reuse and reset

Rules:

* depends only on engine public headers
* must not include UI policy
* must not expose search internals or evaluator internals
* browser-facing search strength selection should use a small stable preset plus
  independent limits, not individual search-option flags
* persistent search knowledge must be opt-in and reset at a new-game boundary
* must not duplicate board rules

### TypeScript WasmCore wrapper under wasm/ts

Owns:

* Emscripten module loading
* raw export access
* memory allocation helpers
* pointer lifetime
* ABI version checks
* conversion from packed WASM wire structs into TypeScript domain types

Rules:

* this should be the only TypeScript layer that knows the raw WASM ABI
* `apps/web` should consume domain objects or worker APIs, not pointers or raw
  memory

### Engine Web Worker under apps/web/src/workers

Owns:

* command handling
* request IDs
* response ordering
* worker-local engine instance lifetime
* bounded search requests
* app-facing error conversion

Rules:

* exposes only postMessage request/response types
* does not duplicate Othello rules
* does not expose raw WASM memory

### Worker client / React hooks under apps/web/src

Own:

* UI-facing async methods
* app state transitions
* loading/error states
* cancellation UI semantics

Rules:

* must not expose Emscripten modules
* must not expose C ABI details
* must not expose raw WASM pointers

### React UI

Owns:

* rendering
* input gestures
* board orientation
* accessibility
* user-facing display formatting
* visual highlights

Rules:

* consumes domain objects
* does not know engine internals
* does not know WASM memory layout

## Boundary formats

Boundary formats have two categories.

1. Low-frequency/debug boundary:

* canonical board text from board_core serialization
* useful for tests, URLs, logs, and debugging

2. High-frequency boundary:

* versioned packed binary structs or typed arrays
* useful for Worker/WASM communication and repeated analysis

Rules:

* any binary ABI shape must include or be guarded by an ABI version
* raw binary formats are implementation boundaries, not UI model types
* UI-facing models should be TypeScript domain objects

Prefer simple v0 commands:

* `init`
* `getLegalMoves`
* `applyMove`
* `search`
* `solveEndgame`
* `cancel` or `stop`

Long synchronous WASM calls inside a single Web Worker cannot be interrupted by
a normal queued `postMessage` until control returns to the Worker event loop. v0
should rely on C++ `SearchLimits` `max_depth` and `max_time` for bounded work.
Hard cancellation should be designed separately, for example through cooperative
polling, shared state, or chunked search.

An exact-endgame threshold must not be accepted with only `max_depth`: exact
root search intentionally bypasses iterative depth search. The WASM adapter must
require a node or time limit whenever callers enable exact root search.

## Search and evaluation use from web

v0 should use existing public search APIs only.

Engine search and evaluation scores are side-to-move-relative. The WASM adapter
and Worker protocol should preserve that engine meaning and carry `score_kind`
or equivalent metadata when exposing results.

Conversion from side-to-move-relative engine scores into UI-facing display
scores is an app adapter responsibility. Worker clients, app services, or React
hooks may convert scores into root-relative, black/white-relative, signed
advantage, exact disc-difference, WLD label, or calibrated display values based
on app state and presentation needs.

React components should consume display-ready score models and labels. They
must not infer score sign from search internals, evaluator internals, raw WASM
memory, or board perspective details.

UI display conversion must not change recursive search scores, evaluator hot
path scores, or engine result semantics.

The web adapter may expose:

* legal moves
* applied move result
* current score
* best move
* candidate root moves
* principal variation
* completed depth
* score kind
* nodes
* elapsed time
* exact flag
* stopped flag

Do not expose:

* transposition table internals
* search stack internals
* move ordering internals
* evaluator feature internals
* training artifacts directly to React
* local-only training or measurement paths

## Review and analysis adapters

Game review, blunder thresholds, swing detection, mistake labels, and human
explanation are separate adapter policies on top of `SearchResult` and board
history.

They must not be built into:

* board_core
* recursive search
* runtime evaluator hot path

A future `ReviewReport` domain shape should include:

* frames
* summary
* highlights
* optional candidate lines
* optional exact/endgame annotations

Keep threshold policy out of engine core.

## Build and deployment

Intended build approach:

* `apps/web` uses Vite.
* GitHub Pages deployment publishes `apps/web/dist`.
* For repository Pages, Vite base should be `/vibe-othello-2/`.
* `wasm/` is built before the Vite build.
* generated WASM runtime assets are copied or emitted into an app-owned
  static/runtime asset location.
* the committed default evaluation artifact may be copied from `data/eval/`
  into `apps/web/public/eval/` as ignored static runtime assets.
* `data/eval/` remains the source of truth for evaluation artifacts; `apps/web`
  must not own copied artifact payloads.
* future browser/WASM artifact loading should fetch the default pointer from
  `/eval/default-artifact.json` under the Vite base URL, then resolve the
  manifest and `weights.bin` from that static path.
* the native C++ build must not require Node.
* the native default CMake build must not require Emscripten.
* Emscripten-specific build steps should be opt-in.
* top-level CMake should not force the WASM build by default.

The first implementation should use single-threaded WASM inside a Web Worker.
Do not require browser pthreads, `SharedArrayBuffer`, COOP, or COEP in v0.

## Testing strategy

Future test layers:

* native C++ tests remain under `engine/tests`
* WASM parity smoke tests compare native and WASM outputs for fixed positions
* TypeScript unit tests cover `WasmCore` conversions
* TypeScript unit tests cover worker protocol request/response behavior
* browser smoke test verifies app boot, worker init, legal move query, and
  bounded search
* deployment workflow builds app and uploads `apps/web/dist` to GitHub Pages

## Milestones

1. architecture docs only
2. minimal top-level `wasm/` adapter design skeleton, still no app dependency
3. minimal `apps/web` Vite skeleton with static board UI and no engine
4. single-thread WASM adapter for board_core legal moves and `applyMove`
5. Worker protocol and React hook integration
6. bounded search/eval display
7. GitHub Pages deploy workflow
8. review/report adapters
9. optional artifact-backed evaluator loading
10. optional advanced cancellation
11. optional threaded WASM only if hosting requirements are solved

## Change checklist

When changing web app architecture, update `docs/architecture/web-app.md`.

When changing current implementation status, update `docs/progress/web-app.md`.

When changing repository layout, update `docs/repository-layout.md`.

When changing app entry points, update `apps/README.md`.

When changing WASM ABI, update `wasm/README.md` or the relevant architecture
section once `wasm/` exists.

Boundary changes must include tests or explicitly document why tests are not
part of the PR.
