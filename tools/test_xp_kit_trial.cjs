// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { chromium } = require('playwright');

const root = path.resolve(__dirname, '..');
const output = path.join(root, 'build/xp-kit-pfd-test');
const report = { format: 1, mode: 'demo', checks: [], errors: [], failedRequests: [] };
const record = (name, details) => report.checks.push({ name, passed: true, details });
let browser;
const server = spawn(process.env.PYTHON || 'python3', [path.join(__dirname, 'serve_xp_kit_trial.py'), '--port', '0'], {
  cwd: root, stdio: ['ignore', 'pipe', 'pipe'],
});
const ready = new Promise((resolve, reject) => {
  let text = '';
  const timeout = setTimeout(() => reject(new Error('Trial server did not start')), 10000);
  server.stdout.on('data', (chunk) => {
    text += chunk;
    const match = text.match(/http:\/\/127\.0\.0\.1:\d+\//);
    if (match) { clearTimeout(timeout); resolve(match[0]); }
  });
  server.once('error', reject);
  server.once('exit', (code) => { clearTimeout(timeout); if (code) reject(new Error(`Trial server exited ${code}`)); });
});
server.stderr.on('data', (chunk) => process.stderr.write(chunk));

async function main() {
  const url = await ready;
  browser = await chromium.launch({
    ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}),
    headless: true,
  });
  const page = await browser.newPage({ viewport: { width: 1120, height: 820 } });
  page.on('pageerror', (error) => report.errors.push(error.message));
  page.on('console', (message) => { if (message.type() === 'error') report.errors.push(message.text()); });
  page.on('response', (response) => { if (!response.ok()) report.failedRequests.push([response.status(), response.url()]); });
  await page.goto(url);
  await page.waitForFunction(() => window.trialReport?.loaded[1] && window.trialReport?.loaded[2]);
  const captain = page.frames().find((frame) => frame.url().includes('duID=0'));
  const officer = page.frames().find((frame) => frame.url().includes('duID=3'));
  for (const frame of [captain, officer]) {
    await frame.waitForFunction(() => document.querySelector('a380x-pfd')?.frameCount > 20);
  }
  record('Both original FlyByWire PFDs boot and tick', await page.evaluate(() => window.trialReport.loaded));

  async function select(scene) {
    await page.locator(`[data-scene="${scene}"]`).click();
    await page.waitForFunction((selected) => window.trialReport.snapshot.scene === selected, scene);
  }
  await select('turn');
  for (const frame of [captain, officer]) {
    await frame.waitForFunction(() => document.querySelector('#RollGroup').getAttribute('transform').startsWith('rotate(-25 '));
  }
  record('25-degree bank moves both horizons in the correct direction');
  await select('climb');
  for (const frame of [captain, officer]) {
    await frame.waitForFunction(() => document.querySelector('#PitchGroup').style.transform === 'translate3d(0px, 14.4px, 0px)');
  }
  record('8-degree pitch moves the horizon using FlyByWire geometry');

  await select('failed');
  await captain.waitForFunction(() => document.querySelector('#RollGroup').style.display === 'none'
    && !document.querySelector('#FailedGroup').classList.contains('HiddenElement'));
  assert.equal(await officer.locator('#RollGroup').evaluate((element) => element.style.display), 'block');
  assert.equal(await officer.locator('#FailedGroup').evaluate((element) => element.classList.contains('HiddenElement')), true);
  record('Captain IR/ADR failure shows warnings; first officer remains valid');

  await select('level');
  await captain.waitForFunction(() => document.querySelector('#RollGroup').style.display === 'block');
  const fixture = await page.evaluate(() => structuredClone(window.trialReport.snapshot));
  fixture.values['a380x/xp12/adirs/captain_ir_source'] = 2;
  fixture.values['a380x/xp12/adirs/ir3_roll_deg'] = -15;
  await page.route('**/snapshot?**', (route) => route.fulfill({ json: fixture }));
  await captain.waitForFunction(() => document.querySelector('#RollGroup').getAttribute('transform').startsWith('rotate(15 '));
  record('Captain source-3 selection reads IR3 rather than IR1');

  fixture.values['a380x/xp12/adirs/adr1_ias_knots'] = 310;
  await captain.waitForFunction(() => document.querySelector('#SpeedTapeElementsGroup').textContent.includes('320'));
  record('Airspeed input updates the original speed-tape graduations');
  fixture.mode = 'live';
  fixture.values['sim/time/total_flight_time_sec'] = 12345;
  fixture.values['sim/time/paused'] = 1;
  await captain.waitForFunction(() => window.__a380xTrial.state.mode === 'live');
  const clock = await captain.evaluate(() => SimVar.GetSimVarValue('E:SIMULATION TIME', 'seconds'));
  await page.waitForTimeout(300);
  assert.equal(clock, 12345);
  assert.equal(await captain.evaluate(() => SimVar.GetSimVarValue('E:SIMULATION TIME', 'seconds')), clock);
  assert.equal(await captain.evaluate(() => SimVar.GetGlobalVarValue('SIMULATION RATE', 'number')), 0);
  record('Live-mode simulation clock stays paused while wall time advances');
  await page.unroute('**/snapshot?**');
  await page.waitForFunction(() => !document.querySelector('[data-scene="dark"]').disabled);

  await select('dark');
  for (const frame of [captain, officer]) {
    await frame.waitForFunction(() => document.querySelector('#PFD_CONTENT > div').style.display === 'none');
  }
  record('Loss of aircraft bus power blanks both PFDs');
  await select('level');
  for (const frame of [captain, officer]) {
    await frame.waitForFunction(() => document.querySelector('#PFD_CONTENT > div').style.display === 'block');
  }
  record('Power restoration returns both PFDs');
  await page.screenshot({ path: path.join(output, 'xp-kit-pfd-preview.png') });

  await page.route('**/snapshot?**', () => {});
  await page.waitForTimeout(1800);
  for (const frame of [captain, officer]) {
    await frame.waitForFunction(() => !window.__a380xTrial.state.ok
      && document.querySelector('#PFD_CONTENT > div').style.display === 'none');
  }
  record('Lost feed invalidates sensor words and blanks stale displays');
  const hostErrors = await page.evaluate(() => window.trialReport.errors);
  report.errors.push(...hostErrors);
  assert.deepEqual(report.failedRequests, []);
  assert.deepEqual(report.errors, []);
  const bundle = fs.readFileSync(path.join(output, 'PFD/bundle.js'), 'utf8');
  assert.equal(/\b(FSComponent|DisplayComponent|MappedSubject|ConsumerSubject|getSubscriber|whenChanged|onAfterRender|buildComponent)\b|@microsoft\/msfs-sdk|msfssdk/.test(bundle), false);
  record('No browser errors, missing assets, or MSFS SDK symbols');
  report.passed = true;
}

main().catch((error) => {
  report.passed = false;
  report.error = error.stack;
  process.exitCode = 1;
}).finally(async () => {
  if (browser) await browser.close();
  server.kill('SIGINT');
  fs.writeFileSync(path.join(output, 'browser-validation.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
});
