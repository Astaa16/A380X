// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {test} from 'node:test';
import {pages,initialState,reduce,fixture,activeAlerts} from '../native/avionics/ecam-preview/model.mjs';
test('all 16 SD pages are reachable and selected page toggles AUTO',()=>{
  for(const p of pages){let s=initialState();s=reduce(s,{type:'page',page:p.index});if(p.index===0)s=reduce(s,{type:'page',page:0});assert.equal(s.page,p.index);assert.equal(s.manual,p.index);
    if(p.index!==14){s=reduce(s,{type:'page',page:p.index});assert.equal(s.manual,null);}}
});
test('held ALL cycles, wraps, freezes on release, ignores stray tick',()=>{
  let s=reduce(initialState(),{type:'all-start'});assert.equal(s.page,1);
  for(let i=0;i<15;i++)s=reduce(s,{type:'all-tick'});assert.equal(s.page,0);
  s=reduce(s,{type:'all-end'});assert.equal(s.all,false);s=reduce(s,{type:'all-tick'});assert.equal(s.page,0);
});
test('power and brightness independent; invalid input bounded',()=>{
  let s=reduce(initialState(),{type:'brightness',value:999});assert.equal(s.brightness,100);
  s=reduce(s,{type:'brightness',value:-5});assert.equal(s.brightness,0);
  s=reduce(s,{type:'power',value:false});assert.equal(s.power,false);assert.equal(s.page,0);
});
test('CLR / RCL / EMER CANC local alert lifecycle',()=>{
  let s=reduce(initialState(),{type:'fault',value:'hyd'});assert.equal(s.page,10);assert.deepEqual(activeAlerts(s),['hyd']);
  s=reduce(s,{type:'clr'});assert.deepEqual(activeAlerts(s),[]);
  s=reduce(s,{type:'rcl-last'});assert.deepEqual(activeAlerts(s),['hyd']);
  s=reduce(s,{type:'emercanc'});assert.deepEqual(activeAlerts(s),[]);
  s=reduce(s,{type:'rcl'});assert.deepEqual(activeAlerts(s),['hyd']);
});
test('checklist selection/check/uncheck, config test, MORE, and reset',()=>{
  let s=reduce(initialState(),{type:'cl'});assert.equal(s.checklist,true);
  s=reduce(s,{type:'down'});s=reduce(s,{type:'check'});assert.deepEqual(s.checked,[1]);
  s=reduce(s,{type:'check'});assert.deepEqual(s.checked,[]);
  for(let i=0;i<20;i++)s=reduce(s,{type:'down'});assert.equal(s.cursor,3);
  s=reduce(s,{type:'abnproc'});assert.equal(s.abnormal,true);
  s=reduce(s,{type:'toconfig'});assert.equal(s.config,'TEST CONFIG NORMAL');
  s=reduce(s,{type:'more'});assert.equal(s.more,true);
  assert.deepEqual(reduce(s,{type:'reset'}),initialState());
});
test('synthetic scenario/fault updates values without touching source state',()=>{
  let s=initialState(),copy=structuredClone(s);assert.ok(fixture('L:A32NX_ENGINE_N2:2',s)>60);
  let fault=reduce(s,{type:'fault',value:'oil'});assert.equal(fixture('GENERAL ENG OIL TEMPERATURE:2',fault),195);
  assert.notEqual(fixture('GENERAL ENG OIL TEMPERATURE:1',fault),195);
  assert.deepEqual(s,copy);
  fault=reduce(fault,{type:'scenario',value:'ground'});assert.equal(fixture('L:A32NX_ENGINE_N2:2',fault),0);
});
test('manual page persists scenario change; AUTO follows phase',()=>{
  let s=reduce(initialState(),{type:'page',page:0});assert.equal(s.manual,null);
  s=reduce(s,{type:'scenario',value:'cruise'});assert.equal(s.page,13);
  s=reduce(s,{type:'page',page:8});s=reduce(s,{type:'scenario',value:'ground'});assert.equal(s.page,8);
});
test('repeated STS cycles detail; invalid page and scenario cannot escape the model',()=>{
  let s=reduce(initialState(),{type:'page',page:14});
  s=reduce(s,{type:'page',page:14});assert.equal(s.more,true);
  s=reduce(s,{type:'page',page:14});assert.equal(s.more,false);
  for(const page of [-1,16,NaN,1.5,'0'])assert.deepEqual(reduce(s,{type:'page',page}),s);
  assert.deepEqual(reduce(s,{type:'scenario',value:'not-a-scenario'}),s);
});
