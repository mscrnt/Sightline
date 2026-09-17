#!/usr/bin/env python3
"""Cartridge-side SEAM capture: identify the music-track play invocation and
report its identity tuple, for comparison against the native side's.

MEASUREMENT ONLY. Reads the user's own ROM through the existing libretro
harness; nothing ROM-derived is written to the repository.

WHY INVOCATION DETECTION RATHER THAN FRAME SAMPLING. The native tuple is
anchored on event identity - which track, which sequence entry, that entry's
two lengths - precisely because frame numbers drift for reasons unrelated to
the sequence data. Sampling on a frame schedule would reintroduce the anchoring
the design rejects, so the detector watches g_musicXTrack1CurrentTrackNum and
fires only on a CHANGE, which is the play itself.

THE DETECTOR'S NEGATIVE CONTROL IS PART OF THE DETECTOR, not an afterthought.
--control re-runs the detection predicate at frames adjacent to each accepted
invocation and requires it to REJECT them. A detector nobody has watched fail
is not known to work: the health gate's first negative control passed while
testing nothing, and that precedent is the reason this one ships alongside.

Symbols from build/u/ge007.u.map.
"""
import argparse, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))
from sltrace.emu_libretro import LibretroEmulator          # noqa: E402
from sltrace.state import Memory                            # noqa: E402

SYM = {
    "trackNum":  0x80024334,   # g_musicXTrack1CurrentTrackNum - s32 (music.c:88),
                               # NOT u16: reading 16 bits takes the HIGH half of a
                               # big-endian s32, which is 0 for any small track
                               # number. Cost 900 frames of "no invocation".
    "dataTable": 0x80063734,   # g_musicDataTable (pointer)
    "ulenArr":   0x80063738,   # g_musicTrackLength[]
    "clenArr":   0x800637B8,   # g_musicTrackCompressedLength[]
    "seqData":   0x80063838,   # g_musicXTrack1SeqData - a POINTER (u8 *,
                               # music.c:503), so read 32 bits here and the
                               # buffer lives at the address it holds.
}

#: Declared types, checked against src/music.c before any read. Recorded here
#: because reading a field at the wrong width cost 900 frames of "no
#: invocation" once already: g_musicXTrack1CurrentTrackNum is s32 (music.c:88)
#: and a 16-bit read takes the high half of a big-endian word.
DECLARED = {
    "trackNum":  ("s32 (music.c:88)",   "u32"),
    "dataTable": ("RareALSeqBankFile * (485)", "u32 pointer"),
    "ulenArr":   ("u16 (490)",          "u16"),
    "clenArr":   ("u16 (495)",          "u16"),
    "seqData":   ("u8 * (503)",         "u32 pointer, then bytes"),
}


def grab_buffer(mem, ulen):
    """The decompressed sequence buffer, with the capture sanity-checked before
    it is trusted. All-zero, single-valued, or short is an INSTRUMENT failure -
    it must fail loudly rather than be compared and reported as a finding."""
    ptr = mem.u32(SYM["seqData"])
    if not (0x80000000 <= ptr < 0x80800000):
        return None, f"seqData pointer {ptr:#x} outside RDRAM"
    if ulen == 0:
        return None, "declared length is zero"
    buf = mem.block(ptr, (ulen + 3) & ~3)[:ulen]
    if len(buf) != ulen:
        return None, f"short read: {len(buf)} of {ulen}"
    if not any(buf):
        return None, "buffer is entirely zero"
    if len(set(buf)) == 1:
        return None, f"buffer is a single repeated byte {buf[0]:#04x}"
    return buf, None


