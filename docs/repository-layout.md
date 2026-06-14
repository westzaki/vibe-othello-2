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
| `docs/` | Architecture, progress, layout, style, and review documents |
| `engine/` | Native C++ Othello engine static library |
| `tools/` | Developer and validation command-line tools |
| `apps/` | User-facing applications |
| `.clang-format` | C++ formatting rules |
| `.clang-tidy` | C++ static analysis rules |

## Documentation Layout

Documentation is indexed from `docs/README.md`.

Architecture documents describe intended boundaries, semantics, and invariants.

Progress documents describe current implementation state, milestones, benchmark
notes, temporary gaps, and deferred work.

Progress documents must not redefine architecture.

```text
docs/
├─ README.md
├─ architecture/
├─ progress/
├─ repository-layout.md
└─ cpp-coding-style.md
```

## Engine Layout

The engine owns Othello rules and state transitions. It builds as a static
library; executable entry points live in `tools/` or `apps/`.

```text
engine/
├─ README.md
├─ benchmarks/
├─ include/
├─ src/
├─ test_support/
└─ tests/
```

Engine tests use Catch2 and run through CTest.
Shared test-only helpers live in `engine/test_support/` and are linked only by
test targets.

## Tools Layout

Tools link against engine libraries and provide small command-line entry points for
development, smoke checks, and validation.

```text
tools/
├─ arena/
│  ├─ openings/
│  └─ README.md
├─ engine-cli/
│  └─ main.cc
├─ engine-smoke/
│  └─ main.cc
├─ endgame/
│  ├─ check_baseline.py
│  ├─ check_golden.py
│  └─ generate_golden.sh
└─ search/
   ├─ check_golden.py
   └─ generate_golden.sh
```

## Apps Layout

Applications live under apps/.

The current repository has only the applications README. Add application
subdirectories here as user-facing apps are introduced.

```text
apps/
└─ README.md
```
