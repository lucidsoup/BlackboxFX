// End-to-end check in headless Chromium against the simulated pedal.
// Run: npm run test:e2e   (needs the playwright package and a Chromium)
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";
import assert from "node:assert/strict";
import { chromium } from "playwright";

const root = fileURLToPath(new URL("..", import.meta.url));
const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css" };
const server = createServer(async (req, res) => {
  const path = normalize(join(root, new URL(req.url, "http://x").pathname.replace(/\/$/, "/index.html")));
  if (!path.startsWith(root)) { res.writeHead(403).end(); return; }
  let body;
  try { body = await readFile(path); } catch { res.writeHead(404).end(); return; }
  res.writeHead(200, { "content-type": types[extname(path)] || "application/octet-stream" });
  res.end(body);
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const url = `http://127.0.0.1:${server.address().port}/`;

const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
const page = await browser.newPage();
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
page.on("console", (m) => m.type() === "error" && errors.push(m.text()));

const sim = () => page.evaluate(() => ({
  received: window.bfx.transport.received,
  state: window.bfx.transport.state,
  base: window.bfx.base,
  synced: window.bfx.synced,
}));
const knobWrites = (rx, addr) => rx.filter((h) => h.startsWith(`00 59 22 0A 00 00 04 ${addr}`));

try {
  await page.goto(url);
  // give the simulated pedal a sound that differs from the app's placeholder
  await page.evaluate(() => { window.bfx.base.blocks.rev.knobs[1] = 12; window.bfx.base.volume = 64; });
  await page.click("#btnSim");
  await page.waitForFunction(() => window.bfx.synced, null, { timeout: 8000 });

  let s = await sim();
  assert.equal(s.base.blocks.rev.knobs[1], 12, "adopted the pedal's state");
  await page.waitForTimeout(400);
  s = await sim();
  assert.equal(s.received.filter((h) => h.startsWith("00 59 22")).length, 0,
    "nothing is written to the pedal just by connecting");
  assert.equal(await page.locator("#chain .block").count(), 6);
  // names are optional and read once the state has arrived
  await page.waitForFunction(() => window.bfx.names.amp && window.bfx.names.cab, null, { timeout: 12000 });
  assert.ok((await page.locator("#chain .block select").nth(1).innerText()).includes("SimAmp-1"),
    "amp names read from the pedal");

  // hand edit: move the REV Mix slider
  const revMix = page.locator('.knob[data-addr="70"] input');
  await revMix.fill("90");
  await page.waitForTimeout(300);
  s = await sim();
  assert.equal(s.state.blocks.rev.knobs[1], 90, "slider reaches the pedal");

  // a movement lane on REV Mix
  await page.click("#addLane");
  await page.waitForTimeout(2500);
  s = await sim();
  const vals = knobWrites(s.received, "46").map((h) => parseInt(h.split(" ")[14], 16));
  assert.ok(new Set(vals).size >= 3, `lane produced several values (${vals.length} writes)`);
  assert.ok(vals.every((v) => v >= 0 && v <= 100));
  assert.equal(s.base.blocks.rev.knobs[1], 90, "lane does not overwrite the base value");

  // turning the lane off restores the hand-set value
  await page.click(".lane button:first-child");
  await page.waitForTimeout(300);
  s = await sim();
  assert.equal(s.state.blocks.rev.knobs[1], 90, "base restored after lane off");

  // reorder the chain
  await page.locator("#chain .block").nth(0).locator(".move button").nth(1).click();
  await page.waitForTimeout(300);
  s = await sim();
  assert.deepEqual(s.state.routing, [1, 0, 2, 3, 4, 5]);

  // scenes: store, change, recall
  await page.check("#storeMode");
  await page.locator("#scenes button").nth(0).click();
  await page.locator('#chain .block select').nth(4).selectOption("4"); // REV -> Shimmer
  await page.waitForTimeout(300);
  assert.equal((await sim()).state.blocks.rev.model, 4);
  await page.locator("#scenes button").nth(0).click();
  await page.waitForTimeout(300);
  assert.equal((await sim()).state.blocks.rev.model, 0, "scene recall restored the model");

  // morph between two captures
  await page.click("#captureA");
  await revMix.fill("10");
  await page.click("#captureB");
  await page.locator("#morph").fill("500");
  await page.waitForTimeout(300);
  assert.equal((await sim()).state.blocks.rev.knobs[1], 50, "morph midpoint");

  // preset change: nothing is pushed until the new preset's state is read
  const before = (await sim()).received.length;
  await page.click("#presetNext");
  await page.waitForTimeout(800);
  s = await sim();
  // routine polls or an in-flight morph write may precede the load; what
  // matters is that no knob write follows it before the new state is read
  const after = s.received.slice(before);
  const load = after.findIndex((h) => h.startsWith("00 59 22 09 00 00 04 01 00 00 E0 01"));
  assert.ok(load >= 0, `load preset P02 sent (got ${after.join(" | ")})`);
  assert.equal(after.slice(load + 1).filter((h) => h.startsWith("00 59 22 0A")).length, 0,
    "no stale knobs sent after the preset change");
  assert.ok(s.synced);

  // a pedal that never answers gets a clear message, not silence
  const silent = await browser.newPage();
  silent.on("pageerror", (e) => errors.push(e.message));
  await silent.goto(url + "?sim=silent");
  await silent.click("#btnSim");
  await silent.waitForFunction(() => document.getElementById("status").dataset.state === "err",
    null, { timeout: 9000 });
  assert.match(await silent.locator("#status").innerText(), /isn't answering/);
  assert.ok(await silent.locator("details.log").evaluate((d) => d.open), "activity log opened");
  const silentSim = await silent.evaluate(() => window.bfx.transport.received);
  assert.equal(silentSim.filter((h) => h.startsWith("00 59 22")).length, 0, "still nothing written");

  // Bluetooth: an empty picker explains itself and offers the unfiltered list
  const ble = await browser.newPage();
  ble.on("pageerror", (e) => errors.push(e.message));
  await ble.addInitScript(() => {
    window.__bleCalls = [];
    const fakeDevice = {
      name: "Some Speaker",
      addEventListener() {},
      gatt: {
        connected: true,
        disconnect() { this.connected = false; },
        async connect() {
          return { async getPrimaryService() { throw new DOMException("No Services matching UUID", "NotFoundError"); } };
        },
      },
    };
    Object.defineProperty(Navigator.prototype, "bluetooth", { configurable: true, get: () => ({
      async requestDevice(opts) {
        window.__bleCalls.push(opts);
        if (opts.acceptAllDevices) return fakeDevice;
        throw new DOMException("User cancelled the requestDevice() chooser.", "NotFoundError");
      },
    }) });
  });
  await ble.goto(url);
  assert.ok(await ble.locator("#btnBleAll").isHidden(), "show-all hidden at first");
  await ble.click("#btnBle");
  await ble.waitForSelector("#btnBleAll", { state: "visible" });
  assert.match(await ble.locator("#status").innerText(), /Location/);
  await ble.click("#btnBleAll");
  await ble.waitForFunction(() => /isn't a BlackBox/.test(document.getElementById("status").textContent));
  const calls = await ble.evaluate(() => window.__bleCalls);
  assert.ok(calls[0].filters.some((f) => f.namePrefix === "BlackBox"), "first try filters by name");
  assert.equal(calls[1].acceptAllDevices, true, "show-all lists everything");
  assert.ok(calls[1].optionalServices.length === 1, "and still asks for the control service");

  assert.deepEqual(errors, [], "no page errors");
  console.log(`ui e2e passed (${s.received.length} packets exchanged)`);
} finally {
  await browser.close();
  server.close();
}
