export type DiscColor = "black" | "white";
export type BoardCell = DiscColor | null;

export interface BoardSnapshot {
  cells: BoardCell[];
  sideToMove: DiscColor;
  legalMoves: number[];
  hasLegalMove: boolean;
  isTerminal: boolean;
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
    };

export type EngineCommand = EngineRequestPayload["command"];
export type EngineRequest = EngineRequestPayload & { id: number };

export type EngineResponse =
  | {
      id: number;
      command: EngineCommand;
      ok: true;
      snapshot: BoardSnapshot;
    }
  | {
      id: number;
      command: EngineCommand;
      ok: false;
      error: {
        message: string;
      };
    };
