add_executable(vibe_othello_pattern_export_smoke main.cc)
add_executable(vibe_othello_pattern_evaluation_roundtrip_smoke roundtrip_smoke.cc)

target_link_libraries(vibe_othello_pattern_export_smoke PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_evaluation_roundtrip_smoke
                      PRIVATE vibe_othello::engine)

set_target_properties(vibe_othello_pattern_export_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-export-smoke)
set_target_properties(vibe_othello_pattern_evaluation_roundtrip_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-evaluation-roundtrip-smoke)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_pattern_export_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py
      --exporter-exe
      $<TARGET_FILE:vibe_othello_pattern_export_smoke>
      --roundtrip-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_roundtrip_smoke>
      --trainer-exe
      $<TARGET_FILE:vibe_othello_pattern_train_smoke>
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --records
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.records.tsv
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json)

  add_test(
    NAME vibe_othello_pattern_evaluation_roundtrip_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py
      --exporter-exe
      $<TARGET_FILE:vibe_othello_pattern_export_smoke>
      --roundtrip-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_roundtrip_smoke>
      --trainer-exe
      $<TARGET_FILE:vibe_othello_pattern_train_smoke>
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --records
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.records.tsv
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json)
endif()
