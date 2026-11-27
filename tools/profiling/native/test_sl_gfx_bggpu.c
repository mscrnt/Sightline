/* Offline tests for the background pass's GPU vertex transform
 * (src/gfx/sl_gfx_bggpu.c).
 *
 * WHAT THIS IS. A pure fixture executable. The module is compiled against the
 * SYNTHETIC GL in sl_gfx_bggpu_testgl.c - no window, no native graphics API,
 * no renderer, no game - and linked against the REAL policy and the REAL
 * settings store (src/platform/sl_perf.c, src/platform/sl_settings.c) and the
 * REAL far-range arithmetic (src/gfx/sl_gfx_worldasset.c). Neither the policy
 * nor the store nor sl_wa_far_range is mocked: the OPTIMIZED RENDERING row is
 * driven through sl_perf_opt_set against a scratch config under %TEMP%, and
 * every depth coefficient below comes out of sl_wa_far_range at the overlap
 * constants src/gfx/sl_gfx_dl.c passes it. No coefficient is hand-written.
 *
 * HOW THE MODULE'S GL INCLUDES ARE REPLACED. sl_gfx_bggpu.c has no test hook:
 * it includes <windows.h> and then <GL/gl.h> unconditionally, with no macro
 * around either, and src/ may not be edited to add one. The fixture therefore
 * injects sl_gfx_bggpu_testgl.h ahead of that translation unit with -include,
 * and that header defines the host headers' own include guards so the real
 * headers expand to nothing. See the long note at the top of the header for
 * the guard names, where they were read, and why this was preferred over a
 * directory of stub windows.h / GL/gl.h files (it needs no extra files, and it
 * fails loudly rather than silently if a toolchain's guards ever differ).
 *
 * WHAT THIS FIXTURE DOES NOT QUALIFY. The vertex program's own depth
 * arithmetic is NOT EXECUTED here: the GLSL in sl_gfx_bggpu.c that computes
 * the one-range far clamp and the SL_BGGPU_DEPTH_FAR0 / _FAR1 depths is only
 * handed to glShaderSource as text and recorded, because there is no GLSL
 * compiler and no GPU in this process - so what remains unqualified is the
 * far-depth expressions' clamp and ranges as actually evaluated on a driver,
 * and the depth-buffer ordering that results. The uniform checks below prove
 * which values the program is GIVEN; they prove nothing about what it computes
 * from them. The one place arithmetic is compared (st_transcription) is a
 * SOURCE-LEVEL agreement check between a hand transcription of that GLSL and
 * the CPU reference in sl_gfx_dl.c, and is labelled as such where it runs.
 *
 * Usage: test_sl_gfx_bggpu.exe
 */
#include "sl_gfx_bggpu_testgl.h"

#include "sl_gfx_bggpu.h"
#include "sl_gfx_worldasset.h"
#include "sl_perf.h"
#include "sl_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

static void begin_case(const char *name)
{
    cases++;
    case_fails = 0;
    current_case = name;
    printf("  %s\n", name);
}

static void end_case(void)
{
    /* Every case ends by proving the watchdogs stayed silent: no name from a
     * previous fake context was used, nothing reached GL after the context was
     * marked destroyed, and no shader source was set on a name that was never
     * created. */
    CHECK(bg.v_stale_name == 0, "%s: a name from an earlier context was used (%ld)",
          current_case, bg.v_stale_name);
    CHECK(bg.v_call_after_context_destroyed == 0,
          "%s: %ld GL calls after the context was destroyed",
          current_case, bg.v_call_after_context_destroyed);
    CHECK(bg.v_source_without_shader == 0,
          "%s: shader source set on a name that was never created (%ld)",
          current_case, bg.v_source_without_shader);
    printf("    %s\n", case_fails == 0 ? "ok" : "FAILED");
}

/* The provider's player-data ladder, which has no module in an offline build.
 * sl_gfx_worldasset.c declares it; it is linked in for sl_wa_far_range and
 * sl_wa_far_plane only, and nothing this fixture calls reaches a file. */
const char *sl_asset_override_dir(void) { return NULL; }

/* ---- reading the stub's record ------------------------------------------ */

/* The uniform the module is expected to have written at log index i. Each
 * helper names the uniform, so a failure says which one moved. */
static int u_is_1i(int i, const char *name, GLint v)
{
    return i >= 0 && i < bg.nu && bg.u[i].kind == BGT_U1I
           && bg.u[i].loc == bgt_loc(name) && bg.u[i].ival == v;
}

static int u_is_1f(int i, const char *name, float v)
{
    return i >= 0 && i < bg.nu && bg.u[i].kind == BGT_U1F
           && bg.u[i].loc == bgt_loc(name) && bg.u[i].fv[0] == v;
}

static int u_is_3f(int i, const char *name, float a, float b, float c)
{
    return i >= 0 && i < bg.nu && bg.u[i].kind == BGT_U3F
           && bg.u[i].loc == bgt_loc(name)
           && bg.u[i].fv[0] == a && bg.u[i].fv[1] == b && bg.u[i].fv[2] == c;
}

static int u_is_1fv(int i, const char *name, const float *v, int n)
{
    int k;
    if (!(i >= 0 && i < bg.nu && bg.u[i].kind == BGT_U1FV
          && bg.u[i].loc == bgt_loc(name) && bg.u[i].count == n))
        return 0;
    for (k = 0; k < n; k++) if (bg.u[i].fv[k] != v[k]) return 0;
    return 1;
}

/* One depth upload, as sl_bggpu_use_depth makes it: the mode, then the two
 * coefficients, in that order. */
static int u_is_depth(int i, int mode, float k1, float k2)
{
    return u_is_1i(i, "u_dmode", (GLint) mode)
           && u_is_1f(i + 1, "u_fk1", k1)
           && u_is_1f(i + 2, "u_fk2", k2);
}

/* ---- the module, armed -------------------------------------------------- */

/* A clean fake driver, an untried module and the row ON. The row is set
 * through the REAL policy, which writes the REAL store. */
static void arm_on(void)
{
    sl_bggpu_invalidate();
    bgt_reset();
    sl_perf_opt_set(1);
}

static struct sl_bggpu_walk a_walk(void)
{
    struct sl_bggpu_walk w;
    int i;
    memset(&w, 0, sizeof w);
    /* Distinguishable values, so a uniform that received the wrong one is not
     * mistaken for the right one. SYNTHETIC. */
    for (i = 0; i < 16; i++) w.C[i] = (float) (i + 1) * 0.125f;
    w.org[0] = 11.0f; w.org[1] = -22.0f; w.org[2] = 33.0f;
    w.sc = 0.75f;
    w.post = 0.5f;
    w.fogless = 7;                 /* not 1: the module normalises it */
    return w;
}

/* ---- the fixture's synthetic camera ------------------------------------- */

