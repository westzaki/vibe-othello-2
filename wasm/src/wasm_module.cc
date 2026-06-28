// Translation unit for the opt-in Emscripten module target.
//
// The exported C ABI lives in wasm_api.cc through vibe_othello::wasm_adapter.
// This file gives CMake an executable source while --no-entry keeps the module
// as a callable library.
namespace vibe_othello_wasm {
namespace {

[[maybe_unused]] constexpr int kModuleTargetAnchor = 0;

} // namespace
} // namespace vibe_othello_wasm
