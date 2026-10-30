// SPDX-License-Identifier: GPL-3.0-or-later
import React,{useEffect,useRef} from 'react';
import {createRoot} from 'react-dom/client';
import {flushSync} from 'react-dom';
import {EngPage} from '@sd/Pages/Engine/EngPage';
import {ApuPage} from '@sd/Pages/Apu/ApuPage';
import {BleedPage} from '@sd/Pages/Bleed/BleedPage';
import {CondPage} from '@sd/Pages/Cond/CondPage';
import {PressPage} from '@sd/Pages/Press/PressPage';
import {DoorPage} from '@sd/Pages/Doors/DoorPage';
import {ElecAcPage} from '@sd/Pages/ElecAc/ElecAcPage';
import {ElecDcPage} from '@sd/Pages/ElecDc/ElecDcPage';
import {FuelPage} from '@sd/Pages/FuelPage';
import {WheelPage} from '@sd/Pages/Wheel/WheelPage';
import {HydPage} from '@sd/Pages/Hyd/HydPage';
import {CbPage} from '@sd/Pages/CbPage';
import {StatusPage} from '@sd/Pages/Status/StatusPage';
import {NativeReact,applyLive,useLive,raw} from './live';
import {mountV2,updateV2} from './v2-native';
const pages=[EngPage,ApuPage,BleedPage,CondPage,PressPage,DoorPage,ElecAcPage,ElecDcPage,FuelPage,WheelPage,HydPage,null,CbPage,null,StatusPage, null];
const names=['ENGINE','APU','BLEED','COND','CAB PRESS','DOOR / OXY','ELEC AC','ELEC DC','FUEL','WHEEL','HYD','F/CTL','CB','CRUISE','STATUS','VIDEO'];
const report=(body)=>window.webkit?.messageHandlers.a380.postMessage(body);
window.addEventListener('error',e=>report({type:'a380x-error',text:e.message}));
window.addEventListener('unhandledrejection',e=>report({type:'a380x-error',text:String(e.reason)}));
function shown(v,dec=0){return v===undefined?'XX':v.toFixed(dec);}
// Native CRUISE uses the reviewed positions but only actual available sensors;
// the upstream view's default zero fuel-used / cabin-temperature placeholders
// must not become apparently healthy readings in the aircraft.
function CruiseLive({frame}){
 const read=n=>raw(frame,'sim/cockpit2/'+n);
 return <svg viewBox="0 0 768 1024" className="sd-page"><text x="6" y="29" className="F36 White TextUnderline">CRUISE</text>
  <text x="15" y="80" className="F29 White TextUnderline">FUEL</text>
  {[112,263,578,730].map((x,i)=>{const ff=raw(frame,'sim/cockpit2/engine/indicators/fuel_flow_kg_sec',i);return <React.Fragment key={i}><text x={x} y="127" className="F29 EndAlign Green">{shown(ff===undefined?undefined:ff*3600)}</text><text x={x} y="227" className="F29 EndAlign Amber">XX</text></React.Fragment>;})}
  <text x="383" y="99" className="F26 White MiddleAlign">FF</text><text x="383" y="131" className="F22 Cyan MiddleAlign">KG/H</text>
  <text x="383" y="199" className="F26 White MiddleAlign">FU</text><text x="383" y="234" className="F22 White MiddleAlign">TOTAL</text><text x="383" y="270" className="F29 Amber MiddleAlign">XX</text><text x="383" y="296" className="F22 Cyan MiddleAlign">KG</text>
  <text x="15" y="332" className="F29 White TextUnderline">AIR</text>
  <text x="470" y="355" className="F26 White MiddleAlign">LDG ELEVN</text><text x="653" y="359" className="F29 Amber EndAlign">XX</text><text x="658" y="359" className="F22 Cyan">FT</text>
  <text x="225" y="419" className="F26 White MiddleAlign">DELTA P</text><text x="344" y="419" className="F29 Green EndAlign">{shown(read('pressurization/indicators/pressure_diffential_psi'),1)}</text><text x="350" y="419" className="F22 Cyan">PSI</text>
  <text x="505" y="450" className="F26 White">CAB V/S</text><text x="650" y="483" className="F29 Green EndAlign">{shown(read('pressurization/indicators/cabin_vvi_fpm'))}</text><text x="655" y="483" className="F22 Cyan">FT/MIN</text>
  <text x="505" y="535" className="F26 White">CAB ALT</text><text x="650" y="569" className="F29 Green EndAlign">{shown(read('pressurization/indicators/cabin_altitude_ft'))}</text><text x="655" y="569" className="F22 Cyan">FT</text>
  <path d="M40 520L80 460H385V600H95L40 555Z M110 460V600 M110 530H385" className="White NoFill SW2"/>
  <text x="175" y="500" className="F25 Amber">XX TO XX</text><text x="175" y="575" className="F25 Amber">XX TO XX</text><text x="48" y="490" className="F22 Cyan">°C</text>
 </svg>;
}
function NativeSd(){
 const frame=useLive(),v2=useRef(null);useEffect(()=>{mountV2(v2.current);},[]);useEffect(()=>updateV2(frame),[frame]);
 const Current=pages[frame.page],special=[11,15].includes(frame.page);
 const sat=raw(frame,'sim/cockpit2/temperature/outside_air_temp_degc'),tat=raw(frame,'sim/cockpit2/temperature/outside_air_LE_temp_degc');
 const gw=raw(frame,'sim/flightmodel/weight/m_total'),fuel=raw(frame,'sim/flightmodel/weight/m_fuel_total');
 return <div id="native-sd" data-page={frame.page} style={{visibility:frame.powered?'visible':'hidden'}}>
   <div ref={v2} className="v2-pages" style={{display:special?'block':'none'}}/>
   {!special&&Current&&<svg viewBox="0 0 768 1024" className="sd-page"><Current/></svg>}
   {frame.page===13&&<CruiseLive frame={frame}/>}
   <svg viewBox="0 0 768 1024" className="sd-overlay"><rect x="0" y="820" width="768" height="204" fill="black"/>
    <path d="M0 864H768 M0 946H768 M254 864V946 M510 864V946" className="White SW2"/>
    <text x="15" y="848" className="F20 Amber">{frame.page===15?'':frame.page===14?'FULL FWS NOT AVAILABLE':'XX / AMBER: SYSTEM DATA INCOMPLETE'}</text>
    <text x="14" y="895" className="F23 White">TAT</text><text x="232" y="895" className="F25 EndAlign Green">{shown(tat)}°C</text>
    <text x="14" y="931" className="F23 White">SAT</text><text x="232" y="931" className="F25 EndAlign Green">{shown(sat)}°C</text>
    <text x="272" y="895" className="F23 White">FOB</text><text x="493" y="931" className="F25 EndAlign Green">{shown(fuel)}KG</text>
    <text x="527" y="895" className="F23 White">GW</text><text x="749" y="931" className="F25 EndAlign Green">{shown(gw)}KG</text>
    <text x="16" y="984" className="F20 Cyan">{names[frame.page]} · AIRCRAFT LIVE INPUTS</text>
   </svg>
 </div>;
}
const el=document.createElement('div');document.body.appendChild(el);const root=createRoot(el);flushSync(()=>root.render(<NativeSd/>));
window.__a380Sd={apply:applyLive};
document.fonts.ready.then(()=>report({type:'a380x-loaded',ok:true}));
