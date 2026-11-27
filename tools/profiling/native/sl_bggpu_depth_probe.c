/* THE BACKGROUND PASS'S GPU DEPTH, ON A DRIVER - the case table, the CPU
 * reference and the probe's own unit checks. See sl_bggpu_depth_probe.h for
 * what gap this closes, how the production shader text is bound and pinned, and
 * why there are two builds.
 *
 * NO ROM DATA AND NO ASSET is read here. Every input below is SYNTHETIC:
 * constructed matrices, model positions placed at the GLSL's branch
 * boundaries, and depth coefficients taken from sl_wa_far_plane and
 * sl_wa_far_range at the real SL_WA_FAR_OVERLAP / SL_WA_FAR_OVERLAP_BLEND.
 * Synthetic probe data is not ROM data. No coefficient is hand-written.
 *
 * WHICH SOURCE LINES THE CPU REFERENCE MIRRORS. All in src/gfx/sl_gfx_dl.c as
 * of the pinned source (sl_bggpu_depth_probe.pin records its sha256):
 *
 *   17784-17786  the room space, in the pass's own order:
 *                  x = (px * sc - org[0]) * post;   and y, z likewise
 *   17843-17844  the clip product, the pass's own row order and association:
 *                  for (j = 0; j < 4; j++)
 *                      c[j] = x*C[j] + y*C[4+j] + z*C[8+j] + C[12+j];
 *                C's left-to-right association is the GLSL's explicit
 *                parenthesisation character for character.
 *   17847        the unclamped z the pass keeps:      zraw[i] = c[2];
 *   17849        the SINGLE far clamp, flags and all:
 *                  if (!fogless && c[3] > 0.0f && c[2] > c[3]) { c[2] = c[3]; nfar++; }
 *   18844        FAR0's blanket rewrite, the opaque pair:
 *                  clip[i*4+2] = fz_k1 * clip[i*4+3] - fz_k2;
 *   17179        FAR0 for a blended mesh, bg_far_rezero, the blend pair:
 *                  clip[vi*4+2] = k1 * clip[vi*4+3] - k2;
 *   18857        FAR1, the game's range:              clip[i*4+2] = zraw[i];
 *   18502-18507  where the coefficients come from:
 *                  sl_wa_far_plane(C, &zf)
 *                  sl_wa_far_range(zf * SL_WA_FAR_OVERLAP,       wmax*1.001f, &fz_k1, &fz_k2)
 *                  sl_wa_far_range(zf * SL_WA_FAR_OVERLAP_BLEND, wmax*1.001f, &fz_t1, &fz_t2)
 *
 * The reference calls the real sl_wa_far_plane / sl_wa_far_range from
 * src/gfx/sl_gfx_worldasset.c, linked in; it does not restate their
 * arithmetic.
 *
 * Usage:
 *   bggpudepthprobe.exe            the table, the reference and the unit checks
 *   bggpudepthprobe-gl.exe         the same, then a line naming the driver run
 *   bggpudepthprobe-gl.exe --gl    ... and the driver run itself
 * Exits 0 when every non-advisory check passes, 1 otherwise.
 */
#include "sl_gfx_bggpu_testgl.h"       /* the synthetic GL; see the note below */

#include "sl_bggpu_depth_probe.h"
#include "sl_bggpu_depth_probe_glsl.h" /* GENERATED: the pinned shader text    */

#include "sl_gfx_bggpu.h"
#include "sl_gfx_worldasset.h"
#include "sl_perf.h"
#include "sl_settings.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The three mode numbers this probe drives are the module's own, not copies.
 * A renumbering in the header breaks the build here rather than silently
 * measuring the wrong branch. */
#if SL_BGGPU_DEPTH_SINGLE != 0 || SL_BGGPU_DEPTH_FAR0 != 1 || SL_BGGPU_DEPTH_FAR1 != 2
#error "SL_BGGPU_DEPTH_* changed; the probe's case table and the GLSL's u_dmode disagree"
#endif

/* ---- the pinned text, behind accessors ---------------------------------- */

/* The generated header is included in this translation unit only, so the GL
 * driver gets the same bytes through these rather than a second definition. */
const char        *sl_bgdp_glsl(void)           { return SL_BGDP_GLSL; }
unsigned           sl_bgdp_glsl_bytes(void)     { return (unsigned) SL_BGDP_GLSL_BYTES; }
const char        *sl_bgdp_glsl_sha256(void)    { return SL_BGDP_GLSL_SHA256; }
const char        *sl_bgdp_uniforms_sha256(void){ return SL_BGDP_UNIFORMS_SHA256; }
const char        *sl_bgdp_source_sha256(void)  { return SL_BGDP_SOURCE_SHA256; }
int                sl_bgdp_uniform_count(void)  { return SL_BGDP_UNIFORM_COUNT; }
const char *const *sl_bgdp_uniform_names(void)  { return SL_BGDP_UNIFORMS; }

/* ---- the check ledger --------------------------------------------------- */

/* SL_BGDP_NO_MAIN compiles this file as a LIBRARY: the case table, the CPU
 * reference and the pinned-text accessors, without its own entry point or its
 * own self-checks. The background vertex-output probe
 * (tools/profiling/native/sl_bggpu_out_probe.c) builds on exactly those three
 * and brings its own main, and two mains cannot link. The macro is undefined
 * in the depth probe's own build, so nothing below changes for it - this is
 * additive, and the depth binaries are not rebuilt by the extension. */
/* The counters themselves stay: the case-table builder increments `fails` when
 * the table overflows, and that is a real failure a probe reusing the table
 * must be able to see. sl_bgdp_builder_ledger below hands both to a caller that
 * does not compile this file's own main. */
static int checks, fails;

void sl_bgdp_builder_ledger(int *out_checks, int *out_fails)
{
    if (out_checks != NULL) *out_checks = checks;
    if (out_fails != NULL) *out_fails = fails;
}

#ifndef SL_BGDP_NO_MAIN

#define CHECK(cond, ...) do {                                                 \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            fails++;                                                          \
            printf("    FAIL line %d: ", __LINE__);                           \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#endif /* !SL_BGDP_NO_MAIN */

/* The provider's player-data ladder has no module in an offline build.
 * src/gfx/sl_gfx_worldasset.c declares it; it is linked in for
 * sl_wa_far_plane and sl_wa_far_range only, and nothing this probe calls
 * reaches a file. */
const char *sl_asset_override_dir(void) { return NULL; }

/* ---- THE CPU REFERENCE ------------------------------------------------- */

