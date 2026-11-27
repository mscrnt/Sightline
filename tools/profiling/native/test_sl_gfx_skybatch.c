/* Offline tests for the skydome batch module (src/gfx/sl_gfx_skybatch.c).
 *
 * WHAT THESE TESTS ARE. A pure mock executable: the module compiled with
 * -DSL_SKYBATCH_TEST against the SYNTHETIC GL in sl_gfx_skybatch_testgl.c. No
 * window, no native graphics API, no renderer, no game, no GPU. Every buffer
 * name, texture-unit count and inherited pointer below is FABRICATED; they
 * prove the module's logic and they prove nothing about any driver, any frame
 * or any pixel, and no frame rate is measured anywhere here.
 *
 * WHAT IS REAL IN THEM. The POLICY and the STORE. src/platform/sl_perf.c and
 * src/platform/sl_settings.c are linked in, not mocked, so the OPTIMIZED
 * RENDERING row these tests drive is the row the game reads, with its real
 * default and its real precedence against the developer switches. The settings
 * store is pointed at a scratch file under %TEMP% through SL_CONFIG, so no
 * player configuration is opened. src/gfx/sl_gfx_worldasset.c is linked in too:
 * the shade, the screen fraction and the cloud cast a prepared record carries
 * are the shipped ones.
 *
 * WHY SEVERAL MODES. Three pieces of module state have PROCESS lifetime by
 * construction and no reset exists for them - the atexit registration, the
 * terminal snapshot it prints and the once-per-process float-carrier marker -
 * so a property about "once per process" cannot be driven twice in one
 * process. Each mode is therefore its own executable run, exits 0 on pass and
 * nonzero on fail, and prints one `FAIL: <what>` line per failure.
 *
 * Usage: test_sl_gfx_skybatch.exe <mode>
 */
#include "sl_gfx_skybatch_testgl.h"

#include "sl_gfx_worldasset.h"
#include "sl_gfx_skybatch.h"
#include "../platform/sl_perf.h"
#include "../platform/sl_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The world-asset provider resolves an override directory from its host. The
 * offline suite has no asset tree and opens none, so this answers the current
 * directory - the sl_gfx_charskin.c / sl_gfx_texprov.c fixture idiom. */
const char *sl_asset_override_dir(void) { return "."; }

static int g_pass, g_fail;

#define CHECK(cond, ...) do {                                                 \
        if (cond) { g_pass++; }                                               \
        else {                                                                \
            g_fail++;                                                         \
            printf("FAIL: ");                                                 \
            printf(__VA_ARGS__);                                              \
            printf(" (line %d)\n", __LINE__);                                 \
        }                                                                     \
    } while (0)

/* ---- the synthetic asset ------------------------------------------------
 *
 * Eight vertices, eighteen indices and FOUR meshes, laid out so every answer
 * sl_skybatch_mesh can give is reachable and so "once per unique referenced
 * vertex" is a different number from "once per index" for every batched mesh:
 *
 *   mesh 0  CLOUD  first_index 0  count 6   references {0,1,2,3}  - 4 unique
 *   mesh 1  PLAIN  first_index 6  count 3   declined by design
 *   mesh 2  CLOUD  first_index 9  count 9   references {4,5,6,7}  - 4 unique
 *   mesh 3  CLOUD  first_index 0  count 0   empty: answers served, draws nothing
 *
 * so the aggregate is 11 unique references over 18 indices, the widest mesh is
 * 4 vertices and the element type is u16. The counts are inside every limit
 * sl_wa_decode_arrays validates, and every index names a vertex the asset
 * carries, which is what makes this a layout the parser would have accepted. */
static struct sl_wa_asset  A;
static struct sl_wa_vertex VTX[8];
static float               CLIP[8 * 4];
static const unsigned      IDX[18] = {
    0u, 1u, 2u,  2u, 1u, 3u,                 /* mesh 0 */
    4u, 5u, 6u,                              /* mesh 1 */
    4u, 5u, 6u,  6u, 5u, 7u,  7u, 4u, 5u     /* mesh 2 */
};

#define MESH0_UNIQUE 4L
#define MESH0_INDICES 6L
#define MESH2_UNIQUE 4L
#define MESH2_INDICES 9L
#define AGG_UNIQUE 11L
#define AGG_INDICES 18L

static void build_asset(void)
{
    unsigned i;
    memset(&A, 0, sizeof A);
    for (i = 0u; i < 8u; i++) {
        VTX[i].x = 100.0f + (float) i;
        VTX[i].y = 200.0f + 2.0f * (float) i;
        VTX[i].z = 300.0f + 3.0f * (float) i;
        VTX[i].u = 0.125f * (float) i;
        VTX[i].v = 0.0625f * (float) i;
        VTX[i].r = (unsigned char) (7u + 29u * i);
        VTX[i].g = (unsigned char) (11u + 31u * i);
        VTX[i].b = (unsigned char) (13u + 37u * i);
        VTX[i].a = (unsigned char) (255u - 5u * i);
        CLIP[i * 4u + 0u] = 1.0f + (float) i;
        CLIP[i * 4u + 1u] = -2.0f - (float) i;
        CLIP[i * 4u + 2u] = 0.5f * (float) i;
        CLIP[i * 4u + 3u] = 4.0f;             /* > 1.0e-6, the D-046 branch's test */
    }
    strcpy(A.path, "offline/skydome");
    A.vertex_count   = 8u;
    A.index_count    = 18u;
    A.material_count = 2u;
    A.mesh_count     = 4u;
    A.vertices = VTX;
    A.indices  = IDX;
    A.materials[0].flags = SL_WA_MAT_CLOUD_PLANE;
    A.materials[1].flags = 0u;
    A.meshes[0].material = 0u; A.meshes[0].first_index = 0u; A.meshes[0].index_count = 6u;
    A.meshes[1].material = 1u; A.meshes[1].first_index = 6u; A.meshes[1].index_count = 3u;
    A.meshes[2].material = 0u; A.meshes[2].first_index = 9u; A.meshes[2].index_count = 9u;
    A.meshes[3].material = 0u; A.meshes[3].first_index = 0u; A.meshes[3].index_count = 0u;
}

/* The material draw's shading state, as sky_draw fills it: the cloud flag comes
 * from the mesh's material, exactly as the caller reads it. `atm` is 0, so the
 * prepared colour is the bundle's own vertex colour and the uploaded bytes are
 * a value this test can state rather than a value it has to trust. */
static void shade_for(unsigned mesh, struct sl_skybatch_shade *s)
{
    memset(s, 0, sizeof *s);
    s->cloud_plane = (A.materials[A.meshes[mesh].material].flags
                      & SL_WA_MAT_CLOUD_PLANE) != 0u;
    s->atm = 0;
    s->scr = 0;
    s->fog[0]   = 40.0f;  s->fog[1]   = 50.0f;  s->fog[2]   = 60.0f;
    s->cloud[0] = 200.0f; s->cloud[1] = 210.0f; s->cloud[2] = 220.0f;
    s->cen[0] = 10.0f; s->cen[1] = 20.0f; s->cen[2] = 30.0f;
    s->cloud_eye[0] = 1.0f; s->cloud_eye[1] = 2.0f; s->cloud_eye[2] = 3.0f;
    s->cloud_height = 4000.0f;
    s->cloud_phase  = 0.25f;
    s->cloud_w = 64u;
    s->cloud_h = 64u;
}

