export type DiscColor = "black" | "white";
export type BoardCell = DiscColor | null;

export interface BoardSnapshot {
  cells: BoardCell[];
  sideToMove: DiscColor;
  legalMoves: number[];
  hasLegalMove: boolean;
  isTerminal: boolean;
}

export interface SearchSummary {
  score: number;
  completedDepth: number;
  nodes: bigint;
  elapsedMs: number;
  stopped: boolean;
  exact: boolean;
}

export interface CpuMoveSummary {
  sideToMoveBefore: DiscColor;
  kind: "move" | "pass";
  squareIndex: number | null;
  search: SearchSummary | null;
}

export interface CpuMoveResult {
  snapshot: BoardSnapshot;
  cpuMove: CpuMoveSummary;
}

export type EngineRequestPayload =
  | {
      command: "init";
    }
  | {
      command: "reset";
    }
  | {
      command: "applyMove";
      squareIndex: number;
    }
  | {
      command: "applyPass";
    }
  | {
      command: "cpuMove";
    };

export type EngineCommand = EngineRequestPayload["command"];
export type EngineRequest = EngineRequestPayload & { id: number };

export interface EngineSuccessResponse {
  id: number;
  command: EngineCommand;
  ok: true;
  snapshot: BoardSnapshot;
  cpuMove?: CpuMoveSummary;
}

export type EngineResponse =
  | EngineSuccessResponse
  | {
      id: number;
      command: EngineCommand;
      ok: false;
      error: {
        message: string;
      };
    };
