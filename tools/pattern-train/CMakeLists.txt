add_executable(vibe_othello_pattern_train_smoke main.cc)

set_target_properties(vibe_othello_pattern_train_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-train-smoke)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_pattern_train_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py
      --trainer-exe
      $<TARGET_FILE:vibe_othello_pattern_train_smoke>
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --records
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.records.tsv
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json)
endif()
