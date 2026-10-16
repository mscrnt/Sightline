/**
 * sl_gfx_worldasset.c - the world-asset provider (#68). See the header for
 * the contract, the identity model, the root ladder and the SLWA layout.
 *
 * Three things live here and nothing else does:
 *
 *   1. THE PARSER. Pure, filesystem-free, and the only code that believes a
 *      byte of the file. It is written so that every count is bounded before
 *      it is multiplied and every section is proved to lie inside a file whose
 *      length was measured, not declared - so no arithmetic can wrap and no
 *      read can leave the buffer.
 *   2. THE POLICY. Is a set selected, where is its root, and which (class,
 *      path) have already been answered for.
 *   3. THE CACHE. One allocation per asset, resolved at most once per run.
 *
 * It draws nothing and knows nothing about GL: the renderer owns the draw, and
 * that is what keeps the file access in ONE place instead of scattered through
 * sl_gfx_dl.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <emmintrin.h>                 /* fill_run_scan: SSE2, asked per function */

#include "sl_gfx_worldasset.h"
#include "../platform/sl_presentation.h"   /* #68: which set the presentation selects */

/* The player-data ladder's directory (src/native/sl_asset_override.c). Same
 * declaration the texture provider makes, for the same reason: this file must
 * not pull in a game header. Under the self-test there is no such module, so
 * the test supplies its own. */
const char *sl_asset_override_dir(void);

/* ---- little-endian readers ----------------------------------------------
 *
 * Explicit, so the format is host-endian-independent and so that nothing here
 * depends on alignment. Every caller has already proved the four or two bytes
 * are inside the buffer. */
static unsigned rd_u32(const unsigned char *p)
{
    return (unsigned) p[0] | ((unsigned) p[1] << 8)
         | ((unsigned) p[2] << 16) | ((unsigned) p[3] << 24);
}

static float rd_f32(const unsigned char *p)
{
    union { unsigned u; float f; } c;
    c.u = rd_u32(p);
    return c.f;
}

const char *sl_wa_strerror(int rc)
{
    switch (rc) {
    case SL_WA_OK:         return "ok";
    case SL_WA_E_MAGIC:    return "not an SLWA world asset";
    case SL_WA_E_VERSION:  return "unsupported SLWA version";
    case SL_WA_E_CLASS:    return "unknown asset class";
    case SL_WA_E_PATH:     return "bad or mismatched item path";
    case SL_WA_E_FLAGS:    return "reserved field is not zero";
    case SL_WA_E_COUNT:    return "a count is zero or past its limit";
    case SL_WA_E_LAYOUT:   return "a section lies outside the file or overlaps";
    case SL_WA_E_SIZE:     return "declared size is not the file's length";
    case SL_WA_E_INDEX:    return "an index is out of range";
    case SL_WA_E_MESH:     return "a mesh range or material reference is bad";
    case SL_WA_E_MATERIAL: return "a material is bad";
    case SL_WA_E_TEXTURE:  return "a texture is bad";
    case SL_WA_E_IO:       return "read failed";
    case SL_WA_E_MEM:      return "out of memory";
    case SL_WA_E_TRUNC:    return "truncated";
    case SL_WA_E_ANIM:     return "an animation record or a vertex's animation fields are bad";
    case SL_WA_E_MATX:     return "a material-extension record is bad";
    case SL_WA_E_SKIN:     return "a skin entry or joint record is bad";
    default:               return "unknown";
    }
}

static short rd_s16(const unsigned char *p)
{
    unsigned v = (unsigned) p[0] | ((unsigned) p[1] << 8);
    return (short) (v >= 0x8000u ? (int) v - 0x10000 : (int) v);
}

static int finite_f(float f)
{
    return f == f && f - f == 0.0f;       /* neither NaN nor an infinity */
}

/* ---- the parser ---------------------------------------------------------- */

/* A section the header declared: where it starts and how long it is. Kept as
 * unsigned long so the sum below cannot wrap a 32-bit unsigned even when both
 * halves are at their limit. */
struct sec { unsigned long off, len; };

/* Does [off, off+len) lie wholly inside [SL_WA_HEADER, file_size), at a
 * 4-byte-aligned start? A zero-length section is allowed to sit anywhere
 * in range (a bundle with no textures declares none). */
static int sec_ok(struct sec s, unsigned long file_size)
{
    if (s.len == 0) return 1;
    if ((s.off & 3u) != 0) return 0;
    if (s.off < SL_WA_HEADER) return 0;
    if (s.off > file_size) return 0;
    return s.len <= file_size - s.off;
}

static int sec_overlap(struct sec a, struct sec b)
{
    if (a.len == 0 || b.len == 0) return 0;
    return a.off < b.off + b.len && b.off < a.off + a.len;
}

int sl_wa_parse_header(const unsigned char *h, unsigned long len,
                       struct sl_wa_asset *out)
{
    struct sec s[8];
    unsigned i, j, n, ns, version;
    unsigned long declared;

    if (h == NULL || out == NULL) return SL_WA_E_IO;
    if (len < SL_WA_HEADER) return SL_WA_E_TRUNC;

    if (h[0] != SL_WA_MAGIC0 || h[1] != SL_WA_MAGIC1 ||
        h[2] != SL_WA_MAGIC2 || h[3] != SL_WA_MAGIC3) return SL_WA_E_MAGIC;
    version = rd_u32(h + 4);
    if (version < SL_WA_VERSION_MIN || version > SL_WA_VERSION) return SL_WA_E_VERSION;

    memset(out, 0, sizeof *out);
    out->asset_class = rd_u32(h + 8);
    /* A skinned item (#70) is a version-4 file and only it is: a character
     * cannot arrive in a layout that has no skin, and a sky or a background
     * cannot arrive in one that means something else at +116. */
    if (version == 4u) {
        if (out->asset_class != SL_WA_CLASS_CHAR &&
            out->asset_class != SL_WA_CLASS_HEAD &&
            out->asset_class != SL_WA_CLASS_GUN &&
            out->asset_class != SL_WA_CLASS_PROP) return SL_WA_E_CLASS;
    } else if (out->asset_class != SL_WA_CLASS_SKYDOME &&
               out->asset_class != SL_WA_CLASS_BACKGROUND) return SL_WA_E_CLASS;

    /* The path must be NUL-terminated inside its field and made only of the
     * characters an item path is allowed to use, so it can never be a
     * directory traversal, an absolute path or anything but a leaf name. */
    {
        int term = 0;
        for (i = 0; i < SL_WA_PATH_MAX; i++) {
            char c = (char) h[12 + i];
            if (c == '\0') { term = 1; break; }
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-')) return SL_WA_E_PATH;
        }
        if (!term || i == 0) return SL_WA_E_PATH;
        for (j = i; j < SL_WA_PATH_MAX; j++)
            if (h[12 + j] != 0) return SL_WA_E_PATH;
        memcpy(out->path, h + 12, i);
        out->path[i] = '\0';
    }

    /* The word at +44 is reserved before version 3 and the material-extension
     * table from it on - the same rule as the animation words below: a
     * reader that knows only the older layout refuses a file that carries a
     * material program rather than drawing it as the base alone. */
    if ((version < 3u || version == 4u) && rd_u32(h + 44) != 0) return SL_WA_E_FLAGS;
    for (i = 0; i < 3; i++) {
        out->bounds_min[i] = rd_f32(h + 48 + 4 * i);
        out->bounds_max[i] = rd_f32(h + 60 + 4 * i);
    }
    /* The three words at +116 are reserved in a version-1 file and are the
     * animation sections in a version-2 one - which is the whole of the
     * difference between the versions, and why a version-1 file cannot
     * carry them: a reader that knows only version 1 refuses a file that
     * moves rather than drawing it still. */
    if (version == 1u) {
        for (i = 0; i < 3; i++)
            if (rd_u32(h + 116 + 4 * i) != 0) return SL_WA_E_FLAGS;
    }
    out->anim_count = (version == 2u || version == 3u) ? rd_u32(h + 124) : 0u;
    out->aux_stride = (version == 3u) ? SL_WA_AUX3_SIZE : SL_WA_AUX_SIZE;
    out->vframe = (version == 3u) ? 1 : 0;
    out->joint_count = (version == 4u) ? rd_u32(h + 124) : 0u;

    out->vertex_count   = rd_u32(h + 72);
    out->index_count    = rd_u32(h + 76);
    out->material_count = rd_u32(h + 80);
    out->texture_count  = rd_u32(h + 84);
    out->mesh_count     = rd_u32(h + 88);

    /* Bound every count BEFORE any of them is multiplied. After these five
     * tests each product below is at most 24 * 1000000 = 24,000,000, so the
     * unsigned long arithmetic that follows cannot wrap on any host this
     * builds for - and that is still true with the wider material, texture and
     * mesh limits a whole level's background needs, because the two that get
     * multiplied by a record size larger than 16 are unchanged. */
    if (out->vertex_count   == 0 || out->vertex_count   > SL_WA_MAX_VERTICES)  return SL_WA_E_COUNT;
    if (out->index_count    == 0 || out->index_count    > SL_WA_MAX_INDICES)   return SL_WA_E_COUNT;
    if (out->material_count == 0 || out->material_count > SL_WA_MAX_MATERIALS) return SL_WA_E_COUNT;
    if (out->texture_count  > SL_WA_MAX_TEXTURES)                              return SL_WA_E_COUNT;
    if (out->mesh_count     == 0 || out->mesh_count     > SL_WA_MAX_MESHES)    return SL_WA_E_COUNT;
    if (out->index_count % 3u != 0) return SL_WA_E_COUNT;
    if (out->anim_count > SL_WA_MAX_ANIMS) return SL_WA_E_COUNT;
    if (version == 4u && (out->joint_count == 0u || out->joint_count > SL_WA_MAX_JOINTS))
        return SL_WA_E_COUNT;

    declared = (unsigned long) rd_u32(h + 112);
    if (declared > (unsigned long) SL_WA_MAX_FILE) return SL_WA_E_COUNT;
    /* The ONE place the file's real length is compared with what it says
     * about itself. Everything after this point trusts `declared`, and this
     * is why it may. */
    if (declared != len) return SL_WA_E_SIZE;

    s[0].off = rd_u32(h + 92);  s[0].len = (unsigned long) out->vertex_count   * SL_WA_VERTEX_SIZE;
    s[1].off = rd_u32(h + 96);  s[1].len = (unsigned long) out->index_count    * SL_WA_INDEX_SIZE;
    s[2].off = rd_u32(h + 100); s[2].len = (unsigned long) out->material_count * SL_WA_MATERIAL_SIZE;
    s[3].off = rd_u32(h + 104); s[3].len = (unsigned long) out->texture_count  * SL_WA_TEXTURE_SIZE;
    s[4].off = rd_u32(h + 108); s[4].len = (unsigned long) out->mesh_count     * SL_WA_MESH_SIZE;
    ns = 5;
    /* THE ANIMATION SECTIONS come together or not at all: a record table with
     * no per-vertex fields moves nothing, and fields naming a table that is
     * not there name nothing. Bounded like the five above - the aux section
     * is at most 12 * SL_WA_MAX_VERTICES and the table 128 * 64. */
    if (version == 2u) {
        unsigned long xo = rd_u32(h + 116), no = rd_u32(h + 120);
        if ((out->anim_count == 0u) != (xo == 0ul && no == 0ul)) return SL_WA_E_LAYOUT;
        if (out->anim_count != 0u) {
            if (xo == 0ul || no == 0ul) return SL_WA_E_LAYOUT;
            s[5].off = xo; s[5].len = (unsigned long) out->vertex_count * SL_WA_AUX_SIZE;
            s[6].off = no; s[6].len = (unsigned long) out->anim_count   * SL_WA_ANIM_SIZE;
            ns = 7;
        }
    }
    /* VERSION 3: the auxiliary section (24 bytes a vertex, the frame in its
     * second half) and the material-extension table (one record per
     * material) are always present; the animation table comes with its
     * count or not at all. Bounded the same way: 24 * SL_WA_MAX_VERTICES
     * and 96 * SL_WA_MAX_MATERIALS. */
    if (version == 3u) {
        unsigned long xo = rd_u32(h + 116), no = rd_u32(h + 120), mo = rd_u32(h + 44);
        if (xo == 0ul || mo == 0ul) return SL_WA_E_LAYOUT;
        if ((out->anim_count == 0u) != (no == 0ul)) return SL_WA_E_LAYOUT;
        s[5].off = xo; s[5].len = (unsigned long) out->vertex_count   * SL_WA_AUX3_SIZE;
        s[6].off = no; s[6].len = (unsigned long) out->anim_count     * SL_WA_ANIM_SIZE;
        s[7].off = mo; s[7].len = (unsigned long) out->material_count * SL_WA_MATX_SIZE;
        ns = 8;
    }
    /* VERSION 4 (#70): the skin section and the joint table, both always
     * present. Bounded like the rest: 8 * SL_WA_MAX_VERTICES and
     * 48 * SL_WA_MAX_JOINTS. */
    if (version == 4u) {
        unsigned long ko = rd_u32(h + 116), jo = rd_u32(h + 120);
        if (ko == 0ul || jo == 0ul) return SL_WA_E_LAYOUT;
        s[5].off = ko; s[5].len = (unsigned long) out->vertex_count * SL_WA_SKIN_SIZE;
        s[6].off = jo; s[6].len = (unsigned long) out->joint_count  * SL_WA_JOINT_SIZE;
        ns = 7;
    }

    for (n = 0; n < ns; n++)
        if (!sec_ok(s[n], declared)) return SL_WA_E_LAYOUT;
    for (n = 0; n < ns; n++)
        for (j = n + 1; j < ns; j++)
            if (sec_overlap(s[n], s[j])) return SL_WA_E_LAYOUT;

    return SL_WA_OK;
}

int sl_wa_parse(const unsigned char *buf, unsigned long len,
                struct sl_wa_asset *out)
{
    unsigned long voff, ioff, moff, toff, hoff, xoff = 0, noff = 0, mxoff = 0;
    unsigned long koff = 0, joff = 0;
    unsigned long payload_lo;
    unsigned i, refl;
    int rc;

    rc = sl_wa_parse_header(buf, len, out);
    if (rc != SL_WA_OK) return rc;

    voff = rd_u32(buf + 92);
    ioff = rd_u32(buf + 96);
    moff = rd_u32(buf + 100);
    toff = rd_u32(buf + 104);
    hoff = rd_u32(buf + 108);
    if (out->anim_count != 0u) {         /* the header proved both present */
        xoff = rd_u32(buf + 116);
        noff = rd_u32(buf + 120);
    }
    if (out->vframe) {                   /* version 3: the header proved both */
        xoff  = rd_u32(buf + 116);
        mxoff = rd_u32(buf + 44);
    }
    if (out->joint_count != 0u) {        /* version 4: the header proved both */
        koff = rd_u32(buf + 116);
        joff = rd_u32(buf + 120);
    }

    /* Texture payloads may not tread on any table, so the lowest table start
     * is the floor they must sit at or above. */
    payload_lo = SL_WA_HEADER;
    if (voff + (unsigned long) out->vertex_count * SL_WA_VERTEX_SIZE > payload_lo)
        payload_lo = voff + (unsigned long) out->vertex_count * SL_WA_VERTEX_SIZE;
    if (ioff + (unsigned long) out->index_count * SL_WA_INDEX_SIZE > payload_lo)
        payload_lo = ioff + (unsigned long) out->index_count * SL_WA_INDEX_SIZE;
    if (moff + (unsigned long) out->material_count * SL_WA_MATERIAL_SIZE > payload_lo)
        payload_lo = moff + (unsigned long) out->material_count * SL_WA_MATERIAL_SIZE;
    if (toff + (unsigned long) out->texture_count * SL_WA_TEXTURE_SIZE > payload_lo)
        payload_lo = toff + (unsigned long) out->texture_count * SL_WA_TEXTURE_SIZE;
    if (hoff + (unsigned long) out->mesh_count * SL_WA_MESH_SIZE > payload_lo)
        payload_lo = hoff + (unsigned long) out->mesh_count * SL_WA_MESH_SIZE;
    if (xoff != 0ul) {
        if (xoff + (unsigned long) out->vertex_count * out->aux_stride > payload_lo)
            payload_lo = xoff + (unsigned long) out->vertex_count * out->aux_stride;
    }
    if (out->anim_count != 0u) {
        if (noff + (unsigned long) out->anim_count * SL_WA_ANIM_SIZE > payload_lo)
            payload_lo = noff + (unsigned long) out->anim_count * SL_WA_ANIM_SIZE;
    }
    if (mxoff != 0ul) {
        if (mxoff + (unsigned long) out->material_count * SL_WA_MATX_SIZE > payload_lo)
            payload_lo = mxoff + (unsigned long) out->material_count * SL_WA_MATX_SIZE;
    }
    if (koff != 0ul) {
        if (koff + (unsigned long) out->vertex_count * SL_WA_SKIN_SIZE > payload_lo)
            payload_lo = koff + (unsigned long) out->vertex_count * SL_WA_SKIN_SIZE;
        if (joff + (unsigned long) out->joint_count * SL_WA_JOINT_SIZE > payload_lo)
            payload_lo = joff + (unsigned long) out->joint_count * SL_WA_JOINT_SIZE;
    }

    /* THE ANIMATION RECORDS (#69), before anything references them. Every
     * number is checked to be one the evaluation can use: a finite table, a
     * positive coordinate scale, a non-zero turn, a mode this renderer
     * evaluates, lanes within the register width - so a record that parses
     * is a record that displaces by a finite amount. */
    for (i = 0; i < out->anim_count; i++) {
        const unsigned char *p = buf + noff + (unsigned long) i * SL_WA_ANIM_SIZE;
        struct sl_wa_anim *an = &out->anims[i];
        unsigned k;
        an->mode        = rd_u32(p);
        an->coord_scale = rd_f32(p + 4);
        an->wave_count  = rd_u32(p + 8);
        if (an->mode != SL_WA_ANIM_SINEWAVES) return SL_WA_E_ANIM;
        if (rd_u32(p + 12) != 0u) return SL_WA_E_ANIM;
        if (!finite_f(an->coord_scale) || !(an->coord_scale > 0.0f)) return SL_WA_E_ANIM;
        if (an->wave_count == 0u || an->wave_count > SL_WA_ANIM_WAVES) return SL_WA_E_ANIM;
        for (k = 0; k < SL_WA_ANIM_WAVES; k++) {
            an->amplitude[k] = rd_f32(p + 16 + 4 * k);
            an->phase[k]     = rd_f32(p + 32 + 4 * k);
            an->speed[k]     = rd_f32(p + 48 + 4 * k);
            an->freq_a[k]    = rd_f32(p + 64 + 4 * k);
            an->freq_b[k]    = rd_f32(p + 80 + 4 * k);
            if (!finite_f(an->amplitude[k]) || !finite_f(an->phase[k])
                || !finite_f(an->speed[k]) || !finite_f(an->freq_a[k])
                || !finite_f(an->freq_b[k])) return SL_WA_E_ANIM;
        }
        an->turn_scale = rd_f32(p + 96);
        an->turn_bias  = rd_f32(p + 100);
        an->turn       = rd_f32(p + 104);
        if (rd_u32(p + 108) != 0u) return SL_WA_E_ANIM;
        if (!finite_f(an->turn_scale) || !finite_f(an->turn_bias)
            || !finite_f(an->turn) || an->turn == 0.0f) return SL_WA_E_ANIM;
        for (k = 0; k < 4; k++) {
            an->poly[k] = rd_f32(p + 112 + 4 * k);
            if (!finite_f(an->poly[k])) return SL_WA_E_ANIM;
        }
    }

    /* Materials first: meshes reference them, textures are referenced by
     * them, so validating in this order means no forward trust. */
    for (i = 0; i < out->material_count; i++) {
        const unsigned char *p = buf + moff + (unsigned long) i * SL_WA_MATERIAL_SIZE;
        unsigned tex   = rd_u32(p);
        unsigned blend = rd_u32(p + 4);
        unsigned flags = rd_u32(p + 8);
        /* The flags word used to be required to be zero. It now carries the
         * decal-pass bit (SL_WA_MAT_DECAL), and a bit this version does not
         * define is still refused - so an older bundle, which writes zero
         * here, parses unchanged, and a newer one cannot smuggle in state
         * this renderer would silently ignore. */
        if ((flags & ~SL_WA_MAT_KNOWN) != 0u) return SL_WA_E_MATERIAL;
        /* A GENERATED material (D-023) is honoured by the skydome pass
         * alone; on another class nothing would ever leave it out. */
        if ((flags & SL_WA_MAT_GENERATED) != 0u
            && out->asset_class != SL_WA_CLASS_SKYDOME) return SL_WA_E_MATERIAL;
        /* Blue coverage is a property of a BLENDED draw's alpha; on an
         * opaque or cutout material it would mean nothing the renderer does. */
        if ((flags & SL_WA_MAT_COVERAGE_BLUE) != 0u && blend != SL_WA_BLEND_ALPHA)
            return SL_WA_E_MATERIAL;
        /* The panorama shell (D-026) is a background's outer ring; only the
         * background pass consults the bit. */
        if ((flags & SL_WA_MAT_PANORAMA) != 0u
            && out->asset_class != SL_WA_CLASS_BACKGROUND) return SL_WA_E_MATERIAL;
        /* The face cull (#96) is consulted by the background pass alone. */
        if ((flags & SL_WA_MAT_CULL_BACK) != 0u
            && out->asset_class != SL_WA_CLASS_BACKGROUND) return SL_WA_E_MATERIAL;
        /* ... and so is the game's moving water (D-048). */
        if ((flags & SL_WA_MAT_GAME_WATER) != 0u
            && out->asset_class != SL_WA_CLASS_BACKGROUND) return SL_WA_E_MATERIAL;
        /* The cloud field (D-023) is a generated skydome draw and nothing else. */
        if ((flags & SL_WA_MAT_CLOUD_PLANE) != 0u
            && (out->asset_class != SL_WA_CLASS_SKYDOME
                || (flags & SL_WA_MAT_GENERATED) == 0u)) return SL_WA_E_MATERIAL;
        /* The last word is reserved (0) on every class but a view model and a
         * prop, where it may name the material's REFLECTION image: 1 + its
         * index (a prop's: Egyptian's Golden Gun pickup, whose gold is its
         * reflection over near-white images). */
        refl = rd_u32(p + 12);
        if (refl != 0u && (out->joint_count == 0u
                           || (out->asset_class != SL_WA_CLASS_GUN
                               && out->asset_class != SL_WA_CLASS_PROP)
                           || refl - 1u >= out->texture_count))
            return SL_WA_E_MATERIAL;
        if (blend != SL_WA_BLEND_OPAQUE && blend != SL_WA_BLEND_ALPHA
            && blend != SL_WA_BLEND_CUTOUT)
            return SL_WA_E_MATERIAL;
        if (tex >= out->texture_count) return SL_WA_E_MATERIAL;
        out->materials[i].texture = tex;
        out->materials[i].blend   = blend;
        out->materials[i].flags   = flags;
        out->materials[i].reflect = refl;
    }

    for (i = 0; i < out->texture_count; i++) {
        const unsigned char *p = buf + toff + (unsigned long) i * SL_WA_TEXTURE_SIZE;
        unsigned w = rd_u32(p), h = rd_u32(p + 4);
        unsigned long doff = rd_u32(p + 8);
        unsigned long dlen = rd_u32(p + 12);
        unsigned long want;
        if (w == 0 || h == 0 || w > SL_WA_MAX_TEX_DIM || h > SL_WA_MAX_TEX_DIM)
            return SL_WA_E_TEXTURE;
        /* w and h are each <= 2048 here, so the product is <= 2^22 and the
         * scale by four is <= 2^24: bounded before it is computed. */
        want = (unsigned long) w * (unsigned long) h * 4ul;
        if (dlen != want) return SL_WA_E_TEXTURE;
        if ((doff & 3u) != 0) return SL_WA_E_TEXTURE;
        if (doff < payload_lo || doff > len || dlen > len - doff)
            return SL_WA_E_TEXTURE;
        out->textures[i].w = w;
        out->textures[i].h = h;
        out->textures[i].rgba = buf + doff;
    }

    for (i = 0; i < out->mesh_count; i++) {
        const unsigned char *p = buf + hoff + (unsigned long) i * SL_WA_MESH_SIZE;
        unsigned mat   = rd_u32(p);
        unsigned first = rd_u32(p + 4);
        unsigned cnt   = rd_u32(p + 8);
        unsigned mfl   = rd_u32(p + 12);
        /* Zero below version 4; a skinned item may mark its built-in head or
         * (a body, on every mesh) that it carries its own head, and nothing
         * else - except a view model, whose draws may name the game switches
         * they stand under (VIEW MODELS) and nothing else. */
        if (mfl != 0u && out->asset_class == SL_WA_CLASS_GUN) {
            if ((mfl & ~(SL_WA_MESH_SWITCH_BITS | SL_WA_MESH_ALT)) != 0u
                || SL_WA_MESH_SWITCH_A(mfl) > SL_WA_MAX_SWITCH
                || SL_WA_MESH_SWITCH_B(mfl) > SL_WA_MAX_SWITCH) return SL_WA_E_MESH;
        } else if (mfl != 0u && (out->joint_count == 0u
                          || (mfl & ~(SL_WA_MESH_BUILTIN_HEAD
                                      | (out->asset_class == SL_WA_CLASS_CHAR
                                         ? SL_WA_MESH_OWN_HEAD : 0u))) != 0u
                          || mfl == (SL_WA_MESH_BUILTIN_HEAD | SL_WA_MESH_OWN_HEAD)))
            return SL_WA_E_MESH;
        /* an item carries its own head on every mesh or on none */
        if (i > 0u && ((mfl ^ out->meshes[0].flags) & SL_WA_MESH_OWN_HEAD) != 0u
            && out->asset_class == SL_WA_CLASS_CHAR) return SL_WA_E_MESH;
        if (mat >= out->material_count) return SL_WA_E_MESH;
        if (cnt == 0 || cnt % 3u != 0) return SL_WA_E_MESH;
        /* first and cnt are both <= SL_WA_MAX_INDICES here only after this
         * pair of tests, which is why the sum is taken in unsigned long. */
        if ((unsigned long) first + (unsigned long) cnt > (unsigned long) out->index_count)
            return SL_WA_E_MESH;
        out->meshes[i].material    = mat;
        out->meshes[i].first_index = first;
        out->meshes[i].index_count = cnt;
        out->meshes[i].flags       = mfl;
    }

    /* THE JOINTS AND THE SKIN (#70), version 4. A joint's name is
     * terminated inside its field and made of the characters a source joint
     * name uses; its parent is an earlier joint, so the table is its own
     * topological order and walking it forwards visits every parent first;
     * its bind is finite. Every skin entry names joints inside the table and
     * its weights sum to exactly 255 - so every vertex is fully bound and a
     * draw can never take a vertex nowhere. */
    for (i = 0; i < out->joint_count; i++) {
        const unsigned char *p = buf + joff + (unsigned long) i * SL_WA_JOINT_SIZE;
        struct sl_wa_joint *jt = &out->joints[i];
        unsigned par, k, term = 0;
        for (k = 0; k < SL_WA_JOINT_NAME; k++) {
            char c = (char) p[k];
            if (c == '\0') { term = 1; break; }
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
                return SL_WA_E_SKIN;
        }
        if (!term || k == 0) return SL_WA_E_SKIN;
        for (; k < SL_WA_JOINT_NAME; k++) if (p[k] != 0) return SL_WA_E_SKIN;
        memcpy(jt->name, p, SL_WA_JOINT_NAME);
        par = rd_u32(p + 24);
        if (par == SL_WA_JOINT_ROOT) jt->parent = -1;
        else if (par < i) jt->parent = (int) par;
        else return SL_WA_E_SKIN;
        for (k = 0; k < 3; k++) {
            jt->bind[k] = rd_f32(p + 28 + 4 * k);
            if (!finite_f(jt->bind[k])) return SL_WA_E_SKIN;
        }
        /* reserved, except the game part a view model's joint names */
        jt->part = rd_u32(p + 40);
        if (rd_u32(p + 44) != 0u) return SL_WA_E_SKIN;
        /* 1 + a Switches[] index, or SL_WA_PART_MATRIX + 1 + a matrix (THE
         * WATCH HAND) - class GUN; a PROP's joint may name a matrix (a
         * vehicle's wheel, THE MOVING PROP) and nothing else */
        if (jt->part != 0u && out->asset_class == SL_WA_CLASS_PROP) {
            if (jt->part <= SL_WA_PART_MATRIX
                || jt->part > SL_WA_PART_MATRIX + SL_WA_MAX_PART_MATRIX) return SL_WA_E_SKIN;
        } else if (jt->part != 0u && (out->asset_class != SL_WA_CLASS_GUN
                               || (jt->part > SL_WA_MAX_SWITCH
                                   && (jt->part <= SL_WA_PART_MATRIX
                                       || jt->part > SL_WA_PART_MATRIX + SL_WA_MAX_PART_MATRIX))))
            return SL_WA_E_SKIN;
    }
    for (i = 0; koff != 0ul && i < out->vertex_count; i++) {
        const unsigned char *p = buf + koff + (unsigned long) i * SL_WA_SKIN_SIZE;
        unsigned k, sum = 0;
        for (k = 0; k < SL_WA_SKIN_LANES; k++) {
            if (p[k] >= out->joint_count) return SL_WA_E_SKIN;
            sum += p[SL_WA_SKIN_LANES + k];
        }
        if (sum != 255u) return SL_WA_E_SKIN;
    }
    out->skin = (koff != 0ul)
              ? (const struct sl_wa_skin *) (const void *) (buf + koff) : NULL;

    /* THE MATERIAL-EXTENSION RECORDS (#69 material), one per material, after
     * the textures they name. A NONE record must be all zeros, so a stray
     * value cannot become a program later; a NORMAL_SPHERE record names
     * three images inside the table and carries finite numbers with a
     * normal scale that is not zero (a zero scale would make every fragment
     * face the bias alone), and a flag bit this version does not define is
     * refused - the DECAL rule. */
    out->matx_count = 0;
    for (i = 0; mxoff != 0ul && i < out->material_count; i++) {
        const unsigned char *p = buf + mxoff + (unsigned long) i * SL_WA_MATX_SIZE;
        struct sl_wa_matx *m = &out->matx[i];
        unsigned k;
        m->mode = rd_u32(p);
        if (m->mode == SL_WA_MATX_NONE) {
            for (k = 4; k < SL_WA_MATX_SIZE; k += 4)
                if (rd_u32(p + k) != 0u) return SL_WA_E_MATX;
            continue;
        }
        if (m->mode != SL_WA_MATX_NORMAL_SPHERE) return SL_WA_E_MATX;
        m->normal_tex = rd_u32(p + 4);
        m->sphere_tex = rd_u32(p + 8);
        m->offset_tex = rd_u32(p + 12);
        m->flags      = rd_u32(p + 16);
        if (m->normal_tex >= out->texture_count || m->sphere_tex >= out->texture_count
            || m->offset_tex >= out->texture_count) return SL_WA_E_MATX;
        if ((m->flags & ~SL_WA_MATX_F_KNOWN) != 0u) return SL_WA_E_MATX;
        for (k = 0; k < 3; k++) {
            m->base_weight[k]    = rd_f32(p + 20 + 4 * k);
            m->reflect_weight[k] = rd_f32(p + 32 + 4 * k);
            if (!finite_f(m->base_weight[k]) || !finite_f(m->reflect_weight[k]))
                return SL_WA_E_MATX;
        }
        m->warp_scale   = rd_f32(p + 44);
        m->normal_scale = rd_f32(p + 48);
        m->normal_bias  = rd_f32(p + 52);
        for (k = 0; k < 2; k++) {
            m->sphere_scale[k] = rd_f32(p + 56 + 4 * k);
            m->sphere_bias[k]  = rd_f32(p + 64 + 4 * k);
            if (!finite_f(m->sphere_scale[k]) || !finite_f(m->sphere_bias[k]))
                return SL_WA_E_MATX;
        }
        m->mean_weight = rd_f32(p + 72);
        m->alpha_floor = rd_f32(p + 76);
        if (!finite_f(m->warp_scale) || !finite_f(m->normal_scale)
            || !finite_f(m->normal_bias) || !finite_f(m->mean_weight)
            || !finite_f(m->alpha_floor) || m->normal_scale == 0.0f)
            return SL_WA_E_MATX;
        for (k = 80; k < SL_WA_MATX_SIZE; k += 4)
            if (rd_u32(p + k) != 0u) return SL_WA_E_MATX;
        out->matx_count++;
    }

    /* THE PER-VERTEX FIELDS reference the table, so they are checked after
     * it. Only the reference is a question of validity here: a raw pair is
     * any 16-bit value and a direction any three, and a zero direction is a
     * vertex that moves nowhere rather than a fault. */
    for (i = 0; xoff != 0ul && i < out->vertex_count; i++) {
        const unsigned char *p = buf + xoff + (unsigned long) i * out->aux_stride;
        unsigned ai = (unsigned) p[10] | ((unsigned) p[11] << 8);
        if (ai > out->anim_count) return SL_WA_E_ANIM;
    }

    out->vertices = (const struct sl_wa_vertex *) (const void *) (buf + voff);
    out->indices  = (const unsigned *) (const void *) (buf + ioff);
    out->vaux     = (xoff != 0ul)
                  ? (const struct sl_wa_vaux *) (const void *) (buf + xoff) : NULL;
    return SL_WA_OK;
}

/* Vertices and indices are read through the same explicit little-endian
 * readers the header uses, into host-order arrays the renderer can hand
 * straight to GL. Done once, at load, so the draw path does no decoding. */
static int sl_wa_decode_arrays(unsigned char *buf, struct sl_wa_asset *a)
{
    unsigned long voff = rd_u32(buf + 92);
    unsigned long ioff = rd_u32(buf + 96);
    struct sl_wa_vertex *v = (struct sl_wa_vertex *) (void *) (buf + voff);
    unsigned *ix = (unsigned *) (void *) (buf + ioff);
    unsigned i;

    /* In-place, field by field, so the decode never needs a second buffer.
     * The record is 24 bytes and struct sl_wa_vertex is 24 bytes on every
     * host this builds for; the assert below is the guard that says so. */
    for (i = 0; i < a->vertex_count; i++) {
        const unsigned char *p = buf + voff + (unsigned long) i * SL_WA_VERTEX_SIZE;
        float f[5];
        unsigned char c[4];
        int k;
        for (k = 0; k < 5; k++) f[k] = rd_f32(p + 4 * k);
        for (k = 0; k < 4; k++) c[k] = p[20 + k];
        v[i].x = f[0]; v[i].y = f[1]; v[i].z = f[2];
        v[i].u = f[3]; v[i].v = f[4];
        v[i].r = c[0]; v[i].g = c[1]; v[i].b = c[2]; v[i].a = c[3];
    }
    for (i = 0; i < a->index_count; i++) {
        unsigned val = rd_u32(buf + ioff + (unsigned long) i * SL_WA_INDEX_SIZE);
        if (val >= a->vertex_count) return SL_WA_E_INDEX;
        ix[i] = val;
    }
    a->vertices = v;
    a->indices  = ix;
    /* The animation fields, the same way: 12 bytes in the file, 12 in
     * struct sl_wa_vaux on every host this builds for (six 16-bit fields,
     * no padding), decoded in place so the draw path reads a struct. A
     * version-3 entry is 24 bytes - the same twelve, then the six 16-bit
     * fields of struct sl_wa_vframe - at the asset's stride. */
    if (a->vaux != NULL) {
        unsigned long xoff = rd_u32(buf + 116);
        for (i = 0; i < a->vertex_count; i++) {
            unsigned char *p = buf + xoff + (unsigned long) i * a->aux_stride;
            struct sl_wa_vaux *x = (struct sl_wa_vaux *) (void *) p;
            short f[5];
            int k;
            for (k = 0; k < 5; k++) f[k] = rd_s16(p + 2 * k);
            x->wave_a = f[0]; x->wave_b = f[1];
            x->nx = f[2]; x->ny = f[3]; x->nz = f[4];
            x->anim = (unsigned short) ((unsigned) p[10] | ((unsigned) p[11] << 8));
            if (a->vframe) {
                struct sl_wa_vframe *fr = (struct sl_wa_vframe *) (void *) (p + SL_WA_AUX_SIZE);
                short g[6];
                for (k = 0; k < 6; k++) g[k] = rd_s16(p + SL_WA_AUX_SIZE + 2 * k);
                fr->xx = g[0]; fr->xy = g[1]; fr->xz = g[2];
                fr->yx = g[3]; fr->yy = g[4]; fr->yz = g[5];
            }
        }
        a->vaux = (const struct sl_wa_vaux *) (const void *) (buf + xoff);
    }
    return SL_WA_OK;
}

/* ---- #69 material animation: the evaluation ------------------------------ */

double sl_wa_anim_seconds(unsigned long frames)
{
    return (double) frames / (double) SL_WA_CLOCK_HZ;
}

float sl_wa_anim_displacement(const struct sl_wa_anim *a,
                              float wave_a, float wave_b, double t)
{
    double sum = 0.0;
    unsigned i;

    if (a == NULL || a->mode != SL_WA_ANIM_SINEWAVES) return 0.0f;
    for (i = 0; i < a->wave_count && i < SL_WA_ANIM_WAVES; i++) {
        double theta, angle, a2, s;
        if (a->amplitude[i] == 0.0f) continue;
        /* The phase runs in turns, wrapped to [0, 1) before the polynomial
         * sees it - the program's frc. The time product is taken in double
         * so a long session does not lose the fraction. */
        theta = -t * (double) a->speed[i] + (double) a->phase[i]
              + (double) wave_a * (double) a->coord_scale * (double) a->freq_a[i]
              + (double) wave_b * (double) a->coord_scale * (double) a->freq_b[i];
        theta -= floor(theta);
        angle = (theta * (double) a->turn_scale - (double) a->turn_bias) * (double) a->turn;
        a2 = angle * angle;
        s = angle * ((double) a->poly[0] + a2 * ((double) a->poly[1]
                  + a2 * ((double) a->poly[2] + a2 * (double) a->poly[3])));
        sum += (double) a->amplitude[i] * s;
    }
    return (float) sum;
}

/* ---- #69 material extension: the reference shade ------------------------- */

void sl_wa_matx_shade(const struct sl_wa_matx *m, unsigned terms,
                      const float *frame, const float *colour, float u, float v,
                      sl_wa_tex_fn tex, void *ctx, float *rgba)
{
    float nt[4], off[4], base[4], refl[4];
    float N[3], su, sv, bu = u, bv = v, mean;
    double len;
    int k;

    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0.0f;
    if (m == NULL || m->mode != SL_WA_MATX_NORMAL_SPHERE || tex == NULL) return;

    /* The bump-environment warp: the offset image's first two channels,
     * unsigned 0..1, scaled by the record and added to the base coordinate. */
    if (terms & SL_WA_MATX_T_WARP) {
        tex(ctx, 2, u, v, off);
        bu = u + m->warp_scale * off[0];
        bv = v + m->warp_scale * off[1];
    }
    tex(ctx, 3, bu, bv, base);

    /* The shading normal: the map's tangent-space vector over the vertex's
     * view-space frame, normalised. With the normal-map term left out the
     * vertex normal stands alone - the tangent-space vector (0, 0, 1). */
    for (k = 0; k < 3; k++) N[k] = frame[6 + k];
    if (terms & SL_WA_MATX_T_NORMAL) {
        tex(ctx, 0, u, v, nt);
        for (k = 0; k < 3; k++) {
            float n = nt[k] * m->normal_scale + m->normal_bias;
            nt[k] = n;
        }
        for (k = 0; k < 3; k++)
            N[k] = nt[0] * frame[k] + nt[1] * frame[3 + k] + nt[2] * frame[6 + k];
    }
    len = sqrt((double) N[0] * N[0] + (double) N[1] * N[1] + (double) N[2] * N[2]);
    if (len > 0.0) for (k = 0; k < 3; k++) N[k] = (float) (N[k] / len);

    /* The sphere lookup on the view-space normal's x and y, and the two
     * weighted terms. */
    refl[0] = refl[1] = refl[2] = refl[3] = 0.0f;
    if (terms & SL_WA_MATX_T_REFLECT) {
        su = N[0] * m->sphere_scale[0] + m->sphere_bias[0];
        sv = N[1] * m->sphere_scale[1] + m->sphere_bias[1];
        tex(ctx, 1, su, sv, refl);
    }
    for (k = 0; k < 3; k++)
        rgba[k] = m->base_weight[k] * colour[k] * base[k]
                + ((terms & SL_WA_MATX_T_REFLECT) ? m->reflect_weight[k] * refl[k] : 0.0f);
    mean = m->mean_weight * (rgba[0] + rgba[1] + rgba[2]);
    if (mean < m->alpha_floor) mean = m->alpha_floor;
    rgba[3] = mean * base[3] * colour[3];
}

/* ---- the policy and the cache -------------------------------------------- */

static int  g_active = -1;              /* -1 = not asked yet */
static char g_set[32];
static char g_root[512];
static int  g_root_done, g_root_from_env;

static unsigned g_lookups, g_loaded, g_missing, g_invalid;

static void path_join(char *out, unsigned cap, const char *a, const char *b)
{
    unsigned n;
    out[0] = '\0';
    if (a == NULL || b == NULL) return;
    n = (unsigned) strlen(a);
    if (n + 1u + (unsigned) strlen(b) + 1u > cap) return;
    strcpy(out, a);
    if (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/') strcat(out, "\\");
    strcat(out, b);
}

/* Normalise one set WORD into this provider's set name: "xbla", or "" for no
 * world assets at all. The off-words are explicit so that a diagnostic
 * override can pin the provider DARK as well as lit; an unrecognised word is
 * dark too, which is the accepted build's behaviour and the safe direction. */
static const char *wa_set_word(const char *w)
{
    if (w == NULL || w[0] == '\0') return "";
    if (strcmp(w, "xbla") == 0 || strcmp(w, "XBLA") == 0) return "xbla";
    return "";
}

/* THE DIAGNOSTIC OVERRIDE (#68, 2026-09-23). SL_WORLD_ASSETS used to BE the
 * selection: absent meant off, and the player's TEXTURES setting could not
 * reach it. It is now an ADVANCED / DIAGNOSTIC override that PINS this one
 * provider, and the normal path never sets it:
 *
 *     override (SL_WORLD_ASSETS)  ->  presentation-derived  (sl_presentation.h)
 *
 * WHY IT IS NOT SILENT. A variable left over from an earlier shell would
 * otherwise pin the world assets while the presentation moved the textures -
 * which is exactly the mixed state this round removed, reappearing as a
 * testing artefact nobody can see. So whenever the override DISAGREES with
 * what the presentation would have selected, one line says so, naming both;
 * it is printed once per distinct (override, derived) pair, so it cannot
 * repeat per frame but does reappear when the player cycles into a different
 * disagreement. */
static char g_mixed_said_over[32];
static char g_mixed_said_derived[32];
static int  g_mixed_said;

static void wa_mixed_notice(const char *over, const char *derived, int presentation)
{
    if (g_mixed_said
        && strcmp(g_mixed_said_over, over) == 0
        && strcmp(g_mixed_said_derived, derived) == 0)
        return;
    g_mixed_said = 1;
    strcpy(g_mixed_said_over, over);
    strcpy(g_mixed_said_derived, derived);
    fprintf(stderr,
            "sl_worldasset: MIXED STATE - SL_WORLD_ASSETS pins world assets to "
            "'%s' while PRESENTATION %s(%d) selects '%s'. The override wins. "
            "Unset SL_WORLD_ASSETS for the coordinated picture.\n",
            over[0] ? over : "none",
            sl_presentation_name(presentation), presentation,
            derived[0] ? derived : "none");
}

/* WHICH SET IS SELECTED, re-asked every time rather than latched once.
 *
 * THIS IS WHAT MAKES LIVE SWITCHING WORK. The old function answered once per
 * process because the selection came from the environment and could not move.
 * The selection now comes from a setting the player changes with F5 mid-frame,
 * so it is read each ask - the same shape sl_texprov_active has always had -
 * and a change bumps a generation. Nothing here loads, frees or uploads: an
 * asset already resident STAYS resident and keeps its GL names, it is simply
 * not looked up and therefore not drawn. Returning to the set that owns it
 * finds the same cache slot and the same upload. */
static unsigned g_wa_gen;

int sl_wa_active(void)
{
    const char *e;
    const char *want;
    int p = sl_presentation();
    const char *derived = wa_set_word(sl_presentation_world_set(p));

    e = getenv("SL_WORLD_ASSETS");
    if (e != NULL && e[0] != '\0') {
        want = wa_set_word(e);
        if (strcmp(want, derived) != 0) wa_mixed_notice(want, derived, p);
    } else {
        want = derived;
    }

    if (strcmp(want, g_set) != 0) {
        strcpy(g_set, want);
        if (g_active >= 0) g_wa_gen++;       /* a real switch, not the first ask */
    }
    g_active = (g_set[0] != '\0');
    return g_active;
}

const char *sl_wa_set(void) { (void) sl_wa_active(); return g_set; }

/* Bumped every time the selection MOVES after the first ask. The renderer
 * keys nothing on it today - the seam simply stops drawing when the provider
 * is dark - but a later asset class that holds per-set state has the same
 * generation idiom the texture provider does. */
unsigned sl_wa_generation(void) { (void) sl_wa_active(); return g_wa_gen; }

static const char *wa_root(void)
{
    const char *e;
    if (g_root_done) return g_root;
    g_root_done = 1;
    e = getenv("SL_WORLDASSET_ROOT");
    if (e != NULL && e[0] != '\0' && strlen(e) + 1u < sizeof g_root) {
        strcpy(g_root, e);
        g_root_from_env = 1;
        return g_root;
    }
    path_join(g_root, sizeof g_root, sl_asset_override_dir(), "worldassets");
    if (g_root[0] == '\0') strcpy(g_root, ".");
    return g_root;
}

const char *sl_wa_root(void)
{
    if (!sl_wa_active()) return "";
    return wa_root();
}

/* The class's directory name, in the SOURCE's own vocabulary rather than an
 * invented one: a background is the stage's static world shell and the source
 * carries it as "background", which is also what the preparation tool reads. */
static const char *class_dir(unsigned c)
{
    switch (c) {
    case SL_WA_CLASS_SKYDOME:    return "skydome";
    case SL_WA_CLASS_BACKGROUND: return "background";
    case SL_WA_CLASS_CHAR:       return "char";
    case SL_WA_CLASS_HEAD:       return "head";
    case SL_WA_CLASS_GUN:        return "gun";
    case SL_WA_CLASS_PROP:       return "prop";
    default:                     return NULL;
    }
}

/* One entry per (class, path) ever asked for. `asset` is NULL for a miss or
 * an invalid file, which is what makes both outcomes cost one attempt. */
struct slot {
    unsigned cls;
    char path[SL_WA_PATH_MAX];
    struct sl_wa_asset *asset;
    unsigned char *buf;
};
/* 256 since the stage's props (#70): a stage asks for each character body and
 * head it shows, each first-person model the player holds and each prop model it
 * draws, on top of its sky and background - a miss holds its slot too. */
#define SLOT_MAX 256
static struct slot g_slot[SLOT_MAX];
static unsigned g_slots;

static void summary_once(void)
{
    static int done;
    if (done) return;
    done = 1;
    fprintf(stderr,
            "sl_worldasset: set=%s root=%s (%s)\n",
            g_set, wa_root(), g_root_from_env ? "SL_WORLDASSET_ROOT" : "player data");
}

static struct sl_wa_asset *load_file(const char *file, unsigned cls,
                                     const char *path)
{
    FILE *f;
    long end;
    unsigned long len;
    unsigned char *buf = NULL;
    struct sl_wa_asset *a = NULL;
    int rc;

    f = fopen(file, "rb");
    if (f == NULL) { g_missing++; return NULL; }

    /* The length is MEASURED here and nothing downstream takes the header's
     * word for it. A file too large to be one of ours is refused before a
     * byte is allocated. */
    if (fseek(f, 0, SEEK_END) != 0) { rc = SL_WA_E_IO; goto bad; }
    end = ftell(f);
    if (end < 0) { rc = SL_WA_E_IO; goto bad; }
    len = (unsigned long) end;
    if (len < SL_WA_HEADER) { rc = SL_WA_E_TRUNC; goto bad; }
    if (len > (unsigned long) SL_WA_MAX_FILE) { rc = SL_WA_E_COUNT; goto bad; }
    if (fseek(f, 0, SEEK_SET) != 0) { rc = SL_WA_E_IO; goto bad; }

    buf = (unsigned char *) malloc((size_t) len);
    if (buf == NULL) { rc = SL_WA_E_MEM; goto bad; }
    if (fread(buf, 1, (size_t) len, f) != (size_t) len) { rc = SL_WA_E_IO; goto bad; }
    fclose(f);
    f = NULL;

    a = (struct sl_wa_asset *) calloc(1, sizeof *a);
    if (a == NULL) { rc = SL_WA_E_MEM; goto bad; }

    rc = sl_wa_parse(buf, len, a);
    if (rc != SL_WA_OK) goto bad;
    /* The file must name the item it is being asked to answer for. Without
     * this a file dropped under the wrong name would draw the wrong sky. */
    if (strcmp(a->path, path) != 0) { rc = SL_WA_E_PATH; goto bad; }
    if (a->asset_class != cls) { rc = SL_WA_E_CLASS; goto bad; }

    rc = sl_wa_decode_arrays(buf, a);
    if (rc != SL_WA_OK) goto bad;

    g_loaded++;
    fprintf(stderr,
            "sl_worldasset: LOADED %s/%s  %u verts  %u indices (%u tris)  "
            "%u meshes  %u materials  %u images\n",
            class_dir(cls), path, a->vertex_count, a->index_count,
            a->index_count / 3u, a->mesh_count, a->material_count,
            a->texture_count);
    return a;

bad:
    if (f != NULL) fclose(f);
    free(buf);
    free(a);
    g_invalid++;
    /* ONE bounded diagnostic per (class, path); the slot remembers the
     * outcome so this cannot repeat per frame. */
    fprintf(stderr, "sl_worldasset: INVALID %s/%s - %s; the stage draws "
                    "exactly as it does with no asset\n",
            class_dir(cls) ? class_dir(cls) : "?", path, sl_wa_strerror(rc));
    return NULL;
}

const struct sl_wa_asset *sl_wa_lookup(unsigned asset_class, const char *path)
{
    char file[640], rel[128];
    const char *cd;
    unsigned i;

    if (!sl_wa_active()) return NULL;
    if (path == NULL || path[0] == '\0') return NULL;
    if (strlen(path) + 1u > SL_WA_PATH_MAX) return NULL;
    cd = class_dir(asset_class);
    if (cd == NULL) return NULL;

    g_lookups++;
    for (i = 0; i < g_slots; i++)
        if (g_slot[i].cls == asset_class && strcmp(g_slot[i].path, path) == 0)
            return g_slot[i].asset;
    if (g_slots >= SLOT_MAX) return NULL;

    summary_once();
    if ((unsigned) snprintf(rel, sizeof rel, "%s\\%s\\%s.slwa", g_set, cd, path)
        >= sizeof rel) return NULL;
    path_join(file, sizeof file, wa_root(), rel);
    if (file[0] == '\0') return NULL;

    i = g_slots++;
    g_slot[i].cls = asset_class;
    strcpy(g_slot[i].path, path);
    g_slot[i].asset = load_file(file, asset_class, path);
    if (g_slot[i].asset == NULL)
        fprintf(stderr, "sl_worldasset: %s/%s not provided - %s unchanged\n",
                cd, path, path);
    return g_slot[i].asset;
}

unsigned sl_wa_lookups(void) { return g_lookups; }
unsigned sl_wa_loaded(void)  { return g_loaded; }
unsigned sl_wa_missing(void) { return g_missing; }
unsigned sl_wa_invalid(void) { return g_invalid; }

/* ---- #68 the remaster's stage fog: SLEN environment metadata --------------
 *
 * See the header for what the file is and where it comes from. The parser is
 * pure and bounded like sl_wa_parse; the loader reads the file at most once
 * per selected set. */
int sl_wa_env_parse(const unsigned char *buf, unsigned long len,
                    struct sl_wa_env *rows, unsigned cap, unsigned *count)
{
    unsigned n, i, j;

    if (count != (unsigned *) 0) *count = 0;
    if (buf == (const unsigned char *) 0 || rows == (struct sl_wa_env *) 0)
        return SL_WA_E_IO;
    if (len < SL_WA_ENV_HEADER) return SL_WA_E_TRUNC;
    if (buf[0] != 'S' || buf[1] != 'L' || buf[2] != 'E' || buf[3] != 'N')
        return SL_WA_E_MAGIC;
    if (rd_u32(buf + 4) != SL_WA_ENV_VERSION) return SL_WA_E_VERSION;
    n = rd_u32(buf + 8);
    if (n == 0u || n > SL_WA_ENV_MAX_ROWS || n > cap) return SL_WA_E_COUNT;
    if (rd_u32(buf + 12) != SL_WA_ENV_ROW) return SL_WA_E_LAYOUT;
    /* n is bounded above, so this product cannot wrap. */
    if (rd_u32(buf + 16) != (unsigned) len
        || len != (unsigned long) SL_WA_ENV_HEADER + (unsigned long) n * SL_WA_ENV_ROW)
        return SL_WA_E_SIZE;
    if (rd_u32(buf + 20) != 0u || rd_u32(buf + 24) != 0u || rd_u32(buf + 28) != 0u)
        return SL_WA_E_FLAGS;
    for (i = 0; i < n; i++) {
        const unsigned char *r = buf + SL_WA_ENV_HEADER + i * SL_WA_ENV_ROW;
        float s = rd_f32(r + 4), e = rd_f32(r + 8);
        /* Finite, ordered and inside a sane world: a NaN fails every compare. */
        if (!(s > -1.0e7f && s < 1.0e7f) || !(e > -1.0e7f && e < 1.0e7f)
            || !(e > s))
            return SL_WA_E_LAYOUT;
        if (r[15] != 0u) return SL_WA_E_FLAGS;
        rows[i].id = rd_u32(r);
        rows[i].fog_start = s;
        rows[i].fog_end = e;
        rows[i].rgb[0] = r[12];
        rows[i].rgb[1] = r[13];
        rows[i].rgb[2] = r[14];
        for (j = 0; j < i; j++)
            if (rows[j].id == rows[i].id) return SL_WA_E_LAYOUT;
    }
    if (count != (unsigned *) 0) *count = n;
    return SL_WA_OK;
}

float sl_wa_env_fog_coord(const struct sl_wa_env *e, float d)
{
    float f;
    if (e == (const struct sl_wa_env *) 0 || !(e->fog_end > e->fog_start))
        return 0.0f;
    f = (e->fog_end - d) / (e->fog_end - e->fog_start);
    if (!(f > 0.0f)) f = 0.0f;           /* also catches a NaN distance */
    if (f > 1.0f) f = 1.0f;
    return 1.0f - f;
}

struct sl_wa_env sl_wa_env_presented(const struct sl_wa_env *authored,
                                     const char *set, int authentic)
{
    struct sl_wa_env r = *authored;
    if (!authentic) {
        float e = sl_presentation_fog_end(set, authored->id, authored->fog_end);
        float s = sl_presentation_fog_start(set, authored->id, authored->fog_start);
        if (e > authored->fog_start && e < 1.0e7f) r.fog_end = e;
        if (s < r.fog_end && s > -1.0e7f) r.fog_start = s;       /* D-043 */
    }
    return r;
}

struct sl_wa_env sl_wa_env_coloured(const struct sl_wa_env *row, const float rgb[3])
{
    struct sl_wa_env r = *row;
    int k;
    for (k = 0; k < 3; k++) {
        float c = rgb[k];
        int v;
        if (!(c > 0.0f)) c = 0.0f;           /* also a NaN */
        if (c > 255.0f) c = 255.0f;
        v = (int) (c + 0.5f);
        r.rgb[k] = (unsigned char) v;
    }
    return r;
}

void sl_wa_sky_shade(const float fog[3], const float cloud[3], const float d[3],
                     float out[3])
{
    const double hz = sqrt((double) d[0] * d[0] + (double) d[2] * d[2]) + 0.0001;
    double up = 2.0 * (double) d[1] / hz;
    if (!(up > 0.0)) up = 0.0;               /* also a NaN */
    if (up > 1.0) up = 1.0;
    sl_wa_sky_shade_frac(fog, cloud, 1.0 - up, out);
}

void sl_wa_sky_shade_frac(const float fog[3], const float cloud[3], double frac,
                          float out[3])
{
    int k;
    if (!(frac > 0.0)) frac = 0.0;           /* also a NaN */
    if (frac > 1.0) frac = 1.0;
    for (k = 0; k < 3; k++) {
        double c = (double) fog[k] + (double) cloud[k] * (1.0 - fog[k] / 255.0) * (1.0 - frac);
        if (!(c > 0.0)) c = 0.0;
        if (c > 255.0) c = 255.0;
        out[k] = (float) c;
    }
}

/* One side's fraction at game row r (see the header). */
static double sky_side_frac(const float rows[10], const int fill[4], int side, double r)
{
    const double ra = rows[2 + 4 * side], fa = rows[3 + 4 * side];
    const double rb = rows[4 + 4 * side], fb = rows[5 + 4 * side];
    double f;
    if (fill[2 * side + 1] && r >= rb) return 1.0;
    if (fill[2 * side] && r <= ra) return 1.0;
    f = (rb != ra) ? fa + (r - ra) / (rb - ra) * (fb - fa) : fa;
    if (!(f > 0.0)) f = 0.0;                 /* also a NaN */
    if (f > 1.0) f = 1.0;
    return f;
}

double sl_wa_sky_screen_frac(const float rows[10], const int fill[4],
                             float ndc_x, float ndc_y)
{
    const double r = (double) rows[0] + (1.0 - (double) ndc_y) * 0.5 * (double) rows[1];
    const double u = ((double) ndc_x + 1.0) * 0.5;
    double f = sky_side_frac(rows, fill, 0, r);
    f += u * (sky_side_frac(rows, fill, 1, r) - f);
    if (!(f > 0.0)) f = 0.0;
    if (f > 1.0) f = 1.0;
    return f;
}

int sl_wa_sky_screen_edge(const float rows[10], const int fill[4], float ndc_y[2])
{
    int side;
    if (!fill[1] || !fill[3] || !(rows[1] > 0.0f)) return 0;
    for (side = 0; side < 2; side++)
        ndc_y[side] = 1.0f - 2.0f * (rows[4 + 4 * side] - rows[0]) / rows[1];
    return 1;
}

/* D-046's switch: SL_XBLA_LENT_SKY_EDGE=0 shades a lent dome along each
 * vertex's direction, as D-044 drew it. Developer switch, one cached getenv. */
int sl_wa_sky_lent_edge(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_XBLA_LENT_SKY_EDGE");
                  on = (s == NULL || *s != '0'); }
    return on && sl_wa_active();
}

struct sl_wa_env sl_wa_env_blend(const struct sl_wa_env *main_row,
                                 const struct sl_wa_env *alt_row, float t)
{
    struct sl_wa_env r = *main_row;
    int k;
    if (!(t > 0.0f)) return r;           /* also a NaN */
    if (t > 1.0f) t = 1.0f;
    r.fog_start = main_row->fog_start + t * (alt_row->fog_start - main_row->fog_start);
    r.fog_end = main_row->fog_end + t * (alt_row->fog_end - main_row->fog_end);
    if (!(r.fog_end > r.fog_start)) return *main_row;
    for (k = 0; k < 3; k++) {
        float c = (float) main_row->rgb[k]
                + t * ((float) alt_row->rgb[k] - (float) main_row->rgb[k]);
        int v = (int) (c + 0.5f);
        r.rgb[k] = (unsigned char) (v < 0 ? 0 : (v > 255 ? 255 : v));
    }
    return r;
}

/* D-025 the authentic-range diagnostic: SL_XBLA_AUTHENTIC_FOG=1 draws every
 * row at its authored range. Developer switch, one cached getenv. */
static int env_authentic(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_XBLA_AUTHENTIC_FOG");
                  on = (s != NULL && *s != '\0' && *s != '0'); }
    return on;
}

/* D-043's switch: SL_XBLA_FOG_START=0 draws every row from its authored
 * start (the presented end stays). Developer switch, one cached getenv. */
static int env_fog_start_on(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_XBLA_FOG_START");
                  on = (s == NULL || *s != '0'); }
    return on;
}

static struct sl_wa_env g_env[SL_WA_ENV_MAX_ROWS];       /* as authored   */
static struct sl_wa_env g_env_draw[SL_WA_ENV_MAX_ROWS];  /* as presented  */
static unsigned g_env_n;
static char     g_env_set[32];     /* the set the table was read for; "" = none */
static int      g_env_tried;

static void env_load(void)
{
    char file[640], rel[64];
    FILE *f;
    long end;
    unsigned char *buf = NULL;
    int rc = SL_WA_E_IO;

    g_env_n = 0;
    if ((unsigned) snprintf(rel, sizeof rel, "%s\\environment.slen", g_set)
        >= sizeof rel) return;
    path_join(file, sizeof file, wa_root(), rel);
    if (file[0] == '\0') return;
    f = fopen(file, "rb");
    if (f == NULL) {
        fprintf(stderr, "sl_worldasset: %s/environment.slen not provided - "
                        "the replacement background draws without the "
                        "remaster's stage fog\n", g_set);
        return;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (end = ftell(f)) >= (long) SL_WA_ENV_HEADER
        && end <= (long) (SL_WA_ENV_HEADER + SL_WA_ENV_MAX_ROWS * SL_WA_ENV_ROW)
        && fseek(f, 0, SEEK_SET) == 0
        && (buf = (unsigned char *) malloc((size_t) end)) != NULL
        && fread(buf, 1, (size_t) end, f) == (size_t) end) {
        unsigned n = 0;
        rc = sl_wa_env_parse(buf, (unsigned long) end, g_env,
                             SL_WA_ENV_MAX_ROWS, &n);
        if (rc == SL_WA_OK) g_env_n = n;
    } else {
        rc = SL_WA_E_SIZE;
    }
    fclose(f);
    free(buf);
    if (rc == SL_WA_OK) {
        unsigned i;
        fprintf(stderr, "sl_worldasset: LOADED %s/environment.slen  %u "
                        "environment rows (remaster stage fog)\n", g_set, g_env_n);
        for (i = 0; i < g_env_n; i++) {
            float want = sl_wa_env_presented(&g_env[i], g_set, 0).fog_end;
            float wants = sl_wa_env_presented(&g_env[i], g_set, 0).fog_start;
            g_env_draw[i] = sl_wa_env_presented(&g_env[i], g_set, env_authentic());
            if (!env_fog_start_on()) g_env_draw[i].fog_start = g_env[i].fog_start;
            if (wants != g_env[i].fog_start)
                fprintf(stderr, "sl_worldasset: environment row %u fog start %.0f "
                                "authored, %.0f presented (presentation fog start, "
                                "D-043 / D-055)%s\n", g_env[i].id, (double) g_env[i].fog_start,
                        (double) wants,
                        env_authentic() ? " - SL_XBLA_AUTHENTIC_FOG=1: drawn from the authored start"
                        : !env_fog_start_on() ? " - SL_XBLA_FOG_START=0: drawn from the authored start"
                        : "");
            if (want != g_env[i].fog_end)
                fprintf(stderr, "sl_worldasset: environment row %u fog end %.0f "
                                "authored, %.0f presented (presentation fog "
                                "range, D-025 / D-029 / D-055)%s\n",
                        g_env[i].id, (double) g_env[i].fog_end, (double) want,
                        env_authentic() ? " - SL_XBLA_AUTHENTIC_FOG=1: drawn "
                                          "at the authored end" : "");
        }
    } else
        fprintf(stderr, "sl_worldasset: INVALID %s/environment.slen - %s; the "
                        "replacement background draws without the remaster's "
                        "stage fog\n", g_set, sl_wa_strerror(rc));
}

const struct sl_wa_env *sl_wa_env_lookup(int env_id)
{
    static int last_id = -1;
    static const struct sl_wa_env *last;
    unsigned i;

    if (!sl_wa_active()) return (const struct sl_wa_env *) 0;
    if (!g_env_tried || strcmp(g_env_set, g_set) != 0) {
        g_env_tried = 1;
        strcpy(g_env_set, g_set);
        last_id = -1;
        last = (const struct sl_wa_env *) 0;
        env_load();
    }
    if (env_id < 0 || g_env_n == 0u) return (const struct sl_wa_env *) 0;
    if (env_id == last_id) return last;
    last_id = env_id;
    last = (const struct sl_wa_env *) 0;
    for (i = 0; i < g_env_n; i++)
        if (g_env_draw[i].id == (unsigned) env_id) { last = &g_env_draw[i]; break; }
    return last;
}

/* D-030's switch: SL_XBLA_GAME_ATMOSPHERE=0 draws the set's own atmosphere.
 * Developer switch, one cached getenv. */
static int env_game_atmosphere_on(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_XBLA_GAME_ATMOSPHERE");
                  on = (s == NULL || *s != '0'); }
    return on;
}

int sl_wa_env_game_atmosphere(int env_id)
{
    static int said = -1;
    int r;
    if (env_id < 0 || !sl_wa_active()) return 0;
    r = sl_presentation_game_atmosphere(g_set, (unsigned) env_id);
    if (r && said != env_id) {
        said = env_id;
        fprintf(stderr, "sl_worldasset: environment row %d - the game's atmosphere "
                        "(D-030): fog colour from the game's environment, sky shaded "
                        "as its cloud layer%s\n", env_id,
                env_game_atmosphere_on() ? "" : " - NOT drawn (SL_XBLA_GAME_ATMOSPHERE=0)");
    }
    return r && env_game_atmosphere_on();
}

/* D-041's switch: SL_XBLA_FOGGED_PROPS=0 keeps the game's far-fog cut.
 * Developer switch, one cached getenv. */
static int env_prop_fog_drawn_on(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_XBLA_FOGGED_PROPS");
                  on = (s == NULL || *s != '0'); }
    return on;
}

/* D-041 PROPS PAST THE FAR-FOG CUT. See the header. */
int sl_wa_env_prop_fog_drawn(int env_id)
{
    static int said = -1;
    int r;
    if (env_id < 0 || !sl_wa_active()) return 0;
    r = sl_presentation_prop_fog_drawn(g_set, (unsigned) env_id);
    if (r && said != env_id) {
        said = env_id;
        fprintf(stderr, "sl_worldasset: environment row %d - props drawn past the "
                        "game's far-fog cut, fogged by the row (D-041)%s\n", env_id,
                env_prop_fog_drawn_on() ? "" : " - NOT drawn (SL_XBLA_FOGGED_PROPS=0)");
    }
    return r && env_prop_fog_drawn_on();
}

/* D-030 / D-034 THE STAGE'S SKY SHADE. See the header. */
int sl_wa_stage_game_atmosphere(const char *item, int env_id)
{
    static int said;
    int r;
    if (env_id >= 0) return sl_wa_env_game_atmosphere(env_id);
    if (item == NULL || !sl_wa_active()) return 0;
    r = sl_presentation_fogless_atmosphere(g_set, item);
    if (r && !said) {
        said = 1;
        fprintf(stderr, "sl_worldasset: %s has no environment row - its sky dome is shaded "
                        "as the game's own cloud layer under the fogless record (D-034)%s\n",
                item, env_game_atmosphere_on() ? "" : " - NOT drawn (SL_XBLA_GAME_ATMOSPHERE=0)");
    }
    return r && env_game_atmosphere_on();
}

/* #83 THE WITHHELD FILL. See the header. */
int sl_wa_fill_withheld(const char *item, int room)
{
    static int on = -1, said = -1;
    int r;
    if (on < 0) { const char *s = getenv("SL_BG_FILL_WITHHELD"); on = (s == NULL || *s != '0'); }
    if (item == NULL || room < 0 || !sl_wa_active()) return 0;
    r = sl_presentation_fill_withheld(g_set, item, room);
    if (r && said != room) {
        said = room;
        fprintf(stderr, "#78 nativefill: %s room %d is remodelled whole by the replacement - "
                        "no native fill there%s\n", item, room,
                on ? "" : " - FILLED as the rule decides (SL_BG_FILL_WITHHELD=0)");
    }
    return r && on;
}

/* #96 THE GAME'S DETAIL IN A NAMED ROOM. See the header. */
int sl_wa_native_kept(const char *item, int room)
{
    static int on = -1, said = -1;
    int r;
    if (on < 0) { const char *s = getenv("SL_BG_KEEP_ROOMS"); on = (s == NULL || *s != '0'); }
    if (item == NULL || room < 0 || !sl_wa_active()) return 0;
    r = sl_presentation_native_kept(g_set, item, room);
    if (r && said != room) {
        said = room;
        fprintf(stderr, "#96 nativefill: %s room %d keeps the game's translucent detail the "
                        "replacement has no surface for%s\n", item, room,
                on ? "" : " - NOT drawn (SL_BG_KEEP_ROOMS=0)");
    }
    return r && on;
}

/* D-035 THE GAME'S FADE, BY STAGE. See the header. */
int sl_wa_game_fade(const char *item)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_BG_FADE_STAGES"); on = (s == NULL || *s != '0'); }
    if (item == NULL || !sl_wa_active()) return 0;
    return !on || sl_presentation_game_fade(g_set, item);
}

/* D-039 SIGHTLINE'S OWN SEA. See the header. */
int sl_wa_own_sea(const char *item)
{
    static int form = -1, said;
    if (form < 0) {
        const char *s = getenv("SL_XBLA_SEA");
        form = (s == NULL || *s == '\0') ? SL_WA_SEA_DEFAULT : atoi(s);
        if (form < 0) form = 0;
    }
    if (item == NULL || !sl_wa_active() || !sl_presentation_own_sea(g_set, item)) return 0;
    if (!said) {
        said = 1;
        fprintf(stderr, "sl_worldasset: %s's sea - %s (D-039, SL_XBLA_SEA=%d)\n", item,
                form ? "Sightline's own" : "the game's", form);
    }
    return form;
}

/* D-031's switch: SL_XBLA_BORROWED_SKY=0 keeps the game's own fill on a stage
 * that borrows a sky. Developer switch, one cached getenv. */
static int sky_borrow_on(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_XBLA_BORROWED_SKY");
                  on = (s == NULL || *s != '0'); }
    return on;
}

/* D-031 A BORROWED SKY. See the header. */
const char *sl_wa_sky_borrow(const char *item, int env_id, unsigned *row)
{
    static int said;
    const char *sky;
    unsigned r = 0u;
    if (item == NULL || env_id >= 0 || !sl_wa_active()) return NULL;
    sky = sl_presentation_sky_borrow(g_set, item, &r);
    if (sky == NULL) return NULL;
    if (!said) {
        said = 1;
        fprintf(stderr, "sl_worldasset: %s has no sky of its own - skydome/%s under "
                        "environment row %u's atmosphere (D-031)%s\n", item, sky, r,
                sky_borrow_on() ? "" : " - NOT drawn (SL_XBLA_BORROWED_SKY=0): the game's fill");
    }
    if (!sky_borrow_on()) return NULL;
    if (row != NULL) *row = r;
    return sky;
}

/* D-044's switch: SL_XBLA_LENT_SKY=0 keeps the game's own cloud layer on a
 * stage lent a dome. Developer switch, one cached getenv. */
static int sky_lent_on(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_XBLA_LENT_SKY");
                  on = (s == NULL || *s != '0'); }
    return on;
}

/* D-044 A LENT DOME. See the header. */
const char *sl_wa_sky_lent(const char *item)
{
    static int said;
    const char *sky;
    if (item == NULL || !sl_wa_active()) return NULL;
    sky = sl_presentation_sky_lent(g_set, item);
    if (sky == NULL) return NULL;
    if (!said) {
        said = 1;
        fprintf(stderr, "sl_worldasset: the source holds no dome for %s - skydome/%s "
                        "under the stage's own atmosphere (D-044)%s\n", item, sky,
                sky_lent_on() ? "" : " - NOT drawn (SL_XBLA_LENT_SKY=0): the game's cloud layer");
    }
    return sky_lent_on() ? sky : NULL;
}

/* THE EYE, RECOVERED FROM THE TRANSFORM ITSELF. See the header for why this
 * is read out of the matrix rather than asked of the game: the two answers
 * are not the same answer, and only this one cannot disagree with the matrix
 * it is paired with. */
int sl_wa_view_origin(const float *m, float *q)
{
    float r0[3], r1[3], r2[3], sx, sy, d0, d1, d2, n2;
    int i;

    if (m == (const float *) 0 || q == (float *) 0) return 0;

    /* The camera basis, as the projection's own terms scaled it. */
    r0[0] = m[0];  r0[1] = m[4];  r0[2] = m[8];
    r1[0] = m[1];  r1[1] = m[5];  r1[2] = m[9];
    r2[0] = -m[3]; r2[1] = -m[7]; r2[2] = -m[11];

    sx = (float) sqrt((double) (r0[0]*r0[0] + r0[1]*r0[1] + r0[2]*r0[2]));
    sy = (float) sqrt((double) (r1[0]*r1[0] + r1[1]*r1[1] + r1[2]*r1[2]));
    n2 = (float) sqrt((double) (r2[0]*r2[0] + r2[1]*r2[1] + r2[2]*r2[2]));
    if (sx < 1.0e-6f || sy < 1.0e-6f || n2 < 1.0e-6f) return 0;
    for (i = 0; i < 3; i++) { r0[i] /= sx; r1[i] /= sy; r2[i] /= n2; }

    /* It has to BE a camera basis, or this is not a matrix of the assumed
     * shape and no eye can be read out of it. Refused, never guessed at. */
    if (fabs((double) (r0[0]*r1[0] + r0[1]*r1[1] + r0[2]*r1[2])) > 1.0e-3) return 0;
    if (fabs((double) (r0[0]*r2[0] + r0[1]*r2[1] + r0[2]*r2[2])) > 1.0e-3) return 0;
    if (fabs((double) (r1[0]*r2[0] + r1[1]*r2[1] + r1[2]*r2[2])) > 1.0e-3) return 0;
    if (fabs((double) n2 - 1.0) > 1.0e-3) return 0;

    /* The eye's coordinates along that basis, and so the eye. */
    d0 = -m[12] / sx;
    d1 = -m[13] / sy;
    d2 =  m[15] / n2;
    for (i = 0; i < 3; i++) q[i] = d0 * r0[i] + d1 * r1[i] + d2 * r2[i];
    return 1;
}

/* The skydome's anchor. See the header: the source draws a dome with its own
 * origin on the eye, so that is the default answer; the bounds centre is the
 * earlier rule, reachable only as a control. */
void sl_wa_sky_anchor(const float bmin[3], const float bmax[3],
                      int bounds_centre, float out[3])
{
    int i;

    for (i = 0; i < 3; i++)
        out[i] = bounds_centre ? (bmin[i] + bmax[i]) * 0.5f : 0.0f;
}

/* The D-023 draw rule. See the header. */
int sl_wa_material_drawn(unsigned flags, int generated_on)
{
    if ((flags & SL_WA_MAT_GENERATED) != 0u) return generated_on != 0;
    return 1;
}

/* The item pass's decal rule. See the header. */
int sl_wa_decal_priority(unsigned flags, int decal_on)
{
    return (flags & SL_WA_MAT_DECAL) != 0u && decal_on != 0;
}

/* #81 the decal pass's depth adjustment. See the header. */
int sl_wa_depth_affine(const float *C, float *a, float *b)
{
    const double zx = C[2], zy = C[6], zz = C[10], wx = C[3], wy = C[7], wz = C[11];
    const double ww = wx * wx + wy * wy + wz * wz;
    double k, rx, ry, rz;
    if (C == (const float *) 0 || a == (float *) 0 || b == (float *) 0 || !(ww > 0.0)) return 0;
    k = (zx * wx + zy * wy + zz * wz) / ww;
    rx = zx - k * wx; ry = zy - k * wy; rz = zz - k * wz;
    if (rx * rx + ry * ry + rz * rz > 1e-8 * (zx * zx + zy * zy + zz * zz + ww)) return 0;
    *a = (float) k;
    *b = (float) ((double) C[14] - k * (double) C[15]);
    return 1;
}

float sl_wa_decal_pull_z(float a, float b, float w, float pull)
{
    double d, w2;
    if (!(w > 0.0f)) return a * w + b;
    d = (pull > 0.0f) ? (double) pull : 0.0;
    if (d > 0.5 * (double) w) d = 0.5 * (double) w;
    w2 = (double) w - d;
    return (float) (((double) a * w2 + (double) b) * ((double) w / w2));
}

float sl_wa_decal_plane_ratio(const float *p0, const float *p1, const float *p2,
                              const float *eye, float p)
{
    double e1[3], e2[3], n[3], ln, h;
    int k;
    if (p0 == (const float *) 0 || p1 == (const float *) 0 || p2 == (const float *) 0
        || eye == (const float *) 0 || !(p > 0.0f)) return 0.0f;
    for (k = 0; k < 3; k++) { e1[k] = (double) p1[k] - p0[k]; e2[k] = (double) p2[k] - p0[k]; }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    ln = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(ln > 0.0)) return 0.0f;
    h = fabs(n[0] * ((double) eye[0] - p0[0]) + n[1] * ((double) eye[1] - p0[1])
             + n[2] * ((double) eye[2] - p0[2])) / ln;
    if (!(h > 0.0)) return 0.0f;
    return (float) ((double) p / h);
}

float sl_wa_decal_plane_z(float a, float b, float w, float ratio)
{
    double r = (ratio > 0.0f) ? (double) ratio : 0.0;
    if (r > 0.5) r = 0.5;
    return (float) ((double) a * w + (double) b / (1.0 - r));
}

/* The item pass's coverage rule. See the header. */
int sl_wa_coverage_blue(unsigned flags, unsigned blend, int coverage_on)
{
    return (flags & SL_WA_MAT_COVERAGE_BLUE) != 0u && blend == SL_WA_BLEND_ALPHA
           && coverage_on != 0;
}

/* #94 the environments the linear shade program takes. See the header. */
int sl_wa_lin_takes(int texenv, int row, int on)
{
    if (texenv == 0 || texenv == 3 || texenv == 4) return 1;
    return texenv == 2 && row != 0 && on != 0;
}

/* #94 a cutout after the solid world behind it. See the header. */
unsigned sl_wa_first_cutout(const struct sl_wa_asset *a)
{
    unsigned mi;
    if (a == NULL) return 0u;
    for (mi = 0; mi < a->mesh_count; mi++) {
        const unsigned m = a->meshes[mi].material;
        if (m < a->material_count && a->materials[m].blend == SL_WA_BLEND_CUTOUT) return mi;
    }
    return a->mesh_count;
}

int sl_wa_mesh_deferred(const struct sl_wa_asset *a, unsigned mi, unsigned first_cut)
{
    unsigned m;
    if (a == NULL || mi >= a->mesh_count || mi < first_cut) return 0;
    m = a->meshes[mi].material;
    if (m >= a->material_count) return 0;
    return a->materials[m].blend == SL_WA_BLEND_CUTOUT
        || (a->materials[m].flags & SL_WA_MAT_DECAL) != 0u;
}

/* #90 the late translucent draw. See the header. */
int sl_wa_blend_late(unsigned flags, unsigned blend, int late_decal, int on)
{
    return on != 0 && blend == SL_WA_BLEND_ALPHA && (flags & SL_WA_MAT_PANORAMA) == 0u
        && ((flags & SL_WA_MAT_DECAL) == 0u || late_decal != 0);
}

/* D-026, the panorama shell's rules. See the header. */
int sl_wa_panorama_fades(unsigned flags, int on)
{
    return (flags & SL_WA_MAT_PANORAMA) != 0u && (flags & SL_WA_MAT_FOG) != 0u && on != 0;
}

static float pano_smooth(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);
    if (!(t > 0.0f)) return 0.0f;            /* also a NaN */
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

float sl_wa_panorama_fogged(float fogc)
{
    return pano_smooth(SL_WA_PANO_COVER, 1.0f, fogc);
}

float sl_wa_panorama_reach(float fogc)
{
    return pano_smooth(SL_WA_PANO_REACH, 1.0f, fogc);
}

float sl_wa_panorama_taper(float dphi, float inner, float outer)
{
    float a = fabsf(dphi);
    if (a <= inner) return 1.0f;
    if (!(outer > inner) || a >= outer) return 0.0f;
    return 1.0f - pano_smooth(inner, outer, a);
}

/* #75. See the header. */
unsigned sl_wa_near_class(const float *c)
{
    if (!(c[3] > 0.0f)) return SL_WA_NEAR_BEHIND;
    return (c[2] < -c[3]) ? SL_WA_NEAR_INSIDE : 0u;
}

unsigned sl_wa_near_cut_select(const unsigned char *cls, const unsigned *idx,
                               unsigned n, unsigned mode, unsigned *out)
{
    unsigned i, m = 0;
    n -= n % 3u;
    for (i = 0; i < n; i += 3u) {
        const unsigned a = cls[idx[i]], b = cls[idx[i + 1u]], c = cls[idx[i + 2u]];
        const unsigned any = a | b | c;
        const int front = !(a & SL_WA_NEAR_BEHIND) || !(b & SL_WA_NEAR_BEHIND)
                       || !(c & SL_WA_NEAR_BEHIND);
        const unsigned want = (mode & SL_WA_NEAR_EYE_ONLY) ? SL_WA_NEAR_BEHIND
                                                           : (SL_WA_NEAR_BEHIND | SL_WA_NEAR_INSIDE);
        if (front && (any & want)) {
            if (out != NULL) { out[m] = idx[i]; out[m + 1u] = idx[i + 1u]; out[m + 2u] = idx[i + 2u]; }
            m += 3u;
        }
    }
    return m;
}

/* #77. See the header. One plane per triangle, sorted by its offset, and a
 * window of equal offsets compared pairwise - so the work is bounded by the
 * triangles that share a plane, not by the item's size squared. */
struct wa_cop_tri {
    double d;                  /* plane offset, normal made positive on its dominant axis */
    double n[3];
    double lo[3], hi[3];
    double mag;                /* the largest coordinate magnitude of its vertices */
    unsigned t;                /* index position / 3 */
    unsigned mesh;
    int axis;                  /* dominant normal axis: the projection dropped */
    int flip;                  /* the authored normal points the other way */
    int xlu;                   /* #81 a translucent (ALPHA) mesh's: it may
                                * yield to an earlier surface, never hold one */
    int decal;                 /* #85 a decal-pass mesh's: pairs only with another */
};

static int wa_cop_cmp(const void *x, const void *y)
{
    const struct wa_cop_tri *a = (const struct wa_cop_tri *) x;
    const struct wa_cop_tri *b = (const struct wa_cop_tri *) y;
    if (a->d < b->d) return -1;
    if (a->d > b->d) return 1;
    return (a->t < b->t) ? -1 : (a->t > b->t) ? 1 : 0;
}

static void wa_cop_2d(const struct sl_wa_asset *a, unsigned t, int axis, double p[3][2])
{
    unsigned k;
    for (k = 0; k < 3u; k++) {
        const struct sl_wa_vertex *v = &a->vertices[a->indices[t * 3u + k]];
        const double q[3] = { v->x, v->y, v->z };
        p[k][0] = q[axis == 0 ? 1 : 0];
        p[k][1] = q[axis == 2 ? 1 : 2];
    }
}

/* See the header: the flat tolerance, plus the rounding of positions at the
 * plane's distance from the origin. */
double sl_wa_coplanar_eps(double mag1, double mag2)
{
    const double m = (fabs(mag1) > fabs(mag2)) ? fabs(mag1) : fabs(mag2);
    return SL_WA_COPLANAR_EPS + SL_WA_COPLANAR_REL * m;
}

/* Positive-area overlap of two triangles in one plane: no edge normal of
 * either separates them, or brings them closer than touching (eps). */
static int wa_cop_overlap(double p[3][2], double q[3][2], double eps)
{
    int s, e, k;
    for (s = 0; s < 2; s++) {
        double (*T)[2] = s ? q : p;
        for (e = 0; e < 3; e++) {
            const double ax = -(T[(e + 1) % 3][1] - T[e][1]);
            const double ay = T[(e + 1) % 3][0] - T[e][0];
            const double ln = sqrt(ax * ax + ay * ay);
            double pmin = 1e300, pmax = -1e300, qmin = 1e300, qmax = -1e300;
            if (ln < 1e-12) continue;
            for (k = 0; k < 3; k++) {
                const double a1 = (p[k][0] * ax + p[k][1] * ay) / ln;
                const double b1 = (q[k][0] * ax + q[k][1] * ay) / ln;
                if (a1 < pmin) pmin = a1;
                if (a1 > pmax) pmax = a1;
                if (b1 < qmin) qmin = b1;
                if (b1 > qmax) qmax = b1;
            }
            if ((pmax < qmax ? pmax : qmax) - (pmin > qmin ? pmin : qmin) <= eps)
                return 0;
        }
    }
    return 1;
}

/* Both rules walk the same pairs; `opp` NULL is exactly the yield rule. */
static unsigned wa_coplanar(const struct sl_wa_asset *a, unsigned char *yield,
                            unsigned char *opp, unsigned *nopp, int xlu_on)
{
    struct wa_cop_tri *tr;
    unsigned ntri, m, n = 0, i, j, k, out = 0;
    double magmax = 0.0;       /* the item's: the widest tolerance any pair can have */
    if (a == NULL || yield == NULL) return 0u;
    ntri = a->index_count / 3u;
    memset(yield, 0, ntri);
    if (opp != NULL) memset(opp, 0, ntri);
    if (ntri == 0u) return 0u;
    tr = (struct wa_cop_tri *) malloc((size_t) ntri * sizeof *tr);
    if (tr == NULL) return 0u;
    for (m = 0; m < a->mesh_count; m++) {
        const struct sl_wa_mesh *mh = &a->meshes[m];
        const struct sl_wa_material *mt;
        unsigned t, t0;
        if (mh->material >= a->material_count) continue;
        mt = &a->materials[mh->material];
        if (mt->blend == SL_WA_BLEND_ALPHA && !xlu_on) continue;
        if (mh->first_index % 3u != 0u || mh->first_index + mh->index_count > a->index_count)
            continue;
        t0 = mh->first_index / 3u;
        for (t = t0; t < t0 + mh->index_count / 3u; t++) {
            const unsigned *ix = &a->indices[t * 3u];
            const struct sl_wa_vertex *A, *B, *C;
            double e1[3], e2[3], nn[3], ln;
            struct wa_cop_tri *r = &tr[n];
            int ax;
            if (ix[0] >= a->vertex_count || ix[1] >= a->vertex_count
                || ix[2] >= a->vertex_count) continue;
            A = &a->vertices[ix[0]]; B = &a->vertices[ix[1]]; C = &a->vertices[ix[2]];
            e1[0] = (double) B->x - A->x; e1[1] = (double) B->y - A->y; e1[2] = (double) B->z - A->z;
            e2[0] = (double) C->x - A->x; e2[1] = (double) C->y - A->y; e2[2] = (double) C->z - A->z;
            nn[0] = e1[1] * e2[2] - e1[2] * e2[1];
            nn[1] = e1[2] * e2[0] - e1[0] * e2[2];
            nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
            ln = sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            if (!(ln > 1e-6)) continue;                    /* degenerate */
            ax = (fabs(nn[0]) >= fabs(nn[1]) && fabs(nn[0]) >= fabs(nn[2])) ? 0
               : (fabs(nn[1]) >= fabs(nn[2])) ? 1 : 2;
            r->flip = (nn[ax] < 0.0);
            if (r->flip) ln = -ln;
            for (k = 0; k < 3u; k++) r->n[k] = nn[k] / ln;
            r->d = r->n[0] * A->x + r->n[1] * A->y + r->n[2] * A->z;
            r->lo[0] = r->hi[0] = A->x; r->lo[1] = r->hi[1] = A->y; r->lo[2] = r->hi[2] = A->z;
            for (k = 0; k < 3u; k++) {
                const double c1 = (k == 0) ? B->x : (k == 1) ? B->y : B->z;
                const double c2 = (k == 0) ? C->x : (k == 1) ? C->y : C->z;
                if (c1 < r->lo[k]) r->lo[k] = c1;
                if (c1 > r->hi[k]) r->hi[k] = c1;
                if (c2 < r->lo[k]) r->lo[k] = c2;
                if (c2 > r->hi[k]) r->hi[k] = c2;
            }
            r->mag = 0.0;
            for (k = 0; k < 3u; k++) {
                if (fabs(r->lo[k]) > r->mag) r->mag = fabs(r->lo[k]);
                if (fabs(r->hi[k]) > r->mag) r->mag = fabs(r->hi[k]);
            }
            if (r->mag > magmax) magmax = r->mag;
            r->t = t;
            r->mesh = m;
            r->axis = ax;
            r->xlu = (mt->blend == SL_WA_BLEND_ALPHA);
            r->decal = (mt->flags & SL_WA_MAT_DECAL) ? 1 : 0;
            n++;
        }
    }
    qsort(tr, n, sizeof *tr, wa_cop_cmp);
    for (i = 0; i < n; i++) {
        for (j = i + 1u; j < n && tr[j].d - tr[i].d <= sl_wa_coplanar_eps(magmax, magmax); j++) {
            const struct wa_cop_tri *p = &tr[i], *q = &tr[j];
            const struct wa_cop_tri *late;
            const double eps = sl_wa_coplanar_eps(p->mag, q->mag);
            if (q->d - p->d > eps) continue;
            double P[3][2], Q[3][2];
            int opposed;
            /* One mesh is one draw in the item's own order, so the ORDER rule
             * has nothing to say inside it; two sides of one plate authored
             * in one mesh are still two sides (#80, Surface's sniper tower:
             * the platform's top and underside, mesh 5). */
            if (p->mesh == q->mesh && !(opp != NULL && p->flip != q->flip)) continue;
            /* #85 a decal and the surface it lies on are the decal pass's
             * (its offset and pull); two decal-pass surfaces in one plane are
             * pulled alike, so between them the order rule decides, and only
             * the order rule: they are never taken as two sides of a wall */
            if (p->decal != q->decal) continue;
            if (p->decal && p->mesh == q->mesh) continue;
            /* #81 a translucent surface holds no pixel (it writes no depth), so
             * it only ever yields, to an earlier depth-writing one; it is never
             * a side of a wall */
            if (p->xlu || q->xlu) {
                const struct wa_cop_tri *early = (p->mesh > q->mesh) ? q : p;
                if (p->mesh == q->mesh || early->xlu) continue;
            }
            if (p->n[0] * q->n[0] + p->n[1] * q->n[1] + p->n[2] * q->n[2] < SL_WA_COPLANAR_COS)
                continue;
            for (k = 0; k < 3u; k++)
                if (p->lo[k] > q->hi[k] + eps || q->lo[k] > p->hi[k] + eps) break;
            if (k < 3u) continue;
            late = (p->mesh > q->mesh) ? p : q;
            opposed = (opp != NULL && p->flip != q->flip && !p->xlu && !q->xlu && !p->decal);
            if (yield[late->t] && !(opposed && !(opp[p->t] && opp[q->t]))) continue;
            wa_cop_2d(a, p->t, p->axis, P);
            wa_cop_2d(a, q->t, p->axis, Q);
            if (!wa_cop_overlap(P, Q, eps)) continue;
            if (p->mesh != q->mesh && !yield[late->t]) { yield[late->t] = 1; out++; }
            if (opposed) {
                if (!opp[p->t]) { opp[p->t] = 1; (*nopp)++; }
                if (!opp[q->t]) { opp[q->t] = 1; (*nopp)++; }
            }
        }
    }
    free(tr);
    return out;
}

unsigned sl_wa_coplanar_yield(const struct sl_wa_asset *a, unsigned char *yield)
{
    return wa_coplanar(a, yield, NULL, NULL, 1);
}

unsigned sl_wa_coplanar_opposed(const struct sl_wa_asset *a, unsigned char *yield,
                                unsigned char *opp)
{
    return sl_wa_coplanar_opposed_ex(a, yield, opp, 1);
}

unsigned sl_wa_coplanar_opposed_ex(const struct sl_wa_asset *a, unsigned char *yield,
                                   unsigned char *opp, int translucent)
{
    unsigned nopp = 0;
    if (opp == NULL) return 0u;
    (void) wa_coplanar(a, yield, opp, &nopp, translucent);
    return nopp;
}

/* One triangle's plane record, as wa_coplanar builds it; 0 if degenerate or
 * out of range. */
static int wa_cop_fill(const struct sl_wa_asset *a, unsigned t, struct wa_cop_tri *r)
{
    const unsigned *ix = &a->indices[t * 3u];
    const struct sl_wa_vertex *A, *B, *C;
    double e1[3], e2[3], nn[3], ln;
    unsigned k;
    if (ix[0] >= a->vertex_count || ix[1] >= a->vertex_count || ix[2] >= a->vertex_count)
        return 0;
    A = &a->vertices[ix[0]]; B = &a->vertices[ix[1]]; C = &a->vertices[ix[2]];
    e1[0] = (double) B->x - A->x; e1[1] = (double) B->y - A->y; e1[2] = (double) B->z - A->z;
    e2[0] = (double) C->x - A->x; e2[1] = (double) C->y - A->y; e2[2] = (double) C->z - A->z;
    nn[0] = e1[1] * e2[2] - e1[2] * e2[1];
    nn[1] = e1[2] * e2[0] - e1[0] * e2[2];
    nn[2] = e1[0] * e2[1] - e1[1] * e2[0];
    ln = sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
    if (!(ln > 1e-6)) return 0;
    r->axis = (fabs(nn[0]) >= fabs(nn[1]) && fabs(nn[0]) >= fabs(nn[2])) ? 0
            : (fabs(nn[1]) >= fabs(nn[2])) ? 1 : 2;
    r->flip = (nn[r->axis] < 0.0);
    if (r->flip) ln = -ln;
    for (k = 0; k < 3u; k++) r->n[k] = nn[k] / ln;
    r->d = r->n[0] * A->x + r->n[1] * A->y + r->n[2] * A->z;
    r->mag = 0.0;
    for (k = 0; k < 3u; k++) {
        const double c0 = (k == 0) ? A->x : (k == 1) ? A->y : A->z;
        const double c1 = (k == 0) ? B->x : (k == 1) ? B->y : B->z;
        const double c2 = (k == 0) ? C->x : (k == 1) ? C->y : C->z;
        r->lo[k] = c0 < c1 ? (c0 < c2 ? c0 : c2) : (c1 < c2 ? c1 : c2);
        r->hi[k] = c0 > c1 ? (c0 > c2 ? c0 : c2) : (c1 > c2 ? c1 : c2);
        if (fabs(r->lo[k]) > r->mag) r->mag = fabs(r->lo[k]);
        if (fabs(r->hi[k]) > r->mag) r->mag = fabs(r->hi[k]);
    }
    r->t = t;
    return 1;
}

/* Of decal triangle p and another plane record q: -1 when q's offset lies
 * farther than the pull (plus the widest tolerance, `reach`) - the walk
 * stops; 1 when q is a surface outside the decal pass, parallel to p, off
 * p's plane by more than the coplanar tolerance and at most the pull, and
 * overlapping p in p's plane; 0 otherwise. With `lies_on` (#85,
 * sl_wa_decal_needs_offset) the 1 is for q of another mesh, parallel and
 * overlapping, outside the pass within the pull (a tie included) or in the
 * pass within the coplanar tolerance. */
static int wa_decal_in_front(const struct sl_wa_asset *a, const struct wa_cop_tri *p,
                             const struct wa_cop_tri *q, double reach, int lies_on)
{
    const double gap = fabs(q->d - p->d);
    const double eps = sl_wa_coplanar_eps(p->mag, q->mag);
    double P[3][2], Q[3][2];
    unsigned k;
    if (gap > reach) return -1;
    if (lies_on) {
        /* sl_wa_decal_needs_offset: another mesh's surface it lies on - one
         * outside the pass within the pull, or a decal-pass one in its plane */
        if (q->mesh == p->mesh || gap > (double) SL_WA_DECAL_PULL + eps || (q->decal && gap > eps))
            return 0;
    } else if (q->decal || gap <= eps || gap > (double) SL_WA_DECAL_PULL + eps) {
        return 0;
    }
    if (p->n[0] * q->n[0] + p->n[1] * q->n[1] + p->n[2] * q->n[2] < SL_WA_COPLANAR_COS)
        return 0;
    for (k = 0; k < 3u; k++)
        if (k != (unsigned) p->axis && (p->lo[k] > q->hi[k] + eps || q->lo[k] > p->hi[k] + eps))
            return 0;
    wa_cop_2d(a, p->t, p->axis, P);
    wa_cop_2d(a, q->t, p->axis, Q);
    return wa_cop_overlap(P, Q, eps) ? 1 : 0;
}

/* #85. See the header. Plane records sorted by offset, as wa_coplanar: only
 * a window of offsets within the pull of a decal triangle's is compared.
 * `lies_on` asks sl_wa_decal_needs_offset's question instead of the pull's. */
static unsigned wa_decal_mark(const struct sl_wa_asset *a, unsigned char *need, int lies_on)
{
    struct wa_cop_tri *tr;
    unsigned ntri, m, n = 0, i, out = 0;
    double magmax = 0.0, reach;
    if (a == NULL || need == NULL) return 0u;
    ntri = a->index_count / 3u;
    memset(need, 0, ntri);
    if (ntri == 0u) return 0u;
    tr = (struct wa_cop_tri *) malloc((size_t) ntri * sizeof *tr);
    if (tr == NULL) return 0u;
    for (m = 0; m < a->mesh_count; m++) {
        const struct sl_wa_mesh *mh = &a->meshes[m];
        const struct sl_wa_material *mt;
        unsigned t, t0;
        if (mh->material >= a->material_count) continue;
        mt = &a->materials[mh->material];
        if (mh->first_index % 3u != 0u || mh->first_index + mh->index_count > a->index_count)
            continue;
        /* a decal triangle, or a surface that writes depth: a translucent
         * surface outside the decal pass hides nothing behind it */
        if (!(mt->flags & SL_WA_MAT_DECAL) && mt->blend == SL_WA_BLEND_ALPHA) continue;
        t0 = mh->first_index / 3u;
        for (t = t0; t < t0 + mh->index_count / 3u; t++) {
            if (!wa_cop_fill(a, t, &tr[n])) continue;
            tr[n].mesh = m;
            tr[n].decal = (mt->flags & SL_WA_MAT_DECAL) ? 1 : 0;
            tr[n].xlu = 0;
            if (tr[n].mag > magmax) magmax = tr[n].mag;
            n++;
        }
    }
    qsort(tr, n, sizeof *tr, wa_cop_cmp);
    reach = (double) SL_WA_DECAL_PULL + sl_wa_coplanar_eps(magmax, magmax);
    for (i = 0; i < n; i++) {
        unsigned j;
        if (!tr[i].decal) continue;
        /* the window below this offset, then the one above it */
        for (j = i; j-- > 0u && !need[tr[i].t]; ) {
            const int r = wa_decal_in_front(a, &tr[i], &tr[j], reach, lies_on);
            if (r < 0) break;
            if (r > 0) { need[tr[i].t] = 1; out++; }
        }
        for (j = i + 1u; j < n && !need[tr[i].t]; j++) {
            const int r = wa_decal_in_front(a, &tr[i], &tr[j], reach, lies_on);
            if (r < 0) break;
            if (r > 0) { need[tr[i].t] = 1; out++; }
        }
    }
    free(tr);
    return out;
}

unsigned sl_wa_decal_needs_pull(const struct sl_wa_asset *a, unsigned char *need)
{
    return wa_decal_mark(a, need, 0);
}

/* #85. See the header. */
unsigned sl_wa_decal_needs_offset(const struct sl_wa_asset *a, unsigned char *need)
{
    return wa_decal_mark(a, need, 1);
}

/* #90 the pieces of a decal mesh: its triangles joined through a shared
 * corner (union-find over the corners sorted by position; corners within the
 * coplanar tolerance are one). */
struct wa_corner { float x, y, z; unsigned tri; };

static int wa_corner_cmp(const void *p, const void *q)
{
    const struct wa_corner *a = (const struct wa_corner *) p, *b = (const struct wa_corner *) q;
    if (a->x != b->x) return (a->x < b->x) ? -1 : 1;
    if (a->y != b->y) return (a->y < b->y) ? -1 : 1;
    if (a->z != b->z) return (a->z < b->z) ? -1 : 1;
    return (a->tri < b->tri) ? -1 : (a->tri > b->tri) ? 1 : 0;
}

static unsigned wa_uf_find(unsigned *par, unsigned x)
{
    while (par[x] != x) { par[x] = par[par[x]]; x = par[x]; }
    return x;
}

/* #90 spread the per-triangle answers of sl_wa_decal_late over mesh m (a
 * translucent decal-pass mesh; any other is left alone): a lone triangle
 * makes the whole mesh late (SL_WA_DECAL_LATE_LONE on every triangle); a
 * triangle under a translucent surface makes its whole piece late
 * (SL_WA_DECAL_LATE_XLU on every triangle of the piece). When the piece walk
 * cannot allocate, the XLU answer spreads over the whole mesh. */
static void wa_decal_late_spread(const struct sl_wa_asset *a, unsigned m, unsigned char *late)
{
    const struct sl_wa_mesh *mh = &a->meshes[m];
    const struct sl_wa_material *mt;
    struct wa_corner *c;
    unsigned *par;
    unsigned char *acc;
    unsigned t0, n, i, k, any = 0;
    if (mh->material >= a->material_count) return;
    mt = &a->materials[mh->material];
    if (!(mt->flags & SL_WA_MAT_DECAL) || mt->blend != SL_WA_BLEND_ALPHA
        || (mt->flags & SL_WA_MAT_PANORAMA)) return;
    if (mh->first_index % 3u != 0u || mh->first_index + mh->index_count > a->index_count) return;
    t0 = mh->first_index / 3u;
    n = mh->index_count / 3u;
    for (i = 0; i < n; i++) any |= late[t0 + i];
    if (any == 0u) return;
    if (any & SL_WA_DECAL_LATE_LONE) {
        for (i = 0; i < n; i++) late[t0 + i] |= SL_WA_DECAL_LATE_LONE;
        return;
    }
    c = (struct wa_corner *) malloc((size_t) n * 3u * sizeof *c);
    par = (unsigned *) malloc((size_t) n * sizeof *par);
    acc = (unsigned char *) malloc((size_t) n);
    if (c == NULL || par == NULL || acc == NULL) {
        for (i = 0; i < n; i++) late[t0 + i] |= SL_WA_DECAL_LATE_XLU;
        free(c); free(par); free(acc);
        return;
    }
    for (i = 0; i < n; i++) {
        par[i] = i;
        acc[i] = 0u;
        for (k = 0; k < 3u; k++) {
            const unsigned vi = a->indices[(t0 + i) * 3u + k];
            const struct sl_wa_vertex *v = &a->vertices[vi < a->vertex_count ? vi : 0u];
            c[i * 3u + k].x = v->x; c[i * 3u + k].y = v->y; c[i * 3u + k].z = v->z;
            c[i * 3u + k].tri = i;
        }
    }
    /* corners within the coplanar tolerance of each other are one corner:
     * the source writes a piece's faces with their own vertices, which differ
     * in the last bits (Dam's plank tops and sides, 2e-4 apart at 4000) */
    qsort(c, (size_t) n * 3u, sizeof *c, wa_corner_cmp);
    for (i = 0; i < n * 3u; i++) {
        const double mi = fabs((double) c[i].x) > fabs((double) c[i].z) ? fabs((double) c[i].x) : fabs((double) c[i].z);
        const double eps = sl_wa_coplanar_eps(mi > fabs((double) c[i].y) ? mi : fabs((double) c[i].y), 0.0);
        unsigned j;
        for (j = i + 1u; j < n * 3u && (double) c[j].x - (double) c[i].x <= eps; j++)
            if (fabs((double) c[j].y - c[i].y) <= eps && fabs((double) c[j].z - c[i].z) <= eps) {
                const unsigned r1 = wa_uf_find(par, c[i].tri), r2 = wa_uf_find(par, c[j].tri);
                if (r1 != r2) par[r1] = r2;
            }
    }
    for (i = 0; i < n; i++) acc[wa_uf_find(par, i)] |= late[t0 + i];
    for (i = 0; i < n; i++) late[t0 + i] |= acc[wa_uf_find(par, i)];
    free(c); free(par); free(acc);
}

/* #90. See the header. Plane records of the translucent decals, the
 * translucent surfaces outside the pass and every surface that writes depth,
 * sorted by offset as wa_coplanar's: a translucent decal triangle is compared
 * with the records whose offset lies within the near reach of its own. In a
 * record, `xlu` marks a translucent one and `decal` a decal-pass one. */
unsigned sl_wa_decal_late(const struct sl_wa_asset *a, unsigned char *late)
{
    struct wa_cop_tri *tr;
    unsigned ntri, m, n = 0, i, t, out = 0;
    double magmax = 0.0, reach;
    if (a == NULL || late == NULL) return 0u;
    ntri = a->index_count / 3u;
    if (ntri == 0u) return 0u;
    memset(late, 0, ntri);
    tr = (struct wa_cop_tri *) malloc((size_t) ntri * sizeof *tr);
    if (tr == NULL) return 0u;
    for (m = 0; m < a->mesh_count; m++) {
        const struct sl_wa_mesh *mh = &a->meshes[m];
        const struct sl_wa_material *mt;
        unsigned t0;
        if (mh->material >= a->material_count) continue;
        mt = &a->materials[mh->material];
        if ((mt->flags & SL_WA_MAT_PANORAMA) != 0u) continue;
        if (mh->first_index % 3u != 0u || mh->first_index + mh->index_count > a->index_count)
            continue;
        t0 = mh->first_index / 3u;
        for (t = t0; t < t0 + mh->index_count / 3u; t++) {
            if (!wa_cop_fill(a, t, &tr[n])) continue;
            tr[n].mesh = m;
            tr[n].decal = (mt->flags & SL_WA_MAT_DECAL) ? 1 : 0;
            tr[n].xlu = (mt->blend == SL_WA_BLEND_ALPHA) ? 1 : 0;
            if (tr[n].mag > magmax) magmax = tr[n].mag;
            n++;
        }
    }
    qsort(tr, n, sizeof *tr, wa_cop_cmp);
    reach = (double) SL_WA_DECAL_XLU_NEAR + sl_wa_coplanar_eps(magmax, magmax);
    for (i = 0; i < n; i++) {
        const struct wa_cop_tri *p = &tr[i];
        unsigned dir, j;
        int solid = 0, xlu = 0;
        if (!p->decal || !p->xlu) continue;
        for (dir = 0; dir < 2u && !(solid && xlu); dir++) {
            for (j = i; !(solid && xlu) && (dir == 0u ? j-- > 0u : ++j < n); ) {
                const struct wa_cop_tri *q = &tr[j];
                const double eps = sl_wa_coplanar_eps(p->mag, q->mag);
                const double gap = fabs(q->d - p->d);
                /* a translucent surface within the near reach; one that
                 * writes depth within the pull */
                const int surf_xlu = q->xlu && !q->decal && gap <= (double) SL_WA_DECAL_XLU_NEAR + eps;
                const int surf_solid = !q->xlu && gap <= (double) SL_WA_DECAL_PULL + eps;
                double P[3][2], Q[3][2];
                unsigned k;
                if (gap > reach) break;
                if (q->mesh == p->mesh) continue;
                if (!(surf_xlu && !xlu) && !(surf_solid && !solid))
                    continue;                                   /* nothing left to learn from it */
                if (p->n[0] * q->n[0] + p->n[1] * q->n[1] + p->n[2] * q->n[2] < SL_WA_COPLANAR_COS)
                    continue;
                for (k = 0; k < 3u; k++)
                    if (k != (unsigned) p->axis && (p->lo[k] > q->hi[k] + eps || q->lo[k] > p->hi[k] + eps))
                        break;
                if (k < 3u) continue;
                wa_cop_2d(a, p->t, p->axis, P);
                wa_cop_2d(a, q->t, p->axis, Q);
                if (!wa_cop_overlap(P, Q, eps)) continue;
                if (surf_xlu) xlu = 1;
                else          solid = 1;
            }
        }
        late[p->t] = (unsigned char) ((xlu ? SL_WA_DECAL_LATE_XLU : 0u)
                                      | (solid ? 0u : SL_WA_DECAL_LATE_LONE));
    }
    free(tr);
    /* the answers are a piece's and a mesh's, not a triangle's (see the header) */
    for (m = 0; m < a->mesh_count; m++)
        wa_decal_late_spread(a, m, late);
    for (t = 0; t < ntri; t++) if (late[t] != 0u) out++;
    return out;
}

/* #90. See the header. */
unsigned sl_wa_decal_mix(const struct sl_wa_asset *a, const unsigned char *late, unsigned mesh)
{
    const struct sl_wa_mesh *mh;
    unsigned t, mix = 0;
    if (a == NULL || mesh >= a->mesh_count) return 0u;
    mh = &a->meshes[mesh];
    if (mh->first_index % 3u != 0u || mh->first_index + mh->index_count > a->index_count)
        return SL_WA_DECAL_MIX_LATE;
    if (late == NULL) return SL_WA_DECAL_MIX_EARLY;
    for (t = mh->first_index / 3u; t < (mh->first_index + mh->index_count) / 3u; t++)
        mix |= (late[t] != 0u) ? SL_WA_DECAL_MIX_LATE : SL_WA_DECAL_MIX_EARLY;
    return mix;
}

/* #89 Do triangles s and t have the same three corners, in any order? */
static int wa_tri_same(const struct sl_wa_asset *a, unsigned s, unsigned t)
{
    unsigned i, j, hit = 0;
    for (i = 0; i < 3u; i++) {
        const struct sl_wa_vertex *p = &a->vertices[a->indices[s * 3u + i]];
        for (j = 0; j < 3u; j++) {
            const struct sl_wa_vertex *q = &a->vertices[a->indices[t * 3u + j]];
            if (p->x == q->x && p->y == q->y && p->z == q->z) { hit |= 1u << j; break; }
        }
        if (j == 3u) return 0;
    }
    return hit == 7u;
}

/* #89. See the header. Plane records as wa_decal_mark builds them, sorted by
 * offset; the first walk marks the yielding decal triangles that lie on a
 * surface outside the pass, the second the earlier decal triangles in their
 * plane that they yield to. */
unsigned sl_wa_decal_layers(const struct sl_wa_asset *a, const unsigned char *yield,
                            unsigned char *layer)
{
    struct wa_cop_tri *tr;
    unsigned ntri, m, n = 0, i, out = 0;
    double magmax = 0.0, reach;
    if (a == NULL || yield == NULL || layer == NULL) return 0u;
    ntri = a->index_count / 3u;
    memset(layer, 0, ntri);
    if (ntri == 0u) return 0u;
    tr = (struct wa_cop_tri *) malloc((size_t) ntri * sizeof *tr);
    if (tr == NULL) return 0u;
    for (m = 0; m < a->mesh_count; m++) {
        const struct sl_wa_mesh *mh = &a->meshes[m];
        const struct sl_wa_material *mt;
        unsigned t, t0;
        if (mh->material >= a->material_count) continue;
        mt = &a->materials[mh->material];
        if (mh->first_index % 3u != 0u || mh->first_index + mh->index_count > a->index_count)
            continue;
        /* a decal triangle, or a surface outside the pass that writes depth */
        if (!(mt->flags & SL_WA_MAT_DECAL) && mt->blend == SL_WA_BLEND_ALPHA) continue;
        t0 = mh->first_index / 3u;
        for (t = t0; t < t0 + mh->index_count / 3u; t++) {
            if (!wa_cop_fill(a, t, &tr[n])) continue;
            tr[n].mesh = m;
            tr[n].decal = (mt->flags & SL_WA_MAT_DECAL) ? 1 : 0;
            /* a translucent decal holds no pixel: never the one yielded to */
            tr[n].xlu = (mt->blend == SL_WA_BLEND_ALPHA) ? 1 : 0;
            if (tr[n].mag > magmax) magmax = tr[n].mag;
            n++;
        }
    }
    qsort(tr, n, sizeof *tr, wa_cop_cmp);
    reach = (double) SL_WA_DECAL_PULL + sl_wa_coplanar_eps(magmax, magmax);
    /* the yielding decal triangles lying on a surface outside the pass */
    for (i = 0; i < n; i++) {
        unsigned j;
        if (!tr[i].decal || !yield[tr[i].t]) continue;
        for (j = i; j-- > 0u && !layer[tr[i].t]; ) {
            if (tr[i].d - tr[j].d > reach) break;
            if (!tr[j].decal && wa_decal_in_front(a, &tr[i], &tr[j], reach, 1) > 0)
                layer[tr[i].t] = SL_WA_DECAL_KEEP;
        }
        for (j = i + 1u; j < n && !layer[tr[i].t]; j++) {
            if (tr[j].d - tr[i].d > reach) break;
            if (!tr[j].decal && wa_decal_in_front(a, &tr[i], &tr[j], reach, 1) > 0)
                layer[tr[i].t] = SL_WA_DECAL_KEEP;
        }
        /* a triangle repeating an earlier decal triangle corner for corner is
         * a two-layer draw's layer over its own base (prepare.py emits the
         * layer on the base's triangles): one draw of the source, not a decal
         * stacked on another, so it is left as #85 draws it */
        if (layer[tr[i].t]) {
            unsigned dir;
            for (dir = 0; dir < 2u && layer[tr[i].t]; dir++) {
                for (j = i; layer[tr[i].t] && (dir == 0u ? j-- > 0u : ++j < n); ) {
                    if (fabs(tr[j].d - tr[i].d) > reach) break;
                    if (tr[j].decal && tr[j].mesh < tr[i].mesh
                        && fabs(tr[j].d - tr[i].d) <= sl_wa_coplanar_eps(tr[i].mag, tr[j].mag)
                        && wa_tri_same(a, tr[i].t, tr[j].t))
                        layer[tr[i].t] = 0;
                }
            }
        }
        if (layer[tr[i].t]) out++;
    }
    /* the earlier decal triangles those yield to: in the plane, overlapping,
     * of an earlier mesh, writing depth (the order rule's earlier member), not
     * yielding themselves */
    for (i = 0; i < n; i++) {
        unsigned j, dir;
        if (!(layer[tr[i].t] & SL_WA_DECAL_KEEP)) continue;
        for (dir = 0; dir < 2u; dir++) {
            for (j = i; dir == 0u ? j-- > 0u : ++j < n; ) {
                const struct wa_cop_tri *p = &tr[i], *q = &tr[j];
                const double eps = sl_wa_coplanar_eps(p->mag, q->mag);
                double P[3][2], Q[3][2];
                unsigned k;
                if (fabs(q->d - p->d) > reach) break;
                if (!q->decal || q->xlu || q->mesh >= p->mesh || yield[q->t] || fabs(q->d - p->d) > eps)
                    continue;
                if (p->n[0] * q->n[0] + p->n[1] * q->n[1] + p->n[2] * q->n[2] < SL_WA_COPLANAR_COS)
                    continue;
                for (k = 0; k < 3u; k++)
                    if (p->lo[k] > q->hi[k] + eps || q->lo[k] > p->hi[k] + eps) break;
                if (k < 3u) continue;
                wa_cop_2d(a, p->t, p->axis, P);
                wa_cop_2d(a, q->t, p->axis, Q);
                if (wa_cop_overlap(P, Q, eps)) layer[q->t] |= SL_WA_DECAL_OVER;
            }
        }
    }
    free(tr);
    return out;
}

int sl_wa_tri_faces(const struct sl_wa_vertex *A, const struct sl_wa_vertex *B,
                    const struct sl_wa_vertex *C, const float *eye)
{
    const double e1[3] = { (double) B->x - A->x, (double) B->y - A->y, (double) B->z - A->z };
    const double e2[3] = { (double) C->x - A->x, (double) C->y - A->y, (double) C->z - A->z };
    const double n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                          e1[0] * e2[1] - e1[1] * e2[0] };
    return n[0] * ((double) eye[0] - A->x) + n[1] * ((double) eye[1] - A->y)
         + n[2] * ((double) eye[2] - A->z) > 0.0;
}

int sl_wa_cull_drops(unsigned mat_flags, int on, int eye_ok, int faces)
{
    return (mat_flags & SL_WA_MAT_CULL_BACK) != 0u && on != 0 && eye_ok != 0 && faces == 0;
}

/* The controller's tile-1 origin: the integer part offset and wrapped at 256
 * quarter-texels (the cartridge's `((s32) p + off) & 0xFF`), the fraction the
 * native controller keeps for B-109 put back on. */
static float wa_water_tile1(float p, int off)
{
    const int ip = (int) p;
    return (float) ((ip + off) & 0xFF) + (p - (float) ip);
}

void sl_wa_water_state(float ps, float pt, float pw, struct sl_wa_water *w)
{
    const float unit = 4.0f * 64.0f;          /* quarter-texels per item unit */
    const unsigned byte = (unsigned) ((sinf(pw) * 127.0f) + 128.0f) & 0xFFu;
    w->du0 = -ps / unit;
    w->dv0 =  pt / unit;
    w->du1 = -wa_water_tile1(ps, 90) / unit;
    w->dv1 =  wa_water_tile1(pt, 150) / unit;
    w->frac = (float) byte / 255.0f;
}

unsigned sl_wa_light_mark(const struct sl_wa_vertex *v, unsigned nv, const unsigned *idx,
                          unsigned ni, const float (*pts)[3], unsigned npts, float reach,
                          unsigned char *mark)
{
    unsigned k, s, t, ntri = 0;
    /* Bit 0: within reach of a point; bit 1: a vertex of a fixture triangle. */
    for (k = 0; k < nv; k++) {
        mark[k] = 0;
        for (s = 0; s < npts; s++) {
            const float d = fabsf(v[k].x - pts[s][0]) + fabsf(v[k].y - pts[s][1])
                          + fabsf(v[k].z - pts[s][2]);
            if (d < reach) { mark[k] = 1; break; }
        }
    }
    ni -= ni % 3u;
    for (t = 0; t < ni; t += 3u)
        if ((mark[idx[t]] & 1u) && (mark[idx[t + 1u]] & 1u) && (mark[idx[t + 2u]] & 1u)) {
            mark[idx[t]] |= 2u; mark[idx[t + 1u]] |= 2u; mark[idx[t + 2u]] |= 2u;
            ntri++;
        }
    for (k = 0; k < nv; k++) mark[k] = (unsigned char) ((mark[k] & 2u) ? 1 : 0);
    return ntri;
}

void sl_wa_fold_eye(const float *wv, const float *ro, float vis, float *out)
{
    int j;
    for (j = 0; j < 3; j++)
        out[j] = (-(wv[12] * wv[j * 4 + 0] + wv[13] * wv[j * 4 + 1] + wv[14] * wv[j * 4 + 2])
                  - ro[j]) * vis;
}

int sl_wa_redraw_quad_ok(const float *p, const float *q, const float *r, const float *s)
{
    /* Closest approach of the lines p + t (r - p) and q + u (s - q). */
    double d1[3], d2[3], w[3], a = 0, b = 0, c = 0, d = 0, e = 0, den, t, u, g = 0, m;
    int j;
    for (j = 0; j < 3; j++) {
        d1[j] = (double) r[j] - p[j];
        d2[j] = (double) s[j] - q[j];
        w[j]  = (double) p[j] - q[j];
        a += d1[j] * d1[j]; b += d1[j] * d2[j]; c += d2[j] * d2[j];
        d += d1[j] * w[j];  e += d2[j] * w[j];
    }
    den = a * c - b * b;
    if (!(a > 0.0) || !(c > 0.0) || !(den > 1e-9 * a * c)) return 0;   /* parallel or degenerate */
    t = (b * e - c * d) / den;
    u = (a * e - b * d) / den;
    if (t < SL_WA_REDRAW_QUAD_END || t > 1.0 - SL_WA_REDRAW_QUAD_END) return 0;
    if (u < SL_WA_REDRAW_QUAD_END || u > 1.0 - SL_WA_REDRAW_QUAD_END) return 0;
    for (j = 0; j < 3; j++) {
        double k = w[j] + t * d1[j] - u * d2[j];
        g += k * k;
    }
    m = a < c ? a : c;
    return g <= (double) SL_WA_REDRAW_QUAD_GAP * SL_WA_REDRAW_QUAD_GAP * m;
}

float sl_wa_panorama_coord(float tan_e, float tan_top, float fade)
{
    const float deg = 3.14159265358979f / 180.0f;
    float top, hi, span;
    if (!(fade > 0.0f)) fade = SL_WA_PANO_FADE;
    if (!(tan_top > 0.0f)) tan_top = 0.0f;
    top = atanf(tan_top);
    hi = top + fade * deg;
    if (hi > 89.0f * deg) hi = 89.0f * deg;
    span = tanf(hi) - tan_top;
    if (!(span > 1e-6f)) return (tan_e > tan_top) ? 1.0f : 0.0f;
    return (tan_e - tan_top) / span;
}

float sl_wa_panorama_alpha(float s)
{
    return 1.0f - pano_smooth(0.0f, 1.0f, s);
}

/* D-023, the game's cloud plane. See the header. */
int sl_wa_cloud_coord(const float d[3], const float eye[3], float height,
                      float phase, unsigned img_w, unsigned img_h, float out[3])
{
    const double ps = 32.0 * (double) img_w / (double) SL_WA_CLOUD_TILES;
    const double pt = 32.0 * (double) img_h / (double) SL_WA_CLOUD_TILES;
    double hc, n, s0, t0;

    out[0] = out[1] = out[2] = 0.0f;
    if (img_w == 0u || img_h == 0u) return 0;
    hc = (double) height - (double) eye[1];
    n = sqrt((double) d[0] * d[0] + (double) d[1] * d[1] + (double) d[2] * d[2]);
    if (!(d[1] > 0.0f) || !(hc > 0.0) || !(n > 0.0)) return 0;
    s0 = fmod(0.1 * (double) eye[0], ps);
    if (s0 < 0.0) s0 += ps;
    t0 = fmod(0.1 * (double) eye[2] + (double) phase, pt);
    if (t0 < 0.0) t0 += pt;
    out[0] = (float) ((s0 * d[1] + 0.1 * d[0] * hc) / (ps * n));
    out[1] = (float) ((t0 * d[1] + 0.1 * d[2] * hc) / (pt * n));
    out[2] = (float) (d[1] / n);
    return 1;
}

/* The aperture rule. See the header for the measurement that made it one.
 *
 * `found` is taken and deliberately not used: naming it in the signature is
 * what lets the test drive the real portal windows through this and require
 * the same answer from each. */
void sl_wa_sky_aperture(const int viewport[4], const int found[4], int box[4])
{
    int i;
    (void) found;
    if (viewport == (const int *) 0 || box == (int *) 0) return;
    for (i = 0; i < 4; i++) box[i] = viewport[i];
}

/* The fog contract. See the header for where its inputs come from and why
 * they are not the state standing at the seam. */
int sl_wa_fog_band(int seen, int fm, int fo, unsigned age,
                   float *start, float *end)
{
    /* Three independent reasons there is no level fog to receive: the list
     * has never written a fog position, the one it wrote has a zero
     * multiplier (a band with no extent - nothing to interpolate over), or
     * the last one is older than the frame before this. The age test is what
     * stops a level that STOPS fogging from leaving the replacement fogged
     * on a value nothing is issuing any more; one frame of slack is there
     * because the seam runs early and may precede this frame's own. */
    if (!seen || fm == 0 || age > 1u) return 0;
    /* What the pair means, which is the only thing this adds to it. The RSP
     * computes alpha = ndcz * fm + fo clamped to 0..255, so the band starts
     * where that reaches 0 and saturates where it reaches 255. */
    if (start != (float *) 0) *start = -(float) fo / (float) fm;
    if (end   != (float *) 0) *end   = (255.0f - (float) fo) / (float) fm;
    return 1;
}

/* The fog DESTINATION rule. See the header for the identity it rests on - the
 * game writes one environment RGB to both its backdrop fill and its fog
 * colour - and for what applying the level's band to a replacement measured. */
int sl_wa_fog_level_applies(int background_replaced)
{
    /* The level's band is a fade INTO the level's own backdrop. A resolved
     * replacement takes that backdrop off screen, and brings its own
     * distance shading. */
    return background_replaced ? 0 : 1;
}

/* The per-object atmosphere. See the header for the measurement: one row fogs
 * the backdrop and the objects in front of it, or the level's own fog fogs
 * both - never one of each. */
const struct sl_wa_env *sl_wa_object_fog_env(int background_replaced,
                                             const struct sl_wa_env *row)
{
    if (sl_wa_fog_level_applies(background_replaced))
        return (const struct sl_wa_env *) 0;
    return row;
}

/* The backdrop under a replacement. See the header for the measurement. */
int sl_wa_backdrop_black(int background_drawn, int sky_drew, int flat_backdrop)
{
    return (background_drawn && !sky_drew && flat_backdrop) ? 1 : 0;
}

/* #81 a fogless stage keeps the cartridge's range and backdrop. See the
 * header. */
int sl_wa_fogless_range(int background_drawn, int env_id)
{
    return (background_drawn && env_id < 0) ? 1 : 0;
}

/* The game's far plane from the matrix in force. See the header. */
int sl_wa_far_plane(const float *C, float *zfar)
{
    int bi = 0, q;
    double k, m, n, f;
    for (q = 1; q < 3; q++)
        if (fabs((double) C[q * 4 + 3]) > fabs((double) C[bi * 4 + 3])) bi = q;
    if (!(fabs((double) C[bi * 4 + 3]) > 1e-12)) return 0;
    k = -(double) C[bi * 4 + 2] / (double) C[bi * 4 + 3];
    m = (double) C[14] + k * (double) C[15];
    if (!(fabs(k - 1.0) > 1e-12) || !(fabs(k + 1.0) > 1e-12)) return 0;
    n = m / (k - 1.0);
    f = m / (k + 1.0);
    if (!(n > 0.0) || !(f > n) || !(f < 1e30)) return 0;
    *zfar = (float) f;
    return 1;
}

/* The first range's depth. See the header. */
void sl_wa_far_range(float from, float to, float *k1, float *k2)
{
    *k1 = (to + from) / (to - from);
    *k2 = 2.0f * to * from / (to - from);
}

/* #78 THE NATIVE FILL's grid. See the header. */
struct fill_cell { unsigned long long key; unsigned start, count; };
struct fill_pair { unsigned long long key; unsigned tri; };
static const struct sl_wa_asset *g_fill_asset;
static float g_fill_cell;
static struct fill_cell *g_fill_cells;      /* open addressing, power of two */
static unsigned g_fill_ncells;
static unsigned *g_fill_runs;               /* triangle indices, by cell */
static unsigned g_fill_nruns, g_fill_ntris, g_fill_nlarge;
static unsigned *g_fill_large;
/* Each grid triangle's box (lo x y z, hi x y z), widened by g_fill_slack, so
 * the distance query can skip a triangle that cannot be nearer than the best
 * found so far without computing its closest point (fill_box_far). */
static float *g_fill_box;
static float *g_fill_rbox;                /* the same boxes in g_fill_runs order */
static float *g_fill_soa;                 /* ... again, as six arrays of g_fill_nruns (fill_run_scan) */
static float g_fill_slack;

/* A cell's key: its level in the top bits, twenty bits per axis. */
static unsigned long long fill_key(unsigned level, int ix, int iy, int iz)
{
    unsigned long long x = (unsigned long long) (unsigned) (ix + (1 << 19)) & 0xfffffull;
    unsigned long long y = (unsigned long long) (unsigned) (iy + (1 << 19)) & 0xfffffull;
    unsigned long long z = (unsigned long long) (unsigned) (iz + (1 << 19)) & 0xfffffull;
    return ((unsigned long long) level << 60) | (x << 40) | (y << 20) | z;
}

/* The cell size's inverse at a level. */
static float fill_level_inv(float inv, unsigned level)
{
    while (level--) inv /= (float) SL_WA_FILL_COARSEN;
    return inv;
}

static unsigned fill_hash(unsigned long long k)
{
    k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33;
    return (unsigned) k;
}

static int fill_cmp(const void *x, const void *y)
{
    const struct fill_pair *a = (const struct fill_pair *) x, *b = (const struct fill_pair *) y;
    if (a->key != b->key) return a->key < b->key ? -1 : 1;
    return a->tri < b->tri ? -1 : a->tri > b->tri;
}

static void fill_release(void)
{
    free(g_fill_cells); free(g_fill_runs); free(g_fill_large); free(g_fill_box); free(g_fill_rbox);
    free(g_fill_soa);
    g_fill_cells = NULL; g_fill_runs = NULL; g_fill_large = NULL; g_fill_box = NULL; g_fill_rbox = NULL;
    g_fill_soa = NULL;
    g_fill_ncells = g_fill_nruns = g_fill_ntris = g_fill_nlarge = 0;
    g_fill_asset = NULL; g_fill_cell = 0.0f;
}

/* The squared distance from p to triangle abc (the closest-point regions). */
static float fill_tri_d2(const float *p, const float *a, const float *b, const float *c)
{
    float ab[3], ac[3], ap[3], bp[3], cp[3], q[3];
    float d1, d2, d3, d4, d5, d6, vc, vb, va, v, w, den;
    int k;
    for (k = 0; k < 3; k++) { ab[k] = b[k] - a[k]; ac[k] = c[k] - a[k]; ap[k] = p[k] - a[k]; }
    d1 = ab[0] * ap[0] + ab[1] * ap[1] + ab[2] * ap[2];
    d2 = ac[0] * ap[0] + ac[1] * ap[1] + ac[2] * ap[2];
    if (d1 <= 0.0f && d2 <= 0.0f) { for (k = 0; k < 3; k++) q[k] = a[k]; goto done; }
    for (k = 0; k < 3; k++) bp[k] = p[k] - b[k];
    d3 = ab[0] * bp[0] + ab[1] * bp[1] + ab[2] * bp[2];
    d4 = ac[0] * bp[0] + ac[1] * bp[1] + ac[2] * bp[2];
    if (d3 >= 0.0f && d4 <= d3) { for (k = 0; k < 3; k++) q[k] = b[k]; goto done; }
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        v = (d1 - d3) != 0.0f ? d1 / (d1 - d3) : 0.0f;
        for (k = 0; k < 3; k++) q[k] = a[k] + v * ab[k];
        goto done;
    }
    for (k = 0; k < 3; k++) cp[k] = p[k] - c[k];
    d5 = ab[0] * cp[0] + ab[1] * cp[1] + ab[2] * cp[2];
    d6 = ac[0] * cp[0] + ac[1] * cp[1] + ac[2] * cp[2];
    if (d6 >= 0.0f && d5 <= d6) { for (k = 0; k < 3; k++) q[k] = c[k]; goto done; }
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        w = (d2 - d6) != 0.0f ? d2 / (d2 - d6) : 0.0f;
        for (k = 0; k < 3; k++) q[k] = a[k] + w * ac[k];
        goto done;
    }
    va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        den = (d4 - d3) + (d5 - d6);
        w = den != 0.0f ? (d4 - d3) / den : 0.0f;
        for (k = 0; k < 3; k++) q[k] = b[k] + w * (c[k] - b[k]);
        goto done;
    }
    den = va + vb + vc;
    if (den == 0.0f) { for (k = 0; k < 3; k++) q[k] = a[k]; goto done; }
    v = vb / den; w = vc / den;
    for (k = 0; k < 3; k++) q[k] = a[k] + ab[k] * v + ac[k] * w;
done:
    return (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1])
         + (p[2] - q[2]) * (p[2] - q[2]);
}

static int fill_tri_takes(const struct sl_wa_asset *a, unsigned mi)
{
    const struct sl_wa_mesh *mh = &a->meshes[mi];
    if (mh->material >= a->material_count) return 0;
    return a->materials[mh->material].blend != SL_WA_BLEND_ALPHA;
}

static void fill_tri_cells(const struct sl_wa_asset *a, unsigned t, float inv, int lo[3], int hi[3])
{
    int k, j;
    for (k = 0; k < 3; k++) { lo[k] = 1 << 30; hi[k] = -(1 << 30); }
    for (j = 0; j < 3; j++) {
        const struct sl_wa_vertex *v = &a->vertices[a->indices[3 * t + j]];
        float c[3];
        c[0] = v->x; c[1] = v->y; c[2] = v->z;
        for (k = 0; k < 3; k++) {
            int i = (int) floorf(c[k] * inv);
            if (i < lo[k]) lo[k] = i;
            if (i > hi[k]) hi[k] = i;
        }
    }
}

static const struct fill_cell *fill_find(unsigned long long key)
{
    unsigned h, i;
    if (g_fill_ncells == 0) return NULL;
    h = fill_hash(key) & (g_fill_ncells - 1);
    for (i = 0; i < g_fill_ncells; i++) {
        const struct fill_cell *c = &g_fill_cells[(h + i) & (g_fill_ncells - 1)];
        if (c->count == 0) return NULL;
        if (c->key == key) return c;
    }
    return NULL;
}

int sl_wa_fill_set(const struct sl_wa_asset *a, float cell)
{
    struct fill_pair *pairs = NULL;
    unsigned npairs = 0, ntris = 0, nlarge = 0, mi, t, i, pass, nrun;
    float inv;
    if (a == g_fill_asset && cell == g_fill_cell) return 0;
    fill_release();
    if (a == NULL || !(cell > 0.0f) || a->vertices == NULL || a->indices == NULL) return 1;
    inv = 1.0f / cell;
    for (pass = 0; pass < 2; pass++) {
        npairs = 0; ntris = 0; nlarge = 0;
        for (mi = 0; mi < a->mesh_count; mi++) {
            const struct sl_wa_mesh *mh = &a->meshes[mi];
            if (!fill_tri_takes(a, mi)) continue;
            if (mh->first_index + mh->index_count > a->index_count) continue;
            for (t = mh->first_index / 3u; t < (mh->first_index + mh->index_count) / 3u; t++) {
                int lo[3], hi[3], x, y, z;
                unsigned span = 0, k, lv;
                if (a->indices[3 * t] >= a->vertex_count || a->indices[3 * t + 1] >= a->vertex_count
                    || a->indices[3 * t + 2] >= a->vertex_count) continue;
                /* the first level the triangle fits: at most SL_WA_FILL_LARGE
                 * cells on every axis */
                for (lv = 0; lv < SL_WA_FILL_LEVELS; lv++) {
                    fill_tri_cells(a, t, fill_level_inv(inv, lv), lo, hi);
                    span = 0;
                    for (k = 0; k < 3; k++)
                        if ((unsigned) (hi[k] - lo[k] + 1) > span) span = (unsigned) (hi[k] - lo[k] + 1);
                    if (span <= SL_WA_FILL_LARGE) break;
                }
                if (lv == SL_WA_FILL_LEVELS) {
                    if (pass) g_fill_large[nlarge] = t;
                    nlarge++;
                    continue;
                }
                ntris++;
                for (x = lo[0]; x <= hi[0]; x++)
                    for (y = lo[1]; y <= hi[1]; y++)
                        for (z = lo[2]; z <= hi[2]; z++) {
                            if (pass) { pairs[npairs].key = fill_key(lv, x, y, z); pairs[npairs].tri = t; }
                            npairs++;
                        }
            }
        }
        if (pass == 0) {
            pairs = (struct fill_pair *) malloc((npairs ? npairs : 1u) * sizeof pairs[0]);
            g_fill_large = (unsigned *) malloc((nlarge ? nlarge : 1u) * sizeof g_fill_large[0]);
            if (pairs == NULL || g_fill_large == NULL) { free(pairs); fill_release(); return 1; }
        }
    }
    g_fill_nlarge = nlarge;
    g_fill_ntris = ntris;
    qsort(pairs, npairs, sizeof pairs[0], fill_cmp);
    nrun = 0;
    for (i = 0; i < npairs; i++)
        if (i == 0 || pairs[i].key != pairs[i - 1].key) nrun++;
    g_fill_runs = (unsigned *) malloc((npairs ? npairs : 1u) * sizeof g_fill_runs[0]);
    g_fill_ncells = 1;
    while (g_fill_ncells < 2u * nrun + 2u) g_fill_ncells <<= 1;
    g_fill_cells = (struct fill_cell *) calloc(g_fill_ncells, sizeof g_fill_cells[0]);
    if (g_fill_runs == NULL || g_fill_cells == NULL) { free(pairs); fill_release(); return 1; }
    for (i = 0; i < npairs; i++) {
        g_fill_runs[i] = pairs[i].tri;
        if (i == 0 || pairs[i].key != pairs[i - 1].key) {
            unsigned h = fill_hash(pairs[i].key) & (g_fill_ncells - 1);
            while (g_fill_cells[h].count != 0) h = (h + 1) & (g_fill_ncells - 1);
            g_fill_cells[h].key = pairs[i].key;
            g_fill_cells[h].start = i;
            g_fill_cells[h].count = 1;
        } else {
            unsigned h = fill_hash(pairs[i].key) & (g_fill_ncells - 1);
            while (g_fill_cells[h].key != pairs[i].key) h = (h + 1) & (g_fill_ncells - 1);
            g_fill_cells[h].count++;
        }
    }
    g_fill_nruns = npairs;
    free(pairs);
    /* the boxes: one per triangle of the item (fill_box_far); without them
     * every candidate is tested as before */
    g_fill_box = (float *) malloc((size_t) (a->index_count / 3u ? a->index_count / 3u : 1u) * 6u * sizeof *g_fill_box);
    if (g_fill_box != NULL) {
        float big = 0.0f;
        for (t = 0; t < a->index_count / 3u; t++) {
            float *b = &g_fill_box[t * 6u];
            unsigned j, k;
            b[0] = b[1] = b[2] = 3.0e38f; b[3] = b[4] = b[5] = -3.0e38f;
            for (j = 0; j < 3u; j++) {
                const unsigned vi = a->indices[3u * t + j];
                float c[3];
                if (vi >= a->vertex_count) continue;
                c[0] = a->vertices[vi].x; c[1] = a->vertices[vi].y; c[2] = a->vertices[vi].z;
                for (k = 0; k < 3u; k++) {
                    if (c[k] < b[k]) b[k] = c[k];
                    if (c[k] > b[3u + k]) b[3u + k] = c[k];
                    if (fabsf(c[k]) > big) big = fabsf(c[k]);
                }
            }
        }
        g_fill_slack = big * SL_WA_FILL_BOX_SLACK + 1.0f;
        for (t = 0; t < (a->index_count / 3u) * 6u; t++)
            g_fill_box[t] += (t % 6u < 3u) ? -g_fill_slack : g_fill_slack;
        /* a cell's candidates are read in run order, so their boxes are
         * laid out that way too: one sequential read per cell */
        g_fill_rbox = (float *) malloc((size_t) (npairs ? npairs : 1u) * 6u * sizeof *g_fill_rbox);
        if (g_fill_rbox != NULL)
            for (i = 0; i < npairs; i++)
                memcpy(&g_fill_rbox[i * 6u], &g_fill_box[g_fill_runs[i] * 6u], 6u * sizeof *g_fill_rbox);
        /* and component by component, so four boxes are held to the best at once */
        g_fill_soa = (g_fill_rbox != NULL)
                   ? (float *) malloc((size_t) (npairs ? npairs : 1u) * 6u * sizeof *g_fill_soa) : NULL;
        if (g_fill_soa != NULL)
            for (i = 0; i < npairs; i++) {
                unsigned k;
                for (k = 0; k < 6u; k++) g_fill_soa[k * npairs + i] = g_fill_rbox[i * 6u + k];
            }
    }
    g_fill_asset = a;
    g_fill_cell = cell;
    return 1;
}

/* `n`, when given, is a unit normal: only a triangle whose plane lies within
 * SL_WA_FILL_ALIGN of it counts (either facing). */
static float fill_tri_test(const struct sl_wa_asset *a, unsigned t, const float *p, float best,
                           const float *n)
{
    const struct sl_wa_vertex *v0 = &a->vertices[a->indices[3 * t]];
    const struct sl_wa_vertex *v1 = &a->vertices[a->indices[3 * t + 1]];
    const struct sl_wa_vertex *v2 = &a->vertices[a->indices[3 * t + 2]];
    float A[3], B[3], C[3], d2;
    A[0] = v0->x; A[1] = v0->y; A[2] = v0->z;
    B[0] = v1->x; B[1] = v1->y; B[2] = v1->z;
    C[0] = v2->x; C[1] = v2->y; C[2] = v2->z;
    if (n != NULL) {
        float u[3], w[3], m[3], len, dt;
        int k;
        for (k = 0; k < 3; k++) { u[k] = B[k] - A[k]; w[k] = C[k] - A[k]; }
        m[0] = u[1] * w[2] - u[2] * w[1];
        m[1] = u[2] * w[0] - u[0] * w[2];
        m[2] = u[0] * w[1] - u[1] * w[0];
        len = (float) sqrt((double) (m[0] * m[0] + m[1] * m[1] + m[2] * m[2]));
        if (!(len > 0.0f)) return best;
        dt = (m[0] * n[0] + m[1] * n[1] + m[2] * n[2]) / len;
        if (fabsf(dt) < SL_WA_FILL_ALIGN) return best;
    }
    d2 = fill_tri_d2(p, A, B, C);
    return d2 < best ? d2 : best;
}

/* THE BOX TEST. The query's cost is the open-hole trace's (sl_wa_fill_open):
 * measured on Statue under XBLA (room 2's pose, unpaced), about 930 traces a
 * frame, 24 steps each and some 100 candidate triangles a step, 84% of a
 * 62 ms frame in this query. Most candidates lie well outside the step's
 * reach, so each is first held to its box: 1 when triangle `b`'s widened box
 * lies farther from p than `best` (squared) allows, by a margin. The closest
 * point fill_tri_d2 computes lies on the triangle up to rounding, the box is
 * widened past that rounding (g_fill_slack), so a skipped triangle's squared
 * distance exceeds `best` and testing it could not have lowered it: the
 * query returns exactly what the exhaustive test returns.
 * SL_WA_FILL_BOXES=0 tests every candidate (the A/B for that identity). */
static int g_fill_boxes = -1;               /* the self-test sets it directly */
static int fill_boxes_on(void)
{
    if (g_fill_boxes < 0) { const char *v = getenv("SL_WA_FILL_BOXES"); g_fill_boxes = (v == NULL || *v != '0'); }
    return g_fill_boxes;
}

static int fill_box_far(const float *b, const float *p, float best)
{
    float d2 = 0.0f, g;
    int k;
    for (k = 0; k < 3; k++) {
        g = (p[k] < b[k]) ? b[k] - p[k] : (p[k] > b[3 + k]) ? p[k] - b[3 + k] : 0.0f;
        d2 += g * g;
    }
    return d2 > best * (1.0f + SL_WA_FILL_BOX_MARGIN);
}

/* (int) floorf(x), exactly, for the cell indices the grid holds (|x| far
 * below 2^31): the truncation, one lower for a negative non-integer. The
 * library floorf was 14% of the same frame once the boxes were in. */
static int fill_floor(float x)
{
    int i = (int) x;
    return ((float) i > x) ? i - 1 : i;
}

/* ONE CELL'S CANDIDATES, four boxes at a time. The same box test as
 * fill_box_far - per axis the gap max(lo - p, p - hi, 0), squared and summed
 * x, y, z in that order, held to best * (1 + SL_WA_FILL_BOX_MARGIN) - on four
 * candidates in one SSE2 operation each, in single precision as the scalar
 * test is computed (-mfpmath=sse), so each gap and sum is the scalar's to
 * the bit: a lane's max picks the scalar branch's operand (lo - p when p is
 * below the box, p - hi above it, and +0 inside, where both differences are
 * at most zero). The four are held to the best as it stood before the
 * first of them is tested, which can only test a candidate the scalar loop
 * would have skipped, never skip one it would have tested; the minimum is
 * the same either way. Measured on Statue under XBLA (room 2's pose): the
 * box test was 30% of the frame's samples and the trace 15 of a 22 ms walk,
 * about 94 candidates a step of which about 5 lie within reach.
 * SL_WA_FILL_SIMD=0 tests one box at a time (the A/B). */
static int g_fill_simd = -1;               /* the self-test sets it directly */
static int fill_simd_on(void)
{
    if (g_fill_simd < 0) { const char *v = getenv("SL_WA_FILL_SIMD"); g_fill_simd = (v == NULL || *v != '0'); }
    return g_fill_simd;
}

__attribute__((target("sse2")))
static float fill_run_scan(const struct sl_wa_asset *a, unsigned start, unsigned count, const float *p,
                           float best, const float *n)
{
    const unsigned N = g_fill_nruns;
    const float *lx = g_fill_soa + start, *ly = lx + N, *lz = ly + N;
    const float *hx = lz + N, *hy = hx + N, *hz = hy + N;
    const __m128 px = _mm_set1_ps(p[0]), py = _mm_set1_ps(p[1]), pz = _mm_set1_ps(p[2]);
    const __m128 zero = _mm_setzero_ps();
    unsigned i = 0;
    for (; i + 4u <= count && best > 0.0f; i += 4u) {
        const __m128 gx = _mm_max_ps(_mm_max_ps(_mm_sub_ps(_mm_loadu_ps(lx + i), px),
                                                _mm_sub_ps(px, _mm_loadu_ps(hx + i))), zero);
        const __m128 gy = _mm_max_ps(_mm_max_ps(_mm_sub_ps(_mm_loadu_ps(ly + i), py),
                                                _mm_sub_ps(py, _mm_loadu_ps(hy + i))), zero);
        const __m128 gz = _mm_max_ps(_mm_max_ps(_mm_sub_ps(_mm_loadu_ps(lz + i), pz),
                                                _mm_sub_ps(pz, _mm_loadu_ps(hz + i))), zero);
        const __m128 d2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(gx, gx), _mm_mul_ps(gy, gy)), _mm_mul_ps(gz, gz));
        /* far: d2 > the bound, as fill_box_far (an unordered lane is tested) */
        int m = ~_mm_movemask_ps(_mm_cmpgt_ps(d2, _mm_set1_ps(best * (1.0f + SL_WA_FILL_BOX_MARGIN)))) & 0xf;
        while (m != 0) {
            const int j = __builtin_ctz((unsigned) m);
            best = fill_tri_test(a, g_fill_runs[start + i + (unsigned) j], p, best, n);
            m &= m - 1;
        }
    }
    for (; i < count && best > 0.0f; i++) {
        if (fill_box_far(&g_fill_rbox[(start + i) * 6u], p, best)) continue;
        best = fill_tri_test(a, g_fill_runs[start + i], p, best, n);
    }
    return best;
}

static float fill_distance(const float *p, const float *n, float limit);

float sl_wa_fill_distance(const float *p, float limit)
{
    return fill_distance(p, NULL, limit);
}

float sl_wa_fill_distance_aligned(const float *p, const float *n, float limit)
{
    return fill_distance(p, n, limit);
}

static float fill_distance(const float *p, const float *n, float limit)
{
    const struct sl_wa_asset *a = g_fill_asset;
    float best;
    int lo[3], hi[3], x, y, z, k;
    unsigned i, lv;
    const int boxes = g_fill_box != NULL && g_fill_rbox != NULL && fill_boxes_on();
    const int simd = boxes && g_fill_soa != NULL && fill_simd_on();
    if (a == NULL || p == NULL || !(limit > 0.0f)) return limit;
    best = limit * limit;
    for (lv = 0; lv < SL_WA_FILL_LEVELS && best > 0.0f; lv++) {
        const float inv = fill_level_inv(1.0f / g_fill_cell, lv);
        for (k = 0; k < 3; k++) {
            lo[k] = fill_floor((p[k] - limit) * inv);
            hi[k] = fill_floor((p[k] + limit) * inv);
        }
        for (x = lo[0]; x <= hi[0] && best > 0.0f; x++)
            for (y = lo[1]; y <= hi[1] && best > 0.0f; y++)
                for (z = lo[2]; z <= hi[2] && best > 0.0f; z++) {
                    const struct fill_cell *c = fill_find(fill_key(lv, x, y, z));
                    if (c == NULL) continue;
                    if (simd) { best = fill_run_scan(a, c->start, c->count, p, best, n); continue; }
                    for (i = 0; i < c->count && best > 0.0f; i++) {
                        const unsigned t = g_fill_runs[c->start + i];
                        if (boxes && fill_box_far(&g_fill_rbox[(c->start + i) * 6u], p, best)) continue;
                        best = fill_tri_test(a, t, p, best, n);
                    }
                }
    }
    for (i = 0; i < g_fill_nlarge && best > 0.0f; i++) {
        if (boxes && fill_box_far(&g_fill_box[g_fill_large[i] * 6u], p, best)) continue;
        best = fill_tri_test(a, g_fill_large[i], p, best, n);
    }
    return (float) sqrt((double) best);
}

/* #81 the replacement leaves a hole open. See the header. */
int sl_wa_fill_open(const float *eye, const float *hq, float sc)
{
    float dir[3], len = 0.0f, t, tmax, lim, eps, pt[3];
    int k, it;
    if (eye == NULL || hq == NULL || !(sc > 0.0f)) return 1;
    for (k = 0; k < 3; k++) { dir[k] = hq[k] - eye[k]; len += dir[k] * dir[k]; }
    len = sqrtf(len);
    if (!(len > 0.0f)) return 1;
    for (k = 0; k < 3; k++) dir[k] /= len;
    tmax = SL_WA_FILL_OPEN / sc;
    lim = SL_WA_FILL_OPEN_STEP / sc;
    eps = SL_WA_FILL_OPEN_HIT / sc;
    for (t = 0.0f, it = 0; t < tmax && it < 256; it++) {
        float dd;
        for (k = 0; k < 3; k++) pt[k] = hq[k] + t * dir[k];
        dd = sl_wa_fill_distance(pt, lim);
        if (dd < eps && t > 0.0f) return 0;
        t += (dd > eps) ? dd : eps;
    }
    return 1;
}

unsigned sl_wa_fill_counts(unsigned *large)
{
    if (large) *large = g_fill_nlarge;
    return g_fill_ntris;
}

/* The scene-assembly rule. See the header for what each class is and why the
 * condition is the replacement's own rather than a level's name. */
int sl_wa_compose_skydome(int background_replaced)
{
    /* Over the game's own backdrop the sky is additive; with a replacement it
     * is the replacement's companion, drawn first and beneath it. Composed
     * either way - measured, see the header. */
    (void) background_replaced;
    return 1;
}

void sl_wa_report(FILE *f)
{
    unsigned i, tris = 0, verts = 0;
    if (f == NULL) return;
    if (!sl_wa_active()) {
        /* WHY it is dark matters: an override that pinned it off must not read
         * as the presentation having chosen nothing. */
        const char *e = getenv("SL_WORLD_ASSETS");
        if (e != NULL && e[0] != '\0') {
            const char *d = sl_presentation_world_set(sl_presentation());
            fprintf(f, "#68 worldasset: inactive (SL_WORLD_ASSETS override '%s' "
                       "pins it dark; PRESENTATION %s selects %s) - no lookup\n",
                    e, sl_presentation_name(sl_presentation()),
                    d[0] != '\0' ? d : "none");
        }
        else
            fprintf(f, "#68 worldasset: inactive (PRESENTATION %s selects no world "
                       "assets) - no lookup\n",
                    sl_presentation_name(sl_presentation()));
        return;
    }
    for (i = 0; i < g_slots; i++)
        if (g_slot[i].asset != NULL) {
            verts += g_slot[i].asset->vertex_count;
            tris  += g_slot[i].asset->index_count / 3u;
        }
    {
        const char *e = getenv("SL_WORLD_ASSETS");
        fprintf(f, "#68 worldasset: set=%s (%s) root=%s (%s) lookups=%u loaded=%u "
                   "missing=%u invalid=%u resident verts=%u tris=%u\n",
                g_set,
                (e != NULL && e[0] != '\0') ? "SL_WORLD_ASSETS override"
                                            : "from the presentation",
                wa_root(), g_root_from_env ? "env" : "player-data",
                g_lookups, g_loaded, g_missing, g_invalid, verts, tris);
    }
}

/* =========================================================================
 * SELF-TEST (-DSL_WA_SELFTEST). Every fixture is SYNTHETIC: this file writes
 * the bytes it then reads, under a scratch directory it makes and removes.
 * No source, no prepared asset and no ROM is opened, so CI can run it with
 * nothing installed - which is the point.
 * ========================================================================= */
#ifdef SL_WA_SELFTEST

#include <stdarg.h>
#include <math.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0777)
#endif

static int t_pass, t_fail;
static char t_dir[512];

static void ck(int cond, const char *fmt, ...)
{
    va_list ap;
    if (cond) { t_pass++; return; }
    t_fail++;
    va_start(ap, fmt);
    fprintf(stdout, "FAIL: ");
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    va_end(ap);
}

const char *sl_asset_override_dir(void) { return t_dir; }

static void put_u32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char) (v & 0xff);       p[1] = (unsigned char) ((v >> 8) & 0xff);
    p[2] = (unsigned char) ((v >> 16) & 0xff); p[3] = (unsigned char) ((v >> 24) & 0xff);
}

static void put_f32(unsigned char *p, float f)
{
    union { unsigned u; float f; } c; c.f = f; put_u32(p, c.u);
}

/* Build a valid bundle: `tris` triangles over `verts` vertices, one 2x2
 * image, one material, one mesh. Returns the length; the caller owns nothing
 * (the buffer is static). */
#define FIX_MAX 65536
static unsigned char g_fix[FIX_MAX];
static unsigned g_fix_class = SL_WA_CLASS_SKYDOME;

static unsigned build(const char *path, unsigned verts, unsigned tris,
                      unsigned ntex, unsigned nmat, unsigned nmesh)
{
    unsigned vo, io, mo, to, ho, po, i, len, idx = verts * SL_WA_VERTEX_SIZE;
    memset(g_fix, 0, sizeof g_fix);
    g_fix[0] = 'S'; g_fix[1] = 'L'; g_fix[2] = 'W'; g_fix[3] = 'A';
    put_u32(g_fix + 4, SL_WA_VERSION_MIN);       /* the static layout */
    put_u32(g_fix + 8, g_fix_class);
    strcpy((char *) g_fix + 12, path);
    vo = SL_WA_HEADER;
    io = vo + idx;
    mo = io + tris * 3u * SL_WA_INDEX_SIZE;
    to = mo + nmat * SL_WA_MATERIAL_SIZE;
    ho = to + ntex * SL_WA_TEXTURE_SIZE;
    po = ho + nmesh * SL_WA_MESH_SIZE;
    len = po + (ntex ? 2u * 2u * 4u : 0u);
    put_u32(g_fix + 72, verts);
    put_u32(g_fix + 76, tris * 3u);
    put_u32(g_fix + 80, nmat);
    put_u32(g_fix + 84, ntex);
    put_u32(g_fix + 88, nmesh);
    put_u32(g_fix + 92, vo);
    put_u32(g_fix + 96, io);
    put_u32(g_fix + 100, mo);
    put_u32(g_fix + 104, to);
    put_u32(g_fix + 108, ho);
    put_u32(g_fix + 112, len);
    for (i = 0; i < verts; i++) {
        unsigned char *p = g_fix + vo + i * SL_WA_VERTEX_SIZE;
        put_f32(p, (float) i); put_f32(p + 4, (float) (i * 2));
        put_f32(p + 8, (float) (i * 3));
        put_f32(p + 12, 0.25f * (float) i); put_f32(p + 16, 0.5f);
        p[20] = (unsigned char) (10 + i); p[21] = 20; p[22] = 30; p[23] = 255;
    }
    for (i = 0; i < tris * 3u; i++)
        put_u32(g_fix + io + i * SL_WA_INDEX_SIZE, i % verts);
    for (i = 0; i < nmat; i++)
        put_u32(g_fix + mo + i * SL_WA_MATERIAL_SIZE, 0);      /* texture 0 */
    for (i = 0; i < ntex; i++) {
        unsigned char *p = g_fix + to + i * SL_WA_TEXTURE_SIZE;
        put_u32(p, 2); put_u32(p + 4, 2);
        put_u32(p + 8, po); put_u32(p + 12, 2u * 2u * 4u);
    }
    for (i = 0; i < nmesh; i++) {
        unsigned char *p = g_fix + ho + i * SL_WA_MESH_SIZE;
        put_u32(p, 0); put_u32(p + 4, 0); put_u32(p + 8, tris * 3u);
    }
    for (i = 0; i < (ntex ? 16u : 0u); i++) g_fix[po + i] = (unsigned char) (i * 7);
    return len;
}

/* A VERSION-2 fixture (#69): the same bundle as build() with `nanim` records
 * and a per-vertex section naming record `named` (0 = static), laid out as
 * header, vertices, indices, materials, images, meshes, aux, records,
 * payload. One live wave per record, amplitude 2, phase 0.75, no speed, no
 * frequency - the phase the evaluation test pins. */
static void put_s16(unsigned char *p, int v)
{
    unsigned u = (unsigned) (v < 0 ? v + 0x10000 : v);
    p[0] = (unsigned char) (u & 0xff); p[1] = (unsigned char) ((u >> 8) & 0xff);
}

static unsigned build_anim(const char *path, unsigned verts, unsigned tris,
                           unsigned nanim, unsigned named)
{
    unsigned vo, io, mo, to, ho, xo, no, po, i, len;
    memset(g_fix, 0, sizeof g_fix);
    g_fix[0] = 'S'; g_fix[1] = 'L'; g_fix[2] = 'W'; g_fix[3] = 'A';
    put_u32(g_fix + 4, 2u);
    put_u32(g_fix + 8, g_fix_class);
    strcpy((char *) g_fix + 12, path);
    vo = SL_WA_HEADER;
    io = vo + verts * SL_WA_VERTEX_SIZE;
    mo = io + tris * 3u * SL_WA_INDEX_SIZE;
    to = mo + SL_WA_MATERIAL_SIZE;
    ho = to + SL_WA_TEXTURE_SIZE;
    xo = ho + SL_WA_MESH_SIZE;
    no = xo + (nanim ? verts * SL_WA_AUX_SIZE : 0u);
    po = no + nanim * SL_WA_ANIM_SIZE;
    len = po + 2u * 2u * 4u;
    put_u32(g_fix + 72, verts);
    put_u32(g_fix + 76, tris * 3u);
    put_u32(g_fix + 80, 1);
    put_u32(g_fix + 84, 1);
    put_u32(g_fix + 88, 1);
    put_u32(g_fix + 92, vo);
    put_u32(g_fix + 96, io);
    put_u32(g_fix + 100, mo);
    put_u32(g_fix + 104, to);
    put_u32(g_fix + 108, ho);
    put_u32(g_fix + 112, len);
    if (nanim) {
        put_u32(g_fix + 116, xo);
        put_u32(g_fix + 120, no);
        put_u32(g_fix + 124, nanim);
    }
    for (i = 0; i < verts; i++) {
        unsigned char *p = g_fix + vo + i * SL_WA_VERTEX_SIZE;
        put_f32(p, (float) i); put_f32(p + 4, 0.0f); put_f32(p + 8, (float) (i * 3));
        put_f32(p + 12, 0.25f * (float) i); put_f32(p + 16, 0.5f);
        p[20] = 200; p[21] = 20; p[22] = 30; p[23] = 255;
    }
    for (i = 0; i < tris * 3u; i++)
        put_u32(g_fix + io + i * SL_WA_INDEX_SIZE, i % verts);
    put_u32(g_fix + mo, 0);
    put_u32(g_fix + to, 2); put_u32(g_fix + to + 4, 2);
    put_u32(g_fix + to + 8, po); put_u32(g_fix + to + 12, 2u * 2u * 4u);
    put_u32(g_fix + ho, 0); put_u32(g_fix + ho + 4, 0); put_u32(g_fix + ho + 8, tris * 3u);
    for (i = 0; nanim && i < verts; i++) {
        unsigned char *p = g_fix + xo + i * SL_WA_AUX_SIZE;
        put_s16(p, (int) i * 100 - 50); put_s16(p + 2, 7);
        put_s16(p + 4, 0); put_s16(p + 6, 32767); put_s16(p + 8, 0);
        p[10] = (unsigned char) (named & 0xff); p[11] = (unsigned char) (named >> 8);
    }
    for (i = 0; i < nanim; i++) {
        unsigned char *p = g_fix + no + i * SL_WA_ANIM_SIZE;
        put_u32(p, SL_WA_ANIM_SINEWAVES);
        put_f32(p + 4, 0.25f);                    /* coord_scale   */
        put_u32(p + 8, 1);                        /* wave_count    */
        put_f32(p + 16, 2.0f);                    /* amplitude[0]  */
        put_f32(p + 32, 0.75f);                   /* phase[0]      */
        put_f32(p + 96, 1.0f);                    /* turn_scale    */
        put_f32(p + 100, 0.5f);                   /* turn_bias     */
        put_f32(p + 104, 6.2831853f);             /* turn          */
        put_f32(p + 112, 1.0f);                   /* poly: a sine  */
        put_f32(p + 116, -1.0f / 6.0f);
        put_f32(p + 120, 1.0f / 120.0f);
        put_f32(p + 124, -1.0f / 5040.0f);
    }
    for (i = 0; i < 16u; i++) g_fix[po + i] = (unsigned char) (i * 7);
    return len;
}

/* A VERSION-3 fixture (#69 material): build_anim's bundle with the 24-byte
 * auxiliary entry (the frame X = +x, Y = +z on every vertex, the normal +y)
 * and one material-extension record - or a NONE record when `mode` is 0 -
 * naming the fixture's single image for all three extra slots, weights
 * 0.9 / 0.1, warp 0.25, normal 2 / -1, sphere (0.5, -0.5) / (0.5, 0.5),
 * mean 1/3, floor 1. Laid out header, vertices, indices, materials, images,
 * meshes, aux, records, matx, payload. */
static unsigned build_matx(const char *path, unsigned verts, unsigned tris,
                           unsigned nanim, unsigned named, unsigned mode)
{
    unsigned vo, io, mo, to, ho, xo, no, mxo, po, i, len;
    memset(g_fix, 0, sizeof g_fix);
    g_fix[0] = 'S'; g_fix[1] = 'L'; g_fix[2] = 'W'; g_fix[3] = 'A';
    put_u32(g_fix + 4, 3u);
    put_u32(g_fix + 8, g_fix_class);
    strcpy((char *) g_fix + 12, path);
    vo = SL_WA_HEADER;
    io = vo + verts * SL_WA_VERTEX_SIZE;
    mo = io + tris * 3u * SL_WA_INDEX_SIZE;
    to = mo + SL_WA_MATERIAL_SIZE;
    ho = to + SL_WA_TEXTURE_SIZE;
    xo = ho + SL_WA_MESH_SIZE;
    no = xo + verts * SL_WA_AUX3_SIZE;
    mxo = no + nanim * SL_WA_ANIM_SIZE;
    po = mxo + SL_WA_MATX_SIZE;
    len = po + 2u * 2u * 4u;
    put_u32(g_fix + 44, mxo);
    put_u32(g_fix + 72, verts);
    put_u32(g_fix + 76, tris * 3u);
    put_u32(g_fix + 80, 1);
    put_u32(g_fix + 84, 1);
    put_u32(g_fix + 88, 1);
    put_u32(g_fix + 92, vo);
    put_u32(g_fix + 96, io);
    put_u32(g_fix + 100, mo);
    put_u32(g_fix + 104, to);
    put_u32(g_fix + 108, ho);
    put_u32(g_fix + 112, len);
    put_u32(g_fix + 116, xo);
    if (nanim) {
        put_u32(g_fix + 120, no);
        put_u32(g_fix + 124, nanim);
    }
    for (i = 0; i < verts; i++) {
        unsigned char *p = g_fix + vo + i * SL_WA_VERTEX_SIZE;
        put_f32(p, (float) i); put_f32(p + 4, 0.0f); put_f32(p + 8, (float) (i * 3));
        put_f32(p + 12, 0.25f * (float) i); put_f32(p + 16, 0.5f);
        p[20] = 200; p[21] = 20; p[22] = 30; p[23] = 255;
    }
    for (i = 0; i < tris * 3u; i++)
        put_u32(g_fix + io + i * SL_WA_INDEX_SIZE, i % verts);
    put_u32(g_fix + mo, 0);
    put_u32(g_fix + to, 2); put_u32(g_fix + to + 4, 2);
    put_u32(g_fix + to + 8, po); put_u32(g_fix + to + 12, 2u * 2u * 4u);
    put_u32(g_fix + ho, 0); put_u32(g_fix + ho + 4, 0); put_u32(g_fix + ho + 8, tris * 3u);
    for (i = 0; i < verts; i++) {
        unsigned char *p = g_fix + xo + i * SL_WA_AUX3_SIZE;
        put_s16(p, (int) i * 100 - 50); put_s16(p + 2, 7);
        put_s16(p + 4, 0); put_s16(p + 6, 32767); put_s16(p + 8, 0);
        p[10] = (unsigned char) (named & 0xff); p[11] = (unsigned char) (named >> 8);
        put_s16(p + 12, 32767); put_s16(p + 14, 0); put_s16(p + 16, 0);   /* X = +x */
        put_s16(p + 18, 0); put_s16(p + 20, 0); put_s16(p + 22, -32767);  /* Y = -z */
    }
    for (i = 0; i < nanim; i++) {
        unsigned char *p = g_fix + no + i * SL_WA_ANIM_SIZE;
        put_u32(p, SL_WA_ANIM_SINEWAVES);
        put_f32(p + 4, 0.25f);
        put_u32(p + 8, 1);
        put_f32(p + 16, 2.0f);
        put_f32(p + 32, 0.75f);
        put_f32(p + 96, 1.0f);
        put_f32(p + 100, 0.5f);
        put_f32(p + 104, 6.2831853f);
        put_f32(p + 112, 1.0f);
        put_f32(p + 116, -1.0f / 6.0f);
        put_f32(p + 120, 1.0f / 120.0f);
        put_f32(p + 124, -1.0f / 5040.0f);
    }
    if (mode != 0u) {
        unsigned char *p = g_fix + mxo;
        put_u32(p, mode);
        put_u32(p + 4, 0); put_u32(p + 8, 0); put_u32(p + 12, 0);
        put_u32(p + 16, SL_WA_MATX_F_SPHERE_CLAMP);
        put_f32(p + 20, 0.9f); put_f32(p + 24, 0.9f); put_f32(p + 28, 0.9f);
        put_f32(p + 32, 0.1f); put_f32(p + 36, 0.1f); put_f32(p + 40, 0.1f);
        put_f32(p + 44, 0.25f);
        put_f32(p + 48, 2.0f); put_f32(p + 52, -1.0f);
        put_f32(p + 56, 0.5f); put_f32(p + 60, -0.5f);
        put_f32(p + 64, 0.5f); put_f32(p + 68, 0.5f);
        put_f32(p + 72, 1.0f / 3.0f); put_f32(p + 76, 1.0f);
    }
    for (i = 0; i < 16u; i++) g_fix[po + i] = (unsigned char) (i * 7);
    return len;
}

/* The self-test's images for sl_wa_matx_shade: `which` selects a constant
 * texel except the base, whose red channel is its u coordinate and whose
 * green is its v - so the warp shows up as a colour. */
static void fix_tex(void *ctx, int which, float u, float v, float *rgba)
{
    (void) ctx;
    switch (which) {
    case 0:  rgba[0] = 0.5f; rgba[1] = 1.0f; rgba[2] = 1.0f; rgba[3] = 1.0f; break;
             /* the normal map: n = (0, 1, 1) after 2t - 1  */
    case 1:  rgba[0] = u;    rgba[1] = v;    rgba[2] = 0.0f; rgba[3] = 1.0f; break;
             /* the sphere image: its own coordinate       */
    case 2:  rgba[0] = 0.4f; rgba[1] = 0.8f; rgba[2] = 0.0f; rgba[3] = 1.0f; break;
             /* the offset map: a shift of (0.1, 0.2) at warp 0.25 */
    default: rgba[0] = u;    rgba[1] = v;    rgba[2] = 1.0f; rgba[3] = 0.5f; break;
             /* the base: its coordinate, alpha 0.5       */
    }
}

static int parse_fix(unsigned len)
{
    static struct sl_wa_asset a;
    return sl_wa_parse(g_fix, len, &a);
}

static void wr(const char *file, const unsigned char *b, unsigned n)
{
    FILE *f = fopen(file, "wb");
    if (f == NULL) { ck(0, "cannot write %s", file); return; }
    fwrite(b, 1, n, f);
    fclose(f);
}

/* #70: a valid VERSION-4 skinned item - four vertices, two triangles in two
 * meshes (the second marked as the built-in head), one 2x2 image, two joints
 * (SKEL_BASE the root, SKEL_NECK above it) and a skin that uses both, one
 * vertex blended. Offsets of the skin, joint and mesh sections are kept for
 * the refusal cases. */
static unsigned g_skin_off, g_joint_off, g_mesh_off;

static unsigned build_skinned(const char *path, unsigned cls)
{
    unsigned vo = SL_WA_HEADER, io, mo, to, ho, ko, jo, po, len, i;
    static const unsigned char js[4][4] = { {0,0,0,0}, {1,0,0,0}, {0,1,0,0}, {1,0,0,0} };
    static const unsigned char ws[4][4] = { {255,0,0,0}, {255,0,0,0}, {128,127,0,0}, {255,0,0,0} };
    static const unsigned tri[6] = { 0, 1, 2, 1, 3, 2 };

    memset(g_fix, 0, sizeof g_fix);
    g_fix[0] = 'S'; g_fix[1] = 'L'; g_fix[2] = 'W'; g_fix[3] = 'A';
    put_u32(g_fix + 4, 4u);
    put_u32(g_fix + 8, cls);
    strcpy((char *) g_fix + 12, path);
    io = vo + 4u * SL_WA_VERTEX_SIZE;
    mo = io + 6u * SL_WA_INDEX_SIZE;
    to = mo + SL_WA_MATERIAL_SIZE;
    ho = to + SL_WA_TEXTURE_SIZE;
    ko = ho + 2u * SL_WA_MESH_SIZE;
    jo = ko + 4u * SL_WA_SKIN_SIZE;
    po = jo + 2u * SL_WA_JOINT_SIZE;
    len = po + 16u;
    put_u32(g_fix + 72, 4);  put_u32(g_fix + 76, 6);  put_u32(g_fix + 80, 1);
    put_u32(g_fix + 84, 1);  put_u32(g_fix + 88, 2);
    put_u32(g_fix + 92, vo); put_u32(g_fix + 96, io); put_u32(g_fix + 100, mo);
    put_u32(g_fix + 104, to); put_u32(g_fix + 108, ho); put_u32(g_fix + 112, len);
    put_u32(g_fix + 116, ko); put_u32(g_fix + 120, jo); put_u32(g_fix + 124, 2);
    for (i = 0; i < 4; i++) {
        unsigned char *p = g_fix + vo + i * SL_WA_VERTEX_SIZE;
        put_f32(p, (float) i);
        put_f32(p + 4, i > 1u ? 10.0f : 0.0f);
        p[20] = p[21] = p[22] = p[23] = 255;
        memcpy(g_fix + ko + i * SL_WA_SKIN_SIZE, js[i], 4);
        memcpy(g_fix + ko + i * SL_WA_SKIN_SIZE + 4, ws[i], 4);
    }
    for (i = 0; i < 6; i++) put_u32(g_fix + io + 4 * i, tri[i]);
    put_u32(g_fix + to, 2); put_u32(g_fix + to + 4, 2);
    put_u32(g_fix + to + 8, po); put_u32(g_fix + to + 12, 16);
    put_u32(g_fix + ho + 0, 0); put_u32(g_fix + ho + 4, 0); put_u32(g_fix + ho + 8, 3);
    put_u32(g_fix + ho + 16, 0); put_u32(g_fix + ho + 20, 3); put_u32(g_fix + ho + 24, 3);
    put_u32(g_fix + ho + 28, SL_WA_MESH_BUILTIN_HEAD);
    strcpy((char *) g_fix + jo, "SKEL_BASE");
    put_u32(g_fix + jo + 24, SL_WA_JOINT_ROOT);
    strcpy((char *) g_fix + jo + SL_WA_JOINT_SIZE, "SKEL_NECK");
    put_u32(g_fix + jo + SL_WA_JOINT_SIZE + 24, 0);
    put_f32(g_fix + jo + SL_WA_JOINT_SIZE + 32, 10.0f);
    memset(g_fix + po, 255, 16);
    g_skin_off = ko; g_joint_off = jo; g_mesh_off = ho;
    return len;
}

int main(void)
{
    static struct sl_wa_asset a;
    unsigned len;
    char sub[640], file[768];
    const char *tmp;

    tmp = getenv("TEMP");
    if (tmp == NULL || tmp[0] == '\0') tmp = getenv("TMP");
    if (tmp == NULL || tmp[0] == '\0') tmp = ".";
    snprintf(t_dir, sizeof t_dir, "%s\\sl_wa_selftest", tmp);
    MKDIR(t_dir);

    /* ---- the parser, on bytes alone ---------------------------------- */
    len = build("dam", 4, 2, 1, 1, 1);
    ck(parse_fix(len) == SL_WA_OK, "a valid two-triangle bundle parses");
    ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK && a.vertex_count == 4 &&
       a.index_count == 6 && a.mesh_count == 1 && a.material_count == 1 &&
       a.texture_count == 1 && strcmp(a.path, "dam") == 0,
       "the parsed bundle reports its own counts and path");
    ck(a.textures[0].w == 2 && a.textures[0].h == 2 &&
       a.textures[0].rgba != NULL, "the image binds at its declared size");

    len = build("dam", 4, 1, 1, 1, 1);
    ck(parse_fix(len) == SL_WA_OK, "a single-triangle bundle parses");

    len = build("dam", 4, 2, 1, 1, 1);
    g_fix[0] = 'X';
    ck(parse_fix(len) == SL_WA_E_MAGIC, "wrong magic is refused");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 4, SL_WA_VERSION + 1u);
    ck(parse_fix(len) == SL_WA_E_VERSION, "an unsupported version is refused");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 8, 99);
    ck(parse_fix(len) == SL_WA_E_CLASS, "an unknown asset class is refused");

    /* ---- THE BACKGROUND CLASS, and that the layout did NOT change -------
     *
     * The whole-background replacement needs many more meshes, materials and
     * images than a skydome, and one more value in the class field. It needs
     * no new LAYOUT, which is why the version did not move - and these are the
     * checks that say a bundle written before it still parses, byte for byte,
     * beside one written after. */
    g_fix_class = SL_WA_CLASS_BACKGROUND;
    len = build("dam", 4, 2, 1, 1, 1);
    ck(parse_fix(len) == SL_WA_OK, "a background-class bundle parses");
    ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK &&
       a.asset_class == SL_WA_CLASS_BACKGROUND,
       "the parsed bundle reports the background class");
    ck(rd_u32(g_fix + 4) == SL_WA_VERSION_MIN,
       "the background class did not move the format version: a bundle that "
       "moves nothing is still a version-1 file");
    len = build("dam", 64, 200, 40, 50, 60);
    ck(parse_fix(len) == SL_WA_OK,
       "a background bundle with 60 meshes, 50 materials and 40 images parses");
    {
        unsigned char keep[FIX_MAX];
        unsigned klen;
        memcpy(keep, g_fix, sizeof keep);
        klen = len;
        g_fix_class = SL_WA_CLASS_SKYDOME;
        len = build("dam", 4, 2, 1, 1, 1);
        ck(parse_fix(len) == SL_WA_OK,
           "a skydome bundle still parses beside the background class");
        memcpy(g_fix, keep, sizeof keep);
        ck(parse_fix(klen) == SL_WA_OK,
           "and the background bundle parses beside it, from the same parser");
    }
    /* Past the new ceilings is still refused - a wider limit is a limit. */
    g_fix_class = SL_WA_CLASS_BACKGROUND;
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 80, SL_WA_MAX_MATERIALS + 1u);
    ck(parse_fix(len) == SL_WA_E_COUNT,
       "a material count past the background limit is still refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 88, SL_WA_MAX_MESHES + 1u);
    ck(parse_fix(len) == SL_WA_E_COUNT,
       "a mesh count past the background limit is still refused");
    /* Depot's background item carries 136 images (#92): past the 128 the
     * limit held before, so the limit is now the material limit's 256. */
    len = build("dam", 4, 2, 136, 1, 1);
    ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK && a.texture_count == 136,
       "a background bundle of 136 images parses (Depot's item)");
    len = build("dam", 4, 2, SL_WA_MAX_TEXTURES, 1, 1);
    ck(parse_fix(len) == SL_WA_OK, "a bundle at the image limit parses");
    g_fix_class = SL_WA_CLASS_SKYDOME;

    len = build("dam", 4, 2, 1, 1, 1);
    ck(sl_wa_parse_header(g_fix, SL_WA_HEADER - 1u, &a) == SL_WA_E_TRUNC,
       "a header shorter than the header is refused");
    ck(parse_fix(len - 1u) == SL_WA_E_SIZE,
       "a file truncated after the header is refused");
    ck(parse_fix(SL_WA_HEADER) == SL_WA_E_SIZE,
       "a file that is only a header is refused");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 44, 1);
    ck(parse_fix(len) == SL_WA_E_FLAGS, "a non-zero reserved flag is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 120, 1);
    ck(parse_fix(len) == SL_WA_E_FLAGS, "a non-zero reserved word is refused");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 72, 0);
    ck(parse_fix(len) == SL_WA_E_COUNT, "a zero vertex count is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 72, SL_WA_MAX_VERTICES + 1u);
    ck(parse_fix(len) == SL_WA_E_COUNT, "an excessive vertex count is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 76, 0xfffffffcu);
    ck(parse_fix(len) == SL_WA_E_COUNT,
       "an index count that would overflow its byte length is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 76, 7);
    ck(parse_fix(len) == SL_WA_E_COUNT, "an index count that is not a multiple of 3 is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 84, SL_WA_MAX_TEXTURES + 1u);
    ck(parse_fix(len) == SL_WA_E_COUNT, "an excessive texture count is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 88, SL_WA_MAX_MESHES + 1u);
    ck(parse_fix(len) == SL_WA_E_COUNT, "an excessive mesh count is refused");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 92, SL_WA_HEADER + 1u);
    ck(parse_fix(len) == SL_WA_E_LAYOUT, "an unaligned section offset is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 92, 0xfffffff0u);
    ck(parse_fix(len) == SL_WA_E_LAYOUT, "a section past the end is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 96, rd_u32(g_fix + 92));
    ck(parse_fix(len) == SL_WA_E_LAYOUT, "two sections that overlap are refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 92, 4);
    ck(parse_fix(len) == SL_WA_E_LAYOUT, "a section inside the header is refused");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + 112, len + 4u);
    ck(parse_fix(len) == SL_WA_E_SIZE, "a declared size that is not the length is refused");

    len = build("dam", 4, 2, 1, 1, 1);
    strcpy((char *) g_fix + 12, "Dam");
    ck(parse_fix(len) == SL_WA_E_PATH, "a path with a capital is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    memset(g_fix + 12, 'a', SL_WA_PATH_MAX);
    ck(parse_fix(len) == SL_WA_E_PATH, "an unterminated path is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    g_fix[12] = '\0';
    ck(parse_fix(len) == SL_WA_E_PATH, "an empty path is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    g_fix[12 + 4] = 'x';                       /* junk after the terminator */
    ck(parse_fix(len) == SL_WA_E_PATH, "junk after the path terminator is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    strcpy((char *) g_fix + 12, "a/b");
    ck(parse_fix(len) == SL_WA_E_PATH, "a path with a separator is refused");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100), 5);   /* material -> texture 5 */
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "a material naming a texture that is not there is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 4, 7);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "an unsupported blend mode is refused");
    /* CUTOUT is accepted at the SAME version as the two modes that came before
     * it. The layout does not move, so a bundle written before it existed
     * still parses - widening an accepted set cannot invalidate an older
     * file, exactly as the background CLASS did. */
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 4, SL_WA_BLEND_CUTOUT);
    ck(parse_fix(len) == SL_WA_OK, "a CUTOUT material is accepted at this version");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 4, SL_WA_BLEND_ALPHA);
    ck(parse_fix(len) == SL_WA_OK, "an ALPHA material still parses beside it");
    /* THE MATERIAL FLAGS WORD, at +8. It used to be a second reserved field
     * required to be zero; it now carries the source's decal-pass bit. The
     * three cases that matter are the three checked: the bit this version
     * defines is ACCEPTED, a bit it does not is REFUSED rather than ignored,
     * and zero - what every bundle written before the flag existed carries -
     * still parses, which is what says the version did not have to move. */
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_DECAL);
    ck(parse_fix(len) == SL_WA_OK, "a DECAL material is accepted at this version");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, 0);
    ck(parse_fix(len) == SL_WA_OK,
       "a material written before the decal flag existed still parses beside it");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, ~SL_WA_MAT_KNOWN);
    ck(parse_fix(len) == SL_WA_E_MATERIAL,
       "a material flag bit this version does not define is refused, not ignored");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_DECAL | 64u);
    ck(parse_fix(len) == SL_WA_E_MATERIAL,
       "a known flag beside an unknown one is refused too");
    /* #71 BLUE COVERAGE: accepted on an ALPHA material, alone or beside the
     * decal flag (depth priority and coverage are independent); refused on an
     * opaque or cutout one, where it would mean nothing. */
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 4, SL_WA_BLEND_ALPHA);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_COVERAGE_BLUE);
    ck(parse_fix(len) == SL_WA_OK, "a COVERAGE_BLUE material is accepted when ALPHA-blended");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 4, SL_WA_BLEND_ALPHA);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_COVERAGE_BLUE | SL_WA_MAT_DECAL);
    ck(parse_fix(len) == SL_WA_OK, "blue coverage and the decal flag coexist on one material");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 4, SL_WA_BLEND_OPAQUE);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_COVERAGE_BLUE);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "blue coverage on an OPAQUE material is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 4, SL_WA_BLEND_CUTOUT);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_COVERAGE_BLUE);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "blue coverage on a CUTOUT material is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_FOG | SL_WA_MAT_DECAL);
    ck(parse_fix(len) == SL_WA_OK,
       "a material whose source program consumes the stage-fog contract is "
       "accepted at this version, beside the decal flag");
    /* #68 D-023: a GENERATED material is accepted on a skydome, refused on
     * any other class (nothing there would ever leave it out), and drawn only
     * while the generated zenith clouds are on - a source material always. */
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_GENERATED);
    ck(parse_fix(len) == SL_WA_OK,
       "a GENERATED material is accepted on a skydome at this version");
    g_fix_class = SL_WA_CLASS_BACKGROUND;
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_GENERATED);
    ck(parse_fix(len) == SL_WA_E_MATERIAL,
       "a GENERATED material is refused on a background");
    g_fix_class = SL_WA_CLASS_SKYDOME;
    ck(sl_wa_material_drawn(SL_WA_MAT_GENERATED, 1) == 1,
       "a GENERATED material is drawn while the zenith clouds are on");
    ck(sl_wa_material_drawn(SL_WA_MAT_GENERATED, 0) == 0,
       "a GENERATED material is left out when they are off - the source dome alone");
    ck(sl_wa_material_drawn(0u, 0) == 1 && sl_wa_material_drawn(0u, 1) == 1
       && sl_wa_material_drawn(SL_WA_MAT_FOG | SL_WA_MAT_DECAL, 0) == 1,
       "a source material is drawn either way");
    /* The item pass's decal rule: the priority is taken by a DECAL material
     * alone, in either pass, and by none while the decal pass is off. */
    ck(sl_wa_decal_priority(SL_WA_MAT_DECAL, 1) == 1
       && sl_wa_decal_priority(SL_WA_MAT_DECAL | SL_WA_MAT_FOG, 1) == 1,
       "a DECAL material on a replacement item takes the decal depth priority");
    ck(sl_wa_decal_priority(0u, 1) == 0 && sl_wa_decal_priority(SL_WA_MAT_FOG, 1) == 0
       && sl_wa_decal_priority(SL_WA_MAT_GENERATED, 1) == 0,
       "an ordinary material - opaque, blended or cutout alike - takes no offset");
    ck(sl_wa_decal_priority(SL_WA_MAT_DECAL, 0) == 0,
       "with the decal pass off a DECAL material is drawn with no priority");
    ck(sl_wa_decal_priority(SL_WA_MAT_DECAL | SL_WA_MAT_COVERAGE_BLUE, 1) == 1,
       "a decal with blue coverage keeps the decal depth priority");
    /* #81 the decal pass's depth adjustment: z = a w + b read from a
     * perspective projection, plain and with a view folded in, and a pulled
     * vertex's depth is exactly that of the point `pull` nearer on its ray */
    {
        const float n = 15.0f, f = 10000.0f;
        float P[16], V[16], C[16], a = 0.0f, b = 0.0f, a2 = 0.0f, b2 = 0.0f;
        const float c = 0.8f, s = 0.6f;
        int i, j, k2;
        memset(P, 0, sizeof P);
        P[0] = 0.9f; P[5] = 1.7f; P[10] = -(f + n) / (f - n); P[11] = -1.0f;
        P[14] = -2.0f * f * n / (f - n);
        ck(sl_wa_depth_affine(P, &a, &b) && fabs(a + P[10]) < 1e-6 && fabs(b - P[14]) < 1e-3,
           "a projection's clip z is a w + b with a = -P[10], b = P[14]");
        memset(V, 0, sizeof V);                       /* a turn about y and a move */
        V[0] = c; V[2] = -s; V[5] = 1.0f; V[8] = s; V[10] = c; V[15] = 1.0f;
        V[12] = 40.0f; V[13] = -7.0f; V[14] = 300.0f;
        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++) {
                double acc = 0.0;
                for (k2 = 0; k2 < 4; k2++) acc += (double) V[i * 4 + k2] * P[k2 * 4 + j];
                C[i * 4 + j] = (float) acc;
            }
        ck(sl_wa_depth_affine(C, &a2, &b2) && fabs(a2 - a) < 1e-5 && fabs(b2 - b) < 1e-2,
           "with a view folded in the relation is the projection's own");
        {
            const float w = 800.0f, z = a * w + b, zp = sl_wa_decal_pull_z(a, b, w, 2.0f);
            const double want = (double) a + (double) b / (w - 2.0);   /* ndc of the nearer point */
            ck(fabs((double) zp / w - want) < 1e-6 && zp < z,
               "a pulled vertex draws at the depth of the point 2 units nearer (%g)", (double) zp);
            ck(sl_wa_decal_pull_z(a, b, w, 0.0f) == z,
               "no pull is the vertex's own depth");
            ck(fabs((double) sl_wa_decal_pull_z(a, b, 3.0f, 2.0f) / 3.0 - ((double) a + (double) b / 1.5))
               < 1e-5, "and never more than half the way to the eye");
        }
        memset(C, 0, sizeof C);
        C[2] = 1.0f; C[11] = -1.0f;
        ck(!sl_wa_depth_affine(C, &a, &b), "a matrix whose z and w rows are not parallel has none");
        {
            /* #81 the plane rule: the wall x = 0, the eye 10 out and well off
             * to the side - the ratio is the plane's distance, not the ray's */
            const float q0[3] = {0.0f, 0.0f, 0.0f}, q1[3] = {0.0f, 1.0f, 0.0f},
                        q2[3] = {0.0f, 0.0f, 1.0f}, eye[3] = {10.0f, 3.0f, -400.0f},
                        on[3] = {0.0f, 5.0f, 5.0f};
            ck(fabs((double) sl_wa_decal_plane_ratio(q0, q1, q2, eye, 2.0f) - 0.2) < 1e-6,
               "a decal moves 2 units along its plane's normal: 2 / 10 of each sight line");
            ck(sl_wa_decal_plane_ratio(q0, q1, q1, eye, 2.0f) == 0.0f
               && sl_wa_decal_plane_ratio(q0, q1, q2, on, 2.0f) == 0.0f
               && sl_wa_decal_plane_ratio(q0, q1, q2, eye, 0.0f) == 0.0f,
               "a degenerate triangle, an eye on the plane or no pull give no ratio");
            {
                /* the plane's depth is AFFINE in clip w - the same value the
                 * sight-line pull gives in front of the eye, and a straight
                 * line the clipper can cut anywhere, behind the eye too */
                const float w = 800.0f, r = 0.25f;
                const float zp = sl_wa_decal_plane_z(a, b, w, r);
                const float zs = sl_wa_decal_pull_z(a, b, w, w * r);
                const float z1 = sl_wa_decal_plane_z(a, b, -40.0f, r),
                            z2 = sl_wa_decal_plane_z(a, b, 360.0f, r);
                ck(fabs((double) zp - (double) zs) < 1e-3 * fabs((double) zs),
                   "the plane's depth is the sight-line pull of w x ratio (%g, %g)",
                   (double) zp, (double) zs);
                ck(fabs((double) sl_wa_decal_plane_z(a, b, 160.0f, r)
                        - ((double) z1 + (double) z2) * 0.5) < 1e-3,
                   "and affine in w: the midpoint of two vertices, one behind the eye, "
                   "is the midpoint's");
                ck(sl_wa_decal_plane_z(a, b, w, 0.9f) == sl_wa_decal_plane_z(a, b, w, 0.5f)
                   && fabs((double) sl_wa_decal_plane_z(a, b, w, 0.0f)
                           - ((double) a * w + (double) b)) < 1e-3,
                   "never more than half the way to the eye; no ratio, no change");
            }
        }
    }
    /* The coverage rule: alpha from blue for a flagged ALPHA material while
     * the rule is on; the image's own alpha for every other material, and for
     * the flagged one with the rule off (the control). */
    ck(sl_wa_coverage_blue(SL_WA_MAT_COVERAGE_BLUE, SL_WA_BLEND_ALPHA, 1) == 1
       && sl_wa_coverage_blue(SL_WA_MAT_COVERAGE_BLUE | SL_WA_MAT_DECAL, SL_WA_BLEND_ALPHA, 1) == 1,
       "a COVERAGE_BLUE blended material samples its alpha from blue");
    ck(sl_wa_coverage_blue(0u, SL_WA_BLEND_ALPHA, 1) == 0
       && sl_wa_coverage_blue(SL_WA_MAT_DECAL, SL_WA_BLEND_ALPHA, 1) == 0
       && sl_wa_coverage_blue(SL_WA_MAT_FOG, SL_WA_BLEND_ALPHA, 1) == 0,
       "an ordinary blended material - a decal or not - keeps its image's alpha");
    ck(sl_wa_coverage_blue(0u, SL_WA_BLEND_OPAQUE, 1) == 0
       && sl_wa_coverage_blue(0u, SL_WA_BLEND_CUTOUT, 1) == 0
       && sl_wa_coverage_blue(SL_WA_MAT_COVERAGE_BLUE, SL_WA_BLEND_OPAQUE, 1) == 0
       && sl_wa_coverage_blue(SL_WA_MAT_COVERAGE_BLUE, SL_WA_BLEND_CUTOUT, 1) == 0,
       "opaque and cutout materials never take blue coverage");
    ck(sl_wa_coverage_blue(SL_WA_MAT_COVERAGE_BLUE, SL_WA_BLEND_ALPHA, 0) == 0,
       "with the coverage rule off the flagged material draws its image's alpha");
    /* #94 the linear program's environments: 0, 3, 4 always; 2 only under
     * the row with the rule on; nothing else, row or not */
    ck(sl_wa_lin_takes(0, 0, 1) && sl_wa_lin_takes(3, 0, 0) && sl_wa_lin_takes(4, 1, 1),
       "modulate, alpha add and alpha lerp take the linear program, row or not");
    ck(sl_wa_lin_takes(2, 1, 1) == 1,
       "under the stage row a shot object's environment (2) takes it, tint and all");
    ck(sl_wa_lin_takes(2, 0, 1) == 0 && sl_wa_lin_takes(2, 1, 0) == 0,
       "without the row, or with the rule off, environment 2 stays on the fixed pipeline");
    ck(sl_wa_lin_takes(1, 1, 1) == 0 && sl_wa_lin_takes(5, 1, 1) == 0 && sl_wa_lin_takes(6, 1, 1) == 0
       && sl_wa_lin_takes(7, 1, 1) == 0 && sl_wa_lin_takes(9, 1, 1) == 0,
       "no other environment takes it");
    /* #94 a cutout after the solid world behind it: synthetic item, meshes
     * 0 opaque, 1 decal, 2 cutout, 3 opaque, 4 decal, 5 cutout decal,
     * 6 panorama translucent, 7 translucent. */
    {
        static struct sl_wa_asset oa;
        static const unsigned bl[8] = { SL_WA_BLEND_OPAQUE, SL_WA_BLEND_ALPHA, SL_WA_BLEND_CUTOUT,
                                        SL_WA_BLEND_OPAQUE, SL_WA_BLEND_OPAQUE, SL_WA_BLEND_CUTOUT,
                                        SL_WA_BLEND_ALPHA, SL_WA_BLEND_ALPHA };
        static const unsigned fl[8] = { 0u, SL_WA_MAT_DECAL, 0u, 0u, SL_WA_MAT_DECAL, SL_WA_MAT_DECAL,
                                        SL_WA_MAT_PANORAMA, SL_WA_MAT_FOG };
        unsigned k, fc;
        int order[16], n = 0;
        memset(&oa, 0, sizeof oa);
        oa.mesh_count = oa.material_count = 8u;
        for (k = 0; k < 8u; k++) {
            oa.meshes[k].material = k;
            oa.materials[k].blend = bl[k];
            oa.materials[k].flags = fl[k];
        }
        fc = sl_wa_first_cutout(&oa);
        ck(fc == 2u, "the first cutout mesh is found (got %u)", fc);
        for (k = 0; k < 16u; k++)
            if (sl_wa_mesh_deferred(&oa, k % 8u, fc) == (k >= 8u)) order[n++] = (int) (k % 8u);
        ck(n == 8 && order[0] == 0 && order[1] == 1 && order[2] == 3 && order[3] == 6
           && order[4] == 7 && order[5] == 2 && order[6] == 4 && order[7] == 5,
           "the solid world first and the cutouts and the decals after them, each run in "
           "source order (got %d: %d %d %d %d %d %d %d %d)", n, order[0], order[1], order[2],
           order[3], order[4], order[5], order[6], order[7]);
        ck(sl_wa_mesh_deferred(&oa, 1u, fc) == 0 && sl_wa_mesh_deferred(&oa, 8u, fc) == 0
           && sl_wa_mesh_deferred(NULL, 2u, fc) == 0,
           "a decal before the first cutout keeps its place; out of range is not deferred");
        oa.materials[2].blend = oa.materials[5].blend = SL_WA_BLEND_OPAQUE;
        fc = sl_wa_first_cutout(&oa);
        for (k = 0, n = 0; k < 8u; k++) n += sl_wa_mesh_deferred(&oa, k, fc);
        ck(fc == 8u && n == 0, "an item with no cutout defers nothing - the source's order");
    }
    /* #90 the late translucent draw: an ALPHA draw - a fogged one, a
     * blue-coverage one, a decal lying on a translucent surface - waits for
     * the game's translucent walk; a draw that writes depth, the panorama
     * shell, a decal lying on no translucent surface and every draw with the
     * rule off do not. */
    ck(sl_wa_blend_late(0u, SL_WA_BLEND_ALPHA, 0, 1) == 1
       && sl_wa_blend_late(SL_WA_MAT_FOG, SL_WA_BLEND_ALPHA, 0, 1) == 1
       && sl_wa_blend_late(SL_WA_MAT_DECAL | SL_WA_MAT_FOG, SL_WA_BLEND_ALPHA, 1, 1) == 1
       && sl_wa_blend_late(SL_WA_MAT_COVERAGE_BLUE, SL_WA_BLEND_ALPHA, 0, 1) == 1,
       "a translucent background draw is drawn in the game's translucent walk");
    ck(sl_wa_blend_late(SL_WA_MAT_DECAL | SL_WA_MAT_FOG, SL_WA_BLEND_ALPHA, 0, 1) == 0
       && sl_wa_blend_late(SL_WA_MAT_DECAL, SL_WA_BLEND_ALPHA, 0, 1) == 0,
       "#90 a translucent decal lying on no translucent surface is drawn with its surface, first");
    ck(sl_wa_blend_late(0u, SL_WA_BLEND_ALPHA, 1, 1) == 1
       && sl_wa_blend_late(SL_WA_MAT_FOG, SL_WA_BLEND_ALPHA, 1, 1) == 1,
       "#90 the decal question leaves a material outside the pass alone");
    ck(sl_wa_blend_late(0u, SL_WA_BLEND_OPAQUE, 1, 1) == 0
       && sl_wa_blend_late(SL_WA_MAT_DECAL, SL_WA_BLEND_OPAQUE, 1, 1) == 0
       && sl_wa_blend_late(0u, SL_WA_BLEND_CUTOUT, 1, 1) == 0
       && sl_wa_blend_late(SL_WA_MAT_DECAL, SL_WA_BLEND_CUTOUT, 1, 1) == 0,
       "a draw that writes depth - opaque or cutout - stays in the first draw");
    ck(sl_wa_blend_late(SL_WA_MAT_PANORAMA | SL_WA_MAT_FOG, SL_WA_BLEND_ALPHA, 1, 1) == 0
       && sl_wa_blend_late(SL_WA_MAT_PANORAMA, SL_WA_BLEND_ALPHA, 1, 1) == 0,
       "the panorama shell stays in the first draw");
    ck(sl_wa_blend_late(0u, SL_WA_BLEND_ALPHA, 1, 0) == 0
       && sl_wa_blend_late(SL_WA_MAT_DECAL, SL_WA_BLEND_ALPHA, 1, 0) == 0,
       "with SL_BG_XLU_LATE=0 every draw stays in the first draw");

    /* D-026 THE PANORAMA SHELL: the bit is a background's (refused on a
     * skydome), a bundle without it parses and takes no rule, the rule needs
     * the stage-fog contract and the switch, and the profile is pinned at
     * known points. Synthetic values throughout. */
    g_fix_class = SL_WA_CLASS_BACKGROUND;
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_PANORAMA | SL_WA_MAT_FOG);
    ck(parse_fix(len) == SL_WA_OK, "a PANORAMA material is accepted on a background");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 4, SL_WA_BLEND_ALPHA);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_PANORAMA | SL_WA_MAT_FOG);
    ck(parse_fix(len) == SL_WA_OK, "an ALPHA-blended PANORAMA material is accepted too");
    g_fix_class = SL_WA_CLASS_SKYDOME;
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_PANORAMA);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "a PANORAMA material is refused on a skydome");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_CULL_BACK);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "a CULL_BACK material is refused on a skydome");
    g_fix_class = SL_WA_CLASS_BACKGROUND;
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_CULL_BACK | SL_WA_MAT_FOG);
    ck(parse_fix(len) == SL_WA_OK, "a CULL_BACK material is accepted on a background");
    g_fix_class = SL_WA_CLASS_SKYDOME;
    ck(sl_wa_cull_drops(SL_WA_MAT_CULL_BACK, 1, 1, 0) == 1
       && sl_wa_cull_drops(SL_WA_MAT_CULL_BACK | SL_WA_MAT_DECAL, 1, 1, 0) == 1,
       "a culled material's triangle turned away from the eye is withheld");
    ck(sl_wa_cull_drops(SL_WA_MAT_CULL_BACK, 1, 1, 1) == 0,
       "a culled material's triangle facing the eye draws");
    ck(sl_wa_cull_drops(0u, 1, 1, 0) == 0 && sl_wa_cull_drops(SL_WA_MAT_DECAL | SL_WA_MAT_FOG, 1, 1, 0) == 0,
       "a material without the cull draws both sides");
    ck(sl_wa_cull_drops(SL_WA_MAT_CULL_BACK, 0, 1, 0) == 0,
       "SL_BG_CULL=0 draws both sides");
    ck(sl_wa_cull_drops(SL_WA_MAT_CULL_BACK, 1, 0, 0) == 0,
       "with no eye in hand nothing is withheld");
    {
        struct sl_wa_vertex A, B, C;
        float eye_front[3] = { 0.0f, 0.0f, 5.0f }, eye_back[3] = { 0.0f, 0.0f, -5.0f };
        memset(&A, 0, sizeof A); memset(&B, 0, sizeof B); memset(&C, 0, sizeof C);
        B.x = 1.0f; C.y = 1.0f;                    /* cross(B - A, C - A) = +z */
        ck(!sl_wa_cull_drops(SL_WA_MAT_CULL_BACK, 1, 1, sl_wa_tri_faces(&A, &B, &C, eye_front))
           && sl_wa_cull_drops(SL_WA_MAT_CULL_BACK, 1, 1, sl_wa_tri_faces(&A, &B, &C, eye_back)),
           "the cull keeps the side the authored normal faces and withholds the other");
    }

    /* D-048 THE GAME'S WATER: the bit is a background's, and the two tiles
     * and the blend follow the controller's own arithmetic at known phases
     * (unk_092E50.c: tile 1 at (+90, +150) wrapped at 256 quarter-texels,
     * PRIM_LOD_FRAC = (u32) (sin * 127 + 128)). */
    g_fix_class = SL_WA_CLASS_BACKGROUND;
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_GAME_WATER | SL_WA_MAT_FOG);
    ck(parse_fix(len) == SL_WA_OK, "a GAME_WATER material is accepted on a background");
    g_fix_class = SL_WA_CLASS_SKYDOME;
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_GAME_WATER);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "a GAME_WATER material is refused on a skydome");
    {
        struct sl_wa_water w;
        sl_wa_water_state(0.0f, 0.0f, 0.0f, &w);
        ck(w.du0 == 0.0f && w.dv0 == 0.0f && fabsf(w.du1 + 90.0f / 256.0f) < 1e-6f
           && fabsf(w.dv1 - 150.0f / 256.0f) < 1e-6f && fabsf(w.frac - 128.0f / 255.0f) < 1e-6f,
           "water at phase zero: tile 0 unmoved, tile 1 at (+90, +150) quarter-texels, "
           "PRIM_LOD_FRAC 128");
        sl_wa_water_state(100.5f, 37.25f, 1.5707964f, &w);
        ck(fabsf(w.du0 + 100.5f / 256.0f) < 1e-6f && fabsf(w.dv0 - 37.25f / 256.0f) < 1e-6f
           && fabsf(w.du1 + 190.5f / 256.0f) < 1e-6f && fabsf(w.dv1 - 187.25f / 256.0f) < 1e-6f
           && w.frac == 1.0f,
           "water moves u against the origin and v with it, one item unit per 256 "
           "quarter-texels, the fraction kept on tile 1");
        sl_wa_water_state(200.75f, 130.5f, -1.5707964f, &w);
        ck(fabsf(w.du1 + 34.75f / 256.0f) < 1e-6f && fabsf(w.dv1 - 24.5f / 256.0f) < 1e-6f
           && fabsf(w.frac - 1.0f / 255.0f) < 1e-6f,
           "tile 1 wraps at 256 quarter-texels as the controller's & 0xFF does; "
           "the blend's floor is 1/255");
    }
    ck(sl_wa_panorama_fades(SL_WA_MAT_PANORAMA | SL_WA_MAT_FOG, 1) == 1,
       "a fogged shell material fades while the rule is on");
    ck(sl_wa_panorama_fades(SL_WA_MAT_PANORAMA | SL_WA_MAT_FOG, 0) == 0,
       "SL_SKY_PANORAMA=0 draws the shell as authored");
    ck(sl_wa_panorama_fades(SL_WA_MAT_PANORAMA, 1) == 0,
       "a shell that does not consume the stage fog is never faded");
    ck(sl_wa_panorama_fades(0u, 1) == 0 && sl_wa_panorama_fades(SL_WA_MAT_FOG, 1) == 0
       && sl_wa_panorama_fades(SL_WA_MAT_FOG | SL_WA_MAT_DECAL, 1) == 0
       && sl_wa_panorama_fades(SL_WA_MAT_FOG | SL_WA_MAT_COVERAGE_BLUE, 1) == 0,
       "every other material - and a bundle written before the bit - keeps today's draw");
    ck(sl_wa_panorama_fogged(1.0f) == 1.0f && sl_wa_panorama_fogged(SL_WA_PANO_COVER) == 0.0f
       && sl_wa_panorama_fogged(0.0f) == 0.0f,
       "only a fully fogged shell vertex fades; a partly fogged one keeps the fog equation");
    {
        float f, prev = 0.0f, step = 0.0f;
        for (f = SL_WA_PANO_COVER; f <= 1.0f; f += 0.0005f) {
            float v = sl_wa_panorama_fogged(f);
            if (v - prev > step) step = v - prev;
            prev = v;
        }
        ck(step < 0.02f && fabsf(sl_wa_panorama_fogged(0.5f * (SL_WA_PANO_COVER + 1.0f)) - 0.5f) < 1e-5f,
           "the fade enters smoothly as a shell vertex's fog coordinate reaches full fog, "
           "never in one step (#80)");
    }
    ck(sl_wa_panorama_reach(SL_WA_PANO_REACH) == 0.0f && sl_wa_panorama_reach(0.5f) == 0.0f
       && sl_wa_panorama_reach(1.0f) == 1.0f
       && fabsf(sl_wa_panorama_reach(0.5f * (SL_WA_PANO_REACH + 1.0f)) - 0.5f) < 1e-5f,
       "scenery raises the haze from the reach threshold to full fog, smoothly");
    ck(sl_wa_panorama_taper(10.0f, 20.0f, 40.0f) == 1.0f
       && sl_wa_panorama_taper(-20.0f, 20.0f, 40.0f) == 1.0f
       && fabsf(sl_wa_panorama_taper(30.0f, 20.0f, 40.0f) - 0.5f) < 1e-5f
       && sl_wa_panorama_taper(40.0f, 20.0f, 40.0f) == 0.0f,
       "the azimuth window is flat inside, smooth to zero at its edge");
    {
        const float d = 3.14159265358979f / 180.0f;
        float t10 = tanf(10.0f * d);
        ck(fabsf(sl_wa_panorama_coord(t10, t10, 8.0f)) < 1e-6f
           && fabsf(sl_wa_panorama_coord(tanf(18.0f * d), t10, 8.0f) - 1.0f) < 1e-5f,
           "the fade starts at the haze top and ends the fade width above it");
        ck(sl_wa_panorama_coord(tanf(5.0f * d), t10, 8.0f) < 0.0f
           && sl_wa_panorama_alpha(sl_wa_panorama_coord(tanf(5.0f * d), t10, 8.0f)) == 1.0f,
           "below the haze top the shell keeps its full coverage");
        ck(fabsf(sl_wa_panorama_coord(tanf(8.0f * d), -0.5f, 8.0f) - 1.0f) < 1e-5f,
           "a haze top below the horizon is the horizon");
    }
    ck(sl_wa_panorama_alpha(0.0f) == 1.0f && sl_wa_panorama_alpha(1.0f) == 0.0f
       && fabsf(sl_wa_panorama_alpha(0.5f) - 0.5f) < 1e-6f
       && sl_wa_panorama_alpha(-3.0f) == 1.0f && sl_wa_panorama_alpha(4.0f) == 0.0f,
       "the coverage profile: whole below, none above, a smoothstep between");
    ck(sl_wa_panorama_alpha(0.25f) > sl_wa_panorama_alpha(0.5f)
       && sl_wa_panorama_alpha(0.5f) > sl_wa_panorama_alpha(0.75f)
       && fabsf(sl_wa_panorama_alpha(0.01f) - 1.0f) < 1e-3f
       && fabsf(sl_wa_panorama_alpha(0.99f)) < 1e-3f,
       "monotone, and flat at both ends - no step where the fade starts or ends");

    /* D-023 THE CLOUD FIELD: the flag is a generated skydome material's only,
     * and the cloud coordinate is sky.c's plane and phase. Synthetic values. */
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_GENERATED | SL_WA_MAT_CLOUD_PLANE);
    ck(parse_fix(len) == SL_WA_OK, "a GENERATED CLOUD_PLANE material is accepted on a skydome");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_CLOUD_PLANE);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "CLOUD_PLANE without GENERATED is refused");
    g_fix_class = SL_WA_CLASS_BACKGROUND;
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 8, SL_WA_MAT_GENERATED | SL_WA_MAT_CLOUD_PLANE);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "a CLOUD_PLANE material is refused on a background");
    g_fix_class = SL_WA_CLASS_SKYDOME;
    ck(sl_wa_material_drawn(SL_WA_MAT_GENERATED | SL_WA_MAT_CLOUD_PLANE, 0) == 0
       && sl_wa_material_drawn(SL_WA_MAT_GENERATED | SL_WA_MAT_CLOUD_PLANE, 1) == 1,
       "the cloud field is drawn only while the zenith-cloud switch is on");
    {
        const float up[3] = { 0.0f, 1.0f, 0.0f }, o[3] = { 0.0f, 0.0f, 0.0f };
        float q[3], r[3], e2[3];
        /* straight up from under the middle of a tile: the phase alone moves
         * it, by phase / (32 * 64 / SL_WA_CLOUD_TILES) tiles */
        ck(sl_wa_cloud_coord(up, o, 5000.0f, 0.0f, 64u, 64u, q) == 1
           && q[2] == 1.0f && fabsf(q[0]) < 1e-6f && fabsf(q[1]) < 1e-6f,
           "straight up from the tile's origin samples the tile's origin");
        sl_wa_cloud_coord(up, o, 5000.0f, 256.0f, 64u, 64u, q);
        ck(fabsf(q[1] - 256.0f / (32.0f * 64.0f / (float) SL_WA_CLOUD_TILES)) < 1e-5f
           && fabsf(q[0]) < 1e-6f,
           "the game's phase moves the field along t only, by phase over the tile's length");
        /* the phase IS a translation of the plane: 10 world units of z per unit */
        {
            const float dd[3] = { 0.3f, 0.7f, -0.2f };
            float eA[3] = { 1234.0f, 60.0f, -777.0f }, eB[3];
            eB[0] = eA[0]; eB[1] = eA[1]; eB[2] = eA[2] + 10.0f * 37.0f;
            sl_wa_cloud_coord(dd, eA, 5000.0f, 37.0f, 64u, 64u, q);
            sl_wa_cloud_coord(dd, eB, 5000.0f, 0.0f, 64u, 64u, r);
            ck(fabsf(q[0] - r[0]) < 1e-5f && fabsf(q[1] - r[1]) < 1e-4f && q[2] == r[2],
               "a phase step is the plane moved 10 world units in z per unit - sky.c's t = 0.1 z + phase");
        }
        /* parallax: a ray 45 degrees off the vertical meets the plane its
         * height out, so s moves by 0.1 * height */
        {
            const float d45[3] = { 1.0f, 1.0f, 0.0f };
            sl_wa_cloud_coord(d45, o, 1000.0f, 0.0f, 64u, 64u, q);
            ck(fabsf(q[0] / q[2] - 0.1f * 1000.0f / (32.0f * 64.0f / (float) SL_WA_CLOUD_TILES)) < 1e-5f,
               "a ray is cast onto the game's plane: 45 degrees off the vertical lands one height out");
        }
        /* the wrap: the game takes 4096 off the phase; the field must not jump */
        {
            const float dd[3] = { -0.2f, 0.9f, 0.4f }, ee[3] = { 5753.9f, 227.6f, -2576.1f };
            float a1[3], a2[3], fa, fb;
            sl_wa_cloud_coord(dd, ee, 5000.0f, 4096.25f, 64u, 64u, a1);
            sl_wa_cloud_coord(dd, ee, 5000.0f, 0.25f, 64u, 64u, a2);
            fa = a1[1] / a1[2]; fb = a2[1] / a2[2];
            ck(fabsf((fa - floorf(fa)) - (fb - floorf(fb))) < 1e-4f && a1[0] == a2[0],
               "4096 is a whole number of tiles: the game's wrap of the phase shows no jump");
        }
        /* fixed-tick determinism: the same state is the same coordinate */
        sl_wa_cloud_coord(up, o, 5000.0f, 333.0f, 64u, 64u, q);
        sl_wa_cloud_coord(up, o, 5000.0f, 333.0f, 64u, 64u, r);
        ck(memcmp(q, r, sizeof q) == 0, "the same game state gives the same coordinate");
        /* nothing below the horizon, nothing from above the plane */
        {
            const float down[3] = { 0.0f, -1.0f, 0.0f }, flat[3] = { 1.0f, 0.0f, 0.0f };
            e2[0] = 0.0f; e2[1] = 6000.0f; e2[2] = 0.0f;
            ck(sl_wa_cloud_coord(down, o, 5000.0f, 0.0f, 64u, 64u, q) == 0 && q[2] == 0.0f
               && sl_wa_cloud_coord(flat, o, 5000.0f, 0.0f, 64u, 64u, q) == 0
               && sl_wa_cloud_coord(up, e2, 5000.0f, 0.0f, 64u, 64u, q) == 0
               && sl_wa_cloud_coord(up, o, 5000.0f, 0.0f, 0u, 64u, q) == 0,
               "no coordinate at or below the horizon, from above the plane, or without an image");
        }
    }

    /* ---- #75 the original's state, read by the replacement ------------- */
    {
        /* Vertices 0-2 ordinary, 3-5 behind the eye, 6-7 in front but inside
         * znear. Triangles: ordinary; eye-crossing x2; wholly behind; ordinary
         * again; in front with two vertices inside znear; wholly inside znear;
         * inside znear and behind the eye, nothing ordinary. */
        static const unsigned char cls[8] = { 0, 0, 0, 1, 1, 1, 2, 2 };
        static const unsigned idx[24] = { 0, 1, 2,  0, 1, 3,  0, 3, 4,  3, 4, 5,  2, 1, 0,
                                          0, 6, 7,  6, 7, 6,  6, 3, 4 };
        unsigned out[24];
        unsigned m = sl_wa_near_cut_select(cls, idx, 15u, SL_WA_NEAR_EYE_ONLY, out);
        ck(m == 6u && out[0] == 0u && out[1] == 1u && out[2] == 3u
           && out[3] == 0u && out[4] == 3u && out[5] == 4u,
           "eye-only: the triangles with vertices on both sides of the eye plane, in order");
        ck(sl_wa_near_cut_select(cls, idx, 14u, SL_WA_NEAR_EYE_ONLY, NULL) == 6u,
           "a trailing partial triple is ignored, and counting needs no output");
        m = sl_wa_near_cut_select(cls, idx, 24u, 0u, out);
        ck(m == 15u && out[6] == 0u && out[7] == 6u && out[8] == 7u
           && out[9] == 6u && out[12] == 6u && out[13] == 3u,
           "the near-cut contract adds the triangles in front of the eye but inside znear");
        ck(sl_wa_near_cut_select(cls, idx, 15u, 0u, NULL) == 6u,
           "a triangle wholly behind the eye or wholly ordinary is never selected");
    }
    {
        static const float c_in[4] = { 0.0f, 0.0f, -3.0f, 2.0f };
        static const float c_ok[4] = { 0.0f, 0.0f, 1.0f, 2.0f };
        static const float c_on[4] = { 0.0f, 0.0f, -2.0f, 2.0f };
        static const float c_bh[4] = { 0.0f, 0.0f, 5.0f, -1.0f };
        static const float c_w0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        ck(sl_wa_near_class(c_in) == SL_WA_NEAR_INSIDE && sl_wa_near_class(c_ok) == 0u
           && sl_wa_near_class(c_on) == 0u && sl_wa_near_class(c_bh) == SL_WA_NEAR_BEHIND
           && sl_wa_near_class(c_w0) == SL_WA_NEAR_BEHIND,
           "near classes: inside znear (z < -w), on the plane is kept, at or behind the eye");
    }
    {
        /* Two fabricated fixtures 1000 apart; the game's darkened vertices
         * sit on the first. Triangle 0 and 1 are the first fixture's, 2 has a
         * vertex out of reach, 3 is the second fixture. */
        static const struct sl_wa_vertex v[10] = {
            {   0.0f, 0.0f,  0.0f, 0, 0, 200, 200, 200, 255 },
            {  10.0f, 0.0f,  0.0f, 0, 0, 200, 200, 200, 255 },
            {   0.0f, 0.0f, 10.0f, 0, 0, 200, 200, 200, 255 },
            {  10.0f, 0.0f, 10.0f, 0, 0, 200, 200, 200, 255 },
            {  60.0f, 0.0f,  0.0f, 0, 0, 200, 200, 200, 255 },
            {1000.0f, 0.0f,  0.0f, 0, 0, 200, 200, 200, 255 },
            {1010.0f, 0.0f,  0.0f, 0, 0, 200, 200, 200, 255 },
            {1000.0f, 0.0f, 10.0f, 0, 0, 200, 200, 200, 255 },
            {   5.0f, 5.0f,  5.0f, 0, 0, 200, 200, 200, 255 },
            {   0.0f, 0.0f,  0.0f, 0, 0, 200, 200, 200, 255 }
        };
        static const unsigned idx[12] = { 0, 1, 2,  1, 3, 2,  1, 4, 3,  5, 6, 7 };
        static const float pts[2][3] = { { 0.0f, 0.0f, 0.0f }, { 10.0f, 0.0f, 10.0f } };
        unsigned char mark[10];
        const unsigned nt = sl_wa_light_mark(v, 10u, idx, 12u, pts, 2u, 23.36f, mark);
        ck(nt == 2u && mark[0] && mark[1] && mark[2] && mark[3] && !mark[4],
           "a triangle wholly within the game's fixture reach is the fixture's; one reaching past it is not");
        ck(!mark[5] && !mark[6] && !mark[7],
           "another fixture of the same item keeps its light");
        ck(!mark[8] && !mark[9],
           "a vertex within reach but on no fixture triangle is left alone");
        ck(sl_wa_light_mark(v, 10u, idx, 12u, pts, 0u, 23.36f, mark) == 0u && !mark[0],
           "no shot-out vertex, nothing dimmed");
        ck(sl_wa_light_mark(v, 10u, idx, 12u, pts, 2u, 0.0f, mark) == 0u,
           "no reach, nothing dimmed");
    }
    {
        /* A camera at (100, 20, -50) looking down -z (world-to-view with the
         * identity rotation), render origin (40, 0, 10), visibility 0.2. */
        static const float wv[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  -100, -20, 50, 1 };
        static const float ro[3] = { 40.0f, 0.0f, 10.0f };
        float e[3];
        sl_wa_fold_eye(wv, ro, 0.2f, e);
        ck(fabsf(e[0] - 12.0f) < 1e-5f && fabsf(e[1] - 4.0f) < 1e-5f && fabsf(e[2] + 12.0f) < 1e-5f,
           "the folded projection's eye is (camera - render origin) x visibility");
    }
    {
        /* Authored-redraw quad recognition. p, q, r, s go round the quad;
         * the first pass cut it along q-s, the redraw along p-r. */
        static const float p[3] = { 0, 0, 0 }, q[3] = { 100, 0, 0 },
                           r[3] = { 100, 100, 0 }, s[3] = { 0, 100, 0 };
        static const float sb[3] = { 0, 100, 12 };     /* s lifted: a gently bent quad */
        /* A box corner: q-r and p-q on the x = 0 face, s on the face
         * perpendicular to it through q. */
        static const float bp[3] = { 0, 50, 70 }, bq[3] = { 0, -10, 70 },
                           br[3] = { 0, -10, -10 }, bs[3] = { -13, -10, 70 };
        static const float far_s[3] = { 0, 100, 300 };  /* crosses in plan, far off the plane */
        ck(sl_wa_redraw_quad_ok(p, q, r, s) == 1,
           "a planar quad re-cut along its other diagonal is one surface");
        ck(sl_wa_redraw_quad_ok(p, q, r, sb) == 1,
           "a gently bent quad (the dome's non-planar skins) is still one surface");
        ck(sl_wa_redraw_quad_ok(bp, bq, br, bs) == 0,
           "a box corner - the fourth vertex on a perpendicular face - is not a quad");
        ck(sl_wa_redraw_quad_ok(p, q, r, far_s) == 0,
           "diagonals whose closest approach is far apart are not one surface");
        ck(sl_wa_redraw_quad_ok(p, q, r, r) == 0 && sl_wa_redraw_quad_ok(p, p, r, s) == 0,
           "degenerate and parallel diagonals are refused");
    }

    /* ---- #68 THE REMASTER STAGE-FOG METADATA (SLEN) ---------------------
     *
     * Every row below is FABRICATED: ids, ranges and colours chosen for the
     * test, not read from any executable. */
    {
        static unsigned char e[SL_WA_ENV_HEADER + 3u * SL_WA_ENV_ROW];
        struct sl_wa_env rows[4];
        unsigned n = 99;
        const unsigned el = (unsigned) sizeof e;
#define ENV_ROW(i, id, s, en, r, g, b) do { \
            unsigned char *q_ = e + SL_WA_ENV_HEADER + (i) * SL_WA_ENV_ROW; \
            put_u32(q_, (id)); put_f32(q_ + 4, (s)); put_f32(q_ + 8, (en)); \
            q_[12] = (r); q_[13] = (g); q_[14] = (b); q_[15] = 0; } while (0)
#define ENV_FRESH() do { memset(e, 0, sizeof e); \
            e[0] = 'S'; e[1] = 'L'; e[2] = 'E'; e[3] = 'N'; \
            put_u32(e + 4, SL_WA_ENV_VERSION); put_u32(e + 8, 3); \
            put_u32(e + 12, SL_WA_ENV_ROW); put_u32(e + 16, el); \
            ENV_ROW(0, 7, -10.0f, 2000.0f, 10, 20, 30); \
            ENV_ROW(1, 907, -10.0f, 9000.0f, 1, 2, 3); \
            ENV_ROW(2, 207, 0.0f, 500.0f, 200, 100, 50); } while (0)
        ENV_FRESH();
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_OK && n == 3,
           "a well-formed SLEN file parses, every row");
        ck(rows[1].id == 907 && rows[1].fog_end == 9000.0f && rows[1].rgb[2] == 3,
           "a row decodes to its id, range and colour");
        ENV_FRESH(); e[3] = 'X';
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_MAGIC, "a wrong magic is refused");
        ENV_FRESH(); put_u32(e + 4, 2);
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_VERSION, "a later version is refused");
        ENV_FRESH();
        ck(sl_wa_env_parse(e, el - 1u, rows, 4, &n) == SL_WA_E_SIZE,
           "a truncated file is refused - the real length decides");
        ENV_FRESH(); put_u32(e + 8, 0);
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_COUNT, "zero rows is refused");
        ENV_FRESH();
        ck(sl_wa_env_parse(e, el, rows, 2, &n) == SL_WA_E_COUNT,
           "more rows than the caller can hold is refused, not truncated");
        ENV_FRESH(); ENV_ROW(1, 7, -10.0f, 9000.0f, 1, 2, 3);
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_LAYOUT,
           "a duplicated environment id is refused - the selector must be exact");
        ENV_FRESH(); ENV_ROW(2, 207, 500.0f, 500.0f, 1, 2, 3);
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_LAYOUT,
           "a range with no extent is refused");
        ENV_FRESH(); { unsigned char *q = e + SL_WA_ENV_HEADER + 4;
                       put_u32(q, 0x7fc00000u); }
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_LAYOUT, "a NaN fog start is refused");
        ENV_FRESH(); e[SL_WA_ENV_HEADER + 15] = 1;
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_FLAGS, "a row flag bit is refused");
        ENV_FRESH(); put_u32(e + 24, 1);
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_FLAGS, "a reserved header word is refused");
        ENV_FRESH(); put_u32(e + 12, 12);
        ck(sl_wa_env_parse(e, el, rows, 4, &n) == SL_WA_E_LAYOUT, "a foreign row size is refused");
#undef ENV_FRESH
#undef ENV_ROW
        /* THE FORMULA: 1 - saturate((end - d) / (end - start)), in the GL
         * LINEAR(0..1) coordinate the renderer hands the fog stage. */
        {
            struct sl_wa_env r;
            r.id = 1; r.fog_start = -100.0f; r.fog_end = 900.0f;
            r.rgb[0] = r.rgb[1] = r.rgb[2] = 0;
            ck(sl_wa_env_fog_coord(&r, 900.0f) == 1.0f,
               "at the fog end the coordinate is 1 - the fog colour");
            ck(sl_wa_env_fog_coord(&r, 5000.0f) == 1.0f, "past it, still 1");
            ck(sl_wa_env_fog_coord(&r, -100.0f) == 0.0f,
               "at the fog start the coordinate is 0 - the material untouched");
            ck(fabsf(sl_wa_env_fog_coord(&r, 400.0f) - 0.5f) < 1e-6f,
               "and it is linear between them");
            ck(fabsf(sl_wa_env_fog_coord(&r, 0.0f) - 0.1f) < 1e-6f,
               "the eye itself is already 10%% of the way here, as the "
               "formula says for a negative start");
            ck(sl_wa_env_fog_coord((const struct sl_wa_env *) 0, 400.0f) == 0.0f,
               "no row is no fog");
        }
        /* D-025 THE PRESENTED ROW: only the end moves, only for the row the
         * policy names, and the authentic switch is the authored row. */
        {
            struct sl_wa_env a, p, q;
            a.id = 33u; a.fog_start = -100.0f; a.fog_end = 15000.0f;
            a.rgb[0] = 133; a.rgb[1] = 173; a.rgb[2] = 202;
            p = sl_wa_env_presented(&a, "xbla", 0);
            ck(p.fog_end == 54537.0f, "xbla row 33 is presented to 54537");
            ck(p.id == a.id && p.fog_start == a.fog_start && p.rgb[0] == 133
               && p.rgb[1] == 173 && p.rgb[2] == 202,
               "the presented row keeps the authored id, start and colour");
            q = sl_wa_env_presented(&a, "xbla", 1);
            ck(memcmp(&q, &a, sizeof a) == 0,
               "the authentic switch presents the authored row exactly");
            ck(fabsf(sl_wa_env_fog_coord(&p, 22051.0f)
                     - (1.0f - (54537.0f - 22051.0f) / 54637.0f)) < 1e-6f
               && sl_wa_env_fog_coord(&q, 22051.0f) == 1.0f,
               "one distance equation: a point 22051 units out is 59%% "
               "visible presented and fog authored");
            ck(sl_wa_env_fog_coord(&p, 54537.0f) == 1.0f
               && sl_wa_env_fog_coord(&p, 62422.0f) == 1.0f,
               "the presented end is still the end: the outer ring stays fog");
            q = sl_wa_env_presented(&a, "", 0);
            ck(q.fog_end == 15000.0f, "no set: the authored end");
            a.id = 933u; a.fog_end = 75000.0f;
            q = sl_wa_env_presented(&a, "xbla", 0);
            ck(q.fog_end == 75000.0f, "the intro cinema row keeps its authored end");
        }
        /* D-043 STATUE: only the start moves (to 800), the authentic switch is
         * the authored row, a point nearer than the new start is clear, and
         * a point at the game's far-fog prop cut (3476.3) is still 99% fog. */
        {
            struct sl_wa_env a, p, q;
            memset(&a, 0, sizeof a);
            a.id = 22u; a.fog_start = -100.0f; a.fog_end = 3500.0f;
            a.rgb[0] = 0; a.rgb[1] = 0; a.rgb[2] = 8;
            p = sl_wa_env_presented(&a, "xbla", 0);
            ck(p.fog_start == 800.0f && p.fog_end == 3500.0f && p.id == 22u
               && p.rgb[0] == 0 && p.rgb[1] == 0 && p.rgb[2] == 8,
               "xbla row 22 starts at 800, end, id and colour kept");
            q = sl_wa_env_presented(&a, "xbla", 1);
            ck(memcmp(&q, &a, sizeof a) == 0,
               "the authentic switch presents Statue's authored row exactly");
            ck(sl_wa_env_fog_coord(&p, 700.0f) == 0.0f
               && sl_wa_env_fog_coord(&q, 700.0f) > 0.2f
               && sl_wa_env_fog_coord(&p, 3476.3f) > 0.99f,
               "700 units out is clear presented (%.3f authored), the prop cut %.4f fogged",
               (double) sl_wa_env_fog_coord(&q, 700.0f), (double) sl_wa_env_fog_coord(&p, 3476.3f));
            q = sl_wa_env_presented(&a, "", 0);
            ck(memcmp(&q, &a, sizeof a) == 0, "no set presents Statue's authored row");
        }
        /* D-029 SURFACE: a SHORTENED row - only the end moves, the authentic
         * switch is the authored row, and a point at the game's far-fog prop
         * cut (12071.2) is already full fog. */
        {
            struct sl_wa_env a, p, q;
            a.id = 36u; a.fog_start = -100.0f; a.fog_end = 45000.0f;
            a.rgb[0] = 164; a.rgb[1] = 150; a.rgb[2] = 130;
            p = sl_wa_env_presented(&a, "xbla", 0);
            ck(p.fog_end == 12071.0f && p.id == 36u && p.fog_start == -100.0f
               && p.rgb[0] == 164 && p.rgb[1] == 150 && p.rgb[2] == 130,
               "xbla row 36 is presented to 12071, id, start and colour kept");
            q = sl_wa_env_presented(&a, "xbla", 1);
            ck(memcmp(&q, &a, sizeof a) == 0,
               "the authentic switch presents Surface's authored row exactly");
            ck(sl_wa_env_fog_coord(&p, 12071.2f) == 1.0f
               && fabsf(sl_wa_env_fog_coord(&q, 12071.2f)
                        - (12171.2f / 45100.0f)) < 1e-5f,
               "at the game's prop cut the presented row is full fog; the "
               "authored row there is 27%% fog");
            /* D-030: the colour from the game, the range the presented one. */
            {
                static const float game[3] = { 96.0f, 96.0f, 128.0f },
                                   wild[3] = { -4.0f, 255.6f, 127.5f };
                struct sl_wa_env c = sl_wa_env_coloured(&p, game);
                ck(c.id == 36u && c.fog_start == -100.0f && c.fog_end == 12071.0f
                   && c.rgb[0] == 96 && c.rgb[1] == 96 && c.rgb[2] == 128,
                   "the game's colour replaces the row's; id, start and end stay");
                c = sl_wa_env_coloured(&p, wild);
                ck(c.rgb[0] == 0 && c.rgb[1] == 255 && c.rgb[2] == 128,
                   "the game's colour is clamped to 0..255 and rounded");
            }
        }
        /* D-030 THE GAME'S CLOUD SHADE (sky.c): Surface's fog 96 96 128 and
         * cloud 240 120 30. Overhead the full cloud over the fog, at the
         * horizon and below the fog colour, half way at tan 0.25. */
        {
            static const float fog[3] = { 96.0f, 96.0f, 128.0f },
                               cl[3] = { 240.0f, 120.0f, 30.0f },
                               up[3] = { 0.0f, 1.0f, 0.0f }, at27[3] = { 2.0f, 1.0f, 0.0f },
                               hor[3] = { 1.0f, 0.0f, 0.0f }, down[3] = { 0.0f, -1.0f, 1.0f },
                               half[3] = { 0.0f, 0.25f, 1.0f };
            float s[3], t[3];
            sl_wa_sky_shade(fog, cl, up, s);
            ck(fabsf(s[0] - (96.0f + 240.0f * (1.0f - 96.0f / 255.0f))) < 0.01f
               && fabsf(s[1] - (96.0f + 120.0f * (1.0f - 96.0f / 255.0f))) < 0.01f
               && fabsf(s[2] - (128.0f + 30.0f * (1.0f - 128.0f / 255.0f))) < 0.01f,
               "overhead: fog + cloud * (1 - fog / 255) (%.1f %.1f %.1f)",
               (double) s[0], (double) s[1], (double) s[2]);
            sl_wa_sky_shade(fog, cl, at27, t);
            ck(fabsf(t[0] - s[0]) < 0.05f && fabsf(t[2] - s[2]) < 0.05f,
               "at tan 0.5 (26.6 degrees) the shade is already the full cloud");
            sl_wa_sky_shade(fog, cl, hor, t);
            ck(fabsf(t[0] - 96.0f) < 0.01f && fabsf(t[1] - 96.0f) < 0.01f && fabsf(t[2] - 128.0f) < 0.01f,
               "at the horizon the shade is the fog colour");
            sl_wa_sky_shade(fog, cl, down, t);
            ck(fabsf(t[0] - 96.0f) < 0.01f && fabsf(t[2] - 128.0f) < 0.01f,
               "below the horizon it stays the fog colour");
            sl_wa_sky_shade(fog, cl, half, t);
            ck(fabsf(t[0] - (96.0f + 0.5f * (s[0] - 96.0f))) < 0.1f,
               "at tan 0.25 it is half way (%.1f)", (double) t[0]);
            {
                float u[3];
                sl_wa_sky_shade_frac(fog, cl, 1.0 - 2.0 * 0.25 / (1.0 + 0.0001), u);
                ck(u[0] == t[0] && u[1] == t[1] && u[2] == t[2],
                   "D-046: a fraction given directly shades as the direction's does");
            }
        }
        /* D-046 THE GAME'S CLOUD LAYER ON THE SCREEN: Streets' measured shape -
         * rows 10..230, the top at fraction 0.389, the horizon edge at row
         * 90.6 (left) and 90.0 (right), the fill below it. */
        {
            const float rows[10] = { 10.0f, 220.0f,
                                     10.0f, 0.389f, 90.6f, 1.0f,
                                     10.0f, 0.389f, 90.0f, 1.0f };
            const int fill[4] = { 0, 1, 0, 1 };
            const int nofill[4] = { 0, 0, 0, 0 };
            float ey[2];
            double f;
            f = sl_wa_sky_screen_frac(rows, fill, -1.0f, 1.0f);
            ck(fabs(f - 0.389) < 1e-5, "the top-left corner takes the game's fraction (%.4f)", f);
            f = sl_wa_sky_screen_frac(rows, fill, -1.0f, 1.0f - 2.0f * (50.3f - 10.0f) / 220.0f);
            ck(fabs(f - (0.389 + 0.5 * (1.0 - 0.389))) < 1e-4,
               "half way down to the edge, half way to the fog (%.4f)", f);
            ck(sl_wa_sky_screen_frac(rows, fill, -1.0f, 0.0f) == 1.0
               && sl_wa_sky_screen_frac(rows, fill, 0.3f, -0.9f) == 1.0,
               "below the edge is the fill: the fog colour");
            ck(sl_wa_sky_screen_frac(rows, fill, -1.0f, 1.5f) == 0.0,
               "the line continues above the picture, held to full cloud");
            f = sl_wa_sky_screen_frac(rows, nofill, -1.0f, -0.5f);
            ck(f == 1.0, "with no fill the line continues past the edge, held at 1 (%.4f)", f);
            ck(sl_wa_sky_screen_edge(rows, fill, ey)
               && fabsf(ey[0] - (1.0f - 2.0f * 80.6f / 220.0f)) < 1e-5f
               && fabsf(ey[1] - (1.0f - 2.0f * 80.0f / 220.0f)) < 1e-5f,
               "the edge in NDC at the two sides");
            ck(!sl_wa_sky_screen_edge(rows, nofill, ey),
               "no fill below on both sides draws no fill");
        }
        /* #77 THE BLENDED ROW (fabricated rows, Facility's shape). */
        {
            struct sl_wa_env m, al, r;
            m.id = 34u; m.fog_start = -100.0f; m.fog_end = 5000.0f;
            m.rgb[0] = 16; m.rgb[1] = 32; m.rgb[2] = 1;
            al.id = 134u; al.fog_start = -100.0f; al.fog_end = 1000.0f;
            al.rgb[0] = 64; al.rgb[1] = 128; al.rgb[2] = 64;
            r = sl_wa_env_blend(&m, &al, 0.0f);
            ck(r.id == 34u && r.fog_end == 5000.0f && r.rgb[0] == 16 && r.rgb[2] == 1,
               "no blend is the main row exactly");
            r = sl_wa_env_blend(&m, &al, 1.0f);
            ck(r.id == 34u && r.fog_start == -100.0f && r.fog_end == 1000.0f
               && r.rgb[0] == 64 && r.rgb[1] == 128 && r.rgb[2] == 64,
               "a full blend is the alt row's range and colour, under the main row's id");
            r = sl_wa_env_blend(&m, &al, 0.5f);
            ck(r.fog_end == 3000.0f && r.rgb[0] == 40 && r.rgb[1] == 80 && r.rgb[2] == 33,
               "half way is half of each, the colour rounded to the nearest step");
            r = sl_wa_env_blend(&m, &al, 7.0f);
            ck(r.fog_end == 1000.0f, "past 1 is held at the alt row");
            r = sl_wa_env_blend(&m, &al, -0.5f);
            ck(r.fog_end == 5000.0f && r.rgb[1] == 32, "below 0 is the main row");
        }
    }
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 100) + 12, 1);
    ck(parse_fix(len) == SL_WA_E_MATERIAL, "a material with a non-zero reserved field is refused");

    /* ---- THE ALPHA-REJECT CONTRACT --------------------------------------
     *
     * Which class takes an alpha test is a RENDER-STATE question, not a
     * format one - no byte of the bundle moved for it - so what is checked
     * here is the predicate the draw loop actually branches on, rather than
     * a re-statement of it.
     *
     * The contract: CUTOUT rejects, because it is drawn opaque and cannot
     * blend its transparent texels away. ALPHA does not, because its alpha
     * IS the blend factor and a texel at zero already contributes nothing.
     * The 0.33 reference entered for the foliage, at a time when foliage
     * reached the ALPHA branch; the CUTOUT split moved that whole population
     * across and the reference stayed behind, which deleted four of the Dam
     * item's translucent materials outright. See SL_WA_BLEND_REJECTS. */
    ck(!SL_WA_BLEND_REJECTS(SL_WA_BLEND_OPAQUE),
       "an OPAQUE material installs no alpha reject");
    ck(!SL_WA_BLEND_REJECTS(SL_WA_BLEND_ALPHA),
       "a TRANSLUCENT material installs no alpha reject - its alpha is the blend");
    ck(SL_WA_BLEND_REJECTS(SL_WA_BLEND_CUTOUT),
       "a CUTOUT material still installs the alpha reject - its alpha is a shape");
    /* TOTALITY is what says no draw can inherit the one before it: every
     * accepted blend value has an answer, so the loop installs a state on
     * every mesh either way and none is ever left over from the last. */
    {
        unsigned b;
        int seen[3];
        int leaked = 0;
        seen[0] = SL_WA_BLEND_REJECTS(SL_WA_BLEND_OPAQUE);
        seen[1] = SL_WA_BLEND_REJECTS(SL_WA_BLEND_ALPHA);
        seen[2] = SL_WA_BLEND_REJECTS(SL_WA_BLEND_CUTOUT);
        /* walk CUTOUT -> ALPHA -> CUTOUT -> OPAQUE -> ALPHA and check that
         * each step's state depends only on that step's own class */
        {
            static const unsigned seq[5] = {
                SL_WA_BLEND_CUTOUT, SL_WA_BLEND_ALPHA, SL_WA_BLEND_CUTOUT,
                SL_WA_BLEND_OPAQUE, SL_WA_BLEND_ALPHA };
            unsigned i2;
            for (i2 = 0; i2 < 5; i2++)
                if (SL_WA_BLEND_REJECTS(seq[i2]) != seen[seq[i2]]) leaked = 1;
        }
        ck(!leaked,
           "CUTOUT->TRANSLUCENT clears the reject, TRANSLUCENT->CUTOUT restores "
           "it, and no draw inherits the previous draw's alpha-test state");
        for (b = 0; b < 3u; b++)
            ck(SL_WA_BLEND_REJECTS(b) == 0 || SL_WA_BLEND_REJECTS(b) == 1,
               "every accepted blend value has an alpha-reject answer");
    }
    ck(SL_WA_ALPHA_REF > 0.0f && SL_WA_ALPHA_REF < 1.0f,
       "the CUTOUT alpha reference is a usable fraction");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 104), SL_WA_MAX_TEX_DIM + 1u);
    ck(parse_fix(len) == SL_WA_E_TEXTURE, "an oversized image is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 104) + 12, 17);
    ck(parse_fix(len) == SL_WA_E_TEXTURE, "an image payload that is not w*h*4 is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 104) + 8, len - 4u);
    ck(parse_fix(len) == SL_WA_E_TEXTURE, "an image payload running past the end is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 104), 0);
    ck(parse_fix(len) == SL_WA_E_TEXTURE, "a zero-sized image is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 104), 0x10000u);
    put_u32(g_fix + rd_u32(g_fix + 104) + 4, 0x10000u);
    ck(parse_fix(len) == SL_WA_E_TEXTURE,
       "an image whose w*h*4 would overflow is refused before the product");

    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 108), 3);   /* mesh -> material 3 */
    ck(parse_fix(len) == SL_WA_E_MESH, "a mesh naming a material that is not there is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 108) + 8, 5);
    ck(parse_fix(len) == SL_WA_E_MESH, "a mesh whose index count is not a multiple of 3 is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 108) + 4, 3);
    ck(parse_fix(len) == SL_WA_E_MESH, "a mesh range running past the index array is refused");
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 108) + 8, 0);
    ck(parse_fix(len) == SL_WA_E_MESH, "an empty mesh is refused");

    /* An index out of range is caught at LOAD, by the decode pass. */
    len = build("dam", 4, 2, 1, 1, 1);
    put_u32(g_fix + rd_u32(g_fix + 96), 99);
    ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK, "an out-of-range index parses the tables");
    ck(sl_wa_decode_arrays(g_fix, &a) == SL_WA_E_INDEX,
       "an out-of-range index is caught before it reaches the renderer");

    /* ---- the policy: ACTIVATION FOLLOWS THE PRESENTATION (#68, 2026-09-23)
     *
     * The whole point of the coordination round is that the NORMAL path sets
     * no environment variable at all, so every check here runs with none. */
    ck(getenv("SL_WORLD_ASSETS") == NULL || getenv("SL_WORLD_ASSETS")[0] == '\0',
       "the fixture starts with no SL_WORLD_ASSETS override");
    ck(sl_presentation() == SL_PRESENTATION_ORIGINAL,
       "an inactive store answers PRESENTATION ORIGINAL");
    ck(sl_wa_active() == 0, "PRESENTATION ORIGINAL selects no world assets");
    ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "dam") == NULL,
       "an inactive provider answers NULL without opening anything");
    ck(sl_wa_lookups() == 0, "an inactive provider does not even count a lookup");

    sl_presentation_set(SL_PRESENTATION_COMMUNITY);
    ck(sl_wa_active() == 0,
       "PRESENTATION COMMUNITY HD selects no world assets either - the community "
       "pack is a texture pack, so the Dam keeps its own sky");
    ck(sl_wa_lookups() == 0, "and still nothing is opened");

    sl_presentation_set(SL_PRESENTATION_XBLA);
    ck(sl_wa_active() == 1,
       "PRESENTATION XBLA selects the xbla set with no environment variable at all");
    ck(strcmp(sl_wa_set(), "xbla") == 0, "and the set is named xbla");

    /* LIVE SWITCHING. The answer moves with the setting, in both directions,
     * at the next ask - which is what one F5 press does mid-frame. */
    {
        unsigned g0 = sl_wa_generation();
        sl_presentation_set(SL_PRESENTATION_ORIGINAL);
        ck(sl_wa_active() == 0 && sl_wa_generation() == g0 + 1u,
           "leaving XBLA goes dark at the very next ask, and the move is counted");
        sl_presentation_set(SL_PRESENTATION_COMMUNITY);
        ck(sl_wa_active() == 0 && sl_wa_generation() == g0 + 1u,
           "ORIGINAL to COMMUNITY is not a world-asset move: no second bump");
        sl_presentation_set(SL_PRESENTATION_XBLA);
        ck(sl_wa_active() == 1 && sl_wa_generation() == g0 + 2u,
           "returning to XBLA lights it again - the selection is re-asked, never latched");
    }

    /* THE DIAGNOSTIC OVERRIDE pins THIS provider ahead of the presentation,
     * in both directions, and says so on stderr when it disagrees. */
    putenv((char *) "SL_WORLD_ASSETS=off");
    ck(sl_wa_active() == 0,
       "the override pins the provider dark under PRESENTATION XBLA");
    sl_presentation_set(SL_PRESENTATION_ORIGINAL);
    putenv((char *) "SL_WORLD_ASSETS=xbla");
    ck(sl_wa_active() == 1,
       "the override pins the provider lit under PRESENTATION ORIGINAL");
    putenv((char *) "SL_WORLD_ASSETS=");
    ck(sl_wa_active() == 0,
       "an EMPTY override is no override: the presentation decides again");
    putenv((char *) "SL_WORLD_ASSETS=nonsense");
    ck(sl_wa_active() == 0,
       "an unrecognised override word leaves the provider dark, never guessing a set");

    /* ---- the provider, against files ---------------------------------- */
    /* The rest of the fixture needs the provider lit; the coordinated path
     * and the override agree here, so no mixed-state line is printed. */
    sl_presentation_set(SL_PRESENTATION_XBLA);
    putenv((char *) "SL_WORLD_ASSETS=xbla");
    ck(sl_wa_active() == 1, "PRESENTATION XBLA and the override agreeing: the set is selected");

    snprintf(sub, sizeof sub, "%s\\wa", t_dir);           MKDIR(sub);
    {
        char e[700];
        snprintf(e, sizeof e, "SL_WORLDASSET_ROOT=%s", sub);
        putenv(strdup(e));
    }
    g_root_done = 0; g_root_from_env = 0;
    {
        char d[700];
        snprintf(d, sizeof d, "%s\\xbla", sub);           MKDIR(d);
        snprintf(d, sizeof d, "%s\\xbla\\skydome", sub);  MKDIR(d);
    }

    ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "facility") == NULL,
       "a stage with no file provided is a miss, not a fault");
    ck(sl_wa_missing() == 1, "the miss is counted once");
    ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "facility") == NULL && sl_wa_missing() == 1,
       "a second lookup of the same miss opens nothing");

    snprintf(file, sizeof file, "%s\\xbla\\skydome\\dam.slwa", sub);
    len = build("dam", 4, 2, 1, 1, 1);
    wr(file, g_fix, len);
    {
        const struct sl_wa_asset *got = sl_wa_lookup(SL_WA_CLASS_SKYDOME, "dam");
        ck(got != NULL, "a valid file is found and loaded");
        if (got != NULL) {
            ck(got->vertex_count == 4 && got->index_count == 6,
               "the loaded asset carries its geometry");
            ck(got->indices[0] == 0 && got->indices[5] == 1,
               "the indices decode to host order");
            ck(got->vertices[1].x == 1.0f && got->vertices[1].r == 11,
               "the vertices decode to host order");
        }
        ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "dam") == got,
           "a second lookup answers from the cache");
        ck(sl_wa_loaded() == 1, "the file was read once");
    }

    /* A file whose embedded path is not the one being asked for. */
    snprintf(file, sizeof file, "%s\\xbla\\skydome\\runway.slwa", sub);
    len = build("dam", 4, 2, 1, 1, 1);
    wr(file, g_fix, len);
    ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "runway") == NULL,
       "a file that names another item is refused rather than drawn");

    /* A malformed file at a name that is asked for. */
    snprintf(file, sizeof file, "%s\\xbla\\skydome\\caverns.slwa", sub);
    len = build("caverns", 4, 2, 1, 1, 1);
    g_fix[0] = 'Z';
    wr(file, g_fix, len);
    {
        unsigned before = sl_wa_invalid();
        ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "caverns") == NULL,
           "a malformed file is rejected, not drawn");
        ck(sl_wa_invalid() == before + 1u, "it is counted invalid exactly once");
        ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "caverns") == NULL &&
           sl_wa_invalid() == before + 1u,
           "a second lookup of a bad file costs no second diagnostic");
    }

    /* A truncated file on disk. */
    snprintf(file, sizeof file, "%s\\xbla\\skydome\\temple.slwa", sub);
    len = build("temple", 4, 2, 1, 1, 1);
    wr(file, g_fix, len - 20u);
    ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "temple") == NULL,
       "a truncated file on disk is rejected, not read past");

    /* ---- THE BACKGROUND CLASS THROUGH THE PROVIDER ----------------------
     *
     * A REPLACEMENT's fallback rule is the inverse of an additive asset's: a
     * missing skydome costs a picture that was never there, a missing
     * background would cost a HOLE in the world. So what these pin is that
     * the provider answers NULL in every case where the replacement is not
     * there and complete - because NULL is what leaves the original drawing,
     * and the renderer arms its suppression from nothing else. */
    {
        char d[700];
        const struct sl_wa_asset *bg;
        snprintf(d, sizeof d, "%s\\xbla\\background", sub); MKDIR(d);

        ck(sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "dam") == NULL,
           "MISSING background: the provider answers nothing, so the original draws");

        snprintf(file, sizeof file, "%s\\xbla\\background\\dam.slwa", sub);
        g_fix_class = SL_WA_CLASS_BACKGROUND;
        len = build("dam", 64, 200, 40, 50, 60);
        g_fix[5] = 0xff;                                  /* break the version */
        wr(file, g_fix, len);
        ck(sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "dam") == NULL,
           "INVALID background: still nothing, so the original still draws");

        /* The cache remembers the invalid outcome, so write the good one under
         * a second item path rather than fight it. */
        snprintf(file, sizeof file, "%s\\xbla\\background\\silo.slwa", sub);
        len = build("silo", 64, 200, 40, 50, 60);
        wr(file, g_fix, len);
        bg = sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "silo");
        ck(bg != NULL, "VALID background: the provider answers it");
        if (bg != NULL) {
            ck(bg->asset_class == SL_WA_CLASS_BACKGROUND,
               "and it is the background class, so the renderer draws it as one");
            ck(bg->mesh_count == 60 && bg->material_count == 50 &&
               bg->texture_count == 40,
               "a whole level's mesh, material and image counts survive the trip");
        }
        ck(sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "silo") == bg,
           "a second background lookup answers from the cache");

        /* The class is part of the identity: a skydome file cannot answer for
         * a background, which is what keeps the two providers' files apart. */
        ck(sl_wa_lookup(SL_WA_CLASS_SKYDOME, "silo") == NULL,
           "a background file does not answer a skydome lookup");

        /* And the PRESENTATION gates it exactly as it gates the skydome. */
        putenv((char *) "SL_WORLD_ASSETS=");
        sl_presentation_set(SL_PRESENTATION_ORIGINAL);
        ck(sl_wa_active() == 0 && sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "silo") == NULL,
           "PRESENTATION ORIGINAL: no background is offered, so nothing is suppressed");
        sl_presentation_set(SL_PRESENTATION_COMMUNITY);
        ck(sl_wa_active() == 0 && sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "silo") == NULL,
           "PRESENTATION COMMUNITY HD: the same - it keeps the original world");
        /* D-031 A BORROWED SKY: XBLA only, Bunker 1 only, and only while the
         * game holds no environment row. */
        ck(sl_wa_sky_borrow("bunker", -1, NULL) == NULL,
           "PRESENTATION COMMUNITY HD: Bunker 1 borrows no sky");
        sl_presentation_set(SL_PRESENTATION_ORIGINAL);
        ck(sl_wa_sky_borrow("bunker", -1, NULL) == NULL,
           "PRESENTATION ORIGINAL: Bunker 1 borrows no sky");
        sl_presentation_set(SL_PRESENTATION_XBLA);
        {
            unsigned row = 0u;
            const char *sk = sl_wa_sky_borrow("bunker", -1, &row);
            ck(sk != NULL && strcmp(sk, "surface") == 0 && row == 36u,
               "PRESENTATION XBLA: Bunker 1 draws skydome surface under row 36 (D-031)");
            row = 5u;
            ck(sl_wa_sky_borrow("bunker", 109, &row) == NULL && row == 5u,
               "a Bunker environment row the game holds (multi-player) keeps its own");
            ck(sl_wa_sky_borrow("surface", -1, &row) == NULL
               && sl_wa_sky_borrow("surface", 36, &row) == NULL
               && sl_wa_sky_borrow("dam", 33, &row) == NULL
               && sl_wa_sky_borrow(NULL, -1, &row) == NULL && row == 5u,
               "every other stage draws its own sky");
        }
        /* D-044 A LENT DOME: XBLA only, Streets, Depot and Cradle only. */
        {
            const char *sk = sl_wa_sky_lent("streets");
            ck(sk != NULL && strcmp(sk, "frigate") == 0,
               "PRESENTATION XBLA: Streets draws skydome frigate (D-044)");
            sk = sl_wa_sky_lent("depot");
            ck(sk != NULL && strcmp(sk, "frigate") == 0,
               "PRESENTATION XBLA: Depot draws skydome frigate (D-044, #92)");
            sk = sl_wa_sky_lent("cradle");
            ck(sk != NULL && strcmp(sk, "frigate") == 0,
               "PRESENTATION XBLA: Cradle draws skydome frigate (D-044, #96)");
            ck(sl_wa_sky_lent("statuepark") == NULL && sl_wa_sky_lent("archives") == NULL
               && sl_wa_sky_lent("frigate") == NULL && sl_wa_sky_lent("cavern") == NULL
               && sl_wa_sky_lent("control") == NULL && sl_wa_sky_lent(NULL) == NULL,
               "no other stage is lent a dome");
            sl_presentation_set(SL_PRESENTATION_COMMUNITY);
            ck(sl_wa_sky_lent("streets") == NULL && sl_wa_sky_lent("depot") == NULL
               && sl_wa_sky_lent("cradle") == NULL,
               "PRESENTATION COMMUNITY HD: Streets, Depot and Cradle are lent no dome");
            sl_presentation_set(SL_PRESENTATION_ORIGINAL);
            ck(sl_wa_sky_lent("streets") == NULL && sl_wa_sky_lent("depot") == NULL
               && sl_wa_sky_lent("cradle") == NULL,
               "PRESENTATION ORIGINAL: Streets, Depot and Cradle are lent no dome");
            sl_presentation_set(SL_PRESENTATION_XBLA);
        }
        /* D-034 THE GAME'S ATMOSPHERE ON A FOGLESS STAGE: XBLA only, Frigate
         * only, only while the game holds no environment row. */
        ck(sl_wa_stage_game_atmosphere("frigate", -1) == 1,
           "PRESENTATION XBLA: Frigate's dome takes the game's fogless record (D-034)");
        ck(sl_wa_stage_game_atmosphere("bunker", -1) == 0
           && sl_wa_stage_game_atmosphere("silo", -1) == 0
           && sl_wa_stage_game_atmosphere(NULL, -1) == 0,
           "no other fogless stage is named");
        ck(sl_wa_stage_game_atmosphere("frigate", 33) == sl_wa_env_game_atmosphere(33)
           && sl_wa_stage_game_atmosphere("dam", 36) == sl_wa_env_game_atmosphere(36),
           "a stage with an environment row is D-030's question, unchanged");
        sl_presentation_set(SL_PRESENTATION_COMMUNITY);
        ck(sl_wa_stage_game_atmosphere("frigate", -1) == 0,
           "PRESENTATION COMMUNITY HD: nothing is shaded");
        sl_presentation_set(SL_PRESENTATION_ORIGINAL);
        ck(sl_wa_stage_game_atmosphere("frigate", -1) == 0,
           "PRESENTATION ORIGINAL: nothing is shaded");
        sl_presentation_set(SL_PRESENTATION_XBLA);
        ck(sl_wa_active() == 1 && sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "silo") == bg,
           "PRESENTATION XBLA: the background is offered again, from the same cache slot");
        g_fix_class = SL_WA_CLASS_SKYDOME;
    }

    /* ---- VERSION 2: MATERIAL ANIMATION (#69) -----------------------------
     *
     * Two things have to hold at once: a bundle that moves parses with its
     * records bound and every field checked, and a bundle that does not -
     * every version-1 file there is - parses exactly as it did. */
    {
        struct sl_wa_anim w;
        float d0, d1, d2, d3;
        unsigned no;
        g_fix_class = SL_WA_CLASS_BACKGROUND;
        len = build_anim("dam", 4, 2, 1, 1);
        ck(parse_fix(len) == SL_WA_OK, "a version-2 bundle with one record parses");
        ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK && a.anim_count == 1 &&
           a.vaux != NULL && a.anims[0].mode == SL_WA_ANIM_SINEWAVES &&
           a.anims[0].wave_count == 1 && a.anims[0].coord_scale == 0.25f &&
           a.anims[0].amplitude[0] == 2.0f && a.anims[0].phase[0] == 0.75f &&
           a.anims[0].turn_bias == 0.5f && a.anims[0].poly[1] == -1.0f / 6.0f,
           "the record binds field for field");
        len = build_anim("dam", 4, 2, 0, 0);
        ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK && a.anim_count == 0 && a.vaux == NULL,
           "a version-2 bundle with nothing to animate parses like a version-1 one");
        len = build("dam", 4, 2, 1, 1, 1);
        ck(parse_fix(len) == SL_WA_OK && rd_u32(g_fix + 4) == 1u,
           "a version-1 bundle still parses, unchanged");
        len = build("dam", 4, 2, 1, 1, 1);
        put_u32(g_fix + 124, 1);
        ck(parse_fix(len) == SL_WA_E_FLAGS,
           "a version-1 file cannot carry the animation words");
        len = build("dam", 4, 2, 1, 1, 1);
        put_u32(g_fix + 4, 4u);
        ck(parse_fix(len) == SL_WA_E_CLASS,
           "version 4 is the skinned layout, which a sky does not arrive in");
        len = build("dam", 4, 2, 1, 1, 1);
        put_u32(g_fix + 4, 5u);
        ck(parse_fix(len) == SL_WA_E_VERSION, "version 5 is refused");
        len = build_anim("dam", 4, 2, 1, 1);
        put_u32(g_fix + 124, 0);
        ck(parse_fix(len) == SL_WA_E_LAYOUT, "sections with no record count are refused");
        len = build_anim("dam", 4, 2, 1, 1);
        put_u32(g_fix + 116, 0);
        ck(parse_fix(len) == SL_WA_E_LAYOUT, "a record table with no per-vertex fields is refused");
        len = build_anim("dam", 4, 2, 1, 1);
        put_u32(g_fix + 124, SL_WA_MAX_ANIMS + 1u);
        ck(parse_fix(len) == SL_WA_E_COUNT, "a record count past the limit is refused");
        len = build_anim("dam", 4, 2, 1, 1);
        put_u32(g_fix + 116, SL_WA_HEADER);
        ck(parse_fix(len) == SL_WA_E_LAYOUT, "fields overlapping the vertices are refused");
        len = build_anim("dam", 4, 2, 1, 1);
        put_u32(g_fix + 120, len - 4u);
        ck(parse_fix(len) == SL_WA_E_LAYOUT, "a record table past the file is refused");
        len = build_anim("dam", 4, 2, 1, 2);
        ck(parse_fix(len) == SL_WA_E_ANIM, "a vertex naming a record that is not there is refused");
        len = build_anim("dam", 4, 2, 1, 0);
        ck(parse_fix(len) == SL_WA_OK, "a vertex naming no record is static, and fine");
        len = build_anim("dam", 4, 2, 1, 1); no = rd_u32(g_fix + 120);
        put_u32(g_fix + no, SL_WA_ANIM_NONE);
        ck(parse_fix(len) == SL_WA_E_ANIM, "a record of no mode is refused");
        len = build_anim("dam", 4, 2, 1, 1); put_u32(g_fix + no, 2u);
        ck(parse_fix(len) == SL_WA_E_ANIM, "a mode this renderer does not evaluate is refused");
        len = build_anim("dam", 4, 2, 1, 1); put_f32(g_fix + no + 4, 0.0f);
        ck(parse_fix(len) == SL_WA_E_ANIM, "a zero coordinate scale is refused");
        len = build_anim("dam", 4, 2, 1, 1); put_u32(g_fix + no + 8, SL_WA_ANIM_WAVES + 1u);
        ck(parse_fix(len) == SL_WA_E_ANIM, "more lanes than the register has is refused");
        len = build_anim("dam", 4, 2, 1, 1); put_u32(g_fix + no + 12, 1u);
        ck(parse_fix(len) == SL_WA_E_ANIM, "a reserved word in a record is refused");
        len = build_anim("dam", 4, 2, 1, 1); put_u32(g_fix + no + 16, 0x7fc00000u);
        ck(parse_fix(len) == SL_WA_E_ANIM, "a NaN amplitude is refused");
        len = build_anim("dam", 4, 2, 1, 1); put_f32(g_fix + no + 104, 0.0f);
        ck(parse_fix(len) == SL_WA_E_ANIM, "a zero turn is refused");
        len = build_anim("dam", 4, 2, 1, 1); put_u32(g_fix + no + 96, 0x7f800000u);
        ck(parse_fix(len) == SL_WA_E_ANIM, "an infinite turn scale is refused");

        /* Through the provider: the fields are decoded at load, host order,
         * and the draw path reads a struct. */
        {
            char d[700];
            const struct sl_wa_asset *bg;
            snprintf(d, sizeof d, "%s\\xbla\\background", sub); MKDIR(d);
            snprintf(file, sizeof file, "%s\\xbla\\background\\statue.slwa", sub);
            len = build_anim("statue", 4, 2, 1, 1);
            wr(file, g_fix, len);
            bg = sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "statue");
            ck(bg != NULL && bg->anim_count == 1 && bg->vaux != NULL,
               "a moving background loads through the provider with its record");
            if (bg != NULL && bg->vaux != NULL) {
                ck(bg->vaux[0].wave_a == -50 && bg->vaux[1].wave_a == 50 &&
                   bg->vaux[2].wave_b == 7 && bg->vaux[3].ny == 32767 &&
                   bg->vaux[3].nx == 0 && bg->vaux[0].anim == 1,
                   "the per-vertex fields decode in host order, sign and all");
            }
            snprintf(file, sizeof file, "%s\\xbla\\background\\bunker.slwa", sub);
            len = build_anim("bunker", 4, 2, 1, 2);
            wr(file, g_fix, len);
            ck(sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "bunker") == NULL,
               "a moving background whose fields name a missing record does not load");
        }

        /* THE EVALUATION, pinned. The fixture's record puts one wave at phase
         * 0.75 with no speed and no frequency: theta 0.75, angle (0.75 - 0.5)
         * x 2 pi = pi / 2, and the shipped seventh-order polynomial at pi / 2
         * is 0.999843 (a library sine would say 1), so amplitude 2 displaces
         * by 1.999686 - the polynomial is evaluated as shipped, not
         * replaced. */
        memset(&w, 0, sizeof w);
        w.mode = SL_WA_ANIM_SINEWAVES; w.wave_count = 1; w.coord_scale = 1.0f;
        w.amplitude[0] = 2.0f; w.phase[0] = 0.75f;
        w.turn_scale = 1.0f; w.turn_bias = 0.5f; w.turn = 6.2831853f;
        w.poly[0] = 1.0f; w.poly[1] = -1.0f / 6.0f; w.poly[2] = 1.0f / 120.0f;
        w.poly[3] = -1.0f / 5040.0f;
        d0 = sl_wa_anim_displacement(&w, 0.0f, 0.0f, 0.0);
        ck(fabs((double) d0 - 1.999686) < 1e-4,
           "one wave at a quarter turn displaces by its amplitude through the "
           "shipped polynomial (%.6f)", d0);
        w.speed[0] = 0.5f;                       /* a two-second period */
        d0 = sl_wa_anim_displacement(&w, 0.0f, 0.0f, 0.3);
        d1 = sl_wa_anim_displacement(&w, 0.0f, 0.0f, 2.3);
        d2 = sl_wa_anim_displacement(&w, 0.0f, 0.0f, 1.3);
        ck(fabs((double) d0 - (double) d1) < 1e-5 && fabs((double) d0 - (double) d2) > 0.1,
           "the displacement repeats every 1 / speed seconds and moves in between");
        w.speed[0] = 0.0f; w.freq_a[0] = 1.0f;
        d3 = sl_wa_anim_displacement(&w, 0.25f, 0.0f, 0.0);
        ck(fabs((double) d3 - 2.0 * (-3.14159265 + 5.16771278 - 2.55016404 + 0.59926453)) < 1e-4,
           "the wave pair advances the phase by pair x scale x frequency (%.6f)", d3);
        w.amplitude[0] = 0.0f;
        ck(sl_wa_anim_displacement(&w, 0.25f, 0.0f, 0.3) == 0.0f,
           "zero amplitude displaces nothing");
        w.amplitude[0] = 2.0f; w.mode = SL_WA_ANIM_NONE;
        ck(sl_wa_anim_displacement(&w, 0.25f, 0.0f, 0.3) == 0.0f
           && sl_wa_anim_displacement(NULL, 0.25f, 0.0f, 0.3) == 0.0f,
           "no mode, or no record, displaces nothing");
        ck(sl_wa_anim_seconds(60) == 1.0 && sl_wa_anim_seconds(90) == 1.5,
           "the clock is VI frames over the VI rate");
        w.mode = SL_WA_ANIM_SINEWAVES; w.speed[0] = 0.5f; w.freq_a[0] = 0.0f;
        ck(sl_wa_anim_displacement(&w, 0.0f, 0.0f, sl_wa_anim_seconds(120))
           == sl_wa_anim_displacement(&w, 0.0f, 0.0f, 2.0),
           "the same clock is the same displacement however many frames were presented");
        g_fix_class = SL_WA_CLASS_SKYDOME;
    }

    /* ---- VERSION 3: THE MATERIAL EXTENSION (#69 material) ----------------
     *
     * The same two things: a bundle with a record parses with the record
     * bound, its frame beside every vertex and every field checked, and a
     * version-2 file parses exactly as it did. Then the reference shade,
     * pinned term by term. */
    {
        unsigned mxo, no;
        g_fix_class = SL_WA_CLASS_BACKGROUND;
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE);
        ck(parse_fix(len) == SL_WA_OK, "a version-3 bundle with a record and a wave parses");
        ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK && a.vframe == 1 &&
           a.aux_stride == SL_WA_AUX3_SIZE && a.matx_count == 1 && a.anim_count == 1 &&
           a.matx[0].mode == SL_WA_MATX_NORMAL_SPHERE && a.matx[0].flags == SL_WA_MATX_F_SPHERE_CLAMP &&
           a.matx[0].base_weight[2] == 0.9f && a.matx[0].reflect_weight[0] == 0.1f &&
           a.matx[0].warp_scale == 0.25f && a.matx[0].normal_scale == 2.0f &&
           a.matx[0].normal_bias == -1.0f && a.matx[0].sphere_scale[1] == -0.5f &&
           a.matx[0].sphere_bias[1] == 0.5f && a.matx[0].alpha_floor == 1.0f,
           "the material record binds field for field, the frame flagged present");
        ck(SL_WA_VAUX(&a, 1)->wave_a == 50 && SL_WA_VAUX(&a, 3)->anim == 1,
           "the auxiliary entries are read at the 24-byte stride");
        len = build_matx("dam", 4, 2, 0, 0, SL_WA_MATX_NORMAL_SPHERE);
        ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK && a.anim_count == 0 && a.vaux != NULL &&
           a.matx_count == 1,
           "a version-3 bundle with a record and nothing to animate parses with its frame");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NONE);
        ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK && a.matx_count == 0 &&
           a.matx[0].mode == SL_WA_MATX_NONE && a.anim_count == 1,
           "a version-3 bundle whose only record is NONE parses as a moving base-only bundle");
        len = build_anim("dam", 4, 2, 1, 1);
        ck(sl_wa_parse(g_fix, len, &a) == SL_WA_OK && a.vframe == 0 &&
           a.aux_stride == SL_WA_AUX_SIZE && a.matx_count == 0,
           "a version-2 bundle still parses, unchanged, at the 12-byte stride");
        len = build_anim("dam", 4, 2, 1, 1);
        put_u32(g_fix + 44, SL_WA_HEADER);
        ck(parse_fix(len) == SL_WA_E_FLAGS, "a version-2 file cannot carry the material word");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE);
        put_u32(g_fix + 44, 0);
        ck(parse_fix(len) == SL_WA_E_LAYOUT, "a version-3 file without the table is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE);
        put_u32(g_fix + 116, 0);
        ck(parse_fix(len) == SL_WA_E_LAYOUT, "a version-3 file without the frames is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE);
        put_u32(g_fix + 44, len - 4u);
        ck(parse_fix(len) == SL_WA_E_LAYOUT, "a table past the file is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE);
        put_u32(g_fix + 44, rd_u32(g_fix + 116));
        ck(parse_fix(len) == SL_WA_E_LAYOUT, "a table overlapping the frames is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE); mxo = rd_u32(g_fix + 44);
        put_u32(g_fix + mxo, 2u);
        ck(parse_fix(len) == SL_WA_E_MATX, "a mode this renderer does not evaluate is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE); put_u32(g_fix + mxo + 8, 1u);
        ck(parse_fix(len) == SL_WA_E_MATX, "a record naming an image that is not there is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE); put_u32(g_fix + mxo + 16, 2u);
        ck(parse_fix(len) == SL_WA_E_MATX, "a flag bit this version does not define is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE); put_f32(g_fix + mxo + 48, 0.0f);
        ck(parse_fix(len) == SL_WA_E_MATX, "a zero normal scale is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE); put_u32(g_fix + mxo + 32, 0x7fc00000u);
        ck(parse_fix(len) == SL_WA_E_MATX, "a NaN weight is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE); put_u32(g_fix + mxo + 84, 1u);
        ck(parse_fix(len) == SL_WA_E_MATX, "a reserved word in a record is refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NONE); put_f32(g_fix + mxo + 20, 0.9f);
        ck(parse_fix(len) == SL_WA_E_MATX, "a NONE record that is not all zeros is refused");
        len = build_matx("dam", 4, 2, 1, 2, SL_WA_MATX_NORMAL_SPHERE);
        ck(parse_fix(len) == SL_WA_E_ANIM, "a frame entry naming a record that is not there is still refused");
        len = build_matx("dam", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE); no = rd_u32(g_fix + 120);
        put_u32(g_fix + no, 2u);
        ck(parse_fix(len) == SL_WA_E_ANIM, "the animation record is still checked in a version-3 file");

        /* Through the provider: the frame decodes at load, host order. */
        {
            char d[700];
            const struct sl_wa_asset *bg;
            snprintf(d, sizeof d, "%s\\xbla\\background", sub); MKDIR(d);
            snprintf(file, sizeof file, "%s\\xbla\\background\\surface.slwa", sub);
            len = build_matx("surface", 4, 2, 1, 1, SL_WA_MATX_NORMAL_SPHERE);
            wr(file, g_fix, len);
            bg = sl_wa_lookup(SL_WA_CLASS_BACKGROUND, "surface");
            ck(bg != NULL && bg->matx_count == 1 && bg->vframe && bg->vaux != NULL,
               "a material background loads through the provider with its record");
            if (bg != NULL && bg->vaux != NULL && bg->vframe) {
                ck(SL_WA_VAUX(bg, 1)->wave_a == 50 && SL_WA_VAUX(bg, 3)->ny == 32767 &&
                   SL_WA_VFRAME(bg, 0)->xx == 32767 && SL_WA_VFRAME(bg, 0)->xy == 0 &&
                   SL_WA_VFRAME(bg, 2)->yz == -32767 && SL_WA_VFRAME(bg, 2)->yx == 0,
                   "the frame decodes in host order at the 24-byte stride, sign and all");
            }
        }

        /* THE REFERENCE SHADE, pinned. The fixture's frame is X = +x, Y = -z,
         * normal +y, handed here already in view space; the base image is
         * its own coordinate, the offset map (0.4, 0.8), the normal map (0.5,
         * 1, 1) which decodes to (0, 1, 1), and the sphere image its own
         * coordinate. */
        {
            struct sl_wa_matx m;
            float fr[9] = { 1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f, 0.0f };
            float col[4] = { 1.0f, 0.5f, 1.0f, 0.8f };
            float o[4], o2[4];
            memset(&m, 0, sizeof m);
            m.mode = SL_WA_MATX_NORMAL_SPHERE;
            m.base_weight[0] = m.base_weight[1] = m.base_weight[2] = 0.9f;
            m.reflect_weight[0] = m.reflect_weight[1] = m.reflect_weight[2] = 0.1f;
            m.warp_scale = 0.25f; m.normal_scale = 2.0f; m.normal_bias = -1.0f;
            m.sphere_scale[0] = 0.5f; m.sphere_scale[1] = -0.5f;
            m.sphere_bias[0] = 0.5f; m.sphere_bias[1] = 0.5f;
            m.mean_weight = 1.0f / 3.0f; m.alpha_floor = 0.0f;
            /* warp alone: base sampled at (u + 0.25 * 0.4, v + 0.25 * 0.8) */
            sl_wa_matx_shade(&m, SL_WA_MATX_T_WARP, fr, col, 0.2f, 0.3f, fix_tex, NULL, o);
            ck(fabs((double) o[0] - 0.9 * 1.0 * 0.3) < 1e-6 && fabs((double) o[1] - 0.9 * 0.5 * 0.5) < 1e-6,
               "the warp composes the base coordinate as uv + scale x offset (%.4f %.4f)", o[0], o[1]);
            ck(fabs((double) o[3] - ((0.9 * 0.3 + 0.9 * 0.25 + 0.9) / 3.0) * 0.5 * 0.8) < 1e-6,
               "alpha is the weighted mean floored, times base alpha and colour alpha (%.4f)", o[3]);
            /* reflection with the vertex normal alone: N = (0, 1, 0) -> sphere (0.5, 0) */
            sl_wa_matx_shade(&m, SL_WA_MATX_T_REFLECT, fr, col, 0.2f, 0.3f, fix_tex, NULL, o);
            ck(fabs((double) o[0] - (0.9 * 0.2 + 0.1 * 0.5)) < 1e-6 && fabs((double) o[1] - (0.9 * 0.5 * 0.3 + 0.1 * 0.0)) < 1e-6,
               "the sphere coordinate is (N.x * sx + bx, N.y * sy + by) on the view normal (%.4f %.4f)", o[0], o[1]);
            /* the normal map too: n = (0, 1, 1) -> N = normalize(Y + normal) = (0, .7071, -.7071)
             * -> sphere (0.5, 0.5 - 0.35355) */
            sl_wa_matx_shade(&m, SL_WA_MATX_T_REFLECT | SL_WA_MATX_T_NORMAL, fr, col, 0.2f, 0.3f, fix_tex, NULL, o);
            ck(fabs((double) o[0] - (0.9 * 0.2 + 0.1 * 0.5)) < 1e-6
               && fabs((double) o[1] - (0.9 * 0.5 * 0.3 + 0.1 * (0.5 - 0.35355339))) < 1e-5,
               "the map's y steers along the frame's Y, the map's z along the normal (%.4f %.4f)", o[0], o[1]);
            /* every term off is the base alone, weighted */
            sl_wa_matx_shade(&m, 0u, fr, col, 0.2f, 0.3f, fix_tex, NULL, o);
            ck(fabs((double) o[0] - 0.9 * 0.2) < 1e-6 && fabs((double) o[1] - 0.9 * 0.5 * 0.3) < 1e-6
               && fabs((double) o[2] - 0.9 * 1.0) < 1e-6,
               "with every term off the shade is base_weight x colour x base (%.4f %.4f %.4f)", o[0], o[1], o[2]);
            /* the floor: a floor of one makes alpha the base alpha times the colour alpha */
            m.alpha_floor = 1.0f;
            sl_wa_matx_shade(&m, SL_WA_MATX_T_ALL, fr, col, 0.2f, 0.3f, fix_tex, NULL, o);
            ck(fabs((double) o[3] - 0.5 * 0.8) < 1e-6, "a floor of one hands alpha straight through (%.4f)", o[3]);
            /* a NONE record, or no record, shades nothing */
            m.mode = SL_WA_MATX_NONE;
            sl_wa_matx_shade(&m, SL_WA_MATX_T_ALL, fr, col, 0.2f, 0.3f, fix_tex, NULL, o);
            sl_wa_matx_shade(NULL, SL_WA_MATX_T_ALL, fr, col, 0.2f, 0.3f, fix_tex, NULL, o2);
            ck(o[0] == 0.0f && o[3] == 0.0f && o2[0] == 0.0f, "no record shades nothing");
        }
        g_fix_class = SL_WA_CLASS_SKYDOME;
    }

    /* An unknown class never touches the filesystem. */
    ck(sl_wa_lookup(7u, "dam") == NULL, "an unknown class resolves to nothing");

    /* ---- THE ANCHORING CONTRACT ------------------------------------------
     *
     * What this pins, and why each half of it earned a test.
     *
     * A skydome has to sit on the eye or the sky slides as the player walks,
     * and the point of it that sits there is its own model-space ORIGIN -
     * sl_wa_sky_anchor, whose rule is pinned first below. The eye is recovered
     * from the world-to-clip matrix in force rather than read from the game,
     * because GoldenEye loads TWO world projections - fr.c's plain one and
     * bondview2.c's with the view folded in - and they consume different
     * spaces: absolute world, and world minus the player's floating render
     * origin. Two earlier revisions failed here in different ways, and both
     * failures are checked for below: feeding absolute coordinates to the
     * folded projection (the dome jumps by the render origin whenever the game
     * alternates), and pinning the dome to the camera read from the player
     * struct (which is not the camera the matrix was built with).
     *
     * Synthetic throughout - a view, a perspective and an origin written here
     * - so it needs no ROM, no asset and no GL. */
    {
        const float c30 = 0.8660254f, s30 = 0.5f;
        const float eye[3] = { 17959.16f, 73.152f, 17809.947f };
        const float ro[3]  = { 17723.850f, -107.002f, 17612.568f };
        /* Bounds shaped like the first measured dome's: NOT centred on the
         * item's own origin, which is the whole point of the first check. */
        const float bmin[3] = { -58685.9f,   961.2f, -79699.4f };
        const float bmax[3] = {  58878.8f, 33486.1f,  37880.4f };
        float cen[3], ctl[3];
        float V[16], P[16], F[16], q[3];
        float Rm[9];
        int r, cc, k;

        /* 0. THE ANCHOR IS THE ITEM'S ORIGIN. The source's dome draw carries
         *    a modelview with zero translation, so (0,0,0) of the item is the
         *    eye - and the bounds centre, the rule this replaced, is reachable
         *    only as the control, and is a different point for these bounds. */
        sl_wa_sky_anchor(bmin, bmax, 0, cen);
        sl_wa_sky_anchor(bmin, bmax, 1, ctl);
        ck(cen[0] == 0.0f && cen[1] == 0.0f && cen[2] == 0.0f,
           "a skydome is anchored by its own origin, not its bounds "
           "(got %.1f %.1f %.1f)", cen[0], cen[1], cen[2]);
        ck(fabsf(ctl[0] - 96.45f) < 0.1f && fabsf(ctl[1] - 17223.65f) < 0.1f
           && fabsf(ctl[2] + 20909.5f) < 0.1f,
           "the control answers the bounds centre (%.1f %.1f %.1f)",
           ctl[0], ctl[1], ctl[2]);

        Rm[0] =  c30; Rm[1] = 0.0f; Rm[2] = -s30;
        Rm[3] = 0.0f; Rm[4] = 1.0f; Rm[5] = 0.0f;
        Rm[6] =  s30; Rm[7] = 0.0f; Rm[8] =  c30;
        for (r = 0; r < 16; r++) P[r] = 0.0f;
        P[0] = 0.9168f; P[5] = 1.7776f; P[10] = -1.000656f; P[11] = -1.0f;
        P[14] = -10.0033f;

        /* V: world-to-view about `eye`.  F: the same camera, but expressed
         * against the render origin and folded into the projection - which is
         * exactly what the game hands the seam. */
        for (r = 0; r < 4; r++) for (cc = 0; cc < 4; cc++) V[r*4+cc] = (r==cc)?1.0f:0.0f;
        for (r = 0; r < 3; r++) for (cc = 0; cc < 3; cc++) V[r*4+cc] = Rm[cc*3+r];
        for (cc = 0; cc < 3; cc++)
            V[12+cc] = -(eye[0]*Rm[cc*3+0] + eye[1]*Rm[cc*3+1] + eye[2]*Rm[cc*3+2]);
        {
            float Vr[16];
            memcpy(Vr, V, sizeof Vr);
            for (cc = 0; cc < 3; cc++)
                Vr[12+cc] = -((eye[0]-ro[0])*Rm[cc*3+0] + (eye[1]-ro[1])*Rm[cc*3+1]
                            + (eye[2]-ro[2])*Rm[cc*3+2]);
            for (r = 0; r < 4; r++) for (cc = 0; cc < 4; cc++) {
                float s = 0.0f;
                for (k = 0; k < 4; k++) s += Vr[r*4+k] * P[k*4+cc];
                F[r*4+cc] = s;
            }
        }

        /* 1. The eye comes back out of each matrix, in that matrix's own
         *    space - absolute for the plain one, render-origin-relative for
         *    the folded one - with nobody having named the space. */
        {
            float A[16];
            int ok;
            for (r = 0; r < 4; r++) for (cc = 0; cc < 4; cc++) {
                float s = 0.0f;
                for (k = 0; k < 4; k++) s += V[r*4+k] * P[k*4+cc];
                A[r*4+cc] = s;
            }
            ok = sl_wa_view_origin(A, q);
            for (k = 0; k < 3; k++) if (fabsf(q[k] - eye[k]) > 0.05f) ok = 0;
            ck(ok, "the eye is recovered from a plain world-to-clip matrix");
            ok = sl_wa_view_origin(F, q);
            for (k = 0; k < 3; k++) if (fabsf(q[k] - (eye[k] - ro[k])) > 0.05f) ok = 0;
            ck(ok, "and from a view-folded one, relative to the render origin");

            /* 2. THE INVARIANT. With the dome's centre put on the recovered
             *    eye, both projections place a dome vertex at the SAME view
             *    coordinates - so the game alternating between them cannot
             *    move the sky. Checked on points far outside any level, which
             *    is where a skydome's vertices are. */
            {
                static const float pts[3][3] = {
                    { -58685.9f,   961.2f, -79699.4f },
                    {  58878.8f, 33486.1f,  37880.4f },
                    {  20000.0f,  5000.0f,  -3000.0f }
                };
                int p;
                for (p = 0; p < 3; p++) {
                    float qa[3], qf[3], ca[4], cf[4], worst = 0.0f;
                    float ax, ay, az, fx, fy, fz;
                    sl_wa_view_origin(A, qa);
                    sl_wa_view_origin(F, qf);
                    ax = pts[p][0] - (cen[0] - qa[0]);
                    ay = pts[p][1] - (cen[1] - qa[1]);
                    az = pts[p][2] - (cen[2] - qa[2]);
                    fx = pts[p][0] - (cen[0] - qf[0]);
                    fy = pts[p][1] - (cen[1] - qf[1]);
                    fz = pts[p][2] - (cen[2] - qf[2]);
                    for (cc = 0; cc < 4; cc++) {
                        ca[cc] = ax*A[cc] + ay*A[4+cc] + az*A[8+cc] + A[12+cc];
                        cf[cc] = fx*F[cc] + fy*F[4+cc] + fz*F[8+cc] + F[12+cc];
                    }
                    for (cc = 0; cc < 4; cc++) {
                        float d = fabsf(ca[cc] - cf[cc]);
                        if (d > worst) worst = d;
                    }
                    ck(worst < 0.5f,
                       "both projections place dome point %d identically "
                       "(worst clip difference %.4f)", p, worst);

                    /* 3. The first failure, still detectable: hand the folded
                     *    projection absolute coordinates and it misplaces the
                     *    point badly - which is the jump observed in play. */
                    for (cc = 0; cc < 4; cc++)
                        cf[cc] = pts[p][0]*F[cc] + pts[p][1]*F[4+cc]
                               + pts[p][2]*F[8+cc] + F[12+cc];
                    worst = 0.0f;
                    for (cc = 0; cc < 4; cc++) {
                        float d = fabsf(ca[cc] - cf[cc]);
                        if (d > worst) worst = d;
                    }
                    ck(worst > 100.0f,
                       "ignoring the anchor misplaces dome point %d (by %.1f in clip)",
                       p, worst);
                }
            }

            /* 4. NO PARALLAX. Move the camera 382 units - the playtest's
             *    second pair of marks - and a dome vertex must not move in
             *    view space at all. This is the property the report was
             *    about: the sky still shifting while walking. */
            {
                float V2[16], A2[16], q2[3], c1[4], c2[4], worst = 0.0f;
                const float eye2[3] = { eye[0] - 258.4f, eye[1] + 12.8f, eye[2] + 280.8f };
                const float pt[3] = { -58685.9f, 961.2f, -79699.4f };
                float x1, y1, z1, x2, y2, z2;
                memcpy(V2, V, sizeof V2);
                for (cc = 0; cc < 3; cc++)
                    V2[12+cc] = -(eye2[0]*Rm[cc*3+0] + eye2[1]*Rm[cc*3+1]
                                + eye2[2]*Rm[cc*3+2]);
                for (r = 0; r < 4; r++) for (cc = 0; cc < 4; cc++) {
                    float s = 0.0f;
                    for (k = 0; k < 4; k++) s += V2[r*4+k] * P[k*4+cc];
                    A2[r*4+cc] = s;
                }
                sl_wa_view_origin(A, q);
                sl_wa_view_origin(A2, q2);
                x1 = pt[0]-(cen[0]-q[0]);  y1 = pt[1]-(cen[1]-q[1]);  z1 = pt[2]-(cen[2]-q[2]);
                x2 = pt[0]-(cen[0]-q2[0]); y2 = pt[1]-(cen[1]-q2[1]); z2 = pt[2]-(cen[2]-q2[2]);
                for (cc = 0; cc < 4; cc++) {
                    c1[cc] = x1*A[cc]  + y1*A[4+cc]  + z1*A[8+cc]  + A[12+cc];
                    c2[cc] = x2*A2[cc] + y2*A2[4+cc] + z2*A2[8+cc] + A2[12+cc];
                }
                for (cc = 0; cc < 4; cc++) {
                    float d = fabsf(c1[cc] - c2[cc]);
                    if (d > worst) worst = d;
                }
                ck(worst < 0.5f,
                   "an eye-anchored dome does not parallax over 382 units "
                   "(worst clip difference %.4f)", worst);
            }
        }

        /* 4b. THE VISIBILITY-SCALED SPACE - the world-anchored contract, and
         *     the defect the matched-presentation playtest marks exposed.
         *
         *     The game multiplies every matrix it hands the RSP by the stage's
         *     `visibility` (bg.c:1088-1091 into matrixmath.c:478, applied by
         *     matrix_4x4_f32_to_s32, and again by name at bondview2.c:8798).
         *     It is 1.0 on every stage but two and 0.2 at the Dam, so the space
         *     the folded matrix consumes is the room space TIMES that number,
         *     not the room space. A world-anchored asset that ignores it is
         *     drawn 1/visibility times its size about the render origin, which
         *     moves the apparent viewpoint by (1 - visibility) times the
         *     player's offset from their own room's origin.
         *
         *     Synthetic, like everything around it: this builds the scaled
         *     folded matrix itself and checks three things - that the eye comes
         *     back out of it in ITS space, that applying the scale puts a world
         *     point on exactly the screen position the unscaled space puts it,
         *     and that OMITTING the scale misplaces it badly. The third is what
         *     makes the second mean anything. */
        {
            const float vis = 0.2f;
            /* Three points in front of this camera, at the distances the
             * playtest marks put geometry at: a wall a few hundred units
             * away, the ground ahead, and something far off. The far one is
             * kept deliberately - it is the contrast that explains why this
             * read as "the models are in the wrong spot" rather than as a
             * wrong picture. */
            static const float pw[3][3] = {
                { 17541.66f,    73.152f, 17550.147f },   /* ~309 to one side  */
                { 17759.16f,   -93.848f, 17463.547f },   /* ~430 ahead, floor */
                { 15459.16f,    73.152f, 13479.947f }    /* ~5000 ahead       */
            };
            float Fv[16], Vs[16], qv[3];
            float omitted[3];
            int p, ok = 1;

            /* The folded matrix as the game builds it when visibility != 1:
             * the same camera basis, but its translation row - and therefore
             * the space it consumes - carries the scale. */
            memcpy(Vs, V, sizeof Vs);
            for (cc = 0; cc < 3; cc++)
                Vs[12+cc] = -(((eye[0]-ro[0])*vis)*Rm[cc*3+0]
                            + ((eye[1]-ro[1])*vis)*Rm[cc*3+1]
                            + ((eye[2]-ro[2])*vis)*Rm[cc*3+2]);
            for (r = 0; r < 4; r++) for (cc = 0; cc < 4; cc++) {
                float s = 0.0f;
                for (k = 0; k < 4; k++) s += Vs[r*4+k] * P[k*4+cc];
                Fv[r*4+cc] = s;
            }

            ok = sl_wa_view_origin(Fv, qv);
            for (k = 0; k < 3; k++)
                if (fabsf(qv[k] - (eye[k]-ro[k]) * vis) > 0.05f) ok = 0;
            ck(ok, "the eye recovered from a visibility-scaled folded matrix is "
                   "the render-origin-relative eye TIMES that scale");

            for (p = 0; p < 3; p++) {
                float sx, sy, wx, wy, worst;
                float cs[4], cu[4], cb[4];
                float ax = (pw[p][0]-ro[0]), ay = (pw[p][1]-ro[1]), az = (pw[p][2]-ro[2]);
                for (cc = 0; cc < 4; cc++) {
                    cs[cc] = (ax*vis)*Fv[cc] + (ay*vis)*Fv[4+cc] + (az*vis)*Fv[8+cc] + Fv[12+cc];
                    cu[cc] = ax*F[cc] + ay*F[4+cc] + az*F[8+cc] + F[12+cc];
                    cb[cc] = ax*Fv[cc] + ay*Fv[4+cc] + az*Fv[8+cc] + Fv[12+cc];
                }
                sx = cs[0]/cs[3]; sy = cs[1]/cs[3];
                wx = cu[0]/cu[3]; wy = cu[1]/cu[3];
                worst = fabsf(sx-wx) > fabsf(sy-wy) ? fabsf(sx-wx) : fabsf(sy-wy);
                ck(worst < 0.001f,
                   "world point %d lands on the same screen position once the "
                   "visibility scale is applied (worst ndc difference %.6f)",
                   p, worst);
                sx = cb[0]/cb[3]; sy = cb[1]/cb[3];
                worst = fabsf(sx-wx) > fabsf(sy-wy) ? fabsf(sx-wx) : fabsf(sy-wy);
                omitted[p] = worst;
                ck(cb[3] > 0.0f, "world point %d stays in front either way", p);
            }
            ck(omitted[0] > 0.5f && omitted[1] > 0.1f,
               "and omitting it misplaces NEAR geometry grossly (%.3f and %.3f "
               "in ndc, at ~309 and ~430 units)", omitted[0], omitted[1]);
            ck(omitted[2] < 0.25f * omitted[1],
               "while distant geometry barely moves (%.3f at ~5000 units) - "
               "which is why this reads as props in the wrong place rather "
               "than as a wrong picture", omitted[2]);

            /* The PLAIN branch's half of the same rule: the seam builds that
             * view itself, so the scale goes on the view's OUTPUT - columns 0
             * to 2, w untouched. No screen position may move, and the eye-space
             * depth MUST scale, because that depth is what this pass is
             * depth-tested against. */
            {
                float Vp[16], Cp[16], Cu[16];
                float worst = 0.0f, dz = 0.0f, du = 0.0f;
                memcpy(Vp, V, sizeof Vp);
                for (r = 0; r < 4; r++) {
                    Vp[r*4+0] *= vis; Vp[r*4+1] *= vis; Vp[r*4+2] *= vis;
                }
                for (r = 0; r < 4; r++) for (cc = 0; cc < 4; cc++) {
                    float s = 0.0f, u = 0.0f;
                    for (k = 0; k < 4; k++) {
                        s += Vp[r*4+k] * P[k*4+cc];
                        u += V[r*4+k]  * P[k*4+cc];
                    }
                    Cp[r*4+cc] = s; Cu[r*4+cc] = u;
                }
                for (p = 0; p < 3; p++) {
                    float cp[4], cu2[4];
                    for (cc = 0; cc < 4; cc++) {
                        cp[cc]  = pw[p][0]*Cp[cc] + pw[p][1]*Cp[4+cc] + pw[p][2]*Cp[8+cc] + Cp[12+cc];
                        cu2[cc] = pw[p][0]*Cu[cc] + pw[p][1]*Cu[4+cc] + pw[p][2]*Cu[8+cc] + Cu[12+cc];
                    }
                    if (fabsf(cp[0]/cp[3] - cu2[0]/cu2[3]) > worst)
                        worst = fabsf(cp[0]/cp[3] - cu2[0]/cu2[3]);
                    if (fabsf(cp[1]/cp[3] - cu2[1]/cu2[3]) > worst)
                        worst = fabsf(cp[1]/cp[3] - cu2[1]/cu2[3]);
                    if (p == 0) { dz = cp[3]; du = cu2[3]; }
                }
                ck(worst < 0.001f,
                   "scaling the plain branch's view moves no screen position "
                   "(worst ndc difference %.6f)", worst);
                ck(du != 0.0f && fabsf(dz / du - vis) < 0.001f,
                   "and it scales the eye-space depth by exactly the visibility "
                   "(%.4f)", (du != 0.0f) ? dz / du : 0.0f);
            }
        }

        /* 5. A matrix that is not a view times a perspective is refused
         *    rather than guessed at. */
        {
            float bad[16];
            memcpy(bad, F, sizeof bad);
            bad[0] = 0.0f; bad[4] = 0.0f; bad[8] = 0.0f;
            ck(sl_wa_view_origin(bad, q) == 0,
               "a degenerate matrix yields no eye");
            memcpy(bad, F, sizeof bad);
            bad[1] += 3.0f;                    /* breaks orthonormality */
            ck(sl_wa_view_origin(bad, q) == 0,
               "a matrix whose basis is not orthonormal yields no eye");
            ck(sl_wa_view_origin(NULL, q) == 0 && sl_wa_view_origin(F, NULL) == 0,
               "and a null matrix or destination yields no eye");
        }

        /* 6. THE APERTURE, and the defect it exists for. The dome is emitted
         *    inside the first ROOM's display list, so the scissor standing
         *    there is that room's PORTAL WINDOW - and the portal traversal
         *    makes which room is first, and how wide its window is, a
         *    function of the camera's HEADING. Cut to that, the sky changes
         *    when the player turns on the spot, with one hard vertical edge
         *    at the window's side.
         *
         *    The three boxes below are the ones MEASURED at the seam at the
         *    three playtest marks near the Dam's first alarm (headings 246.3,
         *    207.0 and 192.6) against the viewport measured with them. What
         *    has to hold is that the aperture does not depend on them: all
         *    three must give the same rectangle, and that rectangle must be
         *    the viewport. Drive them through the rule and require it. */
        {
            static const int viewport[4] = { 0, 30, 960, 660 };
            static const int windows[3][4] = {
                { 654, 30, 303, 660 },     /* heading 246.319 - a right strip */
                { 516, 30, 441, 660 },     /* heading 207.019 - a wider one   */
                {   3, 30, 954, 660 }      /* heading 192.619 - the picture   */
            };
            int box[4], first[4], w, cc2, agree = 1, isvp = 1;
            for (w = 0; w < 3; w++) {
                sl_wa_sky_aperture(viewport, windows[w], box);
                if (w == 0) memcpy(first, box, sizeof first);
                for (cc2 = 0; cc2 < 4; cc2++) {
                    if (box[cc2] != first[cc2])    agree = 0;
                    if (box[cc2] != viewport[cc2]) isvp  = 0;
                }
            }
            ck(agree, "the skydome's aperture is the same under all three "
                      "portal windows measured at the playtest marks");
            ck(isvp,  "and it is the viewport - the rectangle clip space maps "
                      "onto - not the window it was handed");
            /* A viewport that is not the whole window, which is what
             * split-screen and the letterboxed 3D area both produce: the
             * aperture follows the viewport, never the window. */
            {
                static const int half[4] = { 0, 360, 960, 330 };
                sl_wa_sky_aperture(half, windows[0], box);
                ck(box[0] == 0 && box[1] == 360 && box[2] == 960 && box[3] == 330,
                   "a viewport that is not the whole window is followed too");
            }
            /* Null-safe, like everything else here. */
            sl_wa_sky_aperture(viewport, windows[0], (int *) 0);
            sl_wa_sky_aperture((const int *) 0, windows[0], box);
            ck(box[0] == 0 && box[1] == 360,
               "a null viewport or destination writes nothing");
        }

        /* 7. THE FOG CONTRACT, and the two defects it exists for: a
         *    replacement background that received NO level fog at all, and a
         *    classification taken from the seam that changed frame to frame
         *    at a fixed camera. See the header for both measurements.
         *
         *    The pair driven through here is the one MEASURED on the Dam at
         *    the gate yard - fm 25600, fo -25344, environment colour
         *    (16,48,96) - read off the display list the game itself wrote. */
        {
            float s = -1.0f, e = -1.0f;
            ck(sl_wa_fog_band(1, 25600, -25344, 0u, &s, &e),
               "the Dam's own fog position arms the replacement's fog");
            /* The band is what the RSP formula says, not a number chosen
             * here: alpha = ndcz * fm + fo, zero at the start and 255 at the
             * end. Required through the formula rather than against a
             * literal, so the test cannot drift with the implementation. */
            ck(fabs(s * 25600.0f - 25344.0f -   0.0f) < 0.05f,
               "the band starts where the RSP's alpha reaches 0 (ndc z %.5f)", s);
            ck(fabs(e * 25600.0f - 25344.0f - 255.0f) < 0.05f,
               "and saturates where it reaches 255 (ndc z %.5f)", e);
            ck(s > 0.98f && s < 1.0f && e > s && e < 1.0f,
               "which is inside the depth buffer's outer one percent");
            /* A SECOND band must map differently, or the two above would hold
             * for a function that ignored its arguments. This is the pair
             * gbi.h's own gSPFogPosition arithmetic gives for min 990 /
             * max 1000, the band the notes describe. */
            {
                float s2 = -1.0f, e2 = -1.0f;
                ck(sl_wa_fog_band(1, 12800, -12544, 0u, &s2, &e2),
                   "a second fog position arms it too");
                ck(fabs(s2 - 0.98f) < 0.0005f && s2 < s,
                   "and gives a different, earlier band start (%.5f vs %.5f)", s2, s);
            }
            /* THE LEVEL THAT DOES NOT FOG. `fogSetRenderFogColor` returns
             * without writing a fog position when g_FogSkyIsEnabled is clear,
             * so nothing arms and the replacement stays unfogged - the same
             * outcome the game's own geometry gets, for the same reason. */
            ck(!sl_wa_fog_band(0, 25600, -25344, 0u, &s, &e),
               "a level that never issued a fog position receives no fog");
            ck(!sl_wa_fog_band(1, 0, -25344, 0u, &s, &e),
               "and a band with a zero multiplier has no extent to fog over");
            /* STALENESS. The seam runs early, so this frame's own position
             * may not have been written yet and the frame before it is
             * accepted; anything older is a value nothing is issuing. */
            ck(sl_wa_fog_band(1, 25600, -25344, 1u, &s, &e),
               "last frame's fog position is still the contract");
            ck(!sl_wa_fog_band(1, 25600, -25344, 2u, &s, &e),
               "an older one is not, so a level that stops fogging stops");
            /* Null-safe, like everything else here, and the verdict does not
             * depend on the outputs being wanted. */
            ck(sl_wa_fog_band(1, 25600, -25344, 0u, (float *) 0, (float *) 0),
               "and the answer stands with no band wanted");
        }

        /* 7b. THE FOG DESTINATION. The band above says what the LEVEL is
         *     issuing; this says whether that band has anything on screen to
         *     fade into. The game writes one environment RGB to both its
         *     backdrop fill and its fog colour - measured (16,49,99) and
         *     (16,48,96) in one SL_FOG_DBG line - so a replacement backdrop
         *     is exactly the statement that the destination has changed.
         *     Two states, and the rule names neither an item, a level nor a
         *     colour, so these two checks are the whole of it. */
        ck(sl_wa_fog_level_applies(0),
           "with no replacement background the level's own fog band applies - "
           "its backdrop is the one on screen to fade into");
        ck(!sl_wa_fog_level_applies(1),
           "with a replacement background it does NOT - the level's backdrop "
           "is off screen and the replacement carries its own distance shading");
        /* The two rules USED to be one fact read twice ("a replacement is the
         * backdrop, so no level fog and no stage sky"). The console split
         * them: the stage sky is drawn beneath the replacement, and the
         * level's colour is still behind neither. So they must now DIFFER
         * where a replacement is resolved - agreement there would mean the
         * sky had been suppressed again, or the old fog wall restored. */
        ck(sl_wa_compose_skydome(1) && !sl_wa_fog_level_applies(1),
           "with a replacement the stage sky is composed and the level's fog "
           "is not - they no longer answer on one fact");
        /* #77 THE ITEM'S OWN COPLANAR SURFACES (fabricated): a floor quad,
         * a stripe in its plane drawn before it, a wall touching the floor
         * along an edge, a stripe in the decal pass, a translucent pane in
         * the floor's plane, and a second floor slab 0.5 units above. */
        {
            static struct sl_wa_asset ca;
            static struct sl_wa_vertex cv[24];
            static unsigned ci[36];
            unsigned char y[12];
            static const float P[24][3] = {
                /* mesh 0 (stripe, opaque): two tris in y = 0, x 2..4 z 0..10 */
                {2,0,0},{4,0,0},{4,0,10}, {2,0,0},{4,0,10},{2,0,10},
                /* mesh 1 (floor, opaque, later): two tris y = 0, x 0..10 z 0..10 */
                {0,0,0},{10,0,0},{10,0,10}, {0,0,0},{10,0,10},{0,0,10},
                /* mesh 2 (opaque, later): in the floor's plane, sharing its x = 10 edge */
                {10,0,0},{12,0,5},{10,0,10},
                /* mesh 3 (decal, later): in the floor's plane */
                {5,0,5},{6,0,5},{6,0,6},
                /* mesh 4 (translucent, later): in the floor's plane */
                {7,0,7},{8,0,7},{8,0,8},
                /* mesh 5 (opaque, later): 0.5 above the floor */
                {1,0.5f,1},{3,0.5f,1},{3,0.5f,3}
            };
            unsigned k, nyield;
            memset(&ca, 0, sizeof ca);
            for (k = 0; k < 24u; k++) { cv[k].x = P[k][0]; cv[k].y = P[k][1]; cv[k].z = P[k][2]; }
            for (k = 0; k < 24u; k++) ci[k] = k;
            ca.vertices = cv; ca.indices = ci; ca.vertex_count = 24u; ca.index_count = 24u;
            ca.material_count = 6u; ca.mesh_count = 6u;
            for (k = 0; k < 6u; k++) { ca.materials[k].blend = SL_WA_BLEND_OPAQUE; ca.meshes[k].material = k; }
            ca.materials[3].flags = SL_WA_MAT_DECAL;
            ca.materials[4].blend = SL_WA_BLEND_ALPHA;
            ca.meshes[0].first_index = 0u;  ca.meshes[0].index_count = 6u;
            ca.meshes[1].first_index = 6u;  ca.meshes[1].index_count = 6u;
            ca.meshes[2].first_index = 12u; ca.meshes[2].index_count = 3u;
            ca.meshes[3].first_index = 15u; ca.meshes[3].index_count = 3u;
            ca.meshes[4].first_index = 18u; ca.meshes[4].index_count = 3u;
            ca.meshes[5].first_index = 21u; ca.meshes[5].index_count = 3u;
            nyield = sl_wa_coplanar_yield(&ca, y);
            ck(nyield == 3u && y[2] == 1u && y[3] == 1u,
               "both floor triangles under the earlier stripe yield (%u)", nyield);
            ck(y[0] == 0u && y[1] == 0u, "the earlier stripe itself does not yield");
            ck(y[4] == 0u, "a surface in the plane meeting the floor along an edge does not yield");
            ck(y[5] == 0u, "a decal draw in the plane takes no part");
            ck(y[6] == 1u, "#81 a translucent draw lying on the earlier floor yields to it");
            {
                unsigned char y3[12], o3[12];
                (void) sl_wa_coplanar_opposed_ex(&ca, y3, o3, 0);
                ck(y3[6] == 0u && y3[2] == 1u && y3[3] == 1u,
                   "with translucent meshes left out it takes no part, the rest unchanged");
            }
            ck(y[7] == 0u, "a surface half a unit above is not in the plane");
            /* the same pair in the other order: the floor is mesh 0, the
             * stripe mesh 1 - drawn after the floor, the stripe yields */
            for (k = 0; k < 6u; k++) {
                const struct sl_wa_vertex s = cv[k]; cv[k] = cv[k + 6u]; cv[k + 6u] = s;
            }
            nyield = sl_wa_coplanar_yield(&ca, y);
            ck(nyield == 3u && y[0] == 0u && y[1] == 0u && y[2] == 1u && y[3] == 1u && y[6] == 1u,
               "drawn after the floor, the stripe yields where it lies on it (%u)", nyield);
            /* TWO SIDES OF ONE WALL: every pair above faces one way */
            {
                unsigned char y2[12], o[12];
                unsigned nopp = sl_wa_coplanar_opposed(&ca, y2, o), same = 1;
                for (k = 0; k < 8u; k++) same &= (y2[k] == y[k]) && o[k] == 0u;
                ck(nopp == 0u && same,
                   "surfaces facing one way: no opposed pair, and the yields are the same (%u)",
                   nopp);
            }
        }
        /* #85 TWO DECAL-PASS SURFACES IN ONE PLANE (fabricated, Bunker 2's
         * cells): mesh 0 a stain in the decal pass (cutout), mesh 1 the floor
         * under it, also in the decal pass and listed later (translucent),
         * mesh 2 an opaque surface outside the pass, later, on the stain,
         * mesh 3 a decal-pass triangle on the stain facing the other way. */
        {
            static struct sl_wa_vertex dv[15];
            static unsigned di[15];
            static const float D[15][3] = {
                {2,0,2},{4,0,2},{4,0,4}, {2,0,2},{4,0,4},{2,0,4},
                {0,0,0},{10,0,0},{10,0,10},
                {3,0,3},{3.5f,0,3},{3.5f,0,3.5f},
                {2.5f,0,2.5f},{2.5f,0,3.5f},{3.5f,0,3.5f}
            };
            struct sl_wa_asset da;
            unsigned char y1[5], y2[5], o[5];
            unsigned k, ny, nopp;
            memset(&da, 0, sizeof da);
            for (k = 0; k < 15u; k++) { dv[k].x = D[k][0]; dv[k].y = D[k][1]; dv[k].z = D[k][2]; di[k] = k; }
            da.vertices = dv; da.indices = di; da.vertex_count = 15u; da.index_count = 15u;
            da.material_count = 4u; da.mesh_count = 4u;
            for (k = 0; k < 4u; k++) { da.materials[k].flags = SL_WA_MAT_DECAL; da.meshes[k].material = k; }
            da.materials[0].blend = SL_WA_BLEND_CUTOUT;
            da.materials[1].blend = SL_WA_BLEND_ALPHA;
            da.materials[2].blend = SL_WA_BLEND_OPAQUE; da.materials[2].flags = 0u;
            da.materials[3].blend = SL_WA_BLEND_CUTOUT;
            da.meshes[0].first_index = 0u;  da.meshes[0].index_count = 6u;
            da.meshes[1].first_index = 6u;  da.meshes[1].index_count = 3u;
            da.meshes[2].first_index = 9u;  da.meshes[2].index_count = 3u;
            da.meshes[3].first_index = 12u; da.meshes[3].index_count = 3u;
            ny = sl_wa_coplanar_yield(&da, y1);
            ck(y1[2] == 1u, "#85 a decal-pass floor listed after a decal-pass stain in its plane yields to it");
            ck(y1[0] == 0u && y1[1] == 0u, "#85 the earlier decal-pass stain does not yield");
            ck(y1[3] == 0u, "#85 a surface outside the decal pass takes no part against decal-pass ones");
            ck(y1[4] == 1u && ny == 2u, "#85 a later decal-pass triangle on the stain yields, whichever way it faces (%u)", ny);
            nopp = sl_wa_coplanar_opposed(&da, y2, o);
            ck(nopp == 0u && memcmp(y1, y2, sizeof y1) == 0,
               "#85 two decal-pass surfaces facing apart are never two sides of a wall (%u)", nopp);
            nopp = sl_wa_decal_needs_offset(&da, y2);
            ck(y2[0] == 1u && y2[1] == 1u && y2[2] == 1u && y2[4] == 1u && y2[3] == 0u && nopp == 4u,
               "#85 decal-pass surfaces in one plane keep the decal offset; one outside the pass is not marked (%u)",
               nopp);
        }
        /* #89 STACKED DECALS ON A SURFACE OUTSIDE THE PASS (fabricated,
         * Archives' blood pool and fan shadow; Surface's translucent layer
         * over a cutout one): in y = 0, mesh 0 an opaque floor, mesh 1 a
         * translucent decal, mesh 2 a cutout decal and mesh 3 a later cutout
         * decal over both, on the floor; meshes 4 and 5 a cutout pair on
         * nothing; mesh 6 a cutout decal on mesh 2's very triangle (a two-layer
         * draw's layer over its base). */
        {
            static struct sl_wa_vertex lv[21];
            static unsigned li[21];
            static const float L[21][2] = {
                {0,0},{10,0},{10,10},
                {2,1},{6,1},{6,5},
                {3,2},{5,2},{5,4},
                {3.5f,2.5f},{5.5f,2.5f},{5.5f,4.5f},
                {20,0},{22,0},{22,2},
                {20.5f,0.2f},{21.5f,0.2f},{21.5f,1.2f},
                {5,2},{5,4},{3,2}
            };
            struct sl_wa_asset la;
            unsigned char ly[7], lay[7];
            unsigned k, nk, ny;
            memset(&la, 0, sizeof la);
            memset(lv, 0, sizeof lv);
            for (k = 0; k < 21u; k++) { lv[k].x = L[k][0]; lv[k].z = L[k][1]; li[k] = k; }
            la.vertices = lv; la.indices = li; la.vertex_count = 21u; la.index_count = 21u;
            la.material_count = 7u; la.mesh_count = 7u;
            for (k = 0; k < 7u; k++) {
                la.materials[k].flags = SL_WA_MAT_DECAL; la.materials[k].blend = SL_WA_BLEND_CUTOUT;
                la.meshes[k].material = k; la.meshes[k].first_index = k * 3u; la.meshes[k].index_count = 3u;
            }
            la.materials[0].flags = 0u; la.materials[0].blend = SL_WA_BLEND_OPAQUE;
            la.materials[1].blend = SL_WA_BLEND_ALPHA;
            ny = sl_wa_coplanar_yield(&la, ly);
            ck(ny == 3u && ly[3] == 1u && ly[5] == 1u && ly[6] == 1u && ly[1] == 0u && ly[2] == 0u,
               "#89 fixture: the later decal of each pair yields, none yields to a translucent one (%u)", ny);
            nk = sl_wa_decal_layers(&la, ly, lay);
            ck(lay[3] == SL_WA_DECAL_KEEP && nk == 1u,
               "#89 a yielding decal lying on a surface outside the pass keeps the offset (%u)", nk);
            ck(lay[2] == SL_WA_DECAL_OVER, "#89 the decal it yields to draws one offset nearer");
            ck(lay[1] == 0u, "#89 a translucent decal under it is not the one yielded to: unmarked");
            ck(lay[5] == 0u && lay[4] == 0u,
               "#89 a decal pair lying on nothing keeps the #85 rule: unmarked");
            ck(lay[0] == 0u, "#89 the floor itself is unmarked");
            ck(lay[6] == 0u, "#89 a layer on its base's very triangle is one draw, not a stacked decal: unmarked");
            memset(ly, 0, sizeof ly);
            nk = sl_wa_decal_layers(&la, ly, lay);
            for (ny = 0, k = 0; k < 7u; k++) ny |= lay[k];
            ck(nk == 0u && ny == 0u, "#89 with nothing yielding nothing is marked (%u)", nk);
        }
        /* #85 WHICH DECALS ARE PULLED (fabricated): decal triangles in y = 0
         * at x 0..2, 10..12, 20..22, 30..32, 40..42, 50..52, and over each a
         * different neighbour - an opaque surface 1.0 above, nothing, an
         * opaque surface in the plane, an opaque one 3.0 above, a translucent
         * one 1.0 above, another decal 1.0 above; a last decal with an opaque
         * surface 1.0 BELOW it. */
        {
            static struct sl_wa_vertex pv[39];
            static unsigned pi[39];
            struct sl_wa_asset pa;
            unsigned char nd[13];
            unsigned k, np;
            static const float ox[7] = { 0, 10, 20, 30, 40, 50, 60 };
            static const float dy[7] = { 1.0f, 0.0f, 0.0f, 3.0f, 1.0f, 1.0f, -1.0f };
            memset(&pa, 0, sizeof pa);
            for (k = 0; k < 7u; k++) {             /* the decals, mesh 0 */
                pv[k * 3u + 0u].x = ox[k];      pv[k * 3u + 0u].z = 0.0f;
                pv[k * 3u + 1u].x = ox[k] + 2;  pv[k * 3u + 1u].z = 0.0f;
                pv[k * 3u + 2u].x = ox[k] + 2;  pv[k * 3u + 2u].z = 2.0f;
            }
            for (k = 0; k < 6u; k++) {             /* the neighbours, one mesh each */
                const unsigned s = (k == 0u) ? 0u : (k + 1u);   /* skip the bare decal */
                const unsigned b = 21u + k * 3u;
                pv[b + 0u].x = ox[s] + 0.5f; pv[b + 0u].y = dy[s]; pv[b + 0u].z = 0.5f;
                pv[b + 1u].x = ox[s] + 1.5f; pv[b + 1u].y = dy[s]; pv[b + 1u].z = 0.5f;
                pv[b + 2u].x = ox[s] + 1.5f; pv[b + 2u].y = dy[s]; pv[b + 2u].z = 1.5f;
            }
            for (k = 0; k < 39u; k++) pi[k] = k;
            pa.vertices = pv; pa.indices = pi; pa.vertex_count = 39u; pa.index_count = 39u;
            pa.material_count = 7u; pa.mesh_count = 7u;
            for (k = 0; k < 7u; k++) { pa.materials[k].blend = SL_WA_BLEND_OPAQUE; pa.meshes[k].material = k; }
            pa.materials[0].flags = SL_WA_MAT_DECAL; pa.materials[0].blend = SL_WA_BLEND_ALPHA;
            pa.materials[4].blend = SL_WA_BLEND_ALPHA;                 /* translucent, 1.0 above */
            pa.materials[5].flags = SL_WA_MAT_DECAL;                   /* a decal, 1.0 above */
            pa.meshes[0].first_index = 0u; pa.meshes[0].index_count = 21u;
            for (k = 1; k < 7u; k++) { pa.meshes[k].first_index = 18u + k * 3u; pa.meshes[k].index_count = 3u; }
            np = sl_wa_decal_needs_pull(&pa, nd);
            ck(nd[0] == 1u, "#85 a decal with an opaque surface 1.0 in front of it is pulled");
            ck(nd[1] == 0u, "#85 a decal with nothing near it is not pulled");
            ck(nd[2] == 0u, "#85 an opaque surface exactly in the decal's plane is the offset's, not the pull's");
            ck(nd[3] == 0u, "#85 a surface 3.0 off the plane is beyond the pull");
            ck(nd[4] == 0u, "#85 a translucent surface hides nothing: no pull for it");
            ck(nd[5] == 0u, "#85 another decal in front is not a reason to pull");
            ck(nd[6] == 1u, "#85 the surface may lie on either side of the plane");
            ck(np == 2u && nd[7] == 0u && nd[8] == 0u, "#85 only decal triangles are marked (%u)", np);
            /* #85 WHICH DECALS TAKE THE OFFSET, over the same fixture */
            np = sl_wa_decal_needs_offset(&pa, nd);
            ck(nd[0] == 1u && nd[2] == 1u && nd[6] == 1u,
               "#85 a decal on an opaque surface - in front, in its plane or behind, within the pull - keeps the offset");
            ck(nd[1] == 0u, "#85 a decal that lies on nothing draws without the decal offset");
            ck(nd[3] == 0u && nd[4] == 0u,
               "#85 a surface beyond the pull, or a translucent one, is not one the decal lies on");
            ck(nd[5] == 0u, "#85 another decal 1.0 off the plane is not one the decal lies on");
            ck(np == 3u, "#85 only decal triangles take the offset (%u)", np);
        }
        /* #90 WHICH TRANSLUCENT DECALS ARE DRAWN LATE (fabricated, Streets'
         * room 12 and Dam's bridge): one triangle a mesh, every plane facing
         * z. Mesh 0 a window (ALPHA, outside the pass) in z = 0; mesh 1 a
         * translucent decal on it (a tie); mesh 2 an opaque wall in z = 10 and
         * mesh 3 a translucent decal on it (the window 10 off: beyond the near
         * reach); mesh 4 a translucent decal 1.0 off the window; mesh 5 one in
         * z = 30 with nothing within reach; mesh 6 a panorama shell in z = 20
         * and mesh 7 a translucent decal on it; meshes 8 and 9 two translucent
         * decals on each other in z = 50 and on nothing else; mesh 10 a cutout
         * decal on the window; mesh 11 a cutout decal in z = 40 and mesh 12 a
         * translucent decal on it; mesh 13 an opaque deck in z = 60, mesh 14 a
         * translucent decal on it and mesh 15 a translucent ramp 3.0 over it
         * (Dam's planks); mesh 17 an opaque deck in z = 80, mesh 16 a
         * translucent decal on it and mesh 18 a translucent surface 10.0 over
         * it. Mesh 19 is two pieces of one translucent decal, triangle 19 on
         * the window (as mesh 1) and triangle 20 on the wall (as mesh 3):
         * Streets' mesh 91, a piece across a window and a piece on the wall
         * behind it. Mesh 20 is one piece of two triangles on an opaque deck
         * (mesh 21, z = 100) sharing an edge, the first under a small
         * translucent ramp 3.0 over it (mesh 22), the second beside the ramp
         * (Dam's planks, some of whose triangles are not parallel to the
         * ramps); mesh 23 a translucent decal of two pieces, one on that deck
         * and one in z = 120 on nothing. */
        {
            static struct sl_wa_vertex xv[81];
            static unsigned xi[81];
            static const float xz[19] = { 0, 0, 10, 10, 1, 30, 20, 20, 50, 50, 0, 40, 40,
                                          60, 60, 63, 80, 80, 90 };
            struct sl_wa_asset xa;
            unsigned char on[27];
            unsigned k, nx;
            memset(&xa, 0, sizeof xa);
            memset(xv, 0, sizeof xv);
            for (k = 0; k < 19u; k++) {
                const int big = (k == 0u || k == 2u || k == 6u || k == 11u || k == 13u || k == 15u
                                 || k == 17u || k == 18u);
                const float o = (k == 9u) ? 0.5f : 0.0f;
                xv[k * 3u + 0u].x = big ? 0.0f : 6.0f + o;  xv[k * 3u + 0u].y = big ? 0.0f : 1.2f;
                xv[k * 3u + 1u].x = big ? 10.0f : 8.0f - o; xv[k * 3u + 1u].y = big ? 0.0f : 1.2f;
                xv[k * 3u + 2u].x = big ? 10.0f : 8.0f - o; xv[k * 3u + 2u].y = big ? 10.0f : 3.0f - o;
                xv[k * 3u + 0u].z = xv[k * 3u + 1u].z = xv[k * 3u + 2u].z = xz[k];
                xi[k * 3u] = k * 3u; xi[k * 3u + 1u] = k * 3u + 1u; xi[k * 3u + 2u] = k * 3u + 2u;
                xa.materials[k].flags = SL_WA_MAT_DECAL; xa.materials[k].blend = SL_WA_BLEND_ALPHA;
                xa.meshes[k].material = k; xa.meshes[k].first_index = k * 3u; xa.meshes[k].index_count = 3u;
            }
            for (k = 19; k < 21u; k++) {                  /* mesh 19's two pieces */
                xv[k * 3u + 0u].x = 6.0f; xv[k * 3u + 0u].y = 1.2f;
                xv[k * 3u + 1u].x = 8.0f; xv[k * 3u + 1u].y = 1.2f;
                xv[k * 3u + 2u].x = 8.0f; xv[k * 3u + 2u].y = 3.0f;
                xv[k * 3u + 0u].z = xv[k * 3u + 1u].z = xv[k * 3u + 2u].z = (k == 19u) ? 0.0f : 10.0f;
                xi[k * 3u] = k * 3u; xi[k * 3u + 1u] = k * 3u + 1u; xi[k * 3u + 2u] = k * 3u + 2u;
            }
            xa.materials[19].flags = SL_WA_MAT_DECAL; xa.materials[19].blend = SL_WA_BLEND_ALPHA;
            xa.meshes[19].material = 19u; xa.meshes[19].first_index = 57u; xa.meshes[19].index_count = 6u;
            {
                static const float Z[6] = { 100, 100, 100, 103, 100, 120 };
                static const float X[6][3][2] = {
                    { {6, 1.2f}, {8, 1.2f}, {8, 3} },        /* 21: under the ramp */
                    { {8.0002f, 1.2f}, {9.5f, 1.2f}, {8.0002f, 3} },  /* 22: beside it, its own corners 0.0002 off */
                    { {0, 0}, {10, 0}, {10, 10} },           /* 23: the deck */
                    { {6, 1.2f}, {8, 1.2f}, {8, 3} },        /* 24: the ramp */
                    { {1.5f, 0.5f}, {3, 0.5f}, {3, 1.5f} },  /* 25: on the deck */
                    { {6, 1.2f}, {8, 1.2f}, {8, 3} }         /* 26: on nothing */
                };
                for (k = 21; k < 27u; k++) {
                    unsigned c;
                    for (c = 0; c < 3u; c++) {
                        xv[k * 3u + c].x = X[k - 21u][c][0]; xv[k * 3u + c].y = X[k - 21u][c][1];
                        xv[k * 3u + c].z = Z[k - 21u];
                        xi[k * 3u + c] = k * 3u + c;
                    }
                }
                for (k = 20; k < 24u; k++) xa.meshes[k].material = k;
                xa.materials[20].flags = SL_WA_MAT_DECAL; xa.materials[20].blend = SL_WA_BLEND_ALPHA;
                xa.materials[21].flags = 0u;              xa.materials[21].blend = SL_WA_BLEND_OPAQUE;
                xa.materials[22].flags = SL_WA_MAT_FOG;   xa.materials[22].blend = SL_WA_BLEND_ALPHA;
                xa.materials[23].flags = SL_WA_MAT_DECAL; xa.materials[23].blend = SL_WA_BLEND_ALPHA;
                xa.meshes[20].first_index = 63u; xa.meshes[20].index_count = 6u;
                xa.meshes[21].first_index = 69u; xa.meshes[21].index_count = 3u;
                xa.meshes[22].first_index = 72u; xa.meshes[22].index_count = 3u;
                xa.meshes[23].first_index = 75u; xa.meshes[23].index_count = 6u;
            }
            xa.vertices = xv; xa.indices = xi; xa.vertex_count = 81u; xa.index_count = 81u;
            xa.material_count = 24u; xa.mesh_count = 24u;
            xa.materials[0].flags = SL_WA_MAT_FOG;
            xa.materials[2].flags = 0u;  xa.materials[2].blend = SL_WA_BLEND_OPAQUE;
            xa.materials[6].flags = SL_WA_MAT_PANORAMA;
            xa.materials[10].blend = SL_WA_BLEND_CUTOUT;
            xa.materials[11].blend = SL_WA_BLEND_CUTOUT;
            xa.materials[13].flags = 0u; xa.materials[13].blend = SL_WA_BLEND_OPAQUE;
            xa.materials[15].flags = SL_WA_MAT_FOG;
            xa.materials[17].flags = 0u; xa.materials[17].blend = SL_WA_BLEND_OPAQUE;
            xa.materials[18].flags = 0u;
            nx = sl_wa_decal_late(&xa, on);
            ck(on[1] == SL_WA_DECAL_LATE_XLU,
               "#90 a translucent decal lying on a window (a tie) lies on a translucent surface");
            ck(on[4] == SL_WA_DECAL_LATE_XLU, "#90 ... and one 1.0 off the window's plane");
            ck(on[14] == SL_WA_DECAL_LATE_XLU,
               "#90 a translucent decal on an opaque deck with a translucent ramp 3.0 over it stays late");
            ck(on[3] == 0u, "#90 a translucent decal on an opaque wall, a window 10.0 off, is drawn with the wall");
            ck(on[16] == 0u, "#90 ... and one with a translucent surface 10.0 over its deck");
            ck(on[12] == 0u, "#90 ... and one on a cutout decal, which writes depth");
            ck(on[5] == SL_WA_DECAL_LATE_LONE, "#90 a translucent decal with nothing within reach lies on nothing");
            ck(on[7] == SL_WA_DECAL_LATE_LONE,
               "#90 the panorama shell is neither a translucent surface it lies on nor a solid one");
            ck(on[8] == SL_WA_DECAL_LATE_LONE && on[9] == SL_WA_DECAL_LATE_LONE,
               "#90 two translucent decals on each other and nothing else lie on nothing");
            ck(on[10] == 0u && on[0] == 0u && on[2] == 0u && on[6] == 0u && on[11] == 0u
               && on[13] == 0u && on[15] == 0u && on[17] == 0u && on[18] == 0u,
               "#90 only translucent decal meshes are marked");
            ck(on[19] == SL_WA_DECAL_LATE_XLU && on[20] == 0u,
               "#90 per piece: of one decal mesh, the piece on the window is late, the piece on the wall is not");
            ck(on[21] == SL_WA_DECAL_LATE_XLU && on[22] == SL_WA_DECAL_LATE_XLU,
               "#90 a triangle joined to one under a translucent surface goes late with its piece");
            ck(on[25] == SL_WA_DECAL_LATE_LONE && on[26] == SL_WA_DECAL_LATE_LONE,
               "#90 a piece lying on nothing takes its whole mesh late");
            ck(nx == 12u, "#90 twelve triangles marked (%u)", nx);
            ck(sl_wa_decal_mix(&xa, on, 19u) == (SL_WA_DECAL_MIX_LATE | SL_WA_DECAL_MIX_EARLY)
               && sl_wa_decal_mix(&xa, on, 1u) == SL_WA_DECAL_MIX_LATE
               && sl_wa_decal_mix(&xa, on, 3u) == SL_WA_DECAL_MIX_EARLY
               && sl_wa_decal_mix(&xa, NULL, 19u) == SL_WA_DECAL_MIX_EARLY
               && sl_wa_decal_mix(&xa, on, 20u) == SL_WA_DECAL_MIX_LATE
               && sl_wa_decal_mix(&xa, on, 23u) == SL_WA_DECAL_MIX_LATE
               && sl_wa_decal_mix(&xa, on, 24u) == 0u,
               "#90 a mixed decal mesh takes part in both draws; a whole one in one; no mesh past the end");
            ck(sl_wa_blend_late(xa.materials[3].flags, xa.materials[3].blend, on[3], 1) == 0
               && sl_wa_blend_late(xa.materials[1].flags, xa.materials[1].blend, on[1], 1) == 1
               && sl_wa_blend_late(xa.materials[5].flags, xa.materials[5].blend, on[5], 1) == 1
               && sl_wa_blend_late(xa.materials[14].flags, xa.materials[14].blend, on[14], 1) == 1
               && sl_wa_blend_late(xa.materials[0].flags, xa.materials[0].blend, on[0], 1) == 1,
               "#90 over the fixture: the window, the decals on or under translucent surfaces and the lone decal late, the wall's decal first");
        }
        /* #77 TWO SIDES OF ONE WALL. Mesh 0 is a wall in z = 0 facing +z (two
         * triangles); mesh 1, listed later, the next room's wall facing -z in
         * z = -0.0001 across both triangles; mesh 2 a later surface facing +z in
         * the plane (one way: no pair); mesh 3 facing -z one unit away. */
        {
            static struct sl_wa_vertex ov[15];
            static unsigned oi[15];
            static const float Q[15][3] = {
                {0,0,0},{10,0,0},{10,10,0}, {0,0,0},{10,10,0},{0,10,0},
                {2,1,-0.0001f},{2,9,-0.0001f},{9,9,-0.0001f},
                {8,1,0},{9,1,0},{9,2,0},
                {1,1,-1},{1,4,-1},{4,4,-1}
            };
            static const float ez[3] = { 3.0f, 3.0f, 5.0f }, emz[3] = { 3.0f, 3.0f, -5.0f },
                               e0[3] = { 3.0f, 3.0f, 0.0f };
            struct sl_wa_asset oa;
            unsigned char y1[5], y2[5], o[5];
            unsigned k, nopp, ny;
            memset(&oa, 0, sizeof oa);
            for (k = 0; k < 15u; k++) { ov[k].x = Q[k][0]; ov[k].y = Q[k][1]; ov[k].z = Q[k][2]; oi[k] = k; }
            oa.vertices = ov; oa.indices = oi; oa.vertex_count = 15u; oa.index_count = 15u;
            oa.material_count = 4u; oa.mesh_count = 4u;
            for (k = 0; k < 4u; k++) { oa.materials[k].blend = SL_WA_BLEND_OPAQUE; oa.meshes[k].material = k; }
            oa.meshes[0].first_index = 0u;  oa.meshes[0].index_count = 6u;
            oa.meshes[1].first_index = 6u;  oa.meshes[1].index_count = 3u;
            oa.meshes[2].first_index = 9u;  oa.meshes[2].index_count = 3u;
            oa.meshes[3].first_index = 12u; oa.meshes[3].index_count = 3u;
            ny = sl_wa_coplanar_yield(&oa, y1);
            nopp = sl_wa_coplanar_opposed(&oa, y2, o);
            ck(memcmp(y1, y2, sizeof y1) == 0 && ny == 2u && y1[2] == 1u && y1[3] == 1u,
               "the two-sided walk fills the yields exactly as the order rule does (%u)", ny);
            ck(nopp == 3u && o[0] == 1u && o[1] == 1u && o[2] == 1u,
               "a wall and the next room's wall back to back in its plane are both marked (%u)",
               nopp);
            ck(o[3] == 0u, "a surface facing the same way in the plane is not two-sided");
            ck(o[4] == 0u, "a wall facing the other way one unit off is not in the plane");
            ck(sl_wa_tri_faces(&ov[0], &ov[1], &ov[2], ez) && !sl_wa_tri_faces(&ov[6], &ov[7], &ov[8], ez),
               "from the +z side the +z wall faces the eye and the other side does not");
            ck(!sl_wa_tri_faces(&ov[0], &ov[1], &ov[2], emz) && sl_wa_tri_faces(&ov[6], &ov[7], &ov[8], emz),
               "from the other room the other side faces the eye");
            ck(!sl_wa_tri_faces(&ov[0], &ov[1], &ov[2], e0),
               "an eye in the plane sees the wall edge-on: it faces neither way");
        }
        /* #77 THE TOLERANCE AT A DISTANCE. Two shells of one dome share
         * their vertices but not their triangulation, so a pair's plane
         * offsets differ by the rounding of float32 positions at the dome's
         * distance from the origin: 0.3 units at 41000 (Surface's dome,
         * measured 0.0005..0.4021). Mesh 0 faces +y; mesh 1, later, faces -y
         * 0.3 below it, both at x,z about 41000 - a pair; meshes 2/3 are the
         * same two triangles 0.3 apart at the origin - not a pair. */
        {
            static struct sl_wa_vertex fv[12];
            static unsigned fi[12];
            static const float R[12][3] = {
                {41000,100,-41000},{41200,100,-41000},{41200,100,-40800},
                {41050,99.7f,-40980},{41150,99.7f,-40900},{41150,99.7f,-40980},
                {0,100,0},{200,100,0},{200,100,200},
                {50,99.7f,20},{150,99.7f,100},{150,99.7f,20}
            };
            struct sl_wa_asset fa;
            unsigned char y[4], o[4];
            unsigned k, nopp;
            memset(&fa, 0, sizeof fa);
            for (k = 0; k < 12u; k++) { fv[k].x = R[k][0]; fv[k].y = R[k][1]; fv[k].z = R[k][2]; fi[k] = k; }
            fa.vertices = fv; fa.indices = fi; fa.vertex_count = 12u; fa.index_count = 12u;
            fa.material_count = 4u; fa.mesh_count = 4u;
            for (k = 0; k < 4u; k++) {
                fa.materials[k].blend = SL_WA_BLEND_OPAQUE; fa.meshes[k].material = k;
                fa.meshes[k].first_index = 3u * k; fa.meshes[k].index_count = 3u;
            }
            ck(sl_wa_coplanar_eps(0.0, 0.0) == SL_WA_COPLANAR_EPS
               && sl_wa_coplanar_eps(41000.0, -41000.0) > 0.8 && sl_wa_coplanar_eps(-41000.0, 1.0) > 0.8,
               "the tolerance is the flat one at the origin and grows with the offset's magnitude (%.3f)",
               sl_wa_coplanar_eps(41000.0, 0.0));
            nopp = sl_wa_coplanar_opposed(&fa, y, o);
            ck(nopp == 2u && o[0] == 1u && o[1] == 1u && y[1] == 1u,
               "at 41000 units two shells 0.3 apart are two sides of one surface (%u)", nopp);
            ck(o[2] == 0u && o[3] == 0u && y[3] == 0u,
               "at the origin the same two surfaces 0.3 apart are not in one plane");
        }
        /* #80 TWO SIDES OF ONE PLATE IN ONE MESH. Mesh 0 holds a plate's top
         * (two triangles facing +y) and its underside (one triangle facing
         * -y, triangulated differently, 0.01 lower) and, in the same plane,
         * a second patch facing +y; mesh 1 is unrelated. The top and the
         * underside are two sides; the two +y surfaces of one mesh are not
         * ordered (one draw) and stay unmarked. */
        {
            static struct sl_wa_vertex pv[15];
            static unsigned pi[15];
            static const float S[15][3] = {
                {0,5,0},{10,5,10},{10,5,0},  {0,5,0},{0,5,10},{10,5,10},
                {1,4.99f,1},{9,4.99f,1},{5,4.99f,9},
                {8,5,8},{9,5,9},{9,5,8},
                {0,50,0},{1,50,1},{1,50,0}
            };
            static const float below[3] = { 5.0f, 0.0f, 5.0f };
            struct sl_wa_asset pa;
            unsigned char y[5], o[5];
            unsigned k, nopp;
            memset(&pa, 0, sizeof pa);
            for (k = 0; k < 15u; k++) { pv[k].x = S[k][0]; pv[k].y = S[k][1]; pv[k].z = S[k][2]; pi[k] = k; }
            pa.vertices = pv; pa.indices = pi; pa.vertex_count = 15u; pa.index_count = 15u;
            pa.material_count = 2u; pa.mesh_count = 2u;
            for (k = 0; k < 2u; k++) { pa.materials[k].blend = SL_WA_BLEND_OPAQUE; pa.meshes[k].material = k; }
            pa.meshes[0].first_index = 0u;  pa.meshes[0].index_count = 12u;
            pa.meshes[1].first_index = 12u; pa.meshes[1].index_count = 3u;
            ck(!sl_wa_tri_faces(&pv[0], &pv[1], &pv[2], below)
               && sl_wa_tri_faces(&pv[6], &pv[7], &pv[8], below),
               "the plate fixture: the top faces up and the underside down");
            nopp = sl_wa_coplanar_opposed(&pa, y, o);
            ck(o[0] == 1u && o[1] == 1u && o[2] == 1u,
               "a plate's top and underside inside one mesh are two sides (%u)", nopp);
            ck(o[3] == 0u && o[4] == 0u,
               "a second surface facing the same way in one mesh is not two-sided");
            ck(y[0] == 0u && y[1] == 0u && y[2] == 0u && y[3] == 0u && y[4] == 0u
               && sl_wa_coplanar_yield(&pa, y) == 0u,
               "nothing inside one mesh yields - its order is the draw's own");
        }
        /* #77 THE BACKDROP. Black only under a replacement, only with no sky
         * drawn, only where the stage's backdrop is the flat fill. */
        ck(sl_wa_backdrop_black(1, 0, 1),
           "a replacement on a stage with no sky clears to the remaster's black");
        ck(!sl_wa_backdrop_black(1, 1, 0) && !sl_wa_backdrop_black(1, 1, 1),
           "a frame whose stage sky drew keeps it (the Dam)");
        ck(!sl_wa_backdrop_black(1, 0, 0),
           "a stage with a sky whose dome did not draw keeps the game's own sky");
        ck(!sl_wa_backdrop_black(0, 0, 1),
           "no replacement: the game's own fill is the backdrop");
        /* #81 the fogless stage: only under a replacement, only with no
         * game environment row */
        ck(sl_wa_fogless_range(1, -1),
           "a replacement on a fogless stage keeps the game's range and fill (Bunker 1)");
        ck(!sl_wa_fogless_range(1, 34) && !sl_wa_fogless_range(1, 33) && !sl_wa_fogless_range(1, 0),
           "a stage with a fog row is unchanged (Facility, the Dam)");
        ck(!sl_wa_fogless_range(0, -1),
           "no replacement: nothing to range");
        /* #78 THE FAR PLANE, recovered from a world-to-clip matrix built the
         * way the pass's is: an affine view (rotation about y, a scale, a
         * translation) times a perspective with near 10 and far 15000. */
        {
            const double n = 10.0, f = 15000.0, cy = 0.8, sy = 0.6, s = 0.2;
            const double pk = -(f + n) / (f - n), pm = -2.0 * f * n / (f - n);
            double V[16] = { 0 }, P[16] = { 0 };
            float C[16], zf = -1.0f, k1, k2, O[16] = { 0 };
            int r, c, q;
            V[0] = cy * s;  V[2] = -sy * s;  V[5] = s;  V[8] = sy * s;  V[10] = cy * s;
            V[12] = 120.0;  V[13] = -40.0;   V[14] = -3000.0;  V[15] = 1.0;
            P[0] = 1.2;  P[5] = 1.6;  P[10] = pk;  P[11] = -1.0;  P[14] = pm;
            for (r = 0; r < 4; r++)
                for (c = 0; c < 4; c++) {
                    double acc = 0.0;
                    for (q = 0; q < 4; q++) acc += V[r * 4 + q] * P[q * 4 + c];
                    C[r * 4 + c] = (float) acc;
                }
            ck(sl_wa_far_plane(C, &zf) && fabs(zf - 15000.0) < 1.0,
               "the far plane is recovered from the world-to-clip matrix (%.3f)", (double) zf);
            O[0] = 1.0f; O[5] = 1.0f; O[10] = -0.001f; O[15] = 1.0f;
            zf = -1.0f;
            ck(!sl_wa_far_plane(O, &zf) && zf == -1.0f,
               "an orthographic matrix has no far plane to recover and writes nothing");
            sl_wa_far_range(14985.0f, 43000.0f, &k1, &k2);
            ck(fabs((k1 * 14985.0f - k2) / 14985.0f + 1.0) < 1e-4
               && fabs((k1 * 43000.0f - k2) / 43000.0f - 1.0) < 1e-4
               && (k1 * 30000.0f - k2) / 30000.0f < (k1 * 31000.0f - k2) / 31000.0f,
               "the first range maps its near w to -1 and its far w to +1, in order");
            /* #80 the overlap: the first range starts well inside the far
             * plane - more than the 0.3% the second range's clip was
             * measured to fall short by - and never at or beyond it. */
            ck(SL_WA_FAR_OVERLAP < 0.997f && SL_WA_FAR_OVERLAP > 0.9f,
               "the two depth ranges overlap by more than the far clip's measured shortfall");
            ck(SL_WA_FAR_OVERLAP_BLEND > SL_WA_FAR_OVERLAP && SL_WA_FAR_OVERLAP_BLEND < 1.0f,
               "a blended draw keeps a narrower overlap than an opaque one, inside the far plane");
            ck(SL_WA_BLEND_BLENDS(SL_WA_BLEND_ALPHA) && SL_WA_BLEND_BLENDS(SL_WA_BLEND_CUTOUT)
               && !SL_WA_BLEND_BLENDS(SL_WA_BLEND_OPAQUE),
               "the translucent and cutout classes blend; the opaque one does not");
        }
        /* The band decode is INDEPENDENT of it: a report has to be able to
         * print what the level is issuing even where it is not applied. */
        {
            float s = -1.0f, e = -1.0f;
            ck(sl_wa_fog_band(1, 25600, -25344, 0u, &s, &e) && s > 0.98f,
               "the band is still decoded where the destination rule declines "
               "it, so the census can report what the level issues");
        }

        /* #68 THE SCENE-ASSEMBLY RULE. Two states, and the console measured
         * the second: the remaster loads the stage sky inside its background
         * load, draws it as the frame's first world draw, and removing it
         * alone moves 658731 of 1009584 pixels above the replacement's rim.
         * The rule names neither an item nor a level. */
        ck(sl_wa_compose_skydome(0),
           "with no replacement background the skydome is composed - "
           "additive over the stage's own backdrop");
        ck(sl_wa_compose_skydome(1),
           "with a replacement background it is ALSO composed - it is the "
           "replacement's companion and owns the sky above its rim");

        /* #68 THE PER-OBJECT ATMOSPHERE. The remaster binds the backdrop's
         * stage fog to its prop and character draws too (measured: the same
         * c0/c1 on every such gameplay draw, c7 the shade alone), so the row
         * that fogs the replacement fogs the objects in front of it; without
         * a replacement, or without the metadata, the game's own per-object
         * fog stands. */
        {
            struct sl_wa_env r;
            memset(&r, 0, sizeof r);
            r.id = 7u; r.fog_start = -100.0f; r.fog_end = 900.0f;
            r.rgb[0] = 10; r.rgb[1] = 20; r.rgb[2] = 30;
            ck(sl_wa_object_fog_env(1, &r) == &r,
               "with a replacement background and its environment row, the "
               "remaster row fogs the game's props and characters");
            ck(sl_wa_object_fog_env(0, &r) == (const struct sl_wa_env *) 0,
               "with no replacement (ORIGINAL / COMMUNITY select no world "
               "assets, or the stage has none) the game's own per-object fog "
               "stands, whatever metadata exists");
            ck(sl_wa_object_fog_env(1, (const struct sl_wa_env *) 0)
                   == (const struct sl_wa_env *) 0,
               "with the metadata missing, invalid, or without a row for the "
               "game's environment, it falls back to the game's own fog");
            ck(sl_wa_object_fog_env(0, (const struct sl_wa_env *) 0)
                   == (const struct sl_wa_env *) 0,
               "and with neither, likewise");
            /* One atmosphere per scene: the objects take the remaster row
             * exactly where the backdrop does, never where the level's band
             * still applies to the backdrop. */
            ck((sl_wa_object_fog_env(1, &r) != (const struct sl_wa_env *) 0)
                   == !sl_wa_fog_level_applies(1)
               && (sl_wa_object_fog_env(0, &r) != (const struct sl_wa_env *) 0)
                   == !sl_wa_fog_level_applies(0),
               "the objects and the backdrop are fogged by the same atmosphere "
               "in both states");
            /* The class's distance is the backdrop's (level-scaled world units
             * before the visibility scale), so an object takes the row's own
             * coordinate for its distance - the backdrop's formula, not a
             * second one. */
            ck(fabsf(sl_wa_env_fog_coord(sl_wa_object_fog_env(1, &r), 400.0f)
                     - 0.5f) < 1e-6f,
               "an object half way through the row's range takes the row's "
               "half-way coordinate");
        }
    }

    /* ---- #70 SKINNED ITEMS: version 4, classes CHAR and HEAD ------------ */
    {
        const struct sl_wa_asset *got;
        char d[700];
        unsigned vl;

        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        ck(parse_fix(vl) == SL_WA_OK, "a valid version-4 character item parses");
        ck(sl_wa_parse(g_fix, vl, &a) == SL_WA_OK && a.joint_count == 2
           && a.skin != NULL && a.skin[1].joint[0] == 1 && a.skin[1].weight[0] == 255
           && strcmp(a.joints[1].name, "SKEL_NECK") == 0 && a.joints[1].parent == 0
           && a.joints[0].parent == -1 && a.joints[1].bind[1] == 10.0f,
           "its joints and its skin read back as written");
        ck(a.meshes[1].flags == SL_WA_MESH_BUILTIN_HEAD && a.meshes[0].flags == 0,
           "the built-in-head mark survives on its own mesh only");
        vl = build_skinned("testguard", SL_WA_CLASS_HEAD);
        ck(parse_fix(vl) == SL_WA_OK, "a head item is the same layout");

        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        g_fix[g_skin_off + 4] = 254;                     /* weights sum 254 */
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a skin entry whose weights do not sum to 255 is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        g_fix[g_skin_off + 1] = 2;                       /* a joint past the table */
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a skin entry naming a joint past the table is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_joint_off + 24, 1);            /* the root names itself */
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a joint whose parent is not an earlier joint is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        g_fix[g_joint_off + 48 + 5] = 'k';               /* lower case in a name */
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a joint name outside [A-Z0-9_] is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_f32(g_fix + g_joint_off + 28, (float) (1e30 * 1e30));   /* an infinity */
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a non-finite bind position is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_SKYDOME);
        ck(parse_fix(vl) == SL_WA_E_CLASS, "version 4 carries characters and heads only");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + 4, 1);
        ck(parse_fix(vl) == SL_WA_E_CLASS, "a character cannot arrive in a layout with no skin");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + 44, 128);
        ck(parse_fix(vl) == SL_WA_E_FLAGS, "the material-extension word stays reserved in version 4");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + 116, 0);
        ck(parse_fix(vl) == SL_WA_E_LAYOUT, "a version-4 item with no skin section is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + 124, 0);
        ck(parse_fix(vl) == SL_WA_E_COUNT, "a version-4 item with no joints is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_mesh_off + 16 + 12, 4);        /* an undefined mesh flag */
        ck(parse_fix(vl) == SL_WA_E_MESH, "a mesh flag version 4 does not define is refused");

        /* a body item that carries its own head: every mesh or none */
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_mesh_off + 12, SL_WA_MESH_OWN_HEAD);
        put_u32(g_fix + g_mesh_off + 16 + 12, SL_WA_MESH_OWN_HEAD);
        ck(sl_wa_parse(g_fix, vl, &a) == SL_WA_OK && a.meshes[0].flags == SL_WA_MESH_OWN_HEAD
           && a.meshes[1].flags == SL_WA_MESH_OWN_HEAD,
           "a body item carrying its own head on every mesh parses, the mark read back");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_mesh_off + 16 + 12, SL_WA_MESH_OWN_HEAD);
        ck(parse_fix(vl) == SL_WA_E_MESH, "the own-head mark on some meshes only is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_mesh_off + 12, SL_WA_MESH_OWN_HEAD);
        put_u32(g_fix + g_mesh_off + 16 + 12, SL_WA_MESH_OWN_HEAD | SL_WA_MESH_BUILTIN_HEAD);
        ck(parse_fix(vl) == SL_WA_E_MESH, "a built-in head in an item that owns its head is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_HEAD);
        put_u32(g_fix + g_mesh_off + 12, SL_WA_MESH_OWN_HEAD);
        put_u32(g_fix + g_mesh_off + 16 + 12, SL_WA_MESH_OWN_HEAD);
        ck(parse_fix(vl) == SL_WA_E_MESH, "a head item cannot carry the own-head mark");

        /* a view model (class GUN): the game part and the switch conditions */
        vl = build_skinned("testgun", SL_WA_CLASS_GUN);
        put_u32(g_fix + g_mesh_off + 12, 0);
        put_u32(g_fix + g_mesh_off + 16 + 12, (12u << 8) | (14u << 16));
        put_u32(g_fix + g_joint_off + SL_WA_JOINT_SIZE + 40, 7u);
        ck(sl_wa_parse(g_fix, vl, &a) == SL_WA_OK && a.asset_class == SL_WA_CLASS_GUN
           && a.joints[1].part == 7u && a.joints[0].part == 0u
           && SL_WA_MESH_SWITCH_A(a.meshes[1].flags) == 12u
           && SL_WA_MESH_SWITCH_B(a.meshes[1].flags) == 14u,
           "a view model parses: its joint's game part and its draw's switch conditions read back");
        vl = build_skinned("testprop", SL_WA_CLASS_PROP);
        put_u32(g_fix + g_mesh_off + 28, 0);
        ck(sl_wa_parse(g_fix, vl, &a) == SL_WA_OK && a.asset_class == SL_WA_CLASS_PROP,
           "a prop parses as a version-4 item of its own class");
        /* THE MOVING PROP: a prop's joint may name the model's matrix (a
         * vehicle's wheel), and nothing else */
        put_u32(g_fix + g_joint_off + SL_WA_JOINT_SIZE + 40, SL_WA_PART_MATRIX + 3u);
        ck(sl_wa_parse(g_fix, vl, &a) == SL_WA_OK && a.joints[1].part == SL_WA_PART_MATRIX + 3u,
           "a prop's joint on the model's matrix 2 parses");
        put_u32(g_fix + g_joint_off + SL_WA_JOINT_SIZE + 40, 5u);
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a prop's joint names no Switches[] part");
        put_u32(g_fix + g_joint_off + SL_WA_JOINT_SIZE + 40,
                SL_WA_PART_MATRIX + SL_WA_MAX_PART_MATRIX + 1u);
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a prop's matrix past the model's limit is refused");
        {
            /* a view model's material may name its REFLECTION image */
            unsigned mo = SL_WA_HEADER + 4u * SL_WA_VERTEX_SIZE + 6u * SL_WA_INDEX_SIZE;
            vl = build_skinned("testgun", SL_WA_CLASS_GUN);
            put_u32(g_fix + g_mesh_off + 28, 0);
            put_u32(g_fix + mo + 12, 1u);
            ck(sl_wa_parse(g_fix, vl, &a) == SL_WA_OK && a.materials[0].reflect == 1u,
               "a view model's material names its reflection image (1 + index)");
            put_u32(g_fix + mo + 12, 2u);
            ck(parse_fix(vl) == SL_WA_E_MATERIAL, "a reflection past the images is refused");
            vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
            put_u32(g_fix + mo + 12, 1u);
            ck(parse_fix(vl) == SL_WA_E_MATERIAL, "a character's material names no reflection");
            /* ... and a prop's may (Egyptian's Golden Gun pickup) */
            vl = build_skinned("testprop", SL_WA_CLASS_PROP);
            put_u32(g_fix + g_mesh_off + 28, 0);
            put_u32(g_fix + mo + 12, 1u);
            ck(sl_wa_parse(g_fix, vl, &a) == SL_WA_OK && a.materials[0].reflect == 1u,
               "a prop's material names its reflection image (1 + index)");
            put_u32(g_fix + mo + 12, 2u);
            ck(parse_fix(vl) == SL_WA_E_MATERIAL, "a prop's reflection past the images is refused");
        }
        vl = build_skinned("testgun", SL_WA_CLASS_GUN);   /* mesh 1 carries BUILTIN_HEAD */
        ck(parse_fix(vl) == SL_WA_E_MESH, "the built-in-head mark is refused on a view model");
        vl = build_skinned("testgun", SL_WA_CLASS_GUN);
        put_u32(g_fix + g_mesh_off + 16 + 12, (SL_WA_MAX_SWITCH + 1u) << 8);
        ck(parse_fix(vl) == SL_WA_E_MESH, "a switch condition past the table is refused");
        vl = build_skinned("testgun", SL_WA_CLASS_GUN);
        put_u32(g_fix + g_mesh_off + 16 + 12, 0);
        put_u32(g_fix + g_joint_off + 40, SL_WA_MAX_SWITCH + 1u);
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a game part past the table is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_joint_off + 40, 1u);
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a character's joint carries no game part");
        /* THE WATCH HAND: a joint named by the model's matrix, a draw marked
         * as one of the item's alternatives */
        vl = build_skinned("testgun", SL_WA_CLASS_GUN);
        put_u32(g_fix + g_mesh_off + 12, 0);
        put_u32(g_fix + g_mesh_off + 16 + 12, SL_WA_MESH_ALT | (5u << 8));
        put_u32(g_fix + g_joint_off + SL_WA_JOINT_SIZE + 40, SL_WA_PART_MATRIX + 1u);
        ck(sl_wa_parse(g_fix, vl, &a) == SL_WA_OK && a.joints[1].part == SL_WA_PART_MATRIX + 1u
           && a.meshes[1].flags == (SL_WA_MESH_ALT | (5u << 8)),
           "a watch hand parses: a joint on the model's matrix 0, a sleeve serving Switches[4]");
        put_u32(g_fix + g_joint_off + SL_WA_JOINT_SIZE + 40, SL_WA_PART_MATRIX);
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a matrix part with no matrix is refused");
        put_u32(g_fix + g_joint_off + SL_WA_JOINT_SIZE + 40,
                SL_WA_PART_MATRIX + SL_WA_MAX_PART_MATRIX + 1u);
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a matrix past the model's limit is refused");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_mesh_off + 12, 0);
        put_u32(g_fix + g_mesh_off + 16 + 12, SL_WA_MESH_ALT);
        ck(parse_fix(vl) == SL_WA_E_MESH, "a character's draw is no alternative sleeve");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_joint_off + 40, SL_WA_PART_MATRIX + 1u);
        ck(parse_fix(vl) == SL_WA_E_SKIN, "a character's joint names no model matrix");
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        put_u32(g_fix + g_mesh_off + 16 + 12, 12u << 8);
        ck(parse_fix(vl) == SL_WA_E_MESH, "a character's draw carries no switch condition");
        len = build("dam", 4, 2, 1, 1, 1);
        put_u32(g_fix + SL_WA_HEADER + 4 * 24 + 6 * 4 + 16 + 16 + 12, SL_WA_MESH_BUILTIN_HEAD);
        ck(parse_fix(len) == SL_WA_E_MESH, "the built-in-head mark is refused below version 4");

        /* the provider, against files: XBLA only, per item, refusing safely */
        snprintf(d, sizeof d, "%s\\xbla\\char", sub);    MKDIR(d);
        snprintf(d, sizeof d, "%s\\xbla\\head", sub);    MKDIR(d);
        snprintf(file, sizeof file, "%s\\xbla\\char\\testguard.slwa", sub);
        vl = build_skinned("testguard", SL_WA_CLASS_CHAR);
        wr(file, g_fix, vl);
        snprintf(file, sizeof file, "%s\\xbla\\char\\badguard.slwa", sub);
        vl = build_skinned("badguard", SL_WA_CLASS_CHAR);
        g_fix[g_skin_off + 4] = 7;
        wr(file, g_fix, vl);

        putenv((char *) "SL_WORLD_ASSETS=");
        sl_presentation_set(SL_PRESENTATION_ORIGINAL);
        ck(sl_wa_lookup(SL_WA_CLASS_CHAR, "testguard") == NULL,
           "PRESENTATION ORIGINAL never consumes a character item");
        sl_presentation_set(SL_PRESENTATION_COMMUNITY);
        ck(sl_wa_lookup(SL_WA_CLASS_CHAR, "testguard") == NULL,
           "PRESENTATION COMMUNITY HD never consumes a character item");
        sl_presentation_set(SL_PRESENTATION_XBLA);
        got = sl_wa_lookup(SL_WA_CLASS_CHAR, "testguard");
        ck(got != NULL && got->joint_count == 2 && got->skin != NULL,
           "PRESENTATION XBLA finds the item and carries its skeleton");
        ck(sl_wa_lookup(SL_WA_CLASS_HEAD, "testguard") == NULL,
           "a class is its own folder: a body item does not answer for a head");
        ck(sl_wa_lookup(SL_WA_CLASS_HEAD, "headnone") == NULL,
           "a head with no item is a miss - the game's own head draws");
        ck(sl_wa_lookup(SL_WA_CLASS_CHAR, "badguard") == NULL,
           "a malformed character item is refused, never drawn");
        sl_presentation_set(SL_PRESENTATION_ORIGINAL);
        ck(sl_wa_lookup(SL_WA_CLASS_CHAR, "testguard") == NULL,
           "leaving XBLA stops answering on the very next ask");
        sl_presentation_set(SL_PRESENTATION_XBLA);
        ck(sl_wa_lookup(SL_WA_CLASS_CHAR, "testguard") == got,
           "returning to XBLA answers from the same resident item");

        /* view models: their own folder, the same presentation rule */
        snprintf(d, sizeof d, "%s\\xbla\\gun", sub);     MKDIR(d);
        snprintf(file, sizeof file, "%s\\xbla\\gun\\testgun.slwa", sub);
        vl = build_skinned("testgun", SL_WA_CLASS_GUN);
        put_u32(g_fix + g_mesh_off + 16 + 12, 12u << 8);
        wr(file, g_fix, vl);
        got = sl_wa_lookup(SL_WA_CLASS_GUN, "testgun");
        ck(got != NULL && got->asset_class == SL_WA_CLASS_GUN
           && SL_WA_MESH_SWITCH_A(got->meshes[1].flags) == 12u,
           "PRESENTATION XBLA finds a view model under gun/");
        ck(sl_wa_lookup(SL_WA_CLASS_CHAR, "testgun") == NULL,
           "a view model does not answer for a body of the same name");
    }

    /* #78 THE NATIVE FILL's grid: a square of two triangles at z = 0 over
     * 0..100, a blended square beside it (never a surface), one wide
     * triangle (125 cells of 16: a coarser level) at z = 1000, and one
     * wider than the coarsest level (600000 units) at z = 5000, listed */
    {
        sl_presentation_set(SL_PRESENTATION_XBLA);
        static struct sl_wa_asset fa;
        static struct sl_wa_vertex fv[14];
        static unsigned fi[18] = { 0, 1, 2, 0, 2, 3,  4, 5, 6, 4, 6, 7,  8, 9, 10,  11, 12, 13 };
        float p[3], d;
        unsigned nl = 99;
        memset(&fa, 0, sizeof fa);
        memset(fv, 0, sizeof fv);
        fv[1].x = 100.0f; fv[2].x = 100.0f; fv[2].y = 100.0f; fv[3].y = 100.0f;
        fv[4].x = 200.0f; fv[5].x = 300.0f; fv[6].x = 300.0f; fv[6].y = 100.0f;
        fv[7].x = 200.0f; fv[7].y = 100.0f;
        fv[8].z = 1000.0f; fv[9].x = 2000.0f; fv[9].z = 1000.0f; fv[10].y = 2000.0f; fv[10].z = 1000.0f;
        fv[11].z = 5000.0f; fv[12].x = 600000.0f; fv[12].z = 5000.0f; fv[13].y = 600000.0f; fv[13].z = 5000.0f;
        fa.vertices = fv; fa.vertex_count = 14;
        fa.indices = fi; fa.index_count = 18;
        fa.material_count = 2;
        fa.materials[0].blend = SL_WA_BLEND_OPAQUE;
        fa.materials[1].blend = SL_WA_BLEND_ALPHA;
        fa.mesh_count = 4;
        fa.meshes[0].material = 0; fa.meshes[0].first_index = 0; fa.meshes[0].index_count = 6;
        fa.meshes[1].material = 1; fa.meshes[1].first_index = 6; fa.meshes[1].index_count = 6;
        fa.meshes[2].material = 0; fa.meshes[2].first_index = 12; fa.meshes[2].index_count = 3;
        fa.meshes[3].material = 0; fa.meshes[3].first_index = 15; fa.meshes[3].index_count = 3;
        ck(sl_wa_fill_set(&fa, 16.0f) == 1 && sl_wa_fill_set(&fa, 16.0f) == 0
           && sl_wa_fill_counts(&nl) == 3 && nl == 1,
           "the grid builds once per item and cell, two triangles and the wide one gridded (at "
           "a coarser level), the one wider than the coarsest level listed (%u %u)",
           sl_wa_fill_counts(NULL), nl);
        p[0] = 50.0f; p[1] = 50.0f; p[2] = 3.0f;
        d = sl_wa_fill_distance(p, 8.0f);
        ck(fabs(d - 3.0) < 1e-4, "a point three units over the square is three away (%g)", d);
        p[2] = 0.0f;
        ck(sl_wa_fill_distance(p, 8.0f) == 0.0f, "a point on the square is on it");
        p[0] = 103.0f; p[1] = 50.0f; p[2] = 4.0f;
        d = sl_wa_fill_distance(p, 8.0f);
        ck(fabs(d - 5.0) < 1e-4, "past the square's edge the distance runs to the edge (%g)", d);
        p[0] = 250.0f; p[1] = 50.0f; p[2] = 1.0f;
        ck(sl_wa_fill_distance(p, 8.0f) == 8.0f, "a blended surface is no surface: the limit comes back");
        p[0] = 50.0f; p[1] = 50.0f; p[2] = 30.0f;
        ck(sl_wa_fill_distance(p, 8.0f) == 8.0f, "nothing within the limit: the limit comes back");
        p[0] = 600.0f; p[1] = 500.0f; p[2] = 1004.0f;
        d = sl_wa_fill_distance(p, 8.0f);
        ck(fabs(d - 4.0) < 1e-4, "the wide triangle is found at its coarser level (%g)", d);
        p[0] = 300000.0f; p[1] = 200000.0f; p[2] = 5006.0f;
        d = sl_wa_fill_distance(p, 8.0f);
        ck(fabs(d - 6.0) < 1e-4, "the triangle wider than every level is found from the list (%g)", d);
        {
            /* #83 the aligned query: the square lies in z = 0, normal +-z */
            float nz[3] = { 0.0f, 0.0f, -1.0f }, nx[3] = { 1.0f, 0.0f, 0.0f };
            float n33[3] = { 0.0f, 0.5446f, 0.8387f };      /* tilted 33 degrees off it */
            p[0] = 50.0f; p[1] = 50.0f; p[2] = 3.0f;
            d = sl_wa_fill_distance_aligned(p, nz, 8.0f);
            ck(fabs(d - 3.0) < 1e-4, "a surface in the triangle's plane counts, either facing (%g)", d);
            ck(sl_wa_fill_distance_aligned(p, nx, 8.0f) == 8.0f
               && sl_wa_fill_distance_aligned(p, n33, 8.0f) == 8.0f,
               "a surface at right angles, or a step under a 33-degree ramp, does not");
        }
        p[0] = 1990.0f; p[1] = 5.0f; p[2] = 1002.0f;
        d = sl_wa_fill_distance(p, 8.0f);
        ck(fabs(d - 2.0) < 1e-4, "the wide triangle is met at its far corner's cell too (%g)", d);
        p[0] = -1.0f; p[1] = -1.0f; p[2] = 0.0f;
        d = sl_wa_fill_distance(p, 8.0f);
        ck(fabs(d - sqrt(2.0)) < 1e-4, "outside a corner the distance runs to the corner (%g)", d);
        /* #81 the open test: a ray from the eye through a hole point */
        {
            const float e1[3] = { 50.0f, 50.0f, -500.0f }, h1[3] = { 50.0f, 50.0f, -20.0f };
            const float e2[3] = { 150.0f, 50.0f, -500.0f }, h2[3] = { 150.0f, 50.0f, -20.0f };
            const float e3[3] = { 250.0f, 50.0f, -500.0f }, h3[3] = { 250.0f, 50.0f, -20.0f };
            (void) sl_wa_fill_set(&fa, 4.0f * SL_WA_FILL_OPEN_STEP);   /* cells >= 2 x the trace's limit */
            ck(!sl_wa_fill_open(e1, h1, 1.0f),
               "a hole the square stands 20 units behind is the replacement's own opening");
            ck(sl_wa_fill_open(e2, h2, 1.0f),
               "a hole whose first surface behind is 1020 units away is open");
            ck(sl_wa_fill_open(e3, h3, 1.0f) == sl_wa_fill_open(e2, h2, 1.0f),
               "a blended surface behind the hole does not back it");
        }
        ck(sl_wa_fill_set(NULL, 0.0f) == 1 && sl_wa_fill_counts(NULL) == 0
           && sl_wa_fill_distance(p, 8.0f) == 8.0f,
           "released, the grid answers the limit");
    }
    /* THE BOX TEST CHANGES NO ANSWER: a soup of 4000 triangles of every size
     * at Statue's magnitudes (coordinates about 1.5e6, the grid's cell 17067,
     * the trace's limit 4267 and the registration's 1067), each query asked
     * with the boxes four at a time, one at a time and without them, plain
     * and aligned - every answer bit-identical, and enough of them nearer
     * than the limit to mean it; then open-hole traces through the soup,
     * four at a time against one at a time. */
    {
        static struct sl_wa_asset sa;
        static struct sl_wa_vertex sv[12000];
        static unsigned si[12000];
        unsigned seed = 12345u, q, same = 0, asked = 0, found = 0, j, osame = 0, oasked = 0, oshut = 0;
        float lims[2] = { 4267.0f, 1067.0f };
#define SOUP_RND() (seed = seed * 1664525u + 1013904223u, (float) (seed >> 8) / 16777216.0f)
        memset(&sa, 0, sizeof sa);
        for (j = 0; j < 4000u; j++) {
            const float size = (j % 10u == 0u) ? 60000.0f : (j % 3u == 0u) ? 6000.0f : 600.0f;
            const float cx = 1.5e6f + 200000.0f * SOUP_RND(), cy = -20000.0f + 40000.0f * SOUP_RND(),
                        cz = -1.2e6f + 200000.0f * SOUP_RND();
            unsigned k;
            for (k = 0; k < 3u; k++) {
                sv[j * 3u + k].x = cx + size * (SOUP_RND() - 0.5f);
                sv[j * 3u + k].y = cy + size * (SOUP_RND() - 0.5f) * ((j % 2u) ? 0.05f : 1.0f);
                sv[j * 3u + k].z = cz + size * (SOUP_RND() - 0.5f);
                si[j * 3u + k] = j * 3u + k;
            }
        }
        sa.vertices = sv; sa.vertex_count = 12000u;
        sa.indices = si; sa.index_count = 12000u;
        sa.material_count = 1; sa.materials[0].blend = SL_WA_BLEND_OPAQUE;
        sa.mesh_count = 1; sa.meshes[0].material = 0; sa.meshes[0].first_index = 0;
        sa.meshes[0].index_count = 12000u;
        (void) sl_wa_fill_set(&sa, 17067.0f);
        for (q = 0; q < 3000u; q++) {
            float pt[3], nn[3], d1, d2, d3, d4, d5, d6, len;
            const float lim = lims[q % 2u];
            const unsigned near_tri = (q * 7u) % 4000u;
            /* half the points beside a triangle, half anywhere in the soup */
            if (q % 2u) {
                pt[0] = sv[near_tri * 3u].x + 3000.0f * (SOUP_RND() - 0.5f);
                pt[1] = sv[near_tri * 3u].y + 3000.0f * (SOUP_RND() - 0.5f);
                pt[2] = sv[near_tri * 3u].z + 3000.0f * (SOUP_RND() - 0.5f);
            } else {
                pt[0] = 1.5e6f + 200000.0f * SOUP_RND();
                pt[1] = -20000.0f + 40000.0f * SOUP_RND();
                pt[2] = -1.2e6f + 200000.0f * SOUP_RND();
            }
            nn[0] = SOUP_RND() - 0.5f; nn[1] = SOUP_RND() - 0.5f; nn[2] = SOUP_RND() - 0.5f;
            len = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            if (len > 0.0f) { nn[0] /= len; nn[1] /= len; nn[2] /= len; } else nn[1] = 1.0f;
            g_fill_boxes = 1; g_fill_simd = 1;
            d1 = sl_wa_fill_distance(pt, lim);
            d3 = sl_wa_fill_distance_aligned(pt, nn, lim);
            g_fill_simd = 0;
            d5 = sl_wa_fill_distance(pt, lim);
            d6 = sl_wa_fill_distance_aligned(pt, nn, lim);
            g_fill_boxes = 0;
            d2 = sl_wa_fill_distance(pt, lim);
            d4 = sl_wa_fill_distance_aligned(pt, nn, lim);
            asked++;
            if (d2 < lim) found++;
            if (memcmp(&d1, &d2, sizeof d1) == 0 && memcmp(&d3, &d4, sizeof d3) == 0
                && memcmp(&d5, &d2, sizeof d5) == 0 && memcmp(&d6, &d4, sizeof d6) == 0) same++;
            else if (asked - same <= 3u)
                fprintf(stderr, "  box-test difference at query %u: %.9g / %.9g vs %.9g, aligned %.9g / %.9g vs %.9g\n",
                        q, d1, d5, d2, d3, d6, d4);
        }
        /* open-hole traces (the trace's limit 4267 is 32 world units at
         * Statue's scale 0.0075): from a point near a triangle, away from an
         * eye somewhere in the soup */
        for (q = 0; q < 600u; q++) {
            float eye[3], hq[3];
            int o1, o2;
            const unsigned near_tri = (q * 13u) % 4000u;
            hq[0] = sv[near_tri * 3u].x + 6000.0f * (SOUP_RND() - 0.5f);
            hq[1] = sv[near_tri * 3u].y + 6000.0f * (SOUP_RND() - 0.5f);
            hq[2] = sv[near_tri * 3u].z + 6000.0f * (SOUP_RND() - 0.5f);
            eye[0] = 1.5e6f + 200000.0f * SOUP_RND();
            eye[1] = -20000.0f + 40000.0f * SOUP_RND();
            eye[2] = -1.2e6f + 200000.0f * SOUP_RND();
            g_fill_boxes = 1; g_fill_simd = 1;
            o1 = sl_wa_fill_open(eye, hq, 0.0075f);
            g_fill_simd = 0;
            o2 = sl_wa_fill_open(eye, hq, 0.0075f);
            oasked++;
            if (!o2) oshut++;
            if (o1 == o2) osame++;
        }
#undef SOUP_RND
        g_fill_boxes = -1; g_fill_simd = -1;
        ck(same == asked && asked == 3000u && found >= 500u,
           "the box test changes no distance: %u of %u queries bit-identical four boxes at a time, one "
           "at a time and without boxes (%u nearer than the limit)", same, asked, found);
        ck(osame == oasked && oshut >= 50u && oshut + 50u <= oasked,
           "four boxes at a time change no open-hole decision: %u of %u traces agree (%u backed)",
           osame, oasked, oshut);
        (void) sl_wa_fill_set(NULL, 0.0f);
    }
    {
        const struct sl_wa_asset *got = sl_wa_lookup(SL_WA_CLASS_GUN, "testgun");
        (void) got;
        sl_presentation_set(SL_PRESENTATION_ORIGINAL);
        ck(sl_wa_lookup(SL_WA_CLASS_GUN, "testgun") == NULL,
           "PRESENTATION ORIGINAL never consumes a view model");
        sl_presentation_set(SL_PRESENTATION_COMMUNITY);
        ck(sl_wa_lookup(SL_WA_CLASS_GUN, "testgun") == NULL,
           "PRESENTATION COMMUNITY HD never consumes a view model");
        sl_presentation_set(SL_PRESENTATION_XBLA);
    }

    sl_wa_report(stderr);
    printf("%s: %d passed, %d failed\n", t_fail ? "WORLDASSET SELFTEST FAILED"
                                                : "worldasset selftest", t_pass, t_fail);
    return t_fail ? 1 : 0;
}

#endif /* SL_WA_SELFTEST */
