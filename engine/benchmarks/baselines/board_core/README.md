# Board-Core Benchmark Baselines

Board-core baselines track rule-engine hot paths.

The current board-core benchmark reports:

| Benchmark | Meaning |
| --- | --- |
| `legal_moves` | Generate all legal moves for representative positions. |
| `has_legal_move` | Check whether the side to move has at least one legal move. |
| `flips_for_move` | Compute flipped discs for representative legal moves. |
| `apply_move` | Validate and apply a move while producing a delta. |
| `make_move_delta` | Precompute move deltas without mutating the position. |
| `apply_move_delta` | Apply a precomputed move delta. |
| `undo_move` | Restore the exact previous position from a delta. |
| `hash_position` | Fully recompute the deterministic position hash. |

When adding a baseline file, include enough environment metadata to make future
comparisons meaningful. Keep raw one-off outputs in `engine/benchmarks/results/`
instead.
