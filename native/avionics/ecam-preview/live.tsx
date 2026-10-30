// SPDX-License-Identifier: GPL-3.0-or-later
import React,{createContext,useContext,useRef,useSyncExternalStore,useEffect} from 'react';
import {flushSync} from 'react-dom';
import {liveValue,raw} from './live-data.mjs';
let current={page:0,powered:false,more:false,vars:{},warnings:[]};
const listeners=new Set<()=>void>();
const Quality=createContext<any>(null),wrappers=new WeakMap();
const subscribe=fn=>{listeners.add(fn);return ()=>listeners.delete(fn);};
export const getLive=()=>current;
export function applyLive(frame){if(!Number.isInteger(frame.page)||frame.page<0||frame.page>15)throw Error('Invalid native SD page');current=frame;flushSync(()=>listeners.forEach(fn=>fn()));}
export function useLive(){return useSyncExternalStore(subscribe,getLive);}
export function useSimVar(name){const frame=useLive(),quality=useContext(Quality),value=liveValue(name,frame);if(value===undefined)quality?.missing.add(name);return [value??0,()=>{}];}
export function useSimVarList(names){const frame=useLive(),quality=useContext(Quality);return [names.map(n=>{const v=liveValue(n,frame);if(v===undefined)quality?.missing.add(n);return v??0;}),()=>{}];}
export function useArinc429Var(name){const frame=useLive(),quality=useContext(Quality),v=liveValue(name,frame),valid=v!==undefined;if(!valid)quality?.missing.add(name);return {value:v??0,isNormalOperation:()=>valid,isFailureWarning:()=>!valid,isNoComputedData:()=>!valid,valueOr:f=>valid?v:f,bitValueOr:(bit,f)=>valid?!!(Math.trunc(v)&2**(bit-1)):f};}
export const usePersistentSetting=()=>[true,()=>{}];
export const NXUnits={kgToUser:n=>n,userWeightUnit:()=>'KG'};
export const MathUtils={clamp:(n,lo,hi)=>Math.max(lo,Math.min(hi,n))};
export function useInterval(fn,delay,options={}){const ref=useRef(fn);ref.current=fn;useEffect(()=>{if(options.runOnStart)ref.current();const id=setInterval(()=>ref.current(),delay);return ()=>clearInterval(id);},[delay]);}
function scopeMissing(scope){for(let q=scope;q;q=q.parent)if(q.missing.size)return true;return false;}
function NativeNode({tag,children,...props}){
  const quality=useContext(Quality),missing=scopeMissing(quality);
  if(missing&&typeof props.className==='string'&&/\b(Green|Amber)\b/.test(props.className)){
    props.className=props.className.replace(/\bGreen\b/g,'Amber');
    if(tag==='text'&&/^\s*[-+\d.]+\s*$/.test(React.Children.toArray(children).join('')))children='XX';
  }
  return React.createElement(tag,props,children);
}
function wrap(Component){if(wrappers.has(Component))return wrappers.get(Component);function Scoped(props){const parent=useContext(Quality),scope=useRef({parent,missing:new Set()}).current;scope.parent=parent;scope.missing.clear();return React.createElement(Quality.Provider,{value:scope},React.createElement(Component,props));}wrappers.set(Component,Scoped);return Scoped;}
const svgTags=new Set(['text','path','rect','circle','line','polygon','polyline']);
export const NativeReact={...React,createElement(type,props,...children){return typeof type==='function'?React.createElement(wrap(type),props,...children):svgTags.has(type)?React.createElement(NativeNode,{...props,tag:type},...children):React.createElement(type,props,...children);}};
globalThis.SimVar={GetSimVarValue:(name)=>liveValue(name,current),SetSimVarValue:async(name)=>{if(name!=='BUS LOOKUP INDEX')throw Error('SD renderer cannot write simulator state');}};
globalThis.Utils={Clamp:MathUtils.clamp};
export {raw};
