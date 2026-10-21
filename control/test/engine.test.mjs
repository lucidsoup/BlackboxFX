import { test } from "node:test";
import assert from "node:assert/strict";
import * as E from "../engine.js";
import * as P from "../protocol.js";

test("every shape stays within 0..1 and random shapes repeat per seed", () => {
  for (const s of E.SHAPES) {
    for (let i = 0; i < 400; i++) {
      const v = E.shapeAt(s, i * 0.037, 42);
      assert.ok(v >= 0 && v <= 1, `${s} gave ${v}`);
    }
  }
  assert.equal(E.shapeAt("random", 3.2, 7), E.shapeAt("random", 3.9, 7));
  assert.notEqual(E.shapeAt("random", 3.2, 7), E.shapeAt("random", 4.2, 7));
  // drift is continuous across cycle boundaries
  assert.ok(Math.abs(E.shapeAt("drift", 4.9999, 9) - E.shapeAt("drift", 5.0, 9)) < 1e-3);
});

test("lane values respect min/max and tempo sync", () => {
  const l = E.makeLane({ min: 30, max: 60, shape: "triangle", beats: 4 });
  assert.equal(E.laneHz(l, 120), 0.5);              // one cycle per 4 beats at 120
  for (let t = 0; t < 10; t += 0.01) {
    const v = E.laneValue(l, t, 120);
    assert.ok(v >= 30 && v <= 60);
  }
  assert.equal(E.laneValue(l, 1, 120), 60);         // triangle peak at half cycle
  const inv = E.makeLane({ min: 90, max: 10, shape: "ramp up", rateHz: 1 });
  assert.equal(E.laneValue(inv, 0, 120), 90);       // inverted range works
});

test("lanes target the right addresses", () => {
  const l = E.makeLane({ block: "dly", slot: 2 });
  assert.equal(E.laneAddr(l), 0x3c);                // DLY knob 3 = Mix
  assert.equal(E.laneAddr(E.makeLane({ block: "volume" })), 0x00);
  const m = E.modulate([l, E.makeLane({ block: "dly", slot: 2, enabled: false })], 0, 120);
  assert.equal(m.size, 1);
});

test("morph glides knobs and flips switches at the midpoint", () => {
  const a = E.defaultState(), b = E.clone(a);
  b.blocks.rev.knobs[0] = 100; b.blocks.rev.model = 4; b.volume = 30;
  a.blocks.rev.knobs[0] = 0;
  const q = E.morph(a, b, 0.25);
  assert.equal(q.blocks.rev.knobs[0], 25);
  assert.equal(q.blocks.rev.model, 0);
  assert.equal(E.morph(a, b, 0.75).blocks.rev.model, 4);
  assert.equal(E.morph(a, b, 0.5).volume, 50);
});

test("diffPackets sends only what changed, switches first", () => {
  const a = E.defaultState(), b = E.clone(a);
  assert.equal(E.diffPackets(a, b).length, 0);
  b.blocks.mod.model = 3; b.blocks.mod.knobs[1] = 77; b.routing = [4, 0, 1, 2, 3, 5];
  const d = E.diffPackets(a, b);
  assert.deepEqual(d.map((x) => x.key), ["routing", "m16", "k46"]);
  assert.equal(P.hex(d[2].pkt).slice(0, 47), P.hex(P.knob(0x2e, 77)).slice(0, 47));
});

test("send queue coalesces per key and caps packets per tick", async () => {
  const out = [];
  const q = new E.SendQueue(async (p) => out.push(p[14]), { perTick: 2 });
  for (let v = 0; v < 50; v++) q.set("k68", P.knob(0x44, v));   // a dragged slider
  q.set("k70", P.knob(0x46, 5));
  q.set("k72", P.knob(0x48, 6));
  assert.equal(q.size, 3);
  await q.tick();
  assert.equal(out.length, 2);
  await q.tick();
  assert.deepEqual(out, [49, 5, 6]);                // only the latest slider value went out
});