/* ---- reading the recorded transcript ------------------------------------ */

static const struct sl_skbgl_call *last_of(int kind)
{
    long i;
    for (i = tg.logged - 1; i >= 0; i--)
        if (sl_skbgl_log[i].kind == kind) return &sl_skbgl_log[i];
    return NULL;
}

static long count_of(int kind)
{
    long i, n = 0;
    for (i = 0; i < tg.logged; i++) if (sl_skbgl_log[i].kind == kind) n++;
    return n;
}

static const struct sl_skbgl_call *last_upload_to(long target)
{
    long i;
    for (i = tg.logged - 1; i >= 0; i--)
        if (sl_skbgl_log[i].kind == SKBGL_BUFFERDATA && sl_skbgl_log[i].a == target)
            return &sl_skbgl_log[i];
    return NULL;
}

/* ---- the arming of the fake context ------------------------------------- */

static void context_up(void)
{
    sl_skbgl_reset();
    tg.have_buffers     = 1;
    tg.have_client_unit = 1;
    tg.max_texture_units = 4;
}

/* The real store, on a scratch file of this mode's own, removed first so the
 * row's REAL DEFAULT (1) is what the test starts from. */
static void settings_up(const char *mode)
{
    static char cfg[600];
    const char *tmp = getenv("TEMP");
    if (tmp == NULL || *tmp == '\0') tmp = ".";
    sprintf(cfg, "SL_CONFIG=%s\\sl_skybatch_testgl_%s.ini", tmp, mode);
    putenv(cfg);
    remove(strchr(cfg, '=') + 1);
    sl_settings_init();
    sl_perf_test_reset();
}

static void env_put(const char *kv)
{
    static char slots[6][96];
    static int next;
    if (next >= 6) next = 0;
    strcpy(slots[next], kv);
    putenv(slots[next]);
    next++;
    sl_skybatch_test_reset_switch();        /* the policy's cached answers go */
}

/* ---- what one served cloud mesh must have submitted ---------------------
 *
 * The record size, the three pointers' declared sizes, types, stride and
 * offsets, the usage hint, the draw's primitive, count, index type and element
 * offset, and the bytes actually staged for slot 0. The expected bytes come
 * from sl_skybatch_attr, the module's own pure formatter, so what is being
 * checked here is the UPLOAD - that the record the draw was told about is the
 * record that was sent, in the carrier the pass latched. */
static void check_submission(const char *what, int fmode, const struct sl_wa_asset *a,
                             unsigned mesh, long uniq, long ioff_elements)
{
    const struct sl_skbgl_call *bd = last_upload_to(GL_ARRAY_BUFFER);
    const struct sl_skbgl_call *vp = last_of(SKBGL_VERTEXPOINTER);
    const struct sl_skbgl_call *cp = last_of(SKBGL_COLORPOINTER);
    const struct sl_skbgl_call *tp = last_of(SKBGL_TEXCOORDPOINTER);
    const struct sl_skbgl_call *dr = last_of(SKBGL_DRAWELEMENTS);
    const long rec   = fmode > 0 ? 48L : 36L;
    const long toff  = fmode > 0 ? 32L : 20L;
    const long ctype = fmode > 0 ? (long) GL_FLOAT : (long) GL_UNSIGNED_BYTE;
    const long ic    = (long) a->meshes[mesh].index_count;
    struct sl_skybatch_shade s;
    struct sl_skybatch_vtx   bvtx;
    struct sl_skybatch_fvtx  fvtx;
    const unsigned           v0 = a->indices[a->meshes[mesh].first_index];

    CHECK(bd != NULL, "%s: no ARRAY_BUFFER upload", what);
    CHECK(vp != NULL && cp != NULL && tp != NULL, "%s: a pointer command is missing", what);
    CHECK(dr != NULL, "%s: no glDrawElements", what);
    if (bd == NULL || vp == NULL || cp == NULL || tp == NULL || dr == NULL) return;

    CHECK(bd->b == uniq * rec,
          "%s: uploaded %ld bytes, expected %ld (%ld records of %ld)",
          what, bd->b, uniq * rec, uniq, rec);
    CHECK(bd->c == (long) GL_STREAM_DRAW, "%s: usage hint is 0x%lx, expected GL_STREAM_DRAW",
          what, bd->c);
    CHECK(bd->d == 1, "%s: the upload carried no data pointer", what);

    CHECK(vp->a == 4 && vp->b == (long) GL_FLOAT && vp->c == rec && vp->off == 0,
          "%s: glVertexPointer(%ld, 0x%lx, %ld, %ld), expected (4, GL_FLOAT, %ld, 0)",
          what, vp->a, vp->b, vp->c, vp->off, rec);
    CHECK(cp->a == 4 && cp->b == ctype && cp->c == rec && cp->off == 16,
          "%s: glColorPointer(%ld, 0x%lx, %ld, %ld), expected (4, 0x%lx, %ld, 16)",
          what, cp->a, cp->b, cp->c, cp->off, ctype, rec);
    CHECK(tp->a == 4 && tp->b == (long) GL_FLOAT && tp->c == rec && tp->off == toff,
          "%s: glTexCoordPointer(%ld, 0x%lx, %ld, %ld), expected (4, GL_FLOAT, %ld, %ld)",
          what, tp->a, tp->b, tp->c, tp->off, rec, toff);
    CHECK(vp->d == (long) GL_TEXTURE0 && cp->d == (long) GL_TEXTURE0
          && tp->d == (long) GL_TEXTURE0,
          "%s: a pointer command ran with a client unit other than 0", what);

    CHECK(dr->a == (long) GL_TRIANGLES && dr->b == ic
          && dr->c == (long) GL_UNSIGNED_SHORT && dr->off == ioff_elements * 2L,
          "%s: glDrawElements(0x%lx, %ld, 0x%lx, %ld), expected "
          "(GL_TRIANGLES, %ld, GL_UNSIGNED_SHORT, %ld)",
          what, dr->a, dr->b, dr->c, dr->off, ic, ioff_elements * 2L);

    /* THE BYTES. Slot 0 holds the mesh's first referenced vertex, prepared
     * under this material's state; the float carriers copy the position and
     * the coordinate bit for bit and convert only the colour. */
    shade_for(mesh, &s);
    sl_skybatch_attr(&a->vertices[v0], &CLIP[(size_t) v0 * 4u], &s, &bvtx);
    if (fmode > 0) {
        memcpy(fvtx.clip, bvtx.clip, sizeof fvtx.clip);
        CHECK(sl_skybatch_float_colour(fmode, bvtx.col, fvtx.col) == 1,
              "%s: the carrier %d converted no colour", what, fmode);
        memcpy(fvtx.st, bvtx.st, sizeof fvtx.st);
        CHECK(tg.last_upload_bytes == uniq * rec
              && memcmp(tg.last_upload, &fvtx, sizeof fvtx) == 0,
              "%s: slot 0's uploaded 48-byte record is not the prepared record "
              "with its colour through the carrier", what);
    } else {
        CHECK(tg.last_upload_bytes == uniq * rec
              && memcmp(tg.last_upload, &bvtx, sizeof bvtx) == 0,
              "%s: slot 0's uploaded 36-byte record is not the prepared record", what);
    }
}

