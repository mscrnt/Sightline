# Divergences

Every intentional behavioral difference from the matching build, recorded here
before it is re-baselined.

## Why this file exists

The matching build is the correctness oracle. When a trace diverges from it,
exactly one of two things is true:

- **Unintentional** — it is a bug. Fix the code, not the baseline. Nothing goes
  in this file.
- **Intentional** — it is a deliberate change. It gets an entry below, *then*
  gets re-baselined in a separate commit that touches nothing else.

Silently re-baselining a divergence destroys the oracle's value: the next
regression has no clean reference to diff against, and nobody can tell which
differences were chosen and which crept in.

## Rules

1. Document **before** re-baselining, never after.
2. The re-baseline commit contains the new traces and nothing else.
3. Behavior changes default to **off**. Per project rule 5, an intentional change to
   Rare's behavior goes behind a runtime toggle defaulting to original behavior.
   Record the toggle name below.
4. Entries are append-only. Superseded entries get a `Superseded by:` line rather
   than being deleted.

## Format

Copy this block. Newest entries at the top of the log.

```markdown
### D-000 — one-line summary

- **Date:**        YYYY-MM-DD
- **Phase:**       0 | 1 | 2 | 3 | 4 | 5 | AI track
- **Commit:**      <sha of the change; the re-baseline sha goes on its own line>
- **Re-baseline:** <sha of the trace re-baseline commit>
- **Levels:**      facility, silo, ... (or: all)
- **First tick:**  <tick index where the divergence first appears>
- **Fields:**      <which hashed state fields differ — e.g. guard[7].path_state,
                    rng_cursor, alarm_flags>
- **Toggle:**      <runtime toggle name, and its default> (or: none — see rule 3)

**What changed**

<The behavioral difference, in terms a reader can verify against the game.>

**Why**

<Why this was worth breaking parity for. "It looked wrong" is not a reason;
"it softlocks guard AI on Silo when the alarm triggers during a body drag" is.>

**Why it is safe**

<What was checked. Which other levels were replayed, what did not move.>
```

---

## Log

### D-056 — EGYPTIAN'S Q BRANCH PAGE CARRIES A RECOVERED FLOOR PLAN (native front end): on el-Saghira's pre-mission briefing, Q Branch page only, a line "Attachment: Recovered floor plan" under Q's text opens an annotated sketch of the Golden Gun's chamber with the safe route over its trapped floor; closed by default and on every page change; no other briefing, page or mission state changes (native-only screen change; the cartridge's briefing is untouched under `__sgi`)

- **Date:**        2026-10-14
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted): src/native/sl_brief_plan.c (NEW - the line, the sheet, its input), src/game/front.c (native arms in `init_menu0A_briefing`, `interface_menu0A_briefing` and `constructor_menu0A_briefing`)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      n/a (the front end; Egyptian's briefing, stage 32)
- **First tick:**  n/a
- **Fields:**      none hashed
- **Toggle:**      none. The attachment exists in the native build only; the `__sgi` arm draws and runs Rare's briefing.

**How it is reached**

The front end, not a direct boot: SELECT MISSION, Egyptian (the last tile of
the bottom row, offered once the folder has unlocked it), a difficulty, then NEXT through PRIMARY OBJECTIVES, BACKGROUND and M
BRIEFING to Q BRANCH. A direct boot into the stage (`play.ps1 -Level egypt`,
`SL_BOOT_LEVEL`) skips the briefing and never shows it. The stage is the
briefing entry's own (`mission_folder_setup_entries[briefingpage].stage_id`,
the value the briefing loads its text by), never the last stage loaded.

**Controls**

- The line is under the one front-end cursor - stick, WASD / arrow keys and
  mouse move it alike - and is hit-tested in the box it is drawn in; on it the
  line takes the menus' highlight box and the tabs are not highlighted.
- A or Z (Enter on the keyboard, a left click) on the line opens the sheet.
  While it is open, A, Z or a click anywhere closes it, and so does B; none of
  those presses also acts on a tab or turns the page.
- B with the sheet closed goes back a page as always. START launches the
  mission from either state.
- On the keyboard the same edges are Enter (A), Space and Escape (B in the
  front end) and Tab (START); the arrows and WASD move the cursor.
- Turning the page, leaving the briefing or entering it again closes the
  sheet.

**The sheet** (`sl_brief_plan.c`, its header comment has the derivation)

A 5 x 12 plan of room 19 drawn in the briefing's font and the menus' filled
box, entrance at the foot: the doorway from room 18's corridor (column 2 of
the west end), the pedestal (the Golden Gun's case, world 2575, 2583: column
2, row 3), the four sealed bays (the stone blocks whose ten covers hide the
drone guns: columns 0 and 4 of rows 0 and 6), and the route stone by stone
from an arrow at the doorway to an arrowhead at the pedestal's face. The
legend: "Excavation survey, inner chamber." / "Safe stones, in order" /
"Pedestal" / "Sealed bay" / "Keep to the line."

The grid is the room's bounds (sl_map, x 1011 .. 3222, z 2117 .. 3038) over 5
and 12, 184 world units a stone, the pitch at which the trap pads stand
(setup units x 3.905, the room scale prop.c applies to pads at load); column
0 is the low-z side, the left on entering (measured: on column 0 facing east
the low-z wall fills the left of the view).

**The route's static check** (not a replay of the mission). `ai_17`
(UsetupcrypZ.c) springs the trap when Bond comes within 9 decimetres (90
world units - chrai.c, `ntohs(DISTANCE) * 10.0f`) of pads 0x97 .. 0xae other
than 0xa6, and opens the case within 10 (100) of 0xa6. Exact distances,
point to point and point to segment: every stone of the route is at least
165.07 units from every trap pad, every straight step between two stones at
least 164.875 (pad 0x9e, the step from (3,9) to (3,8)), and 0xa6 lies 23.25
units from the centre of (2,4), the stone before the pedestal. The grid, the
doorway, the pedestal and the bays are derived from the stage's data; the
sequence of stones follows the supplied sketch and is what this check
validates. Of the six ways to lay
the grid with row 11 at the west doorway - mirrored or not, or a row off
either way - only the one on the room's bounds keeps the route clear; each
other puts a stone within 7 units of a trap pad. Distances are horizontal:
the pads stand 90 units over the floor (457 against 367), and the trap
compares Bond's prop position in three dimensions, so the true clearance is
at least the stated one.

**Why it is safe**

- `src/game/front.c` is identical to the base commit's outside its
  `#ifndef __sgi` blocks (23 such blocks before, 28 after; the 8872 lines the
  matching build reads are the same, an `#else` arm compared as the matching
  build's code; a one-line change outside a block is reported as a
  difference). The matching build compiles `src/*.c` and `src/game/*.c`
  only, so no other file of this change reaches the matching ROM, and the
  trace harness - which replays that ROM, booting each stage directly - never
  enters the briefing.
- The state the native arms write is the attachment's own (open, hover, the
  last drawn box) and the briefing's three tab-highlight flags for the frame;
  the only flow they suppress is the B arm on the press that closes the
  sheet. Nothing hashed, no mission or setup state.
- Real front end, scratch configuration and a scratch EEPROM made by
  `tools/save/ge_unlock_save.py`, the machine's physical controllers hidden
  from the run (SDL hints), mouse moves and clicks by `SL_POINTER_PROBE`,
  pad presses and stick by `SL_PAD_SCRIPT`:
  - mouse and pad: the line hovered, a click opens, B closes on Q Branch, a
    click reopens, A closes on Q Branch, A away from the line turns to
    MONEYPENNY, B back to Q Branch finds the sheet closed, reopened, START
    with the sheet open closes it and launches - the stage loads, its intro
    plays and play begins;
  - stick and buttons alone from the NEXT tab: hover, A opens, B closes on Q
    Branch, A reopens, START launches;
  - Dam and Train: their Q Branch pages carry no line; on Dam's, a click
    where it would be acts as the original (NEXT, to MONEYPENNY);
  - Dam's briefing viewed first, then Egyptian's: the line, and it opens; Dam
    viewed again afterwards: no line, no hover;
  - Dam launched and aborted from the watch (the report reads ABORTED), back
    to the mission select, then Egyptian: the line, and it opens;
  - the keyboard path, driven by key state injected at `sl_input.c`'s
    keyboard poll in a test build (the file's own key-to-menu mapping
    unchanged; Escape and Tab delivered through `sl_input_live_escape` /
    `sl_input_live_start`, the calls the SDL key-down handler makes): arrows
    onto the line, Enter opens, Space closes on Q Branch, Enter reopens and
    closes, W / S off the line and back, Escape with the sheet closed goes
    back to M BRIEFING, Enter returns to Q Branch, Escape with the sheet open
    closes it on Q Branch, Tab with the sheet open launches the mission.
- The 19 accepted stages booted at their own start under ORIGINAL and XBLA:
  38 of 38 frame sets byte-identical to the Aztec-accepted build's.

Not claimed: a physical keyboard was not used - the key events were
injected at the poll, below the SDL device layer; the route was not walked
in the game - the check is static, against the setup's pads; the sketch is a
plan, not a measured survey of the room's floor art.

### D-055 — EGYPTIAN'S FADE TO BLACK REACHES THE REMASTER (Egyptian): under PRESENTATION XBLA the remaster's environment row 132 - Egyptian's second environment, which the game fades to after Baron Samedi's second death - is drawn from 345 to 1550 world units instead of its authored -100 .. 10000, so the rooms close into black across the fade as the cartridge's do; Egyptian's own row (32) stays authored; default ON within XBLA, `SL_XBLA_FADE_FOG=0` draws the authored row (native-only, render-only, the D-025 / D-043 class)

- **Date:**        2026-10-14
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted): src/platform/sl_presentation.c (`s_fog_range` and `s_fog_start` rows for 132, `fog_row_on`, self-test 9b), src/platform/sl_presentation.h (the two tables' notes), src/gfx/sl_gfx_worldasset.c (the load-time report names D-055)
- **Re-baseline:** none required - no simulation code is touched
- **Levels:**      egypt (environment 132 of the xbla set's `environment.slen`)
- **First tick:**  n/a - no hashed state moves
- **Fields:**      none. Pixels only, and only after the fade has begun
- **Toggle:**      `SL_XBLA_FADE_FOG=0` draws row 132 at its authored range; `SL_XBLA_AUTHENTIC_FOG=1` (D-025's) draws every row's authored range. Developer diagnostics, not settings. ORIGINAL and COMMUNITY HD select no world-asset set and read no row.

**What the game does**

`ai_24` (UsetupcrypZ.c) runs `gas_leak_and_fade_fog` once objective bit
0x00200000 is set, after Baron Samedi's second death. On Egyptian that
command starts no gas: `init_trigger_toxic_gas_effect` (propobj.c) sets a
120-tick fade and no damage, and `fogSwitchToSolosky2` (bgfog.c) blends the
stage's environment toward its ALT row (`LEVELID_EGYPT +
ENVIRONMENTDATA_ALT`, 132): far fog 20000 to 10000, fog band 996..1000 to
972..1000 per mille, fog colour 16 48 96 to black, clouds white to 83 10 10.
Under near 10 and far 10000 at the stage's visibility scale 1.0, the ALT
band is 1.0358 - 357.5 / d of the fog colour: clear to 345 world units, 44%
at 600, 68% at 1000, 86% at 2000.

**What the remaster holds**

Its environment table (`environment.slen`, read from the user's executable,
tools/worldasset/xexenv.py) carries both rows: 32 at -100 .. 20000 toward
16 48 96, and 132 at -100 .. 10000 toward black, both linear in distance.
Sightline blends the two rows as the game blends its own (#77,
`env_in_force_row`), and the blend happens, but row 132 is 7% black at 600
units, 11% at 1000 and 21% at 2000.

**What Sightline did before**

The fade was drawn with the authored row 132. Measured, teleport only, seed
ffffffff97b3f64d, `egypt` difficulty 1, the game's own fade
(`init_trigger_toxic_gas_effect`) called at a fixed poll by a scratch
capture build that is not part of this change - a forced state for a
presentation comparison, not Baron Samedi's deaths or the mission's
progress: mean frame luma over four headings (reads 120, 180, 260, 340
before; 540, 620, 700, 820 after) at four poses - rooms 31, 3, 12 and 19 -
fell 30.5% across the fade under ORIGINAL and 6.2% under XBLA. ORIGINAL's
rooms close into black past a few metres; XBLA's dimmed only at the far end.
A fifth pose, room 10, was captured and is left out of every figure here: in
its frames before the fade the eye stands inside Baron Samedi's model (with
it the means read 31.3% and 8.9%).

**What Sightline does**

Row 132 is presented from 345 - where the game's own fog on that row begins,
10 / (1 - 0.972 x 9990 / 10000) - to 1550; its colour stays authored (black,
the game's own). The end is a design value: trials with the same four poses
at 1500, 1600 and 2600 gave -30.5%, -28.3% and -15.7% against ORIGINAL's
-30.5%, and 1550 lands between the first two. Row 32 is untouched, so nothing changes before the fade, and
the blend toward 132 carries the presented range as it carried the authored
one. Props and characters fogged by the remaster's row follow it, as they
follow every row.

**Why it is safe**

Measured as above with the change built in:

- XBLA falls 29.4% across the fade (ORIGINAL 30.5%): room 31 -17%, room 3
  -36%, room 12 -26%, room 19 -39% (room 19's drone guns fire in every arm
  alike);
- every frame before the fade starts is byte-identical to the authored
  XBLA's (15 of 15 per pose, through read 380, all five poses), and the
  frames differ only from read 400, as the fade begins;
- `SL_XBLA_FADE_FOG=0`: all 195 frames of the five poses byte-identical to
  the authored XBLA's, and the load-time lines that report row 132 presented
  are absent; `SL_XBLA_AUTHENTIC_FOG=1`: the same 195 of 195;
- ORIGINAL with the change built in: 195 of 195 frames byte-identical to
  ORIGINAL before it (ORIGINAL and COMMUNITY HD read no remaster row); the 19
  accepted stages at their own start, ORIGINAL and XBLA: 38 of 38 frame sets
  byte-identical to the Aztec-accepted build's (no other stage's row is
  named);
- the presentation self-test (`tools/windows/presentationtest.ps1`, 101
  passed): 132 at 345 .. 1550 under xbla, 32 authored, no other set reached.

Not claimed: the remaster's fog is linear in distance and the cartridge's
is not, so near the start the remaster's darkens more slowly and toward the
end faster; the match is of the four poses' mean, not of every distance or
room. The captures force the fade; Baron Samedi's deaths and the mission's
end were not played under either presentation.
The remaster's sky dome is drawn as authored and does not take the game's
red clouds.

No file under src/game is touched.

### D-054 — EGYPTIAN'S CRYPT DOORS ARE OPAQUE, THEIR FACES IN THE REMASTER'S OWN ART (Egyptian): under PRESENTATION XBLA the five crypt-door models whose remastered items put every draw in the translucent pass at vertex alphas of 0 .. 255 - cryptdoor1a, 2a, 2b, 3 and 4, twenty doors, opaque stone in the game - are prepared opaque, and each draw whose game texture the remaster's Egyptian background draws with one image of its own draws that image, and a face that the game draws as the wall round it continues that wall's image at the wall's texture map (the secret wall into room 10); an item none of whose draws has such an image (cryptdoor4) is not prepared and the game's door draws; default ON within XBLA, `SL_WA_AS_AUTHORED=D-054` at preparation prepares the five items as authored (native-only, render-only, preparation-time, the D-051 class)

- **Date:**        2026-10-13
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted): tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-054']`, `translucent_only`, `DOOR_BG`, `door_face_art`, `_bg_level`, `DOOR_STAGE`, `FACE_COS`, `WALL_MAP_TOL`, `level_images`, `continue_wall`, the D-054 block of `convert_prop`), tools/worldasset/gameshade.py (`prop_blends`, `prop_draw_textures`, `prop_triangle_textures`, `triangle_textures`, `draw_textures`, `BG_IMAGE_SHARE`, `bg_texture_images`, `texture_images`, `door_placements`' `thin`, `door_plane`, `wall_maps`, `wall_textures`, `door_walls`), tools/worldasset/selftest.py (`opaquedoortests`)
- **Re-baseline:** none required - no simulation code is touched
- **Levels:**      egypt (the props `cryptdoor1a`, `cryptdoor2a`, `cryptdoor2b`, `cryptdoor3`, `cryptdoor4`)
- **First tick:**  n/a - no hashed state moves
- **Fields:**      none. Pixels only. The doors' state, collision and movement are the game's
- **Toggle:**      preparation-time. `SL_WA_AS_AUTHORED=D-054` with `.\tools\windows\prepare-xbla-assets.ps1 -Level egypt -Props` prepares the five items as authored (translucent, the source's own images). ORIGINAL and COMMUNITY HD read no bundle. The rule reads the prepared Egyptian background (`background/temple`) beside the props; without it the five items are refused and the game's doors draw.

**What the game does**

Twenty of Egyptian's 25 doors are crypt doors of these five models: the
secret walls between room 10 and rooms 42 and 43, the upper leaf of the door
between rooms 19 and 20, the doors of room 20's far end, and the ten covers
of the drone guns' alcoves round the Golden Gun's room (the alcoves open as
the guns deploy). Every triangle of each `P<name>Z` is at corner alpha 255
under no blended render mode, and the doors' flags (0x04, 0x08, 0x0c) carry no
`DOORFLAG_WINDOWED`, so the game draws them under no opacity of its own
(propobj.c: the environment alpha is 0 for such a door): opaque stone, dark
by their vertex colours.

**What the source holds, and the remaster draws**

The items of all five, in both of the source's generations, put every draw in
the translucent pass (gate 0x4702) - the same 32x32 stone images the game
uses - at vertex alphas 0 (2a, 2b), 0 .. 63 (4) and 0 .. 255 (1a, 3). The
private emulator (free camera at the landing eye) draws the doorways open:
room 10's pillar through the secret wall from room 43, room 20's pillar
through the door from room 19. The sixth model, `cryptdoor1b` (the lower
leaf), is opaque in the source with two 512x512 images, and is taken as any
door item is.

The source holds no image of its own for the doors' faces. The XBLA texture
set carries none of the game's textures 406 (the grey relief face of 1a, 2a
and 3), 1174 (the lower leaf's face, 2b) or 410 and 404 (the frames); the
remaster's background draws the room surfaces those textures cover in the
game with its own images.

**What Sightline did before**

This change's first preparation drew the four items it read (the fifth,
`cryptdoor4`, was refused by the reader) blended at their vertex alphas: a
misty grey-green leaf where ORIGINAL has a stone door, the secret walls nearly
see-through, and the alcove covers gone so the drone guns stood bare in front
of the walls. Its second refused all five, so the game's own doors drew: the
game's coarse 32x32 grey relief over a brown leaf beside the remaster's
carved walls (room 20's far end, eye 2661.1 534.4 1852.5, theta 177.06,
verta -3.25: the door of `cryptdoor2a` over `cryptdoor2b` at 2626.1, 1589.3).

**What Sightline does**

For the items `DIVERGENCE_ITEMS['D-054']` names, an item every draw of which
is in the translucent pass alone (`translucent_only`), over a game model with
no blended triangle (`gameshade.prop_blends`: no corner alpha below 255, no
forced blend or translucent depth mode), is prepared OPAQUE (`door_face_art`):
every draw in the opaque pass, every vertex at alpha 255, the source's own
vertex colours. Each draw's game texture is found from the game model
(`gameshade.prop_draw_textures`: the game triangle under each item triangle),
and the background image the remaster draws where the game draws that texture
is measured (`gameshade.bg_texture_images`: background triangles on the game's
room planes, matched on a 64-unit grid). A draw whose texture is drawn by one
image on at least `BG_IMAGE_SHARE` (75%) of those triangles draws that image;
the others keep their own 32x32 stone, and the legacy images no draw names
are left out. Measured on Egyptian:

| game texture | background image | room triangles | drawn by |
|---|---|---|---|
| 406 (face of 1a, 2a, 3) | `_0x0DB022C5`, 512x512 papyrus capitals | 24 of 24 (100%) | draw 1 of 1a, 2a, 3 |
| 1174 (face of 2b) | `_0x0DA12A45`, 1024x512 relief frieze | 475 of 592 (80%) | draw 1 of 2b |
| 410 (frames, all of 4) | none above 39% | - | own stone |
| 404 (the room-10 face of 3) | none above 22% (none on 945 of 1511) | - | own stone, but see below |

**A door's face continues its wall.** Where the setup places the door
(`DOOR_STAGE`, `gameshade.door_placements`) and the wall round it
(`in_door_wall`, D-052's wall) faces the way the door model's +z face does,
each triangle of that face whose draw took no image level-wide
(`continue_wall`) draws what the wall draws at its height: the background's
image there at the wall's own texture map - u and v as planes over the door's
face (`gameshade.wall_maps`) - when every wall triangle at that height draws
the one image on one map (within `WALL_MAP_TOL`, 0.01 of an image), and the
game draws the wall at that height in the very texture it draws the door's
triangle in (`gameshade.wall_textures`, `prop_triangle_textures`). That last
condition is the game's own sign that the face continues the wall rather than
being trim of its own. A door placed more than once takes a triangle only when
every placement agrees. The taken triangles move to a draw of their image on
copies of their vertices (the frame's sides keep theirs and their stone);
positions, colours and skin are unchanged.

The secret wall into room 10 (`cryptdoor3`, bound pad 4, one placement, at
966.5, 183.5, -3776.2) closes a gap in room 10's west wall (x 1011), flush
with it: the wall lies 11.9 room units from the door's centre plane, the
door's half-thickness. The game draws its room-10 face and the wall either
side alike at every height: 410 from the floor to 94 world units, 404 to 277,
410 above. The remaster draws the wall's 410 and 404 bands with image 2 (the
papyrus capitals) as one upright copy on one map: u 0.0132 per room unit,
equal either side of the gap to 0.0007; v from 1.0 at the floor to 0.0 at
277. Above 277 it draws the carved upper wall (image 9). Under the level-wide
rule alone the face kept the source's stone in three pieces: the 404 panel
(draw 2, 16x64) and the frame draw's two 410 strips (32x32), one below the
panel and one above. That was a blurred strip in the middle, and once the
panel took the capitals, a plain band below and above them. Continued, the
face's six triangles draw the wall: the panel and the lower strip the
capitals (u 3.606 .. 4.240, v 1.0 .. 0.004, one copy from the floor up), the
upper strip the carved wall (v 1.0 .. 0.940). The door's other face (room
42's, the end of a passage) has no wall facing its way and keeps the
level-wide rule: its 406 panel the capitals, its strips stone. No other crypt
door has a wall facing its +z face within reach (`cryptdoor1a`, `2a`, `2b` and
`4` take nothing), so the rule changes `cryptdoor3` alone.

An item none of whose draws takes an image is not prepared - "D-054:
prop/cryptdoor4 draws every draw in the translucent pass, the game's model
(12 triangles) blends none and the background owns no image for its textures -
the game's door draws" - and with no item the game's door model draws under
XBLA as under ORIGINAL (the dynamic door's contract). The share threshold
separates the faces (80% and above) from the frames (39% and below) with
neither near it. Without the ROM the check is not made and the item is
prepared as authored. No image is invented, recoloured or composited: each
face is a whole image the remaster already draws on Egyptian's walls, at the
item's own coordinates.

**Why it is safe**

Measured, teleport only, seed ffffffff97b3f64d, `egypt` difficulty 1:

- room 20's far end at the eye above and at theta +-10, verta +-5 and x +-40:
  the upper door draws the papyrus capitals over the frieze band, opaque, the
  room beyond not showing, in the tone of the walls beside it; ORIGINAL draws
  its grey stone;
- one use press in front of that door (z 1700, then stepped back to z 1840):
  the door rises between reads 150 and 240, stands open, and closes between
  reads 1200 and 1290, solid throughout, at ORIGINAL's timing (sampled every
  30 reads);
  the movement and collision are the game's;
- room 20 both ends, room 19's alcove covers and the secret wall from room 42,
  four headings: the faces in the remaster's art, the covers closed;
- `SL_WA_AS_AUTHORED=D-054` prepares the six crypt-door items byte-identical
  to the as-authored items of the first preparation; the other Egyptian props
  prepare byte-identical; no item of another stage is named;
- the worldasset tool self-test (`opaquedoortests`: `translucent_only`,
  `prop_blends`, `draw_textures`, `triangle_textures`, `texture_images`,
  `level_images`, `door_face_art`, and for the wall `door_plane`, `wall_maps`,
  `wall_textures`, `continue_wall` - taken at the wall's coordinates, refused
  for another game texture, two images, two maps, two game textures, no wall
  at the height, another thin axis, disagreeing placements, no game texture,
  a level-wide draw, the face turned away - and the moved triangles' copied
  vertices, emptied draw and kept geometry), 847 passed, 0 failed;
- the secret wall's room-10 face, the whole door from either side (x 1450,
  z -3600 and -3950), straight on (x 1250), its lower and upper bands (verta
  -20 and +20), and at the eye 1182.6, 167.3, -3721.5 (theta 107.46, verta
  -3.25) with theta +-10, verta +-5 and 80 units back: XBLA draws the face as
  the wall either side, the capitals from the floor and the carved upper wall
  above, with no stone band; ORIGINAL at the mark and from the left, and the
  room-42 face under XBLA close and from x 650, draw byte-identical frames
  before and after; a use press from room 10 leaves the door at 966.5,
  183.5, -3776.2 under both presentations;
- the triangles' positions, colours and skin are unchanged by the move
  (`cryptdoor3`: 28 triangles, 48 vertices before and after);
- `SL_WA_AS_AUTHORED=D-054` still prepares the five items byte-identical to
  the earlier control; `cryptdoor1a`, `2a`, `2b` and every other Egyptian prop
  prepare byte-identical, and `cryptdoor4` is still refused; Aztec's 26 props
  prepare byte-identical.

Not claimed: the frames' sides and the face of a door with no wall facing its
way draw the game's 32x32 stone, the remaster owning no image that draws
their texture - so the secret wall's room-42 face keeps stone strips above
and below its capitals, the end of a passage. `cryptdoor4`'s doors are the
game's. The wall rule's evidence is one wall, one door. The door's room-10
face lies 0.4 room units behind the wall's surface (its pad box -12 .. 11
about x 248, the face at 259.0 against the wall at 259.4) and is 0.4 wider
than the gap either side; a one-pixel line at each of its vertical edges,
in the same place before and after this rule, is not changed by it. Between
the face's 404 panel (to 23.5 in the door's t) and the wall's image 2 band
(to 23.8) 0.3 room units of the upper strip draw image 9 where the wall
draws image 2. The face is not captured with the secret wall opening.

No file under src/game is touched.

### D-053 — AZTEC'S SHUTTLE IS THE SOURCE'S ORIGINAL GENERATION (#96): under PRESENTATION XBLA Aztec's shuttle item is converted from the source's ORIGINAL generation of `prop/shuttle` - the game's model, its "4" and emblem decals on the wings' undersides where the game draws them - with its decals drawn at the alpha the game blends them at; and Aztec's background withholds the remaster's two image-less stand-ins of those decals, which drew as pale squares; default ON within XBLA, `SL_WA_AS_AUTHORED=D-053` at preparation prepares the remaster's shuttle and background as authored (native-only, render-only, preparation-time, the D-051 class)

- **Date:**        2026-10-12 (revised 2026-10-13: the whole original generation in place of a carried "4")
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted) - Gitea #96: tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-053']`, `ORIGINAL_GENERATION`, `decal_stand_ins`, the D-053 blocks of `convert_prop` and `prepare_background`), tools/worldasset/gameshade.py (`model_triangles`' mode, `prop_decal_alpha`, `decal_alpha_draws`, `describe_decal_alpha`), tools/worldasset/selftest.py (`originalgentests`)
- **Re-baseline:** none required - no simulation code is touched
- **Levels:**      aztec (the background `aztec` and the item `shuttle`)
- **First tick:**  n/a - no hashed state moves
- **Fields:**      none. Pixels only. The shuttle's model node, placement and launch are the game's
- **Toggle:**      preparation-time. `SL_WA_AS_AUTHORED=D-053` with `.\tools\windows\prepare-xbla-assets.ps1 -Level aztec -Background -Props` prepares both items as authored: the shuttle byte-identical to the remaster's own (the atlas triangles, 4 draws, 265113f4), the background with its stand-ins. ORIGINAL and COMMUNITY HD read no bundle.

**What the game does**

`PshuttleZ` draws a "4" (texture 991) on the left wing's underside and the
round emblem (texture 585) on the right, over the orbiter's orange flanks,
and an orange chevron (texture 799, 16 triangles) over its nose - all three
in the model's own list under othermode_l 0xc4104dd8 (the decal depth mode,
blending forced) at vertex alphas 89, 89 and 191, so they read faint and
worn (playtest run 20261013-075405 marks 1 and 2, ORIGINAL).

**What the source holds**

The ORIGINAL generation of `prop/shuttle` is the game's model in the item's
space: 13 draws on 8 images, 850 triangles. The "4" (draw 11,
`_0x0CDDBCA5.tga`) and the emblem (draw 12, `_0x075DD185.tga`) are
translucent decal-pass draws (gate 0x2702) and the chevron (draw 2) an
opaque one (gate 0x2701); every vertex alpha is 255. The NEW generation is
reworked - a 512x1024 photograph atlas over the wings, an engine shroud of
folded sheet - drops both decal draws, and prints the two markings into the
atlas itself, swapped: the emblem on the left wing and a black "4" roundel
on the right. The private emulator draws the remaster so at the playtest
marks (run 20261013-075405, marks 2 and 4). The atlas bakes both markings
into one photograph with no clean wing beneath either, so no derivation from
the new generation restores the game's layout; a chosen-footprint exchange of
the two discs (scratch) left a visible ring round the moved emblem.

