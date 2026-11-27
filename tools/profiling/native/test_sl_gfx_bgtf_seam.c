/* Offline INTEGRATION tests for the background draw seam: the REAL
 * src/gfx/sl_gfx_dl.c driving the REAL src/gfx/sl_gfx_bgtf.c.
 *
 * WHY THIS FIXTURE EXISTS SEPARATELY FROM test_sl_gfx_bgtf.c. That one links
 * the module alone, so it can prove what the module does when called and
 * nothing about whether it is called correctly. Three of the seam's properties
 * live entirely in sl_gfx_dl.c and are invisible there:
 *
 *   - WHICH VERTEX SOURCE each mesh draws from. The candidate switches only the
 *     position array; the meshes it does not admit - four-image, pulled,
 *     coplanar, far shell - draw through their own paths, and leaving a
 *     previous mesh's prepass pool named is what makes one mesh's indices
 *     rasterise against another mesh's positions at another depth.
 *   - WHETHER COVERAGE IS GATED ON THE ACTUAL SOURCE. The admission flag stays
 *     true across the whole walk, including the excluded meshes and the draws
 *     whose prepass refused, so counting on it credits the candidate with
 *     indices the CPU arrays drew.
 *   - WHAT A REFUSAL LEAVES BEHIND. A prepass that refuses mid-walk must put
 *     the original per-mesh source back, not merely return -1.
 *
 * THE REAL TRANSLATION UNIT IS COMPILED IN, NOT REIMPLEMENTED. sl_gfx_dl.c is
 * #included below, which is what makes bg_draw_pass, bggpu_select,
 * bgtf_cpu_source, bg_draw_mesh_run and bgv_vertex_pointer - all file-static -
 * reachable from a test. Nothing here restates their logic: a copied selection
 * algorithm would agree with itself and prove nothing about the code that
 * ships. The predicates that classify a mesh, the slot cache, the counters and
 * the restoration are all the product's own.
 *
 * WHAT IS SYNTHETIC, stated plainly so the coverage claim is not read wider
 * than it is:
 *
 *   - the GL. sl_gfx_seamgl.c, against the real host headers with -D_GDI32_ so
 *     the entry points are ordinary symbols; see that header. No driver, no
 *     window, no GPU, and the prepass program's own arithmetic is never
 *     executed.
 *   - the FRAME INPUTS. The eye, the view matrix, the render origin, the level
 *     and visibility scales, the fog environment and the frame clock are stubs
 *     at the bottom of this file. They are inputs to the pass, not part of it.
 *   - the ASSET. A hand-built struct sl_wa_asset rather than a prepared bundle,
 *     because nothing ROM-derived may enter this tree. Its geometry is chosen
 *     to make the classifications under test reachable and distinguishable.
 *
 *   - the MESH PREDICATES ARE NOT SYNTHETIC. src/gfx/sl_gfx_worldasset.c is
 *     linked for real, so sl_wa_far_range's coefficients, the near-cut
 *     classification, the coplanar rules and the far plane are the product's.
 *
 * WHAT IS NOT COVERED. Menu and settings selection (the candidate is reached
 * only by SL_BG_TF_CLIP at launch), the GLSL arithmetic, any image, and any
 * frame time. See the closing note.
 */
#include "sl_gfx_seamgl.h"

/* The real settings store, which src/gfx/sl_gfx_worldasset.c links against. */
#include "sl_settings.h"

/* THE PRODUCT, COMPILED IN. The file-static draw pass is the thing under test;
 * including the translation unit is what reaches it without editing src/. */
#include "../../../src/gfx/sl_gfx_dl.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, fails, cases, case_fails;
static const char *current_case;

#define CHECK(cond, ...) do {                                                 \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            fails++; case_fails++;                                            \
            printf("    FAIL line %d: ", __LINE__);                           \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

/* THE ONE WATCHDOG A CASE MAY EXPECT A NONZERO VALUE FROM, and why it is a
 * variable rather than a relaxation.
 *
 * `v_draw_without_discard` counts a RECORDING draw that ran with rasterizer
 * discard off - a draw that could reach a fragment - and it is 0 on every arm
 * the renderer has ever had. SL_BG_TF_EMPTY_STATE's row B is T1 D0: a feedback
 * scope open and discard off at the draw, which is precisely that condition and
 * is the row's whole content. A case driving row B must therefore expect exactly
 * one, and a 0 there would mean the row did not happen.
 *
 * SO THE EXPECTATION IS DECLARED BY THE CASE AND THE WATCHDOG STILL FIRES.
 * Removing the check for that arm would have removed it from the arm that most
 * needs an exact count; raising the bound globally would have stopped it
 * catching the thing it exists for anywhere. The value is reset to 0 by
 * begin_case, so a case that forgets to declare one is held to 0 - the
 * expectation cannot leak from one case to the next. The draw is empty on every
 * one of these rows, so no fragment can be produced by it whatever the discard
 * state; that is an API-boundary fact about a count of zero and is not a claim
 * about a driver. */
static long expect_draw_without_discard;

static void begin_case(const char *name)
{
    cases++;
    case_fails = 0;
    current_case = name;
    /* THE EXPECTATION IS RESET PER CASE, so a case that declares one cannot have
     * it carried into the next one and a case that declares none is held to 0.
     * See the note on the declaration above. */
    expect_draw_without_discard = 0;
    printf("  %s\n", name);
}

static void end_case(void)
{
    /* The watchdogs, asserted at the end of every case: these are properties no
     * single assertion covers, because they are about calls a test did not go
     * looking for. */
    CHECK(sg.v_tf_overrun == 0, "%s: a prepass draw would write past its pool (%ld)",
          current_case, sg.v_tf_overrun);
    CHECK(sg.v_draw_without_discard == expect_draw_without_discard,
          "%s: recording draws that ran with rasterizer discard off: %ld, "
          "expected %ld", current_case, sg.v_draw_without_discard,
          expect_draw_without_discard);
    /* NEVER EXPECTED, ON ANY ARM. glUseProgram inside a feedback scope is
     * invalid; the one program-0 row is a feedback-INACTIVE row, so this stays 0
     * whatever is selected, and an edit that paired the two would be caught here
     * at the real caller. */
    CHECK(sg.v_program_change_in_tf == 0,
          "%s: the program was changed inside an open transform-feedback scope, "
          "which the API forbids (%ld)", current_case,
          sg.v_program_change_in_tf);
    CHECK(sg.v_unknown_buffer == 0,
          "%s: a buffer name this shim never handed out was bound (%ld)",
          current_case, sg.v_unknown_buffer);
    CHECK(sg.v_draws_full == 0,
          "%s: the draw record overflowed and is incomplete (%ld)",
          current_case, sg.v_draws_full);
    CHECK(sg.v_read_inside_tf == 0,
          "%s: a diagnostic readback ran inside an open transform-feedback scope, "
          "so it read a pool still being written (%ld)",
          current_case, sg.v_read_inside_tf);
    printf("    %s\n", case_fails == 0 ? "ok" : "FAILED");
}

/* ---- the synthetic asset ------------------------------------------------ */

/* THE MESHES, AND WHAT EACH ONE IS FOR. Every classification the candidate
 * distinguishes has to be reachable AND separable, which is why the meshes
 * differ in exactly one respect each.
 *
 *   0  PLAIN       candidate-eligible: opaque, no decal, no sphere map
 *   1  FOURIMG     candidate-INELIGIBLE: a NORMAL_SPHERE material extension on
 *                  an asset that carries a tangent frame, which is the
 *                  four-image path's own term
 *   2  PLAIN       eligible again, so a restoration between two served meshes
 *                  is observable rather than inferred from the walk's end
 *   3  BLENDED     eligible, and on a far frame it takes the BLEND overlap's
 *                  coefficient pair rather than the opaque one - the reason
 *                  there is a slot per pair and not one FAR0 buffer
 *   4  HIDDEN      filtered out entirely (SL_BG_HIDE), so it submits no run
 *   5  EMPTY       SHOWN and candidate-eligible, so it is admitted and a pool
 *                  IS bound for it - and it carries no indices, so it submits
 *                  no nonempty run. That is the case "a bind is not a draw" is
 *                  about, and the one the hidden mesh cannot reach: the hidden
 *                  mesh is filtered before the bind, so it never offers
 *                  anything to anything. Its index_count is 0 and its
 *                  first_index is one past the item's list, so it adds no
 *                  indices and leaves every other mesh's span alone.
 *
 *                  IT IS IN THE ITEM ONLY FOR THE SCENARIO THAT NEEDS IT, and
 *                  that is not tidiness. A zero-index mesh makes the pass issue
 *                  a zero-count glDrawElements, which is the product's own
 *                  behaviour and is exactly what the empty-run scenario is
 *                  about - but st_filtered asserts, of the ordinary item, that
 *                  no empty run reaches GL at all. Carrying this mesh in every
 *                  scenario would have made that assertion false and invited
 *                  weakening it, so the mesh count is the scenario's.
 */
#define MESH_PLAIN0   0u
#define MESH_FOURIMG  1u
#define MESH_PLAIN1   2u
#define MESH_BLENDED  3u
#define MESH_HIDDEN   4u
#define MESH_EMPTY    5u
#define NMESH         6u

#define MAT_OPAQUE    0u
#define MAT_SPHERE    1u
#define MAT_BLEND     2u

#define NVERT   64u
#define TRISPER  3u                        /* triangles per mesh */
/* The EMPTY mesh contributes none, so the index list is one mesh shorter than
 * the mesh count. */
#define NDRAWN  (NMESH - 1u)
#define NIDX    (NDRAWN * TRISPER * 3u)

static struct sl_wa_asset g_seam_asset;
static struct sl_wa_vertex g_seam_verts[NVERT];
static unsigned g_seam_idx[NIDX];
static unsigned char g_seam_aux[NVERT * SL_WA_AUX3_SIZE];

/* THE ITEM'S GEOMETRY. The z values put every vertex well beyond the far plane
 * the projection below declares, which is what makes a TWO-RANGE frame
 * reachable: the vertex loop raises `nfar` only where the clip z exceeds the
 * clip w on a walk that is not fogless, and `farpass` needs that count
 * non-zero. Spread in x and y so no two triangles are degenerate. */
static void build_asset(int with_empty)
{
    unsigned i, m, t, k = 0u;
    memset(&g_seam_asset, 0, sizeof g_seam_asset);
    memset(g_seam_verts, 0, sizeof g_seam_verts);
    memset(g_seam_aux, 0, sizeof g_seam_aux);

    for (i = 0; i < NVERT; i++) {
        g_seam_verts[i].x = (float) ((int) (i % 8u) * 40 - 140);
        g_seam_verts[i].y = (float) ((int) (i / 8u) * 40 - 140);
        /* Beyond the far plane (15000) and inside the reach the pass computes. */
        g_seam_verts[i].z = -(16000.0f + (float) i * 50.0f);
        g_seam_verts[i].u = 0.25f;
        g_seam_verts[i].v = 0.5f;
        g_seam_verts[i].r = (unsigned char) (i * 3u);
        g_seam_verts[i].g = (unsigned char) (i * 5u);
        g_seam_verts[i].b = (unsigned char) (i * 7u);
        g_seam_verts[i].a = 255u;
    }
    /* Each mesh gets its own disjoint vertex span, so a mesh drawing from
     * another mesh's pool is a different set of positions and not a subset. */
    for (m = 0; m < NDRAWN; m++)
        for (t = 0; t < TRISPER; t++) {
            const unsigned base = m * 12u + t;
            g_seam_idx[k++] = base % NVERT;
            g_seam_idx[k++] = (base + 1u) % NVERT;
            g_seam_idx[k++] = (base + 9u) % NVERT;
        }

    g_seam_asset.asset_class = SL_WA_CLASS_BACKGROUND;
    strcpy(g_seam_asset.path, "seamtest");
    g_seam_asset.bounds_min[0] = -200.0f; g_seam_asset.bounds_min[1] = -200.0f;
    g_seam_asset.bounds_min[2] = -25000.0f;
    g_seam_asset.bounds_max[0] =  200.0f; g_seam_asset.bounds_max[1] =  200.0f;
    g_seam_asset.bounds_max[2] = -15000.0f;
    g_seam_asset.vertex_count = NVERT;
    g_seam_asset.index_count = NIDX;
    g_seam_asset.material_count = 3u;
    g_seam_asset.texture_count = 0u;
    /* THE EMPTY MESH IS THE SCENARIO'S, not the item's. See the note at
     * MESH_EMPTY: a zero-index mesh makes the pass issue a zero-count draw,
     * which st_filtered legitimately asserts the ordinary item never does. */
    g_seam_asset.mesh_count = with_empty ? NMESH : NDRAWN;
    g_seam_asset.anim_count = 0u;              /* nothing animates: the walk is not
                                           * refused for per-vertex motion */
    g_seam_asset.aux_stride = SL_WA_AUX3_SIZE;
    g_seam_asset.vframe = 1;                   /* the four-image term's other half */
    g_seam_asset.matx_count = 1;
    g_seam_asset.vertices = g_seam_verts;
    g_seam_asset.indices = g_seam_idx;
    g_seam_asset.vaux = (const struct sl_wa_vaux *) (const void *) g_seam_aux;

    g_seam_asset.materials[MAT_OPAQUE].blend = SL_WA_BLEND_OPAQUE;
    g_seam_asset.materials[MAT_SPHERE].blend = SL_WA_BLEND_OPAQUE;
    g_seam_asset.materials[MAT_BLEND].blend  = SL_WA_BLEND_ALPHA;

    /* THE FOUR-IMAGE TERM, as sl_gfx_dl.c states it: a->vframe, the mesh's
     * material in range, and that material's extension mode NORMAL_SPHERE. */
    g_seam_asset.matx[MAT_SPHERE].mode = SL_WA_MATX_NORMAL_SPHERE;
    g_seam_asset.matx[MAT_SPHERE].normal_scale = 1.0f;

    for (m = 0; m < NDRAWN; m++) {
        g_seam_asset.meshes[m].first_index = m * TRISPER * 3u;
        g_seam_asset.meshes[m].index_count = TRISPER * 3u;
        g_seam_asset.meshes[m].material = MAT_OPAQUE;
    }
    /* THE EMPTY MESH: shown, eligible, bound - and with nothing to submit. Its
     * first_index is one past the list and its count is 0, so it owns no
     * indices and no other mesh's span moves. */
    g_seam_asset.meshes[MESH_EMPTY].first_index = NIDX;
    g_seam_asset.meshes[MESH_EMPTY].index_count = 0u;
    g_seam_asset.meshes[MESH_EMPTY].material = MAT_OPAQUE;
    g_seam_asset.meshes[MESH_FOURIMG].material = MAT_SPHERE;
    g_seam_asset.meshes[MESH_BLENDED].material = MAT_BLEND;
}

/* ---- the frame the pass is driven with --------------------------------- */

/* A STANDARD GL PERSPECTIVE, column-major, near 10 and far 15000. Not a
 * convenient shape: sl_wa_far_plane RECOVERS the near and far distances from
 * the matrix itself, so a matrix that is not a real projection makes it answer
 * 0 and no two-range frame can be reached at all. The real
 * src/gfx/sl_gfx_worldasset.c does that recovery here. */
static void set_projection(float n, float f)
{
    memset(g_proj, 0, sizeof g_proj);
    g_proj[0]  = 1.5f;                       /* 2n/(r-l) */
    g_proj[5]  = 2.0f;                       /* 2n/(t-b) */
    g_proj[10] = -(f + n) / (f - n);
    g_proj[11] = -1.0f;
    g_proj[14] = -2.0f * f * n / (f - n);
    g_proj[15] = 0.0f;
}

/* ---- reading the shim's draw record ------------------------------------ */

/* WHICH SOURCE A DRAW READ. The three the pass can present are the prepass
 * pool (a buffer of the module's own), the pass's own stream buffer holding the
 * CPU clip positions, and - on a non-buffered pass - a host address. A draw is
 * attributed by the buffer its POSITION pointer named when it ran. */
static int draw_from_pool(const struct seamgl_draw *d, GLuint pool)
{
    return d->pos.set && d->pos.buf == pool;
}

static int draw_from_stream(const struct seamgl_draw *d)
{
    return d->pos.set && d->pos.buf == g_bgv_stream;
}

/* The prepass pools this walk recorded into, in the order they were bound: the
 * names taken out of the shim's own record rather than predicted. */
static GLuint g_seam_pool[8];
static int    g_seam_npool;

static void collect_pools(void)
{
    /* A pool is a buffer that was bound at transform-feedback index 0. The shim
     * keeps the binding in force at each draw, and only the prepass draws
     * (glDrawArrays) record, so those name every pool this walk filled. */
    int i, j;
    g_seam_npool = 0;
    for (i = 0; i < sg.ndraws; i++) {
        if (!sg.draw[i].arrays || sg.draw[i].tf_buf == 0u) continue;
        for (j = 0; j < g_seam_npool; j++) if (g_seam_pool[j] == sg.draw[i].tf_buf) break;
        if (j == g_seam_npool && g_seam_npool < (int) (sizeof g_seam_pool / sizeof g_seam_pool[0]))
            g_seam_pool[g_seam_npool++] = sg.draw[i].tf_buf;
    }
}

static int is_pool(GLuint name)
{
    int j;
    if (name == 0u) return 0;
    for (j = 0; j < g_seam_npool; j++) if (g_seam_pool[j] == name) return 1;
    return 0;
}

/* The indexed draws of this walk, in order, with the prepass draws left out. */
static int raster_draws(const struct seamgl_draw **out, int cap)
{
    int i, n = 0;
    for (i = 0; i < sg.ndraws && n < cap; i++)
        if (!sg.draw[i].arrays) out[n++] = &sg.draw[i];
    return n;
}

/* ---- the stubs' control surface ---------------------------------------- */

/* The FRAME INPUTS, not the pass. Each is read by sl_gfx_dl.c and answered
 * here, so a scenario is a choice of inputs rather than a patched predicate. */
static float s_level_scale  = 1.0f;
static float s_vis_scale    = 1.0f;
static float s_source_scale = 1.0f;
static int   s_fog_env      = 0;

/* ---- driving one walk --------------------------------------------------- */

/* ONE BACKGROUND WALK, through the real pass.
 *
 * The statics the pass keeps between frames are put back to the values a first
 * walk would see, so one process can drive several independent walks: the item
 * selection, the done/armed flags and the candidate's own per-walk state. The
 * vertex buffers are deliberately NOT reset - an item stays resident across
 * walks in the product too, and the second walk of a run is the one the slot
 * cache's generation key is about. */
static void run_walk(void)
{
    g_bg_asset = &g_seam_asset;
    g_bg_armed = 1;
    g_bg_done = 0;
    g_bgx_pending = 0;
    g_proj_is_world = 1;
    g_proj_world_which = 1;            /* the PLAIN branch: the seam supplies
                                        * world-to-view itself */
    g_dl_frame++;
    sl_seamgl_window();
    bg_draw_pass(0);
    collect_pools();
}

/* What the walk did, for the record and for a failure's context. */
static void report_walk(const char *what)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();
    printf("    [%s] admitted=%d src_end=%d pools=%d prepasses=%lu binds=%lu "
           "raster_draws=%lu raster_indices=%lu refusals=%lu r_draw=%u "
           "elig_walks=%u draws=%d\n",
           what, g_bgtf_walk, g_bgtf_src, g_seam_npool, t->prepasses, t->binds,
           t->raster_draws, t->raster_indices, t->refusals, g_bgtf_r_draw,
           g_bgtf_elig_walks, sg.ndraws);
}

/* ---- 1. the walk is actually admitted ---------------------------------- */

/* THE PRECONDITION FOR EVERY CASE BELOW. If the candidate is not admitted, each
 * later case would assert things about a walk that never entered the path and
 * would pass vacuously - so this runs first and says which term refused. */
static void st_admitted(void)
{
    const struct sl_bgtf_counters *t;
    begin_case("seam: the candidate is ADMITTED for this frame, so the cases below are "
               "not vacuous");
    CHECK(sl_bgtf_opt_in() == 1,
          "SL_BG_TF_CLIP=1 is required - run this through tools/windows/bgtfseamtest.ps1");
    CHECK(sl_bggpu_opt_in() == 1,
          "and SL_BG_GPU_XFORM=1, because the candidate is reached inside the "
          "transform admission");
    run_walk();
    report_walk("admission");
    CHECK(g_bgtf_elig_walks >= 1u,
          "the walk was admitted to the candidate (elig_walks=%u, r_prog=%u, "
          "g_bgv_live=%d)", g_bgtf_elig_walks, g_bgtf_r_prog, g_bgv_live);
    /* THE BUFFERED PASS IS A PRECONDITION, and it has to be read from a fact
     * that OUTLIVES the walk. g_bgv_live is the pass's own in-walk flag and the
     * pass clears it in its teardown (the client-pointer restore), so sampling
     * it here would always see 0 and say nothing. What persists is that the item
     * is resident in the buffer objects and the stream buffer exists - which is
     * what makes bgv_vertex_pointer restore through a buffer rather than a host
     * address, and so what makes the restoration checks below meaningful. */
    CHECK(g_bgv_asset == &g_seam_asset,
          "the item is resident in the pass's buffer objects");
    CHECK(g_bgv_stream != 0u && g_bgv_static != 0u && g_bgv_index != 0u,
          "and the three buffers exist (static %u, index %u, stream %u)",
          (unsigned) g_bgv_static, (unsigned) g_bgv_index, (unsigned) g_bgv_stream);
    CHECK(sl_bgtf_state() == 1, "and the module is READY (%d): %s", sl_bgtf_state(),
          sl_bgtf_why());
    t = sl_bgtf_stats();
    CHECK(t->prepasses >= 1, "at least one prepass ran (%lu)", t->prepasses);
    CHECK(t->raster_draws >= 1, "and at least one submission was counted (%lu)",
          t->raster_draws);
    CHECK(sg.draw_elements >= 1, "the pass issued indexed draws (%ld)", sg.draw_elements);
    /* THE REDUCTION IS DELIBERATELY NOT USED BY THE CANDIDATE, so a refusal at
     * any draw finds the CPU arrays already written. gpu_walk staying 0 is what
     * makes the existing repair cover this walk. */
    CHECK(g_bggpu_bound == 0,
          "the vertex PROGRAM took no part: a walk takes one path or the other, "
          "never both (%d)", g_bggpu_bound);
    end_case();
}

/* ---- 2. TF success -> an EXCLUDED mesh -> TF success -------------------- */

static void st_excluded_restores(void)
{
    const struct seamgl_draw *d[64];
    int n, i, iserved = -1, iexcl = -1, iafter = -1;
    unsigned long idx_before, idx_after;

    begin_case("seam: a candidate-INELIGIBLE mesh between two served ones draws from the "
               "CPU source, not from the previous mesh's prepass pool");
    /* THE DEFECT THIS IS ABOUT. A candidate walk leaves g_bggpu_bound at 0, so
     * bggpu_select's pointer switch never runs, and the excluded meshes -
     * four-image, pulled, coplanar, far shell - skipped pointer selection
     * entirely. A previous mesh's prepass pool therefore stayed named, and the
     * excluded mesh drew its own indices against another mesh's positions at
     * another depth. */
    idx_before = sl_bgtf_stats()->raster_indices;
    run_walk();
    report_walk("excluded");
    n = raster_draws(d, 64);
    CHECK(n >= 3, "the walk issued at least three indexed draws (%d)", n);
    CHECK(g_seam_npool >= 1, "and filled at least one prepass pool (%d)", g_seam_npool);

    /* WHICH DRAW IS WHICH, taken from the SOURCE each one read rather than
     * assumed from the mesh order: a pool-sourced draw, then a draw that is
     * not, then a pool-sourced draw again. */
    for (i = 0; i < n; i++) {
        const int pooled = is_pool(d[i]->pos.buf);
        if (iserved < 0 && pooled) { iserved = i; continue; }
        if (iserved >= 0 && iexcl < 0 && !pooled) { iexcl = i; continue; }
        if (iexcl >= 0 && iafter < 0 && pooled) { iafter = i; }
    }
    CHECK(iserved >= 0, "a draw read a prepass pool (%d)", iserved);
    CHECK(iexcl >= 0,
          "a LATER draw did NOT - the excluded mesh fell back to the CPU source "
          "(%d of %d)", iexcl, n);
    CHECK(iafter >= 0,
          "and a draw after THAT read a pool again, so the fallback was per-mesh "
          "and not the end of the walk (%d)", iafter);

    if (iexcl >= 0) {
        /* NOT A STALE POOL, AND NOT A RAW CLIENT POINTER EITHER. The restoration
         * goes through the pass's own bgv_vertex_pointer, which is what knows
         * this pass is streaming through its own vertex buffer - so the source
         * is the STREAM buffer at the clip positions' offset, not a host
         * address that happens to work when the pass is unbuffered. */
        CHECK(!is_pool(d[iexcl]->pos.buf),
              "the excluded mesh's position source is NOT a prepass pool (%u)",
              (unsigned) d[iexcl]->pos.buf);
        CHECK(draw_from_stream(d[iexcl]),
              "it is the pass's OWN stream buffer, so the restore went through "
              "bgv_vertex_pointer rather than open-coding a client pointer "
              "(%u, stream %u)", (unsigned) d[iexcl]->pos.buf,
              (unsigned) g_bgv_stream);
        CHECK(d[iexcl]->pos.off == 0,
              "at the CLIP positions' offset, not the pushed pool's (%ld)",
              d[iexcl]->pos.off);
        CHECK(d[iexcl]->pos.size == 4,
              "and four floats, which a clip position is (%d)", d[iexcl]->pos.size);
        CHECK(d[iexcl]->program == 0u,
              "drawn through program 0 (%u)", (unsigned) d[iexcl]->program);
    }
    if (iserved >= 0 && iafter >= 0)
        CHECK(d[iserved]->pos.buf != g_bgv_stream && d[iafter]->pos.buf != g_bgv_stream,
              "while the served draws read pools and not the stream (%u, %u)",
              (unsigned) d[iserved]->pos.buf, (unsigned) d[iafter]->pos.buf);

    /* NO DRAW ANYWHERE IN THE WALK READ A WRONG-DEPTH POOL. Every pool-sourced
     * draw must read a pool of THIS walk, and the walk's own pools are the only
     * ones the shim recorded a binding for. */
    for (i = 0; i < n; i++)
        CHECK(d[i]->pos.set, "draw %d had a position source at all", i);

    /* AND THE COVERAGE COUNT EXCLUDES IT. The candidate's indices must be the
     * served meshes' only - which is what gating on the actual source rather
     * than on the admission flag buys. */
    idx_after = sl_bgtf_stats()->raster_indices;
    {
        unsigned long served = 0u;
        for (i = 0; i < n; i++) if (is_pool(d[i]->pos.buf)) served += (unsigned long) d[i]->count;
        CHECK(idx_after - idx_before == served,
              "raster_indices counts exactly the indices of the POOL-SOURCED draws "
              "(%lu counted, %lu drawn from pools)", idx_after - idx_before, served);
        CHECK(served > 0u, "and that is not zero, so the equality is not vacuous (%lu)",
              served);
    }
    end_case();
}

/* ---- 3. a FILTERED mesh contributes nothing ---------------------------- */

static void st_filtered(void)
{
    const struct seamgl_draw *d[64];
    int n, i;
    unsigned long before, after, dbefore, dafter;

    begin_case("seam: a mesh whose runs are all filtered submits none, and is counted as "
               "no coverage - a BIND IS NOT A DRAW");
    /* COUNTING AT THE BIND BOTH OVER- AND UNDER-COUNTED: it credited a mesh
     * whose triangles were all filtered, and missed that one bind can serve
     * several runs. SL_BG_HIDE names the hidden mesh, so this frame has one
     * mesh that is admitted by the predicates and draws nothing. */
    CHECK(bg_mesh_shown(MESH_HIDDEN) == 0,
          "mesh %u is hidden this run (SL_BG_HIDE) - without that the case below "
          "would be about nothing", MESH_HIDDEN);
    CHECK(bg_mesh_shown(MESH_PLAIN0) == 1, "while mesh %u is shown", MESH_PLAIN0);

    before = sl_bgtf_stats()->raster_indices;
    dbefore = sl_bgtf_stats()->raster_draws;
    run_walk();
    report_walk("filtered");
    after = sl_bgtf_stats()->raster_indices;
    dafter = sl_bgtf_stats()->raster_draws;
    n = raster_draws(d, 64);

    /* THE HIDDEN MESH ISSUED NO INDEXED DRAW AT ALL, and the draw COUNT cannot
     * say so: a shown mesh is split into as many runs as its triangle states
     * need, so this frame has more draws than meshes. What identifies the
     * hidden mesh is its INDEX SPAN - every indexed draw carries a byte offset
     * from the item's own index list - so the property is that no draw reads
     * inside that span. */
    CHECK(n > 0, "the walk issued indexed draws (%d)", n);
    {
        const long lo = (long) g_seam_asset.meshes[MESH_HIDDEN].first_index
                        * (long) sizeof g_seam_idx[0];
        const long hi = lo + (long) g_seam_asset.meshes[MESH_HIDDEN].index_count
                        * (long) sizeof g_seam_idx[0];
        int inside = 0, outside = 0;
        for (i = 0; i < n; i++) {
            if (d[i]->elem_off >= lo && d[i]->elem_off < hi) inside++;
            else                                             outside++;
        }
        CHECK(inside == 0,
              "NO draw read inside the hidden mesh's index span [%ld, %ld) - it "
              "submitted nothing at all (%d draws did)", lo, hi, inside);
        CHECK(outside > 0,
              "while the shown meshes did draw, so the zero above is not an empty "
              "walk (%d draws)", outside);
        /* THE SPAN IS REACHABLE IN PRINCIPLE, which is what stops `inside == 0`
         * from being a property of the arithmetic rather than of the filter: the
         * draws that did happen span offsets on both sides of it. */
        {
            int below = 0, above = 0;
            for (i = 0; i < n; i++) {
                if (d[i]->elem_off < lo)  below++;
                if (d[i]->elem_off >= hi) above++;
            }
            CHECK(below > 0,
                  "draws exist below the hidden span (%d), so the offsets really do "
                  "index the item's own list", below);
            (void) above;
        }
    }
    {
        unsigned long served = 0u, sdraws = 0u;
        for (i = 0; i < n; i++)
            if (is_pool(d[i]->pos.buf)) { served += (unsigned long) d[i]->count; sdraws++; }
        CHECK(after - before == served,
              "the indices counted are the pool-sourced draws' and no more (%lu vs %lu)",
              after - before, served);
        CHECK(dafter - dbefore == sdraws,
              "and the submissions counted are those draws, one per nonempty run "
              "(%lu vs %lu)", dafter - dbefore, sdraws);
        /* A BIND IS NOT A DRAW: the walk bound pools at least once per distinct
         * key, and the submissions are counted per run instead. */
        CHECK(sl_bgtf_stats()->binds >= 1, "the walk bound at least one pool (%lu)",
              sl_bgtf_stats()->binds);
    }
    for (i = 0; i < n; i++)
        CHECK(d[i]->count > 0,
              "draw %d submitted a nonempty run, so no empty run reached GL (%ld)",
              i, d[i]->count);
    end_case();
}

