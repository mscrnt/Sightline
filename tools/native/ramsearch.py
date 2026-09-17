#!/usr/bin/env python3
"""GameShark-style RAM search over a deterministic ROM replay.

The childhood technique, made repeatable: snapshot RDRAM at chosen frames of a
recorded playthrough, then ask which addresses changed, held still, rose or
fell between them. Because the replay is deterministic the same frames give the
same values every run, so a candidate set can be narrowed across as many passes
as you like without replaying by hand.

This is for the gaps the notes do not cover. Fifteen `not_covered` entries in
docs/doc-routing.json are questions of exactly this shape - what holds this
state, and when does it change?

    # what changes between the tank room and the gas filling it?
    tools/native/ramsearch.py --input facility-gas --at 7000,9000,11000 \
                              --filter changed

    # what holds STILL while everything else moves (a level-constant)?
    tools/native/ramsearch.py --input facility-gas --at 2000,7000,11000 \
                              --filter unchanged --region 80044000:80046000

Addresses resolve against the direct-boot build's own symbol map, so a hit
usually arrives already named.
"""
import argparse, pathlib, struct, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))
RDRAM = 0x00800000          # 8 MiB with the expansion pak


def load_symbols(name: str) -> dict:
    m = ROOT / "build/u/direct" / f"ge007.u.{name}.map"
    out = {}
    if not m.is_file():
        return out
    for ln in m.read_text(errors="ignore").splitlines():
        p = ln.split()
        if len(p) >= 2 and p[0].startswith("0x8"):
            try:
                out.setdefault(int(p[0], 16), p[1])
            except ValueError:
                pass
    return out


def nearest(sym: dict, addr: int) -> str:
    best, bestd = "", 1 << 30
    for a, n in sym.items():
        d = addr - a
        if 0 <= d < bestd and d < 0x400:
            best, bestd = n, d
    return f"{best}+0x{bestd:x}" if best else ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--at", required=True,
                    help="comma-separated frames to snapshot, e.g. 7000,9000,11000")
    ap.add_argument("--filter", default="changed",
                    choices=["changed", "unchanged", "increased", "decreased"])
    ap.add_argument("--width", type=int, default=4, choices=[1, 2, 4])
    ap.add_argument("--region", default=None,
                    help="LO:HI in KSEG0 hex, e.g. 80044000:80046000")
    ap.add_argument("--float", action="store_true",
                    help="show words as floats too")
    ap.add_argument("--max", type=int, default=40)
    ap.add_argument("--no-swap", action="store_true",
                    help="this core presents RDRAM with 32-bit words BYTE "
                         "SWAPPED; values are un-swapped before they are "
                         "interpreted. Pass this to see the raw view.")
    args = ap.parse_args()

    frames = sorted(int(x) for x in args.at.split(","))
    lo, hi = 0, RDRAM
    if args.region:
        a, b = args.region.split(":")
        lo, hi = int(a, 16) & 0x1FFFFFFF, int(b, 16) & 0x1FFFFFFF

    from sltrace.emu_libretro import LibretroEmulator
    inp = ROOT / "tools/trace/inputs" / (args.input + ".input")
    eep = ROOT / "tools/trace/inputs" / (args.input + ".eeprom")
    direct = ROOT / "build/u/direct" / f"ge007.u.{args.input}.z64"
    rom = str(direct) if direct.is_file() else str(ROOT / "baserom.u.z64")

    snaps: dict[int, bytes] = {}

    def on_tick(i, fc, ram):
        if i in frames and ram and i not in snaps:
            snaps[i] = bytes(bytearray(ram[lo:hi]))

    emu = LibretroEmulator(rom, input_stream=str(inp),
                           eeprom=str(eep) if eep.is_file() else None)
    emu.run(on_tick, lambda r: 0, max_ticks=max(frames) + 1)
    emu.close()

    missing = [f for f in frames if f not in snaps]
    if missing:
        print(f"ramsearch: never reached frame(s) {missing}", file=sys.stderr)
        return 2

    w = args.width
    fmt = {1: ">B", 2: ">H", 4: ">I"}[w]
    series = [snaps[f] for f in frames]
    sym = load_symbols(args.input)

    print(f"  rom {rom}")
    print(f"  frames {frames}  filter={args.filter}  width={w}"
          f"  region {0x80000000+lo:08x}..{0x80000000+hi:08x}")
    hits = 0
    for off in range(0, len(series[0]) - w + 1, w):
        vals = [struct.unpack_from(fmt, s, off)[0] for s in series]
        if args.filter == "changed":
            ok = len(set(vals)) > 1
        elif args.filter == "unchanged":
            ok = len(set(vals)) == 1 and vals[0] != 0
        elif args.filter == "increased":
            ok = all(b > a for a, b in zip(vals, vals[1:]))
        else:
            ok = all(b < a for a, b in zip(vals, vals[1:]))
        if not ok:
            continue
        addr = 0x80000000 + lo + off
        name = sym.get(addr) or nearest(sym, addr)
        cells = " -> ".join(f"{v:0{w*2}x}" for v in vals)
        extra = ""
        if args.float and w == 4:
            fs = [struct.unpack("<f" if not args.no_swap else ">f",
                                s[off:off + 4])[0] for s in series]
            if all(-1e9 < x < 1e9 for x in fs):
                extra = "   [" + " -> ".join(f"{x:.2f}" for x in fs) + "]"
        print(f"  {addr:08x}  {cells}{extra}  {name}")
        hits += 1
        if hits >= args.max:
            print(f"  ... (stopped at {args.max}; narrow with --region or more"
                  f" --at frames)")
            break
    if not hits:
        print("  no addresses matched")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
