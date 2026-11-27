#!/usr/bin/env python3
"""Offline seam check for the background GPU clip prepass's TWO SELECTIONS.

Launches nothing, compiles nothing, needs no toolchain and reads no environment
variable of the product's. It reads the product sources, the row's policy
header, the release-facing documents that describe the row, and the live witness
checker, and asserts the properties that can be decided from them.

v0.4 THERE ARE TWO SELECTIONS NOW, AND THE WHOLE POINT IS THAT THEY REACH
DIFFERENT ARMS. The developer switch SL_BG_TF_CLIP=1 reaches everything in the
module, the MAIN-CONTEXT prepass included; the OPTIMIZED RENDERING row reaches
the AUXILIARY-CONTEXT real prepass on arm GPU_SOURCE with FENCE and nothing
else. The main-context arm and the background vertex program were each measured
ONE BLUE VALUE APART from the original renderer's frames at (71,113), 142
against 143, and the auxiliary arm BYTE EXACT there - so the properties this
file is for are mostly about what the PRODUCTION route cannot reach:

    the ROW's selection        the row's route puts the auxiliary GPU_SOURCE arm
                               in force with no SL_BG_TF_CLIP, through the one
                               per-walk admission answer
    OFF is the ORIGINAL        with the row off the caller never asks, so no arm
                               is promoted, no GL call is issued and no
                               auxiliary context is created
    the PER-WALK rule          the row is resolved once per WALK by the caller
                               and is cached nowhere in the module, so a menu
                               moving OFF -> ON -> OFF acts at walk boundaries
    the MAIN-CONTEXT guard     no absence, default or unreadable value can put
                               the product on the main-context arm or on the
                               vertex program
    refusals land on the CPU   a fail-closed launch and a per-draw capability
                               refusal both go to the existing CPU clip and
                               raster path
    the FROZEN wordings        every refusal and diagnostic sentence earlier
                               rounds match on is present byte for byte, and the
                               image-sensitive decoders are untouched
    NO FRAME-RATE PROMISE      neither the product's lines nor the release-facing
                               documents promise a frame rate for the row

AND THE OLDER PROPERTIES ARE UNCHANGED:

    the EMITTED-LINE seam     every line src/gfx/sl_gfx_bgtf.c and
                              src/gfx/sl_gfx_dl.c emit under the module's census
                              prefix must parse as a census or be one of the
                              four diagnostics tools/windows/perflive-check.py
                              exempts, or that checker reads a healthy run as a
                              DRIFTED census. This is a real coupling between
                              two files and the one this check exists for.
    the SELECTION guards      every site that can put an auxiliary arm in force
                              is behind its own route's terms - the opt-in
                              switch answering 1, or the row's eligibility
    the KEY SET               no environment key was added to the module, and
                              the production selection adds none either
    the IMPLICIT grant        each route grants the arm from exactly one site,
                              and each records that the selection was implicit
    the SYNC default          the default synchronisation mode is chosen FROM
                              that record, and the named arm's refusal wording
                              is unchanged byte for byte
    the FAIL-CLOSED order     the unreadable-arm refusal is taken BEFORE the
                              arm block and before anything is issued
    no VALUE is echoed        the candidate's own lines carry no conversion that
                              could print an environment value

WHAT IT DOES NOT DO. It is a source and output-grammar check. It executes no
product code, so it is evidence about the text of the decision and not about the
decision running: the behavioural cases live in the module fixture
(tools/profiling/native/test_sl_gfx_bgtf.c, mode "optin"), which needs a build.

    python bgtfoptin-check.py [<repo root>]

Exit 0 when every check passed, 1 otherwise. Standard library only.
"""
import importlib.util
import re
import sys
from pathlib import Path

# The module's own emitted-line grammar lives in the witness checker; it is
# loaded rather than restated so the two cannot drift apart.
CHECKER = Path('tools/windows/perflive-check.py')
BGTF = Path('src/gfx/sl_gfx_bgtf.c')
DL = Path('src/gfx/sl_gfx_dl.c')
# v0.4 THE ROW'S POLICY AND THE DOCUMENTS THAT DESCRIBE IT. The row is what the
# production selection IS, so a claim about it that the row's own header or the
# release-facing text contradicts is a defect in the same change. Read for one
# property only - that no frame rate is promised - and never parsed.
PERF_H = Path('src/platform/sl_perf.h')
# v0.4 THE ROW'S POLICY SOURCE. Read for ONE property: the accessor that says a
# developer named the vertex transform must answer 0 once the player has set the
# row, or an inherited SL_BG_GPU_XFORM=1 would put the 142 arm back behind a menu
# that had just been set - and it must add no environment key of its own.
PERF_C = Path('src/platform/sl_perf.c')
BGTF_H = Path('src/gfx/sl_gfx_bgtf.h')
DOCS = (Path('readme.md'), Path('docs/backlog.md'), PERF_H, BGTF_H,
        Path('src/platform/sl_settings.h'))

# Every environment key the module may read. A key added to the module without
# being added here fails this check, which is what makes "the default path reads
# nothing new" checkable rather than asserted.
KEYS = {
    'SL_BG_GLSTATE',
    'SL_BG_TF_AUX_EMPTY',
    'SL_BG_TF_AUX_REAL',
    'SL_BG_TF_AUX_SYNC',
    'SL_BG_TF_CLIP',
    'SL_BG_TF_DUMP',
    'SL_BG_TF_DUMP_DIR',
    'SL_BG_TF_DUMP_EXE_SHA',
    'SL_BG_TF_DUMP_SRC_SHA',
    'SL_BG_TF_EMPTY_STATE',
    'SL_BG_TF_NEUTRAL_PROG',
    'SL_BG_TF_POS_INPUT',
    'SL_BG_TF_POST_RESET',
    'SL_BG_TF_PRIVATE_VAO',
    'SL_BG_TF_RASTER_CPU',
    'SL_BG_TF_STATE_ONLY',
    'SL_BG_TF_ZERO_COUNT',
}

# WORDINGS THAT MUST NOT COME BACK. Each served a launch that had selected
# NEITHER synchronisation mode with one of the two anyway - the candidate with
# FENCE, a named arm with FINISH - and each quoted the raw value while doing it.
# An invalid nonempty value now refuses the arm, so a module containing either of
# these strings has regressed to choosing a mode for such a launch.
GONE = (
    'keeps its ORIGINAL synchronisation',
    'keeps its own synchronisation',
    'SL_BG_TF_AUX_SYNC="%s"',
)

# THE FIVE STATEMENTS THE CANDIDATE MAKES, each from exactly one place. A second
# source for any of them is a launch that could be told the same thing twice, or
# told it by two sites that may disagree; a missing one is a launch left to infer
# which prepass produced its frames.
SAYS = (
    ('selection', 'is in force: no arm was named'),
    ('stand-down', 'OPT-IN CANDIDATE STANDS DOWN'),
    ('off-token label', 'names an OFF token'),
    ('arm failed closed', 'that unreadable arm was offered'),
    ('sync failed closed', 'SL_BG_TF_AUX_SYNC was given a value'),
    ('census', 'opt-in candidate census'),
    # v0.4 THE PRODUCTION SELECTION'S OWN TWO, kept separate from the
    # candidate's rather than folded into them with a conditional clause. The
    # candidate's sentences say there is no settings row and no menu item,
    # which is TRUE of the developer switch and would be false of the row, so
    # one line with a branch in it would have to be false for one of the two
    # launches that print it.
    ('production selection', 'is the PRODUCTION selection of the OPTIMIZED'),
    ('production census', 'production selection census'),
)

