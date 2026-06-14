if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_dataset_manifest_sample
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/validate_dataset_manifest.py
      --schema
      ${CMAKE_SOURCE_DIR}/data/corpora/dataset-manifest.schema.json
      ${CMAKE_SOURCE_DIR}/data/corpora/samples/tiny-local-synthetic.manifest.json)
endif()
