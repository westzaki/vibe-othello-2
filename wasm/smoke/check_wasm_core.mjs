import assert from "node:assert/strict";
import { pathToFileURL } from "node:url";

import { WasmCore } from "../js/wasmCore.mjs";

const modulePath = process.argv[2];
if (!modulePath) {
  throw new Error("usage: node check_wasm_core.mjs <generated-module.mjs>");
}

const imported = await import(pathToFileURL(modulePath).href);
const createModule = imported.default ?? imported;
const core = await WasmCore.create(createModule);

const initialPosition = core.initialPosition();
assert.equal(typeof initialPosition.player, "bigint");
assert.equal(typeof initialPosition.opponent, "bigint");
assert.equal(initialPosition.sideToMove, "black");

const initialQuery = core.queryPosition(initialPosition);
assert.equal(typeof initialQuery.legalMoves, "bigint");
assert.notEqual(initialQuery.legalMoves, 0n);
assert.equal(initialQuery.hasLegalMove, true);
assert.equal(initialQuery.isTerminal, false);

const applyResult = core.applyMove(initialPosition, 26);
assert.equal(typeof applyResult.position.player, "bigint");
assert.equal(typeof applyResult.position.opponent, "bigint");
assert.equal(applyResult.position.sideToMove, "white");
assert.notEqual(applyResult.flipped, 0n);
assert.equal(typeof applyResult.legalMoves, "bigint");
assert.equal(typeof applyResult.hasLegalMove, "boolean");
assert.equal(typeof applyResult.isTerminal, "boolean");

assert.throws(() => core.applyPass(initialPosition), /illegal pass/);
