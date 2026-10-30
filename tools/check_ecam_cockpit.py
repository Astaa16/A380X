#!/usr/bin/env python3
"""Exercise only ECAM commands in the stationary project aircraft.

This proves native command/controller/device/render routing, not mouse picking.
Use --observe for read-only physical ECP click evidence after this check.
"""
import argparse
import base64
import json
from pathlib import Path
import time
from xplane_api import request, identifier

PREFIX='a380x/xp12/ecam/sd/'
PAGES=('engine','apu','bleed','cond','press','door','elec_ac','elec_dc','fuel','wheel','hyd','fctl','cb','cruise','status','video')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--observe',type=int,help='Read-only physical-action observation duration in seconds')
    parser.add_argument('--pages-only',action='store_true',help='Display-only page/render routing; no warning, checklist or brightness actions')
    args=parser.parse_args()
    ids={}
    def read(name):
        if name not in ids:ids[name]=identifier('datarefs',name)
        return request(f'datarefs/{ids[name]}/value',timeout=3)['data']
    def value(name):return read(PREFIX+name)
    def activate(name,duration=0):return request(f'command/{identifier("commands",PREFIX+name)}/activate',{'duration':duration},'POST')
    def wait_for(predicate,timeout=8):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if predicate():return
            time.sleep(.1)
        raise AssertionError('ECAM condition timed out')
    proof={'scope':'Actual X-Plane ECAM native command/device check; not a physical mouse-picking proof','checks':[],'restored':False}
    path=base64.b64decode(read('sim/aircraft/view/acf_relative_path')).decode().rstrip('\0')
    if path!='Aircraft/A380X-XP12/A380X-XP12.acf':raise RuntimeError('Wrong aircraft: '+path)
    if args.observe:
        if not 5<=args.observe<=600:parser.error('--observe must be 5..600 seconds')
        proof['scope']='Read-only native ECP action observation; human must use actual cockpit meshes, not menu/API'
        initial=value('action_count');last=initial;end=time.monotonic()+args.observe
        while time.monotonic()<end:
            count=value('action_count')
            if count!=last:
                last=count;event={'count':count,'action':value('last_action'),'page':value('active_page'),'bound':value('device_bound')}
                proof['checks'].append(event);print(json.dumps(event),flush=True)
            time.sleep(.1)
        proof['actions_observed']=last-initial
    else:
        if not args.pages_only and (read('sim/flightmodel/failures/onground_any')!=1 or read('sim/flightmodel/position/groundspeed')>.5):raise RuntimeError('Stationary ground check only')
        if not value('powered'):raise RuntimeError('Power the aircraft avionics and SD before this check')
        wait_for(lambda:value('device_bound')==1 and read('a380x/xp12/xpkit/sd_active')==1)
        saved={name:value(name) for name in ('active_page','manual_selection','brightness','ewd_brightness','more','checklist_visible','abnormal_visible')}
        if saved['checklist_visible'] or saved['abnormal_visible']:raise RuntimeError('Close the SD checklist/abnormal view before automated check')
        try:
            for index,name in enumerate(PAGES):
                before=value('action_count');draws=value('draw_count');presented=read('a380x/xp12/xpkit/sd_presented_frames');activate(name)
                wait_for(lambda:value('active_page')==index and value('action_count')>before)
                wait_for(lambda:read('a380x/xp12/xpkit/sd_active')==1)
                time.sleep(.35)
                wait_for(lambda:value('draw_count')>draws)
                if index>=2:wait_for(lambda:read('a380x/xp12/xpkit/sd_presented_frames')>presented)
                proof['checks'].append({'page':index,'command':name,'action_count':value('action_count'),'device_draws':value('draw_count')})
            if not args.pages_only:
                activate('all',1.3);wait_for(lambda:value('all_held')==0)
                for name in ('more','clear_left','clear_right','recall_last','recall','emergency_cancel','to_config','checklist','down','check_left','check_right','up','checklist','abnormal_procedures','abnormal_procedures'):
                    before=value('action_count');activate(name);wait_for(lambda:value('action_count')>before)
                activate('recall')
                for field,up,down in (('brightness','brightness_up','brightness_down'),('ewd_brightness','ewd_brightness_up','ewd_brightness_down')):
                    before=value(field);activate(down if before>=.05 else up)
                    wait_for(lambda:value(field)!=before)
                    activate(up if before>=.05 else down);wait_for(lambda:abs(value(field)-before)<=.001)
            if read('a380x/xp12/xpkit/errors'):raise AssertionError('Native renderer reported errors')
            proof['checks'].append({'routing':'All page/controller/device/render checks passed' if args.pages_only else 'ECAM action routing, both brightness knobs, ALL release and helper health passed'})
        finally:
            if value('checklist_visible'):activate('checklist')
            if value('abnormal_visible'):activate('abnormal_procedures')
            activate(PAGES[int(saved['active_page'])]);wait_for(lambda:value('active_page')==saved['active_page'])
            if value('manual_selection')!=saved['manual_selection']:activate(PAGES[int(saved['active_page'])])
            if value('more')!=saved['more']:activate('more')
            if not args.pages_only:activate('recall')
            proof['restored']=True
    args.output.write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,indent=2))


if __name__=='__main__':main()
