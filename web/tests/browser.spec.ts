import { test, expect, type Page } from "@playwright/test";
const errors: string[] = [];
test.beforeEach(async ({ page }) => {
  errors.length = 0;
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("./");
  await expect(
    page.getByRole("button", { name: "Run simulation", exact: true }),
  ).toBeEnabled();
  await expect(page.locator(".results-footer")).toContainText("samples");
});
test.afterEach(() => expect(errors).toEqual([]));
async function run(page: Page) {
  await page
    .getByRole("button", { name: "Run simulation", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Run simulation", exact: true }),
  ).toBeEnabled();
  await expect(page.getByRole("alert")).toHaveCount(0);
}
async function canvasPoint(page: Page, x: number, y: number) {
  return page.locator(".schematic").evaluate(
    (el, p) => {
      const point = new DOMPoint(p.x, p.y).matrixTransform(
        (el as SVGSVGElement).getScreenCTM()!,
      );
      return { x: point.x, y: point.y };
    },
    { x, y },
  );
}
async function place(page: Page, kind: string, x: number, y: number) {
  await page.getByTestId(`part-${kind}`).click();
  const p = await canvasPoint(page, x, y);
  await page.mouse.click(p.x, p.y);
}
async function pin(page: Page, ref: string) {
  await page.locator(`[data-pin="${ref}"] .pin-hit`).click({ force: true });
}
async function connect(page: Page, a: string, b: string) {
  await pin(page, a);
  await pin(page, b);
}
async function project(page: Page) {
  await page.waitForTimeout(350);
  return page.evaluate(() => {
    const s = JSON.parse(localStorage.getItem("neospice-lab-v1")!);
    return s.projects[s.current];
  });
}
test("all gallery examples run, plots render, both themes work", async ({
  page,
}) => {
  for (const id of [
    "rc-filter",
    "rlc-resonator",
    "bridge-rectifier",
    "diode-clipper",
    "bjt-amplifier",
    "active-filter",
    "cmos-inverter",
    "ring-oscillator",
    "comparator-oscillator",
  ]) {
    await page.getByTestId(`example-${id}`).click();
    await run(page);
    await expect(page.locator(".uplot canvas")).toBeVisible();
    const legend = await page.locator(".u-legend").boundingBox();
    const footer = await page.locator(".results-footer").boundingBox();
    expect(legend!.y + legend!.height).toBeLessThanOrEqual(footer!.y + 1);
    await expect(page.locator(".badge.warning")).toHaveCount(0);
  }
  await page.getByRole("button", { name: "Switch to dark theme" }).click();
  await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
  await page.screenshot({ path: "test-results/dark-desktop.png" });
  await page.reload();
  await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
});
test("comparator oscillator exposes editable response and three distinct signals", async ({
  page,
}) => {
  await page.getByTestId("example-comparator-oscillator").click();
  await run(page);
  const d = await project(page);
  expect(d.probes).toEqual(["v(out)", "v(cap)", "v(threshold)"]);
  expect(new Set(Object.values(d.probeColors)).size).toBe(3);
  await page
    .getByRole("button", { name: "X1 Comparator", exact: true })
    .press("Enter");
  await expect(page.getByLabel("Output response", { exact: true })).toHaveValue(
    "5n",
  );
  await page.getByLabel("Output response", { exact: true }).fill("10n");
  await run(page);
  await expect(page.locator(".uplot canvas")).toBeVisible();
  await page.reload();
  await page
    .getByRole("button", { name: "X1 Comparator", exact: true })
    .press("Enter");
  await expect(page.getByLabel("Output response", { exact: true })).toHaveValue(
    "10n",
  );
});
test("DC, AC, transient settings and slider auto-run", async ({ page }) => {
  await page.getByRole("button", { name: "DC point", exact: true }).click();
  await run(page);
  await expect(page.locator(".dc-table")).toContainText("909.091 mV");
  await page
    .getByRole("slider", { name: "Resistance", exact: true })
    .fill("1000");
  await expect(page.locator(".dc-table")).toContainText("500 mV");
  await page.getByRole("button", { name: "AC sweep", exact: true }).click();
  await run(page);
  await expect(
    page.getByRole("button", { name: "Phase", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Phase", exact: true }).click();
  await page.getByRole("button", { name: "Transient", exact: true }).click();
  await run(page);
  await expect(page.locator(".results-footer")).toContainText("Time domain");
  const download = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export results as CSV" }).click();
  expect((await download).suggestedFilename()).toBe("simulation.csv");
});
test("build a divider, wire it, simulate, move, rotate, undo, and save", async ({
  page,
}) => {
  await page.getByRole("button", { name: "New circuit" }).click();
  await page.getByLabel("Circuit title").fill("Browser divider");
  await place(page, "V", 160, 240);
  await place(page, "R", 400, 160);
  await place(page, "R", 640, 240);
  await page.keyboard.press("r");
  await place(page, "G", 160, 400);
  await place(page, "G", 640, 400);
  await connect(page, "V1.p", "R1.a");
  await connect(page, "R1.b", "R2.a");
  await connect(page, "R2.b", "G2.g");
  await connect(page, "V1.n", "G1.g");
  await page.keyboard.press("Escape");
  await page.getByRole("button", { name: "DC point", exact: true }).click();
  await run(page);
  await expect(page.locator(".dc-table")).toContainText("2.5 V");
  const before = await project(page);
  expect(before.components).toHaveLength(5);
  expect(before.wires).toHaveLength(4);
  await page
    .locator('[data-component="R1"] .component-hit')
    .click({ force: true });
  await page.keyboard.press("Control+d");
  expect((await project(page)).components).toHaveLength(6);
  await page.keyboard.press("Delete");
  expect((await project(page)).components).toHaveLength(5);
  await page.getByRole("button", { name: "Undo", exact: true }).click();
  expect((await project(page)).components).toHaveLength(6);
  await page.getByRole("button", { name: "Undo", exact: true }).click();
  await page.reload();
  await expect(page.getByLabel("Circuit title")).toHaveValue("Browser divider");
  await expect(page.locator(".dc-table")).toContainText("2.5 V");
});
test("netlist copies preserve schematic and surface parsing errors", async ({
  page,
}) => {
  const schematic = await project(page);
  await page.getByRole("button", { name: "Netlist", exact: true }).click();
  await expect(page.getByLabel("SPICE netlist")).toHaveAttribute(
    "readonly",
    "",
  );
  await page.getByRole("button", { name: "Edit a copy" }).click();
  await page
    .getByLabel("SPICE netlist")
    .fill("Bad include\n.include missing.lib\n.end");
  await page
    .getByRole("button", { name: "Run simulation", exact: true })
    .click();
  await expect(page.getByRole("alert")).toContainText("Inline model");
  await page
    .getByLabel("SPICE netlist")
    .fill("Divider\nV1 in 0 10\nR1 in out 1k\nR2 out 0 1k\n.end");
  await page.getByRole("button", { name: "DC point", exact: true }).click();
  await run(page);
  await expect(page.locator(".dc-table")).toContainText("5 V");
  const saved = await page.evaluate(
    (id) => JSON.parse(localStorage.getItem("neospice-lab-v1")!).projects[id],
    schematic.id,
  );
  expect(saved.mode).toBe("schematic");
  expect(saved.components.length).toBe(schematic.components.length);
});
test("project export/import round-trip and wire junctions", async ({
  page,
}) => {
  const w = page.locator("[data-wire]").first();
  await w.dblclick({ force: true });
  const document = await project(page);
  expect(document.junctions.length).toBe(3);
  await page.getByLabel("Net label", { exact: true }).fill("in");
  await page.getByRole("button", { name: "Project menu" }).click();
  const promise = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export project", exact: true })
    .click();
  const downloaded = await promise;
  const path = await downloaded.path();
  expect(path).toBeTruthy();
  await page.getByRole("button", { name: "New circuit" }).click();
  await page.locator("input[type=file]").setInputFiles({
    name: "roundtrip.neospice.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(document)),
  });
  await expect(page.getByLabel("Circuit title")).toHaveValue(
    "RC low-pass filter",
  );
  await run(page);
});
test("phone layout, touch-style placement, themes and no horizontal overflow", async ({
  page,
}) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await expect(page.locator(".mobile-nav")).toBeVisible();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBeTruthy();
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await expect(
    page.getByRole("slider", { name: "Resistance", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Circuit", exact: true }).click();
  await page.getByRole("button", { name: "Switch to dark theme" }).click();
  await page.screenshot({ path: "test-results/dark-phone.png" });
  await page.getByRole("button", { name: "Library", exact: true }).click();
  await page.getByTestId("example-cmos-inverter").click();
  await run(page);
  await expect(page.locator(".schematic")).toBeVisible();
});
test("worker errors recover and long simulations can be canceled", async ({
  page,
}) => {
  await page.getByRole("button", { name: "Netlist", exact: true }).click();
  await page.getByRole("button", { name: "Edit a copy" }).click();
  await page
    .getByLabel("SPICE netlist")
    .fill("Conflicting sources\nV1 out 0 1\nV2 out 0 2\nR1 out 0 1k\n.end");
  await page.getByRole("button", { name: "DC point", exact: true }).click();
  await page
    .getByRole("button", { name: "Run simulation", exact: true })
    .click();
  await expect(page.getByRole("alert")).toContainText("converge");
  await page.getByTestId("example-ring-oscillator").click();
  await page.getByLabel("Time step", { exact: true }).fill("1p");
  await page.getByLabel("Stop time", { exact: true }).fill("1u");
  await page
    .getByRole("button", { name: "Run simulation", exact: true })
    .click();
  await page.getByRole("button", { name: "Cancel", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Run simulation", exact: true }),
  ).toBeEnabled();
  await page.getByTestId("example-rc-filter").click();
  await run(page);
});
test("missing WASM assets show a recoverable loading error", async ({
  page,
}) => {
  await page.route("**/simulator/neospice.wasm", (route) => route.abort());
  await page.reload();
  await expect(page.getByRole("alert")).toContainText(
    /load|fetch|WebAssembly/i,
  );
  await page.unroute("**/simulator/neospice.wasm");
  await page
    .getByRole("button", { name: "Run simulation", exact: true })
    .click();
  await expect(page.locator(".results-footer")).toContainText("samples");
  await expect(page.getByRole("alert")).toHaveCount(0);
});
test("a stalled worker times out and can be retried", async ({ page }) => {
  await page.route("**/simulation-worker.mjs", (route) =>
    route.fulfill({
      contentType: "text/javascript",
      body: 'self.postMessage({type:"ready"}); self.onmessage=()=>{};',
    }),
  );
  await page.clock.install();
  await page.reload();
  await expect(
    page.getByRole("button", { name: "Cancel", exact: true }),
  ).toBeVisible();
  await page.clock.fastForward(31000);
  await expect(page.getByRole("alert")).toContainText("30 seconds");
  await page.unroute("**/simulation-worker.mjs");
  await page.reload();
  await expect(page.locator(".results-footer")).toContainText("samples");
});

test.describe("touch editor", () => {
  test.use({ hasTouch: true, viewport: { width: 390, height: 844 } });
  test("place and wire a circuit with taps", async ({ page }) => {
    await page.getByRole("button", { name: "Library", exact: true }).tap();
    await page.getByRole("button", { name: "New circuit" }).tap();
    for (const [kind, x, y] of [
      ["V", 160, 240],
      ["R", 400, 240],
      ["G", 160, 400],
      ["G", 400, 400],
    ] as const) {
      await page.getByRole("button", { name: "Library", exact: true }).tap();
      await page.getByTestId(`part-${kind}`).tap();
      const p = await canvasPoint(page, x, y);
      await page.touchscreen.tap(p.x, p.y);
    }
    for (const [a, b] of [
      ["V1.p", "R1.a"],
      ["V1.n", "G1.g"],
      ["R1.b", "G2.g"],
    ]) {
      await page.locator(`[data-pin="${a}"] .pin-hit`).tap({ force: true });
      await page.locator(`[data-pin="${b}"] .pin-hit`).tap({ force: true });
    }
    await page.getByRole("button", { name: "Settings", exact: true }).tap();
    await page.getByRole("button", { name: "Back to circuit settings" }).tap();
    await page.getByRole("button", { name: "DC point", exact: true }).tap();
    await page.getByRole("button", { name: "Circuit", exact: true }).tap();
    await run(page);
    await expect(page.locator(".dc-table")).toContainText("5 V");
  });
});

test("hover probes insert a real current sensor, reverse, persist, undo, and reconnect", async ({
  page,
}) => {
  const position = await canvasPoint(page, 220, 160);
  await page.mouse.move(position.x, position.y);
  await expect(
    page.getByRole("toolbar", { name: "Wire probes" }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Insert current", exact: false })
    .click();
  const marker = page.locator('[data-probe="i(v_probe1)"]');
  await expect(marker).toBeVisible();
  await expect(page.locator(".u-legend")).toContainText("I1");
  await expect(page.locator(".badge.warning")).toHaveCount(0);
  const added = await project(page);
  expect(added.wires.filter((w: any) => w.currentProbe)).toHaveLength(1);
  await page.getByRole("button", { name: "DC point", exact: true }).click();
  await run(page);
  await expect(
    page.locator(".dc-table tr").filter({ hasText: "I1" }),
  ).toContainText("90.9091 µA");
  await marker.locator(".sensor-body").hover();
  await page.getByRole("button", { name: "Reverse I1", exact: true }).click();
  await expect(
    page.locator(".dc-table tr").filter({ hasText: "I1" }),
  ).toContainText("-90.9091 µA");
  await page.reload();
  await expect(marker).toBeVisible();
  expect(
    (await project(page)).wires.find((w: any) => w.currentProbe).currentProbe
      .reversed,
  ).toBe(true);
  await page.getByRole("button", { name: "Remove I1", exact: true }).click();
  await expect(marker).toHaveCount(0);
  expect((await project(page)).wires.some((w: any) => w.currentProbe)).toBe(
    false,
  );
  await page.keyboard.press("Control+z");
  await expect(marker).toBeVisible();
  await page.keyboard.press("Control+Shift+z");
  await expect(marker).toHaveCount(0);
});
test("voltage markers and Add signal share colors and can be removed without rerunning", async ({
  page,
}) => {
  await expect(
    page.getByRole("button", { name: "Add signal", exact: true }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Remove V(out)", exact: true })
    .click();
  await expect(page.locator('[data-probe="v(out)"]')).toHaveCount(0);
  await page.getByRole("button", { name: "Add signal", exact: true }).click();
  await page.getByRole("searchbox", { name: "Find a signal" }).fill("out");
  await page
    .getByRole("checkbox", { name: "V(out) Voltage", exact: true })
    .check();
  await page.keyboard.press("Escape");
  await expect(page.locator('[data-probe="v(out)"]')).toBeVisible();
  await page.getByRole("button", { name: "Add signal", exact: true }).click();
  await expect(
    page.getByRole("checkbox", { name: "V(out) Voltage", exact: true }),
  ).toBeDisabled();
  await page.keyboard.press("Escape");
  await expect(page.locator(".badge.warning")).toHaveCount(0);
  const color = await page
    .locator('[data-probe="v(out)"]')
    .evaluate((e) => getComputedStyle(e).color);
  expect(
    await page
      .locator('[data-trace="v(out)"]')
      .evaluate((e) => getComputedStyle(e).color),
  ).toBe(color);
  await page.locator('[data-trace="v(out)"] .trace-name').hover();
  await expect(page.locator('[data-probe="v(out)"]')).toHaveClass(
    /highlighted/,
  );
  await page.getByRole("button", { name: "New circuit", exact: false }).click();
  await expect(
    page.getByRole("button", { name: "Add signal", exact: true }),
  ).toBeVisible();
});

test.describe("touch probes", () => {
  test.use({
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
  });
  test("tap reveals probe actions, sensor renders and removal reconnects", async ({
    page,
  }) => {
    const position = await canvasPoint(page, 220, 160);
    await page.touchscreen.tap(position.x, position.y);
    await page
      .getByRole("button", { name: "Insert current", exact: false })
      .tap();
    await expect(page.locator('[data-probe="i(v_probe1)"]')).toBeVisible();
    await expect(page.locator(".u-legend")).toContainText("I1");
    const marker = page.locator('[data-probe="v(in)"] .probe-tag');
    expect((await marker.boundingBox())!.height).toBeGreaterThanOrEqual(20);
    expect(
      await page.evaluate(() => document.documentElement.scrollWidth),
    ).toBe(390);
    await page.locator('[data-probe="i(v_probe1)"] .sensor-body').tap();
    await page.getByRole("button", { name: "Delete I1", exact: true }).tap();
    await expect(page.locator('[data-probe="i(v_probe1)"]')).toHaveCount(0);
    expect((await project(page)).wires.some((w: any) => w.currentProbe)).toBe(
      false,
    );
    await page.screenshot({
      path: "test-results/probes-phone.png",
      animations: "disabled",
    });
  });
});

test("component pickup follows the pointer, places once, and Escape cancels", async ({
  page,
}) => {
  await page.getByRole("button", { name: "New circuit", exact: false }).click();
  await page.getByTestId("part-R").click();
  const preview = page.getByTestId("placement-preview");
  await expect(preview).toHaveAttribute("data-kind", "R");
  const position = await canvasPoint(page, 403, 247);
  await page.mouse.move(position.x, position.y);
  await expect(preview).toBeVisible();
  const expected = await canvasPoint(page, 400, 240),
    box = (await preview.boundingBox())!;
  expect(Math.abs(box.x + box.width / 2 - expected.x)).toBeLessThan(1);
  expect(Math.abs(box.y + box.height / 2 - expected.y)).toBeLessThan(1);
  await page.mouse.click(position.x, position.y);
  await expect(preview).toHaveCount(0);
  let doc = await project(page);
  expect(doc.components).toHaveLength(1);
  expect(doc.components[0]).toMatchObject({ kind: "R", x: 400, y: 240 });
  await page.getByTestId("part-C").click();
  await page.mouse.move(position.x + 70, position.y + 70);
  await expect(preview).toHaveAttribute("data-kind", "C");
  await page.keyboard.press("Escape");
  await expect(preview).toHaveCount(0);
  await page.mouse.click(position.x + 70, position.y + 70);
  expect((await project(page)).components).toHaveLength(1);
  // Placement above another part must not start dragging the existing part.
  await page.getByTestId("part-C").click();
  await page.mouse.click(expected.x, expected.y);
  doc = await project(page);
  expect(doc.components).toHaveLength(2);
  expect(doc.components.every((c: any) => c.x === 400 && c.y === 240)).toBe(
    true,
  );
});
test("native library drag places one component, outside drop and Escape cancel", async ({
  page,
}) => {
  await page.getByRole("button", { name: "New circuit", exact: false }).click();
  const startDrag = async (kind: string) => {
    const box = (await page.getByTestId(`part-${kind}`).boundingBox())!;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 15, box.y + box.height / 2, {
      steps: 3,
    });
  };
  const position = await canvasPoint(page, 400, 240);
  await startDrag("R");
  await page.mouse.move(position.x, position.y, { steps: 10 });
  await page.mouse.move(position.x + 1, position.y + 1);
  await expect(page.getByTestId("drop-preview")).toBeVisible();
  await page.mouse.up();
  await expect(page.getByTestId("drop-preview")).toHaveCount(0);
  expect((await project(page)).components).toMatchObject([
    { kind: "R", x: 400, y: 240 },
  ]);
  await page.keyboard.press("Control+z");
  expect((await project(page)).components).toHaveLength(0);
  await startDrag("C");
  await page.mouse.move(600, 35, { steps: 8 });
  await page.mouse.up();
  await expect(page.getByTestId("placement-preview")).toHaveCount(0);
  expect((await project(page)).components).toHaveLength(0);
  await startDrag("L");
  await page.mouse.move(position.x, position.y, { steps: 8 });
  await page.mouse.move(position.x + 1, position.y + 1);
  await page.keyboard.press("Escape");
  await page.mouse.up();
  await expect(page.getByTestId("drop-preview")).toHaveCount(0);
  await expect(page.getByTestId("placement-preview")).toHaveCount(0);
  expect((await project(page)).components).toHaveLength(0);
});

test("existing probes hide add options and expose corner actions on hover", async ({
  page,
}) => {
  const p = await canvasPoint(page, 220, 160);
  await page.mouse.move(p.x, p.y);
  await expect(
    page.getByRole("button", { name: "Plot voltage", exact: true }),
  ).toHaveCount(0);
  await page
    .getByRole("button", { name: "Insert current probe", exact: true })
    .click();
  await expect(page.locator(".u-legend")).toContainText("I1");
  const q = await canvasPoint(page, 260, 160);
  await page.mouse.move(650, 600);
  await page.mouse.move(q.x, q.y);
  await expect(page.getByRole("toolbar", { name: "Wire probes" })).toHaveCount(
    0,
  );
  await page.getByRole("button", { name: "Remove V(in)", exact: true }).click();
  await page.mouse.move(q.x, q.y);
  await expect(
    page.getByRole("button", { name: "Plot voltage", exact: true }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Insert current probe", exact: true }),
  ).toHaveCount(0);
  await page.getByRole("button", { name: "Plot voltage", exact: true }).click();
  await page.mouse.move(650, 600);
  await page.mouse.move(q.x, q.y);
  await expect(page.getByRole("toolbar", { name: "Wire probes" })).toHaveCount(
    0,
  );
  const marker = page.locator('[data-probe="i(v_probe1)"]');
  await marker.locator(".sensor-body").hover();
  const reverse = page.getByRole("button", { name: "Reverse I1", exact: true });
  const remove = page.getByRole("button", { name: "Delete I1", exact: true });
  await expect(reverse).toBeVisible();
  await expect(remove).toBeVisible();
  expect((await reverse.boundingBox())!.x).toBeLessThan(
    (await remove.boundingBox())!.x,
  );
  await remove.click();
  await expect(marker).toHaveCount(0);
  const voltage = page.locator('[data-probe="v(in)"]');
  await voltage.locator(".probe-tag").hover();
  await page.getByRole("button", { name: "Delete V(in)", exact: true }).click();
  await expect(voltage).toHaveCount(0);
  await page.mouse.move(p.x, p.y);
  await expect(
    page.getByRole("button", { name: "Plot voltage", exact: true }),
  ).toBeVisible();
});
test("holding probes drags them along wires, saves once, and Escape cancels", async ({
  page,
}) => {
  const tag = page.locator('[data-probe="v(out)"] .probe-tag');
  const before = (await tag.boundingBox())!;
  await page.mouse.move(
    before.x + before.width / 2,
    before.y + before.height / 2,
  );
  await page.mouse.down();
  await page.mouse.move(
    before.x + before.width / 2 - 70,
    before.y + before.height / 2,
    { steps: 6 },
  );
  expect((await tag.boundingBox())!.x).toBeLessThan(before.x - 50);
  await page.mouse.up();
  await expect(page.locator(".badge.warning")).toHaveCount(0);
  const after = await project(page);
  expect(after.probeAnchors["v(out)"]).toBeDefined();
  await page.keyboard.press("Control+z");
  expect(Math.abs((await tag.boundingBox())!.x - before.x)).toBeLessThan(1);
  const p = await canvasPoint(page, 220, 160);
  await page.mouse.move(p.x, p.y);
  await page
    .getByRole("button", { name: "Insert current probe", exact: true })
    .click();
  await expect(page.locator(".u-legend")).toContainText("I1");
  const sensor = page.locator('[data-probe="i(v_probe1)"] .sensor-body');
  const b = (await sensor.boundingBox())!;
  await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2);
  await page.mouse.down();
  await page.mouse.move(b.x + b.width / 2 + 35, b.y + b.height / 2 + 20, {
    steps: 5,
  });
  await page.mouse.up();
  expect((await sensor.boundingBox())!.x).toBeGreaterThan(b.x + 20);
  expect(Math.abs((await sensor.boundingBox())!.y - b.y)).toBeLessThan(1);
  await expect(page.locator(".badge.warning")).toHaveCount(0);
  const saved = await project(page);
  const next = (await sensor.boundingBox())!;
  await page.mouse.move(next.x + next.width / 2, next.y + next.height / 2);
  await page.mouse.down();
  await page.mouse.move(next.x - 35, next.y, { steps: 5 });
  await page.keyboard.press("Escape");
  await page.mouse.up();
  expect((await project(page)).wires).toEqual(saved.wires);
  await page.reload();
  expect((await project(page)).wires).toEqual(saved.wires);
});
