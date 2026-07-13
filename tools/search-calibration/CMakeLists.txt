if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_shadow_calibration_analyzer_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_analyze_shadow_samples.py
            --analyzer ${CMAKE_CURRENT_SOURCE_DIR}/analyze_shadow_samples.py)
  add_test(
    NAME vibe_othello_probcut_profile_converter_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_convert_probcut_profile.py
            --converter ${CMAKE_CURRENT_SOURCE_DIR}/convert_probcut_profile.py)
  add_test(
    NAME vibe_othello_multi_probcut_pipeline_e2e
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/test_multi_probcut_pipeline_e2e.py
      --analyzer ${CMAKE_CURRENT_SOURCE_DIR}/analyze_shadow_samples.py
      --converter ${CMAKE_CURRENT_SOURCE_DIR}/convert_probcut_profile.py
      --arena $<TARGET_FILE:vibe_othello_full_game_artifact_arena>
      --artifact-fixture
      ${PROJECT_SOURCE_DIR}/tools/arena/check_full_game_artifact_arena_smoke.py)
endif()
