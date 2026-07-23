add_executable(vibe_othello_collect_shadow_samples collect_shadow_samples.cc)

target_link_libraries(vibe_othello_collect_shadow_samples PRIVATE vibe_othello::engine)

set_target_properties(vibe_othello_collect_shadow_samples
                      PROPERTIES OUTPUT_NAME vibe-othello-collect-shadow-samples)

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
    NAME vibe_othello_shadow_calibration_disjoint_filter_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/test_filter_disjoint_shadow_samples.py
      --filter ${CMAKE_CURRENT_SOURCE_DIR}/filter_disjoint_shadow_samples.py)
  add_test(
    NAME vibe_othello_production_probcut_cpp_export_smoke
    COMMAND
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_export_probcut_profile_cpp.py
      --exporter ${CMAKE_CURRENT_SOURCE_DIR}/export_probcut_profile_cpp.py
      --profile
      ${PROJECT_SOURCE_DIR}/data/search/profiles/pattern-v2-egaroucid-lv17-full-value-mpc-d7-fast-v3/profile.json
      --expected ${PROJECT_SOURCE_DIR}/engine/src/search/production_probcut_profile_data.inc)
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
  add_test(
    NAME vibe_othello_shadow_calibration_collector_smoke
    COMMAND
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_collect_shadow_samples.py
      --collector $<TARGET_FILE:vibe_othello_collect_shadow_samples>
      --analyzer ${CMAKE_CURRENT_SOURCE_DIR}/analyze_shadow_samples.py
      --artifact
      ${PROJECT_SOURCE_DIR}/data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json
      --positions ${PROJECT_SOURCE_DIR}/engine/fixtures/search/positions.tsv)
endif()