# v0.4 THE FROZEN SENTENCES. Earlier rounds' evidence and the runners that read
# it match on these exact strings, and this round widened the conditions under
# which two of them are reached without touching a word of any. A missing one
# means a reader of a refused or stood-down launch is now matching on text that
# is not there.
FROZEN = (
    'a known mismatch is not something a mistyped selector may select',
    'NO AUXILIARY GL CONTEXT IS CREATED',
    'ONE BLUE VALUE APART',
    'every served draw is refused to the EXISTING CPU raster path',
    'Spell FENCE or FINISH, or name neither',
    'Spell an arm token or spell an OFF token',
)

# v0.4 THE IMAGE-SENSITIVE DECODERS, AS NAME AND VALUE TOGETHER. The empty-draw
# rows and the post-reset arms decide which GL commands bracket a draw, and
# every recorded image under them is read against the row it names. The
# production selection composes with NONE of them - each is a term of the
# eligibility below, so a launch holding one is not a production launch - so
# this round may not move a token OR A NUMBER of either table.
#
# THE NUMBER IS CHECKED AND NOT ONLY THE NAME, because a renumber is the one
# change that leaves every name present while making an earlier record's row
# name mean a different bracket. Matched against the enum declarations.
IMAGE_SENSITIVE = (
    ('SL_BGTF_ES_OFF', 0), ('SL_BGTF_ES_A', 1), ('SL_BGTF_ES_B', 2),
    ('SL_BGTF_ES_C', 3), ('SL_BGTF_ES_E', 4), ('SL_BGTF_ES_R', 5),
    ('SL_BGTF_ES_S', 6), ('SL_BGTF_ES_Q', 7),
    ('SL_BGTF_PR_OFF', 0), ('SL_BGTF_PR_EMPTY', 1), ('SL_BGTF_PR_FINISH', 2),
    ('SL_BGTF_PR_BOTH', 3),
)

# v0.4 EVERY TERM THE PRODUCTION ELIGIBILITY MUST NAME, as the arbiter's own
# identifiers. Each one is a shape that would otherwise let the product reach
# the MAIN-CONTEXT prepass, or a diagnostic of it, by absence rather than by a
# developer naming it. A term dropped from that expression is exactly the defect
# this round exists to prevent, so the expression is read and each term is
# required in it by name.
ELIGIBILITY_TERMS = (
    'sl_bgtf_opt_in() != 1',      # the developer route has not already decided
    'tf_clip_named_off()',        # ... and nobody named the prepass off
    'ar_absent',                  # no arm and no OFF token was spelled
    'ar_raw == SL_BGTF_AR_OFF',   # ... and the value offered was readable
    'raw_so', 'raw_pi', 'raw_pv', # the three older main-context diagnostics
    'zc_asked', 'ax_asked', 'np_asked',
    'es_raw == SL_BGTF_ES_OFF',
    'pr_raw == SL_BGTF_PR_OFF',
    'sl_bgtf_raster_cpu() != 1',  # the arm's own raster source
    'SL_BG_TF_DUMP', 'SL_BG_GLSTATE',   # and neither capture armed
)

# v0.4 WORDS A FRAME-RATE PROMISE WOULD BE MADE OF. The owner decision this
# round implements is "finish the pixel fix for production; no 1080p60 claim",
# and the measurement behind it is an EXACT IMAGE: the relative timing screen
# passed while the original renderer and the selected path both MISSED the
# absolute p99 target. So the row may promise a picture and may not promise a
# rate, in the product's own emitted lines or in the documents that describe it.
# Matched case-insensitively on the rendered text.
# "frames per second" is deliberately NOT here. It is a unit, not a figure, and
# these documents use it to explain the simulation's own tick-to-frame ratio in
# places that say nothing about this row; a check that flagged the unit would
# have to exempt more text than it examined.
RATE_PROMISES = ('1080p60', '1080p 60', '60 fps', '60fps', 'sixty frames')

# THE RATE FIGURES THESE DOCUMENTS ARE ALLOWED TO CARRY, DECLARED RATHER THAN
# INFERRED. An earlier draft of this check exempted a figure whose SURROUNDINGS
# disclaimed a promise, and a mutant that wrote "the renderer holds 1080p60" in
# its own paragraph next to the disclaimer passed - the disclaimer laundered it.
# So the rule is closure: every rate figure in these files is one of the
# declared ones, and anything else fails until it is classified deliberately.
#
# Each entry is a distinctive substring of the sentence that carries the figure.
# All three are about the NATIVE BUILD's game speed and its frame pacer -
# measurements that predate this row and say nothing about it.
RATE_ALLOWED = (
    '"60 fps and Rare\'s game speed" note needs re-taking',
    'The native build runs at 60 fps and Rare\'s game speed. Measured',
    'The 60 fps pacer holds',
)

ESCAPES = {'n': '\n', 't': '\t', '"': '"', '\\': '\\', '0': '\0', 'r': '\r'}

# EVERY LINE THE TWO SOURCES EMIT UNDER THE MODULE'S CENSUS PREFIX, and what
# makes each acceptable to the witness checker. The first five are the shapes
# that checker knows: one census it parses and four diagnostics it exempts by
# name. The last three carry the prefix and are NOT exempt there, and they are
# acceptable only because each is reachable solely on a launch that set its own
# diagnostic switch - which no arm of the live batch does. A new line under this
# prefix would be read as a drifted census on an ordinary run, so it must be
# added here deliberately or keyed by its selector's name instead.
PREFIXED = (
    ('census', 'prepass: %s, %lu prepass',
     'the census the checker parses'),
    ('ready', 'prepass ready - eligible',
     'exempt: RE_TF_READY'),
    ('unavailable', 'prepass unavailable: ',
     'exempt: RE_TF_UNAVAIL'),
    ('no-compile', 'prepass: program did not compile: ',
     'exempt: RE_TF_NOCOMPILE'),
    ('no-link', 'prepass: program did not link: ',
     'exempt: RE_TF_NOLINK'),
    ('raster-cpu', 'prepass ORIGINAL-RASTER CONTROL',
     'reachable only with SL_BG_TF_RASTER_CPU=1'),
    ('state-only', 'prepass STATE-ONLY CONTROL',
     'reachable only with SL_BG_TF_STATE_ONLY=1'),
    ('pos-input', 'prepass POSITION-ONLY-INPUT CONTROL',
     'reachable only with SL_BG_TF_POS_INPUT=1'),
)


