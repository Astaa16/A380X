// SPDX-License-Identifier: GPL-3.0-or-later
import { Widget, cell } from 'xp-kit';
import { word } from './model.mjs';
export class DestroyableComponent extends Widget {
  subscriptions=[];
  onAfterRender(){}
  pauseSubscriptions(){this.subscriptions.forEach(s=>s.pause?.());}
  resumeSubscriptions(){this.subscriptions.forEach(s=>s.resume?.(true));}
  destroy(){this.subscriptions.forEach(s=>s.destroy?.());super.destroy();}
}
export const MathUtils={clamp:(n,lo,hi)=>Math.max(lo,Math.min(hi,n)),RADIANS_TO_DEGREES:180/Math.PI};
export const NXDataStore={getSetting:()=>cell(true)};
export const NXUnits={kgToUser:(n)=>n,userWeightUnit:()=>'KG'};
export class Arinc429LocalVarConsumerSubject {
  static create(consumer){
    const result=cell(word());let sub;
    result.setConsumer=(c)=>{sub?.destroy();sub=c?.handle(value=>result.set(word(value)));return result;};
    result.setConsumer(consumer);return result;
  }
}
