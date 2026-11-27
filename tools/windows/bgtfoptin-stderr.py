#!/usr/bin/env python3
"""Verify what the opt-in candidate's launches actually SAID.

The fixture asserts what each launch DID; this reads the stderr those same
launches wrote and asserts what each one CLAIMED. The two are different
properties, and the one this file exists for is the one a reader of a live run
depends on: a launch that selects FINISH must not report FENCE, a launch that
keeps the main-context prepass must say so, and a refusal must name its selector
without quoting the value it was given.

    python bgtfoptin-stderr.py <dir with optin-<name>-stderr.txt>

Exit 0 when every expectation held, 1 otherwise. Standard library only; it reads
files and launches nothing.

THE UNREADABLE VALUES THE LAUNCHES WERE GIVEN are listed in NEVER below. They
are the driver's own invented tokens, and the point of listing them is that NO
line of any launch may contain one: an environment value must not reach this
module's output, and a refusal that quoted the value would be the one way it
could.
"""
import sys
from pathlib import Path

NEVER = ('GPU_SOURCEX', 'FENCEX')

SELECTION = '#95 the background GPU clip prepass\'s OPT-IN CANDIDATE is in force'
STANDDOWN = '#95 the background GPU clip prepass\'s OPT-IN CANDIDATE STANDS DOWN'
OFFTOKEN = '#95 SL_BG_TF_AUX_REAL names an OFF token'
ARMCLOSED = 'that unreadable arm was offered'
SYNCCLOSED = '#95 SL_BG_TF_AUX_SYNC was given a value that is neither FENCE nor FINISH'

# name -> (lines that MUST be present, lines that must NOT be present)
EXPECT = {
    # The candidate with nothing else named: it announces itself, names FENCE,
    # and says no key named it.
    'implicit': (
        [SELECTION, 'with FENCE synchronisation, which no key named'],
        [STANDDOWN, OFFTOKEN, SYNCCLOSED, 'with FINISH synchronisation'],
    ),
    'sync_default': (
        [SELECTION, 'with FENCE synchronisation, which no key named'],
        [STANDDOWN, OFFTOKEN, SYNCCLOSED],
    ),
    # The key named FENCE: the same mode, and the line says the launch named it.
    'sync_fence': (
        [SELECTION, 'with FENCE synchronisation, which this launch NAMED'],
        [STANDDOWN, SYNCCLOSED, 'which no key named'],
    ),
    # THE LAUNCH THE EARLIER SINGLE SENTENCE WAS FALSE FOR. It must read FINISH,
    # and the word FENCE must not appear in the mode's position.
    'sync_finish': (
        [SELECTION, 'with FINISH synchronisation, which this launch NAMED',
         'SL_BG_TF_AUX_SYNC=FINISH'],
        [STANDDOWN, SYNCCLOSED, 'with FENCE synchronisation',
         'SL_BG_TF_AUX_SYNC=FENCE'],
    ),
    # Default off: the candidate does not exist, so it says nothing at all.
    'off': (
        [],
        [SELECTION, STANDDOWN, OFFTOKEN, ARMCLOSED, SYNCCLOSED],
    ),
    # An off token: the main-context prepass is labelled as the diagnostic.
    'explicit': (
        [OFFTOKEN, 'KNOWN MISMATCH'],
        [SELECTION, STANDDOWN, SYNCCLOSED],
    ),
    # A named arm: no candidate line of any kind. Its output is unchanged.
    'named': (
        [],
        [SELECTION, STANDDOWN, OFFTOKEN, ARMCLOSED, SYNCCLOSED],
    ),
    # A diagnostic: the candidate stands down and says which prepass runs.
    'standdown': (
        [STANDDOWN, 'MAIN-CONTEXT prepass'],
        [SELECTION, SYNCCLOSED],
    ),
    # An unreadable arm: the fail-closed line, and no selection line.
    'closed': (
        [ARMCLOSED, 'FAILS CLOSED', 'EXISTING CPU raster path'],
        [SELECTION, SYNCCLOSED],
    ),
    # An unreadable synchronisation value, on each arm: the keyed refusal, and
    # no claim that any arm is in force.
    'sync_bad': (
        [SYNCCLOSED, 'FAILS CLOSED', 'EXISTING CPU raster', 'is REFUSED'],
        [SELECTION, 'with FENCE synchronisation', 'with FINISH synchronisation'],
    ),
    'sync_bad_named': (
        [SYNCCLOSED, 'FAILS CLOSED', 'EXISTING CPU raster', 'is REFUSED'],
        [SELECTION, 'with FENCE synchronisation', 'with FINISH synchronisation'],
    ),
    # A missing fence entry point on the candidate: the capability refusal, and
    # the selection line is still true because the arm WAS selected.
    'cap_main': (
        [SELECTION, 'the MAIN GL context did not resolve',
         'REFUSED rather than served by the original two glFinish calls'],
        [STANDDOWN, SYNCCLOSED],
    ),
}


def main(argv):
    d = Path(argv[1]) if len(argv) > 1 else Path.cwd()
    passed = failed = 0
    for name in sorted(EXPECT):
        must, must_not = EXPECT[name]
        p = d / ('optin-%s-stderr.txt' % name)
        if not p.exists():
            print('FAIL  %-15s no stderr capture at %s' % (name, p))
            failed += 1
            continue
        text = p.read_text(encoding='utf-8', errors='replace')
        bad = []
        for needle in must:
            if needle not in text:
                bad.append('missing: %s' % needle)
        for needle in must_not:
            if needle in text:
                bad.append('present but must not be: %s' % needle)
        for needle in NEVER:
            if needle in text:
                bad.append('ECHOED THE VALUE IT WAS GIVEN: %s' % needle)
        if bad:
            print('FAIL  %-15s %d problem(s)' % (name, len(bad)))
            for b in bad:
                print('        %s' % b)
            failed += 1
        else:
            print('PASS  %-15s %d required, %d forbidden, no value echoed'
                  % (name, len(must), len(must_not)))
            passed += 1
    print('\n%d passed, %d failed' % (passed, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
