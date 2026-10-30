// SPDX-License-Identifier: GPL-3.0-or-later
import React, {useEffect,useRef,useState} from 'react';
import {createRoot} from 'react-dom/client';
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
import {pages,scenarios,faults,checklist,initialState,reduce,activeAlerts} from './model.mjs';
import {setFixtureState} from './fixtures';
import {mountV2,updateV2} from './v2';
const originalPages=[EngPage,ApuPage,BleedPage,CondPage,PressPage,DoorPage,ElecAcPage,ElecDcPage,FuelPage,WheelPage,HydPage,null,CbPage,null,StatusPage];
const buttonOrder=[0,2,4,6,8,1,3,5,7,9,10,11,12,14,15];

class DisplayBoundary extends React.Component<any,any>{
  state={error:null};static getDerivedStateFromError(error){return {error};}
  componentDidCatch(error){console.error('ECAM review page failed:',error);}
  render(){return this.state.error?<svg viewBox="0 0 768 1024" aria-label="Display error"><text x="384" y="330" textAnchor="middle" fill="#ffb400" fontSize="25">PREVIEW PAGE ERROR</text><text x="384" y="380" textAnchor="middle" fill="white" fontSize="18">Select another page or reset the preview.</text></svg>:this.props.children;}
}
function PermanentData({state}){const f=state.scenario==='cruise';return <g className="permanent-data"><path d="M0 864H768 M0 946H768 M254 865V946 M510 865V946" className="White SW2"/><text x="15" y="894" className="F23 White">TAT</text><text x="235" y="894" className="F26 Green EndAlign">{f?'-23':'+21'}<tspan className="Cyan F21">°C</tspan></text><text x="15" y="929" className="F23 White">SAT</text><text x="235" y="929" className="F26 Green EndAlign">{f?'-54':'+21'}<tspan className="Cyan F21">°C</tspan></text><text x="380" y="910" className="F30 Green MiddleAlign">12:34<tspan className="F23">56</tspan></text><text x="527" y="894" className="F23 White">GW</text><text x="748" y="894" className="F26 Green EndAlign">{f?'480000':'510000'}<tspan className="Cyan F18">KG</tspan></text><text x="527" y="929" className="F23 White">GWCG</text><text x="746" y="929" className="F26 Green EndAlign">37.5<tspan className="Cyan F21">%</tspan></text><text x="18" y="984" className="F22 Cyan">REVIEW DATA · NOT CONNECTED</text></g>;}
function StatusDetail({state}){return <g><path d="M0 700H768" className="White SW2"/><text x="25" y="737" className="F25 White TextUnderline">REVIEW DETAIL</text><text x="25" y="780" className="F23 Cyan">{state.fault?faults[state.fault].title:'NO INJECTED TEST FAULT'}</text><text x="25" y="820" className="F21 White">CLR / RCL controls the adjacent test queue.</text></g>;}
function App(){
  const [s,setState]=useState(initialState);const stateRef=useRef(s);stateRef.current=s;
  const dispatch=(a)=>setState(old=>reduce(old,a));
  const v2Ref=useRef(null),renders=useRef(0);renders.current++;
  useEffect(()=>{mountV2(v2Ref.current);updateV2(stateRef.current);},[]);
  useEffect(()=>{setFixtureState(s);updateV2(s);},[s]);
  useEffect(()=>{if(!s.all)return;const id=setInterval(()=>dispatch({type:'all-tick'}),1000);return ()=>clearInterval(id);},[s.all]);
  useEffect(()=>{const release=()=>dispatch({type:'all-end'});window.addEventListener('blur',release);document.addEventListener('visibilitychange',release);return ()=>{window.removeEventListener('blur',release);document.removeEventListener('visibilitychange',release);};},[]);
  const Current=originalPages[s.page];const v2=[11,13,15].includes(s.page);const alerts=activeAlerts(s);
  const button=(label,action,extra={})=><button type="button" className="ecp-key" onClick={()=>dispatch({type:action})} {...extra}>{label}</button>;
  return <>
    <header className="site-header"><a className="brand" href="/" aria-label="A380X ECAM review"><strong>A380X</strong><span>AVIONICS LAB <b>04 / ECAM</b></span></a><nav aria-label="Avionics previews"><a href="http://127.0.0.1:8383/">MFD ↗</a><span className="preview-badge">LOCAL REVIEW ONLY</span></nav></header>
    <main><section className="intro"><div><p className="eyebrow">LOWER ECAM / SYSTEM DISPLAY</p><h1>Every system. At a touch.</h1><p>FlyByWire page artwork with an interactive ECAM control panel.<br/>Check the display here before we connect it to your 3D cockpit.</p></div><button className="reset" onClick={()=>dispatch({type:'reset'})}>↺ Reset preview</button></section>
      <div className="workspace">
        <section className="display-card" id="system-display" aria-label="Lower ECAM system display"><div className="card-heading"><span>SD <small>LOWER SYSTEM DISPLAY</small></span><span id="page-label">{pages[s.page].title}</span></div>
          <div className="compact-nav"><label>Review page<select aria-label="Review page" value={s.page} onChange={e=>dispatch({type:'page',page:Number(e.target.value),previousMore:s.more})}>{pages.map(p=><option key={p.index} value={p.index}>{p.title}</option>)}</select></label><a href="#ecp-title">ECAM buttons ↓</a></div>
          <div className="display-bezel"><i className="screw tl"/><i className="screw tr"/><div className="display-glass" role="img" aria-label={`${pages[s.page].title} system display${s.power&&s.brightness?'':' off'}`} data-page={s.page} data-renders={renders.current}>
            <div className="display-image" style={{opacity:s.power?s.brightness/100:0}}>
              <div ref={v2Ref} className="v2-pages" style={{display:v2?'block':'none'}}/>
              {!v2&&Current&&<DisplayBoundary key={`${s.page}:${s.scenario}`}><svg className="sd-svg" viewBox="0 0 768 1024" xmlns="http://www.w3.org/2000/svg"><Current/>{s.page===14&&s.more&&<StatusDetail state={s}/>}</svg></DisplayBoundary>}
              <svg className="footer-svg" viewBox="0 0 768 1024"><rect x="0" y="864" width="768" height="160"/><PermanentData state={s}/></svg>
            </div>
          </div><i className="screw bl"/><i className="screw br"/><div className="bezel-label">SYSTEM DISPLAY <span>A380–800</span></div></div>
          <div className="display-caption"><span className="live-dot"/> {s.manual===null?'AUTOMATIC':'MANUAL'} PAGE <span>768 × 1024 · Original EIS font</span></div>
        </section>
        <div className="control-column"><section className="ecp" aria-labelledby="ecp-title"><div className="panel-title"><h2 id="ecp-title">ECAM CONTROL PANEL</h2><a className="back-to-display" href="#system-display">Display ↑</a><span>INTERACTIVE</span></div>
          <div className="page-keys" aria-label="System page buttons">{buttonOrder.map(i=><button type="button" key={i} className={`ecp-key page-key ${s.manual===i?'selected':''}`} aria-label={`Select ${pages[i].title}`} aria-pressed={s.manual===i} onClick={()=>dispatch({type:'page',page:i,previousMore:s.more})}><span className="key-light"/>{pages[i].key}</button>)}</div>
          <div className="panel-divider"/>
          <div className="ecp-secondary"><button type="button" className={`ecp-key all-key ${s.all?'selected':''}`} aria-label="ALL — hold to cycle pages" aria-pressed={s.all}
            onPointerDown={e=>{if(e.button!==0)return;e.preventDefault();e.currentTarget.setPointerCapture(e.pointerId);e.currentTarget.focus();dispatch({type:'all-start'});}} onPointerUp={()=>dispatch({type:'all-end'})} onPointerCancel={()=>dispatch({type:'all-end'})} onLostPointerCapture={()=>dispatch({type:'all-end'})} onKeyDown={e=>{if([' ','Enter'].includes(e.key)&&!e.repeat){e.preventDefault();dispatch({type:'all-start'});}}} onKeyUp={e=>{if([' ','Enter'].includes(e.key)){e.preventDefault();dispatch({type:'all-end'});}}} onBlur={()=>dispatch({type:'all-end'})}>ALL<small>HOLD</small></button>
            {button('T.O CONFIG','toconfig')}{button('MORE','more',{'aria-pressed':s.more})}{button('RCL LAST','rcl-last')}{button('RCL','rcl')}
            {button('CLR','clr',{'aria-label':'CLR left',className:`ecp-key ${alerts.length?'lit':''}`})}{button('C/L','cl',{'aria-pressed':s.checklist&&!s.abnormal})}{button('ABN PROC','abnproc',{'aria-pressed':s.abnormal})}{button('EMER CANC','emercanc')}{button('CLR','clr',{'aria-label':'CLR right',className:`ecp-key ${alerts.length?'lit':''}`})}
          </div>
          <div className="check-keys">{button('✓','check',{'aria-label':'Checklist check left'})}{button('▲','up',{'aria-label':'Checklist up'})}{button('▼','down',{'aria-label':'Checklist down'})}{button('✓','check',{'aria-label':'Checklist check right'})}</div>
          <div className="brightness"><label htmlFor="brightness">SD BRIGHTNESS <output>{s.brightness}%</output></label><input id="brightness" type="range" min="0" max="100" value={s.brightness} onChange={e=>dispatch({type:'brightness',value:e.target.value})}/><label className="power"><input type="checkbox" checked={s.power} onChange={e=>dispatch({type:'power',value:e.target.checked})}/> Display power</label></div>
          <p className="panel-help">Press a selected page again to return to AUTO. Hold ALL to scan the pages. Keyboard: Tab + Enter / Space.</p>
        </section>
        <section className="testbench" aria-label="Synthetic test data"><div className="test-heading"><h2>Review conditions</h2><span>SYNTHETIC DATA</span></div><div className="test-selects"><label>Aircraft state<select value={s.scenario} onChange={e=>dispatch({type:'scenario',value:e.target.value})}>{Object.entries(scenarios).map(([value,label])=><option key={value} value={value}>{label}</option>)}</select></label><label>Test alert<select value={s.fault||''} onChange={e=>dispatch({type:'fault',value:e.target.value})}><option value="">No test alert</option>{Object.entries(faults).map(([value,f])=><option key={value} value={value}>{f.title}</option>)}</select></label></div>
          <p className="action-feedback" role="status">{s.message}</p>
          <div className="message-test"><div><span className="tiny-label">ECP ACTION TEST AREA · NOT AN EWD</span>{s.config&&<p className={s.fault?'amber':'green'}>{s.config}</p>}{alerts.length?alerts.map(id=><p className="amber" key={id}>{faults[id].title}</p>):<p className="quiet">{s.fault?'Test alert cleared / cancelled.':'No active test alerts.'}</p>}</div>
          {s.checklist&&<div className="checklist"><h3>{s.abnormal?'ABNORMAL PROCEDURE':'CHECKLIST'} · EXAMPLE ONLY</h3><p>Interaction sample — not an operational procedure.</p>{checklist.map((line,i)=><button type="button" key={line} className={`${s.cursor===i?'current':''} ${s.checked.includes(i)?'done':''}`} onClick={()=>setState(old=>({...old,cursor:i}))}><span>{s.checked.includes(i)?'✓':'□'}</span> {line}</button>)}</div>}</div>
          <p className="boundary-note">CLR, RCL, T.O CONFIG and checklist keys operate this local test area. They do not run the aircraft’s flight-warning computer.</p>
        </section></div>
      </div>
      <section className="notes"><div><h2>Original pages. Isolated inputs.</h2><p>FlyByWire’s React SD synoptics and xp-kit F/CTL, CRUISE and VIDEO pages. Page graphics, values and some upstream TODOs are reproduced for visual review. VIDEO correctly shows NOT AVAIL; no camera feed is invented.</p></div><div><h2>Review first, cockpit next.</h2><p>No aircraft installation, simulator data connection or drive remount runs from this website. Mouse movement does not repaint the page or send network events.</p><a href="/manifest.json">View build / source information ↗</a></div></section>
    </main><footer><span>A380X · ECAM REVIEW 01</span><span>FlyByWire-derived · GPL-3.0 · Synthetic values, not flight data</span></footer>
  </>;
}
createRoot(document.getElementById('app')!).render(<App/>);
