add_library(vibe_othello_engine STATIC)
add_library(vibe_othello::engine ALIAS vibe_othello_engine)

target_sources(
  vibe_othello_engine
  PRIVATE src/board_core/board.cc
          src/board_core/hash.cc
          src/board_core/serialization.cc
          src/evaluation/pattern.cc
          src/evaluation/pattern_weights.cc
          src/evaluation/tiny_pattern_evaluator.cc
          src/search/alphabeta.cc
          src/search/endgame.cc
          src/search/move_ordering.cc
          src/search/pvs.cc
          src/search/root_search.cc
          src/search/search_node_common.cc
          src/search/search_util.cc
          src/search/transposition_table.cc
          src/version.cc)

target_include_directories(
  vibe_othello_engine
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(vibe_othello_engine PUBLIC cxx_std_20)
