# Texture sets (#47)

A player-facing TEXTURES setting - **ORIGINAL / COMMUNITY HD / XBLA** - in
the front end's DISPLAY tab and the watch's GRAPHICS child, and the provider
behind it. Render-only, native-only, default ORIGINAL. Nothing in the
simulation reads it; a trace replay never sees it.

This page is the runtime architecture. It carries no asset, no pack and no
pointer to one: Sightline neither ships nor locates a texture set. The
player brings a set; the runtime reads what is there and falls back per
texture to the ROM's own artwork.

---

## The three sets

| Set | What is drawn | Where it comes from |
|---|---|---|
| ORIGINAL | the game's own N64 artwork, decoded from the player's ROM exactly as the accepted build renders it (the #15 enhancement layer included) | the ROM, through `texLoad`; **no pack is read** |
| COMMUNITY HD | a Community HD replacement where one exists, else ORIGINAL for that texture | `<root>\community\` |
| XBLA | a user-supplied XBLA replacement where one exists, else ORIGINAL for that texture | `<root>\xbla\` |

Fallback is **per texture and never across sets**: a COMMUNITY HD miss draws
the decode, never the XBLA file, and the reverse. No set is assumed
complete; a set that is not installed is every id missing, the game is
playable, and one line says so.

The setting is `textures=0/1/2` in the native settings store
(`%LOCALAPPDATA%\sightline\config.ini`, beside `world_detail`). Missing,
malformed or out of range reads ORIGINAL; an inactive store (replay,
headless) answers ORIGINAL. A change applies at each texture's next resolve
- the next frame - with no restart and no level reload.

## Switching sets while you play

Three ways in, one store:

| Where | How |
|---|---|
| **In the level** | **F5**, or the pad's **View / Create** button ("Select") - one press steps ORIGINAL -> COMMUNITY HD -> XBLA -> ORIGINAL |
| Front end | OPTIONS -> SETTINGS -> DISPLAY -> TEXTURES |
| Watch | SIGHTLINE -> GRAPHICS -> textures |

The in-level cycle is a binding like any other - action **TEXTURE SET** in
the registry (#46) - so it appears in the bindings editor in both UIs, both
slots, and is re-bindable. It is bound in every controller preset, and the
button it is bound to is the one that **used to mark**: the pad no longer
marks at all (owner decision, 2026-09-22), while the keyboard's F9 / F8 marks
are unchanged and still deliberately unbindable so nothing can steal them.

A press only lands with live input and no menu up, which is the same rule the
gameplay actions take, so the cycle cannot fire from inside the watch, the
front end or a binding capture - nor on the frame one of those closes with
the button still held.

**What you see.** The set you landed on is named on the HUD's own bottom
message line ("TEXTURES: COMMUNITY HD"), the same line the game's cheats use
- no new text system was added - and one line goes to the log. If no pack is
prepared the setting still moves and the art stays ORIGINAL; the launcher
already says so once per launch.

## Where the files live

```
<root>\community\<hex4>.sltx
<root>\xbla\<hex4>.sltx
```

`<hex4>` is the N64 texture number, four lower-case hex digits (`0000`..
`0a89` - the id in the image table, the argument to `texLoadFromTextureNum`).
The root is, in order:

1. `SL_TEXPACK_ROOT`, if set and non-empty (tests, local runs);
2. the player-data ladder every other native asset uses:
   `%LOCALAPPDATA%\sightline\assets\texpacks`.

ORIGINAL never touches the root.

## Getting the Community HD set (what a PLAYER does)

Nothing. One double-click, and no file to find:

```
Get-Textures.cmd          (beside Sightline.cmd in a release package)
```

It downloads the **pinned official release** of the Community HD project from
its maintainers - `https://github.com/GhostlyDark/GoldenEye-007-HD` - to that
player's own machine, verifies size and SHA-256 against the record in
`tools\community-source.json`, converts the textures the mapping names, and
writes them to `%LOCALAPPDATA%\sightline\assets\texpacks\community`, which is
exactly where the runtime's second root looks. Then TEXTURES = COMMUNITY HD
draws them. The archive is kept under `%LOCALAPPDATA%\sightline\cache` so a
second run needs no network; an archive already there and already matching the
pin is used as it stands.

It needs **no Python, no repository and no build toolchain** - only the Windows
PowerShell every Windows 10/11 has. The conversion in the package is
`tools\get-textures.ps1`, which decodes PNG through `System.Drawing` and writes
SLTX itself; it is **byte-for-byte the same output** as the developer
conversion below, which is asserted on synthetic fixtures by the selftest and
was measured across the whole set (468 of 468 identical).

**Sightline distributes no texture and mirrors nothing.** The package carries
four files for this - the entry point, the converter, the record of which
upstream release is wanted, and the id-to-checksum mapping - and not one pack
byte. The transfer is between the player and the pack's own maintainers, which
is the arrangement they asked for on 2026-09-18 when they declined
redistribution in any form and suggested a script adapting the official pack.

**The XBLA set is not obtained by this or any other tool**, in the package or
in the tree: it is user-supplied, Sightline neither ships, locates nor
downloads a source, and nothing here points at one.

If the pinned release is ever withdrawn, the tool stops with one actionable
line naming the upstream project and what it currently publishes (and
`-AllowUnpinned` takes the newest matching asset instead, saying that the
recorded SHA-256 cannot vouch for it).

## Preparing a pack (development)

The binary's ladder above is the whole of its contract: it holds no source
path and looks in no checkout. That leaves a gap a **source checkout** has to
close on its own, and not closing it is what failed this feature's first
owner replay - the packs sat outside the tree, only an exported
`SL_TEXPACK_ROOT` reached them, and an ordinary launch therefore read a root
that did not exist and drew ORIGINAL under every setting while automated runs
that did export the variable reported replacements. Two launches, described
as one.

So a checkout carries two private directories, behind the boundary
`baserom.u.z64` already lives behind - both gitignored, neither ever
committed, packaged or exported:

```
texsources\      the sources: Community HD is FETCHED here for you; the XBLA
                 source is the one you supply
texpacks\        what the tool WRITES: <set>\<hex4>.sltx - the pack root
```

One command fills the first and converts it into the second:

```
.\tools\windows\prepare-textures.ps1            # fetch what is missing, prepare both
.\tools\windows\prepare-textures.ps1 -Community
.\tools\windows\prepare-textures.ps1 -Xbla
.\tools\windows\prepare-textures.ps1 -NoDownload  # never touch the network
.\tools\windows\prepare-textures.ps1 -Force       # redo work whose inputs are unchanged
.\tools\windows\prepare-textures.ps1 -SelfTest    # synthetic fixtures only
```

**What it fetches, and what it never fetches.** With no copy of the Community
HD release in `texsources\`, it calls the same `tools\texpack\get-textures.ps1`
the release package ships, which asks github.com for the release pinned in
`tools\texpack\community-source.json` (repository, tag, asset, size, SHA-256),
downloads it with progress, resumes an interrupted download, verifies it
against that record and keeps it as `texsources\community-hd.zip`. The XBLA
source is never fetched and never looked for anywhere but the path you give.

**Re-running costs a hash.** Each set records, in gitignored build output
(`build\texpack\*.stamp`), the SHA-256 of the source it was built from, the
mapping's hash and the file count; a second run verifies the source, finds the
pack current and does nothing. `-Force` redoes it anyway. No fingerprint of a
user-supplied archive is ever written into a tracked file.

and `tools\windows\play.ps1` then hands `texpacks\` to the child as
`SL_TEXPACK_ROOT` when it exists, so

```
cd <your checkout>
.\tools\windows\play.ps1
```

shows the selected set with no variable set anywhere. An explicit
`SL_TEXPACK_ROOT` still wins, exactly as `SL_ROM` does; with a non-ORIGINAL
set selected and no pack prepared, the launcher says so in ONE line naming
the command above, and the game runs on the ROM's own artwork.

**What the repository holds, and what it does not.** `tools\texpack\` holds
the converter - the container and block-format readers, the SLTX writer - and
two mapping tables (`mapping\community.json`, `mapping\xbla-accepted.json`)
carrying, per texture, the N64 image-table id, that image's width and height,
and the identity of the file in the source that replaces it: a checksum for
Community HD, a resource name for XBLA. Identities and dimensions; no pixels,
no payload, no path, no acquisition pointer, and no way to obtain a source.
`tools\texpack\selftest.py` exercises all of it on generated fixtures.

**The boundary differs by set, and it is about REDISTRIBUTION, not about
fetching.** Community HD is an externally maintained optional pack whose
maintainers declined redistribution in any form on 2026-09-18 and suggested a
local adapter for their official release - so Sightline never mirrors, bundles
or pre-converts a byte of it, and what the tools do instead is fetch the
maintainers' own release, on the user's own machine, straight from the
maintainers. The XBLA set is user-supplied under a harder rule still:
Sightline neither ships, locates nor downloads a source, and nothing in this
repository points at one.

**Developer prerequisites of the conversion.** The Python converter
(`tools\texpack\prepare.py`, the path `prepare-textures.ps1` prefers) needs
**Pillow** (PNG decoding, Community HD) and **numpy** (block decoding, XBLA)
in the project venv, and 7-Zip for the XBLA container. They are DEVELOPER
prerequisites of this one tool, like the compiler: nothing in the engine, the
build, CI or a release package links them, `requirements.txt` marks them as
such, and the tool says so in one line if they are missing. If Pillow is
absent, `prepare-textures.ps1` falls back to the package's own PowerShell
converter, which needs nothing - so a checkout can always build the Community
set. The XBLA conversion has no such fallback and needs numpy.

## The runtime image format (SLTX)

The tree links no PNG decoder and none was added (project rules: no
dependency without asking). A set is converted once, outside the engine,
into a trivially parsed form the runtime reads directly. Little-endian,
28-byte header, tightly packed RGBA8, rows top first (the orientation the
decoder produces and `glTexImage2D` takes - no flip anywhere):

```
+0   'S' 'L' 'T' 'X'
+4   u32 version            1
+8   u32 id                 the N64 texture number (== the filename's)
+12  u16 n64_w, u16 n64_h   the N64 image's size (== the pool entry's)
+16  u16 phys_w, u16 phys_h the replacement's size, 1..1024 a side
+20  u32 flags              0 (non-zero is rejected)
+24  u32 payload            phys_w * phys_h * 4, exactly the bytes that
                            follow, exactly to the end of the file
```

The parser (`src/gfx/sl_gfx_texprov.c`) checks every field before a byte of
payload is read or an allocation is sized; a malformed file is INVALID,
reported once, and that texture stays ORIGINAL's. `tools\windows\
texprovtest.ps1` drives it with synthetic fixtures (56 checks).

The replacement may be any size within the bound; a non-integer ratio to
the N64 image is fine (see below). Mip levels are not stored; the far-image
pyramid the renderer already builds (B-119) is halved from the replacement.

## How a replacement reaches the screen

```
texLoad (image.c)  -> decoded bytes in a texture pool
        |             + a native side table: decoded pointer -> (id, w, h)
        v
display list ------> tile -> decoded pointer (the FD word is that address)
        v
tex_acquire (sl_gfx_dl.c)
        |  the provider: pointer -> id -> (set, id) -> resident image | NULL
        v
glTexImage2D   the replacement at ITS physical size, or the decode
```

**Identity** is the game's own: `texLoad` is the one place a texture number
becomes bytes, and every display list names those bytes by address, so the
bridge is a side table written there and emptied per pool at `texInitPool`
(two `#ifndef __sgi` islands; the cartridge arm is untouched). No runtime
pixel hashing, no filename matching.

**Logical vs physical.** The renderer normalises texture coordinates by the
tile's LOGICAL size - `s / 32 / tile_w` for geometry, the same for 2D
rectangles - never by the uploaded image's size. A replacement is uploaded
larger and sampled over exactly the region and repeat count the decode was:
the wrap / clamp / mirror modes, the tile size, the combiner and the alpha
path come from the same state as before. (B-116 has relied on this since it
started uploading 4x images under unchanged S/T.)

A replacement is used only when the tile IS the whole image: the same
decoded pointer (a mip level inside the image is a different address) and
the same logical size as the pool entry. A tile that reads a sub-window or a
wrap period larger than the image stays ORIGINAL's ("shape miss" in the
census), because no replacement can reproduce what the RDP reads past the
image there.

