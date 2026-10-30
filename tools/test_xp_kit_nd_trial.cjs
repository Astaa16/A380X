// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'build/xp-kit-nd-test');
const report = { format: 1, scope: 'browser review only', checks: [], errors: [], failedRequests: [], externalRequests: [] };
const record = (name, details) => report.checks.push({ name, passed: true, details });
let browser;
const server = spawn(process.env.PYTHON || 'python3', [path.join(__dirname, 'serve_xp_kit_nd_trial.py'), '--port', '0'], {
  cwd: root, stdio: ['ignore', 'pipe', 'pipe'],
});
const ready = new Promise((resolve, reject) => {
  let text = '';
  const timeout = setTimeout(() => reject(new Error('Preview server did not start')), 30000);
  server.stdout.on('data', (chunk) => {
    text += chunk;
    const match = text.match(/http:\/\/127\.0\.0\.1:\d+\//);
    if (match) { clearTimeout(timeout); resolve(match[0]); }
  });
  server.once('error', reject);
  server.once('exit', (code) => { clearTimeout(timeout); if (code) reject(new Error(`Preview server exited ${code}`)); });
});
server.stderr.on('data', (chunk) => process.stderr.write(chunk));
async function main() {
  const url = await ready;
  browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: true });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1100 } });
  const error = (text) => { if (!report.errors.includes(text)) report.errors.push(text); };
  page.on('pageerror', (event) => error(event.stack));
  page.on('console', (message) => { if (message.type() === 'error') error(message.text()); });
  page.on('response', (response) => { if (!response.ok()) report.failedRequests.push([response.status(), response.url()]); });
  page.on('request', (request) => { if (/^https?:/.test(request.url()) && !request.url().startsWith(url)) report.externalRequests.push(request.url()); });
  await page.goto(url);
  await page.waitForFunction(() => window.ndTrialReport?.ready.L && window.ndTrialReport?.ready.R);
  const nd = page.frames().find((frame) => frame.url().includes('duID=1'));
  const oans = page.frames().find((frame) => frame.url().includes('duID=4'));
  for (const frame of [nd, oans]) await frame.waitForFunction(() => document.querySelector('a380x-nd')?.frameCount > 30);
  await oans.waitForFunction(() => {
    const o = window.__ndDemo.instrument.oansRef.instance;
    return !o.dataLoading && o.dataAirportIcao.get() === 'UAAA' && o.data?.features.length > 100;
  });
  record('Original FlyByWire ND and OANC boot through xp-kit', await oans.evaluate(() => ({
    airport: window.__ndDemo.instrument.oansRef.instance.dataAirportIcao.get(),
    features: window.__ndDemo.instrument.oansRef.instance.data.features.length,
  })));
  await nd.waitForFunction(() => document.body.textContent.includes('DEMO1'));
  for (const mode of ['2', '4', '1', '0', '3']) {
    await page.locator('article[data-side=L] [data-field=mode]').selectOption(mode);
    await nd.waitForFunction((m) => window.__ndDemo.instrument.efisNdMode === Number(m), mode);
  }
  record('ARC, ROSE NAV, PLAN, VOR and LS modes respond');
  for (const [index, nm] of [[1,10],[2,20],[3,40],[4,80],[5,160],[6,320],[7,640],[3,40]]) {
    await page.locator('article[data-side=L] [data-field=range]').selectOption(String(index));
    await nd.waitForFunction((n) => window.__ndDemo.instrument.efisCpRange === n, nm);
  }
  record('All seven ND ranges respond: 10–640 NM');
  await page.locator('article[data-side=L] [data-field=heading]').fill('180');
  await nd.waitForFunction(() => SimVar.GetSimVarValue('L:A32NX_ADIRS_IR_1_HEADING','number') === window.__ndDemo.raw(180));
  await page.locator('article[data-side=L] [data-field=wind]').fill('56');
  await nd.waitForFunction(() => document.body.textContent.includes('56'));
  assert.equal(await oans.evaluate(() => window.__ndDemo.state.heading), 55);
  record('Heading/wind fixtures change captain only');
  await page.locator('article[data-side=L] [data-action=failure]').click();
  await nd.waitForFunction(() => window.__ndDemo.state.failed);
  assert.equal(await oans.evaluate(() => window.__ndDemo.state.failed), false);
  await nd.waitForFunction(() => [...document.querySelectorAll('text')].some(e =>
    e.textContent === 'HDG' && getComputedStyle(e).visibility !== 'hidden'));
  await page.locator('article[data-side=L] [data-action=failure]').click();
  record('Captain IR failure produces HDG flag; OANS input remains valid');
  for (const [side, frame, other] of [['L',nd,oans],['R',oans,nd]]) {
    await page.locator(`article[data-side=${side}] [data-action=power]`).click();
    await frame.waitForFunction(() => document.querySelector('.oanc-container').parentElement.style.display === 'none');
    assert.equal(await other.locator('.oanc-container').evaluate((e) => e.parentElement.style.display), 'block');
    await page.locator(`article[data-side=${side}] [data-action=power]`).click();
    await frame.waitForFunction(() => document.querySelector('.oanc-container').parentElement.style.display === 'block');
  }
  record('Independent loss/restoration of each display power blanks/restores the original display');
  for (const zoom of ['0','2','3','4','5','6','1']) {
    await page.locator('article[data-side=R] [data-field=zoom]').selectOption(zoom);
    await oans.waitForFunction((z) => window.__ndDemo.instrument.oansRef.instance.zoomLevelIndex.get() === Number(z), zoom);
  }
  for (const mode of ['3','2','4']) {
    await page.locator('article[data-side=R] [data-field=mode]').selectOption(mode);
    await oans.waitForFunction((m) => window.__ndDemo.instrument.efisNdMode === Number(m), mode);
  }
  record('OANS zoom 0.05–5 NM and PLAN/ARC/ROSE orientations respond');
  await page.waitForTimeout(700);
  const screen = await page.locator('#nd-R').boundingBox();
  await page.mouse.move(screen.x + screen.width*.45, screen.y + screen.height*.35);
  await page.mouse.down();
  await page.mouse.move(screen.x + screen.width*.55, screen.y + screen.height*.4, { steps: 8 });
  await page.mouse.up();
  await oans.waitForFunction(() => Math.abs(window.__ndDemo.instrument.oansRef.instance.panOffsetX.get()) > 5);
  await page.locator('article[data-side=R] [data-action=center]').click();
  await oans.waitForFunction(() => window.__ndDemo.instrument.oansRef.instance.panOffsetX.get() === 0);
  record('Drag-to-pan and center-on-aircraft work through the original OANC');
  await page.mouse.dblclick(screen.x + screen.width*.45, screen.y + screen.height*.35);
  await oans.locator('#contextMenu_1').click();
  await oans.waitForFunction(() => window.__ndDemo.instrument.oansRef.instance.markerManager.flags.items().length === 1);
  await page.locator('article[data-side=R] [data-action=erase]').click();
  await oans.waitForFunction(() => window.__ndDemo.instrument.oansRef.instance.markerManager.flags.items().length === 0);
  record('Original context menu adds a flag and Erase markers removes it');
  await page.locator('article[data-side=R] [data-action=panel]').click();
  await oans.waitForFunction(() => window.__ndDemo.instrument.controlPanelVisible.get());
  await oans.waitForFunction(() => window.__ndDemo.instrument.controlPanelRef.instance.activeTabIndex.get() === 0
    && window.__ndDemo.instrument.controlPanelRef.instance.availableEntityList.items().length >= 4);
  await page.locator('#nd-R').screenshot({ path: path.join(output, 'oans-map-data.png') });
  await page.locator('article[data-side=R] [data-action=panel]').click();
  record('Original FlyByWire airport control panel opens/closes');
  await page.locator('#airport').selectOption('EGLL');
  await oans.waitForFunction(() => {
    const o = window.__ndDemo.instrument.oansRef.instance;
    return !o.dataLoading && o.dataAirportIcao.get() === 'EGLL' && o.data?.features.length > 100;
  });
  record('Heathrow map replaces Almaty using local scenery geometry');
  await page.locator('#airport').selectOption('UAAA');
  await oans.waitForFunction(() => !window.__ndDemo.instrument.oansRef.instance.dataLoading && window.__ndDemo.instrument.oansRef.instance.dataAirportIcao.get() === 'UAAA');
  await page.locator('article[data-side=L] [data-field=heading]').fill('55');
  await page.locator('article[data-side=L] [data-field=wind]').fill('24');
  await page.waitForTimeout(900);
  await page.screenshot({ path: path.join(output, 'nd-oans-preview.png'), fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.waitForTimeout(300);
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
  await page.screenshot({ path: path.join(output, 'nd-oans-mobile.png'), fullPage: true });
  record('Desktop and narrow layouts render without horizontal overflow');
  report.errors.push(...await page.evaluate(() => window.ndTrialReport.errors));
  assert.deepEqual(report.errors, []);
  assert.deepEqual(report.failedRequests, []);
  assert.deepEqual(report.externalRequests, []);
  record('No JavaScript errors, missing assets or external network calls');
  report.passed = true;
}
main().catch((error) => { report.passed = false; report.error = error.stack; process.exitCode = 1; }).finally(async () => {
  if (browser) await browser.close();
  server.kill('SIGINT');
  fs.writeFileSync(path.join(output, 'browser-validation.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
});
