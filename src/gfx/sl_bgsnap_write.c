/* WRITING ONE BACKGROUND FRAME'S INPUTS. See sl_bgsnap_write.h for what this
 * is for and why it is off, and sl_bgsnap.h for the file layout.
 *
 * EVERY STORE IS AN EXPLICIT LITTLE-ENDIAN BYTE STORE. No struct is written to
 * the file, no padding is relied on, and no float is widened: a float goes out
 * as the 32 bits it already is, because the comparison this feeds is bit for
 * bit and a format that rounded anywhere would destroy the only property under
 * test.
 *
 * EVERY COUNT IS CHECKED BEFORE IT IS MULTIPLIED. SL_BGSNAP_COUNTS_OK bounds
 * the three counts, and only then is a count multiplied by a record size; the
 * format's maxima are chosen so a count that passes cannot produce a wrapping
 * product (asserted at compile time below). A count over its maximum is a
 * recorded failure, not a clamped count.
 */
#include "sl_bgsnap.h"
#include "sl_bgsnap_write.h"
#include "sl_gfx_worldasset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The layout constants and the field offsets must agree, and the maxima must
 * keep every product inside 32 bits. Both are the format's own invariants, so
 * a mismatch is a compile error rather than a bad file. */
typedef char sl_bgsnap_w_vertex_total[(SL_BGSNAP_V_ZRAW + 4u == SL_BGSNAP_VERTEX_BYTES) ? 1 : -1];
typedef char sl_bgsnap_w_draw_total[(SL_BGSNAP_D_RESERVED + 12u == SL_BGSNAP_DRAW_BYTES) ? 1 : -1];
typedef char sl_bgsnap_w_header_total[(SL_BGSNAP_H_INDEX_BYTES + 4u == SL_BGSNAP_HEADER_BYTES) ? 1 : -1];
typedef char sl_bgsnap_w_vprod[(SL_BGSNAP_MAX_VERTICES <= 0xFFFFFFFFu / SL_BGSNAP_VERTEX_BYTES) ? 1 : -1];
typedef char sl_bgsnap_w_dprod[(SL_BGSNAP_MAX_DRAWS <= 0xFFFFFFFFu / SL_BGSNAP_DRAW_BYTES) ? 1 : -1];
typedef char sl_bgsnap_w_iprod[(SL_BGSNAP_MAX_INDICES <= 0xFFFFFFFFu / SL_BGSNAP_INDEX_BYTES) ? 1 : -1];

/* ---- the cached gate ---------------------------------------------------
 *
 * THE ORDINARY RUN'S WHOLE COST. -1 until the environment has been read, then
 * 0 or 1 for the life of the process. The selector cannot change while the
 * game runs, so this is read once rather than per frame: the pass asks this
 * question once per frame and once per draw, and on an unselected run the
 * answer is a load and a branch with no call into anything below. */
static int   g_armed = -1;
static char  g_sel[128];

/* The selected moment, parsed from the selector. ~0u in a field means "any",
 * which only an explicit '*' produces. */
static unsigned g_want_read = ~0u, g_want_frame = ~0u, g_want_cloud = ~0u;
static char     g_want_path[64];

/* ---- THE PROVENANCE, SUPPLIED AND VALIDATED --------------------------
 *
 * A snapshot that does not say which binary and which shader text produced it
 * is not evidence: the comparison it feeds comes down to "these numbers match
 * those numbers", with nothing tying either side to a build. The header
 * therefore carries two digests, and a capture is REFUSED unless both are
 * present and well formed.
 *
 * THE DIGESTS ARE SUPPLIED, NOT COMPUTED HERE. Hashing the running executable
 * from inside the renderer would mean reading the whole image at a frame
 * boundary and carrying a hash implementation in the graphics path, for a
 * value the launcher already has: it has just built or located the binary and
 * the pinned shader text. So the launcher passes them in and this validates
 * them, which adds no dependency and no per-frame work.
 *
 * VALIDATION IS THE POINT, because a supplied value is only as good as the
 * check on it. Each must be exactly 64 lowercase hex characters and must not
 * be all zeros - an all-zero digest is what an unset or partially written
 * field looks like, and accepting it would let a capture claim a provenance
 * nobody established. Anything else disarms the diagnostic rather than
 * producing a file with a hole where its identity should be. */
