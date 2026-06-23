add_executable(vibe_othello_pattern_dataset_smoke main.cc)

target_link_libraries(vibe_othello_pattern_dataset_smoke
                      PRIVATE vibe_othello_data_import_replay_records
                              vibe_othello_pattern_index_mode
                              vibe_othello_pattern_set_options
                              vibe_othello_pattern_schema_validation
                              vibe_othello_pattern_smoke_fixture)

set_target_properties(vibe_othello_pattern_dataset_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-dataset-smoke)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_pattern_dataset_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py
      --exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --records
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.records.tsv
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json)

  add_test(
    NAME vibe_othello_pattern_dataset_canonical_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py
      --exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --records
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.records.tsv
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json
      --index-mode
      canonical)

endif()
