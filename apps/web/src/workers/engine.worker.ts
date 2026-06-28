import { createDefaultEngineRuntime, type EngineRuntime } from "../engine/engineRuntime";
import type { BoardSnapshot, CpuMoveResult, EngineRequest, EngineResponse } from "../engine/protocol";

type WorkerGlobal = {
  addEventListener: (
    type: "message",
    listener: (event: MessageEvent<EngineRequest>) => void,
  ) => void;
  postMessage: (message: EngineResponse) => void;
};

type DispatchResult = BoardSnapshot | CpuMoveResult;

const workerGlobal = self as unknown as WorkerGlobal;
const runtime = createDefaultEngineRuntime();

let requestQueue: Promise<void> = Promise.resolve();

workerGlobal.addEventListener("message", (event) => {
  requestQueue = requestQueue.then(() => handleRequest(event.data)).catch(() => undefined);
});

async function handleRequest(request: EngineRequest): Promise<void> {
  try {
    const result = await dispatchRequest(runtime, request);
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

async function dispatchRequest(
  engineRuntime: EngineRuntime,
  request: EngineRequest,
): Promise<DispatchResult> {
  switch (request.command) {
    case "init":
      return engineRuntime.initialize();
    case "reset":
      return engineRuntime.reset();
    case "applyMove":
      return engineRuntime.applyMove(request.squareIndex);
    case "applyPass":
      return engineRuntime.applyPass();
    case "cpuMove":
      return engineRuntime.cpuMove();
  }
}

function toAppErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim() !== "") {
    return error.message;
  }
  return "Engine worker failed with an unknown error.";
}
