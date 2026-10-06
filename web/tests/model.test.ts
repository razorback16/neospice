import { test } from "node:test";
import assert from "node:assert/strict";
import { GALLERY } from "../src/gallery";
import {
  clone,
  parseValue,
  readDocument,
  pinPosition,
  deleteSelection,
} from "../src/model";
import {
  compile,
  connectivity,
  analysisOptions,
  validateText,
} from "../src/netlist";
import { splitWire } from "../src/Canvas";
const rc = () => clone(GALLERY[0].document);
test("SPICE suffixes follow SPICE semantics, including milli vs mega", () => {
  assert.equal(parseValue("1M"), 0.001);
  assert.equal(parseValue("1meg"), 1e6);
  assert.equal(parseValue("4.7kOhm"), 4700);
  assert.equal(parseValue("100nF"), 100 * 1e-9);
  assert.equal(parseValue("2µ"), 2e-6);
  assert.equal(parseValue("1e-3"), 0.001);
  for (const invalid of ["", "NaN", "Infinity", "1+2", "1;stop"])
    assert.throws(() => parseValue(invalid));
});
test("all gallery documents round-trip and compile deterministically", () => {
  for (const e of GALLERY) {
    const restored = readDocument(JSON.stringify(e.document));
    assert.deepEqual(restored, e.document);
    assert.equal(compile(restored).text, compile(e.document).text);
    for (const wire of restored.wires) assert.notEqual(wire.from, wire.to);
  }
});
test("electrical connectivity survives move and rotation", () => {
  const d = rc(),
    original = compile(d).text;
  d.components.forEach((c) => {
    c.x += 200;
    c.y -= 40;
    c.rotation = (c.rotation + 90) % 360;
  });
  assert.equal(compile(d).text, original);
  assert.equal(
    pinPosition(
      {
        id: "R1",
        kind: "R",
        x: 100,
        y: 100,
        rotation: 90,
        props: { value: "1k" },
      },
      "a",
    ).y,
    60,
  );
});
test("wire crossings do not imply electrical connections", () => {
  const d = rc();
  const before = connectivity(d);
  d.wires.forEach((w) => (w.bends = [{ x: 400, y: 200 }]));
  assert.deepEqual(connectivity(d), before);
});
test("splitting a wire creates a connected junction and preserves netlist", () => {
  const d = rc();
  const w = d.wires.find((w) => w.to === "R1.a")!;
  const split = splitWire(d, w, { x: 220, y: 160 });
  assert.equal(split.doc.wires.length, d.wires.length + 1);
  assert.ok(split.doc.junctions.some((j) => j.id === split.ref));
  assert.equal(compile(split.doc).text, compile(d).text);
});
test("matching labels connect separated wires and ground symbols share node zero", () => {
  const d = rc();
  d.junctions.push({ id: "named", x: 900, y: 100, label: "OUT" });
  assert.equal(connectivity(d).byEndpoint.get("named"), "out");
  const nets = connectivity(d);
  assert.equal(nets.byEndpoint.get("G1.g"), "0");
  assert.equal(nets.byEndpoint.get("G2.g"), "0");
});
test("shorting conflicting named nets fails clearly", () => {
  const d = rc();
  d.wires.push({ id: "short", from: "R1.a", to: "R1.b", bends: [] });
  assert.throws(() => compile(d), /labels conflict/);
});
test("unconnected terminals, missing ground, invalid passive values fail before solving", () => {
  let d = rc();
  d.wires = d.wires.filter((w) => w.to !== "R1.a");
  assert.throws(() => compile(d), /connect terminal a/);
  d = rc();
  d.components = d.components.filter((c) => c.kind !== "G");
  d.wires = d.wires.filter((w) => !w.from.startsWith("G"));
  assert.throws(() => compile(d), /ground/);
  d = rc();
  d.components.find((c) => c.id === "R1")!.props.value = "0";
  assert.throws(() => compile(d), /greater than zero/);
});
test("device order matches SPICE D, Q, M and E pin contracts", () => {
  for (const e of GALLERY) {
    const { text, nets } = compile(e.document);
    for (const c of e.document.components.filter((c) =>
      ["D", "QN", "QP", "MN", "MP", "OP"].includes(c.kind),
    )) {
      const line = text.split("\n").find((l) => l.startsWith(c.id + " "))!;
      const order =
        c.kind === "D"
          ? ["a", "b"]
          : c.kind === "QN" || c.kind === "QP"
            ? ["c", "b", "e"]
            : c.kind === "OP"
              ? ["out", "0", "plus", "minus"]
              : ["d", "g", "s", "b"];
      assert.deepEqual(
        line.split(" ").slice(1, 1 + order.length),
        order.map((p) =>
          p === "0" ? "0" : nets.byEndpoint.get(`${c.id}.${p}`),
        ),
      );
    }
  }
});
test("oscillator startup voltages are retained in generated SPICE", () => {
  const d = clone(GALLERY.find((e) => e.id === "ring-oscillator")!.document);
  assert.match(
    compile(d).text,
    /\.ic V\(n1\)=0 V\(n2\)=1.8 V\(n3\)=0 V\(n4\)=1.8 V\(n5\)=0/,
  );
});
test("comparator pins, response and startup survive project import", () => {
  const d = readDocument(
    JSON.stringify(
      GALLERY.find((e) => e.id === "comparator-oscillator")!.document,
    ),
  );
  const text = compile(d).text;
  assert.match(
    text,
    /X1 threshold cap vcc 0 out NEOSPICE_LAB_CMP GAIN=100 RESPONSE=5e-9/,
  );
  assert.match(text, /\.ic V\(cap\)=0/);
  d.components.find((c) => c.id === "X1")!.props.response = "0";
  assert.throws(() => compile(d), /response must be greater than zero/);
});
test("deleting a component also removes its wires", () => {
  const d = deleteSelection(rc(), "R1");
  assert.ok(!d.components.some((c) => c.id === "R1"));
  assert.ok(
    !d.wires.some((w) => w.to.startsWith("R1.") || w.from.startsWith("R1.")),
  );
});
test("analysis grids and browser-only netlist restrictions are enforced", () => {
  assert.throws(
    () => analysisOptions({ ...rc().analysis, stop: "-1" }),
    /positive/,
  );
  assert.throws(
    () => analysisOptions({ ...rc().analysis, step: "1p", stop: "1" }),
    /million/,
  );
  assert.throws(
    () => analysisOptions({ ...rc().analysis, mode: "ac", points: 1.5 }),
    /points/,
  );
  for (const directive of [
    ".include x.lib",
    ".lib x.lib",
    ".step param R1 1 2 1",
    ".control",
  ])
    assert.throws(() => validateText(`title\n${directive}\n.end`));
});
test("malformed imported projects cannot reach the renderer", () => {
  for (const mutate of [
    (d: any) => (d.version = 2),
    (d: any) => (d.components[0].kind = "bad"),
    (d: any) => (d.wires[0].to = "missing.a"),
    (d: any) => (d.components[0].x = null),
    (d: any) => d.components.push(d.components[0]),
    (d: any) => (d.analysis = null),
  ]) {
    const d = rc();
    mutate(d);
    assert.throws(() => readDocument(JSON.stringify(d)));
  }
});