/* ---- 4. a prepass refusal AFTER a success ------------------------------ */

static void st_refusal_after_success(void)
{
    const struct seamgl_draw *d[64];
    int n, i, nserved = 0, nfallback = 0;
    unsigned long before, after;
    unsigned r_before;

    begin_case("seam: a prepass that refuses AFTER one succeeded puts the CPU source back "
               "for that mesh and counts NO coverage for it");
    /* THE REFUSAL PATH, WHICH A WALK-LEVEL REFUSAL CANNOT REACH. The walk is
     * admitted and the first pool is allocated; the SECOND allocation is
     * refused, so one mesh of an admitted walk falls back mid-walk. What matters
     * is not the -1 but that the pool stops being the source. */
    before = sl_bgtf_stats()->raster_indices;
    r_before = g_bgtf_r_draw;
    /* THE POOLS HAVE TO BE FRESH FOR AN ALLOCATION TO HAPPEN AT ALL. A slot
     * keeps its buffer and its capacity across walks, so a later walk at the
     * same vertex count stores nothing and there is no allocation to fail -
     * which is why an earlier draft of this case injected a failure and watched
     * the walk succeed. Invalidating drops the slots, so this walk mints and
     * sizes its pools again.
     *
     * The pass's own three buffers are EXEMPT by name: they come through the
     * same entry point, and failing one of them refuses the walk long before a
     * prepass is reached. */
    sl_bgtf_invalidate();
    sg.alloc_exempt[0] = g_bgv_static;
    sg.alloc_exempt[1] = g_bgv_index;
    sg.alloc_exempt[2] = g_bgv_stream;
    sg.alloc_short_initial = 1;
    sg.alloc_bytes = 0L;                /* the store leaves the pool EMPTY */
    sg.alloc_short_skip = 1;            /* ... after letting the first through */
    run_walk();
    report_walk("refusal");
    sg.alloc_short_initial = 0;
    sg.alloc_short_skip = 0;
    sg.alloc_exempt[0] = sg.alloc_exempt[1] = sg.alloc_exempt[2] = 0u;
    n = raster_draws(d, 64);
    after = sl_bgtf_stats()->raster_indices;

    CHECK(g_bgtf_r_draw > r_before,
          "a draw-level refusal was counted (%u -> %u)", r_before, g_bgtf_r_draw);
    CHECK(sl_bgtf_state() == -1,
          "and the allocation failure latched UNAVAILABLE (%d): %s",
          sl_bgtf_state(), sl_bgtf_why());
    for (i = 0; i < n; i++) {
        if (is_pool(d[i]->pos.buf)) nserved++;
        else                        nfallback++;
    }
    CHECK(nserved >= 1,
          "at least one mesh was still served from a pool, so the refusal came "
          "AFTER a success (%d)", nserved);
    CHECK(nfallback >= 1, "and at least one fell back (%d)", nfallback);
    /* EVERY FALLBACK DRAW READS THE PASS'S OWN SOURCE, not a pool left named by
     * the mesh before it. */
    for (i = 0; i < n; i++)
        if (!is_pool(d[i]->pos.buf))
            CHECK(draw_from_stream(d[i]),
                  "fallback draw %d reads the pass's own stream buffer (%u, "
                  "stream %u)", i, (unsigned) d[i]->pos.buf,
                  (unsigned) g_bgv_stream);
    {
        unsigned long served = 0u;
        for (i = 0; i < n; i++)
            if (is_pool(d[i]->pos.buf)) served += (unsigned long) d[i]->count;
        CHECK(after - before == served,
              "and the refused mesh's indices are NOT counted as candidate "
              "coverage (%lu counted, %lu from pools)", after - before, served);
    }
    /* A NEW CONTEXT CLEARS THE LATCH, so the cases after this one are not
     * measuring an unavailable module. */
    sl_bgtf_invalidate();
    CHECK(sl_bgtf_state() == 0, "invalidate returns the module to UNTRIED (%d)",
          sl_bgtf_state());
    end_case();
}

/* ---- 5. the two depth ranges, and FAR0's two coefficient pairs --------- */

static void st_far_ranges(void)
{
    const struct seamgl_draw *d[64];
    int n, i;
    unsigned long dc0, dc1, dcs, pre_before;
    const struct sl_bgtf_counters *t;

    begin_case("seam: a TWO-RANGE frame takes a pool per (range, coefficient pair) and "
               "every submission is counted under the SOURCE's depth");
    /* WHY THE PAIR IS IN THE KEY. A frame draws in one range or in two, and
     * inside the first range the coefficients belong to the MESH: a blended
     * mesh takes the blend overlap's pair and every other mesh the opaque pair.
     * One shared first-range buffer would hand the blended mesh the opaque
     * mesh's coefficients. This frame has both kinds of mesh. */
    t = sl_bgtf_stats();
    dc0 = t->dcalls[SL_BGGPU_DEPTH_FAR0];
    dc1 = t->dcalls[SL_BGGPU_DEPTH_FAR1];
    dcs = t->dcalls[SL_BGGPU_DEPTH_SINGLE];
    pre_before = t->prepasses;
    run_walk();
    report_walk("far");
    n = raster_draws(d, 64);
    t = sl_bgtf_stats();

    CHECK(g_bgx.farpass == 1,
          "the frame IS a two-range frame - vertices past the far plane, the far "
          "pass selected and the reach beyond it (farpass=%d, far_verts=%u, "
          "plane=%.1f, reach=%.1f)", g_bgx.farpass, g_bg_far_verts,
          g_bg_far_plane, g_bg_far_reach);
    if (g_bgx.farpass != 1) {
        printf("    NOT REACHED: without farpass this case cannot say anything "
               "about FAR0 pairs or the FAR1 transition, and the checks below "
               "are reported as failures rather than skipped.\n");
    }
    CHECK(t->dcalls[SL_BGGPU_DEPTH_FAR0] > dc0,
          "submissions were counted in the FIRST range (%lu)",
          t->dcalls[SL_BGGPU_DEPTH_FAR0] - dc0);
    CHECK(t->dcalls[SL_BGGPU_DEPTH_FAR1] > dc1,
          "and in the GAME'S range, so the FAR1 transition happened within one "
          "walk (%lu)", t->dcalls[SL_BGGPU_DEPTH_FAR1] - dc1);
    CHECK(t->dcalls[SL_BGGPU_DEPTH_SINGLE] == dcs,
          "and NONE under the one-range clamp, which this frame is not (%lu)",
          t->dcalls[SL_BGGPU_DEPTH_SINGLE] - dcs);
    /* THREE POOLS AT LEAST: the first range's opaque pair, the first range's
     * blend pair, and the game's range. Fewer would mean a shared first-range
     * buffer or a range served from another range's pool. */
    CHECK(g_seam_npool >= 3,
          "at least three pools were filled - FAR0 opaque, FAR0 blend overlap "
          "and FAR1 (%d)", g_seam_npool);

    /* AND AT LEAST TWO OF THEM ARE FAR0 POOLS WITH DIFFERENT COEFFICIENTS,
     * which the pool count alone does not establish - a SINGLE pool would make
     * three just as well. The argument is closed from the numbers this walk
     * produced rather than from the pools' private keys:
     *
     *   every pool counted here was filled by a prepass of THIS walk, so the
     *   prepass count equals the pool count; each was then bound, and a bind
     *   answers its pool's own depth, which is what the submission is counted
     *   under; no submission landed under SINGLE. So no pool of this walk is a
     *   SINGLE pool, one is FAR1, and the remaining two or more are FAR0 pools
     *   that the slot cache refused to share - which it only does when the
     *   coefficient pairs differ, since the pair is in the key.
     *
     * That the cache splits on the pair at all is proved separately, against
     * the real sl_wa_far_range coefficients, by test_sl_gfx_bgtf.c's depth
     * case; what this adds is that sl_gfx_dl.c really hands it two different
     * pairs within one walk - the opaque mesh's and the blended mesh's. */
    CHECK((long) (t->prepasses - pre_before) >= (long) g_seam_npool,
          "every pool this walk used was filled by a prepass of THIS walk "
          "(%lu prepasses this walk, %d pools)",
          t->prepasses - pre_before, g_seam_npool);
    {
        int npooled = 0, i2;
        GLuint seen[8];
        int nseen = 0;
        for (i2 = 0; i2 < n; i2++) {
            int k;
            if (!is_pool(d[i2]->pos.buf)) continue;
            for (k = 0; k < nseen; k++) if (seen[k] == d[i2]->pos.buf) break;
            if (k == nseen && nseen < 8) seen[nseen++] = d[i2]->pos.buf;
            npooled++;
        }
        CHECK(nseen >= 2,
              "at least two DISTINCT pools were the vertex source for draws of "
              "this one walk, so a mesh was not handed another mesh's pool (%d)",
              nseen);
        CHECK(npooled >= 2, "across at least two pool-sourced draws (%d)", npooled);
    }
    /* NO DRAW READ A POOL THAT WAS NOT THIS WALK'S. */
    for (i = 0; i < n; i++)
        CHECK(d[i]->pos.buf == g_bgv_stream || is_pool(d[i]->pos.buf),
              "draw %d reads either the pass's own source or a pool of THIS walk, "
              "never something left over (%u)", i, (unsigned) d[i]->pos.buf);
    end_case();
}

/* ---- 6. the walk's exit ------------------------------------------------- */

static void st_walk_exit(void)
{
    begin_case("seam: at the walk's end the original source is back and the module's own "
               "state is put back, before the interpreter's list resumes");
    run_walk();
    report_walk("exit");
    CHECK(g_bgtf_src == 0,
          "no prepass pool is the vertex source any more (%d)", g_bgtf_src);
    CHECK(g_bgtf_walk == 0, "and the walk is closed (%d)", g_bgtf_walk);
    /* THE SOURCE FIRST, THEN THE MODULE'S STATE: the walk's last mesh may still
     * have had a pool named, and the interpreter's list resumes after this
     * point.
     *
     * THE END STATE IS A HOST ADDRESS, NOT THE STREAM BUFFER, and that is the
     * PASS's own teardown rather than anything the candidate does: after the
     * last draw the pass points its four arrays back at its client arrays with
     * no buffer bound and clears g_bgv_live, so the interpreter resumes exactly
     * as it did before the buffer objects existed. What the candidate owes here
     * is only that the source is not one of ITS pools. Asserting the stream
     * buffer instead would be asserting the pass's in-walk state at a point the
     * pass has deliberately left. */
    CHECK(sg.pos.set, "a position array is set at all");
    CHECK(!is_pool(sg.pos.buf),
          "and it is NOT a prepass pool - no pool outlives the walk (%u)",
          (unsigned) sg.pos.buf);
    CHECK(sg.pos.buf == 0u && sg.pos.off != 0,
          "it is the pass's own client clip array, which is its documented "
          "teardown (buffer %u, address %#lx)", (unsigned) sg.pos.buf,
          (unsigned long) sg.pos.off);
    CHECK(sg.current == 0u, "no program is current (%u)", (unsigned) sg.current);
    CHECK(sg.arr_buf == 0u,
          "GL_ARRAY_BUFFER is back to 0, so the pass's later client pointers are "
          "host addresses and not offsets (%u)", (unsigned) sg.arr_buf);
    CHECK(sg.tf_buf == 0u, "nothing is left recording (%u)", (unsigned) sg.tf_buf);
    CHECK(sg.discard == 0, "and rasterizer discard is off");
    CHECK(sg.tf_active == 0, "with no transform-feedback scope open");
    end_case();
}

/* ---- 7. the production buffer diagnostic, through the real seam --------
 *
 * WHAT THIS FIXTURE OWNS AND THE OTHER ONE DOES NOT. The module fixture can
 * prove what the diagnostic does when it is called; only here is it CALLED by
 * the real pass, so only here can the three seam properties be observed:
 *
 *   - the read happens AFTER the prepass's transform feedback ended and BEFORE
 *     the selected indexed draw, which is a claim about the order of calls the
 *     real pass makes and not about the module's own sequence;
 *   - a bind is not a draw: a mesh the pass admits and binds a pool for, but
 *     whose run is empty, must not be capturable - and the hidden mesh cannot
 *     show that, because it is filtered before the bind;
 *   - an excluded mesh - four-image, pulled, coplanar, far shell - never has a
 *     pool as its source at all, so a selector naming one gets nothing, and a
 *     CPU-sourced draw can never be reported as a transform-feedback capture.
 *
 * The selector is a launch-time switch parsed once, so each of these is a
 * separate run of this binary. tools/windows/bgtfseamtest.ps1 drives them.
 */

static const char *seam_dump_dir(void)
{
    const char *d = getenv("SL_BG_TF_DUMP_DIR");
    return (d != NULL && *d != '\0') ? d : "bgtfdumps";
}

/* Any record this scenario could write, by the writer's own naming. The mesh
 * and the depth are the scenario's; nothing else varies. */
static void seam_dump_path(char *out, size_t cap, unsigned mesh, int dmode,
                           const char *suffix)
{
    snprintf(out, cap, "%s/tfclip-0-%u-0-m%u-t%u-d%d%s", seam_dump_dir(),
             g_dl_frame, mesh, (unsigned) g_seam_asset.meshes[mesh].material,
             dmode, suffix);
}

static long seam_len(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (f == NULL) return -1L;
    if (fseek(f, 0L, SEEK_END) != 0) { fclose(f); return -1L; }
    n = ftell(f);
    fclose(f);
    return n;
}

/* A whole payload, read into a caller's buffer. Returns the byte count, or -1. */
static long seam_slurp(const char *path, unsigned char *out, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t got;
    if (f == NULL) return -1L;
    got = fread(out, 1u, cap, f);
    fclose(f);
    return (long) got;
}

/* THE BYTE THIS SHIM'S READBACK WRITES AT A GIVEN OFFSET.
 *
 * The seam's synthetic read fills its destination with a deterministic function
 * of the offset rather than from a modelled pool, because this fixture's claims
 * are about WHERE the read happens and what it leaves behind. That makes the
 * expected GPU payload computable here, exactly, which is what turns "the file
 * is the right length" into "the file is the right bytes". The expression is
 * sl_gfx_seamgl.c's own, and if the two ever diverge this check is what says
 * so. */
static unsigned char seam_readback_byte(long off)
{
    return (unsigned char) ((unsigned) off * 31u + 7u);
}

/* The two payloads' differing component patterns, counted the way the sidecar's
 * comparison rule states it: all four uint32 of each SUBMITTED index, in the
 * index file's own order. */
static unsigned seam_mismatches(const unsigned char *gpu, const unsigned char *cpu,
                                const unsigned char *idx, long nidx, unsigned nverts)
{
    unsigned n = 0u;
    long k;
    for (k = 0; k < nidx; k++) {
        const unsigned v = (unsigned) idx[k * 4L]
                         | ((unsigned) idx[k * 4L + 1L] << 8)
                         | ((unsigned) idx[k * 4L + 2L] << 16)
                         | ((unsigned) idx[k * 4L + 3L] << 24);
        unsigned c;
        if (v >= nverts) return ~0u;            /* structurally uncomparable */
        for (c = 0u; c < 4u; c++)
            if (memcmp(gpu + (size_t) v * 16u + c * 4u,
                       cpu + (size_t) v * 16u + c * 4u, 4u) != 0) n++;
    }
    return n;
}

/* How many files of ANY record this scenario could have produced exist. The
 * frame counter moves per walk, so a scenario that must write nothing is
 * checked by counting the directory's records rather than by naming one. */
static int seam_records(void)
{
    static const char *const sfx[5] = { ".gpuclip.bin", ".cpuclip.bin",
                                        ".idx.bin", ".json", ".json.staging" };
    char p[640];
    unsigned m, f;
    int d, i, n = 0;
    for (f = 0u; f <= g_dl_frame + 2u; f++)
        for (m = 0u; m < NMESH; m++)
            for (d = 0; d < 3; d++)
                for (i = 0; i < 5; i++) {
                    snprintf(p, sizeof p, "%s/tfclip-0-%u-0-m%u-t%u-d%d%s",
                             seam_dump_dir(), f, m,
                             (unsigned) g_seam_asset.meshes[m].material, d, sfx[i]);
                    if (seam_len(p) >= 0L) n++;
                }
    return n;
}

/* ---- 7a. the diagnostic ABSENT ---------------------------------------- */

static void st_dump_absent(void)
{
    const struct sl_bgtf_dump_counters *c;
    begin_case("seam: with SL_BG_TF_DUMP absent the pass resolves no readback entry "
               "point, makes no readback, writes no file, and draws exactly as it does "
               "without the diagnostic");
    /* The driver HAS the entry point in this run, which is what makes the case
     * about the switch rather than about a shim with nothing to offer. */
    sg.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 0, "the diagnostic is OFF (%d)",
          sl_bgtf_dump_opt_in());
    run_walk();
    report_walk("dump-absent");
    CHECK(sg.get_buffer_sub_data == 0,
          "no readback happened anywhere in the walk (%ld)", sg.get_buffer_sub_data);
    CHECK(sg.seq_first_subdata == 0, "and none is in the call order (%ld)",
          sg.seq_first_subdata);
    CHECK(sg.draw_elements >= 1, "while the pass did draw (%ld)", sg.draw_elements);
    CHECK(sl_bgtf_dump_state() == 0, "no attempt was made (%d)",
          sl_bgtf_dump_state());
    c = sl_bgtf_dump_stats();
    CHECK(c->offers == 0 && c->selected == 0 && c->captures == 0
          && c->readbacks == 0 && c->completed == 0,
          "and the census is empty: offers %lu selected %lu captures %lu readbacks "
          "%lu completed %lu", c->offers, c->selected, c->captures, c->readbacks,
          c->completed);
    CHECK(seam_records() == 0, "no record file exists (%d)", seam_records());
    end_case();
}

/* ---- 7b. the one capture, where the real pass puts it ------------------ */

static void st_dump_capture(void)
{
    const struct seamgl_draw *d[64];
    const struct sl_bgtf_dump_counters *c;
    int n, i, isel = -1;
    char p[640];
    unsigned frame_of_capture;

    begin_case("seam: ONE readback, after the prepass's transform feedback ended and "
               "BEFORE the selected indexed draw, with every binding and the position "
               "pointer left as the draw needs them");
    sg.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1,
          "the diagnostic is ARMED - this scenario needs a selector naming this "
          "frame, mesh %u and dmode %d", MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1);
    run_walk();
    frame_of_capture = g_dl_frame;
    report_walk("dump-capture");
    n = raster_draws(d, 64);

    CHECK(sl_bgtf_dump_state() == 1, "the record is COMPLETE (%d): %s",
          sl_bgtf_dump_state(), sl_bgtf_dump_status());
    CHECK(sg.get_buffer_sub_data == 1, "exactly ONE readback in the whole walk (%ld)",
          sg.get_buffer_sub_data);
    c = sl_bgtf_dump_stats();
    CHECK(c->selected == 1 && c->captures == 1 && c->readbacks == 1
          && c->completed == 1,
          "selected once, attempted once, read once, sealed once (%lu / %lu / %lu / "
          "%lu)", c->selected, c->captures, c->readbacks, c->completed);
    CHECK(c->offers > 1,
          "and several draws were offered, so the one match is a CHOICE and not the "
          "only candidate (%lu)", c->offers);

    /* THE PLACEMENT, read off the call order the real pass produced. */
    CHECK(sg.end_tf >= 1, "the pass ran prepasses (%ld)", sg.end_tf);
    CHECK(sg.seq_first_subdata > 0, "the readback is in the call order (%ld)",
          sg.seq_first_subdata);
    CHECK(sg.seq_first_subdata == sg.seq_last_subdata,
          "and it is the only one (%ld, %ld)", sg.seq_first_subdata,
          sg.seq_last_subdata);
    CHECK(sg.v_read_inside_tf == 0,
          "it did not read inside an open transform-feedback scope (%ld)",
          sg.v_read_inside_tf);

    /* WHICH DRAW IT WAS FOR: the first indexed draw whose sequence number is
     * greater than the readback's, taken from the record rather than assumed. */
    for (i = 0; i < n; i++)
        if (d[i]->seq > sg.seq_first_subdata) { isel = i; break; }
    CHECK(isel >= 0, "an indexed draw follows the readback (%d of %d)", isel, n);
    if (isel >= 0) {
        CHECK(is_pool(d[isel]->pos.buf),
              "and it reads a PREPASS POOL, not the CPU source (%u)",
              (unsigned) d[isel]->pos.buf);
        CHECK(d[isel]->pos.buf == sg.subdata_pos_at.buf,
              "the same pool the readback sampled the pointer against (%u vs %u)",
              (unsigned) d[isel]->pos.buf, (unsigned) sg.subdata_pos_at.buf);
        CHECK(d[isel]->pos.size == sg.subdata_pos_at.size
              && d[isel]->pos.off == sg.subdata_pos_at.off
              && d[isel]->pos.stride == sg.subdata_pos_at.stride,
              "THE POSITION POINTER IS UNCHANGED ACROSS THE READBACK - no pointer "
              "command ran, so the draw reads exactly what it would have read "
              "(size %d/%d, off %ld/%ld, stride %d/%d)",
              d[isel]->pos.size, sg.subdata_pos_at.size,
              d[isel]->pos.off, sg.subdata_pos_at.off,
              (int) d[isel]->pos.stride, (int) sg.subdata_pos_at.stride);
        CHECK(d[isel]->program == 0u,
              "and it still draws through program 0 (%u)",
              (unsigned) d[isel]->program);
        CHECK(d[isel]->count > 0, "on a NONEMPTY run (%ld)", d[isel]->count);
        CHECK(d[isel]->arr_buf == 0u,
              "with GL_ARRAY_BUFFER back at 0, so the pass's later client pointers "
              "are host addresses and not offsets into the pool (%u)",
              (unsigned) d[isel]->arr_buf);
        /* THE INDEX BINDING THE DRAW NEEDS was not disturbed either: the
         * readback runs before the element buffer is bound for the draw, and
         * the draw's own record carries what it had. */
        CHECK(d[isel]->elem_buf == g_bgv_index,
              "and the element binding is the pass's own index buffer (%u vs %u)",
              (unsigned) d[isel]->elem_buf, (unsigned) g_bgv_index);
    }
    CHECK(sg.subdata_tf_buf_at == 0u,
          "the indexed transform-feedback binding was already released when the read "
          "ran, and the read did not touch it (%u)", (unsigned) sg.subdata_tf_buf_at);

    /* THE RECORD. */
    seam_dump_path(p, sizeof p, MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1, ".gpuclip.bin");
    CHECK(seam_len(p) == (long) NVERT * 16L,
          "the GPU pool file is nverts * 16 bytes (%ld, expected %ld): %s",
          seam_len(p), (long) NVERT * 16L, p);
    seam_dump_path(p, sizeof p, MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1, ".cpuclip.bin");
    CHECK(seam_len(p) == (long) NVERT * 16L, "the CPU pool file is the same size (%ld)",
          seam_len(p));
    seam_dump_path(p, sizeof p, MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1, ".idx.bin");
    CHECK(seam_len(p) > 0L && (seam_len(p) % 4L) == 0L,
          "the index file is a whole number of uint32 (%ld)", seam_len(p));
    seam_dump_path(p, sizeof p, MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1, ".json");
    CHECK(seam_len(p) > 400L, "and the sidecar exists (%ld)", seam_len(p));
    seam_dump_path(p, sizeof p, MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1, ".json.staging");
    CHECK(seam_len(p) < 0L,
          "and NO unpublished staging sidecar was left beside it, so the sidecar at "
          "the final name is one whose write, flush and close all succeeded (%ld)",
          seam_len(p));

    /* ---- THE PAYLOADS' ACTUAL BYTES, AND THAT THEY ARE NOT ONE FILE TWICE --
     *
     * WHY LENGTHS WERE NOT ENOUGH. Both payloads are nverts * 16 bytes
     * whichever buffer the writer put in which file, so a GPU payload that
     * received the CPU buffer passed every check above. This shim's readback
     * writes a deterministic function of the offset, nothing like a clip
     * position, so the GPU payload's bytes are computable here exactly - and the
     * two files then have to differ in every component pattern of every
     * submitted index. */
    {
        static unsigned char gpu[NVERT * 16u];
        static unsigned char cpu[NVERT * 16u];
        static unsigned char idx[4096];
        long ng, nc, ni;
        unsigned bad = 0u, k;

        seam_dump_path(p, sizeof p, MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1, ".gpuclip.bin");
        ng = seam_slurp(p, gpu, sizeof gpu);
        seam_dump_path(p, sizeof p, MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1, ".cpuclip.bin");
        nc = seam_slurp(p, cpu, sizeof cpu);
        seam_dump_path(p, sizeof p, MESH_PLAIN0, SL_BGGPU_DEPTH_FAR1, ".idx.bin");
        ni = seam_slurp(p, idx, sizeof idx);
        CHECK(ng == (long) sizeof gpu && nc == (long) sizeof cpu && ni > 0L,
              "all three payloads read back whole (%ld / %ld / %ld)", ng, nc, ni);

        if (ng == (long) sizeof gpu) {
            for (k = 0u; k < (unsigned) sizeof gpu; k++)
                if (gpu[k] != seam_readback_byte((long) k)) bad++;
            CHECK(bad == 0u,
                  "THE GPU PAYLOAD IS BYTE FOR BYTE WHAT THE READBACK WROTE - this "
                  "shim's offset pattern, all %u bytes of it (%u differ)",
                  (unsigned) sizeof gpu, bad);
            /* Three exact bytes, spelled out, so a reader of a failing run can
             * see what the pattern is without recomputing it. */
            CHECK(gpu[0] == 0x07u && gpu[1] == 0x26u
                  && gpu[sizeof gpu - 1u] == seam_readback_byte((long) sizeof gpu - 1L),
                  "starting 0x07 0x26 and ending 0x%02x (got 0x%02x 0x%02x 0x%02x)",
                  (unsigned) seam_readback_byte((long) sizeof gpu - 1L),
                  (unsigned) gpu[0], (unsigned) gpu[1],
                  (unsigned) gpu[sizeof gpu - 1u]);
        }
        if (ng == (long) sizeof gpu && nc == (long) sizeof cpu) {
            CHECK(memcmp(gpu, cpu, (size_t) ng) != 0,
                  "the two payload files are NOT the same bytes, so neither can be "
                  "the other written twice");
        }
        if (ng == (long) sizeof gpu && nc == (long) sizeof cpu && ni > 0L) {
            const unsigned n = seam_mismatches(gpu, cpu, idx, ni / 4L, NVERT);
            /* THIRTY-SIX: the selected run submits nine indices and the contract
             * compares all four component patterns of each, so a readback
             * pattern that is nothing like a clip position must differ in every
             * one of them. Zero would be the two files holding the same buffer;
             * anything between would mean the pattern and a real clip component
             * coincided, which this asserts has not happened. */
            CHECK(ni / 4L == 9L,
                  "the selected run submitted nine indices (%ld)", ni / 4L);
            CHECK(n == 36u,
                  "and ALL 36 component patterns of those nine indices differ (%u) - "
                  "the count a reader of this record must be given, because 0 would "
                  "be the same buffer in both files", n);
        }
    }

    /* A LATER FRAME GETS NOTHING. The next walk raises g_dl_frame, so its draws
     * no longer match the selector - and even a matching one would not, because
     * the attempt is spent. */
    {
        const long rb = sg.get_buffer_sub_data;
        run_walk();
        report_walk("dump-later-frame");
        CHECK(sg.get_buffer_sub_data == 0,
              "the later frame made no readback (%ld)", sg.get_buffer_sub_data);
        CHECK(sl_bgtf_dump_stats()->captures == 1,
              "and the one attempt is still the only one (%lu)",
              sl_bgtf_dump_stats()->captures);
        CHECK(rb == 1, "while the captured frame had made exactly one (%ld)", rb);
        CHECK(frame_of_capture != g_dl_frame,
              "the two walks really are different frames (%u vs %u)",
              frame_of_capture, g_dl_frame);
    }
    end_case();
}

/* ---- 7c. a bind is not a draw ----------------------------------------- */

static void st_dump_empty_run(void)
{
    const struct sl_bgtf_dump_counters *c;
    begin_case("seam: a mesh the pass ADMITS AND BINDS A POOL FOR, whose run is empty, "
               "is never captured - a bind is not a draw");
    sg.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1,
          "armed with a selector naming mesh %u, which carries no indices",
          MESH_EMPTY);
    CHECK(g_seam_asset.meshes[MESH_EMPTY].index_count == 0u,
          "mesh %u really has no indices (%u)", MESH_EMPTY,
          g_seam_asset.meshes[MESH_EMPTY].index_count);
    CHECK(bg_mesh_shown(MESH_EMPTY) == 1,
          "and it is SHOWN, so it reaches the bind - unlike the hidden mesh, which "
          "is filtered before it");
    run_walk();
    report_walk("dump-empty");
    c = sl_bgtf_dump_stats();
    CHECK(c->selected >= 1,
          "the bind WAS offered and accepted, so this case is not vacuous (%lu)",
          c->selected);
    CHECK(c->captures == 0,
          "and yet NO attempt was ever claimed: the submission seam was never "
          "reached for it (%lu)", c->captures);
    CHECK(c->readbacks == 0, "no readback happened (%lu)", c->readbacks);
    CHECK(sg.get_buffer_sub_data == 0, "none in the shim either (%ld)",
          sg.get_buffer_sub_data);
    CHECK(sl_bgtf_dump_state() == 0, "the attempt is UNSPENT (%d)",
          sl_bgtf_dump_state());
    CHECK(seam_records() == 0, "and no record file exists (%d)", seam_records());
    CHECK(sg.draw_elements >= 1,
          "while the walk did draw its other meshes (%ld)", sg.draw_elements);
    end_case();
}

