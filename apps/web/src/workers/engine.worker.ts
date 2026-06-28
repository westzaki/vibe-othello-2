import { WasmCore } from "@vibe-othello/wasm-core";
import type {
  EmscriptenModule,
  EmscriptenModuleFactory,
  WasmPosition,
  WasmPositionQuery,
} from "@vibe-othello/wasm-core";

import type { BoardCell, BoardSnapshot, DiscColor, EngineRequest, EngineResponse } from "./protocol";

type WorkerGlobal = {
  addEventListener: (
    type: "message",
    listener: (event: MessageEvent<EngineRequest>) => void,
  ) => void;
  postMessage: (message: EngineResponse) => void;
};

type GeneratedModuleImport =
  | EmscriptenModuleFactory
  | {
      default?: EmscriptenModuleFactory;
    };

const workerGlobal = self as unknown as WorkerGlobal;

let core: WasmCore | null = null;
let currentPosition: WasmPosition | null = null;

workerGlobal.addEventListener("message", (event) => {
  void handleRequest(event.data);
});

async function handleRequest(request: EngineRequest): Promise<void> {
  try {
    const snapshot = await dispatchRequest(request);
    workerGlobal.postMessage({
      id: request.id,
      command: request.command,
      ok: true,
      snapshot,
    });
  } catch (error) {
    workerGlobal.postMessage({
      id: request.id,
      command: request.command,
      ok: false,
      error: {
        message: toAppErrorMessage(error),
      },
    });
  }
}

async function dispatchRequest(request: EngineRequest): Promise<BoardSnapshot> {
  switch (request.command) {
    case "init":
      return initialize();
    case "reset":
      return reset();
    case "applyMove":
      return applyMove(request.squareIndex);
  }
}

async function initialize(): Promise<BoardSnapshot> {
  const engine = await getCore();
  if (currentPosition === null) {
    currentPosition = engine.initialPosition();
  }
  return snapshotFromPosition(engine, currentPosition);
}

async function reset(): Promise<BoardSnapshot> {
  const engine = await getCore();
  currentPosition = engine.initialPosition();
  return snapshotFromPosition(engine, currentPosition);
}

async function applyMove(squareIndex: number): Promise<BoardSnapshot> {
  const engine = await getCore();
  if (currentPosition === null) {
    currentPosition = engine.initialPosition();
  }

  const result = engine.applyMove(currentPosition, squareIndex);
  currentPosition = result.position;
  return snapshotFromPosition(engine, currentPosition);
}

async function getCore(): Promise<WasmCore> {
  if (core !== null) {
    return core;
  }

  core = await loadCore();
  return core;
}

async function loadCore(): Promise<WasmCore> {
  const baseUrl = import.meta.env.BASE_URL;
  const moduleUrl = `${baseUrl}wasm/vibe_othello_wasm_module.mjs`;

  let imported: GeneratedModuleImport;
  try {
    imported = (await import(/* @vite-ignore */ moduleUrl)) as GeneratedModuleImport;
  } catch (error) {
    throw new Error(
      [
        `WASM runtime module was not found at ${moduleUrl}.`,
        "Build vibe_othello_wasm_module and copy the generated .mjs and .wasm files into apps/web/public/wasm/.",
        error instanceof Error ? `Original error: ${error.message}` : undefined,
      ]
        .filter(Boolean)
        .join(" "),
    );
  }

  const createModule = typeof imported === "function" ? imported : imported.default;
  if (typeof createModule !== "function") {
    throw new Error(`WASM runtime module at ${moduleUrl} does not export an Emscripten factory.`);
  }

  return WasmCore.create(async (): Promise<EmscriptenModule> => {
    try {
      return await createModule({
        locateFile: (path) => `${baseUrl}wasm/${path}`,
      });
    } catch (error) {
      throw new Error(
        [
          "WASM runtime module loaded, but engine initialization failed.",
          `Expected companion file at ${baseUrl}wasm/vibe_othello_wasm_module.wasm.`,
          error instanceof Error ? `Original error: ${error.message}` : undefined,
        ]
          .filter(Boolean)
          .join(" "),
      );
    }
  });
}

function snapshotFromPosition(engine: WasmCore, position: WasmPosition): BoardSnapshot {
  return snapshotFromQuery(position, engine.queryPosition(position));
}

function snapshotFromQuery(position: WasmPosition, query: WasmPositionQuery): BoardSnapshot {
  return {
    cells: cellsFromPosition(position),
    sideToMove: position.sideToMove,
    legalMoves: indexesFromBitboard(query.legalMoves),
    hasLegalMove: query.hasLegalMove,
    isTerminal: query.isTerminal,
  };
}

function cellsFromPosition(position: WasmPosition): BoardCell[] {
  const playerColor = position.sideToMove;
  const opponentColor = opposite(playerColor);
  const cells: BoardCell[] = Array.from({ length: 64 }, () => null);

  for (let squareIndex = 0; squareIndex < 64; squareIndex += 1) {
    const square = 1n << BigInt(squareIndex);
    if ((position.player & square) !== 0n) {
      cells[squareIndex] = playerColor;
    } else if ((position.opponent & square) !== 0n) {
      cells[squareIndex] = opponentColor;
    }
  }

  return cells;
}

function indexesFromBitboard(bitboard: bigint): number[] {
  const indexes: number[] = [];
  for (let squareIndex = 0; squareIndex < 64; squareIndex += 1) {
    if ((bitboard & (1n << BigInt(squareIndex))) !== 0n) {
      indexes.push(squareIndex);
    }
  }
  return indexes;
}

function opposite(color: DiscColor): DiscColor {
  return color === "black" ? "white" : "black";
}

function toAppErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim() !== "") {
    return error.message;
  }
  return "Engine worker failed with an unknown error.";
}
