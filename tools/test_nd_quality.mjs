// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {layoutNdLabels, declutterOansLabels} from '../native/avionics/xp-kit-nd/quality-layout.mjs';
const items=[
 {key:'to',order:0,x:300,y:300,width:90,priority:1000,required:true},
 {key:'near',order:1,x:310,y:305,width:95,priority:300},
 {key:'nav',order:2,x:315,y:315,width:85,priority:400},
 {key:'off',order:3,x:-400,y:100,width:40,priority:300},
];
const positions=layoutNdLabels(items);
assert.ok(positions.get('to'),'The active waypoint must remain identified');
assert.equal(positions.get('off'),null);
const boxes=items.filter(i=>positions.get(i.key)).map(i=>{const p=positions.get(i.key);return [p.x-4,p.y-26,p.x+i.width+4,p.y+4]});
const overlap=(a,b)=>a[0]<b[2]&&a[2]>b[0]&&a[1]<b[3]&&a[3]>b[1];
for(let i=0;i<boxes.length;i++)for(let j=i+1;j<boxes.length;j++)assert.ok(!overlap(boxes[i],boxes[j]));
for(const [i,box] of boxes.entries())for(const point of items.slice(0,3))
 assert.ok(!overlap(box,[point.x-16,point.y-16,point.x+16,point.y+16]),'Labels cannot hide fix/navaid symbols');
assert.deepEqual([...layoutNdLabels(items)],[...positions],'Stable data must not shuffle labels');
const elements=new Map();
const add=(style,text,left,top,transform='translate(-50%, -50%)')=>{
 const element={style:{visibility:'inherit',left:String(left),top:String(top),transform}};
 elements.set({style,text},element);return element;
};
const runway=add('runway-end','09',200,200,'translate(-50%, -50%) rotate(90deg) translate(0px, 50px)');
const taxi=add('taxiway','A',150,200);
const free=add('taxiway','B',400,400);
const marker=add('cross-symbol','',500,500);
const covered=add('taxiway','D',500,500);
const hidden=add('taxiway','C',600,600);hidden.style.visibility='hidden';
declutterOansLabels(elements);
assert.equal(runway.style.visibility,'inherit');assert.equal(taxi.style.visibility,'hidden','Use the rotated runway-label position');
assert.equal(marker.style.visibility,'inherit');assert.equal(covered.style.visibility,'hidden','User map markers retain clear space');
assert.equal(free.style.visibility,'inherit');assert.equal(hidden.style.visibility,'hidden','Respect original zoom and visibility filters');
const dense=Array.from({length:200},(_,order)=>({key:order,order,x:50+(order%20)*33,y:60+Math.floor(order/20)*60,width:52,priority:order===0?1000:300,required:order===0}));
const result=layoutNdLabels(dense);assert.equal(result.size,200);assert.ok([...result.values()].some(x=>x===null),'Dense optional labels must yield rather than overprint');
console.log('ND quality: priority, symbol clearance, label collision, deterministic placement, OANS rotation and source visibility passed');
