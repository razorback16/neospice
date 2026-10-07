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

Open **http://127.0.0.1:48173/**. `preview` serves only `web/dist` on loopback.
It does not expose the checkout, directory listings, or a development server.
WASM is served as `application/wasm`. End this foreground preview with Ctrl+C.

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

With `cloudflared` installed, end any foreground preview and run:

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
restarts. Both processes and the host machine must remain running. Quick Tunnels
provide no uptime guarantee. No router port forwarding is required.

```sh
npm --prefix web run preview:stop
# Start again after a rebuild or to obtain a new tunnel URL:
npm --prefix web run preview:start
```

The uncommon local port is a convenience, not authentication. See
[Cloudflare's Quick Tunnel documentation](https://developers.cloudflare.com/tunnel/get-started/quick-tunnels/).

## Using the workspace

- **Examples:** open one of nine circuits as an editable local copy. Sliders
  rerun the selected analysis after a short delay.
- **Components:** select a library part, then click or tap the canvas to place it.
  Dragging from the library also works. **Esc** cancels placement.
  Drag placed components to move them. The palette includes passives, sources,
  ground, diodes, bipolar/MOS transistors, an ideal VCVS op-amp and a simplified comparator.
  The op-amp has no supply rails, saturation or bandwidth model.
- **Wires:** click a terminal, optional bends, then another terminal.
  Click a wire while wiring to create a branch. Double-click a wire in Select
  mode to insert a junction. Crossings alone do not connect.
  Drag selected bends or add a routing handle in the inspector.
  Ground symbols share node `0`. Matching labels connect globally.
- **Properties:** select a component to edit values or models.
  Select a junction to edit its label or initial voltage.
  Moving or rotating a component preserves connections. Deleting it removes attached wires.
- **Simulation:** select DC, AC or transient settings in the analysis panel.
  These settings control execution. Textual directives do not run automatically.
  Errors appear directly, without substitute example results.
- **Results:** drag across a plot to zoom. **Reset plot zoom** restores it.
  AC magnitude uses dBV/dBA, and phase uses degrees. Magnitude is not transfer gain.
  CSV exports include all returned samples and complex AC values.
  Solve time excludes loading, parsing and rendering.
- **Netlists:** the schematic Netlist tab is read-only.
  **Edit a copy** creates an independent text document.
  Imported `.cir`, `.ckt`, `.sp` and `.txt` files open as text.
  Models and subcircuits must be inline. External includes, `.step` and `.control`
  blocks are unavailable. Arbitrary SPICE-to-schematic import is unavailable.
- **Saving:** browser storage retains edits and themes.
  Export/import version 1 `.neospice.json` projects through the Project menu.
  Projects retain topology, geometry, models, analysis, initial voltages and probes.
  The menu also exports generated SPICE. Export backups to preserve work or move
  between devices and origins. Each localhost, tunnel and Pages origin has separate storage.

To add measurements, click a wire or use its **Plot voltage** and
**Insert current probe** controls. Tap a wire to reveal them on touch screens.
**Add signal** provides a searchable list before or after simulation.
Probe colors match traces. Hovering a trace highlights its probe.

Hover over a probe, or tap it, to reveal removal and current-direction controls.
Drag a marker along its net or measured wire. **Esc** cancels the move.
Moving markers preserves measurements and supports undo.
Removing a current probe rejoins the wire.
Current sensor insertion, removal or reversal reruns existing results.
Undo, saving and export preserve sensors and their direction.
Trace chips also provide an **×** removal control.
Existing measurements disappear from the wire menu and disable their Add signal entry.

Keyboard: **V** select, **W** wire, **R** rotate, **Delete** remove,
**Ctrl/Cmd+D** duplicate, **Ctrl/Cmd+Z** undo, **Ctrl/Cmd+Shift+Z** redo,
**Ctrl/Cmd+Enter** run, **Esc** cancel. Scroll or use buttons to zoom.
Pan moves the canvas. Phone layouts expose Library/Circuit/Settings tabs.

## Comparator relaxation oscillator

The ninth example adapts the [Python notebook](../examples/rc_relaxation_oscillator/rc_relaxation_oscillator.ipynb).
It uses a 5 V supply, three 20 kΩ threshold/feedback resistors, a 6.8 kΩ timing
resistor and a 100 pF capacitor. Timing resistance and capacitance are adjustable.

The behavioral comparator uses a `tanh` transfer, 10 Ω output resistance and
an output capacitor. Its default response time is 5 ns.
This is a simplified comparator, not TI's TLV3201 model.
The default transient step is 0.25 ns over 10 µs.
Reference checks use the existing oscillator contract with ngspice 47.

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
publish an update. Run the workflow with **deploy** selected.
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

The reference executable compares actual WASM arrays with ngspice 47 using
the same generated netlists and native comparison functions.
RC, RLC, CMOS edge and oscillator checks retain their existing contracts.
Other analog tolerances are fixed in `tests/wasm/gallery_reference.cpp`.
Failures report per-signal errors.
Reference comparisons use the default gallery analyses. The 36 slider endpoints
receive convergence checks only, without ngspice comparisons.

Model tests check connectivity, serialization, initial conditions, imports and sensors.
Browser tests check editing, persistence, exports, error handling, cancellation,
asset loading, themes, touch interactions and probes.
Current-probe checks compare both directions in DC, AC and transient with
ngspice 47. They also check Ohm's law and netlist restoration after removal.
Headless Chromium is the tested browser. Safari and Firefox remain unverified.

## Implementation

`web/src/model.ts` defines `CircuitDocument` version 1.
Connectivity uses terminal and junction identities, never coordinate crossings.
The serializer includes models and node initial conditions.
Document revisions prevent stale plots. Display-only probe changes preserve results.

Current probes split endpoint nets and reconnect them with a zero-volt source.
Its branch current supplies the trace. The arrow follows positive-to-negative
terminal order. The sensor adds no voltage drop.
The serializer rejects sensors bypassed by wires or matching labels, preventing
singular ideal-source loops. Voltage anchors follow terminal identities through
sensor insertion and net renaming. Import checks metadata and accepts existing version 1 files.

`web/public/simulation-worker.mjs` owns circuit lifetimes and disposes them after each run.
Messages carry request IDs and document revisions. Only the latest pending slider
request remains queued. Cancellation and the 30-second watchdog replace the worker.
Obsolete results cannot update the active document.
The public WASM API and minimal demo remain available separately.
