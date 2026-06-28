# Vibe Othello Apps

Applications live under `apps/`.

Application subdirectories own user-facing entry points, app-specific runtime
commands, app state, rendering, and user-facing formatting.

## Applications

* `web/` owns the browser app skeleton. It uses React + Vite + TypeScript and
  talks to the board-core WASM adapter through a Web Worker.

Apps may consume public engine APIs through appropriate adapters, but they must
not own reusable engine implementation or C++ WASM adapter source.

The engine remains browser-agnostic. Web apps should call engine behavior
through the WASM adapter and Web Worker boundary rather than depending on engine
internals or duplicating Othello rules.

See `../docs/architecture/web-app.md` for the intended web app and WASM
architecture.
