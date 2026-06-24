add_executable(vibe_othello_pattern_export_smoke main.cc)
add_executable(vibe_othello_pattern_catalog_dump catalog_dump.cc)
add_executable(vibe_othello_pattern_evaluation_roundtrip_smoke roundtrip_smoke.cc)
add_executable(vibe_othello_pattern_evaluation_bench_smoke evaluation_bench_smoke.cc)
add_executable(vibe_othello_pattern_search_bench_smoke search_bench_smoke.cc)

target_link_libraries(vibe_othello_pattern_export_smoke PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_catalog_dump
                      PRIVATE vibe_othello_pattern_index_mode
                              vibe_othello_pattern_set_options
                              vibe_othello_pattern_schema_validation)
target_link_libraries(vibe_othello_pattern_evaluation_roundtrip_smoke
                      PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_pattern_evaluation_bench_smoke
                      PRIVATE vibe_othello_pattern_set_options)
target_link_libraries(vibe_othello_pattern_search_bench_smoke
                      PRIVATE vibe_othello_pattern_set_options)

set_target_properties(vibe_othello_pattern_export_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-export-smoke)
set_target_properties(vibe_othello_pattern_catalog_dump
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-catalog-dump)
set_target_properties(vibe_othello_pattern_evaluation_roundtrip_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-evaluation-roundtrip-smoke)
set_target_properties(vibe_othello_pattern_evaluation_bench_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-evaluation-bench-smoke)
set_target_properties(vibe_othello_pattern_search_bench_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-search-bench-smoke)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_pattern_catalog_export_parity
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_pattern_catalog_parity.py
      --dump-exe
      $<TARGET_FILE:vibe_othello_pattern_catalog_dump>)

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
      --catalog-dump-exe
      $<TARGET_FILE:vibe_othello_pattern_catalog_dump>
      --trainer-exe
      $<TARGET_FILE:vibe_othello_pattern_train_smoke>
      --trainer
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_pattern.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --normalized-tsv
      ${CMAKE_SOURCE_DIR}/tools/pattern/synthetic-normalized-v2-smoke.tsv
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
      --catalog-dump-exe
      $<TARGET_FILE:vibe_othello_pattern_catalog_dump>
      --trainer
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_pattern.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --normalized-tsv
      ${CMAKE_SOURCE_DIR}/tools/pattern/synthetic-normalized-v2-smoke.tsv)

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
      --catalog-dump-exe
      $<TARGET_FILE:vibe_othello_pattern_catalog_dump>
      --trainer
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_pattern.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --normalized-tsv
      ${CMAKE_SOURCE_DIR}/tools/pattern/synthetic-normalized-v2-smoke.tsv)

endif()
