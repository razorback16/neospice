export const COMPONENT_DRAG_TYPE = "application/x-neospice-component";
export type Point = { x: number; y: number };
export type Kind =
  "R" | "C" | "L" | "V" | "I" | "D" | "QN" | "QP" | "MN" | "MP" | "OP" | "G";
export type Component = Point & {
  id: string;
  kind: Kind;
  rotation: number;
  props: Record<string, string>;
};
export type Junction = Point & { id: string; label?: string; initial?: string };
export type Wire = {
  id: string;
  from: string;
  to: string;
  bends: Point[];
  currentProbe?: { id: string; position: number; reversed?: boolean };
};
export type ProbeAnchor = { ref: string; wireId?: string; position?: number };
export type Analysis = {
  mode: "dc" | "ac" | "transient";
  step: string;
  stop: string;
  start: string;
  end: string;
  points: number;
  sweep: "dec" | "oct" | "lin";
};
export type CircuitDocument = {
  version: 1;
  id: string;
  title: string;
  mode: "schematic" | "netlist";
  components: Component[];
  junctions: Junction[];
  wires: Wire[];
  models: Record<string, string>;
  analysis: Analysis;
  probes: string[];
  probeAnchors?: Record<string, ProbeAnchor>;
  probeColors?: Record<string, string>;
  text: string;
  exampleId?: string;
};
export type PartSpec = {
  name: string;
  prefix: string;
  unit: string;
  pins: { id: string; x: number; y: number }[];
  defaults: Record<string, string>;
};
const passivePins = [
  { id: "a", x: -40, y: 0 },
  { id: "b", x: 40, y: 0 },
];
const sourcePins = [
  { id: "p", x: 0, y: -40 },
  { id: "n", x: 0, y: 40 },
];
const transistorPins = [
  { id: "c", x: 20, y: -40 },
  { id: "b", x: -40, y: 0 },
  { id: "e", x: 20, y: 40 },
];
const mosPins = [
  { id: "d", x: 20, y: -40 },
  { id: "g", x: -40, y: 0 },
  { id: "s", x: 20, y: 40 },
  { id: "b", x: 40, y: 0 },
];
export const PARTS: Record<Kind, PartSpec> = {
  R: {
    name: "Resistor",
    prefix: "R",
    unit: "Ω",
    pins: passivePins,
    defaults: { value: "1k" },
  },
  C: {
    name: "Capacitor",
    prefix: "C",
    unit: "F",
    pins: passivePins,
    defaults: { value: "1u" },
  },
  L: {
    name: "Inductor",
    prefix: "L",
    unit: "H",
    pins: passivePins,
    defaults: { value: "10m" },
  },
  V: {
    name: "Voltage source",
    prefix: "V",
    unit: "V",
    pins: sourcePins,
    defaults: {
      dc: "5",
      ac: "1",
      phase: "0",
      wave: "dc",
      low: "0",
      high: "1",
      delay: "0",
      rise: "1u",
      fall: "1u",
      width: "5m",
      period: "10m",
      offset: "0",
      amplitude: "1",
      frequency: "1k",
    },
  },
  I: {
    name: "Current source",
    prefix: "I",
    unit: "A",
    pins: sourcePins,
    defaults: {
      dc: "1m",
      ac: "0",
      phase: "0",
      wave: "dc",
      low: "0",
      high: "1m",
      delay: "0",
      rise: "1u",
      fall: "1u",
      width: "5m",
      period: "10m",
      offset: "0",
      amplitude: "1m",
      frequency: "1k",
    },
  },
  D: {
    name: "Diode",
    prefix: "D",
    unit: "",
    pins: passivePins,
    defaults: { model: "D_GENERIC" },
  },
  QN: {
    name: "NPN transistor",
    prefix: "Q",
    unit: "",
    pins: transistorPins,
    defaults: { model: "NPN_GENERIC" },
  },
  QP: {
    name: "PNP transistor",
    prefix: "Q",
    unit: "",
    pins: transistorPins,
    defaults: { model: "PNP_GENERIC" },
  },
  MN: {
    name: "NMOS transistor",
    prefix: "M",
    unit: "",
    pins: mosPins,
    defaults: { model: "NMOS_GENERIC", w: "10u", l: "1u" },
  },
  MP: {
    name: "PMOS transistor",
    prefix: "M",
    unit: "",
    pins: mosPins,
    defaults: { model: "PMOS_GENERIC", w: "20u", l: "1u" },
  },
  OP: {
    name: "Idealized op-amp",
    prefix: "E",
    unit: "V/V",
    pins: [
      { id: "plus", x: -40, y: 20 },
      { id: "minus", x: -40, y: -20 },
      { id: "out", x: 40, y: 0 },
    ],
    defaults: { value: "100k" },
  },
  G: {
    name: "Ground",
    prefix: "G",
    unit: "",
    pins: [{ id: "g", x: 0, y: 0 }],
    defaults: {},
  },
};
export const MODELS: Record<string, string> = {
  D_GENERIC: ".model D_GENERIC D(IS=1e-14 N=1)",
  NPN_GENERIC:
    ".model NPN_GENERIC NPN(IS=1e-14 BF=150 VAF=100 CJE=10p CJC=4p TF=300p)",
  PNP_GENERIC:
    ".model PNP_GENERIC PNP(IS=1e-14 BF=100 VAF=100 CJE=10p CJC=4p TF=300p)",
  NMOS_GENERIC: ".model NMOS_GENERIC NMOS(LEVEL=1 VTO=0.7 KP=120u LAMBDA=0.02)",
  PMOS_GENERIC: ".model PMOS_GENERIC PMOS(LEVEL=1 VTO=-0.7 KP=60u LAMBDA=0.02)",
};
export const defaultAnalysis: Analysis = {
  mode: "transient",
  step: "10u",
  stop: "10m",
  start: "1",
  end: "100k",
  points: 60,
  sweep: "dec",
};
export const clone = <T>(x: T): T => structuredClone(x);
export const uid = () => globalThis.crypto.randomUUID();
export function blankDocument(): CircuitDocument {
  return {
    version: 1,
    id: uid(),
    title: "Untitled circuit",
    mode: "schematic",
    components: [],
    junctions: [],
    wires: [],
    models: { ...MODELS },
    analysis: { ...defaultAnalysis },
    probes: [],
    text: "",
  };
}
export function addPart(
  doc: CircuitDocument,
  kind: Kind,
  point: Point,
): Component {
  const prefix = PARTS[kind].prefix;
  let n = 1;
  while (doc.components.some((c) => c.id === prefix + n)) n++;
  return {
    id: prefix + n,
    kind,
    rotation: 0,
    x: snap(point.x),
    y: snap(point.y),
    props: { ...PARTS[kind].defaults },
  };
}
export const snap = (v: number) => Math.round(v / 20) * 20;
export function pinPosition(c: Component, pinId: string): Point {
  const pin = PARTS[c.kind].pins.find((p) => p.id === pinId);
  if (!pin) throw new Error(`Unknown terminal ${c.id}.${pinId}`);
  const angle = (c.rotation * Math.PI) / 180;
  return {
    x: c.x + Math.round(pin.x * Math.cos(angle) - pin.y * Math.sin(angle)),
    y: c.y + Math.round(pin.x * Math.sin(angle) + pin.y * Math.cos(angle)),
  };
}
export function endpoint(doc: CircuitDocument, ref: string): Point {
  const j = doc.junctions.find((j) => j.id === ref);
  if (j) return j;
  const [id, pin] = ref.split(".");
  const c = doc.components.find((c) => c.id === id);
  if (!c) throw new Error(`Missing connection ${ref}`);
  return pinPosition(c, pin);
}
export function wirePoints(doc: CircuitDocument, w: Wire): Point[] {
  const start = endpoint(doc, w.from),
    end = endpoint(doc, w.to);
  const waypoints = [start, ...w.bends, end];
  const out: Point[] = [start];
  for (let i = 1; i < waypoints.length; i++) {
    const a = out.at(-1)!;
    const b = waypoints[i];
    if (a.x !== b.x && a.y !== b.y) out.push({ x: b.x, y: a.y });
    out.push(b);
  }
  return out;
}
export const pathFor = (points: Point[]) =>
  points.map((p, i) => `${i ? "L" : "M"}${p.x},${p.y}`).join(" ");