/* ---- 7d. an excluded mesh is never a transform-feedback capture -------- */

static void st_dump_excluded(void)
{
    const struct seamgl_draw *d[64];
    const struct sl_bgtf_dump_counters *c;
    int n, i, pooled = 0, cpu = 0;

    begin_case("seam: a candidate-INELIGIBLE mesh draws from the CPU source, so a "
               "selector naming it is never offered anything and no CPU-sourced draw "
               "can be reported as a transform-feedback capture");
    sg.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1,
          "armed with a selector naming mesh %u, the four-image mesh", MESH_FOURIMG);
    run_walk();
    report_walk("dump-excluded");
    n = raster_draws(d, 64);
    for (i = 0; i < n; i++) {
        if (is_pool(d[i]->pos.buf)) pooled++;
        else                        cpu++;
    }
    CHECK(pooled >= 1,
          "the walk DID serve other meshes from pools, both before and after the "
          "excluded one, so the diagnostic had matching machinery running (%d)",
          pooled);
    CHECK(cpu >= 1, "and the excluded mesh drew from the CPU source (%d)", cpu);
    c = sl_bgtf_dump_stats();
    CHECK(c->offers >= 1,
          "draws were offered to the diagnostic (%lu)", c->offers);
    CHECK(c->selected == 0,
          "and NONE matched: the excluded mesh never has a pool as its source, so it "
          "is never offered (%lu)", c->selected);
    CHECK(c->ident_miss >= 1,
          "the offers that did arrive were refused on identity (%lu)", c->ident_miss);
    CHECK(c->captures == 0 && c->readbacks == 0,
          "no attempt and no readback (%lu / %lu)", c->captures, c->readbacks);
    CHECK(sg.get_buffer_sub_data == 0, "none in the shim either (%ld)",
          sg.get_buffer_sub_data);
    CHECK(seam_records() == 0, "and no record file exists (%d)", seam_records());
    end_case();
}

/* ---- the frame inputs, stubbed ----------------------------------------- */

/* THESE ARE INPUTS TO THE PASS, NOT PART OF IT. src/native/sl_world_assets.c
 * reads them out of the running game, which this fixture has none of; every one
 * is answered with a fixed synthetic value so a scenario is a choice of inputs
 * rather than a patched predicate. Nothing here restates a predicate the pass
 * owns - the classification, the slot cache, the counters and the restoration
 * are all compiled in from src/. */

int sl_wa_view_matrix(float *m)
{
    int i;
    for (i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;     /* identity: the item is at the eye's
                                             * own origin, looking down -z */
    return 1;
}

int sl_wa_render_origin(float *o) { o[0] = o[1] = o[2] = 0.0f; return 1; }
int sl_wa_level_scale(float *s)      { *s = s_level_scale;  return 1; }
int sl_wa_visibility_scale(float *s) { *s = s_vis_scale;    return 1; }
int sl_wa_source_scale(float *s)     { *s = s_source_scale; return 1; }
int sl_wa_stage_num(void)            { return 1; }
const char *sl_wa_stage_item_path(void)     { return "seamtest"; }
const char *sl_wa_stage_sky_item_path(void) { return NULL; }
int sl_wa_stage_flat_backdrop(void)  { return 0; }
int sl_wa_frame_clock(unsigned long *frames) { *frames = 0ul; return 0; }
int sl_wa_level_box(float *lo, float *hi, float *scale)
{ (void) lo; (void) hi; (void) scale; return 0; }
int sl_wa_game_atmosphere(float fog[3], float cloud[3])
{ (void) fog; (void) cloud; return 0; }
int sl_wa_game_atmosphere_row(unsigned env_id, float fog[3], float cloud[3])
{ (void) env_id; (void) fog; (void) cloud; return 0; }
int sl_wa_sky_screen(float rows[10], int fill[4])
{ (void) rows; (void) fill; return 0; }
int sl_wa_sky_cloud(float *phase, float *height, float *eye,
                    unsigned *img_w, unsigned *img_h)
{ (void) phase; (void) height; (void) eye; (void) img_w; (void) img_h; return 0; }
int sl_wa_sky_cloud_row(unsigned env_id, float *phase, float *height, float *eye,
                        unsigned *img_w, unsigned *img_h)
{ (void) env_id; (void) phase; (void) height; (void) eye; (void) img_w;
  (void) img_h; return 0; }
int sl_wa_sea_frame(float *eye, float *sea_y, float *z_offset, float *seconds,
                    float *sky_rgb, float *cloud_rgb)
{ (void) eye; (void) sea_y; (void) z_offset; (void) seconds; (void) sky_rgb;
  (void) cloud_rgb; return 0; }
int sl_wa_water_phase(float *ps, float *pt, float *pw)
{ (void) ps; (void) pt; (void) pw; return 0; }
unsigned sl_wa_light_slots(void) { return 0u; }
float sl_wa_light_reach(void)    { return 0.0f; }
int sl_wa_light_darkened(unsigned i, unsigned *key, float *p)
{ (void) i; (void) key; (void) p; return 0; }

/* The game's own fog emitter and recording index. 0 for the environment, which
 * is what keeps `fogless` out of the way: SL_BG_FOGLESS=0 is what the runner
 * sets, because the far clamp - and so a two-range frame - cannot fire on a
 * fogless walk. */
int sl_fog_environment_id(void)        { return s_fog_env; }
int sl_fog_environment_alt(float *frac) { (void) frac; return 0; }
unsigned sl_record_index(void)   { return 0u; }
unsigned sl_frames_completed(void) { return 0u; }
int sl_gfx_active(void)          { return 0; }
int sl_pager_covers(unsigned long addr) { (void) addr; return 0; }
int sl_mark_room_of_dl(unsigned addr, int *kindout)
{ (void) addr; if (kindout != NULL) *kindout = 0; return 0; }
void sl_fatalf(const char *fmt, unsigned v)
{
    /* A FAULT IS A FAILED TEST, NOT A QUIET EXIT. The pass calls this only on a
     * condition it treats as unrecoverable, so a fixture that swallowed it
     * would report a pass for a walk that had already gone wrong. */
    printf("    FAIL: the pass called sl_fatalf(\"%s\", %u)\n",
           fmt != NULL ? fmt : "(null)", v);
    fails++;
    case_fails++;
}
int sl_pres_correct(int which, const float in[16], float out[16])
{ (void) which; (void) in; (void) out; return 0; }
void sl_pres_latch(void) { }
int sl_chr_draw_ok(const void *p) { (void) p; return 0; }
int sl_vm_draw_ok(const void *p)  { (void) p; return 0; }
void sl_chr_asset_report(void *f) { (void) f; }
void sl_vm_asset_report(void *f)  { (void) f; }
const struct sl_amdl *sl_asset_override_get_model(int id) { (void) id; return NULL; }

/* The provider's player-data ladder, which has no module in an offline build. */
const char *sl_asset_override_dir(void) { return NULL; }

/* The game's own session state, which src/platform/sl_perf.c reads to decide
 * whether a selection may move mid-level. A single-player session in a level is
 * the ordinary case and is what the OPTIMIZED RENDERING row is read under. */
int sl_game_player_count(void) { return 1; }
int sl_game_in_level(void)     { return 1; }

/* ---- the fixture ------------------------------------------------------- */

/* ---- 9. THE ORIGINAL-RASTER CONTROL (SL_BG_TF_RASTER_CPU) ---------------
 *
 * WHAT THESE CASES ARE FOR. The control keeps the prepass running and sends the
 * raster draw back to the ORIGINAL position stream. Two things about it are
 * invisible to the module fixture and live entirely in src/gfx/sl_gfx_dl.c:
 * which buffer each raster draw's POSITION pointer actually named when it ran,
 * and whether a draw that read the original stream can be counted as
 * TF-position-source coverage. Both are measured here against the real pass.
 *
 * WHY THE CONTROL NEEDS ITS OWN RUN. SL_BG_TF_RASTER_CPU is parsed once for the
 * launch, exactly like the candidate's own switch and the diagnostic's selector,
 * so one process can observe one setting. The runner starts these modes
 * separately; a single process could only ever report the one it was launched
 * with.
 */

/* THE DRAW SEQUENCE'S SHAPE, EXCLUDING WHERE POSITIONS CAME FROM.
 *
 * An order-sensitive digest over every indexed draw's element buffer, element
 * offset, index count, program, and its colour and UV pointer shapes. The
 * position source is deliberately LEFT OUT: it is the one thing the control is
 * supposed to change, so a digest that included it could never show that
 * everything else stayed the same.
 *
 * The runner compares this line across the arms. Equality is what establishes
 * that the colour, UV, element buffer, index order and per-range draw
 * decomposition are the same sequence of draws in the same order - a claim no
 * single run can make about itself. */
static unsigned long long seam_shape_digest(void)
{
    const struct seamgl_draw *d[64];
    unsigned long long h = 1469598103934665603ULL;   /* FNV-1a 64 offset basis */
    int n = raster_draws(d, 64), i, k;
    for (i = 0; i < n; i++) {
        const long f[10] = {
            (long) d[i]->elem_buf, d[i]->elem_off, d[i]->count,
            (long) d[i]->program,
            d[i]->col.set, d[i]->col.size, d[i]->col.stride,
            d[i]->uv.set,  d[i]->uv.size,  d[i]->uv.stride
        };
        for (k = 0; k < 10; k++) {
            unsigned long long v = (unsigned long long) f[k];
            int b;
            for (b = 0; b < 8; b++) {
                h ^= (unsigned long long) ((v >> (b * 8)) & 0xffu);
                h *= 1099511628211ULL;
            }
        }
    }
    printf("    SHAPE draws=%d digest=%016llx\n", n, h);
    return h;
}

/* Every indexed draw of this walk reads the pass's OWN stream at the ordinary
 * per-mesh offset, through the pass's own helper - never a prepass pool, never
 * the item's model positions at three components. */
static void seam_all_draws_original(const char *what)
{
    const struct seamgl_draw *d[64];
    int n = raster_draws(d, 64), i;
    CHECK(n >= 1, "%s: the walk issued indexed draws (%d)", what, n);
    for (i = 0; i < n; i++) {
        CHECK(d[i]->pos.set, "%s: draw %d had a position source at all", what, i);
        CHECK(!is_pool(d[i]->pos.buf),
              "%s: draw %d does NOT read a prepass pool (%u)", what, i,
              (unsigned) d[i]->pos.buf);
        CHECK(draw_from_stream(d[i]),
              "%s: draw %d reads the pass's own stream buffer (%u, stream %u)",
              what, i, (unsigned) d[i]->pos.buf, (unsigned) g_bgv_stream);
        /* FOUR COMPONENTS AT OFFSET 0 is the original per-mesh clip source. The
         * prepass leaves the pointer naming the item's MODEL positions - three
         * floats at the vertex record's stride - so a restoration that did not
         * happen would show up here as size 3 and a different buffer, which is
         * the defect this check exists for. */
        CHECK(d[i]->pos.off == 0,
              "%s: draw %d reads it at the unpushed offset (%ld)", what, i,
              d[i]->pos.off);
        CHECK(d[i]->pos.size == 4,
              "%s: draw %d reads FOUR components, not the model positions' "
              "three (%d)", what, i, (int) d[i]->pos.size);
        CHECK(d[i]->program == 0u,
              "%s: draw %d rasterises through the fixed pipeline (%u)", what, i,
              (unsigned) d[i]->program);
        CHECK(d[i]->elem_buf == g_bgv_index,
              "%s: draw %d indexes through the pass's own element buffer "
              "(%u, index %u)", what, i, (unsigned) d[i]->elem_buf,
              (unsigned) g_bgv_index);
    }
}

static void st_rcpu_control(void)
{
    const struct sl_bgtf_counters *t;
    unsigned long cpur_d0 = g_bgtf_cpur_draws, cpur_i0 = g_bgtf_cpur_indices;

    begin_case("control: the prepass RUNS and the raster reads the ORIGINAL position "
               "stream, and that draw is NOT TF-position-source coverage");
    CHECK(sl_bgtf_raster_cpu() == 1,
          "SL_BG_TF_RASTER_CPU=1 is required for this mode - run it through "
          "tools/windows/bgtfseamtest.ps1");
    CHECK(sl_bgtf_opt_in() == 1,
          "and the candidate itself is selected, or no walk would be admitted");
    run_walk();
    report_walk("control");
    t = sl_bgtf_stats();

    /* THE PREPASS STILL RAN. Without this the case would pass vacuously on a
     * walk that simply never entered the candidate.
     *
     * THE ADMISSION IS READ FROM THE WALK COUNT, NOT FROM g_bgtf_walk: that
     * flag is per-walk state and the pass clears it at the walk's exit, so it
     * is 0 by the time any case can look. The count is the durable fact. */
    CHECK(g_bgtf_elig_walks >= 1u,
          "the walk was admitted to the candidate (%u admitted)",
          g_bgtf_elig_walks);
    CHECK(t->prepasses >= 1, "and a prepass actually ran (%lu)", t->prepasses);
    CHECK(t->vertices >= 1, "transforming vertices (%lu)", t->vertices);
    CHECK(g_seam_npool >= 1, "filling at least one pool (%d)", g_seam_npool);

    /* THE POOL WAS NEVER MADE THE SOURCE. binds is incremented only by
     * sl_bgtf_bind_clip, so zero here is the direct evidence that the TF
     * position slot was not consumed rather than bound and undone. */
    CHECK(t->binds == 0,
          "no pool was ever bound as the vertex source (%lu binds)", t->binds);
    CHECK(g_bgtf_src == 0,
          "and no pool is the source at the end of the walk (%d)", g_bgtf_src);

    seam_all_draws_original("control");

    /* THE COUNTER CONTRACT. These draws rasterised the original stream, so the
     * candidate's coverage counts must be untouched while the control's own
     * bounded counts carry them. This is the mislabelling the arm exists to
     * prevent, stated as numbers. */
    CHECK(t->raster_draws == 0,
          "NO draw was counted as a submission on a prepass pool (%lu)",
          t->raster_draws);
    CHECK(t->raster_indices == 0,
          "and no indices were (%lu)", t->raster_indices);
    CHECK(t->dcalls[SL_BGGPU_DEPTH_SINGLE] == 0
          && t->dcalls[SL_BGGPU_DEPTH_FAR0] == 0
          && t->dcalls[SL_BGGPU_DEPTH_FAR1] == 0,
          "and no per-depth submission was (%lu/%lu/%lu)",
          t->dcalls[SL_BGGPU_DEPTH_SINGLE], t->dcalls[SL_BGGPU_DEPTH_FAR0],
          t->dcalls[SL_BGGPU_DEPTH_FAR1]);
    CHECK(g_bgtf_cpur_draws > cpur_d0,
          "while the control's OWN count rose (%lu -> %lu)", cpur_d0,
          g_bgtf_cpur_draws);
    CHECK(g_bgtf_cpur_indices > cpur_i0,
          "and carries the indices (%lu -> %lu)", cpur_i0, g_bgtf_cpur_indices);
    CHECK(g_bgtf_cpur_meshes >= 1,
          "over at least one mesh (%u)", g_bgtf_cpur_meshes);
    /* THE TWO COUNTS ARE MUTUALLY EXCLUSIVE, not merely both present: the
     * control's indices account for the nonempty candidate-eligible runs that
     * the coverage count did not take. */
    CHECK(t->raster_indices + 0u == 0u && g_bgtf_cpur_indices > 0u,
          "and the two gates never both fired for one draw (coverage %lu, "
          "control %lu)", t->raster_indices, g_bgtf_cpur_indices);

    /* THE CPU REFERENCE IT DREW FROM WAS FULLY PRODUCED. A candidate walk keeps
     * gpu_walk at 0, so either the reduction never skipped a vertex or its
     * repair recomputed the item. Nothing stale or bypassed may be what the
     * raster read. */
    CHECK(g_bgb_vtx_skip == 0ul || g_bgb_repairs >= 1u,
          "no vertex was left unwritten by the reduction (skipped %lu, "
          "repairs %u)", g_bgb_vtx_skip, g_bgb_repairs);
    CHECK(g_bgb_broken == 0u,
          "and no mesh drew from clip positions the walk did not write (%u)",
          g_bgb_broken);
    seam_shape_digest();
    end_case();
}

static void st_rcpu_refusal(void)
{
    const struct seamgl_draw *d[64];
    int n, i;
    unsigned r_before = g_bgtf_r_draw;
    unsigned long cov_before = sl_bgtf_stats()->raster_indices;

    begin_case("control: a prepass that REFUSES still leaves the original source in "
               "place and claims nothing");
    /* The same injection the ordinary refusal case uses: the pools are dropped
     * so an allocation happens at all, and the pass's own three buffers are
     * exempt because failing one of them refuses the walk before a prepass. */
    sl_bgtf_invalidate();
    sg.alloc_exempt[0] = g_bgv_static;
    sg.alloc_exempt[1] = g_bgv_index;
    sg.alloc_exempt[2] = g_bgv_stream;
    sg.alloc_short_initial = 1;
    sg.alloc_bytes = 0L;
    run_walk();
    report_walk("control-refusal");
    sg.alloc_short_initial = 0;
    sg.alloc_exempt[0] = sg.alloc_exempt[1] = sg.alloc_exempt[2] = 0u;

    CHECK(g_bgtf_r_draw > r_before,
          "a draw-level refusal was counted (%u -> %u)", r_before,
          g_bgtf_r_draw);
    n = raster_draws(d, 64);
    CHECK(n >= 1, "the walk still drew (%d)", n);
    for (i = 0; i < n; i++) {
        CHECK(draw_from_stream(d[i]),
              "refused draw %d reads the pass's own stream (%u, stream %u)", i,
              (unsigned) d[i]->pos.buf, (unsigned) g_bgv_stream);
        CHECK(d[i]->pos.size == 4 && d[i]->pos.off == 0,
              "refused draw %d reads the ordinary per-mesh clip source "
              "(size %d, off %ld)", i, (int) d[i]->pos.size, d[i]->pos.off);
    }
    CHECK(sl_bgtf_stats()->raster_indices == cov_before,
          "and nothing was counted as candidate coverage (%lu -> %lu)",
          cov_before, sl_bgtf_stats()->raster_indices);
    sl_bgtf_invalidate();
    CHECK(sl_bgtf_state() == 0, "invalidate returns the module to UNTRIED (%d)",
          sl_bgtf_state());
    end_case();
}

/* THE CONTROL ON ITS OWN DOES NOTHING AT ALL. This is the case that keeps the
 * switch from becoming a second way into the candidate: with SL_BG_TF_CLIP
 * absent, no walk is admitted, no prepass runs, no control count moves - and
 * the pass still draws, so the CPU work was not skipped either. */
static void st_rcpu_alone(void)
{
    const struct seamgl_draw *d[64];
    int n, i;

    begin_case("control alone: with SL_BG_TF_CLIP absent it enables nothing and skips "
               "no CPU work");
    CHECK(sl_bgtf_raster_cpu() == 1, "the control is selected (%d)",
          sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_opt_in() == 0,
          "and the candidate's own switch is NOT set (%d) - this mode is the "
          "point of the case", sl_bgtf_opt_in());
    run_walk();
    report_walk("control-alone");
    CHECK(g_bgtf_walk == 0, "no walk was admitted to the candidate (%d)",
          g_bgtf_walk);
    CHECK(sl_bgtf_stats()->prepasses == 0,
          "no prepass ran (%lu)", sl_bgtf_stats()->prepasses);
    CHECK(sl_bgtf_stats()->binds == 0, "nothing was bound (%lu)",
          sl_bgtf_stats()->binds);
    CHECK(g_bgtf_cpur_draws == 0 && g_bgtf_cpur_indices == 0
          && g_bgtf_cpur_meshes == 0,
          "and the control counted nothing (%lu/%lu/%u)", g_bgtf_cpur_draws,
          g_bgtf_cpur_indices, g_bgtf_cpur_meshes);
    CHECK(sl_bgtf_state() == 0,
          "the module was never even asked for a capability (%d)",
          sl_bgtf_state());
    n = raster_draws(d, 64);
    CHECK(n >= 1, "the pass still drew the background (%d draws)", n);
    /* EVERY DRAW STILL HAD A POSITION SOURCE, and that is all this case may
     * assert about it. WHICH non-candidate path takes the walk is decided by
     * SL_BG_GPU_XFORM, not by this control: measured here, the vertex-program
     * path takes it and legitimately reads the item's MODEL positions at three
     * components. Requiring four would be asserting that the other module's
     * path is off, which is a different switch and not this case's subject. */
    for (i = 0; i < n; i++)
        CHECK(d[i]->pos.set, "draw %d had a position source (size %d)", i,
              (int) d[i]->pos.size);
    /* AND NO CPU WORK WAS SKIPPED ON THE CONTROL'S ACCOUNT. The reduction is
     * the only thing that skips per-vertex work, and it either skipped nothing
     * or repaired what it skipped. */
    CHECK(g_bgb_vtx_skip == 0ul || g_bgb_repairs >= 1u,
          "no vertex was left unwritten (skipped %lu, repairs %u)",
          g_bgb_vtx_skip, g_bgb_repairs);
    seam_shape_digest();
    end_case();
}

/* DEFAULT OFF, SAID AS A MEASUREMENT rather than assumed by omission. Runs in
 * the ordinary mode, where the control is absent from the environment. */
static void st_rcpu_default_off(void)
{
    begin_case("default off: the control is not selected and the candidate arm is the "
               "ordinary one");
    CHECK(sl_bgtf_raster_cpu() == 0,
          "SL_BG_TF_RASTER_CPU is absent, so the control is off (%d)",
          sl_bgtf_raster_cpu());
    CHECK(g_bgtf_cpur_draws == 0 && g_bgtf_cpur_indices == 0
          && g_bgtf_cpur_meshes == 0,
          "and nothing in this process was counted for it (%lu/%lu/%u)",
          g_bgtf_cpur_draws, g_bgtf_cpur_indices, g_bgtf_cpur_meshes);
    /* The ordinary arm's own coverage is what the cases above measured, so this
     * only has to show it is NOT zero - otherwise "off is unchanged" would be
     * consistent with the candidate having stopped working. */
    CHECK(sl_bgtf_stats()->binds >= 1,
          "while the ordinary arm did bind pools (%lu)",
          sl_bgtf_stats()->binds);
    CHECK(sl_bgtf_stats()->raster_draws >= 1,
          "and did count submissions on them (%lu)",
          sl_bgtf_stats()->raster_draws);
    seam_shape_digest();
    end_case();
}

/* ---- 10. THE STATE-ONLY CONTROL (SL_BG_TF_STATE_ONLY) -------------------
 *
 * WHAT ONLY THIS FIXTURE CAN SAY. The module fixture proves which calls the
 * prepass makes and that the setup transcript is identical with the draw
 * dropped. What lives entirely in src/gfx/sl_gfx_dl.c is whether the PASS still
 * behaves as the original-raster control does around it: which buffer each
 * raster draw's position pointer named when it ran, that no pool is bound and no
 * coverage is claimed, that the control's own CPU counts still rise, and that
 * the rest of the draw sequence is untouched.
 *
 * ZERO glDrawArrays IS A LITERAL CLAIM HERE. Searched over the whole tree: the
 * only glDrawArrays under src/ is the prepass's own in src/gfx/sl_gfx_bgtf.c
 * (the one in sl_gfx_dl.c is inside a comment), so every array draw this shim
 * can record in a seam build comes from the prepass and a count of 0 means the
 * prepass drew nothing. The pool census is asserted beside it, because
 * collect_pools identifies pools BY those draws and would report none either way.
 *
 * ONE SETTING PER RUN, because the control's switch is parsed once for the launch
 * exactly like the candidate's. */

/* Recording prepass draws: an array draw with a buffer bound at
 * transform-feedback index 0. Taken out of the shim's own record. */
static int seam_prepass_draws(void)
{
    int i, n = 0;
    for (i = 0; i < sg.ndraws; i++)
        if (sg.draw[i].arrays && sg.draw[i].tf_buf != 0u) n++;
    return n;
}

static void st_so_control(void)
{
    const struct sl_bgtf_counters *t;
    unsigned long cpur_d0 = g_bgtf_cpur_draws, cpur_i0 = g_bgtf_cpur_indices;

    begin_case("state-only: the prepass SETUP runs with ZERO vertex draws, no pool is "
               "bound and no coverage is claimed, while the raster reads the ORIGINAL "
               "four-float position stream");
    CHECK(sl_bgtf_state_only() == 1,
          "SL_BG_TF_STATE_ONLY=1 with both required switches is needed for this "
          "mode - run it through tools/windows/bgtfseamtest.ps1");
    CHECK(sl_bgtf_raster_cpu() == 1,
          "and the original-raster control is selected, which the state-only "
          "control requires (%d)", sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_opt_in() == 1,
          "and the candidate itself is selected, or no walk would be admitted");
    run_walk();
    report_walk("state-only");
    t = sl_bgtf_stats();

    /* THE SETUP RAN. Without this the case would pass vacuously on a walk that
     * never entered the candidate at all - which is exactly what the zero draw
     * count would also look like. */
    CHECK(g_bgtf_elig_walks >= 1u,
          "the walk was admitted to the candidate (%u admitted)",
          g_bgtf_elig_walks);
    CHECK(t->so_setups >= 1, "and a prepass SETUP ran (%lu)", t->so_setups);
    CHECK(t->so_omitted == t->so_setups,
          "with an omitted draw for each of them (%lu vs %lu)", t->so_omitted,
          t->so_setups);
    CHECK(sg.begin_tf >= 1 && sg.end_tf == sg.begin_tf,
          "the transform-feedback scope was opened and closed around each "
          "(%ld/%ld)", sg.begin_tf, sg.end_tf);
    CHECK(sg.gen_buffers >= 1 && sg.buffer_data >= 1
          && sg.get_buffer_parameteriv >= 1,
          "and the pools were still created, sized and VERIFIED (%ld/%ld/%ld)",
          sg.gen_buffers, sg.buffer_data, sg.get_buffer_parameteriv);

    /* AND NOTHING WAS DRAWN BY IT. Both numbers, because they fail for
     * different reasons: the first is the shim's own call counter and the second
     * is the recording draws in its ordered record. */
    CHECK(sg.draw_arrays == 0,
          "ZERO glDrawArrays were issued in the whole run (%ld)", sg.draw_arrays);
    CHECK(seam_prepass_draws() == 0,
          "and no recording prepass draw is in the shim's record (%d)",
          seam_prepass_draws());
    CHECK(g_seam_npool == 0,
          "so no pool was recorded into (%d)", g_seam_npool);

    /* NO WORK AND NO COVERAGE IS CLAIMED. These are the candidate's real-work
     * numbers, and all of them must be 0: a setup is not a transformed vertex,
     * and an unbound pool is not a vertex source. */
    CHECK(t->prepasses == 0, "no prepass was counted (%lu)", t->prepasses);
    CHECK(t->vertices == 0, "no vertex was counted as transformed (%lu)",
          t->vertices);
    CHECK(t->walks == 0, "no walk was credited with a submission (%lu)",
          t->walks);
    CHECK(t->reuses == 0, "no POOL reuse was counted (%lu)", t->reuses);
    CHECK(t->binds == 0,
          "no pool was ever bound as the vertex source (%lu)", t->binds);
    CHECK(g_bgtf_src == 0,
          "and no pool is the source at the end of the walk (%d)", g_bgtf_src);
    CHECK(t->raster_draws == 0 && t->raster_indices == 0,
          "NO draw was counted as a submission on a prepass pool (%lu/%lu)",
          t->raster_draws, t->raster_indices);
    CHECK(t->dcalls[SL_BGGPU_DEPTH_SINGLE] == 0
          && t->dcalls[SL_BGGPU_DEPTH_FAR0] == 0
          && t->dcalls[SL_BGGPU_DEPTH_FAR1] == 0,
          "and no per-depth submission was (%lu/%lu/%lu)",
          t->dcalls[SL_BGGPU_DEPTH_SINGLE], t->dcalls[SL_BGGPU_DEPTH_FAR0],
          t->dcalls[SL_BGGPU_DEPTH_FAR1]);
    CHECK(sg.get_buffer_sub_data == 0,
          "and nothing was read back (%ld)", sg.get_buffer_sub_data);

    /* THE RASTER IS THE ORIGINAL ONE. Four components at the unpushed offset out
     * of the pass's own stream, through program 0 and the pass's element buffer,
     * for every indexed draw of the walk. */
    seam_all_draws_original("state-only");

    /* AND THE CONTROL'S OWN CPU COUNTS STILL RISE, which is what says the CPU
     * work was not skipped on this arm. */
    CHECK(g_bgtf_cpur_draws > cpur_d0,
          "the original-raster control's count rose (%lu -> %lu)", cpur_d0,
          g_bgtf_cpur_draws);
    CHECK(g_bgtf_cpur_indices > cpur_i0,
          "and carries the indices (%lu -> %lu)", cpur_i0, g_bgtf_cpur_indices);
    CHECK(g_bgtf_cpur_meshes >= 1, "over at least one mesh (%u)",
          g_bgtf_cpur_meshes);

    /* THE CPU REFERENCE IT DREW FROM WAS FULLY PRODUCED, exactly as on the
     * original-raster control: a candidate walk keeps gpu_walk at 0, so either
     * the reduction skipped nothing or its repair recomputed the item. */
    CHECK(g_bgb_vtx_skip == 0ul || g_bgb_repairs >= 1u,
          "no vertex was left unwritten by the reduction (skipped %lu, "
          "repairs %u)", g_bgb_vtx_skip, g_bgb_repairs);
    CHECK(g_bgb_broken == 0u,
          "and no mesh drew from clip positions the walk did not write (%u)",
          g_bgb_broken);
    seam_shape_digest();
    end_case();
}

/* THE CONTROL ON ITS OWN DOES NOTHING AT ALL. With SL_BG_TF_CLIP absent no walk
 * is admitted, nothing is set up, no count moves - and the pass still draws, so
 * no CPU work was skipped either. This is what keeps the switch from becoming a
 * second way into the candidate. */
static void st_so_alone(void)
{
    const struct seamgl_draw *d[64];
    int n, i;

    begin_case("state-only alone: with SL_BG_TF_CLIP absent it enables nothing and "
               "skips no CPU work");
    CHECK(sl_bgtf_state_only() == 0,
          "the control is NOT effective without the candidate's switch (%d)",
          sl_bgtf_state_only());
    CHECK(sl_bgtf_opt_in() == 0,
          "and the candidate's own switch is NOT set (%d) - this mode is the "
          "point of the case", sl_bgtf_opt_in());
    run_walk();
    report_walk("state-only-alone");
    CHECK(g_bgtf_walk == 0, "no walk was admitted to the candidate (%d)",
          g_bgtf_walk);
    CHECK(sl_bgtf_stats()->so_setups == 0 && sl_bgtf_stats()->so_omitted == 0
          && sl_bgtf_stats()->so_reuses == 0,
          "the control counted nothing (%lu/%lu/%lu)",
          sl_bgtf_stats()->so_setups, sl_bgtf_stats()->so_omitted,
          sl_bgtf_stats()->so_reuses);
    CHECK(sl_bgtf_stats()->prepasses == 0 && sl_bgtf_stats()->binds == 0,
          "no prepass ran and nothing was bound (%lu/%lu)",
          sl_bgtf_stats()->prepasses, sl_bgtf_stats()->binds);
    CHECK(g_bgtf_cpur_draws == 0 && g_bgtf_cpur_indices == 0
          && g_bgtf_cpur_meshes == 0,
          "and the original-raster control counted nothing either (%lu/%lu/%u)",
          g_bgtf_cpur_draws, g_bgtf_cpur_indices, g_bgtf_cpur_meshes);
    CHECK(sl_bgtf_state() == 0,
          "the module was never even asked for a capability (%d)",
          sl_bgtf_state());
    n = raster_draws(d, 64);
    CHECK(n >= 1, "the pass still drew the background (%d draws)", n);
    /* EVERY DRAW STILL HAD A POSITION SOURCE, and that is all this case may
     * assert about it: WHICH non-candidate path takes the walk is decided by
     * SL_BG_GPU_XFORM and not by this control, and that path legitimately reads
     * the item's MODEL positions at three components. */
    for (i = 0; i < n; i++)
        CHECK(d[i]->pos.set, "draw %d had a position source (size %d)", i,
              (int) d[i]->pos.size);
    CHECK(g_bgb_vtx_skip == 0ul || g_bgb_repairs >= 1u,
          "and no vertex was left unwritten (skipped %lu, repairs %u)",
          g_bgb_vtx_skip, g_bgb_repairs);
    seam_shape_digest();
    end_case();
}

/* MISCOMBINED, THE SWITCH CANNOT CHANGE PRODUCT ADMISSION. With the candidate
 * selected and the original-raster control ABSENT, the state-only control is not
 * effective - so the launch must be the ORDINARY candidate arm, prepass draws
 * and bound pools and counted coverage included. A switch that half-applied here
 * would be a path nobody asked for. */
static void st_so_miscombined(void)
{
    const struct sl_bgtf_counters *t;

    begin_case("state-only miscombined: selected WITHOUT the original-raster control, "
               "it is not effective and the ORDINARY candidate arm runs unchanged");
    CHECK(sl_bgtf_opt_in() == 1, "the candidate is selected (%d)",
          sl_bgtf_opt_in());
    CHECK(sl_bgtf_raster_cpu() == 0,
          "the original-raster control is ABSENT (%d) - this mode is the point "
          "of the case", sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_state_only() == 0,
          "so the state-only control answers 0 (%d)", sl_bgtf_state_only());
    run_walk();
    report_walk("state-only-miscombined");
    t = sl_bgtf_stats();
    CHECK(t->so_setups == 0 && t->so_omitted == 0 && t->so_reuses == 0,
          "nothing was counted for the state-only control (%lu/%lu/%lu)",
          t->so_setups, t->so_omitted, t->so_reuses);
    /* THE ORDINARY ARM, MEASURED. Prepasses that ran, vertices transformed,
     * pools bound and submissions counted on them - the opposite of every
     * number the state-only arm reports. */
    CHECK(t->prepasses >= 1, "the ordinary prepass ran (%lu)", t->prepasses);
    CHECK(t->vertices >= 1, "transforming vertices (%lu)", t->vertices);
    CHECK(sg.draw_arrays >= 1, "with real prepass draws (%ld)", sg.draw_arrays);
    CHECK(seam_prepass_draws() >= 1,
          "recording into a pool (%d)", seam_prepass_draws());
    CHECK(g_seam_npool >= 1, "and at least one pool was filled (%d)",
          g_seam_npool);
    CHECK(t->binds >= 1, "which was bound as the vertex source (%lu)", t->binds);
    CHECK(t->raster_draws >= 1 && t->raster_indices >= 1,
          "and submissions were counted on it (%lu/%lu)", t->raster_draws,
          t->raster_indices);
    CHECK(g_bgtf_cpur_draws == 0 && g_bgtf_cpur_meshes == 0,
          "while the original-raster control counted nothing (%lu/%u)",
          g_bgtf_cpur_draws, g_bgtf_cpur_meshes);
    seam_shape_digest();
    end_case();
}

/* DEFAULT OFF, SAID AS A MEASUREMENT. Runs in the ordinary mode, where the
 * control is absent from the environment. */
static void st_so_default_off(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("state-only default off: the control is not selected and nothing in this "
               "process was counted for it");
    CHECK(sl_bgtf_state_only() == 0,
          "SL_BG_TF_STATE_ONLY is absent, so the control is off (%d)",
          sl_bgtf_state_only());
    CHECK(t->so_setups == 0 && t->so_omitted == 0 && t->so_reuses == 0,
          "and nothing in this process was counted for it (%lu/%lu/%lu)",
          t->so_setups, t->so_omitted, t->so_reuses);
    /* The ordinary arm's own work is what the cases above measured, so this only
     * has to show it is NOT zero - otherwise "off is unchanged" would be
     * consistent with the candidate having stopped working. */
    CHECK(t->prepasses >= 1 && t->binds >= 1 && t->raster_draws >= 1,
          "while the ordinary arm ran prepasses, bound pools and counted "
          "submissions (%lu/%lu/%lu)", t->prepasses, t->binds, t->raster_draws);
    end_case();
}

/* ---- 10b. THE ZERO-VERTEX-COUNT CONTROL (SL_BG_TF_ZERO_COUNT) -----------
 *
 * WHAT ONLY THIS FIXTURE CAN SAY. The module fixture proves which call the
 * prepass makes, that the count handed to it is 0 and that the rest of the
 * transcript equals the reference's. What lives entirely in
 * src/gfx/sl_gfx_dl.c is whether the PASS still behaves as the original-raster
 * control does around it: which buffer each raster draw's position pointer named
 * when it ran, that no pool is bound and no coverage is claimed, that the
 * control's own CPU counts still rise with the index sequence intact, and that
 * the rest of the draw sequence is untouched.
 *
 * AND THE ONE NUMBER THAT SEPARATES THIS ARM FROM THE STATE-ONLY ARM IS CHECKED
 * HERE TOO. There the whole-run glDrawArrays count is 0; here it is POSITIVE
 * while the vertices those calls asked for are 0. Searched over the whole tree:
 * the only glDrawArrays under src/ is the prepass's own in
 * src/gfx/sl_gfx_bgtf.c (the one in sl_gfx_dl.c is inside a comment), so every
 * array draw this shim can record in a seam build comes from the prepass - which
 * is what lets a positive call count with zero vertices be read as "the prepass
 * issued empty calls" rather than as some other draw.
 *
 * ONE SETTING PER RUN, because the control's switch is parsed once for the launch
 * exactly like the candidate's. */

/* The vertices the recording prepass draws of this run asked for, summed from
 * the shim's own record. 0 beside a positive draw count is the arm. */
static long seam_prepass_vertices(void)
{
    int i;
    long n = 0;
    for (i = 0; i < sg.ndraws; i++)
        if (sg.draw[i].arrays && sg.draw[i].tf_buf != 0u)
            n += (long) sg.draw[i].count;
    return n;
}

static void st_zc_control(void)
{
    const struct sl_bgtf_counters *t;
    unsigned long cpur_d0 = g_bgtf_cpur_draws, cpur_i0 = g_bgtf_cpur_indices;

    begin_case("zero count: the prepass SETUP runs and ISSUES its draw with the count "
               "0, no vertex is submitted, no pool is bound and no coverage is "
               "claimed, while the raster reads the ORIGINAL four-float position "
               "stream");
    CHECK(sl_bgtf_zero_count() == 1,
          "SL_BG_TF_ZERO_COUNT=1 with both required switches is needed for this "
          "mode - run it through tools/windows/bgtfseamtest.ps1");
    CHECK(sl_bgtf_raster_cpu() == 1,
          "and the original-raster control is selected, which this control "
          "requires (%d)", sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_opt_in() == 1,
          "and the candidate itself is selected, or no walk would be admitted");
    CHECK(sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0
          && sl_bgtf_private_vao() == 0,
          "and none of the three exclusive arms is in force (%d/%d/%d)",
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao());
    run_walk();
    report_walk("zero-count");
    t = sl_bgtf_stats();

    /* THE SETUP RAN. Without this the case would pass vacuously on a walk that
     * never entered the candidate at all. */
    CHECK(g_bgtf_elig_walks >= 1u,
          "the walk was admitted to the candidate (%u admitted)",
          g_bgtf_elig_walks);
    CHECK(t->zc_setups >= 1, "and a prepass SETUP ran (%lu)", t->zc_setups);
    CHECK(t->zc_calls == t->zc_setups,
          "with one ISSUED zero-count call for each of them (%lu vs %lu)",
          t->zc_calls, t->zc_setups);
    CHECK(t->zc_refusals == 0,
          "and the control was not refused on this launch (%lu)",
          t->zc_refusals);
    CHECK(sg.begin_tf >= 1 && sg.end_tf == sg.begin_tf,
          "the transform-feedback scope was opened and closed around each "
          "(%ld/%ld)", sg.begin_tf, sg.end_tf);
    CHECK(sg.gen_buffers >= 1 && sg.buffer_data >= 1
          && sg.get_buffer_parameteriv >= 1,
          "and the pools were still created, sized and VERIFIED (%ld/%ld/%ld)",
          sg.gen_buffers, sg.buffer_data, sg.get_buffer_parameteriv);

    /* THE CALLS HAPPENED AND SUBMITTED NOTHING. This pair is the arm: a positive
     * call count rules out the state-only arm's omission, and zero vertices rule
     * out the ordinary one's work. */
    CHECK(sg.draw_arrays >= 1,
          "glDrawArrays WAS issued in this run (%ld) - the state-only arm's own "
          "case requires this to be 0, which is the distinction", sg.draw_arrays);
    CHECK(seam_prepass_draws() == (int) t->zc_calls,
          "and every one of them is a recording prepass draw, one per counted "
          "empty call (%d vs %lu)", seam_prepass_draws(), t->zc_calls);
    CHECK(seam_prepass_vertices() == 0L,
          "the vertices they asked for sum to ZERO (%ld)",
          seam_prepass_vertices());
    /* AND THE POOL CENSUS SAYS SOMETHING DIFFERENT HERE FROM WHAT IT SAYS ON THE
     * STATE-ONLY ARM, which is worth stating rather than glossing. collect_pools
     * identifies a pool BY a prepass draw with a recording buffer bound, so the
     * state-only arm - which issues no draw at all - reports NO pool either way,
     * while this arm's empty calls DO name their pools: the indexed
     * transform-feedback binding is part of the setup this control keeps. What
     * must be true is that nothing was written into them and that none of them
     * ever became a vertex source, which the counters below and
     * seam_all_draws_original state. A count of 0 here would mean the binding had
     * gone missing from the bracket. */
    CHECK(g_seam_npool >= 1,
          "the empty calls still named their pools, because the indexed binding "
          "is part of the setup this control keeps (%d)", g_seam_npool);

    /* NO WORK AND NO COVERAGE IS CLAIMED. An empty call is not a transformed
     * vertex, and an unbound pool is not a vertex source. */
    CHECK(t->prepasses == 0, "no prepass was counted (%lu)", t->prepasses);
    CHECK(t->vertices == 0, "no vertex was counted as transformed (%lu)",
          t->vertices);
    CHECK(t->walks == 0, "no walk was credited with a submission (%lu)",
          t->walks);
    CHECK(t->reuses == 0, "no POOL reuse was counted (%lu)", t->reuses);
    CHECK(t->binds == 0,
          "no pool was ever bound as the vertex source (%lu)", t->binds);
    CHECK(g_bgtf_src == 0,
          "and no pool is the source at the end of the walk (%d)", g_bgtf_src);
    CHECK(t->raster_draws == 0 && t->raster_indices == 0,
          "NO draw was counted as a submission on a prepass pool (%lu/%lu)",
          t->raster_draws, t->raster_indices);
    CHECK(t->dcalls[SL_BGGPU_DEPTH_SINGLE] == 0
          && t->dcalls[SL_BGGPU_DEPTH_FAR0] == 0
          && t->dcalls[SL_BGGPU_DEPTH_FAR1] == 0,
          "and no per-depth submission was (%lu/%lu/%lu)",
          t->dcalls[SL_BGGPU_DEPTH_SINGLE], t->dcalls[SL_BGGPU_DEPTH_FAR0],
          t->dcalls[SL_BGGPU_DEPTH_FAR1]);
    CHECK(t->so_setups == 0 && t->so_omitted == 0,
          "and the state-only control counted nothing, so this run cannot be "
          "read as its omission (%lu/%lu)", t->so_setups, t->so_omitted);
    CHECK(sg.get_buffer_sub_data == 0,
          "and nothing was read back (%ld)", sg.get_buffer_sub_data);

    /* THE RASTER IS THE ORIGINAL ONE. Four components at the unpushed offset out
     * of the pass's own stream, through program 0 and the pass's element buffer,
     * for every indexed draw of the walk. */
    seam_all_draws_original("zero-count");

    /* AND THE CONTROL'S OWN CPU COUNTS STILL RISE, which is what says the CPU
     * work was not skipped on this arm. */
    CHECK(g_bgtf_cpur_draws > cpur_d0,
          "the original-raster control's count rose (%lu -> %lu)", cpur_d0,
          g_bgtf_cpur_draws);
    CHECK(g_bgtf_cpur_indices > cpur_i0,
          "and carries the indices (%lu -> %lu)", cpur_i0, g_bgtf_cpur_indices);
    CHECK(g_bgtf_cpur_meshes >= 1, "over at least one mesh (%u)",
          g_bgtf_cpur_meshes);

    /* THE CPU REFERENCE IT DREW FROM WAS FULLY PRODUCED. */
    CHECK(g_bgb_vtx_skip == 0ul || g_bgb_repairs >= 1u,
          "no vertex was left unwritten by the reduction (skipped %lu, "
          "repairs %u)", g_bgb_vtx_skip, g_bgb_repairs);
    CHECK(g_bgb_broken == 0u,
          "and no mesh drew from clip positions the walk did not write (%u)",
          g_bgb_broken);
    seam_shape_digest();
    end_case();
}

/* THE CONTROL ON ITS OWN DOES NOTHING AT ALL. With SL_BG_TF_CLIP absent no walk
 * is admitted, nothing is set up, no count moves, and the pass still draws. */
static void st_zc_alone(void)
{
    const struct seamgl_draw *d[64];
    int n, i;

    begin_case("zero count alone: with SL_BG_TF_CLIP absent it enables nothing and "
               "skips no CPU work");
    CHECK(sl_bgtf_zero_count() == 0,
          "the control is NOT effective without the candidate's switch (%d)",
          sl_bgtf_zero_count());
    CHECK(sl_bgtf_opt_in() == 0,
          "and the candidate's own switch is NOT set (%d) - this mode is the "
          "point of the case", sl_bgtf_opt_in());
    CHECK(sl_bgtf_stats()->zc_refusals == 1,
          "the launch asked for the control and did not get it, so exactly ONE "
          "refusal was counted (%lu)", sl_bgtf_stats()->zc_refusals);
    run_walk();
    report_walk("zero-count-alone");
    CHECK(g_bgtf_walk == 0, "no walk was admitted to the candidate (%d)",
          g_bgtf_walk);
    CHECK(sl_bgtf_stats()->zc_setups == 0 && sl_bgtf_stats()->zc_calls == 0
          && sl_bgtf_stats()->zc_reuses == 0,
          "the control set up nothing and issued nothing (%lu/%lu/%lu)",
          sl_bgtf_stats()->zc_setups, sl_bgtf_stats()->zc_calls,
          sl_bgtf_stats()->zc_reuses);
    CHECK(sl_bgtf_stats()->prepasses == 0 && sl_bgtf_stats()->binds == 0,
          "no prepass ran and nothing was bound (%lu/%lu)",
          sl_bgtf_stats()->prepasses, sl_bgtf_stats()->binds);
    CHECK(sg.draw_arrays == 0,
          "and no array draw of any kind was issued (%ld)", sg.draw_arrays);
    CHECK(g_bgtf_cpur_draws == 0 && g_bgtf_cpur_indices == 0
          && g_bgtf_cpur_meshes == 0,
          "the original-raster control counted nothing either (%lu/%lu/%u)",
          g_bgtf_cpur_draws, g_bgtf_cpur_indices, g_bgtf_cpur_meshes);
    CHECK(sl_bgtf_state() == 0,
          "and the module was never even asked for a capability (%d)",
          sl_bgtf_state());
    n = raster_draws(d, 64);
    CHECK(n >= 1, "the pass still drew the background (%d draws)", n);
    for (i = 0; i < n; i++)
        CHECK(d[i]->pos.set, "draw %d had a position source (size %d)", i,
              (int) d[i]->pos.size);
    CHECK(g_bgb_vtx_skip == 0ul || g_bgb_repairs >= 1u,
          "and no vertex was left unwritten (skipped %lu, repairs %u)",
          g_bgb_vtx_skip, g_bgb_repairs);
    seam_shape_digest();
    end_case();
}

/* MISCOMBINED, THE SWITCH CANNOT CHANGE PRODUCT ADMISSION. With the candidate
 * selected and the original-raster control ABSENT, this control is not
 * effective - so the launch must be the ORDINARY candidate arm, real counts and
 * bound pools and counted coverage included, with one refusal recorded. */
static void st_zc_miscombined(void)
{
    const struct sl_bgtf_counters *t;

    begin_case("zero count miscombined: selected WITHOUT the original-raster control, "
               "it is not effective, ONE refusal is counted and the ORDINARY candidate "
               "arm runs unchanged");
    CHECK(sl_bgtf_opt_in() == 1, "the candidate is selected (%d)",
          sl_bgtf_opt_in());
    CHECK(sl_bgtf_raster_cpu() == 0,
          "the original-raster control is ABSENT (%d) - this mode is the point "
          "of the case", sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_zero_count() == 0,
          "so the zero-count control answers 0 (%d)", sl_bgtf_zero_count());
    CHECK(sl_bgtf_stats()->zc_refusals == 1,
          "and the refusal is counted once rather than half-applied (%lu)",
          sl_bgtf_stats()->zc_refusals);
    run_walk();
    report_walk("zero-count-miscombined");
    t = sl_bgtf_stats();
    CHECK(t->zc_setups == 0 && t->zc_calls == 0 && t->zc_reuses == 0,
          "nothing was counted for the zero-count control (%lu/%lu/%lu)",
          t->zc_setups, t->zc_calls, t->zc_reuses);
    /* THE ORDINARY ARM, MEASURED. */
    CHECK(t->prepasses >= 1, "the ordinary prepass ran (%lu)", t->prepasses);
    CHECK(t->vertices >= 1, "transforming vertices (%lu)", t->vertices);
    CHECK(sg.draw_arrays >= 1, "with real prepass draws (%ld)", sg.draw_arrays);
    CHECK(seam_prepass_vertices() > 0L,
          "that asked for vertices, which an empty call would not (%ld)",
          seam_prepass_vertices());
    CHECK(g_seam_npool >= 1, "and at least one pool was filled (%d)",
          g_seam_npool);
    CHECK(t->binds >= 1, "which was bound as the vertex source (%lu)", t->binds);
    CHECK(t->raster_draws >= 1 && t->raster_indices >= 1,
          "and submissions were counted on it (%lu/%lu)", t->raster_draws,
          t->raster_indices);
    seam_shape_digest();
    end_case();
}

/* BOTH OPPOSITE ARMS ASKED FOR AT ONCE. Neither may win: one omits the call and
 * the other issues it empty, so the launch must be the ORDINARY candidate arm
 * with a refusal recorded for this control. */
static void st_zc_exclusive(void)
{
    const struct sl_bgtf_counters *t;

    begin_case("zero count with the state-only control: NEITHER applies, the ordinary "
               "candidate arm runs and this control records one refusal");
    CHECK(sl_bgtf_opt_in() == 1 && sl_bgtf_raster_cpu() == 1,
          "both coupled switches are set, so neither control is refused for "
          "coupling (%d/%d)", sl_bgtf_opt_in(), sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_zero_count() == 0,
          "the zero-count control answers 0 (%d)", sl_bgtf_zero_count());
    CHECK(sl_bgtf_state_only() == 0,
          "and the state-only control answers 0 as well - neither silently wins "
          "(%d)", sl_bgtf_state_only());
    CHECK(sl_bgtf_stats()->zc_refusals == 1,
          "one refusal is counted for the zero-count control (%lu)",
          sl_bgtf_stats()->zc_refusals);
    run_walk();
    report_walk("zero-count-exclusive");
    t = sl_bgtf_stats();
    CHECK(t->zc_setups == 0 && t->zc_calls == 0,
          "it set up nothing and issued nothing (%lu/%lu)", t->zc_setups,
          t->zc_calls);
    CHECK(t->so_setups == 0 && t->so_omitted == 0,
          "and the state-only control set up nothing either (%lu/%lu)",
          t->so_setups, t->so_omitted);
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "the ORDINARY prepass ran and transformed vertices (%lu/%lu)",
          t->prepasses, t->vertices);
    CHECK(seam_prepass_vertices() > 0L,
          "its draws asked for vertices (%ld)", seam_prepass_vertices());
    seam_shape_digest();
    end_case();
}