/* A world-to-clip matrix built the way the background pass's is, and the way
 * sl_gfx_worldasset.c's own far-plane check builds one: an affine view
 * (rotation about y, a scale, a translation) times a perspective with near 10
 * and far 15000. SYNTHETIC, but the SHAPE is what matters - sl_wa_far_plane
 * has to recover a far plane from it, and some of the swept item has to lie
 * past that plane, which is the two-range case. */
static void build_clip(float *C)
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

#define SWEEP_N 512
static const float SW_SC = 2.0f, SW_POST = 1.0f;
static const float SW_ORG[3] = { 10.0f, -5.0f, 20.0f };

/* One swept model position: a ray that starts inside the far plane and runs
 * well past it. */
static void sweep_point(int i, float *px, float *py, float *pz)
{
    const float t = (float) i / (float) (SWEEP_N - 1);
    *px = 400.0f * t - 200.0f;
    *py = 150.0f * t;
    *pz = 5000.0f - 125000.0f * t;
}

/* THE CPU REFERENCE, transcribed from bg_vertex_range (src/gfx/sl_gfx_dl.c):
 * the room space `(p * sc - org) * post`, then the clip product in the pass's
 * own row order and the pass's own expression order. */
static void cpu_clip(const float *C, float px, float py, float pz, float *c)
{
    const float x = (px * SW_SC - SW_ORG[0]) * SW_POST;
    const float y = (py * SW_SC - SW_ORG[1]) * SW_POST;
    const float z = (pz * SW_SC - SW_ORG[2]) * SW_POST;
    int j;
    for (j = 0; j < 4; j++)
        c[j] = x * C[j] + y * C[4 + j] + z * C[8 + j] + C[12 + j];
}

/* A SOURCE-LEVEL TRANSCRIPTION of the depth selection in the GLSL of
 * src/gfx/sl_gfx_bggpu.c (BGG_VS, the two lines after the clip product), read
 * off that text by hand into C. THIS IS NOT WHAT A DRIVER COMPUTES: no GLSL
 * compiler runs in this process, and `precise` has no meaning here. It exists
 * only so the transcription can be compared with the CPU reference, which is a
 * check on the two TEXTS agreeing, not on any execution. */
static float vs_depth(int dmode, int fogless, float fk1, float fk2, float z, float w)
{
    if (dmode == 1) return fk1 * w - fk2;
    if (dmode == 0 && fogless == 0 && w > 0.0f && z > w) return w;
    return z;
}

/* ---- 1. the row drives the transform, and OFF does not poison it -------- */

static void st_row_drives(void)
{
    struct sl_bggpu_walk w = a_walk();
    long calls, compile, program, locs;
    int i;

    begin_case("row: OFF resolves and compiles nothing, and does not poison capability");

    sl_bggpu_invalidate();
    bgt_reset();
    sl_perf_opt_set(0);

    CHECK(sl_bggpu_opt_in() == 0, "the transform is not selected with the row OFF");
    CHECK(sl_bggpu_ready() == 0, "a walk considered while OFF is not ready");
    CHECK(sl_bggpu_state() == 0, "the state is still UNTRIED, not -1 (state=%d)",
          sl_bggpu_state());
    CHECK(bg.proc_calls == 0, "no entry point was resolved while OFF (%ld)", bg.proc_calls);
    CHECK(bg.calls == 0, "no GL call was made while OFF (%ld)", bg.calls);
    CHECK(bg.compile == 0 && bg.create_shader == 0 && bg.shader_source == 0,
          "no shader was created, sourced or compiled while OFF");
    CHECK(strstr(sl_bggpu_why(), "OPTIMIZED RENDERING is off") != NULL,
          "the reason names the off selection: '%s'", sl_bggpu_why());

    /* A whole launch's worth of walks considered while the row is off. */
    for (i = 0; i < 64; i++) (void) sl_bggpu_ready();
    CHECK(bg.proc_calls == 0 && bg.calls == 0 && sl_bggpu_state() == 0,
          "64 walks considered while OFF cost no resolve, no call and no latch");
    CHECK(sl_bggpu_use_walk(&w) == 0, "no walk is served while OFF");
    CHECK(bg.nu == 0, "and nothing was uploaded (%d writes)", bg.nu);

    /* ON: the first walk resolves and compiles exactly as a process that
     * started ON would. */
    sl_perf_opt_set(1);
    CHECK(sl_bggpu_opt_in() == 1, "the row ON selects the transform");
    CHECK(sl_bggpu_ready() == 1, "the first walk after the row goes ON is ready");
    CHECK(sl_bggpu_state() == 1, "the state is READY (state=%d)", sl_bggpu_state());
    CHECK(bg.create_shader == 1 && bg.shader_source == 1 && bg.compile == 1,
          "one shader was created, sourced and compiled");
    CHECK(bg.create_program == 1 && bg.attach == 1 && bg.link == 1,
          "one program was created, attached to and linked");
    CHECK(bg.get_uniform_location == 8,
          "every one of the eight transform uniforms was resolved (%ld)",
          bg.get_uniform_location);
    /* The module resolves exactly the sixteen GL 2.0 program entry points and
     * asks for nothing else - no sync point, no delete, no read-back. Anything
     * outside the stub's table would have resolved NULL and refused. */
    CHECK(bg.proc_calls == 16, "exactly sixteen entry points were resolved (%ld)",
          bg.proc_calls);
    CHECK(bg.last_source != NULL && bg.last_source_count == 1,
          "the program text was handed over once, in one string");
    CHECK(bg.last_source != NULL
          && strstr(bg.last_source, "#version 400 compatibility") != NULL,
          "the text handed to glShaderSource is the module's own program");
    CHECK(bg.current == 0, "the readiness check leaves no program current");

    /* OFF again: the compiled program is not torn down and nothing recompiles.
     * ready() answers from the cached capability, which is what the header
     * says - the live question the caller latches is sl_bggpu_opt_in. */
    calls = bg.calls; compile = bg.compile; program = bg.create_program;
    locs = bg.get_uniform_location;
    sl_perf_opt_set(0);
    CHECK(sl_bggpu_opt_in() == 0, "OFF again: the transform is not selected");
    CHECK(sl_bggpu_ready() == 1, "the cached capability survives the row going OFF");
    CHECK(sl_bggpu_state() == 1, "and the state stays READY");
    CHECK(bg.calls == calls, "the row going OFF made no GL call (%ld -> %ld)",
          calls, bg.calls);

    /* ON again: still usable, nothing recompiled. */
    sl_perf_opt_set(1);
    CHECK(sl_bggpu_ready() == 1, "ON again: still ready");
    CHECK(bg.compile == compile && bg.create_program == program
          && bg.get_uniform_location == locs,
          "nothing was recompiled or re-resolved by OFF -> ON "
          "(compile %ld, program %ld, locations %ld)",
          bg.compile, bg.create_program, bg.get_uniform_location);
    CHECK(sl_bggpu_use_walk(&w) == 1, "and the program is still usable");

    end_case();
}

/* ---- 2. a real failure is latched and never retried --------------------- */

/* Each failure gets its own armed module. After the check the fault is lifted
 * and the module is invalidated, which is the only thing that clears it. */
