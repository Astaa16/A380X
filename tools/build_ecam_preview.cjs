// SPDX-License-Identifier: GPL-3.0-or-later
// Build only the isolated ECAM website. Does not build or install a plugin.
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'..');
const source=process.env.FBW_SOURCE||'/Users/sava/Downloads/Flybywire-A380X-for-Xplane-12-main';
const instrument=path.join(source,'fbw/xplane/fbw-a380x/src/systems/instruments/src');
const local=path.join(root,'native/avionics/ecam-preview');
const native=process.env.ECAM_NATIVE==='1';
const output=path.resolve(process.env.ECAM_PREVIEW_OUTPUT||path.join(root,native?'build/ecam-native':'build/ecam-preview'));
const toolchain=process.env.ND_TOOLCHAIN||'/Users/sava/.cache/a380-nd-preview-tools-20261016';
process.env.NODE_PATH=path.join(toolchain,'node_modules');require('node:module').Module._initPaths();
process.env.XP_KIT=path.join(source,'xp-kit/dist');
const transform=require(path.join(source,'xp-kit/tools/transform.cjs'));
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
async function main(){
  fs.mkdirSync(output,{recursive:true});
  const boundary={name:'isolated-ecam-fixtures',setup(build){
    build.onResolve({filter:/^(@flybywiresim\/fbw-sdk-react|@instruments\/common\/(simVars|arinc429))$/},()=>({path:path.join(local,native?'live.tsx':'fixtures.tsx')}));
    build.onResolve({filter:/^@flybywiresim\/(fbw-sdk|msfs-avionics-common)$/},()=>({path:path.join(local,'v2-adapter.ts')}));
    build.onResolve({filter:/^@sd\//},args=>({path:path.join(instrument,'SD',args.path.slice(4))+'.tsx'}));
    build.onResolve({filter:/^@sdv2\//},args=>({path:path.join(instrument,'SDv2',args.path.slice(6))+'.tsx'}));
    build.onResolve({filter:/^@instruments\/common\//},args=>({path:path.join(instrument,'Common',args.path.split('/').pop())+'.tsx'}));
    build.onLoad({filter:/\.scss$/},()=>({contents:'',loader:'css'}));
    build.onLoad({filter:/\/(SD|SDv2)\/Pages\/.+\.tsx$/},args=>{
      let contents=fs.readFileSync(args.path,'utf8');
      // Wire the original static MORE state to the review ECP, without editing upstream.
      contents=contents.replace('const [showMore] = useState(false);',"const [showMore] = useSimVar('L:A380X_PREVIEW_MORE', 'bool');")
        .replace('const [moreActive] = useState(false);',"const [moreActive] = useSimVar('L:A380X_PREVIEW_MORE', 'bool');");
      if(args.path.endsWith('/Wheel/WheelPage.tsx'))contents="import { useSimVar } from '@instruments/common/simVars';\n"+contents.replace('const moreActive = false;',"const [moreActive] = useSimVar('L:A380X_PREVIEW_MORE', 'bool');");
      // Upstream optional indicator positions are omitted by the parent. SVG
      // translate(undefined undefined) is invalid; apply their intended origin.
      if(/\/(VerticalDeflectionIndication|HorizontalDeflectionIndicator)\.tsx$/.test(args.path))contents=contents.replace('translate(${this.props.x} ${this.props.y})','translate(${this.props.x ?? 0} ${this.props.y ?? 0})');
      if(native&&args.path.endsWith('/ComputerIndication.tsx'))contents=contents.replaceAll('infoAvailable = Subject.create(true)','infoAvailable = Subject.create(false)');
      if(native)contents=contents.replaceAll('deflectionInfoValid = Subject.create(true)','deflectionInfoValid = Subject.create(false)').replaceAll('positionInfoValid = Subject.create(true)','positionInfoValid = Subject.create(false)');
      if(native&&args.path.endsWith('/CruisePage.tsx'))contents=contents.replace('fuelForDisplay(v, this.usingMetric.get(), 1, 5).toFixed(0)',"'XX'").replace('{this.engineTotalFuelUsedDisplay}',"{'XX'}");
      if(native&&args.path.endsWith('/CruiseCond.tsx'))contents=contents.replace(/\{this\.(cockpitCabinTempFormatted|fwdCargoTempFormatted|aftCargoTempFormatted|minMainDeckTemp|maxMainDeckTemp|minUpperDeckTemp|maxUpperDeckTemp)\}/g,"{'XX'}");
      if(native&&contents.includes("from 'react'"))contents=`import { NativeReact } from ${JSON.stringify(path.join(local,'live.tsx'))};\n`+contents;
      return {contents:transform.transform(contents,args.path),loader:'tsx',resolveDir:path.dirname(args.path)};
    });
    if(native)build.onLoad({filter:/\/Common\/.+\.tsx$/},args=>{let contents=fs.readFileSync(args.path,'utf8');if(contents.includes("from 'react'"))contents=`import { NativeReact } from ${JSON.stringify(path.join(local,'live.tsx'))};\n`+contents;return {contents:transform.transform(contents,args.path),loader:'tsx',resolveDir:path.dirname(args.path)};});
    if(native)build.onLoad({filter:/\/ecam-preview\/v2-adapter.ts$/},args=>({contents:transform.transform(fs.readFileSync(args.path,'utf8').replace('cell(word())','cell(word(0,false))'),args.path),loader:'ts',resolveDir:path.dirname(args.path)}));
  }};
  const result=await require('esbuild').build({entryPoints:[path.join(local,native?'native.tsx':'app.tsx')],outfile:path.join(output,'app.js'),bundle:true,format:'iife',target:'es2020',
    jsxFactory:native?'NativeReact.createElement':'React.createElement',jsxFragment:'React.Fragment',nodePaths:[path.join(toolchain,'node_modules')],plugins:[boundary,transform.plugin()],
    // xp-kit exports an unused instrument base class alongside its widgets.
    // The review only mounts widgets: no MSFS runtime / backplane is started.
    banner:{js:'globalThis.BaseInstrument = class {};'},
    define:{'process.env.NODE_ENV':'"production"'},minify:true,metafile:true,logLevel:'info'});
  const sass=require('sass');
  const styles=['index.scss','Common/gauges.scss'].map(name=>sass.compile(path.join(instrument,name),{silenceDeprecations:['legacy-js-api','import','global-builtin','color-functions'],logger:{warn(){},debug(){}}}).css).join('\n');
  fs.writeFileSync(path.join(output,'display.css'),styles);
  for(const name of ['index.html','review.css'])fs.copyFileSync(path.join(local,name),path.join(output,name));
  const assets=path.join(source,'fbw/xplane/fbw-a380x/src/base/flybywire-aircraft-a380-842/html_ui');
  fs.mkdirSync(path.join(output,'Fonts/fbw-a380x'),{recursive:true});
  fs.copyFileSync(path.join(source,'fbw/xplane/fbw-a380x/src/fonts/FBW-Display-EIS-A380.ttf'),path.join(output,'Fonts/fbw-a380x/FBW-Display-EIS-A380.ttf'));
  fs.mkdirSync(path.join(output,'Images/fbw-a380x'),{recursive:true});
  const images=new Set();
  for(const file of Object.keys(result.metafile.inputs)){
    const full=path.resolve(file);if(!full.startsWith(instrument)||!fs.existsSync(full))continue;
    for(const match of fs.readFileSync(full,'utf8').matchAll(/\/Images\/fbw-a380x\/([^'"`\s{}]+\.(?:png|svg))/g))images.add(match[1]);
  }
  for(const name of images){const from=path.join(assets,'Images/fbw-a380x',name);if(!fs.existsSync(from))throw Error('Missing original image: '+name);fs.copyFileSync(from,path.join(output,'Images/fbw-a380x',name));}
  const inputs=Object.keys(result.metafile.inputs).map(f=>path.resolve(f)).filter(f=>f.startsWith(instrument));
  const bundledInputs=Object.keys(result.metafile.inputs);
  if(native&&bundledInputs.some(f=>/\/(fixtures|app|v2)\.tsx$/.test(f)))throw Error('Preview fixture/UI entered the native SD bundle');
  fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({format:1,scope:native?'Native ECAM SD; read-only live aircraft values; no preview fixtures':'Local ECAM web review only; synthetic inputs; no simulator transport or installation',native,source,
    original_pages:'React SD: ENGINE, APU, BLEED, COND, PRESS, DOOR, ELEC AC, ELEC DC, FUEL, WHEEL, HYD, CB, STATUS. xp-kit SDv2: F/CTL, CRUISE, VIDEO.',
    adapters:['Read-only synthetic SimVar / ARINC fixture boundary','Independent ECP state controller','MORE linked to original FUEL and WHEEL pages','F/CTL optional indicator coordinates default to the origin','Isolated ECP alert / checklist test area; not a full FWS'],
    source_files:inputs.map(file=>({file:path.relative(source,file),sha256:sha(file)})),bundle_sha256:sha(path.join(output,'app.js')),license:'GPL-3.0',images:[...images]},null,2));
  const license=path.join(source,'xp-kit/LICENSE');fs.copyFileSync(license,path.join(output,'LICENSE'));
  console.log('ECAM review built:',output);
}
main().catch(e=>{console.error(e);process.exitCode=1;});