static unsigned char g_exe_sha[32];
static unsigned char g_src_sha[32];

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;                       /* uppercase is refused too: one spelling */
}

/* 1 when `text` is exactly 64 lowercase hex digits and not all zeros, with the
 * 32 bytes written to `out`. */
static int parse_sha256(const char *text, unsigned char *out, const char *what)
{
    int i, nonzero = 0;
    if (text == NULL || *text == '\0') {
        fprintf(stderr, "sl_bgsnap: REFUSED - %s was not supplied. A capture "
                        "with no provenance cannot be tied to a build, so "
                        "nothing is recorded.\n", what);
        return 0;
    }
    if (strlen(text) != 64u) {
        fprintf(stderr, "sl_bgsnap: REFUSED - %s is %u characters, not the 64 "
                        "of a sha256\n", what, (unsigned) strlen(text));
        return 0;
    }
    for (i = 0; i < 32; i++) {
        int hi = hex_nibble(text[i * 2]), lo = hex_nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            fprintf(stderr, "sl_bgsnap: REFUSED - %s is not 64 lowercase hex "
                            "characters\n", what);
            return 0;
        }
        out[i] = (unsigned char) ((hi << 4) | lo);
        if (out[i] != 0u) nonzero = 1;
    }
    if (!nonzero) {
        fprintf(stderr, "sl_bgsnap: REFUSED - %s is all zeros, which is what an "
                        "unset field looks like; a capture may not claim a "
                        "provenance nobody established\n", what);
        memset(out, 0, 32u);
        return 0;
    }
    return 1;
}

/* THE GRAMMAR FAILS CLOSED, and that is the whole point of this function.
 *
 * A component is either a single '*', meaning any, or a bounded decimal. It is
 * NEVER anything else: unparsable text disarms the diagnostic instead of
 * selecting everything.
 *
 * An earlier form returned the wildcard for a component it could not parse,
 * which inverted the intent of every malformed selector. MEASURED on that
 * form: `garbage` armed the diagnostic and matched ANY moment, and
 * `140:garbage` matched any frame and any cloud phase. A selector that cannot
 * be read is a mistake by whoever wrote it, and the safe reading of a mistake
 * is to record nothing rather than to record the first frame that happens by.
 *
 * The overflow test covers the ADDITION as well as the multiplication. The
 * earlier `v > UINT_MAX / 10` guard excluded the final digit, so 4294967296
 * passed nine digits, added the tenth and wrapped to 0 - a selector naming a
 * moment that cannot exist silently became a selector naming frame zero.
 *
 * Returns 1 and writes *out (~0u for an explicit '*') on a well-formed
 * component, 0 on anything else. On success *s is advanced past the component
 * and past one ':' if present. */
#define SL_BGSNAP_MAX_DIGITS 10          /* 4294967295 is ten digits */

static int parse_part(const char **s, unsigned *out)
{
    const char *p = *s;
    unsigned v = 0u;
    int digits = 0;

    if (p == NULL || *p == '\0') return 0;      /* an absent component is not any */

    if (*p == '*') {
        p++;
        if (*p != '\0' && *p != ':') return 0;  /* '*7' is not a wildcard */
        *out = ~0u;
    } else {
        while (*p >= '0' && *p <= '9') {
            unsigned d = (unsigned) (*p - '0');
            if (++digits > SL_BGSNAP_MAX_DIGITS) return 0;
            /* The bound includes the digit about to be added, so no value can
             * wrap through the addition the way 4294967296 did. */
            if (v > (0xFFFFFFFFu - d) / 10u) return 0;
            v = v * 10u + d;
            p++;
        }
        if (digits == 0) return 0;              /* not a digit and not '*'    */
        if (*p != '\0' && *p != ':') return 0;  /* trailing junk, e.g. '140x' */
        if (v == ~0u) return 0;                 /* ~0u is the wildcard, not a
                                                 * selectable moment         */
        *out = v;
    }
    if (*p == ':') p++;
    *s = p;
    return 1;
}

