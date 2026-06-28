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

if(NOT TARGET Catch2::Catch2WithMain)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.0)
  FetchContent_MakeAvailable(Catch2)
endif()

FetchContent_GetProperties(Catch2)
if(catch2_SOURCE_DIR)
  set(VIBE_OTHELLO_CATCH2_EXTRAS_DIR "${catch2_SOURCE_DIR}/extras")
  if(NOT VIBE_OTHELLO_CATCH2_EXTRAS_DIR IN_LIST CMAKE_MODULE_PATH)
    list(APPEND CMAKE_MODULE_PATH "${VIBE_OTHELLO_CATCH2_EXTRAS_DIR}")
  endif()
endif()
