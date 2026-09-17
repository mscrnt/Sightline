# Native asset overrides

Replace a boot-screen model with your own glTF, without editing source and
without rebuilding. Drop a file in, the game draws it; delete the file, the
original comes back.

The feature is **opt-in and off by default**. With no override file installed,
the game takes the original path exactly — same call, same draw order, same
display list, same timing. Nothing new is loaded and nothing new is required.

---

## Install one

```
.\tools\windows\asset-import.ps1 -Asset boot.nintendo_logo -Input C:\models\mine.glb
.\tools\windows\asset-import.ps1 -Asset boot.rareware_logo -Input C:\models\other.glb
.\tools\windows\play.ps1
```

Remove it again:

```
.\tools\windows\asset-import.ps1 -Remove boot.nintendo_logo
```

`-Where` prints the override directory, `-List` shows what is installed, and
`-Help` prints the full supported glTF subset. No rebuild is needed for any of
this, and no file in this repository changes.

## Asset ids

| id | what it replaces |
| --- | --- |
| `boot.nintendo_logo` | the model on the Nintendo boot screen |
| `boot.rareware_logo` | the model on the Rareware boot screen |

## Where the files live

Two places, searched in a fixed order. **Yours always wins.**

```
your install directory   ->   data/asset-overrides   ->   the original asset
```

### 1. Your install directory

Where `asset-import.ps1` writes. One ladder, implemented **twice and kept
identical** — `sl_asset_override_dir()` (`src/native/sl_asset_override.c`)
and `override_root()` (`tools/asset/gltf_import.py`):

1. `$SL_ASSET_OVERRIDE_DIR`, if set and non-empty
2. `%LOCALAPPDATA%\sightline\assets`
3. `%USERPROFILE%\AppData\Local\sightline\assets`
4. `%TEMP%\sightline\assets`

...then `boot/nintendo_logo.slmodel` or `boot/rareware_logo.slmodel`.

This is the same ladder `play.ps1` uses for the EEPROM. A model you import is
player content and lives outside the tree for the same reason the save does.

### 2. `data/asset-overrides/` — the committed models

Models that ship with the repository. A fresh clone shows them with no import
step at all; that is the point of them.

