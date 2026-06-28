import { WasmCore } from "@vibe-othello/wasm-core";
import type {
  EmscriptenModule,
  EmscriptenModuleFactory,
  WasmEvaluationArtifact,
  WasmPosition,
  WasmPositionQuery,
  WasmSearchResult,
} from "@vibe-othello/wasm-core";

import type {
  BoardCell,
  BoardSnapshot,
  CpuMoveResult,
  DiscColor,
  EngineRequest,
  EngineResponse,
  SearchSummary,
} from "./protocol";

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

type DispatchResult = BoardSnapshot | CpuMoveResult;
type JsonObject = Record<string, unknown>;

const CPU_SEARCH_LIMITS = {
  maxDepth: 2,
  maxNodes: 0,
  maxTimeMs: 500,
} as const;

const workerGlobal = self as unknown as WorkerGlobal;

let core: WasmCore | null = null;
let currentPosition: WasmPosition | null = null;
let defaultEvaluationArtifact: WasmEvaluationArtifact | null = null;
let defaultEvaluationArtifactPromise: Promise<WasmEvaluationArtifact> | null = null;
let requestQueue: Promise<void> = Promise.resolve();

workerGlobal.addEventListener("message", (event) => {
  requestQueue = requestQueue.then(() => handleRequest(event.data)).catch(() => undefined);
});

