/**
 * Sightline — native asset overrides.
 *
 * ONE seam, not a per-asset hack. A logical ASSET ID resolves to an optional
 * file on disk; when that file is present and valid the native renderer draws
 * it instead of the original, and when it is absent NOTHING changes: no new
 * required file, no extra load, no altered draw order, no timing difference.
 * The feature is opt-in and its off-state is the byte-for-byte original path.
 *
 * The two logo assets have genuinely different SOURCE representations - the
 * Nintendo logo is a 1172-compressed ROM prop reached through subdraw(), the
 * Rareware logo is source-resident display lists in assets/rarewarelogo.c - so
 * the abstraction is deliberately NOT "replace the original structure". It is
 *
 *      asset id  ->  optional native override  ->  otherwise the original
 *
 * and a future asset needs a new id plus one call-site hook, never a second
 * loader.
 *
 * WHAT IS NOT HERE, ON PURPOSE: no glTF parsing, no JSON, no PNG/JPEG decode.
 * Conversion is offline (tools/asset/gltf_import.py). The runtime reads one
 * compact, versioned, bounds-checked binary and nothing else.
 *
 * NATIVE ONLY. Nothing in this file is part of the cartridge build: src/native
 * is outside the Makefile's src/ and src/game/ globs, and the two game
 * call sites guard their hooks with #ifndef __sgi. Custom models must not
 * become a ROM feature.
 */
#ifndef SL_ASSET_OVERRIDE_H
#define SL_ASSET_OVERRIDE_H

/* ---- logical asset ids -------------------------------------------------
 *
 * The id is the API. Paths are derived from it (see sl_asset_override_path)
 * so no string path is scattered through front.c or title.c.
 */
#define SL_ASSET_BOOT_NINTENDO_LOGO  0
#define SL_ASSET_BOOT_RAREWARE_LOGO  1
#define SL_ASSET_BOOT_GOLDENEYE_LOGO 2
#define SL_ASSET_BOOT_LEGAL_PAGE     3
/* The watch's device-matched controller models (#63): drawn on the watch's
 * controller page in place of the N64 pad under the MODERN profile, one per
 * controller family. Each carries a PART TABLE (below) so the runtime moves
 * its sticks, buttons and triggers individually. */
#define SL_ASSET_CONTROLLER_XBOX      4
#define SL_ASSET_CONTROLLER_DUALSENSE 5
#define SL_ASSET_ID_COUNT            6

/* ---- canonical physical controller parts --------------------------------
 *
 * The runtime addresses a controller model's parts by these ids and by
 * nothing else - never by node order or by a brand's own button names. The
 * importer maps a package's labels onto them (btn_a and btn_cross are both
 * FACE_SOUTH), so the draw code asks for "the south face button" of whatever
 * pad it has. PHYSICAL CONTROLS ONLY: nothing here says what a part does -
 * "A = interact" is the binding registry's, never the model's. Kept in step
 * with PARTS in tools/asset/gltf_import.py by hand; the test suite asserts
 * the two agree. */
#define SL_PART_NONE           0xFFFFFFFFu  /* a primitive outside every part */
#define SL_PART_BODY            1u
#define SL_PART_LEFT_STICK      2u
#define SL_PART_RIGHT_STICK     3u
#define SL_PART_DPAD            4u
#define SL_PART_FACE_SOUTH      5u
#define SL_PART_FACE_EAST       6u
#define SL_PART_FACE_WEST       7u
#define SL_PART_FACE_NORTH      8u
#define SL_PART_LEFT_SHOULDER   9u
#define SL_PART_RIGHT_SHOULDER 10u
#define SL_PART_LEFT_TRIGGER   11u
#define SL_PART_RIGHT_TRIGGER  12u
#define SL_PART_MENU           13u
#define SL_PART_BACK           14u
#define SL_PART_GUIDE          15u
#define SL_PART_MUTE           16u
#define SL_PART_TOUCHPAD       17u
#define SL_PART_DPAD_UP        18u
#define SL_PART_DPAD_DOWN      19u
#define SL_PART_DPAD_LEFT      20u
#define SL_PART_DPAD_RIGHT     21u
#define SL_PART_ID_COUNT       22u
#define SL_AMDL_MAX_PARTS      32u
/* part flags */
#define SL_AMDL_P_AUTHORED    0x0001u  /* geometry authored for Sightline
                                        * inside a third-party model
                                        * (extras.sl_authored_part) */

