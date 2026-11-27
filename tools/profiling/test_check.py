"""Mock-runner guards: fixed sources, per-binary import checks, failure paths.

No compiler, inspector or mock binary actually runs here; subprocess results
are fabricated so the runner's ordering and refusal rules can be exercised
without a toolchain. These tests check the runner, not the C sources.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import check
from common import ProfileError, load_json

SAFE_IMPORTS = 'DLL Name: KERNEL32.dll\nDLL Name: msvcrt.dll\n'
GRAPHICS_IMPORTS = 'DLL Name: KERNEL32.dll\nDLL Name: OPENGL32.dll\n'


@unittest.skipUnless(check.os.name == 'nt', 'Windows mock-runner import guard')
class CheckRunnerTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory(prefix='sightline-offline-runner-')
        self.addCleanup(temp.cleanup)
        self.root=Path(temp.name);self.source=self.root/'source'
        self.here=self.source/'tools/profiling'
        files=['src/gfx/sl_gfx_profile.c','src/gfx/sl_gfx_profile.h',
               'tools/profiling/native/sl_gfx_profile_testgl.c',
               'tools/profiling/native/sl_gfx_profile_testgl.h',
               'tools/profiling/native/test_sl_gfx_profile.c',
               'tools/profiling/native/regress_frame_identity.c']
        for name in files:
            path=self.source/name;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text('SYNTHETIC INPUT, NOT COMPILABLE')
        self.compiler=self.root/'compiler.exe';self.compiler.write_text('fake compiler')
        (self.root/'objdump.exe').write_text('fake inspector')
        self.calls=[];self.inspections=0
        self.imports=[SAFE_IMPORTS,SAFE_IMPORTS];self.exits={}
        for name,value in [('ROOT',self.source),('HERE',self.here)]:
            patch=mock.patch.object(check,name,value);patch.start();self.addCleanup(patch.stop)

    def fake_process(self,args,**kwargs):
        self.calls.append(args)
        name=Path(args[0]).name
        if name=='compiler.exe':
            return subprocess.CompletedProcess(args,0,'synthetic compile','')
        if name=='objdump.exe':
            text=self.imports[min(self.inspections,len(self.imports)-1)]
            self.inspections+=1
            return subprocess.CompletedProcess(args,0,text,'')
        return subprocess.CompletedProcess(args,self.exits.get(name,0),'synthetic mock result','')

    def executions(self):
        return [Path(c[0]).name for c in self.calls if Path(c[0]).suffix=='.exe'
                and Path(c[0]).name not in ('compiler.exe','objdump.exe')]

    def test_graphics_import_is_rejected_before_mock_execution(self):
        self.imports=[GRAPHICS_IMPORTS]
        out=self.root/'refused'
        with mock.patch.object(check.subprocess,'run',side_effect=self.fake_process),self.assertRaises(ProfileError):
            check.native_mocks(out,self.compiler)
        self.assertEqual(len(self.calls),2);self.assertEqual(self.executions(),[])
        self.assertEqual(load_json(out/'result.json')['status'],'FAILED')

    def test_fixed_mock_inputs_and_safe_imports_can_finish(self):
        out=self.root/'accepted'
        with mock.patch.object(check.subprocess,'run',side_effect=self.fake_process):
            result=check.native_mocks(out,self.compiler)
        self.assertEqual(result['status'],'MOCK_TESTS_PASSED')
        # Two binaries, each compiled, inspected and executed: six commands.
        self.assertEqual(len(self.calls),6);self.assertEqual(len(result['commands']),6)
        self.assertEqual(self.executions(),
                         ['renderer-profile-mock.exe','frame-identity-regression.exe'])
        self.assertEqual([b['main_source'] for b in result['binaries']],
                         ['native/test_sl_gfx_profile.c','native/regress_frame_identity.c'])
        for index in (0,3):
            self.assertIn('-DSL_GFX_PROFILE_TEST',self.calls[index])
        self.assertIn(str(self.here/'native/test_sl_gfx_profile.c'),map(str,self.calls[0]))
        self.assertIn(str(self.here/'native/regress_frame_identity.c'),map(str,self.calls[3]))
        self.assertNotIn(str(self.here/'native/regress_frame_identity.c'),map(str,self.calls[0]))

    def test_second_binary_imports_are_inspected_before_it_runs(self):
        # The first binary is clean; the second imports graphics. The guard must
        # stop the second launch even though the first already ran.
        self.imports=[SAFE_IMPORTS,GRAPHICS_IMPORTS]
        out=self.root/'second-refused'
        with mock.patch.object(check.subprocess,'run',side_effect=self.fake_process),self.assertRaises(ProfileError):
            check.native_mocks(out,self.compiler)
        self.assertEqual(len(self.calls),5)
        self.assertEqual(self.executions(),['renderer-profile-mock.exe'])
        record=load_json(out/'result.json')
        self.assertEqual(record['status'],'FAILED')
        self.assertIn('frame-identity-regression',record['error'])
        self.assertEqual([b['name'] for b in record['binaries']],['renderer-profile-mock'])

    def test_missing_regression_source_refuses_before_compiling(self):
        (self.source/'tools/profiling/native/regress_frame_identity.c').unlink()
        out=self.root/'incomplete'
        with mock.patch.object(check.subprocess,'run',side_effect=AssertionError('must not compile')):
            with self.assertRaises(ProfileError) as caught:
                check.native_mocks(out,self.compiler)
        self.assertIn('regress_frame_identity.c',str(caught.exception))
        self.assertEqual(self.calls,[]);self.assertFalse(out.exists())

    def test_second_binary_failure_propagates_and_preserves_outputs(self):
        self.exits={'frame-identity-regression.exe':1}
        out=self.root/'regression-failed'
        with mock.patch.object(check.subprocess,'run',side_effect=self.fake_process),self.assertRaises(ProfileError):
            check.native_mocks(out,self.compiler)
        self.assertEqual(len(self.calls),6)
        record=load_json(out/'result.json')
        self.assertEqual(record['status'],'FAILED')
        self.assertEqual(record['commands'][-1]['exit_code'],1)
        self.assertEqual([b['name'] for b in record['binaries']],['renderer-profile-mock'])
        for name in ['renderer-profile-mock-tests.txt','frame-identity-regression-tests.txt']:
            self.assertTrue((out/name).is_file())

    def test_existing_outputs_and_source_tree_are_never_reused(self):
        existing=self.root/'existing';existing.mkdir();sentinel=existing/'keep';sentinel.write_text('preserve')
        with mock.patch.object(check.subprocess,'run',side_effect=AssertionError('must not compile')):
            for out in [existing,self.source/'new-output']:
                with self.subTest(out=out),self.assertRaises(ProfileError):check.native_mocks(out,self.compiler)
        self.assertEqual(sentinel.read_text(),'preserve')


if __name__=='__main__':unittest.main()