/* The ten counters, read from the inside. */
struct counters {
    unsigned long passes, idx_draws, idx_vertices, arr_draws, arr_vertices;
    unsigned long uniq, pass_refused, mesh_refused, declined, declined_vertices;
};

static void counters_read(struct counters *c)
{
    sl_skybatch_test_counters(&c->passes, &c->idx_draws, &c->idx_vertices,
                              &c->arr_draws, &c->arr_vertices, &c->uniq,
                              &c->pass_refused, &c->mesh_refused,
                              &c->declined, &c->declined_vertices);
}

/* ---- the foreign state a sky pass inherits ------------------------------
 *
 * NONZERO buffer bindings in both user-controlled slots and in three arrays, a
 * client active texture unit other than 0, arbitrary sizes, types, strides and
 * pointers, and every array the module does not set left ENABLED. This is the
 * state sl_gfx_dl.c's model, item and #88 background paths can leave standing
 * when sky_draw is reached, so it is the state the bracket has to survive. */
static void drive_foreign_state(void)
{
    const GLuint b1 = sl_skbgl_make_buffer();
    const GLuint b2 = sl_skbgl_make_buffer();
    const GLuint b3 = sl_skbgl_make_buffer();

    sl_skbgl_test_unit(GL_TEXTURE0);
    sl_skbgl_test_bind(GL_ARRAY_BUFFER, b1);
    glVertexPointer(3, GL_SHORT, 24, (const GLvoid *) (size_t) 48);
    sl_skbgl_test_bind(GL_ARRAY_BUFFER, b2);
    glColorPointer(3, GL_UNSIGNED_BYTE, 24, (const GLvoid *) (size_t) 64);
    glTexCoordPointer(2, GL_FLOAT, 20, (const GLvoid *) (size_t) 80);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    sl_skbgl_test_unit(GL_TEXTURE0 + 1);
    sl_skbgl_test_bind(GL_ARRAY_BUFFER, b3);
    glTexCoordPointer(4, GL_FLOAT, 32, (const GLvoid *) (size_t) 96);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    sl_skbgl_test_unit(GL_TEXTURE0 + 2);
    glTexCoordPointer(2, GL_SHORT, 8, (const GLvoid *) (size_t) 112);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_INDEX_ARRAY);
    glEnableClientState(GL_EDGE_FLAG_ARRAY);
    glEnableClientState(GL_SECONDARY_COLOR_ARRAY);
    glEnableClientState(GL_FOG_COORD_ARRAY);

    /* The two user-controlled bindings, and the inherited client unit, last. */
    sl_skbgl_test_bind(GL_ARRAY_BUFFER, b2);
    sl_skbgl_test_bind(GL_ELEMENT_ARRAY_BUFFER, b3);
    sl_skbgl_test_unit(GL_TEXTURE0 + 2);
}

static void check_watchdog(const char *what)
{
    CHECK(tg.v_draw_foreign_array == 0,
          "%s: %ld draw(s) issued with an array the module does not set still enabled",
          what, tg.v_draw_foreign_array);
    CHECK(tg.v_draw_no_element_buffer == 0,
          "%s: %ld draw(s) with no element buffer bound", what, tg.v_draw_no_element_buffer);
    CHECK(tg.v_use_after_delete == 0, "%s: %ld use(s) of a deleted buffer name",
          what, tg.v_use_after_delete);
    CHECK(tg.v_stale_name == 0, "%s: %ld use(s) of a name this context never handed out",
          what, tg.v_stale_name);
    CHECK(tg.v_call_after_context_destroyed == 0,
          "%s: %ld GL call(s) after the context was destroyed",
          what, tg.v_call_after_context_destroyed);
    CHECK(tg.v_program_proc == 0, "%s: %ld shader / uniform entry point(s) resolved",
          what, tg.v_program_proc);
    CHECK(tg.v_sync_proc == 0, "%s: %ld synchronisation entry point(s) resolved",
          what, tg.v_sync_proc);
    CHECK(tg.v_unknown_proc == 0, "%s: %ld unmodelled entry point(s) asked for",
          what, tg.v_unknown_proc);
    CHECK(tg.v_unknown_enum == 0, "%s: %ld call(s) carried a token the stub does not model",
          what, tg.v_unknown_enum);
    CHECK(tg.v_log_full == 0, "%s: the call log overflowed, so it is not a transcript", what);
}

/* ======================================================================== */
/* 1. THE ROW DRIVES THE PATH.                                              */
/* ======================================================================== */
static void m_row(void)
{
    struct sl_skybatch_shade cloud, plain;
    struct counters c;

    settings_up("row");
    context_up();
    build_asset();
    shade_for(0u, &cloud);
    shade_for(1u, &plain);

    /* OFF: nothing is asked of GL at all - not an entry point, not the
     * loader. The module allocates nothing and queries nothing. */
    sl_perf_opt_set(0);
    CHECK(sl_perf_opt() == 0, "the row did not take OFF");
    CHECK(sl_skybatch_on() == 0, "sl_skybatch_on answered 1 with the row OFF");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 0, "a pass opened with the row OFF");
    CHECK(tg.calls == 0, "%ld GL call(s) with the row OFF, expected none", tg.calls);
    CHECK(tg.proc_calls == 0, "%ld entry point(s) resolved with the row OFF", tg.proc_calls);
    counters_read(&c);
    CHECK(c.passes == 0ul, "%lu pass(es) counted with the row OFF", c.passes);
    CHECK(c.pass_refused == 0ul,
          "an off answer was counted as a refusal (%lu); pass_begin returns before "
          "any counter when the path is off", c.pass_refused);

    /* ON: the pass opens and a cloud mesh is served. */
    sl_perf_opt_set(1);
    CHECK(sl_skybatch_on() == 1, "sl_skybatch_on answered 0 with the row ON");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass refused with the row ON");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "the cloud mesh was not served");
    CHECK(tg.draw_calls == 1, "%ld draw(s) for one served cloud mesh", tg.draw_calls);
    check_submission("row ON", SL_SKYBATCH_FLOAT_PROD, &A, 0u, MESH0_UNIQUE, 0L);

    /* The plain mesh is DECLINED, and the decline makes no GL call. */
    {
        const long before = tg.logged;
        const long calls  = tg.calls;
        CHECK(sl_skybatch_mesh(1u, &plain) == SL_SKYBATCH_DECLINED,
              "the plain mesh was not declined");
        CHECK(tg.logged == before && tg.calls == calls,
              "a declined mesh made %ld GL call(s)", tg.calls - calls);
    }
    /* The empty cloud mesh answers served and draws nothing. */
    {
        const long draws = tg.draw_calls;
        struct sl_skybatch_shade empty;
        shade_for(3u, &empty);
        CHECK(sl_skybatch_mesh(3u, &empty) == SL_SKYBATCH_SERVED,
              "an empty mesh did not answer served");
        CHECK(tg.draw_calls == draws, "an empty mesh issued a draw");
    }
    sl_skybatch_pass_end();

    counters_read(&c);
    CHECK(c.passes == 1ul, "%lu pass(es) counted, expected 1", c.passes);
    CHECK(c.idx_draws == 1ul, "%lu indexed draw(s), expected 1", c.idx_draws);
    CHECK(c.idx_vertices == (unsigned long) MESH0_INDICES,
          "%lu indices submitted, expected %ld", c.idx_vertices, MESH0_INDICES);
    CHECK(c.uniq == (unsigned long) MESH0_UNIQUE,
          "%lu unique vertices prepared, expected %ld", c.uniq, MESH0_UNIQUE);
    CHECK(c.declined == 1ul, "%lu decline(s), expected 1", c.declined);
    CHECK(c.declined_vertices == 3ul, "%lu declined vertices, expected 3",
          c.declined_vertices);
    CHECK(c.mesh_refused == 0ul, "%lu mesh refusal(s)", c.mesh_refused);
    CHECK(c.arr_draws == 0ul && c.arr_vertices == 0ul,
          "the array counters are not zero; this candidate has no array path");
    check_watchdog("row");
}

