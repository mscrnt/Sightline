#!/usr/bin/env python3
"""ACMD parity: compare the native audio manager's command stream against the
cartridge's, on the DERIVED SCHEDULER/AUDIO EVENT DOMAIN.

MEASUREMENT ONLY. Nothing here executes a command or emulates the microcode;
it decodes words and reports differences. Captures are ROM-derived and live
under /tmp - none of their bytes are committed.

WHY EVENT PARITY COMES FIRST. "435 tasks over 900 frames" is a count in the
CARTRIDGE's domain divided by a count in the native build's, and that division
is the error this whole seam exists to prevent. Both sides key their tasks on
the same live counter instead:

  g_AudioFrameCount   incremented exactly once per handled audio frame, at the
                      last statement of amClearDmaBuffers (src/audi.c), and
                      read nowhere else as a writer. The cartridge census reads
                      it from RDRAM; the native capture writes it into each
                      record's header (SLNATV02).
  g_CurrentAcmdList   the command-list slot the task was built into, taken
                      BEFORE audi.c's post-dispatch `^= 1`.

So a task's identity is its audio-frame number, and the two populations are
aligned on that key. Video/pumped frame numbers are reported per side and are
never compared with each other.

  tools/native/acmdparity.py --cart /tmp/sl-acmd/frames.json \
                             --native /tmp/sl-native-acmd.bin

Exit status is 0 when the event domains align AND the overlapping tasks are
byte-identical; 1 otherwise. A mismatch is a finding to localize, never a
number to tune down.
"""
from __future__ import annotations

import argparse
import collections
import json
import pathlib
import struct
import sys

NAMES = {0: "SPNOOP", 1: "ADPCM", 2: "CLEARBUFF", 3: "ENVMIXER", 4: "LOADBUFF",
         5: "RESAMPLE", 6: "SAVEBUFF", 7: "SEGMENT", 8: "SETBUFF", 9: "SETVOL",
         10: "DMEMMOVE", 11: "LOADADPCM", 12: "MIXER", 13: "INTERLEAVE",
         14: "POLEF", 15: "SETLOOP"}

NATIVE_MAGIC = b"SLNATV02"

#: Opcodes whose w1 is a DRAM ADDRESS - exactly the ones whose decode carries an
#: "addr" field in acmd_census.decode(). These words CANNOT agree between the
#: two sides and that is not a defect: the cartridge's operand is an N64
#: physical RDRAM address and the native one is a host heap address. Excluding
#: them is a weakening of the criterion, so both the raw and the
#: address-independent results are always printed, and never only the latter.
#:
#: The same set is the whole of the run-to-run variation in the native capture:
#: with ASLR on, two runs differ in 136562 w1 words, ALL of them in these eight
#: opcodes and NONE in any w0. Under `setarch -R` two runs are byte-identical,
#: which is what makes "deterministic" a measurement here rather than a claim.
ADDR_OPS = {1, 3, 4, 5, 6, 11, 14, 15}   # ADPCM ENVMIXER LOADBUFF RESAMPLE
                                          # SAVEBUFF LOADADPCM POLEF SETLOOP


def load_native(path: pathlib.Path) -> list[dict]:
    """SLNATV02: magic, then per task u32 index, u32 ncmds, u32 audioFrame,
    u32 listSlot, then ncmds 8-byte commands.

    The command words are written straight out of the native Acmd list, so
    they are HOST word order - little-endian on this -m32 build - whereas the
    cartridge blob is read big-endian out of RDRAM. Both are decoded to
    integers here and compared as VALUES; comparing raw bytes would report a
    difference that is only the word order of the machine that wrote them.
    """
    b = path.read_bytes()
    if b[:8] != NATIVE_MAGIC:
        raise SystemExit(f"acmdparity: {path} is not {NATIVE_MAGIC.decode()} "
                         f"(got {b[:8]!r})")
    out, o = [], 8
    while o + 16 <= len(b):
        idx, n, af, slot = struct.unpack_from("<IIII", b, o)
        o += 16
        if o + n * 8 > len(b):
            raise SystemExit(f"acmdparity: truncated capture at task {idx}")
        cmds = []
        for i in range(n):
            w0, w1 = struct.unpack_from("<II", b, o + i * 8)
            cmds.append((w0, w1))
        o += n * 8
        out.append({"task": idx, "audio_frame": af, "list_index": slot,
                    "n": n, "cmds": cmds})
    return out


