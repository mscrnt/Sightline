/**
 * Sightline — resolving a GAME TEXTURE by identifier, at runtime.
 *
 * WHY THIS FILE EXISTS. A custom model that reuses one of the game's own
 * textures cannot be distributed with those pixels baked in: project rule 2
 * forbids ROM-derived assets in the repository, and that rule is what the
 * whole distribution model rests on. The author's GEOMETRY and MATERIALS are
 * their own work and travel fine. The pixels do not.
 *
 * So a shipped model carries an IDENTIFIER where a texture would have been,
 * and this file turns that identifier back into pixels using data the player
 * already has. The committed file contains no game texels at all - not
 * encoded, not compressed, not obfuscated: absent. There is nothing to strip.
 *
 * THE IDENTIFIERS ARE THE API. They are stable and meaningful, never raw
 * addresses or array indices, because those are precisely the things that move
 * under a refactor - and a moved index does not fail, it silently resolves to
 * the wrong picture. A name that no longer exists fails loudly and falls back.
 *
 * TWO SOURCES, ONE MECHANISM. The caller does not know or care which:
 *
 *   SEGMENT   The Rareware environment maps. title.c binds them with
 *             gDPLoadTextureBlock(&D_02004FE8, ...) and &D_02005FF0. MEASURED:
 *             in the native build those two symbols are not compiled data at
 *             all - build/win32/segments.elf.s:504 sets them to the ABSOLUTE
 *             values 0x02004fe8 and 0x02005ff0, which are SEGMENT addresses.
 *             The bytes arrive at runtime through
 *             setupRarewareLogoData() -> romCopy(virtualaddress, ...), and
 *             title.c:435 then binds segment 2 (SPSEGMENT_GETITLE) to
 *             `virtualaddress`. So the pixels live at
 *             virtualaddress + (0x02004FE8 & 0xFFFFFF), which is exactly how
 *             the display-list walker resolves the same address - this is not
 *             a parallel scheme, it is the same arithmetic written out.
 *
 *             They are RAW ROM BYTES, therefore big-endian, which is what
 *             sl_tex_decode already expects (it assembles a 16-bit texel as
 *             row[n] << 8 | row[n+1], sl_gfx_tex.c:360). Nothing is swapped
 *             here and nothing should be.
 *
 *   PROP      The Nintendo logo's I8 texture, inside the 1172-compressed ROM
 *             prop PnintendologoZ. init_menu01_nintendo() (front.c:1659)
 *             inflates that prop with load_object_fill_header(), which fills
 *             in header->Textures (objecthandler_2.c:105) and leaves each
 *             record's TextureID as a SEGMENT-relative value against base
 *             0x05000000. sl_model_head_swap() has already byte-swapped it
 *             into host order (sl_swap_model.c:133), and sub_GAME_7F075A90
 *             promotes node offsets but NOT TextureID - so the pointer
 *             arithmetic below is the promotion this one field never got.
 *
 * NO SECOND DECOMPRESSOR AND NO SECOND DECODER. The prop is inflated by the
 * game's own loader on the path it always took; the texels are decoded by
 * sl_tex_decode, the same function the display-list walker uses. This file
 * adds arithmetic and validation, not machinery.
 *
 * NATIVE ONLY. src/native is outside the cartridge Makefile's globs, and the
 * whole file compiles away on IDO in any case.
 */
#ifndef __sgi

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ultra64.h>

#include "bondconstants.h"
#include "bondtypes.h"
#include "game/chrobjdata.h"
#include "game/title.h"

#include "../gfx/sl_gfx_tex.h"
#include "../sl_asset_override.h"

/* ------------------------------------------------------------ the table -- */

#define TEXREF_SEG  1   /* an address in a display-list segment              */
#define TEXREF_PROP 2   /* a texture record inside a loaded ModelFileHeader  */

/* Alpha policy. An I-format texel is replicated by the RDP to R, G, B AND A,
 * so a strict decode of an I8 image is translucent wherever it is dark. That
 * is right for the RDP and WRONG here: the importer's reference stands in for
 * the PNG it refused to embed, and an 8-bit greyscale PNG decodes opaque. The
 * contract a reference has to meet is "the same pixels the importer would have
 * written", so the slot says which it wants and the hash check below proves it
 * got there. */
#define TEXREF_ALPHA_KEEP   0   /* whatever the format decodes to */
#define TEXREF_ALPHA_OPAQUE 1   /* force A = 255 */

