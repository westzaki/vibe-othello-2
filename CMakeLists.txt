cmake_minimum_required(VERSION 3.20)

project(
  vibe_othello_2
  VERSION 0.1.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VIBE_OTHELLO_BUILD_BENCHMARKS "Build local benchmark executables" OFF)

include(CTest)

add_subdirectory(engine)
if(VIBE_OTHELLO_BUILD_BENCHMARKS)
  add_subdirectory(engine/benchmarks)
endif()
if(BUILD_TESTING)
  add_subdirectory(engine/tests)
endif()
add_subdirectory(tools)
