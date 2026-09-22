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

### D-019 — WORLD DETAIL ENHANCED (#43): the near-fog visibility-range rejection of props and characters is not applied at the render seam, and a prop the TICK's copy of the same test rejected is drawn without its on-screen flag (the render-only lift), so a prop stays eligible until the far-fog cull or the room / portal set removes it; default ORIGINAL = the accepted rendering byte for byte (native-only setting, the D-009 class; render only, the simulation's own copy of the same test is untouched)

- **Date:**        2026-09-21 (the lift added the same day, on the owner's Dam yard marks)
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

THE LIFT (the owner's Dam yard marks, 2026-09-21: "the boxes still stream
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

The owner's #43 contract: an optional profile in which "props not popping
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
(B-090 had already matched the disassembly and the owner's log at 9 of 9
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
THE OWNER MUST WEIGH: a lifted prop is visible but, until it comes inside
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
hall, the owner's Dam mark 1), key absent and key 0, SL_VI_CATCHUP=0 -
byte-identical frames (sha256 7CC42D4F..., 4357AB44..., C5D30773...,
FB0AFCAF...) and identical DL census. Positive controls: ENHANCED at the
Dam witness pose lifts 4 evaluations (the two props plus two guards), frame
121 tris 1786 -> 1828, and the frame differs in exactly 78 pixels - the
alarm panel; at the owner's mark 1 (Dam 15227 / 60.3 / 13970.6 theta 212.4,
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

### D-018 — the modern pad's DEFAULT bumpers MIRROR the triggers (owner, 2026-09-21): RB / R1 is a second FIRE and LB / L1 a second AIM in every live preset, the weapon cycle and the zoom are the d-pad's alone, and the BUMPER preset is retired; the scope-aware source mechanism is unchanged (native-only setting, the D-009 class - but the DEFAULT itself moves, deliberately)

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

Owner, 2026-09-21: "I also don't like RB and LB and L1 and R1 cycling the
weapons. They should just mirror the triggers for aim and shoot. Dpad
handles weapons fine." A control-feel decision, made by the owner, on a
native-only control layer the cartridge does not have.

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

Owner-observed on the v0.2.0 candidate: at any aspect wider than 4:3 the
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

Owner-observed: at a large resolution or in exclusive fullscreen there was
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

Owner intent (#52): the player chooses the window mode, size and VSync
from the menus and has it persist, without touching a script; the window
contract of #45 holds in every mode (the aspect is the shape at the
current height; a fixed framebuffer FITS - pillarbox or letterbox, never a
stretch); accepted current behaviour (windowed, the launcher's size, VSync
off) is the default and a config without these rows behaves as today.

**Why it is safe**

Default identity is asserted against a MEASUREMENT taken before the first
edit: the 230b4b52 exe on the owner-shaped config through play.ps1's
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

Owner intent (#56): Crouch and Sprint each get a HOLD / TOGGLE choice,
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

Owner intent (#51, unblocked by #63's acceptance): the controller's look
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
- **Toggle:**      **none** - an owner request on the modern pad (D-011's only pad), not a change to Rare's code: the game still reads the N64 pad exactly as it did; what the native layer puts on it changed. A player who wants the old d-pad behaviour rebinds: the four directions are ordinary registry sources (pad:DPAD_UP etc.), and an unbound direction does nothing in play. The bumpers' rule is the registry's `ctx` flag on LB / RB (sl_bindings.c g_pad), the same flag the wheel carries; BUTTON LAYOUT BUMPER puts AIM / FIRE on them, which is a plain level (the rule gates only weapon-cycle and zoom rows). The compiled DEFAULT preset changed: RB next weapon + zoom in, LB previous + zoom out, the d-pad as above, Y and R3 unbound (two slots per action); GREEN THUMB's B is CROUCH again.

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
per press and read to the owner as doing nothing.

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

The owner's replay with a real Xbox pad and a DualSense (2026-09-20):
"look and zoom in aren't mapped correctly. D pad isn't used at all, and it
should be, and zoom in is actually look. To zoom in, we should probably use
lb and rb. So it zooms when scoping for sniper rifle, and cycles weapons
when not."

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
- **Toggle:**      **none for the removal** - an owner decision, not a rule-5 toggle ("I don't really see a point to hide that behind a profile. Sightline will never get a n64 controller hooked up and we aren't using emulation"). The __sgi build keeps all eight styles and the N64 page verbatim. The two settings that replace the styles: `pad_stick_layout` (0 DEFAULT / 1 SOUTHPAW / 2 LEGACY / 3 LEGACY SOUTHPAW, default DEFAULT = D-010's routing) and `pad_button_layout` (0 DEFAULT / 1 SOUTHPAW / 2 BUMPER / 3 GREEN THUMB / 4 CUSTOM, default DEFAULT = the compiled pad table). Views: the watch's Control Options page (its two rows), the SIGHTLINE -> CONTROLS child, OPTIONS -> SETTINGS -> PAD.

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

Owner decision 2026-09-20 (#63, after the round-3 replay with a real Xbox
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
retired keys, no pad. settingstest 118 / 0 (section 6: an owner-shaped
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

Owner direction (#63): a modern controller plays as a modern dual-stick
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

Owner intent (#50): normal and scoped look tunable from the menus, felt
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
- **Addendum (same day, owner contract):** the selected aspect is the SHAPE at the CURRENT HEIGHT: in windowed mode the window's width becomes height x aspect (960x720 -> 1280 / 1680 / 2560 x 720; sl_gfx_sdl.c sdl_apply_aspect, live and at the first frame; SL_WINDOW_SIZE names the initial window, the aspect wins on the width); the letterbox / pillarbox fit is for a framebuffer that cannot be resized only. 21:9 is id 3 (append-only: 2 = 32:9 was already in the owner's config). FIELD OF VIEW: `fov_vertical` (hundredths of a degree of the VERTICAL fov, default 6000 = 60.00 = FOV_Y_F exactly, range 3598..7756) displayed and stepped as the 16:9-equivalent horizontal h16 = 2 atan(tan(v/2) 16/9) in whole degrees 60..110 (default 91); applied by the renderer to the world projection only (named by fr.c and bondview2.c, native arms) as clip x,y scaled by s = tan(30)/tan(v_eff/2); total horizontal 2 atan(tan(v/2) ratio) capped at 140 degrees by reducing v_eff (the `sl_display:` line says `cap=engaged`); the crosshair's drawn position follows s about the view centre (gunfire.c gunDrawSight, native arm); the first-person weapon is bracketed by tagged no-ops ('SVM1'/'SVM0', bondview2.c) and keeps the 60-degree projection widened by the aspect only; the sky / sea polygons are drawn through the same zoom with their edge corners pushed back to the edges; the traversal root widens vertically by 1/s as well. The mouse look stays degrees per count (nothing in the game's look path reads the setting); the sniper zoom's own fovy-scaled rate is Rare's. Default: every scale 1.0, byte-identical.
- **Re-baseline:** none required - see "Why it is safe"
- **Levels:**      all (the seams are the renderer's projection / viewport / 2D mapping and bg.c's traversal root)
- **First tick:**  n/a at 4:3 (no hashed state moves). At 16:9 / 32:9 the props digest (key 0xFFFE) differs from tick 0 of a Facility session: PROPFLAG_ONSCREEN on the props drawn in the bands - a render fact - nothing else
- **Fields:**      at 16:9 / 32:9: `prop->flags` bit PROPFLAG_ONSCREEN (0x02) for props in the bands; the room_rendered set (wider); the room apertures. NOT: any character entity (65 of 65 identical on 749 ticks), the player, prop positions / object state / door fractions (per-prop detail identical), the AI command list's on-screen tests, spawn placement, CHRFLAG_HAS_BEEN_ON_SCREEN, aim, auto-aim, spread, guard perception
- **Toggle:**      `aspect_ratio` in the native settings store (`%LOCALAPPDATA%\sightline\config.ini`), **default 0 = 4:3, the accepted presentation exactly as it stood before #45**; 1 = 16:9, 2 = 32:9, 3 = 21:9. Two views of the one state: OPTIONS -> SETTINGS -> DISPLAY on the front end and (2026-09-19, owner request) the solo watch's SIGHTLINE page -> DISPLAY child (ASPECT RATIO, FIELD OF VIEW). Inactive store (trace replay, headless health) and split-screen (two or more players) read 4:3.
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

Owner requirement (#45): a selectable 4:3 / 16:9 / 32:9 with a genuinely
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

- **Status:**      **RETIRED / NOT SHIPPED (2026-09-18).** Owner decision: the
                   owner compared ORIGINAL against MODERN on Dam and found the
                   filtering-only MODERN not meaningfully distinguishable, and
                   does not want to ship a filtering-only Original / Modern
                   toggle. Removed by forward commit 474f6691 on
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
the owner's NVIDIA 4.6 context; absent = trilinear alone). S/T coordinates,
wrap modes, alpha, the combiner and the geometry are the same in both modes.
The mode is part of the texture cache key, so a change applies at each
texture's next resolve - the next frame - with no restart.

**Why**

Owner intent (#43): a player-selectable ORIGINAL / MODERN presentation. The
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

Owner direction (#42): an optional Sprint on Left Shift for players who want
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

Owner direction (#38): the cartridge folds use and reload into one contextual
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

### D-004 — props are fully visible until the near-fog visibility cull; the translucent distance-fade band is removed (owner-requested presentation)

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
- **Toggle:**      `SL_PROP_FADE`, **default 0 = the owner's presentation**;
                   `SL_PROP_FADE=1` restores Rare's translucent fade exactly.
                   This is the documented exception to rule 3: owner decision
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

Owner ruling on the Bunker 2 replay of 0f07002b, 2026-09-15: *"the level is
mostly good, but there are LOD issues on doors and props like crates that I
want resolved even if the ROM does the same thing."* The "LOD" is this fade:
on Bunker 2 (NearFog 1000, MaxVisRange 15000) the crates at 1000-1500 units
and the corridor doors at 1450 return 0.93..0.98 - invisible as a fade - but
any value below 1.0 switches the prop to the translucent path, on which every
authored vertex alpha becomes real translucency (the crates' plank strips
carry vertex alpha 0x7f and become 48% glass; the closed corridor door shows
the guard behind it; the main hall's mainframe column all but vanishes from
the balcony). The cartridge at mark 3's pose draws the same dark, faded crates
(romtele.py, stage 27) - it is Rare's look, and the owner ruled against it.
The rule is derived, not tuned: the band contributes nothing but the switch,
so the switch is removed and the cull is kept; no constant, no per-prop,
per-level or resolution term. Scaling the band by output height / 240 was
considered and rejected - it moves the switch outward, the artefact remains.

**Why it is safe**

Nothing simulated reads the value: the two consumers' "not drawn at all"
branches (`objAlpha <= 0`, `chrfadealpha > 0`) see the same zeros as before,
so `time_other_players_on_screen` and the prop bookkeeping are unchanged;
`chr->fadealpha` (death/spawn fade) is still multiplied in. Measured on the
new build: the six owner marks under the default vs `SL_PROP_FADE=1`
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
- the owner's Dam capture, which reproduces it at 9 of 9 sample points, each
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

The prop types in the owner's capture, with the two named witnesses first
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
the band itself is measured at its inputs here and follows arithmetically; the
owner's replay is what exercises it end to end. The "off" column is the prediction the current build must already
satisfy, and it does: the owner's log shows the size-33.8 prop at alpha 254 at
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
  `modelUpdateDistanceRelations`, and `SL_LOD_DBG` on the owner's run shows it
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

Owner-reported: right-click aiming is "effectively unusable", the crosshair
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

FINAL ACCEPTANCE IS THE OWNER'S HANDS. Nothing here injects a physical mouse -
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

Owner-reported: with mouse look, pitch "doesn't fully move up or down" and
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

Owner-reported: "I've been moving my mouse in a 165 degree turn from left to
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
| owner's 165-count sweep, 3 frames vs 12, ×10 | **−309.09** | 0.00 |
| 100 counts over 20 / 5 / 2 / 1 frames | 9.66 / 17.50 / 7.00 / 3.50 | 15.00 each |
| 400-count flick in one frame | **3.50** | 60.00 |

The first row is the important control: at EQUAL speeds the old path was already
reciprocal. The defect is specifically speed-dependent, which is exactly the
behaviour the owner described. The last row is the same root cause seen from the
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
