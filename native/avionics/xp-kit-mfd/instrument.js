// SPDX-License-Identifier: GPL-3.0-or-later
import { Kit, Widget } from 'xp-kit';
import { drawFrame, validateFrame } from './draw.js';

const side = new URLSearchParams(location.search).get('index') === '2' ? 2 : 1;
const post = (message) => window.webkit?.messageHandlers.a380.postMessage({ ...message, side });
class NativeMfd extends Widget {
  canvas = Kit.createRef();
  frame = null;
  live = false;
  freshAt = 0;
  mounted() {
    const ctx = this.canvas.instance.getContext('2d', { alpha: false });
    const paint = () => drawFrame(ctx, this.live && this.frame ? this.frame : {
      sequence: 0, uri: '', powered: false, commands: [],
    });
    window.__a380Mfd = {
      apply: (frame) => { this.frame = validateFrame(frame); paint(); },
      get frame() { return window.__mfdWidget.frame; },
    };
    window.__mfdWidget = this;
    window.__a380xTrial = { apply: (state) => {
      const changed = this.live !== !!state.ok;
      this.live = !!state.ok; this.freshAt = performance.now();
      if (changed) paint();
    } };
    window.__a380NativeFrame = () => {
      if (this.live && performance.now() - this.freshAt > 1500) { this.live = false; paint(); }
    };
    paint();
    Promise.all([document.fonts.load('27px A380Slashed'), document.fonts.load('27px A380EIS')])
      .then(() => { paint(); post({ type: 'a380x-loaded', ok: true, text: 'Native MFD / xp-kit renderer' }); })
      .catch((error) => post({ type: 'a380x-error', text: String(error) }));
  }
  render() { return Kit.h('canvas', { width: 768, height: 1024, ref: this.canvas, id: 'MFD_CONTENT' }); }
}
Kit.render(Kit.h(NativeMfd, {}), document.body);