**Cache identity.** The renderer's texture cache keys each entry on the
provider generation it was uploaded under (0 = the decode). A switch bumps
the generation: an entry holding one set's pixels never answers another
set's resolve, an entry holding the decode keeps answering for an id no set
replaces, and GL names are reused by the cache's ring as before - no new
object per switch, no leak.

**Laziness.** A (set, id) is resolved at first use; missing and invalid
outcomes are remembered, so an absent file costs one open per run. Resident
replacements are LRU-evicted under a byte budget (`SL_TEXPACK_BUDGET_MB`,
default 256) and re-read on demand. No per-frame disk I/O; no preload.

## Diagnostics

- One summary line when a non-ORIGINAL set is first asked for: the set, the
  root and how it was resolved, the set folder and whether it exists.
- The renderer's census (`SL_DL_CENSUS=1`) carries `#47 provider-replaced
  uploads=` and the provider's own line: generation, switches, registry
  size, lookups / unregistered / shape misses / hits, files loaded /
  missing / invalid, resident images and KB, evictions.
- `SL_TEX_PROVIDER_DBG=1`: each (set, id) outcome once (LOADED with both
  sizes, MISSING), each unregistered source once, each shape miss once.
- `SL_TEX_PROVIDER_WATCH=<hex id>`: one line per change of that id's answer
  - a live switch reads as a transcript.
- `SL_TEX_COVERAGE=<file>` (+ `SL_TEX_COVERAGE_FRAME=1`): WHAT IS ON
  SCREEN, per texture number - one CSV row per id drawn, with the screen
  area its primitives cover, the logical tile size, how many primitives,
  whether it reached the screen as geometry or as a 2D rectangle, and
  whether the active set replaced it. Per-frame mode clears the table as
  each frame starts and publishes the one that just ended, so the table
  describes the captured picture rather than an average of a moving camera.
  Projected, viewport-clipped primitive area: it ranks and bounds, it does
  not depth-test. `SL_TEX_COVERAGE_FIRST` / `_LAST` bound the whole-run mode
  to a frame range.
- `SL_TEX_HILITE=mapped` / `=<hex id>` (+ `SL_TEX_HILITE_RGB=rrggbb`):
  WHICH PIXELS a texture actually paints. Every texture the active set
  replaces - or one named id, under any set - is uploaded as a flat colour
  over the decode's own alpha, so a cutout stays a cutout. Capture the same
  pose under two colours and the pixels that differ are the footprint,
  occlusion included. A pair, never a single run: differencing one hilite
  against an ordinary capture would miss every pixel the texture paints
  black.
- `SL_TEXPROBE=1` (existing) reports which artwork a probed draw uploaded.
- `SL_TEX_DUMP_IDS=<dir>`: the game's own decode, keyed by TEXTURE NUMBER -
  one `<hex4>.sltx` per id, written once per run, in the same format a pack
  uses. The renderer's older `SL_TEX_DUMP` is keyed by decode order and
  answers "what did this draw look like"; this one answers "what is the
  ROM's artwork for id 0112", which is what comparing a whole mapping
  offline needs (a decode and a replacement become two files one reader
  opens). Diagnostic, native-only, off unless the variable is set, and the
  id comes from the provider's own side table - an image that never passed
  through `texLoad` has none and is skipped rather than guessed at.

## What a set actually covers, per scene (measured)

**A set is not a skin.** It replaces the textures it has art for, and the
ROM's own artwork everywhere else - so how different a scene looks is
decided by two numbers, and only one of them is about Sightline:

1. **how much of that frame is painted by textures the set carries**, and
2. **how far the set's art differs from the ROM's, once the level's own
   lighting has multiplied it.**

Both are measurable from inside the engine, and the three seams below exist
so that neither has to be guessed at. Nine deterministic poses, one frame
each, ORIGINAL / COMMUNITY HD / XBLA at the identical camera (the control -
ORIGINAL captured twice - differs in **0.00%** of pixels at every pose, so
every number here is a texture effect and not the harness):

| scene | COULD change (COMMUNITY) | DID change | COULD (XBLA) | DID |
|---|---|---|---|---|
| Archives, the posters | 91.1% | 85.0% | 0.0% | 0.0% |
| Archives, facing away | 91.1% | 74.8% | 0.0% | 0.0% |
| Jungle, the start | 85.5% | 37.9% | 2.4% | 1.8% |
| Facility, the door sign | 83.6% | 29.3% | 19.1% | 14.0% |
| Dam, the gate yard | 81.7% | 21.0% | **41.9%** | **11.9%** |
| Facility, the corridor | 71.9% | 37.8% | 1.6% | 0.2% |
| **Dam, the reservoir** | 17.5% | 8.4% | **47.2%** | **40.5%** |
| Dam, the face | 44.6% | 10.5% | 0.2% | 0.0% |
| Depot, the yard | 34.1% | 12.7% | 36.7% | 24.1% |
| Control, the lift cutscene | 4.0% | 1.7% | 0.0% | 0.0% |

The Dam rows are the ones that moved, and they moved because two mappings
were added this round: `011e`, the concrete band that is 38.6% of the gate
yard, and `05e7`, the reservoir water. Both Dam vantages are listed because
the reservoir and the downstream face are different surfaces with different
answers - the row that used to be labelled "the reservoir" was in fact the
face, and is now named as such.

"COULD change" is the visible footprint of the textures the active set
replaces - measured, not estimated, by painting exactly those textures a flat
colour and capturing the pose twice in two different colours. "DID change" is
the plain pixel difference against ORIGINAL at the same pose.

**Two different reasons a scene can look unchanged**, and telling them apart
is the point of the table:

- **Nothing on screen is mapped.** Control's lift is 4.3% mappable - one
  64x32 wall texture covers 60% of that frame and no set has art for it. No
  amount of engine work changes that picture; only a mapping would.
- **It is mapped, it IS replaced, and the replacement looks like the
  original.** The Dam reservoir is the clean case, and it is worked through
  below.

### The Dam water, end to end - and the witness that was pointed the wrong way

An earlier round answered "the water on the Dam looks the same in every
setting" by analysing **id 0123** at a pose it called the reservoir. That
answer was about **the wrong surface**, and it is corrected here.

**What 0123 actually is.** Sixteen poses were swept around the dam - a full
turn from the level's own elevated intro camera and another from the crest -
and the on-screen census taken at each. 0123 paints the **dam's downstream
concrete face**: a grey wall with vertical expansion joints that fills the
lower half of the frame from the elevated camera. It is 64x64, I4, opaque.
The Community pack's CRC-exact texture for it is dark rough concrete, which
agrees. Hiliting 0123 at the reservoir pose paints **0.00%** of the frame -
it is not drawn on that side at all.

**What the water actually is.** From the level's intro camera 4, looking
across the impounded water, the surface that fills the frame is **id 05e7** -
32x32, CI8_RGBA16, opaque, geometry path. Hiliting that id alone paints
**47.2% of the frame**, and the ROM's own decode for it is a pale blue-grey
ripple. That is the reservoir.

**Why it looked the same in every setting, simply.** 05e7 was mapped by
**neither** set. Measured at the reservoir pose before this round's change,
against an ORIGINAL-vs-ORIGINAL control of 0.000%:

| | whole frame | the water's own pixels |
|---|---|---|
| COMMUNITY HD | 8.44% changed | **0.01%** |
| XBLA | **0.00%** changed | **0.00%** |

Community's 8.44% is the mountains and the dam, not the water. So the water
was byte-identical under all three settings, and the honest reason is the
plainest one: **no set had art for it.** The earlier "the level's own night
lighting eats the difference" explanation was measured on 0123, the concrete
face, and does not describe the water at all.

**What was done about it.** The XBLA source *does* remaster this surface.
Stage A pairs 05e7 to the legacy tile `_0x0864E125.rgb` in the Dam bundle at
NCC **0.945**, and the HD Dam bundle carries a 512x512 water albedo beside
its own matching normal map - the same ripple pattern in both, a material
pair. Pixels cannot join the two halves (the remaster is re-authored: best
NCC against every HD texture in the bundle is +0.14), but every other line of
evidence agrees: same bundle, a clean **16x** ratio, 1:1 aspect on both
sides, and the subject confirmed in the engine rather than by eye - 05e7 is
the water, and the candidate is water. With that mapping in place, at the
same pose and the same control:

| | whole frame | the water's own pixels |
|---|---|---|
| XBLA, before | 0.00% | 0.00% |
| XBLA, after | **40.46%** | **85.76%** (mean delta 10.2/255) |

The reservoir now reads as rippled water under XBLA, with the horizon,
the mountains and the dam unchanged around it.

**The dam face keeps its old answer.** 0123 is still Community-mapped and
still barely changes at the face pose, and the lighting arithmetic recorded
for it stands - box-filtered to 64x64 the replacement differs by a mean of
14.3/255 while the Dam at night draws that surface at 47.9/255, about 19% of
full, so 14.3 x 0.19 = 2.7 against 2.4 measured. That was always a correct
statement about the concrete, and only ever mislabelled as being about water.

### Is the XBLA set replacing anything, or replacing art with itself?

A fair question was put to the accepted XBLA set: the source ships **both**
the legacy N64-era art and the remaster, so a mapping that terminated on the
legacy copy would replace a texture with a near-identical one and the setting
would look like it did nothing. Every one of the 126 the set then held was
measured rather than argued about. (It holds 127 now: one was dropped and two
were added by the work below.)

**How the two are told apart, derived from the source itself.** A level's HD
bundle and its legacy bundle both describe the same rooms. Where the HD
bundle carries a texture with the *same dimensions* as one in its own legacy
bundle and near-identical pixels (NCC >= 0.97, mean abs diff <= 0.035), Rare
carried the old art across unchanged. Measured over the whole source: 1464 HD
textures have a same-size legacy counterpart and **1325 of those are
carry-overs** - about 41% of the HD tree is not remastered at all.

**The audit.** Each accepted mapping's replacement was decoded, scaled to the
N64 logical size and compared with the ROM's own decode:

| verdict | count | what it means |
|---|---|---|
| REMASTERED | 124 | 2x-32x the N64 size and materially different art |
| LEGACY-NO-OP | 1 | near-identical to the ROM art after downscaling |
| UNCLEAR | 1 | neither a clean upscale nor clearly the same subject |

**Not one accepted mapping sources from a carry-over texture** (0 of 126),
and the replacement is 2x or larger in every case but one. The size-ratio
histogram is 8x (63), 4x (36), 16x (11), and a tail of 2x/3x/7x/32x. So the
concern is real about the source and **false about this set**: the pipeline's
requirement that a replacement be higher-resolution than the legacy texture
already excluded every carry-over.

The one LEGACY-NO-OP (`088b`, a monitor logo) is a faithful 3.75x upscale of
the same artwork - sharper, not different - and is kept. The one UNCLEAR
(`0828`, an attract-mode button) was **dropped**: at 1.14x it is no
resolution gain at all, and its structural similarity to the N64 texture is
*negative*, which is the signature of a wrong pairing rather than a subtle
one. A mapping that cannot change anything is worse than no mapping.

The same test over the 468 Community mappings found **zero** no-ops: the
smallest replacement there is 2x, the median 8x, and no replacement is within
the near-identity band.

### Why XBLA changes so little on the Dam, and what was recovered

The reservoir pose is not short of XBLA art because the chain broke - it is
short because the **pairing** step cannot see through re-authoring. The HD
Dam bundle holds roughly fifty genuinely remastered textures; ten of them
reach a mapping. The rest were redrawn far enough from the N64 tile that
pixel similarity cannot pair them with the legacy texture whose id is known,
and three independent attempts to pair them by other means were measured and
**failed**:

- **Bundle texture-table order** - if the HD and legacy bundles listed their
  textures in the same order, every re-authored texture would pair for free.
  Tested against the 231 pairs already established by pixels: **31 agree**,
  which is about what chance gives. The order does not correspond.
- **Cross-source agreement** - where the Community pack has a CRC-exact
  texture for the same id, two independent remasters of one surface might be
  expected to agree. Calibrated on known-good mappings the measure does not
  separate: 40 of 53 true pairs score below the 99th percentile of the null.
  Two artists remastering one tile diverge as much as strangers.
- **Cross-bundle co-occurrence** - a texture shared by several levels should
  have a remaster shared by the same levels. True for **112 of 232** pairs,
  and where true it narrows to a single candidate only 9 times. It narrows;
  it does not decide.

Those are recorded so nobody re-walks them. What the search *did* recover is
`011e`, the Dam's concrete wall band: rank 1 of 1895 remastered candidates
from two independent queries, a clean 8x ratio, matching 1:1 aspect, opaque
on both sides, and the same distinctive horizontal seam visible in the ROM
decode, the Community texture and the XBLA one. It was sitting in the review
tier because the pairing step scored 0.46 against a 0.60 bar. It is **38.6%
of the Dam gate-yard frame** - the single largest texture at that pose - and
4.2% at the reservoir.

The reservoir water (`05e7`) was recovered the same way and is now mapped -
see the Dam water section below, which also corrects which id the water
actually is.

The other high-coverage unmapped ids were searched the same way, against
every texture in the source rather than through the two-step chain: `08c0`
(60% of the Control frame), `08d9`, `09b7`, `08d8`, `01c3`, `0055`, `08df`,
`0880`, `03b5`, `03b4` and `001a`. **None has a credible XBLA counterpart** -
every candidate that ranked was either the wrong aspect or sat inside the
null distribution. For the two biggest Dam backdrop surfaces (`03b5`, `03b4`,
greyscale rock tiles) the remaster exists but is a full-colour panorama
containing sky, on different UVs: different art for different geometry, which
a per-id substitution cannot use.

### Does the source carry its own original-to-remastered table?

It would settle the pairing outright, so it was looked for rather than
assumed. The answer is **no, not at texture granularity**, and the two places
it could have lived were both checked:

- **In the bundles.** Each level appears twice, as a legacy bundle and an HD
  one, and every bundle does contain a resource named "texture pairs" - but
  that resource is the bundle's shared **texture pool** (its bytes are pixel
  data), not a correspondence table. The two generations are separate files
  with no cross-reference between them.
- **In the executable.** It is not encrypted and its image is stored raw, so
  it can be read statically. Every one of the Dam bundle's 144 texture names
  was searched for in it, as text and as a 32-bit value: **0 hits** for both,
  against a control of 0 hits for the same count of random values. The bundle
  paths are absent too. The asset namespace is resolved through the file
  system, not through a table in the image.

That is consistent with how the build works: the display-mode switch swaps
**whole models**, so the runtime pairs a legacy bundle with an HD bundle by
**item path** - which this pipeline already uses - and never needs a
per-texture correspondence. Texture-level pairing therefore has to be earned
from evidence, which is what the stages above do.

### Coverage limits

- **The mapping is the ceiling, not the engine.** 468 of the image table's
  2698 ids have a Community HD mapping and 127 an accepted XBLA one, 541
  distinct. The other 2157 draw the ROM's artwork under every set because no
  art for them exists in the sets, not because anything refuses it.
- Fonts and HUD glyphs are not image-table textures (they never pass
  through `texLoad`) and stay ORIGINAL under every set.
- Sub-window and wrap-period tiles (above) stay ORIGINAL. Across the nine
  poses above this cost **zero** on-screen area: not one shape miss occurred.
- Whole-model UV atlases (the XBLA remaster's characters, heads, guns) have
  no per-texture mapping and are out of scope; a set that needs different
  UVs is a different feature.
- Effects frames (explosions, smoke) are drawn through a separate path and
  are not covered by the available mappings.

### Measuring it yourself

```
SL_TEX_COVERAGE=<file>        one CSV row per texture number drawn, with the
SL_TEX_COVERAGE_FRAME=1       screen area it covers, for ONE frame
SL_TEX_HILITE=mapped          paint every texture the active set replaces a
SL_TEX_HILITE=<hex id>        flat colour - or just one id
SL_TEX_HILITE_RGB=rrggbb      the colour (use two runs and diff them)
```

All three are diagnostic, native-only and dark unless set. The coverage
number is projected, viewport-clipped primitive area - it ranks what is on
screen and counts overdraw, and it does NOT depth-test, which is why the
hilite pair exists beside it as the visible-pixel measurement.

## Asset safety

No texture, converted file, pack manifest with payload, source archive, or
path to one is committed, packaged or referenced by this repository (project
rule 2). Tests use synthetic fixtures only.

The converter now lives in the tree (above) because it must, if an ordinary
launch is to show a pack at all; what it carries is identities and
dimensions, and what it reads and writes are the two gitignored directories
a checkout keeps for the purpose. Release packaging and the public export
take neither: `texsources\` and `texpacks\` are outside everything staged -
`package-release.ps1` copies named files, never a directory tree of the
checkout - and the public export publishes tracked files only, of which
neither directory holds any.
