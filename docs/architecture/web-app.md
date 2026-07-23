# Web App and WASM Architecture

## Purpose

The browser app is a single-threaded React client backed by the native engine
compiled to WebAssembly. It is built with Vite, runs engine work in a Web
Worker, and is deployed to GitHub Pages.

The architecture keeps one rule and search implementation in C++. TypeScript
owns asynchronous orchestration and presentation; it must not reimplement
legal moves, flips, pass/terminal rules, evaluation, or move choice.

## Implemented Stack

```text
React App
  -> EngineWorkerClient
  -> typed postMessage request/response protocol
  -> engine.worker.ts
  -> wasm/js/wasmCore.mjs
  -> versioned flat C ABI in wasm/
  -> engine public C++ API
  -> board_core / evaluation / search
```

The current app supports:

* initial position and reset
* legal-move display, checked move application, and checked pass
* a manual CPU move for the current side
* a minimal human-Black/CPU-White mode with automatic White replies
* a compact summary of the last CPU search
* runtime loading of the committed default evaluation artifact
* GitHub Pages builds containing generated WASM and copied evaluation assets

The app uses a plain ESM `WasmCore` implementation shared by Node smokes and
the browser. `apps/web/src/types/wasmCore.d.ts` supplies the app-facing type
declarations. There is no separate TypeScript wrapper implementation.

## Dependency and Ownership Boundaries

### Engine

`engine/` owns board rules, relative position semantics, evaluation, search,
exact endgame behavior, result semantics, and reusable search sessions. It has
no React, Vite, Worker, Emscripten, or deployment dependency.

### WASM adapter

`wasm/` owns:

* ABI version and status codes
* C-compatible position, query, apply, and search-result structures
* layout-introspection exports used instead of hard-coded JavaScript offsets
* validation and conversion between wire values and engine public types
* opaque evaluation-artifact handles
* in-memory manifest-plus-weights loading
* phase-aware evaluation and bounded best-move search
* easy/normal/hard algorithm presets
* evaluator-owned WASM-profile `SearchSession` state
* opt-in Emscripten module generation and Node smokes
* the Node/browser-neutral `WasmCore` JavaScript wrapper

The adapter links against public engine headers and never duplicates Othello
rules. Generated `.mjs` and `.wasm` files are build outputs, not source files.
The adapter does not own Worker commands, UI policy, static asset URLs, or
deployment behavior.

Each loaded evaluator owns a 1 MiB TT session. Session retention is disabled by
default; explicit ABI/JavaScript methods enable retention and reset it at a
game boundary. The current Worker leaves retention disabled.

### Engine Worker

`apps/web/src/workers/engine.worker.ts` owns the live `WasmCore`, current
position, loaded default evaluator, CPU search policy, and serialized request
queue. It dynamically imports the generated Emscripten module using Vite's
`BASE_URL` and fetches evaluation assets from the same base.

The Worker is the only app layer that calls `WasmCore`. It converts C/WASM
results into serializable domain snapshots and validates a searched move
against the pre-move legal mask before applying it.

### Worker client and protocol

`protocol.ts` defines the app boundary. Current commands are:

* `init`
* `reset`
* `applyMove`
* `applyPass`
* `cpuMove`

Each request has an id; each response echoes the id and command. Success
responses carry a `BoardSnapshot` and optionally a CPU move/search summary.
Failure responses carry a user-readable message. `EngineWorkerClient` owns the
Worker and the pending request map and rejects outstanding requests when the
Worker fails or is disposed.

### React UI

`App.tsx` owns rendering, busy/error state, board orientation, user actions,
and the human/CPU turn policy. React consumes `BoardSnapshot` and summary
objects only. It does not import the Emscripten module, inspect pointers,
interpret C structs, or calculate rule state.

## Boundary Data

The C ABI position is the engine's relative representation:

```text
player bitboard
opponent bitboard
side_to_move
```

The Worker converts that representation into a 64-cell absolute-color array,
side to move, legal square indices, `hasLegalMove`, and `isTerminal`. React
reorders square indices only for visual rank-8-to-rank-1 display; engine square
identity remains `a1 = 0`.

Search results exposed to the app currently include best move/pass, score,
completed depth, nodes, elapsed milliseconds, stopped, and exact. The score is
still the engine's side-to-move-relative search score. The current UI shows the
raw value and does not claim calibrated win probability or provide score-kind
explanations. The result contract also includes `probcutEnabled`, which reports
effective production-profile selection rather than whether a cut occurred.

