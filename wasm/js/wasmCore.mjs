const ABI_VERSION = 1;

const STATUS_OK = 0;
const STATUS_NAMES = new Map([
  [1, "null pointer"],
  [2, "invalid position"],
  [3, "invalid move"],
  [4, "illegal move"],
  [5, "illegal pass"],
]);

const SIDE_BLACK = 0;
const SIDE_WHITE = 1;

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
  };
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

  checkResultStatus(returnStatus, resultPtr, statusOffset, operation) {
    const resultStatus = this.view().getUint32(resultPtr + statusOffset, true);
    checkStatus(returnStatus, operation);
    checkStatus(resultStatus, operation);
  }
}
