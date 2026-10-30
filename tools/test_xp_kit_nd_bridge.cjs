// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const root=path.resolve(__dirname,'..');
let now=100;
const messages=[],events=[],published=[];
const target={dispatchEvent(event){events.push(event);return true;}};
const context=vm.createContext({URL,URLSearchParams,Map,Number,Math,Promise,
  performance:{now:()=>now},location:{href:'file:///aircraft/xp-kit/nd.html?index=2&duID=4',search:'?index=2&duID=4'},
  __A380_NATIVE:true,__a380NdSource:'native ND bundle',addEventListener(){},
  MouseEvent:class {constructor(type,fields){Object.assign(this,{type},fields);}},
  document:{elementFromPoint:()=>target,querySelector:()=>({frameCount:1}),dispatchEvent:target.dispatchEvent},
  webkit:{messageHandlers:{a380:{postMessage:(data)=>messages.push(data)}}},
});
context.window=context;
for(const file of ['native/avionics/xp-kit/bridge.js','native/avionics/xp-kit-nd/native-bridge.js'])vm.runInContext(fs.readFileSync(path.join(root,file),'utf8'),context);
const cell=(value)=>({get:()=>value,set:(next)=>{value=next;}});
const list=(value)=>({items:()=>value,set:(next)=>{value=next;}});
const owner={bus:{send:(topic,data)=>published.push([topic,data])},cursorVisible:cell(false),mouseCursorRef:{instance:{updatePosition(x,y){owner.position=[x,y];}}},
  controlPanelVisible:cell(false),controlPanelRef:{instance:{activeDatabase:cell(''),navigraphAvailable:cell(false),store:{airports:list([]),selectedAirport:cell(null)},activeTabIndex:cell(2)}},
  oansRef:{instance:{dataLoading:false,dataAirportIcao:cell('UAAA'),isPanning:false,isPanningArmed:false}},efisNdMode:4};
const state={ok:true,mode:'native',values:{'sim/cockpit2/electrical/bus_volts':[28],
  'a380x/xp12/du/captain_nd':.8,'a380x/xp12/du/first_officer_nd':.6,
  'sim/time/total_flight_time_sec':123,'sim/time/paused':1,'a380x/xp12/adirs/first_officer_ir_source':2},
  nav:{positionValid:true,lat:43.35,lon:77.04,magVar:8,groundSpeed:280,tas:300,windDir:280,windSpeed:25,zulu:32000,
    controls:[{mode:3,range:3,powered:1},{mode:4,range:0,zoom:1,option:2,powered:1}],
    airport:'UAAA',airports:[{idarpt:'UAAA',name:'Almaty Intl'}],route:[{ident:'TEST',lat:44,lon:78,active:true,start:[43.35,77.04]}],
    symbols:[{ident:'VOR',type:2|256},{ident:'AIRPORT',type:16|256}],to:{ident:'TEST',lat:44,lon:78},
    pointers:[{inside:0,events:[]},{x:430,y:440,inside:1,events:[[1,0,430,440,0]]}]} };
