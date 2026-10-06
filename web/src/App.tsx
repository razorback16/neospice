import { useCallback, useEffect, useRef, useState } from "react";
import {
  Activity,
  ArrowDownToLine,
  ArrowUpFromLine,
  BookOpen,
  Check,
  ChevronDown,
  ChevronRight,
  Code2,
  Copy,
  FolderOpen,
  Grid2X2,
  Lightbulb,
  LoaderCircle,
  Menu,
  MoreHorizontal,
  Pause,
  Play,
  Plus,
  Redo2,
  RotateCcw,
  Search,
  Settings2,
  SlidersHorizontal,
  Sun,
  Moon,
  Undo2,
  X,
  Zap,
} from "lucide-react";
import { GALLERY, type Example } from "./gallery";
import {
  PARTS,
  COMPONENT_DRAG_TYPE,
  addPart,
  blankDocument,
  clone,
  deleteSelection,
  engineering,
  parseValue,
  readDocument,
  uid,
  type Analysis,
  type CircuitDocument,
  type Component,
  type Kind,
  type ProbeAnchor,
} from "./model";
import { analysisOptions, compile, validateText } from "./netlist";
import { Canvas, type Tool } from "./Canvas";
import { MiniSymbol } from "./Symbol";
import { PlacementCursor } from "./PlacementCursor";
import { Plot, download } from "./Plot";
import { useSimulation } from "./simulation";
import {
  addVoltageProbe,
  insertCurrentProbe,
  removeProbe,
  reconcileProbes,
} from "./probes";
const STORAGE = "neospice-lab-v1";
function stored() {
  try {
    const raw = JSON.parse(localStorage.getItem(STORAGE) || "{}");
    const projects: Record<string, CircuitDocument> = {};
    for (const [id, value] of Object.entries(raw.projects || {})) {
      try {
        projects[id] = readDocument(JSON.stringify(value));
      } catch {}
    }
    return { projects, current: raw.current as string | undefined };
  } catch {
    return {
      projects: {} as Record<string, CircuitDocument>,
      current: undefined,
    };
  }
}
function fromExample(e: Example) {
  return { ...clone(e.document), id: `circuit_${uid()}` };
}
function initial() {
  const saved = stored();
  const hash = decodeURIComponent(location.hash.slice(1));
  const example = GALLERY.find((e) => e.id === hash);
  const current = saved.current ? saved.projects[saved.current] : undefined;
  return example
    ? current?.exampleId === example.id
      ? current
      : fromExample(example)
    : (current ?? fromExample(GALLERY[0]));
}
const groupNames: Partial<Record<Kind, string>> = {
  R: "Passives",
  V: "Sources",
  D: "Semiconductors",
  OP: "Building blocks",
  G: "Connections",
};
function Field({
  label,
  value,
  onChange,
  unit,
  help,
}: {
  label: string;
  value: string;
  onChange: (v: string) => void;
  unit?: string;
  help?: string;
}) {
  return (
    <label className="field">
      <span>{label}</span>
      <div className="input-unit">
        <input
          aria-label={label}
          value={value}
          onChange={(e) => onChange(e.target.value)}
          spellCheck={false}
        />
        {unit && <span>{unit}</span>}
      </div>
      {help && <small>{help}</small>}
    </label>
  );
}
function Inspector({
  doc,
  part,
  onChange,
}: {
  doc: CircuitDocument;
  part: Component;
  onChange: (d: CircuitDocument) => void;
}) {
  const update = (key: string, value: string) => {
    const next = clone(doc);
    next.components.find((c) => c.id === part.id)!.props[key] = value;
    onChange(next);
  };
  const p = part.props;
  const source = part.kind === "V" || part.kind === "I";
  const modelKind =
    part.kind === "D"
      ? "D"
      : part.kind === "QN"
        ? "NPN"
        : part.kind === "QP"
          ? "PNP"
          : part.kind === "MN"
            ? "NMOS"
            : "PMOS";
  return (
    <>
      <div className="inspector-part">
        <MiniSymbol kind={part.kind} />
        <div>
          <span className="eyebrow">{PARTS[part.kind].name}</span>
          <h2>{part.id}</h2>
        </div>
      </div>
      {p.value !== undefined && (
        <Field
          label={part.kind === "OP" ? "Open-loop gain" : "Value"}
          value={p.value}
          unit={PARTS[part.kind].unit}
          onChange={(v) => update("value", v)}
          help="SPICE notation: 1k, 4.7u, 10meg. m means milli."
        />
      )}
      {source && (
        <>
          <Field
            label="DC value"
            value={p.dc}
            unit={PARTS[part.kind].unit}
            onChange={(v) => update("dc", v)}
          />
          <div className="field-row">
            <Field
              label="AC magnitude"
              value={p.ac}
              onChange={(v) => update("ac", v)}
            />
            <Field
              label="AC phase"
              value={p.phase}
              unit="°"
              onChange={(v) => update("phase", v)}
            />
          </div>
          <label className="field">
            <span>Transient waveform</span>
            <select
              value={p.wave}
              onChange={(e) => update("wave", e.target.value)}
            >
              <option value="dc">Constant (DC)</option>
              <option value="sin">Sine wave</option>
              <option value="pulse">Pulse</option>
            </select>
          </label>
          {p.wave === "sin" && (
            <>
              <Field
                label="Offset"
                value={p.offset}
                onChange={(v) => update("offset", v)}
              />
              <Field
                label="Amplitude"
                value={p.amplitude}
                onChange={(v) => update("amplitude", v)}
              />
              <Field
                label="Frequency"
                value={p.frequency}
                unit="Hz"
                onChange={(v) => update("frequency", v)}
              />
            </>
          )}
          {p.wave === "pulse" && (
            <>
              {[
                ["low", "Low level"],
                ["high", "High level"],
                ["delay", "Delay"],
                ["rise", "Rise time"],
                ["fall", "Fall time"],
                ["width", "Pulse width"],
                ["period", "Period"],
              ].map(([key, label]) => (
                <Field
                  key={key}
                  label={label}
                  value={p[key]}
                  onChange={(v) => update(key, v)}
                  unit={
                    ["low", "high"].includes(key) ? PARTS[part.kind].unit : "s"
                  }
                />
              ))}
            </>
          )}
        </>
      )}
      {p.model && (
        <>
          <label className="field">
            <span>Device model</span>
            <select
              value={p.model}
              onChange={(e) => update("model", e.target.value)}
            >
              {Object.entries(doc.models)
                .filter(([, text]) =>
                  new RegExp(
                    `^\\s*\\.model\\s+\\S+\\s+${modelKind}(?:\\s|\\(|$)`,
                    "i",
                  ).test(text),
                )
                .map(([name]) => (
                  <option key={name}>{name}</option>
                ))}
            </select>
          </label>
          {p.w && (
            <div className="field-row">
              <Field
                label="Width"
                value={p.w}
                unit="m"
                onChange={(v) => update("w", v)}
              />
              <Field
                label="Length"
                value={p.l}
                unit="m"
                onChange={(v) => update("l", v)}
              />
            </div>
          )}
          <details className="model-details">
            <summary>Inline model card</summary>
            <textarea
              aria-label="Inline model card"
              value={doc.models[p.model] ?? ""}
              spellCheck={false}
              onChange={(e) => {
                const next = clone(doc);
                next.models[p.model] = e.target.value;
                onChange(next);
              }}
            />
          </details>
        </>
      )}
      {part.kind === "OP" && (
        <div className="note">
          Finite-gain ideal VCVS. No supply rails, saturation, or bandwidth
          limit.
        </div>
      )}
      <div className="pin-list">
        <span className="eyebrow">TERMINALS</span>
        {PARTS[part.kind].pins.map((pin) => (
          <div key={pin.id}>
            <span>{pin.id}</span>
            <code>
              {part.id}.{pin.id}
            </code>
          </div>
        ))}
      </div>
    </>
  );
}
function AnalysisControls({
  analysis,
  onChange,
}: {
  analysis: Analysis;
  onChange: (a: Analysis) => void;
}) {
  const update = (key: keyof Analysis, value: string | number) =>
    onChange({ ...analysis, [key]: value });
  return (
    <div className="analysis-controls">
      <div className="segmented analysis-modes">
        {(["transient", "ac", "dc"] as const).map((mode) => (
          <button
            key={mode}
            className={analysis.mode === mode ? "active" : ""}
            onClick={() => update("mode", mode)}
          >
            {mode === "transient"
              ? "Transient"
              : mode === "ac"
                ? "AC sweep"
                : "DC point"}
          </button>
        ))}
      </div>
      {analysis.mode === "transient" ? (
        <div className="field-row">
          <Field
            label="Time step"
            value={analysis.step}
            unit="s"
            onChange={(v) => update("step", v)}
          />
          <Field
            label="Stop time"
            value={analysis.stop}
            unit="s"
            onChange={(v) => update("stop", v)}
          />
        </div>
      ) : analysis.mode === "ac" ? (
        <>
          <div className="field-row">
            <Field
              label="Start frequency"
              value={analysis.start}
              unit="Hz"
              onChange={(v) => update("start", v)}
            />
            <Field
              label="Stop frequency"
              value={analysis.end}
              unit="Hz"
              onChange={(v) => update("end", v)}
            />
          </div>
          <div className="field-row">
            <label className="field">
              <span>Sweep</span>
              <select
                aria-label="AC sweep mode"
                value={analysis.sweep}
                onChange={(e) => update("sweep", e.target.value)}
              >
                <option value="dec">Decade</option>
                <option value="oct">Octave</option>
                <option value="lin">Linear</option>
              </select>
            </label>
            <Field
              label={analysis.sweep === "lin" ? "Points" : "Points / interval"}
              value={String(analysis.points)}
              onChange={(v) => update("points", Number(v))}
            />
          </div>
        </>
      ) : (
        <p className="quiet small">
          Find the steady-state node voltages and branch currents.
        </p>
      )}
    </div>
  );
}
export default function App() {
  const [doc, setDoc] = useState<CircuitDocument>(initial),
    [revision, setRevision] = useState(0),
    [selected, setSelected] = useState<string | null>(null),
    [tool, setTool] = useState<Tool>("select");
  const [draggingPart, setDraggingPart] = useState(false);
  const [placementOrigin, setPlacementOrigin] = useState({
    x: 0,
    y: 0,
    touch: false,
  });
  const [highlightedSignal, setHighlightedSignal] = useState<string | null>(
    null,
  );
  const [theme, setTheme] = useState(
      () => document.documentElement.dataset.theme || "light",
    ),
    [library, setLibrary] = useState<"examples" | "components" | "saved">(
      "examples",
    ),
    [search, setSearch] = useState(""),
    [view, setView] = useState<"schematic" | "netlist">("schematic"),
    [mobile, setMobile] = useState<"canvas" | "library" | "inspector">(
      "canvas",
    ),
    [menu, setMenu] = useState(false),
    [help, setHelp] = useState(false),
    [storageError, setStorageError] = useState("");
  useEffect(() => {
    if (!help) return;
    const previous = document.activeElement as HTMLElement | null;
    const modal = document.querySelector<HTMLElement>(".help-modal");
    modal?.querySelector<HTMLButtonElement>("button")?.focus();
    const keyboard = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        event.preventDefault();
        setHelp(false);
      }
      if (event.key !== "Tab" || !modal) return;
      const focusable = [
        ...modal.querySelectorAll<HTMLElement>("button, a[href]"),
      ];
      const first = focusable[0],
        last = focusable.at(-1);
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last?.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first?.focus();
      }
    };
    window.addEventListener("keydown", keyboard);
    return () => {
      window.removeEventListener("keydown", keyboard);
      previous?.focus();
    };
  }, [help]);
  const [history, setHistory] = useState<CircuitDocument[]>([]),
    [future, setFuture] = useState<CircuitDocument[]>([]),
    [savedProjects, setSavedProjects] = useState(() => stored().projects);
  const [plotHeight, setPlotHeight] = useState(285),
    [inspectorOpen, setInspectorOpen] = useState(true),
    [libraryOpen, setLibraryOpen] = useState(true);
  const docRef = useRef(doc);
  docRef.current = doc;
  const revisionRef = useRef(revision);
  revisionRef.current = revision;
  const file = useRef<HTMLInputElement>(null);
  const sliderTimer = useRef<ReturnType<typeof setTimeout> | undefined>(
    undefined,
  );
  const autoRan = useRef(false);
  const sim = useSimulation();
  const example = GALLERY.find((e) => e.id === doc.exampleId);
  const part = doc.components.find((c) => c.id === selected);
  const junction = doc.junctions.find((j) => j.id === selected);
  const wire = doc.wires.find((w) => w.id === selected);
  const projectsRef = useRef(savedProjects);
  const persist = useCallback((nextDocument: CircuitDocument) => {
    const projects = { ...projectsRef.current };
    delete projects[nextDocument.id];
    projects[nextDocument.id] = nextDocument;
    projectsRef.current = projects;
    setSavedProjects(projects);
    try {
      localStorage.setItem(
        STORAGE,
        JSON.stringify({ current: nextDocument.id, projects }),
      );
      setStorageError("");
    } catch {
      setStorageError(
        "Local saving is unavailable. Export your project to keep a copy.",
      );
    }
  }, []);
  const commit = useCallback((next: CircuitDocument) => {
    next = reconcileProbes(docRef.current, next);
    if (JSON.stringify(next) === JSON.stringify(docRef.current)) return;
    const previous = clone(docRef.current);
    let electricalChange = previous.id !== next.id;
    try {
      electricalChange ||=
        compile(previous, false).text !== compile(next, false).text;
    } catch {
      electricalChange = true;
    }
    setHistory((h) => [...h.slice(-99), previous]);
    setFuture([]);
    docRef.current = next;
    persist(next);
    setDoc(next);
    if (electricalChange) {
      revisionRef.current += 1;
      setRevision(revisionRef.current);
    }
  }, []);
  const change = useCallback(
    (next: CircuitDocument) => {
      clearTimeout(sliderTimer.current);
      commit(next);
    },
    [commit],
  );
  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    try {
      localStorage.setItem("neospice-theme", theme);
    } catch {}
  }, [theme]);
  useEffect(() => persist(doc), [doc, persist]);
  const run = useCallback(() => {
    clearTimeout(sliderTimer.current);
    try {
      const current = docRef.current;
      const { text } = compile(current);
      validateText(text);
      sim.run(
        text,
        {
          mode: current.analysis.mode,
          options: analysisOptions(current.analysis),
        },
        revisionRef.current,
      );
    } catch (e) {
      sim.setError((e as Error).message);
    }
  }, [sim.run]);
  useEffect(() => {
    if (sim.ready && !autoRan.current) {
      autoRan.current = true;
      run();
    }
  }, [sim.ready, run]);
  useEffect(() => () => clearTimeout(sliderTimer.current), []);
  const load = (next: CircuitDocument) => {
    historyReplace(next.exampleId || "");
    clearTimeout(sliderTimer.current);
    sim.cancel();
    sim.clear();
    change(next);
    setSelected(null);
    setTool("select");
    setView(next.mode === "netlist" ? "netlist" : "schematic");
    setMobile("canvas");
    setMenu(false);
  };
  const loadExample = (e: Example) => {
    historyReplace(e.id);
    load(fromExample(e));
  };
  function historyReplace(id: string) {
    window.history.replaceState(
      null,
      "",
      `${location.pathname}${location.search}${id ? "#" + id : ""}`,
    );
  }
  useEffect(() => {
    const onHash = () => {
      const found = GALLERY.find(
        (e) => e.id === decodeURIComponent(location.hash.slice(1)),
      );
      if (found) load(fromExample(found));
    };
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);
  const undo = () => {
    const prev = history.at(-1);
    if (!prev) return;
    clearTimeout(sliderTimer.current);
    setFuture((f) => [...f, doc]);
    setHistory((h) => h.slice(0, -1));
    persist(prev);
    setDoc(prev);
    setRevision((v) => v + 1);
  };
  const redo = () => {
    const next = future.at(-1);
    if (!next) return;
    clearTimeout(sliderTimer.current);
    setHistory((h) => [...h, doc]);
    setFuture((f) => f.slice(0, -1));
    persist(next);
    setDoc(next);
    setRevision((v) => v + 1);
  };
  const rotate = () => {
    if (!part) return;
    const next = clone(doc);
    next.components.find((c) => c.id === part.id)!.rotation =
      (part.rotation + 90) % 360;
    change(next);
  };
  const duplicate = () => {
    if (!part) return;
    const next = clone(doc);
    const copy = addPart(next, part.kind, { x: part.x + 60, y: part.y + 60 });
    copy.rotation = part.rotation;
    copy.props = { ...part.props };
    next.components.push(copy);
    change(next);
    setSelected(copy.id);
  };
  const remove = () => {
    if (selected) {
      change(deleteSelection(doc, selected));
      setSelected(null);
    }
  };
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (help) return;
      if (e.key === "Escape") {
        setTool("select");
        setDraggingPart(false);
        return;
      }
      if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
        e.preventDefault();
        run();
        return;
      }
      if (
        (e.target as HTMLElement).closest(
          "input,textarea,select,[contenteditable]",
        )
      )
        return;
      if (e.key === "/") {
        e.preventDefault();
        document.querySelector<HTMLInputElement>(".search input")?.focus();
      }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") {
        e.preventDefault();
        e.shiftKey ? redo() : undo();
        return;
      }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "d") {
        e.preventDefault();
        duplicate();
        return;
      }
      if (e.key === "Delete" || e.key === "Backspace") {
        e.preventDefault();
        remove();
      }
      if (e.key.toLowerCase() === "r") rotate();
      if (e.key.toLowerCase() === "w") setTool("wire");
      if (e.key.toLowerCase() === "v") setTool("select");
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  });
  let generated = "";
  try {
    generated = compile(doc, false).text;
  } catch (e) {
    generated = `* ${(e as Error).message}`;
  }
  const updateProbes = (probes: string[]) => {
    let next = docRef.current;
    const removedSensor = next.wires.some(
      (w) =>
        w.currentProbe &&
        !probes.includes(`i(${w.currentProbe.id.toLowerCase()})`),
    );
    for (const signal of next.probes)
      if (!probes.includes(signal)) next = removeProbe(next, signal);
    for (const signal of probes)
      if (!next.probes.includes(signal)) next = addVoltageProbe(next, signal);
    change(next);
    setHighlightedSignal(null);
    if (removedSensor && sim.output) sliderTimer.current = setTimeout(run, 100);
  };
  const probe = (signal: string, anchor?: ProbeAnchor) => {
    change(addVoltageProbe(docRef.current, signal, anchor));
    setHighlightedSignal(signal);
  };
  const currentProbe = (wireId: string, position: number) => {
    try {
      const next = insertCurrentProbe(docRef.current, wireId, position);
      change(next);
      setHighlightedSignal(next.probes.at(-1) ?? null);
      sim.setError("");
      if (sim.output) sliderTimer.current = setTimeout(run, 100);
    } catch (error) {
      sim.setError((error as Error).message);
    }
  };
  const deleteProbe = (signal: string) => {
    const hasSensor = docRef.current.wires.some(
      (w) =>
        w.currentProbe && `i(${w.currentProbe.id.toLowerCase()})` === signal,
    );
    change(removeProbe(docRef.current, signal));
    setHighlightedSignal(null);
    if (hasSensor && sim.output) sliderTimer.current = setTimeout(run, 100);
  };
  const reverseProbe = (signal: string) => {
    const next = clone(docRef.current);
    const wire = next.wires.find(
      (w) =>
        w.currentProbe && `i(${w.currentProbe.id.toLowerCase()})` === signal,
    );
    if (!wire?.currentProbe) return;
    wire.currentProbe.reversed = !wire.currentProbe.reversed;
    change(next);
    if (sim.output) sliderTimer.current = setTimeout(run, 100);
  };
  const tune = (partId: string, property: string, value: number) => {
    const next = clone(docRef.current);
    next.components.find((c) => c.id === partId)!.props[property] = String(
      Number(value.toPrecision(6)),
    );
    commit(next);
    clearTimeout(sliderTimer.current);
    sliderTimer.current = setTimeout(run, 250);
  };
  const safeValue = (value: string) => {
    try {
      return parseValue(value);
    } catch {
      return 0;
    }
  };
  const exportProject = () => {
    download(
      `${doc.title.replace(/[^\w-]+/g, "-").toLowerCase()}.neospice.json`,
      JSON.stringify(doc, null, 2),
      "application/json",
    );
    setMenu(false);
  };
  const exportNetlist = () => {
    try {
      download(
        `${doc.title.replace(/[^\w-]+/g, "-").toLowerCase()}.cir`,
        compile(doc).text,
      );
    } catch (e) {
      sim.setError((e as Error).message);
    }
    setMenu(false);
  };
  const importFile = async (f: File) => {
    try {
      if (f.size > 2_000_000)
        throw new Error("Files must be smaller than 2 MB.");
      const text = await f.text();
      if (f.name.endsWith(".json"))
        load({ ...readDocument(text), id: `circuit_${uid()}` });
      else {
        const next = blankDocument();
        next.title = f.name.replace(/\.[^.]+$/, "");
        next.mode = "netlist";
        next.text = text;
        load(next);
      }
      historyReplace("");
    } catch (e) {
      sim.setError((e as Error).message);
    }
  };
  const netlistMode = doc.mode === "netlist" || view === "netlist";
  return (
    <div
      className={`app ${Object.hasOwn(PARTS, tool) ? "placing-component" : ""} ${libraryOpen ? "" : "library-collapsed"} ${inspectorOpen ? "" : "inspector-collapsed"} mobile-${mobile}`}
      style={{ "--plot-height": `${plotHeight}px` } as React.CSSProperties}
    >
      {Object.hasOwn(PARTS, tool) && !draggingPart && (
        <PlacementCursor kind={tool as Kind} origin={placementOrigin} />
      )}
      <header className="app-header">
        <a
          className="brand"
          href="#"
          onClick={(e) => {
            e.preventDefault();
            loadExample(GALLERY[0]);
          }}
          aria-label="neospice home"
        >
          <span className="brand-mark">
            <Activity size={24} />
          </span>
          <span>
            neo<span className="brand-light">spice</span>
            <sup>LAB</sup>
          </span>
        </a>
        <div className="header-divider" />
        <span className="header-description">
          A little space for big circuit ideas.
        </span>
        <div className="header-actions">
          <span className="local-badge">
            <span className="live-dot" /> Simulation is running in the broswer
          </span>
          <button
            className="icon-button"
            aria-label="Keyboard shortcuts and help"
            title="Quick guide"
            onClick={() => setHelp(true)}
          >
            <BookOpen size={18} />
          </button>
          <button
            className="icon-button theme-button"
            aria-label={
              theme === "light"
                ? "Switch to dark theme"
                : "Switch to light theme"
            }
            title="Toggle theme"
            onClick={() => setTheme(theme === "light" ? "dark" : "light")}
          >
            {theme === "light" ? <Moon size={18} /> : <Sun size={18} />}
          </button>
          <a
            className="github-link"
            href="https://github.com/razorback16/neospice"
            target="_blank"
            rel="noreferrer"
          >
            GitHub <span>↗</span>
          </a>
        </div>
      </header>
      <nav className="mobile-nav" aria-label="Workspace panels">
        {(["library", "canvas", "inspector"] as const).map((p) => (
          <button
            className={mobile === p ? "active" : ""}
            key={p}
            onClick={() => setMobile(p)}
          >
            {p === "canvas"
              ? "Circuit"
              : p === "library"
                ? "Library"
                : "Settings"}
          </button>
        ))}
      </nav>
      <aside className="library">
        <div className="library-top">
          <span className="eyebrow">WORKSPACE</span>
          <button
            className="icon-button"
            aria-label="Collapse library"
            onClick={() => setLibraryOpen(false)}
          >
            <Menu size={16} />
          </button>
        </div>
        <div className="library-tabs">
          <button
            className={library === "examples" ? "active" : ""}
            onClick={() => {
              setLibrary("examples");
              setSearch("");
            }}
          >
            <Grid2X2 size={14} /> Examples
          </button>
          <button
            className={library === "components" ? "active" : ""}
            onClick={() => {
              setLibrary("components");
              setSearch("");
            }}
          >
            <Plus size={15} /> Components
          </button>
        </div>
        <label className="search">
          <Search size={15} />
          <input
            aria-label={
              library === "components" ? "Search components" : "Search circuits"
            }
            placeholder={
              library === "components" ? "Find a component…" : "Find a circuit…"
            }
            value={search}
            onChange={(e) => setSearch(e.target.value)}
          />
          <kbd>/</kbd>
        </label>
        <div className="library-scroll">
          {library === "examples" ? (
            <>
              <div className="library-section-label">
                A FEW GOOD STARTING POINTS <span>08</span>
              </div>
              {GALLERY.filter((e) =>
                (e.title + " " + e.category)
                  .toLowerCase()
                  .includes(search.toLowerCase()),
              ).map((e) => (
                <button
                  key={e.id}
                  className={`example-card ${doc.exampleId === e.id ? "active" : ""}`}
                  onClick={() => loadExample(e)}
                  data-testid={`example-${e.id}`}
                >
                  <div className="example-top">
                    <span className={`example-number ${e.accent}`}>
                      {e.number}
                    </span>
                    <span className="example-category">{e.category}</span>
                    {doc.exampleId === e.id && (
                      <span className="example-active-dot" />
                    )}
                  </div>
                  <strong>{e.title}</strong>
                  <span className="example-description">{e.description}</span>
                </button>
              ))}
            </>
          ) : library === "components" ? (
            <>
              {(Object.keys(PARTS) as Kind[])
                .filter((k) =>
                  PARTS[k].name.toLowerCase().includes(search.toLowerCase()),
                )
                .map((kind) => (
                  <div key={kind}>
                    {!search && groupNames[kind] && (
                      <div className="library-section-label">
                        {groupNames[kind]?.toUpperCase()}
                      </div>
                    )}
                    <button
                      className={`part-card ${tool === kind ? "active" : ""}`}
                      data-testid={`part-${kind}`}
                      draggable={doc.mode === "schematic"}
                      onDragStart={(e) => {
                        e.dataTransfer.setData(COMPONENT_DRAG_TYPE, kind);
                        e.dataTransfer.effectAllowed = "copy";
                        const symbol = e.currentTarget.querySelector("svg");
                        if (symbol) e.dataTransfer.setDragImage(symbol, 17, 17);
                        setDraggingPart(true);
                        setSelected(null);
                        setTool(kind);
                        setView("schematic");
                      }}
                      onDragEnd={() => {
                        setDraggingPart(false);
                        setTool("select");
                      }}
                      onClick={(e) => {
                        const touch =
                          "pointerType" in e.nativeEvent &&
                          e.nativeEvent.pointerType === "touch";
                        setPlacementOrigin({
                          x: e.clientX,
                          y: e.clientY,
                          touch,
                        });
                        setDraggingPart(false);
                        setSelected(null);
                        setTool(kind);
                        setView("schematic");
                        setMobile("canvas");
                      }}
                      disabled={doc.mode === "netlist"}
                    >
                      <MiniSymbol kind={kind} />
                      <span>{PARTS[kind].name}</span>
                      <Plus size={13} />
                    </button>
                  </div>
                ))}
              <p className="library-tip">
                Click a component to pick it up, then click to place it—or drag
                it onto the canvas. Esc cancels placement.
              </p>
            </>
          ) : (
            <>
              <div className="library-section-label">SAVED ON THIS DEVICE</div>
              {Object.values(savedProjects)
                .filter((d) =>
                  d.title.toLowerCase().includes(search.toLowerCase()),
                )
                .reverse()
                .map((d) => (
                  <button
                    className={`saved-card ${doc.id === d.id ? "active" : ""}`}
                    key={d.id}
                    onClick={() => load(clone(d))}
                  >
                    <FolderOpen size={17} />
                    <div>
                      <strong>{d.title}</strong>
                      <span>
                        {d.mode === "netlist"
                          ? "SPICE netlist"
                          : `${d.components.filter((c) => c.kind !== "G").length} components`}
                      </span>
                    </div>
                  </button>
                ))}
            </>
          )}
        </div>
        <div className="library-bottom">
          <button
            className="new-circuit"
            onClick={() => {
              historyReplace("");
              load(blankDocument());
              setLibrary("components");
            }}
          >
            <Plus size={17} /> New circuit <span>↗</span>
          </button>
          <button
            className="saved-link"
            onClick={() => {
              setLibrary(library === "saved" ? "examples" : "saved");
              setSearch("");
            }}
          >
            <FolderOpen size={14} />
            {library === "saved" ? "Back to examples" : "Your saved circuits"}
            <span>{Object.keys(savedProjects).length}</span>
          </button>
        </div>
      </aside>
      <main className="workspace">
        <div className="workspace-heading">
          <div className="document-title">
            {!libraryOpen && (
              <button
                className="icon-button"
                aria-label="Open library"
                onClick={() => setLibraryOpen(true)}
              >
                <Menu size={18} />
              </button>
            )}
            <div>
              <div className="breadcrumb">
                CIRCUIT LAB <ChevronRight size={11} />{" "}
                {doc.mode === "netlist"
                  ? "NETLIST"
                  : example
                    ? "EXPLORATIONS"
                    : "YOUR WORKSPACE"}
              </div>
              <input
                aria-label="Circuit title"
                className="title-input"
                value={doc.title}
                onChange={(e) => change({ ...doc, title: e.target.value })}
              />
            </div>
          </div>
          <div className="document-actions">
            <span
              className="saved-indicator"
              title="Changes are saved on this device"
            >
              <Check size={12} /> {storageError ? "Not saved" : "Saved locally"}
            </span>
            <button
              className="icon-button"
              disabled={!history.length}
              aria-label="Undo"
              title="Undo (Ctrl+Z)"
              onClick={undo}
            >
              <Undo2 size={16} />
            </button>
            <button
              className="icon-button"
              disabled={!future.length}
              aria-label="Redo"
              title="Redo (Ctrl+Shift+Z)"
              onClick={redo}
            >
              <Redo2 size={16} />
            </button>
            <div className="menu-wrap">
              <button
                className={`icon-button ${menu ? "active" : ""}`}
                aria-label="Project menu"
                aria-expanded={menu}
                onClick={() => setMenu(!menu)}
              >
                <MoreHorizontal size={19} />
              </button>
              {menu && (
                <div className="dropdown-menu">
                  <button onClick={() => file.current?.click()}>
                    <ArrowUpFromLine size={15} /> Import project or SPICE
                  </button>
                  <button onClick={exportProject}>
                    <ArrowDownToLine size={15} /> Export project
                  </button>
                  <button onClick={exportNetlist}>
                    <Code2 size={15} /> Download SPICE
                  </button>
                  {example && (
                    <button onClick={() => loadExample(example)}>
                      <RotateCcw size={15} /> Reset example
                    </button>
                  )}
                </div>
              )}
            </div>
            {!inspectorOpen && (
              <button
                className="icon-button"
                aria-label="Open settings"
                onClick={() => setInspectorOpen(true)}
              >
                <SlidersHorizontal size={18} />
              </button>
            )}
          </div>
        </div>
        <div className="workspace-subhead">
          <div className="view-tabs">
            {doc.mode === "schematic" && (
              <button
                className={!netlistMode ? "active" : ""}
                onClick={() => setView("schematic")}
              >
                <Grid2X2 size={14} /> Schematic
              </button>
            )}
            <button
              className={netlistMode ? "active" : ""}
              onClick={() => setView("netlist")}
            >
              <Code2 size={15} />{" "}
              {doc.mode === "netlist" ? "SPICE editor" : "Netlist"}
            </button>
          </div>
          <div className="run-actions">
            <span className="analysis-label">
              {doc.analysis.mode === "transient"
                ? "Transient"
                : doc.analysis.mode === "ac"
                  ? "AC sweep"
                  : "DC operating point"}
            </span>
            {sim.busy ? (
              <button className="run-button running" onClick={sim.cancel}>
                <Pause size={14} /> Cancel
              </button>
            ) : (
              <button
                className="run-button"
                aria-label="Run simulation"
                onClick={run}
                disabled={!sim.ready && !sim.error}
              >
                {!sim.ready && !sim.error ? (
                  <LoaderCircle size={15} className="spin" />
                ) : (
                  <Play size={14} fill="currentColor" />
                )}
                {!sim.ready && !sim.error ? "Loading…" : "Run simulation"}
                <kbd>⌘ ↵</kbd>
              </button>
            )}
          </div>
        </div>
        {(sim.error || storageError) && (
          <div className="error-banner" role="alert">
            <span>{sim.error || storageError}</span>
            <button
              className="icon-button"
              aria-label="Dismiss error"
              onClick={() => {
                sim.setError("");
                setStorageError("");
              }}
            >
              <X size={15} />
            </button>
          </div>
        )}
        {sim.output?.result.status.warnings.length ? (
          <div className="warning-banner" role="status">
            {sim.output.result.status.warnings.join(" · ")}
          </div>
        ) : null}
        <div className="editor-area">
          {netlistMode ? (
            <div className="netlist-editor">
              <div className="netlist-toolbar">
                <span>
                  {doc.mode === "netlist"
                    ? "Run settings in the analysis panel determine the analysis."
                    : "Generated from your schematic · read only"}
                </span>
                {doc.mode === "schematic" && (
                  <button
                    className="text-button"
                    onClick={() => {
                      const next = blankDocument();
                      next.mode = "netlist";
                      next.title = doc.title + " — netlist";
                      next.text = generated;
                      next.analysis = { ...doc.analysis };
                      next.probes = [...doc.probes];
                      load(next);
                    }}
                  >
                    Edit a copy <Copy size={13} />
                  </button>
                )}
              </div>
              <textarea
                aria-label="SPICE netlist"
                className="netlist-text"
                value={doc.mode === "netlist" ? doc.text : generated}
                readOnly={doc.mode === "schematic"}
                spellCheck={false}
                onChange={(e) => change({ ...doc, text: e.target.value })}
              />
              <div className="netlist-footer">
                Inline models and subcircuits supported · External includes must
                be inlined
              </div>
            </div>
          ) : (
            <Canvas
              doc={doc}
              onChange={change}
              selected={selected}
              onSelect={setSelected}
              tool={tool}
              onTool={setTool}
              output={sim.output}
              stale={sim.output?.revision !== revision}
              onProbe={probe}
              onCurrentProbe={currentProbe}
              onRemoveProbe={deleteProbe}
              onReverseProbe={reverseProbe}
              highlightedSignal={highlightedSignal}
              onHighlightSignal={setHighlightedSignal}
              onRotate={rotate}
              onDuplicate={duplicate}
              onDelete={remove}
            />
          )}
        </div>
        <div
          className="panel-resizer"
          role="separator"
          aria-label="Resize waveform panel"
          aria-orientation="horizontal"
          tabIndex={0}
          onKeyDown={(e) => {
            if (e.key === "ArrowUp")
              setPlotHeight((h) => Math.min(550, h + 20));
            if (e.key === "ArrowDown")
              setPlotHeight((h) => Math.max(200, h - 20));
          }}
          onPointerDown={(e) => {
            const start = e.clientY,
              height = plotHeight;
            const handle = e.currentTarget;
            handle.setPointerCapture(e.pointerId);
            const move = (event: PointerEvent) =>
              setPlotHeight(
                Math.max(
                  200,
                  Math.min(
                    window.innerHeight * 0.6,
                    height + start - event.clientY,
                  ),
                ),
              );
            const end = () => {
              handle.removeEventListener("pointermove", move);
              handle.removeEventListener("pointerup", end);
              handle.removeEventListener("pointercancel", end);
            };
            handle.addEventListener("pointermove", move);
            handle.addEventListener("pointerup", end);
            handle.addEventListener("pointercancel", end);
          }}
        >
          <span />
        </div>
        <Plot
          output={sim.output}
          probes={doc.probes}
          onProbes={updateProbes}
          doc={doc}
          highlightedSignal={highlightedSignal}
          onHighlightSignal={setHighlightedSignal}
          theme={theme}
          stale={sim.output?.revision !== revision}
        />
      </main>
      <aside className="inspector">
        <div className="inspector-heading">
          <span>
            <SlidersHorizontal size={15} />{" "}
            {selected ? "Inspector" : "Circuit settings"}
          </span>
          <button
            className="icon-button"
            aria-label="Close inspector"
            onClick={() => {
              setInspectorOpen(false);
              setMobile("canvas");
            }}
          >
            <X size={15} />
          </button>
        </div>
        <div className="inspector-scroll">
          {part ? (
            <Inspector doc={doc} part={part} onChange={change} />
          ) : junction ? (
            <>
              <span className="eyebrow">JUNCTION</span>
              <h2>Make a connection.</h2>
              <Field
                label="Net label"
                value={junction.label ?? ""}
                onChange={(value) => {
                  const next = clone(doc);
                  next.junctions.find((j) => j.id === junction.id)!.label =
                    value;
                  change(next);
                }}
                help="Matching labels connect electrically. Use 0 for ground."
              />
              <Field
                label="Initial voltage"
                value={junction.initial ?? ""}
                unit="V"
                onChange={(value) => {
                  const next = clone(doc);
                  next.junctions.find((j) => j.id === junction.id)!.initial =
                    value;
                  change(next);
                }}
                help="Optional startup condition for transient analysis."
              />
              <button className="text-button danger" onClick={remove}>
                Delete junction
              </button>
            </>
          ) : wire ? (
            <>
              <span className="eyebrow">CONNECTION</span>
              <h2>Follow the signal.</h2>
              <p className="quiet">
                Double-click a wire to add a junction. Drag the square handles
                to adjust bends. Crossings connect only at explicit junctions.
              </p>
              <button
                className="secondary-button"
                onClick={() => {
                  const next = clone(doc);
                  const w = next.wires.find((w) => w.id === wire.id)!;
                  const c = doc.components.find(
                    (c) => c.id === wire.from.split(".")[0],
                  );
                  w.bends.push({
                    x: (c?.x ?? 300) + 60,
                    y: (c?.y ?? 200) + 60,
                  });
                  change(next);
                }}
              >
                Add routing handle
              </button>
              <button className="text-button danger" onClick={remove}>
                Delete wire
              </button>
            </>
          ) : (
            <>
              <div className="section-heading">
                <Settings2 size={15} />
                <span>Analysis</span>
              </div>
              <AnalysisControls
                analysis={doc.analysis}
                onChange={(analysis) => change({ ...doc, analysis })}
              />
            </>
          )}
          {selected && (
            <details className="analysis-details">
              <summary>
                Analysis settings <ChevronDown size={14} />
              </summary>
              <AnalysisControls
                analysis={doc.analysis}
                onChange={(analysis) => change({ ...doc, analysis })}
              />
            </details>
          )}
          {example && doc.mode === "schematic" && !selected && (
            <>
              <div className="inspector-divider" />
              <div className="section-heading">
                <Settings2 size={15} />
                <span>Make it your own</span>
              </div>
              <p className="quiet small">Move a slider. See what changes.</p>
              {example.tunes.map((t) => {
                const p = doc.components.find((c) => c.id === t.part);
                if (!p) return null;
                const v = safeValue(p.props[t.property]);
                return (
                  <label className="tune-control" key={t.part + t.property}>
                    <span>
                      <span>{t.label}</span>
                      <code>{t.part}</code>
                    </span>
                    <strong>{engineering(v, t.unit, 4)}</strong>
                    <input
                      type="range"
                      aria-label={t.label}
                      min={0}
                      max={1000}
                      step={1}
                      value={Math.max(
                        0,
                        Math.min(
                          1000,
                          (t.log
                            ? Math.log(Math.max(t.min, v) / t.min) /
                              Math.log(t.max / t.min)
                            : (v - t.min) / (t.max - t.min)) * 1000,
                        ),
                      )}
                      onChange={(e) => {
                        const fraction = Number(e.target.value) / 1000;
                        tune(
                          t.part,
                          t.property,
                          t.log
                            ? t.min * (t.max / t.min) ** fraction
                            : t.min + (t.max - t.min) * fraction,
                        );
                      }}
                    />
                    <div className="range-labels">
                      <span>{engineering(t.min, t.unit)}</span>
                      <span>{engineering(t.max, t.unit)}</span>
                    </div>
                  </label>
                );
              })}
              <div className="inspector-divider" />
              <div className="learning-note">
                <div>
                  <Lightbulb size={16} />
                  <span>A CLOSER LOOK</span>
                </div>
                <p>{example.lesson}</p>
              </div>
            </>
          )}
          {!selected && !example && (
            <div className="learning-note">
              <div>
                <Lightbulb size={16} />
                <span>MAKE SOMETHING INTERESTING</span>
              </div>
              <p>
                Add components, connect their terminals, and include a ground
                reference. Select any component to edit its properties.
              </p>
            </div>
          )}
          {selected && (
            <button
              className="back-settings text-button"
              onClick={() => setSelected(null)}
            >
              ← Back to circuit settings
            </button>
          )}
        </div>
        <div className="inspector-foot">
          <Zap size={13} />
          <span>
            {sim.busy
              ? "Simulating…"
              : sim.ready
                ? "Engine ready"
                : "Loading engine…"}
          </span>
          <span className={sim.busy ? "busy-dot" : "live-dot"} />
        </div>
      </aside>
      <footer className="app-footer">
        <span>
          <span className={sim.busy ? "busy-dot" : "live-dot"} />
          {sim.busy
            ? "Solving your circuit"
            : sim.ready
              ? "All systems ready"
              : "Starting WebAssembly"}
          <span className="footer-separator">/</span> neospice
        </span>
        <span>
          Your circuits stay on your device.
          <span className="footer-separator">/</span>
          <button onClick={() => setHelp(true)}>
            Quick guide <span>↗</span>
          </button>
        </span>
      </footer>
      <input
        ref={file}
        type="file"
        hidden
        accept=".json,.cir,.ckt,.sp,.txt"
        onChange={(e) => {
          const f = e.target.files?.[0];
          if (f) void importFile(f);
          e.target.value = "";
        }}
      />
      {help && (
        <div className="modal-backdrop" onClick={() => setHelp(false)}>
          <section
            className="help-modal"
            role="dialog"
            aria-modal="true"
            aria-labelledby="help-title"
            onClick={(e) => e.stopPropagation()}
          >
            <button
              className="icon-button modal-close"
              aria-label="Close quick guide"
              onClick={() => setHelp(false)}
            >
              <X size={20} />
            </button>
            <span className="eyebrow">WELCOME TO THE LAB</span>
            <h2 id="help-title">Follow your curiosity.</h2>
            <p>
              Start with an example, or build a circuit from scratch. All
              simulation happens locally in your browser.
            </p>
            <ol>
              <li>
                <strong>Place.</strong> Pick a component, then click the canvas,
                or drag it from the library. Press Esc to cancel.
              </li>
              <li>
                <strong>Connect.</strong> Click a pin, add bends, and click
                another pin. Click a wire while wiring to branch at a new
                junction.
              </li>
              <li>
                <strong>Explore.</strong> Set an analysis and run. Click wires
                to plot voltage, or hover/tap for a current probe. Matching
                colored markers show each measurement. Hover or tap a probe for
                its delete icon, or the current probe’s reverse icon. Drag
                probes along their wires to reposition them.
              </li>
            </ol>
            <div className="shortcut-grid">
              {[
                ["V", "Select"],
                ["W", "Wire"],
                ["R", "Rotate selection"],
                ["Delete", "Delete selection"],
                ["Ctrl/⌘ Z", "Undo"],
                ["Ctrl/⌘ Shift Z", "Redo"],
                ["Ctrl/⌘ D", "Duplicate"],
                ["Ctrl/⌘ Enter", "Run"],
                ["Esc", "Cancel placement / wire"],
                ["Scroll", "Zoom"],
              ].map(([key, label]) => (
                <div key={key}>
                  <span>{label}</span>
                  <kbd>{key}</kbd>
                </div>
              ))}
            </div>
            <p className="small quiet">
              Crossing wires do not connect without a junction. Use SPICE units:
              k = kilo, m = milli, meg = mega. AC plots show dBV or dBA, and
              phase in degrees. Use Export project to move circuits between
              devices.
            </p>
            <p className="small quiet">
              <a
                href={`${import.meta.env.BASE_URL}licenses.txt`}
                target="_blank"
                rel="noreferrer"
              >
                Open-source licenses ↗
              </a>
            </p>
            <button className="run-button" onClick={() => setHelp(false)}>
              Let’s build <ChevronRight size={15} />
            </button>
          </section>
        </div>
      )}
    </div>
  );
}
