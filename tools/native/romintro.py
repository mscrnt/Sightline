#!/usr/bin/env python3
"""ROM oracle for a level INTRO: what the cartridge draws, and where its camera is.

    .venv\\Scripts\\python.exe tools/native/romintro.py --stage 20 \\
        --first 1200 --last 1900 --every 3 --out %TEMP%\\sightline-oracle\\silo-intro

Boots the UNMODIFIED cartridge ROM under the parallel_n64 libretro core (the
same core rom-reference.ps1, romweapon.py and the trace harness use), lands it
straight in a stage by the romweapon.py mechanism (g_StageNum and
g_SelectedDifficulty written for the first --boot-pokes frames, boss.c:95),
holds the pad neutral so the intro runs untouched, and saves every --every'th
frame of the window [--first, --last]. Alongside the frames it writes
camera.log with one line PER TICK:

    f<tick> cam=<g_CameraMode> timer=<camera_transition_timer> eye=x,y,z
           fovy=.. aspect=.. znear=.. zfar=..

eye is player->viewtoworldmtxf translation (bondview.h offset 0x10d4, m[3]),
the same value the native probe prints as `eye=` under SL_MISSION_EVERY, so a
native frame and a cartridge frame are paired by CAMERA POSITION rather than
by frame index - the two sides do not share a VI count. The projection fields
are g_ViBackData (fr.h VideoSettings: fovy +8, aspect +0xc, znear +0x10,
zfar +0x14), the parameters guPerspectiveF is handed at fr.c:709.

WHY. An intro-camera report ("the camera clips into the wall during the
spin", Silo 2026-09-14) needs the cartridge's own drawing at the same camera
position before any native stage is blamed. The recorded streams under
tools/trace/inputs cannot supply it: they answer direct-boot ROM images the
Windows tree does not build. Frames are ROM-derived: they go OUTSIDE the
repository and are never committed; the script refuses a path inside it.

The core is an EXTERNAL dependency: see tools/windows/rom-reference.ps1 for
where it lives and how to fetch one. 64-bit Python for a win64 DLL.
"""
import argparse
import ctypes as C
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/trace"))
from sltrace.emu_libretro import LibretroEmulator, InputFrame      # noqa: E402
from sltrace.state import Memory                                   # noqa: E402
from sltrace.symbols import SymbolTable                            # noqa: E402

PLAYER_VIEWTOWORLDMTXF = 0x10D4   # bondview.h: Mtxf *viewtoworldmtxf
MTXF_TRANSLATION = 0x30           # m[3][0..2] of a row-major 4x4 of f32
PLAYER_BOND_POS = 0x3C4           # bondview.h: field_3C4/3C8/3CC, the swirl's origin
PLAYER_THETA_TRANSFORM = 0x498    # bondview.h: field_488 (collision434) + 0x10 theta_transform
PLAYER_TRUE_POS = 0x4B4           # bondview.h: field_488 + 0x2c pos, the position bond= is a low-pass of
VI_FOVY, VI_ASPECT, VI_ZNEAR, VI_ZFAR = 0x8, 0xC, 0x10, 0x14   # fr.h VideoSettings


