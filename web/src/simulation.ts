import { useCallback, useEffect, useRef, useState } from "react";
import type { Analysis } from "./model";
export type Complex = { real: number; imag: number };
export type Result = {
  time?: number[];
  frequency?: number[];
  voltages: Record<string, number | number[] | Complex[]>;
  currents: Record<string, number | number[] | Complex[]>;
  status: {
    converged: boolean;
    elapsedSeconds: number;
    iterations: number;
    warnings: string[];
  };
};
export type SimulationRequest = {
  type: "run";
  id: number;
  revision: number;
  netlist: string;
  analysis: { mode: Analysis["mode"]; options: Record<string, unknown> };
};
export type SimulationResponse =
  | { type: "ready" }
  | { type: "load-error"; error: string }
  | {
      type: "result";
      id: number;
      revision: number;
      mode: Analysis["mode"];
      result: Result;
    }
  | {
      type: "error";
      id: number;
      revision: number;
      error: string;
      diagnostics: string[];
    };
export type RunResult = {
  result: Result;
  revision: number;
  mode: Analysis["mode"];
};
export function useSimulation() {
  const [ready, setReady] = useState(false),
    [busy, setBusy] = useState(false),
    [error, setError] = useState(""),
    [output, setOutput] = useState<RunResult | null>(null);
  const worker = useRef<Worker | null>(null),
    timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined),
    id = useRef(0),
    loaded = useRef(false),
    running = useRef(false),
    pending = useRef<SimulationRequest | null>(null),
    latest = useRef(0);
  const launch = useRef<() => void>(() => {});
  const send = useCallback((request: SimulationRequest) => {
    running.current = true;
    setBusy(true);
    worker.current!.postMessage(request);
    clearTimeout(timer.current);
    timer.current = setTimeout(() => {
      pending.current = null;
      setError(
        "Simulation stopped after 30 seconds. Try a shorter time window or larger step, then run again.",
      );
      launch.current();
    }, 30000);
  }, []);
  const start = useCallback(() => {
    clearTimeout(timer.current);
    worker.current?.terminate();
    loaded.current = false;
    running.current = false;
    setReady(false);
    setBusy(false);
    const w = new Worker(
      new URL(
        `${import.meta.env.BASE_URL}simulation-worker.mjs`,
        window.location.origin,
      ),
      { type: "module" },
    );
    worker.current = w;
    const fail = (message: string) => {
      if (worker.current !== w) return;
      clearTimeout(timer.current);
      w.terminate();
      loaded.current = false;
      running.current = false;
      setReady(false);
      setBusy(false);
      setError(message);
    };
    timer.current = setTimeout(
      () =>
        fail(
          "The simulator could not load within 30 seconds. Check your connection and retry.",
        ),
      30000,
    );
    w.onerror = (e) =>
      fail(
        e.message ||
          "The simulator could not load. Retry to reload its assets.",
      );
    w.onmessage = ({ data }: { data: SimulationResponse }) => {
      if (worker.current !== w) return;
      if (data.type === "load-error") {
        fail(`Could not load WebAssembly: ${data.error}`);
        return;
      }
      if (data.type === "ready") {
        clearTimeout(timer.current);
        loaded.current = true;
        setReady(true);
        if (pending.current) {
          const next = pending.current;
          pending.current = null;
          send(next);
        }
        return;
      }
      clearTimeout(timer.current);
      running.current = false;
      setBusy(false);
      if (data.id === latest.current) {
        if (data.type === "error") setError(data.error);
        else {
          setError("");
          setOutput({
            result: data.result,
            revision: data.revision,
            mode: data.mode,
          });
        }
      }
      if (pending.current) {
        const next = pending.current;
        pending.current = null;
        send(next);
      }
    };
  }, [send]);
  launch.current = start;
  useEffect(() => {
    start();
    return () => {
      clearTimeout(timer.current);
      worker.current?.terminate();
    };
  }, [start]);
  const run = useCallback(
    (
      netlist: string,
      analysis: SimulationRequest["analysis"],
      revision: number,
    ) => {
      setError("");
      const request: SimulationRequest = {
        type: "run",
        id: ++id.current,
        revision,
        netlist,
        analysis,
      };
      latest.current = request.id;
      if (!loaded.current || running.current) {
        pending.current = request;
        if (!loaded.current) start();
      } else send(request);
    },
    [send, start],
  );
  const cancel = useCallback(() => {
    pending.current = null;
    latest.current = ++id.current;
    setError("");
    start();
  }, [start]);
  return {
    ready,
    busy,
    error,
    output,
    run,
    cancel,
    retry: start,
    clear: () => {
      setOutput(null);
      setError("");
    },
    setError,
  };
}
