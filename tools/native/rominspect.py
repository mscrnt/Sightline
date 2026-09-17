#!/usr/bin/env python3
"""Ask the CARTRIDGE what the crosshair is on, and log that object every frame.

This is the host half of SIGHTLINE_ROM_DEBUG. See src/game/sl_romdbg.h for the
ROM half, the rules it lives under, and why the trigger is a mailbox word
rather than a button.

    make trace-debug LEVEL=facility SL_NAME=facility-pane
    tools/native/rominspect.py --input facility-pane --select-at 7000 \
                               --frames 7418 --out /tmp/rominspect

What it does per run:

  * loads the DEBUG direct-boot ROM built for that recording, plus its map, and
    resolves g_SlRomDbg out of the map - no address is ever guessed;
  * replays the recording's own input stream (and its eeprom, which is not
    optional - a run without it answers a different saved game);
  * issues SELECT either where the recorded stream presses the reserved bit, or
    at the frames named by --select-at;
  * writes one report per SELECT and one CSV row per frame for the watched
    prop, plus the framebuffer at each SELECT.

TRAPS THIS FILE EXISTS TO NOT REPEAT
------------------------------------
* RDRAM in this libretro core is WORD-BYTE-SWAPPED. Every read here goes
  through sltrace.state.Memory and every write through Memory.w32(). There is
  no ad-hoc struct.unpack in this file, on purpose: a pointer read big-endian
  came back 0x60630b80 (invalid) where little-endian gave 0x800b6360 (valid),
  and the wrong answer looks like plausible garbage rather than an error.
  tools/native/romprobe.py still defines big-endian f32()/u32() helpers it
  never calls - do not copy them.

* Host offsetof is not MIPS offsetof. Nothing here derives a struct layout from
  the host compiler. The mailbox layout is fixed by src/game/sl_romdbg.h and
  mirrored ONCE, in tools/trace/sltrace/romdbg.py, which this file and the live
  recorder both import. The magic word is checked before a single other field
  is believed, and the ROM publishes its own sizeof so a drift stops the run.

* A stream recorded through trace-record answers its DIRECT-BOOT ROM, never
  baserom.u.z64.

* One libretro core per process. This script runs standalone.

* An instrument that cannot fail is worthless. hook_calls is the known-positive
  control: the ROM increments it every frame the hook runs, unconditionally. If
  it is still zero at the end of a run, the mailbox never executed and every
  other number in the report is meaningless - the run says so and exits non-zero.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))

# ---------------------------------------------------------------------------
# The mailbox layout, the decoder and the report live in ONE place, shared with
# the LIVE recorder (tools/trace/sltrace/record_libretro.py). Two copies of a
# hand-mirrored struct layout is two chances to drift from src/game/sl_romdbg.h,
# and a silently shifted field is exactly the confident-wrong answer this tool
# exists to remove.
# ---------------------------------------------------------------------------
from sltrace.romdbg import (MAGIC, VERSION, MB, HITS_OFF, HIT_STRIDE, MAX_HITS,
                            EXPECTED_SIZE, CMD_SELECT, CMD_SNAPSHOT, CMD_CLEAR,
                            CMD_FINDTYPE, CMD_NEAREST, EV_NAME, DBG_SELECT_BIT,
                            PROPDEF_TINTED_GLASS,
                            check_layout, read_field, read_mailbox, hexdump,
                            write_ppm, report, CSV_COLUMNS, csv_row)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True,
                    help="stream name under tools/trace/inputs (no extension)")
    ap.add_argument("--rom", default=None,
                    help="defaults to the DEBUG direct-boot ROM for --input")
    ap.add_argument("--map", default=None)
    ap.add_argument("--frames", type=int, default=0,
                    help="0 = the whole recorded stream")
    ap.add_argument("--select-at", default="",
                    help="comma-separated input indices to fire SELECT at, for "
                         "streams recorded before the reserved bit existed")
    ap.add_argument("--select-sweep", default="",
                    help="START:END:STEP - fire SELECT repeatedly and report "
                         "what each one found. The way to locate the frame "
                         "where the crosshair is on a pane.")
    ap.add_argument("--find-type", type=int, default=None,
                    help="issue FINDTYPE for this PROPDEF instead of SELECT "
                         "(47 = PROPDEF_TINTED_GLASS, 42 = PROPDEF_GLASS, "
                         "-1 = every on-screen prop). Answers 'is one on "
                         "screen at all, and does it pass the shot gate' - so "
                         "a SELECT that found nothing is a measurement rather "
                         "than a shrug.")
    ap.add_argument("--out", default="/tmp/rominspect")
    ap.add_argument("--no-shots", action="store_true")
    ap.add_argument("--trace", default="",
                    help="also write a .sltrace for this run. Run once with "
                         "SELECTs and once without, then diff: same ROM image, "
                         "so any difference is the instrument being USED. That "
                         "is the control the cross-ROM comparison cannot give "
                         "you, because boss.c:389 seeds the RNG from "
                         "osGetCount() and every ROM image gets its own seed.")
    args = ap.parse_args()

    from sltrace.emu_libretro import LibretroEmulator, InputFrame
    from sltrace.state import Memory, StateReader, SCHEMA_VERSION
    from sltrace.symbols import SymbolTable
    from sltrace.traceio import TraceWriter, TraceHeader

    name = args.input
    rom = args.rom or str(ROOT / "build/u/direct" / f"ge007.u.{name}.dbg.z64")
    mp = args.map or str(ROOT / "build/u/direct" / f"ge007.u.{name}.dbg.map")
    inp = ROOT / "tools/trace/inputs" / (name + ".input")
    eep = ROOT / "tools/trace/inputs" / (name + ".eeprom")

    if not pathlib.Path(rom).is_file():
        print(f"rominspect: no debug ROM at {rom}\n"
              f"  build it with: make trace-debug LEVEL=<level> SL_NAME={name}",
              file=sys.stderr)
        return 2
    if not inp.is_file():
        print(f"rominspect: no such stream: {inp}", file=sys.stderr)
        return 2
    if not eep.is_file():
        # Not a warning. Measured: a run without the eeprom answers a different
        # saved game and the gas timer read 0.000 for a whole run the owner had
        # blown the tanks in.
        print(f"rominspect: REFUSING to run without {eep} - the eeprom is not "
              f"optional, the same input answers a different saved game "
              f"without it.", file=sys.stderr)
        return 2

    syms = SymbolTable.from_map(mp)
    if "g_SlRomDbg" not in syms:
        print(f"rominspect: g_SlRomDbg is not in {mp}.\n"
              f"  That map is from a ROM built WITHOUT SIGHTLINE_ROM_DEBUG.",
              file=sys.stderr)
        return 2
    base = syms.addr("g_SlRomDbg")
    print(f"rominspect: rom = {rom}")
    print(f"rominspect: g_SlRomDbg = 0x{base:08x}")

    raw = inp.read_bytes()
    frames = [InputFrame.from_bytes(raw[i:i + 4]) for i in range(0, len(raw) - 3, 4)]
    total = args.frames or len(frames)
    print(f"rominspect: {len(frames)} recorded frames, running {total}")

    # SELECT points: the reserved recorded bit, plus anything asked for on the
    # command line. Edge-detected, exactly as a button press is.
    select_at = set()
    for tok in args.select_at.split(","):
        if tok.strip():
            select_at.add(int(tok.strip()))
    if args.select_sweep:
        a0, a1, st = (int(x) for x in args.select_sweep.split(":"))
        select_at.update(range(a0, a1, st))
    recorded = 0
    prev = False
    for i, f in enumerate(frames):
        now = f.pressed(DBG_SELECT_BIT)
        if now and not prev:
            select_at.add(i)
            recorded += 1
        prev = now
    print(f"rominspect: {recorded} SELECT presses in the recorded stream, "
          f"{len(select_at)} SELECT points total")

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    state = {"fb": None, "w": 0, "h": 0, "pitch": 0}

    def on_video(data, width, height, pitch):
        state["fb"], state["w"], state["h"], state["pitch"] = \
            data, width, height, pitch

    emu = LibretroEmulator(rom, input_stream=str(inp), eeprom=str(eep))
    emu._on_video_capture = on_video

    csv = (out / f"{name}-watch.csv").open("w")
    csv.write(",".join(CSV_COLUMNS) + "\n")

    writer = None
    if args.trace:
        import hashlib
        sys.path.insert(0, str(ROOT / 'tools/trace'))
        from trace import libretro_fingerprint
        reader = StateReader(syms)
        writer = TraceWriter(args.trace, TraceHeader(
            rom_sha1=hashlib.sha1(pathlib.Path(rom).read_bytes()).hexdigest(),
            schema_version=SCHEMA_VERSION, level=name,
            emu_fingerprint=libretro_fingerprint(), tick_count=0,
            eeprom_sha1=hashlib.sha1(eep.read_bytes()).hexdigest(),
            input_stream=str(inp), notes="rominspect"))

    seq = [0]
    captures = [0]
    rows = [0]
    last_hook = [0]
    checked = [False]
    last_seq = [-1]

    def on_tick(index, fc, ram):
        mem = Memory(ram)
        # Host -> ROM, BEFORE the frame that will act on it. The ROM reads the
        # command at the top of its hook, so the poke must already be in RDRAM.
        if index in select_at:
            seq[0] += 1
            if args.find_type is not None:
                mem.w32(base + MB["host_arg"][0], args.find_type)
                mem.w32(base + MB["host_cmd"][0], CMD_FINDTYPE)
            else:
                mem.w32(base + MB["host_cmd"][0], CMD_SELECT)
            mem.w32(base + MB["host_seq"][0], seq[0])

    def after_frame(index, ram):
        mem = Memory(ram)
        if mem.u32(base + MB["magic"][0]) != MAGIC:
            return
        # Layout check, once. The offsets in MB are hand-mirrored from
        # src/game/sl_romdbg.h, and a silently shifted field is exactly the
        # kind of confident-wrong answer this tool exists to remove. The ROM
        # publishes its own sizeof; if it disagrees, stop.
        if not checked[0]:
            checked[0] = True
            print(f"rominspect: {check_layout(mem, base)}")
        box = read_mailbox(mem, base)
        last_hook[0] = box["hook_calls"]
        # The ROM hook does NOT run on every emulator frame - the in-game path
        # is reached on roughly half of them. On the others the mailbox still
        # holds the previous frame's event word, so an event must be counted
        # once per rom_seq or the same capture is reported two and three times.
        # Measured: a 361-point sweep produced 527 "captures".
        fresh = box["rom_seq"] != last_seq[0]
        last_seq[0] = box["rom_seq"]
        if not fresh:
            return
        if box["event"] in (2, 3, 4, 5):    # SELECT / MISS / FIND / LOST
            shot = None
            if not args.no_shots and state["fb"] is not None:
                shot = f"shot-{index:06d}.ppm"
                write_ppm(out / shot, state["fb"], state["w"], state["h"],
                          state["pitch"])
            text = report(mem, box, index, out, shot)
            captures[0] += 1
            head = [ln for ln in text.splitlines()
                    if ln.startswith("event:") or "watched" in ln
                    or ln.startswith("  (empty")]
            print(f"  index {index:6d}  {EV_NAME.get(box['event'])}"
                  f"  hits={box['nhits']}"
                  f"  watch=0x{box['watch_prop']:08x}"
                  f"  objtype={box['watch_objtype']}")
            for ln in head[1:2]:
                print("      " + ln.strip())
        if box["watch_prop"]:
            csv.write(csv_row(index, box) + "\n")
            rows[0] += 1
        if writer is not None:
            st = reader.capture(mem, index)
            writer.add(index, box["frame"], st.composite, st.entity_hashes)

    # emu.run() gives us the tick callback AFTER retro_run, which is where the
    # mailbox is read. The host->ROM poke has to happen before, so drive the
    # loop here rather than through emu.run().
    emu._load()
    emu._warming_up = True
    emu._core.retro_run()
    emu._warming_up = False
    for i in range(total):
        ram = emu.rdram()
        if ram is not None:
            on_tick(i, 0, ram)
        emu._core.retro_run()
        emu._frame_index += 1
        ram = emu.rdram()
        if ram is not None:
            after_frame(i, ram)
    emu.close()
    csv.close()
    if writer is not None:
        writer.close()
        print(f"rominspect: state trace -> {args.trace}")

    print(f"rominspect: {captures[0]} captures, {rows[0]} watch rows -> {out}")
    # The known-positive control. An instrument that cannot fail is worthless.
    if last_hook[0] == 0:
        print("rominspect: FAILED - the ROM hook never ran (hook_calls == 0). "
              "Nothing in this run means anything.", file=sys.stderr)
        return 3
    print(f"rominspect: control OK - ROM hook ran {last_hook[0]} times")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