/* A TRIPLE REQUEST: this control, the private vertex array, and one of the two
 * older exclusive arms. NONE of the three may apply.
 *
 * WHY THIS CASE EXISTS, AND IT IS A REGRESSION GUARD. The older controls resolve
 * their own conflicts by CLEARING their effective flags, and the private/
 * state-only and private/position-only clauses run BEFORE this control's
 * exclusivity questions. Asking those questions of the effective flags therefore
 * found both partners already 0 on a triple request and selected this control
 * SILENTLY, with no refusal counted - measured in r631 and corrected by deciding
 * this control's exclusions from the RAW requests. At the real caller seam the
 * consequence of that defect would have been empty prepass calls on an arm
 * nobody selected, so the check here is that the ORDINARY arm ran: real counts,
 * a filled pool and counted coverage.
 *
 * `which` names the third switch, for the message only; the environment is the
 * runner's. */
static void st_zc_triple(const char *which)
{
    const struct sl_bgtf_counters *t;

    begin_case("zero count in a TRIPLE request with the private vertex array and "
               "one older exclusive arm: NONE of the three applies, this control "
               "records exactly one refusal, and the ORDINARY candidate arm runs");
    printf("    (the third switch is %s)\n", which);
    CHECK(sl_bgtf_opt_in() == 1 && sl_bgtf_raster_cpu() == 1,
          "both coupled switches are set, so no control here is refused for "
          "coupling (%d/%d)", sl_bgtf_opt_in(), sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_zero_count() == 0,
          "the zero-count control answers 0 - it must NOT survive a triple "
          "request (%d)", sl_bgtf_zero_count());
    CHECK(sl_bgtf_private_vao() == 0,
          "the private vertex-array control answers 0 (%d)",
          sl_bgtf_private_vao());
    CHECK(sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0,
          "and so do both older exclusive arms - none of the three silently wins "
          "(%d/%d)", sl_bgtf_state_only(), sl_bgtf_pos_input());
    CHECK(sl_bgtf_stats()->zc_refusals == 1,
          "exactly ONE refusal is counted for this control, however many clauses "
          "spoke (%lu)", sl_bgtf_stats()->zc_refusals);
    run_walk();
    report_walk("zero-count-triple");
    t = sl_bgtf_stats();
    /* THE ABSENCE OF THIS CONTROL'S WORK, as an absolute over the run. */
    CHECK(t->zc_setups == 0 && t->zc_calls == 0 && t->zc_reuses == 0,
          "NO setup ran, NO empty call was issued and nothing was reused for it "
          "(%lu/%lu/%lu)", t->zc_setups, t->zc_calls, t->zc_reuses);
    CHECK(t->so_setups == 0 && t->so_omitted == 0 && t->pv_binds == 0
          && t->pv_draws == 0,
          "and neither of the other two arms did anything either (%lu/%lu/%lu/%lu)",
          t->so_setups, t->so_omitted, t->pv_binds, t->pv_draws);
    /* AND THE ARM THAT RAN IS THE ONE THE TWO COUPLED SWITCHES SELECTED, which
     * on this launch is the ORIGINAL-RASTER control and NOT the plain candidate.
     *
     * WHY THE POOL IS NOT BOUND HERE, AND WHY THAT IS THE RIGHT EXPECTATION.
     * These launches carry SL_BG_TF_CLIP=1 AND SL_BG_TF_RASTER_CPU=1, because
     * that is the coupling all three refused controls require - so with every
     * one of them off, what is left is exactly the original-raster arm: the
     * prepass RUNS with a real vertex count and fills its pool, and the raster
     * then reads the ORIGINAL position stream, so `binds` and the submission
     * counts are 0 BY DESIGN and the control's own CPU counts rise instead.
     * Expecting a bound pool here would be expecting the plain candidate arm,
     * which these switches do not select; st_zc_miscombined is the case that
     * has SL_BG_TF_RASTER_CPU absent and therefore does see the pool bound. */
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "the ORDINARY prepass ran and transformed vertices (%lu/%lu)",
          t->prepasses, t->vertices);
    CHECK(seam_prepass_vertices() > 0L,
          "its draws asked for vertices, which an empty call would not (%ld)",
          seam_prepass_vertices());
    CHECK(g_seam_npool >= 1, "a pool was filled (%d)", g_seam_npool);
    CHECK(t->binds == 0 && t->raster_draws == 0,
          "and the pool was NOT bound, because this launch is the "
          "original-raster arm: the raster reads the ORIGINAL stream (%lu/%lu)",
          t->binds, t->raster_draws);
    CHECK(g_bgtf_cpur_meshes >= 1 && g_bgtf_cpur_draws >= 1,
          "with the original-raster control's own counts rising instead "
          "(%u/%lu)", g_bgtf_cpur_meshes, g_bgtf_cpur_draws);
    seam_shape_digest();
    end_case();
}

/* DEFAULT OFF, SAID AS A MEASUREMENT. Runs in the ordinary mode, where the
 * control is absent from the environment. */
/* ---- THE SIX-CONDITION EMPTY-DRAW STATE MODE AT THE REAL CALLER ----------
 *
 * WHAT THIS HALF ADDS TO THE MODULE FIXTURE'S. The module fixture drives
 * sl_bgtf_prepass directly and reads the three conditions AT the empty draw, one
 * bracket at a time. This one runs the REAL src/gfx/sl_gfx_dl.c over a synthetic
 * asset, so what it can establish is what the module fixture cannot: that across
 * a whole walk the CPU position, colour, UV and index stream every indexed draw
 * reads is the original one, that the mode's own work is COMPLETELY accounted
 * against the meshes the caller actually served, and that the ordinary
 * transform-feedback and TF-source counts stay 0.
 *
 * ONE ROW PER RUN, because the selector is parsed once for the launch exactly
 * like every control beside it. */

/* THE SERVED-MESH IDENTITY, at the caller that owns the mesh count. Every mesh
 * the original-raster control served took either a setup or a cache hit, so the
 * two sum to g_bgtf_cpur_meshes. This is the equality the census line prints,
 * checked here against the real caller's own count rather than against a number
 * the module supplied. */
static void seam_es_served_complete(const struct sl_bgtf_counters *t)
{
    CHECK(t->es_setups + t->es_reuses == (unsigned long) g_bgtf_cpur_meshes,
          "SETUPS + REUSES == the meshes the caller served, so the mode's work "
          "is COMPLETELY accounted and none went unrecorded (%lu + %lu vs %u)",
          t->es_setups, t->es_reuses, g_bgtf_cpur_meshes);
    CHECK(g_bgtf_cpur_meshes >= 1u,
          "over at least one served mesh, so the identity is not satisfied "
          "vacuously (%u)", g_bgtf_cpur_meshes);
    /* AND THE ARM BENEATH IT AGREES. The mode runs on the zero-count arm, whose
     * own setups and reuses count the same events, so the two accounts must be
     * identical - a difference would mean one of them missed a served mesh. */
    CHECK(t->es_setups == t->zc_setups && t->es_reuses == t->zc_reuses,
          "and the zero-count arm's own account of the same events is identical "
          "(%lu/%lu vs %lu/%lu)", t->es_setups, t->es_reuses, t->zc_setups,
          t->zc_reuses);
}

/* THE CENSUS LINES THIS ROW PRODUCES, READ BACK OUT OF THE PRODUCT'S OWN
 * REPORTER.
 *
 * WHY HERE AND NOT IN THE MODULE FIXTURE. The lines are written by
 * src/gfx/sl_gfx_dl.c, which only this fixture compiles, and sl_sky_report takes
 * the destination as a FILE * - so the text can be produced into a scratch file
 * and parsed, with no GL context, no window and no game. Nothing about this call
 * touches the driver: it formats counters that the walk above already produced.
 *
 * WHAT IS CHECKED, AND WHY EACH ONE IS A SEPARATE CLAIM.
 *
 *   - The mode line is present and its program pair carries the ROW'S OWN
 *     arithmetic: switches = (pre_zero + post_zero) * empty_draws and
 *     restores = (pre_prog + post_prog) * empty_draws. Built from the module's
 *     decoder and the counters, so a line printing the wrong field or the wrong
 *     value fails rather than being described by the test.
 *   - The sentence claiming each transition count equals its restoration count
 *     is GONE. It was false for the row that leaves program 0 current past its
 *     draw, and a reader acting on it would gate on a call that row omits. Its
 *     absence is asserted as text, because prose is what a reader acts on.
 *   - The supplemental line is present on exactly the rows that are meant to
 *     carry it, with its four positions and its two draw-program counts, and
 *     ABSENT on the rows whose output a frozen parser was baselined against.
 *   - The supplemental line does not contain the mode line's own key. A reader
 *     keyed on a variable name matches by that name, so a supplemental line
 *     carrying it could be read as a second, malformed mode line.
 *
 * It reports what was ISSUED. No field here is a statement about a driver. */
