# Schema v11 — what the trace should hash

Derived from a full pass over Zoinkity's notes (617 files, indexed by
`tools/trace/index_docs.py`; areas mapped in `docs/reference-map.md`). Sources
are cited per field so nobody re-derives them. Paths are relative to
`/mnt/projects/goldeneye_docs/notes/GE Documentation/`.

## Why v11 exists

v10 hashes two raw `PropRecord *` pointers per guard (`weapons_held[0..1]`).
Pointers cannot agree across backends, so **every armed guard mismatches by
construction**. Measured on archives: of 31 distinct chrs, 0 match always, 3
partially, 28 never. That means the AI regression net — the whole point of the
chr hashes — has never verified anything cross-backend. Fixing that is the
minimum bar for v11; everything else here is worth doing while the hash is
already changing, because each change costs a rebaseline.

## Design rules

1. **No pointers, ever.** Hash identity, not address. This is the v10 bug.
2. **Hash simulation, not presentation.** Anything that only affects what is
   drawn stays out — it differs legitimately between backends.
3. **Both sides move together.** `tools/trace/sltrace/state.py` (MIPS offsets)
   and `src/game/sl_state_reader.c` (symbolic) are one contract. A field added
   to one and not the other silently reds out every comparison.
4. **Recordings survive.** Changing the hash invalidates trace files, never
   inputs; `rebaseline.py` regenerates them unattended.

## Field plan

### 1. Guards (chr) — the blocking change

Replace `weapons_held[0]/[1]` raw pointers with weapon IDENTITY:
- the held object's setup id and object type, reached via the PropRecord's
  ObjectRecord rather than the pointer value.
- Weapons assigned to a guard are marked by bitflag `0x4000` on the object's
  high bitfield — `Objects and Attributes/Expansion/08 weapons/08 weapon object
  type.txt`. Weapon objects are setup type `08`, 0x22 words
  (`Setup File Microcodes and Paths/Action block microcode/Action Block Types.txt`).

Keep every existing v10 chr field. Guard ids are never below 3
(`.../Action block microcode/7F03415C - guard constructor.txt`), which is a
useful sanity assertion.

Identity is the body+head PAIR, not body alone — the same constructor takes
both, and this is what the coverage tool got wrong for months.

### 1b. Hit locations, damage and weapon dropping

`Gameplay and Misc/Character hit locations.txt` gives the registration table
(part -> hit type): part 8 head, parts 7/F body, 0x64 weapon, 0x6E hat,
default limb.  The decomp's `HITTARGET` enum (src/bondaicommands.h) carries the
damage multipliers outright: `HIT_CHEST` 2x, hands/arms/shoulders 1x, and
**`HIT_GUN` (0x64) and `HIT_HAT` (0x6E) are annotated 0x damage** - shooting a
guard's weapon or hat registers a hit and provokes a reaction without hurting
them.  That is exactly why the stats block counts "other hits: weapon" and
"other hits: hat" separately from head/body/leg.

Hash the per-hit-type counters (they are already in the player stats block) -
they distinguish "the AI got shot" from "the AI got shot somewhere that
mattered", which a health delta alone cannot.

**`weapons_held` is DYNAMIC, which sharpens the v11 change.** Guards can drop
what they hold: `chrDropItems()` (chr.c) and the AI-list commands
`AI_ChrDropAllHeldItems` / `AI_ChrDropAllConcealedItems` (chrai.c) call
`propobjSetDropped()` on each slot and set `CHRHIDDEN_DROP_HELD_ITEMS`, with
surrender animations (`PTR_ANIM_surrendering_armed_drop_weapon`).  In
GoldenEye this is script/surrender driven rather than triggered by shooting the
weapon - the Perfect Dark behaviour of shooting a gun out of a hand does not
appear here.

So weapon identity must be hashed as a value that legitimately CHANGES during a
run, and the dropped weapon becomes a world prop whose dropped-state matters.
A schema that assumed weapons were static would read a normal disarm as a
divergence.