static void st_failures(void)
{
    struct sl_bggpu_walk w = a_walk();
    long p, c, n;
    int i;

    begin_case("failure: a missing entry point is latched, never retried, and only invalidate clears it");
    arm_on();
    bg.withhold_proc = "glUniform1fv";
    CHECK(sl_bggpu_ready() == 0, "a withheld entry point refuses");
    CHECK(sl_bggpu_state() == -1, "and latches UNAVAILABLE (state=%d)", sl_bggpu_state());
    CHECK(strstr(sl_bggpu_why(), "entry points are not all present") != NULL,
          "the reason names the cause: '%s'", sl_bggpu_why());
    CHECK(bg.create_shader == 0 && bg.compile == 0,
          "nothing was compiled once the resolve failed");
    p = bg.proc_calls; c = bg.calls;
    CHECK(sl_bggpu_ready() == 0, "a second walk still refuses");
    CHECK(bg.proc_calls == p && bg.calls == c,
          "and did not retry (resolves %ld -> %ld, calls %ld -> %ld)",
          p, bg.proc_calls, c, bg.calls);
    CHECK(sl_bggpu_use_walk(&w) == 0 && bg.nu == 0,
          "no walk is served and nothing is uploaded after a real failure");
    /* The row cannot clear a real failure. */
    sl_perf_opt_set(0);
    CHECK(sl_bggpu_ready() == 0 && sl_bggpu_state() == -1,
          "the row going OFF does not clear a real failure");
    sl_perf_opt_set(1);
    CHECK(sl_bggpu_ready() == 0 && sl_bggpu_state() == -1,
          "and the row coming back ON does not clear it either");
    CHECK(bg.proc_calls == p, "nor retry (resolves %ld)", bg.proc_calls);
    /* Only invalidate does. */
    bg.withhold_proc = NULL;
    sl_bggpu_invalidate();
    CHECK(sl_bggpu_state() == 0, "invalidate returns the state to UNTRIED");
    CHECK(sl_bggpu_why()[0] == '\0', "and clears the reason ('%s')", sl_bggpu_why());
    CHECK(sl_bggpu_ready() == 1 && sl_bggpu_state() == 1,
          "the next walk resolves and compiles against the context again");
    end_case();

    begin_case("failure: a vertex program that will not compile");
    arm_on();
    bg.fail_compile = 1;
    CHECK(sl_bggpu_ready() == 0, "a failed compile refuses");
    CHECK(sl_bggpu_state() == -1, "and latches UNAVAILABLE");
    CHECK(strstr(sl_bggpu_why(), "did not compile") != NULL,
          "the reason names the compile: '%s'", sl_bggpu_why());
    CHECK(bg.get_shaderlog == 1,
          "the driver's own words were asked for, once (%ld)", bg.get_shaderlog);
    CHECK(bg.create_program == 0, "no program was created after the compile failed");
    p = bg.compile; c = bg.calls;
    CHECK(sl_bggpu_ready() == 0, "a second walk still refuses");
    CHECK(bg.compile == p && bg.calls == c,
          "and recompiled nothing (compiles %ld -> %ld, calls %ld -> %ld)",
          p, bg.compile, c, bg.calls);
    sl_perf_opt_set(0); sl_perf_opt_set(1);
    CHECK(sl_bggpu_state() == -1 && bg.compile == p,
          "OFF -> ON neither clears it nor recompiles");
    end_case();

    begin_case("failure: a program that will not link");
    arm_on();
    bg.fail_link = 1;
    CHECK(sl_bggpu_ready() == 0, "a failed link refuses");
    CHECK(sl_bggpu_state() == -1, "and latches UNAVAILABLE");
    CHECK(strstr(sl_bggpu_why(), "did not link") != NULL,
          "the reason names the link: '%s'", sl_bggpu_why());
    CHECK(bg.get_programlog == 1,
          "the driver's own words were asked for, once (%ld)", bg.get_programlog);
    CHECK(bg.get_uniform_location == 0,
          "no uniform was located after the link failed (%ld)", bg.get_uniform_location);
    p = bg.link; c = bg.calls;
    CHECK(sl_bggpu_ready() == 0 && bg.link == p && bg.calls == c,
          "a second walk refuses without relinking (links %ld -> %ld)", p, bg.link);
    CHECK(sl_bggpu_use_walk(&w) == 0, "and serves no walk");
    /* The module zeroes the program name on a failed link, so nothing can be
     * bound; the stub would record a stale or unminted name if it were. */
    sl_bggpu_bind(1);
    CHECK(bg.use_program == 0, "a failed link binds nothing (%ld)", bg.use_program);
    end_case();

    begin_case("failure: each one of the eight transform uniforms is required");
    for (i = 0; i < 8; i++) {
        arm_on();
        bg.withhold_uniform = bgt_uniform_names[i];
        CHECK(sl_bggpu_ready() == 0, "withholding %s refuses", bgt_uniform_names[i]);
        CHECK(sl_bggpu_state() == -1, "withholding %s latches UNAVAILABLE",
              bgt_uniform_names[i]);
        CHECK(strstr(sl_bggpu_why(), "uniform did not resolve") != NULL,
              "the reason names the uniform resolve: '%s'", sl_bggpu_why());
        n = bg.get_uniform_location; c = bg.calls;
        CHECK(sl_bggpu_ready() == 0 && bg.get_uniform_location == n && bg.calls == c,
              "withholding %s is not retried (locations %ld -> %ld)",
              bgt_uniform_names[i], n, bg.get_uniform_location);
        sl_bggpu_bind(1);
        CHECK(bg.use_program == 0,
              "withholding %s binds nothing (%ld)", bgt_uniform_names[i], bg.use_program);
        bg.withhold_uniform = NULL;
    }
    end_case();
}

/* ---- 3. context recreation ---------------------------------------------- */

