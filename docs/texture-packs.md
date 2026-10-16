# Texture sets (#47)

The TEXTURE PROVIDER - **ORIGINAL / COMMUNITY HD / XBLA** - and the seam
behind it. Render-only, native-only, default ORIGINAL. Nothing in the
simulation reads it; a trace replay never sees it.

> **THERE IS NO LONGER A PLAYER-FACING "TEXTURES" SETTING (#68,
> 2026-09-23).** The player has ONE visual selection, **PRESENTATION**, and a
> policy resolves it into a set for each visual provider - this one and the
> world-asset provider - so one choice moves the whole picture together.
> `docs/world-assets.md` and the policy header
> `src/platform/sl_presentation.h` hold that layer. What this page describes
> is still exactly true of the MECHANISM: the sets below, their roots, their
> format, their cache and their per-texture fallback are this provider's own,
> and nothing else reads them. What changed is only WHO CHOOSES which set is
> active. Where this page says "the TEXTURES setting", read "the texture set
> the active presentation selects".

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

## Which set is active

The provider does not read a setting of its own. It asks the presentation
policy - `sl_presentation_textures(sl_presentation())`,
`src/platform/sl_presentation.h` - which of the three sets above the player's
one visual selection resolves to:

| PRESENTATION | this provider draws | (and the world-asset provider) |
|---|---|---|
| ORIGINAL | ORIGINAL - no pack is read | nothing |
| COMMUNITY HD | the COMMUNITY HD set | nothing |
| XBLA | the XBLA set | the `xbla` world-asset set |

The stored key is `presentation=0/1/2` in the native settings store
(`%LOCALAPPDATA%\sightline\config.ini`, beside `world_detail`). It replaced
#47's `textures` key, which held the same three ids: a config written before
2026-09-23 is migrated at load - the value is copied across, the canonical
key is written once, and the legacy key is never written again, so exactly
one stored value is authoritative. If a hand-edited file carries both, the
new key wins whatever the line order. Missing, malformed or out of range
reads ORIGINAL; an inactive store (replay, headless) answers ORIGINAL.

A change applies at each texture's next resolve - the next frame - with no
restart and no level reload, because the provider asks the policy at every
resolve and bumps its cache generation when the answer moves.

## Switching while you play

Three ways in, one stored value:

| Where | How |
|---|---|
| **In the level** | **F5**, or the pad's **View / Create** button ("Select") - one press steps ORIGINAL -> COMMUNITY HD -> XBLA -> ORIGINAL |
| Front end | OPTIONS -> SETTINGS -> DISPLAY -> PRESENTATION |
| Watch | SIGHTLINE -> GRAPHICS -> presentation |

One press moves **every** visual provider, which is the point of the
coordination: at the Dam, stepping into XBLA brings the XBLA artwork and the
XBLA skydome together, and stepping out of it takes both away.

