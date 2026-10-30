"""Small deterministic tests; no simulator process or aircraft writes."""
import json
import math
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import urlopen, Request

from nd_airport_preview import (project, unproject, flatten, convert, airport_blocks, scenery_sources, load_airports, amdb,
                                CURVE_TOLERANCE_M, CURVE_MAX_DEPTH, _curve_endpoints)
from serve_xp_kit_nd_trial import Handler


FIXTURE = '''1 100 0 0 TEST Test Airport
1302 icao_code KTES
1302 iata_code TES
1302 datum_lat 43.0
1302 datum_lon 77.0
100 45 1 0 0.25 1 2 1 09 43.0 77.0 100 0 3 0 0 0 27 43.0 77.02 0 0 3 0 0 0
110 1 0.25 0 Main pavement
111 43.0 77.0
111 43.001 77.0
111 43.001 77.001
113 43.0 77.001
111 43.0002 77.0002
111 43.0002 77.0008
111 43.0008 77.0008
113 43.0008 77.0002
120 Centerline
112 43.0 77.0 43.0005 77.0005 1
116 43.001 77.001 43.0015 77.0005
1300 43.0005 77.0005 90 gate jets Gate A1
'''.splitlines()


class GeometryTests(unittest.TestCase):
    def setUp(self):
        self.airport = convert('KTES', FIXTURE, Path('/fixture/apt.dat'))

    def test_projection_roundtrip(self):
        for lat, lon in [(43,77), (43.02,77.04), (-20,179.99), (80,-170)]:
            origin = (lat-.01, lon-.01)
            restored = unproject(*project(lat,lon,origin),origin)
            self.assertAlmostEqual(restored[0],lon,places=8)
            self.assertAlmostEqual(restored[1],lat,places=8)

    def test_runway_dimensions_and_displacement(self):
        self.assertEqual(self.airport['runways'][0]['width_m'],45)
        length = self.airport['runways'][0]['length_m']
        self.assertTrue(1600 < length < 1700)
        thresholds = self.airport['features']['runwaythreshold']['features']
        self.assertAlmostEqual(math.hypot(*thresholds[0]['geometry']['coordinates']),100,places=5)
        self.assertEqual(thresholds[0]['properties']['lda'],round(length-100))
        self.assertEqual(thresholds[0]['properties']['idthr'],'09')

    def test_hole_preserved(self):
        rings = self.airport['features']['taxiwayelement']['features'][0]['geometry']['coordinates']
        self.assertEqual(len(rings),2)
        for ring in rings:
            self.assertEqual(ring[0],ring[-1])
            self.assertEqual(len(ring),5)

    def test_bezier_and_open_end(self):
        line = self.airport['features']['taxiwayguidanceline']['features'][0]['geometry']['coordinates']
        self.assertGreater(len(line),9)
        self.assertEqual(line[0],project(43,77,self.airport['origin']))
        self.assertEqual(line[-1],project(43.001,77.001,self.airport['origin']))
        self.assertNotEqual(line[0],line[-1])
        self.assertEqual(flatten([],self.airport['origin'],False),[])

    def test_adaptive_curve_error_is_bounded_in_metres(self):
        # A 1 km cubic with a tight bend needs more than eight chords. Check
        # geometric error independently against a dense analytical reference.
        controls = [[0, 0], [0, 1000], [1000, 1000], [1000, 0]]
        line = [controls[0], *list(_curve_endpoints(*controls, CURVE_TOLERANCE_M, CURVE_MAX_DEPTH))]
        self.assertGreater(len(line),9)

        def distance(point, first, last):
            dx, dy = last[0] - first[0], last[1] - first[1]
            span = dx * dx + dy * dy
            ratio = max(0, min(1, ((point[0]-first[0])*dx + (point[1]-first[1])*dy) / span)) if span else 0
            return math.hypot(point[0]-first[0]-ratio*dx, point[1]-first[1]-ratio*dy)

        worst = 0
        for step in range(2001):
            t = step / 2000
            point = [(1-t)**3*controls[0][j] + 3*(1-t)**2*t*controls[1][j] +
                     3*(1-t)*t*t*controls[2][j] + t**3*controls[3][j] for j in (0,1)]
            worst = max(worst, min(distance(point, a, b) for a, b in zip(line, line[1:])))
        self.assertLessEqual(worst, CURVE_TOLERANCE_M)
        self.assertEqual(line[-1],controls[-1])

    def test_adaptive_straight_handles_do_not_oversample_or_lose_overshoot(self):
        straight = list(_curve_endpoints([0,0], [30,0], [70,0], [100,0], CURVE_TOLERANCE_M, CURVE_MAX_DEPTH))
        self.assertEqual(straight,[[100,0]])
        # Distance to the infinite chord alone would collapse this out-and-back.
        overshoot = list(_curve_endpoints([0,0], [300,0], [300,0], [100,0], CURVE_TOLERANCE_M, CURVE_MAX_DEPTH))
        self.assertGreater(max(point[0] for point in overshoot),200)
        self.assertEqual(overshoot[-1],[100,0])

    def test_adaptive_tessellation_is_bounded_for_pathological_handles(self):
        points = list(_curve_endpoints([0,0], [0,1e9], [1e9,1e9], [1e9,0], 1e-9, 3))
        self.assertEqual(len(points),2**3)
        for kwargs in ({'tolerance_m':0}, {'tolerance_m':float('nan')}, {'max_depth':CURVE_MAX_DEPTH+1}):
            with self.assertRaises(ValueError):
                flatten([(43,77),(43.001,77.001)],(43,77),False,**kwargs)

    def test_duplicate_vertices_removed_with_exact_closure_and_open_end(self):
        nodes = [(43,77),(43,77),(43.001,77),(43.001,77),(43.001,77.001)]
        line = flatten(nodes,(43,77),False)
        self.assertEqual(len(line),3)
        self.assertEqual(line[-1],project(*nodes[-1],(43,77)))
        ring = flatten(nodes+[(43,77),(43,77)],(43,77),True)
        self.assertEqual(len(ring),4)
        self.assertEqual(ring[0],ring[-1])
        self.assertTrue(all(math.dist(a,b) > 0 for a,b in zip(ring,ring[1:])))

    def test_source_paint_styles_and_names_remain_distinct(self):
        rows = FIXTURE + ['120 Sourced centerline','111 43.0 77.0 1 101',
                          '111 43.0001 77.0001 7','111 43.0002 77.0002 57',
                          '115 43.0003 77.0003']
        data = convert('KTES',rows,Path('/fixture/apt.dat'))
        lines = data['features']['taxiwayguidanceline']['features'][1:]
        self.assertEqual([line['properties']['source_line_codes'] for line in lines],[[1],[7],[57]])
        self.assertTrue(all(line['properties']['source_kind'] == 'xplane_painted_centerline' for line in lines))
        self.assertTrue(all(line['properties']['source_name'] == 'Sourced centerline' for line in lines))
        self.assertEqual(data['features']['taxiwayelement']['features'][0]['properties']['source_name'],'Main pavement')
        # Source descriptions are not promoted to operational taxiway IDs.
        self.assertTrue(all('idlin' not in line['properties'] for line in lines))

    def test_metadata_stands_and_wgs84(self):
        self.assertEqual(self.airport['catalogue']['name'],'Test Airport')
        self.assertEqual(self.airport['stands'][0]['name'],'Gate A1')
        self.assertEqual(len(self.airport['source_block_sha256']),64)
        arp = amdb(self.airport,'EPSG:4326')['aerodromereferencepoint']['features'][0]['geometry']['coordinates']
        self.assertEqual(arp,[77,43])
        self.assertEqual(self.airport['features']['aerodromereferencepoint']['features'][0]['geometry']['coordinates'],[0,0])
        json.dumps(amdb(self.airport,'EPSG:4326'),allow_nan=False)

    def test_roadway_and_unknown_styles_are_not_taxi_routes(self):
        rows = FIXTURE + ['120 Road markings','111 43.0 77.0 20','115 43.001 77.001',
                          '120 Unknown marking','111 43.0 77.0 25','115 43.001 77.001']
        data = convert('KTES',rows,Path('/fixture/apt.dat'))
        self.assertEqual(len(data['features']['taxiwayguidanceline']['features']),1)
        # A split handle must not introduce an out-and-back curve.
        nodes = [(43,77,43.0005,77.0005),(43,77),(43.001,77.001)]
        self.assertEqual(len(flatten(nodes,(43,77),False)),2)

    def test_alias_and_eof(self):
        with tempfile.TemporaryDirectory(prefix='a380-nd-parser-') as directory:
            file = Path(directory)/'apt.dat'
            file.write_text('I\n1200 Generated test\n'+'\n'.join(FIXTURE))
            blocks = list(airport_blocks(file,{'KTES'}))
            self.assertEqual(blocks[0][0],'KTES')
            self.assertEqual(blocks[0][1],FIXTURE)

    def test_whitespace_and_comments(self):
        rows = [row.replace('100 45','  100\t45') for row in FIXTURE]+['# a scenery comment']
        self.assertEqual(convert('KTES',rows,Path('/fixture/apt.dat'))['runways'],self.airport['runways'])

    def test_atc_taxi_geometry_excludes_runway_truck_and_dangling_edges(self):
        rows = FIXTURE+['1201 43.0 77.0 both 0','1201 43.001 77.001 both 1',
                        '1202 0 1 twoway taxiway_D A','1202 0 1 twoway runway 09/27',
                        '1206 0 1 twoway Road','1202 0 999 twoway taxiway A']
        data = convert('KTES',rows,Path('/fixture/apt.dat'))
        edges = [f for f in data['features']['taxiwayguidanceline']['features'] if f['properties'].get('source_kind') == 'xplane_atc_taxi_edge']
        self.assertEqual(len(edges),1)
        self.assertEqual(edges[0]['properties']['idlin'],'A')
        self.assertEqual(edges[0]['geometry']['coordinates'][0],[0,0])

    def test_scenery_priority_and_disabled_pack(self):
        with tempfile.TemporaryDirectory(prefix='a380-nd-priority-') as directory:
            root = Path(directory)
            for pack in ['Custom Scenery/First','Custom Scenery/Disabled','Global Scenery/Global Airports']:
                folder = root/pack/'Earth nav data'
                folder.mkdir(parents=True)
                (folder/'apt.dat').write_text('\n'.join(FIXTURE)+'\n99\n')
            (root/'Custom Scenery/scenery_packs.ini').write_text(
                'SCENERY_PACK Custom Scenery/First/\nSCENERY_PACK_DISABLED Custom Scenery/Disabled/\nSCENERY_PACK *GLOBAL_AIRPORTS*\n')
            sources = scenery_sources(root)
            self.assertEqual(len(sources),2)
            airports = load_airports(('KTES',),root)
            self.assertIn('Custom Scenery/First',airports['KTES']['source'])
            with self.assertRaises(FileNotFoundError):
                load_airports(('MISSING',),root)

    def test_http_read_only_boundary(self):
        server = ThreadingHTTPServer(('127.0.0.1',0),Handler)
        server.airports = {'KTES':self.airport}
        worker = threading.Thread(target=server.serve_forever,daemon=True)
        worker.start()
        base = f'http://127.0.0.1:{server.server_port}'
        try:
            with urlopen(base+'/airports?q=tes') as response:
                self.assertEqual(len(json.load(response)),1)
                self.assertEqual(response.headers['Cache-Control'],'no-store')
                self.assertIn("connect-src 'self'",response.headers['Content-Security-Policy'])
            with urlopen(base+'/geometry/KTES') as response:
                self.assertNotIn('features',json.load(response))
            for target, method, status in [('/bridge.js','POST',501),('/snapshot','GET',404),
                                           ('/ND/../../../native/avionics/a380x_avionics.c','GET',404),('/geometry/NONE','GET',404)]:
                with self.assertRaises(HTTPError) as error:
                    urlopen(Request(base+target,method=method))
                self.assertEqual(error.exception.code,status)
        finally:
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == '__main__':
    unittest.main()
