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
  const ratio = (name, fallback, maximum = 1) => {
    const parsed = Number(values.get(name) ?? fallback);
    if (!Number.isFinite(parsed) || parsed <= 0 || parsed > maximum) {
      fail(`${name} must be in (0, ${maximum}]`);
    }
    return parsed;
  };
  const integerList = (name, fallback) => {
    const parsed = (values.get(name) ?? fallback).split(",").map(Number);
    if (
      parsed.length === 0 ||
      parsed.some((value) => !Number.isSafeInteger(value) || value <= 0) ||
      new Set(parsed).size !== parsed.length
    ) {
      fail(`${name} must be a comma-separated list of unique positive integers`);
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
    profileId: values.get("--profile-id") ?? null,
    machineClass: values.get("--machine-class") ?? `local ${process.arch} desktop`,
    depth: integer("--depth", 8),
    extendedDepths: integerList("--extended-depths", values.get("--depth") ?? "8"),
    exactEndgameEmpties: integer("--exact-endgame-empties", 8),
    positionsPerPhase: integer("--positions-per-phase", 30),
    trials: integer("--trials", 3),
    extendedPositionsPerPhase: integer(
      "--extended-positions-per-phase",
      values.get("--positions-per-phase") ?? 30,
    ),
    extendedTrials: integer("--extended-trials", 1),
    fixedMaxNodes: integer("--fixed-max-nodes", 50_000_000),
    timedMaxDepth: values.has("--timed-max-depth")
      ? integer("--timed-max-depth", 64)
      : null,
    timedMaxTimeMs: values.has("--timed-max-time-ms")
      ? integer("--timed-max-time-ms", 500)
      : null,
    timedPositionsPerPhase: integer(
      "--timed-positions-per-phase",
      values.get("--extended-positions-per-phase") ?? values.get("--positions-per-phase") ?? 30,
    ),
    timedTrials: integer("--timed-trials", 1),
    maximumNodeRatio: ratio("--maximum-node-ratio", 0.99),
    maximumMedianWallRatio: ratio("--maximum-median-wall-ratio", 0.99),
    maximumExtendedAggregateNodeRatio: ratio(
      "--maximum-extended-aggregate-node-ratio",
      0.99,
    ),
    maximumExtendedDepthNodeRatio: ratio("--maximum-extended-depth-node-ratio", 1.01, 2),
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

function takePositionsPerPhase(positions, phases, positionsPerPhase) {
  const counts = new Map(phases.map((phase) => [phase, 0]));
  const selected = [];
  for (const item of positions) {
    const count = counts.get(item.phase);
    if (count === undefined || count >= positionsPerPhase) continue;
    selected.push(item);
    counts.set(item.phase, count + 1);
  }
  const missing = [...counts]
    .filter(([, count]) => count < positionsPerPhase)
    .map(([phase, count]) => `${phase}:${count}/${positionsPerPhase}`);
  if (missing.length !== 0) fail(`selected corpus lacks positions: ${missing.join(", ")}`);
  return selected;
}

async function createArtifact(modulePath, manifest, weights) {
  const imported = await import(pathToFileURL(resolve(modulePath)).href);
  const core = await WasmCore.create(imported.default ?? imported);
  return core.loadEvaluationArtifact(manifest, weights);
}

async function runVariant(label, modulePath, manifest, weights, positions, config, limits) {
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
      const positionStarted = performance.now();
      const result = artifact.searchBestMoveWithPreset(
        item.position,
        limits,
        "normal",
        config.exactEndgameEmpties,
      );
      rows.push({
        id: item.id,
        phase: item.phase,
        elapsedMs: performance.now() - positionStarted,
        result,
      });
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
  let sameDepthRows = 0;
  let sameDepthMoveMatches = 0;
  let sameDepthScoreMatches = 0;
  let onDeeper = 0;
  let equalDepth = 0;
  let offDeeper = 0;
  let onStopped = 0;
  let offStopped = 0;
  for (let index = 0; index < on.rows.length; ++index) {
    const enabled = on.rows[index];
    const disabled = off.rows[index];
    if (enabled.id !== disabled.id) fail("variant position order differs");
    moveMatches += enabled.result.bestMoveSquare === disabled.result.bestMoveSquare;
    scoreMatches += enabled.result.score === disabled.result.score;
    depthMatches += enabled.result.completedDepth === disabled.result.completedDepth;
    enabledSelections += enabled.result.probcutEnabled === true;
    disabledSelections += disabled.result.probcutEnabled === false;
    onStopped += enabled.result.stopped === true;
    offStopped += disabled.result.stopped === true;
    if (enabled.result.completedDepth > disabled.result.completedDepth) {
      onDeeper += 1;
    } else if (enabled.result.completedDepth < disabled.result.completedDepth) {
      offDeeper += 1;
    } else {
      equalDepth += 1;
      sameDepthRows += 1;
      sameDepthMoveMatches += enabled.result.bestMoveSquare === disabled.result.bestMoveSquare;
      sameDepthScoreMatches += enabled.result.score === disabled.result.score;
    }
  }
  return {
    moveMatches,
    scoreMatches,
    depthMatches,
    enabledSelections,
    disabledSelections,
    sameDepthRows,
    sameDepthMoveMatches,
    sameDepthScoreMatches,
    onDeeper,
    equalDepth,
    offDeeper,
    onStopped,
    offStopped,
  };
}

async function runTrials(config, manifest, weights, positions, limits, trialCount) {
  const trials = [];
  for (let trial = 0; trial < trialCount; ++trial) {
    const order = trial % 2 === 0 ? ["off", "on"] : ["on", "off"];
    const results = new Map();
    for (const label of order) {
      const modulePath = label === "on" ? config.onModule : config.offModule;
      results.set(
        label,
        await runVariant(label, modulePath, manifest, weights, positions, config, limits),
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
      on_nodes: on.nodes,
      off_nodes: off.nodes,
      node_ratio: Number(on.nodes) / Number(off.nodes),
      on_rows: on.rows,
      off_rows: off.rows,
      comparison: compare(on, off),
    });
  }
  return trials;
}

function sumBigInts(values) {
  return values.reduce((sum, value) => sum + value, 0n);
}

function histogram(values) {
  const counts = new Map();
  for (const value of values) counts.set(value, (counts.get(value) ?? 0) + 1);
  return Object.fromEntries([...counts].sort(([left], [right]) => left - right));
}

function summarizeFixed(depth, positionCount, trials, maximumNodeRatio, maximumWallRatio) {
  const expectedOutputs = positionCount * trials.length;
  const onNodes = sumBigInts(trials.map((trial) => trial.on_nodes));
  const offNodes = sumBigInts(trials.map((trial) => trial.off_nodes));
  const totals = trials.reduce(
    (result, trial) => {
      for (const name of [
        "moveMatches",
        "scoreMatches",
        "depthMatches",
        "enabledSelections",
        "disabledSelections",
        "onStopped",
        "offStopped",
      ]) {
        result[name] += trial.comparison[name];
      }
      return result;
    },
    {
      moveMatches: 0,
      scoreMatches: 0,
      depthMatches: 0,
      enabledSelections: 0,
      disabledSelections: 0,
      onStopped: 0,
      offStopped: 0,
    },
  );
  const nodeRatio = Number(onNodes) / Number(offNodes);
  const medianWallRatio = median(trials.map((trial) => trial.wall_ratio));
  const onRows = trials.flatMap((trial) => trial.on_rows);
  const offRows = trials.flatMap((trial) => trial.off_rows);
  const outputParityPassed =
    totals.moveMatches === expectedOutputs &&
    totals.scoreMatches === expectedOutputs &&
    totals.depthMatches === expectedOutputs &&
    totals.enabledSelections === expectedOutputs &&
    totals.disabledSelections === expectedOutputs &&
    totals.onStopped === 0 &&
    totals.offStopped === 0;
  return {
    depth,
    position_count: positionCount,
    trial_count: trials.length,
    enabled_nodes: onNodes.toString(),
    disabled_nodes: offNodes.toString(),
    node_ratio: nodeRatio,
    median_wall_ratio: medianWallRatio,
    move_matches: totals.moveMatches,
    score_matches: totals.scoreMatches,
    completed_depth_matches: totals.depthMatches,
    expected_output_matches: expectedOutputs,
    enabled_selection_matches: totals.enabledSelections,
    disabled_selection_matches: totals.disabledSelections,
    enabled_stopped: totals.onStopped,
    disabled_stopped: totals.offStopped,
    enabled_stopped_position_ids: [
      ...new Set(onRows.filter((row) => row.result.stopped).map((row) => row.id)),
    ],
    disabled_stopped_position_ids: [
      ...new Set(offRows.filter((row) => row.result.stopped).map((row) => row.id)),
    ],
    enabled_completed_depth_histogram: histogram(
      onRows.map((row) => row.result.completedDepth),
    ),
    disabled_completed_depth_histogram: histogram(
      offRows.map((row) => row.result.completedDepth),
    ),
    output_parity_passed: outputParityPassed,
    node_gate_passed: nodeRatio <= maximumNodeRatio,
    wall_gate_passed: maximumWallRatio === null || medianWallRatio <= maximumWallRatio,
    passed:
      outputParityPassed &&
      nodeRatio <= maximumNodeRatio &&
      (maximumWallRatio === null || medianWallRatio <= maximumWallRatio),
    wall_trials: trials.map((trial) => ({
      order: trial.order,
      ratio: trial.wall_ratio,
    })),
  };
}

function summarizeTimed(positionCount, trials) {
  const expectedOutputs = positionCount * trials.length;
  const comparisons = trials.map((trial) => trial.comparison);
  const onRows = trials.flatMap((trial) => trial.on_rows);
  const offRows = trials.flatMap((trial) => trial.off_rows);
  const total = (name) => comparisons.reduce((sum, item) => sum + item[name], 0);
  const onDepths = onRows.map((row) => row.result.completedDepth);
  const offDepths = offRows.map((row) => row.result.completedDepth);
  const onDepthSum = onDepths.reduce((sum, depth) => sum + depth, 0);
  const offDepthSum = offDepths.reduce((sum, depth) => sum + depth, 0);
  const sameDepthRows = total("sameDepthRows");
  const sameDepthOutputParityPassed =
    total("sameDepthMoveMatches") === sameDepthRows &&
    total("sameDepthScoreMatches") === sameDepthRows;
  const selectionPassed =
    total("enabledSelections") === expectedOutputs &&
    total("disabledSelections") === expectedOutputs;
  const outputParityPassed =
    total("moveMatches") === expectedOutputs && total("scoreMatches") === expectedOutputs;
  const depthGatePassed = onDepthSum >= offDepthSum && median(onDepths) >= median(offDepths);
  return {
    position_count: positionCount,
    trial_count: trials.length,
    median_enabled_wall_ms: median(onRows.map((row) => row.elapsedMs)),
    median_disabled_wall_ms: median(offRows.map((row) => row.elapsedMs)),
    median_total_wall_ratio: median(trials.map((trial) => trial.wall_ratio)),
    median_enabled_completed_depth: median(onDepths),
    median_disabled_completed_depth: median(offDepths),
    enabled_completed_depth_histogram: histogram(onDepths),
    disabled_completed_depth_histogram: histogram(offDepths),
    enabled_completed_depth_sum: onDepthSum,
    disabled_completed_depth_sum: offDepthSum,
    completed_depth_sum_ratio: onDepthSum / offDepthSum,
    enabled_deeper: total("onDeeper"),
    equal_depth: total("equalDepth"),
    disabled_deeper: total("offDeeper"),
    move_matches: total("moveMatches"),
    score_matches: total("scoreMatches"),
    completed_depth_matches: total("depthMatches"),
    expected_output_comparisons: expectedOutputs,
    same_depth_comparisons: sameDepthRows,
    same_depth_move_matches: total("sameDepthMoveMatches"),
    same_depth_score_matches: total("sameDepthScoreMatches"),
    enabled_stopped: total("onStopped"),
    disabled_stopped: total("offStopped"),
    same_depth_output_parity_passed: sameDepthOutputParityPassed,
    output_parity_passed: outputParityPassed,
    selection_passed: selectionPassed,
    completed_depth_gate_passed: depthGatePassed,
    passed:
      outputParityPassed && sameDepthOutputParityPassed && selectionPassed && depthGatePassed,
    wall_trials: trials.map((trial) => ({
      order: trial.order,
      enabled_ms: trial.on_wall_ms,
      disabled_ms: trial.off_wall_ms,
      ratio: trial.wall_ratio,
    })),
  };
}

async function main() {
  const config = parseArguments(process.argv.slice(2));
  const manifest = await readFile(config.manifest, "utf8");
  const weights = await readFile(config.weights);
  const maximumPositionsPerPhase = Math.max(
    config.positionsPerPhase,
    config.extendedPositionsPerPhase,
    config.timedMaxDepth === null ? 0 : config.timedPositionsPerPhase,
  );
  const corpus = await loadPositions(config.corpus, config.phases, maximumPositionsPerPhase);
  const primaryPositions = takePositionsPerPhase(
    corpus.positions,
    config.phases,
    config.positionsPerPhase,
  );
  const primaryTrials = await runTrials(
    config,
    manifest,
    weights,
    primaryPositions,
    { maxDepth: config.depth, maxNodes: config.fixedMaxNodes },
    config.trials,
  );
  const primaryFixed = summarizeFixed(
    config.depth,
    primaryPositions.length,
    primaryTrials,
    config.maximumNodeRatio,
    config.maximumMedianWallRatio,
  );

  const extendedPositions = takePositionsPerPhase(
    corpus.positions,
    config.phases,
    config.extendedPositionsPerPhase,
  );
  const extendedFixed = [];
  for (const depth of config.extendedDepths) {
    const trials = await runTrials(
      config,
      manifest,
      weights,
      extendedPositions,
      { maxDepth: depth, maxNodes: config.fixedMaxNodes },
      config.extendedTrials,
    );
    extendedFixed.push(
      summarizeFixed(
        depth,
        extendedPositions.length,
        trials,
        config.maximumExtendedDepthNodeRatio,
        null,
      ),
    );
  }
  const extendedEnabledNodes = sumBigInts(
    extendedFixed.map((result) => BigInt(result.enabled_nodes)),
  );
  const extendedDisabledNodes = sumBigInts(
    extendedFixed.map((result) => BigInt(result.disabled_nodes)),
  );
  const extendedAggregateNodeRatio =
    Number(extendedEnabledNodes) / Number(extendedDisabledNodes);
  const extendedFixedPassed =
    extendedFixed.every((result) => result.passed) &&
    extendedAggregateNodeRatio <= config.maximumExtendedAggregateNodeRatio;

  let timed = null;
  if (config.timedMaxDepth !== null || config.timedMaxTimeMs !== null) {
    if (config.timedMaxDepth === null || config.timedMaxTimeMs === null) {
      fail("--timed-max-depth and --timed-max-time-ms must be provided together");
    }
    const timedPositions = takePositionsPerPhase(
      corpus.positions,
      config.phases,
      config.timedPositionsPerPhase,
    );
    const timedTrials = await runTrials(
      config,
      manifest,
      weights,
      timedPositions,
      { maxDepth: config.timedMaxDepth, maxNodes: 0, maxTimeMs: config.timedMaxTimeMs },
      config.timedTrials,
    );
    timed = {
      max_depth: config.timedMaxDepth,
      max_time_ms: config.timedMaxTimeMs,
      positions_per_phase: config.timedPositionsPerPhase,
      ...summarizeTimed(timedPositions.length, timedTrials),
    };
  }

  const report = {
    schema_version: "wasm-probcut-speed-gate-v2",
    profile_id: config.profileId,
    environment: {
      machine_class: config.machineClass,
      node_version: process.versions.node,
    },
    corpus_checksum_sha256: corpus.checksum,
    phases: config.phases,
    exact_endgame_empties: config.exactEndgameEmpties,
    fixed_max_nodes: config.fixedMaxNodes,
    acceptance: {
      maximum_node_ratio: config.maximumNodeRatio,
      maximum_median_wall_ratio: config.maximumMedianWallRatio,
      maximum_extended_aggregate_node_ratio: config.maximumExtendedAggregateNodeRatio,
      maximum_extended_depth_node_ratio: config.maximumExtendedDepthNodeRatio,
      require_fixed_output_match: true,
      require_timed_best_move_score_match: true,
      require_timed_same_depth_output_match: true,
      require_timed_completed_depth_non_regression: true,
    },
    result: {
      primary_fixed: {
        positions_per_phase: config.positionsPerPhase,
        ...primaryFixed,
      },
      extended_fixed: {
        positions_per_phase: config.extendedPositionsPerPhase,
        enabled_nodes: extendedEnabledNodes.toString(),
        disabled_nodes: extendedDisabledNodes.toString(),
        aggregate_node_ratio: extendedAggregateNodeRatio,
        aggregate_node_gate_passed:
          extendedAggregateNodeRatio <= config.maximumExtendedAggregateNodeRatio,
        depths: extendedFixed,
        passed: extendedFixedPassed,
      },
      timed,
    },
  };
  report.passed = primaryFixed.passed && extendedFixedPassed && (timed?.passed ?? true);
  const rendered = `${JSON.stringify(report, null, 2)}\n`;
  if (config.output) await writeFile(config.output, rendered, "utf8");
  process.stdout.write(rendered);
  return report.passed ? 0 : 1;
}

process.exitCode = await main();
