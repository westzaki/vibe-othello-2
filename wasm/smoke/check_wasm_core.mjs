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

const parsePosition = (text) => {
  const [board, sideToMove] = text.split(" ");
  const rows = board.split("/");
  let black = 0n;
  let white = 0n;
  for (let row = 0; row < rows.length; ++row) {
    const rank = 7 - row;
    for (let file = 0; file < rows[row].length; ++file) {
      const mask = 1n << BigInt(rank * 8 + file);
      if (rows[row][file] === "B") black |= mask;
      if (rows[row][file] === "W") white |= mask;
    }
  }
  const blackToMove = sideToMove === "b";
  return {
    player: blackToMove ? black : white,
    opponent: blackToMove ? white : black,
    sideToMove: blackToMove ? "black" : "white",
  };
};

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
assert.equal(searchResult.probcutEnabled, false);

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
assert.equal(normalSearchResult.probcutEnabled, false);
assert.throws(
  () => artifact.searchBestMoveWithPreset(initialPosition, { maxDepth: 1 }, "invalid"),
  /invalid search preset/,
);
assert.throws(
  () => artifact.searchBestMoveWithPreset(initialPosition, { maxDepth: 1 }, "normal", 64),
  /requires maxNodes or maxTimeMs/,
);

// Native Release parity fixture: this covered late phase exercises the
// incremental search backend in the generated Emscripten module.
const latePosition = parsePosition(
  "WWWWWWW./.WWWBW../BBWBWW../BWWWWWB./BWBBWBBB/BBBWB.B./BBWBBB../BW.WWWWW b",
);
assert.equal(artifact.evaluatePosition(latePosition), -8);
const lateSearch = artifact.searchBestMove(latePosition, { maxDepth: 3 });
assert.equal(lateSearch.score, 4);
assert.equal(lateSearch.completedDepth, 3);
assert.equal(lateSearch.bestMoveSquare, 48);
assert.equal(lateSearch.nodes, 152n);

const productionManifestText = await readFile(
  new URL(
    "../../data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/manifest.json",
    import.meta.url,
  ),
  "utf8",
);
const productionWeightsBytes = await readFile(
  new URL(
    "../../data/eval/artifacts/pattern-v2-egaroucid-lv17-full-value-v1/weights.bin",
    import.meta.url,
  ),
);
const productionArtifact = core.loadEvaluationArtifact(
  productionManifestText,
  productionWeightsBytes,
);
const productionSearch = productionArtifact.searchBestMoveWithPreset(
  initialPosition,
  { maxDepth: 8, maxNodes: 1000 },
  "normal",
  8,
);
assert.equal(productionSearch.probcutEnabled, true);
const widerRootSearch = productionArtifact.searchBestMoveWithPreset(
  initialPosition,
  { maxDepth: 8, maxNodes: 1000 },
  "normal",
  14,
);
assert.equal(widerRootSearch.probcutEnabled, true);
const widerExactRootSearch = productionArtifact.searchBestMoveWithPreset(
  latePosition,
  { maxDepth: 8, maxNodes: 1000 },
  "normal",
  14,
);
assert.equal(widerExactRootSearch.probcutEnabled, false);
productionArtifact.free();

artifact.free();
artifact.free();
assert.throws(() => artifact.evaluatePosition(initialPosition), /freed/);
