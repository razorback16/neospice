# Circuit Lab

Circuit Lab is neospice's static browser application: a visual schematic editor,
editable example gallery, separate SPICE editor, and interactive DC, AC and
transient results. Simulation runs locally in a Web Worker using the existing
WebAssembly API. There is no simulation service or account system.

**[Open Circuit Lab](https://razorback16.github.io/neospice/)** on GitHub Pages.

## Build and run

Use Node **22.22.0** (or a compatible newer Node) and the pinned Emscripten
**6.0.11** SDK described in [WebAssembly](webassembly.md).

From the repository root, after activating the SDK:

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j8
npm --prefix web ci
npm --prefix web run build
npm --prefix web run preview
```

Open **http://127.0.0.1:48173/**. `preview` serves only `web/dist` on loopback;
it does not expose the checkout, directory listings, or a development server.
WASM is served as `application/wasm`. Stop this foreground preview with Ctrl+C.

`WASM_DIR` can select another CMake artifact directory. Relative paths are
resolved from `web/`:

```sh
WASM_DIR=/absolute/build/wasm npm --prefix web run build
```

For frontend development, stage the module once with `npm --prefix web run
stage:wasm`, then run `npm --prefix web run dev`. Rebuild and restage after engine
changes. Generated WASM files, dependency directories, test reports and build
artifacts are ignored by Git.

## External preview

With `cloudflared` installed, stop any foreground preview and run:

```sh
npm --prefix web run preview:start
npm --prefix web run preview:status
```

The script starts the static server on **48173** and a Cloudflare Quick Tunnel
in the background. `preview:status` prints the temporary HTTPS URL after
Cloudflare allocates it. Logs and process IDs are in `web/.preview/`:

- `server.log`, `server.pid`: static preview.
- `tunnel.log`, `tunnel.pid`: external tunnel and its URL.

Anyone with the URL can access the static app. The URL changes when the tunnel
restarts. Both processes and the host machine must remain running; Quick Tunnels
provide no uptime guarantee. No router port forwarding is required.

```sh
npm --prefix web run preview:stop
# Start again after a rebuild or to obtain a new tunnel URL:
npm --prefix web run preview:start
```

The uncommon local port is a convenience, not authentication. See
[Cloudflare's Quick Tunnel documentation](https://developers.cloudflare.com/tunnel/get-started/quick-tunnels/).

## Using the workspace

- **Examples**: RC low-pass, RLC resonator, bridge rectifier, diode clipper,
  common-emitter amplifier, Sallen–Key filter, CMOS inverter and a five-stage
  CMOS ring oscillator. Each opens as a local editable copy. Slider controls
  rerun the selected analysis after a short debounce.
- **Components**: click a library part to pick it up. Its symbol follows the
  mouse and previews the snapped position on the canvas; click to place one copy.
  You can also drag a library component onto the canvas. Dropping elsewhere or
  pressing **Esc** cancels placement. On touch screens, tap a library part and
  then the canvas. Existing placed components can be dragged to move them. The palette
  includes R/C/L, voltage/current sources, ground, diodes, NPN/PNP, NMOS/PMOS,
  and a finite-gain ideal VCVS op-amp. The latter has no rails, saturation or
  bandwidth model. CMOS gallery circuits use inline BSIM4 models.
- **Wires**: click a terminal, optionally click bends, then another terminal.
  Click an existing wire while wiring to create a branch junction. Double-click
  a wire in Select mode to insert a junction. Crossings alone do not connect.
  Selected wire bends can be dragged; the inspector can add a routing handle.
  All ground symbols connect to node `0`. Matching net labels connect globally.
- **Properties**: select a component to edit its values or model. Select a
  junction to set its net label or initial voltage. Moving or rotating a symbol
  preserves its electrical connections; deleting it removes attached wires.
- **Simulation**: the analysis panel selects DC operating point, AC frequency
  grid, or transient step/stop. Run settings control the browser analysis;
  textual analysis directives do not execute automatically. Parsing and
  convergence errors are shown without replacing them with example data.
- **Probes**: click a wire to plot its voltage, or hover over it to choose
  the compact voltage/current icon buttons (tooltips: **Plot voltage** /
  **Insert current probe**). On touch screens, tap a wire to show these choices. Voltage probes appear as labeled tags; current probes are
  inserted in the wire with an arrow showing positive current direction. Hover
  over a probe for its red trash icon at the top-right. Current probes also show
  a reverse-direction icon at the top-left. Tap a probe to reveal these controls
  on touch screens. Removing a current probe automatically rejoins the wire.
  Press and drag a voltage probe along its net, or a current probe along its
  measured wire; release to save the position, or press Esc to cancel. Moving
  markers preserves the measurement and supports undo. Existing probe types
  disappear from the wire's add menu; if both types are present, no menu appears.
  Voltages already plotted are greyed out in the Add signal list. Matching colors connect schematic
  markers to trace chips and waveforms; hovering a trace highlights its probe.
  The **Add signal** button offers a searchable list, including before a run.
  Trace chips have an **×** action to remove measurements. Existing voltage
  results display immediately; inserting/removing/reversing a current sensor
  reruns the analysis if results already exist. Undo, saving and project export
  retain sensors and their direction. Markers grow on small screens for legibility.
- **Results**: drag over a plot to zoom; use Reset
  plot zoom to restore it. AC magnitude is **dBV/dBA**, not an automatically
  calculated transfer gain. Phase is in degrees. CSV exports retain the full
  returned data, including real/imaginary AC values. Displayed solve time is
  engine time, excluding loading, parsing, and rendering.
- **Netlists**: the schematic's Netlist tab is generated and read-only. Edit a
  copy opens an independent SPICE document and preserves the original schematic.
  Imported `.cir`, `.ckt`, `.sp` and `.txt` files open in text mode. Models and
  subcircuits must be inline; external includes, `.step`, and `.control` blocks
  are unavailable. This release does not import arbitrary SPICE as a drawing.
- **Saving**: edits and themes are saved in browser local storage. Project menu
  exports/imports a version 1 `.neospice.json` project, including geometry,
  topology, models, analysis, initial voltages, and probes. It also downloads
  generated SPICE. Export projects to move between devices or origins: localhost,
  each tunnel hostname, and GitHub Pages have separate browser storage. Storage
  failures are reported; keep exported backups for work you want to retain.

Keyboard: **V** select, **W** wire, **R** rotate, **Delete** remove,
**Ctrl/Cmd+D** duplicate, **Ctrl/Cmd+Z** undo, **Ctrl/Cmd+Shift+Z** redo,
**Ctrl/Cmd+Enter** run, **Esc** cancel placement/wiring/selection. Scroll or use buttons to
zoom; Pan moves the canvas. Phone layouts expose Library/Circuit/Settings tabs.

## GitHub Pages

The WebAssembly workflow builds the app under `/<repository-name>/`, tests that
path, and uploads the `neospice-circuit-lab` artifact. A second job compares the
actual WASM gallery results against checksum-pinned **ngspice 47** using the
repository's native comparison functions.

To publish an update after pushing the changes:

1. Set the repository's **Settings → Pages → Source** to **GitHub Actions**.
2. Run **Actions → WebAssembly → Run workflow** on `main`, selecting **deploy**.
3. The deploy job waits for both browser and reference checks, then publishes
   the static artifact to the `github-pages` environment.

The project URL is **https://razorback16.github.io/neospice/**. The repository
uses GitHub Actions as its Pages source. Merely building or pushing does not
publish an update; run the workflow with **deploy** selected.
The [initial deployment](https://github.com/razorback16/neospice/actions/runs/37537274689)
passed the browser and ngspice 47 reference jobs before publishing.
A custom domain/root deployment needs `BASE_PATH=/` in the workflow.

All runtime assets use the configured base, including the worker, wrapper and
WASM binary. Hash navigation preserves refreshable example links without server
rewrite rules. The existing single-threaded WASM build needs no cross-origin
isolation headers. No CDN libraries, analytics or remote fonts are required.

## Validation

```sh
npm --prefix web test
npm --prefix web run test:gallery
npm --prefix web run test:probes
# Start the production preview separately before browser tests.
npm --prefix web exec playwright install chromium
npm --prefix web run test:browser
```

`CHROME_BINARY` can select an installed browser. `LAB_URL` can select a different
origin/base path, for example `http://127.0.0.1:48174/neospice/`.

For numerical reference checks, configure a native build against the pinned
ngspice 47 shared library as in [building](building.md), then:

```sh
cmake --build build --target neospice_gallery_reference -j4
GALLERY_REFERENCE=../build/tests/neospice_gallery_reference \
  npm --prefix web run test:gallery -- --reference
GALLERY_REFERENCE=../build/tests/neospice_gallery_reference \
  npm --prefix web run test:probes -- --reference
```

The comparison executable reads the WASM result arrays and applies existing
native comparators to the **same generated netlists** run by ngspice 47. There
is no second hand-maintained netlist copy or alternate reference version. RC,
RLC, CMOS edge and oscillator checks retain the existing comparison contracts.
New analog checks use fixed tolerances defined in `gallery_reference.cpp`;
failures retain per-signal error diagnostics. All eight default gallery analyses
passed locally. The 32 slider endpoints were additionally checked for
convergence; they are not separate ngspice parity claims.

The model tests cover connectivity, crossing/junction behavior, SPICE suffixes,
pin order, startup conditions, imports, sensor insertion/removal/reversal,
voltage-probe attachment and validation. Browser acceptance tests cover
examples, analyses, project editing and persistence, exports, errors, cancellation,
timeouts, missing WASM assets, themes, mobile layout, touch wiring, probes,
component pickup, drag-and-drop, grid snapping and canceled placement.
All 17 acceptance cases were also verified at the `/neospice/` path.
All 17 were exercised on the public GitHub Pages URL as well; one initial
asset request hit Chromium's `ERR_NETWORK_CHANGED`, and that case passed on
retry without changes.
The current-probe numerical check compares both reference directions in DC, AC
and transient analyses with ngspice 47 using the existing RC fixture tolerances;
it also checks current against Ohm's law and exact netlist restoration on removal.
The public preview was also exercised through its HTTPS tunnel. Initial validation uses
headless Chromium; Safari and Firefox are not yet verified.

## Implementation

`web/src/model.ts` defines `CircuitDocument` version 1. Connectivity uses explicit
terminal/junction identities, never coordinate intersection. The deterministic
serializer emits model definitions and node initial conditions along with SPICE
components. A document revision identifies stale plots independently of its
saved identity. Probe-only display changes do not invalidate simulation results.

A wire's optional `currentProbe` metadata separates its endpoint nets during
connectivity analysis. The serializer reconnects them with a zero-volt voltage
source (`V_PROBE…`), whose measured branch current supplies the trace. This is
ngspice's documented ideal ammeter method; the sensor adds no voltage drop ([ngspice source documentation](https://nmg.gitlab.io/ngspice-manual/voltageandcurrentsources/independentsourcesforvoltageorcurrent.html)).
The marker arrow follows the source's positive-to-negative terminal order.
A sensor bypassed by another wire or matching net labels is rejected rather than
creating a singular ideal-source loop. Voltage-probe anchors follow terminal
identities when sensor insertion or net renaming changes generated node names.
Optional metadata is validated on import; existing version 1 files still load.

`web/public/simulation-worker.mjs` loads the existing wrapper and owns native
circuit lifetimes. Every run disposes its circuit in a `finally` block. Messages
carry request IDs and document revisions. Only the latest pending slider request
is retained. Cancel and the 30-second watchdog terminate and replace the worker;
obsolete results cannot update the active document. The C++/WASM public API and
minimal WASM smoke demo remain available independently of the application.
