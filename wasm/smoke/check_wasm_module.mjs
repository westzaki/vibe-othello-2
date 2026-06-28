import assert from "node:assert/strict";
import { pathToFileURL } from "node:url";

const modulePath = process.argv[2];
if (!modulePath) {
  throw new Error("usage: node check_wasm_module.mjs <generated-module.mjs>");
}

const imported = await import(pathToFileURL(modulePath).href);
const createModule = imported.default ?? imported;
const module = await createModule();

const statusOk = 0;
const conservativeStructBytes = 128;
const legalInitialMove = 26;

assert.equal(module._vibe_othello_wasm_abi_version(), 1);

const positionPtr = module._malloc(conservativeStructBytes);
const queryPtr = module._malloc(conservativeStructBytes);
const resultPtr = module._malloc(conservativeStructBytes);

try {
  assert.notEqual(positionPtr, 0);
  assert.notEqual(queryPtr, 0);
  assert.notEqual(resultPtr, 0);

  assert.equal(module._vibe_othello_wasm_initial_position(positionPtr), statusOk);
  assert.equal(module._vibe_othello_wasm_query_position(positionPtr, queryPtr), statusOk);
  assert.equal(
    module._vibe_othello_wasm_apply_move(positionPtr, legalInitialMove, resultPtr),
    statusOk,
  );
} finally {
  module._free(resultPtr);
  module._free(queryPtr);
  module._free(positionPtr);
}
