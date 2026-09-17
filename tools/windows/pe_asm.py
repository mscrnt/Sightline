#!/usr/bin/env python3
"""Rewrite an ELF/i386 assembly file for the PE32/i386 assembler.

Two mechanical differences, both measured on this toolchain rather than
assumed:

  1. PE32/i386 decorates C symbols with a leading underscore
     (`i686-w64-mingw32-gcc -dM -E` reports `__USER_LABEL_PREFIX__` as `_`;
     `nm` on a trivial object shows `_g_ModelHitEntries`). The two .s inputs
     this build assembles - tools/native/modelhit_pool.s and the generated
     segments.s - are written with undecorated ELF names, so every symbol
     they define would miss the C references that need them.

  2. `.section .note.GNU-stack,"",@progbits` is ELF-only and the PE assembler
     rejects it outright ("junk at end of line").

Rewriting here keeps both generators emitting one ELF-shaped output, so the
Linux build is untouched and there is no second copy of the layout to drift.
"""
import re
import sys

IDENT = r'[A-Za-z_][A-Za-z0-9_]*'
RE_GLOBL = re.compile(r'^(\s*\.globl\s+)(' + IDENT + r')\s*$')
RE_LABEL = re.compile(r'^(' + IDENT + r'):\s*$')
# `.set name, other + 0x10` and `.set name, 0x8004a5c` both occur.
RE_SET = re.compile(r'^(\s*\.set\s+)(' + IDENT + r')(\s*,\s*)(.*)$')
RE_RHS = re.compile(r'^(' + IDENT + r')\b')


def convert(text):
    out = []
    for line in text.splitlines():
        if '.note.GNU-stack' in line:
            continue
        m = RE_GLOBL.match(line)
        if m:
            out.append('%s_%s' % (m.group(1), m.group(2)))
            continue
        m = RE_LABEL.match(line)
        if m:
            out.append('_%s:' % m.group(1))
            continue
        m = RE_SET.match(line)
        if m:
            rhs = m.group(4)
            # Only an identifier on the right-hand side is a symbol; a bare
            # 0x... absolute must be left exactly as it is.
            rhs = RE_RHS.sub(lambda r: '_' + r.group(1), rhs)
            out.append('%s_%s%s%s' % (m.group(1), m.group(2), m.group(3), rhs))
            continue
        out.append(line)
    return '\n'.join(out) + '\n'


def main():
    if len(sys.argv) != 3:
        sys.stderr.write('usage: pe_asm.py <in.s> <out.s>\n')
        return 2
    with open(sys.argv[1], 'r') as f:
        text = f.read()
    with open(sys.argv[2], 'w') as f:
        f.write(convert(text))
    return 0


if __name__ == '__main__':
    sys.exit(main())
