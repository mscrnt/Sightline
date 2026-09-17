#!/usr/bin/env python3
"""Record AI observation sweeps for every level and difficulty, back to back.

Walk a level for a few minutes with guards engaged; the point is contact, not
completion. What the schema captures - actiontype, lastseetarget60,
lastheartarget60, lastshooter, firecount, accuracyrating - only populates when
guards actually see, hear and shoot at you.

Each recording runs as its own PROCESS. That is not tidiness: dlopen hands back
an already-loaded core, so a second recording in one process would resume the
first one's state instead of booting.

Resumable. Anything already recorded is skipped, so stopping and coming back
costs nothing.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sltrace.levelboot import LEVEL_IDS  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
INPUTS = ROOT / "tools/trace/inputs"

#: Campaign order. Multiplayer-only maps are excluded.
CAMPAIGN = ["dam", "facility", "runway", "surface", "bunker1", "silo",
            "frigate", "surface2", "bunker2", "statue", "archives", "streets",
            "depot", "train", "jungle", "control", "caverns", "cradle",
            "aztec", "egypt"]

ALL_DIFFICULTIES = ["agent", "secret", "00", "007"]

#: What a sweep records. "007max" is 007 mode with its sliders at maximum -
#: accuracy, damage and health at 10, reaction at 1.
#:
#: 007 is the CONFIGURABLE difficulty: get_007_*_mod() reads sliders the player
#: sets, and their defaults are 1/1/1/0, which is why an unconfigured 007
#: measures the same as 00 Agent. That is the defaults coinciding, not the two
#: difficulties being the same - a mistake worth not repeating. The menu
#: computes the sliders as x*x*10, and chraction.c takes a different branch
#: above 1.0, so maxed 007 is a regime no fixed difficulty reaches.
SWEEP_DIFFICULTIES = ["agent", "secret", "00", "007max", "007agent"]

#: Slider settings per preset, passed to the build.
PRESET_SLIDERS = {
    # Accuracy and reaction maxed; damage and health left at 100%.
    #
    # Damage and health scaling is already known exactly from the difficulty
    # constants, so raising them makes a run punishing without revealing any
    # behaviour the table does not already give. Accuracy and reaction change
    # what guards DO - how often they connect and how fast they respond - which
    # is what an AI observation trace is for.
    # Only REACTION is raised, and it is the one slider that changes what
    # guards do rather than what happens to the player.
    #
    # get_007_reaction_speed() returns 0 outside 007 mode, and the call sites
    # compute ret = mod*(100-ret)+ret - so 0 leaves ratings untouched and 1
    # pushes them to 100. Those ratings feed modelSetAnimation's play speed for
    # firing, kneeling and grenade throws, so guards physically act faster.
    # That regime is unreachable at agent, secret or 00.
    #
    # Accuracy is 0.6, matching AGENT. Below 1.0 the slider is a straight
    # multiply, and 007 mode sets g_AiAccuracyModifier to 1.0, so 0.6 lands on
    # agent's effective accuracy exactly (agent: 0.60 x 1.0; here: 1.0 x 0.6).
    #
    # Lowering it costs nothing: accuracy only scales shotbondsum, an
    # accumulator deciding how often a shot connects. It changes what happens
    # to Bond, not what guards decide - so it is the one slider that can be
    # turned down purely for recordability.
    #
    # Damage and health stay at 1: their scaling is already exact in the
    # difficulty constants.
    "007max": {"S007_REACTION": "1", "S007_ACCURACY": "0.6",
               "S007_DAMAGE": "1", "S007_HEALTH": "1"},

    # 007 with AGENT lethality and maxed reaction - the do-everything preset.
    #
    # 007max above lowers accuracy to agent but leaves DAMAGE at 1, which is
    # 00 Agent's full damage (lv.h: agent 0.5, secret 0.75, 00 1.0).  A run
    # made to visit every objective, trip every alarm and try every kill needs
    # to survive long enough to do it, and damage - like accuracy - only
    # changes what happens to Bond, not what guards decide.  So both drop to
    # agent's constants while reaction stays maxed, which is the one slider
    # that changes guard BEHAVIOUR.
    #
    # 007 carries 00 Agent's objective list, so this is the fullest objective
    # set at the lowest lethality the game can express.  Enemy health has no
    # per-difficulty constant, so it stays neutral at 1.
    "007agent": {"S007_REACTION": "1", "S007_ACCURACY": "0.6",
                 "S007_DAMAGE": "0.5", "S007_HEALTH": "1"},
}

#: The build only knows the four real difficulties.
PRESET_DIFFICULTY = {"007max": "007", "007agent": "007"}

#: How much to divide incoming damage by, per preset.
#:
#: All presets use the same toughness now. 007max needed extra only while its
#: damage and accuracy were raised; with both back at 1 it is no deadlier per
#: hit than 00.
DEFAULT_TOUGH = "10"
PRESET_TOUGH = {}

#: file2.c locks these bonus stages below the given difficulty, so the lower
#: ones are not playable and must not be queued.
FLOORS = {"aztec": "secret", "egypt": "00"}

#: 5 minutes at 60fps. The recorder stops itself here if you do not.
DEFAULT_CAP_MINUTES = 5.0


def difficulties_for(level: str) -> list[str]:
    """Presets to record for a level, honouring the bonus-stage floors."""
    floor = FLOORS.get(level)
    if not floor:
        return list(SWEEP_DIFFICULTIES)
    cut = ALL_DIFFICULTIES.index(floor)
    return [d for d in SWEEP_DIFFICULTIES
            if ALL_DIFFICULTIES.index(PRESET_DIFFICULTY.get(d, d)) >= cut]


def sweep_name(level: str, difficulty: str) -> str:
    # The "-sweep-" is load-bearing: the gate uses it to classify these as
    # observation data rather than Phase 0 parity recordings.
    return f"{level}-sweep-{difficulty}"


def already_done(level: str, difficulty: str) -> bool:
    f = INPUTS / f"{sweep_name(level, difficulty)}.input"
    return f.exists() and f.stat().st_size > 0


def record(level: str, difficulty: str, cap_frames: int,
           ally_invincible: bool = False) -> tuple[bool, str, int]:
    """Run one recording. Returns (ok, stop reason, frames)."""
    name = sweep_name(level, difficulty)
    cmd = ["make", "trace-record", f"LEVEL={level}",
           f"DIFFICULTY={PRESET_DIFFICULTY.get(difficulty, difficulty)}",
           f"TOUGH={PRESET_TOUGH.get(difficulty, DEFAULT_TOUGH)}",
           f"SL_NAME={name}", f"RECORD_TICKS={cap_frames}"]
    cmd += [f"{k}={v}" for k, v in PRESET_SLIDERS.get(difficulty, {}).items()]
    if ally_invincible:
        cmd.append("ALLY_INVINCIBLE=1")
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = proc.stdout + proc.stderr
    reason, frames = "unknown", 0
    for line in out.splitlines():
        if line.startswith("STOP "):
            parts = line.split()
            reason = parts[1]
            frames = int(parts[2].split("=")[1])
    if proc.returncode != 0 and reason == "unknown":
        tail = "\n".join(out.strip().splitlines()[-6:])
        print(f"\n  recording failed:\n{tail}\n", file=sys.stderr)
        return False, "failed", 0
    return True, reason, frames


def ask(prompt: str, choices: str) -> str:
    while True:
        got = input(prompt).strip().lower()[:1]
        if got in choices:
            return got
        print(f"  please answer one of: {', '.join(choices)}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--minutes", type=float, default=DEFAULT_CAP_MINUTES,
                    help="cap per recording (default 5; 0 = no cap, stop by "
                         "closing the window)")
    ap.add_argument("--dry-run", action="store_true",
                    help="print the queue and exit without recording")
    ap.add_argument("--ally-invincible", action="store_true",
                    help="escort NPCs (Natalya) immune - lets escort levels "
                         "be played to completion. Recorded in each sidecar "
                         "as ALLY=1, and a no-op where she does not appear.")
    ap.add_argument("--levels", default="",
                    help="comma-separated subset, default the whole campaign")
    ap.add_argument("--difficulties", default="",
                    help="comma-separated subset, e.g. 007")
    ap.add_argument("--redo", action="store_true",
                    help="re-record even where a sweep already exists")
    a = ap.parse_args()

    # The recorder stops itself on window close; the cap is only a backstop
    # for a session left running.  A level played for every objective can far
    # outlast five minutes, so 0 lifts it (six hours, effectively unlimited).
    cap = int(a.minutes * 60 * 60) if a.minutes > 0 else 6 * 60 * 60 * 60
    levels = [l.strip() for l in a.levels.split(",") if l.strip()] or CAMPAIGN
    unknown = [l for l in levels if l not in LEVEL_IDS]
    if unknown:
        print(f"unknown level(s): {', '.join(unknown)}", file=sys.stderr)
        return 1

    want = [d.strip() for d in a.difficulties.split(",") if d.strip()]
    queue = []
    for lvl in levels:
        for dif in difficulties_for(lvl):
            if want and dif not in want:
                continue
            if not a.redo and already_done(lvl, dif):
                continue
            queue.append((lvl, dif))

    if not queue:
        print("nothing to record - everything requested already exists "
              "(use --redo to replace).")
        return 0

    capdesc = f"{a.minutes:g} minute cap each" if a.minutes > 0 else "no cap"
    print(f"{len(queue)} recording(s) queued, {capdesc}.")
    print("Close the game window when you are done with a level.\n")

    if a.dry_run:
        for lvl, dif in queue:
            print(f"  {lvl:10} {dif}")
        return 0

    i = 0
    while i < len(queue):
        lvl, dif = queue[i]
        print(f"[{i+1}/{len(queue)}] {lvl} @ {dif}")
        print("  building/loading ROM, then the window opens...", flush=True)
        ok, reason, frames = record(lvl, dif, cap, a.ally_invincible)

        if not ok:
            choice = ask("  recording FAILED. [r]etry, [n]ext, [q]uit? ", "rnq")
        else:
            mins = frames / 3600
            if reason == "frame-cap":
                print(f"  hit the {a.minutes:g} minute cap ({frames} frames).")
            else:
                print(f"  window closed after {frames} frames ({mins:.1f} min).")
            choice = ask("  [r]etry this one, [n]ext, [q]uit? ", "rnq")

        if choice == "q":
            print(f"\nstopped. {len(queue) - i} left; re-run to continue where "
                  f"you left off.")
            return 0
        if choice == "n":
            i += 1
        # 'r' leaves i alone, so the same level records again

    print("\nall queued sweeps recorded.")
    print("verify them with:  make trace-gate            (sweeps gate at 3 runs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
