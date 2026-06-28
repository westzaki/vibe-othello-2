function(vibe_othello_add_web_wasm_asset_copy_target)
  set(options)
  set(one_value_args MODULE_TARGET DESTINATION_DIR)
  set(multi_value_args)
  cmake_parse_arguments(
    VIBE_OTHELLO_WEB_WASM_ASSETS
    "${options}"
    "${one_value_args}"
    "${multi_value_args}"
    ${ARGN})

  if(VIBE_OTHELLO_WEB_WASM_ASSETS_UNPARSED_ARGUMENTS)
    message(
      FATAL_ERROR
        "vibe_othello_add_web_wasm_asset_copy_target received unexpected arguments: "
        "${VIBE_OTHELLO_WEB_WASM_ASSETS_UNPARSED_ARGUMENTS}")
  endif()

  if(NOT VIBE_OTHELLO_WEB_WASM_ASSETS_MODULE_TARGET)
    set(VIBE_OTHELLO_WEB_WASM_ASSETS_MODULE_TARGET vibe_othello_wasm_module)
  endif()

  if(NOT VIBE_OTHELLO_WEB_WASM_ASSETS_DESTINATION_DIR)
    set(VIBE_OTHELLO_WEB_WASM_ASSETS_DESTINATION_DIR
        "${PROJECT_SOURCE_DIR}/apps/web/public/wasm")
  endif()

  if(NOT TARGET "${VIBE_OTHELLO_WEB_WASM_ASSETS_MODULE_TARGET}")
    message(
      FATAL_ERROR
        "vibe_othello_add_web_wasm_asset_copy_target requires target "
        "'${VIBE_OTHELLO_WEB_WASM_ASSETS_MODULE_TARGET}'")
  endif()

  if(TARGET vibe_othello_copy_web_wasm_assets)
    message(FATAL_ERROR "Target 'vibe_othello_copy_web_wasm_assets' already exists")
  endif()

  add_custom_target(
    vibe_othello_copy_web_wasm_assets
    COMMAND
      ${CMAKE_COMMAND} -E make_directory
      "${VIBE_OTHELLO_WEB_WASM_ASSETS_DESTINATION_DIR}"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:${VIBE_OTHELLO_WEB_WASM_ASSETS_MODULE_TARGET}>"
      "${VIBE_OTHELLO_WEB_WASM_ASSETS_DESTINATION_DIR}/vibe_othello_wasm_module.mjs"
    COMMAND
      ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE_DIR:${VIBE_OTHELLO_WEB_WASM_ASSETS_MODULE_TARGET}>/$<TARGET_FILE_BASE_NAME:${VIBE_OTHELLO_WEB_WASM_ASSETS_MODULE_TARGET}>.wasm"
      "${VIBE_OTHELLO_WEB_WASM_ASSETS_DESTINATION_DIR}/vibe_othello_wasm_module.wasm"
    DEPENDS "${VIBE_OTHELLO_WEB_WASM_ASSETS_MODULE_TARGET}"
    COMMENT "Copying generated WASM runtime assets into apps/web/public/wasm")
endfunction()