int sl_bgsnap_armed(void)
{
    const char *v;
    if (g_armed >= 0) return g_armed;
    g_armed = 0;
    v = getenv("SL_BGSNAP");
    if (v == NULL || *v == '\0' || (v[0] == '0' && v[1] == '\0')) return 0;
    /* The selector is "read:frame:cloud[:path-substring]", each numeric part
     * optionally '*' for any. No level name and no geometry constant is
     * accepted: an item is named, if at all, by a substring of the path it
     * already carries. */
    /* THE LENGTH IS BOUNDED BEFORE THE TEXT IS COPIED. An over-long selector
     * is refused rather than silently truncated: a truncated selector names a
     * different moment from the one written, and selecting the wrong frame is
     * the failure this diagnostic exists to avoid. */
    if (strlen(v) >= sizeof g_sel) {
        fprintf(stderr, "sl_bgsnap: REFUSED - SL_BGSNAP is %u characters, over "
                        "the %u the selector allows. Nothing is recorded; a "
                        "truncated selector would name a different moment.\n",
                (unsigned) strlen(v), (unsigned) (sizeof g_sel - 1u));
        return 0;
    }
    snprintf(g_sel, sizeof g_sel, "%s", v);
    {
        const char *p = g_sel;
        if (!parse_part(&p, &g_want_read) ||
            !parse_part(&p, &g_want_frame) ||
            !parse_part(&p, &g_want_cloud)) {
            fprintf(stderr, "sl_bgsnap: REFUSED - SL_BGSNAP=%s is not "
                            "read:frame:cloud[:path], where each of the three is "
                            "a decimal below 4294967295 or '*' for any. Nothing "
                            "is recorded: an unreadable selector is a mistake, "
                            "and recording the first frame that happens by would "
                            "be the wrong answer to it.\n", g_sel);
            g_want_read = g_want_frame = g_want_cloud = 0u;
            return 0;
        }
        /* The remainder, if any, is the item-path substring. It is bounded the
         * same way, and an over-long one is refused rather than truncated. */
        if (*p != '\0') {
            if (strlen(p) >= sizeof g_want_path) {
                fprintf(stderr, "sl_bgsnap: REFUSED - the path component is %u "
                                "characters, over the %u allowed\n",
                        (unsigned) strlen(p), (unsigned) (sizeof g_want_path - 1u));
                g_want_read = g_want_frame = g_want_cloud = 0u;
                return 0;
            }
            snprintf(g_want_path, sizeof g_want_path, "%s", p);
        }
    }
    /* THE PROVENANCE IS REQUIRED TO ARM, not merely recorded if offered. A
     * selector without it would produce a file that reads as a measurement and
     * names no build, which is worse than producing nothing. */
    if (!parse_sha256(getenv("SL_BGSNAP_EXE_SHA"), g_exe_sha,
                      "SL_BGSNAP_EXE_SHA (the running executable's sha256)") ||
        !parse_sha256(getenv("SL_BGSNAP_SRC_SHA"), g_src_sha,
                      "SL_BGSNAP_SRC_SHA (the pinned shader text's sha256)")) {
        memset(g_exe_sha, 0, sizeof g_exe_sha);
        memset(g_src_sha, 0, sizeof g_src_sha);
        g_want_read = g_want_frame = g_want_cloud = 0u;
        g_want_path[0] = '\0';
        return 0;
    }
    g_armed = 1;
    fprintf(stderr, "sl_bgsnap: ARMED by SL_BGSNAP=%s - one background frame's "
                    "inputs will be recorded (read %u, frame %u, cloud %u, path '%s'; "
                    "~0 means any). This is a DIAGNOSTIC: the recording disables this "
                    "walk's vertex-loop reduction so the whole item is transformed, so "
                    "this run's timing is not a measurement.\n",
            g_sel, g_want_read, g_want_frame, g_want_cloud, g_want_path);
    return 1;
}

/* ---- collection state ------------------------------------------------- */

static int      g_open;          /* collection is open for this walk       */
static int      g_done;          /* a file has been written; write once    */
static unsigned g_verts;
static unsigned g_ndraws, g_nindices;
static unsigned char *g_vbuf;    /* g_verts * SL_BGSNAP_VERTEX_BYTES       */
static unsigned char *g_dbuf;    /* SL_BGSNAP_MAX_DRAWS * DRAW_BYTES       */
static unsigned char *g_ibuf;    /* SL_BGSNAP_MAX_INDICES * INDEX_BYTES    */
static unsigned g_icap;
static unsigned g_frame, g_read, g_cloud;
static char     g_recipe[SL_BGSNAP_RECIPE_BYTES];
static char     g_failure[SL_BGSNAP_FAILURE_BYTES];

