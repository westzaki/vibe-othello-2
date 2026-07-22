add_library(vibe_othello_engine STATIC)
add_library(vibe_othello::engine ALIAS vibe_othello_engine)

target_sources(
  vibe_othello_engine
  PRIVATE src/board_core/board.cc
          src/board_core/hash.cc
          src/board_core/serialization.cc
          src/evaluation/early_midgame_heuristic_evaluator.cc
          src/evaluation/pattern.cc
          src/evaluation/pattern_artifact.cc
          src/evaluation/pattern_evaluator.cc
          src/evaluation/pattern_weights.cc
          src/evaluation/phase_aware_evaluator.cc
          src/search/alphabeta.cc
          src/search/endgame_root.cc
          src/search/endgame_search.cc
          src/search/endgame_small_empty.cc
          src/search/endgame_stability.cc
          src/search/endgame_tt.cc
          src/search/move_ordering/endgame_ordering.cc
          src/search/move_ordering/midgame_ordering.cc
          src/search/move_ordering/mobility_ordering.cc
          src/search/move_ordering/move_list.cc
          src/search/move_ordering/parity_ordering.cc
          src/search/move_ordering/static_othello_ordering.cc
          src/search/pvs.cc
          src/search/probcut.cc
          src/search/result_builder_internal.cc
          src/search/root_search.cc
          src/search/search_options.cc
          src/search/search_node_common.cc
          src/search/search_session.cc
          src/search/shadow_calibration.cc
          src/search/search_util.cc
          src/search/transposition_table.cc)

target_include_directories(
  vibe_othello_engine
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(vibe_othello_engine PUBLIC cxx_std_20)