/* ---- the bridge display-list command ------------------------------------
 *
 * The custom draw MUST happen inside the display list, at the same point the
 * original geometry would have been issued, so that it inherits the screen's
 * projection, modelview, viewport and render order exactly. Issuing GL from a
 * game constructor would bypass all of that.
 *
 * ucode05.txt names opcode 0x02 "rsp_reserved0": a real command byte that no
 * GoldenEye list ever contains, and one sl_gfx_dl.c already scores as zero
 * evidence when sniffing a list's word order (op_strong()). A 40-bit magic on
 * top of it means an accidental 0x02 word cannot be mistaken for a bridge
 * command.
 *
 *   w0 = 0x02 'S' 'L' <id>          0x02534Cxx
 *   w1 = 0xC5A5 <part> <fade 0..255>
 *
 * fade is the SCREEN's own fade level, not the asset's: the boot sequences
 * fade their logos in through a light/primitive colour, and that is
 * choreography rather than asset data. It is passed through so a custom model
 * fades on exactly the original schedule.
 *
 * <part> (bits 8..15, 2026-09-20, #63 / #64 - the watch page's button icons)
 * selects ONE PART of a parts model: 0 draws the whole model exactly as
 * before (every command emitted until now carries 0 there), and a value p
 * draws only the primitives of the part whose canonical id is p - 1, about
 * ITS OWN PIVOT under the modelview in force - the pivot translation the
 * whole-model draw applies is left out, so the caller's matrix places the
 * part's pivot wherever it likes, at whatever scale. The part's pose (its
 * press, tilt and tint) still applies, so a standalone icon of a button moves
 * with the button on the pad. sl_asset_override_emit_part writes it. */
#define SL_AOV_DL_OP        0x02u
#define SL_AOV_DL_W0_BASE   0x02534C00u
#define SL_AOV_DL_W0_MASK   0xFFFFFF00u
#define SL_AOV_DL_W1_BASE   0xC5A50000u
#define SL_AOV_DL_W1_MASK   0xFFFF0000u
#define SL_AOV_DL_W1_PART_SHIFT 8u
#define SL_AOV_DL_W1_PART_MASK  0x0000FF00u

/* ---- on-disk format ------------------------------------------------------
 *
 * "SLM1". Little-endian, fixed-width, offset-addressed, every count and every
 * offset validated before a single byte is dereferenced. See
 * tools/asset/gltf_import.py for the writer and the same limits restated in
 * its --help.
 */
#define SL_AMDL_MAGIC0      'S'
#define SL_AMDL_MAGIC1      'L'
#define SL_AMDL_MAGIC2      'M'
#define SL_AMDL_MAGIC3      '1'
#define SL_AMDL_VERSION     2u
#define SL_AMDL_HEADER_SIZE 128u

/* VERSION 2 changed ONE thing: a texture slot is now EITHER embedded pixels
 * OR a REFERENCE to a game texture by identifier, and the slot grew from 16
 * to 32 bytes to say which. The magic stays "SLM1" - it names the family, and
 * the version field is what a reader is required to check. A version-1 file
 * is rejected with a message telling the owner to re-import, exactly as a
 * version-2 file would be by an older build.
 *
 * WHY REFERENCES EXIST. A model whose textures came out of the game cannot be
 * distributed with those pixels baked in - project rule 2, no ROM-derived
 * assets in the repository, ever. Geometry and materials are the author's own
 * work and travel fine; the pixels do not. A reference slot carries the
 * IDENTIFIER of a game texture instead, and the runtime resolves it from data
 * the player already has. The shipped file therefore contains no game pixels
 * at all - not obfuscated, not compressed: absent.
 *
 * See sl_texref_resolve() below for the resolution side and
 * tools/asset/gltf_import.py for the importer's refusal to embed. */

