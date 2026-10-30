"""Isolated controller tests for the interactive web mockup, never the simulator."""
import json
from pathlib import Path
import unittest
from serve_xp_kit_mfd_trial import Engine


class PreviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.engine=Engine()

    def setUp(self):
        self.engine.lib.preview_reset(1)

    def state(self):return self.engine.state()

    def click(self,x,y,side=0):
        return self.engine.action({'action':'events','side':side,'events':[
            {'type':0,'x':x,'y':y},{'type':1},{'type':2}]})

    def key(self,name,side=0):
        return self.engine.action({'action':'events','side':side,'events':[{'type':5,'key':name}]})

    def text(self,text,side=0):
        for ch in text:self.key(ch,side)
        self.key('ent',side);self.engine.lib.preview_tick(.6)
        return self.state()

    def test_active_dropdown_opens_and_selection_changes_page(self):
        state=self.click(100,70)
        self.assertNotEqual(state['frames'][0]['menu_open'],0)
        texts=[c[10] for c in state['frames'][0]['commands'] if c[0]==6]
        self.assertIn('F-PLN',texts);self.assertIn('PERF',texts);self.assertIn('INIT',texts)
        state=self.click(80,152)
        self.assertEqual(state['frames'][0]['uri'],'fms/active/perf')
        self.assertEqual(state['frames'][0]['menu_open'],0)

    def test_system_dropdown_selects_fcu_backup(self):
        state=self.click(175,22)
        self.assertNotEqual(state['frames'][0]['menu_open'],0)
        state=self.click(80,170)
        self.assertEqual(state['frames'][0]['uri'],'fcubkup/afs')

    def test_position_and_data_dropdowns(self):
        self.click(290,70)
        state=self.click(280,224)  # IRS is fourth; REPORT is intentionally unavailable.
        self.assertEqual(state['frames'][0]['uri'],'fms/position/irs')
        self.click(680,70)
        state=self.click(680,260)  # AIRPORT is fifth; unsupported entries stay disabled.
        self.assertEqual(state['frames'][0]['uri'],'fms/data/airport')

    def test_secondary_index_dropdown(self):
        self.click(480,70)
        state=self.click(480,152)
        self.assertEqual(state['frames'][0]['uri'],'fms/sec/index/2')

    def test_outside_click_closes_menu(self):
        self.click(100,70)
        state=self.click(500,500)
        self.assertEqual(state['frames'][0]['menu_open'],0)

    def test_entries_are_validated_and_shared(self):
        self.key('perf')
        self.click(104,356)
        state=self.text('140')
        self.assertEqual(state['values']['v1'],140)
        right=[c[10] for c in state['frames'][1]['commands'] if c[0]==6]
        self.assertIn('140',right)
        self.click(104,356)
        state=self.text('12')
        self.assertEqual(state['values']['v1'],140,'Invalid speed must not overwrite V1')

    def test_pages_are_independent(self):
        state=self.key('init')
        self.assertEqual(state['frames'][0]['uri'],'fms/active/init')
        self.assertEqual(state['frames'][1]['uri'],'fms/active/perf')

    def test_performance_dropdown_changes_native_value(self):
        self.key('perf')
        state=self.click(390,565)  # PACKS selector
        self.assertNotEqual(state['frames'][0]['menu_open'],0)
        state=self.click(380,600)  # OFF / APU item
        self.assertEqual(state['values']['packs'],0)

    def test_du_off_and_reset_are_preview_only(self):
        state=self.engine.action({'action':'power','side':0,'on':False})
        self.assertFalse(state['frames'][0]['powered']);self.assertTrue(state['frames'][1]['powered'])
        state=self.click(100,70)
        self.assertEqual(state['frames'][0]['menu_open'],0)
        state=self.engine.action({'action':'reset','example':False})
        self.assertIsNone(state['values']['cost_index']);self.assertIsNone(state['values']['v1'])
        self.assertEqual(state['mode'],'isolated_preview')

    def test_malformed_input_is_rejected_before_events_are_applied(self):
        for data in ({'action':'events','side':5},
                     {'action':'events','events':[{'type':0,'x':float('nan')}]},
                     {'action':'events','events':[{'type':5,'key':'x'*30}]},
                     {'action':'navigate','uri':'x'*64}):
            with self.assertRaises(ValueError):self.engine.action(data)


if __name__=='__main__':unittest.main()
