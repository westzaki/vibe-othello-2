cmake_minimum_required(VERSION 3.20)

project(
  vibe_othello_2
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VIBE_OTHELLO_BUILD_BENCHMARKS "Build local benchmark executables" OFF)
option(VIBE_OTHELLO_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(VIBE_OTHELLO_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  else()
    message(FATAL_ERROR "VIBE_OTHELLO_ENABLE_SANITIZERS requires Clang or GCC")
  endif()
endif()

include(CTest)

add_subdirectory(engine)
if(VIBE_OTHELLO_BUILD_BENCHMARKS)
  add_subdirectory(engine/benchmarks)
endif()
if(BUILD_TESTING AND NOT EMSCRIPTEN)
  add_subdirectory(engine/tests)
elseif(BUILD_TESTING AND EMSCRIPTEN)
  message(STATUS "Skipping native engine tests for Emscripten build")
endif()
add_subdirectory(wasm)
if(TARGET vibe_othello_wasm_module)
  include("${PROJECT_SOURCE_DIR}/cmake/web/WasmAssets.cmake")
  vibe_othello_add_web_wasm_asset_copy_target()
endif()
if(NOT EMSCRIPTEN)
  add_subdirectory(tools)
else()
  message(STATUS "Skipping native tools for Emscripten build")
endif()