async function handleRequest(request: EngineRequest): Promise<void> {
  try {
    const result = await dispatchRequest(request);
    const snapshot = "cpuMove" in result ? result.snapshot : result;
    workerGlobal.postMessage({
      id: request.id,
      command: request.command,
      ok: true,
      snapshot,
      ...("cpuMove" in result ? { cpuMove: result.cpuMove } : {}),
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

async function dispatchRequest(request: EngineRequest): Promise<DispatchResult> {
  switch (request.command) {
    case "init":
      return initialize();
    case "reset":
      return reset();
    case "applyMove":
      return applyMove(request.squareIndex);
    case "applyPass":
      return applyPass();
    case "cpuMove":
      return cpuMove();
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
  const position = ensureCurrentPosition(engine);

  const result = engine.applyMove(position, squareIndex);
  currentPosition = result.position;
  return snapshotFromPosition(engine, currentPosition);
}

async function applyPass(): Promise<BoardSnapshot> {
  const engine = await getCore();
  const position = ensureCurrentPosition(engine);

  const result = engine.applyPass(position);
  currentPosition = result.position;
  return snapshotFromPosition(engine, currentPosition);
}

async function cpuMove(): Promise<CpuMoveResult> {
  const engine = await getCore();
  const positionBefore = ensureCurrentPosition(engine);
  const queryBefore = engine.queryPosition(positionBefore);
  if (queryBefore.isTerminal) {
    throw new Error("Cannot run CPU move on a terminal position.");
  }

  const sideToMoveBefore = positionBefore.sideToMove;
  if (!queryBefore.hasLegalMove) {
    const passResult = engine.applyPass(positionBefore);
    currentPosition = passResult.position;
    return {
      snapshot: snapshotFromQuery(passResult.position, passResult),
      cpuMove: {
        sideToMoveBefore,
        kind: "pass",
        squareIndex: null,
        search: null,
      },
    };
  }

  const artifact = await getDefaultEvaluationArtifact(engine);
  const searchResult = artifact.searchBestMove(positionBefore, CPU_SEARCH_LIMITS);
  if (!searchResult.hasBestMove) {
    throw new Error("CPU search did not return a best move.");
  }

  const search = searchSummaryFromResult(searchResult);
  if (searchResult.isPass) {
    const passResult = engine.applyPass(positionBefore);
    currentPosition = passResult.position;
    return {
      snapshot: snapshotFromQuery(passResult.position, passResult),
      cpuMove: {
        sideToMoveBefore,
        kind: "pass",
        squareIndex: null,
        search,
      },
    };
  }

  const squareIndex = searchResult.bestMoveSquare;
  if (
    squareIndex === null ||
    !Number.isInteger(squareIndex) ||
    squareIndex < 0 ||
    squareIndex >= 64
  ) {
    throw new Error("CPU search returned an invalid best move square.");
  }

  const legalMoves = indexesFromBitboard(queryBefore.legalMoves);
  if (!legalMoves.includes(squareIndex)) {
    throw new Error(`CPU search returned illegal move square ${squareIndex}.`);
  }

  const moveResult = engine.applyMove(positionBefore, squareIndex);
  currentPosition = moveResult.position;
  return {
    snapshot: snapshotFromQuery(moveResult.position, moveResult),
    cpuMove: {
      sideToMoveBefore,
      kind: "move",
      squareIndex,
      search,
    },
  };
}

async function getCore(): Promise<WasmCore> {
  if (core !== null) {
    return core;
  }

  core = await loadCore();
  return core;
}

function ensureCurrentPosition(engine: WasmCore): WasmPosition {
  if (currentPosition === null) {
    currentPosition = engine.initialPosition();
  }
  return currentPosition;
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

async function getDefaultEvaluationArtifact(engine: WasmCore): Promise<WasmEvaluationArtifact> {
  if (defaultEvaluationArtifact !== null) {
    return defaultEvaluationArtifact;
  }

  if (defaultEvaluationArtifactPromise === null) {
    defaultEvaluationArtifactPromise = loadDefaultEvaluationArtifact(engine)
      .then((artifact) => {
        defaultEvaluationArtifact = artifact;
        return artifact;
      })
      .catch((error: unknown) => {
        defaultEvaluationArtifactPromise = null;
        throw error;
      });
  }

  return defaultEvaluationArtifactPromise;
}

async function loadDefaultEvaluationArtifact(engine: WasmCore): Promise<WasmEvaluationArtifact> {
  const evalRootUrl = new URL(
    "eval/",
    new URL(import.meta.env.BASE_URL, globalThis.location.origin),
  );
  const defaultPointerUrl = new URL("default-artifact.json", evalRootUrl);
  const pointerText = await fetchText(defaultPointerUrl, "default evaluation artifact pointer");
  const pointer = parseJsonObject(pointerText, "default evaluation artifact pointer");
  const manifestPath = requiredStringField(
    pointer,
    "artifact_manifest",
    "default evaluation artifact pointer",
  );

  const manifestUrl = new URL(manifestPath, evalRootUrl);
  const manifestText = await fetchText(manifestUrl, "evaluation artifact manifest");
  const manifest = parseJsonObject(manifestText, "evaluation artifact manifest");
  const weightsPath = requiredStringField(manifest, "weights_file", "evaluation artifact manifest");

  const weightsUrl = new URL(weightsPath, manifestUrl);
  const weightsBytes = await fetchBytes(weightsUrl, "evaluation artifact weights");
  return engine.loadEvaluationArtifact(manifestText, weightsBytes);
}

async function fetchText(url: URL, description: string): Promise<string> {
  const response = await fetchReadable(url, description);
  return response.text();
}

async function fetchBytes(url: URL, description: string): Promise<Uint8Array> {
  const response = await fetchReadable(url, description);
  return new Uint8Array(await response.arrayBuffer());
}

async function fetchReadable(url: URL, description: string): Promise<Response> {
  let response: Response;
  try {
    response = await fetch(url);
  } catch (error) {
    throw new Error(
      [
        `Failed to fetch ${description} from ${url.href}.`,
        error instanceof Error ? `Original error: ${error.message}` : undefined,
      ]
        .filter(Boolean)
        .join(" "),
    );
  }

  if (!response.ok) {
    throw new Error(
      `Failed to fetch ${description} from ${url.href}: ${response.status} ${response.statusText}`,
    );
  }
  return response;
}

function parseJsonObject(text: string, description: string): JsonObject {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch (error) {
    throw new Error(
      [
        `Failed to parse ${description} as JSON.`,
        error instanceof Error ? `Original error: ${error.message}` : undefined,
      ]
        .filter(Boolean)
        .join(" "),
    );
  }

  if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw new Error(`${description} must be a JSON object.`);
  }
  return parsed as JsonObject;
}

function requiredStringField(object: JsonObject, fieldName: string, description: string): string {
  const value = object[fieldName];
  if (typeof value !== "string" || value.trim() === "") {
    throw new Error(`${description} is missing string field "${fieldName}".`);
  }
  return value;
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

function searchSummaryFromResult(result: WasmSearchResult): SearchSummary {
  return {
    score: result.score,
    completedDepth: result.completedDepth,
    nodes: result.nodes,
    elapsedMs: result.elapsedMs,
    stopped: result.stopped,
    exact: result.exact,
  };
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
