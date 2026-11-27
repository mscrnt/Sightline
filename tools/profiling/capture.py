"""Plan or explicitly record an existing Windows process with WPR.

Importing, --help and plan never start a process or recording. Recording never
launches the game. The test suite injects a fake backend into record().
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

from common import ProfileError, integer, number, pin, same_pin, write_json


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def capabilities():
    """Read the current token; never enable a privilege or start a recorder."""
    if sys.platform != 'win32':
        raise ProfileError('Windows capability checks require Windows')
    import ctypes as ct
    from ctypes import wintypes as wt
    kernel = ct.WinDLL('kernel32', use_last_error=True)
    security = ct.WinDLL('advapi32', use_last_error=True)
    class LUID(ct.Structure):
        _fields_ = [('low', wt.DWORD), ('high', wt.LONG)]
    class Entry(ct.Structure):
        _fields_ = [('luid', LUID), ('attributes', wt.DWORD)]
    class Privileges(ct.Structure):
        _fields_ = [('count', wt.DWORD), ('entries', Entry * 1)]
    kernel.GetCurrentProcess.restype = wt.HANDLE
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    kernel.CloseHandle.restype = wt.BOOL
    security.OpenProcessToken.argtypes = [wt.HANDLE, wt.DWORD, ct.POINTER(wt.HANDLE)]
    security.OpenProcessToken.restype = wt.BOOL
    security.GetTokenInformation.argtypes = [wt.HANDLE, ct.c_int, ct.c_void_p,
                                            wt.DWORD, ct.POINTER(wt.DWORD)]
    security.GetTokenInformation.restype = wt.BOOL
    security.LookupPrivilegeValueW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ct.POINTER(LUID)]
    security.LookupPrivilegeValueW.restype = wt.BOOL
    token, length, wanted = wt.HANDLE(), wt.DWORD(), LUID()
    if not security.OpenProcessToken(kernel.GetCurrentProcess(), 0x0008, ct.byref(token)):
        raise ProfileError('cannot query current process token')
    try:
        if not security.LookupPrivilegeValueW(None, 'SeSystemProfilePrivilege', ct.byref(wanted)):
            raise ProfileError('cannot identify system profiling privilege')
        security.GetTokenInformation(token, 3, None, 0, ct.byref(length))
        if length.value < ct.sizeof(wt.DWORD):
            raise ProfileError('cannot size token privileges')
        buffer = ct.create_string_buffer(length.value)
        if not security.GetTokenInformation(token, 3, buffer, length, ct.byref(length)):
            raise ProfileError('cannot read token privileges')
        count = ct.cast(buffer, ct.POINTER(wt.DWORD)).contents.value
        offset = Privileges.entries.offset
        if offset + count * ct.sizeof(Entry) > length.value:
            raise ProfileError('invalid token privilege buffer')
        entries = ct.cast(ct.addressof(buffer) + offset, ct.POINTER(Entry))
        available, enabled = False, False
        for i in range(count):
            if entries[i].luid.low == wanted.low and entries[i].luid.high == wanted.high:
                available = True
                enabled = bool(entries[i].attributes & 2)
        return {'schema': 'sightline-profile-capabilities/1',
                'system_profile_privilege_available': available,
                'system_profile_privilege_enabled': enabled,
                'recording_started': False,
                'qualification': 'read-only token check; availability is necessary, not sufficient, for WPR CPU profiling'}
    finally:
        kernel.CloseHandle(token)


def make_plan(executable, pid, seconds, output):
    pid = integer(pid, 'pid', 1)
    seconds = number(seconds, 'seconds', 1)
    if seconds > 120:
        raise ProfileError('capture duration must be 1..120 seconds')
    executable = Path(executable).resolve(strict=True)
    if not executable.is_file() or executable.suffix.lower() != '.exe':
        raise ProfileError('target must be an existing .exe file')
    output = Path(output).resolve()
    if output.exists():
        raise ProfileError('output directory already exists')
    if not output.parent.is_dir():
        raise ProfileError('output parent must already exist')
    instance = 'Sightline-' + uuid.uuid4().hex
    return {
        'schema': 'sightline-profile-capture/1', 'target': pin(executable),
        'pid': pid, 'duration_s': seconds, 'output': str(output),
        'ready_timeout_s': 10,
        'tools': [pin(Path(__file__)), pin(Path(__file__).with_name('common.py'))],
        'instance': instance, 'profiles': ['CPU', 'GPU'],
        'scope': 'system-wide ETW; target selection is applied during analysis',
        'start': ['-start', 'CPU', '-start', 'GPU', '-filemode',
                  '-recordtempto', str(output), '-instancename', instance],
        'stop': ['-stop', str(output / 'capture.etl'), '-instancename', instance],
        'recovery': ['-stop', str(output / 'recovered.etl'),
                     '-instancename', instance],
    }


class WindowsBackend:
    """Lazily initialized Windows APIs; no shell, elevation or process launch."""
    def __init__(self, plan):
        if sys.platform != 'win32':
            raise ProfileError('record requires Windows')
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.wintypes = wintypes
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE,
            wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        self.kernel.QueryFullProcessImageNameW.restype = wintypes.BOOL
        self.kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [
            ctypes.POINTER(wintypes.FILETIME)] * 4
        self.kernel.GetProcessTimes.restype = wintypes.BOOL
        self.kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE,
            ctypes.POINTER(wintypes.DWORD)]
        self.kernel.GetExitCodeProcess.restype = wintypes.BOOL
        self.user.GetForegroundWindow.restype = wintypes.HWND
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND,
            ctypes.POINTER(wintypes.DWORD)]
        self.user.GetWindowThreadProcessId.restype = wintypes.DWORD
        # Use the Windows directory API rather than a caller-supplied PATH.
        self.kernel.GetWindowsDirectoryW.argtypes = [wintypes.LPWSTR, wintypes.UINT]
        self.kernel.GetWindowsDirectoryW.restype = wintypes.UINT
        buf = ctypes.create_unicode_buffer(32768)
        size = self.kernel.GetWindowsDirectoryW(buf, len(buf))
        if not size or size >= len(buf):
            raise ProfileError('cannot locate Windows directory')
        self.wpr = Path(buf.value) / 'System32' / 'wpr.exe'
        if not self.wpr.is_file():
            raise ProfileError('system WPR is not installed')
        self.pid = plan['pid']
        self.handle = self.kernel.OpenProcess(0x1000, False, self.pid)
        if not self.handle:
            raise ProfileError('cannot open target with query-only access')
        try:
            self.identity = self._identity()
            if not os.path.samefile(self.identity['path'], plan['target']['path']):
                raise ProfileError('PID executable does not match pinned target')
        except BaseException:
            self.close()
            raise

    def _identity(self):
        ct, wt = self.ctypes, self.wintypes
        path, length = ct.create_unicode_buffer(32768), wt.DWORD(32768)
        times = [wt.FILETIME() for _ in range(4)]
        if not self.kernel.QueryFullProcessImageNameW(self.handle, 0, path, ct.byref(length)):
            raise ProfileError('cannot identify target executable')
        if not self.kernel.GetProcessTimes(self.handle, *(ct.byref(x) for x in times)):
            raise ProfileError('cannot identify target creation time')
        return {'path': str(Path(path.value).resolve()),
                'creation_filetime': (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime}

    def observe(self):
        code, front = self.wintypes.DWORD(), self.wintypes.DWORD()
        if not self.kernel.GetExitCodeProcess(self.handle, self.ctypes.byref(code)):
            raise ProfileError('cannot query target liveness')
        self.user.GetWindowThreadProcessId(self.user.GetForegroundWindow(),
                                         self.ctypes.byref(front))
        return {'alive': code.value == 259, 'foreground_pid': front.value,
                'identity': self.identity}

    def command(self, args):
        # No timeout/retry/cancel: interrupt handling in record saves this instance.
        result = subprocess.run([str(self.wpr), *args], capture_output=True,
                                text=True, errors='replace', shell=False,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        return {'args': args, 'returncode': result.returncode,
                'stdout': result.stdout, 'stderr': result.stderr}

    def now(self):
        return time.monotonic()

    def sleep(self, seconds):
        time.sleep(seconds)

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def record(plan, backend):
    """One start and at most one stop, always scoped to a fresh instance name.

    A failed/uncertain start is never followed by an automatic stop or retry.
    State is persisted before invoking WPR so manual recovery is possible.
    """
    same_pin(plan['target'])
    for tool in plan['tools']:
        same_pin(tool)
    first = backend.observe()
    ready_deadline = backend.now() + plan['ready_timeout_s']
    while first['alive'] and first['foreground_pid'] != plan['pid'] and backend.now() < ready_deadline:
        backend.sleep(0.25)
        first = backend.observe()
    if not first['alive'] or first['foreground_pid'] != plan['pid']:
        raise ProfileError('target must already be alive and become foreground within readiness timeout')
    output = Path(plan['output'])
    output.mkdir()  # exclusive; preserve all earlier runs
    write_json(output / 'plan.json', plan)
    state = {'schema': plan['schema'], 'started_utc': utc(),
             'status': 'START_PENDING', 'commands': [], 'observations': [],
             'error': None, 'target': plan['target'], 'pid': plan['pid'],
             'trace_quality': 'UNREVIEWED', 'optimization_verdict': 'NOT_EVALUATED'}
    # Immutable checkpoints survive exceptions, process termination and Ctrl-C.
    write_json(output / 'start-pending.json', state)
    started = False
    try:
        result = backend.command(plan['start'])
        state['commands'].append(result)
        if result['returncode']:
            raise ProfileError('WPR start failed; see command output and recovery instructions')
        started = True
        state['status'] = 'RECORDING'
        write_json(output / 'recording.json', state)
        begin = backend.now()
        while True:
            obs = backend.observe()
            obs['elapsed_s'] = backend.now() - begin
            state['observations'].append(obs)
            if not obs['alive']:
                raise ProfileError('target exited during recording')
            if obs['identity'] != first['identity']:
                raise ProfileError('target identity changed')
            if obs['foreground_pid'] != plan['pid']:
                raise ProfileError('target lost foreground; capture is qualified as failed')
            if obs['elapsed_s'] >= plan['duration_s']:
                break
            backend.sleep(min(0.25, plan['duration_s'] - obs['elapsed_s']))
        state['status'] = 'RECORDED'
    except (Exception, KeyboardInterrupt) as exc:
        state['status'] = 'FAILED'
        state['error'] = str(exc) or type(exc).__name__
    finally:
        if started:
            # Save partial evidence on every exit, never cancel or discard it.
            try:
                write_json(output / 'stop-pending.json', state)
            except (OSError, ValueError) as exc:
                # Disk/write errors must not prevent stopping our recording.
                state['status'] = 'FAILED'
                state['error'] = (state['error'] or '') + '; checkpoint: ' + str(exc)
            try:
                result = backend.command(plan['stop'])
                state['commands'].append(result)
                if result['returncode']:
                    raise ProfileError('WPR stop failed; named instance may still be recording')
                etl = output / 'capture.etl'
                if not etl.is_file() or not etl.stat().st_size:
                    raise ProfileError('WPR returned success without a nonempty ETL')
                state['etl'] = pin(etl)
                same_pin(plan['target'])
            except (Exception, KeyboardInterrupt) as exc:
                state['status'] = 'FAILED'
                state['error'] = (state['error'] or '') + '; stop: ' + (str(exc) or type(exc).__name__)
        state['finished_utc'] = utc()
        write_json(output / 'result.json', state)
    return state


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['plan', 'record', 'doctor'])
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--pid', type=int)
    parser.add_argument('--seconds', type=float)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--confirm-system-recording', action='store_true')
    args = parser.parse_args(argv)
    backend = None
    try:
        if args.action == 'doctor':
            result = capabilities()
            print(json.dumps(result, indent=2))
            return 0 if result['system_profile_privilege_available'] else 1
        if any(value is None for value in (args.exe, args.pid, args.seconds, args.out)):
            raise ProfileError('plan/record require --exe, --pid, --seconds and --out')
        if args.action == 'record' and not args.confirm_system_recording:
            raise ProfileError('record requires --confirm-system-recording; plan is inert')
        plan = make_plan(args.exe, args.pid, args.seconds, args.out)
        if args.action == 'plan':
            print(json.dumps(plan, indent=2))
            return 0
        if not capabilities()['system_profile_privilege_available']:
            raise ProfileError('current token lacks SeSystemProfilePrivilege; recording requires a separately authorized privileged session')
        backend = WindowsBackend(plan)
        result = record(plan, backend)
        print(json.dumps(result, indent=2))
        return 0 if result['status'] == 'RECORDED' else 1
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    finally:
        if backend:
            backend.close()


if __name__ == '__main__':
    sys.exit(main())
