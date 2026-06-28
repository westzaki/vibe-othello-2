function(vibe_othello_add_web_eval_artifact_copy_target)
  set(options)
  set(one_value_args SOURCE_DIR DESTINATION_DIR)
  set(multi_value_args)
  cmake_parse_arguments(
    VIBE_OTHELLO_WEB_EVAL_ASSETS
    "${options}"
    "${one_value_args}"
    "${multi_value_args}"
    ${ARGN})

  if(VIBE_OTHELLO_WEB_EVAL_ASSETS_UNPARSED_ARGUMENTS)
    message(
      FATAL_ERROR
        "vibe_othello_add_web_eval_artifact_copy_target received unexpected arguments: "
        "${VIBE_OTHELLO_WEB_EVAL_ASSETS_UNPARSED_ARGUMENTS}")
  endif()

  if(NOT VIBE_OTHELLO_WEB_EVAL_ASSETS_SOURCE_DIR)
    set(VIBE_OTHELLO_WEB_EVAL_ASSETS_SOURCE_DIR
        "${PROJECT_SOURCE_DIR}/data/eval")
  endif()

  if(NOT VIBE_OTHELLO_WEB_EVAL_ASSETS_DESTINATION_DIR)
    set(VIBE_OTHELLO_WEB_EVAL_ASSETS_DESTINATION_DIR
        "${PROJECT_SOURCE_DIR}/apps/web/public/eval")
  endif()

  if(TARGET vibe_othello_copy_web_eval_artifact_assets)
    message(FATAL_ERROR "Target 'vibe_othello_copy_web_eval_artifact_assets' already exists")
  endif()

  find_package(Python3 QUIET COMPONENTS Interpreter)

  if(NOT Python3_Interpreter_FOUND)
    add_custom_target(
      vibe_othello_copy_web_eval_artifact_assets
      COMMAND
        ${CMAKE_COMMAND} -E echo
        "Python3 interpreter is required to copy Web eval artifact assets"
      COMMAND ${CMAKE_COMMAND} -E false
      COMMENT "Copying default evaluation artifact into apps/web/public/eval"
      VERBATIM)
    return()
  endif()

  add_custom_target(
    vibe_othello_copy_web_eval_artifact_assets
    COMMAND
      "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/cmake/web/copy_default_eval_artifact.py"
      --source-dir
      "${VIBE_OTHELLO_WEB_EVAL_ASSETS_SOURCE_DIR}"
      --destination-dir
      "${VIBE_OTHELLO_WEB_EVAL_ASSETS_DESTINATION_DIR}"
    DEPENDS "${PROJECT_SOURCE_DIR}/cmake/web/copy_default_eval_artifact.py"
    COMMENT "Copying default evaluation artifact into apps/web/public/eval"
    VERBATIM)
endfunction()
