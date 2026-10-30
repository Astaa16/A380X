// SPDX-License-Identifier: GPL-3.0-or-later
// Offline renderer QA only: no simulator writes or invented live flight plan.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const { chromium } = require('playwright');
async function main() {
  const [assets, fixtures, output] = process.argv.slice(2).map((p) => path.resolve(p));
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: true });
  try {
  const page = await browser.newPage({ viewport: { width: 768, height: 1024 } });
  const errors = [], external = [], checks = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('request', (r) => { if (/^https?:/.test(r.url())) external.push(r.url()); });
  await page.goto('file://' + path.join(assets, 'mfd.html'));
  await page.waitForFunction(() => !!window.__a380Mfd);
  await page.evaluate(() => document.fonts.ready);
  for (const row of JSON.parse(fs.readFileSync(path.join(fixtures, 'index.json')))) {
    const frame = JSON.parse(fs.readFileSync(path.join(fixtures, row.file)));
    await page.evaluate((data) => { window.__a380xTrial.apply({ ok: true }); window.__a380Mfd.apply(data); }, frame);
    const lit = await page.evaluate(() => {
      const data = document.querySelector('canvas').getContext('2d').getImageData(0, 0, 768, 1024).data;
      let count = 0; for (let i = 0; i < data.length; i += 4) if (Math.max(data[i], data[i + 1], data[i + 2]) > 30) ++count;
      return count;
    });
    assert(lit > 2000, row.file + ' is blank');
    if (/-perf_to_filled\.json$|-fpln_full\.json$|-init_filled\.json$|-fcub_afs\.json$/.test(row.file))
      await page.screenshot({ path: path.join(output, row.file.replace('.json', '.png')) });
    checks.push({ ...row, lit_pixels: lit, passed: true });
  }
  await page.evaluate(() => window.__a380xTrial.apply({ ok: false }));
  assert.equal(await page.evaluate(() => document.querySelector('canvas').getContext('2d').getImageData(30, 30, 1, 1).data[0]), 0);
  assert.deepEqual(errors, []); assert.deepEqual(external, []);
  const report = { passed: true, scope: 'offline real native-page fixtures, not live flight data', checks, errors, external_requests: external };
  fs.writeFileSync(path.join(output, 'renderer-validation.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ passed: true, rendered_states: checks.length, unique_routes: [...new Set(checks.map((r) => r.uri))].length, errors }));
  } finally { await browser.close(); }
}
main().catch((e) => { console.error(e); process.exitCode = 1; });
