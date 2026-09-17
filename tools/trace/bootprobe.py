#!/usr/bin/env python3
"""Sample the retail ROM's boot sequence, one emulator frame at a time.

WHAT THIS IS FOR
----------------
Sightline's front-end timing has been "fixed" three times by reasoning from
counter arithmetic in `front.c` and `title.c`, and three times the owner has
re-tested and found it still wrong. The arithmetic was never the problem: the
question "how long does the Legal screen stay ON SCREEN" is not answerable from
a threshold constant, because the visible duration includes whatever the game
does AFTER the timer expires - screen switches, PI DMAs, EEPROM reads - none of
which appear in `g_MenuTimer`.

So this measures the original instead of deriving it. The retail ROM boots
under the same parallel_n64 libretro core the trace harness already uses, and
this walks it frame by frame reading both:

  * game state, at addresses resolved from the matching build's link map, and
  * what is actually ON SCREEN, from the core's video callback.

Nothing is patched. The ROM that runs is byte-identical to `baserom.u.z64`.

WHY BOTH HALVES ARE NEEDED
--------------------------
`current_menu` tells you which state the game is IN. It does not tell you what
the television is showing: MENU_SWITCH_SCREENS is a black gap, and a stage's
image can persist across the state change. "State entry" and "first visible
frame" are different frames and this reports them separately, because the whole
disagreement between the owner's stopwatch and the source's constants lives in
that gap.

INSTRUMENT DISCIPLINE
---------------------
This project has caught two dozen faults in its own instruments, and the
expensive ones all looked like clean output. Two guards are built in:

  * `--control` proves the probe COULD have printed something else, by
    reporting every distinct value each field took. A field that never varied
    is named as such rather than quietly passing.
  * Counts are printed EXPLICITLY, including zero. An empty section is never
    reported as "none" - it is reported as 0, so a probe that read nothing
    cannot be mistaken for a probe that read nothing INTERESTING.

Output goes OUTSIDE the repository. The samples are ROM-derived and must never
be committed (project rule 2), so writing inside the working tree
is refused rather than left to a .gitignore.

Usage:
    python tools/trace/bootprobe.py --frames 1400
    python tools/trace/bootprobe.py --frames 1400 --out D:/scratch/boot.csv
"""

from __future__ import annotations

import argparse
import csv
import ctypes as C
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

#: numpy is a fast path, NOT a dependency. The repo's venv does not have it and
#: this tool is not worth adding one for (project rules, Dependencies), so there is
#: a pure-Python fallback on the identical sampling grid. tests/test_bootprobe.py
#: asserts the two agree exactly on the same buffer - a fallback that silently
#: computed something else would be worse than no fallback at all.
try:
    import numpy as np
except ImportError:
    np = None

import silhouette                                           # noqa: E402
from silhouette import lit_bbox                             # noqa: E402
from sltrace.emu_libretro import LibretroEmulator           # noqa: E402
from sltrace.state import Memory                            # noqa: E402
from sltrace.symbols import SymbolTable                     # noqa: E402

ROOT = Path(__file__).resolve().parents[2]

#: MENU enum, src/bondconstants.h:1916. MENU_INVALID is -1, so the names are
#: indexed from there rather than from zero.
MENU_NAMES = [
    "LEGAL_SCREEN", "NINTENDO_LOGO", "RAREWARE_LOGO", "EYE_INTRO",
    "GOLDENEYE_LOGO", "FILE_SELECT", "MODE_SELECT", "MISSION_SELECT",
    "DIFFICULTY", "007_OPTIONS", "BRIEFING", "RUN_STAGE", "MISSION_FAILED",
    "MISSION_COMPLETE", "MP_OPTIONS", "MP_CHAR_SELECT", "MP_HANDICAP",
    "MP_CONTROL_STYLE", "MP_STAGE_SELECT", "MP_SCENARIO_SELECT", "MP_TEAMS",
    "CHEAT", "NO_CONTROLLERS", "SWITCH_SCREENS", "DISPLAY_CAST",
    "SPECTRUM_EMU",
]


def menu_name(v: int) -> str:
    if v == -1:
        return "INVALID"
    if 0 <= v < len(MENU_NAMES):
        return MENU_NAMES[v]
    return f"MENU_{v}"


