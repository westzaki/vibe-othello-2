# Vibe Othello Web

This is the first browser app skeleton for Vibe Othello.

The app uses React + Vite + TypeScript for UI and talks to the engine through a
Web Worker. React code consumes serializable worker snapshots only. It does not
import the Emscripten module, call `WasmCore` directly, or manipulate WASM
memory.

## Runtime WASM assets

The Worker loads generated Emscripten runtime assets from:

```text
apps/web/public/wasm/vibe_othello_wasm_module.mjs
apps/web/public/wasm/vibe_othello_wasm_module.wasm
```

These files are generated build artifacts and are not committed to git.

Build the module from an Emscripten CMake environment:

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVIBE_OTHELLO_BUILD_WASM_MODULE=ON
cmake --build build-wasm --target vibe_othello_wasm_module
```

Then copy the generated `.mjs` and `.wasm` files from the build tree into
`apps/web/public/wasm/`, for example:

```sh
cp build-wasm/wasm/vibe_othello_wasm_module.mjs apps/web/public/wasm/
cp build-wasm/wasm/vibe_othello_wasm_module.wasm apps/web/public/wasm/
```

If the files are missing, the Vite build still works, but browser runtime engine
initialization reports a readable error.

## Development

```sh
npm ci
npm run dev
npm run typecheck
npm run build
```
