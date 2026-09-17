#!/usr/bin/env python3
"""ACMD census: decode the audio command stream GoldenEye actually generates.

MEASUREMENT ONLY. This decodes command words; it implements no opcode
semantics and never executes or emulates the aspMain microcode.

WHY THE CARTRIDGE. The native build produces no command stream to census at
all: alAudioFrame is a `return 0` stub, and musicSeqPlayerInit returns early
(src/music.c:652) before amStartAudioThread (src/music.c:784), so the audio
thread never starts. The stream therefore has to be read out of the real
game's RDRAM. The ROM is unmodified and read from outside, in exactly the
window a native interpreter would occupy - AFTER alAudioFrame has built the
list (src/audi.c:537) and BEFORE osSendMesg hands the task to the RSP
(src/audi.c:561).

  g_AudioManager.cmdList[2]  0x8005e518   (offset 0 of the struct)
  g_CurrentAcmdList          0x800230fc
  g_CommandLength            0x8005eccc   COMMANDS, not bytes - alAudioFrame
                                          sets *cmdLen = cmdlEnd - cmdList
                                          (src/libultra/audio/synthesizer.c:226)
                                          and sizeof(Acmd) is 8.
  g_AudioFrameCount          0x800230f4

src/audi.c:563 flips g_CurrentAcmdList AFTER the dispatch, so the list just
handed over is cmdList[g_CurrentAcmdList ^ 1]. Reading the un-flipped index
would census the buffer about to be written NEXT frame - an off-by-one this
note exists to prevent.

Field encodings are from include/PR/abi.h (the a* emit macros at :272-402),
which is the authority: the corpus does not document the audio microcode or
the ACMD command set - see the "audio microcode / ACMD command set"
not_covered entry in docs/doc-routing.json.

  tools/native/acmd_census.py --input facility --frames 900 --out /tmp/sl-acmd

Captures are ROM-derived: they go under /tmp and are never committed.
"""
from __future__ import annotations

import argparse
import ctypes as C
import json
import pathlib
import struct
import sys
from collections import Counter, defaultdict

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))
sys.path.insert(0, str(ROOT / "tools/native"))

from sltrace.emu_libretro import LibretroEmulator   # noqa: E402
from sltrace.state import Memory, KSEG0             # noqa: E402
import audiocap                                     # noqa: E402

#: Symbols, from build/u/ge007.u.map. Identical in the direct-boot maps.
SYM = {"cmdList": 0x8005E518, "cur": 0x800230FC,
       "len": 0x8005ECCC, "cnt": 0x800230F4}

NAMES = {0: "SPNOOP", 1: "ADPCM", 2: "CLEARBUFF", 3: "ENVMIXER", 4: "LOADBUFF",
         5: "RESAMPLE", 6: "SAVEBUFF", 7: "SEGMENT", 8: "SETBUFF", 9: "SETVOL",
         10: "DMEMMOVE", 11: "LOADADPCM", 12: "MIXER", 13: "INTERLEAVE",
         14: "POLEF", 15: "SETLOOP"}

#: A voice is decoding or being enveloped. Frames with neither are the
#: "zero-voice" set the stage 1-3 interpreter is accepted against.
VOICE_OPS = ("ADPCM", "ENVMIXER")

CAPTURE_MAGIC = b"SLACMD01"


