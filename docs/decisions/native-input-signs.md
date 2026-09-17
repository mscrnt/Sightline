# Native keyboard/mouse: the four channel sign conventions, measured

Status: SETTLED by measurement, 2026-09-01. **RE-MEASURED and CONFIRMED the
same day** after an owner report of mirrored mouse look — see "Re-measurement"
at the end. Nothing in the table below changed; the conclusions here are not the
cause of that report.

The native keyboard/mouse path writes four numbers into `struct MoveData` at a
seam inside `bondviewProcessInput` (`src/game/bondview2.c`, search
`sl_move_channels_get`). Their signs are not a matter of taste — get one wrong
and the control is reversed. This records how each was established.

The whole reason this file exists is the project rules' standing corollary
(`docs/project-rules.md`, rule 7): *every
measurement taken held up; every conclusion reached by reading code was wrong.*
That held again here — see "What reading the code said" below.

## The answer

| channel | field | positive means |
|---|---|---|
| forward/back | `analogWalk` | forward |
| strafe | `analogStrafe` | right |
| turn | `analogTurn` | **turn right** |
| pitch | `analogPitch` | **look down** |

All four in the game's own +/-70 unit: every consumer divides by `70.0f`
(`bondview2.c:5635`, `:5654`, `:5902`, `:5960`).

`analogPitch` positive = look DOWN is the game's default vertical sense, not an
inversion introduced here. The player's own Look Up/Down option flips it at
`bondview2.c:5455-5464`, which is *after* the seam — writing the channel before
that block is what keeps the option working instead of duplicating it in the
host.

## Method

Screen-space measurement on the real renderer. A temporary env knob in
`src/platform/sl_input.c` injected a CONSTANT value into one channel; the
engine's own frame capture (`SL_SHOT`, PPM) recorded every frame; a
sum-of-absolute-differences search found the integer image shift between
consecutive frames. The knob was removed before commit.

Facility, direct boot, 320x240, `SL_SHOT_EVERY=1`. Direct boot performs zero
`osEepromRead` (see `src/native/sl_game_query.c`), so this is the game's DEFAULT
Look Up/Down setting, not a saved one.

Why screen space and not a state variable: "which way did the player turn" is a
question about the rendered image, and `vv_theta` only answers it once you have
already assumed a mapping from yaw to screen direction — which is the very thing
in doubt.

### Yaw: `SL_TEST_TURN=25`

Every one of the first 32 consecutive frame pairs:

```
f-00001.ppm   dx= -2 (err 2.3)  dy=+0 (err 6.8)  raw= 6.76
f-00002.ppm   dx= -2 (err 2.4)  dy=+0 (err 6.7)  raw= 6.74
...            (dx = -2 on all 32, err 1.3-2.6 against raw 6.5-8.3)
f-00032.ppm   dx= -2 (err 1.5)  dy=+1 (err 6.7)  raw= 6.75
```

Image content moves LEFT, at a constant rate, with the fitted shift's residual
well below the unshifted difference. In a first-person view content moving left
IS the camera turning right. Confirmed independently by eye on a filmstrip: a
riveted vertical door seam tracks steadily from x≈75/240 to x≈8/240 over six
sampled frames.

**Positive `analogTurn` turns RIGHT.**

### Pitch: `SL_TEST_PITCH=25`

```
f-00001.ppm   dx=+0 (err 4.2)  dy= -1 (err 1.6)  raw= 4.17
...            (dy = -1 on frames 1-12, err ~1.5 against raw ~4.2)
```

Content moves UP the frame, i.e. the camera pitches DOWN. Confirmed by eye: the
centre of an X-brace panel climbs from y≈60/150 to y≈40/150. The sign reverses
around frame 13 — that is the pitch hitting the game's limit and auto-centring
pulling back, which is the held-input behaviour, not the response being measured.

**Positive `analogPitch` looks DOWN.**

### Walk and strafe

Measured with the real keyboard (unaffected by the instrument problem below).
Holding W from the airlock start moved Bond forward through the airlock and into
the corridor beyond — unambiguous on the filmstrip. **Positive `analogWalk` is
forward.**

