# Setting up the COMMUNITY HD and XBLA presentations

How to prepare the inputs the **PRESENTATION** setting draws from. ORIGINAL
needs nothing beyond the game ROM; COMMUNITY HD needs a texture pack; XBLA
needs a texture pack **and** the world assets (backgrounds, skies,
characters, props and first-person models).

> **Release package or source build.** The preparation below runs from a
> source checkout. A build of the tree (`.\tools\windows\build.ps1`, launched
> with `.\tools\windows\play.ps1`) reads the checkout's own `texpacks\` and
> `worldassets\`. The v0.3.0 release package reads prepared sets from
> `%LOCALAPPDATA%\sightline\assets\`: its `Get-Textures.cmd` fetches
> COMMUNITY HD there on its own, and for XBLA pass
> `-TexturesOut "$env:LOCALAPPDATA\sightline\assets\texpacks"` and
> `-WorldOut "$env:LOCALAPPDATA\sightline\assets\worldassets"` to
> `prepare-all.ps1`. The v0.2.0 package predates PRESENTATION.

The runtime design is in [texture-packs.md](texture-packs.md) and
[world-assets.md](world-assets.md); this page is the practical recipe.

## The three inputs, and why they are different files

| Input | Needed for | Where it goes | Identity |
|---|---|---|---|
| **GoldenEye 007 (USA) N64 ROM** - required | the game itself, and several preparation steps | `baserom.u.z64` in the repository root, or `SL_ROM` | a plain big-endian `.z64`, 12582912 bytes, SHA-1 `abe01e4aeb033b6c0836819f549c791b26cfde83` |
| **XBLA source archive** - optional | the XBLA texture pack and the XBLA world assets | `texsources\xbla-source.7z`, or any path passed as `-XblaArchive` | user-supplied; see below |
| **Community HD release** - optional | the COMMUNITY HD texture pack | `texsources\community-hd.zip` | downloaded by `prepare-textures.ps1` from its maintainers' GitHub release, pinned and verified by `tools\texpack\community-source.json` |

The ROM is the game. The other two are only sources of replacement artwork,
converted once into the prepared roots below; the game never reads either
archive directly. The ROM and the XBLA source are user-supplied: Sightline
ships, mirrors and locates neither. The Community HD release comes from its
own maintainers' GitHub release, fetched on your machine by the pinned
downloader in `prepare-textures.ps1`; Sightline does not redistribute it.

### The XBLA source archive

The XBLA source is **user-supplied**: Sightline does not provide, locate or
download it, and this page does not point at one. One archive is known to
work with the current tools:

| | |
|---|---|
| file | `GoldenEye_007_XBLA.7z` |
| size | 242343042 bytes |
| SHA-256 | `dfb7b35afd9767f671469e00cf34bb454b6acc3c2ddf93d4953bc056611f6eb2` |
| SHA-1 | `1522e3d4f36d56f4d089f9c2fdebfb73b5ef2c9b` |

These are checksums of that **archive file as a whole**: not of anything
inside it, not of an extracted container, and not of the N64 ROM. Renaming or
moving the file does not change its bytes, so a copy renamed to
`xbla-source.7z` still has both hashes above. Repacking or recompressing the
same content produces a different archive with different hashes, which may
still work: the tools discover every item inside the archive from its own
index rather than assuming a layout. A matching hash identifies the tested,
known-compatible archive; it says nothing about where a copy came from.

Rename it to the conventional path, or leave it where it is and pass
`-XblaArchive` to each preparation script. To check it in PowerShell:

```powershell
(Get-Item .\texsources\xbla-source.7z).Length                     # 242343042
(Get-FileHash .\texsources\xbla-source.7z -Algorithm SHA256).Hash # DFB7B35AFD9767F671469E00CF34BB454B6ACC3C2DDF93D4953BC056611F6EB2
(Get-FileHash .\texsources\xbla-source.7z -Algorithm SHA1).Hash   # 1522E3D4F36D56F4D089F9C2FDEBFB73B5EF2C9B
```

`Get-FileHash` prints upper case; the values are the same.

## Prerequisites

- The Windows toolchain and Python venv: `.\tools\windows\setup.ps1`, then
  `.\tools\windows\setup.ps1 -Install` for anything missing.
- **7-Zip**, for the XBLA archive only - found on `PATH`, under
  `Program Files\7-Zip\7z.exe`, or wherever `SL_SEVENZIP` points. It is a
  preparation tool; the game never needs it.
- The ROM in place (`baserom.u.z64` or `SL_ROM`): world-asset preparation
  reads the game's own models and tables from it (for example the watch
  hand's cuffs, and the corrections recorded in
  [divergences.md](divergences.md)).

All three directories involved - `texsources\`, `texpacks\` and
`worldassets\` - are git-ignored. Nothing in them is ever committed.

## Everything at once

```powershell
.\tools\windows\prepare-all.ps1
```

prepares both texture packs (step 1) and every XBLA world asset of the
single-player campaign (step 2), one after the other. Both steps always run,
and it exits non-zero if either did. It takes `-XblaArchive <path>`,
`-CommunityArchive <path>`, `-NoDownload`, `-TexturesOut <root>`,
`-WorldOut <root>`, `-Force` and `-KeepScratch`, handing each to the step
that uses it. The steps below are what it runs, and each also works on its
own.

## Step 1: texture packs

```powershell
.\tools\windows\prepare-textures.ps1              # every set whose source is present
.\tools\windows\prepare-textures.ps1 -Community   # Community HD only
.\tools\windows\prepare-textures.ps1 -Xbla        # XBLA only
```

Writes `texpacks\community\<hex4>.sltx` and `texpacks\xbla\<hex4>.sltx`.
The Community HD release is fetched from its maintainers and verified against
the pinned record; `-NoDownload` keeps the run offline, and
`-CommunityArchive <path>` reads a copy you already have. A texture a set does
not cover draws the game's own artwork. A set whose source is not there is
reported and skipped.

COMMUNITY HD is complete after this step. XBLA also needs step 2.

## Step 2: XBLA world assets

```powershell
.\tools\windows\prepare-xbla-assets.ps1 -All
```

prepares everything the XBLA presentation draws on the 20 single-player
missions:

- the **19 backgrounds** the stages draw (Surface 2 draws Surface's) and the
  remaster's stage fog (`environment.slen`);
- the **7 domes** those stages draw that the source holds - Bunker 1 borrows
  Surface's dome and Streets, Depot and Cradle are lent Frigate's; the other
  stages keep the game's own sky;
- then stage by stage, the **props** (doors, vehicles, pickups, monitors),
  the **characters and heads** the stage can field, and the **player's
  items**: every first-person weapon, the watch hand and the player's own
  body.

The plan is read from the runtime's own stage, sky and lending tables
(`tools\worldasset\campaign.py`), so it prepares exactly what the game will
look for. Backgrounds and domes come first, because some prop corrections
read the prepared background beside them. Items run one at a time, the
archive is hashed and indexed once, and `-XblaArchive`, `-Out`, `-Force` and
`-KeepScratch` apply to the whole run. Multiplayer maps are not included.

Every item gets one line - PREPARED, CURRENT, SOURCE ABSENT or CONVERSION
REFUSED with its reason - and anything not prepared keeps the game's own
model. The closing summary names any stage step that did not finish cleanly,
and the run then exits non-zero. A rerun is cheap: an item already built from
the same source and tools reports CURRENT. `-Force` rebuilds anyway; `-Out
<root>` writes somewhere other than `worldassets\`.

The result is `worldassets\xbla\` - `background\`, `skydome\`, `char\`,
`head\`, `gun\`, `prop\` and `environment.slen`, about 1 GB. A first run
into an empty root took about five minutes on one development machine; a
rerun with nothing changed only checks.

### One stage at a time

The same script prepares single stages or items. Its `-Level` then means
two different things depending on the switch beside it:

- with **`-Background`** or **`-Skydome`**, `-Level` names the **source's
  item** (`background/<name>`, `skydome/<name>`);
- with **`-Props`**, **`-Characters`**, **`-Player`** or **`-ViewModels`**, it
  names the **stage as the launcher does** (`dam`, `bunker1`, `egypt`, ...),
  and the item list is derived from the game's own tables for that stage.
  `-Player` covers everything `-ViewModels` does plus the player's own body.

The two names differ for several stages, and the launcher's `temple` is the
multiplayer Temple, not Egyptian. Prepare a stage's background **before** its
props. What `-All` resolves for each stage:

| Stage | `-Background` item | `-Skydome` item | `-Props -Characters -Player` level |
|---|---|---|---|
| Dam | `dam` | `dam` | `dam` |
| Facility | `facility` | - | `facility` |
| Runway | `runway` | `runway` | `runway` |
| Surface | `surface` | `surface` | `surface` |
| Bunker 1 | `bunker` | (Surface's, borrowed) | `bunker1` |
| Silo | `silo` | - | `silo` |
| Frigate | `frigate` | `frigate` | `frigate` |
| Surface 2 | (Surface's) | `surface2` | `surface2` |
| Bunker 2 | `bunker2` | `bunker2` | `bunker2` |
| Statue | `statuepark` | - | `statue` |
| Archives | `archives` | - | `archives` |
| Streets | `streets` | (Frigate's, lent) | `streets` |
| Depot | `depot` | (Frigate's, lent) | `depot` |
| Train | `train` | - | `train` |
| Jungle | `jungle` | - | `jungle` |
| Control | `control` | - | `control` |
| Caverns | `cavern` | - | `caverns` |
| Cradle | `cradle` | (Frigate's, lent) | `cradle` |
| Aztec | `aztec` | - | `aztec` |
| Egyptian | `temple` | `egyptian` | `egypt` |

A dash means the game's own sky stays. For example, Egyptian alone:

```powershell
.\tools\windows\prepare-xbla-assets.ps1 -Level temple -Background
.\tools\windows\prepare-xbla-assets.ps1 -Level egyptian -Skydome
.\tools\windows\prepare-xbla-assets.ps1 -Level egypt -Props -Characters -Player
```

## Step 3: play

`play.ps1` finds `texpacks\` and `worldassets\` in the checkout and points the
game at them; `SL_TEXPACK_ROOT` and `SL_WORLDASSET_ROOT` override that, and
otherwise the game looks under `%LOCALAPPDATA%\sightline\assets\`.

Choose the presentation in any of three places - one stored value, shared by
all of them:

- the front end: **OPTIONS -> SETTINGS -> DISPLAY -> PRESENTATION**;
- the watch, in a level: its last tab, **SIGHTLINE -> GRAPHICS**;
- in play: **F5**, or the controller's **View / Create** button, steps
  ORIGINAL -> COMMUNITY HD -> XBLA -> ORIGINAL and names the result on the
  HUD.

Or start on one: `.\tools\windows\play.ps1 -Level dam -Presentation XBLA`.

To confirm a set was found, look in the console log for
`sl_texprov: TEXTURES=XBLA root=... found` and, under XBLA, for
`sl_worldasset: LOADED background/<item>`. `play.ps1` also says so before
launch when the selected presentation has nothing prepared.