static void st_context(void)
{
    struct sl_bggpu_walk w = a_walk();
    long calls;

    begin_case("context: invalidate makes no GL call, drops the program, and the next walk re-resolves");
    arm_on();
    CHECK(sl_bggpu_ready() == 1, "ready in the first context");
    CHECK(sl_bggpu_use_walk(&w) == 1, "a walk is served");
    sl_bggpu_bind(1);
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    sl_bggpu_bind(0);

    calls = bg.calls;
    sl_bggpu_invalidate();
    CHECK(bg.calls == calls, "invalidate made no GL call at all (%ld -> %ld)",
          calls, bg.calls);
    CHECK(sl_bggpu_state() == 0, "and returned the state to UNTRIED");

    /* While untried nothing is usable and nothing reaches GL, which is what
     * proves the program name was dropped rather than kept and rebound. */
    CHECK(sl_bggpu_use_walk(&w) == 0, "no walk is served while untried");
    sl_bggpu_bind(1);
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR0, 1.0f, 2.0f);
    sl_bggpu_bind(0);
    CHECK(bg.calls == calls, "and no GL call was made while untried (%ld)", bg.calls);

    /* The context is replaced. Every name minted before now is stale, and the
     * stub records a violation if one is used. */
    bgt_new_context();
    CHECK(sl_bggpu_ready() == 1, "the next eligible walk is ready in the new context");
    CHECK(bg.create_shader == 1 && bg.compile == 1 && bg.link == 1
          && bg.get_uniform_location == 8,
          "it re-resolved and recompiled against the new context");
    CHECK(sl_bggpu_use_walk(&w) == 1, "and serves a walk there");
    sl_bggpu_bind(1);
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR1, 3.0f, 4.0f);
    sl_bggpu_bind(0);
    CHECK(bg.v_stale_name == 0,
          "no name from before the invalidate was used afterwards (%ld)", bg.v_stale_name);
    end_case();

    /* THE WATCHDOG IS LIVE, not vacuously silent. A context replaced WITHOUT
     * telling the module makes the very same sequence use a stale name, and
     * the counter fires. This case asserts it is NON-zero, which is what makes
     * the zero above mean something. */
    begin_case("context: the stale-name watchdog fires when the module is not told");
    arm_on();
    CHECK(sl_bggpu_ready() == 1, "ready in the first context");
    bgt_new_context();              /* no sl_bggpu_invalidate: the module still
                                     * holds the old context's program name */
    sl_bggpu_bind(1);
    CHECK(bg.v_stale_name == 1,
          "binding a name from the replaced context is detected (%ld)", bg.v_stale_name);
    bg.v_stale_name = 0;            /* the positive control is the point here */
    end_case();

    begin_case("teardown: a destroyed context is never called into");
    arm_on();
    CHECK(sl_bggpu_ready() == 1, "ready before teardown");
    sl_bggpu_invalidate();          /* what the backend calls before deleting */
    bg.context_destroyed = 1;
    /* Nothing the module exposes deletes a GL object: the header records that
     * the program and shader are owned for the process and released with the
     * context, so invalidate is the whole of teardown. The stub provides no
     * glDeleteShader or glDeleteProgram, so a delete would not even link. */
    sl_bggpu_invalidate();
    CHECK(sl_bggpu_use_walk(&w) == 0, "no walk is served after teardown");
    sl_bggpu_bind(1);
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR0, 1.0f, 2.0f);
    sl_bggpu_bind(0);
    CHECK(bg.v_call_after_context_destroyed == 0,
          "nothing reached GL after the context was destroyed (%ld)",
          bg.v_call_after_context_destroyed);
    bg.context_destroyed = 0;
    end_case();
}

/* ---- 4. the walk's upload ----------------------------------------------- */

static void st_use_walk(void)
{
    struct sl_bggpu_walk w = a_walk();
    int n;
    long binds;

    begin_case("walk: the walk's uniforms are uploaded, nothing is left current, depth starts SINGLE at zero");
    arm_on();
    CHECK(sl_bggpu_ready() == 1, "ready");
    n = bg.nu;
    binds = bg.use_program;
    CHECK(sl_bggpu_use_walk(&w) == 1, "the walk is served");

    /* Eight writes, in the module's own order, each to its own location. */
    CHECK(bg.nu - n == 8, "eight uniforms were written (%d)", bg.nu - n);
    CHECK(u_is_1f(n + 0, "u_sc", w.sc), "u_sc took the walk's scale");
    CHECK(u_is_1f(n + 1, "u_post", w.post), "u_post took the walk's visibility factor");
    CHECK(u_is_3f(n + 2, "u_org", w.org[0], w.org[1], w.org[2]),
          "u_org took the walk's render origin");
    CHECK(u_is_1fv(n + 3, "u_C", w.C, 16), "u_C took all sixteen clip-matrix elements");
    CHECK(u_is_1i(n + 4, "u_fogless", 1),
          "u_fogless took the stage rule, normalised to 1 from %d", w.fogless);
    /* The header's contract: the depth mode is left at SINGLE with zero
     * coefficients, so the program's depth state is defined before any draw
     * selects it. */
    CHECK(u_is_depth(n + 5, SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f),
          "the depth state was left at SINGLE with zero coefficients");
    /* All eight landed in one program. */
    CHECK(bg.u[n].prog == bg.last_program && bg.u[n + 7].prog == bg.last_program,
          "every write landed in the linked program");
    /* And the binding is handed back. */
    CHECK(bg.current == 0, "no program is left current");
    CHECK(bg.use_program - binds == 2,
          "the upload bound the program and unbound it, once each (%ld)",
          bg.use_program - binds);

    /* A fogless walk uploads 0, so the normalisation is a mapping and not a
     * constant. */
    w.fogless = 0;
    n = bg.nu;
    CHECK(sl_bggpu_use_walk(&w) == 1, "a fogless walk is served");
    CHECK(u_is_1i(n + 4, "u_fogless", 0), "u_fogless took 0 for a fogless walk");

    /* Not ready: answers 0 and writes nothing. Both not-ready states. */
    sl_bggpu_invalidate();
    n = bg.nu;
    CHECK(sl_bggpu_use_walk(&w) == 0, "an untried module serves no walk");
    CHECK(bg.nu == n, "and writes nothing (%d)", bg.nu - n);
    bg.fail_link = 1;
    CHECK(sl_bggpu_ready() == 0 && sl_bggpu_state() == -1, "a real failure is latched");
    CHECK(sl_bggpu_use_walk(&w) == 0, "an unavailable module serves no walk");
    CHECK(bg.nu == n, "and writes nothing (%d)", bg.nu - n);
    bg.fail_link = 0;
    /* A null walk is refused with the module ready. */
    sl_bggpu_invalidate();
    CHECK(sl_bggpu_ready() == 1, "ready again");
    n = bg.nu;
    CHECK(sl_bggpu_use_walk(NULL) == 0, "a null walk is refused");
    CHECK(bg.nu == n, "and writes nothing");
    end_case();
}

/* ---- 5. the depth uniform follows the mesh and the range ---------------- */