/* Sane upper bounds. NOT N64-tiny, and the numbers are measured rather than
 * guessed: a survey of 27 real Blender/trimesh logo exports ran from 22k to
 * 461k vertices and from 132k to 2.76M indices, so a cap under those would
 * have made the feature useless for the models it exists to install. At the
 * ceiling a model costs about 32 MB of attributes and 12 MB of indices, which
 * is bounded and far from letting a corrupt count ask for a gigabyte. Every
 * one of these is enforced at load, before any allocation is sized by it.
 *
 * A model near the ceiling is a million triangles a frame. That is fine on a
 * boot screen - the screens' timers advance on g_ClockTimer, so a slower frame
 * lengthens no sequence - but it is not free, and a lighter model draws
 * faster. */
#define SL_AMDL_MAX_VERTS   1000000u
#define SL_AMDL_MAX_INDICES 3000000u
#define SL_AMDL_MAX_PRIMS       256u
#define SL_AMDL_MAX_MATS        256u
#define SL_AMDL_MAX_TEXS         16u
#define SL_AMDL_MAX_TEXDIM     2048u
#define SL_AMDL_MAX_TEXBYTES 67108864u   /* 64 MiB of decoded RGBA, all tex */
#define SL_AMDL_MAX_FILE    100663296u   /* 96 MiB on disk */
#define SL_AMDL_MAX_TEXNAME      63u     /* identifier length, excluding NUL */

/* texture slot kinds */
#define SL_AMDL_TEX_EMBEDDED 0u   /* w*h*4 bytes of RGBA live in the file    */
#define SL_AMDL_TEX_REF      1u   /* the file names a game texture instead   */

/* header flags */
#define SL_AMDL_F_NORMALS   0x0001u
#define SL_AMDL_F_UV        0x0002u
#define SL_AMDL_F_COLOR     0x0004u
/* F_PARTS (2026-09-19, #63): the file carries a PART TABLE at header +72
 * (offset) / +76 (count) - the first two words of what was zero padding - and
 * every primitive's fourth word is an index into it, or SL_PART_NONE. Each
 * table entry is 32 bytes: canonical id (SL_PART_*), pivot x y z (f32), flags
 * (SL_AMDL_P_*), three reserved words. A part's vertices are stored RELATIVE
 * to its pivot, so the draw path translates by the pivot, applies the part's
 * pose, and draws.
 *
 * NO VERSION BUMP, for the same reason the texgen bits took none: without the
 * flag the two header words and the prim word are the zeros they always were,
 * so a partless file is byte-identical to one written before parts existed,
 * and an older reader draws a parts model whole in its rest pose - a static
 * controller, never a corrupt one. The version field is for changes an old
 * reader could get WRONG. */
#define SL_AMDL_F_PARTS     0x0008u

/* material flags */
#define SL_AMDL_M_DOUBLESIDED 0x0001u
#define SL_AMDL_M_ALPHA_BLEND 0x0002u
#define SL_AMDL_M_ALPHA_MASK  0x0004u
/* KHR_materials_unlit, the one extension the importer accepts. A material
 * carrying it is drawn from its base colour alone even when the mesh supplies
 * normals - which is the whole point of the extension, and honouring it is the
 * price of accepting it. */