Strafe is the weakest of the four measurements and is reported as such: holding
D gave a consistent dx of -1 to -2 (content moving left, i.e. strafing right),
but the residual was no better than the unshifted difference because the motion
down a corridor is an expansion rather than a pure shift. It is corroborated
rather than proven: the game's own default is `analogStrafe = controlStickXSafe`
(`bondview2.c:4832`), and the existing gamepad path — which the owner has played
and validated — already maps its `move_strafe` (+ = right) straight onto
`stick_x`. The channel uses the identical convention.

## What reading the code said, and why it was wrong

The plausible reading, and the one carried into this task as a hypothesis, was
that positive `analogTurn` turns LEFT and horizontal look is therefore reversed:

- `canNaturalTurn` sets `speedtheta = +(analogTurn/70)^2signed * fovScale`
  (`:5981`).
- The aim path calls `bondviewCurrentPlayerUpdateSpeedTheta(+aimTurnLeftSpeed)`
  for LEFT and `(-aimTurnRightSpeed)` for RIGHT (`:5985`, `:5989`).

Taken at face value that makes positive `speedtheta` a LEFT turn, and so
positive `analogTurn` a left turn too.

It is wrong because `bondviewCurrentPlayerUpdateSpeedTheta` does not assign its
argument. It drives `speedtheta` toward a limit computed by
`sub_GAME_7F080228` (`:4076`), and that helper NEGATES:

```c
if (0.0f < arg0)  return (viGetFovY() * arg0 * -0.7f) / FOV_Y_F;   /* < 0 */
else if (arg0 < 0.0f) return (viGetFovY() * -arg0 * 0.7f) / FOV_Y_F; /* > 0 */
```

So aim-turn-LEFT drives `speedtheta` NEGATIVE and aim-turn-RIGHT drives it
POSITIVE, which agrees with `canNaturalTurn` rather than contradicting it. The
two paths were consistent all along; one function two hundred lines away
inverted the sense, and no amount of care at the call site would have surfaced
it. Hence: measure.

## What was NOT the problem

Horizontal look really did behave wrongly before this change, but not because of
a sign. Under 1.1 Honey the N64 stick turns and the C cluster strafes
(`bondview2.c:5236-5250`), and the old keyboard/mouse map wired the mouse to C
and WASD to the stick. So the mouse STRAFED and A/D TURNED. Under 1.2 Solitaire
the two swap and the confusion swaps with them. That is a consequence of pushing
two devices through one analog stick, and it is what the four-channel seam
removes.

## Instrument note

`xdotool mousemove_relative` is NOT a usable instrument for mouse sign here.
With SDL relative mouse mode plus a window grab, SDL's own pointer warp and
xdotool's synthetic motion fight each other: a run driving the pointer only
rightwards produced `turn` values of both `+70` and `-70` in roughly equal
numbers. That is an artefact of the instrument, not of the mapping — the mapping
applies no negation to `dx` anywhere. Constant-value injection was used instead,
which is why the sign results above are stated for the CHANNELS rather than for
the mouse.

## Re-measurement, 2026-09-01

The owner reported mouse look FULLY MIRRORED on both axes. Because a single
wrong sign mirrors one axis and only a wrong *convention* mirrors both, the
suspicion was that the shift measurement above had its sign convention
backwards. It did not. Everything below is a fresh measurement, not a re-read.

### The detector was validated before it was trusted

The convention is now stated explicitly:

> `dx` is the amount CONTENT MOVED between frame A and frame B. A feature at
> column `x` in A is at column `x+dx` in B. So `dx < 0` means content moved
> LEFT, which in a first-person view means the camera turned RIGHT.

and proved on shifts of KNOWN direction before being pointed at shifts of
unknown direction — a real frame displaced by `(+3,0)`, `(-3,0)`, `(0,+2)`,
`(0,-2)` and `(+4,-3)`, all five recovered exactly, residual 0.000.

### Channel signs: unchanged

`SL_TEST_TURN=25` gives content `dx = -3` per frame, every frame, residual 2.7
against 6.9 unshifted. Content moves LEFT, so the camera turns RIGHT. The
original run measured `dx = -2` under the same injection — the same direction,
and the difference is only the constant used. Confirmed by eye on a filmstrip:
the riveted vertical door seam tracks steadily leftward across four sampled
frames.