/* src/gfx/sl_gfx_dl.c:17784-17786. */
static void room_space(const struct sl_bgdp_case *k, float *x, float *y, float *z)
{
    *x = (k->px * k->sc - k->org[0]) * k->post;
    *y = (k->py * k->sc - k->org[1]) * k->post;
    *z = (k->pz * k->sc - k->org[2]) * k->post;
}

/* src/gfx/sl_gfx_dl.c:17843-17844. */
static void clip_product(const float *C, float x, float y, float z, float *c)
{
    int j;
    for (j = 0; j < 4; j++)
        c[j] = x * C[j] + y * C[4 + j] + z * C[8 + j] + C[12 + j];
}

void sl_bgdp_reference(const struct sl_bgdp_case *k, struct sl_bgdp_ref *r)
{
    float x, y, z;
    room_space(k, &x, &y, &z);
    clip_product(k->C, x, y, z, r->c);
    r->zraw = r->c[2];                                  /* :17847            */
    r->clamped = 0;
    switch (k->dmode) {
    case SL_BGGPU_DEPTH_FAR0:
        /* :18844 for the opaque pair, :17179 (bg_far_rezero) for the blend
         * pair. Both are the same expression on the same w; which pair arrives
         * is the mesh's, which is why the case carries it. */
        r->z = k->fk1 * r->c[3] - k->fk2;
        break;
    case SL_BGGPU_DEPTH_FAR1:
        r->z = r->zraw;                                 /* :18857            */
        break;
    default:
        /* :17849, the flags and the comparisons in the source's order. */
        r->z = r->c[2];
        if (!k->fogless && r->c[3] > 0.0f && r->c[2] > r->c[3]) {
            r->z = r->c[3];
            r->clamped = 1;
        }
        break;
    }
}

int sl_bgdp_equal(float measured, float expected)
{
    /* The 32-bit pattern, not the value: no tolerance, and a NaN on either
     * side is compared as bits rather than compared equal by accident. */
    return memcmp(&measured, &expected, sizeof measured) == 0;
}

/* ---- the probe's two synthetic matrices --------------------------------- */

/* THE CAMERA MATRIX. Built the way the background pass's is, and the way
 * src/gfx/sl_gfx_worldasset.c's own far-plane check builds one: an affine view
 * (a rotation about y, a scale, a translation) times a perspective with near 10
 * and far 15000. SYNTHETIC, but the SHAPE is what matters - sl_wa_far_plane has
 * to recover a far plane from it, and part of the swept item has to lie past
 * that plane, which is the two-range case. Same construction as
 * tools/profiling/native/test_sl_gfx_bggpu.c's build_clip, so the far plane the
 * existing fixture already exercises is the one measured here. */
static float C_cam[16];

static void build_camera(float *C)
{
    const double n = 10.0, f = 15000.0, cy = 0.8, sy = 0.6, s = 0.2;
    const double pk = -(f + n) / (f - n), pm = -2.0 * f * n / (f - n);
    double V[16], P[16];
    int r, c, q;
    for (r = 0; r < 16; r++) { V[r] = 0.0; P[r] = 0.0; }
    V[0] = cy * s;  V[2] = -sy * s;  V[5] = s;  V[8] = sy * s;  V[10] = cy * s;
    V[12] = 120.0;  V[13] = -40.0;   V[14] = -3000.0;  V[15] = 1.0;
    P[0] = 1.2;  P[5] = 1.6;  P[10] = pk;  P[11] = -1.0;  P[14] = pm;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            double acc = 0.0;
            for (q = 0; q < 4; q++) acc += V[r * 4 + q] * P[q * 4 + c];
            C[r * 4 + c] = (float) acc;
        }
}

/* THE DIRECT MATRIX, and why a second one is needed. The GLSL's branches turn
 * on comparisons between c.z and c.w and on w landing exactly at a range end.
 * A camera matrix cannot place those exactly: the product is three multiplies
 * and three adds away from the input, so "z one ulp above w" is not something
 * a model position can be solved for in float. This matrix makes the product
 * the identity on the components that matter - with sc = 1, post = 1 and
 * org = 0 it gives c = (px, py, pz, py) exactly, every term being a multiply by
 * 1.0 or 0.0 and an add of 0.0 - so a case can put w and z on either side of a
 * boundary, or exactly on it, to the ulp.
 *
 * It is NOT a projection, and sl_wa_far_plane says so (a unit check below
 * proves it refuses this matrix). The depth coefficients the FAR0 cases use are
 * therefore still the camera matrix's, derived by the real helpers; this matrix
 * only delivers an exactly chosen w to the expression. */
static float C_dir[16];

static void build_direct(float *C)
{
    int i;
    for (i = 0; i < 16; i++) C[i] = 0.0f;
    C[0]  = 1.0f;   /* c.x = x */
    C[5]  = 1.0f;   /* c.y = y */
    C[10] = 1.0f;   /* c.z = z */
    C[7]  = 1.0f;   /* c.w = y */
}

/* The camera cases' walk scale and origin, the existing fixture's: a level
 * scale and a room origin, so the room-space step is exercised rather than
 * being the identity on every case. */
static const float CAM_SC = 2.0f, CAM_POST = 1.0f;
static const float CAM_ORG[3] = { 10.0f, -5.0f, 20.0f };

/* One swept model position: a ray that starts inside the far plane and runs
 * well past it (test_sl_gfx_bggpu.c's sweep_point). */
#define SWEEP_N 512
static void sweep_point(int i, float *px, float *py, float *pz)
{
    const float t = (float) i / (float) (SWEEP_N - 1);
    *px = 400.0f * t - 200.0f;
    *py = 150.0f * t;
    *pz = 5000.0f - 125000.0f * t;
}

/* ---- the derived coefficients ------------------------------------------- */

/* Exactly what src/gfx/sl_gfx_dl.c:18502-18507 derives, from the real helpers
 * at the real overlap constants. */
static float g_zf;                      /* the camera's far plane            */
static float g_wmax;                    /* the sweep's own w reduction       */
static float g_to;                      /* wmax * 1.001f, the ranges' end    */
static float g_from_k, g_from_t;        /* the opaque and blend range starts */
static float g_k1, g_k2;                /* the opaque pair                   */
static float g_t1, g_t2;                /* the blend pair (bg_far_rezero)    */
static int   g_have_zf;

