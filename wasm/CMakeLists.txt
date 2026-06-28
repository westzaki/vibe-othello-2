option(VIBE_OTHELLO_BUILD_WASM_MODULE "Build opt-in Emscripten .mjs/.wasm module" OFF)

add_library(vibe_othello_wasm_adapter STATIC)
add_library(vibe_othello::wasm_adapter ALIAS vibe_othello_wasm_adapter)

target_sources(vibe_othello_wasm_adapter PRIVATE src/wasm_api.cc)

target_include_directories(
  vibe_othello_wasm_adapter
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(vibe_othello_wasm_adapter PUBLIC vibe_othello::engine)

target_compile_features(vibe_othello_wasm_adapter PUBLIC cxx_std_20)

if(VIBE_OTHELLO_BUILD_WASM_MODULE)
  if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "VIBE_OTHELLO_BUILD_WASM_MODULE requires the Emscripten CMake toolchain")
  endif()

  add_executable(vibe_othello_wasm_module src/wasm_module.cc)
  target_link_libraries(vibe_othello_wasm_module PRIVATE vibe_othello::wasm_adapter)
  target_compile_features(vibe_othello_wasm_module PRIVATE cxx_std_20)
  set_target_properties(vibe_othello_wasm_module PROPERTIES SUFFIX ".mjs")

  set(
    VIBE_OTHELLO_WASM_MODULE_EXPORTED_FUNCTIONS
    "['_vibe_othello_wasm_abi_version','_vibe_othello_wasm_initial_position','_vibe_othello_wasm_query_position','_vibe_othello_wasm_apply_move','_vibe_othello_wasm_apply_pass','_malloc','_free']")

  target_link_options(
    vibe_othello_wasm_module
    PRIVATE
      "SHELL:--no-entry"
      "SHELL:-sMODULARIZE=1"
      "SHELL:-sEXPORT_ES6=1"
      "SHELL:-sENVIRONMENT=web,worker,node"
      "SHELL:-sEXPORTED_FUNCTIONS=${VIBE_OTHELLO_WASM_MODULE_EXPORTED_FUNCTIONS}"
      "SHELL:-Wl,-u,vibe_othello_wasm_abi_version")

  if(BUILD_TESTING)
    find_program(VIBE_OTHELLO_NODE_EXECUTABLE NAMES node nodejs)
    if(VIBE_OTHELLO_NODE_EXECUTABLE)
      add_test(
        NAME vibe_othello_wasm_module_smoke
        COMMAND
          ${VIBE_OTHELLO_NODE_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/smoke/check_wasm_module.mjs
          $<TARGET_FILE:vibe_othello_wasm_module>)
    else()
      message(STATUS "Node.js not found; skipping vibe_othello_wasm_module_smoke CTest")
    endif()
  endif()
endif()

if(BUILD_TESTING AND NOT EMSCRIPTEN)
  add_subdirectory(tests)
endif()
