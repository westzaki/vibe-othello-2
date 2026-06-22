add_executable(vibe_othello_engine_cli main.cc)

target_link_libraries(vibe_othello_engine_cli PRIVATE vibe_othello::engine)

target_compile_definitions(vibe_othello_engine_cli
                           PRIVATE VIBE_OTHELLO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

set_target_properties(vibe_othello_engine_cli PROPERTIES OUTPUT_NAME vibe-othello-engine-cli)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_engine_cli_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py --exe
            $<TARGET_FILE:vibe_othello_engine_cli> --source-dir ${CMAKE_SOURCE_DIR})
endif()