/* ======================================================================== */
/* 2. OFF -> ON -> OFF -> ON, FOUR PASSES OVER THE SAME ASSET POINTER.      */
/*                                                                          */
/* The regression: the module used to latch itself unavailable the first     */
/* time it was asked while off (g_state = -1), and used to be unable to      */
/* acquire float staging for a topology it had already built - either of     */
/* which makes an ON pass after an OFF pass draw nothing.                   */
/* ======================================================================== */
static void m_flip(void)
{
    struct sl_skybatch_shade cloud;
    struct counters c;
    int i;

    settings_up("flip");
    context_up();
    build_asset();
    shade_for(0u, &cloud);

    for (i = 0; i < 4; i++) {
        const int on = (i % 2) == 1;          /* OFF, ON, OFF, ON */
        sl_perf_opt_set(on ? 1 : 0);
        sl_skbgl_log_reset();
        if (!on) {
            CHECK(sl_skybatch_pass_begin(&A, CLIP) == 0, "pass %d opened with the row OFF", i);
            CHECK(tg.calls == 0 && tg.proc_calls == 0,
                  "pass %d made %ld GL call(s) and %ld resolve(s) with the row OFF",
                  i, tg.calls, tg.proc_calls);
            CHECK(tg.draw_calls == 0, "pass %d drew with the row OFF", i);
            continue;
        }
        CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1,
              "pass %d refused with the row ON - the same asset pointer as the "
              "pass before it", i);
        sl_skbgl_log_reset();
        CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED,
              "pass %d did not serve the cloud mesh", i);
        CHECK(tg.draw_calls == 1, "pass %d issued %ld draw(s)", i, tg.draw_calls);
        {
            const struct sl_skbgl_call *dr = last_of(SKBGL_DRAWELEMENTS);
            CHECK(dr != NULL && dr->b == MESH0_INDICES,
                  "pass %d drew %ld indices, expected %ld", i,
                  dr != NULL ? dr->b : -1L, MESH0_INDICES);
        }
        check_submission("flip ON", SL_SKYBATCH_FLOAT_PROD, &A, 0u, MESH0_UNIQUE, 0L);
        sl_skybatch_pass_end();
    }

    counters_read(&c);
    CHECK(c.passes == 2ul, "%lu pass(es) opened over the four, expected 2", c.passes);
    CHECK(c.idx_draws == 2ul, "%lu indexed draw(s), expected 2", c.idx_draws);
    CHECK(c.idx_vertices == (unsigned long) (2L * MESH0_INDICES),
          "%lu indices submitted, expected %ld", c.idx_vertices, 2L * MESH0_INDICES);
    CHECK(c.uniq == (unsigned long) (2L * MESH0_UNIQUE),
          "%lu unique vertices prepared, expected %ld", c.uniq, 2L * MESH0_UNIQUE);
    CHECK(c.pass_refused == 0ul,
          "%lu pass refusal(s): an OFF pass is not a refusal, and an ON pass after "
          "one must not be refused either", c.pass_refused);
    CHECK(c.mesh_refused == 0ul, "%lu mesh refusal(s)", c.mesh_refused);
    check_watchdog("flip");
}

/* ======================================================================== */
/* 3. THE CARRIER.                                                          */
/*                                                                          */
/* Ordered so the BYTE arm runs first: the topology is then cached with no   */
/* float staging, and the prod pass that follows has to acquire staging for  */
/* a topology it did not build - the same-asset byte -> prod transition the  */
/* earlier revision refused for the life of the asset.                      */
/* ======================================================================== */
static void m_carrier(void)
{
    struct sl_skybatch_shade cloud;
    int allocated;
    unsigned long cap, markers;

    settings_up("carrier");
    context_up();
    build_asset();
    shade_for(0u, &cloud);

    /* (a) SL_SKY_BATCH=1 with no carrier switch: the arm's own default is
     *     BYTE, which is what such a launch has always uploaded. */
    env_put("SL_SKY_BATCH=1");
    CHECK(sl_skybatch_on() == 1, "SL_SKY_BATCH=1 did not arm the path");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_OFF,
          "SL_SKY_BATCH=1 with no carrier switch resolved carrier %d, expected byte",
          sl_skybatch_float_mode());
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the byte pass refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "the byte pass served nothing");
    check_submission("byte", SL_SKYBATCH_FLOAT_OFF, &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();
    sl_skybatch_test_float_state(&allocated, &cap, &markers);
    CHECK(allocated == 0, "the byte pass acquired float staging");
    CHECK(markers == 0ul, "the byte pass printed the float carrier marker");

    /* (b) THE ROW, on the SAME asset: prod, with staging acquired at
     *     pass_begin for a topology that was built without it. */
    env_put("SL_SKY_BATCH=");
    CHECK(sl_perf_opt() == 1, "the row is not ON by default in this store");
    CHECK(sl_skybatch_on() == 1, "the row did not select the path");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "the row resolved carrier %d, expected prod", sl_skybatch_float_mode());
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1,
          "the prod pass refused on a topology cached while the carrier was byte");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "the prod pass served nothing");
    check_submission("prod", SL_SKYBATCH_FLOAT_PROD, &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();
    sl_skybatch_test_float_state(&allocated, &cap, &markers);
    CHECK(allocated == 1, "the prod pass did not acquire float staging");
    CHECK(cap == (unsigned long) MESH0_UNIQUE,
          "the float staging holds %lu records, expected the widest mesh's %ld",
          cap, MESH0_UNIQUE);
    CHECK(markers == 1ul, "the float carrier marker printed %lu time(s), expected 1", markers);

    /* (c) SL_SKY_BATCH_FLOAT_COLOR=byte: the 36-byte upload again, on the same
     *     asset, after the float staging exists. */
    env_put("SL_SKY_BATCH_FLOAT_COLOR=byte");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_OFF,
          "the byte spelling resolved carrier %d", sl_skybatch_float_mode());
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the second byte pass refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "the second byte pass served nothing");
    check_submission("byte again", SL_SKYBATCH_FLOAT_OFF, &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();

    /* (d) div is reachable only through the developer switch, and it uploads
     *     48-byte records with a GL_FLOAT colour like prod. */
    env_put("SL_SKY_BATCH_FLOAT_COLOR=div");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_DIV,
          "the div spelling resolved carrier %d", sl_skybatch_float_mode());
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the div pass refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "the div pass served nothing");
    check_submission("div", SL_SKYBATCH_FLOAT_DIV, &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();

    /* (e) THE ROW NEVER PRODUCES div OR byte. With no switch at all the
     *     carrier is prod in BOTH positions of the row. */
    env_put("SL_SKY_BATCH_FLOAT_COLOR=");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "with no switch the row's carrier is %d, expected prod", sl_skybatch_float_mode());
    sl_perf_opt_set(0);
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "with the row OFF and no switch the carrier is %d, expected prod",
          sl_skybatch_float_mode());
    sl_perf_opt_set(1);
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "with the row ON and no switch the carrier is %d, expected prod",
          sl_skybatch_float_mode());
    /* An unrecognised spelling names no carrier, so the policy's own stands. */
    env_put("SL_SKY_BATCH_FLOAT_COLOR=PROD");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "an unrecognised carrier spelling moved the carrier to %d",
          sl_skybatch_float_mode());
    env_put("SL_SKY_BATCH_FLOAT_COLOR=");
    check_watchdog("carrier");
}