def refuse_inside_repo(p: pathlib.Path) -> None:
    try:
        p.resolve().relative_to(ROOT)
    except ValueError:
        return
    print(f"romintro: refusing to write ROM-derived frames inside the "
          f"repository: {p}", file=sys.stderr)
    sys.exit(2)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", type=int, required=True, help="LEVELID number (20 = Silo)")
    ap.add_argument("--difficulty", type=int, default=1)
    ap.add_argument("--out", required=True)
    ap.add_argument("--rom", default=os.environ.get("SL_ROM") or str(ROOT / "baserom.u.z64"))
    ap.add_argument("--core", default=None)
    ap.add_argument("--frames", type=int, default=2400, help="hard bound on ticks")
    ap.add_argument("--boot-pokes", type=int, default=240, dest="boot_pokes")
    ap.add_argument("--first", type=int, default=600, help="first tick to save")
    ap.add_argument("--last", type=int, default=2000, help="last tick to save; the run stops after it")
    ap.add_argument("--every", type=int, default=4)
    a = ap.parse_args()

    out = pathlib.Path(a.out)
    refuse_inside_repo(out)
    out.mkdir(parents=True, exist_ok=True)

    sym = SymbolTable.from_map(ROOT / "build/u/ge007.u.map")
    A_PLAYER = sym.addr("g_CurrentPlayer")
    A_CAM = sym.addr("g_CameraMode")
    A_STAGE = sym.addr("g_StageNum")
    A_DIFF = sym.addr("g_SelectedDifficulty")
    A_LOAD = sym.addr("g_CurrentStageToLoad")
    A_TIMER = sym.addr("camera_transition_timer")
    A_INDEX = sym.addr("intro_camera_index")
    A_VI = sym.addr("g_ViBackData")

    emu = LibretroEmulator(a.rom, core_path=a.core, input_stream=None, eeprom=None)
    fb = {"d": None, "w": 0, "h": 0, "pitch": 0}

    def on_video(d, w, h, pitch):
        fb["d"], fb["w"], fb["h"], fb["pitch"] = d, w, h, pitch
    emu._on_video_capture = on_video
    emu._frames = [InputFrame(0)] * a.frames          # neutral pad throughout

    t = {"last_load": None, "last_cam": None, "saved": 0}
    log = open(out / "camera.log", "w")

    def save(index):
        w, h, pitch = fb["w"], fb["h"], fb["pitch"]
        raw = C.string_at(fb["d"], pitch * h)
        rows = []
        for y in range(h):
            # XRGB8888 little-endian: memory order is B,G,R,X (romshot.py).
            line = raw[y * pitch: y * pitch + w * 4]
            row = bytearray(w * 3)
            row[0::3] = line[2::4]
            row[1::3] = line[1::4]
            row[2::3] = line[0::4]
            rows.append(bytes(row))
        p = out / f"rom-{index:05d}.ppm"
        with open(p, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            f.write(b"".join(rows))
        t["saved"] += 1

    def on_tick(index, fc, ram):
        mem = Memory(ram)
        if index < a.boot_pokes:
            mem.w32(A_STAGE, a.stage)
            mem.w32(A_DIFF, a.difficulty)
        load, cam = mem.s32(A_LOAD), mem.s32(A_CAM)
        if (load, cam) != (t["last_load"], t["last_cam"]):
            print(f"tick {index}: stage-to-load {load} camera-mode {cam}")
            t["last_load"], t["last_cam"] = load, cam
        p = mem.u32(A_PLAYER)
        if mem.valid_ptr(p):
            m = mem.u32(p + PLAYER_VIEWTOWORLDMTXF)
            if mem.valid_ptr(m):
                e = [mem.f32(m + MTXF_TRANSLATION + 4 * i) for i in range(3)]
                vi = mem.u32(A_VI)
                vs = ""
                if mem.valid_ptr(vi):
                    vs = (f" fovy={mem.f32(vi + VI_FOVY):.2f} aspect={mem.f32(vi + VI_ASPECT):.5f}"
                          f" znear={mem.f32(vi + VI_ZNEAR):.2f} zfar={mem.f32(vi + VI_ZFAR):.1f}")
                # The swirl's own inputs (bondview2.c:1413, bondviewCalcIntroSwirlCamera):
                # segment index, the timer within it, Bond's position the spline is
                # offset from (player+0x3c4) and the theta_transform the Bond-relative
                # control points are rotated by (player+0x488 collision434, +0x10).
                bp = [mem.f32(p + PLAYER_BOND_POS + 4 * i) for i in range(3)]
                tt = [mem.f32(p + PLAYER_THETA_TRANSFORM + 4 * i) for i in range(3)]
                tp = [mem.f32(p + PLAYER_TRUE_POS + 4 * i) for i in range(3)]
                log.write(f"f{index} cam={cam} timer={mem.f32(A_TIMER):.1f} "
                          f"eye={e[0]:.1f},{e[1]:.1f},{e[2]:.1f}{vs}"
                          f" idx={mem.s32(A_INDEX)} bond={bp[0]:.2f},{bp[1]:.2f},{bp[2]:.2f}"
                          f" true={tp[0]:.2f},{tp[1]:.2f},{tp[2]:.2f}"
                          f" tt={tt[0]:.4f},{tt[1]:.4f},{tt[2]:.4f}\n")
        if a.first <= index <= a.last and (index - a.first) % a.every == 0 and fb["d"] is not None:
            save(index)
        if index > a.last:
            raise StopIteration

    try:
        emu.run(on_tick, lambda ram: 0, max_ticks=a.frames)
    except StopIteration:
        pass
    log.close()
    emu.close()
    print(f"romintro: saved={t['saved']} camera.log -> {out}")
    return 0 if t["saved"] > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
