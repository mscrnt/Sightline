"""Repeatable offline verification. Never launches a game or a graphics API.

Default: standard-library synthetic Python tests only, in this process.
--native-mocks: additionally compile the fixed mock sources, inspect each
resulting binary's DLL imports and execute each one in a new output root. No
game build, game binary, window or graphics API is involved, and there is no
generic build or run interface: only the fixed source list below is accepted.

Two executables are produced, not one. Each fixed test source defines its own
main(), so linking them together is a duplicate-symbol error, and dropping
either silently retires every check that source carries - including the
frame-identity regression, which is the only offline check that fails when the
native pump reorders its end-frame, present-note, begin-frame and counter
increment.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from common import ProfileError, pin, write_json

ALLOWED_IMPORTS = {'kernel32.dll', 'msvcrt.dll'}
# Compiled into every mock binary: the producer under test plus the synthetic
# GL, clock, environment and frame counter it is linked against.
SHARED_MOCK_SOURCES = ('src/gfx/sl_gfx_profile.c', 'native/sl_gfx_profile_testgl.c')
MOCK_HEADERS = ('src/gfx/sl_gfx_profile.h', 'native/sl_gfx_profile_testgl.h')
# (binary stem, its own main source). Fixed; not configurable from the CLI.
MOCK_BINARIES = (('renderer-profile-mock', 'native/test_sl_gfx_profile.c'),
                 ('frame-identity-regression', 'native/regress_frame_identity.c'))


def mock_source(name):
    """Resolve one fixed mock input. 'src/...' is engine source, else a fixture."""
    return (ROOT / name) if name.startswith('src/') else (HERE / name)


def native_mocks(output, compiler):
    if os.name != 'nt':
        raise ProfileError('native mock command currently supports the Windows MinGW toolchain only')
    output = output.resolve()
    if output.exists() or not output.parent.is_dir() or output.is_relative_to(ROOT):
        raise ProfileError('mock output must be new, outside the source tree, under an existing parent')
    if len(str(output)) > 700:
        raise ProfileError('mock output path is too long for the fixed C test fixture paths')
    compiler = compiler.resolve(strict=True)
    objdump = compiler.parent / 'objdump.exe'
    if not objdump.is_file():
        raise ProfileError('objdump.exe is required beside the selected compiler')
    shared = [mock_source(x) for x in SHARED_MOCK_SOURCES]
    headers = [mock_source(x) for x in MOCK_HEADERS]
    mains = [mock_source(x) for _, x in MOCK_BINARIES]
    # A missing fixed source refuses rather than building a smaller suite.
    for path in [*shared, *headers, *mains]:
        if not path.is_file():
            raise ProfileError('fixed mock source is missing: ' + str(path))
    inputs = [pin(p) for p in [compiler, objdump, *shared, *mains, *headers]]
    output.mkdir()
    env = dict(os.environ)
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    commands = []
    binaries = []

    def run(args, name):
        result = subprocess.run(list(map(str, args)), capture_output=True, text=True,
                                env=env, cwd=output, timeout=120, shell=False)
        commands.append({'argv': list(map(str,args)), 'exit_code': result.returncode})
        with (output/(name+'.txt')).open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(result.stdout + result.stderr)
        if result.returncode:
            raise ProfileError(name+' failed; partial outputs preserved in '+str(output))
        return result.stdout

    try:
        for stem, main_name in MOCK_BINARIES:
            exe = output/(stem+'.exe')
            run([compiler,'-m32','-O2','-Wall','-Wextra','-Werror=implicit-function-declaration',
                 '-DSL_GFX_PROFILE_TEST','-I'+str(ROOT/'src/gfx'),'-I'+str(HERE/'native'),
                 *shared, mock_source(main_name),'-o',exe], stem+'-compile')
            # Imports are inspected before this binary runs, every binary.
            imports = run([objdump,'-p',exe], stem+'-imports')
            dlls = re.findall(r'DLL Name:\s*(\S+)',imports)
            if not dlls or any(x.lower() not in ALLOWED_IMPORTS for x in dlls):
                raise ProfileError('mock '+stem+' has unexpected imports; refusing execution')
            csv_root=output/(stem+'-synthetic-csv');csv_root.mkdir()
            run([exe,csv_root], stem+'-tests')
            binaries.append({'name':stem,'main_source':main_name,'imports':dlls})
        result={'schema':'sightline-offline-check/1','status':'MOCK_TESTS_PASSED',
                'qualification':'fabricated GL, clocks and frame counters; no native graphics, game or optimization measurement',
                'inputs':inputs,'commands':commands,'binaries':binaries,
                'imports':sorted({x for b in binaries for x in b['imports']})}
    except (OSError, subprocess.SubprocessError, ProfileError) as error:
        write_json(output/'result.json',{'schema':'sightline-offline-check/1',
                   'status':'FAILED','error':str(error),'inputs':inputs,
                   'commands':commands,'binaries':binaries})
        raise
    write_json(output/'result.json',result)
    return result


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-mocks',action='store_true')
    parser.add_argument('--out',type=Path)
    parser.add_argument('--compiler',type=Path,default=Path('C:/msys64/mingw32/bin/i686-w64-mingw32-gcc.exe'))
    args=parser.parse_args(argv)
    if args.native_mocks != (args.out is not None):
        parser.error('--native-mocks and a new --out are required together')
    tests=unittest.defaultTestLoader.discover(str(HERE),pattern='test_*.py')
    if not unittest.TextTestRunner(verbosity=2).run(tests).wasSuccessful():
        return 1
    if args.native_mocks:
        try:
            native_mocks(args.out,args.compiler)
        except (OSError,subprocess.SubprocessError,ProfileError) as error:
            print(str(error),file=sys.stderr)
            return 1
    print('Offline verification passed. No game or live graphics measurement ran.')
    return 0


if __name__=='__main__':
    sys.exit(main())