#define SL_AMDL_M_UNLIT       0x0008u
/* ---- GENERATED TEXTURE COORDINATES, per material ------------------------
 *
 * THE DEFECT THESE EXIST FOR. An override model's UVs are BAKED: they are
 * written once by the importer and never change, so a reflection sampled
 * through them is WELDED TO THE SURFACE and rotates with the model. The
 * original logos do not work that way. They set G_TEXTURE_GEN, and the RSP
 * manufactures a coordinate PER FRAME from the vertex normal under the matrix
 * then in force - so the highlight SWEEPS ACROSS the letterforms while the
 * model spins. Baked UVs cannot express that at any resolution, because the
 * thing that changes is the coordinate, not the picture.
 *
 * THE DEFAULT IS IMPLIED, NOT DECLARED, and it is implied by something that
 * already carries the answer. A material whose texture slot is a REFERENCE to
 * one of the game's own reflection maps is by construction using the map the
 * original material used, and the original material sets G_TEXTURE_GEN:
 *
 *   rareware.env_field  title.c:338 - gSPSetGeometryMode(..., G_TEXTURE_GEN)
 *   rareware.env_gold   the same draw, same geometry mode
 *   nintendo.logo_i8    MEASURED off the prop's own display list: the decline
 *                       recorded in sl_gfx_dl.c reads mode=00062205, and
 *                       0x00040000 of that IS G_TEXTURE_GEN
 *
 * So the runtime asks the texture-reference registry whether an identifier
 * names a generated-coordinate map (sl_texref_is_generated) and turns texgen
 * on for that material by itself. Nothing has to be declared, the two models
 * already committed need no re-import, and a model with genuinely authored
 * UVs over a hand-painted texture is untouched - it has no reference slot, so
 * the implication never fires for it.
 *
 * THE FLAGS ARE THE OVERRIDE OF THAT IMPLICATION, in both directions, for the
 * author who knows better. They come from the glTF material's
 * `extras.sl_texgen` (true / false). Setting BOTH is rejected at load: a file
 * that says "on" and "off" about the same material has no defensible
 * interpretation and guessing one would be a silent wrong answer.
 *
 * NO LAYOUT CHANGE AND THEREFORE NO VERSION BUMP. These are two previously
 * unused bits of the material flags word that has been a u32 since version 1;
 * every offset, every size and every count is unchanged. An older build reads
 * a file carrying them and ignores them, which lands on exactly the behaviour
 * it had before this existed - baked UVs - so the degradation is the old
 * behaviour rather than a corrupt one. The version field is reserved for
 * changes that an old reader could get WRONG, and this is not one. */
#define SL_AMDL_M_TEXGEN      0x0010u   /* force generated coordinates ON  */
#define SL_AMDL_M_TEXGEN_OFF  0x0020u   /* force them OFF                  */

struct sl_amdl_prim {
    unsigned int first;      /* index of the first element in the index array */
    unsigned int count;      /* number of index elements (multiple of 3) */
    unsigned int material;   /* < nmat */
    unsigned int part;       /* < npart, or SL_PART_NONE (always NONE when the
                              * file has no part table) */
};

struct sl_amdl_part {
    unsigned int id;         /* SL_PART_*, never NONE, unique in the model */
    float        pivot[3];   /* the part's origin in the model's frame */
    unsigned int flags;      /* SL_AMDL_P_* */
    /* MEASURED AT LOAD, not stored on disk: the bounds of the part's own
     * vertices RELATIVE TO ITS PIVOT (the frame its primitives are stored
     * in), so a caller drawing the part alone can size it - the watch
     * page's icons fit each part to one icon height (#63 / #64). A part
     * with no primitives reads all zeros. */
    float        lo[3], hi[3];
};

/* A part's POSE for the next draw: a translation and a rotation about the
 * part's pivot (radians, applied X then Y then Z about the model's own axes,
 * which for a controller in the GjoypadZ frame are +x right, +y toward the
 * watch camera, -z the top edge), and a tint multiplied into the part's
 * colour (1,1,1 = untouched). Set by the watch each frame before it emits the
 * bridge command; cleared to rest by sl_asset_override_pose_reset. */
struct sl_amdl_pose {
    float move[3];
    float rot[3];
    float tint[3];
};