static void st_depth(void)
{
    struct sl_bggpu_walk w = a_walk();
    float C[16];
    float zf = -1.0f, wmax = 0.0f;
    float k1 = 0.0f, k2 = 0.0f;      /* the opaque overlap's pair */
    float t1 = 0.0f, t2 = 0.0f;      /* the blend overlap's pair  */
    float from, to;
    float c[4];
    int i, n, past = 0;
    long binds, rejected;

    begin_case("depth: the pair comes from the real sl_wa_far_range at the real overlap constants");
    build_clip(C);
    CHECK(sl_wa_far_plane(C, &zf) == 1 && fabs((double) zf - 15000.0) < 1.0,
          "the far plane is recovered from the clip matrix (%.3f)", (double) zf);
    for (i = 0; i < SWEEP_N; i++) {
        float px, py, pz;
        sweep_point(i, &px, &py, &pz);
        cpu_clip(C, px, py, pz, c);
        if (c[3] > wmax) wmax = c[3];
        if (c[3] > 0.0f && c[2] > c[3]) past++;
    }
    CHECK(wmax > zf, "part of the swept item lies past the far plane "
                     "(wmax %.3f, far %.3f) - this is the two-range case",
          (double) wmax, (double) zf);
    CHECK(past > 0, "and %d swept vertices would be clamped by the one-range frame", past);
    /* THE SAME TWO CALLS src/gfx/sl_gfx_dl.c:18462 and :18466 make, with the
     * same overlap constants. No coefficient is written by hand here. */
    sl_wa_far_range(zf * SL_WA_FAR_OVERLAP, wmax * 1.001f, &k1, &k2);
    sl_wa_far_range(zf * SL_WA_FAR_OVERLAP_BLEND, wmax * 1.001f, &t1, &t2);
    CHECK(k1 != t1 && k2 != t2,
          "the opaque and blend pairs differ (%.6f/%.3f vs %.6f/%.3f) - a check that "
          "confused them would be blind", (double) k1, (double) k2, (double) t1, (double) t2);
    from = zf * SL_WA_FAR_OVERLAP;
    to = wmax * 1.001f;
    CHECK(fabs((double) ((k1 * from - k2) / from) + 1.0) < 1e-4
          && fabs((double) ((k1 * to - k2) / to) - 1.0) < 1e-4,
          "the first range maps its own near w to -1 and its far w to +1");
    end_case();

    begin_case("depth: rejected with no program current, written once bound");
    arm_on();
    CHECK(sl_bggpu_ready() == 1, "ready");
    CHECK(sl_bggpu_use_walk(&w) == 1, "a walk is served, leaving nothing current");
    CHECK(bg.current == 0, "nothing is current");
    n = bg.nu;
    rejected = bg.uniform_rejected;
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR0, k1, k2);
    /* WHAT THE PROGRAM RECEIVES IS NOTHING. The header puts the obligation on
     * the caller ("must not be called with the fixed pipeline bound"); the
     * module itself does not re-check the binding, and the rule is GL's own -
     * glUniform writes the program in use, so with none in use the call raises
     * INVALID_OPERATION and writes nothing. The stub models exactly that, so
     * the log is what the program actually got and the rejection count is what
     * the module nevertheless called. */
    CHECK(bg.nu == n, "an unbound depth upload reaches the program not at all (%d)",
          bg.nu - n);
    CHECK(bg.uniform_rejected - rejected == 3,
          "GL rejected the three calls the module made unbound (%ld)",
          bg.uniform_rejected - rejected);

    sl_bggpu_bind(1);
    CHECK(bg.current == bg.last_program, "bind(1) makes the program current");
    n = bg.nu;
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR0, k1, k2);
    CHECK(bg.nu - n == 3, "a bound depth upload writes three uniforms (%d)", bg.nu - n);
    CHECK(u_is_depth(n, SL_BGGPU_DEPTH_FAR0, k1, k2),
          "FAR0 with the opaque pair");
    sl_bggpu_bind(0);
    end_case();

    begin_case("depth: a two-range far frame's FAR0 run - the uniform follows the MESH, with no re-bind");
    arm_on();
    CHECK(sl_bggpu_ready() == 1, "ready");
    CHECK(sl_bggpu_use_walk(&w) == 1, "the walk's uniforms are up");
    sl_bggpu_bind(1);
    n = bg.nu;
    binds = bg.use_program;
    /* The sequence a far frame's first range actually produces: an opaque
     * mesh, a blended mesh, then an opaque mesh again, all inside one binding.
     * The header states this is why the uniform follows the mesh and the range
     * rather than the binding; these three writes are that statement measured. */
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR0, k1, k2);   /* opaque  */
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR0, t1, t2);   /* blended */
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR0, k1, k2);   /* opaque  */
    CHECK(bg.nu - n == 9, "three separate depth uploads, nine writes (%d)", bg.nu - n);
    CHECK(u_is_depth(n + 0, SL_BGGPU_DEPTH_FAR0, k1, k2),
          "the first opaque mesh took the opaque pair");
    CHECK(u_is_depth(n + 3, SL_BGGPU_DEPTH_FAR0, t1, t2),
          "the blended mesh took the blend overlap's pair");
    CHECK(u_is_depth(n + 6, SL_BGGPU_DEPTH_FAR0, k1, k2),
          "and the opaque pair came back for the third mesh");
    CHECK(bg.use_program == binds,
          "no re-bind happened between the three (%ld -> %ld)", binds, bg.use_program);
    CHECK(bg.u[n].prog == bg.u[n + 6].prog,
          "all three landed in the same bound program");

    /* THE SAME WALK'S SECOND RANGE. The header: FAR1 and SINGLE read neither
     * coefficient. What the MODULE does is upload the pair whatever the mode,
     * so the program never carries a stale one (sl_gfx_bggpu.c, the comment
     * above sl_bggpu_use_depth) - so the measured fact is: the mode uniform is
     * 2 and the coefficient uniforms carry exactly what was passed. Which of
     * them the program then READS is a property of the GLSL, checked at source
     * level below and NOT executed anywhere in this fixture. */
    n = bg.nu;
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR1, k1, k2);
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR1, t1, t2);
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_FAR1, -1.0f, 12345.0f);
    CHECK(bg.nu - n == 9, "the second range uploads three times too (%d)", bg.nu - n);
    CHECK(u_is_1i(n + 0, "u_dmode", SL_BGGPU_DEPTH_FAR1)
          && u_is_1i(n + 3, "u_dmode", SL_BGGPU_DEPTH_FAR1)
          && u_is_1i(n + 6, "u_dmode", SL_BGGPU_DEPTH_FAR1),
          "the mode uniform is 2 for every FAR1 draw, whatever the pair");
    CHECK(u_is_depth(n + 0, SL_BGGPU_DEPTH_FAR1, k1, k2)
          && u_is_depth(n + 3, SL_BGGPU_DEPTH_FAR1, t1, t2)
          && u_is_depth(n + 6, SL_BGGPU_DEPTH_FAR1, -1.0f, 12345.0f),
          "and the pair uploaded is exactly the pair passed, so no stale pair is carried");

    /* SINGLE, with the one-range frame's clamp - which has no coefficient, so
     * the pair the pass uses there is the zero pair the walk's upload set. */
    n = bg.nu;
    sl_bggpu_use_depth(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(u_is_depth(n, SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f),
          "SINGLE uploads mode 0 with the zero pair");
    CHECK(bg.u[n].ival != (GLint) SL_BGGPU_DEPTH_FAR0
          && bg.u[n].ival != (GLint) SL_BGGPU_DEPTH_FAR1,
          "and is distinguishable from both far modes in the mode uniform (%d)",
          (int) bg.u[n].ival);
    sl_bggpu_bind(0);
    end_case();

    /* ---- the SOURCE-LEVEL transcription check --------------------------- */

    begin_case("depth: SOURCE-LEVEL agreement between a transcription of the GLSL and the CPU reference"
               " - NOT a driver result");
    {
        int mism_single = 0, mism_far0 = 0, mism_blend = 0, mism_far1 = 0;
        int mism_fogless = 0, mism_modefree = 0, distinct = 0;
        for (i = 0; i < SWEEP_N; i++) {
            float px, py, pz, cpu;
            sweep_point(i, &px, &py, &pz);
            cpu_clip(C, px, py, pz, c);
            /* bg_vertex_range's own clamp, transcribed:
             *   if (!fogless && c[3] > 0.0f && c[2] > c[3]) c[2] = c[3]; */
            cpu = c[2];
            if (c[3] > 0.0f && c[2] > c[3]) cpu = c[3];
            if (vs_depth(SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f, c[2], c[3]) != cpu)
                mism_single++;
            /* the fogless walk keeps the unclamped z on both paths */
            if (vs_depth(SL_BGGPU_DEPTH_SINGLE, 1, 0.0f, 0.0f, c[2], c[3]) != c[2])
                mism_fogless++;
            /* the pass's blanket rewrite: clip[z] = fz_k1 * clip[w] - fz_k2 */
            if (vs_depth(SL_BGGPU_DEPTH_FAR0, 0, k1, k2, c[2], c[3]) != k1 * c[3] - k2)
                mism_far0++;
            /* bg_far_rezero's pair for a blended mesh */
            if (vs_depth(SL_BGGPU_DEPTH_FAR0, 0, t1, t2, c[2], c[3]) != t1 * c[3] - t2)
                mism_blend++;
            /* the game's range: the per-vertex zraw, which is the unclamped z */
            if (vs_depth(SL_BGGPU_DEPTH_FAR1, 0, k1, k2, c[2], c[3]) != c[2])
                mism_far1++;
            /* FAR1 reads neither coefficient: the same z for any pair */
            if (vs_depth(SL_BGGPU_DEPTH_FAR1, 0, 1.0f, 2.0f, c[2], c[3])
                != vs_depth(SL_BGGPU_DEPTH_FAR1, 0, -7.5f, 9999.0f, c[2], c[3]))
                mism_modefree++;
            /* a clamped vertex tells the three modes apart */
            if (c[3] > 0.0f && c[2] > c[3]) {
                const float s = vs_depth(SL_BGGPU_DEPTH_SINGLE, 0, 0.0f, 0.0f, c[2], c[3]);
                const float f0 = vs_depth(SL_BGGPU_DEPTH_FAR0, 0, k1, k2, c[2], c[3]);
                const float f1 = vs_depth(SL_BGGPU_DEPTH_FAR1, 0, k1, k2, c[2], c[3]);
                if (s != f0 && s != f1 && f0 != f1) distinct++;
            }
        }
        CHECK(mism_single == 0,
              "the transcribed SINGLE clamp agrees with bg_vertex_range's over %d "
              "swept vertices (%d mismatches)", SWEEP_N, mism_single);
        CHECK(mism_fogless == 0,
              "a fogless walk's transcribed depth is the unclamped z (%d mismatches)",
              mism_fogless);
        CHECK(mism_far0 == 0,
              "the transcribed FAR0 depth agrees with the pass's blanket rewrite "
              "(%d mismatches)", mism_far0);
        CHECK(mism_blend == 0,
              "and with bg_far_rezero's blended pair (%d mismatches)", mism_blend);
        CHECK(mism_far1 == 0,
              "the transcribed FAR1 depth is the per-vertex zraw (%d mismatches)",
              mism_far1);
        CHECK(mism_modefree == 0,
              "FAR1's transcribed depth is independent of the coefficient pair "
              "(%d mismatches)", mism_modefree);
        CHECK(distinct == past && past > 0,
              "all three modes give different depths on each of the %d clamped vertices "
              "(%d)", past, distinct);
        printf("    NOT QUALIFIED by the above: the GLSL is never compiled or run here. "
               "This compares a hand transcription of BGG_VS's depth lines with the CPU "
               "reference; what a driver computes is outside this fixture.\n");
    }
    end_case();
}

