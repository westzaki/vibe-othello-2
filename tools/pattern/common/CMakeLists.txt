add_library(vibe_othello_pattern_index_mode STATIC index_mode.cc)
add_library(vibe_othello_pattern_normalized_tsv STATIC normalized_tsv.cc)
add_library(vibe_othello_pattern_set_options STATIC pattern_set_options.cc)
add_library(vibe_othello_pattern_schema_validation STATIC schema_validation.cc)
add_library(vibe_othello_pattern_smoke_fixture STATIC smoke_fixture.cc)

target_link_libraries(vibe_othello_pattern_index_mode PUBLIC vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_normalized_tsv PUBLIC vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_set_options PUBLIC vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_schema_validation PUBLIC vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_smoke_fixture
                      PUBLIC vibe_othello::engine)

target_include_directories(vibe_othello_pattern_index_mode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vibe_othello_pattern_normalized_tsv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vibe_othello_pattern_set_options PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vibe_othello_pattern_schema_validation
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(vibe_othello_pattern_smoke_fixture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  if(TARGET Catch2::Catch2WithMain)
    add_executable(vibe_othello_pattern_normalized_tsv_tests normalized_tsv_test.cc)
    target_link_libraries(vibe_othello_pattern_normalized_tsv_tests
                          PRIVATE vibe_othello_pattern_normalized_tsv Catch2::Catch2WithMain)
    add_test(NAME vibe_othello_pattern_normalized_tsv_tests
             COMMAND vibe_othello_pattern_normalized_tsv_tests)
  endif()

  add_test(
    NAME vibe_othello_pattern_common_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_boundary.py --common-dir
            ${CMAKE_CURRENT_SOURCE_DIR})
endif()
