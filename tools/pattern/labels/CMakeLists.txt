if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_pattern_teacher_label_overlay_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_teacher_label_overlay_smoke.py
      --overlay
      ${CMAKE_CURRENT_SOURCE_DIR}/apply_teacher_labels.py)
endif()
