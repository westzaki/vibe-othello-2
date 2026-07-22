# Board Core Architecture

## Purpose

Board core implements the rules and state transitions of Othello.

It is the foundation used by search, evaluation, pattern learning, WASM, tools, and UI.

The board core must be correct, deterministic, fast, and easy to validate.

## Implemented System

The production implementation under `engine/src/board_core/` provides the
complete rule surface used by the rest of the repository:

* relative two-bitboard positions with absolute color helpers
* checked normal-move and pass application
* precomputed `MoveDelta` application and exact undo for trusted search paths
* reference and unrolled legal-move and flip implementations
* pass and terminal detection
* canonical parsing and formatting
* deterministic full-position hashing and `hash_after_move`

Search uses these operations directly and maintains no competing rule model.
The public implementation is covered by unit, reference differential,
property, deterministic random-game, perft, serialization, and incremental-hash
tests. `engine/benchmarks/board_core_bench.cc` and checked-in aggregate
baselines protect the hot paths.

There is no required board-core gap for the current search, evaluation, WASM,
or tooling stack. New consumers may require adapter-specific validation, but
they must not introduce rules outside this module.

## Boundary

Board core owns:

* board representation
* move representation
* legal move generation
* flip calculation
* move application
* move undo
* pass detection
* terminal detection
* disc counting
* serialization
* hashing
* correctness validation helpers

Board core does not own:

* move choice
* search
* evaluation
* pattern learning
* opening book
* endgame solving
* UI rendering
* game review
* blunder detection

Search, evaluation, WASM, tools, and UI may depend on board core.
Board core must not depend on them.

## Core Approach

The production board core is bitboard-based.

A simple array-based reference implementation lives in test support for
correctness checks.

The bitboard implementation is optimized for speed.

The reference implementation is optimized for clarity.

Both implementations must agree in tests.

## Representation

Use two 64-bit bitboards:

* one for the side to move
* one for the opponent

```cpp
using Bitboard = std::uint64_t;

enum class Color : std::uint8_t {
  black,
  white,
};

struct Position {
  Bitboard player;
  Bitboard opponent;
  Color side_to_move;
};
```

`player` means the side to move.
`opponent` means the other side.

This relative representation keeps move generation and move application simple and fast.

UI, serialization, logs, and debugging may expose black/white views.
Core algorithms should use `player` / `opponent`.

## Bit Order and Coordinates

Board core uses a fixed internal bit order.

```text
a8 b8 c8 d8 e8 f8 g8 h8   bits 56..63
a7 b7 c7 d7 e7 f7 g7 h7   bits 48..55
a6 b6 c6 d6 e6 f6 g6 h6   bits 40..47
a5 b5 c5 d5 e5 f5 g5 h5   bits 32..39
a4 b4 c4 d4 e4 f4 g4 h4   bits 24..31
a3 b3 c3 d3 e3 f3 g3 h3   bits 16..23
a2 b2 c2 d2 e2 f2 g2 h2   bits  8..15
a1 b1 c1 d1 e1 f1 g1 h1   bits  0..7
```

Rules:

* `a1 = bit 0`
* `h1 = bit 7`
* `a8 = bit 56`
* `h8 = bit 63`
* file index: `a = 0`, ..., `h = 7`
* rank index: `1 = 0`, ..., `8 = 7`

Square index:

```cpp
index = rank_index * 8 + file_index;
bit = 1ULL << index;
```

Directional offsets:

* east       +1
* west       -1
* north      +8
* south      -8
* north-east +9
* north-west +7
* south-east -7
* south-west -9

Directional shifts must mask board edges before shifting to avoid wraparound.

Examples:

* h1 east must not become a2
* a1 west must not wrap
* h8 north-east must not wrap

Internal bit order is an engine rule.

UI orientation is a presentation concern and must not change engine bit order.

Do not change bit order silently.

## Position Validity

A valid position must satisfy:

```cpp
(position.player & position.opponent) == 0
```

A valid position is not necessarily reachable from the initial position.

Tests may use both:

* reachable positions from legal games
* valid synthetic positions for edge cases

Board core functions preserve position validity for valid inputs.

## Move Semantics

A normal move represents one square.

A legal normal move must:

* be on an empty square
* flip at least one opponent disc

Pass is a board-core operation used only when the side to move has no legal move.

Pass must:

* not place a disc
* not flip discs
* not change occupancy
* swap player/opponent perspective
* toggle side to move

Move encoding details may live in code, but pass behavior must remain stable and tested.

## Move Generation

Legal move generation uses directional bitboard operations.

For each of the 8 directions:

* mask board edges to avoid wraparound
* shift from player discs toward opponent discs
* collect opponent runs
* detect empty squares beyond those runs

A legal move must:

* be on an empty square
* flip at least one opponent disc

The implementation favors simple, testable bitboard code over dense
micro-optimizations.

## Flip Calculation

Move generation answers “where can I move?”

Flip calculation answers “what discs change if I move here?”

Flip calculation is deterministic and independently testable.

A non-pass move is legal only when the flip mask is non-zero.

## Move Application

Move application uses a precomputed move delta.

Conceptually:

