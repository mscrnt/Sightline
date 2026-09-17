#!/usr/bin/env python3
"""Read the RUNNING ROM's live fog/environment block out of RDRAM.

Frame colours say what changed; this says why. The notes put the active
environment at 0x80044DC0 ("current sky", with 0x80044DC4 the near fog value)
and the table of per-stage records at 0x80044E10, 0x5C bytes each - see
"Background File Data/Fog Water and Sky/fog and sky.txt" and the copy routine
disassembled in "NTSC fog and sky.txt" (7F0BAA64).

    tools/native/romprobe.py --input facility-gas --frames 11231 --every 400
"""
import argparse, pathlib, struct, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))

# Addresses come from the DIRECT-BOOT BUILD'S OWN SYMBOL MAP, not from the
# notes' retail addresses - this ROM is compiled from the decomp, so its
# globals sit wherever the linker put them. Reading the retail address gave a
# block that never changed while the gas plainly did.
SCALED_FAR = 0x44DC4       # g_ScaledFarFogIntensity  (f32)
CUR_ENV    = 0x44DCC       # g_CurrentEnvironment
FOG_DETAIL = 0x825C8       # g_CurFogDetails
FAR_INTEN  = 0x825E0       # g_FarFogIntensity        (f32)


def f32(b, o):
    return struct.unpack_from(">f", b, o)[0]


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True)
    ap.add_argument("--frames", type=int, default=11231)
    ap.add_argument("--every", type=int, default=400)
    args = ap.parse_args()

    from sltrace.emu_libretro import LibretroEmulator
    import ctypes as C

    inp = ROOT / "tools/trace/inputs" / (args.input + ".input")
    direct = ROOT / "build/u/direct" / f"ge007.u.{args.input}.z64"
    rom = str(direct) if direct.is_file() else str(ROOT / "baserom.u.z64")
    print(f"romprobe: rom = {rom}")

    # The eeprom is NOT optional. trace-verify restores it, and without it the
    # same input answers a different saved game and diverges - measured: the
    # gas timer read 0.000 for a whole run the owner had blown the tanks in.
    eep = ROOT / "tools/trace/inputs" / (args.input + ".eeprom")
    emu = LibretroEmulator(rom, input_stream=str(inp),
                           eeprom=str(eep) if eep.is_file() else None)
    prev = None
    print("  g_CurrentEnvironment, dumped only when it CHANGES")

    def on_tick(index, fc, ram):
        nonlocal prev
        if index % args.every or not ram:
            return
        env = bytes(bytearray(ram[CUR_ENV:CUR_ENV + 0x40]))
        row = env[:0x30]
        if prev is not None and row == prev:
            return
        print(f"  frame {index:6d}")
        for off in range(0, 0x30, 16):
            print(f"    +{off:02x}  {env[off:off+16].hex(' ')}")
        prev = row
        prev = row

    emu.run(on_tick, lambda ram: 0, max_ticks=args.frames)
    emu.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
