import assert from "node:assert/strict";
import { mkdtemp, writeFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { spawnSync } from "node:child_process";
import { GALLERY } from "../src/gallery";
import { compile, analysisOptions } from "../src/netlist";
import { insertCurrentProbe, removeProbe, currentSignal } from "../src/probes";
import { clone } from "../src/model";
import type { Result, Complex } from "../src/simulation";
const { createNeospice } = await import(
  pathToFileURL(
    resolve(process.env.WASM_DIR || "public/simulator", "neospice-api.mjs"),
  ).href
);
const { Circuit } = await createNeospice({ print() {}, printErr() {} });
const reference = process.env.GALLERY_REFERENCE;
if (process.argv.includes("--reference") && !reference)
  throw Error("Set GALLERY_REFERENCE to the ngspice 47 comparison executable.");
const directory = await mkdtemp(join(tmpdir(), "neospice-probes-"));
try {
  for (const mode of ["dc", "ac", "transient"] as const)
    for (const reversed of [false, true]) {
      const original = clone(GALLERY[0].document);
      original.analysis.mode = mode;
      const d = insertCurrentProbe(
        original,
        original.wires.find((w) => w.to === "R1.a")!.id,
      );
      const wire = d.wires.find((w) => w.currentProbe)!;
      wire.currentProbe!.reversed = reversed;
      const key = currentSignal(wire),
        { text } = compile(d),
        c = new Circuit(text);
      let result: Result;
      try {
        result =
          mode === "dc"
            ? c.dc()
            : mode === "ac"
              ? c.ac(analysisOptions(d.analysis))
              : c.transient(analysisOptions(d.analysis));
      } finally {
        c.dispose();
      }
      assert.ok(result.status.converged);
      const current = result.currents[key];
      const input = result.voltages["v(in)"],
        output = result.voltages["v(out)"];
      const sign = reversed ? -1 : 1;
      if (mode === "dc")
        assert.ok(Math.abs((current as number) - sign / 11000) < 1e-12);
      else
        for (let i = 0; i < (current as unknown[]).length; i++) {
          const actual = (current as (number | Complex)[])[i],
            vin = (input as (number | Complex)[])[i],
            vout = (output as (number | Complex)[])[i];
          if (typeof actual === "number")
            assert.ok(
              Math.abs(
                actual - (sign * ((vin as number) - (vout as number))) / 1000,
              ) < 1e-10,
            );
          else
            for (const part of ["real", "imag"] as const)
              assert.ok(
                Math.abs(
                  actual[part] -
                    (sign *
                      ((vin as Complex)[part] - (vout as Complex)[part])) /
                      1000,
                ) < 1e-10,
              );
        }
      assert.equal(compile(removeProbe(d, key)).text, compile(original).text);
      console.log(
        `${mode}, ${reversed ? "reverse" : "forward"}: current matches Ohm's law and removal restores original netlist`,
      );
      if (reference) {
        const axis = result.time ?? result.frequency ?? [];
        const voltages = Object.entries(result.voltages),
          currents = Object.entries(result.currents);
        const lines = [
          `${mode} ${axis.length} ${voltages.length} ${currents.length}`,
        ];
        if (mode !== "dc") lines.push(axis.join(" "));
        for (const [name, values] of [...voltages, ...currents])
          lines.push(
            name +
              " " +
              (Array.isArray(values) ? values : [values])
                .flatMap((v: number | Complex) =>
                  typeof v === "number" ? [v] : [v.real, v.imag],
                )
                .join(" "),
          );
        const deck = join(directory, "probe.cir"),
          data = join(directory, "probe.txt");
        await writeFile(deck, text);
        await writeFile(data, lines.join("\n"));
        // Reuse the RC fixture's existing strict comparator contract, unchanged.
        const checked = spawnSync(
          resolve(reference),
          [deck, data, "rc-filter"],
          { encoding: "utf8", timeout: 30000 },
        );
        process.stdout.write(checked.stdout);
        process.stderr.write(checked.stderr);
        assert.equal(
          checked.status,
          0,
          checked.error?.message ??
            "ngspice 47 current-probe comparison failed",
        );
      }
    }
} finally {
  await rm(directory, { recursive: true, force: true });
}
