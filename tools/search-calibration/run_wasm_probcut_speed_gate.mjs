#!/usr/bin/env node

import { createHash } from "node:crypto";
import { readFile, writeFile } from "node:fs/promises";
import { performance } from "node:perf_hooks";
import { pathToFileURL } from "node:url";
import { resolve } from "node:path";

import { WasmCore } from "../../wasm/js/wasmCore.mjs";

function fail(message) {
  throw new Error(message);
}

function parseArguments(argv) {
  const values = new Map();
  for (let index = 0; index < argv.length; index += 2) {
    const name = argv[index];
    const value = argv[index + 1];
    if (!name?.startsWith("--") || value === undefined) {
      fail(`invalid argument near ${name ?? "end of command"}`);
    }
    values.set(name, value);
  }
  const required = ["--on-module", "--off-module", "--manifest", "--weights", "--corpus"];
  for (const name of required) {
    if (!values.has(name)) fail(`${name} is required`);
  }
  const integer = (name, fallback) => {
    const parsed = Number(values.get(name) ?? fallback);
    if (!Number.isSafeInteger(parsed) || parsed <= 0) fail(`${name} must be a positive integer`);
    return parsed;
  };
  const ratio = (name, fallback) => {
    const parsed = Number(values.get(name) ?? fallback);
    if (!Number.isFinite(parsed) || parsed <= 0 || parsed > 1) {
      fail(`${name} must be in (0, 1]`);
    }
    return parsed;
  };
  const phases = (values.get("--phases") ?? "2,3,4,5,6,7,8,9,10")
    .split(",")
    .map(Number);
  if (phases.length === 0 || phases.some((phase) => !Number.isInteger(phase) || phase < 0 || phase > 12)) {
    fail("--phases must be a comma-separated list in 0..12");
  }
  return {
    onModule: values.get("--on-module"),
    offModule: values.get("--off-module"),
    manifest: values.get("--manifest"),
    weights: values.get("--weights"),
    corpus: values.get("--corpus"),
    output: values.get("--output"),
    depth: integer("--depth", 8),
    exactEndgameEmpties: integer("--exact-endgame-empties", 8),
    positionsPerPhase: integer("--positions-per-phase", 30),
    trials: integer("--trials", 3),
    maximumNodeRatio: ratio("--maximum-node-ratio", 0.99),
    maximumMedianWallRatio: ratio("--maximum-median-wall-ratio", 0.99),
    phases,
  };
}

function median(values) {
  const ordered = [...values].sort((left, right) => left - right);
  const middle = Math.floor(ordered.length / 2);
  return ordered.length % 2 === 0
    ? (ordered[middle - 1] + ordered[middle]) / 2
    : ordered[middle];
}

function positionFromBoard(board) {
  if (board.length !== 64) fail("corpus board must contain exactly 64 squares");
  let player = 0n;
  let opponent = 0n;
  for (let index = 0; index < board.length; ++index) {
    const bit = 1n << BigInt(index);
    if (board[index] === "X") player |= bit;
    if (board[index] === "O") opponent |= bit;
  }
  return { player, opponent, sideToMove: "black" };
}

async function loadPositions(path, phases, positionsPerPhase) {
  const raw = await readFile(path, "utf8");
  const lines = raw.split(/\r?\n/).filter(Boolean);
  if (lines.length < 2) fail("corpus is empty");
  const header = lines[0].split("\t");
  const required = ["position_id", "game_group_id", "board_a1_to_h8", "phase"];
  const columns = Object.fromEntries(required.map((name) => [name, header.indexOf(name)]));
  if (Object.values(columns).some((index) => index < 0)) fail("corpus header is missing a required column");

  const selectedGames = new Map(phases.map((phase) => [phase, new Set()]));
  const positions = [];
  for (const line of lines.slice(1)) {
    const fields = line.split("\t");
    const phase = Number(fields[columns.phase]);
    const games = selectedGames.get(phase);
    const game = fields[columns.game_group_id];
    if (games === undefined || games.size >= positionsPerPhase || games.has(game)) continue;
    positions.push({
      id: fields[columns.position_id],
      phase,
      position: positionFromBoard(fields[columns.board_a1_to_h8]),
    });
    games.add(game);
    if ([...selectedGames.values()].every((items) => items.size >= positionsPerPhase)) break;
  }
  const missing = [...selectedGames]
    .filter(([, games]) => games.size < positionsPerPhase)
    .map(([phase, games]) => `${phase}:${games.size}/${positionsPerPhase}`);
  if (missing.length !== 0) fail(`corpus lacks phase-balanced positions: ${missing.join(", ")}`);
  return {
    positions,
    checksum: createHash("sha256").update(raw).digest("hex"),
  };
}

async function createArtifact(modulePath, manifest, weights) {
  const imported = await import(pathToFileURL(resolve(modulePath)).href);
  const core = await WasmCore.create(imported.default ?? imported);
  return core.loadEvaluationArtifact(manifest, weights);
}