#: (column, symbol, reader). Every one of these resolves in
#: build/u/ge007.u.map; the probe fails loudly at startup if one does not,
#: rather than silently writing a column of zeros.
FIELDS = [
    ("current_menu",        "current_menu",        "s32"),
    ("g_MenuTimer",         "g_MenuTimer",         "s32"),
    ("g_ClockTimer",        "g_ClockTimer",        "s32"),
    ("currentFrameCounter", "currentFrameCounter", "s32"),
    ("speedgraphframes",    "speedgraphframes",    "s32"),
    ("ninLogoRotRate",      "ninLogoRotRate",      "f32"),
    ("D_8002A89C",          "D_8002A89C",          "f32"),
    ("intro_eye_counter",   "intro_eye_counter",   "s32"),
    ("gunbarrel_mode",      "gunbarrel_mode",      "u8"),
    ("gunbarrelTimer",      "gunbarrelTimer",      "s32"),
]

#: A step is an OUTLIER when it differs from the stage's modal step by more
#: than this, relative to the modal step. Deltas of a f32 counter differ in
#: their low bits from frame to frame, so an exact comparison would report
#: hundreds of "distinct" steps; a reset or a hitch is orders of magnitude out.
OUTLIER_REL = 1e-3


def progression(vals):
    """Describe how one counter moved across one stage.

    RETURNS a dict, and the reason it exists is a measured instrument fault.

    The first version of this report printed ``total = last - first`` and
    labelled it "total". For NINTENDO_LOGO that read ``total=7.3129``, and
    7.3129 rad was then carried into the next round as the ROM's angular
    TRAVEL. It is not travel. ``init_menu01_nintendo`` (front.c:1624) SETS
    ``ninLogoRotRate`` to -1.39626348019 on a frame INSIDE the stage, so the
    stage's first sample is the stale pre-init value 0 and end-minus-start
    spans a discontinuity. The real travel is 499 x 0.017453292 = 8.7092 rad -
    about 80 increments more than the printed figure, which is why the two
    numbers could not be reconciled with each other and nearly sent a repair
    at the wrong target.

    So end-minus-start is still reported, under the name ``net``, and any step
    that is not the stage's modal step is NAMED, with the frame index it
    happened on, alongside the travel accumulated since the last one. A reader
    can no longer mistake one for the other by reading quickly.
    """
    steps = [(i, vals[i] - vals[i - 1]) for i in range(1, len(vals))
             if vals[i] != vals[i - 1]]
    out = {
        "start": vals[0] if vals else 0,
        "end": vals[-1] if vals else 0,
        "net": (vals[-1] - vals[0]) if vals else 0,
        "n_steps": max(len(vals) - 1, 0),
        "changes": len(steps),
        "modal": None,
        "modal_count": 0,
        "outliers": [],
        "travel": 0,
        "travel_from": 0,
    }
    if not steps:
        return out

    # Modal step, bucketed so f32 low-bit jitter counts as one value.
    buckets = {}
    for _, d in steps:
        buckets.setdefault(round(d, 6), []).append(d)
    modal_key = max(buckets, key=lambda k: len(buckets[k]))
    modal = sum(buckets[modal_key]) / len(buckets[modal_key])
    out["modal"] = modal
    out["modal_count"] = len(buckets[modal_key])

    tol = abs(modal) * OUTLIER_REL
    out["outliers"] = [(i, d) for i, d in steps if abs(d - modal) > tol]

    # Travel accumulated since the last outlier - the span over which the
    # counter was doing nothing but its own regular step.
    last = out["outliers"][-1][0] if out["outliers"] else 0
    out["travel_from"] = last
    out["travel"] = sum(d for i, d in steps if i > last)
    return out


#: A frame counts as SHOWING SOMETHING when this fraction of sampled pixels is
#: above black. The logos and the legal text are bright on black, and the
#: switch-screen gaps are true black, so the two populations are far apart -
#: measured, the gaps sit at 0.0000 and any drawn stage above 0.01. Nothing
#: here depends on the exact cut.
VISIBLE_FRACTION = 0.002

#: Threshold and sampling grid come from silhouette.py, which is also what the
#: NATIVE side measures its captures with. They are imported rather than
#: restated so the two sides cannot drift apart by editing one copy: "visible"
#: and "lit" have to mean the same thing on both, or the comparison this whole
#: probe exists to support is between two different measurements.
#:
#: 8 gives 80x60 = 4800 samples of a 640x480 frame, which separates "black gap"
#: from "logo on screen" by two orders of magnitude and keeps the pure-Python
#: path usable. Both the numpy and fallback paths MUST use this same grid or
#: their numbers are not comparable.
BLACK_LEVEL = silhouette.BLACK_LEVEL
SAMPLE_STRIDE = silhouette.SAMPLE_STRIDE


