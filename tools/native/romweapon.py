#!/usr/bin/env python3
"""ROM oracle for a first-person WEAPON: what the cartridge draws in Bond's hand.

    .venv\\Scripts\\python.exe tools/native/romweapon.py --stage 36 --weapon sniper \\
        --out %TEMP%\\sightline-oracle\\surface-sniper

Boots the UNMODIFIED cartridge ROM under the parallel_n64 libretro core (the
same core rom-reference.ps1 and the trace harness use), lands it straight in a
stage, switches on the game's own All Guns / Max Ammo cheats, presses A until
the wanted weapon is in the right hand, and saves frames. Frames are
ROM-derived: they go OUTSIDE the repository and are never committed.

WHY. Every "the gun looks wrong" question needs the cartridge's own drawing of
that gun, and the recorded streams under tools/trace/inputs cannot supply it:
they answer direct-boot ROM images (build/u/direct) that the Windows tree does
not build, and none of them was recorded holding the weapon in question.
Nothing here patches the ROM; every value is written into RDRAM through the
one writer the trace harness already has (sltrace.state.Memory.w32), at
addresses resolved from the matching build's link map.

HOW EACH STEP IS THE GAME'S OWN MECHANISM, so the frames are the cartridge's
behaviour and not this script's:

  * Direct boot: boss.c:95 - the direct-boot ROMs set g_StageNum's INITIALISER
    so bossMainloop takes its "not the title" branch. Here g_StageNum and
    g_SelectedDifficulty (lv.c:131) are written every frame for the first
    --boot-pokes frames instead, because the data segment lands in RDRAM
    during the first frames of boot and the value has to be in place when
    bossMainloop reads it. The readback in the log shows when the game took
    it (g_CurrentStageToLoad follows).

  * Cheats: the cheat MENU's leftover state, front.c:7959 g_CheatActivated[id]
    and front.c:7842 g_AppendCheatSinglePlayer; lv.c:1012-1025 then applies
    them every ticking frame of the level. Written once the level is up.

  * Weapon: the A button is "next weapon" (bondview2.c:5077), and with All
    Guns the cycle walks every hand item in ITEM_IDS order
    (bondinv.c:564-604). The right hand is read back each frame from
    g_CurrentPlayer->hands[0] (the offsets sltrace.state already uses) and
    the presses stop the frame it matches.

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
from sltrace.state import Memory, PLAYER_HAND0, HAND_WEAPONNUM    # noqa: E402
from sltrace.symbols import SymbolTable                            # noqa: E402

# src/bondconstants.h CHEAT_IDS and ITEM_IDS, the few this oracle needs. The
# native side (src/native/sl_cheat.c) carries the full name tables; the
# numbers accepted here are the same enums.
CHEAT_ALLGUNS = 3
CHEAT_MAXAMMO = 4
ITEM_NAMES = {
    "pp7": 4, "wppk": 4, "pp7sil": 5, "dd44": 6, "tt33": 6, "klobb": 7,
    "kf7": 8, "ak47": 8, "zmg": 9, "uzi": 9, "d5k": 10, "d5ksil": 11,
    "phantom": 12, "ar33": 13, "m16": 13, "rcp90": 14, "shotgun": 15,
    "autoshotgun": 16, "sniper": 17, "sniperrifle": 17, "cougar": 18,
    "ruger": 18, "goldengun": 19, "silverpp7": 20, "goldpp7": 21,
    "laser": 22, "watchlaser": 23, "grenadelauncher": 24,
    "rocketlauncher": 25, "grenade": 26, "timedmine": 27,
    "proximitymine": 28, "remotemine": 29, "detonator": 30, "taser": 31,
}


def refuse_inside_repo(p: pathlib.Path) -> None:
    try:
        p.resolve().relative_to(ROOT)
    except ValueError:
        return
    print(f"romweapon: refusing to write ROM-derived frames inside the "
          f"repository: {p}", file=sys.stderr)
    sys.exit(2)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", type=int, default=36, help="LEVELID number (36 = Surface 1)")
    ap.add_argument("--difficulty", type=int, default=1)
    ap.add_argument("--weapon", default="sniper", help="in-game name or ITEM_IDS number")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rom", default=os.environ.get("SL_ROM") or str(ROOT / "baserom.u.z64"))
    ap.add_argument("--core", default=None)
    ap.add_argument("--eeprom", default=None, help="save to boot with (default: none, a fresh cartridge)")
    ap.add_argument("--frames", type=int, default=6000, help="hard bound on frames")
    ap.add_argument("--boot-pokes", type=int, default=240, dest="boot_pokes")
    ap.add_argument("--cheat-at", type=int, default=840, dest="cheat_at",
                    help="frame to write the cheat state (the level is up well before)")
    ap.add_argument("--settle", type=int, default=600, help="frames after the cheat write before pressing")
    ap.add_argument("--gap", type=int, default=40, help="frames between A presses")
    ap.add_argument("--hold", type=int, default=3)
    ap.add_argument("--after", type=int, default=90, help="frames after the weapon is up before the first save")
    ap.add_argument("--shots", type=int, default=4)
    ap.add_argument("--every", type=int, default=30)
    a = ap.parse_args()

    weapon = ITEM_NAMES.get(a.weapon.lower().replace("_", "").replace("item", ""), None)
    if weapon is None:
        if not a.weapon.isdigit():
            print(f"romweapon: unknown weapon '{a.weapon}'", file=sys.stderr)
            return 2
        weapon = int(a.weapon)

    out = pathlib.Path(a.out)
    refuse_inside_repo(out)
    out.mkdir(parents=True, exist_ok=True)

    sym = SymbolTable.from_map(ROOT / "build/u/ge007.u.map")
    A_CHEAT = sym.addr("g_CheatActivated")
    A_APPEND = sym.addr("g_AppendCheatSinglePlayer")
    A_PLAYER = sym.addr("g_CurrentPlayer")
    A_CAM = sym.addr("g_CameraMode")
    A_STAGE = sym.addr("g_StageNum")
    A_DIFF = sym.addr("g_SelectedDifficulty")
    A_LOAD = sym.addr("g_CurrentStageToLoad")

    emu = LibretroEmulator(a.rom, core_path=a.core, input_stream=None, eeprom=a.eeprom)
    fb = {"d": None, "w": 0, "h": 0, "pitch": 0}

    def on_video(d, w, h, pitch):
        fb["d"], fb["w"], fb["h"], fb["pitch"] = d, w, h, pitch
    emu._on_video_capture = on_video

    A = InputFrame(1 << InputFrame.A_BUTTON)
    N = InputFrame(0)
    press_from = a.cheat_at + a.settle
    emu._frames = [N] * press_from
    for _ in range(64):
        emu._frames += [A] * a.hold + [N] * (a.gap - a.hold)

    t = {"cheat_at": None, "up_at": None, "saved": 0, "last_w": None,
         "last_cam": None, "last_load": None}

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
        p = out / f"rom-w{weapon}-{index:06d}.ppm"
        with open(p, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            f.write(b"".join(rows))
        t["saved"] += 1
        return p

    def poke_u8(mem, addr, value):
        word = bytearray(mem.u32(addr & ~3).to_bytes(4, "big"))
        word[addr & 3] = value
        mem.w32(addr & ~3, int.from_bytes(word, "big"))

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
        if not mem.valid_ptr(p):
            return
        if t["cheat_at"] is None and index >= a.cheat_at:
            poke_u8(mem, A_CHEAT + CHEAT_ALLGUNS, 1)
            poke_u8(mem, A_CHEAT + CHEAT_MAXAMMO, 1)
            mem.w32(A_APPEND, 1)
            t["cheat_at"] = index
            print(f"tick {index}: cheats written (player {p:08x})")
        wr = mem.s32(p + PLAYER_HAND0 + HAND_WEAPONNUM)
        if wr != t["last_w"]:
            print(f"tick {index}: right hand weapon {wr}")
            t["last_w"] = wr
        if wr == weapon and t["up_at"] is None:
            t["up_at"] = index
            del emu._frames[emu._frame_index + 1:]
            print(f"tick {index}: weapon {wr} is up, presses stopped")
        if t["up_at"] is not None and fb["d"] is not None:
            d = index - t["up_at"]
            if d >= a.after and (d - a.after) % a.every == 0 and t["saved"] < a.shots:
                print("saved", save(index))
                if t["saved"] >= a.shots:
                    raise StopIteration

    try:
        emu.run(on_tick, lambda ram: 0, max_ticks=a.frames)
    except StopIteration:
        pass
    emu.close()
    ok = t["up_at"] is not None and t["saved"] > 0
    print(f"romweapon: {'OK' if ok else 'FAILED'} cheat_at={t['cheat_at']} "
          f"up_at={t['up_at']} saved={t['saved']} -> {out}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
