import { useEffect, useMemo, useRef, useState } from "react";

import { createEngineWorkerClient, type EngineWorkerClient } from "./engine/workerClient";
import type { BoardCell, BoardSnapshot } from "./workers/protocol";

type EngineStatus = "loading" | "ready" | "error";

const BOARD_SIZE = 8;
const DISPLAY_SQUARES = Array.from({ length: 64 }, (_, displayIndex) => {
  const displayRank = Math.floor(displayIndex / BOARD_SIZE);
  const file = displayIndex % BOARD_SIZE;
  const rank = BOARD_SIZE - 1 - displayRank;
  return rank * BOARD_SIZE + file;
});

function App() {
  const clientRef = useRef<EngineWorkerClient | null>(null);
  const [status, setStatus] = useState<EngineStatus>("loading");
  const [snapshot, setSnapshot] = useState<BoardSnapshot | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    const client = createEngineWorkerClient();
    let active = true;
    clientRef.current = client;

    setStatus("loading");
    setError(null);
    client
      .init()
      .then((nextSnapshot) => {
        if (!active) {
          return;
        }
        setSnapshot(nextSnapshot);
        setStatus("ready");
      })
      .catch((initError: unknown) => {
        if (!active) {
          return;
        }
        setError(errorMessage(initError));
        setStatus("error");
      });

    return () => {
      active = false;
      clientRef.current = null;
      client.dispose();
    };
  }, []);

  const legalMoveSet = useMemo(() => new Set(snapshot?.legalMoves ?? []), [snapshot]);
  const canPass =
    status === "ready" &&
    !busy &&
    snapshot !== null &&
    snapshot.hasLegalMove === false &&
    snapshot.isTerminal === false;
  const shouldShowPassNotice =
    snapshot !== null && snapshot.hasLegalMove === false && snapshot.isTerminal === false;

  async function reset() {
    const client = clientRef.current;
    if (client === null) {
      return;
    }

    setBusy(true);
    setNotice(null);
    setError(null);
    try {
      setSnapshot(await client.reset());
      setStatus("ready");
    } catch (resetError) {
      setError(errorMessage(resetError));
      setStatus("error");
    } finally {
      setBusy(false);
    }
  }

  async function applyMove(squareIndex: number) {
    const client = clientRef.current;
    if (client === null || snapshot === null || busy) {
      return;
    }

    if (!legalMoveSet.has(squareIndex)) {
      setNotice("That square is not legal for the side to move.");
      return;
    }

    setBusy(true);
    setNotice(null);
    setError(null);
    try {
      setSnapshot(await client.applyMove(squareIndex));
      setStatus("ready");
    } catch (moveError) {
      setError(errorMessage(moveError));
      setStatus("error");
    } finally {
      setBusy(false);
    }
  }

  async function applyPass() {
    const client = clientRef.current;
    if (client === null || snapshot === null || busy || !canPass) {
      return;
    }

    setBusy(true);
    setNotice(null);
    setError(null);
    try {
      setSnapshot(await client.applyPass());
      setStatus("ready");
    } catch (passError) {
      setError(errorMessage(passError));
      setStatus("error");
    } finally {
      setBusy(false);
    }
  }

  return (
    <main className="app">
      <header className="app__header">
        <div>
          <h1>Vibe Othello</h1>
          <p className="app__status">Engine: {statusLabel(status, busy)}</p>
        </div>
        <div className="app__actions">
          <button
            className="app__button"
            type="button"
            onClick={reset}
            disabled={busy || status === "loading"}
          >
            Reset
          </button>
          {snapshot !== null ? (
            <button className="app__button" type="button" onClick={applyPass} disabled={!canPass}>
              Pass
            </button>
          ) : null}
        </div>
      </header>

      {error !== null ? <p className="app__error">{error}</p> : null}
      {notice !== null ? <p className="app__notice">{notice}</p> : null}
      {shouldShowPassNotice ? (
        <p className="app__notice">No legal move for the side to move. Pass is available.</p>
      ) : null}

      <section className="game" aria-label="Othello board">
        <div className="board" role="grid" aria-label="Board">
          {DISPLAY_SQUARES.map((squareIndex) => (
            <BoardSquare
              key={squareIndex}
              cell={snapshot?.cells[squareIndex] ?? null}
              isLegal={legalMoveSet.has(squareIndex)}
              isDisabled={busy || status !== "ready"}
              squareIndex={squareIndex}
              onPlay={applyMove}
            />
          ))}
        </div>

        <aside className="summary" aria-label="Position summary">
          <div>
            <span className="summary__label">Side</span>
            <strong>{snapshot?.sideToMove ?? "black"}</strong>
          </div>
          <div>
            <span className="summary__label">Legal moves</span>
            <strong>{snapshot?.legalMoves.length ?? 0}</strong>
          </div>
          <div>
            <span className="summary__label">State</span>
            <strong>{stateLabel(snapshot)}</strong>
          </div>
        </aside>
      </section>
    </main>
  );
}

interface BoardSquareProps {
  cell: BoardCell;
  isLegal: boolean;
  isDisabled: boolean;
  squareIndex: number;
  onPlay: (squareIndex: number) => void;
}

function BoardSquare({ cell, isLegal, isDisabled, squareIndex, onPlay }: BoardSquareProps) {
  const coordinate = coordinateFromSquareIndex(squareIndex);
  const className = ["board__square", isLegal ? "board__square--legal" : ""]
    .filter(Boolean)
    .join(" ");

  return (
    <button
      className={className}
      type="button"
      role="gridcell"
      aria-label={`${coordinate}${isLegal ? " legal move" : ""}`}
      disabled={isDisabled}
      onClick={() => onPlay(squareIndex)}
    >
      {cell !== null ? <span className={`disc disc--${cell}`} aria-label={cell} /> : null}
      {cell === null && isLegal ? <span className="board__legal-marker" /> : null}
    </button>
  );
}

function coordinateFromSquareIndex(squareIndex: number): string {
  const file = String.fromCharCode("a".charCodeAt(0) + (squareIndex % BOARD_SIZE));
  const rank = Math.floor(squareIndex / BOARD_SIZE) + 1;
  return `${file}${rank}`;
}

function statusLabel(status: EngineStatus, busy: boolean): string {
  if (busy) {
    return "working";
  }
  if (status === "loading") {
    return "loading";
  }
  if (status === "error") {
    return "error";
  }
  return "ready";
}

function stateLabel(snapshot: BoardSnapshot | null): string {
  if (snapshot === null) {
    return "loading";
  }
  if (snapshot.isTerminal) {
    return "terminal";
  }
  return snapshot.hasLegalMove ? "playable" : "no legal move";
}

function errorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim() !== "") {
    return error.message;
  }
  return "Unexpected engine error.";
}

export default App;
