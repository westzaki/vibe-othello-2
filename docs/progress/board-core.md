# Board Core Progress

## Purpose

This document tracks current implementation status for board core.

The intended design lives in `docs/architecture/board-core.md`.

This file may change frequently as implementation progresses.

## Design Sources

Relevant design documents:

* `docs/architecture/board-core.md`
* `docs/architecture/search.md`

## Current Implementation

The current board-core implementation includes:

* fixed bit order and coordinate helpers
* core value types for positions, colors, squares, moves, and deltas
* bitboard legal move generation
* flip calculation
* checked move application
* precomputed delta application for trusted hot paths
* undo for normal moves and passes
* pass and terminal detection
* canonical serialization and parsing
* deterministic full position hashing
* a slow reference board in test support
* unit, differential, property, random-play, and perft tests
* local benchmark coverage and checked-in board-core baseline data

Search and evaluation are built on top of this surface. They should continue to
use board-core primitives for rule behavior instead of duplicating move legality,
pass, undo, serialization, or hashing logic.

## Current Gaps

There are no required board-core gaps for the current search foundation.

Optional future work includes:

* incremental hashing if profiling shows full recomputation is too expensive
* additional benchmark baselines for new machines or compilers
* extra corpora for unusual pass, terminal, or serialization cases
* adapter-specific validation when new public consumers are added

## Implementation Status

Status values:

* `done` means implemented in the repository
* `not started` means no production implementation exists yet
* `deferred` means intentionally left for a later phase

| Area | Status | Notes |
| --- | --- | --- |
| Bitboard representation | done | Production board core |
| Coordinate helpers | done | Fixed internal bit order |
| Legal move generation | done | Bitboard implementation |
| Flip calculation | done | Tested independently |
| Checked move application | done | External-input path |
| Move deltas | done | Trusted hot path for search |
| Undo for normal moves and passes | done | Search depends on this |
| Pass and terminal detection | done | Board core owns rule semantics |
| Serialization and parsing | done | Canonical text format |
| Full position hashing | done | Source of truth |
| Reference board | done | Test support only |
| Perft support | done | Validation tool |
| Unit, differential, property, random-play, and perft tests | done | Board-core test suite |
| Board-core benchmark | done | Local benchmark coverage |
| Checked-in board-core baseline data | done | Baseline metadata must avoid personal identifiers |
| Incremental hashing | not started | Optional future optimization |

## Completion Bar

Board core is strong enough to build on when:

* fixed bit order is documented
* position validity invariants are documented
* move, pass, and terminal semantics are documented
* legal moves, flips, apply, and undo are tested
* serialization round-trips are tested
* hashing is deterministic and tested
* reference-board differential tests exist
* perft tests exist
* random-play differential tests exist
* hot-path benchmarks exist
* search can use board-core primitives without duplicating rule behavior

## Progress Update Rules

Update this document when:

* current board-core coverage changes
* an optional item moves into scope
* a benchmark baseline is added or replaced
* a known implementation gap is discovered
* a new public consumer needs adapter-specific validation

Update `docs/architecture/board-core.md` only when the intended design,
boundary, semantics, or correctness rules change.
