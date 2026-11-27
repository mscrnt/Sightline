"""Synthetic-only regression tests. All native subprocess creation is forbidden."""
import contextlib
import copy
import csv
import io
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import analyze
import capture
from common import ProfileError, load_json, pin, sha256, write_json


class FakeBackend:
    def __init__(self, plan, fail=None):
        self.plan, self.fail = plan, fail
        self.clock, self.commands, self.observations = 0, [], 0

    def observe(self):
        self.observations += 1
        row = {'alive': True, 'foreground_pid': self.plan['pid'],
               'identity': {'path': self.plan['target']['path'], 'creation_filetime': 42}}
        if self.observations > 2:
            if self.fail == 'foreground':
                row['foreground_pid'] = 999
            if self.fail == 'exit':
                row['alive'] = False
            if self.fail == 'pid_reuse':
                row['identity']['creation_filetime'] += 1
        return row

    def command(self, args):
        self.commands.append(args)
        if args[0] == '-start' and self.fail == 'start_exception':
            raise OSError('synthetic start error')
        failed = self.fail == args[0].lstrip('-')
        if args[0] == '-stop' and not failed and self.fail != 'missing_etl':
            Path(args[1]).write_bytes(b'SYNTHETIC TEST ETL; NOT A RECORDING')
        if args[0] == '-stop' and self.fail == 'stop_exception':
            raise OSError('synthetic stop error')
        return {'args': args, 'returncode': 1 if failed else 0, 'stdout': '', 'stderr': ''}

    def now(self):
        return self.clock

    def sleep(self, seconds):
        if self.fail == 'interrupt':
            raise KeyboardInterrupt()
        self.clock += seconds


class SyntheticTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sightline-profile-synthetic-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.binary = self.root / 'synthetic.exe'
        self.binary.write_bytes(b'NOT EXECUTABLE; SYNTHETIC TEST INPUT')
        self.nm = self.root / 'nm.txt'
        self.nm.write_text('00401000 00000010 T _walk\n'
                           '00401000 00000010 t _walk_alias\n'
                           '00401020 00000008 T _submit\n'
                           '00401030 t _unsized\n'
                           '00402000 00000004 D _data\n', encoding='utf-8')
        self.symbols = analyze.symbol_map(self.binary, self.nm, '400000', '3000')

    def plan(self):
        return capture.make_plan(self.binary, 123, 1, self.root / 'new capture')

    def samples(self, rows=None, header=None):
        path = self.root / 'samples.csv'
        rows = rows if rows is not None else [
            ['0.5', '123', '7', sha256(self.binary), '500000', '501000', '2'],
            ['1', '123', '7', sha256(self.binary), '500000', '50100f', '3'],
            ['1.5', '123', '8', sha256(self.binary), '600000', '601010', '1'],
            ['2', '123', '7', '-', '700000', '701000', '4'],
            ['3', '123', '7', sha256(self.binary), '500000', '501020', '99'],
            ['1', '124', '7', sha256(self.binary), '500000', '501000', '99']]
        with path.open('w', encoding='utf-8-sig', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(header or analyze.SAMPLE_FIELDS)
            writer.writerows(rows)
        return path

    def pres(self, name='pres.txt', multiplier=1, hashes='12345678'):
        path = self.root / name
        rows = ['# synthetic presentations']
        for i, frame in enumerate((1, 3, 5, 7)):
            rows.append('%s R %d 1.0 %d %s' % (i * 10 * multiplier, frame, i, hashes))
            rows.append('%s E %d 0.5 %d %s' % ((i * 10 + 5) * multiplier, frame, i, hashes))
        path.write_text('\n'.join(rows), encoding='utf-8')
        return path

    def test_plan_is_inert_and_unique(self):
        plan = self.plan()
        self.assertFalse(Path(plan['output']).exists())
        self.assertNotEqual(plan['instance'], self.plan()['instance'])
        for command in ('start', 'stop', 'recovery'):
            self.assertEqual(plan[command][-2:], ['-instancename', plan['instance']])
        self.assertNotIn('-cancel', repr(plan))

    def test_plan_rejects_invalid_scope(self):
        for pid, duration in ((0, 1), (1, 0), (1, 121), (1, float('nan')),
                              (1, float('inf')), (True, 1)):
            with self.subTest(pid=pid, duration=duration), self.assertRaises((ValueError, ProfileError)):
                capture.make_plan(self.binary, pid, duration, self.root / 'out')
        with self.assertRaises(ProfileError):
            capture.make_plan(self.binary, 1, 1, self.root)

    def test_record_without_confirmation_never_constructs_backend(self):
        with mock.patch.object(capture, 'WindowsBackend', side_effect=AssertionError('live backend')):
            with contextlib.redirect_stderr(io.StringIO()):
                code = capture.main(['record', '--exe', str(self.binary), '--pid', '123',
                                     '--seconds', '1', '--out', str(self.root / 'out')])
        self.assertEqual(code, 2)
        self.assertFalse((self.root / 'out').exists())

    def test_missing_privilege_fails_before_target_or_recording(self):
        with mock.patch.object(capture, 'capabilities', return_value={'system_profile_privilege_available': False}), \
             mock.patch.object(capture, 'WindowsBackend', side_effect=AssertionError('must not open process')):
            with contextlib.redirect_stderr(io.StringIO()):
                code = capture.main(['record', '--exe', str(self.binary), '--pid', '123',
                    '--seconds', '1', '--out', str(self.root / 'out'), '--confirm-system-recording'])
        self.assertEqual(code, 2)
        self.assertFalse((self.root / 'out').exists())

    def test_doctor_only_reports_capability(self):
        with mock.patch.object(capture, 'capabilities', return_value={'system_profile_privilege_available': False}):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(capture.main(['doctor']), 1)

    def test_plan_cli_never_constructs_backend(self):
        with mock.patch.object(capture, 'WindowsBackend', side_effect=AssertionError('live backend')):
            with contextlib.redirect_stdout(io.StringIO()):
                code = capture.main(['plan', '--exe', str(self.binary), '--pid', '123',
                                     '--seconds', '1', '--out', str(self.root / 'out')])
        self.assertEqual(code, 0)

    def test_fake_record_success_preserves_raw_and_never_claims_quality(self):
        plan = self.plan()
        backend = FakeBackend(plan)
        result = capture.record(plan, backend)
        self.assertEqual(result['status'], 'RECORDED')
        self.assertEqual(result['trace_quality'], 'UNREVIEWED')
        self.assertEqual(result['optimization_verdict'], 'NOT_EVALUATED')
        self.assertEqual([x[0] for x in backend.commands], ['-start', '-stop'])
        self.assertEqual(load_json(Path(plan['output']) / 'result.json'), result)
        self.assertEqual(result['etl'], pin(Path(plan['output']) / 'capture.etl'))

    def test_capture_failure_paths_preserve_evidence_without_retry(self):
        for failure in ('start', 'start_exception', 'stop', 'stop_exception', 'missing_etl',
                        'foreground', 'exit', 'pid_reuse', 'interrupt'):
            with self.subTest(failure=failure):
                plan = capture.make_plan(self.binary, 123, 1, self.root / failure)
                backend = FakeBackend(plan, failure)
                result = capture.record(plan, backend)
                self.assertEqual(result['status'], 'FAILED')
                self.assertTrue(result['error'])
                self.assertEqual(len(backend.commands), 1 if failure.startswith('start') else 2)
                self.assertTrue((Path(plan['output']) / 'result.json').is_file())
                self.assertTrue(all(x[-1] == plan['instance'] for x in backend.commands))
                self.assertNotIn('-cancel', repr(backend.commands))

    def test_capture_rejects_mutated_binary_and_existing_output(self):
        plan = self.plan()
        self.binary.write_bytes(b'changed')
        backend = FakeBackend(plan)
        with self.assertRaises(ProfileError):
            capture.record(plan, backend)
        self.assertEqual(backend.commands, [])
        plan = self.plan()
        Path(plan['output']).mkdir()
        with self.assertRaises(FileExistsError):
            capture.record(plan, FakeBackend(plan))

    def test_checkpoint_write_failure_still_stops_named_recording(self):
        plan = self.plan()
        backend = FakeBackend(plan)
        real_write = capture.write_json
        def fail_stop_checkpoint(path, data):
            if path.name == 'stop-pending.json':
                raise OSError('synthetic disk failure')
            return real_write(path, data)
        with mock.patch.object(capture, 'write_json', side_effect=fail_stop_checkpoint):
            result = capture.record(plan, backend)
        self.assertEqual([x[0] for x in backend.commands], ['-start', '-stop'])
        self.assertEqual(result['status'], 'FAILED')
        self.assertIn('checkpoint', result['error'])

    def test_capture_preflight_must_be_foreground(self):
        plan = self.plan()
        backend = FakeBackend(plan)
        with mock.patch.object(backend, 'observe', return_value={'alive': True, 'foreground_pid': 999}):
            with self.assertRaises(ProfileError):
                capture.record(plan, backend)
        self.assertFalse(Path(plan['output']).exists())

    def test_capture_waits_without_recording_until_foreground_ready(self):
        plan = self.plan()
        backend = FakeBackend(plan)
        observe = backend.observe
        def initially_background():
            row = observe()
            if backend.clock < 1:
                self.assertEqual(backend.commands, [])
                row['foreground_pid'] = 999
            return row
        with mock.patch.object(backend, 'observe', side_effect=initially_background):
            result = capture.record(plan, backend)
        self.assertEqual(result['status'], 'RECORDED')

    def test_symbols_aliases_and_unsized_are_explicit(self):
        self.assertEqual(self.symbols['unsized_code_symbols'], 1)
        self.assertEqual(self.symbols['intervals'][0]['names'], ['_walk', '_walk_alias'])
        self.assertEqual(len(self.symbols['intervals']), 2)

    def test_symbols_reject_overlap_bad_bounds_empty_and_unknown_format(self):
        for text in ('00401000 00000020 T _a\n00401010 00000020 T _b',
                     '00403000 00000001 T _outside', '00400000 t _unsized',
                     'NOT AN NM TABLE', '00401000 -4 T _bad'):
            with self.subTest(text=text), self.assertRaises(ProfileError):
                self.nm.write_text(text, encoding='utf-8')
                analyze.symbol_map(self.binary, self.nm, '400000', '3000')

    def test_unwind_extents_require_exact_entry_and_do_not_fill_padding(self):
        self.nm.write_text('00401000 t .text\n00401000 T _walk\n00401020 T _draw\n', encoding='utf-8')
        frames = self.root / 'frames.txt'
        frames.write_text('00000018 00000010 0000001c FDE cie=00000000 pc=00401000..0040100f\n'
                          '0000002c 00000010 00000030 FDE cie=00000000 pc=00401018..0040101a\n'
                          '00000040 00000010 00000044 FDE cie=00000000 pc=00401020..00401028\n', encoding='utf-8')
        symbols = analyze.symbol_map(self.binary, self.nm, '400000', '3000', frames)
        self.assertEqual(symbols['fde_entry_matches'], 2)
        self.assertEqual(symbols['intervals'], [
            {'rva': 0x1000, 'size': 15, 'names': ['_walk']},
            {'rva': 0x1020, 'size': 8, 'names': ['_draw']}])
        result = analyze.attribute(self.samples(), symbols, 123, 0, 3)
        self.assertEqual(result['coverage_rows']['mapped_target_code'], 1)
        self.assertEqual(result['coverage_rows']['no_explicit_symbol_extent'], 2)

    def test_unwind_parser_rejects_bad_ranges_and_format(self):
        self.nm.write_text('00401000 T _walk\n', encoding='utf-8')
        frames = self.root / 'frames.txt'
        for text in ('0 0 0 FDE cie=0 pc=00401000..00400fff',
                     '0 0 0 FDE cie=0 pc=00401000..00404000',
                     '0 0 0 FDE cie=0 no-PC-field'):
            with self.subTest(text=text), self.assertRaises(ProfileError):
                frames.write_text(text, encoding='utf-8')
                analyze.symbol_map(self.binary, self.nm, '400000', '3000', frames)

    def test_samples_aslr_gaps_denominator_and_half_open_window(self):
        result = analyze.attribute(self.samples(), self.symbols, 123, 0, 3)
        self.assertEqual(result['sample_rows'], 4)
        self.assertEqual(result['total_weight_ms'], 10)
        self.assertEqual(result['functions'][0]['weight_ms'], 5)
        self.assertEqual(result['functions'][0]['percent_of_selected_cpu_weight'], 50)
        self.assertEqual(result['coverage_rows']['no_explicit_symbol_extent'], 1)
        self.assertEqual(result['coverage_rows']['other_or_unidentified_module'], 1)
        self.assertEqual(analyze.attribute(self.samples(), self.symbols, 123, 0, 3, 7)['sample_rows'], 3)

    def test_debug_resolver_uses_exact_relocated_points_without_native_execution(self):
        def fake(argv, **options):
            self.assertFalse(options['shell'])
            addresses = options['input'].splitlines()
            self.assertIn('0x401000', addresses)
            self.assertNotIn('0x501000', addresses)
            text = ''.join('%s\n%s\nsynthetic.c:7\n' % (address,
                '??' if address == '0x401010' else 'walk') for address in addresses)
            return SimpleNamespace(returncode=0, stdout=text, stderr='')
        source = self.samples()
        symbols = analyze.resolve_points(self.binary, self.binary, source, 123,
                                         '400000', '3000', runner=fake)
        self.assertEqual(symbols['unresolved_addresses'], 1)
        self.assertTrue(all(x['size'] == 1 for x in symbols['intervals']))
        result = analyze.attribute(source, symbols, 123, 0, 3)
        self.assertEqual(result['coverage_rows']['mapped_target_code'], 2)
        self.assertEqual(result['coverage_rows']['no_explicit_symbol_extent'], 1)

    def test_debug_resolver_rejects_wrong_order_missing_output_and_failure(self):
        for result in (SimpleNamespace(returncode=1, stdout='', stderr='synthetic failure'),
                       SimpleNamespace(returncode=0, stdout='', stderr=''),
                       SimpleNamespace(returncode=0, stdout='0x0\nwrong\nx:1\n' * 4, stderr='')):
            with self.subTest(result=result), self.assertRaises(ProfileError):
                analyze.resolve_points(self.binary, self.binary, self.samples(), 123,
                    '400000', '3000', runner=lambda *a, **kw: result)

    def test_samples_reject_empty_or_missing_selection(self):
        for rows, pid, start, end in (([], 123, 0, 3), (None, 999, 0, 3),
                                      (None, 123, 3, 3), (None, 123, 0, float('nan'))):
            with self.subTest(rows=rows, pid=pid), self.assertRaises(ProfileError):
                analyze.attribute(self.samples(rows), self.symbols, pid, start, end)

    def test_samples_reject_bad_data_even_outside_selection(self):
        base = ['1', '999', '7', sha256(self.binary), '500000', '501000', '1']
        for index, bad in ((0, 'NaN'), (1, '0'), (2, '7.5'), (3, 'bad-hash'),
                           (4, '-1'), (5, 'bad address'), (6, '-2'), (6, '0'), (6, 'inf')):
            with self.subTest(index=index, bad=bad), self.assertRaises(ValueError):
                row = list(base)
                row[index] = bad
                analyze.attribute(self.samples([row]), self.symbols, 123, 0, 3)

    def test_samples_reject_out_of_image_and_bad_maps(self):
        row = ['1', '123', '7', sha256(self.binary), '500000', '503000', '1']
        with self.assertRaises(ProfileError):
            analyze.attribute(self.samples([row]), self.symbols, 123, 0, 3)
        symbols = copy.deepcopy(self.symbols)
        symbols['intervals'].reverse()
        with self.assertRaises(ProfileError):
            analyze.attribute(self.samples(), symbols, 123, 0, 3)

    def test_explicit_csv_column_mapping(self):
        names = ['a', 'b', 'c', 'd', 'e', 'f', 'g']
        result = analyze.attribute(self.samples(header=names), self.symbols, 123, 0, 3,
                                  columns=dict(zip(analyze.SAMPLE_FIELDS, names)))
        self.assertEqual(result['sample_rows'], 4)
        with self.assertRaises(ProfileError):
            analyze.attribute(self.samples(header=names), self.symbols, 123, 0, 3)

    def test_normalize_export_pins_modules_and_preserves_unknown_weight(self):
        path = self.root / 'export.csv'
        columns = dict(zip(analyze.EXPORT_FIELDS, ['Time', 'PID', 'TID', 'Module', 'Base', 'IP', 'Weight']))
        with path.open('w', encoding='utf-8-sig', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(columns.values())
            writer.writerow(['1', '123', '7', 'synthetic.exe', '500000', '501000', '2'])
            writer.writerow(['2', '123', '7', 'driver.dll', '700000', '701000', '8'])
        rows = analyze.normalize_samples(path, columns, {'synthetic.exe': pin(self.binary)})
        self.assertEqual(rows[0]['module_sha256'], sha256(self.binary))
        self.assertEqual(rows[0]['module_base'], '500000')
        self.assertEqual(rows[1]['module_sha256'], '-')
        self.assertEqual(rows[1]['weight_ms'], '8')
        with self.assertRaises(ProfileError):
            analyze.normalize_samples(path, columns, {'wrong-name.exe': pin(self.binary)})

    def test_normalize_cli_provenance_and_no_overwrite(self):
        path = self.root / 'export.csv'
        with path.open('w', encoding='utf-8', newline='') as stream:
            writer = csv.writer(stream)
            writer.writerow(analyze.EXPORT_FIELDS)
            writer.writerow(['1', '123', '7', 'synthetic.exe', '500000', '501000', '1'])
        columns, modules, output = (self.root / name for name in ('columns.json', 'modules.json', 'normalized.csv'))
        write_json(columns, {x: x for x in analyze.EXPORT_FIELDS})
        write_json(modules, {'synthetic.exe': pin(self.binary)})
        argv = ['normalize', '--csv', str(path), '--columns', str(columns),
                '--modules', str(modules), '--out', str(output)]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(analyze.main(argv), 0)
        provenance = load_json(output.with_name(output.name + '.provenance.json'))
        self.assertEqual(provenance['output'], pin(output))
        self.assertEqual(analyze.attribute(output, self.symbols, 123, 0, 3)['sample_rows'], 1)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(analyze.main(argv), 2)

    def test_csv_rejects_ragged_duplicate_or_incomplete_headers(self):
        for text in ('a,a\n1,2', ','.join(analyze.SAMPLE_FIELDS) + '\n1,2',
                     ','.join(analyze.SAMPLE_FIELDS) + '\n' + ','.join(['1'] * 8)):
            path = self.root / 'bad.csv'
            path.write_text(text, encoding='utf-8')
            with self.subTest(text=text), self.assertRaises(ProfileError):
                list(analyze.csv_rows(path, analyze.SAMPLE_FIELDS))

    def test_frames_exact_interval_scope_and_statistics(self):
        result = analyze.frame_report(self.pres(), 1, 9, 2)
        self.assertEqual(result['presentations'], 8)
        self.assertEqual(result['intervals'], 7)
        self.assertEqual(result['mean_ms'], 5)
        self.assertEqual(result['fps_from_mean'], 200)
        self.assertEqual(result['p99_ms_nearest_rank'], 5)
        self.assertEqual(result['one_percent_low_fps'], 200)
        self.assertTrue(result['hashes_informative'])

    def test_frame_window_never_shifts_or_skips_missing_regular_frames(self):
        for lo, hi, step in ((2, 9, 2), (1, 11, 2), (1, 9, 1), (1, 1, 2), (1, 9, 0)):
            with self.subTest(lo=lo, hi=hi, step=step), self.assertRaises(ProfileError):
                analyze.frame_report(self.pres(), lo, hi, step)

    def test_frame_parser_rejects_empty_malformed_nonfinite_and_reversed(self):
        for text in ('', '# header only', 'NaN R 1 1 1 12345678',
                     '0 R 1 1 1 12345678\n0 E 1 .5 1 12345678',
                     '10 R 3 1 1 12345678\n20 R 1 1 2 12345678',
                     '0 R 1 1.1 1 12345678', '0 R 1 1 1 zzzzzzzz',
                     '0 B 1 0 junk 0', '0 Q 1 1 1 12345678'):
            path = self.root / 'bad.txt'
            path.write_text(text, encoding='utf-8')
            with self.subTest(text=text), self.assertRaises(ValueError):
                analyze.presentations(path)

    def test_budget_rows_are_validated_and_not_used_as_presents(self):
        path = self.pres()
        with path.open('a', encoding='utf-8') as stream:
            stream.write('\n99 B 7 1 -100 -200\n')
        self.assertEqual(analyze.frame_report(path, 1, 9, 2)['presentations'], 8)

    def comparisons(self):
        reports = [analyze.frame_report(self.pres('p%d.txt' % i, factor), 1, 9, 2)
                   for i, factor in enumerate((2, 1, 1, 2))]
        contexts = [{'workload': 'synthetic', 'config_sha256': 'a' * 64,
                     'input_sha256': 'b' * 64, 'binary_sha256': 'c' * 64,
                     'arm': arm, 'foreground_passed': True, 'normal_exit': True,
                     'correctness_passed': True} for arm in ('A', 'B', 'B', 'A')]
        return reports, contexts

    def test_abba_descriptive_only_and_exact_workload(self):
        reports, contexts = self.comparisons()
        result = analyze.compare_abba(reports, contexts)
        self.assertTrue(result['timing_screen_passed'])
        self.assertEqual(result['optimization_verdict'], 'NOT_EVALUATED')
        self.assertTrue(result['informative_log_digests_equal'])

    def test_abba_rejects_missing_gates_workload_and_binary_mismatch(self):
        for field, value in (('arm', 'B'), ('config_sha256', 'f' * 64),
                             ('input_sha256', 'f' * 64), ('binary_sha256', 'f' * 64),
                             ('workload', 'other'), ('foreground_passed', False),
                             ('normal_exit', False), ('correctness_passed', 1)):
            reports, contexts = self.comparisons()
            contexts[0][field] = value
            with self.subTest(field=field), self.assertRaises(ProfileError):
                analyze.compare_abba(reports, contexts)

    def test_abba_rejects_summary_tampering_and_source_mutation(self):
        reports, contexts = self.comparisons()
        reports[0]['mean_ms'] = 999
        with self.assertRaises(ProfileError):
            analyze.compare_abba(reports, contexts)
        reports, contexts = self.comparisons()
        Path(reports[0]['source']['path']).write_text('changed', encoding='utf-8')
        with self.assertRaises(ProfileError):
            analyze.compare_abba(reports, contexts)

    def test_zero_digests_do_not_claim_image_equality(self):
        report = analyze.frame_report(self.pres(hashes='00000000'), 1, 9, 2)
        self.assertFalse(report['hashes_informative'])

    def test_outputs_never_overwrite_or_follow_existing_symlink(self):
        path = self.root / 'out.json'
        write_json(path, {'synthetic': True})
        before = path.read_bytes()
        with self.assertRaises(FileExistsError):
            write_json(path, {})
        self.assertEqual(path.read_bytes(), before)

    def test_json_rejects_duplicate_keys_and_nonfinite(self):
        path = self.root / 'bad.json'
        for text in ('{"a":1,"a":2}', '{"a":NaN}'):
            path.write_text(text, encoding='utf-8')
            with self.subTest(text=text), self.assertRaises(ProfileError):
                load_json(path)

    def test_end_to_end_offline_cli_on_synthetic_inputs(self):
        symbols = self.root / 'symbols.json'
        samples = self.root / 'samples.json'
        frames = self.root / 'frames.json'
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(analyze.main(['symbols', '--binary', str(self.binary),
                '--nm-text', str(self.nm), '--image-base', '400000', '--image-size', '3000',
                '--out', str(symbols)]), 0)
            self.assertEqual(analyze.main(['samples', '--csv', str(self.samples()),
                '--symbols', str(symbols), '--pid', '123', '--start', '0', '--end', '3',
                '--out', str(samples)]), 0)
            self.assertEqual(analyze.main(['frames', '--pres', str(self.pres()),
                '--lo', '1', '--hi', '9', '--frame-step', '2', '--out', str(frames)]), 0)
        self.assertEqual(load_json(samples)['sample_rows'], 4)
        self.assertEqual(load_json(frames)['intervals'], 7)


if __name__ == '__main__':
    # Even an accidental production-backend call in a test cannot start WPR,
    # a game, a compiler, an exporter or any other native child process.
    with mock.patch.object(subprocess, 'Popen', side_effect=AssertionError('native execution forbidden in synthetic suite')), \
         mock.patch.object(capture, 'WindowsBackend', side_effect=AssertionError('live process access forbidden in synthetic suite')):
        unittest.main(verbosity=2)
