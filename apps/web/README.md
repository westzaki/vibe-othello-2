# Vibe Othello Web

This is the first browser app skeleton for Vibe Othello.

The app uses React + Vite + TypeScript for UI and talks to the engine through a
Web Worker. React code consumes serializable worker snapshots only. It does not
import the Emscripten module, call `WasmCore` directly, or manipulate WASM
memory.

The minimal app supports resetting to the initial position, playing legal moves
by clicking board markers, and manually passing when the engine reports that the
side to move has no legal move and the position is not terminal. It also has a
simple CPU opponent mode where the human is fixed to Black and the CPU is fixed
to White. When that mode is on, the CPU automatically responds after human moves.
When CPU opponent mode is off, a manual CPU move button can still play one
bounded search move for the current side to move.

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

## Runtime evaluation artifact assets

The committed default evaluation artifact can be copied from `data/eval/` into:

```text
apps/web/public/eval/default-artifact.json
apps/web/public/eval/artifacts/<artifact-id>/manifest.json
apps/web/public/eval/artifacts/<artifact-id>/weights.bin
```

`data/eval/` remains the source of truth. `apps/web/public/eval/` is only an
ignored runtime asset directory for local browser runs, Web CI, and GitHub
Pages builds.

Copy the current default artifact with:

```sh
cmake --build build-wasm --target vibe_othello_copy_web_eval_artifact_assets
```

The Worker fetches the default pointer from
`${BASE_URL}eval/default-artifact.json`, resolves the manifest and `weights.bin`
from that static path, loads the bytes through `WasmCore.loadEvaluationArtifact()`,
and uses the loaded artifact for CPU moves.

The browser CPU search is intentionally conservative: depth 2, no node cap, and
a 500 ms time cap. CPU opponent mode is intentionally minimal: the human is
fixed to Black, the CPU is fixed to White, and the same bounded CPU move command
is reused for automatic responses. There is no side selection, difficulty
selector, cancellation UI, threaded WASM, game review UI, or production-strength
claim yet.

## GitHub Pages deployment

The repository has a dedicated GitHub Pages workflow at
`.github/workflows/pages.yml`. It runs on pushes to `main` and
`workflow_dispatch`; it does not deploy pull requests.

The workflow builds the Emscripten runtime module, copies the generated runtime
assets into `apps/web/public/wasm/`, copies the committed default evaluation
artifact into `apps/web/public/eval/`, builds the Vite app, uploads
`apps/web/dist` as a Pages artifact, and deploys it to GitHub Pages.

The generated `.mjs` and `.wasm` runtime files, copied eval payloads under
`apps/web/public/eval/`, and `apps/web/dist/` remain build artifacts and are not
committed to git. Repository Pages settings must use GitHub Actions as the Pages
source. After merge and a successful Pages workflow, the expected public URL is:

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
