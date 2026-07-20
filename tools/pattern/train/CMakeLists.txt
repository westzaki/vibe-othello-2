add_executable(vibe_othello_pattern_train_smoke main.cc)
add_executable(vibe_othello_egaroucid_board_score_pretrain
               egaroucid_board_score_pretrain.cc)
add_executable(vibe_othello_wthor_policy_pretrain wthor_policy_pretrain.cc)

target_link_libraries(vibe_othello_egaroucid_board_score_pretrain
                      PRIVATE vibe_othello::engine)
target_link_libraries(vibe_othello_wthor_policy_pretrain
                      PRIVATE vibe_othello::engine)

set_target_properties(vibe_othello_pattern_train_smoke
                      PROPERTIES OUTPUT_NAME vibe-othello-pattern-train-smoke)
set_target_properties(vibe_othello_egaroucid_board_score_pretrain
                      PROPERTIES OUTPUT_NAME
                                 vibe-othello-egaroucid-board-score-pretrain)
set_target_properties(vibe_othello_wthor_policy_pretrain
                      PROPERTIES OUTPUT_NAME vibe-othello-wthor-policy-pretrain)

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
    NAME vibe_othello_wthor_policy_pretrain_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_wthor_policy_pretrain_smoke.py
      --trainer
      $<TARGET_FILE:vibe_othello_wthor_policy_pretrain>
      --artifact-manifest
      ${CMAKE_SOURCE_DIR}/data/eval/artifacts/pattern-v2-progressive-search-d5-fast6-p5-v1/manifest.json)

  add_test(
    NAME vibe_othello_egaroucid_board_score_pretrain_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_egaroucid_board_score_pretrain_smoke.py
      --trainer
      $<TARGET_FILE:vibe_othello_egaroucid_board_score_pretrain>
      --artifact-manifest
      ${CMAKE_SOURCE_DIR}/data/eval/artifacts/pattern-v2-progressive-search-d5-fast6-p5-v1/manifest.json)

  add_test(
    NAME vibe_othello_pattern_growth_cycle_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_pattern_growth_cycle_smoke.py
      --growth-runner
      ${CMAKE_CURRENT_SOURCE_DIR}/run_pattern_growth_cycle.py)

  add_test(
    NAME vibe_othello_egaroucid_board_score_training_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_egaroucid_board_score_training_smoke.py
      --runner
      ${CMAKE_CURRENT_SOURCE_DIR}/run_egaroucid_board_score_training.py
      --importer
      ${CMAKE_SOURCE_DIR}/tools/data-import/import_egaroucid_board_scores.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --trainer
      ${CMAKE_CURRENT_SOURCE_DIR}/train_pattern.py)

  add_test(
    NAME vibe_othello_full_phase_pattern_growth_cycle_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_full_phase_growth_cycle_smoke.py
      --runner ${CMAKE_CURRENT_SOURCE_DIR}/run_full_phase_growth_cycle.py
      --importer ${CMAKE_SOURCE_DIR}/tools/data-import/import_egaroucid_sequences.py
      --replay-helper $<TARGET_FILE:vibe_othello_egaroucid_sequence_replay>
      --sequence-fixture ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-egaroucid-sequences.txt
      --sequence-manifest ${CMAKE_SOURCE_DIR}/data/corpora/manifests/egaroucid-sequence-v0002-local.manifest.json
      --openings ${CMAKE_SOURCE_DIR}/tools/arena/openings/smoke.txt
      --selector ${CMAKE_SOURCE_DIR}/tools/pattern/labels/select_phase_stratified_roots.py
      --search-teacher-runner ${CMAKE_SOURCE_DIR}/tools/pattern/labels/run_search_move_teacher_generation.py
      --search-generator $<TARGET_FILE:vibe_othello_generate_search_move_teacher_dataset>
      --exact-generator $<TARGET_FILE:vibe_othello_generate_exact_move_teacher_dataset>
      --dataset-exe $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --trainer ${CMAKE_CURRENT_SOURCE_DIR}/train_pattern.py
      --exporter ${CMAKE_SOURCE_DIR}/tools/pattern/export/export_v0b.py
      --catalog-dump $<TARGET_FILE:vibe_othello_pattern_catalog_dump>
      --ranking-evaluator $<TARGET_FILE:vibe_othello_evaluate_move_teacher_ranking>
      --late-arena-runner ${CMAKE_SOURCE_DIR}/tools/arena/run_pattern_artifact_arena_matrix.py
      --late-arena-exe $<TARGET_FILE:vibe_othello_pattern_artifact_arena>
      --full-arena-exe $<TARGET_FILE:vibe_othello_full_game_artifact_arena>)
endif()