def literals(text):
    """Every C string literal in `text`, with adjacent literals concatenated.

    Returns a list of (line number of the run's first literal, value). Runs are
    joined exactly as the compiler joins them - only whitespace, newlines and
    comments may separate two literals - so one emitted line is one entry and a
    prefix split across source lines is still found.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j + 1
            continue
        if c == "'":
            # a character constant: skip it, escapes included
            i += 1
            while i < n and text[i] != "'":
                i += 2 if text[i] == '\\' else 1
            i += 1
            continue
        if c != '"':
            i += 1
            continue
        start = i
        parts = []
        while i < n and text[i] == '"':
            i += 1
            buf = []
            while i < n and text[i] != '"':
                if text[i] == '\\' and i + 1 < n:
                    buf.append(ESCAPES.get(text[i + 1], text[i + 1]))
                    i += 2
                else:
                    buf.append(text[i])
                    i += 1
            i += 1
            parts.append(''.join(buf))
            # skip whitespace and comments between adjacent literals
            while i < n:
                if text[i].isspace():
                    i += 1
                elif text[i] == '/' and i + 1 < n and text[i + 1] == '*':
                    j = text.find('*/', i + 2)
                    i = n if j < 0 else j + 2
                else:
                    break
        out.append((text.count('\n', 0, start) + 1, ''.join(parts)))
    return out


def strip_comments(text):
    """`text` with every C comment replaced by one space, literals preserved.

    A check about WHAT THE CODE READS cannot be run over a file whose comments
    name every entry point it deliberately does NOT call: this module's own
    prose names sl_bggpu_opt_in in order to say that the row is the caller's to
    resolve, which a naive search reads as a row read. Literals are kept because
    a separate check scans them.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            out.append(' ')
            i = n if j < 0 else j + 2
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            out.append(' ')
            i = n if j < 0 else j + 1
            continue
        if c in '"\'':
            q = c
            start = i
            i += 1
            while i < n and text[i] != q:
                i += 2 if text[i] == '\\' else 1
            i += 1
            out.append(text[start:i])
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def braced_block(text, idx):
    """The statement block that opens at the first '{' at or after `idx`."""
    i = text.find('{', idx)
    if i < 0:
        return ''
    depth = 0
    n = len(text)
    while i < n:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[idx:i + 1]
        i += 1
    return text[idx:]


def enclosing_call(text, idx):
    """The whole fprintf call containing offset `idx`, parentheses balanced.

    Scanning has to skip string literals: these format strings contain both
    semicolons and parentheses, so a search for the next ';' or ')' lands inside
    the sentence and truncates the argument list - which would make a check about
    WHERE A TOKEN COMES FROM pass or fail on the wording instead.
    """
    start = text.rfind('fprintf', 0, idx)
    if start < 0:
        return ''
    i = text.index('(', start)
    depth = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in '"\'':
            q = c
            i += 1
            while i < n and text[i] != q:
                i += 2 if text[i] == '\\' else 1
            i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return text[start:]