for(let n=1;n<=3;n++)for(const [field,value] of Object.entries({nav_valid:1,attitude_valid:1,heading_valid:1,heading_deg:55,track_deg:58,latitude_deg:43.35,longitude_deg:77.04}))state.values[`a380x/xp12/adirs/ir${n}_${field}`]=value;
for(let n=1;n<=3;n++)state.values[`a380x/xp12/adirs/adr${n}_valid`]=1;
context.__a380xTrial.apply(state);
const host=context.__xphfbw;
assert.equal(host.readFile('/ND/bundle.js'),'native ND bundle');
assert.equal(host.screen,'SCREEN_DU_NDR');
assert.equal(host.getVar('LIGHT POTENTIOMETER:91'),.6);
assert.equal(host.getVar('L:A32NX_ATT_HDG_SWITCHING_KNOB'),2);
assert.equal(Math.floor(host.getVar('L:A32NX_EFIS_R_MRP_LAT')/2**32),3);
host.snapshot();assert.equal(host.getVar('E:SIMULATION RATE'),0);
assert.equal(host.getVar('E:ABSOLUTE TIME'),62135596800+32000);
assert.equal(host.magVar(),8);
context.__a380Nd.onUpdate(owner.bus,owner);
assert.equal(owner.cursorVisible.get(),false);assert.equal(owner.position,undefined,'Native cursor must not repaint the WebKit view');
assert.equal(events[0].type,'mousemove');
assert.equal(published.find(([topic])=>topic==='symbols')[1].length,2);
assert.equal(published.find(([topic])=>topic==='vectorsActive')[1].length,1);
assert.equal(context.__a380Nd.needsSnapshot(),true);
now+=50;state.nav.pointers[1].x=460;state.nav.pointers[1].events=[[2,0,460,440,0]];
context.__a380xTrial.apply(state);context.__a380Nd.onUpdate(owner.bus,owner);
assert.equal(context.__a380Nd.needsSnapshot(),false,'Pure cursor movement must reuse the cached ND');
state.nav.controls[1].zoom=2;context.__a380xTrial.apply(state);context.__a380Nd.onUpdate(owner.bus,owner);
assert.equal(context.__a380Nd.needsSnapshot(),true,'A zoom change must render immediately');
now+=50;assert.equal(context.__a380Nd.needsSnapshot(),false);
now+=500;assert.equal(context.__a380Nd.needsSnapshot(),true,'Safety refresh retains asynchronous and flashing cues');
now+=50;state.values['a380x/xp12/adirs/ir1_pitch_deg']=.01;context.__a380xTrial.apply(state);
assert.equal(context.__a380Nd.needsSnapshot(),false,'Unshown attitude sensor noise must not invalidate the ND');
context.__a380NdHover='optional-fix';assert.equal(context.__a380Nd.needsSnapshot(),true,'A changed waypoint hover box must update');
now+=50;state.values['a380x/xp12/adirs/ir3_heading_deg']=58;context.__a380xTrial.apply(state);
assert.equal(context.__a380Nd.needsSnapshot(),true,'A selected/standby gyro change remains visible immediately');
const before=events.length;context.__a380Nd.onUpdate(owner.bus,owner);assert.equal(events.length,before,'Events must not replay with every frame');
state.nav.pointers[1].events=[[3,1,430,440,0],[4,2,430,440,0],[5,5,430,440,0]];
context.__a380xTrial.apply(state);context.__a380Nd.onUpdate(owner.bus,owner);
assert.ok(events.some((e)=>e.type==='mousedown'));assert.ok(events.some((e)=>e.type==='mouseup'));assert.ok(events.some((e)=>e.type==='click'));
assert.equal(owner.controlPanelVisible.get(),true);assert.equal(owner.controlPanelRef.instance.activeTabIndex.get(),0);
now+=1600;host.snapshot();context.__a380Nd.onUpdate(owner.bus,owner);
assert.equal(context.__a380xTrial.state.ok,false);assert.equal(owner.cursorVisible.get(),false);assert.equal(host.getVar('CIRCUIT AVIONICS ON'),0);
(async()=>{
  const first=context.__a380Nd.airport('UAAA'),second=context.__a380Nd.airport('UAAA');
  assert.equal(first,second);const request=messages.find((m)=>m.type==='a380x-airport');assert.equal(request.icao,'UAAA');
  context.__a380Nd.airportResult(request.id,null,{source:'local apt.dat'});assert.equal((await first).source,'local apt.dat');
  await assert.rejects(context.__a380Nd.airport('../private'),/Invalid airport identifier/);
  console.log('Native ND bridge: side/source routing, ARINC map centre, live clock, power, filters, route, yellow-cursor inputs, ordered clicks, map panel, stale-feed cancellation and local scenery request boundary passed');
})().catch((e)=>{console.error(e);process.exitCode=1;});
