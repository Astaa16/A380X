// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const bridge = fs.readFileSync(path.join(__dirname, '../native/avionics/xp-kit/bridge.js'), 'utf8');
let now = 100;
const messages = [];
const context = vm.createContext({
  URL, URLSearchParams, performance: { now: () => now },
  location: { href: 'file:///aircraft/xp-kit/pfd.html?index=2&duID=3', search: '?index=2&duID=3' },
  __A380_NATIVE: true, __a380PfdSource: 'original PFD source',
  webkit: { messageHandlers: { a380: { postMessage: (value) => messages.push(value) } } },
  addEventListener() {},
});
context.window = context;
vm.runInContext(bridge, context);
const host = context.__xphfbw;
assert.equal(host.view, 2);
assert.equal(host.screen, 'SCREEN_DU_PFDR');
assert.equal(host.readFile('/PFD/bundle.js'), 'original PFD source');
host.loaded(true, 'ready');
assert.equal(messages[0].type, 'a380x-loaded');
assert.equal(messages[0].side, 2);
let called = 0;
const cancelled = context.requestAnimationFrame(() => { throw Error('Cancelled callback ran'); });
context.cancelAnimationFrame(cancelled);
context.requestAnimationFrame(() => { called++; context.requestAnimationFrame(() => called++); });
context.__a380NativeFrame();
assert.equal(called, 1);
context.__a380NativeFrame();
assert.equal(called, 2);
const state = { ok: true, mode: 'native', values: {
  'sim/cockpit2/electrical/bus_volts': [28],
  'a380x/xp12/du/captain_pfd': .7,
  'a380x/xp12/du/first_officer_pfd': .4,
  'sim/time/total_flight_time_sec': 1234,
  'sim/time/paused': 1,
  'sim/time/sim_speed_actual': 1,
}, words: { 'L:A32NX_PRIM_1_SELECTED_AIRSPEED': [251, true], 'L:INVALID': [99, false] },
numbers: { 'L:NATIVE_TEST': 17 } };
context.__a380xTrial.apply(state);
host.snapshot();
assert.equal(host.getVar('CIRCUIT AVIONICS ON'), 1);
assert.equal(host.getVar('LIGHT POTENTIOMETER:90'), .4);
assert.equal(host.getVar('L:NATIVE_TEST'), 17);
assert.equal(host.getVar('E:SIMULATION TIME'), 1234);
assert.equal(host.getVar('E:SIMULATION RATE'), 0);
assert.equal(Math.floor(host.getVar('L:A32NX_PRIM_1_SELECTED_AIRSPEED') / 2 ** 32), 3);
assert.equal(Math.floor(host.getVar('L:INVALID') / 2 ** 32), 0);
now += 1600;
host.snapshot();
assert.equal(context.__a380xTrial.state.ok, false);
assert.equal(host.getVar('CIRCUIT AVIONICS ON'), 0);
assert.equal(host.getVar('LIGHT POTENTIOMETER:90'), 0);
console.log('Native bridge: file-only source, side routing, host clock/cancellation, ARINC validity, FCU values, power, brightness, pause and stale-feed blanking passed');
