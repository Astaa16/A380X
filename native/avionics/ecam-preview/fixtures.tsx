// SPDX-License-Identifier: GPL-3.0-or-later
import { useSyncExternalStore, useEffect, useRef } from 'react';
import { fixture, initialState, word } from './model.mjs';
let current=initialState();
const listeners=new Set<()=>void>();
export function setFixtureState(s){current=s;listeners.forEach(fn=>fn());}
const subscribe=fn=>{listeners.add(fn);return ()=>listeners.delete(fn);};
const snapshot=()=>current;
export function useSimVar(name,_unit?,_interval?){const s=useSyncExternalStore(subscribe,snapshot);return [fixture(name,s),()=>{}];}
export function useSimVarList(names){const s=useSyncExternalStore(subscribe,snapshot);return [names.map(n=>fixture(n,s)),()=>{}];}
export function useArinc429Var(name){const [v]=useSimVar(name);return word(v);}
export function usePersistentSetting(){return [true,()=>{}];}
export const NXUnits={kgToUser:(n)=>n,userWeightUnit:()=>'KG'};
export const MathUtils={clamp:(n,lo,hi)=>Math.max(lo,Math.min(hi,n))};
export function useInterval(fn,delay,options={}){const ref=useRef(fn);ref.current=fn;useEffect(()=>{if(options.runOnStart)ref.current();const id=setInterval(()=>ref.current(),delay);return ()=>clearInterval(id);},[delay]);}
// Original FuelPage uses a local bus lookup before reading switch fixtures.
globalThis.SimVar={GetSimVarValue:(name)=>fixture(name,current),SetSimVarValue:async(name)=>{if(name!=='BUS LOOKUP INDEX')throw Error('Simulator writes are disabled in ECAM preview');}};
globalThis.Utils={Clamp:(n,lo,hi)=>Math.max(lo,Math.min(hi,n))};
