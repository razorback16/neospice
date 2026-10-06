import {
  blankDocument,
  PARTS,
  type CircuitDocument,
  type Component,
  type Kind,
  type Point,
} from "./model";
export type Tune = {
  part: string;
  property: string;
  label: string;
  unit: string;
  min: number;
  max: number;
  log?: boolean;
};
export type Example = {
  id: string;
  number: string;
  title: string;
  category: string;
  description: string;
  lesson: string;
  accent: string;
  tunes: Tune[];
  document: CircuitDocument;
};
function builder(id: string, title: string) {
  const d = blankDocument();
  d.id = `example-${id}`;
  d.exampleId = id;
  d.title = title;
  let count = 0;
  const part = (
    id: string,
    kind: Kind,
    x: number,
    y: number,
    rotation = 0,
    props: Record<string, string> = {},
  ) => {
    const c: Component = {
      id,
      kind,
      x,
      y,
      rotation,
      props: { ...PARTS[kind].defaults, ...props },
    };
    d.components.push(c);
    return c;
  };
  const wire = (from: string, to: string, bends: Point[] = []) =>
    d.wires.push({ id: `wire${++count}`, from, to, bends });
  const net = (
    label: string,
    x: number,
    y: number,
    refs: string[],
    initial?: string,
  ) => {
    const id = `junction${++count}`;
    d.junctions.push({
      id,
      x,
      y,
      label,
      ...(initial !== undefined ? { initial } : {}),
    });
    refs.forEach((ref) => wire(id, ref));
    return id;
  };
  const ground = (id: string, x: number, y: number, refs: string[]) => {
    part(id, "G", x, y);
    refs.forEach((ref) => wire(`${id}.g`, ref));
  };
  return { d, part, wire, net, ground };
}
const examples: Omit<Example, "number">[] = [];
{
  const { d, part, net, ground } = builder("rc-filter", "RC low-pass filter");
  part("V1", "V", 120, 240, 0, {
    dc: "1",
    wave: "pulse",
    high: "1",
    width: "20m",
    period: "40m",
  });
  part("R1", "R", 340, 160);
  part("C1", "C", 540, 240, 90);
  part("R2", "R", 760, 240, 90, { value: "10k" });
  net("in", 120, 160, ["V1.p", "R1.a"]);
  net("out", 540, 160, ["R1.b", "C1.a", "R2.a"]);
  ground("G1", 120, 360, ["V1.n"]);
  ground("G2", 540, 360, ["C1.b"]);
  ground("G3", 760, 360, ["R2.b"]);
  d.probes = ["v(in)", "v(out)"];
  d.analysis = { ...d.analysis, step: "10u", stop: "10m" };
  examples.push({
    id: "rc-filter",
    title: d.title,
    category: "FILTERS",
    description: "Small circuit. Smoother signals.",
    lesson:
      "The capacitor takes time to charge. Increase R₁ or C₁ to slow the response and lower the cutoff frequency. The 10 kΩ load slightly reduces the final output.",
    accent: "teal",
    tunes: [
      {
        part: "R1",
        property: "value",
        label: "Resistance",
        unit: "Ω",
        min: 100,
        max: 10000,
        log: true,
      },
      {
        part: "C1",
        property: "value",
        label: "Capacitance",
        unit: "F",
        min: 1e-7,
        max: 1e-5,
        log: true,
      },
    ],
    document: d,
  });
}
{
  const { d, part, net, ground } = builder("rlc-resonator", "RLC resonator");
  part("V1", "V", 120, 240, 0, {
    dc: "0",
    wave: "pulse",
    rise: "1n",
    fall: "1n",
    width: "10m",
    period: "20m",
  });
  part("R1", "R", 320, 160, 0, { value: "100" });
  part("L1", "L", 520, 160, 0, { value: "10m" });
  part("C1", "C", 720, 240, 90, { value: "100n" });
  net("in", 120, 160, ["V1.p", "R1.a"]);
  net("mid", 420, 160, ["R1.b", "L1.a"]);
  net("out", 720, 160, ["L1.b", "C1.a"]);
  ground("G1", 120, 360, ["V1.n"]);
  ground("G2", 720, 360, ["C1.b"]);
  d.probes = ["v(in)", "v(out)"];
  d.analysis = {
    ...d.analysis,
    step: "500n",
    stop: "500u",
    start: "10",
    end: "1meg",
  };
  examples.push({
    id: "rlc-resonator",
    title: d.title,
    category: "RESONANCE",
    description: "Watch energy move back and forth.",
    lesson:
      "Energy moves between the inductor and capacitor. Resistance dissipates it: increase R₁ to turn a ringing response into a smoothly damped one.",
    accent: "blue",
    tunes: [
      {
        part: "R1",
        property: "value",
        label: "Damping resistance",
        unit: "Ω",
        min: 10,
        max: 1000,
        log: true,
      },
      {
        part: "L1",
        property: "value",
        label: "Inductance",
        unit: "H",
        min: 1e-3,
        max: 1e-1,
        log: true,
      },
    ],
    document: d,
  });
}
{
  const { d, part, net, ground } = builder(
    "bridge-rectifier",
    "Bridge rectifier",
  );
  part("V1", "V", 120, 260, 0, {
    dc: "0",
    wave: "sin",
    amplitude: "5",
    frequency: "1k",
  });
  part("D1", "D", 340, 160, 0);
  part("D2", "D", 340, 360, 180);
  part("D3", "D", 540, 160, 180);
  part("D4", "D", 540, 360, 0);
  part("R1", "R", 760, 240, 90, { value: "1k" });
  part("C1", "C", 960, 240, 90, { value: "10u" });
  // Two floating AC terminals, DC output on the top rail, grounded return below.
  net("acp", 240, 220, ["V1.p", "D1.a", "D2.b"]);
  net("acn", 640, 300, ["V1.n", "D3.a", "D4.b"]);
  net("out", 440, 80, ["D1.b", "D3.b", "R1.a", "C1.a"]);
  ground("G1", 440, 440, ["D2.a", "D4.a", "R1.b", "C1.b"]);
  d.probes = ["v(out)", "v(acp)", "v(acn)"];
  d.analysis = { ...d.analysis, step: "5u", stop: "8m" };
  examples.push({
    id: "bridge-rectifier",
    title: d.title,
    category: "POWER",
    description: "Turn both halves of AC into DC.",
    lesson:
      "Four diodes steer both input polarities into a positive output. The reservoir capacitor fills near each peak; a larger capacitor or lighter load reduces ripple.",
    accent: "amber",
    tunes: [
      {
        part: "C1",
        property: "value",
        label: "Reservoir capacitor",
        unit: "F",
        min: 1e-6,
        max: 1e-4,
        log: true,
      },
      {
        part: "R1",
        property: "value",
        label: "Load resistance",
        unit: "Ω",
        min: 100,
        max: 10000,
        log: true,
      },
    ],
    document: d,
  });
}
{
  const { d, part, net, ground } = builder("diode-clipper", "Diode clipper");
  part("V1", "V", 120, 240, 0, {
    dc: "0",
    wave: "sin",
    amplitude: "2",
    frequency: "1k",
  });
  part("R1", "R", 340, 160);
  part("D1", "D", 540, 240, 90);
  part("D2", "D", 740, 240, 270);
  net("in", 120, 160, ["V1.p", "R1.a"]);
  net("out", 540, 160, ["R1.b", "D1.a", "D2.b"]);
  ground("G1", 120, 360, ["V1.n"]);
  ground("G2", 540, 360, ["D1.b"]);
  ground("G3", 740, 360, ["D2.a"]);
  d.probes = ["v(in)", "v(out)"];
  d.analysis = { ...d.analysis, step: "2u", stop: "3m" };
  examples.push({
    id: "diode-clipper",
    title: d.title,
    category: "WAVESHAPING",
    description: "Give a sine wave a little attitude.",
    lesson:
      "The antiparallel diodes conduct on opposite half-cycles, rounding off the peaks. Raise the input amplitude to hear with your eyes how clipping changes a waveform.",
    accent: "rose",
    tunes: [
      {
        part: "V1",
        property: "amplitude",
        label: "Input amplitude",
        unit: "V",
        min: 0.1,
        max: 5,
      },
      {
        part: "R1",
        property: "value",
        label: "Series resistance",
        unit: "Ω",
        min: 100,
        max: 10000,
        log: true,
      },
    ],
    document: d,
  });
}
{
  const { d, part, net, ground } = builder(
    "bjt-amplifier",
    "Common-emitter amplifier",
  );
  part("VCC", "V", 100, 260, 0, { dc: "12", ac: "0" });
  part("VIN", "V", 120, 500, 0, {
    dc: "0",
    wave: "sin",
    amplitude: "5m",
    frequency: "1k",
  });
  part("CIN", "C", 300, 420, 0, { value: "1u" });
  part("R1", "R", 440, 260, 90, { value: "56k" });
  part("R2", "R", 440, 520, 90, { value: "12k" });
  part("RC", "R", 680, 260, 90, { value: "3.3k" });
  part("Q1", "QN", 660, 420);
  part("RE", "R", 680, 560, 90, { value: "1k" });
  part("CE", "C", 840, 560, 90, { value: "100u" });
  part("COUT", "C", 860, 360, 0, { value: "1u" });
  part("RL", "R", 1020, 480, 90, { value: "10k" });
  net("vcc", 440, 140, ["VCC.p", "R1.a", "RC.a"]);
  net("in", 120, 420, ["VIN.p", "CIN.a"]);
  net("base", 440, 420, ["CIN.b", "R1.b", "R2.a", "Q1.b"]);
  net("col", 680, 360, ["RC.b", "Q1.c", "COUT.a"]);
  net("emit", 680, 500, ["Q1.e", "RE.a", "CE.a"]);
  net("out", 1020, 360, ["COUT.b", "RL.a"]);
  ground("G1", 100, 340, ["VCC.n"]);
  ground("G2", 120, 600, ["VIN.n"]);
  ground("G3", 440, 620, ["R2.b"]);
  ground("G4", 680, 660, ["RE.b"]);
  ground("G5", 840, 660, ["CE.b"]);
  ground("G6", 1020, 600, ["RL.b"]);
  d.probes = ["v(in)", "v(out)"];
  d.analysis = {
    ...d.analysis,
    mode: "ac",
    start: "10",
    end: "10meg",
    step: "2u",
    stop: "3m",
  };
  examples.push({
    id: "bjt-amplifier",
    title: d.title,
    category: "AMPLIFIERS",
    description: "A small signal, with a bigger voice.",
    lesson:
      "A biased NPN transistor turns a small base-voltage change into a larger, inverted collector-voltage change. Coupling capacitors block DC; the emitter bypass capacitor shapes low-frequency gain.",
    accent: "violet",
    tunes: [
      {
        part: "RC",
        property: "value",
        label: "Collector resistor",
        unit: "Ω",
        min: 1000,
        max: 4700,
      },
      {
        part: "CE",
        property: "value",
        label: "Bypass capacitor",
        unit: "F",
        min: 1e-6,
        max: 1e-3,
        log: true,
      },
    ],
    document: d,
  });
}
{
  const { d, part, net, ground } = builder(
    "active-filter",
    "Sallen–Key filter",
  );
  part("V1", "V", 100, 280, 0, {
    dc: "0",
    wave: "pulse",
    rise: "1u",
    fall: "1u",
    width: "10m",
    period: "20m",
  });
  part("R1", "R", 280, 200, 0, { value: "10k" });
  part("R2", "R", 480, 200, 0, { value: "10k" });
  part("C1", "C", 480, 80, 0, { value: "10n" });
  part("C2", "C", 600, 320, 90, { value: "10n" });
  part("E1", "OP", 780, 180);
  net("in", 100, 200, ["V1.p", "R1.a"]);
  net("mid", 380, 200, ["R1.b", "R2.a", "C1.a"]);
  net("sense", 600, 200, ["R2.b", "C2.a", "E1.plus"]);
  net("out", 920, 80, ["C1.b", "E1.out"]);
  // Unity-gain feedback, routed above the amplifier.
  const j = d.junctions.find((j) => j.label === "out")!;
  d.wires.push({
    id: "feedback",
    from: j.id,
    to: "E1.minus",
    bends: [
      { x: 700, y: 80 },
      { x: 700, y: 160 },
    ],
  });
  ground("G1", 100, 420, ["V1.n"]);
  ground("G2", 600, 420, ["C2.b"]);
  d.probes = ["v(in)", "v(out)"];
  d.analysis = {
    ...d.analysis,
    mode: "ac",
    start: "10",
    end: "1meg",
    step: "1u",
    stop: "1m",
  };
  examples.push({
    id: "active-filter",
    title: d.title,
    category: "ACTIVE FILTERS",
    description: "Two poles. One elegant response.",
    lesson:
      "A unity-gain buffer isolates this second-order low-pass network. Equal R and C values give Q = 0.5. The op-amp is an idealized finite-gain VCVS without supply rails or bandwidth limits.",
    accent: "teal",
    tunes: [
      {
        part: "R1",
        property: "value",
        label: "First resistance",
        unit: "Ω",
        min: 1000,
        max: 100000,
        log: true,
      },
      {
        part: "C2",
        property: "value",
        label: "Shunt capacitance",
        unit: "F",
        min: 1e-9,
        max: 1e-7,
        log: true,
      },
    ],
    document: d,
  });
}
const mosModels = {
  NMOD: ".model NMOD NMOS LEVEL=14 VTH0=0.4 U0=0.04 TOXE=2e-9",
  PMOD: ".model PMOD PMOS LEVEL=14 VTH0=-0.4 U0=0.02 TOXE=2e-9",
};
{
  const { d, part, net, ground, wire } = builder(
    "cmos-inverter",
    "CMOS inverter",
  );
  d.models = { ...d.models, ...mosModels };
  part("VDD", "V", 100, 260, 0, { dc: "1.8", ac: "0" });
  part("VIN", "V", 300, 360, 0, {
    dc: "0",
    wave: "pulse",
    high: "1.8",
    rise: "100p",
    fall: "100p",
    width: "5n",
    period: "10n",
  });
  part("M1", "MP", 580, 200, 180, { model: "PMOD", w: "2u", l: "100n" });
  part("M2", "MN", 540, 400, 0, { model: "NMOD", w: "1u", l: "100n" });
  part("CL", "C", 820, 400, 90, { value: "10f" });
  net("vdd", 560, 80, ["VDD.p", "M1.s", "M1.b"]);
  net("in", 360, 300, ["VIN.p", "M1.g", "M2.g"]);
  net("out", 720, 300, ["M1.d", "M2.d", "CL.a"]);
  ground("G1", 100, 480, ["VDD.n"]);
  ground("G2", 300, 480, ["VIN.n"]);
  ground("G3", 560, 520, ["M2.s", "M2.b"]);
  ground("G4", 820, 520, ["CL.b"]);
  void wire;
  d.probes = ["v(in)", "v(out)"];
  d.analysis = { ...d.analysis, step: "10p", stop: "20n" };
  examples.push({
    id: "cmos-inverter",
    title: d.title,
    category: "TRANSISTORS",
    description: "The little switch behind digital logic.",
    lesson:
      "The PMOS pulls the output high when the input is low; the NMOS pulls it low when the input is high. Add load capacitance to see slower edges and propagation delay. Uses inline BSIM4 models.",
    accent: "blue",
    tunes: [
      {
        part: "CL",
        property: "value",
        label: "Output load",
        unit: "F",
        min: 1e-15,
        max: 1e-12,
        log: true,
      },
      {
        part: "VIN",
        property: "width",
        label: "Pulse width",
        unit: "s",
        min: 1e-9,
        max: 8e-9,
      },
    ],
    document: d,
  });
}
{
  const { d, part, net, ground } = builder(
    "ring-oscillator",
    "CMOS ring oscillator",
  );
  d.models = { ...d.models, ...mosModels };
  part("VDD", "V", 40, 240, 0, { dc: "1.8", ac: "0" });
  ground("G0", 40, 360, ["VDD.n"]);
  for (let i = 1; i <= 5; i++) {
    const x = 180 + (i - 1) * 200;
    part(`M${i}p`, "MP", x + 40, 160, 180, {
      model: "PMOD",
      w: "2u",
      l: "100n",
    });
    part(`M${i}n`, "MN", x, 400, 0, { model: "NMOD", w: "1u", l: "100n" });
    ground(`G${i}`, x + 20, 500, [`M${i}n.s`, `M${i}n.b`]);
  }
  net("vdd", 200, 60, [
    "VDD.p",
    ...Array.from({ length: 5 }, (_, i) => [
      `M${i + 1}p.s`,
      `M${i + 1}p.b`,
    ]).flat(),
  ]);
  for (let i = 1; i <= 5; i++) {
    const next = i === 5 ? 1 : i + 1;
    const x = 200 + (i - 1) * 200;
    const id = net(
      `n${i}`,
      x,
      280,
      [`M${i}p.d`, `M${i}n.d`],
      i % 2 ? "0" : "1.8",
    );
    if (i < 5) {
      d.wires.push(
        {
          id: `gatep${i}`,
          from: id,
          to: `M${next}p.g`,
          bends: [
            { x: x + 100, y: 280 },
            { x: x + 100, y: 220 },
            { x: x + 280, y: 220 },
          ],
        },
        {
          id: `gaten${i}`,
          from: id,
          to: `M${next}n.g`,
          bends: [
            { x: x + 100, y: 280 },
            { x: x + 100, y: 400 },
          ],
        },
      );
    } else {
      d.wires.push(
        {
          id: "feedbackp",
          from: id,
          to: "M1p.g",
          bends: [
            { x: 1140, y: 280 },
            { x: 1140, y: 580 },
            { x: 100, y: 580 },
            { x: 100, y: 220 },
            { x: 260, y: 220 },
          ],
        },
        {
          id: "feedbackn",
          from: id,
          to: "M1n.g",
          bends: [
            { x: 1140, y: 280 },
            { x: 1140, y: 580 },
            { x: 100, y: 580 },
            { x: 100, y: 400 },
          ],
        },
      );
    }
  }
  d.probes = ["v(n1)", "v(n3)", "v(n5)"];
  d.analysis = { ...d.analysis, step: "1p", stop: "5n" };
  examples.push({
    id: "ring-oscillator",
    title: d.title,
    category: "OSCILLATORS",
    description: "Five inverters. An endless conversation.",
    lesson:
      "An odd number of inverters cannot settle into a consistent logic state. Each stage adds delay, and a transition keeps traveling around the loop. Initial node voltages start the oscillation; inline BSIM4 models capture transistor capacitance.",
    accent: "violet",
    tunes: [
      {
        part: "VDD",
        property: "dc",
        label: "Supply voltage",
        unit: "V",
        min: 1.2,
        max: 2.4,
      },
      {
        part: "M1n",
        property: "w",
        label: "Stage 1 NMOS width",
        unit: "m",
        min: 0.5e-6,
        max: 3e-6,
      },
    ],
    document: d,
  });
}
export const GALLERY: Example[] = examples.map((e, i) => ({
  ...e,
  number: String(i + 1).padStart(2, "0"),
}));
