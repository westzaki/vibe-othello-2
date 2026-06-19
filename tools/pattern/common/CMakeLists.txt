add_library(vibe_othello_pattern_index_mode STATIC index_mode.cc)
add_library(vibe_othello_pattern_schema_validation STATIC schema_validation.cc)
add_library(vibe_othello_pattern_smoke_fixture STATIC smoke_fixture.cc)

target_link_libraries(vibe_othello_pattern_index_mode PUBLIC vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_schema_validation PUBLIC vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_smoke_fixture
                      PUBLIC vibe_othello::engine)

target_include_directories(vibe_othello_pattern_index_mode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vibe_othello_pattern_schema_validation
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vibe_othello_pattern_smoke_fixture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_pattern_common_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_boundary.py --common-dir
            ${CMAKE_CURRENT_SOURCE_DIR})
endif()
