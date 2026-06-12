include(FetchContent)

set(CATCH_INSTALL_DOCS
    OFF
    CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS
    OFF
    CACHE BOOL "" FORCE)
set(CATCH_BUILD_TESTING
    OFF
    CACHE BOOL "" FORCE)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.15.0)
FetchContent_MakeAvailable(Catch2)

add_executable(vibe_othello_engine_tests board_core/coordinates_test.cc
                                         board_core/position_test.cc)

target_link_libraries(vibe_othello_engine_tests PRIVATE vibe_othello::engine
                                                        Catch2::Catch2WithMain)

target_compile_features(vibe_othello_engine_tests PRIVATE cxx_std_20)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(vibe_othello_engine_tests)
