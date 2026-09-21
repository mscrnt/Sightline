# controllers/ - the watch's device-matched controller models (#63)

Two source packages, one per controller family SDL can classify, drawn on
the solo watch's Control Options page in place of the N64 pad whenever such
a pad drives the game (`src/native/sl_watch_controller.c`; the ORIGINAL /
MODERN profile that gated this left on 2026-09-20):

| package      | asset id                | family (SDL type)      | parts |
|--------------|-------------------------|------------------------|-------|
| `xbox/`      | `controllers.xbox`      | XBOX (360 / One)       | 15    |
| `dualsense/` | `controllers.dualsense` | PLAYSTATION (PS3/4/5)  | 17    |

They compile at build time (`tools/asset/build_repo_assets.py`, run by
`build.ps1`) to `build\win32\data\asset-overrides\controllers\*.slmodel`;
the binaries are build outputs and are not tracked.

## THIRD-PARTY WORK - read before touching

Both models are **CC-BY-4.0** work by their Sketchfab authors, modified for
Sightline. They are **not** Sightline-authored and **not** under the CC0
grant in `../../LICENSE.md`: every image in each glTF is declared
`extras.sl_third_party` (title, author, author URL, source URL, licence,
the credit line, the changes), never `sl_authored`, and the importer
refuses a file that claims both. The credit line a release must reproduce
is in each package's `ATTRIBUTION.md`; the inventory row is in
`LICENSES/README.md`. No endorsement by the authors is implied.

The one exception inside each model: the node flagged `extras.sl_authored_part`
(`btn_guide` on the Xbox pad, `btn_ps` on the DualSense) is the project
owner's own replacement geometry - a plain dome / oval standing in for the
manufacturer's logo-shaped button. Manufacturer logos were removed (the
DualSense wordmark tile is painted a flat colour); button letters and
symbols, d-pad arrows and the Create / Options glyphs are kept.

Not derived from any GoldenEye data.

## Parts

Each glTF has one node per physical control, its mesh in local coordinates
about the node's origin (the pivot: the centre of the part's top surface),
labelled `extras.sl_part`. `metadata.json` `"parts"` maps those labels onto
the canonical physical parts the runtime addresses (`PARTS` in
`tools/asset/gltf_import.py` = `SL_PART_*` in `src/sl_asset_override.h`):
BODY, LEFT_STICK, RIGHT_STICK, DPAD, FACE_SOUTH / EAST / WEST / NORTH,
LEFT/RIGHT_SHOULDER, LEFT/RIGHT_TRIGGER, MENU, BACK, GUIDE, and on the
DualSense MUTE and TOUCHPAD. Each canonical part resolves exactly once per
model; the test suite (`tools/asset/test_gltf_import.py` [35]) checks the
set, the provenance declarations and the compile.

The parts are PHYSICAL CONTROLS ONLY. Nothing here says what a button does:
the binding registry owns the actions and the watch draws whatever the
hands are doing (`sl_input_pad_visual`). The historical `sl_n64` "default
N64 slot" metadata the package builder wrote was dropped on import.

## Frame

GjoypadZ's, the N64 pad the watch draws: +x = the player's right, +y = up
toward the watch camera, -z = the top edge. Body 788 x 253 x 836; both
models are fitted to the 788 width and centred (the figures are the package
builder's measurement of GjoypadZ, recorded in `metadata.json`). The page's
own matrices, look-at and perspective are handed over unchanged.

**The NORMAL data in both `.bin` files is NOT in this frame** (measured
2026-09-20): the package builder re-based `POSITION` and left `NORMAL` in
the source file's axes - the DualSense's stored (x, y, z) sits where its
geometry says (-x, -z, -y), the Xbox pad's where it says (x, -z, y). Drawn
as shipped, every polygon the watch camera sees carried a normal pointing
sideways or away from the light and took the ambient term alone: the white
DualSense drew as a dark silhouette (RGB 69 on the green face) and read as
nothing drawn. The `.bin` files stay verbatim (their SHA-1 is the oracle);
the importer measures each model's normals against its winding on every
compile and re-bases them by the signed axis permutation the geometry
implies (`tools/asset/gltf_import.py`, `rebase_normals`; the `normals` line
in the build output records the agreement before and after). A future
package whose normals arrive correct passes through untouched.

**The Xbox package's face is not level in this frame either** (measured
2026-09-20, defect round 4): its face-level parts - the four face buttons,
the d-pad, View and Menu - lie on a plane pitched 16.5 degrees about x, the
top edge down (the Y button's pivot at y 1.0, the d-pad's at 55), while the
DualSense's lie within a degree of level. Drawn as shipped the Xbox face was
tilted away from the page's camera and showed its top edge - the owner's
"it doesn't face the right way". The importer fits a plane through those
parts on every compile (`gltf_import.py`, `face_level_fit`) and turns a
model whose plane is pitched or rolled past 3 degrees level as a whole -
pivots, part-relative geometry and normals together, so the pose animation
about each pivot is unchanged (the `face` line in the build output). The
`.bin` stays verbatim; a level package passes through untouched.

