# Symbols must come from the ROM being run

## The bug

Every capture resolved symbols through `build/u/ge007.u.map` — the *matching*
build's map — regardless of which ROM it was replaying. `trace.py` has a
`--map` option, but nothing passed it, so `DEFAULT_MAP` applied to all 117
direct-boot ROMs.

Direct-boot ROMs are built with extra defines (`SL_DIRECT_BOOT_LEVEL`,
`TOUGH=`, the `SL_007_*` sliders, `SL_ALLY_INVINCIBLE`). Those change code size,
which shifts symbols above some address. Measured on `dam-sweep-007agent`:
**653 of 2593 symbols differ**, the affected ones by +0x10.

## Why it survived a year of green gates

It did not fail loudly. It failed silently, and in the most expensive way:

- `g_CurrentPlayer` resolved to a word holding `0x00000000`.
- `_read_player` saw an invalid pointer and returned `{}`.
- `_player_hash` returned **eight zero bytes** — every tick, every recording.

Constant zeros are perfectly deterministic, so the gate passed. **The player
was never verified by the corpus**: not position, not health, not armour, not
death state. A replay where Bond ended up somewhere else entirely would have
hashed identically, which is the worst failure available to an oracle —
confident and wrong.

It also silently misaligned schema v12. `objectiveStatuses` and
`g_CheatActivated` are both above the boundary, so objective status was read
four words early. That looked convincing rather than broken: the array's
leading `1,1` appeared at slots 4 and 5, so the values latched 0 -> 1 exactly
as real objectives do. Genuine data, wrong alignment.

Symbols *below* the boundary were unaffected, which is why chrs, props,
`g_ActivePropsTail` and `alarm_timer` always read correctly and nothing looked
wrong from the outside.

## The fix

1. `direct-boot` writes `ge007.u.<name>.map` beside every ROM it builds.
2. `trace.py::_map_for()` prefers that sibling map whenever `--map` is left at
   its default, so capture, verify, rebaseline and gate all pick it up without
   changing a single caller. An explicit `--map` still wins.
3. `tools/trace/rebuild_roms.py` rebuilds the existing ROMs from their `.spec`
   sidecars so each gets its map. It verifies the ROM hash after every build:
   the traces are matched to ROMs by `rom_sha1`, so the bytes must not change.
   All 117 rebuilt **unchanged** — the build is deterministic, as assumed.

## What it cost to find

Nothing about the corpus looked wrong. It surfaced only because a v13 offset
probe read the player through a per-ROM map and got a valid pointer where the
schema got null. The tell was available earlier and was not chased: an earlier
session recorded "player parity to tick 292" measured through a different path,
which should not have been reconciled with a corpus that hashes no player.

## Rule

**Never resolve symbols against a different binary than the one executing.**
A symbol table is part of the build, not of the repository. If a tool takes a
ROM, it must take that ROM's map, and the default must not silently be
somebody else's.
