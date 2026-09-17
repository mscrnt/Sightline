# data/asset-overrides

Committed **native asset overrides**: custom models that ship with the
repository, so a fresh clone shows them with no import step.

Not to be confused with `data/overrides/`, which holds small hand-tuned
corrections to *deriver* output (lights, normals, spawns). Different feature,
different file kind, different size expectations. This directory holds binary
`.slmodel` files measured in megabytes; that one holds text files measured in
lines.

See `docs/asset-overrides.md` for the whole picture.

## What may live here

Geometry and materials, which are the model author's own work.

**No ROM-derived pixels. Ever** — project rules, rule 2 (`docs/project-rules.md`). A texture that came out of
the game is stored as a *reference to an identifier*, and the runtime resolves
it from the player's own game data at load.

This is enforced, not remembered:

- `gltf_import.py --repo` refuses to write a texture that is not a reference.
  For game textures the rule is categorical — *no embedded pixels at all* —
  rather than "no texture we recognise", because a recognition-based rule is
  only as good as its registry.
- `tools/asset/test_gltf_import.py` walks every `.slmodel` under this directory
  and asserts the same thing.

The one exception is pixels the author made themselves, and only when the
source glTF **declares** them:

```json
"extras": { "sl_authored": true, "sl_authored_provenance": "how they were made" }
```

That declaration is never inferred, the provenance string is required, and it
is read only after game-texture detection has already turned any recognised
texture into a reference — so it widens what may be embedded without
narrowing what is recognised.

## source/ — what is actually committed

`source/<group>/<name>/` holds a model's **source package**: the glTF, its
buffers, its PNG textures and its provenance notes. The directory names the
asset id — `source/boot/goldeneye_logo` is `boot.goldeneye_logo`.

`tools/asset/build_repo_assets.py` converts every package through ordinary
`--repo` mode, and `build.ps1` runs it, so a player who builds gets the model
with no import step. The `.slmodel` it writes is a **build output**: it lands
in `build\win32\data\asset-overrides\`, the exe-adjacent directory the
runtime already searches, and it is not tracked.

Two `.slmodel` files predate this and are still tracked binaries here
(`boot/nintendo_logo.slmodel`, `boot/rareware_logo.slmodel`). New models
should ship as source.

## Precedence

```
your install directory   ->   data/asset-overrides   ->   the original asset
```

A model you import yourself is found first and replaces the shipped one.
`SL_ASSET_OVERRIDES=0` turns the whole feature off and restores the originals.

## Adding one

```
.\tools\windows\asset-import.ps1 -Repo -Asset boot.rareware_logo -Input model.gltf
```