**Positive `analogTurn` turns RIGHT. Unchanged.**

### The whole mouse chain, end to end

The stronger test the original run could not do, because `xdotool` is not a
usable instrument here (see "Instrument note"): inject `dx`/`dy` at the
`SDL_GetRelativeMouseState` boundary — overriding only the two numbers SDL would
have reported — so `read_mouse`, `map_kbm`, the channel, `moveData` and the
renderer are all the real path.

| injected | content shift, per frame | camera | verdict |
|---|---|---|---|
| mouse RIGHT (`dx=+15`) | `dx = -12` | turns RIGHT | correct |
| mouse LEFT  (`dx=-15`) | `dx = +12` | turns LEFT | correct |
| mouse DOWN  (`dy=+15`) | `dy = -12` | pitches DOWN | correct |
| mouse UP    (`dy=-15`) | `dy = +8`  | pitches UP | correct |

Confirmed by eye: holding mouse-down brings the airlock floor grating into
frame; holding mouse-up raises the view toward the top of the door.

**The mouse-to-channel translation is not mirrored on either axis.** No sign in
`read_mouse` or `map_kbm` was changed, and none should be — inverting a
measured-correct mapping would break it.

### What this means for the owner's report

The mirroring is therefore not in the arithmetic. The remaining candidate, and
it is inference rather than measurement, is the pointer capture: SDL relative
mode was being asserted before the window owned the pointer, and in that state
on this XWayland/WSLg setup SDL's motion accounting is not trustworthy — the
"Instrument note" below already records a run in which motion in ONE direction
produced `turn` values of both `+70` and `-70` in roughly equal numbers, which
was blamed on `xdotool` and may well have been this. Capture is now
click-to-capture, which is the fix for that state.

### Aim mode and the pad paths

Both were derived from the table above, and the table did not change, so neither
inherits an error. The aim-mode stick sense is corroborated rather than
re-measured: `stick_y > 60` while aiming raises `speedVertaDown`
(`bondview2.c:5279`), and `bondviewCurrentPlayerUpdateSpeedVerta(+v)` drives
`speedverta` DOWNWARD (`:4010-4030` — it subtracts), the same direction positive
`analogPitch` drives it through `canNaturalPitch`. Positive `analogPitch` is
measured to look down, so positive `stick_y` while aiming looks down too, and
mouse-down produces it. `tools/native/inputtest.sh` asserts all four aim-mode
directions against that.

## Resolution of the mirrored-pitch report, 2026-09-01 (later)

The re-measurement above was right that the mouse-to-channel arithmetic is not
mirrored, and wrong to leave pointer capture as the only remaining candidate.
For PITCH there was a second mechanism, and it was in the game, not this layer.

`bondviewProcessInput` applies the player's Look Up/Down option at
`bondview2.c:5579`:

```c
moveData.invertPitch = get_cur_player_look_vertical_inverted() == 0;  /* :4849 */
...
if (moveData.invertPitch == 0) {                                      /* :5579 */
    moveData.controlStickYRaw = (s32) -stick_y;
    moveData.analogPitch      = -moveData.analogPitch;
    swap(moveData.speedVertaDown, moveData.speedVertaUp);
}
```

`invertPitch` is the NEGATION of the setting, so the block runs when the player
has selected UPRIGHT. `options.c:133` declares the option's labels in the order
"reverse, upright" with default `current_value` 0, and `options.c:498` returns
`current_value` unchanged — so 0 is REVERSE, GoldenEye's authentic default,
under which the block does NOT run.

Every earlier measurement in this file was taken on a direct boot with a fresh
save, i.e. with the option at REVERSE and the block dormant. That is why they
all measured correct and the owner still saw inverted pitch: the owner's session
had Look set to UPRIGHT. Measured, mouse UP, Honey, injected at the SDL
boundary:

| | before | after |
|---|---|---|
| upright, normal look | `vv_verta = -90.00` (camera DOWN) | `+90.00` (UP) |
| upright, aiming | `vv_verta = -90.00` (camera DOWN) | `+90.00` (UP) |

