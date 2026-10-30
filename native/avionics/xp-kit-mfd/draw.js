// SPDX-License-Identifier: GPL-3.0-or-later
// Raster backend for the existing FlyByWire-derived native MFD page ports.
// No FMS logic, entry parsing, navigation decisions or simulator writes here.
export function validateFrame(frame) {
  if (!frame || !Array.isArray(frame.commands) || frame.commands.length > 4096 ||
      typeof frame.uri !== 'string' || frame.uri.length >= 64 || !Number.isSafeInteger(frame.sequence)) {
    throw new Error('Invalid MFD frame');
  }
  for (const c of frame.commands) {
    if (!Array.isArray(c) || c.length !== 14 || !c.slice(0, 10).every(Number.isFinite) ||
        !Number.isInteger(c[0]) || c[0] < 0 || c[0] > 6 || c[1] < 0 || c[1] > 0xffffff ||
        !Array.isArray(c[11]) || c[11].length !== 4 || !c[11].every(Number.isFinite) ||
        !Number.isFinite(c[12]) || !Number.isFinite(c[13])) throw new Error('Invalid MFD command');
    if (c[0] === 6 && (typeof c[10] !== 'string' || c[10].length > 32768)) throw new Error('Invalid MFD text');
    if (c[0] >= 3 && c[0] <= 5 && (!Array.isArray(c[10]) || c[10].length > 32768 ||
        c[10].length % 2 || !c[10].every(Number.isFinite))) throw new Error('Invalid MFD path');
  }
  return frame;
}

export function drawFrame(ctx, frame) {
  validateFrame(frame);
  ctx.save();
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, 768, 1024);
  if (!frame.powered) { ctx.restore(); return; }
  // Stable sort: same-layer commands retain the native widget emission order.
  for (const c of [...frame.commands].sort((a, b) => a[12] - b[12])) {
    const [kind, rgb, font, anchor, width, size, x, y, w, h, payload, clip] = c;
    if (clip[2] <= 0 || clip[3] <= 0) continue;
    ctx.save();
    ctx.beginPath(); ctx.rect(...clip); ctx.clip();
    ctx.fillStyle = ctx.strokeStyle = '#' + rgb.toString(16).padStart(6, '0');
    ctx.lineWidth = width;
    ctx.lineCap = 'butt'; ctx.lineJoin = 'miter';
    if (kind === 0) ctx.fillRect(x, y, w, h);
    else if (kind === 1 && width > 0) ctx.strokeRect(x + width / 2, y + width / 2, Math.max(0, w - width), Math.max(0, h - width));
    else if (kind === 2 && width > 0) {
      ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(w, h); ctx.stroke();
    } else if (kind >= 3 && kind <= 5 && payload.length >= 4) {
      ctx.beginPath(); ctx.moveTo(payload[0], payload[1]);
      for (let p = 2; p < payload.length; p += 2) ctx.lineTo(payload[p], payload[p + 1]);
      if (kind !== 3 || c[13]) ctx.closePath();
      if (kind === 3) { if (width > 0) ctx.stroke(); } else ctx.fill();
    } else if (kind === 6 && size > 0) {
      ctx.font = `${size}px ${font === 1 ? 'A380EIS' : 'A380Slashed'}`;
      ctx.textBaseline = 'alphabetic';
      ctx.textAlign = ['left', 'center', 'right'][anchor] || 'left';
      ctx.fillText(payload, x, y);
    }
    ctx.restore();
  }
  ctx.restore();
}