const suffix: Record<string, number> = {
  t: 1e12,
  g: 1e9,
  meg: 1e6,
  k: 1e3,
  m: 1e-3,
  u: 1e-6,
  n: 1e-9,
  p: 1e-12,
  f: 1e-15,
};
export function parseValue(input: string): number {
  const match = input
    .trim()
    .replaceAll("µ", "u")
    .replaceAll("μ", "u")
    .match(
      /^([+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?)\s*(meg|[tgkmunpf])?([a-zΩ]*)$/i,
    );
  if (!match)
    throw new Error(
      `“${input}” is not a SPICE value. Try 4.7k, 100n, or 1e-3.`,
    );
  const v = Number(match[1]) * (suffix[match[2]?.toLowerCase()] ?? 1);
  if (!Number.isFinite(v)) throw new Error("Values must be finite.");
  return v;
}
export function engineering(n: number, unit = "", digits = 3): string {
  if (!Number.isFinite(n)) return "—";
  if (n === 0) return `0${unit ? " " + unit : ""}`;
  const scales: [number, string][] = [
    [1e12, "T"],
    [1e9, "G"],
    [1e6, "M"],
    [1e3, "k"],
    [1, ""],
    [1e-3, "m"],
    [1e-6, "µ"],
    [1e-9, "n"],
    [1e-12, "p"],
    [1e-15, "f"],
  ];
  const [factor, prefix] = scales.find(([factor]) => Math.abs(n) >= factor) ?? [
    1e-15,
    "f",
  ];
  return `${Number((n / factor).toPrecision(digits))} ${prefix}${unit}`.trim();
}
export function deleteSelection(
  doc: CircuitDocument,
  id: string,
): CircuitDocument {
  const next = clone(doc);
  next.components = next.components.filter((c) => c.id !== id);
  next.junctions = next.junctions.filter((j) => j.id !== id);
  next.wires = next.wires.filter(
    (w) =>
      w.id !== id &&
      w.from !== id &&
      w.to !== id &&
      !w.from.startsWith(id + ".") &&
      !w.to.startsWith(id + "."),
  );
  return next;
}
// Check imported and restored documents before any rendering or code generation.
export function readDocument(text: string): CircuitDocument {
  if (text.length > 2_000_000)
    throw new Error("Project files must be smaller than 2 MB.");
  const d = JSON.parse(text);
  const fail = () => {
    throw new Error(
      "Invalid circuit project. Expected a version 1 neospice project.",
    );
  };
  if (
    !d ||
    d.version !== 1 ||
    !["schematic", "netlist"].includes(d.mode) ||
    typeof d.id !== "string" ||
    typeof d.title !== "string" ||
    typeof d.text !== "string" ||
    !Array.isArray(d.components) ||
    !Array.isArray(d.junctions) ||
    !Array.isArray(d.wires) ||
    !Array.isArray(d.probes) ||
    !d.probes.every((x: unknown) => typeof x === "string") ||
    !d.models ||
    typeof d.models !== "object" ||
    Array.isArray(d.models)
  )
    fail();
  const ids = new Set<string>();
  const checkId = (s: unknown) => {
    if (
      typeof s !== "string" ||
      !/^[a-zA-Z_][a-zA-Z0-9_-]*$/.test(s) ||
      ids.has(s)
    )
      fail();
    ids.add(s as string);
  };
  const pos = (p: Point) => {
    if (
      !p ||
      !Number.isFinite(p.x) ||
      !Number.isFinite(p.y) ||
      Math.abs(p.x) > 1e6 ||
      Math.abs(p.y) > 1e6
    )
      fail();
  };
  for (const c of d.components) {
    checkId(c.id);
    pos(c);
    if (
      !Object.hasOwn(PARTS, c.kind) ||
      ![0, 90, 180, 270].includes(c.rotation) ||
      !c.props ||
      typeof c.props !== "object" ||
      !Object.values(c.props).every((v) => typeof v === "string")
    )
      fail();
  }
  for (const j of d.junctions) {
    checkId(j.id);
    pos(j);
    if (
      (j.label !== undefined && typeof j.label !== "string") ||
      (j.initial !== undefined && typeof j.initial !== "string")
    )
      fail();
  }
  for (const w of d.wires) {
    checkId(w.id);
    if (
      typeof w.from !== "string" ||
      typeof w.to !== "string" ||
      !Array.isArray(w.bends)
    )
      fail();
    w.bends.forEach(pos);
    endpoint(d, w.from);
    endpoint(d, w.to);
    if (w.currentProbe !== undefined) {
      const p = w.currentProbe;
      if (
        !p ||
        !/^V_PROBE[1-9][0-9]*$/.test(p.id) ||
        !Number.isFinite(p.position) ||
        p.position < 0 ||
        p.position > 1 ||
        (p.reversed !== undefined && typeof p.reversed !== "boolean")
      )
        fail();
      checkId(p.id);
      if (
        d.components.some(
          (c: Component) => c.id.toLowerCase() === p.id.toLowerCase(),
        )
      )
        fail();
    }
  }
  if (d.probeAnchors !== undefined) {
    if (
      !d.probeAnchors ||
      typeof d.probeAnchors !== "object" ||
      Array.isArray(d.probeAnchors)
    )
      fail();
    for (const [signal, anchor] of Object.entries(d.probeAnchors)) {
      const a = anchor as ProbeAnchor;
      if (
        !/^v\([a-z0-9_]+\)$/.test(signal) ||
        !a ||
        typeof a.ref !== "string" ||
        (a.wireId !== undefined && typeof a.wireId !== "string") ||
        (a.position !== undefined &&
          (!Number.isFinite(a.position) || a.position < 0 || a.position > 1))
      )
        fail();
      endpoint(d, a.ref);
    }
  }
  if (
    d.probeColors !== undefined &&
    (!d.probeColors ||
      typeof d.probeColors !== "object" ||
      Array.isArray(d.probeColors) ||
      !Object.values(d.probeColors).every(
        (c) => typeof c === "string" && /^#[0-9a-f]{6}$/i.test(c),
      ))
  )
    fail();
  if (
    !Object.entries(d.models).every(
      ([key, value]) =>
        /^[a-zA-Z_][\w-]*$/.test(key) && typeof value === "string",
    )
  )
    fail();
  const a = d.analysis;
  if (
    !a ||
    !["dc", "ac", "transient"].includes(a.mode) ||
    !["dec", "oct", "lin"].includes(a.sweep) ||
    !["step", "stop", "start", "end"].every((k) => typeof a[k] === "string") ||
    !Number.isInteger(a.points)
  )
    fail();
  return d as CircuitDocument;
}
