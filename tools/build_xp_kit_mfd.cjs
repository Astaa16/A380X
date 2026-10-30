// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const source = process.env.FBW_SOURCE || '/Users/sava/Downloads/Flybywire-A380X-for-Xplane-12-main';
const toolchain = process.env.ND_TOOLCHAIN || '/Users/sava/.cache/a380-nd-preview-tools-20261016';
const output = path.join(root, 'build/xp-kit-mfd-native');
const library = path.join(source, 'xp-kit/dist/index.js');
const components = path.join(source, 'xp-kit/src/components');
const sha = (p) => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
async function main() {
  fs.mkdirSync(output, { recursive: true });
  await require(path.join(toolchain, 'node_modules/esbuild')).build({
    entryPoints: [path.join(root, 'native/avionics/xp-kit-mfd/instrument.js')],
    outfile: path.join(output, 'bundle.js'), bundle: true, format: 'iife', target: 'es2020',
    // Import the real component-only entry point. The monolithic dist also
    // evaluates MSFS GaugeBase, which would require an unused BaseInstrument.
    alias: { 'xp-kit': path.join(components, 'index.ts') }, metafile: true,
  });
  fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify({
    format: 1, renderer: 'xp-kit Widget / WebKit Canvas2D',
    scope: 'Native aircraft MFD page ports; not the upstream JavaScript FMC',
    library_sha256: sha(library), bundle_sha256: sha(path.join(output, 'bundle.js')),
    component_sources: Object.fromEntries(fs.readdirSync(components).filter((n) => n.endsWith('.ts')).map((n) => [n, sha(path.join(components, n))])),
    resolution: [768, 1024], transport_version: 3, browser_server_required: false,
    controller: 'a380x_mfd + a380x_fms + a380x_fms_fpln; one native FMS shared by both sides',
    upstream_templates: 'Translated native page modules documented in docs/mfd-spec; not recompiled TSX pages',
  }, null, 2) + '\n');
  console.log('Built native MFD renderer through xp-kit:', output);
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
