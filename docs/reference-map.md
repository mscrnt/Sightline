# Reference map — Zoinkity's GE notes

`/mnt/projects/goldeneye_docs/notes/GE Documentation/` — 617 files, 8.4 MB of
disassembly-level notes. This maps the parts that bear on Sightline's tracks so
nobody re-derives what is already written down. Paths are relative to that root.

**Authority warning.** `Rand++ Documentation (Misc)/` (121 files) documents a ROM
hack's *modified* formats, not the base game. Its "new format" tables describe
what the hack proposes, not what our ROM does. Treat it as inspiration only.

## Native port / endianness

- `Gameplay and Misc/Goldeneye eeprom saves.txt` — save block format. Bears
  directly on the save-endian fix (file2.c): checksums and options are read as
  values out of a cartridge-order image.
- `Setup File Microcodes and Paths/objects/object block types.txt` — per-type
  setup object layouts with field offsets.
- `Setup File Microcodes and Paths/Action block microcode/Action Block Types.txt`
  — setup object type table with word counts (01 Door 0x40, 08 Weapon 0x22,
  09 Guard 7, 06 Camera 0x3B). Matches the decomp's `sizepropdef()`.
- `Background File Data/` — bg, portal and stan file formats:
  `Portal (port) Lists/port format.txt`, `Clipping (stan) Files/clipping files.txt`,
  `Clipping (stan) Files/tile roving.txt`, `Clipping (stan) Files/CLIPPING order flags.txt`.

## Visibility / culling  (the tick-113 ONSCREEN divergence)

- `Background File Data/Global Visibility Microcode/global visibility types.txt`
  and `commands.txt` — how rooms are determined visible. `posIsOnScreen()` sits
  on top of this via `getROOMID_isRendered()`.
- `expando guard.txt` (root) — the 0x34-byte position record: type codes
  (1 normal, 2 door, 3 guard, 4 weapon pickup, 6 player, 7 explosion) and state
  bits, where **bit 2 = "actually on screen"**. This is the flag that mismatched.

## AI track — pathing, guards, perception

- `Setup File Microcodes and Paths/Paths/path dissection.txt`, `path load routine.txt`,
  `amendment to fully read path data.doc` — per-level path data format. This is
  the level-specific navigation data that only that level's trace exercises.
- `Setup File Microcodes and Paths/generate guards.txt`
- `Setup File Microcodes and Paths/Action block microcode/7F03415C - guard constructor.txt`
  — takes body, head, preset, pad; returns a guard id, never below 3.
  Confirms identity is the body+head PAIR.
- `Actor-Specific Data and Animations/Initialization/7F000EB8 - allocate and intialize GUARDdata entries.txt`
  — GUARDdata (chr record) allocation and initial field values.
- `Gameplay and Misc/Damage and Kill Attributement.txt`,
  `Gameplay and Misc/Character hit locations.txt` — damage model and hit regions.

## Schema v11 candidates

- `Setup File Microcodes and Paths/objectives and briefings/objective types.txt`
  — setup format: create 0x17, end 0x18, destroy 0x19, complete-if-true 0x1A,
  fail-if-true 0x1B, collect 0x1C, each with a minimum-difficulty field.
- `.../7F057238 - return objective status.txt` — runtime per-objective status,
  up to 10 objectives, table at 0x80075D30. Our schema hashes only
  `objective_count` plus one register, which is far coarser.
- `Objects and Attributes/Expansion/08 weapons/08 weapon object type.txt` —
  weapons carry bitflag 0x4000 "assigned to a guard". Needed to replace the raw
  `weapons_held` pointers with weapon identity.
- `Objects and Attributes/ammunition/` — ammo types, value tables, maximums,
  collection behaviour (`ammo types and amounts.txt`, `ammo value tables and
  maximums.txt`, `ammo on object collection.txt`).
- `Objects and Attributes/destruction table.txt` — destructible objects.
- `Setup File Microcodes and Paths/objects/object block types.txt` line ~236 —
  surveillance camera (type 06) with centre preset, rotation limits, turn speed.

## Harness parallels

- `Gameplay and Misc/Ramrom Demos/` — the game's OWN input recording system,
  including `7F0BFE5C - record controller input as packet.txt`. Our trace
  harness solves the same problem; worth reading before changing input replay.

## Index

`/tmp/index_docs.py` regenerates a full TSV catalog (path, size, first line) at
`/tmp/docs-index.tsv`. Rebuild it rather than grepping blind.