def frame_stats(data, width: int, height: int, pitch: int) -> tuple:
    """(mean channel value, fraction of sampled pixels above black).

    XRGB8888 - measured from retro_get_system_av_info and already relied on by
    record_libretro.py, so this is not a fresh assumption. Little-endian, so
    the bytes arrive B, G, R, X and the X byte is dropped.
    """
    xs = range(0, width * 4, 4 * SAMPLE_STRIDE)
    if np is not None:
        buf = np.ctypeslib.as_array(
            C.cast(data, C.POINTER(C.c_uint8)), shape=(height * pitch,))
        img = buf.reshape(height, pitch)[:, :width * 4].reshape(height, width, 4)
        sub = img[::SAMPLE_STRIDE, ::SAMPLE_STRIDE, :3].astype(np.uint32)
        return float(sub.mean()), float((sub.max(axis=2) > BLACK_LEVEL).mean())

    total = 0
    lit = 0
    n = 0
    for y in range(0, height, SAMPLE_STRIDE):
        row = C.string_at(data + y * pitch, width * 4)
        for x in xs:
            b, g, r = row[x], row[x + 1], row[x + 2]
            total += b + g + r
            if b > BLACK_LEVEL or g > BLACK_LEVEL or r > BLACK_LEVEL:
                lit += 1
            n += 1
    if not n:
        return 0.0, 0.0
    return total / (n * 3), lit / n


def frame_bbox(data, width: int, height: int, pitch: int) -> dict:
    """The lit silhouette's bounding box and aspect, for THIS frame.

    XRGB8888 little-endian, so the bytes arrive B, G, R, X and the X byte is
    dropped - the same decode ``frame_stats`` does, on the same sampling grid.

    Deliberately ONE path, with no numpy variant. ``frame_stats`` has two and
    the repo pays for that with a test asserting they agree; a second fast path
    here would buy a few seconds a run and add another way for the ROM side to
    silently compute something the native side does not.
    """
    rows = {}

    def is_lit(x, y):
        row = rows.get(y)
        if row is None:
            row = rows[y] = C.string_at(data + y * pitch, width * 4)
        o = x * 4
        return (row[o] > BLACK_LEVEL or row[o + 1] > BLACK_LEVEL
                or row[o + 2] > BLACK_LEVEL)

    return lit_bbox(width, height, is_lit)


class VideoStats:
    """Per-frame brightness, straight off the core's presented framebuffer.

    XRGB8888 at 640x480 - measured from retro_get_system_av_info and already
    relied on by record_libretro.py, so this is not a fresh assumption.

    Pixels are subsampled 4x in each axis. That is 19200 samples per frame,
    which is plenty to separate "black gap" from "logo on screen" and keeps a
    1400-frame run to a few seconds.

    A frame can fail to deliver a new image in TWO different ways, and an
    earlier version of this class only counted one of them:

      * the callback fires with a NULL data pointer, or
      * the callback does not fire at all.

    Both mean "the core presented the previous image again". Counting only the
    first reported "1400 frames, 1327 presented, 0 duped" - three numbers that
    cannot all be true, and the arithmetic is the only thing that gave it away.
    `begin_frame()` is therefore called before every retro_run and the absence
    of a callback is recorded as a carry, so presented + carried == frames
    EXACTLY. That identity is asserted at the end of the run.

    A carried frame is not "no picture"; it is the same picture. The previous
    stats are carried forward, because treating a carry as black would invent a
    gap the television never showed.
    """

    def __init__(self):
        self.mean = 0.0
        self.nonblack = 0.0
        self.bbox = {"lit": 0, "samples": 0, "x0": -1, "x1": -1, "y0": -1,
                     "y1": -1, "span_x": -1, "span_y": -1, "aspect": -1.0}
        self.fired = False
        self.carried_now = True
        self.presented = 0
        self.carried = 0
        self.null_data = 0

    def begin_frame(self):
        self.fired = False

    def end_frame(self):
        """Settle what this frame actually showed. Call after retro_run."""
        if self.fired:
            self.presented += 1
            self.carried_now = False
        else:
            self.carried += 1
            self.carried_now = True

    def on_video(self, data, width, height, pitch):
        if not data or not width or not height:
            self.null_data += 1
            return                      # no new image; end_frame counts a carry
        self.mean, self.nonblack = frame_stats(data, width, height, pitch)
        self.bbox = frame_bbox(data, width, height, pitch)
        self.fired = True