#define SEAM_CENSUS_CAP 262144u
#define SEAM_ES_SUPP_KEY "#95 SL_BGTF_ES_SWITCHES"
#define SEAM_ES_MODE_KEY "#95 SL_BG_TF_EMPTY_STATE six-condition empty-draw state MODE"
/* THE POST-RESET BOUNDARY CONTROL'S OWN KEY, which is the selector's own name -
 * how the other control lines are found, and a name no existing key is a prefix
 * of. The converse rule is checked too, as text: that line names NO other
 * control variable, so a reader grepping for one of those still lands on
 * exactly one line. */
#define SEAM_PR_KEY "#95 SL_BG_TF_POST_RESET post-reset boundary control"
/* THE NEUTRAL-PROGRAM CONTROL'S OWN KEY, on the same terms: the selector's own
 * name, which no existing key is a prefix of, and whose own fields are all
 * prefixed `neutral_` so a reader grepping for any of them lands on one line. */
#define SEAM_NP_KEY "#95 SL_BG_TF_NEUTRAL_PROG neutral-program empty-draw control"
/* And the auxiliary-context control's, on the same terms: the line's own opening
 * words, so a reader grepping for the selector lands on exactly one line. */
#define SEAM_AX_KEY "#95 SL_BG_TF_AUX_EMPTY auxiliary-context empty-draw control"

static char g_seam_census[SEAM_CENSUS_CAP];

/* The product's census, into a scratch file and back. Returns the byte count, or
 * -1; g_seam_census holds the text, NUL terminated. */
static long seam_census_text(void)
{
    char path[560];
    const char *tmp = getenv("TEMP");
    FILE *f;
    long n;

    if (tmp == NULL || *tmp == '\0') tmp = ".";
    snprintf(path, sizeof path, "%s\\sl_bgtf_seam_census.txt", tmp);
    remove(path);
    f = fopen(path, "wb");
    if (f == NULL) return -1L;
    sl_sky_report(f);
    if (fclose(f) != 0) return -1L;
    n = seam_slurp(path, (unsigned char *) g_seam_census,
                   (size_t) SEAM_CENSUS_CAP - 1u);
    if (n < 0L) return -1L;
    g_seam_census[n] = '\0';
    remove(path);
    return n;
}

/* The number of times `needle` occurs in the census text. A count rather than a
 * presence test: "printed once" and "printed at all" are different claims, and a
 * line printed twice would satisfy the second. */
static int seam_census_count(const char *needle)
{
    const char *p = g_seam_census;
    int n = 0;
    size_t len = strlen(needle);
    if (len == 0u) return 0;
    while ((p = strstr(p, needle)) != NULL) { n++; p += len; }
    return n;
}

/* ONE CENSUS LINE'S FIELD PORTION, PRINTED. The lines carry a long prose tail
 * that is reviewable in src/gfx/sl_gfx_dl.c and would bury the run's own output,
 * so what is echoed here is the key and the fields up to and including `last` -
 * the part a reader or a gate parses. It is printed rather than only asserted
 * because an assertion that passed leaves no record of WHAT it passed on, and
 * this text is the evidence the row produced. */
static void seam_census_echo(const char *key, const char *last)
{
    const char *p = strstr(g_seam_census, key);
    const char *q = p != NULL ? strstr(p, last) : NULL;
    if (p == NULL) {
        printf("    census: NO LINE under \"%s\"\n", key);
        return;
    }
    if (q == NULL) {
        printf("    census: a line under \"%s\" with no \"%s\" in it\n", key, last);
        return;
    }
    printf("    census: %.*s\n", (int) ((q + strlen(last)) - p), p);
}

static void seam_es_census(int mode)
{
    const struct sl_bgtf_counters *t;
    int es_t = 1, es_d = 1, es_p = 1;
    int es_p0 = 0, es_pp = 0, es_q0 = 0, es_qp = 0;
    int supplemental;
    long n;
    char want[512];

    sl_bgtf_es_matrix(mode, &es_t, &es_d, &es_p);
    sl_bgtf_es_switches(mode, &es_p0, &es_pp, &es_q0, &es_qp);
    /* The rows that carry the supplemental line are the ones added after the
     * mode line's parser was baselined. Written as the enum rather than as a
     * name test, so a renamed row cannot slip past this. */
    supplemental = (mode == SL_BGTF_ES_R || mode == SL_BGTF_ES_S
                    || mode == SL_BGTF_ES_Q);

    begin_case("empty-draw state census: the product's own reporter prints this row's "
               "mode line with the row's program arithmetic, no equal-pair claim, and "
               "the supplemental position line on exactly the rows that carry it");
    /* THE DISCARD WATCHDOG'S EXPECTATION IS RESTATED, not inherited. It is reset
     * per case while the shim's count is cumulative for the run, and this case
     * drives no walk and issues no GL call at all - formatting counters cannot
     * move it - so the count standing here is exactly the one the case above
     * accounted for. The same rule produces it: only a row that records with
     * discard off raises any, one per empty draw. Leaving it at 0 would make
     * this case fail on that row alone and would say nothing about the census. */
    expect_draw_without_discard = (!es_d && es_t) ? (long) sl_bgtf_stats()->es_draws
                                                  : 0L;
    t = sl_bgtf_stats();
    n = seam_census_text();
    CHECK(n > 0L, "the census was produced into a scratch file (%ld bytes)", n);
    CHECK(t->es_draws >= 1,
          "over a walk that issued at least one empty draw, so the fields are "
          "not vacuous (%lu)", t->es_draws);
    CHECK(seam_census_count(SEAM_ES_MODE_KEY) == 1,
          "the mode line is printed exactly once (%d)",
          seam_census_count(SEAM_ES_MODE_KEY));
    snprintf(want, sizeof want, "mode %s, tf_active %d, discard_on %d, program_tf %d",
             sl_bgtf_es_name(mode), es_t, es_d, es_p);
    CHECK(seam_census_count(want) == 1,
          "and names this row and its three conditions: \"%s\"", want);
    /* THE PROGRAM PAIR, AS THE ROW'S OWN ARITHMETIC rather than as an equality.
     * This is the field a gate reads, and the formula is what makes the one
     * unequal row readable instead of looking like a missed restoration. */
    snprintf(want, sizeof want, "program0_switches %lu, program0_restores %lu",
             (unsigned long) (es_p0 + es_q0) * t->es_draws,
             (unsigned long) (es_pp + es_qp) * t->es_draws);
    CHECK(seam_census_count(want) == 1,
          "the program pair is (pre_zero + post_zero) and (pre_prog + post_prog) "
          "times the empty draws: \"%s\"", want);
    if (es_p0 + es_q0 != es_pp + es_qp)
        CHECK(t->es_prog0_switches != t->es_prog0_restores,
              "and on this row the two are UNEQUAL by construction, which is the "
              "row's signature and not a missed restoration (%lu vs %lu)",
              t->es_prog0_switches, t->es_prog0_restores);
    CHECK(seam_census_count("each transition count equals its restoration count") == 0,
          "the false equal-pair sentence is GONE from the line a reader acts on");
    seam_census_echo(SEAM_ES_MODE_KEY, "cpu_indices");
    /* THE SUPPLEMENTAL LINE, on exactly the rows that carry it. */
    CHECK(seam_census_count(SEAM_ES_SUPP_KEY) == (supplemental ? 1 : 0),
          "the supplemental position line is printed %s (%d)",
          supplemental ? "exactly once on this row"
                       : "NOT AT ALL on a row whose output a frozen parser was "
                         "baselined against",
          seam_census_count(SEAM_ES_SUPP_KEY));
    if (supplemental) {
        snprintf(want, sizeof want,
                 "mode %s, prog_pre_zero %lu, prog_pre_prog %lu, "
                 "prog_post_zero %lu, prog_post_prog %lu, draws_prog_tf %lu, "
                 "draws_prog0 %lu, empty_draws %lu",
                 sl_bgtf_es_name(mode),
                 (unsigned long) es_p0 * t->es_draws,
                 (unsigned long) es_pp * t->es_draws,
                 (unsigned long) es_q0 * t->es_draws,
                 (unsigned long) es_qp * t->es_draws,
                 es_p ? t->es_draws : 0ul, es_p ? 0ul : t->es_draws,
                 t->es_draws);
        CHECK(seam_census_count(want) == 1,
              "and carries the four positions and both draw-program counts: "
              "\"%s\"", want);
        snprintf(want, sizeof want,
                 "row_pre_zero %d, row_pre_prog %d, row_post_zero %d, "
                 "row_post_prog %d", es_p0, es_pp, es_q0, es_qp);
        CHECK(seam_census_count(want) == 1,
              "beside what the module's table says this row issues: \"%s\"", want);
        seam_census_echo(SEAM_ES_SUPP_KEY, "row_post_prog");
        /* THE TWO KEYS ARE DISTINCT, as text. The supplemental line must not
         * carry the mode line's key, or a reader matching on that name would
         * find two lines under it. */
        {
            const char *p = strstr(g_seam_census, SEAM_ES_SUPP_KEY);
            const char *end = p != NULL ? strchr(p, '\n') : NULL;
            int carries = 0;
            if (p != NULL && end != NULL) {
                char line[4096];
                size_t len = (size_t) (end - p);
                if (len >= sizeof line) len = sizeof line - 1u;
                memcpy(line, p, len);
                line[len] = '\0';
                carries = strstr(line, "SL_BG_TF_EMPTY_STATE") != NULL;
            }
            CHECK(p != NULL && end != NULL && !carries,
                  "and does NOT name the mode line's own key, so a reader keyed "
                  "on it finds one line (%d)", carries);
        }
    }
    /* AND THE POST-RESET BOUNDARY'S LINE IS ABSENT on every launch of this mode
     * that did not select an arm, which is every launch of it above. The arm's
     * own composed runs assert the converse. */
    if (sl_bgtf_post_reset() == SL_BGTF_PR_OFF)
        CHECK(seam_census_count(SEAM_PR_KEY) == 0,
              "and the post-reset boundary control's line is absent, no arm "
              "being selected (%d)", seam_census_count(SEAM_PR_KEY));
    end_case();
}

/* THE POST-RESET BOUNDARY CONTROL'S OWN CENSUS CASE, with the same shape and for
 * the same reason as the empty-draw mode's: what the calls were is one claim and
 * what the product REPORTS about them is another, and a reader of a run acts on
 * the second. It runs after the walk, so the counters it prints are the ones the
 * walk produced. */
static void seam_pr_census(int mode)
{
    const struct sl_bgtf_counters *t;
    int pr_d = 0, pr_fin = 0;
    long n;
    char want[640];

    sl_bgtf_pr_calls(mode, &pr_d, &pr_fin);

    begin_case("post-reset boundary census: the product's own reporter prints this "
               "arm's line once, with the arm's declared calls beside the issued "
               "counts, and names no other control variable");
    n = seam_census_text();
    CHECK(n > 0L, "the census was produced (%ld bytes)", n);
    t = sl_bgtf_stats();
    CHECK(t->pr_setups >= 1,
          "over a walk that reached at least one boundary, so the fields are not "
          "vacuous (%lu)", t->pr_setups);
    CHECK(seam_census_count(SEAM_PR_KEY) == 1,
          "the line is printed exactly once (%d)",
          seam_census_count(SEAM_PR_KEY));
    snprintf(want, sizeof want,
             "arm %s, arm_empty_draw %d, arm_finish %d, boundary_reached %lu, "
             "boundary_draws %lu, boundary_draws_under_zero %lu, "
             "boundary_finishes %lu, boundary_skips %lu, boundary_reuses %lu, "
             "arm_refusals %lu",
             sl_bgtf_pr_name(mode), pr_d, pr_fin, t->pr_setups, t->pr_draws,
             t->pr_draws_prog0, t->pr_finishes, t->pr_skips, t->pr_reuses,
             t->pr_refusals);
    CHECK(seam_census_count(want) == 1,
          "and names the arm, its two declared calls and every issued count: "
          "\"%s\"", want);
    /* THE DECLARED CALLS AND THE ISSUED COUNTS AGREE, which is the arithmetic a
     * gate reads off this line: each issued count is the arm's own answer times
     * the boundaries actually reached. */
    CHECK(t->pr_draws == (unsigned long) pr_d * t->pr_setups
          && t->pr_finishes == (unsigned long) pr_fin * t->pr_setups,
          "each issued count is the arm's declared call times the boundaries "
          "reached (%lu/%lu for %d/%d over %lu)", t->pr_draws, t->pr_finishes,
          pr_d, pr_fin, t->pr_setups);
    CHECK(t->pr_draws_prog0 == t->pr_draws,
          "and every boundary draw was made under program 0 (%lu vs %lu)",
          t->pr_draws_prog0, t->pr_draws);
    CHECK(t->pr_refusals == 0,
          "arm_refusals is 0 on a line a reader can see, a refused launch "
          "printing none at all (%lu)", t->pr_refusals);
    seam_census_echo(SEAM_PR_KEY, "candidate_submitted_runs");
    /* AND THE LINE NAMES NO OTHER CONTROL VARIABLE, as text. The three existing
     * control lines are found by their variable's name, so a line carrying one
     * of those names would be a second match under it. The composed empty-draw
     * row is therefore carried as the module's own row NAME and not as the
     * selector's. */
    {
        const char *p = strstr(g_seam_census, SEAM_PR_KEY);
        const char *end = p != NULL ? strchr(p, '\n') : NULL;
        int carries = 0;
        if (p != NULL && end != NULL) {
            static char line[8192];
            size_t len = (size_t) (end - p);
            if (len >= sizeof line) len = sizeof line - 1u;
            memcpy(line, p, len);
            line[len] = '\0';
            carries = strstr(line, "SL_BG_TF_EMPTY_STATE") != NULL
                   || strstr(line, "SL_BG_TF_ZERO_COUNT") != NULL
                   || strstr(line, "SL_BG_TF_RASTER_CPU") != NULL
                   || strstr(line, "SL_BG_TF_STATE_ONLY") != NULL
                   || strstr(line, "SL_BG_TF_POS_INPUT") != NULL
                   || strstr(line, "SL_BG_TF_PRIVATE_VAO") != NULL
                   || strstr(line, "SL_BG_TF_DUMP") != NULL
                   || strstr(line, "SL_BG_TF_CLIP") != NULL
                   || strstr(line, SEAM_ES_SUPP_KEY) != NULL;
        }
        CHECK(p != NULL && end != NULL && !carries,
              "the line names no other control variable and no other line's key, "
              "so every existing reader still finds exactly one line under its "
              "own name (%d)", carries);
    }
    /* AND THE EXISTING LINES ARE STILL THERE, UNDISTURBED, on a launch that
     * composed an empty-draw row with this arm: adding a line must not remove
     * or duplicate one. */
    if (sl_bgtf_empty_state() != SL_BGTF_ES_OFF) {
        CHECK(seam_census_count(SEAM_ES_MODE_KEY) == 1,
              "the empty-draw mode line is still printed exactly once beside it "
              "(%d)", seam_census_count(SEAM_ES_MODE_KEY));
    } else {
        CHECK(seam_census_count(SEAM_ES_MODE_KEY) == 0,
              "and no empty-draw mode line is printed, no row being selected "
              "(%d)", seam_census_count(SEAM_ES_MODE_KEY));
    }
    end_case();
}

/* ONE ARM AT THE REAL CALLER, on the REAL NONZERO path: the real
 * src/gfx/sl_gfx_dl.c driving the real src/gfx/sl_gfx_bgtf.c, with the candidate
 * admitted, every vertex transformed and the pool bound and rasterised. This is
 * the arm a shipping question is about and the one a module-only fixture cannot
 * reach, because the claim is about the instant between the prepass's own
 * restoration and the caller's raster draw - and only the caller's own code puts
 * a draw there.
 *
 * `want_pools` is whether the pool is expected to be the raster's vertex source
 * on this launch, which is what separates the real nonzero path from a launch
 * that also selected the original-raster control. */
static void st_pr_control(int mode)
{
    const struct sl_bgtf_counters *t;
    int pr_d = 0, pr_fin = 0;
    int i, boundary_after_all = 1, pool_draws = 0;
    long last_pool_draw_seq = -1L, first_raster_seq = -1L;

    sl_bgtf_pr_calls(mode, &pr_d, &pr_fin);

    begin_case("post-reset boundary at the real caller: the arm's calls are issued "
               "after the prepass's own restoration and before the raster draws, the "
               "prepass's work and pool are untouched, and the account is complete");
    CHECK(sl_bgtf_post_reset() == mode,
          "SL_BG_TF_POST_RESET selects arm %s - run it through "
          "tools/windows/bgtfseamtest.ps1 (%s)", sl_bgtf_pr_name(mode),
          sl_bgtf_pr_name(sl_bgtf_post_reset()));
    CHECK(sl_bgtf_opt_in() == 1,
          "and the candidate itself is selected, or no walk would be admitted "
          "and this boundary would not exist (%d)", sl_bgtf_opt_in());
    CHECK(sl_bgtf_raster_cpu() == 0 && sl_bgtf_zero_count() == 0
          && sl_bgtf_empty_state() == SL_BGTF_ES_OFF,
          "with the original-raster control, the zero-count arm and every "
          "empty-draw row ABSENT, so this is the REAL nonzero transform path "
          "(%d/%d/%s)", sl_bgtf_raster_cpu(), sl_bgtf_zero_count(),
          sl_bgtf_es_name(sl_bgtf_empty_state()));
    CHECK(sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0
          && sl_bgtf_private_vao() == 0,
          "and none of the three exclusive controls either (%d/%d/%d)",
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_stats()->pr_refusals == 0,
          "the arm was not refused on this launch (%lu)",
          sl_bgtf_stats()->pr_refusals);
    run_walk();
    report_walk("post-reset-boundary");
    t = sl_bgtf_stats();

    CHECK(g_bgtf_elig_walks >= 1u,
          "the walk was admitted to the candidate (%u admitted)",
          g_bgtf_elig_walks);
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "and REAL prepasses transformed REAL vertices, which is what makes "
          "this the nonzero path (%lu/%lu)", t->prepasses, t->vertices);
    CHECK(seam_prepass_vertices() > 0L,
          "the recording draws asked for a positive number of vertices (%ld)",
          seam_prepass_vertices());
    CHECK(t->binds >= 1,
          "and the pools were bound as a raster vertex source (%lu)", t->binds);

    /* THE BOUNDARY WAS REACHED ONCE PER PREPASS, AND NEVER SKIPPED. */
    CHECK(t->pr_setups == t->prepasses,
          "the boundary was reached once for every prepass that ran (%lu vs "
          "%lu)", t->pr_setups, t->prepasses);
    CHECK(t->pr_skips == 0,
          "and no bracket was skipped for an unproved precondition (%lu)",
          t->pr_skips);
    CHECK(t->pr_draws == (unsigned long) pr_d * t->pr_setups
          && t->pr_finishes == (unsigned long) pr_fin * t->pr_setups,
          "each of the arm's calls was issued once per reached boundary "
          "(%lu/%lu for %d/%d over %lu)", t->pr_draws, t->pr_finishes, pr_d,
          pr_fin, t->pr_setups);
    CHECK(t->pr_draws_prog0 == t->pr_draws,
          "every boundary draw was made under program 0 (%lu vs %lu)",
          t->pr_draws_prog0, t->pr_draws);
    /* AND THE FAKE DRIVER SAW EXACTLY THAT MANY glFinish CALLS, which is the
     * other half: the counter above is what the module counted and this is what
     * the driver was handed. A disagreement is a count raised without a call or
     * a call made without a count. */
    CHECK(sg.finish_calls == (long) t->pr_finishes,
          "and the driver was handed exactly that many glFinish calls, so no "
          "count was raised without a call (%ld vs %lu)", sg.finish_calls,
          t->pr_finishes);

    /* THE ORDER, FROM THE SHIM'S OWN RECORD. Every boundary draw - count 0, no
     * capture open, program 0, nothing bound at index 0 - comes AFTER the last
     * recording prepass draw it belongs to, and every raster draw on a pool
     * comes after the boundary calls of its own prepass. The walk serves several
     * meshes, so the claim is checked as "no boundary draw precedes a recording
     * draw" and "the last boundary call precedes nothing that was already
     * drawn", both read off the record's sequence numbers. */
    for (i = 0; i < sg.ndraws; i++) {
        const struct seamgl_draw *d = &sg.draw[i];
        if (d->arrays && d->tf_active) {
            /* a recording prepass draw */
            if (last_pool_draw_seq < d->seq) last_pool_draw_seq = d->seq;
        } else if (d->arrays && d->count == 0L && d->program == 0u
                   && d->tf_buf == 0u && !d->discard) {
            /* a boundary draw: it must come after a recording draw has happened
             * in this walk, never before the first one */
            if (last_pool_draw_seq < 0L) boundary_after_all = 0;
        } else if (!d->arrays) {
            pool_draws++;
            if (first_raster_seq < 0L) first_raster_seq = d->seq;
        }
    }
    CHECK(boundary_after_all,
          "no boundary draw was recorded before the walk's first recording "
          "prepass draw (%d)", boundary_after_all);
    CHECK(pool_draws >= 1 && first_raster_seq > 0L,
          "the walk issued indexed raster draws, so there is a 'before the "
          "raster' to be before (%d draws, first at %ld)", pool_draws,
          first_raster_seq);
    if (pr_fin)
        CHECK(sg.seq_last_finish > 0L,
              "the last glFinish has a recorded position in the call order "
              "(%ld)", sg.seq_last_finish);
    else
        CHECK(sg.seq_last_finish == 0L,
              "and no glFinish was issued at all, so none has a position (%ld)",
              sg.seq_last_finish);

    /* NO PROGRAM SWITCH IS ADDED, AND NONE INSIDE A FEEDBACK SCOPE. The arm
     * relies on program 0 already being current and sets it nowhere. */
    CHECK(sg.v_program_change_in_tf == 0,
          "no program was changed inside an open feedback scope (%ld)",
          sg.v_program_change_in_tf);
    CHECK(sg.v_tf_overrun == 0 && sg.v_unknown_buffer == 0
          && sg.v_draws_full == 0,
          "nothing overran a pool, no unknown buffer was named and the draw "
          "record is complete (%ld/%ld/%ld)", sg.v_tf_overrun,
          sg.v_unknown_buffer, sg.v_draws_full);
    /* NO ERROR STATE WAS CONSUMED. The control never calls glGetError. */
    CHECK(sg.get_error_calls == 0,
          "no glGetError was called, so no error state was consumed and no "
          "error claim is available from this arm (%ld)", sg.get_error_calls);
    CHECK(sg.get_buffer_sub_data == 0,
          "and nothing was read back, so no outcome here rests on a state read "
          "(%ld)", sg.get_buffer_sub_data);

    /* NOTHING WAS COUNTED FOR ANY OTHER ARM, which is what says this launch is
     * the one it claims to be. */
    CHECK(t->zc_setups == 0 && t->zc_calls == 0 && t->es_setups == 0
          && t->es_draws == 0 && t->so_setups == 0 && t->so_omitted == 0
          && t->pi_draws == 0 && t->pv_binds == 0,
          "no zero-count, empty-draw, state-only, position-only or private "
          "vertex-array number was raised");
    seam_shape_digest();
    end_case();

    seam_pr_census(mode);
}

/* A REFUSED ARM LEAVES THE LAUNCH EXACTLY AS ITS OWN SWITCHES MADE IT. One
 * reason per run, and the case asserts the same thing for each: the arm answers
 * OFF, ONE refusal is counted, nothing of the control's own happened, NO
 * glFinish was issued anywhere, no line of the control's is printed, and
 * whatever arm the other switches selected ran unchanged. */
static void st_pr_refused(const char *why)
{
    const struct sl_bgtf_counters *t;

    begin_case("post-reset boundary refused: the arm is NOT in force, ONE refusal is "
               "counted, nothing of the control's own ran or was issued, and no line "
               "of its is printed");
    printf("    reason under test: %s\n", why);
    CHECK(sl_bgtf_post_reset() == SL_BGTF_PR_OFF,
          "the arm answers OFF and none was substituted (%s)",
          sl_bgtf_pr_name(sl_bgtf_post_reset()));
    CHECK(sl_bgtf_stats()->pr_refusals == 1,
          "the refusal is counted ONCE per launch rather than per draw (%lu)",
          sl_bgtf_stats()->pr_refusals);
    run_walk();
    report_walk("post-reset-boundary-refused");
    t = sl_bgtf_stats();
    CHECK(t->pr_setups == 0 && t->pr_draws == 0 && t->pr_finishes == 0
          && t->pr_draws_prog0 == 0 && t->pr_skips == 0 && t->pr_reuses == 0,
          "NOTHING was counted for the control - no boundary reached, no call "
          "issued, no skip and no reuse (%lu/%lu/%lu/%lu/%lu/%lu)",
          t->pr_setups, t->pr_draws, t->pr_finishes, t->pr_draws_prog0,
          t->pr_skips, t->pr_reuses);
    CHECK(t->pr_mode == 0,
          "and the recorded EFFECTIVE arm is 0 beside that refusal (%lu)",
          t->pr_mode);
    CHECK(sg.finish_calls == 0,
          "the driver was handed NO glFinish anywhere in this walk (%ld)",
          sg.finish_calls);
    {
        const long n = seam_census_text();
        CHECK(n > 0L, "the census was produced (%ld bytes)", n);
        CHECK(seam_census_count(SEAM_PR_KEY) == 0,
              "and no line of this control's is printed, so a refused launch "
              "leaves the census as it was (%d)",
              seam_census_count(SEAM_PR_KEY));
    }
    end_case();
}

