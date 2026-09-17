"""Boot the game straight into a stage, skipping the menus.

Recording twenty-plus levels means reaching each one, and reaching one means
walking the legal screen, logos, file select, mission select and difficulty
every time - then replaying that walk on every verification run afterwards.

This is not a new feature. bossMainloop already handles g_StageNum being
something other than the title: it validates saves, selects folder 1, sets
Agent difficulty, prepares the briefing, and calls lvlStageLoad. All that was
missing was a way to set the variable.

Setting its INITIALISER is what makes it reachable, and the timing is why.
The value is in place before the first instruction executes, so there is no
window to hit - which matters here, because the harness cannot touch RDRAM
until after the first frame, by which point the boot check has long run.
Nothing is written at runtime, so record and replay have nothing to disagree
about.

Rare's own route in was `-level_NN`, read by tokenReadIo over the dev-kit PI
interface at 0xFFB000. That does not work here twice over: the address is past
the end of a retail image, and the tokens that actually reach g_Tokens are the
game's own built-in defaults (the -m* memory pool switches). Padding a ROM out
to 0xFFB000 and writing the switch there was measured to have no effect.

Each level needs its OWN build. The segment holding the initialiser is
compressed, so changing one word moves 58103 bytes across a 209KB span - there
is no byte to patch in a finished image. A build costs well under a minute and
the result is cached, so this is cheaper than it sounds.
"""

from __future__ import annotations

from pathlib import Path

#: LEVELID values from bondconstants.h. These are internal map ids, NOT
#: campaign order - Bunker 1 is 9 while the surrounding missions are in the
#: twenties and thirties, so never compute one from a level's position.
LEVEL_IDS = {
    "bunker1": 9,
    "silo": 20,
    "statue": 22,
    "control": 23,
    "archives": 24,
    "train": 25,
    "frigate": 26,
    "bunker2": 27,
    "aztec": 28,
    "streets": 29,
    "depot": 30,
    "complex": 31,
    "egypt": 32,
    "dam": 33,
    "facility": 34,
    "runway": 35,
    "surface": 36,
    "jungle": 37,
    "temple": 38,
    "caverns": 39,
    "citadel": 40,
    "cradle": 41,
    "surface2": 43,
}

#: DIFFICULTY values from bondconstants.h. Agent is the direct-boot default,
#: so it needs no override and gets no suffix.
DIFFICULTIES = {"agent": 0, "secret": 1, "00": 2, "007": 3}

#: Where per-level builds are cached. Never committed - it is ROM-derived.
DIRECT_DIR = Path("build/u/direct")


def difficulty_id(name: str) -> int:
    key = name.strip().lower()
    if key not in DIFFICULTIES:
        raise KeyError(f"unknown difficulty {name!r}. "
                       f"Known: {', '.join(DIFFICULTIES)}")
    return DIFFICULTIES[key]


def level_id(name: str) -> int:
    key = name.strip().lower()
    if key not in LEVEL_IDS:
        raise KeyError(
            f"unknown level {name!r}. Known: {', '.join(sorted(LEVEL_IDS))}")
    return LEVEL_IDS[key]


def direct_rom(name: str, difficulty: str = "agent", *,
               tough: bool = False, base: Path | None = None) -> Path:
    """Path of the cached direct-boot ROM for a level, built or not.

    Agent keeps the unsuffixed name so every existing recording and its ROM
    keep matching by hash; harder builds get their own file.
    """
    lvl = name.strip().lower()
    dif = difficulty.strip().lower()
    stem = lvl if dif == "agent" else f"{lvl}.{dif}"
    if tough:
        stem += ".tough"
    return (base or DIRECT_DIR) / f"ge007.u.{stem}.z64"


def have_direct_rom(name: str, difficulty: str = "agent", *,
                    base: Path | None = None) -> bool:
    p = direct_rom(name, difficulty, base=base)
    return p.exists() and p.stat().st_size > 0