def load_checker(root):
    path = root / CHECKER
    spec = importlib.util.spec_from_file_location('perflive_check', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Report:
    def __init__(self):
        self.failed = 0
        self.passed = 0

    def check(self, ok, what, detail=''):
        if ok:
            self.passed += 1
            print('PASS  %s' % what)
        else:
            self.failed += 1
            print('FAIL  %s' % what)
            if detail:
                for line in str(detail).splitlines():
                    print('        %s' % line)


def main(argv):
    root = Path(argv[1]).resolve() if len(argv) > 1 else Path.cwd()
    r = Report()
    bgtf = (root / BGTF).read_text(encoding='utf-8', errors='strict')
    dl = (root / DL).read_text(encoding='utf-8', errors='strict')
    bgtf_h = (root / BGTF_H).read_text(encoding='utf-8', errors='strict')
    perf_c = (root / PERF_C).read_text(encoding='utf-8', errors='strict')
    # THE CODE WITHOUT ITS PROSE, for the checks about what the module READS.
    # This file's comments name the row's entry points in order to say that the
    # row is the caller's to resolve, so a search over the raw text reads the
    # explanation as the thing it forbids.
    bgtf_code = strip_comments(bgtf)
    chk = load_checker(root)

    # ---- the emitted-line seam -------------------------------------------
    #
    # THE RULE IS CLOSURE OVER THE PREFIX, NOT A GRAMMAR MATCH. A format string
    # cannot be matched against the checker's census regex - that regex reads
    # RENDERED output, where the conversions have become digits and tokens - so
    # what is decidable here is WHICH lines carry the prefix at all. The set is
    # declared below, with what makes each one acceptable, and any line that
    # joins it fails this check until it is classified deliberately. That is the
    # property the coupling needs: a new line under this prefix is read by
    # perflive-check.py as a DRIFTED census rather than as a new fact, so adding
    # one must not be possible by accident.
    found_prefixed = []
    for src, text in ((BGTF, bgtf), (DL, dl)):
        for lineno, value in literals(text):
            if value.startswith('#95') and chk.RE_TF_ANY.search(value):
                found_prefixed.append((src.as_posix(), lineno, value))
    unclassified = []
    seen = set()
    for src, lineno, value in found_prefixed:
        key = None
        for name, needle, _why in PREFIXED:
            if needle in value:
                key = name
                break
        if key is None:
            unclassified.append('%s:%d %s' % (src, lineno, value[:90]))
        else:
            seen.add(key)
    r.check(not unclassified,
            'every emitted line under the census prefix is one of the %d '
            'declared ones (%d carry the prefix)'
            % (len(PREFIXED), len(found_prefixed)),
            '\n'.join(unclassified))
    missing = sorted({n for n, _, _ in PREFIXED} - seen)
    r.check(not missing,
            'and every declared line is still emitted, so the declaration '
            'describes this source and not an older one',
            '\n'.join(missing))
    # AND THE CANDIDATE'S OWN LINES ARE OUTSIDE THAT PREFIX, stated positively
    # rather than left to the closure above: they are keyed by the selector's
    # name, as the two sibling auxiliary census lines are.
    all_lines = [v for _, v in literals(bgtf) + literals(dl)]
    # v0.4 THE PRODUCTION SELECTION'S LINES JOIN THIS SET, for the reason the
    # candidate's are in it: every one of them has to be keyed by the row or by
    # a selector's name rather than by the module's census prefix, or the live
    # witness checker reads an ordinary run as a DRIFTED census - and an
    # ordinary run is now exactly what prints them.
    candidate_lines = [v for v in all_lines
                       if 'OPT-IN CANDIDATE' in v or 'opt-in candidate' in v
                       or 'OPTED INTO' in v
                       or 'PRODUCTION selection' in v
                       or 'production selection' in v
                       or 'SL_BG_TF_AUX_SYNC was given a value' in v]
    said = {name: len([v for v in all_lines if needle in v])
            for name, needle in SAYS}
    r.check(all(c == 1 for c in said.values()),
            "each of the candidate's %d statements is emitted from exactly one "
            'place' % len(SAYS),
            '\n'.join('%s: %d' % (k, v) for k, v in sorted(said.items())
                      if v != 1))
    r.check(not [v for v in candidate_lines if chk.RE_TF_ANY.search(v)],
            "and not one of them carries the module's census prefix",
            '\n'.join(v[:90] for v in candidate_lines
                      if chk.RE_TF_ANY.search(v)))

    # ---- the selection guards --------------------------------------------
    #
    # v0.4 TWO ROUTES REACH THE SAME ARM, so there are two grant sites and each
    # one is checked against ITS OWN route's terms. A grant that is not inside
    # one of the two blocks is a third route, which is the shape that could put
    # the product on an arm nothing selected.
    dev_grant = [m.start() for m in re.finditer(r'(?<!g_)ar = SL_BGTF_AR_GPU;',
                                                bgtf)]
    prod_grant = [m.start() for m in re.finditer(r'g_ar = SL_BGTF_AR_GPU;',
                                                 bgtf)]
    r.check(len(dev_grant) == 1 and len(prod_grant) == 1,
            'exactly two sites put the auxiliary GPU_SOURCE arm in force, one '
            'per route (developer %d, production %d)'
            % (len(dev_grant), len(prod_grant)))
    guard = bgtf.find(
        'if (ar == SL_BGTF_AR_OFF && ar_absent && sl_bgtf_opt_in() == 1) {')
    sync = bgtf.find('if (ar != SL_BGTF_AR_OFF && tf_fx_decide(')
    if len(dev_grant) == 1:
        r.check(guard > 0 and guard < dev_grant[0],
                "the developer route's site is inside the one block guarded by "
                'the opt-in switch answering 1 and by the arm not having been '
                'named')
        r.check(sync > dev_grant[0],
                'which is decided before the synchronisation mode is, so the '
                'arm is known when that key is read (%d/%d)'
                % (dev_grant[0], sync))
        tail = bgtf[dev_grant[0]:dev_grant[0] + 200]
        r.check('g_ar_implicit = 1;' in tail,
                'and it records the selection as IMPLICIT')
    if len(prod_grant) == 1:
        # THE PRODUCTION GRANT IS GUARDED BY ITS OWN ELIGIBILITY AND BY THE
        # SYNCHRONISATION DECISION HAVING SUCCEEDED, in that order: a launch
        # that cannot be read must not reach a grant at all.
        elig = bgtf.find('if (g_prod_elig != 1) {')
        pfx = bgtf.find('if (tf_fx_decide(1, &fx_named) < 0) {')
        r.check(0 < elig < prod_grant[0],
                "the production route's site is behind its eligibility answer, "
                'so an environment that named anything cannot reach it '
                '(%d/%d)' % (elig, prod_grant[0]))
        r.check(0 < pfx < prod_grant[0],
                'and behind the synchronisation decision, so an unreadable '
                'value refuses before the arm is granted (%d/%d)'
                % (pfx, prod_grant[0]))
        tail = bgtf[prod_grant[0]:prod_grant[0] + 400]
        r.check('g_ar_implicit = 1;' in tail and 'g_prod_said = 1;' in tail,
                'and it records the selection as IMPLICIT and as the '
                "PRODUCTION one, which the arm token cannot say")
        r.check('tf_fx_decide(1,' in bgtf,
                "and it takes the IMPLICIT synchronisation default, which is "
                'FENCE - the half the exact-image captures were taken under')

    standdowns = bgtf.count('tf_ar_optin_standdown()')
    r.check(standdowns >= 3,
            'the stand-down statement has one definition and is reached from '
            'every path that keeps the main-context prepass (%d references)'
            % standdowns)

    closed = [m.start() for m in re.finditer(r'g_ar_closed = 1;', bgtf)]
    r.check(len(closed) == 2,
            'the fail-closed latch is raised from exactly two sites, one per '
            'unreadable selector (%d)' % len(closed))
    if len(closed) == 2:
        # CLASSIFIED BY WHAT GUARDS EACH ONE, not by which comes first in the
        # file: the shared stand-down is defined above the arbiter, so an
        # index-ordered reading swapped the two the moment it was extracted.
        heads = [bgtf[max(0, c - 420):c + 40] for c in closed]
        arm_head = [h for h in heads
                    if 'ar_raw < 0' in h and 'sl_bgtf_opt_in() == 1' in h]
        r.check(len(arm_head) == 1,
                'one is raised only for an unreadable ARM on a launch that can '
                'reach a prepass')
        # v0.4 AND THE OTHER LIVES IN THE ONE SHARED STAND-DOWN, because both
        # routes have to refuse an unreadable synchronisation value with the
        # same latch and the same frozen words. A second copy of the sentence
        # would be a second thing to drift.
        sync_head = [h for h in heads
                     if 'static void tf_ar_sync_standdown(void)' in h]
        r.check(len(sync_head) == 1,
                'and the other is inside the ONE shared stand-down, so both '
                'routes refuse an unreadable SYNCHRONISATION mode with the '
                'same latch and the same words')
        calls = bgtf.count('tf_ar_sync_standdown();')
        r.check(calls == 2,
                'which has one definition and is called once per route (%d '
                'calls)' % calls)
        tail = braced_block(bgtf, sync) if sync > 0 else ''
        r.check('ar = SL_BGTF_AR_OFF;' in tail
                and 'g_ar_implicit = 0;' in tail,
                "and the developer route's call also stands the arm down, so "
                'nothing claims the mode ran')

    # ---- v0.4 the production selection -----------------------------------
    #
    # THE ONE PER-WALK ADMISSION ANSWER. Two routes, one answer, one caller: a
    # second predicate at the call site could admit a walk under one route's
    # terms and raster it under another's.
    admit = bgtf.find('int sl_bgtf_walk_admit(void)')
    r.check(admit > 0, 'the module exposes one per-walk admission answer')
    if admit > 0:
        body = bgtf[admit:admit + 400]
        r.check('sl_bgtf_opt_in() == 1' in body and 'tf_prod_arm()' in body,
                'and it answers for both routes: the developer switch first, '
                'then the production selection')
    r.check(dl.count('sl_bgtf_walk_admit()') == 1,
            "the walk's selection asks it exactly once (%d)"
            % dl.count('sl_bgtf_walk_admit()'))
    r.check('else if (sl_bgtf_walk_admit()) {' in dl,
            'and that is the branch which answers INSTEAD of the vertex '
            'program, so an admitted walk cannot take both')
    # AND THE OLD ONE-ROUTE GATE IS GONE FROM THE SELECTION. sl_bgtf_opt_in may
    # still be read by the census lines, which are about which switch was named;
    # it may not be what admits a walk, or the row selects nothing.
    r.check('else if (sl_bgtf_opt_in()) {' not in dl,
            "the selection no longer gates on the developer switch alone, so "
            'the row is what admits an ordinary session')

    # ---- v0.4 EVERY WAY OUT OF THE WALK CHAIN ---------------------------
    #
    # THE PROPERTY A REVIEW FOUND MISSING, AND THE DEFECT IT FOUND. The chain
    # read `... else if (admitted) {prepass} else if (!ready || !use_walk)
    # r_prog++; else {gpu_walk = 1;}`, so with the row ON every walk the prepass
    # did NOT admit fell through to the BACKGROUND VERTEX PROGRAM - the arm
    # measured one blue value apart from the original renderer's frames at
    # (71,113), 142 against 143. The walks that fell there included a launch
    # whose SL_BG_TF_AUX_REAL could not be read, which is a known mismatch
    # reached BY TYPO.
    #
    # SO THE CHAIN IS PARSED, not described. The block guarded by the walk's
    # latched row answer must have exactly three outcomes, in this order: the
    # prepass for an admitted walk, the vertex program ONLY behind an explicit
    # developer naming, and a final `else` that reaches neither.
    chain_start = dl.find('    if (opt_xform) {')
    r.check(chain_start > 0, 'the walk chain is one block behind the row answer')
    if chain_start > 0:
        chain = braced_block(dl, chain_start)
        code = strip_comments(chain)
        # THE VERTEX PROGRAM IS REACHED FROM EXACTLY ONE PLACE in the chain, and
        # that place is guarded by the explicit naming. Two sites, or one that is
        # not guarded, is the defect above.
        prog = [m.start() for m in re.finditer(r'gpu_walk = 1;', code)]
        r.check(len(prog) == 1,
                'the vertex program is admitted from exactly one site in the '
                'chain (%d)' % len(prog))
        named = code.find('else if (sl_perf_bg_xform_named()) {')
        r.check(named > 0 and len(prog) == 1 and named < prog[0],
                'and that site is behind sl_perf_bg_xform_named(), so the '
                'program is reached ONLY when a developer explicitly named it '
                '- never by absence, default or typo (%d/%s)'
                % (named, prog[0] if prog else None))
        # AND THE ADMISSION IS ASKED FIRST, which is what keeps SL_BG_TF_CLIP=1
        # winning over an inherited SL_BG_GPU_XFORM=1 exactly as it did before
        # this branch existed - the live candidate arm sets both.
        adm = code.find('else if (sl_bgtf_walk_admit()) {')
        r.check(0 < adm < named,
                'the admission is asked BEFORE that branch, so a launch setting '
                'both switches still takes the prepass (%d/%d)' % (adm, named))
        # AND THE CHAIN ENDS IN A BARE else THAT REACHES NEITHER GPU PATH. This
        # is the fall-through the defect lived in, so what is checked is the
        # LAST clause: it must be an `else` with no GPU admission in it.
        tail = code[code.rfind('else '):]
        r.check(tail.startswith('else g_bggpu_r_tfstand++;'),
                "the chain's final clause is a bare else that counts a "
                'stand-down and admits NOTHING, so the default way out is the '
                'CPU clip and raster path', tail[:120])
        r.check('sl_bggpu_use_walk' not in tail and 'gpu_walk = 1' not in tail
                and 'sl_bgtf_begin_walk' not in tail,
                'and that clause reaches neither GPU clip computation, which is '
                'the fall-through the first draft put the 142 arm in')
        # NO OTHER PREDICATE MAY ADMIT A WALK. The chain's own pass predicates
        # only count refusals; the three admissions above are the whole set.
        r.check(code.count('sl_bggpu_use_walk') == 1
                and code.count('sl_bgtf_begin_walk') == 1,
                'each GPU clip computation is entered from exactly one place in '
                'the chain (%d/%d)' % (code.count('sl_bggpu_use_walk'),
                                       code.count('sl_bgtf_begin_walk')))
    # AND THE NAMED PROGRAM IS LABELLED WHERE THE FRAMES ARE PRODUCED, which is
    # the other half of requirement (2): it stays reachable, and a reader is
    # told it is a diagnostic rather than the row's output.
    r.check(dl.count('sl_perf_bg_xform_named()') == 2,
            'the explicit naming is asked at the one chain site and stated at '
            'the one census site (%d)' % dl.count('sl_perf_bg_xform_named()'))
    label = [v for v in all_lines
             if 'names the BACKGROUND VERTEX TRANSFORM' in v]
    r.check(len(label) == 1
            and 'DIAGNOSTIC' in label[0] and 'KNOWN MISMATCH' in label[0]
            and '%' not in label[0],
            'and the label says DIAGNOSTIC and KNOWN MISMATCH, exactly once, '
            'with no conversion that could echo a value (%d)' % len(label))
    stand = [v for v in all_lines if 'OPTIMIZED RENDERING stood down' in v]
    r.check(len(stand) == 1
            and 'FULLY PRODUCED CPU clip and raster path' in stand[0]
            and 'NOT the background vertex program' in stand[0],
            'and a stood-down walk is reported, saying where it drew and what '
            'it did NOT draw (%d)' % len(stand))
    # THE TWO NEW LINES ARE OUTSIDE THE MODULE'S CENSUS PREFIX, so neither is
    # read as a drifted census - the closure check above would already have
    # caught it, and this says it positively.
    r.check(not [v for v in label + stand if chk.RE_TF_ANY.search(v)],
            'and neither carries the census prefix, so the live checker reads '
            'them as new facts rather than as a drifted census')

    # ---- v0.4 EVERY GATE ON THE TRACED ROW-ON PATH ----------------------
    #
    # THE DEFECT THIS SECTION EXISTS FOR, AND IT SHIPPED PAST THE LAST CHECK.
    # sl_bgtf_ready() opened with `if (!sl_bgtf_opt_in()) return 0`, which was
    # the whole answer while the developer switch was the only route in. After
    # the row became a route, sl_gfx_dl.c admitted an ordinary row-ON walk
    # through sl_bgtf_walk_admit, called sl_bgtf_ready, got 0 because
    # SL_BG_TF_CLIP was absent, and drew the CPU path - so the production
    # selection could not enter its prepass on ANY launch. No image could show
    # it, the fixture was never compiled, and this file did not look.
    #
    # SO EVERY GATE ON THE PATH IS CHECKED, not just the one that broke. The
    # path a row-ON walk takes inside the module is: readiness, begin_walk, the
    # per-draw prepass, and the auxiliary acquire. A gate on any of them that
    # tests the DEVELOPER SWITCH admits only the developer route.
    TRACED = ('sl_bgtf_ready', 'sl_bgtf_begin_walk', 'sl_bgtf_prepass',
              'tf_ar_acquire')
    for fn in TRACED:
        at = re.search(r'^(?:int|static int) %s\(' % re.escape(fn),
                       bgtf_code, re.M)
        r.check(at is not None, 'the traced path has %s' % fn)
        if at is None:
            continue
        body = braced_block(bgtf_code, at.start())
        # THE EXACT REVERT IS WHAT THIS NAMES FIRST, so a reader of a failure
        # knows which line to look at.
        r.check('sl_bgtf_opt_in()' not in body,
                '%s does not gate on the developer switch, so it cannot admit '
                'only the developer route' % fn,
                'found sl_bgtf_opt_in() inside %s - this is the shape that made '
                'the row\'s walks unreachable' % fn)
        r.check('g_optin' not in body,
                'and %s does not read the switch\'s cached answer directly '
                'either' % fn)
    # AND READINESS ASKS THE ADMISSION, positively: it is not enough that the
    # old test is gone, the new one has to be the one that is there.
    rd = re.search(r'^int sl_bgtf_ready\(', bgtf_code, re.M)
    if rd is not None:
        body = braced_block(bgtf_code, rd.start())
        r.check('if (!sl_bgtf_walk_admit()) {' in body,
                'readiness gates on the ADMISSION answer, so both routes reach '
                'it and neither can disagree with the caller about which '
                'launches prepare a prepass')
        # AND IT ASKS THAT BEFORE RESOLVING ANYTHING, so an unselected launch
        # still resolves nothing and compiles nothing.
        gate = body.find('if (!sl_bgtf_walk_admit()) {')
        first = body.find('sl_gl_proc(')
        r.check(0 < gate < first,
                'and asks it BEFORE the first entry-point resolve, so a launch '
                'with no prepass selected still resolves and compiles nothing '
                '(%d/%d)' % (gate, first))
    # AND THE CALLER ASKS THE ADMISSION BEFORE READINESS, which is where "off
    # costs nothing" now lives: the module cannot see the row, so a row-OFF
    # launch must never reach readiness at all.
    if chain_start > 0:
        chain = braced_block(dl, chain_start)
        code = strip_comments(chain)
        a_at = code.find('sl_bgtf_walk_admit()')
        r_at = code.find('sl_bgtf_ready()')
        r.check(0 < a_at < r_at,
                'the caller asks the admission BEFORE readiness, so a row-OFF '
                'launch never reaches it and resolves nothing (%d/%d)'
                % (a_at, r_at))
        r.check(code.count('sl_bgtf_ready()') == 1,
                'and readiness is asked from exactly one place in the chain '
                '(%d)' % code.count('sl_bgtf_ready()'))

    # AND THE TWO REFUSALS STAY DISTINGUISHABLE. A capability failure is decided
    # at readiness and latches UNAVAILABLE; an unreadable selector is decided at
    # the per-draw prepass site under ar_skips. Collapsing them would make a
    # census say the driver could not do what the launch could not spell.
    r.check('"SL_BG_TF_CLIP is not 1"' not in bgtf,
            "readiness no longer reports 'SL_BG_TF_CLIP is not 1', which was "
            'false for every row-ON launch')
    why = [v for _, v in literals(bgtf)
           if v.startswith('no GPU clip prepass is selected')]
    r.check(len(why) == 1,
            'and its unselected reason names the row and the developer '
            'selectors instead (%d)' % len(why))
    ready_line = [v for v in all_lines
                  if 'prepass ready - eligible background meshes' in v]
    r.check(len(ready_line) == 1 and 'unset or 0' not in ready_line[0],
            "the success line no longer claims 'SL_BG_TF_CLIP unset or 0' "
            'selects the existing paths - an unset switch is the ordinary '
            'production launch that line is printed for')

    # AND THE ACCESSOR BEHIND THAT BRANCH ADDS NO SWITCH AND ENDS AT A PRESS.
    # If it survived an explicit menu choice, a variable the session happened to
    # inherit would put the 142 arm back behind a row the player had just set -
    # which is the same mistake sl_perf_bg_far_admit already records having made.
    # THE OFFSET AND THE SLICE MUST COME FROM THE SAME TEXT. strip_comments
    # collapses each comment to one space, so an offset taken on the raw file
    # and used on the stripped one lands in a different function - which is
    # exactly how the first draft of this check read the wrong body and failed
    # on an unmutated copy.
    perf_c_code = strip_comments(perf_c)
    acc = perf_c_code.find('int sl_perf_bg_xform_named(void)')
    r.check(acc > 0, 'the explicit-naming answer has one definition')
    if acc > 0:
        body = braced_block(perf_c_code, acc)
        r.check('if (s_row_chosen) return 0;' in body,
                'and it answers 0 once the player has set the row, so an '
                'inherited switch cannot put the vertex transform back behind '
                'a menu that was just set', body[:160])
        r.check('perf_env01("SL_BG_GPU_XFORM", &s_e_xform) == 1' in body,
                "and it reads the row policy's existing one-parse of "
                'SL_BG_GPU_XFORM, so only an exact 1 names the transform')
        r.check('getenv(' not in body,
                'and it adds no environment key and no second read of one',
                body[:160])

    # THE ROW IS NOT READ IN THE MODULE, which is what makes the per-walk rule
    # the caller's and keeps one walk on one answer. A read here would be a
    # second answer a press between the two could make disagree, and a static
    # holding one would be the once-per-launch cache this round removes.
    row_reads = [n for n in ('sl_perf_opt(', 'sl_perf_bg_xform(',
                             'sl_bggpu_opt_in(', 'sl_settings_get(')
                 if n in bgtf_code]
    r.check(not row_reads,
            'the module reads no settings row and no row policy, so the row is '
            'resolved once per WALK by the caller and cached nowhere here',
            ', '.join(row_reads))
    # AND THE CALL SITE RESOLVES IT PER WALK. opt_xform is the walk's one
    # latched answer; the admission is asked inside it.
    latch = dl.find('const int opt_xform  = sl_bggpu_opt_in();')
    gate = dl.find('if (opt_xform) {')
    use = dl.find('else if (sl_bgtf_walk_admit()) {')
    r.check(latch > 0 and 0 < gate < use,
            "the caller latches the row once for the walk and asks the "
            'admission inside that answer, so OFF -> ON -> OFF acts at walk '
            'boundaries (%d/%d/%d)' % (latch, gate, use))

    # OFF DOES NOTHING EXTRA. The promotion is reached only from the admission,
    # so a row-off launch never takes it - and the promotion itself issues no GL
    # call and acquires no context, so even reaching it costs no draw.
    prod = bgtf.find('static int tf_prod_arm(void)')
    r.check(prod > 0, 'the production promotion has one definition')
    if prod > 0:
        end = bgtf.find('\n}\n', prod)
        pbody = bgtf[prod:end if end > 0 else prod + 4000]
        issues = [n for n in ('glDraw', 'glUseProgram', 'glBind', 'glEnable',
                              'tf_ar_acquire', 'tf_ax_acquire', 'sl_gfx_gl_')
                  if n in pbody]
        r.check(not issues,
                'and it issues no GL call and acquires no context, so even a '
                'launch that reaches it has drawn nothing extra',
                ', '.join(issues))
        r.check(bgtf_code.count('tf_prod_arm()') == 1,
                'and it is called from the admission answer and nowhere else '
                '(%d calls)' % bgtf_code.count('tf_prod_arm()'))

    # A CENSUS MAY NOT PROMOTE A SELECTION. The census runs on launches that
    # drew nothing, so the reader a census uses has to be pure.
    pub = bgtf.find('int sl_bgtf_production(void)')
    r.check(pub > 0, 'the production fact has a reader of its own')
    if pub > 0:
        pbody = bgtf[pub:pub + 200]
        r.check('tf_prod_arm' not in pbody and 'getenv' not in pbody,
                'and reading it promotes nothing and reads no environment, so '
                'a census on a launch that drew nothing arms no selection')
    r.check(dl.count('sl_bgtf_production()') == 3,
            'the census reads that fact rather than the admission, in the one '
            'tail clause and the two selection censuses (%d)'
            % dl.count('sl_bgtf_production()'))

    # ---- v0.4 what the production route cannot reach ---------------------
    #
    # EVERY TERM OF THE ELIGIBILITY, BY NAME. Each is a shape that would
    # otherwise let an absence put the product on the MAIN-CONTEXT prepass or on
    # one of its diagnostics. A dropped term is the defect this round exists to
    # prevent, and it is not visible in any output.
    estart = bgtf.find('    g_prod_elig = 0;')
    r.check(estart > 0, 'the production eligibility is decided in one place')
    if estart > 0:
        expr = bgtf[estart:bgtf.find('g_prod_elig = 1;', estart) + 40]
        absent = [t for t in ELIGIBILITY_TERMS if t not in expr]
        r.check(not absent,
                'and it names every one of the %d terms that keep an absence '
                'off the main-context arm and its diagnostics'
                % len(ELIGIBILITY_TERMS),
                'missing: %s' % ', '.join(absent))
        r.check('g_prod_elig = 1;' in expr and expr.count('g_prod_elig = 1;') == 1,
                'and it is granted from exactly one point inside that one '
                'expression')

    # AND THE REFUSALS LAND ON THE CPU CLIP PATH. The fail-closed branch leaves
    # the walk ADMITTED, which is what routes its draws through the one refusal
    # at the served-draw site instead of leaving the choice to a second
    # predicate - and that site returns -1, the CPU path, never an arm.
    if prod > 0:
        pfx = bgtf.find('if (tf_fx_decide(1, &fx_named) < 0) {', prod)
        if pfx > 0:
            branch = braced_block(bgtf, pfx)
            r.check('tf_ar_sync_standdown();' in branch
                    and 'return 1;' in branch
                    and 'SL_BGTF_AR_GPU' not in branch,
                    'a production launch that fails closed keeps the walk '
                    'ADMITTED and grants no arm, so every served draw travels '
                    'through the one refusal to the CPU clip path')
        armed = bgtf.find('g_prod_armed = 1;', prod)
        r.check(0 < armed < pfx,
                'and the admission is recorded BEFORE that branch, so the '
                'refusal cannot be reached by a walk the caller then treats as '
                'unadmitted (%d/%d)' % (armed, pfx))

    # ---- v0.4 the frozen wordings and the image-sensitive decoders -------
    # ON THE CONCATENATED LITERALS AND NOT ON THE SOURCE TEXT, for the reason
    # the candidate's own sentence check is: every one of these sentences is
    # split across adjacent literals, so a search of the source would miss a
    # sentence that is present and report one that was deleted as present.
    emitted = '\n'.join(all_lines)
    gone_frozen = [f for f in FROZEN if f not in emitted]
    r.check(not gone_frozen,
            'every one of the %d frozen refusal and diagnostic sentences '
            'earlier rounds match on is still emitted byte for byte'
            % len(FROZEN),
            '\n'.join(gone_frozen))
    gone_img = ['%s = %d' % (t, v) for t, v in IMAGE_SENSITIVE
                if not re.search(r'%s\s*=\s*%d\b' % (re.escape(t), v), bgtf_h)]
    r.check(not gone_img,
            'and the image-sensitive row and arm tables are untouched, name '
            'and NUMBER, so a row name in an earlier record still means what '
            'it meant (%d entries)' % len(IMAGE_SENSITIVE),
            '\n'.join(gone_img))
    # AND THE PRODUCTION ROUTE COMPOSES WITH NONE OF THEM, which is what keeps
    # those records readable: each of those controls is a term of the
    # eligibility above, so a launch holding one is not a production launch.

    # ---- v0.4 no frame-rate promise --------------------------------------
    #
    # The owner decision is the pixel fix for production with NO 1080p60 claim,
    # and the measurement is an exact image beside a MISSED absolute target. So
    # neither the product's emitted lines nor the documents that describe the
    # row may promise a rate.
    # CLOSURE OVER THE RATE FIGURES, not a judgement about their surroundings.
    # An earlier draft exempted a figure whose SURROUNDINGS disclaimed a
    # promise, and a mutant writing "the renderer holds 1080p60" in its own
    # paragraph beside the disclaimer passed - the disclaimer laundered it. So
    # every figure must be one of the three declared ones, which are the native
    # build's own game-speed measurements and predate this row.
    promises = []
    for _lineno, value in literals(bgtf) + literals(dl):
        low = value.lower()
        for p in RATE_PROMISES:
            if p in low:
                promises.append('emitted line: %s' % value[:90])
                break
    for path in DOCS:
        text = (root / path).read_text(encoding='utf-8', errors='strict')
        low = text.lower()
        for p in RATE_PROMISES:
            start = 0
            while True:
                idx = low.find(p, start)
                if idx < 0:
                    break
                start = idx + 1
                window = text[max(0, idx - 200):idx + 200]
                if any(a in window for a in RATE_ALLOWED):
                    continue
                promises.append('%s: ...%s...'
                                % (path.as_posix(),
                                   text[max(0, idx - 70):idx + 70]))
    r.check(not promises,
            'no emitted line and no release-facing document promises a frame '
            'rate for the row - every rate figure in them is one of the %d '
            'declared pre-existing ones, and the claim is an exact image'
            % len(RATE_ALLOWED),
            '\n'.join(promises))
    # AND EVERY DECLARED EXEMPTION IS STILL THERE, so the declaration describes
    # these documents rather than an older pair of them.
    stale = []
    for a in RATE_ALLOWED:
        if not any(a in (root / p).read_text(encoding='utf-8', errors='strict')
                   for p in DOCS):
            stale.append(a)
    r.check(not stale,
            'and every declared rate exemption is still present, so the '
            'declaration is not describing an older document',
            '\n'.join(stale))

    # ---- the key set ------------------------------------------------------
    found = set(re.findall(r'getenv\("([A-Z_0-9]+)"\)', bgtf))
    found |= set(re.findall(r'tf_switch_1\("([A-Z_0-9]+)"', bgtf))
    r.check(found == KEYS,
            'the module reads exactly the declared environment keys (%d)'
            % len(found),
            'added: %s\nmissing: %s' % (sorted(found - KEYS),
                                        sorted(KEYS - found)))

    # ---- the synchronisation decision ------------------------------------
    r.check('implicit ? SL_BGTF_FX_FENCE : SL_BGTF_FX_FINISH' in bgtf,
            'the default synchronisation mode is chosen from the implicit '
            'record: FENCE for the candidate, FINISH for a named arm')
    decide = [m.start() for m in re.finditer(r'tf_fx_decide\(', bgtf)]
    r.check(len(decide) == 3,
            'the synchronisation key has ONE definition and ONE call site PER '
            'ROUTE, so two readers cannot disagree about it (%d references)'
            % len(decide))
    r.check('if (ar != SL_BGTF_AR_OFF && tf_fx_decide(' in bgtf,
            "the developer route's call site is behind an arm being in force, "
            'so an off launch reads the key not at all')
    r.check('if (tf_fx_decide(1, &fx_named) < 0) {' in bgtf,
            "and the production route's is behind its eligibility, so a launch "
            'the row never admitted reads it not at all either')
    r.check('getenv("SL_BG_TF_AUX_SYNC")' in bgtf
            and bgtf.count('getenv("SL_BG_TF_AUX_SYNC")') == 1,
            'the key is read exactly once in the module (%d)'
            % bgtf.count('getenv("SL_BG_TF_AUX_SYNC")'))
    back = [g for g in GONE if g in bgtf]
    r.check(not back,
            'no wording that served a launch which selected NEITHER mode with '
            'one of them, and no echo of that selector\'s value, is present',
            '\n'.join(back))
    invalid = [v for _, v in literals(bgtf)
               if 'SL_BG_TF_AUX_SYNC was given a value' in v]
    r.check(len(invalid) == 1,
            'the invalid-synchronisation refusal is stated exactly once (%d)'
            % len(invalid))
    if len(invalid) == 1:
        v = invalid[0]
        r.check('FAILS CLOSED' in v and 'CPU raster' in v
                and 'MAIN-CONTEXT prepass' in v and '%' not in v,
                'and it is keyed by the selector, says where the draws go, says '
                'what it does NOT fall to, and carries no conversion at all')

    # ---- the fail-closed order -------------------------------------------
    site = bgtf.find('if (g_ar_closed) {')
    armblk = bgtf.find('if (ar_mode != SL_BGTF_AR_OFF) {')
    r.check(site > 0 and armblk > 0 and site < armblk,
            'the served draw refuses for an unreadable arm BEFORE the arm '
            'block, so no prepass of either kind is reached (%d/%d)'
            % (site, armblk))
    if site > 0:
        body = bgtf[site:site + 200]
        r.check('return -1;' in body and 'g_c.refusals++;' in body,
                'and that refusal returns to the CPU raster path and is counted')

    # ---- the selection line names the mode IN FORCE ----------------------
    #
    # THE LINE MUST NOT SPELL A MODE. An opted-in launch may name FINISH, so a
    # sentence with "FENCE" written into it is false for that launch; the token
    # has to come from the decision. This reads the fprintf CALL rather than the
    # literal, so what is checked is where the token comes from.
    say = bgtf.find('is in force: no arm was named')
    r.check(say > 0, 'the candidate announces itself')
    if say > 0:
        call = enclosing_call(bgtf, say)
        r.check('sl_bgtf_fx_name(g_fx)' in call,
                'and it names the synchronisation mode from the DECISION rather '
                'than from a word in the sentence')
        # ON THE CONCATENATED LITERAL AND NOT ON THE SOURCE TEXT. The sentence is
        # split across adjacent literals, so a mode spelled into it can sit
        # across a quote boundary where a search of the source would not see it -
        # measured: a mutant that wrote FENCE into the sentence passed this check
        # until it read the joined string instead.
        said = [v for _, v in literals(bgtf)
                if 'is in force: no arm was named' in v]
        r.check(len(said) == 1
                and 'with FENCE synchronisation' not in said[0]
                and 'with FINISH synchronisation' not in said[0],
                'with no mode spelled into the sentence, so an explicit FINISH '
                'launch reads a true line')
        r.check('fx_named ?' in call,
                'and it says whether the key named that mode or the arm\'s own '
                'default did')
        r.check('getenv(' not in call,
                'and no environment value can reach it')

    # ---- v0.4 and the production line names the mode IN FORCE too --------
    psay = bgtf.find('PRODUCTION selection of the OPTIMIZED RENDERING row')
    r.check(psay > 0, 'the production selection announces itself')
    if psay > 0:
        call = enclosing_call(bgtf, psay)
        r.check(call.count('sl_bgtf_fx_name(g_fx)') == 2,
                'and it names the synchronisation mode from the DECISION, in '
                'both places it spells it, rather than from a word in the '
                'sentence')
        psaid = [v for _, v in literals(bgtf)
                 if 'is the PRODUCTION selection of the OPTIMIZED' in v]
        r.check(len(psaid) == 1
                and 'with FENCE synchronisation' not in psaid[0]
                and 'with FINISH synchronisation' not in psaid[0],
                'with no mode spelled into the sentence')
        r.check(len(psaid) == 1
                and 'NO FRAME RATE AND NO FRAME TIME IS PROMISED' in psaid[0],
                'and it says what the selection does NOT claim, where the '
                'frames are produced rather than only in a document')
        r.check(len(psaid) == 1
                and 'MAIN-CONTEXT prepass' in psaid[0]
                and 'reachable only by name' in psaid[0],
                'and it says the main-context arm is not selected and how a '
                'developer still reaches it')
        r.check('getenv(' not in call,
                'and no environment value can reach it')

    # ---- no environment value in the candidate's own lines ---------------
    leaky = []
    for lineno, value in literals(bgtf) + literals(dl):
        if 'OPT-IN CANDIDATE' not in value and 'opt-in candidate' not in value \
                and 'PRODUCTION selection' not in value \
                and 'production selection' not in value:
            continue
        if '="%s"' in value or '="%' in value:
            leaky.append('%d %s' % (lineno, value[:90]))
    r.check(not leaky,
            "neither selection's own lines quote any selector's value",
            '\n'.join(leaky))

    # ---- v0.4 the live checker's grammar reads the row's tail clause -----
    #
    # LOADED RATHER THAN RESTATED, exactly as the census grammar above is. The
    # emitter now prints THREE tail clauses where it printed two, and the two
    # failure modes are both ones this pair has had: a clause the grammar does
    # not accept reads a healthy line as a DRIFTED census, and a clause matched
    # by presence alone reads a WORKING production run as off.
    prod_tail = [v for _, v in literals(dl)
                 if v.startswith(' (the PRODUCTION selection:')]
    r.check(len(prod_tail) == 1,
            "the census emits the row's tail clause from exactly one place "
            '(%d)' % len(prod_tail))
    if len(prod_tail) == 1:
        rendered = ('#95 background GPU clip prepass: ready, 4 prepasses over '
                    '8 vertices, 0 reuses, 4 binds; SUBMITTED 4 nonempty runs '
                    "on a prepass pool (12 indices) by the SOURCE's depth "
                    'single 4 / far0 0 / far1 0, 2 walks; ADMITTED 2, refused '
                    '0 walks and 0 draws, 0 slot refills and 0 evictions, 0 '
                    'module refusals' + prod_tail[0])
        m = chk.RE_TFCLIP.search(rendered)
        # THE TAIL MUST BE CONSUMED, not merely tolerated. The grammar is
        # applied with search, so a line whose tail clause it does not know
        # still matches - with the tail left outside the match and the clause
        # silently lost. Requiring group 18 is what makes this a check on the
        # clause rather than on the prefix.
        r.check(m is not None and m.group(18) is not None,
                "and the live checker's census grammar accepts a line carrying "
                'it AND consumes the clause, so an ordinary run does not read '
                'as a drifted census and the clause is not dropped')
        if m is not None:
            tf = chk.TfClip(m)
            r.check(tf.production and not tf.off_phrase and tf.prepasses == 4,
                    'and reads it as the PRODUCTION selection rather than as '
                    'an off arm, beside the positive counters it prints with')
    off_tail = [v for _, v in literals(dl)
                if v.startswith(' (SL_BG_TF_CLIP is not 1:')]
    if len(off_tail) == 1:
        rendered = ('#95 background GPU clip prepass: not asked, 0 prepasses '
                    'over 0 vertices, 0 reuses, 0 binds; SUBMITTED 0 nonempty '
                    "runs on a prepass pool (0 indices) by the SOURCE's depth "
                    'single 0 / far0 0 / far1 0, 0 walks; ADMITTED 0, refused '
                    '0 walks and 0 draws, 0 slot refills and 0 evictions, 0 '
                    'module refusals' + off_tail[0])
        m = chk.RE_TFCLIP.search(rendered)
        r.check(m is not None and chk.TfClip(m).off_phrase
                and not chk.TfClip(m).production,
                'and the OFF clause still reads as off and only as off, so an '
                'off witness cannot be satisfied by a production run')

    # AND THE CHECKER CLASSIFIES A ROW-ON ARM AS A PREPASS ARM. Two witness
    # sets hang off that answer, and reading a production arm as a non-prepass
    # arm would invert one and fail the other on a working run.
    r.check(chk.is_candidate_arm({'env': {}}),
            'an arm that names nothing runs the prepass, because the row is on '
            'by default in a player session')
    r.check(chk.is_candidate_arm({'env': {'SL_BG_TF_CLIP': '1'}}),
            'and so does the developer switch, as before')
    r.check(not chk.is_candidate_arm({'env': {'SL_BG_TF_CLIP': '0'}}),
            'an explicit developer OFF does not')
    r.check(not chk.is_candidate_arm({'env': {'SL_PERF_OPT': '0'}}),
            'the row turned off does not')
    r.check(not chk.is_candidate_arm({'env': {'SL_BG_GPU_XFORM': '0'}}),
            "the background subsystem's own switch turned off does not")
    r.check(not chk.is_candidate_arm({'env': {'SL_BG_TF_AUX_REAL': 'OFF'}}),
            'and an arm naming an OFF token does not, because that names the '
            'main-context diagnostic')
    diag = [k for k in chk.TF_MAIN_CONTEXT_SELECTORS
            if chk.is_candidate_arm({'env': {k: '1'}})]
    r.check(not diag,
            'nor does any of the %d main-context diagnostics and captures'
            % len(chk.TF_MAIN_CONTEXT_SELECTORS),
            ', '.join(diag))

    print('\n%d passed, %d failed' % (r.passed, r.failed))
    return 1 if r.failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
