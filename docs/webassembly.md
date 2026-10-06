# WebAssembly browser build

The Emscripten target produces `neospice.mjs` and `neospice.wasm`, plus a small
JavaScript API, TypeScript declarations and a Web Worker demo. The module runs
locally in a browser or Node. It does not upload netlists or contact a simulation
service. Emscripten **6.0.11** was used for validation.

## Build

Install the [official Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
and activate the pinned compiler version in your SDK directory:

```sh
./emsdk install 6.0.11
./emsdk activate 6.0.11
source ./emsdk_env.sh
```

From the neospice repository:

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j8
node tests/wasm/smoke.mjs build-wasm/wasm
python3 -m http.server 8080 --bind 127.0.0.1 --directory build-wasm/wasm
```

Open `http://127.0.0.1:8080`. Serve over HTTP rather than opening the HTML file
with a `file:` URL. A production host must serve `.wasm` as `application/wasm`
and allow the module and worker assets from their configured origin. No
cross-origin-isolation headers or SharedArrayBuffer are needed for this
single-threaded build. Deployment is not performed by building the target.

The WASM configuration defaults native reference tests and CLI off, skips
OpenBLAS/SLEEF/OpenMP/host-thread linkage, and preserves C++ exception handling.
It uses the same core and device sources as the native build. Pole-zero analysis
has an explicit unsupported stub because it requires LAPACK; the browser-facing
API does not expose that analysis. Native defaults are unchanged.

## JavaScript

```js
import {createNeospice} from './neospice-api.mjs';
const {Circuit, capabilities} = await createNeospice();
const circuit = new Circuit(`Divider
V1 in 0 DC 10 AC 1
R1 in out 1k
R2 out 0 1k
.op
.end
`);
try {
  console.log(circuit.dc().voltages['v(out)']); // 5
  circuit.updateParam('r2', 2000);
  console.log(circuit.dc().voltages['v(out)']); // 6.666...
  const ac = circuit.ac({mode: 'dec', points: 20, start: 10, stop: 1e5});
  console.log(ac.frequency, ac.voltages['v(out)']); // {real, imag} pairs
  console.log(circuit.sensitivity(['v(out)'], ['r1', 'r2']).jacobian);
  console.log(circuit.reuseStatistics());
} finally {
  circuit.dispose();
}
```

`createNeospice` is asynchronous; subsequent circuit calls are synchronous.
Run them in a Worker for responsive interfaces. `wasm/worker.mjs` shows this
pattern; terminating the Worker cancels an in-flight simulation. The demo keeps
one circuit for slider updates and rebuilds it when the netlist changes.

Result arrays and signal maps are copied into JavaScript-owned data and remain
valid after disposal. Always dispose circuits to free their C++ resources;
repeated disposal is harmless and further use raises a JavaScript `Error`.
Parse/simulation failures also become JavaScript errors with readable messages.
TypeScript resolves `neospice-api.d.mts` beside the module.

`transient({step, stop})` returns time/voltage/current arrays.
`sensitivityAC(outputs, parameters, frequencies)` returns complex Jacobians for
the [supported linear scope](adjoint-gradients.md). `setTemperature(celsius)`
updates simulation temperature. `dc()` and `ac()` use the
[incremental solver caches](incremental-simulation.md).

## Scope and validation

- Inline SPICE netlists and built-in device models; inline subcircuits and model
  cards. External `.include` / `.lib` and nested `.step` are rejected by the JS
  interface. Analysis methods select what runs rather than executing directives.
- DC, AC, transient, supported adjoint derivatives and value updates are exposed.
  No browser parallel sweeps, Monte Carlo wrapper, pole-zero, noise or raw-file
  interface is exposed. The full native API remains available separately.
- The WASM binary was about **2.9 MiB** uncompressed in the verified release
  build; the generated loader was about **108 KiB**. This is not a load-time or
  simulation-performance claim. Browser memory and long-run resource limits
  depend on the workload.
- Node smoke tests cover analytical values, DC/AC/transient, gradients, update
  validation, ownership, failure handling and capability reporting.
  `tests/wasm/native_reference.py output.json` produces six native device
  comparisons; pass that JSON as the third argument to `smoke.mjs`. These are
  native/WASM checks, not a replacement ngspice compatibility baseline.
- `tests/wasm/browser-smoke.mjs` uses Playwright to check worker loading, three
  analyses, the slider, errors and reset. Install Playwright and its Chromium
  browser before running it against the local HTTP server:

  ```sh
  node tests/wasm/browser-smoke.mjs http://127.0.0.1:8080/
  ```

  It imports `playwright` by default. `PLAYWRIGHT_MODULE` can point to an existing
  installation's `index.mjs`; `CHROME_BINARY` can select an installed Chrome.
  This checkout was exercised in headless Chrome, not Safari or Firefox.

The [Emscripten Embind documentation](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html)
describes the underlying bindings and ownership model. The wrapper keeps native
handles private and exposes copied result data.
