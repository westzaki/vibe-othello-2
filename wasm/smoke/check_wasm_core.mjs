import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
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

const manifestText = await readFile(
  new URL(
    "../../data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/manifest.json",
    import.meta.url,
  ),
  "utf8",
);
const weightsBytes = await readFile(
  new URL(
    "../../data/eval/artifacts/pattern-v2-endgame-lite-100k-mt-v0/weights.bin",
    import.meta.url,
  ),
);

const hasLegalMoveBit = (legalMoves, squareIndex) =>
  ((legalMoves >> BigInt(squareIndex)) & 1n) !== 0n;

const artifact = core.loadEvaluationArtifact(manifestText, weightsBytes);
assert.equal(typeof artifact.evaluatePosition(initialPosition), "number");
assert.equal(artifact.evaluatePosition(initialPosition), 0);

const searchResult = artifact.searchBestMove(initialPosition, { maxDepth: 1 });
assert.equal(searchResult.hasBestMove, true);
assert.equal(searchResult.isPass, false);
assert.equal(typeof searchResult.bestMoveSquare, "number");
assert.ok(searchResult.bestMoveSquare >= 0);
assert.ok(searchResult.bestMoveSquare < 64);
assert.equal(hasLegalMoveBit(initialQuery.legalMoves, searchResult.bestMoveSquare), true);
assert.equal(typeof searchResult.score, "number");
assert.notEqual(searchResult.score, 0);
assert.equal(searchResult.completedDepth, 1);
assert.equal(typeof searchResult.nodes, "bigint");
assert.ok(searchResult.nodes > 0n);
assert.equal(typeof searchResult.elapsedMs, "number");
assert.equal(typeof searchResult.stopped, "boolean");
assert.equal(typeof searchResult.exact, "boolean");

const normalSearchResult = artifact.searchBestMoveWithPreset(
  initialPosition,
  { maxDepth: 3 },
  "normal",
  0,
);
assert.equal(normalSearchResult.hasBestMove, true);
assert.equal(normalSearchResult.isPass, false);
assert.equal(typeof normalSearchResult.bestMoveSquare, "number");
assert.equal(hasLegalMoveBit(initialQuery.legalMoves, normalSearchResult.bestMoveSquare), true);
assert.equal(normalSearchResult.completedDepth, 3);
assert.ok(normalSearchResult.completedDepth > 2);
assert.equal(typeof normalSearchResult.nodes, "bigint");
assert.ok(normalSearchResult.nodes > 0n);
assert.throws(
  () => artifact.searchBestMoveWithPreset(initialPosition, { maxDepth: 1 }, "invalid"),
  /invalid search preset/,
);
assert.throws(
  () => artifact.searchBestMoveWithPreset(initialPosition, { maxDepth: 1 }, "normal", 64),
  /requires maxNodes or maxTimeMs/,
);

artifact.free();
artifact.free();
assert.throws(() => artifact.evaluatePosition(initialPosition), /freed/);
