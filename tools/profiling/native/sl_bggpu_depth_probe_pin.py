#!/usr/bin/env python3
"""Extract the background pass's PRODUCTION vertex program text and uniform
names out of src/gfx/sl_gfx_bggpu.c, check them against a pin, and emit the
generated C header the depth probe compiles against.

WHY EXTRACTION AND NOT A COPY. BGG_VS is a file-static string in
src/gfx/sl_gfx_bggpu.c with no external linkage, and src/ is not edited to
expose it. A retyped or "equivalent" shader would make the probe a test of the
copy, not of what ships, so the text the probe compiles on a driver is read out
of that source at build time and hashed. The hash is the pin: the SHA-256 of
the decoded GLSL and of the uniform-name list are compared with
sl_bggpu_depth_probe.pin and a mismatch REFUSES, so a shader edit cannot reach
a driver run under a stale measurement's name. The whole source file's SHA-256
is recorded for provenance but not enforced - an unrelated edit to that file
does not invalidate a depth measurement, a shader-text edit does.

The two halves of the source are also cross-checked against each other: the
`uniform` declarations inside the extracted GLSL, in declaration order, must be
exactly the names the module passes to glGetUniformLocation, in call order. A
uniform added to one half and not the other refuses here rather than resolving
to -1 on a driver.

NO ROM DATA AND NO ASSET is read: the only input is one C source file in this
repository.

Usage (both paths absolute or repo-relative):
    sl_bggpu_depth_probe_pin.py --source <sl_gfx_bggpu.c> --pin <pin file>
                               --out <generated header>
    ... --write-pin            rewrite the pin from what the source now holds
                               (an intentional shader change; review the diff)
    ... --print                print the hashes and exit 0 without writing

Exit 0 only when the extraction succeeded and matched the pin.
"""

import argparse
import hashlib
import re
import sys

# The declaration the text hangs off. Matched literally; a rename refuses.
DECL = 'static const char *BGG_VS ='

# The module's own uniform resolution, in call order.
GETLOC = re.compile(r'p_GetUniformLocation\s*\(\s*g_prog\s*,\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\)')

# A GLSL uniform declaration inside the extracted text. Array suffixes are
# stripped: GL reports `u_C[16]` under the name `u_C`, which is the name the
# module asks for.
GLSL_UNIFORM = re.compile(r'^\s*uniform\s+\w+\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[\s*\d+\s*\])?\s*;', re.M)

STRING_PIECE = re.compile(r'"((?:[^"\\]|\\.)*)"')

ESCAPES = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\', "'": "'", '0': '\0'}


class PinError(Exception):
    """Refuse: the extraction failed or the source no longer matches the pin."""


def decode_c_string(piece):
    """One C string literal's bytes, with the escapes the source actually uses.

    Only the escapes BGG_VS contains are accepted; anything else refuses rather
    than being passed through, because a silently mis-decoded byte would change
    the text a driver compiles while the hash still looked plausible.
    """
    out = []
    i = 0
    while i < len(piece):
        ch = piece[i]
        if ch != '\\':
            out.append(ch)
            i += 1
            continue
        if i + 1 >= len(piece):
            raise PinError('a string literal ends in a backslash')
        esc = piece[i + 1]
        if esc not in ESCAPES:
            raise PinError('unsupported escape \\%s in the shader text' % esc)
        out.append(ESCAPES[esc])
        i += 2
    return ''.join(out)


def scan_literals(source_text, start):
    """The concatenated string literals of one declaration, and where it ends.

    The scan is a character walk and not a `find(';')`, because the GLSL itself
    contains semicolons: every `uniform ...;` line would end the declaration
    early and truncate the text mid-literal. Only a semicolon OUTSIDE a string
    literal terminates it. Nothing but whitespace may sit between the literals -
    a macro or a conditional there would mean the text is assembled rather than
    literal, and refuses.
    """
    pieces = []
    i = start
    n = len(source_text)
    while i < n:
        ch = source_text[i]
        if ch == ';':
            return pieces, i
        if ch.isspace():
            i += 1
            continue
        if ch != '"':
            raise PinError('non-literal text inside the shader declaration: %r'
                           % source_text[i:i + 60])
        match = STRING_PIECE.match(source_text, i)
        if match is None:
            raise PinError('an unterminated string literal in the shader declaration')
        pieces.append(match.group(1))
        i = match.end()
    raise PinError('the shader declaration has no terminating semicolon')


def extract(source_text):
    """The GLSL text and the module's uniform names, from one source file."""
    at = source_text.find(DECL)
    if at < 0:
        raise PinError('the declaration %r is not in the source' % DECL)
    if source_text.find(DECL, at + 1) >= 0:
        raise PinError('the declaration %r appears more than once' % DECL)
    pieces, _ = scan_literals(source_text, at + len(DECL))
    if not pieces:
        raise PinError('no string literal between the declaration and its semicolon')
    glsl = ''.join(decode_c_string(p) for p in pieces)
    if not glsl.startswith('#version '):
        raise PinError('the extracted text does not begin with a #version directive')

    resolved = GETLOC.findall(source_text)
    if not resolved:
        raise PinError('the module resolves no uniform through p_GetUniformLocation(g_prog, ...)')
    declared = GLSL_UNIFORM.findall(glsl)
    if declared != resolved:
        raise PinError('the GLSL declares %r but the module resolves %r' % (declared, resolved))
    return glsl, resolved


