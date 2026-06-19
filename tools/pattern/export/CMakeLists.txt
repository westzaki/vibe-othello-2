add_executable(vibe_othello_pattern_export_smoke main.cc)
add_executable(vibe_othello_pattern_evaluation_roundtrip_smoke roundtrip_smoke.cc)
add_executable(vibe_othello_pattern_evaluation_bench_smoke evaluation_bench_smoke.cc)
add_executable(vibe_othello_pattern_search_bench_smoke search_bench_smoke.cc)

target_link_libraries(vibe_othello_pattern_export_smoke PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_evaluation_roundtrip_smoke
                      PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_evaluation_bench_smoke PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_search_bench_smoke PRIVATE vibe_othello::engine)

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
endif()
