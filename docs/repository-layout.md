# Repository Layout

The repository is organized by responsibility, not by programming language.

## Initial Layout

```text
repo/
├─ AGENTS.md
├─ README.md
├─ .github/
├─ docs/
├─ engine/
├─ tools/
├─ apps/
├─ .clang-format
└─ .clang-tidy
```

## Responsibilities

| Path | Purpose |
| --- | --- |
| `AGENTS.md` | Agent working rules |
| `README.md` | Project entry point |
| `.github/` | Pull request templates and CI workflows |
| `docs/` | Architecture, layout, style, and review documents |
| `engine/` | Native C++ Othello engine static library |
| `tools/` | Developer and validation command-line tools |
| `apps/` | User-facing applications |
| `.clang-format` | C++ formatting rules |
| `.clang-tidy` | C++ static analysis rules |

## Engine Layout

The engine owns Othello rules and state transitions. It builds as a static
library; executable entry points live in `tools/` or `apps/`.

```text
engine/
├─ README.md
├─ include/
├─ src/
└─ tests/
```

## Tools Layout

Tools link against engine libraries and provide small command-line entry points for
development, smoke checks, and validation.

```text
tools/
└─ engine-smoke/
   └─ main.cc
```

## Apps Layout

Applications live under apps/.

```text
apps/
└─ web/
   ├─ README.md
   ├─ src/
   └─ tests/
```