test("current probes split connectivity and removing one exactly restores the circuit", async () => {
  const { insertCurrentProbe, removeProbe, currentSignal } =
    await import("../src/probes");
  const d = rc(),
    wire = d.wires.find((w) => w.to === "R1.a")!;
  const next = insertCurrentProbe(d, wire.id);
  const sensor = next.wires.find((w) => w.id === wire.id)!;
  const nets = connectivity(next);
  assert.notEqual(nets.byEndpoint.get(wire.from), nets.byEndpoint.get(wire.to));
  assert.match(compile(next).text, /V_PROBE1 \S+ \S+ DC 0/);
  assert.ok(next.probes.includes(currentSignal(sensor)));
  assert.deepEqual(readDocument(JSON.stringify(next)), next);
  assert.equal(
    compile(removeProbe(next, currentSignal(sensor))).text,
    compile(d).text,
  );
});
test("sensor reversal changes the reference direction and survives wire branching", async () => {
  const { insertCurrentProbe } = await import("../src/probes");
  const d = rc(),
    wire = d.wires.find((w) => w.to === "R1.a")!;
  const next = insertCurrentProbe(d, wire.id, 0.7);
  const w = next.wires.find((w) => w.id === wire.id)!;
  const line = compile(next)
    .text.split("\n")
    .find((l) => l.startsWith("V_PROBE1 "))!
    .split(" ");
  w.currentProbe!.reversed = true;
  const reversed = compile(next)
    .text.split("\n")
    .find((l) => l.startsWith("V_PROBE1 "))!
    .split(" ");
  assert.equal(line[1], reversed[2]);
  assert.equal(line[2], reversed[1]);
  const split = splitWire(next, w, { x: 220, y: 160 }).doc;
  assert.equal(split.wires.filter((w) => w.currentProbe).length, 1);
  assert.deepEqual(readDocument(JSON.stringify(split)), split);
  assert.equal(compile(split).text, compile(next).text);
});
test("current sensors reject bypasses and reserve names without clashing with sources", async () => {
  const { insertCurrentProbe } = await import("../src/probes");
  const d = rc(),
    wire = d.wires.find((w) => w.to === "R1.a")!;
  d.wires.push({ ...clone(wire), id: "parallel" });
  assert.throws(() => insertCurrentProbe(d, wire.id), /parallel/);
  d.wires.pop();
  d.components.find((c) => c.id === "V1")!.id = "V_PROBE1";
  d.wires.forEach((w) => {
    w.from = w.from.replace("V1.", "V_PROBE1.");
    w.to = w.to.replace("V1.", "V_PROBE1.");
  });
  assert.equal(
    insertCurrentProbe(d, wire.id).wires.find((w) => w.id === wire.id)!
      .currentProbe!.id,
    "V_PROBE2",
  );
});
test("voltage probes stay attached and keep their colors when nets are renamed", async () => {
  const { addVoltageProbe, reconcileProbes, signalColor } =
    await import("../src/probes");
  const d = addVoltageProbe(rc(), "v(out)");
  const color = signalColor(d, "v(out)");
  const edited = clone(d);
  edited.junctions.find((j) => j.label === "out")!.label = "output";
  const next = reconcileProbes(d, edited);
  assert.ok(next.probes.includes("v(output)"));
  assert.ok(!next.probes.includes("v(out)"));
  assert.equal(signalColor(next, "v(output)"), color);
});
test("invalid sensor metadata and probe anchors are rejected on import", async () => {
  const { insertCurrentProbe } = await import("../src/probes");
  const d = rc(),
    wire = d.wires.find((w) => w.to === "R1.a")!;
  for (const mutate of [
    (d: any) =>
      (d.wires.find((w: any) => w.currentProbe).currentProbe.position = 2),
    (d: any) =>
      (d.wires.find((w: any) => w.currentProbe).currentProbe.id =
        "R_injection"),
    (d: any) => (d.probeAnchors = { "v(out)": { ref: "missing.a" } }),
    (d: any) => (d.probeColors = { "v(out)": "url(https://example.com)" }),
  ]) {
    const next = insertCurrentProbe(d, wire.id);
    mutate(next);
    assert.throws(() => readDocument(JSON.stringify(next)));
  }
});

