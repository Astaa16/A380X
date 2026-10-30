// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const root = path.resolve(__dirname, '..');
const source = process.env.FBW_SOURCE || '/Users/sava/Downloads/Flybywire-A380X-for-Xplane-12-main';
const upstream = path.join(source, 'fbw/xplane');
const aircraft = path.join(upstream, 'fbw-a380x');
const native = process.env.ND_NATIVE === '1';
const output = path.join(root, native ? 'build/xp-kit-nd-native' : 'build/xp-kit-nd-test');
const toolchain = process.env.ND_TOOLCHAIN || '/Users/sava/.cache/a380-nd-preview-tools-20261016';
process.env.NODE_PATH = path.join(toolchain, 'node_modules');
require('node:module').Module._initPaths();
process.env.XP_KIT = path.join(source, 'xp-kit/dist');
process.env.FBW_TYPECHECK = '0';
fs.mkdirSync(path.join(output, 'ND'), { recursive: true });
const hash = (file) => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
async function main() {
  process.chdir(aircraft);
  const config = require(path.join(aircraft, 'mach.config.js'));
  const transform = require(path.join(source, 'xp-kit/tools/transform.cjs'));
  const replaceOnce = (text, before, after) => {
    if (text.split(before).length !== 2) throw new Error(`Preview adapter must match exactly once: ${before}`);
    return text.replace(before, after);
  };
  const demoAdapter = { name: 'nd-browser-review-boundary', setup(build) {
    build.onResolve({ filter: /NavigraphAmdbClient$/ }, () => ({ path: path.join(root, 'native/avionics/xp-kit-nd', native ? 'native-amdb.js' : 'preview-amdb.js') }));
    build.onLoad({ filter: /\/ND\/instrument\.tsx$/ }, (args) => {
      let text = fs.readFileSync(args.path, 'utf8');
      const marker = 'this.backplane.onUpdate();';
      text = replaceOnce(text, marker, marker + '\n globalThis.' + (native ? '__a380Nd' : '__ndDemo') + '?.onUpdate(this.bus, this);');
      text = replaceOnce(text, 'color={this.oansShown.map((it) => (it ? MouseCursorColor.Magenta : MouseCursorColor.Yellow))}', 'color={MouseCursorColor.Yellow}');
      text = replaceOnce(text,'private cursorVisible = Subject.create<boolean>(true);','private cursorVisible = Subject.create<boolean>(false);');
      text = replaceOnce(text,"this.topRef.instance.addEventListener('mousemove', (ev) => {",
        "this.topRef.instance.addEventListener('mouseleave', () => this.cursorVisible.set(false));\n    this.topRef.instance.addEventListener('mousemove', (ev) => {");
      return { contents: transform.transform(text, args.path), loader: 'tsx', resolveDir: path.dirname(args.path) };
    });
    build.onLoad({ filter: /\/(ArcModeUnderlay|RoseModeUnderlay|OancRoseCompass|OancPlanModeCompass|OancArcModeCompass|OansControlPanel)\.tsx$/ }, (args) => {
      let text = fs.readFileSync(args.path, 'utf8');
      if (args.path.endsWith('OansControlPanel.tsx')) {
        text = replaceOnce(text, "'FBW9027250BB04'", "'LOCAL SCENERY'");
        text = replaceOnce(text, "'cross' : 'flag'}.svg", "'cross' : 'flag'}.png");
        if (native) {
          const ident = /NavigationDatabaseService\.activeDatabase\.getDatabaseIdent\(\)\.then\(\(db\) => \{[\s\S]*?\n    \}\);/g;
          if ([...text.matchAll(ident)].length !== 1) throw new Error('Native database identity adapter mismatch');
          text = text.replace(ident, "this.activeDatabase.set('LOCAL SCENERY');");
        }
      } else {
        if (/\/Oanc(RoseCompass|PlanModeCompass)\.tsx$/.test(args.path)) {
          text=replaceOnce(text,'((rng / 2).toString().length === 4 ? 55 : 41)',
            'Math.max(41, (rng / 2).toString().length * 14)');
        }
        // A no-op CSS transform is not a valid SVG transform attribute in Chromium/WebKit.
        const count = args.path.endsWith('/OancPlanModeCompass.tsx') ? 0 : args.path.includes('/OANC/') ? 2 : 1;
        if (text.split('transform="rotateX(0deg)"').length !== count+1) throw new Error('Unexpected no-op SVG transform count');
        text = text.replaceAll('transform="rotateX(0deg)"', 'transform="rotate(0)"');
      }
      return { contents: transform.transform(text, args.path), loader: 'tsx', resolveDir: path.dirname(args.path) };
    });
  } };
  const qualityAdapter = require('./nd_quality_adapter.cjs')(root,transform,replaceOnce);
  const cursorAdapter = {name:'nd-yellow-map-cursor',setup(build) {
    build.onLoad({filter:/\/UiWidgets\/MouseCursor\.tsx$/},(args)=>{
      let text=fs.readFileSync(args.path,'utf8');
      text=replaceOnce(text,'  show() {\n    this.hide();\n  }',"  show() {\n    this.divRef.instance.style.display = 'block';\n  }");
      text=replaceOnce(text,'  updatePosition(x: number, y: number) {','  updatePosition(x: number, y: number) {\n    this.show();');
      return {contents:transform.transform(text,args.path),loader:'tsx',resolveDir:path.dirname(args.path)};
    });
    build.onLoad({filter:/\/ND\/pages\/plan\/index\.tsx$/},(args)=>{
      let text=fs.readFileSync(args.path,'utf8');
      text=replaceOnce(text,'private handleMovePlane() {\n    if (this.isVisible.get()) {',
        'private handleMovePlane() {\n    if (this.isVisible.get() && this.mapParams.valid) {');
      return {contents:transform.transform(text,args.path),loader:'tsx',resolveDir:path.dirname(args.path)};
    });
  }};
  const result = await require('esbuild').build({
    absWorkingDir: aircraft, entryPoints: ['src/systems/instruments/src/ND/instrument.tsx'],
    outfile: path.join(output, 'ND/bundle.js'), bundle: true, format: 'iife',
    target: 'es2020', logLevel: 'silent', tsconfig: path.join(aircraft, 'src/systems/instruments/src/ND/tsconfig.json'),
    nodePaths: [path.join(toolchain, 'node_modules')], plugins: [demoAdapter, cursorAdapter, qualityAdapter, ...config.plugins],
    external: ['/Images/*', '/Fonts/*'],
    define: { DEBUG: 'false', 'process.env.NODE_ENV': '"development"' }, metafile: true,
  });
  fs.appendFileSync(path.join(output,'ND/bundle.css'),'\n'+fs.readFileSync(path.join(root,'native/avionics/xp-kit-nd/quality.css'),'utf8'));
  fs.writeFileSync(path.join(output, 'ND/metafile.json'), JSON.stringify(result.metafile, null, 2));
  const runtime = path.join(source, 'app/js');
  for (const name of ['msfs-runtime.js', 'msfs/coherent.js', 'msfs/simvar.js', 'msfs/environment.js', 'msfs/instrument.js']) {
    fs.mkdirSync(path.dirname(path.join(output, 'runtime', name)), { recursive: true });
    fs.copyFileSync(path.join(runtime, name), path.join(output, 'runtime', name));
  }
  const font = path.join(aircraft, 'src/fonts/FBW-Display-EIS-A380.ttf');
  fs.mkdirSync(path.join(output, 'Fonts/fbw-a380x'), { recursive: true });
  fs.copyFileSync(font, path.join(output, 'Fonts/fbw-a380x/FBW-Display-EIS-A380.ttf'));
  const assets = path.join(aircraft, 'src/base/flybywire-aircraft-a380-842/html_ui');
  for (const name of ['FBW-Display-EIS-A380-SlashedZero.ttf', 'NDChrono.ttf']) {
    fs.copyFileSync(path.join(assets, 'Fonts/fbw-a380x', name), path.join(output, 'Fonts/fbw-a380x', name));
  }
  for (const relative of ['Images/fbw-a380x/oans', 'Images/fbw-a380x/Common']) {
    fs.cpSync(path.join(assets, relative), path.join(output, relative), { recursive: true });
  }
  fs.copyFileSync(path.join(source, 'xp-kit/LICENSE'), path.join(output, 'LICENSE'));
  const checks = cp.spawnSync(process.execPath,
    [path.join(toolchain, 'node_modules/typescript/lib/tsc.js'), '--noEmit', '-p', path.join(aircraft, 'src/systems/instruments/src/ND/tsconfig.json')],
    { encoding: 'utf8', env: { ...process.env, FBW_TYPECHECK: '1' } });
  fs.writeFileSync(path.join(output, 'typecheck.txt'), (checks.stdout || '') + (checks.stderr || ''));
  fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify({
    format: 1, scope: native ? 'Native aircraft ND/OANS — no demo fixtures' : 'browser review only',
    source, entrypoint: 'fbw-a380x/src/systems/instruments/src/ND/instrument.tsx',
    library: 'xp-kit', library_sha256: hash(path.join(source, 'xp-kit/dist/index.js')),
    preview_adapters: [native?'Native local-file scenery provider; no HTTP/Navigraph':'Local X-Plane apt.dat geometry provider; no Navigraph authentication',
      native?'Native live-data bus and input hook':'One post-backplane demo-fixture hook', 'Neutral SVG transform correction for ARC/ROSE ND/OANC compasses',
      'Control panel uses available PNG icons and labels its database LOCAL SCENERY',
      'Original FlyByWire cursor show/refresh fixed; ND/OANS cursor always yellow',
      'PLAN waits for valid map parameters before positioning the aircraft',
      'Native-resolution EIS typography and vector compass; rounded route joins and spatial label decluttering'],
    render_resolution: [768,1024], source_font: 'FBW-Display-EIS-A380.ttf',
    oans_zoom_levels_nm: [0.05,0.1,0.2,0.5,1,2,5], oans_default_zoom_nm: 0.1,
    quality_source_hashes: Object.fromEntries(['tools/nd_quality_adapter.cjs','native/avionics/xp-kit-nd/quality-layout.mjs',
      'native/avionics/xp-kit-nd/quality.css','native/avionics/xp-kit-nd/native-bridge.js',
      'native/avionics/xp-kit-nd/close-zoom.mjs'].map(name=>[name,hash(path.join(root,name))])),
    bundle_sha256: hash(path.join(output, 'ND/bundle.js')),
    typecheck: checks.status === 0 ? 'passed' : 'blocked — see typecheck.txt',
  }, null, 2));
  console.log('Built original FlyByWire ND + OANS through xp-kit:', output);
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
