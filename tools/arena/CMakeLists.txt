add_library(vibe_othello_arena_core STATIC arena_core.cc)

target_link_libraries(vibe_othello_arena_core PUBLIC vibe_othello::engine)
target_include_directories(vibe_othello_arena_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(vibe_othello_arena main.cc)

target_link_libraries(vibe_othello_arena PRIVATE vibe_othello_arena_core)

set_target_properties(vibe_othello_arena PROPERTIES OUTPUT_NAME vibe-othello-arena)

add_executable(vibe_othello_pattern_artifact_arena pattern_artifact_arena.cc)

target_link_libraries(vibe_othello_pattern_artifact_arena PRIVATE vibe_othello_arena_core)

set_target_properties(vibe_othello_pattern_artifact_arena
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-artifact-arena)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  if(TARGET Catch2::Catch2WithMain)
    add_executable(vibe_othello_arena_tests arena_core_test.cc)
    target_link_libraries(vibe_othello_arena_tests PRIVATE vibe_othello_arena_core
                                                         Catch2::Catch2WithMain)
    add_test(NAME vibe_othello_arena_tests COMMAND vibe_othello_arena_tests)
  endif()

  add_test(
    NAME vibe_othello_pattern_artifact_arena_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_pattern_artifact_arena_smoke.py
            --exe $<TARGET_FILE:vibe_othello_pattern_artifact_arena>)
endif()
