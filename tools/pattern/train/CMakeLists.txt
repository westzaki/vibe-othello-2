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
      --trainer-v0a
      ${CMAKE_CURRENT_SOURCE_DIR}/train_v0a.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --egaroucid-importer
      ${CMAKE_SOURCE_DIR}/tools/data-import/import_egaroucid_train_data.py
      --egaroucid-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-board-score.txt
      --egaroucid-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json
      --records
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.records.tsv
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json)

  add_test(
    NAME vibe_othello_pattern_local_training_analyzer_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_local_training_analyzer_smoke.py
      --analyzer
      ${CMAKE_CURRENT_SOURCE_DIR}/analyze_local_training_runs.py
      --source-dir
      ${CMAKE_SOURCE_DIR})

  add_test(
    NAME vibe_othello_pattern_trainer_sweep_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_pattern_trainer_sweep_smoke.py
      --sweep-runner
      ${CMAKE_CURRENT_SOURCE_DIR}/run_pattern_trainer_sweep.py
      --trainer
      ${CMAKE_CURRENT_SOURCE_DIR}/train_v0a.py)
endif()
