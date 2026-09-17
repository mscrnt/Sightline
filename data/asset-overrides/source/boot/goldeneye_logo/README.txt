SightLine logo - authored replacement for the GoldenEye wordmark
================================================================

WHAT THIS IS
    The word "SightLine" with the letter t replaced by a red crosshair.
    Replaces the GoldenEye logo prop drawn by
    constructor_menu04_goldeneyelogo (front.c:1964).

    This is the SOURCE package. The importer converts it to
    boot/goldeneye_logo.slmodel at build time; that binary is a build
    output and is not committed. Override slot boot.goldeneye_logo
    (SL_ASSET_BOOT_GOLDENEYE_LOGO) and its emit hook exist and are
    verified rendering - see front.c:2127.

PROVENANCE - AUTHORED, NOT EXTRACTED
    Letterforms   authored by the project owner, from the owner's OWN
                  custom font. The outlines are extracted from that font
                  with fontTools and flattened at a tolerance of ~0.45
                  model units (sub-pixel at 1440p). The font is the
                  owner's own work: it is not Nintendo's, not Rare's, not
                  a third party's, and nothing in it is ROM-derived.
    Crosshair     generated procedurally: ring, four ticks, centre dot.
    Colour ramp   authored from seven colour stops. NOT resampled from any
                  ROM texture. An earlier revision DID resample the original
                  env map; that was replaced precisely because it would have
                  been ROM-derived.

    No ROM vertex data, pixel data or display-list data is present in this
    package. What was taken from the original is METRICS - proportions, not
    assets:
        baseline    -119
        cap height   172
        envelope    x -1284 .. 1284

GEOMETRY
    meshes      2   (lettering, crosshair) - SEPARATE so they take separate
                    materials; the red must not be baked into one mesh
    triangles   5619
    bounds      x [-1270, 1274]  y [-141, 63]  z [0, 0]
    flat at z=0, matching the original lettering, which is also flat

    Scale is 1.0 as authored. One glTF unit is one N64 model unit; do not
    rescale at import.

    Original prop for reference:
        lettering        x [-1284, 1284]  y [-139, 106]  z 0
        flatshaded_slab  x [  345,  903]  y [-272, 272]  z [-67, 67]
    The slab is the RED DIAGONAL SLASH across the original logo. This package
    has no equivalent - the crosshair is doing that job instead. If a slash is
    wanted, it is a deliberate addition, not a restoration.

MATERIALS
    SightLine-gold          unlit, extras.sl_texgen TRUE
                            textures/ramp_gold.png, 64x64
    SightLine-crosshair-red unlit, extras.sl_texgen FALSE
                            untextured, baseColorFactor (214, 46, 42)

    ramp_gold.png is EMBEDDED in the built model rather than referenced,
    because it is not a game texture and has no identifier to reference.
    That is allowed only because the glTF image declares it, in
    extras.sl_authored / extras.sl_authored_provenance - see the AUTHORED
    TEXTURES note in tools/asset/gltf_import.py. Without that declaration
    --repo refuses the file.

THE GRADIENT - AND WHY THE NORMALS LOOK WRONG
    The gold is NOT painted. The geometry is flat (z=0) but the lettering
    normals are FAKED: ny ramps linearly with height, -0.290 at the baseline
    to +0.290 at the cap. Through G_TEXTURE_GEN that samples t 0.35..0.65 of
    the vertical ramp, so the letters run orange-red at the foot to pale gold
    at the top.

    This is the original's own mechanism, measured not guessed:
        original  corr(ny, height) = +0.948,  ny -0.287 .. +0.289,  t 0.36..0.64
        this      corr             = 1.000,   ny -0.290 .. +0.290,  t 0.35..0.65

    So: do not "correct" the normals to face +Z. That would flatten the logo
    to a single colour, because every vertex would sample one texel.

    To make the red stronger, widen the ny range. +-0.40 reaches t 0.30 and
    puts a distinctly orange-red foot on the letters.

TYPE
    "SightLine" is mixed case, but this font has no true lowercase - its
    lowercase glyphs are SMALL CAPS at 85% of cap height with no descenders,
    even on g. So the word reads as large S and L with smaller letters
    between. Tracking is 83 units/gap, wider than the all-caps version (55),
    because the small caps leave more air.

CHECKING IT
    .venv\Scripts\python.exe check_bounds.py sightline.gltf
    (needs trimesh + numpy; it is a reference figure, not a build step)