### 1c. Autoguns, CCTV and drone guns — a separate AI class

Turrets are not chrs and do not share their AI.  The decomp distinguishes
`PROPDEF_AUTOGUN`, `PROPDEF_CCTV`, and drone guns flagged with
`PROPFLAG_IS_DRONE_GUN` (0x10000000); `AutogunRecord` carries `is_active` at
0xD0.  Docs: `Objects and Attributes/Data and Features/0D Autoturrets/notes.txt`
(damage frequency, firing rate) and
`Expansion/0D autoturrets/7F004500 - object 0D expansion - autoturrets.txt`.

Hash per turret: active state, current rotation, and destroyed state.  They
acquire and fire on targets independently of guard AI, so an AI change that
breaks turret targeting would be invisible in the chr hashes.

### 2. Objectives — currently far too coarse

v10 hashes `objective_count` plus one register word. The runtime exposes
per-objective status for up to 10 objectives via a pointer table at
`0x80075D30` (`Setup File Microcodes and Paths/objectives and briefings/
7F057238 - return objective status.txt`).

Hash all 10 status slots. Objective types and their completion conditions are
documented in `.../objective types.txt`: create `0x17`, end `0x18`, destroy
object `0x19`, complete-if-true `0x1A`, fail-if-true `0x1B`, collect `0x1C`,
each carrying a minimum-difficulty byte (0 agent, 1 secret, 2 00, 3 007) —
which is why the objective LIST differs per difficulty and why a sweep's
objective state is difficulty-dependent.

### 3. Surveillance cameras (type 06)

Documented with field offsets in `Setup File Microcodes and Paths/objects/
object block types.txt`: centre preset (0x82), rotation limits (0xCC/0xCE/0xD0),
turn speed (0xDE). Hash current rotation and active/destroyed state per camera.
Cameras feed guard perception, so they are simulation, not presentation.

### 3b. Camera detection timer — measured, not guessed

`propobj.c` around 5183: a camera sees the player only within a **+/-45 degree
cone** AND with an unobstructed `stanTestLineUnobstructed` line (height 100 -
the same stan-tile test guards use).  While seen it accumulates
`bottom_pad->timer += g_ClockTimer`, and at
`CCTV_ALARM_FRAMES * F_80030B14` it calls `alarmActivate()` and resets to 0.

`CCTV_ALARM_FRAMES` is 250 (300 PAL).  `F_80030B14` is 2.0 on the easiest
difficulty and 1.0 on the others (lv.c), so the delay is about 4.2s at 60fps on
secret/00/007 and about 8.3s on agent.

**Hash `bottom_pad->timer` per camera.**  It is a slow accumulator that only
advances under a geometric AND line-of-sight condition, which makes it an
unusually sensitive detector: any drift in camera angle, player position or the
stan line test shows up as a differing countdown long before it changes the
alarm outcome.

### 4. Object runtime state

`object block types.txt` documents a runtime bitflags word at object+0x64,
filled during play. The 0x34-byte position record (`expando guard.txt`) carries
type codes (1 normal, 2 door, 3 guard, 4 weapon pickup, 6 player, 7 explosion)
and state bits where **bit 2 = actually on screen**.

Keep hashing the on-screen bit: it gates model unloading and therefore feeds
the simulation. It is also the current tick-113 divergence, so it must stay
visible rather than be masked.

Destructible objects have documented destruction format codes 0x00-0x0B
(`Objects and Attributes/destruction table.txt`); hash destroyed state.

### 5. Ammo and inventory

Ammo types and pickup amounts are tabulated in `Objects and Attributes/
ammunition/ammo types and amounts.txt` (9mm, rifle, shotgun cartridges,
grenades, rockets, remote/proximity/timed mines...), with per-difficulty
multipliers already known from `lv.h` (agent 2.0, secret 1.5, 00/007 1.0).
Maximums in `ammo value tables and maximums.txt`.

