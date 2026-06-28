# Vibe Othello WASM Adapter

`wasm/` is the top-level adapter layer for browser/WASM-facing engine
boundaries.

This directory currently contains only a native-buildable C ABI adapter for the
board core. It links against the engine public C++ API and does not duplicate
Othello rules.

The current adapter exposes:

* ABI version query
* initial position
* position query for legal moves, legal-move availability, and terminal state
* checked move application
* checked pass application

This directory does not currently contain:

* Emscripten-specific build flags
* generated `.wasm` or `.mjs` runtime output
* TypeScript wrappers
* Web Worker protocol code
* React, Vite, or `apps/web`
* GitHub Pages deployment workflow
* search or evaluation bindings

The native default CMake build must keep working without Node or Emscripten.
Emscripten-specific build steps should remain opt-in when they are added later.

`engine/` must not depend on this directory.
