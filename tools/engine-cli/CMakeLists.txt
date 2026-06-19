add_executable(vibe_othello_engine_cli main.cc)

target_link_libraries(vibe_othello_engine_cli PRIVATE vibe_othello::engine)

set_target_properties(vibe_othello_engine_cli PROPERTIES OUTPUT_NAME vibe-othello-engine-cli)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_engine_cli_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py --exe
            $<TARGET_FILE:vibe_othello_engine_cli>)
endif()