static void derive_coefficients(void)
{
    int i;
    /* wmax as the pass reduces it: the largest c[3] over the item, from 0.0f
     * (src/gfx/sl_gfx_dl.c:17761 and :17848). */
    g_wmax = 0.0f;
    for (i = 0; i < SWEEP_N; i++) {
        struct sl_bgdp_case k;
        struct sl_bgdp_ref r;
        memset(&k, 0, sizeof k);
        k.C = C_cam; k.sc = CAM_SC; k.post = CAM_POST;
        k.org[0] = CAM_ORG[0]; k.org[1] = CAM_ORG[1]; k.org[2] = CAM_ORG[2];
        k.dmode = SL_BGGPU_DEPTH_FAR1;
        sweep_point(i, &k.px, &k.py, &k.pz);
        sl_bgdp_reference(&k, &r);
        if (r.c[3] > g_wmax) g_wmax = r.c[3];
    }
    g_have_zf = sl_wa_far_plane(C_cam, &g_zf);
    g_to = g_wmax * 1.001f;
    g_from_k = g_zf * SL_WA_FAR_OVERLAP;
    g_from_t = g_zf * SL_WA_FAR_OVERLAP_BLEND;
    sl_wa_far_range(g_from_k, g_to, &g_k1, &g_k2);
    sl_wa_far_range(g_from_t, g_to, &g_t1, &g_t2);
}

/* ---- the case table ----------------------------------------------------- */

static struct sl_bgdp_case g_cases[SL_BGDP_MAX_CASES];
static int g_ncases;

static struct sl_bgdp_case *add(const char *name, const char *probes,
                                const float *C, const char *matrix,
                                float px, float py, float pz,
                                int dmode, int fogless, float fk1, float fk2)
{
    struct sl_bgdp_case *k;
    if (g_ncases >= SL_BGDP_MAX_CASES) {
        printf("    FAIL: the case table is full (%d)\n", SL_BGDP_MAX_CASES);
        fails++;
        return &g_cases[0];
    }
    k = &g_cases[g_ncases++];
    memset(k, 0, sizeof *k);
    k->name = name; k->probes = probes; k->C = C; k->matrix = matrix;
    k->px = px; k->py = py; k->pz = pz;
    k->dmode = dmode; k->fogless = fogless; k->fk1 = fk1; k->fk2 = fk2;
    if (C == C_dir) {
        k->sc = 1.0f; k->post = 1.0f;        /* the exact-placement identity */
    } else {
        k->sc = CAM_SC; k->post = CAM_POST;
        k->org[0] = CAM_ORG[0]; k->org[1] = CAM_ORG[1]; k->org[2] = CAM_ORG[2];
    }
    return k;
}

/* Does the SINGLE clamp fire for this camera sweep index? Used to find the two
 * sweep vertices either side of the clamp's own boundary rather than guessing
 * indices. */
static int sweep_clamps(int i)
{
    struct sl_bgdp_case k;
    struct sl_bgdp_ref r;
    memset(&k, 0, sizeof k);
    k.C = C_cam; k.sc = CAM_SC; k.post = CAM_POST;
    k.org[0] = CAM_ORG[0]; k.org[1] = CAM_ORG[1]; k.org[2] = CAM_ORG[2];
    k.dmode = SL_BGGPU_DEPTH_SINGLE;
    sweep_point(i, &k.px, &k.py, &k.pz);
    sl_bgdp_reference(&k, &r);
    return r.clamped;
}