## Evaluation Artifact Flow

`data/eval/` is the committed source of truth. Repo-level CMake integration
copies only the current default pointer, selected manifest, and weights into
the ignored Web runtime tree:

```text
apps/web/public/eval/default-artifact.json
apps/web/public/eval/artifacts/<artifact-id>/manifest.json
apps/web/public/eval/artifacts/<artifact-id>/weights.bin
```

The Worker:

1. fetches `${BASE_URL}eval/default-artifact.json`
2. resolves `artifact_manifest` relative to that URL
3. resolves `weights_file` relative to the manifest URL
4. fetches manifest text and weight bytes
5. passes both to `WasmCore.loadEvaluationArtifact()`

The C++ in-memory loader performs the same runtime compatibility and checksum
checks as the filesystem loader. Missing or invalid assets fail initialization
or CPU search visibly; the Worker does not silently substitute static
evaluation.

## Browser Search Policy

The Worker currently requests the `normal` preset with:

```text
max depth: 8
max nodes: 0
max time: 500 ms
exact-score handoff: 8 empties
```

`normal` and `hard` enable PVS, aspiration, IID, midgame/endgame TT, TT/history/
killer ordering, depth-gated internal mobility ordering, and parity ordering.
They also select the fail-closed production Multi-ProbCut profile only for the
exact reviewed evaluator, artifact, weights checksum, score scale,
trained-phase mask, fallback-additive phase boundary, move-search mode, and
8-empty handoff identity. They currently differ through caller-supplied limits
rather than algorithm flags. `easy`, the legacy search API, identity mismatch,
and other handoff thresholds keep Multi-ProbCut disabled. Search results expose
the effective configuration selection through `probcutEnabled`.

Limits are independent from presets. The ABI requires a node or time limit when
exact root search is enabled because exact root solving ignores `max_depth`.
Time stopping is cooperative and may overshoot slightly.

A long synchronous WASM search cannot consume a normal queued `postMessage`
until control returns to the Worker event loop. The current design therefore
relies on bounded C++ limits and does not promise interactive cancellation.

## Build and Deployment

The normal native build compiles and tests the C ABI adapter without requiring
Node or Emscripten. Emscripten module generation is opt-in with
`VIBE_OTHELLO_BUILD_WASM_MODULE=ON`.

Repo-level CMake targets copy generated runtime files into the app:

* `vibe_othello_copy_web_wasm_assets`
* `vibe_othello_copy_web_eval_artifact_assets`

Vite aliases `@vibe-othello/wasm-core` to `wasm/js/wasmCore.mjs`, uses the
repository Pages base `/vibe-othello-2/`, and emits `apps/web/dist`.

`.github/workflows/pages.yml` runs on pushes to `main` and manual dispatch. It
builds and tests the Emscripten module, copies WASM and evaluation assets,
typechecks and builds the app, checks the Pages artifact contents, and deploys
`apps/web/dist`. Pull requests do not deploy.

## Verification

The implemented boundaries are checked at several levels:

* native C ABI tests compare adapter behavior with engine board/search behavior
* Emscripten Node smokes load the generated module and exercise raw ABI calls
* `WasmCore` Node smokes cover board operations, artifact loading, evaluation,
  and bounded search
* Web CI typechecks and builds React/Vite with generated runtime assets
* Pages CI verifies the output contains the module, WASM binary, and a complete
  copied default artifact

Detailed native-versus-WASM position/search parity and a browser interaction
smoke are not yet implemented.

## Current Limitations

The browser app has no side selector, difficulty selector, continuous
evaluation view, score explanation, game review, opening book, hard
cancellation, threaded WASM, or browser pthread support. CPU opponent mode is
fixed to human Black and CPU White. The UI and presets make no Elo or
production-strength claim.

Threaded WASM would require a separate design for shared state and hosting
headers. Review labels, blunder thresholds, and human explanations belong in
an app-level adapter over board history and search results, not in board core,
recursive search, or evaluator hot paths.

## Change Checklist

When changing this boundary:

* update `wasm/README.md` for C ABI or wrapper behavior
* update `apps/web/README.md` for user-visible development or runtime behavior
* update `docs/repository-layout.md` if ownership moves
* update the C ABI version/layout checks when a wire structure changes
* keep generated WASM, copied evaluation files, and `dist/` out of git
* add boundary tests, or state why a relevant layer could not be tested
