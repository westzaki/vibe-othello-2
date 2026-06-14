add_executable(vibe_othello_engine_cli main.cc)

target_link_libraries(vibe_othello_engine_cli PRIVATE vibe_othello::engine)

set_target_properties(vibe_othello_engine_cli PROPERTIES OUTPUT_NAME vibe-othello-engine-cli)