Aim mode was broken by the SAME block through a different route: while aiming
the four-channel seam is skipped entirely (it is gated on `insightaimmode == 0`)
and the mouse drives the N64 stick instead, from which `:5279` derives
`speedVertaUp`/`Down` — which `:5579` then swaps.

**The lesson for this file: a measurement is only as general as the game state
it was taken in.** Every run here used the default option and none of them said
so. The 8-cell matrix that replaced them varies control style, the Look Up/Down
setting and normal-versus-aim, and states which cell each number came from.

Owner decision, the same day: the Look Up/Down option is a CONTROLLER setting.
The mouse now has conventional PC direction unconditionally, implemented by
pre-negating the mouse's own pitch in `map_kbm` so the block above cancels
exactly. The pad path is untouched and still obeys the option.

## The physical-to-SDL sign is a PROPERTY OF THE HOST, 2026-09-02 (B-071)

Everything above measures the chain from `read_mouse`'s dx/dy inward, and all
of it still stands. This section is about the one link upstream of that
boundary — how a PHYSICAL movement becomes SDL's dx/dy — which no measurement
in this file ever touched, by construction: every test here injected dx/dy AT
that boundary and so assumed the answer.

**The defaults are now per-platform.** `src/platform/sl_input.c`, at the
declaration of `g_dx_sign`/`g_dy_sign`:

| host | default | evidence |
|---|---|---|
| `_WIN32` | `+1 / +1` | stock SDL2's nominal convention (+dx right, +dy down), which is the convention the "whole mouse chain, end to end" table above was screen-measured correct against |
| everything else | `-1 / -1` | owner hand-test under WSLg/XWayland at commit `fbcc3a48`: LEFT->LEFT, RIGHT->RIGHT, UP->UP, DOWN->DOWN |

### Why a single global default was wrong

MEASURED, by git rather than by reading:

```
git log -S "g_dx_sign" --all -- src/platform/sl_input.c
  fbcc3a48  [P1] input: PRE-WINDOWS-PLATFORM CHECKPOINT
```

One commit, and only one. `git show fbcc3a48^:src/platform/sl_input.c` shows
`read_mouse` applying NO sign at all before it — SDL's raw dx/dy went straight
into `look_yaw`/`look_pitch`. So `fbcc3a48` did two things at once: it
introduced the knobs, and it introduced a NEGATION ON BOTH AXES, defaulting to
the value the owner had just accepted under WSLg.

The re-measurement table above was taken on the pre-`fbcc3a48` code, i.e. at an
effective `+1 / +1`, and it found all four directions correct on the rendered
image. This file then said, in as many words, that inverting a measured-correct
mapping would break it — and the very next commit inverted it, correctly for
its own host.

`fbcc3a48` is titled PRE-WINDOWS-PLATFORM CHECKPOINT. The checkout moved to
Windows immediately after, onto stock `mingw-w64-i686-SDL2` with no Sightline
transport in between (`tools/windows/setup.ps1` provisions it; the build links
`-lSDL2` and stages the DLL). A transport that negates nothing, plus a default
that negates both axes, is mirrored look on both axes — which is exactly what
the owner reported as B-071.

### What was NOT the cause

Tree-wide, `grep -rn "mouse\|Mouse\|MOUSE" src/ --include=*.c --include=*.h`
returns no relative-motion handling outside `src/platform/sl_input.c`:
`src/gfx/sl_gfx_sdl.c` touches only button-down, wheel and focus events, and
`SDL_GetRelativeMouseState` has exactly one caller in `src/` (`sl_input.c:516`
at the time of the repair). There is no Windows mouse transport of Sightline's
own to be wrong, so the sign application IS the first transformation in the
chain that this repository owns.

`g_look_invert` and the Look Up/Down handling were also cleared without a
measurement being needed: both are pitch-only, and a pitch-only knob cannot
mirror yaw. The report was both axes.

### Standing rule

A sign that encodes a HOST convention must be defaulted per host, and each arm
must carry the evidence for its own host. This is the second time a value
validated on one transport was carried onto another as though it were a
property of the game.
