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
      --trainer
      ${CMAKE_CURRENT_SOURCE_DIR}/train_pattern.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --normalized-tsv
      ${CMAKE_SOURCE_DIR}/tools/pattern/synthetic-normalized-v2-smoke.tsv
      --records
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.records.tsv
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json)

  add_test(
    NAME vibe_othello_pattern_train_golden_byte_equality
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_golden_byte_equality.py
      --trainer
      ${CMAKE_CURRENT_SOURCE_DIR}/train_pattern.py)

  add_test(
    NAME vibe_othello_pattern_rank_train_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_rank_smoke.py
      --trainer
      ${CMAKE_CURRENT_SOURCE_DIR}/train_pattern.py
      --exporter
      ${CMAKE_SOURCE_DIR}/tools/pattern/export/export_v0b.py
      --catalog-dump
      $<TARGET_FILE:vibe_othello_pattern_catalog_dump>
      --roundtrip
      $<TARGET_FILE:vibe_othello_pattern_evaluation_roundtrip_smoke>)

  add_test(
    NAME vibe_othello_pattern_growth_cycle_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_pattern_growth_cycle_smoke.py
      --growth-runner
      ${CMAKE_CURRENT_SOURCE_DIR}/run_pattern_growth_cycle.py)
endif()