def load_cart(path: pathlib.Path) -> list[dict]:
    fr = json.loads(path.read_text())
    return [{"task": i, "audio_frame": f["audio_frame"],
             "list_index": f["list_index"], "n": f["n"],
             "frame": f["video_frame"],
             "cmds": [(c["w0"], c["w1"]) for c in f["cmds"]]}
            for i, f in enumerate(fr)]


def event_report(label: str, ts: list[dict]) -> dict:
    af = [t["audio_frame"] for t in ts]
    li = [t["list_index"] for t in ts]
    n = [t["n"] for t in ts]
    gaps = collections.Counter(af[i + 1] - af[i] for i in range(len(af) - 1))
    print(f"{label}")
    print(f"  ordered task population   {len(ts)}")
    if not ts:
        return {"af": af, "li": li, "n": n}
    print(f"  first task                audioFrame={af[0]} listSlot={li[0]} "
          f"cmds={n[0]}")
    print(f"  last  task                audioFrame={af[-1]} listSlot={li[-1]} "
          f"cmds={n[-1]}")
    print(f"  audio-frame sequence      {af[0]}..{af[-1]}  distinct={len(set(af))}"
          f"  strictly increasing={all(af[i] < af[i+1] for i in range(len(af)-1))}")
    print(f"  audio-frame gap histogram {dict(sorted(gaps.items()))}")
    print(f"  list-slot sequence        {li[:12]}...  slots={sorted(set(li))}  "
          f"alternates={all(li[i] != li[i+1] for i in range(len(li)-1))}")
    print(f"  commands/task             min={min(n)} max={max(n)} "
          f"total={sum(n)}")
    return {"af": af, "li": li, "n": n}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cart", required=True, type=pathlib.Path)
    ap.add_argument("--native", required=True, type=pathlib.Path)
    ap.add_argument("--max-report", type=int, default=8)
    a = ap.parse_args()

    cart = load_cart(a.cart)
    nat = load_native(a.native)

    print("=== STAGE 1: EVENT PARITY (no command word is compared yet) ===\n")
    c = event_report("CARTRIDGE", cart)
    print()
    n = event_report("NATIVE", nat)

    ok = True
    # The two structural predicates that make the domains comparable at all.
    # Neither is a count comparison: both are properties of one side's own
    # sequence, checked on each side separately.
    for label, d in (("cartridge", c), ("native", n)):
        if not d["af"]:
            print(f"\nFAIL: {label} produced no tasks - nothing is asserted.")
            return 1
        if set(collections.Counter(
                d["af"][i + 1] - d["af"][i]
                for i in range(len(d["af"]) - 1))) - {1}:
            print(f"\nFAIL: {label} audio-frame sequence has gaps; its own "
                  f"event domain is not contiguous.")
            ok = False

    cmap = {t["audio_frame"]: t for t in cart}
    nmap = {t["audio_frame"]: t for t in nat}
    both = sorted(set(cmap) & set(nmap))
    print(f"\nALIGNED ON audioFrame: cartridge {min(cmap)}..{max(cmap)}, "
          f"native {min(nmap)}..{max(nmap)}, overlap {len(both)} task(s)")
    if not both:
        print("STOP: the populations do not overlap on the derived event key. "
              "Localize the scheduler-event divergence before comparing any "
              "command content.")
        return 1

    print("\n=== STAGE 2: COMMAND STREAM PARITY over the aligned tasks ===\n")
    lens_bad = [af for af in both if cmap[af]["n"] != nmap[af]["n"]]
    slots_bad = [af for af in both
                 if cmap[af]["list_index"] != nmap[af]["list_index"]]
    print(f"  tasks compared            {len(both)}")
    print(f"  list-slot disagreements   {len(slots_bad)}")
    print(f"  command-count disagreements {len(lens_bad)}")
    for af in lens_bad[:a.max_report]:
        print(f"    audioFrame {af}: cartridge {cmap[af]['n']} cmds, "
              f"native {nmap[af]['n']} cmds")

    words = mism = 0
    ai_words = ai_mism = 0        # address-independent projection
    addr_words = 0
    first = first_ai = None
    op_c: collections.Counter = collections.Counter()
    op_n: collections.Counter = collections.Counter()
    tasks_diff = 0
    opseq_bad = []
    for af in both:
        cc, nc = cmap[af]["cmds"], nmap[af]["cmds"]
        for w0, _ in cc:
            op_c[NAMES.get((w0 >> 24) & 0xFF, "UNK")] += 1
        for w0, _ in nc:
            op_n[NAMES.get((w0 >> 24) & 0xFF, "UNK")] += 1
        if [c[0] >> 24 for c in cc] != [c[0] >> 24 for c in nc]:
            opseq_bad.append(af)
        d = False
        for i in range(min(len(cc), len(nc))):
            op = (cc[i][0] >> 24) & 0xFF
            for w in (0, 1):
                words += 1
                bad = cc[i][w] != nc[i][w]
                if bad:
                    mism += 1
                    d = True
                    if first is None:
                        first = (af, i, w, cc[i][w], nc[i][w],
                                 NAMES.get((cc[i][0] >> 24) & 0xFF, "UNK"),
                                 NAMES.get((nc[i][0] >> 24) & 0xFF, "UNK"))
                if w == 1 and op in ADDR_OPS:
                    addr_words += 1
                    continue
                ai_words += 1
                if bad:
                    ai_mism += 1
                    if first_ai is None:
                        first_ai = (af, i, w, cc[i][w], nc[i][w],
                                    NAMES.get((cc[i][0] >> 24) & 0xFF, "UNK"),
                                    NAMES.get((nc[i][0] >> 24) & 0xFF, "UNK"))
        if d or len(cc) != len(nc):
            tasks_diff += 1

    print(f"\n  opcode census cartridge   {dict(op_c.most_common())}")
    print(f"  opcode census native      {dict(op_n.most_common())}")
    same = {k: v for k, v in op_c.items() if op_n.get(k) == v}
    print(f"  opcodes with IDENTICAL total counts: {same}")

    print(f"\n  RAW comparison")
    print(f"    command words compared  {words}")
    print(f"    exact word mismatches   {mism}")
    if first:
        af, i, w, cv, nv, cn, nn = first
        print(f"    first difference        audioFrame={af} cmd={i} w{w}: "
              f"cartridge {cv:#010x} ({cn}) vs native {nv:#010x} ({nn})")
    print(f"\n  ADDRESS-INDEPENDENT comparison  (every w0, plus w1 for the "
          f"opcodes whose\n  w1 is not a DRAM address; an N64 physical address "
          f"cannot equal a host one)")
    print(f"    words excluded as addresses {addr_words}")
    print(f"    words compared          {ai_words}")
    print(f"    mismatches              {ai_mism}")
    if first_ai:
        af, i, w, cv, nv, cn, nn = first_ai
        print(f"    first difference        audioFrame={af} cmd={i} w{w}: "
              f"cartridge {cv:#010x} ({cn}) vs native {nv:#010x} ({nn})")
    # ---- ARENA-RELATIVE comparison ---------------------------------------
    # Both sides map segment 0 to base 0 (measured: every SEGMENT command on
    # both sides is (0,0)), so w1 IS the address and no segment base can absorb
    # a difference. A cartridge physical address and a host heap address can
    # therefore never be equal, and the strong criterion is unreachable until
    # the native audio buffers live at the cartridge's own addresses.
    #
    # This is the weaker criterion that IS available now: subtract each side's
    # own arena base and compare OFFSETS. It cannot see the arena being at the
    # wrong absolute address, nor a uniform shift of every buffer - that is
    # exactly what it gives up. It does see every RELATIVE layout error, which
    # is what a single constant delta would otherwise hide.
    deltas: collections.Counter = collections.Counter()
    strict = 0
    for af in both:
        cc, nc = cmap[af]["cmds"], nmap[af]["cmds"]
        if len(cc) != len(nc):
            continue
        if [c[0] >> 24 for c in cc] != [c[0] >> 24 for c in nc]:
            continue
        strict += 1
        for (w0, a), (_, b) in zip(cc, nc):
            if ((w0 >> 24) & 0xFF) in ADDR_OPS:
                deltas[(b - a) & 0xFFFFFFFF] += 1
    def sgn(v: int) -> str:
        v &= 0xFFFFFFFF
        return f"-{(1 << 32) - v:#x}" if v > 0x7FFFFFFF else f"+{v:#x}"

    if deltas:
        tot = sum(deltas.values())
        base, n0 = deltas.most_common(1)[0]
        exact = deltas.get(0, 0)
        print(f"\n  ADDRESS-OPERAND EQUALITY  (the acceptance test: the emitted "
              f"address IS\n  the cartridge's, which needs the arena at the "
              f"cartridge's own placement)")
        print(f"    address operands compared {tot}")
        print(f"    byte-identical            {exact} ({100.0 * exact / tot:.1f}%)")
        print(f"    differing                 {tot - exact}")
        print(f"\n  ARENA-RELATIVE  over {strict} task(s) whose command count and "
              f"opcode sequence agree exactly")
        print(f"    address operands            {tot}")
        print(f"    single dominant delta       {sgn(base)} on {n0} "
              f"({100.0 * n0 / tot:.1f}%) - the arena base offset")
        print(f"    operands NOT at that delta  {tot - n0}  "
              f"(genuine relative layout differences)")
        # EVERY delta, never a truncated list. A top-N view of this table
        # produced two wrong conclusions in a row - four collinear points read
        # as a constant stride, and a delta family read as a layout error -
        # because the tail is where the shape actually shows.
        for d, c_ in deltas.most_common()[1:]:
            print(f"      {sgn(d - base)} from the arena base: {c_} operand(s)")

    # SET / MULTISET / SEQUENCE, which is what distinguishes a LAYOUT
    # difference from a SELECTION difference. Positional pairing alone cannot:
    # if both sides use the same buffers in a different order, every pairing is
    # "wrong" and the deltas look like misplacement. Compare the address
    # collections directly instead.
    set_eq = ms_eq = seq_eq = 0
    for af in both:
        cc, nc = cmap[af]["cmds"], nmap[af]["cmds"]
        if len(cc) != len(nc):
            continue
        if [c[0] >> 24 for c in cc] != [c[0] >> 24 for c in nc]:
            continue
        ca = [w1 for w0, w1 in cc if ((w0 >> 24) & 0xFF) in ADDR_OPS]
        na = [w1 for w0, w1 in nc if ((w0 >> 24) & 0xFF) in ADDR_OPS]
        set_eq += set(ca) == set(na)
        ms_eq += collections.Counter(ca) == collections.Counter(na)
        seq_eq += ca == na
    if strict:
        print(f"\n  ADDRESS COLLECTIONS over the same {strict} task(s)")
        print(f"    identical SET       {set_eq}/{strict}   "
              f"(same buffers exist -> placement is right)")
        print(f"    identical MULTISET  {ms_eq}/{strict}   (same buffers, same counts)")
        print(f"    identical SEQUENCE  {seq_eq}/{strict}   (same buffers, same order)")
        if set_eq > seq_eq:
            print("    -> SET matches where SEQUENCE does not: a SELECTION/ordering "
                  "difference,\n       not a layout one. Deltas above are then "
                  "pairing artefacts, not misplacement.")

    print(f"\n  ordered OPCODE SEQUENCE per task differs in {len(opseq_bad)} "
          f"of {len(both)} tasks")
    if opseq_bad:
        af = opseq_bad[0]
        cc, nc = cmap[af]["cmds"], nmap[af]["cmds"]
        co = [(c[0] >> 24) & 0xFF for c in cc]
        no = [(c[0] >> 24) & 0xFF for c in nc]
        k = next((j for j in range(min(len(co), len(no))) if co[j] != no[j]),
                 min(len(co), len(no)))
        print(f"    first at audioFrame={af}, command {k}: "
              f"cartridge {[NAMES.get(o) for o in co[k:k+4]]} "
              f"vs native {[NAMES.get(o) for o in no[k:k+4]]}")
    print(f"  tasks differing           {tasks_diff} of {len(both)}")

    if mism or lens_bad or slots_bad:
        ok = False
    else:
        print("  BYTE-IDENTICAL over every aligned task.")

    print("\n" + ("PARITY: OK" if ok else "PARITY: MISMATCH - localize, "
                                          "do not tune"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
