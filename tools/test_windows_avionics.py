import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import install_windows_avionics as install

class WindowsInstallationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.package=self.root/'package';self.aircraft=self.root/'A380X-XP12';self.xplane=self.root/'XP'
        for p in (self.package,self.aircraft,self.xplane):p.mkdir()
        (self.aircraft/'A380X-XP12.acf').write_text('fixture')
        self.binary='plugins/A380XAvionics/win_x64/A380XAvionics.xpl'
        self.lua='FlyWithLua/Scripts/A380X_XP12_Ailerons.lua'
        for name in (self.binary,self.lua):
            p=self.package/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('new '+name)
        self.manifest={'platform':'Windows x86-64','files':{n:install.sha(self.package/n) for n in (self.binary,self.lua)},
            'required_aircraft_files':{'A380X-XP12.acf':install.sha(self.aircraft/'A380X-XP12.acf')}}
        (self.package/'windows-package.json').write_text(json.dumps(self.manifest))
    def tearDown(self):self.temp.cleanup()
    def test_hashes_and_exact_aircraft(self):
        self.assertEqual(install.verify(self.package,self.aircraft),self.manifest)
        (self.package/self.binary).write_text('modified')
        with self.assertRaisesRegex(ValueError,'hash mismatch'):install.verify(self.package,self.aircraft)
    def test_wrong_geometry(self):
        (self.aircraft/'A380X-XP12.acf').write_text('older model')
        with self.assertRaisesRegex(ValueError,'geometry/version mismatch'):install.verify(self.package,self.aircraft)
    def test_traversal_and_absolute_paths(self):
        for value in ('../outside','/root','C:/root','plugins/../../outside','plugins\\bad'):
            with self.subTest(value=value),self.assertRaises(ValueError):install.local(self.package,value)
    def test_symlink_escape(self):
        (self.package/'escape').symlink_to(self.xplane,target_is_directory=True)
        with self.assertRaises(ValueError):install.local(self.package,'escape/test')
    def test_targets_and_backups_preserve_other_files(self):
        plan=install.installation_plan(self.package,self.aircraft,self.xplane,self.manifest)
        self.assertEqual(len(plan),2)
        old=self.aircraft/self.binary;old.parent.mkdir(parents=True);old.write_text('old binary')
        unrelated=self.aircraft/'liveries/custom';unrelated.parent.mkdir();unrelated.write_text('user paint')
        backup=self.root/'backup';install.install_files(plan,backup)
        self.assertEqual(old.read_text(),'new '+self.binary)
        self.assertEqual((backup/'aircraft'/self.binary).read_text(),'old binary')
        self.assertEqual((self.xplane/'Resources/plugins'/self.lua).read_text(),'new '+self.lua)
        self.assertEqual(unrelated.read_text(),'user paint')
    def test_failure_rolls_back_existing_and_new_files(self):
        plan=install.installation_plan(self.package,self.aircraft,self.xplane,self.manifest)
        old=self.aircraft/self.binary;old.parent.mkdir(parents=True);old.write_text('old binary')
        real=install.copy_atomic
        def fail(source,target):
            if Path(source).resolve()==(self.package/self.lua).resolve():raise OSError('injected copy failure')
            return real(source,target)
        with patch.object(install,'copy_atomic',side_effect=fail),self.assertRaises(OSError):
            install.install_files(plan,self.root/'backup')
        self.assertEqual(old.read_text(),'old binary')
        self.assertFalse((self.xplane/'Resources/plugins'/self.lua).exists())
    def test_mac_no_global_install(self):
        plan=install.installation_plan(self.package,self.aircraft,None,self.manifest)
        self.assertEqual(len(plan),1)
        self.assertTrue(str(plan[0][1]).startswith(str(self.aircraft.resolve())))
    def test_platform_metadata_keeps_mac_records(self):
        platform='plugins/A380XAvionics/win_x64/windows-build-report.json'
        current='plugins/A380XAvionics/build-report.json';provider='avionics-providers/fbw-avionics-v1.json'
        old={'binaries':[{'file':'mac.xpl','sha256':'preserve-mac'},{'file':'win.xpl','sha256':'old-win'}]}
        new={'binaries':[{'file':'mac.xpl','sha256':'do-not-install'},{'file':'win.xpl','sha256':'new-win'}],
            'xp_kit_native_windows':{'windows_runtime_tested':False}}
        p=self.package/platform;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(new))
        p=self.aircraft/current;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(old))
        p=self.aircraft/provider;p.parent.mkdir();p.write_text(json.dumps({'files':[
            {'path':self.binary,'sha256':'old-win'},{'path':current,'sha256':'old-report'},
            {'path':'plugins/A380XAvionics/mac_x64/A380XAvionics.xpl','sha256':'preserve-mac'}]}))
        scratch=self.root/'scratch';scratch.mkdir()
        plan=install.metadata_plan(self.package,self.aircraft,self.manifest,scratch)
        self.assertEqual(len(plan),2)
        report=json.loads(plan[0][0].read_text());self.assertEqual(report['binaries'][0],old['binaries'][0])
        self.assertEqual(report['binaries'][1]['sha256'],'new-win')
        meta=json.loads(plan[1][0].read_text());self.assertEqual(meta['files'][2]['sha256'],'preserve-mac')
        self.assertEqual(meta['files'][0]['sha256'],self.manifest['files'][self.binary])
        self.assertEqual(meta['files'][1]['sha256'],install.sha(plan[0][0]))

if __name__=='__main__':unittest.main()
