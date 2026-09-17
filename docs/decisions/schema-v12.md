# Schema v12 — per-objective status and the active cheat set

Continues the order of work in `schema-v11.md`. v11 removed the last pointers
from the hash; v12 is the first of its step 2.

## What landed

### Per-objective status

v10 and v11 hashed `objective_count` plus one register word. That cannot
distinguish *objective 3 failed* from *objective 3 still running* — the count
is identical in both. The runtime keeps a status per objective in
`objectiveStatuses`, `OBJECTIVES_MAX` (10) of them, each a 4-byte
`OBJECTIVESTATUS`: 0 incomplete, 1 complete, 2 failed.

All 10 slots are hashed.

Measured on the Control 007agent recording — a run that completed the level:

```
frame   416  objectives=[0,0,0,0,0,0,0,0,0,0]
frame 32412  objectives=[0,0,0,0,0,1,0,0,0,0]
frame 34038  objectives=[0,0,0,0,0,1,1,0,0,0]
frame 35693  objectives=[0,0,0,0,1,1,1,0,0,0]
```

Four transitions across 36,053 ticks: the field latches rather than churns, so
it costs almost nothing in the hash while carrying real mission state.

It also settled an unrelated question. Natalya's `AI_RemoveMe` fires at frame
35,709, sixteen frames after the last objective completes — confirming that
transition is the level ending, not her death. See `chraidata.c` and
`docs/backlog.md`.

### Active cheat set

One byte per cheat from `g_CheatActivated`. Cheats change damage, physics and
spawns, so a run recorded with one active is a different simulation, and a
cheat toggled mid-run would otherwise surface as an unexplained divergence.

Sightline's own `TOUGH=` / `SL_007_*` / `ALLY_INVINCIBLE` knobs are deliberately
NOT cheats — they are build-time defines, captured in each recording's `.spec`
sidecar instead.

## Slot counts come from the ELF, not from counting

`OBJECTIVE_SLOTS` and `CHEAT_SLOTS` are 10 and 80 because the linked image says
so:

```
80075d58 00000028 B objectiveStatuses     -> 0x28 / 4 = 10
800696a0 00000050 B g_CheatActivated      -> 0x50     = 80
```

Counting the `CHEAT_ID` enum by hand gives the wrong answer: entries carry
explicit values, so ordinal position is not the count. `CHEAT_MAX` is
`CHEAT_INVALID+5`, which is not something to eyeball. Ask the binary.

## Deliberately deferred

First, a correction to an earlier draft of this file: **object destroyed state
is NOT deferred - it has been hashed all along.** The prop digest already walks
every OBJ/DOOR/WEAPON/EXPLOSION/SMOKE prop and hashes `state` (0x80 destroyed,
0x01 damaged) together with both object flag words, position, door travel and
open state, and dropped-weapon number and timer. That covers alarm panels,
CCTV, drone guns and crates as objects. Listing it as missing was wrong.

What genuinely remains after v12:

- **Camera and turret ROTATION, and the CCTV detection timer.** Their existence
  and destroyed/activated state are hashed; where they are POINTING is not.
  The detection timer (`bottom_pad->timer`, accumulating only inside a +/-45
  degree cone with an unobstructed stan line) is the sensitive one - a slow
  accumulator that drifts long before the alarm outcome changes.
- **Ammo and current weapon.** Not a global; it hangs off the player's
  inventory and the field was not pinned with confidence.
- **Per-hit-type counters.** Needs the player stats block layout. These
  distinguish "the AI got shot" from "the AI got shot somewhere that mattered",
  which a health delta cannot - HIT_GUN and HIT_HAT do zero damage.
- **Multiplayer**: four player slots and the per-player stats block, deferred
  by design until the MP track starts.

These are worth doing — the reasoning in `schema-v11.md` still stands. They were
left out because a field added on one side and not the other, or read at a
guessed offset, does not fail loudly: it silently reds out every comparison and
reads exactly like a divergence. Each deferred field costs one more unattended
rebaseline later; a wrong one costs a day of chasing a phantom.

## Both halves are one contract

`tools/trace/sltrace/state.py` and `src/game/sl_state_reader.c` must change
together, in the same order, in the same commit. The globals blob is now:

```
pack(">QiiiI", rng_seed, alarm_timer, objective_count,
               objective_registers, num_chr_slots)
+ objectiveStatuses[10] as 10 big-endian u32
+ g_CheatActivated[80] as raw bytes
```

On the Python side both arrays are read with `mem.block()`, which indexes by
true address and undoes the RDRAM word swizzle — so the u32 array unpacks
big-endian and the byte array reads straight out. Do not read the cheat bytes
with a per-byte loop: same answer, eighty block reads per tick.

## Verification

- Field behaviour measured on a real completed run (above).
- `make native-skeleton` builds the C half.
- Corpus rebaselined and gated: **GATE PASSED — 20 campaign levels at 10 runs,
  97 sweeps at 3.** 491 replays, zero divergences.

## Note on sequencing

Do not edit `state.py` while a gate is running. Each capture imports it live, so
the run starts hashing under the new schema against old baselines and reports
`different state schema: vN vs vN+1`. The harness refuses the comparison rather
than inventing a divergence, and the ledger is not written — but the run is
wasted. Rebaseline first, then gate.
