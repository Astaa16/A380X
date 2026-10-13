#!/usr/bin/env python3
"""Regression checks for guarded full-turn fan export and inboard reversers."""
import json
from pathlib import Path
import re
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'blender'),str(ROOT/'tools')]
from normalize_engine_fan_rotation import normalize_text
from obj8_preview import parse_obj8
from prepare_engine_animation_subset import prepare_subset


def fan_fixture():
    lines=['I','800','OBJ','POINT_COUNTS 3 0 0 3',
        'VT 1 0 0 0 0 1 0 0','VT 0 1 0 0 0 1 1 0','VT 0 0 0 0 0 1 0 1',
        'IDX 0','IDX 1','IDX 2','ANIM_begin']
    for axis,angles in (((0,0,1),(0,90,180,-90,0)),((0,1,0),(0,0,0,0,0)),((1,0,0),(0,0,0,0,0))):
        lines.append('ANIM_rotate_begin '+' '.join(map(str,axis))+' sim/flightmodel2/engines/prop_rotation_angle_deg[0]')
        lines.extend(f'ANIM_rotate_key {v} {a}' for v,a in zip((0,90,180,270,360),angles))
        lines.append('ANIM_rotate_end')
    lines.extend(('TRIS 0 3','ANIM_end'))
    return '\n'.join(lines)+'\n'


class EngineAnimationTests(unittest.TestCase):
    def test_fan_full_turn_does_not_reverse_at_wrapped_key(self):
        text,count=normalize_text(fan_fixture())
        self.assertEqual(count,1)
        obj=parse_obj8(text.splitlines(),{'sim/flightmodel2/engines/prop_rotation_angle_deg[0]':225})
        matrix=obj.draws[0].matrix
        self.assertAlmostEqual(matrix[0],-2**-.5)
        self.assertAlmostEqual(matrix[4],-2**-.5)

    def test_normalization_is_idempotent_and_preserves_geometry(self):
        source=fan_fixture();text,_=normalize_text(source)
        self.assertEqual(normalize_text(text),(text,0))
        payload=lambda value:[line for line in value.splitlines() if line.startswith(('VT ','IDX ','TRIS ','POINT_COUNTS'))]
        self.assertEqual(payload(source),payload(text))

    def test_unexpected_rotation_is_rejected(self):
        text=fan_fixture().replace('ANIM_rotate_key 270 -90','ANIM_rotate_key 270 -80')
        with self.assertRaisesRegex(ValueError,'fixed-axis'):normalize_text(text)

    def test_unrelated_animation_is_untouched(self):
        text=fan_fixture().replace('prop_rotation_angle_deg','other_rotation')
        self.assertEqual(normalize_text(text),(text,0))

    def test_mappings_use_independent_native_fans_and_only_inboard_reversers(self):
        config=json.loads((ROOT/'config/animation_datarefs.json').read_text())
        match=lambda name:next(r for r in config['mappings'] if re.fullmatch(r['pattern'],name))
        for number in range(1,5):
            row=match(f'N1_{number}_anim')
            self.assertEqual(row['dataref'],f'sim/flightmodel2/engines/prop_rotation_angle_deg[{number-1}]')
            self.assertEqual((row['value_min'],row['value_max']),(0,360))
        self.assertEqual(match('thrust_rev_1')['dataref'],'sim/flightmodel2/engines/thrust_reverser_deploy_ratio[1]')
        self.assertEqual(match('thrust_rev_2')['dataref'],'sim/flightmodel2/engines/thrust_reverser_deploy_ratio[2]')
        self.assertFalse(any('thrust_reverser' in r.get('dataref','') and r['dataref'].endswith(('[0]','[3]')) for r in config['mappings']))

    def test_subset_preserves_nodes_clips_and_selected_geometry(self):
        document={'materials':[{'name':'engine'},{'name':'wing'}],
          'meshes':[{'primitives':[{'material':0,'attributes':{'POSITION':3,'TEXCOORD_0':4}},{'material':1,'attributes':{'POSITION':7}}]}],
          'nodes':[{'name':'fan','mesh':0,'children':[1]},{'name':'hinge'}],
          'animations':[{'name':'N1_1_anim','channels':[{'target':{'node':1}}]}]}
        subset=prepare_subset(document,{'engine'})
        self.assertEqual(subset['nodes'],document['nodes'])
        self.assertEqual(subset['animations'],document['animations'])
        self.assertEqual(subset['meshes'][0]['primitives'],document['meshes'][0]['primitives'][:1])
        self.assertEqual(len(document['meshes'][0]['primitives']),2)


if __name__=='__main__':unittest.main()
