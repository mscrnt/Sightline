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
