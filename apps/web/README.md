# Vibe Othello Web

This is the first browser app skeleton for Vibe Othello.

The app uses React + Vite + TypeScript for UI and talks to the engine through a
Web Worker. React code consumes serializable worker snapshots only. It does not
import the Emscripten module, call `WasmCore` directly, or manipulate WASM
memory.

The minimal app supports resetting to the initial position, playing legal moves
by clicking board markers, and manually passing when the engine reports that the
side to move has no legal move and the position is not terminal.

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
cmake --build build-wasm --target vibe_othello_copy_web_wasm_assets
```

The copy target places the generated `.mjs` and `.wasm` files from the build
tree into `apps/web/public/wasm/`.

If the files are missing, the Vite build still works, but browser runtime engine
initialization reports a readable error.

## GitHub Pages deployment

The repository has a dedicated GitHub Pages workflow at
`.github/workflows/pages.yml`. It runs on pushes to `main` and
`workflow_dispatch`; it does not deploy pull requests.

The workflow builds the Emscripten runtime module, copies the generated runtime
assets into `apps/web/public/wasm/`, builds the Vite app, uploads
`apps/web/dist` as a Pages artifact, and deploys it to GitHub Pages.

The generated `.mjs` and `.wasm` runtime files and `apps/web/dist/` remain build
artifacts and are not committed to git. Repository Pages settings must use
GitHub Actions as the Pages source. After merge and a successful Pages workflow,
the expected public URL is:

```text
https://westzaki.github.io/vibe-othello-2/
```

## Development

```sh
npm ci
npm run dev
npm run typecheck
npm run build
```
