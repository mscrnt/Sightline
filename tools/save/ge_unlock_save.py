#!/usr/bin/env python3
"""Generate (or validate) a fully-unlocked GoldenEye 007 (U) EEPROM save.

WHY THIS EXISTS. ROM-side testing needs every solo stage and every cheat
available, and the game gates both behind per-folder save state. This tool
synthesizes that state from format knowledge alone - nothing is read from,
or derived from, any ROM.

AUTHORITY, cited per the project's docs-first rule (docs/project-rules.md, rule 7):

  format   goldeneye_docs "Gameplay and Misc/Goldeneye eeprom saves.txt":
           the 0x60-byte save block - checksum pair at +0x00/+0x04 (stored
           big-endian, see below), flags at +0x08 (folder 0x07, wear slot
           0x18, bond 0x60, do-reset 0x80), 007-mode flag +0x09 bit 0x01
           ("all solo stages available"), music/sfx volumes +0x0A/+0x0B,
           controller config +0x0C, option bits +0x0D, the 20-cheat unlock
           bitfield +0x0E..+0x10, and sixty 10-bit best-completion times
           packed MSB-first from +0x12 (Agent block, then Secret Agent,
           then 00 Agent; 007 is not stored; 0 = incomplete, valid times
           1..0x3FF seconds).
  decomp   src/game/file2.c fileValidateSaves(): EEPROM byte 0 holds a
           32-byte smallSave {crc1, crc2, u8 unk[24]} whose unk[0] must
           equal SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY, FALSE)
           = 0x42, CRC over unk[0..24); five 0x60-byte save_data slots
           follow from byte 32 (device block address 4), CRC over bytes
           +0x08..+0x60. src/game/crc.c fileGenerateCRC() is the checksum
           (two XOR-folded passes of the game's own 64-bit RNG step,
           src/game/sl_ported_asm.c sl_rng_step, seeded 0x8F809F473108B3C1),
           and src/game/file2.c fileGetSaveStageDifficultyTime()/
           fileSetDifficultyStageTime() pin the 10-bit packing.
           Checksums are compared as VALUES after big-endian promotion
           (file2.c:497-543), so they are big-endian on the wire; every
           other field is byte-wise wire order. options is a big-endian
           u16 covering +0x0C..+0x0D (file2.c:571).
  targets  src/game/front.c:803 solo_target_time_array - the fastest
           cheat target is 80 s (Archives, 00 Agent), so the default
           stored time of 60 s beats every target and the save is
           self-consistent with its directly-set cheat bits.

GROUND TRUTH. The checksum implementation here was validated against a
real save written by the game itself (all six blocks matched, including
the do-reset spare slot) before this tool was committed.

WHAT IT WRITES. All four folders identical: 007 mode on, all 20 cheats
unlocked (invincibility is bit 1 of +0x0E), and every stage/difficulty
combination the game itself can record completed at --time seconds
(default 60). Aztec below Secret Agent and Egyptian below 00 Agent are
left incomplete on purpose - fileIsStageUnlockedAtDifficulty() refuses
those combinations (file2.c:680), so a real save never holds them.
Slot 4 is the wear-level spare, written exactly as the game writes it
(BLANKSAVEDATA with the do-reset flag and a valid checksum).

CONTAINERS. A raw EEPROM image (.eep) is the 512 bytes themselves. A
libretro .srm for the mupen64plus family (parallel_n64 included) is the
core's SAVE_RAM blob: EEPROM at offset 0 (0x800 reserved), then 4
mempaks, SRAM and FlashRAM - 0x48800 bytes total; only the first 512
bytes belong to this game and only they are touched. An existing target
is backed up beside itself (timestamped) before patching unless
--no-backup is given.

USAGE
  ge_unlock_save.py --out <save.srm|save.eep>   generate/patch + self-verify
  ge_unlock_save.py --check <file>              validate any save, write nothing
  ge_unlock_save.py --out <file> --time 95      choose the stored best time
"""

import argparse
import datetime
import os
import shutil
import struct
import sys

MASK64 = (1 << 64) - 1
CRC_SEED = 0x8F809F473108B3C1
SMALLSAVE_MAGIC = 0x42          # SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY, FALSE)
EEPROM_SIZE = 512               # 4kbit device
SRM_SIZE = 0x48800              # mupen64plus-family SAVE_RAM blob
BLOCK = 0x60
SAVES_OFF = 0x20                # device block address 4 = byte 32
LEVELS = 20                     # SP_LEVEL_DAM..SP_LEVEL_EGYPT
DIFFS = 3                       # Agent, Secret Agent, 00 Agent (007 not stored)
AZTEC, EGYPT = 18, 19
DEFAULT_OPTIONS = 0x003A        # OPTION_AUTOAIM|SIGHTONSCREEN|LOOKAHEAD|DISPLAYAMMO


# ---- checksum, transcribed from src/game/crc.c + sl_rng_step ------------

def _rng_step(seed):
    a2 = ((seed & 1) << 32) | ((seed >> 1) & 0xFFFFFFFF)
    a2 ^= ((seed << 44) & MASK64) >> 32
    return ((a2 >> 20) & 0xFFF) ^ a2


