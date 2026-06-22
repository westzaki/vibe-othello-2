if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  add_test(
    NAME vibe_othello_eval_artifact_commit_policy
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_eval_artifact_commit_policy.py
      --repo-root
      ${CMAKE_SOURCE_DIR})

  add_test(
    NAME vibe_othello_eval_artifact_commit_policy_smoke
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/check_eval_artifact_commit_policy_smoke.py
      --checker
      ${CMAKE_CURRENT_SOURCE_DIR}/check_eval_artifact_commit_policy.py)
endif()
