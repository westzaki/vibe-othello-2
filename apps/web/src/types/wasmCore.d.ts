declare module "@vibe-othello/wasm-core" {
  export type WasmSideToMove = "black" | "white";

  export interface WasmPosition {
    player: bigint;
    opponent: bigint;
    sideToMove: WasmSideToMove;
  }

  export interface WasmPositionQuery {
    legalMoves: bigint;
    hasLegalMove: boolean;
    isTerminal: boolean;
  }

  export interface WasmApplyMoveResult extends WasmPositionQuery {
    position: WasmPosition;
    flipped: bigint;
  }

  export interface EmscriptenModule {
    HEAPU8: Uint8Array;
    [name: string]: unknown;
  }

  export interface EmscriptenModuleOptions {
    locateFile?: (path: string, prefix?: string) => string;
  }

  export type EmscriptenModuleFactory = (
    options?: EmscriptenModuleOptions,
  ) => EmscriptenModule | Promise<EmscriptenModule>;

  export class WasmCore {
    static create(
      createModuleOrModule:
        | EmscriptenModule
        | Promise<EmscriptenModule>
        | (() => EmscriptenModule | Promise<EmscriptenModule>),
    ): Promise<WasmCore>;

    initialPosition(): WasmPosition;
    queryPosition(position: WasmPosition): WasmPositionQuery;
    applyMove(position: WasmPosition, squareIndex: number): WasmApplyMoveResult;
    applyPass(position: WasmPosition): WasmApplyMoveResult;
  }
}
