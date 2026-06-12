# C++ Coding Style

## Baseline

- C++20
- LLVM style as the base formatting style
- C++ Core Guidelines for ownership, lifetime, safety, and API design
- clang-format is the source of truth for formatting

## Error handling

- For engine/internal logic, prefer explicit result values or status types.
- Do not throw exceptions across low-level hot paths unless the project explicitly allows it.
