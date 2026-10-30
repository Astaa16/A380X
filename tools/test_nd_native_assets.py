"""Native ND packaging/geometry checks and explicit offline host-test fixture."""
import json
from pathlib import Path
import sys
import unittest
from nd_cockpit_devices import augment, ND_DEVICES
from validate_aircraft import cockpit_device_quads

ROOT=Path(__file__).resolve().parents[1]


def fixture(destination):
    state=json.loads((ROOT/'build/xp-kit-cockpit-dev/fixture.json').read_text())
    values=state['values'];values['sim/flightmodel/failures/onground_any']=1
    for side in ('captain','first_officer'):values[f'a380x/xp12/du/{side}_nd']=1
    for n in (1,2,3):
        values[f'a380x/xp12/adirs/ir{n}_latitude_deg']=43.348289
        values[f'a380x/xp12/adirs/ir{n}_longitude_deg']=77.021213
    state['nav']={'lat':43.348289,'lon':77.021213,'positionValid':True,'magVar':0,'tas':8,'groundSpeed':8,
        'windDir':285,'windSpeed':10,'zulu':32000,'selectedHeading':55,'selectedAltitude':12000,
        'controls':[{'mode':3,'range':3,'zoom':1,'powered':1},{'mode':4,'range':0,'zoom':1,'powered':1}],
        'airports':[{'idarpt':'UAAA','iata':'ALA','name':'Almaty Intl','coordinates':{'lat':43.355555556,'lon':77.043888889},'elev':679.4}],
        'airport':'UAAA','route':[],'symbols':[],'to':None,'radios':[],
        'pointers':[{'x':384,'y':384,'inside':0,'events':[]},{'x':430,'y':440,'inside':1,'events':[[1,0,430,440,0]]}]}
    destination.write_text(json.dumps(state))


class DeviceTests(unittest.TestCase):
    def test_geometry_preserves_existing_ranges_and_adds_full_uv_twins(self):
        for relative,touch in [('objects/interior/cockpit-controls/a380x_cockpit_controls.obj',True),
                               ('objects/interior/cockpit-panels/a380x_mfd_screens.obj',False)]:
            original=(ROOT/'output/A380X-XP12'/relative).read_text()
            result=augment(original,touch)
            self.assertEqual(augment(result,touch),result)
            for device in ND_DEVICES:
                self.assertEqual(cockpit_device_quads(result,touch)[device],[(0,0,1,1)])
            # Every pre-existing draw remains at its original index range.
            old=[line for line in original.splitlines() if line.startswith('TRIS')]
            new=[line for line in result.splitlines() if line.startswith('TRIS')]
            self.assertEqual(new[:len(old)],old)

    def test_native_payload_has_no_demo_hook_or_http_provider(self):
        html=(ROOT/'build/xp-kit-nd-native-package-probe/nd.html').read_text()
        self.assertIn('__a380Nd?.onUpdate',html)
        self.assertNotIn('__ndDemo',html)
        self.assertNotIn('/airports?q=',html)
        self.assertIn('connect-src \'none\'',html)
        self.assertTrue('"#ffff00"' in html or '#ffff00' in html,'Native bundle must retain yellow cursor paint')


if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--fixture':fixture(Path(sys.argv[2]))
    else:unittest.main()