def read_tuple(mem, trk):
    """The identity tuple, from the same three sources the native side uses:
    the entry address out of the patched table, and the two length arrays."""
    tbl = mem.u32(SYM["dataTable"])
    entry_addr = None
    if 0x80000000 <= tbl < 0x80800000:
        # seqArray starts at +4; entries are 8 bytes {u32 addr, u16 ulen, u16 clen}
        entry_addr = mem.u32(tbl + 4 + 8 * trk)
    return {
        "track": trk,
        "entryAddr": entry_addr,
        "ulen": mem.u16(SYM["ulenArr"] + 2 * trk),
        "clen": mem.u16(SYM["clenArr"] + 2 * trk),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default="facility")
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--dump", default=None, metavar="FILE",
                    help="write the decompressed sequence buffer (kept out of the repo)")
    ap.add_argument("--control", action="store_true",
                    help="prove the detector rejects deliberately misaligned samples")
    a = ap.parse_args()

    rom = ROOT / "baserom.u.z64"
    inp = ROOT / "tools/trace/inputs" / f"{a.input}.input"
    for p in (rom, inp):
        if not p.is_file():
            sys.stderr.write(f"seamcap: missing {p}\n")
            return 2

    eep = ROOT / "tools/trace/inputs" / f"{a.input}.eep"
    emu = LibretroEmulator(str(rom), input_stream=str(inp),
                           eeprom=str(eep) if eep.is_file() else None)
    emu._load()
    emu._core.retro_run()
    mem = Memory(emu.rdram())

    history = []          # (frame, trackNum) every frame - the control needs neighbours
    accepted = []         # frames where the detector fired
    prev = None
    for fr in range(a.frames):
        emu._core.retro_run()
        trk = mem.u32(SYM["trackNum"])
        history.append((fr, trk))
        if prev is not None and trk != prev:
            t = read_tuple(mem, trk)
            buf, why = grab_buffer(mem, t["ulen"])
            accepted.append((fr, prev, trk, t, buf, why))
        prev = trk
    emu.close()

    from collections import Counter
    seen = Counter(t for _, t in history)
    print(f"seamcap: level={a.input} frames={a.frames} "
          f"invocations detected={len(accepted)}")
    print(f"  DIAGNOSTIC distinct trackNum values seen: "
          f"{dict(list(seen.most_common(6)))}")
    for k, (decl, how) in DECLARED.items():
        print(f"  WIDTH {k:<10} declared {decl:<28} read as {how}")
    print(f"  DIAGNOSTIC dataTable pointer = "
          f"{hex(mem.u32(SYM['dataTable']))}")
    for fr, old, new, t, buf, why in accepted[:6]:
        print(f"  SEAM[cartridge] frame={fr} trackNum {old} -> {new}  "
              f"track={t['track']} entryAddr={t['entryAddr'] and hex(t['entryAddr'])} "
              f"ulen={t['ulen']} clen={t['clen']}")
        if why:
            print(f"    BUFFER CAPTURE FAILED: {why}")
        else:
            import hashlib
            print(f"    buffer len={len(buf)} sha1={hashlib.sha1(buf).hexdigest()}"
                  f" first16={' '.join(f'{b:02x}' for b in buf[:16])}")
            if a.dump:
                pathlib.Path(a.dump).write_bytes(buf)
                print(f"    written to {a.dump} (outside the repository)")

    if a.control:
        # The predicate is "the track number changed at this frame". Re-apply it
        # at the neighbours of every accepted invocation; each must be rejected.
        idx = {f: i for i, (f, _) in enumerate(history)}
        checked = rejected = 0
        for fr, _, _, _, _, _ in accepted:
            for delta in (-1, +1):
                nb = fr + delta
                if nb not in idx or nb == 0:
                    continue
                checked += 1
                changed = history[idx[nb]][1] != history[idx[nb] - 1][1]
                if not changed:
                    rejected += 1
        print(f"  CONTROL: adjacent frames tested={checked} rejected={rejected}")
        if checked == 0:
            print("  CONTROL VACUOUS: no neighbours tested - nothing was proven")
            return 3
        if rejected != checked:
            print(f"  CONTROL FAILED: {checked - rejected} misaligned sample(s) "
                  f"were ACCEPTED as invocations")
            return 4
        print("  CONTROL PASS: every deliberately misaligned sample rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