The in-level cycle is a binding like any other - action **PRESENTATION** in
the registry (#46) - so it appears in the bindings editor in both UIs, both
slots, and is re-bindable. It is bound in every controller preset, and the
button it is bound to is the one that **used to mark**: the pad no longer
marks at all (design decision, 2026-09-22), while the keyboard's F9 / F8 marks
are unchanged and still deliberately unbindable so nothing can steal them.
Its persisted binding key is still `bind.texture_cycle.*`, deliberately: a
stored key is an identity, and renaming it would silently discard a player's
own rebinding. The label they read is what changed.

A press only lands with live input and no menu up, which is the same rule the
gameplay actions take, so the cycle cannot fire from inside the watch, the
front end or a binding capture - nor on the frame one of those closes with
the button still held.

**What you see.** The presentation you landed on is named on the HUD's own
bottom message line ("PRESENTATION: COMMUNITY HD"), the same line the game's
cheats use - no new text system was added - and one line goes to the log. If
no pack is prepared the selection still moves and the art stays ORIGINAL; the
launcher already says so once per launch.

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
acceptance playtest - the packs sat outside the tree, only an exported
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

**Where level 0 lives (#68).** `tools\texpack\xdecode.py` owns the console's
texture memory layout, not just its block formats, because reading the right
bytes is a prerequisite to decoding them. A texture's base level is stored at
the row pitch its fetch constant carries, padded to 32 blocks each way; its
mip levels have a layout of their own; and when packed mips are on, every
level whose width **or height** has fallen to 16 texels or fewer shares a
single packed tail rather than getting storage of its own. Because that rule
is written on the SHORTER side, a texture already 16 or less on one side has
its tail begin at level 0 - **the base level itself is inside the tail and is
not at the start of its allocation**. The decoder models the packed level, the
level's offset within the tail and the base pitch, refuses a level placed
outside its surface, and takes the container's declared mip count as the gate:
with no chain there is no tail, so the base is at the origin. `selftest.py`
drives all of it on synthetic surfaces whose every block carries its own
coordinates. This changes no accepted mapping's output - none of the 127 names
a texture the rule moves - and `docs\world-assets.md` records the measurement
that recovered it.

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

### Native-domain entries (re-UV'd remaster items)

Most XBLA entries are a source image that IS the native image at a higher
resolution: the two flips are the whole conversion. Remaster images already
reach cartridge geometry this way; the mapping has taken images from the
remaster's `new/prop/*` items since it was built. Some remaster items were
re-UV'd, though. The native model samples its texture with the cartridge's
coordinates, while the remaster draws different artwork with different
coordinates. The mapping names such an id with up to three more fields,
applied in order after decoding (`tools\texpack\nativeuv.py`):

- **`sample`** - resample a source window into the native texture's domain.
  `u` maps a native texel band `n64: [a, b]` onto source pixels `src: [c, d]`.
  With `wrap`, texels below the band continue past its far end. With
  `"outside": "mirror"`, a model that samples past the band sees the artwork
  reflected, not one edge column smeared across. `v` repeats a source window
  every `n64` span, and `seam` cross-fades the last fraction of each repeat
  into the rows before the window, so the repeat tiles without a seam row.
- **`level`** - scale colour so the covered texels' mean luminance equals the
  native image's. The native model shades its texture with vertex colours
  authored for the native image's brightness, and a remaster image lit for
  another shading model can land several times darker.
- **`bleed`** - give every transparent texel the colour of its nearest covered
  texel, so a cut-out's filtered edge never fades towards the colour under
  its transparent part.

Every number is measured from the two models. On the Dam's chain-link doors
(`Pdamchaindoor`, `prop/damchaindoor`):

- **Chain field (0x0105).** The chain quad's corners carry
  (u, v) = (s / 2 - 12 x 1024, 1024 - t) in both remaster generations, where
  s and t are the ROM quad's S/T. At the remaster's 1/1024 scale this is the
  native normalised domain, with v flipped and whole-repeat offsets. So the
  remaster chain tile IS the native tile. Flipped vertically, its alpha
  correlates 0.991 with the ROM image's alpha; the unflipped image correlates
  only 0.531. The entry is that image flipped, levelled to the ROM image's
  covered luminance (117) and bled.
- **Frame (0x02fa).** The remaster re-UV'd the frame: it draws a riveted steel
  bar from a 256x256 atlas at u 487..588 (atlas pixels 121.75..147.0), two
  repeats per bar. The ROM frame samples a 32x32 tube gradient across the
  native band S 9.72..33.16 and tiles it about 18 times per bar. Measured over
  both models' frame edges (median texel density along the bar), one native
  repeat is 39.87 atlas pixels. The entry takes the bar across the band, a
  rivet-free window of that length along it (atlas rows 72.0..111.87, seam
  0.25) and the ROM image's covered luminance (178). Unlevelled, the bar
  read 71 and drew near-black under the native shading.

An id is global, so the replacement reaches every model that draws it, in
every level. A scan of the ROM's image commands, room streams included:

- 0x0105 is also drawn in rooms of `bg_ark`, `bg_run` and `bg_sevx`. The
  replacement is tile-exact, so it lands correctly wherever it is drawn.
- 0x02fa is also drawn by `PcarweirdZ` (S 0.31..31.69), `PcarzilZ`
  (S 10.28..31.66) and rooms of `bg_dest` and `bg_crad`. That is why its
  band mirrors instead of clamping.

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
texprovtest.ps1` drives it with synthetic fixtures (74 checks, the
carried-coordinate table's included).

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

### Pairing by geometry

Pixels cannot pair a texture the remaster redrew, but on some levels the
source pairs it another way. Facility's remastered background
(`new/background/facility`) and its original one describe the same rooms:
both declare the same bounds, they hold 23364 and 24897 vertices, and 12040
of the remaster's 19603 triangles sit at exactly the positions of an original
triangle. Each such triangle names one remastered image and one original
image, so the pair comes from the level itself rather than from how the two
images look.

The same triangles also show whether the remastered image can stand in for
the native one. Over every shared edge, the remaster's texture-coordinate step
is compared with the original's. When the ratio is 1 on both axes and the
offset is a whole number of repeats, the image tiles exactly as the native
texture does, so the substitution is the same kind as every other entry.

**The rule**, applied to every pair on the level, with nothing chosen by hand:

- the coordinate ratio is 1 on both axes, within 1%, on at least 60% of the
  shared edges;
- the offset is a whole number of repeats, within 0.01, on at least 60% of
  them;
- the pair shares at least 4 triangles;
- the original image's id comes from the pixel stage, which matches the
  original bundle's images (copies of the ROM's own) to the image table, at
  NCC 0.75 or better;
- the remastered image is larger than the original one, so it is a remaster
  and not a carried-over copy;
- the id is not already mapped.

When one id meets the rule with more than one image, the pair with the most
shared triangles wins. The orientation is the pixel stage's flip for the
original image, since the two images share coordinates.

**Checked against the pixel stage.** All 13 accepted Facility entries that the
geometry reaches come back with the same remastered image, and every one of
their offsets is a whole number of repeats. The rule added 17 Facility ids:
`00a0 0101 0118 011a 0123 029f 02a0 02a1 02a6 02b8 02be 02bf 02c2 0492 05d0
05d2 05d6`. Among them are the bathroom and second-floor walls (`0123`), the
air duct (`029f 02a0 02a1`), the stairs (`02be`) and the biohazard sign
(`05d6`). The pixel stage had paired some of these remastered images with the
wrong id. For example, `0123`'s image had been placed beside `02c5`, and
`00a0`'s beside `02ac`.

**What the rule leaves out.** The biggest Facility walls (`02cf 02ce 02d1
02d3 00b5 0137`) do pair by geometry, but at a coordinate ratio of about 0.2,
0.5, 0.69 or 2. The remaster tiles them on a different period, so one native
repeat is a fraction or a multiple of the remastered image. A plain
substitution would draw them at the wrong scale. They are taken by the
re-tiling rule below instead.

### Re-tiled pairs

On the triangles both generations share, the exact relation between the two
texture coordinates is an affine map, solved per triangle from its three
corners. Weighted by area, each of these walls has one dominant map, and it
is a pure scale with no shear:

| id | remastered image | dominant scale (share of the shared area) | the image's own cells | cells per native repeat | drawn larger than the remaster by |
|---|---|---|---|---|---|
| `02cf` | 512x512 bathroom tiles | 0.1983 x 0.1984 (84%) | 5 x 5 tiles | 1 | +0.9%, +0.8% |
| `02ce` | 512x512 tiles | 0.2012 x 0.2015 (68%) | 5 x 5 tiles | 1 | -0.6%, -0.7% |
| `02d1` | 256x256 concrete | 2.0562 x 2.0548 (60%) | 1 | 2 | -2.7%, -2.7% |
| `02d3` | 256x256 concrete | 2.0507 x 2.0518 (68%) | 1 | 2 | -2.5%, -2.5% |
| `00b5` | 512x512 brick | 0.3654 x 0.3770 (53%) | 3 x 3 (a brick, two courses) | 1 | -8.8%, -11.6% |
| `0137` | 512x512 floor | 1.0000 x 1.0000, phase 0 (33%) | 2 x 2 | 2 (the whole image) | 0 |

The scale is how many remastered images one native repeat spans. The cells
are the image's own repeat grid: along each axis, the shift of 1/n of the
image at which it correlates best with itself (`nativeuv.repeat_cells`, 0.84
for the bathroom tiles at n = 5; an image with no correlation of 0.4 or more
is one cell).

**The rule** (`nativeuv.retile`): one native repeat can only hold a WHOLE
number of the image's cells and still tile, so it holds
k = max(1, round(scale x cells)) of them. A window of exactly 2.056 images
would put a 0.056-image discontinuity at every native repeat. The last column
of the table is what rounding costs: how much larger the drawn artwork is
than the remaster draws it, which is k / cells / scale - 1.

**Where the window starts.** When the window is a single cell of a larger
grid, the cell and its shift, in steps of 1/32 cell, are the ones that best
match the native image (normalised cross-correlation 0.925 for `02cf`, 0.910
for `02ce`, 0.747 for `00b5`). So a tile's grout and a brick's mortar land
where the native texture has them. When the window is whole images, the
start is the remaster's own phase at the native lattice, rounded to a whole
source pixel so the copy is not resampled. `0137` comes out at phase
0.7 px, scale 1: it is a direct entry, with no `sample` block.

The colours are the remaster's, not levelled, like the seventeen direct
entries beside them.

**What it does not reproduce:**

- The remaster gives each tile on a bathroom wall its own cell of the 5 x 5
  image, varying the grime; one cell repeats here.
- `00b5` is drawn at two scales in the remaster: 0.37 in the dominant rooms
  and 0.53 in the others (about 45% of the shared area). Only the dominant
  one is reproduced. In the other rooms the bricks are about 1.4 times the
  remaster's size, which is the native texture's own brick size.
- `0137` is rotated by about 105 degrees at scale 0.53 in about a third of
  the shared area. There the floor tiles are half the remaster's size.
- **`0137` bends where the cartridge's own coordinates bend.** On parts of
  the floor the native texture coordinates change scale and direction from
  one triangle to the next. In room 55, the lab beside the biohazard sign,
  for example, a single floor carries 1.8 to 6.0 repeats per 1000
  units at -60 to +30 degrees, while the remaster's coordinates on the same
  triangles are uniform (2.56 to 2.60, 0 degrees). The cartridge's tile is
  noise, so its bends never showed. Any image with lines drawn through
  those coordinates bends with them. Over the coplanar edges two `0137`
  triangles share, 35 of 394 change the coordinate gradient by more than
  10%; the 47 triangles they touch are 12.6% of the floor's area, in
  several rooms. No single native-domain image can be right on both sides
  of such an edge. The floor now follows the remaster's own coordinates
  there instead (see "Carried coordinates" below).

The ids are global. A scan of every room stream of the ROM's backgrounds for
image commands finds `00b5 02ce 02cf 02d1 02d3` only in Facility (`bg_ark`)
and `0137` also in the Bunker (`bg_sev`), so under XBLA the Bunker's floor
draws Facility's remastered floor tile too.

**The floor beyond the lockers (`02b9`).** The remaster lays the same floor
image as `0137` there. On the 90 shared triangles the dominant relation is
again the identity at phase zero (about 35% of the area; the rest at 1.5 x 2
or turned), so `02b9` is a direct entry for that image. Its native
coordinates carry no seam on any of their 99 shared edges. `bg_ark` only.

**The catwalk grating (`02c0`).** The cartridge has the diamond grating
twice: `02bf` and `02c0` are the same 32x32 artwork (normalised
cross-correlation 0.995, no shift), `02c0` about 17% brighter. Only `02c0` is
drawn in a level. Two image commands name it, both in `bg_ark`'s room
streams, and no background room stream names `02bf`. The remaster's copy of
the original draws Facility's gratings, the catwalk above the last tank
room included, with ONE image that the pixel stage pairs with `02bf` (0.9629)
a hair ahead of `02c0` (0.9625). The geometry rule took its id from that
match, so the grating went into the table as `02bf` and the surfaces stayed
the cartridge's. On the catwalk the original generation's four corners carry
the native texture coordinates exactly (u -1.783 and 5.023, v 4.891 and
32.881), and the remastered generation draws `_0x09A2C585` at the same
coordinates. So `02c0` is a direct entry for that image, with `02bf`'s
flips; table 153 -> 154.

**The tank room's tanks and pipes (`02bd`) are not taken.** They draw a flat
grey native tile under vertex colour. On the remastered background, the
same surfaces (a flat 64x64 legacy image, 1709 shared triangles) carry a
512x512 painted-metal image, but with no dominant relation: the largest
family of triangles is mirrored at 1.9 x 0.9 and covers about a third of the
area, the rest scattered across scales, swaps and turns. The pixel stage
cannot name the flat legacy image's id either, since a uniform image
correlates with nothing. Choosing a scale, a mirroring and a brightness for
it would be a judgment rather than a measurement. The carried coordinates
(below) do not reach them either: over the native `02bd` area, 53% has no
remastered surface in its plane at all (the tanks were re-tessellated), 24%
lies under the image `0101` takes and 22% under the painted metal. One id,
two remastered materials and a different surface: no image and no set of
coordinates stands for it.

**What Facility's remaster did not redraw.** Some surfaces look the same
under XBLA because the remaster kept the original images. Its background
draws them on the same triangles as the original:

- **The bulletin-board papers** (`041b 041d 041e 041f 0422`). Both boards
  carry 64x32 and 32x64 copies of the N64 papers (`.scaled`), and the
  original generation draws the same names. Nothing in the source redraws
  them. The only redrawn paper is the staff list, a different document
  (`prop/chrstafflist`). Community HD's own images for these ids are its
  own.
- **The window frames in the labs** (`0393`, a 64x32 copy in both
  generations) and the grey trim beside them (`02c4`, a 64x64 copy). The frames around the glazed lab
  doors are the doors' own model, which the door item now replaces
  (`docs/world-assets.md`, the swinging door).
- **Small wall pieces**: a 32x32 rusty plate (`0493`, beside the replaced
  `0492`) and a vent grille (`00f0`, re-UV'd with no dominant relation).
- **The conveyor**: its rails and legs (`0393`) draw the same image,
  `_0x001D0AD5`, in both generations.

**Redrawn on other geometry: the conveyor's exit frame (`0134`).** The
remaster did redraw the metal (`_0x0E2932C5`, 512x512), but on geometry of
its own. Each remastered triangle was sampled at three interior points
against the original generation's `0134` triangles in the same plane. Only
11% of the image's area lies over them, and there no relation dominates:
the identity covers about 4% of the image's area, the rest is scattered.
96% of `0134`'s own area has no remastered triangle at its positions. At
the conveyor's exit, 62% of the frame's area has no remastered surface in
its plane at all, and the rest mixes two images. No native-domain image can
stand for it. Like the tanks (`02bd`), it follows the remaster only by
drawing the remaster's geometry.

The biohazard sign that seemed to vanish between two close views in the lab
(`05d6`) is not a defect. Over nine steps between the two cameras, its
visible pixels track its projected area inside the viewport (7698 -> 100
pixels, the last 100 being a second, distant sign). As the view turns, the
sign leaves the left edge of the frame; ORIGINAL draws it the same way.

**Every level that draws these ids changes.** The ids are global. By the pixel
stage, `0101` is also drawn on Bunker 1 and 2, Surface 1 and the Train; `011a`
and `0123` on the Dam; `0123` on Runway and Surface 1; `0492` in the Depot and
Statue; `05d0` in the Cradle; `05d6` on Runway. `02c2` is a metal tile the
gas-plant doors, cars and gas tank models share. Under XBLA all of them now
draw Facility's remaster of the same native tile.

### Carried coordinates

Some direct entries are right as images and wrong as drawn, because the
cartridge's own texture coordinates bend. On Facility's floor (`0137`, and
`02b9` beyond the lockers) the coordinates change scale and direction from
one triangle to the next; the cartridge's noise tile hid it, and the
remaster's tiled floor drawn through those coordinates shows every grout
line kinking at a triangle edge. The remaster draws the same surfaces with
coordinates of its own, which do not bend. So for those triangles the set
carries the remaster's coordinates as data, and the renderer draws the
triangle with them.

**Where the triangle and its coordinates come from.** The pairing of
triangles used for the mapping does not reach here: the remaster's two
generations re-triangulate the cartridge's rooms, so a native triangle is
looked for on the remastered SURFACE instead (`tools\texpack\uvcarry.py`):

- The native triangles are read from the user's ROM: the stage's background
  file (`levelinfotable`, `src/game/bg.c`), its room table, each room's point
  table and lists ('Background File Data/background data.txt'; the room
  record holds the three offsets first and the position after them, as the
  decomp's `bg_room_data` says and the file confirms). Each triangle is named
  by its room and the three point-table entries its corners were loaded from
  (a segment-14 vertex operand, offset / 16), and the C0 command gives its
  image ('Texture application (Rooms).txt').
- The remastered background (`new/background/<stage>`) is authored at the
  room coordinates times one scale, fitted from the two vertex sets by least
  squares over the vertices that coincide: Facility 1.560002 (it is
  100 / the stage's last `levelinfotable` column, docs/world-assets.md).
- Its triangles are the reader's (`xblaworld.read_level`), whose draws come
  back already expanded into flat triangle lists. They are taken three at a
  time as they stand; an earlier revision expanded Facility's 67 quad lists
  and 13 fans a second time, which read 19603 triangles where the item has
  13913 - 13708 of them not the remaster's, and 8200 of its own missing.
- A native triangle takes the remastered triangle of the entry's image that
  lies in its plane (normals within 0.999, 2 units) and contains its centroid
  (failing that, most of six interior samples; failing that, the nearest
  such triangle within 96 units, across the remaster's own seams). That
  triangle's map is affine, and is evaluated at the native corners:
  s = 32 w u, t = 32 h (1 - v), the relation that makes the original
  generation's coordinates equal the cartridge's. A triangle under which the
  remaster changes its own map (beyond whole repeats) is refused.
- An id is carried only when the cartridge's coordinates bend and the
  carried ones do not: of the coplanar edges two of its triangles share, at
  least 4 change the coordinate gradient by more than 10% under the
  cartridge's coordinates, and at most half as many under the carried ones.
  Only triangles whose carried coordinates differ from the cartridge's by
  more than whole repeats get a row, shifted by an even number of repeats to
  sit beside the cartridge's values; a clamped tile is never carried.

On Facility that takes two ids and 423 triangles: `0137` (49 bent edges ->
0, 371 triangles carried, 70 already equal to whole repeats, none uncovered)
and `02b9` (30 -> 0, 52 carried, 40 equal). Every other direct id keeps the
cartridge's coordinates, by the same rule: theirs do not bend.

**The plates the double-read left (FIXED 2026-10-02).** With the corrupted
triangle set 12 of `0137`'s triangles found no remastered surface and drew
the cartridge's coordinates between carried neighbours. Their tile pattern
stood off by a fraction of a repeat - the floor read as tiles laid in
overlapping plates. Measured over every coplanar edge and T-junction the
floor ids' triangles share: 14 steps in the pattern before, 2 after (both
on `02b9` where room 26 meets room 27, carried on both sides - a seam in
the remaster's own map).

**The table** is written beside the pack, `texpacks\xbla\uv\<LEVELID>.sluv`
(little-endian: 'SLUV', version 1, the LEVELID, the row count, then 24-byte
rows of room, image id, the three point entries in increasing order, a zero
pad word and the three (s, t) pairs, sorted). `prepare-textures.ps1 -Xbla`
derives it when the ROM is at hand (`SL_ROM`, else `baserom.u.z64` at the
root) and says per stage what it carried or why it refused. It holds
coordinates and indices only, and like the pack it is generated, gitignored
and never committed or packaged.

**At runtime** (`emit_tri_room`, `src/gfx/sl_gfx_dl.c`; the table is read by
`sl_texprov_uv`, `src/gfx/sl_gfx_texprov.c`) a triangle is carried only when
it is room geometry, its three corners came from the room's point table,
the render tile's image is the row's id and the active set replaces that
image. The three vertex slots' s/t are swapped for the row's before the
triangle is clipped and drawn, and restored after it, so the vertex buffer
the next command sees is unchanged. Under ORIGINAL nothing is opened; a set
with no table draws the cartridge's coordinates. `SL_TEX_UVCARRY=0` is the
control.

**Not reached.** The fit refuses a stage whose remastered background does
not pair with its rooms on one scale; on the user's source that is every
stage the mapping names except Facility, Control, the Frigate, Silo, Streets
and the Train, and those five carry nothing (their coordinates do not bend
under a direct entry). The Bunker draws `0137` too and has no table, so its
floor keeps the cartridge's coordinates. The multiplayer Facility is another
LEVELID and has no table either.

### The monitors' images (#81)

The control room's main screen in Bunker 1 drew the cartridge's own images
under XBLA. The remaster's are in the source as standalone bundles under
`texture/monitors/` - one per image, named for it (`earthmap`, `screen1` ..
`screen9`, `screennew1` .. `screennew4`, `screenstatic`, the Cyrillic labels
and more) - in two generations: `texture/monitors/<name>` the legacy copy at
the cartridge's size, `new/texture/monitors/<name>` the remaster's. The
legacy copy pairs with the ROM's image by pixels and the remaster's with the
legacy one by NAME, the way the runtime pairs whole items by path.

Measured at playtest mark 6 over 700 reads (the screens animate - the
game's own image cycling, which the provider only follows): every id the
screens asked for and the XBLA set lacked was dumped from the running game
(`SL_TEX_DUMP`) and compared with every legacy monitor image under the four
orientations. Taken, all at the `v` orientation and all remastered (larger
than the legacy copy; `none` from the remaster's image to the legacy one, by
inspection - the art is redrawn, so pixels cannot say):

| id | legacy NCC | resource |
|---|---|---|
| `08a6` | 0.998 | `earthmap` (128x48 -> 512x256) |
| `08af` | 0.999 | `screennew1` |
| `0892` | 0.925 | `screen1` |
| `08b0` | 1.000 | `screennew2` |
| `0893` | 0.933 | `screen2` (the remaster draws a bar chart in its place) |
| `0897` | 0.888 | `screen7` |
| `08b3` | 0.989 | `screenstatic` |

Left out: the Cyrillic labels (`088c`, `088d`, `0891`), the `drax` panels
(`0245` .. `0248`, `0899` .. `089d`) and `anim2.x` - carry-overs, the
remaster's copy identical to the legacy one; `0898`, `08a7`, `08a9`, `08b7`,
`07bc` - no pairing above 0.6; `0894` (`screen3`, 0.765) and `0896`
(`screen6`, 0.642) below the bar this table used (0.85).

The ids are the game's, not Bunker 1's: any stage whose monitors show these
images draws the remaster's now. Facility does (its desk monitor at the
mark accepted in playtest shows `screennew2`, the remaster's list of names, where
the cartridge's bars of text were).

### Coverage limits

- **The mapping is the ceiling, not the engine.** 468 of the image table's
  2698 ids have a Community HD mapping and 161 an accepted XBLA one, 555
  distinct. The other 2143 draw the ROM's artwork under every set because no
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
