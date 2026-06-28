# Vibe Othello WASM Adapter

`wasm/` is the top-level adapter layer for browser/WASM-facing engine
boundaries.

This directory currently contains a native-buildable C ABI adapter for the board
core plus an opt-in Emscripten module target for that same adapter. It links
against the engine public C++ API and does not duplicate Othello rules.

The current adapter exposes:

* ABI version query
* initial position
* position query for legal moves, legal-move availability, and terminal state
* checked move application
* checked pass application

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

When Node is available, CTest runs `wasm/smoke/check_wasm_module.mjs` against
the generated module. This smoke only verifies module loading and execution of
the existing C ABI surface.

This directory does not currently contain:

* generated `.wasm` or `.mjs` runtime output
* TypeScript wrappers
* Web Worker protocol code
* React, Vite, or `apps/web`
* GitHub Pages deployment workflow
* search or evaluation bindings

The native default CMake build must keep working without Node or Emscripten.
TypeScript wrappers, Worker integration, React, Vite, `apps/web`, GitHub Pages,
search bindings, and evaluation bindings are deferred.

`engine/` must not depend on this directory.
