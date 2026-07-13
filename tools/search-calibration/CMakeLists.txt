if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_shadow_calibration_analyzer_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_analyze_shadow_samples.py
            --analyzer ${CMAKE_CURRENT_SOURCE_DIR}/analyze_shadow_samples.py)
endif()