/* ======================================================================== */
/* 4. ONE COHERENT CARRIER PER PASS.                                        */
/*                                                                          */
/* The carrier is latched at pass_begin, so a row that moves between two     */
/* mesh draws of an open pass cannot leave one mesh's 36-byte records        */
/* described to the draw as 48-byte ones. The NEXT pass is the one that      */
/* sees the change.                                                         */
/* ======================================================================== */
static void m_latch(void)
{
    struct sl_skybatch_shade c0, c2;

    settings_up("latch");
    context_up();
    build_asset();
    shade_for(0u, &c0);
    shade_for(2u, &c2);

    CHECK(sl_perf_opt() == 1, "the row is not ON by default in this store");
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the prod pass refused");

    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &c0) == SL_SKYBATCH_SERVED, "mesh 0 was not served");
    check_submission("latch, first mesh", SL_SKYBATCH_FLOAT_PROD, &A, 0u, MESH0_UNIQUE, 0L);

    /* The row moves INSIDE the open pass. */
    sl_perf_opt_set(0);
    CHECK(sl_skybatch_on() == 0, "the row did not move OFF");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "the live carrier question answered %d", sl_skybatch_float_mode());

    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(2u, &c2) == SL_SKYBATCH_SERVED,
          "the second mesh of the open pass was not served after the row moved OFF");
    check_submission("latch, second mesh after the row moved", SL_SKYBATCH_FLOAT_PROD,
                     &A, 2u, MESH2_UNIQUE, 9L);
    sl_skybatch_pass_end();

    /* The NEXT pass is the one that sees it. */
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 0,
          "the pass after the row moved OFF still opened");
    CHECK(tg.calls == 0 && tg.proc_calls == 0,
          "%ld GL call(s) in the pass after the row moved OFF", tg.calls);
    sl_perf_opt_set(1);
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass after the row moved back ON refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &c0) == SL_SKYBATCH_SERVED, "the re-opened pass served nothing");
    check_submission("latch, after ON again", SL_SKYBATCH_FLOAT_PROD, &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();
    check_watchdog("latch");
}

/* ======================================================================== */
/* 5. A FLOAT ALLOCATION FAILURE IS THE ORIGINAL PATH, NOT A DEGRADED ONE.  */
/* ======================================================================== */
static void m_allocfail(void)
{
    struct sl_skybatch_shade cloud;
    struct sl_skbgl_state before, after;
    struct counters c0, c1;
    const char *diff;

    settings_up("allocfail");
    context_up();
    build_asset();
    shade_for(0u, &cloud);

    drive_foreign_state();
    sl_skbgl_log_reset();
    sl_skbgl_snapshot(&before);
    counters_read(&c0);

    sl_skybatch_test_fail_float_alloc(1);
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 0,
          "a prod pass opened with the float staging unallocatable");
    CHECK(tg.draw_calls == 0, "%ld draw(s) on a refused pass", tg.draw_calls);
    CHECK(last_upload_to(GL_ARRAY_BUFFER) == NULL,
          "a refused pass uploaded attribute records");
    sl_skbgl_snapshot(&after);
    diff = sl_skbgl_diff(&before, &after, 1);
    CHECK(diff == NULL,
          "a refused pass left GL changed; first difference: %s",
          diff != NULL ? diff : "");
    counters_read(&c1);
    CHECK(c1.pass_refused == c0.pass_refused + 1ul,
          "%lu pass refusal(s) counted, expected one more than %lu",
          c1.pass_refused, c0.pass_refused);
    CHECK(c1.passes == c0.passes, "a refused pass was counted as opened");

    /* Cleared, and the next pass succeeds - the refusal latched nothing. */
    sl_skybatch_test_fail_float_alloc(0);
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1,
          "the pass after the allocation failure was cleared still refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "that pass served nothing");
    check_submission("after the allocation failure cleared", SL_SKYBATCH_FLOAT_PROD,
                     &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();
    {
        const char *d2;
        sl_skbgl_snapshot(&after);
        d2 = sl_skbgl_diff(&before, &after, 0);
        CHECK(d2 == NULL, "the successful pass left GL changed; first difference: %s",
              d2 != NULL ? d2 : "");
    }
    check_watchdog("allocfail");
}

/* ======================================================================== */
/* 6. CONTEXT RECREATION, AND TEARDOWN WITH A PASS OPEN.                    */
/* ======================================================================== */
static void m_context(void)
{
    struct sl_skybatch_shade cloud;
    struct sl_skbgl_state before, after;
    GLuint foreign;
    long gen_before, proc_before;
    const char *diff;

    settings_up("context");
    context_up();
    build_asset();
    shade_for(0u, &cloud);

    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the first pass refused");
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "the first pass served nothing");
    sl_skybatch_pass_end();
    CHECK(tg.generated == 2, "%ld buffer name(s) handed out, expected 2", tg.generated);

    /* THE CONTEXT CHANGES. The module drops its names without touching GL;
     * the stub then advances its generation, so every name handed out before
     * now is stale and any use of one is refused and recorded. */
    sl_skbgl_log_reset();
    sl_skybatch_context_invalidate();
    CHECK(tg.calls == 0, "sl_skybatch_context_invalidate made %ld GL call(s)", tg.calls);
    CHECK(tg.deleted == 0, "sl_skybatch_context_invalidate deleted %ld name(s) - every one "
          "belonged to the context being left", tg.deleted);
    sl_skbgl_new_context();

    proc_before = tg.proc_calls;
    gen_before  = tg.generated;
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass after the context changed refused");
    /* Read the resolve and create counts HERE, before the measurement window
     * for the submission is opened: sl_skbgl_log_reset zeroes them, and
     * re-resolving and re-creating both happen inside pass_begin. */
    CHECK(tg.proc_calls > proc_before,
          "the pass after the context changed resolved no entry point");
    CHECK(tg.generated == gen_before + 2,
          "%ld name(s) handed out after the context changed, expected 2",
          tg.generated - gen_before);
    CHECK(tg.v_stale_name == 0,
          "%ld call(s) used a buffer name from before the context changed",
          tg.v_stale_name);
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED,
          "the pass after the context changed served nothing");
    check_submission("after the context changed", SL_SKYBATCH_FLOAT_PROD,
                     &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();
    CHECK(tg.v_stale_name == 0,
          "%ld call(s) in the served pass used a buffer name from before the "
          "context changed", tg.v_stale_name);

    /* TEARDOWN WITH A PASS OPEN. The saved state goes back while the context
     * is still current, and only the two names this module owns are deleted. */
    foreign = sl_skbgl_make_buffer();
    drive_foreign_state();
    sl_skbgl_log_reset();
    sl_skbgl_snapshot(&before);
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass before the shutdown refused");
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED,
          "the pass before the shutdown served nothing");
    sl_skybatch_shutdown();
    sl_skbgl_snapshot(&after);
    /* The current colour and coordinate are deliberately left at the last
     * submitted index's values - the glDrawElements rule in the module's
     * header - so they are not part of the bracket. */
    diff = sl_skbgl_diff(&before, &after, 0);
    CHECK(diff == NULL, "a shutdown with a pass open left GL changed; "
          "first difference: %s", diff != NULL ? diff : "");
    CHECK(tg.deleted == 2, "the shutdown deleted %ld name(s), expected the 2 it owns",
          tg.deleted);
    CHECK(sl_skbgl_buffer_alive(foreign), "the shutdown deleted a name it does not own");
    CHECK(tg.v_stale_name == 0, "%ld stale name(s) touched by the shutdown", tg.v_stale_name);
    CHECK(tg.v_use_after_delete == 0, "%ld name(s) used after deletion by the shutdown",
          tg.v_use_after_delete);
    check_watchdog("context");
}

