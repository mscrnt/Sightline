# Sightline brand

The project's identity artwork: the wordmark, the crosshair mark, and the
files the README and the Windows executable use. This directory holds the
working subset; the full kit (web favicons, app-icon PNG ladder, social
preview, hero and wallpaper renders, site palette) is kept outside the
repository and belongs to the landing site, not to the engine.

Nothing here is ROM-derived. Every file is generated from authored
geometry — glyph outlines plus a hand-built reticle — and the same authored
gold ramp the replacement boot logo uses
(`data/asset-overrides/source/boot/goldeneye_logo/textures/ramp_gold.png`).

## Files

| File | What it is | Use it for |
|---|---|---|
| `sightline-logo.svg` | Wordmark + crosshair, transparent, gold gradient | Vector master; anything on a light or neutral ground |
| `sightline-logo-on-dark.svg` | The same wordmark on the dark ground | Vector master; dark surfaces |
| `sightline-icon.svg` | Crosshair mark only, transparent | Vector master for the mark |
| `sightline-icon-tile.svg` | Crosshair on the dark rounded tile | App-icon look; source for the icon ladder |
| `readme-banner.png` | 1600×500, wordmark on the dark ground | The README header |
| `sightline-logo-800.png` | 800 px wide, transparent | Inline use in documents where SVG is not an option |
| `icon-mark-512-transparent.png` | 512×512 crosshair without the tile | Raster mark |
| `sightline.ico` | 16, 24, 32, 48, 64, 128, 256 — each size rendered separately; 16–32 use a heavier reticle | The Windows executable and window icon |

Prefer the SVG masters wherever a vector is accepted. The rasters exist for
the places that need them (GitHub's README renderer for the banner, the PE
resource table for the icon).

## Colours

- Gold ramp, top to bottom: `#fffde3` `#fffbb2` `#fff76c` `#ffd81c`
  `#ffa809` `#ff7407` `#ff3e0b`
- Crosshair red: `#d62e2a`
- Background dark: `#090a0e`; tile edge: `#2e303a`

## Usage notes

- The wordmark is one unit: the crosshair sits in place of the T and is not
  a separate element to be moved or recoloured.
- Keep the gradient. A flat-gold fill is not the mark.
- The transparent masters are drawn for dark or neutral grounds; the top of
  the ramp is close to white, so on a white page use `readme-banner.png` or
  `sightline-logo-on-dark.svg` rather than the transparent files.
- Windows executable: `sightline.ico` is embedded as icon resource 1 by
  `tools/windows/sightline.rc`, which `tools/windows/build.ps1` compiles
  with the toolchain's `windres` into `build/win32/sightline_res.o` and
  links into `sightline.exe`. Wired 2026-09-19; the Explorer icon and the
  SDL2 window-class icon (SDL2 takes the exe's first `RT_GROUP_ICON`) both
  come from it, so no `SDL_SetWindowIcon` call is needed.
- GitHub's social preview image is not a repository file; it is uploaded
  through the repository settings.

## Licensing

Every file under `docs/brand/` — the logo, the wordmark, the crosshair
mark, the icon, the banner and the tile — is the project owner's own
artwork and is included in the repository on that basis (owner statement,
2026-09-19). These files identify the Sightline project. They are not
covered by the Sightline code (0BSD) or artwork (CC0) grants — see
[LICENSES/README.md](../../LICENSES/README.md) — and are not offered for
reuse in other projects. The wordmark's glyph outlines derive from a
third-party typeface; the crosshair and the colour ramp are original.
