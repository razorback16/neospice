import assert from 'node:assert/strict';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {readFile} from 'node:fs/promises';

const directory = path.resolve(process.argv[2] ?? 'build-wasm/wasm');
const {createNeospice} = await import(pathToFileURL(path.join(directory, 'neospice-api.mjs')));
const api = await createNeospice({print() {}, printErr() {}});
const near = (actual, expected, tolerance = 1e-10) => assert.ok(Math.abs(actual - expected) <= tolerance,
  `${actual} differs from ${expected} by ${Math.abs(actual - expected)} (limit ${tolerance})`);
const deck = 'RC\nV1 in 0 DC 2 AC 1 PULSE(0 2 0 1u 1u 1 2)\nR1 in out 1k\nR2 out 0 1k\nC1 out 0 1u\n.op\n.end\n';
const circuit = new api.Circuit(deck);
const dc = circuit.dc();
assert.equal(dc.status.converged, true);
near(dc.voltages['v(out)'], 1);
circuit.updateParam('r2', 2000);
near(circuit.dc().voltages['v(out)'], 4 / 3);
assert.equal(circuit.reuseStatistics().dcSymbolicAnalyses, 1);
assert.throws(() => circuit.updateParam('r2', 0), /Invalid passive/);
assert.throws(() => circuit.updateParam('r2', NaN), /finite/);
near(circuit.dc().voltages['v(out)'], 4 / 3);
const gradient = circuit.sensitivity(['v(out)'], ['r1', 'r2', 'v1']);
near(gradient.jacobian[0][0], -4 / 9000);
near(gradient.jacobian[0][1], 2 / 9000);
assert.equal(gradient.adjointSolves, 1);
const ac = circuit.ac({mode: 'lin', points: 3, start: 100, stop: 300});
assert.deepEqual(ac.frequency, [100, 200, 300]);
for (let i = 0; i < ac.frequency.length; i++) {
  const b = 2 * Math.PI * ac.frequency[i] * 0.001;
  near(ac.voltages['v(out)'][i].real, 1.5 / (2.25 + b*b));
  near(ac.voltages['v(out)'][i].imag, -b / (2.25 + b*b));
}
const ag = circuit.sensitivityAC(['v(out)'], ['c1'], [100]);
assert.equal(ag.adjointSolves, 1);
assert.ok(Number.isFinite(ag.jacobian[0][0][0].real));
const tran = circuit.transient({step: 1e-5, stop: 0.005});
assert.equal(tran.status.converged, true);
assert.ok(tran.time.length > 20);
near(tran.voltages['v(out)'].at(-1), 4 / 3, 0.001);
circuit.dispose();
circuit.dispose();
assert.throws(() => circuit.dc(), /disposed/);
near(dc.voltages['v(out)'], 1); // returned values own their data
assert.throws(() => new api.Circuit('Include\n.include model.lib\n.end\n'), /inline/);
const failed = new api.Circuit('Conflict\nV1 out 0 1\nV2 out 0 2\nR1 out 0 1k\n.end\n');
assert.throws(() => failed.dc(), /failed to converge/);
failed.dispose();
assert.equal(api.capabilities.parallel, false);
assert.equal(api.capabilities.poleZero, false);

// Optional comparison against results emitted by the native library.
if (process.argv[3]) {
  const cases = JSON.parse(await readFile(process.argv[3], 'utf8'));
  for (const test of cases) {
    const c = new api.Circuit(test.netlist);
    const actual = c.dc();
    for (const [signal, expected] of Object.entries(test.voltages))
      near(actual.voltages[signal], expected, 1e-8 + Math.abs(expected) * 1e-8);
    c.dispose();
  }
  console.log(`${cases.length} native/WASM circuit comparisons passed`);
}
console.log('WebAssembly API smoke tests passed');
