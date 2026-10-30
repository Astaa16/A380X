// SPDX-License-Identifier: GPL-3.0-or-later
'use strict';
const fs = require('node:fs');
const path = require('node:path');
module.exports = (root, transform, replaceOnce) => ({name:'nd-native-quality',setup(build) {
  build.onResolve({filter:/^a380x-nd-quality$/},()=>({path:path.join(root,'native/avionics/xp-kit-nd/quality-layout.mjs')}));
  build.onResolve({filter:/^a380x-nd-close-zoom$/},()=>({path:path.join(root,'native/avionics/xp-kit-nd/close-zoom.mjs')}));
  build.onLoad({filter:/\/OANC\/Oanc\.tsx$/},args=>{
    let text=fs.readFileSync(args.path,'utf8');
    text="import { createOansCloseViewport, oansLayerVisible } from 'a380x-nd-close-zoom';\n"+text;
    text=replaceOnce(text,'export type A380EfisZoomRangeValue = 0.2 | 0.5 | 1 | 2 | 5;',
      'export type A380EfisZoomRangeValue = 0.05 | 0.1 | 0.2 | 0.5 | 1 | 2 | 5;');
    text=replaceOnce(text,'export const a380EfisZoomRangeSettings: A380EfisZoomRangeValue[] = [0.2, 0.5, 1, 2, 5];',
      'export const a380EfisZoomRangeSettings: A380EfisZoomRangeValue[] = [0.05, 0.1, 0.2, 0.5, 1, 2, 5];');
    text=replaceOnce(text,'export const LABEL_VISIBILITY_RULES = [true, true, true, true, true];',
      'export const LABEL_VISIBILITY_RULES = [true, true, true, true, true, true, true];');
    text=replaceOnce(text,'  FeatureType.TaxiwayElement,\n  FeatureType.VerticalPolygonalStructure,',
      '  FeatureType.TaxiwayElement,\n  FeatureType.TaxiwayGuidanceLine,\n  FeatureType.VerticalPolygonalStructure,');
    text=replaceOnce(text,'if (feature.properties.feattype === FeatureType.TaxiwayElement && !feature.properties.idlin)',
      'if ([FeatureType.TaxiwayElement, FeatureType.TaxiwayGuidanceLine].includes(feature.properties.feattype) && !feature.properties.idlin)');
    const filterStart=text.indexOf('    if (this.efisNDModeSub.get() === EfisNdMode.ARC) {',text.indexOf('  private handleLabelFilter()'));
    const filterEnd=text.indexOf('\n\n    this.handleLayerVisibilities();',filterStart);
    if(filterStart<0 || filterEnd<0) throw new Error('OANC physical-range label filter adapter mismatch');
    text=text.slice(0,filterStart)+`    const range = this.props.zoomValues[this.zoomLevelIndex.get()];
    if (this.efisNDModeSub.get() === EfisNdMode.ARC) {
      this.labelManager.currentFilter = { type: range >= 2 ? 'none' : range >= 1 ? 'major' : 'null' };
    } else {
      this.labelManager.currentFilter = { type: 'runwayBtvSelection', runwayIdent: null, showAdjacent: range <= 0.2 };
    }`+text.slice(filterEnd);
    text=replaceOnce(text,'LAYER_SPECIFICATIONS[i].zoomLevelVisibilities[this.zoomLevelIndex.get()]',
      'oansLayerVisible(i, this.props.zoomValues[this.zoomLevelIndex.get()])');
    text=replaceOnce(text,'  public labelContainerRef = FSComponent.createRef<HTMLDivElement>();',
      `  private readonly qualityCloseCanvasRef = FSComponent.createRef<HTMLCanvasElement>();
  private readonly qualityCloseMap = createOansCloseViewport();
  public labelContainerRef = FSComponent.createRef<HTMLDivElement>();`);
    text=replaceOnce(text,'    // Transform airplane',
      '    this.qualityCloseMap.update(this.qualityCloseCanvasRef.instance, this, LAYER_SPECIFICATIONS);\n\n    // Transform airplane');
    text=replaceOnce(text,'          <div\n            ref={this.animationContainerRef[0]}',
      `          <canvas ref={this.qualityCloseCanvasRef} class="oanc-close-map" width={768} height={768}
            style="position: absolute; left: 0; top: 0; pointer-events: none; display: none;" />
          <div
            ref={this.animationContainerRef[0]}`);
    return {contents:transform.transform(text,args.path),loader:'tsx',resolveDir:path.dirname(args.path)};
  });
  build.onLoad({filter:/\/ND\/shared\/map\/WaypointLayer\.ts$/},args=>{
    let text=fs.readFileSync(args.path,'utf8');
    text="import { layoutNdLabels } from 'a380x-nd-quality';\n"+text;
    text=replaceOnce(text,'  private readonly options: MapOptions;',`  private readonly options: MapOptions;
  private qualityLabels = new Map<NdSymbol, {x:number,y:number}|null>();
  private qualityWidths = new Map<string, number>();
  private qualityFontReady = false;
  private prepareQualityLabels(context: CanvasRenderingContext2D, width: number, height: number, parameters: MapParameters) {
    context.font = '21px Ecam';
    const ready=document.fonts?.check('21px Ecam') ?? true;
    if(ready!==this.qualityFontReady) { this.qualityWidths.clear(); this.qualityFontReady=ready; }
    const rows = this.data.map((symbol,order)=>{
      const [x,y]=parameters.coordinatesToXYy(symbol.location);
      let measured=this.qualityWidths.get(symbol.ident);
      if(measured===undefined) { measured=context.measureText(symbol.ident).width; this.qualityWidths.set(symbol.ident,measured); }
      const active=!!(symbol.type & NdSymbolTypeFlags.ActiveLegTermination);
      const route=!!(symbol.type & NdSymbolTypeFlags.FlightPlan);
      return {key:symbol,order,x:x+width/2,y:y+height/2,width:Math.max(measured,...(symbol.constraints||[]).map(line=>context.measureText(line).width)),below:(symbol.constraints?.length||0)*18,
        priority:active?1000:route?500:(symbol.type & NdSymbolTypeFlags.Tuned)?400:300,required:active};
    });
    if(this.qualityWidths.size>512) this.qualityWidths.clear();
    this.qualityLabels=layoutNdLabels(rows,width,height);
  }
  private paintQualityLabel(colorLayer:boolean, context:CanvasRenderingContext2D, symbol:NdSymbol, color:string) {
    const position=this.qualityLabels.get(symbol);
    if(position) PaintUtils.paintText(colorLayer,context,position.x,position.y,symbol.ident,color);
  }`);
    const shadow=`  ) {
    for (const symbol of this.data) {`;
    // The signature occurs once in each paint pass; include the owner name.
    const start=text.indexOf('  paintShadowLayer('), end=text.indexOf('  paintColorLayer(');
    const section=text.slice(start,end);
    text=text.slice(0,start)+replaceOnce(section,shadow,shadow.replace('    for (','    this.prepareQualityLabels(context,mapWidth,mapHeight,mapParameters);\n    for ('))+text.slice(end);
    for(const [before,after] of [
      ['PaintUtils.paintText(isColorLayer, context, x + 13, y + 18, symbol.ident, mainColor);','this.paintQualityLabel(isColorLayer,context,symbol,mainColor);'],
      ['PaintUtils.paintText(isColorLayer, context, x + 13, y + 17, symbol.ident, mainColor);','this.paintQualityLabel(isColorLayer,context,symbol,mainColor);'],
      ["PaintUtils.paintText(isColorLayer, context, x + 15, y + 17, symbol.ident, '#ff94ff');","this.paintQualityLabel(isColorLayer,context,symbol,'#ff94ff');"],
      ['PaintUtils.paintText(isColorLayer, context, x + 15, y + 17, symbol.ident, mainColor);','this.paintQualityLabel(isColorLayer,context,symbol,mainColor);'],
    ]) text=replaceOnce(text,before,after);
    const originalBox=`    const TEXT_LENGTH = Math.max(110, symbol.ident.length * 13.5);
    if (px > x - 7 && px < x + 13 + TEXT_LENGTH && py > y - 10 && py < y + 22) {
      context.strokeStyle = '#0ff';
      context.lineWidth = 1.75;
      context.strokeRect(x - 7, y - 10, 10 + 13 + TEXT_LENGTH, 29);
    }`;
    text=replaceOnce(text,originalBox,`    const label=this.qualityLabels.get(symbol);
    if(!label) return;
    const width=this.qualityWidths.get(symbol.ident) || symbol.ident.length*13.5;
    const left=Math.min(x-8,label.x-4),right=Math.max(x+8,label.x+width+4);
    const top=Math.min(y-10,label.y-24),bottom=Math.max(y+10,label.y+4);
    if(px>left && px<right && py>top && py<bottom) {
      globalThis.__a380NdHover=symbol.databaseId || symbol.ident;
      context.strokeStyle='#0ff';context.lineWidth=1.75;
      context.strokeRect(left,top,right-left,bottom-top);
    }`);
    text=replaceOnce(text,"    context.fillStyle = '#ff94ff';","    const position=this.qualityLabels.get(symbol);\n    if(!position) return;\n    context.fillStyle = '#ff94ff';");
    text=replaceOnce(text,"PaintUtils.paintText(true, context, x + 15, y + 35 + 18 * i, line, '#ff94ff');","PaintUtils.paintText(true, context, position.x, position.y + 18 + 18 * i, line, '#ff94ff');");
    return {contents:transform.transform(text,args.path),loader:'ts',resolveDir:path.dirname(args.path)};
  });
  build.onLoad({filter:/\/OANC\/OancLabelManager\.ts$/},args=>{
    let text=fs.readFileSync(args.path,'utf8');
    text="import { declutterOansLabels } from 'a380x-nd-quality';\n"+text;
    text=replaceOnce(text,"    } else {\n      this.oanc.labelContainerRef.instance.style.visibility = 'hidden';",
      "      declutterOansLabels(this.visibleLabelElements);\n    } else {\n      this.oanc.labelContainerRef.instance.style.visibility = 'hidden';");
    return {contents:transform.transform(text,args.path),loader:'ts',resolveDir:path.dirname(args.path)};
  });
  build.onLoad({filter:/\/ND\/shared\/map\/CanvasMap\.tsx$/},args=>{
    let text=fs.readFileSync(args.path,'utf8');
    text=replaceOnce(text,'    context.clearRect(0, 0, size, size);',
      "    context.clearRect(0, 0, size, size);\n    globalThis.__a380NdHover='';\n    context.save();\n    context.lineJoin = 'round';\n    context.lineCap = 'round';");
    text=replaceOnce(text,'  private drawVector(context:', '  private drawVector(context:');
    text=replaceOnce(text,'\n  }\n\n  private drawVector(context:', '\n    context.restore();\n  }\n\n  private drawVector(context:');
    text=replaceOnce(text,'    context.lineWidth = 1.75;','    context.lineWidth = 2.0;');
    return {contents:transform.transform(text,args.path),loader:'tsx',resolveDir:path.dirname(args.path)};
  });
}});
