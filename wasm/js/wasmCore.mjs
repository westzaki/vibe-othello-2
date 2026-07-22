const ABI_VERSION = 1;

const STATUS_OK = 0;
const STATUS_NAMES = new Map([
  [1, "null pointer"],
  [2, "invalid position"],
  [3, "invalid move"],
  [4, "illegal move"],
  [5, "illegal pass"],
  [6, "invalid argument"],
  [7, "artifact load failed"],
  [8, "evaluator not loaded"],
  [9, "search failed"],
]);

const SIDE_BLACK = 0;
const SIDE_WHITE = 1;
const VIBE_OTHELLO_WASM_SEARCH_RESULT_FLAG_PROBCUT_ENABLED = 1;

const SEARCH_PRESETS = new Map([
  ["easy", 0],
  ["normal", 1],
  ["hard", 2],
]);

function requireFunction(module, name) {
  const fn = module[name];
  if (typeof fn !== "function") {
    throw new Error(`WASM module is missing ${name}`);
  }
  return fn;
}

function statusName(status) {
  return STATUS_NAMES.get(status) ?? `unknown status ${status}`;
}

function checkStatus(status, operation) {
  if (status !== STATUS_OK) {
    throw new Error(`${operation} failed: ${statusName(status)}`);
  }
}

function sideToWire(sideToMove) {
  if (sideToMove === "black") {
    return SIDE_BLACK;
  }
  if (sideToMove === "white") {
    return SIDE_WHITE;
  }
  throw new Error(`invalid sideToMove: ${sideToMove}`);
}

function sideFromWire(sideToMove) {
  if (sideToMove === SIDE_BLACK) {
    return "black";
  }
  if (sideToMove === SIDE_WHITE) {
    return "white";
  }
  throw new Error(`WASM returned invalid side_to_move: ${sideToMove}`);
}

function toBoolean(value) {
  return value !== 0;
}

function toUint32Limit(value, name) {
  if (value === undefined) {
    return 0;
  }
  if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
    throw new Error(`${name} must be an integer between 0 and 4294967295`);
  }
  return value;
}

function searchPresetToWire(preset) {
  const value = SEARCH_PRESETS.get(preset);
  if (value === undefined) {
    throw new Error(`invalid search preset: ${preset}`);
  }
  return value;
}

function normalizeWeightsBytes(weightsBytes) {
  if (weightsBytes instanceof Uint8Array) {
    return weightsBytes;
  }
  if (weightsBytes instanceof ArrayBuffer) {
    return new Uint8Array(weightsBytes);
  }
  throw new Error("weightsBytes must be a Uint8Array or ArrayBuffer");
}