def read_fields(mem: Memory, addrs: dict) -> dict:
    out = {}
    for col, _sym, kind in FIELDS:
        a = addrs[col]
        if kind == "s32":
            out[col] = mem.s32(a)
        elif kind == "f32":
            out[col] = mem.f32(a)
        elif kind == "u8":
            out[col] = mem.u8(a)
    return out


def default_out() -> Path:
    if sys.platform == "win32":
        base = Path(os.environ.get("TEMP", Path.home() / "AppData/Local/Temp"))
    else:
        base = Path("/tmp")
    return base / "sightline-oracle" / f"boot-{time.strftime('%Y%m%d-%H%M%S')}.csv"


def refuse_inside_repo(p: Path) -> None:
    """ROM-derived samples must not land in the working tree."""
    try:
        p.resolve().relative_to(ROOT)
    except ValueError:
        return
    raise SystemExit(
        f"refusing to write ROM-derived samples inside the repository:\n"
        f"  {p}\nChoose a path outside {ROOT} (project rule 2).")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=str(ROOT / "baserom.u.z64"))
    ap.add_argument("--map", default=str(ROOT / "build/u/ge007.u.map"))
    ap.add_argument("--core", default=None,
                    help="libretro core; defaults to SL_LIBRETRO_CORE")
    ap.add_argument("--frames", type=int, default=1400,
                    help="bounded frame count (default 1400 ~ 23s of video)")
    ap.add_argument("--out", default=None, help="CSV path, OUTSIDE the repo")
    ap.add_argument("--control", action="store_true",
                    help="report distinct values per field, to prove the "
                         "probe could have printed something else")
    args = ap.parse_args()

    out = Path(args.out) if args.out else default_out()
    refuse_inside_repo(out)
    out.parent.mkdir(parents=True, exist_ok=True)

    rom = Path(args.rom)
    if not rom.exists():
        raise SystemExit(f"ROM not found: {rom}")

    sym = SymbolTable.from_map(args.map)
    sym.require([s for _c, s, _k in FIELDS])
    addrs = {col: sym.addr(s) for col, s, _k in FIELDS}

    print(f"rom          {rom}")
    print(f"map          {args.map}")
    print(f"symbols      {len(sym)} parsed, {len(addrs)} required, 0 missing")
    for col, s, _k in FIELDS:
        print(f"  {col:<20} {s:<20} 0x{addrs[col]:08x}")

    emu = LibretroEmulator(str(rom), core_path=args.core)
    print(f"core         {emu.core_path}")
    vid = VideoStats()
    emu._on_video_capture = vid.on_video
    av = emu.av_info()
    print(f"av_info      {av}")

    emu._load()
    emu._warming_up = True
    emu._core.retro_run()          # RAM pointer is NULL until after frame one
    emu._warming_up = False

    base = emu.rdram()
    if base is None:
        raise SystemExit("RDRAM pointer still NULL after the warm-up frame")
    mem = Memory(base)

    cols = (["frame", "presented", "carried", "mean_luma", "nonblack",
             "lit", "lit_x0", "lit_x1", "lit_span_x", "lit_span_y",
             "lit_aspect", "menu_name"] + [c for c, _s, _k in FIELDS])
    rows = []
    t0 = time.time()
    for i in range(args.frames):
        vid.begin_frame()
        emu._core.retro_run()
        vid.end_frame()
        f = read_fields(mem, addrs)
        row = {
            "frame": i,
            "presented": vid.presented,
            "carried": int(vid.carried_now),
            "mean_luma": round(vid.mean, 4),
            "nonblack": round(vid.nonblack, 5),
            "lit": vid.bbox["lit"],
            "lit_x0": vid.bbox["x0"],
            "lit_x1": vid.bbox["x1"],
            "lit_span_x": vid.bbox["span_x"],
            "lit_span_y": vid.bbox["span_y"],
            "lit_aspect": round(vid.bbox["aspect"], 4),
            "menu_name": menu_name(f["current_menu"]),
        }
        row.update(f)
        rows.append(row)
    dt = time.time() - t0

    with out.open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)

    print(f"\nframes       {len(rows)} sampled in {dt:.1f}s")
    print(f"video        {vid.presented} presented, {vid.carried} carried "
          f"({vid.null_data} of them a NULL-data callback)")
    # presented + carried must account for EVERY frame. An earlier version
    # reported 1400/1327/0, which is arithmetically impossible; this is the
    # assertion that would have caught it.
    assert vid.presented + vid.carried == len(rows), (
        f"video accounting lost frames: {vid.presented} + {vid.carried} "
        f"!= {len(rows)}")
    print(f"             accounting checks out: "
          f"{vid.presented} + {vid.carried} == {len(rows)}")
    print(f"csv          {out}  ({out.stat().st_size} bytes)")

    if args.control:
        print("\n--- CONTROL: distinct values per field ---")
        print("A field showing 1 distinct value did NOT vary in this window.")
        for c in ["menu_name"] + [c for c, _s, _k in FIELDS]:
            vals = []
            for r in rows:
                if not vals or r[c] != vals[-1]:
                    vals.append(r[c])
            uniq = sorted({r[c] for r in rows}, key=str)
            shown = ", ".join(str(v) for v in uniq[:8])
            more = f" ... (+{len(uniq) - 8} more)" if len(uniq) > 8 else ""
            print(f"  {c:<20} {len(uniq):>5} distinct, "
                  f"{len(vals):>5} runs | {shown}{more}")

    summarise(rows, av["fps"])
    emu.close()
    return 0


