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

add_executable(vibe_othello_full_game_artifact_arena full_game_artifact_arena.cc)

add_library(vibe_othello_full_game_artifact_arena_core STATIC full_game_artifact_arena_core.cc)

target_include_directories(vibe_othello_full_game_artifact_arena_core
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(vibe_othello_full_game_artifact_arena PRIVATE vibe_othello_arena_core
                                                               vibe_othello_full_game_artifact_arena_core)

execute_process(
  COMMAND git -C ${PROJECT_SOURCE_DIR} rev-parse HEAD
  RESULT_VARIABLE VIBE_OTHELLO_ARENA_GIT_RESULT
  OUTPUT_VARIABLE VIBE_OTHELLO_ARENA_GIT_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT VIBE_OTHELLO_ARENA_GIT_RESULT EQUAL 0)
  set(VIBE_OTHELLO_ARENA_GIT_SHA "unknown")
endif()
execute_process(
  COMMAND git -C ${PROJECT_SOURCE_DIR} status --porcelain
  RESULT_VARIABLE VIBE_OTHELLO_ARENA_GIT_STATUS_RESULT
  OUTPUT_VARIABLE VIBE_OTHELLO_ARENA_GIT_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(VIBE_OTHELLO_ARENA_GIT_STATUS_RESULT EQUAL 0 AND VIBE_OTHELLO_ARENA_GIT_STATUS STREQUAL "")
  set(VIBE_OTHELLO_ARENA_SOURCE_DIRTY 0)
else()
  set(VIBE_OTHELLO_ARENA_SOURCE_DIRTY 1)
endif()
if(CMAKE_BUILD_TYPE)
  set(VIBE_OTHELLO_ARENA_BUILD_TYPE_VALUE ${CMAKE_BUILD_TYPE})
else()
  set(VIBE_OTHELLO_ARENA_BUILD_TYPE_VALUE "multi-config")
endif()
target_compile_definitions(
  vibe_othello_full_game_artifact_arena
  PRIVATE VIBE_OTHELLO_ARENA_REPOSITORY_SHA="${VIBE_OTHELLO_ARENA_GIT_SHA}"
          VIBE_OTHELLO_ARENA_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
          VIBE_OTHELLO_ARENA_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
          VIBE_OTHELLO_ARENA_BUILD_TYPE="${VIBE_OTHELLO_ARENA_BUILD_TYPE_VALUE}"
          VIBE_OTHELLO_ARENA_SOURCE_DIRTY=${VIBE_OTHELLO_ARENA_SOURCE_DIRTY})

set_target_properties(vibe_othello_full_game_artifact_arena
                      PROPERTIES OUTPUT_NAME vibe-othello-full-game-artifact-arena)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  if(TARGET Catch2::Catch2WithMain)
    add_executable(vibe_othello_arena_tests arena_core_test.cc)
    target_link_libraries(vibe_othello_arena_tests PRIVATE vibe_othello_arena_core
                                                         Catch2::Catch2WithMain)
    add_test(NAME vibe_othello_arena_tests COMMAND vibe_othello_arena_tests)

    add_executable(vibe_othello_full_game_artifact_arena_core_tests
                   full_game_artifact_arena_core_test.cc)
    target_link_libraries(vibe_othello_full_game_artifact_arena_core_tests
                          PRIVATE vibe_othello_full_game_artifact_arena_core
                                  Catch2::Catch2WithMain)
    add_test(NAME vibe_othello_full_game_artifact_arena_core_tests
             COMMAND vibe_othello_full_game_artifact_arena_core_tests)
  endif()

  add_test(
    NAME vibe_othello_pattern_artifact_arena_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_pattern_artifact_arena_smoke.py
            --exe $<TARGET_FILE:vibe_othello_pattern_artifact_arena>)

  add_test(
    NAME vibe_othello_full_game_artifact_arena_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_full_game_artifact_arena_smoke.py
            --exe $<TARGET_FILE:vibe_othello_full_game_artifact_arena>)

  add_test(
    NAME vibe_othello_pattern_artifact_arena_matrix_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/check_pattern_artifact_arena_matrix_smoke.py
            --helper ${CMAKE_CURRENT_SOURCE_DIR}/run_pattern_artifact_arena_matrix.py)

  add_test(
    NAME vibe_othello_fixed_time_artifact_strength_campaign_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_fixed_time_artifact_strength_campaign_smoke.py
      --exe $<TARGET_FILE:vibe_othello_full_game_artifact_arena>
      --runner ${CMAKE_CURRENT_SOURCE_DIR}/run_fixed_time_artifact_strength_campaign.py
      --schema-checker
      ${CMAKE_CURRENT_SOURCE_DIR}/check_fixed_time_artifact_strength_decision.py
      --artifact-fixture ${CMAKE_CURRENT_SOURCE_DIR}/check_full_game_artifact_arena_smoke.py)
endif()
