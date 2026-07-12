add_executable(vibe_othello_board_core_bench board_core_bench.cc)
add_executable(vibe_othello_endgame_bench endgame_bench.cc)
add_executable(vibe_othello_evaluation_bench evaluation_bench.cc)
add_executable(vibe_othello_search_bench search_bench.cc)

target_link_libraries(vibe_othello_board_core_bench PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_endgame_bench PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_evaluation_bench PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_search_bench PRIVATE vibe_othello::engine)

target_compile_definitions(vibe_othello_evaluation_bench
                           PRIVATE VIBE_OTHELLO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(vibe_othello_search_bench
                           PRIVATE VIBE_OTHELLO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