/* ======================================================================== */
/* 7. THE GL STATE BRACKET.                                                 */
/* ======================================================================== */
static void m_bracket(void)
{
    struct sl_skybatch_shade c0, c1, c2;
    struct sl_skbgl_state before, after;
    const char *diff;
    long logged_before, calls_before;

    settings_up("bracket");
    context_up();
    build_asset();
    shade_for(0u, &c0);
    shade_for(1u, &c1);
    shade_for(2u, &c2);

    drive_foreign_state();
    sl_skbgl_log_reset();
    sl_skbgl_snapshot(&before);

    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass refused on inherited state");
    CHECK(sl_skybatch_mesh(0u, &c0) == SL_SKYBATCH_SERVED, "mesh 0 was not served");
    logged_before = tg.logged;
    calls_before  = tg.calls;
    CHECK(sl_skybatch_mesh(1u, &c1) == SL_SKYBATCH_DECLINED, "mesh 1 was not declined");
    CHECK(tg.logged == logged_before && tg.calls == calls_before,
          "the declined mesh made %ld GL call(s)", tg.calls - calls_before);
    CHECK(sl_skybatch_mesh(2u, &c2) == SL_SKYBATCH_SERVED, "mesh 2 was not served");
    CHECK(tg.draw_calls == 2, "%ld draw(s) for two served meshes", tg.draw_calls);
    sl_skybatch_pass_end();

    sl_skbgl_snapshot(&after);
    diff = sl_skbgl_diff(&before, &after, 0);
    CHECK(diff == NULL, "the pass did not put the inherited state back; "
          "first difference: %s", diff != NULL ? diff : "");
    /* The current colour and coordinate ARE redefined, from the last submitted
     * index of the last batched mesh. Not asserting their value here - that is
     * the module's own last-index rule - but they must not have been left at
     * the inherited ones either, because glDrawElements left them undefined. */
    CHECK(sl_skbgl_diff(&before, &after, 1) != NULL,
          "the pass drew and left the current colour and coordinate inherited, "
          "which glDrawElements leaves undefined");

    /* sl_skybatch_pass_end is idempotent and makes no further call. */
    sl_skbgl_log_reset();
    sl_skybatch_pass_end();
    CHECK(tg.calls == 0, "a second sl_skybatch_pass_end made %ld GL call(s)", tg.calls);

    check_watchdog("bracket");

    /* AFTER THE CONTEXT IS GONE. The shutdown runs while it is still current;
     * then the context is marked destroyed and the three calls a renderer can
     * still make are made. None may reach GL. */
    sl_skybatch_shutdown();
    sl_skbgl_log_reset();
    tg.context_destroyed = 1;
    sl_skybatch_context_invalidate();
    sl_skybatch_pass_end();
    sl_skybatch_report(NULL);
    CHECK(tg.v_call_after_context_destroyed == 0,
          "%ld GL call(s) after the context was destroyed",
          tg.v_call_after_context_destroyed);
    CHECK(tg.calls == 0, "%ld GL call(s) after the shutdown", tg.calls);
}

/* ======================================================================== */
/* 8. NO SYNCHRONISATION POINT, AND NO SHADER.                              */
/*                                                                          */
/* Checked by the BUILD first: the stub declares and defines no glFinish,    */
/* glFlush, glGetError, buffer map or pixel read, so a module that called    */
/* one would not link and this executable would not exist. What is left for  */
/* a test to measure is the only other way such a name can be reached on     */
/* this target - the loader - and that the pass is served without one.       */
/* ======================================================================== */
static void m_nosync(void)
{
    struct sl_skybatch_shade cloud;

    settings_up("nosync");
    context_up();
    build_asset();
    shade_for(0u, &cloud);

    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass refused");
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "the pass served nothing");
    CHECK(sl_skybatch_mesh(2u, &cloud) == SL_SKYBATCH_SERVED, "the second mesh was not served");
    sl_skybatch_pass_end();
    sl_skybatch_shutdown();

    CHECK(tg.v_sync_proc == 0,
          "%ld synchronisation entry point(s) asked for - a served pass may introduce none",
          tg.v_sync_proc);
    CHECK(tg.v_program_proc == 0,
          "%ld shader / uniform entry point(s) asked for - no shader is introduced",
          tg.v_program_proc);
    CHECK(tg.v_unknown_proc == 0,
          "%ld entry point(s) outside the five this module needs were asked for",
          tg.v_unknown_proc);
    CHECK(tg.proc_calls > 0, "no entry point was resolved at all, so this proves nothing");
    check_watchdog("nosync");
}

