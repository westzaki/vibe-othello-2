include(FetchContent)

set(CATCH_INSTALL_DOCS
    OFF
    CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS
    OFF
    CACHE BOOL "" FORCE)
set(CATCH_BUILD_TESTING
    OFF
    CACHE BOOL "" FORCE)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.15.0)
FetchContent_MakeAvailable(Catch2)

add_library(
  vibe_othello_engine_test_support STATIC
  support/board_core/perft.cc
  support/search/endgame_positions.cc
  support/board_core/reference_board.cc
  support/search/reference_endgame.cc
  support/search/reference_search.cc)

target_include_directories(vibe_othello_engine_test_support
                           PUBLIC support)

target_link_libraries(vibe_othello_engine_test_support PUBLIC vibe_othello::engine)

target_compile_features(vibe_othello_engine_test_support PUBLIC cxx_std_20)

add_executable(
  vibe_othello_engine_tests
  board_core/apply_undo_test.cc
  board_core/corpus_test.cc
  board_core/coordinates_test.cc
  board_core/differential_test.cc
  board_core/flip_test.cc
  board_core/hash_test.cc
  board_core/move_generation_test.cc
  board_core/pass_terminal_test.cc
  board_core/perft_test.cc
  board_core/position_test.cc
  board_core/property_test.cc
  board_core/random_play_differential_test.cc
  board_core/serialization_test.cc
  evaluation/pattern_evaluator_test.cc
  evaluation/pattern_schema_test.cc
  evaluation/pattern_weights_test.cc
  evaluation/tiny_pattern_evaluator_test.cc
  search/alphabeta_test.cc
  search/endgame_reference_test.cc
  search/endgame_corpus_test.cc
  search/endgame_small_empty_test.cc
  search/endgame_test.cc
  search/iterative_search_test.cc
  search/move_ordering_test.cc
  search/null_window_search_test.cc
  search/pvs_test.cc
  search/reference_search_test.cc
  search/search_result_invariant_test.cc
  search/search_limits_test.cc
  search/search_options_test.cc
  search/transposition_table_test.cc)

target_link_libraries(
  vibe_othello_engine_tests PRIVATE vibe_othello_engine_test_support
                                    Catch2::Catch2WithMain)

target_compile_definitions(vibe_othello_engine_tests
                           PRIVATE VIBE_OTHELLO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_compile_features(vibe_othello_engine_tests PRIVATE cxx_std_20)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(vibe_othello_engine_tests)
