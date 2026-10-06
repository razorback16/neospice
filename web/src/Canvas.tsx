import {
  useEffect,
  useRef,
  useState,
  type PointerEvent as ReactPointerEvent,
} from "react";
import {
  MousePointer2,
  Waypoints,
  Hand,
  Plus,
  Minus,
  Maximize,
  RotateCw,
  Copy,
  Trash2,
  ArrowLeftRight,
  CornerDownLeft,
} from "lucide-react";
import { Symbol } from "./Symbol";
import {
  PARTS,
  COMPONENT_DRAG_TYPE,
  addPart,
  clone,
  endpoint,
  pinPosition,
  wirePoints,
  pathFor,
  snap,
  uid,
  engineering,
  type CircuitDocument,
  type Kind,
  type Point,
  type Wire,
  type ProbeAnchor,
} from "./model";
import { connectivity } from "./netlist";
import type { RunResult } from "./simulation";
import {
  anchorFor,
  moveProbe,
  voltageProbeOnNet,
  currentProbeOnNet,
  currentSignal,
  probePosition,
  signalColor,
  signalLabel,
  wireFraction,
  wireLocation,
} from "./probes";
function ProbeAction({
  x,
  y,
  label,
  kind,
  onAction,
}: {
  x: number;
  y: number;
  label: string;
  kind: "delete" | "reverse";
  onAction: () => void;
}) {
  return (
    <g
      transform={`translate(${x} ${y})`}
      className={`probe-action probe-action-${kind}`}
      role="button"
      tabIndex={0}
      aria-label={label}
      onPointerDown={(e) => e.stopPropagation()}
      onClick={(e) => {
        e.stopPropagation();
        onAction();
      }}
      onKeyDown={(e) => {
        if (e.key === "Enter" || e.key === " ") {
          e.preventDefault();
          e.stopPropagation();
          onAction();
        }
      }}
    >
      <circle className="probe-action-background" r="11" />
      {kind === "delete" ? (
        <Trash2 x="-6.5" y="-6.5" width="13" height="13" />
      ) : (
        <ArrowLeftRight x="-7" y="-7" width="14" height="14" />
      )}
      <title>{label}</title>
    </g>
  );
}
export type Tool = "select" | "wire" | "pan" | Kind;
export function splitWire(
  doc: CircuitDocument,
  wire: Wire,
  point: Point,
): { doc: CircuitDocument; ref: string } {
  const next = clone(doc);
  const points = wirePoints(doc, wire);
  let nearest = { distance: Infinity, point: points[0], index: 0 };
  for (let i = 0; i < points.length - 1; i++) {
    const a = points[i],
      b = points[i + 1];
    const p = {
      x:
        a.x === b.x
          ? a.x
          : Math.min(
              Math.max(snap(point.x), Math.min(a.x, b.x)),
              Math.max(a.x, b.x),
            ),
      y:
        a.y === b.y
          ? a.y
          : Math.min(
              Math.max(snap(point.y), Math.min(a.y, b.y)),
              Math.max(a.y, b.y),
            ),
    };
    const distance = Math.hypot(p.x - point.x, p.y - point.y);
    if (distance < nearest.distance) nearest = { distance, point: p, index: i };
  }
  const p = nearest.point;
  if (Math.hypot(p.x - points[0].x, p.y - points[0].y) < 1)
    return { doc: next, ref: wire.from };
  if (Math.hypot(p.x - points.at(-1)!.x, p.y - points.at(-1)!.y) < 1)
    return { doc: next, ref: wire.to };
  const id = `j_${uid()}`;
  next.junctions.push({ id, ...p });
  next.wires = next.wires.filter((w) => w.id !== wire.id);
  next.wires.push(
    {
      id: `w_${uid()}`,
      from: wire.from,
      to: id,
      bends: points.slice(1, nearest.index + 1),
    },
    {
      id: `w_${uid()}`,
      from: id,
      to: wire.to,
      bends: points.slice(nearest.index + 1, -1),
    },
  );
  if (wire.currentProbe) {
    const fraction = wireFraction(doc, wire, p);
    const first = next.wires.at(-2)!,
      second = next.wires.at(-1)!;
    if (wire.currentProbe.position <= fraction)
      first.currentProbe = {
        ...wire.currentProbe,
        position: wire.currentProbe.position / fraction,
      };
    else
      second.currentProbe = {
        ...wire.currentProbe,
        position: (wire.currentProbe.position - fraction) / (1 - fraction),
      };
  }
  return { doc: next, ref: id };
}
function bounds(doc: CircuitDocument) {
  const points = [
    ...doc.components,
    ...doc.junctions,
    ...doc.wires.flatMap((w) => w.bends),
  ];
  if (!points.length) return { x: 0, y: 0, w: 1000, h: 560 };
  const left = Math.min(...points.map((p) => p.x)) - 110,
    right = Math.max(...points.map((p) => p.x)) + 140,
    top = Math.min(...points.map((p) => p.y)) - 100,
    bottom = Math.max(...points.map((p) => p.y)) + 100;
  return {
    x: left,
    y: top,
    w: Math.max(640, right - left),
    h: Math.max(360, bottom - top),
  };
}
export function Canvas({
  doc,
  onChange,
  selected,
  onSelect,
  tool,
  onTool,
  output,
  stale,
  onProbe,
  onCurrentProbe,
  onRemoveProbe,
  onReverseProbe,
  highlightedSignal,
  onHighlightSignal,
  onRotate,
  onDuplicate,
  onDelete,
}: {
  doc: CircuitDocument;
  onChange: (d: CircuitDocument) => void;
  selected: string | null;
  onSelect: (s: string | null) => void;
  tool: Tool;
  onTool: (t: Tool) => void;
  output: RunResult | null;
  stale: boolean;
  onProbe: (p: string, anchor?: ProbeAnchor) => void;
  onCurrentProbe: (wireId: string, position: number) => void;
  onRemoveProbe: (signal: string) => void;
  onReverseProbe: (signal: string) => void;
  highlightedSignal: string | null;
  onHighlightSignal: (signal: string | null) => void;
  onRotate: () => void;
  onDuplicate: () => void;
  onDelete: () => void;
}) {
  const svg = useRef<SVGSVGElement>(null);
  const wireTouch = useRef(false);
  const placedOnPointerDown = useRef(false);
  const [dropPreview, setDropPreview] = useState<{
    kind: Kind;
    point: Point;
  } | null>(null);
  const shell = useRef<HTMLDivElement>(null);
  const [hoverWire, setHoverWire] = useState<{
    id: string;
    position: number;
    x: number;
    y: number;
  } | null>(null);
  const [activeProbe, setActiveProbe] = useState<string | null>(null);
  const [probeDraft, setProbeDraft] = useState<CircuitDocument | null>(null);
  const probeDrag = useRef<{
    signal: string;
    origin: Point;
    base: Point;
    client: Point;
    pointerId: number;
    draft: CircuitDocument | null;
  } | null>(null);
  const suppressProbeClick = useRef(false);
  const cancelProbeDrag = () => {
    const id = probeDrag.current?.pointerId;
    probeDrag.current = null;
    setProbeDraft(null);
    if (id !== undefined && svg.current?.hasPointerCapture(id))
      svg.current.releasePointerCapture(id);
  };
  const hoverTimer = useRef<ReturnType<typeof setTimeout> | undefined>(
    undefined,
  );
  const clearHover = () => {
    clearTimeout(hoverTimer.current);
    setHoverWire(null);
    onHighlightSignal(null);
  };
  const leaveWire = () => {
    hoverTimer.current = setTimeout(clearHover, 220);
  };
  const showWire = (wire: Wire, clientX: number, clientY: number) => {
    if (tool !== "select" || drag.current || probeDrag.current) return;
    clearTimeout(hoverTimer.current);
    const bounds = shell.current!.getBoundingClientRect();
    const fraction = wireFraction(doc, wire, point(clientX, clientY));
    setHoverWire({
      id: wire.id,
      position: fraction,
      x: Math.max(24, Math.min(bounds.width - 24, clientX - bounds.left)),
      y: Math.max(90, clientY - bounds.top),
    });
    const ref =
      wire.currentProbe && fraction > wire.currentProbe.position
        ? wire.to
        : wire.from;
    const net = nets?.byEndpoint.get(ref);
    onHighlightSignal(net ? `v(${net})` : null);
  };
  const [view, setView] = useState(() => bounds(doc));
  const [canvasSize, setCanvasSize] = useState({ width: 1000, height: 560 });
  useEffect(() => {
    const element = svg.current;
    if (!element) return;
    const observer = new ResizeObserver(() =>
      setCanvasSize({
        width: element.clientWidth,
        height: element.clientHeight,
      }),
    );
    observer.observe(element);
    return () => {
      observer.disconnect();
      clearTimeout(hoverTimer.current);
    };
  }, []);
  const markerScale = Math.max(
    1,
    Math.min(
      3,
      0.9 / Math.min(canvasSize.width / view.w, canvasSize.height / view.h),
    ),
  );
  const [start, setStart] = useState<string | null>(null),
    [bends, setBends] = useState<Point[]>([]),
    [cursor, setCursor] = useState<Point>({ x: 0, y: 0 });
  const [moving, setMoving] = useState<{ id: string; point: Point } | null>(
    null,
  );
  const drag = useRef<{
    mode: "part" | "pan" | "bend" | "junction";
    id: string;
    origin: Point;
    base: Point;
    view: typeof view;
    index?: number;
  } | null>(null);
  const [bendMove, setBendMove] = useState<{
    id: string;
    index: number;
    point: Point;
  } | null>(null);
  useEffect(() => {
    cancelProbeDrag();
    setView(bounds(doc));
    setHoverWire(null);
    setActiveProbe(null);
    setStart(null);
    setBends([]);
    onSelect(null);
  }, [doc.id]);
  useEffect(() => {
    clearHover();
    setActiveProbe(null);
    setDropPreview(null);
    cancelProbeDrag();
    if (tool !== "wire") {
      setStart(null);
      setBends([]);
    }
  }, [tool]);
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        if (probeDrag.current) suppressProbeClick.current = true;
        cancelProbeDrag();
        clearHover();
        setActiveProbe(null);
        setStart(null);
        setBends([]);
        onSelect(null);
        onTool("select");
      }
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, [onTool]);
  const point = (clientX: number, clientY: number): Point => {
    const s = svg.current!;
    const pt = new DOMPoint(clientX, clientY).matrixTransform(
      s.getScreenCTM()!.inverse(),
    );
    return { x: pt.x, y: pt.y };
  };
  useEffect(() => {
    const s = svg.current;
    if (!s) return;
    const wheel = (e: WheelEvent) => {
      e.preventDefault();
      if (probeDrag.current) return;
      const p = point(e.clientX, e.clientY);
      const scale = e.deltaY > 0 ? 1.12 : 1 / 1.12;
      setView((v) => {
        const w = Math.max(200, Math.min(6000, v.w * scale)),
          ratio = w / v.w;
        return {
          x: p.x - (p.x - v.x) * ratio,
          y: p.y - (p.y - v.y) * ratio,
          w,
          h: v.h * ratio,
        };
      });
    };
    s.addEventListener("wheel", wheel, { passive: false });
    return () => s.removeEventListener("wheel", wheel);
  }, []);
  let displayDoc = probeDraft ?? doc;
  if (moving) {
    displayDoc = clone(doc);
    const c =
      displayDoc.components.find((c) => c.id === moving.id) ??
      displayDoc.junctions.find((j) => j.id === moving.id);
    if (c) {
      c.x = moving.point.x;
      c.y = moving.point.y;
    }
  }
  if (bendMove) {
    displayDoc = clone(displayDoc);
    const wire = displayDoc.wires.find((w) => w.id === bendMove.id);
    if (wire) wire.bends[bendMove.index] = bendMove.point;
  }
  let nets: ReturnType<typeof connectivity> | null = null;
  try {
    nets = connectivity(doc);
  } catch {}
  const wireEnd = (ref: string, updated = doc) => {
    if (!start) {
      setStart(ref);
      setBends([]);
      onTool("wire");
      return;
    }
    if (ref !== start) {
      const next = clone(updated);
      next.wires.push({ id: `w_${uid()}`, from: start, to: ref, bends });
      onChange(next);
    }
    setStart(null);
    setBends([]);
  };
  const zoom = (factor: number) =>
    setView((v) => ({
      x: v.x + (v.w * (1 - factor)) / 2,
      y: v.y + (v.h * (1 - factor)) / 2,
      w: Math.max(200, Math.min(6000, v.w * factor)),
      h: Math.max(120, Math.min(4000, v.h * factor)),
    }));
  const placePart = (kind: Kind, point: Point) => {
    const next = clone(doc);
    const component = addPart(next, kind, point);
    next.components.push(component);
    onChange(next);
    onSelect(component.id);
    onTool("select");
    setDropPreview(null);
  };
  const down = (e: ReactPointerEvent<SVGSVGElement>) => {
    if (e.button !== 0 && e.button !== 1) return;
    const p = point(e.clientX, e.clientY);
    clearHover();
    setActiveProbe(null);
    if (tool === "pan" || e.button === 1) {
      e.currentTarget.setPointerCapture(e.pointerId);
      drag.current = {
        mode: "pan",
        id: "",
        origin: { x: e.clientX, y: e.clientY },
        base: p,
        view,
      };
      return;
    }
    if (Object.hasOwn(PARTS, tool)) {
      placePart(tool as Kind, p);
      return;
    }
    if (tool === "wire" && start) {
      setBends([...bends, { x: snap(p.x), y: snap(p.y) }]);
      return;
    }
    onSelect(null);
  };
  function beginDrag(
    e: ReactPointerEvent,
    id: string,
    mode: "part" | "bend" | "junction",
    base: Point,
    index?: number,
  ) {
    e.stopPropagation();
    if (e.button !== 0) return;
    onSelect(id);
    svg.current!.setPointerCapture(e.pointerId);
    drag.current = {
      mode,
      id,
      origin: point(e.clientX, e.clientY),
      base,
      view,
      index,
    };
  }
  const beginProbeDrag = (e: ReactPointerEvent, signal: string) => {
    e.stopPropagation();
    if (e.button !== 0 || tool !== "select") return;
    const sensor = doc.wires.find(
      (w) => w.currentProbe && currentSignal(w) === signal,
    );
    const anchor = anchorFor(doc, signal);
    const base = sensor
      ? wireLocation(doc, sensor, sensor.currentProbe!.position)
      : anchor
        ? probePosition(doc, anchor)
        : null;
    if (!base) return;
    clearHover();
    setActiveProbe(e.pointerType === "touch" ? signal : null);
    onHighlightSignal(signal);
    suppressProbeClick.current = false;
    svg.current!.setPointerCapture(e.pointerId);
    probeDrag.current = {
      signal,
      base,
      origin: point(e.clientX, e.clientY),
      client: { x: e.clientX, y: e.clientY },
      pointerId: e.pointerId,
      draft: null,
    };
  };
  const selectProbe = (e: React.MouseEvent, signal: string) => {
    e.stopPropagation();
    if (suppressProbeClick.current) {
      suppressProbeClick.current = false;
      return;
    }
    clearHover();
    setActiveProbe(signal);
    onHighlightSignal(signal);
  };
  const move = (e: ReactPointerEvent<SVGSVGElement>) => {
    const p = point(e.clientX, e.clientY);
    setCursor({ x: snap(p.x), y: snap(p.y) });
    const probe = probeDrag.current;
    if (probe) {
      if (
        !probe.draft &&
        Math.hypot(e.clientX - probe.client.x, e.clientY - probe.client.y) < 4
      )
        return;
      probe.draft = moveProbe(doc, probe.signal, {
        x: probe.base.x + p.x - probe.origin.x,
        y: probe.base.y + p.y - probe.origin.y,
      });
      suppressProbeClick.current = true;
      setProbeDraft(probe.draft);
      return;
    }
    const d = drag.current;
    if (!d) return;
    if (d.mode === "pan") {
      const ratio = d.view.w / svg.current!.getBoundingClientRect().width;
      setView({
        ...d.view,
        x: d.view.x - (e.clientX - d.origin.x) * ratio,
        y: d.view.y - (e.clientY - d.origin.y) * ratio,
      });
    } else {
      const dest = {
        x: snap(d.base.x + p.x - d.origin.x),
        y: snap(d.base.y + p.y - d.origin.y),
      };
      if (d.mode === "bend")
        setBendMove({ id: d.id, index: d.index!, point: dest });
      else setMoving({ id: d.id, point: dest });
    }
  };
  const up = () => {
    if (probeDrag.current) {
      const probe = probeDrag.current;
      probeDrag.current = null;
      setProbeDraft(null);
      if (probe.draft) onChange(probe.draft);
      return;
    }
    if (drag.current && (moving || bendMove)) onChange(displayDoc);
    drag.current = null;
    setMoving(null);
    setBendMove(null);
  };
  const hoverTarget = doc.wires.find((w) => w.id === hoverWire?.id);
  const hoverRef =
    hoverTarget &&
    (hoverTarget.currentProbe &&
    hoverWire!.position > hoverTarget.currentProbe.position
      ? hoverTarget.to
      : hoverTarget.from);
  const hoverNet = hoverRef ? nets?.byEndpoint.get(hoverRef) : undefined;
  const hoverSignal = `v(${hoverNet})`;
  const voltageAdded = !!hoverRef && !!voltageProbeOnNet(doc, hoverRef);
  const currentAdded = !!hoverRef && !!currentProbeOnNet(doc, hoverRef);
  const selectedPart = doc.components.find((c) => c.id === selected);
  return (
    <div
      ref={shell}
      className={`canvas-shell tool-${tool} ${probeDraft ? "probe-dragging" : ""}`}
    >
      <div className="canvas-toolbar">
        <div className="tool-group">
          {(
            [
              { id: "select", name: "Select (V)", icon: MousePointer2 },
              { id: "wire", name: "Wire (W)", icon: Waypoints },
              { id: "pan", name: "Pan", icon: Hand },
            ] as const
          ).map((t) => (
            <button
              key={t.id}
              className={`icon-button ${tool === t.id ? "active" : ""}`}
              title={t.name}
              aria-label={t.name}
              aria-pressed={tool === t.id}
              onClick={() => onTool(t.id)}
            >
              <t.icon size={17} />
            </button>
          ))}
        </div>
        <span className="toolbar-divider" />
        {selectedPart ? (
          <div className="tool-group">
            <button
              className="icon-button"
              title="Rotate (R)"
              aria-label="Rotate component"
              onClick={onRotate}
            >
              <RotateCw size={16} />
            </button>
            <button
              className="icon-button"
              title="Duplicate (Ctrl+D)"
              aria-label="Duplicate component"
              onClick={onDuplicate}
            >
              <Copy size={16} />
            </button>
            <button
              className="icon-button danger"
              title="Delete"
              aria-label="Delete selected"
              onClick={onDelete}
            >
              <Trash2 size={16} />
            </button>
          </div>
        ) : (
          <span className="toolbar-hint">
            {Object.hasOwn(PARTS, tool)
              ? `Place ${PARTS[tool as Kind].name.toLowerCase()} · Esc to cancel`
              : tool === "wire"
                ? "Choose a terminal to start"
                : "Hover or tap a wire to add probes"}
          </span>
        )}
      </div>
      <svg
        ref={svg}
        className="schematic"
        role="img"
        aria-label="Circuit schematic editor"
        data-testid="schematic"
        viewBox={`${view.x} ${view.y} ${view.w} ${view.h}`}
        onPointerDownCapture={(e) => {
          if (e.button === 0 && Object.hasOwn(PARTS, tool)) {
            e.stopPropagation();
            placedOnPointerDown.current = true;
            down(e);
          }
        }}
        onClickCapture={(e) => {
          if (placedOnPointerDown.current || suppressProbeClick.current) {
            suppressProbeClick.current = false;
            e.stopPropagation();
            placedOnPointerDown.current = false;
          }
        }}
        onDragOver={(e) => {
          if (
            !e.dataTransfer.types.includes(COMPONENT_DRAG_TYPE) ||
            !Object.hasOwn(PARTS, tool)
          )
            return;
          e.preventDefault();
          e.dataTransfer.dropEffect = "copy";
          const p = point(e.clientX, e.clientY);
          setDropPreview({
            kind: tool as Kind,
            point: { x: snap(p.x), y: snap(p.y) },
          });
        }}
        onDragLeave={(e) => {
          if (!e.currentTarget.contains(e.relatedTarget as Node | null))
            setDropPreview(null);
        }}
        onDrop={(e) => {
          if (!e.dataTransfer.types.includes(COMPONENT_DRAG_TYPE)) return;
          e.preventDefault();
          const kind = e.dataTransfer.getData(COMPONENT_DRAG_TYPE);
          if (!Object.hasOwn(PARTS, kind) || kind !== tool) return;
          clearHover();
          setActiveProbe(null);
          placePart(kind as Kind, point(e.clientX, e.clientY));
        }}
        onPointerDown={down}
        onPointerMove={move}
        onPointerUp={up}
        onLostPointerCapture={() => {
          if (probeDrag.current) cancelProbeDrag();
        }}
        onPointerCancel={() => {
          cancelProbeDrag();
          drag.current = null;
          setMoving(null);
          setBendMove(null);
        }}
      >
        <defs>
          <pattern
            id="grid"
            x="0"
            y="0"
            width="20"
            height="20"
            patternUnits="userSpaceOnUse"
          >
            <circle cx="0" cy="0" r="0.9" fill="var(--grid-dot)" />
          </pattern>
        </defs>
        <rect
          x={view.x - 1000}
          y={view.y - 1000}
          width={view.w + 2000}
          height={view.h + 2000}
          fill="url(#grid)"
        />
        {displayDoc.wires.map((w) => {
          let points: Point[];
          try {
            points = wirePoints(displayDoc, w);
          } catch {
            return null;
          }
          const net = nets?.byEndpoint.get(w.from),
            probed = doc.probes.includes(`v(${net})`);
          return (
            <g key={w.id}>
              <path
                d={pathFor(points)}
                className={`wire ${selected === w.id || hoverWire?.id === w.id || highlightedSignal === `v(${net})` ? "selected" : ""} ${probed ? "probed" : ""}`}
                style={
                  probed ? { stroke: signalColor(doc, `v(${net})`) } : undefined
                }
              />
              <path
                d={pathFor(points)}
                className="wire-hit"
                data-wire={w.id}
                onPointerEnter={(e) => {
                  if (e.pointerType !== "touch")
                    showWire(w, e.clientX, e.clientY);
                }}
                onPointerLeave={leaveWire}
                onPointerDown={(e) => {
                  e.stopPropagation();
                  wireTouch.current = e.pointerType === "touch";
                }}
                onClick={(e) => {
                  e.stopPropagation();
                  if (tool === "wire") {
                    const split = splitWire(
                      doc,
                      w,
                      point(e.clientX, e.clientY),
                    );
                    if (start) wireEnd(split.ref, split.doc);
                    else {
                      onChange(split.doc);
                      wireEnd(split.ref, split.doc);
                    }
                  } else {
                    onSelect(w.id);
                    showWire(w, e.clientX, e.clientY);
                    const position = wireFraction(
                      doc,
                      w,
                      point(e.clientX, e.clientY),
                    );
                    const ref =
                      w.currentProbe && position > w.currentProbe.position
                        ? w.to
                        : w.from;
                    const voltage = nets?.byEndpoint.get(ref);
                    if (
                      !wireTouch.current &&
                      voltage &&
                      voltage !== "0" &&
                      !voltageProbeOnNet(doc, ref)
                    )
                      onProbe(`v(${voltage})`, { ref, wireId: w.id, position });
                  }
                }}
                onDoubleClick={(e) => {
                  e.stopPropagation();
                  if (tool === "wire") return;
                  const split = splitWire(doc, w, point(e.clientX, e.clientY));
                  onChange(split.doc);
                  onSelect(split.ref);
                }}
              >
                <title>
                  {net === "0"
                    ? "Ground"
                    : `V(${net}) · click to plot · hover for current probe`}
                </title>
              </path>
              {selected === w.id &&
                w.bends.map((p, i) => (
                  <rect
                    key={i}
                    className="bend-handle"
                    x={p.x - 5}
                    y={p.y - 5}
                    width="10"
                    height="10"
                    onPointerDown={(e) => beginDrag(e, w.id, "bend", p, i)}
                  />
                ))}
            </g>
          );
        })}
        {displayDoc.junctions.map((j) => {
          const net = nets?.byEndpoint.get(j.id);
          const val =
            output?.mode === "dc" && !stale
              ? output.result.voltages[`v(${net})`]
              : undefined;
          return (
            <g key={j.id}>
              <circle
                className={`junction ${selected === j.id ? "selected" : ""}`}
                cx={j.x}
                cy={j.y}
                r="3.5"
              />
              <circle
                className="junction-hit"
                cx={j.x}
                cy={j.y}
                r="11"
                data-junction={j.id}
                onPointerDown={(e) => {
                  if (tool === "wire") {
                    e.stopPropagation();
                    wireEnd(j.id);
                  } else beginDrag(e, j.id, "junction", j);
                }}
              />
              {j.label && (
                <text className="net-label" x={j.x + 9} y={j.y - 12}>
                  {j.label}
                </text>
              )}
              {typeof val === "number" && (
                <text className="dc-annotation" x={j.x + 9} y={j.y + 19}>
                  {engineering(val, "V", 4)}
                </text>
              )}
            </g>
          );
        })}
        {displayDoc.components.map((c) => (
          <g
            key={c.id}
            data-component={c.id}
            role="button"
            aria-label={`${c.id} ${PARTS[c.kind].name}`}
            tabIndex={0}
            onKeyDown={(e) => {
              if (e.key === "Enter") {
                e.stopPropagation();
                onSelect(c.id);
              }
            }}
            onPointerDown={(e) => beginDrag(e, c.id, "part", c)}
            className={`component ${selected === c.id ? "selected" : ""}`}
          >
            <rect
              className="component-hit"
              x={c.x - 50}
              y={c.y - 48}
              width="100"
              height="96"
              rx="9"
            />
            <g transform={`translate(${c.x} ${c.y}) rotate(${c.rotation})`}>
              <Symbol kind={c.kind} />
            </g>
            {c.kind !== "G" && (
              <g
                className="component-label"
                transform={`translate(${c.x + (c.rotation % 180 === 90 || ["V", "I", "QN", "QP", "MN", "MP"].includes(c.kind) ? 48 : 0)} ${c.y + (c.rotation % 180 === 90 || ["V", "I", "QN", "QP", "MN", "MP"].includes(c.kind) ? -5 : -33)})`}
                textAnchor={
                  c.rotation % 180 === 90 ||
                  ["V", "I", "QN", "QP", "MN", "MP"].includes(c.kind)
                    ? "start"
                    : "middle"
                }
              >
                <text className="part-name">{c.id}</text>
                <text className="part-value" y="17">
                  {c.props.value
                    ? `${c.props.value}${PARTS[c.kind].unit ? " " + PARTS[c.kind].unit : ""}`
                    : c.kind === "V" || c.kind === "I"
                      ? c.props.wave === "dc"
                        ? `${c.props.dc} ${PARTS[c.kind].unit}`
                        : c.props.wave.toUpperCase()
                      : c.props.model?.replace("_GENERIC", "")}
                </text>
              </g>
            )}
            {PARTS[c.kind].pins.map((pin) => {
              const p = pinPosition(c, pin.id);
              return (
                <g
                  key={pin.id}
                  className="pin"
                  data-pin={`${c.id}.${pin.id}`}
                  onPointerDown={(e) => {
                    e.stopPropagation();
                    wireEnd(`${c.id}.${pin.id}`);
                  }}
                >
                  <circle className="pin-hit" cx={p.x} cy={p.y} r="10" />
                  <circle className="pin-dot" cx={p.x} cy={p.y} r="3" />
                  <title>
                    {c.id} · {pin.id}
                  </title>
                </g>
              );
            })}
          </g>
        ))}
        {displayDoc.wires
          .filter((w) => w.currentProbe)
          .map((w) => {
            const sensor = w.currentProbe!,
              signal = currentSignal(w);
            const p = wireLocation(displayDoc, w, sensor.position),
              color = signalColor(doc, signal);
            const direction = p.angle + (sensor.reversed ? 180 : 0);
            return (
              <g
                key={sensor.id}
                className={`schematic-probe current-probe ${highlightedSignal === signal ? "highlighted" : ""} ${activeProbe === signal ? "probe-active" : ""}`}
                style={{ color }}
                data-probe={signal}
                role="group"
                tabIndex={0}
                aria-label={`Current probe ${signalLabel(signal)}`}
                onPointerDown={(e) => beginProbeDrag(e, signal)}
                onPointerEnter={() => {
                  clearHover();
                  onHighlightSignal(signal);
                }}
                onPointerLeave={() => onHighlightSignal(null)}
                onClick={(e) => selectProbe(e, signal)}
                onKeyDown={(e) => {
                  if (e.key === "Enter" || e.key === " ") {
                    e.preventDefault();
                    setActiveProbe(signal);
                  }
                }}
              >
                <g
                  transform={`translate(${p.x} ${p.y}) scale(${markerScale}) rotate(${direction})`}
                >
                  <rect
                    className="sensor-body"
                    x="-14"
                    y="-10"
                    width="28"
                    height="20"
                    rx="6"
                  />
                  <path className="sensor-arrow" d="M-6,0 H6 M2,-4 L6,0 2,4" />
                </g>
                <text
                  className="probe-name"
                  transform={`translate(${p.x} ${p.y}) scale(${markerScale})`}
                  x={p.angle % 180 ? 19 : 0}
                  y={p.angle % 180 ? 4 : 25}
                  textAnchor={p.angle % 180 ? "start" : "middle"}
                >
                  {signalLabel(signal)}
                </text>
                <g
                  className="probe-actions"
                  transform={`translate(${p.x} ${p.y}) scale(${markerScale})`}
                >
                  <ProbeAction
                    x={-19}
                    y={-17}
                    kind="reverse"
                    label={`Reverse ${signalLabel(signal)}`}
                    onAction={() => onReverseProbe(signal)}
                  />
                  <ProbeAction
                    x={19}
                    y={-17}
                    kind="delete"
                    label={`Delete ${signalLabel(signal)}`}
                    onAction={() => {
                      onRemoveProbe(signal);
                      setActiveProbe(null);
                    }}
                  />
                </g>
                <title>
                  {signalLabel(signal)} · current flows in the arrow direction ·
                  drag along the wire; hover for reverse and delete
                </title>
              </g>
            );
          })}
        {doc.probes
          .filter((signal) => signal.startsWith("v("))
          .map((signal) => {
            try {
              const anchor = anchorFor(displayDoc, signal);
              if (!anchor) return null;
              const p = probePosition(displayDoc, anchor),
                color = signalColor(doc, signal),
                label = signalLabel(signal);
              const width = Math.max(54, label.length * 7 + 16);
              return (
                <g
                  key={signal}
                  className={`schematic-probe voltage-probe ${highlightedSignal === signal ? "highlighted" : ""} ${activeProbe === signal ? "probe-active" : ""}`}
                  style={{ color }}
                  data-probe={signal}
                  role="group"
                  tabIndex={0}
                  aria-label={`Voltage probe ${label}`}
                  onPointerDown={(e) => beginProbeDrag(e, signal)}
                  onPointerEnter={() => {
                    clearHover();
                    onHighlightSignal(signal);
                  }}
                  onPointerLeave={() => onHighlightSignal(null)}
                  onClick={(e) => selectProbe(e, signal)}
                  onKeyDown={(e) => {
                    if (e.key === "Enter" || e.key === " ") {
                      e.preventDefault();
                      setActiveProbe(signal);
                    }
                  }}
                >
                  <g
                    transform={`translate(${p.x} ${p.y}) scale(${markerScale})`}
                  >
                    <path className="probe-stem" d="M0,0 v-18" />
                    <circle className="probe-contact" cx="0" cy="0" r="3.25" />
                    <rect
                      className="probe-tag"
                      x={-width / 2}
                      y="-41"
                      width={width}
                      height="23"
                      rx="6"
                    />
                    <text
                      className="probe-name"
                      x="0"
                      y="-25"
                      textAnchor="middle"
                    >
                      {label}
                    </text>
                  </g>
                  <g
                    className="probe-actions"
                    transform={`translate(${p.x} ${p.y}) scale(${markerScale})`}
                  >
                    <ProbeAction
                      x={width / 2 + 3}
                      y={-43}
                      kind="delete"
                      label={`Delete ${label}`}
                      onAction={() => {
                        onRemoveProbe(signal);
                        setActiveProbe(null);
                      }}
                    />
                  </g>
                  <title>
                    {label} · voltage relative to ground · drag along the net;
                    hover to delete
                  </title>
                </g>
              );
            } catch {
              return null;
            }
          })}
        {start &&
          (() => {
            try {
              const p = endpoint(displayDoc, start);
              const preview: Point[] = [p];
              for (const b of [...bends, cursor]) {
                const a = preview.at(-1)!;
                if (a.x !== b.x && a.y !== b.y)
                  preview.push({ x: b.x, y: a.y });
                preview.push(b);
              }
              return <path className="wire-preview" d={pathFor(preview)} />;
            } catch {
              return null;
            }
          })()}
        {dropPreview && (
          <g
            className="canvas-placement-preview"
            data-testid="drop-preview"
            transform={`translate(${dropPreview.point.x} ${dropPreview.point.y})`}
          >
            <rect
              className="placement-outline"
              x="-47"
              y="-47"
              width="94"
              height="94"
              rx="9"
            />
            <Symbol kind={dropPreview.kind} />
          </g>
        )}
      </svg>
      {hoverWire &&
        hoverTarget &&
        tool === "select" &&
        (!voltageAdded || !currentAdded) && (
          <div
            className="wire-probe-actions wire-probe-picker"
            role="toolbar"
            aria-label="Wire probes"
            style={{ left: hoverWire.x, top: hoverWire.y }}
            onPointerEnter={() => clearTimeout(hoverTimer.current)}
            onPointerLeave={leaveWire}
          >
            {!voltageAdded && (
              <button
                aria-label="Plot voltage"
                title="Plot voltage"
                disabled={!hoverNet || hoverNet === "0"}
                onClick={() => {
                  onProbe(hoverSignal, {
                    ref: hoverRef!,
                    wireId: hoverTarget.id,
                    position: hoverWire.position,
                  });
                  clearHover();
                }}
              >
                <svg
                  className="probe-action-icon"
                  viewBox="0 0 24 24"
                  aria-hidden="true"
                >
                  <path d="M12 1v3m0 16v3" />
                  <circle cx="12" cy="12" r="8" />
                  <path d="m8.5 8.5 3.5 7 3.5-7" />
                </svg>
              </button>
            )}
            {!currentAdded && (
              <button
                aria-label="Insert current probe"
                title="Insert current probe"
                onClick={() => {
                  onCurrentProbe(hoverTarget.id, hoverWire.position);
                  clearHover();
                }}
                disabled={!!hoverTarget.currentProbe}
              >
                <svg
                  className="probe-action-icon"
                  viewBox="0 0 24 24"
                  aria-hidden="true"
                >
                  <path d="M1 12h3m16 0h3" />
                  <circle cx="12" cy="12" r="8" />
                  <path d="M7.5 12h9m-3.5-3.5 3.5 3.5-3.5 3.5" />
                </svg>
              </button>
            )}
          </div>
        )}
      {!doc.components.length && (
        <div className="canvas-empty">
          <span className="eyebrow">YOUR NEXT IDEA STARTS HERE</span>
          <h2>
            A little space.
            <br />
            Infinite possibilities.
          </h2>
          <p>
            Choose a component from the library,
            <br />
            place it on the canvas, and connect its pins.
          </p>
        </div>
      )}
      <div className="canvas-bottom">
        <span className="canvas-caption">
          {start ? (
            <>
              <CornerDownLeft size={12} /> Click bends, then a terminal · Esc to
              cancel
            </>
          ) : (
            <>
              <span className="tiny-dot" />{" "}
              {doc.components.filter((c) => c.kind !== "G").length} components ·{" "}
              {doc.wires.length} connections
            </>
          )}
        </span>
        <div className="zoom-tools">
          <button
            className="icon-button"
            aria-label="Zoom out"
            onClick={() => zoom(1.2)}
          >
            <Minus size={15} />
          </button>
          <button
            className="icon-button"
            aria-label="Zoom in"
            onClick={() => zoom(1 / 1.2)}
          >
            <Plus size={15} />
          </button>
          <button
            className="icon-button"
            aria-label="Fit circuit"
            title="Fit circuit"
            onClick={() => setView(bounds(doc))}
          >
            <Maximize size={15} />
          </button>
        </div>
      </div>
    </div>
  );
}
