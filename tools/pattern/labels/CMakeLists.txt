add_executable(vibe_othello_generate_exact_endgame_teacher_labels
               generate_exact_endgame_teacher_labels.cc)

target_link_libraries(vibe_othello_generate_exact_endgame_teacher_labels
                      PRIVATE vibe_othello::engine)

set_target_properties(vibe_othello_generate_exact_endgame_teacher_labels
                      PROPERTIES OUTPUT_NAME
                                 vibe-othello-generate-exact-endgame-teacher-labels)

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
      ${CMAKE_SOURCE_DIR}/tools/pattern/train/train_v0a.py)
endif()
