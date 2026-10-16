# World assets (#68)

An optional, **additive**, **render-only** external world asset, identified by
**asset class** and **canonical item path**, drawn in addition to the game's
own picture. Native-only, off by default.

Since **2026-09-23** it is not selected on its own. The player has ONE visual
selection - **PRESENTATION** (ORIGINAL / COMMUNITY HD / XBLA) - and a policy
resolves it into a set for each visual provider, so one F5 press moves the
artwork and the world assets together. The provider below is unchanged in
every other respect; what changed is who chooses. See
[The presentation policy](#the-presentation-policy).

This page is the runtime architecture. It carries no asset, no bundle and no
pointer to one: Sightline neither ships nor locates a world asset. The user
brings a source; a local tool prepares it; the runtime reads what is there and
falls back to the game's own rendering when it is not.

**Two mechanisms now ship.** ADDITIVE - the skydome, drawn as well as the
game's own picture - and REPLACEMENT - the Dam's whole static background,
standing in for the room background the game draws and suppressing it. The
first attempt at replacement stopped at identifying a sub-target; the second
took the unit the remaster itself replaces. Both accounts are kept, because
the boundary the first one hit is what chose the second one's unit:

> **A playtest REJECTED the first whole-background result** ("this is
> completely broken", 2026-09-23). #68 is OPEN. Four claims made for that
> result are withdrawn, and what the rejection's own six marks actually
> measured is in
> [What the playtest rejection measured](#what-the-playtest-rejection-measured).
> Read that section before trusting anything below it about correctness.
>
> **A second playtest then supplied a matched ORIGINAL/XBLA pair at one
> camera**, and that pair found the real first wrong stage: the replacement was
> being put into the room space while the frame's matrices consume that space
> TIMES the stage's `visibility` - 0.2 at the Dam, 1.0 almost everywhere else.
> See
> [The matched-presentation pair, and the registration contract it exposed](#the-matched-presentation-pair-and-the-registration-contract-it-exposed).
> It also retires the floor-sample argument the section before it rests on.
>
> **A playtest then ACCEPTED the registration repair as materially better** -
> "plays much better and looks a lot better", "I can traverse it normally" -
> and reported gaps in the world with it. The first of those is closed: 258 of
> the item's 389 draws are triangle FANS and were being expanded as ribbons,
> which tiles a zig-zag and leaves a lens of sky through solid ground. See
> [The world gap: a FAN was being drawn as a RIBBON](#the-world-gap-a-fan-was-being-drawn-as-a-ribbon).
>
> **A playtest then confirmed the world is traversable and the fan repair
> held**, and returned eight marks with seven reports. The first of them - the
> z-fighting on the bridges at the start - is closed: the source separates a
> DECAL PASS by a bit of its style word, the preparation was dropping it, and
> two surfaces authored **exactly** coincident were resolving per pixel by
> floating-point noise. See
> [The coplanar pass](#the-coplanar-pass-the-source-has-a-decal-group-and-the-pipeline-dropped-it).
> The truck and watchtower marks shared that cause and are repaired with it.
> The tunnel's missing road lines, the sky ring, the water, the mountain
> shading, fog and the UV scale are all still open, each classified at the end
> of that section, and **#68 is still OPEN**.

[Additive and REPLACEMENT are different mechanisms](#additive-and-replacement-are-different-mechanisms),
[Where the first replacement attempt stops, measured](#where-the-first-replacement-attempt-stops-measured)
and [The whole static background, as one replacement](#the-whole-static-background-as-one-replacement).

---

## Why this is not the texture provider

The texture provider (#47, `docs/texture-packs.md`) answers *"what artwork goes
on this game texture"*, keyed by the N64 texture number. It can only repaint a
surface the game already draws.

The first asset here has **no counterpart in the game at all**. It is additive
geometry, and no per-texture substitution can produce it at any resolution,
because there is no original id to key on. So the identity is different in
kind, and the two remain independent **mechanisms**:

| | texture sets (#47) | world assets (#68) |
|---|---|---|
| identity | N64 texture number | asset class + item path |
| unit | one image | one item: meshes, materials and its own images |
| effect | repaints an existing surface | adds geometry that was not there |
| format | `SLTX` | `SLWA` |
| root | `<assets>\texpacks` | `<assets>\worldassets` |
| fallback | this set's miss -> ORIGINAL | this set's miss -> the game's own picture |
| default | ORIGINAL | off |
| **selected by** | **the active presentation** | **the active presentation** |

Two things that are both true, and are worth keeping apart:

- **The providers are independent mechanisms.** Different identity, different
  format, different root, different cache. Neither reads the other's state,
  and a texture provider that grew meshes would be the wrong design.
- **The active presentation chooses both.** Which set each provider draws is
  one player-facing decision, not two.

Before 2026-09-23 only the first was true and the second was not, and the
result was reachable states nobody designed - ORIGINAL artwork with an XBLA
skydome, COMMUNITY HD artwork with an XBLA skydome. The design decision was
coherent bundles.

## The presentation policy

One layer above both providers, `src/platform/sl_presentation.{c,h}`. It owns
the canonical value and RESOLVES it, per provider:

| PRESENTATION | texture provider | world-asset provider |
|---|---|---|
| ORIGINAL (0) | ORIGINAL - no pack read | none - no root resolved, no directory opened |
| COMMUNITY HD (1) | the COMMUNITY HD set | none |
| XBLA (2) | the XBLA set | the `xbla` set - so the Dam's skydome follows |

COMMUNITY HD carries no world assets because none exist for it: the community
pack is a texture pack. That is a fact about what is installed, not a hole in
the design - the resolver answers "none" and the Dam draws its own sky, which
is the correct picture for that presentation.

**Fallback stays per provider and never crosses sets.** The policy chooses a
set per provider; what happens when that set is incomplete is the provider's
own business and is unchanged. A texture the XBLA set lacks falls back to
ORIGINAL, **never** to COMMUNITY HD; a world asset the XBLA set lacks draws
the game's own picture, never another set's. A half-installed XBLA pack
therefore degrades towards the original artwork and never towards the
community one.

**A later provider class joins here, not at F5.** Add a resolver and a table
column; the new provider reads it the way these two do. The cycle action, the
two UI surfaces, the stored key and the migration are all expressed in terms
of the presentation, so none of them changes. No empty providers were created
for this: model, material and lighting providers do not exist yet and are not
stubbed. Lighting (#48) in particular is **not** pre-coupled to XBLA - when a
lighting policy exists, this is where it would resolve, and that is the only
claim made.

### Where the selection comes from

| Where | How |
|---|---|
| **In the level** | **F5**, or the pad's **View / Create** button - one press steps ORIGINAL -> COMMUNITY HD -> XBLA -> ORIGINAL |
| Front end | OPTIONS -> SETTINGS -> DISPLAY -> PRESENTATION |
| Watch | SIGHTLINE -> GRAPHICS -> presentation |

All three read and write the same stored key, `presentation=0/1/2`, and none
of them holds a copy - so a change made in one is visible from the others when
they next draw.

### The diagnostic override

`SL_WORLD_ASSETS` used to BE the selection. It is now an **advanced /
diagnostic** override that pins THIS provider ahead of the presentation:

```
override (SL_WORLD_ASSETS)  ->  presentation-derived
```

`xbla` pins it lit; anything else - including the off-words and an
unrecognised word - pins it dark. **It is never silent.** Whenever the
override disagrees with what the presentation selected, the provider prints
one line naming both, and `tools\windows\play.ps1` prints another before the
run. The normal launch does not set it at all: with `-WorldAssets FOLLOW`
(the default) the launcher CLEARS any value left in the shell and says so, so
a stale variable cannot quietly make a session look mixed.

## Identity

```
asset class + canonical item path      SKYDOME + "dam"
```

The **class** says what the asset is and therefore how it is drawn. The
**path** says which item it belongs to. A stage with no item path performs no
lookup, opens no file and draws exactly as it does today - so a stage the
feature does not cover is a negative control by construction rather than by a
flag. The stage-to-item-path vocabulary lives in exactly one place
(`src/native/sl_world_assets.c`); adding an item is a line there plus a
prepared file, and no renderer change.

## Where the files live

```
<root>\xbla\<class>\<item>.slwa
```

The root is, in order:

1. `SL_WORLDASSET_ROOT`, if set and non-empty (tests, development launches);
2. the player-data ladder every other native asset uses,
   `%LOCALAPPDATA%\sightline\assets\worldassets`.

No source-tree path is compiled into the binary. Activation is separate from
installation and still defaults **off**, but it is the PRESENTATION that
decides (above): ORIGINAL and COMMUNITY HD leave the provider dark - no root
resolved, no directory opened, not one file touched - and XBLA selects the
`xbla` set. `SL_WORLD_ASSETS` is the diagnostic override, not the selection.

The selection is **re-asked every time, never latched once**, because it is
now a value the player moves mid-session. Going dark does not unload: an
asset already parsed stays resident and keeps the GL names the renderer gave
it, so returning to the set that owns it re-uses the same cache slot and the
same upload rather than duplicating either.

## The runtime format (SLWA)

The tree links no model loader and none was added (project rules: no
dependency without asking), and **the runtime does not parse the private
source container either**. A preparation tool converts the source once,
outside the engine, into a small Sightline-owned form the runtime reads
directly. The layout is specified in `src/gfx/sl_gfx_worldasset.h`: a 128-byte
header with a clear magic and version, then vertices (position, one texture
coordinate set, RGBA8 colour), indices, materials, item-local images and
meshes.

**Version 2 (#69)** gives the header's three reserved words a meaning: a
per-vertex auxiliary section (12 bytes: the source program's wave coordinate
pair, the displacement direction, and which animation record moves the
vertex) and a table of 128-byte animation records (the source's own sine-wave
contract - amplitude, phase, speed and frequency per wave lane, the
coordinate scale, and the evaluation constants). Both sections are present
together or not at all, bounded and overlap-checked like the five before
them, and every record is checked to be one the evaluation can use. **A
bundle with nothing to animate is still written as a version-1 file, byte
for byte**, and the runtime reads both versions; a version-1 file cannot carry
the animation words, so an older reader refuses a moving bundle rather than
drawing it still. See "The water moves" below for the contract itself.

**This is not a universal model format and is not offered as one.** It carries
exactly what the renderer consumes and rejects everything else, so that what
it does carry can be validated exhaustively.

**Every field is checked before a byte is trusted**: counts are bounded before
they are multiplied, so no length can overflow; every section must lie wholly
inside a file whose length was *measured* and compared against the length the
header declares; sections may not overlap each other or the header; every
index, material reference and image reference is range-checked at load rather
than at draw. Nothing is allocated from an untrusted count.

A file that is truncated, malformed, of an unknown version or class, or that
names an item other than the one being asked for, is **INVALID**: counted,
reported once, and the stage draws exactly as it does with no asset at all.
A file that is not there is **MISSING**, with the same outcome and one line in
the log. Both are resolved at most once per run.

## Preparing an asset (development)

A checkout carries two private directories, behind the boundary
`baserom.u.z64` already lives behind - both gitignored, neither ever
committed, packaged or exported:

```
texsources\      the sources. Shared with the texture packs, and that is ALL
                 the two features share.
worldassets\     what the tool WRITES: xbla\<class>\<item>.slwa
```

One command:

```
.\tools\windows\prepare-xbla-assets.ps1 -DamSkydome
.\tools\windows\prepare-xbla-assets.ps1 -SelfTest     # synthetic fixtures only
.\tools\windows\prepare-xbla-assets.ps1 -DamSkydome -Force
```

It is deterministic, rerunnable and idempotent: the source and the tooling are
identified by SHA-256 in gitignored build output, an asset already built from
them is left alone, and every file is written through a temporary and renamed,
so a bundle is only ever complete or unchanged. No fingerprint of a
user-supplied source is ever written into a tracked file. With no source
present it stops with one actionable line and writes nothing.

`tools\windows\play.ps1` hands `worldassets\` to the child whenever it exists -
the ROOT, not the selection - so the ordinary launch finds a prepared asset
with no switch at all:

```
.\tools\windows\play.ps1 -Level dam -Cheats invincible,invisible,maxammo
```

then **F5** cycles ORIGINAL -> COMMUNITY HD -> XBLA -> ORIGINAL and the
skydome comes and goes with the artwork.
`-Presentation ORIGINAL|COMMUNITY|XBLA` starts on one deterministically (it
writes the one canonical stored key, so no environment export is needed), and
`-WorldAssets OFF|XBLA` is the diagnostic pin described above. An explicit
`SL_WORLDASSET_ROOT` still wins for the root, exactly as `SL_ROM` does.

**The source is user-supplied.** Sightline neither ships, locates nor downloads
one, and nothing in this repository points at one.

### Looking at what was prepared (contact sheets)

Many graphics questions are about what a prepared bundle HOLDS, not about how
the game draws it: which image a part uses, whether its vertex colour zeroes
it, how large it is, whether a level's world carries a given surface at all.
`tools\worldasset\xblasheet.py` answers those from the bundles alone - no
emulator, no game:

```
.venv\Scripts\python.exe tools\worldasset\xblasheet.py --root worldassets --out <dir> --class prop
.venv\Scripts\python.exe tools\worldasset\xblasheet.py --root worldassets --out <dir> `
    --items prop/gun_runway1,prop/plane,prop/tank,background/runway --size 512
```

Per bundle it writes a sheet - five orthographic views (front, side, two
three-quarter, top) with each image modulated by vertex colour, every image
with its size, and one line per mesh (material, image, blend, flags,
triangles, extent, and the share of its vertices whose colour is black,
highlighted when most are) - plus `index.png` (one view per bundle) and
`report.json` (the same table). A background is framed on where most of its
vertices are, not on its panorama shell. It is a developer view, not the
renderer: nearest sampling, no fog, no normal maps, no lighting - a mesh that
is black on the sheet is black in its own data. The sheets show the private
artwork and stay outside the repository like the bundles they are drawn from.

## The first slice: the Dam skydome

Chosen because it is the smallest thing that proves the mechanism: additive
and remaster-only, so there is no original asset to preserve and no regression
surface in the accepted rendering; one item, so the loader is exercised end to
end without a dependency graph; and no original-UV compatibility problem,
which is the wall the texture pipeline hit for re-authored art.

What the prepared asset carries, measured from the source: two draws
(1277 and 501 vertices, 3400 triangles between them, in the order the item's
own command list draws them) and two item-local images, one opaque and one
carrying alpha. The geometry is in the **original
game's own world coordinates** - measured, with no scale factor and no axis
change - and the bounds computed from the vertices read agree to the float
with the bounds the source bundle declares for itself, which is the check that
says the right bytes were read.

**Item-local images are the asset's own resources.** They carry no N64 texture
number, they are not in the image table, and #47's mapping tables are neither
read nor touched.

## The render seam

**Render-only, and deliberately narrow.** The asset is not in any room, not in
collision, not in the AI's world, not an objective, not a portal, not a prop,
not in hit detection and not in save state. Nothing in `src/game` is touched
by this feature at all - the two questions it has to ask the game (which item
is this stage, and where is the camera) are answered in `src/native`, which is
compiled with the game include path for exactly that purpose. So the cartridge
build is unchanged by construction rather than by inspection.

**Where it is drawn:** the frame's first triangle under the world projection.
That is after every clear the frame performs, so nothing wipes it, and before
any world geometry, so the world draws over it - which is the whole of a
background's ordering requirement. It is **not** the whole of a background's
requirement, though: that seam is inside a room's display list, and the room
brings its own portal window with it. See the aperture contract below.

**How it is transformed:** through the same world-to-clip matrix the game's own
world geometry goes through, so the background follows camera rotation exactly
as the world does, with nothing about the camera reconstructed from angles.
**Where it is anchored is a separate question, and the answer is not "in the
world" - see the anchoring contract below.**

**Depth, cull and fog:** depth test off and depth write off, so the asset can
never occlude near geometry and contributes no depth value anything else tests
against; culling off, because a dome is viewed from inside and a wrong winding
guess would cost the sky; fog off, because the game's fog is a distance fog
against its own far plane and would flatten a background meant to be beyond
it.

> **Still true of the dome, and only of the dome.** That last clause was later
> carried across to the world-anchored REPLACEMENT, where its premise does not
> hold, and that was a defect - see *The fog contract* below. The dome's own
> unfogged picture is unchanged; what changed is that it is now obtained by
> pinning the fog coordinate to zero rather than by disabling the stage, so it
> no longer desynchronises the enable the rest of the renderer mirrors.

**The far plane, without substituting a projection:** the asset is far larger
than the game's frustum, so the clip coordinate is computed in the renderer
and its Z replaced, with x, y and w untouched. That is exact rather than
approximate - screen position is x/w and y/w and neither moves, perspective
correct interpolation is a function of w and w does not move, and the near
plane still clips - and depth is tested and written by nothing in this pass.

**Materials:** only what the asset needs - one image, vertex colour, and a
blend mode of opaque or straight alpha. No general multi-map material
architecture was introduced, and the source says none is needed: the shader
constant tables in the item's own bundle name `c_ambient`, `c_constant0`,
`c_fog`, `c_fogcolour`, `c_fogtype`, `c_light0colour`, `c_light0dir` and
`s_sampler0` for the pixel stage and `c_modelview` / `c_modelviewprojection`
for the vertex stage. **There is no uv scale, offset or matrix constant
anywhere in them**, so the coordinate in the vertex is the coordinate the
sampler receives and no material transform is missing.

## The material contract, measured

Three things decide what a pixel of this asset ends up being, and each is read
from the item rather than defaulted.

**The texture coordinate is 16-bit fixed point with 14 fractional bits** -
`0x4000` is exactly 1.0 of texture. It was first read as two IEEE binary16
floats, which is wrong and which is what the first attempt drew: binary16 is
monotone in the unsigned bit pattern over the positive range, so the
"continuous across triangles" test that chose it cannot tell the two apart.
What tells them apart is arithmetic. The dome's raw span is exactly 16384
units on both axes, which is exactly one texture each way - what a dome that
wraps once must have - where the half-float reading gives a U of 0.0065 ..
426.0. The second mesh's raw maximum is `0x4000`, an exact 1.0 only against a
divisor of 16384. And at that scale the dome's U is a linear function of its
own azimuth to within 0.00005 of a turn over the 5th..95th percentile.

**The mesh set, its order and its buffers are parsed, not inferred.** The
item's `.stream` record is a command list whose draws carry an index count and
a buffer handle; the `.data` rendergraph carries the buffer table those
handles resolve through. The preparation tool reads both and requires the
result to agree with what the model blob decomposes into on its own. The
container encodes no sampler-to-resource link - no texture-name hash appears
in any of its records - so which image goes with which draw is derived, from
the draw order and from a set that splits into exactly one opaque image and
one carrying alpha; anything that does not split that way is refused.

**Both axes WRAP, and S is not a preference.** The dome's U is its azimuth and
the prepared coordinates run 0.479 .. 1.479, so a full turn crosses the
integer boundary; under any clamp everything past 1.0 collapses into the last
texel column. T runs 0.0048 .. 1.0049, the overshoot being the dome's bottom
rim below the horizon, and the source's texture records carry no clamp field
to say the axes differ.

**Mipmapped, item-locally.** The source ships a complete pyramid for both of
this item's images (11 levels and 10), so the reference sampler is
mip-filtered. The pyramid is built at upload with the same box filter the
game's own textures use; `GL_GENERATE_MIPMAP` and `GL_TEXTURE_MAX_LEVEL` are
absent from this tree's GL headers. Nothing in #47's global filtering policy
is read or changed - these are the asset's own images. The census prints the
sampler state **read back from GL**, not echoed from the request.

## The anchoring contract, measured

A playtest found the dome **shifting while walking** - "like it's loading over
again a bunch of times", and after a first repair, "better, but it still shifts
around when walking slightly". Two separate faults, both in where the dome was
placed rather than in what it was made of. The material work above is unchanged
by either.

**A skydome is a polar map about its own centre, so that centre belongs on the
eye.** Measured on the Dam item, not assumed: for the dome shell (1277 of its
1778 vertices) **U is the azimuth about the centre of the item's own bounds to
within 0.0002 of a turn**, and V is the elevation over **-15.5 .. +90 degrees** -
a full turn of sky from below the horizon to the zenith. A map like that is
only correct seen from the point it was projected about. Drawn at its raw
coordinates the viewer stands **42641 units off that centre** at the Dam, which
is outside the dome's own mean radius of 34659; from there the dome covers only
34 of 36 azimuth sectors and stops at 0.5 degrees of elevation, and every step
the player takes slides the sky. So the offset applied to each vertex is
`centre - eye`, and the dome carries no parallax at all.

**The eye is read out of the transform, never asked of the game.** GoldenEye
loads two world projections - `fr.c`'s plain perspective and `bondview2.c`'s
with the view already folded in - and **they consume different spaces**: the
first absolute world, the second world minus the player's `current_model_pos`,
the floating origin its rooms and view matrices are built against
(`bgroomtrans.c:216-218`, `bondview2.c:11415-11417`). The renderer records
which one is in force from the **address the game named**, in the same
statement that decides the projection is a world one, so the two cannot
disagree - the earlier structural sniff of the matrix's shape is gone.

That leaves only the question of which eye, and `sl_wa_view_origin` answers it
by recovering the view origin from the world-to-clip matrix itself: the rows of
a view-times-perspective carry the camera basis scaled by the projection's own
terms, the scales are those vectors' lengths because the basis is orthonormal,
and the eye falls out of the translation row. The recovered point is expressed
in whatever space that matrix consumes, so `centre - eye` is right in that space
without anyone naming the space. A matrix whose recovered basis is not
orthonormal is refused rather than guessed at.

Asking the game instead does not work, and the number says why: the camera in
the player struct is the camera **now**, while the matrix at the seam was built
earlier. Anchoring to the struct's eye left a residual of about **40 world
units against the folded projection, drifting frame to frame** with the
camera's own motion - a sky that slides while walking. Against the plain
projection, where the seam builds the view from that same struct, the residual
was 0.001. Read from the matrix, it is **0.001 on both**.

**Measured, teleport-sampled with the heading held fixed so only translation
varies** (mean absolute difference over the dome's own pixels, 0-255):

| translation only, heading fixed | per step (~40 units) | over 447 units | over 1557 units |
|---|---|---|---|
| as first playtested | 0.08 .. **22.11** at a room change | 22.17 | 27.96 |
| projection identity fixed | 0.10 .. **2.36** | 3.28 | - |
| **anchored to the recovered eye** | **0.00** | **0.001** | **0.002** |

On the playtest's own marked pairs, with their real headings, the sky between the
two marks differs by 21.56 then 2.52 (2026-09-22 pair) and 6.45 then 1.74
(2026-09-23 pair). **That remainder is the ~1 degree of heading between them
and nothing else**: re-render the same pair with the heading held fixed and
382 units of pure translation moves the sky by **0.0000**.

## The aperture contract, measured

A playtest then found the sky **changing with the heading alone**, and only
in one part of the Dam - "at my marks, you can just rotate and see the sky
change". Three marks, the eye moving 4.7 units across all of them while the
heading swings 53.7 degrees: a pure rotation in place, near the first alarm
button. Anchoring had nothing to do with it. **Where the dome was allowed to
paint did.**

**The dome is emitted on the frame's first world triangle, which is inside the
first ROOM's display list** - and GoldenEye issues `ED setscissor` before every
room's list, carrying that room's **portal window**. So the scissor standing at
the seam is one room's aperture; the portal traversal decides which room is
first and how wide its window is; and both are functions of where the camera is
pointing. In a portal-rich spot the sky was therefore being cut to a different
strip of the screen at every angle, with the game's own background filling the
rest - one hard vertical edge, sweeping across as the player turned.

Measured at the three playtest marks, teleport-sampled, one bounded run per
pose, heading and position both taken from the mark:

| heading | scissor box found at the seam | asset pixels inside it | dome coverage |
|---|---|---|---|
| 246.319 | `654,30 303x660` | **98.1%** | 5.4% of the frame |
| 207.019 | `516,30 441x660` | **100.0%** | 28.2% |
| 192.619 | `3,30 954x660` - the whole picture | **100.0%** | 71.5% |

against a viewport of `0,30 960x660` throughout. The playtest's own frames carry
the same artefact independently: a single dominant vertical step at window
column 876 of 1365 at the first heading and 723 at the second - and **none at
the third**, which is the one whose box was already the whole picture. That
negative case is what makes the box the cause rather than a correlate.

**A background covers the whole picture, and the rectangle clip space maps onto
is the VIEWPORT** - so that is the aperture the pass takes, whatever it finds.
`sl_wa_sky_aperture` is a function rather than four lines at the seam so the
rule can be tested with no GL context: what must hold is that the answer does
not depend on the box it was handed. The box and the scissor enable bit found
at the seam are restored exactly on the way out, so the room whose list this
pass interrupted draws under its own window as before.

After it, at the same three poses: coverage **5.4 -> 50.9%**, **28.2 -> 70.2%**
and **71.5 -> 72.1%**. The third barely moves, which is the control: where the
aperture was already the whole picture there was nothing to repair.

### What the recovered eye is in, measured

The witness now prints the eye the **game** has beside the eye this pass
**recovers**, in the same units, because `residual_w` cannot tell them apart -
it is computed from the recovered eye and is therefore ~0 whatever that eye is.
Read at the Dam, the ratio is **exactly 5.0000**: the folded projection consumes
world coordinates **divided by five** as well as offset by the render origin, so
the eye that comes out of it is one fifth of the eye in world units.

That is not a fault and it moves no pixel - `sl_wa_view_origin`'s contract is
that the eye comes back in the matrix's own space, and it does. The consequence
is that the dome is drawn five times its size **about the eye it is centred
on**, and a uniform scale about the eye changes no ray, so every vertex lands on
the same pixel; depth is replaced and fog is off, so nothing else reads the
distance. The plain branch, whose space is absolute world, draws it at 1x - and
the two agree on screen for the same reason. It is recorded here because the
number is measured and because a later asset class that does read distance
would not be able to ignore it.

### Known limits of the first slice

- One asset class and one item. Everything else - existing-world geometry
  replacement, props, characters - is a later increment and is not promised by
  this one.
- The image for each draw is derived from draw order and blend rather than
  read from an explicit binding, because the container does not carry one.
  That is sound for a background of one opaque shell and one blended pass and
  would not generalise to an item with several of either.

---

## Additive and REPLACEMENT are different mechanisms

Everything above is **additive**: the asset is drawn as well as the game's own
picture, so it has to name nothing in that picture and there is no original
population to preserve. That is why the skydome was the first slice.

A **replacement** is a different mechanism with one extra obligation, and the
obligation is the hard half:

```
original static render item -> identify -> suppress ONLY its native visual
                               draw -> draw the replacement -> preserve the
                               original simulation
```

Three rules follow from the obligation and hold whatever the target is.

**The fallback rule inverts.** Additive: a missing asset costs a picture that
was never there. Replacement: a missing asset would cost a HOLE in the world.
So the original draw is suppressed **only when a valid replacement is loaded,
validated and uploaded** - missing, invalid, or a failed upload means the
original draws exactly as it does today, and nothing is ever suppressed before
validity is known.

**The suppression boundary is the final native visual emission and nothing
else.** The display list still executes: every vertex loads, every matrix
applies, every render-mode and texture command takes effect, the interpreter's
own bookkeeping runs unchanged. Only the GL output of the selected draw is
skipped. No game data, room membership, collision, portal, prop list, AI
visibility or save state is read or written - which is what keeps the
simulation boundary the same one the additive slice has.

**The simulation and collision boundary is unmoved.** A remastered surface
whose shape differs from the original's is a VISUAL difference only: movement
and collision stay the original game's, and the disagreement is evidence to be
recorded rather than a defect to be fixed (project rule 5).

### What a replacement needs that the additive slice did not

A **world-space transform contract**. The skydome is eye-anchored: its centre
is put on the recovered eye and it carries no parallax, which is why the
five-times folded-projection scale (above) moves no pixel. A replacement for
existing world geometry is **world-anchored** - it must sit where the original
sat, through room changes and portal traversal - so the asset has to carry ONE
canonical world-space transform and the render seam has to convert it into
whichever space the active projection consumes, per frame, never store it
pre-folded for whichever room happened to render first. **None of the
skydome's eye-anchoring applies**, and the k = 5 scale becomes load-bearing
for the first time.

## Where the first replacement attempt stops, measured

The target was the Dam's cliff / snow environment shell. The work stopped at
**identify**, and the boundary is structural rather than a matter of effort.

### The original side is separable - but not by the identity the renderer has

Measured on the Dam, one bounded teleport-sampled run per case, at the gate
yard where the cliff fills most of the frame:

- The renderer's only stable identity for world geometry is the one the game
  itself branches with: the **room**, and which of its two lists. It is read
  from the depth-1 display-list operand through the same `g_BgRoomInfo` lookup
  the F8 mark's provenance uses, so nothing in `src/game` moves to supply it.
  57 rooms carry geometry at that pose.
- **One room's primary list carries the cliff, the ground the player walks on,
  the concrete barriers and the kerb together.** Suppressing that room removes
  **72.9%** of the frame's pixels; what is left is the game's own sky pass, the
  ladders and the truck. Suppressing every room at once removes 84.8%. So the
  room is far too coarse a seam: replacing the cliff through it would delete
  the yard the player stands in.
- A **finer** identity does separate it, and it is one the tree already has -
  the **N64 texture number** #47 keys on. At that pose texture `03b5` paints
  **65.3%** of the frame from 92 primitives, and painting it flat (the existing
  `SL_TEX_HILITE`) covers the cliff shell exactly while the ground, barriers,
  kerb, ladders and truck are untouched.

So the original population IS bounded and IS selectable. The blocker is on the
other side.

### The source side is one item per level, with nothing inside it that names a region

The user's source carries exactly **one world bundle per level**: one model
blob, one rendergraph, and **one** command list under a single resource name.
There is no per-room, per-region, per-object or named sub-object structure
anywhere in it - the only names its rendergraph carries are shader constant
names and style names.

It is not opaque: its command list decomposes into **389 draws** over a shared
vertex pool, in three primitive types. But a draw is a **material batch**, not
a region, and nothing in the bundle says which batches are the cliff as
opposed to the yard, the dam wall or the buildings.

Nor does the source supply the correspondence from outside. `docs/texture-packs.md`
already records, from the #47 round, that the source carries **no
original-to-remastered table at texture granularity**, and that the remaster's
own display-mode switch swaps **whole models, paired by item path** - the unit
the source itself provides is the whole level background, which is precisely
the granularity this slice must not use ("do not replace the whole level").

The two sides therefore meet nowhere: the original cliff is a per-texture
population inside a room, the remastered cliff is an unlabelled subset of 389
material batches, and no artefact in either names the other. Choosing a subset
anyway would be a derivation with no authority behind it - the same shape as
the half-float texture coordinate the skydome round had to withdraw - so it is
**refused rather than guessed at**.

### A correction to the source reader, measured

`tools/worldasset/xblaworld.py` finds draws by searching for the byte pattern
`00100100 00000004`. Read against a level bundle that is **wrong**, and the
error is now measured rather than suspected:

- the `.stream` record is a command list of `(byte length << 16 | opcode << 8)`
  headers followed by their arguments, beginning at the offset its own first
  word names. Walked that way it consumes the stream;
- a **draw** is opcode `0x01`, and its first argument is a **primitive type**,
  not a constant: the level bundle uses three of them. The existing pattern
  matches only one, so it sees **46** records where there are **389**, and some
  of the 46 are other records that happen to carry those bytes;
- the buffer resolver alongside it assumes one vertex buffer per draw, ending
  where its index buffer begins. The level bundle uses a **shared** vertex pool
  with many index buffers into it, so that rule mis-resolves.

**The skydome path is unaffected**: its one-item bundle does satisfy both
assumptions. That reader is now structural too (below) and produces a
**byte-identical** skydome bundle, checked by SHA-256 across the change.

---

## The whole static background, as one replacement

The section above is where the first attempt stopped, and it is kept because
it is what chose this one's unit. Its finding was that **no bounded sub-target
exists**: the original cliff is a per-texture population inside a room whose
other geometry is the yard the player stands on, the source side is one
unlabelled bundle of material batches, and no artefact in either names the
other. The remaster's own display-mode switch swaps **whole models paired by
item path**. So the whole background IS the replacement unit, and that is
source-native rather than a convenience.

What ships: under PRESENTATION XBLA the Dam's whole static background is drawn
from a prepared bundle and the game's own room background emission is
suppressed. **The player sees the remastered Dam and plays the original one** -
rooms, portals, collision, AI, props, doors, objectives and scripts are
untouched, and the per-tick state trace is byte-identical across all three
presentations.

### The reader, and what validates it

`tools/worldasset/xblaworld.py`'s `read_level`. Nothing in it searches for a
byte pattern and nothing is validated against itself - every stage is checked
against what it **consumes**.

| stage | how it is read | what says it is right |
|---|---|---|
| the command list | by its own record framing: `(byte length << 16 \| opcode << 8)` then arguments, from the offset the stream's first header word names to the next section the header carries | the walk lands **exactly** on that end - 1481 records, no remainder |
| the draws | opcode `0x01`: primitive TYPE, element count, index-buffer handle | **389** draws, in three primitive types (46 list, 258 **fan**, 85 quad - and never a strip), where a pattern requiring type 4 saw 46 |
| the index buffers | a `(handle, offset, length, kind)` record whose length is the draw's element count times its index width | **387 of 389** resolve; the two that do not are the pair that needs **32-bit** indices, and they resolve against kind 6 with `count * 4` |
| the vertex pools | every index run must begin exactly where some vertex buffer's bytes END | 13 of 14 runs; the 14th continues the previous pool across a 224-byte gap, and inherits it |
| the binding | every index must fall inside the pool it is bound to | 386 of 389 draws; the three dropped are on pools whose layout does not validate |
| the whole thing | the bounds computed from the vertices actually read against the bounds the bundle **declares for itself** | centre and extent agree to the printed digit: `(-3.5, 748.2, -1.7)`, `(12231.3, 8165.8, 12826.3)` |

The shared pool is the point: **342 of 386 draws index ONE buffer of 54651
vertices**, and a second of 77880 vertices is why a 32-bit index buffer exists
at all. "One vertex buffer per draw, ending where its index buffer begins" -
the one-item reader's rule - mis-resolves every one of them.

### The vertex layout is measured, not decoded

The bundle names a vertex DECLARATION per draw (opcode `0x0c`'s first
argument), and a level uses several; the skydome item uses a different one
again. This reader does not decode the declaration. It measures the two
consequences, and both measurements are arithmetic on the bytes:

**WHICH word is the texture coordinate**, from the geometry. Along every
triangle EDGE, take `|world change| / |raw coordinate change|`. A coordinate
gives a consistent positive number because it is linear in position over a
face. The other word is a packed NORMAL, which is CONSTANT over a face - so
the only edges where it changes are the ones between faces, where the position
barely moves and the value jumps, and its ratio collapses. Over **113972**
edges of the main pool the coordinate word reads **0.072** and the normal word
reads **0.000**.

Per EDGE and not per draw, and that correction is the whole of it. A draw is a
material BATCH and every face in it carries its own chart, so the batch's total
span says nothing about the mapping: measured per draw the normal word WINS,
and drawing that is a level of flat-shaded facets with the texture sampled at
one texel per triangle. That is exactly what the first render of this item
showed.

**AT WHAT SCALE**, from the values. A chart mapped across one whole texture
puts its corners on exact multiples of the scale, so the raw values pile up
there. The three most common absolute values in this item are **0** (86365
vertices), **128** (45200) and **127** (42974), and **25.8%** of the non-zero
values are exact multiples of 128 against a 0.78% chance - while only 0.47%
are multiples of 256. One texture is **128 raw units** here. The skydome item's
own declaration uses **16384**, measured independently in the earlier round,
which is why the scale is read per item and never fixed.

Two more things the pixels settle rather than the structure:

- **the colour's first byte is the ALPHA.** It is 0xff on all 132860 vertices
  of the four large pools and 0x80, 0x8b or 0x99 on the small ones. Read as
  alpha that is "opaque everywhere except a few translucent decals", which is
  what a level background is; read as red it would be "the red channel is
  saturated on every vertex of the level", which nothing supports. This round
  read the three bytes after it as R, G, B, and **that half was wrong**: they
  are B, G, R, because the word is byte-reversed like every other field in the
  record. The alpha reading above is untouched by that correction. See
  [The vertex colour](#the-vertex-colour-one-word-in-the-record-was-never-byte-reversed).
- **slot 0 is not always the base image.** One pool of 7081 triangles - every
  tree in the level - names a tangent-space NORMAL MAP in slot 0, whose mean
  colour is `(125, 125, 254)`. Drawn as a base that is a flat violet, standing
  beside every tree, and it is what the first render showed. The base is the
  lowest slot whose image is not one, and "is one" is measured on the decoded
  pixels. No base texture in the item comes close to that mean.

### The world transform, and a correction to the skydome round

A background is WORLD-ANCHORED: it sits where the original sits, through room
changes and portal traversal. **None of the skydome's eye-anchoring applies.**

GoldenEye builds every room's matrix as (that room's own position TIMES the
level scale) MINUS the player's `current_model_pos`
(`bgroomtrans.c:211-218`), so the space the frame's geometry is laid out in is

```
S = position * level_scale - render_origin
```

and the same relation holds for the camera. Measured at the gate yard: the
player's eye reads `(17959.16, 73.15, 17809.95)`, the render origin reads
`(17723.85, -107.00, 17612.57)`, and their difference `(235.31, 180.15,
197.38)` is where the eye is in S, to the last digit printed.

The source authors a level's bundle in the bg data's **own room coordinates** -
the ones before the level scale. So for a bundle coordinate B, with
`k = room_data_float2` for the stage:

| projection | what it consumes | the conversion |
|---|---|---|
| FOLDED (`bondview2.c`'s, view already in it) | `position * k - origin` | `X = B * k - origin` |
| PLAIN (`fr.c`'s; the seam supplies a view from the player's own view-to-world matrix) | `position * k` | `X = B * k` |

**k is read per frame and is NOT a constant across levels** - it is 4.2801 at
the Dam. ONE canonical representation in the asset, converted at draw time into
whichever space the projection in force consumes; nothing is ever stored
pre-folded for whichever room rendered first.

**A correction the skydome round left behind.** That round measured
`#68 skyeye`'s ratio as "exactly 5.0000" and recorded it as the folded
projection consuming world coordinates divided by five. It is an artefact of
`sl_wa_view_origin`, which recovers the eye ASSUMING the view's basis is
orthonormal; the folded view carries a uniform scale of five, so the eye it
returns is one fifth of the eye in the matrix's own space. **Nothing about the
skydome moves** - it is eye-anchored and a uniform scale about the eye changes
no ray, which is precisely why the error could not show there. A world-anchored
asset cannot ignore it, and does not: it takes the scale and the origin from
the game and never from the recovered eye.

**What says the transform is right** is not the derivation, it is two numbers
the game itself supplies. The game reports the floor under the gate-yard
teleport at world y `-94.162` and under the first-alarm one at `-107.002`. Cast
a vertical ray through the prepared geometry at those two columns:

| column | the game's floor | the bundle's surface | difference |
|---|---|---|---|
| gate yard | -94.162 | -93.66 | **0.5 world units** |
| first alarm | -107.002 | -107.00 | **0.0 world units** |

Before the correction the same two columns crossed **no surface at all**.

### The render seam, and what is suppressed

Drawn at the same seam the skydome uses - the frame's first world triangle,
after every clear - with the sky FIRST (it carries no depth and is the
backdrop) and the background over it. It takes the same **aperture** rule: the
pass runs inside the first room's display list under that room's portal window,
and a background covers the whole picture, so the rectangle is the VIEWPORT and
the box and enable bit are restored exactly on the way out.

Unlike the skydome it **tests and writes depth**, because it has to occlude and
be occluded by the props, doors and characters the game still draws. The one
liberty is at the FAR plane: the item is larger than the game's frustum, so a
clip z past w is CLAMPED to w rather than clipped away - x, y and w untouched,
so no screen position and no perspective-correct interpolation moves, and the
near plane still clips. (A clamped z cannot be ordered by the depth test, so
a frame on which part of the item lies past the far plane draws that part in
a depth range of its own first - "Beyond the game's far plane" under Runway's
background below.) Blended materials take an alpha test as well as the
blend, because a tree here is a quad whose image is transparent almost
everywhere and the colour under that transparency is not black.

Geometry goes through **vertex arrays**, one `glDrawElements` per material:
271080 indices is 27 times the dome's, and 80 draw calls is what keeps the pass
affordable.

**WHAT IS SUPPRESSED, exactly.** The final native visual emission of ROOM
BACKGROUND geometry, identified by the renderer's own room identity - the one
the game itself branches with, read from the depth-1 display-list operand
through the same `g_BgRoomInfo` lookup the F8 mark uses. Props, guards, doors,
the view model, the HUD, effects and 2D all carry no room number there and are
never reached. The display list still executes in full: every vertex loads,
every matrix applies, every render mode takes effect, the interpreter's own
bookkeeping runs unchanged. No game data, room membership, collision, portal,
prop list, AI visibility or save state is read or written.

**THE VALIDITY GATE.** The flag that arms suppression is cleared at every
frame's start and set in exactly one place: the end of a background pass that
has already loaded, validated, uploaded and DRAWN a complete replacement this
frame. So a missing file, a malformed one, a truncated one, a failed image
upload, a stage with no item path, and any presentation but XBLA all leave the
original drawing exactly as it does today. Measured: the missing, the malformed
and the truncated cases produce **one identical picture**, and it is the
original world.

**Collision stays the original game's.** A remastered surface whose shape
differs from the original's is a VISUAL difference only, and the disagreement
is evidence rather than a defect (project rule 5). The two floor columns above
are that evidence: the remastered floor sits 0.5 and 0.0 world units from where
the player actually stands.

### The preparation command

```
.\tools\windows\prepare-xbla-assets.ps1 -DamBackground
```

The Dam by name (the same as `-Level dam -Background`; any stage the source
holds a background for is `-Level <stage> -Background`), deterministic, rerunnable and idempotent
on the same stamp the skydome uses, reading the same gitignored private source
and writing the same gitignored output tree. What it produces, measured:
**140066 vertices, 271080 indices (90360 triangles), 72 materials, 80 meshes
after merging adjacent same-material draws, 68 of the item's 73 images.**

### What is not right yet

- **Three pools do not validate** and their three draws are dropped and
  counted: one of stride 12, which has no room for both tail words, and two of
  stride 36 whose first word is not a position.
- Some cliff facets read **untextured white** at a steep look-up, and the
  skydome's lower rim shows as an arc where the background does not reach it.
- The lighting is the remaster's baked vertex colour and its base image, with
  no second map and no shader. Whatever remains after that is **#48's**, and no
  brightness compensation is applied here.

---

## What the playtest rejection measured

A playtest of the section above returned **"this is completely broken"** and
six marked poses with it (2026-09-23, Dam, six marks across the gate yard, the
cliff, and two tunnels). All six reproduce deterministically by teleport on
this branch. What they measured is not what the round that shipped them
claimed, in both directions - some of it was worse than claimed, and the
largest part of what was visible in play turned out not to be this feature at all.

### Four claims, withdrawn

**1. "The whole Dam background replacement is COMPLETE."** Withdrawn. It was a
claim about a pipeline reaching the end of its own stages, and it was read as a
claim about the picture. A manual playtest is the only thing that can say complete,
and it said the opposite. The mechanism works; the result was not accepted.

**2. "There is no duplicate cliff, ground or dam wall BY CONSTRUCTION."**
Withdrawn: suppressing the original room background proves nothing about the
bundle's own contents. Measured instead - sample 400 columns over the region
the playtest run covered and ask how many carry two coplanar surfaces from **different
meshes**: **2 of 268** columns that carry any surface at all (mesh 58, an
alpha material, over meshes 26 and 27, both at world y -107.002). So the
conclusion happened to hold, but at 1% rather than at zero, and now for a
reason that was measured rather than assumed.

**3. "The transform is right."** The evidence for it was two floor columns, and
two points cannot establish a scene contract across a stage. It is withdrawn
*as an argument* and replaced by one that can carry the weight - see
**The control that does settle placement** below. The transform itself survived
the stronger test.

**4. "Visual and collision may disagree because the simulation trace is
byte-identical."** Withdrawn as a defence. The trace being identical says the
simulation is untouched; it says nothing about whether the player can read the
picture. Trace identity is a **precondition**, never an acceptance.

### The control that does settle placement

The user's source carries the level background **twice**, under the same item
path: the remastered model and the **original** one - the pairing the
remaster's own display-mode switch swaps between. That second model is the N64
level's own geometry, in the same container and the same frame. So it is an
oracle, and it costs nothing to use: prepare it through the **identical**
pipeline, hand it to the **identical** render seam, and ask whether the game's
own world comes back.

It does. At all six marks the original model, drawn as the replacement,
reproduces the game's own world - the same cliff silhouette in the same place,
the same kerb, the same barriers, the same tunnel mouth, and the guards
occluded exactly where the game occludes them. And per ray rather than per
picture, a 7x5 fan of view rays at the first mark:

| | rays hitting the same surface at the SAME distance | differing by > 15% |
|---|---|---|
| remastered model vs the source's own original model | **34 of 35** | 1 |

So the reader, the shared vertex pools, the world transform, the level scale
`k = 4.2801`, both space conversions, the aperture, the depth handling and the
suppression are correct **end to end**, and that is now established by
reproducing a known world rather than by two floor heights. The one ray that
differs is foliage the remaster adds.

Two more measurements in the same direction. Sampling columns across the
region the playtest run covered, the two models' topmost surfaces agree within **5
world units on 99%** of them (median 0.00, max 73). And the game's own floor,
read from the teleport seam, is **-107.002** at every one of the six marks,
which is exactly what the prepared bundle puts under the player at each. There
is **no floor displacement at any playtest mark** - so "in a ditch" and "suspended
in air" are not floor geometry, and the next round should not start there.

### What was actually on screen

Two of the reported symptoms are **not this feature**, and the instrument that
settled each is a control rather than an argument.

**The horizontal bands across the tunnel walls are the game's own light
shafts.** Render the same mark with the world-asset provider pinned dark and
the bands are still there, in the same place. Not a z-fight, not a bleed, and
nothing the replacement does.

**The blue blocky shapes at the tunnel mouth are the game's own props.** With
the replacement loaded, validated and suppressing, but drawing **no mesh at
all** (`SL_BG_ONLY` naming nothing), the whole world background vanishes and
those shapes remain, floating in the sky beside the guards. They are crates,
drawn by the game, wearing a texture from #47's set. Whatever is wrong with
them is a texture question, not a world-asset one.

That null control is worth keeping as a habit: it separates "the replacement
drew this" from "the replacement failed to suppress that" in one run, and it is
what stopped two rounds of this investigation before they started.

### The scene-assembly contract, recovered

The round that shipped the replacement flattened the item's command list into a
linear list of draws. That was the suspected first wrong stage. It is not,
and the contract is now read rather than assumed.

The `.stream` command list is a **graph**, not a list. Walked by its own record
framing it is 1481 records in 18 opcodes, and three of them are control flow:

| opcode | count | what it is | what says so |
|---|---|---|---|
| `0x16` | 113 | an **N-way selector**: `count << 24`, then that many child offsets | all 113 carry count 3, and **every one** of their 339 child offsets lands exactly on a record |
| `0x19` | 226 | an unconditional forward jump | all 226 land exactly on a record, and **226 = 2 x 113** - the two non-final arms of every selector, the third falling through |
| `0x1d` | 1 | the end | zero arguments, at the end of the record area |

So the item is a chain of three-way choices. The question that matters is what
they choose **between**, and executing the graph answers it: take arm 0 at
every selector, then arm 1, then arm 2, and count the draws each reaches.

| | draws reached |
|---|---|
| arm 0 | 389 |
| arm 1 | 389 |
| arm 2 | 389 |
| **common to all three** | **389** |
| reachable by no arm | 0 |

**The selectors are material and shader permutations, and they gate no
geometry at all.** Every draw is on every path, so all 389 are meant to be
simultaneously active, and the flat walk emits exactly the right set. The same
holds for the original model (156 draws, 99 selectors) and for the skydome item
(2 draws, 6 selectors), which is what makes it the format's rule rather than
this item's accident.

Nothing else in the bundle encodes what a wrong scene would need. Searched for
and **not present**: any per-draw or per-node transform (the only matrices the
item names are the shader constants `c_modelview` and
`c_modelviewprojection`, which the engine supplies); any parent or pivot node;
any room, sector or region id; any draw-enable or conditional state; any LOD or
alternate-generation record; any bounding volume per draw. The bundle carries
one model, in one space, drawn in one order.

**So "should this draw be here, and where?" is answered: yes, all of them, and
exactly where they are being put.** A later round should not re-derive this.

### The defect this round fixes: CUTOUT is not ALPHA

What the flat walk got wrong is not which draws exist, it is **one bit of state
per material**.

The preparation tool decided a draw blends if "its style says so **OR** its own
image carries alpha", and wrote that down as the one derived thing, with the
note that being generous "costs sorting, never a black box where a decal should
be". It is not free. A blended mesh draws with **depth writes off**, so every
draw that reached that state only because its image has an alpha channel
stopped occluding anything at all.

On the Dam that is **39050 triangles - 43% of the item**: the level's foliage
and its detail shell, drawn with no depth, with the world behind free to draw
straight through them.

The source separates the two cases and it is now read rather than derived. A
draw whose style low byte is **2** is genuinely translucent - glass, a light
shaft, a decal - and must not occlude. A draw whose style says opaque while its
image carries alpha is a **CUTOUT**: the alpha is a *shape*, and wherever it
survives the alpha test it is as opaque as a wall and must write depth. On this
item that is 22 draws of the first kind against 39050 triangles of the second.

`SL_WA_BLEND_CUTOUT` carries the distinction to the renderer, which now sets
three states rather than two, differing in one bit - the depth mask. **The
LAYOUT does not move, so the version does not either**: widening the set of
accepted values can never make an older bundle invalid, exactly as the
background class did, and the self-test checks that a bundle written before
CUTOUT existed still parses beside one written after.

Measured at the playtest's own marks, changed pixels against the rejected build:

| mark | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| pixels changed | 2.36% | 3.13% | 3.00% | 0.00% | 0.00% | 0.00% |

The three that move are the three outdoor marks, which are the ones with
foliage in frame; the three that do not are a tunnel interior, a floor looked
at straight down, and a tunnel with no foliage in view. **This is a real defect
with a source-native fix, and it is not the whole of what was visible in play** - it
is the first one this round could demonstrate, and it is reported as that.

### Draw isolation (diagnostic - no feature behind it)

The instrument the section above needed, alongside `SL_WR_*`. Those name
ROOMS; the replacement's meshes are not rooms, so nothing could ask which mesh
drew a surface. Two variables, each a comma-separated list of mesh indices or
`N-M` ranges:

```
SL_BG_ONLY=<list>     draw ONLY these meshes of the replacement
SL_BG_HIDE=<list>     draw every mesh EXCEPT these
```

They change which meshes the pass emits and nothing else. The **validity gate
is deliberately untouched**, so the original stays suppressed and the
comparison is between two replacement renderings rather than between a
replacement and the original coming back - which is what makes
`SL_BG_ONLY=<nothing>` the null control described above. One cached getenv
pair; nothing unset costs anything.

### The next blocker, with its evidence

Not fixed here, and named so the next round starts on it rather than on scene
assembly.

**The texture coordinate's scale is still derived, and the pool it matters most
for has the weakest evidence for it.** The divisor is measured per pool from
where the raw values pile up on multiples of a candidate. For the Dam's main
pool - **351 of 386 draws and 43812 triangles** - only **7.2%** of values land
on the chosen scale of 128, against the 25.8% that justified that scale for the
item as a whole, and the result is **171 world units per texture repeat** with
texture coordinates running to +-74 and -158. That is the grossly stretched
ground visible in play looking down at the fourth mark. A second pool reads
**2430** world units per repeat on 13.3% evidence.

Deciding this by eye would be a visual heuristic with nothing behind it - the
same shape as the half-float coordinate this feature already had to withdraw
once. It needs an authority, and the bundle does not carry one: **the container
encodes no sampler-to-resource link**, which is recorded here and in
`docs/texture-packs.md` from the #47 round. The one bounded reference
measurement - asking the real remaster what it samples at one of these
surfaces - is the next step, and it is a deliberate one rather than something
to drift into.

Two smaller ones behind it: the skydome's lower rim still shows as a hard edge
where the background's distant terrain does not reach it (visible at the second
and third marks, which were marked 1.05 degrees apart), and the
replacement reads brighter and flatter than the world it stands in, which is
**#48's** and gets no compensation here.

### What this round did NOT reproduce

The playtest was played with live input. This investigation can only teleport - input replay
is not a tool it has - so every measurement above is a **static pose**. Two of
the reported symptoms, "movement that visually jumps" and "in a ditch at some
spots and suspended in air at others", are properties of motion, and six still
frames cannot refute them. What can be said is bounded and should be read that
way: the projection branch does **not** flap (one or two changes per run, then
stable), the render origin the pass uses agrees with the matrix in force to
0.01 world units, and the floor is exact at all six marks. If displacement is still
visible while walking after this, the next evidence needed is a
recorded run rather than another mark.

---

## The matched-presentation pair, and the registration contract it exposed

The round above ended by asking for different evidence, and the next playtest supplied
exactly the right thing: **two marks at the same camera under different
presentations**, plus a third somewhere else. Marks 2 and 3 of the 2026-09-23
run carry the **identical** eye `(16783.068, 60.311, 6851.682)`, the identical
heading, the identical room and the identical render origin, 656 frames apart -
one with the room background drawing (every room's list emits triangles:
ORIGINAL) and one with it suppressed (every room's list `DREW NOTHING`: XBLA).
So the gameplay state is not a variable at all and presentation is the only one.
Playtest verdict: *"the last 2 marks shows how my position changes between
presentation mode"*, and at the first mark *"models are in the wrong spot with
the player floating too"*.

That pair is what the six static floor samples could not see, and it settles the
previous round's remaining doubt in the opposite direction to the one that round
guessed.

### The floor evidence was vacuous, and here is why

Both prepared bundles put a surface **167.31 world units under the eye at
y = -107.00** at mark 1 and at mark 2 - the same distance, the same height, the
same mesh (mesh 2), at two poses over ten thousand units apart in completely
different parts of the level. That is not registration; it is **one flat plane**
spanning the stage, and the Dam's walkable surfaces happen to sit on it. The
"the game's floor is -107.002 and the bundle puts a surface at -107.00" check
could not have failed whatever the horizontal placement was. It is withdrawn as
evidence.

### The first wrong stage, measured three ways

**The bundle's world placement was never the problem.** Ray-cast offline at
`v * level_scale`, the prepared geometry reproduces the game's own world from the
playtest's recorded eye. What was wrong is the last step: **the space the matrix in force
consumes**.

| measurement | result |
|---|---|
| `sl_wa_view_origin` on the FOLDED matrix, against the game's own eye | recovers exactly `(eye - render_origin) * 0.2` - ratio **5.0000** at both marks |
| the game's own room vertices, from the mark's `source` and `eye` columns | reach the interpreter scaled by `level_scale * 0.2`, i.e. **k/5**, measured on three independent triangle edges to **1.0000** |
| ray-casting the prepared bundle from `render_origin + (eye - render_origin) * 0.2` | reproduces **the picture this pass was drawing**; casting from the true eye reproduces **the game's own picture** |

The third is the one that cannot be argued with, and the instrument for it was
already in the tree: the source carries the level background **twice** under one
item path, and the ORIGINAL-generation model is the N64 level's own geometry.
Prepared through the identical pipeline and drawn through the identical seam it
came out in the **same wrong place** as the remastered one - which is what
retires the previous round's reading that "the original model, drawn as the
replacement, reproduces the game's own world". That round compared the two
SOURCE models with each other, where they agree trivially, and never compared
either against the game.

### The contract: the stage's VISIBILITY scale

`bg.c:1088-1091` reads `levelinfotable[...].visibility` and hands it to
`matrix_4x4_7F058C4C`, which stores `65535 * visibility` as the fixed-point
conversion's own multiplier (`matrixmath.c:478`); `matrix_4x4_f32_to_s32` then
applies it to the first three columns of **every** matrix the game converts
(`matrixmath.c:495-511`), and `bondview2.c:8798-8800` applies it again,
explicitly and by name, to the player's own world matrix. So the space the
frame's world matrices consume is

```
X = (position * level_scale - render_origin) * visibility
```

and the seam was supplying only the first half of it.

**It is 1.0 on every stage but two.** Of the entries in `levelinfotable`, only
**DAM** (`bg.c:291`) and **SURFACE** (`bg.c:294`) carry `0.2`; the rest carry
`1.0`. That is the whole reason this could ship unnoticed: the skydome is
eye-anchored and a uniform scale about the eye changes no ray, so the error
cannot show there at all - and the Dam is the one stage the background asset
exists for. The skydome round measured this number and wrote it down correctly
("the folded projection consumes world coordinates divided by five"); the
replacement round then reclassified it as an artefact of `sl_wa_view_origin`'s
orthonormality assumption and dropped it. That reclassification was the defect.

### What omitting it did, in world units

Leaving the scale out draws the level `1/visibility` times its size **about the
render origin**, which is the player's own room's origin - not about the eye. A
uniform scale about a point that is not the eye moves the apparent viewpoint:
the picture becomes the world seen from `render_origin + (eye - render_origin) *
visibility`, with every distance multiplied by `1/visibility`.

| | mark 1 (dam top) | mark 2/3 (loading yard) |
|---|---|---|
| eye offset from its own room's origin | `(-605.6, -697.3, 695.0)` | `(-645.5, -196.5, 722.6)` |
| apparent viewpoint displaced by | **924.7 units**, 557.8 of them vertical | **790.9 units**, 157.2 vertical |
| ground under the player, drawn at | **3625.6** units below | **1622.5** units below |
| ground under the player, correct | **167.3** | **167.3** |

Mark 1 is the worse of the two because its room origin is far below the player,
and mark 1 is exactly where the playtest report wrote "the player floating too". The
displacement is a function of where the player stands relative to their room's
origin, which is why it read as *"my position changes between presentation
mode"* rather than as a uniformly wrong picture.

### The fix, and the before/after geometry

One term, read from the game, in `src/native/sl_world_assets.c`
(`sl_wa_visibility_scale`, the same read-only shape as `sl_wa_level_scale`), and
applied once per branch in `bg_draw`: the FOLDED matrix already carries the
scale, so the point handed to it is scaled; the PLAIN branch's view is built by
the seam, so the scale goes on **that view's output columns** instead - w
untouched, so no screen position moves there and only the eye-space depth
changes, which is what has to agree with the geometry this pass is depth-tested
against. Applied once either way, never twice. **Nothing in `src/game` is
touched.**

Geometry at the playtest's own poses, ray-cast against the game's own world - a
35-ray fan over the playtest's actual view volume rather than a global column
percentage:

| | rays within 15% of the GAME's own surface | median error |
|---|---|---|
| mark 1, before | **0 of 33** | 759% |
| mark 1, after | **33 of 33** | **0.0%** |
| mark 2/3, before | 1 of 34 | 870% |
| mark 2/3, after | **33 of 34** | **0.0%** |

and the local volume, in world units, at the same two poses:

| | mark 1 before / after | mark 2/3 before / after |
|---|---|---|
| floor below the eye | 3625.6 / **167.3** | 1622.5 / **167.3** |
| wall to the left | 5340.1 / **1039.3** | 5035.0 / **308.9** |
| wall to the right | 7623.2 / **1531.0** | 8134.9 / **2231.7** |
| forward | 24151.8 / **3986.2** | 16123.1 / **4504.2** |

The witness prints the number the stage declares beside the number recovered
from the matrix, so the contract is checked rather than asserted: at the Dam
`SL_BG_TRACE=1` reads `visibility 0.2000 (1/5.0000) measured 1/5.0000`. A
disagreement between those two is this fault returning, and it is a number
rather than an argument.

`SL_BG_NOSUPPRESS=1` is the picture form of the same thing: with the original
and the replacement both drawn, they now coincide - the barriers, the tower, the
tunnel mouth and the platform overlay the originals instead of standing apart.

### What this does NOT fix

- **The texture coordinate's derived scale is still parked**, deliberately. The
  stretched ground at a steep look-down is the blocker named above and it is
  unchanged by this round: a wrong UV scale cannot move a wall, and this round's
  evidence was walls moving.
- **Props, guards and collision are untouched**, by design and by rule 5. The
  simulation trace is byte-identical across all three presentations and across
  this change (936 lines, 80802 bytes, one hash over five runs), which is a
  precondition and never an acceptance.
- The lighting is still the remaster's baked vertex colour with no second map,
  which is **#48's**.

### Controls

| | result |
|---|---|
| ORIGINAL, both playtest poses, this build against the branch head | **byte-identical** framebuffers |
| COMMUNITY HD, both playtest poses | **byte-identical** |
| per-tick simulation trace, 400 frames, ORIGINAL / COMMUNITY / XBLA, both builds | **one hash**, 936 lines, 80802 bytes. No re-baseline |
| skydome, background drawing no mesh | identical picture before and after; 3400 triangles per frame, sampler unchanged |
| missing / malformed / truncated bundle | **one identical picture**, and it is the same picture the provider pinned dark produces |
| Facility under XBLA | `lookups=0` - no Dam background requested or drawn |
| the visibility contract, synthetic | the eye comes back out of a visibility-scaled folded matrix at exactly that scale; applying it puts a world point on the same screen position to 0.000001 in ndc; omitting it misplaces near geometry by 0.50 and 0.17 while distant geometry moves 0.05 - which is why this read as props in the wrong place rather than as a wrong picture |

---

## The world gap: a FAN was being drawn as a RIBBON

A playtest of the registration repair above returned *"plays much better and
looks a lot better"* and *"I can traverse it normally"* - and five new marks
with it, reporting **gaps in the world**, a skydome that looks wrong, "pathing
lines", z-fighting on the ground, and the Dam's water. This section is the
first of those, and only the first: it is the WORLD GAP.

### The gap, and the fork it forced

Mark 1 is the sharpest of the five. Standing on the dam top looking down at
45.9 degrees, **14.0% of the frame is a hard-edged lens of sky through ground
the player can walk on**. Every room's list `DREW NOTHING` (the replacement is
suppressing), the centre pixel is the environment colour at **window depth
exactly 1.0** - so nothing wrote depth there - and the mark's own
`[candidate-draws]` says no submitted triangle reaches the centre. The picture
is not a z-fight, not a bleed and not a wrong colour: the surface was never
rasterised.

That leaves one question worth asking, and it has to be asked before any state
is touched: **does a triangle covering that ray EXIST in the prepared bundle?**
Cast offline against the prepared geometry at `v * level_scale`, from the mark's
own eye, on a grid over the whole viewport:

| | rays |
|---|---|
| the grid, in view | 551 |
| screen is ground, and the bundle has a triangle | 464 |
| screen is sky, and the bundle has a triangle | **0** |
| screen is sky, and the bundle has NO triangle | **87** |

549 of 551 agree, so the offline cast reproduces the picture and can be trusted
to answer the question - and the answer is that **no covering triangle exists**.
The defect is an omission, not rasterisation or state. Culling was never a
candidate: this pass has run with `GL_CULL_FACE` disabled since it shipped.

### Where it is NOT, measured before it was found

Two plausible causes were eliminated rather than argued away.

**The three dropped pools are not it.** The reader refuses three of the item's
14 vertex pools (one of stride 12 with no room for both tail words, two of
stride 36 whose first word is not a position), and their three draws carry
20004 triangles. Decoded anyway and cast at the same rays, they cover **0 of
87**: the largest of them, 19626 triangles, is a flat tessellated grid at world
y -824 spanning the reservoir end of the level, nowhere near this hole.

**The source's own ORIGINAL-generation model is not a get-out either.** It
lacks the surface at those rays too - `0 of 87` - which for a moment looked
like "the remaster simply has a hole here". It is not: rendering the same pose
under PRESENTATION ORIGINAL paints **solid ground at every one of those pixels,
with zero sky**. Both prepared models disagreeing with the game at the same
rays is not two holes; it is one reader reading both of them the same wrong way.

### The cause: one enumeration, read from two tables

A draw record carries a primitive TYPE, and this item uses three: 4, 5 and 13.

**Type 13 is a QUAD LIST, and a quad list exists only in the Xbox 360's
extension of `D3DPRIMITIVETYPE`** - desktop Direct3D 9 stops at 6. In that same
Xbox table **5 is a TRIANGLE FAN**, and a triangle strip is 6, which this item
never uses. The reader took 13 from the Xbox table and 5 from the desktop one,
where 5 is a strip, and expanded **258 of the item's 389 draws as ribbons**.
Taking two meanings from two tables is the defect; the item's own set of types
(4, 5, 13 and never 6) says which table governs.

**Why nothing could catch it.** A fan of n elements and a strip of n elements
both make `n - 2` triangles. So `primitive_triangles` is identical either way,
and so is everything that rests on it: the index buffer still resolves by exact
consumed length, every index still falls inside its pool, and the bounds
computed from the vertices read still agree with the bounds the bundle declares
for itself. **The Dam bundle carries 140066 vertices and 90360 triangles under
BOTH readings.** Only the topology differs - and a wrong topology over the same
points tiles a zig-zag that leaves the rim's own area bare, which is exactly a
lens-shaped hole bounded on every side by the draws that should have closed it.

### What says it is right, and what says it is not just a bigger blanket

Three measurements, and the second is the one that matters.

| | prim 5 as a ribbon | prim 5 as a FAN |
|---|---|---|
| mark 1's 87 gap rays, covered | **0** | **87** |
| of the 464 rays that already hit, still the SAME surface at the SAME distance | - | **464 of 464, none moved** |
| rays that were covered and are now bare | - | **0** |
| triangles in the bundle | 90360 | **90360** |

The middle row is the control that separates a repair from a blanket: geometry
invented to close a hole would occlude the rays that were already right, at
shorter distances. **Not one of them moved.** And the source's
ORIGINAL-generation model - a second, independent model of 11903 triangles -
reproduces the identical result on the identical rays, `0 of 87` against
`87 of 87` at an unchanged triangle count.

### The picture, at the playtest's own five marks

Teleport-sampled, one bounded run per pose, cheats
`invincible,invisible,maxammo`, the same camera, presentation and resolution
before and after, at the frame that reproduces the playtest's own framing:

| mark | sky through solid world, before | after |
|---|---|---|
| 1 dam top, looking down | **14.16%** of the frame | **0.00%** |
| 2 loading yard | 0.28% | 0.07% |
| 3 under the pumps | 0.04% | 0.00% |
| 4 tunnel interior | 0.00% | 0.00% |
| 5 walkway, looking down | **2.17%** | **0.00%** |

Geometrically rather than as a percentage: at mark 1 the lens of sky is
replaced by the snow surface its neighbours already stood on, at the same
distance those neighbours were already at. At mark 5 the blue wedges are
replaced by **a whole chain-link fence** - the draws that make its panels are
fans, and as ribbons they were shredded into slivers with sky between them.
Mark 3's small blue wedge at the pillar base closes, and the pillars' black
untextured wedges become concrete.

**It is not camera-specific.** Moved 85 units and rotated +/-14 degrees about
the mark, three nearby poses: sky through solid world **7.53% -> 0.00%**,
**15.09% -> 0.00%**, **15.33% -> 0.00%**.

### What this fix is, and what it is not

One line of topology in `tools/worldasset/xblaworld.py`. **No engine code
changes at all** - `src/` is untouched, the renderer, the seam, the suppression
and the validity gate are exactly as the registration round left them. What
changes is how the preparation tool connects the vertices it already read.

Still open, and none of it addressed here: the **skydome** at marks 2 and 3, the
white overlay patches still on the ground at mark 3, the ground **z-fighting**,
the **water**, and the **UV scale**, which stays parked - a wrong coordinate
cannot leave a hole where a triangle should be, and this round's evidence was
triangles that were not there.

---

## The coplanar pass: the source has a DECAL group and the pipeline dropped it

A playtest of the fan repair returned a run of eight marks and seven reports,
and the first of them - *"z-fighting on the bridges at the start"* - is what
this section closes. The others are classified at the end of it and not fixed.

### The witness, and the instrument it needed first

Mark 1 of the 2026-09-23 21:52 run: the two ramps and the walkway at the level
start, looked down on at 52.3 degrees. The bridge deck carries a dark overlay
drawn as a half-tone STIPPLE rather than as a surface.

**The first thing measured was the measurement.** Two captures of that pose,
same build, same settings, differed by **75% of their pixels**. The VI is a
wall clock by default (`sl_ultra_shim.c`, `SL_VI_CATCHUP`), so the number of
retraces elapsed before a given pumped frame depends on how fast the machine
ran: the two runs reached frame 123 at read index **121 and 107**, fourteen
game ticks apart, and the teleported pose decays over those ticks. With
`SL_VI_CATCHUP=0` the frame number is the tick number and two independent runs
of the same pose are **byte-identical**. Every before/after below rests on that;
without it the first hide-a-mesh comparison read 83% changed where the truth was
2.24%.

### Both participants, named

Cast the recorded playtest eye against the prepared geometry and collect every
surface each ray crosses, not just the nearest - a nearest-hit cast can only
ever report the winner of a fight. At mark 1:

| | |
|---|---|
| the overlay | one mesh, material 56, image 56, translucent, a 28-triangle quad-list patch spanning 932 x 27 x 893 world units over the walkway |
| the base | the opaque ground (mesh 2) and a second opaque mesh (22) |
| separation | **0.0000** world units, and 0.0000 .. 0.0012 against the second |

And the shape of the defect is in the cast itself: **the overlay is nearer on
21 rays and further on 21 others, at a measured separation of zero.** Which of
two coincident surfaces is in front flips from ray to ray by floating-point
noise. That is the fight, seen in the geometry before any GL state is involved.

Hiding that one mesh at the playtest's own frame removes the stipple and leaves a
clean rusty grating: **2.24% of the frame, confined to the walkway.**

### The competing explanation, refused by measurement

The renderer applies `GL_GREATER 0.33` to translucent materials as well as to
cutouts, and a decal whose alpha sits near 0.33 would be punched into a
texture-space dither by that test alone - the same picture from a different
cause. The histogram settles it: **image 56's alpha is binary** - 42.0% at
exactly 0, 55.9% at exactly 255, deciles 0.00 0.00 0.00 1.00 1.00 1.00 1.00.
There is no intermediate alpha for a threshold to stipple. (That test is *not*
innocent elsewhere - see the tunnel, below.)

### What the source carries: a style word that is a BITFIELD

A draw stands under the last op-`0x15` record before it, whose second argument
is a style word. The preparation read its **low byte** - the blend - and threw
the rest away. The Dam's bundle uses eight of them:

```
0x2701  0x2702  0x2712    0x4701  0x4702  0x4711    0x4b01    0x6701
```

The high half is a bitfield rather than an enumeration: `0x2000` and `0x4000`
occur alone and together, and `0x6701` is exactly `0x2000 | 0x4701`, which an
enumeration cannot do.

**What `0x2000` selects, measured three ways** - the source carries no string
for it, so it is not taken on the name:

| | |
|---|---|
| size | 17 of 389 draws, **254 of 90360 triangles**, against 71066 of world geometry under `0x4000` and 39044 of foliage under `0x4000 \| 0x0800` |
| shape | every one a small quad-list patch, 2 to 198 triangles; the ones that lie flat do so at a constant world y |
| **position** | over the playtest's eight marks, every ray that crosses two surfaces within 0.05 world units: **652 of 655 have exactly one of the two in this group** |

The three that do not are two ordinary world meshes that happen to touch (2
rays) and a foliage mesh grazing a wall (1 ray). **No ray has two.** That is a
prediction a scan could have falsified and did not.

It also recovers by rule what two earlier rounds found by hand, one at a time:
the mesh lying on meshes 26 and 27, the mesh lying on mesh 2 at the fourth
mark, and this round's walkway overlay are all in this group and nothing else
is.

### What the source does NOT carry, also measured

The item's own vertex programs declare **`c_zadjust`**, one float4, immediately
beside `c_modelviewprojection`, in **36 of its 42 programs - exactly the 36
that declare the projection matrix**. So the remaster's vertex stage takes a
per-draw CLIP-SPACE DEPTH ADJUSTMENT, by name.

Its VALUE is not in the bundle. Every constant-upload record in the item's
command list was read - 189 of them, three distinct argument shapes, two
registers, values `(1,1,1,1)`, `(0,0,0,1)` and two lighting vectors - and none
carries a depth term; **no block under the decal style uploads a constant at
all**. The bundle names the pass and names the mechanism, and the number is the
engine's.

So the claim the previous round made - *"drawn exactly coplanar with no
ordering or offset contract behind it"* - was half right and is narrowed: there
is no ordering contract to recover because **the ordering was already correct**
(below), and there is a depth contract, named but unvalued.

### Draw order was never the fault

Checked before any depth term was considered, because a bias applied over a
reversed order would have been a second bug hiding a first. The preparation
appends draws in the source's own command-list order and merges only adjacent
same-material runs; the renderer draws meshes in that order. The walkway
overlay's draw is at stream offset `0x540c`, its two bases at `0x53c` and
`0x17d8` - so the overlay is issued **after** both, in the source and in
Sightline alike. Order cannot separate two surfaces at the same depth, which is
what makes this a depth contract rather than a sorting one.

### What ships

**One bit, carried end to end.** `SL_WA_MAT_DECAL` in the material's `flags`
word - which the parser previously required to be zero. The layout does not
move, so **the version does not move**: a bundle written before it carries zero
there and still parses, exactly as the CUTOUT widening did, and a bit this
version does not define is refused rather than ignored.

The flag is part of the **material key**, not a field written afterwards, and
that is load-bearing: adjacent draws of one material are merged into one mesh,
so a key that ignored it would merge a decal draw into the ordinary draw beside
it and hand both the same state. Three of the Dam's meshes sit exactly on such
a boundary. The bundle goes from 72 materials and 80 meshes to **79 and 84**;
its vertices and triangles are **unchanged at 140066 and 90360**.

At draw time, and only around a decal mesh's own `glDrawElements`: a polygon
offset, enabled immediately before and disabled immediately after, at **the
magnitudes this tree already applies for the RDP's `G_RM_ZMODE_DEC`** - the
same contract on the original renderer's own decals, shipping there since
B-122. Not a number tuned against a picture. Depth writes stay off for a
translucent decal, so pulling it forward can only win the compare against the
surface it was authored to lie on; it can never occlude anything.

`SL_BG_DECAL=0` draws the pass with no priority - the same kill-switch idiom as
`SL_Z_LEQUAL` and `SL_TREE_ZWRITE`, and for the same reason: a depth-state
change is only believable if both semantics can be captured back to back on one
binary. Every before/after below is that one flag.

### The picture

A fight is one-pixel-period alternation, so it has a number: the mean
`|I(x) - I(x+1)|` over the decal's own INTERIOR (eroded three pixels, because a
decal's edges are difference that is supposed to be there - counting them would
report a decal that merely starts being drawn as a worse stipple).

| pose | decal pixels | stipple, pass off | pass on | |
|---|---|---|---|---|
| **mark 1, the playtest's own framing** | 36311 | 14.84 | **6.76** | **-54.5%** |
| +85 units | 22201 | 14.19 | **6.46** | -54.5% |
| +85 units, +14 deg heading | 39309 | 12.48 | **5.68** | -54.5% |
| -85 units, -14 deg heading, +8 deg pitch | 8079 | 13.15 | **6.01** | -54.3% |
| mark 4 | 35993 | 6.56 | **3.56** | -45.7% |
| mark 5, where the truck parks | 19529 | 11.17 | **5.65** | -49.4% |
| mark 7, the dam top | 18070 | 12.84 | **11.79** | -8.2% |

The three nearby poses are the control that says this is not camera-specific,
and they land within 0.2 points of the primary. At marks 4 and 5 the decals
were mostly LOSING the fight and reading as a wash in the ground; they now draw
as coherent surfaces.

### Controls

| | result |
|---|---|
| the new build with the pass disabled, against the pre-change build and bundle at the playtest's frame | **byte-identical, 0 pixels** - so the parser widening, the material split and the four extra draw calls move no pixel, and the whole 3.46% at the witness is the pass and nothing else |
| ORIGINAL, with and without a world-asset root | **byte-identical** |
| COMMUNITY HD, same | **byte-identical** |
| ORIGINAL against COMMUNITY HD | 90.3% of pixels differ - so the two above are not vacuous |
| per-tick trace, 400 frames, ORIGINAL / COMMUNITY HD / XBLA / XBLA with the pass off | **one hash**, 901 lines, 80802 bytes. No re-baseline |
| a bundle whose material flags carry an undefined bit | INVALID, one line, and the original world draws - the same picture as a missing and a truncated one |
| Facility under XBLA | `lookups=0` |
| the census | 84 GL draws per frame, **16** of them the decal pass, one offset each |

### The other six reports, classified and NOT fixed

**The truck, and the third watchtower (marks 4, 5, 6) share this cause.** The
same style group, the same coincidence, repaired by the same flag - see the
table above. Nothing separate was done for them.

**The tunnel's missing road lines are a DIFFERENT mechanism.** Two decal meshes
lie flat at the tunnel's floor height across its whole length, and hiding
either changes **zero pixels** at the playtest's tunnel mark: they are submitted
and every fragment is discarded. The reason is the alpha test above. The
renderer applies `GL_GREATER 0.33` to translucent materials as well as to
cutouts - a rule introduced for foliage, which is now classified as CUTOUT and
no longer needs it - and these two images reach a **maximum alpha of 0.314 and
0.235**. One of them misses the threshold by 5%. Three translucent meshes and
56 triangles are discarded outright this way. **That is the next blocker, it is
a decal-pass state question like this one, and it is not fixed here** because
this round's contract was depth order and that one is not.

**The sky ring (mark 2).** With the background drawing no mesh, the dome alone
fills the frame smoothly: its strongest horizontal step is **3.30** against a
mean of 0.38, so there is no ring in the dome's own image. With the background
drawn it paints 63.9% of the frame and its skyline crosses every one of the
1365 columns. The ring is the **boundary between the remastered distant terrain
and the dome behind it**, not a seam, a wrap or a clamp in the dome. Next
stage: why that silhouette reads as a hard band rather than a horizon - which
is the dome's lower-rim issue already recorded, now with a second witness.

**The water.** The refused stride-36 pool is a stronger candidate than it was,
and still a candidate. Read with the position at byte **+4** it is **100%
finite** and a **perfectly flat plane at world y -824.4, y spread 0.00**,
spanning x -14872..14158 and z -38976..1532; read at +0 it is not finite at
all. At the playtest's water mark, **15.3% of the frame is the environment colour**
- nothing rasterised - and the ray through the centre of that region crosses
that plane at world (8794, -824.4, -3675), **inside the pool's footprint**.
Under PRESENTATION ORIGINAL the same pose has **0.00%** environment colour. So
the reservoir is drawn by the game and is not drawn by the replacement, and the
pool that would carry it is the one the reader refuses. Not shipped here: it is
a second vertex-layout question and deserves its own round.

**The mountain shading (mark 7).** Bounded, ownership only. Measured in the
mountain band alone, because the near railings dominate a whole-frame step: the
playtest's own frame steps hardest at row 276 and this capture at row 268, so it
reproduces. It is **not a mip transition and not vertex shading** - it survives
both `SL_BG_NOMIP` and `SL_BG_WHITE` - but neither leaves it where it was, and
`SL_BG_NOMIP` changes the whole region's step structure (mean 7.15 -> 11.07).
That points at the base image and its coordinate, which is the **parked UV
blocker** (171 world units per repeat on 7.2% evidence for the main pool), not
at geometry, normals, a decal or the dome.

**Fog.** One measurement, no implementation. The game's own room background
goes through `fog_apply` on the triangle path; both external passes call
`glDisable(GL_FOG)` unconditionally and nothing re-enables it - a tree-wide
search of `src/` for `glEnable(GL_FOG)` finds it only inside `fog_apply`. So
the answer is **NO: the external XBLA background does not receive the fog state
the original level background receives.** The reason on record was written for
an eye-anchored dome beyond the far plane and was carried across to a
world-anchored replacement that sits inside the world and is depth-tested
against it, where it does not follow. The stage does configure fog - this run's
own events record `zfar -1.0 -> 15000.0` and `far-fog -1.0 -> 75000.0`. A
concrete later blocker.

> **Closed later.** See *The fog contract: the replacement never received the
> level's own fog* below, which implements this measurement and corrects one
> thing it implies: classifying the replacement from the state standing at the
> seam is not stable, because the seam is not always a room triangle.

**The UV scale stays parked**, unchanged and deliberately: a wrong coordinate
cannot make two coincident surfaces flip which one is in front, and this
round's evidence was exactly that.

## The alpha reject: a threshold outlived the population it was written for

The tunnel's road markings were prepared, loaded, submitted and drawn, and put
**zero pixels** on the screen. Hiding their meshes at the playtest's own tunnel
mark changed nothing, because every fragment was being discarded before it
reached the framebuffer. This is that defect, and it is a different mechanism
from the coplanar one above - same pass, different state.

### Where the 0.33 came from, from the history rather than from memory

`git log -S` over `src/gfx/sl_gfx_dl.c` names one commit for `0.33f`: the one
that introduced the background pass itself. Its own comment says what it was
for - **the foliage**. A tree in this item is a quad whose image is
transparent almost everywhere, the colour under that transparency is whatever
the artist left there, and blending it at a low alpha was the violet wash the
first render showed. At that moment foliage reached the translucent branch,
because a draw was classified translucent if its style said so **or** its image
carried alpha.

The CUTOUT split then moved every one of those draws to
`SL_WA_BLEND_CUTOUT`. **The rule's entire originating population left the
branch and the rule stayed behind.** Nothing re-derived whether the class that
remained had ever needed it.

### What the source says, measured over all 386 draws

The style word's low byte is the blend: 1 opaque, 2 translucent. Its
**alpha-masked group is a separate bit, `0x0800`**, and on the Dam item it is
set on exactly the 4 foliage draws - 39044 of 90360 triangles - and on nothing
else. Cross-tabulated over every draw:

```
style low byte 0x01, 0x0800 clear : 359 draws      (ordinary world geometry)
style low byte 0x01, 0x0800 SET   :   4 draws      (the foliage and detail shell)
style low byte 0x02, 0x0800 clear :  22 draws      (the translucent class)
style low byte 0x12, 0x0800 clear :   1 draw
```

**No draw carries both.** The source's masked group and its blended group are
disjoint, so the reject was being applied to a class the source never put it
on.

The causal half of that, rather than the correlational half: the masked group
is the only population that *needs* a reject. Those 4 draws are styled
**opaque**, so they cannot blend their transparent texels away - and their
images are 52% to 80% fully transparent. A blended draw has no such problem,
because a texel at alpha zero multiplies to nothing through the blend.

### What it cost, and the risk of removing it, both measured

Of the bundle's 19 translucent materials, **4 have an alpha maximum at or
below 0.33**, so every fragment failed and the mesh contributed nothing. Two
of them are the tunnel's road markings, at image alpha maxima of **0.314 and
0.235** - one misses the threshold by 5%.

The wash the threshold was written against is a real effect, so the risk was
measured rather than argued. The band a removal newly admits is
`0 < alpha <= 0.33`. Over the 17 images the translucent class uses:

| the band | images | what is in it |
|---|---|---|
| **no texel at all** | 8 | alpha is binary; removal admits nothing |
| under 4% of the image | 6 | and dark - mean contribution 1.2 to 15.1 of 255 |
| 19.9% and 45.6% | 2 | **the road markings themselves**, at rgb 184 and 255 |

A fully transparent image (one is, at alpha 0.000 everywhere) stays invisible
either way.

### What ships

**One state, removed from one class.** `SL_WA_BLEND_REJECTS` is the single
place that answers "does this class take an alpha test", the draw loop
branches on it, and the self-test checks the same predicate rather than a
restatement of it. CUTOUT rejects at `SL_WA_ALPHA_REF`; TRANSLUCENT and
OPAQUE do not. The predicate is TOTAL, which is what says no draw inherits the
previous draw's alpha-test state: the loop installs its own on every mesh,
both ways.

**No byte of the format moved**, so the version did not either. This is a
render-state contract, not a layout one - there is no new material bit,
because the classification the format already carries expresses it.

`SL_BG_XLU_BLEND=0` restores the previous state - the reject on both classes -
the same kill-switch idiom as `SL_BG_DECAL` and for the same reason: a state
change is only believable if both semantics can be captured back to back on
one binary. Every before/after below is that one flag.

### The picture, at the playtest's own tunnel mark

| | |
|---|---|
| the meshes | 60, 61, 62 and 69 - 56 triangles, materials 60/61/62/64, images 57/58/60 |
| before | submitted, drawn, **0 pixels**; hiding them changes nothing, on all 8 captured frames |
| geometry in view | mesh 62 alone covers **20128 framebuffer pixels**, lying on the opaque tunnel floor (meshes 26, 27, 28) at a median separation of **0.0000** |
| after | **4161 pixels**, 0.397% of the frame, in a strip down the tunnel floor |
| what painted | 20.7% of the geometric footprint, against image 58 being 19.9% non-transparent - the marking is a stripe inside a wide quad |
| opaque stamping | **0 pixels** driven to opaque white; mean rgb goes 12/13/14 -> 51/71/71, a blend |

Five nearby poses - forward, back, heading either way, pitched - all draw the
markings, all with zero opaque pixels, and the bounding box swings with the
camera (heading +12 deg moves it to x 559..627, -12 deg to x 762..839), which
is a marking staying where it is in the world.

### Controls

| | result |
|---|---|
| the new build with `SL_BG_XLU_BLEND=0` against the pre-change build, at the witness | **byte-identical, 0 pixels**, all 8 frames |
| two captures of one pose, same settings | **byte-identical** (`SL_VI_CATCHUP=0`, pinned seed) |
| hiding the four meshes after the fix | restores the pre-fix frame **exactly**, so the pixels that appeared are precisely the ones those meshes draw |
| **the accepted coplanar-decal repair** | the decal pass still wins the depth compare on **100.0%** of its footprint at marks 1 and 4 under both alpha rules; with the pass off it loses on 82% and 100%. No fight returns |
| CUTOUT drawn alone, both rules, marks 1 and 2 | **byte-identical**, all 8 frames |
| a translucent material with no texel below 0.33, drawn alone | **byte-identical** |
| every translucent mesh hidden, six marks | **byte-identical** - so opaque, cutout, the FAN/world-gap repair and registration are all untouched |
| ORIGINAL and COMMUNITY HD, both rules, with and without an asset root | **byte-identical** (and they differ from each other by 90.3%, so that is not vacuous) |
| per-tick trace, 400 frames, ORIGINAL / COMMUNITY / XBLA / XBLA under the old rule | **one hash**, 901 lines, 80802 bytes. No re-baseline |
| a missing, malformed or truncated bundle | the original world draws, one refusal line each |
| Facility under XBLA | no world-asset lookup at all |

**A note on one metric that misreported.** The adjacent-pixel stipple number
used to grade the coplanar repair reads *worse* at mark 4 after this change
(-45.7% becomes -1.1%), and that is the metric's limit rather than a
regression: it cannot tell "the decal now draws texture the reject used to
punch out of it" from "the decal is fighting again". What separates them is
physical - a decal that loses the depth compare contributes nothing and reads
as the decal-free frame. Measured that way, **0.0% of the decal footprint
contributes nothing** with the pass on, under both alpha rules, at both marks.
There is no alternation between decal and base to be a fight.

### Recorded debt, not fixed here

The derived CUTOUT set and the source's own `0x0800` differ by **one draw of 6
triangles** (draw `0x4f64`, style `0x4701`, image 53): the preparation calls it
a cutout because its image carries alpha, and the source does not mark it
masked. The same image is used by a marked draw and an unmarked one, so the
bit is per-DRAW state rather than a property of the image. Reading CUTOUT from
the source bit instead of deriving it from the image is a candidate for a
later round; it is out of scope here, where the contract under test was the
alpha reject and not the classification.


## The packed mip tail: level 0 was never at the start of its allocation

The tunnel road marking was rejected in playtest after the alpha reject above made
it visible: *"The lines in the tunnel aren't right yet."* The previous round
had already cleared the stages downstream of the picture - mesh 62 alone paints
the pattern, its geometry is a healthy continuous strip, the UV field is
big-endian s16 at vertex +16 over 1/128, U runs along the strip and V spans
exactly 0..1 across it, and current UVs with the current sampler and the
**currently decoded image** reproduce the reported broken blocks. So the first
wrong stage was upstream of all of it: **the decoded image itself**.

It was not the format, the endianness, the block decode or the tiling. It was
the ADDRESS. Level 0 of that texture is not stored at the start of its own
allocation, and the decoder had always assumed it was.

### The contract, recovered from the hardware behaviour

A Xenos texture allocation has three rules, and the pipeline only knew the
first two:

1. the **base level** is stored under the fetch constant's base address, laid
   out at the row pitch that constant carries - a pitch counted in texels
   divided by 32, so in blocks it is always a multiple of 32 - with the height
   in blocks padded to 32 as well;
2. **mip levels** from 1 are stored under a separate mip address, with a
   power-of-two derived layout of their own;
3. when packed mips are enabled, every level whose width **or height** has
   fallen to 16 texels or fewer is not given storage of its own. Those levels
   share a single tile - the **mip tail** - packed into the 32-texel padding of
   the last level above it.

The third rule is written on the SHORTER side, and that is the whole of this
defect. A texture that is already 16 or fewer texels on one side has a tail
that begins at **level 0**: the base level itself lives inside the tail, and
is therefore NOT at allocation coordinate (0, 0). Its own smaller levels are
stacked in the rows above it, and those are precisely what the decoder was
reading and handing over as the picture.

Within the tail the levels are laid out along the short axis - a wider-than-
tall texture stacks them downwards, a taller-than-wide one across - each at a
halving offset, until the levels become smaller than a compression block and
fold back along the long axis.

### The predicted offset, and what said it before it was decoded

The canonical bad texture is the tunnel's road marking: **256x16, DXT4/5, five
declared levels**, in its own 32768-byte allocation. The shorter side is
exactly 16, so the rule puts its packed level at 0 and its base level 16 texels
down - **block offset (0, 4)** - inside a surface 64 blocks wide and 32 block
rows tall.

That prediction was made before anything was decoded, and it predicted the
SHAPE OF THE DAMAGE as well as the fix. If the base is at (0, 4), then what the
old decoder read at block rows 0..3 is the tail's smaller levels: level 2 at
block row 1, level 1 at rows 2..3, and levels 3 and 4 side by side in row 0 at
block columns 32..39 and 16..19. The rightmost of those ends at block column
39, which is **texel column 159**. So the rule says the old decode must carry
content in columns 0..159 and be empty in 160..255.

Measured on the old decode: content in columns 0..159, **149 empty columns,
every one of them at 160..255.** The rule named the corruption's exact extent
from the layout alone.

| | old decode (origin) | Xenos layout (packed base) |
|---|---|---|
| alpha range | 0..80 | 0..76 |
| non-zero alpha | 19.9% | 50.0% |
| occupied columns | 107 of 256, all below 160 | 128 of 256, spanning 0..255 |
| distinct alpha values | 31 | **5** |
| distinct RGB triples | 31 | **6** |
| column run pattern | 17 on, 6 off, 18 on, 6 off, 35 on, 29 off, 22 on, 6 off, 10 on, 6 off, 5 on, 96 off | **32 on, 64 off, 64 on, 64 off, 32 on** |

The run pattern is the answer in one line. Under REPEAT the leading 32 and the
trailing 32 join into one dash, so the texture is a **period-128 dashed stripe**
- 64 on, 64 off - light grey with darker edges across its width. That is an
authored road marking. Six distinct colours and five alpha values is authored
art; thirty-one of each is what reading four different mip levels as one image
looks like.

### Packed mips are on, measured from the allocations rather than assumed

Nothing in the container's texture record is a fetch constant. The word at
+0x18 carries the format and the endianness and is otherwise identical across
textures of the same format; there is no pitch field, no tiled flag, no packed
flag and no address. So whether packed mips are enabled had to be measured,
and the allocation sizes measure it exactly.

For every larger texture in the Dam bundle, the allocation is the base extent
plus a mip chain **truncated at the packed level** - with a residual of zero
bytes, over the whole bundle, with nothing fitted:

| texture | base extent | mips stored | total | allocation |
|---|---|---|---|---|
| 256x256 DXT1 | 32768 | 1,2,3 + tail at 4 | 32768 | **65536** |
| 512x512 DXT1 | 131072 | 1..4 + tail at 5 | 65536 | **196608** |
| 1024x1024 DXT1 | 524288 | 1..5 + tail at 6 | 196608 | **720896** |

Without packing, the chains would run on to the 4x4, 2x2 and 1x1 levels and
every one of those totals would be larger. The truncation IS the packed-mip
signature, and it is present in every bundle.

### The gate, which is also measured

A tail can only hold a level when a chain exists at all, so a texture the
container declares with a single level is read at the origin. That is not an
assumption: the Dam bundle has a **64x1 single-level** texture, which the
short-side rule would otherwise move. At the packed position its pixels are
entirely zero; at the origin they are real. The mip count is the gate, and the
container carries it.

### What ships

`tools/texpack/xdecode.py` now owns the layout, because it is the module that
already owned Xenos texture memory. It gained the packed mip level, the packed
level offset, the base origin and the base row pitch, expressed in those
terms; `untile` takes the level's origin and the surface pitch, and REFUSES a
level placed outside its surface rather than decoding it out of padding. The
three call sites - the texture-pack converter and both world-asset readers -
pass the mip count the container declares. There is no texture id, no size and
no byte offset anywhere in it.

It is deliberately general, and the generality is visible in what moves:
**39 textures of 1369, across 35 bundles**, and every one of them has a short
side of 16 or less with a declared mip chain. Every square texture, every large
one, all 22 skydome textures and the 64x1 single-level texture are
byte-identical. Not one of the 127 accepted XBLA texture-pack mappings names a
moved texture, so all three texture sets regenerate byte-identical - 595 files,
zero changed - and only the world-asset bundles move.

### The picture, at the playtest's own tunnel mark

Captured at the playtest's `mark-001` camera, teleport-only, `SL_VI_CATCHUP=0`,
the playtest's pinned seed, both bundles built from the same source by the same
pipeline and differing only in the decoder:

- **before**: the irregular grey blocks of the playtest's own screenshot,
  reproduced exactly;
- **after**: a regular sequence of dashes receding up the tunnel with correct
  perspective foreshortening.

The change is contained: **7389 pixels of 1048320 (0.705%)** differ, all of them
inside the road band at x 622..673, y 407..735. Outside that band the two
frames are **pixel-identical at every one of the five captured indices** - so
the mesh has not moved, the blending has not changed, and nothing else in the
scene was touched.

### What this round did not do

The alpha values in the repaired texture top out at 76 of 255, which is the
art's own; whether the material's blend makes that read at the right strength
is a separate question and was not touched. Image 57 (32x32) still decodes to
zero alpha and image 60 (64x64) is still wrong in a way this fix does not
address: both have their packed tail at level 1 or 2, so their base levels are
at the origin and are byte-identical before and after. Whatever is wrong with
them is a LATER stage, and this round deliberately left it there rather than
inventing one universal offset to cover it.

### The staleness hole this exposed, and closed

The repair would have been invisible. A world-asset bundle is rebuilt when its
source or its tools change, and the tool hash covered only the three
`tools/worldasset` modules - not `caff.py` and `xdecode.py`, which it imports
and which decide every texture byte. The source had not changed, so nothing
would have noticed. Both stamps now hash the modules that actually decide the
output: the world-asset stamp covers the two texture-pipeline modules it
borrows, and the texture-pack stamp covers its own converter. The evidence that
it works is that regeneration ran on its own, with no `-Force`, the moment the
decoder changed.


## The vertex colour: one word in the record was never byte-reversed

A playtest of the repaired road marking reported *"lines look better,
but they are blue and not yellow"* (2026-09-24, Dam, `mark-001`). The shape,
the spacing and the repetition were accepted; the **colour** was not. This is
that defect, and it is not in the texture path at all.

### The texture carries no hue, so the hue is the vertex colour

Measured before anything was changed. The road marking is image 58, 256x16,
DXT4/5, and decoded it is **NEUTRAL**: 8 distinct RGBA values, mean rgb over
its visible texels **205.6 / 206.0 / 205.6**, brightest visible texel
`(207, 207, 207)`. A grey image cannot be made cyan by a channel swap in the
decoder, so the first wrong stage is downstream of the texture and upstream of
the framebuffer - and the only thing between them that carries colour is the
**vertex colour**.

The same frame carries the control for that, and it is what rules the texture
path out rather than an argument: the kerb's **yellow-and-black hazard stripes
are a texture**, and they render correctly yellow under BOTH presentations in
the same picture where the centre line is cyan.

The prepared bundle's road-line mesh (mesh 62, 26 triangles, 42 vertices)
carried three vertex colours: white on 26 vertices, `(71, 63, 57)` on 8, and
**`(0, 107, 107)` on 8** - which is the cyan visible in play.

### The rule that was wrong, and what says so

The raw word on disk is `ff 00 6b 6b`. Two readings fit its alpha:

| | reading | rgb |
|---|---|---|
| what shipped | alpha at byte 0, then **R G B** | `(0, 107, 107)` - cyan |
| what is right | alpha at byte 0, then **B G R** | `(107, 107, 0)` - yellow |

**The mechanism.** A Xenos vertex fetch converts memory into register order
with the endian its fetch constant carries, and this buffer's is the one that
makes a 32-bit float come out right - `8IN32`, which reverses all four bytes
(`GpuSwap`, Xenia canary_experimental `c9c8e483`, agreeing with upstream
`95a5c3ee`). That is exactly why this reader already reads the position
`'>fff'`, the coordinate `'>hh'` and the indices big-endian. `k_8_8_8_8` then
takes x, y, z, w from bits 0..7, 8..15, 16..23 and 24..31 of the reversed word,
so `r, g, b, a = byte3, byte2, byte1, byte0`. **The colour word was the one
field in the record that was not byte-reversed.**

The mechanism alone cannot close it, and this is worth writing down: a vfetch
carries a free destination swizzle, so a D3DCOLOR element would reach the
shader as RGBA under either byte order. The endian argument makes the reversal
the *no-swizzle* reading; it does not prove the shader adds none. **Statistics
cannot close it either** - the item's mean vertex colour is `(202, 189, 178)`
one way and `(178, 189, 202)` the other, and a warm Dam and a cool Dam are both
plausible pictures. Deciding by eye at that point would be exactly the shape of
the half-float texture coordinate this feature already had to withdraw once.

### What settles it: the game draws the same surface

The source carries the level background twice under one item path, and the
**ORIGINAL-generation model is the N64 level's own geometry**. Its floor decal
(draw `0x2f10`, style `0x2702`, translucent, world y -107 running the length of
the road) carries the colour word **`ff 00 e1 ff`** on 34 of its 46 vertices -
the same family of word as the remastered road line, in the same encoding.

So the game itself is the oracle, at the playtest's own camera. Rendered at
`mark-001`, teleport-only, `SL_VI_CATCHUP=0`, the playtest's pinned seed, in the
centre-line band `x 657..707, y 399..735`, brightest 1% of each picture:

| | rgb | R - B |
|---|---|---|
| **PRESENTATION ORIGINAL - the game's own line** | `121.5 121.5 8.0` | **+113.5** |
| the N64-generation model through our pipeline, **reversed** reading | `130.8 127.2 96.1` | **+34.6** |
| the same model, the **shipped** reading | `96.1 127.2 130.8` | **-34.6** |

The game draws that line **yellow** - red equal to green, blue near zero. The
reversed reading reproduces the sign; the shipped one inverts it. That is a
second surface, from a second model, prepared through the identical pipeline
and drawn through the identical seam, and it is the known-colour control this
change rests on rather than the line being repaired.

### What ships

One function, `xblaworld.vertex_colour`, and **both readers take it** - the
level reader's `_pool_vertices` and the one-item reader's `_vertex_at`, which
had quietly disagreed with each other about where alpha was. **No engine code
changed**: `src/` is untouched, the format did not move, and the version did
not move. What changed is four bytes of interpretation in the preparation tool.

What moved in the bundle, measured by section:

| | |
|---|---|
| header, indices, materials, textures, meshes, image payload | **byte-identical** |
| vertex positions and texture coordinates | **byte-identical** |
| vertex alpha, vertex green | **byte-identical** |
| vertex red and blue | exactly each other's values, on all 140066 vertices |
| the **skydome** bundle | **byte-identical**, because its vertices are white and white cannot tell the two readings apart |

### The picture, at the playtest's own mark

The road line's own pixels, isolated as exactly the set that changes when
meshes 60, 61, 62 and 69 are hidden:

| pose | line pixels | before | after |
|---|---|---|---|
| **mark-001** | 6280 | `117.8 126.7 126.7`, R-B **-8.89** | `126.1 126.7 118.4`, R-B **+7.69** |
| +85 forward | 4203 | R-B -6.72 | **+5.60** |
| -85 back | 4359 | R-B -9.17 | **+8.03** |
| +12 deg heading | 6256 | R-B -8.58 | **+7.46** |
| -12 deg heading | 2536 | R-B -17.88 | **+16.61** |

The footprint differs before and after by **1 to 3 pixels** at every pose -
antialiasing on its own edge - so the geometry, the repetition and the
transparency are untouched and only the hue moved. The box swings with the
camera (`x 529..580` at +12 degrees, `x 784..833` at -12), which is a marking
staying where it is in the world.

**The change is not confined to the line, and that is the honest number**: the
vertex colour is the whole replacement's, so 32% to 38% of an XBLA frame moves.
What it does to the frame is small - whole-frame mean R-B goes `+0.40 -> -0.92`
in the tunnel and `-0.58 -> -7.82` at the gate yard, where the snow reads
cooler and the barriers' painted red, which is a texture, does not move at all.

### The control that proves nothing else moved

Only red and blue moved in the bundle, and blending is per channel - so the
rendered **GREEN channel must be identical everywhere**. Any geometry shift,
any depth decision, any alpha change, any mesh appearing or disappearing would
break it, and none of them can hide in it.

Measured over six poses, five captured frames each, whole frame:

| pose | green differs | red or blue differs |
|---|---|---|
| mark-001 | **0** of 5241600 | 1737290 |
| +85 forward | **0** | 1144985 |
| -85 back | **0** | 1748863 |
| +12 deg heading | **0** | 1684494 |
| -12 deg heading | **0** | 1702743 |
| gate yard | **0** | 1975083 |

Zero on 31.4 million pixels. The road-line geometry, the FAN repair, the
world-gap repair, the coplanar decal depth, the CUTOUT classification, the
alpha-test rule and the registration are all untouched, and that is a
measurement rather than an argument from the diff.

### Controls

| | result |
|---|---|
| ORIGINAL, old bundle against new | **byte-identical**, all 5 frames |
| COMMUNITY HD, old bundle against new | **byte-identical**, all 5 frames |
| ORIGINAL against COMMUNITY HD | differ - so the two above are not vacuous |
| a missing bundle under XBLA against the provider pinned dark | **byte-identical**, all 5 frames |
| per-tick trace, 400 frames, ORIGINAL / COMMUNITY / XBLA / XBLA on the old bundle | **one hash**, 901 lines, 80802 bytes. No re-baseline |
| Facility under XBLA | `lookups=0` - no Dam world asset requested or drawn |
| the skydome bundle | byte-identical across the change |

### What this does NOT explain

Images **57** and **60**, left open by the packed-mip round, are unchanged and
unexplained by this. Neither carries hue: image 57 decodes to alpha **0
everywhere** (one RGBA value, `255 255 255 0`) and image 60 decodes to a
neutral white at an alpha maximum of 60. A vertex-colour contract cannot reach
either - whatever is wrong with them is still a texture-side question.

The sky ring, the water, the mountain shading, the fog state and the parked UV
scale are all untouched here.


## The fog contract: the replacement never received the level's own fog

The previous round left one measurement with no implementation behind it: the
game's room background goes through `fog_apply` on the triangle path and both
external passes called `glDisable(GL_FOG)` unconditionally. This is that
defect, closed. It is a RENDERER STATE contract, not an asset change - no
bundle, no preparation tool and no source byte moves.

### What the game's own background consumes, measured

Read off the display list the game itself wrote, at the Dam's gate yard under
PRESENTATION ORIGINAL (`SL_FOG_DBG`, teleport-free boot pose, `SL_VI_CATCHUP=0`,
the pinned seed, `invincible,invisible,maxammo`):

| | |
|---|---|
| fog enabled | **yes** - 2072 of the frame's triangles carry `G_FOG` and all 2072 are drawn under the fog stage |
| fog colour | `10 30 60 ff` = **(16, 48, 96)**, alpha 255 - the environment's own sky RGB, and the same three bytes the run's `env-rgb` event records |
| the band | `fm 25600`, `fo -25344` |
| what that band means | the RSP computes `alpha = ndcz * fm + fo` clamped 0..255, so fog **starts at NDC z 0.99000** and **saturates at 0.99996** - the outer one percent of a 1/w depth buffer |
| the mode | `GL_LINEAR`, start 0, end 1, over an explicit **per-vertex fog coordinate** (`GL_FOG_COORD_SRC = GL_FOG_COORD`, read back as `0x8451`) |
| who applies it | `fog_apply` / `fog_coord` in `sl_gfx_dl.c`, from `fogSetRenderFogColor` (`bgfog.c:620`) |

**The fog is not eye distance.** It is the perspective-divided z put through
the RSP's own two halves, which is why nothing here invents a distance
function: the replacement has to hand GL the same quotient the triangle path
hands it.

### The same camera, under PRESENTATION XBLA, before the fix

`SL_BG_FOGTRACE` reads GL back rather than echoing the request, before, during
and after each external pass:

| | |
|---|---|
| GL fog during the replacement's draw | **disabled** |
| fog colour, band | never handed over - the pass configures none |
| what the level had standing at that instant | colour (16,48,96), `fm 25600`, `fo -25344` - **all of it available and all of it unused** |

**And the disable LEAKED.** `fog_apply` mirrors GL's enable in `g_fog_gl_on`
and issues `glEnable(GL_FOG)` only when that mirror is clear; the frame reset
deliberately does not clear it. A raw `glDisable` therefore left the mirror
saying "enabled" while GL was not, and nothing in the tree ever turned it back
on - tree-wide, `glEnable(GL_FOG)` appears in `src/` only inside `fog_apply`,
behind that mirror. Measured: from the second frame onward every trace line
reads `GL_FOG=0 mirror=1`, and it never recovers.

**What that leak did NOT do, measured rather than assumed.** With the
replacement drawing no mesh at all (`SL_BG_ONLY=9999`, so only the game's own
geometry is left), the leaking and non-leaking arms are **byte-identical on
every captured frame**. The reason is that the game's fogged triangles go
through the B-051 alpha-equation SHADER, which applies fog from its own
`u_fog` uniform and `gl_Fog` state and does not consult the fixed-function
enable at all - toggling `SL_FOG_PRIM` in the same configuration moves 15559
pixels, so that population is visible and is simply not gated by `GL_FOG`. The
leak is a real state defect and is fixed; **the load-bearing half is the
background's own missing fog.**

### The second defect: the seam is not always a room triangle

The obvious repair - classify the replacement with `fog_mode()`, the same
classifier the triangle path calls - was implemented, measured, and found
**unstable**. The seam draws on the frame's first world triangle, and
`bg.c:737` draws a room's PROPS before `bg.c:755` draws its background. So the
blender mux standing at the seam is the room's `A_SHADE` on most frames and a
prop's `A_FOG` on others, and a prop's fog alpha at this pose is zero. The
replacement's fog changed from frame to frame at a fixed camera.

The contract is therefore read from the game's own fog EMITTER instead.
`gSPFogPosition` has exactly one emitter in the game - tree-wide,
`grep -rn "gSPFogPosition" src/` names `bgfog.c:632` and `bgfog.c:637` and
nothing else - and `fogSetRenderFogColor` writes `SetFogColor` immediately
before it and returns without writing either when `g_FogSkyIsEnabled` is
clear. Latching the colour where that command is executed gives the LEVEL's
fog by construction rather than by luck, and gives the no-fog answer for a
level that does not fog, for the same reason the game's own geometry gets it.

### What ships

**One contract, consumed three times; no second fog configuration anywhere.**

- `sl_wa_fog_band` (`sl_gfx_worldasset.c`) is the decision: given whether a
  fog position has been seen, its two halves and its age in frames, does the
  replacement receive fog, and over what NDC band. A function rather than a
  condition at the seam so it can be tested with no GL context - the same
  reason `sl_wa_sky_aperture` is one.
- The background pass computes its per-vertex value with the renderer's
  existing `fog_coord`, from `c[2]/c[3]` - the same perspective-divided z the
  triangle path reads out of `g_vdepth` - and hands it to GL as a
  `GL_FOG_COORD_ARRAY`, because this pass draws through `glDrawElements`.
  `glFogCoordPointer` is resolved separately from the three B-075 entry points
  so a driver missing only it costs the background's fog and not TEXEL1 and
  the whole fog stage.
- `fog_apply` establishes the enable and the colour, so the renderer's mirror
  of GL stays truthful. The level's colour is handed to it for the length of
  that one call and the RDP register is put back.
- **Neither pass disables the stage any more.** At `LINEAR(0..1)` a coordinate
  of zero is factor 1 - the incoming fragment untouched - so `fog_neutral` is
  the same picture a disable drew without desynchronising the mirror. The
  **skydome keeps its unfogged picture** through exactly that: it is
  eye-anchored, carries no depth and sits beyond the frustum, so the level's
  distance band has nothing to say about it, and the reason on record for
  fog-off is correct for the dome and was only ever wrong when carried across
  to a world-anchored replacement.

`SL_BG_FOG=0` restores the previous behaviour - both passes disabling the
stage - from the same binary. It reproduces the pre-change build
**byte-identically on all five captured frames**, which is what makes the A/B
one build rather than two.

### The picture, at the Dam's gate yard

The long-distance outdoor pose: distant peaks, a large depth range, and before
the fix no haze at all. Deterministic boot pose, no input, `SL_VI_CATCHUP=0`,
the pinned seed, `-Cheats invincible,invisible,maxammo`.

| band | before | after | delta | pixels changed |
|---|---|---|---|---|
| far peaks (rows 40..200) | `85.19 98.44 110.55` | `73.92 89.28 105.83` | **-11.27 -9.16 -4.72** | 46.5% |
| mid cliffs (rows 200..380) | `85.93 95.84 105.14` | `82.95 93.57 104.16` | -2.97 -2.27 -0.98 | 27.0% |
| near yard (rows 520..730) | `58.68 61.44 63.52` | `58.68 61.44 63.52` | **0.00 0.00 0.00** | **0.0%** |

Three things have to be true of that table and all three are. The attenuation
is **monotone with depth**. The near band is **untouched to the last bit**,
which is what says this is a depth contract and not a tint. And the direction
is the fog COLOUR's: mixing bright snow toward `(16, 48, 96)` must take red
furthest, then green, then blue, and the deltas are in exactly that order.

Whole frame, 190039 of 1048320 pixels move. The coordinates handed to GL over
the item's 140066 vertices run `0.0000 .. 0.9659`, mean `0.1725`, from a depth
range of `-154.5 .. 0.99962` - and the mode and projection are the same on
every sampled frame, so the flicker the first attempt had is gone.

**Pixel identity with the original is not the test and is not claimed** - the
geometry and the textures differ by design. What is claimed is that the
replacement now receives the same fog parameters, over the same depth, in the
same direction.

### Controls

| control | result |
|---|---|
| ORIGINAL, pre-change build vs this one | **byte-identical**, 5 of 5 frames |
| COMMUNITY HD, pre-change build vs this one | **byte-identical**, 5 of 5 frames |
| ORIGINAL vs COMMUNITY HD | differ - so neither of the above is vacuous |
| XBLA, pre-change build vs `SL_BG_FOG=0` | **byte-identical**, 5 of 5 - the escape hatch is the old behaviour |
| XBLA, pre-change build vs the fixed default | differs on all 5 - the A/B is not vacuous either |
| no bundle under XBLA, pre-change vs fixed | **byte-identical**, 4 of 4; log reads "not provided - dam unchanged" |
| Facility under XBLA | `lookups=0 loaded=0 missing=0 invalid=0` - no Dam world asset requested or drawn |
| the tunnel mark (playtest `mark-003`) | **3 of 1048320 pixels** differ - the tunnel is interior, no distance band reaches it; road lines present, dashes regular, no z-fighting, no repetition change |
| per-tick trace, 400 frames, ORIGINAL / COMMUNITY / XBLA / XBLA with `SL_BG_FOG=0` | **one hash**, 901 lines, 80802 bytes. No re-baseline |

The tunnel's 3 pixels and the near yard's 0 together cover the accepted
repairs by measurement rather than by an argument from the diff: the FAN /
world-gap repair, the world registration and visibility scale, the coplanar
decal depth, the TRANSLUCENT alpha-test rule, the CUTOUT foliage
classification and the packed-base texture addressing all sit in bands that
did not move.

### What this round did NOT fix, and what it says about them

- **The mountain appearance improved as a natural consequence** and nothing
  separate was done for it. The distance-shading complaint recorded against
  the mountain band is a different measurement and is not claimed closed.
- **The sky ring is NOT fog.** The strongest row-to-row step across the
  terrain-to-sky band moves from **9.19 to 8.83** - within the noise of the
  band's own structure - so fog was not causally involved and the sky
  transition remains the next separate candidate.

  **Corrected later, and the correction is narrow.** That measurement is about
  the boundary the ADDITIVE DOME's lower edge made, and it still says what it
  said about that boundary. It says nothing about the boundary left once the
  dome is not composed, and that second one IS the fog band - see
  [The cloud ring](#the-cloud-ring-the-levels-fog-faded-the-remaster-into-a-backdrop-it-had-replaced).
  The reading that has to be retired is the metric, not the number: the
  largest row step **falls** when a region is crushed to a constant, so it
  cannot on its own distinguish a seam from a wall.
- **The water is untouched** and still parked.
- The parked UV scale, the source `0x0800` CUTOUT classification debt, the
  exact `c_zadjust` magnitude and images 57 / 60 are all carried forward
  unchanged.


## Scene assembly: the additive sky was being composed with a REPLACEMENT

The ring across the Dam's sky/backdrop transition was not fog - the fog round
measured that and said so. It is a **composition** defect, and the unit that is
wrong is which assets the scene contains.

### What the four-state matrix measured

Four captures, same camera, same frame, same geometry, at the sky-ring witness
the previous round established (Dam, eye `15605, 60, 3755`, room 123, theta
315, verta 20, read 62; teleport only, `SL_VI_CATCHUP=0`, the pinned seed,
`-Cheats invincible,invisible,maxammo`). The dome is removed by pointing the
world-asset root at a set carrying the background bundle only, so the two arms
differ by the asset's presence and by nothing else.

| | max step across the dome's own lower boundary |
|---|---|
| **A** dome + background, fog ON | **7.15** |
| **B** dome + background, fog OFF | **12.64** |
| **C** background only, fog ON | **1.88** |
| **D** background only, fog OFF | **7.28** |

The boundary is located once, from the dome's own visible footprint, and all
four are then measured on the same screen rows. Below that boundary **A and C
are equal to the last bit at every row, and so are B and D** - which is what
says the footprint really ends there and that nothing else moved.

The dome owns **141052 pixels** of the frame and **33.9%** of the sky/backdrop
band. **0.02%** of that band is painted by neither external pass, so there is
no literal gap for a sky to fill.

### What actual XBLA shows, and the sign that settles it

The remastered Dam's own sky, read off the reference console build under Xenia:
where it is unoccluded it is a **flat pale blue** - `(120.7, 163.4, 195.1)`
with a standard deviation of `(2.3, 2.8, 3.2)`, and over the top thirty rows
of that patch it is one exact constant. No cloud pattern, no horizon band, no
arc, in any capture that shows sky.

The discriminator is the same shape as the road line's: a **sign**.

| | rgb over the sky/backdrop band | R − B |
|---|---|---|
| actual XBLA, four unoccluded sky patches | - | **−48 to −74** |
| **the standalone skydome ALONE** | `176.1 167.0 163.1` | **+13.0** |
| dome + background, fog ON | `131.6 133.1 141.0` | −9.5 |
| dome + background, fog OFF | `161.3 159.8 163.0` | −1.7 |
| **background only**, fog ON | `83.9 104.8 134.5` | **−50.6** |
| **background only**, fog OFF | `113.6 131.5 156.5` | **−42.9** |

The dome's own picture is **warm** - a peach and cream cloud map with its own
baked horizon and its own jagged mountain rim - against a reference sky that
never comes near neutral. Composed, it drags the band from the reference's
−48..−74 to −2..−10. Removed, the band sits at −43..−51, inside the
reference's own range. **The dome is a sky the actual game is not drawing
here.**

### The rule, and why it is this condition

It is an ordering accident rather than a decision. The skydome shipped first,
when the Dam had no external background at all and the only other picture was
the game's own; the whole-background replacement arrived afterwards, carrying
the stage's complete distant panorama **and its own authored sky transition**;
and nothing ever related the two classes, because nothing had to. The provider
resolves each class separately by `(class, item path)`, the preparation tool
converts whatever directory it finds, and the seam drew both. **Presence in the
archive, not scene membership, was deciding.**

`sl_wa_compose_skydome` decides it instead, and the condition is read off what
each class already **is**: SKYDOME is ADDITIVE - drawn as well as the game's
own picture - and BACKGROUND is a REPLACEMENT that stands in for the stage's
static background and suppresses the emission it replaces. The additive class's
premise is that the game's own backdrop is the one on screen; a loaded
replacement is exactly the statement that it is not.

It is the **same** condition that decides the suppression, so the two can never
disagree about which backdrop is on screen, and every degenerate case falls out
of it rather than being enumerated:

- a stage with a sky and **no** replacement composes the sky exactly as before;
- a **missing, invalid or unuploadable** replacement leaves the game's own
  backdrop drawing and the sky composed over it - the same fallback the
  replacement contract already demands;
- a presentation that selects no world assets resolves neither.

No item path, no level id, no mesh index and no colour appears in it. The
replacement is resolved **first** so an additive sky the composition does not
contain is never even opened, and the census then reports what was composed
rather than what exists.

`SL_SKY_WITH_BG=1` composes both from the same binary, which is what makes
every before/after below one build.

### What was checked and did NOT hold

The source's `new/skydome/` covers eleven stages (`bunker`, `bunker2`,
`caverns`, `complex`, `dam`, `egyptian`, `frigate`, `runway`, `surface`,
`surface2`, `temple`) against `new/background/`'s twenty-four, it is the one
class with **no** `original/` counterpart, and its names are a STAGE vocabulary
where the background's are MODEL names (`caverns` against `cavern` and `caves`,
`egyptian` against `aztec`, `surface2` where the background has only
`surface`). The obvious reading - that the set names the stages whose own
environment draws a sky - was tested against `fog_tables[]` and **is wrong**:
`clouds` is set on sixteen stages, eight of which have no skydome
(`statue`, `archives`, `train`, `streets`, `depot`, `cradle`, `citadel`,
`aztec`) and `caverns` has a skydome with `clouds` clear. **What the eleven
are is not established here**, and the rule above deliberately does not depend
on it.

### Before and after, ten poses, one binary

`SL_SKY_WITH_BG=1` is the old composition; the default is the rule.

| pose | pixels changed | ring before | ring after |
|---|---|---|---|
| the ring witness | 141052 (13.5%) | 7.15 | **1.88** |
| +85 forward | 140020 | 7.61 | **2.16** |
| −85 back | 142019 | 6.39 | **1.48** |
| −12° heading | 139341 | 2.84 | 4.26 |
| +12° heading | 134392 | 11.14 | **5.03** |
| same spot, settled pitch (read 123) | **1** | - | - |
| same spot, level pitch | **0** | - | - |
| the crest | **2** | - | - |
| playtest `mark-001` (tunnel) | **0** | - | - |
| playtest `mark-003` (tunnel) | **0** | - | - |

Two honest qualifications, both measured:

- **The metric's limit.** After the dome is gone the boundary locus is an
  arbitrary line through the panorama, and what it then reports is the
  panorama's own ridge detail rather than a ring - which is why the −12°
  heading reads higher after, from an already-low 2.84. The same limit is why
  the largest row step in the whole sky band cannot by itself separate a ring
  from real terrain: unfogged and dome-free, the panorama's ridge produces
  steps of 7.3, above actual XBLA's 2.6-5.6, because it is structure rather
  than a seam.
- **How much of this a player sees depends on pitch.** At the gate yard the
  dome contributes nothing at level pitch and grows as the camera looks up:
  0 px, then 1458, 35723, 86808 and 141052 as the teleport pitch is raised.
  The ring witness is a deliberately pitched diagnostic camera, and **none of
  the five playtest marks recorded for this level is an outdoor sky pose** - all
  five are tunnel poses pitched down. This repair is a scene-assembly
  correction demonstrated at the witness; what it is worth in play is for a
  playtest to decide.

### Controls

| | result |
|---|---|
| the witness, new default vs a pre-change build with no skydome file | **byte-identical** |
| the witness, `SL_SKY_WITH_BG=1` vs the pre-change build's default | **byte-identical** |
| the witness, new default vs `SL_SKY_WITH_BG=1` | differs by **141052** px - the A/B is not vacuous |
| **ORIGINAL**, new build vs the pre-change build | **byte-identical** |
| ORIGINAL, new default vs `SL_SKY_WITH_BG=1` | **byte-identical** |
| COMMUNITY HD, new default vs `SL_SKY_WITH_BG=1` | **byte-identical** |
| ORIGINAL vs COMMUNITY HD | differ by 446886 px - so neither above is vacuous |
| a set with a **skydome and no background**, both arms | **byte-identical** - the sky still composes, and the picture differs from dome+background by 855199 px |
| Facility under XBLA | no world-asset line at all: no lookup, no file opened |
| per-tick trace, 400 frames, ORIGINAL / COMMUNITY / XBLA / XBLA `SL_SKY_WITH_BG=1` | **one hash**, 901 lines, 80802 bytes - the same trace the fog round recorded. No re-baseline |

The tunnel marks' **0 pixels** and the level-pitch pose's **0** cover the
accepted repairs by measurement rather than by an argument from the diff: the
FAN / world-gap repair, world registration and the visibility scale, the
coplanar decal depth, the TRANSLUCENT alpha-test rule, the CUTOUT foliage
classification, the packed-base texture layout and the tunnel road line all sit
in bands that did not move at all.

**The fog implementation is untouched.** No added or removed line changes it;
`SL_BG_FOG` was used only as a diagnostic, and the remastered atmosphere -
distant scenery that keeps its structure and trends toward a bright pale-blue
haze - remains a separate open question, deliberately not addressed here.

That open question is the one the next round answers, and this section's own
closing sentence was the pointer: see
[The cloud ring](#the-cloud-ring-the-levels-fog-faded-the-remaster-into-a-backdrop-it-had-replaced).
Removing the dome is what made it legible - with the dome composed, its own
picture sat across the very band the fog band saturates over.


## The cloud ring: the level's fog faded the remaster into a backdrop it had replaced

Playtest report after the scene-assembly and frame-time rounds: *"Frame rate is
better, and dome is mostly acceptable, but the clouds seems to be contained in
a ring."* That is a different defect from the one the dome removal addressed,
it is reproducible on the current binary at the playtest's own mark, and this is
its cause. **#68 stays OPEN and nothing here is a playtest acceptance.**

### The witness, and what the playtest's own mark already said

`runs\20260925-112133-lvl33\mark-001` - Dam, outdoor, on the dam wall looking
across the valley, eye `11648.3 60.3 1000.1`, room 53, theta 127.4, verta 22.3.
Two of its recorded fields are the whole finding:

- **centre-rgba `10 2f 5e`** - (16, 47, 94), which is the level's own fog colour
  to within a unit;
- **centre-depth `0.999965`**, eye distance **13794.8**, and `triangles-at-centre
  0`. So it is not the clear, not a 2D pass and not a submitted display-list
  triangle: it is OPAQUE geometry, depth written, 13795 units out - the
  replacement's own.

Reproduced at that camera by teleport (no input, `SL_VI_CATCHUP=0`, the pinned
seed, `-Cheats invincible,invisible,maxammo`, read 62); the earlier
`20260925-092236` `mark-002` shows the same boundary from a second pose.

### What paints each side of the ring, measured by removal

| | pixels changed | rows |
|---|---|---|
| hide the opaque distant panorama mesh | **213153** | 115..523 |
| hide the source's own fade strip | **56894** | 48..368 |

Overlaid on the frame, the panorama mesh is **everything below the arc** and the
fade strip is **a narrow band along the arc itself**. The pale cloud region
above it is neither: it is the asset's own near sky shell. So the "wall" is the
distant panorama, and the authored fade is a thin strip on its rim - **not** a
broad fade whose alpha runs out.

### The fade is not what fails; the destination is

The per-pixel fog factor is recovered arithmetically rather than argued about:
a pixel of this pass is `(1-f)*unfogged + f*fogcolour`, so capturing the same
frame with and without the stage solves for `f` per pixel. Down one column:

```
  y 60..320   f = 0.000     the near sky shell - no fog at all
  y 340       f = 0.410
  y 360       f = 0.917
  y 380..460  f = 1.000     the distant panorama - the fog colour exactly
```

**It is a mask, not a gradient.** The level's band is the outer one percent of
a 1/w buffer - no fog below NDC z 0.99000, saturated at 0.99996, which is 939
to 13780 eye units - and this asset's near sky shell sits inside it while its
far panorama shell sits beyond it, with nothing in between. Below the rim the
frame is a constant: mean `(17.1, 48.9, 96.5)`, per-channel sd `(2.2, 1.8, 1.3)`.

### The identity the game keeps, and the replacement broke

The game writes ONE value to two places. `g_CurrentEnvironment`'s three bytes
are what the backdrop is filled with and what `fogSetRenderFogColor`
(`bgfog.c:620`) hands to `SetFogColor`, and the environment lerp moves both
together. Measured on the Dam in one `SL_FOG_DBG` line:

| | |
|---|---|
| backdrop fill | `103163` = **(16, 49, 99)** - the 5551 quantisation |
| fog colour | `103060ff` = **(16, 48, 96)** |
| the run's own `env-rgb` event | **16, 48, 96** |

So the game's distance does not fade toward "a fog colour"; **it fades into the
picture behind it**, and that identity is exactly why the original shows no
seam where its band saturates. A REPLACEMENT background *is* the stage's
backdrop - the same statement the suppression is armed by and the same one
`sl_wa_compose_skydome` is built on - so handing it the level's band fades the
remastered world toward a backdrop that is no longer on screen, and the picture
snaps to that colour wherever the band saturates.

### And the replacement already carries the remaster's own atmosphere

Unfogged, the same panorama reads down the same column `(131,155,188)`,
`(112,145,183)`, `(101,136,175)`, `(91,129,171)` - monotone, paler with
distance. Held at white vertex colour those pixels are **identical**, so the
shading is in the asset's own images, not in a vertex term.

Against the console, read off the reference XBLA build under Canary
(captures kept privately, out of the repository):

| | rgb | sd |
|---|---|---|
| actual XBLA, distant terrain | **(110.5, 147.0, 174.3)** | (13.7, 16.9, 20.3) |
| actual XBLA, unoccluded sky | (120.7, 163.4, 195.1) | (2.3, 2.8, 3.2) |
| ours, band NOT applied | **(92.4, 124.1, 159.9)** | (30.8, 31.7, 34.9) |
| ours, band applied | **(17.1, 48.9, 96.5)** | (2.2, 1.8, 1.3) |

No distance in any console capture comes near `(16,48,96)`. The level's band is
a SECOND atmosphere laid over one that is already there, toward the wrong
destination.

### What ships

`sl_wa_fog_level_applies` (`sl_gfx_worldasset.c`) - one rule, beside the
scene-assembly rule and reading the same fact: **the level's own fog band
applies to an external pass only while the level's own backdrop is the one on
screen.** A resolved replacement is exactly the statement that it is not.

- The band is still **decoded** by `sl_wa_fog_band` and still **reported**, so
  the census prints what the level is issuing whether or not it is applied;
  only whether it reaches this pass's vertices changes.
- **No colour, level id, item path or mesh index appears in the rule.** Nothing
  here chooses what the distance should look like - it stops overwriting what
  the asset already says.
- **The fog state handling is untouched.** `fog_apply` / `fog_neutral` stay
  exactly as the fog round left them, so the mirror desync that round fixed
  stays fixed: at `LINEAR(0..1)` a coordinate of zero is the incoming fragment
  untouched, which is why this is a value change and not a `glDisable`.
- Every degenerate case falls out of the condition: a stage with **no**
  replacement fogs exactly as today; a **missing, invalid or unuploadable**
  replacement leaves the game's own backdrop drawing and its own band applying;
  a presentation that selects no world assets resolves neither; the **additive
  skydome** is not touched at all.

`SL_BG_LEVEL_FOG=1` applies the level's band to the replacement anyway, which
is what this renderer did before the rule was written - the same kill-switch
idiom as `SL_SKY_WITH_BG`, `SL_BG_DECAL` and `SL_BG_XLU_BLEND`, and for the
same reason: the before and after are one binary.

### The picture, at the playtest's own mark

| band | before | after |
|---|---|---|
| below the panorama rim | `17.1 48.9 96.5`, sd `2.2 1.8 1.3` | `92.4 124.1 159.9`, sd `30.8 31.7 34.9` |
| largest row step, sky/backdrop band | 1.30 | **4.09** |
| actual XBLA, same metric | - | 2.61 .. 5.04 |

The step metric is reported with its limit stated: **1.30 is low because the
region is a flat constant**, not because it is right. It is the pair - a step
inside the console's own range AND a distance band that has structure and a
colour in the console's range - that says the transition is a ridge line under
a cloud layer rather than a wall.

### Controls

| | result |
|---|---|
| the playtest mark, `SL_BG_LEVEL_FOG=1` vs the pre-change build's default | **byte-identical** |
| the playtest mark, new default vs the pre-change build's `SL_BG_FOG=0` | **byte-identical** - the change lands on the fog VALUE and nothing else |
| the playtest mark, new default vs `SL_BG_LEVEL_FOG=1` | differs by **578938** px - the A/B is not vacuous |
| COMMUNITY HD, new default vs `SL_BG_LEVEL_FOG=1` | **byte-identical** |
| a set with a **skydome and no background**, both arms | **byte-identical** - the additive class is untouched |
| ORIGINAL, new build vs the pre-change build | **byte-identical** |
| Facility under XBLA | no world-asset line at all |

**A caveat this round measured and will not paper over.** The teleport-sampled
pose is reproducible at read 62 but not beyond it, and one ORIGINAL run in
three landed on a different decayed pitch and differed by 367310 px from the
other two - which were byte-identical to each other and to the pre-change
build. The teleport applies on the first frame the stan world is ready, and
that frame depends on load timing. Cross-build identity claims above are
therefore stated at read 62 and at the teleport-free gate-yard pose, and the
gate-yard arms are the ones with no such sensitivity at all.

### What this round did NOT fix

- **The mountain shell's own scale and shading** are unchanged and still open.
  What the repair restores is the asset's own picture; whether that picture is
  the right SIZE is the parked terrain UV / distance-shading debt and is not
  claimed here.
- The **water**, the tunnel's per-dash hue, the source `0x0800` CUTOUT
  classification debt, images 57 / 60 and the dome's lower-rim UV wrap are all
  carried forward unchanged.
- **The game's own remaining geometry still fogs toward the level's colour.**
  Props, characters and every model the distance shade applies to are untouched
  by this rule. They sit in the near field, where the band contributes nothing
  - the gate yard measured 0 pixels of the near yard moving under the band at
  all - but it is a boundary this rule deliberately does not cross, and if a
  distant prop ever reads wrong against the replacement's sky, that is where it
  will come from.


## Generated zenith clouds (D-023): a chosen divergence, not a repair

Everything above reproduces the remaster. This section does not: it is a
deliberate, documented, toggleable departure from it
(docs/divergences.md **D-023**), and it is kept apart so that nothing here is
read as a claim about what the console draws.

### What was reported, and what was measured first

After the anchor repair the playtest verdict was: *"I don't think it's the dome
brightness alone. My main issue is the cloud coverage. The clouds seem to be
concentrated in a ring shape around the dome."* The ring was measured
**source-authentic** before anything was changed:

- the dome is rings of constant `v` about a VERTICAL axis through the image's
  pole, with `u` linear in azimuth; the eye (the item's origin) sits about
  20.9k units off that axis, so the pole is seen at about 58 degrees
  elevation rather than overhead;
- per-row statistics of the image (each texel row is one ring): the rows
  from the pole down to row 155 have a luminance spread under 1 (a flat blue
  gradient), the spread then ramps to the band's plateau (about 14) and the
  band's mean colour settles by row 271; the band runs to row ~771, then
  mountains. Wrapped, that is a clear cap containing the zenith with a ring of
  cloud round it;
- the console's own picture looking up (the previous round's capture) is clear
  deep blue overhead, and the previous round had already shown Sightline's
  dome matching it at every stage.

Of three options (the remaster's unused cap draw, keep it authentic, or
generate clouds overhead), the decision was to **generate clouds overhead from
the dome image's own cloud band**, on the stated rationale that the defect
should be fixed even where the ROM itself has it wrong.

### The mechanism, and the one it replaced

**A separate cap CARRIER**, generated at preparation time by
`tools/worldasset/zenith.py` and appended to the skydome bundle as one more
draw after the source's:

- geometry laid on the dome's own surface (its measured ring table) from the
  pole out past the cap, with **planar** texture coordinates - the pole is an
  ordinary interior point of the carrier, so there is no convergence of
  columns to pinch, swirl or star;
- an image that is the dome's band reprojected top-down about the pole, with
  the cap and the band's fade-in replaced by a fill synthesised from the band;
- vertex alpha 1 over the fill, falling to 0 across a rim where the image IS
  the band, so the hand-off to the dome underneath shows nothing.

The alternative - writing the fill into the dome's own image, each cap texel
taking the fill at its own polar coordinates - was built and **rejected by
measurement**: the source mesh closes its pole with a two-vertex fan, so the
rows inside the first ring must be one colour or the fan smears them into a
swirl, and the fan's texture-space derivatives drive the mip level to ~2.5-3.8
there. Rendered offline with the same LOD rule and box mips as the renderer,
that left a flat disc at the pole whose small-circle spread was 1.89 against a
median of 3.64 elsewhere in the same view. A naive upward extension of the
band's first settled row, the control, produced the star: angular detail on
small circles about the pole 31x the median.

### How the fill is made

Everything is measured from the item, nothing names it: the pole, the axis
(from the first rings' plane normals - a vertex mean is biased toward the
seam's duplicate), the centre (a sphere fit constrained to the axis over the
first rings), the ring table and the `u` rule from the mesh; the cap, the
settled band and the band's extent from the per-row statistics, every
threshold a fraction of the band's own measured spread. On the Dam: cap to
17.8 degrees about the fitted centre, band settled at 23.2 degrees (row 271),
hand-over to 26.7, rim to 28.4.

The fill is synthesised in the azimuthal-equidistant plane about the pole:

- spot noise (ADSN) whose texton is the band annulus in a decorrelated,
  Gaussianised colour space, driven by ONE white-noise field shared by the
  three channels, mapped back through the band's own histograms - nothing is
  copied, so nothing repeats;
- the band's low frequencies carried into the disc by the exact harmonic
  extension from the hand-over circle (one FFT round the circle), so the fill
  starts where the band leaves off - locally clear where the band is clear -
  and the noise's own low frequencies fade in only inside it;
- across the hand-over, detail mixed variance-preservingly, so contrast does
  not dip into a ring.

A first version that took the fill's tone from the band's inner histogram
alone left a coherent arc wherever the band is locally clear; the harmonic
continuation is what removed it (per-5-degree-sector luminance step across the
hand-over, max over sectors: 20.0 before, 17.1 after, against 31.5 within the
band itself). A patch-based by-example alternative was also built; it showed a
lattice of bright specks and higher self-similarity, so it was not taken.

Deterministic (a fixed seed; four generations of the Dam bundle byte-identical)
and bounded: the whole preparation of the skydome takes under nine seconds
including extraction.

### What the bundle carries, and what the runtime does with it

The carrier is material 1 (its own 1024-square image, blended, flag
`SL_WA_MAT_GENERATED`) and mesh 1: 1729 vertices, 3392 triangles. The source's
header bounds, all 1277 source vertices, 7350 indices, material 0 and image 0
are **byte-identical** to the bundle before this change. The flag widens the
accepted set at the same version (the DECAL rule); the parser refuses it on
any class but the skydome, whose pass is the only one that consults it.

`sky_draw` asks `sl_wa_material_drawn(flags, SL_SKY_ZENITH)` per mesh.
**`SL_SKY_ZENITH` defaults on** (a design decision); `SL_SKY_ZENITH=0` leaves
every generated material out and the frame is the source dome exactly. One
line reports which, the first time a generated draw is met. Nothing of this is
drawn under ORIGINAL or COMMUNITY HD, because the skydome is not.

`zenith.py` is in the preparation's tool hash, so a change to it regenerates
the bundle without `-Force`. A dome image without the structure - no flat cap
at the pole, no band that settles below it, a mesh that is not rings about one
axis - adds nothing and says why in one line.

### The picture, measured

At the proxy of the playtest mark and five other poses (verta 89, the playtest
heading at 37.7, two reconstructions of the playtest's steep upward screenshots
and the pole itself), read 62:

- toggle off against the build before this change: **6 of 6 frames
  byte-identical**; the new binary with the old bundle: 6 of 6 identical;
- changed pixels outside the dome (ownership taken as "the frame changes when
  the dome is removed OR painted white") **zero** at all six;
- pole: small-circle statistics at the pole's own screen position sit inside
  the distribution of 237 random points in the same view (percentiles 54-80);
- repetition: best non-overlapping match of high-pass 3-degree windows in the
  fill median 0.44 / max 0.61, against 0.47 / 0.72 inside the band itself;
- tone: fill mean (160-168, 185-189, 201-208) against the band beyond the
  hand-over (165-170, 186-197, 203-217) in the same views.

### The round smudge in clear sky, explained

The faint lighter wedge the second playtest screenshot shows just right of
centre is the dome's **texture pole**: a direction fit of that screenshot
against the dome puts the pole at the smudge, and the offline render
reproduces it only with mipmapping on. The two-vertex fan's derivatives raise
the mip level, and at those levels the sampler's REPEAT in `v` pulls the
image's bottom (horizon) rows into the top. The mechanism is the source's own
sampler and mesh (its 2:1 anisotropy may weaken it on the console; not
measured). The carrier covers the pole, so with the zenith clouds on it is no
longer visible; with them off it is unchanged.

### Not claimed

- The console draws no clouds overhead; this is not a match to anything and
  is not offered as one. Acceptance is pending a playtest.
- The fill's character (soft, mottled) is a statistical match to the band,
  not painted cloud; the band's stretched cumulus shapes are not reproduced.
- Frame time: the carrier's cost was not separable from run-to-run spread
  (median 31.64 ms on against 30.80 / 30.84 ms before and off, n = 4 each).

### The cloud field: the game's own cloud plane and phase (#76)

Revised 2026-09-30. The cap carrier above ended 28.4 degrees from the dome's
texture pole - 17.7 to 40.5 degrees of elevation from the eye - so below it the
authored band showed, partly clear, down to the mountains and the D-026 haze;
and nothing in it moved (its coordinates were baked into the bundle and
nothing read the game's cloud state), where ORIGINAL's cloud layer drifts
continuously. Measured at a fixed walkway camera over 300 ticks: ORIGINAL's
sky moves 35-40 px per 50 ticks, the XBLA sky only with the camera's 1 px
sway.

**The game's contract** (sky.c, lv.c). `skyTick` adds `g_ClockTimer` to
`g_SkyCloudOffset` every game tick and takes 4096 off it past 4096;
`g_ClockTimer` is zero only while controls are locked or the multiplayer pause
is up (the solo watch does not stop it). `skyRender` casts each sky ray from
the eye onto a horizontal plane at the environment's cloud height (the Dam's
5000) and textures it in the RDP's 10.5 fixed-point units as s = 0.1 x,
t = 0.1 z + `g_SkyCloudOffset`; its cloud image (the Dam's is 64 x 64) repeats
every 32 x 64 = 2048 of those units, so the 4096 wrap is two repeats and never
shows.

**The field.** The generated draw keeps its place (one flagged draw after the
source's, `SL_SKY_ZENITH=0` leaves it out) and changes its three parts:

- geometry: a cap of directions about the vertical from `zenith.LOW` = 8
  degrees of elevation to the zenith, 3-degree rings of 48 columns (1345
  vertices, 2640 triangles), vertex alpha 0 at 8 degrees, 1 from 18 up, a
  smoothstep between - so the field fades into the dome only where scenery and
  haze already stand in front of the horizon;
- image: one 1024-square tile, periodic in both directions - spot noise over
  eight Hann-tapered windows of the settled band, taken undistorted in the
  plane about the pole, each driven by its own white field on the tile's torus,
  mapped back through the band's histograms;
- coordinates: none stored that are read. Materials flagged
  `SL_WA_MAT_CLOUD_PLANE` (32, a generated skydome material only) are cast per
  vertex, per frame, by `sl_wa_cloud_coord` - sky.c's plane and phase, from the
  game's eye and cloud height (`sl_wa_sky_cloud`, sl_world_assets.c) - into a
  projective coordinate GL interpolates exactly across each triangle of
  directions. The tile stands for half a repeat of the game's image
  (`SL_WA_CLOUD_TILES` = 2): at the band's settled ring as seen from the walkway
  that puts one tile pixel at one band-image pixel (10.0 against 9.7 world
  units); one tile a repeat measured half as sharp and flatter than the zenith
  fill it replaced. A density, not a speed: the field moves as the game's plane
  moves either way.

The capture line (`sl_shot`) names the cloud phase each frame shows.

**How the profile was chosen.** Three profiles, the same motion: transparent
at 18 / 8 / 2 degrees, whole 8 / 10 / 6 degrees above. Over 72 views (six eyes,
twelve headings, pitch 10) the 18-degree profile left the still authored band
showing beside moving cloud in 58 (at the walkway a streak of it under the
field); the 8-degree profile's fade showed in 4 views, at most 49 pixels, and
the 2-degree one added nothing visible beyond it. Camera-compensated block
motion over 300 ticks at the walkway: still sky blocks 8 in ORIGINAL, 14 with
the 18-degree profile (on the band streak), 7 with the 8-degree one. The
8-degree profile is the one taken.

**Measured.** Same tick twice, the same frame; later ticks move the sky 35-39
px per 50 ticks against ORIGINAL's 35-40 at the same camera; across the 4096
wrap both move 4 px per 5 ticks with no jump; a presentation cycle back to XBLA
returns to the never-switched frames but for its caption. `SL_SKY_ZENITH=0` on
the new build and bundle is byte-identical to the previous build's at 24
witness frames, and with the sky pass left out the two builds agree at all of
them - every changed pixel is the field's. Frame time unchanged (18.91 ms
before, 18.82 after, reservoir wide sky).

**Not claimed.** The tile is a statistical texture of the band, not painted
cumulus, and the plane's perspective packs it toward the horizon as ORIGINAL's
does. The console's dome does not move; this is not a match to it.

## Presentation fog range (D-025): the Dam gameplay row drawn to 30000

Like the zenith clouds this is a deliberate departure from the remaster, not a
repair (docs/divergences.md **D-025**). The remaster's own Dam gameplay row
(environment 33: start -100, end 15000 world units, colour 133 173 202) and
its distance equation are reproduced exactly - see "The fog contract" above
and the backlog's walkway entry. At the walkway the mountain range stands
20000..30000 units out, so under 15000 it is drawn at exactly the fog colour,
which is also the remaster's horizon sky: the range is not there at all.

Sightline presents that one row with a fog end of **30000**:

- **Where the value lives.** The presentation policy
  (`sl_presentation_fog_end`, src/platform/sl_presentation.c) holds one table
  row: set `xbla`, environment 33, end 30000. The provider keeps the SLEN
  table as authored and builds the table it draws with from it
  (`sl_wa_env_presented`); `sl_wa_env_lookup` answers with the presented
  row. The generated `environment.slen` still carries 15000 - it is the
  source's metadata and is never rewritten. The load line prints both:
  `environment row 33 fog end 15000 authored, 30000 presented (D-025)`.
- **What moves.** Only `fog_end`. Colour, start, id and
  `sl_wa_env_fog_coord` are the authored ones. Because the lookup is the one
  place every consumer reads a row, the replacement background and the game's
  props and characters fogged by the same row (`sl_wa_object_fog_env`) move
  together; nothing native and replaced disagree at a seam.
- **What does not.** The intro cinema row (933), the multi-player rows
  (233..433), every other stage's row, ORIGINAL and COMMUNITY HD (no world
  asset set, so no row at all), the skydome (reads no row), and every
  visibility or distance cull - the game's own tests, the far fog at 75000,
  the room set. What changes about a pre-existing cull is only how much of
  the object is still showing when it happens.
- **The control.** `SL_XBLA_AUTHENTIC_FOG=1` presents every row as authored;
  measured byte-identical to the build before at every witness.

### How the value was chosen

Three Dam cameras (the walkway, a view down the dam across two towers to the
range, a reservoir shore view), fog ends 15000 / 22500 / 30000 / 37500 and a
no-fog geometry reference, with the smallest end that leaves the range
plainly present taken. Ray cast against the source background, the fraction
of each pixel's own colour left after fog, measured from the frames (and
matching `(end - d) / (end + 100)` to 0.01):

    walkway pixel          distance   15000   22500   30000   37500
    mountain, centre         22051    0.00    0.02    0.26    0.41
    mountains (9 px)   18343-30793    0.00  0-0.18  0-0.39  0.18-0.51
    watchtower                6109    0.59    0.73    0.79    0.83
    dam / terrain             8752    0.41    0.61    0.70    0.76
    outer ring               62422    0.00    0.00    0.00    0.00

At 22500 five of the nine mountain pixels are still pure fog and the centre is
at 2%: a ghost at the left edge. At 30000 the silhouette and the snow and rock
structure read across the frame (the local luminance spread 4.5..7.6 levels
against 18..56 with no fog), the watchtowers still separate by distance
(down-dam: near tower 0.92, far tower 0.53, range 0.03..0.25), the reservoir
shore keeps its haze, and the outer ring stays fog.

### Revised (2026-09-30): 54537, placed on the cartridge's curve

At 30000 the far half of the range behind the walkway still drew as flat fog,
and so did the towers seen down the dam. The end moved outward, and this time
it was placed on the cartridge's own Dam fog rather than on a sweep. The
cartridge's level fog is the band 995..1000 per mille of window depth under
near 5, far 15000 (eye units) and visibility 0.2. In world units that is

    f_O(d) = 1.0667 - 5001.7 / d

It is zero at 4689, 50% at 8826 and 90% at 30004, and it reaches full fog
only at the far plane, 75000. A linear range cannot follow that tail, so
three ends were taken from it:

- **A = 34074**: the linear range meets f_O at the farthest mountain
  (30793 units, 90.4% fog).
- **C = 75000**: f_O's own full-fog distance.
- **B = 54537**: their midpoint.

Six cameras were read at 62: the walkway, down the dam to the towers, the
reservoir shore, the wide sky, the gate yard, and the D-026 worst case. At
each, the table gives the share of fog-touched scenery pixels in the frame's
far third still at least a quarter of their own colour. Visibility is the
projection of (frame - fog colour) onto (no-fog frame - fog colour).

    camera        30000   A 34074   B 54537   C 75000
    walkway        47.4     53.9      62.2      29.6
    down the dam   26.2     35.7      70.2      51.3
    reservoir      62.7     66.3      80.0     100.0
    D-026 case     58.3     68.3      78.3      56.9

A leaves the far right of the range and the far towers washed out. C lifts
the fog off the panorama shell (52000-55000 units): the painted peaks and
their cloudless band come through above the ridge, which is D-026's
transition exposed. The walkway and D-026 figures drop at C for that reason.
**B** is kept:

- The range reads across the walkway at a third to two thirds of its own
  colour, 20000..35000 units out.
- The towers down the dam separate.
- The reservoir's far shore holds its depth.
- The scene still ends in haze inside the far plane.
- The clouds still meet the mountains at six headings from two eyes.

The table row is (xbla, 33, 54537). Colour, start and equation are
unchanged, and `SL_XBLA_AUTHENTIC_FOG=1` still draws the authored 15000.

## The panorama shell (D-026): the fully fogged ring fades toward the sky

A presentation departure from the remaster (docs/divergences.md **D-026**).
The Dam background's outermost ring is a painted panorama - peaks, and sky
above them - on two draws of one image: an opaque wall and a translucent band
over it whose vertex alpha runs from 1 to 0. It stands 52000-55000 world units
from any eye on the stage, beyond the gameplay row's fog end at 15000 and at
30000 alike, so its own programs draw it at exactly the fog colour. It is
drawn after the sky dome and over it: from the dam, everything between the
ridge and the dome's cloud band is that flat colour.

### Which draws are the shell

`prepare.panorama_shell`, from the geometry alone. Each mesh is measured by
the horizontal distance of its vertices from the centre of the item's bounds;
the shell is the set of meshes that lies wholly farther out than every other
mesh (its nearest vertex beyond their farthest) and that, taken together,
surrounds the centre - no azimuth gap wider than 60 degrees. Of the sets that
qualify the outermost is taken, and its materials must be its own. The
preparation prints what it found:

    panorama shell: meshes 48, 88 (2 materials) - every vertex 12153..12908
    out, the rest of the item within 11829; largest gap round it 24.0 deg

Those materials carry `SL_WA_MAT_PANORAMA` (16), accepted on the background
class only, at the same layout and version - the DECAL rule. The Dam bundle
differs from the one before in exactly those two flag words. An item with no
such ring, or whose ring shares a material with the scenery inside it, marks
nothing and says why.

### What the runtime does with it

In `bg_draw`, only while the remaster's stage fog is on the pass and the dome
was drawn beneath it this frame:

- **The haze top.** The vertex loop already visits every vertex for its fog
  coordinate. Each non-shell vertex the fog reaches (coordinate above 0.85)
  goes into a 0.25-degree azimuth bin, keeping its elevation tangent seen from
  the eye, weighted by `sl_wa_panorama_reach` (0 at 0.85, 1 at full fog,
  smooth). Each shell vertex's haze top is the highest bin within the widest
  gap between the shell's own columns (seen from the eye), tapering to zero at
  twice that - so both columns of every face of the shell reach over the whole
  face. Scenery the fog has flattened to its colour therefore always stands
  against fog-coloured shell.
- **The fade.** Above the haze top the shell's coverage falls along a
  smoothstep over `SL_WA_PANO_FADE` = 6 degrees of elevation. The coordinate is
  linear in the elevation tangent, which is exactly linear up the shell's
  vertical faces, so the profile holds across each face with no interior
  vertex.
- **Fully fogged only.** A shell vertex whose fog coordinate is below 1 keeps
  coverage 1: partly fogged pixels are the ordinary fog equation. The Dam
  intro's cinema row (end 75000) reaches the ring, so the intro is unchanged.
- **The draw.** The shell's meshes draw blended with texture unit 1 holding
  the profile (row 0 whole, row 1 the fade) under MODULATE: the fragment's
  colour and fog are what they were, and only its alpha is scaled. Where that
  alpha is 1 the blend writes the fragment unchanged. Depth writes stay as the
  material's class sets them.

`SL_SKY_PANORAMA=0` draws the shell as authored; the first frame that fades it
prints `#74 worldasset: panorama shell of background/dam fades where the stage
fog covers it (D-026)`.

### Measured

- The haze is what keeps fully fogged ridges from standing as flat
  silhouettes against the clouds: with the shell simply hidden, such a ridge
  shows at elevations up to 25 degrees from the walkway.
- The dome's painted lower rows never show: a probe dome with its image rows
  from 700 down in magenta changes no pixel in 72 views from six eyes, with the
  fade on or the shell hidden - the mountain ring closes the circle in front of
  them.
- Fade width, over 65 swept views: the share of the shell's footprint showing
  more than half sky 0.83 / 0.72 / 0.52 for 6 / 12 / 20 degrees; contrast just
  above fully fogged silhouettes 11 / 4 / 1 levels at the 95th percentile. The
  smallest width that shows the clouds above the walkway's mountains and the
  reservoir's wide sky with no line in the frames is the one taken.
- The pixels it changes lie inside the shell's footprint in every measured
  frame; switched off, and with the shell hidden, the build is byte-identical
  to the one before.

### Entering the fade (#80)

In a playtest, the player walked Surface's snow toward the far tower and marked the distance
"where it blends into the clouds" popping (run 20261005-120919-lvl36, marks
1 - 4, XBLA). Reproduced by stepping the teleport along the line between two
marks every game frame and capturing every frame (seed pinned, a private
instrument): walking from mark 3 to mark 4, one frame step changed 208407
pixels of the distant band at once - 38 times the walk's median - and the next
frame was playtest mark 4: the fogged shell standing opaque to its painted
top with the mountains showing through it. Across that step the dome, the
haze tops, the far pass and the visibility were the same; what changed was
the shell's own "fully fogged" count, 87 -> 85 of its 96 vertices. The test
was a step at a fog coordinate of exactly 1, carried per vertex in unit 1's
second coordinate and interpolated across faces that each span tens of
degrees of sky, so a vertex crossing 1 switched its faces between drawn and
faded in one frame. The coordinate depends on the eye's position, not its
heading (constant through 60-degree yaw sweeps at all four marks), which is
why it showed while walking. Not level of detail, not the far pass, not the
room traversal.

The rule now enters over the last `SL_WA_PANO_COVER` (0.05) of the
coordinate, smoothly (`sl_wa_panorama_fogged`): a vertex fully fogged fades
as it did, one at or below 0.95 keeps its full coverage as it did, and in
between the coverage moves with the eye. Measured on walks 1 -> 2, 2 -> 3 and
3 -> 4: before, one coverage jump of 1.0 on each walk and a one-frame pop on
two of them (8.7 and 38.7 times the walk's median); after, no vertex's
coverage moves more than 0.014 in a frame and no step stands above the
near-field parallax of the walk (at most 2.8 times its median). At the four
playtest marks: mark 3 is byte-identical, marks 1 and 2 differ by at most
8 and 23 levels inside the shell, mark 4 is the un-popped picture. A wider
entry (from 0.85, the haze's own reach) moved the accepted marks by up to 53
levels and was not taken. ORIGINAL and COMMUNITY HD at the four marks and the
Dam's walkway witness under XBLA are byte-identical.

## The water: two draws in a 36-byte pool, and the image their blend consumes

**What was wrong.** At an engineering camera over the Dam reservoir (teleport
(6226.1, 413.6, -3387.4), theta 225.9, verta -28.95, room 72) 298429 pixels -
31% of the picture - were the flat backdrop colour: nothing rasterised. All of
them are identical with no replacement mesh drawn at all (`SL_BG_ONLY=9999`),
so no mesh owns them. The water was **absent**.

**Which draws are the water.** Draws `0x605c` (19626 triangles) and `0x6524`
(374), style `0x4702`, four sampler slots `{68, 69, 70, 71}`. They are the only
two draws whose vertex program declares the `c_sinwaves*` and texture-matrix
constants, the running remaster issues both every frame (depth test on, depth
write off, blend `SRC_ALPHA / ONE_MINUS_SRC_ALPHA`, measured from its register
state), and their geometry is one horizontal plane at item y -192.6 (world
-824.4): 94.75% of the empty pixels are rays that meet that plane inside these
draws' footprint. `read_level` was dropping both, because their pools
(10 and 11) have a 36-byte vertex whose position is not at +0.

**The layout** is read from the item's own vertex program, whose fetch
instructions carry offset, format and stride (see `POOL_LAYOUTS` in
`tools/worldasset/xblaworld.py`): coordinate pair +0 (the wave term's),
position +4, normal +16, **texture coordinate +20**, tangent frame +24/+28,
colour +32. The coordinate is an unnormalised integer and the program's two
texture matrices are diag(1/128) - captured from the running game and present
in the item's rendergraph - so its scale is **128**. The same fetch decoder on
the 24-byte pools' program reproduces the layout already measured for them.

**The image.** The pixel program, with the constants the running game gives
it, outputs alpha = slot 3's alpha exactly and colour = 0.926 x vertex colour x
slot 3 + 0.074 x a sphere-map reflection (slot 1, looked up by the normal map
in slot 0; slot 2 offsets the slot-3 coordinate by 0.1 of its value). Slot 3
(image 71, the only one with alpha) is therefore the base. The existing
normal-map rule would have taken slot 1, a photograph of a shoreline; a
translucent draw naming several slots now takes the one slot whose image
carries alpha (`translucent_base` in `prepare.py`). No other multi-slot draw
of the item is translucent.

**Result**, one mesh (60000 indices, image 71, `ALPHA | FOG`):

| at the engineering camera, read 62 | before | after |
|---|---|---|
| empty backdrop pixels | 298429 | **15681** |
| pixels changed | - | 823362 (all inside the water's own coverage, plus 561 HUD-text edge pixels at <= 3 levels) |
| the same bundle with the water mesh hidden (`SL_BG_HIDE`) | - | **byte-identical to before** |
| layout reverted only | - | bundle byte-identical to before |
| base-slot rule reverted only | - | the shoreline photograph tiled over the lake |

The foreground rock is the lake bed (y -241..-252), now seen through the water
as in the source. The remaining 15681 pixels are a far strip beyond the edge of
the source mesh.

### Not reproduced

- The 7.4% sphere-map reflection and the 0.1 coordinate offset from slot 2:
  the water is its base term only. (The vertex program's sine-wave
  displacement is reproduced - see "The water moves".)
- No console frame of the reservoir was captured; the reference is the
  console's register and constant state for these draws, not a picture.
- The far strip beyond the mesh edge (above).

## The water moves: the vertex program's own contract, carried as data

**What was wrong.** The water drew as a static surface. The source's water
is not static: its vertex program displaces every vertex, every frame, and
the material's whole motion is in that displacement.

**What the source does**, read from the two water programs' microcode, the
item's constant tables and the constants the running game hands them:

```
w        = (the +0 pair, a signed integer)  x  LITERAL.z
theta_i  = frac( -t x speed_i + phase_i + w.b x freqB_i + w.a x freqA_i )
angle_i  = (theta_i x 0.98 - 0.5) x 2 pi
s_i      = angle - angle^3/6 + angle^5/120 - angle^7/5040
position += normal(+16) x sum_i amplitude_i x s_i
```

One register lane per wave, four lanes, two live on the Dam: a short
diagonal chop of a few item units and a longer, gentler swell, both at the
same rate, a period of a few seconds (the values themselves are the item's
and stay in the generated bundle). The polynomial is the program's sine and
is evaluated as shipped - its shape at the ends of the turn is part of what
the source draws. Nothing else in the
material moves: the texture coordinates the program emits have no time
term, the pixel program's constants never change (measured over 2935
consecutive console frames), and the offset-map warp and the reflection ride
on the moving surface.

**Where every number lives, in the item.** The wave table (amplitude, phase,
speed, the two frequency rows) is uploaded by the command list before each
water draw - op `0x03`, register 32, seven rows, one upload per program
variant - and repeated in the program's default block, which op `0x20` names
by rendergraph offset and which also carries the evaluation constants. The
coordinate scale is the program's own literal bank: the sixteen bytes in
front of its microcode in the model blob, `(0, 1, 1/128, 0)` on both water
programs, `.z` being the scale; the running game hands the program exactly
that as c255, on every frame measured. The +0 pair is bit-identical to the
texture coordinate on every water vertex. These records scope to the
program binding (op `0x2e`): the two small draws that follow the water in the
stream bind another program and inherit nothing.

**The time.** The program's time constant is seconds: measured on the
console it starts near zero when gameplay begins, advances by exactly one
frame period (1/30 s) per rendered frame - 2935 consecutive frames, no
stall, no jump - and keeps advancing behind the pause screen (the water
draws are issued through the watch and the clock does not stop). The
runtime's clock is the game's own VI frame counter (`currentFrameCounter`,
frametiming.c) over 60: it runs through the pause for the same reason, it is
deterministic under a replay, it is independent of how many frames the window
presents, and it is never the wall clock. The simulation never reads it back,
so the per-tick trace cannot move.

**What ships.** The reader carries the per-vertex fields and the record
(`_pool_aux`, `wave_program_literal`, `wave_record` in `xblaworld.py`; the
block's copy of the table is checked against the upload row for row); the
bundle carries them as SLWA version 2 (above); the runtime evaluates the
record per animated vertex in the item's model space, before the level scale,
on that clock (`sl_wa_anim_displacement`), and `SL_BG_ANIM=0` draws the
bundle at rest. The displacement at any instant is not zero - the spatial
phase terms see to that - so the accepted static surface is the mean the
water heaves about, by up to ~4 item units.

**Result**, at the playtest's dock mark (eye (1549.4, -474.7, -7468.6), yaw
71.99, pitch -0.70, room 71), three reads two seconds apart, the pinned seed:

| | |
|---|---|
| the same binary with `SL_BG_ANIM=0`, and the previous bundle through the new binary | **byte-identical to the previous build** at all three reads |
| animated vs at rest, same read | 166403 / 163494 / 166603 pixels differ, **4 / 10 / 7 of them outside the water** |
| the camera | not still even here: the standing drift moves ~2.8 px per two seconds and every pixel the animated build changes outside the water is one the static build changed itself |
| the animation's own term D = animated - at rest, drift-compensated, against its earlier self | correlation **0.03 / 0.06**; the static water's texture against its earlier self under the same compensation: 0.96 / 0.93 |
| repeats | identical, all arms |

A frozen displacement would ride the camera as the texture does; it does not.
The vertex arithmetic was also checked on the item's own vertices through two
independent evaluations (the tree's function reading the production bundle,
and the program's instruction sequence in float32 with the constants read
from the item), agreeing to 0.0005 item units.

## The east shore: a second draw record the reader never took

**What was reported in play.** "The water looks good, but the treeline next to the
water doesn't look good" - mark `20260926-180142-lvl33` #1, eye (2054.4,
-474.7, -5020.6), room 71. Along the far (east) shore a flat dark-navy band
runs between the water and the snowy slope, and the pale trees stand on it.
Reproduced by teleport at the mark's view-matrix yaw (265.29 - the mark's own
look angle and view matrix disagree by 1.4 degrees; the matrix reproduces the
picture), pitch -0.70, read 62: **25551** pixels within 3 levels of the
backdrop fill (16, 49, 99), columns 0..992, 22-32 rows tall.

**Who owns those pixels.** Nobody. The band is identical with the skydome
off and with the original room geometry drawn as well, and a ray cast of the
prepared bundle at the same camera hits nothing along 98.8% of the band's
rays. Above it is the east-bank terrain, whose lower edge sits at item y -50
to -90; below it the water, whose far edge is the water mesh's own straight
x = 3307.9 boundary. The rays pass under the terrain, over the water, and out
beyond the water mesh. On the west bank the same gap is closed by a snow
embankment (image 47); on the east bank there was none.

**What the source carries.** Two vertex runs that no draw the reader took
references - pool 0's last 45 vertices (3 rows x 15, y -49..-168) and pool
3's last 30 (2 x 15, y -125..-256) - and two command records of opcode
**0x30**: `(prim, count, index-buffer handle, condition)`, quad lists of 112
and 56 indices, in the image-47 blocks, whose indices are exactly those runs.
Their geometry is the east shore, vertex-exact with its neighbours: the
east-bank terrain's lower edge is the first strip's top row, its bottom row is
the second strip's top row, and the second strip's bottom row is the edge of
the east lake-bed pieces. Those neighbours are the two **op-0x17** blocks,
`(condition, skip target)`, which the reader already carries as ordinary
draws. The band's rays hit the op-0x30 geometry and no other source geometry
(3178 of 3178 sampled); the pixels just above it are the op-0x17 terrain
(110 of 125).

**What the console does with it.** In the two frames already captured from
the running remaster (the level start, far from the reservoir), matched to the
item by primitive, count and every bound texture: it issues the item's draws
except the two op-0x17 blocks and the two op-0x30 draws. The two forms are
conditional in the same way. This pipeline has no such visibility and draws
the op-0x17 blocks always, so the op-0x30 draws are carried the same way
(`level_draw_records` in `tools/worldasset/xblaworld.py`).

**First wrong stage: source discovery.** `read_level` took opcode 0x01 as the
only draw record.

**Result** (preparation only; no runtime change):

| | before | after |
|---|---|---|
| draws read / kept | 389 / 388 | 391 / 390 |
| triangles | 110360 | 110444 (+28 +56) |
| vertices, materials, meshes, images | | byte-identical; only meshes 50 and 66 grow, their old triangles an exact prefix |
| backdrop pixels at the playtest mark | 25551 | **0** |
| pixels changed | - | 64334, all drawn by the two meshes that grew; with those two hidden the frame is byte-identical to before |
| the same reader with op 0x30 refused | - | bundle byte-identical to before |

Of the changed pixels, 25551 were the band and 38783 were water seen beyond
the east shore, now behind the bank.

### Not established

- What the condition word selects. Both forms carry 0 on this item; this
  pipeline does not need the answer, because it draws both forms always.
- A console frame of the reservoir. The console reference is its draw
  stream at the level start, not a picture of this shore.

## The ladders: one pool read at four times its texture scale

The Dam's ladders and the railing panels beside the towers are CUTOUT cards
in the background item - a 256x128 ladder image (two rails and one rung) and
a 256x256 railing image - drawn from one small pool of 205 vertices. The
reader measures each pool's texture-coordinate scale from where its values
pile up (`xblaworld._uv_format`), and took the LARGEST divisor that at least
1% of the values land on. In this pool 23.6% of the values are multiples of
128 and 1.75% of 512, so it read the pool at 1/512: every ladder showed a
quarter of its image, one rail stretched into a thick rusty bar with stubs
of rung beside it. The same ladder image drawn from two other pools reads at
1/128, as every pool of the item does, and at 1/128 the ladder card is one
image wide (17 item units per repeat on a 16-unit card) with the original
ladder's rung count.

The rule is now the candidate whose values pile up most ABOVE CHANCE (hits x
divisor), ties to the larger. Re-measured over the Dam item: only that pool
changes. The regenerated bundle differs from the previous one in exactly 205
vertices' texture coordinates (x4); every other byte, the environment file
and every other bundle are identical. At the playtest's ladder mark the frame
changes only inside the two meshes that draw the pool (0 pixels outside, by
hiding them); ORIGINAL and COMMUNITY HD are unchanged.

## The wall behind the ladder: a draw whose program blends two images

Standing at a tower's ladder, the wall behind it showed bricks set back from
the concrete face. They were the tower's INTERIOR wall seen from behind: a ray
through the tower-wall mark meets the ladder plane at 98 world units and then the
back face of the interior brick draw at 131 - the mark's own depth - and
hiding the brick mesh shows the room inside. The exterior face is one draw of
six triangles over all three towers (pool 5, 32-byte records) naming two
images: slot 0 the 256x128 ladder image, slot 1 the concrete. The preparation
drew slot 0 as the surface, a CUTOUT, so the wall existed only where the
ladder image is opaque. The same bundle from before the ladder pool's scale
change shows the same hole; that change did not cause it.

What the draw is comes from its pixel program, read from the item (Xenos
disassembly, registers named by the item's constant table):

    t0 = tex(slot 0, interpolator 1)      t1 = tex(slot 1, interpolator 2)
    rgb = lerp(i5 x t1, i4 x t0, i4.a x t0.a)      then tint and stage fog

and every vertex program of a 32-byte pool feeds +16 -> 1 and +20 -> 2 (the
two coordinates, one scale), +24 -> 4 (a colour with alpha) and +28 -> 5 (a
colour). On the tower wall +24 is black at alpha 0x8b: the second image is
the ladder's SHADOW laid on the concrete. Seven more draws (pool 3, opaque
images) run the same blend weighted by the colour's alpha alone.

The preparation now reads it that way (`xblaworld.layer_blend`,
`LAYER_INPUTS`): each draw's first bound program is EVALUATED on numbers -
random inputs, the game's measured light state - and a draw is two-layer only
when its colour equals that lerp for exactly one assignment of images,
interpolators and weight. Over the whole item that is exactly the two
programs above; nothing else (the foliage's normal-mapped pair included)
matches. Such a draw is carried as its BASE surface (slot 1, +20, +28) and a
coplanar LAYER over it (slot 0, +16, +24) blended by source alpha - which is
the program's weight - and flagged as the decal pass. 8 draws, 554 vertices
re-read, 11 more meshes; the decal census from the style bit is unchanged.

| tower-wall mark (seed pinned, read 62) | before | after |
|---|---|---|
| the wall behind the ladder | interior brick, back faces | concrete, the ladder's shadow on it |
| frame | E8436131 | 18F3B46F (repeat identical) |
| the same preparation with the two-layer reading off | - | bundle byte-identical to before |
| the changed meshes hidden, before and after | 94A146F6 | 94A146F6 |
| ORIGINAL / COMMUNITY HD | D3ED9091 / 45C60680 | identical |

## The measurement instrument (diagnostic only - no feature behind it)

The identification above needed an instrument, and the instrument is kept so
the finding is reproducible. **It changes nothing by default**: it is three
environment variables, one cached int test on the triangle path when none of
them is set, and it is wired to no setting, no presentation and no key.

```
SL_WR_CENSUS=1        per (room, list) triangle counts and screen area,
                      printed with SL_DL_CENSUS's report
SL_WR_HIDE=<list>     suppress these rooms' native draw
SL_WR_ONLY=<list>     suppress every OTHER room's native draw
```

Each list is comma-separated room numbers, optionally suffixed `:1` (primary)
or `:2` (secondary). The census counts **before** the suppression can remove
anything, so it always describes what the original draws.

One reading of it was wrong and is worth keeping: the first version took each
triangle's NDC area without requiring the triangle to be in front of the eye
or clamping it to the screen, and it ranked three rooms that paint nothing at
the top of the table. Pixels measured by hiding a room and differencing the
frame are the authority; the area column is a ranking hint.

### What this instrument is NOT

It is not the replacement, and none of these three variables is reachable from
a presentation. The replacement's own suppression is a separate flag with its
own gate (above); these stay what they were - a way to ask what a room paints.

Three more, all diagnostic, all off unless named, added with the replacement:

```
SL_BG_NOSUPPRESS=1    draw BOTH - the original and the replacement - so a
                      misalignment is visible rather than argued about
SL_BG_WHITE=1         hold the vertex colour at white, so what is left on
                      screen is the texture alone
SL_BG_NOMIP=1         sample level 0 only, which separates mip collapse from
                      a coordinate that is wrong
SL_BG_TRACE=1         the eye the GAME has, the render origin, and the eye
                      RECOVERED from the transform, with the scale and offset
                      actually applied - the three points that decide where a
                      world-anchored asset lands
SL_BG_WORLDBOX=1      the box the GAME's own room background occupies and the
                      box the REPLACEMENT occupies, in ONE space, plus the
                      stage's room extent and level scale read from bg data
SL_BG_DECAL=0         draw the source's DECAL PASS with no depth priority,
                      which is what this renderer did before the pass was
                      carried. The kill switch that makes a coplanar
                      before/after one flag on one binary
```

One more belongs here even though it is not this feature's: **`SL_VI_CATCHUP=0`**
(`src/platform/sl_ultra_shim.c`). Left at its default the VI is a wall clock, so
how many retraces elapse before a given pumped frame depends on how fast the
machine ran, and two captures of one teleported pose reached the same frame
**fourteen game ticks apart**. Any teleport-sampled comparison of two renderings
needs it, and the coplanar round above is the first that could not have been
measured without it.

`SL_BG_WHITE` and `SL_BG_NOMIP` are what said the flat-facet render was the
TEXTURE and not the shading or the filter; `SL_BG_WORLDBOX` is what produced
the level scale of 4.2801 and retired the k = 5 reading.

## Characters and heads (#70): the remaster's own models on the game's skeleton

A character's look under PRESENTATION XBLA used to stay the cartridge's, and
the cause was not a texture seam. Measured on the Dam gate-yard guard
(`Cgreatguard2Z` body, `CheadleeZ` head):

- **The texture provider is already asked for every character texture.** A
  body or head file's images load through `texLoadFromModelFileHeader` ->
  `texLoad`, which registers them (#47), and the renderer's lookup names all
  21 of this guard's images - no unregistered source, no shape miss.
- **COMMUNITY HD has almost nothing to give.** Of the 21 images only `07a9`
  (the coat front) has a pack file by the Rice texel CRC the pack's own names
  carry, and it is applied; the head has none. Across the 28 character model
  files the 20 campaign stages load (248 distinct images) the pack covers 2.
  No unmapped pack file is character art. ORIGINAL fallback is the correct
  answer there.
- **The remaster does not repaint these images - it replaces the model.**
  `char/greatguard2` and `head/headlee` exist in both generations; the
  remastered ones are new meshes on two unwrapped atlases (a 512 x 512 body,
  a 256 x 256 face) with no one-to-one image identity to map. So the gap was
  a missing asset seam, not a texture one.

### The seam

`src/sl_chr_asset.h` is the contract. In `chrRenderProp`, before the game
walks the character's model, `sl_chr_asset_begin` (src/native) asks the
world-asset provider for `char/<item>` and, when the character wears a
separate head, `head/<item>`, where the item is the model file's own name
(`C<item>Z`). A found item is related to the model's skeleton (below); the
seam then emits one bridge command (`0x02 'S' 'C' 'H'`, the record as the
second word) in the opaque pass and marks the replaced part. The walk itself
is untouched - distance LOD, switches and the head attachment still apply -
and `modelRenderNodeGundl` / `modelRenderNodeDl` withhold only their
`gSPDisplayList` for a node of a replaced part: body nodes are those outside
the model's head node, head nodes those under it. The renderer draws the item
where the bridge sits (`chr_asset_draw`, `src/gfx/sl_gfx_dl.c`).

A part with no item draws as the game's. A fading character (`PropType` 8)
keeps the game's model and its fade. `SL_CHR_ASSETS=0` keeps the seam dark
under every presentation - the control.

### Who owns the neck

The two vocabularies put the neck in different parts, and that decides which
pairings compose (`sl_chr_compose`, `src/gfx/sl_gfx_charskin.c`):

- **The game** draws the neck as part of the BODY model: withhold the body's
  lists and the neck goes with them while the head's own lists still draw; a
  separate head file (`Chead<name>Z`) is the head alone.
- **The remaster** draws it as part of whichever HEAD is drawn. A body item's
  gated block - the draws `SL_WA_MESH_BUILTIN_HEAD` marks - is its built-in
  head: neck (weighted between `SKEL_NECK` and `SKEL_BACK`) and face (on
  `SKEL_NECK` alone) together, and no always-drawn body draw reaches above
  the collar. A separate head item carries the same two groups - its own
  neck, down to the same base under the collar - and has no gate.

So:

| wears a separate head | body item | head item | drawn |
|---|---|---|---|
| no | yes | - | the body item, its built-in head included |
| yes | yes | yes | the body item without its built-in head, and the head item |
| yes | no | yes | the game's body and the head item: the item's neck closes over the game's neck into the game's collar |
| yes | yes | no | **the game's body and head** - the body item is held back |
| - | no | no | the game's character |

The held-back row is the one that matters. A remastered body under the
game's own head has nothing that draws a neck - the game's neck went with the
game's body, and the item's went with its built-in head - and the head floats
above the collar. Rather than draw that, a character whose separate head has
no item draws exactly as the game's until the head is prepared too. Drawing
the body item's built-in head instead would compose, but with the body's
default face rather than the head the game chose for that character.

`SL_CHR_PRES=1` prints, per character and pass, what the seam decided for
each part (`MISS`, `REFUSED`, `HELD`, `REPLACED`), the bridge record, and how
many of the game's own lists each part kept and withheld; the renderer adds
what each bridge record drew (meshes, built-in-head meshes left out, vertices,
triangles).

### The skeleton relation

The item's pose block names the game's own joints (`SKEL_POSITION`,
`SKEL_BASE`, `SKEL_BACK`, hips, knees, ankles, shoulders, elbows, wrists,
`SKEL_NECK` - the sixteen of `MODELSKELETON(guard)`), with identity bind
rotations and bind positions in item space. The game's GROUP tree carries
the same bones as origins in each parent part's frame, but lays every limb
along an axis where the item hangs it down: the right hip-to-knee bone is
`(-430.3, -7.8, 0)` in the game and points straight down, several times
longer, in the item.
`sl_chr_derive` (`src/gfx/sl_gfx_charskin.c`) matches each item joint's child
bones to a game joint's child origins by direction under a quarter turn about
z, pairs them off, and takes the scale from the matched lengths, requiring
every bone on it within 3%. On `Cgreatguard2Z` both items match all 16 joints
onto its 15 GROUP nodes on one common scale. A vertex then reaches eye space
through `T(-bind) * scale(1/s) * Rz(turns) * M`, `M` the game's own matrix for
that joint this frame, blended by its (up to four) weights. A model the item
does not match is refused with the reason, once, and draws as the game's.

### The shading

The item is drawn under the character's own render state rather than one of
its own: texel times vertex colour, then the lerp toward the character's
shade register the game's lists set (`G_RM_FOG_PRIM_A` with `chrShade`), then
- where the game's own character takes it - the stage fog toward the
remaster row by the vertex's distance, through the same program and the same
coordinate `stage_fog_draw` gives a native character. No light, normal map or
term of the source's own character programs is reproduced.

The body item carries the body's built-in head - neck and face, two gated
blocks drawing the face atlas; it is marked `SL_WA_MESH_BUILTIN_HEAD` and
drawn only when the character wears no separate head. A head item may gate
its own neck and face the same way (the source's `head/headjoe` gates all of
its draws); there the gated block IS the head being worn, so preparation
marks only a body's gated draws, and the writer refuses the mark on a head
item - carried onto one, it would leave that head undrawn on every guard
wearing it.

The draw calls `SKY_MAYBE()` first, as the game's first world triangle does:
a character can be the first 3D geometry a list reaches, and the skydome,
which carries no depth, drawn after it painted over the part of the head that
stood against the sky.

### The format

Version 4 of SLWA, for classes CHAR (3) and HEAD (4) only - see SKINNED
ITEMS in `src/gfx/sl_gfx_worldasset.h`: the version-1 tables, an 8-byte skin
entry per vertex (four joints, four weights summing to 255) and a 48-byte
joint record (name, earlier parent, bind). `tools/worldasset/xblachar.py`
reads the source item: the pose block, the bone table (stream op `0x12`), the
per-draw palettes (op `0x13`), the two vertex strides (36 blended, 32 rigid),
the gated blocks and the images.

The texture coordinate's scale is the item's own, and it is measured. The
item's vertex programs fetch the pair as a raw 16-bit signed integer (no
exponent adjust) and scale it in code the reader does not decode; most items
reach the far edge of their atlases at 1/32768, and some at 1/16384 - on the
Dam, `char/oliveguard` and the heads `headdave`, `headgraham`, `headgrant`,
`headmark` and `headneil`. As the world reader does for its pools
(`xblaworld._uv_format`), the scale is taken from the values: an unwrapped
atlas reaches its far edge, so it is the power of two that brings the item's
largest coordinate into (0.9, 1], and an item that reaches no such edge is
refused. Read at the wrong scale an item samples one quarter of its atlas -
the olive guard's tunic came out as a dark shirt, and `headneil`'s face as a
black mask.

### A stage's character set

Which bodies and heads a stage can draw is in the game's own data, and
`tools/worldasset/stagechars.py` reads it there; nothing lists a stage's
characters by hand. A non-player character reaches a stage through one of
three doors, each naming a body and a head:

- a **guard record** (propdef type 9) in the stage's setup: body at +0x8,
  head at +0x16 ('object block types.txt', `09 guards`; `GuardRecord`),
  body `0xFFFF` the random body and a negative head a random head. Guards are
  expanded whatever the difficulty (`prop.c`);
- an **AI spawn** command whose first two arguments are the body and the head
  (`chr_try_spawning_at_pad`, `chr_try_spawning_next_to_unseen_chr`, in
  either spelling of the command set), in the stage's own AI lists or the
  global ones; the head byte is read signed, so `0xff` is a random head;
- a **clone** of a guard whose setup flags carry
  `GUARD_SETUP_FLAG_CHR_CLONE`: the same body with a random head.

**A random head is not a per-stage choice.** `bodyChooseHead` (chraction.c)
gives a male body `random_male_heads[(current + (rand & 3)) % count]` and a
female body `random_female_heads[current_female]`, with `current` drawn once
per stage load (`bodiesReset`, initguards.c); both pools are global (chr.c).
One run of a stage therefore shows a window of at most four male heads, the
seed places the window, and any window can come up: a stage that gives any
male body a random head can show all 25 of the pool. A body whose model
record says `hasHead` carries its own head and wears none. A random body is
any entry of `list_of_bodies`, each then taking its own head rule.

The stage resolves to its setup through the launcher's level table and the
game's `setup_text_pointers` (the country's own setup source first, as
`assets/obseg/Makefile.setup` builds it); the propdef list is walked record
by record to its end marker with the record lengths of 'Action Block
Types.txt', and a list that does not walk exactly is refused. Model files
come from `c_item_entries` in index order, enum values from
`src/bondconstants.h`, and the provider key is the runtime's own rule:
`C<item>Z`, lowered, under `char/` for a body and `head/` for a worn head.

    .venv\Scripts\python.exe tools\worldasset\stagechars.py inventory dam

prints the set with the reason each item is in it. **The Dam** is 36 guard
records - 14 `Cgreatguard2Z`, 21 `ColiveguardZ` (five of them clone-flagged)
and one `CcommguardZ` - plus two AI spawns of `Cgreatguard2Z`; all three
bodies are male and wear a separate head, and no guard fixes one, so the Dam
can draw 3 bodies and all 25 heads of `random_male_heads`: `headjim`,
`headchris`, `headlee`, `headgraham`, `headsteveh`, `headneil`, `headrobin`,
`headdes`, `headgrant`, `headdave`, `headkarl`, `headalan`, `headpete`,
`headmartin`, `headmark`, `headduncan`, `headshaun`, `headdwayne`, `headb`,
`headstevee`, `headjoel`, `headscott`, `headjoe` (`HEAD_Male_Joe_Altered`),
`headken` and `headjoe2` (`HEAD_Male_Joe`). The balaclava, biker and Mishkin
heads, the female pool and Bond's heads are not choices on the Dam.

### Preparing

    .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Characters

derives that set, looks every item up in the user's own source, extracts all
of the ones not already current in ONE pass (a source may be a single solid
block, where each separate extraction decompresses it from the start again),
converts each, and reports every part of the set on its own line:

- `PREPARED` or `CURRENT` - the bundle is written, or was already built from
  this very source by these very tools (the SHA-256 stamps every item here
  uses, so a rerun rebuilds nothing);
- `SOURCE ABSENT` - the source carries no remastered item of that name, and
  that part draws as the game's; a fallback, not an error;
- `CONVERSION REFUSED` - an item exists and would not convert, with the
  reason; the other items are still prepared and the command exits non-zero.

A part that ends absent or refused keeps no bundle from an earlier
preparation. Naming items instead of a stage is the diagnostic form:

    .\tools\windows\prepare-xbla-assets.ps1 -Characters char/greatguard2,head/headlee

On the user's source all 28 of the Dam's parts have a remastered item and
convert: `worldassets\xbla\char\` holds the three bodies (about 1.4 MB each)
and `worldassets\xbla\head\` the 25 heads (0.28 to 0.33 MB each). The Dam
has no source-absent part.

### Measured

At the gate-yard witness (eye `(18992.314, 73.152, 17486.443)`, theta 30,
verta -6, read 62, pinned seed): ORIGINAL and COMMUNITY HD byte-identical to
the build without the seam; XBLA draws the remastered greatcoat and head in
the game's pose; `SL_CHR_ASSETS=0` byte-identical to the build without the
seam. All 50,463 changed pixels lie inside the union of the game character's
own coverage and the item's silhouette. The per-tick trace is identical under
all three presentations and the control. A replaced character is cheaper to
draw than the game's (two array draws per mesh instead of the interpreter's
per-triangle path): 20.6-21.2 ms against 26.5-27.5 ms a frame at the witness,
27.2-27.9 against 34.3-34.5 at a second guard, with the dark seam equal to
the build without it.

At a second gate-yard guard (the same body wearing `CheadrobinZ`; eye
`(18949.371, 60.019, 17669.727)`, theta 98.869, verta -11.350, room 135,
read 62, pinned seed): with only the body prepared the game's head floated
above the remastered collar with no neck; the held-back rule draws that
guard byte-identical to `SL_CHR_ASSETS=0`, and with `head/headrobin` prepared
the remastered body and head draw with one continuous neck. At read 240
every changed pixel of both lies inside the game character's coverage or the
items' silhouettes; ORIGINAL, COMMUNITY HD and the first witness above are
byte-identical to the build before.

### Measured on the Dam's whole set

With all 28 parts prepared, eleven pinned seeds and seven teleport stops near
the Dam's guard clusters (the gate yard, the checkpoint, the Commandant's
room, the dam walkway, the tower yard) drew 158 characters, and `SL_CHR_PRES`
reported every one as body `REPLACED` and head `REPLACED`: all 25 heads and
all three bodies, in 50 distinct pairings, each seed loading its window of
four consecutive pool heads exactly as `bodyChooseHead` says. No lookup of a
character or head item came back "not provided", and no head was loaded that
the derived set does not name. Every item's joint table is the same sixteen
joints, and `sl_chr_derive` relates both classes to all three bodies on one
scale. Three runs of the ordinary launch - no seed, normal pacing - drew
three different windows of heads, again every character replaced.

At the recorded witnesses ORIGINAL, COMMUNITY HD, XBLA and `SL_CHR_ASSETS=0`
are byte-identical to the build before; a live presentation cycle loads each
item once and returns the game's own guard on ORIGINAL; the per-tick trace
is unchanged under every presentation. More characters replaced is cheaper,
not dearer: 25.5-25.8 ms a frame against 35.3-35.7 with only the earlier
three items prepared, at a checkpoint with four guards in view.

### Facility's set

    .\tools\windows\prepare-xbla-assets.ps1 -Level facility -Characters

**Facility** is 65 guard records and the AI spawns of its lists: four bodies -
`ColiveguardZ` (49 guards, 13 spawns), `CtechmanZ` (the scientists, 15 guards,
7 spawns), `CboilertrevZ` (006, one guard: chr 67 on pad 131) and `CorumovZ`
(two spawns) - and the same 25 random heads. 006 and Ourumov carry their own
heads (the item's report reads "its own head, no place for another"), so their
body items are drawn whole, built-in head included; the guards and the
scientists wear the stage's random heads. All 29 parts have a remastered item
on the user's source and convert. The 26 the Dam already prepared come back
byte-identical; the three new bodies are `boilertrev`, `orumov` and
`techman`. Until they were prepared, 006 and the scientists drew the
cartridge's bodies under XBLA (the game's body under a remastered head, for a
scientist; the game's whole model, for 006).

### Not here yet

- The Dam's and Facility's sets have been prepared and measured. The same
  command takes any stage name, and a stage whose parts are not prepared
  draws them as the game's - as does a prepared body worn under an
  unprepared head. A body prepared for one stage is drawn on every stage
  that uses it: by the same inventory, `techman` is also on Silo and the
  Caverns and `orumov` on Silo and the Train; `boilertrev` is Facility's
  alone.
- Hats are the game's own prop models (`Phat*Z`), not character items, and
  draw as the game's.
- The game's per-vertex blood and damage colouring on a character is not
  carried onto the item, and a fading character keeps the game's model (the
  player's body excepted - see "Bond (#70)" below).
- The source's own character lighting (a directional light and ambient in its
  programs) is not reproduced; lighting is #48.

## Bond (#70): the first-person view model and the player's body

Two more of the game's models draw from the remaster's items under PRESENTATION
XBLA: the weapon Bond holds in first person, with the hand holding it, and his
external body where the game shows it. ORIGINAL and COMMUNITY HD ask for
neither; `SL_BOND_ASSETS=0` keeps both the game's under every presentation
(the control), and `SL_CHR_ASSETS=0` still darkens the character seam - the
guards and the player's body, not the view model.

### The first-person view model

The game draws the first-person weapon from ONE model file per weapon,
`G<item>Z` (`gunfire.c` `gunRenderFirstPersonGunModels`, inside the viewmodel
no-op bracket, with the Z buffer off): the gun, the hand and trigger finger,
the silencer and the muzzle flash, all in that one file's lists. Its parts are
posed each frame by the game's view-model code, not by an animation - the gun
matrix, the trigger finger's turn, the slide's travel, the flash - and its
pieces are shown and hidden by the file's own switches (`Switches[]`: the
hand under `SW_HANDOFF1..7`, the flash under `SW_FLASH`). The PP7 carries no
cuff: the cuff switches (`SW_BOILER` ... `SW_SNOW`) are absent from its file.

The remaster's counterpart is the same unit: one `gun/<name>` item per weapon
carrying the gun and the hand, rigidly bound to the game's own view-model
skeleton (`SKEL_TOP` and the parts the game positions, `SKEL_FINGER`,
`SKEL_SLIDE`, ...) at the characters' scale (4.695) and in the game's own
orientation. Its draws are gated by the same switches by name - its
rendergraph carries a condition table (`|TOP|wppk|gun|...|SW_HANDOFF4`) - and
its translucent pass is the muzzle flash on the game's own four flash images.

So the seam (`src/native/sl_vm_asset.c`, around the view model's `subdraw`)
asks the provider for `gun/<item>` by the model file's own name
(`GwppksilZ` -> `gun/wppksil`) and, when the item rides the model, withholds
every one of the view model's lists except those under `SW_FLASH` and emits
one bridge command (`0x02 'S' 'V' 'W'`) in their place. The renderer draws
the item skinned by the view model's own matrices of that frame, under the
view model's shade register (the player's tile colour, the fog its render
mode loads), with each draw shown only while the game has its switches
visible. The game draws its view model without depth; the item is a solid,
so it is drawn with the depth test on inside a thin slice at the front of the
range, over the world as the game's is.

The relation is `sl_vm_derive`, not the character mapper: each item joint
names its game part (resolved at preparation to the model file's
`Switches[]` index by the game's own switch vocabulary,
`assets/obseg/gun/gunModelFileHeaders.inc.c`), the scale is measured from the
parts' distances to the root - from the root alone for an item whose only
positioned joint is its root (the fist, the knives) - and every joint must
sit where the game's part sits. A joint the game positions no part for (a
watch gadget's `BEAN_END`) moves with its nearest positioned ancestor: its
vertices are rebound there at preparation. A weapon with no item, or one
refused, draws whole as the game's - there is no partial unit.

THE PAIR. Dual wield needs nothing of its own: each hand loads the same
`G<item>Z` into its own buffer, the seam asks for the same item for either
hand, and the renderer draws it by that hand's own matrices. For a weapon
whose stats carry `WEAPONSTATBITFLAG_MIRROR_DUAL` (the pistols, the knives,
the launcher) the game has already negated the left root's x; every other
pair draws the right-handed weapon at the left position, as the game does.
The item is drawn with culling off, so the mirrored winding needs no flip.

THE MATERIALS a weapon carries beyond one image per draw:

- a TRANSLUCENT pass - a clear magazine, a watch glass, a sight lens - is
  carried as a blended material and drawn after the item's solid draws,
  depth-tested and writing no depth; a translucent draw standing under
  `SW_FLASH` is the muzzle flash and stays the game's;
- a REFLECTION image: the gold and silver pistols, the golden gun, the laser
  and the knives pair their atlas with a 256x256 environment image. The
  material names both; the base is the larger. The reflection is carried in
  the material (class `GUN`; and class `PROP` for the props
  `prepare.PROP_REFLECTIONS` names - Egyptian's Golden Gun pickup, A PROP'S
  REFLECTION in Egyptian's section) and drawn as a second pass, read at the
  sphere coordinate of each vertex's eye-space direction (taken from the
  item's own triangles, turned by the pose) and multiplied in at twice its
  value - the gold and silver pistols share one atlas and differ only there;
- a PLAIN draw (a stride-28 buffer: no texture coordinate - a weapon's hands
  and trims are made this way) is carried as its vertex colour on a small
  white image.

THE COSTUME. The watch gadgets' models carry one cuff per costume under
`Switches[29..34]`, which the game switches by the stage's costume
(`bondviewSelectCuff`). An item that stands for such a model must gate its
draws on those switches, or it would draw every costume at once: the seam
refuses one that gates none (the watch laser's item carries five sleeves,
all ungated).

A STATIC weapon item - the Skorpion's has no skeleton - is one rigid piece
placed as a prop is (see "The stage's rigid props" below): the bundle carries
the box of the source's original generation of the item (which keeps a
skeleton, and is the game's model), the seam measures the box of the view
model's own vertices (the muzzle flash left out), and the item rides the
model's root matrix at the scale and offset the two boxes give.

### The player's external body

In single-player the game shows Bond's body in the intro swirl, the death
camera and the mission's end (`bondview2.c` `solo_char_load`), built from
PRIVATE COPIES of his body and head files in the view model's buffer, and this
build records neither number on the player's character. The game's own
choice is handed to the character seam where it is made
(`sl_chr_player_model`, read-only); the player's body then draws through the
same seam, the same mapper (`sl_chr_derive` matches it exactly as a guard) and
the same bridge as any character.

Bond's remastered bodies (`char/boilerbond` on the Dam) carry no condition
table: no gated built-in head and no head attach point. The remaster draws
the head such a body carries, always, and gives it no place to wear another.
Preparation marks such an item (`SL_WA_MESH_OWN_HEAD` on every mesh), and the
seam lets it stand for the game's body AND the head the body wears: the game's
head lists are withheld with the body's, no head item is consulted, and the
neck is the item's own. Every Dam guard body gates its built-in head on
`CP_HEADCHILD` and composes exactly as before.

The swirl ends in the game's own fade. A fading character still draws as the
game's - except the player's body, which the renderer fades by the game's
alpha: a depth-only pass finds the item's nearest surface, and only that
surface is blended.

**The costume's body is the stage's own item.** The intro list's cuff names
the costume, and each costume is a different body file: `CUFF_BOILER` the
boiler suit (`char/boilerbond`: Dam, Facility, Runway, Bunker 1 and 2, Silo),
`CUFF_SNOW` the parka (`char/snowbond`: Surface 1 and 2), `CUFF_BLUE` the
suit (`char/suitbond`: Frigate, Statue, Archives, Streets, Depot, Train). A
body is prepared only by `-Player`; `-ViewModels`, the stage recipe from
Bunker 1 on, prepares the first-person set alone, so until round trn2 only
the Dam's `char/boilerbond` was ever prepared and the parka and suit stages
drew the game's Bond in their intros and endings under XBLA. Both prepare
(`snowbond` 18 draws, `suitbond` 15, each carrying its own head as
`boilerbond` does); the source has no `head/headbrosnansuit` or
`headbrosnanboiler`, and none is needed. With them, the remaster's suit and
parka stand in the intros of Frigate, Statue, Archives, Streets, Depot,
Surface 1 and 2 and Train and in Train's ending (`CHR_BOND_CINEMA`); in
Train's intro and ending, followed frame by frame, the weapon stays in the
hand through the animation. The stages' read-600 start frames change where the swirl is still running
there (Surface 1 and 2, Statue, Depot) and are byte-identical on the other
nine. A stage is prepared with `-Player` in place of `-ViewModels`:

    .\tools\windows\prepare-xbla-assets.ps1 -Level train -Player

### Preparing

    .\tools\windows\prepare-xbla-assets.ps1 -Level dam -ViewModels
    .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Player
    .\tools\windows\prepare-xbla-assets.ps1 -Player gun/wppksil,char/boilerbond

`tools/worldasset/viewmodels.py` derives the set from the game's tables
(`first_person_set`): the stage's intro list gives the ordinary-play loadout
(every life also begins with the fist); the game's ALL GUNS rule
(`bondinv.c` `bondinvItemAvailable`: every item above `ITEM_UNARMED` and below
`ITEM_BOMBCASE`) gives every weapon the cheat puts in reach; the compiled
weapon stats say which of them the game draws in first person at all
(`SHOW_FIRST_PERSON` without `HIDE_FIRST_PERSON_HAND` - a grenade or a mine is
thrown from an empty view) and which pair (`CAN_DUAL_WIELD`,
`MIRROR_DUAL`); the gun record table gives each model file, the watch hand's
included. `-Player` adds `solo_char_load`'s costume choice, the body and head.
The remaster's own executable, read from the same source, carries the game's
gun record table with each file-name field pointing at its own item name
(`GwppksilZ` -> `gun/ppksilenced`); it is located by the decomp's own record
values and must be found once. Bundles are named by the GAME'S model; the
extraction is one pass for every item that needs building. Every outcome is
recorded under the source and tool stamp - a bundle and its `.stamp`, or a
`.refused` / `.absent` record with its reason - so an unchanged rerun extracts
and converts nothing.

The reader's contracts the weapons needed (`xblachar.py`): a draw reads the
vertex buffer its vertex-program binding (op `0x2e`) names, whose descriptor
may sit at any byte offset; texture coordinates are signed, and their scale
is the one most non-tiling draws reach their edge at, over the world reader's
range (1/32768 .. 1/64); a joint's bind rotation places nothing.

Measured on the Dam (`-Level dam -ViewModels`, 8 s, a rerun 2 s): 27 weapon
models drawn in first person and the watch hand; 26 PREPARED; the taser (a
material record this reader does not frame, and gated draws it does not read)
CONVERSION REFUSED; the watch hand's source is a
character-class item with its own relation, not prepared here. The watch
laser prepares but is refused at runtime (THE COSTUME). The grenade, the three
mines and the Dam's covert modem (`ITEM_BUG`) are not drawn in first person.

### Measured

At the gate-yard witness (read 210), the XBLA frame's changed pixels lie
wholly inside the game's view-model silhouette united with the item's; no
list of the game's view model survives the seam; ORIGINAL, COMMUNITY HD and
`SL_BOND_ASSETS=0` are byte-identical to before. Aiming, firing (recoil, the
slide's travel, the ejected case, the ammunition count), reloading and
switching away and back follow the game frame for frame. In the Dam's intro
swirl the whole external body - fade included - is the remaster's, with the
game's pistol in its hand; the changed pixels lie inside the two silhouettes.
The per-tick trace is unchanged.

Every weapon of the Dam's set, one at a time (`SL_WEAPON`) and, for the 23
that pair, in both hands (`SL_WEAPON` under ALL GUNS, one hand-written
next-weapon step on the developer pad): 26 draw the remaster item at one
scale (4.695; the Skorpion's box gives 4.702), no list of the game's view
model surviving; all 23 pairs draw it in both hands. The alternating fire, the
flash and the ejected cases of a pair follow the game frame for frame.

### The watch hand

The watch is worn on a model of its own: the left arm `Csuit_lf_handZ`,
drawn by `bondview2.c bondviewRenderWatch` while the watch is up - not by the
view-model path. It carries the arm, the hand, the watch, the watch's three
hands (`Switches[0..2]`, turned by the level clock onto matrices 1..3), the
dial face (`Switches[3]`, off while a watch page is shown) and one cuff per
costume under `Switches[4..9]` - boiler suit, Brosnan tuxedo, Connery
tuxedo, blue suit, jungle fatigues, snow suit ('GE Object Offsets.txt',
ARM) - of which `bondviewSelectCuff(model, header, 4)` shows the one the
player's costume names. MEASURED from the ROM's model: all six cuffs carry
geometry; boiler and snow share one mesh, the three suits another, the
jungle cuff is its own (a rolled sleeve beginning some 600 model units up the
forearm).

The source's `char/suitlfhand` is the arm, hand and watch as one unit, plus
FIVE alternative sleeves - a black tuxedo sleeve with its buttons and shirt
cuff, an olive rolled sleeve, a grey parka, a charcoal ribbed-knit sleeve and
a dark navy sleeve with a shirt cuff - none of them gated. The remaster
itself draws all five at once (measured in the remaster, 2026-09-28: every
sleeve pair issued, opaque, depth-written, fighting by depth). There is no
selector in the item to carry.

**Which sleeve a costume wears is DERIVED** (`tools/worldasset/watchhand.py`,
at preparation, chosen as a deliberate fix of the remaster's
composite - docs/divergences.md D-024):

1. the game's model is read from the user's ROM: each cuff switch's
   triangles with the game texture each run is drawn with;
2. the item's space is related to the model's by the joints both name (the
   decomp's matrix-order names, `suit_lf_hand_JointNames`): scale 0.4695 and
   one quarter turn about z;
3. each image of the source's ORIGINAL generation of the same item (the
   game's model re-packed) is identified with the game texture whose
   triangles it lies on - same place, same image width. A cuff whose own
   textures are each exactly one such image is determined; the Brosnan
   tuxedo's and the blue suit's sleeve images (both 8 wide, 18 triangles, on
   the one shared mesh) cannot be told apart this way and stay undetermined;
4. the remastered generation's alternative sleeves are the image groups
   lying beyond the cuffs' wrist end;
5. each is described by where it begins along the forearm (full, or rolled
   up the arm), the tone of its wrist end (a white shirt cuff, or a dark
   band) and the colour of its body; a cuff and a sleeve pair only when both
   class features agree and each is the other's only candidate or clearly
   nearest by colour.

On the Dam's source this gives:

| cuff | game cuff (original generation) | sleeve | why |
|---|---|---|---|
| `Switches[4]` boiler | full, dark knit band, (23,26,35) | charcoal ribbed | the only full sleeve with a dark wrist end |
| `Switches[8]` jungle | rolled (0.42 up), dark, (72,73,22) | olive rolled | the only rolled sleeve |
| `Switches[9]` snow | full, light, (167,177,186) | grey parka | light and full, nearest by colour 53 vs 372 |
| `Switches[5]` Brosnan tux | undetermined | - | two identical original images |
| `Switches[7]` blue suit | undetermined | - | as the tuxedo |
| `Switches[6]` Connery | full, light, (189,193,195) | - | its nearest sleeve is the parka's, which is nearer snow |

The tuxedo and navy-with-shirt sleeves serve no cuff. The remaster's own
costume bodies agree with the boiler pairing independently: the forearm of
`char/boilerbond` samples (36,37,39), the ribbed sleeve (34,36,38), the navy
sleeve (41,45,52) - distances 4 and 40 (measured with a scratch tool; not
part of the derivation).

The bundle is `gun/suit_lf_hand` (class GUN, the model's own name): its
joints that carry vertices name the model's MATRICES (`SL_WA_PART_MATRIX`:
the wrist on matrix 0, the watch's hands on 1..3), and every sleeve draw is
marked `SL_WA_MESH_ALT`, gated on the cuff it serves when it serves one. The
seam (`sl_vm_watch_begin`, around the watch's `subdraw`) reuses the view
model's machinery - the bridge, the depth slice, the shading - with three
additions: the relation is read from the matrix-named joints (one scale, one
quarter turn, `sl_vm_derive`); the sleeves are drawn by
`sl_vm_draw_visible` (the costume's one; all five under
`SL_WATCH_SLEEVES=all`); and the arm's depth is released after it is drawn,
because the game draws the watch pages after the arm and the controls page's
pad is a model drawn with the Z buffer. A costume whose cuff no sleeve
serves draws the game's WHOLE watch hand - never a remaster arm with no
sleeve - and `SL_WATCH_SLEEVES=native` keeps the game's watch hand under
every presentation. The watch body, dial and hands are the item's (the
remaster's watch); the watch pages are the game's, drawn over them as
before.

Prepared by `-Level dam -ViewModels` (or `-Player`); the ROM is read from
`baserom.u.z64` at the repository root or `SL_ROM`, and without it the watch
hand is refused and the game's is drawn.

### The weapon on the watch

The watch's pages show a weapon: the mission page the one in hand, the
equipment page the highlighted entry of the list. The game draws it from the
same first-person model file the hand holds. `set_enviro_fog_for_items_in_solo_watch_menu`
(gunfire.c), called from `draw_current_hand_item_and_ammo` and the equipment
page (options.c), loads the item's `G<item>Z` into the right hand's buffer
(`gitem_structs[item].item_header`, the table "watch equipment.txt"
documents). It then draws it under the watch's own matrix and perspective,
with the Z buffer off. So the item that stands for the held weapon stands for
it here too, through the same map, the whole weapon as the unit
(`sl_vm_menu_begin`, keyed by the item the watch passes). Its hand stays off
because the game turns the hand's switches off, which gate the item's hand
draws as they gate the game's.

The look is the game's render data for that draw (`sl_vm_menu_look`):

- **The mission page** uses the first-person render mode, with the fog
  register set to the watch's green (`envcolour` 0x64DC6428). The bridge
  shades it as it shades the weapon in hand.
- **The equipment page** uses the translucent mode (PropType 5): fog
  `fogcolour` 0xA0FFA03C and the surface's alpha `envcolour`'s low byte,
  0x40. A page fading in uses the same mode. The item is one solid, so its
  nearest surface is found first and only that surface is blended by the
  alpha, as a fading body is. The cartridge, drawing with the Z buffer off,
  blends every face in list order; that difference is accepted. A faded
  draw skips the reflection pass.

The item's depth is released after the draw (as the watch hand's is), so a
page drawn later is not cut by it. Measured at the gate-yard witness with
`allguns` (coverage only), for the sniper rifle, PP7 (silenced), KF7 and D5K:

- Both pages show the remaster's weapon, the one the hand holds in first
  person, at the place and size of the game's.
- The held frame is identical, and every changed pixel is inside the watch's
  screen.
- ORIGINAL and COMMUNITY HD are byte-identical.
- `SL_BOND_ASSETS=0` draws the game's weapon on the watch as in the hand.

### Not here yet

- The taser and the watch laser keep the game's model (above).
- The watch hand's Brosnan-tuxedo, blue-suit and Connery cuffs keep the
  game's watch hand: the derivation cannot tell the tuxedo and blue-suit
  sleeves of the original generation apart, and no remastered sleeve is
  Connery's white jacket (above).
- Weapons of other stages are prepared by the same command and have not been
  measured.
- The weapon Bond holds in third person is a prop (`Pchr*Z`), the #68 prop
  path's business.
- The source's own lighting of the view model is not reproduced (#48); the
  reflection is the environment image alone.

## The game's state on the replacement world (#75)

The replacement background is one static item for the whole stage; the game
still owns every piece of state the original draws from. Three of them reach
the replacement's pixels, each read where the game keeps it and applied by
the renderer without a rule of its own.

### Replacement geometry at the eye (walls and props)

The original renderer's near contract (B-100, `emit_tri`): a triangle with
vertices on both sides of the eye plane keeps all of its in-front coverage,
the part nearer than znear drawn at znear; a triangle wholly in front is cut
at the near plane like the cartridge's, because both are the cartridge's own
triangles. GL applied to a replacement item cut both, so a wall or a crate the
camera stands against opened and whatever lay behind it - the sky, the next
crate - showed through.

A replacement item's triangulation is its own, so the cartridge's
per-triangle cut has no counterpart there. The Dam's tower wall triangle that
leaked at one pose (a vertex behind the eye) leaked again four units away with
all three vertices in front of the eye and two of them inside znear (clip w
1.41 / 16.28 / 3.06); a selector that took only the eye-crossing population
closed the first and missed the second. So the replacement keeps both: the
contract is that whatever part of a replacement triangle lies in front of the
eye but nearer than znear is drawn at znear.

One primitive serves every replacement seam that draws opaque geometry by
index lists - the stage background and the rigid props (`wa_near_fill`).
Per vertex, this frame's clip position gives a near class
(`sl_wa_near_class`: at or behind the eye, or in front but inside znear);
`sl_wa_near_cut_select` takes the triangles the near plane cuts while some part
stays in front. After a depth-writing, non-decal mesh draws as before, those
triangles draw once more under `GL_DEPTH_CLAMP`, tested LESS behind a small
polygon offset: the second draw loses to the first wherever the first drew and
adds only the coverage the near cut removed, at the near depth. Translucent
and decal draws write no depth and are left as they were; characters, heads
and a fading draw are not rigid props and are not touched.

Controls: `SL_WA_NEARKEEP=0` draws every seam the previous way,
`SL_WA_NEARKEEP=eye` keeps only the eye-crossing population (the first form of
this repair), `SL_BG_NEARKEEP=0` turns the background's fill off alone as
before. A driver that refuses `GL_DEPTH_CLAMP` gets the previous way and says
so once. The census line (`SL_DL_CENSUS=1`) counts the triangles filled per
seam.

Measured on the Dam: at five near-wall poses every leaking pixel's first
surface was a mesh-1 wall triangle cut by the near plane; at the metal crates
(`prop/boxes2x4`) the prop seam had no near contract at all and its
eye-crossing triangles opened onto the next crate; a wooden crate
(`prop/wood_sm_crate4`) with the camera at its face showed its inside the same
way. All close. At a tunnel pose where about 630 background triangles a frame
take the fill and nothing leaked, the frame is byte-identical; frame time at
the crate camera is within run-to-run spread.

### Windows

A tinted-glass pane (`PROPDEF_TINTED_GLASS`) is the game's own prop under
every presentation; its opacity comes from `glassCalculateOpacity`
(TintDist / CullDist) and the portal behind it closes at full opacity. The
replacement item has no surface at the pane, so there is nothing to fade: the
pane over the replacement's interior is the composition.

What differed was the remaster stage fog on objects (#68). Its coordinate is
the vertex's distance from the eye; under the VIEW-FOLDED world projection a
vertex position is the world minus the frame's render origin, times the
visibility scale, and the distance was taken from that space's origin - the
player's room origin - instead of from the eye. A pane drawn inside a room
was therefore fogged by where the room starts, and its look jumped at every
room crossing. `stage_fog_coord` now subtracts the eye put into that space
(`sl_wa_fold_eye`: (camera - render origin) x visibility); the plain
projection is unchanged. At the Dam's tower window the pane now brightens
monotonically as the camera approaches (window-centre luminance 23.0 at 750
units to 35.3 at 120; ORIGINAL 18.8 to 35.2) instead of holding a flat 38-39
until the room crossing at 450.

### Shot-out lights

A bullet that hits a light-fixture triangle (`lightFixtureBreak`) records the
fixture's vertices in `darkened_light_table` and shifts their colours right
by two. `sl_wa_light_darkened` (src/native, read-only) returns each recorded
vertex's position in the replacement item's own coordinates - the room
data's, through the item's scale (`sl_wa_source_scale`, exactly 1.0 on the
Dam; see "Facility's background") - and `sl_wa_light_reach` the game's
fixture reach (`lightIsCoordNearDarkenedVertex`: a sum of axis differences
below `room_data_float1 * 100`) in the same units. A replacement triangle whose three vertices all lie
within that reach of a shot-out vertex is the fixture's
(`sl_wa_light_mark`); its vertices draw with the game's own colour >> 2. The
dimmed colours are rebuilt only when the recorded set changes, and a
position is remembered per table slot, so a fixture stays out while its room
is streamed out, as the game's table does. `SL_BG_LIGHTS=0` draws every light
as authored.

Measured in the Dam tunnel: one shot records 54 vertices (20 positions) at
one fixture; 30 replacement triangles dim - that fixture's 14 panes and its
16-triangle housing - and the other 22 fixtures of the same mesh keep their
light.

## Facility's background (#77)

Facility's whole static background drawn from the remaster's own item
(`background/facility`), through the Dam's replacement path unchanged:
the room background emission is withheld only on a frame whose replacement
was loaded, validated, uploaded and drawn, and rooms, portals, collision,
guards, props, doors and characters stay the game's. It is what PRESENTATION
XBLA draws on Facility (`sl_wa_stage_item_path`). `SL_WA_PREVIEW=0` keeps the
earlier look instead: Facility then looks up no background, opens no file
and draws the texture set's artwork on the game's rooms ("Pairing by
geometry" and "Carried coordinates" in docs/texture-packs.md). A root with
no `background/facility` prepared draws that look as well.

```
.\tools\windows\prepare-xbla-assets.ps1 -Level facility -Background
```

prepares it beside the Dam's (`-DamBackground` is `-Level dam -Background`;
the Dam's bundle and the stage-fog table regenerate byte-identical). Read by
the same reader: 930 records, 120 draws, 118 kept (a pool of 11 vertices
carries no texture coordinate and does not validate) - 13923 triangles read,
13933 carried with the two two-layer draws' layers, 72 materials of 69
images, 24 of them the decal pass and 27 translucent; every pixel program
declares the stage fog; no panorama shell, no animated or four-image
material.

### The item's own scale

The Dam's item reads at the room coordinates, and the first replacement
took that as the format's rule. It is not: the source authors EVERY stage
background at the room coordinates times 100 / the stage's last
`levelinfotable` column (`unknownfloat`, `bg.c`; in the decomp only
`sub_GAME_7F0B4848` reads it, and nothing calls that - `grep -rn
"unknownfloat\|sub_GAME_7F0B4848" src/`). The Dam's column is 100. Measured by
least squares over the vertices the two backgrounds share:

| stage | fitted | 100 / column |
|---|---|---|
| Facility | 1.560002 | 1.560000 |
| Dam | 1.000001 | 1.0 |
| Bunker | 4.319999 | 4.32 |
| Silo | 3.440084 | 3.440091 |
| Archives | 1.840005 | 1.84 |
| Frigate | 2.719945 | 2.72 |
| Cradle | 2.319957 | 2.32 |

`sl_wa_source_scale` (src/native, read-only) returns the column / 100 and
`bg_draw` multiplies the level scale by it, once; the shot-out lights put
the game's positions and reach into the item's units by the same factor. On
the Dam it is exactly 1.0 and every float of the pass is the one it was.

### The backdrop under it

A stage whose environment has no cloud layer draws no sky: the game's sky
pass fills the whole view with the environment colour (`skyRender`,
`sky.c`'s `!Clouds` branch) and the rooms draw over it. That fill is the
cartridge's backdrop. The remaster clears to black before its background
(measured on the console at the Dam). So on a frame whose replacement draws
and whose stage has no sky (`sl_wa_backdrop_black`, reading
`sl_wa_stage_flat_backdrop`), the aperture is
cleared to black before the first mesh. Facility's conveyor exit is open in
the remaster's item and backed by a black face on the cartridge; it showed
the green fill (16, 33, 16) over 5025 pixels at one replay mark and is black
now. A stage with a sky, and a frame whose dome drew, are untouched - the
Dam is. `SL_BG_BACKDROP=0` keeps the game's fill.

### Measured

Teleport only, seed pinned, scratch builds, at ten replay marks (the tank
rooms, the floor beside the lab windows and in the corridor, the stairs,
the lab, the conveyor):

- **Registration.** The remaster's walls, tanks, floor and conveyor coincide
  with the game's at every mark (`SL_BG_TRACE`: the combined scale 0.5313 =
  0.82886 level scale x 0.641026 item scale, visibility 1.0).
- **What changes.** The tanks draw the remaster's textured cylinders, the
  conveyor's rails, belt and legs the remaster's materials, the floor its
  own tiling with straight grout, the stair treads aligned tiles.
- **Whole, not per room.** The item is drawn whole every frame with the
  depth buffer, as the Dam's is. A per-room split was not needed: every
  opaque remastered surface lies within 20 world units of one of the game's
  room surfaces (point-to-triangle over the whole level), so the depth
  buffer resolves exactly what the portals would. The whole draw is cheaper
  than the room path it withholds - 8.7 ms a frame against 12.8 at the
  lab-window mark (`SL_PHASE`, 449 frames, the same binary with the switch
  off reads 12.7).
- **What stays the game's, on top of it.** A swing door opened by the pad
  lands on the same pixel columns with and without the replacement, and the
  corridor behind it is the remaster's. Pressed against a wall the view
  stays closed (the near-cut fill). Bullet impacts mark the replacement's
  wall where they mark the game's. Characters, props and the lab's
  glassware draw over it as before, with the remaster's stage-fog row (34:
  -100 .. 5000 toward (16, 32, 1)).
- **Collision.** Nothing about collision changes. Of the game's room
  surfaces, 0.4% of the area has no remastered surface within 25 world
  units (room 14: 39% of it; rooms 66 - 68: 1.1 - 1.4%, up to 53 units); no
  opaque remastered surface lies further than 20 units from one of the
  game's. Elsewhere what the player collides with and what is drawn agree to
  within those distances.

### The door frames' hazard stripes

The security door frame into the last tank room drew plain concrete under
XBLA where the cartridge paints yellow and black hazard stripes. The frame's
front face is a pool of its own in the item: 20 vertices on a 32-byte
stride (position +0, normal +12, two coordinates at +16 and +20, two
colours at +24 and +28, read from the fetches of the vertex program bound
to it), drawn by two draws that blend the stripe image over the concrete
(the two-layer draw, "The wall behind the ladder"). The reader measures a
pool's coordinate scale from where its values pile up, and 20 vertices
pile up on no divisor, so the pool was refused and the frame's front face
was missing - the concrete behind it showed.

A pool too small to show its own scale now takes the scale every pool of
the item that did measure one agrees on (`xblaworld.item_scale`; 1/256 on
Facility), and still passes the edge test that picks its coordinate word and
the validation every pool passes. The pool is read and both draws are
carried; a pool that measures its own scale reads exactly as before - the
Dam's bundle, the stage-fog table and every prop regenerate byte-identical.
Forcing a stride-32 layout instead was tried and refused: the Dam's two
other stride-32 pools measure their coordinate at +20, and the forced word
moved them. The 11-vertex pool still refused carries no coordinate at all
(position, normal and colour only).

Measured, teleport only, seed pinned, at the replay mark: the frame's front
faces draw the remaster's stripes where the cartridge has its own.

### Two of the item's own surfaces in one plane

The remaster tests its background's depth LESS (the console's
RB_DEPTHCONTROL on 317 of the Dam's 323 opaque draws) and keeps its own
list order, so where two of the item's surfaces lie exactly in one plane
the one drawn first keeps the pixels. This renderer tests LEQUAL (B-122),
under which the later one wins: the hazard stripe beside the lab windows,
drawn before the floor in the floor's plane and not in the decal pass, was
covered by the floor.

Testing the whole pass LESS moves pixels everywhere two surfaces merely meet
(14 of the Dam's 27 witness frames, 4 to 28 pixels each). So only the
triangles that decide something change: `sl_wa_coplanar_yield` finds, once
per item, every triangle of a depth-writing, non-decal mesh lying in the
plane of a triangle of an EARLIER such mesh (normals within 0.9999, offsets
within 0.01 item units) and overlapping it with positive area, and the pass
draws those triangles LESS, in the mesh's own order. Facility has 44 such
triangles in 6 meshes (the stripe under the floor, the stripes against the
door frames, three wall overlays); the Dam 34 in 6. `SL_BG_COPLANAR=0` draws
them LEQUAL as before.

Measured: the stripe shows on the floor at the lab-window mark (the frame
equals the one a whole-pass LESS gives there), the door-frame mark is
unchanged, and the Dam's 27 witness frames are byte-identical.

### Two sides of one wall

Some of those pairs are not two surfaces on one side. They face opposite
ways: a wall of one room authored back to back with a wall of the next, the
planes a few ten-thousandths of a unit apart. Facility's bathroom wall
(mesh 0, z -849.9999, facing into the bathroom) has the stairwell's wall
behind it (mesh 2, z -850.0000, facing the stairwell); the doorframe by the
first stairs is the same (meshes 3 and 4). The item is drawn with culling off,
as the remaster draws it (`PA_SU_SC_MODE_CNTL` 0x4 on every background draw
in the console frames of the Dam), so from either room both walls draw in one
plane, and the order rule above handed the pixels to whichever mesh is listed
first:

- in the bathroom, a strip of the stairwell's wall striped through the tiles
  at the corner (the depth test tied between the two, pixel by pixel);
- in the stairwell, the bathroom's tiles showed through the stairwell's wall,
  because the bathroom's wall is listed first;
- at the doorframe, a striped edge down the frame.

Of two such surfaces the one that faces the eye is the one seen.
`sl_wa_coplanar_opposed` marks, once per item, every triangle in a pair whose
authored normals (cross(B - A, C - A)) point opposite ways, with the same walk
and thresholds as the order rule: 48 triangles on Facility (mesh pairs 3/4,
24/28, 0/2, 2/5), 10 on the Dam (1/8, 1/4). Each frame the pass puts the eye
into the item's coordinates and draws a marked triangle whose normal faces
away from it (`sl_wa_tri_faces`) pushed behind the depth it would have (the
polygon offset +1, +2, the decal pass's opposite); everything else draws as
before. Where its partner covers it, the partner shows; where nothing does,
it still draws. `SL_BG_TWOSIDED=0` draws both as the order rule alone does.

Measured, teleport only, seed pinned, at three replay marks: the bathroom
corner, the stairwell under it and the doorframe show one wall each, with no
stripes; `SL_BG_TWOSIDED=0` is byte-identical to the build before at all
three. The Dam's 27 witness frames are byte-identical (its opposed
triangles are not in view at any of them).

**Two sides of one curved shell, and the eye on the plain branch (#80).**
Surface's dome building has its dome twice: an inside (mesh 12, 48
triangles, the dark shell) and an outside (mesh 16, 60 triangles, the white
one) on the same 44 vertices with different triangulations, every one of
the inside's positions a position of the outside's (`dome.py`), and from
the replay's mark the cap striped black and white. Three things were
missing, measured one at a time:

- The tolerance. Of the 96 overlapping same-normal pairs the two shells
  form, only 15 lay within the flat 0.01 item units: the dome's coordinates
  reach 41166, a float32 there is held to 2^-8, a normal built over a
  ~200-unit edge tilts by ~1e-5, and the plane offset wanders by that times
  the distance - 0.0005 .. 0.4021 across the pairs (`copgap.py`). Ray-cast
  from the mark's bearing 95% of the rays through both shells tie within
  half a unit and none has the inside in front by more than one: depth-tie
  noise between two triangulations of one surface, the two-sided case the
  flat tolerance missed. Both rules now take `sl_wa_coplanar_eps`: the flat
  tolerance plus `SL_WA_COPLANAR_REL` (2e-5) times the pair's largest
  coordinate magnitude - 0.01 near the origin, 0.83 at the dome; the scale
  is the coordinates', not the plane offset's, because an axis-aligned plane
  far from the origin has a small offset and the same rounding. On Surface
  the yields go from 24 triangles of 4 meshes to 69 of 8, the opposed from
  26 to 100; self-tested at 41000 units and at the origin.
- The push. One polygon-offset step resolves an exact tie, and two walls in
  one plane tie exactly; two triangulations of a curved shell cross within
  that rounding, and the inside stayed in front across whole triangles. A
  vertex of a two-sided triangle is now also pushed in the item's own space
  (`g_cop_vn`): along the area-weighted normal of the two-sided triangles
  that meet at it, away from the eye, by twice the pair tolerance at its
  distance from the origin - past every crossing the tolerance admits, and
  behind the side that covers it. The normals are found once per item, the
  pushed positions once per frame through the same transform and the same
  two depth ranges, and a run facing away draws from them with the same
  indices (`SL_BG_TWOSIDED=0` draws both as before).
- The eye. The rule's eye came from `sl_wa_view_origin` on the matrix in
  force, which has no answer under the PLAIN world projection - the branch
  Runway and Surface draw under - so on those stages no side ever faced
  away and the rule was inert; the tolerance alone only made the outside
  yield to the inside. Where the recovered eye is not in hand the game's own
  camera position (`sl_wa_view_matrix`'s translation, the point the fog's
  eye is taken from) over the item's scale is the eye now; a folded stage
  keeps the recovered one and draws exactly as before.

**Two sides of one plate inside one mesh (#80).** The playtest mark at
Surface's sniper tower: the platform flickered between its grating top and
its dark underside, half the plate one way and half the other, changing
from frame to frame and at small camera offsets. Ray-cast from the mark,
the top (triangles 227, 264, 265, facing up) and the underside (266, 267,
facing down) of the plate are both in mesh 5, triangulated differently,
their plane offsets 0.03 item units apart - two sides of one surface. They
were never paired: the walk skipped every pair inside one mesh, because the
ORDER rule has nothing to say there (a mesh is one draw in the item's own
order). The two-sided rule is not an order rule, so a pair inside one mesh
whose normals point opposite ways is now marked opposed - and yields
nothing (`sl_wa_coplanar_opposed`). Opposed triangles: Surface 100 -> 135,
Facility 48 -> 82, the Dam 10 -> 16, Runway 12 unchanged. At the mark, 21
consecutive frames and the same with the eye 40 units along each axis show
the top alone; the Dam walkway and Facility witnesses are byte-identical.
`SL_BG_TWOSIDED=0` draws both as before.

### Glass

A translucent prop's colour is the image that carries its coverage
(`xblachar.coverage_base`, "Glass" under the rigid props below). The lab
doors' glazing draws a blue tint with the corridor behind it instead of
the black sphere image.

### The toxic gas

A destroyed bottling tank releases gas (`init_trigger_toxic_gas_effect`,
propobj.c); while it spreads the game blends its environment from the main
row toward the next (`fogSwitchToSolosky2`, bgfog.c: row 34's far fog 5000
toward row 134's 1000, colour (16, 32, 16) toward (64, 128, 64)), and its
rooms fill green. The remaster's table carries both rows' extensions: row
134 is -100 .. 1000 toward (64, 128, 64), the cartridge's own range and
colour; row 34 is -100 .. 5000 toward (16, 32, 1). The replacement and the
objects in front of it are fogged by the same blend of those two rows
(`sl_wa_env_blend`), its fraction read where the game applies it
(`sl_fog_environment_alt`, read-only). Without it the replacement stayed
clear while the gas filled the room. `SL_WA_ENV_BLEND=0` keeps the main row.

A per-object triangle on the fixed pipeline - a draw the programs do not
take, such as the destroyed tank's blackened model - had no fog at all
under the row (the game leaves its distance term out of every object's
register there), and stood black and clear in the gas. It takes the row's
stage fog through GL's fog stage now; `SL_OBJ_FIXED_STAGE_FOG=0` leaves it
as before.

### The destroyed tank

The remaster has one bottling-tank item; its `gastank_hits` is a hit table
like every other `*_hits` file, not a model, so it has no destroyed
variant. On destruction the game crumples its own model's vertices and
blackens it (propobj.c), and the prop seam hands a destroyed object back to
the game (`objGetDestroyedLevel`): the tank that was shot draws as the
cartridge's destroyed model, now fogged with the room.

**A destroyed tank that was gone when the player looked back** - a native
byte-order defect, fixed. A destroyed object's crumpled vertices are a
copy in the object vertex store (`vtxstore_allocate`, type 0x0b0b: 1500
vertices in at most 40 blocks on a one-player stage); a bottling tank's
copy is 102 vertices. The allocator takes its block limit as the low half
of the count word through a halfword pun (`((s16 *)&count)[1]`). On the
cartridge, big-endian, that is 40; on a little-endian host it is the HIGH
half, 0, so the split never ran and the 102-vertex request took the whole
free block. The store then read 0 of 1500 free, and `vtxstore_tick` (lv.c),
which frees permanently the first destroyed, deformed object that is NOT on
screen while less than a quarter of the store is free
(`sub_GAME_7F056690` -> `objFreePermanently`), removed the tank as soon as
it left the view. A second and third tank in a row found no block at all
and took the allocator's failure path: no crumpled copy, and drawn shrunk
instead of blackened. The same pun limited the character paint store
(type 0xcccc, 500 vertices, 20 blocks) to one copy. Natively the limit is
now read by value (`src/game/vtxstore.c`, inside `#ifndef __sgi`; the
`__sgi` arm is the pun as before).

Measured with the synthetic pad (three tanks shot in a row from the
bottling-room pose, a turn away and back, seed pinned): before, the store
went to 0 free on the first tank, the second and third failed, and the
first was freed on the frame it left the view; after, the three copies
leave 1398, 1296 and 1194 free, nothing is freed, and all three blackened
tanks stand after the turn back. On the cartridge the quarter mark (375
free) is reached only past 1125 deformed vertices - Facility's ten tanks
take 1020.

Past that mark the cartridge would free an off-screen destroyed object
all the same. Natively the destroyed-object store is eight times the
cartridge's (D-028, `SL_DESTROYED_KEEP=0` restores it), so a destroyed
object stays for the rest of the stage.

The tank's footprint is not walkable either way: the level's floor tiles
(the stan) have a hole under it, so the player slides around the spot
whether the tank stands, was freed, or is kept - the path of a walk into
it is float-identical in all three, and the prop list confirms the freed
tank was gone from it.

### Room 14

Room 14 is the vertical vent shaft beside the starting duct, seen from the
duct through a portal a few pixels wide. On the cartridge it runs about
900 world units below the bathroom ceiling as a room overlapping others;
the remaster ends it at the ceiling (its shaft walls stop at y -93, its
bathroom ceiling at y -120 spans the shaft). The game's surfaces with no
remastered surface near them there are that continuation: the remaster's
own design, not a gap in the item.

### Lights

No Facility room triangle carries one of the light images the game breaks
when shot (`check_if_imageID_is_light`, lightfixture.c), so no Facility light
can be shot out; the item's scale reaches the shot-out lights all the same
(Dam measurement above).

### The texture set on this stage

The direct and re-tiled entries and the carried coordinates act on the
game's ROOM triangles. While the replacement draws, those are withheld, so
on Facility the set's floor, walls and signs do not draw - the remaster's
item does. They are kept: they are what XBLA draws with `SL_WA_PREVIEW=0`,
and the same ids draw on other stages.

## Runway's background and sky (#78)

Runway's whole static background is the remaster's own item
(`background/runway`) and its sky the remaster's dome (`skydome/runway`),
both through the paths the Dam and Facility use, unchanged: the room
background emission is withheld only on a frame whose replacement was
loaded, validated, uploaded and drawn, and rooms, portals, collision,
guards, props, doors, vehicles and characters stay the game's. It is what
PRESENTATION XBLA draws on Runway (`sl_wa_stage_item_path`); `SL_WA_PREVIEW=0`
keeps the game's rooms with the texture set's artwork, as on Facility, and a
root with neither bundle prepared draws that look as well.

```
.\tools\windows\prepare-xbla-assets.ps1 -Level runway -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level runway -Skydome
```

`-Skydome` is the generic form of `-DamSkydome` (`-Level dam -Skydome`);
the Dam's dome, the Dam's and Facility's backgrounds and the stage-fog table
regenerate byte-identical.

What the reader takes: 891 records, 219 draws, 217 kept - two pools of 24
and 16 vertices carry no texture coordinate and do not validate - 18436
triangles, 53 materials of 48 images, 5 of them the decal pass and 11
translucent; every pixel program declares the stage fog (environment row
35). The item's scale is the rule above: Runway's last `levelinfotable`
column is 4.5537338, so the item is the room coordinates times 21.96.

### Registration, measured

Over the whole item against the game's room triangles (both subdivided to
pieces under 400 world units, point to triangle):

- **The ground and the interiors coincide.** 96.2% of the game's floor area
  has a remastered surface within 5 world units. A vertical ray under nine
  of the replay's outdoor eyes meets the game's floor at y -658.7 and the
  remaster's at -660.9 every time. The interior rooms (2, 3, 4, 7, 15, 16)
  agree to within 5.4 units everywhere.
- **The mountains are remodelled.** Of the game's walls and slopes only
  19.8% of the area has a remastered surface within 5 units, and 73% none
  within 25: the outdoor rooms' cliffs (rooms 5, 6, 8 - 14, 17) are the
  remaster's own shape, beyond and around the game's. Collision stays the
  game's - the player meets the cartridge's cliff, the eye sees the
  remaster's - which is the same contract as the Dam's.

### The sky

Runway's environment draws a cloud layer, so the game's sky pass draws its
dark cloud sky under the rooms (no black backdrop: `sl_wa_stage_flat_backdrop`
is 0). With the background alone that sky showed above the remaster's
panorama, with the panorama's own pale horizon as a band along its top edge.
The remaster's dome is drawn beneath the background exactly as the Dam's is
(`sl_wa_compose_skydome`): a pale blue sky with soft cloud, continuous with
the panorama's horizon, covering the game's sky.

Overhead, the source's dome is a clear deep-blue cap inside a ring of cloud -
the same image structure as the Dam's (a flat cap to 17.8 degrees from the
pole, the band settled at row 271). The generated zenith clouds (D-023) are
therefore written into Runway's dome as they are into the Dam's
(`DIVERGENCE_ITEMS`, tools/worldasset/prepare.py), riding the game's own
cloud plane and phase; `SL_SKY_ZENITH=0` draws the source's dome. The
panorama shell (D-026) stays the Dam's: Runway's background's outer ring -
found by the same geometry rule, meshes 35 and 48 - is not fully fogged
against the sky, the fade changed no pixel at three outdoor marks, and it
draws as authored. The Dam's fog range (D-025) is keyed by the Dam's
environment row and does not reach Runway's.

### Measured

At the replay's 23 marks (teleport to each mark's eye and view, seed pinned,
read 80): ORIGINAL and COMMUNITY HD byte-identical to the previous build at
all 23; under XBLA every mark draws the remaster's world - the building's
exterior and its tower, the mountains and their trees, the snow, the
runway's frosted asphalt, the tunnel portal beside the tank, the conveyor's
rusted rails, the concrete planters, the interiors' floors and ceiling tiles.
Props, doors, vehicles, turrets and characters draw as before.

### The dock numbers

The numbers painted on the loading dock's wall fought the wall: from one
frame to the next a different part of each number showed. Casting the eye
of the replay's second mark against the item, every ray through a number
crosses two surfaces at the same distance to the thousandth of a unit - the
wall (mesh 19, opaque) and the number (meshes 56 and 57, alpha cutouts):
two quads of two triangles each, in a vertex pool of their own, lying
exactly in the wall's plane at z -12200 and drawn after it. Their style
word is 0x4701, the ordinary world style; the source's decal bit (0x2000,
"The coplanar pass" above) is not on them, so they took no depth priority
and, as a later surface in an earlier one's plane, were drawn under the
order rule ("Two of the item's own surfaces in one plane") - which resolves
two coincident surfaces from different triangles by rounding.

The preparation now recognises such a draw from the geometry
(`overlay_meshes`, tools/worldasset/prepare.py): a CUTOUT mesh every one of
whose triangles has its centroid inside a triangle of an EARLIER opaque,
non-decal mesh in the same plane (the runtime's coplanar thresholds, 0.01
item units and 0.9999) is an overlay - nothing of it stands off that
surface, so the only way it is seen is on top of it - and takes the decal
bit the source gives its own decal pass. On Runway it is exactly the two
numbers (4 triangles); the Dam and Facility have none (their shaped meshes
that touch a surface also stand off it - foliage on the ground, a grating at
a floor) and regenerate byte-identical.

Measured walking back from the second mark (teleport, seed pinned, every
fourth read): before, each frame showed a different fragment of the "2";
after, the whole number, every frame.

### Beyond the game's far plane

Runway's distant mountains and the far end of its buildings appeared and
vanished in whole pieces as the player walked or turned. The replacement was
being drawn whole every frame - nothing about rooms, portals or the game's
visibility gates it - so the decision that failed was a depth one, in the
background pass itself.

Runway's far plane is its environment row's `FarFog`, 15000 (`fog_tables`,
`bgfog.c`), at a visibility scale of 1.0; recovered from the matrix in force
it reads w 14950 - 15080. The remaster's item reaches w 32000 - 43700 from
the replay's runway eyes: at the tenth mark about 900 vertices a frame lie past
the far plane, at the fourth up to 10107. The pass clamped every one of them
to the far plane, so they all reached the depth buffer at 1.0 and were
ordered by the item's draw order instead of their distance: the panorama's
far, hazy range covered the nearer snowy range behind the building, and a
piece of either flipped as it crossed w 15000. Neither the Dam (a far
plane of 75000 world units at its 0.2 visibility scale) nor Facility has a
vertex past the far plane at any pose measured, which is why it surfaced
here.

A frame with any vertex past the far plane is now drawn in two depth ranges
(`sl_wa_far_plane`, `sl_wa_far_range`): first the part of the item beyond the
game's far plane, its x, y and w unchanged and its z mapped from just inside
the far plane to just beyond the item's furthest vertex, so the depth test
orders it; then the depth is cleared inside the aperture - every pixel holds
1.0 on entry to the pass, measured over 122 frames, so the clear restores what
was there - and the item is drawn again under the game's own projection,
unclamped. That second draw leaves exactly the depth the game's props, doors
and characters test against, and the far plane clips what the first range
already drew. A frame with nothing past the far plane is the single draw it
was. `SL_BG_FARPASS=0` keeps the clamped single draw; the census
(`SL_DL_CENSUS=1`) reports how many frames took two ranges.

Measured at the second replay's marks (teleport, seed pinned, read 80): at
the building marks 10 and 11 the snowy range behind the complex and the
building's far side now draw where the haze stood (32590 and 32877 pixels),
at the mountain marks 4 - 7 the cliff edges and the horizon (472 - 4128
pixels); the interior marks 1 and 2 are unchanged. Walking back from mark
10 frame by frame, the piece of cliff that appeared between two frames (1409
pixels in the band) no longer does (288). ORIGINAL and COMMUNITY HD are
byte-identical at all 11 marks; with `SL_BG_FARPASS=0` marks 4 and 10 are
byte-identical to the previous build; Dam and Facility witnesses and a
Surface dome pose are byte-identical under XBLA.

**The overlap (#80).** The two ranges met a thousandth inside the far
plane, and on Surface that was not enough: the playtest mark showed two long
thin streaks of sky ("rays") through the panorama shell, running from the
cloud cap down to the treeline. They are not the sky or the clouds
(`SL_SKY_ZENITH=0` and `SL_SKY_WITH_BG=0` keep them, showing whatever lies
under the background), nor the coplanar, two-sided or fill rules (each
switched off is byte-identical); `SL_BG_FARPASS=0` removes them and changes
no other pixel (1456 pixels, all of them the streaks). With the shell alone
drawn and one range at a time, each streak is the strip NEITHER range drew,
1 - 5 pixels wide along a line of constant depth. The second range's far
clip is where the game's clip z meets w, and z - w moves by only
2n / (f - n) of w per unit of depth - 0.0016 on Surface (near 2, far 2522.6
in clip w, the same k on all three rows of the matrix) - so the rounding of
z and w puts that clip up to about 0.3% short of the recovered far plane.
The first range now starts `SL_WA_FAR_OVERLAP` (0.99) of the far plane in
for an opaque draw: at the playtest mark the strip goes from 1240 pixels to
none (0.995 also reaches none; 0.99 is three times the shortfall seen).
Between the two the second range draws last over a cleared depth buffer, so
its picture stands where both draw - for a draw that does not blend. A
blended one (the translucent class, and a cutout's soft edge,
`SL_WA_BLEND_BLENDS`) is laid twice in the overlap: with the whole 0.99 the
canopy edges of distant trees at Runway's mountain mark (the second replay's
mark 4) went a step darker over 146 pixels, so a blended draw keeps the
thousandth it always had (`SL_WA_FAR_OVERLAP_BLEND`, 0.999).

The streaks lay in the PANORAMA SHELL, and the shell blends: its upper band
is translucent (at 0.999 348 of its pixels stayed open), and with D-026
fading it on Surface (D-030) its opaque wall is drawn blended too - with the
0.99 overlap it was laid twice and stood as two dark wedges in the haze along
the streaks' old lines. The shell is the item's outermost ring
(`prepare.panorama_shell`: every vertex of it farther out than every other
vertex), so along any ray from an eye inside it it is the last surface. On a
two-range frame it is therefore drawn ONCE, whole, in the first range, under
`GL_DEPTH_CLAMP` so the part of it nearer than that range's start keeps its
pixels (at the nearest depth) instead of being cut, and the second range
skips it (`bg_far_shell`): nothing the first range draws can stand in front of
such a part - it would lie outside the ring - and the second range's nearer
scenery draws over it as it always did. Measured at playtest mark 2:
no streak, no wedge (against `SL_BG_FARPASS=0` the frame differs only at the
foot of the far tower, where the two ranges order the scenery and the clamped
single draw does not, max 7 levels); with `SL_SKY_PANORAMA=0` and
`SL_XBLA_GAME_ATMOSPHERE=0` no pixel of the streaks stays open. Runway's
replay marks 4 and 10 change in 53 and 55 pixels (the opaque overlap; at
most 72 and 2 levels); the Dam (no vertex past its far plane) and Facility
witnesses are byte-identical.

### The bunker door's strip: the native fill

The marked pose above the bunker door (room 4) shows a strip of sky over
the door and a sliver beside it. Measured (`doorgeo.py`, both geometries in
world units): the game's wall has the door opening at x 7525..7726, y
-458..-246, the remaster's wall (mesh 11, coincident with the game's to 3
units) cuts it at x 7534..7728, y -458..-239 - seven units taller and a few
wider - and the door, the game's box in either presentation, is the size of
the game's opening. The console build shows the same strip at the same pose,
and the source has no triangle around or beyond the opening in either
generation: nothing in the remaster can close it. The game's wall can.

THE NATIVE FILL (`sl_gfx_worldasset.h`, `#78`). A room triangle of the game's
is REGISTERED when each of its corners, its edge midpoints and its centroid
lies within `SL_WA_FILL_REGISTER` (8) world units of a depth-writing surface
of the replacement - a triangle the replacement reproduces, give or take the
registration the two geometries have (interiors coincide to about five
units; a remodelled cliff is tens of units away and never registers; a lake
bed under translucent water, or a ceiling the remaster remodelled, touches
the replacement at its corners and leaves it in its middle). It is HOLED
when some point of a barycentric grid over it (`SL_WA_FILL_GRID` 8 along an
edge) lies `SL_WA_FILL_HOLE` (2.5) world units from every replacement
surface: the strip above the door is seven units tall and the crack beside
it three and a half, so each has points that far from the remaster's cut
edges, while a wall top or barrier edge the remaster moved by a unit or two
leaves a sliver every point of which is within that - the replacement's own
shape, not a hole, and before this test those slivers drew as hairlines
along a few silhouettes at the Dam. The radius was swept at 1.5, 2.5 and
3.5: at 3.5 the crack beside the door reopens, at 1.5 the Dam's barrier
edge draws a sliver, and 2.5 closes the doorway with every Dam, Facility
and Surface witness pose byte-identical but one barrier corner (159
pixels). What remains at that radius on Runway is what the game's walls
are and the remaster's are not - a wall top two units proud of the
remaster's roof draws as a ledge along its silhouette (mark 3), a railing
brace the remaster placed elsewhere shows behind the remaster's (mark 2),
and single-pixel outlines along a few wall silhouettes (marks 4-7, 10,
11; 0.03-0.2% of the frame) - the fill's residue, carried as debt;
`SL_BG_FILL=0` is the switch. A registered, holed triangle is not
suppressed: it is
drawn `SL_WA_FILL_PUSH` (12) world units behind its own plane, along
its normal away from the eye, so wherever the replacement has a surface
within that distance the replacement wins the depth test and the game's
triangle is invisible, and only where the replacement has nothing - the
strip above the door - does the game's surface show, with the texture set
the presentation gives the game's own geometry. The push is along the
normal rather than the line of sight so a floor two units above the
replacement's stays hidden at a grazing angle too. Only an opaque,
depth-tested, depth-writing, opaque-zmode, non-alpha-compared draw fills (a
fence or a window would show through the replacement's own holes), and a
near-clipped fan - drawn from clip positions no push reaches - is
suppressed as before.

The provider holds the grid (`sl_wa_fill_set`: hashed cells of sixteen
registration radii over every material but ALPHA, in three levels each
sixteen times coarser than the last, a triangle at the first level where it
spans at most eight cells on every axis and one wider than that at the
coarsest level in a list of its own, rebuilt when the item changes;
`sl_wa_fill_distance` visits the cells the search box touches at each level
and then the list; self-tested). The renderer runs the
background pass's own mapping backwards for the corners, under the world
projection in force at the triangle's OWN draw - the first build read the
branch the background pass had run under, and the room lists draw under the
other one: the pass ran FOLDED (the view in the projection) while the rooms
came PLAIN (the view in the modelview), so every corner came out turned by
the camera's yaw and nothing registered. Under the plain projection the
loaded position is view space and the inverse of the seam's view (the
visibility scale on its output, as bg_draw scales it) takes it to world
times the visibility scale; under the folded one it is already world less
the render origin, times that scale. The decision is cached by the
triangle's room and its own coordinates (the game's room geometry never
moves; 64k entries), and the three loaded positions are pushed for the one
draw and restored after. A near-clipped fan fills too - the wall beside the
marked pose is one - its sub-triangles' eye positions registering as any
triangle's and their clip positions moved by the same vector through the
projection, decided every frame rather than cached since the cut moves with
the eye. `SL_BG_FILL=0` suppresses every room triangle, as before;
`SL_BG_FILL_TRACE=<room>` prints a room's first decisions with each corner's
distance, and the census line `#78 nativefill:` counts the triangles drawn
behind the replacement and the decisions taken.

Measured at the marked pose (room 4, XBLA): the wall's corners register at
0.4 .. 10.3 item units of the 15.7 allowed, the strip above the door and the
sliver beside it close with the game's wall under the presentation's
texture set (12761 pixels changed against the build before), and the door
reads as a doorway.

**The first sight of a scene.** The grid had one level and a list, and on
Surface the list held 2272 of the item's 39199 triangles - the mountains
and the snowfields, up to 77 cells wide - which every distance query then
tested after its cells. A decision is up to 52 queries (the seven corner and
middle samples and the 45-sample hole grid), so each cost about 2 ms; the
2200 room triangles first seen at a pose stalled the frame for 4.3 seconds
(`sl_tick` first window `ticks=1 frames=2 dt=4.313`, the same at two other
outdoor poses), and every room entering view hitched for as long as its
triangles took to decide. A per-call profile could not see it - the
decisions are cached, so the lookups measured a fraction of a microsecond -
and a build with the lookup stubbed out could: the whole difference between
the stalled and the stubbed run was that first window. With the levels
(cells of 16 and 256 cells hold every wide triangle of Surface, Facility and
the Dam and all but 12 of Runway's) the first window is the ordinary load
(`ticks=49 frames=98 dt=2.008`, as under ORIGINAL) and the steady state is
unchanged, the pacer's 60 ticks a second; the answers are the same minimum
over the same triangles, and the three poses are byte-identical with the
levels alone against the build before.

**A room the remaster remodelled whole (#83).** Frigate's boarding boat
(room 57) is another object in the remaster - its own hull, gunwale and bow.
Judged surface by surface, 17 of the room's 64 triangles register on the
remodelled boat and 14 are holed (the gunwale strip 3.38 item units from it
at its farthest point against the 3.04 needed, 2.5 world units; hull panels
at x 39 .. 47) with the sea open behind, so they drew pushed back and showed past the
remaster's silhouette as white shards (vertex colour fefefe). Runway's door
strip is the same class by every measure the rule has (room 4: 13 of 13
registered triangles holed, open behind), so the rule stays and the room is
named instead: `sl_presentation_fill_withheld` lists (xbla, frigate, 57), and
`nf_registered` suppresses every triangle of a listed room as before the fill
(`sl_wa_fill_withheld`; `SL_BG_FILL_WITHHELD=0` fills it as the rule
decides). Measured at the boat poses (the playtest's marks 1 and 2 of run
20261006-201517 and four waterline poses): the shards are gone, and two of
the six frames are byte-identical with `SL_BG_FILL=0` while the other four
keep the fill elsewhere (the hull at the waterline); Runway's start frame and
the door witness above are byte-identical with the build before, and the
door witness still differs from `SL_BG_FILL=0` by the 12761 pixels measured
above.

## Surface's background, sky and props (#80)

Surface's whole static background is the remaster's `background/surface`
and its sky `skydome/surface`, drawn through the same paths as the Dam's,
Facility's and Runway's (`sl_wa_stage_item_path`, stage 36; `SL_WA_PREVIEW=0`
keeps the game's rooms with the texture set's artwork). The source holds one
`background/surface` and two domes, `skydome/surface` and `skydome/surface2`;
the first stage's entry names the first of each.

```
.\tools\windows\prepare-xbla-assets.ps1 -Level surface -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level surface -Skydome
.\tools\windows\prepare-xbla-assets.ps1 -Level surface -Props
.\tools\windows\prepare-xbla-assets.ps1 -Level surface -Characters
```

The item's scale is the rule above: Surface's last `levelinfotable` column
is 22.603975, so the item is the room coordinates times 4.42. Registration,
measured as Runway's was (both geometries subdivided under 400 world units,
point to triangle): 99.0% of the game's floor area has a remastered surface
within one world unit and 100% within five; of the game's walls and slopes
half are within five units and half are remodelled - the cliffs and the
treeline walls, as on Runway - and 90% of the remaster's wall and slope area
is more than 25 units from any game surface (its own mountains). Collision
stays the game's.

### One tree, drawn sixty-four times

The first reading of the item kept 84 of its 215 draws. Every draw it lost
belongs to ONE TREE: a canopy of 66 crossed alpha-cutout quads (264 vertices
of 20 bytes) and a trunk (23 vertices of 28 bytes), drawn 64 times each -
the first time at the pool's own coordinates and 63 times under a record
the reader had never met, op 0x21: a rendergraph offset, two zero words and
a 4x4 row-vector matrix, bracketed by op 0x23 before and op 0x25 (nineteen
zero words) after. 126 matrices: 24 distinct rotations, every one about y,
two of them stretching the tree by 1.2113 along y, and translations over the
whole level. The two tree pools also carry their texture coordinate in a
SECOND STREAM - a stride-4 buffer of one word per vertex (1056 bytes for
264, 92 for 23) the rendergraph describes in the same four words as any
vertex buffer, laid between the pool and its index run - so no position
buffer ended where the run began: the canopy draws bound to the previous
pool and failed its range, and the trunk draws bound to it silently (23
indices into a 94-vertex pool) and read garbage.

Both are reader rules now (`tools/worldasset/xblaworld.py`):

- **A pool named through a trailing stream** (`pool_ending_at`). When no
  position buffer ends at an index run, a stream of `STREAM_STRIDES` (4 or
  8 bytes per vertex) ending there names the position buffer ending where
  the stream begins, provided the two describe the same number of vertices.
  The pool's records are then read as one buffer with the stream's bytes
  laid in front of the colour word (`pool_source`), and the texture
  coordinate is MEASURED over that record by the edge and pile-up rule like
  any other - not taken from the stream because it is there. It measures
  there: the canopy's quads read 0 / 255 / 256 at 1/256, the trunk's wrap
  at 1/128. A measured layout (`POOL_LAYOUTS`) is never asked about a
  streamed pool (`pool_layout_stride`).
- **The instance matrix** (`OP_INSTANCE`, `instance_matrix`). A draw after
  an op-0x21 record carries its matrix until op 0x25 clears it; a matrix is
  taken only when it is finite, its last column (0, 0, 0, 1) and its rows
  pairwise perpendicular - a placement, rotation and a scale per axis - and
  refused otherwise. The preparation bakes each (pool, matrix) as its own
  copy of the pool with every position carried through the matrix
  (`prepare.instance_vertices`); the bundle is static geometry, so baking is
  exact. A draw that also blends two layers, animates or shades through a
  tangent frame is not baked (none is) and is reported.

With them the item reads 212 of 215 draws (the three refused are untextured
draws of a 94-vertex pool with no image), 126 placements, 39437 triangles
against 29453. The Dam's, Facility's and Runway's items carry neither a
stream nor an instance record and regenerate byte-identical.

### The sky, and beyond the far plane

Surface's dome is the Dam's and Runway's kind - a clear deep-blue cap inside
a ring of cloud; the generator measures the same cap (17.8 degrees from the
pole, the band settled at 23.2 degrees, row 271). The generated zenith
clouds (D-023) are written for it (`DIVERGENCE_ITEMS`); `SL_SKY_ZENITH=0`
draws the source's dome. The Dam's fog range
(D-025) stays the Dam's; the panorama shell (D-026) is Surface's too since D-030.

Surface's far plane is its environment row's `FarFog` 2500 at the stage's
0.2 visibility scale, and the item reaches far past it: at every outdoor
pose measured (61 of 61 frames at each of three marks and a look-up) between
889 and 28286 vertices a frame lie beyond it, so the two-range far pass
written for Runway takes every frame; nothing new was needed.

### The props, and the satellite dish

`-Level surface -Props` prepares 21 of the stage's 25 models: the satellite
dish (`sevdish`, an autogun - four joints riding the game's three
matrices), the four `sevdoor*` doors, the safe door, the motorbike, the
book, the desk lamp, the wall map, the peaked hat, and the ten shared with
earlier stages, which regenerate byte-identical. Three refuse conversion and
keep the game's model: `jerry_can1` and `stool1` (their coordinates reach no
atlas edge at any power-of-two scale) and `carbmw` (a 16-byte vertex buffer,
a stride the prop reader does not take) - docs/backlog.md B-SRFPROPS.

**The bookshelf (#80).** `bookshelf1` was refused, "a draw names image 26 of
10" (playtest run 20261005-120919, mark 13). Each of its ten draws is
preceded by two material records: the first binds slot 0 to the draw's own
image (0 .. 9), the second binds slots 1 and 2 to images 25 and 26 - the
engine's own resources, not the item's, which carries ten. The reader took
the last record's largest image as the draw's. A record that names none of
the item's images now leaves the draw's image as the record before set it
(`xblachar.own_slots`); every item whose records name only its own images
reads as before. The bookshelf prepares (10 draws, 10 images, 64 triangles)
and rides the game's model by its box like any shelf. `-Characters` prepares the three bodies
(`commguard`, `snowguard`, `bluecamguard`) and the 25 random heads.

**The snow guard's left ankle (#80).** The snow guard prepared but drew the
game's model ("NPC models are still the original", run 20261005-120919,
mark 5): the runtime refused it - `a vertex is bound to SKEL_LF_ANKLE, which
the model does not have`. Its remastered pose lists SKEL_LF_ANKLE FIRST, with
no parent and its offset equal to its bind (376.2, -4662.5) - a second root
standing where the knee chain puts the ankle - while the original generation
of the same item and the remastered commando hold the same joint, at the same
bind, under SKEL_LF_KNEE; 164 vertices are weighted to it. The runtime's match
walks the item's tree from its hub, so a second root is never reached. Of the
80 character and head poses in the source, it is the only one with two roots.
The reader mends it (`xblachar.mend_stray_roots`): a childless root that is a
side's SKEL_LF_/SKEL_RT_ joint takes the mirror of its mirror's parent when
the bone that makes is within 10% of the mirror's length (2237.6 against
2174.2 here), and the joints are re-ordered parents first. Anything else is
left as read. `-Characters char/snowguard` re-prepares it; the guard then
rides the game's skeleton like the commando (16 item joints on 15 game
joints), both feet on the game's ankles.

The dish was suspected of a level-of-detail defect. Measured at the two
replay marks that show it and along a ladder of three closer poses (2000,
4000 and 6000 units along the second mark's view): the dish is the game's
PROP in every arm - the remaster item with the background, the same item in
the game's near fog with the background off, the game's model with the
texture set's artwork with the prop seam off - and not a piece of the
background; the item draws whole and the same at every distance, while the
game's model under ORIGINAL changes only by the game's fog. What the replay
showed at distance was the game's fog over the game's model, which the
remaster's stage fog does not reach. No wrong output was demonstrated.

### Measured

At the replay's 13 marks (teleport, seed pinned, read 80): ORIGINAL at mark
13 is byte-identical to the previous build; under XBLA marks 1 - 12 draw the
remaster's world - the snow, the cliffs and the mountains, the treeline and
the placed trees, the fences, the cabins outside and in, the guard tower's
surroundings, the dome building and its interior, the dish - and the
prepared props.

### The dome, and the first sight of a scene

The replay's next mark (the dome building from its stair, XBLA) showed the
dome's cap striped black and white, and the stage felt slow. The cap is
the two sides of one curved shell drawn on the same vertices with different
triangulations, which the two-sided rule missed for three reasons - its
tolerance, its push and, on this stage, its eye - all three under "Two
sides of one wall" above; with them the cap is the outer shell alone, as
`SL_BG_HIDE=12` draws it, from the mark and from two more bearings, and
ORIGINAL and COMMUNITY at the mark are byte-identical to the build before.

The slowness was measured before it was named: at the mark and two more
outdoor poses, XBLA against ORIGINAL and against each of the pass's switches
off in turn (frame-time harness, 449 presented frames, the replay's window
size). The far pass, the trees, the coplanar rules and the zenith cost
nothing measurable; the replacement is cheaper than the native rooms it
suppresses (`SL_WA_PREVIEW=0` costs ten milliseconds more); and the replay's
lead - the draw distance - measures to nothing here: the stage's
presented fog row (36) ends at 45000 world units and the item's farthest
triangle lies at 33000 .. 38700 from these poses, so no triangle of it is
wholly beyond the fog and a fog cull would remove none. What remained was
the native fill's decisions: a 4.3-second stall at every first sight of a
scene, under "The bunker door's strip" above, which the grid's levels end.
After it the three poses average 20.3, 20.3 and 19.6 ms a presented frame
over a 449-frame run under XBLA (29.5, 30.3 and 27.4 before, with the
stall), steady at the pacer's 60 ticks a second; ORIGINAL's 45, 48 and 21
are unchanged. ORIGINAL's own cost on this stage - every room triangle
through the interpreter's immediate-mode draw, about 8 microseconds each
over four thousand a frame - is the game's path and not this pass's; it is
on the backlog.

### Presentation fog range on Surface (D-029)

The second row of the presentation fog range (`sl_presentation_fog_end`,
"Presentation fog range" above), and the first that SHORTENS one:
Surface's gameplay row (environment 36, authored start -100, end 45000,
colour 164 150 130) is drawn to **12071** under XBLA
(docs/divergences.md **D-029**). Colour, start, the equation, the provider
and the `SL_XBLA_AUTHENTIC_FOG=1` control are D-025's.

The value is the game's own. `fogGetPropDistColor` (src/game/bgfog.c)
stops a prop or a character from being drawn once the cartridge's object
fog would exceed full density; on Surface's row that is view depth
12071.2 world units. The remaster's fog is measured along the straight
line from the eye, never shorter than the view depth, so with the end at
the cut nothing the game removes is still showing:

    distance   cartridge   XBLA authored (45000)   XBLA presented (12071)
        2000      0.42            0.05                   0.17
        6000      0.88            0.14                   0.50
       10000      0.98            0.22                   0.83
       12071      1.00 (cut)      0.27                   1.00
       20000      1.00            0.45                   1.00
       30000      1.00            0.67                   1.00

The satellite dish (`sevdish`, an autogun) was the witness: approached from
the replay's two cameras it was cut at zDepth 12073.6 and drawn at 12069.4,
appearing whole at 27% fog; now it comes out of the fog. In its window the
change across the cut fell from 126 levels at most (mean 21.3) to 8 (mean
0.56), the camera step's own noise.

Beyond the end the frame is the fog colour: the remaster's mountains and
the painted sky shell behind them, 20000-38700 units out, read as one flat
band from the treeline to the zenith cloud cap (D-023, which the stage fog
does not reach), meeting the cap's feathered rim without an edge. A cull of
the replacement's draws wholly beyond the end is not made: it would remove
28 of 69 draws (1155 triangles) at the first replay camera, and the fully
fogged geometry is what covers the unfogged dome - skipped, the dome shows
where it stood. The frame time at the two cameras is unchanged (20.1 ms a
presented frame, pacing-capped).

### The game's atmosphere on Surface (D-030)

That band was rejected in playtest (run 20261005-100452-lvl36, marks 1 and 2):
"dirty beige/grey haze between the snow and a bright blue cloud cap - it
does not read as a snowstorm", against ORIGINAL's one cold storm, in which
the sky, the distance fog and the snow are the same weather. What each
presentation consumes at the mark, measured:

| | ORIGINAL | XBLA before | XBLA now |
|---|---|---|---|
| fog colour | 96 96 128 (the game's row 36) | 164 150 130 (the remaster's row 36) | 96 96 128, read from the game each frame |
| fog range | the game's (FarFog 2500, visibility 0.2) | -100 .. 12071 (D-029) | -100 .. 12071 (D-029) |
| sky | the game's cloud layer: 240 120 30 over the fog colour overhead, the fog colour at the horizon (sky.c) | the remaster's dome (the Dam's image) and the D-023 field, unfogged | the same dome and field, shaded by the game's cloud formula |
| the painted ring | none | fully fogged to the beige, standing to 25 - 30 degrees | fades above the fogged scenery (D-026) |

The remaster's row is Surface's own (id 0x24, the decomp's row-36
signature, between Runway's variants and Jungle's); its colour is one of
three the remaster changed (the Dam and Runway take a sky blue), and its
dome is the image every one of the source's eleven domes carries - a
beta's placeholder pair, not a Surface sky. D-030 keeps the remaster's
geometry, images and range and takes the atmosphere from the game:

- `sl_presentation_game_atmosphere` names (xbla, 36);
- `env_in_force` gives that row the game's fog colour
  (`sl_wa_game_atmosphere`, read-only), so the replacement, the props and
  characters fogged by it and the shell all fade to the colour the game
  holds - and follow it if the game changes it;
- `sky_draw` draws the dome and the cloud field through the game's cloud
  combiner: per vertex the game's cloud shade along the vertex's direction
  from the anchor (`sl_wa_sky_shade`: full cloud over the fog from 26.6
  degrees up, the fog colour at the horizon), interpolated by the image
  toward the fog colour (`sky_atm_texenv`, GL_INTERPOLATE, alpha as before);
- the prepared background marks its outermost ring (meshes 36 and 48, the
  painted panorama, 46517 - 49414 item units out) as D-026's shell, and the
  fade lets the sky come down to the fogged treeline with no band.

```
.\tools\windows\prepare-xbla-assets.ps1 -Level surface -Background
```

re-prepares the background: two material flag words change (2 -> 18). An
older bundle draws the band as before under the new colour.

Measured at the three playtest marks (teleport, seed pinned, read 80):
ORIGINAL and COMMUNITY HD byte-identical; the satellite dish crosses the
prop cut with at most 5 levels of change in its window (8 before);
`SL_XBLA_GAME_ATMOSPHERE=0` draws the remaster's colour and the dome as
authored. What it does not do: the remaster's mountains (20000 - 38700
units) stay inside the haze. Showing them needs a fog end past the game's
prop cut, and the cut on the draw side lifted with it, so the dish does not
pop - this needs a design ruling (docs/backlog.md).

## Bunker 1's background (#81)

Bunker 1 (stage 9) draws the remaster's `background/bunker` under XBLA
through the same paths as the four stages before it (`sl_wa_stage_item_path`,
`SL_WA_PREVIEW=0` keeps the game's rooms with the texture set's artwork).
The source spells the stage without the game's number: Bunker 1's item is
`bunker`, Bunker 2's `bunker2`, so the entry is a lookup, not a derivation.

```
.\tools\windows\prepare-xbla-assets.ps1 -Level bunker -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level bunker1 -Props
.\tools\windows\prepare-xbla-assets.ps1 -Level bunker1 -Characters
.\tools\windows\prepare-xbla-assets.ps1 -Level bunker1 -ViewModels
```

`-Background` takes the source's item name; the other three take the
game's stage name, as everywhere else.

The item's scale is the rule above: Bunker 1's last `levelinfotable` column
is 23.148148, so the item is the room coordinates times 4.32 (measured
4.319999 in the table under "The item's own scale"). Its declared bounds
reach 58790 item units: beside the bunker it carries the snowfield over it,
a painted sky shell and a cloud ring. The level has no sky: it is drawn
from the game's fogless table, and the one opening onto that exterior, the
vent shaft's grate, shows the game's navy fill in the remaster - see "The
second playtest" below (the stage has no skydome entry and none is
prepared). No panorama shell is marked: nothing in it lies wholly outside
the rest and surrounds it.

Measured at 21 poses, one or two per major room (teleport, seed pinned,
read 80; rooms 1, 3, 5 - 8, 10, 13, 14, 16 - 20, 24, 26, 27, 29): XBLA draws
the remaster's rooms everywhere - the vent room's ribbed walls, the
corridors' panels and light strips, the control room, the rock caverns and
tunnel - on the game's floors and walls, with no misplaced wall or floor
seen at any pose. Two things the game draws were missing: every railing,
and the black backing of the control room's screen wall, through whose
opening the snowfield and sky showed white. Both are the reader's, below.
ORIGINAL and COMMUNITY HD are untouched by the entry: at 25 poses (the 21
above, two more in the control room, the cavern and a balcony) both are
byte-identical to the build before, 50 of 50 frames.

### The railings, and the surfaces with no image

The first reading kept 226 of the item's 236 draws. Eight of the ten it
dropped stand on two pools the reader refused, and both pools have the same
20-byte record: a position, ONE word, and the colour. The word is not a
texture coordinate. Read as a 2_10_10_10 signed normal (`snorm10x3`) it has
length 0.997 .. 1.000 on all 2990 vertices of the first pool and 0.998 ..
1.000 on all 154 of the second; read as an s16 pair it measures 0.0009 units
per raw unit along an edge and the pool fails validation.

- **The railings** (2990 vertices, two draws, 3216 triangles): every
  handrail, stair rail and post of the vent room, the control room and the
  stairs. The colour word carries their baked shading (greys 0x1a .. 0xb3).
  The material binds a chrome sphere image (a 128x128 reflective bubble) to
  slot 0 and the control room's own fisheye panorama to slot 1, and the
  program fetches both at coordinates its vertex program computes: the
  source sphere-maps them by the normal. Their style is the translucent
  pass (low byte 2), drawn as the source draws it.
- **The untextured surfaces** (154 vertices, six draws, 109 triangles): draws
  that name no image at all - the black backing of the control room's screen
  wall (one quad, colour 0xff000000, in the wall's opening behind the game's
  screens), the ceiling lamps' warm faces and a few frames.

Both are reader rules now. `xblaworld._pool_sphere`: a pool still refused,
with no stream and no measured layout, whose single word between position
and colour is a unit normal on EVERY vertex (within 0.01), is read with the
normal's sphere coordinate where it has none - u = 0.5 + 0.5 n.x, v = 0.5 -
0.5 n.y, the convention the Dam water's reflection measures - in the item's
own frame. `prepare.UNTEXTURED`: a draw naming no image, on a pool that was
read, takes one white texel as its image, so it draws by its vertex colour.
With them the item keeps 234 of 236 draws, 23688 triangles (the two still
dropped: one triangle on a 3-vertex pool and an outdoor 28-triangle draw on
a 32-byte pool with no coordinate). The screen wall is black behind the
game's screens again, and the rails stand as chrome.

Not reproduced: the source's reflection turns with the eye; this one is
taken from the normal in the item's frame and does not (the reflection held
still), and the slot-1 panorama term is not drawn.

The other stages' items, re-prepared into a scratch root: the Dam's and
Runway's are byte-identical. Facility's and Surface's each carry such an
untextured pool (11 and 94 vertices) and change on re-preparation - Facility
gains two draws of five triangles (two white quads and one black triangle),
Surface three draws of 100 triangles (white quads below the snow, at the
bunker shaft). Not witnessed; the prepared bundles of those stages were not
re-prepared (docs/backlog.md B-BNK1).

### The floor and the porthole (playtest marks, run 20261005-160352-lvl9)

The first playtest of the stage (three marks in the control room) asked
whether the textures and models are the right ones. Two were not, both the
reader's, and the remaster settles each (private emulator, debug camera at
the marks):

- **The control room's floor** drew each tile of its 256x256 tile image
  over 343 world units - four times too large, so the tiles read as grey
  slabs. Its pool (1755 vertices) was read at 1/2048, where 41 of its 3510
  coordinate values land; 42 land on 1/1024 and on 1/512, 82 on 1/256. Such
  a pile-up places a grid, not a repeat: every candidate down to 1/512 holds
  the same values. THE ITEM'S REPEAT (`xblaworld.tied_divisors`, read
  in `read_level`): a pool whose measured divisor holds under 5% of its
  values (`REPEAT_WEAK`) and is tied with smaller candidates (at most 5%
  more values on them, or one) takes the tied divisor whose repeat stands
  nearest the item's LARGEST pool's - here every wall and corridor floor,
  200 item units a repeat - within a factor of 1.5. The floor reads at 1/512,
  200 units a tile, the cartridge's tile count and the remaster's. (Pool 5,
  read then at 1/2048 with 31.7% of its values on it, was not the light
  panels': that was its normal word read as the coordinate - see below.)
- **The porthole** on the control room's east wall drew the chrome bubble
  four times over by its pool's stored coordinate; the remaster draws one
  reflecting dome. Its pool carries a unit normal at +12 as well as the
  coordinate, and its one draw names the rails' bubble in slot 0. THE SPHERE
  IMAGE: an image a sphere-mapped pool's draws sample in slot 0 is a sphere
  image, and a pool with a unit normal whose every draw names one in slot 0
  is sphere-mapped by the normal too. A pool whose normals face one way
  (`sphere_uvs`, mean length at least 0.5 - the dome's is 0.81, the rails'
  0.02) is taken face-on, its mean normal the depth axis and the item's up
  its up, so the dome's middle samples the image's middle; the rails keep the
  item's axes and their bundle bytes. The room's fisheye panorama the
  remaster also shows in the dome is not drawn.

The other questions the marks raised were answered by the source: the red
and white "icon" over the ammo counter at mark 2 is the corrugated crate's
own label (`prop/metal_crate1`, the item's decal; the cartridge's crate has
its hazard label in the same place), not the HUD; the hanging monitors, the
crates, the desk, the guards, Boris and their weapons are the prepared
items; the world map's screens stay the game's.

The Dam's, Facility's, Runway's and Surface's items re-prepared with both
rules: no pool of theirs is tied or sphere-mapped; the Dam's and Runway's are
byte-identical and Facility's and Surface's differ only by the untextured
draws above.

### The second playtest (run 20261005-192329-lvl9, round bnk2)

Seven marks, all XBLA. Each was reproduced by teleport (marks 2, 5 and 7 also
by synthetic-pad walks, every read captured) and settled where the remaster
could be asked (private emulator, debug camera; that camera draws no props).

- **Mark 7, "part of the wall unloads".** The dark beams on the cavern's
  wooden wall are one translucent decal mesh lying 0.2 - 1.5 item units
  behind the planks' faces; the decal offset won only far off, so the beam's
  near part vanished as the player walked up. THE DECAL PULL
  (`SL_WA_DECAL_PULL`, `sl_wa_decal_pull_z`): a decal vertex draws at the depth
  of the point 2 item units nearer on its line of sight. `SL_BG_DECAL_PULL=0`.
- **Marks 5 and 2, the stairs and the rails.** The "z-fight" was two things.
  The stair treads are cut-out gratings in pool 5, whose word at +12 is a
  2_10_10_10 unit normal on every vertex and whose coordinate is at +16; the
  reader took the normal's word (`_uv_format` now passes over a word that is
  a unit normal on every vertex). And the game's solid white steps registered
  as native fill on the remaster's open stair, pushed 12 units back into its
  open risers: bright stripes. THE OPEN HOLE (`sl_wa_fill_open`): a fill
  triangle draws only when, along the line of sight through its hole point,
  the replacement has no surface within 1000 world units - a sphere trace
  through the fill's own grid, per frame. Runway's door strip keeps its
  fill (terrain 4900 units behind); the slivers the fill drew along Runway's
  crate rims and the Dam's road edge are gone. `SL_BG_FILL_VOID=0`. The start
  railing reads as the remaster's dark blue tubes (reflection held still, as
  before).
- **Mark 3, the control room's lights.** A translucent glow sheet lies in the
  light pillars' own plane; the remaster's LESS hides it (it shows the pillar
  and its lamp bar alone). The #77 order rule now takes translucent meshes as
  the later surface (`sl_wa_coplanar_opposed_ex`, `SL_BG_XLU_YIELD=0`).
- **Mark 1, the sky up the vent shaft.** Pool 5's grate was the same misread
  word: one texel, wholly transparent. Behind it the item's exterior (a sky
  panorama and a cloud sheet 14000 world units out) was drawn. Bunker 1 is
  a fogless stage: the game keeps its 15 .. 10000 range and fills the view
  with the environment colour (0, 16, 64), and the remaster shows a uniform
  navy field behind the grate (mode 3, 10, 53). THE FOGLESS STAGE
  (`sl_wa_fogless_range`): no geometry past the game's far plane, no black
  clear, the game's own fill. `SL_BG_FOGLESS=0`. The purple light on the shaft
  walls is the remaster's (the emulator shows it).
- **Mark 6, the main screen.** The screens are the game's props with the
  game's own image cycling; their images had no XBLA mapping. Seven are now
  mapped to the remaster's monitor images (docs/texture-packs.md, "The
  monitors' images"). The emulator's black screen wall was its debug camera,
  which draws no props.
- **Mark 4, the GoldenEye key.** It now prepares (`xblachar`: a static item's
  32-byte record; a normal-map base slot gives way): the remaster's silver
  card and holographic disc. Its gold dome is sphere-mapped in the source and
  reads its stored coordinate here (docs/backlog.md B-BNK1).

Shared seams, witnessed on the frozen stages (playtest marks of their accepted
rounds): Surface byte-identical within 5 levels at 14 pixels; Runway's door
strip unchanged, and fill slivers gone at a crate field and a rail; the Dam's
road-edge slivers gone; Facility's desk monitor shows `screennew2`. ORIGINAL
and COMMUNITY HD: byte-identical at the seven marks and at the first round's
25 poses.

### The third playtest (run 20261005-225822-lvl9, round bnk3)

Six marks, all XBLA, each reproduced by teleport; the cavern's beams also by
a synthetic-pad walk and a teleport yaw sweep, every frame captured.

- **Mark 1, black between the hazard strip's yellow bars.** The strips are
  64x1 black-and-white bar images, alpha 255 everywhere, tinted yellow by the
  vertex colour; their program takes its coverage from the image's BLUE
  (#71's `coverage_channel`), so black is clear - the original and the
  remaster (private emulator) both show the wall between the bars. The level
  item's coverage could not be read: the pool's programs are not always one
  per distinct object (`_pixel_program_layout` now walks each repeated object
  both ways and keeps the one walk that accounts for the whole pool), and
  background preparation never asked. It does now: `MAT_COVERAGE_BLUE` on 4
  draws, and the background pass swizzles alpha from blue as the item pass
  does (`SL_WA_COVERAGE=0`).
- **Mark 2, the handrails composited wrongly.** Their draws sit in the
  translucent pass but export an alpha of exactly one (no image factor,
  vertex alpha 255 throughout). Blended, they wrote no depth: every prop
  drawn after the background (it is the frame's first world draw) painted
  over them, and their back faces over their front. A blended draw whose
  alpha is one everywhere is prepared OPAQUE (4 draws, 3596 triangles: the
  rails, the porthole, one panel). Not real z-fighting.
- **Mark 3, the key's gold dome.** Its 20-byte record carries a unit normal
  where the static reader took the coordinate; it now reads the held-still
  sphere coordinate (`sphere_uvs`), as the level reader does. A buffer whose
  word is the same on every vertex reads as before.
- **Mark 4, the emblem's dome.** Blue reflecting glass in the remaster
  (emulator, first round); the original's is grey chrome. Not gold; unchanged.
- **Marks 5 and 6, the cavern beams still broken.** The middle beam is one
  quad 1000 item units long whose near end lies behind the eye; the decal
  pull was per vertex and not affine in clip w (and nothing at w <= 0), so
  the clipper faded it out toward the eye - a straight cut, planks in front.
  A decal now draws at the depth of its own PLANE moved 2 item units toward
  the eye, z' = a w + b / (1 - P/h) per triangle (`sl_wa_decal_plane_z`),
  which clips exactly and covers the same gap at every viewing angle.

Continuity, from the game's data: Bond's outfit changes because the setups
say so (Surface 1's intro cuff is 4, CUFF_SNOW - the parka; Bunker 1's is 3,
CUFF_BOILER - the special operations uniform; `assets/obseg/setup/
UsetupsevxZ.c`, `UsetupsevbunkerZ.c`, `src/game/bondview2.c`). And Bunker 1
has no single-player fog or sky row (`src/game/bgfog.c`, Surface's carries
its sky): above the shaft the game fills navy (0, 16, 64), and the remaster
shows navy panels behind a grate - not open sky.

### Bunker 1's props, characters and doors

`-Level bunker1 -Props` takes 35 models. 30 prepare - the 13 new to this
stage are the ammo crate, two card boxes, the first console of the Severnaya
bank (`console_sev2a`), two crates, the wooden crates, the swipe card, the
windowed double door (`sev_door4_wind`, a dynamic door), the PPK lying in the
level (`chrwppk`), the brown fur hat and the two berets - and every model
shared with earlier stages regenerates byte-identical. Five keep the game's
model:

- `tvscreen`, `tv4screen`: the source's items hold no model (1.8 KB). The
  game's screens stay the game's, as on every stage ("Monitors, screens").
- `window`: a pane; the game's own glazing.
- `mainframe2` (the server racks of the rock cavern, room 17): its draws give
  two answers - the front panel lays its image whole at 1/16384, a side
  panel at 1/8192 - and no rule decides between them (taken since #95 at
  1/8192: THE GAME'S REPEATS, Control round 2).
- `chrgoldeneyekey`: its body draws a 32-byte record (position, normal,
  coordinate, two frame words, colour) the static reader does not take, and
  read as that layout its material is the remaster's normal-mapped,
  sphere-mapped gold, which a prop does not carry: two draws would take a
  tangent-space normal map as their colour. Not taken.

The CCTV cameras are setup type 6, a kind the prop seam does not take; they
draw the game's model (docs/backlog.md B-BNK1).

**The arrow door.** `sev_door_v1`, the vertical door of the vent room and the
generator room, was refused: "the texture coordinates reach no atlas edge".
It is ONE draw: its two faces lay its 256x512 image whole (0 .. 16384 on both
axes) and its four side faces run the image's bottom strip 1.456 times along
their 600-unit length, so the draw passes its image at 1/16384 and stops at
0.728 of it at 1/32768. THE FACE (`xblachar.face_scale`), a last rule of
`texcoord_scale` asked only when every other one refused and only with the
item's triangles: the largest power of two at which more than half the
triangles lie inside the image, one reaching past 0.9, and one lies on all
four of its edges. The door reads at 1/16384 and rides the game's model
(scale 0.2857, the box -408.8 .. 467.2, 0 .. 2100), cut at the game's box like
any clipped door (D-027). The stages before re-prepared into a scratch root:
the Dam's chain-link door, Facility's glassware and Surface's jerry can and
stool stay refused, and every bundle they prepare is byte-identical (four
collectables the reference play machine's root does not hold yet -
Facility's swipe card, MP5K and door decoder, the Dam's TT33 - prepare as
they would at the build before; they come from the #80 collectable seam, not from this rule).

The light strip in the head of the vent room's doorways is the remaster's
own: an untextured quad of the item at the doorway's top, coloured 168 183
242 .. 193 204 255 - absent before the untextured rule above, present with or
without the door's item.

`-Level bunker1 -Characters` prepares the camouflage guard, Trevelyan's
guard and Boris (`camguard`, `trevguard`, `boris`); the olive guard, the
officer and the 25 random heads are the earlier stages' bundles. The player:
`gun/taser` is new; `char/boilerbond` regenerates byte-identical; Bunker 1's
head `headbrosnanboiler` is not in the source.

## Silo's background, props and characters (#82)

Silo (stage 20) draws the remaster's `background/silo` under XBLA through
the same paths as the stages before it: one entry in
`sl_wa_stage_item_path` (the source spells it `silo`), `SL_WA_PREVIEW=0`
keeping the game's rooms with the texture set's artwork.

```
.\tools\windows\prepare-xbla-assets.ps1 -Level silo -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level silo -Props
.\tools\windows\prepare-xbla-assets.ps1 -Level silo -Characters
.\tools\windows\prepare-xbla-assets.ps1 -Level silo -ViewModels
```

The item's scale is the rule above (the last `levelinfotable` column
29.069, so the room coordinates times 3.44). Its bounds reach 6422 item
units: the stage is wholly underground. The source has no `skydome/silo`
and the game draws Silo from its fogless table (no environment row), as
Bunker 1; nothing in it opens onto a sky, and no panorama shell is marked.
The reader keeps 98 of 99 draws (39122 triangles): one 4-vertex pool has no
texture coordinate and three draws name no image.

Measured at 24 poses (teleport, seed pinned, read 80: the top control area,
the four fuel rooms, the four silo shafts with their walkways and missiles,
the corridors between them and the rock tunnels): XBLA draws the remaster's
rooms everywhere on the game's floors and walls, with no misplaced surface
seen. ORIGINAL and COMMUNITY HD are untouched by the entry: at the 24 poses
both are byte-identical to the build before, 48 of 48 frames.

`-Level silo -Props` takes 22 models and prepares all of them; 15 are new
to the reference play machine's root - the three consoles, the tuning
console, the missile and its nose cone (`icbm`, `icbm_nose`), the satellite (`sat1_reflect`),
the security panel, the steel door, the lift and silo-top doors, and the
briefcase, data tape, circuit board and TT33 lying in the level. Two of
them needed the reader:

- **The missile's lettering.** The material of the missile's and the nose
  cone's "CCCP" strip names three images: the 85x324 lettering, a 256x256
  sphere image and a 256x128 sky image. `base_image` takes the largest, so
  the strip drew the sphere image - a grey smear down the missile.
  MEASURED over every prop of the source whose material names more than one
  image (37 records on 315 items): wherever a 256x128 image is named, the
  slot just before it names a 256x256 one, and they are the same two images
  on every item - the opaque grey sphere every reflective surface shares
  and its sky, the reflection laid over the draw. `xblachar.static_base`
  sets that pair aside and takes the largest of the rest; a material with
  nothing else keeps the largest, as before. The same rule colours the oil
  drum's lid with its own image (it tied the sphere image at 256x256 and
  lost on slot order) and the desk lamp with its own dark image - both
  shared with frozen stages, see docs/backlog.md B-SILO1.
- **The tuning console and the circuit board.** Their first material record
  binds no sampler and its draw follows it: "a draw before its image". The
  static reader now takes such a draw as UNTEXTURED - its vertex colour over
  the white image, the level reader's rule (#81) and the character reader's
  plain draws. Every prop that prepared before prepares as it did.

`-Level silo -Characters` prepares the Russian guard (`rusguard`); the
officer, the scientists and the random heads are the earlier stages'
bundles. The view models are all the earlier stages'.

### The first playtest (run 20261006-094148-lvl20, round silo2)

Four marks, all XBLA, each reproduced by teleport. Two reader defects in the
background's preparation, and two things that are the remaster's own:

- **The rails carried a landscape (marks 1 and 4).** The walkway rails' draw
  names three images: a tangent-space normal map (slot 0), a 128x128
  mountain landscape (slot 1) and a 512x256 grey metal (slot 2). Its program
  fetches slots 0 and 2 at the interpolated coordinate, builds the shading
  normal from slot 0, and fetches slot 1 at 0.5 N + 0.5 - a reflection added
  over the lit slot 2. The base rule passed over the normal map and took the
  next slot, the landscape. THE COMPUTED COORDINATE
  (`xblaworld.computed_fetches`, read in `read_level` over every program
  variant of the draw that can be placed): a slot the program samples at a
  coordinate one of its own instructions wrote is passed over for the next
  plain slot, when one remains and the draw has no material record. The
  rails draw the ribbed grey band the remaster shows (private emulator,
  debug camera at mark 1). One draw of the item moves.
- **The stairs and the walkway stairs were broken (marks 2 and 4).** Pool 5
  (2570 vertices, the cut-out frames, gratings, hatches and ladders of the
  stairs and walkways) was read at 1/2048: its quad decals run raw 0 .. 512
  exactly, 44% of its values are on 512, but 2048 won the enrichment score
  through 576 values of +-6144 (one draw's frames twelve images along), none
  at 2048 itself - so every image drew a quarter of itself. THE UNIT ITSELF
  (`_uv_format`): a candidate with no value at exactly +-d yields to the
  candidates that have one. Pool 5 reads at 1/512; one 4-vertex pool the
  reader refused before (a quad of the wall image at a wall corner) now
  reads too, so the item keeps 99 of 99 draws (39124 triangles). Steady
  under a yaw and pitch sweep at both marks.
- **The walls (mark 1) are the remaster's.** The shaft walls are one opaque
  draw of a 1024x1024 image of pipes and girders over black; the remaster
  shows the same lattice, lit cyan and red, and no solid guard wall under the
  rail - the cartridge's curved wall became an open railing.
- **The door stripes (mark 3) are as authored.** The wall's stripe is the
  background's black-and-white bar image tinted yellow by its vertex colour,
  as on Bunker 1; the door is `prop/steel_door1`, one draw of one 256x512
  image that paints its edge stripe black and white, every vertex colour
  white. Both left as they are.

The other stages' items re-prepared into a scratch root with both rules
are byte-identical to the build before (the Dam, Facility, Runway, Surface,
Bunker 1). ORIGINAL is byte-identical at the four marks.

### The game's tint on the steel door's bars (D-032, round silo4)

Decision on round silo3's finding: the door's bars take the game's
yellow. `tools/worldasset/gameshade.py` reads the game's own door model
from the ROM, finds the triangles it draws tinted (one non-white vertex
colour on a textured run - the bar image under e4e42d), places them on the
remastered item by the box relation, reads them through the remastered
triangles' coordinates into texel rectangles of the door image, moves each
inner edge to where the luminance rises into the painted bars (within 3
texels), and multiplies the texels inside by the tint. White bars become
the tint, black bars stay black, nothing outside the rectangles moves. It
runs in `convert_prop` for the items `DIVERGENCE_ITEMS['D-032']` names
(`steel_door1`, drawn by Silo alone) and needs the ROM, which
`prepare-xbla-assets.ps1 -Props` now passes. `SL_WA_AS_AUTHORED=D-032`
prepares the door as authored. docs/divergences.md D-032 has the
measurements.

### The game's teal on Silo's whitened rail-end caps (D-033, round silo4)

The background's untextured draw (drawn by vertex colour over the white
image) carries pure white on 15 of the silos' rail-end caps where the game
shades them teal. `gameshade.restore_background_shade`, in
`prepare_background` for the stages `DIVERGENCE_ITEMS['D-033']` names
(Silo alone), reads the game's rooms with their vertex colours from the ROM,
places them by the game's own scale (levelinfotable's last column, as
`sl_wa_source_scale` does at runtime - a fit fails on most stages, whose
remastered backgrounds reach past the rooms), finds each untextured
remastered triangle's game triangle, measures the stage's colour curve on
the surfaces the remaster kept (255 (g / 255) ^ 1.218 on Silo), and gives
each whitened triangle over a shaded game surface the game's shade through
that curve. A vertex another surface shares is never moved. The pools'
colours change before the vertex array is laid out, so nothing else in
the preparation sees a difference. `SL_WA_AS_AUTHORED=D-033` prepares the
background as authored; docs/divergences.md D-033 has the measurements and
what the rule would do on the frozen stages (nothing).

### Frigate's sky: the game's night over a cloud cover (D-034, round frg1)

Frigate loads the game's FOGLESS record (`fog_tables2`): no environment row,
so `sl_fog_environment_id` answers -1 and D-030's row-keyed table cannot name
it, yet the record carries a sky (navy 16 48 96), a cloud layer (230 230 230
at height 3000) and the sea sky.c draws below it. The remaster's
`skydome/frigate` is the daylight image every source dome carries, with
snowy mountains at its foot; its sea is the game's (`texture/bg/water1`,
`water2` are 32x32, as the game's). Two halves, both keyed on the item:

* preparation (`DIVERGENCE_ITEMS['D-023']` and `['D-034']` name `frigate`):
  the D-023 zenith cap is generated from the dome's band as for the Dam's,
  then every image of the dome (the dome and the cap) is written as its
  CLOUD COVER - grey, `clamp(1 - saturation / 0.35) * clamp(value / 200)`,
  alpha kept (`prepare.cloud_cover`). A blue texel of sky is no cover, a
  white or grey texel of cloud is;
* runtime (`sl_presentation_fogless_atmosphere`, `sl_wa_stage_game_atmosphere`):
  `sky_draw` shades the dome with D-030's combiner and `sl_wa_sky_shade`,
  the colours read live from the record the game loaded
  (`sl_wa_game_atmosphere`). The cover lays the cloud colour over the navy
  overhead and fades to the navy at the horizon, as sky.c lays its own
  intensity cloud image.

Nothing else of the stage moves: it stays fogless, its sea and backdrop are
the game's, and a stage with an environment row asks D-030 exactly as
before. Shading the authored daylight image instead left the overhead sky
pale blue over the night sea (measured at the round's mark 1), because the
combiner reads every bright texel as cloud. `SL_WA_AS_AUTHORED=D-034`
prepares the dome's images as authored; `SL_XBLA_GAME_ATMOSPHERE=0` draws
the prepared dome unshaded (a diagnostic: the cover is grey);
`SL_SKY_ZENITH=0` leaves the cap out.

### Frigate's deck paint: the game's decal alpha (D-037, round frg4b)

Frigate paints its deck markings, deck numbers and signs as blended decals
under an environment alpha its room lists set per group (0x66, 0x7f), and the
runtime combiner multiplies the image's coverage by it. The remaster's
translucent decal pass (style low byte 2) leaves every vertex alpha at 255,
and two of its images carry no partial coverage (the engine-room arrows are a
cut shape, the checker an opaque black and white image), so they drew as
solid mats. The preparation (`gameshade.decal_opacity`, `read_bg_walk` for
the environment alpha) scales the vertex alpha of every such decal triangle
lying on a blended game decal by the game's alpha, on copied corners. The
images, colours and coordinates stay the remaster's. `SL_WA_AS_AUTHORED=D-037`
prepares the background as authored.

Round frg5 widened the population to the remaster's whole decal pass. The
yellow dashed arcs and circles on the decks (image 61) are an OPAQUE decal
draw (style `0x2b01`) whose vertex alpha nothing reads, and the hazard
stripes (image 69) and a red wall arrow (image 0) are opaque too, while the
game blends all of them under 0x66 / 0x7f / 0x75. Two guards keep the rule to
the placements that redraw a game decal: a placement (triangles joined by
shared corners) takes the alpha whole or not at all, and only when its
in-plane extent and that of the game triangles under it overlap by an
intersection over union of at least 0.25 (`DECAL_EXTENT_IOU`; measured 0.44
.. 0.99 on the redrawn markings, 0.05 and below on a remastered vent and on
20-unit studs lying on the game's wall-sized 0x21a grain overlay, which the
first version dimmed). An opaque draw's admitted triangles move to a
translucent copy of the draw placed straight after it. Frigate: 12 draws,
433 triangles.

### Frigate's sea: Sightline's own (D-039, round frg5)

The game's sky.c sea under the xbla sky meets it a band darker than the sky
(the water image multiplies the horizon navy), and its 32x32 image shimmers in
the distance; the remaster's sea is one constant navy. Under the xbla set the
sea triangles are drawn by a program in `sl_gfx_dl.c` (the sea program, used
from `draw_rdp_tri`): each pixel's texture coordinate is run back into its
point on the sea plane and, with the game's eye (`sl_wa_sea_frame`,
src/native), into an exact view ray; the image's detail in three drifting
layers that fade below a pixel, under the game's shade, the sky's reflection
by Schlick's Fresnel term and the sky's horizon colour within six degrees of
the horizon. `sl_presentation_own_sea` names the stage (xbla, frigate);
`SL_XBLA_SEA=0` draws the game's sea, `1` the layers and horizon alone, `9` a
diagnostic (distance, depression and world x as colour). The triangles, plane
and render mode are the game's, so the hull, D-035's waterline and the boat
are unchanged. docs/divergences.md D-039.

## Surface 2's background, sky and atmosphere (#84)

The game builds Surface 2 (stage 43) from Surface's background file
(`bg_sevx_all_p`, the same scale column in `levelinfotable`), and the source
carries one background item for the pair - `background/surface`, no
`background/surface2` (the preparation refuses `-Level surface2 -Background`)
- but a dome of its own, `skydome/surface2`, the same daylight image as
Surface's (the same two texture ids). So `sl_wa_stage_item_path` answers
`surface` on Surface 2, and `sl_wa_stage_sky_item_path` answers `surface2`
there and the background item everywhere else; `sky_frame_begin` asks for the
dome by the second unless D-031 borrows one. SL_WA_PREVIEW=0 clears both.
Everything keyed by the background item (the D-026 shell marked in
`background/surface`, the native fill) applies to Surface 2 as to Surface.

Before the entry the stage resolved no item: no lookup, no draw, the game's
rooms with the texture set's artwork under the game's own sky (round srf2a,
playtest run 20261006-230603-lvl43, all eight marks).

The game plays the stage at night: environment row 43, fog 32 16 16, a cloud
layer coloured 58 17 0, BlendMultiplier 2 and FarFog 2000 at the visibility
scale 0.2 with the intensities 957 / 1000, which put the far-fog prop cut at
zDepth 7662.5 (SL_TABLE_DBG). The remaster's row 43 keeps the decomp's colour
and the end 10000; its 943 (the intro cinema) the colour and 40000. Three
mechanisms already accepted for Surface take the stage, keyed on its own rows
and item (docs/divergences.md D-040):

* D-029's presented fog end, row 43 at 7662: under 10000 the satellite dish
  (`prop/sevdish`, a prop - `SL_PROP_ASSETS=0` swaps it for the game's model)
  was removed by the game's cut while the replacement around it was still a
  quarter clear, so it appeared whole when crossing 7662;
* D-030's game atmosphere, rows 43 and 943: the dome shaded as the game's
  cloud layer with the game's live colours;
* D-023's zenith cap for `skydome/surface2`.

The end at the cut is not enough for a large prop. The cut tests the prop's
origin and the row fogs each vertex by its own distance, so when the dish's
origin crossed 7662 its nearer faces (6943..7370 units, fog 0.93) appeared at
once, while the building under it - the background, never cut - had been a
silhouette since well before (round srf2b). D-041 draws the props on row 43
past the game's cut (`chrobjRenderProp` asks `sl_gfx_prop_fog_drawn`; the
policy is `sl_presentation_prop_fog_drawn`), fogged by the row as the
background is, so they come out of the fog with the world. Past the end a
prop is the fog colour, and above the horizon the sky behind it is the
game's cloud shade, so from far back the dish is a dark silhouette - as any
background shape standing there is; the game's far plane slices it in
gradually rather than a cut removing it whole, so the draw is not bounded
short of that plane. Characters and monitors keep the cut.
`SL_XBLA_FOGGED_PROPS=0` keeps the cut for props too.

The remaster (private emulator, debug camera, round srf2a) shows the stage the
same way round: the snow bright, the trees in light fog, the sky a near-black
night.

## Bunker 2's background, props and characters (#85)

The game builds Bunker 2 (stage 27) from a background file of its own
(`bg_sevb_all_p`; Bunker 1 loads `bg_sev_all_p`), with Bunker 1's scale column
(23.148148, `levelinfotable`), and the source carries `background/bunker2`
and `skydome/bunker2` beside Bunker 1's `bunker` items. So
`sl_wa_stage_item_path` answers `bunker2` there; SL_WA_PREVIEW=0 clears it.
Before the entry the stage resolved no item, and none of its own characters
or props had been prepared: XBLA drew the game's rooms with the texture set's
artwork, and Natalya, the documents, the tape, the ceiling guns, the server
racks and the cabinets as the game's models (round bk2a, playtest run
20261007-061839-lvl27, all nine marks).

With the entry and the stage prepared (`-Level bunker2 -Background -Skydome
-Props -Characters -ViewModels`) every earlier reader rule applies unchanged.
What the marks showed:

* The corridor walls are the remaster's. The dashed yellow band along them
  is the remaster's own decal mesh, not the game's geometry: hiding the
  wall meshes leaves it, and the native fill switched off leaves the frame
  identical.
* The control room's light pillars are Bunker 1's case: a translucent glow
  sheet lies in the pillar's plane and yields to it (`SL_BG_XLU_YIELD`), so
  the lamp bar shows alone; with the yield off the glow returns as hard white
  blobs on the pillar.
* The cabinets (`kit_units1`), the GoldenEye console (`console_sev_gea`),
  the documents (`chrdossierred`, `chrstafflist`, `chrclipboard`) and the
  tape (`chrvideotape`) are rigid props and draw as the remaster's once
  prepared; Natalya (`char/natalya`, her own head) rides the game's skeleton
  as every character does.
* The ceiling gun (`prop/roofgun`, an autogun) needed two rules, below.
* The server racks of the rock room (`prop/mainframe2`) stayed the game's
  (the backlog's B-BNK2A) until #95 read them at 1/8192 (THE GAME'S REPEATS,
  Control round 2) and took them.

The environment row (27) carries fog 16 0 0 under a cloud layer coloured
58 17 0. The stage is enclosed; the dome draws behind the item and shows only
where the item is open, and no mark showed it.

### The ceiling gun

The PREPARATION. `xblachar.read_character` measured a skinned item's scale
without its triangles, so the last rule of `texcoord_scale` (THE FACE, #81)
was never asked of one. The remaster's ceiling gun lays three of its seven
images whole (0 .. 16384 on both axes) and tiles two along the barrel to 1.6
repeats, so no draw-level rule found a scale and the item was refused. The
triangles now go with the measured draws, as `read_static`'s do, and the face
rule reads it at 1/16384. The rule is asked only where every other rule
refused; re-prepared into a scratch root, every character, head and view
model of the accepted stages is byte-identical.

The RUNTIME. `sl_prop_derive` refused the item whole: `SKEL_CARTRIDGE` sits
exactly on `SKEL_XROT`'s pivot (0, -23.96, 0) while the model's node for
matrix 4 lies 242 item units away (tolerance 92). A joint that sits on its
parent's pivot says nothing about where its part turns, so where the pivot
test would refuse it, it rides its parent's matrix with its parent's bind
(`SL_PROP_SAME_PIVOT`); a joint the test accepts is mapped as before. The
game never turns the autogun's matrix 4 apart from matrix 2 (the autogun
branch of the object render sets it to identity and its node's position under
matrix 2), so the part lands exactly where matrix 4 would put it. Runway's
gun (`gun_runway1`, whose cartridge joint passes the test), Surface 2's dish
and helicopter and Frigate's helicopter map as before.

### The decal pass on itself, and the pull (round bk2b)

Bunker 2's floors are themselves decal-pass draws: mesh 69, translucent, in
the source's decal group, with nothing beneath it (hidden, the frame shows the
clear colour). Two things followed from rules that assumed a decal always lies
on a surface outside the pass.

* **The cell stains flickered** (playtest marks 1 and 2). The stain (mesh 61,
  a cutout decal, y = 0) and the floor under it (mesh 69, y = 0, listed later)
  are 0.0000 item units apart. The order rule (#77) left every decal-pass
  triangle out, and both carry the same offset, so the floor covered the stain
  in some frames and not others (13 consecutive frames at mark 1: whole,
  partial, absent); with the floor hidden the stain is steady. Two decal-pass
  surfaces in one plane now pair under the order rule, the later yielding (the
  remaster's LESS); a decal and a surface outside the pass still never pair.
  LESS alone did not hold - the two rasterisations differ by a step or two of
  depth that moves with the view - so a yielding decal triangle draws without
  the decal offset, two steps behind the earlier decal. Mark 1: steady over
  13 frames; mark 2's grate through the bars likewise (mesh 60 on mesh 69).
* **The floor bled through the walls** as a thin line at eye height (mark 3,
  the top of a staircase). The pull (#81) is measured in the plane's normal,
  so for an eye 10 item units from a plane it takes the plane a fifth nearer
  on every sight line; the upper floor (mesh 69 at y = 400, the eye at 389.8)
  lies behind the walls on those lines and won against them. `SL_BG_DECAL_PULL
  =0`, `SL_BG_HIDE=69` and `SL_BG_DECAL=0` each remove the line. The pull
  exists for a decal standing a little behind its surface; that floor has
  nothing in front of it. Only a decal triangle with a surface outside the
  pass, that writes depth, parallel, overlapping, and off its plane by more
  than the coplanar tolerance and at most the pull, is pulled
  (`sl_wa_decal_needs_pull`); the rest keep the offset alone
  (`SL_BG_DECAL_NEED=0` pulls all). Bunker 2 pulls 14 of 1722 decal
  triangles; Bunker 1 10 of 302, the ten its beams were measured at.

Witnessed against the build of the playtest: ORIGINAL and COMMUNITY HD
byte-identical at all three marks. Start frames under XBLA: Facility, Runway,
Bunker 1 and Silo byte-identical; the Dam 160 pixels (single-pixel edges along
the walkway rails), Surface 230 and Surface 2 223 (snow, at most 12 levels),
Frigate 4.

### Thresholds, the helipad's rim, the offset's slope and the emblem (round bk2c)

Playtest run 20261007-085655-lvl27 (build 96D8525B), four marks under XBLA.

* **The floor under a doorway and round the helipad was missing** (marks 1
  and 4, one cause). Both showed the clear colour (centre pixel 10 00 00 at
  depth 1.0: row 27's fog colour, nothing drawn). The source carries both as
  32-byte two-layer pools that the reader refused for want of a scale: pool
  15 (4 vertices, the doorway quad exactly, item x -3266.7 .. -2616.7,
  z 10933.3 .. 11050, y 0) and pool 9 (56 vertices, the rim of the pad,
  y 850). Too few of their values land on any divisor, and the item's scale
  (#77) has no answer here: its pools measure 1/64 to 1/512. Every vertex
  program of the item that reads a 32-byte record carries the one coordinate
  scale 1/256 in its literal bank, the scale its other two-layer pool (pool 1)
  measures, so a two-layer pool takes the scale the item's two-layer pools
  agree on when its pools disagree (`layout_scale`). Read, neither draw is one
  the two-layer test confirms: no program variant of the rim's draw can be
  placed, and the threshold's one placed variant samples slot 0 alone, at the
  layer's coordinate. All 42 draws the test does confirm, over the Dam,
  Facility, Runway, Surface, Silo, Frigate and Bunker 2, assign the layout
  the same way - base slot 1 at +20 with the colour at +28, layer slot 0 at
  +16 with the colour and weight at +24 - so a draw on a two-layer pool that
  names slots 0 and 1 and whose programs do not contradict that assignment is
  carried by it (`layout_layers`), its weight taking the layer image's alpha
  where the image has one. The threshold reads as the yellow hazard stripes
  over the corridor's floor image (the original draws yellow and black
  stripes there), the rim as a cracked concrete base under snow drifts.
  Only Bunker 2 moves: the pools and the two-layer draws of the other prepared
  items read as before (the Dam's and Runway's two-image terrain samples both
  slots at one coordinate and is left as it was), and Bunker 1's identical rim
  (pool 13) stays refused - its item has no measured 32-byte pool to agree
  with. The door at mark 4 is locked at the start of the stage; it was opened
  for the witness with the game's own `doorActivate`, as an AI list does.
* **The next room's floor still showed through a corridor's far wall** as a
  thin line at eye height (mark 2, the eye 389.8 item units up, the floor at
  y = 400 behind the wall). Not the pull this time - `SL_BG_DECAL_PULL=0`
  leaves the line - but the decal offset's slope term: a plane seen edge-on
  changes depth steeply across its one row of pixels, and the term takes it
  that much nearer. Measured with the offset's two terms apart in a scratch
  build: the units alone (0, -2) leave no line, the slope alone (-1, 0) draws
  it. The offset exists for a decal drawn on another surface; mesh 69 is the
  floor. Only a decal triangle that lies on another mesh's surface - one
  outside the pass that writes depth, within the pull of its plane (a tie
  included), or another decal-pass surface within the coplanar tolerance (the
  pairs of round bk2b) - takes the offset (`sl_wa_decal_needs_offset`); the
  rest draw without it (`SL_BG_DECAL_LONE=0` offsets all). Decal triangles
  lying on nothing: Bunker 2 923 of 1754, the Dam 7 of 425, Facility 13 of
  311, Runway and Surface none, Bunker 1 14 of 302, Silo 32 of 1228, Frigate
  61 of 1181.
* **The emblem's dome is dark, as the source draws it** (mark 3; no change).
  Bunker 1's dome at the same place (pool 15, 223 vertices) names the blue
  chrome bubble, the room's fisheye and a flare image, and is sphere-mapped by
  its normals. Bunker 2's is other art: pool 16 (55 vertices, 90 triangles),
  one draw in the decal pass naming a 54x54 intensity image - a dark grey
  fisheye of the room - in both slots, under vertex colours of 56 .. 82. No
  blue is in its data. The remaster's own frame at the mark (private
  emulator, debug camera) shows the dome dark with a faint grey reflection.

Witnessed against the build of the playtest at the marks: ORIGINAL and
COMMUNITY HD byte-identical at all four. Under XBLA the threshold holds over an
11-frame turn; the line at mark 2 is in all 22 frames of a turn and a walk
toward it before, and in none after (the frames differ only in that one-pixel
row); mark 3 differs in 5 pixels. Start frames under XBLA of the Dam,
Facility, Runway, Surface, Bunker 1, Silo, Frigate and Surface 2:
byte-identical. Re-prepared with these tools, the Runway, Bunker 1, Silo and
Frigate backgrounds are byte-identical to the bundles in use; the Dam,
Facility and Surface prepare the same bytes with the tools before and after
this round (B-BNK2C).

### The helipad's rim is drawn as the remaster draws it (round bk2d)

Playtest run 20261007-102158-lvl27 (build 49F38EA8), one mark: the rim round
the helipad shows hard-edged white wedges and dark cracked patches, where the
original has a soft, round snow border running into the snowfield.

* **The remaster draws the rim the same way.** Its own frame from the mark's
  pose and from above the pad (private emulator, debug camera) shows the same
  wedges and patches, quad for quad. The rim's draw there is a quad list of
  56 indices whose vertex program fetches +16 and +20 as 16-bit pairs (the
  same `.yx` destination order as every 24-byte pool's coordinate), scales
  both by one constant and passes +24 and +28 on as colours. Its pixel
  program samples slot 1 (the 1024x1024 cracked concrete) at +20 and slot 0
  (the 256x256 snow image with an alpha edge) at +16, and blends
  `lerp(base x +28 x light, layer x +24 x light, layer.a x (+24).a)`. That
  is `LAYER_CONTRACT`, so the assignment `layout_layers` gave the rim
  (INFERRED in round bk2c) is the draw's own.
* **Where the wedges come from: the source's charts.** Each of the rim's 14
  quads maps the whole snow image once, the first coordinate across the rim
  (outer edge 0, inner edge 256) and the second along it. The image's alpha
  edge runs along its rows (opaque rows 0 .. about 140, clear below), so it
  cuts every quad across the rim rather than along it: half of each quad is
  snow, half concrete, with a straight edge between them. The rim's inner
  vertices stand on the pad's vertices (same position, the pad's image and
  +20 coordinate exactly), and its outer ones on the snowfield's; 28 of 56
  each.
* **No change to the reader.** The coordinate scale, the slot assignment,
  which word feeds which layer, the weight and the geometry all match the
  remaster's draw. A look like the original's is a presentation change, not
  a reading fix (B-BNK2D). Measured in a scratch bundle: transposing the
  layer's coordinate alone leaves angular wedges; taking the layer's weight
  to zero at the vertices that stand on the base image's surface (the pad)
  gives a round snow border fading into the concrete, with or without the
  transposition.

Bunker 1's rim (its pool 13) stays refused and unchanged.

### The helipad's rim fades into the pad (round bk2e, D-042)

The rim is finished as a documented divergence (D-042, `docs/divergences.md`):
the preparation's `band_fade` (tools/worldasset/prepare.py), enabled for
Bunker 2's background alone by `DIVERGENCE_ITEMS['D-042']`.

* **Which draws.** A two-layer draw whose layer is SHAPED (weighted by its
  image's alpha, and the image carries alpha) and is a BAND onto its base
  image's own surface: every triangle has a corner on that surface and one
  off it. A corner is on the surface when a corner of another draw drawn
  with the base image stands within 0.01 item units (`BAND_EPS`) and carries
  the same base coordinate (within `BAND_COORD_EPS`). On Bunker 2 that is
  the rim alone: 28 of its 56 corners stand on the pad's (0 to 0.00025
  units; float rounding leaves 16 of them a bit or two off exactly), every
  other corner 850 units or more away.
* **The fade.** A corner on the surface takes layer weight 0 (the vertex
  alpha of the layer's colour word), so the snow thins across each quad
  from the snowfield edge to the pad.
* **The turn.** The weight's unit gradient in the layer's coordinate,
  summed over the draw's triangles, runs along u (25.5 against 6.6); the
  snow image's alpha runs along v (the spread of its row means 116.3
  against 3.5 for its column means). The axes differ, so the image's edge
  would cut across the fade: the layer's two coordinates are swapped, which
  lays the snow's opaque rows on the snowfield side. A turn that would face
  the opaque half onto the base is not taken, and an image already running
  along the fade is not turned. The turn is decided for the draw as a
  whole: 13 of the 14 quads chart u across the rim (per triangle u : v
  3 : 1 to 27 : 1), and one - corners 40-43, on the west side - charts it
  the other way round, so there the turned snow edge runs along the rim;
  the fade still takes it to nothing at the pad.
* **The bundle.** `background/bunker2` prepares to D4B6D142, byte for byte
  the fade-and-transposition preview of round bk2d. The base copy of the
  draw and every other draw are untouched; the layer's copy of pool 9 is
  appended under its own key. `SL_WA_AS_AUTHORED=D-042` prepares F660EAEB,
  the bundle before.
* **Other stages.** Forced on for every prepared background in a scratch
  preparation, the rule changes nothing else: no other shaped layer is a
  band (Silo's three and Frigate's one shaped layers touch their base's
  vertices at a few corners, never with the base's coordinate), and Bunker
  1 carries no two-layer draw at all (its rim, pool 13, stays refused). The
  Dam, Facility, Runway, Surface, Bunker 1, Silo and Frigate backgrounds
  prepare the same bytes with the tools before and after.

## Statue's background, props and characters (#86)

The game builds Statue (stage 22, Statue Park) from `bg_stat_all_p`, scale
column 0.0801 (`levelinfotable`), and the source spells the stage its own
way: its listing carries `background/statuepark` (and `statuepark_hits`) and
nothing named `statue`, and no skydome item for the stage. So
`sl_wa_stage_item_path` answers `statuepark` there; SL_WA_PREVIEW=0 clears
it, and `sl_wa_stage_sky_item_path` names a dome no root holds, so the game's
own sky draws. Before the entry the stage resolved no item: XBLA drew the
game's own park with the texture set's artwork (round sta1, 31 teleport
poses).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level statuepark -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level statue -Props -Characters -ViewModels
```

* **The item's scale.** The column is far from every other stage's (100 /
  0.0801 = 1248.44 item units per room unit), and the item reads at it: the
  item's extents over the game's rooms are 1248.6, 1246.8 and 1248.1 on the
  three axes, and a least-squares fit over the 3113 room corners that land on
  an item vertex gives 1248.42. The item reaches past three million units
  (x -3121149 .. 271247), inside its own declared bounds.
* **The environment.** Row 22 is a night row: fog 0 0 8 from 2000 to 3500,
  a cloud layer coloured 170 100 40. The remaster's row is the same (start
  -100, end 3500, 0 0 8), so nothing of D-025 / D-029 / D-030 applies: the
  game's far-fog cut stops props at view depth 3476.3 (`SL_TABLE_DBG`), where
  the presented fog is 99% of the way to its colour, and with no dome in the
  source the sky is the game's own cloud layer. The item carries no panorama
  shell.
* **The park.** The remaster's paths, lawns, stones, fences, the containers,
  the toppled statues and the hand (the cartridge's open hand is a red fist
  there), the string lights over the paths (two untextured draws, 5402
  triangles, wires and bulbs by their colour) and its light pools draw at
  every pose. The remaster lights the park far darker than the cartridge's
  vertex colours, and its fog is linear from -100, so distant ground and the
  far side of the gate pillars read near black; that is the item's own
  shading.
* **The fog starts at 800 (D-043, round sta2).** Measured over the 31 poses
  (mean frame luma, read 80): ORIGINAL 22.95, XBLA as authored 19.23, XBLA
  with the stage fog off 24.90, ORIGINAL with its own fog off within 7% of
  ORIGINAL (the game fogs only its far band). The remaster's row fogs from
  the eye toward 0 0 8 and is the larger part of the difference; the baked
  vertex colour (area mean 61 of 255 over the item, against 211 at the Dam)
  is the rest, and is left as the remaster bakes it. Under the xbla set the
  row is presented from 800 instead of -100 (`sl_presentation_fog_start`),
  colour and end kept: 22.64 over the same poses, between the two
  presentations, with props and characters fogged by the same row and a
  prop at the cut still 99% fogged. `SL_XBLA_FOG_START=0` (or
  `SL_XBLA_AUTHENTIC_FOG=1`) draws the authored start.
* **The park gate** (`prop/doorstatgate`, a rigid door) is authored in the
  background's space, at the gate's place in the park (x -2600800 ..
  -2554810), as is the game's own `PdoorstatgateZ` (its box -91028 ..
  -89418): every corner is past the static reader's 1e6 bound, which
  refused it as "not a number". A static item's position now reads when it
  lies inside the box the item declares for itself (`xblachar.position_ok`,
  `declared_box`, widened by 1% of the largest extent); an item whose
  positions are under 1e6 reads as before. The gate rides the game's model
  (scale 28.5632) and stays shut and moves as the game moves it.
* **The containers' galvanised panel.** Pool 4 (16 vertices: a container's
  wall, roof and floor and one path quad) measured 1/4096 from 2 of its 28
  values, against 7 on 256, so its panel image repeated every 1331 world
  units there while pool 1 draws the same image every 95 and 202: the
  container Valentin waits in showed two flat bands, light over dark, inside.
  THE IMAGE'S OWN REPEAT (`xblaworld.image_repeat`): a pool drawing every
  image it shares with the item's other pools four or more times off their
  repeat, all the same way, is read again at the candidate divisor (one
  holding at least 1% of its values) that brings each within 1.5 of the
  others. Pool 4 reads at 1/256 (83 units a repeat); the remaster (private
  emulator, debug camera) shows the panel's corrugations banding the walls
  and roof. The Dam, Facility, Runway, Surface, Bunker 1, Silo, Frigate and
  Bunker 2 backgrounds prepare the same bytes with the tools before and
  after.
* **Characters and pickups.** The guards (`char/rusguard` with the helmet
  `prop/hathelmet` and the rifle `prop/chrkalash` in hand), Valentin
  (`char/valentin`, his own head) and the body armour (`prop/bodyarmour`)
  draw as the remaster's. Trevelyan, the Janus guards (`trevguard`,
  `greatguard`), Mishkin's head and Natalya are prepared and come on stage
  later in the mission. The helicopter (`prop/tiger`, prepared for Surface 2)
  stands in its compound beside the flight recorder.
* **What is left** (the backlog's B-STA1): the flight recorder stays the
  game's (`prop/chrblackbox`, no rule of `texcoord_scale` decides it), and
  the cobbled paths round the helicopter compound (pools 2 and 3, measured at
  1/64) draw the cobble image as thin stripes.

## Archives' background, props and characters (#89)

The game builds Archives (stage 24) from `bg_arch_all_p`, scale column
54.347824 (`levelinfotable`), and the source carries the stage's background
as `background/archives` (and `archives_hits`) with no skydome item: an
interior, whose courtyard and street keep the game's own cloud layer. Before
the entry the stage resolved no item, and XBLA drew the game's own rooms
with the texture set's artwork - at 33 teleport poses the XBLA frames held
the ORIGINAL world, with the remaster's characters and the props prepared
for earlier stages on top (round arc1).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level archives -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level archives -Props -Characters -ViewModels
```

* **The item.** 121 draws over ten pools (11471 triangles, 75 images, all
  DXT: no 8888 image, so the decoder's red-blue swap of B-FRG4 does not
  reach it), read at 1.84 item units per room unit; its declared and
  computed bounds agree. Three draws carry two layers, 842 triangles take
  the source's decal pass, and no panorama shell is present.
* **The environment.** Row 24 is the game's: black, far 3000. The
  remaster's row keeps the colour and the end and starts at -100. Over the
  33 poses (mean frame luma, read 80): ORIGINAL 45.62, XBLA 41.92, XBLA with
  the stage fog off 50.35. The remaster (private emulator, debug camera)
  draws the attic darker than XBLA does, and the library at the same tone,
  so no presentation fog row is added: the row is drawn as authored.
* **The steel door.** `prop/steeldoor3` (thirty doors) is one draw whose
  front and back faces each lay its 512x1024 image whole at 1/16384 as a
  grid of eight quads, with two edge strips running 0.073 of the image past
  either side; no rule of `texcoord_scale` decided it and the doors drew as
  the game's. THE FACE now measures a face as its PLANE where no single
  triangle spans the image (`xblachar.face_scale`): the inside triangles of
  one plane, taken together, on all four edges. The door reads at 1/16384,
  its border rivets on the face's edges, rides the game's model (scale
  0.2857) and swings with it when used. The source's other refused props
  stay refused, and every stage's props, characters and view models prepare
  the same bytes.
* **The rooms.** The cells and interrogation rooms, the corridors, the
  archive stacks, the offices, the stairs, the library and its galleries,
  the attic, the courtyard (the remaster builds a walled yard with barbed
  wire where the game shows a void past its wall) and the street draw as the
  remaster's at every pose. The crates (`prop/card_box1`) and the library's
  two secret doors (`prop/archsecdoor1`, `archsecdoor2`) are new; the
  windows stay the game's glazing; the guards (`char/oliveguard`,
  `greatguard`), Natalya and the stage's heads were prepared for earlier
  stages.
* **The mission.** Driven by teleport only (a scratch sequence of the
  game's own place-Bond primitive; no input replay): room 22 completes
  "escape from interrogation room", standing by Natalya in room 52
  completes "find natalya" (the game's AI sees Bond: the run withholds the
  invisibility cheat), and room 64 completes "escape with natalya"; the
  exit cinematic plays and the game returns to the title.
* **What is left** (the backlog's B-ARC1): the street face of the building
  is open between its baseboard and cornice in the source itself, and the
  bookshelves show a quarter of each row's books.

### Stacked decals, the fan's shadow and the shot-out window (round arc2)

Playtest run 20261008-182214-lvl24 (build 4BDE863A), three marks under XBLA.

* **The blood pools flickered** (mark 1) **and so did the ceiling fan's
  shadow** (mark 2), one cause. Neither is the game's: the mark record holds
  no game triangle at mark 1, and the pool is there before a shot is fired.
  Each is two decals of the item stacked on an opaque floor outside the
  decal pass, all three in one plane (0.0000 item units apart): the pool is
  meshes 60 (a shaped overlay given the pass, #78) and 61, both the same
  blood image; the shadow is meshes 86 and 88, the fan's own blade and
  housing images under a black vertex colour. The pair rule of round bk2b
  makes the later decal of each pair yield, and a yielding decal drew
  without the offset - at the floor's depth, where the two resolve per pixel
  by rasterisation noise: the overlapping part of the later decal came and
  went frame to frame. `SL_BG_HIDE=61` (and `=88`) leaves the rest steady;
  `SL_BG_HIDE=60` shows mesh 61's yielding part striped across the floor.
  Bunker 2's yielding floor lay on nothing else, which is why dropping the
  offset was exact there. A yielding decal that lies on a surface outside the
  pass now keeps the offset under LESS, and the earlier decal it yields to (a
  depth-writing one; a translucent decal holds no pixel) draws with twice it
  (`sl_wa_decal_layers`; `SL_BG_DECAL_LAYER=0` drops the offset as before).
  A triangle repeating an earlier decal triangle corner for corner is a
  two-layer draw's layer over its own base - one draw of the source - and is
  left as it was: without that, Surface's fence shadow (meshes 64 and 65)
  moved. Archives: 12 triangles keep the offset, 5 take twice it (both pools
  and the shadow's ring); no other prepared stage has one. Over 13
  consecutive frames and a yaw sweep the pools and the ring are whole in
  every frame. The shadow's look - a hard black silhouette of the fan and its
  ring over a lit disc, where the original draws a soft light pool - is the
  source's art: its images carry alpha 0 or 255 only, and the remaster's own
  frame (private emulator, debug camera) shows the same black ring and
  blades.
* **Bullet holes stay in the air after a window is shot out** (mark 3; no
  change). The panes are the game's glazing. Fired at from the mark, the
  pane breaks on the fourth pistol shot under ORIGINAL and XBLA alike, and
  both keep the impacts: a mark record taken after the break holds the
  impact triangles in the plane of the facade's opening (539 .. 541 units
  from the eye) while the pixel behind them is the far facade at 637, and
  shots fired after the break land on that plane too. It is the game's own
  behaviour and is kept; changing it would be a toggle defaulting to the
  original.

Witnessed against the build of the playtest: ORIGINAL and COMMUNITY HD
byte-identical at all three marks, ORIGINAL (and XBLA) also over the whole
shot-out sequence. Start
frames under XBLA of the Dam, Facility, Runway, Surface, Bunker 1, Silo,
Frigate, Surface 2, Bunker 2, the Statue Park and Archives: byte-identical;
the stages other than Archives mark no triangle, so they draw exactly as
before, and Surface's fence shadow is byte-identical over six frames.

## Streets' background, props and characters (#90)

The game builds Streets (stage 29) from `bg_pete_all_p`, scale column
42.372883 (`levelinfotable`), and the source carries the stage's background
as `background/streets` (and `streets_hits`) with no skydome item. Before the
entry the stage resolved no item, and XBLA drew the game's own streets: at
40 on-floor teleport poses the XBLA frames held the ORIGINAL world (mean
frame luma 42.67 against 42.66), with the remaster's tank and guards, which
earlier stages had prepared, on top (round str1).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level streets -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level streets -Props -Characters -ViewModels
```

* **The item.** 181 draws over eight pools (14835 triangles, 80 images, all
  DXT: no 8888 image, so the decoder's red-blue swap of B-FRG4 does not reach
  it or the stage's props), read at 2.36 item units per room unit: the item's
  extent over the game's room corners is 2.35997 on x and 2.35998 on z (y
  2.58, the remaster's rooftops being taller). Declared and computed bounds
  agree; 1237 triangles take the source's decal pass, two draws carry two
  layers, and no panorama shell is present.
* **The environment.** Row 29 is a dusk: fog 16 24 32 from 5000 to 7500, a
  cloud layer coloured 225 175 100. The remaster's row keeps the colour and
  the end and starts at -100. Over the 40 poses (mean frame luma, read 80):
  ORIGINAL 42.66, XBLA 40.19, XBLA with the stage fog off 44.03 - a smaller
  step than Archives' (no presentation row there either), so the row is drawn
  as authored. The remaster (private emulator) draws no cloud layer over the
  streets, a plain navy sky. Round str1 left the game's own cloud layer
  drawing there; the sky is now a lent dome (below).
* **The streets.** The alleys, the streets and their intersections, the
  facades and their windows, the kerbs (the remaster paints them yellow), the
  cracked asphalt, the barricades, the parked cars and the interiors of
  rooms 12 and 13 draw as the remaster's at every on-floor pose; the remaster
  builds a park with trees and railings behind the tank's square where the
  game shows the sky. Thirteen frames at two poses change in under 170 pixels
  a frame, along edges as the camera settles: no surface comes and goes.
* **The props and characters.** Of the setup's sixteen models, sixteen are
  prepared: the tank, the jeeps, the two kinds of parked car (`cargolf`; the
  remaster's `carweird` is a rusted wreck where the game's is a polished
  orange car), the barricades, the landmines, the grenades, the ammo crate,
  the body armour, the rifles and the five rooms the game places inside its
  building blocks (`st_pete_room_*`, the remaster's `pstpeteroom*`). The
  guards (`char/oliveguard`, and `char/redman`, new, for the guards the AI
  spawns), Valentin and the 25 random heads were prepared for earlier stages
  or here.
* **The tank.** Driven through the game's own action - Bond walks onto the
  tank and uses it once - the tank moves, its turret turns and it crushes a
  jeep under ORIGINAL and XBLA alike, and the remaster's turret and gun stand
  where the game's do in every frame, seen from the hatch (`prop/tank`, the
  moving prop of Runway's #78). The jeep and the 5-minute countdown behave the
  same in both.
* **The mission.** Driven by teleport only: standing at the exit (room 55)
  completes "pursue ourumov and natalya" and "minimize civilian casualties",
  the exit cinematic plays over the remaster's street and the game returns to
  the front end.
* **What was left** (the backlog's B-STR1): the untextured backdrop at the
  end of the start alley (mesh 70, 76 triangles) drew about three times
  brighter than the remaster draws it - answered in round str3, below.

### Streets' sky, jeep and landmines (round str2)

* **The sky (D-044).** The source holds no `skydome/streets`, and every
  other outdoor stage of the set draws a source dome under the game's
  atmosphere, so the presentation lends one (`sl_presentation_sky_lent`:
  `streets` -> `frigate`, asked by `sky_frame_begin` after D-031's borrow).
  The dome keeps the stage's own atmosphere: `s_game_atmosphere` names row 29,
  so `sky_draw` shades it with the live fog (16 24 32) and cloud (225 175 100)
  colours, and its D-023 cap rides the stage's own cloud plane and phase. The
  dome lent is Frigate's because its images are prepared as cloud cover
  (D-034): Surface 2's, prepared as authored, drew a flat pale overcast under
  the bright cloud colour. `SL_XBLA_LENT_SKY=0` draws the game's cloud layer.
  Nothing else of Frigate reaches the stage, and re-preparing `frigate`
  (or `SL_WA_AS_AUTHORED=D-034`) changes Streets' sky as well.
* **The jeep (D-045).** `prop/jeep` is a static item - its wheels lie in the
  body's own strip - and the game's `PjeepZ` moves five parts, so the prop
  seam refused it and the game's jeep drew under XBLA (round str1 had counted
  it prepared, which it was; it never drew). Named by
  `sl_presentation_prop_rigid`, it rides the model's root matrix whole; its
  wheels hold the rest pose. `SL_PROP_RIGID=0` refuses it as before.
* **The landmines.** `prop/landmine` draws from 20-byte records whose vertex
  programs fetch a normal at +12 and a texture coordinate at +16, and no
  colour. The static reader took +16 as the colour and mapped the image by the
  normal, so the mines drew in blue, red and purple noise. The reader now
  reads the record the item's programs declare (`xblachar.static_fetches`,
  `coord_only_strides`): over the source's new-generation props and weapons,
  the reader's output moves for `landmine` and `carescort` only, and of the
  accepted stages' prepared props only `prop/landmine` changes.

### The ends of the streets, and the start alley's backdrop (round str3)

* **The street is props.** Along the long streets (rooms 21 - 25) the game's
  rooms carry no geometry; the street is the setup's `st_pete_room_*` props,
  and under XBLA the remaster's `pstpeteroom*` items. With the background
  item hidden (`SL_BG_ONLY=9999`) the frame at playtest mark 3 is byte-
  identical; with the prop items off as well the game's own prop street draws.
* **The end of the street (D-046).** The block at the far end is a prop the
  game's far plane cuts, fully fogged by row 29. The game's cloud layer stops
  at its horizon edge - 25 rows above the true horizon on Streets, the
  environment's `WaterConcavity` - and leaves the fog colour below it, so the
  cut never shows; D-044's dome stood lighter there and the block came in
  whole between two steps of an approach. The lent dome now takes the game's
  cloud fraction where the game's layer lies on the picture
  (`sl_wa_sky_screen`, the game's own corner functions) and the fog colour
  below the edge. `SL_XBLA_LENT_SKY_EDGE=0` shades along each direction.
* **The backdrop behind the start alley (mesh 70).** The draw names no image
  and binds a program that fetches none; the private emulator names it (a
  quad list of 152 indices, no texture) and its disassembly reads
  `(c196 + sat(N . c198) c197) x colour x c13`, then the tint and the stage
  fog, with c13 the item's own upload for that draw, (0.5, 0.5, 0.5). The
  preparation drew it by the vertex colour over a white texel - twice the
  remaster's. `xblaworld.colour_times_c13_program` finds that program in an
  item (every fetch-less program in its pool, evaluated: an interpolator
  times c13 under the game's light state), `untextured_colour` reads the
  draw's c13, and `prepare` gives the draw one texel of that colour. At the
  playtest mark's position (pitch -4, the emulator's) the backdrop measures
  53/58/58 before, 28/32/33 now and 17/20/21 in the emulator, whose whole
  frame is darker there (the fence beside it 20/17/14 against 9.5/8/6): the
  backdrop now stands with its neighbours as the remaster's does.
* **The same program elsewhere.** Bunker 1's, Surface's, Silo's and
  Facility's items carry it too (the Dam's, Runway's, Frigate's, Bunker 2's,
  Statue's and Archives' do not), and their untextured draws upload c13
  (0.5; Facility 0.099). Re-prepared into a scratch root their bundles
  change (Bunker 1 by this rule alone; Surface, Silo and Facility on top of
  earlier tool changes, and the Dam by those alone); the installed bundles
  of those stages are not re-prepared (docs/backlog.md B-STR1).

### The guard behind the window (round str4)

Playtest run 20261008-235001-lvl29 (build CD0E8A86, XBLA), mark 1: a guard
standing behind a ground-floor window of room 12 and the window traded places
on screen.

* **What is on screen.** The guard (chrnum 29, seed 000000006345b389,
  difficulty 1) walks to (5735.8, 30.2, 5865.5) and holds there aiming at the
  player. The window is the item's mesh 60, one ALPHA draw carrying the frame
  and the glass in one image, in the facade's plane (z 5672.7; the facade,
  mesh 30, is opaque in the same plane). The guard is 193 units inside the
  room, yet he drew in front of the mullions and the glass, while the opaque
  wall beside the window cut him: as the view moved, his silhouette crossed
  from "in front of the frame" to "behind the wall" at the jamb. Thirteen
  consecutive frames at the mark pose are steady (40 pixels a frame, his idle
  sway), so it is an order fault, not depth noise. `SL_BG_HIDE=60` removes
  the whole window and leaves the room and the guard; `SL_PROP_ASSETS=0`
  changes nothing at the window; ORIGINAL draws the bars over him. No glass
  prop is involved (the stage's setup carries none), the guard is nowhere near
  the pane's plane, and the window keeps its ALPHA class (not a decal, not an
  alpha-one draw).
* **The first wrong stage: order.** The background is drawn whole at the
  frame's first world draw, and an ALPHA draw writes no depth, so every
  character and prop drawn after it painted over it. The game draws its rooms'
  SECONDARY (translucent) lists after every solid object of the frame
  (`bg.c` `sub_GAME_7F0B3C8C`), and the remaster's own frame (private
  emulator, round str3's logged frame on this stage) draws its twenty
  non-writing draws after all but one of its depth-writing ones.
* **The rule (`sl_wa_blend_late`).** In the game's depth range an ALPHA mesh
  of the item is left out of the first draw and drawn when the walk reaches
  the first room SECONDARY list (failing that, at the first-person weapon, a
  2D draw, a triangle outside the world projection or the end of the walk),
  on the depths, fog coordinates, decal offsets and fog colour the first draw
  computed. Opaque and CUTOUT meshes, the panorama shell and the first depth
  range beyond the game's far plane stay where they were. `SL_BG_XLU_LATE=0`
  draws the whole item at the first world draw. The census line `#90
  background translucent draws` counts the late meshes and which point drew
  them (`SL_DL_CENSUS=1`): at the mark every frame flushed at the secondary
  list.
* **The picture.** At the mark pose the mullions and the glass now lie over
  the guard, steady over thirteen frames and a yaw-and-strafe sweep.
* **Elsewhere.** A late mesh changes a pixel only where something drawn
  between the two draws lies behind it, or where the item's own order put a
  depth-writing mesh after a translucent one. Of the eleven accepted stages'
  XBLA start frames ten are byte-identical; Silo's differs in 1059 pixels
  (mean 4.5) on the soft edges of a cutout grating over the hazard stripes,
  which the item lists after a translucent mesh - the late draw puts the
  translucent one after it, as the remaster's frame orders its passes. With
  `SL_BG_XLU_LATE=0` that frame, and the mark's, are byte-identical to the
  build before. The decal spots of earlier rounds (Archives' blood and fan
  shadow, Bunker 2's stains, corridor and thresholds, Frigate's deck paint and
  waterline, Surface's fence shadow, Bunker 1's beams; six frames each) are
  byte-identical, and so are ORIGINAL and COMMUNITY HD at the mark.
* **Cost.** The late draw reuses the first draw's buffer contents (nothing is
  re-uploaded); unpaced frame times at the mark and on the long street stay
  within run-to-run spread (3.2 - 4.2 ms either way).

### The wall's decals behind the window (round str5)

Playtest run 20261009-004715-lvl29 (build 9C9A3888, XBLA), mark 1, through
the same window of room 12: "other decals on the wall behind that same NPC
are doing the same thing".

* **What is on screen.** Replayed at the mark pose (seed fffffffff92598c5,
  difficulty 1, the run's clock 02:34:90), the torn-paper decals of the room
  lie over the window's central mullion and its right jamb, and at the end of
  a strafe the "SGA" graffiti lies over the mullion; steady over thirteen
  consecutive frames and through a yaw-and-strafe sweep. They are the item's
  meshes 90, 92 and 94 (DECAL, ALPHA, on the room's opaque walls, the back
  wall at z 6196), about 520 units behind the window (mesh 60, z 5672.7).
  `SL_BG_HIDE=90,91,92,94,98` changes 5527 pixels, none inside the guard's
  outline. What lies across the guard himself is the window's own glass:
  the window image's alpha is a broken pane - holes, a ring of cracks round
  each, a grey sheen at each pane's foot - and `SL_BG_HIDE=60` takes the rings
  off his face and the wall together. That glass is in front of him, so it
  stays. With the camera inside the room the guard's hair covers the frog
  graffiti on the back wall, before and after this change: the decals were
  depth-tested behind him throughout.
* **The first wrong stage: order between two translucent draws.** Since the
  late translucent draw (round str4) the decals and the window are both
  drawn there, neither writes depth, and the item lists the decals after the
  window, so the farther decal painted over the nearer frame. The same order
  held before that round (`SL_BG_XLU_LATE=0` shows it too).
* **The rule (`sl_wa_decal_late`, `sl_wa_blend_late`).** A translucent decal
  lying on a surface that writes depth is drawn with that surface in the
  first draw, before every translucent surface, which is then laid over it
  where it is in front. Two kinds stay late in the item's order: a decal
  with a translucent surface on it or within `SL_WA_DECAL_XLU_NEAR` (8 item
  units) over it - one layered construction (Streets' mesh 61 on the
  window's glass, 91 and 93 across a window's edge; Dam's bridge planks,
  mesh 85, 3.1 units under the translucent ramps, mesh 59, listed before
  them) - and a decal lying on nothing that writes depth, which has no depth
  under it in the first draw. The renderer names them once per item (`#90
  worldasset: background/<stage> - ... drawn late`): Streets 8 of 26, Dam 2
  of 20, Silo 7 of 42. `SL_BG_XLU_DECAL=0` draws every translucent decal late
  as round str4 did (byte-identical to that build at the mark).
* **Bound.** 8 lies between the two separations measured: 3.1 (Dam, which
  must stay late; with the reach at the pull the ramps were laid over the
  planks, 262 pixels of the stage's start frame) and about 422 (Streets'
  window in front of the wall decals). A translucent surface between those
  distances off a decal on a solid surface is untested.
* **The picture.** At the mark the tapes sit behind the mullion and the
  jamb and show dimmed through the glass, steady over thirteen frames; over
  the 84-frame sweep no decal lies over the frame. Round str4's window mark
  keeps its fix (guard behind the mullions and glass); two tapes there come
  off the frame.
* **Elsewhere.** Of the eleven accepted stages' XBLA start frames ten are
  byte-identical to the build before; Silo's is byte-identical to the frame
  before round str4 (a translucent decal on a solid surface under the cutout
  grating returns to the first draw, and the 1059 pixels round str4 moved on
  the grating's soft edges over the hazard stripes move back). The decal spots
  of earlier rounds (six frames each) are byte-identical, and so are
  ORIGINAL and COMMUNITY HD at the mark.
* **Not chased.** At one pose the guard's pose differed between two
  renderings of the same seed and read (with the window hidden, or the late
  draw off): the simulation there is not independent of what is drawn.

### One decal mesh, two answers (round str6)

Playtest run 20261009-073643-lvl29 (build A700F5FA, XBLA), mark 1, at the
same window: "there is one more decal you missed at this mark".

* **What is on screen.** Under the mark's crosshair a dark torn-paper piece
  lies over the window's left jamb, steady over thirteen frames.
  `SL_BG_HIDE=91` removes it. The mark's centre ray, through its depth
  (663.6), ends on room 12's side wall at (5997.5, 153.2, 6099.4), where
  mesh 91 has a quad (triangles 14630 and 14631, on the opaque wall) - about
  425 units behind the window.
* **The first wrong stage: the answer was the mesh's.** A decal-pass mesh is
  a scatter of pieces over the whole stage; mesh 91 has 24 triangles in
  twelve places. One of its quads lies across a facade window's edge, partly
  on the window's glass (a tie), so round str5's rule marked the whole mesh
  late, and the piece on the side wall was drawn after the window.
* **The rule (`sl_wa_decal_late`, `sl_wa_decal_mix`).** The answer is now a
  piece's: the triangles of one mesh joined through shared corners (corner
  positions within the coplanar tolerance - the source writes a piece's
  faces with vertices of their own, 2e-4 apart on Dam's planks). A piece
  with a triangle under a translucent surface is late whole; a mesh with a
  lone triangle stays late whole, as before. A mesh with pieces of both kinds
  takes part in both draws, each drawing its own pieces. The renderer's
  census line now counts triangles and names the meshes drawn in part
  (Streets: 91, 2 of its 24 triangles late).
* **Two narrower versions were measured and dropped.** Per triangle, Dam's
  planks split (12 of mesh 85's 28 triangles are not themselves under the
  ramps; 12 pixels of the start frame) and Bunker 2's lone meshes split (6037
  pixels); per piece with exact corner equality, Dam's planks still split.
* **The picture.** The piece now lies behind the jamb at the mark, steady
  over thirteen frames and through a yaw-and-strafe sweep. The mark's
  framing was reached with the mark's teleport and a hand-written pitch step:
  the teleport's own pitch did not hold at this pose.
* **Elsewhere.** The eleven accepted stages' XBLA start frames (Silo's as
  restored in round str5), the twelve decal spots, round str5's and round
  str4's marks, and ORIGINAL and COMMUNITY HD are byte-identical to the build
  before; `SL_BG_XLU_DECAL=0` still gives round str4's frames.

## Depot's background, props and characters (#92)

The game builds Depot (stage 30) from `bg_depo_all_p`, scale column
17.605633 (`levelinfotable`), and the source carries the stage's background
as `background/depot` (and `depot_hits`) with no skydome item. Before the
entry the stage resolved no item, and XBLA drew the game's own depot with
the texture set's artwork: at 79 teleport poses (one per room with a setup
pad) the XBLA frames held the ORIGINAL world, with the remaster's guards and
the props prepared for earlier stages on top (round dep1).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level depot -Background -Props -Characters -ViewModels
```

* **136 images.** The item carries 136 item-local images, and the bundle
  format accepted 128: the preparation refused it ("136 textures is past
  128") and wrote nothing. The limit is now 256, the material limit
  (`SL_WA_MAX_TEXTURES`, `slwa.MAX_TEXTURES`; a material names its image by a
  32-bit index, and the asset record is allocated per item). A wider limit
  makes no older bundle invalid; every accepted stage's bundles parse as
  before. The tools' self-test checks that the two limits agree.
* **A header change rebuilds its objects.** `tools\windows\build.ps1`
  rebuilt an object when a header under `src\`, `src\game\` or `include\` was
  newer, and not for one under `src\gfx\` or `src\platform\`: after the
  limit moved, the incremental build compiled `sl_gfx_worldasset.c` alone,
  and the background pass (compiled against the old asset record) crashed
  reading the new one at the old offsets (`bg_coplanar_prepare`, at Depot's
  first frame). The scan now takes every header under `src\` and `include\`.
* **The item.** 174 draws over eleven pools (24892 triangles, all DXT: no
  8888 image, so the decoder's red-blue swap of B-FRG4 does not reach it or
  the stage's props), read at 5.68 item units per room unit: a least-squares
  fit over the 3820 room corners that land on an item vertex gives 5.680019,
  and the extent on x 5.6808 (the remaster's structures are taller, and it
  builds past the game's rooms at the yards' far edge). Declared and computed
  bounds agree; 666 triangles take the source's decal pass, one draw carries
  two layers, two untextured draws take their program's material colour
  (round str3), and no panorama shell is present.
* **The yards and buildings.** The warehouses inside and out, their roller
  doors and mezzanines, the container stacks, the yards and their lamps, the
  fences, the gatehouse, the hangar and the office block inside it, the
  platform and its canopy draw as the remaster's at every pose looked at. The
  private emulator (debug camera, background only) draws the same geometry
  and images at the four poses compared.
* **The train.** The setup holds no train object: the train at the platform
  is the background's own geometry, with the two door props on pads 46 and 47
  (`prop/trainextdoor`, a rigid door, riding the game's model at scale
  0.2857; the game's model is authored off its origin as the item is). The
  item's train is a rusty freight car (mesh 46, one 1024x512 image) with
  wooden interiors and a door at each car end, where the game's is a green
  armoured car with quilted padding; the remaster's doors are green with
  hazard stripes. Whether the remaster draws that car in play is not
  established (the emulator's debug view at the platform shows rails and
  fence where Sightline draws it; that view draws no prop and its room
  culling is unknown).
* **The props and characters.** Of the setup's 24 prop models, 22 are
  prepared and consumed: the crates and box stacks, the desks, keyboards,
  chairs, monitors and mainframes, the roof gun, the body armour, the doors
  (the warehouses' roller doors, the steel doors, the safe's door, the train
  doors) and the pickups (the rifles, the rocket launcher, the safe key, the
  blueprints). `tv4screen` (no model data) and `damchaindoor` (no atlas
  edge) are refused as on the stages before. The guards are
  `char/armourguard` with the 25 random heads, all prepared for earlier
  stages.
* **The environment.** Row 30 is a night: fog 0 0 8 from 1600 to 3000, a
  cloud layer coloured 70 199 186. The remaster's row keeps the colour,
  starts at -100 and ends at 5000; the source holds no dome, and the
  remaster draws the sky black (private emulator). The game's cloud layer
  stays. Over the 79 poses (mean frame luma, read 80): ORIGINAL 32.70, XBLA
  24.82, XBLA with the stage fog off 27.55; the remaster's own frames are
  darker than Sightline's. The row is drawn as authored.
* **The prop cut.** The game removes a prop past view depth 2974.7 on this
  row (`SL_TABLE_DBG`), where the remaster's row is 60% fogged (Surface's
  was 27%, D-029). Doors were cut at 2997 - 3993 at 15 of the 79 poses; on a
  head-on approach in yard 8 (one teleport every 30 units) the step that
  admits the two doors there changes the far window no more than its
  neighbours do. No presentation fog row is added.
* **The mission.** Driven by teleport and the game's own actions at
  difficulty 1 (a scratch sequence of the game's place-Bond primitive and
  hand-written pad steps; no input replay): the two mainframes and the
  world-map screen shot from room 58 complete "destroy computer network",
  standing on the key "obtain safe key", one press at the safe in room 42
  opens the remaster's safe door on the game's model and stepping in front of
  it "recover helicopter blueprints"; entering the car at room 84 ends the
  stage. With the invisibility cheat withheld the guards in room 58 see Bond
  and fire back; no crash.

## Train's background, props and characters (#93)

The game builds Train (stage 25) from `bg_tra_all_p`, scale column 19.53125
(`levelinfotable`), and the source carries the stage's background as
`background/train` (and `train_hits`) with no skydome item. Before the entry
the stage resolved no item, and XBLA drew the game's own carriages with the
texture set's artwork, under the remaster's guards and the props prepared for
earlier stages (round trn1, 50 teleport poses, one per room with a setup pad).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level train -Background -Props -Characters -Player
```

* **The item.** 122 draws over ten pools (35372 triangles, 55 images, all
  DXT: no 8888 image, so the decoder's red-blue swap of B-FRG4 does not
  reach it or the stage's props), read at 5.12 item units per room unit: a
  least-squares fit over the 4618 room corners that land on an item vertex
  gives 5.120057, and the extent 5.119 / 5.121 / 5.128 on x, y and z.
  Declared and computed bounds agree; 362 triangles take the source's decal
  pass, two materials are translucent and thirteen are cutouts, two
  untextured draws take their program's material colour (round str3) and a
  third its vertex colour, one pool is sphere-mapped, and no panorama shell is present. The item holds
  the carriages and the rock cutting and track of the exterior rooms 53 -
  57 below and ahead of the train; it carries no animation record.
* **The train does not move on screen.** The game's carriages are static
  rooms; the motion is sound (the setup's ambient lists) and the brake
  objective's slowing (an AI flag), and every outer window is closed by
  blinds or louvres in the game and in the remaster alike. Nothing in the
  source scrolls, so the presentation adds nothing that moves.
* **The luggage racks (THE UNIT'S EDGE, `xblaworld.unit_edge`).** The
  compartments' racks, their brackets and net, the louvres of the last car
  and the small plates and emblems are one pool (pool 2, 1028 vertices) whose
  values pile up thinly on every divisor: 1.4% on 1024, 10.2% on 128. The
  enrichment score took 1024 by a hair (14.3 against 13.1), the item's
  repeat (#81) moved it to 512, and the net drew as five large crosses along
  a rack where the game draws about twenty cells. The pool's own chart edges
  say otherwise - 144 values at 128 and 242 at 127 (a chart drawn to one raw
  unit short of the edge, the pile-up the Dam's pools show at their unit),
  against 24 at 1024 and none at 1023 - and the remaster's own frame
  (private emulator, debug camera, compartment room 19) draws a fine brass
  net. A smaller divisor whose enrichment stands within 1.25 of the pick's
  and that holds at least four times its chart-edge values is now the unit:
  pool 2 reads at 1/128, the net's cell count and the rack's end bracket
  match the game's, and the plates' and the louvre's draws span their images
  exactly once. Re-prepared with the new rule, the twelve accepted stages'
  backgrounds are byte-identical.
* **The carriages.** The crate cars, the compartment cars and their
  corridors, the saloon car, the last car and the exterior draw as the
  remaster's at every pose looked at; at eleven poses compared with the
  private emulator (debug camera, background only) the quilting, seats,
  panelling, the saloon's bench and the darkened window on the saloon's
  left side match. One window does not (below).
* **The props and characters.** Of the setup's 18 prop models, 16 are
  prepared: the crates, the tables, keyboard and monitor, the compartment
  doors (`prop/train_door`, `train_door2`, new), the roller doors, the hatch
  bolts, the rifles and pistols, the berets and the key. At eight poses run
  with `SL_PROP_DBG` the ones met load and ride their models with no refusal
  (the crates and table, `train_door` at 0.2855, `door_rollertrain`, the
  rifles and pistols, both berets); `train_door2`, the keyboard, the monitor,
  the hatch bolts and the key were not met. `prop/window` is a pane and stays the
  game's glazing (the twelve interior partitions between corridors and
  compartments). `prop/brakeunit`, refused in round trn1, is read by THE
  WRAPPED OVERRUN (below). The guards (`char/trevguard`, `armourguard`), Natalya,
  Ourumov, Trevelyan and `char/xenia` (new) ride the game's skeletons.
* **The environment.** Row 25 is the game's: fog 0 0 8, far 1500, clouds on;
  the remaster's row keeps the colour and the end and starts at -100. The
  game's own switch to row 125 (far 15000) when Bond drops below the train
  (the setup's `switch_fog_instantly`) reaches the replacement as well: the
  two remaster rows are blended alike. The source holds no dome; the
  exterior rooms and the ending keep the game's cloud layer, where the
  remaster draws its sky black.
* **The mission.** Driven by teleport and the game's own actions at
  difficulty 1 (a scratch sequence of the game's place-Bond primitive and
  hand-written pad steps; no input replay): the six brake units shot
  from 100 units complete "destroy brake units"; in the last car one burst
  at Ourumov saves Natalya, Trevelyan and Xenia leave, the 60-second
  countdown starts and Natalya's "He's in Cuba!" completes "locate janus
  secret base"; Bond placed below the train completes "escape to safety",
  and beside the track ahead the ending plays (the remaster's tunnel and
  car, the explosions, Bond and Natalya running) and completes "rescue
  natalya"; the stage returns to the front end. ORIGINAL reaches every
  objective on the same frame. The floor hatch (the watch laser on its
  bolts) was not exercised.
* **The brake units (THE WRAPPED OVERRUN, `xblachar.overrun_scale`, round
  trn2).** `prop/brakeunit` is ONE draw on one 512x512 wrapping image. At
  1/16384 its front panel, side strips and two pipes land exactly on the
  image's panel, gauges and yellow and red strips (60 of its 62 triangles
  inside), and only the back face runs to 1.042 - into the next repeat; at
  1/32768 every triangle is inside but none passes 0.521, at 1/8192 none is
  inside. The draw passes 1 + 1/64, so the draw-level rules and the lip
  refuse, and no face spans the image's height (the bottom strip holds the
  pipes), so the face rule refuses too. A last rule, asked only where every
  other refused: the largest power of two at which more than half of the
  triangles lie inside, one reaching past 0.9, and every other triangle is a
  wrapping face running at most 1/16 past the far edge (never below zero,
  never on a clamping image). The brake unit reads at 1/16384 and rides
  PbrakeunitZ at scale 0.5 (the model's box -536.5 .. 163.5, -1400 .. 0,
  -513.5 .. -163.5). At four of the six units (rooms 5, 23, 28, 43) the
  remaster's box - two gauges, the recessed lever, ribbed yellow and red
  pipes - stands on the game's footprint. A shot-out unit is a destroyed
  object, which the prop seam leaves to the game (`sl_proppres` state
  DAMAGED, levels 1 - 3): it takes the game's blackened model and smoke,
  as under ORIGINAL. "Destroy brake units" and the rest of the difficulty-1
  run complete on the same frames as before (2201, 8765, 10023, 10785; the
  front end at 11638) under ORIGINAL and XBLA, with and without the item.
  The item is not unique: Surface's jerry can has the
  same shape of coordinates (90 of 94 triangles inside at 1/16384, its
  cap's side to 1.020) and is read by the same rule - the two cans in
  Surface's hut now draw the remaster's olive can on the game's footprint
  under XBLA, where the game's grey can stood; the stool (0.672 past
  the edge), the server rack, the flight recorder, the Dam's chain door and
  Surface's hatch stay refused.
* **What is left** (the backlog's B-TRN1): the louvres of the last car's
  outer windows are covered by the back of the outer hull.

## Jungle's background, props and characters (#94)

The game builds Jungle (stage 37) from `bg_jun_all_p`, scale column
6.6844921 (`levelinfotable`), and the source carries the stage's background
as `background/jungle` (and `jungle_hits`) with no skydome item. Before the
entry the stage resolved no item, and XBLA drew the game's own jungle under
the remaster's guards (round jng1, 44 teleport poses, one per room with a
setup pad clear of the setup's objects).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level jungle -Background -Props -Characters -Player
```

* **The item.** 110 draws over eleven pools, 107 carried (63613 triangles,
  55 of its 60 images, all DXT: no 8888 image, so the decoder's red-blue
  swap of B-FRG4 does not reach it), read at 14.96 item units per room
  unit: a least-squares fit over the 1219 room corners that land on an item
  vertex gives 14.960104. Only a third of the game's corners land on one -
  the remaster remodels the ground, the slopes and the paths' edges - while
  the registration holds over the whole stage. Declared and computed bounds
  agree; 14 triangles take the source's decal pass, the canopy and
  undergrowth layers are CUTOUT materials (depth written) and the few
  translucent ones are small pieces; no panorama shell, no animation
  record.
* **The vegetation is mostly the setup's.** The stage places 111 palms,
  167 plants of five kinds (`plant1`, `plant11`, `plant2`, `plant2b`,
  `plant3`), four palm stumps and 45 jungle trees as standard props; each
  rides its model with no refusal (palms, plants and stumps at 0.2857 -
  1.4286 of the item, the tree at 0.2857). Over the survey poses no leaf
  draws a solid rectangle, a black or white fringe or a hole, and the
  remaster's bark, roots and fronds replace the game's.
* **The jungle tree (THE WHOLE IMAGE, `xblachar.whole_image_scale`).**
  `prop/jungle3tree` is five draws, each on its own wrapping image: bark
  wrapped around the trunk, buttress roots wrapped six times around its
  foot, two leaf clumps and a vine card. Most of its triangles TILE, so
  every earlier texture-scale rule (each counts vertices or triangles inside
  an image) refused it, and the 45 trees drew as the game's. At 1/8192 the
  vine card spans its image exactly and one clump covers its own while the
  roots run exactly once along their image's height. A last rule now reads
  such an item: the one power of two at which a draw lies on its whole
  image (within 1/16 of all four edges), at least one other draw tiles a
  wrapping image, and every other draw lies inside its image or tiles. A
  draw that runs partly past its image without completing a repeat refuses
  that scale, and two qualifying scales refuse the item. Over the source's
  317 remastered props it reads seven of the 28 refused: the jungle tree,
  the drone gun, a torpedo rack, two roller doors, a rifle magazine
  and `mainframe2`. Re-prepared with `-Force`, every character, head, view
  model and prop of the fourteen accepted stages is byte-identical except
  `mainframe2`, which Bunker 1 and Bunker 2 would now take; both hold it
  (`data/overrides/bunker1|bunker2/props-held.txt`), so their accepted sets
  are reproduced.
* **The drone gun.** `prop/groundgun` prepares (THE WHOLE IMAGE, at
  1/4096: the muzzle face over its five-hole disc, the barrels' metal once
  around, the camouflage tiling). Round jng1 found the runtime refusing it
  (`SKEL_BARREL does not sit where the model's part for matrix 3 sits`) and
  read the cause as a pivot on the barrel's axis; round jng2 measured the
  model's nodes and found the barrels' names traded instead, and the gun
  now draws (THE PARTS BY PLACE, below).
* **The crashed plane's canopy.** Two small pools of the item (the
  canopy's teal glass and a reflective metal part) carry coordinates with
  no unit the reader can measure and the item has no single scale to fall
  back on; they are refused, and the cockpit draws open. B-JNG1.
* **The placeholder canopy.** Two draws of the item around the start
  clearing - 448 triangles on a 16x16 solid black image and 16 (a gated
  draw, condition 2) on a 16x16 magenta one - are low, angular crowns on
  trunks. The remaster draws the same crowns bright magenta (private
  emulator, debug camera, background only): unfinished content in the
  source. Sightline draws them as authored - black ones fogged to the
  stage's olive, and the magenta one beyond room 1's edge. Whether to
  withhold them is open (B-JNG1).
* **The environment.** Row 37 is the game's: fog colour 24 32 0, far 2500,
  no cloud layer. The remaster's row keeps the colour and the end and
  starts at -100: the jungle's mist. The source holds no dome; the canopy
  covers most of the sky.
* **The characters.** The guards are `char/camguard` with the 25 random
  heads; Xenia is `char/xenia`; Natalya wears the jungle fatigues
  (`BODY_Natalya_Jungle_Fatigues`, `char/spicebond`, new, its own head);
  Bond's costume is `CUFF_JUNGLE` (`CtimberbondZ`, `char/timberbond`, new,
  its own head - the source has no `head/headbrosnantimber` and none is
  needed). All ride the game's skeletons; Bond's body shows in the intro
  swirl and in the ending (`CHR_BOND_CINEMA`).
* **The mission.** Driven by teleport and the game's own actions at
  difficulty 1 (a scratch sequence of the game's place-Bond primitive, an
  aim held on the target by view angle, and hand-written fire steps; no
  input replay; the invisibility cheat withheld so Xenia engages): the seven
  drone guns, Xenia, the nine crates of the ammo dump, and Bond entering the
  bunker complete all four objectives; the ending plays in the remaster's
  bunker corridor and the stage returns to the front end. ORIGINAL reaches
  the objectives on the same frames.

## Jungle, round 2: the canopy's edges, the backdrop cards, the drone gun, a shot object's tint (#94)

Four findings of a playtest on the native build (run 20261010-080749,
marks 1 - 4), each traced to its first wrong stage. Captures are ORIGINAL,
XBLA before and XBLA after at the marks' poses, the look held at the mark's
angles by a capture-only teleport aid.

* **The canopy's black outlines (mark 1): A CUTOUT IS LAID AFTER THE SOLID
  WORLD BEHIND IT** (`sl_wa_first_cutout`, `sl_wa_mesh_deferred`). In the
  upper canopy every leaf of the far foliage drew as a dark outline over the
  fog. The leaves are mesh 41 of the item, a cutout (18288 triangles, a
  512-square image of binary alpha); hiding it removes the outlines. A
  cutout writes depth and blends the soft edge that survives its reject, and
  the item's command-list order puts mesh 41 before the opaque meshes 42 -
  53, so each edge was blended over the aperture's black clear and then
  kept the far jungle out. Neither the fog (under `SL_BG_FOG=0` the
  outlines stand over the lit jungle), the reject's threshold nor the image
  is the stage: laid after the opaque meshes the edges blend over what is
  behind them, and pixels under half the frame's median luminance in the
  upper frame fall from 4993 to 328. The background pass now draws the item
  in two runs of its order - every mesh but the deferred ones, then the
  cutouts and the decals from the first cutout on - so a decal keeps its
  place after the cutouts it was authored after. An item with no cutout
  draws as before. Measured on the fourteen accepted stages' starts, Dam
  (the tree line's edges over the mountains, 4505 pixels), Surface (25),
  Surface 2 (4183) and Silo (a stair grating's edges, 3300) move and the
  other ten are byte-identical; at 48 earlier playtest marks of those
  stages, 35 are byte-identical and 13 move: Surface's fence no longer hides
  the trunks behind it (seven marks), Silo's grating bars and stair rails
  lose a dark and blue edge line (three), Dam's chain-link (one) and two of
  Frigate's (72 and 946 pixels). Every change is a cutout edge now blended
  over the world behind it. `SL_BG_CUTOUT_LAST=0` draws the source's order.
  Separately measured and not changed: the images keep rgb 0 under alpha 0,
  so where the fog is light a cutout's bilinear edge still darkens (B-JNG1).
* **The flat backdrop panel (mark 2): the game's fade on D-035's stages
  alone** (`sl_presentation_game_fade`, `sl_wa_game_fade`). The panel was
  the game's own room 11 SECONDARY list: a 32x32 bamboo backdrop card
  (textures 827/828), lit white at the top and 32 71 52 at the foot, which
  the #83 fade rule (`nf_fade_kept`: a translucent room triangle whose shade
  spans 32 or darker to 224 or brighter in some channel, drawn where the
  replacement has no surface) kept under the remaster. That rule came with
  D-035 for Frigate's waterline; measured from the ROM's room lists it also
  takes 96 of Jungle's backdrop card triangles (87 with no remaster surface
  near), 42 of Train's (22 drawn: track strips at the rail ends, one a
  white line floating over the remaster's rails) and one of Statue's. A
  tighter shade test (dark in every channel) still keeps 12 of Jungle's
  cards, so the fade is now drawn on the stages the presentation names -
  Frigate - and suppressed with the rest of the rooms elsewhere. With it off
  the remaster's own canopy closes the view at mark 2; ORIGINAL shows the
  game's card there as part of its scene. `SL_BG_FADE_STAGES=0` draws the
  fade on every stage, as before.
* **THE PARTS BY PLACE (mark 3)** (`prop_parts_by_place`,
  sl_gfx_charskin.c). The drone gun's model (`PgroundgunZ`, the autogun
  skeleton) turns two barrels about their own x axes by the same angle:
  matrix 3 (`Switches[3]`, node -1000 177.2 -247.2) and matrix 4
  (`Switches[6]`, node -1000 177.2 247.2). The item's barrels are
  `SKEL_BARREL` at -500 88.6 123.6 and `SKEL_BAR2` at -500 88.6 -123.6 -
  the two nodes exactly at the item's scale, 0.4998 - but the bundle names
  matrices by the game's NAMES, so `SKEL_BARREL` rode matrix 3, a barrel
  spacing from its node, and `SKEL_BAR2`, named for none, would have ridden
  the pitch without turning. Where the names are refused, the seam now
  relates the item by place: every joint that rides a matrix must sit
  within 0.001 of the item's extent of a present node and rides the nearest,
  and every named matrix must still be ridden. The gun rides matrices 0 1 2
  3 4 4 3 and aims, turns and fires with the game's model over a sequence
  of teleports round it (invisibility off for that capture). An item the
  names relate never reaches the rule: every accepted stage's start is
  byte-identical with it.
* **A shot object's tint (mark 4)** (`sl_wa_lin_takes`). A destroyed drone
  gun drew bright under XBLA where ORIGINAL draws it black. chrobjRenderProp
  darkens a shot object through its fog register (rgb toward 0 by 21/256 per
  shot, alpha up 15); the shot gun's lists draw under texture environment 2
  (alpha the vertex's alone), which the linear program did not take, so
  under the remaster row they drew on the fixed pipeline, where #77 gives
  the one fog stage to the row and the register's tint is lost
  (`SL_OBJ_FIXED_STAGE_FOG=0` or `SL_NATIVE_STAGE_FOG=0` brings the black
  back). The program reproduces environment 2, so under the row it now takes
  it and applies the tint before the row's fog; ORIGINAL and COMMUNITY HD
  are untouched. The remaster's drone gun keeps its own destroyed shading
  (THE DESTROYED AUTOGUN) and is black as well. Of the accepted stages'
  starts only Runway's moves, by at most 2 in 420 pixels.
  `SL_OBJ_REPLACE_PROG=0` keeps environment 2 on the fixed pipeline.

## Control's background, props and characters (#95)

The game builds Control (stage 23) from `bg_arec_all_p`, scale column
80.645164 (`levelinfotable`), and the source carries the stage's background
as `background/control` (and `control_hits`) with no skydome item: the stage
is an interior. Before the entry the stage resolved no item, and XBLA drew
the game's own rooms under the remaster's guards (round ctl1, 71 teleport
poses, one per room with a setup pad or at the room's centre).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level control -Background -Props -Characters -Player
```

* **The item.** 166 draws over twelve pools (15868 triangles, 110 of its
  111 images; the one left out is a normal map), read at 1.24 item units per
  room unit: a least-squares fit over the 3355 room corners that land on an
  item vertex gives 1.239988, and the extents agree to 1.240 on all three
  axes. Declared and computed bounds agree; 1124 triangles take the source's
  decal pass, 23 materials are translucent and 20 are cutouts, eight
  untextured draws take their vertex colour, and no panorama shell or
  animation record is present. It holds the whole stage: the cave approach
  and its lift shafts, the control room on its floor and galleries, the
  side rooms, store rooms and corridors.
* **The floors (THE CONDITIONAL EXEC, `xblaworld.computed_fetches`).** Every
  tiled floor of the control centre drew as a dark field of blocks. The
  floors are one pool of 1068 triangles whose two draws name three images: a
  tangent-space normal map (slot 0), a 256x256 pre-rendered view of the
  control room (slot 1) and a 512x512 marble tile (slot 2). The base rule
  passes over the normal map, and the #82 rule passes over a slot the
  program fetches at a coordinate it computes - a reflection - but that rule
  never saw these draws: each placed program of this item ends in a
  predicated exec (control-flow op 5), the program walk refused anything but
  plain execs, so `computed_fetches` answered nothing and the reflection
  image was laid as the floor's colour, tiled. The walk now takes the
  conditional and predicated execs as instructions that may run when the
  question is what may have written a fetch's coordinate: the floors'
  programs fetch slot 1 at a computed coordinate, and the marble tile is
  drawn. The last-write question (a blended draw's coverage) still refuses
  such a program. The private emulator's debug camera shows the remaster's
  floors as the same light marble tiles. Re-prepared with the new walk, the
  fourteen accepted stages' backgrounds are byte-identical. The reflection
  layer itself is not drawn, as on Silo's rails.
* **The props.** Of the setup's 19 prop models, 17 are prepared. Thirteen
  are the bundles earlier stages prepared, byte-identical (the crates, box
  stacks, keyboards, the `tv1` monitors, `mainframe1`, the autoguns
  (`prop/roofgun`), the body armour, the lift doors (`prop/silo_lift_door`)
  and the pickups); four are new: the consoles' desks (`desk_arecibo1`), the
  lockers (`locker4`), the sliding doors (`door_st_arec1`, a dynamic door)
  and the vertical doors (`vertdoor`, a rigid door). Over the survey every
  one rides its model with no refusal. `prop/tvscreen` is refused (no model
  data, as on Facility and the Bunkers); the hanging screens are the game's.
  `prop/mainframe2` was held in round ctl1 (THE WHOLE IMAGE read it at
  1/16384); round ctl2 reads it at 1/8192 by THE GAME'S REPEATS and takes it
  (below), so all six armoured mainframes draw the remaster's items.
* **The glass.** Every pane of the stage is a tinted-glass prop
  (`PROPDEF_TINTED_GLASS`, model `PwindowZ`), the game's own glazing, which
  the prop seam does not take; the background item's translucent materials
  are its own fittings.
* **The environment.** Row 23 is the game's: colour 0 0 0, far 10000; the
  remaster's row keeps the colour and the end and starts at -100.
* **The characters.** The guards are `char/camguard` with the 25 random
  heads; Boris (`char/boris`), Trevelyan (`char/trevelyan`) and Natalya in
  her jungle fatigues (`char/spicebond`) are the bundles earlier stages
  prepared; Bond's costume is `CUFF_JUNGLE` (`char/timberbond`), shown in the
  intro swirl in the lift.
* **The doors.** Opened by the game's own interact press, the vertical doors
  and the lift doors draw the remaster's items and open on the same frames
  under ORIGINAL and XBLA; the locked doors stay shut under both.
* **The mission.** Driven by teleport and the game's own actions at
  difficulty 1 (a scratch sequence of the game's place-Bond primitive, an aim
  held on the target by view angle, hand-written fire steps; no input
  replay; the invisibility cheat withheld throughout): the autoguns and the
  cave guards fall, Natalya leaves the lift for her first computer, Boris
  surrenders and flees, Natalya moves to the second computer and the alarm
  brings the guards; the remote mines from the cave crate destroy the six
  mainframes and complete "destroy armored mainframes". ORIGINAL reaches
  every recorded event on the same frame, and the characters' positions
  traced every 250 frames agree line for line. Natalya's second timer was
  not survived, so her escape and the ending were not reached (B-CTL1).

### Control round 2 (#95): the dark marks, the armoured mainframe, the autoguns

* **The dark marks on the WARNING sign and the floors are not the game's
  scorch.** They stand in the frame with no shot fired (frame 200 of a
  teleport to either playtest mark), and ORIGINAL draws nothing there. They
  are one mesh of the item, 81: six triangles in three quads - on the wall
  under the WARNING sign of room 13 and on two floor patches, one of them the
  floor of room 14 under the second mark - of
  a 32x32 grey cloud image with a binary alpha under a dark vertex colour
  (51..175 grey), source draw 0x5090 (image 75, style 0x4b01: the cutout
  group). Its program draws the image times the vertex colour, alpha the
  image's, so as a cutout it is an opaque black blotch with a ragged edge.
  The #78 overlay rule had given it the decal pass, since every triangle
  lies on an earlier opaque surface. The remaster's own frame (private
  emulator, debug camera, at both marks) shows the sign and the floor clean,
  and Sightline with the mesh hidden matches it, down to the faint square
  beside the sign.
* **WHICH OVERLAYS THE SOURCE DRAWS (`prepare.overlay_alone`, the pass
  gate).** A level draw now carries the pass gate standing over it
  (`xblaworld.draw_passes`, `pass_mask`). The overlays #78 found on Runway,
  Silo and Surface lie in a decal-pass gate (0x2702, 0x2b01) although their
  style word does not carry the bit, and keep the decal pass. An overlay in
  no decal pass sits in the cutout gate with no depth priority; where the
  source draws its image nowhere else it is withheld, and where the image is
  drawn elsewhere too it stays a layer of that construction as before:
  Archives' blood pool (meshes 60 and 61, #89), Depot's mesh 64 beside its
  image's decal draws, Surface's two-layer base on the opaque draws of its
  own image. Re-prepared with `-Force`, the fourteen accepted stages'
  backgrounds are byte-identical except Silo's, which leaves out mesh 61 - two
  triangles of a black-coloured image on the floor of room 23, the foot of a
  shaft no stand tile reaches, seen from none of the six playtest marks of
  Silo (each frame byte-identical with the mesh hidden). What the remaster
  does with such a draw - culls it, loses it to the surface's depth, or
  never issues it - is not read; only its absence from the frame is
  measured.
* **THE GAME'S REPEATS (`gameshade.game_repeats`).** THE WHOLE IMAGE read
  `prop/mainframe2` at 1/16384, the one scale at which one of its draws (the
  panel) lies on its whole image - half of the game's repeats on each axis.
  The game's own model gives the rate: `Pmainframe2Z`, read from the ROM,
  draws its back plates two by four, its panels and blades as `Pmainframe1Z`
  does, on the same textures at the same repeats. Placed into the item's
  reference box as D-032 places a model, every remastered triangle lying on a
  game triangle gives the per-axis log2 of the two rates; for the mainframe
  the area-weighted median is -1.000 with 78% of the matched area within a
  quarter of it, for `mainframe1` (read 1/8192 by the reach) +0.000 and 83%.
  The preparation now asks this of every static item THE WHOLE IMAGE alone
  decided, and reads it a power of two over when the median lies within
  `REPEAT_NEAR` (0.25) of a whole power other than zero and `REPEAT_SHARE`
  (75%) of the matched area agrees. It is a check on that rule alone: over
  the source's 159 static items with a game model, 46 that earlier rules
  decide sit a power of two or more from the game's rate, where the remaster
  gathered several of the game's textures into one image or re-tiled a face.
  Of the items THE WHOLE IMAGE decides, the Jungle tree (-0.017), the wooden
  table (-0.303, 44%) and the filing cabinet (-0.791, 64%) keep their scale,
  and the mainframe takes 1/8192. Without the ROM the answer stands and the
  summary says so. Re-prepared with `-Force`, every prop of the fourteen
  accepted stages and Control is byte-identical but `mainframe2`, prepared
  for the first time.
* **The mainframe on Control, Bunker 1 and Bunker 2.** At 1/8192 the item's
  front is two by two panels over four columns of blades, its back the
  plates of `mainframe1`'s back, its sides whole plates - the game's own
  layout, beside the `mainframe1` items. Control's four (rooms 10, 11, 44 and
  53), Bunker 1's one (room 17) and Bunker 2's three (room 11) were seen from
  each side; shot with the PP7 the item takes the game's impacts (the model
  is immune to gunfire, flags2 0x4000), and destroyed by a rocket it draws as
  the game's blackened model, as any destroyed prop does. The three stages'
  held lists no longer name it. On Bunker 1 and Bunker 2 this changes an
  accepted look: restoring `mainframe2` to `data/overrides/bunker1/props-held.txt`
  and `data/overrides/bunker2/props-held.txt` reproduces the accepted sets on
  the next preparation, and removing `prop/mainframe2.slwa` from the world
  asset root draws the game's model on every stage at once (the bundle is
  shared).
* **The autoguns darken where the game darkens them.** The ceiling gun of
  room 10 (setup obj 98, an autogun drawn by `prop/roofgun`) takes five PP7
  hits to destroy at difficulty 1 and four more for each further destroyed
  level, under ORIGINAL and XBLA alike. ORIGINAL draws it blue-grey and
  smoking at level 1 and black from level 2; the remaster's item, darkened by
  THE DESTROYED AUTOGUN, is blue-grey at level 1 and dark from level 2 - the
  same steps on the same shots. A destroyed gun that darkens only after
  further shots is the game's own behaviour; nothing is changed (Rare's
  behaviour is preserved by default).

## Caverns' background, props and characters (#96)

The game builds Caverns (stage 39) from `bg_cave_all_p`, scale column
13.44086 (`levelinfotable`), and the source carries the stage's background
as `background/cavern` (and `cavern_hits`) - the MODEL spelling;
`background/caves` is the multiplayer Caves (`bg_oat_all_p`). Before the
entry the stage resolved no item, and XBLA drew the game's own caverns under
the remaster's guards (round cav1, 59 teleport poses, one per room with a
setup pad or at the room's centre).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level cavern -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level caverns -Props -Characters -Player
```

* **The item.** 82 draws over six pools (12736 triangles, 49 images), read at
  7.44 item units per room unit: the extents agree to 7.440 on all three axes
  and a least-squares fit over the 2982 room corners that land on an item
  vertex gives 7.439977. Declared and computed bounds agree; 146 triangles
  take the source's decal pass, the catwalk gratings and railings are cutouts,
  one draw is untextured, and no panorama shell or animation record is
  present. Every draw stands under one pixel program family, the rock's.
* **The water.** Three surfaces of the game's room lists are water (texture
  1508, scrolled by the game's own list, tex.c): the inlet lake (room 15, y
  -3474), the outlet caverns' plane (room 37, y -15) and the plane under the
  control-room block (room 61, y 7). The item draws all three as one mesh
  (source draws 0x854 and 0x864, a 64x64 intensity image) whose 53 vertices
  carry the game's own water corners and vertex colours (165 213 213 and
  black in the game, 151 205 205 and black in the item), at the game's
  heights. Its records are the rock's: the same pixel program, the opaque
  style, no constant upload, no default block, no wave pool and no material
  record - so it is an opaque, unmoving surface, dark teal on the lake and
  near black where the corners are black. The private emulator's debug camera
  shows the remaster's water at four of the same poses as the same dark,
  opaque surface. Whether the remaster's engine scrolls it at run time is not
  established: that camera freezes the game, and placing Bond by memory is
  refused on this stage. The game's room triangles kept by the native fill lie
  under the item's plane and are covered there (`SL_BG_FILL=0` changes no
  pixel at the water poses).
* **The water moves: D-048.** The game animates texture 1508 through its
  own setup list (`MipMap2C_Something_Setup`, unk_092E50.c): two wrapping
  64-texel tiles of the image, tile 0's origin advanced every tick by the
  water controller (S 0.25 and T 0.1 quarter-texels a `g_ClockTimer` step),
  tile 1 at (+90, +150) from it, lerped by a PRIM_LOD_FRAC that follows a
  sine (0.04 a step). The item's water draws carry the game's own
  coordinates in the remaster's convention - u = s / 2048 + c, v = c' -
  t / 2048, measured triangle by triangle on all 41 (`gameshade.water_draws`;
  both draws lie on the game's water, their plane up to 0.48 room units above
  it, so the shade rule's 2-unit plane tolerance missed 0x864). The
  preparation marks their material `MAT_GAME_WATER` and the background pass
  draws it through the game's two-tile blend at the controller's phases
  (`sl_wa_water_state`; texenv mode 8 on unit 0, the vertex colour on unit 1,
  both texture matrices back to the identity after). The rock draw that
  reaches the shore (24 of its 2306 triangles on the water) follows another
  relation and is left still. Re-prepared, the bundle differs from the one
  before in one byte (material 7's flags, 0x2 -> 0x82). Measured at the
  playtest mark with the pitch held, 20 reads apart: the water region moves
  2.0 - 2.4 grey levels a step (ORIGINAL's water 3.5 - 4.1), and 0.35 - 0.45
  with `SL_BG_WATER=0`, which is byte-identical to the build before the rule
  over 13 frames. ORIGINAL and COMMUNITY HD read no bundle and are unchanged.
* **The airlocks: THE PARTED DOOR.** Each airlock is an iris door
  (`door_iris`, nine, door type 7) on one side and an eyelid door
  (`door_eyelid`, nine, type 6) on the other, neither carrying
  `DOORFLAG_CLIP_TO_BBOX`; no other campaign stage uses either type. The
  object render turns their leaves under matrices of their own (propobj.c,
  the door branch: the eyelid's two lids about x by the open angle, the
  iris's six blade holders and six blades about z), and the remaster's items
  joint their parts by the skeleton's names (`SKEL_UPPER` / `SKEL_LOWER`,
  `SKEL_OUT1..6` / `SKEL_IN1..6`). The derivation now takes them
  (`stageprops.door_taken`: a parted door) and the seam rides the item's
  joints on those matrices as a moving prop's (`sl_door_parts`,
  `door_ride`); a rigid item is refused. `prop/door_iris` rides all thirteen
  matrices (`19 joints on matrices 0 1 3 5 7 9 11 2 4 6 8 10 12 ...`) and,
  closed by the game's own interact press, turns its blades shut on the same
  reads as ORIGINAL. `prop/door_eyelid` was refused by the preparation: its
  draws reach no image edge at any scale (two clamped steel images, the
  largest coordinate 0.645 at 1/32768, one draw running to 1.289 of its image
  at 1/16384), so the eyelids were the game's.
* **The eyelids: THE GAME'S COORDINATES.** The source's two generations of
  `prop/dooreyelid` are the same item, and at 1/16384 its coordinates ARE the
  game's `Pdoor_eyelidZ`'s, in image repeats with t turned (v = 1 - t):
  texture 1161 (32x32) spans s -0.109 .. 1.073, t -0.289 .. 1.026 and the
  item's image 1 u -0.109 .. 1.073, v -0.026 .. 1.289; texture 197 likewise
  against image 0 - the overrun past the image's edge is the game's own,
  clamped as the game clamps it. A moving prop every texture-scale rule
  refuses is read at the one power of two at which every coordinate of each
  image lies within a quarter texel of one game texture's, each image on its
  own texture, each texture's coordinates reached to 90%
  (`gameshade.coords_scale`, `prepare.convert_prop`; with the ROM only). The
  iris (decided by the reach rule) carries the remaster's own coordinates and
  meets the rule at no scale; it is not asked. `prop/door_eyelid` rides
  `Pdoor_eyelidZ` at scale 0.2857, its five joints on matrices 0 2 1 1 2 (the
  upper lid on matrix 1, the lower on 2). Pressed shut from the inlet catwalk
  it closes on the same reads under ORIGINAL, XBLA before and XBLA after, and
  with Bond in its path it reopens on the same reads in all three; the game's
  door, its collision and its interaction are untouched.
* **The props.** Of the setup's 22 prop models and its two airlock doors, 21
  prepare. The consoles (`console1`, the nine objective consoles, screens on
  the item's own rectangles), the monitors, desks, radios, crates, lockers,
  oil drums, autoguns, lift and vertical doors, body armour, hats and held
  weapons are the bundles earlier stages prepared, byte-identical; each rides
  its model with no refusal over the survey. `prop/tv4screen` (no model data)
  and the window panes are refused as everywhere; `prop/chrautoshot` prepares
  but the seam refuses it (its box is not the model's on axis 0), so the
  floor's autoshotgun is the game's. A destroyed console draws as the game's
  blackened model, as any destroyed prop does.
* **The environment.** Row 39 is the game's: colour 8 0 8, far 6000, no
  cloud layer; the remaster's row keeps the colour and the end and starts at
  -100. The source's `skydome/caverns` is a daylight dome the stage does not
  compose (the sky item is the background's, `cavern`, and none is
  provided); no view of the caverns opens onto a sky.
* **The characters.** The guards are `char/trevguard` with the 25 random
  heads, the scientists `char/techman`, and a guard on Trevelyan's body
  `char/trevelyan`; Bond's costume is `CUFF_JUNGLE` (`char/timberbond`),
  shown in the intro swirl and the ending in the exit lift. All are the
  bundles earlier stages prepared.
* **The intro swirl.** The game's intro camera circles Bond in the start
  lift and passes behind the lift's walls; ORIGINAL sees the lift's inside
  through them, because the game's walls face inward. The item's walls are
  drawn from both sides (the item carries no face cull, THE FACE CULL below),
  so for about 30 reads the swirl looks at the wall's back. The remaster's own
  intro shows the same wall at the same moment (private emulator, a time
  series of the level start).
* **The mission.** Driven by teleport and the game's own actions at
  difficulty 1 (a scratch sequence of the game's place-Bond primitive, an aim
  held on each console, hand-written fire steps; no input replay; the
  invisibility cheat withheld): the four inlet consoles, the four outlet
  consoles and the master console are destroyed and objectives a, b and c
  complete; the scientists at the consoles die in the consoles' explosions,
  which fails "minimize scientist casualties", and the stage returns to the
  front end. ORIGINAL reaches every event on the same frame and the traced
  characters agree line for line. At Agent, where that objective is the only
  one, entering the exit lift completes it and the ending plays to the front
  end under both presentations.

### THE FACE CULL (#96): Control's lift intro

During Control's intro swirl the game's camera stands behind the lift's back
wall for about 50 reads (measured: the eye leaves the lift room's box), and
XBLA drew the wall's back (the item's mesh 29, met 15 units from the eye) where
ORIGINAL shows Bond and Natalya in the lift. The background pass draws with
culling off - as the Dam's console frames measured the remaster drawing its
background (`PA_SU_SC_MODE_CNTL` 0x4). But the item's command list writes a
render-state register, 0x38 (ops 0x0c and 0x0d, `xblaworld.RS_CULL`), before
almost every draw, and over every accepted stage's item and Caverns' it is
0; on Control it is 0x10 over 9661 of its 15868 triangles, the lift's walls
among them. The remaster's own Control intro shows the lift's inside at that
moment, and its Caverns intro, under 0, shows the wall. So a nonzero 0x38 is
read as the draw culling the face that turns away; the value itself is not
decoded.

The preparation marks such a draw's material `MAT_CULL_BACK` (part of the
material key), and the background pass withholds a triangle of it whose
authored normal does not face the frame's eye in the item's coordinates
(`sl_wa_cull_drops`, `sl_wa_tri_faces`). `SL_BG_CULL=0` draws both sides as
before. Re-prepared, every accepted stage's background is byte-identical but
Control's (129 materials and 131 meshes, where 127 and 129 were). Control's
intro now shows the lift's inside through the swirl; at its 27 survey poses
16 frames are byte-identical and the rest move by a few to 263 pixels, except
two that look across a yellow hazard-stripe sheet whose stripes draw fainter
over part of it, its triangles there turned away from the eye (2314 and 8500
pixels). Caverns' intro keeps the wall, as the remaster does.

## Cradle's background, props and characters

The game builds Cradle (stage 41) from `bg_crad_all_p`, scale column
43.103451 (`levelinfotable`), and the source carries the stage's background as
`background/cradle` (and `cradle_hits`) and no `skydome/cradle`. Before the
entry the stage resolved no item, and XBLA drew the game's own rooms under the
remaster's guards.

```
.\tools\windows\prepare-xbla-assets.ps1 -Level cradle -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level cradle -Props -Characters -Player
```

* **The item.** 81 draws over eight pools (14561 triangles, 40 images), read
  at 2.32 item units per room unit: a least-squares fit over the 1107 room
  corners that land on an item vertex gives 2.319943. Its extents run 4.4
  times the rooms' across and 3.2 vertically, because it carries the canyon
  the antenna stands over, cliffs and forest out to the horizon, where the
  game draws its fog colour. 278 triangles take the source's decal pass, 9
  materials are cutouts and 11 translucent; no animation or material record.
* **The sky.** The source holds no dome for the stage (its eleven domes are
  bunker, bunker2, caverns, complex, dam, egyptian, frigate, runway,
  surface, surface2 and temple). Cradle is lent `skydome/frigate` under its
  own row 41 (fog 96 128 160, a cloud layer coloured 255 255 0; the
  remaster's row -100 .. 30000), D-044 extended (#96), with D-046's edge.
  The stage has no cinema row, so the intro and the ending take the same
  dome. `SL_XBLA_LENT_SKY=0` draws the game's cloud layer, byte-identical to
  the build before.
* **The shadows** (D-049). The item lays the antenna's truss, brace, rail
  and cable shadows onto its floors as separate draws and two-layer layers
  under a black vertex colour (0x000000 or 0x040404), which its programs
  export as opaque black - the remaster draws them so (private emulator,
  round crd5). The preparation draws every such triangle blended at vertex
  alpha 143 (`prepare.shadow_overlays`: 11 overlays, 166 triangles), the
  alpha the remaster gives its black shadow overlays on the Dam, Archives
  and Depot, so the floors darken under them with their tread kept.
  `SL_WA_AS_AUTHORED=D-049` prepares them as authored. Owner playtest run
  20261012-142635 confirms the shadows fixed.
* **The shaft ladder** (D-050). The remaster's item carries the pole in the
  shaft down to the bottom platform (room 7) - draw 0xe44 in the `new`
  generation, 0xeac in the `original` - and no ladder: the draws near the
  pole's column (the pole, the shaft's panels 0x4e8 / 0xa64, the floors,
  shadows and structure beside it) were inspected and none is a ladder,
  and the remaster draws none at the mark. The game's ladder is one
  translucent quad of room 7's secondary list (texture 844); the room is
  named (`sl_presentation_native_kept`) so that its translucent triangles
  with no surface of the native fill's index (the replacement's
  non-blended meshes) within the hole radius draw where they stand
  (`sl_gfx_dl.c nf_room_kept`): the ladder, 2 triangles, and nothing else
  of the room. `SL_BG_KEEP_ROOMS=0` suppresses them as before.
* **The props and characters.** Every model the setup and the stage's
  characters use prepares and rides with no refusal over the survey: the
  objective consoles (`console_sev2b`), the autoguns (`roofgun`), the sliding
  doors (`sevdoormetslide`), body armour, held weapons and hats; the guards
  `char/trevguard` with their heads, `char/trevelyan` (character 0, measured
  running the west arm in step with ORIGINAL) and Bond's `char/timberbond`
  in the intro. All are the bundles earlier stages prepared, byte-identical.
* **The helicopter** (`prop/helicopter`, the aircraft on pad 147, hidden
  until the ending clears its flags2 0x00080000 - `object_flags_2_set_off(0x05,
  0x00000800)`, which `CharArrayFrom32Rev` writes reversed). Two rules (#96):
  * THE TWO-LAYER RECORD (`xblachar.STRIDE_RIGID_UV2`). The tail boom's draw
    (52 of 53, 40 vertices) reads a 40-byte record the item's programs
    declare as position, slots, normal, two coordinates (+24, +28) and two
    colours (+32, +36; the second full fetch, `static_fetches(follow_full)`).
    Its pixel program is the two-layer blend (`xblaworld.layer_blend` with
    `channels`): the body atlas (slot 1, +28, colour +36) under the
    "LB-5577" number (slot 0, +24, colour +32 at alpha 0xbf), weighed by the
    number image's BLUE. It is carried as the base draw and a blended decal
    draw of the same triangles, `SL_WA_MAT_COVERAGE_BLUE`. Only a moving
    prop reads the record (`read_character(layers=True)`); every other
    reader refuses it as before. Prepared: 54 draws (2 blended) (1 decal)
    (1 blue coverage), 5305 vertices, texture scale 2.0 with and without
    the layer.
  * THE PARTS ABOUT THEIR OWN PIVOTS (`sl_gfx_charskin.c`,
    `prop_parts_own_pivot`). The item is a different build of the aircraft:
    its tail rotor sits 2633 item units from the model's tail-rotor node
    (tolerance 1387), its main rotor 1297 from the hub, so the runtime
    refused it ("SKEL_TAILROTOR does not sit where the model's part for
    matrix 3 sits"). On an animated model (rooted on a HEADER node) whose
    parts each lie nearest the node their names give them, each part now
    turns about its own joint by its matrix's turn relative to the root's;
    an item the names or places relate maps exactly as before. Runtime:
    "prop item rides PhelicopterZ - scale 1.5384 ... 6 joints on matrices 0
    1 2 3 2 3"; both rotors turn on the item's own mast and boom. Owner
    playtest run 20261012-142635 played the ending with the bundle
    ("LOADED prop/helicopter", "rides PhelicopterZ", its log lines 667 and
    668) and confirms the aircraft fixed.

## Aztec's background, props and characters (#96)

The game builds Aztec (stage 28, launcher `aztec`) from `bg_azt_all_p`, scale
column 52.083332 (`levelinfotable`). The source's item for it is
`background/aztec`: of the 7286 corners of the game's rooms, 6123 land on a
vertex of it (7275 on its `original` generation, of which the remaster
remodels part), against 4 on `temple` and 1 on `multitemple` - the
multiplayer Temple and its sibling, other stages. The fit over those corners
gives 1.920040 item units per room unit, read at 1.92
(`sl_wa_stage_item_path`).

```
.\tools\windows\prepare-xbla-assets.ps1 -Level aztec -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level aztec -Props -Characters -Player
```

* **The item.** 87 draws over 13 pools (30704 triangles, 57 images); 82
  carried (30684 triangles, 53 images, 64 meshes). 71 draws (25079
  triangles) stand under the source's face cull (THE FACE CULL, below,
  `MAT_CULL_BACK`); 195 triangles take the decal pass; 18 materials are
  cutouts and 4 translucent. No animation or material record.
* **The placeholder images** (D-051). Five 16x16 images of the item are the
  authoring tool's magenta checker (the pattern matches nothing else over
  the 19 prepared backgrounds); their five draws (2809 triangles) drew
  magenta, as the remaster draws them (private emulator, room 72 and the
  mural halls). The two opaque ones - the ceiling tubes' sleeves and end
  caps (0x2018, 2232 triangles) and a ceiling strip (0x484c) - are whole in
  their geometry and draw by their own vertex colour over one white image;
  the three whose outline or coverage is the image's alpha (two in the
  cutout gate 0x4b01, one translucent) are withheld
  (`prepare.placeholder_checker`, `placeholder_opaque`). Withholding all
  five left the tubes as bare glowing cores (playtest run 20261012-195917,
  mark 4); they stand complete now. `SL_WA_AS_AUTHORED=D-051` keeps the
  five as authored.
* **The launch pad's markings: an untextured draw in the translucent pass
  alone** (#96, `prepare.background_blend`, `background_decal`). Two
  untextured draws of pool 12 (38 triangles: the yellow dashed arrow beside
  the emblem and the pad's white and orange marks, vertex alpha 128) carry
  style 0x4701 - the opaque pass by its low byte - under the pass gate
  0x2702, which admits the translucent pass alone and names the decal pass
  (0x2000). Drawn opaque they laid white slabs (one across the view of the
  orbiter's wing from the silo's top); they are blended at their vertex
  alpha, and take the gate's decal pass, so they carry the decal depth
  priority on the pad's floor they lie on. Without it they tied with the
  floor pixel by pixel in the late translucent draw ("Decals on launch pad
  flicker", below). Over the 19 stages no other untextured draw stands
  under such a gate (the textured ones that do - on Caverns, the Dam,
  Runway, Surface and Train - keep the style word's answer, and their
  backgrounds prepare byte-identical). Not matched: the remaster shows a tan
  band over the orbiter's belly that nothing here draws; its source is not
  established (B-AZT1).
* **The sky.** The source holds no `skydome/aztec`, and the remaster's row
  28 is black fog from -100 to 15000. Under the game's row 28 (fog 0 0 0,
  far 15000, a cloud layer coloured 83 72 65) XBLA draws the game's own
  dark cloud layer where the item leaves the sky open - above the silo's
  canopy (room 67) and over the temple's outer walls (rooms 2 and 13) - as
  ORIGINAL does; the private emulator shows black there. The item closes
  the voids ORIGINAL shows round rooms 2, 13, 71 and 73 (the game's sky
  under black fog there).
* **The props.** 26 of 28 models prepare and ride; `prop/tvscreen` holds no
  model data (as on Facility) and the source carries no remastered
  `prop/discreader`. Three needed a rule each:
  * THE GAME'S COORDINATES FOR A STATIC ITEM (`prepare.GAME_COORDS_STATIC`).
    The boardroom's desk doors, `door_azt_desk` and `door_azt_desk_top`,
    reach no atlas edge at any power of two; every coordinate of their
    draws lies on the coordinates of one of the game model's textures at
    exactly one scale (`gameshade.game_coords_scale`): 1/2048 (images 0 and
    1 on the game's 294 and 1418) and 1/4096 (images 0 .. 2 on 1031, 294
    and 1418). Named, so that `hatchsevx` - which qualifies too, at 1/8192 -
    stays as its stage was accepted. Needs the ROM.
  * THE ATLAS TRIANGLES (`xblachar.atlas_triangles_scale`,
    `prepare.ATLAS_TRIANGLES_STATIC`). The shuttle is four draws: a hull
    panel and a louvre tiling below zero, and two draws on a 512x1024
    photograph of the shuttle from above and the side. At 1/8192 those two
    trace the photograph (the wing's edge along the wing, the engines on
    the nozzles) with 368 of their 370 triangles inside it, reaching 0.943;
    one vertex of a small cap runs to 1.274. Measured over the triangles of
    the draws that never run below zero, the scale is the largest power of
    two at which most lie inside, one reaching the edge (12 of 370 inside
    at 1/4096). Named: the game's model drew before. Seen from the bay
    floor and from the silo's top it stands where the remaster's does, its
    wing art, "4" roundel, emblem, tank and boosters matching the private
    emulator's. This is the shuttle `SL_WA_AS_AUTHORED=D-053` prepares;
    by default D-053 converts the original generation instead.
  * THE ORIGINAL SHUTTLE (D-053, `prepare.ORIGINAL_GENERATION`,
    `gameshade.prop_decal_alpha`, `prepare.decal_stand_ins`). The game's
    model draws a "4" on the left wing's underside and the emblem on the
    right (textures 991 and 585) and a chevron over the nose (799), blended
    in the decal depth mode at vertex alphas 89, 89 and 191, so they read
    faint and worn. The new generation drops the two decals and prints the
    markings into its wing atlas swapped - the emblem on the left, a black
    "4" roundel on the right - baked into one photograph with no clean wing
    under either, over a reworked body (the private emulator draws it so at
    playtest run 20261013-075405, marks 2 and 4). The shuttle is converted
    from the source's original generation - the game's model, 13 draws on
    8 images - and its three decal draws take the game's alphas (the
    chevron, opaque in the source, drawn translucent). The background kept
    image-less copies of both decals (pool 12, triangles 16 .. 19), drawn as
    pale squares, and withholds them. At marks 2 and 4 the markings stand
    where ORIGINAL's do; at mark 2 their contrast reads 32 and 60 (luma
    p95 - p5) against ORIGINAL's 22 and 42, and 81 and 166 at the source's
    alpha. The shuttle reads brighter than ORIGINAL under the remaster's
    lighting.
  * THE AZTEC CHAIR (door type 9, `DOORTYPE_AZTECCHAIR`). The boardroom's
    eight chairs are doors the game turns about z, as a swinging door
    (`door7F0526EC`); `door_taken` takes them as rigid doors and
    `sl_door_rides` lets the item ride the door's matrix. They stand where
    and as ORIGINAL's do along the desk.

  `door_aztec`, the temple's secret stone door, is authored pale where
  ORIGINAL's is olive: the game's model is olive on every vertex ((110, 110, 85) and
  (92, 92, 71) over texture 141), while the remaster's item - its `new` and
  `original` generations alike - is white over its main face and (74, 74,
  54) on its edges, and the private emulator draws it pale. In the game
  the door is its wall's stone: its shade (106.8, 106.8, 82.5) is 1.317
  times the room wall's round it (81.1, 81.1, 62.7, textures 143 .. 146),
  one hue. D-052 sets the door's tone to the remaster wall's round each
  placement (bound pads 34 and 35) times that ratio
  (`prepare.door_wall_tone`: shade 94.1 91.0 85.3). At playtest mark 1 the
  door reads (48.5, 46.9, 43.9), warm grey, 1.36 .. 1.47 times the wall
  beside it; the game's shade alone (the rule's first form) left it (55,
  55, 42), olive, 1.5 times, and the source's own (131, 130, 131); ORIGINAL
  draws it 1.25 times its wall.
* **The TV walls** (room 23, playtest run 20261012-195917, marks 2 and 3).
  The screens are the game's `tvscreen` props (`prop/tvscreen` holds no
  model data in either source generation, so the game's model draws), and
  their pictures are the game's monitor animation: images of the global
  monitor list, drawn as image-table textures, which the XBLA texture set
  replaces where it carries the remaster's art (docs/texture-packs.md, The
  monitors' images). Measured over reads 200 .. 800 at both marks
  (`SL_TEX_COVERAGE`): the screens draw ids `0245` .. `0248` and `0899`
  .. `089e` - the `drax` panels (shuttles, Earths, galaxies, the target
  Earth) - and the replaced `088b`, `08af` .. `08b1`. Decoded, all twelve
  `drax` images in `new/texture/monitors` are pixel-identical to the
  legacy copies (32x32): the remaster never redrew them, and its own frames
  at both marks show the same art (private emulator). There is no owned
  XBLA picture for them; the walls draw what the remaster draws.
* **The launch-pad decals** (playtest run 20261012-195917, mark 5: "Decals
  on launch pad flicker"). The owner's frame lacks the yellow dashed arrow
  beside the emblem, and shows the exhaust bay open (`ai_11` opens it when
  objective bit 0x40000 is raised, printing "Exhaust bay opening."). In the
  runs here it opened when the run started in the boardroom (room 34) and
  not when it started at the pad, with nothing pressed; what raised the bit
  was not traced. A while after it, explosions burn at the shuttle's base. Reproduced at the mark's eye and look, holding the laser, VSync on,
  SL_RENDER_FPS=240, seed ffffffff8fee6239: over reads 1480 .. 1599, while
  the explosions burn, the arrow's box counted 0 .. 2565 yellow pixels, 43
  reads under half the median, the arrow drawn in diagonal bands or not at
  all - a depth tie with the floor; the owner's mark record and a mark of
  that window list the same extra model submission (`05000ba8`, 48
  triangles). The arrow is the pool-12 draw above (`SL_BG_HIDE` of its mesh
  alone removes it), drawn without the decal pass the source's gate names.
  With the gate's decal pass: 6317 .. 6410 over the same 120 reads; steady
  turning the camera (6018 .. 6228 over 180 reads), holding the PPK (6356 ..
  6443), and with the bay open before the explosions (6313 .. 6424); the
  arrow under ORIGINAL steady throughout. Why the tie went the arrow's way
  until the explosions is not established; the stipple and the alpha
  compare's dither were measured to change no pixel of it.
* **The characters.** `char/jaws`, `char/moonguard` (its head built in) and
  25 heads; the player's `char/djbond` with `head/headbrosnan`. All prepare
  and ride.

## Egyptian's background, props and characters

The game builds Egyptian (stage 32, launcher `egypt`) from `bg_cryp_all_p`,
scale column 23.584906 (`levelinfotable`), so 4.24 item units per room unit.
The source's item for it is `background/temple`: its extents are the game's
rooms' times 4.24 on all three axes (-5100 .. 3700, -1600 .. 2400, -10600 ..
3300), and of the 4851 corners of the game's rooms 4491 lie within one room
unit of a vertex of its original generation (3925 of the new, which adds the
palms and reworks part), with no mean offset (per axis 0.03, -0.05, -0.01
room units; spread 0.30, 0.25, 0.28 - the game's integer rounding of the
source's positions); `multitemple` holds 0 and `aztec` 2. Fitted 4.239944.
The launcher's `temple` is another stage - the multiplayer Temple, stage 38,
`bg_dish_all_p` - so the item is named in the runtime's stage table
(`sl_wa_stage_item_path`), the tools resolve a background item through that
table before its own name (`gameshade.stage_bg`; every other item resolves as
before), and the preparation's launch hint names `egypt`.

```
.\tools\windows\prepare-xbla-assets.ps1 -Level temple -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level egyptian -Skydome
.\tools\windows\prepare-xbla-assets.ps1 -Level egypt -Props -Characters -Player
```

* **The item.** 37 draws over 5 pools (11166 triangles, 17 images); 37
  carried (17 materials, 20 meshes); 2 materials translucent (the water), 1
  cutout (the palm fronds); no decal pass, no animation or material record.
* **THE CHART SHORT OF ITS UNIT** (`xblaworld.chart_short`,
  `prepare.CHART_SHORT_ITEMS`). The remaster adds 19 palms round the temple
  and over its courtyards, which the original generation and the game do not
  have: trunks (pool 3, 703 vertices) and fronds (pool 4, 1292 vertices, 323
  quads on one 64x512 frond image). Their coordinates land on no divisor -
  each frond quad is the whole image, raw (1021, -1), (1, -2), (1020, 1020)
  and (0, 1019), a 1024-unit chart drawn three to five units short of both
  edges; the trunks reach 1010 - so both pools were refused, and with them
  the item: its declared bounds are the palms' (x to -5818, y to 3030, z to
  -11416), and the geometry read disagreed with them. A pool every other rule
  refuses is read at the smallest divisor bounding every value when its
  largest stops within 2% of it: both at 1/1024, and the bounds agree to
  0.000. Named for `temple`: over the source's 24 backgrounds the rule would
  also read Runway's pools 3 and 8 and the multiplayer Caves' pool 2, which
  draw without them as accepted; with it named, every other background reads
  exactly as before (B-EGY1).
* **The palms' trunks** (D-051, extended). The trunks are drawn on image 15,
  the placeholder's magenta with clear texels round it; the remaster draws
  them magenta (private emulator, start courtyard, room 31). They are opaque
  draws and draw by their vertex colour - white and greys, so they read as
  pale stubs under the fronds (B-EGY1).
* **The water.** The hall's pool (rooms 3 and 14) and the channel under the
  temple (rooms 23 .. 27) are the item's own translucent water (draws 31 and
  32, image 13, 32x32) at the game's heights (-14.8 and -1466.0 world units
  against the game's -16 and -1464), over the item's basin and channel floors
  (-29.1, -1473.6). Over the hall's basin it reads dark slate where ORIGINAL's
  reads blue, as the private emulator draws it; in the channel it reads blue.
* **The sky.** The game's row 32 carries a cloud layer under a navy sky; the
  remaster draws its daylight dome over the open courtyards (room 31, the
  start; the pillared hall's open roof, room 3), the palms against it (private
  emulator). The source's eleven domes read identically (one geometry, the
  same two images), so the name decides no pixel; the stage's is named as the
  stages are, `skydome/egyptian` (`skydome/temple` is the multiplayer
  Temple's), `sl_wa_stage_sky_item_path`. XBLA draws that dome as authored -
  no game-owned atmosphere names row 32 - and the palms over the walls, as the
  remaster does.
* **The fade to black** (D-055). After Baron Samedi's second death `ai_24`
  runs `gas_leak_and_fade_fog`, which on this stage only blends the
  environment from row 32 toward row 132 over 120 ticks (`fogSwitchToSolosky2`)
  - black fog from 345 world units, 68% at 1000. The remaster's table carries
  row 132 (black, linear -100 .. 10000) and the replacement blends toward it as
  the game does (#77), but at 11% black by 1000 units its rooms barely
  darkened. Row 132 is presented from 345 to 1550 (`sl_presentation.c`), which
  over four rooms' views (the fade forced at a fixed poll for the comparison)
  darkens the frame 29.4% across the fade against ORIGINAL's 30.5%; row 32 is
  untouched, so every frame before the fade is as before. `SL_XBLA_FADE_FOG=0`
  draws the authored row.
* **The props.** All 14 models the setup places have items: the 25 doors
  (cryptdoor1a .. 4, door_win), 34 collectables (28 KF7s, 5 hatch bolts, the
  Golden Gun), 3 body armour, the golden shells, 7 drone guns. Thirteen are
  taken (the shared ones byte-identical); the remaster's crypt doors are
  translucent at near-zero alpha over the game's shut stone doors, and four
  of the five (`cryptdoor1a`, `2a`, `2b`, `3`) are drawn opaque with their
  faces in the background's own images - the papyrus capitals where the game
  draws texture 406, the relief frieze where it draws 1174, measured on the
  background's triangles in the game's rooms - their frames in their own
  stone (D-054). The secret wall's face into room 10 (`cryptdoor3`), flush
  with room 10's west wall and drawn by the game in the wall's own textures at
  every height (410, 404, 410), continues that wall: its panel and lower strip
  draw the papyrus capitals and its upper strip the carved upper wall, each at
  the wall's own texture map (`prepare.continue_wall`,
  `gameshade.wall_maps`). `cryptdoor4`, all frame, is the game's. It reaches no atlas
  edge at any power of two; every coordinate of its one draw lies on the
  game's texture 410 at 1/8192, and it is named in
  `prepare.GAME_COORDS_STATIC` beside Aztec's desk doors - it is read so when
  D-054 is set aside. The rule reads `background/temple` from the same
  preparation root, so the background is prepared before the props.
* **The Golden Gun's pickup: A PROP'S REFLECTION** (`prepare.PROP_REFLECTIONS`,
  `sl_gfx_dl.c` `chr_reflection`). `prop/chrgolden` draws on two near-white
  512x512 images (mean 248 and 254) with every draw naming a reflection image
  (256x256, mean 143 136 113) and the pixel constants of a reflection blend
  (c12 1.77, c14 -0.77) - the same three images the in-hand `gun/goldengun`
  draws with, gold under the view model's reflection. Without the reflection
  the pickup read silver in its case beside the gold in-hand gun; the game's
  own model, held instead, draws near black under the XBLA texture set. The
  pickup's bundle carries the reflection on its six opaque draws (the writer
  and the loader take a prop's reflection as a view model's), and the prop
  draw lays it as the view model does: each solid, non-decal draw that names
  one drawn again, the image read at the sphere coordinate of the vertex's
  eye-space direction and multiplied in at twice its value. Named: the other
  props whose draws name a reflection (the gas tank, the oil drums, the
  missile) carry none and draw as accepted. The census counts the draws
  (`sl_chrdraw: ... reflected=`).
* **The characters.** `char/rusguard` with 25 heads and `char/baronsamedi`
  (its own head); the player's `char/djbond` with `head/headbrosnan` and the
  stage's 27 weapons. Only `baronsamedi` is new; the rest are byte-identical
  to the accepted bundles.
* **Coverage.** The map's 44 rooms with geometry were teleported into from a
  setup pad inside each, or its bounds centre where none stands (17), four
  headings held at pitch -4, ORIGINAL and XBLA - an initial sweep, taken
  before D-054 and the pickup's reflection (room 20's door and the Golden
  Gun's room are covered again by the focused views of both). Two pads
  registered a neighbour: room 3's pad lies on room 14's basin floor (floor
  -27, registered 14) and room 13's on its edge with room 5 (registered 5);
  points of their own floor (room 3's pad 6, room 13's bounds centre) land in
  them (floor 0, the tile under the point in the room), and both are covered
  from there. Rooms 38 .. 40 are the roof guns' niches beside corridor 41
  (floor 442, 187 units deep; their pads are the guns'): the camera stands
  inside the remaster's closed wall there, not a place the player stands, and
  the remaster's corridor wall in front of them is solid relief as XBLA's is.
  Room 10's nearest pad is Baron Samedi's spawn (pad 150): the camera there
  stands inside his head, so the room is covered from 500 units off it.


Under PRESENTATION XBLA a stage's standard props and its single and
multi-screen monitors - the Dam's mainframes, desks, keyboards, monitors, door
panels, chairs, crates and drums - draw from the remaster's `prop/` items.
`SL_PROP_ASSETS=0` keeps them the game's (the control); ORIGINAL and COMMUNITY
HD ask for nothing.

The game draws such a prop from its model `P<item>Z` (`propobj.c`, the
object render) under the prop's own matrix. The remaster's item is a STATIC
item - no skeleton, one rigid piece, in its own space - so nothing in it says
where the game's model sits. What does is the source's ORIGINAL generation of
the same item: it is the game's model, in the item's space (the two
generations of the Dam's desk, keyboard, monitor and mainframe share one box).
The bundle (class `PROP`) carries that generation's box as its REFERENCE box;
the seam measures the box of the game model's own vertices once per model
file, and `sl_prop_derive` relates the two by the scale their extents agree on
(every axis) and the offset of their centres. Measured: every Dam prop model
at 1/7.

The seam (`sl_chr_prop_begin/end` in `src/native/sl_chr_asset.c`, around the
object render's subdraw) withholds the model's lists and draws the item
through the character bridge with the prop's shade. A monitor's screen
(`Switches[0]`, the node the game animates its text on) stays the game's and
draws over the item. The prop's placement, collision, interaction, damage and
destruction are the game's: a prop in any damaged state
(`objGetDestroyedLevel`), a model with more than one matrix (unless its item
names the parts - a vehicle, below), glass, and the weapons and keys lying in
the level draw as the game's. Doors have a contract of their own (below).

**THE FITTED ITEM (D-038).** An item the remaster authored as another object
has no reference box to match: `prop/chrplastique` is a round mounted device
in both of the source's generations where the game's C4 is a brick. For an
item the presentation names (`sl_presentation_prop_fit`) a box that fails the
test is fitted instead - the smallest scale that keeps the item inside the
model's box on every axis, centred in x and z, its bottom on the model's
bottom - and rides the model's root matrix as any one-piece prop.
`SL_PROP_FIT=0` refuses it as before.

    .\tools\windows\prepare-xbla-assets.ps1 -Level dam -Props

`tools/worldasset/stageprops.py` derives the set from the game's tables: the
setup's object list (standard props, single monitors, vehicles) gives the
models, the
prop file table (`PitemZ_entries`) their files, and the remaster's own
executable - which carries the game's prop file table, found by the records'
scales in the game's order - the item each stands for
(`Pswivel_chair1Z` -> `prop/swivelchair1`). Both generations of each item are
extracted in one pass. The Dam: 13 models (12 standard props and monitors,
one vehicle), 13 PREPARED.

**Facility** takes the same command, `-Level facility -Props`, with nothing of
its own. Its setup adds two kinds:

- **Gas props** (setup type 36, `PROPDEF_GAS_RELEASING`): the bottling tanks
  and gas barrels. The object render draws them as one-piece models, so the
  seam takes them exactly as it takes a standard prop. The gas a shot
  releases, the damage and the destruction stay the game's, and a damaged
  one draws as the game's.
- **Glass** (type 42, `PROPDEF_GLASS`): breakable glass, the lab
  glassware. Taken as a one-piece prop, drawn blended by the image that
  carries its coverage (Glass, below); a shattered piece draws as the
  game's. The kind also holds window panes - the Dam's twenty `PwindowZ` -
  and a PANE is refused by the preparation: an item whose reference box is
  at most 1% of its largest extent thick (`prepare.PANE_THICKNESS`; the
  window item is one quad, 89.9 x 81.0 x 0.0). So no stage's preparation
  can write `prop/window`, and a window stays the game's own glazing
  everywhere.

Of Facility's 14 standard, monitor and gas-prop models, 13 are PREPARED:
boxes, consoles, desks, chairs, crates, lockers, lab benches, the door
consoles, the gas barrels and the tanks. `prop/tvscreen` is refused because
the remaster's bundle for it holds neither a model nor an image, in either
generation (an empty rendergraph), so the four TV screens stay the game's.

The lockers needed one reader rule. `prop/locker3` samples a wrapping image
at v -16384 .. 16384 (at 1/16384), with u reaching 16384. That is exactly
one repeat either side of zero: the image shown twice down the door. The
wrapped-lip rule (THE DYNAMIC DOOR, below) took only draws starting above
-1 repeat. It now also takes a draw that starts exactly at -1, which reads
the lockers at 1/16384, the scale their original generation reads at. A draw
reaching past a whole repeat below zero is still refused, and every bundle
the rule read before is byte-identical.

**Runway** (#78) takes `-Level runway -Props` the same way. Of its eleven
models the five shared with earlier stages (keyboard, oil drum, monitor, gas
barrels, swing door) regenerate byte-identical, and six are new: the filing
cabinet, the metal chair, the office table, the Sea Wolf missile launcher on
the bunker roof, the hangar's three roller doors (a dynamic door, D-027) and
the guards' fur hat. Two needed a reader rule each, both reached only where
the item was refused before:

- **The rounded zero.** `prop/woodentable1` tiles its top from raw -12272 to
  22333 and lays one quad over the whole of its second image at u 0 .. 4100,
  v -1 .. 4094 - one raw unit below zero, so no draw stayed inside zero and
  no scale was found. When every rule refuses, a draw whose smallest
  coordinate is exactly one raw unit below zero is measured from zero
  (`xblachar.texcoord_scale`); the table reads at 1/4096. Two units below
  zero is still refused.
- **A buffer of positions alone.** The ORIGINAL generation of
  `prop/destseawolf` - the reference box, never drawn - draws one buffer of
  12-byte vertices, a position and nothing else. The box needs only the
  positions, so `read_static` takes that stride when it reads for the box
  (`STATIC_POSITIONS`); an item that would draw from such a buffer is still
  refused.

None of the six models is drawn on the Dam or Facility, so neither changes.

Measured at the 30 marks of a Facility XBLA replay (teleport only, seed
pinned):

- The new build with no Facility bundle is byte-identical to the old one at
  all 30.
- With the props prepared, 8 marks change: the boxes, the door consoles, the
  lockers, the lab benches and the gas barrels. With `SL_PROP_ASSETS=0` the
  same 8 frames are byte-identical to before, so every change is the seam's.
- ORIGINAL and COMMUNITY HD are byte-identical at all 30.

Measured at the tunnel marks (the mainframes; the desk, keyboard, monitor and
chair): every changed pixel lies inside the game prop's silhouette or the
item's, no pixel of a game prop survives beside its item, and with
`SL_PROP_ASSETS=0` the frame is byte-identical to before.

A prop's DECAL PASS is the level item's (see SL_WA_MAT_DECAL): a draw under
the source's style bit `0x2000` - the crates' graffiti, style `0x2702` - is
a quad authored exactly on the face it marks (0.0000 item units apart on
the Dam's `prop/boxes2x4`), so the preparation marks its material DECAL and
the item pass draws that mesh alone with the decal polygon offset the
background pass uses (`sl_wa_decal_priority`; `SL_BG_DECAL=0` turns it off
here too). On the Dam: `boxes2x4` 3 draws, `metal_crate3` 1,
`wood_sm_crate4` 2.

A blended draw's COVERAGE is its pixel program's, not necessarily its
image's alpha (SL_WA_MAT_COVERAGE_BLUE). The `boxes2x4` graffiti images are
opaque DXT1 (no block uses the one-bit transparent index) - grey glyphs on
black - and the program their draws bind fetches the image WITHOUT its alpha
and exports `alpha = blue * colour.a * max(dot(lit rgb, 1/3), 1)`, the last
factor exactly 1 under the game's light state; blended SRC_ALPHA /
ONE_MINUS_SRC_ALPHA, alpha test off, no depth write (measured on a draw of
the same program in the running remaster, whose fetch swizzle is the
identity). So black is transparent and the glyph's grey is its own graded
coverage. The preparation reads that from the draw's own program
(`xblaworld.coverage_channel`: the base-image component that reaches the
exported alpha through the multiply chain; draw -> program through the
rendergraph's shader objects, `xblaworld.draw_coverage`) and marks the
material; the item pass samples alpha from blue for it with the texture's
alpha swizzle, the image left as the source has it. `SL_WA_COVERAGE=0` draws
the image's alpha (the earlier black square). Across the remastered props
whose programs map (262 of 317) the family covers 16 blended draws (9 decal,
7 translucent); `metal_crate3` and `wood_sm_crate4` bind programs that use
the image's alpha (DXT1 punch-through) or none, and are unchanged. On the
Dam the only change is the three `boxes2x4` materials.

Not here: the weapon a character holds is drawn inside the character's own
render walk (an attached model), not by the object render - it stays the
game's.

**A stage's accepted set.** The derivation grows as the runtime takes more
kinds - the rigid doors, the worn hats - and a stage whose look has been
accepted must not change because its props are prepared again. Run on the
Dam after those two kinds landed, `-Level dam -Props` added
`prop/damtundoor` (a rigid door) and `prop/hatpeaked` (a worn hat) to the
set the Dam was accepted with. A stage may name, in
`data/overrides/<stage>/props-held.txt`, the models the derivation takes
that are held back on it - one item per line with its reason
(`stageprops.held_items`). The preparation lists them as HELD and prepares
nothing for them; `stageprops.py inventory` marks them. The Dam holds those
two. Measured: `-Level dam -Props` into an empty root prepares the Dam's 19
bundles byte-identical to the accepted ones and nothing else. A bundle is
shared by every stage that draws its model, so a held item is only not
ADDED by that stage's preparation.

### The moving prop: a vehicle

The Dam's truck is a VEHICLE (setup type 39, model `PmiltruckZ`, record 279),
drawn by the same object render as any prop but from a model that moves
parts of itself: five matrices, the game's skeleton `car` naming them
`base, wheel1..wheel4` (assets/embedded/skeletons/props.inc.c), the vehicle
branch of the object render turning all four wheels and steering the front
pair every frame. The remaster's executable pairs record 279 with
`prop/miltruck`, and that item is not static: it carries a joint per moving
part, named as the game names that matrix (`base`, `wheel1..wheel4`, lower
case, in the item's own name table right after its joint records), its body
on `base` and each wheel's triangles on its wheel's joint. The joints sit
where the game's GROUP nodes sit at one scale (the base joint is the root
GROUP's origin x 0.328, the front wheels' pivots the same), and the item's
original generation is the game's model in the item's space on all three
axes.

So the contract is the rigid prop's, widened by one rule. The preparation
reads the skeleton's names beside the model (`stageprops.matrix_names`: the
model header names the skeleton, the name table directly above its joint list
names the matrices), marks each item joint that bears one as riding that
matrix (`SL_WA_PART_MATRIX + 1 + m`, as the watch hand does), a nameless joint
its parent's, a nameless root none. The seam relates the item to the model by
the reference box exactly as for a rigid prop, and sets each part's joint at
the model's own node for its matrix - the wheel turns about the game's pivot,
under the matrix the game computed. The truck's path, speed, wheel turn,
steer, collision, damage and lifetime stay the game's. A one-piece item on a
model that moves parts is still refused, as is a joint named for a part the
model places more than a tenth of the item's extent away.

The item's reader needed two things the characters never had: the
lower-case names (read from the name table itself, `read_pose(table_names)`)
and one draw in the static world layout (stride 24) inside a skinned item,
which rides the root joint. On the Dam the set is 13 PREPARED; the other 12
bundles are byte-identical.

Measured at the playtest's truck mark (teleport, the truck arrives and stops at
the player by tick ~700; seed pinned, `SL_VI_CATCHUP=0`, reads 500/600/700):
the frame changes only inside the game truck's silhouette or the item's (0
pixels outside both, against a scratch bundle that draws nothing), no list of
the game's truck is emitted (`kept=0 withheld=6`), a repeat run is
byte-identical, the build with no truck bundle is byte-identical to before,
and ORIGINAL and COMMUNITY HD are unchanged. Not settled: the windshield's
material names two images of one size - a light-flare environment image in
slot 0 and a dark glass in slot 1 - and the base-image rule (largest, lowest
slot on a tie) takes the flare; the draw's program could not be placed to say
which is the base.

### Runway's ammo boxes, autoguns, plane and tank (#78)

Four more setup kinds are drawn by the same object render, and the seam now
takes them (`sl_prop_takes`; `stageprops.TAKEN`):

- **The ammo box** (type 20, `PROPDEF_AMMO`): a one-piece crate the player
  collects. Its item is placed by the box as any rigid prop's; the pickup,
  and the crate's removal, are the game's.
- **The autogun** (13), **the aircraft** (40) and **the tank** (45): moving
  props, taken as the vehicle is. The object render computes their parts'
  matrices from the game's own state every frame - the autogun's yaw and
  pitch as it tracks, the plane's rotor and its animation, the tank's turret
  and gun from `turret_orientation_angle` / `turret_vertical_angle` - and
  each item joint rides the matrix its name names. A destroyed aircraft or
  tank draws as the game's (`objGetDestroyedLevel`), as every other prop
  does; a destroyed autogun keeps its item ("The destroyed autogun" below).

The names. `stageprops.matrix_names` reads them as for the truck, with two
corrections the skeletons needed. The autogun's skeleton has six joints and
the gun five matrices, and the aircraft's five joints serve a three-matrix
plane: a skeleton may serve a model with fewer matrices than joints, and its
names then name the model's matrices in order (tank `SKEL_TOP, SKEL_TURRET,
SKEL_HATCH, SKEL_GUN, SKEL_MUZZLE`; autogun `SKEL_TOP, SKEL_YROT, SKEL_XROT,
SKEL_BARREL, SKEL_CARTRIDGE`; plane `SKEL_POSITION, SKEL_TOP, SKEL_ROTOR`). And
the name table directly above the autogun's joint list is a second one under
`#if 0` (`SKEL_BAR2` for the cartridge); a block under `#if 0` is skipped.
The remaster's items carry exactly those joint names.

The plane's parts. Its root is a HEADER node (the part its animation
positions) and its rotor a GROUPSIMPLE, each drawn under the matrix it names
(model.c, `modelFindNodeMtxIndex`); the seam's model reader registered only
GROUP nodes, so the plane was refused. It now registers those two kinds as
well, at the origin of the GROUPs above them (`prop_box`). No item that
rode before reads a different node: a rigid prop and the truck have neither.

Measured at the replay marks (teleport, seed pinned, read 80): the ammo boxes
(marks 1-4), the tank (16), the autogun (20) and the plane (21, 22) draw the
remaster's items; each moving item's joints passed the pivot check (every
named joint within a tenth of the item's extent of the game's node for its
matrix). Without the invisibility cheat the autogun at mark 20 turns to
track the player over 400 frames and the item's barrel points where the
game's does in every frame, under XBLA and ORIGINAL alike.

The autogun's barrel and muzzle face draw black. That is the item's own
data: those draws carry a vertex colour of zero, and the program they bind
multiplies the lit image by it (`(c196 + sat(N . c198) x c197) x colour x
image`, then the tint and the stage fog), so the remaster draws them black
too under the game's light state. Nothing compensates for it here.

With them `-Level runway -Props` prepares sixteen models: the eleven above
byte-identical, and `ammo_crate2`, `ammo_crate3`, `gun_runway1`, `plane` and
`tank`. Dam and Facility: neither setup places any of the four kinds
(inventories unchanged), and their replay witnesses are byte-identical.

### The destroyed autogun (#78)

The remaster has one sentry-gun item (`gun_runway1`; the source index lists
it once, no destroyed variant), and the console keeps drawing it once the
gun is destroyed: measured on the running remaster with the gun's object
record set destroyed the way `objExplode` sets it (state `PROPSTATE_DESTROYED`,
damage reset), the same model stands, its barrel black as before and its
mount darkened. The prop seam handed every destroyed object back to the
game (`objGetDestroyedLevel != 0`), so under XBLA a destroyed sentry gun
swapped to the cartridge's model - a grey, textured barrel in place of the
item's black one. (The item's black barrel is itself a defect of the
source's data, and is lifted at preparation: "The sentry gun's unseen
images" below. An intact gun now stands with its shaded-metal barrel and
five-hole muzzle, and the destroyed one must read as destroyed against
that.)

A destroyed autogun now keeps its item (`sl_prop_destroyed_keeps`; the
header's THE DESTROYED AUTOGUN). Destruction stays the game's: the state,
the damage count, the gun's silence (`PROPFLAG_IS_DRONE_GUN`) and the
explosion are `objExplode`'s, and the presentation reads one value,
`objGetDestroyedLevel`. The game's own destroyed shading is `objDeform`'s
(propobj.c): the model's FIRST display list - the first `DLCOLLISION` node
in tree order, the gun's mount under the root matrix - gets a copy of its
vertices, each blackened by chance (nine in ten above the part's mid-height,
one in five below) once per destroyed level, jittered and shrunk; the
barrel and the gun, under the model's other matrices, are left alone. That
is why the cartridge's destroyed gun shows a grey barrel over a black mount,
and why the console's darkens only the mount. The item's vertices are its
own, which the game never sees, so the item takes the share objDeform's
chances leave: every vertex bound to the matrix the deformed list is drawn
under (`destroyed_mtx` on the bridge record; the heaviest skin lane
decides) draws its colour scaled by 0.1^L when it lies at or above the
part's mid-height and by 0.8^L below it - the mount's upper half goes
black at the first level as the game's does, its lower half dims - and the
rest of the item as it was. The mid-height is the middle of the riding
vertices' range along the model's y, taken through the joint's relation
(`sl_prop_destroyed_shade`, `sl_prop_destroyed_height`,
`sl_prop_destroyed_vertex`, pure, self-tested). A first version averaged
the two halves into one share (0.45 at level 1), which darkened the mount
without making it read as wrecked beside a barrel that now has its
texture; the halves are what the game draws. The game's jitter and
shrink, and its upside-down and small-explosion cases, are not carried:
the item keeps its shape. The prop report prints the level (`destroyed=`),
and the renderer census counts the parts shaded (`destroyed-shaded=`).

Every other destroyed object - the bottling tanks of Facility among them,
whose crumpled, blackened model is the chosen presentation ("The destroyed
tank" above) - draws as the game's, as before.

### The sentry gun's unseen images (#78)

The intact sentry gun stood with a pure black barrel - "it always looks
destroyed". The source's programs draw an image multiplied by the vertex
colour, and the item's barrel meshes (26, 27, 28: the muzzle face on a
256x256 image of a five-hole disc, the barrel on a 256x128 image of shaded
metal) carry a vertex colour of (0, 0, 0) on every vertex. The running
remaster draws the same black barrel (measured at the marked pose on the
console build), so this is the item's data, not a reading error - but it is
a defect of that data, not a shade: an authored image that no draw could
ever show was meant to be seen.

The rule is the data's own, not the gun's. Over every prepared bundle
(112, `census.py`), the only images drawn by nothing but wholly black
opaque draws are those two. The wholly black meshes elsewhere all show an
image some other draw of the same item shows lit - a chair's base, a
rifle's muzzle recess, the gun's own housing face plate (mesh 0, image 0,
the housing's image) - and those are shades of their surface, kept. A
translucent black draw (the Dam's shadow decals, a weapon's dark glass)
never counts as showing nothing. So `xblachar.unseen_images` names an
image UNSEEN when every draw naming it lies in the opaque pass and reads
only (0, 0, 0) vertices, and `xblachar.lift_unseen` reads those draws with
the lane lifted - white, the identity of the multiply, alpha kept - copying
any vertex such a draw shares with a draw that keeps its black (pure,
self-tested); `convert_prop` applies it to every prop and says so in its
summary (`lane lifted on unseen images 1 2`). Re-preparing Runway's props
changes `gun_runway1` alone; the other fifteen come out byte-identical, and
the rule is dormant on the Dam's and Facility's props by the census.

### Monitors, screens, and what is not taken

A monitor's SCREENS stay the game's: the nodes it animates the screen text on
(`process_monitor_animation_microcode`) - `Switches[0]` on a single monitor,
`Switches[0..3]` on a multi-screen one such as the Dam's gate door panel
(`Pdoorpanel`) - keep drawing over the item (`sl_prop_takes`).

The item carries a screen surface of its own: a static image lying where the
game's screens lie (`prop/doorpanel` mesh 2, `prop/modembox` mesh 1), 0.12
model units behind the game's screen quads. Depth alone settles which one
shows, so the item must be drawn with no depth offset but its own. The item
bridge (`chr_asset_draw`, and `vm_asset_draw` for view models) therefore
disables the polygon offset on entry. Before that it inherited the offset of
the last native triangle drawn ahead of it. After a bullet impact, a decal
drawn with (-1, -2), the whole item was pulled toward the eye by its depth
slope.

Measured on the gate panel beside gate A under XBLA:

- After two shots into the gate, drawn just before the panel, the depth
  stored over the screens stood about 480 depth steps in front of the game's
  screen at 190 eye units. That is the slope term at those pixels. The mark
  record of run 20260930-211359-lvl33 (mark 1) agrees: the game's red screen
  was submitted there, and the item's grey screen (texel 103, 103, 103) was
  what showed.
- The slope term falls off as 1/z and the 0.12-unit margin as 1/z^2. On the
  line between that run's two marks, the game's screen won again only from
  about 70 eye units in.
- With the fix, every frame keeps the screen's colour wherever ORIGINAL shows
  it, with or without the shots, at both ranges. This holds for all four door
  panels and for the colour changes the game's door state drives.
- The fix touches no presentation that draws no item, so ORIGINAL and
  COMMUNITY HD are byte-identical.

**THE SCREEN'S PLACE.** On Facility's console bank (`prop/console_sev2b`)
the game's screen crossed the item's bezel at the top and bottom and left
the item's grey tube showing at the sides. Measured: the item sits on the
model exactly (scale 1/7, the reference box), and the game's screen quad
(`Switches[0]`, a four-vertex collision-list node the game re-textures each
frame) lies on the item's monitor face, 0.03 item units in front of it. What
differs is the rectangle: the item lays its own screen as one quad of its
face draw whose texture coordinates span the whole image - item x 107.6 ..
192.4, y 192.2 .. 243.2 - where the game's quad covers x 111.1 .. 187.6, y
188.7 .. 246.0. The remaster draws the screen there; the game's screen is
narrower and taller.

So a kept screen is drawn on the item's screen rectangle
(`sl_screen_place`): of the item's triangles, a pair laying the four
corners of the whole image, every corner within 5% of the screen's
diagonal of the screen's plane, overlapping the game's quad by at least
half; the game's four vertices move in their own plane onto that rectangle.
That plane is not the face's: the two cross (below), so each corner is then
kept in front of the item's corner under it, by 0.0002 of the diagonal plus
the face's own twist, and set on whole units that keep it there - the front
being the side away from the item's box centre. A rectangle equal to
the game's within 0.1% of its diagonal changes nothing. The game's vertex
buffer is not touched - the hit tests read it - the seam registers the
frame's buffer with the renderer (`sl_gfx_screen_place`) and the load of
that buffer takes the place. The Dam's door panels lay their screen surface
in one flat colour, not a whole image, and are left as they were.
`SL_PROP_SCREEN_PLACE=0` keeps every screen where the game puts it.

Measured under XBLA, teleport only, seed pinned: at the console bank mark
the black panel sits in the item's tube with no bezel crossed; another
console of the same model seen from three sides changes only on its
screen; the door console (`prop/doorconsole`) is placed on its small
display by the same rule, and its frames at four views from 160 units are
unchanged; the
Dam's 27 witnesses, its door panels among them, are byte-identical in all
three presentations, and ORIGINAL and COMMUNITY HD are byte-identical at
every Facility replay mark.

The two planes cross. Measured on `prop/console_sev2b`: the item's face
stands 0.15 model units in front of the game's quad plane at the top and
0.22 behind it at the bottom, and the top vertices rounded to 0.48 behind
it; `prop/doorconsole` 0.30 in front at the top, 0.34 behind at the bottom.
The kept screen's decal priority covers less of such a gap the nearer the
eye, so the top of the screen showed the item's face, more of it as the
player walked up - the console's black panel over the top rows of text, the
door console's fingerprint image over the top of its display. Kept in front
of the face, both screens draw whole at every distance swept (85 units in
front of the console bank mark to 300 behind it), and the door console's
display matches ORIGINAL; `SL_WA_NEARKEEP=0`, `SL_PROP_TWOPASS=0` and
`SL_Z_REDRAW=0` change nothing at those poses.

A door is not a rigid prop: the doors the game reshapes as they move are the
dynamic doors below, and the ones it moves whole are the rigid doors. Glass,
and the weapons and keys lying in the level, stay the game's.

### Glass

A translucent draw of a static item whose material names more than one image
is coloured by the image that carries its coverage (`xblachar.coverage_base`).
`base_image` takes the largest image, which holds for the items it was
measured on and not for glass: over every prop item of the source whose
translucent draws name more than one image (the gas plant's glazed door and
window, the lab glassware, the Severnaya and train doors, the jeep's
glass), the pair is a fully opaque environment image - the 256x256 sphere
image, or the 54x54 one every lens in the source shares - and a small image
whose alpha is the pane's coverage, flat at 102 or 119 on most. Taking the
larger drew the environment image as the pane's colour: the gas plant's
glazed door (`prop/gasplant_clear_door`, a rigid door) drew black with
highlights. The coverage image is taken when exactly one image the material
names has alpha below 255; two (the train door's) or none leave the largest.
A prop carries no reflection layer, so the environment image is not drawn.
Characters, heads and weapons are read by the skinned reader and are not
touched: a lens or a watch glass keeps its earlier image.

Of the stage props prepared today only the glazed door changes. Facility's
windows are tinted glass (`PROPDEF_TINTED_GLASS`, the game's own pane),
which is not taken.

**The lab glassware.** Facility's glassware is glass (`PROPDEF_GLASS`):
`glassware1` x6, `glassware2` x12, `glassware3` x8, `glassware4` x6. Its
translucent draws pair the lens image every glass in the source shares with
a flat tint (`glassware1` 256x256 (90, 117, 131) at alpha 102,
`glassware3` and `glassware4` 32x32 (115, 153, 148..150) at alpha 119), which
coverage_base takes as the colour. Three of them were refused by the
reader: their glass draws sit below zero and reach no atlas edge, and for
`glassware1` and `glassware3` that draw is the item's only one. A draw
whose image is one colour (every channel within `FLAT_SPREAD`, 8 levels:
the block compression's spread) draws the same at any scale, so where the
two scale rules find nothing the reader measures without such draws, and
an item made only of them keeps its coordinates as read
(`xblachar.texcoord_scale`, THE FLAT DRAW). Every item the two rules
decide reads as before. `glassware1`, `glassware3` and `glassware4` are
PREPARED; every other bundle regenerates byte-identical.

`glassware2` (the capped tubes) stays the game's. Neither of its images is
laid out by stored coordinates, because it has none. Read from its own
programs (the Xenos microcode of its three vertex-program variants and six
pixel programs):

- its vertex record is 20 bytes: position (three floats), the NORMAL
  (2_10_10_10, signed, normalised) at +12 and the vertex COLOUR (8888) at
  +16 - no texture coordinate is fetched; the reader had taken the normal
  word for one;
- the vertex program exports the normal through the modelview rows
  (c0..c2), normalised, the eye-relative position (the fog distance) and the
  colour;
- both pixel programs sample their image at (0.5 + 0.5 n.x, 0.5 - 0.5 n.y)
  of the normalised view-space normal - a sphere map;
- the glass multiplies the sample by the colour and the light
  (c196 + sat(n . c198) c197), then the stage fog, and takes its alpha as
  (1 - blue) of the sample times max(mean of the lit colour, 1);
- the cap is opaque: the same sphere sample times a material colour
  uploaded by the item (c12), with a specular term from the light's
  reflection (c14, c15).

Drawing that needs the item's normals in a prop bundle, coordinates
generated per frame from the view-space normal, and the inverted-blue
coverage: a material class of its own, not a reader rule. It is not built.

Measured at two replay marks in the lab, teleport only, seed pinned: the
flask, the beaker and the rack draw the remaster's tinted glass over the
bench and the wall behind; the capped tubes are the cartridge's. The prop
carries no reflection layer, so the lens image laid over the tint in the
remaster is not drawn.

**THE PROP'S TWO PASSES.** The game draws its objects in two walks: the
solid lists of every object in one, then, after every solid surface of the
frame, the translucent lists in a second (`chrobjRenderProp`'s flags: 1
solid, 2 translucent, 3 both for a fading object, drawn in the second walk
only). An item had been drawn whole in the first walk, its glass blended
there and writing no depth, so anything solid drawn later in the same walk
painted over it: the rack of tubes on a lab bench (`glassware4`, its 480
glass triangles) was covered by the bench item drawn after it, and only a
few fragments showed where nothing was drawn later. A prop item's record
now carries the walk it belongs to (`sl_chr_draw.passes`): the seam emits
one in each walk the item has draws for (`sl_prop_item_passes`) and the item
pass draws that walk's meshes only (`sl_chr_pass_draws`). A CUTOUT draw
writes depth and is solid. `SL_PROP_TWOPASS=0` draws the item whole in the
first walk, as before.

Measured at the lab mark, teleport only, seed pinned: the rack's five tubes
draw in front of the bench where before only the rack's frame did; the
first wrong stage was the order, not the depth test, culling, coverage or
the gas fog.

### The worn hat and the body armour

**Body armour** lying in the level (setup type 21, `PROPDEF_ARMOUR`) is a
one-piece model the object render draws, so the seam takes it like a
standard prop until the player picks it up. The pickup itself is the game's.
Facility's vest (`PbodyarmourvestZ`) rides at scale 4.70.

**A worn hat** is not drawn by the object render. `chr.c` places the hat's
model on the head, with the game's per-head offsets, from the head's node
(`instcalcmatrices`), and the character's joint list draws it after the
body. The character seam therefore takes it (THE WORN HAT,
`src/native/sl_chr_asset.c`):

- when an item exists for the hat's model (`prop/<model>`, related to it by
  `sl_prop_derive` like any one-piece prop), it rides the hat model's own
  matrix and every list of that model is withheld;
- the body and head follow their own rules, independently;
- a fading character keeps the game's hat, as it keeps its body;
- a hat shot off is a free object again, setup type 17, and the object render
  draws it with the same item.

Two readers needed one rule each:

- A hat's model has no GROUP. It is rooted on a GROUPSIMPLE whose matrix is
  its `Group1`, as `model.c` reads it, so the model's box takes that root when
  no GROUP exists. Every model with a GROUP is read exactly as before.
- The remaster's hat items are authored on the character's skeleton (five
  joints, `SKEL_POSITION` .. `SKEL_HAT`) with every vertex on one joint. The
  preparation reads such an item as one rigid piece; an item skinned over two
  joints is still refused for a one-piece model.

Facility's guards wear `PhattbirdZ` (47) and `PhathelmetZ` (4); the side cap
rides at scale 4.70.

#### The hat's seat on a remastered head

Riding the hat model's matrix put the remaster's cap on some heads and not
on others. The matrix is the body's hat node (`Switches[6]`) times
T(21.3 x offset) x S(size), from the hat table's entry for that head and hat
type (`headHat_array_8003E464`, `chr.c`). The entries were fitted to the
cartridge's heads, and the remaster's heads are different shapes.

What the remaster does, measured in its executable (`default.xex`):

- It carries the same 168-row table, byte for byte, and only that one.
- One routine reads it, and that routine is the cartridge's hat placement
  with one condition added. It applies the entry only while a settings byte
  is nonzero; otherwise it uses offset 0 and size 1.0, which is the node's
  own matrix.
- Its hat and head items are authored in one skeleton space. The native hat
  model's origin is the neck joint (`SKEL_NECK`) of that space, and its root
  node the item's `SKEL_HAT` joint.

The fit, measured over all 28 heads of the table, with each item placed in
the node's frame and the head's top surface compared with the cap's over
the cap's footprint seen from above:

- **With the entry**, the remaster's side cap sinks into the head over 98% of
  its footprint on `graham`, `joe` and `joe2`, 96% on `pete`, 63% on `b`,
  51% on `martin` and 43 to 46% on `duncan` and `dwayne`. That is the hair
  showing over the cap in the screenshots. The same entries seat the
  cartridge's cap on the cartridge's heads.
- **Without it**, 24 heads show at most 12% of the footprint through the cap,
  and the remaster's fullest hair still comes through: `neil` 22%, `mark`
  21%, `pete` 20%, `bike` 30%.

So a hat item drawn on a head item now takes the entry back, as the
remaster does with its own heads (`sl_hat_unseat`). It then rises by the
hat's REST on that head (`sl_hat_rest`): how far it must rise for the head
to stay under it at the 98th percentile of the footprint. The rest is
derived from the two items once per pair and is 0 when the head already
clears. Measured rests for the side cap: `neil` 24.5 game units, `mark`
15.25, `steveh` 8.0, and 0 for `graham`, `robin`, `joe2` and `dwayne`. For
the helmet: `neil` 25.25, and 0 for `robin` and `graham`. On the cartridge's
own head the hat item keeps the entry. A hat shot off is a free object, so
its record carries no seat.

What this does not settle:

- Which value of the remaster's settings byte selects its remastered
  models. The polarity follows from the fit above, and the remaster itself
  was not run.
- Up to 2% of a cap's footprint can still show hair, at the front of
  `neil`'s side cap from one side.
- A shot-off hat was not exercised: the synthetic shots hit the body.

The seam is the same on every level: Jungle's (40) and Statue's (4) helmets
take it. Bunker 1 and the Archives wear `PhattbirdbrownZ`, for which no item
is prepared.

### The dynamic door (D-027)

The Dam's yard gates (`Pdamgatedoor`), the roller door (`Pdoor_rollertrain`)
and the gas-plant doors (`Pgas_plant_met1_do1`) carry `DOORFLAG_CLIP_TO_BBOX`
on a sliding or vertical door. Measured on the yard gate B (setup door 268,
the one playtest mark m3 stands at):

- **The game's state.** `openPosition` runs 0 .. `maxFrac` (0.95) at
  `maxSpeed` 0.00311 a tick. The door's matrix moves by `openPosition` times
  its travel (650.6 world units along the gate), and `doorUpdateBbox` raises
  the MODEL-space box's `xmin` from -253 by 506 times `openPosition`. That box
  under the moved matrix is the collision polygon: closed it spans
  x 16735.2 .. 17385.7, fully open a 31-unit stub at the east jamb
  (17354.7 .. 17385.7). `perimFrac` is 1.0, so the collision is never dropped.
- **The game's geometry.** One `DLCOLLISION` list, 44 vertices in 11
  axis-aligned quads. `doorBuildClippedVertices` clamps every vertex past the
  box's moving face onto it and rebuilds its S/T along the quad's edge, so the
  part slid past the east jamb is not drawn and the far end cap rides the
  face. What is drawn is exactly what collides.
- **Two gates, one airlock.** Gates A (setup 267, open at level start, closed
  by a guard's `door_close`) and B are linked with flags2 0x40000000: opening
  B closes A first and B waits. The gate panels (`Pdoorpanel`, linked props)
  are the player's switch.

The contract (`src/sl_chr_asset.h`, THE DYNAMIC DOOR) reads the live box at
the draw and nothing else - no timer, no door state of its own:

- **ORIGINAL and COMMUNITY HD** draw the game's own reshaped lists with their
  own artwork. The seam brackets them (`SL_DOOR_DL_W0`), and inside the
  bracket a triangle the near plane cuts takes the eye-crossing population's
  path in `emit_tri` - kept, its sub-near part drawn at the near depth - so the
  gate the camera touches stays closed. Nothing outside the bracket moves.
- **An item** (`prop/damgatedoor`, class `PROP`, placed by `sl_prop_derive`
  as a rigid prop is) is cut at the same half-space every frame
  (`sl_door_cut_mesh`): triangles across the face are clipped with every
  attribute interpolated, triangles wholly past it are laid onto it (the
  game's clamp, generalised past quads; edge-on ones vanish), so the item's
  far end closes the gate where the game's does. The game's lists are all
  withheld; the item draws double-sided with the rigid props' near-cut fill.
- **The artwork.** COMMUNITY HD replaces all three of the gate's textures
  through the texture provider's own lookup (0x048f 32x32 -> 512x512, 0x055a
  and 0x055b 64x17 -> 640x170); nothing falls back. XBLA draws the
  remaster's item.

The item's texture coordinates. The gate's one draw samples a wrapping
512x512 image with coordinates on exact quarters of 16384 and a lip below
zero (-1219, -62). The reader's first rule measures only draws that stay
inside zero and so refused it; the WRAPPED LIP rule
(`xblachar.texcoord_scale`) applies only when that rule finds nothing: the
largest power of two at which more than half of the item's vertices lie on
wrapping draws within one repeat of the image, one of them reaching the far
edge. The gate reads at 1/16384, the scale its sibling door items reach with
no lip; the chain-link door's draws, which run past a whole repeat below
zero, are still refused, and no item the first rule measures reads
differently (every other bundle byte-identical).

Measured, teleport and the synthetic pad only (the panel pressed at m1, the
game's own airlock and auto-close doing the rest): the per-tick door state is
identical across two boots and across ORIGINAL, COMMUNITY HD and XBLA; the
leading edge of all three presentations lands on the same pixel column at
every state, opening and closing; Bond pressed against the gate passes when
the collision edge clears him, at the moment the drawn edge does; nine live
presentation switches at closed, half-open and open leave the door state
byte-identical to the run without them. The roller door (vertical) and the
gas-plant doors follow the same contract with no change of their own;
Facility's security doors are the gas-plant door's model, so under XBLA they
draw its item too.
`SL_DYNDOOR=0` is the whole contract off (D-027): every door as the cartridge
draws it, byte-identical to the build before.

**The rigid door.** Three of Facility's fifteen gas-plant doors (setup pads 93,
113 and 116, the decoder-locked doors into the labs) are sliding doors without
`DOORFLAG_CLIP_TO_BBOX`. The game never reshapes them: the whole model moves
under its matrix and the box is left alone. Such a door's item rides that
matrix whole, the way a rigid prop's item does - no cut, every list of the
model withheld (`sl_door_rides`, `door_ride`). Without an item the game's lists
draw untouched, so ORIGINAL and COMMUNITY HD do not change. Before this, the
three drew the cartridge's door under XBLA while the twelve clipped doors drew
the item (B-FACDOOR).

**The swinging door** is the same case. The game never reshapes a door of
type 5 (`DOORTYPE_SWINGING`) without the flag: `door7F0526EC` turns the whole
model about a hinge it builds from the door's pad into the door's matrix,
and the box is left alone. So its item rides that matrix whole as well.
Facility has three such models, each with an item:

- the 18 swing doors (`Pgas_plant_sw2_do1Z`);
- the 9 toilet cubicle doors (`Pgas_plant_wc_cub1Z`);
- the 4 glazed lab doors (`Pgasplant_clear_doorZ`, `DOORFLAG_WINDOWED`), whose
  item carries its own steel frame and blended glass.

**The parted door** (#96) is the eye and iris door of Caverns' airlocks
(types 6 and 7, `DOORTYPE_EYE` / `DOORTYPE_IRIS`, without the flag): the game
leaves the box alone and turns the door's leaves under matrices of their own,
so an item whose joints name those matrices rides them as a moving prop's
does (`sl_door_parts`), and a rigid item is refused. See Caverns' section
above.

The preparation now takes every door whose model the runtime rides. That
includes the Dam's tunnel and chain-link doors; the Dam holds its tunnel
door back (A stage's accepted set, below), and its chain-link door is
refused by the reader, so the Dam does not change.

Measured on a swing door, teleport and the synthetic pad only (use pressed,
then a walk through the doorway): the per-tick trace is identical under
ORIGINAL and XBLA (5368 lines through read 900). Frame by frame, the item's
leading edge lands on the game's edge column while the door swings open. At
the 30 replay marks the three door models change XBLA at 11 marks: the
cubicles, the swing doors and the lab doors. ORIGINAL and COMMUNITY HD are
byte-identical.

### The chain-link fences and doors (not taken)

The Dam's chain-link is two different things:

- **The static fences are room geometry.** Under XBLA the replacement world
  draws them, so they are the remaster's own fences.
- **The chain-link panels with a metal frame are doors** (`Pdamchaindoor`,
  `PROP_DAMCHAINDOOR`, nine instances). This includes the padlocked two-leaf
  gate: its two leaves stand at (12651.9, 53.5, 1908.9) and
  (12810.9, 53.5, 1694.2), and the padlock's health gates them. They are
  rigid doors without `DOORFLAG_CLIP_TO_BBOX`, so neither the prop seam nor
  the dynamic door takes them. In every presentation they are the game's own
  model: two lists, the frame on texture 0x02fa (32x32) and the translucent
  chain quad on 0x0105 (64x32).
  - COMMUNITY HD replaces 0x0105 (256x128). 0x02fa falls back.
  - The XBLA texture set has neither, so under XBLA both are the cartridge's.
  Measured through the provider's own lookups.

The remaster's `prop/damchaindoor` is refused by the preparation, and that
is not a lip. It is a frame (256x256, u 487..588, v -1027..1024) and a
chain quad (128x128) whose coordinates run -28523..-2211 by
-12663..14240: the chain tiles across many repeats of its image.

- The ORIGINAL generation of the same item carries the cartridge's own
  numbers: its frame's u 311..1060 are the ROM model's S values 311..1061.
- Neither a value lattice (at most 14% of values on any power of two) nor an
  edge reach fixes the scale. The scale is the draw's texture matrix, and no
  reader decodes that.
- A rigid-door intake would come next. It would draw the item at the door's
  live matrix while the game keeps state and collision.

Those two remain for full-model parity only. The doors' ARTWORK is upgraded
without either. The game keeps drawing its own model, and the XBLA texture
set carries 0x0105 and 0x02fa, prepared from this item into the native tiles
("Native-domain entries", `docs/texture-packs.md`). Everything the door does
stays the game's, since only two texture ids changed: slide, swing, padlock
and collision.

### Keys, alarms and weapons (#80)

The Surface playtest run (20261005-120919-lvl36, XBLA) marked four things
still drawn from the cartridge: the large key ("silver instead of gold",
mark 6), the weapons lying around and in the guards' hands (mark 7, the
grenade launcher on a crate), and the security alarm (mark 9). None of them
was ever offered to the prop seam: the derivation took standard props,
monitors, vehicles and the rest, and the runtime's `sl_prop_takes` refused
every other object kind. What was reported as "silver" was the game's own
key model under the XBLA texture set; the remaster's `prop/chrkeybolt` is
brass.

Three object kinds are taken now, by both halves:

- **The preparation** (`stageprops.TAKEN`): the KEY (setup type 4), the ALARM
  (5) and the COLLECTABLE (8, a weapon on the floor or carried by a guard).
  On Surface that adds `alarm2`, `chrkeybolt`, `chrkeyyale`, `chrkalash`,
  `chrgrenadelaunch`, `chrskorpion` and `chrsniperrifle`.
- **The object render** (`sl_prop_takes`): the same three kinds as one-piece
  props. A weapon model (`skeleton_prop_weapon`) keeps its muzzle flash -
  Switches[0] and Switches[2], which the game shows while it fires
  (`weaponSetGunfireVisible`) - as the game's own lists, and those lists are
  not part of the model's box (`gunfire_switch`).
- **The guard's walk** (THE HELD WEAPON, `held_begin`): a carried weapon is
  not drawn by the object render. chr.c places its model in the hand
  (`chrRenderHeldWeapon`) and the guard's joint list draws it after the body,
  as it draws a worn hat. The item rides the weapon model's matrix there,
  under exactly the conditions the game draws it, and the walk withholds the
  model's lists but the flash. A dropped weapon is a free object again.

Pickup, the key's use, the alarm's state, firing, and where each object lies
or which hand holds it all stay the game's. A destroyed object draws as the
game's.

**The turned item.** The remaster authors its carried items in the carrying
hand's frame: `prop/chrkalash`'s pose is a whole guard's skeleton with
SKEL_GUN on the right wrist, and its geometry runs along the item's y. Its
box (180 x 3066 x 1320) is the game model's (653 x 39 x 281) 4.69 times over
only under a quarter turn about z, so the two-box relation refused it ("the
item's box is not the model's on axis 0"). `sl_prop_derive` now tries the
quarter turn for the carried kinds (a key or a collectable, `turn_ok`). Two
turns fit any box that one does, half a turn apart. Both the item and the
game's model are laid out from their origin at the hand. The game's AK runs
from its origin 4.9 to -648.1 along x, the Klobb's centre lies at -182 of
its 364. So the turn taken puts the item's box on the same side of the
origin along the model's long axis. A model centred on its origin does not
choose, and the item is refused. The first trial, by the vertex mean, picked
the wrong turn for the AK (both guards' rifles pointed backwards, measured
at mark 5) and was not taken. A box that matches unturned is taken unturned,
so no prop the seam already took moves.

**The model entity.** `prop/chrgrenadelaunch` and `prop/chrskorpion` were
refused, "the command list does not walk to its end". Their ORIGINAL
generations (the reference box) hold the model as entity 1 (8288 bytes, a
.stream and a 54476-byte rendergraph) beside a larger program pool (9088
bytes, no .stream). The reader took the largest entity. `model_entity` now
takes the one with a command list. On every bundle where the largest entity
had the command list nothing changes: Surface's 21 earlier props
regenerate byte-identical. The launcher's original generation also draws
before naming an image, which a geometry-only read (`static_bounds`) no
longer refuses.

Measured (teleport, seed pinned; the pickups from 150 - 250 units behind the
mark so Bond does not collect them):

- **Mark 5.** Both guards' AKs are the remaster's, in the game's hands at the
  game's angle.
- **Mark 6.** The officer carries the bolt key, so the key on the floor in
  the playtest frame is his drop. Shot from 250 units back, he drops it and
  the brass `chrkeybolt` lies where the game put it. His Klobb is the
  remaster's in his hand and on the floor.
- **Mark 7.** The grenade launcher on the crate is the remaster's.
- **Mark 9.** The alarm is `alarm2`.

ORIGINAL frames at the marks are unchanged. Under XBLA, the Dam walkway
witness changes in 9 pixels (a distant guard's rifle) and the Facility witness
is byte-identical. The bundles are shared, so once prepared they stand in on
every stage that draws those models.

Not seen: a guard's muzzle flash over the item. Under the standard cheat set
guards never fire at an invisible Bond. The flash nodes stay the game's by
construction (`gunfire_switch`); the capture is open.

## Diagnostics

- One summary line when a set is selected: the set, the root and how it
  resolved.
- `SL_DL_CENSUS=1` carries the provider's census - set, root, lookups, loaded,
  missing, invalid, resident vertices and triangles - and the draw's own line:
  frames drawn, triangles and vertices per frame, frames skipped.
- the sampler line alongside it: wrap S and T, min and mag filter, and how many
  mip levels were uploaded. The filters are **read back from GL** rather than
  echoed from what was asked for, which is the reading that would have caught
  the first attempt's mip probe asking for generation while the min filter
  never sampled a level above zero.
- `SL_SKY_TRACE=1` prints the anchoring witness: which of the two world
  projections is in force, the offset applied, the frame's render origin, the
  transform's own translation row, and **`residual_w`** - where the dome's
  centre lands along the view axis after the offset, which must be zero
  because that centre is supposed to BE the eye. It is the number that
  separated the three attempts: about 25000 when the folded projection was fed
  absolute coordinates, about 40 and drifting when the dome was pinned to the
  player struct's camera, and 0.001 once the eye is recovered from the matrix.
  A line is printed only when the branch or the render origin changes, so a
  healthy run is a handful of lines; `SL_SKY_TRACE_ALL=1` prints every frame.
  Both are off by default and bounded.

  The same witness carries two more lines, and the aperture defect is the
  reason both exist. **`#68 skyeye`** prints the eye the game has beside the
  eye the pass recovered, their ratio, the camera's forward vector and the
  transform's own axis scales - `residual_w` is self-consistent by
  construction and can say nothing about whether the recovered eye is the
  camera, so this pair is what can. **`#68 skyaperture`** prints the scissor
  box **found** at the seam, the viewport, and the box drawn into. It is the
  line that named the portal window as the cause, and it is where a
  regression would show first: `found` narrower than the viewport is normal
  and expected; `drawn into` anything but the viewport is the bug returning.
  Both obey the same gate as the lines above, so they cost nothing unset.

  The previous revision of this page documented an `SL_SKY_DBG=1` that printed
  "the projection in force when a frame is skipped". **No such variable ever
  existed** - a tree-wide search of `src/`, `tools/` and `docs/` found it only
  in this file. It is replaced by the above rather than left standing.

- `SL_BG_FOGTRACE=1` prints the **fog state readback**, before, during and
  after each external pass: GL's own `GL_FOG` enable read back with
  `glIsEnabled`, the renderer's mirror of it and the colour that mirror holds,
  the RDP fog register and blender mux standing at that instant, the LEVEL's
  latched colour and band with its age in frames, and the mode the pass
  resolved. Reading GL back rather than echoing the request is what named the
  mirror desync; printing the register beside the latch is what named the seam
  as an unstable authority. `SL_BG_FOGTRACE=<n>` additionally strides the
  per-frame `#68 fogdepth` line - projection, mode, the NDC depth range and
  the coordinate range - so a run long enough to reach a pose can be sampled
  at it rather than only at its first frames. Off and free unset.
- `SL_BG_FOG=0` makes both external passes disable the stage outright, which
  is what they did before the fog contract landed. It is the A/B, and it
  reproduces the pre-change build byte-identically.
- `SL_BG_LEVEL_FOG=1` applies the LEVEL's own fog band to a replacement
  background, which is what the seam did before `sl_wa_fog_level_applies` was
  written. Same kill-switch idiom and same reason: it reproduces the
  pre-change build byte-identically, so the destination A/B is one binary. It
  is not `SL_BG_FOG=0` - that one still turns the whole stage off for both
  passes; this one leaves the stage exactly as it is and changes only whether
  the band reaches the replacement's vertices. The census reports which way it
  went in a `#68 background fog destination:` line beside the band itself.
- `SL_SKY_WITH_BG=1` composes the additive skydome alongside a replacement
  background, which is what the seam did before `sl_wa_compose_skydome` was
  written. Same kill-switch idiom and same reason: it reproduces the
  pre-change build byte-identically, so the composition A/B is one build.
- `SL_SKY_ZENITH=0` leaves the generated cloud field (D-023) out and draws
  the source dome exactly - measured byte-identical to the build before it.
  Default on. `#68 skydome: generated zenith clouds (D-023) drawn|NOT drawn`
  is printed once, the first time a bundle carrying them is drawn.
- `SL_XBLA_AUTHENTIC_FOG=1` draws every environment row at its authored range
  instead of the presentation fog range (D-025) - measured byte-identical to
  the build before it. Developer diagnostic, not a setting.
- `SL_SKY_PANORAMA=0` draws the background's panorama shell as authored
  instead of fading its fully fogged part (D-026) - measured byte-identical to
  the build before it. Default on; `#74 worldasset: panorama shell ...` is
  printed once, the first time it fades or is drawn as authored.
- `SL_BG_VBO=0` draws the background pass from client-side arrays, as before
  #88. By default the item's records and indices are held in buffer objects
  (one upload per item, `#88 worldasset: background/<item> in buffer
  objects` once) and the walk's clip positions, pushed positions and fog
  coordinates are uploaded once a walk, and again for a mesh's vertex span
  where the pass rewrites it between draws (the far pass's second range, a
  blended mesh's first-range depth, the decal pull and its restore). From
  client arrays every draw had the driver copy its index list and every
  vertex its indices span - one call over Frigate's largest mesh took
  1.1 ms. Presented frames hash identically either way (B-PERF1); the
  `#88 background arrays:` census line says which way the run went, how many
  items were uploaded and how many mesh re-uploads followed a rewrite.

## Tests

`tools\windows\worldassettest.ps1` runs both suites, on **synthetic fixtures
only** - bytes the tests write themselves. No source, no prepared asset and no
ROM is opened, so CI runs them with nothing installed.

- the SLWA parser against every rejection it has: magic, version, class, item
  path (case, separators, empty, unterminated, junk past the terminator),
  reserved fields, counts that are zero, past their limit or that would
  overflow their own byte length, section offsets that are unaligned, outside
  the file, inside the header or overlapping, a declared size that is not the
  file's length, and bad materials, images and mesh ranges;
- the loader against a truncated file, a missing file, a file naming another
  item, and a malformed one;
- the provider policy: dark with no set selected, one open per (class, path),
  a cached second lookup, an unknown class that never touches the filesystem;
- **the stage character set**, on a miniature decomp tree the test writes:
  every door (a fixed head, a random head, a clone, a body with its own head,
  an AI spawn, the random body), each pool expanded whole and once, the
  `C<item>Z` key rule, a level with no setup, an unknown level and a propdef
  list that does not walk all refused with their reason, and deterministic
  model-index order; the preparation plan on a made-up archive index -
  present, absent (including another generation's item and a neighbour
  sharing the name's prefix), one extraction pass, stamping, a rerun that
  rebuilds and extracts nothing, a refused conversion that names its reason
  while the rest prepare, and the command line's exit codes; and that a
  gated draw marks a body's built-in head but never a head item's;
- the preparation tooling: the writer's limits, that its output parses, that
  it is deterministic and that rerunning replaces rather than appends; the
  mesh splitter on a blob laid out by hand; the draw-record and buffer-table
  parse, including a handle the table does not carry, a record whose length is
  not the draw's index count, and a range outside the model blob; and the
  fixed-point texture coordinate, pinned at the values that separate it from
  the half-float reading it was mistaken for;
- **the anchoring contract**, on a synthetic camera, perspective and render
  origin written by the test itself: that the eye comes back out of a plain
  world-to-clip matrix and out of a view-folded one, each in that matrix's own
  space; that with the dome's centre on the recovered eye **both projections
  place a dome vertex at the same clip coordinate**, so the game alternating
  between them cannot move the sky; that **moving the camera 382 units does not
  move a dome vertex at all**; and that a matrix which is not a view times a
  perspective yields no eye rather than a guess. Both earlier failures are
  checked for as failures: handing the folded projection absolute coordinates
  must still misplace the point badly, or the agreement test proves nothing;
- **the far plane** (#78): recovered from a world-to-clip matrix built from an
  affine view and a perspective, refused for an orthographic one, and the
  first depth range mapping its near and far w to -1 and +1 in order;
- **the trailing stream and the instance matrix** (#80): a run beginning
  where a stride-4 stream ends names the position buffer ending where the
  stream begins, a stream of another vertex count or with no buffer before
  it names nothing, and a pool whose own bytes end at the run is named as
  before; the streamed pool reads as one record with the stream's word before
  the colour word; an op-0x21 record reads as a rotation about y and a
  translation applied as a row vector, a placement scaled along one axis is
  taken, a shear, a zero row, a wrong length or a last column other than
  (0, 0, 0, 1) is not; a draw after the record carries the matrix and the
  draws before it and after op 0x25 carry none; and `instance_vertices`
  carries the positions through the matrix and keeps the rest;
- **the aperture contract**, driven with the three portal windows **measured
  at the playtest's own marks** and the viewport measured with them: all three
  must give the same rectangle, that rectangle must be the viewport, a
  viewport that is not the whole window must be followed too, and a null
  argument must write nothing. The rule is a function precisely so this needs
  no GL context - and the property under test is that the answer does not
  depend on the window it was handed, which is exactly what the defect was;
- **the background class**, on both sides. In the parser: that a
  background-class bundle parses, that it reports its class, that the wider
  mesh, material and image counts are accepted and that one past them is still
  refused, and - the point of it - that a bundle written before the class
  existed **still parses beside one written after**, from the same parser, at
  the same version. In the provider: MISSING answers nothing, INVALID answers
  nothing, VALID answers the bundle and a second lookup answers from the same
  cache slot, a background file does not answer a skydome lookup, and the
  PRESENTATION gates it exactly as it gates the skydome - ORIGINAL and
  COMMUNITY HD offer nothing, XBLA offers it again from the same slot;
- **the level bundle's structural reader**, on streams this file lays out: that
  the walk consumes its record area exactly; that all three primitive types are
  found where a pattern requiring one sees a third of them; that a record
  shorter than its own header, one whose length is not a multiple of four, and
  one that claims to run past the area each STOP the walk rather than read
  past; every primitive type's triangle count and expansion, including the
  fan's hub, which is in every one of its triangles, and the ribbon reading it
  replaces refused by name; the material record in its one-slot, K-block and
  four-slot forms and the two ways it is refused; an index buffer resolved by
  handle and exact length in both 16- and 32-bit kinds, a handle the table does
  not carry, a length that is not the draw's count, and a range outside the
  blob; the SHARED pool found as the buffer whose bytes END where the index run
  begins, and the three ways that is refused; and the vertex layout - that the
  word running with position is chosen over the per-face normal, that its scale
  is read from where its values pile up, that 128 raw units reads as exactly
  1.0, and that a pool leaving the item's own bounds, a stride with no room, or
  a range past the blob are each refused;
- **the conditional draw record**, on a list this file lays out: that op 0x01
  and op 0x30 are both read as draws, in list order and at their own offsets;
  that an op-0x30 draw takes the material and style standing where it is
  recorded; that a draw inside an op-0x17 block is an ordinary draw; that the
  op-0x17 record itself and an op-0x30 record too short to name its index
  buffer are not draws; that the same list without the op-0x30 records reads
  the same op-0x01 draws; and that the one-item reader's view is unchanged.
  With the reader restored to op 0x01 only, three of these fail;
- **the 36-byte vertex and the translucent base image**: that the layout table
  names position +4, coordinate +20 and scale 128 for stride 36 and nothing for
  12, 24 or 32; that a fixture with every field where the vertex program reads
  it decodes position from +4, coordinate from +20 (not from the +0 pair) and
  colour from the last word; that reading it from +0, a truncated pool, a
  coordinate overlapping the position, a position running into the colour,
  positions outside the bounds and a triangle past the pool are each refused;
  and that a translucent multi-slot draw takes the one slot whose image carries
  alpha, while an opaque draw, two alpha images, none, a single slot or no
  style leave the choice to the existing rule;
- **the vertex colour's component order**, one rule that both readers take.
  That the word is byte-reversed - alpha at byte 0, then B, G, R - pinned at
  the two words the game itself adjudicated: the N64-generation model's floor
  decal (`ff 00 e1 ff`) and the remastered road line (`ff 00 6b 6b`), each
  required to read YELLOW rather than cyan; that white is white either way,
  which is why the skydome cannot tell the readings apart; that alpha stays at
  byte 0; and that the one-item reader answers the same as the level reader,
  from the same function;
- **the fog contract**, on the pair measured off the Dam's own display list
  (`fm 25600`, `fo -25344`). That it arms the replacement's fog; that the band
  it reports is required **through the RSP's own formula** rather than against
  a literal - zero at the start, 255 at the end - so the test cannot drift
  with the implementation; that a second, different pair gives a different and
  earlier band, so none of that would hold for a function ignoring its
  arguments; that a level which never issued a fog position, and a band with a
  zero multiplier, each receive no fog; that last frame's position is still
  the contract and an older one is not, so a level that stops fogging stops;
  and that the answer stands with no band wanted. A function rather than a
  condition at the seam for the same reason the aperture is one - no GL
  context, no display list.
- **the scene-assembly rule**: that the additive skydome is composed when
  there is no replacement background and is NOT composed when there is, and
  that the two answers differ so neither direction is vacuous. Two states is
  the whole of the rule, which is the point of writing it as one - it names no
  item, no level and no mesh, so there is nothing else to enumerate, and the
  fallback the replacement contract already demands is the same call with the
  same argument.
- **the generated zenith clouds (D-023)**, on both sides. In the runtime: a
  GENERATED material parses on a skydome and is refused on a background, and
  `sl_wa_material_drawn` draws it only while the switch is on and a source
  material always. In the tooling, on a synthetic polar dome built by the test
  (two-vertex pole, `v` non-linear in the polar angle, a flat blue cap over a
  band made from fixed-seed noise): the pole, axis and `u` rule are read back;
  the cap and the band are found by row statistics; the carrier is opaque at
  its pole and transparent at its rim; the angular detail on small circles
  about the pole is under a tenth of a naive upward extension's (measured
  0.05 against 7.4); the cap is filled with the band's detail and tone; two
  runs are byte-identical; an image with no clear cap and a mesh that is not a
  dome are each refused; and the bundle records already written are untouched,
  the carrier appended after them, flagged, while an image without a cap
  leaves the bundle exactly as it was.
- **the cloud field (D-023, #76)**, on both sides. In the runtime: a
  CLOUD_PLANE material parses only as a generated skydome one; it is drawn only
  while the switch is on; `sl_wa_cloud_coord` samples the tile's origin straight
  up from the tile's origin, moves along t only by phase / (32 x 64 / 2) tiles,
  treats a phase step as the plane moved 10 world units in z (sky.c's
  t = 0.1 z + phase), lands a 45-degree ray one cloud height out, shows no jump
  across the 4096 wrap, gives the same coordinate for the same state, and none
  at or below the horizon, from above the plane, or without an image. In the
  tooling, on the synthetic dome: the carrier spans the profile's lowest
  elevation to the zenith, transparent at its foot, whole from the top of its
  fade, never falling toward the zenith; a broader profile reaches lower; the
  tile has no seam where it repeats and carries the band's detail and tone;
  two runs are byte-identical; the bundle's records are untouched and the field
  is flagged GENERATED and CLOUD_PLANE; the flag is refused off a generated
  skydome draw.
- **the panorama shell (D-026)**, on both sides. In the runtime: a PANORAMA
  material parses on a background and is refused on a skydome; only a
  PANORAMA material that also consumes the stage fog takes the rule, and only
  while the switch is on - every other material, and a bundle written before
  the bit, draws as it did; only a fully fogged shell vertex fades; the reach
  weight, the azimuth window, the fade coordinate and the coverage profile are
  pinned at known points (whole below the haze top, none a fade width above,
  a smoothstep between, flat at both ends). In the tooling, on synthetic ring
  items: an outer ring and its band are the shell and nothing inside them is;
  a lone far object that does not surround the item is not one; an item
  with no separate outer ring marks nothing; a ring sharing a material with
  scenery inside it is refused; a ring with scenery beyond it is not the
  outermost; the bit round-trips beside FOG at the same version and is refused
  on a skydome.

## Asset safety

No asset, converted bundle, manifest with payload, source, or path to one is
committed, packaged or referenced by this repository (project rule 2). Tests
use synthetic fixtures only. `texsources\` and `worldassets\` are gitignored,
outside everything release packaging stages, and hold no tracked file - the
public export publishes tracked files only.
