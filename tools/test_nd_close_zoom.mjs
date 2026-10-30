// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {OANS_RANGES_NM,OANS_DEFAULT_ZOOM,oansLayerVisible,oansViewportMatrix,oansBoundsVisible,createOansCloseViewport} from '../native/avionics/xp-kit-nd/close-zoom.mjs';
assert.deepEqual(OANS_RANGES_NM,[.05,.1,.2,.5,1,2,5]);
assert.equal(OANS_RANGES_NM[OANS_DEFAULT_ZOOM],.1);
for(const range of [.05,.1,.2]) assert.deepEqual([0,1,2,3,4].map(layer=>oansLayerVisible(layer,range)),[true,true,false,true,true]);
assert.deepEqual([0,1,2,3,4].map(layer=>oansLayerVisible(layer,.5)),[true,true,false,false,true]);
for(const range of [1,2,5]) assert.deepEqual([0,1,2,3,4].map(layer=>oansLayerVisible(layer,range)),[false,true,true,false,true]);
// Native paths invert northing; test a rotated, scaled and panned projection.
const projectPoint=([x,y])=>[384+2*x+3*y,620+3*x-2*y];
const matrix=oansViewportMatrix(projectPoint);
assert.deepEqual(matrix,[2,3,-3,2,384,620]);
const apply=([x,y])=>[matrix[0]*x+matrix[2]*-y+matrix[4],matrix[1]*x+matrix[3]*-y+matrix[5]];
assert.deepEqual(apply([25,-80]),projectPoint([25,-80]));
assert.equal(oansBoundsVisible([-5,-5,5,5],matrix),true);
assert.equal(oansBoundsVisible([10000,10000,11000,11000],matrix),false);
const paths=[];
globalThis.Path2D=class {constructor(){this.calls=[];paths.push(this);}moveTo(...xy){this.calls.push(['move',...xy]);}lineTo(...xy){this.calls.push(['line',...xy]);}closePath(){this.calls.push(['close']);}};
const calls=[];
const context={resetTransform(){calls.push(['reset']);},clearRect(...v){calls.push(['clear',...v]);},setTransform(...v){calls.push(['matrix',...v]);},fill(...v){calls.push(['fill',...v]);},stroke(...v){calls.push(['stroke',...v]);}};
const canvas={style:{},getContext(){return context;}};
const polygon={properties:{feattype:1},geometry:{type:'Polygon',coordinates:[[[0,0],[10,0],[10,10],[0,10],[0,0]],[[2,2],[2,8],[8,8],[8,2],[2,2]]]}};
const refs=Array.from({length:5},()=>({instance:{style:{}}}));
let zoom=1,pan=0;
const oanc={props:{zoomValues:OANS_RANGES_NM},zoomLevelIndex:{get:()=>zoom},data:{},layerCanvasScaleContainerRefs:refs,
  layerFeatures:Array.from({length:5},(_,i)=>({features:i===0?[polygon]:[]})),projectPoint:([x,y])=>[384+5.4*x+pan,384-5.4*y]};
const specs=Array.from({length:5},()=>({styleRules:[{forFeatureTypes:[1],styles:{doFill:true,fillStyle:'gray'}}]}));
const view=createOansCloseViewport();view.update(canvas,oanc,specs);
assert.equal(view.profile.draws,1);assert.equal(view.profile.cachedPaths,1);
assert.equal(paths[0].calls.filter(c=>c[0]==='close').length,2,'Preserve both exterior and hole rings');
assert.ok(calls.find(c=>c[0]==='fill'&&c[2]==='evenodd'),'Holes use compound even-odd fill');
view.update(canvas,oanc,specs);assert.equal(view.profile.draws,1,'Unchanged view and pointer events reuse the viewport');
pan=50;view.update(canvas,oanc,specs);assert.equal(view.profile.draws,2);assert.equal(paths.length,1,'Panning must reuse geometry');
zoom=6;view.update(canvas,oanc,specs);assert.equal(canvas.style.display,'none');
assert.equal(refs[2].instance.style.visibility,'inherit');assert.equal(refs[3].instance.style.visibility,'hidden');
assert.equal(refs[4].instance.style.visibility,undefined,'Dynamic BTV canvas remains owned by upstream');
console.log('Close airport zoom: semantic ranges, affine alignment, layer visibility, polygon holes, geometry and viewport cache passed.');
