// Golden packets are the worked examples from the protocol notes.
import { test } from "node:test";
import assert from "node:assert/strict";
import * as P from "../protocol.js";

const bytes = (s) => Uint8Array.from(s.trim().split(/\s+/), (h) => parseInt(h, 16));

test("toggle FX on matches the documented packet", () => {
  assert.equal(P.hex(P.toggle(P.blockById("fx"), true)), "00 59 22 09 00 00 04 08 00 00 10 01 00 00 01 E1");
});

test("routing for the default chain has checksum D4", () => {
  assert.equal(P.hex(P.routing([0, 1, 2, 3, 4, 5])),
    "00 59 22 0E 00 00 04 02 00 00 10 06 00 00 00 01 02 03 04 05 D4");
  assert.throws(() => P.routing([0, 0, 2, 3, 4, 5]));
});

test("commit, state request and preset request match the notes", () => {
  assert.equal(P.hex(P.commitUi()), "00 59 22 08 00 00 04 00 00 00 F0 00 00 00 0B");
  assert.equal(P.hex(P.requestState()), "00 59 23 08 00 00 04 00 00 00 10 5C 00 00 8F");
  assert.equal(P.hex(P.requestPresetId()).slice(0, 41), "00 59 23 08 00 00 04 00 00 00 20 04 00 00");
});

test("knob and preset packets have the documented shape", () => {
  const k = P.knob(0x44, 73);
  assert.equal(k.length, 17);
  assert.equal(P.hex(k.slice(0, 16)), "00 59 22 0A 00 00 04 44 00 00 10 02 00 00 49 00");
  assert.equal(k[16], P.checksum(k));
  assert.deepEqual(Array.from(P.knob(0x44, 250).slice(14, 16)), [100, 0]);
  assert.deepEqual(Array.from(P.knob(0x44, -5).slice(14, 16)), [0, 0]);
  const lp = P.loadPreset(0x4f);
  assert.equal(P.hex(lp.slice(0, 15)), "00 59 22 09 00 00 04 4F 00 00 E0 01 00 00 01");
});

test("name request uses route 05 and a little-endian address", () => {
  const p = P.requestNames(P.AMP_NAMES_ADDR);
  assert.equal(P.hex(p.slice(0, 14)), "00 59 23 08 00 00 05 00 00 00 90 18 01 00");
  assert.equal(p[14], P.checksum(p));
});

test("SysEx packing reproduces the documented USB bytes and round-trips", () => {
  // The notes label this SysEx as the FX-on toggle, but it unpacks to a
  // neighbouring packet (route 05, sub-address 80 01) whose checksum is
  // valid. Packing that packet must give the documented bytes exactly.
  const docSysex = bytes("F0 00 32 09 49 00 00 40 02 09 00 00 00 18 00 00 00 01 5E 01 F7");
  const raw = P.unpackSysex(docSysex).slice(0, 16);
  assert.equal(P.hex(raw), "00 59 22 09 00 00 05 09 00 00 80 01 00 00 01 6F");
  assert.equal(raw[15], P.checksum(raw));
  assert.equal(P.hex(P.packSysex(raw)), P.hex(docSysex));
  for (let n = 1; n < 120; n++) {
    const r = Uint8Array.from({ length: n }, (_, i) => (i * 97 + n * 31) & 0xff);
    const packed = P.packSysex(r);
    assert.ok(packed.slice(1, -1).every((b) => b < 0x80), "7-bit safe");
    assert.deepEqual(P.unpackSysex(packed).slice(0, n), r);
  }
});

test("state packets parse and re-encode losslessly", () => {
  const s = {
    volume: 80, routing: [4, 0, 1, 2, 3, 5],
    blocks: Object.fromEntries(P.BLOCKS.map((b, i) => [b.id, {
      on: i % 2 === 0, model: i, knobs: [10 + i, 20, 30, 40, 50, 0] }])),
  };
  const pkt = P.encodeState(s);
  assert.equal(pkt.length, 107);
  const c = P.classify(pkt);
  assert.equal(c.kind, "state");
  assert.deepEqual(c.state, s);
  assert.equal(P.classify(bytes("00 59 00 01 00 00 00 FF")).kind, "ack");
});

test("names strip their numeric prefixes", () => {
  const enc = new TextEncoder();
  const buf = [...enc.encode("1AC-SeVin"), 0, ...enc.encode("14Recto_112"), 0];
  assert.deepEqual(P.parseNames(buf), ["AC-SeVin", "Recto_112"]);
});

test("there is no flash-write builder", () => {
  for (const [name, fn] of Object.entries(P)) {
    if (typeof fn !== "function") continue;
    assert.ok(!/save|flash|write/i.test(name), `unexpected export ${name}`);
  }
});