def sha(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def hashes(source_bytes, glsl, names):
    return {
        'source_sha256': hashlib.sha256(source_bytes).hexdigest(),
        'glsl_sha256': sha(glsl),
        'uniforms_sha256': sha('\n'.join(names) + '\n'),
    }


def read_pin(path):
    pinned = {}
    with open(path, 'r', encoding='utf-8') as stream:
        for line in stream:
            line = line.split('#', 1)[0].strip()
            if not line:
                continue
            if '=' not in line:
                raise PinError('the pin file has a line that is not key=value: %r' % line)
            key, value = line.split('=', 1)
            pinned[key.strip()] = value.strip()
    return pinned


def c_literal(text):
    """The GLSL as one C string literal per source line, newlines kept."""
    out = []
    for line in text.split('\n')[:-1] if text.endswith('\n') else text.split('\n'):
        body = line.replace('\\', '\\\\').replace('"', '\\"')
        out.append('    "%s\\n"' % body)
    return '\n'.join(out)


def write_header(path, glsl, names, hs):
    lines = [
        '/* GENERATED by tools/profiling/native/sl_bggpu_depth_probe_pin.py.',
        ' * DO NOT EDIT and DO NOT COMMIT: this is a build artifact under build/.',
        ' *',
        ' * The background pass\'s production vertex program, read out of',
        ' * src/gfx/sl_gfx_bggpu.c and hashed. The probe compiles THIS text on a',
        ' * driver, so a shader edit without a pin update refuses at the',
        ' * extraction step rather than measuring the wrong program. */',
        '#ifndef SL_BGGPU_DEPTH_PROBE_GLSL_H',
        '#define SL_BGGPU_DEPTH_PROBE_GLSL_H',
        '',
        '#define SL_BGDP_GLSL_SHA256    "%s"' % hs['glsl_sha256'],
        '#define SL_BGDP_UNIFORMS_SHA256 "%s"' % hs['uniforms_sha256'],
        '#define SL_BGDP_SOURCE_SHA256  "%s"' % hs['source_sha256'],
        '#define SL_BGDP_GLSL_BYTES     %d' % len(glsl.encode('utf-8')),
        '#define SL_BGDP_UNIFORM_COUNT  %d' % len(names),
        '',
        'static const char SL_BGDP_GLSL[] =',
        c_literal(glsl),
        ';',
        '',
        'static const char *const SL_BGDP_UNIFORMS[SL_BGDP_UNIFORM_COUNT] = {',
    ]
    lines += ['    "%s",' % n for n in names]
    lines += ['};', '', '#endif /* SL_BGGPU_DEPTH_PROBE_GLSL_H */', '']
    # LF only: the byte count above is the text's own, and write_text on
    # Windows would turn every newline into CRLF.
    with open(path, 'wb') as stream:
        stream.write('\n'.join(lines).encode('utf-8'))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--source', required=True)
    parser.add_argument('--pin', required=True)
    parser.add_argument('--out')
    parser.add_argument('--write-pin', action='store_true')
    parser.add_argument('--print', dest='only_print', action='store_true')
    args = parser.parse_args(argv)

    try:
        with open(args.source, 'rb') as stream:
            source_bytes = stream.read()
        glsl, names = extract(source_bytes.decode('utf-8'))
        hs = hashes(source_bytes, glsl, names)

        print('shader text  : %d bytes, sha256 %s' % (len(glsl.encode('utf-8')), hs['glsl_sha256']))
        print('uniforms     : %s' % ' '.join(names))
        print('uniform list : sha256 %s' % hs['uniforms_sha256'])
        print('source file  : sha256 %s (recorded, not enforced)' % hs['source_sha256'])

        if args.write_pin:
            body = (
                '# The pin for the background pass\'s production vertex program.\n'
                '# Written by sl_bggpu_depth_probe_pin.py --write-pin. The probe\n'
                '# REFUSES to build when the extracted GLSL or the uniform list no\n'
                '# longer hashes to these values, so a shader edit cannot reach a\n'
                '# driver run under an earlier measurement\'s name. source_sha256 is\n'
                '# provenance only: an unrelated edit to that file does not\n'
                '# invalidate a depth measurement, a shader-text edit does.\n'
                'glsl_sha256=%s\n'
                'uniforms_sha256=%s\n'
                'source_sha256=%s\n' % (hs['glsl_sha256'], hs['uniforms_sha256'], hs['source_sha256'])
            )
            with open(args.pin, 'wb') as stream:
                stream.write(body.encode('utf-8'))
            print('pin written  : %s' % args.pin)
        else:
            pinned = read_pin(args.pin)
            for key in ('glsl_sha256', 'uniforms_sha256'):
                if key not in pinned:
                    raise PinError('the pin file has no %s' % key)
                if pinned[key] != hs[key]:
                    raise PinError(
                        'PIN MISMATCH on %s: the pin says %s, the source now gives %s. '
                        'The production shader text or its uniform list CHANGED. Review '
                        'the change, then re-run with --write-pin; a driver measurement '
                        'taken before the change does not describe the shader that now '
                        'ships.' % (key, pinned[key], hs[key]))
            print('pin          : MATCHED (%s)' % args.pin)
            if pinned.get('source_sha256') != hs['source_sha256']:
                print('note         : the source file changed but the shader text and '
                      'uniform list did not; the depth measurement still describes '
                      'this shader.')

        if args.only_print:
            return 0
        if args.out:
            write_header(args.out, glsl, names, hs)
            print('header       : %s' % args.out)
        return 0
    except (OSError, UnicodeDecodeError, PinError) as error:
        print('sl_bggpu_depth_probe_pin.py: %s' % error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
