import {
  clone,
  endpoint,
  wirePoints,
  type CircuitDocument,
  type Point,
  type ProbeAnchor,
  type Wire,
} from "./model";
import { connectivity } from "./netlist";

export const COLORS = [
  "#139b88",
  "#5e83dc",
  "#dd9855",
  "#b077ce",
  "#d7667f",
  "#789955",
];
export const currentSignal = (wire: Wire) =>
  `i(${wire.currentProbe!.id.toLowerCase()})`;
export function signalLabel(signal: string) {
  return signal
    .replace(/^i\(v_probe(\d+)\)$/i, "I$1")
    .replace(/^v\(/, "V(")
    .replace(/^i\(/, "I(");
}
export function signalColor(doc: CircuitDocument, signal: string): string {
  if (doc.probeColors?.[signal]) return doc.probeColors[signal];
  let hash = 0;
  for (const c of signal) hash = (hash * 31 + c.charCodeAt(0)) >>> 0;
  return COLORS[hash % COLORS.length];
}
function colorNewProbe(doc: CircuitDocument, signal: string) {
  doc.probeColors ??= {};
  const used = new Set(
    doc.probes.filter((p) => p !== signal).map((p) => signalColor(doc, p)),
  );
  doc.probeColors[signal] ??=
    COLORS.find((c) => !used.has(c)) ?? signalColor(doc, signal);
}
export function wireLocation(doc: CircuitDocument, wire: Wire, fraction = 0.5) {
  const points = wirePoints(doc, wire);
  const lengths = points
    .slice(1)
    .map((p, i) => Math.hypot(p.x - points[i].x, p.y - points[i].y));
  const total = lengths.reduce((a, b) => a + b, 0);
  let distance = Math.max(0, Math.min(1, fraction)) * total;
  for (let i = 0; i < lengths.length; i++) {
    if (distance <= lengths[i] && lengths[i] > 0) {
      const a = points[i],
        b = points[i + 1],
        t = distance / lengths[i];
      return {
        x: a.x + (b.x - a.x) * t,
        y: a.y + (b.y - a.y) * t,
        angle: (Math.atan2(b.y - a.y, b.x - a.x) * 180) / Math.PI,
        index: i,
        total,
      };
    }
    distance -= lengths[i];
  }
  return { ...points[0], angle: 0, index: 0, total };
}
export function wireFraction(doc: CircuitDocument, wire: Wire, point: Point) {
  const points = wirePoints(doc, wire);
  let total = 0,
    closest = Infinity,
    at = 0;
  for (let i = 1; i < points.length; i++) {
    const a = points[i - 1],
      b = points[i],
      dx = b.x - a.x,
      dy = b.y - a.y,
      length = Math.hypot(dx, dy);
    if (!length) continue;
    const t = Math.max(
      0,
      Math.min(
        1,
        ((point.x - a.x) * dx + (point.y - a.y) * dy) / (length * length),
      ),
    );
    const distance = Math.hypot(point.x - a.x - t * dx, point.y - a.y - t * dy);
    if (distance < closest) {
      closest = distance;
      at = total + t * length;
    }
    total += length;
  }
  return total ? at / total : 0.5;
}
export function anchorFor(
  doc: CircuitDocument,
  signal: string,
): ProbeAnchor | undefined {
  if (doc.probeAnchors?.[signal]) return doc.probeAnchors[signal];
  if (!signal.startsWith("v(") || doc.mode !== "schematic") return;
  const net = signal.slice(2, -1),
    nets = connectivity(doc);
  // Prefer an ordinary wire; voltage is measured on the chosen side of a sensor.
  const wire = doc.wires
    .filter((w) => !w.currentProbe && nets.byEndpoint.get(w.from) === net)
    .sort((a, b) => wireLocation(doc, b).total - wireLocation(doc, a).total)[0];
  if (wire) return { ref: wire.from, wireId: wire.id, position: 0.5 };
  const ref = nets.members.get(net)?.[0];
  return ref ? { ref } : undefined;
}
export function addVoltageProbe(
  doc: CircuitDocument,
  signal: string,
  anchor?: ProbeAnchor,
) {
  const next = clone(doc);
  if (!next.probes.includes(signal)) next.probes.push(signal);
  if (signal.startsWith("v(")) {
    const a = anchor ?? anchorFor(next, signal);
    if (a) (next.probeAnchors ??= {})[signal] = a;
  }
  colorNewProbe(next, signal);
  return next;
}
export function insertCurrentProbe(
  doc: CircuitDocument,
  wireId: string,
  position = 0.5,
) {
  const next = clone(doc),
    wire = next.wires.find((w) => w.id === wireId);
  if (!wire) throw Error("Select a wire to measure its current.");
  if (wire.currentProbe) return addVoltageProbe(next, currentSignal(wire));
  let n = 1;
  while (
    next.components.some((c) => c.id.toLowerCase() === `v_probe${n}`) ||
    next.wires.some((w) => w.currentProbe?.id === `V_PROBE${n}`)
  )
    n++;
  // Keep the symbol within one straight segment, including on short wires.
  const location = wireLocation(next, wire, position),
    points = wirePoints(next, wire);
  if (location.total < 1)
    throw Error("Extend this wire before adding a current probe.");
  const before = points
    .slice(1, location.index + 1)
    .reduce(
      (sum, p, i) => sum + Math.hypot(p.x - points[i].x, p.y - points[i].y),
      0,
    );
  const a = points[location.index],
    b = points[location.index + 1];
  const length = Math.hypot(b.x - a.x, b.y - a.y),
    margin = Math.min(18, length / 2);
  const distance = Math.max(
    margin,
    Math.min(length - margin, position * location.total - before),
  );
  wire.currentProbe = {
    id: `V_PROBE${n}`,
    position: (before + distance) / location.total,
  };
  const nets = connectivity(next);
  if (nets.byEndpoint.get(wire.from) === nets.byEndpoint.get(wire.to))
    throw Error(
      "This wire has a parallel connection or matching labels. Choose a single branch to measure its current.",
    );
  const signal = currentSignal(wire);
  next.probes.push(signal);
  colorNewProbe(next, signal);
  return reconcileProbes(doc, next);
}
export function removeProbe(doc: CircuitDocument, signal: string) {
  const next = clone(doc);
  next.probes = next.probes.filter((p) => p !== signal);
  delete next.probeAnchors?.[signal];
  for (const wire of next.wires)
    if (wire.currentProbe && currentSignal(wire) === signal)
      delete wire.currentProbe;
  return reconcileProbes(doc, next);
}
// Keep voltage measurements attached to terminals when insertion renumbers nets.
export function reconcileProbes(
  previous: CircuitDocument,
  document: CircuitDocument,
) {
  if (previous.id !== document.id || document.mode !== "schematic")
    return document;
  const next = clone(document);
  let nets: ReturnType<typeof connectivity>;
  try {
    nets = connectivity(next);
  } catch {
    return next;
  }
  const anchors: Record<string, ProbeAnchor> = {},
    colors: Record<string, string> = {};
  const signals: string[] = [];
  for (const oldSignal of next.probes) {
    let signal = oldSignal;
    if (oldSignal.startsWith("v(")) {
      let anchor = next.probeAnchors?.[oldSignal];
      try {
        anchor ??= anchorFor(previous, oldSignal) ?? anchorFor(next, oldSignal);
      } catch {}
      if (!anchor) continue;
      let net = nets.byEndpoint.get(anchor.ref);
      if (!net) {
        // An endpoint was deleted: retain another surviving terminal of that net.
        try {
          const oldNets = connectivity(previous),
            oldNet = oldNets.byEndpoint.get(anchor.ref);
          const ref = oldNets.members
            .get(oldNet ?? "")
            ?.find((r) => nets.byEndpoint.has(r));
          if (ref) {
            anchor = { ref };
            net = nets.byEndpoint.get(ref);
          }
        } catch {}
      }
      if (!net || net === "0") continue;
      signal = `v(${net})`;
      if (anchor.wireId && !next.wires.some((w) => w.id === anchor!.wireId))
        anchor = { ref: anchor.ref };
      anchors[signal] = anchor;
    } else if (
      /^i\(v_probe\d+\)$/.test(oldSignal) &&
      !next.wires.some((w) => w.currentProbe && currentSignal(w) === oldSignal)
    )
      continue;
    if (!signals.includes(signal)) signals.push(signal);
    colors[signal] = signalColor(next, oldSignal);
  }
  next.probes = signals;
  next.probeAnchors = anchors;
  next.probeColors = colors;
  return next;
}
export function probePosition(doc: CircuitDocument, anchor: ProbeAnchor) {
  const wire = doc.wires.find((w) => w.id === anchor.wireId);
  return wire
    ? wireLocation(doc, wire, anchor.position ?? 0.5)
    : endpoint(doc, anchor.ref);
}
export function schematicSignals(doc: CircuitDocument): string[] {
  if (doc.mode !== "schematic") return [];
  try {
    const nets = connectivity(doc);
    return [...nets.members.keys()]
      .filter((n) => n !== "0")
      .map((n) => `v(${n})`)
      .concat(
        doc.components
          .filter((c) => ["V", "L", "OP"].includes(c.kind))
          .map((c) => `i(${c.id.toLowerCase()})`),
        doc.wires.filter((w) => w.currentProbe).map(currentSignal),
      );
  } catch {
    return [];
  }
}

// Ignore ideal current sensors when deciding whether a physical net is already probed.
function voltageNets(doc: CircuitDocument) {
  try {
    return connectivity({
      ...doc,
      wires: doc.wires.map((w) => ({ ...w, currentProbe: undefined })),
    });
  } catch {
    // Imported ideal sensors may connect two deliberately different net labels.
    return connectivity(doc);
  }
}
export function voltageProbeOnNet(doc: CircuitDocument, ref: string) {
  try {
    const nets = voltageNets(doc),
      net = nets.byEndpoint.get(ref);
    if (!net) return undefined;
    return doc.probes.find((signal) => {
      if (!signal.startsWith("v(")) return false;
      const anchor = anchorFor(doc, signal);
      return anchor && nets.byEndpoint.get(anchor.ref) === net;
    });
  } catch {
    return undefined;
  }
}
export function hasVoltageProbe(doc: CircuitDocument, signal: string) {
  if (!signal.startsWith("v(")) return false;
  if (doc.probes.includes(signal)) return true;
  if (doc.mode !== "schematic") return false;
  try {
    const ref = connectivity(doc).members.get(signal.slice(2, -1))?.[0];
    return !!ref && !!voltageProbeOnNet(doc, ref);
  } catch {
    return false;
  }
}
export function moveProbe(
  doc: CircuitDocument,
  signal: string,
  target: Point,
): CircuitDocument {
  const next = clone(doc);
  const sensor = next.wires.find(
    (w) => w.currentProbe && currentSignal(w) === signal,
  );
  if (sensor?.currentProbe) {
    // Move the marker on its measured branch without changing the measurement.
    const fraction = wireFraction(next, sensor, target);
    const location = wireLocation(next, sensor, fraction),
      points = wirePoints(next, sensor);
    if (!location.total) return doc;
    const before = points
      .slice(1, location.index + 1)
      .reduce(
        (sum, p, i) => sum + Math.hypot(p.x - points[i].x, p.y - points[i].y),
        0,
      );
    const a = points[location.index],
      b = points[location.index + 1],
      length = Math.hypot(b.x - a.x, b.y - a.y);
    const margin = Math.min(14, length / 2);
    sensor.currentProbe.position =
      (before +
        Math.max(
          margin,
          Math.min(length - margin, fraction * location.total - before),
        )) /
      location.total;
    return next;
  }
  const anchor = anchorFor(doc, signal);
  if (!anchor) return doc;
  const nets = voltageNets(doc),
    net = nets.byEndpoint.get(anchor.ref);
  let closest = Infinity,
    destination: ProbeAnchor | undefined;
  for (const wire of doc.wires) {
    if (nets.byEndpoint.get(wire.from) !== net) continue;
    const position = wireFraction(doc, wire, target),
      p = wireLocation(doc, wire, position);
    const distance = Math.hypot(p.x - target.x, p.y - target.y);
    if (distance < closest) {
      closest = distance;
      destination = {
        ref:
          wire.currentProbe && position > wire.currentProbe.position
            ? wire.to
            : wire.from,
        wireId: wire.id,
        position,
      };
    }
  }
  if (destination) (next.probeAnchors ??= {})[signal] = destination;
  return next;
}
export function currentProbeOnNet(doc: CircuitDocument, ref: string) {
  try {
    const nets = voltageNets(doc),
      net = nets.byEndpoint.get(ref);
    return doc.wires.find(
      (w) =>
        w.currentProbe &&
        net !== undefined &&
        nets.byEndpoint.get(w.from) === net,
    )?.currentProbe;
  } catch {
    return undefined;
  }
}