struct sl_amdl_mat {
    float        base[4];    /* baseColorFactor, linear 0..1 */
    int          texture;    /* index into tex[], or -1 */
    unsigned int flags;      /* SL_AMDL_M_* */
    float        alpha_cutoff;
    /* RESOLVED AT LOAD, not stored on disk: does this material take
     * per-frame generated coordinates? The flags above and the texture
     * slot's identifier are the inputs; this is the single answer the
     * renderer reads, so the policy lives in one place and the draw path
     * cannot re-derive it differently. */
    int          texgen;
};

struct sl_amdl_tex {
    unsigned int         w, h;
    const unsigned char *rgba;   /* w*h*4. Into the loaded blob when the slot
                                  * is EMBEDDED; into `owned` when it is a
                                  * resolved REFERENCE. Never NULL once the
                                  * model has loaded - a slot that could not be
                                  * resolved fails the whole load, so the
                                  * renderer never sees a half-textured model. */
    unsigned int         gl;     /* GL name, filled in lazily by the renderer */
    unsigned int         kind;   /* SL_AMDL_TEX_EMBEDDED | SL_AMDL_TEX_REF */
    unsigned int         hash;   /* FNV-1a 32 the reference must decode to, or
                                  * 0 when the slot is embedded */
    unsigned char       *owned;  /* malloc'd RGBA for a resolved reference,
                                  * NULL otherwise. Freed with the model. */
    const char          *name;   /* identifier, into the blob, or NULL */
};

/* The loaded model, in memory. All pointers address the single blob read from
 * disk, except prim/mat/tex which are decoded into host structs. */
struct sl_amdl {
    unsigned int nvert, nidx, nprim, nmat, ntex;
    unsigned int flags;
    /* EXPOSURE (#63): a bounded lift on the whole draw, 1.0 (the default,
     * plain modulate) .. 4.0, applied by the renderer as GL_RGB_SCALE on the
     * texture stage. A near-black texture cannot be brightened by lighting
     * alone (modulate never exceeds the texel); this is the one knob, set
     * per draw by sl_asset_override_exposure_set, never stored on disk. */
    float        exposure;
    const float         *pos;    /* 3 * nvert                          */
    const float         *nrm;    /* 3 * nvert, or NULL                 */
    const float         *uv;     /* 2 * nvert, or NULL                 */
    const unsigned char *col;    /* 4 * nvert, or NULL                 */
    const unsigned int  *idx;    /* nidx                               */
    struct sl_amdl_prim *prim;
    struct sl_amdl_mat  *mat;
    struct sl_amdl_tex  *tex;
    unsigned int         npart;  /* 0 for a model without a part table */
    struct sl_amdl_part *part;   /* npart entries, or NULL             */
    struct sl_amdl_pose *pose;   /* npart entries, rest pose until set */
    unsigned char       *blob;
    unsigned long        blob_len;
};

/* ---- API ----------------------------------------------------------------
 *
 * Deliberately free of ultra64 types so src/gfx (which compiles without
 * -Iinclude) can include this header. The one function that appends to a
 * display list takes and returns the Gfx* as void*.
 */

/* Is an override installed and valid for this id? Loads on first call per id
 * and caches the answer - including the negative one, so a missing file costs
 * one stat() per boot and nothing thereafter. */
int sl_asset_override_available(int id);

/* The loaded model, or NULL. Never call without checking available() first. */
const struct sl_amdl *sl_asset_override_get_model(int id);

/* Append the bridge command. gdl is a Gfx*, and a Gfx* one command further on
 * is returned. fade is 0..255, the screen's own fade level. */
void *sl_asset_override_emit(void *gdl, int id, int fade);

/* Append the bridge command for ONE PART (SL_PART_*) of a parts model, drawn
 * about its own pivot under the modelview the caller has loaded - see the
 * <part> field above. A part the model does not have draws nothing; a model
 * without a part table draws nothing. */
void *sl_asset_override_emit_part(void *gdl, int id, unsigned int part_id, int fade);