/* What the pass established about the forwarded attributes.
 *
 * The COLOUR MODE is per walk, so it decides the colour state word directly.
 *
 * The FOG POOL flag is NOT a per-draw fog state and must not be used as one:
 * the array is toggled per material, so only the draw site knows what state a
 * given draw left it in. This records solely whether a pool of per-vertex fog
 * values was copied at all, which is what lets a draw claiming to READ that
 * pool be checked against its existence - a draw cannot reference a pool that
 * was never recorded, and a record asserting otherwise would point a reader at
 * zeros.
 *
 * -1 is "not established", which the mask reports as unrecorded so the reader
 * withholds the oracle rather than reading a value nobody set. */
static int g_fog_pool = -1;      /* 0 no pool copied, 1 a pool was copied    */
static int g_col_mode = -1;      /* 0 disabled, 1 the item's own, 2 replaced */

int sl_bgsnap_collecting(void) { return g_open; }

static void fail_once(const char *why)
{
    if (g_failure[0] != '\0') return;        /* the first reason is the cause */
    snprintf(g_failure, sizeof g_failure, "%s", why);
    fprintf(stderr, "sl_bgsnap: FAILED - %s. The snapshot will be written "
                    "without its complete marker, so a reader refuses it rather "
                    "than reading a partial record set as a smaller one.\n", why);
}

int sl_bgsnap_want(unsigned read, unsigned frame, unsigned cloud, const char *path)
{
    if (!sl_bgsnap_armed() || g_done || g_open) return 0;
    if (g_want_read != ~0u && read != g_want_read) return 0;
    if (g_want_frame != ~0u && frame != g_want_frame) return 0;
    if (g_want_cloud != ~0u && cloud != g_want_cloud) return 0;
    if (g_want_path[0] != '\0' &&
        (path == NULL || strstr(path, g_want_path) == NULL)) return 0;
    return 1;
}

/* ---- the little-endian stores ----------------------------------------- */

static void put_u32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char) (v & 0xFFu);
    p[1] = (unsigned char) ((v >> 8) & 0xFFu);
    p[2] = (unsigned char) ((v >> 16) & 0xFFu);
    p[3] = (unsigned char) ((v >> 24) & 0xFFu);
}

/* A float's own 32 bits, never its value. memcpy rather than a pointer cast:
 * the cast is undefined behaviour the compiler is entitled to exploit, and
 * this value is the one thing in the file that must survive exactly. */
static void put_f32(unsigned char *p, float f)
{
    unsigned u;
    memcpy(&u, &f, sizeof u);
    put_u32(p, u);
}

static void put_f32v(unsigned char *p, const float *f, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) put_f32(p + i * 4u, f[i]);
}

/* ---- begin ------------------------------------------------------------ */

int sl_bgsnap_begin(const struct sl_wa_asset *a, unsigned verts,
                    unsigned read, unsigned frame, unsigned cloud)
{
    if (!sl_bgsnap_armed() || g_done || g_open) return 0;
    if (a == NULL || verts == 0u) return 0;

    /* THE BOUND, BEFORE THE SIZE. The count is compared with the format's
     * maximum first; only a count that passes is multiplied by a record size,
     * and the maxima are chosen so such a product cannot wrap. */
    if (!SL_BGSNAP_COUNTS_OK(0u, verts, 0u)) {
        fail_once("vertex count over maximum");
        g_done = 1;                          /* do not retry on a later frame */
        return 0;
    }

    g_vbuf = (unsigned char *) calloc((size_t) verts, (size_t) SL_BGSNAP_VERTEX_BYTES);
    g_dbuf = (unsigned char *) calloc((size_t) SL_BGSNAP_MAX_DRAWS, (size_t) SL_BGSNAP_DRAW_BYTES);
    g_ibuf = (unsigned char *) calloc((size_t) SL_BGSNAP_MAX_INDICES, (size_t) SL_BGSNAP_INDEX_BYTES);
    if (g_vbuf == NULL || g_dbuf == NULL || g_ibuf == NULL) {
        free(g_vbuf); free(g_dbuf); free(g_ibuf);
        g_vbuf = g_dbuf = g_ibuf = NULL;
        fail_once("allocation failed");
        g_done = 1;
        return 0;
    }
    g_icap = SL_BGSNAP_MAX_INDICES;
    g_verts = verts;
    g_ndraws = g_nindices = 0u;
    g_read = read; g_frame = frame; g_cloud = cloud;
    /* Not established until this walk's own sl_bgsnap_effective runs. A value
     * left over from anything else would be a claim about the wrong walk. */
    g_fog_pool = -1;
    g_col_mode = -1;
    g_failure[0] = '\0';
    /* `path` is a fixed array in the asset, not a pointer, so it is always
     * present and a NULL test on it is always true. It is printed with an
     * explicit precision rather than %s because nothing here may assume the
     * array is NUL-terminated. */
    snprintf(g_recipe, sizeof g_recipe, "read=%u frame=%u cloud=%u item=%.*s",
             read, frame, cloud, (int) sizeof a->path, a->path);
    g_open = 1;
    return 1;
}

