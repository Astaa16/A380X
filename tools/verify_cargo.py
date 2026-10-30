#!/usr/bin/env python3
"""Evaluate actual staged/installed cargo OBJ8 motion, without X-Plane.

Checks all door vertices at five ratios, independent channels, closed geometry,
stationary structures and source coverage. Optional PNG is an offline geometry
preview, deliberately labelled; it is not a simulator image or FPS measurement.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'blender'))
from obj8_preview import load_obj8
from patch_efis_animations import vertex_payload

REFS=tuple(f'sim/flightmodel2/misc/door_open_ratio[{i}]' for i in (17,18,19))


def evaluate(path,values):
    obj=load_obj8(path,values=values)
    points=np.frombuffer(obj.positions,dtype=np.float64).reshape(-1,3)
    indices=np.frombuffer(obj.indices,dtype=np.uint32)
    result=[]
    for draw in obj.draws:
        ids=indices[draw.start:draw.start+draw.count]
        matrix=np.array(draw.matrix).reshape(4,4)
        world=points[ids]@matrix[:3,:3].T+matrix[:3,3]
        result.append((draw.draw_index,tuple(draw.transform_datarefs),world.reshape(-1,3,3)))
    return result


def preview(files,output):
    image=Image.new('RGB',(1440,1180),(17,26,36));painter=ImageDraw.Draw(image)
    painter.text((26,14),'CARGO DOORS + HOLDS | ACTUAL OBJ8 GEOMETRY | OFFLINE PREVIEW, NOT X-PLANE',fill=(220,235,245),font_size=18)
    names=('FWD','AFT','BULK');centers=(23.5,-13.5,-16.05)
    for row,(name,center) in enumerate(zip(names,centers)):
        for column,ratio in enumerate((0.,1.)):
            left=column*720;top=52+row*370
            painter.rectangle((left+14,top,left+704,top+352),fill=(31,44,58))
            painter.text((left+28,top+12),name+(' / CLOSED' if ratio==0 else ' / OPEN'),fill=(228,237,244),font_size=18)
            values={ref:ratio if i==row else 0 for i,ref in enumerate(REFS)}
            triangles=[];colors=[]
            for path in files:
                if path.name.endswith('_decals.obj'):continue
                color=(132,147,157) if 'holds' in path.name else (106,121,133) if 'hardware' in path.name else (214,220,225)
                for _,_,mesh in evaluate(path,values):
                    centers3d=mesh.mean(axis=1)
                    selected=(np.abs(centers3d[:,2]-center)<2.45)&(centers3d[:,1]>-1.6)&(centers3d[:,1]<4.)&(centers3d[:,0]<.7)
                    face=mesh[selected]
                    if len(face):triangles.append(face);colors.extend([color]*len(face))
            faces=np.concatenate(triangles);base=np.array(colors,dtype=float)
            eye=np.array([-1.,.3,.35]);eye/=np.linalg.norm(eye)
            right=np.cross([0.,1.,0.],eye);right/=np.linalg.norm(right)
            up=np.cross(eye,right)
            origin=np.array([-2.7,.5,center])
            uv=np.stack(((faces-origin)@right,(faces-origin)@up),axis=-1)
            scale=99 if row<2 else 150
            screen=np.stack((left+360+uv[:,:,0]*scale,top+210-uv[:,:,1]*scale),axis=-1)
            normal=np.cross(faces[:,1]-faces[:,0],faces[:,2]-faces[:,0]);length=np.linalg.norm(normal,axis=1)
            normal/=np.maximum(length[:,None],1e-10)
            light=np.array([-.5,.8,.3]);light/=np.linalg.norm(light)
            shade=.48+.52*np.abs(normal@light)
            rgb=np.clip(base*shade[:,None],0,255).astype(np.uint8)
            depth=faces.mean(axis=1)@eye
            tile=Image.new('RGB',(690,305),(31,44,58));draw=ImageDraw.Draw(tile)
            for i in np.argsort(depth):
                coords=[(float(x-left-14),float(y-top-37)) for x,y in screen[i]]
                if not all(-1000<x<2000 and -1000<y<2000 for x,y in coords):continue
                draw.polygon(coords,fill=tuple(map(int,rgb[i])))
            image.paste(tile,(left+14,top+37))
    painter.text((28,1156),'Geometry preview only. Cargo actuator/locking systems and simulator clearance/performance remain unverified.',fill=(157,181,199),font_size=15)
    image.save(output)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staging',type=Path,required=True)
    parser.add_argument('--aircraft',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--preview',type=Path)
    parser.add_argument('--installed',action='store_true')
    args=parser.parse_args()
    report=json.loads((args.staging/'cargo-report.json').read_text())
    files=[args.aircraft/r['path'] if args.installed else Path(r['staged']) for r in report['files'] if r['path'].endswith('.obj')]
    baseline={p:evaluate(p,{}) for p in files}
    counts=defaultdict(int);maximum=defaultdict(float);closed_errors=[]
    for path,draws in baseline.items():
        for _,refs,points in draws:
            active=set(refs)&set(REFS)
            if active:
                if len(active)!=1 or set(refs)!=active:raise ValueError('Cargo mesh is coupled to another door/gear animation')
                ref=next(iter(active));counts[ref]+=len(points)
        relative=next(r['path'] for r in report['files'] if Path(r['staged']).name==path.name)
        if relative.startswith('objects/exterior/') and not args.installed:
            original=(args.aircraft/relative).read_text()
            if vertex_payload(original)!=vertex_payload(path.read_text()):raise ValueError('Fuselage payload changed')
            obj=load_obj8(path);positions=np.frombuffer(obj.positions,dtype=np.float64).reshape(-1,3)
            ids=np.frombuffer(obj.indices,dtype=np.uint32)
            for draw,(_,refs,points) in zip(obj.draws,draws):
                if set(refs)&set(REFS):closed_errors.append(float(np.max(np.abs(points.reshape(-1,3)-positions[ids[draw.start:draw.start+draw.count]]))))
    expected=defaultdict(int)
    for t in report['doors']:expected[t['dataref']]+=t['count']//3
    if dict(counts)!=dict(expected):raise ValueError(f'Cargo coverage mismatch: {dict(counts)} != {dict(expected)}')
    if closed_errors and max(closed_errors)>2e-5:raise ValueError('Closed door moved away from authored rest position')
    for ref in REFS:
        for ratio in (0.,.25,.5,.75,1.):
            for path in files:
                moved=evaluate(path,{ref:ratio})
                for old,new in zip(baseline[path],moved):
                    if old[:2]!=new[:2] or old[2].shape!=new[2].shape:raise ValueError('Animation changed draw inventory')
                    delta=float(np.max(np.abs(old[2]-new[2])))
                    if ref not in old[1] and delta>1e-8:raise ValueError('Independent cargo channel moved another structure')
                    if ref in old[1]:maximum[ref]=max(maximum[ref],delta)
                    if not np.isfinite(new[2]).all():raise ValueError('Invalid animated position')
        if maximum[ref]<.3:raise ValueError('Cargo door does not visibly open')
    result=dict(passed=True,scope='Offline OBJ8 evaluation; not simulator acceptance',door_triangles=dict(counts),
        max_component_travel_m=dict(maximum),ratios=[0,.25,.5,.75,1],independent_channels=True,
        stationary_hold_and_fuselage_preserved=True,closed_pose_max_error_m=max(closed_errors,default=0),
        source_hold_triangles={r['material']:r['triangles'] for r in report['holds']},simulator_tested=False)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result),flush=True)
    if args.preview:preview(files,args.preview)


if __name__=='__main__':main()
