import { useEffect, useRef, useState } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import { Download, Maximize2, Activity, Plus, X, Search } from "lucide-react";
import { engineering, type CircuitDocument } from "./model";
import {
  schematicSignals,
  signalColor,
  signalLabel,
  hasVoltageProbe,
} from "./probes";
import type { Complex, RunResult } from "./simulation";
export function download(name: string, text: string, type = "text/plain") {
  const a = document.createElement("a");
  const url = URL.createObjectURL(new Blob([text], { type }));
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
export function Plot({
  output,
  probes,
  onProbes,
  theme,
  stale,
  doc,
  highlightedSignal,
  onHighlightSignal,
}: {
  output: RunResult | null;
  probes: string[];
  onProbes: (p: string[]) => void;
  theme: string;
  stale: boolean;
  doc: CircuitDocument;
  highlightedSignal: string | null;
  onHighlightSignal: (signal: string | null) => void;
}) {
  const host = useRef<HTMLDivElement>(null),
    chart = useRef<uPlot | null>(null);
  const [tab, setTab] = useState<"magnitude" | "phase">("magnitude"),
    [reset, setReset] = useState(0),
    [showSignals, setShowSignals] = useState(false),
    [signalSearch, setSignalSearch] = useState("");
  const picker = useRef<HTMLDivElement>(null);
  const addButton = useRef<HTMLButtonElement>(null);
  useEffect(() => {
    setShowSignals(false);
    setSignalSearch("");
  }, [doc.id]);
  useEffect(() => {
    if (!showSignals) return;
    picker.current
      ?.querySelector<HTMLInputElement>('input[type="search"]')
      ?.focus();
    const close = (event: PointerEvent) => {
      if (
        !picker.current?.contains(event.target as Node) &&
        !addButton.current?.contains(event.target as Node)
      )
        setShowSignals(false);
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setShowSignals(false);
        addButton.current?.focus();
      }
    };
    document.addEventListener("pointerdown", close);
    document.addEventListener("keydown", escape);
    return () => {
      document.removeEventListener("pointerdown", close);
      document.removeEventListener("keydown", escape);
    };
  }, [showSignals]);
  const result = output?.result;
  const maps = { ...result?.voltages, ...result?.currents };
  const available = Object.keys(maps);
  const choices = doc.mode === "schematic" ? schematicSignals(doc) : available;
  const active = probes.filter((p) => available.includes(p));
  const x = result?.time ?? result?.frequency ?? [];
  const arrays = active.map((key) => {
    const values = maps[key];
    if (!Array.isArray(values)) return [];
    return values.map((v) =>
      typeof v === "number"
        ? v
        : tab === "phase"
          ? (Math.atan2(v.imag, v.real) * 180) / Math.PI
          : 20 * Math.log10(Math.max(1e-30, Math.hypot(v.real, v.imag))),
    );
  });
  useEffect(() => {
    if (
      !host.current ||
      !result ||
      output?.mode === "dc" ||
      !x.length ||
      !active.length
    )
      return;
    const container = host.current;
    const dark = theme === "dark";
    const ink = dark ? "#899b99" : "#82908e";
    const grid = dark ? "#273431" : "#e8edeb";
    const chartOptions: uPlot.Options = {
      width: container.clientWidth || 600,
      height: Math.max(90, (container.clientHeight || 220) - 30),
      pxAlign: 0,
      padding: [20, 20, 0, 0],
      legend: { show: true, live: true },
      cursor: { drag: { x: true, y: false, setScale: true } },
      scales: {
        x: { time: false, ...(result.frequency ? { distr: 3, log: 10 } : {}) },
        voltage: { auto: true },
        current: { auto: true },
      },
      series: [
        {
          label: result.frequency ? "Frequency" : "Time",
          value: (_u, v) => engineering(v, result.frequency ? "Hz" : "s"),
        },
        ...active.map((key) => ({
          label: signalLabel(key),
          stroke: signalColor(doc, key),
          width: 1.8,
          scale: key.startsWith("i(") ? "current" : "voltage",
          points: { show: false },
          value: (_u: uPlot, v: number) =>
            engineering(
              v,
              output?.mode === "ac"
                ? tab === "phase"
                  ? "°"
                  : key.startsWith("i(")
                    ? "dBA"
                    : "dBV"
                : key.startsWith("i(")
                  ? "A"
                  : "V",
              4,
            ),
        })),
      ],
      axes: [
        {
          stroke: ink,
          grid: { show: false },
          ticks: { show: true, stroke: grid },
          font: "13px ui-monospace, monospace",
          values: (_u, values) =>
            values.map((v) => engineering(v, result.frequency ? "Hz" : "s")),
          size: 38,
        },
        ...(["voltage", "current"] as const)
          .filter((scale) =>
            active.some(
              (k) => (k.startsWith("i(") ? "current" : "voltage") === scale,
            ),
          )
          .map((scale, i) => ({
            scale,
            side: (i === 0 ? 3 : 1) as 3 | 1,
            stroke: ink,
            grid: { stroke: grid, width: 1 },
            ticks: { show: false },
            font: "13px ui-monospace, monospace",
            size: 72,
            values: (_u: uPlot, values: number[]) =>
              values.map((v) =>
                engineering(
                  v,
                  output?.mode === "ac"
                    ? tab === "phase"
                      ? "°"
                      : scale === "current"
                        ? "dBA"
                        : "dBV"
                    : scale === "current"
                      ? "A"
                      : "V",
                ),
              ),
          })),
      ],
    };
    const u = new uPlot(chartOptions, [x, ...arrays], container);
    chart.current = u;
    const observer = new ResizeObserver(() => {
      u.setSize({
        width: container.clientWidth,
        height: Math.max(90, container.clientHeight - 30),
      });
    });
    observer.observe(container);
    return () => {
      observer.disconnect();
      u.destroy();
      chart.current = null;
    };
  }, [
    output,
    probes.join("|"),
    JSON.stringify(doc.probeColors),
    theme,
    tab,
    reset,
  ]);
  useEffect(() => {
    const index = highlightedSignal ? active.indexOf(highlightedSignal) : -1;
    chart.current?.setSeries(index < 0 ? null : index + 1, { focus: true });
  }, [highlightedSignal, output, probes.join("|")]);
  function csv() {
    if (!result) return;
    const keys = available;
    if (output?.mode === "dc") {
      download(
        "operating-point.csv",
        "signal,value,unit\n" +
          keys
            .map((k) => `${k},${maps[k]},${k.startsWith("i(") ? "A" : "V"}`)
            .join("\n"),
        "text/csv",
      );
      return;
    }
    const ac = !!result.frequency;
    const header = [
      ac ? "frequency_Hz" : "time_s",
      ...keys.flatMap((k) => (ac ? [`${k}_real`, `${k}_imag`] : [k])),
    ];
    const rows = x.map((value, i) =>
      [
        value,
        ...keys.flatMap((k) => {
          const val = (maps[k] as (number | Complex)[])[i];
          return typeof val === "number" ? [val] : [val.real, val.imag];
        }),
      ].join(","),
    );
    download(
      "simulation.csv",
      [header.join(","), ...rows].join("\n"),
      "text/csv",
    );
  }
  return (
    <section className="results" aria-label="Simulation results">
      <div className="results-heading">
        <div className="section-heading">
          <Activity size={15} />
          <span>{output?.mode === "dc" ? "Operating point" : "Waveforms"}</span>
          {stale && output && (
            <span className="badge warning">Out of date</span>
          )}
        </div>
        <div className="result-actions">
          {output?.mode === "ac" && (
            <div className="segmented">
              <button
                className={tab === "magnitude" ? "active" : ""}
                onClick={() => setTab("magnitude")}
              >
                Magnitude
              </button>
              <button
                className={tab === "phase" ? "active" : ""}
                onClick={() => setTab("phase")}
              >
                Phase
              </button>
            </div>
          )}
          <button
            ref={addButton}
            className="add-signal-button"
            aria-expanded={showSignals}
            onClick={() => {
              setShowSignals(!showSignals);
              setSignalSearch("");
            }}
          >
            <Plus size={14} /> Add signal
          </button>
          {output && (
            <>
              <button
                className="icon-button"
                title="Reset plot zoom"
                aria-label="Reset plot zoom"
                onClick={() => setReset(reset + 1)}
              >
                <Maximize2 size={15} />
              </button>
              <button
                className="icon-button"
                title="Export results as CSV"
                aria-label="Export results as CSV"
                onClick={csv}
              >
                <Download size={15} />
              </button>
            </>
          )}
        </div>
      </div>
      {probes.length > 0 && (
        <div className="trace-chips" aria-label="Plotted signals">
          {probes.map((signal) => (
            <div
              key={signal}
              data-trace={signal}
              className={`trace-chip ${highlightedSignal === signal ? "highlighted" : ""}`}
              style={{ color: signalColor(doc, signal) }}
              onPointerEnter={() => onHighlightSignal(signal)}
              onPointerLeave={() => onHighlightSignal(null)}
            >
              <button
                className="trace-name"
                onFocus={() => onHighlightSignal(signal)}
                onBlur={() => onHighlightSignal(null)}
                onClick={() =>
                  onHighlightSignal(
                    highlightedSignal === signal ? null : signal,
                  )
                }
                title={
                  available.includes(signal)
                    ? signalLabel(signal)
                    : "Run to see this signal"
                }
              >
                <i />
                {signalLabel(signal)}
                {!available.includes(signal) && (
                  <span className="trace-pending">Ready to plot</span>
                )}
              </button>
              <button
                className="trace-remove"
                aria-label={`Remove ${signalLabel(signal)}`}
                title={`Remove ${signalLabel(signal)}`}
                onClick={() => onProbes(probes.filter((p) => p !== signal))}
              >
                <X size={12} />
              </button>
            </div>
          ))}
        </div>
      )}
      {showSignals && (
        <div
          className="signal-picker"
          ref={picker}
          role="dialog"
          aria-label="Add signal"
        >
          <label className="signal-search">
            <Search size={14} />
            <input
              type="search"
              aria-label="Find a signal"
              placeholder="Find a signal…"
              value={signalSearch}
              onChange={(e) => setSignalSearch(e.target.value)}
            />
          </label>
          <div className="signal-options">
            {choices
              .filter((key) =>
                `${signalLabel(key)} ${key}`
                  .toLowerCase()
                  .includes(signalSearch.toLowerCase()),
              )
              .map((key) => (
                <label
                  key={key}
                  className={
                    hasVoltageProbe(doc, key) ? "signal-already-added" : ""
                  }
                >
                  <input
                    disabled={hasVoltageProbe(doc, key)}
                    type="checkbox"
                    checked={probes.includes(key)}
                    onChange={() =>
                      onProbes(
                        probes.includes(key)
                          ? probes.filter((p) => p !== key)
                          : [...probes, key],
                      )
                    }
                  />
                  <span>{signalLabel(key)}</span>
                  <small>{key.startsWith("v(") ? "Voltage" : "Current"}</small>
                </label>
              ))}
            {!choices.length && (
              <p className="quiet small">
                {doc.mode === "netlist"
                  ? "Run the netlist to discover its signals."
                  : "Connect a circuit to see its signals."}
              </p>
            )}
          </div>
          {doc.mode === "schematic" && (
            <p className="signal-picker-tip">
              Or hover a wire to plot voltage or insert a current probe.
            </p>
          )}
        </div>
      )}
      {!output ? (
        <div className="plot-empty">
          <Activity size={30} />
          <strong>A circuit is only the beginning.</strong>
          <span>Run a simulation to see what happens.</span>
        </div>
      ) : output.mode === "dc" ? (
        <div className="dc-table">
          <table>
            <thead>
              <tr>
                <th>Signal</th>
                <th>Value</th>
              </tr>
            </thead>
            <tbody>
              {available.map((key) => (
                <tr key={key}>
                  <td>
                    <i style={{ background: signalColor(doc, key) }} />
                    {signalLabel(key)}
                  </td>
                  <td>
                    {engineering(
                      maps[key] as number,
                      key.startsWith("i(") ? "A" : "V",
                      6,
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      ) : (
        <>
          {active.length === 0 && (
            <div className="plot-empty">
              Click a wire or choose “Add signal” to see its waveform.
            </div>
          )}
          <div
            className="plot-host"
            ref={host}
            style={{ display: active.length ? "block" : "none" }}
          />
        </>
      )}
      <div className="results-footer">
        <span>
          {output
            ? output.mode === "dc"
              ? "DC operating point"
              : `${x.length.toLocaleString()} samples · ${output.mode === "ac" ? "Frequency domain" : "Time domain"}`
            : "DC · AC · Transient"}
        </span>
        <span>
          {output
            ? `${(output.result.status.elapsedSeconds * 1000).toFixed(2)} ms solve time`
            : "Powered by neospice / WebAssembly"}
        </span>
      </div>
    </section>
  );
}