**The facing, measured against the N64 pad** (2026-09-20, round 5): by the
mesh (the area-weighted winding normal of the up-facing body surface in
the face region, `facemesh.py` in the round's scratch) the levelled Xbox
face is pitched -0.6 degrees, the DualSense -2.4, and GjoypadZ's own face
buttons are +y to three places (measured from the ROM model); through the
page's matrices all three land within 2 degrees of each other, 45 degrees
from the view axis. The DualSense's STORED normals lean 9.7 degrees toward
the top edge while its winding is level - a lighting detail of that
package, not a shape.

**The button icons** (round 5): the watch page draws each labelled part
ALONE beside its action label through the renderer's part-only bridge
command (`src/sl_asset_override.h`, the `<part>` byte). The Xbox letters
read at the ~13 px icon size. The DualSense's cross / circle / square /
triangle were in `textures/dualsense_basecolor_1.png` as 2..3-texel grey
lines (RGB ~159) on the lilac button tops and did not read at that size,
nor on the pad's own ~12 px buttons: the renderer mipmaps every override
texture (a box pyramid, trilinear), so what was lost was contrast, not
sampling.

**The DualSense symbol edit** (round 6, 2026-09-20), two parts, both
needed (measured on the page with each alone):

1. *The material.* The tile's ALPHA channel is the author's clear-coat
   layering: each face button is built as a cap (texels at alpha ~51) over
   the symbol face (alpha ~150-160) over the opaque button (255), and 26%
   of the tile is below alpha 128. The glTF declared no `alphaMode`, which
   is OPAQUE, so the caps drew solid and the symbols (and the d-pad's
   arrows) were never visible - the darkened strokes compiled in and still
   drew as plain discs. `dualsense_mat1` now carries `alphaMode: MASK`,
   `alphaCutoff: 0.5` (the renderer's alpha test); BLEND was tried and
   shows depth-order artefacts (a black button). Nothing else on the page
   changes.
2. *The strokes.* Under MASK alone the symbols are the author's 2..3-texel
   grey (RGB 159) lines and read faintly at the pad's ~12 px and not at the
   icons' 13 px. The tile now carries a SCRIPTED edit -
   `tools/asset/darken_symbols.py dualsense` - which, inside each symbol's
   measured box, dilates the grey stroke by 3 texels onto the lilac and
   paints it RGB 56/56/64 (5080 texels; nothing outside the four boxes is
   touched, the size and format are unchanged).

The regions, the thresholds and the SHA-1 before (`826BA223...`, the
oracle as received) and after (`93951E60...`) are in
`dualsense/metadata.json` under `sightline_edits` with the material change,
both are in `ATTRIBUTION.md` and the image's `sl_third_party.changes`, and
`darken_symbols.py dualsense --check` (run by `test_gltf_import.py` [38],
which also asserts the MASK) proves the committed tile is the script's
output and that a re-run is a no-op. The `.bin` and the other two tiles
stay verbatim. The d-pad's four small arrow glyphs on the same tile are
left as they were - they show under MASK at their original contrast.

**The tilt** (round 6): the modern pad draws pitched 20 degrees from
face-on (`SL_WC_MODERN_TILT` in `src/native/sl_watch_controller.c`; the
page's own 45 stays for the N64 pad), the owner's call from round 5's
previews; `SL_PAD_TILT=<degrees>` overrides it. Under that matrix the
probe reads the Xbox face at ~20 degrees from the view axis and the
DualSense at ~30 - the DualSense's stored normals lean 9.7 degrees toward
the top edge (above), which the probe measures and the shape does not
have.

## Provenance record

`metadata.json` `source_oracle_sha1` holds the SHA-1 of every file as
received from the owner's staging (2026-09-19). The `.bin` and the textures
are committed verbatim; the `.gltf` had its `extras` rewritten (the false
`sl_authored` claim replaced by `sl_third_party`, `sl_n64` dropped, the
buffer renamed) and nothing else - its accessors and buffer views are the
staged file's.