The directory is found from the **executable's own location**, never from a
compiled-in path (which would bake one machine's directory into every binary)
and never from the working directory (which is wherever the player launched
from):

- `<exe dir>\..\..\data\overrides` — a source tree, `build\win32\sightline.exe`
- `<exe dir>\data\overrides` — a packaged layout, data beside the exe

A build that cannot determine its own path simply has no committed step.

### Precedence, and why that way round

The install directory is searched **first**, so importing your own model
replaces a shipped one. Nothing is uninstalled and nothing is overwritten —
the committed file stays where it is, and `-Remove` uncovers it again.
The reverse order would let the repository silently override the player.

A candidate that FAILS TO LOAD does not end the search; the next one is
tried. That matters for exactly one case, the upgrade: a model installed by
an older importer is a version this build cannot read, and stopping there
would leave the player with the original logo while a perfectly good model
sat one directory below. The stale file still produces its warning.

### Turning it off

`SL_ASSET_OVERRIDES=0` disables the whole seam — committed models included —
and both boot screens take their original code path unchanged. The check runs
before any path resolution, so the off state costs a cached compare.

This exists because the default changed. While overrides lived only in your
install directory, "off" was the absence of a file and `-Remove` was the off
switch. Now that models ship in the tree, deleting a tracked file is not an
acceptable way to see the original logos.

If the two path implementations ever disagree the importer writes a file the
game never looks for — a silent no-op — so a test asserts the id→path table
matches the C loader verbatim.

---

## Game textures are referenced, never embedded

A model that reuses one of the game's own textures cannot ship with those
pixels in it: project rule 2 forbids ROM-derived assets in the repository,
absolutely. Geometry and materials are the author's own work and travel fine.
The pixels do not.

So a texture slot in the file is **either** embedded pixels **or** a
reference to a game texture by identifier:

| identifier | what it is | resolved from |
| --- | --- | --- |
| `rareware.env_field` | the silver environment map, 32x32 RGBA16 | segment 2 at `0x02004FE8` |
| `rareware.env_gold` | the gold environment map, 32x32 RGBA16 | segment 2 at `0x02005FF0` |
| `nintendo.logo_i8` | the logo's I8 texture, 32x32 | the `PnintendologoZ` prop's texture record |

The identifiers are the API: stable and meaningful, never raw addresses or
array indices. Those are exactly the things that move under a refactor, and a
moved index does not fail — it silently resolves to the wrong picture.

### Detection is content-addressed

The importer **hashes every incoming texture** and compares it against the
game textures it can reach. On a match it writes a reference and drops the
pixels. It does not ask the author, because that would make rule 2 a matter
of discipline; hashing makes it mechanical. This applies to a model you
install for yourself too — no `.slmodel` this tool writes contains game
pixels, wherever it is going.

`--texture-ref IMAGE=IDENTIFIER` declares a texture the content check misses
because it was re-saved or edited. The hash stored is still the **game's**,
not the author's edit, because the game's copy is what will be resolved.

`asset-import.ps1 -GameTextures` lists what can be referenced.

### Where the registry's knowledge comes from

- **Rareware** — DERIVED at import time from `assets/rarewarelogo.c`, which is
  already committed source. Nothing is stored for these: the digests are
  recomputed every run, so they cannot go stale and add no bytes.
- **Nintendo** — two committed DIGESTS (a SHA-256 and an FNV-1a). Those pixels
  are inside a 1172-compressed ROM prop and are not in the tree, so there is
  nothing to derive them from. The digests are one-way hashes over a 32x32
  image; they cannot reconstruct a pixel and cannot be rendered. Their only
  function is to let the importer REFUSE the pixels and let the runtime prove
  it resolved the right ones. Deleting them costs automatic detection for that
  identifier and nothing else.

### Committing a model

`asset-import.ps1 -Repo -Asset <id> -Input <file>` writes into
`data/asset-overrides/` and **refuses to write any texture that is neither a
reference to a game texture nor declared in the glTF as the author's own**.

For game textures the rule is not "no texture we recognise" — it is *no
embedded pixels at all*. A recognition-based rule is only as good as the
registry, and a texture the registry has never seen would sail through it.

The one way past it is a declaration the AUTHOR writes into the file:

```json
"extras": {
  "sl_authored": true,
  "sl_authored_provenance": "how these pixels were made"
}
```

on the glTF image or texture. It is never inferred from the pixels, the
provenance string is required and non-empty, and it is read only *after*
content-addressed game-texture detection has already turned any recognised
texture into a reference — so it cannot launder ROM pixels. Forgetting it
fails closed. See the AUTHORED TEXTURES note in `tools/asset/gltf_import.py`.

### Building a committed model

A committed model is **source**, not a binary. The glTF, its buffers and its
textures live under `data/asset-overrides/source/<group>/<name>/`, where the
directory names the asset id (`source/boot/goldeneye_logo` →
`boot.goldeneye_logo`). `tools/asset/build_repo_assets.py` converts every
package through ordinary `--repo` mode, and `build.ps1` runs it, so a player
who builds gets the models with no import step.

The `.slmodel` it writes is a build output. It lands in
`build\win32\data\asset-overrides\`, which is the exe-adjacent repository
candidate the runtime already searches, and it is **not tracked** — the
reviewable glTF is what is committed.

### The runtime check

Each reference slot carries the FNV-1a of the pixels the importer refused to
embed. At load the runtime recomputes it over what it resolved. Equal means
the player's game data holds exactly the texture the author saw; unequal —
a different region, revision or ROM hack — falls back to the original logo
rather than rendering a model wearing the wrong picture.

---

## The seam

```
asset id  ->  optional native override  ->  otherwise the original
```

Two halves, and neither does the other's job:

- **`src/native/sl_asset_override.c`** — path resolution, loading, validation.
  Never touches GL.
- **`src/gfx/sl_gfx_dl.c`** (`draw_asset_override`) — the drawing. Never touches
  a file.

The two game call sites (`src/game/front.c`, `src/game/title.c`) emit **one
bridge command** into the display list exactly where the original geometry
would have gone. The custom draw therefore inherits the screen's projection,
modelview, viewport and render order — all of which are properties of a
*position in the list*, not of a moment in the frame. GL issued from a game
constructor would be ordered against the list only by luck.

Both call sites are guarded `#ifndef __sgi` with the original as a verbatim
`#else` arm, so the cartridge build is untouched. `src/gfx` and `src/native` are
outside the cartridge Makefile's globs entirely.

### Choreography is not the asset

The screens keep their own duration, camera, rotation, scale-in, fade and
transitions. Only the **model** is replaceable. The bridge command carries the
screen's own per-frame fade level so a custom logo fades in on the original
schedule; nothing else about the sequence moves.

Consequently a custom model inherits the original's spin, including the half
of each revolution where you are looking at its back. That is Rare's
choreography, faithfully reproduced — not a defect.

## The file format

`SLM1`, **version 2**: little-endian, fixed-width, offset-addressed, 128-byte
header. The runtime reads this and nothing else — **no glTF, no JSON, no PNG
or JPEG decoder ships in the game.** Conversion is entirely offline.

Every field is validated before a byte is dereferenced: magic, version, header
size, declared length against the real file length, every count against an
explicit cap, every offset and extent against the file, every triangle index
against the vertex count, every material index against the material count,
every texture index, dimension and byte length. Multiplications are checked for
overflow *before* they are performed.

### Texture slots (what version 2 changed)

A slot is 32 bytes and is **either** embedded pixels **or** a reference:

| offset | field | embedded | reference |
| --- | --- | --- | --- |
| 0 | width | texels | texels |
| 4 | height | texels | texels |
| 8 | pixel offset | into the file | must be 0 |
| 12 | pixel length | `w*h*4` | must be 0 |
| 16 | kind | 0 | 1 |
| 20 | identifier offset | must be 0 | into the file |
| 24 | identifier length | must be 0 | 1..63, excluding the NUL |
| 28 | FNV-1a | 0 | of the pixels the reference stands for |

The loader rejects a slot that carries both, an identifier that is not
NUL-terminated inside the file, an identifier holding anything outside
`[a-z0-9._]` (it is compared with `strcmp` and printed in a warning, so its
bytes are constrained rather than trusted), and any kind it does not know.

A reference costs the same memory once resolved as embedded pixels would, so
both count against the decoded-bytes budget below — exempting references would
make the cap describe the file rather than the runtime.

The magic stays `SLM1`; it names the family, and the version field is what a
reader must check. A version-1 file is rejected with a message telling you to
re-import, and the search moves on to the next candidate.

### Limits

Enforced identically by the importer and the loader (a test asserts the two
tables match):

| | |
| --- | --- |
| vertices | 1 000 000 |
| indices | 3 000 000 |
| primitives | 256 |
| materials | 256 |
| textures | 16 |
| texture dimension | 2048 (larger is halved, not rejected) |
| decoded texture bytes | 64 MiB (embedded and referenced alike) |
| texture identifier | 63 characters |
| file size | 96 MiB |

### Fallback contract

| situation | result |
| --- | --- |
| no file anywhere | the original, silently |
| valid file | the custom model, one line logged |
| present but invalid | **one** warning naming the reason, then the next candidate |
| wrong format version | **one** warning, then the next candidate |
| reference names an unknown identifier | **one** warning, then the original |
| referenced texture not resident yet | retried, bounded; then the original |
| referenced texture has different pixels | **one** warning naming both hashes, then the original |
| `SL_ASSET_OVERRIDES=0` | the original, silently, nothing read |

Never a crash, never an empty screen, never a half-textured model — reference
resolution is all-or-nothing, because one slot resolved and one not would look
like a rendering bug rather than a missing ROM.

Load happens once per id per process and the answer is cached, including the
negative one — a missing override costs one `fopen` per candidate per boot and
nothing after. Only *resolution* is retried, never the file read, so a retry
costs arithmetic rather than a re-read of a multi-megabyte model.

**Load order, MEASURED.** Both hooks are constructors that run after their
screen's `init`, so the game data a reference needs is already resident the
first time the asset is asked for. Every resolution on this machine reported
`attempt 1` — Nintendo at record index 83, Rareware at 588. The retry loop is
defensive, not load-bearing.

---

## Supported glTF 2.0 subset

**Accepted:** static triangle meshes (mode 4), multiple primitives and
materials, node transforms (baked into the vertices at import), `POSITION`,
`NORMAL`, `TEXCOORD_0`, `COLOR_0`, indexed or non-indexed, `baseColorFactor`,
`baseColorTexture` (PNG), `alphaMode` OPAQUE/MASK/BLEND with `alphaCutoff`,
`doubleSided`, and `KHR_materials_unlit`.

**Rejected with a message rather than a wrong picture:** skinning, morph
targets, animation tracks, non-triangle modes, sparse accessors, normal /
occlusion / emissive / metallicRoughness textures, JPEG textures, interlaced
PNG, and any extension not named above.

**Ignored:** `metallicFactor`, `roughnessFactor`, `TANGENT`, `TEXCOORD_1+`.

There is **no PBR renderer here.** A model with `NORMAL`s gets one fixed
directional light from the camera; one without — or one whose material declares
`KHR_materials_unlit` — is drawn unlit from its base colour. Either way the
screen's own fade is applied on top. Bake the look you want into the base
colour texture; the renderer will not approximate what it cannot do.

> **Known gap.** glTF requires a client to compute flat normals when `NORMAL`
> is absent. This importer does not — it clears the normals flag and the model
> is drawn unlit. A mesh exported without normals will therefore look flatter
> here than in a PBR viewer, which synthesises normals and lights them. The fix
> is one place (`emit_primitive` in `tools/asset/gltf_import.py`) and is not
> yet made.

### Coordinate contract

1 glTF unit == 1 N64 model unit, for **every** asset. The game holds no
per-logo scale factor. Axes are glTF's (+Y up, +Z toward the viewer,
right-handed). Front faces are counter-clockwise and back faces are culled
unless the material is `doubleSided`. UV origin is glTF's — (0,0) is the
top-left texel — and textures upload in file row order, so no flip is applied.

