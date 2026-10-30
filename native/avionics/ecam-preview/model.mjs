// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated review state. This module has no simulator or network transport.
export const pages = [
  ['ENG','ENGINE'], ['APU','APU'], ['BLEED','BLEED'], ['COND','COND'],
  ['PRESS','CAB PRESS'], ['DOOR','DOOR / OXY'], ['EL/AC','ELEC AC'],
  ['EL/DC','ELEC DC'], ['FUEL','FUEL'], ['WHEEL','WHEEL'], ['HYD','HYD'],
  ['F/CTL','F/CTL'], ['CB','CB'], ['CRUISE','CRUISE'], ['STS','STATUS'], ['VIDEO','VIDEO'],
].map(([key,title],index)=>({key,title,index}));
export const scenarios = {ground:'On stand · APU / external power', idle:'Taxi · four engines idle', cruise:'Cruise · FL350'};
export const faults = {
  oil:{title:'ENG 2 OIL TEMPERATURE',page:0},
  hyd:{title:'HYD GREEN SYS LO PR',page:10},
  door:{title:'DOOR FWD LEFT OPEN',page:5},
  brake:{title:'BRAKES HOT',page:9},
};
export const checklist = ['PARKING BRAKE … SET','DOORS … CLOSED','FLIGHT CONTROLS … CHECKED','T.O DATA … REVIEWED'];
export function initialState(){return {page:0,manual:0,scenario:'idle',brightness:90,power:true,all:false,more:false,fault:null,cleared:[],cancelled:[],message:'Select a system page on the ECAM panel.',config:null,checklist:false,abnormal:false,cursor:0,checked:[]};}
export function autoPage(s){return s.fault&&!s.cleared.includes(s.fault)&&!s.cancelled.includes(s.fault)?faults[s.fault].page:s.scenario==='cruise'?13:s.scenario==='ground'?1:0;}
export function activeAlerts(s){return s.fault&&!s.cleared.includes(s.fault)&&!s.cancelled.includes(s.fault)?[s.fault]:[];}
export function reduce(s,a){
  s={...s};
  switch(a.type){
    case 'reset':return initialState();
    case 'page': {
      if(!Number.isInteger(a.page)||!pages[a.page])return s;
      const previousMore=s.more;s.all=false;s.more=false;
      if(a.page===14&&s.page===14){s.more=!previousMore;s.message='STATUS detail toggled for review.';}
      else {s.manual=s.manual===a.page?null:a.page;s.page=s.manual??autoPage(s);s.message=s.manual===null?'Automatic page selection restored.':`${pages[s.page].title} selected.`;}
      break;
    }
    case 'all-start':s.all=true;s.page=(s.page+1)%pages.length;s.manual=s.page;s.message='ALL held · cycling one page per second.';break;
    case 'all-tick':if(s.all){s.page=(s.page+1)%pages.length;s.manual=s.page;}break;
    case 'all-end':if(s.all){s.all=false;s.message=`ALL released · ${pages[s.page].title} retained.`;}break;
    case 'brightness':s.brightness=Math.min(100,Math.max(0,Number(a.value)||0));break;
    case 'power':s.power=!!a.value;break;
    case 'scenario':if(scenarios[a.value]){s.scenario=a.value;s.config=null;s.message='Synthetic scenario changed. No simulator data is used.';if(s.manual===null)s.page=autoPage(s);}break;
    case 'fault':
      if(a.value!==''&&!faults[a.value])return s;
      s.fault=a.value||null;s.cleared=[];s.cancelled=[];s.config=null;s.more=false;s.manual=null;s.page=autoPage(s);
      s.message=s.fault?`${faults[s.fault].title} · synthetic test alert.`:'Synthetic test alert removed.';break;
    case 'clr':{
      const alert=activeAlerts(s)[0];if(alert)s.cleared=[...s.cleared,alert];
      s.message=alert?'Test alert cleared from the review queue. Use RCL to recall.':'No active test alert to clear.';
      s.manual=null;s.page=autoPage(s);break;
    }
    case 'rcl':case 'rcl-last':s.cleared=[];s.cancelled=[];s.message=s.fault?'Test alert recalled.':'No stored test alerts.';if(s.fault){s.manual=null;s.page=autoPage(s);}break;
    case 'emercanc':s.cancelled=s.fault?[s.fault]:[];s.message=s.fault?'Synthetic alert cancelled in preview only. RCL restores it.':'No test alert to cancel.';break;
    case 'more':s.more=!s.more;s.message=s.more?'Additional page detail / review data shown.':'Additional detail hidden.';break;
    case 'toconfig':s.config=s.fault?'TEST CONFIG NOT NORMAL':'TEST CONFIG NORMAL';s.message='Preview-only configuration check. Not an aircraft takeoff calculation.';break;
    case 'cl':s.checklist=!s.checklist;s.abnormal=false;s.cursor=0;s.message='Example checklist controls affect the test area, not the aircraft.';break;
    case 'abnproc':s.checklist=true;s.abnormal=!s.abnormal;s.cursor=0;s.message='Example abnormal-procedure test area selected.';break;
    case 'up':s.cursor=Math.max(0,s.cursor-1);s.message='Checklist selection moved up.';break;
    case 'down':s.cursor=Math.min(checklist.length-1,s.cursor+1);s.message='Checklist selection moved down.';break;
    case 'check':s.checked=s.checked.includes(s.cursor)?s.checked.filter(i=>i!==s.cursor):[...s.checked,s.cursor];s.message='Example checklist item toggled.';break;
    default:return s;
  }
  return s;
}

