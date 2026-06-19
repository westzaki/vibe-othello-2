add_library(vibe_othello_data_import_replay_records STATIC replay_records.cc)

target_link_libraries(vibe_othello_data_import_replay_records PUBLIC vibe_othello::engine)

target_include_directories(vibe_othello_data_import_replay_records PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(vibe_othello_data_import_replay_smoke main.cc)

target_link_libraries(vibe_othello_data_import_replay_smoke PRIVATE
                      vibe_othello_data_import_replay_records)

set_target_properties(vibe_othello_data_import_replay_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-data-import-replay-smoke)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_data_import_replay_smoke
    COMMAND
      vibe_othello_data_import_replay_smoke
      --records
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.records.tsv
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json)
  set_tests_properties(
    vibe_othello_data_import_replay_smoke
    PROPERTIES PASS_REGULAR_EXPRESSION
               "summary total_records=6 accepted_records=2 rejected_records=4")

  add_test(
    NAME vibe_othello_data_import_egaroucid_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_egaroucid_smoke.py
      --importer
      ${CMAKE_CURRENT_SOURCE_DIR}/import_egaroucid_train_data.py
      --fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-board-score.txt
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-train-data-board-score-v2025-02-02.manifest.json)

  add_test(
    NAME vibe_othello_egaroucid_sequence_import_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_egaroucid_sequence_smoke.py
      --importer
      ${CMAKE_CURRENT_SOURCE_DIR}/import_egaroucid_sequences.py
      --fixture
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-sequences.txt
      --manifest
      ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>)
endif()