/* ---- the per-vertex inputs and the CPU reference ---------------------- */

void sl_bgsnap_vertices(const struct sl_wa_asset *a, unsigned verts,
                        const float *clip, const float *zraw)
{
    unsigned i;
    if (!g_open || g_vbuf == NULL) return;
    if (a == NULL || a->vertices == NULL) { fail_once("item has no vertices"); return; }
    if (verts != g_verts) { fail_once("vertex count changed"); return; }

    for (i = 0u; i < verts; i++) {
        unsigned char *r = g_vbuf + (size_t) i * SL_BGSNAP_VERTEX_BYTES;
        const struct sl_wa_vertex *v = &a->vertices[i];
        float pos[3], uv[2];
        pos[0] = v->x; pos[1] = v->y; pos[2] = v->z;
        uv[0] = v->u;  uv[1] = v->v;
        put_f32v(r + SL_BGSNAP_V_POS, pos, 3u);
        put_f32v(r + SL_BGSNAP_V_UV0, uv, 2u);
        /* THE DEPTHS, COPIED BEFORE ANYTHING REWRITES THEM. Later stages
         * rewrite clip z per range and per mesh; these are the values the
         * joined loop itself produced, which is what the format's two depth
         * fields are defined to hold. */
        if (clip != NULL) put_f32v(r + SL_BGSNAP_V_CLIP, &clip[i * 4u], 4u);
        if (zraw != NULL) put_f32(r + SL_BGSNAP_V_ZRAW, zraw[i]);
    }
    if (clip == NULL) fail_once("no clip array");
    if (zraw == NULL) fail_once("no raw depth array");
}

void sl_bgsnap_effective(const struct sl_wa_asset *a, unsigned verts,
                         const float *fogc, const unsigned char *colours,
                         int disabled)
{
    unsigned i;
    if (!g_open || g_vbuf == NULL) return;
    if (a == NULL || a->vertices == NULL) { fail_once("item has no vertices"); return; }
    if (verts != g_verts) { fail_once("vertex count changed"); return; }

    g_fog_pool = (fogc != NULL) ? 1 : 0;
    g_col_mode = disabled ? 0 : ((colours != NULL) ? 2 : 1);

    for (i = 0u; i < verts; i++) {
        unsigned char *r = g_vbuf + (size_t) i * SL_BGSNAP_VERTEX_BYTES;
        /* THE COLOUR BYTES ACTUALLY BOUND. When a replacement array is bound
         * these are its bytes, not the item's: the item's own records are not
         * what the draw reads, and recording them would assert a colour that
         * was never submitted. When the array is disabled no per-vertex
         * colour is bound at all, so the field is left alone and the state
         * mask marks it unrecorded rather than inventing white. */
        if (g_col_mode == 2) {
            r[SL_BGSNAP_V_COL + 0u] = colours[i * 4u + 0u];
            r[SL_BGSNAP_V_COL + 1u] = colours[i * 4u + 1u];
            r[SL_BGSNAP_V_COL + 2u] = colours[i * 4u + 2u];
            r[SL_BGSNAP_V_COL + 3u] = colours[i * 4u + 3u];
        } else if (g_col_mode == 1) {
            const struct sl_wa_vertex *v = &a->vertices[i];
            r[SL_BGSNAP_V_COL + 0u] = v->r;
            r[SL_BGSNAP_V_COL + 1u] = v->g;
            r[SL_BGSNAP_V_COL + 2u] = v->b;
            r[SL_BGSNAP_V_COL + 3u] = v->a;
        }
        /* The fog value as bound, which is what makes the forwarding identity
         * an oracle. Absent when the pass bound no fog array. */
        if (fogc != NULL) put_f32(r + SL_BGSNAP_V_FOG, fogc[i]);
    }
}

