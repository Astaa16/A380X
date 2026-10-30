// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
// Real browser interaction checks for the aircraft ND bundle. These are not
// cockpit screenshots, simulator frame-rate samples or physical mouse tests.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'build/nd-quality-20261020/close-zoom-browser-review');
fs.mkdirSync(output, { recursive: true });
const report = { scope: 'Independent browser integration, not live X-Plane', checks: [], errors: [] };
const record = (name, details) => report.checks.push({ name, passed: true, details });
let browser;
const server = spawn(process.env.PYTHON || 'python3', [path.join(__dirname, 'serve_xp_kit_nd_trial.py'), '--port', '0'],
  { cwd: root, stdio: ['ignore', 'pipe', 'pipe'] });
const ready = new Promise((resolve, reject) => {
  let received = '';
  const timeout = setTimeout(() => reject(new Error('Preview server timeout')), 30000);
  server.stdout.on('data', chunk => {
    received += chunk;
    const match = received.match(/http:\/\/127\.0\.0\.1:\d+\//);
    if (match) { clearTimeout(timeout); resolve(match[0]); }
  });
  server.once('error', reject);
  server.once('exit', code => { clearTimeout(timeout); if (code) reject(new Error(`Preview server exited ${code}`)); });
});
server.stderr.on('data', chunk => process.stderr.write(chunk));
async function main() {
  const url = await ready;
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: true });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } });
  page.on('pageerror', error => report.errors.push(error.stack));
  page.on('console', message => { if (message.type() === 'error') report.errors.push(message.text()); });
  await page.goto(url);
  await page.waitForFunction(() => window.ndTrialReport?.ready.R);
  const frame = page.frames().find(item => item.url().includes('duID=4'));
  assert.ok(frame);
  await frame.waitForFunction(() => {
    const o = window.__ndDemo?.instrument?.oansRef?.instance;
    return o?.doneDrawing && !o.dataLoading && o.dataAirportIcao.get() === 'UAAA' && o.qualityCloseMap?.profile.draws > 0;
  });
  const initial = await frame.evaluate(() => {
    const o = window.__ndDemo.instrument.oansRef.instance;
    return { ranges: o.props.zoomValues, index: o.zoomLevelIndex.get(), canvas: [o.qualityCloseCanvasRef.instance.width, o.qualityCloseCanvasRef.instance.height] };
  });
  assert.deepEqual(initial.ranges, [.05, .1, .2, .5, 1, 2, 5]);
  assert.equal(initial.index, 1);
  assert.deepEqual(initial.canvas, [768, 768]);
  record('Default apron range is 0.1 NM and close viewport is exactly 768×768', initial);
  const sourceCanvases = await frame.evaluate(() => window.__ndDemo.instrument.oansRef.instance.layerCanvasRefs.map(ref => [ref.instance.width, ref.instance.height]));
  for (let index = 6; index >= 0; index--) {
    await page.locator('article[data-side=R] [data-field=zoom]').selectOption(String(index));
    await frame.waitForFunction(index => window.__ndDemo.instrument.oansRef.instance.zoomLevelIndex.get() === index, index);
    await page.waitForTimeout(700);
    const state = await frame.evaluate(() => {
      const o = window.__ndDemo.instrument.oansRef.instance, c = o.qualityCloseCanvasRef.instance;
      return { range: o.props.zoomValues[o.zoomLevelIndex.get()], shown: getComputedStyle(c).display !== 'none',
        canvas: [c.width, c.height], sourceCanvases: o.layerCanvasRefs.map(ref => [ref.instance.width, ref.instance.height]),
        profile: { ...o.qualityCloseMap.profile } };
    });
    assert.equal(state.shown, state.range <= .2);
    assert.deepEqual(state.canvas, [768, 768]);
    assert.deepEqual(state.sourceCanvases, sourceCanvases, 'Zoom must not grow whole-airport source bitmaps');
    if (state.shown) assert.ok(state.profile.visibleFeatures > 0, 'Close viewport must contain actual airport features');
    record(`Range ${state.range} NM preserves bounded canvas dimensions`, state);
  }
  await page.locator('article[data-side=R] [data-field=zoom]').selectOption('1');
  await frame.evaluate(() => {
    const o = window.__ndDemo.instrument.oansRef.instance, context = o.qualityCloseCanvasRef.instance.getContext('2d');
    const setTransform = context.setTransform.bind(context);
    context.setTransform = function (...args) {
      if (args.length === 6) window.__closeReviewMatrix = args;
      return setTransform(...args);
    };
  });
  for (const mode of ['3', '2', '4']) {
    await page.locator('article[data-side=R] [data-field=mode]').selectOption(mode);
    await frame.waitForFunction(mode => window.__ndDemo.instrument.efisNdMode === Number(mode), mode);
    await frame.evaluate(() => {
      const o = window.__ndDemo.instrument.oansRef.instance;
      o.panOffsetX.set(37); o.panOffsetY.set(-21);
    });
    await page.waitForTimeout(1400);
    const placement = await frame.evaluate(() => {
      const o = window.__ndDemo.instrument.oansRef.instance, matrix = window.__closeReviewMatrix;
      if (!matrix) throw new Error('Viewport did not redraw after mode/pan');
      let maxError = 0;
      for (const [x, north] of [[0, 0], [123, 456], [-234, -321]]) {
        const expected = o.projectPoint([x, north]), [a, b, c, d, e, f] = matrix;
        maxError = Math.max(maxError, Math.hypot(a*x-c*north+e-expected[0], b*x-d*north+f-expected[1]));
      }
      const viewport = o.qualityCloseCanvasRef.instance, btv = o.layerCanvasScaleContainerRefs[4].instance;
      const aboveViewport = !!(viewport.compareDocumentPosition(btv) & Node.DOCUMENT_POSITION_FOLLOWING) ||
        Number(getComputedStyle(btv).zIndex) > Number(getComputedStyle(viewport).zIndex);
      return { matrix, maxError, pointerEvents: getComputedStyle(viewport).pointerEvents, btvAboveViewport: aboveViewport };
    });
    assert.ok(placement.maxError < 1e-6, `Viewport must align with labels in mode ${mode}`);
    assert.equal(placement.pointerEvents, 'none');
    assert.equal(placement.btvAboveViewport, true, 'Dynamic BTV geometry must remain above base airport');
    record(`Mode ${mode} keeps viewport geometry aligned with original labels and overlays`, placement);
  }
  await page.locator('article[data-side=R] [data-action=center]').click();
  await frame.waitForFunction(() => window.__ndDemo.instrument.oansRef.instance.panOffsetX.get() === 0);
  await page.waitForTimeout(900);
  const screen = await page.locator('#nd-R').boundingBox();
  const beforeCursor = await frame.evaluate(() => ({ ...window.__ndDemo.instrument.oansRef.instance.qualityCloseMap.profile }));
  await page.mouse.move(screen.x + screen.width*.2, screen.y + screen.height*.35);
  await page.mouse.move(screen.x + screen.width*.7, screen.y + screen.height*.55, { steps: 35 });
  await page.waitForTimeout(350);
  const afterCursor = await frame.evaluate(() => ({ ...window.__ndDemo.instrument.oansRef.instance.qualityCloseMap.profile }));
  assert.equal(afterCursor.draws, beforeCursor.draws, 'Cursor movement must not redraw static viewport geometry');
  assert.equal(afterCursor.cachedPaths, beforeCursor.cachedPaths);
  record('Moving browser pointer does not regenerate static airport geometry', { beforeCursor, afterCursor });
  await page.mouse.move(screen.x + screen.width*.45, screen.y + screen.height*.35);
  await page.mouse.down();
  await page.mouse.move(screen.x + screen.width*.6, screen.y + screen.height*.45, { steps: 12 });
  await page.mouse.up();
  await frame.waitForFunction(() => Math.abs(window.__ndDemo.instrument.oansRef.instance.panOffsetX.get()) > 5);
  record('Map remains draggable through close viewport');
  await page.locator('article[data-side=R] [data-action=center]').click();
  await frame.waitForFunction(() => window.__ndDemo.instrument.oansRef.instance.panOffsetX.get() === 0);
  // Click the actual original lower-right panel arrow through its rendered DOM.
  await frame.locator('.mfd-icon-button').evaluateAll(nodes => {
    const visible = nodes.filter(node => getComputedStyle(node).display !== 'none' && node.getBoundingClientRect().width > 0);
    const arrow = visible.find(node => /top:\s*768px/.test(node.getAttribute('style') || ''));
    if (!arrow) throw new Error('Original airport panel arrow missing');
    arrow.dataset.closeReviewPanelArrow = 'true';
  });
  await frame.locator('[data-close-review-panel-arrow=true]').click();
  await frame.waitForFunction(() => window.__ndDemo.instrument.controlPanelVisible.get());
  await frame.locator('.mfd-icon-button').evaluateAll(nodes => {
    for (const node of nodes) delete node.dataset.closeReviewPanelArrow;
    const arrow = nodes.find(node => node.getBoundingClientRect().width > 0 && /top:\s*768px/.test(node.getAttribute('style') || ''));
    if (!arrow) throw new Error('Original panel close arrow missing');
    arrow.dataset.closeReviewPanelArrow = 'true';
  });
  await frame.locator('[data-close-review-panel-arrow=true]').click();
  await frame.waitForFunction(() => !window.__ndDemo.instrument.controlPanelVisible.get());
  record('Actual original airport panel open and close buttons remain clickable');
  await page.locator('#nd-R').screenshot({ path: path.join(output, 'uAAA-apron-0.1nm-browser.png') });
  assert.deepEqual(report.errors, []);
  report.passed = true;
}
main().catch(error => { report.passed = false; report.error = error.stack; process.exitCode = 1; }).finally(async () => {
  if (browser) await browser.close();
  server.kill('SIGINT');
  fs.writeFileSync(path.join(output, 'review.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
});
