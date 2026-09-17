# data/asset-overrides — license

The original artwork and replacement models in this directory that are
identified by their source glTF metadata (`extras.sl_authored` together with a
non-empty `sl_authored_provenance`) as authored for Sightline are released
under **CC0 1.0 Universal**. The full legal code is at
[../../LICENSES/Sightline-Assets-CC0-1.0.txt](../../LICENSES/Sightline-Assets-CC0-1.0.txt).

The grant applies **only** to Sightline-authored content in this directory. It
does **not** apply to:

- GoldenEye textures that a model references. Those are stored as
  identifiers, never as pixels, and are resolved at load time from the
  player's own game data.
- ROM-derived data of any kind.
- Upstream game content, or any other third-party material.

Nothing here changes the no-ROM-derived-pixels rule or the `sl_authored`
provenance model; both are described in [./README.md](./README.md) and
[../../docs/asset-overrides.md](../../docs/asset-overrides.md), and the import
tooling continues to enforce them.

The small scripts in this directory (for example `check_bounds.py`) are code,
not artwork, and fall under the code grant in
[../../LICENSES/Sightline-Code-0BSD.txt](../../LICENSES/Sightline-Code-0BSD.txt).
