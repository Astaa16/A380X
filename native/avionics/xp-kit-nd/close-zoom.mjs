// SPDX-License-Identifier: GPL-3.0-or-later
// A close airport view is rendered at display resolution from cached geometry,
// rather than enlarging the original one-pixel-per-metre whole-airport canvases.
export const OANS_RANGES_NM = Object.freeze([0.05, 0.1, 0.2, 0.5, 1, 2, 5]);
export const OANS_DEFAULT_ZOOM = 1;

export function oansLayerVisible(layer, range) {
  return layer === 0 ? range <= 0.5 : layer === 2 ? range >= 1 : layer === 3 ? range <= 0.2 : true;
}

export function oansViewportMatrix(projectPoint) {
  const origin = projectPoint([0, 0]);
  const x = projectPoint([1, 0]);
  const y = projectPoint([0, -1]);
  return [x[0] - origin[0], x[1] - origin[1], y[0] - origin[0], y[1] - origin[1], ...origin];
}

export function oansBoundsVisible(bounds, matrix, size = 768, margin = 16) {
  const [a, b, c, d, e, f] = matrix;
  const points = [[bounds[0], bounds[1]], [bounds[0], bounds[3]], [bounds[2], bounds[1]], [bounds[2], bounds[3]]];
  const xs = points.map(([x, y]) => a*x+c*y+e), ys = points.map(([x, y]) => b*x+d*y+f);
  return Math.max(...xs) >= -margin && Math.min(...xs) <= size+margin &&
    Math.max(...ys) >= -margin && Math.min(...ys) <= size+margin;
}

export function createOansCloseViewport() {
  let cache = new WeakMap(), previousData, signature = '';
  const profile = {draws: 0, cachedPaths: 0, visibleFeatures: 0, drawMs: 0};
  function geometry(feature) {
    let item = cache.get(feature);
    if (item) return item;
    const path = new Path2D(), bounds = [Infinity, Infinity, -Infinity, -Infinity];
    const line = (coordinates, closed) => {
      coordinates.forEach(([x, north], index) => {
        const y = -north;
        if (index) path.lineTo(x, y); else path.moveTo(x, y);
        bounds[0] = Math.min(bounds[0], x); bounds[1] = Math.min(bounds[1], y);
        bounds[2] = Math.max(bounds[2], x); bounds[3] = Math.max(bounds[3], y);
      });
      if (closed) path.closePath();
    };
    const shape = feature.geometry;
    if (shape.type === 'LineString') line(shape.coordinates, false);
    else if (shape.type === 'Polygon') shape.coordinates.forEach(ring => line(ring, true));
    else if (shape.type === 'MultiPolygon') shape.coordinates.forEach(polygon => polygon.forEach(ring => line(ring, true)));
    item = {path, bounds}; cache.set(feature, item); profile.cachedPaths++;
    return item;
  }
  return {profile, update(canvas, oanc, specs) {
    const range = oanc.props.zoomValues[oanc.zoomLevelIndex.get()];
    const enabled = range <= 0.2 && !!oanc.data;
    canvas.style.display = enabled ? 'block' : 'none';
    // Preserve the dynamic BTV layer, its original transforms, and every input target.
    for (let layer = 0; layer < 4; layer++) {
      oanc.layerCanvasScaleContainerRefs[layer].instance.style.visibility =
        enabled ? 'hidden' : oansLayerVisible(layer, range) ? 'inherit' : 'hidden';
    }
    if (!enabled) { signature = ''; return; }
    if (previousData !== oanc.data) { cache = new WeakMap(); previousData = oanc.data; signature = ''; profile.cachedPaths = 0; }
    const matrix = oansViewportMatrix(point => oanc.projectPoint(point));
    const next = `${range}|${matrix.join(',')}|${oanc.layerFeatures.map(layer => layer.features.length).join(',')}`;
    if (next === signature) return;
    signature = next;
    const start = performance.now(), context = canvas.getContext('2d');
    context.resetTransform(); context.clearRect(0, 0, 768, 768);
    context.setTransform(...matrix); context.lineJoin = 'round'; context.lineCap = 'round';
    profile.visibleFeatures = 0;
    for (let layer = 0; layer < 4; layer++) {
      if (!oansLayerVisible(layer, range)) continue;
      for (const feature of oanc.layerFeatures[layer].features) {
        const rule = specs[layer].styleRules.find(rule =>
          (!rule.forFeatureTypes || rule.forFeatureTypes.includes(feature.properties.feattype)) &&
          (!rule.forPolygonStructureTypes || rule.forPolygonStructureTypes.includes(feature.properties.plysttyp)));
        if (!rule) continue;
        const item = geometry(feature);
        if (!oansBoundsVisible(item.bounds, matrix)) continue;
        const style = rule.styles;
        if (style.doFill) { context.fillStyle = style.fillStyle; context.fill(item.path, 'evenodd'); }
        if (style.doStroke) {
          context.strokeStyle = style.strokeStyle;
          // Keep source proportions, with a screen-space cap so paint does not
          // obscure adjacent stand geometry at the new 0.05 NM setting.
          context.lineWidth = Math.min(style.lineWidth ?? 1, 2.5/Math.hypot(matrix[0], matrix[1]));
          context.stroke(item.path);
        }
        profile.visibleFeatures++;
      }
    }
    context.resetTransform(); profile.draws++; profile.drawMs = performance.now()-start;
  }};
}
