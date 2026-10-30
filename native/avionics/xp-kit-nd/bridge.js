// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit demo inputs for original FlyByWire ND/OANC components. No sim uplink.
(() => {
  const side = new URLSearchParams(location.search).get('index') === '2' ? 'R' : 'L';
  const vars = new Map(), reads = new Map(), strings = new Map(), stored = new Map();
  const start = performance.now();
  const float = new Float32Array(1), bits = new Uint32Array(float.buffer);
  const key = (name) => String(name).replace(/^A:/, '').toUpperCase();
  const set = (name, value) => vars.set(key(name), value);
  const raw = (value, valid = true) => { float[0] = Number.isFinite(value) ? value : 0; return bits[0] + (valid ? 3 : 0) * 2 ** 32; };
  const word = (name, value, valid = true) => set(name, raw(value, valid));
  let current = { airport: 'UAAA', mode: side === 'L' ? 3 : 4, range: side === 'L' ? 3 : 0,
    zoom: 1, heading: 55, wind: 24, option: 0, failed: false, power: true, brightness: 1 };
  let airport, version = 1, sent = 0, instrument, messageVersion = 0, displayedAirport;
  const post = (type, details = {}) => parent.postMessage({ type, side, ...details }, location.origin);
  function update() {
    const isOans = current.range === 0;
    const ground = isOans;
    const stand = airport?.stands[0];
    const lat = airport ? ground ? stand?.lat ?? airport.origin[0] : airport.origin[0] + .025 : 43.35;
    const lon = airport ? ground ? stand?.lon ?? airport.origin[1] : airport.origin[1] - .035 : 77.02;
    const heading = current.heading;
    const seconds = (performance.now() - start) / 1000;
    for (const name of ['AC_ESS', 'AC_1', 'AC_2', 'DC_ESS', 'DC_1', 'DC_2', '108PH']) set(`L:A32NX_ELEC_${name}_BUS_IS_POWERED`, +current.power);
    set('CIRCUIT AVIONICS ON', +current.power);
    set('LIGHT POTENTIOMETER:89', current.brightness);
    set('LIGHT POTENTIOMETER:91', current.brightness);
    set('L:A32NX_ATT_HDG_SWITCHING_KNOB', 1); set('L:A32NX_AIR_DATA_SWITCHING_KNOB', 1);
    set('L:A32NX_COLD_AND_DARK_SPAWN', 0); set('L:A32NX_AIRCRAFT_PRESET_QUICK_MODE', 1);
    for (let n = 1; n <= 3; n++) {
      const ir = `L:A32NX_ADIRS_IR_${n}_`, adr = `L:A32NX_ADIRS_ADR_${n}_`;
      for (const [name, value] of Object.entries({ PITCH: 2, ROLL: 0, HEADING: heading, TRUE_HEADING: heading,
        TRACK: heading + (ground ? 0 : 3), TRUE_TRACK: heading + (ground ? 0 : 3), LATITUDE: lat, LONGITUDE: lon,
        GROUND_SPEED: ground ? 8 : 286, WIND_DIRECTION_BNR: 285, WIND_SPEED_BNR: current.wind,
        VERTICAL_SPEED: 0, FLIGHT_PATH_ANGLE: 0, DRIFT_ANGLE: ground ? 0 : 3, MAINT_WORD: 0 })) word(ir + name, value, !current.failed);
      for (const [name, value] of Object.entries({ COMPUTED_AIRSPEED: ground ? 0 : 250, TRUE_AIRSPEED: ground ? 8 : 310,
        ALTITUDE: ground ? 2250 : 12000, BARO_CORRECTED_ALTITUDE_1: 12000, BARO_CORRECTED_ALTITUDE_2: 12000,
        MACH: .48, BAROMETRIC_VERTICAL_SPEED: 0, STATIC_AIR_TEMPERATURE: -8, TOTAL_AIR_TEMPERATURE: 2 })) word(adr + name, value, !current.failed);
    }
    for (const s of ['L', 'R']) {
      set(`L:A32NX_EFIS_${s}_ND_MODE`, current.mode);
      set(`L:A32NX_EFIS_${s}_ND_RANGE`, current.range);
      set(`L:A32NX_EFIS_${s}_OANS_RANGE`, current.zoom);
      set(`L:A32NX_EFIS_${s}_OPTION`, current.option);
      set(`L:A32NX_EFIS_${s}_NAVAID_1_MODE`, current.mode === 1 ? 2 : 0);
      set(`L:A32NX_EFIS_${s}_NAVAID_2_MODE`, 0);
      word(`L:A32NX_EFIS_${s}_MRP_LAT`, lat, !current.failed); word(`L:A32NX_EFIS_${s}_MRP_LONG`, lon, !current.failed);
      set(`L:A32NX_EFIS_${s}_TO_WPT_BEARING`, 55); set(`L:A32NX_EFIS_${s}_TO_WPT_TRUE_BEARING`, 55);
      set(`L:A32NX_EFIS_${s}_TO_WPT_DISTANCE`, 14.6); set(`L:A32NX_EFIS_${s}_TO_WPT_ETA`, seconds + 180);
      let ident = 0;
      [...'DEMO1'].forEach((char, index) => { ident += (char.charCodeAt(0)-31) * 2 ** (index*6); });
      set(`L:A32NX_EFIS_${s}_TO_WPT_IDENT_0`, ident);
      set(`L:A32NX_EFIS_${s}_TO_WPT_IDENT_1`, 0);
      set(`L:A32NX_FMGC_${s}_RNP`, 1);
    }
    set('L:A32NX_FCU_HEADING_SELECTED', (heading + 12) % 360); set('L:A320_FCU_SHOW_SELECTED_HEADING', 1);
    set('AUTOPILOT HEADING LOCK DIR:1', (heading + 12) % 360);
    set('AUTOPILOT ALTITUDE LOCK VAR:3', 12000);
    set('L:A380X_WXR_OFF', 1); set('L:A32NX_TCAS_MODE', 0);
    set('L:A32NX_LGCIU_1_LEFT_GEAR_COMPRESSED', +ground); set('L:A32NX_LGCIU_1_RIGHT_GEAR_COMPRESSED', +ground);
    set('L:A32NX_LGCIU_1_NOSE_GEAR_COMPRESSED', +ground);
    set('SIM ON GROUND', +ground); set('PLANE LATITUDE', lat); set('PLANE LONGITUDE', lon);
    set('GPS GROUND SPEED', ground ? 8 : 286); set('AIRSPEED TRUE', ground ? 8 : 310);
    set('E:ABSOLUTE TIME', 62135596800 + Date.now() / 1000); set('E:SIMULATION TIME', seconds);
    set('E:SIMULATION RATE', 1); set('SIMULATION RATE', 1);
  }
  const route = () => {
    const lat = airport.origin[0], lon = airport.origin[1];
    const coordinates = [[.025,-.035],[.16,.22],[.35,.44],[.58,.63]].map(([a,b]) => ({lat:lat+a,long:lon+b}));
    const symbols = coordinates.slice(1).map((location, i) => ({ databaseId:'demo-'+i, ident:'DEMO'+(i+1), location, type:8|4096|(i===0?128:0) }));
    if (current.option === 2) symbols.push({databaseId:'demo-vor',ident:'VOR1',location:{lat:lat+.13,long:lon-.11},type:2|256});
    if (current.option === 16) symbols.push({databaseId:'demo-airport',ident:current.airport,location:{lat,long:lon},type:16|256});
    return {symbols, vectors: coordinates.slice(1).map((endPoint,i) => ({type:0,startPoint:coordinates[i],endPoint}))};
  };
  window.__ndDemo = {
    reads, vars, raw, get state() { return current; }, get instrument() { return instrument; },
    onUpdate(bus, owner) {
      instrument = owner;
      if (!airport) return;
      const panel = owner.controlPanelRef.instance;
      // The original airport catalogue is asynchronous. Dispatch only once it
      // contains the selected airport; do not reload the map on heading changes.
      if (displayedAirport !== current.airport && panel.store.airports.items().some((a) => a.idarpt === current.airport)) {
        displayedAirport = current.airport;
        bus.send('oans_display_airport', current.airport);
      }
      panel.activeDatabase.set('LOCAL SCENERY');
      if (sent === version) return;
      sent = version;
      const fixture = route();
      bus.send('symbols', fixture.symbols); bus.send('vectorsActive', fixture.vectors);
      bus.send('traffic', []);
      post('nd-ready', { mode: current.mode, range: current.range, airport: current.airport });
    },
  };
  window.addEventListener('message', async (event) => {
    if (event.origin !== location.origin || event.source !== parent || event.data?.type !== 'nd-state') return;
    const next = event.data.state;
    const received = ++messageVersion;
    if (!airport || next.airport !== current.airport) {
      const response = await fetch('/geometry/' + encodeURIComponent(next.airport));
      if (!response.ok) throw new Error('Airport review geometry unavailable');
      const loaded = await response.json();
      if (received !== messageVersion) return;
      airport = loaded;
    }
    current = { ...current, ...next }; version++; update();
    if (event.data.action === 'panel' && instrument) {
      if (!instrument.controlPanelVisible.get()) instrument.controlPanelRef.instance.activeTabIndex.set(0);
      instrument.controlPanelVisible.set(!instrument.controlPanelVisible.get());
    }
    if (event.data.action === 'center' && instrument) instrument.bus.send('oans_center_on_acft', true);
    if (event.data.action === 'erase' && instrument) {
      instrument.bus.send('oans_erase_all_crosses', true); instrument.bus.send('oans_erase_all_flags', true);
    }
  });
  window.__xphfbw = {
    view: side === 'L' ? 1 : 2, screen: side === 'L' ? 'SCREEN_DU_NDL' : 'SCREEN_DU_NDR', aircraftDir: 'A380X-XP12-preview',
    snapshot: update,
    getVar(name, unit) { reads.set(name, unit); return vars.get(key(name)) ?? 0; },
    setVar: set, getString(name) { return strings.get(name) ?? ''; }, setString(name, value) { strings.set(name, value); },
    call(name) { if (name === 'PLAY_INSTRUMENT_SOUND') return Promise.resolve('null'); return Promise.resolve('null'); },
    sendEvent() {}, poll() { return []; },
    storedData(op, name, value) {
      if (op === 'get') {
        if (name.includes('SELF_TEST_TIME')) return '0';
        if (name.includes('USING_METRIC_UNIT')) return '1';
        return stored.get(name) ?? '';
      }
      if (op === 'set') { stored.set(name,value); return value; }
      if (op === 'search') return '[]';
      if (op === 'delete') stored.delete(name);
      return '';
    },
    gameString(name) { return name === 'FLIGHT NAVDATA DATE RANGE' ? '08OCT04NOV/26' : ''; },
    readFile(name) {
      if (new URL(name,location.href).pathname !== '/ND/bundle.js') return null;
      const request = new XMLHttpRequest(); request.open('GET','/ND/bundle.js',false); request.send();
      return request.status === 200 ? request.responseText : null;
    },
    log(level, text) { if (level === 2) post('nd-error',{text}); }, magVar() { return 0; },
    loaded(ok, text) { post('nd-loaded',{ok,text}); },
  };
  update();
})();
