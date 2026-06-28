include("${PROJECT_SOURCE_DIR}/cmake/testing/Catch2.cmake")

add_executable(vibe_othello_wasm_adapter_tests wasm_api_tests.cc)

target_link_libraries(
  vibe_othello_wasm_adapter_tests PRIVATE vibe_othello::wasm_adapter
                                          vibe_othello::engine
                                          Catch2::Catch2WithMain)

target_compile_features(vibe_othello_wasm_adapter_tests PRIVATE cxx_std_20)

target_compile_definitions(vibe_othello_wasm_adapter_tests
                           PRIVATE VIBE_OTHELLO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME vibe_othello_wasm_adapter_tests COMMAND vibe_othello_wasm_adapter_tests)