/* ---- one draw --------------------------------------------------------- */

void sl_bgsnap_draw(unsigned mesh, unsigned flags, int dmode, float fk1, float fk2,
                    const unsigned *indices, unsigned index_count,
                    const float *C, const float *org, float sc, float post,
                    const float *texmat0, const float *texmat1,
                    const unsigned *state, unsigned state_set)
{
    unsigned char *d;
    unsigned i;

    if (!g_open || g_dbuf == NULL || g_ibuf == NULL) return;
    if (g_ndraws >= SL_BGSNAP_MAX_DRAWS) { fail_once("draw table full"); return; }
    /* The index range is bounded before it is copied, and a draw that will not
     * fit is a recorded failure rather than a dropped draw: a snapshot missing
     * a draw would read as a frame that never issued it. */
    if (index_count > g_icap - g_nindices) { fail_once("index table full"); return; }
    if (indices == NULL && index_count != 0u) { fail_once("draw has no indices"); return; }
    for (i = 0u; i < index_count; i++) {
        if (indices[i] >= g_verts) { fail_once("index past vertex count"); return; }
        put_u32(g_ibuf + (size_t) (g_nindices + i) * SL_BGSNAP_INDEX_BYTES, indices[i]);
    }

    d = g_dbuf + (size_t) g_ndraws * SL_BGSNAP_DRAW_BYTES;
    put_u32(d + SL_BGSNAP_D_FLAGS, flags);
    put_u32(d + SL_BGSNAP_D_MESH, mesh);
    put_u32(d + SL_BGSNAP_D_DMODE, (unsigned) dmode);
    put_f32(d + SL_BGSNAP_D_FK1, fk1);
    put_f32(d + SL_BGSNAP_D_FK2, fk2);
    put_u32(d + SL_BGSNAP_D_FIRST_INDEX, g_nindices);
    put_u32(d + SL_BGSNAP_D_INDEX_COUNT, index_count);
    put_f32(d + SL_BGSNAP_D_SC, sc);
    put_f32(d + SL_BGSNAP_D_POST, post);
    if (org != NULL) put_f32v(d + SL_BGSNAP_D_ORG, org, 3u);
    if (C != NULL) put_f32v(d + SL_BGSNAP_D_C, C, 16u);
    else fail_once("draw has no matrix");
    if (texmat0 != NULL) put_f32v(d + SL_BGSNAP_D_TEXMAT0, texmat0, 16u);
    if (texmat1 != NULL) put_f32v(d + SL_BGSNAP_D_TEXMAT1, texmat1, 16u);
    if (state != NULL) for (i = 0u; i < 16u; i++)
        put_u32(d + SL_BGSNAP_D_STATE + i * 4u, state[i]);
    /* WHICH SIDE OWNS WHICH WORD, and why they differ.
     *
     * THE COLOUR ARRAY IS PER WALK, so the writer fills it from what
     * sl_bgsnap_effective observed actually bound. The draw site does not
     * restate it, because restating a walk-level fact at a per-draw site is
     * how a record previously claimed a state the pass did not have.
     *
     * THE FOG ARRAY IS PER DRAW, so the writer must NOT fill it. The pass
     * toggles the fog-coordinate array per material from the shared pool
     * (sl_gfx_dl.c:19092-19099), so one walk issues fogged and unfogged draws
     * and a walk-level answer is false for at least one of them. Stamping the
     * walk's value into every draw is exactly that defect: a material without
     * the stage-fog flag received a claimed array enable and a pool reference
     * it never read. The draw site passes the state it actually left the array
     * in, and this function records that unchanged - the three values are
     * defined at SL_BGSNAP_S_FOG_ARRAY.
     *
     * A word neither side establishes stays unrecorded, and the reader
     * withholds the oracle that depended on it. */
    {
        unsigned set = state_set;
        set &= ~(1u << SL_BGSNAP_S_COLOR_ARRAY);
        if (g_col_mode >= 0) {
            put_u32(d + SL_BGSNAP_D_STATE + SL_BGSNAP_S_COLOR_ARRAY * 4u,
                    (g_col_mode == 0) ? 0u : 1u);
            set |= 1u << SL_BGSNAP_S_COLOR_ARRAY;
        }
        put_u32(d + SL_BGSNAP_D_STATE_SET, set);
        /* A DRAW CANNOT READ A POOL THAT WAS NEVER RECORDED. The fog state is
         * the draw site's to report, and this does not override it - but a
         * draw claiming to read the per-vertex pool while no pool was copied
         * would point a reader at zeros and call them measurements, so the
         * contradiction is recorded as a failure of the whole snapshot rather
         * than silently carried in one record. */
        if ((set & (1u << SL_BGSNAP_S_FOG_ARRAY)) != 0u &&
            state != NULL && state[SL_BGSNAP_S_FOG_ARRAY] == SL_BGSNAP_FOG_ARRAY_ON &&
            g_fog_pool == 0)
            /* Short by necessity: the header's failure field is
             * SL_BGSNAP_FAILURE_BYTES, and a longer reason would be truncated
             * into something a reader cannot act on. */
            fail_once("fog pool not copied");
    }

    g_nindices += index_count;
    g_ndraws++;
}

