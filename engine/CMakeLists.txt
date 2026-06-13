add_library(vibe_othello_engine STATIC)
add_library(vibe_othello::engine ALIAS vibe_othello_engine)

target_sources(
  vibe_othello_engine
  PRIVATE src/board_core/board.cc
          src/board_core/hash.cc
          src/board_core/serialization.cc
          src/search/alphabeta.cc
          src/search/best_move_table.cc
          src/search/move_ordering.cc
          src/search/root_search.cc
          src/version.cc)

target_include_directories(
  vibe_othello_engine
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(vibe_othello_engine PUBLIC cxx_std_20)
