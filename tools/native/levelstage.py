#!/usr/bin/env python3
"""level NAME -> boot stage NUMBER, from the trace harness\'s own table.

Exists because the two were silently different things. `SL_LEVEL=facility` is
a name that the skeleton does not read; `SL_BOOT_LEVEL=34` is the number it
does. Scripts that had the name and passed it through booted the title screen
and reported success. Resolving through levelboot.py keeps the demo, the
health gate and the determinism corpus on one table rather than three.

Prints nothing and exits 1 when the name is unknown, so a caller that forgets
to check gets an empty value rather than a plausible default.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def stage_for(name: str):
    src = (ROOT / "tools/trace/sltrace/levelboot.py").read_text()
    m = re.search(r'"%s"\s*:\s*(\d+)' % re.escape(name), src)
    return int(m.group(1)) if m else None


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: levelstage.py <level-name>", file=sys.stderr)
        raise SystemExit(2)
    n = stage_for(sys.argv[1])
    if n is None:
        print("levelstage: unknown level %r" % sys.argv[1], file=sys.stderr)
        raise SystemExit(1)
    print(n)
