import type { WasmCore, WasmPosition, WasmPositionQuery } from "@vibe-othello/wasm-core";

import type { BoardCell, BoardSnapshot, DiscColor } from "./protocol";

export function snapshotFromPosition(engine: WasmCore, position: WasmPosition): BoardSnapshot {
  return snapshotFromQuery(position, engine.queryPosition(position));
}

export function snapshotFromQuery(position: WasmPosition, query: WasmPositionQuery): BoardSnapshot {
  return {
    cells: cellsFromPosition(position),
    sideToMove: position.sideToMove,
    legalMoves: indexesFromBitboard(query.legalMoves),
    hasLegalMove: query.hasLegalMove,
    isTerminal: query.isTerminal,
  };
}

export function indexesFromBitboard(bitboard: bigint): number[] {
  const indexes: number[] = [];
  for (let squareIndex = 0; squareIndex < 64; squareIndex += 1) {
    if ((bitboard & (1n << BigInt(squareIndex))) !== 0n) {
      indexes.push(squareIndex);
    }
  }
  return indexes;
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

function opposite(color: DiscColor): DiscColor {
  return color === "black" ? "white" : "black";
}