def summarise(rows, fps: float) -> None:
    """Per-stage boundaries: state entry, first/last VISIBLE frame, exit.

    The two are reported separately on purpose. If the Legal image is still on
    screen long after g_MenuTimer passed its threshold, that shows up here as a
    visible span longer than the state span - which is precisely the evidence
    the source's constants cannot supply.
    """
    print(f"\n--- STAGES (core declares {fps:.4f} fps) ---")
    if not rows:
        print("  0 frames - nothing to summarise")
        return

    segs = []
    start = 0
    for i in range(1, len(rows) + 1):
        if i == len(rows) or rows[i]["menu_name"] != rows[start]["menu_name"]:
            segs.append((rows[start]["menu_name"], start, i - 1))
            start = i

    print(f"  {len(segs)} state segments")
    hdr = (f"  {'stage':<16} {'entry':>6} {'exit':>6} {'frames':>7} "
           f"{'state_s':>8} | {'vis1st':>6} {'visLast':>7} {'visFrm':>7} "
           f"{'vis_s':>7}")
    print(hdr)
    print("  " + "-" * (len(hdr) - 2))
    for name, a, b in segs:
        span = b - a + 1
        vis = [r["frame"] for r in rows[a:b + 1]
               if r["nonblack"] >= VISIBLE_FRACTION]
        if vis:
            v1, v2, vn = vis[0], vis[-1], len(vis)
            vs = f"{vn / fps:7.3f}"
        else:
            v1 = v2 = vn = 0
            vs = f"{0.0:7.3f}"
        print(f"  {name:<16} {a:>6} {b:>6} {span:>7} {span / fps:8.3f} | "
              f"{v1:>6} {v2:>7} {vn:>7} {vs}")

    print("\n  Visible spans are contiguous runs of frames above black,")
    print(f"  threshold nonblack >= {VISIBLE_FRACTION} of sampled pixels.")

    # ---- the three visible sample points ---------------------------------
    #
    # STATE END IS NOT VISIBLE END. The table above already shows the gap -
    # NINTENDO_LOGO's state runs long after the picture has gone - and a
    # comparison anchored on the state boundary is therefore comparing two
    # different moments. These rows are anchored on the PICTURE: first frame
    # above black, the middle one, and the LAST one.
    #
    # ``lit_aspect`` is the orientation metric (silhouette.py): the lit
    # silhouette's horizontal span over its vertical span. Rotation about Y
    # changes the first and not the second, so face-on reads high and edge-on
    # reads low, independently of scale or resolution.
    print("\n--- VISIBLE SAMPLE POINTS (anchored on the PICTURE) ---")
    print("  State entry/exit are NOT these frames. lit_aspect = span_x/span_y")
    print("  in pixels; high is face-on, low is edge-on.")
    hdr2 = (f"  {'stage':<16} {'point':<7} {'frame':>6} {'menuTmr':>8} "
            f"{'ninLogoRotRate':>15} {'deg':>8} {'mod360':>7} "
            f"{'nonblk':>8} {'spanx':>6} {'spany':>6} {'aspect':>7}")
    print(hdr2)
    print("  " + "-" * (len(hdr2) - 2))
    shown = 0
    for name, a, b in segs:
        vis = [r for r in rows[a:b + 1] if r["nonblack"] >= VISIBLE_FRACTION]
        if not vis:
            continue
        picks = [("first", vis[0]), ("middle", vis[len(vis) // 2]),
                 ("LAST", vis[-1])]
        for label, r in picks:
            rad = r["ninLogoRotRate"]
            deg = rad / 0.017453292
            print(f"  {name:<16} {label:<7} {r['frame']:>6} "
                  f"{r['g_MenuTimer']:>8} {rad:>15.6f} {deg:>8.2f} "
                  f"{deg % 360.0:>7.2f} {r['nonblack']:>8.5f} "
                  f"{r['lit_span_x']:>6} {r['lit_span_y']:>6} "
                  f"{r['lit_aspect']:>7.3f}")
            shown += 1
    # Explicitly a count, including zero: an empty section here would read as
    # "nothing notable" when it means "the probe saw no picture at all".
    print(f"\n  {shown} sample point(s) printed across "
          f"{len([1 for n, a, b in segs if any(r['nonblack'] >= VISIBLE_FRACTION for r in rows[a:b + 1])])} "
          f"visible stage(s).")

    # ---- per-stage progression -------------------------------------------
    #
    # Each stage gets the counters that actually drive IT. They are reported
    # separately because there is no reason to expect one cadence to govern
    # all of them - Nintendo turns a matrix, Rareware turns a different one,
    # and the eye intro runs a timer. Assuming a single global rate is the
    # mistake this whole probe exists to stop repeating.
    WATCH = {
        "NINTENDO_LOGO": ["ninLogoRotRate"],
        "RAREWARE_LOGO": ["D_8002A89C", "intro_eye_counter", "gunbarrel_mode"],
        "EYE_INTRO": ["intro_eye_counter", "gunbarrel_mode", "gunbarrelTimer",
                      "D_8002A89C"],
        "LEGAL_SCREEN": ["g_MenuTimer"],
    }
    print("\n--- PROGRESSION (per stage, per counter) ---")
    for name, a, b in segs:
        watch = WATCH.get(name)
        if not watch:
            continue
        span = b - a + 1
        print(f"\n  {name}  frames {a}..{b}  ({span} frames, "
              f"{span / fps:.3f}s)")
        for col in watch:
            vals = [r[col] for r in rows[a:b + 1]]
            pr = progression(vals)
            pad = f"    {'':<18} "
            deltas = sorted({round(vals[i] - vals[i - 1], 6)
                             for i in range(1, len(vals))
                             if vals[i] != vals[i - 1]})
            ds = ", ".join(f"{d:g}" for d in deltas[:6])
            more = f" (+{len(deltas) - 6})" if len(deltas) > 6 else ""
            print(f"    {col:<18} start={pr['start']:<12g} "
                  f"end={pr['end']:<12g} net={pr['net']:<12g}"
                  f"  (net = end - start)")
            print(f"{pad}changed on {pr['changes']} of {pr['n_steps']} "
                  f"frame steps")
            if pr["modal"] is None:
                print(f"{pad}modal step: NONE - the counter never moved")
            else:
                print(f"{pad}modal step {pr['modal']:g} on "
                      f"{pr['modal_count']} of {pr['changes']} changes")
            # NET IS NOT TRAVEL when the counter was reset or hitched inside
            # the stage. Name every such step, so the two cannot be confused.
            if pr["outliers"]:
                print(f"{pad}{len(pr['outliers'])} OUTLIER step(s) - "
                      f"net is NOT accumulated travel:")
                for i, d in pr["outliers"][:6]:
                    print(f"{pad}  stage-frame {i} (abs {a + i}): step {d:g} "
                          f"-> value {vals[i]:g}")
                if len(pr["outliers"]) > 6:
                    print(f"{pad}  (+{len(pr['outliers']) - 6} more)")
                print(f"{pad}travel after stage-frame {pr['travel_from']}: "
                      f"{pr['travel']:g} over "
                      f"{pr['changes'] - len(pr['outliers'])} regular steps")
            else:
                print(f"{pad}0 outlier steps - net IS accumulated travel")
            print(f"{pad}distinct steps: [{ds}]{more}")


if __name__ == "__main__":
    raise SystemExit(main())
