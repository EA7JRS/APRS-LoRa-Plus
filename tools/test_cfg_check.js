/* Copyright (C) 2026 Manuel - EA7JRS
 *
 * This file is part of APRS LoRa+.
 *
 * APRS LoRa+ is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
 * later version. See <https://www.gnu.org/licenses/>.
 */

// Tests of the WebUI configuration check (data_embed/script.js, between CFG_CHECK BEGIN and END).
//   node tools/test_cfg_check.js
"use strict";
const fs = require("fs");
const path = require("path");
const assert = require("assert");

const src = fs.readFileSync(path.join(__dirname, "..", "data_embed", "script.js"), "utf8");
const block = src.split("// ---- CFG_CHECK BEGIN ----")[1].split("// ---- CFG_CHECK END ----")[0];
const { cfgRules, cfgPathHops } = new Function(block + "\nreturn { cfgRules, cfgPathHops };")();

const CH = [433.625, 433.65, 433.675, 433.7, 433.725, 433.75, 433.775, 434.45, 434.475, 434.5, 434.525, 434.55, 434.575, 434.855];
const BANDS = { 1: [[430, 440]], 2: [[420, 450], [902, 928]], 3: [[430, 440]] };
const inBand = (r, hz) => (BANDS[r] || BANDS[1]).some((b) => hz / 1e6 >= b[0] && hz / 1e6 <= b[1]);

function good() {
    return {
        beacons: [{ i: 0, callsign: "EA7XXX-7" }, { i: 1, callsign: "EA7XXX-9" }],
        loras: [{ i: 0, ais: false, region: 1, freq: 433775000, sf: 12, bw: 125000, cr: 5 },
                { i: 4, ais: true, region: 1, freq: 433775000, sf: 9, bw: 125000, cr: 5 }],
        path: "WIDE1-1",
        wifiApPassword: "k3Yp-rT9xQ2m",
        bt: { active: true, ble: true, kiss: false, pin: "482913" },
        kissNet: { enabled: true, allowed: "192.168.1.", port: "8001" }
    };
}
const run = (v) => cfgRules(v, CH, inBand);
const ids = (v) => run(v).findings.map((f) => f.id + ":" + f.sev).sort();
let n = 0;
const test = (name, fn) => { fn(); n++; console.log("ok - " + name); };

test("clean configuration passes everything", () => {
    const r = run(good());
    assert.deepStrictEqual(r.findings, []);
    assert.strictEqual(r.passed.length, 6);
});
test("factory defaults: NOCALL and AP password are errors", () => {
    const v = good();
    v.beacons[0].callsign = "NOCALL-7";
    v.wifiApPassword = "1234567890";
    assert.deepStrictEqual(ids(v), ["ap:error", "nocall:error"]);
    v.beacons[1].callsign = "NOCALL-5";
    const f = run(v).findings.find((x) => x.id === "nocall");
    assert.strictEqual(f.where, "Beaconing · Beacons 1, 2");
});
test("invalid callsigns", () => {
    for (const c of ["EA7XXX-16", "EAXXX", "1234", "EA7-XXX", "EA7XXXXX-1"]) {
        const v = good(); v.beacons[1].callsign = c;
        assert.deepStrictEqual(ids(v), ["call1:error"], c);
    }
    for (const c of ["EA7XXX", "EA7XXX-15", "M0ABC-0", "4X1AB"]) {
        const v = good(); v.beacons[1].callsign = c;
        assert.deepStrictEqual(ids(v), [], c);
    }
});
test("path hops", () => {
    assert.deepStrictEqual(cfgPathHops(["WIDE1-1", "WIDE2-2"]), { hops: 3, worstN: 2 });
    const v = good();
    v.path = "WIDE1-1,WIDE2-2";
    const f = run(v).findings[0];
    assert.strictEqual(f.sev, "warn");
    assert.strictEqual(f.fix.value, "WIDE1-1");
    v.path = "WIDE1-1,WIDE2-1"; assert.deepStrictEqual(ids(v), []);
    v.path = "";                assert.deepStrictEqual(ids(v), []);
    v.path = "WIDE3-3";         assert.deepStrictEqual(ids(v), ["path:warn"]);
    v.path = "WIDE2-3";         assert.deepStrictEqual(ids(v), ["path:error"]);
    v.path = "WIDE1-1,TCP*";    assert.deepStrictEqual(ids(v), ["path:error"]);
});
test("LoRa frequency: band, channel and LoRa APRS settings", () => {
    const v = good();
    v.loras[0].freq = 433900000;
    assert.deepStrictEqual(ids(v), ["freq0:warn"]);
    v.loras[0].freq = 446000000;
    assert.deepStrictEqual(ids(v), ["freq0:warn"]);
    v.loras[0].freq = 434855000;
    assert.deepStrictEqual(ids(v), []);
    v.loras[0].freq = 433775000; v.loras[0].sf = 9;
    assert.deepStrictEqual(ids(v), ["std0:info"]);
    v.loras[0].sf = 12; v.loras[0].region = 2; v.loras[0].freq = 915000000;
    assert.deepStrictEqual(ids(v), []);                    // Region 2: only the allocation is checked
});
test("WiFi AP password length and open AP", () => {
    const v = good();
    v.wifiApPassword = "abc123";  assert.deepStrictEqual(ids(v), ["ap:error"]);
    v.wifiApPassword = "";        assert.deepStrictEqual(ids(v), ["ap:warn"]);
});
test("Bluetooth PIN and KISS links", () => {
    const v = good();
    v.bt.pin = "123456";
    v.bt.kiss = true;
    v.kissNet.allowed = " ";
    assert.deepStrictEqual(ids(v), ["kiss:warn", "kissopen:warn", "pin:warn"]);
    v.bt.active = false;
    assert.deepStrictEqual(ids(v), ["kissopen:warn"]);
});
console.log(n + " tests passed");