function makeLayout(module) {
  return {
    position: {
      size: requireFunction(module, "_vibe_othello_wasm_sizeof_position")(),
      player: requireFunction(module, "_vibe_othello_wasm_offsetof_position_player")(),
      opponent: requireFunction(module, "_vibe_othello_wasm_offsetof_position_opponent")(),
      sideToMove: requireFunction(module, "_vibe_othello_wasm_offsetof_position_side_to_move")(),
    },
    query: {
      size: requireFunction(module, "_vibe_othello_wasm_sizeof_position_query")(),
      status: requireFunction(module, "_vibe_othello_wasm_offsetof_position_query_status")(),
      legalMoves: requireFunction(module, "_vibe_othello_wasm_offsetof_position_query_legal_moves")(),
      hasLegalMove: requireFunction(
        module,
        "_vibe_othello_wasm_offsetof_position_query_has_legal_move",
      )(),
      isTerminal: requireFunction(module, "_vibe_othello_wasm_offsetof_position_query_is_terminal")(),
    },
    applyResult: {
      size: requireFunction(module, "_vibe_othello_wasm_sizeof_apply_result")(),
      status: requireFunction(module, "_vibe_othello_wasm_offsetof_apply_result_status")(),
      position: requireFunction(module, "_vibe_othello_wasm_offsetof_apply_result_position")(),
      flipped: requireFunction(module, "_vibe_othello_wasm_offsetof_apply_result_flipped")(),
      legalMoves: requireFunction(module, "_vibe_othello_wasm_offsetof_apply_result_legal_moves")(),
      hasLegalMove: requireFunction(
        module,
        "_vibe_othello_wasm_offsetof_apply_result_has_legal_move",
      )(),
      isTerminal: requireFunction(module, "_vibe_othello_wasm_offsetof_apply_result_is_terminal")(),
    },
    searchResult: {
      size: requireFunction(module, "_vibe_othello_wasm_sizeof_search_result")(),
      status: requireFunction(module, "_vibe_othello_wasm_offsetof_search_result_status")(),
      hasBestMove: requireFunction(
        module,
        "_vibe_othello_wasm_offsetof_search_result_has_best_move",
      )(),
      bestMoveSquare: requireFunction(
        module,
        "_vibe_othello_wasm_offsetof_search_result_best_move_square",
      )(),
      isPass: requireFunction(module, "_vibe_othello_wasm_offsetof_search_result_is_pass")(),
      flags: requireFunction(module, "_vibe_othello_wasm_offsetof_search_result_flags")(),
      score: requireFunction(module, "_vibe_othello_wasm_offsetof_search_result_score")(),
      completedDepth: requireFunction(
        module,
        "_vibe_othello_wasm_offsetof_search_result_completed_depth",
      )(),
      nodes: requireFunction(module, "_vibe_othello_wasm_offsetof_search_result_nodes")(),
      elapsedMs: requireFunction(module, "_vibe_othello_wasm_offsetof_search_result_elapsed_ms")(),
      stopped: requireFunction(module, "_vibe_othello_wasm_offsetof_search_result_stopped")(),
      exact: requireFunction(module, "_vibe_othello_wasm_offsetof_search_result_exact")(),
    },
  };
}

export class WasmEvaluationArtifact {
  #core;
  #handle;

  constructor(core, handle) {
    this.#core = core;
    this.#handle = handle;
  }

  evaluatePosition(position) {
    return this.#core.evaluatePositionWithHandle(this.#requireHandle(), position);
  }

  searchBestMove(position, limits = {}) {
    return this.#core.searchBestMoveWithHandle(this.#requireHandle(), position, limits);
  }

  searchBestMoveWithPreset(position, limits = {}, preset = "easy", exactEndgameEmpties = 0) {
    return this.#core.searchBestMoveWithPresetHandle(
      this.#requireHandle(),
      position,
      limits,
      preset,
      exactEndgameEmpties,
    );
  }

  setSearchSessionReuse(retain) {
    this.#core.setSearchSessionReuseHandle(this.#requireHandle(), retain);
  }

  resetSearchSession() {
    this.#core.resetSearchSessionHandle(this.#requireHandle());
  }

  free() {
    if (this.#handle !== 0) {
      this.#core.freeEvaluationArtifactHandle(this.#handle);
      this.#handle = 0;
    }
  }

  #requireHandle() {
    if (this.#handle === 0) {
      throw new Error("evaluation artifact has been freed");
    }
    return this.#handle;
  }
}

/**
 * Small JavaScript domain wrapper around the Vibe Othello Emscripten C ABI.
 */
export class WasmCore {
  static async create(createModuleOrModule) {
    const module =
      typeof createModuleOrModule === "function"
        ? await createModuleOrModule()
        : await createModuleOrModule;

    if (!module?.HEAPU8?.buffer) {
      throw new Error("WASM module is missing HEAPU8");
    }

    const abiVersion = requireFunction(module, "_vibe_othello_wasm_abi_version")();
    if (abiVersion !== ABI_VERSION) {
      throw new Error(`unsupported WASM ABI version: ${abiVersion}`);
    }

    return new WasmCore(module, makeLayout(module));
  }

