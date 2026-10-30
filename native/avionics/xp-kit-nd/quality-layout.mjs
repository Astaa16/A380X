// SPDX-License-Identifier: GPL-3.0-or-later
// Screen-space label placement. Geometry and navigation data are never removed.
const intersects = (a, b) => a[0] < b[2] && a[2] > b[0] && a[1] < b[3] && a[3] > b[1];

export function layoutNdLabels(items, width = 768, height = 768) {
  const result = new Map(), buckets = new Map(), cell = 64;
  const cells = (r) => {
    const keys = [];
    for (let x = Math.floor(r[0] / cell); x <= Math.floor(r[2] / cell); x++)
      for (let y = Math.floor(r[1] / cell); y <= Math.floor(r[3] / cell); y++) keys.push(`${x},${y}`);
    return keys;
  };
  const collision = (r) => {
    const seen = new Set();
    for (const key of cells(r)) for (const box of buckets.get(key) || []) {
      if (!seen.has(box) && intersects(r, box)) return true;
      seen.add(box);
    }
    return false;
  };
  // Reserve every visible navigation symbol before placing text, so a moved
  // label cannot obscure a neighbouring fix, airport or navaid glyph.
  for (const item of items) if ([item.x,item.y].every(Number.isFinite) &&
      item.x>=0 && item.x<=width && item.y>=0 && item.y<=height) {
    const r=item.symbolRadius ?? 16, box=[item.x-r,item.y-r,item.x+r,item.y+r];
    for (const key of cells(box)) { if(!buckets.has(key)) buckets.set(key,[]); buckets.get(key).push(box); }
  }
  for (const item of [...items].sort((a, b) => b.priority - a.priority || a.order - b.order)) {
    if (![item.x, item.y, item.width].every(Number.isFinite) || item.width <= 0 ||
        item.x < -30 || item.x > width + 30 || item.y < -30 || item.y > height + 30) {
      result.set(item.key, null); continue;
    }
    const h = item.height || 22, below=item.below || 0, pad = 4, w = Math.min(item.width, width - 24);
    const candidates = item.fixed ? [[item.x, item.y]] :
      [[item.x + 20, item.y + 17], [item.x + 20, item.y - 10],
       [item.x - w - 20, item.y + 17], [item.x - w - 20, item.y - 10],
       [item.x - w / 2, item.y - 23], [item.x - w / 2, item.y + 39]];
    let chosen = null;
    for (const [x, y] of candidates) {
      const box = [x - pad, y - h - pad, x + w + pad, y + below + pad];
      if (box[0] < 4 || box[2] > width - 4 || box[1] < 4 || box[3] > height - 4) continue;
      if (!collision(box)) { chosen = { x, y, box }; break; }
    }
    // Essential active-route labels get a bounded last resort position;
    // optional database labels yield first. No symbol or route is suppressed.
    if (!chosen && item.required) {
      const x = Math.max(8, Math.min(width - w - 8, item.x + 20));
      const y = Math.max(h + 8, Math.min(height - below - 8, item.y + 17));
      chosen = { x, y, box: [x-pad, y-h-pad, x+w+pad, y+below+pad] };
    }
    result.set(item.key, chosen ? { x: chosen.x, y: chosen.y } : null);
    if (chosen) for (const key of cells(chosen.box)) {
      if (!buckets.has(key)) buckets.set(key, []);
      buckets.get(key).push(chosen.box);
    }
  }
  return result;
}

export function declutterOansLabels(elements) {
  const boxes = [];
  const priority = (label) => /symbol|arrow|stop-line/.test(label.style) ? 120 : /btv-selected|fms-selected/.test(label.style) ? 100 :
    /runway/.test(label.style) ? 80 : /taxiway|exit-line/.test(label.style) ? 40 : 20;
  const entries = [...elements.entries()].sort((a, b) => priority(b[0]) - priority(a[0]));
  for (const [label, element] of entries) {
    if (element.style.visibility === 'hidden') continue;
    const x = parseFloat(element.style.left), y = parseFloat(element.style.top);
    if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
    const runway = /runway-end/.test(label.style), axis = /runway-axis/.test(label.style), marker=/symbol|arrow|stop-line/.test(label.style);
    const w = marker ? (/arrow/.test(label.style)?50:35) : runway ? 64 : Math.max(18, String(label.text || '').length * 12 + 6);
    const h = marker ? 35 : runway ? 47 : axis ? 25 : 23;
    const rotation=element.style.transform?.match(/rotate\(([-\d.]+)deg\)/);
    const offset=element.style.transform?.match(/translate\(0px,\s*([-\d.]+)px\)/);
    const a=rotation ? Number(rotation[1])*Math.PI/180 : 0, dy=offset ? Number(offset[1]) : 0;
    const cx=x-Math.sin(a)*dy, cy=y+Math.cos(a)*dy;
    const bw=Math.abs(Math.cos(a))*w+Math.abs(Math.sin(a))*h;
    const bh=Math.abs(Math.sin(a))*w+Math.abs(Math.cos(a))*h;
    const box = [cx-bw/2-3, cy-bh/2-3, cx+bw/2+3, cy+bh/2+3];
    const required = priority(label) >= 80;
    if (!required && boxes.some((other) => intersects(box, other))) {
      element.style.visibility = 'hidden';
    } else boxes.push(box);
  }
}