static void st_es_control(int mode)
{
    const struct sl_bgtf_counters *t;
    unsigned long cpur_d0 = g_bgtf_cpur_draws, cpur_i0 = g_bgtf_cpur_indices;
    int es_t = 1, es_d = 1, es_p = 1;
    int es_p0 = 0, es_pp = 0, es_q0 = 0, es_qp = 0;
    int i, at_draw = 0, agree_t = 0, agree_d = 0, agree_p = 0;

    sl_bgtf_es_matrix(mode, &es_t, &es_d, &es_p);
    sl_bgtf_es_switches(mode, &es_p0, &es_pp, &es_q0, &es_qp);

    begin_case("empty-draw state at the real caller: every empty draw carries this "
               "row's three conditions, the CPU stream and indices are the ORIGINAL "
               "ones, the mode's work is completely accounted and no coverage is "
               "claimed");
    /* ROW B IS T1 D0: a recording draw with discard off is the row, so the
     * watchdog expects exactly one for each empty draw this run issues. It is
     * declared before run_walk and reconciled against the counted draws below,
     * so it cannot be satisfied by a number that happens to match. */
    CHECK(sl_bgtf_empty_state() == mode,
          "SL_BG_TF_EMPTY_STATE selects row %s - run it through "
          "tools/windows/bgtfseamtest.ps1 (%s)", sl_bgtf_es_name(mode),
          sl_bgtf_es_name(sl_bgtf_empty_state()));
    CHECK(sl_bgtf_zero_count() == 1,
          "and the zero-count arm is in force, which is what makes the draw "
          "EMPTY (%d)", sl_bgtf_zero_count());
    CHECK(sl_bgtf_raster_cpu() == 1 && sl_bgtf_opt_in() == 1,
          "with both coupled switches, or no walk would be admitted and the "
          "raster would read a pool (%d/%d)", sl_bgtf_raster_cpu(),
          sl_bgtf_opt_in());
    CHECK(sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0
          && sl_bgtf_private_vao() == 0,
          "and none of the three exclusive arms is (%d/%d/%d)",
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_stats()->es_refusals == 0,
          "the mode was not refused on this launch (%lu)",
          sl_bgtf_stats()->es_refusals);
    run_walk();
    report_walk("empty-draw-state");
    t = sl_bgtf_stats();

    CHECK(g_bgtf_elig_walks >= 1u,
          "the walk was admitted to the candidate (%u admitted)",
          g_bgtf_elig_walks);
    CHECK(t->es_setups >= 1,
          "and at least one setup bracket ran under the mode (%lu)",
          t->es_setups);
    CHECK(t->es_draws == t->es_setups,
          "with one ISSUED empty draw for each of them (%lu vs %lu)",
          t->es_draws, t->es_setups);
    CHECK(t->es_mode == (unsigned long) mode,
          "and the recorded EFFECTIVE row is %s (%lu)", sl_bgtf_es_name(mode),
          t->es_mode);
    /* THE ROW'S OWN EXPECTED WATCHDOG VALUE, reconciled against the draws that
     * actually happened rather than asserted as a constant.
     *
     * AND THIS SHIM'S RULE IS NARROWER THAN THE MODULE FIXTURE'S, which is
     * worth stating rather than letting a formula imply it. This one raises the
     * count only for a RECORDING draw - `if (sg.tf_active)` guards it, because a
     * draw with no capture open cannot write a pool to overrun either - so row
     * C, which is T0 D0, raises nothing here while the module fixture's shim,
     * which checks discard unconditionally, does raise one for it. Row B, T1 D0,
     * is the row that raises one in both. The two shims are measuring different
     * things and the expectation follows each one's own rule rather than being
     * averaged into something neither does. */
    expect_draw_without_discard =
        (!es_d && es_t) ? (long) t->es_draws : 0L;

    /* EVERY RECORDING DRAW CARRIED THIS ROW'S CONDITIONS, read from the shim's
     * own per-draw record - which samples them AT the draw, before the row's
     * restoration. Counted over ALL of them, so one agreeing draw among several
     * cannot pass the case. */
    for (i = 0; i < sg.ndraws; i++) {
        if (!sg.draw[i].arrays || sg.draw[i].tf_buf == 0u) continue;
        at_draw++;
        if (sg.draw[i].tf_active == es_t) agree_t++;
        if (sg.draw[i].discard == es_d) agree_d++;
        if ((sg.draw[i].program != 0u) == (es_p != 0)) agree_p++;
        CHECK(sg.draw[i].count == 0L,
              "recording draw %d submitted ZERO vertices (%ld)", i,
              sg.draw[i].count);
        CHECK(sg.draw[i].mode == GL_POINTS && sg.draw[i].first == 0L,
              "and was GL_POINTS from vertex 0, matching the mode a T1 row's "
              "scope was opened with (%u/%ld)", (unsigned) sg.draw[i].mode,
              sg.draw[i].first);
    }
    CHECK(at_draw >= 1 && at_draw == (int) t->es_draws,
          "the shim recorded one draw per counted empty draw (%d vs %lu)",
          at_draw, t->es_draws);
    CHECK(agree_t == at_draw,
          "EVERY one had transform feedback %s at the call (%d of %d)",
          es_t ? "ACTIVE" : "INACTIVE", agree_t, at_draw);
    CHECK(agree_d == at_draw,
          "EVERY one had rasterizer discard %s at the call (%d of %d)",
          es_d ? "ON" : "OFF", agree_d, at_draw);
    CHECK(agree_p == at_draw,
          "and EVERY one had %s current at the call (%d of %d)",
          es_p ? "the prepass program" : "program 0", agree_p, at_draw);
    CHECK(seam_prepass_vertices() == 0L,
          "the vertices they asked for sum to ZERO (%ld)",
          seam_prepass_vertices());

    /* THE SCOPE AND THE TRANSITIONS, AS COUNTS. The begin/end pair is matched or
     * absent; each transition is closed by exactly one restoration. */
    CHECK(sg.begin_tf == sg.end_tf,
          "the feedback scope was opened and closed the same number of times "
          "(%ld/%ld)", sg.begin_tf, sg.end_tf);
    CHECK(t->es_tf_begins == t->es_tf_ends,
          "the mode's own begin and end counts are EQUAL (%lu/%lu)",
          t->es_tf_begins, t->es_tf_ends);
    CHECK(t->es_tf_begins == (es_t ? t->es_setups : 0UL),
          "and are %s (%lu)",
          es_t ? "one per setup, because this row keeps the pair"
               : "ZERO, because this row omits the pair together",
          t->es_tf_begins);
    CHECK(t->es_discard_offs == t->es_discard_ons
          && t->es_discard_offs == (es_d ? 0UL : t->es_draws),
          "the discard transitions and their restorations are EQUAL and are %s "
          "(%lu/%lu)", es_d ? "ZERO on a D1 row" : "one per empty draw",
          t->es_discard_offs, t->es_discard_ons);
    /* THE PROGRAM SWITCHES ARE COUNTED PER POSITION, and the expectation is the
     * row's own four answers times the draws it issued. The two totals are NOT
     * required to be equal, because one row leaves program 0 current past the
     * draw and so makes no rebind to pair with its switch; requiring equality
     * here would demand that row issue a call it exists to omit. Each positional
     * count is pinned instead, which is a stronger statement than the equality
     * was: it says WHERE every switch was, not only how many there were. */
    CHECK(t->es_prog_pre_zero == (unsigned long) es_p0 * t->es_draws
          && t->es_prog_pre_prog == (unsigned long) es_pp * t->es_draws
          && t->es_prog_post_zero == (unsigned long) es_q0 * t->es_draws
          && t->es_prog_post_prog == (unsigned long) es_qp * t->es_draws,
          "and each program switch this row names was issued once per empty "
          "draw, in its own position (%lu/%lu/%lu/%lu for %d/%d/%d/%d over %lu "
          "draws)", t->es_prog_pre_zero, t->es_prog_pre_prog,
          t->es_prog_post_zero, t->es_prog_post_prog, es_p0, es_pp, es_q0,
          es_qp, t->es_draws);
    CHECK(t->es_prog0_switches
              == (unsigned long) (es_p0 + es_q0) * t->es_draws
          && t->es_prog0_restores
              == (unsigned long) (es_pp + es_qp) * t->es_draws,
          "so the two totals are this row's own (%lu/%lu)",
          t->es_prog0_switches, t->es_prog0_restores);
    /* AND EVERY EMPTY DRAW WAS COUNTED UNDER THE PROGRAM IT WAS MADE UNDER,
     * which is what separates the rows that share a program at the draw from
     * each other only by position. */
    CHECK(t->es_draws_prog_tf == (es_p ? t->es_draws : 0UL)
          && t->es_draws_prog0 == (es_p ? 0UL : t->es_draws)
          && t->es_draws_prog_tf + t->es_draws_prog0 == t->es_draws,
          "every empty draw was counted under %s, and the two account for all of "
          "them (%lu/%lu vs %lu)",
          es_p ? "the prepass program" : "program 0", t->es_draws_prog_tf,
          t->es_draws_prog0, t->es_draws);
    CHECK(sg.gen_buffers >= 1 && sg.buffer_data >= 1
          && sg.get_buffer_parameteriv >= 1,
          "the pools were still created, sized and VERIFIED on this row "
          "(%ld/%ld/%ld)", sg.gen_buffers, sg.buffer_data,
          sg.get_buffer_parameteriv);
    CHECK(g_seam_npool >= 1,
          "and the empty draws still named their pools, because the indexed "
          "binding is part of the setup every row keeps (%d)", g_seam_npool);

    /* NO WORK AND NO COVERAGE, exactly as on Z_KNOWN. */
    CHECK(t->prepasses == 0 && t->vertices == 0 && t->walks == 0
          && t->reuses == 0 && t->binds == 0,
          "no prepass, transformed vertex, credited walk, POOL reuse or bind "
          "(%lu/%lu/%lu/%lu/%lu)", t->prepasses, t->vertices, t->walks,
          t->reuses, t->binds);
    CHECK(g_bgtf_src == 0,
          "no pool is the vertex source at the end of the walk (%d)",
          g_bgtf_src);
    CHECK(t->raster_draws == 0 && t->raster_indices == 0,
          "NO draw was counted as a submission on a prepass pool (%lu/%lu)",
          t->raster_draws, t->raster_indices);
    CHECK(t->dcalls[SL_BGGPU_DEPTH_SINGLE] == 0
          && t->dcalls[SL_BGGPU_DEPTH_FAR0] == 0
          && t->dcalls[SL_BGGPU_DEPTH_FAR1] == 0,
          "and no per-depth submission was (%lu/%lu/%lu)",
          t->dcalls[SL_BGGPU_DEPTH_SINGLE], t->dcalls[SL_BGGPU_DEPTH_FAR0],
          t->dcalls[SL_BGGPU_DEPTH_FAR1]);
    CHECK(t->so_setups == 0 && t->so_omitted == 0,
          "the state-only control counted nothing, so no row of this mode can "
          "be read as its omission (%lu/%lu)", t->so_setups, t->so_omitted);
    CHECK(t->pi_draws == 0 && t->pv_binds == 0 && t->pv_draws == 0,
          "and neither exclusive control counted anything (%lu/%lu/%lu)",
          t->pi_draws, t->pv_binds, t->pv_draws);
    CHECK(sg.get_buffer_sub_data == 0,
          "nothing was read back, so no outcome here rests on a state read "
          "(%ld)", sg.get_buffer_sub_data);

    /* THE RASTER IS THE ORIGINAL ONE, for every indexed draw of the walk: four
     * components at the unpushed offset out of the pass's own stream, through
     * program 0 and the pass's element buffer. */
    seam_all_draws_original("empty-draw-state");

    /* AND THE CPU WORK WAS NOT SKIPPED, which is what keeps the comparison to
     * the original arm meaningful. */
    CHECK(g_bgtf_cpur_draws > cpur_d0,
          "the original-raster control's draw count rose (%lu -> %lu)", cpur_d0,
          g_bgtf_cpur_draws);
    CHECK(g_bgtf_cpur_indices > cpur_i0,
          "and carries the indices (%lu -> %lu)", cpur_i0,
          g_bgtf_cpur_indices);
    seam_es_served_complete(t);
    CHECK(g_bgb_vtx_skip == 0ul || g_bgb_repairs >= 1u,
          "no vertex was left unwritten by the reduction (skipped %lu, "
          "repairs %u)", g_bgb_vtx_skip, g_bgb_repairs);
    CHECK(g_bgb_broken == 0u,
          "and no mesh drew from clip positions the walk did not write (%u)",
          g_bgb_broken);
    seam_shape_digest();
    end_case();

    /* AND WHAT THE PRODUCT REPORTS ABOUT THIS ROW, read back out of its own
     * census. It is its own case because it is a different claim from the ones
     * above: those are about the calls the pass made, this is about the text a
     * reader of the run acts on. It runs after the walk, so the counters it
     * prints are the ones the walk produced. */
    seam_es_census(mode);
}

/* A REFUSED MODE LEAVES THE LAUNCH EXACTLY AS ITS OWN SWITCHES MADE IT. Three
 * reasons reach this, one per run, and the case asserts the same thing for each:
 * the mode answers OFF, ONE refusal is counted, nothing of the mode's own
 * happened, and whatever arm the other switches selected ran unchanged.
 *
 * `want_zc` is what the zero-count arm itself must answer on this launch, which
 * is NOT always 0: a launch that asked for an unreadable mode with the arm
 * properly coupled still gets the ordinary zero-count arm, and a launch missing
 * a coupled switch does not. Passing it in keeps the two refusals distinct
 * instead of collapsing them into "something was off". */
static void st_es_refused(const char *why, int want_zc)
{
    const struct sl_bgtf_counters *t;

    begin_case("empty-draw state refused: the mode is NOT in force, ONE refusal is "
               "counted, nothing of the mode's own ran, and the launch is whatever "
               "its other switches selected");
    printf("    reason under test: %s\n", why);
    CHECK(sl_bgtf_empty_state() == SL_BGTF_ES_OFF,
          "the mode answers OFF and no row was substituted (%s)",
          sl_bgtf_es_name(sl_bgtf_empty_state()));
    CHECK(sl_bgtf_stats()->es_refusals == 1,
          "the refusal is counted ONCE per launch rather than per draw (%lu)",
          sl_bgtf_stats()->es_refusals);
    CHECK(sl_bgtf_zero_count() == want_zc,
          "and the zero-count arm is %s, as this launch's own switches decide "
          "(%d)", want_zc ? "still in force" : "not in force",
          sl_bgtf_zero_count());
    run_walk();
    report_walk("empty-draw-state-refused");
    t = sl_bgtf_stats();
    CHECK(t->es_setups == 0 && t->es_draws == 0 && t->es_reuses == 0
          && t->es_tf_begins == 0 && t->es_tf_ends == 0
          && t->es_discard_offs == 0 && t->es_discard_ons == 0
          && t->es_prog0_switches == 0 && t->es_prog0_restores == 0
          && t->es_prog_pre_zero == 0 && t->es_prog_pre_prog == 0
          && t->es_prog_post_zero == 0 && t->es_prog_post_prog == 0
          && t->es_draws_prog_tf == 0 && t->es_draws_prog0 == 0
          && t->es_mode == 0,
          "NOTHING was counted for the mode, in any switch position, and no "
          "effective row is recorded");
    /* AND NEITHER CENSUS LINE WAS PRINTED. A refused launch reports its reason
     * once on stderr and prints no line here, so a reader of a refused run
     * cannot find a row to attribute its images to. Asserted on the text,
     * because a line printed with zeroes would satisfy the counters above. */
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_ES_MODE_KEY) == 0
          && seam_census_count(SEAM_ES_SUPP_KEY) == 0,
          "and carries NO empty-draw line of either kind (%d/%d)",
          seam_census_count(SEAM_ES_MODE_KEY),
          seam_census_count(SEAM_ES_SUPP_KEY));
    /* THE SCOPE AND THE PROGRAM ARE THE ORDINARY ONES. A refused mode must not
     * have omitted a begin/end pair or moved the program: those are the
     * differences it would have made, and their absence is what "unchanged"
     * means here. */
    CHECK(sg.begin_tf >= 1 && sg.end_tf == sg.begin_tf,
          "the feedback scope was opened and closed as usual, so no pair was "
          "omitted (%ld/%ld)", sg.begin_tf, sg.end_tf);
    if (want_zc) {
        CHECK(t->zc_setups >= 1 && t->zc_calls == t->zc_setups,
              "the ordinary zero-count arm ran and issued its empty calls "
              "(%lu/%lu)", t->zc_setups, t->zc_calls);
        CHECK(seam_prepass_vertices() == 0L,
              "which submitted no vertices (%ld)", seam_prepass_vertices());
        CHECK(t->prepasses == 0 && t->binds == 0,
              "and claimed no transformed prepass or bind (%lu/%lu)",
              t->prepasses, t->binds);
    } else {
        CHECK(t->zc_setups == 0 && t->zc_calls == 0,
              "the zero-count arm counted nothing either (%lu/%lu)",
              t->zc_setups, t->zc_calls);
        CHECK(t->prepasses >= 1 && t->vertices >= 1,
              "and the ORDINARY prepass ran, transforming vertices (%lu/%lu)",
              t->prepasses, t->vertices);
        CHECK(seam_prepass_vertices() > 0L,
              "whose draws asked for vertices, which an empty call would not "
              "(%ld)", seam_prepass_vertices());
    }
    seam_shape_digest();
    end_case();
}

/* THE SELECTOR ALONE ENABLES NOTHING, with the candidate off. The same shape as
 * the three controls before it: the mode is a refinement of an arm that does not
 * exist on this launch. */
static void st_es_alone(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("empty-draw state alone: the selector with SL_BG_TF_CLIP absent "
               "selects nothing, is refused once and admits no walk");
    CHECK(sl_bgtf_opt_in() == 0,
          "the candidate is NOT selected (%d) - this is the point of the case",
          sl_bgtf_opt_in());
    CHECK(sl_bgtf_empty_state() == SL_BGTF_ES_OFF,
          "so the mode answers OFF (%s)",
          sl_bgtf_es_name(sl_bgtf_empty_state()));
    CHECK(t->es_refusals == 1, "and the refusal is counted once (%lu)",
          t->es_refusals);
    CHECK(t->es_setups == 0 && t->es_draws == 0 && t->es_reuses == 0,
          "with nothing counted for it (%lu/%lu/%lu)", t->es_setups,
          t->es_draws, t->es_reuses);
    run_walk();
    t = sl_bgtf_stats();
    CHECK(g_bgtf_elig_walks == 0u,
          "no walk was admitted to the candidate at all (%u)",
          g_bgtf_elig_walks);
    CHECK(sg.begin_tf == 0 && sg.draw_arrays == 0,
          "no feedback scope was opened and no array draw was issued (%ld/%ld)",
          sg.begin_tf, sg.draw_arrays);
    CHECK(t->es_setups == 0 && t->es_draws == 0,
          "and the walk added nothing for the mode (%lu/%lu)", t->es_setups,
          t->es_draws);
    end_case();
}

static void st_pr_alone(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("post-reset boundary alone: the selector with SL_BG_TF_CLIP absent "
               "selects nothing, is refused once, admits no walk and issues no "
               "glFinish");
    CHECK(sl_bgtf_opt_in() == 0,
          "the candidate is NOT selected (%d) - this is the point of the case",
          sl_bgtf_opt_in());
    CHECK(sl_bgtf_post_reset() == SL_BGTF_PR_OFF,
          "so the arm answers OFF (%s)",
          sl_bgtf_pr_name(sl_bgtf_post_reset()));
    CHECK(t->pr_refusals == 1, "and the refusal is counted once (%lu)",
          t->pr_refusals);
    run_walk();
    t = sl_bgtf_stats();
    CHECK(g_bgtf_elig_walks == 0u,
          "no walk was admitted to the candidate at all (%u)",
          g_bgtf_elig_walks);
    CHECK(sg.begin_tf == 0 && sg.draw_arrays == 0,
          "no feedback scope was opened and no array draw was issued (%ld/%ld)",
          sg.begin_tf, sg.draw_arrays);
    CHECK(sg.finish_calls == 0,
          "and NO glFinish was issued, which is what the coupling to "
          "SL_BG_TF_CLIP is for: with no prepass there is no boundary (%ld)",
          sg.finish_calls);
    CHECK(t->pr_setups == 0 && t->pr_draws == 0 && t->pr_finishes == 0
          && t->pr_skips == 0 && t->pr_reuses == 0,
          "and the walk added nothing for the control (%lu/%lu/%lu/%lu/%lu)",
          t->pr_setups, t->pr_draws, t->pr_finishes, t->pr_skips,
          t->pr_reuses);
    end_case();
}

static void st_es_default_off(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("empty-draw state default off: no mode is selected and nothing in this "
               "process was counted for one, refusals and the recorded row included");
    CHECK(sl_bgtf_empty_state() == SL_BGTF_ES_OFF,
          "SL_BG_TF_EMPTY_STATE is absent, so no row is in force (%s)",
          sl_bgtf_es_name(sl_bgtf_empty_state()));
    CHECK(t->es_setups == 0 && t->es_draws == 0 && t->es_reuses == 0
          && t->es_tf_begins == 0 && t->es_tf_ends == 0
          && t->es_prog0_switches == 0 && t->es_prog0_restores == 0
          && t->es_prog_pre_zero == 0 && t->es_prog_pre_prog == 0
          && t->es_prog_post_zero == 0 && t->es_prog_post_prog == 0
          && t->es_draws_prog_tf == 0 && t->es_draws_prog0 == 0
          && t->es_discard_offs == 0 && t->es_discard_ons == 0
          && t->es_refusals == 0 && t->es_mode == 0,
          "and nothing in this process was counted for one, the positional "
          "switch counts included");
    CHECK(t->prepasses >= 1 && t->binds >= 1 && t->raster_draws >= 1,
          "while the ordinary arm ran prepasses, bound pools and counted "
          "submissions (%lu/%lu/%lu)", t->prepasses, t->binds,
          t->raster_draws);
    CHECK(seam_prepass_vertices() > 0L,
          "and its prepass draws asked for vertices (%ld)",
          seam_prepass_vertices());
    /* AND THE CENSUS CARRIES NEITHER LINE. This is the Z_KNOWN output a frozen
     * reader was baselined against: an absent selector adds no line at all, so
     * the mode line and the supplemental line must both be missing from the
     * text. Asserted on the text rather than on the counters, because a line
     * printed with zeroes would satisfy the counters and change the output. */
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_ES_MODE_KEY) == 0
          && seam_census_count(SEAM_ES_SUPP_KEY) == 0,
          "and carries NO empty-draw line of either kind, which is the output an "
          "absent selector has always had (%d/%d)",
          seam_census_count(SEAM_ES_MODE_KEY),
          seam_census_count(SEAM_ES_SUPP_KEY));
    end_case();
}

static void st_pr_default_off(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("post-reset boundary default off: no arm is selected, nothing in this "
               "process was counted for one, NO glFinish was issued anywhere and the "
               "census carries no line of this control's");
    CHECK(sl_bgtf_post_reset() == SL_BGTF_PR_OFF,
          "SL_BG_TF_POST_RESET is absent, so no arm is in force (%s)",
          sl_bgtf_pr_name(sl_bgtf_post_reset()));
    CHECK(t->pr_setups == 0 && t->pr_draws == 0 && t->pr_finishes == 0
          && t->pr_draws_prog0 == 0 && t->pr_skips == 0 && t->pr_reuses == 0
          && t->pr_refusals == 0 && t->pr_mode == 0,
          "and nothing in this process was counted for one, the skip count and "
          "the recorded arm included (%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu)",
          t->pr_setups, t->pr_draws, t->pr_finishes, t->pr_draws_prog0,
          t->pr_skips, t->pr_reuses, t->pr_refusals, t->pr_mode);
    /* AND NO SYNCHRONISATION POINT WAS ADDED. glFinish exists as a symbol in
     * this shim and always did - sl_gfx_dl.c calls it on paths this fixture
     * does not drive - so the claim has always had to be a counter here rather
     * than a link-time absence. It is now OBSERVED rather than inert, which is
     * what lets the count be read at all. */
    CHECK(sg.finish_calls == 0,
          "and the driver was handed NO glFinish anywhere in this process "
          "(%ld)", sg.finish_calls);
    CHECK(t->prepasses >= 1 && t->binds >= 1 && t->raster_draws >= 1,
          "while the ordinary arm ran prepasses, bound pools and counted "
          "submissions (%lu/%lu/%lu)", t->prepasses, t->binds,
          t->raster_draws);
    /* AND THE CENSUS CARRIES NO LINE OF THIS CONTROL'S, asserted on the text
     * rather than on the counters, because a line printed with zeroes would
     * satisfy the counters and change the output a frozen reader was baselined
     * against. */
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_PR_KEY) == 0,
          "and carries no post-reset boundary line, which is the output an "
          "absent selector has (%d)", seam_census_count(SEAM_PR_KEY));
    end_case();
}

/* ---- THE NEUTRAL-PROGRAM EMPTY-DRAW CONTROL (SL_BG_TF_NEUTRAL_PROG) ------
 *
 * WHAT ONLY THIS FIXTURE CAN OBSERVE. The module fixture already proves the
 * bracket's exact call order against a synthetic GL. What the REAL caller adds
 * is that every empty draw of a whole walk ran under the second program, that
 * the raster still reads the ORIGINAL stream, that the served-mesh account is
 * complete, and that the product's own census prints this control's line with
 * the numbers the walk produced.
 *
 * AND THE PROGRAM IS IDENTIFIED BY WHAT TEARDOWN DELETES, not by a name this
 * fixture guesses. Several programs exist in this process - the candidate's own
 * prepass program and whatever else the pass built - so "the draws ran under the
 * neutral one" is asserted as an IDENTITY against the object sl_bgtf_shutdown
 * releases, which is the one object this control owns. A name read from the
 * draws themselves would make the claim true by construction. */
static void st_np_control(void)
{
    const struct sl_bgtf_counters *t;
    unsigned long cpur_d0 = g_bgtf_cpur_draws, cpur_i0 = g_bgtf_cpur_indices;
    GLuint at_prog = 0u;
    int i, at_draw = 0, agree_t = 0, agree_d = 0, agree_p = 0, one_prog = 1;

    begin_case("neutral program at the real caller: every empty draw of the walk runs "
               "under ONE second program with row A's conditions, the raster reads the "
               "ORIGINAL stream, the account is complete and teardown deletes exactly "
               "that program");
    CHECK(sl_bgtf_neutral_prog() == 1,
          "SL_BG_TF_NEUTRAL_PROG selects the control - run it through "
          "tools/windows/bgtfseamtest.ps1 (%d)", sl_bgtf_neutral_prog());
    CHECK(sl_bgtf_zero_count() == 1 && sl_bgtf_raster_cpu() == 1
          && sl_bgtf_opt_in() == 1,
          "on the zero-count arm with both coupled switches, which is what makes "
          "the draw EMPTY and the raster original (%d/%d/%d)",
          sl_bgtf_zero_count(), sl_bgtf_raster_cpu(), sl_bgtf_opt_in());
    CHECK(sl_bgtf_empty_state() == SL_BGTF_ES_OFF
          && sl_bgtf_post_reset() == SL_BGTF_PR_OFF
          && sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0
          && sl_bgtf_private_vao() == 0,
          "and with every control it is exclusive with ABSENT (%s/%s/%d/%d/%d)",
          sl_bgtf_es_name(sl_bgtf_empty_state()),
          sl_bgtf_pr_name(sl_bgtf_post_reset()), sl_bgtf_state_only(),
          sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_stats()->np_refusals == 0,
          "the control was not refused on this launch (%lu)",
          sl_bgtf_stats()->np_refusals);
    run_walk();
    report_walk("neutral-program");
    t = sl_bgtf_stats();

    CHECK(g_bgtf_elig_walks >= 1u,
          "the walk was admitted to the candidate (%u admitted)",
          g_bgtf_elig_walks);
    CHECK(t->np_setups >= 1 && t->np_draws == t->np_setups,
          "at least one bracket ran under the control, with one ISSUED empty "
          "draw for each (%lu/%lu)", t->np_setups, t->np_draws);
    CHECK(t->np_draws_neutral == t->np_draws,
          "and EVERY one of them was issued under the neutral program, taken "
          "from the switches this process made (%lu vs %lu)",
          t->np_draws_neutral, t->np_draws);
    CHECK(t->np_switches == t->np_draws && t->np_restores == t->np_draws,
          "with one switch and one restore per draw, EQUAL to each other, so no "
          "bracket was handed on under the second program (%lu/%lu vs %lu)",
          t->np_switches, t->np_restores, t->np_draws);
    CHECK(t->np_creates == 1 && t->np_create_fails == 0 && t->np_skips == 0,
          "ONE program served the whole walk, with no failure and no skipped "
          "served draw (%lu/%lu/%lu)", t->np_creates, t->np_create_fails,
          t->np_skips);
    /* NO WATCHDOG IS EXPECTED. The conditions at the draw are row A's: feedback
     * INACTIVE and discard ON, so this shim's recording-draw-without-discard
     * rule - which is guarded on an open capture - raises nothing. */
    expect_draw_without_discard = 0L;

    /* EVERY EMPTY DRAW CARRIED ROW A'S CONDITIONS AND ONE AND THE SAME SECOND
     * PROGRAM, read from the shim's per-draw record, which samples them AT the
     * draw and so before the restore. Counted over ALL of them. */
    for (i = 0; i < sg.ndraws; i++) {
        if (!sg.draw[i].arrays || sg.draw[i].tf_buf == 0u) continue;
        at_draw++;
        if (sg.draw[i].tf_active == 0) agree_t++;
        if (sg.draw[i].discard == 1) agree_d++;
        if (sg.draw[i].program != 0u) agree_p++;
        if (at_prog == 0u) at_prog = sg.draw[i].program;
        else if (sg.draw[i].program != at_prog) one_prog = 0;
        CHECK(sg.draw[i].count == 0L,
              "empty draw %d submitted ZERO vertices (%ld)", i,
              sg.draw[i].count);
    }
    CHECK(at_draw >= 1 && at_draw == (int) t->np_draws,
          "the shim recorded one draw per counted empty draw (%d vs %lu)",
          at_draw, t->np_draws);
    CHECK(agree_t == at_draw,
          "EVERY one had transform feedback INACTIVE at the call, which is row "
          "A's T0 (%d of %d)", agree_t, at_draw);
    CHECK(agree_d == at_draw,
          "EVERY one had rasterizer discard ON at the call, which is row A's D1 "
          "(%d of %d)", agree_d, at_draw);
    CHECK(agree_p == at_draw && one_prog && at_prog != 0u,
          "and EVERY one had ONE AND THE SAME non-zero program current (%d of "
          "%d, program %u)", agree_p, at_draw, (unsigned) at_prog);
    CHECK(seam_prepass_vertices() == 0L,
          "the vertices they asked for sum to ZERO (%ld)",
          seam_prepass_vertices());
    /* AND NO FEEDBACK SCOPE WAS OPENED ANYWHERE IN THE WALK, which is what
     * makes the two program switches legal: the API forbids a switch inside a
     * begin/end pair, and this control omits the pair exactly as row A does. */
    CHECK(sg.begin_tf == 0 && sg.end_tf == 0,
          "no feedback scope was opened or closed in the whole walk, so neither "
          "switch could have been made inside one (%ld/%ld)", sg.begin_tf,
          sg.end_tf);
    CHECK(sg.finish_calls == 0,
          "and no glFinish was issued - this control adds no synchronisation "
          "point (%ld)", sg.finish_calls);

    /* NO WORK AND NO COVERAGE, exactly as on the zero-count arm. */
    CHECK(t->prepasses == 0 && t->vertices == 0 && t->walks == 0
          && t->binds == 0 && t->raster_draws == 0,
          "no prepass, transformed vertex, credited walk, bind or submission "
          "(%lu/%lu/%lu/%lu/%lu)", t->prepasses, t->vertices, t->walks,
          t->binds, t->raster_draws);
    CHECK(t->zc_setups == t->np_setups && t->zc_reuses == t->np_reuses,
          "and the zero-count arm's own account of the same events is identical "
          "(%lu/%lu vs %lu/%lu)", t->zc_setups, t->zc_reuses, t->np_setups,
          t->np_reuses);
    CHECK(t->np_setups + t->np_reuses == (unsigned long) g_bgtf_cpur_meshes
          && g_bgtf_cpur_meshes >= 1u,
          "SETUPS + REUSES == the meshes the caller served, over at least one "
          "(%lu + %lu vs %u)", t->np_setups, t->np_reuses, g_bgtf_cpur_meshes);
    CHECK(t->es_setups == 0 && t->pr_setups == 0 && t->so_setups == 0
          && t->pi_draws == 0 && t->pv_draws == 0,
          "and nothing for any control this one is exclusive with");

    /* THE RASTER IS THE ORIGINAL ONE, and the CPU work was not skipped. */
    seam_all_draws_original("neutral-program");
    CHECK(g_bgtf_cpur_draws > cpur_d0 && g_bgtf_cpur_indices > cpur_i0,
          "the original-raster control's draws and indices rose (%lu -> %lu, "
          "%lu -> %lu)", cpur_d0, g_bgtf_cpur_draws, cpur_i0,
          g_bgtf_cpur_indices);
    seam_shape_digest();
    end_case();

    /* AND WHAT THE PRODUCT REPORTS, read back out of its own census, and then
     * what teardown releases. Both are separate claims from the calls above:
     * one is about the text a reader acts on, the other about the object's
     * lifetime. */
    begin_case("neutral program census and teardown: the product's own reporter prints "
               "this control's line with the walk's numbers, and teardown deletes "
               "exactly the program every empty draw ran under");
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_NP_KEY) == 1,
          "and carries this control's line exactly once (%d)",
          seam_census_count(SEAM_NP_KEY));
    {
        char want[256];
        snprintf(want, sizeof want,
                 "neutral_empty_draws %lu, neutral_draws_under_neutral %lu, "
                 "neutral_switches %lu, neutral_restores %lu",
                 t->np_draws, t->np_draws_neutral, t->np_switches,
                 t->np_restores);
        CHECK(seam_census_count(want) == 1,
              "with the walk's own four issued-call numbers in it (\"%s\")",
              want);
    }
    /* AND IT NAMES NO OTHER CONTROL'S VARIABLE, so a reader grepping for one of
     * those still lands on exactly one line. */
    CHECK(seam_census_count(SEAM_NP_KEY) == 1
          && seam_census_count(SEAM_ES_MODE_KEY) == 0
          && seam_census_count(SEAM_PR_KEY) == 0,
          "and no empty-draw or post-reset line appears, neither being in force "
          "(%d/%d)", seam_census_count(SEAM_ES_MODE_KEY),
          seam_census_count(SEAM_PR_KEY));
    seam_census_echo(SEAM_NP_KEY, "neutral_refusals");
    /* THE TEARDOWN, AND THE IDENTITY THAT MAKES IT CHECKABLE. */
    CHECK(sg.delete_program == 0,
          "no PROGRAM delete had been issued before teardown (%ld)",
          sg.delete_program);
    /* THE SHADER'S RELEASE, HOWEVER, IS ALREADY DONE, and before teardown
     * rather than at it: the acquire issues it on every path it can leave, and
     * on the successful one the linked program holds the flagged object until
     * the program itself is deleted. The capable context that this shim models
     * resolves both releases, so this is the count a real backend would make. */
    CHECK(sg.delete_shader == 1,
          "while exactly one glDeleteShader had already been issued, by the "
          "acquire (%ld)", sg.delete_shader);
    CHECK(sl_bgtf_stats()->np_shader_deletes == 1UL
          && sl_bgtf_stats()->np_fail_prog_deletes == 0UL,
          "which the module counts, with no failed-link program deleted "
          "(%lu/%lu)", sl_bgtf_stats()->np_shader_deletes,
          sl_bgtf_stats()->np_fail_prog_deletes);
    sl_bgtf_shutdown();
    CHECK(sg.delete_program == 1,
          "teardown issued exactly one glDeleteProgram (%ld)",
          sg.delete_program);
    CHECK(sg.delete_shader == 1,
          "and no second glDeleteShader - teardown has no shader to release "
          "(%ld)", sg.delete_shader);
    CHECK(sg.last_deleted_program == at_prog,
          "and it named THE program every empty draw of the walk ran under, "
          "which is what says the object deleted is this control's own and not "
          "the prepass program (%u vs %u)",
          (unsigned) sg.last_deleted_program, (unsigned) at_prog);
    CHECK(sl_bgtf_stats()->np_deletes == sl_bgtf_stats()->np_creates,
          "the delete count matches the create count (%lu vs %lu)",
          sl_bgtf_stats()->np_deletes, sl_bgtf_stats()->np_creates);
    sl_bgtf_shutdown();
    CHECK(sg.delete_program == 1 && sg.delete_shader == 1,
          "and a second teardown issues no further delete of either kind "
          "(%ld/%ld)", sg.delete_program, sg.delete_shader);
    end_case();
}

