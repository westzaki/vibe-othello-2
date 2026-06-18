# Engine Tests

Engine tests use Catch2 and run through CTest.
Test sources and test-only helpers live under this directory. Checked-in data
shared with benchmarks or golden generation scripts lives in `../fixtures/`.

## Board Core Tests

| File | Role |
| --- | --- |
| `board_core/coordinates_test.cc` | Coordinate, square, and bit-index invariants. |
| `board_core/position_test.cc` | Initial position, color-relative views, and validity helpers. |
| `board_core/move_generation_test.cc` | Known legal move examples and edge/wraparound cases. |
| `board_core/flip_test.cc` | Known flip masks, illegal move flips, and multi-direction flips. |
| `board_core/apply_undo_test.cc` | Move delta creation, apply, apply_delta, and undo round-trips. |
| `board_core/pass_terminal_test.cc` | Pass legality, pass application, and terminal detection. |
| `board_core/serialization_test.cc` | Canonical format, parser rejection, and round-trips. |
| `board_core/hash_test.cc` | Hash stability and semantic coverage. |
| `board_core/differential_test.cc` | Focused production-vs-reference comparisons. |
| `board_core/corpus_test.cc` | Representative and random corpus positions against reference behavior. |
| `board_core/random_play_differential_test.cc` | Full deterministic random games against the reference board. |
| `board_core/property_test.cc` | Invariants that must hold for representative and random reachable positions. |
| `board_core/perft_test.cc` | Move-tree validation including pass and terminal behavior. |

## Evaluation Tests

| File | Role |
| --- | --- |
| `evaluation/pattern_schema_test.cc` | Pattern definition validation, fixed pattern-set fixture shape, duplicate-square policy, and size overflow coverage. |
| `evaluation/pattern_weights_test.cc` | Pattern weight artifact loader validation and loaded-to-runtime weight conversion. |
| `evaluation/tiny_pattern_evaluator_test.cc` | Tiny pattern evaluator indexing, fixture-backed weights, validation failures, phase, sign convention, determinism, and score range. |

## Search Tests

| File | Role |
| --- | --- |
| `search/reference_search_test.cc` | Reference search behavior and deterministic result coverage. |
| `search/alphabeta_test.cc` | Alpha-beta results against reference search behavior. |
| `search/iterative_search_test.cc` | Iterative search behavior across depth limits and pass/terminal positions. |
| `search/move_ordering_test.cc` | Move ordering legality preservation and priority feature coverage. |
| `search/null_window_search_test.cc` | Internal null-window fail-high and fail-low bound semantics. |
| `search/pvs_test.cc` | PVS correctness neutrality against alpha-beta when enabled. |
| `search/search_limits_test.cc` | Cooperative node, time, infinite, and external-stop limit behavior. |
| `search/search_options_test.cc` | Search option normalization defaults, legacy/typed equivalence, and conflict compatibility rules. |
| `search/search_result_invariant_test.cc` | Cross-cutting `SearchResult` publication invariants for midgame, endgame, stopped, terminal, and forced-pass roots. |
| `search/transposition_table_test.cc` | Midgame, exact-score endgame, and WLD endgame transposition-table storage, probing, cutoff, and kind-separation semantics. |
| `search/endgame_test.cc` | Root exact/WLD endgame integration, pass handling, flags, legality, and PV replay. |
| `search/endgame_reference_test.cc` | Production exact endgame results against the slow reference solver. |
| `search/endgame_corpus_test.cc` | Exact endgame corpus coverage against the reference solver. |
| `search/endgame_small_empty_test.cc` | Specialized 0/1/2/3/4-empty exact-score paths against the generic solver. |

## Test Support

| File | Role |
| --- | --- |
| `support/board_core/reference_board.*` | Slow readable reference board for differential tests. |
| `support/board_core/perft.*` | Shared perft implementation for board-core validation. |
| `support/board_core/corpus.h` | Shared representative positions, deterministic seeds, RNG, and move selection helpers. |
| `support/search/reference_search.*` | Shared reference search implementation for search validation. |
| `support/search/reference_endgame.*` | Slow readable exact endgame solver for differential tests. |
| `support/search/endgame_positions.*` | Shared deterministic endgame positions for tests. |
