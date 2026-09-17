# Live owner bug mark (F8)

**Status:** shipped 2026-09-08, on `sightline/full-sp-native`.
**Hardened 2026-09-08** (v2) — stale telemetry removed, room semantics made
truthful, centre pixel and depth recorded, source provenance added.

## The problem it replaces

Diagnosis of rendering defects kept routing through the same loop: the owner
plays, sees something wrong, and then somebody tries to reproduce it. F9 was
built for that loop — it writes `mark <n> read <index>` into the run directory
so a replay can jump straight to the moment.

The loop does not close, because the replay is not the run. Measured on this
branch: a native recording and its replay agree for roughly the first 1400
samples, are 202 world units apart by sample 4201, and reach 26,464 units apart
at worst. A mark that names only a MOMENT therefore points into a playthrough
that never happened. Five rounds went into making replay faithful and produced
no progress on the actual rendering defects; that work is now parked as
tooling/evidence debt (Gitea #7).

F8 removes the replay from the path entirely. It captures the answer in the
live run, at the instant the owner is looking at the defect.

## What it does

Press F8 while playing. Into the run's own directory:

```
<run>/mark-NNN.txt    the record, in stable sections
<run>/mark-NNN.bmp    the framebuffer of THAT SAME FRAME
```

No environment variable, no replay, no controller, no command prompt during
play. `SL_MARK_BOX` optionally resizes the centre box; the default is what
ships and what the acceptance runs used.

Sections, in order:

```
[mark]            number, run dir, screenshot, pumped frame, VI/record index
[camera]          eye position, basis, matrices, look angles, render origin
[projection]      the game's near/far/fov/aspect and its framebuffer/view
[rooms]           four separate room facts - see below
[centre-pixel]    what the marked framebuffer actually holds at its centre
[renderer]        framebuffer, viewports, scissor, projection matrix, counts
[candidate-draws] every draw whose projection meets the centre box
```

## v2: what was wrong, and what replaced it

Three fields were carried by v1 that were measured to be untrue. None was
documented around; each was corrected or removed.

### `camera-world-pos` — REMOVED

It printed `pl->pos`, whose own declaration (`bondview.h:305`) is a guess:
*"canonical memcampos ?"*. Measured across three marks in one run it was
byte-identical — `16011.993 613.779 19719.410` every time — while the player
plainly moved and `viewtoworldmtxf`'s translation row tracked the movement
correctly on each mark.

Replaced by **`eye-world-pos`**, read from `viewtoworldmtxf` `m[3]`, and the
record now **names that matrix as the single authority**. That it is in world
units is established rather than assumed: `bondview.c:967-1002` builds every
frustum plane offset as a dot product of a basis row with `m[3]`, and
`bondview.c:1092` tests that offset against prop **world** positions — a plane
test only closes if both operands are in the same space.

There is deliberately no second position field left to disagree with it. A
confidently-named stale value is worse than no value.

### `player-model-pos` — RENAMED to `render-origin`

It was never the player. `bondview2.c:8380` assigns `current_model_pos` from
`getRoomPositionScaledByIndex(room)` — the scaled **origin of the player's
current room** — and `bgroomtrans.c:216` subtracts it from each room's own
scaled origin to build that room's transform. It is the offset world geometry
is drawn relative to. The measurement agreed: it was exactly
`current_room_pos * 4.28008`, which is the scale factor, not a coincidence.

### `visibility-packet` — RELABELLED, and no longer leads the section

`bgCopyVisibleRoomsToList` reads the *current* visibility packet
(`bg.c:4569`), which the portal traversal fills and then leaves at whatever the
last packet produced. A mark taken at a frame boundary routinely finds it
empty: it read **0 on a frame with six rooms loaded and four drawing at the
centre of the screen**. It is now `visibility-snapshot`, captioned as a
frame-boundary snapshot that is routinely empty, with that measurement quoted
inline so a 0 cannot be read as "no room was visible".

## Four room facts, never one number

The failure above was one number silently standing for four different things.
The record now separates them and never lets "0 rooms" mean all of them:

| fact | where it comes from |
|---|---|
| **visibility decision** | `visibility-snapshot` — the packet, explicitly caveated |
| **requested for render** | `requested-primary` / `requested-secondary`, from `dword_CODE_bss_8007FFA0[0..g_BgNumberOfRoomsDrawn)`; both `bg.c` passes walk the same array, so there is one requested set, not two |
| **actually submitted** | `submitted-lists` — every depth-1 `G_DL` branch the interpreter was **handed**, recorded at the branch |
| **actually drawn** | the `tris=` count on each submitted list; `tris=0` prints `[DREW NOTHING]` |

A list that was submitted and emitted nothing is now a readable outcome rather
than an absence — which is exactly what a room disappearing looks like from the
renderer's side.

Measured on a Dam frame: 68 depth-1 branches submitted, 64 drew, 4 executed and
emitted nothing; rooms 132, 133, 134, 124, 125 and 135 all resolved.

## `[centre-pixel]`: what the pixel IS, not only what could have painted it

A candidate list cannot separate these four, and they are different diagnoses:

```
sky / clear colour at far depth     nothing was drawn here
opaque world colour at mid depth    geometry drew, and drew wrong
blended colour at mid depth         an effect or alpha surface is on top
a depth step across the 5x5         the centre is on an edge or a seam
```

So the record reads the centre pixel of the marked framebuffer — RGBA, window
depth, the eye-space distance that depth converts to through the projection
that was in force — plus a bounded 5×5 neighbourhood of colours and depths and
its depth spread. Bounded on purpose: a framebuffer dump is not evidence,
because nobody reads it.

`glReadPixels` and `glGetIntegerv` read; they do not draw. The probe runs after
the walk has finished and before the swap, so nothing it does can reach the
image it is reading.

## The load-bearing part: source provenance, not just "which list"

The seam investigation got as far as `tri=50 dl=20026730` and then spent hours
by hand turning that into *which command in that list, which `G_VTX` load,
which point-table entries, which source coordinates*. v2 makes that step free.

Every candidate draw now carries:

- the depth-1 list **and the byte offset of the triangle command within it**
- the innermost list and offset, nesting depth, branch ordinal
- per vertex: the RSP slot, the `G_VTX` segment operand **and the index within
  that load**, the offset of the `G_VTX` command in its list, the host address,
  and the source coordinates, `s`/`t` and RGBA as interpreted
- per vertex: eye space, clip space (with `w`), NDC, and projected window pixels
- material: combiner words, cycle, geometry mode, `om_l`, fog class
- depth test, depth write, z-offset mode, blend enable and both factors
- cull state, scissor rectangle, viewport
- texture dimensions, tile, format, cache slot, GL name and source address

This is **passive bookkeeping**: one pointer store per display-list command,
five stores per loaded vertex. No vertex datum is altered and the display list
executes identically whether or not the record is kept.

It works. On a live Dam mark, a room-135 candidate resolved to
`src=0e003120+10` with source coordinates `391 -189 87` — the exact T-junction
endpoint the seam round had to dig out by hand.

## Classification: confident answers only

A depth-1 list that IS a room's primary or secondary geometry is named as such,
because the resolver reads `g_BgRoomInfo` and that is a lookup, not an
inference. Everything else is `class=unknown`, with the segment number recorded
so a reader can look it up.

No object-name resolver was built and none is wanted. Naming a segment-5 list
"a character or a prop" would be an inference dressed as a reading, and this
record has already been wrong once that way.

The 2D and HUD passes go through the texture-rectangle path and never reach
`emit_tri`, so they cannot appear as candidates at all. A centre painted by one
of those reads as "no candidate draws" plus a `[centre-pixel]` colour, which is
the honest shape of that answer.

## Centre candidates are a FILTER

A triangle is listed when the projected bounding box over its in-front vertices
meets the centre box. That over-reports: a large triangle straddling the centre
is listed even when the centre pixel belongs to something else, and source
vertices may sit far outside the viewport. Over-reporting is the safe
direction — the alternative is a mark that silently omits the draw being
hunted. The header says so where it will be read, and a nearest-first ranking by
eye distance is printed so the reader knows where to look.

Deliberately NOT built: clipping-aware CPU rasterisation, exact GPU fragment
attribution, RenderDoc-style capture, framebuffer command replay.

## It must not alter the rendering under test

`SL_TRI_ID` and `SL_GLASS_MARK` answer "which draw covers this pixel" by
replacing the draw's colour or dropping its texture — and `tex_apply`'s own
comment records the resulting error: a TRI_ID capture was read as evidence that
a quad covered a region, when disabling texturing is what made the quad visible.

**Static proof, re-run 2026-09-08 over the whole v2 feature** (`mark_note_tri`,
`dl_off_at`, `mark_class`, `sl_mark_render`, `sl_mark_depth_to_eye`,
`sl_mark_game_render`, `sl_mark_player_room_safe`, `sl_run_mark_full`,
`sl_mark_centre_probe`): **zero render-state-writing GL calls**. The only GL in
the entire path is inside the centre probe and is `glGetError`, `glPixelStorei`
(the client-side pack alignment, which affects read-back only) and
`glReadPixels` — all reads, all after the walk.

Empirically: across 27 live marks the centre RGB in the text matched the centre
pixel of that mark's own BMP **exactly, 27 times out of 27**, verified by an
independent BMP parser that does not share code with the engine.

## No continuous cost

The always-on bookkeeping was A/B'd against the pre-change build, windowed Dam,
900 pumped frames, three runs each:

```
baseline (56cb4092)   16.51 s avg   54.5 fps
with provenance       16.20 s avg   55.6 fps
```

The new build measured marginally faster, i.e. the difference is inside
run-to-run noise — both series contain a 15.18 s run. The several-hundred-ms
census hitch fixed in `83e472ea` is not reintroduced; nothing per-frame was
added.

## A crash was found and fixed

Pressing F8 on the boot or title screens took an **ACCESS VIOLATION reading
0x00000003**, resolved to `bondviewGetCurrentPlayersRoom+0x34` called from
`sl_mark_game_render`. The accessor (`bondview2.c:9800`) reads
`field_488.current_tile_ptr_for_portals->room` with no null check, and outside a
level that pointer is null — `0x3` is `room`'s offset within `StandTile`.
`g_CurrentPlayer` is non-null there, so the existing `pl == NULL` guard never
fired. The mark wrote its `.bmp` and then died before its `.txt`.

This is pre-existing: v1 called the same accessor the same way. The game
function is **not touched** (rule 5) — every in-level caller satisfies its
preconditions and faulting there is Rare's behaviour. The guard belongs to the
reader, so `sl_mark_player_room_safe` restates the accessor's own two branches
and checks the pointer each is about to follow, printing `UNKNOWN` when there
is no tile to ask. Ten front-end marks now write cleanly with an empty
`crash.txt`.

## Same-frame correspondence is structural

`sl_gfx_frame_dl` arms the capture at the top of one display list and sets
"ready" when that walk ends. `sl_run_mark_full` is called from `sdl_end`
*before* `SDL_GL_SwapWindow`, so the buffer `glReadPixels` returns is the one
that walk just filled. The B-101 witness re-walk is excluded by construction —
it is the same frame twice, and arming on it would pair pass two's triangles
with pass one's screenshot.

## Verification (2026-09-08, v2)

27 live marks across five windowed runs, F8 delivered as real
`WM_KEYDOWN`/`WM_KEYUP`.

| | in-level marks | background marks |
|---|---|---|
| eye position | moved between marks, matrix and translation row agreeing | — |
| `basis-row-dots` | 1.000000 1.000000 1.000000 | — |
| centre depth | 0.730723 … 0.996300 (eye 18.55 … 675.59) | 1.000000000 |
| far-plane note | not printed | printed |
| candidate draws | 20 … 400 at centre, with full provenance | 0, and said so in as many words |
| rooms | 68 submitted / 64 drew / 4 empty | player-room `UNKNOWN, tile pointer is NULL` |
| centre RGB vs BMP | match | match |

Two marks resolving to different geometry gave genuinely different provenance:
top-level lists `05006050` vs `05001a00`/`05000520`, vertex loads
`050075b8 @+0x00e8` vs `05001a00 @+0x00a8`, rooms 132 vs 135.

Both background branches fired live and are distinguishable from an
opaque-world centre: depth exactly 1.0 with the far-plane note and zero
candidates, against depth 0.73–0.996 with candidates carrying provenance. One
background mark reported "NO TRIANGLES WERE EMITTED AT ALL" and three reported
"triangles emitted, none at the centre" — the two negative results stay
distinct.

## A trap found on the way, worth recording

`include/math.h` declares `sqrtf(float)` to GCC but **not** `sqrt(double)` —
`gcc -E -P -Iinclude` over a file calling both emits the `sqrtf` prototype and
no `sqrt` prototype. So `sqrt()` in any translation unit built without `__sgi`
is an implicit declaration returning `int`, and the value it yields is nonsense:
the first version of the basis check printed `30680096.000000` for a row whose
three unit-length components were on the line above it.

A tree-wide `grep -rn "[^a-zA-Z_]sqrt(" src/ --include=*.c --include=*.h` finds
exactly one other occurrence and it is inside a comment, so nothing that shipped
was affected — but the next caller would have been. The check now uses the dot
product, which needs no library call and is exact.

## Deliberately not built

Frame capture and replay, GPU command serialisation, a GUI, an editor, a large
trace format, per-pixel provenance, an asset database for friendly names, a
visibility debugger. Addresses, indices and source offsets are enough.