test("probe drag stays on its net or current branch without changing the circuit", async () => {
  const {
    moveProbe,
    anchorFor,
    probePosition,
    insertCurrentProbe,
    voltageProbeOnNet,
    currentProbeOnNet,
  } = await import("../src/probes");
  const d = rc(),
    wire = d.wires.find((w) => w.to === "R1.a")!;
  const next = moveProbe(d, "v(out)", { x: 120, y: 160 }); // the unrelated input wire is nearby
  const anchor = anchorFor(next, "v(out)")!;
  assert.equal(connectivity(next).byEndpoint.get(anchor.ref), "out");
  assert.equal(compile(next).text, compile(d).text);
  const before = probePosition(d, anchorFor(d, "v(out)")!);
  const after = probePosition(next, anchor);
  assert.notDeepEqual({ x: before.x, y: before.y }, { x: after.x, y: after.y });
  const sensed = insertCurrentProbe(d, wire.id, 0.5);
  const moved = moveProbe(sensed, "i(v_probe1)", { x: 280, y: 500 });
  assert.equal(compile(moved).text, compile(sensed).text);
  assert.notEqual(
    moved.wires.find((w) => w.id === wire.id)!.currentProbe!.position,
    sensed.wires.find((w) => w.id === wire.id)!.currentProbe!.position,
  );
  assert.ok(voltageProbeOnNet(sensed, wire.to));
  assert.ok(currentProbeOnNet(sensed, wire.from));
  assert.ok(currentProbeOnNet(sensed, wire.to));
  assert.equal(currentProbeOnNet(sensed, "R1.b"), undefined);
});