def generate_crc(buf):
    """fileGenerateCRC over buf: (checksum1, checksum2) as u32."""
    poly = CRC_SEED
    shift = 0
    c1 = 0
    for b in buf:
        poly = (poly + (b << (shift & 0xF))) & MASK64
        poly = _rng_step(poly)
        c1 ^= poly & 0xFFFFFFFF
        shift += 7
    c2 = 0
    for b in reversed(buf):
        poly = (poly + (b << (shift & 0xF))) & MASK64
        poly = _rng_step(poly)
        c2 ^= poly & 0xFFFFFFFF
        shift += 3
    return c1, c2


# ---- an INDEPENDENT second implementation, for self-verification --------
# Written separately from generate_crc on purpose: the two agreeing is the
# defence against a transcription slip in either. The RNG step is expressed
# differently (bit arithmetic on one integer, no compound masks) and the
# passes drive indices instead of iterators.

def _rng_step_alt(s):
    lo20 = s % (1 << 20)
    a = ((s % 2) * (1 << 32)) + ((s // 2) % (1 << 32))
    a = a ^ (lo20 * (1 << 12))
    return ((a // (1 << 20)) % (1 << 12)) ^ a


def generate_crc_alt(buf):
    n = len(buf)
    poly = CRC_SEED
    c1 = 0
    for i in range(n):
        poly = (poly + (buf[i] << ((7 * i) % 16))) % (1 << 64)
        poly = _rng_step_alt(poly)
        c1 = c1 ^ (poly % (1 << 32))
    c2 = 0
    for j in range(n):
        sh = 7 * n + 3 * j
        poly = (poly + (buf[n - 1 - j] << (sh % 16))) % (1 << 64)
        poly = _rng_step_alt(poly)
        c2 = c2 ^ (poly % (1 << 32))
    return c1, c2


# ---- block builders ------------------------------------------------------

def build_smallsave():
    body = bytes([SMALLSAVE_MAGIC]) + bytes(23)
    c1, c2 = generate_crc(body)
    return struct.pack(">II", c1, c2) + body


def pack_times(seconds):
    """The sixty 10-bit fields, MSB-first from +0x12, as file2.c packs them."""
    bits = 0
    nfields = DIFFS * LEVELS
    for diff in range(DIFFS):
        for level in range(LEVELS):
            if level == AZTEC and diff < 1:
                continue    # Aztec exists only from Secret Agent up
            if level == EGYPT and diff < 2:
                continue    # Egyptian exists only at 00 Agent
            start = (diff * LEVELS + level) * 10
            bits |= (seconds & 0x3FF) << (nfields * 10 - 10 - start)
    return bits.to_bytes((nfields * 10 + 7) // 8, "big")


def build_folder_save(folder, seconds):
    flags = (folder & 0x07) | (0 << 3) | (0 << 5)   # wear slot 0, Brosnan
    body = bytearray()
    body.append(flags)                 # +0x08 completion_bitflags
    body.append(0x01)                  # +0x09 007 mode unlocked
    body.append(0xFF)                  # +0x0A music volume
    body.append(0xFF)                  # +0x0B sfx volume
    body += struct.pack(">H", DEFAULT_OPTIONS)   # +0x0C..0x0D
    body += bytes([0xFF, 0xFF, 0x0F])  # +0x0E..0x10 all 20 cheats
    body.append(0x00)                  # +0x11 padding
    body += pack_times(seconds)        # +0x12..0x5C (75 bytes)
    body += bytes(BLOCK - 8 - len(body))
    assert len(body) == BLOCK - 8
    c1, c2 = generate_crc(body)
    return struct.pack(">II", c1, c2) + bytes(body)


def build_spare_save():
    """Slot 4 as the game itself writes it: BLANKSAVEDATA + do-reset flag."""
    body = bytearray(BLOCK - 8)
    body[0] = 0x80                     # SAVEFLAG_DORESET
    body[2] = 0xFF                     # music
    body[3] = 0xFF                     # sfx
    body[4:6] = struct.pack(">H", DEFAULT_OPTIONS)
    c1, c2 = generate_crc(body)
    return struct.pack(">II", c1, c2) + bytes(body)


def build_eeprom(seconds):
    ee = bytearray(EEPROM_SIZE)
    ee[0:SAVES_OFF] = build_smallsave()
    for folder in range(4):
        off = SAVES_OFF + folder * BLOCK
        ee[off:off + BLOCK] = build_folder_save(folder, seconds)
    off = SAVES_OFF + 4 * BLOCK
    ee[off:off + BLOCK] = build_spare_save()
    assert len(ee) == EEPROM_SIZE
    return bytes(ee)


# ---- independent verifier ------------------------------------------------

def verify_eeprom(ee, expect_unlocked=False, seconds=None):
    """Validate 512 EEPROM bytes the way fileValidateSaves does, using the
    independent checksum implementation. Returns a list of findings; empty
    means valid (and, with expect_unlocked, fully unlocked)."""
    bad = []
    s1, s2 = struct.unpack(">II", ee[0:8])
    c1, c2 = generate_crc_alt(ee[8:0x20])
    if ee[8] != SMALLSAVE_MAGIC:
        bad.append("smallSave magic %02x != %02x" % (ee[8], SMALLSAVE_MAGIC))
    if (s1, s2) != (c1, c2):
        bad.append("smallSave checksum stored %08x/%08x != computed %08x/%08x"
                   % (s1, s2, c1, c2))
    for slot in range(5):
        off = SAVES_OFF + slot * BLOCK
        blk = ee[off:off + BLOCK]
        s1, s2 = struct.unpack(">II", blk[0:8])
        c1, c2 = generate_crc_alt(blk[8:BLOCK])
        if (s1, s2) != (c1, c2):
            bad.append("slot %d checksum stored %08x/%08x != computed %08x/%08x"
                       % (slot, s1, s2, c1, c2))
            continue
        flags = blk[8]
        if slot < 4 and expect_unlocked:
            if flags & 0x80:
                bad.append("slot %d unexpectedly marked do-reset" % slot)
            if (flags & 0x07) != slot:
                bad.append("slot %d folder field %d" % (slot, flags & 0x07))
            if not (blk[9] & 0x01):
                bad.append("slot %d: 007 mode not set" % slot)
            cheats = blk[0x0E] | (blk[0x0F] << 8) | (blk[0x10] << 16)
            if cheats != 0xFFFFF:
                bad.append("slot %d cheats %05x != fffff" % (slot, cheats))
            # decode every 10-bit time per the docs table (MSB-first)
            tbits = int.from_bytes(blk[0x12:0x12 + 75], "big")
            for diff in range(DIFFS):
                for level in range(LEVELS):
                    start = (diff * LEVELS + level) * 10
                    t = (tbits >> (600 - 10 - start)) & 0x3FF
                    if level == AZTEC and diff < 1 or level == EGYPT and diff < 2:
                        if t != 0:
                            bad.append("slot %d d%d l%d: bonus stage holds a "
                                       "time the game cannot write" % (slot, diff, level))
                    elif t == 0:
                        bad.append("slot %d d%d l%d incomplete" % (slot, diff, level))
                    elif seconds is not None and t != seconds:
                        bad.append("slot %d d%d l%d time %d != %d"
                                   % (slot, diff, level, t, seconds))
    return bad


# ---- container handling --------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", help="target save file (.srm container or raw .eep)")
    ap.add_argument("--check", help="validate an existing save file and exit")
    ap.add_argument("--time", type=int, default=60,
                    help="stored best time in seconds, 1..1023 (default 60; "
                         "the fastest cheat target is 80)")
    ap.add_argument("--no-backup", action="store_true",
                    help="do not write a timestamped backup of an existing target")
    args = ap.parse_args()

    if not (1 <= args.time <= 0x3FF):
        ap.error("--time must be 1..1023")

    if args.check:
        data = open(args.check, "rb").read()
        if len(data) < EEPROM_SIZE:
            print("FAIL: %s is %d bytes; need at least %d"
                  % (args.check, len(data), EEPROM_SIZE))
            return 1
        findings = verify_eeprom(data[:EEPROM_SIZE])
        if findings:
            for f in findings:
                print("FAIL:", f)
            return 1
        print("OK: %s carries a structurally valid GoldenEye EEPROM "
              "(checksums verified)" % args.check)
        return 0

    if not args.out:
        ap.error("one of --out or --check is required")

    ee = build_eeprom(args.time)

    # cross-check the two checksum implementations before anything is written
    probe = ee[SAVES_OFF + 8:SAVES_OFF + BLOCK]
    if generate_crc(probe) != generate_crc_alt(probe):
        print("FAIL: internal checksum implementations disagree; refusing to write")
        return 1

    out = args.out
    existed = os.path.exists(out)
    if existed:
        if not args.no_backup:
            stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
            backup = "%s.backup-%s" % (out, stamp)
            shutil.copy2(out, backup)
            print("backup:", backup)
        container = bytearray(open(out, "rb").read())
        if len(container) < EEPROM_SIZE:
            print("FAIL: existing %s is smaller than an EEPROM image" % out)
            return 1
    else:
        raw = out.lower().endswith(".eep") or out.lower().endswith(".bin")
        container = bytearray(EEPROM_SIZE if raw else SRM_SIZE)

    container[:EEPROM_SIZE] = ee
    with open(out, "wb") as f:
        f.write(container)
    print("wrote: %s (%d bytes; EEPROM occupies the first %d)"
          % (out, len(container), EEPROM_SIZE))

    # self-verify: re-read the FILE and validate with the independent path
    reread = open(out, "rb").read()
    findings = verify_eeprom(reread[:EEPROM_SIZE], expect_unlocked=True,
                             seconds=args.time)
    if findings:
        for f in findings:
            print("FAIL:", f)
        return 1
    print("self-verify: OK - checksums (independent implementation), 0x42 "
          "magic, 4 folders, 007 mode, 20 cheats, %d completions at %d s"
          % (DIFFS * LEVELS - 3, args.time))
    return 0


if __name__ == "__main__":
    sys.exit(main())