* place the move disc
* flip captured discs
* swap player/opponent perspective
* toggle side to move

The move delta stores the move and flipped-disc mask.
It does not store a full previous position.
For a normal move, undo reconstructs the previous position algebraically from the
current position, placed square, and flipped-disc mask.
For a pass, undo swaps perspective back without changing occupancy.

Apply and undo must round-trip exactly.

Board core exposes two delta application paths:

* a hot path that trusts a delta produced for the current position
* a checked path that recomputes and verifies the delta before applying it

Search should use the hot path after creating a delta through board core.
Tests, adapters, and external input should use the checked path when accepting a
delta-shaped value from outside the trusted flow.

## Pass and Terminal Rules

If the side to move has no legal move, it may pass.

After pass:

* player/opponent perspective swaps
* side to move toggles
* no discs change

The game is terminal only when both sides have no legal move.

Pass handling is part of board core, not search.

## Serialization

Board core supports a stable text format for tests, tools, and debugging.

Serialization uses an absolute black/white board view, not the internal `player` / `opponent` view.

Canonical format:

```text
<board> <side-to-move>
```

`<board>` is 8 rows separated by `/`.

Rows are written from rank 8 to rank 1.

Each row is written from file `a` to file `h`.

Characters:

* `B` black disc
* `W` white disc
* `.` empty square

`<side-to-move>` is:

* `b` black to move
* `w` white to move

Initial position:

```text
......../......../......../...BW.../...WB.../......../......../........ b
```

This represents:

```text
a8 b8 c8 d8 e8 f8 g8 h8
...
a1 b1 c1 d1 e1 f1 g1 h1
```

Parser rules:

* accept only the canonical shape: 8 rows, 8 squares per row, one side-to-move field
* reject unknown characters
* reject missing or extra rows
* reject missing or extra side-to-move values
* reject extra non-whitespace content
* convert black/white view into the internal relative representation

Formatter rules:

* always emit the canonical format
* use exactly one space between `<board>` and `<side-to-move>`
* emit rows from rank 8 to rank 1
* emit files from `a` to `h`
* do not include move history, score, hash, or legal moves

Serialization must round-trip exactly:

```text
parse(format(position)) == position
```

Serialization is a boundary format.

It is not the internal representation.

Changes to this format must be documented and tested.

## Hashing

Board core provides deterministic position hashing.

Hashing is used by search, tests, caches, and tools.

Hashing must include:

* occupied squares
* disc colors
* side to move

Full recomputation is the source of truth.

If incremental hashing is added, it must match full recomputation.

## Reference Implementation

A slow, clear reference implementation exists in test support.

It uses this shape internally:

```cpp
std::array<Cell, 64>
```

The reference implementation is intentionally boring and readable.

It exists to catch mistakes in the optimized bitboard implementation.

Use it for differential tests.

## Perft

Perft is a board-core validation tool.

It recursively counts legal move trees from a position.

Perft validates:

* legal move generation
* flip calculation
* apply move
* pass handling
* terminal detection

Perft must not:

* choose a move
* evaluate a position
* use search heuristics
* use transposition tables as part of correctness tests

Perft is allowed because it verifies board rules, not engine strength.

Perft depth is counted in plies.

Rules:

* `perft(position, 0) = 1`
* if legal moves exist, sum `perft(next, depth - 1)` for all legal moves
* if no legal move exists but the opponent can move, pass counts as one ply
* if neither side can move, the position is terminal

## Testing Strategy

Board core correctness is protected by multiple test styles.

Unit tests cover known examples:

* initial position
* edge flips
* corner flips
* diagonal flips
* multi-direction flips
* illegal occupied moves
* illegal no-flip moves
* pass positions
* terminal positions
* serialization round-trips
* hash stability

Differential tests compare:

* bitboard implementation
* reference implementation

Property tests cover invariants:

* `player & opponent == 0`
* legal moves are empty squares
* legal non-pass moves flip discs
* applying a non-pass move increases occupancy by one
* apply then undo restores the exact position
* serialization round-trips
* full hash recomputation matches incremental hash at every apply, pass, and undo

Perft tests validate move trees.

Benchmarks track hot-path performance.

The scalar loop implementation remains the correctness and rollback reference.
The separately named unrolled legal-move and flip paths use the same public
position semantics and are differentially checked on deterministic random game
corpora. Search-internal incremental hashing is implemented through
`hash_after_move`; it does not add mutable state to `Position`.

## Performance Principles

Hot-path board core functions should be:

* allocation-free
* exception-free
* deterministic
* logging-free
* file-I/O-free
* independent from UI and WASM

Correctness comes before micro-optimization.

Optimize only after tests can detect mistakes.

## Change Checklist

When changing board-core public behavior, update this document and the relevant
tests in `engine/tests/board_core`.

Rule or representation changes should run:

* unit tests
* differential tests against the reference board
* property tests
* random-play differential tests
* perft tests
* sanitizer tests where available

Hot-path changes should also run the board-core benchmark and compare against
the checked-in baseline for the same machine, compiler, and build type when
possible.

Boundary changes should update all affected adapters and docs. Serialization,
hashing, bit order, move semantics, and pass behavior are public behavior for
the rest of the engine.