The remaster's background holds two untextured quads where the decals were
(pool 12 of `background/aztec`, triangles 16 .. 19 of draw 0x4af8: the
decals' sizes and vertex colours, at alpha 128); the remaster draws them as
pale squares.

**What Sightline did before**

Drew the remaster's shuttle and its background's two pale squares (a plain
patch where ORIGINAL has the "4"); then, in this change's first form, the
remaster's shuttle with the original generation's "4" carried onto the
left flank - under the atlas's printed emblem, the roundel still on the
right ("The 4 is there, but i think the decals aren't right... i want it to
be placed like the original").

**What Sightline does**

- For the items `ORIGINAL_GENERATION` names - the shuttle - the item is
  converted from the source's original generation: its draws, images,
  coordinates and decals, through the same conversion as any static item.
- Its decal-pass draws take the game's blend (`gameshade.prop_decal_alpha`):
  each triangle lying on a game triangle drawn in the decal depth mode with
  blending forced under a corner alpha below 255 (planes within cos 0.999,
  within 0.005 of the item's extent, centroid inside) has its corners' alpha
  multiplied by the game's there, on copies of the corners; an opaque draw
  wholly on such triangles moves to the translucent pass. Prepared: "prop/
  shuttle: 13 draws (3 blended) (3 decal), 8 images, 940 vertices, 850
  triangles ... (D-053: the original generation; draw 2: 16 at 191/255 of 16
  triangles (opaque: drawn translucent); draw 11: 2 at 89/255 of 2
  triangles; draw 12: 2 at 89/255 of 2 triangles)".
- The background withholds every untextured triangle under a
  translucent-only pass gate whose three corners are neutral grey
  (`decal_stand_ins`): those four triangles and nothing else in Aztec's
  background (the pad's markings are yellow, the exhaust shapes orange and
  brown). "D-053: 4 image-less decal stand-in triangles in 1 draw withheld".

**Why it is safe**

Measured, teleport only, seed ffffffff97b3f64d, the four marks of playtest
run 20261013-075405 (room 66):

- marks 2 and 4: the "4" stands on the left wing's underside and the emblem
  on the right, where ORIGINAL draws them; the remastered silo, trees and
  bay lettering stand round it. Each mark's frame is byte-identical over two
  runs;
- the decals' contrast at mark 2 (luma p95 - p5 in a 60-pixel box round each):
  the "4" 32 and the emblem 60, against ORIGINAL's 22 and 42; drawn at the
  source's alpha 255 they read 81 and 166;
- `SL_WA_AS_AUTHORED=D-053` prepares the shuttle byte-identical to the
  remaster's own conversion before this change (265113f4); re-prepared, the
  Aztec background and every other installed Aztec item are byte-identical
  to before, and the 54 non-Aztec bundles of the preservation set prepare
  byte-identical;
- the worldasset tool self-test (nine mutations of the rule - the mode read,
  the alpha scale, the pass move, the split, the decal-pass test, the 255
  test, the plane distance, the skin copy, the item list - each fail it).

Not claimed: the shuttle's shading. Under the remaster's lighting the
original model reads brighter than ORIGINAL, and the right wing's orange
fades toward white along its gradient; the vertex colours are the source's
(the "4" corners 102 where the game's are 120, the emblem's 136 where the
game's are 152). The shuttle is the game's model at the game's detail, not
remastered art.

No file under src/game is touched.

### D-052 — AZTEC'S STONE DOOR TAKES ITS WALL'S TONE (#96): under PRESENTATION XBLA the remaster's `door_aztec` - the temple's secret stone door, authored as a pale grey block - has its vertex colours set so its tone is the remaster wall's round it times the game's own door-to-wall ratio, so it reads as the wall's stone, a little lighter, as the game's door does; default ON within XBLA, `SL_WA_AS_AUTHORED=D-052` at preparation prepares it as authored (native-only, render-only, preparation-time, the D-032 class)

- **Date:**        2026-10-12
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted) - Gitea #96: tools/worldasset/gameshade.py (`prop_game_shade`, `shade_vertices`, `door_placements`, `in_door_wall`, `door_wall_shade`), tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-052']`, `DOOR_WALL`, `item_face_tone`, `bundle_wall_tone`, `door_wall_tone`, the D-052 block of `convert_prop`), tools/worldasset/selftest.py (`propshadetests`, `originalgentests`)
- **Re-baseline:** none required - no simulation code is touched
- **Levels:**      aztec (the item `door_aztec` alone)
- **First tick:**  n/a - no hashed state moves
- **Fields:**      none. Pixels only. The door's placement, opening, collision and sound are the game's
- **Toggle:**      preparation-time, needs the ROM and the background bundle beside the props. `SL_WA_AS_AUTHORED=D-052` and `.\tools\windows\prepare-xbla-assets.ps1 -Level aztec -Props` prepare the door as authored (measured byte-identical to the bundle before D-052). Without the background bundle the game's shade alone applies (the rule's first form) and the summary says so. ORIGINAL and COMMUNITY HD read no bundle.

**What the game does**

The secret door is its wall's stone. At the door of bound pad 34 (room 6/7;
the second, pad 35, is the same model) `Pdoor_aztecZ` lays texture 141
under (106.8, 106.8, 82.5) - the area-weighted mean of its corner colours
(110, 110, 85) and (92, 92, 71) - and the room's wall round it (textures
143 .. 146, in the door's plane) lies under (81.1, 81.1, 62.7): one hue,
the door 1.317 times the wall in every channel. At playtest run
20261012-195917 mark 1 ORIGINAL draws the door 1.25 times the wall in the
same plane beside it, in every channel.

**What the remaster's item holds, and draws**

`prop/dooraztec` - its `new` and `original` generations alike - lays a pale
grey stone image under white on its faces and (74, 74, 54) on its edges:
its tone (image mean x vertex colour) is (117.7, 117.7, 116.0) where the
remaster's wall round it reads (33.0, 31.9, 29.5) - a pale block in a dark,
vine-hung wall. The remaster draws it so (private emulator, mark 1).

**What Sightline did before**

At first drew it as the remaster does: (131, 130, 131) against the wall's
(37, 35, 29) at mark 1 ("The secret doors looks really bright"). Then, in
this rule's first form, multiplied it by the game's shade alone: (55, 55,
42), 1.5 times the wall and olive where the wall is warm grey ("secret
stone doors are still pretty pale", run 20261012-230527).

**What Sightline does**

For the doors `DOOR_WALL` names, at every placement of the door in the
setup (`gameshade.door_placements`: the bound pads its records name, its
face centre, the normal of its thinnest box axis, the face's half-extents):

    shade = 255 x T_wall x (S_door / S_wall) / T_door        per channel

- S_door: the game model's shade (`prop_game_shade`);
- S_wall: the game's room shade in the wall round each placement - room
  triangles facing the door's normal (within 0.9), within 15 room units of
  its face, within twice its larger half-extent of its centre, outside its
  footprint (`in_door_wall`, `door_wall_shade`);
- T_wall: the tone (image mean x vertex colour) of the prepared
  background's triangles in the same walls, at the stage's 1.92 item units
  per room unit (`bundle_wall_tone`);
- T_door: the item's tone on its principal faces (`item_face_tone`).

Prepared: "its wall: the game's 81.1 81.1 62.7 over 40560 room units^2, the
remaster's tone 33.0 31.9 29.5 over 105000, the door's 117.7 117.7 116.0 ->
shade 94.1 91.0 85.3". The image, its detail and the geometry are the
remaster's.

**Why it is safe**

Measured, teleport only, seed ffffffff97b3f64d:

- mark 1, held: the door (48.5, 46.9, 43.9) against the wall beside it
  (35.6, 33.2, 29.9) - 1.36 .. 1.47 times it, warm grey - where the first
  form left it (55.2, 55.1, 42.3), 1.47 .. 1.57 and olive, and ORIGINAL
  draws it 1.25; the second door (pad 35) reads as the doorway's stone the
  same way; over 17 frames of a walk to the first door and a press of use
  the tone holds (the door does not open from that approach under ORIGINAL
  or XBLA);
- `SL_WA_AS_AUTHORED=D-052` prepares the door byte-identical to the bundle
  before D-052 (8c5b04aa...); every other item of Aztec and the 54 non-Aztec
  bundles of the preservation set prepare byte-identical;
- the worldasset tool self-test (the shade's five mutations and the wall's
  eight each fail it).

Not claimed: the door is drawn through the prop path, whose lighting puts
it a little above the rule's 1.317 on screen; the console's own result is
the pale block.

No file under src/game is touched.

### D-051 — AZTEC'S PLACEHOLDER IMAGE IS NOT DRAWN (#96): under PRESENTATION XBLA no draw of Aztec's background samples the source's magenta placeholder - a 16x16 checker of 8x8 squares, full magenta on one diagonal and dark magenta on the other, the authoring tool's missing-image fill. Its two opaque draws (the ceiling tubes' sleeves, a ceiling strip) are drawn by their own vertex colour over one white image; its three alpha-shaped draws (two cutouts, one translucent) are left out of the bundle; default ON within XBLA, `SL_WA_AS_AUTHORED=D-051` at preparation keeps them as authored (native-only, render-only, preparation-time, the D-032 / D-047 class)

- **Date:**        2026-10-12
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted) - Gitea #96: tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-051']`, `PLACEHOLDER_GREEN`, `PLACEHOLDER_BRIGHT`, `PLACEHOLDER_SHAPED_GROUP`, `placeholder_checker`, `placeholder_fill`, `placeholder_opaque`, `withhold_placeholders`, the D-051 block of `prepare_background`), tools/worldasset/selftest.py (`placeholdertests`)
- **Re-baseline:** none required - no simulation code is touched
- **Levels:**      aztec, egypt (the rule is general in form and enabled for Aztec's background and Egyptian's, `temple`, alone)
- **First tick:**  n/a - no hashed state moves
- **Fields:**      none. Pixels only
- **Toggle:**      preparation-time. `SL_WA_AS_AUTHORED=D-051` and `.\tools\windows\prepare-xbla-assets.ps1 -Level aztec -Background` (or `-Level temple -Background`) prepare the background with the placeholder draws as authored. ORIGINAL and COMMUNITY HD read no bundle.

**What the game does**

Its rooms carry no tube fixtures: the N64 lights of the mural halls are
small square ceiling panels (ORIGINAL, playtest run 20261012-195917 mark 4
from both sides), and nothing stands where the remaster's vines hang round
room 72.

**What the remaster's item holds, and draws**

`background/aztec` names five 16x16 images - the source's 32, 50, 56, 57
and 58 (`_0x044FF005`, `_0x09AE5895.tga`, `_0x0E1D4805.tga`, `_0x05581215`,
`_0x0C427C65`) - that are no texture: decoded, every one is the same 2 x 2
checker of 8 x 8 squares, (255, 0, 255) on one diagonal and (117 .. 120, 0,
117 .. 120) on the other, alpha 255. Five draws use them as their base
image, 2809 triangles:

| draw | style | triangles | what |
|---|---|---|---|
| 0x2018 | 0x4701 opaque | 2232 | the ceiling tubes' sleeves and end caps over the mural halls (vertex colours teal 84 105 111, white, dark 17 24 26) |
| 0x484c | 0x4701 opaque | 64 | a thin white ceiling strip |
| 0x4684 | 0x4b01 cutout gate | 192 | alpha-tested shapes over the mural halls |
| 0x3690 | 0x4b01 cutout gate | 186 | alpha-tested shapes round room 72 |
| 0x44ac | 0x4702 translucent | 135 | translucent shapes round room 72 |

The private emulator draws all five magenta (round azt1, room 72; resume
round, mark 4 and the TV walls of marks 2 and 3, where the tubes hang
magenta).

**What Sightline did before**

At first drew them as the remaster does - magenta tubes and sheets in the
mural halls and round rooms 71 .. 73 (sweep az2) - and then, in this
round's first form of D-051, withheld all five: the tubes lost their
sleeves and only the glowing cores inside them drew (playtest run
20261012-195917, mark 4: "Lights look incomplete").

**What Sightline does**

For Aztec's background, a draw whose base image (after the base-slot choice)
is the placeholder checker no longer samples it:

- an OPAQUE draw (style low byte 1, outside the foliage group 0x0800 that the
  cutout gate 0x4b01 carries) is whole in its geometry, and is drawn by its
  own vertex colour over one white image - the untextured contract (#81).
  The tubes stand complete: a pale sleeve with grey end caps round the
  glowing core (mark 4, both sides; rooms 14 and 20);
- a draw in the foliage group or the translucent pass takes its outline or
  its coverage from the image's alpha, which the source does not hold, and
  is left out of the bundle, as a draw whose image is not carried already is.

The checker is recognised by its pattern, not by its colour alone: every
texel has green at most 8 and red within 8 of blue, alpha 255, the image is
square with an even side, and the texels whose red is at least 200 fill
exactly two diagonal quadrants. Prepared: "D-051: 5 placeholder images: 2
opaque draws (2296 triangles) drawn by their vertex colour, 3 shaped draws
(513 triangles) withheld".

**Why it is safe**

Measured, teleport only, seed ffffffff97b3f64d:

- the pattern, scanned over every image of the 19 prepared backgrounds,
  matches Aztec's five images and nothing else;
- mark 4 from both sides, as authored (magenta sleeve), withheld (bare
  core) and now (sleeve and caps); rooms 13, 14, 20 at four headings; marks 2
  and 3 (the tubes over the TV walls); no magenta anywhere in the 90-room
  sweep;
- `SL_WA_AS_AUTHORED=D-051` prepares the five draws as authored (33493
  triangles against 32980);
- the worldasset tool self-test (the checker's nine mutations and the
  narrowing's four each fail it).

Not claimed: the images the remaster's authors meant for these draws are
not in the source; the sleeves carry their vertex colour, not a texture,
and the three shaped draws stay out.

**Extended to Egyptian (2026-10-13)**

`background/temple` (Egyptian, stage 32) names one more placeholder, its
image 15 (`_0x075114E5`, 16x16, format 8_8_8_8 with a mip chain). Decoded it
holds full magenta (255, 0, 255) and dark magenta (120, 0, 120) texels at
alpha 255 and every other texel clear (alpha 0): the same fill, read with
clear texels round it (the base level of a 16x16 texture with a chain is
read from the packed tail; whether the clear texels are the image's own is
not established, and the rule does not depend on it). One draw uses it, style
0x4701 (opaque): the remaster's palm trunks, pool 3 (703 vertices, 1254
triangles, 19 trunks), which the item adds to the courtyards and round the
temple's outer walls - the original generation has no palms, and the game
none. The private emulator draws the trunks magenta under their green fronds
(start courtyard, room 31). The rule takes that form too
(`placeholder_fill`): alpha 0 or 255 on every texel, every opaque texel
without green and with red equal to blue (within 8), both full (red at least
200) and dark ones present. The trunks are opaque and draw by their vertex
colour (white and greys, 0xffffffff .. 0xff9fa5a2). Scanned over the 1347
images of the source's 24 backgrounds, the fill matches Aztec's five
checkers and Egyptian's image 15, nothing else. Prepared: "D-051: 1
placeholder image (_0x075114E5): 1 opaque draw (1254 triangles) drawn by
their vertex colour, 0 shaped draws (0 triangles) withheld".
`SL_WA_AS_AUTHORED=D-051` with `-Level temple -Background` prepares the
trunks on the placeholder, as authored.

No file under src/game is touched.

### D-050 — CRADLE'S SHAFT LADDER IS THE GAME'S (#96): under PRESENTATION XBLA the ladder beside the pole in Cradle's shaft (room 7) - which the remaster leaves out, its item carrying the pole and no ladder - is drawn from the game's own room list: a translucent room triangle of a room the presentation names, with no surface of the native fill's index of the replacement (its non-blended meshes) within the fill's hole radius of its corners and centroid, draws where it stands instead of being suppressed with the room; default ON within XBLA, `SL_BG_KEEP_ROOMS=0` suppresses it as before (native-only, render-only, the #83 / D-035 class)

- **Date:**        2026-10-12
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted) - Gitea #96: src/platform/sl_presentation.{c,h} (`s_native_kept`, `sl_presentation_native_kept`, the self-test), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_native_kept`), src/gfx/sl_gfx_dl.c (`nf_room_kept`, the background suppression, the census line)
- **Re-baseline:** none required - no simulation code is touched
- **Levels:**      cradle (the rule names Cradle's room 7 alone)
- **First tick:**  n/a - no hashed state moves
- **Fields:**      none. Pixels only. Collision, the ladder tile and climbing are the game's, untouched
- **Toggle:**      `SL_BG_KEEP_ROOMS=0` suppresses the room's triangles as before. ORIGINAL and COMMUNITY HD replace no background and read nothing here.

**What the game does**

Cradle's room 7 is the shaft from the cradle down to the bottom platform: the
pole (texture 762, the primary list) and, beside it, the ladder - one
translucent quad on the room's secondary list (texture 844, 2 triangles at
world z 76, x -68 .. -17, y -2456 .. -1205; render mode FORCE_BL, no depth
update, ZMODE_XLU), drawn by the image's alpha as rails and rungs.

**What the remaster does**

Its item carries the pole - draw 0xe44 in the `new` generation of
`background/cradle` (12 vertices, item y -1489 .. -633), draw 0xeac in the
`original` one - and no ladder. The draws of either generation with
vertices within 120 item units of the pole's column are the pole, the
shaft's panels (0x4e8, 0xa64) and the floors, shadows and structure beside
it; inspected one by one, none of them is a ladder or carries a ladder's
image or shape. The source holds no item named for a ladder, and the
running remaster draws the bare pole beside no ladder (private emulator,
round crd6, at the playtest mark), where ORIGINAL draws the ladder.

**What Sightline did before**

Drew the remaster's item and suppressed every room triangle of the game's,
so the shaft had no ladder (playtest run 20261012-142635, mark 1: "the
ladder from the final part of the mission is missing in xbla").

**What Sightline does**

`nf_room_kept` (sl_gfx_dl.c): in a room `sl_presentation_native_kept` names
(xbla, cradle, 7), a translucent room triangle whose three corners and
centroid lie farther than `SL_WA_FILL_HOLE` (2.5 world units) from every
surface of the native fill's index of the replacement (`sl_wa_fill_distance`:
its meshes of every material but `SL_WA_BLEND_ALPHA`, which the index
leaves out) draws where it stands, through the presentation's own
texture set, as the #83 fade does. The room's other translucent triangles
(the shaft panel, texture 529) have an indexed replacement surface within
that and stay suppressed. Measured from the ROM's room lists over every
translucent triangle of Cradle: the ladder is the only group wholly that
far in room 7; elsewhere the arms' overhead cables (texture 213, rooms 34 ..
36, 98%) and two rail runs (texture 1437, rooms 10 and 14) are too, which
the remaster models itself - so the room is named rather than the rule made
general. Census: "#96 nativefill: 2 translucent triangles of named rooms
drawn where the replacement has no surface".

**Why it is safe**

Measured, teleport only, seed ffffffff97b3f64d, the delivered root:

- at the mark and from the ladder's other face the ladder stands beside the
  pole, rails and rungs from the platform to the shaft's top, as ORIGINAL
  draws it; side-on it is edge-on in both (sheets ladder-poses,
  ladder-poses2);
- the frozen XBLA start of the eighteen accepted stages against the build
  before: seventeen byte-identical; Cradle's differs by 133 pixels (box x
  347 .. 361, y 286 .. 367 of 1365 x 768) - the ladder under the distant
  cradle in the intro view; with `SL_BG_KEEP_ROOMS=0` all eighteen are
  byte-identical. Caverns' doors and water byte-identical (105 of 105
  frames);
- ORIGINAL and COMMUNITY HD byte-identical; the presentation self-test
  (the table's entry, other rooms and stages, other presentations).

Not claimed: a climb of the ladder was not completed by the bounded local
actions (three approaches walked past it off the platform, identically
under ORIGINAL and XBLA); collision and the ladder tile are not touched.

No file under src/game is touched.

### D-049 — CRADLE'S TRUSS SHADOWS ARE DRAWN AS SHADOWS (#96): under PRESENTATION XBLA the shadow overlays of Cradle's background - the truss, brace, rail and cable images laid onto its floors under a pure or near-black vertex colour (every channel at most 8), which the remaster draws OPAQUE BLACK - are drawn blended at vertex alpha 143, the remaster's own shadow alpha, so the floors darken under them with their detail kept; default ON within XBLA, `SL_WA_AS_AUTHORED=D-049` at preparation prepares them as authored (native-only, render-only, preparation-time, the D-032 / D-047 class)

- **Date:**        2026-10-12
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted) - Gitea #96: tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-049']`, `SHADOW_ALPHA`, `SHADOW_BLACK`, `shadow_overlays`, the D-049 block of `prepare_background`, the blend of a marked draw), tools/worldasset/selftest.py (`shadowtests`)
- **Re-baseline:** none required - no simulation code is touched
- **Levels:**      cradle (the rule is general in form and enabled for Cradle's background alone)
- **First tick:**  n/a - no hashed state moves
- **Fields:**      none. Pixels only
- **Toggle:**      preparation-time. `SL_WA_AS_AUTHORED=D-049` and `.\tools\windows\prepare-xbla-assets.ps1 -Level cradle -Background` prepare the background as the source authors it - measured byte-identical to the bundle before this change. ORIGINAL and COMMUNITY HD read no bundle.

**What the game does**

The N64 rooms carry the antenna's shadows in their own floor shading: at the
lower platform (playtest run 20261012-131506, mark 1) the floor under the
truss shadows is 0.544 of its lit brightness (mean luma 67.9 against 124.7,
ORIGINAL at the mark), the tread visible through them.

**What the remaster's item holds, and draws**

Separate shadow draws of the truss images: on the lower platform draws
0x300 / 0x310 (56 triangles, the 512x512 brace image, the source's cutout
gate 0x4b01, alpha test on, colour word 0xff000000 - the 8_8_8_8 colour the
item's programs declare at the record's last word); on the three arms'
floors the layer of the two-layer draw 0x23ec (36 triangles, layer colour
0x000000, alpha 255); on the top catwalks the layers of 0x25a8 .. 0x26ac (22
triangles, 0x040404) and draws 0x7bc / 0x7cc (12 triangles, 0x040404, the
decal pass); and 40 black triangles inside the truss decal draw 0x210 (with
0x200 beside it). Their programs export texel x colour (the two-layer
layer's weight is its colour alpha x the image's), so every shape draws
opaque black. The private emulator shows the same at the mark (round crd5:
`scratchpad\crd1\sh\x-mark1c.png`, pitch -38.8 against the mark's -40.2):
solid black bands over the tread. The remaster's finished stages draw
their black shadow overlays translucent - vertex alpha 139 on the Dam, 143
on Archives, 107 and 147 on Depot (every all-black blended mesh of those
backgrounds), median 143.

**What Sightline did before**

Drew them as the remaster does: opaque black (owner playtest run
20261012-131506 mark 1, "Shadows are pitch black").

**What Sightline does**

For Cradle's background, every triangle of a shaped draw (its image carries
alpha) whose three vertices have every colour channel at most 8, and every
such two-layer draw's layer, is drawn blended with vertex alpha 143: the
floor under the shape darkens to 0.44 of itself. The triangles move onto
copies of their vertices (a mixed draw keeps its other triangles), so no
other triangle changes; a layer surface so faded is keyed apart from the
unchanged one. Prepared: "D-049: 11 shadow overlays (166 triangles) blended
at vertex alpha 143", 14561 triangles as before.

**Why it is safe**

Measured, teleport only, seed ffffffff97b3f64d:

- `SL_WA_AS_AUTHORED=D-049` prepares Cradle's background byte-identical to
  the bundle before (97d33279...); Dam, Depot, Archives and Caverns
  backgrounds prepare byte-identical to the code before this change;
- the owner's mark and ten more views (the lower platform both ways, the
  top catwalks, the pylon bases, the arms; sheets shadow-mark1,
  shadow-poses, shadow-poses2): every former black band now shades the
  floor with its tread visible, as ORIGINAL's shadows do; the shadows the
  remaster authored translucent are unchanged;
- the lent sky (D-044) is untouched: over the sky sweep the frames differ
  only on the catwalk and underside shadows, never on the dome;
- the worldasset tool self-test (eight mutations of the rule each fail it).

