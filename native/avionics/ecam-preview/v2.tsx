// SPDX-License-Identifier: GPL-3.0-or-later
/** @jsx Kit.h */
import { Kit, Hub, cell } from 'xp-kit';
import { FctlPage } from '@sdv2/Pages/Fctl/FctlPage';
import { VideoPage } from '@sdv2/Pages/Video/VideoPage';
import { CruisePage } from '@sdv2/Pages/Cruise/CruisePage';
// No MSFS listener, socket, polling backplane or X-Plane connection.
const bus=new Hub(()=>({sendSyncedEvent(){}}),false);
const fctlVisible=cell(false),videoVisible=cell(false),cruiseVisible=cell(false);
export function mountV2(el){Kit.render(<FctlPage bus={bus} visible={fctlVisible}/>,el);Kit.render(<VideoPage bus={bus} visible={videoVisible}/>,el);Kit.render(<CruisePage bus={bus} visible={cruiseVisible}/>,el);}
export function updateV2(s){
  fctlVisible.set(s.page===11);videoVisible.set(s.page===15);cruiseVisible.set(s.page===13);
  const good=!(s.fault==='hyd');
  const signals={lgciu1LeftGearCompressed:s.scenario!=='cruise',prim1Healthy:true,prim2Healthy:true,prim3Healthy:true,sec1Healthy:true,sec2Healthy:true,sec3Healthy:true,
    hydGreenPressureSwitch:good,hydYellowPressureSwitch:true,acEhaBusPowered:true,acEssBusPowered:true,
    greenPressureSwitch:good,yellowPressureSwitch:true,acEhaPowered:true,acEssPowered:true,ac1Powered:true,
    sec1RudderStatusWord:2**27,sec3RudderStatusWord:2**27,sec1RudderTrimPosition:0,sec3RudderTrimPosition:0,
    slat1Healthy:true,slat2Healthy:true,flap1Healthy:true,flap2Healthy:true,
    cockpitCabinTemp:21,fwdCargoTemp:18,aftCargoTemp:18,condMainDeckTemp:22,condUpperDeckTemp:22,
    cabinAltitudeIsAuto:true,cabinVerticalSpeedIsAuto:true,pressCabinAltitude:s.scenario==='cruise'?6500:0,
    pressCabinDeltaPressure:s.scenario==='cruise'?8.1:0,pressCabinVerticalSpeed:0};
  for(let i=1;i<=4;i++){signals[`engineFuelFlow_${i}`]=s.scenario==='cruise'?2450:s.scenario==='idle'?650:0;signals[`engineFuelUsed_${i}`]=s.scenario==='cruise'?2100:0;}
  for(let i=1;i<=4;i++){
    signals[`cpcsBxDiscreteWord_${i}`]=0;
    signals[`pressCabinAltitude_${i}`]=s.scenario==='cruise'?6500:0;
    signals[`pressCabinDeltaPressure_${i}`]=s.scenario==='cruise'?8.1:0;
    signals[`pressCabinVerticalSpeed_${i}`]=0;
  }
  signals.fmLandingElevation_1=400;signals.fmLandingElevation_2=400;
  for(let i=1;i<=8;i++)signals[`condMainDeckTemp_${i}`]=22+(i%2);
  for(let i=1;i<=7;i++)signals[`condUpperDeckTemp_${i}`]=22+(i%2);
  for(const [k,v] of Object.entries(signals))bus.send(k,v,false,true);
}