  constructor(module, layout) {
    this.module = module;
    this.layout = layout;
    this.initialPositionFn = requireFunction(module, "_vibe_othello_wasm_initial_position");
    this.queryPositionFn = requireFunction(module, "_vibe_othello_wasm_query_position");
    this.applyMoveFn = requireFunction(module, "_vibe_othello_wasm_apply_move");
    this.applyPassFn = requireFunction(module, "_vibe_othello_wasm_apply_pass");
    this.loadEvalArtifactFn = requireFunction(module, "_vibe_othello_wasm_load_eval_artifact");
    this.freeEvalArtifactFn = requireFunction(module, "_vibe_othello_wasm_free_eval_artifact");
    this.setSearchSessionReuseFn = requireFunction(
      module,
      "_vibe_othello_wasm_set_search_session_reuse",
    );
    this.resetSearchSessionFn = requireFunction(
      module,
      "_vibe_othello_wasm_reset_search_session",
    );
    this.evaluatePositionFn = requireFunction(module, "_vibe_othello_wasm_evaluate_position");
    this.searchBestMoveFn = requireFunction(module, "_vibe_othello_wasm_search_best_move");
    this.searchBestMoveV2Fn = requireFunction(module, "_vibe_othello_wasm_search_best_move_v2");
    this.mallocFn = requireFunction(module, "_malloc");
    this.freeFn = requireFunction(module, "_free");
  }

  initialPosition() {
    const positionPtr = this.malloc(this.layout.position.size);
    try {
      const status = this.initialPositionFn(positionPtr);
      checkStatus(status, "initialPosition");
      return this.readPosition(positionPtr);
    } finally {
      this.freeFn(positionPtr);
    }
  }

  queryPosition(position) {
    let positionPtr = 0;
    let queryPtr = 0;
    try {
      positionPtr = this.malloc(this.layout.position.size);
      queryPtr = this.malloc(this.layout.query.size);
      this.writePosition(positionPtr, position);
      const status = this.queryPositionFn(positionPtr, queryPtr);
      this.checkResultStatus(status, queryPtr, this.layout.query.status, "queryPosition");
      return {
        legalMoves: this.view().getBigUint64(queryPtr + this.layout.query.legalMoves, true),
        hasLegalMove: toBoolean(this.view().getUint8(queryPtr + this.layout.query.hasLegalMove)),
        isTerminal: toBoolean(this.view().getUint8(queryPtr + this.layout.query.isTerminal)),
      };
    } finally {
      if (queryPtr !== 0) {
        this.freeFn(queryPtr);
      }
      if (positionPtr !== 0) {
        this.freeFn(positionPtr);
      }
    }
  }

  applyMove(position, squareIndex) {
    return this.apply(position, "applyMove", (positionPtr, resultPtr) =>
      this.applyMoveFn(positionPtr, squareIndex, resultPtr),
    );
  }

  applyPass(position) {
    return this.apply(position, "applyPass", (positionPtr, resultPtr) =>
      this.applyPassFn(positionPtr, resultPtr),
    );
  }

  loadEvaluationArtifact(manifestText, weightsBytes) {
    if (typeof manifestText !== "string") {
      throw new Error("manifestText must be a string");
    }

    const manifestBytes = new TextEncoder().encode(manifestText);
    const weightsView = normalizeWeightsBytes(weightsBytes);
    let manifestPtr = 0;
    let weightsPtr = 0;
    let handlePtr = 0;
    try {
      manifestPtr = this.copyBytes(manifestBytes);
      weightsPtr = this.copyBytes(weightsView);
      handlePtr = this.malloc(4);
      const status = this.loadEvalArtifactFn(
        manifestPtr,
        manifestBytes.byteLength,
        weightsPtr,
        weightsView.byteLength,
        handlePtr,
      );
      checkStatus(status, "loadEvaluationArtifact");

      const handle = this.view().getUint32(handlePtr, true);
      if (handle === 0) {
        throw new Error("loadEvaluationArtifact failed: evaluator handle was not returned");
      }
      return new WasmEvaluationArtifact(this, handle);
    } finally {
      if (handlePtr !== 0) {
        this.freeFn(handlePtr);
      }
      if (weightsPtr !== 0) {
        this.freeFn(weightsPtr);
      }
      if (manifestPtr !== 0) {
        this.freeFn(manifestPtr);
      }
    }
  }

