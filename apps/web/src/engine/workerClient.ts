import type {
  BoardSnapshot,
  CpuDifficulty,
  CpuMoveResult,
  EngineRequest,
  EngineRequestPayload,
  EngineResponse,
  EngineSuccessResponse,
} from "../workers/protocol";

type PendingRequest = {
  resolve: (response: EngineSuccessResponse) => void;
  reject: (error: Error) => void;
};

export class EngineWorkerClient {
  private readonly worker: Worker;
  private nextRequestId = 1;
  private readonly pending = new Map<number, PendingRequest>();

  constructor() {
    this.worker = new Worker(new URL("../workers/engine.worker.ts", import.meta.url), {
      type: "module",
    });
    this.worker.addEventListener("message", (event: MessageEvent<EngineResponse>) => {
      this.handleResponse(event.data);
    });
    this.worker.addEventListener("error", (event) => {
      this.rejectAll(new Error(event.message || "Engine worker script failed."));
    });
    this.worker.addEventListener("messageerror", () => {
      this.rejectAll(new Error("Engine worker returned an unreadable message."));
    });
  }

  init(): Promise<BoardSnapshot> {
    return this.requestSnapshot({ command: "init" });
  }

  reset(): Promise<BoardSnapshot> {
    return this.requestSnapshot({ command: "reset" });
  }

  applyMove(squareIndex: number): Promise<BoardSnapshot> {
    return this.requestSnapshot({ command: "applyMove", squareIndex });
  }

  applyPass(): Promise<BoardSnapshot> {
    return this.requestSnapshot({ command: "applyPass" });
  }

  async cpuMove(difficulty: CpuDifficulty): Promise<CpuMoveResult> {
    const response = await this.request({ command: "cpuMove", difficulty });
    if (response.cpuMove === undefined) {
      throw new Error("Engine worker returned a CPU move response without a summary.");
    }
    return {
      snapshot: response.snapshot,
      cpuMove: response.cpuMove,
    };
  }

  dispose(): void {
    this.rejectAll(new Error("Engine worker client was disposed."));
    this.worker.terminate();
  }

  private async requestSnapshot(payload: EngineRequestPayload): Promise<BoardSnapshot> {
    const response = await this.request(payload);
    return response.snapshot;
  }

  private request(payload: EngineRequestPayload): Promise<EngineSuccessResponse> {
    const id = this.nextRequestId;
    this.nextRequestId += 1;

    const request: EngineRequest = { ...payload, id };

    return new Promise<EngineSuccessResponse>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.worker.postMessage(request);
    });
  }

  private handleResponse(response: EngineResponse): void {
    const pending = this.pending.get(response.id);
    if (pending === undefined) {
      return;
    }

    this.pending.delete(response.id);
    if (response.ok) {
      pending.resolve(response);
      return;
    }

    pending.reject(new Error(response.error.message));
  }

  private rejectAll(error: Error): void {
    for (const pending of this.pending.values()) {
      pending.reject(error);
    }
    this.pending.clear();
  }
}

export function createEngineWorkerClient(): EngineWorkerClient {
  return new EngineWorkerClient();
}
