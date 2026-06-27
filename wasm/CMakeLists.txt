add_library(vibe_othello_wasm_adapter STATIC)
add_library(vibe_othello::wasm_adapter ALIAS vibe_othello_wasm_adapter)

target_sources(vibe_othello_wasm_adapter PRIVATE src/wasm_api.cc)

target_include_directories(
  vibe_othello_wasm_adapter
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(vibe_othello_wasm_adapter PUBLIC vibe_othello::engine)

target_compile_features(vibe_othello_wasm_adapter PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
