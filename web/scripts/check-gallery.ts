import assert from "node:assert/strict";
import { mkdtemp, writeFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve, join } from "node:path";
import { pathToFileURL } from "node:url";
import { spawnSync } from "node:child_process";
import { GALLERY } from "../src/gallery";
import { compile, analysisOptions } from "../src/netlist";
import { clone } from "../src/model";
import type { Complex, Result } from "../src/simulation";
const source = resolve(process.env.WASM_DIR || "public/simulator");
const { createNeospice } = await import(
  pathToFileURL(join(source, "neospice-api.mjs")).href
);
const { Circuit } = await createNeospice({ print() {}, printErr() {} });
const reference = process.env.GALLERY_REFERENCE;
if (process.argv.includes("--reference") && !reference)
  throw new Error(
    "Set GALLERY_REFERENCE to the neospice_gallery_reference executable.",
  );
const directory = await mkdtemp(join(tmpdir(), "neospice-gallery-"));
let failures = 0;
try {
  for (const example of GALLERY) {
    const doc = example.document;
    const { text } = compile(doc);
    const circuit = new Circuit(text);
    try {
      const options = analysisOptions(doc.analysis);
      const result: Result =
        doc.analysis.mode === "ac"
          ? circuit.ac(options)
          : doc.analysis.mode === "transient"
            ? circuit.transient(options)
            : circuit.dc();
      assert.ok(result.status.converged, `${example.id} did not converge`);
      const axis = result.time ?? result.frequency ?? [];
      assert.ok(axis.length > 20);
      axis.forEach((v, i) =>
        assert.ok(Number.isFinite(v) && (!i || v > axis[i - 1])),
      );
      for (const [key, values] of Object.entries({
        ...result.voltages,
        ...result.currents,
      })) {
        assert.ok(Array.isArray(values));
        assert.equal(values.length, axis.length);
        for (const v of values)
          assert.ok(
            typeof v === "number"
              ? Number.isFinite(v)
              : Number.isFinite(v.real) && Number.isFinite(v.imag),
            `${key} has nonfinite data`,
          );
      }
      for (const key of doc.probes)
        assert.ok(result.voltages[key] !== undefined, `Missing probe ${key}`);
      if (example.id === "rc-filter")
        assert.ok(
          Math.abs((result.voltages["v(out)"] as number[]).at(-1)! - 10 / 11) <
            3e-5,
        );
      if (example.id === "ring-oscillator") {
        const y = result.voltages["v(n1)"] as number[];
        assert.ok(Math.max(...y) - Math.min(...y) > 1.5);
      }
      if (example.id === "bridge-rectifier")
        assert.ok((result.voltages["v(out)"] as number[]).at(-1)! > 3);
      console.log(`${example.id}: WASM converged, ${axis.length} samples`);
      if (reference) {
        const deck = join(directory, example.id + ".cir");
        const actual = join(directory, example.id + ".txt");
        await writeFile(deck, text);
        const voltages = Object.entries(result.voltages),
          currents = Object.entries(result.currents);
        const lines = [
          `${doc.analysis.mode} ${axis.length} ${voltages.length} ${currents.length}`,
          axis.join(" "),
        ];
        for (const [key, values] of [...voltages, ...currents])
          lines.push(
            key +
              " " +
              (values as (number | Complex)[])
                .flatMap((v) =>
                  typeof v === "number" ? [v] : [v.real, v.imag],
                )
                .join(" "),
          );
        await writeFile(actual, lines.join("\n"));
        const r = spawnSync(resolve(reference), [deck, actual, example.id], {
          encoding: "utf8",
          timeout: 30000,
        });
        process.stdout.write(r.stdout || "");
        if (r.status !== 0) {
          failures++;
          process.stderr.write(r.stderr || String(r.error || ""));
        }
      }
      // Check that every exposed tuning range remains loadable and convergent.
      for (const tune of example.tunes)
        for (const value of [tune.min, tune.max]) {
          const changed = clone(doc);
          changed.components.find((c) => c.id === tune.part)!.props[
            tune.property
          ] = String(value);
          const c = new Circuit(compile(changed).text);
          try {
            const o = analysisOptions(changed.analysis);
            const r = changed.analysis.mode === "ac" ? c.ac(o) : c.transient(o);
            assert.ok(
              r.status.converged,
              `${example.id}: ${tune.part}=${value}`,
            );
          } finally {
            c.dispose();
          }
        }
    } catch (e) {
      failures++;
      console.error(`${example.id}:`, e);
    } finally {
      circuit.dispose();
    }
  }
} finally {
  await rm(directory, { recursive: true, force: true });
}
assert.equal(failures, 0, `${failures} gallery checks failed`);
console.log(
  `All 8 gallery circuits and 32 tuning endpoints passed${reference ? " with ngspice 47 comparisons" : ""}.`,
);