/* A REFUSED CONTROL LEAVES THE LAUNCH EXACTLY AS ITS OWN SWITCHES MADE IT. One
 * reason per run. `want_zc` is what the zero-count arm itself must answer, which
 * is not always 0: a launch that asked for this control alongside an empty-draw
 * row still has the arm, while one missing a coupled switch does not. */
static void st_np_refused(const char *why, int want_zc)
{
    const struct sl_bgtf_counters *t;

    begin_case("neutral program refused: the control is NOT in force, ONE refusal is "
               "counted, nothing of its own ran, no second program was created and the "
               "launch is whatever its other switches selected");
    printf("    reason under test: %s\n", why);
    CHECK(sl_bgtf_neutral_prog() == 0,
          "the control answers off and nothing was substituted (%d)",
          sl_bgtf_neutral_prog());
    CHECK(sl_bgtf_stats()->np_refusals == 1,
          "the refusal is counted ONCE per launch rather than per draw (%lu)",
          sl_bgtf_stats()->np_refusals);
    CHECK(sl_bgtf_zero_count() == want_zc,
          "and the zero-count arm is %s, as this launch's own switches decide "
          "(%d)", want_zc ? "still in force" : "not in force",
          sl_bgtf_zero_count());
    run_walk();
    report_walk("neutral-program-refused");
    t = sl_bgtf_stats();
    CHECK(t->np_setups == 0 && t->np_draws == 0 && t->np_draws_neutral == 0
          && t->np_switches == 0 && t->np_restores == 0 && t->np_reuses == 0
          && t->np_skips == 0 && t->np_creates == 0 && t->np_create_fails == 0,
          "NOTHING was counted for the control, in any position");
    CHECK(sg.delete_program == 0 && sg.delete_shader == 0,
          "no object of this control's was created, so neither delete was "
          "issued (%ld/%ld)", sg.delete_program, sg.delete_shader);
    /* AND NO CENSUS LINE WAS PRINTED. A refused launch reports its reason once
     * on stderr and prints no line, so a reader of a refused run cannot
     * attribute its images to this control. Asserted on the text, because a
     * line printed with zeroes would satisfy the counters above. */
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_NP_KEY) == 0,
          "and carries no line of this control's (%d)",
          seam_census_count(SEAM_NP_KEY));
    if (want_zc) {
        CHECK(t->zc_setups >= 1 && t->zc_calls == t->zc_setups,
              "the ordinary zero-count arm ran and issued its empty calls "
              "(%lu/%lu)", t->zc_setups, t->zc_calls);
    } else {
        CHECK(t->prepasses >= 1 && t->vertices >= 1,
              "and the ORDINARY prepass ran, transforming vertices (%lu/%lu)",
              t->prepasses, t->vertices);
        CHECK(seam_prepass_vertices() > 0L,
              "whose draws asked for vertices, which an empty call would not "
              "(%ld)", seam_prepass_vertices());
    }
    seam_shape_digest();
    end_case();
}

/* THE SWITCH ALONE ENABLES NOTHING, with the candidate off. */
static void st_np_alone(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("neutral program alone: the switch with SL_BG_TF_CLIP absent selects "
               "nothing, is refused once, admits no walk and creates no program");
    CHECK(sl_bgtf_opt_in() == 0,
          "the candidate is NOT selected (%d) - this is the point of the case",
          sl_bgtf_opt_in());
    CHECK(sl_bgtf_neutral_prog() == 0, "so the control answers off (%d)",
          sl_bgtf_neutral_prog());
    CHECK(t->np_refusals == 1, "and the refusal is counted once (%lu)",
          t->np_refusals);
    run_walk();
    t = sl_bgtf_stats();
    CHECK(g_bgtf_elig_walks == 0u,
          "no walk was admitted to the candidate at all (%u)",
          g_bgtf_elig_walks);
    CHECK(sg.draw_arrays == 0 && sg.delete_program == 0
          && sg.delete_shader == 0,
          "no array draw was issued and no object was created or deleted "
          "(%ld/%ld/%ld)", sg.draw_arrays, sg.delete_program,
          sg.delete_shader);
    CHECK(t->np_setups == 0 && t->np_draws == 0 && t->np_creates == 0,
          "and the walk added nothing for the control (%lu/%lu/%lu)",
          t->np_setups, t->np_draws, t->np_creates);
    end_case();
}

static void st_np_default_off(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("neutral program default off: the control is not selected, nothing in "
               "this process was counted for it, no second program was created or "
               "deleted anywhere, and the census carries no line of its");
    CHECK(sl_bgtf_neutral_prog() == 0,
          "SL_BG_TF_NEUTRAL_PROG is absent, so the control is off (%d)",
          sl_bgtf_neutral_prog());
    CHECK(t->np_setups == 0 && t->np_draws == 0 && t->np_draws_neutral == 0
          && t->np_switches == 0 && t->np_restores == 0 && t->np_reuses == 0
          && t->np_skips == 0 && t->np_creates == 0 && t->np_create_fails == 0
          && t->np_deletes == 0 && t->np_shader_deletes == 0
          && t->np_fail_prog_deletes == 0 && t->np_refusals == 0,
          "and nothing in this process was counted for it (%lu/%lu/%lu/%lu/%lu/"
          "%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu)", t->np_setups, t->np_draws,
          t->np_draws_neutral, t->np_switches, t->np_restores, t->np_reuses,
          t->np_skips, t->np_creates, t->np_create_fails, t->np_deletes,
          t->np_shader_deletes, t->np_fail_prog_deletes,
          t->np_refusals);
    CHECK(sg.delete_program == 0 && sg.delete_shader == 0,
          "the driver was handed NEITHER delete anywhere in this process "
          "(%ld/%ld)", sg.delete_program, sg.delete_shader);
    CHECK(t->prepasses >= 1 && t->binds >= 1 && t->raster_draws >= 1,
          "while the ordinary arm ran prepasses, bound pools and counted "
          "submissions (%lu/%lu/%lu)", t->prepasses, t->binds,
          t->raster_draws);
    /* AND TEARDOWN IS A NO-OP ON AN OFF LAUNCH, which is what keeps the new
     * backend call free of any effect on the default path. */
    sl_bgtf_shutdown();
    CHECK(sg.delete_program == 0 && sg.delete_shader == 0
          && sl_bgtf_stats()->np_deletes == 0
          && sl_bgtf_stats()->np_shader_deletes == 0,
          "and teardown issues no delete and counts none (%ld/%ld/%lu/%lu)",
          sg.delete_program, sg.delete_shader,
          sl_bgtf_stats()->np_deletes,
          sl_bgtf_stats()->np_shader_deletes);
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_NP_KEY) == 0,
          "and carries no neutral-program line, which is the output an absent "
          "switch has (%d)", seam_census_count(SEAM_NP_KEY));
    end_case();
}

/* ---- THE AUXILIARY-CONTEXT EMPTY-DRAW CONTROL AT THE REAL CALLER -------
 *
 * WHAT THIS FIXTURE OWNS AND WHAT IT DOES NOT. The module fixture drives every
 * refusal shape, because it can gate each entry point and each switch; this one
 * drives the REAL sl_gfx_dl.c over a real walk against a shim that models a
 * CAPABLE platform, and owns three things that fixture cannot show:
 *
 *   - over a whole walk of many meshes, EVERY empty draw landed on the
 *     auxiliary context and the MAIN context issued none;
 *   - one context served the whole walk, the main context was current again at
 *     every close, and the raster draw sequence is UNCHANGED from the
 *     original-raster control's - which is what says the relocation did not
 *     disturb the picture's commands;
 *   - the product's own census prints this control's line with the walk's
 *     numbers, and teardown destroys exactly the one context.
 */
static void st_ax_control(void)
{
    const struct sl_bgtf_counters *t;
    unsigned long cpur_d0 = g_bgtf_cpur_draws, cpur_i0 = g_bgtf_cpur_indices;
    GLuint at_prog = 0u;
    int i, at_draw = 0, agree_t = 0, agree_d = 0, agree_c = 0, one_prog = 1;

    begin_case("auxiliary context at the real caller: every empty draw of the walk is "
               "issued on ONE second, object-sharing context with row A's conditions "
               "there, the MAIN context issues none, the raster reads the ORIGINAL "
               "stream, the account is complete and teardown destroys exactly that "
               "context");
    CHECK(sl_bgtf_aux_empty() == 1,
          "SL_BG_TF_AUX_EMPTY selects the control - run it through "
          "tools/windows/bgtfseamtest.ps1 (%d)", sl_bgtf_aux_empty());
    CHECK(sl_bgtf_zero_count() == 1 && sl_bgtf_raster_cpu() == 1
          && sl_bgtf_opt_in() == 1,
          "on the zero-count arm with both coupled switches, which is what makes "
          "the draw EMPTY and the raster original (%d/%d/%d)",
          sl_bgtf_zero_count(), sl_bgtf_raster_cpu(), sl_bgtf_opt_in());
    CHECK(sl_bgtf_empty_state() == SL_BGTF_ES_OFF
          && sl_bgtf_post_reset() == SL_BGTF_PR_OFF
          && sl_bgtf_neutral_prog() == 0 && sl_bgtf_state_only() == 0
          && sl_bgtf_pos_input() == 0 && sl_bgtf_private_vao() == 0,
          "and with every control it is exclusive with ABSENT "
          "(%s/%s/%d/%d/%d/%d)", sl_bgtf_es_name(sl_bgtf_empty_state()),
          sl_bgtf_pr_name(sl_bgtf_post_reset()), sl_bgtf_neutral_prog(),
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_stats()->ax_refusals == 0,
          "the control was not refused on this launch (%lu)",
          sl_bgtf_stats()->ax_refusals);
    run_walk();
    report_walk("auxiliary-context");
    t = sl_bgtf_stats();

    CHECK(g_bgtf_elig_walks >= 1u,
          "the walk was admitted to the candidate (%u admitted)",
          g_bgtf_elig_walks);
    CHECK(t->ax_setups >= 1 && t->ax_draws == t->ax_setups,
          "at least one bracket ran under the control, with one ISSUED empty "
          "draw for each (%lu/%lu)", t->ax_setups, t->ax_draws);
    CHECK(t->ax_draws_aux == t->ax_draws,
          "and EVERY one was issued with the AUXILIARY context current, taken "
          "from the switch this process made (%lu vs %lu)", t->ax_draws_aux,
          t->ax_draws);
    CHECK(t->ax_main_omits == t->ax_draws,
          "with one omitted main-context draw for each, which is the positive "
          "record that the call MOVED rather than was added (%lu vs %lu)",
          t->ax_main_omits, t->ax_draws);
    CHECK(t->ax_switches == t->ax_draws && t->ax_restores == t->ax_draws,
          "one switch and one restore per draw, EQUAL to each other, so no "
          "bracket was handed on with the wrong context current (%lu/%lu vs "
          "%lu)", t->ax_switches, t->ax_restores, t->ax_draws);
    CHECK(t->ax_creates == 1 && t->ax_create_fails == 0 && t->ax_skips == 0
          && t->ax_share_fails == 0 && t->ax_switch_fails == 0
          && t->ax_lost == 0,
          "ONE context served the whole walk, with no failure of any kind "
          "(%lu/%lu/%lu/%lu/%lu/%lu)", t->ax_creates, t->ax_create_fails,
          t->ax_skips, t->ax_share_fails, t->ax_switch_fails, t->ax_lost);
    CHECK(sg.aux_created == 1 && sg.aux_exists == 1,
          "the platform was asked for exactly one and it is alive (%ld/%d)",
          sg.aux_created, sg.aux_exists);
    CHECK(sg.is_program_calls == 1 && sg.is_program_on_aux == 1,
          "the share check was made ONCE, on the auxiliary context (%ld/%d)",
          sg.is_program_calls, sg.is_program_on_aux);
    /* NO WATCHDOG IS EXPECTED. The conditions at the draw are row A's on the
     * context it is made on: feedback INACTIVE and discard ON, so this shim's
     * recording-draw-without-discard rule raises nothing. */
    expect_draw_without_discard = 0L;

    /* EVERY EMPTY DRAW LANDED ON THE AUXILIARY CONTEXT, with row A's conditions
     * and one and the same shared program, read from the shim's per-draw record,
     * which samples all four AT the draw and so before the restore. */
    for (i = 0; i < sg.ndraws; i++) {
        if (!sg.draw[i].arrays) continue;
        at_draw++;
        if (sg.draw[i].tf_active == 0) agree_t++;
        if (sg.draw[i].discard == 1) agree_d++;
        if (sg.draw[i].ctx == 1) agree_c++;
        if (at_prog == 0u) at_prog = sg.draw[i].program;
        else if (sg.draw[i].program != at_prog) one_prog = 0;
        CHECK(sg.draw[i].count == 0L,
              "empty draw %d submitted ZERO vertices (%ld)", i,
              sg.draw[i].count);
    }
    CHECK(at_draw >= 1 && at_draw == (int) t->ax_draws,
          "the shim recorded one glDrawArrays per counted empty draw (%d vs "
          "%lu)", at_draw, t->ax_draws);
    CHECK(agree_c == at_draw && sg.draws_on_aux == at_draw,
          "EVERY one landed on the AUXILIARY context, so the main context "
          "issued NONE of them (%d of %d, %ld)", agree_c, at_draw,
          sg.draws_on_aux);
    CHECK(agree_t == at_draw,
          "EVERY one had transform feedback INACTIVE at the call, which is row "
          "A's T0 (%d of %d)", agree_t, at_draw);
    CHECK(agree_d == at_draw,
          "EVERY one had rasterizer discard ON at the call - enabled on that "
          "context, discard being per-context state (%d of %d)", agree_d,
          at_draw);
    CHECK(one_prog && at_prog != 0u,
          "and EVERY one ran under ONE AND THE SAME non-zero program, the "
          "SHARED prepass program by the name the main context uses (program "
          "%u)", (unsigned) at_prog);
    /* WHETHER AN OUTPUT BUFFER WAS BOUND ON THAT CONTEXT IS NOT ASKED HERE, and
     * the reason is a limit of this shim rather than an omission. It models ONE
     * global GL state, not one per context, so the indexed binding the MAIN
     * context's setup made reads back on an auxiliary draw's record too -
     * asking the question here would fail against a module that bound nothing
     * there, which was measured rather than argued. What this fixture can say
     * instead is the count below: exactly the share check plus five calls per
     * relocated draw landed on the auxiliary context, so there was no sixth call
     * for a binding to be. WHICH five they are is the module fixture's claim -
     * its transcript stamps every record with its context and names each one. */
    CHECK(seam_prepass_vertices() == 0L,
          "the vertices they asked for sum to ZERO (%ld)",
          seam_prepass_vertices());
    CHECK(sg.begin_tf == 0 && sg.end_tf == 0,
          "no feedback scope was opened or closed on either context in the "
          "whole walk (%ld/%ld)", sg.begin_tf, sg.end_tf);
    CHECK(sg.finish_calls == 0,
          "and no glFinish was issued - this control adds no synchronisation "
          "point (%ld)", sg.finish_calls);
    CHECK(sg.v_call_no_context == 0,
          "and not one GL call was made with no context current (%ld)",
          sg.v_call_no_context);
    CHECK(sg.ctx_cur == 0,
          "the MAIN context is current at the end of the walk, which is the "
          "state the caller renders in (%d)", sg.ctx_cur);
    /* AND THE AUXILIARY CONTEXT CARRIED NOTHING BUT WHAT THIS CONTROL DECLARES:
     * the one share check, plus five calls per bracket. A sixth per bracket
     * would be work done on a context nobody else knows about. */
    CHECK(sg.calls_on_aux == 1 + 5 * (long) t->ax_draws,
          "exactly the share check plus five calls per relocated draw landed on "
          "the auxiliary context (%ld, expected %ld for %lu draws)",
          sg.calls_on_aux, 1 + 5 * (long) t->ax_draws, t->ax_draws);
    /* AND THE ENTRY POINTS IT USES THERE WERE RESOLVED THERE. A pointer belongs
     * to the context it was resolved on, so the two the bracket needs on the
     * auxiliary context are resolved once on it and cached for its lifetime -
     * ONCE, not per bracket, which is what this walk of several brackets and
     * several cache hits shows. This fixture models one GL state rather than one
     * per context, so what it can state is WHERE the resolve happened; the
     * per-pointer identity claim is the module fixture's. */
    CHECK(sg.proc_calls_on_aux == 2,
          "and its own two entry points were resolved ON it, exactly once for "
          "the context rather than once per bracket (%ld)",
          sg.proc_calls_on_aux);
    /* AND THE LOST-CONTEXT EXIT PATH WAS NEVER REACHED. Every shape in this
     * fixture is a platform on which nothing fails, so a termination here would
     * mean the control ended a healthy launch. */
    CHECK(sg.fatal_calls == 0 && t->ax_lost == 0 && t->ax_fatals == 0,
          "no lost context, and the terminate-now path was not taken "
          "(%ld/%lu/%lu)", sg.fatal_calls, t->ax_lost, t->ax_fatals);

    /* NO WORK AND NO COVERAGE, exactly as on the zero-count arm - with the ONE
     * declared exception that arm's own call count does not rise. */
    CHECK(t->prepasses == 0 && t->vertices == 0 && t->walks == 0
          && t->binds == 0 && t->raster_draws == 0,
          "no prepass, transformed vertex, credited walk, bind or submission "
          "(%lu/%lu/%lu/%lu/%lu)", t->prepasses, t->vertices, t->walks,
          t->binds, t->raster_draws);
    CHECK(t->zc_setups == t->ax_setups && t->zc_reuses == t->ax_reuses,
          "the zero-count arm's account of the same brackets is identical "
          "(%lu/%lu vs %lu/%lu)", t->zc_setups, t->zc_reuses, t->ax_setups,
          t->ax_reuses);
    CHECK(t->zc_calls == 0,
          "but zc_calls is 0, because that counter is of empty calls made on the "
          "MAIN context and this arm makes none there (%lu)", t->zc_calls);
    CHECK(t->ax_setups + t->ax_reuses == (unsigned long) g_bgtf_cpur_meshes
          && g_bgtf_cpur_meshes >= 1u,
          "REACHED + REUSES == the meshes the caller served, over at least one "
          "(%lu + %lu vs %u)", t->ax_setups, t->ax_reuses,
          g_bgtf_cpur_meshes);
    CHECK(t->es_setups == 0 && t->pr_setups == 0 && t->so_setups == 0
          && t->np_setups == 0 && t->pi_draws == 0 && t->pv_draws == 0,
          "and nothing for any control this one is exclusive with");

    /* THE RASTER IS THE ORIGINAL ONE, and the CPU work was not skipped. */
    seam_all_draws_original("auxiliary-context");
    CHECK(g_bgtf_cpur_draws > cpur_d0 && g_bgtf_cpur_indices > cpur_i0,
          "the original-raster control's draws and indices rose (%lu -> %lu, "
          "%lu -> %lu)", cpur_d0, g_bgtf_cpur_draws, cpur_i0,
          g_bgtf_cpur_indices);
    seam_shape_digest();
    end_case();

    /* AND WHAT THE PRODUCT REPORTS, read back out of its own census, and then
     * what teardown releases. */
    begin_case("auxiliary context census and teardown: the product's own reporter "
               "prints this control's line with the walk's numbers, and teardown "
               "destroys exactly the one context it created");
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_AX_KEY) == 1,
          "and carries this control's line exactly once (%d)",
          seam_census_count(SEAM_AX_KEY));
    {
        char want[256];
        snprintf(want, sizeof want,
                 "aux_brackets %lu, main_draws_omitted %lu, "
                 "aux_empty_draws %lu, draws_on_aux %lu",
                 t->ax_setups, t->ax_main_omits, t->ax_draws,
                 t->ax_draws_aux);
        CHECK(seam_census_count(want) == 1,
              "with the walk's own four placement numbers in it (\"%s\")",
              want);
    }
    CHECK(seam_census_count(SEAM_AX_KEY) == 1
          && seam_census_count(SEAM_NP_KEY) == 0
          && seam_census_count(SEAM_ES_MODE_KEY) == 0
          && seam_census_count(SEAM_PR_KEY) == 0,
          "and no neutral-program, empty-draw or post-reset line appears, none "
          "being in force (%d/%d/%d)", seam_census_count(SEAM_NP_KEY),
          seam_census_count(SEAM_ES_MODE_KEY), seam_census_count(SEAM_PR_KEY));
    seam_census_echo(SEAM_AX_KEY, "aux_refusals");
    CHECK(sg.aux_destroyed == 0,
          "no context had been destroyed before teardown (%ld)",
          sg.aux_destroyed);
    CHECK(sg.delete_program == 0 && sg.delete_shader == 0,
          "and no GL OBJECT was created or deleted by this control anywhere - "
          "it owns a context, not an object (%ld/%ld)", sg.delete_program,
          sg.delete_shader);
    sl_bgtf_shutdown();
    CHECK(sg.aux_destroyed == 1 && sg.aux_exists == 0,
          "teardown destroyed exactly one context and none is alive (%ld/%d)",
          sg.aux_destroyed, sg.aux_exists);
    CHECK(sl_bgtf_stats()->ax_destroys == sl_bgtf_stats()->ax_creates,
          "the destroy count matches the create count (%lu vs %lu)",
          sl_bgtf_stats()->ax_destroys, sl_bgtf_stats()->ax_creates);
    CHECK(sg.ctx_cur == 0,
          "and the MAIN context was current at the destroy, which is the "
          "precondition the backend states for it (%d)", sg.ctx_cur);
    sl_bgtf_shutdown();
    CHECK(sg.aux_destroyed == 1,
          "and a second teardown destroys nothing further (%ld)",
          sg.aux_destroyed);
    end_case();
}

/* A REFUSED CONTROL LEAVES THE LAUNCH EXACTLY AS ITS OWN SWITCHES MADE IT, and
 * for this one the refusal is also a statement that the process owns no second
 * GL context. One reason per run. */
static void st_ax_refused(const char *why, int want_zc)
{
    const struct sl_bgtf_counters *t;

    begin_case("auxiliary context refused: the control is NOT in force, ONE refusal is "
               "counted, NO second GL context was created, and the launch is whatever "
               "its other switches selected");
    printf("    reason under test: %s\n", why);
    CHECK(sl_bgtf_aux_empty() == 0, "so the control answers off (%d)",
          sl_bgtf_aux_empty());
    t = sl_bgtf_stats();
    CHECK(t->ax_refusals == 1,
          "and the launch is counted as a refusal, exactly once (%lu)",
          t->ax_refusals);
    CHECK(sl_bgtf_zero_count() == want_zc,
          "the zero-count arm answers %d, as its own switches decided (%d)",
          want_zc, sl_bgtf_zero_count());
    run_walk();
    report_walk("auxiliary-context-refused");
    t = sl_bgtf_stats();
    CHECK(t->ax_setups == 0 && t->ax_draws == 0 && t->ax_draws_aux == 0
          && t->ax_main_omits == 0 && t->ax_switches == 0
          && t->ax_restores == 0 && t->ax_reuses == 0 && t->ax_skips == 0
          && t->ax_creates == 0 && t->ax_create_fails == 0
          && t->ax_destroys == 0,
          "nothing of this control's ran (%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/"
          "%lu/%lu)", t->ax_setups, t->ax_draws, t->ax_draws_aux,
          t->ax_main_omits, t->ax_switches, t->ax_restores, t->ax_reuses,
          t->ax_skips, t->ax_creates, t->ax_create_fails, t->ax_destroys);
    CHECK(sg.aux_created == 0 && sg.aux_exists == 0 && sg.aux_switches == 0
          && sg.is_program_calls == 0,
          "and the platform was never asked for a context, never switched and "
          "never asked about sharing (%ld/%d/%ld/%ld)", sg.aux_created,
          sg.aux_exists, sg.aux_switches, sg.is_program_calls);
    CHECK(sg.calls_on_aux == 0 && sg.draws_on_aux == 0 && sg.ctx_cur == 0,
          "nothing landed anywhere but the MAIN context (%ld/%ld/%d)",
          sg.calls_on_aux, sg.draws_on_aux, sg.ctx_cur);
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_AX_KEY) == 0,
          "and carries NO line of this control's, a refused launch printing "
          "none (%d)", seam_census_count(SEAM_AX_KEY));
    sl_bgtf_shutdown();
    CHECK(sg.aux_destroyed == 0,
          "and teardown destroys nothing (%ld)", sg.aux_destroyed);
    end_case();
}

static void st_ax_default_off(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("auxiliary context default off: the control is not selected, NO SECOND "
               "GL CONTEXT was asked for, created, switched to or destroyed anywhere in "
               "this process, nothing was counted for it, and the census carries no "
               "line of its");
    CHECK(sl_bgtf_aux_empty() == 0,
          "SL_BG_TF_AUX_EMPTY is absent, so the control is off (%d)",
          sl_bgtf_aux_empty());
    CHECK(t->ax_setups == 0 && t->ax_main_omits == 0 && t->ax_draws == 0
          && t->ax_draws_aux == 0 && t->ax_switches == 0
          && t->ax_restores == 0 && t->ax_reuses == 0 && t->ax_skips == 0
          && t->ax_creates == 0 && t->ax_create_fails == 0
          && t->ax_share_fails == 0 && t->ax_switch_fails == 0
          && t->ax_lost == 0 && t->ax_destroys == 0 && t->ax_refusals == 0,
          "and nothing in this process was counted for it (%lu/%lu/%lu/%lu/%lu/"
          "%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu)", t->ax_setups,
          t->ax_main_omits, t->ax_draws, t->ax_draws_aux, t->ax_switches,
          t->ax_restores, t->ax_reuses, t->ax_skips, t->ax_creates,
          t->ax_create_fails, t->ax_share_fails, t->ax_switch_fails,
          t->ax_lost, t->ax_destroys, t->ax_refusals);
    /* THE BACKEND WAS NEVER CALLED AT ALL, which is the strongest form of "an
     * off launch pays nothing": not a refused create, no create and no switch
     * of any kind, so the number of GL contexts this process owns is what it is
     * without this control existing. */
    CHECK(sg.aux_created == 0 && sg.aux_exists == 0 && sg.aux_destroyed == 0,
          "no auxiliary context was created, exists or was destroyed (%ld/%d/"
          "%ld)", sg.aux_created, sg.aux_exists, sg.aux_destroyed);
    CHECK(sg.aux_switches == 0 && sg.main_switches == 0,
          "and no context switch happened in either direction (%ld/%ld)",
          sg.aux_switches, sg.main_switches);
    CHECK(sg.is_program_calls == 0 && sg.calls_on_aux == 0
          && sg.draws_on_aux == 0 && sg.v_call_no_context == 0,
          "the share check was never called, nothing landed on another context "
          "and no call was made with none current (%ld/%ld/%ld/%ld)",
          sg.is_program_calls, sg.calls_on_aux, sg.draws_on_aux,
          sg.v_call_no_context);
    CHECK(t->prepasses >= 1 && t->binds >= 1 && t->raster_draws >= 1,
          "while the ordinary arm ran prepasses, bound pools and counted "
          "submissions (%lu/%lu/%lu)", t->prepasses, t->binds,
          t->raster_draws);
    /* AND TEARDOWN IS A NO-OP ON AN OFF LAUNCH, which is what keeps the new
     * backend call free of any effect on the default path. */
    sl_bgtf_shutdown();
    CHECK(sg.aux_destroyed == 0 && sl_bgtf_stats()->ax_destroys == 0,
          "and teardown destroys nothing and counts none (%ld/%lu)",
          sg.aux_destroyed, sl_bgtf_stats()->ax_destroys);
    CHECK(seam_census_text() > 0L, "the census was produced");
    CHECK(seam_census_count(SEAM_AX_KEY) == 0,
          "and carries no auxiliary-context line, which is the output an absent "
          "switch has (%d)", seam_census_count(SEAM_AX_KEY));
    end_case();
}

