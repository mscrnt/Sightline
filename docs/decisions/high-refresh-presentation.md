# High-refresh presentation: extra frames between unchanged simulation frames

**Decided:** 2026-10-07, round hr1 of the high-refresh rendering foundation (#87).
**Status:** in force for the native build, behind `render_fps` (default 0 = off).
No effect on the matching build, on headless runs or on trace replay.

## The decision

The native build may present frames between simulation frames. The
simulation is not touched: the VI cadence that drives it (one retrace per
paced period, the catch-up when a frame runs late, the 0.25 s resync), the
game step (`waitForNextFrame` -> `speedgraphframes` -> `g_ClockTimer`, the
VIs a frame spanned), every timer, the AI, the audio manager and the input
stream advance exactly as they do without it. An extra frame is the frame's
own, already-built display list interpreted a second time with the camera
blended between the previous simulation frame's and this one's.

This is opened as a sprint ahead of ROADMAP Phase 2 ("Decouple sim from
render") and differs from that phase on purpose. Phase 2 converts the game to
a fixed 60 Hz tick and ends bit-exact parity. This work keeps the variable VI
step and keeps parity: its acceptance gate is the per-tick trace staying
byte-identical at every render rate, VSync setting and cap.

## How it is safe

- **Lifetime.** The extra frames are made inside the frame pump
  (`src/platform/sl_ultra_shim.c`, `sl_pres_between`) after the frame's
  retrace and before its pacing sleep. The game thread is blocked in that
  pump - the build is single-threaded - so the display list, the dynamic
  matrices and vertices, the texture pool and the room data are exactly what
  the original walk read. No copy, no new thread.
- **No game code runs.** The re-walk (`src/gfx/sl_gfx_dl.c`,
  `sl_gfx_dl_rewalk`) re-enters the interpreter under a presentation-pass
  flag, on the B-101 witness re-walk's contract: no frame counter, dither
  seed, mark, census or witness advances, and the task state the walk does
  not reset (viewport, framebuffer size) is handed in as the original walk
  found it and put back as it left it.
- **Only the view changes.** The two projections that carry the player's
  camera are already named to the renderer by address (fr.c's plain
  perspective, bondview2.c's view-folded `field_10E0`). On a presentation
  pass each gets one left-multiply: eye space `C1 = V^-1 V_I` (translation
  scaled by the stage's visibility scale, which the game applies to x, y and
  z of every matrix it submits), origin space `D = V_rel_I V_rel^-1`
  (`src/gfx/sl_gfx_present.c`). The first-person weapon (its view-model
  bracket), the HUD, the watch and the front end (unnamed projections) are
  left alone.
- **The regular frame is the original.** Before the first extra frame the
  back buffer (colour, depth, stencil) is copied to an offscreen framebuffer
  and blitted back for the regular present at the VI; without that copy the
  list is re-walked at its own camera, which measured pixel-identical.
- **Nothing extra when in doubt.** No window or a minimised one, a frame of
  more or fewer than one display list, a pending bug mark, a camera pair
  that is not consecutive or jumps (teleport, cut, respawn, camera-mode
  change, level load, split screen), a frame already past its VI.

## What it changes in the pacing

With `render_fps` set, the pacing sleep is a high-resolution timed wait plus
a short spin (`src/platform/sl_precise_wait.c`) instead of nanosleep. On this
toolchain nanosleep waits on the system tick: measured overshoot 10.6-14.6 ms
mean, so the accepted pacing's regular presents land 8.8 / 25 ms apart at
p10 / p90 for a nominal 16.7 ms. The VI schedule is the same code; with
`render_fps` 0 nanosleep stays, and the accepted pacing is unchanged.

## The FRAME RATE row

The value is chosen in the game's options, beside VSYNC: the watch's
SIGHTLINE -> DISPLAY child and the front end's DISPLAY tab step one store
row (`render_fps`) through ORIGINAL (0, the default), 30, 60, 120, 144,
165, 240 and UNLIMITED (-1). The pump reads the value in force every frame,
so a change applies on the next frame; SL_RENDER_FPS overrides it for a
launch until a row is changed.

- **ORIGINAL** is the accepted pacing, exactly: nanosleep, one present per
  VI, nothing extra.
- **60** is the same frames with the precise wait.
- **A cap below the VI rate (30)** holds regular presents: the frame is
  built, paced, traced and marked as before, and only its swap is skipped,
  so at most N frames a second reach the screen (each VI earns N / VI-rate
  of a present). The simulation cannot see it.
- **Above the VI rate** extra frames are presented as described above.
  With VSYNC on, every swap waits for a refresh, so the display's refresh
  rate is the real limit: on a 60 Hz panel no extra frame fits.

## The SHOW FPS counter

SHOW FPS (OFF by default, beside FRAME RATE in both DISPLAY views; the store
row `show_fps`, SL_SHOW_FPS=0 / 1 for a launch until the row is changed)
draws the number of frames actually presented per second in the upper-right
corner of the game image.

- **What it counts.** Swaps. Every SDL_GL_SwapWindow the backend makes goes
  through one function (`sdl_swap`, `src/gfx/sl_gfx_sdl.c`): the regular
  present and each extra frame. A regular frame held by a cap below the VI
  rate makes no swap and is not counted. The value is swaps per second over
  a 0.5 s window on the performance counter (`sl_fps_meter_note`,
  `src/platform/sl_window.c`) - the display's real throughput, not the
  simulation's frames and not the cap.
- **Where it is drawn.** Into the back buffer just before the swap, after
  the bug mark's and SL_SHOT's read-backs and after the SL_PRES_LOG hash, so
  no capture, hash or trace carries it. The game's own on-screen messages
  (`hudmsgBottomShow`) are game state in the display list and do appear in
  captures; the counter is a presentation overlay and does not. It sits in
  the corner of the content rect (the selected aspect fitted in the window),
  scaled by its height, so at a wide aspect it is in the side band, clear of
  the 4:3 HUD.
- **How.** Scissored clears of the colour buffer (an opaque box, then one
  per run of lit pixels of a 5x7 font), with only the scissor, colour mask
  and clear colour pushed and popped. A first version drew quads behind
  `glPushAttrib(GL_ALL_ATTRIB_BITS)` and two `glGet` queries and measured
  0.28-0.50 ms of CPU per present (max 24-42 ms); the clears measure 6-50 us.

## Evidence (round hr1, Facility and Dam)

docs/backlog.md B-HR1 holds the measurements: per-tick traces byte-identical
across seven arms on each stage, headless traces unchanged, extra frames
pixel-identical to the regular frame with the camera blend off and new
images with it on, VI and game clock at 60.0/s in every arm, audio queue and
underruns unchanged.
