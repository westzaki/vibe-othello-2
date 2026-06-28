# WASM Runtime Assets

Generated Emscripten runtime files are copied here for local browser runs:

```text
vibe_othello_wasm_module.mjs
vibe_othello_wasm_module.wasm
```

The generated `.mjs` and `.wasm` files are intentionally ignored by git.

Build and copy them from an Emscripten CMake build with:

```sh
cmake --build build-wasm --target vibe_othello_copy_web_wasm_assets
```