No rotation is ever baked into an asset; the screen owns the rotation.

### Reflections: generated texture coordinates

A model's UVs are **baked** — written once at import, never changing. That is
right for a hand-painted texture and wrong for a reflection: sampled through
baked UVs, a reflection is welded to the surface and turns *with* the model.

The originals do not work that way. They set `G_TEXTURE_GEN`, and the
coordinate is manufactured **per frame from the vertex normal** under the
matrix in force, so the highlight **sweeps across** the letterforms while the
logo spins. No model file can express that, because what changes each frame is
the coordinate, not the mesh.

Override models get the same treatment, from the same code — `texgen_generate()`
in `src/gfx/sl_gfx_dl.c` is one function with two callers, so the display-list
path and the override path cannot disagree about the formula, the pre-scale
domain, or the open questions the `SL_TEXGEN_*` arms exist to settle.

**You usually declare nothing.** A material whose texture is a
[reference](#game-textures-are-referenced-never-embedded) to one of the game's
own reflection maps inherits generation automatically, because the original
material using that map sets `G_TEXTURE_GEN`:

| identifier | evidence |
| --- | --- |
| `rareware.env_field` | sourced — `title.c:338` sets `G_TEXTURE_GEN` for the draw that binds it |
| `rareware.env_gold` | sourced — the same draw, same geometry mode |
| `nintendo.logo_i8` | measured — the walker's own decline line reads `mode=00062205`, and `0x00040000` of that is `G_TEXTURE_GEN` |

A material with a texture of **your own** is untouched: it has no reference
slot, so the implication never fires and your UVs are used exactly as authored.

**To override that**, put `sl_texgen` in the material's `extras`:

```json
{ "materials": [ { "name": "badge", "extras": { "sl_texgen": true } } ] }
```

`true` forces generation on, `false` forces it off, and leaving it out lets the
texture decide. It must be a real JSON boolean — `"true"` as a string is
rejected rather than guessed at. Setting both bits is refused at load.

Generation needs `NORMAL`; a material that asks for it on a mesh without
normals draws its stored UVs and says so once on stderr.

**This changed no file format.** The two flag bits are previously unused bits
of a material flags word that has been 32 bits wide since version 1 — no
offset, count or size moved, so `SL_AMDL_VERSION` is still 2 and the models
already installed need no re-import. An older build reading a file that
carries the bits ignores them and draws baked UVs, which is exactly what it did
before this existed.

`SL_TEXGEN_MODEL` switches arms from one binary: `0` off (baked UVs
everywhere), `1` per material (default), `2` forced on for every textured
material. It composes with `SL_TEXGEN_MODE`, `SL_TEXGEN_SWAP` and
`SL_TEXGEN_DEFBASIS`, which reach this path too because it is the same
function. `SL_DL_CENSUS=1` reports the counters (`sl_texgen: model draws=...`).

**How big should it be?** Every import prints your model's bounding box beside
the box the original occupies, and the uniform `--scale` that would match it:

| asset | original extents (model units) |
| --- | --- |
| `boot.nintendo_logo` | 3706 x 814 x 498 |
| `boot.rareware_logo` | 308 x 415 x 29 |

`--scale` is applied at import and baked into the installed file. **Nothing is
ever applied automatically** — when your model's aspect ratio differs from the
original's, matching width and matching height give different numbers, and the
tool prints both because only you can make that choice.

`--emit-sample` writes a small synthetic model already authored to one of those
boxes, so the whole loop can be walked before you have a model of your own.

---

## Cost

Measured 2026-09-05, Windows build, 960x720, via `SL_PHASE`, over each
screen's whole run (`B dl` divided by frames):

| screen | triangles | per frame | fps |
| --- | --- | --- | --- |
| Nintendo, custom, generated coordinates | 171 984 | 2.15 ms | 59.8 |
| Nintendo, custom, baked UVs (`SL_TEXGEN_MODEL=0`) | 171 984 | 1.42 ms | 59.9 |
| Nintendo, original | 1 018 | 14.42 ms | 59.5 |
| Rareware, custom, generated coordinates | 119 644 | 1.61 ms | 58.9 |
| Rareware, custom, baked UVs (`SL_TEXGEN_MODEL=0`) | 119 644 | 1.00 ms | 59.4 |
| Rareware, original | 268 | 4.50 ms | 59.8 |

Both custom rows are the **committed** models, measured with
`SL_ASSET_OVERRIDE_DIR` pointed at an empty directory so the shipped files are
the ones under test rather than whatever is installed.

Generating coordinates costs **+0.73 ms** on Nintendo and **+0.61 ms** on
Rareware — one pass over the vertex array per frame, nine multiplies and a
square root each. Both screens still draw in about a tenth of a 16.7 ms frame,
and both remain far cheaper than the originals they replace.

The Nintendo custom figure had earlier moved from 1.14 ms to 1.36 ms when the
model gained `NORMAL`: the mesh became lit rather than drawn flat. The
baked-UV rows above are that same configuration re-measured on this build.

The override path is roughly an order of magnitude **cheaper** than the
display-list interpreter it bypasses, while drawing ~170x the geometry — which
is consistent with the interpreter's cost being CPU-side list walking rather
than GL submission (B-094). Frame time is not the binding constraint on model
size; the format's vertex and index ceilings are.

---

## Rules

Anything derived from a ROM **never** enters this repository (project rules,
rule 2 — `docs/project-rules.md`). No path to anyone's content is hardcoded in committed source.

A model MAY be committed — its geometry and materials, which are the author's
own work. Every texture slot in a committed `.slmodel` is a reference and
carries no pixels; `--repo` enforces that by category rather than by
recognition, and a test walks every `.slmodel` under `data/asset-overrides/`
asserting the same thing.

Pixels the author made themselves are the one exception, and only when the
source glTF declares them (`extras.sl_authored` plus a non-empty
`extras.sl_authored_provenance`). That declaration says nothing about ROM
content — game-texture detection has already run and already won — it only
separates "no ROM assets" from "no artwork of the author's own", which are two
rules with two different justifications.
