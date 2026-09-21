#!/usr/bin/env python3
"""Extract, from the matching build's linker map, exactly what the native
build consumes - and nothing else - into a small tracked text file.

WHY. tools/windows/build.ps1 places the ROM-flavoured symbols and the segment
windows through tools/native/gen_segments.py, which used to read
build/u/ge007.u.map directly. That map is a product of `make matching`
(MIPS / IDO toolchain), which the Windows workflow cannot run, so every
Windows build silently depended on a file only a Linux host could produce
and nobody could say where a given build's copy had come from. This script
turns the consumed subset into tools/native/ge007.u.linkmap.txt, committed
with its provenance, so the Windows build is self-contained and a release
can state how its map input was produced.

WHAT IS IN IT, and why that is not ROM content: symbol NAMES (the decomp's
own identifiers) and LINK ADDRESSES. The segment symbols are the map's
_*Segment(Rom)?(Start|End) values; the ROM-flavoured symbols (address below
0x04000000) are the linker's placement of the asset files at their ROM
offsets - the same offsets scripts/filelist.u.csv already carries in decimal.
No byte of the ROM, no game data, nothing derived from either.

WHAT IS NOT IN IT: RAM symbols (0x8xxxxxxx) - gen_segments.py never uses
them (they are state, and are stubbed), so they are not extracted.

    python tools/native/extract_linkmap.py build/u/ge007.u.map \\
        tools/native/ge007.u.linkmap.txt --commit <sha> [--rom-sha1 <hex>]

    python tools/native/extract_linkmap.py build/u/ge007.u.map \\
        tools/native/ge007.u.linkmap.txt --check

--check re-extracts from the map and compares the symbol lines (not the
provenance header) with the tracked file: exit 0 when identical, 1 with the
first differences otherwise. That is the release-preparation step: run
`make matching` at the commit being released, then --check.

The extraction keeps gen_segments.py's own first-occurrence rule (setdefault)
so the two agree symbol for symbol.
"""
import argparse
import hashlib
import re
import sys
from pathlib import Path

SEG_RX = re.compile(r"0x([0-9a-f]+)\s+(_\w+Segment(?:Rom)?(?:Start|End)) =")
ADDR_RX = re.compile(r"^\s+0x([0-9a-f]{8,16})\s+([A-Za-z_][A-Za-z0-9_]+)\s*$", re.M)
ROM_FLAVOURED_LIMIT = 0x04000000
HEADER = "# sightline linkmap"


def extract(map_text: str):
    seg = {}
    for m in SEG_RX.finditer(map_text):
        seg.setdefault(m.group(2), int(m.group(1), 16))
    sym = {}
    for m in ADDR_RX.finditer(map_text):
        v = int(m.group(1), 16)
        if v < ROM_FLAVOURED_LIMIT:
            sym.setdefault(m.group(2), v)
    return seg, sym


def render_lines(seg, sym):
    lines = []
    for name, val in sorted(seg.items()):
        lines.append(f"seg {name} {val:#x}")
    for name, val in sorted(sym.items()):
        lines.append(f"sym {name} {val:#x}")
    return lines


def parse_tracked(text: str):
    """The format gen_segments.py reads: header comments, then seg/sym lines."""
    seg, sym = {}, {}
    if not text.startswith(HEADER):
        raise ValueError("not a sightline linkmap file")
    for ln in text.splitlines():
        if not ln or ln.startswith("#"):
            continue
        kind, name, val = ln.split()
        (seg if kind == "seg" else sym)[name] = int(val, 16)
    return seg, sym


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("map")
    ap.add_argument("out")
    ap.add_argument("--commit", default="", help="the commit `make matching` ran at")
    ap.add_argument("--rom-sha1", default="", help="SHA-1 of the ROM the matching build reproduced (MATCH!)")
    ap.add_argument("--check", action="store_true", help="compare the map's extraction with the tracked file")
    a = ap.parse_args()

    map_bytes = Path(a.map).read_bytes()
    seg, sym = extract(map_bytes.decode(errors="replace"))
    lines = render_lines(seg, sym)

    if a.check:
        tracked = Path(a.out).read_text(encoding="utf-8")
        tseg, tsym = parse_tracked(tracked)
        tracked_lines = render_lines(tseg, tsym)
        if tracked_lines == lines:
            print(f"extract_linkmap: {a.out} agrees with {a.map} ({len(seg)} segment symbols, {len(sym)} ROM-flavoured symbols)")
            return 0
        a_set, b_set = set(tracked_lines), set(lines)
        for ln in sorted(a_set - b_set)[:10]:
            print(f"  only in tracked: {ln}")
        for ln in sorted(b_set - a_set)[:10]:
            print(f"  only in map:     {ln}")
        print(f"extract_linkmap: MISMATCH ({len(a_set - b_set)} tracked-only, {len(b_set - a_set)} map-only)")
        return 1

    head = [
        HEADER,
        "# The subset of the matching build's linker map (build/u/ge007.u.map, a",
        "# product of `make matching`) that tools/native/gen_segments.py consumes:",
        "# segment windows and ROM-flavoured symbol addresses. Symbol names and link",
        "# addresses only - no ROM bytes, no game data. Regenerate with",
        "# tools/native/extract_linkmap.py; verify with --check. Do not hand-edit.",
        f"# source-map-sha256 {hashlib.sha256(map_bytes).hexdigest()}",
        f"# source-map-bytes {len(map_bytes)}",
        f"# make-matching-commit {a.commit or 'unrecorded'}",
        f"# matched-rom-sha1 {a.rom_sha1 or 'unrecorded'}",
        f"# segment-symbols {len(seg)}",
        f"# rom-flavoured-symbols {len(sym)}",
    ]
    Path(a.out).write_text("\n".join(head + lines) + "\n", encoding="utf-8", newline="\n")
    print(f"extract_linkmap: wrote {a.out} ({len(seg)} segment symbols, {len(sym)} ROM-flavoured symbols)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
