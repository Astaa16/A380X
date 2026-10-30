// SPDX-License-Identifier: GPL-3.0-or-later
/** @jsx Kit.h */
import {Kit,Hub,cell} from 'xp-kit';
import {FctlPage} from '@sdv2/Pages/Fctl/FctlPage';
import {VideoPage} from '@sdv2/Pages/Video/VideoPage';
import {raw} from './live-data.mjs';
const bus=new Hub(()=>({sendSyncedEvent(){}}),false),fctl=cell(false),cruise=cell(false),video=cell(false);
export function mountV2(el){Kit.render(<FctlPage bus={bus} visible={fctl}/>,el);Kit.render(<VideoPage bus={bus} visible={video}/>,el);}
export function updateV2(frame){
  fctl.set(frame.page===11);cruise.set(frame.page===13);video.set(frame.page===15);
  const send=(key,v)=>{if(v!==undefined)bus.send(key,v,false,true);};
  const read=n=>raw(frame,'sim/cockpit2/'+n);
  send('lgciu1LeftGearCompressed',raw(frame,'sim/flightmodel/failures/onground_any')!==undefined?!!raw(frame,'sim/flightmodel/failures/onground_any'):undefined);
  for(let i=1;i<=4;i++) {const ff=raw(frame,'sim/cockpit2/engine/indicators/fuel_flow_kg_sec',i-1);send(`engineFuelFlow_${i}`,ff===undefined?undefined:ff*3600);}
  const alt=read('pressurization/indicators/cabin_altitude_ft'),diff=read('pressurization/indicators/pressure_diffential_psi'),vs=read('pressurization/indicators/cabin_vvi_fpm');
  if(alt!==undefined&&diff!==undefined&&vs!==undefined){send('pressCabinAltitude_1',alt);send('pressCabinDeltaPressure_1',diff);send('pressCabinVerticalSpeed_1',vs);send('cpcsBxDiscreteWord_1',0);}
}
