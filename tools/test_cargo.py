import json
from pathlib import Path
import re
import struct
import tempfile
import unittest

from build_cargo_geometry import door_specs, hold_objects
from install_cargo import append_attachments, CARGO_ATTACHMENTS


class CargoTests(unittest.TestCase):
    def acf(self):
        return ('I\n1200 Version\nACF\nP acf/_m_max 1200000\n'
                'P _obja/0/_obj_flags 13\n'
                'P _obja/0/_v10_att_file_stl exterior/fuselage/a380x_fuse1.obj\n'
                'P _obja/0/_v10_att_x_acf_prt_ref 0.123\n'
                'P _obja/0/_v10_att_psi_ref 180\n'
                'P _obja/count 1\nP acf/_Vne_kts 340\nEND\n')

    def test_door_channels_follow_all_seventeen_existing_doors(self):
        self.assertEqual([r['index'] for r in door_specs()],[17,18,19])
        mapping=json.loads((Path(__file__).resolve().parents[1]/'config/animation_datarefs.json').read_text())
        for name,index in [('fwd_door_cargo',17),('aft_door_cargo',18)]:
            rows=[r for r in mapping['mappings'] if re.fullmatch(r['pattern'],name)]
            self.assertEqual(len(rows),1)
            self.assertEqual(rows[0]['dataref'],f'sim/flightmodel2/misc/door_open_ratio[{index}]')
            self.assertNotIn('static_fraction',rows[0])

    def test_attachments_preserve_every_flightmodel_and_existing_object_property(self):
        original=self.acf();after=append_attachments(original)
        for path in CARGO_ATTACHMENTS:self.assertEqual(after.count(path),1)
        self.assertIn('P _obja/count 4\n',after)
        self.assertEqual([l for l in original.splitlines() if not l.startswith('P _obja/')],
                         [l for l in after.splitlines() if not l.startswith('P _obja/')])
        for i in (1,2,3):
            self.assertIn(f'P _obja/{i}/_v10_att_x_acf_prt_ref 0.123\n',after)
            self.assertIn(f'P _obja/{i}/_v10_att_psi_ref 180\n',after)
        self.assertTrue(after.endswith('END\n'))

    def test_repeat_install_is_idempotent(self):
        first=append_attachments(self.acf());self.assertEqual(first,append_attachments(first))

    def test_partial_install_or_unknown_transform_is_rejected(self):
        with self.assertRaises(ValueError):
            append_attachments(self.acf().replace('P _obja/count 1',f'P _obja/1/_v10_att_file_stl {CARGO_ATTACHMENTS[0]}\nP _obja/count 2'))
        with self.assertRaises(ValueError):append_attachments(self.acf().replace('a380x_fuse1.obj','unknown.obj'))

    def test_authored_hold_geometry_keeps_units_winding_uvs_and_independent_hardware(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);data=bytearray();views=[];accessors=[]
            def accessor(rows,kind,fmt,width):
                at=len(data)
                for row in rows:data.extend(struct.pack('<'+fmt*width,*row))
                views.append(dict(buffer=0,byteOffset=at,byteLength=len(data)-at))
                accessors.append(dict(bufferView=len(views)-1,componentType=5126 if fmt=='f' else 5123,count=len(rows),type=kind))
                return len(accessors)-1
            pos=accessor([(0,0,0),(1,0,0),(0,0,1)],'VEC3','f',3)
            norm=accessor([(0,1,0)]*3,'VEC3','f',3)
            uv=accessor([(0,.25),(1,.25),(0,.75)],'VEC2','f',2)
            idx=accessor([(0,),(1,),(2,)],'SCALAR','H',1)
            materials=['Cargo_FWD','Cargo_FWD_DECAL','A380_EXTERIOR_MISC']
            nodes=[dict(name=n,mesh=m,translation=[0,0,z]) for n,m,z in (
                ('forward',0,25),('aft',0,-12),('bulk',0,-17),('decal',1,25),
                ('MAIN FUSELAGE.117',2,25),('MAIN FUSELAGE.102',2,-12))]
            meshes=[dict(primitives=[dict(material=m,attributes={'POSITION':pos,'NORMAL':norm,'TEXCOORD_0':uv},indices=idx)]) for m in range(3)]
            document=dict(nodes=nodes,meshes=meshes,materials=[dict(name=m) for m in materials],
                          accessors=accessors,bufferViews=views,buffers=[dict(uri='mesh.bin',byteLength=len(data))])
            (root/'mesh.bin').write_bytes(data);(root/'source.gltf').write_text(json.dumps(document))
            (root/'materials.json').write_text(json.dumps(dict(exports=[dict(materials=[m],texture='cargo.png') for m in materials])))
            targets=[dict(name=d,source_nodes=[n],kind='rotation',pivot=(0,0,0),axis=(0,0,1),
                          values=[0,1],poses=[0,120],dataref=f'sim/flightmodel2/misc/door_open_ratio[{i}]')
                     for d,n,i in [('FWD','MAIN FUSELAGE.117',17),('AFT','MAIN FUSELAGE.102',18)]]
            output={name:(text,report) for name,text,report in hold_objects(root/'source.gltf',root/'materials.json',targets)}
            text,report=output['a380x_cargo_holds.obj']
            self.assertEqual(report['triangles'],3)
            self.assertEqual(report['station_triangles'],dict(FWD=1,AFT=1,BULK=1))
            vertex=next(l for l in text.splitlines() if l.startswith('VT\t')).split()
            self.assertEqual(float(vertex[3]),25)
            self.assertEqual(float(vertex[8]),.75)
            self.assertIn('IDX\t0\nIDX\t1\nIDX\t2',text)
            hardware=output['a380x_cargo_hardware.obj'][0]
            for i in (17,18):self.assertIn(f'door_open_ratio[{i}]',hardware)
            self.assertNotIn('door_open_ratio[19]',hardware)


if __name__=='__main__':unittest.main()