/* Coordinate policy. Does the ORIGINAL material sample this texture through
 * coordinates the RSP GENERATES per frame from the vertex normal
 * (G_TEXTURE_GEN), or through the coordinates stored in the vertex?
 *
 * This is a fact about the TEXTURE'S ROLE IN THE GAME, which is why it lives
 * in this table and not in the model format. It is what lets an override
 * model that names one of these identifiers inherit the original's sweeping
 * reflection without declaring anything, and it is what stops a future
 * identifier for some ordinary wall texture from inheriting one it never had.
 *
 * Every value below is sourced or measured - see the per-entry notes. */
#define TEXREF_COORD_STORED    0
#define TEXREF_COORD_GENERATED 1

struct texref {
    const char  *name;
    int          src;
    unsigned int seg_addr;   /* TEXREF_SEG: the full segment address        */
    int          prop;       /* TEXREF_PROP: PROP_* index                   */
    unsigned int prop_tex;   /* TEXREF_PROP: index into header->Textures    */
    int          fmt;        /* SL_TEX_*                                    */
    unsigned int w, h;
    int          alpha;
    int          coord;      /* TEXREF_COORD_*                              */
};

/*
 * ADDING AN ENTRY is deliberate work in two places: here, and in the
 * importer's registry (tools/asset/gltf_import.py, GAME_TEXTURES). The
 * importer's copy is what refuses to embed the pixels; this one is what brings
 * them back. A name present in only one of the two is caught by the test
 * suite (t_texref_tables_agree).
 */
static const struct texref g_texref[] = {
    /* The two Rareware environment maps. 32x32 RGBA16, and title.c's own
     * gDPLoadTextureBlock calls at title.c:384 and :388 are the authority for
     * both the format and the dimensions - the same four numbers appear there
     * as G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32. */
    /* GENERATED coordinates, and this one is SOURCED rather than inferred:
     * the single draw that binds both maps sets the bit in this tree's own
     * source. title.c:338 -
     *   gSPSetGeometryMode(gdl++, (G_SHADE | G_CULL_BACK | G_LIGHTING
     *                              | G_TEXTURE_GEN | G_SHADING_SMOOTH));
     * and the two gDPLoadTextureBlock calls for D_02004FE8 and D_02005FF0
     * stand below it inside that same geometry mode, which is never cleared
     * between them. Their vertices therefore carry s=t=0 and expect the RSP
     * to manufacture the coordinate - the reason the original highlight
     * sweeps across the letters while the logo spins instead of turning
     * with them. */
    { "rareware.env_field", TEXREF_SEG,  0x02004FE8u, 0, 0u,
      SL_TEX_RGBA16, 32u, 32u, TEXREF_ALPHA_KEEP, TEXREF_COORD_GENERATED },
    { "rareware.env_gold",  TEXREF_SEG,  0x02005FF0u, 0, 0u,
      SL_TEX_RGBA16, 32u, 32u, TEXREF_ALPHA_KEEP, TEXREF_COORD_GENERATED },

    /* The Nintendo logo's only texture. The prop's ModelFileHeader declares
     * numtextures = 1 (assets/obseg/prop/nintendologo/ModelFileHeader.inc.c),
     * and the record's own Width/Height are read at resolve time rather than
     * hardcoded - the 32x32 here is the expectation the record is CHECKED
     * against, not a substitute for reading it. */
    /* GENERATED, and this one is MEASURED rather than sourced, because the
     * geometry mode is not in this tree at all: the Nintendo logo's display
     * list is inside the 1172-compressed PnintendologoZ prop, so no
     * gSPSetGeometryMode for it can be read in source. The renderer measured
     * it instead, and the number is recorded verbatim in sl_gfx_dl.c's B-085
     * note - the walker's own decline line for this screen reads
     *
     *   sl_texgen: DECLINED - geom has TEXTURE_GEN (mode=00062205) ...
     *
     * and 0x00062205 & 0x00040000 is G_TEXTURE_GEN, set. front.c:1688 issues
     * the lights for that draw and no gSPLookAt, which is precisely why the
     * decline was observable in the first place. */
    { "nintendo.logo_i8",   TEXREF_PROP, 0u, PROP_NINTENDOLOGO, 0u,
      SL_TEX_I8, 32u, 32u, TEXREF_ALPHA_OPAQUE, TEXREF_COORD_GENERATED }
};