Not claimed: the remaster's shapes are not the game's (its truss shadows
are sharp, the game's soft); only their strength is the game's.

No file under src/game is touched.

### D-048 — CAVERNS' REMASTERED WATER MOVES WITH THE GAME'S (#96): under PRESENTATION XBLA the remaster's water draws on Caverns' background - which lie on the game's texture-1508 water and carry its coordinates, but which nothing in the remaster's records moves - are drawn through the game's own two-tile water blend at the water controller's phases, so they scroll and shimmer as the game's water does; default ON within XBLA, `SL_BG_WATER=0` draws them still at run time and `SL_WA_AS_AUTHORED=D-048` prepares them unmarked (native-only, render-only, the D-023 cloud-phase class)

- **Date:**        2026-10-12
- **Phase:**       1 (the world-asset sprint)
- **Commit:**      (this change, uncommitted) - Gitea #96: tools/worldasset/gameshade.py (`water_draws`, `game_water`, `_table_stages`), tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-048']`, the D-048 block of `prepare_background`, the material key), tools/worldasset/slwa.py (`MAT_GAME_WATER`), src/gfx/sl_gfx_worldasset.c/.h (`SL_WA_MAT_GAME_WATER`, `sl_wa_water_state`), src/gfx/sl_gfx_dl.c (the background pass), src/native/sl_world_assets.c (`sl_wa_water_phase`, read-only)
- **Re-baseline:** none required - no simulation code is touched
- **Levels:**      caverns (the rule is general in form and enabled for Caverns' background alone)
- **First tick:**  n/a - no hashed state moves
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_BG_WATER=0` (run time) draws the marked water still; `SL_WA_AS_AUTHORED=D-048` (preparation) leaves it unmarked. ORIGINAL and COMMUNITY HD read no bundle.

**What the game does**

tex.c draws room texture 1508, Caverns' water, under `MipMap2C_Something_Setup`
(unk_092E50.c): two wrapping 64-texel tiles of the image lerped by
PRIM_LOD_FRAC, tile 0's origin advanced every tick by the water controller
`sub_GAME_7F092E50` (S 0.25 and T 0.1 quarter-texels a step), tile 1 at
(+90, +150) from it, the lerp a sine of a third phase (0.04 a step). The inlet
lake, the outlet caverns' plane and the plane under the control block scroll
and shimmer.

**What the remaster's item holds**

Two draws (0x854, 0x864; 41 triangles) on a 64x64 intensity image, lying on
the game's water triangles with the game's coordinates in its convention (u =
s / 2048 + c, v = c' - t / 2048, every triangle to 0.1%), under the rock's
pixel program, the opaque style, no constant upload, no wave pool and no
material record. Whether the remaster's engine moves them by other means is
not established (its debug camera freezes the game).

**What Sightline did before**

Drew them still, at their stored coordinates.

**What Sightline does**

The preparation marks a draw every triangle of which lies on the game's
texture-1508 water and follows that relation (`gameshade.water_draws`); its
material takes `MAT_GAME_WATER`. The background pass draws such a material
with tile 0's shift on unit 0's texture matrix, the same image at tile 1's
shift on unit 1's, lerped by the game's fraction (texenv mode 8, B-107's
crossbar) and multiplied by the vertex colour, the shifts and fraction taken
each frame from the controller's own phases (`sl_wa_water_state`).

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`, seed pinned,
pitch held by a scratch capture build, reads 200 .. 440 every 20 at the
playtest mark: the water moves 2.0 - 2.4 grey levels a step (ORIGINAL's 3.5 -
4.1; 0.35 - 0.45 with the switch off, the scene's own residue). With
`SL_BG_WATER=0` the 13 frames are byte-identical to the build before the
change on the bundle before it. The re-prepared bundle differs from the one
before in one byte (material 7's flags). ORIGINAL and COMMUNITY HD are
byte-identical at both playtest marks. No file under src/game is touched.

### D-047 — DEPOT'S YARD TAG IS DRAWN WHERE ITS SILHOUETTE IS (#92): under PRESENTATION XBLA the one two-image decal of Depot's background - a black-on-white silhouette (coverage) and a coloured tag on black (colour) - is prepared as the tag's colours under the silhouette taken the other way round, so the tag is painted on the container where the game paints its own, in place of what its program exports (the tag left out and its black margin drawn round it); default ON within XBLA, `SL_WA_AS_AUTHORED=D-047` at preparation prepares the coverage as the program reads it (native-only, render-only, preparation-time, the D-032 / D-036 class)

- **Date:**        2026-10-09
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #92: tools/worldasset/prepare.py (`coverage_inverted`, `coverage_composite`, the two-image block of `prepare_background`, `DIVERGENCE_ITEMS['D-047']`), tools/worldasset/xblaworld.py (`two_image_coverage`), tools/worldasset/selftest.py
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      depot (the rule is general in form and enabled for Depot's background alone)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_WA_AS_AUTHORED=D-047` when preparing Depot's background writes the coverage as the program reads it. A developer diagnostic, not a setting. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

On the container in the yard south of the warehouses (world x 1281 .. 1509,
z -5751, the wall facing +z) the game paints a pale blue-and-white tag under
a pink one, at the place playtest run 20261009-122825-lvl30 marks (mark 1).

**What the remaster's item holds**

One draw at that place (background item draw 0x80e8, 6 triangles, style
0x2702 - the decal pass, vertex alpha 147) names two images: slot 0 a
256x128 black silhouette on white (`_0x0C836C85`), slot 1 a 256x128
coloured tag - blue outlines, tan letters - on black (`_0x01B68E25`), the
remaster's drawing of the game's tag. Its pixel program (one of the item's
effect groups, read once the pairing rule below reaches it) fetches both at
the same interpolator and exports rgb = light x colour x slot 1, a = slot
0's BLUE x colour alpha: coverage_channel answers 'z' for slot 0 and '' for
slot 1 (`xblaworld.two_image_coverage`). Measured on the two images: where
the silhouette is white the tag image is black (mean luma 2.5 of 255), where
it is black the tag image carries the whole tag (135.4). Taken as the
program reads it, the coverage leaves out exactly the tag and draws its
black margin as a dark rectangle with a tag-shaped hole. Whether the
remaster's own frame shows that is NOT witnessed: the private emulator's
debug camera at the mark (two sessions, pitch and heading in both senses)
draws no container there, and its Bond-view teleport is refused on Depot
(B-DEP1).

**What Sightline did before**

The level reader could not pair the item's programs (its first effect group
carries its vertex half twice - see "The reader" below), so no draw of the
item had a coverage answer, and the preparation drew this one as its slot-0
image with that image's alpha (255): the white silhouette rectangle, at the
decal's 58%, over the container (mark 1, "a black graffiti decal inside a
solid white rectangle").

**What Sightline does**

The preparation draws a two-image coverage draw as ONE image - its colour
slot's rgb, its coverage slot's channel as alpha (`coverage_composite`), the
program's own reading, for any item. D-047, for Depot's background: when the
coverage image's bright texels (above 191) cover none of the colour image's
content (mean luma below 8) while its dark texels (below 64) carry it (above
32), each over at least 5% of the image (`coverage_inverted`), the coverage
is taken inverted. The tag is drawn at the decal's own vertex alpha.

**The reader (not a divergence)**

`xblaworld._paired_pixel_objects` pairs an item's vertex objects with its
pixel objects group by group. Depot's first group is 3 pixel objects then 6
vertex objects, the last three byte-for-byte copies of the first three, and
its 33 vertex-program records name all 33 vertex objects; the rule refused
the whole item. A further run that copies the first byte for byte now pairs
with the same pixel objects. Re-preparing every class of the 12 accepted
stages and Depot (249 bundles) with the tools before and after this change:
byte-identical but `background/depot`. Within it the change gives 174 of 174
draws a coverage answer (none before): this tag, two small blended draws
taking their coverage from blue (6 triangles; their images, a 32x32 disc and
a 128x64 streak of light, stand on black), one 120-triangle translucent draw
with an alpha of one drawn opaque (a 32x32 image spread over the whole
level), and a window image over a concrete one read as a two-layer surface
(#72).

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed pinned, read 80:

- the mark, level with it and 200 units back: the tag reads where the game
  paints its own, the white rectangle gone; prepared with
  `SL_WA_AS_AUTHORED=D-047`, the dark rectangle with the tag-shaped hole
  (sheets m1-after, m1-arms);
- every one of Depot's 79 pad poses, the same build over the bundle before
  and after: 78 of 79 byte-identical; at the platform (p38) 138 pixels of
  the buffer stop's bar, where the dark joints its blended draw left over
  itself are gone - the draw now drawn opaque (sheets p38, p38-crop);
- the 248 other bundles byte-identical; the XBLA start frames of the twelve
  accepted stages byte-identical; ORIGINAL and COMMUNITY HD read no bundle;
- tools self-test (the pairing, the two-image answer, the composite and the
  inversion; a mutation of each fails it).

No file under src/game is touched.

### D-046 — STREETS' LENT DOME MEETS THE FOG WHERE THE GAME'S CLOUD LAYER ENDS (#90): under PRESENTATION XBLA the dome a stage is lent (D-044: Streets) is shaded with the cloud fraction the game's own cloud layer has at each point of the picture - read each frame from the game's corners (sky.c's skyGetWorldPosFromScreenPos and skyIsScreenCornerInSky, the environment's WaterConcavity included) - and below the layer's horizon edge it is the fog colour, as the game's fill is there, in place of a fraction taken along each vertex's direction; default ON within XBLA, `SL_XBLA_LENT_SKY_EDGE=0` shades along each direction as D-044 did (native-only, render-only, the D-030 / D-044 class)

- **Date:**        2026-10-08
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #90: src/native/sl_world_assets.c (`sl_wa_sky_screen`, read-only), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_sky_screen_frac`, `sl_wa_sky_screen_edge`, `sl_wa_sky_shade_frac`, the switch, the self-test), src/gfx/sl_gfx_dl.c (`sky_draw`, `sky_frame_begin`)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      streets (the only stage lent a dome)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_LENT_SKY_EDGE=0` shades the lent dome along each vertex's direction (the picture before, byte for byte). A developer diagnostic, not a setting. ORIGINAL and COMMUNITY HD select no world-asset set and draw the game's cloud layer.

**What the game does**

skyRender (sky.c) draws its cloud layer as one screen polygon from the top
of the view down to a horizon EDGE, its colour per corner the cloud shade of
that corner's direction, and fills the rest of the view with the
environment's fog colour. Every direction it takes is read 25 rows LOWER
than the row it is drawn at on Streets (`WaterConcavity`, row 29), so the
edge - where a direction turns level - stands well above the true horizon:
looking along a street (eye y 97, pitch 2.9) at row 90.6 of 10..230 (a
scratch print of skyRender's own `sp54c`; the frame byte-identical with and
without it). Below it the picture is the fill, (16, 24, 33).

**What Sightline did before (D-044)**

The lent dome's vertices took the cloud shade along their own direction
(D-030's `sl_wa_sky_shade`), which reaches the fog colour only at the true
horizon. Between the game's edge and the horizon the dome stood lighter than
the fog: (36, 39, 40) where the game holds (16, 24, 33). Down Streets' long
streets the far end is a prop (`st_pete_room_*`: the street at playtest
marks 2 and 3 is drawn by props - with the background item hidden the frame
is byte-identical), fully fogged by row 29 (-100 .. 7500) and cut by the
game's far plane. Against the fog-coloured fill the cut is invisible; against
the lighter dome the block at the end of the street came in whole as the far
plane passed it (playtest run 20261008-222210-lvl29, marks 2 and 3,
"buildings at the end of the road streaming in late").

**What Sightline does**

`sl_wa_sky_screen` (src/native) asks the game's own functions for the four
corners' directions and fractions and computes skyRender's edge row on each
side; `sky_draw` shades each dome vertex with the fraction at its place on
the picture (linear between the top row's fraction and the edge's, 1 below
the edge) and lays the fog colour over the dome below the edge line, as
skyRender's fill. Looking up, where every corner is in the sky, the
fractions are the corners' and the dome reads as before.

**Why**

Measured on an approach every 100 units from 600 behind mark 2 to 400 past
mark 3 (19 steps, teleport, read 80, seed 0000000050aee279): the block at the
end of the street changed a 160x45 window from (36, 39, 40) to (16, 24, 32)
between steps 11 and 13 before; after, the window is (16, 24, 32) from step
1 to step 12 and as before from 13 - no step. ORIGINAL holds (16, 24, 33) at
every step. Mean frame luma at the sky poses moves toward ORIGINAL's: level
view r05 top 30% 60.8 -> 48.8 (ORIGINAL 41.9), looking up (up60) 117.4 ->
111.4.

**Why it is safe**

- `SL_XBLA_LENT_SKY_EDGE=0` byte-identical to the build before at twelve
  Streets poses (the sky poses and the three marks); ORIGINAL and
  COMMUNITY HD byte-identical at the same twelve; the XBLA start frames of
  the eleven accepted stages byte-identical (none is lent a dome).
- The native read calls the game's corner functions, which read the view,
  the eye and the environment and write only their outputs; no file under
  src/game is touched.
- Cost at the marks (presented frame, two interleaved runs each): mark 2
  5.39 -> 5.56 ms, mark 1 6.04 -> 6.11 ms.
- The world-asset self-test pins the fraction, the fill and the edge (a
  mutation of the edge fails it).

### D-045 — A ONE-PIECE JEEP RIDES THE GAME'S VEHICLE WHOLE (#90): under PRESENTATION XBLA the remaster's `prop/jeep` - one rigid piece, its wheels inside the body's own draws - draws on the game's PjeepZ, which moves five parts (base, wheel1..wheel4), riding the model's root matrix whole where the prop seam refused it and the game's flat-green jeep drew; its wheels hold the rest pose; default ON within XBLA, `SL_PROP_RIGID=0` refuses the item as before (native-only, render-only, the D-038 class)

- **Date:**        2026-10-08
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #90: src/platform/sl_presentation.{c,h} (`sl_presentation_prop_rigid`, its self-test), src/native/sl_chr_asset.c (`prop_rigid_ok` in `prop_map`)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      streets (the policy names one item of the xbla set; any stage placing PjeepZ takes it)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_PROP_RIGID=0` refuses the item on a model that moves parts, so the game's jeep draws (the picture before, byte for byte). A developer diagnostic, not a setting. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

The jeep (setup type 39, a vehicle) is `PjeepZ`, a model of five matrices
named by the `car` skeleton - base and four wheels - which the object render
turns and steers every frame it moves. Streets places eight; they stand
parked.

**What the remaster does**

`prop/jeep` is a STATIC item in both of the source's generations: no
skeleton, the body, arches and all four wheels in one 3602-triangle strip
plus a 342-triangle list, a glass windscreen in a draw of its own. It can
only be drawn as one rigid piece.

**What Sightline did, and does**

The prop seam relates a one-piece item to a model of one matrix only; on a
model that moves parts it refused the item (`prop item on PjeepZ REFUSED -
the model moves 5 parts`), and the game's model drew - under XBLA the flat
green panels and black windscreen of the cartridge's jeep (playtest run
20261008-214033-lvl29, mark 1; the XBLA frame's jeep is the ORIGINAL
frame's). The bundle itself was right (contact sheet: the camouflage atlas,
the glass). For an item the presentation names
(`sl_presentation_prop_rigid`), the seam now relates it to the model's
whole box and draws it on the root matrix, as any rigid prop.

**Why**

Every recorded scratch run of the sprint shows one refusal of this kind,
PjeepZ's; every other vehicle item carries the joints the seam needs.
Riding the root matrix is what the remaster's own item can do. The cost is
the wheels: they do not turn or steer while a jeep moves, and no jeep was
seen moving on Streets.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, read 80, seed ffffffffc6ebddbd, difficulty 1:

- the playtest mark and a pose 300 units back: the remaster's jeep stands
  where the game's stood, wheels on the street, the glass in its frame
  (sheets marks-c2);
- ORIGINAL at nine Streets poses byte-identical to the build before; the
  XBLA start frames of the eleven accepted stages byte-identical;
- presentation self-test (the new table; a mutation of it fails), headless
  Streets (`test.ps1 -Level streets`) PASS, layering 282.

No file under src/game is touched.

### D-044 — STREETS DRAWS A LENT DOME UNDER ITS OWN DUSK (#90): under PRESENTATION XBLA Streets, for which the source carries no skydome, draws `skydome/frigate` - the source's daylight dome prepared as cloud cover (D-034) with its D-023 cap - shaded by D-030's combiner with Streets' own environment row 29 read live (fog 16 24 32, clouds 225 175 100) in place of the game's sky.c cloud layer; nothing else of Frigate reaches the stage, and the row's range is the remaster's; default ON within XBLA, `SL_XBLA_LENT_SKY=0` draws the game's cloud layer as before (native-only, render-only, the D-030 / D-031 / D-034 class)

- **Date:**        2026-10-08
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #90: src/platform/sl_presentation.{c,h} (`sl_presentation_sky_lent`, row 29 in `s_game_atmosphere`, the self-test), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_sky_lent`, its self-test), src/gfx/sl_gfx_dl.c (`sky_frame_begin`)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      streets only (the policy names one item and one row of the xbla set)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_LENT_SKY=0` keeps the game's cloud layer (the picture before, byte for byte); `SL_XBLA_GAME_ATMOSPHERE=0` (D-030's) draws the lent dome unshaded, grey; `SL_SKY_ZENITH=0` (D-023's) leaves the cap out. Developer diagnostics, not settings. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

Streets' row (bgfog.c, `LEVELID_STREETS`) is a dusk: BlendMultiplier 10,
NearFog 5000, FarFog 7500, fog 16 24 32, and a cloud layer coloured 225 175
100 that sky.c fades into the fog colour toward the horizon. The far-fog prop
cut is at zDepth 7343.7 (SL_TABLE_DBG).

**What the remaster does**

The source carries `background/streets` and no dome; the remaster draws a
plain navy sky over the stage (private emulator, round str1). Its row 29
keeps the decomp's colour and end and starts at -100.

**What Sightline did, and does**

Before, the replacement background stood under the game's own cloud layer
(measured: the sky pixels of an XBLA frame looking up match the ORIGINAL
frame's, mean 103.4 89.7 68.1 against 103.5 89.8 68.2), while every other
outdoor stage of the set draws a source dome under the game's atmosphere
(Dam, Runway, Surface, Surface 2, Frigate, and Bunker 1's borrowed sky).
Now the policy lends Streets `frigate` (`sl_presentation_sky_lent`), asked
by `sky_frame_begin` after D-031's borrow; the dome is drawn exactly as a
stage's own - eye-anchored, the D-023 cap cast on the game's cloud plane
with the live phase and height, D-030's combiner and `sl_wa_sky_shade` with
the colours of the row the game holds (`s_game_atmosphere` names row 29; the
fog colour it puts on the row is the colour the row already holds).

**Why that dome**

Every source dome carries the same daylight image, so the choice is between
preparations. Frigate's is the one written as cloud cover (D-034). Under
Streets' bright cloud colour the combiner reads every bright texel of an
image prepared as authored as cloud: Surface 2's dome (authored plus cap)
drew one flat pale overcast (sky box looking up: luma 143.4, spread 9.8),
the cover a navy dusk with lit clouds fading to the fog navy at the horizon
(122.1, spread 40.8; the game's own layer 91.4, 34.6). Surface 2's own
treatment (D-040) is the model in every other respect - an outdoor stage
with a fog row, shaded by that row - but its image is the wrong one here.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed pinned, read 80 (the start view read 600):

- `SL_XBLA_LENT_SKY=0` at nine Streets poses (the start alley, the tank's
  square, four long street views, three looking up): byte-identical to the
  build before;
- ORIGINAL at the same nine poses byte-identical; the XBLA start frames of
  the eleven accepted stages byte-identical;
- no seam at the horizon: the dome meets the fog colour there, and the
  fully fogged facades stand in that colour; a yaw-and-walk strip down the
  long street (30 frames) shows the dome steady and no prop appearing; at
  the game's prop cut the row is 97.9% fogged, so no D-029 end is needed;
  the background carries no panorama shell, so D-026 has nothing to fade;
- presentation and worldasset self-tests (a mutation of the table fails),
  headless Streets PASS, layering 282.

Not claimed: overhead the cover is denser than the game's cloud image, so
the sky looking up is about a third brighter than the game's.

No file under src/game is touched.

**Extended (2026-10-09, #92): Depot.** Depot (stage 30, the source's item
`depot`) is the same case: `background/depot` and no dome in the source, a
sky the remaster draws black (private emulator, round dep1), and a row of its
own - environment 30, BlendMultiplier 10, NearFog 1600, FarFog 3000, fog
0 0 8, a cloud layer coloured 70 199 186, WaterConcavity 25; the remaster's
row keeps the colour and reaches -100 .. 5000. The policy lends it the same
dome (`s_sky_lent`: depot -> frigate) under its own row (`s_game_atmosphere`
row 30), and D-046 lays the fog colour below the game's cloud-layer edge, so
the picture below that edge is the fill it was. Same switch,
`SL_XBLA_LENT_SKY=0`. Measured, teleport only, read 80, seed
000000002284c0a1, owner root:

- `SL_XBLA_LENT_SKY=0` at nine Depot poses (five yards level, four looking
  up) byte-identical to the build before;
- the sky band (rows 6 - 35% of the frame), mean luma, ORIGINAL / before /
  after: level views p07 26.8 / 15.4 / 23.7, p08 29.3 / 19.5 / 25.0, p28
  47.0 / 33.3 / 38.3, p29 23.4 / 10.0 / 13.3; looking up u07 44.8 / 38.9 /
  52.4, u28 32.2 / 24.3 / 48.9, v28 (70 degrees) 39.8 / 37.3 / 82.3;
- the game's prop cut is at view depth 2974.7 on this row, where the
  remaster's row is 60% fogged. Both of round dep1's approach series (yard
  8 toward doors 41/42, eleven steps of 30 units; yard 28 toward warehouse
  70, twelve steps of 70 units) before and after: no step of the far window
  or of the sky band stands above its neighbours (yard 8 sky band step
  difference 3.6 .. 4.1 after, 2.7 .. 3.0 before; the far window
  byte-identical); a twelve-heading yaw strip in yard 8 shows the dome
  continuous and the fog-coloured band under every roofline - no prop or
  building edge stands against the dome, so no D-029 end and no D-041 draw
  is added (sheets sky-a, sky-b, approach-y8, approach-y28, yaw-p08);
- the XBLA start frames of the twelve accepted stages byte-identical.

Not claimed: under Depot's bright teal cloud colour the cover reads much
denser than the game's sparse cloud image - looking straight up about twice
the game's luma, a mottled teal field where the game draws soft wisps over
black.

**Extended (2026-10-12, #96): Cradle.** Cradle (stage 41, the source's item
`cradle`) is the same case: the source carries `background/cradle` and no
dome - its eleven domes are bunker, bunker2, caverns, complex, dam,
egyptian, frigate, runway, surface, surface2 and temple - and the stage
has a row of its own, environment 41: BlendMultiplier 10, NearFog 3333,
FarFog 9500, fog 96 128 160, a cloud layer (repeat 5000) coloured 255 255
0, no water. The remaster's row keeps the colour and reaches -100 .. 30000
(the user's `environment.slen`), and the remaster draws a plain blue sky
over the stage (private emulator, round crd5, from the lower platform).
There is no cinema row for the stage, so
the intro and the ending play under row 41 too. The policy lends it the same
dome (`s_sky_lent`: cradle -> frigate) under row 41 (`s_game_atmosphere`),
with D-046's edge. Same switch, `SL_XBLA_LENT_SKY=0`. Measured, teleport
only, read 60 .. 500, seed ffffffff97b3f64d, four zones (the central hub
looking 15 degrees up, the east arm level, the south arm 35 degrees up,
the lower platform 30 degrees up) at four headings each:

- `SL_XBLA_LENT_SKY=0` byte-identical to the build before, 180 of 180
  frames; ORIGINAL 180 of 180 and COMMUNITY HD likewise between the two
  builds;
- the lent dome draws from the intro on (the census's first report, at
  frame 1, is the intro's) and meets the fog band at the canyon's horizon
  at every heading, the rim of the canyon standing against it (sheets
  sky-k1 .. sky-k4, sky-intro);
- the sky band (rows 6 - 35% of the frame), mean luma, ORIGINAL / before /
  after, at heading 0: hub 144.5 / 144.3 / 171.1, east arm 136.2 / 136.1 /
  161.3, south arm up 152.6 / 152.5 / 182.6, lower platform up 131.5 /
  131.3 / 162.0;
- presentation and worldasset self-tests (dropping the Cradle row from
  either table, or naming its multi-player or cinema row, fails).

Not claimed: under the row's yellow cloud colour the cover reads denser and
brighter than the game's soft layer - the sky band 15 to 25% brighter, a
mottled pale-yellow field where the game draws long yellow-green wisps.

### D-043 — STATUE'S XBLA FOG STARTS AT 800 (#86): under PRESENTATION XBLA Statue's gameplay stage fog (environment 22) is drawn from 800 world units instead of the authored -100, so the near and middle park are no longer fogged toward the row's near-black colour; colour, end (3500) and the distance equation stay the remaster's, and props and characters are fogged by the same row; default ON within XBLA, `SL_XBLA_FOG_START=0` (or `SL_XBLA_AUTHENTIC_FOG=1`) draws the authored start (native-only, render-only, the D-025 / D-029 class)

- **Date:**        2026-10-07
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #86: src/platform/sl_presentation.c (`s_fog_start` row 22, `sl_presentation_fog_start`, the self-test), src/platform/sl_presentation.h, src/gfx/sl_gfx_worldasset.c (`sl_wa_env_presented` applies the start, `env_fog_start_on`, the load line, the self-test)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      statue only (the policy names row 22 of the xbla set)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_FOG_START=0` draws every row from its authored start; `SL_XBLA_AUTHENTIC_FOG=1` (D-025's) draws every row's authored range. Developer diagnostics, not settings. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

Statue's row in `fog_tables` (bgfog.c, `LEVELID_STATUE`) is a far band:
NearFog 2000, FarFog 3500, intensities 996..1000 per mille, colour 0 0 8,
a cloud layer 170 100 40. Measured at 16 Statue poses (mean frame luma,
read 80, `SL_VI_CATCHUP=0`), ORIGINAL with the game's fog switched off
(`SL_FOG=0`) is 25.4 against 23.9 fogged: the near and middle park are
unfogged.

**What the remaster does**

Its row 22 keeps the colour and the end and is linear from -100: 30% fog at
1000 units, 58% at 2000, toward a colour that is almost black. Its baked
vertex colour is dark as well (area mean luma 61 of 255 over
`background/statuepark`, against 211 for the Dam's and 83 for Silo's).

**What Sightline does**

`sl_presentation_fog_start` names row 22 of the xbla set at 800;
`sl_wa_env_presented` applies it beside D-025's end, so the replacement and
the per-object draws it fogs (`sl_wa_object_fog_env`) take the same row.

**Why**

Gameplay readability: the authored row left the park hard to read at
middle distance (round sta2, #86). Measured over round sta1's 31 teleport
poses (mean frame luma, read 80):
ORIGINAL 22.95, XBLA as authored 19.23, XBLA with the stage fog off 24.90 -
the remaster's fog is the larger part of the difference; the baked vertex
colour is the rest and stays as baked (the darkest views - room 1's fence
line, room 7, the helicopter compound's far side - stay much darker than
ORIGINAL, unfogged or not). Trial starts 700, 1100 and 1500 gave 22.44,
23.10 and 23.57; 800 gives 22.64 (+17.7%), between the two presentations,
a design value for gameplay readability, not a remaster fact. The far park
still closes into the same night.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed ffffffff97b3f64d, read 80, against the build
before the change:

- ORIGINAL at the 31 poses: 31 / 31 frames byte-identical;
- XBLA at the 31 poses: every frame brighter (sheets of the round), mean
  luma 19.23 -> 22.64; `SL_XBLA_FOG_START=0` reproduces the frames before
  the change byte for byte at 6 of 6 poses checked;
- the guards in the park (two poses) brighten with the ground they stand
  on, and a walk-back strip from the park gate (`prop/doorstatgate`, 11
  poses 350 units apart, out past the game's far-fog cut) shows the gate
  fading into the night as before, no step where it vanishes;
- the Dam, Facility, Runway, Surface, Bunker 1, Silo, Frigate, Surface 2 and
  Bunker 2 XBLA start frames: 9 / 9 byte-identical;
- presentation self-test 74 / 0 (5 new) and worldasset self-test 468 / 0
  (4 new); with row 22's start put back to -100 two presentation checks
  fail.

### D-042 — BUNKER 2'S HELIPAD RIM FADES INTO THE PAD (#85): under PRESENTATION XBLA the snow layer of the rim round Bunker 2's helipad takes layer weight 0 at the 28 corners that stand on the pad's own surface, and its image's two coordinates are swapped so the snow image's alpha edge runs across the rim instead of along it, so the rim reads as a soft snow border thinning from the snowfield into the concrete instead of the remaster's 14 hard white wedges; the rule (a shaped two-layer band onto its base image's surface) is general in form and enabled for Bunker 2's background alone; default ON within XBLA, `SL_WA_AS_AUTHORED=D-042` at preparation prepares the background as authored (native-only, render-only, preparation-time, the D-032 / D-033 class)

- **Date:**        2026-10-07
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #85: tools/worldasset/prepare.py (`band_fade`, `describe_band_fade`, `_surface_corners`, `_alpha_axis`, `BAND_EPS`, `BAND_COORD_EPS`, `BAND_AXIS_RATIO`, `DIVERGENCE_ITEMS['D-042']` names `bunker2`) and the tools self-test
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      bunker2 only, by an explicit enable. Forced on for every prepared background in a scratch preparation, the rule changes no other.
- **First tick:**  n/a - no hashed state moves; no runtime code changes
- **Fields:**      none. Pixels only, and only the rim's layer
- **Toggle:**      preparation-time. `SL_WA_AS_AUTHORED=D-042` (the D-032 switch's list) and `.\tools\windows\prepare-xbla-assets.ps1 -Level bunker2 -Background` prepare the background as the source authors it - measured byte-identical to the bundle before this change (F660EAEB). ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

The original's helipad has a soft, round snow border running from the
snowfield into the pad (ORIGINAL at the playtest's mark, run
20261007-102158-lvl27).

**What the remaster does**

The rim is one draw of 14 quads (pool 9 of `background/bunker2`, 56
corners) whose programs (dumped from the running remaster in round bk2d)
lerp a cracked-concrete base (+20, slot 1) toward a 256x256 snow image
(+16, slot 0) by the snow's alpha times the +24 vertex alpha. Every vertex
alpha is 255, and each quad maps the whole snow image once with its first
coordinate across the rim and its second along it, while the image's alpha
edge runs along its rows. So the edge cuts every quad across the rim: half
of each quad is snow, half concrete, split by a straight line - hard white
wedges, witnessed in the remaster itself at the mark's pose.

**What Sightline does**

`band_fade` (tools/worldasset/prepare.py). A two-layer draw qualifies when
its layer is shaped (weighted by its image's alpha, which the image
carries) and it is a band onto its base image's own surface: every
triangle has a corner on that surface and one off it, a corner being on it
when a corner of another draw drawn with the base image stands within 0.01
item units and carries the same base coordinate within 0.001. The corners
on the surface take layer weight 0. When the weight's unit gradient in the
layer coordinate, summed over the draw's triangles, runs along one
coordinate at least twice as much as the other, and the image's alpha runs
along the other axis (the spread of its row means against its column
means, at least two to one), the layer's two coordinates are swapped -
provided that puts the image's more opaque half on the side away from the
base's surface. On Bunker 2: the rim alone; 28 of 56 corners on the pad
(0 to 0.00025 units, every other corner 850 or more), the weight along u
(25.5 against 6.6), the snow's alpha along v (116.3 against 3.5), its
opaque rows toward the snowfield: faded and swapped. One quad of the 14
(corners 40-43) charts the rim the other way round; the turn, decided for
the draw, lays its snow edge along the rim there, and the fade still ends
it at the pad. The base copy of the draw and every other draw are
untouched; the layer's own copy of the pool is appended under its own key.

**Why**

A presentation decision (#85, round bk2e): the remaster's own rim is a
defect of its charts - nothing in the source fades the layer toward the
pad, and its alpha edge crosses every quad - and the remaster is evidence
of intent, not a ceiling. The rule takes what the source does carry (the
pad's surface, the layer's alpha) and does not paint anything new.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed ffffffff81e372bd, build 172D3671 (no runtime
change), the outside pose of round bk2c (m4out) and the playtest mark's
pose through the door (opened by the game's own `doorActivate` in a
scratch probe build), bundle F660EAEB against D4B6D142:

- ORIGINAL and COMMUNITY HD: byte-identical at both poses;
- XBLA: 370472 pixels differ outside and 45701 inside the doorway, all on
  the rim and the pad beyond it - the wedges are gone, a soft snow border
  thins into the concrete; an 11-frame yaw and a 16-frame walk out
  through the door show no seam and no popping;
- the bundle: `SL_WA_AS_AUTHORED=D-042` prepares F660EAEB, the bundle
  before, byte for byte; with the rule it is D4B6D142, byte for byte the
  scratch preview of round bk2d;
- forced on for the Dam, Facility, Runway, Surface, Bunker 1, Silo and
  Frigate backgrounds the rule fades nothing; with it enabled as shipped
  (Bunker 2 alone) those seven prepare the same bytes with the tools
  before and after this change;
- worldasset tools selftest 620/0 (13 new: the coincident-corner fade, the
  turn, an image already along the fade, the orientation guard, a draw
  that is not a band, a corner off the base's coordinate, an unshaped
  layer, the Bunker-2-only enable, the switch); breaking the turn, the
  band test, the coordinate test, the orientation guard, the enable, the
  fade or the layer copy's own key each fails one or two of them.

### D-041 — SURFACE 2'S PROPS COME OUT OF THE FOG WITH THE WORLD (#84): under PRESENTATION XBLA, while Surface 2's gameplay row (environment 43) fogs the game's per-object draws, a prop past the game's far-fog cut (`fogGetPropDistColor`, bgfog.c) is drawn instead of removed, fogged per vertex by that row as the background is; monitors keep the cut, the fog end stays 7662 (D-040); default ON within XBLA, `SL_XBLA_FOGGED_PROPS=0` keeps the game's cut (native-only, render-only, the D-029 / D-040 class)

- **Date:**        2026-10-07
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #84: src/game/propobj.c (`chrobjRenderProp`, `__sgi`-guarded), src/gfx/sl_gfx_dl.c (`sl_gfx_prop_fog_drawn`), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_env_prop_fog_drawn`, the switch), src/platform/sl_presentation.{c,h} (`sl_presentation_prop_fog_drawn`, the self-test)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      surface2 only (the policy names row 43 of the xbla set)
- **First tick:**  n/a - no hashed state moves (per-tick traces below)
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_FOGGED_PROPS=0` keeps the game's cut - a developer diagnostic, not a setting. ORIGINAL and COMMUNITY HD select no world-asset set, so the renderer answers 0 and the game's cut stands. docs/world-assets.md "Surface 2's background, sky and atmosphere" is the architecture.

**What the game does**

`fogGetPropDistColor` returns 0 - "completely obscured by fog, don't render"
- once the object fog `far_fog_dist_scaled / zDepth + near_fog_dist_scaled`
passes 1, and `chrobjRenderProp` returns without a display list. On row 43
the cut is at zDepth 7662.5 (farK -128.5372, nearK 1.0168). It tests the
prop's ORIGIN; under the cartridge's own fog the prop is fully fogged there
and the cut is invisible.

**What Sightline did before (D-040)**

The presented row 43 ends at 7662, the cut, and the row fogs the background
and the per-object draws alike by each vertex's straight-line distance
(`sl_wa_object_fog_env`). D-029's reasoning: an object's origin is fully
fogged at the cut, and its nearer parts are inside only by the object's own
extent. The satellite dish (`prop/sevdish`, an autogun prop at 1989 1581
-11506, room 12) is large enough for that extent to show. Measured at the
two marks of playtest run 20261006-235609-lvl43 (seed 0000000004d3d279,
teleport, read 80, SL_PROP_DBG=2 / SL_TABLE_DBG=1):

- mark 1 (eye -5432 408 -9690): dish zDepth 7719.1, object fog 1.0001,
  CULLED; mark 2 (165 units on): 7551.2, 0.9998, drawn. Eleven more props
  (rooms 12, 14, 31, 32) were culled at mark 1, zDepth 7685..10775.
- Distance solved per pixel from the same frame under the ends 7662 and
  10000 (`SL_XBLA_AUTHENTIC_FOG=1`; start -100, colour 32 16 16): the
  dish's faces at mark 2 lie 6943..7370 units out (median 7127), fog 0.93,
  13 levels (sum of RGB) off the fog colour. The building under it - the
  background, never cut - lies 6900..7100 out at mark 1 (median 6927), fog
  0.905, also 13 levels off. So the building read as a silhouette while the
  dish was absent, and the dish then appeared at the building's visibility.

**What Sightline does**

`chrobjRenderProp` asks `sl_gfx_prop_fog_drawn` when the cut answers 0: the
renderer answers 1 while the row fogs the per-object draws this frame
(`sl_gfx_native_stage_fog`'s condition) and the presentation policy names
the row (`sl_presentation_prop_fog_drawn`: xbla, 43). The prop is then drawn
with its colour left to the row, exactly as `lerp_rgba_s32_with_rgba_f32`
already leaves it, so it is fogged as the background around it. The fog end
stays at the cut: the game's characters keep their cut (chr.c), and under
the authored 10000 they would appear 23% clear at 7662 (D-040's
measurement); a character's extent is small enough for the end at its cut.
Monitors keep the cut too: their draw advances the screen animation
(`process_monitor_animation_microcode`).

**Why**

The dish appeared late - absent while the building it stands on was already
a silhouette in the fog (playtest marks 1 and 2). Drawn past the cut, it
comes out of the fog with the building. Measured on an approach along the
marks' line, every 100 units from 600 behind mark 1 to 800 past it: the
step that carries the dish's origin inside the cut changed the dish's
window by 0.93 levels on average before, 0.29 after (the neighbouring steps
0.23..0.41); nothing differs from the build before once the origin is inside.

What it shows instead, and why it is not bounded: past the presented end
the dish is the fog colour exactly (32 16 16), and above the horizon the
sky behind it is the game's cloud shade (about 52 23 16, D-030), so from far
back it is a dark silhouette - as any background shape standing above the
horizon is. Drawn to the game's far plane it is sliced in gradually by that
plane (origin view depth 10815: a sliver; 10722 and 10625: more of it), with
no whole appearance anywhere; any bound short of the far plane would remove
a fog-coloured silhouette whole against the cloud layer, the pop again.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed pinned, read 80, one game at a time:

- **No tick moves.** Per-tick traces (`SL_TRACE_OUT`, 700 frames) from the
  stage start, from mark 1 standing, and from mark 1 walking forward into
  the dish's cut (the probe shows the dish drawn past the cut, then inside
  it): every arm identical within its start - the build before and this
  one, XBLA and ORIGINAL, `SL_XBLA_FOGGED_PROPS=0`, and the probe armed.
  The drawn prop's matrices are the tick's: the tick still counts it on
  screen (`PROPFLAG_ONSCREEN`), only the render returned early.
- **The switch is the build before.** `SL_XBLA_FOGGED_PROPS=0` is
  byte-identical to the build before at the two marks and all fifteen
  approach steps.
- **Scope.** ORIGINAL and COMMUNITY HD byte-identical at all fifteen steps;
  of round srf2a's nine Surface 2 poses only the one facing the dish (mark 5)
  changes, inside the dish's window; Surface 1's start and its D-029 dish
  ladder (ten frames) byte-identical. The self-test pins the policy (a row
  moved from 43 to 36 fails three checks).
- **Cost** at mark 1 (normal window, 449 presented frames, three
  interleaved runs, medians): the presented frame stays 19.0 ms; the game's
  share of it 15.65 -> 16.65 ms and the display-list share 9.82 -> 10.47 ms,
  for the twelve props now drawn there; at mark 2 +0.3 and 0.0 ms.
- Nothing derived from any source is committed: the policy is one row.

### D-040 — SURFACE 2 TAKES SURFACE'S XBLA ATMOSPHERE (#84): under PRESENTATION XBLA Surface 2's gameplay stage fog (environment 43) is drawn with a fog end of 7662 world units instead of the authored 10000 - the view depth at which the game's far-fog cut stops drawing props there (D-029's rule) - its sky dome is shaded as the game shades its own night cloud layer under rows 43 and 943 (D-030's), and its dome takes the generated zenith cap (D-023's); colour, start and the distance equation stay the remaster's; default ON within XBLA, `SL_XBLA_AUTHENTIC_FOG=1` draws the authored range, `SL_XBLA_GAME_ATMOSPHERE=0` the dome unshaded, `SL_SKY_ZENITH=0` the dome without the cap (native-only, render-only, the D-023 / D-029 / D-030 class)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #84: src/platform/sl_presentation.c (`s_fog_range` row 43, `s_game_atmosphere` rows 43 and 943, the self-test); tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-023']` names `surface2`) and its self-test
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      surface2 only (the policy names rows 43 / 943 and one item of the xbla set)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_AUTHENTIC_FOG=1` (D-025's) draws the authored end; `SL_XBLA_GAME_ATMOSPHERE=0` (D-030's) the daylight dome as authored; `SL_SKY_ZENITH=0` (D-023's) leaves the cap out. Developer diagnostics, not settings. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here. docs/world-assets.md "Surface 2's background, sky and atmosphere" is the architecture.
- **Later:**       the end at the cut did not remove the dish's late appearance: the game's cut tests the dish's origin, and the faces it shows lie up to 780 units nearer, 93% fogged when it appeared (run 20261006-235609-lvl43). **D-041** (2026-10-07) draws props past the cut on row 43; the end 7662 and the rest of this entry stand.

**What the game does**

Surface 2 is Surface's background played at night: environment row 43, fog
32 16 16, a cloud layer coloured 58 17 0, and the far-fog prop cut
(`fogGetPropDistColor`, bgfog.c) at zDepth 7662.5 world units - measured,
SL_TABLE_DBG's `cutoff-zdepth`, from BlendMultiplier 2 and FarFog 2000 at the
visibility scale 0.2 with the intensities 957 / 1000.

**What the remaster does**

The source has no Surface 2 background; the stage draws `background/surface`.
Its `skydome/surface2` is the daylight image every source dome carries (the
same two texture ids as `skydome/surface`). Its row 43 keeps the decomp's
colour with the fog end 10000, its row 943 the colour with 40000.

**What Sightline does**

The rules accepted for Surface, keyed on Surface 2's own rows and item: the
presented end of row 43 is the prop cut, 7662 (`sl_presentation_fog_end`);
`sl_presentation_game_atmosphere` names rows 43 and 943, so `sky_draw` shades
the dome with the game's live fog and cloud colours; the preparation writes
the D-023 cap into `skydome/surface2`.

**Why**

Drawn as authored (round srf2a, run 20261006-230603-lvl43 marks 3 and 5-6) the
stage shows a clear daylight sky over the game's night, and the satellite dish
- a prop, `prop/sevdish` - appears whole when the walk carries it inside the
game's cut, because the replacement around it is still 23% clear at 7662 under
the authored end. With the end at the cut the dish comes out of the fog
(sheets dish-steps / dish-back: absent at every back step, faint at the mark,
clearer with every 400-unit step in), and the sky reads as the game's night.

**Why it is safe**

- Every other row and set is returned as before (self-test; the mutation that
  restores 10000 or drops row 943 fails two checks).
- Surface 2 ORIGINAL and COMMUNITY HD at the round's eight marks and the crash
  pose: 18 / 18 frames byte-identical to the build before the change.
- Surface 1 and Frigate XBLA start frames byte-identical; the Dam's, Runway's,
  Surface's and Frigate's domes re-prepared with the new tools to a scratch
  root are byte-identical to the bundles in use (85F1CD88, 49BC4AE7, 9210F46C,
  860D724D).

### D-039 — SIGHTLINE'S OWN SEA ON FRIGATE (#83): under PRESENTATION XBLA Frigate's sky.c sea is drawn by Sightline's own program - the game's water image and shade on the game's plane, its detail in three drifting layers that fade as their texels shrink below a pixel, the sky's reflection by Schlick's Fresnel term and the sky's horizon colour at the horizon - instead of the game's texture times shade; default ON within XBLA, `SL_XBLA_SEA=0` draws the game's sea (native-only, render-only, runtime)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #83: src/gfx/sl_gfx_dl.c (the sea program: `SEA_VS`, `SEA_FS`, `sea_program_ok`, `sea_program_begin`, its use in `draw_rdp_tri`), src/native/sl_world_assets.c (`sl_wa_sea_frame`), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_own_sea`, `SL_XBLA_SEA`), src/platform/sl_presentation.{c,h} (`sl_presentation_own_sea`, its self-test)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      frigate only (the presentation names one stage of the xbla set)
- **First tick:**  n/a - no hashed state moves; src/game is not touched (the seam reads the environment record, the eye and the cloud scroll)
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_SEA=0` draws the game's sea (byte-identical with the build before at eight poses); `1` the layered image and the horizon without the reflection; `9` a diagnostic of the reconstruction. ORIGINAL and COMMUNITY HD select no world-asset set and draw the game's sea.

**What the game does**

sky.c draws Frigate's sea as raw RDP triangles over the plane y = `WaterRepeat`
(-150): the 32x32 water image (0x5e5) under a per-corner shade that runs from
the water colour near the boat (223 226 177) to the sky's navy at the horizon
(16 48 96), each corner's texture coordinates its own world x and z (plus the
cloud scroll in z). Because the image multiplies that navy, the sea meets the
sky a band darker than the sky: at the boarding boat's view out to sea the
sky reads 17 49 97 just above the horizon and the sea 2 14 49 just below it.
One texel spans 32 world units and the image is sampled without a mip chain,
so the far sea shimmers.

**What the remaster does**

One constant navy (6 32 82) at every sea pixel from the boat to the horizon,
unchanged over time (round frg4b, the private emulator). Its source image
(`texture/bg/water2`) decodes with red and blue swapped through
`tools/texpack/xdecode.py`; that decoder defect is shared with other stages
and is left for its own change.

**What Sightline does**

Under the xbla set, on the stage the presentation names, each sea triangle is
drawn through a GLSL program instead of texture times shade. The triangle, its
corners, its shade, the plane and the render mode are the game's. Each pixel's
perspective-divided texture coordinate is run back into the point of the
plane it shows (world x = u x 32 x the tile width x 65536 / 65535, z the
same less the cloud scroll), and with the eye the game draws from (`sl_wa_sea_frame`) the
view ray per pixel is exact. Then: the image's mean (16 taps) plus its detail
in three layers - 1, 0.29 and 2.13 times its scale, weights 1, 0.6 and 0.45,
the last two drifting with the game's clock - each fading out as its texels
shrink below a pixel; under the game's shade; the sky's reflection mixed in by
Schlick's term (0.02 + 0.98 (1 - cos)^5) from the horizon colour up to the
cloud layer at 30%, the detail kept over it at 1 - 0.6 F; and within six
degrees of the horizon the sky's horizon colour.

Three candidates were compared at the boarding boat, its view out to sea and
the helipad looking astern, three frames each, against ORIGINAL and the game's
sea: the layers and horizon alone (form 1) left a dark band under the horizon
fade; a glint toward a light high in the sky (dropped) changed 12k pixels by
0.4 - the night sky has no bright direction to reflect; the reflection (form
2, the default) meets the D-034 sky at the horizon without a seam, keeps the
ripple at middle distance and moves.

**Why**

The sea is the largest surface of the stage. The game's sea against the xbla
sky shows a dark band at the horizon; the remaster's is a flat colour. A sea
of the game's own image, shaded so its horizon is the sky's, keeps the game's
sea and its motion and reads as one scene with the sky.

**Why it is safe**

Render-only, on one stage of one set. Nothing the simulation reads is written;
the seam only reads. `SL_XBLA_SEA=0` frames are byte-identical with the build
before at eight Frigate poses (the boat, the waterline poses, the deck marks,
the helipad); ORIGINAL and COMMUNITY HD frames are byte-identical with the
build before at the same eight. The hull, its D-035 waterline and the boat
are drawn over the sea as before; only the sea's own pixels change. A
missing shader entry point, a program that does not compile or an environment
without a sea draws the game's sea.

### D-038 — FRIGATE'S XBLA C4 IS THE REMASTER'S DEVICE, FITTED ONTO THE GAME'S (#83): under PRESENTATION XBLA the remaster's `prop/chrplastique` - a round mounted device where the game's PchrplastiqueZ is a brick - draws in place of the game's C4 on Frigate's consoles, scaled to stand inside the game model's box with its proportions kept and seated on the model's bottom; default ON within XBLA, `SL_PROP_FIT=0` refuses the item as the box test did (native-only, render-only, runtime)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #83: src/sl_chr_asset.h (`struct sl_prop_box` `fit_ok`, THE FITTED ITEM), src/gfx/sl_gfx_charskin.c (`sl_prop_derive`, its self-test), src/native/sl_chr_asset.c (`prop_map`, `SL_PROP_FIT`), src/platform/sl_presentation.{c,h} (`sl_presentation_prop_fit`, its self-test)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      every stage that places the C4 under XBLA (the item is keyed by model, not by stage); measured on Frigate
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_PROP_FIT=0` (runtime) refuses the item and the game's model draws. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

Frigate places its C4 (model PchrplastiqueZ, a brick: model box -120 .. 120
x 0 .. 104 x -60 .. 60, origin at its bottom) on the bridge and engine-room
consoles (playtest run 20261006-201517, mark 7, room 30). Its existence,
placement, objective state and destruction are the game's.

**What the remaster does**

Its `prop/chrplastique` is, in both of the source's generations, another
object: a round mounted device, a box with two cylinders on a round foot
(-421 .. 536 x -205 .. 235 x -208 .. 208 item units). The prop seam relates
an item to the game's model by the item's reference box (the source's
original generation), and that box is not the brick's (axis 2 off by 12%,
against a 3% tolerance), so the seam refused it and the game's brick drew
under XBLA - the mark the playtest asked to see in the remaster's
presentation.

**What Sightline does**

For an item the presentation names (`sl_presentation_prop_fit`, one entry:
`chrplastique` in the xbla set) a reference box that fails the test is
FITTED: the scale is the smallest that keeps the item inside the model's box
on every axis (item units per model unit, the largest of the three extent
ratios - 4.235 here, the item's height against the brick's), the item's box
is centred on the model's in x and z, and its bottom sits on the model's
bottom, so it stands on the console where the brick stands. It rides the
model's root matrix as any one-piece prop; a destroyed C4 draws as the
game's, as any destroyed prop does. Every other item keeps the box test.

**Why**

The remaster's object for the game's C4 is its own design, not the brick
remastered, and the playtest asked for the remaster's. No measurement of the
source relates the two, so the placement is the one that keeps the game's
footprint: inside the game model's box, on its surface.

**Why it is safe**

The policy names one item of one set; with `SL_PROP_FIT=0`, or for any item
not named, `sl_prop_derive` takes exactly the path it took before (the
charskin self-test's box cases are unchanged and pass). Nothing the
simulation reads is written.

### D-037 — FRIGATE'S XBLA DECK PAINT IS AS SEE-THROUGH AS THE GAME'S (#83): under PRESENTATION XBLA a placement of the remaster's decal pass in Frigate's background - translucent or opaque - that redraws a game decal the game blends under an environment alpha below 255 takes that alpha into its vertex alpha and is drawn translucent, so the engine-room deck arrows and checker, the yellow dashed arcs and circles on the decks, the hazard stripes and a red wall arrow read as paint with the deck plate and its shadow showing through instead of as solid mats; default ON within XBLA, `SL_WA_AS_AUTHORED=D-037` at preparation prepares the background as authored (native-only, render-only, preparation-time, the D-032 / D-033 class)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      a8655fb4, widened by the round frg5 change - Gitea #83: tools/worldasset/gameshade.py (`read_bg_walk`, `decal_opacity`, `opacity_pools`, `describe_opacity`, `_placements`, `decal_extent_iou`, `DECAL_EXTENT_IOU`), tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-037']` names `frigate`) and the tools self-test
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      frigate only (the preparation names one item of the xbla set)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_WA_AS_AUTHORED=D-037` (preparation) writes the background as authored. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

Frigate's deck markings, deck numbers and signs are room triangles in the
decal depth mode on the rooms' translucent lists (render mode 0c184dd8,
FORCE_BL). Before each group the list sets the environment colour (FB), and
its alpha is the paint's opacity: 0x66 or 0x7f on the engine-room arrows and
checker (0x4c on a wall grain tile). At runtime the combiner's alpha is the
image's coverage times that environment alpha - measured in ORIGINAL at the
playtest's marks 3 and 5 (run 20261006-201517): the arrows (texture 0x1d6, a
shape with alpha 255 inside) draw under `fc26a004 1f1493ff`, TEXEL0 x ENV;
the checker (texture 0x1fa) under `fc26a004 1ffc93fd`, ENV alone. So the
arrows are 40-50% yellow over the deck plate, and the checker's yellow
squares the same while its black squares darken the plate by as much.

**What the remaster does**

Its decal pass draws the same markings from translucent draws (style low
byte 2) whose opacity is the vertex alpha times, where the program reads it,
the image's alpha: the arrow image is a cut shape, alpha 255 inside it; the
checker image is an opaque black and white DXT1 image and the program takes
no coverage from it; every vertex alpha is 255. Drawn so (playtest marks 4
and 6) the arrows are opaque saturated yellow slabs and the checker an
opaque black and yellow mat - "rugs" - with the floor's own shadow beside
them reading as a cast shadow.

**What Sightline does**

The preparation reads the game's rooms from the user's ROM with the render
mode and the environment alpha each triangle is drawn under. Each triangle
of a translucent remastered decal draw that lies on a blended game decal
(coplanar within 15 degrees and 2 item units, its centroid inside - D-036's
test) under an environment alpha `a` below 255 has its corners' alpha scaled
by `a / 255`, on copies of the corners appended to the pool, so no other
triangle's corner moves. The remaster's images, colours and coordinates are
kept. On Frigate: 9 draws, 260 triangles - the engine-room arrows (image 77)
and checker (image 63), the deck number "-06-" (image 75), four sign images
(64, 73, 74, 76) at 102/255 or 127/255, and 100 triangles of a near-white
grain tile (image 78) at 76/255.

**Why**

The paint's opacity is the game's, set by the game's own list; the remaster
dropped it, and its marking draws (authored translucent) have no other
source of it. A mat is not what either version paints.

**Why it is safe**

The rule names one item and touches vertex alpha only, on copied corners.
Prepared with the rule switched off (`SL_WA_AS_AUTHORED=D-037`) the
background is byte-identical to the background before it (E61E2C6B), and the
room reader's refactor (`read_bg_full` now reads through `read_bg_walk`)
leaves Silo's D-033 background byte-identical (7F26C0C4). ORIGINAL and
COMMUNITY HD read no bundle.

**Widened: the opaque decals, and only the placements that redraw a game decal (round frg5)**

The playtest (run 20261006-220209, marks 1-3) found the yellow dashed arcs
and circles on the decks still drawn as opaque saturated slabs. The game
draws them as it draws the arrows: room triangles of textures 0x7 and 0x8 in
the decal depth mode, blended (0c184dd8) under `fc26a004 1f1093ff` (TEXEL0 x
ENV) and an environment alpha of 0x66 or 0x7f. The remaster draws them from
an OPAQUE decal draw (image 61, style `0x2b01`, 178 of its 182 triangles on
those game decals), so the first rule - translucent draws only - never
reached them, and an opaque draw reads no vertex alpha. The source's blend
does not say which markings the game paints see-through; the game's list
does. A census of every decal-pass draw of the item on the game's blended
decals under an alpha below 255 found five opaque ones: the dashes (61), the
hazard stripes (69, two draws, 84 triangles), a dark red wall arrow (0, two
draws, 11 triangles, 0x75), and parts of a vent (55) and a sign (68).

The rule now takes the remaster's whole decal pass, with two guards that the
census showed are needed:

- a PLACEMENT (the draw's triangles joined by shared corners, as D-036
  counts them) takes the alpha whole or not at all - one triangle of the
  sign (68) lay on a fainter game decal and alone would have faded half the
  sign;
- the placement must REDRAW the game decal: its extent in its plane and the
  extent of the game triangles found under it overlap by an intersection
  over union of at least 0.25 (`DECAL_EXTENT_IOU`). The redrawn markings
  measure 0.44 .. 0.99 (the low end a remastered dash shorter than the
  game's); the remastered vent at 0.05 (it faded to a ghost on a wall the
  game covers with a 0x21a grain overlay under 0x4c, measured at room 46)
  and image 78 - 20-unit studs on the same overlay, which the first version
  dimmed - at 0.03 and below.

The triangles of an opaque draw that take an alpha move to a copy of the
draw with style low byte 2 placed straight after it (the decal order kept),
or the draw turns translucent whole when all of them do. On Frigate: 12
draws, 433 triangles - the dashes (178 triangles at 102 or 127), the hazard
stripes (84), the red wall arrow (11, at 117), and the first rule's
translucent markings (images 63, 64, 73-77; 160 triangles). Image 78's 100
triangles keep the remaster's opacity. `SL_WA_AS_AUTHORED=D-037` still
prepares E61E2C6B; Silo re-prepared byte-identical (7F26C0C4).

### D-036 — A FRIGATE DECAL READS THE WAY THE GAME PAINTS IT (#83): under PRESENTATION XBLA a decal placement in Frigate's background that the remaster turned half round against the game's orientation - while more than half of the placements of its image agree with the game - is turned back within its own coordinate range, so the yellow "-06-" deck number reads "-06-" on the four walls where the remaster drew "-90-"; default ON within XBLA, `SL_WA_AS_AUTHORED=D-036` at preparation prepares the background as authored (native-only, render-only, preparation-time, the D-032 / D-033 class)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #83: tools/worldasset/gameshade.py (`read_bg_full`, `orient_decals`, `orient_pools`, `describe_oriented`), tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-036']` names `frigate`) and the tools self-test
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      frigate only (the preparation names one item of the xbla set)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_WA_AS_AUTHORED=D-036` (preparation) writes the background as authored. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

Frigate paints the deck number "-06-" (texture 0x1e0) as a decal - a room
triangle in the decal depth mode - at ten places on its walls, and reads
from the ROM's room lists with each corner's s and t, it runs +s and +t the
same way relative to the wall at every one of them (ORIGINAL, playtest run
20261006-185746 mark 3: "-06-" on the red wall beside the stair in room 25).

**What the remaster does**

Its background draws the ten placements from one draw of one image (a
256x128 "-06-" with alpha). Six relate to the game's placements one way;
four - two in room 25, one in room 27, one at world (349, 548, -3180) - have
both coordinates reversed, the image turned half round, so the number reads
"-90-" there. The coordinates are the source's own (draw 0x5190, no
transform); nothing in the preparation turns them.

**What Sightline does**

The preparation reads the game's decal triangles with their s and t, scaled
into the item. Each triangle of a remastered draw in the source's decal pass
(style bit 0x2000) that lies on a game decal triangle (coplanar within 15
degrees and 2 item units, its centroid inside) relates its +u and +v to the
game's +s and +t by a pair of signs. Per image and game texture, the pair
more than half of the placements (triangles of one draw joined by shared
vertices, all agreeing) hold is the image's; a placement with BOTH signs
reversed is turned back - each coordinate mirrored within the placement's
own range, on its own vertices (a vertex another triangle uses is copied). A
placement with one sign reversed - a mirror - is left alone: measured on the
"HMS RABBETTE" life rings, the remaster's placements read upright on both
sides of the bridge while two of five differ from the game's by a mirror, so
a mirror can be the remaster correcting the game rather than breaking it. On Frigate: 27 decal images lie on game decals; 13
placements are turned - the four "-06-" and nine of a near-uniform white
grain tile (texture 0x21a) whose turn is not visible.

**Why**

The number is the game's, and it reads one way round in the game everywhere;
four placements reading "-90-" are the remaster's authoring error.

**Why it is safe**

The rule names one item. Prepared with the rule switched off
(`SL_WA_AS_AUTHORED=D-036`) the background is byte-identical to the
background without it, and the refactor of the room reader it rests on
leaves Silo's D-033 background byte-identical. Engine-room frames where
"-06-" already read correctly (mark 4) are unchanged.

### D-035 — FRIGATE'S XBLA HULL ENDS AT THE GAME'S WATERLINE (#83): under PRESENTATION XBLA the remaster's opaque triangles in Frigate's background are cut at the game's waterline - the lowest corner of any opaque room triangle, world -259 - and their part below it is withheld, so the hull stops on one horizontal line along the whole ship, and below it the game's own translucent fade is drawn over the game's sea as the game draws it; default ON within XBLA, `SL_WA_AS_AUTHORED=D-035` at preparation prepares the background as authored and `SL_BG_FADE=0` leaves the game's fade out (native-only, render-only, preparation-time and runtime, the D-032 / D-033 class)

- **Date:**        2026-10-06 (revised the same day, round frg3: a cut at the waterline replaces withholding whole triangles)
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #83: tools/worldasset/gameshade.py (`read_bg_lists`, `is_fade`, `waterline`, `withhold_translucent_only`, `withhold_pools`, `describe_withheld`), tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-035']` names `frigate`) and the tools self-test; src/gfx/sl_gfx_dl.c (`nf_fade_kept`, the game's fade under the replacement)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      frigate only (the preparation names one item of the xbla set; since round jng2, #94, the runtime's fade is drawn on the stages `sl_presentation_game_fade` names - Frigate - where it had reached every stage whose rooms pass the shade test: Jungle's backdrop cards, Train's track strips, one of Statue's)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_WA_AS_AUTHORED=D-035` (preparation) writes the background as authored; `SL_BG_FADE=0` (runtime) suppresses the game's fade under XBLA as every other room triangle; `SL_BG_FADE_STAGES=0` draws it on every stage, the rule before #94. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

Frigate's hull side (room 35) is opaque down to world y -259 - the
waterline, where the boarding boat's bottom sits (-254.7). Below it the
room's SECONDARY list carries a skirt down to -876: translucent (render mode
0c1849d8, the XLU depth mode, no depth write), image 0x4fd (a 32x32 vertical
alpha ramp, alpha 7 .. 250) times a shade that is black at the waterline and
white at depth, so the hull darkens into the sea. Read from the ROM's room
lists: every game triangle with a corner below world -262 is one of those 30
skirt triangles (24 on the port side, 6 at the bow); no opaque room triangle
reaches below -259.2. The sea is the sky pass's plane (sky.c, the stage's
water height -150), drawn behind everything without depth: the waterline the
scene shows is where the hull's opaque paint stops.

**What the remaster does**

Its background registers to the game's rooms within a world unit (hull
bottom -876.2 against -875.8, boat floor -254.5 against -254.7) and carries
the skirt in the hull's own opaque draw, hull paint under a vertex colour of
152 at the waterline rising to 255 at depth. Drawn as authored, the hull
continues some 600 units under the boat, lighter with depth, and the sea
reads as standing far below a floating boat (playtest run 20261006-173108,
marks 1 and 2). The console remaster draws the same opaque hull under the
boat (one bounded capture, its debug camera beside the boat). Its hull
triangles do not follow the waterline: 171 run from above it to below it,
all in four draws of one hull image; nothing else of the item reaches below
-259.

**What Sightline does**

The preparation reads the game's rooms from the user's ROM with the list
and render mode each triangle is drawn under. Where the rooms carry a fade (a
translucent secondary-list triangle whose shade spans 32 or less to 224 or
more in some channel) the WATERLINE is the lowest corner of any opaque room
triangle. Every triangle of an opaque remastered draw (no blended style, an
image without alpha) is cut by the horizontal plane at that height: the part
below is withheld, the part above kept, its new corners carrying position,
coordinate and colour interpolated along the cut edges and appended to the
pool. On Frigate: waterline item y -315.52 (world -259.2), 133 triangles
wholly below withheld, 179 cut (269 kept above), 256 corners added, 4 draws
touched, one of which loses every triangle and is dropped.

Under XBLA the replacement suppresses the game's room triangles; the
renderer keeps a FADE of a translucent list (the same shade test, read from
the loaded vertices) where no replacement surface lies within the native
fill's registration radius (8 world units) of its centroid, and draws it as
the game does. Below the cut that is the skirt: the hull darkens into the
sea under the waterline as in ORIGINAL. The fade at the boat's wheel lies
inside the remaster's boat and draws nothing (a frame over the wheel is
byte-identical with `SL_BG_FADE=0`).

The first form of this rule (beb29f90) withheld only whole triangles lying
on the skirt (59); the 171 that cross the waterline stayed, and the hull
ended in a jagged edge that followed the remaster's triangulation - a
dangling hull panel beside the boarding boat and open sea beside it
(playtest run 20261006-185746, mark 1).

**Why**

The waterline is the game's: the boat and the hull's opaque edge both sit on
it in every presentation. A solid hull under the boat is the remaster's
conversion of a fade into paint, and reproducing it is reproducing a defect;
one cut height keeps one sea level along the whole ship.

**Why it is safe**

The rule names one item, and needs a fade in the stage's rooms: measured
from the ROM, the Dam, Facility, Runway, Surface, Bunker 1 and Silo carry
none. On Frigate it touches only the hull image's four draws (every
remastered triangle below -259); the boarding boat's floor (-254.5) and
everything above the waterline draw as before.

### D-034 — FRIGATE'S XBLA SKY IS THE GAME'S NIGHT OVER THE DOME'S CLOUDS (#83): under PRESENTATION XBLA Frigate's sky dome is drawn as a cloud cover - each texel of the remaster's daylight dome image (and of its D-023 zenith cap, extended to Frigate) written as the fraction of cloud in it - shaded as the game shades its own cloud layer with the colours of the fogless record the game loads for the stage (sky navy 16 48 96, clouds 230 230 230), so sky, horizon and the game's sea read as one night; nothing else of the stage moves (no fog, range, fill or sea); default ON within XBLA, `SL_WA_AS_AUTHORED=D-034` at preparation prepares the dome's images as authored and `SL_XBLA_GAME_ATMOSPHERE=0` draws the dome unshaded (native-only, render-only, preparation-time and runtime, the D-023 / D-030 class)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #83: src/platform/sl_presentation.{c,h} (`sl_presentation_fogless_atmosphere`, its self-test), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_stage_game_atmosphere`, its self-test), src/gfx/sl_gfx_dl.c (`sky_frame_begin`, `sky_draw`); tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-023']` and `['D-034']` name `frigate`, `cloud_cover`, `grade_cloud_cover`) and its self-test
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      frigate only (the policy and the preparation name one item of the xbla set)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_WA_AS_AUTHORED=D-034` (preparation) writes the dome's images as authored; `SL_XBLA_GAME_ATMOSPHERE=0` (runtime, D-030's) draws the prepared dome unshaded - grey, a diagnostic; `SL_SKY_ZENITH=0` leaves the D-023 cap out. Developer diagnostics, not settings. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here. docs/world-assets.md "Frigate's sky" is the architecture.

**What the game does**

Frigate has no row in `fog_tables`; single-player loads its record from
`fog_tables2` (bgfog.c): no fog, sky colour 16 48 96, a cloud layer of
colour 230 230 230 at height 3000, and the sea (water image 2) at -150.
`sl_fog_environment_id` answers -1. sky.c draws the cloud layer over the
navy and the sea below it.

**What the remaster does**

`skydome/frigate` is the clear daylight image every source dome carries,
with a band of snowy mountains at its foot. Its sea is the game's: the
source's `texture/bg/water1` and `water2` are 32x32, the game's size, and
its Frigate background stops at the hull. Drawn as authored (measured at the
round's marks 1, 2 and 7) the dome is a bright day sky with a mountain range
on the horizon over the game's night-navy sea.

**What Sightline does**

Preparation: for `frigate` the D-023 zenith cap is generated from the dome's
own band as on the Dam, then every image of the dome is written as its cloud
cover, grey `clamp(1 - s / 0.35) * clamp(v / 200)` with s the saturation and
v the value of the texel, alpha kept (measured on the image: blue sky
s 0.35 .. 0.55, white and pink cloud s 0 .. 0.2). Runtime: while the game
holds no environment row, the policy names `frigate`
(`sl_presentation_fogless_atmosphere`), and `sky_draw` takes D-030's combiner
and `sl_wa_sky_shade` with the colours of the record in force
(`sl_wa_game_atmosphere`, read live). The cover lays the cloud colour over
the navy overhead and fades into the navy toward the horizon, where the
mountains therefore vanish. Shading the authored image instead left the
overhead sky pale blue, because the combiner reads every bright texel as
cloud.

**Why**

The remaster's dome for the stage is a placeholder and the stage's own sky
is the game's night; drawn as authored it contradicts the sea and the
stage's colours. This keeps the remaster's cloud imagery and the game's
hour, through the mechanism already accepted for Surface (D-030, D-023).

**Why it is safe**

- `sl_wa_stage_game_atmosphere` with an environment row is
  `sl_wa_env_game_atmosphere` unchanged (self-test); Bunker 1's borrowed sky
  takes the D-031 branch, which is untouched; no other item is named.
- The Dam's, Runway's and Surface's domes re-prepared with the new tools to
  a scratch root are byte-identical to the bundles in use (85F1CD88,
  49BC4AE7, 9210F46C); backgrounds are not touched by the change.
- Frigate ORIGINAL and COMMUNITY HD at the round's 12 marks: byte-identical
  to the build before the change.
- presentation self-test, worldasset self-test and worldasset tools
  self-test pass (counts in the commit).

No file under src/game is touched.

### D-033 — SILO'S WHITENED RAIL-END CAPS TAKE THE GAME'S TEAL (#82): under PRESENTATION XBLA the 15 rail-end caps of Silo's background that the remaster draws pure white (30 untextured triangles) are drawn in the game's own shade for the surface under each - teal 002f2f, carried into the remaster's colours through the curve the stage's other untextured surfaces follow (002121) - so they match the caps the remaster kept teal; no other surface moves; default ON for Silo alone (design decision, round silo4), `SL_WA_AS_AUTHORED=D-033` at preparation prepares the background exactly as authored (native-only, render-only, preparation-time, the D-023 class)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      this commit - Gitea #82: tools/worldasset/gameshade.py (`stage_bg`, `read_bg_shaded`, `restore_background_shade`, `shade_pools`), tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-033']`, `prepare_background` and `--rom`), tools/windows/prepare-xbla-assets.ps1 (the ROM to the background conversion, tools\texpack\uvcarry.py in the tool hash), tools/worldasset/selftest.py (a synthetic stage)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      silo only, by an explicit enable (`DIVERGENCE_ITEMS['D-033'] = ('silo',)`). The rule is general in form; enabled on the frozen stages in a scratch preparation it changes none of them - the Dam and Runway have no untextured surface over the game's rooms to measure the curve on, Facility too few (18 channel pairs of 24), Bunker 1's do not follow one curve (spread 0.77), and Surface's 96 white triangles lie over white or no game surface (72) or share vertices with other surfaces (24, refused) - and it stays off there.
- **First tick:**  n/a - no hashed state moves; no runtime code changes
- **Fields:**      none. Pixels only, and only the 15 caps
- **Toggle:**      preparation-time. `SL_WA_AS_AUTHORED=D-033` (the D-032 switch's list) and `.\tools\windows\prepare-xbla-assets.ps1 -Level silo -Background` prepare the background as the source authors it - measured byte-identical to the bundle before this change; clearing the variable and preparing again restores the teal. Without the ROM it is prepared as authored and the preparation says so.

**What the game does**

Each rail end inside the silos is capped by two shade-only triangles (no
texture) whose vertex colours are teal: 002f2f on every one of the 15 the
remaster whitened (read from the ROM's background file, and witnessed at
playtest mark 1), 002424 / 002525 on the ones it kept.

**What the remaster does**

`new/background/silo` draws all the caps in one untextured draw (no image,
the vertex colour alone). 15 caps carry 0xFFFFFFFF; the rest carry the
game's teal darkened by the same curve as every untextured surface of the
stage that coincides with the game's - 255 (g / 255) ^ 1.218 per channel,
measured over 762 channel pairs with a quartile spread of 0.007 (the
mark-2 end's game 002424 is its 001818). Its ORIGINAL generation carries
the same white.

**What Sightline does**

`gameshade.restore_background_shade`, for the stages
`DIVERGENCE_ITEMS['D-033']` names: the game's room triangles are read from
the ROM with their vertex colours and placed in the item by the game's own
scale for the stage (100 / levelinfotable's last column, the factor the
runtime draws the item by). An untextured remastered triangle lies on the
game triangle containing its centroid's projection, planes within 15
degrees (the remaster tilts some caps by 11) and 2 item units. The curve
is measured on the untextured triangles the remaster did not whiten. A
triangle white on every corner whose game surface is not white takes, per
corner, the game's shade there carried through the curve - unless one of
its vertices is shared with a surface that would not take the same colour,
when it is reported and left white. On Silo: 30 triangles, all over game
002f2f, all to 002121; none left, none refused. The bundle differs from
the one before in exactly those 60 vertex colours.

The 15 caps, by the game room they lie in: room 5 (2), 11 (3, mark 1's
among them), 12 (2), 17 (4), 18 (2), 27 (2) - every one over 002f2f.

**Why**

Design decision (#82, round silo4): the white ends read as a defect beside
their teal opposites, and the game draws both teal. The curve is taken so
that a corrected cap stands to its kept opposite as the game's do (game
002f2f / 002424, drawn 002121 / 001818) rather than nearly twice as bright.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed 00000000760538d1, read 80, build 73B8F500 (no
runtime change), the playtest marks of run 20261006-114157-lvl20:

- mark 1: XBLA with D-032 -> with D-032 and D-033 differs in 3872 pixels,
  every one ffffff -> 002121, the cap alone;
- mark 2, the correct opposite end: byte-identical with and without D-033;
- the steel door pose: byte-identical with and without D-033;
- ORIGINAL byte-identical at all three;
- the bundle: `SL_WA_AS_AUTHORED=D-033` and no ROM both prepare
  A100CAC044B67BE6, the bundle before, byte for byte; the Dam, Facility,
  Runway, Surface and Bunker 1 backgrounds prepare byte-identically with the
  tools before and after this change;
- worldasset tools selftest 571/0 (7 new: the stage table, the curve, a
  synthetic stage's kept, whitened, white-over-white and shared-vertex
  surfaces, the Silo-only enable); with the shared-vertex guard off, 2 fail.

Nothing the derivation reads or produces is committed as data.

### D-032 — THE REMASTER WHITENED A GAME-TINTED HAZARD BAR (#82): under PRESENTATION XBLA Silo's steel door (`prop/steel_door1`) draws its edge hazard bars yellow and black - the preparation multiplies the bar texels of the remaster's door image by the tint the game draws them with (vertex colour e4e42d, read from the user's ROM) - instead of the remaster's black and white; nothing else of the door's image moves; default ON (design decision, round silo4), `SL_WA_AS_AUTHORED=D-032` at preparation prepares the door exactly as authored (native-only, render-only, preparation-time, the D-023 class)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      this commit - Gitea #82: tools/worldasset/gameshade.py (NEW - the derivation), tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-032']`, `SL_WA_AS_AUTHORED`, `convert_prop` takes the ROM), tools/worldasset/stageprops.py (`prepare --rom`), tools/windows/prepare-xbla-assets.ps1 (the ROM to `-Props`, gameshade.py in the tool hash, the switch in the stamp), tools/worldasset/selftest.py (synthetic model, item and image)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      silo only. The bundle is per model and shared by every stage that draws it; `steel_door1` (prop file record 141) is drawn by Silo alone - the props derivation (`stageprops.stage_props`) over every campaign stage names it on Silo only (23 dynamic doors), and a tree-wide `grep '_mkword(141,' assets/obseg/setup` finds only the three countries' `UsetupsiloZ.c` (23 each). No frozen stage (Dam, Facility, Runway, Surface 1, Bunker 1) draws it.
- **First tick:**  n/a - no hashed state moves; no runtime code changes
- **Fields:**      none. Pixels only, and only the door's bar column
- **Toggle:**      preparation-time. `SL_WA_AS_AUTHORED=D-032` (a comma list; the preparation folds it into its stamp) and `.\tools\windows\prepare-xbla-assets.ps1 -Level silo -Props` prepare the door as the source authors it - measured byte-identical to the bundle before this change; clearing the variable and preparing again restores the yellow. Without the ROM the door is prepared as authored and the preparation says so.

**What the game does**

The game's model (Psteel_door1Z, read from the ROM: one DLCOLLISION node)
draws the door's metal under white vertex colours, and its hazard bars as a
separate draw of a 32x32 black-and-white bar image (texture 0x7) under
vertex colour e4e42d - alpha 0x72 on the front face, 0x66 on the back - a
translucent decal over the metal. The combiner multiplies the image by that
colour: the white bars are yellow, the black bars black. The footprint is
x 353..467, y 0..840 and 1260..2100 of the model, on both faces.

**What the remaster does**

`new/prop/steeldoor1` is one draw of one 256x512 image with the bars painted
into its right-hand column black and white, every vertex colour white, its
program the image times the vertex colour times the scene light. Its
ORIGINAL generation draws the game's bar image opaque at vertex colour 255:
the conversion whitened the tint, and the new art was painted from that.

**What Sightline does**

`gameshade.restore_prop_tint`, for the items `DIVERGENCE_ITEMS['D-032']`
names: the game's tinted triangles (one non-white colour on a textured
triangle) are placed on the item by the box relation the runtime draws it
by (the model's box onto the ORIGINAL generation's reference box, per axis),
found on the remastered triangles (coplanar, containing each corner, the
remastered vertex colour white) and read through their texture coordinates
into texel rectangles - cols 221.2..256, rows 0..204.8 and 307.2..512. The
remaster's painted bars are registered to the game's footprint to within
2.8 texels, so each inner edge moves to where the image's luminance rises
most into the rectangle within 3 texels (the bars sit inside a dark divider
column and separator rows; a plain largest step took the divider's outer
edge on one block) - cols 223..256, rows 0..202 and 310..512. Inside, each
texel is multiplied by e4e42d (12643 texels change: every non-black bar
texel). The handle plate, the rivets, the divider and every other texel are
the source's. The game's alpha (a translucent decal) is not carried - the
remaster's bars are opaque art, so this is the tint without the blend.

**Why**

Design decision (#82, round silo4): the game shows yellow-and-black hazard
bars on this door, and the remaster's black and white is its conversion's
whitening, not a design (round silo3 measured both generations). A recolour
of the whole door would tint the metal; the rule touches only the texels
the game itself tints.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed 00000000760538d1, read 80, build 73B8F500 (no
runtime change), scratch world-asset roots before and after:

- the door pose (round silo2's mark 3): XBLA before -> after differs in
  19955 pixels, all in x 646..698 - the bar column; the playtest mark-2 pose
  of run 20261006-114157: 60002 pixels, x 917..1062 - another steel door's
  edge, now yellow; mark 1: 101 pixels, x 482..505, a door edge in shadow;
- ORIGINAL byte-identical at all three poses; XBLA repeatable (same frame
  hash twice);
- the bundle: `SL_WA_AS_AUTHORED=D-032` and no ROM both prepare
  F1D8F4B43FFD5963, the bundle before this change, byte for byte;
- worldasset tools selftest 564/0 (15 new: the model reader, the tint, the
  box, the footprint, the snap and multiply, the switch); a no-op multiply
  fails 2 of them.

Nothing the derivation reads or produces is committed as data: the tint,
the footprint and the image are computed at preparation and live only in
the gitignored bundle; the selftest's model, item and image are synthetic.

### D-031 — BUNKER 1 BORROWS SURFACE 1'S SKY UNDER XBLA (#81): under PRESENTATION XBLA, wherever Bunker 1 shows the outside (the start vent shaft, up through its grate) the stage draws Surface 1's sky - skydome `surface`, its D-023 cloud field and D-030's game atmosphere for environment row 36 - instead of the game's navy fill (0 16 64); a deliberate continuity change, not a remaster fact; the sky only, no fog, range or fill of row 36 reaches the stage; default ON within XBLA, `SL_XBLA_BORROWED_SKY=0` draws the navy fill as before (native-only, render-only, the D-021 / D-025 class)

- **Date:**        2026-10-06
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      8f33c37c - Gitea #81: src/platform/sl_presentation.{c,h} (`sl_presentation_sky_borrow`, its self-test), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_sky_borrow`, its self-test), src/native/sl_world_assets.c (`sl_wa_game_atmosphere_row`, `sl_wa_sky_cloud_row`, read-only), src/gfx/sl_gfx_dl.c (`sky_frame_begin`, `sky_draw`)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      bunker1 only (the policy names one item of the xbla set)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_BORROWED_SKY=0` keeps the game's navy fill behind the grate (the picture before this change, byte for byte). `SL_XBLA_GAME_ATMOSPHERE=0` and `SL_SKY_ZENITH=0` act on the borrowed sky exactly as on Surface's (D-030, D-023). Developer diagnostics, not settings. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here.

**What the game does**

Bunker 1 has no environment row: `fog_tables` (bgfog.c) carries rows for
its multi-player variants only, so single-player loads the fogless record
(`fog_tables2`) and `sl_fog_environment_id` answers -1. sky.c's `skyRender`
then draws no sky and fills the view with that record's colour, navy 0 16
64. The remaster keeps that fill: the start shaft is closed at the top by a
grate, and behind it is the same flat navy (bnk2, measured on the console,
Xenia). The stage lies under Surface 1, and the shaft is its one view out.

**What Sightline does**

The presentation policy names (xbla, `bunker`) -> skydome `surface` under
environment row 36 (`sl_presentation_sky_borrow`). While the game holds no
environment row on that stage (`sl_wa_sky_borrow`), the renderer's sky pass
(`sky_frame_begin`, `sky_draw`) resolves skydome `surface` instead of the
absent `bunker`, and draws it exactly as Surface does - same asset, same
eye anchoring and aperture, same D-023 cloud field cast on the game's cloud
plane, same D-030 combiner and `sl_wa_sky_shade` - with row 36's fog colour,
cloud colour, cloud height and cloud image read from the game's own table
(`sl_wa_game_atmosphere_row`, `sl_wa_sky_cloud_row`: `fog_tables` row 36,
field for field as `fogLoadCurrentEnvironment` copies it: 96 96 128, cloud
240 120 30, height 10000) and the live cloud phase and eye (skyTick runs on
every stage). Nothing else of row 36 is consumed: the object fog, the
replacement's stage fog and the D-026 shell fade all key on the environment
the game selected, which is still none, so Bunker 1 keeps its fogless range
and fill (`sl_wa_fogless_range`, #81 958263bd) and the bundle's own exterior
(the panorama and cloud sheet ~14000 units out) stays beyond the game's far
plane, not drawn. The dome is drawn depth-less before the replacement, so it
shows only where the replacement leaves the view open - up the shaft.

**Why**

Design direction (#81, bnk4): Bunker 1 should read as the same hour as
Surface 1 wherever the sky shows, not as an abrupt dark or night sky. The
navy is the game's and the remaster's by design of the game data (no
environment row), so this is a continuity improvement chosen on purpose, and
it reuses Surface's accepted sky mechanism and assets rather than
approximating them.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed 000000005ae44bbd, read 80, HEAD accfc518 against the
change:

- the start shaft (playtest bnk2 mark 1, eye -1411 340 2345, looking up) and
  five yaw/pitch variations: Surface's sky behind the grate at every pose; the
  only differing pixels at the mark are one region, the grate opening;
- `SL_XBLA_BORROWED_SKY=0` at the mark: byte-identical to HEAD;
- XBLA interiors (bnk3 mark 1 hazard-strip wall, the control room, the spawn
  room): byte-identical;
- ORIGINAL and COMMUNITY HD at the mark and at the hazard-strip wall:
  byte-identical;
- Surface 1's XBLA start frame: byte-identical;
- presentation self-test 48/0, worldasset self-test 437/0, charskin 146/0,
  worldasset tools 543/0, headless Bunker 1 (`test.ps1 -Level bunker1`)
  PASS, layering 282 (baseline 282).

No file under src/game is touched; the two new provider reads write nothing.

### D-030 — SURFACE'S XBLA ATMOSPHERE IS THE GAME'S (#80): under PRESENTATION XBLA Surface's gameplay stage fog (environment 36) fades to the colour the game's own environment holds (96 96 128) instead of the remaster's authored 164 150 130, the sky dome and its generated cloud field (D-023) are shaded as the game shades its own cloud layer (sky.c: the cloud colour over the fog colour overhead, fading into the fog colour toward the horizon), and the background's panorama shell fades where that fog covers it (D-026, extended to Surface); range, start and distance equation stay D-029's; default ON within XBLA, `SL_XBLA_GAME_ATMOSPHERE=0` draws the remaster's colour and the dome as authored, `SL_SKY_PANORAMA=0` the shell as authored (native-only, render-only, the D-021 / D-025 class)

- **Date:**        2026-10-05
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #80: src/platform/sl_presentation.{c,h} (`sl_presentation_game_atmosphere`, its self-test), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_env_game_atmosphere`, `sl_wa_env_coloured`, `sl_wa_sky_shade`, their self-tests), src/native/sl_world_assets.c (`sl_wa_game_atmosphere`, read-only), src/gfx/sl_gfx_dl.c (`env_in_force`, `sky_draw`, `sky_atm_texenv`); tools/worldasset/prepare.py (`DIVERGENCE_ITEMS['D-026']` names `surface`) and its self-test
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      surface only (the policy names one row of the xbla set; the D-026 list one more item)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_GAME_ATMOSPHERE=0` draws the remaster's fog colour and the dome as authored; `SL_SKY_PANORAMA=0` draws the shell as authored (D-026's switch); `SL_XBLA_AUTHENTIC_FOG=1` the authored range (D-025's). Developer diagnostics, not settings. ORIGINAL and COMMUNITY HD select no world-asset set and read nothing here. docs/world-assets.md "The game's atmosphere on Surface (D-030)" is the architecture.

**What the remaster does**

Its Surface row (an extension of `fog_tables`' row 36 in its executable,
read by `tools/worldasset/xexenv.py`) fogs toward 164 150 130, a warm
grey-beige. Every other row of that table it carries keeps the decomp's own
colour except the three stages it re-dressed: the Dam and Runway (both 133
173 202, a clear-sky blue) and this one - measured over the 57 rows of the
derived file. It is Surface's row and no other's: id 0x24, found by the
signature of the decomp's own row 36, in table order between Runway's
variants and Jungle's. Its sky dome for Surface is byte for byte the Dam's:
all eleven of the source's skydome items carry one image (the same 1024 x
1024 clear-blue sky with a ring of cloud), so no stage of the beta had a sky
of its own. A warm haze under a clear sky - an unfinished stage.

**What the game does**

Surface's environment is fog 96 96 128 with a cloud layer coloured 240 120
30 (`fog_tables`, bgfog.c). sky.c draws the clouds through (SHADE - ENV) *
TEXEL0 + ENV with ENV the fog colour and SHADE = fog + cloud * (1 - fog /
255) * (1 - frac), frac = 1 - min(1, 2 * tan(elevation))
(`skyChooseCloudVtxColour`, `skyIsScreenCornerInSky`): pink-purple cloud
overhead, fading into the blue-grey fog colour at the horizon, the same
colour the fogged snow, trees and props fade to. One storm.

**What Sightline does**

The presentation policy names (xbla, 36) (`sl_presentation_game_atmosphere`).
For that row the renderer reads the game's environment each frame
(`sl_wa_game_atmosphere`, read-only) and:

- the row in force (`env_in_force`) takes the game's fog colour - so the
  replacement background, the props and characters fogged by the same row,
  and the panorama shell all fade to it; range and start are D-029's;
- the sky dome and the D-023 cloud field are drawn through the game's
  combiner: per vertex the game's cloud shade along that vertex's direction
  from the dome's anchor (`sl_wa_sky_shade`), interpolated by the dome's own
  image toward the fog colour (`sky_atm_texenv`, GL_INTERPOLATE); alpha as
  before;
- the background's outermost ring (meshes 36 and 48, found by
  `prepare.panorama_shell`) is marked as the Dam's is, and D-026 fades it
  where the stage fog covers it, so the fully fogged ring no longer stands as
  a flat band in front of the sky.

Nothing in the presentation holds a colour: the game owns the environment and
the presentation consumes it, and a row the game changes is followed.

**Why**

The playtest run (20261005-100452-lvl36, marks 1 and 2, XBLA): "dirty
beige/grey haze between the snow and a bright blue cloud cap; it does not
read as a snowstorm", against ORIGINAL's cold storm. The first wrong stage
is the consumed atmosphere itself: the remaster's beta colour (measured, row
36's bytes) under the remaster's shared placeholder dome (measured,
identical images), made a flat band by D-029's range, which fogs everything
past 12071 world units - mountains and the painted ring included - to that
colour, and capped by the unfogged D-023 field. Taking the game's colour and
the game's sky shading makes the haze and the sky one storm; the shell's
fade lets the sky come down to the haze with no edge.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed ffffffffac36f825, read 80, at the three playtest
marks:

- **ORIGINAL and COMMUNITY HD** byte-identical to the build before at all
  three marks; the Dam walkway witness and a Facility mark byte-identical
  under XBLA (no row there is named).
- **The switches.** With `SL_XBLA_GAME_ATMOSPHERE=0` and
  `SL_SKY_PANORAMA=0` the re-prepared bundle draws the build before's
  picture except where the two #80 fixes landed with this act: the far
  pass's streaks (mark 2, 1701 pixels, all on the streaks' lines) and the
  sniper towers' two-sided platforms (mark 3, 3638 pixels on the platform;
  mark 1, 3 pixels; mark 2, 445 pixels on the far tower) - no atmosphere
  pixel.
- **No pop returns.** The satellite dish across the game's prop cut (srf3's
  ladder, culled at zDepth 12073.6, drawn at 12069.4): in its window the
  frame changes by at most 5 levels (mean 0.04), the camera step's own
  noise; the build before measured 8 (0.02).
- **The bundle.** The re-prepared Surface background differs from the one
  before in exactly two material flag words (2 -> 18, `SL_WA_MAT_PANORAMA`).
- Nothing derived from any source is committed: the table holds one row id.
- **Not claimed.** The remaster's mountains (20000 - 38700 units) stay
  beyond the fog end - the haze hides them, as the game's own fog hides
  everything past it; showing them needs the game's prop cut lifted on the
  draw side, which was not done.

### D-029 — SURFACE'S XBLA FOG RANGE SHORTENED TO THE GAME'S PROP DRAW DISTANCE (#80): under PRESENTATION XBLA Surface's gameplay stage-fog row (environment 36) is drawn with a fog end of 12071 world units instead of the authored 45000 - the view depth at which the game's far-fog cut stops drawing props and characters - so an object is wholly fog-coloured when the game removes it; colour, start and the distance equation are the remaster's; default ON within XBLA, `SL_XBLA_AUTHENTIC_FOG=1` draws the authored range (native-only, render-only, the D-021 / D-025 class)

- **Date:**        2026-10-05
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this change) - Gitea #80: src/platform/sl_presentation.{c,h} (the second row of `sl_presentation_fog_end`'s table, its self-test), src/gfx/sl_gfx_worldasset.c (the load line names the range, the provider's self-test)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      surface only (the table names one row of the xbla set)
- **First tick:**  n/a - no hashed state moves; no simulation code is touched
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_AUTHENTIC_FOG=1` draws every environment row at its authored range (the D-025 switch, unchanged). A developer diagnostic, not a setting. ORIGINAL and COMMUNITY HD select no world-asset set and read no row. docs/world-assets.md "Presentation fog range on Surface" is the architecture.
- **Later:**       the RANGE above is unchanged, but its colour and what it fogs against were changed on 2026-10-05 by **D-030**: the row's colour is the game's (96 96 128), the sky is shaded as the game's cloud layer, and the panorama shell fades (D-026 on Surface). The paragraph below calling the fully fogged band "one flat band of the fog colour" describes the remaster's beige under the unfogged cap, which was rejected in playtest ("dirty beige haze", run 20261005-100452-lvl36). This entry is left as written, because it records what was measured on the day.

**What the remaster does**

Its Surface gameplay row fogs from -100 to 45000 world units toward 164 150
130, by the straight-line distance from the eye - reproduced exactly (#68,
#80). The game's props and characters are fogged by the same row under XBLA
(`sl_wa_object_fog_env`), but whether one is drawn at all is still the
game's decision.

**What the game does**

`fogGetPropDistColor` (src/game/bgfog.c) returns 0 - "completely obscured by
fog, don't render" - once the cartridge's own object fog alpha,
`far_fog_dist_scaled / zDepth + near_fog_dist_scaled`, exceeds 1, and
`chrobjRenderProp` (src/game/propobj.c) and the character render (chr.c)
then return without a display list. On Surface's row (BlendMultiplier 2,
FarFog 2500, visibility scale 0.2, intensities 996 / 1000) the
coefficients are -1381.4946 and 1.114445, so the cut is at view depth
**12071.2** world units - exactly where the cartridge's own fog reaches full
density. Under the cartridge's fog the cut is invisible. Under the authored
remaster row an object at that depth is only 27% fogged, so it appears or
vanishes whole: measured on the satellite dish (`sevdish`, an autogun prop,
room 12) walking from one replay camera toward it, CULLED-fog at zDepth
12073.6 and drawn at 12069.4, the tick side admitting it on screen
throughout (no near-fog, room, portal or level-of-detail involvement; the
game's model with the prop seam off vanishes at the same step).

**What Sightline does**

The presentation policy's table gains one row, (xbla, 36, 12071). The cut
tests the view depth and the fog the straight-line distance, which is never
shorter, so at the cut every object's origin is already at full fog; its
nearer parts are at most the object's own extent inside. The end is the cut
itself, rounded down - not a tuned value. The provider, the lookup and every
consumer are D-025's; the load line prints `environment row 36 fog end 45000
authored, 12071 presented`.

**Why**

A presentation choice: a shorter visible distance on Surface, with the end
placed where it removes the pop rather than by eye. The pop at the cut,
measured in the dish's window (the frame just before against the frame just
after, a few units apart): end 45000 max 126 levels, mean 21.3; 30000
99 / 15.4; 20000 68 / 10.2; 16000 44 / 6.3; 12071 max 8, mean 0.56, which is
the noise of the camera step itself. The mean follows the residual
(E - 12071) / (E + 100) of each candidate end E; only the end at the cut
takes it to nothing.

What it costs, measured at the replay cameras, four more headings, a
look-up and two other outdoor poses: the remaster's mountains (20000-38700
units) and its painted sky shell behind them are fully fogged, so between
the treeline and the zenith cloud cap (D-023, not fogged by the stage fog)
the frame is one flat band of the fog colour. It has no edge: everything
beyond the end is the same colour, and the cap's own feathered rim meets it
softly. Snow, fences, cabins and trees nearer than 12000 keep their shapes,
increasingly warm with distance (50% fog at 6000).

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed pinned, read 80:

- **The authentic control is the build before.** `SL_XBLA_AUTHENTIC_FOG=1`
  is byte-identical to the previous build at both replay cameras.
- **Only the fog end moved.** At eleven ladder steps and eight views the new
  build is byte-identical to the previous build drawing a copy of the
  environment metadata whose row 36 end alone reads 12071.
- **Scope.** ORIGINAL and COMMUNITY HD byte-identical at both cameras; the
  Dam walkway witness and a Facility camera byte-identical under XBLA.
- **No cull moved.** The dish's cut is at the same depth before and after
  (12073.6 culled, 12069.4 drawn); nothing the game decides is read from
  the presented row.
- **Frame time** (the two replay cameras, normal pacing, 449 presented
  frames, three interleaved runs, medians): 20.17 / 20.05 ms before, 20.10 /
  20.15 after - no difference. A cull of the replacement's draws wholly
  beyond the end was considered and not made: 28 of 69 draws (1155 of 40191
  triangles) at the first camera, and skipping them is not exact - the fully
  fogged geometry still covers the unfogged dome, which shows through where
  it stood.
- Nothing derived from any source is committed: the table holds one number.

### D-028 — A DESTROYED OBJECT IS KEPT: the native destroyed-object vertex store is eight times the cartridge's, so the game's low-memory free of an off-screen destroyed object (`vtxstore_tick` -> `sub_GAME_7F056690` -> `objFreePermanently`) is not reached in play; default ON, `SL_DESTROYED_KEEP=0` sizes the store as the cartridge does (native-only)

- **Date:**        2026-10-02
- **Phase:**       1
- **Commit:**      (this change) - Gitea #77: src/game/vtxstore.c (`vtxstore_reset`, inside `#ifndef __sgi`)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all
- **First tick:**  n/a in every recorded trace (none deforms enough to reach the free). In a session that does, the first tick on which the cartridge would free a destroyed object
- **Fields:**      the props digest (the freed object's presence in the prop list), and the drawn object
- **Toggle:**      `SL_DESTROYED_KEEP`, **default 1**; `SL_DESTROYED_KEEP=0` restores the cartridge's store exactly. A developer switch - no settings row. This is a documented exception to rule 3, as D-004 is: the default is the specified behaviour, and it moves no recorded trace

**What the cartridge does**

A destroyed object's crumpled vertices are a copy in the object vertex
store (type 0x0b0b: 1500 vertices in at most 40 blocks on a one-player
stage, 500 / 20 with more players or on stages 0x1d / 0x1e). Every frame
`vtxstore_tick` checks the store, and while less than a quarter of it is
free it frees permanently the first destroyed, deformed object that is not
on screen: model, prop and room registration. The object is gone when the
player looks back. A request that finds no block takes the allocator's
failure path: the object is not crumpled, its matrix is shrunk instead.
On Facility the quarter mark (375 free) is passed only after more than
1125 deformed vertices; the ten bottling tanks take 1020.

**What Sightline does**

On entry to a stage, after the store's stage-pool allocations (unchanged,
so nothing else in the pool moves), the native build multiplies the
store's vertex and block counts by eight and points the store at a native
buffer of that size (12000 vertices, 320 blocks on a one-player stage).
The allocator, the merge, the quarter test and the free are the game's
own code; the free is simply not reached in play. A destroyed object
stays where it was destroyed, crumpled, for the rest of the stage.

**Why**

A destroyed object vanishing when it leaves the view is a memory policy of
the cartridge, not a gameplay element: the spot it stood on is a hole in
the level's floor tiles either way, so collision does not change with it.
It was reported as a defect on the native build (Facility's bottling
tanks), where a separate byte-order defect made it happen on the first
destroyed tank; with that fixed it remains reachable on the cartridge by
heavy destruction.

**Why it is safe**

Nothing changes until the cartridge's store would pass its quarter mark:
the same requests get the same blocks at the same offsets within the
store, and the free count is only read by the quarter test. Measured with
the synthetic pad at the bottling-room pose (three tanks destroyed in a
row, a turn away and back): free 12000 -> 11898 -> 11796 -> 11694 against
the cartridge's 1500 -> 1398 -> 1296 -> 1194, and all 33 frames identical
with `SL_DESTROYED_KEEP=0`. Every recorded trace arm (Dam and Facility)
hashes as before with the default.

### D-027 — THE DYNAMIC DOOR (#71): a door the game reshapes as it moves - `DOORFLAG_CLIP_TO_BBOX` on a sliding or vertical door, the Dam's yard gates, roller door and gas-plant doors - is drawn at the game's own live box in every presentation: under ORIGINAL and COMMUNITY HD the game's own reshaped lists stay closed at the eye (their near-crossing triangles keep their sub-near part at the near depth instead of the cartridge's near cut); under XBLA the remaster's item for the model is drawn cut at that box in place of the model's lists; door state, timing and collision are the game's; default ON in every presentation, `SL_DYNDOOR=0` draws every door as the cartridge does (native-only, render-only)

- **Date:**        2026-09-30
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      bd89c55d (branch sightline/xbla-world-assets) - Gitea #71: src/sl_chr_asset.h (THE DYNAMIC DOOR: `sl_door_takes`, `sl_door_cut_of`, `sl_door_item_fits`, `SL_DOOR_DL_W0`), src/gfx/sl_gfx_charskin.{c,h} (the pure helpers and `sl_door_cut_mesh`, self-test), src/native/sl_chr_asset.c (the door path of the prop seam, `SL_DYNDOOR`), src/gfx/sl_gfx_dl.c (the bracket in `emit_tri`, the item cut in `chr_asset_draw`, the census line), src/game/propobj.c (the seam's end is handed the render data, inside `#ifndef __sgi`), tools/worldasset (the dynamic doors in the stage inventory; the wrapped-lip texture-coordinate rule)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      every level with such a door draws its own lists under the near rule; under XBLA the set's item draws wherever the model is used - the Dam's three door models, and Facility's security doors, which are the Dam's gas-plant door model (measured at a Facility door: ORIGINAL unchanged, XBLA the item)
- **First tick:**  n/a - no hashed state moves. MEASURED: see "Why it is safe"
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_DYNDOOR=0` draws every door as the cartridge draws it, in every presentation. A developer switch - no settings row, no front-end row. docs/world-assets.md "The dynamic door (D-027)" is the architecture.
- **Later (2026-10-01):** THE RIGID DOOR (B-FACDOOR). A sliding or vertical door WITHOUT `DOORFLAG_CLIP_TO_BBOX` whose model has an item now draws that item riding the door's matrix whole, as a rigid prop's item does; without an item nothing changes. Facility's gas-plant door model is used by twelve clipped and three unclipped doors, and under XBLA the three had drawn the cartridge's door beside the item. Same switch (`SL_DYNDOOR=0`), same presentations, pixels only: ORIGINAL and COMMUNITY HD byte-identical at every witness; every Dam door with an item carries the flag, so the Dam does not change.

**What the cartridge does**

A sliding or vertical door with `DOORFLAG_CLIP_TO_BBOX` moves under its
matrix by `openPosition` times its travel while `doorUpdateBbox` shrinks its
MODEL-space box by the same fraction; the box under the moved matrix is its
collision, and `doorBuildClippedVertices` clamps the model's vertices to the
box's moving face. On the Dam's yard gate B the drawn door and the collision
polygon are the same half-space at every tick. The native near contract
(B-100) cuts a triangle wholly in front of the eye at the near plane, as the
cartridge does, so with the camera at the gate (playtest mark m3, the eye 30
units from its face) the part of a gate triangle nearer than the near plane
is removed and the jamb wall and the sky behind the gate show through a
wedge.

**What Sightline does**

The prop seam takes such a door in every presentation and reads its live
box at the draw - the only state the presentation follows.

- Without an item (ORIGINAL, COMMUNITY HD) the game's own reshaped lists
  draw, bracketed by `SL_DOOR_DL_W0`; inside the bracket a triangle the near
  plane cuts takes the eye-crossing population's path in `emit_tri` (kept,
  saturated at the near depth) instead of the near cut. The artwork is the
  texture provider's as before: COMMUNITY HD replaces all three of the gate's
  textures (0x048f, 0x055a, 0x055b).
- With an item (XBLA, `prop/damgatedoor`, placed as a rigid prop is) the
  model's lists are withheld and the item is cut at the same half-space every
  frame: triangles across the face clipped with every attribute
  interpolated, triangles wholly past it laid onto it (the game's clamp,
  generalised), so its far end closes the door where the game's does. It is
  drawn with the rigid props' near-cut fill.

**Why**

The gate has to function in every presentation: a remaster gate that stays
closed while the game's opens (the rigid prop the damgate round refused) is a
visible wall with no collision, and the cartridge's near cut opens a wedge
onto the scenery behind a closed gate the camera stands against. The door's
state, timing, interaction and collision are left entirely to the game; only
the visible door moves, and it moves with the game's own box. This is
intentionally the default in ORIGINAL and COMMUNITY HD too, where project
rule 5 would keep the cartridge's near cut behind a switch: the functional
gate is the requirement, and the cartridge rendering stays one switch away.

**Why it is safe**

Measured, teleport and the #63 synthetic pad only (the gate-B panel pressed
at playtest mark m1, the game's airlock and auto-close doing the rest), cheats
`invincible,invisible,maxammo`, `SL_VI_CATCHUP=0`, seed pinned:

- **Off is the build before.** `SL_DYNDOOR=0` on the new build and bundles is
  byte-identical to the previous build at the four yard-gate marks under all
  three presentations (twelve frames), and at every XBLA witness the default
  changes (the walkway, the reservoir haze, the truck, the ladder, the door
  panel, the window line, the tower marks, the shot-light run).
- **The game's door is untouched.** The per-tick state of gate B - state,
  `openPosition`, speed, box, matrix, collision polygon, Bond's position -
  is byte-identical across two boots, across ORIGINAL / COMMUNITY HD / XBLA
  through opening (999 ticks) and through the game's own auto-close
  (2819 ticks), and with nine live presentation switches at closed,
  half-open and open against the run without them.
- **Drawn edge and collision agree.** The drawn leading edge lands on the
  same pixel column in all three presentations at every captured state,
  opening and closing, and Bond pressed against the gate at x 16840 moves
  through it at the tick the collision edge clears him (edge at 16844.7,
  `openPosition` 0.168) - the tick the drawn edge passes him.
- **Only doors move.** ORIGINAL and COMMUNITY HD are byte-identical to the
  previous build at every other witness (walls, crates, panel, window,
  lights, fog, clouds, panorama, water, decals, ladder, truck); under XBLA
  every changed witness pixel is a dynamic door now drawn from its item, and
  each such frame is byte-identical with `SL_DYNDOOR=0`.
- **Near the camera.** At playtest mark m3 the wedge is closed in all three
  presentations; at the gate face while it opens (the eye 30 units away) and
  through a sweep of seven headings and two pitches at m3, no sky or wall
  shows through the gate.
- **Per-tick simulation trace.** Unchanged under every presentation and
  switch arm, including `SL_DYNDOOR=0` and four arms that open gate B through
  its panel under ORIGINAL, COMMUNITY HD, XBLA and XBLA with the contract off
  (identical to each other): ab9e61bf across 13 arms, the four gate arms
  012fc574 (4574 lines), and the seeded, ALL GUNS, watch and long pairs as
  before (2cf40dd4, 0a85d196, e6bfae38, a2146cbe).
- **Frame time** (normal pacing, three interleaved rounds, medians, previous
  build / this one): XBLA at m3 19.31 / 19.45 ms, XBLA at the gate face while
  it opens 19.77 / 19.76; ORIGINAL 32.42 / 32.65 and 28.99 / 29.24 - within
  run-to-run spread. The item cut processes about 25 item vertices and draws
  about 21 triangles per door drawn (1863 cuts, 45554 vertices, 37381
  triangles over 699 frames at m3).

### D-026 — THE DAM'S PANORAMA SHELL FADES WHERE THE FOG COVERS IT (#74): under PRESENTATION XBLA the fully fogged part of the Dam background's outermost ring - a painted panorama beyond every stage-fog range used in play, drawn opaque at exactly the fog colour - fades toward the sky dome with elevation, above a band of fog-coloured haze that always covers the fully fogged scenery standing against it; fog range, start, colour and distance equation are the remaster's; default ON within XBLA, `SL_SKY_PANORAMA=0` draws the ring as authored (native-only, render-only, the D-021 / D-023 class)

- **Date:**        2026-09-29
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      a98929fa (branch sightline/xbla-world-assets) - Gitea #74: tools/worldasset/prepare.py (`panorama_shell`), tools/worldasset/slwa.py (`MAT_PANORAMA`), src/gfx/sl_gfx_worldasset.{c,h} (`SL_WA_MAT_PANORAMA`, background class only; the pure `sl_wa_panorama_*` rules), src/gfx/sl_gfx_dl.c (`bg_draw`: the haze top, the fade coordinate, the coverage on texture unit 1; `SL_SKY_PANORAMA`), both self-tests
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      dam only: the shell is found from an item's geometry, and the preparation marks it in the Dam's background alone (`DIVERGENCE_ITEMS`, tools/worldasset/prepare.py, since #78) - Runway's background has an outer ring by the same rule and it draws as authored
- **First tick:**  n/a - no hashed state moves. MEASURED: the Dam per-tick state trace is byte-identical to the previous rounds' under ORIGINAL, COMMUNITY HD, XBLA, XBLA with `SL_SKY_PANORAMA=0`, `SL_XBLA_AUTHENTIC_FOG=1` and the bond / coverage / props switches (901 lines, ab9e61bf...), the seeded, ALL GUNS, watch-open and long runs likewise (2cf40dd4, 0a85d196, e6bfae38, a2146cbe)
- **Fields:**      none. Pixels only, and only the ring's
- **Toggle:**      `SL_SKY_PANORAMA=0` draws the ring exactly as authored. A developer switch - no settings row, no front-end row. It matters only under PRESENTATION XBLA with a prepared background whose bundle marks a shell and a skydome drawn beneath it; ORIGINAL and COMMUNITY HD draw neither. docs/world-assets.md "The panorama shell (D-026)" is the architecture.
- **Later:**       extended to Surface's background on 2026-10-05 by **D-030** (`DIVERGENCE_ITEMS['D-026']` names `surface`; the shell is its meshes 36 and 48, found by the same rule). The mechanism is unchanged; on a frame drawn in two depth ranges (#78, never the Dam's) the shell is drawn once, whole, in the first range (docs/world-assets.md "Beyond the game's far plane").
- **Later (2):**   2026-10-05 (#80): the "fully fogged only" test enters smoothly over the last 0.05 of a shell vertex's fog coordinate (`SL_WA_PANO_COVER`) instead of stepping at 1. A step switched a whole face of the shell between drawn and faded in one frame as the eye walked a vertex across 1 - the distant pop of the playtest Surface marks 1 - 4 (run 20261005-120919-lvl36). Fade, haze, range and colour are unchanged; a vertex fully fogged or below 0.95 draws as before (docs/world-assets.md "The panorama shell (D-026)", "Entering the fade").

**What the remaster does**

The Dam background's outermost ring (two draws of one painted panorama image,
an opaque wall and a translucent band above it) stands 52000-55000 world
units from the eye. Its programs fog it like every other background draw, and
at that distance the gameplay row (end 15000, and 30000 under D-025) leaves
exactly the fog colour 133 173 202. It is drawn after the sky dome and over
it, so from the dam the sky is a flat fog-coloured bowl from the ridge up to
the dome's cloud band; hiding the ring shows the dome's own clouds reaching
down to the mountains.

**What Sightline does**

The preparation marks the ring's materials `MAT_PANORAMA`, found from the
geometry alone: the set of meshes whose nearest vertex lies beyond the
farthest vertex of every other mesh and which together close the circle round
the item's centre (on the Dam: two meshes, 12153-12908 asset units out, the
rest of the item within 11829, largest gap 24.0 degrees). The bundle differs
from the one before in those two flag words only.

At draw time, while the remaster's stage fog is on the pass and the dome was
drawn beneath it, each ring vertex takes a haze top: the highest elevation,
seen from the eye, of any other background vertex the fog reaches (weighted
from fog coordinate 0.85 to full), within an azimuth window of the ring's own
column spacing. Above that top the ring's coverage falls to zero along a
smoothstep over 6 degrees of elevation; below it, and wherever the fog does
not cover the ring completely, the coverage is exactly 1 and the pixel is the
one drawn before. Colour, fog and depth writes are the material's own; only
the coverage moves, through texture unit 1 under MODULATE.

**Why**

A presentation choice, not a correctness repair: with the mountains visible
through D-025's range, the flat bowl above them read as an empty sky. The
ring cannot fade everywhere: fully fogged ridges farther out than the fog end
are flat fog-coloured shapes, and with nothing behind them but the dome they
stand as hard silhouettes against the clouds. The haze rule keeps every one of
them inside fog-coloured ring.

The fade width is the smallest of 6 / 12 / 20 degrees that meets the criteria
at the walkway, the reservoir wide sky and 65 views swept from six eyes at
twelve headings: the share of the ring's footprint showing more than half sky
0.83 / 0.72 / 0.52; contrast just above fully fogged silhouettes 11 / 4 / 1
levels at the 95th percentile, and in the frames no ridge line at any width.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed pinned, read 62:

- **Off is the build before.** `SL_SKY_PANORAMA=0` on the new build and
  bundle is byte-identical to the previous build at the walkway, the reservoir
  (pitch -5 and +15), straight up, the worst-case horizon view and every
  witness: tower wall, W4 (three reads), truck (reads 500/600/700), ladder,
  decal marks dm0..dm5.
- **Only the ring moves.** With the ring hidden, the two builds are
  byte-identical at all fifteen of those frames; the pixels D-026 changes lie
  inside the ring's own footprint at every one (193025 of 206674 at the
  walkway, zero outside anywhere).
- **The dome's painted lower edge never shows.** With the dome image's rows
  from 700 down (its painted mountains) turned magenta, 72 views from six eyes
  at pitch 5, the walkway and the wide sky show no changed pixel - with the
  fade on and with the ring hidden. The mountain ring in front of it closes
  the circle.
- **Scope.** ORIGINAL and COMMUNITY HD byte-identical at three cameras;
  Facility under XBLA identical; the Dam intro, reads 2..80 (40 frames under
  the cinema row, whose 75000 end does not cover the ring), identical. The
  fog is untouched: row 33 still 30000, `SL_XBLA_AUTHENTIC_FOG=1` still the
  authored 15000 (with both switches, the previous build's authentic frame).
  The skydome bundle and the zenith clouds (D-023) are unchanged.
- **Frame time** (the wide-sky camera, normal pacing, four interleaved runs):
  18.83 ms off, 18.77 ms on (medians) - no difference.

### D-025 — PRESENTATION FOG RANGE ON THE DAM (#73): under PRESENTATION XBLA the Dam's gameplay stage-fog row (environment 33) is drawn with a fog end of 30000 world units instead of the authored 15000, so the mountain range behind the dam stays in the scene; colour, start and the distance equation are the remaster's; default ON within XBLA, `SL_XBLA_AUTHENTIC_FOG=1` draws the authored range (native-only, render-only, the D-021 / D-023 class)

- **Date:**        2026-09-29
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      cf9d1820 (branch sightline/xbla-world-assets) - Gitea #73: src/platform/sl_presentation.{c,h} (`sl_presentation_fog_end`, the one-row table), src/gfx/sl_gfx_worldasset.{c,h} (`sl_wa_env_presented`; the authored and presented tables; `sl_wa_env_lookup` answers with the presented row; `SL_XBLA_AUTHENTIC_FOG`), both self-tests
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      dam only (the table names one row of the xbla set)
- **First tick:**  n/a - no hashed state moves. MEASURED: the Dam per-tick state trace is byte-identical to the previous rounds' under ORIGINAL, COMMUNITY HD, XBLA, XBLA with `SL_XBLA_AUTHENTIC_FOG=1` and the bond / coverage / props switches (901 lines, ab9e61bf...), the seeded, ALL GUNS, watch-open and long runs likewise (2cf40dd4, 0a85d196, e6bfae38, a2146cbe)
- **Fields:**      none. Pixels only
- **Toggle:**      `SL_XBLA_AUTHENTIC_FOG=1` draws every environment row at its authored range. A developer diagnostic, not a setting - no settings row, no front-end row. The presented end is the default within XBLA; ORIGINAL and COMMUNITY HD select no world-asset set and read no row. docs/world-assets.md "Presentation fog range" is the architecture.

**What the remaster does**

Its Dam gameplay row fogs from -100 to 15000 world units toward 133 173 202,
by the distance through the background's own modelview (the level scale,
no visibility factor) - reproduced exactly (#68). From the dam walkway the
mountain range stands 20000-30000 units out, so it is drawn at exactly the fog
colour, which is also the remaster's horizon sky: the range is absent.

**What Sightline does**

Sightline extends the XBLA Dam gameplay fog range while retaining the
authored fog colour and distance equation. The presentation policy holds one
row (xbla, 33, 30000); the provider keeps the SLEN table as authored and
draws from a presented copy in which only that row's end differs. The
lookup is the single place every consumer reads a row, so the replacement
background and the native props and characters fogged by the same row
(`sl_wa_object_fog_env`) move together. The generated `environment.slen`
still carries 15000; the load line prints `environment row 33 fog end 15000
authored, 30000 presented (D-025)`. The intro cinema row (933), the
multi-player rows, every other stage's row and every visibility or distance
cull are unchanged.

**Why**

A presentation choice, not a correctness repair. The value is the smallest of
22500 / 30000 / 37500 that meets six criteria at three Dam cameras (the
walkway, a view down the dam across two towers, the reservoir shore): the
silhouette plainly visible, snow and rock structure perceptible, contrast
well below no fog, towers still receding, the reservoir hazed, no new pop.
Measured on the walkway frame (fraction of each pixel's own colour left after
fog, ray-cast distances, matching the equation to 0.01): at 22500 five of nine
mountain pixels are still pure fog and the centre is at 2%; at 30000 they are
0.11-0.39 (centre 0.26), local luminance spread 4.5-7.6 levels against 18-56
without fog, a watchtower at 6109 units 0.79, the dam at 8752 units 0.70, the
outer ring at 62422 units still pure fog. Down the dam: near tower 0.92, far
tower (13953) 0.53, range 0.03-0.25.

**Why it is safe**

Measured, teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, seed pinned, read 62:

- **The authentic control is the build before.** `SL_XBLA_AUTHENTIC_FOG=1`
  is byte-identical to the previous build at the three cameras and at every
  witness: tower wall, ladder, truck (reads 500/600/700), reservoir W4 (three
  reads), decal marks dm0..dm5, looking up at the skydome.
- **Only the fog end moved.** At every one of those frames the new default is
  byte-identical to a scratch build of the previous code whose only change is
  row 33's end set to 30000.
- **Scope.** ORIGINAL and COMMUNITY HD byte-identical at the three cameras;
  Facility under XBLA identical; the Dam intro, reads 2..80 (40 frames, all
  under row 933), identical; the skydome frame unchanged.
- **No new boundary.** No cull threshold moved. Along a 16-position teleport
  walk down the dam (WORLD DETAIL ENHANCED) the drawn prop set changes only at
  room crossings, never beyond 12.7k units, and the background has no
  distance cull. Under WORLD DETAIL ORIGINAL the game's near-fog cull (D-004)
  still removes props at the same 6.5k-17k units - the object is less fogged
  when it goes. The reservoir platform's native crates fog exactly as the
  platform beneath them.
- **Frame time** (walkway, normal pacing, six interleaved runs per arm):
  18.86-18.92 ms authentic against 18.88-19.12 ms presented, the first run of
  each arm excluded (20.21 and 25.14, warm-up) - no difference.
- Nothing derived from any source is committed: the table holds one number.

**Revised 2026-09-30 (#73): end 54537.** At 30000 the far half of the range
behind the walkway and the towers down the dam still drew as flat fog. The
end is now placed on the cartridge's own Dam fog curve,
f(d) = 1.0667 - 5001.7 / d in world units (the level band 995..1000 per mille
under near 5, far 15000, visibility 0.2). That curve reaches full fog only at
the far plane, 75000. Three candidates were measured at six Dam cameras:

- 34074, where the linear range meets the curve at the farthest mountain;
- 75000, the curve's full-fog distance;
- 54537, their midpoint.

34074 still washes out the far range. 75000 lifts the fog off the panorama
shell and exposes D-026's transition. 54537 is kept (docs/world-assets.md,
"Revised"). Checks:

- Colour, start, the equation and `SL_XBLA_AUTHENTIC_FOG=1` are unchanged.
- The authentic control is byte-identical to the previous build at every
  camera.
- ORIGINAL and COMMUNITY HD are byte-identical.
- The per-tick traces are unchanged.

### D-024 — ONE SLEEVE PER COSTUME ON THE WATCH HAND (#70): under PRESENTATION XBLA the arm the watch is worn on is the remaster's item with exactly ONE of its five alternative sleeves - the one a derivation from the game's own cuff variants pairs with the player's costume - where the remaster draws all five at once, overlapping; a costume no sleeve is paired with keeps the game's whole watch hand; a design decision, default ON within XBLA, `SL_WATCH_SLEEVES=all` draws the remaster's composite, `SL_WATCH_SLEEVES=native` the game's watch hand (native-only, render-only, the D-021 / D-023 class)

- **Date:**        2026-09-28
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this branch, sightline/xbla-world-assets) - Gitea #70: tools/worldasset/watchhand.py (NEW - the game model reader, the relation, the pairing), tools/worldasset/prepare.py (`convert_watchhand`), tools/worldasset/slwa.py (`PART_MATRIX`, `MESH_ALT`), tools/worldasset/viewmodels.py and tools/windows/prepare-xbla-assets.ps1 (the ROM joins the watch hand's preparation and the stamp), src/gfx/sl_gfx_worldasset.{h,c} (the two values, class GUN only), src/gfx/sl_gfx_charskin.c (the arm relation, `sl_vm_draw_visible`, `sl_vm_alt_serves`), src/gfx/sl_gfx_dl.c (the rule, the arm's depth released), src/native/sl_vm_asset.c (`sl_vm_watch_begin`), src/sl_chr_asset.h (THE WATCH HAND), src/game/bondview2.c (the seam around the watch's `subdraw`, `#ifndef __sgi`), the runtime and tooling self-tests (synthetic fixtures)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      every stage whose costume's cuff a sleeve is paired with, under PRESENTATION XBLA with the watch hand prepared: boiler suit, jungle fatigues, snow suit. Measured on the Dam (boiler suit) and, with a scratch costume override, all six cuffs
- **First tick:**  n/a - no hashed state moves (the per-tick trace, below)
- **Fields:**      none. Pixels only, and only the watch hand's
- **Toggle:**      `SL_WATCH_SLEEVES`: `one` (**default within XBLA, a design decision**), `all` (the remaster's composite - every sleeve, as the remaster draws them), `native` (the game's watch hand under every presentation). A developer switch this round - no settings row, no front-end row. ORIGINAL and COMMUNITY HD never draw the item. docs/world-assets.md "The watch hand" is the architecture.

**What the remaster does**

Its watch-hand item (`char/suitlfhand`) carries one sleeve per costume - a
black tuxedo sleeve with its buttons and shirt cuff, an olive rolled sleeve,
a grey parka, a charcoal ribbed-knit sleeve, a dark navy sleeve with a shirt
cuff - and gates none of them: no condition table, no gated block, no
selector at runtime. Measured in the remaster itself on the Dam with the
watch raised (2026-09-28, one logged frame): all five sleeve draw pairs are
issued, every one opaque with depth test and write, and the picture is the
tuxedo sleeve pierced by patches of the parka and the ribbed knit. The game
(`bondviewSelectCuff`) shows exactly one cuff, the costume's.

**What Sightline does**

The preparation pairs the sleeves with the game's own cuff variants
(`Switches[4..9]` of `Csuit_lf_handZ`, read from the user's ROM) through the
source's original generation of the same item, by two class features - where
the sleeve begins along the forearm, and whether its wrist end is a white
shirt cuff or a dark band - and colour, and marks each sleeve draw with the
cuff it serves. The runtime draws the arm, hand and watch with only the
sleeve serving the cuff the game made visible. On the Dam's source: boiler
suit -> the charcoal ribbed sleeve (the only full-length sleeve with a dark
wrist end; the navy sleeve's wrist end is a white shirt cuff; the remaster's
own boiler-suit body agrees independently, forearm colour distance 4 against
40), jungle -> the olive rolled sleeve (the only one begun up the forearm),
snow -> the parka (light, full, nearest by colour 53 against 372). The
Brosnan-tuxedo and blue-suit cuffs cannot be told apart through the original
generation (identical in size, count and place) and Connery's white jacket
matches no sleeve: those costumes keep the game's whole watch hand, as does
any costume under a bundle that pairs nothing.

**Why**

A design decision. Of one sleeve per costume, the native watch hand, or
the remaster's five-sleeve composite, one sleeve per costume was chosen,
with the mapping derived by matching each remaster sleeve to the
game's own cuff variants - not guessed - and documented as a deliberate fix
of the remaster's bug, with a toggle to the authentic look.

**Why it is safe**

Measured at the Dam gate-yard witness with the watch opened by the developer
pad (a hand-written START step, never a recording), teleport only, cheats
`invincible,invisible,maxammo`, `SL_VI_CATCHUP=0`, reads 300..400:

- **Off is the build before.** `SL_WATCH_SLEEVES=native` is byte-identical to
  the build before this change on all 11 frames; ORIGINAL and COMMUNITY HD
  are byte-identical to it on all 11.
- **Only the watch hand moves, and none of the game's arm survives.** With a
  scratch bundle that draws nothing, the frame's changed pixels lie wholly
  inside the game's watch-hand silhouette united with the item's - 0 outside
  at every read, through the raise, the open watch and three watch pages
  (reads 380..600) - and no pixel of the game's watch hand survives where
  the item draws nothing.
- **The watch is unchanged as a watch.** The pages and the pad navigation
  through them are the game's and appear at the same frames as before; the
  controls page's pad (a model drawn with the Z buffer after the arm) is
  drawn, because the arm's depth is released once it is drawn. The mouse
  pointer draws after the watch as before; it was not exercised in these
  captures (nothing touches the window).
- **Live.** ORIGINAL -> COMMUNITY HD -> XBLA -> ORIGINAL -> COMMUNITY HD ->
  XBLA -> ORIGINAL, the watch raised in each: the game's hand, the game's,
  the remaster's with one sleeve, the game's (nothing stale); the item is
  loaded once.
- **The simulation.** The Dam per-tick trace (`SL_TRACE_OUT`) is unchanged:
  ORIGINAL, COMMUNITY HD, XBLA and XBLA with `SL_BOND_ASSETS=0`, 400 frames,
  one hash (ab9e61bf..., the previous rounds'); and with the watch raised and
  paged by the developer pad (teleported, 1200 frames) ORIGINAL, XBLA,
  `SL_WATCH_SLEEVES=all` and `=native` are one hash (e6bfae38...), the XBLA
  arms drawing the item.
- **Everything else unchanged.** The first-person witness (ORIGINAL,
  COMMUNITY HD, XBLA, `SL_BOND_ASSETS=0`), a guard witness, a prop mark and a
  decal mark are byte-identical to the build before; every other prepared
  bundle is byte-identical under the changed tools. Frame time with the watch
  open (three interleaved rounds): 19.1-19.6 ms before, 19.0-20.4 ms after,
  18.8-19.5 ms with `=native` - inside run-to-run spread; the item is loaded
  once.
- Nothing derived from any source is committed - the derivation is code, its
  output lives only in the gitignored `worldassets\`, and its tests are
  synthetic.

### D-023 — GENERATED ZENITH CLOUDS (#68): under PRESENTATION XBLA the Dam skydome (Runway's since #78, Surface's since #80) is drawn with one more draw than the remaster's - a cap of cloud the preparation synthesised from the dome image's own cloud band - so the sky overhead is cloudy instead of the remaster's clear cap inside a ring of cloud; a design decision, default ON within XBLA, `SL_SKY_ZENITH=0` draws the source dome exactly (native-only, render-only, the D-004 / D-021 class)

- **Date:**        2026-09-26
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this branch, sightline/xbla-world-assets) - Gitea #68: tools/worldasset/zenith.py (NEW - the generator), tools/worldasset/prepare.py (`add_zenith_clouds`: appends the carrier as one flagged draw after the source's), tools/worldasset/slwa.py (`MAT_GENERATED`), tools/windows/prepare-xbla-assets.ps1 (the generator joins the tool hash), src/gfx/sl_gfx_worldasset.{h,c} (`SL_WA_MAT_GENERATED`, skydome class only; `sl_wa_material_drawn`), src/gfx/sl_gfx_dl.c (`sky_draw` leaves generated draws out when the switch is off), tools/worldasset/selftest.py and the runtime self-test (synthetic fixtures)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      dam, runway and surface: the generator keys on the dome image's structure (a flat cap at the pole over a cloud band) and the mesh's (rings about one axis), and the preparation runs it for the domes `DIVERGENCE_ITEMS` names (tools/worldasset/prepare.py, since #78) - the Dam's, Runway's since 2026-10-04, and Surface's since 2026-10-05 (the same clear cap inside a ring of cloud, measured looking up from the dome building); every other stage's dome is prepared as the source authors it
- **First tick:**  n/a - no hashed state moves. MEASURED: the Dam per-tick state trace (`SL_TRACE_OUT`, 400 frames) is **byte-identical** under ORIGINAL, COMMUNITY HD, XBLA, XBLA with `SL_SKY_WITH_BG=1`, XBLA with `SL_BG_REMASTER_FOG=0` and XBLA with `SL_SKY_ZENITH=0` - all six 901 lines, one SHA-256 (ab9e61bf...), the previous rounds' hash
- **Fields:**      none. Pixels only, and only the dome's
- **Toggle:**      `SL_SKY_ZENITH`, **default on (a design decision)**; `SL_SKY_ZENITH=0` leaves every generated draw out and draws the source dome exactly. A developer switch this round - no settings row, no front-end row. It matters only under PRESENTATION XBLA with a prepared skydome; ORIGINAL and COMMUNITY HD never draw one. docs/world-assets.md "Generated zenith clouds" is the architecture.
- **Later:**       on Surface the field and the dome are drawn through the game's own cloud shading since 2026-10-05 (**D-030**); the field's geometry, image and switch are unchanged.

**What changed**

The remaster's Dam dome is an equirectangular sky about a pole: its image is a
flat blue gradient from the pole down to row 155, then a cloud band that
settles by row 271, then mountains. Wrapped, that is a clear cap overhead with
a ring of cloud round it - measured source-authentic (the image's own
structure, and the console's picture looking up, which is clear deep blue).

The playtest verdict on the faithful dome: *"I don't think it's the dome
brightness alone. My main issue is the cloud coverage. The clouds seem to be
concentrated in a ring shape around the dome."* Of the remaster's unused cap
draw, the authentic dome, or clouds generated overhead from the dome's own
band, the last was chosen, as a fix to make even where the source itself
draws the sky this way.

So the preparation (`prepare-xbla-assets.ps1 -DamSkydome`) now appends ONE
more draw to the skydome bundle: a cap carrier laid on the dome's own surface
with planar texture coordinates (no pole to pinch), whose image is the band
reprojected top-down with the cap and the band's fade-in replaced by a fill
synthesised from the band (spot noise with the band's own texton and
histograms, its low frequencies continued from the band by harmonic
extension), fading to transparent across a rim where it IS the band. The
source's draws and images are byte-identical inside the bundle; the carrier is
flagged `GENERATED`, and the runtime draws it only while `SL_SKY_ZENITH` is on.

**Why**

It is a deliberate design decision, made knowing the remaster is clear
overhead; project rule 5 puts such a change behind a toggle, and the toggle
here restores the remaster exactly. It defaults ON only because of that
decision; changing this default requires an explicit design decision.

**Why it is safe**

Measured at the proxy of the playtest mark and five other poses (straight up,
the playtest heading, two reconstructions of the reported upward screenshots
and the pole), teleport only, cheats `invincible,invisible,maxammo`,
`SL_VI_CATCHUP=0`, read 62:

- **Off is the source.** `SL_SKY_ZENITH=0` against the build before this
  change: **6 of 6 frames byte-identical**. The new binary with the old bundle:
  6 of 6 identical.
- **Only the dome moves.** Changed pixels outside the dome's own pixels (the
  frame changes when the dome is removed, or when it is painted white) are
  **zero** at all six poses.
- **No pinch, repetition or new ring** - measured offline with the renderer's
  own LOD rule and box mips: the pole is unremarkable among 237 random points
  of the same view (a naive upward extension, the control, is 31x the median
  there); best-match self-similarity of the fill is below the band's own; the
  luminance step across the hand-over stays inside the band's own range. The
  rejected in-texture variant is recorded in docs/world-assets.md with the
  flat disc it left at the pole.
- **Controls.** ORIGINAL and COMMUNITY HD byte-identical to the build before;
  no bundle, and a truncated one, draw exactly as before (`not provided`,
  `INVALID`); Facility under XBLA identical and requests nothing; the
  simulation trace above.
- **Frame time** (the frame-time harness, one session, four interleaved runs
  each): 30.80 ms before, 31.64 ms on, 30.84 ms off (medians) - any cost is
  inside run-to-run spread.
- Nothing derived from any source is committed: the generator is code, its
  output lives only in the gitignored `worldassets\`, and its tests are
  synthetic.

**Revised 2026-09-30 (#76): the cloud field - broader, and on the game's own cloud phase**

The same divergence, refined; one number, because it is one draw with one
switch and the rule of this file is that entries grow rather than split.

- **What changed.** The generated draw is no longer a cap about the dome's
  texture pole with its image and coordinates baked into the bundle - which
  ended at 28.4 degrees from the pole (17.7-40.5 degrees of elevation from the
  eye), left the authored band showing below it, and never moved. It is now a
  cloud field: a cap of directions from 8 degrees of elevation to the zenith
  (transparent at 8, whole from 18), whose image is one periodic tile grown
  from the band, and whose coordinates the runtime casts every frame onto the
  game's own cloud plane exactly as sky.c does - s = 0.1 x, t = 0.1 z +
  `g_SkyCloudOffset`, at the environment's cloud height, from the game's eye.
  The clouds therefore stand where the game's clouds stand and move exactly as
  they move, one `g_ClockTimer` step of `skyTick` at a time; there is no clock
  of Sightline's. The tile is laid twice per repeat of the game's 64 x 64 cloud
  image, and the game's wrap of the phase at 4096 is a whole number of tiles.
- **Why.** In play the sky still read as mostly empty and entirely still
  beside ORIGINAL's drifting layer. The remaster's dome is still; moving it is
  a presentation decision, made with the game's own motion contract so that
  the phase is deterministic, frozen whenever the game's is, and shared by
  every presentation.
- **Toggle.** Unchanged: `SL_SKY_ZENITH=0` draws the source dome exactly.
- **Measured.** Motion: over 300 ticks at a fixed walkway camera the new XBLA
  sky moves 35-39 px per 50 ticks against ORIGINAL's 35-40, the terrain within
  the camera's own 1 px sway; the same tick twice is the same frame; across the
  4096 wrap both move 4 px per 5 ticks with no jump; a presentation cycle
  (XBLA -> ORIGINAL -> COMMUNITY HD -> XBLA) returns to the never-switched
  frames apart from the presentation caption. The solo watch does not stop the
  game's phase (lv.c zeroes `g_ClockTimer` only for locked controls and the
  multiplayer pause) and the field follows it. Coverage: of three profiles
  (transparent at 18 / 8 / 2 degrees, whole 8 / 10 / 6 above), the smallest
  that leaves no still authored band beside moving cloud was taken - over 72
  views from six eyes at twelve headings the 18-degree profile exposes the
  band in 58 while the 8-degree one's fade shows in 4 views, at most 49
  pixels. Identity: `SL_SKY_ZENITH=0` on the new build and bundle is
  byte-identical to the previous build's at 24 frames (six cameras, tower
  wall, W4, truck, ladder, six decal marks, four yard-gate marks); with the sky
  pass left out both builds agree at every one; ORIGINAL, COMMUNITY HD,
  Facility and the Dam intro's source-dome frames identical; the fog
  (`SL_XBLA_AUTHENTIC_FOG=1`) and panorama (`SL_SKY_PANORAMA=0`) switches draw
  the previous build's frames with the field off. Frame time at the reservoir
  wide sky: 18.91 ms before, 18.82 ms after (medians of four). Per-tick trace
  unchanged under every arm including `SL_SKY_ZENITH=0`.
- **Not claimed.** The tile is a statistical texture of the band, not painted
  cumulus; its scale is a density choice (two tiles a repeat), not measured
  against the console, whose sky does not move.

**Extended 2026-10-04 (#78): Runway's dome**

- **What changed.** Runway's dome is prepared with the same generated cloud
  field (`DIVERGENCE_ITEMS['D-023']` names `runway` beside `dam`). Nothing at
  runtime changes: the cloud field rides the game's own cloud plane and phase
  on Runway exactly as on the Dam, under the same switch.
- **Why.** Looking up on Runway under XBLA showed the clear deep-blue cap
  inside a ring of cloud that the Dam showed before this divergence, and the
  request was for the same complete coverage the Dam has. Measured first that
  the gap is the source's own cap and not the dome's extent, scale or
  placement: the dome covers the whole view at the zenith mark (no game sky
  shows through), and the generator's analysis of Runway's dome image reads a
  flat cap to 17.8 degrees from the pole over a band settled at row 271 -
  the same structure, to the row, as the Dam's image.
- **Not extended.** D-026, the panorama shell's fade, stays the Dam's: on
  Runway the shell is not fully fogged where it stands against the sky (its
  haze top reads 0.0 degrees), and drawing it with the fade changed no pixel
  at three outdoor marks.
- **Measured.** At five of the second Runway replay's marks (teleport, seed
  pinned, read 80): ORIGINAL and COMMUNITY HD byte-identical; under XBLA only
  the sky moves - the zenith mark's clear cap is cloud (657317 pixels), and
  the cloud reaches the horizon panorama at the other four. The Dam's dome
  and background, Facility's background, Runway's background and the
  stage-fog table regenerate byte-identical.

### D-022 — PRESENTATION (#68): ONE player-facing visual selection - ORIGINAL / COMMUNITY HD / XBLA - resolved by a policy layer into a set for EACH visual provider, so one choice moves the texture provider and the world-asset provider together; the incoherent combinations reachable before it (ORIGINAL or COMMUNITY HD artwork with an XBLA skydome) can no longer be selected; default ORIGINAL = the accepted rendering byte for byte (native-only, render-only, the D-009 / D-019 / D-020 / D-021 class)

- **Date:**        2026-09-23
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this branch, sightline/xbla-world-assets) - Gitea #68: src/platform/sl_presentation.{c,h} (NEW - the policy: the canonical value and the per-provider resolvers), src/platform/sl_settings.{c,h} (the `presentation` row replacing #47's `textures` row, and the one legacy migration), src/gfx/sl_gfx_texprov.c (asks the policy instead of reading a setting), src/gfx/sl_gfx_worldasset.{c,h} (asks the policy; the selection is re-asked rather than latched, so it switches live; `SL_WORLD_ASSETS` demoted to a diagnostic override that announces every mixed state), src/native/sl_settings_apply.c (the cycle action's HUD line), src/platform/sl_action.h + sl_bindings.c + sl_input.c (the action retargeted; its persisted binding key unchanged), src/native/sl_front_options.c + src/game/options.{c,h} (both UI surfaces relabelled onto the same value), tools/windows/play.ps1 (-Presentation; -WorldAssets demoted to a pin), tools/windows/presentationtest.ps1 (NEW)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the policy is one function; what it resolves to depends on what the player's installed sets carry, and today only the Dam has a world asset at all)
- **First tick:**  n/a - no hashed state moves. MEASURED: the Dam per-tick state trace (`SL_TRACE_OUT`, 600 frames, windowed, teleport-sampled) is **byte-identical** under ORIGINAL, COMMUNITY HD and XBLA, and byte-identical again under a run that cycles the presentation six times mid-level - all four 1355 lines, 121402 bytes, one SHA-256
- **Fields:**      none. Pixels only. The two providers' own effects are D-020's and D-021's and are unchanged; what this entry adds is WHICH of them is in force
- **Toggle:**      `presentation` in the native settings store (`%LOCALAPPDATA%\sightline\config.ini`), **0 = ORIGINAL (the default): the accepted Sightline rendering exactly, no pack read and no world asset opened**; 1 = COMMUNITY HD; 2 = XBLA. THREE ways in, one stored value: the binding-registry action **PRESENTATION** (keyboard F5, pad BACK - the same binding #47 put there, retargeted, with no second hotkey), OPTIONS -> SETTINGS -> DISPLAY `PRESENTATION`, and the watch's SIGHTLINE -> GRAPHICS `presentation`. `SL_WORLD_ASSETS` survives as an ADVANCED / DIAGNOSTIC override pinning the world-asset provider alone, with defined precedence (override -> presentation-derived) and a printed diagnostic on every disagreement. docs/world-assets.md is the architecture.

**What changed**

Nothing about either provider's mechanism. The texture provider's one policy
owner (`sl_texprov_active`) and the world-asset provider's
(`sl_wa_active`) stopped reading their own state and started asking a policy
layer, `sl_presentation.h`, which resolves the player's one selection into a
set per provider:

```
    ORIGINAL      textures ORIGINAL       world assets none
    COMMUNITY HD  textures COMMUNITY HD   world assets none
    XBLA          textures XBLA           world assets xbla
```

The world-asset provider's selection had been latched once per process from an
environment variable; it is now re-asked at every ask, the same shape the
texture provider always had, which is what lets one press move both. Going
dark does not unload: the asset stays resident with its GL names, so returning
duplicates nothing.

`presentation` replaced #47's `textures` row in the settings table. A config
written before this date is migrated at load - the value copied across
unchanged, the canonical key written once, the legacy key never written again,
and the new key winning if a hand-edited file carries both, whatever the line
order.

**Why it is safe**

Nothing in `src/game` gained a read of it: options.c's watch row calls the
same kind of native accessor every other Sightline row calls, and the policy
itself is `src/platform` host code over plain ints. The store is INACTIVE
during trace replay and headless health, so the policy answers ORIGINAL there
and no provider is ever selected.

Measured, all at a fixed Dam pose, teleport-sampled, no input replay, one
bounded run per case, cheats `invincible,invisible,maxammo`:

- **ORIGINAL identity** - PRESENTATION ORIGINAL against a build of
  `origin/master` (db232071) at the same pose and frames: **7 of 7 frames
  byte-identical** (SHA-256 of the raw framebuffer), with `TEXTURES=ORIGINAL
  gen=1 switches=0 files loaded=0` and `worldasset: inactive`.
- **Simulation identity** - the four traces above, byte-identical.
- **Live switching, two full cycles** in one run (six presses of the bound pad
  button, sticks at rest throughout, so nothing navigates): the texture
  provider reads `switches=6` ending on ORIGINAL, the world-asset provider
  lights and goes dark with the presentation, and the skydome's drawn-frame
  count advances only inside the XBLA windows.
- **Return to ORIGINAL** - against a run that never switched, same frames,
  both runs verified deterministic (7 of 7 frames byte-identical when repeated):
  before any press **0.0000%**; after one full cycle and after two, the world
  outside the HUD differs on **0.216%** of the picture, confined to fourteen
  rows of the game's own animated sky, and **does not grow with the second
  cycle** - against an animation floor of **15.0%** between two frames of the
  same never-switched run. The remaining difference is the feature's own HUD
  notice line.
- **Lifetime, four repeated cycles** (twelve presses): `LOADED` printed
  **once**, every census `lookups=1 loaded=1`, the texture provider
  `reloaded=0 evicted=0` across eleven switches.
- **WORLD DETAIL independence** - all six (ORIGINAL / ENHANCED) x (three
  presentations) combinations: each pair resolves the same texture set and the
  same world-asset state, and the picture still differs between the two detail
  profiles, so the two controls act independently.
- **Non-Dam control** - facility under PRESENTATION XBLA: `lookups=0
  loaded=0`, no file opened.
- **Fallback** - a missing root reads `skydome/dam not provided - dam
  unchanged`, `missing=1` counted once over 301 lookups; a malformed file
  reads `INVALID skydome/dam - not an SLWA world asset`, `invalid=1` once.
- **The mixed state is never silent** - pinning the override against the
  presentation in either direction prints one line from the provider and one
  from the launcher; the default launch clears a stale `SL_WORLD_ASSETS` for
  the child and says it did.

### D-021 — WORLD ASSETS XBLA (#68): under an opt-in developer switch the native renderer draws, for a stage the selected set carries an asset for, one ADDITIVE external item - meshes and its own images - behind the game's own world; a stage the set lacks, an absent file and a malformed one all draw the accepted picture exactly (native-only, render-only, the D-009 / D-019 / D-020 class)

- **Date:**        2026-09-22
- **Phase:**       1 (v0.3.0 Visual Fidelity, the world-asset sprint)
- **Commit:**      (this branch, sightline/xbla-world-assets) - Gitea #68: src/gfx/sl_gfx_worldasset.{c,h} (the SLWA parser, the policy, the per-(class, path) cache), src/gfx/sl_gfx_dl.c (the draw seam at the frame's first world triangle, and the census line), src/native/sl_world_assets.c (the stage's item path and the camera, read-only), tools/worldasset/ (the preparation tooling), tools/windows/prepare-xbla-assets.ps1, tools/windows/play.ps1 (-WorldAssets), tools/windows/worldassettest.ps1
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      the Dam only in this slice; every other stage resolves no item path and therefore performs no lookup at all
- **First tick:**  n/a - no hashed state moves. MEASURED: the Dam per-tick state trace (`SL_TRACE_OUT`, 300 frames, windowed, with the asset actually drawing) is byte-identical off and on - same length, same SHA-256
- **Fields:**      none. Pixels only, and only pixels the game's own world does not already cover: the asset is drawn before world geometry with depth test and depth write off, so it can never occlude near geometry. MEASURED at the Dam reservoir: 41.43% of the frame changes, 86.95% of it in the top third, and **0.00%** in the bottom third
- **Toggle:**      `SL_WORLD_ASSETS` (the set) plus `SL_WORLDASSET_ROOT` (the root), reached through `.\tools\windows\play.ps1 -WorldAssets XBLA`. **Absent = off, which is the default: no root is resolved, no directory is opened and the provider is never asked.** A DEVELOPER control this round - no settings row, no front-end row, no watch row, no binding - and **independent of the TEXTURES setting**: a texture set never moves world geometry and this never touches a texture. docs/world-assets.md is the architecture.
- **Later:**       the MECHANISM above is unchanged, but the CONTROL described in this entry was superseded on 2026-09-23 by **D-022**: the set is selected by the player's PRESENTATION, and `SL_WORLD_ASSETS` became a diagnostic override. The independence claim in this entry is about the two providers' mechanisms and still holds; it is no longer true that the two are SELECTED separately, and that was the point of D-022. This entry is left as written rather than rewritten, because it records what was measured on the day.

**What changed**

The frame's first triangle under the world projection (sl_gfx_dl.c) - after
every clear, before any world geometry - draws the asset the provider resolved
for this stage's (asset class, item path), through the same world-to-clip
matrix the game's own world geometry goes through. Nothing else in the frame
moves.

**Why it is safe**

Nothing in `src/game` is touched, so the cartridge build is unchanged by
construction rather than by inspection: the two questions the seam asks the
game - which item is this stage, and where is the camera - are answered in
`src/native`, which is compiled with the game include path for that purpose,
and both are pure reads. The draw is reached only from `sl_gfx_frame_dl`,
which returns before it when there is no window, so a trace replay never
executes a line of it. Off, the whole feature is one int test per frame.

Measured controls, all at a fixed Dam pose with a deterministic seed: the
feature build with the switch off is **0.00%** different from a build of
`origin/master` (and from itself, the harness control); a missing asset and a
malformed asset are each **0.00%** different from that same picture, counted
once and reported once; and a non-Dam stage with the provider on is **0.00%**
different with no world-asset line in the log at all.

### D-020 — TEXTURES COMMUNITY HD / XBLA (#47): under an opt-in persisted setting the native renderer uploads, for a game texture the selected set carries, that set's image at its own physical size in place of the ROM decode, sampled over the same logical tile; a texture the set lacks stays the decode (never the other set's); default ORIGINAL = the accepted rendering byte for byte (native-only, render-only, the D-009 / D-019 class)

- **Date:**        2026-09-22
- **Phase:**       1 (v0.3.0 Visual Fidelity, the texture-providers sprint)
- **Commit:**      (this branch, sightline/texture-providers) - Gitea #47: src/platform/sl_settings.{c,h} (the row) + src/native/sl_settings_apply.c (the accessor pair), src/gfx/sl_gfx_texprov.{c,h} (the provider: identity side table, SLTX loader, resident cache, the one policy owner), src/game/image.c (texLoad / texInitPool native arms feeding the side table), src/gfx/sl_gfx_dl.c tex_acquire (the upload seam and the cache key), src/native/sl_front_options.c (the DISPLAY tab row), src/game/options.{c,h} (the watch's GRAPHICS child row)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seam is one function; which textures change depends on what the player's installed set carries)
- **First tick:**  n/a - no hashed state moves (measured: the Dam pad replay's per-tick state trace is byte-identical under all three sets, below)
- **Fields:**      none. Pixels only: under COMMUNITY HD or XBLA a texture the set carries is drawn from the set's image (larger, the same wrap / clamp / mirror, the same tile size the coordinates divide by, the same combiner and alpha path); every other texture, and every texture under ORIGINAL, is the decode
- **Toggle:**      `textures` in the native settings store (`%LOCALAPPDATA%\sightline\config.ini`), **0 = ORIGINAL (default): the accepted Sightline rendering exactly as it stood before this entry, no pack read**; 1 = COMMUNITY HD; 2 = XBLA. Read by the renderer through `sl_texprov_active()` (src/gfx/sl_gfx_texprov.c) at each texture resolve; an inactive store (trace replay, headless) answers ORIGINAL. THREE ways in, one row: OPTIONS -> SETTINGS -> DISPLAY `TEXTURES  ORIGINAL / COMMUNITY HD / XBLA` (an advancing cell); the watch's SIGHTLINE -> GRAPHICS `textures  original / community hd / xbla` (LEFT / RIGHT, wrapping); and, from 2026-09-22, the binding-registry action **TEXTURE SET** - keyboard **F5**, pad **BACK** (View / Create / "Select") in every preset, re-bindable in both editors - which steps the same row while the player is on foot with no menu up, names the set it landed on through the game's own bottom HUD message line, and cannot fire from inside a menu or from a stale edge as one closes. The pad's BACK stopped marking to make room for it (a design decision); the keyboard's F9 / F8 marks are unchanged. docs/texture-packs.md is the architecture.
- **Later:**       the SUBSTITUTION above is unchanged, but the CONTROL described in this entry was superseded on 2026-09-23 by **D-022**: the `textures` row became `presentation` (same three ids, migrated at load), the action was retargeted from TEXTURE SET to PRESENTATION on the same binding, and both UI rows were relabelled onto the same value - so the set this provider draws is now chosen by the player's one visual selection rather than by a setting of its own. This entry is left as written rather than rewritten, because it records what was measured on the day.
- **Later (2026-09-30):** the XBLA table grows to 129 rows. 0x0105 and 0x02fa, the Dam chain-link doors' chain field and frame, come from `prop/damchaindoor`, resampled into each native tile (docs/texture-packs.md, "Native-domain entries"). Same substitution, same tile, same switch, so no new entry. ORIGINAL and COMMUNITY HD are byte-identical at every witness (B-DAMCHAIN).

**What changed**

`tex_acquire` (sl_gfx_dl.c), the one place a resolved tile becomes a GL
texture, asks the provider for a replacement of the tile's decoded pointer
at the tile's logical w x h. Under ORIGINAL the answer is NULL at the cost
of one store read and the upload is the pre-#47 statement. Under a set,
the provider maps the pointer back to the N64 texture number through a
side table texLoad's native arm fills (image.c: the bytes at tex->data ARE
texture tex->texturenum, tex->width x tex->height at the base level;
texInitPool's native arm drops a pool's registrations), then asks the
set's folder for `<hex4>.sltx` - once per (set, id), the outcome
remembered - and, when the file is present, valid and the tile is the
whole image, returns its RGBA8 at the file's physical size. tex_acquire
uploads THAT with glTexImage2D in place of the decode (or the B-116
enhancement, which is skipped for a replacement) and keys the cache entry
on the provider generation, which every switch bumps. The texture
coordinates the draw path emits divide by the tile's logical size (tex_apply:
s / 32 / tile_w; draw_texrect the same), so the replacement is sampled
over exactly the region and repeat count the decode was.

**Why**

The #47 contract: three texture sets through one abstraction -
ORIGINAL the game's own, COMMUNITY HD an externally maintained pack the
player installs, XBLA a user-supplied set - with per-texture fallback to
ORIGINAL, no set assumed complete, the renderer consuming (active set,
canonical id) only, no per-frame filename or hash matching, and nothing
from any set in the repository. The N64 texture number is that canonical
id; it reaches the seam through the game's own loader, which is why no
runtime pixel hashing exists.

**Why it is safe**

The boundary: two `#ifndef __sgi` islands in image.c (proof: image.c,
options.c and options.h preprocess token-identical to the base under
__sgi, a bare-token control differs, zero #47 tokens in any __sgi
expansion); the side table is written by the loader and read by the
renderer and no value flows back. Measured: the dam-pad input stream
replayed WINDOWED with the settings store active and SL_TRACE_OUT, 3000
pumped frames, under ORIGINAL, COMMUNITY HD (49 files loaded, 2074430
replaced resolves) and XBLA (9 files) - all three per-tick state traces
byte-identical (613402 bytes, sha256 A1FD1FB8...) and identical to the
#43 baseline trace. ORIGINAL identity: the pre-feature exe (master
1fd6335a, a scratch worktree build) and this exe at four teleport poses
(Facility catwalk theta 270 and theta 90, Archives room 14, the Depot
spawn), key absent and key 0 - byte-identical frames (sha256 1C9ABB4E...,
23818A31..., 87E29178..., 4F0C02B6...). Positive controls: the Archives
poster (id 0112, 32x48 CI8 -> 352x528, an asymmetric lettered witness)
under COMMUNITY HD keeps the ORIGINAL's orientation, mirroring, placement
and size at a crisp 11x; the Facility door sign (id 05d3, 38x38 ->
256x256) is replaced under XBLA only and stays the decode under COMMUNITY
HD (no cross-set fallback), and the poster the reverse; the pistol ammo
icon (08b7, 5x12 -> 80x180, the 2D texrect path) keeps its orientation
and placement. A live switch through the watch (ORIGINAL -> COMMUNITY HD
-> XBLA -> ORIGINAL, then the other way, three full cycles) shows each
expected image with no stale carry-over, no re-read of resident files
(reloaded=0), a bounded resident cache (21 images, 12 MB) and the same GL
cache ring. World Detail x Textures 2x2 at the playtest Dam mark 1: the
prop population and triangle count move only with WORLD DETAIL (the same
1604 / 1611-pixel lift under either set), pixels only with TEXTURES.
Malformed files (wrong magic, truncated, garbage, wrong embedded id) are
each reported once and drawn ORIGINAL; a missing set folder is one line.
Performance at the highest-replacement scene (Dam mark 1, 92 replaced
uploads, 41 resident): the DL walk 7.96 / 7.96 s ORIGINAL vs 8.07 / 8.05 s
COMMUNITY HD vs 8.01 / 7.99 s XBLA over 179 rendered frames (+1.1% at
most), the run's wall within 0.08 s. test.ps1 facility 300 PASS;
settingstest 180/180; texprovtest 56/56; displaytest and inputtest
unchanged; check-layering 282 = baseline. `make trace-verify` NOT run (no
MIPS toolchain on this host).

**Amendment, 2026-09-22 (acceptance playtest FAILED; the runtime contract is
unchanged).** Everything above still holds and nothing in `src/` moved -
the toggle, the seam, the SLTX format, the root ladder (`SL_TEXPACK_ROOT`,
else the player-data directory) and the per-texture fallback are exactly as
described. What was WRONG was the claim's reach: every measurement above
was taken with `SL_TEXPACK_ROOT` exported at a research workspace, which an
ordinary launch never sets, so on the reference play machine the player-data root
did not exist, `set-folder=... NOT FOUND` and every texture drew ORIGINAL
under all three settings. Read the visual evidence above as "the runtime
does this WHEN A PACK IS PRESENT", never as "a launch finds one".

The repair is entirely outside the binary: the converter now lives in the
tree (`tools/texpack`, driven by `tools\windows\prepare-textures.ps1`), a
checkout keeps two gitignored directories for the sources and the pack it
writes, and `tools\windows\play.ps1` hands that pack root to the child when
it exists - an explicit `SL_TEXPACK_ROOT` still winning. The witnesses were
then re-taken through `play.ps1` with NOTHING set (Facility 05d3 REPLACED
256x256 under XBLA, Archives 0112 REPLACED 352x528 under COMMUNITY HD,
ORIGINAL replacing nothing), and with the research workspace made
unavailable to the launch. Acceptance remains PENDING: docs/backlog.md
(2026-09-22) is the evidence.

**Second amendment, 2026-09-22 (acquisition; the runtime contract is again
unchanged).** Nothing above moves. `src/gfx`, `src/game` and `src/platform`
keep the same seam, the same SLTX format, the same root ladder and the same
per-texture fallback; the only binary change in this round is a diagnostic
that is dark unless `SL_TEX_DUMP_IDS` is set (the decode written once per
texture number, so a whole mapping can be compared offline).

What changed is where a pack comes from, and it changed in the direction the
recorded permission already allowed. Sightline still redistributes, mirrors
and pre-converts NOTHING. Instead `tools\texpack\get-textures.ps1` fetches
the Community HD project's own pinned release from its own GitHub releases,
on the user's machine, verifies size and SHA-256 against
`tools\texpack\community-source.json`, and converts it; `prepare-textures.ps1`
calls it for a checkout, and a RELEASE PACKAGE now ships the same script with
a `Get-Textures.cmd` beside the launcher, writing the converted set into the
player-data root the binary already reads. The packaged conversion needs no
Python (PNG through System.Drawing) and is byte-identical to the developer
one over the whole set, so a player's pack and a developer's pack are the
same files. **The XBLA set is unchanged and unautomated: user-supplied,
never fetched, with no acquisition pointer anywhere in the tree or in any
package.**

### D-019 — WORLD DETAIL ENHANCED (#43): the near-fog visibility-range rejection of props and characters is not applied at the render seam, and a prop the TICK's copy of the same test rejected is drawn without its on-screen flag (the render-only lift), so a prop stays eligible until the far-fog cull or the room / portal set removes it; default ORIGINAL = the accepted rendering byte for byte (native-only setting, the D-009 class; render only, the simulation's own copy of the same test is untouched)

- **Date:**        2026-09-21 (the lift added the same day, on the playtest Dam yard marks)
- **Phase:**       1 (v0.3.0 Visual Fidelity, the first World Detail sprint)
- **Commit:**      (this branch, sightline/world-detail) - Gitea #43: src/platform/sl_settings.{c,h} + src/native/sl_settings_apply.c (the row and the accessor pair), src/game/propobj.c chrobjFogVisRangeRelated (the render seam), src/native/sl_world_detail.c + the objTick / sub_GAME_7F04AC20 / chrprop.c islands (the lift), src/native/sl_front_options.c (the DISPLAY tab row), src/game/options.{c,h} (the watch's GRAPHICS child)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seam is one function; it has a live effect only on the seventeen levels whose fog row carries a near-fog triple - Facility's row is NULL there and the gate is dead, B-051)
- **First tick:**  n/a - no hashed state moves (measured: the Dam pad replay's per-tick state trace is byte-identical under both profiles, below)
- **Fields:**      none. Pixels only: under ENHANCED a prop or guard the cartridge's rule would have dropped at its size-scaled distance is drawn, in the same material, until the far-fog cull (fogGetPropDistColor) drops it
- **Toggle:**      `world_detail` in the native settings store (`%LOCALAPPDATA%\sightline\config.ini`), **0 = ORIGINAL (default): the accepted Sightline rendering exactly as it stood before this entry, N64 visibility tuning kept**; 1 = ENHANCED. Read by the game through `sl_world_detail_enhanced()` (sl_settings_apply.c) at the one seam; an inactive store (trace replay, headless) answers ORIGINAL. Two views: OPTIONS -> SETTINGS -> DISPLAY `WORLD DETAIL  ORIGINAL ENHANCED`, and the watch's SIGHTLINE -> GRAPHICS `world detail  original / enhanced`.

**What changed**

`chrobjFogVisRangeRelated` (propobj.c) is the cartridge's size-scaled
visibility-range test: with the level's near-fog triple (NearFog,
MaxVisRange, MaxObfuscationRange) and the FOV factor c_lodscalez it returns
0 - "not drawn" - once

    zDepth >= MaxObfusc + size * (MaxVisRange / c_lodscalez - MaxObfusc) / 100

The two callers are render functions (chrobjRenderProp :7619, chrRenderProp
chr.c:2907), and a 0 makes each return without emitting a display list.
Under ENHANCED the native arm returns 1.0 instead, so the prop or guard is
drawn; the far-fog cull that precedes it in both callers, the room / portal
visibility that decides which props reach the render at all, and the
tick's own on-screen test (PROPFLAG_ONSCREEN) are exactly as before. No
constant moves and no threshold is scaled: the fog is where the eye stops
seeing, and everything nearer is drawn. Under ORIGINAL the function is the
B-133 / D-004 rule unchanged (fully visible until the cull; SL_PROP_FADE=1
still restores the cartridge's fade band there).

THE LIFT (the playtest Dam yard marks, 2026-09-21: "the boxes still stream
in late"). The same formula runs a second time on the TICK side -
posIsOnScreen (propobj.c objTick :5948) -> sub_GAME_7F054C58 - and a prop it
rejects never gets PROPFLAG_ONSCREEN, no render matrices and no place in
g_OnScreenPropList, so the render seam never sees it. Under ENHANCED such a
prop, when posIsOnScreen admits it with that one term removed (the same
call with applyFogCull = FALSE: rendered room, fog-visible, inside its
rooms' portal window) and its on-screen matrix path is the generic one
(not a door, CCTV, autogun, vehicle, aircraft or tank), takes the on-screen
arm for its RENDER data only - the matrices, prop->zDepth, the model's
relations - while the flag is cleared as the off-screen arm would have, the
sim owner's shade lerp is skipped and its children take the off-screen
child tick. It joins this frame's lift list (src/native/sl_world_detail.c),
which chrpropsRenderPass walks after the room's on-screen props of the same
pass; sub_GAME_7F04AC20 accepts a lifted prop in place of the flag test.
ORIGINAL: the list is empty and every island is the __sgi arm's decision.

**Why**

The #43 contract: an optional profile in which "props not popping
at close range" and "obvious geometry and prop pop-in reduced", with
sensible culling retained. The world-detail recon (docs/backlog.md,
2026-09-21) walked the pipeline from the room set to the pixels and found
this the FIRST load-bearing restriction on the current build that is (a)
demonstrably an N64 draw-budget rule rather than the visual limit, (b)
render-only by construction, and (c) live and witnessed. The witness: Dam
(near-fog 3333 / 4444 / 600, far-fog cull at 67411), pose 15605 / 60 / 3755
theta 156.7 room 123 - the gate's alarm panel (size 20.8) at 1500.8 units
and its monitor (33.8) at 2230.4 are CULLED-visrange with the fog at 0%
while the gate's crates (size 94) at 2047-2376 draw; the cartridge at the
same pose (romtele.py, parallel_n64) draws neither, so the rule is Rare's
(B-090 had already matched the disassembly and the playtest log at 9 of 9
points). The derived cutoffs across the levels (a 30-unit pickup: Archives
1175, Depot 1110, Aztec / Egypt 1520, Dam 1642, Streets 2350; a 72-unit
crate: Depot 1544, Statue 2210, Dam 3101; a 200-unit guard on Dam: 7547,
where the fog is still only 69%) put the rule well inside clear air on most
of the campaign.

**Why it is safe**

The boundary was derived, not assumed: a tree-wide grep finds exactly the
two render callers, and the gameplay side reads the same table row through
its own functions - posIsOnScreen -> sub_GAME_7F054C58 and
fogPositionIsVisibleThroughFog (the AI's if-I'm-on-screen, the scripts'
tests, the prop bookkeeping) - which this entry does not touch. THE LIFT'S
BOUNDARY: PROPFLAG_ONSCREEN is hashed by the trace (sl_state_reader.c),
read by the AI (chrai.c :1832, aicommands.def :3283) and is the membership
of g_OnScreenPropList - the list a shot (chraiDefaultWeaponFireHandler), a
punch, INTERACT (propFindForInteract) and auto-aim walk - so the lift never
sets it and never joins that list; what it writes (render matrices in the
frame allocator, prop->zDepth, the model's rwdata relations) is hashed
nowhere and read by the simulation only behind that flag. THE CONSEQUENCE
TO WEIGH: a lifted prop is visible but, until it comes inside
the cartridge's own range, it is exactly what it is under ORIGINAL - not
shootable, not interactable, not a target; a bullet passes through it to
whatever the cartridge would have hit. Lifting the flag instead would
change the simulation (project rules 1 and 5). Measured: the dam-pad input
stream replayed WINDOWED with the settings store active and SL_TRACE_OUT,
3000 pumped frames, under ORIGINAL and under ENHANCED, before and after the
lift - all four per-tick state traces are byte-identical (613402 bytes,
sha256 A1FD1FB8...) while the census read reject=178 lifted=0 tick-lift=0
against reject=16664 lifted=16664 tick-lift=8224 (the lifted props reach
the render seam twice a frame, opaque and alpha pass); the unbounded pair
before the lift (21815 / 21773 ticks) is identical over its whole common
prefix of 8,844,646 bytes.
ORIGINAL identity: the pre-feature exe (master 8af7aa29) and this exe, at
four teleport poses (Facility catwalk, the Dam witness, the Bunker 2 crate
hall, the playtest Dam mark 1), key absent and key 0, SL_VI_CATCHUP=0 -
byte-identical frames (sha256 7CC42D4F..., 4357AB44..., C5D30773...,
FB0AFCAF...) and identical DL census. Positive controls: ENHANCED at the
Dam witness pose lifts 4 evaluations (the two props plus two guards), frame
121 tris 1786 -> 1828, and the frame differs in exactly 78 pixels - the
alarm panel; at playtest mark 1 (Dam 15227 / 60.3 / 13970.6 theta 212.4,
room 110) the tick lifts 16 props (9 crates, 4 multi-monitors, 1 armour, 2
glass), tris 3504 -> 4404, 1601 pixels differ - the mid-yard container
stacks - and the cartridge at that pose (romtele.py) withholds the same
stacks ORIGINAL withholds. test.ps1 facility
300 PASS; settingstest 168/168; displaytest 67 + 171; inputtest 22
B-096-class failures unchanged; check-layering 282 = baseline; the __sgi
arms of propobj.c / options.c / options.h are token-identical to the base
(preprocess proof, non-vacuous). `make trace-verify` NOT run (no MIPS
toolchain on this host).

---

### D-018 — the modern pad's DEFAULT bumpers MIRROR the triggers (design decision, 2026-09-21): RB / R1 is a second FIRE and LB / L1 a second AIM in every live preset, the weapon cycle and the zoom are the d-pad's alone, and the BUMPER preset is retired; the scope-aware source mechanism is unchanged (native-only setting, the D-009 class - but the DEFAULT itself moves, deliberately)

- **Date:**        2026-09-21
- **Phase:**       1
- **Commit:**      322ca69e (src/platform/sl_bindings.c g_layouts and the retired flag, sl_bindings.h, the watch row's stepper) - Gitea #64, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the change is the native binding registry's default table; nothing in src/game changed at all)
- **First tick:**  n/a - no recorded trace carries pad input; the registry is consulted only by the live input path
- **Fields:**      none hashed
- **Toggle:**      `pad_button_layout` in the native settings store, and every pad slot is editable in both BINDINGS editors. Unlike the rest of the D-009 class the DEFAULT is not the previous behaviour: this entry exists because the shipped default deliberately changed. A config that already spells the old table keeps it (it loads as CUSTOM).

**What changed**

The compiled pad default and the preset table (`g_layouts`). Before: RB was
NEXT WEAPON + ZOOM IN and LB PREVIOUS WEAPON + ZOOM OUT - the scope-aware
cycle introduced in #63 round 6 - with the d-pad carrying the same pairs.
Now RB is FIRE's second slot and LB is AIM's, in DEFAULT, SOUTHPAW (sides
swapped) and GREEN THUMB; the weapon cycle and the zoom are the d-pad's
alone (left / right cycle, up / down zoom). GREEN THUMB's R3 AIM takes the
slot LB holds elsewhere - two slots per action is the whole editor - so LB
is unbound in that preset only. The BUMPER preset is retired: its point was
the TRIGGERS cycling weapons, which is what this removes. Its id stays
occupied (the setting is persisted and its ids are append-only) and is
never offered, applied or matched.

FIRE and AIM are UNPAIRED actions, so their rows carry no context
(`sl_bindings_source_ctx`) and a bumper acts in every context - measured
inside the sniper scope. The scope-aware MECHANISM is untouched: the `ctx`
flag still rides on the wheel and on both bumpers, so a player who binds a
cycle or zoom action to a bumper by hand still gets the two-row behaviour,
the stale rule and the conflict policy exactly as before.

**Why**

Design decision, 2026-09-21: RB / LB and R1 / L1 should not cycle the
weapons; they mirror the triggers for aim and shoot, and the d-pad handles
weapons. A control-feel decision on a native-only control layer the
cartridge does not have.

**Why it is safe**

Nothing in src/game changed (`git diff --stat -- src/game` empty for this
commit), so the matching build and every recorded trace are untouched: no
trace carries pad input, and the registry is read only by the live input
path. The three config paths were witnessed on scratch files - a DEFAULT
file with no pad `bind.` lines gets the new table with no manual step, a
custom pad row is kept verbatim as CUSTOM, and a file recording the retired
BUMPER loads as CUSTOM keeping whatever lines it holds, so no player's
controls change under them. In play on Dam with an SDL virtual Xbox pad and
the sniper rifle: RB alone `FIRE+H+P` with the N64 Z bit, LB `AIM+H` and
the context turning `scoped`, RB while LB held `ctx=scoped held=3000
pressed=1000 FIRE+H+P AIM+H`, d-pad up scoped `ZOOM_IN+H` with the zoom
running 11.27 -> 7.00, d-pad right `WEAPON_NEXT+H+P`. inputtest's
modern-pad case 132 -> 139 checks, 0 failed (including the retired
preset's refusals and the hand-binding mechanism); pad-tune, pad-tune-invert
and hold-toggle 0 failed; the main run's 22 B-096 failures unchanged.

---

### D-017 — the native menu POINTER is drawn across the CONTENT rect while the hit position stays Rare's, and a click outside the 4:3 image confirms nothing (#65): the widescreen cursor could not reach the window's edges and a click in a side band activated whatever the frozen cursor sat on (native-only presentation, no game state)

- **Date:**        2026-09-21
- **Phase:**       1
- **Commit:**      c203b508 (src/gfx/sl_gfx_dl.c placement tag + sl_gfx_content_rect, src/platform/sl_display.c sl_display_pointer_logical, src/native/sl_menu_pointer.c, src/native/sl_watch_pointer.c, src/platform/sl_input.c, one guarded call in src/game/front.c) - Gitea #65, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the front end and the solo watch; the change is in the render and input layers)
- **First tick:**  n/a - the cursor's DRAWN position is a render fact and the game's own cursor variables are written by the same code as before
- **Fields:**      none hashed. `cursor_h_pos` / `cursor_v_pos` keep exactly the values they had: they are written only while the pointer is over the 4:3 safe rect, and the game's own 20-unit inset still clamps them.
- **Toggle:**      none. The drawn cursor follows the pointer only when the MOUSE owns the cursor (`sl_input_pointer_owns`); a pad or keyboard player sees Rare's placement drawn exactly as before, and at the 4:3 aspect the frames are pixel-identical (measured).

**What changed**

Three things that were one variable are named apart: the PHYSICAL POINTER
(window pixels), the HIT POSITION (Rare's cursor, unchanged) and the
VISIBLE CURSOR. The cursor's texrect - the front end's red crosshair and
the watch's reduced one - is now placed anywhere in the #45 CONTENT rect,
the bands a wider aspect adds included, by a tagged no-op the renderer
consumes with the next texrect (`C0 'SLC'`, the rect's top-left in signed
1/4 logical px); that one quad is drawn with the scissor lifted, because
the level's own [0,10]-[320,230] scissor clipped a crosshair at the top and
bottom edges. A pointer outside the safe rect hovers nothing (the hit
position is left where it was, never clamped to an edge) and a left click
there into a cursor menu is DROPPED instead of confirming.

**Why**

Observed in play on the v0.2.0 candidate: at any aspect wider than 4:3 the
red cursor could not reach the visible left / right edges, and in the watch
the crosshair vanished at the edges. Measured before the change: a probe at
window (0,360) of 1280x720 drew nothing new and left the cursor at Rare's
placement, and a probe at (40,360) plus a click produced `menu 6 -> 26` -
the click activated the row the frozen cursor still sat on. Underneath it,
a texrect cannot name a position left of logical 0 (E4's corners are
unsigned) and the game's own `draw_textured_rectangle` clips `xl < 0` to 0,
so no game-side coordinate could ever have reached the band.

**Why it is safe**

At 4:3 the content rect IS the safe rect and the tag names the corner the
untagged draw would have had, so the frames are pixel-identical - measured
against the pre-round binary on the same seed and probes (the baseline, two
mid-screen probes and a click that opened OPTIONS: identical; the two
probes inside Rare's 20-unit inset differ only inside the cursor's own box,
with the same row highlighted in both logs). Every front-end hit test, every
menu target and the dossier's layout are untouched. src/game gained one
guarded call in `frontDrawCursor`; front.c preprocesses token-identically
under `__sgi` (+10 native lines, zero repair tokens). displaytest 171 / 0
(17 new mapping checks), inputtest's four new click-gate checks pass and
the main run's 22 B-096 failures are unchanged, test.ps1 300 PASS.

---

### D-016 — the front end's mode-select gains a fourth native row, 4. QUIT GAME (#66), which files the one quit request the frame pump already honours for SDL_QUIT (native-only screen change; the cartridge's three rows are untouched under `__sgi`)

- **Date:**        2026-09-21
- **Phase:**       1
- **Commit:**      9b554aec (src/game/front.c native arm, src/platform/sl_window.c sl_quit_request / _requested, src/platform/sl_ultra_shim.c) - Gitea #66, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      n/a (the front end)
- **First tick:**  n/a
- **Fields:**      none hashed
- **Toggle:**      none. The row is present in the native build only; the `__sgi` arm still draws Rare's three rows (and the third only when a cheat is unlocked, as the cartridge does).

**What changed**

`interface_menu06_modesel` / `constructor_menu06_modesel` (native arm) draw
and hit-test a fourth row under OPTIONS, placed by the menu's own rule: a
row every 0x20 with its band starting 9 above its text (Rare's 243 / 275
compares), so text at 0x13C, box 0x13A..0x14A, band from 307, all plus the
existing `SL_MODESEL_DY` group offset. Confirming it (START / Z / A - what
a click, Enter and the pad's A all become) plays the menu's own confirm
sound and calls `sl_quit_request`; the frame pump reads the flag at the
next frame boundary and takes the path closing the window already took -
`sl_gfx_shutdown` then `exit(0)` with its atexit work.

**Why**

Observed in play: at a large resolution or in exclusive fullscreen there was
no normal way to close the game - no X to click, and a borderless window at
5120x1440 hides the desktop.

**Why it is safe**

Nothing exits from menu code: a tree-wide grep for exit / _exit / abort /
ExitProcess over src/game and src/native finds two hits and neither is a
call. The flag lives with the window's other lifecycle state so the display
self-test asserts it (5 checks: unset until activated, latching, changing no
display state, cleared afterwards - the test never calls exit). front.c
preprocesses token-identically under `__sgi` (+49 native lines, zero quit
tokens), so the cartridge screen is unchanged. Witnessed by keyboard, mouse
and a virtual pad, in WINDOWED, BORDERLESS, FULLSCREEN at the desktop's mode
and FULLSCREEN with a real 2560x1440 mode switch: exit 0 every time, no
crash, the scratch config's SHA256 unchanged, and the desktop measured back
at 5120x1440 after each.

---

### D-015 — WINDOW MODE, RESOLUTION and VSYNC (#52): three persisted PC display settings the SDL backend applies to its window and GL context - windowed at a chosen client height, SDL's fullscreen-desktop, or a real exclusive display mode, and the swap interval - live, transactional, under the #45 fit; default WINDOWED / nothing chosen / vsync off = the accepted launch bit for bit (native-only setting, the D-009 class)

- **Date:**        2026-09-20
- **Phase:**       1
- **Commit:**      see the #52 backlog entry of the same date (the settings rows, src/platform/sl_window.h/.c, the SDL backend's sdl_apply_display, the DISPLAY tab rows, the watch DISPLAY child rows) - Gitea #52, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the change is the window's, in src/gfx / src/platform; nothing in src/game changed behaviour - options.c / options.h gained native-arm rows only)
- **First tick:**  n/a - nothing in the simulation reads the window's mode, size or swap interval; the render side reads the framebuffer size at its frame reset exactly as before #52 (the #45 fit), through the one path
- **Fields:**      none hashed. The renderer's g_window_vp (and the content / safe rects derived from it), the input layer's confinement rectangle, and the pointer layers' safe-rect mapping follow the window as they already did on any size the window had
- **Toggle:**      `window_mode` (0 WINDOWED / 1 BORDERLESS / 2 FULLSCREEN), `window_width` / `window_height`, `fullscreen_width` / `fullscreen_height` (0 / 0 = not chosen), `vsync` (0 / 1) in the native settings store, **the defaults = exactly the pre-#52 launch** (the launcher's SL_WINDOW_SIZE window at the aspect's width, `SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN`, swap interval 0). Two views of the one store: OPTIONS -> SETTINGS -> DISPLAY (front end: WINDOW MODE, RESOLUTION, VSYNC before ASPECT RATIO and FIELD OF VIEW) and the solo watch's SIGHTLINE -> DISPLAY child. An INACTIVE store (trace replay, headless health) reads the defaults and the backend never consults it.

**What changed**

src/gfx/sl_gfx_sdl.c, `sdl_apply_display` (the successor of #45's
sdl_apply_aspect, whose width rule it keeps verbatim), once per frame at
the frame reset and once at init. The editors file a REQUEST through
src/platform/sl_window.c (a mode, a size of the current mode's list, a
swap interval); the backend takes it, remembers the working state, asks
SDL (WINDOWED: SDL_SetWindowFullscreen(0) then SDL_SetWindowSize to the
chosen height at the aspect's width, re-centred; BORDERLESS:
SDL_WINDOW_FULLSCREEN_DESKTOP; FULLSCREEN: the chosen pair when the
display offers it else the desktop's, at the DESKTOP'S refresh rate,
SDL_SetWindowDisplayMode then SDL_SetWindowFullscreen(SDL_WINDOW_FULLSCREEN)
- through a windowed hop when the window is already exclusive, measured
necessary on SDL 2.32.10's windows driver, where a mode change in place
switches the panel but leaves the HWND's client rectangle; VSYNC:
SDL_GL_SetSwapInterval on the live context), reads back (the flags, the
window size, the exclusive mode, the display bounds, the GL drawable, the
swap interval) and COMMITS to config.ini only what matched, restoring the
working state otherwise (and the launcher's windowed size should the
restore fail too); a fallback the backend took for an unsupported stored
value is logged once and never written. A chosen windowed height creates
the window at that size; a fullscreen mode creates it hidden and shows it
once the mode is established. The lists come from the display the window
is on: the exclusive list its modes deduped by width x height, the
windowed list the distinct heights that fit the desktop at the aspect's
width. Nothing else was told: the renderer keeps reading the window from
GL at its frame reset (sl_gfx_dl.c g_window_vp), the #45 fit keeps
deriving the content and safe rects, the input layer keeps re-reading the
size every poll, the pointer layers keep reading the safe rect back - so a
mode change reaches every consumer once, on the next frame, with no level
reload, no context recreation and no second copy of the aspect maths.

**Why**

Intent (#52): the player chooses the window mode, size and VSync
from the menus and has it persist, without touching a script; the window
contract of #45 holds in every mode (the aspect is the shape at the
current height; a fixed framebuffer FITS - pillarbox or letterbox, never a
stretch); accepted current behaviour (windowed, the launcher's size, VSync
off) is the default and a config without these rows behaves as today.

**Why it is safe**

Default identity is asserted against a MEASUREMENT taken before the first
edit: the 230b4b52 exe on a user-shaped config through play.ps1's
launch (client 960x720, window rect 966x749 at (2077,334), style
0x16ca0000, the `sl_display:` / confinement / aspect lines) and the new
build on the same config print the same lines byte for byte
(Compare-Object 0 differences over the 8 lines; the external window probe
the same numbers; the 16:9 launch the same, 1280x720), the config's hash
unchanged, `swap=0` read back. The pacing contract is measured unchanged
with VSync on (SL_PHASE=1: NINTENDO 501 frames in 8.343 s at 60.1 fps vs
8.345 s at 60.0 off, catchup 0 both) - the swap interval is applied to the
presentation only, and trace replay (no window) never reaches it. The
mode transitions are read back from SDL and the OS (the drawable, the
client rectangle, the monitor rectangle by an external probe) in WINDOWED
960x720 / 1024x768 / 1067x800 / 1365x768, BORDERLESS 5120x1440 at 4:3 /
16:9 / 32:9 (the fit 1920x1440 / 2560x1440 / 5120x1440 with the 2D layer
1920x1440 centred), exclusive 5120x1440 and 2560x1440 (a real mode switch,
back again), and the rollback path was witnessed on a transition SDL
reported but the OS did not honour (restored, nothing written). The
front end's rows and the watch's rows hit at the same logical spot in
every mode through the existing pointer mapping (SL_POINTER_PROBE), and a
WINDOWED -> BORDERLESS -> WINDOWED switch from the open watch on Dam kept
the watch, its hit rects and the level. displaytest 149 / 0 (62 new),
settingstest 156 / 0 (10 new), inputtest the 22 pre-existing B-096
failures byte-identical, test.ps1 facility 300 PASS. options.c / options.h
(the watch rows) preprocess byte-identically to 230b4b52 under __sgi (md5
c5b3b9864b82 / 89af0a83a899), non-vacuous control, native arms +89 / +11
lines, zero #52 tokens. `make trace-verify` NOT run (no MIPS toolchain; the
store is inactive in every replay, no window exists there, and the __sgi
arms are verbatim).

### D-014 — CROUCH MODE and SPRINT MODE, HOLD or TOGGLE (#56): under an opt-in persisted setting per action, a fresh press of a CROUCH or SPRINT source flips a runtime latch at the platform action layer instead of the level following the control; default HOLD = the pre-#56 behaviour bit for bit (native-only setting, the D-009 class)

- **Date:**        2026-09-20
- **Phase:**       1
- **Commit:**      see the #56 backlog entry of the same date (settings rows, the action-layer transform and harness case, the GAMEPLAY tab rows, the watch GAMEPLAY child rows) - Gitea #56, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the transform is the platform action layer's; nothing in src/game changed behaviour - options.c / options.h gained native-arm rows only, and bondview2.c's crouch and sprint consumers are untouched)
- **First tick:**  n/a under HOLD (nothing moves). Under TOGGLE only the published CROUCH / SPRINT level differs from the raw one - a player input, not a state the harness hashes independently of input
- **Fields:**      none hashed. The action channels' held mask (sl_action_channels_set) carries the latch in place of the level for the two actions; every other action, the movement channels, the N64 buttons, the registry, the mouse and the menus do not read it
- **Toggle:**      `crouch_mode` and `sprint_mode` (0 HOLD / 1 TOGGLE) in the native settings store, **default HOLD = exactly the pre-#56 behaviour** (under HOLD sl_action_modes_apply leaves the evaluated state untouched). Two views of the one store: OPTIONS -> SETTINGS -> GAMEPLAY (front end: SPRINT MODE / CROUCH MODE after SPRINT) and the solo watch's SIGHTLINE -> GAMEPLAY child. An INACTIVE store (trace replay, headless health, the input harness's main run) reads HOLD.

**What changed**

src/platform/sl_action.c, `sl_action_modes_apply`, called by sl_input.c
between the registry evaluation (sl_action_eval) and the publish
(sl_action_channels_set). Under TOGGLE the action's `held` is replaced by
ONE runtime latch per action - never per source: Ctrl and the pad's B flip
the same CROUCH latch, Shift and L3 the same SPRINT latch - that the
evaluator's own fresh-press edge flips (PRESSED, with the round-6 stale
rule; no second edge history exists): inactive + press -> active, active +
next press -> inactive; holding never repeats, releasing never toggles. The
flip is gated on the publish predicate (live input, no menu), so a press in
the watch, the front end or an editor never flips, and a control held
across a menu's close finds its per-slot raw memory already down - no
stale edge in either direction. The latch is dropped when the action's
mode changes (HOLD then follows the level; TOGGLE starts OFF and wants a
release and a fresh press even with a control held through the change),
when its bindings change (a per-action generation the registry advances on
every slot write, steal, preset, RESET DEFAULTS and load -
sl_bindings_generation), when SPRINT ENABLED is off (a disabled sprint
cannot stay latched; on again does not resume it), at every stage start
(sl_settings_apply_player_defaults -> sl_action_latch_reset: the front-end
flow, a restart after death, SL_BOOT_LEVEL; the edge history is kept, so a
control held across it is not a fresh press), and on a focus loss or
shutdown (sl_action_reset - no stuck crouch or sprint). The game's
consumers (bondview2.c:5776 SPRINT, :5783 CROUCH) read the same level bits
as before and do not know which mode produced them; a toggled sprint may
stay latched while stationary and applies again when movement resumes
under the game's own gates, with no invented auto-cancel; a toggled crouch
is the existing g_sl_crouch_native state driven by a constant level.
Nothing is persisted but the two modes.

**Why**

Intent (#56): Crouch and Sprint each get a HOLD / TOGGLE choice,
persisted, shown in both editors, with HOLD (today's behaviour) as the
default - the cartridge's own AIM CONTROL vocabulary. A PC player expects a
toggled crouch and a toggled sprint from a keyboard; the N64 pad never had
either. The interpretation belongs to the action layer, where the
semantics already live, so the game is handed the same level it always
was.

**Why it is safe**

Default identity is asserted against a MEASUREMENT taken before the
transform existed: the real sl_input.c / sl_action.c / sl_bindings.c /
sl_settings.c through the inputtest stubs at 68fd4d1e printed a 92-row
table (per poll: the physical keys / pad buttons / menu state fed in -> the
published CROUCH / SPRINT level and edge, the held mask, the four movement
channels - across press / hold / release on both devices, two sources,
the watch both directions, a focus loss and the negatives; scratch
holdtoggle witness-pre.txt) and the same program on the new tree with no
config prints it byte-identically (Compare-Object: 0 differences). The
in-game sprint magnitudes are unchanged under HOLD (Surface, the #42
measurement, the pre-change and post-change binaries: W 2023.1 units per
240 ticks and 9.693 / tick, W+Shift 2785.9 and 12.978 / tick with spd
1.729, W+D 2789.0 and 12.885 / tick, W+D+Shift 2789.0 = W+D exactly,
sprint=1 held-not-applied; the one 2784.8 reading on the pre binary
repeated as 2785.9 twice and is injection timing). The hold-toggle
inputtest case pins the semantics (127 checks, 0 failed: the HOLD identity,
the CROUCH trace OFF / press ON / hold ON ON ON / release ON / idle ON /
press OFF / hold OFF / release OFF, the second source, keyboard = pad, the
menu and watch isolation both directions for both actions, the mode
changes with a control held, the SPRINT trace through stop and resume,
sprint_enabled off / on / fresh press, both resets, the remap and RESET
DEFAULTS, the negatives); settingstest 146 / 0; the inputtest main run 219
with the 22 pre-existing B-096 failures byte-identical; test.ps1 facility
300 PASS; displaytest 87 / 0. options.c / options.h (the watch rows)
preprocess byte-identically to 68fd4d1e under __sgi (md5 c5b3b9864b82 /
89af0a83a899), non-vacuous control, native arms +28 / +7 lines, zero #56
tokens. `make trace-verify` NOT run (no MIPS toolchain; the store is
inactive in every replay and the __sgi arms are verbatim).

### D-013 — CONTROLLER LOOK SENSITIVITY, LOOK DEADZONE and MOVE DEADZONE (#51): three persisted values tune the modern pad's sticks at the platform seam - the look pair's gain after the stick layout has routed it, and the inner deadzone of each pair - default 100 / 15 / 15 = the accepted pad arithmetic bit for bit (native-only setting, the D-009 class)

- **Date:**        2026-09-20
- **Phase:**       1
- **Commit:**      see the #51 backlog entry of the same date (settings rows, the seam, the PAD tab rows, the watch STICK TUNING child) - Gitea #51, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seam is the platform layer's pad reader; nothing in src/game changed behaviour - options.c / options.h gained native-arm rows only)
- **First tick:**  n/a at 100 / 15 / 15 (nothing moves). At any other value only the live pad's channel values change - a player input, not a state the harness hashes independently of input
- **Fields:**      none hashed. The four movement channels (sl_move_channels_set) carry the tuned values; the N64 stick, the buttons, the registry, the mouse and the menus' stick do not read the gain
- **Toggle:**      `pad_look_sensitivity` (25..200 step 5), `pad_look_deadzone` and `pad_move_deadzone` (0..40 step 1) in the native settings store, **default 100 / 15 / 15 = exactly the pre-#51 arithmetic** (100 / 100.0f is 1.0f; 15 x 5000 / 15 is the compiled SL_PAD_DEADZONE 5000). Two views of the one store: OPTIONS -> SETTINGS -> PAD (front end) and the solo watch's SIGHTLINE -> CONTROLS -> STICK TUNING child. An INACTIVE store (trace replay, headless health, the input harness) reads the defaults.

**What changed**

src/platform/sl_input.c. `pad_axis(raw, dz)` keeps the shape the pad
reader has always had - per axis (a square zone), the remainder over
(32767 - dz) so full travel still reaches 1.0, clamped - with the size a
parameter. read_pad applies the LOOK DEADZONE to the two physical axes the
STICK LAYOUT routes to turn / pitch and the MOVE DEADZONE to the other two,
by one roles table (s_stick_roles) that map_pad_modern's routing now reads
as well, so the deadzone and the routing cannot disagree under SOUTHPAW,
LEGACY or LEGACY SOUTHPAW. map_pad_modern multiplies the LOGICAL look pair
(turn, pitch - after the routing, after the deadzone, before the developer
SL_LOOK_INVERT and before channel()'s +/-70 clamp) by LOOK SENSITIVITY /
100. The cartridge's own curve on the channel (bondview2.c:6533 turn,
:6453 pitch: the value over 70, signed-squared, times fovy / 60) is
untouched: 100 = full stick = the game's full turn rate; a higher percent
reaches that rate at a smaller deflection and never past it (200 saturates
from half the remaining travel); the sniper zoom slows the pad through
fovy / 60 exactly as before; the game's Look Up/Down option still flips
the pad's pitch game-side. The intent carries the TUNED axes, so the
last-device-wins arbitration, the menu's digital step and the watch's
controller picture all see a stick inside its deadzone as neutral. The
deadzone percent p is p x 5000 / 15 raw SDL units - 15 is the compiled
5000 exactly, every other value within one percent of p percent of the
32767-unit travel (0 = no deadzone, 40 = 13333).

**Why**

Intent (#51, unblocked by #63's acceptance): the controller's look
tunable from the menus and persisted - sensitivity and the stick deadzone
at minimum - with the classic feel as the default. The pad is a native
producer with no cartridge behaviour to preserve beyond the accepted feel,
which the defaults reproduce exactly. The move deadzone rides along
because it is the same compiled constant on the other pair, a drifting
left stick makes Bond creep, and exposing it is one table row; there is no
move sensitivity (full stick is the game's full speed).

**Why it is safe**

Default identity is asserted against a MEASUREMENT taken before the seam
existed: the real sl_input.c through the inputtest stubs at 04b92554
printed a 230-line transfer table (raw SDL axis -> the snapshot's
normalized value -> the four channels: both sticks, both signs, the
boundary raws 4999 / 5000 / 5001, the diagonals, the four stick layouts,
aiming, the menu; scratch analog witness-pre.txt) and the same program on
the new tree with no config prints it byte-identically (Compare-Object: 0
differences). The pad-tune inputtest case pins those numbers (73 checks,
0 failed, and 73 / 0 again under SL_LOOK_INVERT=1: 20 oracle raws x 7
probes; 50 -> 35 at full stick, 200 -> 57 at half and 70 from 18884 on,
25 -> 17; deadzone 30 kills raw 7777 and rescales 16384 to 19, 0 wakes
5001 to 10, 40 leaves full stick at 70; the four layouts tune the look
pair only; the d-pad, the bumpers, the mouse's 6.00 degrees, W's 70 and
the menu stick unchanged). settingstest 134 / 0; modern-pad 124 / 0;
inputtest main 219 with the 22 pre-existing B-096 failures byte-identical.
options.c / options.h (the watch rows) preprocess byte-identically to
04b92554 under __sgi (md5 c5b3b9864b82 / 89af0a83a899), non-vacuous
control, native arms +63 / +22 lines, zero #51 tokens. `make trace-verify`
NOT run (no MIPS toolchain; the store is inactive in every replay and the
__sgi arms are verbatim).

### D-012 — THE D-PAD AND THE BUMPERS IN PLAY (#63 / #64, round 6): the pad's d-pad is four registry sources in play (DEFAULT: up / down ZOOM IN / OUT, left / right PREVIOUS / NEXT WEAPON) and no N64 d-pad bit reaches the game from the pad outside a menu; the two bumpers are contextual sources by the wheel's rule (NEXT / PREVIOUS WEAPON on foot, ZOOM IN / OUT in the scope); a zoom action on a held button zooms for as long as it is held

- **Date:**        2026-09-20
- **Phase:**       1
- **Commit:**      see the #63 / #64 round-6 backlog entry of the same date - Gitea #63 / #64, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the platform layer's pad mapping and registry; one native channel getter; nothing in src/game touched)
- **First tick:**  n/a - player input, not a state the harness hashes independently of input; the store and the action channels are inactive in every replay
- **Fields:**      none hashed. The d-pad's actions reach the game through sl_action_channels (D-005) as ZOOM_IN / ZOOM_OUT / WEAPON_PREVIOUS / WEAPON_NEXT; the pad's U/D/L/R_JPAD bits are 0 in play (they were the cartridge's C-button aliases under 1.1 Honey: bondview2.c:5427 look, :5495 the scope's zoom, :5419 the digital strafes) and unchanged in a menu
- **Toggle:**      **none** - a requirement (#63 / #64) on the modern pad (D-011's only pad), not a change to Rare's code: the game still reads the N64 pad exactly as it did; what the native layer puts on it changed. A player who wants the old d-pad behaviour rebinds: the four directions are ordinary registry sources (pad:DPAD_UP etc.), and an unbound direction does nothing in play. The bumpers' rule is the registry's `ctx` flag on LB / RB (sl_bindings.c g_pad), the same flag the wheel carries; BUTTON LAYOUT BUMPER puts AIM / FIRE on them, which is a plain level (the rule gates only weapon-cycle and zoom rows). The compiled DEFAULT preset changed: RB next weapon + zoom in, LB previous + zoom out, the d-pad as above, Y and R3 unbound (two slots per action); GREEN THUMB's B is CROUCH again.

**What changed**

src/platform/sl_bindings.c: four d-pad sources (tokens pad:DPAD_UP / DOWN /
LEFT / RIGHT, family label "D-PAD UP" etc.), the `ctx` flag on LB / RB and
the wheel, `sl_bindings_source_ctx` (a row of a contextual source on a
weapon-cycle action is PLAY, on a zoom action SCOPED, anything else no
context) read by the evaluator and the conflict policy in place of the
wheel-only test, the four presets re-seeded. src/platform/sl_action.c: a
level row out of its context contributes nothing; the STALE rule (an edge
needs a live source that went down THIS poll, so leaving the scope with RB
held does not step the weapon). src/platform/sl_input.c map_pad_modern: the
pad intent's d-pad bits pass only while a menu is up.
src/native/sl_action_channels.c: the getter reports a zoom tick while the
notch pulse runs OR the ZOOM level is held - the cartridge's own C-up feel
(zoomInFovPersec = 1.0 every tick the button is down; gun.c:1326 divides
the FOV by 1.1 per tick). Until round 6 R3's ZOOM IN was a two-tick pulse
per press and read in play as doing nothing.

**Consequence for an existing config**

A stored DEFAULT / SOUTHPAW / BUMPER / GREEN THUMB preset re-seeds to the
new table at the next preset apply; at load a config whose bind. lines
equal the OLD table no longer matches any preset and reads CUSTOM (the
row never lies), keeping exactly the bindings it held - RB / Y next weapon,
LB previous, R3 zoom in - with the d-pad unbound in play (it did the
Honey C-button aliases before). RESET DEFAULTS or choosing DEFAULT gives
the new table. A wheel bound by hand to an action outside the two pairs
(INTERACT, say) now fires in either context and its capture steals both
the play and the scoped row of that notch; the default wheel table is
untouched (asserted: wheel up / down scoped and unscoped).

**Why**

The playtest with a real Xbox pad and a DualSense (2026-09-20) reported:
"look and zoom in aren't mapped correctly. D pad isn't used at all, and it
should be, and zoom in is actually look." The requirement that followed:
zoom on LB and RB, so they zoom when scoping with the sniper rifle and
cycle weapons when not.

**Why it is safe**

inputtest `modern-pad` 124 / 0 (96 before): each d-pad direction to its
action with no N64 bit in play and the N64 bit in the watch, the held
zoom level, RB / LB unscoped -> the cycle and scoped -> the zoom (both
directions), the stale rule both ways, the d-pad's zoom rows as plain
levels, the wheel unchanged scoped and unscoped, a key on the cycle
unchanged in the scope, BUMPER's LB aiming straight through the scope and
its RT cycling there, the row contexts, the conflict policy on RB, every
preset re-seeding exactly with a behavioural probe, the label resolver's
two rows per bumper, the d-pad token and labels. The main run 219 with the
22 pre-existing B-096 failures byte-identical (218 before: the wheel-onto-
INTERACT case became two). Witnessed in play on Dam with the sniper rifle
(the round-6 backlog entry: the action lines). `make trace-verify` NOT run
(no MIPS toolchain; nothing in src/game changed, the channels are inactive
in every replay).

### D-011 — THE MODERN PAD IS THE ONLY PAD, and the control style is pinned (#63, round 4): the ORIGINAL / MODERN profile, BUTTON MODE and the N64 CONTROL STYLE setting are gone from the native build; the gamepad is always the dual-stick controller of D-010, the game's control style is 1.1 Honey on every stage start, and which thumb does what is the new STICK LAYOUT and BUTTON LAYOUT settings

- **Date:**        2026-09-20
- **Phase:**       1
- **Commit:**      see the #63 round-4 backlog entry of the same date - Gitea #63, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the platform layer's pad mapping and one native apply seam; nothing in src/game changed behaviour outside the native arms)
- **First tick:**  n/a - player input, not a state the harness hashes independently of input; the store is inactive in every replay
- **Fields:**      none hashed. The pad's sticks arrive as the channels of D-001/D-002 (which pair on which stick is the STICK LAYOUT), FIRE / AIM as N64 Z / R, the rest through sl_action_channels (D-005); g_CurrentPlayer->cur_player_control_type_0 is CONTROLLER_CONFIG_HONEY in solo play whatever the per-folder save carries
- **Toggle:**      **none for the removal** - a design decision, not a rule-5 toggle (there is no point hiding the old styles behind a profile: Sightline will never have an N64 controller attached, and it does not use emulation). The __sgi build keeps all eight styles and the N64 page verbatim. The two settings that replace the styles: `pad_stick_layout` (0 DEFAULT / 1 SOUTHPAW / 2 LEGACY / 3 LEGACY SOUTHPAW, default DEFAULT = D-010's routing) and `pad_button_layout` (0 DEFAULT / 1 SOUTHPAW / 2 BUMPER / 3 GREEN THUMB / 4 CUSTOM, default DEFAULT = the compiled pad table). Views: the watch's Control Options page (its two rows), the SIGHTLINE -> CONTROLS child, OPTIONS -> SETTINGS -> PAD.

**What changed**

Superseding D-010's toggle. `controller_profile`, `pad_button_mode` and
`control_style` are retired keys of the settings store (read as unknown
keys: ignored, never rewritten by a load, dropped on the next
write-on-change); `map_pad`, `map_pad_dual`, `style_is_dual`, the
SL_CONTROLS=retro knob and read_pad's fixed-button block are deleted from
src/platform/sl_input.c and `map_pad_modern` is the one pad mapping; the
registry's PAD slots are always live. src/native/sl_settings_apply.c
writes CONTROLLER_CONFIG_HONEY through cur_player_set_control_type at every
stage start in solo play (the seam that used to apply the store's style),
so Z is FIRE and R is AIM - what every native binding assumes; the SYNC
seam no longer mirrors the style. The compiled pad default table is now
the DEFAULT preset (RT fire, LT aim, RB + Y next weapon, LB previous, A
interact, X reload, B crouch, L3 sprint, R3 zoom in) - RB, LB and R3
changed meaning from the #46 CUSTOM layout for anyone who never edited
them. The watch's Control Options page draws BUTTON LAYOUT / STICK LAYOUT
in place of CONTROL STYLE / CONTROLLER and, beside the pad, each control's
family name with the action the live registry holds for it.

**Consequence for an existing config**

A `control_style` line is ignored. A keyboard player who had picked 1.3
Kissy or 1.4 Goodnight - under which the mouse's Z arrived as AIM and R as
FIRE - gets the Honey click back: left button FIRE, right button AIM (the
KBM registry never changed; the game's reading of Z / R did). A
`controller_profile=0` (ORIGINAL) file gets the modern pad. A
`pad_button_mode` line changes nothing (the registry was already live
under MODERN).

**Why**

Design decision 2026-09-20 (#63, after the round-3 playtest with a real Xbox
pad): no profile, and Sightline's own layouts (Halo CE's Button Layout and
Stick Layout as the model) in place of the N64 control styles.

**Why it is safe**

inputtest `modern-pad` 96 / 0: the family, the labels, the right and left
sticks at 10 / 50 / 100 percent and the twelve-point sweep with no C bit,
each of the four stick layouts routing a four-magnitude probe to the right
channels (and SOUTHPAW's look pair staying live while aiming), every
DEFAULT button, each preset seeding the table exactly with a behavioural
probe, CUSTOM on a hand edit and a preset again on re-seed, RESET reading
DEFAULT, a mismatched stored preset loading as CUSTOM, the label resolver,
the physical snapshot, the menu mapping, persistence with none of the
retired keys, no pad. settingstest 118 / 0 (section 6: a user-shaped
round-3 file with the three stale keys loads with every live key intact
and is not rewritten; section 12: the two layout keys). The main inputtest
run keeps its 22 pre-existing B-096 failures byte-identical (the ORIGINAL
identity cases that asserted the deleted path went with it). test.ps1
facility 300 PASS; options.c / options.h preprocess byte-identically to
45479b1b under __sgi (md5 c5b3b9864b82 / 89af0a83a899; control differs;
native arms +75 / +10 lines; zero new tokens). `make trace-verify` NOT run
(no MIPS toolchain; the store is inactive in every replay, the apply seam
returns before touching anything there, and the __sgi arms are verbatim).

### D-010 — CONTROLLER PROFILE MODERN (#63): under an opt-in persisted setting the gamepad is a modern dual-stick controller - both sticks reach the game through the four native movement channels (the keyboard/mouse seam, D-001/D-002's), its buttons and triggers through the binding registry, and the watch's Control Options page draws the attached family's own controller; default ORIGINAL = the accepted virtual N64 pad, byte-identical

- **Superseded by:** D-011 (2026-09-20): the profile is gone, MODERN is the only pad path, and the control style is pinned.
- **Date:**        2026-09-19
- **Phase:**       1
- **Commit:**      see the #63 backlog entry of the same date - Gitea #63, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the profile is the platform layer's pad mapping; nothing in src/game changed behaviour outside the native arms)
- **First tick:**  n/a under ORIGINAL (nothing moves). Under MODERN the pad's sticks arrive as the channels a live keyboard/mouse already publishes and its buttons as the registry's actions - player input, not a state the harness hashes independently of input
- **Fields:**      none hashed. Under MODERN: moveData.analogWalk/Strafe/Turn/Pitch through sl_move_channels (the D-001/D-002 seam), FIRE/AIM as N64 Z/R exactly as the keyboard's F/Q, the other actions through sl_action_channels (D-005); the N64 stick stays neutral in play and no C-button bit is raised for look
- **Toggle:**      `controller_profile` in the native settings store, 0 ORIGINAL / 1 MODERN, **default ORIGINAL** (rule 5: the accepted pad behaviour stays the default, and an existing file's `pad_button_mode` keeps its meaning). Two views of the one store: OPTIONS -> SETTINGS -> PAD (front end) and the solo watch's SIGHTLINE -> CONTROLS child (PROFILE). Live on the next poll; changing it rewrites no binding, style or button mode. An INACTIVE store (trace replay, headless, the input harness) reads ORIGINAL.

**What changed**

src/platform/sl_input.c reads the profile every poll (as BUTTON MODE is
read). Under MODERN `map_pad_modern` replaces `map_pad` / `map_pad_dual`
for the pad: the left stick becomes walk / strafe and the right stick turn
/ pitch in the game's own +/-70 unit (`channel()`, the mouse's fallback
arithmetic: full deflection = 70 = the channel's own maximum, partial
proportional, after the fixed 5000-unit inner deadzone), published through
`sl_move_channels_set` when the pad is the owning device - the consumer is
bondviewProcessInput's NATIVE MOVEMENT seam, which applies Rare's own
stick curve, ramp, pitch limits and the Look Up/Down option to them. The
right stick raises no U/D/L/R_CBUTTONS bit and the N64 stick stays neutral
in play (aim mode's crosshair stays centred, as for the mouse). The linear
mouse-look channel is not used by the pad (mouse_sensitivity, the scoped
percent and Invert Mouse Y never touch it). Buttons and triggers feed the
registry's PAD slots whatever BUTTON MODE says (the accepted #46 CUSTOM
layout by default; a user's custom pad bindings stand); FIRE / AIM leave as
the same N64 Z / R the keyboard's F / Q become, Start as START, the d-pad
as the d-pad; in a menu the left stick is the menu stick, A accepts and B
goes back. The pad's family (SDL_GameControllerGetType, SDL's mapping
database - never a product name) labels the editors' pad slots (A / CROSS,
RT / R2 ...; the persisted token `pad:A` never changes) and picks the model
the watch draws in place of GjoypadZ on the Control Options page, its parts
posed from the pad's physical state; no model, a generic pad or ORIGINAL
draws the N64 page exactly as before.

**Why**

Design direction (#63): a modern controller plays as a modern dual-stick
game, with the original experience selectable and unchanged. The control
styles still decide what Z / R do (the keyboard precedent: FIRE is Z under
every style, so 1.3 / 1.4 and the 2.3 / 2.4 hands read differently - the
accepted KBM behaviour, not a new one).

**Why it is safe**

ORIGINAL identity is asserted against HEAD's own platform sources, not
against this build's expectations: a witness program compiled once against
the 8891b62e sl_input.c / sl_action.c / sl_bindings.c / sl_settings.c and
once against the working tree prints, for a fixed set of pad inputs under
all eight styles (sticks, C-button deflections, A / B / X / Y / LB / RB /
RT / LT / Start / d-pad / L3, aiming, the menu) plus the keyboard and mouse
rows, a byte-identical 319-line table (md5 7b6512f7aa27 both, scratch
modern\witness_head.txt / witness_work.txt). The inputtest `modern-pad`
case (72 checks, 0 failed) pins the profile default and persistence, the
family classification, the labels, the ORIGINAL fixed map, the MODERN
channels at 10 / 50 / 100 percent on both axes and the diagonal, a
twelve-point sweep with no C bit anywhere (the C-BUTTON NEGATIVE
CONTROL), the left stick's proportional walk / strafe, the accepted
defaults on every button and trigger with the 8000 threshold kept, a
custom pad binding honoured, the menu mapping, the no-pad fallback and a
live switch both ways. settingstest 120 / 0 (section 12), test.ps1
facility 300 PASS, inputtest 225 with the 22 pre-existing B-096 failures
unchanged. options.c / options.h (the watch rows and the page hook)
preprocess token-identically to 8891b62e under __sgi (control differs,
native arms +69 / +6 lines, zero #63 tokens). `make trace-verify` NOT run
(no MIPS toolchain; the store is inactive in every replay and the __sgi
arms are verbatim).

### D-009 — MOUSE SENSITIVITY and SCOPED SENSITIVITY (#50): two persisted percents scale the live keyboard/mouse look at the one gameplay mouse-look seam, the second only under the game's own adjustable-scope predicate; default 100 / 100 = the accepted look bit for bit (native-only setting, the D-008 class)

- **Date:**        2026-09-19
- **Phase:**       1
- **Commit:**      see the #50 backlog entry of the same date (settings rows, the seam, the CONTROL tab rows, the watch CONTROLS rows) - Gitea #50, branch sightline/qol-controls
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seam is the platform layer's mouse gain; nothing in src/game changed behaviour)
- **First tick:**  n/a at 100 / 100 (nothing moves). At any other value only the live keyboard/mouse look rate changes - a player input, not a state the harness hashes independently of input
- **Fields:**      none hashed. The linear look channel's degrees (sl_mouse_look_set) and the +/-70 fallback channel scale by the factor; the pad sticks, the watch / front-end / bindings pointers, the buttons and the wheel do not read it
- **Toggle:**      `mouse_sensitivity` and `scoped_mouse_sensitivity` in the native settings store, percents 10..300 in steps of 10, **default 100 / 100 = exactly the pre-#50 arithmetic** (SL_MOUSE_SENS 6 x 0.025 = 0.15 deg/count; 100 / 100.0f is 1.0f). Two views of the one store: OPTIONS -> SETTINGS -> CONTROL (front end) and the solo watch's SIGHTLINE -> CONTROLS child. An INACTIVE store (trace replay, headless health, the input harness) reads 100 / 100.

**What changed**

`mouse_sens()` in src/platform/sl_input.c multiplies the developer base
(SL_MOUSE_SENS, default 6) by MOUSE SENSITIVITY / 100 and, while
`sl_game_scoped_zoom_active` holds (aim mode with an item carrying
WEAPONSTATBITFLAG_DISABLE_CROUCH - the sniper rifle, the camera; the same
predicate the wheel's ZOOM context uses, asked once per poll before
read_mouse and shared), by SCOPED SENSITIVITY / 100. The one factor feeds
both look axes at the two mouse lines in read_mouse (the fallback channel)
and the linear publish in sl_input_live_poll (the channel the game
consumes, bondview2.c). Invert Mouse Y stays the one sign point it was
(#39), applied before the factor and independent of it. Plain aiming
without an adjustable scope is not scoped (measured in the game: Q held
with weapon 5 -> `aim=1 ... scoped=0`).

**Why**

Intent (#50): normal and scoped look tunable from the menus, felt
at once, persisted, identical in both editors. The mouse is a native
producer with no cartridge behaviour to preserve; the only preservation
obligation is the accepted feel, which the default reproduces exactly.

**Why it is safe**

Default identity is asserted against a MEASUREMENT taken before the seam
existed: the real sl_input.c through the inputtest stubs at cc3a418a gave
dx 40 -> yaw 6.000000 deg (pitch 6.000000 for dy -40, 0.15 for one count,
scoped identical); the same program on the new tree prints a
byte-identical table (scratch qol11 witness-pre.txt / witness-post-
defaults.txt), and the mouse-sens inputtest case pins those numbers
(30 checks, 0 failed: 50 -> 3.0, 200 -> 12.0, base 200 / scoped 50 ->
12.0 play / 6.0 scope, 10 / 10 -> 0.6 / 0.06, malformed -> 6.0, invert ON
at 50 -> pitch -3.0, the watch pointer moves one pixel per count at 50 and
at 300 / 300, a pad stick deflection is the same N64 stick at 100 / 100 and
50 / 50). settingstest 111 / 0, displaytest 87 / 0, inputtest 225 with the
22 pre-existing B-096 failures unchanged, test.ps1 facility 300 PASS.
options.c / options.h (the watch rows) preprocess byte-identically to
cc3a418a under __sgi (md5 7bb753c2d90d / 89af0a83a899 both), non-vacuous
control, native arms +25 / +9 lines, zero #50 tokens. `make trace-verify`
NOT run (no MIPS toolchain; the store is inactive in every replay and the
__sgi arms are verbatim).

### D-008 — selectable 4:3 / 16:9 / 21:9 / 32:9 presentation and a FIELD OF VIEW setting (#45): the native renderer widens the 3D view horizontally (vertical composition kept) and, with the slider, zooms the world projection out about the centre; the 2D layer stays a centred 4:3 image at the window's height; the room traversal's draw set follows the wide view while the script and spawn tests keep the 4:3 one; the viewmodel keeps the 60-degree projection; default 4:3 / h16 91 = the accepted presentation, byte-identical

- **Date:**        2026-09-18
- **Phase:**       1
- **Commit:**      f73db2c2 (setting + sl_display helper), 057bf539 (renderer), b6b0c4cd (room traversal / 4:3 tests), 5881dc9a (DISPLAY tab); second round the same day: the window-width contract, 21:9, the FIELD OF VIEW row and cap, the viewmodel bracket (see the #45 backlog entries) - Gitea #45, branch sightline/qol-controls
- **Addendum (same day, the #45 contract):** the selected aspect is the SHAPE at the CURRENT HEIGHT: in windowed mode the window's width becomes height x aspect (960x720 -> 1280 / 1680 / 2560 x 720; sl_gfx_sdl.c sdl_apply_aspect, live and at the first frame; SL_WINDOW_SIZE names the initial window, the aspect wins on the width); the letterbox / pillarbox fit is for a framebuffer that cannot be resized only. 21:9 is id 3 (append-only: 2 = 32:9 was already in an existing user config). FIELD OF VIEW: `fov_vertical` (hundredths of a degree of the VERTICAL fov, default 6000 = 60.00 = FOV_Y_F exactly, range 3598..7756) displayed and stepped as the 16:9-equivalent horizontal h16 = 2 atan(tan(v/2) 16/9) in whole degrees 60..110 (default 91); applied by the renderer to the world projection only (named by fr.c and bondview2.c, native arms) as clip x,y scaled by s = tan(30)/tan(v_eff/2); total horizontal 2 atan(tan(v/2) ratio) capped at 140 degrees by reducing v_eff (the `sl_display:` line says `cap=engaged`); the crosshair's drawn position follows s about the view centre (gunfire.c gunDrawSight, native arm); the first-person weapon is bracketed by tagged no-ops ('SVM1'/'SVM0', bondview2.c) and keeps the 60-degree projection widened by the aspect only; the sky / sea polygons are drawn through the same zoom with their edge corners pushed back to the edges; the traversal root widens vertically by 1/s as well. The mouse look stays degrees per count (nothing in the game's look path reads the setting); the sniper zoom's own fovy-scaled rate is Rare's. Default: every scale 1.0, byte-identical.
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seams are the renderer's projection / viewport / 2D mapping and bg.c's traversal root)
- **First tick:**  n/a at 4:3 (no hashed state moves). At 16:9 / 32:9 the props digest (key 0xFFFE) differs from tick 0 of a Facility session: PROPFLAG_ONSCREEN on the props drawn in the bands - a render fact - nothing else
- **Fields:**      at 16:9 / 32:9: `prop->flags` bit PROPFLAG_ONSCREEN (0x02) for props in the bands; the room_rendered set (wider); the room apertures. NOT: any character entity (65 of 65 identical on 749 ticks), the player, prop positions / object state / door fractions (per-prop detail identical), the AI command list's on-screen tests, spawn placement, CHRFLAG_HAS_BEEN_ON_SCREEN, aim, auto-aim, spread, guard perception
- **Toggle:**      `aspect_ratio` in the native settings store (`%LOCALAPPDATA%\sightline\config.ini`), **default 0 = 4:3, the accepted presentation exactly as it stood before #45**; 1 = 16:9, 2 = 32:9, 3 = 21:9. Two views of the one state: OPTIONS -> SETTINGS -> DISPLAY on the front end and (2026-09-19, a later requirement) the solo watch's SIGHTLINE page -> DISPLAY child (ASPECT RATIO, FIELD OF VIEW). Inactive store (trace replay, headless health) and split-screen (two or more players) read 4:3.
- **Addendum (2026-09-19, watch structure):** the SIGHTLINE watch page (src/game/options.c, native arm only - the __sgi expansion of options.c / options.h is token-identical to 40fbbccb) is a table of VIEWS: the page itself lists GRAPHICS (dimmed, unselectable - nothing ships there yet), GAMEPLAY (SPRINT), DISPLAY (ASPECT RATIO, FIELD OF VIEW) and CONTROLS (INVERT MOUSE Y, BINDINGS - the #46 child moved under it); every child ends in BACK, Escape steps one level up (sl_game_watch_child_open / _back), the ring's L/R belong to the SIGHTLINE page only, and the #40 pointer hit-tests each child's rows by kind. No game-visible state is touched by the restructure: the rows write the same settings-store fields the front end writes.

**What changed**

The player picks a SHAPE. The renderer (src/gfx/sl_gfx_dl.c) fits that
shape inside the window (content rect; bars, never a stretch), fits the
logical 4:3 image inside it (safe rect) and takes k = content / safe (1,
4/3, 8/3). Every projection matrix the display list loads has its clip-space
x divided by k and the RSP viewport is widened by k about its centre, so the
4:3 image lands pixel-for-pixel on the safe rect and the bands to either
side receive the world the 4:3 frustum edge cut off: tan(hfov/2) =
tan(30 deg) * 4/3 * k, the vertical 60 degrees untouched (horizontal-plus).
The 2D layer (HUD, watch, front-end text, crosshair, overlays) maps logical
[0, w] onto the safe rect - centred, unstretched - and backdrops that span
the whole logical width (fades, the letterbox strips, the sky / sea band)
are extended to the content edges. The room traversal (src/game/bg.c)
starts from a root rectangle widened by the same band so the rooms behind
the bands are drawn; the AI command list's IF-I'M-ON-SCREEN /
IF-MY-ROOM-IS-ON-SCREEN / IF-ROOM-WITH-PAD-IS-ON-SCREEN, the spawn
placement test and CHRFLAG_HAS_BEEN_ON_SCREEN read the ORIGINAL 4:3
rectangle through sl_roomIsOnScreen43 / sl_propIsOnScreen43; the
consumers whose wrong answer the player would see (animation ticks for
characters in view, "magic" off-screen travel, slot recycling, scorch /
impact drawing, hit registration on a drawn character) follow the drawn
set.

**Why**

Requirement (#45): a selectable 4:3 / 16:9 / 32:9 with a genuinely
wider horizontal view, no stretch, the vertical composition kept, aspect
separate from resolution. The projection seam is the renderer's because the
game's projection (fr.c:709, fovy 60 over the 4:3 logical viewport) also
feeds aim (bondview.c c_scalex), auto-aim (chrprop.c screen bands), spread
(gunfire.c) and the frustum planes: widening the game's aspect would have
changed aim-mode angular speed and auto-aim acquisition by k. The traversal
root is the one game-side seam because a room not reached is not drawn at
all, and the visible bands showed the fog backdrop where rooms stand
(measured, Facility catwalk theta 240 / 285 / 300 at 32:9).

**Why it is safe**

At 4:3 (or the store inactive) all three rectangles are the window, k is
1, the band is 0 and every value is the original's: measured same-pose
frames (SL_VI_CATCHUP=0, frame 301) on the Facility catwalk, Dam room 121
and the Surface intro are byte-identical to captures from the pre-#45 exe
(sha256 EF09C9FB..., 4F8DE0D7..., 7EF5F4AD...). At 32:9 the per-tick
state traces (SL_TRACE_OUT) of the same Facility session at 4:3 and 32:9
agree on every character entity and the player on all 749 ticks; the
props digest differs only by the on-screen bit of props drawn in the
bands (per-prop detail at tick 200: 4 of 1050 lines, all `flags=04` vs
`flags=06`). The safe-rect crop of the 16:9 / 32:9 frame against the 4:3
frame differs by GL sub-pixel rasterisation only (interior: 3.5% of pixels
by <= 4/255, 100 px by 9-16, max 55 at 3 px; the rest in the outer 12
columns where the 4:3 frame's own 1-px scissor inset was). __sgi proof over
the seven src/game files: token-identical expansions, non-vacuous control,
zero #45 tokens. test.ps1 facility 300 PASS; settingstest 82/82;
displaytest 53/53; inputtest 225 checks with the 22 pre-existing B-096
failures unchanged. `make trace-verify` NOT run (no MIPS toolchain; the
__sgi arms are verbatim and the 4:3 native path is byte-identical).

### D-007 — MODERN presentation (#43 Phase A): every world texture minifies through a box-filtered mip pyramid with 16x anisotropic filtering; default OFF, opt-in through OPTIONS -> SETTINGS -> DISPLAY (native renderer only) — RETIRED / NOT SHIPPED 2026-09-18

- **Status:**      **RETIRED / NOT SHIPPED (2026-09-18).** Design decision: a
                   playtest comparison of ORIGINAL against MODERN on Dam found
                   the filtering-only MODERN not meaningfully distinguishable,
                   and a filtering-only Original / Modern toggle is not to
                   ship. Removed by forward commit 474f6691 on
                   sightline/qol-controls (the setting, the accessor and enum,
                   the DISPLAY tab and row, the cache key / activation branch /
                   census); src/gfx/sl_gfx_dl.c is byte-identical to its
                   pre-#43 content (5fce05ed) again, and the Facility same-pose
                   frame 601 on the retired build is byte-identical to the
                   pre-#43 baseline capture (sha256 2B45EE02..., 2073600
                   bytes). Gitea #43 stays OPEN as PARKED / DEFERRED until the
                   dedicated graphics phase, where Modern is to carry
                   substantial visible changes (textures, lighting). No pixel
                   divergence from this entry exists in the tree any more; the
                   record below is kept as written for what was measured.
- **Date:**        2026-09-18
- **Phase:**       1
- **Commit:**      defaaa42 (renderer seam), 1eda1849 (setting), b0aa67e7 (DISPLAY tab) - Gitea #43; retired by 474f6691
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seam is tex_acquire in src/gfx/sl_gfx_dl.c)
- **First tick:**  n/a - no hashed state moves; this is texture SAMPLING in the native renderer
- **Fields:**      none. Pixels only: under MODERN a world texture's GL object
                   carries GL_LINEAR_MIPMAP_LINEAR, GL_TEXTURE_MAX_ANISOTROPY
                   and a full pyramid; under ORIGINAL the pre-#43 GL_LINEAR
                   object, byte-identical frames (measured)
- **Toggle:**      `presentation_mode` in the native settings store
                   (`%LOCALAPPDATA%\sightline\config.ini`), **default 0 =
                   ORIGINAL, the accepted Sightline rendering exactly as it
                   stood before #43**; read by the renderer through
                   `sl_presentation_mode()` only. The front end's SETTINGS
                   page, DISPLAY tab, is its only view.

**What changed**

With the mode at MODERN, `tex_acquire` uploads every 3D world texture
(outside the two-tile water / mip-lerp family the B-116 enhancement also
excludes) with a complete box-filtered mip pyramid built from the image it
actually uploads as level 0 (the B-116 enhanced image or the decoded one),
trilinear minification and the largest anisotropy the context grants (16x on
the reference play machine's NVIDIA 4.6 context; absent = trilinear alone). S/T coordinates,
wrap modes, alpha, the combiner and the geometry are the same in both modes.
The mode is part of the texture cache key, so a change applies at each
texture's next resolve - the next frame - with no restart.

**Why**

Intent (#43): a player-selectable ORIGINAL / MODERN presentation. The
first MODERN enhancement had to be visible, renderer-side only, on the
existing assets, with no simulation change and no dependency: the bounded
recon found every texture minifying under plain GL_LINEAR at the window's
resolution (the RDP's own per-pixel LOD selection has no native counterpart
outside the B-119 far image), so oblique floors, distant gratings and walls
alias and sparkle. Proper minification is the one fixed renderer state that
delivers an honest delta today.

**Why it is safe**

`sl_presentation_mode()` answers ORIGINAL whenever the settings store is
inactive (trace replay and headless never initialise it), the key is
missing, malformed or out of range; under ORIGINAL `want_modern` is 0, the
filter / upload statements are the pre-#43 ones and the MODERN-path census
(resolves this frame / uploads over the run, on the `sl_dl` heartbeat line)
reads 0/0. Measured (scratch qol6, SL_VI_CATCHUP=0, same teleport pose and
frame): Facility catwalk frame 601 and Dam intro frame 901 from the pre-#43
binary (5fce05ed, exe sha256 0CB0F3BE...) and the post-#43 binary are
byte-identical under ORIGINAL (2073600 bytes each), and differ under MODERN
(24.95% / 9.91% of pixels). `test.ps1` facility 300 PASS; settingstest
50/50. No src/game file is touched. `make trace-verify` was NOT run (no MIPS
toolchain on this host; nothing in the harness's path changed).

### D-006 — native SPRINT (Left Shift, held): the keyboard's movement vector is scaled to the magnitude the cartridge's own 45-degree diagonal already reaches, never more; default OFF, opt-in through OPTIONS -> SETTINGS -> GAMEPLAY (live keyboard/mouse only)

- **Date:**        2026-09-18
- **Phase:**       1
- **Commit:**      (this change) - Gitea #42
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seam is in bondviewProcessInput)
- **First tick:**  n/a - no hashed state moves under replay or headless (below)
- **Fields:**      none under the harness. Under LIVE keyboard/mouse, with the
                   setting ON and Left Shift held: `speedforwards` and
                   `speedsideways` are scaled by one common factor after
                   :6063 (after the +/-1 clamps and 1.08 * speedboost)
- **Toggle:**      `sprint_enabled` in the native settings store
                   (`%LOCALAPPDATA%\sightline\config.ini`), **default 0 =
                   the cartridge's movement; Left Shift does nothing**. The
                   front end's SETTINGS page, GAMEPLAY tab, and (since
                   2026-09-18, #44) the solo watch's SIGHTLINE page are its two
                   views, both through sl_sprint_enabled / _set - one state.
                   Rule 5's shape exactly: an intentional behaviour change
                   behind a runtime toggle defaulting to original behaviour.

**What changed**

An eighth native action, SPRINT (`src/platform/sl_action.h`, a LEVEL bound
to Left Shift in the default table, published through the action channels
regardless of the setting), and one block in `bondviewProcessInput`
(`src/game/bondview2.c`, "NATIVE SPRINT", right after `speedforwards *=
speedboost`): with the key held, the setting ON, the keyboard channels applied
this tick, not aiming, standing (crouchpos == CROUCH_STAND) and the vector
non-zero, the pair (F, 1.08 * S) is scaled so its magnitude equals this
tick's `|(1.08 * speedboost, 1.08)|` - the magnitude W+D already produces -
and never beyond it (a request already at or above the cap is left alone, so
W+D+Shift equals W+D exactly). The 1.08 is MEASURED, not assumed: 7.79 world
units per tick per unit of `speedforwards` against 8.42 per unit of
`speedsideways` (ratio 1.081), i.e. Rare's own 1.08 at :6062 is what makes
the un-boosted forward speed equal the strafe speed. Straight-line Sprint is
therefore 11.90 units/tick un-boosted (= sqrt(2) * 8.41, the diagonal's) and
13.47 fully boosted (= |(10.52, 8.42)|, the boosted diagonal's).

**Why**

Design direction (#42): an optional Sprint on Left Shift for players who want
it, off by default, and explicitly NOT an emulation of the diagonal-running
trick - "its own, explicit speed change" whose hard upper bound is what the
game already allows to a player holding W+D.

**Why it is safe**

`sl_action_channels_get` returns 0 for every headless run and every recorded
replay, `sl_sprint_enabled` answers the table default 0 while the settings
store is inactive (replay and headless never initialise it), and the block is
further gated on `g_sl_channels_live`, so it is inert under the trace harness
and for a controller by construction. The `__sgi` arms of `bondview2.c`,
`front.c` and `bondconstants.h` preprocess byte-identically to master
(362607 b, 305088 b, 379103 b; positive controls differ; zero native tokens
in the `__sgi` expansions). Measured on Surface (280 ticks of held keys, eye
position per tick, scratch qol5): W 2443.9 units, W+D 3203.1, W+Shift 2785.9
over the first 240 ticks against W+D's 2789.0 (ratio 0.999, straight,
dz = 0.0), W+D+Shift 2789.0 = W+D exactly (every 40-tick segment identical);
with the setting OFF, W+Shift = W to the unit. `test.ps1` facility 300 PASS.
`make trace-verify` was NOT run (no MIPS toolchain on this host).

### D-005 — native ACTIONS: interact, reload, crouch, weapon previous/next and scope zoom reach the game as semantics from a keyboard and wheel, beside the N64 button path (live keyboard/mouse only)

- **Date:**        2026-09-17
- **Phase:**       1
- **Commit:**      (this change) - Gitea #38
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seams are in bondviewProcessInput and lvlRender)
- **First tick:**  n/a - no hashed state moves under replay or headless (below)
- **Fields:**      none under the harness. Under LIVE keyboard/mouse only:
                   moveData.btap / weaponBackOffset / weaponForwardOffset /
                   crouchDown / crouchUp / zoomIn,OutFovPersec, and the two
                   reload calls, are raised by native actions in addition to
                   the pad
- **Toggle:**      none needed by rule 3's own terms: the cartridge had no
                   keyboard, so there is no original keyboard behaviour being
                   overridden. The pad path is byte-for-byte untouched and
                   remains the original behaviour for a controller.

**What changed**

Seven native actions (`src/platform/sl_action.h`) - INTERACT, RELOAD, CROUCH,
WEAPON_PREVIOUS, WEAPON_NEXT, ZOOM_IN, ZOOM_OUT - bound by default to E, R,
Left Ctrl, 1, 2 and the mouse wheel, are published per poll through
`src/native/sl_action_channels.c` (held mask + accumulated edges + a zoom pulse
counted in game ticks) and consumed at two seams:

- `bondviewProcessInput` (`src/game/bondview2.c`, "NATIVE ACTIONS", after the
  movement seam and under the same gates): INTERACT sets `moveData.btap` like
  a B edge, so Rare's tank enter/exit runs verbatim, and marks the press
  interact-only; after Rare's btap block the mark takes `field_D0` and clears
  it. RELOAD raises a flag of its own. WEAPON_PREVIOUS/NEXT set
  `weaponBackOffset` / `weaponForwardOffset`. CROUCH (hold) sets `crouchDown`
  while held and `crouchUp` once released, both refused by the same
  `WEAPONSTATBITFLAG_DISABLE_CROUCH` test as :5375. ZOOM_IN/OUT set
  `zoomIn/OutFovPersec = 1.0f` for each tick of the pulse (2 ticks per
  notch, x1.21), under the same aiming + stat-bit test as :5359.
- `lvlRender` (`src/game/lv.c`, beside :756): the interact-only press calls
  `bond_interact_object()` and ignores its return (E in front of nothing does
  nothing); the reload press calls `attempt_reload_item_in_hand` for both
  hands without consulting `bond_interact_object` (R in front of a door leaves
  the door alone).

E no longer raises N64 B and R no longer raises N64 A; Space still raises B.
The wheel in play cycles weapons, or zooms while aiming with a weapon that
carries the 0x8000 stat bit (`sl_game_scoped_zoom_active`); in a menu it is
still the stick pulse.

**Why**

Design direction (#38): the cartridge folds use and reload into one contextual
button, puts crouch inside aim mode and reaches the previous weapon with a
two-button chord. A PC player expects E, R, Ctrl and a wheel that each do one
thing, and none of those can be expressed by synthesising N64 buttons without
also triggering the button's contextual behaviour - E would reload when
nothing was there, R would activate the door in front of Bond.

**Why it is safe**

`sl_action_channels_get` returns 0 for every headless run and every recorded
replay - the channels are published only by the live poll, gated on
`sl_live_input_active()` and on no menu being up, and no replay sidecar exists
for them - so both game blocks are inert under the trace harness by
construction. The `__sgi` arms of `bondview2.c` and `lv.c` preprocess
byte-identically to master (362607 b == 362607 b, 57879 b == 57879 b;
positive controls differ; zero `sl_action`/`sl_bond_pressed`/`SL_ACTCH`
tokens in the `__sgi` expansion). The pad does not come through the action
table (no gamepad rows by default), so a controller's B/A/X and aim + C-down
behave exactly as before. `test.ps1` facility 300 PASS; live evidence in
docs/backlog.md (2026-09-17, #38). `make trace-verify` was NOT run (no MIPS
toolchain on this host).

### D-004 — props are fully visible until the near-fog visibility cull; the translucent distance-fade band is removed (a presentation design decision)

- **Date:**        2026-09-15
- **Phase:**       1
- **Commit:**      (this change) - B-133, Gitea #26
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      every level whose fog row carries a near-fog record (Bunker 1/2,
                   Dam, Surface, Surface 2, ...); levels whose record is NULL
                   (Facility, measured) are untouched
- **First tick:**  n/a - no hashed state moves (below)
- **Fields:**      none hashed. The value feeds `objAlpha` / `chrfadealpha`, i.e.
                   the render path (`PropType` 5 vs 9, 8 vs 7), the DL's env
                   alpha and the draw-pass flags
- **Toggle:**      `SL_PROP_FADE`, **default 0 = the chosen presentation**;
                   `SL_PROP_FADE=1` restores Rare's translucent fade exactly.
                   This is the documented exception to rule 3: design decision
                   2026-09-15, quoted verbatim below.

**What changed**

`chrobjFogVisRangeRelated` (`src/game/propobj.c`, the function D-003 derives)
returns Rare's value `alpha` in [0, 1]. Natively, when `alpha > 0` and
`SL_PROP_FADE` is unset or 0, it returns 1.0 instead. The cull (`alpha == 0`
at `MaxVisRange <= t`) is unchanged, so a prop pops out at exactly the
distance it used to reach alpha 0; between there and the camera it is drawn
on the opaque path the game uses for near props (`chrobjRenderProp`
`PropType 9`: `G_CC_TRILERP`/`MODULATEIA2`, `OPA_SURF`; `chr.c` `PropType 7`)
instead of the translucent one (`PropType 5` / `8`: ENV-alpha combiner,
`XLU_SURF2`, alpha pass only). The `__sgi` arm is verbatim (preprocess proof:
382610 b == 382610 b, positive control differs; docs/backlog.md 2026-09-15).

**Why**

Ruling on the Bunker 2 playtest of 0f07002b, 2026-09-15: *"the level is
mostly good, but there are LOD issues on doors and props like crates that I
want resolved even if the ROM does the same thing."* The "LOD" is this fade:
on Bunker 2 (NearFog 1000, MaxVisRange 15000) the crates at 1000-1500 units
and the corridor doors at 1450 return 0.93..0.98 - invisible as a fade - but
any value below 1.0 switches the prop to the translucent path, on which every
authored vertex alpha becomes real translucency (the crates' plank strips
carry vertex alpha 0x7f and become 48% glass; the closed corridor door shows
the guard behind it; the main hall's mainframe column all but vanishes from
the balcony). The cartridge at mark 3's pose draws the same dark, faded crates
(romtele.py, stage 27) - it is Rare's look, and the ruling is against it.
The rule is derived, not tuned: the band contributes nothing but the switch,
so the switch is removed and the cull is kept; no constant, no per-prop,
per-level or resolution term. Scaling the band by output height / 240 was
considered and rejected - it moves the switch outward, the artefact remains.

**Why it is safe**

Nothing simulated reads the value: the two consumers' "not drawn at all"
branches (`objAlpha <= 0`, `chrfadealpha > 0`) see the same zeros as before,
so `time_other_players_on_screen` and the prop bookkeeping are unchanged;
`chr->fadealpha` (death/spawn fade) is still multiplied in. Measured on the
new build: the six playtest marks under the default vs `SL_PROP_FADE=1`
(scratch `bunker2\marks-fade-ab.png`); the mark 1 -> mark 2 forward push
(29 shots) shows no pop; Facility and Surface 1, intro + spawn view, 23
frames each under `SL_VI_CATCHUP=0`, pixel-identical between the two arms
(max diff 0); census clean; bunker2 / facility 400 PASS. `make trace-verify`
was NOT run (no MIPS toolchain on this host); the `#ifndef __sgi` guard plus
the byte-identity proof stands in for it, and the change is render-only.

### D-003 — the near-fog visibility triple can be divided by the level visibility scale, as its two siblings in the same record already are

- **Date:**        2026-09-08
- **Phase:**       1
- **Commit:**      (this change)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      DAM, SURFACE, SURFACE2 only, and only with the toggle armed
- **First tick:**  n/a while the toggle is off, which is the default
- **Fields:**      would be prop draw/cull state on those three levels
- **Toggle:**      `SL_VISRANGE_LEVELSCALE`, **default 0 = Rare's behaviour**

**What changed**

Nothing, unless `SL_VISRANGE_LEVELSCALE=1` is set. Armed, `fogLoadCurrentEnvironment`
(`src/game/bgfog.c`) hands out a COPY of the `NearFogRecord` triple with each of
`NearFog`, `MaxVisRange` and `MaxObfuscationRange` divided by
`bgGetLevelVisibilityScale()`, instead of a raw pointer into the `fog_tables[]`
row.

**The original calculation, derived rather than asserted**

`chrobjFogVisRangeRelated` (`src/game/propobj.c:13680`) is:

```
t     = lodscalez * ( 100*(zDepth - MaxObfuscationRange)/size + MaxObfuscationRange )
alpha = 0                                        when MaxVisRange <= t
      = (MaxVisRange - t)/(MaxVisRange - NearFog) when NearFog < t
      = 1                                         otherwise
objAlpha = (s32)(alpha * 255)
```

`size` is `getinstsize()` = the model header's `BoundingVolumeRadius * model->scale`,
and it is a DIVISOR, which is why small props vanish nearer than large ones.

Three independent authorities agree on that, and none of them is this repository
reading itself:

- the upstream matching decomp: `chrobjFogVisRangeRelated`,
  `fogLoadCurrentEnvironment`, `getinstsize`, `modelUpdateDistanceRelations`,
  `currentPlayerSetCameraScale` and `bondviewUpdateCameraMatrices` are all
  byte-identical between `D:\Projects` and this tree. `src/game/chr.c` and
  `src/game/matrixmath.c` are identical in full.
- the ROM disassembly, `GE Documentation/Background File Data/Fog Water and
  Sky/NTSC fog and sky.txt` line 440 onward (7F054B80) - same operand order,
  same three record offsets, same two comparisons.
- the playtest Dam capture, which reproduces it at 9 of 9 sample points, each
  within one alpha step of the logged value (the residual is the log printing
  zDepth to one decimal), with
  `NearFog=3333`, `MaxVisRange=4444`, `MaxObfuscationRange=600`,
  `lodscalez=1.0909`. Worked example: `size=170.4`, `zDepth=5844` gives
  `t = 1.0909*(100*5244/170.4 + 600) = 4011.8`, `alpha = (4444-4011.8)/1111 =
  0.389`, `objAlpha = 99` - the logged value exactly.

**Why the ROM behaves this way, and why it is Rare's and not ours**

The `Visibility` subrecord holds five distances. `fogLoadCurrentEnvironment`
divides two of them - `BlendMultiplier` and `FarFog` - by
`bgGetLevelVisibilityScale()`, which is what puts them in the same units as
`prop->zDepth`. The measured proof that `zDepth` lives in that divided domain is
in the same capture: `blend=25.0` and `farfog=75000.0` against table values 5 and
15000 and a scale of 0.2, and those two constants are compared directly against
`prop->zDepth` in `fogGetPropDistColor`.

The remaining three - the `NearFogRecord` triple - are handed out raw and then
compared against that same `prop->zDepth`. On the seventeen levels whose scale is
1.0 the asymmetry is invisible. On DAM, SURFACE and SURFACE2, which carry 0.2,
the three thresholds are effectively five times tighter than their siblings.

The ROM disassembly is explicit about this: at 7F0BA758 the visibility scale is
fetched once (7F0B4878, 800413FC) and applied by exactly two `DIV.S`, covering
pervasiveness and far fog; the near-fog record is stored by `ADDIU T7,A1,000C`,
a raw pointer into the table row.

So this is **not a port divergence**, and the round that went looking for one
found none. That is why the toggle exists and why it defaults to off: per
project rule 5 this is Rare's behaviour and the default arm must preserve it.

**Predicted effect when armed, on Dam**

Thresholds become `MaxObfuscationRange=3000`, `NearFog=16665`,
`MaxVisRange=22220`. Cull distance becomes `zDepth = 173.685*size + 3000`, fade
onset `zDepth = 122.764*size + 3000`:

The prop types in the playtest capture, with the two named witnesses first
(`type=` in `sl_table` is the `PROPDEF_TYPE` ordinal):

| prop                       | size  | fade onset, off | armed   | culled, off | armed   |
|----------------------------|-------|-----------------|---------|-------------|---------|
| PROPDEF_ALARM (5)          | 20.8  | 1110.7          | 5553.5  | 1322.5      | 6612.6  |
| PROPDEF_MULTI_MONITOR (11) | 33.8  | 1429.9          | 7149.4  | 1774.1      | 8870.6  |
| PROPDEF_PROP (3)           | 94.3  | 2915.3          | 14576.6 | 3875.7      | 19378.5 |
| PROPDEF_GLASS (42)         | 159.6 | 4518.6          | 22593.1 | 6144.0      | 30720.1 |
| PROPDEF_PROP (3)           | 170.4 | 4783.8          | 23918.9 | 6519.2      | 32595.9 |
| PROPDEF_DOOR (1)           | 178.2 | 4975.3          | 24876.5 | 6790.1      | 33950.7 |

The alarm panel is the smallest prop in the whole capture, so it is the first
thing on the level to vanish - which is exactly the symptom reported.

Both bands move by exactly 5.00x, which is 1/0.2 and nothing else - no constant
was fitted.

**Measured, after predicting**: booted headless on Dam (stage 33, 300 frames) in
both arms with `SL_TABLE_DBG=1`. The `sl_nfd` probe reports the three thresholds
the formula consumes:

```
off: NearFog=3333.0  MaxVisRange=4444.0  MaxObfusc=600.0   lodscalez=1.0909
on:  NearFog=16665.0 MaxVisRange=22220.0 MaxObfusc=3000.0  lodscalez=1.0909
```

All three moved by 5.00x and nothing else moved; `lodscalez` is unchanged, and it
is the authentic cartridge value (`tan(30 deg)/110` over `tan(30 deg)/120` =
120/110 = 1.0909, i.e. a 220-line screen at fovy 60). Both arms exited 0.

The static headless boot emits no `sl_table` lines in either arm, because the
camera never moves and no prop crosses a fade threshold from the spawn point. So
the band itself is measured at its inputs here and follows arithmetically;
a manual playtest is what exercises it end to end. The "off" column is the prediction the current build must already
satisfy, and it does: the playtest log shows the size-33.8 prop at alpha 254 at
zDepth 1432 (just past the 1429.9 fade onset) and alpha 0 at 1774 (just past the
1774.1 cull point).

**Scope - which reported symptoms this does and does not explain**

`chrobjFogVisRangeRelated` has exactly two call sites in the whole tree
(`grep -rn "chrobjFogVisRangeRelated(" src/`): `propobj.c:7618`, reached from
`chrobjRenderProp` for setup props, and `chr.c:2899` for characters. So:

- **Shares this cause:** the alarm panel (PROPDEF_ALARM, size 20.8) and the
  small monitor/switch props by the door (size 33.8). Both are in the capture,
  both reach `alpha 0`, both are explained to the alpha value by the formula.
- **Does NOT share it: the player's gun and the ground arrow.** Neither is a
  `PropRecord` passing through either call site. They are a separate rendering
  class and this change cannot move them. They stay open as the next visual
  defect rather than widening this one.
- **Does NOT share it: the missing tower geometry.** Two measurements, not
  arguments. First, the only mechanism that hides PART of a model by distance is
  `modelUpdateDistanceRelations`, and `SL_LOD_DBG` on the playtest run shows it
  healthy: 38518 VISIBLE against 38518 hidden, in exact near/far pairs sharing
  one switch point, with the far node's `MaxDistance * scale` at 3.2e7 - so no
  part is ever removed outright, only swapped for its other detail level.
  Second, the largest prop radius anywhere in the capture is 185.1, which is a
  glass pane; nothing tower-sized is a prop at all. Dam's towers are room
  geometry and travel the bg/portal path.

**Why it is safe**

Division by `1.0f` is exact in IEEE754, so the seventeen levels carrying scale
1.0 are bit-identical whether the toggle is armed or not. Only DAM, SURFACE and
SURFACE2 can move at all.

The copy is required rather than cosmetic: `g_NearFogValuesP` otherwise aims into
the `fog_tables[]` row itself, so dividing in place would corrupt the table for
the rest of the process. `VERSION_EU` already keeps a copy for its own reasons
(`eu_loadCurrentNearFog`), and the new block is guarded off on that version.

Every added token is inside `#if !defined(__sgi) && !defined(VERSION_EU)`. Proven
rather than asserted: `src/game/bgfog.c` preprocessed with `-E -P -D__sgi -x c`,
includes stripped, is **19507 bytes** before the change and **19507 bytes**
after, byte-identical (SHA-256 `1ef738c4...`), each containing 171 statements so
neither is vacuous; a positive control that appends one declaration outside every
guard preprocesses to **19544 bytes** and a different hash, so the comparison can
in fact fail. Without `-D__sgi` the same file is 23614 bytes and contains the new
block exactly once.

`make trace-verify` was NOT run - there is no MIPS toolchain on this machine, so
the matching build cannot be produced here. What stands in: the `__sgi`
byte-identity proof above, which is the stronger statement for a change of this
shape, since the matching build cannot contain a single token of it. The native
gates were run: `tools/windows/build.ps1` and `tools/windows/test.ps1` (facility,
300 frames, 1/1 PASS). **Facility is not coverage for this path** - its fog row is
`0/0/0`, so `fogLoadCurrentEnvironment` leaves `g_NearFogValuesP` NULL and the
mechanism is structurally unreachable there. A passing Facility gate is consistent
with the change, not evidence about it.

### D-002 — live keyboard/mouse look goes through analogTurn/analogPitch while aiming too, instead of becoming the N64 stick

- **Date:**        2026-09-03
- **Phase:**       1
- **Commit:**      (this change)
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      none affected in the trace corpus
- **First tick:**  n/a - cannot occur during replay
- **Fields:**      would be `player.vv_theta`, `player.vv_verta`,
                   `player.speedtheta`, `player.speedverta` if it could occur
- **Toggle:**      the producer itself. The seam is reached only where
                   `sl_move_channels_get()` returns non-zero, which is live
                   keyboard/mouse only: 0 for the gamepad, 0 for every headless
                   run and 0 for every recorded replay. A controller keeps
                   GoldenEye's original floating manual aim with no flag to
                   set, because the pad does not come through the channels at
                   all.

**What changed**

While `insightaimmode` was set, the platform layer used to stop publishing the
four movement channels and drive the N64 STICK with the mouse instead
(`map_kbm`'s aim branch, and `!aim` in the `ch_on` gate). The game seam in
`bondviewProcessInput` was gated on `insightaimmode == 0` to match.

Now `map_kbm` publishes `turn` and `pitch` in both modes, leaves `walk` and
`strafe` at 0 while aiming, and leaves the mouse-generated stick NEUTRAL. The
seam drops `insightaimmode` from its gate and instead applies only
`analogTurn` / `analogPitch` / `canNaturalTurn` / `canNaturalPitch` while
aiming; `analogWalk`, `analogStrafe`, `canLookAhead` and `canTurnTank` are
written only when aim mode is off.

Everything else aim mode owns is untouched: right-click still arrives as the
N64 aim button and `bondview2.c:5178` still sets `insightaimmode` from it,
auto-aim, zoom, weapon state and the sights are all unchanged, and the seam
never writes `insightaimmode`.

**Why**

Reported in playtest: right-click aiming is "effectively unusable", the crosshair
"jumps around", and it "behaves like a mouse being converted into digital/stick
input". That last phrase is the defect exactly.

GoldenEye's aim mode consumes stick POSITION. `bondview2.c:6295` offsets the
crosshair by `controlStickXRaw`/`controlStickYRaw`; `:5309` and `:5318` derive
`aimTurnLeftSpeed`/`aimTurnRightSpeed` only once `|stick_x|` passes 60, at a
rate set by how far past 60 it went. A mouse reports a RATE - one poll's
motion - so routing it onto that stick produced three separate defects at once:
the reticle floated instead of the view turning, the view did not turn at all
below a hard threshold, and turning stopped as a pulse the moment the mouse
did.

MEASURED with the shipped default `SL_MOUSE_SENS = 6`: full stick deflection
needs 13.33 pixels of motion in one poll (`SL_STICK_MAX / g_sens`), so ordinary
aiming motion sat pinned at the extreme while fine motion sat below the 60
threshold and moved only the crosshair. Lowering the sensitivity moves that
cliff, it does not remove it - the mismatch is the routing, not the number.

**Why it is safe**

`sl_move_channels_get()` returns 0 whenever there is no live keyboard and
mouse, which is every headless run and every recorded replay, so this block
cannot execute during trace verification at all and the trace corpus is
untouched.

Every changed token in `src/game/bondview2.c` is inside the existing
`#ifndef __sgi` region. MEASURED by a preprocessor-directive nesting scan of
the file: the changed range 5465-5541 reports a guard stack of
`['ifndef __sgi']` throughout, and the stack is empty again at 5545. IDO
therefore sees no change and the matching build is byte-identical.

The gamepad is unaffected by construction: `map_kbm` is the keyboard/mouse
mapping only, the pad goes through `map_pad` / `map_pad_dual`, and the channels
carry no pad input. GoldenEye's floating manual aim is still what a controller
gets.

`canNaturalTurn` and `canNaturalPitch` are reachable while aiming - MEASURED by
brace-nesting scan of `bondviewProcessInput`: `if (moveData.canNaturalPitch)`
at :6097 is enclosed only by the function, by
`if (watch_animation_state == WATCH_ANIMATION_0x0)` and by the `else` of
`if (docentreupdown)`, and `if (moveData.canNaturalTurn)` at :6155 is at
function top level. Neither has an aim gate. Both take the FIRST arm of their
if/else chain, ahead of `aimTurnLeft/RightSpeed` and `speedVertaUp/Down`, so
the stick-extreme derivations are not consulted - and they are 0 anyway,
because the keyboard/mouse stick is neutral.

Windows Facility headless acceptance passes with the change in:
`survived 300 pumped frames`, exit 0.

FINAL ACCEPTANCE IS MANUAL PLAY. Nothing here injects a physical mouse -
the before/after evidence is a data-path measurement of the real `map_kbm`,
not a claim about feel.

### D-001 — look-ahead auto-centring is off while live keyboard/mouse drives the pitch channel

- **Date:**        2026-09-01
- **Phase:**       1
- **Commit:**      (this change)
- **Re-baseline:** none required — see "Why it is safe"
- **Levels:**      none affected in the trace corpus
- **First tick:**  n/a — cannot occur during replay
- **Fields:**      would be `player.vv_verta`, `docentreupdown`,
                   `automovecentre` if it could occur
- **Toggle:**      `g_sl_channels_live` in `src/game/bondview2.c`, which is set
                   only where `sl_move_channels_get()` returns non-zero. That is
                   live keyboard/mouse only; it is 0 for the pad, for every
                   headless run and for every recorded replay. Original
                   behaviour is therefore the default everywhere the oracle
                   looks.

**What changed**

`bondviewProcessInput` decides "is the player pitching manually?" by testing
`moveData.speedVertaDown > 0 || moveData.speedVertaUp > 0` — the two DIGITAL
C-button pitch rates. When it concludes no, look-ahead auto-centring is allowed
to run, and a few lines later `docentreupdown` is re-armed on any walk past
±60 and `vv_verta` is dragged toward `targetPitch` (about −4°).

The native keyboard/mouse seam writes `analogPitch` and `canNaturalPitch` and
never touches `speedVerta*`, so for a mouse that test always answered "no".
The condition now also accepts `g_sl_channels_live`, so a mouse counts as
manual pitch input and auto-centring stands down.

**Why**

Reported in playtest: with mouse look, pitch "doesn't fully move up or down" and
"fights to move away from centre of screen". Both halves are this. Auto-centre
does not merely tug — the analog pitch branch is the `else` of
`if (docentreupdown)`, so while auto-centre is latched the mouse's pitch input
is discarded outright.

Measured, injecting a sustained pitch-up channel together with forward walk
(walk is what arms `automovecentre`), reading `vv_verta` directly:

| style | before | after |
|---|---|---|
| 1.1 Honey | 18.19° → −4.00° → 19.49°, never settles | 90.00° and holds |
| 1.2 Solitaire | 90.00° and holds | 90.00° and holds |

Solitaire was already masking the defect because it sets
`moveData.disableLookAhead = 1` at `bondview2.c:5404` and Honey does not. That
is why control style appeared to change mouse feel when the four-channel seam
is supposed to make style irrelevant for keyboard and mouse — it was this bug,
not the seam failing.

Auto-centring exists because a C-button pitch scheme cannot easily return the
view to level. A mouse can. Keeping it for a mouse is not fidelity, it is a
control fighting its user.

Deliberately NOT fixed by writing `speedVertaUp`/`speedVertaDown` from the pitch
channel: those are the digital pitch RATE and feed the same `vv_verta`
integration the analog path already drives, so pitch would be applied twice.

**Why it is safe**

`sl_move_channels_get()` returns 0 whenever there is no live keyboard and mouse,
which is every headless run and every recorded replay, so `g_sl_channels_live`
is 0 for the entire trace corpus and this block cannot execute during
verification. The pad does not come through the channels and is unaffected.
Every added token is inside `#ifndef __sgi`, so IDO sees none of it and the
matching build is byte-identical — confirmed by `make matching` printing MATCH!
and `make trace-verify LEVEL=facility` printing IDENTICAL with the change in.

---

## Mouse look is linear; the stick's squared curve is bypassed for the mouse

**Date** 2026-09-07 · **Toggle** `SL_MOUSE_LINEAR_LOOK` (default **ON**, set to
`0` to restore the previous routing) · **Affects** live keyboard/mouse only

**What changed**

`g_CurrentPlayer->speedtheta` (`bondview2.c`, the `canNaturalTurn` branch) and
`g_CurrentPlayer->speedverta` (the `canNaturalPitch` branch). When a live,
captured MOUSE owns look, both are now derived linearly from the frame's raw
mouse counts instead of from `analogTurn` / `analogPitch` through the
divide-by-70, clamp and signed-square curve. Everything else about both fields
is unchanged: the same variables, integrated by the same two statements, so gun
sway and every other consumer still reads a normal rate.

**Why**

Reported in playtest: "I've been moving my mouse in a 165 degree turn from left to
right and I expected it to land in the same spot each rotation. However, it
seems to either go further or shorter than the opposite movement before."

That is non-reciprocity, and the cause is the signed square. `y = sign(x)·x²`
makes the total turn depend on how a displacement was DISTRIBUTED ACROSS FRAMES
rather than on its sum, so the same physical sweep delivered a little faster
produces disproportionately more angle. Two further saturations compound it: the
platform layer clamps each poll to ±1 before the channels (±13.3 counts at the
default sensitivity) and the game clamps `analogTurn/70` to ±1 again.

Measured on the shipping arithmetic, degrees of yaw:

| case | before | after |
|---|---|---|
| +300 / −300 counts, both over 10 frames, ×10 | 0.00 | 0.00 |
| +300 over 5 frames / −300 over 20 frames, ×10 | **−525.00** | 0.00 |
| the reported 165-count sweep, 3 frames vs 12, ×10 | **−309.09** | 0.00 |
| 100 counts over 20 / 5 / 2 / 1 frames | 9.66 / 17.50 / 7.00 / 3.50 | 15.00 each |
| 400-count flick in one frame | **3.50** | 60.00 |

The first row is the important control: at EQUAL speeds the old path was already
reciprocal. The defect is specifically speed-dependent, which is exactly the
behaviour the report described. The last row is the same root cause seen from the
other end — a fast flick saturates and turns almost nowhere.

The curve is correct for a STICK, which reports a POSITION held over time. A
mouse reports a DISPLACEMENT that has already happened, and squaring a
displacement is not a sensitivity choice, it is an error.

**Why the default is ON**

The cartridge had no mouse. There is no original mouse behaviour being
overridden here — the stick's curve was only ever a stand-in for one, so this
does not override remembered game feel. The toggle exists to restore the old
routing for comparison, not because the old routing is the reference.

**Why it is safe**

The pad is untouched and keeps Rare's model entire — square curve, ramp,
ceiling. `sl_mouse_look_get()` returns 0 unless the platform layer armed the
channel, and it arms it only for live keyboard/mouse that is the owning
producer with the pointer actually captured, so the gamepad, every headless run
and every recorded replay all take the cartridge branch verbatim. Verified by a
gate test linking the real `sl_move_channels.c` (9/9).

Pitch is still clamped at ±90 on `vv_verta` itself, downstream of every
producer, so linearising the rate cannot look past vertical or wrap. Nothing is
banked or carried between frames: the counts apply in the frame they arrive.

Every added token is inside `#ifndef __sgi`. Proven rather than asserted: the
preprocessed output of `bondview2.c` under `-D__sgi` is byte-identical at HEAD
and with the change in (SHA-256 `75b30f77…`), and a positive control that
alters one token inside the cartridge path does make that comparison fail.

`make trace-verify` was NOT run for this change — there is no MIPS toolchain on
this machine. The `#ifndef __sgi` guard plus the byte-identity proof above is
what stands in for it, and the trace corpus cannot reach this code in any case
because the channel is never armed without live input.
