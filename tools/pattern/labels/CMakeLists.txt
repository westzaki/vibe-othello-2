add_executable(vibe_othello_generate_exact_endgame_teacher_labels
               generate_exact_endgame_teacher_labels.cc)

target_link_libraries(vibe_othello_generate_exact_endgame_teacher_labels
                      PRIVATE vibe_othello::engine)

set_target_properties(vibe_othello_generate_exact_endgame_teacher_labels
                      PROPERTIES OUTPUT_NAME
                                 vibe-othello-generate-exact-endgame-teacher-labels)

add_executable(vibe_othello_generate_exact_move_teacher_dataset
               generate_exact_move_teacher_dataset.cc)

target_link_libraries(vibe_othello_generate_exact_move_teacher_dataset
                      PRIVATE vibe_othello::engine)

set_target_properties(vibe_othello_generate_exact_move_teacher_dataset
                      PROPERTIES OUTPUT_NAME
                                 vibe-othello-generate-exact-move-teacher-dataset)

add_executable(vibe_othello_evaluate_move_teacher_ranking
               evaluate_move_teacher_ranking.cc)

target_link_libraries(vibe_othello_evaluate_move_teacher_ranking
                      PRIVATE vibe_othello_pattern_set_options)

set_target_properties(vibe_othello_evaluate_move_teacher_ranking
                      PROPERTIES OUTPUT_NAME
                                 vibe-othello-evaluate-move-teacher-ranking)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_pattern_teacher_label_overlay_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_teacher_label_overlay_smoke.py
      --overlay
      ${CMAKE_CURRENT_SOURCE_DIR}/apply_teacher_labels.py)

  add_test(
    NAME vibe_othello_pattern_exact_teacher_label_generator_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_exact_endgame_teacher_label_generator_smoke.py
      --generator
      $<TARGET_FILE:vibe_othello_generate_exact_endgame_teacher_labels>
      --overlay
      ${CMAKE_CURRENT_SOURCE_DIR}/apply_teacher_labels.py
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --trainer
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_pattern.py)

  add_test(
    NAME vibe_othello_pattern_move_teacher_decision_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_move_teacher_decision_smoke.py
      --generator
      $<TARGET_FILE:vibe_othello_generate_exact_move_teacher_dataset>
      --ranking-evaluator
      $<TARGET_FILE:vibe_othello_evaluate_move_teacher_ranking>
      --dataset-exe
      $<TARGET_FILE:vibe_othello_pattern_dataset_smoke>
      --trainer
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_pattern.py
      --exporter
      ${CMAKE_SOURCE_DIR}/tools/pattern/export/export_v0b.py
      --catalog-dump-exe
      $<TARGET_FILE:vibe_othello_pattern_catalog_dump>
      --campaign-helper
      ${CMAKE_CURRENT_SOURCE_DIR}/run_move_teacher_decision_campaign.py
      --matrix-helper
      ${CMAKE_CURRENT_SOURCE_DIR}/run_move_teacher_campaign_matrix.py)

  add_test(
    NAME vibe_othello_pattern_move_teacher_cache_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_move_teacher_cache_smoke.py
      --materializer
      ${CMAKE_CURRENT_SOURCE_DIR}/materialize_move_teacher_from_cache.py
      --generator
      $<TARGET_FILE:vibe_othello_generate_exact_move_teacher_dataset>
      --campaign-helper
      ${CMAKE_CURRENT_SOURCE_DIR}/run_move_teacher_decision_campaign.py
      --matrix-helper
      ${CMAKE_CURRENT_SOURCE_DIR}/run_move_teacher_campaign_matrix.py
      --growth-cycle-helper
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/run_pattern_growth_cycle.py)

  add_test(
    NAME vibe_othello_pattern_derive_exact_root_from_move_teacher_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_derive_exact_root_from_move_teacher_smoke.py
      --deriver
      ${CMAKE_CURRENT_SOURCE_DIR}/derive_exact_root_labels_from_move_teacher.py)

  add_test(
    NAME vibe_othello_pattern_select_low_empty_roots_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_select_low_empty_roots_smoke.py
      --selector
      ${CMAKE_CURRENT_SOURCE_DIR}/select_low_empty_roots.py)

  add_test(
    NAME vibe_othello_pattern_select_phase_stratified_roots_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_select_phase_stratified_roots_smoke.py
      --selector
      ${CMAKE_CURRENT_SOURCE_DIR}/select_phase_stratified_roots.py)
endif()