#define TEXREF_COUNT ((int) (sizeof g_texref / sizeof g_texref[0]))

/* ------------------------------------------------------------- failures -- */

/* One reason string, read by the single warning line the loader prints. Set on
 * every failure path so a rejection always says WHICH thing was not there;
 * "the override did not load" with no reason is the failure mode this seam
 * exists to avoid. */
static const char *g_why = "no attempt made";

const char *sl_texref_why(void) { return g_why; }

/* The coordinate policy query. Deliberately independent of whether the
 * texture can be RESOLVED: it answers a question about the identifier, so it
 * works before any ROM data is resident and cannot fail for a reason that has
 * nothing to do with what it was asked. An unknown name answers 0 - it has no
 * claim on the behaviour of a texture this build has never heard of. */
int sl_texref_is_generated(const char *name)
{
    int i;
    if (name == NULL) return 0;
    for (i = 0; i < TEXREF_COUNT; i++)
        if (strcmp(g_texref[i].name, name) == 0)
            return g_texref[i].coord == TEXREF_COORD_GENERATED;
    return 0;
}

static int fail(const char *why)
{
    g_why = why;
    return 0;
}

/* ---------------------------------------------------------------- hash --- */

unsigned int sl_amdl_fnv1a(const unsigned char *p, unsigned long n)
{
    unsigned int h = 2166136261u;
    unsigned long i;

    if (p == NULL) return 0u;
    for (i = 0u; i < n; i++) {
        h ^= (unsigned int) p[i];
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------- sources --- */

/*
 * A segment address, resolved the way the walker resolves it: base for the
 * segment plus the low 24 bits. Only segment 2 (SPSEGMENT_GETITLE, and only
 * while the title screens own it) is reachable from here, and that is
 * deliberate - a general segment table lookup would let an identifier name
 * whatever happened to be bound at the moment, which is not a stable identity.
 */
static const unsigned char *seg_bytes(unsigned int addr, unsigned int need)
{
    unsigned int seg = (addr >> 24) & 0xFu;
    unsigned char *base;

    if (seg != (unsigned int) SPSEGMENT_GETITLE) {
        fail("the identifier names a segment this build cannot resolve");
        return NULL;
    }
    /* virtualaddress is set by setupRarewareLogoData() before the Rareware
     * screen constructs its first display list, and it is what title.c:435
     * binds segment 2 to. Zero means that has not happened yet - which is a
     * LOAD-ORDER failure, not a corrupt file, and the caller retries. */
    base = (unsigned char *) (uintptr_t) virtualaddress;
    if (base == NULL) {
        fail("the Rareware logo segment is not loaded yet");
        return NULL;
    }
    if (need == 0u) { fail("zero-length texture"); return NULL; }
    return base + (addr & 0x00FFFFFFu);
}

/*
 * A texture record inside a prop the game has already inflated into RAM.
 *
 * Reached ONLY through the header the game itself filled in - this file never
 * opens the ROM, never inflates anything and never learns the prop's layout
 * beyond the one field the loader leaves un-promoted.
 */
static const unsigned char *prop_bytes(const struct texref *r,
                                       unsigned int *w, unsigned int *h,
                                       unsigned int need_stride_rows)
{
    struct ModelFileHeader *hd;
    const unsigned char    *filedata;
    unsigned int            id, off, rw, rh;

    (void) need_stride_rows;

    if (r->prop < 0) { fail("bad prop index"); return NULL; }
    hd = PitemZ_entries[r->prop].header;
    if (hd == NULL) { fail("the prop has no model header"); return NULL; }

    /* numtextures and Textures are both filled by load_object_fill_header;
     * before that call Textures is whatever the static initialiser left, so
     * "not loaded yet" is the ordinary early state, not corruption. */
    if (hd->Textures == NULL) {
        fail("the Nintendo logo prop is not loaded yet");
        return NULL;
    }
    if (hd->numtextures <= 0
        || r->prop_tex >= (unsigned int) hd->numtextures) {
        fail("the prop has no such texture record");
        return NULL;
    }

    /*
     * The base of the inflated file. load_object_fill_header sets
     *     Switches = filedata
     *     Textures = &((s32 *) filedata)[numSwitches]
     * so the base is Textures walked back over the switch index array. Derived
     * from Textures rather than from Switches because Switches[] has since had
     * its entries promoted to pointers in place, while the ARRAY ADDRESS - the
     * thing wanted here - is untouched in both.
     */
    if (hd->numSwitches < 0) { fail("negative switch count"); return NULL; }
    filedata = (const unsigned char *) hd->Textures
             - (unsigned long) hd->numSwitches * 4ul;

    /* TextureID is a segment-relative pointer against base 0x05000000, already
     * byte-swapped into host order by sl_model_head_swap(). sub_GAME_7F075A90
     * promotes node offsets and switch entries but not this field, so the
     * promotion happens here. */
    id = (unsigned int) hd->Textures[r->prop_tex].TextureID;
    if ((id >> 24) != 0x05u) {
        fail("the prop texture record does not hold a segment-5 address");
        return NULL;
    }
    off = id & 0x00FFFFFFu;

    /* Width and Height are u8 in the record. Read them, then require them to
     * be what the identifier promised: an identifier names ONE texture, and a
     * record that has changed shape is a different texture wearing the same
     * name. Falling back beats rendering the wrong thing at the wrong size. */
    rw = (unsigned int) hd->Textures[r->prop_tex].Width;
    rh = (unsigned int) hd->Textures[r->prop_tex].Height;
    if (rw != r->w || rh != r->h) {
        fail("the prop texture is not the size this identifier names");
        return NULL;
    }
    *w = rw;
    *h = rh;
    return filedata + off;
}

/* ------------------------------------------------------------- resolve --- */

int sl_texref_resolve(const char *name, unsigned int *w_out,
                      unsigned int *h_out, unsigned char **rgba_out)
{
    const struct texref *r = NULL;
    const unsigned char *src;
    unsigned char       *dst;
    unsigned int         w, h, need, px, bytes;
    int                  i, rc;

    g_why = "no attempt made";
    if (name == NULL || w_out == NULL || h_out == NULL || rgba_out == NULL)
        return fail("bad arguments");
    *rgba_out = NULL;
    *w_out = *h_out = 0u;

    for (i = 0; i < TEXREF_COUNT; i++) {
        if (strcmp(g_texref[i].name, name) == 0) { r = &g_texref[i]; break; }
    }
    if (r == NULL)
        return fail("no game texture has that identifier in this build");

    w = r->w;
    h = r->h;

    /* Every multiplication that sizes an allocation is bounded first. The
     * dimensions come from a table here, but prop_bytes() reads them off data
     * the ROM supplied, and that data is not trusted. */
    if (w == 0u || h == 0u || w > (unsigned int) SL_TEX_MAX_DIM
        || h > (unsigned int) SL_TEX_MAX_DIM)
        return fail("texture dimensions out of range");

    need = sl_tex_data_size(r->fmt, (int) w, (int) h);
    if (need == 0u) return fail("unsupported texture format");

    switch (r->src) {
    case TEXREF_SEG:  src = seg_bytes(r->seg_addr, need);       break;
    case TEXREF_PROP: src = prop_bytes(r, &w, &h, need);        break;
    default:          return fail("unknown resolution source");
    }
    if (src == NULL) return 0;             /* g_why already says why */

    /* prop_bytes may have re-read the dimensions; re-derive the byte count
     * from what will actually be decoded rather than from what was expected. */
    need = sl_tex_data_size(r->fmt, (int) w, (int) h);
    if (need == 0u) return fail("unsupported texture format");

    px = w * h;                            /* both <= 1024, cannot overflow */
    bytes = px * 4u;

    dst = (unsigned char *) malloc((size_t) bytes);
    if (dst == NULL) return fail("out of memory");

    /* Flags 0: neither source has been through texLoad, so neither carries
     * GoldenEye's odd-row word swap. The Rareware maps are raw ROM bytes bound
     * straight by gDPLoadTextureBlock, and the prop texture is raw in the
     * inflated file - tools/export/logo_models.py reads it the same way and
     * produced a correct picture. */
    rc = sl_tex_decode(src, need, r->fmt, (int) w, (int) h, 0u,
                       NULL, 0u, dst, bytes);
    if (rc != SL_TEX_OK) {
        free(dst);
        g_why = sl_tex_strerror(rc);
        return 0;
    }

    if (r->alpha == TEXREF_ALPHA_OPAQUE) {
        unsigned int k;
        for (k = 0u; k < px; k++) dst[k * 4u + 3u] = 0xFFu;
    }

    *w_out = w;
    *h_out = h;
    *rgba_out = dst;
    g_why = "ok";
    return 1;
}

#endif /* !__sgi */