  evaluatePositionWithHandle(handle, position) {
    let positionPtr = 0;
    let scorePtr = 0;
    try {
      positionPtr = this.malloc(this.layout.position.size);
      scorePtr = this.malloc(4);
      this.writePosition(positionPtr, position);
      const status = this.evaluatePositionFn(handle, positionPtr, scorePtr);
      checkStatus(status, "evaluatePosition");
      return this.view().getInt32(scorePtr, true);
    } finally {
      if (scorePtr !== 0) {
        this.freeFn(scorePtr);
      }
      if (positionPtr !== 0) {
        this.freeFn(positionPtr);
      }
    }
  }

  searchBestMoveWithHandle(handle, position, limits = {}) {
    const maxDepth = toUint32Limit(limits.maxDepth, "maxDepth");
    const maxNodes = toUint32Limit(limits.maxNodes, "maxNodes");
    const maxTimeMs = toUint32Limit(limits.maxTimeMs, "maxTimeMs");
    if (maxDepth === 0 && maxNodes === 0 && maxTimeMs === 0) {
      throw new Error("searchBestMove requires maxDepth, maxNodes, or maxTimeMs");
    }

    let positionPtr = 0;
    let resultPtr = 0;
    try {
      positionPtr = this.malloc(this.layout.position.size);
      resultPtr = this.malloc(this.layout.searchResult.size);
      this.writePosition(positionPtr, position);
      const status = this.searchBestMoveFn(
        handle,
        positionPtr,
        maxDepth,
        maxNodes,
        maxTimeMs,
        resultPtr,
      );
      this.checkResultStatus(status, resultPtr, this.layout.searchResult.status, "searchBestMove");
      return this.readSearchResult(resultPtr);
    } finally {
      if (resultPtr !== 0) {
        this.freeFn(resultPtr);
      }
      if (positionPtr !== 0) {
        this.freeFn(positionPtr);
      }
    }
  }

  searchBestMoveWithPresetHandle(
    handle,
    position,
    limits = {},
    preset = "easy",
    exactEndgameEmpties = 0,
  ) {
    const maxDepth = toUint32Limit(limits.maxDepth, "maxDepth");
    const maxNodes = toUint32Limit(limits.maxNodes, "maxNodes");
    const maxTimeMs = toUint32Limit(limits.maxTimeMs, "maxTimeMs");
    const searchPreset = searchPresetToWire(preset);
    const exactEmpties = toUint32Limit(exactEndgameEmpties, "exactEndgameEmpties");
    if (exactEmpties > 64) {
      throw new Error("exactEndgameEmpties must be at most 64");
    }
    if (exactEmpties !== 0 && maxNodes === 0 && maxTimeMs === 0) {
      throw new Error(
        "searchBestMoveWithPreset requires maxNodes or maxTimeMs when exactEndgameEmpties is set",
      );
    }
    if (maxDepth === 0 && maxNodes === 0 && maxTimeMs === 0) {
      throw new Error("searchBestMoveWithPreset requires maxDepth, maxNodes, or maxTimeMs");
    }

    let positionPtr = 0;
    let resultPtr = 0;
    try {
      positionPtr = this.malloc(this.layout.position.size);
      resultPtr = this.malloc(this.layout.searchResult.size);
      this.writePosition(positionPtr, position);
      const status = this.searchBestMoveV2Fn(
        handle,
        positionPtr,
        maxDepth,
        maxNodes,
        maxTimeMs,
        searchPreset,
        exactEmpties,
        resultPtr,
      );
      this.checkResultStatus(
        status,
        resultPtr,
        this.layout.searchResult.status,
        "searchBestMoveWithPreset",
      );
      return this.readSearchResult(resultPtr);
    } finally {
      if (resultPtr !== 0) {
        this.freeFn(resultPtr);
      }
      if (positionPtr !== 0) {
        this.freeFn(positionPtr);
      }
    }
  }

  freeEvaluationArtifactHandle(handle) {
    this.freeEvalArtifactFn(handle);
  }