def decode(w0: int, w1: int) -> tuple[int, str, dict]:
    """Split one Acmd into named fields. Encodings per abi.h's emit macros.

    Note where the macros and the C structs disagree: aClearBuffer/aDMEMMove/
    aLoadADPCM shift their first operand as a 24-bit field while the matching
    struct declares 16. Every observed value is far below 16 bits (DMEM tops
    out at 0x800), so the two readings coincide on real data; the 16-bit
    reading is used and this note records that it is a CHOICE, not an
    accident.
    """
    op = (w0 >> 24) & 0xFF
    name = NAMES.get(op, f"UNK{op:02X}")
    f: dict = {}
    if op in (1, 14):                       # ADPCM / POLEF
        f = {"flags": (w0 >> 16) & 0xFF, "gain": w0 & 0xFFFF, "addr": w1}
    elif op == 2:                           # CLEARBUFF
        f = {"dmem": w0 & 0xFFFF, "count": w1 & 0xFFFF}
    elif op in (3, 4, 6, 15):               # ENVMIXER / LOADBUFF / SAVEBUFF / SETLOOP
        f = {"flags": (w0 >> 16) & 0xFF, "addr": w1}
    elif op == 5:                           # RESAMPLE
        f = {"flags": (w0 >> 16) & 0xFF, "pitch": w0 & 0xFFFF, "addr": w1}
    elif op == 7:                           # SEGMENT
        f = {"segment": (w1 >> 24) & 0xFF, "base": w1 & 0xFFFFFF, "raw_w1": w1}
    elif op == 8:                           # SETBUFF
        f = {"flags": (w0 >> 16) & 0xFF, "dmemin": w0 & 0xFFFF,
             "dmemout": (w1 >> 16) & 0xFFFF, "count": w1 & 0xFFFF}
    elif op == 9:                           # SETVOL
        f = {"flags": (w0 >> 16) & 0xFF, "vol": w0 & 0xFFFF,
             "voltgt": (w1 >> 16) & 0xFFFF, "volrate": w1 & 0xFFFF}
    elif op == 10:                          # DMEMMOVE
        f = {"dmemin": w0 & 0xFFFF, "dmemout": (w1 >> 16) & 0xFFFF,
             "count": w1 & 0xFFFF}
    elif op == 11:                          # LOADADPCM
        f = {"count": w0 & 0xFFFF, "addr": w1}
    elif op == 12:                          # MIXER
        f = {"flags": (w0 >> 16) & 0xFF, "gain": w0 & 0xFFFF,
             "dmemi": (w1 >> 16) & 0xFFFF, "dmemo": w1 & 0xFFFF}
    elif op == 13:                          # INTERLEAVE
        f = {"inL": (w1 >> 16) & 0xFFFF, "inR": w1 & 0xFFFF}
    return op, name, f


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", default="facility",
                    help="stream name under tools/trace/inputs")
    ap.add_argument("--frames", type=int, default=900)
    ap.add_argument("--out", default="/tmp/sl-acmd")
    ap.add_argument("--max-cmds", type=int, default=4096,
                    help="reject an implausible g_CommandLength rather than "
                         "read megabytes from a half-written pointer")
    ap.add_argument("--dram-lo", type=lambda s: int(s, 0), default=0x002CF000)
    ap.add_argument("--dram-hi", type=lambda s: int(s, 0), default=0x002E1000)
    ap.add_argument("--capture-all", action="store_true",
                    help="capture EVERY aligned frame, not just zero-voice "
                         "ones. A non-vacuous arithmetic witness may only "
                         "exist in a voice-bearing frame, and the taint pass "
                         "does not need to execute those opcodes to classify "
                         "their dependencies.")
    ap.add_argument("--capture", default=None, metavar="FILE",
                    help="also write a binary replay capture of the ZERO-VOICE "
                         "frames: commands plus the audio DRAM window before "
                         "and after each. This is what lets an interpreter be "
                         "replayed against the cartridge offline.")
    a = ap.parse_args()

    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    inp = ROOT / "tools/trace/inputs" / f"{a.input}.input"
    eep = ROOT / "tools/trace/inputs" / f"{a.input}.eeprom"
    if not inp.is_file():
        print(f"acmd_census: no such stream: {inp}", file=sys.stderr)
        return 2
    rom = ROOT / "build/u/direct" / f"ge007.u.{a.input}.z64"
    if not rom.is_file():
        rom = ROOT / "baserom.u.z64"

    emu = LibretroEmulator(str(rom), input_stream=str(inp),
                           eeprom=str(eep) if eep.is_file() else None)
    chunks: list[bytes] = []

    def on_audio(data, frames):
        n = int(frames)
        if n > 0:
            chunks.append(C.string_at(data, n * 4))
    emu._on_audio_capture = on_audio

    emu._load()
    emu._core.retro_run()
    mem = Memory(emu.rdram())
    dsz = a.dram_hi - a.dram_lo

    frames: list[dict] = []
    seen: set[int] = set()
    prev_af = None
    prev_dram = None
    pending: list = []

    for vf in range(a.frames):
        emu._core.retro_run()
        try:
            af = mem.u32(SYM["cnt"])
            cur = mem.u32(SYM["cur"])
            ln = mem.s32(SYM["len"])
        except Exception:
            continue
        # Memory works in KSEG0; the census window is PHYSICAL because that
        # is what the command stream carries. The translation is the same
        # subtraction a native interpreter performs, spelled out here.
        dram = mem.block(KSEG0 | a.dram_lo, dsz) if a.capture else None

        # BRACKET REPAIR, measured 2026-08-28. The RSP's writes for the task
        # dispatched during video frame vf land between vf and vf+1, NOT
        # between vf-1 and vf. Measured over 335 tasks: diff(vf-1, vf) changes
        # a mean of 14.5 64-byte blocks (CPU-side churn only) while
        # diff(vf, vf+1) changes 131.1, and the pattern alternates cleanly with
        # the audio cadence. The original capture used the vf-1 bracket, which
        # is why every SAVEBUFF target looked unchanged and a null interpreter
        # scored perfectly. So: before = this frame's snapshot, after = the
        # NEXT frame's, resolved one iteration later.
        for pend in pending:
            if pend["_await_vf"] == vf:
                pend["_after"] = dram
                pend["aligned"] = pend["_before"] is not None
        pending = [q for q in pending if q.get("_after") is None]

        if af in seen or ln <= 0 or ln > a.max_cmds:
            prev_af, prev_dram = af, dram
            continue
        seen.add(af)
        idx = cur ^ 1                     # flip happens AFTER dispatch
        try:
            ptr = mem.u32(SYM["cmdList"] + 4 * idx)
            if not mem.valid_ptr(ptr):
                prev_af, prev_dram = af, dram
                continue
            blob = mem.block(ptr, ln * 8)
        except Exception:
            prev_af, prev_dram = af, dram
            continue

        cmds = []
        for i in range(ln):
            w0 = int.from_bytes(blob[i * 8:i * 8 + 4], "big")
            w1 = int.from_bytes(blob[i * 8 + 4:i * 8 + 8], "big")
            op, nm, fl = decode(w0, w1)
            cmds.append({"i": i, "op": op, "name": nm, "w0": w0, "w1": w1, "f": fl})

        rec = {"video_frame": vf, "audio_frame": af, "list_index": idx,
               "list_ptr": ptr, "n": ln, "cmds": cmds,
               # Resolved when the NEXT frame's snapshot arrives; see the
               # bracket-repair note above.
               "aligned": False}
        if a.capture:
            rec["_before"] = dram
            rec["_after"] = None
            rec["_await_vf"] = vf + 1
            pending.append(rec)
        frames.append(rec)
        prev_af, prev_dram = af, dram

    emu.close()

    pcm = b"".join(chunks)
    cap = audiocap.Capture(pcm, rate=22050, endian="little", source="rom",
                           meta={"input": a.input, "frames": a.frames,
                                 "rom": str(rom),
                                 "git_rev": audiocap.git_rev(ROOT)})
    m = cap.metrics()
    cap.write_wav(out / "audio.wav")
    cap.write_json(out / "audio.json")

    slim = [{k: v for k, v in f.items() if not k.startswith("_")} for f in frames]
    (out / "frames.json").write_text(json.dumps(slim) + "\n")

    def prof(fr):
        return Counter(c["name"] for c in fr["cmds"])

    zero_voice = [f for f in frames if not any(prof(f)[o] for o in VOICE_OPS)]

    if a.capture:
        pool = frames if a.capture_all else zero_voice
        usable = [f for f in pool if f["aligned"]
                  and f["_before"] is not None and f["_after"] is not None]
        with open(a.capture, "wb") as fh:
            fh.write(CAPTURE_MAGIC)
            fh.write(struct.pack(">III", len(usable), a.dram_lo, dsz))
            for f in usable:
                fh.write(struct.pack(">III", f["audio_frame"],
                                     f["video_frame"], f["n"]))
                for c in f["cmds"]:
                    fh.write(struct.pack(">II", c["w0"], c["w1"]))
                fh.write(f["_before"])
                fh.write(f["_after"])
        print(f"capture: {len(usable)} aligned "
              f"{'' if a.capture_all else 'zero-voice '}frames -> {a.capture}")

    print(f"video frames run : {a.frames}")
    print(f"audio frames seen: {len(frames)}")
    if not frames:
        print("NO ACMD LISTS CAPTURED - census cannot report an opcode set",
              file=sys.stderr)
        return 2
    print(f"cmds/frame       : min {min(f['n'] for f in frames)} "
          f"max {max(f['n'] for f in frames)}")
    print(f"zero-voice frames: {len(zero_voice)} "
          f"(no ADPCM and no ENVMIXER)")
    print(f"PCM: frames={m['stereo_frames']} peak={m['peak']} "
          f"nonzero={100 * m['nonzero_frac']:.2f}% onset={m['onset_frame']}")

    total: Counter = Counter()
    first: dict = {}
    per: dict = defaultdict(list)
    present: Counter = Counter()
    for fr in frames:
        c = prof(fr)
        for k, v in c.items():
            total[k] += v
            per[k].append(v)
            present[k] += 1
            first.setdefault(k, fr["audio_frame"])
    print(f"\n{'opcode':<12}{'total':>9}{'/frame':>9}{'first af':>10}{'frames':>10}")
    for k, v in total.most_common():
        print(f"{k:<12}{v:>9}{sum(per[k]) / len(per[k]):>9.1f}"
              f"{first[k]:>10}{present[k]:>7}/{len(frames)}")
    unused = [NAMES[o] for o in sorted(NAMES) if NAMES[o] not in total]
    print(f"\nZERO OBSERVED USES ({len(unused)}/16): "
          f"{', '.join(unused) if unused else 'none'}")
    print(f"\n-> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
