"""Synthetic-only readiness tests; no subprocess, graphics API or recording."""
import contextlib
import copy
import csv
import io
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import tempfile
import unittest
from unittest import mock

import readiness as R
from common import ProfileError, pin, write_json


class ReadinessTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='sightline-readiness-unit-')
        self.root=Path(self.temp.name).resolve()
        self.assertTrue(self.root.is_relative_to(Path(tempfile.gettempdir()).resolve()))
        self.addCleanup(self.temp.cleanup)
        self.guard=mock.patch.object(subprocess,'Popen',side_effect=AssertionError('native process forbidden'))
        self.guard.start();self.addCleanup(self.guard.stop)
        self.osguard=mock.patch.object(os,'system',side_effect=AssertionError('shell forbidden'))
        self.osguard.start();self.addCleanup(self.osguard.stop)
        self.source=self.root/'source';(self.source/'src/platform').mkdir(parents=True)
        (self.source/'src/platform/sl_main.c').write_text('getenv("SL_RUN"); getenv("SL_RUN_DIR");\n'
            "if (off != NULL && *off == '0') return;\n"
            'if (g_run_dir[0] == 0) return;\n'
            'snprintf(path, sizeof path, "%s/events", g_run_dir);\n'
            'far-fog %.1f -> %.1f env-rgb\n',encoding='utf-8')
        self.write_shim()
        self.harness=self.root/'run.ps1'
        self.harness.write_text("SL_RUN = '0'\nforeach ($kv in ($ExtraKv -split ';'))\n",encoding='utf-8')
        # Fixture adapter only. The real pinned runner hash remains unchanged.
        self.adapter=mock.patch.object(R,'KNOWN_HARNESS',pin(self.harness)['sha256'])
        self.adapter.start();self.addCleanup(self.adapter.stop)
        self.a=self.root/'baseline.bin';self.a.write_bytes(b'SYNTHETIC NOT EXECUTABLE A')
        self.b=self.root/'candidate.bin';self.b.write_bytes(b'SYNTHETIC NOT EXECUTABLE B')
        self.input=self.root/'input.txt';self.input.write_text('synthetic ordinary input')
        self.pose=self.root/'pose.txt';self.pose.write_text('synthetic pose')
        self.policy=R.suite();self.policy['window']={'lo':1,'hi':9,'step':2}
        for p in self.policy['profiles'].values():
            p.update(width=2,height=2)
            p['budget'].update(mean_ms=10,p99_ms=10,mean_noise_floor_ms=.1,p99_noise_floor_ms=.25)
        for scene in self.policy['scenes']:
            scene['captures']={'first':0,'last':2 if scene['events_required'] else 0,'every':1,'phases':[0]}
        self.suite=self.root/'suite.json';self.save(self.suite,self.policy)
        self.n=0

    def write_shim(self,order=None,extra=''):
        """The synthetic counter fixture plus a frame pump in the given order.

        Shaped like the real block only where the guard reads it: the two
        anchors and the four statements between them. Statement order is the
        variable under test; nothing here is compiled.
        """
        body=''.join('            %s\n'%x for x in (order or R.PUMP_ORDER))
        (self.source/'src/platform/sl_ultra_shim.c').write_text(
            'sl_watch_frame((unsigned) sl_vi_advances);\n'
            '        {\n'
            '            int held = sl_gfx_active() && sl_pres_hold();\n'
            '            if (held) sl_gfx_hold_present();\n'
            +body+extra+
            '        }\n'
            '        /* limit 0 means run until the window is closed. */\n',encoding='utf-8')

    def save(self,path,data):
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8',newline='\n')

    def plan(self,scene='facility-tanks',binary=None,mode='clean'):
        self.n+=1;out=self.root/('run-%03d'%self.n)
        env={'SL_RUN':'0'};overrides={}
        if scene=='facility-gas':overrides={'SL_RUN':'1','SL_RUN_DIR':str(out/'native-events')}
        if mode=='profile':overrides['SL_RENDER_PROFILE']=str(out/'metrics.csv')
        bindings={'measurement_mode':mode,'workload':{'stage':scene,'pose_id':'synthetic','seed':'0'},'binary':str(binary or self.a),'source_root':str(self.source),'machine_id':'synthetic-machine',
                  'input':str(self.input),'pose':str(self.pose),'harness':str(self.harness),'output_root':str(out),
                  'base_environment':env,'overrides':overrides,'counter_mapping_reviewed':True,
                  'metric_capabilities':['host_frames','cpu_submit','gpu_elapsed']}
        return R.make_plan(self.suite,scene,'1080p60',bindings)

    def run_fixture(self,plan,mean=5):
        root=Path(plan['output_root']);root.mkdir()
        (root/'pres.txt').write_text('# SYNTHETIC\n'+'\n'.join('%s R %d 1.0 %d 00000000'%(i*mean,f,i) for i,f in enumerate((1,3,5,7)))+'\n',encoding='utf-8')
        gas=plan['scene']['events_required'];caps=[]
        for i,read in enumerate((0,2,4) if gas else (0,)):
            name='shot-%d.ppm'%i;(root/name).write_bytes(b'P6\n2 2\n255\n'+b'\x40'*12)
            caps.append({'path':name,'frame':i if gas else 0,'read':read})
        events=None
        if gas:
            events='native-events/one/events';p=root/events;p.parent.mkdir(parents=True)
            p.write_text('0 env-rgb 16,32,16\n0 far-fog -1.0 -> 5000.0\n'
                         '2 env-rgb 32,64,32\n2 far-fog 5000.0 -> 3000.0\n'
                         '4 env-rgb 64,128,64\n4 far-fog 3000.0 -> 1000.0\n',encoding='utf-8')
        metrics=None
        if plan['measurement_mode']=='profile':
            metrics='metrics.csv'
            with (root/metrics).open('w',encoding='utf-8',newline='') as f:
                w=csv.writer(f);w.writerow(R.METRIC_FIELDS)
                for frame in (1,3,5,7):w.writerow([frame,'frame',.5,1.5,'ready'])
        receipt={'schema':'sightline-performance-receipt/1','plan_sha256':R.identity(plan),
                 'binary_sha256':plan['binary']['sha256'],'machine_id':plan['machine_id'],'resolution':[2,2],
                 'normal_exit':True,'foreground_qualified':True,'effective_environment':plan['effective_environment'],
                 'captures':caps,'events_path':events,'metrics_path':metrics,'evidence_kind':'synthetic',
                 'primary_work_area':[0,0,100,100],'workload':plan['workload']}
        self.save(root/'receipt.json',receipt)
        (root/'launch.txt').write_text('exit_code 0\ntimed_out False\n',encoding='utf-8')
        (root/'foreground-log.txt').write_text('iso_time elapsed_s fg_hwnd fg_pid fg_is_game visible iconic unobscured rect_l rect_t rect_r rect_b game_alive\n'
            'synthetic 6.1 1 1 True True False True 1 1 3 3 True\n'
            'synthetic 7.1 1 1 True True False True 1 1 3 3 True\n',encoding='utf-8')
        return root

    def quartet(self,scene='facility-tanks',means=(5,4,4.01,5.01),mode='clean'):
        plans=[self.plan(scene,b,mode) for b in (self.a,self.b,self.b,self.a)]
        roots=[self.run_fixture(p,m) for p,m in zip(plans,means)]
        return plans,roots

    def test_catalog_valid_and_five_distinct_workloads(self):
        self.assertEqual(len(R.suite()['scenes']),5)
        self.assertFalse(R.suite()['baseline_policy']['automatic_updates'])

    def test_plan_is_inert(self):
        p=self.plan();self.assertFalse(Path(p['output_root']).exists())
        self.assertFalse(p['launch_authorized']);self.assertEqual(p['native_qualification'],'NOT_RUN')
        self.assertEqual(R.preflight(p)['status'],'PREPARED_NOT_LIVE_QUALIFIED')

    def test_gas_requires_override_and_private_root(self):
        p=self.plan('facility-gas');self.assertEqual(p['effective_environment']['SL_RUN'],'1')
        for field,value in [('SL_RUN','0'),('SL_RUN_DIR',str(self.root/'shared'))]:
            q=copy.deepcopy(p);q['effective_environment'][field]=value
            with self.subTest(field=field),self.assertRaises(ProfileError):R.preflight(q)

    def test_plan_rejects_unknown_event_adapter(self):
        with mock.patch.object(R,'KNOWN_HARNESS','0'*64):
            with self.assertRaises(ProfileError):self.plan('facility-gas')

    def test_event_source_contract_rejects_disabled_or_unknown_layout(self):
        (self.source/'src/platform/sl_ultra_shim.c').write_text('unknown mapping')
        with self.assertRaises(ProfileError):self.plan()

    def test_pump_order_records_the_frame_identity_prerequisite(self):
        self.assertEqual(self.plan()['source_contract']['frame_identity_order'],
                         list(R.PUMP_ORDER))

    def test_counter_increment_ahead_of_a_frame_boundary_is_refused(self):
        end,note,begin,bump=R.PUMP_ORDER
        for label,order in [('before the end frame',(bump,end,note,begin)),
                            ('before the begin frame',(end,note,bump,begin))]:
            self.write_shim(order)
            with self.subTest(moved=label),self.assertRaises(ProfileError):self.plan()

    def test_unknown_pump_layout_requires_adapter_review(self):
        end,note,begin,bump=R.PUMP_ORDER
        cases={'anchor missing':('sl_watch_frame((unsigned) sl_vi_advances);\n'
                                 +''.join('%s\n'%x for x in R.PUMP_ORDER)),
               'end frame twice':None,'note respelled':None}
        for label in cases:
            if label=='anchor missing':
                (self.source/'src/platform/sl_ultra_shim.c').write_text(cases[label],encoding='utf-8')
            elif label=='end frame twice':
                self.write_shim(extra='            %s\n'%end)
            else:
                self.write_shim((end,'sl_pres_note(back,0,1.0,h);',begin,bump))
            with self.subTest(layout=label),self.assertRaises(ProfileError) as caught:self.plan()
            self.assertIn('adapter',str(caught.exception))

    def test_source_pin_change_invalidates_preflight(self):
        p=self.plan();(self.source/'src/platform/sl_main.c').write_text('changed')
        with self.assertRaises(ProfileError):R.preflight(p)

    def test_incomplete_source_contract_rejected(self):
        p=self.plan();p['source_contract']['inputs']={}
        with self.assertRaises(ProfileError):R.preflight(p)

    def test_missing_gpu_capability_is_blocked(self):
        p=self.plan('combat-effects',mode='profile');p['metric_capabilities']=['host_frames']
        self.assertEqual(R.preflight(p)['status'],'BLOCKED')

    def test_raw_frame_and_image_chain(self):
        p=self.plan();root=self.run_fixture(p);r=R.validate_run(p,root)
        self.assertEqual(r['frames']['mean_ms'],5);self.assertEqual(r['receipt_kind'],'synthetic')

    def test_all_five_scenes_complete_only_synthetic_gate(self):
        reports=[]
        for scene in self.policy['scenes']:
            plans,roots=self.quartet(scene['id']);rec=R.compare_runs(plans,roots)
            path=self.root/(scene['id']+'.json');self.save(path,rec);reports.append(path)
        plans,roots=self.quartet('combat-effects',mode='profile')
        path=self.root/'combat-profile.json';self.save(path,R.compare_runs(plans,roots));reports.append(path)
        out=R.suite_gate(self.suite,reports,'1080p60')
        self.assertEqual(out['status'],'SYNTHETIC_ONLY');self.assertFalse(out['release_authorized'])

    def test_incomplete_suite_is_not_pass(self):
        self.assertEqual(R.suite_gate(self.suite,[],'1080p60')['status'],'INCOMPLETE')

    def test_edited_comparison_is_recomputed(self):
        p,roots=self.quartet();result=R.compare_runs(p,roots);result['means_ms'][1]=.01
        out=self.root/'edited.json';self.save(out,result)
        with self.assertRaises(ProfileError):R.suite_gate(self.suite,[out],'1080p60')

    def test_duplicate_scene_refused(self):
        p,roots=self.quartet();out=self.root/'report.json';self.save(out,R.compare_runs(p,roots))
        with self.assertRaises(ProfileError):R.suite_gate(self.suite,[out,out],'1080p60')

    def test_tail_regression_is_separate_from_average(self):
        p,roots=self.quartet(means=(5,5,5,5))
        for root in roots[1:3]:
            (root/'pres.txt').write_text('0 R 1 1.0 0 00000000\n1 R 3 1.0 1 00000000\n2 R 5 1.0 2 00000000\n15 R 7 1.0 3 00000000\n')
        out=R.compare_runs(p,roots);self.assertEqual(out['verdict'],'REGRESSION')
        self.assertEqual(out['means_ms'],[5,5,5,5]);self.assertEqual(out['p99_ms'],[5,13,13,5])

    def test_absolute_budget_can_fail_without_relative_regression(self):
        p,roots=self.quartet(means=(20,20,20,20));self.assertEqual(R.compare_runs(p,roots)['verdict'],'OVER_BUDGET')

    def test_gas_means_do_not_become_performance_verdict(self):
        p,roots=self.quartet('facility-gas',means=(20,30,30,20));self.assertEqual(R.compare_runs(p,roots)['verdict'],'DIAGNOSTIC_ONLY')

    def test_missing_event_file_and_endpoint_only_refused(self):
        p=self.plan('facility-gas');root=self.run_fixture(p);event=root/'native-events/one/events'
        event.unlink()
        with self.assertRaises((ProfileError,OSError)):R.validate_run(p,root)
        event.write_text('0 env-rgb 64,128,64\n0 far-fog -1 -> 1000\n')
        with self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_nonfinite_malformed_and_legacy_tokens_fail_closed(self):
        path=self.root/'events'
        for token in ['nan','inf','-inf','--','1.#INF','9'*400]:
            path.write_text('0 env-rgb 16,32,16\n0 far-fog -1 -> '+token+'\n')
            with self.subTest(token=token[:12]),self.assertRaises(ProfileError):R.parse_events(path)

    def test_gas_timelines_must_match(self):
        p,roots=self.quartet('facility-gas');event=roots[1]/'native-events/one/events'
        event.write_text(event.read_text().replace('3000.0','3100.0'))
        with self.assertRaises(ProfileError):R.compare_runs(p,roots)

    def test_captures_cannot_overlap_timing(self):
        p=self.plan();root=self.run_fixture(p);d=R.load_json(root/'receipt.json');d['captures'][0]['frame']=1;self.save(root/'receipt.json',d)
        with self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_raw_image_mismatch_and_truncation_refused(self):
        p,roots=self.quartet();image=roots[1]/'shot-0.ppm'
        image.write_bytes(b'P6\n2 2\n255\n'+b'\x41'*12)
        with self.assertRaises(ProfileError):R.compare_runs(p,roots)
        image.write_bytes(b'P6\n2 2\n255\nshort')
        with self.assertRaises(ProfileError):R.compare_runs(p,roots)

    def test_work_machine_and_environment_mismatch(self):
        for key,value in [('machine_id','another-machine'),('effective_environment',{'SL_RUN':'1'}),('binary_sha256','0'*64),('resolution',[1,1]),('normal_exit',False),('foreground_qualified',False)]:
            p=self.plan();root=self.run_fixture(p);d=R.load_json(root/'receipt.json');d[key]=value;self.save(root/'receipt.json',d)
            with self.subTest(key=key),self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_native_exit_and_foreground_logs_required(self):
        for file in ['launch.txt','foreground-log.txt']:
            p=self.plan();root=self.run_fixture(p);(root/file).write_text('invalid')
            with self.subTest(file=file),self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_foreground_loss_and_obscured_frames_refused(self):
        for field in [4,5,7]:
            p=self.plan();root=self.run_fixture(p);path=root/'foreground-log.txt';rows=path.read_text().splitlines();f=rows[-1].split();f[field]='False';rows[-1]=' '.join(f);path.write_text('\n'.join(rows)+'\n')
            with self.subTest(field=field),self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_receipt_plan_and_path_tampering(self):
        p=self.plan();root=self.run_fixture(p);d=R.load_json(root/'receipt.json');d['captures'][0]['path']='../escape.ppm';self.save(root/'receipt.json',d)
        with self.assertRaises(ProfileError):R.validate_run(p,root)
        d['captures'][0]['path']='shot-0.ppm';d['plan_sha256']='0'*64;self.save(root/'receipt.json',d)
        with self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_missing_frame_and_reused_roots_refused(self):
        p,roots=self.quartet();(roots[1]/'pres.txt').write_text('0 R 1 1.0 0 00000000\n5 R 5 1.0 2 00000000\n')
        with self.assertRaises(ProfileError):R.compare_runs(p,roots)
        with self.assertRaises(ProfileError):R.compare_runs(p,[roots[0]]*4)

    def test_metric_pending_never_becomes_gpu_zero(self):
        p=self.plan('combat-effects',mode='profile');root=self.run_fixture(p);path=root/'metrics.csv';path.write_text(path.read_text().replace('1.5,ready',',pending'))
        with self.assertRaises(ProfileError):R.validate_run(p,root)
        groups=R.feature_metrics(path,p['window']);self.assertIsNone(groups['frame']['gpu_elapsed_mean_ms'])

    def test_metric_duplicate_and_nonfinite_rejected(self):
        p=self.plan('combat-effects',mode='profile');root=self.run_fixture(p);path=root/'metrics.csv';original=path.read_text()
        for bad in [original+original.splitlines()[1]+'\n',original.replace('1.5','nan'),original.replace('ready','unknown')]:
            path.write_text(bad)
            with self.assertRaises(ProfileError):R.feature_metrics(path,p['window'])

    def test_duplicate_json_keys_rejected(self):
        path=self.root/'bad.json';path.write_text('{"schema":1,"schema":2}')
        with self.assertRaises(ProfileError):R.load_json(path)

    def test_instrumented_runs_cannot_set_clean_budget(self):
        plans,roots=self.quartet(mode='profile',means=(20,30,30,20))
        self.assertEqual(R.compare_runs(plans,roots)['verdict'],'DIAGNOSTIC_ONLY')
        p=self.plan();root=self.run_fixture(p);d=R.load_json(root/'receipt.json')
        d['metrics_path']='metrics.csv';self.save(root/'receipt.json',d)
        with self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_capture_schedule_cannot_be_cherry_picked(self):
        p=self.plan('facility-gas');root=self.run_fixture(p);d=R.load_json(root/'receipt.json')
        d['captures'].pop(1);self.save(root/'receipt.json',d)
        with self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_metric_grid_selects_regular_pumps_without_inventing_ratio(self):
        p=self.plan(mode='profile');root=self.run_fixture(p);path=root/'metrics.csv'
        with path.open('a') as f:f.write('2,frame,999,999,ready\n')
        metrics=R.validate_run(p,root)['feature_metrics']['frame']
        self.assertEqual(metrics['frames'],[1,3,5,7]);self.assertEqual(metrics['gpu_elapsed_mean_ms'],1.5)

    def test_async_retirement_order_does_not_change_frame_coverage(self):
        p=self.plan(mode='profile');root=self.run_fixture(p);path=root/'metrics.csv'
        rows=path.read_text().splitlines();path.write_text('\n'.join([rows[0],*reversed(rows[1:])])+'\n')
        self.assertEqual(R.validate_run(p,root)['feature_metrics']['frame']['frames'],[1,3,5,7])

    def test_incomplete_profile_does_not_fall_back_to_host_time(self):
        p=self.plan(mode='profile');root=self.run_fixture(p);path=root/'metrics.csv'
        path.write_text('\n'.join(path.read_text().splitlines()[:-1])+'\n')
        with self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_mixed_suite_candidate_binaries_rejected(self):
        paths=[]
        for scene in ['facility-tanks','frigate-deck']:
            plans,roots=self.quartet(scene)
            path=self.root/(scene+'.json');self.save(path,R.compare_runs(plans,roots));paths.append(path)
            # The next quartet has another candidate; its own ABBA is valid.
            self.b=self.root/'different.bin';self.b.write_bytes(b'different candidate')
        with self.assertRaises(ProfileError):R.suite_gate(self.suite,paths,'1080p60')

    def test_workload_attestation_is_required(self):
        p=self.plan();root=self.run_fixture(p);d=R.load_json(root/'receipt.json')
        d['workload']['pose_id']='unplanned';self.save(root/'receipt.json',d)
        with self.assertRaises(ProfileError):R.validate_run(p,root)

    def test_complete_cli_chain_on_raw_synthetic_data(self):
        plans,roots=self.quartet();paths=[]
        for n,p in enumerate(plans):
            path=self.root/('plan-%d.json'%n);self.save(path,p);paths.append(path)
        for command,options in [('preflight',['--plan',str(paths[0])]),
                                ('validate',['--plan',str(paths[0]),'--root',str(roots[0])]),
                                ('compare',['--plans',*map(str,paths),'--roots',*map(str,roots)])]:
            out=self.root/(command+'.json')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(R.main([command,*options,'--out',str(out)]),0)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(R.main(['gate','--suite',str(self.suite),'--comparisons',str(self.root/'compare.json'),'--out',str(self.root/'gate.json')]),1)
        self.assertEqual(R.load_json(self.root/'gate.json')['status'],'INCOMPLETE')

    def test_plan_cli_uses_reviewed_bindings_without_creating_run(self):
        p=self.plan();bindings={'binary':p['binary']['path'],'source_root':str(self.source),
            'machine_id':p['machine_id'],'input':p['input']['path'],'pose':p['pose']['path'],
            'harness':p['harness']['path'],'output_root':p['output_root'],
            'measurement_mode':p['measurement_mode'],'workload':p['workload'],
            'base_environment':p['base_environment'],'overrides':p['environment_overrides'],
            'counter_mapping_reviewed':True,'metric_capabilities':p['metric_capabilities']}
        path=self.root/'bindings.json';self.save(path,bindings);out=self.root/'inert-plan.json'
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(R.main(['plan','--suite',str(self.suite),'--scene','facility-tanks','--bindings',str(path),'--out',str(out)]),0)
        self.assertEqual(R.identity(R.load_json(out)),R.identity(p))
        self.assertFalse(Path(p['output_root']).exists())

    def test_cli_create_only_and_no_launch_command(self):
        out=self.root/'catalog.json'
        with contextlib.redirect_stdout(io.StringIO()):self.assertEqual(R.main(['catalog','--out',str(out)]),0)
        with contextlib.redirect_stderr(io.StringIO()):self.assertEqual(R.main(['catalog','--out',str(out)]),2)
        with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):R.main(['run','--out',str(out)])


if __name__=='__main__':
    unittest.main(verbosity=2)