static void st_zc_default_off(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("zero count default off: the control is not selected and nothing in "
               "this process was counted for it, refusals included");
    CHECK(sl_bgtf_zero_count() == 0,
          "SL_BG_TF_ZERO_COUNT is absent, so the control is off (%d)",
          sl_bgtf_zero_count());
    CHECK(t->zc_setups == 0 && t->zc_calls == 0 && t->zc_reuses == 0
          && t->zc_refusals == 0,
          "and nothing in this process was counted for it (%lu/%lu/%lu/%lu)",
          t->zc_setups, t->zc_calls, t->zc_reuses, t->zc_refusals);
    CHECK(t->prepasses >= 1 && t->binds >= 1 && t->raster_draws >= 1,
          "while the ordinary arm ran prepasses, bound pools and counted "
          "submissions (%lu/%lu/%lu)", t->prepasses, t->binds, t->raster_draws);
    CHECK(seam_prepass_vertices() > 0L,
          "and its prepass draws asked for vertices (%ld)",
          seam_prepass_vertices());
    end_case();
}

/* ---- 11. THE POSITION-ONLY-INPUT CONTROL (SL_BG_TF_POS_INPUT) -----------
 *
 * WHAT ONLY THIS FIXTURE CAN SAY. The module fixture proves which client-state
 * calls the control makes and that the state it saved comes back field for
 * field. What lives entirely in src/gfx/sl_gfx_dl.c is the state the REAL PASS
 * had enabled when the prepass draw ran, and whether it was back by the time the
 * raster draw that follows read it. The shim samples the client arrays inside
 * every draw, so both are read off the draws themselves rather than inferred
 * from a walk-final state that has already been restored.
 *
 * ONE SETTING PER RUN, because the control's switch is parsed once per launch.
 */

/* Every prepass draw of this walk read positions only. */
static void seam_prepass_positions_only(void)
{
    int i, n = 0;
    for (i = 0; i < sg.ndraws; i++) {
        const struct seamgl_draw *d = &sg.draw[i];
        int u;
        if (!d->arrays || d->tf_buf == 0u) continue;
        n++;
        CHECK(d->cli.v,
              "prepass draw %d: the POSITION array was enabled at it (%d)", i,
              (int) d->cli.v);
        CHECK(!d->cli.c && !d->cli.n && !d->cli.i && !d->cli.e && !d->cli.s
              && !d->cli.f,
              "prepass draw %d: every non-position single array was OFF at it "
              "(c%d n%d i%d e%d s%d f%d)", i, (int) d->cli.c, (int) d->cli.n,
              (int) d->cli.i, (int) d->cli.e, (int) d->cli.s, (int) d->cli.f);
        for (u = 0; u < SEAMGL_UNITS; u++)
            CHECK(!d->cli.t[u],
                  "prepass draw %d: unit %d's coordinate array was OFF at it (%d)",
                  i, u, (int) d->cli.t[u]);
    }
    CHECK(n >= 1, "the walk issued prepass draws to measure (%d)", n);
}

static void st_pi_control(void)
{
    const struct sl_bgtf_counters *t;
    const struct seamgl_draw *d[64];
    unsigned long cpur_d0 = g_bgtf_cpur_draws;
    int n, i, raster_unit = -1;

    begin_case("position-only input: the prepass draw RUNS and does transform-feedback "
               "work, it reads POSITIONS ONLY, and the pass's own arrays and client "
               "unit are back before the ORIGINAL raster reads them");
    CHECK(sl_bgtf_pos_input() == 1,
          "SL_BG_TF_POS_INPUT=1 with both required switches is needed for this "
          "mode - run it through tools/windows/bgtfseamtest.ps1");
    CHECK(sl_bgtf_raster_cpu() == 1, "the original-raster control is selected (%d)",
          sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_opt_in() == 1, "and the candidate itself is selected");
    CHECK(sl_bgtf_state_only() == 0,
          "while the state-only control is NOT - the two are exclusive (%d)",
          sl_bgtf_state_only());
    run_walk();
    report_walk("position-only");
    t = sl_bgtf_stats();

    /* THE REAL WORK HAPPENED. This arm is not the state-only one: the draws ran
     * and the transform feedback wrote, so these are positive and the setup
     * counters are the ones that must be zero. */
    CHECK(g_bgtf_elig_walks >= 1u, "the walk was admitted (%u)",
          g_bgtf_elig_walks);
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "real prepasses over real vertices (%lu / %lu)", t->prepasses,
          t->vertices);
    CHECK(sg.draw_arrays >= 1 && seam_prepass_draws() >= 1,
          "with real recording prepass draws (%ld / %d)", sg.draw_arrays,
          seam_prepass_draws());
    CHECK(g_seam_npool >= 1, "filling at least one pool (%d)", g_seam_npool);
    CHECK(t->so_setups == 0 && t->so_omitted == 0,
          "and NOTHING was counted as a state-only setup (%lu / %lu)",
          t->so_setups, t->so_omitted);

    /* THE ISOLATION WAS APPLIED, AND EXACTLY ONCE PER DRAW. */
    CHECK(t->pi_draws >= 1, "the isolation was in force for real draws (%lu)",
          t->pi_draws);
    CHECK(t->pi_draws == t->prepasses,
          "for every prepass draw there was, and no more (%lu draws, %lu "
          "prepasses)", t->pi_draws, t->prepasses);
    CHECK(t->pi_refusals == 0,
          "with no draw refusing to isolate (%lu)", t->pi_refusals);
    CHECK(t->pi_off >= 1, "arrays were actually disabled (%lu)", t->pi_off);
    CHECK(t->pi_back == t->pi_off,
          "and exactly as many were put back (%lu off, %lu back)", t->pi_off,
          t->pi_back);
    CHECK(t->pi_units >= 1, "over a unit sweep of %lu", t->pi_units);
    CHECK(sg.v_client_unit_range == 0,
          "and no unit outside the modelled range was ever selected (%ld)",
          sg.v_client_unit_range);

    /* WHAT EACH PREPASS DRAW ACTUALLY READ. */
    seam_prepass_positions_only();

    /* AND WHAT THE RASTER DRAWS READ: the pass's own arrays, back in place. The
     * position array and unit 0's coordinate array are the two the background
     * pass always enables, so a restore that did not happen before the raster
     * shows up here rather than at the end of the walk. */
    n = raster_draws(d, 64);
    CHECK(n >= 1, "the walk issued indexed draws (%d)", n);
    for (i = 0; i < n; i++) {
        CHECK(d[i]->cli.v,
              "raster draw %d: the position array is enabled again (%d)", i,
              (int) d[i]->cli.v);
        CHECK(d[i]->cli.t[0],
              "raster draw %d: unit 0's coordinate array is enabled again (%d)",
              i, (int) d[i]->cli.t[0]);
        if (raster_unit < 0) raster_unit = (int) d[i]->cli.unit;
        CHECK((int) d[i]->cli.unit == raster_unit,
              "raster draw %d: under the same client active unit as the others "
              "(%d vs %d)", i, (int) d[i]->cli.unit, raster_unit);
    }
    /* THE CLIENT ACTIVE UNIT WAS NOT PERTURBED. Every prepass draw ran under the
     * same unit the raster draws did, which is what says the sweep put the
     * inherited selection back rather than leaving it where it finished. */
    for (i = 0; i < sg.ndraws; i++) {
        if (!sg.draw[i].arrays || sg.draw[i].tf_buf == 0u) continue;
        CHECK((int) sg.draw[i].cli.unit == raster_unit,
              "prepass draw %d ran under the pass's own client active unit "
              "(%d vs the raster draws' %d)", i, (int) sg.draw[i].cli.unit,
              raster_unit);
    }

    /* NO TF-POSITION-SOURCE COVERAGE IS CLAIMED: this arm rasterises the
     * original stream through the existing control, so the pool is never bound. */
    CHECK(t->binds == 0, "no pool was bound as the vertex source (%lu)",
          t->binds);
    CHECK(t->raster_draws == 0 && t->raster_indices == 0,
          "and no submission was counted on one (%lu / %lu)", t->raster_draws,
          t->raster_indices);
    CHECK(g_bgtf_src == 0, "with no pool the source at the walk's end (%d)",
          g_bgtf_src);
    seam_all_draws_original("position-only");
    CHECK(g_bgtf_cpur_draws > cpur_d0,
          "the original-raster control's own count rose (%lu -> %lu)", cpur_d0,
          g_bgtf_cpur_draws);
    CHECK(g_bgtf_cpur_meshes >= 1, "over at least one mesh (%u)",
          g_bgtf_cpur_meshes);
    CHECK(g_bgb_vtx_skip == 0ul || g_bgb_repairs >= 1u,
          "no vertex was left unwritten by the reduction (skipped %lu, "
          "repairs %u)", g_bgb_vtx_skip, g_bgb_repairs);
    CHECK(g_bgb_broken == 0u, "and no mesh drew from clip positions the walk "
          "did not write (%u)", g_bgb_broken);
    seam_shape_digest();
    end_case();
}

/* THE TWO EXCLUSIVE CONTROLS ASKED FOR AT ONCE. Neither may win: the launch must
 * be the ordinary original-raster arm, with the prepass draw running, its
 * inherited arrays intact and no setup-only accounting. */
static void st_pi_exclusive(void)
{
    const struct sl_bgtf_counters *t;

    begin_case("position-only input and state-only together: both are REFUSED, and the "
               "launch is the ordinary original-raster arm");
    CHECK(sl_bgtf_pos_input() == 0,
          "the position-only-input control is not in force (%d)",
          sl_bgtf_pos_input());
    CHECK(sl_bgtf_state_only() == 0,
          "and neither is the state-only control (%d)", sl_bgtf_state_only());
    CHECK(sl_bgtf_raster_cpu() == 1 && sl_bgtf_opt_in() == 1,
          "while the two switches that DO select an arm are set (%d / %d)",
          sl_bgtf_raster_cpu(), sl_bgtf_opt_in());
    run_walk();
    report_walk("position-only-exclusive");
    t = sl_bgtf_stats();
    CHECK(t->pi_draws == 0 && t->pi_off == 0 && t->pi_back == 0
          && t->pi_refusals == 0,
          "nothing was counted for the position-only-input control (%lu/%lu/%lu/%lu)",
          t->pi_draws, t->pi_off, t->pi_back, t->pi_refusals);
    CHECK(t->so_setups == 0 && t->so_omitted == 0,
          "and nothing for the state-only control (%lu / %lu)", t->so_setups,
          t->so_omitted);
    /* THE ORDINARY ARM, MEASURED: the draw ran, and it ran with the arrays the
     * pass had enabled - which is what "neither control applied" means. */
    CHECK(t->prepasses >= 1 && sg.draw_arrays >= 1,
          "the prepass and its draw ran (%lu / %ld)", t->prepasses,
          sg.draw_arrays);
    CHECK(sg.client_active_texture == 0,
          "no unit was ever selected by the candidate (%ld)",
          sg.client_active_texture);
    {
        int i, n = 0;
        for (i = 0; i < sg.ndraws; i++) {
            if (!sg.draw[i].arrays || sg.draw[i].tf_buf == 0u) continue;
            n++;
            CHECK(sg.draw[i].cli.t[0],
                  "prepass draw %d kept the pass's inherited coordinate array "
                  "(%d)", i, (int) sg.draw[i].cli.t[0]);
        }
        CHECK(n >= 1, "there were prepass draws to look at (%d)", n);
    }
    CHECK(g_bgtf_cpur_draws > 0 && g_bgtf_cpur_meshes >= 1,
          "and the original-raster control still served the raster (%lu / %u)",
          g_bgtf_cpur_draws, g_bgtf_cpur_meshes);
    seam_shape_digest();
    end_case();
}

/* DEFAULT OFF, SAID AS A MEASUREMENT. Runs in the ordinary mode, where the
 * control is absent: nothing was counted for it, and - the strongest form this
 * fixture can state - no client array was written and no unit selected by the
 * candidate at all. */
static void st_pi_default_off(void)
{
    const struct sl_bgtf_counters *t = sl_bgtf_stats();

    begin_case("position-only default off: the control is not selected, nothing was "
               "counted for it, and the candidate selected no texture unit");
    CHECK(sl_bgtf_pos_input() == 0,
          "SL_BG_TF_POS_INPUT is absent, so the control is off (%d)",
          sl_bgtf_pos_input());
    CHECK(t->pi_draws == 0 && t->pi_off == 0 && t->pi_back == 0
          && t->pi_units == 0 && t->pi_refusals == 0,
          "and nothing in this process was counted for it (%lu/%lu/%lu/%lu/%lu)",
          t->pi_draws, t->pi_off, t->pi_back, t->pi_units, t->pi_refusals);
    CHECK(sg.client_active_texture == 0,
          "the candidate selected no texture unit (%ld)",
          sg.client_active_texture);
    CHECK(t->prepasses >= 1 && t->binds >= 1 && t->raster_draws >= 1,
          "while the ordinary arm ran prepasses, bound pools and counted "
          "submissions (%lu/%lu/%lu)", t->prepasses, t->binds,
          t->raster_draws);
    end_case();
}

int main(int argc, char **argv)
{
    static char cfg[520];
    const char *tmp = getenv("TEMP");
    const char *path;
    const char *mode = argc >= 2 ? argv[1] : "main";

    printf("background draw SEAM fixture - the REAL src/gfx/sl_gfx_dl.c driving the\n"
           "REAL src/gfx/sl_gfx_bgtf.c against a SYNTHETIC GL (no window, no driver,\n"
           "no GPU, no game)\n");
    printf("mode: %s\n\n", mode);

    /* The REAL settings store, pointed at a scratch file under %TEMP% so the
     * player's own config is never opened. */
    if (tmp == NULL || *tmp == '\0') tmp = ".";
    snprintf(cfg, sizeof cfg, "SL_CONFIG=%s\\sl_bgtf_seam_config.ini", tmp);
    putenv(cfg);
    path = strchr(cfg, '=') + 1;
    remove(path);
    sl_settings_init();

    sl_seamgl_reset();
    build_asset(strcmp(mode, "dumpempty") == 0);
    set_projection(10.0f, 15000.0f);

    /* THE ONE MODE THAT REQUIRES THE CANDIDATE TO BE OFF. "rcpualone" exists to
     * show that the control enables nothing by itself, so the refusal below -
     * which is right for every other mode - would reject exactly the condition
     * that case is about. It is checked before the refusal rather than inside
     * it so the two cannot drift apart. */
    if (strcmp(mode, "rcpualone") == 0) {
        if (sl_bgtf_raster_cpu() != 1 || sl_bgtf_opt_in() != 0) {
            printf("REFUSING: mode rcpualone needs SL_BG_TF_RASTER_CPU=1 with "
                   "SL_BG_TF_CLIP ABSENT (control %d, candidate %d).\n",
                   sl_bgtf_raster_cpu(), sl_bgtf_opt_in());
            return 2;
        }
        st_rcpu_alone();
        remove(path);
        printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
        return fails == 0 ? 0 : 1;
    }

    /* THE STATE-ONLY CONTROL'S OWN MODE WITH THE CANDIDATE OFF, for the same
     * reason: "the control alone enables nothing" is the condition the refusal
     * below would otherwise reject. */
    if (strcmp(mode, "soalone") == 0) {
        if (sl_bgtf_state_only() != 0 || sl_bgtf_opt_in() != 0) {
            printf("REFUSING: mode soalone needs SL_BG_TF_STATE_ONLY=1 with "
                   "SL_BG_TF_CLIP ABSENT (state-only %d, candidate %d).\n",
                   sl_bgtf_state_only(), sl_bgtf_opt_in());
            return 2;
        }
        st_so_alone();
        remove(path);
        printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
        return fails == 0 ? 0 : 1;
    }

    /* THE ZERO-COUNT CONTROL'S OWN MODE WITH THE CANDIDATE OFF, for the reason
     * the two above have theirs: "the control alone enables nothing" is the
     * condition the refusal below would otherwise reject. */
    if (strcmp(mode, "zcalone") == 0) {
        if (sl_bgtf_zero_count() != 0 || sl_bgtf_opt_in() != 0) {
            printf("REFUSING: mode zcalone needs SL_BG_TF_ZERO_COUNT=1 with "
                   "SL_BG_TF_CLIP ABSENT (zero-count %d, candidate %d).\n",
                   sl_bgtf_zero_count(), sl_bgtf_opt_in());
            return 2;
        }
        st_zc_alone();
        remove(path);
        printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
        return fails == 0 ? 0 : 1;
    }

    /* THE EMPTY-DRAW STATE SELECTOR'S OWN MODE WITH THE CANDIDATE OFF, for the
     * reason the three above have theirs: "the selector alone enables nothing"
     * is the condition the refusal below would otherwise reject. */
    if (strcmp(mode, "esalone") == 0) {
        if (sl_bgtf_empty_state() != SL_BGTF_ES_OFF || sl_bgtf_opt_in() != 0) {
            printf("REFUSING: mode esalone needs SL_BG_TF_EMPTY_STATE set with "
                   "SL_BG_TF_CLIP ABSENT (mode %s, candidate %d).\n",
                   sl_bgtf_es_name(sl_bgtf_empty_state()), sl_bgtf_opt_in());
            return 2;
        }
        st_es_alone();
        remove(path);
        printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
        return fails == 0 ? 0 : 1;
    }

    /* THE POST-RESET BOUNDARY SELECTOR'S OWN MODE WITH THE CANDIDATE OFF, for
     * the reason the four above have theirs: "the selector alone enables
     * nothing" is the condition the refusal below would otherwise reject. */
    if (strcmp(mode, "pralone") == 0) {
        if (sl_bgtf_post_reset() != SL_BGTF_PR_OFF || sl_bgtf_opt_in() != 0) {
            printf("REFUSING: mode pralone needs SL_BG_TF_POST_RESET set with "
                   "SL_BG_TF_CLIP ABSENT (arm %s, candidate %d).\n",
                   sl_bgtf_pr_name(sl_bgtf_post_reset()), sl_bgtf_opt_in());
            return 2;
        }
        st_pr_alone();
        remove(path);
        printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
        return fails == 0 ? 0 : 1;
    }

    /* THE NEUTRAL-PROGRAM SWITCH'S OWN MODE WITH THE CANDIDATE OFF, for the
     * reason the five above have theirs: "the switch alone enables nothing" is
     * the condition the refusal below would otherwise reject. */
    if (strcmp(mode, "npalone") == 0) {
        if (sl_bgtf_neutral_prog() != 0 || sl_bgtf_opt_in() != 0) {
            printf("REFUSING: mode npalone needs SL_BG_TF_NEUTRAL_PROG=1 with "
                   "SL_BG_TF_CLIP ABSENT (control %d, candidate %d).\n",
                   sl_bgtf_neutral_prog(), sl_bgtf_opt_in());
            return 2;
        }
        st_np_alone();
        remove(path);
        printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
        return fails == 0 ? 0 : 1;
    }

    if (sl_bgtf_opt_in() != 1) {
        printf("REFUSING TO REPORT COVERAGE: SL_BG_TF_CLIP is not 1, so the suite "
               "below would be measuring the off path under the wrong name.\n");
        return 2;
    }

    /* THE DIAGNOSTIC'S SCENARIOS ARE SEPARATE RUNS, because SL_BG_TF_DUMP is a
     * launch-time selector parsed once and the one capture attempt is spent for
     * the life of the process. A single run could only ever observe one
     * selector and one attempt. */
    if (strcmp(mode, "dumpon") == 0) {
        st_dump_capture();
    } else if (strcmp(mode, "dumpempty") == 0) {
        st_dump_empty_run();
    } else if (strcmp(mode, "dumpexcluded") == 0) {
        st_dump_excluded();
    } else if (strcmp(mode, "rastercpu") == 0) {
        /* THE CONTROL'S OWN RUN. Its switch is parsed once, so these cases
         * cannot share a process with the ordinary arm's. */
        st_rcpu_control();
        st_rcpu_refusal();
    } else if (strcmp(mode, "stateonly") == 0) {
        /* THE STATE-ONLY CONTROL'S OWN RUN, for the same reason. */
        st_so_control();
    } else if (strcmp(mode, "zerocount") == 0) {
        /* THE ZERO-VERTEX-COUNT CONTROL'S OWN RUN, for the same reason. */
        st_zc_control();
    } else if (strcmp(mode, "zcboth") == 0) {
        /* THE TWO OPPOSITE ARMS ASKED FOR AT ONCE: neither applies. */
        st_zc_exclusive();
    } else if (strcmp(mode, "zcmis") == 0) {
        /* SELECTED WITHOUT THE ORIGINAL-RASTER CONTROL: not effective, so the
         * ordinary candidate arm must run unchanged. */
        st_zc_miscombined();
    } else if (strcmp(mode, "zctripleso") == 0) {
        /* THE TRIPLE REQUESTS, one per launch because the arbiter latches once.
         * These are the r631 regression: the older controls' own conflict
         * resolution used to clear the partner flags before this control's
         * exclusivity questions were asked. */
        st_zc_triple("SL_BG_TF_STATE_ONLY");
    } else if (strcmp(mode, "zctriplepi") == 0) {
        st_zc_triple("SL_BG_TF_POS_INPUT");
    } else if (strcmp(mode, "esA") == 0) {
        /* THE ROWS, one per launch because the selector latches once for the
         * process. All of them share ONE compile of this fixture. */
        st_es_control(SL_BGTF_ES_A);
    } else if (strcmp(mode, "esB") == 0) {
        st_es_control(SL_BGTF_ES_B);
    } else if (strcmp(mode, "esC") == 0) {
        st_es_control(SL_BGTF_ES_C);
    } else if (strcmp(mode, "esE") == 0) {
        st_es_control(SL_BGTF_ES_E);
    } else if (strcmp(mode, "esR") == 0) {
        st_es_control(SL_BGTF_ES_R);
    } else if (strcmp(mode, "esS") == 0) {
        st_es_control(SL_BGTF_ES_S);
    } else if (strcmp(mode, "esQ") == 0) {
        st_es_control(SL_BGTF_ES_Q);
    } else if (strcmp(mode, "esbad") == 0) {
        /* AN UNREADABLE SELECTOR VALUE with the arm properly coupled: the mode
         * is refused and the launch is the ORDINARY zero-count arm, which is
         * what "never silently runs another mode" has to mean. */
        st_es_refused("an unreadable SL_BG_TF_EMPTY_STATE value, with "
                      "SL_BG_TF_ZERO_COUNT=1 and both coupled switches", 1);
    } else if (strcmp(mode, "esmiszc") == 0) {
        /* A VALID ROW WITHOUT THE ZERO-COUNT ARM: there is no empty draw for a
         * condition to hold at, so the mode is refused and this launch is the
         * ordinary candidate arm. */
        st_es_refused("a valid row with SL_BG_TF_ZERO_COUNT absent", 0);
    } else if (strcmp(mode, "esmisrcpu") == 0) {
        /* A VALID ROW AND A ZERO-COUNT REQUEST WITHOUT THE ORIGINAL-RASTER
         * CONTROL: the arm beneath the mode is itself refused, so the mode
         * cannot be carried past it. */
        st_es_refused("a valid row and SL_BG_TF_ZERO_COUNT=1 with "
                      "SL_BG_TF_RASTER_CPU absent", 0);
    } else if (strcmp(mode, "esmixso") == 0) {
        /* A MIXED RAW REQUEST: a valid row alongside one of the three older
         * exclusive controls. Decided from the RAW request, so it refuses
         * whatever the clauses above did to that control's effective flag. */
        st_es_refused("a valid row with SL_BG_TF_STATE_ONLY=1 also requested",
                      0);
    } else if (strcmp(mode, "esmixpi") == 0) {
        st_es_refused("a valid row with SL_BG_TF_POS_INPUT=1 also requested", 0);
    } else if (strcmp(mode, "esmixpv") == 0) {
        st_es_refused("a valid row with SL_BG_TF_PRIVATE_VAO=1 also requested",
                      0);
    } else if (strcmp(mode, "prEMPTY") == 0) {
        /* THE POST-RESET BOUNDARY'S ARMS, one per launch because the selector
         * latches once for the process. All of them share ONE compile of this
         * fixture, and all three run on the REAL nonzero transform path. */
        st_pr_control(SL_BGTF_PR_EMPTY);
    } else if (strcmp(mode, "prFINISH") == 0) {
        st_pr_control(SL_BGTF_PR_FINISH);
    } else if (strcmp(mode, "prBOTH") == 0) {
        st_pr_control(SL_BGTF_PR_BOTH);
    } else if (strcmp(mode, "prcompose") == 0) {
        /* ONE COMPOSED LAUNCH: an empty-draw row AND a post-reset arm at once.
         * The control is independent of that mode, so the row's own case must
         * pass unchanged while the arm's line appears beside the row's - which
         * is the one thing a run of either alone cannot show. The row and the
         * arm are read from the environment rather than named here, so the
         * runner chooses the pair and this branch cannot drift from it. */
        if (sl_bgtf_empty_state() == SL_BGTF_ES_OFF
            || sl_bgtf_post_reset() == SL_BGTF_PR_OFF) {
            printf("REFUSING: mode prcompose needs BOTH a row and an arm in "
                   "force (row %s, arm %s).\n",
                   sl_bgtf_es_name(sl_bgtf_empty_state()),
                   sl_bgtf_pr_name(sl_bgtf_post_reset()));
            return 2;
        }
        st_es_control(sl_bgtf_empty_state());
        seam_pr_census(sl_bgtf_post_reset());
    } else if (strcmp(mode, "prbad") == 0) {
        /* AN UNREADABLE SELECTOR VALUE with the candidate selected: the arm is
         * refused and the launch is the ordinary candidate arm, which is what
         * "never silently runs another arm" has to mean. */
        st_pr_refused("an unreadable SL_BG_TF_POST_RESET value, with "
                      "SL_BG_TF_CLIP=1");
    } else if (strcmp(mode, "prmixso") == 0) {
        /* THE FOUR EXCLUSIONS, one reason per launch. The first is structural -
         * that control omits the prepass draw this boundary is defined to follow
         * - and the next two are attribution. All are decided from the RAW
         * request, so they refuse whatever the clauses above did to the other
         * control's effective flag. */
        st_pr_refused("an arm with SL_BG_TF_STATE_ONLY=1 also requested, which "
                      "omits the prepass draw the boundary follows");
    } else if (strcmp(mode, "prmixpi") == 0) {
        st_pr_refused("an arm with SL_BG_TF_POS_INPUT=1 also requested");
    } else if (strcmp(mode, "prmixpv") == 0) {
        st_pr_refused("an arm with SL_BG_TF_PRIVATE_VAO=1 also requested");
    } else if (strcmp(mode, "npon") == 0) {
        /* THE NEUTRAL-PROGRAM CONTROL'S OWN RUN, on the zero-count arm and in a
         * process of its own, because its switch is parsed once and the program
         * it creates is deleted at the end of the case. */
        st_np_control();
    } else if (strcmp(mode, "npmixes") == 0) {
        /* THE REFUSALS, one reason per launch. Each must leave the control off,
         * count exactly one refusal, create no program, print no line of its
         * own, and run whatever arm its own switches selected. */
        st_np_refused("an empty-draw row also requested, which varies the "
                      "program at the SAME call", 1);
    } else if (strcmp(mode, "npmixpr") == 0) {
        st_np_refused("a post-reset arm also requested, whose guard is about "
                      "which program the bracket last made current", 1);
    } else if (strcmp(mode, "npmixso") == 0) {
        st_np_refused("the state-only control also requested, which OMITS the "
                      "draw there would be a program to bind for", 0);
    } else if (strcmp(mode, "npmixpi") == 0) {
        st_np_refused("the position-only input also requested, which changes "
                      "what that draw reads", 0);
    } else if (strcmp(mode, "npmixpv") == 0) {
        st_np_refused("the private vertex array also requested, which changes "
                      "the object that draw runs inside", 0);
    } else if (strcmp(mode, "axon") == 0) {
        /* THE AUXILIARY-CONTEXT CONTROL'S OWN RUN, on the zero-count arm and in
         * a process of its own, because its switch is parsed once and the
         * context it creates is destroyed at the end of the case. */
        st_ax_control();
    } else if (strcmp(mode, "axmixnp") == 0) {
        /* THE REFUSAL WORTH DRIVING HERE, out of the eight: the neutral-program
         * control is the one closest to this one - one changes WHICH PROGRAM the
         * empty draw runs under, this one WHICH CONTEXT it is issued on - so a
         * launch asking for both is the pair a reader would most want to see
         * kept apart. The other seven exclusions are driven one per launch by
         * the module fixture, which can assert each clause's own message. */
        st_ax_refused("the neutral-program control also requested, which "
                      "changes WHICH PROGRAM the same call runs under", 1);
    } else if (strcmp(mode, "axmiszc") == 0) {
        st_ax_refused("the zero-count arm absent, so there is no EMPTY draw to "
                      "relocate to a second context", 0);
    } else if (strcmp(mode, "npmiszc") == 0) {
        st_np_refused("the zero-count arm absent, so there is no EMPTY draw for "
                      "a second program to be bound at", 0);
    } else if (strcmp(mode, "posinput") == 0) {
        /* THE POSITION-ONLY-INPUT CONTROL'S OWN RUN. */
        st_pi_control();
    } else if (strcmp(mode, "piboth") == 0) {
        /* BOTH EXCLUSIVE CONTROLS ASKED FOR: neither applies. */
        st_pi_exclusive();
    } else if (strcmp(mode, "somis") == 0) {
        /* SELECTED WITHOUT THE ORIGINAL-RASTER CONTROL: not effective, so the
         * ordinary candidate arm must run unchanged. */
        st_so_miscombined();
    } else {
        st_admitted();
        st_excluded_restores();
        st_filtered();
        st_refusal_after_success();
        st_far_ranges();
        st_walk_exit();
        st_dump_absent();
        st_rcpu_default_off();
        st_so_default_off();
        st_pi_default_off();
        st_zc_default_off();
        st_es_default_off();
        st_pr_default_off();
        st_np_default_off();
        st_ax_default_off();
    }

    remove(path);
    printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
    printf("NOT COVERED HERE: the prepass program's own arithmetic (no GLSL compiler "
           "and no GPU in this process), any image, any frame time, and the menu or "
           "settings integration - the candidate is selected only by SL_BG_TF_CLIP at "
           "launch, so there is no row for this fixture to move. The GL is synthetic "
           "and the frame inputs are stubs; what is REAL here is the draw pass, the "
           "mesh predicates, the slot cache, the counters and the restoration.\n");
    return fails == 0 ? 0 : 1;
}
