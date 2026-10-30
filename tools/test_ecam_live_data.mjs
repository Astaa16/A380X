// SPDX-License-Identifier: GPL-3.0-or-later
import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {raw,liveValue} from '../native/avionics/ecam-preview/live-data.mjs';
test('absent inputs are unavailable, not live zeroes',()=>{const f={page:0,vars:{}};assert.equal(raw(f,'missing'),undefined);assert.equal(liveValue('L:A32NX_ENGINE_N2:1',f),undefined);assert.equal(liveValue('L:A32NX_FWS1_IS_HEALTHY',f),undefined);});
test('four engine channels and unit conversion stay independent',()=>{const f={vars:{'sim/cockpit2/engine/indicators/N2_percent':[61,62,null,64],'sim/cockpit2/engine/indicators/fuel_flow_kg_sec':[1,2,3,4],'sim/cockpit2/engine/indicators/oil_temperature_deg_C':[212,86],'sim/aircraft/engine/acf_oilT_is_C':0}};assert.equal(liveValue('L:A32NX_ENGINE_N2:2',f),62);assert.equal(liveValue('L:A32NX_ENGINE_N2:3',f),undefined);assert.equal(liveValue('L:A32NX_ENGINE_FF:4',f),14400);assert.equal(liveValue('GENERAL ENG OIL TEMPERATURE:1',f),100);});
test('native SD has no fixture imports or simulator write transport',()=>{const text=fs.readFileSync(new URL('../native/avionics/ecam-preview/native.tsx',import.meta.url),'utf8');assert.doesNotMatch(text,/fixtures|scenario|127\.0\.0\.1|fetch\(|WebSocket/);});