  setSearchSessionReuseHandle(handle, retain) {
    checkStatus(this.setSearchSessionReuseFn(handle, retain ? 1 : 0), "setSearchSessionReuse");
  }

  resetSearchSessionHandle(handle) {
    checkStatus(this.resetSearchSessionFn(handle), "resetSearchSession");
  }

  apply(position, operation, call) {
    let positionPtr = 0;
    let resultPtr = 0;
    try {
      positionPtr = this.malloc(this.layout.position.size);
      resultPtr = this.malloc(this.layout.applyResult.size);
      this.writePosition(positionPtr, position);
      const status = call(positionPtr, resultPtr);
      this.checkResultStatus(status, resultPtr, this.layout.applyResult.status, operation);
      return {
        position: this.readPosition(resultPtr + this.layout.applyResult.position),
        flipped: this.view().getBigUint64(resultPtr + this.layout.applyResult.flipped, true),
        legalMoves: this.view().getBigUint64(resultPtr + this.layout.applyResult.legalMoves, true),
        hasLegalMove: toBoolean(
          this.view().getUint8(resultPtr + this.layout.applyResult.hasLegalMove),
        ),
        isTerminal: toBoolean(this.view().getUint8(resultPtr + this.layout.applyResult.isTerminal)),
      };
    } finally {
      if (resultPtr !== 0) {
        this.freeFn(resultPtr);
      }
      if (positionPtr !== 0) {
        this.freeFn(positionPtr);
      }
    }
  }

  malloc(size) {
    const ptr = this.mallocFn(size);
    if (ptr === 0) {
      throw new Error(`WASM malloc failed for ${size} bytes`);
    }
    return ptr;
  }

  view() {
    return new DataView(this.module.HEAPU8.buffer);
  }

  readPosition(ptr) {
    const view = this.view();
    return {
      player: view.getBigUint64(ptr + this.layout.position.player, true),
      opponent: view.getBigUint64(ptr + this.layout.position.opponent, true),
      sideToMove: sideFromWire(view.getUint8(ptr + this.layout.position.sideToMove)),
    };
  }

  writePosition(ptr, position) {
    const view = this.view();
    view.setBigUint64(ptr + this.layout.position.player, position.player, true);
    view.setBigUint64(ptr + this.layout.position.opponent, position.opponent, true);
    view.setUint8(ptr + this.layout.position.sideToMove, sideToWire(position.sideToMove));
  }

  copyBytes(bytes) {
    if (bytes.byteLength === 0) {
      return 0;
    }
    const ptr = this.malloc(bytes.byteLength);
    this.module.HEAPU8.set(bytes, ptr);
    return ptr;
  }

  readSearchResult(ptr) {
    const view = this.view();
    const hasBestMove = toBoolean(view.getUint8(ptr + this.layout.searchResult.hasBestMove));
    return {
      hasBestMove,
      bestMoveSquare: hasBestMove
        ? view.getUint8(ptr + this.layout.searchResult.bestMoveSquare)
        : null,
      isPass: toBoolean(view.getUint8(ptr + this.layout.searchResult.isPass)),
      probcutEnabled:
        (view.getUint8(ptr + this.layout.searchResult.flags) &
          VIBE_OTHELLO_WASM_SEARCH_RESULT_FLAG_PROBCUT_ENABLED) !==
        0,
      score: view.getInt32(ptr + this.layout.searchResult.score, true),
      completedDepth: view.getUint32(ptr + this.layout.searchResult.completedDepth, true),
      nodes: view.getBigUint64(ptr + this.layout.searchResult.nodes, true),
      elapsedMs: view.getUint32(ptr + this.layout.searchResult.elapsedMs, true),
      stopped: toBoolean(view.getUint8(ptr + this.layout.searchResult.stopped)),
      exact: toBoolean(view.getUint8(ptr + this.layout.searchResult.exact)),
    };
  }

  checkResultStatus(returnStatus, resultPtr, statusOffset, operation) {
    const resultStatus = this.view().getUint32(resultPtr + statusOffset, true);
    checkStatus(returnStatus, operation);
    checkStatus(resultStatus, operation);
  }
}