/* ---- 6. the binding ----------------------------------------------------- */

static void st_bind(void)
{
    long calls, binds;

    begin_case("bind: the fixed pipeline comes back, and an unready module binds nothing");
    sl_bggpu_invalidate();
    bgt_reset();
    sl_perf_opt_set(1);
    /* UNTRIED: bind makes no GL call either way - there is no program yet. */
    sl_bggpu_bind(1);
    sl_bggpu_bind(0);
    CHECK(bg.calls == 0, "an untried module binds nothing (%ld calls)", bg.calls);

    CHECK(sl_bggpu_ready() == 1, "ready");
    binds = bg.use_program;
    /* NOTHING WAS BOUND. This is a no-op in STATE, not in CALLS, and the
     * numbers below are the measured behaviour rather than a wish: the module
     * guards only on its own capability (g_state, p_UseProgram), so bind(0)
     * with the fixed pipeline already current issues one redundant
     * glUseProgram(0). The header claims only "the program current (on != 0)
     * or the fixed pipeline back (on == 0)", which that satisfies, and a
     * redundant glUseProgram(0) is a state-setting call with no sync point and
     * no object lifetime attached. The count is pinned here so that making it
     * a true no-op is a deliberate change rather than a silent one. */
    sl_bggpu_bind(0);
    CHECK(bg.use_program == binds + 1,
          "bind(0) with nothing bound issues one redundant glUseProgram(0) (%ld)",
          bg.use_program - binds);
    CHECK(bg.current == 0,
          "and the fixed pipeline was and stays current, so it is a no-op in state");
    sl_bggpu_bind(0);
    CHECK(bg.current == 0, "and again");
    sl_bggpu_bind(1);
    CHECK(bg.current == bg.last_program, "bind(1) makes the program current");
    sl_bggpu_bind(0);
    CHECK(bg.current == 0, "bind(0) puts the fixed pipeline back");

    /* A LATCHED FAILURE BINDS NOTHING. */
    sl_bggpu_invalidate();
    bg.fail_compile = 1;
    CHECK(sl_bggpu_ready() == 0 && sl_bggpu_state() == -1, "a real failure is latched");
    calls = bg.calls;
    sl_bggpu_bind(1);
    sl_bggpu_bind(0);
    CHECK(bg.calls == calls, "an unavailable module binds nothing (%ld)",
          bg.calls - calls);
    bg.fail_compile = 0;
    end_case();
}

/* ---- 7. no sync point -------------------------------------------------- */

/* THIS IS CHECKED BY THE BUILD, NOT BY AN ASSERTION. The stub provides no
 * glFinish, glFlush, glGetError, glMapBuffer, glUnmapBuffer, glReadPixels,
 * glGetUniformfv, glDeleteShader or glDeleteProgram - neither as a symbol nor
 * in sl_gl_proc's table. A direct call to one would be an undefined symbol at
 * LINK time, and a resolve of one would return NULL and make the module refuse
 * its own capability, which the cases above would see as a failure to become
 * ready. A counter can only speak for the paths a test drives; a missing
 * symbol speaks for the whole translation unit. The one assertion that belongs
 * here is the count of what the module DOES resolve, which st_row_drives makes:
 * exactly sixteen entry points, all of them GL 2.0 program calls. */
