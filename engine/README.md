# Vibe Othello Engine

The engine builds as a C++ static library.

It owns Othello rules and state transitions. Tools, tests, WASM adapters, and
applications should link against the engine library instead of putting executable
entry points inside `engine/`.
