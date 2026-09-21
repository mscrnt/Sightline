#!/usr/bin/env python3
"""Independent derivation of the audio microcode's data tables from a ROM.

The ORACLE for tools/windows/ucodetabtest.ps1: it reproduces what
src/platform/sl_ucode.c does at start-up - ROM 137616 / 71760 bytes, the 1172
container header, raw deflate, the aspMain data segment at inflated
0x3c290..0x3c550, the ENVMIXER ramp at +0xB0 (8 shorts) and the RESAMPLE
polyphase table at +0xC0 (256 shorts), big-endian in the ROM - with the
standard library's zlib instead of the game's inflater, so the two agreeing
means something.

Writes <out>/ramp.bin (16 bytes) and <out>/taps.bin (512 bytes) as
LITTLE-ENDIAN shorts, the i686 build's host order, which is exactly what
SL_UCODE_TABLES_DUMP writes. Prints sizes and digests only - never a value.
The output is ROM-derived: write it to a scratch directory and delete it.

    python tools/native/ucode_tables_oracle.py <rom.z64> <out-dir>
"""
import hashlib
import pathlib
import struct
import sys
import zlib

ROM_SIZE = 12582912
CDATA_OFF, CDATA_LEN = 137616, 71760          # scripts/extract_asp_gsp_rsp.sh
RAMSTART = 0x80020D90
SEG_LO, SEG_HI = 0x8005D020 - RAMSTART, 0x8005D2E0 - RAMSTART
RAMP_OFF, RAMP_N = 0xB0, 8
TAPS_OFF, TAPS_N = 0xC0, 256


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    rom = pathlib.Path(sys.argv[1]).read_bytes()
    out = pathlib.Path(sys.argv[2])
    if len(rom) != ROM_SIZE or rom[:4] != b"\x80\x37\x12\x40":
        sys.stderr.write("ucode_tables_oracle: not a plain 12582912-byte big-endian .z64\n")
        return 2
    cdata = rom[CDATA_OFF:CDATA_OFF + CDATA_LEN]
    if cdata[:2] != b"\x11\x72":
        sys.stderr.write("ucode_tables_oracle: no 1172 header at ROM 0x21990\n")
        return 2
    inflated = zlib.decompressobj(-15).decompress(cdata[2:])
    seg = inflated[SEG_LO:SEG_HI]
    if len(seg) != SEG_HI - SEG_LO:
        sys.stderr.write(f"ucode_tables_oracle: inflated {len(inflated)} bytes, segment short\n")
        return 2
    ramp = struct.unpack(f">{RAMP_N}h", seg[RAMP_OFF:RAMP_OFF + 2 * RAMP_N])
    taps = struct.unpack(f">{TAPS_N}h", seg[TAPS_OFF:TAPS_OFF + 2 * TAPS_N])
    if not any(ramp):
        sys.stderr.write("ucode_tables_oracle: the ENVMIXER ramp is all zeros\n")
        return 2
    out.mkdir(parents=True, exist_ok=True)
    ramp_le = struct.pack(f"<{RAMP_N}h", *ramp)
    taps_le = struct.pack(f"<{TAPS_N}h", *taps)
    (out / "ramp.bin").write_bytes(ramp_le)
    (out / "taps.bin").write_bytes(taps_le)
    print(f"cdata sha1 {hashlib.sha1(cdata).hexdigest()}  inflated {len(inflated)} bytes")
    print(f"segment {len(seg)} bytes sha1 {hashlib.sha1(seg).hexdigest()}")
    print(f"ramp.bin {len(ramp_le)} bytes sha256 {hashlib.sha256(ramp_le).hexdigest()}")
    print(f"taps.bin {len(taps_le)} bytes sha256 {hashlib.sha256(taps_le).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
