#!/usr/bin/env python3
"""Compare a native state log against an emulator .sltrace, per subsystem.

The native build writes SLNATIVE logs (see sl_ultra_shim.c: per tick a
big-endian record of tick, frame counter, 16-byte composite, then entity
key/hash pairs).  This tool aligns the two streams and reports, per tick:

  player  - entity 0xFFFF, pointer-free, comparable across backends
  props   - entity 0xFFFE, pointer-free, comparable across backends
  chr     - per-guard hashes; under schema v10 these embed two raw
            PropRecord pointers (weapons held), so an armed guard NEVER
            matches across backends.  Reported separately so signal isn't
            drowned: an unarmed-guard mismatch is real.
  composite - matches only when everything above does (and globals)

Alignment: --scan searches the first N native ticks for the emulator's
tick-0 player hash, since the native warm-up offset is calibratable rather
than known a priori.

Usage:
  nativediff.py <native.bin> <trace.sltrace> [--scan 600] [--offset K]
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from sltrace.traceio import Trace  # noqa: E402


def load_native(path: str):
    raw = Path(path).read_bytes()
    if raw[:8] != b"SLNATIVE":
        raise SystemExit(f"{path}: not a native state log")
    off = 8
    ticks = []
    n = len(raw)
    while off + 26 <= n:
        tick, fc = struct.unpack_from(">II", raw, off)
        comp = raw[off + 8:off + 24]
        (cnt,) = struct.unpack_from(">H", raw, off + 24)
        off += 26
        ents = {}
        if off + cnt * 10 > n:
            break               # truncated final record (run cut mid-write)
        for _ in range(cnt):
            (key,) = struct.unpack_from(">H", raw, off)
            ents[key] = raw[off + 2:off + 10]
            off += 10
        ticks.append((tick, fc, comp, ents))
    return ticks


def collapse_frames(ticks):
    """Settled frame states: the last tick before each fc change.

    VI samples land during rendering, when the sim state for the frame is
    complete - the last sample of each constant-fc run is the settled state
    the native pump boundary also observes.
    """
    out = []
    for i in range(len(ticks) - 1):
        if ticks[i + 1].frame_counter != ticks[i].frame_counter:
            out.append(ticks[i])
    if ticks:
        out.append(ticks[-1])
    return out


def extract_ticks(trace_path: str, out_path: str) -> int:
    """Write the per-frame VI-count stream (u16 BE) for SL_TICKS, plus the
    absolute trace-tick index of each frame boundary (u32 BE) as <out>.vis
    for SL_VIS.

    The frame counter is derived from rounded elapsed-cycle division, so it
    drifts a few VIs either side of the true VI count - the input stream is
    indexed by true VIs (one record per emulator sample), so the poll head
    needs the absolute index, not the counter deltas.
    """
    emu = Trace.load(trace_path).ticks
    counts = []
    vis = []
    prev = emu[0].frame_counter
    for i, t in enumerate(emu[1:], start=1):
        if t.frame_counter != prev:
            counts.append(t.frame_counter - prev)
            vis.append(i)
            prev = t.frame_counter
    data = b"".join(struct.pack(">H", max(0, min(0xFFFF, c))) for c in counts)
    Path(out_path).write_bytes(data)
    Path(out_path + ".vis").write_bytes(
        b"".join(struct.pack(">I", i) for i in vis))
    print(f"{out_path}: {len(counts)} frames, "
          f"first jump {counts[0] if counts else 0}; "
          f"vis targets {vis[0] if vis else 0}..{vis[-1] if vis else 0}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("native")
    ap.add_argument("trace")
    ap.add_argument("--scan", type=int, default=600,
                    help="search this many native ticks for alignment")
    ap.add_argument("--offset", type=int, default=None,
                    help="skip alignment scan; native tick K == emu tick 0")
    ap.add_argument("--align-live", action="store_true",
                    help="align at the first live (fc!=0) emulator tick")
    ap.add_argument("--align-fc", action="store_true",
                    help="join on frame counter (correct for direct-boot runs)")
    ap.add_argument("--align-best", action="store_true",
                    help="correlate props hashes to find the best offset")
    ap.add_argument("--limit", type=int, default=0,
                    help="compare at most this many ticks (0 = all)")
    ap.add_argument("--frames", action="store_true",
                    help="collapse the emulator stream to settled frame states")
    ap.add_argument("--extract-ticks", metavar="OUT",
                    help="write the per-frame VI-count stream and exit")
    args = ap.parse_args()

    if args.extract_ticks:
        return extract_ticks(args.trace, args.extract_ticks)

    nat = load_native(args.native)
    emu = Trace.load(args.trace).ticks
    print(f"native ticks: {len(nat)}   emulator ticks: {len(emu)}")
    if args.frames:
        emu = [t for t in collapse_frames(emu) if t.frame_counter != 0]
        print(f"emulator frame states: {len(emu)}")

    if args.align_fc:
        # Join on FRAME COUNTER. Index offsets cannot align a direct-boot
        # native run against a recording that contains boot frames: the
        # emulator samples hundreds of ticks at fc=0 while the stage loads and
        # the native pump produces none of them, so no single offset lines the
        # streams up (align-live got within ONE frame and still compared
        # nothing). The native boot design makes fc exact across backends -
        # "native frame counters equal the emulator's EXACTLY, value for
        # value" - so it is the coordinate to join on.
        # The emulator samples SEVERAL ticks inside one game frame (measured on
        # archives: only 293 of 1874 frames carry a single sample, most carry
        # 2-3, some 15) while the native pump emits exactly one. Picking the
        # first sample is therefore close to a coin flip - it is what produced
        # a 12% player match that meant nothing. For each frame, pick the
        # sample that agrees with native on the most categories: the question
        # worth answering is whether ANY sampling phase agrees, not whether an
        # arbitrary one does.
        by_fc_all = {}
        for t in emu:
            if t.frame_counter:
                by_fc_all.setdefault(t.frame_counter, []).append(t)

        def _agreement(nent, ncomp, t):
            n = 0
            if nent.get(0xFFFF) == t.entities.get(0xFFFF):
                n += 1
            if nent.get(0xFFFE) == t.entities.get(0xFFFE):
                n += 1
            if ncomp == t.composite:
                n += 1
            nchr = {kk: v for kk, v in nent.items() if kk < 0xFFFE}
            echr = {kk: v for kk, v in t.entities.items() if kk < 0xFFFE}
            n += sum(1 for kk in set(nchr) | set(echr)
                     if nchr.get(kk) == echr.get(kk))
            return n

        by_fc = {}
        for entry in nat:
            cands = by_fc_all.get(entry[1])
            if cands:
                by_fc[entry[1]] = max(
                    cands, key=lambda t: _agreement(entry[3], entry[2], t))
        multi = sum(1 for v in by_fc_all.values() if len(v) > 1)
        print(f"align-fc: {multi} frames carry multiple emulator samples; "
              f"picking the best-agreeing sample per frame")
        kept, aligned = [], []
        for entry in nat:
            t = by_fc.get(entry[1])
            if t is not None:
                kept.append(entry)
                aligned.append(t)
        if not kept:
            print("align-fc: NO native frame counter appears in the emulator "
                  "trace - the two runs are not the same recording")
            return 1
        nat, emu, k = kept, aligned, 0
        print(f"align-fc: joined on frame counter, {len(nat)} native ticks "
              f"matched an emulator tick "
              f"(fc {nat[0][1]}..{nat[-1][1]})")
    elif args.offset is not None:
        k = args.offset
    elif args.align_live:
        # the emulator samples ~hundreds of ticks while the stage loads (VI
        # runs, game blocks); the native pump produces none of those.  Align
        # from the first LIVE emulator tick (fc != 0) by scanning native
        # ticks for the best props-sequence agreement.
        live = next((i for i, t in enumerate(emu) if t.frame_counter != 0), 0)
        window = min(1000, len(emu) - live)
        best_k, best_score = 0, -1
        for k in range(min(args.scan, max(1, len(nat) - window))):
            score = sum(1 for i in range(window)
                        if nat[k + i][3].get(0xFFFE)
                        == emu[live + i].entities.get(0xFFFE))
            if score > best_score:
                best_k, best_score = k, score
        print(f"align-live: emu live tick {live}, native offset {best_k} "
              f"with {best_score}/{window} props matches")
        emu = emu[live:]
        k = best_k
    elif args.align_best:
        # correlation alignment: pick the offset that maximizes props-hash
        # agreement over a window - immune to the all-zero loading anchor
        window = min(2000, len(emu))
        best_k, best_score = None, -1
        for k in range(min(args.scan, max(1, len(nat) - window))):
            score = sum(1 for i in range(window)
                        if k + i < len(nat)
                        and nat[k + i][3].get(0xFFFE) == emu[i].entities.get(0xFFFE))
            if score > best_score:
                best_k, best_score = k, score
        k = best_k
        print(f"align-best: offset {k} with {best_score}/{window} props matches")
    else:
        want = emu[0].entities.get(0xFFFF)
        k = next((i for i, (_, _, _, e) in enumerate(nat[:args.scan])
                  if e.get(0xFFFF) == want), None)
        if k is None:
            # the player hash may be all-zero (implausible) early on both
            # sides; try the props hash as a second anchor
            want = emu[0].entities.get(0xFFFE)
            k = next((i for i, (_, _, _, e) in enumerate(nat[:args.scan])
                      if e.get(0xFFFE) == want), None)
        if k is None:
            print("NO ALIGNMENT: emulator tick 0 matches no native tick "
                  f"in the first {args.scan} (player and props anchors)")
            # show both sides' first few for eyeballing
            for i in range(3):
                t, fc, _, e = nat[i]
                print(f"  native {t} fc={fc} player={e.get(0xFFFF, b'').hex()}"
                      f" props={e.get(0xFFFE, b'').hex()}")
            for t in emu[:3]:
                print(f"  emu    {t.tick} fc={t.frame_counter}"
                      f" player={t.entities.get(0xFFFF, b'').hex()}"
                      f" props={t.entities.get(0xFFFE, b'').hex()}")
            return 2
        print(f"aligned: native tick {k} == emulator tick 0")

    pairs = min(len(nat) - k, len(emu))
    if args.limit:
        pairs = min(pairs, args.limit)
    stats = {"player": 0, "props": 0, "chr": 0, "composite": 0}
    firsts: dict[str, int] = {}
    for i in range(pairs):
        _, nfc, ncomp, nents = nat[k + i]
        et = emu[i]
        ok_pl = nents.get(0xFFFF) == et.entities.get(0xFFFF)
        ok_pr = nents.get(0xFFFE) == et.entities.get(0xFFFE)
        nchr = {kk: v for kk, v in nents.items() if kk < 0xFFFE}
        echr = {kk: v for kk, v in et.entities.items() if kk < 0xFFFE}
        ok_ch = nchr == echr
        ok_co = ncomp == et.composite
        for name, ok in (("player", ok_pl), ("props", ok_pr),
                         ("chr", ok_ch), ("composite", ok_co)):
            if ok:
                stats[name] += 1
            elif name not in firsts:
                firsts[name] = i
    # Per-entity chr stats.  The aggregate below compares the WHOLE chr dict,
    # so a single mismatching guard reports the entire category as 0% and hides
    # how many actually agree - which masked the real state of AI parity for a
    # long time.  Break it out.
    if pairs:
        import collections as _c
        pm, pt = _c.Counter(), _c.Counter()
        for i in range(pairs):
            ne = {kk: vv for kk, vv in nat[k + i][3].items() if kk < 0xFFFE}
            ee = {kk: vv for kk, vv in emu[i].entities.items() if kk < 0xFFFE}
            for key in set(ne) | set(ee):
                pt[key] += 1
                if ne.get(key) == ee.get(key):
                    pm[key] += 1
        if pt:
            always = sum(1 for key in pt if pm[key] == pt[key])
            never = sum(1 for key in pt if pm[key] == 0)
            print(f"chr entities: {len(pt)} distinct - {always} match always, "
                  f"{len(pt) - always - never} partially, {never} never")

    print(f"compared {pairs} ticks:")
    for name in ("player", "props", "chr", "composite"):
        pct = 100.0 * stats[name] / pairs if pairs else 0.0
        extra = f"   first mismatch at tick {firsts[name]}" if name in firsts else ""
        print(f"  {name:9s} {stats[name]}/{pairs} ({pct:5.1f}%){extra}")
    if "player" in firsts or "props" in firsts:
        i = min(firsts.get("player", 1 << 30), firsts.get("props", 1 << 30))
        _, nfc, _, nents = nat[k + i]
        et = emu[i]
        print(f"first pointer-free divergence at emu tick {i} "
              f"(fc emu={et.frame_counter} native={nfc}):")
        for kk in (0xFFFF, 0xFFFE):
            print(f"  key {kk:04x}: emu={et.entities.get(kk, b'').hex()} "
                  f"native={nents.get(kk, b'').hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