Hash the player's per-type ammo counts and current weapon. Ammo is a pure
simulation quantity and a sensitive divergence detector — it changes on every
shot and pickup.

### 6. Cheats

Cheat entries live in a table at `0x8003F808`, 0x10 bytes each, the last word
holding `cheat.flags`; activation goes through `7F091B64`
(`Main Menus/Cheat Menu/Activate Cheats.txt`, `Deactivate Cheats.txt`). Flags
`0x10` and `0x20` alter activation behaviour. Button cheat codes are documented
separately (`Button Cheat Codes.txt`, `cheat text & button codes (Krijy).txt`).

Hash the active-cheat set. Cheats change physics, damage and spawns, so a run
recorded with one active is a different simulation — and a cheat toggled
mid-run would otherwise look like an unexplained divergence. Sightline's own
`TOUGH=`/`SL_007_*` observation knobs are deliberately NOT cheats
(see `Makefile` notes) and are already captured in the sidecars.

### 7. Multiplayer — for when we get there

- **Per-player stats block** at `0x80079EE0`, pointers to players 1-4
  (`Gameplay and Misc/player statistics.txt`): shots fired, head/body/leg/other
  hits, per-player kill counts, distance travelled, timing records. This is the
  scoring substrate and it is all simulation state.
- **Scenario** and game length (`Multiplayer/scenarios/`), rank determination
  (`7F0C3C94 - determine rank.txt`), flag-tag scoring quirks
  (`scenarios/flag tag scoring.txt` — returns a millisecond value).
- **Object respawn** flag at obj+2 bit `0x04`, with MP-only expansion and
  FlagTag-specific generation (`Multiplayer/object respawn.txt`).
- **Respawn/death**: `7F0888E8 - MP respawn.txt`, `death fadeout and respawn.txt`.
- Radar (`Radar Blips - 7F0AD014.txt`, `7F0C6090 - Radar Display.txt`) is
  presentation — exclude.

The v10 schema assumes a single player (entity key 0xFFFF). MP needs four
player slots keyed separately, plus the stats block. Design the key space for
that now even if only player 1 is populated in solo.

### 8. Alarm — NOT documented

"Alarm" appears only incidentally across the corpus (address ranges, sound
effects, object templates). There is no alarm-system document. The alarm
information graph in ROADMAP goal 1 is therefore original work, not a port.
Keep hashing `alarm_timer`; extend when the graph exists.

## Deliberately excluded

Radar and blips, explosion/smoke/scorch/bullet-impact overlay pools
(`Gameplay and Misc/Overlays/`), monitor and wallscreen animation state
(`Data and Features/0A 0B Monitors/`), fog and sky, display lists. All
presentation. Note that explosions DO carry simulation consequences (damage,
destruction) — those are captured via object destroyed state and player/chr
health, not via the overlay pool itself.

## Open defect this schema does NOT fix

After weapon identity landed, 11 of 31 guards on archives still never match.
Critically they fail in frames 0-112 - the window where player and props agree
byte for byte - so this is a real schema or port defect, not fallout from the
tick-113 divergence.

Ruled out so far: `aioffset`/`aireturnlist` (AI-list indices, not pointer
arithmetic, initialised to 0/-1), and a suspected `OBJ_ID_OFF` mismatch (the
field really is at offset 4 on both sides; the "ID 0x6" struct comment refers
to something else).  Native inspection shows no obvious shape difference -
keys 9 and 12 dual-wield, but 10, 11 and 13 look identical to keys that match.

Next step needs emulator-side chr field values to diff against the native ones
field by field; deferred only to avoid competing with a running gate.

## Order of work

1. Weapon identity only. Rebaseline. Verify chr hashes actually start matching
   on archives. **If they do not, the pointer explanation was wrong and
   everything below is built on sand — stop and find out why.**
2. Objectives (10 slots), ammo, cheats.
3. Cameras and object destroyed state.
4. MP key space, populated when the MP track starts.