async function runVariant(label, modulePath, manifest, weights, positions, config) {
  const artifact = await createArtifact(modulePath, manifest, weights);
  try {
    artifact.searchBestMoveWithPreset(
      positions[0].position,
      { maxDepth: 4, maxNodes: 1_000_000 },
      "normal",
      config.exactEndgameEmpties,
    );
    const rows = [];
    const started = performance.now();
    for (const item of positions) {
      const result = artifact.searchBestMoveWithPreset(
        item.position,
        { maxDepth: config.depth, maxNodes: 50_000_000 },
        "normal",
        config.exactEndgameEmpties,
      );
      rows.push({ id: item.id, phase: item.phase, result });
    }
    return {
      label,
      wallMs: performance.now() - started,
      nodes: rows.reduce((sum, row) => sum + row.result.nodes, 0n),
      rows,
    };
  } finally {
    artifact.free();
  }
}

function compare(on, off) {
  if (on.rows.length !== off.rows.length) fail("variant row counts differ");
  let moveMatches = 0;
  let scoreMatches = 0;
  let depthMatches = 0;
  let enabledSelections = 0;
  let disabledSelections = 0;
  for (let index = 0; index < on.rows.length; ++index) {
    const enabled = on.rows[index];
    const disabled = off.rows[index];
    if (enabled.id !== disabled.id) fail("variant position order differs");
    moveMatches += enabled.result.bestMoveSquare === disabled.result.bestMoveSquare;
    scoreMatches += enabled.result.score === disabled.result.score;
    depthMatches += enabled.result.completedDepth === disabled.result.completedDepth;
    enabledSelections += enabled.result.probcutEnabled === true;
    disabledSelections += disabled.result.probcutEnabled === false;
  }
  return { moveMatches, scoreMatches, depthMatches, enabledSelections, disabledSelections };
}

async function main() {
  const config = parseArguments(process.argv.slice(2));
  const manifest = await readFile(config.manifest, "utf8");
  const weights = await readFile(config.weights);
  const corpus = await loadPositions(config.corpus, config.phases, config.positionsPerPhase);
  const trials = [];
  for (let trial = 0; trial < config.trials; ++trial) {
    const order = trial % 2 === 0 ? ["off", "on"] : ["on", "off"];
    const results = new Map();
    for (const label of order) {
      const modulePath = label === "on" ? config.onModule : config.offModule;
      results.set(
        label,
        await runVariant(label, modulePath, manifest, weights, corpus.positions, config),
      );
    }
    const on = results.get("on");
    const off = results.get("off");
    trials.push({
      trial: trial + 1,
      order,
      on_wall_ms: on.wallMs,
      off_wall_ms: off.wallMs,
      wall_ratio: on.wallMs / off.wallMs,
      on_nodes: on.nodes.toString(),
      off_nodes: off.nodes.toString(),
      node_ratio: Number(on.nodes) / Number(off.nodes),
      ...compare(on, off),
    });
  }

  const expectedPositions = corpus.positions.length;
  const nodeRatio = median(trials.map((trial) => trial.node_ratio));
  const medianWallRatio = median(trials.map((trial) => trial.wall_ratio));
  const correctnessPassed = trials.every(
    (trial) =>
      trial.moveMatches === expectedPositions &&
      trial.scoreMatches === expectedPositions &&
      trial.depthMatches === expectedPositions &&
      trial.enabledSelections === expectedPositions &&
      trial.disabledSelections === expectedPositions,
  );
  const report = {
    schema_version: "wasm-probcut-speed-gate-v1",
    corpus_checksum_sha256: corpus.checksum,
    position_count: expectedPositions,
    phases: config.phases,
    positions_per_phase: config.positionsPerPhase,
    depth: config.depth,
    exact_endgame_empties: config.exactEndgameEmpties,
    trial_count: config.trials,
    acceptance: {
      maximum_node_ratio: config.maximumNodeRatio,
      maximum_median_wall_ratio: config.maximumMedianWallRatio,
      require_exact_output_match: true,
    },
    result: {
      node_ratio: nodeRatio,
      median_wall_ratio: medianWallRatio,
      correctness_passed: correctnessPassed,
      node_gate_passed: nodeRatio <= config.maximumNodeRatio,
      wall_gate_passed: medianWallRatio <= config.maximumMedianWallRatio,
    },
    trials,
  };
  report.passed =
    report.result.correctness_passed &&
    report.result.node_gate_passed &&
    report.result.wall_gate_passed;
  const rendered = `${JSON.stringify(report, null, 2)}\n`;
  if (config.output) await writeFile(config.output, rendered, "utf8");
  process.stdout.write(rendered);
  return report.passed ? 0 : 1;
}

process.exitCode = await main();
