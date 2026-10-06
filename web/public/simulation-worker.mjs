import { createNeospice } from "./simulator/neospice-api.mjs";
let diagnostics = [];
const api = createNeospice({
  print: (line) => {
    if (diagnostics.length < 30) diagnostics.push(String(line));
  },
  printErr: (line) => {
    if (diagnostics.length < 30) diagnostics.push(String(line));
  },
});
api
  .then(() => self.postMessage({ type: "ready" }))
  .catch((error) =>
    self.postMessage({
      type: "load-error",
      error: String(error.message ?? error),
    }),
  );
self.onmessage = async ({ data }) => {
  if (data.type !== "run") return;
  let circuit;
  try {
    const { Circuit } = await api;
    diagnostics = [];
    circuit = new Circuit(data.netlist);
    const { mode, options } = data.analysis;
    const result =
      mode === "dc"
        ? circuit.dc()
        : mode === "ac"
          ? circuit.ac(options)
          : circuit.transient(options);
    if (!result.status.converged)
      throw new Error(
        "The circuit did not converge. Check its connections, bias, and analysis settings.",
      );
    self.postMessage({
      type: "result",
      id: data.id,
      revision: data.revision,
      mode,
      result,
    });
  } catch (error) {
    self.postMessage({
      type: "error",
      id: data.id,
      revision: data.revision,
      error: String(error.message ?? error),
      diagnostics,
    });
  } finally {
    circuit?.dispose();
  }
};