// Synthetic values for the unmodified FlyByWire page components. Unspecified
// inputs default to zero only inside this explicitly labelled web review.
export function fixture(name,s){
  const n=name.toUpperCase(),running=s.scenario!=='ground',flight=s.scenario==='cruise';
  const eng=Number(n.match(/(?:[:_]|ENG)([1-4])(?:_|$)/)?.[1]||1);
  if(n.includes('ECAM_SD_PAGE_TO_SHOW'))return s.page;
  if(n.includes('SD_MORE')||n.includes('BTN_MORE')||n.includes('PREVIEW_MORE'))return Number(s.more);
  if(n.includes('FWS')&&n.includes('HEALTHY'))return 1;
  if(n.includes('ENGINE_STATE:'))return Number(running);
  if(n.includes('ENG_MODE_SEL'))return 1;
  if(n.includes('ENGINE_N2:'))return running?(flight?91.4:64.2)+eng*.1:0;
  if(n.includes('ENGINE_N3:'))return running?(flight?94.8:71.1)+eng*.1:0;
  if(n.includes('ENGINE_FF:'))return running?(flight?2450:650)+eng*20:0;
  if(n.includes('ENGINE_OIL_QTY:'))return 17+eng*.3;
  if(n.includes('OIL TEMPERATURE:'))return s.fault==='oil'&&eng===2?195:running?83+eng:21;
  if(n.includes('OIL PRESSURE:'))return running?85+eng:0;
  if(n.includes('VIBRATION:'))return running?.2+eng*.1:0;
  if(n.includes('FADEC_'))return 1;
  if(n.includes('FUEL_USED:'))return flight?2100+eng*80:0;
  if(n.includes('APU_N2'))return flight?0:99;
  if(n.endsWith('APU_N'))return flight?0:100;
  if(n.includes('APU_EGT_CAUTION'))return 900;
  if(n.includes('APU_EGT_WARNING'))return 1000;
  if(n.includes('APU_EGT'))return flight?20:430;
  if(n.includes('APU_FUEL_USED'))return 120;
  if(n.includes('APU_BLEED_CONTAINER_PRESSURE'))return 54.7;
  if(n.includes('CORRECTED_AVERAGE_STATIC_PRESSURE'))return 14.7;
  if(n.includes('APU')&&(n.includes('AVAILABLE')||n.includes('MASTER_SW')||n.includes('BLEED')||n.includes('IS_ON')||n.includes('IS_POWERED')||n.includes('CONTACTOR')))return Number(!flight);
  if(n.includes('REPORTED_BRAKE_TEMPERATURE'))return s.fault==='brake'?460:85;
  if(n.includes('DOOR')&&n.includes('OPEN'))return s.fault==='door'?1:0;
  if(n.includes('INTERACTIVE POINT OPEN:'))return s.fault==='door'&&n.endsWith(':0')?100:0;
  if(n.includes('HYD_')&&n.includes('PRESSURE_SWITCH'))return Number(running&&!(s.fault==='hyd'&&n.includes('GREEN')));
  if(n.includes('HYD_')&&n.includes('PRESSURE'))return running&&!(s.fault==='hyd'&&n.includes('GREEN'))?5000:0;
  if(n.includes('HYD_')&&n.includes('RESERVOIR_LEVEL'))return 85;
  if(n.includes('TEMPERATURE')||n.includes('_TEMP'))return 21;
  if(n.includes('CABIN_ALTITUDE'))return flight?6500:0;
  if(n.includes('DELTA_PRESSURE')||n.includes('DIFF_PRESS'))return flight?8.1:0;
  if(n.includes('OXYGEN'))return 1800;
  if(n.includes('POTENTIAL'))return n.includes('DC_')||n.includes('BAT_')||n.includes('TR_')?28:115;
  if(n.includes('FREQUENCY'))return 400;
  if(n.includes('_LOAD'))return 32;
  if(n.includes('CURRENT'))return 12;
  if(n.includes('IS_POWERED')||n.includes('IS_CLOSED')||n.includes('PB_IS_AUTO')||n.includes('CIRCUIT CONNECTION')||n.includes('MASTER ALTERNATOR'))return 1;
  if(n.includes('GEAR')&&(n.includes('POSITION')||n.includes('COMPRESSED')||n.includes('DOWNLOCKED')))return Number(!flight);
  if(n.includes('TANK')&&(n.includes('QUANTITY')||n.includes('WEIGHT')))return 8000;
  if(n.includes('CPIOM')||n.includes('CPCS')||n.includes('PUMP_RUNNING_WORD'))return 0x3fffffff;
  return 0;
}
export function word(value=0,valid=true){return {value,isNormalOperation:()=>valid,isFailureWarning:()=>!valid,isNoComputedData:()=>false,valueOr:f=>valid?value:f,bitValueOr:(i,f)=>valid?!!(Math.trunc(value)&2**(i-1)):f};}
