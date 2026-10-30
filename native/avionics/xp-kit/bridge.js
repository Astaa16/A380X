// SPDX-License-Identifier: GPL-3.0-or-later
(() => {
  // WebKit suspends requestAnimationFrame for an offscreen window. The native
  // renderer supplies the frame clock instead; browser previews keep normal RAF.
  if (window.__A380_NATIVE) {
    const callbacks = new Map();
    let next = 0;
    window.requestAnimationFrame = (callback) => { callbacks.set(++next, callback); return next; };
    window.cancelAnimationFrame = (id) => callbacks.delete(id);
    window.__a380NativeFrame = () => {
      const batch = [...callbacks.values()];
      callbacks.clear();
      const now = performance.now();
      for (const callback of batch) callback(now);
    };
  }
  const side = Number(new URLSearchParams(location.search).get('index')) === 2 ? 2 : 1;
  const vars = new Map();
  const strings = new Map();
  const stored = new Map([['A32NX_CONFIG_SELF_TEST_TIME', '0']]);
  const pending = [];
  const reads = new Map();
  const words = new Float32Array(1);
  const bits = new Uint32Array(words.buffer);
  let current = { ok: false };
  let updatedAt = 0;
  const started = performance.now();
  const canonical = (name) => String(name).replace(/^A:/i, '').toUpperCase();
  const post = (message) => {
    const data = { ...message, side };
    if (window.__A380_NATIVE) window.webkit.messageHandlers.a380.postMessage(data);
    else parent.postMessage(data, location.origin);
  };
  const set = (name, value) => vars.set(canonical(name), value);
  const rawWord = (value, valid) => {
    words[0] = Number.isFinite(value) ? value : 0;
    return bits[0] + (valid && Number.isFinite(value) ? 3 : 0) * 2 ** 32;
  };
  const arinc = (name, value, valid) => set(name, rawWord(value, valid));
  function apply(state) {
    current = state;
    updatedAt = performance.now();
    const data = state.values || {};
    const read = (name) => data[name];
    const ad = (name) => read(`a380x/xp12/adirs/${name}`);
    const valid = (name) => !!state.ok && ad(name) === 1;
    const bus = read('sim/cockpit2/electrical/bus_volts');
    const powered = !!state.ok && Array.isArray(bus) && bus[0] >= 18;
    for (const name of ['108PH', 'DC_ESS', 'DC_1', 'DC_2', 'AC_ESS', 'AC_1', 'AC_2']) {
      set(`L:A32NX_ELEC_${name}_BUS_IS_POWERED`, powered ? 1 : 0);
    }
    set('CIRCUIT AVIONICS ON', powered ? 1 : 0);
    set('L:A32NX_COLD_AND_DARK_SPAWN', 0);
    set('L:A32NX_AIRCRAFT_PRESET_QUICK_MODE', 1);
    for (const [channel, pot] of [['captain_pfd', 88], ['first_officer_pfd', 90]]) {
      const level = read(`a380x/xp12/du/${channel}`);
      const normalized = level > 1 ? level / 100 : level;
      set(`LIGHT POTENTIOMETER:${pot}`, Number.isFinite(normalized) ? Math.max(0, Math.min(1, normalized)) : 0);
    }
    const selectedIr = ad(`${side === 1 ? 'captain' : 'first_officer'}_ir_source`);
    const selectedAdr = ad(`${side === 1 ? 'captain' : 'first_officer'}_adr_source`);
    set('L:A32NX_ATT_HDG_SWITCHING_KNOB', selectedIr === 2 ? side === 1 ? 0 : 2 : 1);
    set('L:A32NX_AIR_DATA_SWITCHING_KNOB', selectedAdr === 2 ? side === 1 ? 0 : 2 : 1);
    for (let unit = 1; unit <= 3; unit++) {
      const ir = `L:A32NX_ADIRS_IR_${unit}_`;
      const adr = `L:A32NX_ADIRS_ADR_${unit}_`;
      for (const [out, input, validity] of [
        ['PITCH', 'pitch_deg', 'attitude_valid'], ['ROLL', 'roll_deg', 'attitude_valid'],
        ['HEADING', 'heading_deg', 'heading_valid'], ['TRACK', 'track_deg', 'nav_valid'],
        ['VERTICAL_SPEED', 'vertical_speed_fpm', 'nav_valid'], ['GROUND_SPEED', 'ground_speed_knots', 'nav_valid'],
        ['FLIGHT_PATH_ANGLE', 'flight_path_deg', 'nav_valid'],
      ]) arinc(ir + out, ad(`ir${unit}_${input}`), valid(`ir${unit}_${validity}`));
      arinc(ir + 'DRIFT_ANGLE', ad(`ir${unit}_track_deg`) - ad(`ir${unit}_heading_deg`), valid(`ir${unit}_nav_valid`));
      for (const [out, input] of [
        ['COMPUTED_AIRSPEED', 'ias_knots'], ['BARO_CORRECTED_ALTITUDE_1', 'altitude_ft'],
        ['BARO_CORRECTED_ALTITUDE_2', 'altitude_ft'], ['ALTITUDE', 'altitude_ft'], ['MACH', 'mach'],
      ]) arinc(adr + out, ad(`adr${unit}_${input}`), valid(`adr${unit}_valid`));
      arinc(adr + 'BAROMETRIC_VERTICAL_SPEED', ad(`ir${unit}_vertical_speed_fpm`), valid(`adr${unit}_valid`) && valid(`ir${unit}_nav_valid`));
      const ra = read(`sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_${unit === 2 ? 'copilot' : 'pilot'}`);
      const raFailure = read(`sim/operation/failures/rel_${unit === 2 ? 'cop' : 'pil'}_radalt`);
      arinc(`L:A32NX_RA_${unit}_RADIO_ALTITUDE`, ra, unit <= 2 && state.ok && Number.isFinite(raFailure) && raFailure < 6);
    }
    set('L:A32NX_LGCIU_1_NOSE_GEAR_COMPRESSED', read('sim/flightmodel/failures/onground_any') === 1);
    set('L:A32NX_LGCIU_1_LEFT_GEAR_COMPRESSED', read('sim/flightmodel/failures/onground_any') === 1);
    set('L:A32NX_LGCIU_1_RIGHT_GEAR_COMPRESSED', read('sim/flightmodel/failures/onground_any') === 1);
    for (const [name, value] of Object.entries(state.numbers || {})) set(name, value);
    for (const [name, word] of Object.entries(state.words || {})) arinc(name, word[0], state.ok && word[1]);
  }
  window.addEventListener('message', (event) => {
    if (event.origin === location.origin && event.source === parent && event.data?.type === 'a380x-state') apply(event.data.state);
  });
  window.__a380xTrial = { vars, reads, rawWord, apply, get state() { return current; } };
  window.__xphfbw = {
    view: side,
    screen: side === 1 ? 'SCREEN_DU_PFDL' : 'SCREEN_DU_PFDR',
    aircraftDir: 'A380X-XP12',
    snapshot() {
      if (current.ok && performance.now() - updatedAt > 1500) apply({ ok: false, mode: current.mode });
      const live = current.mode === 'live' || current.mode === 'native';
      const values = current.values || {};
      const seconds = live ? values['sim/time/total_flight_time_sec'] ?? 0 : (performance.now() - started) / 1000;
      set('E:ABSOLUTE TIME', 62135596800 + (live ? seconds : Date.now() / 1000));
      set('E:SIMULATION TIME', seconds);
      const rate = live ? values['sim/time/paused'] ? 0 : values['sim/time/sim_speed_actual'] ?? 0 : 1;
      set('E:SIMULATION RATE', rate);
      set('SIMULATION RATE', rate);
    },
    getVar(name, unit) {
      reads.set(name, unit);
      return vars.get(canonical(name)) ?? 0;
    },
    setVar(name, unit, value) { set(name, value); },
    getString(name) { return strings.get(name) ?? ''; },
    setString(name, value) { strings.set(name, String(value)); },
    sendEvent(name, values) { if (name.startsWith('H:')) pending.push(['h', name.slice(2), '']); },
    call(name) {
      if (name === 'PLAY_INSTRUMENT_SOUND') return Promise.resolve('null');
      return Promise.reject(new Error(`Trial host has no provider for ${name}`));
    },
    poll() { return pending.splice(0); },
    gameString() { return ''; },
    storedData(op, key, value) {
      if (op === 'get') return stored.get(key) ?? '';
      if (op === 'set') { stored.set(key, value); return value; }
      if (op === 'delete') { stored.delete(key); return ''; }
      if (op === 'search') return JSON.stringify([...stored].filter(([k]) => k.startsWith(key)).map(([key, data]) => ({ key, data })));
      return '';
    },
    readFile(name) {
      const pathname = new URL(name, location.href).pathname;
      if (window.__A380_NATIVE && pathname.endsWith('/PFD/bundle.js')) return window.__a380PfdSource;
      if (pathname !== '/PFD/bundle.js') return null;
      const request = new XMLHttpRequest();
      request.open('GET', pathname, false);
      request.send();
      return request.status === 200 ? request.responseText : null;
    },
    log(level, text) { if (level === 2) post({ type: 'a380x-error', text }); },
    magVar() { return 0; },
    loaded(ok, text) { post({ type: 'a380x-loaded', ok, text }); },
  };
})();