/* ---- finish and write ------------------------------------------------- */

static const char *snap_dir(void)
{
    const char *d = getenv("SL_BGSNAP_DIR");
    /* The default is the ignored diagnostic directory beside the other
     * ROM-derived trees, so the output inherits their boundary by location as
     * well as by rule. */
    return (d != NULL && *d != '\0') ? d : "bgsnapshots";
}

void sl_bgsnap_end(void)
{
    unsigned char hdr[SL_BGSNAP_HEADER_BYTES];
    char path[512];
    unsigned draw_off, vertex_off, index_off, file_bytes;
    FILE *f;
    int wrote_all = 1;

    if (!g_open) return;
    g_open = 0;
    g_done = 1;                              /* write once, whatever happened */

    if (g_vbuf == NULL || g_dbuf == NULL || g_ibuf == NULL) goto release;

    if (!SL_BGSNAP_COUNTS_OK(g_ndraws, g_verts, g_nindices)) {
        fail_once("counts over maximum");
    }

    draw_off = SL_BGSNAP_HEADER_BYTES;
    vertex_off = draw_off + g_ndraws * SL_BGSNAP_DRAW_BYTES;
    index_off = vertex_off + g_verts * SL_BGSNAP_VERTEX_BYTES;
    file_bytes = index_off + g_nindices * SL_BGSNAP_INDEX_BYTES;

    snprintf(path, sizeof path, "%s/bgsnap-%u-%u-%u.bin",
             snap_dir(), g_read, g_frame, g_cloud);

    /* CREATE ONCE, NEVER REPLACE. "rb" first because the portable write modes
     * all truncate: a snapshot already on disk is evidence, and a later run
     * must not overwrite it silently. */
    f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
        fprintf(stderr, "sl_bgsnap: %s already exists and was NOT replaced - an "
                        "existing snapshot is evidence. Move it aside or point "
                        "SL_BGSNAP_DIR elsewhere.\n", path);
        goto release;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "sl_bgsnap: could not create %s - the directory must "
                        "exist and be writable\n", path);
        goto release;
    }

    memset(hdr, 0, sizeof hdr);
    put_u32(hdr + SL_BGSNAP_H_MAGIC, SL_BGSNAP_MAGIC);
    put_u32(hdr + SL_BGSNAP_H_VERSION, SL_BGSNAP_VERSION);
    put_u32(hdr + SL_BGSNAP_H_HEADER_BYTES, SL_BGSNAP_HEADER_BYTES);
    put_u32(hdr + SL_BGSNAP_H_FLAGS, (g_failure[0] != '\0') ? SL_BGSNAP_F_FAILED : 0u);
    put_u32(hdr + SL_BGSNAP_H_DRAWS, g_ndraws);
    put_u32(hdr + SL_BGSNAP_H_VERTICES, g_verts);
    put_u32(hdr + SL_BGSNAP_H_INDICES, g_nindices);
    put_u32(hdr + SL_BGSNAP_H_DRAW_OFF, draw_off);
    put_u32(hdr + SL_BGSNAP_H_VERTEX_OFF, vertex_off);
    put_u32(hdr + SL_BGSNAP_H_INDEX_OFF, index_off);
    put_u32(hdr + SL_BGSNAP_H_FILE_BYTES, file_bytes);
    put_u32(hdr + SL_BGSNAP_H_FRAME, g_frame);
    put_u32(hdr + SL_BGSNAP_H_READ, g_read);
    put_u32(hdr + SL_BGSNAP_H_CLOUD, g_cloud);
    put_u32(hdr + SL_BGSNAP_H_DRAW_BYTES, SL_BGSNAP_DRAW_BYTES);
    put_u32(hdr + SL_BGSNAP_H_VERTEX_BYTES, SL_BGSNAP_VERTEX_BYTES);
    put_u32(hdr + SL_BGSNAP_H_INDEX_BYTES, SL_BGSNAP_INDEX_BYTES);
    /* The validated provenance, as the 32 raw bytes each digest decodes to.
     * Both were proven present, well formed and non-zero before the
     * diagnostic armed, so neither field can be a hole here. */
    memcpy(hdr + SL_BGSNAP_H_EXE_SHA, g_exe_sha, sizeof g_exe_sha);
    memcpy(hdr + SL_BGSNAP_H_SRC_SHA, g_src_sha, sizeof g_src_sha);
    memcpy(hdr + SL_BGSNAP_H_RECIPE, g_recipe, sizeof g_recipe);
    memcpy(hdr + SL_BGSNAP_H_FAILURE, g_failure, sizeof g_failure);

    if (fwrite(hdr, 1u, sizeof hdr, f) != sizeof hdr) wrote_all = 0;
    if (wrote_all && g_ndraws != 0u &&
        fwrite(g_dbuf, 1u, (size_t) g_ndraws * SL_BGSNAP_DRAW_BYTES, f)
            != (size_t) g_ndraws * SL_BGSNAP_DRAW_BYTES) wrote_all = 0;
    if (wrote_all &&
        fwrite(g_vbuf, 1u, (size_t) g_verts * SL_BGSNAP_VERTEX_BYTES, f)
            != (size_t) g_verts * SL_BGSNAP_VERTEX_BYTES) wrote_all = 0;
    if (wrote_all && g_nindices != 0u &&
        fwrite(g_ibuf, 1u, (size_t) g_nindices * SL_BGSNAP_INDEX_BYTES, f)
            != (size_t) g_nindices * SL_BGSNAP_INDEX_BYTES) wrote_all = 0;

    /* THE COMPLETE FLAG IS WRITTEN LAST, and only when every payload byte went
     * out and no failure was recorded. A reader requires it, so an interrupted
     * or short write leaves a file that is refused rather than read as a
     * smaller one. */
    if (wrote_all && g_failure[0] == '\0' && fflush(f) == 0) {
        unsigned char fl[4];
        put_u32(fl, SL_BGSNAP_F_COMPLETE);
        if (fseek(f, (long) SL_BGSNAP_H_FLAGS, SEEK_SET) == 0 &&
            fwrite(fl, 1u, 4u, f) == 4u && fflush(f) == 0) {
            fprintf(stderr, "sl_bgsnap: wrote %s - %u draws, %u vertices, "
                            "%u indices, %u bytes, COMPLETE\n",
                    path, g_ndraws, g_verts, g_nindices, file_bytes);
        } else {
            fprintf(stderr, "sl_bgsnap: %s is written but its complete marker "
                            "could not be set; a reader will refuse it\n", path);
        }
    } else {
        fprintf(stderr, "sl_bgsnap: %s is INCOMPLETE (%s); a reader will refuse "
                        "it\n", path,
                (g_failure[0] != '\0') ? g_failure : "a write was short");
    }
    fclose(f);

release:
    free(g_vbuf); free(g_dbuf); free(g_ibuf);
    g_vbuf = g_dbuf = g_ibuf = NULL;
    g_verts = g_ndraws = g_nindices = g_icap = 0u;
    g_fog_pool = -1;
    g_col_mode = -1;
}