int sl_bgdp_build_cases(void)
{
    const float W = 1000.0f;            /* a plain w for the clamp cases     */
    float px, py, pz;
    int i, first_clamped = -1, last_unclamped = -1;

    build_camera(C_cam);
    build_direct(C_dir);
    derive_coefficients();
    g_ncases = 0;

    /* ---- SL_BGGPU_DEPTH_SINGLE: the one-range frame's far clamp ---------
     * The GLSL's arm is
     *   else if (u_dmode == 0 && u_fogless == 0 && c.w > 0.0 && c.z > c.w) c.z = c.w;
     * which is four conditions. Each case below drives one side of one of
     * them, and the direct matrix puts z and w where the case says. */
    add("single/below-clamp-1ulp",
        "c.z one ulp BELOW c.w: the strict comparison must not fire",
        C_dir, "direct", 0.0f, W, nextafterf(W, -1.0e30f),
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    add("single/at-clamp-exactly",
        "c.z EXACTLY c.w: `c.z > c.w` is strict, so the clamp must not fire",
        C_dir, "direct", 0.0f, W, W,
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    add("single/above-clamp-1ulp",
        "c.z one ulp ABOVE c.w: the smallest input the clamp must catch",
        C_dir, "direct", 0.0f, W, nextafterf(W, 1.0e30f),
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    add("single/above-clamp-far",
        "c.z far above c.w: the clamp well inside its arm",
        C_dir, "direct", 0.0f, W, 4.0e6f,
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    add("single/w-zero",
        "c.w EXACTLY 0 with c.z above it: `c.w > 0.0` must refuse the clamp",
        C_dir, "direct", 0.0f, 0.0f, 1.0f,
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    add("single/w-negative",
        "c.w behind the eye with c.z above it: the clamp must not fire",
        C_dir, "direct", 0.0f, -500.0f, 100.0f,
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    add("single/w-smallest-normal",
        "c.w the smallest positive NORMAL float: `c.w > 0.0` on its tightest "
        "specified input, and the clamp fires",
        C_dir, "direct", 0.0f, FLT_MIN, 1.0f,
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    add("single/w-smallest-normal-z-below",
        "the same tiny c.w with c.z below it: the other side of that comparison",
        C_dir, "direct", 0.0f, FLT_MIN, 0.0f,
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    add("single/fogless",
        "a fogless walk with c.z above c.w: `u_fogless == 0` must refuse the clamp",
        C_dir, "direct", 0.0f, W, 4.0e6f,
        SL_BGGPU_DEPTH_SINGLE, 1, 0.0f, 0.0f);
    /* ADVISORY: a denormal w. GLSL's default precision does not require
     * denormal support, so a driver that flushes to zero takes the `c.w > 0.0`
     * branch the other way. Measured and reported, not counted. */
    add("single/w-denormal",
        "ADVISORY - c.w DENORMAL: a driver that flushes denormals to zero "
        "answers `c.w > 0.0` differently from the CPU, which is a measurement "
        "and not a defect on either side",
        C_dir, "direct", 0.0f, 1.0e-40f, 1.0f,
        SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f)->advisory = 1;

    /* The same clamp on the camera matrix, at the sweep's own boundary: the
     * last vertex it leaves alone and the first it catches, found by evaluating
     * the reference rather than by picking indices. */
    for (i = 0; i < SWEEP_N; i++) {
        if (sweep_clamps(i)) { if (first_clamped < 0) first_clamped = i; }
        else if (first_clamped < 0) last_unclamped = i;
    }
    if (last_unclamped >= 0) {
        sweep_point(last_unclamped, &px, &py, &pz);
        add("single/camera-last-unclamped",
            "the camera sweep's last vertex inside the far plane: the clamp's "
            "boundary on a real projection",
            C_cam, "camera", px, py, pz, SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    }
    if (first_clamped >= 0) {
        sweep_point(first_clamped, &px, &py, &pz);
        add("single/camera-first-clamped",
            "the camera sweep's first vertex past the far plane: the first "
            "input the clamp catches on a real projection",
            C_cam, "camera", px, py, pz, SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    }
    sweep_point(0, &px, &py, &pz);
    add("single/camera-nearest",
        "the camera sweep's near end, well inside the far plane",
        C_cam, "camera", px, py, pz, SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);
    sweep_point(SWEEP_N - 1, &px, &py, &pz);
    add("single/camera-farthest",
        "the camera sweep's far end, the largest w the ranges are derived from",
        C_cam, "camera", px, py, pz, SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f);

    /* ---- SL_BGGPU_DEPTH_FAR0: z = k1*w - k2, and WHICH pair -------------
     * The expression is unconditional - `if (u_dmode == 1) c.z = u_fk1 * c.w -
     * u_fk2;` reads neither fogless nor z - so what is left to probe is the
     * range's two ends, the band where the two pairs disagree, and that the
     * pair the case carries is the pair the expression uses. */
    add("far0/opaque-range-start",
        "w EXACTLY the opaque range's start, zf * SL_WA_FAR_OVERLAP: the end "
        "the pair maps to -1",
        C_dir, "direct", 0.0f, g_from_k, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_k1, g_k2);
    add("far0/opaque-range-end",
        "w EXACTLY the range's end, wmax * 1.001f: the end the pair maps to +1",
        C_dir, "direct", 0.0f, g_to, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_k1, g_k2);
    add("far0/opaque-range-mid",
        "w halfway along the opaque range: the expression away from both ends",
        C_dir, "direct", 0.0f, 0.5f * (g_from_k + g_to), 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_k1, g_k2);
    add("far0/opaque-in-overlap-band",
        "w inside the OVERLAP BAND [zf*OVERLAP, zf*OVERLAP_BLEND] under the "
        "OPAQUE pair: inside the opaque range, before the blend range starts",
        C_dir, "direct", 0.0f, g_zf * 0.995f, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_k1, g_k2);
    add("far0/blend-in-overlap-band",
        "the SAME w under the BLEND pair (SL_WA_FAR_OVERLAP_BLEND, "
        "bg_far_rezero's): the depth must differ, which is what makes "
        "coefficient selection visible rather than assumed",
        C_dir, "direct", 0.0f, g_zf * 0.995f, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_t1, g_t2);
    add("far0/blend-range-start",
        "w EXACTLY the blend range's start, zf * SL_WA_FAR_OVERLAP_BLEND",
        C_dir, "direct", 0.0f, g_from_t, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_t1, g_t2);
    add("far0/blend-range-end",
        "w EXACTLY the range's end under the blend pair",
        C_dir, "direct", 0.0f, g_to, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_t1, g_t2);
    add("far0/w-zero",
        "w EXACTLY 0: FAR0 has no guard, so the depth is -k2",
        C_dir, "direct", 0.0f, 0.0f, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_k1, g_k2);
    add("far0/w-negative",
        "w behind the eye: still the unguarded expression, -k1 - k2",
        C_dir, "direct", 0.0f, -1.0f, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 0, g_k1, g_k2);
    add("far0/z-input-ignored",
        "the range's end with an absurd input z: FAR0 reads w only",
        C_dir, "direct", 0.0f, g_to, -9.0e5f,
        SL_BGGPU_DEPTH_FAR0, 0, g_k1, g_k2);
    add("far0/fogless-ignored",
        "the range's end on a fogless walk: the `u_dmode == 1` arm is taken "
        "before the clamp's arm, so fogless cannot reach it",
        C_dir, "direct", 0.0f, g_to, 7.0f,
        SL_BGGPU_DEPTH_FAR0, 1, g_k1, g_k2);
    if (first_clamped >= 0) {
        sweep_point(first_clamped, &px, &py, &pz);
        add("far0/camera-past-far-opaque",
            "a camera vertex genuinely past the far plane, opaque pair: FAR0 "
            "where the pass actually selects it",
            C_cam, "camera", px, py, pz, SL_BGGPU_DEPTH_FAR0, 0, g_k1, g_k2);
        add("far0/camera-past-far-blend",
            "the same vertex, blend pair: bg_far_rezero's mesh-level selection",
            C_cam, "camera", px, py, pz, SL_BGGPU_DEPTH_FAR0, 0, g_t1, g_t2);
    }

    /* ---- SL_BGGPU_DEPTH_FAR1: the game's range, unclamped --------------
     * Neither coefficient and neither flag may reach the depth: the mode falls
     * through both arms and gl_Position keeps the z the product computed. */
    add("far1/direct-clamp-region",
        "z far above w, where SINGLE would clamp: FAR1 must keep the "
        "unclamped z",
        C_dir, "direct", 0.0f, W, 4.0e6f,
        SL_BGGPU_DEPTH_FAR1, 0, 0.0f, 0.0f);
    add("far1/coefficients-ignored",
        "the same input with the opaque pair uploaded: FAR1 must not read it",
        C_dir, "direct", 0.0f, W, 4.0e6f,
        SL_BGGPU_DEPTH_FAR1, 0, g_k1, g_k2);
    add("far1/coefficients-absurd-ignored",
        "the same input with a pair no range would produce: identical depth, "
        "or FAR1 is reading a coefficient",
        C_dir, "direct", 0.0f, W, 4.0e6f,
        SL_BGGPU_DEPTH_FAR1, 0, -7.5f, 9999.0f);
    add("far1/fogless-ignored",
        "the same input on a fogless walk: FAR1 must not read the flag either",
        C_dir, "direct", 0.0f, W, 4.0e6f,
        SL_BGGPU_DEPTH_FAR1, 1, 0.0f, 0.0f);
    add("far1/w-negative",
        "w behind the eye: FAR1 keeps z there too",
        C_dir, "direct", 0.0f, -500.0f, 100.0f,
        SL_BGGPU_DEPTH_FAR1, 0, 0.0f, 0.0f);
    sweep_point(0, &px, &py, &pz);
    add("far1/camera-inside-far",
        "a camera vertex inside the far plane: FAR1 and SINGLE agree here",
        C_cam, "camera", px, py, pz, SL_BGGPU_DEPTH_FAR1, 0, g_k1, g_k2);
    if (first_clamped >= 0) {
        sweep_point(first_clamped, &px, &py, &pz);
        add("far1/camera-past-far",
            "the same camera vertex the clamp catches: FAR1, SINGLE and FAR0 "
            "must give three different depths here",
            C_cam, "camera", px, py, pz, SL_BGGPU_DEPTH_FAR1, 0, g_k1, g_k2);
    }
    return g_ncases;
}

const struct sl_bgdp_case *sl_bgdp_cases(void) { return g_cases; }

/* Everything from here to the end of the file is the depth probe's OWN run:
 * its self-checks, its table print, its driver call and its entry point. The
 * vertex-output probe reuses the case table and the reference above and brings
 * its own, so SL_BGDP_NO_MAIN compiles this out. See the note at the ledger. */
#ifndef SL_BGDP_NO_MAIN

/* ---- 1. the shader text is the production text -------------------------- */

/* The module compiled against the SYNTHETIC GL of sl_gfx_bggpu_testgl.c has no
 * driver and no window, and its stub records the pointer handed to
 * glShaderSource. Comparing those bytes with the extraction proves the pinned
 * text IS what ships, at run time, with no GL anywhere - the extractor's hash
 * alone would only prove the extraction is stable. */
static void st_text_binding(void)
{
    const char *text = sl_bgdp_glsl();
    int i;

    printf("  binding: the pinned text is byte-for-byte the module's own\n");

    CHECK(strlen(text) == sl_bgdp_glsl_bytes(),
          "the generated byte count is the text's length (%u vs %u)",
          (unsigned) strlen(text), sl_bgdp_glsl_bytes());
    CHECK(strncmp(text, "#version 400 compatibility\n", 27) == 0,
          "the pinned text opens with the module's own #version line");
    CHECK(sl_bgdp_uniform_count() == 8,
          "eight uniforms were extracted (%d)", sl_bgdp_uniform_count());

    /* The stub's own expected name list, which the existing fixture asserts the
     * module resolves in order. Extraction must agree with it. */
    for (i = 0; i < sl_bgdp_uniform_count() && i < 8; i++)
        CHECK(strcmp(sl_bgdp_uniform_names()[i], bgt_uniform_names[i]) == 0,
              "extracted uniform %d is %s, the module's own order says %s",
              i, sl_bgdp_uniform_names()[i], bgt_uniform_names[i]);

    /* Arm the module through the REAL policy and let it hand its text over. */
    sl_bggpu_invalidate();
    bgt_reset();
    sl_perf_opt_set(1);
    CHECK(sl_bggpu_ready() == 1, "the module is ready against the synthetic GL");
    CHECK(bg.shader_source == 1 && bg.last_source_count == 1,
          "it sourced one shader with one string (%ld, %d)",
          bg.shader_source, (int) bg.last_source_count);
    CHECK(bg.last_source != NULL && strcmp(bg.last_source, text) == 0,
          "THE TEXT THE MODULE HANDS TO glShaderSource IS THE PINNED TEXT, "
          "byte for byte");
    CHECK(bg.get_uniform_location == 8,
          "it resolved eight uniform locations (%ld)", bg.get_uniform_location);
    for (i = 0; i < sl_bgdp_uniform_count() && i < 8; i++)
        CHECK(bgt_loc(sl_bgdp_uniform_names()[i]) >= 0,
              "the extracted name %s is one the module resolves",
              sl_bgdp_uniform_names()[i]);
    CHECK(bg.v_stale_name == 0 && bg.v_source_without_shader == 0,
          "and the stub's watchdogs stayed silent");

    printf("    glsl sha256     %s\n", sl_bgdp_glsl_sha256());
    printf("    uniforms sha256 %s\n", sl_bgdp_uniforms_sha256());
    printf("    source sha256   %s (recorded, not enforced)\n", sl_bgdp_source_sha256());
}

/* ---- 2. the coefficients come from the real helpers --------------------- */

static void st_coefficients(void)
{
    float probe;

    printf("  coefficients: sl_wa_far_plane and sl_wa_far_range, not numbers\n");

    CHECK(g_have_zf == 1, "sl_wa_far_plane recovers a far plane from the camera matrix");
    CHECK(fabsf(g_zf - 15000.0f) < 1.0f,
          "and it is the 15000 the matrix was built with (%.3f)", (double) g_zf);
    CHECK(sl_wa_far_plane(C_dir, &probe) == 0,
          "the direct matrix is NOT a projection and the helper refuses it");
    CHECK(g_wmax > g_zf,
          "part of the swept item lies past the far plane, which is the "
          "two-range case (wmax %.3f, zf %.3f)", (double) g_wmax, (double) g_zf);
    CHECK(g_from_t > g_from_k,
          "the blend range starts LATER than the opaque one, so there is an "
          "overlap band to probe (%.4f vs %.4f)", (double) g_from_t, (double) g_from_k);
    CHECK(g_zf * 0.995f > g_from_k && g_zf * 0.995f < g_from_t,
          "and the band's probe w is inside it (%.4f)", (double) (g_zf * 0.995f));
    /* The range's own contract, as src/gfx/sl_gfx_worldasset.h states it:
     * "c2 = k1 * w - k2 maps w = from to the near plane and w = to to the far
     * plane" - which is the NDC depth z/w reaching -1 and +1, not z itself.
     * Checked loosely on purpose: this is the helper's definition, not the
     * probe's comparison, which is exact and on z. */
    CHECK(fabsf((g_k1 * g_from_k - g_k2) / g_from_k + 1.0f) < 1.0e-3f,
          "the opaque pair maps its range start to the near plane, z/w = -1 (%.6f)",
          (double) ((g_k1 * g_from_k - g_k2) / g_from_k));
    CHECK(fabsf((g_k1 * g_to - g_k2) / g_to - 1.0f) < 1.0e-3f,
          "and its range end to the far plane, z/w = +1 (%.6f)",
          (double) ((g_k1 * g_to - g_k2) / g_to));
    CHECK(fabsf((g_t1 * g_from_t - g_t2) / g_from_t + 1.0f) < 1.0e-3f,
          "the blend pair maps its own start to z/w = -1 (%.6f)",
          (double) ((g_t1 * g_from_t - g_t2) / g_from_t));
    CHECK(fabsf((g_t1 * g_to - g_t2) / g_to - 1.0f) < 1.0e-3f,
          "and its end to z/w = +1 (%.6f)",
          (double) ((g_t1 * g_to - g_t2) / g_to));
    CHECK(g_k1 != g_t1 && g_k2 != g_t2,
          "and the two pairs are different numbers, or selection could not be seen");
}

/* ---- 3. the reference computes all three cases -------------------------- */

static void st_reference(void)
{
    const struct sl_bgdp_case *cs = sl_bgdp_cases();
    int i, n = g_ncases;
    int nsingle = 0, nfar0 = 0, nfar1 = 0, nclamped = 0, nunclamped = 0, nadvisory = 0;
    int i_band_k = -1, i_band_t = -1, i_f1 = -1, i_f1k = -1, i_f1absurd = -1;
    int i_cam_single = -1, i_cam_far0 = -1, i_cam_far1 = -1;
    struct sl_bgdp_ref r;

    printf("  reference: the three depth cases, computed\n");

    for (i = 0; i < n; i++) {
        sl_bgdp_reference(&cs[i], &r);
        if (cs[i].advisory) nadvisory++;
        if (cs[i].dmode == SL_BGGPU_DEPTH_SINGLE) {
            nsingle++;
            if (r.clamped) nclamped++; else nunclamped++;
        } else if (cs[i].dmode == SL_BGGPU_DEPTH_FAR0) nfar0++;
        else nfar1++;
        if (strcmp(cs[i].name, "far0/opaque-in-overlap-band") == 0) i_band_k = i;
        if (strcmp(cs[i].name, "far0/blend-in-overlap-band") == 0) i_band_t = i;
        if (strcmp(cs[i].name, "far1/direct-clamp-region") == 0) i_f1 = i;
        if (strcmp(cs[i].name, "far1/coefficients-ignored") == 0) i_f1k = i;
        if (strcmp(cs[i].name, "far1/coefficients-absurd-ignored") == 0) i_f1absurd = i;
        if (strcmp(cs[i].name, "single/camera-first-clamped") == 0) i_cam_single = i;
        if (strcmp(cs[i].name, "far0/camera-past-far-opaque") == 0) i_cam_far0 = i;
        if (strcmp(cs[i].name, "far1/camera-past-far") == 0) i_cam_far1 = i;
    }

    CHECK(nsingle > 0 && nfar0 > 0 && nfar1 > 0,
          "every one of the three modes has cases (SINGLE %d, FAR0 %d, FAR1 %d)",
          nsingle, nfar0, nfar1);
    CHECK(nclamped > 0 && nunclamped > 0,
          "the SINGLE clamp fires on some inputs and not on others (%d / %d), "
          "so the branch itself is covered", nclamped, nunclamped);
    CHECK(nadvisory == 1, "exactly one case is advisory (%d)", nadvisory);

    /* The clamp fires exactly where the table says it should. Each name
     * encodes the expectation, so this is the table and the reference checking
     * each other rather than a restatement. */
    for (i = 0; i < n; i++) {
        int want;
        if (cs[i].dmode != SL_BGGPU_DEPTH_SINGLE) continue;
        if (cs[i].advisory) continue;
        want = (strcmp(cs[i].name, "single/above-clamp-1ulp") == 0
                || strcmp(cs[i].name, "single/above-clamp-far") == 0
                || strcmp(cs[i].name, "single/w-smallest-normal") == 0
                || strcmp(cs[i].name, "single/camera-first-clamped") == 0
                || strcmp(cs[i].name, "single/camera-farthest") == 0);
        sl_bgdp_reference(&cs[i], &r);
        CHECK(r.clamped == want, "%s: the clamp %s, and the case says it %s",
              cs[i].name, r.clamped ? "fired" : "did not fire",
              want ? "should" : "should not");
    }

    /* Coefficient SELECTION is visible: the same w, two pairs, two depths. */
    if (i_band_k >= 0 && i_band_t >= 0) {
        struct sl_bgdp_ref a, b;
        sl_bgdp_reference(&cs[i_band_k], &a);
        sl_bgdp_reference(&cs[i_band_t], &b);
        CHECK(a.c[3] == b.c[3],
              "the overlap-band pair of cases has the same w (%.6f vs %.6f)",
              (double) a.c[3], (double) b.c[3]);
        CHECK(!sl_bgdp_equal(a.z, b.z),
              "and DIFFERENT depths under the opaque and blend pairs "
              "(%.9g vs %.9g), so coefficient selection is measurable",
              (double) a.z, (double) b.z);
    } else {
        CHECK(0, "the overlap-band cases are missing from the table");
    }

    /* FAR1 reads neither coefficient. */
    if (i_f1 >= 0 && i_f1k >= 0 && i_f1absurd >= 0) {
        struct sl_bgdp_ref a, b, c;
        sl_bgdp_reference(&cs[i_f1], &a);
        sl_bgdp_reference(&cs[i_f1k], &b);
        sl_bgdp_reference(&cs[i_f1absurd], &c);
        CHECK(sl_bgdp_equal(a.z, b.z) && sl_bgdp_equal(a.z, c.z),
              "FAR1's depth is the same under no pair, the real pair and an "
              "absurd pair (%.9g, %.9g, %.9g)",
              (double) a.z, (double) b.z, (double) c.z);
        CHECK(sl_bgdp_equal(a.z, a.zraw),
              "and it is the unclamped z the pass keeps");
    } else {
        CHECK(0, "the FAR1 coefficient-independence cases are missing");
    }

    /* On one camera vertex past the far plane, the three modes must be three
     * different depths - otherwise a driver run could pass while selecting the
     * wrong one. */
    if (i_cam_single >= 0 && i_cam_far0 >= 0 && i_cam_far1 >= 0) {
        struct sl_bgdp_ref a, b, c;
        sl_bgdp_reference(&cs[i_cam_single], &a);
        sl_bgdp_reference(&cs[i_cam_far0], &b);
        sl_bgdp_reference(&cs[i_cam_far1], &c);
        CHECK(!sl_bgdp_equal(a.z, b.z) && !sl_bgdp_equal(a.z, c.z)
              && !sl_bgdp_equal(b.z, c.z),
              "the three modes give three different depths on the same camera "
              "vertex (%.9g, %.9g, %.9g)", (double) a.z, (double) b.z, (double) c.z);
    } else {
        CHECK(0, "the three-mode camera vertex is missing from the table");
    }
}

/* ---- 4. the comparison bites ------------------------------------------- */

/* A comparison that cannot fail proves nothing, so each control below feeds the
 * comparator a deliberately wrong value and requires a REJECTION. The last one
 * is the positive control: without it a comparator that rejected everything
 * would pass this whole section. */
static void st_negative_controls(void)
{
    const struct sl_bgdp_case *cs = sl_bgdp_cases();
    struct sl_bgdp_case bad;
    struct sl_bgdp_ref good, wrong;
    int i, i_f0 = -1, i_clamp = -1;

    printf("  controls: a wrong coefficient pair, and a wrong depth, are REJECTED\n");

    for (i = 0; i < g_ncases; i++) {
        if (strcmp(cs[i].name, "far0/opaque-range-mid") == 0) i_f0 = i;
        if (strcmp(cs[i].name, "single/above-clamp-far") == 0) i_clamp = i;
    }
    if (i_f0 < 0 || i_clamp < 0) {
        CHECK(0, "the control cases are missing from the table");
        return;
    }

    sl_bgdp_reference(&cs[i_f0], &good);
    CHECK(sl_bgdp_equal(good.z, good.z),
          "POSITIVE CONTROL: the comparator accepts the reference's own depth");

    /* THE WRONG PAIR: the blend pair where the mesh is opaque. This is the
     * mistake the probe exists to catch - the pass selects per mesh, and a
     * program given the other mesh's pair draws a wrong depth that no clip
     * component would reveal. */
    bad = cs[i_f0];
    bad.fk1 = g_t1; bad.fk2 = g_t2;
    sl_bgdp_reference(&bad, &wrong);
    CHECK(!sl_bgdp_equal(wrong.z, good.z),
          "the BLEND pair where the opaque pair belongs is rejected "
          "(%.9g vs %.9g)", (double) wrong.z, (double) good.z);
    printf("    on %s the opaque pair (%.9g, %.9g) gives %.9g;\n"
           "      the BLEND pair (%.9g, %.9g) gives %.9g - REJECTED\n",
           cs[i_f0].name, (double) cs[i_f0].fk1, (double) cs[i_f0].fk2,
           (double) good.z, (double) g_t1, (double) g_t2, (double) wrong.z);

    /* One ulp on each coefficient: the comparison has no tolerance, so the
     * smallest possible wrong pair must also be rejected. */
    bad = cs[i_f0];
    bad.fk1 = nextafterf(cs[i_f0].fk1, 1.0e30f);
    sl_bgdp_reference(&bad, &wrong);
    CHECK(!sl_bgdp_equal(wrong.z, good.z),
          "k1 one ulp out is rejected (%.9g vs %.9g)",
          (double) wrong.z, (double) good.z);
    bad = cs[i_f0];
    bad.fk2 = nextafterf(cs[i_f0].fk2, 1.0e30f);
    sl_bgdp_reference(&bad, &wrong);
    CHECK(!sl_bgdp_equal(wrong.z, good.z),
          "k2 one ulp out is rejected (%.9g vs %.9g)",
          (double) wrong.z, (double) good.z);

    /* One ulp on the depth itself. */
    CHECK(!sl_bgdp_equal(nextafterf(good.z, 1.0e30f), good.z),
          "a depth one ulp out is rejected");
    CHECK(!sl_bgdp_equal(-good.z, good.z) || good.z == 0.0f,
          "and so is its negation");

    /* A MODE MIX-UP on an input where the three differ: the clamped vertex's
     * SINGLE depth offered as its FAR0 depth. */
    sl_bgdp_reference(&cs[i_clamp], &good);
    bad = cs[i_clamp];
    bad.dmode = SL_BGGPU_DEPTH_FAR0;
    bad.fk1 = g_k1; bad.fk2 = g_k2;
    sl_bgdp_reference(&bad, &wrong);
    CHECK(!sl_bgdp_equal(wrong.z, good.z),
          "FAR0's depth offered as SINGLE's is rejected (%.9g vs %.9g)",
          (double) wrong.z, (double) good.z);
    bad.dmode = SL_BGGPU_DEPTH_FAR1;
    sl_bgdp_reference(&bad, &wrong);
    CHECK(!sl_bgdp_equal(wrong.z, good.z),
          "and so is FAR1's (%.9g vs %.9g)", (double) wrong.z, (double) good.z);
}

/* ---- 5. the comparison the DRIVER PATH uses, shown to bite -------------- */

/* The one comparison loop, used by the driver run and by the control below, so
 * what the control exercises is the code that will judge a driver and not a
 * second copy of it. Returns the number of non-advisory differences and, when
 * `report` is set, prints each one. */
static int count_differences(const float *measured, int report, int *advisory_out)
{
    const struct sl_bgdp_case *cs = sl_bgdp_cases();
    int i, bad = 0, adv = 0;
    for (i = 0; i < g_ncases; i++) {
        struct sl_bgdp_ref r;
        sl_bgdp_reference(&cs[i], &r);
        if (sl_bgdp_equal(measured[i], r.z)) continue;
        if (cs[i].advisory) {
            adv++;
            if (report)
                printf("    ADVISORY DIFFERENCE %s: measured %.9g, reference %.9g (%s)\n",
                       cs[i].name, (double) measured[i], (double) r.z, cs[i].probes);
            continue;
        }
        bad++;
        if (report)
            printf("    FAIL %s: measured %.9g, reference %.9g\n",
                   cs[i].name, (double) measured[i], (double) r.z);
    }
    if (advisory_out != NULL) *advisory_out = adv;
    return bad;
}

/* A driver whose depths all match must produce zero differences, and a driver
 * that selected the OTHER coefficient pair, or the wrong one of the three
 * modes, must not. Both are run here with no GL: the measured array is
 * fabricated, and what is under test is the comparison, not a driver. */
static void st_comparison_bites(void)
{
    const struct sl_bgdp_case *cs = sl_bgdp_cases();
    static float measured[SL_BGDP_MAX_CASES];
    int i, adv = 0, bad, expected = 0;

    printf("  comparison: the driver path's own loop accepts the reference and "
           "rejects a swapped pair\n");

    for (i = 0; i < g_ncases; i++) {
        struct sl_bgdp_ref r;
        sl_bgdp_reference(&cs[i], &r);
        measured[i] = r.z;
    }
    bad = count_differences(measured, 0, &adv);
    CHECK(bad == 0 && adv == 0,
          "POSITIVE CONTROL: the reference's own depths produce no differences "
          "(%d, %d advisory)", bad, adv);

    /* The fabricated wrong driver: every FAR0 case measured with the OTHER
     * pair, every FAR1 case measured as if the clamp had been applied, every
     * SINGLE case measured as if it had not. */
    for (i = 0; i < g_ncases; i++) {
        struct sl_bgdp_case k = cs[i];
        struct sl_bgdp_ref r, w;
        sl_bgdp_reference(&cs[i], &r);
        if (k.dmode == SL_BGGPU_DEPTH_FAR0) {
            int opaque = (k.fk1 == g_k1 && k.fk2 == g_k2);
            k.fk1 = opaque ? g_t1 : g_k1;
            k.fk2 = opaque ? g_t2 : g_k2;
            sl_bgdp_reference(&k, &w);
            measured[i] = w.z;
        } else if (k.dmode == SL_BGGPU_DEPTH_FAR1) {
            k.dmode = SL_BGGPU_DEPTH_SINGLE;    /* the clamp, wrongly applied */
            sl_bgdp_reference(&k, &w);
            measured[i] = w.z;
        } else {
            measured[i] = r.zraw;               /* the clamp, wrongly skipped */
        }
        if (!sl_bgdp_equal(measured[i], r.z) && !cs[i].advisory) expected++;
    }
    bad = count_differences(measured, 0, &adv);
    CHECK(expected > 0, "the fabricated wrong driver differs on some cases (%d)", expected);
    CHECK(bad == expected,
          "and the comparison reports exactly those %d differences (%d), so it "
          "bites rather than agreeing with whatever it is handed", expected, bad);
}

/* ---- the table, printed ------------------------------------------------- */

static void print_table(void)
{
    const struct sl_bgdp_case *cs = sl_bgdp_cases();
    static const char *const mode[3] = { "SINGLE", "FAR0  ", "FAR1  " };
    int i;
    printf("\n  the synthetic probe inputs (%d), and the boundary each is here for:\n",
           g_ncases);
    for (i = 0; i < g_ncases; i++) {
        struct sl_bgdp_ref r;
        sl_bgdp_reference(&cs[i], &r);
        printf("   %2d %s %-8s %-34s z=%-14.9g w=%-14.9g -> %.9g%s\n",
               i, mode[cs[i].dmode], cs[i].matrix, cs[i].name,
               (double) r.zraw, (double) r.c[3], (double) r.z,
               cs[i].advisory ? "  [advisory]" : "");
        printf("        %s\n", cs[i].probes);
    }
}

/* ---- the driver run, or the line that says what it would be ------------- */

static int run_or_describe(int want_gl)
{
    const struct sl_bgdp_case *cs = sl_bgdp_cases();
    static float measured[SL_BGDP_MAX_CASES];
    char why[512];
    int bad = 0, advisory_bad = 0;

    if (!want_gl) {
        printf("\n  NO DRIVER RAN. %s\n",
               sl_bgdp_gl_built()
               ? "This build can create a context; it was not asked to (--gl)."
               : "This build has no GL entry point compiled in and does not "
                 "link opengl32, so it cannot create one.");
        printf("  WHAT A DRIVER RUN WOULD DO: compile the pinned text above as a\n"
               "  GL_VERTEX_SHADER in a hidden-window compatibility context, link it\n"
               "  with gl_Position captured through transform feedback, upload each\n"
               "  case's eight uniforms by the module's own names, draw one\n"
               "  GL_POINTS vertex per case with GL_RASTERIZER_DISCARD enabled, read\n"
               "  the captured clip position back and compare its z with the CPU\n"
               "  reference's bit for bit. %d draws, no window shown, no file\n"
               "  written, nothing installed.\n", g_ncases);
        printf("  Command: tools\\windows\\gpudepthprobe.ps1 -RunGl\n");
        return 0;
    }

    why[0] = '\0';
    printf("\n  DRIVER RUN\n");
    if (sl_bgdp_gl_run(cs, g_ncases, measured, why, (int) sizeof why) != 0) {
        printf("    FAIL: the driver run did not complete: %s\n",
               why[0] != '\0' ? why : "no reason given");
        fails++; checks++;
        return 1;
    }
    bad = count_differences(measured, 1, &advisory_bad);
    checks += g_ncases;
    fails += bad;
    printf("    %d cases, %d differ, %d advisory differences\n",
           g_ncases, bad, advisory_bad);
    return bad != 0;
}

int main(int argc, char **argv)
{
    static char cfg[520];
    const char *tmp = getenv("TEMP");
    const char *path;
    int want_gl = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gl") == 0) want_gl = 1;
        else {
            fprintf(stderr, "bggpudepthprobe: unknown argument %s "
                            "(only --gl is accepted)\n", argv[i]);
            return 2;
        }
    }
    if (want_gl && !sl_bgdp_gl_built()) {
        fprintf(stderr, "bggpudepthprobe: --gl was asked for, but this build has "
                        "no GL side. Build bggpudepthprobe-gl.exe.\n");
        return 2;
    }

    printf("background GPU depth probe (src/gfx/sl_gfx_bggpu.c, BGG_VS)\n");
    printf("SYNTHETIC inputs only - no ROM data, no asset, no installed file.\n\n");

    /* The REAL settings store, pointed at a scratch file under %TEMP% so the
     * player's own config is never opened; sl_perf_test_reset drops the
     * policy's cached environment answers after the putenv, so the row governs
     * the module's answers below. Same arrangement as
     * tools/profiling/native/test_sl_gfx_bggpu.c. */
    if (tmp == NULL || *tmp == '\0') tmp = ".";
    snprintf(cfg, sizeof cfg, "SL_CONFIG=%s\\sl_bggpu_depth_probe_config.ini", tmp);
    putenv(cfg);
    path = strchr(cfg, '=') + 1;
    remove(path);
    sl_perf_test_reset();
    sl_settings_init();
    bgt_reset();

    if (sl_bgdp_build_cases() <= 0) {
        fprintf(stderr, "bggpudepthprobe: no cases were built\n");
        remove(path);
        return 1;
    }

    st_text_binding();
    st_coefficients();
    st_reference();
    st_negative_controls();
    st_comparison_bites();
    print_table();
    (void) run_or_describe(want_gl);

    remove(path);
    printf("\n%d checks, %d failed\n", checks, fails);
    if (!want_gl)
        printf("NOT QUALIFIED by this run: no driver evaluated BGG_VS's depth "
               "expressions. The checks above bind the pinned text to the module's "
               "own, derive every coefficient from sl_wa_far_plane / "
               "sl_wa_far_range, and prove the CPU reference computes all three "
               "cases and that the comparison rejects a wrong pair - they say "
               "nothing about what a driver computes.\n");
    return fails == 0 ? 0 : 1;
}

#endif /* !SL_BGDP_NO_MAIN */
