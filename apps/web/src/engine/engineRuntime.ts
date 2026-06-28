import { WasmCore } from "@vibe-othello/wasm-core";
import type {
  EmscriptenModule,
  EmscriptenModuleFactory,
  WasmPosition,
  WasmSearchResult,
} from "@vibe-othello/wasm-core";

import { snapshotFromPosition, snapshotFromQuery, indexesFromBitboard } from "./boardSnapshot";
import { CPU_SEARCH_LIMITS } from "./cpuPolicy";
import { DefaultEvaluationArtifactLoader } from "./evalArtifactLoader";
import type { BoardSnapshot, CpuMoveResult, SearchSummary } from "./protocol";

type GeneratedModuleImport =
  | EmscriptenModuleFactory
  | {
      default?: EmscriptenModuleFactory;
    };

export interface EngineRuntime {
  initialize(): Promise<BoardSnapshot>;
  reset(): Promise<BoardSnapshot>;
  applyMove(squareIndex: number): Promise<BoardSnapshot>;
  applyPass(): Promise<BoardSnapshot>;
  cpuMove(): Promise<CpuMoveResult>;
}

export class WasmEngineRuntime implements EngineRuntime {
  private core: WasmCore | null = null;
  private currentPosition: WasmPosition | null = null;

  constructor(
    private readonly baseUrl: string,
    private readonly artifactLoader = new DefaultEvaluationArtifactLoader(baseUrl),
  ) {}

  async initialize(): Promise<BoardSnapshot> {
    const engine = await this.getCore();
    if (this.currentPosition === null) {
      this.currentPosition = engine.initialPosition();
    }
    return snapshotFromPosition(engine, this.currentPosition);
  }

  async reset(): Promise<BoardSnapshot> {
    const engine = await this.getCore();
    this.currentPosition = engine.initialPosition();
    return snapshotFromPosition(engine, this.currentPosition);
  }

  async applyMove(squareIndex: number): Promise<BoardSnapshot> {
    const engine = await this.getCore();
    const position = this.ensureCurrentPosition(engine);

    const result = engine.applyMove(position, squareIndex);
    this.currentPosition = result.position;
    return snapshotFromPosition(engine, this.currentPosition);
  }

  async applyPass(): Promise<BoardSnapshot> {
    const engine = await this.getCore();
    const position = this.ensureCurrentPosition(engine);

    const result = engine.applyPass(position);
    this.currentPosition = result.position;
    return snapshotFromPosition(engine, this.currentPosition);
  }

  async cpuMove(): Promise<CpuMoveResult> {
    const engine = await this.getCore();
    const positionBefore = this.ensureCurrentPosition(engine);
    const queryBefore = engine.queryPosition(positionBefore);
    if (queryBefore.isTerminal) {
      throw new Error("Cannot run CPU move on a terminal position.");
    }

    const sideToMoveBefore = positionBefore.sideToMove;
    if (!queryBefore.hasLegalMove) {
      const passResult = engine.applyPass(positionBefore);
      this.currentPosition = passResult.position;
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

    const artifact = await this.artifactLoader.load(engine);
    const searchResult = artifact.searchBestMove(positionBefore, CPU_SEARCH_LIMITS);
    if (!searchResult.hasBestMove) {
      throw new Error("CPU search did not return a best move.");
    }

    const search = searchSummaryFromResult(searchResult);
    if (searchResult.isPass) {
      const passResult = engine.applyPass(positionBefore);
      this.currentPosition = passResult.position;
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
    this.currentPosition = moveResult.position;
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

  private async getCore(): Promise<WasmCore> {
    if (this.core !== null) {
      return this.core;
    }

    this.core = await this.loadCore();
    return this.core;
  }

  private ensureCurrentPosition(engine: WasmCore): WasmPosition {
    if (this.currentPosition === null) {
      this.currentPosition = engine.initialPosition();
    }
    return this.currentPosition;
  }

  private async loadCore(): Promise<WasmCore> {
    const moduleUrl = `${this.baseUrl}wasm/vibe_othello_wasm_module.mjs`;

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
          locateFile: (path) => `${this.baseUrl}wasm/${path}`,
        });
      } catch (error) {
        throw new Error(
          [
            "WASM runtime module loaded, but engine initialization failed.",
            `Expected companion file at ${this.baseUrl}wasm/vibe_othello_wasm_module.wasm.`,
            error instanceof Error ? `Original error: ${error.message}` : undefined,
          ]
            .filter(Boolean)
            .join(" "),
        );
      }
    });
  }
}

export function createDefaultEngineRuntime(): EngineRuntime {
  return new WasmEngineRuntime(import.meta.env.BASE_URL);
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
