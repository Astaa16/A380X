// SPDX-License-Identifier: GPL-3.0-or-later
// Live aircraft inputs only. Loaded after the common, validated PFD transport.
(() => {
  const base = window.__a380xTrial;
  const host = window.__xphfbw;
  const side = new URLSearchParams(location.search).get('index') === '2' ? 1 : 0;
  const letter = side ? 'R' : 'L';
  const vars = base.vars;
  const set = (name,value) => vars.set(name.toUpperCase(),value);
  const word = (name,value,valid) => set(name,base.rawWord(value,valid));
  const originalApply = base.apply;
  const originalRead = host.readFile;
  let instrument, routeStamp = '', airportStamp = '', requestId = 0, lastEvent = 0, lastClick = 0, pressed, airportSent='',airportSentAt=0;
  const requests = new Map(), airportCache = new Map();
  let visualStamp = '', lastCapture = -Infinity, interactionRevision = 0, settleUntil = 0, lastHover;
  const captureProfile = { requested: 0, skipped: 0 };
  const invalidate = (settle = 0) => { interactionRevision++; settleUntil = Math.max(settleUntil, performance.now()+settle); };
  document.fonts?.ready?.then(() => invalidate(300));
  document.documentElement?.setAttribute('data-native-cursor','true');

  const post = (data) => window.webkit.messageHandlers.a380.postMessage(data);
  const packed = (text) => [...text.slice(0,8)].reduce((value,char,index) => value+(char.charCodeAt(0)-31)*2**(index*6),0);
  const degrees = Math.PI/180;
  function geometry(a,b) {
    const lat1=a.lat*degrees,lat2=b.lat*degrees,dl=(b.lon-a.lon)*degrees,da=lat2-lat1;
    const hav=Math.min(1,Math.max(0,Math.sin(da/2)**2+Math.cos(lat1)*Math.cos(lat2)*Math.sin(dl/2)**2));
    return {distance:3440.065*2*Math.atan2(Math.sqrt(hav),Math.sqrt(1-hav)),
      bearing:(Math.atan2(Math.sin(dl)*Math.cos(lat2),Math.cos(lat1)*Math.sin(lat2)-Math.sin(lat1)*Math.cos(lat2)*Math.cos(dl))/degrees+360)%360};
  }
  base.apply = (state) => {
    originalApply(state);
    const nav=state.nav||{}, controls=nav.controls?.[side]||{};
    const values=state.values||{}, ad=(name)=>values['a380x/xp12/adirs/'+name];
    for(const [s,pot] of [['captain_nd',89],['first_officer_nd',91]]) {
      const level=values['a380x/xp12/du/'+s];
      set('LIGHT POTENTIOMETER:'+pot,state.ok&&Number.isFinite(level)?Math.max(0,Math.min(1,level)):0);
    }
    for(let unit=1;unit<=3;unit++) {
      const ir=`L:A32NX_ADIRS_IR_${unit}_`,adr=`L:A32NX_ADIRS_ADR_${unit}_`;
      const valid=state.ok&&ad(`ir${unit}_nav_valid`)===1&&nav.positionValid;
      for(const [out,value] of Object.entries({LATITUDE:ad(`ir${unit}_latitude_deg`),LONGITUDE:ad(`ir${unit}_longitude_deg`),
        TRUE_HEADING:ad(`ir${unit}_heading_deg`)+nav.magVar,TRUE_TRACK:ad(`ir${unit}_track_deg`)+nav.magVar,
        WIND_DIRECTION_BNR:(nav.windDir-nav.magVar+360)%360,WIND_SPEED_BNR:nav.windSpeed})) word(ir+out,value,valid);
      word(adr+'TRUE_AIRSPEED',nav.tas,state.ok&&ad(`adr${unit}_valid`)===1);
      word(adr+'STATIC_AIR_TEMPERATURE',ad(`adr${unit}_sat_c`),state.ok&&ad(`adr${unit}_valid`)===1);
      word(adr+'TOTAL_AIR_TEMPERATURE',ad(`adr${unit}_tat_c`),state.ok&&ad(`adr${unit}_valid`)===1);
    }
    set(`L:A32NX_EFIS_${letter}_ND_MODE`,controls.mode??3);
    set(`L:A32NX_EFIS_${letter}_ND_RANGE`,controls.range??3);
    set(`L:A32NX_EFIS_${letter}_OANS_RANGE`,controls.zoom??1);
    set(`L:A32NX_EFIS_${letter}_OPTION`,controls.option??0);
    set(`L:A32NX_EFIS_${letter}_NAVAID_1_MODE`,controls.vor1?2:0);
    set(`L:A32NX_EFIS_${letter}_NAVAID_2_MODE`,controls.vor2?2:0);
    set(`L:A380X_EFIS_${letter}_LS_BUTTON_IS_ON`,+!!controls.ls);
    set('AUTOPILOT HEADING LOCK DIR:1',nav.selectedHeading??0);
    set('AUTOPILOT ALTITUDE LOCK VAR:3',nav.selectedAltitude??0);
    set('L:A380X_WXR_OFF',1);
    set('L:A32NX_TCAS_MODE',0); // TCAS encounter classification has not been ported.
    set('PLANE LATITUDE',nav.lat??0);set('PLANE LONGITUDE',nav.lon??0);
    set('E:ABSOLUTE TIME',62135596800+(nav.zulu??0));
    set('L:A32NX_FM_LS_COURSE',nav.radios?.[side]?.course??0);
    const selected=nav.to;
    const to=selected&&nav.positionValid?geometry(nav,selected):null;
    set(`L:A32NX_EFIS_${letter}_TO_WPT_IDENT_0`,packed(to?selected.ident:''));
    set(`L:A32NX_EFIS_${letter}_TO_WPT_IDENT_1`,0);
    set(`L:A32NX_EFIS_${letter}_TO_WPT_DISTANCE`,to?.distance??-1);
    set(`L:A32NX_EFIS_${letter}_TO_WPT_BEARING`,to?(to.bearing-nav.magVar+360)%360:-1);
    set(`L:A32NX_EFIS_${letter}_TO_WPT_TRUE_BEARING`,to?.bearing??-1);
    set(`L:A32NX_EFIS_${letter}_TO_WPT_ETA`,to&&nav.groundSpeed>30?(nav.zulu+to.distance/nav.groundSpeed*3600)%86400:-1);
    word(`L:A32NX_EFIS_${letter}_MRP_LAT`,selected?.lat??nav.lat,state.ok&&nav.positionValid);
    word(`L:A32NX_EFIS_${letter}_MRP_LONG`,selected?.lon??nav.lon,state.ok&&nav.positionValid);
    for(let i=1;i<=2;i++) {
      const radio=nav.radios?.[i-1]||{};
      host.setString('NAV IDENT:'+i,radio.valid?radio.ident:'');
      set('NAV ACTIVE FREQUENCY:'+i,radio.frequency??0);
      set('NAV HAS NAV:'+i,+!!radio.valid);set('NAV HAS DME:'+i,+!!radio.dmeValid);
      set('NAV DME:'+i,radio.distance??0);set('NAV OBS:'+i,radio.course??0);
      set('NAV RELATIVE BEARING TO STATION:'+i,(radio.bearing-(ad(`ir${side+1}_heading_deg`)??0)+360)%360);
      set('NAV RADIAL ERROR:'+i,(radio.deviation??0)*5);
    }
  };
  host.screen=side?'SCREEN_DU_NDR':'SCREEN_DU_NDL';
  const originalSnapshot=host.snapshot;
  host.snapshot=()=>{originalSnapshot();const state=base.state;if(state.ok&&Number.isFinite(state.nav?.zulu))set('E:ABSOLUTE TIME',62135596800+state.nav.zulu);};
  host.magVar=()=>base.state.nav?.magVar??0;
  host.readFile=(name)=>new URL(name,location.href).pathname.endsWith('/ND/bundle.js')?window.__a380NdSource:originalRead(name);
  function pointer(event) {
    const [seq,type,x,y,amount]=event;
    if(seq<=lastEvent)return;
    lastEvent=seq;
    const target=document.elementFromPoint(x,y);
    if(!target)return;
    const fire=(name,element=target,detail=1)=>element.dispatchEvent(new MouseEvent(name,{bubbles:true,cancelable:true,
      clientX:x,clientY:y,screenX:x,screenY:y,button:0,buttons:pressed?1:0,detail}));
    if(type===0) {
      fire('mousemove',pressed?.target||target);
      const hover=target.closest?.('button,input,select,.oanc-label,[role=button]');
      if(pressed) invalidate(250);
      else if(hover!==lastHover) { lastHover=hover; invalidate(100); }
    }
    if(type>0 && type!==4) invalidate(1200);
    if(type===1){pressed={target,x,y};fire('mousemove');fire('mousedown');}
    if(type===2){
      const previous=pressed;pressed=null;fire('mouseup',previous?.target||target);
      if(previous&&Math.hypot(previous.x-x,previous.y-y)<8){
        const now=performance.now(), twice=now-lastClick<450;
        fire('click',target,twice?2:1);if(twice)fire('dblclick',target,2);lastClick=now;
      }
    }
    if(type===3){
      // The EFIS ZOOM selection stays authoritative; wheel is a local OANS zoom command.
      post({type:'a380x-nd-command',side,action:amount>0?'zoom_in':'zoom_out'});
    }
    if(type===4){const previous=pressed;pressed=null;fire('mouseup',previous?.target||document);instrument?.cursorVisible.set(false);}
    if(type===5){instrument.controlPanelVisible.set(!instrument.controlPanelVisible.get());instrument.controlPanelRef.instance.activeTabIndex.set(0);}
    if(type===6)instrument.bus.send('oans_center_on_acft',true);
  }
  window.__a380Nd={
    get state(){return base.state;},get instrument(){return instrument;},
    captureProfile,
    needsSnapshot(){
      const state=base.state, nav=state.nav||{}, o=instrument?.oansRef?.instance;
      const values=Object.entries(state.values||{}).filter(([key])=>
        (/^a380x\/xp12\/adirs\/(?:ir[123]_(?:latitude_deg|longitude_deg|heading_deg|track_deg|ground_speed_knots|nav_valid|heading_valid|attitude_valid)|adr[123]_(?:valid|sat_c|tat_c)|(?:captain|first_officer)_(?:ir|adr)_source)$/.test(key)) || key===`a380x/xp12/du/${side?'first_officer':'captain'}_nd`);
      // Pointer position/events are independent of the map pixels. Preserve all
      // actual nav, selected-mode, source, route and validity changes.
      const {pointers, controls, zulu, ...map}=nav;
      const read=(value)=>value?.get?.();
      const stamp=JSON.stringify([!!state.ok,values,map,controls?.[side],Math.floor(zulu||0),
        interactionRevision,globalThis.__a380NdHover,read(instrument?.controlPanelVisible),o?.dataLoading,o?.doneDrawing,
        read(o?.dataAirportIcao),read(o?.zoomLevelIndex),read(o?.panOffsetX),read(o?.panOffsetY),
        read(o?.interpolatedMapHeading),read(o?.modeAnimationOffsetX),read(o?.modeAnimationOffsetY),
        o?.markerManager?.crosses?.items?.().length,o?.markerManager?.flags?.items?.().length]);
      const now=performance.now();
      // A two-Hz safety refresh retains original flash/async indications;
      // active data and map transitions still capture at the host update rate.
      const changed=stamp!==visualStamp || now<settleUntil || now-lastCapture>=500;
      if(changed) { visualStamp=stamp;lastCapture=now;captureProfile.requested++; }
      else captureProfile.skipped++;
      return changed;
    },
    airport(icao){
      if(!/^[A-Z0-9_-]{2,16}$/.test(icao))return Promise.reject(new Error('Invalid airport identifier'));
      if(!airportCache.has(icao))airportCache.set(icao,new Promise((resolve,reject)=>{
        const id=++requestId;requests.set(id,{resolve,reject});post({type:'a380x-airport',id,icao});
      }).catch((error)=>{airportCache.delete(icao);throw error;}));
      return airportCache.get(icao);
    },
    airportResult(id,error,data){const request=requests.get(id);if(!request)return;requests.delete(id);
      if(error)request.reject(new Error(error));else request.resolve(data);invalidate(1500);},
    onUpdate(bus,owner){
      instrument=owner;
      const state=base.state,nav=state.nav||{};
      const panel=owner.controlPanelRef.instance;
      panel.activeDatabase.set('LOCAL SCENERY');
      const stamp=JSON.stringify(nav.airports||[]);
      if(stamp!==airportStamp){airportStamp=stamp;panel.store.airports.set(nav.airports||[]);panel.navigraphAvailable.set(true);}
      const nearest=nav.airport;
      const oanc=owner.oansRef.instance;
      if(nearest&&nav.controls?.[side]?.range===0&&!oanc.dataLoading&&oanc.dataAirportIcao.get()!==nearest&&!panel.manualAirportSelection
          &&(airportSent!==nearest||performance.now()-airportSentAt>10000)){
        airportSent=nearest;airportSentAt=performance.now();
        bus.send('oans_display_airport',nearest);
      }
      const option=nav.controls?.[side]?.option??0;
      const flag={2:2,4:8,8:4,16:16}[option]??0;
      const data=JSON.stringify([nav.route,nav.symbols,nav.to,option]);
      if(data!==routeStamp){routeStamp=data;
        bus.send('symbols',[...(nav.symbols||[]).filter((s)=>flag&&(s.type&flag)),...(nav.route||[]).map((p,index)=>({databaseId:'fms-'+index,ident:p.ident,
          location:{lat:p.lat,long:p.lon},type:8|4096|(p.active?128:0)}))]);
        bus.send('vectorsActive',(nav.route||[]).filter((p)=>p.start).map((p)=>({type:0,startPoint:{lat:p.start[0],long:p.start[1]},endPoint:{lat:p.lat,long:p.lon}})));
        bus.send('traffic',[]);
      }
      const cursor=nav.pointers?.[side];
      if(state.ok&&cursor){for(const event of cursor.events||[])pointer(event);
        // X-Plane draws the original yellow cursor over the cached texture.
        // Keep forwarding ordered UI/pan events, without repainting a DOM cursor.
        owner.cursorVisible.set(false);
      }else{
        if(pressed){const previous=pressed;pressed=null;previous.target.dispatchEvent(new MouseEvent('mouseup',{bubbles:true,buttons:0}));}
        pressed=null;owner.oansRef.instance.isPanning=false;owner.oansRef.instance.isPanningArmed=false;owner.cursorVisible.set(false);
      }
      if((document.querySelector('a380x-nd')?.frameCount??0)%20===0) {
        const o=owner.oansRef.instance;
        post({type:'a380x-nd-status',values:[owner.efisNdMode,nav.controls?.[side]?.range??0,o.zoomLevelIndex.get(),
          o.dataLoading?0:o.data?.features.length??0,+!!cursor?.inside,o.markerManager.crosses.items().length,
          o.markerManager.flags.items().length,+owner.controlPanelVisible.get()]});
      }
    },
  };
})();
