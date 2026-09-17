# Schema v13 — ammo, hands, hit registers, camera and turret aim

Completes the solo-play half of the field plan in `schema-v11.md`. Landed in one
bump rather than several, because each schema change costs a full rebaseline.

## Context: v13 is also the first schema the player is actually in

v13 shipped alongside the per-ROM symbol map fix
(`per-rom-symbol-maps.md`). Until that fix the player hash was eight constant
zero bytes on every tick of every recording, so this is the first gate where a
player-state regression could be caught at all — and the first where v12's
objective statuses are read at the right address.

## Fields

### Player ammo — `ammoheldarr[30]` at player+0x1130

A pure simulation quantity that moves on every shot and pickup, which makes it
one of the sharpest divergence detectors available. Offset corroborated by
`gunsightmode` (0x1128) and `field_112C`, whose name encodes its own address.

### Hands — `hands[2]` at player+0x870

`weaponnum` at +0x00 and `weapon_ammo_in_magazine` at +0x2C. Field names inside
`struct hand` encode absolute offsets (`field_87D`, `field_884`, `field_8A0`),
which fixes both.

**The stride is derived, not annotated.** `struct hand` runs to `field_A48` and
`gunposamplitude` lands at 0xFC0, leaving 0x3A8 per hand. Supporting evidence:
`hands[1]` reads valid item ids (0 and 7) on both bunker levels rather than
noise. That is inference, not proof — if dual-wield state ever looks wrong,
this is the first thing to re-check.

### Hit registers — `g_playerPerm->shot_count[7]`

total, head, body, limb, gun, hat, object. These separate "the AI got shot" from
"shot somewhere that mattered": `HIT_GUN` and `HIT_HAT` register a hit and do
zero damage, so a health delta cannot see them.

Measured, monotonic, zero regressions across three levels:

```
bunker1 [615, 14, 126, 193, 15,  2, 23]
bunker2 [598, 14, 111, 184, 17,  5, 14]
dam     [358,  8,  68, 102, 10,  0, 41]
```

### Camera and turret aim

Cameras and turrets were already hashed as objects — existence, destroyed and
activated bits. What was missing is where they POINT.

- CCTV (`PROPDEF_CCTV`, 6): rotation `unkC8` (0xC8) and detection `timer`
  (0xE0). `unkC8` is identified by use, not by name: it is integrated as
  `unkC8 += unkD8 * g_GlobalTimerDelta` and tested against limits
  (propobj.c ~5217). The timer accumulates `g_ClockTimer` only inside a +/-45
  degree cone with an unobstructed stan line, firing at
  `CCTV_ALARM_FRAMES * F_80030B14` (propobj.c:5182) — a slow accumulator that
  drifts long before the alarm outcome changes.
- Autogun (`PROPDEF_AUTOGUN`, 13): `rot_related` (0x84) and `is_active` (0xD0).

`PROPDEF_CCTV`=6 and `PROPDEF_AUTOGUN`=13 cross-check against Zoinkity's notes
("type 06 camera", "0D autoturrets"), as does `state` at PropDefHeader+0x2
matching the schema's existing `OBJ_STATE_OFF`.

## The plausibility gate had to grow, and why that is not cheating

The v13 player fields are read BEFORE the gate, because they are part of what
it has to judge.

Measured on dam: **76 ticks of 22,686 — frames 24248..24297, the level tearing
down at the end of the run — kept a valid pointer and plausible position and
health while ammo read +/-1e9.** Position and health do not notice teardown, so
gating on them alone let garbage into the hash. Confirmed by running the real
`StateReader` rather than a probe: `gated-out 0` on all three levels, garbage
reaching the hash on two.

Extending a filter until data passes is normally how you get a green gate that
means nothing. What makes it defensible here:

- the filter already existed for exactly this condition — its docstring
  describes a pointer that passes a range check while the struct behind it is
  not a player;
- the excluded window is a specific, explained 50-frame teardown at the end of
  a run, not a tuned cutoff;
- the exclusion count matches the independent diagnostic exactly (76 bad ticks
  found, 76 gated, 22,610 hashed).

`-1` is kept as legitimate: it is the codebase's "unset" sentinel, the same
convention as the weapon fuse timers.

## Still deferred

Multiplayer — four player slots and the per-player stats block — until the MP
track exists. There is no way to populate or verify it from a solo corpus, and
an unverifiable field is the risk pattern this schema keeps running into.

## Verification

- Offsets checked against real recordings BEFORE being hashed, then re-checked
  through the real `StateReader`: all three levels PLAUSIBLE.
- `make native-skeleton` builds the C half.
- Corpus rebaselined and gated: **GATE PASSED — 20 campaign levels at 10 runs,
  97 sweeps at 3.**

## Probe lessons, recorded because they cost most of a day

Three probes produced confident numbers while measuring nothing:

1. OR-ing chrflags across a run — cannot detect a flag set then cleared, which
   was the exact question.
2. Memory writes that never landed — `numarghs` stayed 0 while "forcing" it
   to 10.
3. `max_ticks` past the end of the input stream — every aggregate poisoned by
   post-teardown memory. Recording length is `input_size / 4`; stay inside it.

The rule that caught all three: check the measurement against what the values
must look like, before believing the result. Ammo cannot be -2,105,112,956;
hit counters cannot count down.