/* ---- parts ----------------------------------------------------------------
 *
 * Does the loaded model for this id carry a part with this canonical id?
 * Returns its index, or -1 (also for an id that is not loaded, or a model
 * without a part table). */
int sl_asset_override_part_index(int id, unsigned int part_id);

/* Set one part's pose for the next draw. A part the model does not have is
 * ignored (returns 0), so the caller can pose "the right stick" without
 * knowing which pad it is looking at. */
int sl_asset_override_pose_set(int id, unsigned int part_id,
                               const struct sl_amdl_pose *pose);

/* Every part of this id back to its rest pose (no move, no rotation, no
 * tint). Called before a frame's poses are set. */
void sl_asset_override_pose_reset(int id);

/* The draw's exposure (see struct sl_amdl), clamped to 1.0 .. 4.0. */
void sl_asset_override_exposure_set(int id, float exposure);

/* The resolved override directory, for diagnostics. Never NULL. */
const char *sl_asset_override_dir(void);

/* The file this id resolves to: the first directory on the ladder that
 * actually HAS it, or - when none does - the highest-priority writable one, so
 * the importer and the diagnostics agree on where a new file would go.
 * Returns 0 on truncation. */
int sl_asset_override_path(int id, char *out, unsigned int cap);

/* ---- game-texture references -------------------------------------------
 *
 * A committed model names the game textures it uses instead of carrying them.
 * The identifiers are STABLE, MEANINGFUL and resolved through one small table
 * (src/native/sl_texref.c) - never raw addresses, array indices or filenames,
 * because those are exactly the things that change under a refactor and would
 * silently re-point a model at the wrong pixels.
 *
 *   rareware.env_field   the silver environment map, D_02004FE8
 *   rareware.env_gold    the gold environment map,  D_02005FF0
 *   nintendo.logo_i8     the I8 texture inside the PnintendologoZ ROM prop
 *
 * Both resolve through ONE call. What differs is only where the bytes come
 * from, and that is the table's business, not the caller's.
 *
 * Writes w*h*4 RGBA into a malloc'd buffer the CALLER owns and must free.
 * Returns 1 on success, 0 on any failure - unknown identifier, source not
 * resident, bad dimensions, decode error. Failure is ordinary: it means the
 * player has no ROM, or a different one, and the caller's job is then to fall
 * back to the original asset. */
int sl_texref_resolve(const char *name, unsigned int *w, unsigned int *h,
                      unsigned char **rgba);

/* Why a resolve failed, for the one warning line. Never NULL. */
const char *sl_texref_why(void);

/* Does this identifier name a texture the ORIGINAL material sampled through
 * GENERATED coordinates (G_TEXTURE_GEN) rather than through stored ones?
 *
 * The property belongs to the identifier, not to the model: it is a fact about
 * what that texture IS in the game - a reflection map consumed by the RSP's
 * texture generator - and the registry is where facts about identifiers live.
 * Putting it here rather than in the model format is what lets the already
 * committed models get the right behaviour without being re-imported, and what
 * stops a future reference to some ordinary game texture from silently
 * inheriting a reflection sweep it never had.
 *
 * Returns 1 for a generated-coordinate map, 0 for anything else INCLUDING an
 * unknown identifier - an unknown name has no claim on the answer. */
int sl_texref_is_generated(const char *name);

/* FNV-1a, 32-bit, over the decoded RGBA. The importer computes this over the
 * pixels it REFUSED to embed and stores it in the reference slot; the loader
 * recomputes it over what it resolved and rejects a mismatch. That turns "the
 * player has a different ROM" from a wrong-looking logo into a clean fallback
 * to the original, and it is what proves - at runtime, on the owner's machine
 * - that a reference resolved to exactly the texture the author saw.
 *
 * Four bytes, one-way, and no more able to reconstruct a texture than a
 * checksum is. Defined here so the two sides cannot drift. */
unsigned int sl_amdl_fnv1a(const unsigned char *p, unsigned long n);

#endif /* SL_ASSET_OVERRIDE_H */
