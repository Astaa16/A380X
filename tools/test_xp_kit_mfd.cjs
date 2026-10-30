// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.resolve(__dirname, '../native/avionics/xp-kit-mfd/draw.js'), 'utf8');
const sandbox = { Number, Array, Error };
vm.runInNewContext(source.replaceAll('export function ', 'function ') + ';this.api={drawFrame,validateFrame}', sandbox);
const { drawFrame, validateFrame } = sandbox.api;
const calls = [];
const ctx = new Proxy({}, { get: (_, key) => (...args) => calls.push([key, ...args]), set: (_, key, value) => { calls.push(['set', key, value]); return true; } });
const c = (kind, layer, payload = null) => [kind, 0x00ffff, 0, 0, 2, 27, 10, 20, 30, 40, payload, [0, 0, 768, 1024], layer, 1];
const frame = { sequence: 2, uri: 'fms/active/perf', powered: true,
  commands: [c(6, 99, 'CURSOR'), c(0, 0), c(1, 0), c(2, 0), c(3, 0, [0, 0, 10, 20]),
    c(4, 0, [0, 0, 10, 20, 30, 0]), c(5, 0, [0, 0, 10, 20, 30, 0]), c(6, 0, 'V1')] };
drawFrame(ctx, frame);
assert.equal(calls.filter((a) => a[0] === 'save').length, calls.filter((a) => a[0] === 'restore').length);
assert.deepEqual(calls.filter((a) => a[0] === 'fillText').map((a) => a[1]), ['V1', 'CURSOR']);
assert.equal(calls.filter((a) => a[0] === 'clip').length, 8);
assert(calls.some((a) => a[0] === 'strokeRect' && a[1] === 11 && a[3] === 28));
assert(calls.some((a) => a[0] === 'set' && a[1] === 'font' && a[2] === '27px A380Slashed'));
calls.length = 0; drawFrame(ctx, { ...frame, powered: false });
assert(!calls.some((a) => a[0] === 'fillText'));
assert.throws(() => validateFrame({ ...frame, commands: Array(4097).fill(c(0, 0)) }));
assert.throws(() => validateFrame({ ...frame, commands: [c(3, 0, [0, NaN])] }));
assert.throws(() => validateFrame({ ...frame, commands: [c(6, 0, {})] }));
console.log('MFD renderer: all seven primitives, stable layers, clipping, full-resolution fonts, power-off and malformed-frame rejection passed');
