"""Check cockpit/provider names against the native registration source."""
import re
import unittest
from pathlib import Path
from avionics_provider import (AVIONICS_DATAREFS, FCU_COMMANDS, FCU_DATAREFS,
                               FCU_LIGHT_DATAREFS, FCU_MOTION_DATAREFS, KCCU_ACTIONS, KCCU_COMMANDS, KCCU_KEYS,
                               MFD_COMMANDS)
from build_cockpit_controls import FCU_ANNUNCIATORS, load_controls


PROJECT = Path(__file__).resolve().parents[1]


def configured_commands(prefixes: tuple[str, ...]) -> set[str]:
    controls = load_controls(PROJECT / 'config/cockpit_controls.json')
    return {name for control in controls for name in (
        control.command, control.command_up, control.command_down, control.command_push, control.command_pull)
        if name and name.startswith(prefixes)}


class FcuAdapterContractTests(unittest.TestCase):
    def test_mechanical_and_light_outputs_have_separate_provider_contracts(self):
        source = (PROJECT / 'native/avionics/a380x_fcu_motion.inc').read_text()
        buttons = tuple('a380x/xp12/fcu/' + name + '_pressed'
                        for name in re.findall(r'FM_BUTTON\("([^"]+)"', source))
        knobs = tuple('a380x/xp12/fcu/' + name + '_knob_' + axis
                      for name in re.findall(r'FM_KNOB\("([^"]+)"', source)
                      for axis in ('rotation', 'axial'))
        self.assertEqual((*buttons, *knobs, 'a380x/xp12/fcu/altitude_selector'), FCU_MOTION_DATAREFS)
        self.assertEqual(tuple(row[2] for row in FCU_ANNUNCIATORS), FCU_LIGHT_DATAREFS)
        self.assertTrue(set(FCU_DATAREFS).isdisjoint(FCU_MOTION_DATAREFS))
        self.assertTrue(set(FCU_DATAREFS).isdisjoint(FCU_LIGHT_DATAREFS))
        self.assertTrue(set(FCU_MOTION_DATAREFS).isdisjoint(FCU_LIGHT_DATAREFS))
        for name in (*FCU_MOTION_DATAREFS, *FCU_LIGHT_DATAREFS):
            self.assertEqual(AVIONICS_DATAREFS.count(name), 1)

    def test_native_registration_tables_match_packaged_provider(self):
        adapter = (PROJECT / 'native/avionics/a380x_fcu_adapter.inc').read_text()
        self.assertEqual(tuple('a380x/xp12/fcu/' + name for name in re.findall(r'FCUC\("([^"]+)"', adapter)), FCU_COMMANDS)
        self.assertEqual(tuple('a380x/xp12/fcu/' + name for name in re.findall(r'FCUR\("([^"]+)"', adapter)), FCU_DATAREFS)
        # AP disconnect and the two LS toggles are assignable only; the
        # physical LS buttons are EFIS-side controls.
        self.assertEqual(configured_commands(('a380x/xp12/fcu/',)), set(FCU_COMMANDS) - {
            'a380x/xp12/fcu/ap_disconnect', 'a380x/xp12/fcu/ls_captain_toggle',
            'a380x/xp12/fcu/ls_first_officer_toggle'})


class KccuAdapterContractTests(unittest.TestCase):
    def test_cockpit_kccu_usage_matches_provider_with_popout_assignable_only(self):
        popup = {command for command in MFD_COMMANDS if command.endswith('/popup')}
        popout = {command for command in MFD_COMMANDS if command.endswith('/popout')}
        self.assertEqual(len(KCCU_COMMANDS), 136)
        self.assertEqual(len(popup), 2)
        used = configured_commands(('a380x/xp12/kccu/', 'a380x/xp12/mfd/'))
        self.assertEqual(used, set(KCCU_COMMANDS) | popup)
        self.assertFalse(used & popout)

    def test_native_mfd_registration_follows_the_provider_contract(self):
        # build.py compares the adapter test's --contracts C/D/V lines with
        # KCCU_COMMANDS | MFD_COMMANDS, the dataref tuples and MFD_DEVICES
        # (spec 09 section 1.3 f); this test guards that wiring statically.
        adapter = PROJECT / 'native/avionics/a380x_mfd_adapter.inc'
        if not adapter.is_file():
            self.skipTest('pending WP-J native integration: native/avionics/a380x_mfd_adapter.inc is absent')
        source = adapter.read_text(encoding='utf-8', errors='replace')
        # Drop comments and character literals (e.g. '"') before pairing string quotes.
        source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
        source = re.sub(r"'(?:[^'\\\n]|\\.)+'", "''", source)
        literals = set(re.findall(r'"((?:[^"\\\n]|\\.)*)"', source))
        missing = [name for name in (*KCCU_KEYS, *KCCU_ACTIONS, 'popup', 'popout')
                   if name not in literals and not any(literal.endswith('/' + name) for literal in literals)]
        self.assertEqual(missing, [], 'KCCU/MFD command names missing from the native registration table')
        build = (PROJECT / 'native/avionics/build.py').read_text(encoding='utf-8')
        missing_contract = [token for token in ('KCCU_COMMANDS', 'MFD_COMMANDS', 'MFD_DEVICES',
                                                'MFD native registry disagrees with cockpit provider contract')
                            if token not in build]
        self.assertEqual(missing_contract, [],
                         'native/avionics/build.py does not yet check the MFD adapter --contracts output '
                         'against the provider tuples (spec 09 section 1.3 f)')


if __name__ == '__main__':
    unittest.main()
