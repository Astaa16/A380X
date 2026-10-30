// SPDX-License-Identifier: GPL-3.0-or-later
(() => {
  const frames = [...document.querySelectorAll('iframe')];
  const status = document.getElementById('status');
  const errors = document.getElementById('errors');
  let scene = 'level';
  let live = false;
  let last = null;
  const report = { loaded: {}, errors: [], mode: 'starting', scene };
  window.trialReport = report;
  const broadcast = (state) => {
    last = state;
    for (const frame of frames) frame.contentWindow.postMessage({ type: 'a380x-state', state }, location.origin);
  };
  window.addEventListener('message', (event) => {
    if (event.origin !== location.origin || !frames.some((f) => f.contentWindow === event.source)) return;
    const data = event.data || {};
    if (data.type === 'a380x-loaded') {
      report.loaded[data.side] = data.ok;
      if (last) event.source.postMessage({ type: 'a380x-state', state: last }, location.origin);
    }
    if (data.type === 'a380x-error') {
      if (!report.errors.includes(data.text)) report.errors.push(data.text);
      errors.textContent = report.errors.slice(-6).join('\n');
    }
  });
  for (const button of document.querySelectorAll('[data-scene]')) {
    button.addEventListener('click', () => { scene = button.dataset.scene; report.scene = scene; poll(); });
  }
  let busy = false;
  async function poll() {
    if (busy) return;
    busy = true;
    try {
      const response = await fetch(`/snapshot?scene=${encodeURIComponent(scene)}`, { cache: 'no-store' });
      const state = await response.json();
      live = state.mode === 'live';
      report.mode = state.mode;
      report.snapshot = state;
      status.textContent = state.ok
        ? live ? `LIVE · ${state.aircraft} · read only` : `DEMO DATA · ${scene}`
        : `NO LIVE DATA · ${state.error}`;
      status.style.color = state.ok ? live ? '#7dddc1' : '#f6d37b' : '#ffb1a8';
      for (const button of document.querySelectorAll('[data-scene]')) button.disabled = live;
      broadcast(state);
    } catch (error) {
      status.textContent = `Disconnected · ${error.message}`;
      broadcast({ ok: false, mode: live ? 'live' : 'demo', error: error.message });
    } finally { busy = false; }
  }
  poll();
  setInterval(poll, 250);
})();