/* ======================================================================== */
/* 9. ONCE PER UNIQUE REFERENCED VERTEX, AND INSIDE THE RECORDED CAPACITY.  */
/* ======================================================================== */
static void m_attr(void)
{
    struct sl_skybatch_shade c0, c2;
    unsigned long uc, uu, ic, iu;
    unsigned long calls0, calls2;

    settings_up("attr");
    context_up();
    build_asset();
    shade_for(0u, &c0);
    shade_for(2u, &c2);

    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass refused");

    sl_skybatch_test_attr_calls = 0ul;
    CHECK(sl_skybatch_mesh(0u, &c0) == SL_SKYBATCH_SERVED, "mesh 0 was not served");
    calls0 = sl_skybatch_test_attr_calls;

    sl_skybatch_test_attr_calls = 0ul;
    CHECK(sl_skybatch_mesh(2u, &c2) == SL_SKYBATCH_SERVED, "mesh 2 was not served");
    calls2 = sl_skybatch_test_attr_calls;

    CHECK(calls0 == (unsigned long) MESH0_UNIQUE,
          "mesh 0 prepared %lu record(s); it references %ld unique vertices over "
          "%ld indices", calls0, MESH0_UNIQUE, MESH0_INDICES);
    CHECK(calls0 != (unsigned long) MESH0_INDICES,
          "mesh 0 prepared once per INDEX (%lu), which is the work this candidate "
          "exists to avoid", calls0);
    CHECK(calls2 == (unsigned long) MESH2_UNIQUE,
          "mesh 2 prepared %lu record(s); it references %ld unique vertices over "
          "%ld indices", calls2, MESH2_UNIQUE, MESH2_INDICES);
    CHECK(calls2 != (unsigned long) MESH2_INDICES,
          "mesh 2 prepared once per INDEX (%lu)", calls2);

    sl_skybatch_test_capacity(&uc, &uu, &ic, &iu);
    CHECK(uu <= uc, "the writing pass wrote %lu unique slot(s) into a capacity of %lu",
          uu, uc);
    CHECK(iu <= ic, "the writing pass wrote %lu compact index/indices into a capacity of %lu",
          iu, ic);
    CHECK(uc == (unsigned long) AGG_UNIQUE && uu == (unsigned long) AGG_UNIQUE,
          "the unique list is %lu/%lu, expected %ld/%ld - the aggregate over all "
          "meshes, not the asset's index count", uu, uc, AGG_UNIQUE, AGG_UNIQUE);
    CHECK(ic == (unsigned long) AGG_INDICES && iu == (unsigned long) AGG_INDICES,
          "the compact list is %lu/%lu, expected %ld/%ld", iu, ic, AGG_INDICES, AGG_INDICES);

    /* The element buffer was uploaded ONCE, at the checked size: one u16 per
     * aggregate index. */
    {
        const struct sl_skbgl_call *eb = last_upload_to(GL_ELEMENT_ARRAY_BUFFER);
        CHECK(eb != NULL && eb->b == AGG_INDICES * 2L,
              "the element upload is %ld bytes, expected %ld",
              eb != NULL ? eb->b : -1L, AGG_INDICES * 2L);
        CHECK(eb != NULL && eb->c == (long) GL_STATIC_DRAW,
              "the element upload's usage hint is not GL_STATIC_DRAW");
    }
    sl_skybatch_pass_end();

    /* A second pass over the same asset re-uses the topology and uploads no
     * element buffer again. */
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the second pass refused");
    CHECK(sl_skybatch_mesh(0u, &c0) == SL_SKYBATCH_SERVED, "the second pass served nothing");
    sl_skybatch_pass_end();
    CHECK(count_of(SKBGL_GENBUFFERS) == 0,
          "the second pass over the same asset created %ld buffer(s) again",
          count_of(SKBGL_GENBUFFERS));
    CHECK(last_upload_to(GL_ELEMENT_ARRAY_BUFFER) == NULL,
          "the second pass over the same asset uploaded the element buffer again");
    check_watchdog("attr");
}

/* ======================================================================== */
/* AN EXPLICIT MENU CHOICE GOVERNS AN INHERITED SUBSYSTEM SWITCH.           */
/*                                                                          */
/* The property is the policy's (src/platform/sl_perf.c, the s_row_chosen   */
/* half of the precedence rule), but it is driven HERE through the real      */
/* module, because what a player sees is not the policy's answer - it is     */
/* what the pass uploads. Two defects are removed by it and both are         */
/* observable only at this level: with SL_SKY_BATCH=1 inherited, a row set   */
/* to OFF left the indexed path running while both menus displayed OFF; and  */
/* a row set to ON in such a session left the 36-byte BYTE carrier in force  */
/* although the row only ever selects PROD.                                  */
/*                                                                          */
/* THE DISTINCTION THE CASE TURNS ON. The row is written two different ways  */
/* on purpose: sl_settings_set writes the stored value and leaves the        */
/* explicit-choice latch CLEAR, which is the state a launch-time diagnostic  */
/* runs in; sl_perf_opt_set is the menu press, and sets it. So each arm       */
/* below states which of the two it is exercising.                           */
/* ======================================================================== */
static void m_precedence(void)
{
    struct sl_skybatch_shade cloud;
    struct sl_skybatch_vtx   v0rec;
    struct counters c;
    float div_col[4], prod_col[4];

    settings_up("precedence");
    context_up();
    build_asset();
    shade_for(0u, &cloud);

    /* The two carriers must DISAGREE on this vertex's colour, or the "the
     * upload moved from one table to the other" checks below would pass with
     * nothing having moved. Measured, not assumed. */
    sl_skybatch_attr(&VTX[0], &CLIP[0], &cloud, &v0rec);
    CHECK(sl_skybatch_float_colour(SL_SKYBATCH_FLOAT_DIV,  v0rec.col, div_col) == 1
          && sl_skybatch_float_colour(SL_SKYBATCH_FLOAT_PROD, v0rec.col, prod_col) == 1
          && memcmp(div_col, prod_col, sizeof div_col) != 0,
          "the div and prod tables agree on vertex 0's colour, so a carrier "
          "change would not be visible in the upload");

    /* (1) THE INHERITED BYTE ARM. SL_SKY_BATCH=1 is in the environment and the
     *     player has NOT touched the row - the row is written straight to the
     *     store - so the subsystem switch governs its own subsystem and keeps
     *     the arm's own default carrier, the 36-byte upload. */
    env_put("SL_SKY_BATCH=1");
    sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
    CHECK(sl_perf_opt() == 1, "the stored row is not ON");
    CHECK(sl_perf_opt_env() == 0, "SL_PERF_OPT reports itself in force; it is not set here");
    CHECK(sl_skybatch_on() == 1, "SL_SKY_BATCH=1 did not arm the path");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_OFF,
          "the inherited arm resolved carrier %d, expected byte", sl_skybatch_float_mode());
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the inherited byte pass refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED,
          "the inherited byte pass served nothing");
    check_submission("inherited SL_SKY_BATCH=1, row untouched",
                     SL_SKYBATCH_FLOAT_OFF, &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();

    /* (2) A PRESS ON, with SL_SKY_BATCH=1 still set. The row governs the
     *     carrier from here, and the row selects PROD and nothing else - so
     *     the SAME asset pointer now uploads 48-byte records, which also
     *     re-exercises the on-demand float staging a topology built in byte
     *     mode does not carry. */
    sl_perf_opt_set(1);
    CHECK(sl_skybatch_on() == 1, "a press ON did not select the path");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "a press ON left carrier %d in force with SL_SKY_BATCH=1 inherited; "
          "the row selects prod only", sl_skybatch_float_mode());
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1,
          "the pass after a press ON refused on a topology cached in byte mode");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED,
          "the pass after a press ON served nothing");
    check_submission("press ON over an inherited SL_SKY_BATCH=1",
                     SL_SKYBATCH_FLOAT_PROD, &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();

    /* (3) THE HEADLINE DEFECT: A PRESS OFF, with SL_SKY_BATCH=1 still set.
     *     An inherited switch may no longer keep the path alive behind a menu
     *     that displays OFF. Measured as zero GL calls and zero loader calls,
     *     not as a return value. */
    sl_perf_opt_set(0);
    CHECK(sl_perf_opt() == 0, "the press OFF did not reach the row");
    CHECK(sl_skybatch_on() == 0,
          "SL_SKY_BATCH=1 kept the path armed after a press OFF - the menus "
          "would display OFF while the indexed path ran");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 0, "a pass opened after a press OFF");
    CHECK(tg.calls == 0, "%ld GL call(s) after a press OFF, expected none", tg.calls);
    CHECK(tg.proc_calls == 0, "%ld entry point(s) resolved after a press OFF",
          tg.proc_calls);
    CHECK(tg.draw_calls == 0, "%ld draw(s) after a press OFF", tg.draw_calls);

    /* (4) AND BACK ON: the transition is not one-way. */
    sl_perf_opt_set(1);
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass after a second press ON refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED,
          "the pass after a second press ON served nothing");
    check_submission("press ON again", SL_SKYBATCH_FLOAT_PROD, &A, 0u, MESH0_UNIQUE, 0L);
    sl_skybatch_pass_end();

    /* (5) AN INHERITED DIAGNOSTIC CARRIER, and the press that ends it. div is
     *     reachable only through the developer switch and no menu offers it,
     *     so an untouched row still serves it and a press moves the uploaded
     *     colour to the prod table. The uploaded bytes are what is compared,
     *     through sl_skybatch_float_colour for each table in turn. */
    env_put("SL_SKY_BATCH_FLOAT_COLOR=div");
    sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
    CHECK(sl_skybatch_on() == 1, "the path is not armed in the div arm");
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_DIV,
          "an inherited div switch resolved carrier %d with the row untouched",
          sl_skybatch_float_mode());
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the div pass refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "the div pass served nothing");
    check_submission("inherited SL_SKY_BATCH_FLOAT_COLOR=div, row untouched",
                     SL_SKYBATCH_FLOAT_DIV, &A, 0u, MESH0_UNIQUE, 0L);
    CHECK(memcmp(tg.last_upload + 16, div_col, sizeof div_col) == 0,
          "the div arm's uploaded colour is not the div table's");
    sl_skybatch_pass_end();

    sl_perf_opt_set(1);
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "a press left the inherited div carrier (%d) in force; no menu offers it",
          sl_skybatch_float_mode());
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "the pass after the div arm refused");
    sl_skbgl_log_reset();
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "that pass served nothing");
    check_submission("press ON over an inherited div switch",
                     SL_SKYBATCH_FLOAT_PROD, &A, 0u, MESH0_UNIQUE, 0L);
    CHECK(memcmp(tg.last_upload + 16, prod_col, sizeof prod_col) == 0,
          "the uploaded colour did not move to the prod table after the press");
    sl_skybatch_pass_end();

    /* The accounting, from the inside: five passes served one mesh each, the
     * press-OFF pass is not a refusal because pass_begin returns before any
     * counter when the path is off, and nothing was refused or declined. */
    counters_read(&c);
    CHECK(c.passes == 5ul, "%lu pass(es) opened, expected 5", c.passes);
    CHECK(c.idx_draws == 5ul, "%lu indexed draw(s), expected 5", c.idx_draws);
    CHECK(c.idx_vertices == (unsigned long) (5L * MESH0_INDICES),
          "%lu indices submitted, expected %ld", c.idx_vertices, 5L * MESH0_INDICES);
    CHECK(c.uniq == (unsigned long) (5L * MESH0_UNIQUE),
          "%lu unique vertices prepared, expected %ld", c.uniq, 5L * MESH0_UNIQUE);
    CHECK(c.pass_refused == 0ul, "%lu pass refusal(s)", c.pass_refused);
    CHECK(c.mesh_refused == 0ul, "%lu mesh refusal(s)", c.mesh_refused);
    CHECK(c.declined == 0ul, "%lu decline(s)", c.declined);
    check_watchdog("precedence");

    /* The environment and the latch are left clear, so nothing later in this
     * process - and no reader of this file taking the mode as a template -
     * inherits either. */
    env_put("SL_SKY_BATCH=");
    env_put("SL_SKY_BATCH_FLOAT_COLOR=");
    sl_perf_test_reset();
    CHECK(sl_skybatch_float_mode() == SL_SKYBATCH_FLOAT_PROD,
          "the cleared environment does not resolve to the row's prod carrier");
}

