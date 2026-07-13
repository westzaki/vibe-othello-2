# Vibe Othello WASM Adapter

`wasm/` is the top-level adapter layer for browser/WASM-facing engine
boundaries.

This directory currently contains a native-buildable C ABI adapter for the board
core, an opt-in Emscripten module target for that same adapter, and a minimal
plain ESM JavaScript wrapper around the generated module. It links against the
engine public C++ API and does not duplicate Othello rules.

The current adapter exposes:

* ABI version query
* initial position
* position query for legal moves, legal-move availability, and terminal state
* checked move application
* checked pass application
* loading an evaluation artifact from manifest text plus weights bytes
* side-to-move-relative phase-aware position evaluation through the loaded artifact
* legacy bounded best-move search through the same phase-aware artifact evaluator
* preset-based bounded best-move search with independent depth/node/time limits
* ABI layout introspection for the C structs read by JavaScript

The plain JavaScript wrapper lives in `wasm/js/wasmCore.mjs`. It converts the raw
C ABI memory layout into small domain objects such as:

```js
{
  player: 0n,
  opponent: 0n,
  sideToMove: "black",
}
```

The wrapper is intentionally Node/browser-neutral and uses the exported layout
introspection functions instead of hardcoded struct offsets.

Evaluation artifact ownership is explicit:

```js
const artifact = core.loadEvaluationArtifact(manifestText, weightsBytes);
try {
  const score = artifact.evaluatePosition(position);
  const result = artifact.searchBestMove(position, { maxDepth: 1 });
  const normal = artifact.searchBestMoveWithPreset(
    position,
    { maxDepth: 8, maxTimeMs: 500 },
    "normal",
    8,
  );
} finally {
  artifact.free();
}
```

Search requests must be bounded with `maxDepth`, `maxNodes`, or `maxTimeMs`.
The WASM path does not support infinite search, advanced cancellation, threaded
WASM, or browser pthreads.

`searchBestMove()` preserves the legacy C ABI behavior, which uses empty
`SearchOptions`. `searchBestMoveWithPreset()` exposes only `easy`, `normal`,
and `hard` algorithm presets instead of individual search internals. Limits are
always caller-provided; `normal` and `hard` currently enable the same algorithm
set, while callers choose wider limits for hard play. A nonzero final argument
enables exact-score endgame search at or below that empty-square threshold and
requires `maxNodes` or `maxTimeMs`: exact root search intentionally ignores a
depth limit.

All WASM presets leave ProbCut and Multi-ProbCut disabled. `normal` remains
disabled by policy; `hard` must also remain disabled until a reviewed matching
calibration profile and a WASM fixed-time strength/overhead gate both pass.
The C++ runtime capability is not exposed as a browser option, and no profile is
embedded in the module.

## Native adapter build

The normal repository build compiles the C ABI adapter as a native static
library and runs the native adapter tests through CTest. It does not require
Node or Emscripten.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Opt-in Emscripten module build

The Emscripten module is off by default. Build it explicitly with
`VIBE_OTHELLO_BUILD_WASM_MODULE=ON` from an Emscripten CMake environment.

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVIBE_OTHELLO_BUILD_WASM_MODULE=ON
cmake --build build-wasm --target vibe_othello_wasm_module
ctest --test-dir build-wasm --output-on-failure
```

The module target emits an ES module `.mjs` file and companion `.wasm` file in
the build tree. These generated files are build artifacts and must not be
committed to git.

`wasm/` owns producing the generated module only. App-specific runtime asset
copying is repo-level CMake integration.

To run the browser app locally with the generated runtime assets, use the
repo-level copy target:

```sh
cmake --build build-wasm --target vibe_othello_copy_web_wasm_assets
```

This target copies:

```text
build-wasm/wasm/vibe_othello_wasm_module.mjs
build-wasm/wasm/vibe_othello_wasm_module.wasm
```

into:

```text
apps/web/public/wasm/
```

When Node is available, CTest runs `wasm/smoke/check_wasm_module.mjs` and
`wasm/smoke/check_wasm_core.mjs` against the generated module. The first smoke
checks raw module loading and C ABI calls. The second smoke checks the plain ESM
`WasmCore` wrapper, including committed evaluation artifact loading,
position evaluation, and a tiny bounded best-move search.

To run the Emscripten smokes through CTest:

```sh
ctest --test-dir build-wasm --output-on-failure
```

This directory does not currently contain:

* generated `.wasm` or `.mjs` runtime output
* TypeScript wrappers
* Web Worker protocol code
* React, Vite, or `apps/web`
* app-specific runtime asset copying
* GitHub Pages deployment workflow
* Worker fetching from `/eval/default-artifact.json`
* React CPU opponent UI or automatic CPU moves

The native default CMake build must keep working without Node or Emscripten.
Browser Worker, React, Vite, GitHub Pages, artifact fetching, and CPU-opponent
integration live under `apps/web`; this adapter remains their engine-facing ABI
boundary. A TypeScript wrapper is still deferred.

`engine/` must not depend on this directory.
