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
| `engine/` | Native C++ Othello engine |
| `apps/` | User-facing applications |
| `.clang-format` | C++ formatting rules |
| `.clang-tidy` | C++ static analysis rules |

## Engine Layout

The engine owns Othello rules and state transitions.

```text
engine/
├─ README.md
├─ include/
├─ src/
└─ tests/
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
