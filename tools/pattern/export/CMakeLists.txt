add_executable(vibe_othello_pattern_export_smoke main.cc)
add_executable(vibe_othello_pattern_evaluation_roundtrip_smoke roundtrip_smoke.cc)
add_executable(vibe_othello_pattern_evaluation_bench_smoke evaluation_bench_smoke.cc)
add_executable(vibe_othello_pattern_search_bench_smoke search_bench_smoke.cc)

target_link_libraries(vibe_othello_pattern_export_smoke PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_evaluation_roundtrip_smoke
                      PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_evaluation_bench_smoke
                      PRIVATE vibe_othello_pattern_set_options)
target_link_libraries(vibe_othello_pattern_search_bench_smoke
                      PRIVATE vibe_othello_pattern_set_options)

set_target_properties(vibe_othello_pattern_export_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-export-smoke)
set_target_properties(vibe_othello_pattern_evaluation_roundtrip_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-evaluation-roundtrip-smoke)
set_target_properties(vibe_othello_pattern_evaluation_bench_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-evaluation-bench-smoke)
set_target_properties(vibe_othello_pattern_search_bench_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-search-bench-smoke)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_pattern_export_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py
      --exporter-exe
      $<TARGET_FILE:vibe_othello_pattern_export_smoke>
      --v0b-exporter
      ${CMAKE_CURRENT_SOURCE_DIR}/export_v0b.py
      --roundtrip-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_roundtrip_smoke>
      --trainer-exe
      $<TARGET_FILE:vibe_othello_pattern_train_smoke>
      --trainer-v0a
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_v0a.py
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
    NAME vibe_othello_pattern_evaluation_roundtrip_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_smoke.py
      --exporter-exe
      $<TARGET_FILE:vibe_othello_pattern_export_smoke>
      --v0b-exporter
      ${CMAKE_CURRENT_SOURCE_DIR}/export_v0b.py
      --roundtrip-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_roundtrip_smoke>
      --trainer-exe
      $<TARGET_FILE:vibe_othello_pattern_train_smoke>
      --trainer-v0a
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_v0a.py
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
    NAME vibe_othello_pattern_evaluation_bench_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_evaluation_bench_smoke.py
      --bench-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_bench_smoke>
      --v0a-exporter
      ${CMAKE_CURRENT_SOURCE_DIR}/export_v0a.py
      --v0b-exporter
      ${CMAKE_CURRENT_SOURCE_DIR}/export_v0b.py
      --trainer-v0a
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_v0a.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --egaroucid-importer
      ${CMAKE_SOURCE_DIR}/tools/data-import/import_egaroucid_train_data.py
      --egaroucid-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-board-score.txt
      --egaroucid-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json)

  add_test(
    NAME vibe_othello_pattern_search_bench_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_search_bench_smoke.py
      --bench-exe
      $<TARGET_FILE:vibe_othello_pattern_search_bench_smoke>
      --v0a-exporter
      ${CMAKE_CURRENT_SOURCE_DIR}/export_v0a.py
      --v0b-exporter
      ${CMAKE_CURRENT_SOURCE_DIR}/export_v0b.py
      --trainer-v0a
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_v0a.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --egaroucid-importer
      ${CMAKE_SOURCE_DIR}/tools/data-import/import_egaroucid_train_data.py
      --egaroucid-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-board-score.txt
      --egaroucid-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json)

  add_test(
    NAME vibe_othello_pattern_local_training_runner_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/check_local_training_runner_smoke.py
      --runner
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/run_egaroucid_local_training.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --eval-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_bench_smoke>
      --search-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_search_bench_smoke>
      --egaroucid-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-board-score.txt
      --egaroucid-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json)

  add_test(
    NAME vibe_othello_pattern_local_training_buro_lite_runner_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/check_local_training_runner_smoke.py
      --runner
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/run_egaroucid_local_training.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --eval-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_bench_smoke>
      --search-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_search_bench_smoke>
      --egaroucid-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-board-score.txt
      --egaroucid-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json
      --pattern-set
      pattern-v1-buro-lite
      --expected-pattern-set-id
      pattern-v1-buro-lite
      --learning-rate
      0.8)

  add_test(
    NAME vibe_othello_pattern_local_training_v0c_compact_runner_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/check_local_training_runner_smoke.py
      --runner
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/run_egaroucid_local_training.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --eval-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_bench_smoke>
      --search-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_search_bench_smoke>
      --egaroucid-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-board-score.txt
      --egaroucid-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json
      --trainer-mode
      pattern-sgd-v0c
      --dataset-output-format
      compact-tsv
      --learning-rate
      0.9)

  add_test(
    NAME vibe_othello_pattern_local_training_v0d_compact_runner_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/check_local_training_runner_smoke.py
      --runner
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/run_egaroucid_local_training.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --eval-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_bench_smoke>
      --search-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_search_bench_smoke>
      --egaroucid-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-board-score.txt
      --egaroucid-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json
      --trainer-mode
      pattern-sgd-v0d
      --dataset-output-format
      compact-tsv
      --phase-balance
      none
      --learning-rate
      0.9)

  add_test(
    NAME vibe_othello_pattern_local_training_sequence_runner_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/check_local_training_sequence_runner_smoke.py
      --runner
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/run_egaroucid_local_training.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --eval-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_bench_smoke>
      --search-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_search_bench_smoke>
      --sequence-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-sequences.txt
      --sequence-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json)

  add_test(
    NAME vibe_othello_pattern_measurement_suite_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/check_pattern_measurement_suite_smoke.py
      --suite-runner
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/run_pattern_measurement_suite.py
      --runner
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/run_egaroucid_local_training.py
      --analyzer
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/analyze_local_training_runs.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --eval-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_evaluation_bench_smoke>
      --search-smoke-exe
      $<TARGET_FILE:vibe_othello_pattern_search_bench_smoke>
      --sequence-fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-sequences.txt
      --sequence-manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json)
endif()