static void st_no_sync_point(void)
{
    begin_case("sync: no sync point, read-back or delete entry point exists to call");
    arm_on();
    CHECK(sl_bggpu_ready() == 1, "the module becomes ready with only those sixteen present");
    CHECK(bg.proc_calls == 16, "and resolves exactly sixteen (%ld)", bg.proc_calls);
    CHECK(sl_gl_proc("glFinish") == NULL && sl_gl_proc("glFlush") == NULL
          && sl_gl_proc("glGetError") == NULL && sl_gl_proc("glReadPixels") == NULL
          && sl_gl_proc("glMapBuffer") == NULL && sl_gl_proc("glGetUniformfv") == NULL
          && sl_gl_proc("glDeleteShader") == NULL && sl_gl_proc("glDeleteProgram") == NULL,
          "no sync point, read-back or delete resolves here at all");
    end_case();
}

/* ---- 8. precedence: an explicit menu choice ends an inherited switch ---- */

/* putenv takes a modifiable string, and this runtime copies it, which is the
 * idiom src/platform/sl_perf.c's own fixture uses. An empty value unsets as
 * far as every reader here is concerned: perf_env01 accepts exactly "0" or
 * exactly "1", so "" leaves the row governing. */
static void put(const char *kv) { putenv((char *) kv); }

/* THE PRECEDENCE RULE, DRIVEN THROUGH THE REAL MODULE rather than through the
 * policy's own fixture. src/platform/sl_perf.c resolves the two background
 * enablements through a latch: before the player has touched the row a
 * subsystem switch governs its own subsystem, which is what makes a
 * launch-time diagnostic work; after sl_perf_opt_set the ROW governs and
 * SL_BG_GPU_XFORM / SL_BG_GPU_BYPASS are not consulted again for the launch.
 *
 * THE DEFECT THAT REMOVES: a session that had inherited SL_BG_GPU_XFORM=1 kept
 * the transform selected after the player set the row to OFF, so both menus
 * displayed OFF while the transform was still running. The headline assertion
 * below is sl_bggpu_opt_in() answering 0 after that press.
 *
 * WHY IT IS ASSERTED HERE AND NOT ONLY IN THE POLICY'S FIXTURE: the module is
 * what the pass actually asks, and sl_bggpu_opt_in is a call into the policy
 * that could have cached, latched or re-ordered the answer on its own. The
 * stub's counters are what prove the second half - that the row PARKS the
 * selection rather than tearing the program down, so a press costs no
 * recompile either way. */
