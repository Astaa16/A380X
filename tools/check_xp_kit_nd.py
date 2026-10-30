#!/usr/bin/env python3
"""Stationary-aircraft ND/OANS verification, with all moved selectors restored."""
import argparse
import base64
import ctypes as C
import json
import mmap
from pathlib import Path
import struct
from check_xp_kit_cockpit import Shared, Check, eventually, png
from xp_kit_transport import VERSION, ND_W, ND_H


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shared-file',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    saved={}
    with args.shared_file.open('rb') as file,mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ) as memory:
        assert len(memory)==C.sizeof(Shared)
        assert struct.unpack_from('II',memory)==(0x58333830,VERSION)
        check=Check(memory,args.output)
        def health(name):return check.read('a380x/xp12/xpkit/'+name)
        def controls(side):return check.snapshot()[1]['nav']['controls'][side]
        def status(side):return list(check.snapshot()[0].nd_status[side])
        def command(side,action):check.activate('a380x/xp12/nd/'+('first_officer' if side else 'captain')+'/'+action)
        def select(side,field,target):
            current=controls(side)[field]
            for _ in range(abs(target-current)):
                check.activate('a380x/xp12/efis/'+('first_officer' if side else 'captain')+'/'+field+('_up' if target>current else '_down'))
            eventually(lambda:controls(side)[field]==target)
        try:
            aircraft=base64.b64decode(check.read('sim/aircraft/view/acf_relative_path')).decode().rstrip('\0')
            assert aircraft=='Aircraft/A380X-XP12/A380X-XP12.acf',aircraft
            assert check.read('sim/flightmodel/failures/onground_any')==1
            assert check.read('sim/flightmodel/position/groundspeed')<.5
            assert check.read('sim/flightmodel2/misc/has_crashed')==0
            eventually(lambda:all(health(name)==1 for name in ('captain_active','first_officer_active','captain_nd_active','first_officer_nd_active','captain_nd_bound','first_officer_nd_bound')))
            check.record('Four native FlyByWire channels healthy; both ND devices bound to this cockpit')
            for side in (0,1):saved[side]=dict(controls(side))
            select(0,'mode',3);select(0,'range',3)
            eventually(lambda:status(0)[:2]==[3,3])
            check.record('Physical captain EFIS selectors drive ARC / 40 NM in original FlyByWire ND')
            for side in (0,1):select(side,'mode',4);select(side,'range',0)
            eventually(lambda:all(status(side)[3]>20 for side in (0,1)),timeout=60)
            state=check.snapshot()[1]
            assert state['mode']=='native' and state['nav']['airport']
            check.record('Both OANS maps load real local airport geometry',airport=state['nav']['airport'],features=[status(s)[3] for s in (0,1)])
            for side in (0,1):
                previous=status(side)[2]
                action='zoom_out' if previous==0 else 'zoom_in'
                expected=previous+1 if previous==0 else previous-1
                command(side,action);eventually(lambda:status(side)[2]==expected)
                command(side,'zoom_in' if action=='zoom_out' else 'zoom_out');eventually(lambda:status(side)[2]==previous)
            check.record('Independent native OANS zoom commands update original map and restore')
            previous=status(0)[7]
            command(0,'map_data');eventually(lambda:status(0)[7]!=previous)
            command(0,'map_data');eventually(lambda:status(0)[7]==previous)
            check.record('Native map-data command opens and closes the original airport panel')
            for side in (0,1):
                sequence,pixels=check.frame(side+2)
                assert sum(max(pixels[i:i+3])>30 for i in range(0,len(pixels),4))>2000
                png(args.output/('captain-oans.png' if side==0 else 'first-officer-oans.png'),pixels,ND_W,ND_H)
                check.record('Native OANS pixels captured from cockpit renderer',side=side,sequence=sequence)
            assert health('errors')==0
            check.proof['passed']=True
        except Exception as error:
            check.proof['passed']=False;check.proof['errors'].append(repr(error));raise
        finally:
            for side,control in saved.items():
                select(side,'mode',control['mode']);select(side,'range',control['range'])
                while controls(side)['zoom']!=control['zoom']:
                    before=controls(side)['zoom']
                    command(side,'zoom_in' if before>control['zoom'] else 'zoom_out')
                    eventually(lambda:controls(side)['zoom']!=before)
            check.proof['controls_restored']=True
            check.save();print(json.dumps(check.proof,indent=2))


if __name__=='__main__':main()