/* ======================================================================== */
/* THE ONCE-PER-PROCESS STATE. The float carrier marker and the atexit       */
/* registration are never cleared by any lifecycle path, so this mode drives */
/* three prod passes across a context change and requires ONE marker. The    */
/* terminal `final` line this process also prints is counted from the        */
/* captured stderr by tools\windows\skybatchtest.ps1 - it is written by an   */
/* atexit handler, so nothing inside main can observe it.                    */
/* ======================================================================== */
static void m_marker(void)
{
    struct sl_skybatch_shade cloud;
    int allocated;
    unsigned long cap, markers;

    settings_up("marker");
    context_up();
    build_asset();
    shade_for(0u, &cloud);

    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "pass 1 refused");
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "pass 1 served nothing");
    sl_skybatch_pass_end();
    sl_skybatch_test_float_state(&allocated, &cap, &markers);
    CHECK(markers == 1ul, "pass 1 printed the marker %lu time(s), expected 1", markers);

    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "pass 2 refused");
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "pass 2 served nothing");
    sl_skybatch_pass_end();
    sl_skybatch_test_float_state(&allocated, &cap, &markers);
    CHECK(markers == 1ul, "after pass 2 the marker count is %lu, expected 1", markers);

    sl_skybatch_context_invalidate();
    sl_skbgl_new_context();
    CHECK(sl_skybatch_pass_begin(&A, CLIP) == 1, "pass 3 refused after the context changed");
    CHECK(sl_skybatch_mesh(0u, &cloud) == SL_SKYBATCH_SERVED, "pass 3 served nothing");
    sl_skybatch_pass_end();
    sl_skybatch_test_float_state(&allocated, &cap, &markers);
    CHECK(markers == 1ul,
          "after a context change and a third pass the marker count is %lu, expected 1",
          markers);
    check_watchdog("marker");
    sl_skybatch_shutdown();
}

/* ======================================================================== */

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";

    printf("skydome batch module: offline mock tests, mode %s\n",
           *mode != '\0' ? mode : "(none)");
    printf("  SYNTHETIC GL; no window, no GPU, no game, no frame measured.\n");
    printf("  The OPTIMIZED RENDERING row, its policy and the settings store are REAL.\n\n");

    if      (strcmp(mode, "row")       == 0) m_row();
    else if (strcmp(mode, "flip")      == 0) m_flip();
    else if (strcmp(mode, "carrier")   == 0) m_carrier();
    else if (strcmp(mode, "latch")     == 0) m_latch();
    else if (strcmp(mode, "allocfail") == 0) m_allocfail();
    else if (strcmp(mode, "context")   == 0) m_context();
    else if (strcmp(mode, "bracket")   == 0) m_bracket();
    else if (strcmp(mode, "nosync")    == 0) m_nosync();
    else if (strcmp(mode, "attr")       == 0) m_attr();
    else if (strcmp(mode, "precedence") == 0) m_precedence();
    else if (strcmp(mode, "marker")     == 0) m_marker();
    else {
        fprintf(stderr, "usage: %s row|flip|carrier|latch|allocfail|context|"
                "bracket|nosync|attr|precedence|marker\n", argv[0]);
        return 2;
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
