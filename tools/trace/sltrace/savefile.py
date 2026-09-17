"""Read and edit the cartridge save the harness carries between runs.

Why this exists: recording a level requires reaching it, and reaching a late
level means completing every level before it. Twenty-plus levels of that,
before a single trace can be recorded, is not a reasonable prerequisite for
working on the engine. Seeding a save is.

The checksum routine is ported from the game's own (crc.c + the PRNG step in
random.s). Nothing here trusts that port blindly: `find_records` locates save
slots BY recomputing their checksums and demanding a match against what the
game itself wrote, so a bad port finds nothing rather than silently producing
a corrupt save. That check is also how the save area was located in the first
place - the core hands back one opaque blob covering SRAM, FlashRAM, EEPROM
and the controller paks, with no map of what lives where.
"""

from __future__ import annotations

import struct

MASK64 = (1 << 64) - 1

SAVE_SIZE = 0x60          # sizeof(save_data)
CRC_BEGIN = 8             # checksummed range starts after the two checksums
CRC_END = SAVE_SIZE
TIMES_OFF = 18            # save_data.times
TIMES_LEN = 76            # (SP_LEVEL_MAX - 1) * 4, 19 levels

#: The blob is far larger than the save area; slots live in its first page.
SEARCH_LIMIT = 1024


def _to_s32(v: int) -> int:
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v >> 31 else v


def rng_step(state: int) -> tuple[int, int]:
    """One step of the game's 64-bit PRNG; returns (new state, s32 output)."""
    x = state & MASK64
    a = ((x << 63) & MASK64) >> 31
    b = ((x << 31) & MASK64) >> 32
    c = ((x << 44) & MASK64) >> 32
    m = (a | b) ^ c
    nxt = ((((m >> 20) & 0xFFF) ^ m)) & MASK64
    return nxt, _to_s32(nxt)


def checksums(body: bytes) -> tuple[int, int]:
    """The pair the game stores at the head of each save slot."""
    poly = 0x8F809F473108B3C1
    c1 = c2 = 0
    shift = 0
    for byte in body:
        poly = (poly + (byte << (shift & 0xF))) & MASK64
        poly, out = rng_step(poly)
        c1 ^= out
        shift += 7
    for byte in reversed(body):
        poly = (poly + (byte << (shift & 0xF))) & MASK64
        poly, out = rng_step(poly)
        c2 ^= out
        shift += 3
    return _to_s32(c1), _to_s32(c2)


def find_records(blob: bytes) -> list[int]:
    """Offsets of save slots whose stored checksums verify."""
    hits = []
    for off in range(0, min(len(blob), SEARCH_LIMIT) - SAVE_SIZE + 1):
        stored = struct.unpack_from(">ii", blob, off)
        if stored == (0, 0):
            continue
        if checksums(blob[off + CRC_BEGIN:off + CRC_END]) == stored:
            hits.append(off)
    return hits


def reseal(record: bytearray) -> None:
    """Recompute the checksums in place after editing a slot."""
    c1, c2 = checksums(bytes(record[CRC_BEGIN:CRC_END]))
    struct.pack_into(">ii", record, 0, c1, c2)


def unlock_all(blob: bytes) -> tuple[bytes, list[int]]:
    """Mark every stage complete on every difficulty, in every valid slot.

    A stage counts as completed when its stored time is non-zero - that is the
    game's whole test - so filling the time table is enough. It means the
    packed 10-bit time fields never have to be decoded, and no assumption
    about level ordering or difficulty layout can be got wrong.
    """
    out = bytearray(blob)
    touched = []
    for off in find_records(blob):
        rec = bytearray(out[off:off + SAVE_SIZE])
        rec[TIMES_OFF:TIMES_OFF + TIMES_LEN] = b"\xFF" * TIMES_LEN
        reseal(rec)
        out[off:off + SAVE_SIZE] = rec
        touched.append(off)
    return bytes(out), touched
