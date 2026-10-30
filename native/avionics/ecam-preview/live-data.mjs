// SPDX-License-Identifier: GPL-3.0-or-later
// Aircraft-only mapping. Missing/invalid values stay unavailable. No fixtures.
export function raw(frame,name,index){const v=frame?.vars?.[name];const n=index===undefined?v:Array.isArray(v)?v[index]:undefined;return typeof n==='number'&&Number.isFinite(n)?n:undefined;}
const P='sim/cockpit2/';
export function liveValue(name,frame){
  const n=name.toUpperCase(),engine=Number(n.match(/:([1-4])$/)?.[1]||n.match(/ENG_([1-4])_/)?.[1]||1)-1;
  const array=(path,i=engine)=>raw(frame,P+path,i),scalar=path=>raw(frame,P+path);
  if(n.includes('ECAM_SD_PAGE_TO_SHOW'))return frame.page;
  if(n.includes('PREVIEW_MORE')||n.includes('SD_MORE'))return Number(frame.more);
  if(n.includes('ENGINE_N2:'))return array('engine/indicators/N2_percent');
  if(n.includes('ENGINE_N3:'))return raw(frame,'a380x/xp12/engine/n3_percent',engine);
  if(n.includes('ENGINE_OIL_QTY:'))return raw(frame,'a380x/xp12/engine/oil_quantity_quarts',engine);
  if(n.includes('ENGINE_FF:')){const v=array('engine/indicators/fuel_flow_kg_sec');return v===undefined?v:v*3600;}
  if(n.includes('ENGINE_STATE:'))return array('engine/indicators/engine_running');
  if(n.includes('OIL PRESSURE:'))return array('engine/indicators/oil_pressure_psi');
  if(n.includes('OIL TEMPERATURE:')){const v=array('engine/indicators/oil_temperature_deg_C'),c=raw(frame,'sim/aircraft/engine/acf_oilT_is_C');return v===undefined||c===undefined?undefined:c?v:(v-32)*5/9;}
  if(n.endsWith('APU_N'))return scalar('electrical/APU_N1_percent');
  if(n.includes('APU_N2'))return raw(frame,'a380x/xp12/apu/n2_percent');
  if(n.endsWith('APU_EGT'))return scalar('electrical/APU_EGT_c');
  if(n.includes('APU_FUEL_USED'))return raw(frame,'a380x/xp12/apu/fuel_used_kg');
  if(n.includes('APU_START_PB_IS_AVAILABLE')){const r=scalar('electrical/APU_running'),n1=scalar('electrical/APU_N1_percent');return r===undefined||n1===undefined?undefined:Number(!!r&&n1>=95);}
  if(n.includes('APU_MASTER_SW_PB_IS_ON')){const v=scalar('electrical/APU_starter_switch');return v===undefined?v:Number(v>0);}
  if(n.includes('APU_BLEED_AIR_VALVE_OPEN')||n.includes('PNEU_APU_BLEED_PB_IS_ON'))return scalar('bleedair/actuators/apu_bleed');
  if(n.includes('APU_BLEED_CONTAINER_PRESSURE')){const v=raw(frame,'a380x/xp12/apu/bleed_pressure_psi');return v===undefined?v:v+14.7;}
  if(n.includes('PNEU_ENG_')&&n.includes('PR_VALVE_OPEN'))return array('bleedair/actuators/engine_bleed_sov');
  if(n.includes('PACK_1')&&n.includes('FLOW_VALVE'))return scalar('bleedair/actuators/pack_left');
  if(n.includes('PACK_2')&&n.includes('FLOW_VALVE'))return scalar('bleedair/actuators/pack_right');
  if(n.includes('CABIN_ALTITUDE'))return scalar('pressurization/indicators/cabin_altitude_ft');
  if(n.includes('CABIN_VS')||n.includes('CABIN_VERTICAL_SPEED'))return scalar('pressurization/indicators/cabin_vvi_fpm');
  if(n.includes('CABIN_DELTA_PRESSURE')||n.includes('CABIN_DELTA_P'))return scalar('pressurization/indicators/pressure_diffential_psi');
  // X-Plane hydraulic pressure uses Plane Maker-defined units. Do not label it
  // PSI until an aircraft-specific producer establishes the conversion.
  if(n.includes('HYD_GREEN')||n.includes('HYD_YELLOW'))return undefined;
  const gen=n.match(/ELEC_ENG_GEN_([1-4])_POTENTIAL/);if(gen)return array('electrical/generator_volts',Number(gen[1])-1);
  const bat=n.match(/ELEC_BAT_([12])_(POTENTIAL|CURRENT)/);if(bat)return array('electrical/'+(bat[2]==='POTENTIAL'?'battery_voltage_actual_volts':'battery_amps'),Number(bat[1])-1);
  if(n.startsWith('GENERAL ENG MASTER ALTERNATOR:'))return array('electrical/generator_on');
  // No implicit healthy FWS, four independent PRIMs, A380 fuel/door layout,
  // or duplicated generic sensor values are fabricated here.
  return undefined;
}