static void st_precedence(void)
{
    struct sl_bggpu_walk w = a_walk();
    long calls, compile, program, locs, binds;
    GLuint prog;
    int n;

    begin_case("precedence: a menu press ends an inherited SL_BG_GPU_XFORM / _BYPASS, and parks rather than rebuilds");

    /* 1. THE INHERITED DIAGNOSTIC STATE. The switch is set, the latch is
     *    clear, and the row is written straight to the store - NOT through
     *    sl_perf_opt_set, which would set the latch and make this step
     *    something else. */
    sl_bggpu_invalidate();
    bgt_reset();
    put("SL_BG_GPU_XFORM=1");
    sl_perf_test_reset();
    sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
    CHECK(sl_perf_opt() == 1, "the row reads ON before any press");
    CHECK(sl_perf_opt_env() == 0, "and no SL_PERF_OPT override is in force");
    CHECK(sl_bggpu_opt_in() == 1, "the inherited SL_BG_GPU_XFORM=1 selects the transform");
    CHECK(sl_bggpu_ready() == 1, "so the first walk resolves and compiles");
    CHECK(sl_bggpu_state() == 1, "and the capability is READY");
    CHECK(bg.compile == 1 && bg.link == 1 && bg.get_uniform_location == 8,
          "the program compiled, linked and resolved every uniform");
    prog = bg.last_program;

    /* 2. THE PRESS OFF, with SL_BG_GPU_XFORM=1 STILL SET. This is the defect:
     *    the transform used to stay selected here while both menus read OFF. */
    calls = bg.calls; compile = bg.compile; program = bg.create_program;
    locs = bg.get_uniform_location;
    sl_perf_opt_set(0);
    CHECK(sl_bggpu_opt_in() == 0,
          "A PRESS OFF ENDS THE INHERITED SWITCH: the transform is not selected "
          "(SL_BG_GPU_XFORM=1 is still set)");
    CHECK(sl_perf_opt() == 0, "and the row - what both menus display - reads OFF");
    /* The row PARKS the selection. Nothing is torn down: the capability stays
     * READY, no GL call is made by the press, and nothing is deleted - the
     * stub has no glDeleteShader or glDeleteProgram at all, so a teardown
     * would not even link. */
    CHECK(sl_bggpu_state() == 1, "the capability is still READY, not retried or lost");
    CHECK(bg.calls == calls, "the press made no GL call (%ld -> %ld)", calls, bg.calls);
    CHECK(bg.compile == compile && bg.create_program == program
          && bg.get_uniform_location == locs,
          "and recompiled nothing (compile %ld, program %ld, locations %ld)",
          bg.compile, bg.create_program, bg.get_uniform_location);

    /* 3. THE PRESS BACK ON reuses the program rather than rebuilding it. */
    sl_perf_opt_set(1);
    CHECK(sl_bggpu_opt_in() == 1, "a press ON selects the transform again");
    CHECK(sl_bggpu_ready() == 1, "the capability is still READY");
    CHECK(bg.compile == compile && bg.create_program == program
          && bg.get_uniform_location == locs,
          "and the press reused the program rather than rebuilding it "
          "(compile %ld, program %ld, locations %ld)",
          bg.compile, bg.create_program, bg.get_uniform_location);
    n = bg.nu;
    binds = bg.use_program;
    CHECK(sl_bggpu_use_walk(&w) == 1, "the parked program serves a walk again");
    CHECK(bg.nu - n == 8 && bg.u[n].prog == prog,
          "into the SAME program name as before the two presses");
    CHECK(bg.use_program - binds == 2, "with one bind and one unbind");

    /* 4. THE DEPENDENT LOOP REDUCTION follows the same three steps, and the
     *    press governs in BOTH directions - an inherited 0 is ended too, not
     *    just an inherited 1. */
    put("SL_BG_GPU_XFORM=");
    put("SL_BG_GPU_BYPASS=1");
    sl_perf_test_reset();
    CHECK(sl_perf_bg_bypass() == 1, "the inherited SL_BG_GPU_BYPASS=1 selects the reduction");
    sl_perf_opt_set(0);
    CHECK(sl_perf_bg_bypass() == 0, "a press OFF ends it");
    sl_perf_opt_set(1);
    CHECK(sl_perf_bg_bypass() == 1, "and a press ON selects it from the row");
    put("SL_BG_GPU_BYPASS=0");
    sl_perf_test_reset();
    CHECK(sl_perf_bg_bypass() == 0, "an inherited SL_BG_GPU_BYPASS=0 refuses the reduction");
    sl_perf_opt_set(1);
    CHECK(sl_perf_bg_bypass() == 1,
          "and a press ON overrides THAT too: the latch governs in both directions");

    /* 5. THE FAR ADMISSION: WHICH SWITCH SURVIVES A PRESS, AND WHICH DOES NOT.
     *    Two different switches reach sl_perf_bg_far_admit, and treating them
     *    as one - calling an inherited SL_BG_GPU_XFORM=1 "the far arm" - is the
     *    defect this block is written to catch, so they are driven separately
     *    and named separately below:
     *
     *      SL_PERF_BG_FAR IS THE EXPLICIT ARM. It is the switch a qualification
     *      run binds ON PURPOSE, and a menu press must not disarm the run it is
     *      part of, so it survives a press in BOTH directions - a named 1 keeps
     *      admitting and a named 0 keeps refusing.
     *
     *      SL_BG_GPU_XFORM=1 IS NOT AN ARM. It widened the admission as a side
     *      effect of the sprint's diagnostic scope, and nothing binds it for
     *      the far walk's sake, so a press ENDS it: honouring one a session
     *      happened to inherit after the player had chosen the row would put
     *      the unqualified two-range far walk back on the GPU behind a menu
     *      that had just been set.
     *
     *    The order is the header's (sl_perf.h, sl_perf_bg_far_admit): the
     *    explicit flag, then the row's refusal once the row has been chosen,
     *    then the diagnostic scope before any press, then refuse. Neither
     *    switch is an enablement or a carrier - with the transform off the
     *    admission selects nothing at all, which the press-OFF checks show. */
    put("SL_BG_GPU_BYPASS=");
    put("SL_BG_GPU_XFORM=1");
    put("SL_PERF_BG_FAR=");
    sl_perf_test_reset();
    CHECK(sl_perf_bg_far_admit() == 1,
          "BEFORE any press, an inherited SL_BG_GPU_XFORM=1 admits the far walk "
          "- the sprint's diagnostic scope, and the only state in which it does");
    sl_perf_opt_set(1);
    CHECK(sl_perf_bg_far_admit() == 0,
          "AFTER a press, an inherited SL_BG_GPU_XFORM=1 admits no far walk");
    CHECK(sl_perf_bg_xform() == 1 && sl_bggpu_opt_in() == 1,
          "while the transform itself is selected, by the row the press governs");
    sl_perf_opt_set(0);
    CHECK(sl_perf_bg_far_admit() == 0,
          "and a press OFF admits none either, with the switch still set");

    /* THE EXPLICIT ARM, on its own: a named 1 admits before a press and
     * survives one in both directions. */
    put("SL_BG_GPU_XFORM=");
    put("SL_PERF_BG_FAR=1");
    sl_perf_test_reset();
    CHECK(sl_perf_bg_far_admit() == 1, "SL_PERF_BG_FAR=1 admits before any press");
    sl_perf_opt_set(1);
    CHECK(sl_perf_bg_far_admit() == 1, "and SURVIVES a press ON");
    sl_perf_opt_set(0);
    CHECK(sl_perf_bg_far_admit() == 1 && sl_bggpu_opt_in() == 0,
          "and survives a press OFF - where it selects nothing, because no walk "
          "is admitted to a transform that is not selected");

    /* AND IT BEATS THE INHERITED ONE IN BOTH DIRECTIONS: a named 0 refuses even
     * with the diagnostic scope's switch set beside it, before and after a
     * press. */
    put("SL_PERF_BG_FAR=0");
    put("SL_BG_GPU_XFORM=1");
    sl_perf_test_reset();
    CHECK(sl_perf_bg_far_admit() == 0,
          "SL_PERF_BG_FAR=0 refuses before a press, with SL_BG_GPU_XFORM=1 set too");
    sl_perf_opt_set(1);
    CHECK(sl_perf_bg_far_admit() == 0, "and still refuses after a press ON");
    sl_perf_opt_set(0);
    CHECK(sl_perf_bg_far_admit() == 0, "and after a press OFF");

    /* The environment and the latch, left as the fixture found them. */
    put("SL_PERF_BG_FAR=");
    put("SL_BG_GPU_XFORM=");
    put("SL_BG_GPU_BYPASS=");
    sl_perf_test_reset();
    CHECK(sl_perf_bg_far_admit() == 0 && sl_perf_bg_bypass() == 0,
          "with the environment cleared and the latch dropped, the row's own "
          "answers are back (the row is OFF from the press above)");
    end_case();
}

/* ---- the fixture -------------------------------------------------------- */

int main(void)
{
    static char cfg[520];
    const char *tmp = getenv("TEMP");
    const char *path;

    printf("background GPU transform fixture (src/gfx/sl_gfx_bggpu.c, SYNTHETIC GL)\n\n");

    /* The REAL settings store, pointed at a scratch file under %TEMP% so the
     * player's own config is never opened. sl_perf_test_reset drops the
     * policy's cached environment answers after the putenv, so the row - not a
     * developer switch read before this ran - governs every answer below. */
    if (tmp == NULL || *tmp == '\0') tmp = ".";
    snprintf(cfg, sizeof cfg, "SL_CONFIG=%s\\sl_bggpu_fixture_config.ini", tmp);
    putenv(cfg);
    path = strchr(cfg, '=') + 1;
    remove(path);
    sl_perf_test_reset();
    sl_settings_init();

    bgt_reset();
    CHECK(sl_settings_active() == 1, "the settings store is ACTIVE");
    CHECK(sl_settings_get(SL_SET_OPTIMIZED_RENDERING) == 1,
          "OPTIMIZED RENDERING is at its real default, 1");
    CHECK(sl_perf_bg_xform() == 1, "so the policy selects the transform");
    CHECK(sl_bggpu_opt_in() == 1, "and so does the module's own question");
    CHECK(sl_bggpu_state() == 0, "the module starts UNTRIED");
    CHECK(bg.calls == 0 && bg.proc_calls == 0,
          "and nothing has touched the fake driver yet");

    st_row_drives();
    st_failures();
    st_context();
    st_use_walk();
    st_depth();
    st_bind();
    st_no_sync_point();
    /* LAST, because it is the only case that drives the policy's launch-time
     * state: it clears the row-chosen latch to reach the inherited-switch half
     * of the precedence rule, and leaves the environment and the latch clear
     * again at its end. */
    st_precedence();

    remove(path);
    printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
    printf("NOT COVERED: the vertex program's own depth arithmetic - the GLSL in "
           "src/gfx/sl_gfx_bggpu.c that computes the one-range far clamp and the "
           "FAR0 / FAR1 depths - is not executed by this fixture, which has no GLSL "
           "compiler and no GPU and only records that the text was handed to "
           "glShaderSource, so the far-depth expressions' clamp and ranges as "
           "actually evaluated on a driver, and the resulting depth-buffer ordering, "
           "remain unqualified.\n");
    return fails == 0 ? 0 : 1;
}
