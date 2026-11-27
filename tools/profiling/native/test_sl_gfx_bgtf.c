/* Offline tests for the background pass's GPU CLIP PREPASS
 * (src/gfx/sl_gfx_bgtf.c).
 *
 * WHAT THIS IS. A pure fixture executable. The module is compiled against the
 * SYNTHETIC GL in sl_gfx_bgtf_testgl.c - no window, no native graphics API, no
 * renderer, no game - and the REAL far-range arithmetic
 * (src/gfx/sl_gfx_worldasset.c) is linked in rather than mocked, so every FAR0
 * coefficient pair below comes out of sl_wa_far_range at the real overlap
 * constants src/gfx/sl_gfx_dl.c passes it. No coefficient is hand-written.
 *
 * The module has NO POLICY DEPENDENCY, and that is itself a measured fact of
 * its shape rather than an omission here: its selection is the SL_BG_TF_CLIP
 * environment switch alone, so neither src/platform/sl_perf.c nor the
 * OPTIMIZED RENDERING row is reachable from it and neither is linked. The
 * settings store is linked only because sl_gfx_worldasset.c needs it, and it
 * is pointed at a scratch SL_CONFIG under %TEMP% so the player's own config is
 * never opened.
 *
 * HOW THE MODULE'S GL INCLUDES ARE REPLACED. sl_gfx_bgtf.c has no test hook: it
 * includes <windows.h> under _WIN32 and then <GL/gl.h>, unconditionally, with
 * no SL_*_TEST macro around either, and src/ may not be edited to add one. The
 * fixture therefore injects sl_gfx_bgtf_testgl.h ahead of that translation unit
 * with -include, and that header defines the host headers' own include guards
 * so the real headers expand to nothing. See the long note at the top of the
 * header for the guard names, where they were read, and why this was preferred
 * over a directory of stub windows.h / GL/gl.h files.
 *
 * THE SWITCH IS PARSED ONCE PER PROCESS, which decides the shape of this
 * fixture. sl_bgtf_opt_in caches its answer in a static and sl_bgtf_invalidate
 * deliberately does not re-read it - the switch belongs to the launch, not to a
 * context - so ONE executable can observe exactly ONE value of SL_BG_TF_CLIP.
 * The environment cases are therefore separate runs of this same binary in
 * `env` mode, driven by tools/windows/bgtftest.ps1, which sets the variable
 * each way and asserts the answer. Faking it with a reset hook would mean
 * editing src/, and asserting it from one run would mean asserting the cached
 * answer five times.
 *
 * WHAT THIS FIXTURE DOES NOT QUALIFY. The prepass program's own arithmetic is
 * NEVER EXECUTED: the GLSL in sl_gfx_bgtf.c is only handed to glShaderSource as
 * text and recorded, because there is no GLSL compiler and no GPU in this
 * process. So what the program computes, whether a driver's transform feedback
 * writes the pool this module's indices then read, and whether the resulting
 * image matches the original path, are all outside this fixture. What is proved
 * here is the module's own logic: what it resolves, what it compiles, what it
 * latches, what it submits and in what order, which state it changes and puts
 * back, which pool a key gets, and what it refuses.
 *
 * Usage: test_sl_gfx_bgtf.exe                 the full suite (needs =1)
 *        test_sl_gfx_bgtf.exe env 0|1         the switch as the launch set it
 *        test_sl_gfx_bgtf.exe envempty        an empty value, set in-process
 *        test_sl_gfx_bgtf.exe dump <scenario> one production-buffer-diagnostic
 *                                             scenario; see case 17 for why
 *                                             each one needs its own process
 *        test_sl_gfx_bgtf.exe prod <case>    one PRODUCTION-SELECTION case
 *
 * v0.4 THE PRODUCTION MODE'S EXACT INVOCATIONS, written down here because they
 * are NOT in tools/windows/bgtftest.ps1 and must not be guessed at. Mode `prod`
 * follows mode `optin`'s convention exactly: the arbiter decides once per
 * launch, so each case is ONE PROCESS with its own environment, and the runner
 * table drives neither. Every case below requires SL_BG_TF_CLIP,
 * SL_BG_TF_AUX_REAL, SL_BG_TF_AUX_SYNC and SL_BG_TF_RASTER_CPU to be CLEARED
 * except where it names one; each dispatcher case verifies its own launch and
 * REFUSES with exit 2 rather than reporting another launch's numbers. Exit 0 is
 * a pass for every one of them.
 *
 *   (no SL_BG_TF_* set)                      prod row_on
 *   (no SL_BG_TF_* set)                      prod row_off
 *   (no SL_BG_TF_* set)                      prod transition
 *   (no SL_BG_TF_* set)                      prod cap
 *   SL_BG_TF_CLIP=0                          prod clip_off
 *   SL_BG_TF_AUX_REAL=OFF                    prod arm_off
 *   SL_BG_TF_AUX_REAL=NOT_A_TOKEN            prod arm_bad
 *   SL_BG_TF_RASTER_CPU=1                    prod diag
 *   SL_BG_TF_AUX_SYNC=NOT_A_TOKEN            prod sync_bad
 *
 * Under cmd, one case per line, from the directory holding the built exe:
 *
 *   set SL_BG_TF_CLIP=&& set SL_BG_TF_AUX_REAL=&& set SL_BG_TF_AUX_SYNC=&& ^
 *     set SL_BG_TF_RASTER_CPU=&& test_sl_gfx_bgtf.exe prod row_on
 *
 * NOT COMPILED OR RUN IN THE ROUNDS THAT WROTE THEM. These cases and this list
 * were authored against the module's declared contract under an assignment that
 * forbade any build, so no line of mode `prod` has been executed.
 */
#include "sl_gfx_bgtf_testgl.h"

#include "sl_gfx_bgtf.h"
#include "sl_gfx_worldasset.h"
#include "sl_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* _fsopen and _SH_DENYRD, which is how one scenario makes an EXISTING file
 * genuinely unreadable without leaving the C runtime: it holds the file open
 * with read sharing denied for the length of the capture. That is the condition
 * the writer's old probe-then-truncate design lost to, and no other portable
 * interface produces it on demand. */
#include <share.h>

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

/* THE TWO WATCHDOGS A CASE MAY EXPECT A NONZERO VALUE FROM, and why they are
 * variables rather than relaxations.
 *
 * `v_draw_without_discard` counts a draw that could reach a fragment and
 * `v_draw_without_program` a draw with no program current. Both are 0 on every
 * arm this renderer has ever had, and both are exactly what two rows of
 * SL_BG_TF_EMPTY_STATE create on purpose: a D0 row draws with discard off, and
 * the program-0 row draws with program 0. For those rows a 0 here would mean the
 * row did not happen.
 *
 * SO THE EXPECTATION IS DECLARED BY THE CASE AND THE WATCHDOG STILL FIRES.
 * Dropping either check for those rows would remove it from the arms that most
 * need an exact count; raising it globally would stop it catching the thing it
 * exists for anywhere else. begin_case resets both to 0, so a case that declares
 * nothing is held to 0 and an expectation cannot leak between cases.
 *
 * AND THE DRAW IS EMPTY ON EVERY ONE OF THOSE ROWS. A draw over no vertices
 * produces no fragment whatever the discard state and reads no uniform whatever
 * the program, which is an API-boundary fact about a count of zero and not a
 * claim about a driver. */
static long expect_draw_without_discard;
static long expect_draw_without_program;

static void begin_case(const char *name)
{
    cases++;
    case_fails = 0;
    current_case = name;
    expect_draw_without_discard = 0;
    expect_draw_without_program = 0;
    printf("  %s\n", name);
}

static void end_case(void)
{
    /* Every case ends by proving the watchdogs stayed silent. These are the
     * properties no single assertion covers, because they are about calls a
     * test did not go looking for: a name from a replaced context, a call after
     * teardown, a source set on a name that was never created, a prepass draw
     * that could have reached a fragment or recorded into nothing or run with
     * no program, a transform-feedback scope that nested or ended unopened, a
     * draw that would write past its pool, and a token the stub does not
     * model. Three of them are proven ABLE TO FIRE by st_watchdogs. */
    CHECK(tf.v_stale_name == 0, "%s: a name from an earlier context was used (%ld)",
          current_case, tf.v_stale_name);
    CHECK(tf.v_call_after_context_destroyed == 0,
          "%s: %ld calls after the context was destroyed",
          current_case, tf.v_call_after_context_destroyed);
    CHECK(tf.v_source_without_shader == 0,
          "%s: shader source set on a name that was never created (%ld)",
          current_case, tf.v_source_without_shader);
    CHECK(tf.v_draw_without_discard == expect_draw_without_discard,
          "%s: prepass draws that ran with rasterizer discard off: %ld, "
          "expected %ld", current_case, tf.v_draw_without_discard,
          expect_draw_without_discard);
    CHECK(tf.v_draw_without_tf_buffer == 0,
          "%s: transform feedback recorded with nothing bound at index 0 (%ld)",
          current_case, tf.v_draw_without_tf_buffer);
    CHECK(tf.v_draw_without_program == expect_draw_without_program,
          "%s: prepass draws that ran with no program current: %ld, expected "
          "%ld", current_case, tf.v_draw_without_program,
          expect_draw_without_program);
    /* NEVER EXPECTED, ON ANY ROW. glUseProgram inside a feedback scope is
     * invalid; the one program-0 row is a feedback-INACTIVE row, so this stays 0
     * whatever is selected. */
    CHECK(tf.v_program_change_in_tf == 0,
          "%s: the program was changed inside an open transform-feedback scope, "
          "which the API forbids (%ld)", current_case,
          tf.v_program_change_in_tf);
    CHECK(tf.v_tf_nested == 0, "%s: a transform-feedback scope nested (%ld)",
          current_case, tf.v_tf_nested);
    CHECK(tf.v_tf_unbalanced == 0, "%s: a transform-feedback scope ended unopened (%ld)",
          current_case, tf.v_tf_unbalanced);
    CHECK(tf.v_tf_overrun == 0, "%s: a draw would write past the bound pool (%ld)",
          current_case, tf.v_tf_overrun);
    CHECK(tf.v_unknown_enum == 0, "%s: a token the stub does not model (%ld)",
          current_case, tf.v_unknown_enum);
    CHECK(tf.v_log_full == 0, "%s: the transcript overflowed and is incomplete (%ld)",
          current_case, tf.v_log_full);
    printf("    %s\n", case_fails == 0 ? "ok" : "FAILED");
}

/* The provider's player-data ladder, which has no module in an offline build.
 * sl_gfx_worldasset.c declares it; it is linked in for sl_wa_far_range only,
 * and nothing this fixture calls reaches a file. */
const char *sl_asset_override_dir(void) { return NULL; }

/* ---- the fixture's own constants ---------------------------------------- */

/* A vertex count that is not a round number, so an off-by-one in a count or a
 * size would not land on a plausible value, and an item stride that is not the
 * output stride, so the input and output descriptions cannot be confused. */
#define NV   257u
#define STR   32u

/* The module's own slot count, from the header ("FOUR SLOTS"). Pinned here
 * rather than read, because the module does not export it: a change to
 * TF_SLOTS has to come past this fixture. */
#define SLOTS 4

/* The vertex-count ceiling the module computes before it allocates:
 * 0x7FFFFFFF / (4 * sizeof(GLfloat)). Transcribed from sl_bgtf_begin_walk, not
 * guessed - the point of the case is that the limit is expressed on the COUNT,
 * so the byte multiply is never performed on an unchecked value. */
#define VCAP (0x7FFFFFFFu / (4u * (unsigned) sizeof(GLfloat)))

/* ---- reading the stub's record: the uniform log ------------------------- */

/* The uniform the module is expected to have written at log index i. Each
 * helper names the uniform, so a failure says which one moved. */
static int u_is_1i(int i, const char *name, GLint v)
{
    return i >= 0 && i < tf.nu && tf.u[i].kind == BGTF_U1I
           && tf.u[i].loc == sl_bgtfgl_loc(name) && tf.u[i].ival == v;
}

static int u_is_1f(int i, const char *name, float v)
{
    return i >= 0 && i < tf.nu && tf.u[i].kind == BGTF_U1F
           && tf.u[i].loc == sl_bgtfgl_loc(name) && tf.u[i].fv[0] == v;
}

static int u_is_3f(int i, const char *name, float a, float b, float c)
{
    return i >= 0 && i < tf.nu && tf.u[i].kind == BGTF_U3F
           && tf.u[i].loc == sl_bgtfgl_loc(name)
           && tf.u[i].fv[0] == a && tf.u[i].fv[1] == b && tf.u[i].fv[2] == c;
}

static int u_is_1fv(int i, const char *name, const float *v, int n)
{
    int k;
    if (!(i >= 0 && i < tf.nu && tf.u[i].kind == BGTF_U1FV
          && tf.u[i].loc == sl_bgtfgl_loc(name) && tf.u[i].count == n))
        return 0;
    for (k = 0; k < n; k++) if (tf.u[i].fv[k] != v[k]) return 0;
    return 1;
}

/* One depth upload, as sl_bgtf_prepass makes it: the mode, then the two
 * coefficients AS PASSED, in that order. The module uploads the pair whatever
 * the mode, so the program never carries a previous draw's; which of them the
 * program READS is a property of the GLSL and is not executed here. */
static int u_is_depth(int i, int mode, float k1, float k2)
{
    return u_is_1i(i, "u_dmode", (GLint) mode)
           && u_is_1f(i + 1, "u_fk1", k1)
           && u_is_1f(i + 2, "u_fk2", k2);
}

/* ---- reading the stub's record: the ordered transcript ------------------ */

/* A field a test does not constrain. The transcript carries buffer and program
 * NAMES, which are the stub's invention and differ per generation, so some
 * records are asserted by shape and some by value. */
#define ANY (-999999L)

/* WHICH CONTEXT A RECORD IS REQUIRED TO HAVE LANDED ON. The shim stamps every
 * call with 0 main, 1 auxiliary or -1 none, and one control's whole claim is
 * about placement - so a sequence has to be able to require it.
 *
 * IT IS A PARALLEL ARRAY AND NOT A FIELD OF `struct expect`, deliberately.
 * Several suites build their expectations as brace-initialised arrays, and a
 * sixth field would be left unset in every one of them: that is both a wall of
 * missing-initializer warnings and, worse, a silent assertion - the unset value
 * would read as "the MAIN context", which those suites would then be requiring
 * without having asked. So the placement check is its own function over the same
 * window, and a sequence that does not call it asks nothing. */
#define CTX_MAIN  0
#define CTX_AUX   1

struct expect {
    int         kind;
    long        a, b, c, d;
    const char *what;
};

static const char *kind_name(int k)
{
    switch (k) {
    case TFGL_USEPROGRAM:     return "glUseProgram";
    case TFGL_UNIFORM:        return "glUniform*";
    case TFGL_GENBUFFERS:     return "glGenBuffers";
    case TFGL_BINDBUFFER:     return "glBindBuffer";
    case TFGL_BUFFERDATA:     return "glBufferData";
    case TFGL_GETBUFPARAM:    return "glGetBufferParameteriv";
    case TFGL_BINDBUFFERBASE: return "glBindBufferBase";
    case TFGL_BEGINTF:        return "glBeginTransformFeedback";
    case TFGL_ENDTF:          return "glEndTransformFeedback";
    case TFGL_DRAWARRAYS:     return "glDrawArrays";
    case TFGL_VERTEXPOINTER:  return "glVertexPointer";
    case TFGL_ENABLE:         return "glEnable";
    case TFGL_DISABLE:        return "glDisable";
    case TFGL_ISENABLED:      return "glIsEnabled";
    case TFGL_GETINTEGERV:    return "glGetIntegerv";
    case TFGL_CLIENTSTATE:    return "glEnable/DisableClientState";
    case TFGL_CLIENTACTIVETEX: return "glClientActiveTexture";
    case TFGL_DELETEPROGRAM:  return "glDeleteProgram";
    case TFGL_DELETESHADER:   return "glDeleteShader";
    case TFGL_ISPROGRAM:      return "glIsProgram";
    case TFGL_ISBUFFER:       return "glIsBuffer";
    case TFGL_GENVAO:         return "glGenVertexArrays";
    case TFGL_BINDVAO:        return "glBindVertexArray";
    case TFGL_FENCESYNC:      return "glFenceSync";
    case TFGL_WAITSYNC:       return "glWaitSync";
    case TFGL_DELETESYNC:     return "glDeleteSync";
    case TFGL_FLUSH:          return "glFlush";
    default:                  return "(none)";
    }
}

/* The transcript from `from` is exactly these n records, in this order, and
 * nothing follows them. Each mismatch is reported against the record's own
 * name, so a failure says which call moved rather than that a sequence
 * differed. */
static void seq_is(long from, const struct expect *e, int n)
{
    long i;
    for (i = 0; i < n; i++) {
        const struct sl_bgtfgl_call *r;
        if (from + i >= tf.nlog) {
            CHECK(0, "the transcript ended at record %ld, before '%s' (%s)",
                  i, e[i].what, kind_name(e[i].kind));
            return;
        }
        r = &tf.log[from + i];
        CHECK(r->kind == e[i].kind,
              "record %ld is %s, expected %s (%s)",
              i, kind_name(r->kind), kind_name(e[i].kind), e[i].what);
        if (r->kind != e[i].kind) continue;
        if (e[i].a != ANY) CHECK(r->a == e[i].a, "record %ld (%s, %s): a = %ld, expected %ld",
                                 i, kind_name(r->kind), e[i].what, r->a, e[i].a);
        if (e[i].b != ANY) CHECK(r->b == e[i].b, "record %ld (%s, %s): b = %ld, expected %ld",
                                 i, kind_name(r->kind), e[i].what, r->b, e[i].b);
        if (e[i].c != ANY) CHECK(r->c == e[i].c, "record %ld (%s, %s): c = %ld, expected %ld",
                                 i, kind_name(r->kind), e[i].what, r->c, e[i].c);
        if (e[i].d != ANY) CHECK(r->d == e[i].d, "record %ld (%s, %s): d = %ld, expected %ld",
                                 i, kind_name(r->kind), e[i].what, r->d, e[i].d);
    }
    CHECK(tf.nlog == from + n,
          "the transcript holds exactly %d records and nothing after them (%ld)",
          n, tf.nlog - from);
}

/* AND WHICH CONTEXT EACH OF THOSE RECORDS LANDED ON, over the same window.
 *
 * WHY IT IS A SECOND PASS. seq_is asks about a call's identity and its
 * arguments; this asks where the call went, which is not in any argument. The
 * two are kept apart so that a sequence can require order without requiring
 * placement, which is what every expectation written before a second context
 * existed does.
 *
 * `want` is one entry per record, CTX_MAIN or CTX_AUX, and `e` is the SAME array
 * seq_is was given so a failure can name the call rather than an index. A
 * mismatch is reported per record, so a bracket that put one call on the wrong
 * context says which one. */
static void seq_ctx_is(long from, const struct expect *e, const int *want,
                       int n)
{
    long i;
    for (i = 0; i < n; i++) {
        const struct sl_bgtfgl_call *r;
        if (from + i >= tf.nlog) return;     /* seq_is has already said so */
        r = &tf.log[from + i];
        CHECK(r->ctx == want[i],
              "record %ld (%s, %s): landed on context %d, expected %d "
              "(0 the MAIN context, 1 the AUXILIARY one, -1 none)",
              i, kind_name(r->kind), e[i].what, r->ctx, want[i]);
    }
}

/* The first record of a kind at or after `from`, or NULL. */
static const struct sl_bgtfgl_call *first_of(long from, int kind)
{
    long i;
    for (i = from; i < tf.nlog; i++) if (tf.log[i].kind == kind) return &tf.log[i];
    return NULL;
}

/* ---- the module, armed -------------------------------------------------- */

/* A clean fake driver and an untried module. The COUNTERS ARE NOT RESET: the
 * module's census is cumulative for the process and sl_bgtf_invalidate
 * deliberately leaves it alone, so every count below is read as a delta. */
static void arm(void)
{
    sl_bgtf_invalidate();
    sl_bgtfgl_reset();
}

static struct sl_bgtf_counters snap(void) { return *sl_bgtf_stats(); }

/* The pass's own static vertex buffer: a name the CALLER owns, which the
 * prepass reads rather than creates. Minted in the generation in force, so it
 * has to be taken again after a reset or a context change. */
static GLuint input_vbo(unsigned nverts, unsigned stride)
{
    return sl_bgtfgl_make_buffer((long) nverts * (long) stride);
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

/* ---- the real far-range coefficients ------------------------------------ */

/* FAR0's two pairs, from the REAL sl_wa_far_range at the REAL overlap
 * constants: every mesh takes the opaque pair and a blended mesh the blend
 * overlap's, which is exactly the distinction the keyed slot cache exists to
 * keep apart. The far plane and the furthest w are SYNTHETIC; the pairs
 * derived from them are not. */
static float K1, K2;               /* the opaque overlap's pair */
static float T1, T2;               /* the blend overlap's pair  */
static const float ZFAR = 15000.0f, WMAX = 20000.0f;

static void real_pairs(void)
{
    sl_wa_far_range(ZFAR * SL_WA_FAR_OVERLAP, WMAX, &K1, &K2);
    sl_wa_far_range(ZFAR * SL_WA_FAR_OVERLAP_BLEND, WMAX, &T1, &T2);
}

/* ---- 1. the capability answer ------------------------------------------- */

static void st_ready(void)
{
    int i;

    begin_case("ready: exactly twenty-four entry points, one program, the varying recorded "
               "BEFORE the link, every uniform located");
    arm();
    CHECK(sl_bgtf_ready() == 1, "the first walk with the switch on is ready");
    CHECK(sl_bgtf_state() == 1, "and the state is READY (state=%d)", sl_bgtf_state());

    CHECK(tf.proc_calls == BGTFGL_PROCS,
          "exactly %d entry points were resolved (%ld)", BGTFGL_PROCS, tf.proc_calls);
    CHECK(tf.nasked == BGTFGL_PROCS, "and %d names were asked for (%d)",
          BGTFGL_PROCS, tf.nasked);
    /* A COUNT ALONE WOULD NOT SAY WHICH. The resolved names are compared one
     * by one against the expected list, in the order the module asks: the
     * sixteen GL 2.0 program calls, the four transform-feedback calls, the
     * four buffer-object calls - the last being glGetBufferParameteriv, the
     * allocation-acceptance query, which is required like any other. */
    for (i = 0; i < BGTFGL_PROCS && i < tf.nasked; i++)
        CHECK(strcmp(tf.asked[i], sl_bgtfgl_proc_names[i]) == 0,
              "resolve %d is '%s', expected '%s'", i, tf.asked[i],
              sl_bgtfgl_proc_names[i]);

    CHECK(tf.create_shader == 1 && tf.shader_source == 1 && tf.compile == 1,
          "one shader was created, sourced and compiled");
    CHECK(tf.create_program == 1 && tf.attach == 1 && tf.link == 1,
          "one program was created, attached to and linked");
    CHECK(tf.last_source != NULL && tf.last_source_count == 1,
          "the program text was handed over once, in one string");
    CHECK(tf.last_source != NULL
          && strstr(tf.last_source, "#version 400 compatibility") != NULL,
          "the text handed to glShaderSource is the module's own program");
    /* THE PREPASS WRITES A RECORDED VARYING, NOT gl_Position. That is the one
     * difference from the other module's program, and it is what makes the
     * output an indexable pool rather than a rasterised draw. */
    CHECK(tf.last_source != NULL && strstr(tf.last_source, "out vec4 v_clip") != NULL,
          "it declares the recorded output varying");
    CHECK(tf.last_source != NULL && strstr(tf.last_source, "gl_Position") == NULL,
          "and writes no gl_Position, because the prepass rasterises nothing");

    CHECK(tf.tf_varyings == 1, "the output varying was recorded once (%ld)", tf.tf_varyings);
    CHECK(tf.tf_varying_count == 1, "one varying (%d)", (int) tf.tf_varying_count);
    CHECK(tf.tf_varying0 != NULL && strcmp(tf.tf_varying0, "v_clip") == 0,
          "and it is v_clip ('%s')", tf.tf_varying0 == NULL ? "(null)" : tf.tf_varying0);
    CHECK(tf.tf_varying_mode == GL_INTERLEAVED_ATTRIBS,
          "recorded as interleaved attribs (0x%X)", (unsigned) tf.tf_varying_mode);
    /* THE ORDER IS THE PROPERTY. glTransformFeedbackVaryings takes effect at
     * link time, so a program named after its link captures nothing and fails
     * later at glBeginTransformFeedback instead of here. */
    CHECK(tf.link_saw_varyings == 1,
          "the varying was recorded BEFORE the link, so the link captured it");
    CHECK(tf.varyings_after_link == 0,
          "and nothing was recorded after the link (%ld)", tf.varyings_after_link);

    CHECK(tf.get_uniform_location == BGTFGL_UNIFORMS,
          "every one of the eight prepass uniforms was located (%ld)",
          tf.get_uniform_location);
    CHECK(tf.current == 0, "the readiness check leaves no program current");
    CHECK(tf.gen_buffers == 0, "and allocates no pool: a slot is sized by its first draw");
    CHECK(tf.draw_arrays == 0 && tf.begin_tf == 0,
          "and submits nothing");

    /* The answer is cached: a second walk neither resolves nor compiles. */
    {
        const long p = tf.proc_calls, c = tf.calls;
        for (i = 0; i < 64; i++) CHECK(sl_bgtf_ready() == 1, "walk %d is still ready", i);
        CHECK(tf.proc_calls == p && tf.calls == c,
              "64 further walks cost no resolve and no GL call (resolves %ld -> %ld, "
              "calls %ld -> %ld)", p, tf.proc_calls, c, tf.calls);
    }
    end_case();
}

/* ---- 2. the walk's upload ----------------------------------------------- */

static void st_walk(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    int n;
    long binds;

    begin_case("walk: the transform goes up once per walk, nothing is left current, "
               "and the depth state starts SINGLE at zero");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    n = tf.nu;
    binds = tf.use_program;
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    CHECK(tf.nu - n == 8, "eight uniforms were written (%d)", tf.nu - n);
    CHECK(u_is_1f(n + 0, "u_sc", w.sc), "u_sc took the walk's scale");
    CHECK(u_is_1f(n + 1, "u_post", w.post), "u_post took the walk's visibility factor");
    CHECK(u_is_3f(n + 2, "u_org", w.org[0], w.org[1], w.org[2]),
          "u_org took the walk's render origin");
    CHECK(u_is_1fv(n + 3, "u_C", w.C, 16), "u_C took all sixteen clip-matrix elements");
    CHECK(u_is_1i(n + 4, "u_fogless", 1),
          "u_fogless took the stage rule, normalised to 1 from %d", w.fogless);
    /* The header's contract: the depth uniforms are left at a DEFINED starting
     * value rather than a previous walk's, and every prepass sets its own. */
    CHECK(u_is_depth(n + 5, SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f),
          "the depth state was left at SINGLE with zero coefficients");
    CHECK(tf.u[n].prog == tf.last_program && tf.u[n + 7].prog == tf.last_program,
          "every write landed in the linked program");
    CHECK(tf.uniform_rejected == 0,
          "GL accepted all eight, so none was written with the fixed pipeline "
          "bound (%ld rejected)", tf.uniform_rejected);
    CHECK(tf.current == 0, "no program is left current");
    CHECK(tf.use_program - binds == 2,
          "the upload bound the program and unbound it, once each (%ld)",
          tf.use_program - binds);
    CHECK(tf.gen_buffers == 0, "and the walk itself allocates nothing");

    /* A fogless walk uploads 0, so the normalisation is a mapping and not a
     * constant. */
    w.fogless = 0;
    n = tf.nu;
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 1, "a fogless walk is admitted");
    CHECK(u_is_1i(n + 4, "u_fogless", 0), "u_fogless took 0 for a fogless walk");

    /* The admission's own refusals: nothing an eligible walk needs may be
     * absent, and each costs no GL call. */
    {
        const long c = tf.calls;
        const struct sl_bgtf_counters b = snap();
        CHECK(sl_bgtf_begin_walk(NULL, NV, vbo, STR, 3u) == 0, "a null transform is refused");
        CHECK(sl_bgtf_begin_walk(&w, 0u, vbo, STR, 3u) == 0, "an empty pool is refused");
        CHECK(sl_bgtf_begin_walk(&w, NV, 0u, STR, 3u) == 0, "no input buffer is refused");
        CHECK(sl_bgtf_begin_walk(&w, NV, vbo, 0u, 3u) == 0, "no input stride is refused");
        CHECK(tf.calls == c, "and none of the four made a GL call (%ld)", tf.calls - c);
        CHECK(sl_bgtf_stats()->refusals - b.refusals == 4,
              "four refusals were counted (%lu)", sl_bgtf_stats()->refusals - b.refusals);
    }
    end_case();
}

/* ---- 3. canonical output indexing, and the whole submission ------------- */

static void st_indexing(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo, pool;
    long mark;
    int s;
    struct sl_bgtf_counters b;

    begin_case("indexing: the prepass draws GL_POINTS over [0, nverts) from the input "
               "buffer at the item's stride, into a pool of four floats per vertex");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    mark = tf.nlog;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0 && s < SLOTS, "a slot was filled (%d)", s);

    /* The pool the prepass recorded into, read out of the submission rather
     * than guessed: it is the name bound at transform-feedback index 0. */
    {
        const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
        const struct sl_bgtfgl_call *draw = first_of(mark, TFGL_DRAWARRAYS);
        CHECK(base != NULL, "a recording buffer was bound");
        pool = base != NULL ? (GLuint) base->c : 0u;
        CHECK(pool != 0u && pool != vbo,
              "the pool is a buffer of the module's own, not the input (%u vs %u)",
              (unsigned) pool, (unsigned) vbo);
        CHECK(draw != NULL && draw->d == (long) pool,
              "and it is still bound at the draw");
    }

    /* THE WHOLE SUBMISSION, IN ORDER. Every call the prepass makes and nothing
     * else: the pool is created and sized, the transform's depth case goes up
     * with the program current, rasterizer discard is enabled for the whole of
     * it, the input is described as three floats at the item's stride out of
     * the pass's own buffer, the element binding is dropped for the duration,
     * the pool is bound at index 0, ONE GL_POINTS draw covers [0, nverts), and
     * then everything is put back. */
    {
        const struct expect e[] = {
            { TFGL_GENBUFFERS,     1, ANY, ANY, ANY, "the pool's name" },
            { TFGL_BINDBUFFER,     GL_ARRAY_BUFFER, (long) pool, ANY, ANY, "bind to size it" },
            { TFGL_BUFFERDATA,     GL_ARRAY_BUFFER, (long) NV * 16L, GL_STATIC_DRAW, 0,
              "four floats per vertex, no initial data" },
            /* ALLOCATION ACCEPTANCE, BEFORE THE SLOT IS TRUSTED. glBufferData
             * is void, so this query is the only thing that distinguishes a
             * store the driver honoured from one it refused - and it is asked
             * here, once per allocation, rather than per draw. */
            { TFGL_GETBUFPARAM,    GL_ARRAY_BUFFER, GL_BUFFER_SIZE, (long) NV * 16L, 0,
              "and the driver is asked whether it honoured the size" },
            { TFGL_BINDBUFFER,     GL_ARRAY_BUFFER, 0, ANY, ANY, "and unbind it again" },
            { TFGL_USEPROGRAM,     ANY, ANY, ANY, ANY, "the prepass program" },
            { TFGL_UNIFORM,        sl_bgtfgl_loc("u_dmode"), ANY, ANY, ANY, "the depth case" },
            { TFGL_UNIFORM,        sl_bgtfgl_loc("u_fk1"), ANY, ANY, ANY, "the first coefficient" },
            { TFGL_UNIFORM,        sl_bgtfgl_loc("u_fk2"), ANY, ANY, ANY, "the second coefficient" },
            { TFGL_ENABLE,         GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
              "no fragment may be produced" },
            { TFGL_BINDBUFFER,     GL_ARRAY_BUFFER, (long) vbo, ANY, ANY,
              "the pass's own static vertex buffer" },
            { TFGL_VERTEXPOINTER,  3, GL_FLOAT, (long) STR, (long) vbo,
              "three floats at the item's stride, out of that buffer" },
            { TFGL_BINDBUFFER,     GL_ARRAY_BUFFER, 0, ANY, ANY, "unbound straight away" },
            { TFGL_BINDBUFFER,     GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
              "no stale element buffer across the draw" },
            { TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, (long) pool, ANY,
              "the pool records at index 0" },
            { TFGL_BEGINTF,        GL_POINTS, ANY, ANY, ANY, "one output per input vertex" },
            { TFGL_DRAWARRAYS,     GL_POINTS, 0, (long) NV, (long) pool,
              "CANONICAL ORDER: [0, nverts), so output i is input i" },
            { TFGL_ENDTF,          ANY, ANY, ANY, ANY, "the scope closes" },
            { TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0, ANY,
              "the indexed binding is released" },
            { TFGL_DISABLE,        GL_RASTERIZER_DISCARD, ANY, ANY, ANY, "discard goes off" },
            { TFGL_USEPROGRAM,     0, ANY, ANY, ANY, "and the fixed pipeline comes back" }
        };
        seq_is(mark, e, (int) (sizeof e / sizeof e[0]));
    }

    /* THE INDEXED PREPASS THE MODULE REFUSES TO BE. An indexed draw would emit
     * one output per INDEX, which is a duplicated stream and not an indexable
     * pool; the one draw above is GL_POINTS over the vertex range, and the
     * element binding is 0 across it. */
    CHECK(tf.draw_arrays == 1, "exactly one draw (%ld)", tf.draw_arrays);
    CHECK(tf.draw_vertices == (long) NV,
          "covering exactly nverts vertices (%ld)", tf.draw_vertices);
    CHECK(tf.elem_buf == 0u, "with no element buffer bound (%u)", (unsigned) tf.elem_buf);
    CHECK(tf.vptr.off == 0,
          "the input is read from the start of the pass's buffer (%ld)", tf.vptr.off);
    CHECK(sl_bgtfgl_buffer_bytes(pool) == (long) NV * 16L,
          "the pool is sized nverts * 4 floats (%ld, expected %ld)",
          sl_bgtfgl_buffer_bytes(pool), (long) NV * 16L);
    CHECK(tf.discard == 0, "discard is off once the prepass is done");
    CHECK(tf.current == 0, "and no program is current");
    CHECK(tf.tf_buf == 0u, "and nothing is left recording");

    {
        const struct sl_bgtf_counters a = snap();
        CHECK(a.prepasses - b.prepasses == 1, "one prepass was counted (%lu)",
              a.prepasses - b.prepasses);
        CHECK(a.vertices - b.vertices == (unsigned long) NV,
              "and nverts vertices with it (%lu)", a.vertices - b.vertices);
        CHECK(a.walks - b.walks == 1, "and one walk (%lu)", a.walks - b.walks);
        CHECK(a.reuses == b.reuses && a.refusals == b.refusals,
              "with no reuse and no refusal");
    }
    end_case();
}

/* ---- 4. the vertex-count bound ----------------------------------------- */

static void st_bounds(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    struct sl_bgtf_counters b;
    long c;
    int s;

    begin_case("bounds: an absurd vertex count is refused before anything is allocated, "
               "and the largest accepted count still has a byte size that does not wrap");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);

    c = tf.calls;
    b = snap();
    CHECK(sl_bgtf_begin_walk(&w, VCAP + 1u, vbo, STR, 1u) == 0,
          "a count one past the ceiling (%u) is refused", VCAP + 1u);
    CHECK(tf.calls == c, "with no GL call at all (%ld)", tf.calls - c);
    CHECK(tf.gen_buffers == 0 && tf.buffer_data == 0,
          "and NOTHING ALLOCATED: no name taken (%ld), no buffer sized (%ld)",
          tf.gen_buffers, tf.buffer_data);
    CHECK(sl_bgtf_stats()->refusals - b.refusals == 1, "one refusal was counted");
    CHECK(sl_bgtf_stats()->prepasses == b.prepasses
          && sl_bgtf_stats()->vertices == b.vertices,
          "and the refusal reads as no coverage");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
          "a draw on the refused walk is refused too");

    /* The largest count the guard accepts, carried through to a real
     * submission: the byte size is positive and within a signed 32-bit
     * ptrdiff_t, which is what the guard is for. */
    CHECK(sl_bgtf_begin_walk(&w, VCAP, vbo, STR, 2u) == 1,
          "the ceiling itself (%u) is accepted", VCAP);
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0, "and it submits (%d)", s);
    {
        const struct sl_bgtfgl_call *bd = first_of(0, TFGL_BUFFERDATA);
        CHECK(bd != NULL, "a pool was sized");
        if (bd != NULL) {
            CHECK(bd->b == (long) VCAP * 16L,
                  "the byte size is the count times four floats (%ld, expected %ld)",
                  bd->b, (long) VCAP * 16L);
            CHECK(bd->b > 0, "and it is POSITIVE, so the product did not wrap (%ld)", bd->b);
            CHECK(bd->b <= 0x7FFFFFFFL, "and within a signed 32-bit size (%ld)", bd->b);
        }
    }
    end_case();
}

/* ---- 5. the reuse key: the walk generation ----------------------------- */

static void st_reuse(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    int s1, s2, s3;
    struct sl_bgtf_counters b;
    long c;

    begin_case("reuse: an identical key inside one walk reuses with no GPU work, "
               "and a NEW WALK never reuses even when the transform compares equal");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);

    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "walk 1 is admitted");
    b = snap();
    s1 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f);
    CHECK(s1 >= 0, "the first draw fills a slot (%d)", s1);
    CHECK(sl_bgtf_stats()->prepasses - b.prepasses == 1, "one prepass ran");
    CHECK(sl_bgtf_stats()->reuses == b.reuses, "and nothing was reused");

    /* THE SAME KEY AGAIN: served from the slot, and the point is that it costs
     * NO GL CALL - a counter alone could not tell a reuse from a cheap
     * re-submission. */
    b = snap();
    c = tf.calls;
    s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f);
    CHECK(s2 == s1, "the same key is served by the same slot (%d vs %d)", s2, s1);
    CHECK(sl_bgtf_stats()->reuses - b.reuses == 1, "one reuse was counted (%lu)",
          sl_bgtf_stats()->reuses - b.reuses);
    CHECK(sl_bgtf_stats()->prepasses == b.prepasses,
          "and no prepass ran (%lu)", sl_bgtf_stats()->prepasses - b.prepasses);
    CHECK(sl_bgtf_stats()->vertices == b.vertices, "and no vertex was transformed");
    CHECK(tf.calls == c, "and NO GL CALL was made (%ld)", tf.calls - c);

    /* A NEW WALK. Same transform, same vertex count, same depth, same
     * coefficients - everything compares equal except the generation, and the
     * generation is in the key, so the previous walk's pool cannot be read. */
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 1, "walk 2 is admitted");
    b = snap();
    s3 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f);
    CHECK(s3 >= 0, "its first draw is served (%d)", s3);
    CHECK(s3 != s1, "FROM A DIFFERENT SLOT than the previous walk's (%d vs %d)", s3, s1);
    CHECK(sl_bgtf_stats()->reuses == b.reuses,
          "nothing was reused across the walk boundary (%lu)",
          sl_bgtf_stats()->reuses - b.reuses);
    CHECK(sl_bgtf_stats()->prepasses - b.prepasses == 1,
          "a prepass ran for the new walk (%lu)",
          sl_bgtf_stats()->prepasses - b.prepasses);
    CHECK(sl_bgtf_stats()->vertices - b.vertices == (unsigned long) NV,
          "transforming the pool again (%lu)", sl_bgtf_stats()->vertices - b.vertices);
    end_case();
}

/* ---- 6. the three depth cases, and FAR0's two pairs -------------------- */

static void st_depth(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    int s_k, s_t, s_f1, s_s;
    struct sl_bgtf_counters b;
    long c;
    int n;

    begin_case("depth: the two FAR0 pairs get DIFFERENT pools, which is why there is no "
               "single shared first-range buffer");
    CHECK(K1 != T1 && K2 != T2,
          "the opaque and blend pairs out of sl_wa_far_range differ "
          "(%.6f/%.3f vs %.6f/%.3f) - a check that confused them would be blind",
          (double) K1, (double) K2, (double) T1, (double) T2);
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    b = snap();
    n = tf.nu;
    s_k = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s_k >= 0, "an opaque mesh's first range is served (%d)", s_k);
    CHECK(u_is_depth(n, SL_BGGPU_DEPTH_FAR0, K1, K2),
          "with FAR0 and the opaque pair uploaded");

    n = tf.nu;
    s_t = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, T1, T2);
    CHECK(s_t >= 0, "a blended mesh's first range is served (%d)", s_t);
    CHECK(s_t != s_k,
          "FROM A DIFFERENT SLOT: one shared FAR0 buffer would hand the blended "
          "mesh the opaque pair's pool (%d vs %d)", s_t, s_k);
    CHECK(u_is_depth(n, SL_BGGPU_DEPTH_FAR0, T1, T2),
          "with FAR0 and the blend overlap's pair uploaded");

    s_f1 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, K1, K2);
    CHECK(s_f1 >= 0 && s_f1 != s_k && s_f1 != s_t,
          "the game's range gets a pool of its own (%d)", s_f1);
    s_s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, K1, K2);
    CHECK(s_s >= 0 && s_s != s_k && s_s != s_t && s_s != s_f1,
          "and so does the one-range frame's clamp (%d)", s_s);

    CHECK(sl_bgtf_stats()->prepasses - b.prepasses == 4,
          "four distinct keys, four prepasses (%lu)",
          sl_bgtf_stats()->prepasses - b.prepasses);
    CHECK(sl_bgtf_stats()->reuses == b.reuses, "and no reuse");
    CHECK(sl_bgtf_stats()->slot_evictions == b.slot_evictions,
          "and no eviction: four distinct keys fit the pool exactly (%lu)",
          sl_bgtf_stats()->slot_evictions - b.slot_evictions);
    CHECK(tf.gen_buffers == 4, "four pools were allocated (%ld)", tf.gen_buffers);
    end_case();

    /* ---- the modes that read no coefficient ----------------------------- */

    begin_case("depth: FAR1 and SINGLE read neither coefficient, so a different pair "
               "must NOT split the pool");
    /* Keying them on whatever pair the caller happened to pass would run a
     * prepass per distinct pair for a value neither mode uses. The slots are
     * full at this point, so a split would also EVICT - which is how a wrong
     * key would show up as more than a wasted prepass. */
    b = snap();
    c = tf.calls;
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, T1, T2) == s_f1,
          "FAR1 with the blend pair is the same pool");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, -1.0f, 12345.0f) == s_f1,
          "FAR1 with an arbitrary pair is the same pool");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, T1, T2) == s_s,
          "SINGLE with the blend pair is the same pool");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, -7.5f, 9999.0f) == s_s,
          "SINGLE with an arbitrary pair is the same pool");
    CHECK(sl_bgtf_stats()->reuses - b.reuses == 4, "all four were reuses (%lu)",
          sl_bgtf_stats()->reuses - b.reuses);
    CHECK(sl_bgtf_stats()->prepasses == b.prepasses, "no prepass ran");
    CHECK(sl_bgtf_stats()->slot_evictions == b.slot_evictions, "and nothing was evicted");
    CHECK(tf.calls == c, "and no GL call was made at all (%ld)", tf.calls - c);
    /* The opaque FAR0 pair, by contrast, is still its own pool and still
     * distinguishable - so the four reuses above are not a cache that lost the
     * key altogether. */
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == s_k,
          "while FAR0 with the opaque pair is still its own pool");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, T1, T2) == s_t,
          "and FAR0 with the blend pair still the other one");
    end_case();
}

/* ---- 7. program-0 raster routing --------------------------------------- */

static void st_raster(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo, pool;
    long mark;
    int s, dm;
    struct sl_bgtf_counters b;

    begin_case("raster: the binding leaves PROGRAM 0 current, points the position array "
               "at the pool as four floats, and leaves the array buffer unbound");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s >= 0, "a pool is filled (%d)", s);
    {
        const struct sl_bgtfgl_call *base = first_of(0, TFGL_BINDBUFFERBASE);
        pool = base != NULL ? (GLuint) base->c : 0u;
    }
    CHECK(pool != 0u, "and its name is known (%u)", (unsigned) pool);

    mark = tf.nlog;
    b = snap();
    /* THE BINDING ANSWERS WITH THE POOL'S DEPTH, which is the one fact the
     * caller cannot otherwise know: the depth the WALK last computed is not the
     * depth of the source that is now current once a mesh falls back. Taken
     * ONCE into a local - a second call would bind again and put two more
     * records in the transcript the sequence below reads. */
    dm = sl_bgtf_bind_clip(s);
    CHECK(dm == SL_BGGPU_DEPTH_FAR0,
          "the binding returns the POOL'S depth mode, not a success flag (%d)", dm);

    /* FOUR CALLS, AND THE WHOLE CANDIDATE IS IN THE FIRST ONE. The transform
     * happened in the prepass; the draw that follows is the ORIGINAL
     * fixed-function path with the original arrays, and only the POSITION
     * array moves to the prepass output. */
    {
        const struct expect e[] = {
            { TFGL_USEPROGRAM,    0, ANY, ANY, ANY,
              "PROGRAM 0 - the fixed pipeline - is what rasterises" },
            { TFGL_BINDBUFFER,    GL_ARRAY_BUFFER, (long) pool, ANY, ANY,
              "the pool, bound for the pointer command" },
            { TFGL_VERTEXPOINTER, 4, GL_FLOAT, 0, (long) pool,
              "FOUR floats - a clip position, not a model position" },
            { TFGL_BINDBUFFER,    GL_ARRAY_BUFFER, 0, ANY, ANY,
              "and unbound immediately after it" }
        };
        seq_is(mark, e, (int) (sizeof e / sizeof e[0]));
    }
    CHECK(tf.current == 0, "no program is current, so the fixed pipeline rasterises");
    /* A BUFFER LEFT BOUND HERE WOULD CORRUPT EVERY LATER CLIENT POINTER: the
     * pass's colour, UV and fog arrays are host addresses, and GL_ARRAY_BUFFER
     * non-zero would make each be read as an offset into the pool. */
    CHECK(tf.arr_buf == 0u,
          "GL_ARRAY_BUFFER is back to 0, so later client pointers are host "
          "addresses and not offsets (%u)", (unsigned) tf.arr_buf);
    CHECK(tf.vptr.size == 4 && tf.vptr.type == GL_FLOAT && tf.vptr.stride == 0,
          "the position array is four tightly packed floats (size %d, stride %d)",
          tf.vptr.size, (int) tf.vptr.stride);
    CHECK(tf.vptr.off == 0, "from the start of the pool (%ld)", tf.vptr.off);
    CHECK(tf.vptr.buf == pool,
          "and out of the POOL, not the input buffer (%u vs %u)",
          (unsigned) tf.vptr.buf, (unsigned) vbo);
    CHECK(tf.discard == 0, "rasterizer discard is off, so the draw will produce fragments");

    /* A BIND IS NOT A DRAW, and the census must say so. The binding counts a
     * BIND and nothing else: no raster draw, no index, no depth call. Counting
     * coverage here was the defect this replaces - it credited a mesh whose
     * triangles were all filtered and missed a bind that served several runs. */
    {
        const struct sl_bgtf_counters a = snap();
        CHECK(a.binds - b.binds == 1, "one bind was counted (%lu)", a.binds - b.binds);
        CHECK(a.raster_draws == b.raster_draws,
              "and NOT a raster draw: a bind happens before the mesh's runs are "
              "known (%lu)", a.raster_draws - b.raster_draws);
        CHECK(a.raster_indices == b.raster_indices, "nor an index (%lu)",
              a.raster_indices - b.raster_indices);
        CHECK(a.dcalls[SL_BGGPU_DEPTH_FAR0] == b.dcalls[SL_BGGPU_DEPTH_FAR0]
              && a.dcalls[SL_BGGPU_DEPTH_SINGLE] == b.dcalls[SL_BGGPU_DEPTH_SINGLE]
              && a.dcalls[SL_BGGPU_DEPTH_FAR1] == b.dcalls[SL_BGGPU_DEPTH_FAR1],
              "nor a depth call under any mode");
    }

    /* ONE BINDING SERVES SEVERAL RUNS, and each nonempty run is one submission
     * counted at the real draw seam with the SOURCE's depth. The depth passed
     * here is the one the binding answered with, not the one the walk last
     * computed - which is the distinction a fallback makes visible. */
    b = snap();
    sl_bgtf_count_submission(dm, 7923u);
    sl_bgtf_count_submission(dm, 12u);
    CHECK(sl_bgtf_stats()->raster_draws - b.raster_draws == 2,
          "TWO runs off ONE bind are two submissions (%lu)",
          sl_bgtf_stats()->raster_draws - b.raster_draws);
    CHECK(sl_bgtf_stats()->raster_indices - b.raster_indices == 7935,
          "and their indices are summed (%lu)",
          sl_bgtf_stats()->raster_indices - b.raster_indices);
    CHECK(sl_bgtf_stats()->dcalls[SL_BGGPU_DEPTH_FAR0]
          - b.dcalls[SL_BGGPU_DEPTH_FAR0] == 2,
          "both under the SOURCE'S depth (%lu)",
          sl_bgtf_stats()->dcalls[SL_BGGPU_DEPTH_FAR0] - b.dcalls[SL_BGGPU_DEPTH_FAR0]);
    CHECK(sl_bgtf_stats()->binds == b.binds,
          "and submitting is not itself a bind (%lu)", sl_bgtf_stats()->binds - b.binds);

    /* AN EMPTY RUN IS NOT A SUBMISSION. A mesh whose triangles are all filtered
     * submits none at all, and the module drops the call rather than leaving
     * that to its caller. */
    b = snap();
    sl_bgtf_count_submission(dm, 0u);
    sl_bgtf_count_submission(SL_BGGPU_DEPTH_SINGLE, 0u);
    CHECK(sl_bgtf_stats()->raster_draws == b.raster_draws
          && sl_bgtf_stats()->raster_indices == b.raster_indices,
          "two empty runs are not two draws (%lu draws, %lu indices)",
          sl_bgtf_stats()->raster_draws - b.raster_draws,
          sl_bgtf_stats()->raster_indices - b.raster_indices);
    CHECK(sl_bgtf_stats()->dcalls[SL_BGGPU_DEPTH_SINGLE]
          == b.dcalls[SL_BGGPU_DEPTH_SINGLE],
          "and raise no depth call either (%lu)",
          sl_bgtf_stats()->dcalls[SL_BGGPU_DEPTH_SINGLE]
          - b.dcalls[SL_BGGPU_DEPTH_SINGLE]);

    /* A RUN DRAWING FROM THE CPU ARRAY CONTRIBUTES NOTHING, whatever the walk
     * was admitted as - which is what the dmode parameter is for. An out-of-
     * range depth records the submission but credits no range, so a wrong depth
     * cannot be laundered into a range it did not draw. */
    b = snap();
    sl_bgtf_count_submission(-1, 50u);
    sl_bgtf_count_submission(3, 50u);
    CHECK(sl_bgtf_stats()->dcalls[0] == b.dcalls[0]
          && sl_bgtf_stats()->dcalls[1] == b.dcalls[1]
          && sl_bgtf_stats()->dcalls[2] == b.dcalls[2],
          "a depth outside the three ranges credits none of them");
    end_case();
}

/* ---- 8. state restoration ---------------------------------------------- */

static void st_restore(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo, pool;
    long mark;
    int s;

    begin_case("restore: discard is on for the prepass and off after, and the unbind puts "
               "back the program, both buffer bindings and the indexed TF binding");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0, "a pool is filled (%d)", s);
    {
        const struct sl_bgtfgl_call *base = first_of(0, TFGL_BINDBUFFERBASE);
        pool = base != NULL ? (GLuint) base->c : 0u;
    }
    /* Discard bracketed the prepass exactly once each way, and the draw ran
     * inside it - v_draw_without_discard, asserted at the end of every case,
     * is what says the draw itself was covered. */
    CHECK(tf.enables == 1 && tf.disables == 1,
          "discard was enabled once and disabled once (%ld / %ld)",
          tf.enables, tf.disables);
    CHECK(tf.discard == 0, "and is off afterwards");
    CHECK(tf.tf_active == 0, "with no transform-feedback scope left open");
    CHECK(tf.begin_tf == 1 && tf.end_tf == 1,
          "which opened and closed once each (%ld / %ld)", tf.begin_tf, tf.end_tf);

    /* THE RESTORE NEEDS SOMETHING TO BE FALSE ABOUT. Against GL's defaults the
     * unbind would read as a no-op, so the state is dirtied first with exactly
     * the five things the module claims to put back. */
    sl_bgtfgl_test_state(tf.last_program, vbo, vbo, pool, 1);
    CHECK(tf.current != 0 && tf.arr_buf != 0 && tf.elem_buf != 0 && tf.tf_buf != 0
          && tf.discard != 0, "the state is dirty before the unbind");
    mark = tf.nlog;
    sl_bgtf_unbind();
    {
        const struct expect e[] = {
            { TFGL_USEPROGRAM,     0, ANY, ANY, ANY, "no program" },
            { TFGL_BINDBUFFER,     GL_ARRAY_BUFFER, 0, ANY, ANY, "no array binding" },
            { TFGL_BINDBUFFER,     GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
              "no element binding" },
            { TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0, ANY,
              "no indexed TF binding" },
            { TFGL_DISABLE,        GL_RASTERIZER_DISCARD, ANY, ANY, ANY, "discard off" }
        };
        seq_is(mark, e, (int) (sizeof e / sizeof e[0]));
    }
    CHECK(tf.current == 0, "the program is back to the fixed pipeline");
    CHECK(tf.arr_buf == 0u, "the array binding is back to 0");
    CHECK(tf.elem_buf == 0u, "the element binding is back to 0");
    CHECK(tf.tf_buf == 0u, "the indexed TF binding is back to 0");
    CHECK(tf.discard == 0, "and rasterizer discard is off");
    end_case();
}

/* ---- 9. capability failures: each one latched, none retried ------------- */

/* THE REASON STRING IS PART OF THE ANSWER, because a refusal of the transform-
 * feedback capability and a refusal of the buffer-object capability read the
 * same from outside and are different facts. The three groups are the module's
 * own three checks, in its own order. */
static const char *expected_reason(int i)
{
    if (i < 16) return "GL 2.0 program entry points are not all present";
    if (i < 20) return "transform feedback entry points are not all present";
    return "buffer object entry points are not all present";
}

static void st_proc_failures(void)
{
    struct sl_bggpu_walk w = a_walk();
    int i;

    begin_case("failure: each of the twenty-four entry points is REQUIRED - withheld in "
               "turn it latches UNAVAILABLE, is never retried, and only invalidate clears it");
    for (i = 0; i < BGTFGL_PROCS; i++) {
        GLuint vbo;
        long p, c;
        arm();
        vbo = input_vbo(NV, STR);
        tf.withhold_proc = sl_bgtfgl_proc_names[i];
        CHECK(sl_bgtf_ready() == 0, "withholding %s refuses", sl_bgtfgl_proc_names[i]);
        CHECK(sl_bgtf_state() == -1, "withholding %s latches UNAVAILABLE (state=%d)",
              sl_bgtfgl_proc_names[i], sl_bgtf_state());
        CHECK(strstr(sl_bgtf_why(), expected_reason(i)) != NULL,
              "withholding %s names its own capability group: '%s'",
              sl_bgtfgl_proc_names[i], sl_bgtf_why());
        CHECK(tf.create_shader == 0 && tf.compile == 0 && tf.shader_source == 0,
              "withholding %s compiles nothing: the resolve is checked first",
              sl_bgtfgl_proc_names[i]);
        CHECK(tf.gen_buffers == 0, "and allocates nothing (%s)", sl_bgtfgl_proc_names[i]);

        p = tf.proc_calls; c = tf.calls;
        CHECK(sl_bgtf_ready() == 0, "a second walk still refuses (%s)",
              sl_bgtfgl_proc_names[i]);
        CHECK(tf.proc_calls == p && tf.calls == c,
              "and did not retry (%s: resolves %ld -> %ld, calls %ld -> %ld)",
              sl_bgtfgl_proc_names[i], p, tf.proc_calls, c, tf.calls);
        CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 0,
              "no walk is admitted after a real failure (%s)", sl_bgtfgl_proc_names[i]);
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "and no draw is served (%s)", sl_bgtfgl_proc_names[i]);
        sl_bgtf_bind_clip(0);
        sl_bgtf_unbind();
        CHECK(tf.use_program == 0 && tf.calls == c,
              "an unavailable module binds and unbinds nothing (%s: %ld calls)",
              sl_bgtfgl_proc_names[i], tf.calls - c);

        /* ONLY INVALIDATE CLEARS IT. */
        tf.withhold_proc = NULL;
        sl_bgtf_invalidate();
        CHECK(sl_bgtf_state() == 0, "invalidate returns the state to UNTRIED (%s)",
              sl_bgtfgl_proc_names[i]);
        CHECK(sl_bgtf_why()[0] == '\0', "and clears the reason (%s: '%s')",
              sl_bgtfgl_proc_names[i], sl_bgtf_why());
        CHECK(sl_bgtf_ready() == 1, "and the next walk resolves again (%s)",
              sl_bgtfgl_proc_names[i]);
    }
    end_case();
}

static void st_object_failures(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    long p, c;

    begin_case("failure: glCreateShader hands back no object");
    arm();
    tf.create_shader_zero = 1;
    CHECK(sl_bgtf_ready() == 0, "a shader name of 0 refuses");
    CHECK(sl_bgtf_state() == -1, "and latches UNAVAILABLE (state=%d)", sl_bgtf_state());
    CHECK(strstr(sl_bgtf_why(), "glCreateShader gave no object") != NULL,
          "the reason names the object: '%s'", sl_bgtf_why());
    CHECK(tf.shader_source == 0 && tf.compile == 0,
          "nothing was sourced or compiled onto a name that does not exist");
    CHECK(tf.create_program == 0, "and no program was created");
    p = tf.create_shader; c = tf.calls;
    CHECK(sl_bgtf_ready() == 0 && tf.create_shader == p && tf.calls == c,
          "a second walk refuses without asking again (%ld -> %ld)", p, tf.create_shader);
    end_case();

    begin_case("failure: glCreateProgram hands back no object");
    arm();
    tf.create_program_zero = 1;
    CHECK(sl_bgtf_ready() == 0, "a program name of 0 refuses");
    CHECK(sl_bgtf_state() == -1, "and latches UNAVAILABLE");
    CHECK(strstr(sl_bgtf_why(), "glCreateProgram gave no object") != NULL,
          "the reason names the object: '%s'", sl_bgtf_why());
    CHECK(tf.compile == 1 && tf.get_shaderiv == 1,
          "the shader got as far as a compile status before the program failed");
    CHECK(tf.attach == 0 && tf.tf_varyings == 0 && tf.link == 0,
          "and nothing was attached, recorded or linked onto it");
    p = tf.create_program; c = tf.calls;
    CHECK(sl_bgtf_ready() == 0 && tf.create_program == p && tf.calls == c,
          "a second walk refuses without asking again (%ld -> %ld)", p, tf.create_program);
    end_case();

    begin_case("failure: a prepass program that will not compile");
    arm();
    tf.fail_compile = 1;
    CHECK(sl_bgtf_ready() == 0, "a failed compile refuses");
    CHECK(sl_bgtf_state() == -1, "and latches UNAVAILABLE");
    CHECK(strstr(sl_bgtf_why(), "did not compile") != NULL,
          "the reason names the compile: '%s'", sl_bgtf_why());
    /* The driver's own words: a refusal of `#version 400 compatibility` and a
     * refusal of `precise` read the same from outside and are different facts. */
    CHECK(tf.get_shaderlog == 1, "the driver's own words were asked for, once (%ld)",
          tf.get_shaderlog);
    CHECK(tf.create_program == 0, "no program was created after the compile failed");
    p = tf.compile; c = tf.calls;
    CHECK(sl_bgtf_ready() == 0 && tf.compile == p && tf.calls == c,
          "a second walk recompiles nothing (compiles %ld -> %ld)", p, tf.compile);
    end_case();

    begin_case("failure: a prepass program that will not link");
    arm();
    vbo = input_vbo(NV, STR);
    tf.fail_link = 1;
    CHECK(sl_bgtf_ready() == 0, "a failed link refuses");
    CHECK(sl_bgtf_state() == -1, "and latches UNAVAILABLE");
    CHECK(strstr(sl_bgtf_why(), "did not link") != NULL,
          "the reason names the link: '%s'", sl_bgtf_why());
    CHECK(tf.get_programlog == 1, "the driver's own words were asked for, once (%ld)",
          tf.get_programlog);
    CHECK(tf.get_uniform_location == 0,
          "no uniform was located after the link failed (%ld)", tf.get_uniform_location);
    p = tf.link; c = tf.calls;
    CHECK(sl_bgtf_ready() == 0 && tf.link == p && tf.calls == c,
          "a second walk relinks nothing (links %ld -> %ld)", p, tf.link);
    /* The module zeroes the program name on a failed link, so nothing can be
     * bound; the stub would record a stale or unminted name if it were. */
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 0, "no walk is admitted");
    sl_bgtf_bind_clip(0);
    CHECK(tf.use_program == 0, "and a failed link binds nothing (%ld)", tf.use_program);
    end_case();

    begin_case("failure: each of the eight prepass uniforms is required");
    {
        int i;
        for (i = 0; i < BGTFGL_UNIFORMS; i++) {
            long n;
            arm();
            tf.withhold_uniform = sl_bgtfgl_uniform_names[i];
            CHECK(sl_bgtf_ready() == 0, "withholding %s refuses",
                  sl_bgtfgl_uniform_names[i]);
            CHECK(sl_bgtf_state() == -1, "withholding %s latches UNAVAILABLE",
                  sl_bgtfgl_uniform_names[i]);
            CHECK(strstr(sl_bgtf_why(), "uniform did not resolve") != NULL,
                  "the reason names the uniform resolve: '%s'", sl_bgtf_why());
            CHECK(tf.get_uniform_location == BGTFGL_UNIFORMS,
                  "all eight were asked for before the check, so the failure is not "
                  "order-dependent (%ld)", tf.get_uniform_location);
            n = tf.get_uniform_location; c = tf.calls;
            CHECK(sl_bgtf_ready() == 0 && tf.get_uniform_location == n && tf.calls == c,
                  "withholding %s is not retried (locations %ld -> %ld)",
                  sl_bgtfgl_uniform_names[i], n, tf.get_uniform_location);
            sl_bgtf_bind_clip(0);
            CHECK(tf.use_program == 0, "withholding %s binds nothing (%ld)",
                  sl_bgtfgl_uniform_names[i], tf.use_program);
            tf.withhold_uniform = NULL;
        }
    }
    end_case();
}

/* ---- 10. a pool that cannot be allocated ------------------------------- */

/* A SLOT THAT WAS REFUSED MUST NOT BE USABLE, and the three ways a caller could
 * try to use one are checked together after every allocation failure below: the
 * prepass answered -1, so there is no slot index to bind; every in-range index
 * is unfilled, so a caller that invented one binds nothing; and no raster draw,
 * index or depth call appears. The point is that `filled` is never set on a
 * refused slot, so no later bind can find it however it is reached. */
static void refused_slot_unusable(const char *what,
                                  const struct sl_bgtf_counters *b)
{
    const long calls = tf.calls;
    int i;
    for (i = 0; i < SLOTS; i++) {
        /* Taken ONCE into a local: a second call in the message argument would
         * bind again on the very failure the message is reporting. */
        const int got = sl_bgtf_bind_clip(i);
        CHECK(got == -1,
              "%s: slot %d cannot be bound - a refused allocation leaves no pool "
              "stamped valid (%d)", what, i, got);
    }
    CHECK(tf.calls == calls,
          "%s: and none of those %d bindings made a GL call (%ld)", what, SLOTS,
          tf.calls - calls);
    CHECK(sl_bgtf_stats()->binds == b->binds,
          "%s: no bind was counted (%lu)", what, sl_bgtf_stats()->binds - b->binds);
    CHECK(sl_bgtf_stats()->raster_draws == b->raster_draws
          && sl_bgtf_stats()->raster_indices == b->raster_indices,
          "%s: and NO RASTER DRAW was counted (%lu draws, %lu indices)", what,
          sl_bgtf_stats()->raster_draws - b->raster_draws,
          sl_bgtf_stats()->raster_indices - b->raster_indices);
    CHECK(sl_bgtf_stats()->prepasses == b->prepasses
          && sl_bgtf_stats()->vertices == b->vertices,
          "%s: and reads as no coverage at all", what);
}

/* The state this module touched, back as it was found - asserted on the ERROR
 * path, which is where a refusal that returned early would leave it disturbed.
 * The restore is in one place at the end of the allocation block for exactly
 * this reason, so the error path and the success path put back the same five
 * things. */
static void state_restored_on_error(const char *what)
{
    CHECK(tf.current == 0u, "%s: no program is left current (%u)", what,
          (unsigned) tf.current);
    CHECK(tf.arr_buf == 0u,
          "%s: GL_ARRAY_BUFFER is back to 0, so later client pointers are host "
          "addresses and not offsets (%u)", what, (unsigned) tf.arr_buf);
    CHECK(tf.tf_buf == 0u, "%s: nothing is left recording (%u)", what,
          (unsigned) tf.tf_buf);
    CHECK(tf.discard == 0, "%s: rasterizer discard is off", what);
    CHECK(tf.tf_active == 0, "%s: no transform-feedback scope is left open", what);
}

static void st_alloc_failure(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    struct sl_bgtf_counters b;
    int s;

    begin_case("failure: glGenBuffers hands back a zero name - a buffer the context "
               "will not give LATCHES UNAVAILABLE");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    tf.gen_buffers_zero = 1;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s == -1, "a pool that cannot be allocated refuses the draw (%d)", s);
    CHECK(sl_bgtf_stats()->refusals - b.refusals == 1, "one refusal was counted (%lu)",
          sl_bgtf_stats()->refusals - b.refusals);
    CHECK(tf.gen_buffers == 1, "one name was asked for (%ld)", tf.gen_buffers);
    CHECK(tf.buffer_data == 0, "nothing was sized onto a name of 0 (%ld)", tf.buffer_data);
    CHECK(tf.get_buffer_parameteriv == 0,
          "and nothing was queried about it either (%ld)", tf.get_buffer_parameteriv);
    CHECK(tf.begin_tf == 0 && tf.draw_arrays == 0 && tf.bind_buffer_base == 0,
          "and nothing was submitted");
    refused_slot_unusable("zero name", &b);

    /* IT LATCHES, and that is the measured behaviour the module now states.
     * Returning -1 alone left the state READY and asked for a name again on the
     * next draw, which turned one exhausted context into one glGenBuffers per
     * draw for the rest of the frame while the picture was the CPU path's
     * either way. A latch is cleared only by sl_bgtf_invalidate, so a NEW
     * context tries again and the same one does not. */
    CHECK(sl_bgtf_state() == -1,
          "the capability LATCHES UNAVAILABLE (state=%d)", sl_bgtf_state());
    CHECK(strstr(sl_bgtf_why(), "transform-feedback buffer could not be created") != NULL,
          "and the reason names the allocation: '%s'", sl_bgtf_why());
    b = snap();
    {
        const long g = tf.gen_buffers;
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "the next draw is refused by the latch");
        CHECK(tf.gen_buffers == g,
              "WITHOUT asking for a name again: one glGenBuffers per context, not "
              "one per draw (%ld -> %ld)", g, tf.gen_buffers);
    }
    CHECK(sl_bgtf_stats()->refusals - b.refusals == 1, "counting another refusal");
    /* Names coming back do NOT un-latch it: only a new context does. */
    tf.gen_buffers_zero = 0;
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
          "a name that would now come back is not asked for: the latch holds");
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 0,
          "and no further walk is admitted");
    CHECK(sl_bgtf_state() == -1, "the state is still UNAVAILABLE (%d)", sl_bgtf_state());
    sl_bgtf_invalidate();
    CHECK(sl_bgtf_state() == 0, "only invalidate returns it to UNTRIED (%d)",
          sl_bgtf_state());
    CHECK(sl_bgtf_ready() == 1, "and the next context is ready again");
    end_case();
}

/* ---- 10b. ALLOCATION IS VERIFIED: a name is not storage ----------------- */

/* THE FAILURE THIS BLOCK IS ABOUT. glBufferData is void. A driver that will not
 * honour the size reports nothing and leaves the object at whatever it had, so
 * "the call returned" is not evidence that storage exists. Marking the capacity
 * sufficient straight after it was wrong in a way that could RASTERISE: the
 * slot would be stamped filled and drawn from, and an undersized or undefined
 * pool would reach the screen instead of the CPU fallback this module promises.
 *
 * Every case here injects one shape of that failure on a NON-ZERO buffer name -
 * which the zero-name case above cannot express - and requires the same four
 * things: the draw is refused, no slot is left usable, no coverage is counted,
 * and the touched state is put back. */
static void st_alloc_verified(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    struct sl_bgtf_counters b;
    const long want = (long) NV * 16L;

    /* ---- the initial allocation installs LESS than was asked ----------- */

    begin_case("allocation: an initial store that installs LESS than requested is "
               "REFUSED - no undersized pool is stamped valid");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    tf.alloc_short_initial = 1;
    tf.alloc_bytes = want - 16L;        /* one vertex short: the subtlest case */
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
          "a store one vertex short refuses the draw");
    CHECK(tf.buffer_data == 1, "the store was attempted once (%ld)", tf.buffer_data);
    CHECK(tf.alloc_shorted == 1, "and the fake driver did install less (%ld)",
          tf.alloc_shorted);
    CHECK(tf.get_buffer_parameteriv == 1,
          "GL_BUFFER_SIZE was read ONCE for that allocation - the only way to "
          "learn the request was accepted without reading the contents back (%ld)",
          tf.get_buffer_parameteriv);
    {
        const struct sl_bgtfgl_call *q = first_of(0, TFGL_GETBUFPARAM);
        CHECK(q != NULL, "the query is in the transcript");
        if (q != NULL) {
            CHECK(q->a == GL_ARRAY_BUFFER,
                  "asked on the target the store used (0x%lX)", q->a);
            CHECK(q->b == GL_BUFFER_SIZE, "for GL_BUFFER_SIZE (0x%lX)", q->b);
            CHECK(q->c == want - 16L,
                  "and it reported the INSTALLED size, not the requested one "
                  "(%ld, requested %ld)", q->c, want);
        }
    }
    /* NOTHING WAS SUBMITTED INTO IT. The refusal happens before the prepass, so
     * no transform-feedback scope opens and no draw runs - which is also why
     * the overrun watchdog is silent rather than firing: there was no draw to
     * overrun, and end_case asserts that zero. */
    CHECK(tf.begin_tf == 0 && tf.draw_arrays == 0 && tf.bind_buffer_base == 0,
          "and NOTHING was submitted into the short pool (%ld begins, %ld draws)",
          tf.begin_tf, tf.draw_arrays);
    refused_slot_unusable("short initial store", &b);
    state_restored_on_error("short initial store");
    CHECK(sl_bgtf_state() == -1,
          "storage that was not allocated at the requested size latches "
          "UNAVAILABLE (state=%d)", sl_bgtf_state());
    CHECK(strstr(sl_bgtf_why(), "storage was not allocated at the requested size") != NULL,
          "and the reason names the allocation: '%s'", sl_bgtf_why());
    tf.alloc_short_initial = 0;
    tf.alloc_bytes = 0;
    end_case();

    /* ---- the store installs NOTHING AT ALL ----------------------------- */

    begin_case("allocation: a store that leaves the object EMPTY is refused - a buffer "
               "NAME is not allocated storage");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    tf.alloc_install_none = 1;          /* the object keeps its zero bytes */
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
          "a name with no storage behind it refuses the draw");
    {
        const struct sl_bgtfgl_call *q = first_of(0, TFGL_GETBUFPARAM);
        CHECK(q != NULL && q->c == 0L,
              "the query reported an empty object (%ld)", q != NULL ? q->c : -1L);
    }
    refused_slot_unusable("empty store", &b);
    state_restored_on_error("empty store");
    CHECK(sl_bgtf_state() == -1, "and it latches (state=%d)", sl_bgtf_state());
    tf.alloc_install_none = 0;
    end_case();

    /* ---- the query itself cannot answer -------------------------------- */

    begin_case("allocation: a GL_BUFFER_SIZE query that writes NOTHING is refused - "
               "unverifiable storage is not acceptable storage");
    /* A DIFFERENT FACT FROM A SHORT ALLOCATION. Real GL leaves the out
     * parameter untouched on an error, so the module keeps whatever it
     * initialised. Accepting that would mean trusting a store it could not
     * check, which is the very thing this block exists to stop - so the
     * sentinel has to be a value that FAILS the comparison, not one that
     * passes it. */
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    tf.getbufparam_fail = 1;
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
          "a query that answers nothing refuses the draw");
    CHECK(tf.get_buffer_parameteriv == 1, "the query was attempted (%ld)",
          tf.get_buffer_parameteriv);
    {
        const struct sl_bgtfgl_call *q = first_of(0, TFGL_GETBUFPARAM);
        CHECK(q != NULL && q->d == 1L,
              "and the stub wrote nothing through the out pointer (%ld)",
              q != NULL ? q->d : -1L);
    }
    CHECK(tf.begin_tf == 0 && tf.draw_arrays == 0, "nothing was submitted");
    refused_slot_unusable("unanswerable query", &b);
    state_restored_on_error("unanswerable query");
    CHECK(sl_bgtf_state() == -1, "and it latches (state=%d)", sl_bgtf_state());
    tf.getbufparam_fail = 0;
    end_case();

    /* ---- a GROWTH that installs less ----------------------------------- */

    begin_case("allocation: a GROWTH that installs less than requested is refused, and "
               "the pool that was already good is not left stamped for the bigger count");
    /* THE SECOND ALLOCATION SITE, AND REACHING IT TAKES CARE. The module stores
     * only when the chosen slot's recorded capacity is short, so a growth needs
     * the victim to be a slot that ALREADY OWNS A BUFFER. A free slot is picked
     * before any occupied one and mints a fresh name, which is an initial
     * allocation however large the walk - so every slot is filled at the small
     * count first, and only then does a larger walk force a prior-walk slot to
     * grow. Getting this wrong is why an earlier draft of this case injected a
     * growth failure and watched an initial allocation succeed instead. */
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV * 2u, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "a small walk is admitted");
    {
        int i;
        for (i = 0; i < SLOTS; i++)
            CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, (float) (i + 1), (float) (i + 2)) >= 0,
                  "its key %d is served, so no slot is left free", i);
    }
    CHECK(tf.buffer_data == SLOTS, "four stores so far (%ld)", tf.buffer_data);
    CHECK(tf.get_buffer_parameteriv == SLOTS,
          "and four queries, every one of which ACCEPTED (%ld)",
          tf.get_buffer_parameteriv);
    CHECK(tf.gen_buffers == SLOTS, "four names were minted (%ld)", tf.gen_buffers);
    CHECK(sl_bgtf_state() == 1, "the capability is still READY (%d)", sl_bgtf_state());

    /* A LARGER WALK. Every slot is occupied, so the victim is a prior-walk slot
     * that already has storage sized for the SMALL count - a real growth. */
    CHECK(sl_bgtf_begin_walk(&w, NV * 2u, vbo, STR, 2u) == 1,
          "a larger walk is admitted");
    tf.alloc_short_growth = 1;
    tf.alloc_bytes = want;              /* still only the OLD size */
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
          "the growth is refused");
    CHECK(tf.buffer_data == SLOTS + 1, "a fifth store was attempted (%ld)",
          tf.buffer_data);
    CHECK(tf.gen_buffers == SLOTS,
          "onto a name that already existed, so this is a GROWTH and not a fresh "
          "allocation (%ld names minted)", tf.gen_buffers);
    CHECK(tf.alloc_shorted == 1, "and the fake driver did install less (%ld)",
          tf.alloc_shorted);
    CHECK(tf.get_buffer_parameteriv == SLOTS + 1, "with a fifth query (%ld)",
          tf.get_buffer_parameteriv);
    CHECK(tf.begin_tf == SLOTS && tf.draw_arrays == SLOTS,
          "and NOTHING FURTHER was submitted: the four small prepasses drew, the "
          "growth did not (%ld begins, %ld draws)", tf.begin_tf, tf.draw_arrays);
    refused_slot_unusable("short growth", &b);
    state_restored_on_error("short growth");
    /* THE CAPACITY IS NOT LEFT CLAIMING THE BIGGER COUNT. If it were, a later
     * walk at the larger count would find `cap_verts >= nverts`, skip the store
     * entirely and draw the bigger count out of the smaller buffer - which is
     * the write past the end this check exists to prevent. The fake driver's
     * own overrun watchdog is the independent witness, and end_case asserts it. */
    tf.alloc_short_growth = 0;
    tf.alloc_bytes = 0;
    sl_bgtf_invalidate();
    end_case();

    /* ---- and the honest path still works ------------------------------- */

    begin_case("allocation: with the store honoured the query ACCEPTS it, exactly once "
               "per allocation - so the refusals above are not a module that stopped "
               "allocating");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) >= 0, "the draw is served");
    CHECK(sl_bgtf_stats()->prepasses - b.prepasses == 1, "one prepass ran (%lu)",
          sl_bgtf_stats()->prepasses - b.prepasses);
    CHECK(tf.buffer_data == 1 && tf.get_buffer_parameteriv == 1,
          "one store and ONE query (%ld / %ld)", tf.buffer_data,
          tf.get_buffer_parameteriv);
    CHECK(tf.alloc_shorted == 0, "with nothing shorted (%ld)", tf.alloc_shorted);
    CHECK(sl_bgtf_state() == 1, "and the capability is READY (%d)", sl_bgtf_state());
    /* A SECOND DRAW ON THE SAME KEY QUERIES NOTHING. The acceptance is asked at
     * allocation time and nowhere else, which is what makes it not a per-frame
     * stall: a reuse makes no GL call at all. */
    {
        const long q = tf.get_buffer_parameteriv, c = tf.calls;
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) >= 0,
              "the same key is served again");
        CHECK(tf.get_buffer_parameteriv == q,
              "with NO further query: acceptance is per allocation, not per draw "
              "(%ld -> %ld)", q, tf.get_buffer_parameteriv);
        CHECK(tf.calls == c, "and no GL call at all (%ld)", tf.calls - c);
    }
    /* A LARGER WALK THAT IS HONOURED DOES GROW, so the growth site is reached on
     * the success path too and the refusal above was the injection and not the
     * shape of every growth. Every slot is filled first for the reason the
     * refusal case gives: a free slot would mint a new name and allocate
     * initially rather than grow. */
    {
        long bd, q;
        const long names = tf.gen_buffers;
        GLuint big = input_vbo(NV * 2u, STR);
        int i;
        for (i = 1; i < SLOTS; i++)
            CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, (float) (i + 1), (float) (i + 2)) >= 0,
                  "filling slot %d so none is free", i);
        CHECK(tf.gen_buffers == names + (SLOTS - 1),
              "three more names were minted (%ld)", tf.gen_buffers - names);
        bd = tf.buffer_data;
        q = tf.get_buffer_parameteriv;
        CHECK(sl_bgtf_begin_walk(&w, NV * 2u, big, STR, 2u) == 1,
              "a larger walk is admitted");
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) >= 0,
              "and served by a pool that grew");
        CHECK(tf.buffer_data == bd + 1, "one more store (%ld)", tf.buffer_data - bd);
        CHECK(tf.gen_buffers == names + (SLOTS - 1),
              "onto an EXISTING name, so it grew rather than being allocated afresh "
              "(%ld names minted since)", tf.gen_buffers - names);
        CHECK(tf.get_buffer_parameteriv == q + 1, "and one more query (%ld)",
              tf.get_buffer_parameteriv - q);
        CHECK(tf.alloc_shorted == 0, "with nothing shorted (%ld)", tf.alloc_shorted);
        {
            const struct sl_bgtfgl_call *bdr = NULL;
            long k;
            for (k = tf.nlog - 1; k >= 0; k--)
                if (tf.log[k].kind == TFGL_BUFFERDATA) { bdr = &tf.log[k]; break; }
            CHECK(bdr != NULL && bdr->b == want * 2L,
                  "sized for the bigger count (%ld, expected %ld)",
                  bdr != NULL ? bdr->b : -1L, want * 2L);
            CHECK(bdr != NULL && bdr->off == want * 2L,
                  "and the storage installed is that size (%ld)",
                  bdr != NULL ? bdr->off : -1L);
        }
    }
    end_case();
}

/* ---- 10c. the slot a previous walk left is a REFILL, not pressure ------- */

static void st_slot_refills(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    struct sl_bgtf_counters b;
    int s, i;

    begin_case("slots: a free slot and a PREVIOUS walk's slot are REFILLS; only "
               "displacing a key of the CURRENT walk is pressure");
    /* CONFLATING THE TWO MADE THE CENSUS CLAIM CACHE PRESSURE ON EVERY WALK. A
     * slot left behind by a previous walk can never be wanted again, because a
     * key carries its walk generation - so taking it is an ordinary refill. An
     * eviction is a pool THIS walk may still draw from being displaced, which
     * is a real cost and the only thing worth reporting as one. */
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);

    /* FOUR FREE SLOTS: four refills, no eviction. */
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "walk 1 is admitted");
    b = snap();
    for (i = 0; i < SLOTS; i++) {
        s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, (float) (i + 1), (float) (i + 2));
        CHECK(s >= 0, "key %d is served (%d)", i, s);
    }
    CHECK(sl_bgtf_stats()->slot_refills - b.slot_refills == (unsigned long) SLOTS,
          "four FREE slots are four refills (%lu)",
          sl_bgtf_stats()->slot_refills - b.slot_refills);
    CHECK(sl_bgtf_stats()->slot_evictions == b.slot_evictions,
          "and no eviction (%lu)", sl_bgtf_stats()->slot_evictions - b.slot_evictions);

    /* A NEW WALK. Every slot is full, but every key belongs to the PREVIOUS
     * walk, so all four are refills and the census reports no pressure. */
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 1, "walk 2 is admitted");
    b = snap();
    for (i = 0; i < SLOTS; i++) {
        s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, (float) (i + 1), (float) (i + 2));
        CHECK(s >= 0, "walk 2's key %d is served (%d)", i, s);
    }
    CHECK(sl_bgtf_stats()->slot_refills - b.slot_refills == (unsigned long) SLOTS,
          "four PRIOR-WALK slots are four more refills, NOT pressure (%lu)",
          sl_bgtf_stats()->slot_refills - b.slot_refills);
    CHECK(sl_bgtf_stats()->slot_evictions == b.slot_evictions,
          "and still no eviction - this is the count that used to be wrong (%lu)",
          sl_bgtf_stats()->slot_evictions - b.slot_evictions);

    /* THE FIFTH KEY OF THE CURRENT WALK. Now a key this same walk may still
     * draw from is displaced, and that - only that - is an eviction. */
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, 99.0f, 98.0f);
    CHECK(s >= 0, "a fifth key in the same walk is served (%d)", s);
    CHECK(sl_bgtf_stats()->slot_evictions - b.slot_evictions == 1,
          "ONE EVICTION: a current-walk key was displaced (%lu)",
          sl_bgtf_stats()->slot_evictions - b.slot_evictions);
    CHECK(sl_bgtf_stats()->slot_refills == b.slot_refills,
          "and it is NOT counted as a refill too (%lu)",
          sl_bgtf_stats()->slot_refills - b.slot_refills);
    end_case();

    /* ---- a slot from a previous walk cannot be BOUND ------------------- */

    begin_case("slots: a slot index from a PREVIOUS walk is refused by the binding, so "
               "no pool computed under another transform can become the source");
    /* `filled` alone would let a caller holding a stale index bind a pool built
     * under a different transform. sl_gfx_dl.c only ever hands back what the
     * prepass just returned, which is exactly why the check is cheap here
     * rather than left as a property of one caller's discipline. */
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "walk 1 is admitted");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f);
    CHECK(s >= 0, "and fills a slot (%d)", s);
    CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_FAR1,
          "which binds in its own walk, answering FAR1");

    /* A NEW WALK, and the old index held over. The slot is still `filled` - no
     * prepass has displaced it - so the generation is the only thing refusing
     * it, which is the property. */
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 1, "walk 2 is admitted");
    b = snap();
    {
        const long calls = tf.calls;
        const long vp = tf.vertex_pointer;
        /* Taken ONCE into a local: a second call in the message argument would
         * bind again on the very failure the message is reporting. */
        const int stale = sl_bgtf_bind_clip(s);
        CHECK(stale == -1, "the SAME index is refused in the new walk (%d)", stale);
        CHECK(tf.calls == calls, "with no GL call (%ld)", tf.calls - calls);
        CHECK(sl_bgtf_stats()->binds == b.binds, "and no bind counted (%lu)",
              sl_bgtf_stats()->binds - b.binds);
        CHECK(tf.vertex_pointer == vp,
              "and NO position array command was issued, so the stale pool never "
              "became the vertex source (%ld)", tf.vertex_pointer - vp);
    }
    /* And the new walk's own prepass does bind, so the refusal above is the
     * generation and not a module that stopped binding. */
    {
        const int s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f);
        CHECK(s2 >= 0, "walk 2's own prepass is served (%d)", s2);
        CHECK(sl_bgtf_bind_clip(s2) == SL_BGGPU_DEPTH_FAR1,
              "and ITS slot binds, answering FAR1");
    }
    end_case();
}

/* ---- 11. context recreation -------------------------------------------- */

static void st_context(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    long calls;
    int s;

    begin_case("context: invalidate makes NO GL call, drops the program and the pool names, "
               "and the next walk re-resolves against the new context");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready in the first context");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "a walk is admitted");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s >= 0, "and served");
    sl_bgtf_bind_clip(s);
    sl_bgtf_unbind();
    CHECK(tf.gen_buffers == 1, "one pool was allocated in that context (%ld)", tf.gen_buffers);

    calls = tf.calls;
    sl_bgtf_invalidate();
    CHECK(tf.calls == calls, "invalidate made no GL call at all (%ld -> %ld)",
          calls, tf.calls);
    CHECK(sl_bgtf_state() == 0, "and returned the state to UNTRIED");
    CHECK(sl_bgtf_why()[0] == '\0', "and cleared the reason ('%s')", sl_bgtf_why());

    /* While untried nothing is usable and nothing reaches GL, which is what
     * proves the program name and the pool names were dropped rather than kept
     * and rebound. */
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 0, "no walk is admitted while untried");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == -1, "no draw is served");
    sl_bgtf_bind_clip(s);
    sl_bgtf_bind_clip(0);
    sl_bgtf_unbind();
    CHECK(tf.calls == calls, "and no GL call was made while untried (%ld)",
          tf.calls - calls);

    /* THE CONTEXT IS REPLACED. Every name minted before now is stale, and the
     * stub records a violation if one is used - which is what makes "re-resolves
     * against the current context" measured rather than asserted. */
    sl_bgtfgl_new_context();
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_ready() == 1, "the next walk is ready in the new context");
    CHECK(tf.create_shader == 1 && tf.compile == 1 && tf.link == 1
          && tf.get_uniform_location == BGTFGL_UNIFORMS && tf.proc_calls == BGTFGL_PROCS,
          "it re-resolved and recompiled against the new context "
          "(resolves %ld, compiles %ld, links %ld, locations %ld)",
          tf.proc_calls, tf.compile, tf.link, tf.get_uniform_location);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 3u) == 1, "a walk is admitted there");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s >= 0, "and served (%d)", s);
    CHECK(tf.gen_buffers == 1,
          "A POOL NAME WAS MINTED AGAIN, so the old context's was dropped rather "
          "than rebound (%ld)", tf.gen_buffers);
    sl_bgtf_bind_clip(s);
    sl_bgtf_unbind();
    CHECK(tf.v_stale_name == 0,
          "no name from before the invalidate was used afterwards (%ld)", tf.v_stale_name);
    end_case();

    begin_case("teardown: a destroyed context is never called into");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready before teardown");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "a walk is admitted");
    sl_bgtf_invalidate();           /* what the backend calls before deleting */
    tf.context_destroyed = 1;
    /* Nothing the module exposes deletes a GL object: the header records that
     * the driver releases the program and the buffers with the context, so
     * invalidate is the whole of teardown. The stub provides no glDeleteShader,
     * glDeleteProgram or glDeleteBuffers, so a delete would not even link. */
    sl_bgtf_invalidate();
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 0, "no walk is admitted after teardown");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1, "and no draw");
    sl_bgtf_bind_clip(0);
    sl_bgtf_unbind();
    CHECK(tf.v_call_after_context_destroyed == 0,
          "nothing reached GL after the context was destroyed (%ld)",
          tf.v_call_after_context_destroyed);
    tf.context_destroyed = 0;
    end_case();
}

/* ---- 12. the watchdogs, proven able to fire ---------------------------- */

typedef void (*fn_begin_tf)(GLenum);
typedef void (*fn_end_tf)(void);

static void st_watchdogs(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo, small;
    fn_begin_tf beg;
    fn_end_tf end;

    /* THE WATCHDOGS ARE LIVE, NOT VACUOUSLY SILENT. Every case above ends by
     * asserting they are zero; this case makes three of them fire on purpose,
     * so those zeros mean something. The stale-name control is the one that
     * matters most, because it is what "re-resolves against the current
     * context" rests on. */
    begin_case("watchdog: the stale-name counter fires when the module is NOT told the "
               "context changed");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready in the first context");
    sl_bgtfgl_new_context();        /* no sl_bgtf_invalidate: the module still
                                     * holds the old context's program name */
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "the walk is admitted, because the module believes it is still ready");
    CHECK(tf.v_stale_name > 0,
          "and using a program name from the replaced context is detected (%ld)",
          tf.v_stale_name);
    tf.v_stale_name = 0;            /* the positive control is the point here */
    end_case();

    begin_case("watchdog: the discard, program and overrun counters fire on a draw the "
               "module would never make");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    /* A draw with no discard, no program and no recording buffer - the three
     * preconditions of a prepass draw, all absent. */
    glDrawArrays(GL_POINTS, 0, 4);
    CHECK(tf.v_draw_without_discard == 1, "a draw with discard off is detected (%ld)",
          tf.v_draw_without_discard);
    CHECK(tf.v_draw_without_program == 1, "and one with no program current (%ld)",
          tf.v_draw_without_program);
    tf.v_draw_without_discard = 0;
    tf.v_draw_without_program = 0;

    /* A POOL TOO SMALL FOR THE DRAW. Sixteen bytes is one vertex; a thousand
     * vertices would be a write 15984 bytes past the end, which is exactly what
     * the vertex-count guard and the per-slot sizing exist to prevent. */
    small = sl_bgtfgl_make_buffer(16);
    sl_bgtfgl_test_state(tf.last_program, 0u, 0u, small, 1);
    beg = (fn_begin_tf) sl_gl_proc("glBeginTransformFeedback");
    end = (fn_end_tf) sl_gl_proc("glEndTransformFeedback");
    CHECK(beg != NULL && end != NULL, "the fake driver has the scope entry points");
    if (beg != NULL && end != NULL) {
        beg(GL_POINTS);
        glDrawArrays(GL_POINTS, 0, 1000);
        CHECK(tf.v_tf_overrun == 1, "a draw past the end of the pool is detected (%ld)",
              tf.v_tf_overrun);
        tf.v_tf_overrun = 0;
        end();
        end();                      /* the second closes nothing */
        CHECK(tf.v_tf_unbalanced == 1, "and a scope that ends unopened (%ld)",
              tf.v_tf_unbalanced);
        tf.v_tf_unbalanced = 0;
    }
    sl_bgtfgl_test_state(0u, 0u, 0u, 0u, 0);
    end_case();
}

/* ---- 13. refusals and stale output ------------------------------------- */

static void st_refusals(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    struct sl_bgtf_counters b;
    long calls;
    int s;

    begin_case("refusal: a draw with no admitted walk, an unknown depth mode, and a clip "
               "binding that is out of range or unfilled");
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);

    /* A DRAW WITHOUT A WALK. The transform uniforms belong to the walk, so a
     * prepass with none admitted would run the previous walk's transform over
     * this item's positions. */
    b = snap();
    calls = tf.calls;
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
          "a prepass with no admitted walk is refused");
    CHECK(tf.calls == calls, "with no GL call (%ld)", tf.calls - calls);
    CHECK(sl_bgtf_stats()->refusals - b.refusals == 1, "and one refusal counted");

    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "a walk is admitted");
    b = snap();
    calls = tf.calls;
    CHECK(sl_bgtf_prepass(3, K1, K2) == -1, "a depth mode of 3 is refused");
    CHECK(sl_bgtf_prepass(-1, K1, K2) == -1, "a depth mode of -1 is refused");
    CHECK(sl_bgtf_prepass(99, K1, K2) == -1, "a depth mode of 99 is refused");
    CHECK(tf.calls == calls, "none of the three made a GL call (%ld)", tf.calls - calls);
    CHECK(sl_bgtf_stats()->refusals - b.refusals == 3, "three refusals were counted (%lu)",
          sl_bgtf_stats()->refusals - b.refusals);
    CHECK(tf.gen_buffers == 0, "and no pool was taken for a mode that has none");

    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s == 0, "the first real draw takes slot 0 (%d)", s);

    /* A BINDING THAT WOULD READ A POOL THAT DOES NOT EXIST. Out of range in
     * both directions, and a slot index inside the pool that nothing has
     * filled: each is a no-op rather than a bind of whatever is there. */
    b = snap();
    calls = tf.calls;
    /* EACH ANSWERS -1, which is the fact the caller acts on: a bind that could
     * not happen must be distinguishable from one that bound a SINGLE pool,
     * and a void return made those two look alike. */
    CHECK(sl_bgtf_bind_clip(-1) == -1, "a slot index of -1 is refused");
    CHECK(sl_bgtf_bind_clip(-100) == -1, "and -100");
    CHECK(sl_bgtf_bind_clip(SLOTS) == -1, "and one past the last slot");
    CHECK(sl_bgtf_bind_clip(99) == -1, "and 99");
    CHECK(sl_bgtf_bind_clip(s + 1) == -1, "and one in range that nothing has filled");
    CHECK(tf.calls == calls,
          "five bad clip bindings made no GL call at all (%ld)", tf.calls - calls);
    CHECK(sl_bgtf_stats()->binds == b.binds,
          "and none counted as a bind (%lu)", sl_bgtf_stats()->binds - b.binds);
    CHECK(sl_bgtf_stats()->raster_draws == b.raster_draws,
          "nor as a raster draw (%lu)",
          sl_bgtf_stats()->raster_draws - b.raster_draws);
    /* And the good one still works, so the five above are not a module that
     * stopped binding altogether. */
    CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_SINGLE,
          "while the filled slot binds and answers its own depth");
    CHECK(sl_bgtf_stats()->binds - b.binds == 1,
          "counting one bind (%lu)", sl_bgtf_stats()->binds - b.binds);
    CHECK(tf.calls > calls, "and does make GL calls");
    end_case();
}

/* ---- 14. slot exhaustion ----------------------------------------------- */

static void st_slots(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    float p1[5], p2[5];
    int s[5], i, j;
    struct sl_bgtf_counters b;

    begin_case("slots: a fifth distinct key in one walk EVICTS and counts it, rather than "
               "serving a wrong pool");
    /* Five distinct coefficient pairs, every one out of the REAL
     * sl_wa_far_range rather than written here: the two real overlaps, then
     * three more ranges. A frame that needs more pools than the module has is
     * the case this is about, and the module's own note says an eviction is
     * counted rather than hidden so the census says so. */
    sl_wa_far_range(ZFAR * SL_WA_FAR_OVERLAP,       WMAX,          &p1[0], &p2[0]);
    sl_wa_far_range(ZFAR * SL_WA_FAR_OVERLAP_BLEND, WMAX,          &p1[1], &p2[1]);
    sl_wa_far_range(ZFAR * SL_WA_FAR_OVERLAP,       WMAX * 1.5f,   &p1[2], &p2[2]);
    sl_wa_far_range(ZFAR * SL_WA_FAR_OVERLAP_BLEND, WMAX * 2.0f,   &p1[3], &p2[3]);
    sl_wa_far_range(ZFAR * 0.95f,                   WMAX * 3.0f,   &p1[4], &p2[4]);
    for (i = 0; i < 5; i++)
        for (j = i + 1; j < 5; j++)
            CHECK(p1[i] != p1[j] || p2[i] != p2[j],
                  "pair %d and pair %d are distinct (%.8f/%.4f vs %.8f/%.4f)",
                  i, j, (double) p1[i], (double) p2[i], (double) p1[j], (double) p2[j]);

    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    b = snap();
    for (i = 0; i < SLOTS; i++) {
        s[i] = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, p1[i], p2[i]);
        CHECK(s[i] >= 0 && s[i] < SLOTS, "key %d is served (%d)", i, s[i]);
    }
    for (i = 0; i < SLOTS; i++)
        for (j = i + 1; j < SLOTS; j++)
            CHECK(s[i] != s[j], "keys %d and %d took different slots (%d, %d)",
                  i, j, s[i], s[j]);
    CHECK(sl_bgtf_stats()->slot_evictions == b.slot_evictions,
          "the first four fit without evicting (%lu)",
          sl_bgtf_stats()->slot_evictions - b.slot_evictions);
    CHECK(sl_bgtf_stats()->prepasses - b.prepasses == SLOTS,
          "and ran one prepass each (%lu)", sl_bgtf_stats()->prepasses - b.prepasses);

    /* THE FIFTH. There is no free slot and nothing from an older walk, so the
     * least recently used pool is taken - and the eviction is COUNTED. */
    b = snap();
    s[4] = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, p1[4], p2[4]);
    CHECK(s[4] >= 0 && s[4] < SLOTS, "the fifth key is served (%d)", s[4]);
    CHECK(sl_bgtf_stats()->slot_evictions - b.slot_evictions == 1,
          "ONE EVICTION WAS COUNTED, not hidden (%lu)",
          sl_bgtf_stats()->slot_evictions - b.slot_evictions);
    CHECK(sl_bgtf_stats()->prepasses - b.prepasses == 1,
          "and a real prepass ran for it (%lu)", sl_bgtf_stats()->prepasses - b.prepasses);
    CHECK(sl_bgtf_stats()->reuses == b.reuses, "it was not served from a slot (%lu)",
          sl_bgtf_stats()->reuses - b.reuses);
    CHECK(s[4] == s[0], "the LEAST RECENTLY USED pool was the one taken (%d, expected %d)",
          s[4], s[0]);

    /* THE EVICTED KEY IS NOT SERVED A WRONG POOL. Asking for it again runs
     * another prepass rather than returning the slot that now holds the fifth
     * key's output. */
    b = snap();
    i = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, p1[0], p2[0]);
    CHECK(i >= 0, "the evicted key is served again (%d)", i);
    CHECK(i != s[4], "FROM A DIFFERENT SLOT than the key that displaced it (%d vs %d)",
          i, s[4]);
    CHECK(sl_bgtf_stats()->prepasses - b.prepasses == 1,
          "by running a prepass (%lu)", sl_bgtf_stats()->prepasses - b.prepasses);
    CHECK(sl_bgtf_stats()->reuses == b.reuses,
          "and NOT by reusing whatever was in the slot (%lu)",
          sl_bgtf_stats()->reuses - b.reuses);
    CHECK(sl_bgtf_stats()->slot_evictions - b.slot_evictions == 1,
          "displacing one more (%lu)", sl_bgtf_stats()->slot_evictions - b.slot_evictions);
    /* The fifth key's own pool survived that, so the eviction choice is not
     * simply "always slot 0". */
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, p1[4], p2[4]) == s[4],
          "and the fifth key's pool is still its own");
    CHECK(sl_bgtf_stats()->reuses - b.reuses == 1, "served as a reuse (%lu)",
          sl_bgtf_stats()->reuses - b.reuses);
    end_case();
}

/* ---- 15. counter truthfulness ----------------------------------------- */

static void st_counters(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    struct sl_bgtf_counters b;
    long calls;
    int s;

    begin_case("counters: a refusal never raises prepasses, vertices, raster draws or "
               "raster indices - refusals rises instead");
    /* A FALLBACK TO THE CPU MUST NOT BE ABLE TO READ AS GPU COVERAGE. The
     * baseline is taken after a real submission, so the counters being compared
     * are non-zero and an "unchanged" check is not vacuous. */
    arm();
    CHECK(sl_bgtf_ready() == 1, "ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "a walk is admitted");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f);
    CHECK(s >= 0, "and served");
    sl_bgtf_count_submission(sl_bgtf_bind_clip(s), 100u);
    b = snap();
    CHECK(b.prepasses > 0 && b.vertices > 0 && b.raster_draws > 0 && b.raster_indices > 0,
          "the baseline counters are non-zero, so 'unchanged' means something");

    /* NINE REFUSALS, counted one at a time so the arithmetic is visible. */
    CHECK(sl_bgtf_begin_walk(NULL, NV, vbo, STR, 2u) == 0, "1: a null transform");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f) == -1,
          "2: a draw on the walk that refusal ended");
    CHECK(sl_bgtf_begin_walk(&w, 0u, vbo, STR, 2u) == 0, "3: an empty pool");
    CHECK(sl_bgtf_begin_walk(&w, NV, 0u, STR, 2u) == 0, "4: no input buffer");
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, 0u, 2u) == 0, "5: no input stride");
    CHECK(sl_bgtf_begin_walk(&w, VCAP + 1u, vbo, STR, 2u) == 0, "6: an absurd count");
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 1, "a walk is admitted again");
    CHECK(sl_bgtf_prepass(4, K1, K2) == -1, "7: an unknown depth mode");
    CHECK(sl_bgtf_prepass(-2, K1, K2) == -1, "8: another unknown depth mode");
    tf.gen_buffers_zero = 1;
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == -1,
          "9: a pool that cannot be allocated");
    tf.gen_buffers_zero = 0;

    CHECK(sl_bgtf_stats()->refusals - b.refusals == 9,
          "nine refusals were counted (%lu)", sl_bgtf_stats()->refusals - b.refusals);
    CHECK(sl_bgtf_stats()->prepasses == b.prepasses,
          "and not one raised prepasses (%lu -> %lu)", b.prepasses,
          sl_bgtf_stats()->prepasses);
    CHECK(sl_bgtf_stats()->vertices == b.vertices,
          "or vertices (%lu -> %lu)", b.vertices, sl_bgtf_stats()->vertices);
    CHECK(sl_bgtf_stats()->raster_draws == b.raster_draws,
          "or raster draws (%lu -> %lu)", b.raster_draws, sl_bgtf_stats()->raster_draws);
    CHECK(sl_bgtf_stats()->raster_indices == b.raster_indices,
          "or raster indices (%lu -> %lu)", b.raster_indices,
          sl_bgtf_stats()->raster_indices);
    CHECK(sl_bgtf_stats()->reuses == b.reuses,
          "or reuses (%lu -> %lu)", b.reuses, sl_bgtf_stats()->reuses);

    /* THE COUNTERS ARE CUMULATIVE ACROSS A CONTEXT, which is what makes the
     * census readable after a recreation: invalidate drops the capability and
     * the pools, not the record of what ran. */
    b = snap();
    sl_bgtf_invalidate();
    CHECK(sl_bgtf_stats()->prepasses == b.prepasses
          && sl_bgtf_stats()->vertices == b.vertices
          && sl_bgtf_stats()->refusals == b.refusals
          && sl_bgtf_stats()->raster_draws == b.raster_draws,
          "invalidate leaves the census alone");

    /* sl_bgtf_count_submission HAS NO CAPABILITY GUARD, and the asymmetry is
     * pinned rather than assumed. Every other entry point here refuses while
     * the module is untried or unavailable; this one counts what it is handed
     * except an empty run. That follows from its contract - it is called at the
     * one place a background run is really submitted, and only the caller knows
     * whether a prepass pool was the vertex source for THAT run - so the gate
     * is the caller's `g_bgtf_src` and not a state this module can see.
     *
     * WHICH IS WHY THE INTEGRATION SEAM IS TESTED SEPARATELY. The guard this
     * function lacks lives in src/gfx/sl_gfx_dl.c, so a module-only fixture can
     * only pin the asymmetry; proving the gate is actually applied needs the
     * real caller, which test_sl_gfx_bgtf_seam.c drives. */
    CHECK(sl_bgtf_state() == 0, "the module is untried after the invalidate (%d)",
          sl_bgtf_state());
    b = snap();
    calls = tf.calls;
    sl_bgtf_count_submission(SL_BGGPU_DEPTH_FAR1, 5u);
    CHECK(sl_bgtf_stats()->raster_indices - b.raster_indices == 5,
          "the submission count carries no capability guard of its own (%lu)",
          sl_bgtf_stats()->raster_indices - b.raster_indices);
    CHECK(sl_bgtf_stats()->raster_draws - b.raster_draws == 1,
          "and counts the run (%lu)", sl_bgtf_stats()->raster_draws - b.raster_draws);
    CHECK(tf.calls == calls, "while making no GL call (%ld)", tf.calls - calls);
    /* AN EMPTY RUN IS STILL DROPPED while untried, so the one thing it does
     * refuse is refused on every path rather than only on a ready module. */
    b = snap();
    sl_bgtf_count_submission(SL_BGGPU_DEPTH_FAR1, 0u);
    CHECK(sl_bgtf_stats()->raster_draws == b.raster_draws
          && sl_bgtf_stats()->raster_indices == b.raster_indices,
          "an empty run is dropped even while the module is untried");
    end_case();
}

/* ---- 16. no read-back, no sync point ---------------------------------- */

/* THIS IS CHECKED BY THE BUILD, NOT BY AN ASSERTION. The stub provides no
 * glFinish, glFlush, glGetError, glMapBuffer, glUnmapBuffer, glMapBufferRange,
 * glGetBufferSubData or glReadPixels, no fence, sync or query entry point, and
 * no glDeleteBuffers - neither as a symbol nor in sl_gl_proc's table.
 * glDeleteProgram and glDeleteShader are not symbols either and are not in that
 * table; each is reachable only through its own gate, which the
 * neutral-program control's own cases set, so with both gates off - which is
 * every run of this suite - a resolve of either still answers NULL and the two
 * assertions below are about that.
 * A direct call to one would be an undefined symbol
 * at LINK time and this executable would not exist; a resolve of one returns
 * NULL and would make the module refuse its own capability, which the readiness
 * case would see. A counter can only speak for the paths a test drives; a
 * missing symbol speaks for the whole translation unit. The assertion that
 * belongs here is the count and the identity of what the module DOES resolve,
 * which st_ready makes: exactly these twenty-four, in this order. */
static void st_no_sync(void)
{
    static const char *const forbidden[] = {
        "glFinish", "glFlush", "glGetError",
        "glMapBuffer", "glUnmapBuffer", "glMapBufferRange", "glGetBufferSubData",
        "glReadPixels", "glFenceSync", "glClientWaitSync", "glWaitSync",
        "glGenQueries", "glBeginQuery", "glEndQuery", "glGetQueryObjectuiv",
        "glGetQueryObjectiv", "glMemoryBarrier", "glGetUniformfv",
        "glDeleteShader", "glDeleteProgram", "glDeleteBuffers",
        "glGetTransformFeedbackVarying", "glBindBufferRange"
    };
    size_t i;

    begin_case("sync: no read-back, no glFinish and no per-frame synchronisation entry "
               "point exists to call");
    arm();
    CHECK(sl_bgtf_ready() == 1,
          "the module becomes ready with only those twenty-four present");
    CHECK(tf.proc_calls == BGTFGL_PROCS, "and resolves exactly %d (%ld)",
          BGTFGL_PROCS, tf.proc_calls);
    for (i = 0; i < sizeof forbidden / sizeof forbidden[0]; i++)
        CHECK(sl_gl_proc(forbidden[i]) == NULL,
              "%s does not resolve here at all", forbidden[i]);
    printf("    LINK-TIME GUARANTEE: this executable linked, and no glFinish, glFlush, "
           "glGetError, glMapBuffer, glUnmapBuffer, glGetBufferSubData, glReadPixels, "
           "fence, sync or query symbol exists in the fixture. A call to one from "
           "src/gfx/sl_gfx_bgtf.c would be an undefined reference over the WHOLE "
           "translation unit, so the product path performs no read-back, no glFinish "
           "and no per-frame synchronisation. That is proven by the build, not by a "
           "counter.\n");
    end_case();
}

/* ---- 17. the production buffer diagnostic (SL_BG_TF_DUMP) --------------
 *
 * ONE SCENARIO PER PROCESS, and that is forced by the module rather than
 * chosen: the switch is parsed once into a static, and the ONE capture attempt
 * is spent for the life of the process on purpose, so a failure cannot retry at
 * another draw, frame or slot. A fixture that drove all of these in one run
 * would be asserting the behaviour of the second attempt, which does not exist.
 * tools/windows/bgtftest.ps1 runs this binary once per scenario with the
 * selector and the scratch output directory the scenario needs.
 *
 * WHAT IS REAL AND WHAT IS NOT. The module is the product's. The GL is the
 * synthetic one, so the pool's CONTENTS are bytes a test installed and no
 * driver produced them; what that proves is that the record carries the pool's
 * bytes unchanged, which is the property the comparison contract rests on. The
 * CPU clip array is the fixture's, chosen so its z DISAGREES with its zraw -
 * otherwise no test could tell which of the two a FAR1 record took.
 */

/* A small pool: the sizes here are the contract's arithmetic, not the capture's.
 * 64 vertices is 1024 bytes a pool and three triangles is 9 indices, which keeps
 * the whole scenario inside the stub's 64 KiB buffer store. */
#define DNV   64u
#define DIDX   9u

/* The released control's own numbers, used literally so the clock case is about
 * the mapping and not about a stand-in: the draw-site tuple is 139:139:139.
 *
 * D_PUMP IS DELIBERATELY NEITHER 139 NOR 279, and the value is arbitrary. The
 * draw site's sl_frames_completed() sample is a different sample of a mutable
 * counter from the one the screenshot line prints, taken earlier in the frame,
 * and no measurement fixes what it is: the r557 snapshot that established the
 * 139:139:139 / screenshot-279 mapping did not contain it. A fixture that used
 * 279 here would be asserting an equality nobody measured, and one that used
 * 139 could not tell this field from the gate clocks. So it carries a distinct
 * plausible value, and the property under test is that the record CARRIES it
 * and the gate ACCEPTS it without selecting on it. */
#define D_READ  139u
#define D_FRAME 139u
#define D_CLOUD 139u
#define D_PUMP  277u
#define D_MESH   29u
#define D_MAT    29u

/* THE VERTEX AND COMPONENT THE UNEQUAL SCENARIOS MOVE, fixed here so the
 * expected mismatch is a named index and component rather than "some
 * difference". 31 is d_ix[4] - it really is submitted, exactly once, so one
 * moved component is exactly one reported mismatch and not zero or two. */
#define D_ODD_VERT  31u
#define D_ODD_IXPOS  4u

static const char D_ITEM[] = "xbla/background/fixtureitem";

static float    d_clip[DNV * 4u];
static float    d_zraw[DNV];
static unsigned d_ix[DIDX];
static unsigned char d_ref[DNV * 16u];     /* what the CPU file must hold  */
static unsigned char d_pool[DNV * 16u];    /* what the fake pool holds     */
/* WHAT THE GPU FILE MUST HOLD, KEPT APART FROM WHAT WAS INSTALLED. These are
 * the same bytes in an honest build. They are two arrays so that a build which
 * substitutes the CPU reference for the pool - BGTF_MUT_GPUISCPU - changes
 * d_pool and leaves this one alone, and every assertion over the GPU file then
 * compares against what the scenario MEANT to install rather than against
 * whatever it ended up installing. An assertion that reads its expectation out
 * of the mutated value cannot catch the mutation. */
static unsigned char d_expect_gpu[DNV * 16u];
static struct sl_bggpu_walk d_w;

static void d_put_u32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char) (v & 0xFFu);
    p[1] = (unsigned char) ((v >> 8) & 0xFFu);
    p[2] = (unsigned char) ((v >> 16) & 0xFFu);
    p[3] = (unsigned char) ((v >> 24) & 0xFFu);
}

static unsigned d_bits(float f)
{
    unsigned u;
    memcpy(&u, &f, sizeof u);
    return u;
}

/* THE CPU ARRAYS, AND THE RECORD THE CONTRACT SAYS THEY PRODUCE.
 *
 * clip[2] and zraw are DELIBERATELY DIFFERENT numbers at every vertex, and the
 * expected CPU payload takes clip[2] in EVERY depth mode. That is the
 * distinction the record rests on: the payload is the contemporaneous clip
 * array, component for component, and the unclamped depth is not substituted
 * into z for FAR1. A fixture whose two depths agreed could not tell the two
 * apart at all - it would pass against a record that took either one - and the
 * substitution it would have hidden is one that manufactures agreement with the
 * GPU pool precisely where the submitted bits are the question.
 *
 * `dmode` no longer changes the expected payload, and the parameter is kept
 * because the scenarios pass the range they are about and a reader comparing
 * this with the sidecar's dmode_name should see the same value flowing through. */
static void d_build(int dmode)
{
    unsigned i;
    (void) dmode;
    memset(&d_w, 0, sizeof d_w);
    for (i = 0u; i < 16u; i++) d_w.C[i] = (float) (i + 1) * 0.0625f;
    d_w.org[0] = 3.5f; d_w.org[1] = -4.25f; d_w.org[2] = 5.125f;
    d_w.sc = 0.5f; d_w.post = 0.75f; d_w.fogless = 0;

    for (i = 0u; i < DNV; i++) {
        d_clip[i * 4u + 0u] = 1.0f + (float) i;
        d_clip[i * 4u + 1u] = -2.0f - (float) i * 0.5f;
        d_clip[i * 4u + 2u] = 100.0f + (float) i;          /* the clamped z  */
        d_clip[i * 4u + 3u] = 7.0f + (float) i * 0.25f;
        d_zraw[i]           = -900.0f - (float) i;         /* the raw z      */
    }
    for (i = 0u; i < DIDX; i++) d_ix[i] = (i * 7u + 3u) % DNV;

    for (i = 0u; i < DNV; i++) {
        unsigned char *r = d_ref + (size_t) i * 16u;
        d_put_u32(r + 0u,  d_bits(d_clip[i * 4u + 0u]));
        d_put_u32(r + 4u,  d_bits(d_clip[i * 4u + 1u]));
        d_put_u32(r + 8u,  d_bits(d_clip[i * 4u + 2u]));
        d_put_u32(r + 12u, d_bits(d_clip[i * 4u + 3u]));
    }
    /* The fake pool starts EQUAL to the reference, so a record whose two pools
     * compare equal is reachable; d_pool_differ moves one named component of
     * one submitted vertex so an unequal one is reachable too. */
    memcpy(d_pool, d_ref, sizeof d_pool);
    memcpy(d_expect_gpu, d_pool, sizeof d_expect_gpu);
}

/* ONE NAMED COMPONENT OF ONE SUBMITTED VERTEX, MOVED IN THE POOL ONLY.
 *
 * WHY THIS EXISTS. Every scenario that installed the pool equal to the CPU
 * reference made both payload comparisons pass whatever the writer did with the
 * two buffers - a GPU file that received the CPU bytes would have been
 * indistinguishable from a correct one. These scenarios install a pool that
 * DIFFERS from the reference in exactly one uint32, so the two files can be
 * asserted bitwise and separately, and the out-of-process comparison has a
 * fixed expected mismatch count at a fixed index and component.
 *
 * `pattern` is the replacement bits. A one-ULP step is the smallest difference
 * representable and the hardest case for any comparison that rounds; the other
 * caller substitutes the unclamped depth's bits, which is exactly what the
 * discarded-clip.z defect would have made agree. */
static void d_pool_differ(unsigned vert, unsigned comp, unsigned pattern)
{
    d_put_u32(d_pool + (size_t) vert * 16u + (size_t) comp * 4u, pattern);
    memcpy(d_expect_gpu, d_pool, sizeof d_expect_gpu);
#ifdef BGTF_MUT_GPUISCPU
    /* THE MUTANT: the pool is handed the CPU REFERENCE's bytes, which is what a
     * writer that put the CPU buffer in the GPU file would produce. d_expect_gpu
     * keeps the bytes the scenario meant to install, so every assertion over the
     * GPU payload - and the out-of-process mismatch count - must now fail. */
    memcpy(d_pool, d_ref, sizeof d_pool);
#endif
}

/* The two files' differing component patterns, counted the way the sidecar's
 * comparison rule states it: all four uint32 of each SUBMITTED index, in the
 * index sequence's own order. Reading the files rather than the arrays, because
 * what is under test is what reached disk. */
static unsigned d_mismatches(const unsigned char *gpu, const unsigned char *cpu,
                             unsigned *first_vert, unsigned *first_comp,
                             unsigned *first_pos)
{
    unsigned n = 0u, k, c;
    if (first_vert != NULL) *first_vert = ~0u;
    if (first_comp != NULL) *first_comp = ~0u;
    if (first_pos != NULL) *first_pos = ~0u;
    for (k = 0u; k < DIDX; k++) {
        const size_t base = (size_t) d_ix[k] * 16u;
        for (c = 0u; c < 4u; c++) {
            if (memcmp(gpu + base + c * 4u, cpu + base + c * 4u, 4u) != 0) {
                if (n == 0u) {
                    if (first_vert != NULL) *first_vert = d_ix[k];
                    if (first_comp != NULL) *first_comp = c;
                    if (first_pos != NULL) *first_pos = k;
                }
                n++;
            }
        }
    }
    return n;
}

static struct sl_bgtf_dump_site d_site(int dmode, int slot, float k1, float k2)
{
    struct sl_bgtf_dump_site s;
    memset(&s, 0, sizeof s);
    s.read = D_READ;
    s.dl_frame = D_FRAME;
    s.cloud = D_CLOUD;
    s.pumped = D_PUMP;
    s.item = D_ITEM;
    s.item_bytes = (unsigned) sizeof D_ITEM;
    s.mesh = D_MESH;
    s.material = D_MAT;
    s.dmode = dmode;
    s.slot = slot;
    s.k1 = k1;
    s.k2 = k2;
    s.nverts = DNV;
    s.clip = d_clip;
    s.zraw = d_zraw;
    s.w = &d_w;
    return s;
}

/* ONE WALK WITH ONE FILLED POOL, and the pool's name taken out of the stub's
 * own transcript rather than predicted. Returns the slot, or -1. */
static int d_walk(unsigned gen, int dmode, float k1, float k2, GLuint *pool)
{
    struct sl_bggpu_walk w = a_walk();
    const long mark = tf.nlog;
    GLuint vbo = input_vbo(DNV, STR);
    int slot;

    if (pool != NULL) *pool = 0u;
    if (!sl_bgtf_begin_walk(&w, DNV, vbo, STR, gen)) return -1;
    slot = sl_bgtf_prepass(dmode, k1, k2);
    if (slot < 0) return -1;
    if (pool != NULL) {
        const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
        *pool = base != NULL ? (GLuint) base->c : 0u;
    }
    return slot;
}

static const char *d_dir(void)
{
    const char *d = getenv("SL_BG_TF_DUMP_DIR");
    return (d != NULL && *d != '\0') ? d : "bgtfdumps";
}

static void d_path(char *out, size_t cap, int dmode, const char *suffix)
{
    snprintf(out, cap, "%s/tfclip-%u-%u-%u-m%u-t%u-d%d%s", d_dir(),
             D_READ, D_FRAME, D_CLOUD, D_MESH, D_MAT, dmode, suffix);
}

/* A file's length, or -1 when it does not exist. */
static long d_len(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (f == NULL) return -1L;
    if (fseek(f, 0L, SEEK_END) != 0) { fclose(f); return -1L; }
    n = ftell(f);
    fclose(f);
    return n;
}

static int d_read(const char *path, unsigned char *out, size_t cap, size_t *got)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    *got = fread(out, 1u, cap, f);
    fclose(f);
    return 1;
}

/* No record of this scenario may be on disk when it starts: the writer is
 * create-only, so a leftover from an earlier run would make the scenario about
 * the refusal instead of about the capture. */
static void d_clean(int dmode)
{
    /* The staging name is swept too: it is create-only like the rest, so a
     * leftover from an earlier run would make the next scenario about the
     * refusal rather than about the capture. */
    static const char *const sfx[5] = { ".gpuclip.bin", ".cpuclip.bin",
                                        ".idx.bin", ".json", ".json.staging" };
    char p[600];
    int i;
    for (i = 0; i < 5; i++) { d_path(p, sizeof p, dmode, sfx[i]); remove(p); }
}

static void d_no_files(int dmode, const char *what)
{
    static const char *const sfx[5] = { ".gpuclip.bin", ".cpuclip.bin",
                                        ".idx.bin", ".json", ".json.staging" };
    char p[600];
    int i;
    for (i = 0; i < 5; i++) {
        d_path(p, sizeof p, dmode, sfx[i]);
        CHECK(d_len(p) < 0, "%s: no %s was created", what, sfx[i]);
    }
}

/* ---- 17a. the switch OFF, which is a list of things that do not happen -- */

static void d_mode_off(void)
{
    struct sl_bgtf_dump_site s;
    const long mark0 = tf.nlog;
    long seq_calls, seq_procs;
    GLuint pool = 0u;
    int slot;

    begin_case("dump off: with SL_BG_TF_DUMP absent, zero or malformed NOTHING is "
               "resolved, allocated, read or written, and the raster call sequence is "
               "the candidate's own");
    arm();
    d_build(SL_BGGPU_DEPTH_FAR1);
    d_clean(SL_BGGPU_DEPTH_FAR1);
    /* THE READBACK IS AVAILABLE FROM THE DRIVER, which is what makes this case
     * about the switch rather than about a stub that has nothing to hand out. */
    tf.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 0, "the diagnostic is OFF (%d)",
          sl_bgtf_dump_opt_in());
    {
        int i, mismatch = 0;
        for (i = 0; i < 64; i++) if (sl_bgtf_dump_opt_in() != 0) mismatch++;
        CHECK(mismatch == 0, "and 64 further reads answer the same, so the switch is "
                             "parsed once (%d differed)", mismatch);
    }
    CHECK(sl_bgtf_ready() == 1, "the candidate itself is ready");
    slot = d_walk(1u, SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f, &pool);
    CHECK(slot >= 0 && pool != 0u, "a pool was filled (slot %d, buffer %u)",
          slot, (unsigned) pool);
    CHECK(sl_bgtf_bind_clip(slot) == SL_BGGPU_DEPTH_FAR1, "and bound");

    /* The whole prepass and bind are in the transcript already; from here the
     * offer and the capture must add nothing at all. */
    seq_calls = tf.calls;
    seq_procs = tf.proc_calls;
    {
        const long here = tf.nlog;
        s = d_site(SL_BGGPU_DEPTH_FAR1, slot, 0.0f, 0.0f);
        CHECK(sl_bgtf_dump_select(&s) == 0, "no draw is selected while off");
        sl_bgtf_dump_capture(d_ix, DIDX);
        CHECK(tf.nlog == here,
              "and the offer and the capture add NOTHING to the GL transcript "
              "(%ld records)", tf.nlog - here);
    }
    CHECK(tf.calls == seq_calls, "no GL call was made (%ld)", tf.calls - seq_calls);
    CHECK(tf.proc_calls == seq_procs,
          "NO ENTRY POINT WAS RESOLVED - in particular glGetBufferSubData was never "
          "asked for (%ld resolves)", tf.proc_calls - seq_procs);
    CHECK(tf.get_buffer_sub_data == 0, "no readback happened (%ld)",
          tf.get_buffer_sub_data);
    CHECK(tf.get_error_calls == 0,
          "and no GL error was consumed, so nobody else's error state moved (%ld)",
          tf.get_error_calls);
    CHECK(sl_bgtf_dump_state() == 0, "the diagnostic state is UNATTEMPTED (%d)",
          sl_bgtf_dump_state());
    CHECK(sl_bgtf_dump_status()[0] == '\0', "with no status words ('%s')",
          sl_bgtf_dump_status());
    {
        const struct sl_bgtf_dump_counters *c = sl_bgtf_dump_stats();
        CHECK(c->offers == 0 && c->selected == 0 && c->captures == 0
              && c->readbacks == 0 && c->completed == 0,
              "and the census is empty: offers %lu selected %lu captures %lu "
              "readbacks %lu completed %lu", c->offers, c->selected, c->captures,
              c->readbacks, c->completed);
    }
    d_no_files(SL_BGGPU_DEPTH_FAR1, "off");
    /* The transcript since the walk began holds exactly one recording draw and
     * no readback record anywhere, which is the candidate's own sequence. */
    CHECK(first_of(mark0, TFGL_GETBUFSUBDATA) == NULL,
          "no readback record appears anywhere in the walk");
    CHECK(tf.draw_arrays == 1, "exactly the one prepass draw (%ld)", tf.draw_arrays);
    sl_bgtf_unbind();
    end_case();
}

/* ---- 17b. selection: every rejection, then the one capture ------------- */

static void d_mode_sel(void)
{
    const int DM = SL_BGGPU_DEPTH_FAR1;
    const struct sl_bgtf_dump_counters *c;
    struct sl_bgtf_dump_site s;
    GLuint pool = 0u, pool2 = 0u;
    char p[600];
    int slot, slot2;

    begin_case("dump selection: the clock, the identity, a stale generation, a "
               "wrong-depth pool, the other coefficient pair and a missing CPU array "
               "are each refused WITHOUT spending the attempt");
    arm();
    d_build(DM);
    d_clean(DM);
    tf.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1,
          "the diagnostic is ARMED - this scenario needs SL_BG_TF_DUMP="
          "139:139:139:29:29:* and both provenance digests");
    CHECK(sl_bgtf_ready() == 1, "the candidate is ready");

    slot = d_walk(1u, DM, 0.0f, 0.0f, &pool);
    CHECK(slot >= 0 && pool != 0u, "a FAR1 pool was filled (slot %d)", slot);
    CHECK(sl_bgtf_bind_clip(slot) == DM, "and bound");

    /* THE CLOCK, one field at a time. The draw-site read is 139; the
     * screenshot's 140 is a DIFFERENT counter and must not select. */
    s = d_site(DM, slot, 0.0f, 0.0f); s.read = 140u;
    CHECK(sl_bgtf_dump_select(&s) == 0,
          "draw-site read 140 is refused: the screenshot's read is not this clock");
    s = d_site(DM, slot, 0.0f, 0.0f); s.dl_frame = 279u;
    CHECK(sl_bgtf_dump_select(&s) == 0,
          "g_dl_frame 279 is refused: the pumped frame is not this clock either");
    s = d_site(DM, slot, 0.0f, 0.0f); s.cloud = 140u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "an adjacent cloud phase is refused");
    c = sl_bgtf_dump_stats();
    CHECK(c->clock_miss == 3, "three clock misses (%lu)", c->clock_miss);
    CHECK(c->selected == 0 && c->captures == 0,
          "nothing selected and no attempt spent (%lu / %lu)", c->selected,
          c->captures);

    /* THE IDENTITY. */
    s = d_site(DM, slot, 0.0f, 0.0f); s.mesh = 28u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "another mesh is refused");
    s = d_site(DM, slot, 0.0f, 0.0f); s.material = 30u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "another material is refused");
    s = d_site(DM, slot, 0.0f, 0.0f); s.item = "xbla/background/somethingelse";
    s.item_bytes = 30u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "another item is refused");
    CHECK(sl_bgtf_dump_stats()->ident_miss == 3, "three identity misses (%lu)",
          sl_bgtf_dump_stats()->ident_miss);

    /* A WRONG-DEPTH POOL. The selector's dmode is '*' in this scenario, so the
     * refusal below is the SLOT's own key disagreeing - a FAR1 draw offered a
     * pool that holds a FAR0 range - and not the selector failing to match. */
    {
        const int far0 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
        CHECK(far0 >= 0 && far0 != slot,
              "a FAR0 pool took a DIFFERENT slot (%d vs %d)", far0, slot);
        s = d_site(DM, far0, 0.0f, 0.0f);
        CHECK(sl_bgtf_dump_select(&s) == 0,
              "a FAR1 draw offered the FAR0 slot is refused on the key");
        /* AND FAR0's TWO PAIRS ARE NOT ONE POOL. The blend overlap's pair takes
         * its own slot, and offering one slot with the other pair's
         * coefficients is refused - which is the whole reason the pair is in
         * the key rather than the range alone. */
        {
            const int blend = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, T1, T2);
            CHECK(blend >= 0 && blend != far0,
                  "the blend overlap's pair took another slot again (%d vs %d)",
                  blend, far0);
            s = d_site(SL_BGGPU_DEPTH_FAR0, far0, T1, T2);
            CHECK(sl_bgtf_dump_select(&s) == 0,
                  "the opaque slot offered with the BLEND pair is refused");
            s = d_site(SL_BGGPU_DEPTH_FAR0, blend, K1, K2);
            CHECK(sl_bgtf_dump_select(&s) == 0,
                  "and the blend slot offered with the OPAQUE pair is refused");
        }
        s = d_site(DM, SLOTS, 0.0f, 0.0f);
        CHECK(sl_bgtf_dump_select(&s) == 0, "a slot index outside the cache is refused");
        s = d_site(DM, -1, 0.0f, 0.0f);
        CHECK(sl_bgtf_dump_select(&s) == 0, "and a negative one");
    }

    /* A STALE GENERATION: a new walk makes every key of the previous one
     * unmatchable, so the slot index the old walk returned names a pool that is
     * no longer the one it was accepted for. */
    slot2 = d_walk(2u, DM, 0.0f, 0.0f, &pool2);
    CHECK(slot2 >= 0, "a second walk filled a pool (slot %d)", slot2);
    s = d_site(DM, slot, 0.0f, 0.0f);
    if (slot != slot2)
        CHECK(sl_bgtf_dump_select(&s) == 0,
              "the FIRST walk's slot is refused under the second walk's generation");
    CHECK(sl_bgtf_dump_stats()->slot_reject >= 5,
          "the slot rejections were counted as such (%lu)",
          sl_bgtf_dump_stats()->slot_reject);

    /* WHAT THE RECORD NEEDS. A record with no CPU reference would invite a
     * reader to compare the pool against nothing. */
    s = d_site(DM, slot2, 0.0f, 0.0f); s.clip = NULL;
    CHECK(sl_bgtf_dump_select(&s) == 0, "no CPU clip array is refused");
    s = d_site(DM, slot2, 0.0f, 0.0f); s.zraw = NULL;
    CHECK(sl_bgtf_dump_select(&s) == 0, "no raw depth array is refused");
    s = d_site(DM, slot2, 0.0f, 0.0f); s.w = NULL;
    CHECK(sl_bgtf_dump_select(&s) == 0, "no transform is refused");
    CHECK(sl_bgtf_dump_stats()->bounds_reject == 3, "three bounds rejections (%lu)",
          sl_bgtf_dump_stats()->bounds_reject);
    CHECK(sl_bgtf_dump_select(NULL) == 0, "and a null offer is refused");

    /* NOTHING ABOVE RESOLVED, READ OR WROTE ANYTHING. */
    CHECK(tf.get_buffer_sub_data == 0, "no readback has happened yet (%ld)",
          tf.get_buffer_sub_data);
    CHECK(sl_bgtf_dump_state() == 0, "the attempt is UNSPENT (%d)",
          sl_bgtf_dump_state());
    d_no_files(DM, "rejections");
    d_no_files(SL_BGGPU_DEPTH_FAR0, "rejections");
    end_case();

    /* ---- and now the one capture, on the second walk's own pool --------- */
    begin_case("dump capture: ONE readback, after the transform feedback ended and "
               "before the raster draw, into a create-only record whose bytes are the "
               "pool's and whose CPU z is the UNCLAMPED depth");
    CHECK(sl_bgtfgl_pool_write(pool2, 0L, d_pool, (long) sizeof d_pool) == 1,
          "the synthetic pool contents were installed in buffer %u",
          (unsigned) pool2);
    CHECK(sl_bgtf_bind_clip(slot2) == DM, "the second walk's pool is the source");
    {
        const long mark = tf.nlog;
        const long draws_before = tf.draw_arrays;
        const struct sl_bgtfgl_call *rb;
        long iend = -1, irb = -1, i;
        struct sl_bgtfgl_call vptr_before = tf.log[0];
        int have_vptr = 0;

        /* The position pointer as the bind left it, so the readback can be shown
         * not to have moved it. */
        {
            const struct sl_bgtfgl_call *v = NULL;
            for (i = tf.nlog - 1; i >= 0; i--)
                if (tf.log[i].kind == TFGL_VERTEXPOINTER) { v = &tf.log[i]; break; }
            if (v != NULL) { vptr_before = *v; have_vptr = 1; }
        }
        CHECK(have_vptr, "a position pointer was set by the bind");

        s = d_site(DM, slot2, 0.0f, 0.0f);
        CHECK(sl_bgtf_dump_select(&s) == 1, "the draw IS selected");
        CHECK(sl_bgtf_dump_stats()->selected == 1, "and counted once (%lu)",
              sl_bgtf_dump_stats()->selected);
        sl_bgtf_dump_capture(d_ix, DIDX);

        CHECK(sl_bgtf_dump_state() == 1, "the record is COMPLETE (%d): %s",
              sl_bgtf_dump_state(), sl_bgtf_dump_status());
        CHECK(strcmp(sl_bgtf_dump_status(), "complete") == 0,
              "with the complete status ('%s')", sl_bgtf_dump_status());
        CHECK(tf.get_buffer_sub_data == 1, "exactly ONE readback (%ld)",
              tf.get_buffer_sub_data);
        CHECK(tf.get_buffer_sub_bytes == (long) DNV * 16L,
              "asking for the ACTIVE vertex count's bytes and no more (%ld, expected "
              "%ld)", tf.get_buffer_sub_bytes, (long) DNV * 16L);
        rb = first_of(mark, TFGL_GETBUFSUBDATA);
        CHECK(rb != NULL, "the readback is in the transcript");
        if (rb != NULL) {
            CHECK(rb->a == GL_ARRAY_BUFFER,
                  "through the generic array target (0x%lX)", rb->a);
            CHECK(rb->d == (long) pool2, "on the SELECTED pool (%ld, expected %u)",
                  rb->d, (unsigned) pool2);
            CHECK(rb->off == 0, "from the start of it (%ld)", rb->off);
            CHECK(rb->c == (long) DNV * 16L, "and it wrote the whole request (%ld)",
                  rb->c);
        }
        /* THE PLACEMENT, read out of the ORDER of the transcript: the last
         * glEndTransformFeedback is before the readback, and the readback is the
         * last record of all - the raster draw has not happened yet. */
        for (i = 0; i < tf.nlog; i++) {
            if (tf.log[i].kind == TFGL_ENDTF) iend = i;
            if (tf.log[i].kind == TFGL_GETBUFSUBDATA) irb = i;
        }
        CHECK(iend >= 0 && irb >= 0 && iend < irb,
              "the readback is AFTER glEndTransformFeedback (end %ld, read %ld)",
              iend, irb);
        CHECK(tf.v_read_inside_tf == 0,
              "and no read happened inside an open transform-feedback scope (%ld)",
              tf.v_read_inside_tf);
        CHECK(tf.v_read_past_end == 0, "and none past the pool's storage (%ld)",
              tf.v_read_past_end);
        CHECK(tf.tf_active == 0, "no scope is open (%d)", tf.tf_active);

        /* THE BINDINGS, PUT BACK EXACTLY. The array buffer is 0 again, so the
         * pass's later client pointers are host addresses; the indexed
         * transform-feedback binding was not touched; and NO pointer command
         * ran, so the clip binding's own glVertexPointer still names the pool. */
        CHECK(tf.arr_buf == 0u, "GL_ARRAY_BUFFER is back to 0 (%u)",
              (unsigned) tf.arr_buf);
        CHECK(tf.tf_buf == 0u, "the indexed TF binding is untouched (%u)",
              (unsigned) tf.tf_buf);
        CHECK(tf.current == 0u, "the fixed pipeline is still current (%u)",
              (unsigned) tf.current);
        CHECK(tf.discard == 0, "rasterizer discard is still off");
        if (have_vptr) {
            const struct sl_bgtfgl_call *v = NULL;
            for (i = tf.nlog - 1; i >= 0; i--)
                if (tf.log[i].kind == TFGL_VERTEXPOINTER) { v = &tf.log[i]; break; }
            CHECK(v == &vptr_before || (v != NULL
                  && v->a == vptr_before.a && v->b == vptr_before.b
                  && v->c == vptr_before.c && v->d == vptr_before.d
                  && v->off == vptr_before.off),
                  "NO pointer command ran: the position array is still the one the "
                  "clip binding set");
        }
        CHECK(tf.vptr.size == 4 && tf.vptr.buf == pool2,
              "and it still names the pool as four floats (size %d, buffer %u)",
              tf.vptr.size, (unsigned) tf.vptr.buf);
        CHECK(tf.draw_arrays == draws_before,
              "NO SECOND TRANSFORM RAN - the record is the live pool's bytes, not a "
              "re-run of the prepass into somewhere else (%ld new draws)",
              tf.draw_arrays - draws_before);
    }

    /* ---- the files ----------------------------------------------------- */
    {
        static unsigned char got[DNV * 16u + 64u];
        size_t n = 0u;
        d_path(p, sizeof p, DM, ".gpuclip.bin");
        CHECK(d_len(p) == (long) DNV * 16L, "the GPU pool file is %ld bytes (%s)",
              (long) DNV * 16L, p);
        CHECK(d_read(p, got, sizeof got, &n) && n == (size_t) DNV * 16u,
              "and reads back whole (%u bytes)", (unsigned) n);
        CHECK(n == (size_t) DNV * 16u && memcmp(got, d_expect_gpu, n) == 0,
              "BYTE FOR BYTE the pool's own contents - the record carries what the "
              "buffer held, not a re-derivation of it");

        d_path(p, sizeof p, DM, ".cpuclip.bin");
        CHECK(d_len(p) == (long) DNV * 16L, "the CPU pool file is %ld bytes",
              (long) DNV * 16L);
        CHECK(d_read(p, got, sizeof got, &n) && n == (size_t) DNV * 16u,
              "and reads back whole (%u bytes)", (unsigned) n);
        CHECK(n == (size_t) DNV * 16u && memcmp(got, d_ref, n) == 0,
              "and holds the CONTEMPORANEOUS CLIP ARRAY'S own bits in all four "
              "components, z included - not the unclamped zraw, which is a different "
              "number here and is never substituted into the payload");
        /* Said positively as well, so a future change that made the two agree
         * could not make the check above vacuous. */
        CHECK(d_bits(d_clip[8u * 4u + 2u]) != d_bits(d_zraw[8]),
              "the fixture's clip z and raw depth really do differ, so the check "
              "above distinguishes them");

        d_path(p, sizeof p, DM, ".idx.bin");
        CHECK(d_len(p) == (long) DIDX * 4L, "the index file is %ld bytes",
              (long) DIDX * 4L);
        CHECK(d_read(p, got, sizeof got, &n) && n == (size_t) DIDX * 4u,
              "and reads back whole (%u bytes)", (unsigned) n);
        {
            int bad = 0;
            unsigned k;
            for (k = 0u; k < DIDX && (size_t) (k * 4u + 4u) <= n; k++) {
                unsigned v = (unsigned) got[k * 4u] | ((unsigned) got[k * 4u + 1u] << 8)
                           | ((unsigned) got[k * 4u + 2u] << 16)
                           | ((unsigned) got[k * 4u + 3u] << 24);
                if (v != d_ix[k]) bad++;
            }
            CHECK(bad == 0, "in the run's EXACT submitted order, little-endian uint32 "
                            "(%d out of order)", bad);
        }
        d_path(p, sizeof p, DM, ".json");
        CHECK(d_len(p) > 400L, "the sidecar exists and is not a stub (%ld bytes)",
              d_len(p));
    }

    /* ---- ONE ATTEMPT, EVEN ACROSS LATER MATCHING DRAWS AND WALKS ------- */
    {
        const struct sl_bgtf_dump_counters *k;
        int slot3;
        GLuint pool3 = 0u;
        long rb = tf.get_buffer_sub_data;

        s = d_site(DM, slot2, 0.0f, 0.0f);
        CHECK(sl_bgtf_dump_select(&s) == 0,
              "a SECOND matching draw of the same walk is not selected");
        sl_bgtf_dump_capture(d_ix, DIDX);
        slot3 = d_walk(3u, DM, 0.0f, 0.0f, &pool3);
        CHECK(slot3 >= 0, "a third walk - a later frame - filled a pool");
        CHECK(sl_bgtf_bind_clip(slot3) == DM, "and bound it");
        s = d_site(DM, slot3, 0.0f, 0.0f);
        CHECK(sl_bgtf_dump_select(&s) == 0,
              "and a matching draw of THAT walk is not selected either");
        sl_bgtf_dump_capture(d_ix, DIDX);
        CHECK(tf.get_buffer_sub_data == rb,
              "no further readback happened (%ld)", tf.get_buffer_sub_data - rb);
        k = sl_bgtf_dump_stats();
        CHECK(k->captures == 1, "exactly one attempt was ever claimed (%lu)",
              k->captures);
        CHECK(k->completed == 1, "and exactly one record sealed (%lu)", k->completed);
        CHECK(k->readbacks == 1, "with exactly one readback (%lu)", k->readbacks);
        /* Two: the second draw of this walk, and the matching draw of the walk
         * after it. Both are offers that arrived with the attempt already
         * spent, which is a different fact from a clock or a key that did not
         * match and is counted apart from them. */
        CHECK(k->late_offers == 2,
              "the two later offers were counted as LATE rather than as misses (%lu)",
              k->late_offers);
        CHECK(k->clock_miss + k->ident_miss + k->slot_reject + k->bounds_reject
              == k->offers - k->selected,
              "and every other offer is accounted for by exactly one rejection "
              "reason (%lu offers, %lu selected, %lu + %lu + %lu + %lu rejected)",
              k->offers, k->selected, k->clock_miss, k->ident_miss,
              k->slot_reject, k->bounds_reject);
        CHECK(sl_bgtf_dump_state() == 1, "and the state is still COMPLETE (%d)",
              sl_bgtf_dump_state());
    }
    sl_bgtf_unbind();
    end_case();
}

/* ---- 17c. the clock mapping, which is the one thing a control gets wrong */

static void d_mode_clock(void)
{
    const int DM = SL_BGGPU_DEPTH_FAR1;
    struct sl_bgtf_dump_site s;
    GLuint pool = 0u;
    char p[600];
    int slot;

    begin_case("dump clock: the selector's three fields are the DRAW SITE's counters - "
               "screenshot read 140 / frame 279 / cloud 139 is draw-site 139:139:139 - "
               "and an adjacent value in any field does not match");
    arm();
    d_build(DM);
    d_clean(DM);
    tf.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1,
          "armed - this scenario needs SL_BG_TF_DUMP=139:139:139:29:29:2");
    CHECK(sl_bgtf_ready() == 1, "ready");
    slot = d_walk(1u, DM, 0.0f, 0.0f, &pool);
    CHECK(slot >= 0 && pool != 0u, "a pool was filled");
    CHECK(sl_bgtf_bind_clip(slot) == DM, "and bound");
    CHECK(sl_bgtfgl_pool_write(pool, 0L, d_pool, (long) sizeof d_pool) == 1,
          "the pool contents were installed");

    /* THE WRONG CLOCKS, INCLUDING THE ONE A CONTROL WOULD ACTUALLY BE GIVEN.
     * The screenshot line prints read=140 and frame=279; a selector built from
     * those numbers names a draw that is not this one, and this is the case
     * that says so rather than leaving it to be assumed. */
    s = d_site(DM, slot, 0.0f, 0.0f); s.read = 140u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "draw-site read 140 does not match");
    s = d_site(DM, slot, 0.0f, 0.0f); s.read = 140u; s.dl_frame = 279u;
    CHECK(sl_bgtf_dump_select(&s) == 0,
          "and neither does the screenshot's whole tuple 140 / 279 / 139");
    s = d_site(DM, slot, 0.0f, 0.0f); s.read = 138u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "nor the previous read");
    s = d_site(DM, slot, 0.0f, 0.0f); s.dl_frame = 138u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "nor an adjacent dl frame");
    s = d_site(DM, slot, 0.0f, 0.0f); s.dl_frame = 140u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "in either direction");
    s = d_site(DM, slot, 0.0f, 0.0f); s.cloud = 138u;
    CHECK(sl_bgtf_dump_select(&s) == 0, "nor an adjacent cloud phase");
    /* AND THE DEPTH IS PINNED IN THIS SELECTOR, so another range is refused by
     * the selector itself rather than by the slot key. */
    {
        const int far0 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
        CHECK(far0 >= 0, "a FAR0 pool exists (%d)", far0);
        s = d_site(SL_BGGPU_DEPTH_FAR0, far0, K1, K2);
        CHECK(sl_bgtf_dump_select(&s) == 0,
              "a FAR0 draw is refused by a selector that names dmode 2");
        CHECK(sl_bgtf_dump_stats()->ident_miss >= 1,
              "as an IDENTITY miss, not a slot one (%lu)",
              sl_bgtf_dump_stats()->ident_miss);
    }
    CHECK(sl_bgtf_dump_stats()->clock_miss == 6, "six clock misses (%lu)",
          sl_bgtf_dump_stats()->clock_miss);
    CHECK(sl_bgtf_dump_state() == 0, "and the attempt is unspent (%d)",
          sl_bgtf_dump_state());
    d_no_files(DM, "clock misses");

    /* THE RIGHT ONE: the draw-site tuple, with the pumped frame recorded beside
     * it rather than selected on. */
    CHECK(sl_bgtf_bind_clip(slot) == DM, "the FAR1 pool is the source again");
    /* THE PUMPED COUNTER IS RECORDED AND NOT SELECTED ON, and the way to say so
     * is to offer the same draw with three different values of it and have all
     * three answer yes. The draw site's sl_frames_completed() sample is a
     * different sample of a mutable counter from the screenshot's, taken earlier
     * in the frame, so a selector that discriminated on it would be
     * discriminating on something no measurement fixes - including the
     * screenshot's own 279, which is the value a control would be tempted to
     * require. Offering is not capturing: a match costs no attempt. */
    s = d_site(DM, slot, 0.0f, 0.0f); s.pumped = 279u;
    CHECK(sl_bgtf_dump_select(&s) == 1,
          "the screenshot's pumped frame 279 at the draw site is ACCEPTED, not "
          "required and not refused");
    s = d_site(DM, slot, 0.0f, 0.0f); s.pumped = 0u;
    CHECK(sl_bgtf_dump_select(&s) == 1, "and so is 0");
    CHECK(sl_bgtf_dump_state() == 0,
          "and none of that spent the attempt (%d)", sl_bgtf_dump_state());

    s = d_site(DM, slot, 0.0f, 0.0f);
    CHECK(s.read == 139u && s.dl_frame == 139u && s.cloud == 139u
          && s.pumped == D_PUMP,
          "the offered draw is draw-site 139:139:139 with a draw-site pumped of "
          "%u - a plausible value distinct from both the gate clocks and the "
          "screenshot's 279", D_PUMP);
    CHECK(D_PUMP != 279u && D_PUMP != D_FRAME,
          "the fixture's pumped value really is distinct from the screenshot's 279 "
          "and from g_dl_frame, so the checks below are not vacuous");
    CHECK(sl_bgtf_dump_select(&s) == 1, "and it IS selected");
    sl_bgtf_dump_capture(d_ix, DIDX);
    CHECK(sl_bgtf_dump_state() == 1, "the record is complete (%d): %s",
          sl_bgtf_dump_state(), sl_bgtf_dump_status());
    d_path(p, sizeof p, DM, ".json");
    CHECK(d_len(p) > 400L, "and the sidecar exists (%ld bytes)", d_len(p));
    /* The sidecar's own words, so the mapping is in the artefact and not only in
     * this fixture. The verifier checks the fields; this checks they are there
     * at all, which a reader of a failing run will want. */
    {
        static unsigned char js[16384];
        char want[80];
        size_t n = 0u;
        d_read(p, js, sizeof js - 1u, &n);
        js[n < sizeof js ? n : sizeof js - 1u] = 0u;
        CHECK(strstr((const char *) js, "\"gate_tuple\": \"139:139:139\"") != NULL,
              "the sidecar's gate tuple is the DRAW SITE's 139:139:139");
        snprintf(want, sizeof want, "\"draw_site_pumped\": %u", D_PUMP);
        CHECK(strstr((const char *) js, want) != NULL,
              "and the DRAW SITE's own pumped sample is recorded separately, under a "
              "name that says which site it came from (%s)", want);
        CHECK(strstr((const char *) js, "\"screenshot_frame_pumped\"") == NULL,
              "and the record does NOT claim a screenshot sample it never took");
        CHECK(strstr((const char *) js, "\"draw_site_read\": 139") != NULL,
              "with the draw-site read named as such");
    }
    sl_bgtf_unbind();
    end_case();
}

/* ---- 17d. the FAR0 reading of the CPU z -------------------------------- */

static void d_mode_far0(void)
{
    const int DM = SL_BGGPU_DEPTH_FAR0;
    struct sl_bgtf_dump_site s;
    GLuint pool = 0u;
    char p[600];
    int slot;

    begin_case("dump FAR0: the CPU reference's z is the clip z THIS RANGE left behind, "
               "not the raw depth, and the record's coefficient bits are the draw's");
    arm();
    d_build(DM);
    d_clean(DM);
    tf.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1, "armed with dmode 1");
    CHECK(sl_bgtf_ready() == 1, "ready");
    slot = d_walk(1u, DM, K1, K2, &pool);
    CHECK(slot >= 0 && pool != 0u, "a FAR0 pool was filled with the REAL opaque pair");
    CHECK(sl_bgtf_bind_clip(slot) == DM, "and bound");
    CHECK(sl_bgtfgl_pool_write(pool, 0L, d_pool, (long) sizeof d_pool) == 1,
          "the pool contents were installed");
    s = d_site(DM, slot, K1, K2);
    CHECK(sl_bgtf_dump_select(&s) == 1, "the FAR0 draw is selected");
    sl_bgtf_dump_capture(d_ix, DIDX);
    CHECK(sl_bgtf_dump_state() == 1, "the record is complete (%d): %s",
          sl_bgtf_dump_state(), sl_bgtf_dump_status());
    {
        static unsigned char got[DNV * 16u + 64u];
        size_t n = 0u;
        d_path(p, sizeof p, DM, ".cpuclip.bin");
        CHECK(d_read(p, got, sizeof got, &n) && n == (size_t) DNV * 16u,
              "the CPU file reads back whole (%u bytes)", (unsigned) n);
        CHECK(n == (size_t) DNV * 16u && memcmp(got, d_ref, n) == 0,
              "and its z is the clip array's, NOT zraw - the pre-range reference "
              "would be the wrong numbers for this range");
    }
    {
        static unsigned char js[16384];
        size_t n = 0u;
        char want[64];
        d_path(p, sizeof p, DM, ".json");
        d_read(p, js, sizeof js - 1u, &n);
        js[n < sizeof js ? n : sizeof js - 1u] = 0u;
        snprintf(want, sizeof want, "\"k1\": \"0x%08x\"", d_bits(K1));
        CHECK(strstr((const char *) js, want) != NULL,
              "the sidecar carries the draw's own k1 BITS (%s)", want);
        snprintf(want, sizeof want, "\"slot_k2_bits\": \"0x%08x\"", d_bits(K2));
        CHECK(strstr((const char *) js, want) != NULL,
              "and the slot's k2 bits agree with it (%s)", want);
        CHECK(strstr((const char *) js, "\"dmode_name\": \"FAR0\"") != NULL,
              "and the range is named FAR0");
        CHECK(strstr((const char *) js,
                     "\"cpu_z_source\": \"clip[2] as it stands at this draw\"") != NULL,
              "and the z's source is declared as the clip array, in the one wording "
              "that holds for every range");
        CHECK(strstr((const char *) js, "\"cpu_payload_provenance\"") != NULL,
              "with the payload's provenance stated component by component");
    }
    sl_bgtf_unbind();
    end_case();
}

/* ---- 17d2. the two pools DISTINCT, which is what makes the record evidence -
 *
 * WHY THIS CASE HAD TO EXIST. Every complete scenario above installs the pool
 * EQUAL to the CPU reference, so both payload comparisons pass whatever the
 * writer did with the two buffers: a GPU file that received the CPU bytes would
 * read as correct, and the out-of-process comparison would report zero
 * mismatches either way. Here the pool differs from the reference in exactly ONE
 * uint32 of ONE submitted vertex, so the two files are asserted bitwise and
 * SEPARATELY, the difference has a fixed index, position and component, and a
 * substitution of one buffer for the other changes the answer.
 *
 * AN UNEQUAL RECORD IS A VALID RECORD. The seal is about whether the bytes were
 * obtained and written, never about whether they agree: these scenarios are
 * sealed COMPLETE while reporting one mismatch, and the out-of-process verifier
 * is asked for exactly that count. Making acceptance depend on equality would
 * make the one outcome the diagnostic exists to find into an invalid packet.
 *
 * THE TWO VARIANTS DIFFER IN WHAT THE ODD COMPONENT IS:
 *
 *   ulp    one ULP up in x - the smallest representable difference, and the
 *          hardest case for any comparison that rounds anywhere.
 *   far1z  the unclamped depth's own bits in z, on a FAR1 draw. This is the
 *          one that catches the discarded-clip.z defect: a writer that put
 *          zraw into the CPU payload's z would make these two AGREE, the
 *          mismatch count would be 0 instead of 1, and the record would claim
 *          agreement at exactly the vertex where the submitted clip bits and
 *          the pool's bits are the question.
 */
static void d_mode_unequal(const char *which)
{
    const int DM = SL_BGGPU_DEPTH_FAR1;
    const unsigned V = D_ODD_VERT;
    struct sl_bgtf_dump_site s;
    GLuint pool = 0u;
    char p[600];
    unsigned odd_comp, odd_pattern;
    int slot;

    begin_case("dump unequal: a COMPLETE record whose two pools differ in one named "
               "component of one submitted vertex, with each payload asserted bitwise "
               "against its own source");
    printf("    scenario: %s\n", which);
    arm();
    d_build(DM);
    d_clean(DM);
    tf.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1, "armed with dmode 2");
    CHECK(sl_bgtf_ready() == 1, "ready");

    if (strcmp(which, "far1z") == 0) {
        odd_comp = 2u;                       /* z */
        odd_pattern = d_bits(d_zraw[V]);
    } else {
        odd_comp = 0u;                       /* x, one ULP up */
        odd_pattern = d_bits(d_clip[V * 4u + 0u]) + 1u;
    }
    d_pool_differ(V, odd_comp, odd_pattern);
    CHECK(d_ix[D_ODD_IXPOS] == V,
          "vertex %u is index position %u of the submitted run, so one moved "
          "component is exactly one reported mismatch (%u)", V, D_ODD_IXPOS,
          d_ix[D_ODD_IXPOS]);
    {
        unsigned k, hits = 0u;
        for (k = 0u; k < DIDX; k++) if (d_ix[k] == V) hits++;
        CHECK(hits == 1u,
              "and it is submitted exactly ONCE, so the count is not a multiple (%u)",
              hits);
    }
    CHECK(memcmp(d_expect_gpu, d_ref, sizeof d_ref) != 0,
          "the pool the scenario means to install really DIFFERS from the CPU "
          "reference, so this case is not the equal arrangement under another name");

    slot = d_walk(1u, DM, 0.0f, 0.0f, &pool);
    CHECK(slot >= 0 && pool != 0u, "a FAR1 pool was filled (slot %d)", slot);
    CHECK(sl_bgtf_bind_clip(slot) == DM, "and bound");
    CHECK(sl_bgtfgl_pool_write(pool, 0L, d_pool, (long) sizeof d_pool) == 1,
          "the DISTINCT pool contents were installed in buffer %u", (unsigned) pool);
    s = d_site(DM, slot, 0.0f, 0.0f);
    CHECK(sl_bgtf_dump_select(&s) == 1, "the draw is selected");
    sl_bgtf_dump_capture(d_ix, DIDX);

    /* UNEQUAL IS NOT INCOMPLETE. */
    CHECK(sl_bgtf_dump_state() == 1,
          "the record is sealed COMPLETE even though the pools disagree (%d): %s",
          sl_bgtf_dump_state(), sl_bgtf_dump_status());
    CHECK(strcmp(sl_bgtf_dump_status(), "complete") == 0,
          "with the complete status ('%s')", sl_bgtf_dump_status());

    {
        static unsigned char gotg[DNV * 16u + 64u];
        static unsigned char gotc[DNV * 16u + 64u];
        size_t ng = 0u, nc = 0u;
        unsigned fv = ~0u, fc = ~0u, fp = ~0u, nmis;

        d_path(p, sizeof p, DM, ".gpuclip.bin");
        CHECK(d_read(p, gotg, sizeof gotg, &ng) && ng == (size_t) DNV * 16u,
              "the GPU payload reads back whole (%u bytes)", (unsigned) ng);
        /* EXACT, AGAINST WHAT THE SCENARIO INSTALLED. */
        CHECK(ng == (size_t) DNV * 16u && memcmp(gotg, d_expect_gpu, ng) == 0,
              "and is BYTE FOR BYTE the pool the scenario installed");

        d_path(p, sizeof p, DM, ".cpuclip.bin");
        CHECK(d_read(p, gotc, sizeof gotc, &nc) && nc == (size_t) DNV * 16u,
              "the CPU payload reads back whole (%u bytes)", (unsigned) nc);
        /* EXACT, AGAINST THE CLIP ARRAY. */
        CHECK(nc == (size_t) DNV * 16u && memcmp(gotc, d_ref, nc) == 0,
              "and is BYTE FOR BYTE the contemporaneous clip array, z from clip[2]");

        /* AND THE TWO FILES REALLY ARE DIFFERENT FILES. */
        CHECK(ng == nc && memcmp(gotg, gotc, ng) != 0,
              "the two payload files are not the same bytes, so neither can be the "
              "other written twice");

        if (ng == (size_t) DNV * 16u && nc == (size_t) DNV * 16u) {
            nmis = d_mismatches(gotg, gotc, &fv, &fc, &fp);
            CHECK(nmis == 1u,
                  "EXACTLY ONE component pattern of the submitted run differs (%u)",
                  nmis);
            CHECK(fv == V, "at vertex %u (%u)", V, fv);
            CHECK(fc == odd_comp, "in component %u of x,y,z,w (%u)", odd_comp, fc);
            CHECK(fp == D_ODD_IXPOS, "at index position %u (%u)", D_ODD_IXPOS, fp);
            /* THE PATTERNS, NOT JUST THE PLACE. */
            {
                unsigned g = 0u, c = 0u;
                const size_t off = (size_t) V * 16u + (size_t) odd_comp * 4u;
                g = (unsigned) gotg[off] | ((unsigned) gotg[off + 1u] << 8)
                  | ((unsigned) gotg[off + 2u] << 16) | ((unsigned) gotg[off + 3u] << 24);
                c = (unsigned) gotc[off] | ((unsigned) gotc[off + 1u] << 8)
                  | ((unsigned) gotc[off + 2u] << 16) | ((unsigned) gotc[off + 3u] << 24);
                CHECK(g == odd_pattern,
                      "the GPU file holds the installed pattern 0x%08x there (0x%08x)",
                      odd_pattern, g);
                CHECK(c == d_bits(d_clip[V * 4u + odd_comp]),
                      "and the CPU file holds the clip array's own 0x%08x (0x%08x)",
                      d_bits(d_clip[V * 4u + odd_comp]), c);
                if (strcmp(which, "far1z") == 0)
                    CHECK(c != d_bits(d_zraw[V]) && g == d_bits(d_zraw[V]),
                          "THE CLIP z SURVIVED INTO THE PAYLOAD: the CPU file's z is "
                          "clip[2] (0x%08x) and not the unclamped zraw (0x%08x), which "
                          "is what the pool holds - so the comparison CAN observe the "
                          "difference instead of being handed agreement",
                          c, d_bits(d_zraw[V]));
            }
        }
    }
    {
        static unsigned char js[16384];
        char want[80];
        size_t n = 0u;
        d_path(p, sizeof p, DM, ".json");
        d_read(p, js, sizeof js - 1u, &n);
        js[n < sizeof js ? n : sizeof js - 1u] = 0u;
        CHECK(strstr((const char *) js, "\"complete\": true") != NULL,
              "the sidecar seals an UNEQUAL record as complete");
        snprintf(want, sizeof want, "\"clip_z_differs_from_zraw\": %u", DNV);
        CHECK(strstr((const char *) js, want) != NULL,
              "and records the clip-z-against-zraw count as separate evidence about "
              "the two ARRAYS rather than as a payload component (%s)", want);
        CHECK(strstr((const char *) js, "\"cpu_payload_provenance\"") != NULL,
              "with the payload's provenance stated component by component");
    }
    sl_bgtf_unbind();
    end_case();
}

/* ---- 17e. the ACTIVE count, not the slot's capacity -------------------- */

/* A SLOT'S CAPACITY REALLY CAN EXCEED THE COUNT IT HOLDS, which is why the
 * readback's length is the active count and not the storage. sl_bgtf_prepass
 * only grows a pool - `if (sl->cap_verts < g_nverts)` - so a slot sized for a
 * large item and later taken by a smaller one keeps the larger storage, and the
 * bytes past the active count are the PREVIOUS item's. Reading the capacity
 * would put those bytes in the record and invite a reader to compare them.
 *
 * Reaching it takes a little arranging and the arrangement is the product's own
 * eviction rule: a walk with four distinct keys fills every slot, so the next
 * walk finds none free and takes a slot left by the older walk. */
static void d_mode_capacity(void)
{
    const int DM = SL_BGGPU_DEPTH_FAR1;
    /* Four times the active count, and inside the stub's per-buffer store. */
    const unsigned BIG = DNV * 4u;
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_dump_site s;
    static unsigned char tail[DNV * 16u * 3u];
    GLuint vbo, pool = 0u;
    char p[600];
    int slot, i;

    begin_case("dump capacity: a slot sized for a LARGER earlier item is read for the "
               "ACTIVE vertex count only, so the previous item's bytes never enter the "
               "record");
    arm();
    d_build(DM);
    d_clean(DM);
    tf.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1, "armed");
    CHECK(sl_bgtf_ready() == 1, "ready");

    /* WALK 1 FILLS EVERY SLOT at the larger count: SINGLE, FAR1 and FAR0's two
     * coefficient pairs are four distinct keys and there are four slots. */
    vbo = input_vbo(BIG, STR);
    CHECK(sl_bgtf_begin_walk(&w, BIG, vbo, STR, 1u) == 1, "a large walk is admitted");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) >= 0, "SINGLE filled");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR1, 0.0f, 0.0f) >= 0, "FAR1 filled");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) >= 0, "FAR0 opaque filled");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, T1, T2) >= 0, "FAR0 blend filled");

    /* WALK 2, SMALLER, finds no free slot and takes one of the older walk's -
     * keeping its storage, because the module only ever grows a pool. */
    {
        const long before = tf.buffer_data;
        const long mark = tf.nlog;
        vbo = input_vbo(DNV, STR);
        CHECK(sl_bgtf_begin_walk(&w, DNV, vbo, STR, 2u) == 1,
              "a smaller second walk is admitted");
        slot = sl_bgtf_prepass(DM, 0.0f, 0.0f);
        CHECK(slot >= 0, "and is served (slot %d)", slot);
        CHECK(tf.buffer_data == before,
              "WITHOUT A REALLOCATION, so the slot keeps the larger storage (%ld new "
              "glBufferData calls)", tf.buffer_data - before);
        {
            const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
            pool = base != NULL ? (GLuint) base->c : 0u;
        }
        CHECK(pool != 0u, "the pool's name was recorded (%u)", (unsigned) pool);
        CHECK(sl_bgtfgl_buffer_bytes(pool) == (long) BIG * 16L,
              "and its storage is the LARGER item's %ld bytes while the active count "
              "needs %ld (%ld)", (long) BIG * 16L, (long) DNV * 16L,
              sl_bgtfgl_buffer_bytes(pool));
    }
    CHECK(sl_bgtf_bind_clip(slot) == DM, "the pool is bound");

    /* THE ACTIVE VERTICES hold the reference; everything past them holds a
     * recognisable stand-in for the previous item's bytes. */
    CHECK(sl_bgtfgl_pool_write(pool, 0L, d_pool, (long) sizeof d_pool) == 1,
          "the active vertices' contents were installed");
    for (i = 0; i < (int) sizeof tail; i++) tail[i] = (unsigned char) (0xE0u + (i & 7));
    CHECK(sl_bgtfgl_pool_write(pool, (long) DNV * 16L, tail, (long) sizeof tail) == 1,
          "and a DIFFERENT pattern was installed in the capacity past them");

    s = d_site(DM, slot, 0.0f, 0.0f);
    CHECK(sl_bgtf_dump_select(&s) == 1, "the draw is selected");
    sl_bgtf_dump_capture(d_ix, DIDX);
    CHECK(sl_bgtf_dump_state() == 1, "the record is complete (%d): %s",
          sl_bgtf_dump_state(), sl_bgtf_dump_status());
    CHECK(tf.get_buffer_sub_bytes == (long) DNV * 16L,
          "THE READBACK ASKED FOR THE ACTIVE COUNT'S BYTES AND NO MORE (%ld, "
          "capacity would have been %ld)", tf.get_buffer_sub_bytes,
          (long) BIG * 16L);
    CHECK(tf.v_read_past_end == 0, "and read nothing past the storage (%ld)",
          tf.v_read_past_end);
    {
        static unsigned char got[DNV * 16u + 64u];
        static unsigned char beyond[DNV * 16u];
        size_t n = 0u;
        d_path(p, sizeof p, DM, ".gpuclip.bin");
        /* THE LENGTH IS THE WHOLE OF IT. A record of the capacity's length
         * would be four times this, and its tail would be the previous item's
         * bytes; a record of this length cannot contain them at all. */
        CHECK(d_len(p) == (long) DNV * 16L,
              "the record is the ACTIVE count's bytes and not the capacity's "
              "(%ld, the capacity would have given %ld)",
              d_len(p), (long) BIG * 16L);
        CHECK(d_read(p, got, sizeof got, &n) && n == (size_t) DNV * 16u,
              "and reads back whole (%u)", (unsigned) n);
        CHECK(n == (size_t) DNV * 16u && memcmp(got, d_pool, n) == 0,
              "holding the ACTIVE vertices' own bytes, byte for byte");
        /* AND THE CASE IS NOT VACUOUS: what sits past the active count really is
         * different, so a capacity-length read would have produced different
         * bytes rather than more of the same. Asserted by reading the pool's own
         * store rather than by guessing which byte values could not occur - any
         * byte value can occur in a float, which is why a value-range test here
         * would be a test of the fixture's data and not of the module. */
        CHECK(sl_bgtfgl_pool_read(pool, (long) DNV * 16L, beyond,
                                  (long) sizeof beyond) == 1,
              "the capacity past the active count is readable in the stub");
        CHECK(memcmp(beyond, d_pool, sizeof beyond) != 0,
              "and holds something OTHER than the active vertices' bytes, so the "
              "length above is a real distinction");
    }
    {
        static unsigned char js[8192];
        size_t n = 0u;
        char want[64];
        d_path(p, sizeof p, DM, ".json");
        d_read(p, js, sizeof js - 1u, &n);
        js[n < sizeof js ? n : sizeof js - 1u] = 0u;
        snprintf(want, sizeof want, "\"slot_capacity_verts\": %u", BIG);
        CHECK(strstr((const char *) js, want) != NULL,
              "the sidecar records the capacity as such (%s)", want);
        snprintf(want, sizeof want, "\"slot_nverts\": %u", DNV);
        CHECK(strstr((const char *) js, want) != NULL,
              "distinctly from the active count (%s)", want);
        snprintf(want, sizeof want, "\"bytes_read\": %lu", (unsigned long) DNV * 16ul);
        CHECK(strstr((const char *) js, want) != NULL,
              "and says how many bytes were actually read (%s)", want);
    }
    sl_bgtf_unbind();
    end_case();
}

/* THE RENDERER'S NARROW FATAL, SUPPLIED HERE AS A FAILED TEST.
 *
 * WHY IT IS HERE. The module calls sl_fatalf on one explicitly enabled
 * diagnostic's unprovable-binding path; the call is unreachable on every
 * scenario this fixture drives, but the reference to the symbol is not, so the
 * link needs it. Its definition lives in src/platform/sl_ultra_shim.c, which no
 * offline fixture links, and the seam fixture supplies its own for the same
 * reason - this is that convention, with the same wording.
 *
 * A FAULT IS A FAILED TEST, NOT A QUIET EXIT. The module calls this only on a
 * condition it treats as unrecoverable, so a fixture that swallowed it would
 * report a pass for a run that had already gone wrong. It returns, which the
 * module's own abort() immediately after the call makes safe to rely on: a
 * masked fatal cannot become a continuation. */
void sl_fatalf(const char *fmt, unsigned v)
{
    printf("    FAIL: the module called sl_fatalf(\"%s\", %u)\n",
           fmt != NULL ? fmt : "(null)", v);
    fails++;
    case_fails++;
}

/* ---- 17f. one terminal failure per process ----------------------------- */

/* EVERY ONE OF THESE SPENDS THE ATTEMPT, which is why each is its own run: the
 * property under test is that the outcome is a DISTINCT, named refusal, that
 * nothing is reported as a measurement, and that no later draw or frame gets a
 * second try. */
/* THE BYTES AN ALREADY-OCCUPIED PATH HOLDS, written by the scenario so the
 * refusal can be shown to have altered NONE of them. A length alone would not
 * say that: a writer that truncated and then refused would leave 0 bytes, but
 * one that truncated and rewrote the same length would read as untouched. */
static const char D_OCCUPIED[] = "r580 existing record bytes - must survive";

/* Is the path still exactly what the scenario put there? */
static int d_unchanged(const char *path, const char *want_bytes)
{
    static unsigned char got[256];
    const size_t n = strlen(want_bytes);
    size_t had = 0u;
    if (!d_read(path, got, sizeof got, &had)) return 0;
    if (had != n) return 0;
    return memcmp(got, want_bytes, n) == 0;
}

static void d_mode_fail(const char *which)
{
    const int DM = SL_BGGPU_DEPTH_FAR1;
    struct sl_bgtf_dump_site s;
    GLuint pool = 0u;
    char p[600];
    char occupied[600];
    const char *want = "";
    FILE *held = NULL;
    int slot, expect_bytes = 0;
    int want_staging = 0, no_final_json = 0;

    occupied[0] = '\0';
    begin_case("dump failure: a named INCOMPLETE or REFUSED outcome, never a "
               "zero-difference result and never a second attempt");
    printf("    scenario: %s\n", which);
    arm();
    d_build(DM);
    d_clean(DM);
    tf.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 1, "armed");
    CHECK(sl_bgtf_ready() == 1, "ready");
    slot = d_walk(1u, DM, 0.0f, 0.0f, &pool);
    CHECK(slot >= 0 && pool != 0u, "a pool was filled");
    CHECK(sl_bgtf_bind_clip(slot) == DM, "and bound");
    CHECK(sl_bgtfgl_pool_write(pool, 0L, d_pool, (long) sizeof d_pool) == 1,
          "the pool contents were installed");

    if (strcmp(which, "noapi") == 0) {
        /* THE ENTRY POINT THE DIAGNOSTIC NEEDS AND THE DRIVER HAS NOT. */
        tf.provide_readback = 0;
        want = "refused:no-getbuffersubdata";
    } else if (strcmp(which, "readerr") == 0) {
        /* A PRE-EXISTING ERROR AND ONE RAISED BY THE READ ARE DIFFERENT FACTS.
         * The queue hands back GL_NO_ERROR first - so nothing is attributed to
         * the readback that was not its own - and then an error. */
        tf.geterror_queue[0] = 0L;
        tf.geterror_queue[1] = 0x0502L;          /* GL_INVALID_OPERATION */
        tf.ngeterror_queue = 2;
        want = "incomplete:readback-gl-error";
        expect_bytes = 1;
    } else if (strcmp(which, "readnothing") == 0) {
        tf.getsub_fail = 1;
        want = "incomplete:readback-wrote-nothing";
        expect_bytes = 1;
    } else if (strcmp(which, "readpartial") == 0) {
        tf.getsub_bytes = (long) DNV * 8L;       /* half the pool */
        want = "incomplete:readback-partial";
        expect_bytes = 1;
    } else if (strcmp(which, "readzero") == 0) {
        tf.getsub_zero = 1;
        want = "incomplete:readback-all-zero";
        expect_bytes = 1;
    } else if (strcmp(which, "badindex") == 0) {
        d_ix[4] = DNV;                            /* one past the last vertex */
        want = "refused:index-past-vertex-count";
    } else if (strcmp(which, "nocount") == 0) {
        want = "refused:no-indices";
    } else if (strcmp(which, "overflow") == 0) {
        want = "refused:index-count-over-max";
    } else if (strcmp(which, "exists") == 0) {
        /* A PAYLOAD PATH ALREADY TAKEN. The exclusive create refuses it, and the
         * bytes already there are asserted unchanged rather than merely
         * present. */
        FILE *f;
        d_path(occupied, sizeof occupied, DM, ".gpuclip.bin");
        f = fopen(occupied, "wb");
        if (f != NULL) {
            (void) fwrite(D_OCCUPIED, 1u, strlen(D_OCCUPIED), f);
            fclose(f);
        }
        CHECK(d_unchanged(occupied, D_OCCUPIED),
              "a record is already on disk, with known bytes (%ld)",
              d_len(occupied));
        want = "incomplete:file-exists";
    } else if (strcmp(which, "unreadable") == 0) {
        /* AN EXISTING FILE THIS PROCESS CANNOT OPEN FOR READING, which is the
         * case the old probe-then-truncate lost to: fopen("rb") answered NULL,
         * the path was read as free, and the fopen("wb") that followed destroyed
         * a record the diagnostic's whole job was to preserve. The handle below
         * is opened with read sharing DENIED and held across the capture, so
         * reading the path really is impossible while writing it would not have
         * been. The exclusive create never asks: it refuses the path because it
         * EXISTS.
         *
         * Either named create refusal is correct here - the path is taken, and a
         * platform may report a sharing condition instead of an existence one -
         * and the property that matters is the same under both: not one byte of
         * the file changed. */
        d_path(occupied, sizeof occupied, DM, ".gpuclip.bin");
        held = _fsopen(occupied, "wb", _SH_DENYRD);
        CHECK(held != NULL, "an existing file was opened with read sharing denied");
        if (held != NULL) {
            (void) fwrite(D_OCCUPIED, 1u, strlen(D_OCCUPIED), held);
            (void) fflush(held);
        }
        {
            FILE *probe = fopen(occupied, "rb");
            CHECK(probe == NULL,
                  "and it really is UNREADABLE while that handle is held, so the "
                  "old probe would have read the path as free");
            if (probe != NULL) fclose(probe);
        }
        want = NULL;                      /* either create refusal; checked below */
    } else if (strcmp(which, "race") == 0) {
        /* A COMPETING WRITER TAKES THE NEXT PATH MID-CAPTURE. The wrapped close
         * creates the CPU payload's path, with known bytes, as the GPU
         * payload's own close returns - so the path is free when the capture
         * starts and occupied an instant before its create runs. The create
         * must refuse, the bytes that appeared must survive, and the payload
         * already written must survive too.
         *
         * THE NARROWER RACE IS NOT REPRODUCIBLE AGAINST THIS WRITER, and that
         * is the property rather than a gap in the test: a file appearing
         * between a probe and a truncating open needs those to be two
         * operations, and the create is one. See the note at race_on_fclose_
         * after in sl_gfx_bgtf_testgl.h for why the primitive itself cannot be
         * interposed on here. */
        d_path(occupied, sizeof occupied, DM, ".cpuclip.bin");
        snprintf(tf.race_path, sizeof tf.race_path, "%s", occupied);
        snprintf(tf.race_bytes, sizeof tf.race_bytes, "%s", D_OCCUPIED);
        tf.race_on_fclose_after = 0;      /* the first close: the GPU payload's */
        tf.io_fault = 1;
        want = "incomplete:file-exists";
        expect_bytes = 1;                 /* the GPU payload got written whole  */
    } else if (strcmp(which, "sidecarexists") == 0) {
        /* THE FINAL SIDECAR NAME IS ALREADY TAKEN. The payloads are written, the
         * staging sidecar is written whole, and the publication refuses - which
         * is the only step at which the final name is touched at all. */
        FILE *f;
        d_path(occupied, sizeof occupied, DM, ".json");
        f = fopen(occupied, "wb");
        if (f != NULL) {
            (void) fwrite(D_OCCUPIED, 1u, strlen(D_OCCUPIED), f);
            fclose(f);
        }
        CHECK(d_unchanged(occupied, D_OCCUPIED),
              "a sidecar is already at the final name, with known bytes (%ld)",
              d_len(occupied));
        want = "incomplete:sidecar-final-name-exists";
        expect_bytes = 1;
        want_staging = 1;
    } else if (strcmp(which, "sidecarshortwrite") == 0) {
        /* THE FAULT LANDS ON THE SIDECAR AND NOT ON A PAYLOAD. Three payload
         * writes are let through, so the fourth - the staging sidecar's - is the
         * one that is short. The final name must never come into existence. */
        tf.fwrite_short_after = 3;
        tf.io_fault = 1;
        want = "incomplete:short-write";
        expect_bytes = 1;
        want_staging = 1;
        no_final_json = 1;
    } else if (strcmp(which, "sidecarflushfail") == 0) {
        tf.fflush_fail_after = 3;
        tf.io_fault = 1;
        want = "incomplete:flush-failed";
        expect_bytes = 1;
        want_staging = 1;
        no_final_json = 1;
    } else if (strcmp(which, "sidecarclosefail") == 0) {
        /* THE CASE THE PUBLICATION CONTRACT EXISTS FOR. Every byte of the
         * sidecar reaches the staging file and its close then fails. The text
         * says complete: true, because completeness is decided before the
         * sidecar is composed - and the final name is never created, so no
         * qualifying complete record exists. */
        tf.fclose_fail_after = 3;
        tf.io_fault = 1;
        want = "incomplete:close-failed";
        expect_bytes = 1;
        want_staging = 1;
        no_final_json = 1;
    } else if (strcmp(which, "nodir") == 0) {
        /* SL_BG_TF_DUMP_DIR names a directory that does not exist, so the
         * create fails for a real reason the runner arranged. */
        want = "incomplete:file-create-failed";
    } else if (strcmp(which, "allocfail") == 0) {
        tf.malloc_fail_after = 0;
        tf.io_fault = 1;
        want = "refused:allocation-failed";
    } else if (strcmp(which, "shortwrite") == 0) {
        tf.fwrite_short_after = 0;
        tf.io_fault = 1;
        want = "incomplete:short-write";
    } else if (strcmp(which, "flushfail") == 0) {
        tf.fflush_fail_after = 0;
        tf.io_fault = 1;
        want = "incomplete:flush-failed";
    } else if (strcmp(which, "closefail") == 0) {
        tf.fclose_fail_after = 0;
        tf.io_fault = 1;
        want = "incomplete:close-failed";
    } else {
        CHECK(0, "unknown failure scenario '%s'", which);
        end_case();
        return;
    }

    s = d_site(DM, slot, 0.0f, 0.0f);
    CHECK(sl_bgtf_dump_select(&s) == 1, "the draw is selected");
    if (strcmp(which, "nocount") == 0)        sl_bgtf_dump_capture(d_ix, 0u);
    else if (strcmp(which, "overflow") == 0)  sl_bgtf_dump_capture(d_ix,
                                                  SL_BGTF_DUMP_MAX_INDICES + 1u);
    else                                      sl_bgtf_dump_capture(d_ix, DIDX);
    tf.io_fault = 0;

    CHECK(sl_bgtf_dump_state() == -1,
          "the outcome is INCOMPLETE or REFUSED and NOT a measurement (%d)",
          sl_bgtf_dump_state());
    if (want != NULL) {
        CHECK(strcmp(sl_bgtf_dump_status(), want) == 0,
              "and it is named exactly '%s' (got '%s')", want,
              sl_bgtf_dump_status());
    } else {
        /* The occupied-but-unreadable path. Both of these are named create
         * refusals and both leave the file alone; which one a platform reports
         * for an existing file it will not share is the platform's business. */
        const char *got = sl_bgtf_dump_status();
        CHECK(strcmp(got, "incomplete:file-exists") == 0
              || strcmp(got, "incomplete:file-create-failed") == 0,
              "and it is one of the two named create refusals (got '%s')", got);
        want = got;                       /* for the spent-attempt check below */
    }
    CHECK(sl_bgtf_dump_stats()->completed == 0,
          "no record was sealed complete (%lu)", sl_bgtf_dump_stats()->completed);
    CHECK(sl_bgtf_dump_stats()->captures == 1,
          "the one attempt was claimed (%lu)", sl_bgtf_dump_stats()->captures);

    /* THE ATTEMPT IS SPENT. A later matching draw, on a later walk, must not get
     * a second try - which is the whole reason the attempt is claimed before
     * anything is touched. */
    {
        const long rb = tf.get_buffer_sub_data;
        GLuint pool2 = 0u;
        const int slot2 = d_walk(2u, DM, 0.0f, 0.0f, &pool2);
        CHECK(slot2 >= 0, "a later walk filled a pool");
        CHECK(sl_bgtf_bind_clip(slot2) == DM, "and bound it");
        tf.provide_readback = 1;
        tf.getsub_fail = 0;
        tf.getsub_zero = 0;
        tf.getsub_bytes = -1;
        s = d_site(DM, slot2, 0.0f, 0.0f);
        CHECK(sl_bgtf_dump_select(&s) == 0,
              "and a matching draw of it is NOT selected");
        sl_bgtf_dump_capture(d_ix, DIDX);
        CHECK(tf.get_buffer_sub_data == rb, "no further readback (%ld)",
              tf.get_buffer_sub_data - rb);
        CHECK(sl_bgtf_dump_state() == -1,
              "and the outcome is still the first one (%d)", sl_bgtf_dump_state());
        CHECK(strcmp(sl_bgtf_dump_status(), want) == 0,
              "with the first reason, not a later consequence ('%s')",
              sl_bgtf_dump_status());
    }

    /* AN OCCUPIED PATH IS REFUSED WITHOUT A BYTE OF IT CHANGING. Asserted on
     * the bytes and not on the length, because a writer that truncated and
     * rewrote the same number of bytes would pass a length check. The
     * deny-read handle is released first so the file can be read back at all -
     * it was held only to make the path unreadable DURING the capture. */
    if (held != NULL) { fclose(held); held = NULL; }
    if (occupied[0] != '\0') {
        CHECK(d_unchanged(occupied, D_OCCUPIED),
              "THE OCCUPIED PATH IS BYTE FOR BYTE WHAT IT WAS: %s (%ld bytes)",
              occupied, d_len(occupied));
    }

    /* WHAT IS ON DISK. A failure before any byte existed leaves no payload; one
     * after the read leaves the bytes it obtained, with the seal withheld - a
     * partial payload is evidence and a deleted one is nothing. */
    d_path(p, sizeof p, DM, ".gpuclip.bin");
    if (strcmp(which, "nodir") == 0) {
        CHECK(d_len(p) < 0, "nothing was written into a directory that does not exist");
    } else if (strcmp(which, "exists") == 0 || strcmp(which, "unreadable") == 0) {
        CHECK(d_unchanged(p, D_OCCUPIED),
              "the record already on disk was NOT replaced and NOT truncated (%ld "
              "bytes)", d_len(p));
    } else if (strcmp(which, "race") == 0) {
        CHECK(tf.race_fired == 1,
              "the competing writer really did take the next path during the "
              "capture (%ld)", tf.race_fired);
        CHECK(d_len(p) == (long) DNV * 16L,
              "the payload written BEFORE the race was preserved whole (%ld)",
              d_len(p));
    } else if (expect_bytes) {
        CHECK(d_len(p) == (long) DNV * 16L,
              "the bytes the read did obtain were PRESERVED (%ld)", d_len(p));
    } else if (strcmp(which, "shortwrite") == 0 || strcmp(which, "flushfail") == 0
               || strcmp(which, "closefail") == 0) {
        CHECK(d_len(p) >= 0L, "the partial payload was preserved (%ld bytes)",
              d_len(p));
    } else {
        CHECK(d_len(p) < 0, "no payload was written at all");
    }

    /* THE STAGING FILE, WHICH IS EVIDENCE AND NOT A RECORD. Every sidecar
     * failure leaves it exactly where it is; nothing deletes it and nothing
     * rewrites it. */
    if (want_staging) {
        d_path(p, sizeof p, DM, ".json.staging");
        CHECK(d_len(p) > 0L,
              "the composed sidecar was LEFT at its staging name as evidence (%ld "
              "bytes): %s", d_len(p), p);
    }

    /* AND NO QUALIFYING COMPLETE RECORD EXISTS AT THE FINAL NAME. For the three
     * sidecar-write failures the final name must not exist at all: that is the
     * whole of the publication contract. A full, parseable, digest-matching
     * object claiming completeness may well be sitting in the staging file -
     * which is exactly the failed-close case - and it is not a record because it
     * is not at the record's name. */
    {
        static unsigned char js[16384];
        size_t n = 0u;
        d_path(p, sizeof p, DM, ".json");
        if (no_final_json) {
            CHECK(d_len(p) < 0,
                  "NO SIDECAR AT THE FINAL NAME: the write, flush or close failed, so "
                  "publication never happened (%ld)", d_len(p));
            /* AND THE CASE IS NOT VACUOUS: the staging file really does carry a
             * complete-looking object for the close failure, which is the
             * record a reader would have accepted before this contract. */
            if (strcmp(which, "sidecarclosefail") == 0) {
                d_path(p, sizeof p, DM, ".json.staging");
                if (d_read(p, js, sizeof js - 1u, &n)) {
                    js[n < sizeof js ? n : sizeof js - 1u] = 0u;
                    CHECK(strstr((const char *) js, "\"complete\": true") != NULL,
                          "while the UNPUBLISHED staging file does say complete: true - "
                          "so the contract is what withholds the seal, not the text");
                }
            }
        } else if (strcmp(which, "sidecarexists") == 0) {
            CHECK(d_unchanged(p, D_OCCUPIED),
                  "the sidecar already at the final name is untouched, so publication "
                  "refused rather than replaced (%ld bytes)", d_len(p));
        } else if (d_len(p) > 0L && d_read(p, js, sizeof js - 1u, &n)) {
            js[n < sizeof js ? n : sizeof js - 1u] = 0u;
            CHECK(strstr((const char *) js, "\"complete\": true") == NULL,
                  "the sidecar does NOT claim completeness");
            CHECK(strstr((const char *) js, "\"complete\": false") != NULL,
                  "and says so explicitly");
        } else {
            printf("    (no sidecar on disk for '%s', which is itself the refusal)\n",
                   which);
        }
    }
    sl_bgtf_unbind();
    end_case();
}

/* ---- 17f. a malformed or unprovenanced selector does not arm ----------- */

static void d_mode_disarm(const char *why)
{
    begin_case("dump disarm: a selector that cannot be read exactly, or one with no "
               "validated provenance, leaves the diagnostic OFF");
    printf("    scenario: %s\n", why);
    arm();
    tf.provide_readback = 1;
    CHECK(sl_bgtf_dump_opt_in() == 0,
          "the diagnostic did NOT arm - a mistake in the control records nothing "
          "rather than recording the first draw that happens by (%d)",
          sl_bgtf_dump_opt_in());
    {
        int i, mismatch = 0;
        for (i = 0; i < 64; i++) if (sl_bgtf_dump_opt_in() != 0) mismatch++;
        CHECK(mismatch == 0, "and stays off (%d differed)", mismatch);
    }
    CHECK(tf.proc_calls == 0, "nothing was resolved (%ld)", tf.proc_calls);
    CHECK(tf.calls == 0, "no GL call was made (%ld)", tf.calls);
    CHECK(sl_bgtf_dump_state() == 0, "and no attempt was made (%d)",
          sl_bgtf_dump_state());
    {
        const struct sl_bgtf_dump_counters *c = sl_bgtf_dump_stats();
        CHECK(c->offers == 0 && c->captures == 0 && c->readbacks == 0,
              "the census is empty (%lu / %lu / %lu)", c->offers, c->captures,
              c->readbacks);
    }
    d_no_files(SL_BGGPU_DEPTH_FAR1, "disarmed");
    end_case();
}

/* ---- 18. THE STATE-ONLY CONTROL (SL_BG_TF_STATE_ONLY) ------------------
 *
 * WHAT IS BEING MEASURED. The control keeps the whole prepass bracket and omits
 * ONE call: the prepass's glDrawArrays. So the claim is a DIFFERENCE between two
 * launches - identical setup and restoration, one draw against none - and no
 * single process can state it, because the control's switch is parsed once for
 * the launch exactly like the candidate's and the diagnostic's.
 *
 * SO THE SAME SCENARIO IS RUN TWICE AND A DIGEST IS COMPARED. `so ref` runs it
 * with the control ABSENT and `so only` with it SELECTED; each prints an
 * order-sensitive digest over its prepass transcript with the DRAWARRAYS records
 * EXCLUDED, and tools/windows/bgtftest.ps1 requires the two to be EQUAL while
 * the draw counts differ. Excluding the draw is the point: a digest containing it
 * could never show that everything else is the same calls with the same
 * arguments in the same order.
 *
 * EACH ARM ALSO ASSERTS ITS OWN TRANSCRIPT EXACTLY, so the digest is not the only
 * thing standing between a dropped setup call and a pass.
 *
 * AND THE ZERO IS A MEASUREMENT, NOT AN ARRANGEMENT. The reference arm requires
 * exactly one counted draw over nverts vertices, so the other arm's zero is the
 * same observation taken twice rather than a counter that never moves. */

/* The setup transcript's digest, with the prepass draw left out. */
static unsigned long long so_digest(long from, long to)
{
    unsigned long long h = 1469598103934665603ULL;   /* FNV-1a 64 offset basis */
    long i, n = 0;

    for (i = from; i < to && i < tf.nlog; i++) {
        const struct sl_bgtfgl_call *r = &tf.log[i];
        long f[6];
        int k;
        if (r->kind == TFGL_DRAWARRAYS) continue;
        n++;
        f[0] = (long) r->kind;
        f[1] = r->a; f[2] = r->b; f[3] = r->c; f[4] = r->d; f[5] = r->off;
        for (k = 0; k < 6; k++) {
            unsigned long long v = (unsigned long long) f[k];
            int bb;
            for (bb = 0; bb < 8; bb++) {
                h ^= (unsigned long long) ((v >> (bb * 8)) & 0xffu);
                h *= 1099511628211ULL;
            }
        }
    }
    printf("    SOSETUP calls=%ld digest=%016llx\n", n, h);
    return h;
}

/* One expected record, appended. A helper rather than a compound literal so the
 * array can be built with the draw present or absent without two copies of the
 * whole sequence - which is exactly the duplication the module itself avoids. */
static int so_push(struct expect *e, int n, int kind, long a, long b, long c,
                   long d, const char *what)
{
    e[n].kind = kind;
    e[n].a = a; e[n].b = b; e[n].c = c; e[n].d = d;
    e[n].what = what;
    return n + 1;
}

/* THE SAME PUSH, PLUS THE CONTEXT THAT RECORD MUST HAVE LANDED ON, written into
 * a parallel array `want` that seq_ctx_is reads. Used by the one control whose
 * claim is about WHERE a call landed; every other sequence goes through so_push
 * and asks nothing about placement. The two arrays are filled in lockstep by
 * construction, so they cannot drift out of step with each other. */
static int ax_push(struct expect *e, int *want, int n, int kind, long a,
                   long b, long c, long d, int ctx, const char *what)
{
    n = so_push(e, n, kind, a, b, c, d, what);
    want[n - 1] = ctx;
    return n;
}

static void so_scenario(int state_only)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo, pool = 0u;
    long mark, after, calls_before;
    int s, s2, sf0, sf0b;

    begin_case(state_only
               ? "state-only: the WHOLE prepass bracket runs and ONLY its vertex draw "
                 "is omitted, nothing is transformed, no pool is written and an "
                 "unwritten slot cannot be bound"
               : "state-only REFERENCE: the same scenario with the control absent - one "
                 "counted draw over nverts vertices, and the pool binds");
    arm();
    CHECK(sl_bgtf_state_only() == state_only,
          "the control answers %d", state_only);
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    mark = tf.nlog;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    after = tf.nlog;
    CHECK(s >= 0 && s < SLOTS, "a slot was taken (%d)", s);

    /* The pool, read out of the submission: the name bound at
     * transform-feedback index 0. It is bound on BOTH arms - keeping that
     * binding is part of the setup the control preserves - so this is as much a
     * check of the state-only arm as of the reference. */
    {
        const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
        CHECK(base != NULL, "a recording buffer was bound at index 0");
        pool = base != NULL ? (GLuint) base->c : 0u;
        CHECK(pool != 0u && pool != vbo,
              "and it is a buffer of the module's own, not the input (%u vs %u)",
              (unsigned) pool, (unsigned) vbo);
    }

    /* THE WHOLE SUBMISSION, IN ORDER, ON THIS ARM. The sequence is built once
     * and the draw record is inserted only when the control is off, so the two
     * expectations cannot drift apart in anything but that one call. */
    {
        struct expect e[24];
        int n = 0;
        n = so_push(e, n, TFGL_GENBUFFERS, 1, ANY, ANY, ANY, "the pool's name");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) pool, ANY, ANY,
                    "bind to size it");
        n = so_push(e, n, TFGL_BUFFERDATA, GL_ARRAY_BUFFER, (long) NV * 16L,
                    GL_STATIC_DRAW, 0, "four floats per vertex, no initial data");
        n = so_push(e, n, TFGL_GETBUFPARAM, GL_ARRAY_BUFFER, GL_BUFFER_SIZE,
                    (long) NV * 16L, 0,
                    "and the driver is asked whether it honoured the size");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                    "and unbind it again");
        n = so_push(e, n, TFGL_USEPROGRAM, ANY, ANY, ANY, ANY, "the prepass program");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_dmode"), ANY, ANY, ANY,
                    "the depth case");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk1"), ANY, ANY, ANY,
                    "the first coefficient");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk2"), ANY, ANY, ANY,
                    "the second coefficient");
        n = so_push(e, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                    "no fragment may be produced");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY, ANY,
                    "the pass's own static vertex buffer");
        n = so_push(e, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR, (long) vbo,
                    "three floats at the item's stride, out of that buffer");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                    "unbound straight away");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
                    "no stale element buffer across the bracket");
        n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                    (long) pool, ANY, "the pool records at index 0");
        n = so_push(e, n, TFGL_BEGINTF, GL_POINTS, ANY, ANY, ANY,
                    "the scope opens on BOTH arms");
        if (!state_only)
            n = so_push(e, n, TFGL_DRAWARRAYS, GL_POINTS, 0, (long) NV, (long) pool,
                        "THE ONE CALL THE CONTROL OMITS");
        n = so_push(e, n, TFGL_ENDTF, ANY, ANY, ANY, ANY, "the scope closes");
        n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0,
                    ANY, "the indexed binding is released");
        n = so_push(e, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                    "discard goes off");
        n = so_push(e, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY,
                    "and the fixed pipeline comes back");
        seq_is(mark, e, n);
    }

    /* ZERO DRAWS, OR EXACTLY ONE. The reference arm's requirement is what makes
     * the other arm's zero a reading of the same counter rather than a counter
     * that does not work. */
    CHECK(tf.draw_arrays == (state_only ? 0L : 1L),
          "glDrawArrays calls: %ld (expected %d)", tf.draw_arrays,
          state_only ? 0 : 1);
    CHECK(tf.draw_vertices == (state_only ? 0L : (long) NV),
          "vertices asked for by a draw: %ld (expected %ld)", tf.draw_vertices,
          state_only ? 0L : (long) NV);

    /* THE BRACKET AND THE RESTORATION ARE THE SAME ON BOTH ARMS. These are
     * stated as counts and as modelled state, beside the ordered transcript, so
     * a setup call that vanished is caught by more than the digest. */
    CHECK(tf.begin_tf == 1 && tf.end_tf == 1,
          "the transform-feedback scope opened and closed exactly once (%ld/%ld)",
          tf.begin_tf, tf.end_tf);
    CHECK(tf.bind_buffer_base == 2,
          "the indexed binding was made and released (%ld)", tf.bind_buffer_base);
    CHECK(tf.enables == 1 && tf.disables == 1,
          "rasterizer discard was enabled and disabled once (%ld/%ld)",
          tf.enables, tf.disables);
    CHECK(tf.gen_buffers == 1 && tf.buffer_data == 1
          && tf.get_buffer_parameteriv == 1,
          "the pool was created, sized and VERIFIED (%ld/%ld/%ld)",
          tf.gen_buffers, tf.buffer_data, tf.get_buffer_parameteriv);
    CHECK(sl_bgtfgl_buffer_bytes(pool) == (long) NV * 16L,
          "and sized nverts * 4 floats (%ld, expected %ld)",
          sl_bgtfgl_buffer_bytes(pool), (long) NV * 16L);
    CHECK(tf.vptr.set && tf.vptr.size == 3 && tf.vptr.stride == (GLsizei) STR
          && tf.vptr.buf == vbo && tf.vptr.off == 0,
          "the input position pointer named the pass's own buffer (set %d, size %d, "
          "stride %d, buf %u, off %ld)", tf.vptr.set, (int) tf.vptr.size,
          (int) tf.vptr.stride, (unsigned) tf.vptr.buf, tf.vptr.off);
    CHECK(tf.discard == 0, "discard is off afterwards (%d)", tf.discard);
    CHECK(tf.current == 0, "no program is current (%u)", (unsigned) tf.current);
    CHECK(tf.tf_buf == 0u, "nothing is left recording (%u)", (unsigned) tf.tf_buf);
    CHECK(tf.arr_buf == 0u, "and no array buffer is left bound (%u)",
          (unsigned) tf.arr_buf);
    CHECK(tf.elem_buf == 0u, "with no element buffer bound (%u)",
          (unsigned) tf.elem_buf);

    /* THE WORK COUNTERS. With no vertex draw there is no transformed vertex and
     * no submitted prepass, so the control's own bounded counts carry the
     * cadence and the candidate's carry nothing. */
    a = snap();
    if (state_only) {
        CHECK(a.prepasses == b.prepasses,
              "NO prepass was counted (%lu)", a.prepasses - b.prepasses);
        CHECK(a.vertices == b.vertices,
              "and NO vertex was counted as transformed (%lu)",
              a.vertices - b.vertices);
        CHECK(a.walks == b.walks,
              "and no walk was credited with a submission (%lu)",
              a.walks - b.walks);
        CHECK(a.so_setups - b.so_setups == 1,
              "one setup was counted (%lu)", a.so_setups - b.so_setups);
        CHECK(a.so_omitted - b.so_omitted == 1,
              "and one omitted draw with it (%lu)", a.so_omitted - b.so_omitted);
        CHECK(a.so_omitted - b.so_omitted == a.so_setups - b.so_setups,
              "the two agree, because the omission IS the dropped call (%lu vs %lu)",
              a.so_omitted - b.so_omitted, a.so_setups - b.so_setups);
    } else {
        CHECK(a.prepasses - b.prepasses == 1, "one prepass was counted (%lu)",
              a.prepasses - b.prepasses);
        CHECK(a.vertices - b.vertices == (unsigned long) NV,
              "and nverts vertices with it (%lu)", a.vertices - b.vertices);
        CHECK(a.walks - b.walks == 1, "and one walk (%lu)", a.walks - b.walks);
        CHECK(a.so_setups == b.so_setups && a.so_omitted == b.so_omitted
              && a.so_reuses == b.so_reuses,
              "and NOTHING was counted for the state-only control (%lu/%lu/%lu)",
              a.so_setups - b.so_setups, a.so_omitted - b.so_omitted,
              a.so_reuses - b.so_reuses);
    }
    CHECK(a.refusals == b.refusals, "with no refusal (%lu)",
          a.refusals - b.refusals);
    CHECK(a.slot_refills - b.slot_refills == 1,
          "and the slot it took was a refill, not pressure (%lu)",
          a.slot_refills - b.slot_refills);
    CHECK(a.slot_evictions == b.slot_evictions, "nothing was evicted (%lu)",
          a.slot_evictions - b.slot_evictions);

    /* THE DIGEST OF THE SETUP, which is what the runner compares across the two
     * launches. Printed from the window this scenario just drove. */
    (void) so_digest(mark, after);

    /* THE BINDING. An unwritten pool must be unbindable even when a caller
     * offers it the slot the prepass just returned, and a written one must still
     * bind - the second half is what keeps the first from being a module that
     * simply stopped binding. */
    b = snap();
    calls_before = tf.calls;
    if (state_only) {
        CHECK(sl_bgtf_bind_clip(s) == -1,
              "the setup-only slot is REFUSED by the binding");
        CHECK(snap().binds == b.binds,
              "no bind was counted (%lu)", snap().binds - b.binds);
        CHECK(tf.calls == calls_before,
              "and the refusal cost no GL call (%ld -> %ld)", calls_before,
              tf.calls);
        CHECK(tf.vptr.size == 3 && tf.vptr.buf == vbo,
              "the position pointer still names the INPUT, untouched by the "
              "refusal (size %d, buf %u)", (int) tf.vptr.size,
              (unsigned) tf.vptr.buf);
    } else {
        CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_SINGLE,
              "the written pool binds and reports its depth");
        CHECK(snap().binds - b.binds == 1, "and one bind was counted (%lu)",
              snap().binds - b.binds);
        CHECK(tf.vptr.size == 4 && tf.vptr.buf == pool,
              "with the position array on the pool at four components "
              "(size %d, buf %u)", (int) tf.vptr.size, (unsigned) tf.vptr.buf);
    }

    /* THE CADENCE IS THE KEY'S, NOT THE MESH'S. A second draw at the same key
     * costs no setup and no GL call at all on either arm, which is what keeps
     * the two arms' setup counts comparable. */
    b = snap();
    calls_before = tf.calls;
    s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    CHECK(s2 == s, "the same key is served by the same slot (%d vs %d)", s2, s);
    CHECK(tf.calls == calls_before,
          "a reuse costs no GL call whatever (%ld -> %ld)", calls_before, tf.calls);
    if (state_only) {
        CHECK(a.so_reuses - b.so_reuses == 1,
              "and is counted as a setup-only reuse (%lu)",
              a.so_reuses - b.so_reuses);
        CHECK(a.reuses == b.reuses,
              "never as a POOL reuse, because there is no pool (%lu)",
              a.reuses - b.reuses);
        CHECK(a.so_setups == b.so_setups,
              "and no second setup ran (%lu)", a.so_setups - b.so_setups);
    } else {
        CHECK(a.reuses - b.reuses == 1, "and is counted as a pool reuse (%lu)",
              a.reuses - b.reuses);
        CHECK(a.so_reuses == b.so_reuses,
              "with nothing for the state-only control (%lu)",
              a.so_reuses - b.so_reuses);
        CHECK(a.prepasses == b.prepasses, "and no second prepass ran (%lu)",
              a.prepasses - b.prepasses);
    }

    /* FAR0'S TWO PAIRS STILL GET TWO SLOTS. The key's depth and coefficient
     * facts are the ones a comparison between the arms depends on, so the
     * control must not collapse them: the real opaque and blend pairs are
     * different keys and each pays its own setup, and the first pair then
     * reuses. */
    b = snap();
    sf0 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    sf0b = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, T1, T2);
    a = snap();
    CHECK(sf0 >= 0 && sf0b >= 0 && sf0 != sf0b && sf0 != s && sf0b != s,
          "the two FAR0 pairs took two slots of their own (%d, %d, single %d)",
          sf0, sf0b, s);
    if (state_only) {
        CHECK(a.so_setups - b.so_setups == 2,
              "each paid its own setup (%lu)", a.so_setups - b.so_setups);
        CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices,
              "and neither transformed anything (%lu/%lu)",
              a.prepasses - b.prepasses, a.vertices - b.vertices);
        CHECK(sl_bgtf_bind_clip(sf0) == -1 && sl_bgtf_bind_clip(sf0b) == -1,
              "and neither can be bound");
    } else {
        CHECK(a.prepasses - b.prepasses == 2,
              "each ran its own prepass (%lu)", a.prepasses - b.prepasses);
        CHECK(sl_bgtf_bind_clip(sf0) == SL_BGGPU_DEPTH_FAR0,
              "and the first binds as FAR0");
    }
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == sf0,
          "the first pair is then reused rather than recomputed");
    a = snap();
    CHECK((state_only ? a.so_reuses - b.so_reuses : a.reuses - b.reuses) == 1,
          "and the reuse is counted on this arm's own number");

    sl_bgtf_unbind();
    CHECK(tf.discard == 0 && tf.current == 0 && tf.tf_buf == 0u
          && tf.arr_buf == 0u && tf.elem_buf == 0u,
          "the unbind leaves no state disturbed (discard %d, program %u, tf %u, "
          "array %u, element %u)", tf.discard, (unsigned) tf.current,
          (unsigned) tf.tf_buf, (unsigned) tf.arr_buf, (unsigned) tf.elem_buf);
    end_case();
}

/* A PREPASS THAT REFUSES, UNDER WHICHEVER ARM. The allocation verification is
 * part of the setup the control keeps, so a store the fake driver leaves empty
 * must still refuse the draw, leave the slot invalid and claim nothing - and the
 * invalid slot must be unbindable for the same reason a setup-only one is. */
static void so_refusal(int state_only)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s;

    begin_case(state_only
               ? "state-only: an allocation the driver leaves EMPTY still refuses, and "
                 "the invalid slot cannot be bound"
               : "state-only REFERENCE: the same allocation refusal, unchanged by the "
                 "control being absent");
    arm();
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    tf.alloc_short_initial = 1;
    tf.alloc_bytes = 0L;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    tf.alloc_short_initial = 0;
    a = snap();
    CHECK(s == -1, "the draw is refused (%d)", s);
    CHECK(a.refusals - b.refusals == 1, "and the refusal is counted (%lu)",
          a.refusals - b.refusals);
    CHECK(sl_bgtf_state() == -1, "the capability latched UNAVAILABLE (%d)",
          sl_bgtf_state());
    CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices,
          "nothing was counted as a prepass or a transformed vertex (%lu/%lu)",
          a.prepasses - b.prepasses, a.vertices - b.vertices);
    CHECK(a.so_setups == b.so_setups && a.so_omitted == b.so_omitted,
          "and nothing as a setup or an omitted draw either (%lu/%lu)",
          a.so_setups - b.so_setups, a.so_omitted - b.so_omitted);
    CHECK(tf.draw_arrays == 0, "no draw ran (%ld)", tf.draw_arrays);
    {
        int i;
        for (i = 0; i < SLOTS; i++)
            CHECK(sl_bgtf_bind_clip(i) == -1,
                  "slot %d cannot be bound after the refusal", i);
    }
    CHECK(snap().binds == a.binds, "and no bind was counted (%lu)",
          snap().binds - a.binds);
    end_case();
}

static void mode_so(int state_only)
{
    so_scenario(state_only);
    so_refusal(state_only);
}

/* ---- 18b. THE ZERO-VERTEX-COUNT CONTROL (SL_BG_TF_ZERO_COUNT) -----------
 *
 * WHAT IS BEING MEASURED, AND HOW IT DIFFERS FROM THE CONTROL ABOVE. The
 * state-only control OMITS the prepass draw; this one MAKES it with the count 0.
 * So the claim is again a DIFFERENCE between two launches - but the difference
 * is one ARGUMENT of one call rather than the presence of the call, and that is
 * the thing a reader of either arm's numbers could otherwise not tell apart.
 *
 * SO THE SAME SCENARIO IS RUN TWICE AND A DIGEST IS COMPARED, as the arm above
 * does and for the same reason: the switch is parsed once for the launch. `zc
 * ref` runs it with the control ABSENT and `zc only` with it SELECTED.
 *
 * AND THE DIGEST HERE INCLUDES THE DRAW, WITH THE COUNT MASKED, which is a
 * STRONGER statement than the state-only digest's exclusion of it. Excluding the
 * record would leave the draw's mode, its first vertex and the pool it recorded
 * into unexamined across the two arms - exactly the fields this control must not
 * move. Masking only the count means the two digests are equal if and only if
 * every record including the draw matches in every other field, in order, so
 * "ONLY the count changed" is what the equality says.
 *
 * AND THE EMPTY CALL IS A MEASUREMENT RATHER THAN AN ABSENCE. The reference arm
 * requires one draw over nverts vertices; this arm requires one draw over ZERO
 * vertices. The call count is 1 on BOTH, which is what distinguishes this arm
 * from the state-only arm's 0 and is why a reader cannot mistake an issued empty
 * call for an omitted one. */

/* The transcript's digest with the prepass draw's COUNT masked and every other
 * field of every record - the draw's included - hashed. */
#define ZC_COUNT_MASKED (-424242L)

static unsigned long long zc_digest(long from, long to)
{
    unsigned long long h = 1469598103934665603ULL;   /* FNV-1a 64 offset basis */
    long i, n = 0, draws = 0;

    for (i = from; i < to && i < tf.nlog; i++) {
        const struct sl_bgtfgl_call *r = &tf.log[i];
        long f[6];
        int k;
        n++;
        f[0] = (long) r->kind;
        f[1] = r->a; f[2] = r->b; f[3] = r->c; f[4] = r->d; f[5] = r->off;
        if (r->kind == TFGL_DRAWARRAYS) { f[3] = ZC_COUNT_MASKED; draws++; }
        for (k = 0; k < 6; k++) {
            unsigned long long v = (unsigned long long) f[k];
            int bb;
            for (bb = 0; bb < 8; bb++) {
                h ^= (unsigned long long) ((v >> (bb * 8)) & 0xffu);
                h *= 1099511628211ULL;
            }
        }
    }
    printf("    ZCSETUP calls=%ld draws=%ld digest=%016llx\n", n, draws, h);
    printf("    ZCDRAW arrays=%ld vertices=%ld\n", tf.draw_arrays,
           tf.draw_vertices);
    return h;
}

static void zc_scenario(int zero_count)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo, pool = 0u;
    long mark, after, calls_before;
    int s, s2, sf0, sf0b;

    begin_case(zero_count
               ? "zero count: the WHOLE prepass bracket runs, the draw is ISSUED with "
                 "the count 0, no vertex is submitted, no pool is written and an "
                 "unwritten slot cannot be bound"
               : "zero count REFERENCE: the same scenario with the control absent - the "
                 "same call over nverts vertices, and the pool binds");
    arm();
    CHECK(sl_bgtf_zero_count() == zero_count,
          "the control answers %d", zero_count);
    CHECK(sl_bgtf_state_only() == 0,
          "and the state-only control is NOT in force, so the call is made rather "
          "than omitted (%d)", sl_bgtf_state_only());
    CHECK(sl_bgtf_pos_input() == 0 && sl_bgtf_private_vao() == 0,
          "and neither exclusive arm is either (%d / %d)", sl_bgtf_pos_input(),
          sl_bgtf_private_vao());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    mark = tf.nlog;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    after = tf.nlog;
    CHECK(s >= 0 && s < SLOTS, "a slot was taken (%d)", s);

    {
        const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
        CHECK(base != NULL, "a recording buffer was bound at index 0");
        pool = base != NULL ? (GLuint) base->c : 0u;
        CHECK(pool != 0u && pool != vbo,
              "and it is a buffer of the module's own, not the input (%u vs %u)",
              (unsigned) pool, (unsigned) vbo);
    }

    /* THE WHOLE SUBMISSION, IN ORDER, ON THIS ARM. One sequence, built once,
     * with the draw's COUNT the only field that differs between the arms - so
     * the two expectations cannot drift apart in anything else. */
    {
        struct expect e[24];
        int n = 0;
        n = so_push(e, n, TFGL_GENBUFFERS, 1, ANY, ANY, ANY, "the pool's name");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) pool, ANY, ANY,
                    "bind to size it");
        n = so_push(e, n, TFGL_BUFFERDATA, GL_ARRAY_BUFFER, (long) NV * 16L,
                    GL_STATIC_DRAW, 0, "four floats per vertex, no initial data");
        n = so_push(e, n, TFGL_GETBUFPARAM, GL_ARRAY_BUFFER, GL_BUFFER_SIZE,
                    (long) NV * 16L, 0,
                    "and the driver is asked whether it honoured the size");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                    "and unbind it again");
        n = so_push(e, n, TFGL_USEPROGRAM, ANY, ANY, ANY, ANY, "the prepass program");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_dmode"), ANY, ANY, ANY,
                    "the depth case");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk1"), ANY, ANY, ANY,
                    "the first coefficient");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk2"), ANY, ANY, ANY,
                    "the second coefficient");
        n = so_push(e, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                    "no fragment may be produced");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY, ANY,
                    "the pass's own static vertex buffer");
        n = so_push(e, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR, (long) vbo,
                    "three floats at the item's stride, out of that buffer");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                    "unbound straight away");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
                    "no stale element buffer across the bracket");
        n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                    (long) pool, ANY, "the pool records at index 0");
        n = so_push(e, n, TFGL_BEGINTF, GL_POINTS, ANY, ANY, ANY,
                    "the scope opens on BOTH arms");
        /* THE CALL IS PRESENT ON BOTH ARMS. Its mode and first vertex are the
         * same, the pool it records into is the same, and the COUNT is the one
         * field this control changes. */
        n = so_push(e, n, TFGL_DRAWARRAYS, GL_POINTS, 0,
                    zero_count ? 0L : (long) NV, (long) pool,
                    "THE ONE ARGUMENT THE CONTROL REPLACES");
        n = so_push(e, n, TFGL_ENDTF, ANY, ANY, ANY, ANY, "the scope closes");
        n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0,
                    ANY, "the indexed binding is released");
        n = so_push(e, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                    "discard goes off");
        n = so_push(e, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY,
                    "and the fixed pipeline comes back");
        seq_is(mark, e, n);
    }

    /* EXACTLY ONE CALL ON BOTH ARMS, OVER ZERO VERTICES OR OVER NVERTS. The
     * first number is what separates this arm from the state-only arm's
     * omission; the second is what separates it from the reference. */
    CHECK(tf.draw_arrays == 1L,
          "the prepass draw was ISSUED exactly once, on either arm (%ld)",
          tf.draw_arrays);
    CHECK(tf.draw_vertices == (zero_count ? 0L : (long) NV),
          "vertices asked for by it: %ld (expected %ld)", tf.draw_vertices,
          zero_count ? 0L : (long) NV);

    /* THE BRACKET AND THE RESTORATION ARE THE SAME ON BOTH ARMS, stated as
     * counts and as modelled state beside the ordered transcript. */
    CHECK(tf.begin_tf == 1 && tf.end_tf == 1,
          "the transform-feedback scope opened and closed exactly once (%ld/%ld)",
          tf.begin_tf, tf.end_tf);
    CHECK(tf.bind_buffer_base == 2,
          "the indexed binding was made and released (%ld)", tf.bind_buffer_base);
    CHECK(tf.enables == 1 && tf.disables == 1,
          "rasterizer discard was enabled and disabled once (%ld/%ld)",
          tf.enables, tf.disables);
    CHECK(tf.gen_buffers == 1 && tf.buffer_data == 1
          && tf.get_buffer_parameteriv == 1,
          "the pool was created, sized and VERIFIED (%ld/%ld/%ld)",
          tf.gen_buffers, tf.buffer_data, tf.get_buffer_parameteriv);
    CHECK(sl_bgtfgl_buffer_bytes(pool) == (long) NV * 16L,
          "and sized nverts * 4 floats, on both arms (%ld, expected %ld)",
          sl_bgtfgl_buffer_bytes(pool), (long) NV * 16L);
    CHECK(tf.vptr.set && tf.vptr.size == 3 && tf.vptr.stride == (GLsizei) STR
          && tf.vptr.buf == vbo && tf.vptr.off == 0,
          "the input position pointer named the pass's own buffer at its own "
          "stride (set %d, size %d, stride %d, buf %u, off %ld)", tf.vptr.set,
          (int) tf.vptr.size, (int) tf.vptr.stride, (unsigned) tf.vptr.buf,
          tf.vptr.off);
    CHECK(tf.discard == 0, "discard is off afterwards (%d)", tf.discard);
    CHECK(tf.current == 0, "no program is current (%u)", (unsigned) tf.current);
    CHECK(tf.tf_buf == 0u, "nothing is left recording (%u)", (unsigned) tf.tf_buf);
    CHECK(tf.arr_buf == 0u, "and no array buffer is left bound (%u)",
          (unsigned) tf.arr_buf);
    CHECK(tf.elem_buf == 0u, "with no element buffer bound (%u)",
          (unsigned) tf.elem_buf);

    /* THE WORK COUNTERS. A call that submitted nothing transformed nothing, so
     * the candidate's own numbers stay put and the control's carry the cadence. */
    a = snap();
    if (zero_count) {
        CHECK(a.prepasses == b.prepasses,
              "NO prepass was counted (%lu)", a.prepasses - b.prepasses);
        CHECK(a.vertices == b.vertices,
              "and NO vertex was counted as transformed (%lu)",
              a.vertices - b.vertices);
        CHECK(a.walks == b.walks,
              "and no walk was credited with a submission (%lu)",
              a.walks - b.walks);
        CHECK(a.zc_setups - b.zc_setups == 1,
              "one setup bracket was counted (%lu)", a.zc_setups - b.zc_setups);
        CHECK(a.zc_calls - b.zc_calls == 1,
              "and one ISSUED zero-count call with it (%lu)",
              a.zc_calls - b.zc_calls);
        CHECK(a.zc_calls - b.zc_calls == a.zc_setups - b.zc_setups,
              "the two agree, because one bracket issues exactly one call "
              "(%lu vs %lu)", a.zc_calls - b.zc_calls,
              a.zc_setups - b.zc_setups);
        CHECK(a.zc_refusals == b.zc_refusals,
              "and the control was not refused (%lu)",
              a.zc_refusals - b.zc_refusals);
        CHECK(a.so_setups == b.so_setups && a.so_omitted == b.so_omitted,
              "nothing was counted for the state-only control, whose omission is "
              "the OTHER arm (%lu/%lu)", a.so_setups - b.so_setups,
              a.so_omitted - b.so_omitted);
    } else {
        CHECK(a.prepasses - b.prepasses == 1, "one prepass was counted (%lu)",
              a.prepasses - b.prepasses);
        CHECK(a.vertices - b.vertices == (unsigned long) NV,
              "and nverts vertices with it (%lu)", a.vertices - b.vertices);
        CHECK(a.walks - b.walks == 1, "and one walk (%lu)", a.walks - b.walks);
        CHECK(a.zc_setups == b.zc_setups && a.zc_calls == b.zc_calls
              && a.zc_reuses == b.zc_reuses && a.zc_refusals == b.zc_refusals,
              "and NOTHING was counted for the zero-count control (%lu/%lu/%lu/%lu)",
              a.zc_setups - b.zc_setups, a.zc_calls - b.zc_calls,
              a.zc_reuses - b.zc_reuses, a.zc_refusals - b.zc_refusals);
    }
    CHECK(a.refusals == b.refusals, "with no refusal (%lu)",
          a.refusals - b.refusals);
    CHECK(a.slot_refills - b.slot_refills == 1,
          "and the slot it took was a refill, not pressure (%lu)",
          a.slot_refills - b.slot_refills);
    CHECK(a.slot_evictions == b.slot_evictions, "nothing was evicted (%lu)",
          a.slot_evictions - b.slot_evictions);

    /* THE DIGEST, which is what the runner compares across the two launches. */
    (void) zc_digest(mark, after);

    /* THE BINDING. A slot keyed by an empty call must be unbindable even when a
     * caller offers it the index the prepass just returned - the direct
     * sl_bgtf_bind_clip call is exactly that offer - and the reference arm's
     * pool must still bind, which is what keeps the first half from being a
     * module that simply stopped binding. */
    b = snap();
    calls_before = tf.calls;
    if (zero_count) {
        CHECK(sl_bgtf_bind_clip(s) == -1,
              "the slot the zero-count call keyed is REFUSED by the binding");
        CHECK(snap().binds == b.binds,
              "no bind was counted (%lu)", snap().binds - b.binds);
        CHECK(tf.calls == calls_before,
              "and the refusal cost no GL call (%ld -> %ld)", calls_before,
              tf.calls);
        CHECK(tf.vptr.size == 3 && tf.vptr.buf == vbo,
              "the position pointer still names the INPUT, untouched by the "
              "refusal (size %d, buf %u)", (int) tf.vptr.size,
              (unsigned) tf.vptr.buf);
    } else {
        CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_SINGLE,
              "the written pool binds and reports its depth");
        CHECK(snap().binds - b.binds == 1, "and one bind was counted (%lu)",
              snap().binds - b.binds);
        CHECK(tf.vptr.size == 4 && tf.vptr.buf == pool,
              "with the position array on the pool at four components "
              "(size %d, buf %u)", (int) tf.vptr.size, (unsigned) tf.vptr.buf);
    }

    /* THE CADENCE IS THE KEY'S, NOT THE MESH'S, and a HIT ADDS NO CALL. */
    b = snap();
    calls_before = tf.calls;
    s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s2 == s, "the same key is served by the same slot (%d vs %d)", s2, s);
    CHECK(tf.calls == calls_before,
          "a reuse costs no GL call whatever (%ld -> %ld)", calls_before,
          tf.calls);
    CHECK(tf.draw_arrays == 1L,
          "and NO second draw was issued for it (%ld)", tf.draw_arrays);
    a = snap();
    if (zero_count) {
        CHECK(a.zc_reuses - b.zc_reuses == 1,
              "the hit is counted as the control's own cache reuse (%lu)",
              a.zc_reuses - b.zc_reuses);
        CHECK(a.reuses == b.reuses,
              "never as a POOL reuse, because there is no pool (%lu)",
              a.reuses - b.reuses);
        CHECK(a.zc_setups == b.zc_setups && a.zc_calls == b.zc_calls,
              "and no second setup or call (%lu/%lu)",
              a.zc_setups - b.zc_setups, a.zc_calls - b.zc_calls);
    } else {
        CHECK(a.reuses - b.reuses == 1, "and is counted as a pool reuse (%lu)",
              a.reuses - b.reuses);
        CHECK(a.zc_reuses == b.zc_reuses,
              "with nothing for the zero-count control (%lu)",
              a.zc_reuses - b.zc_reuses);
        CHECK(a.prepasses == b.prepasses, "and no second prepass ran (%lu)",
              a.prepasses - b.prepasses);
    }

    /* FAR0'S TWO PAIRS STILL GET TWO SLOTS, so the depth cadence a comparison
     * between the arms rests on is not collapsed by the control. */
    b = snap();
    sf0 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    sf0b = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, T1, T2);
    a = snap();
    CHECK(sf0 >= 0 && sf0b >= 0 && sf0 != sf0b && sf0 != s && sf0b != s,
          "the two FAR0 pairs took two slots of their own (%d, %d, single %d)",
          sf0, sf0b, s);
    if (zero_count) {
        CHECK(a.zc_setups - b.zc_setups == 2 && a.zc_calls - b.zc_calls == 2,
              "each paid its own setup and issued its own empty call (%lu/%lu)",
              a.zc_setups - b.zc_setups, a.zc_calls - b.zc_calls);
        CHECK(tf.draw_vertices == 0L,
              "and the vertices asked for over the whole run are still ZERO (%ld)",
              tf.draw_vertices);
        CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices,
              "with nothing transformed (%lu/%lu)", a.prepasses - b.prepasses,
              a.vertices - b.vertices);
        CHECK(sl_bgtf_bind_clip(sf0) == -1 && sl_bgtf_bind_clip(sf0b) == -1,
              "and neither can be bound");
    } else {
        CHECK(a.prepasses - b.prepasses == 2,
              "each ran its own prepass (%lu)", a.prepasses - b.prepasses);
        CHECK(sl_bgtf_bind_clip(sf0) == SL_BGGPU_DEPTH_FAR0,
              "and the first binds as FAR0");
    }
    b = snap();
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == sf0,
          "the first pair is then reused rather than recomputed");
    a = snap();
    CHECK((zero_count ? a.zc_reuses - b.zc_reuses : a.reuses - b.reuses) == 1,
          "and the reuse is counted on this arm's own number");

    sl_bgtf_unbind();
    CHECK(tf.discard == 0 && tf.current == 0 && tf.tf_buf == 0u
          && tf.arr_buf == 0u && tf.elem_buf == 0u,
          "the unbind leaves no state disturbed (discard %d, program %u, tf %u, "
          "array %u, element %u)", tf.discard, (unsigned) tf.current,
          (unsigned) tf.tf_buf, (unsigned) tf.arr_buf, (unsigned) tf.elem_buf);
    end_case();
}

/* A PREPASS THAT REFUSES, UNDER WHICHEVER ARM. The allocation verification is
 * part of the setup this control keeps, so a store the fake driver leaves empty
 * must still refuse the draw, leave the slot invalid, issue NO call and claim
 * nothing - and the invalid slot must be unbindable. */
static void zc_refusal(int zero_count)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s;

    begin_case(zero_count
               ? "zero count: an allocation the driver leaves EMPTY still refuses "
                 "BEFORE the call, and the invalid slot cannot be bound"
               : "zero count REFERENCE: the same allocation refusal, unchanged by the "
                 "control being absent");
    arm();
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    tf.alloc_short_initial = 1;
    tf.alloc_bytes = 0L;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    tf.alloc_short_initial = 0;
    a = snap();
    CHECK(s == -1, "the draw is refused (%d)", s);
    CHECK(a.refusals - b.refusals == 1, "and the refusal is counted (%lu)",
          a.refusals - b.refusals);
    CHECK(sl_bgtf_state() == -1, "the capability latched UNAVAILABLE (%d)",
          sl_bgtf_state());
    CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices,
          "nothing was counted as a prepass or a transformed vertex (%lu/%lu)",
          a.prepasses - b.prepasses, a.vertices - b.vertices);
    CHECK(a.zc_setups == b.zc_setups && a.zc_calls == b.zc_calls
          && a.zc_reuses == b.zc_reuses,
          "and nothing as a setup, a call or a reuse for this control either "
          "(%lu/%lu/%lu)", a.zc_setups - b.zc_setups, a.zc_calls - b.zc_calls,
          a.zc_reuses - b.zc_reuses);
    CHECK(tf.draw_arrays == 0,
          "NO call was issued at all - the storage is verified before the "
          "bracket (%ld)", tf.draw_arrays);
    {
        int i;
        for (i = 0; i < SLOTS; i++)
            CHECK(sl_bgtf_bind_clip(i) == -1,
                  "slot %d cannot be bound after the refusal", i);
    }
    CHECK(snap().binds == a.binds, "and no bind was counted (%lu)",
          snap().binds - a.binds);
    end_case();
}

static void mode_zc(int zero_count)
{
    zc_scenario(zero_count);
    zc_refusal(zero_count);
}

/* ---- 18c. THE EMPTY-DRAW STATE MODE (SL_BG_TF_EMPTY_STATE) ---------------
 *
 * WHAT IS BEING MEASURED. The zero-count arm issues the prepass glDrawArrays
 * with the count 0 and reproduces the pixel difference. Three conditions hold at
 * that call - transform feedback ACTIVE, rasterizer discard ON, the prepass
 * program CURRENT - and this mode's rows vary them. So what each case below has
 * to establish is which of the three held AT THE CALL, and that nothing else
 * about the bracket moved.
 *
 * AT THE CALL, NOT AFTER IT, AND THAT IS THE FIRST DIFFICULTY. Every row leaves
 * the common bracket's state to be restored around it, so the rows are identical
 * by the time the bracket closes: a test reading discard, the current program or
 * the feedback scope after the call would find them indistinguishable and would
 * pass on all of them whatever the draw site did. The synthetic GL therefore
 * samples the three inside glDrawArrays (st_at_draw_*) and keeps per-run totals
 * beside the sample, so "every draw agreed" is a measurement rather than an
 * inference from the last one.
 *
 * AND THE CONDITIONS AT THE CALL ARE NOT ENOUGH EITHER, which is the second.
 * Rows A, R and Q all draw with the prepass program current and differ only in
 * WHERE the program switches sit, and E and S differ only in whether the rebind
 * after the draw happens at all. No sample taken at the draw can separate those,
 * so the case below asserts the WHOLE ORDERED TRANSCRIPT with exact program
 * names, and then asserts that it matches exactly one row's expectation and no
 * other's.
 *
 * ONE ROW PER LAUNCH, because the selector is parsed once for the process - the
 * runner launches this binary once per row and they share one compile.
 *
 * THE EXPECTED TRANSCRIPT IS BUILT FROM THE MODE, through the module's OWN
 * decoders. Writing each row's expected call sequence out by hand would be a
 * second copy of the matrix, and the first thing it could do is agree with a
 * wrong table. sl_bgtf_es_matrix and sl_bgtf_es_switches are asked what the row
 * means and the sequence is assembled from the answers, so a table and a draw
 * site that disagree fail here rather than both being described by the test. */

/* The highest row this fixture knows about, so a loop over the rows is written
 * once. A row added to the module without being added here would be covered by
 * the out-of-range clauses below and would fail them, which is the intended
 * outcome: a new row has to be described before it can be driven. */
#define ES_ROW_MAX SL_BGTF_ES_Q

/* THE TABLE'S OWN INVARIANTS, asserted over the module's decoders rather than
 * over the draw site. The decoders are where a row could be introduced, so this
 * is where the rules about the rows belong: OFF must be the unchanged state, NO
 * row may pair an active feedback scope with a program switch, because
 * glUseProgram inside a begin/end scope is invalid and a row that asked for both
 * would be asking the draw site to make an invalid call, and the program at the
 * draw must be the one the row's own switches produce.
 *
 * AND THE ROWS MUST BE DISTINGUISHABLE FROM THE COMMANDS ALONE. The whole point
 * of the three program-switch rows is that A, R and Q draw under the same
 * program and E and S draw under the same program, so a reader cannot tell them
 * apart by the conditions AT the draw. The pairwise check below requires every
 * row's six-field description to be unique, which is what makes a later gate
 * able to name the row from what was issued rather than from the selector. */
static void es_table(void)
{
    int m, i, j;

    begin_case("empty-draw state: the decoders answer the unchanged state for OFF, "
               "name every row, pair NO feedback-active row with a program switch, "
               "derive the program at the draw from the switches, and describe every "
               "row DISTINCTLY");
    {
        int t = -1, d = -1, p = -1, p0 = -1, pp = -1, q0 = -1, qp = -1;
        sl_bgtf_es_matrix(SL_BGTF_ES_OFF, &t, &d, &p);
        sl_bgtf_es_switches(SL_BGTF_ES_OFF, &p0, &pp, &q0, &qp);
        CHECK(t == 1 && d == 1 && p == 1,
              "OFF is the zero-count arm's own state, so nothing is changed from "
              "it (%d/%d/%d)", t, d, p);
        CHECK(p0 == 0 && pp == 0 && q0 == 0 && qp == 0,
              "and it issues NO program switch, which is that arm's own command "
              "history (%d/%d/%d/%d)", p0, pp, q0, qp);
        CHECK(strcmp(sl_bgtf_es_name(SL_BGTF_ES_OFF), "OFF") == 0,
              "and it is named OFF (%s)", sl_bgtf_es_name(SL_BGTF_ES_OFF));
    }
    /* EVERY ROW, including the modes and the sentinels either side, so an
     * out-of-range value is covered by the same loop that covers the rows. */
    for (m = -3; m <= ES_ROW_MAX + 3; m++) {
        int t = -1, d = -1, p = -1, p0 = -1, pp = -1, q0 = -1, qp = -1;
        sl_bgtf_es_matrix(m, &t, &d, &p);
        sl_bgtf_es_switches(m, &p0, &pp, &q0, &qp);
        CHECK((t == 0 || t == 1) && (d == 0 || d == 1) && (p == 0 || p == 1),
              "mode %d decodes to three 0/1 conditions (%d/%d/%d)", m, t, d, p);
        CHECK((p0 == 0 || p0 == 1) && (pp == 0 || pp == 1)
              && (q0 == 0 || q0 == 1) && (qp == 0 || qp == 1),
              "and to four 0/1 switch answers (%d/%d/%d/%d)", p0, pp, q0, qp);
        CHECK(!(t == 1 && (p0 || pp || q0 || qp)),
              "mode %d does not pair an ACTIVE feedback scope with a program "
              "switch, which glUseProgram forbids (%d: %d/%d/%d/%d)", m, t,
              p0, pp, q0, qp);
        CHECK(!(t == 1 && p == 0),
              "nor an ACTIVE scope with program 0 at the draw, which is the same "
              "rule read off the derived condition (%d/%d)", t, p);
        /* THE DERIVATION, ASSERTED ROW BY ROW. The program at the draw is 0
         * exactly when the row switched to 0 before it and did not switch back,
         * so a stored condition that disagreed with the issued calls - the one
         * way a row can lie about itself - fails here. */
        CHECK(p == ((p0 && !pp) ? 0 : 1),
              "mode %d's program AT the draw is the one its own pre-draw "
              "switches produce (%d, from %d/%d)", m, p, p0, pp);
        /* AND THE ORDER INSIDE A PAIR IS 0 THEN THE PROGRAM, so no row asks for
         * a rebind of a program that is already current. */
        CHECK(!(pp && !p0),
              "mode %d does not rebind the prepass program before the draw "
              "without having left it (%d/%d)", m, p0, pp);
        CHECK(!(qp && !q0 && !(p0 && !pp)),
              "nor after the draw unless program 0 was current there (%d/%d/%d)",
              p0, pp, qp);
        if (m <= 0 || m > ES_ROW_MAX)
            CHECK(t == 1 && d == 1 && p == 1 && p0 == 0 && pp == 0 && q0 == 0
                  && qp == 0,
                  "and mode %d, which is not one of the rows, answers the "
                  "unchanged state and no switch rather than a default row "
                  "(%d/%d/%d, %d/%d/%d/%d)", m, t, d, p, p0, pp, q0, qp);
    }
    {
        /* THE ROWS ARE THE MATRIX THE SCOPE NAMES, stated once here so a table
         * edit has to come past this case. */
        int t, d, p, p0, pp, q0, qp;
        sl_bgtf_es_matrix(SL_BGTF_ES_A, &t, &d, &p);
        sl_bgtf_es_switches(SL_BGTF_ES_A, &p0, &pp, &q0, &qp);
        CHECK(t == 0 && d == 1 && p == 1 && !p0 && !pp && !q0 && !qp,
              "A is T0 D1 shader with NO switch (%d/%d/%d, %d/%d/%d/%d)",
              t, d, p, p0, pp, q0, qp);
        sl_bgtf_es_matrix(SL_BGTF_ES_B, &t, &d, &p);
        sl_bgtf_es_switches(SL_BGTF_ES_B, &p0, &pp, &q0, &qp);
        CHECK(t == 1 && d == 0 && p == 1 && !p0 && !pp && !q0 && !qp,
              "B is T1 D0 shader with NO switch (%d/%d/%d, %d/%d/%d/%d)",
              t, d, p, p0, pp, q0, qp);
        sl_bgtf_es_matrix(SL_BGTF_ES_C, &t, &d, &p);
        sl_bgtf_es_switches(SL_BGTF_ES_C, &p0, &pp, &q0, &qp);
        CHECK(t == 0 && d == 0 && p == 1 && !p0 && !pp && !q0 && !qp,
              "C is T0 D0 shader with NO switch (%d/%d/%d, %d/%d/%d/%d)",
              t, d, p, p0, pp, q0, qp);
        sl_bgtf_es_matrix(SL_BGTF_ES_E, &t, &d, &p);
        sl_bgtf_es_switches(SL_BGTF_ES_E, &p0, &pp, &q0, &qp);
        CHECK(t == 0 && d == 1 && p == 0 && p0 && !pp && !q0 && qp,
              "E is T0 D1 program 0: 0 BEFORE the draw, the program after it "
              "(%d/%d/%d, %d/%d/%d/%d)", t, d, p, p0, pp, q0, qp);
        sl_bgtf_es_matrix(SL_BGTF_ES_R, &t, &d, &p);
        sl_bgtf_es_switches(SL_BGTF_ES_R, &p0, &pp, &q0, &qp);
        CHECK(t == 0 && d == 1 && p == 1 && p0 && pp && !q0 && !qp,
              "R is T0 D1 shader: 0 AND the program both BEFORE the draw, so the "
              "switch pair happens and the shader is current at the call "
              "(%d/%d/%d, %d/%d/%d/%d)", t, d, p, p0, pp, q0, qp);
        sl_bgtf_es_matrix(SL_BGTF_ES_S, &t, &d, &p);
        sl_bgtf_es_switches(SL_BGTF_ES_S, &p0, &pp, &q0, &qp);
        CHECK(t == 0 && d == 1 && p == 0 && p0 && !pp && !q0 && !qp,
              "S is T0 D1 program 0: 0 before the draw and LEFT current, which is "
              "E without E's rebind (%d/%d/%d, %d/%d/%d/%d)", t, d, p,
              p0, pp, q0, qp);
        sl_bgtf_es_matrix(SL_BGTF_ES_Q, &t, &d, &p);
        sl_bgtf_es_switches(SL_BGTF_ES_Q, &p0, &pp, &q0, &qp);
        CHECK(t == 0 && d == 1 && p == 1 && !p0 && !pp && q0 && qp,
              "Q is T0 D1 shader: the switch pair AFTER the draw, which is A plus "
              "that pair (%d/%d/%d, %d/%d/%d/%d)", t, d, p, p0, pp, q0, qp);
        /* THE THREE NEW ROWS HOLD T, D AND THE EMPTY DRAW FIXED, which is what
         * makes them a bisection of E against A rather than three more arms. */
        {
            int at, ad, ap, rt, rd, rp, st, sd, sp, qt, qd, qp2, et, ed, ep;
            sl_bgtf_es_matrix(SL_BGTF_ES_A, &at, &ad, &ap);
            sl_bgtf_es_matrix(SL_BGTF_ES_E, &et, &ed, &ep);
            sl_bgtf_es_matrix(SL_BGTF_ES_R, &rt, &rd, &rp);
            sl_bgtf_es_matrix(SL_BGTF_ES_S, &st, &sd, &sp);
            sl_bgtf_es_matrix(SL_BGTF_ES_Q, &qt, &qd, &qp2);
            CHECK(rt == at && rd == ad && st == at && sd == ad && qt == at
                  && qd == ad && et == at && ed == ad,
                  "R, S, Q and E all hold A's T0 and D1, so only the program "
                  "switches differ between them");
            CHECK(ap == 1 && rp == 1 && qp2 == 1 && ep == 0 && sp == 0,
                  "A, R and Q draw under the prepass program and E and S under "
                  "program 0 (%d/%d/%d vs %d/%d)", ap, rp, qp2, ep, sp);
        }
        CHECK(strcmp(sl_bgtf_es_name(SL_BGTF_ES_A), "A") == 0
              && strcmp(sl_bgtf_es_name(SL_BGTF_ES_B), "B") == 0
              && strcmp(sl_bgtf_es_name(SL_BGTF_ES_C), "C") == 0
              && strcmp(sl_bgtf_es_name(SL_BGTF_ES_E), "E") == 0
              && strcmp(sl_bgtf_es_name(SL_BGTF_ES_R), "R") == 0
              && strcmp(sl_bgtf_es_name(SL_BGTF_ES_S), "S") == 0
              && strcmp(sl_bgtf_es_name(SL_BGTF_ES_Q), "Q") == 0,
              "and the rows are named A, B, C, E, R, S and Q");
        CHECK(strcmp(sl_bgtf_es_name(99), "?") == 0,
              "while a value outside them has no name (%s)",
              sl_bgtf_es_name(99));
    }
    /* EVERY ROW IS DESCRIBED DISTINCTLY by the two conditions and the four switch
     * positions together. Two rows sharing all six would be indistinguishable in
     * any record built from issued commands, which is the one thing a gate
     * reading those records cannot work around. */
    for (i = 0; i <= ES_ROW_MAX; i++) {
        for (j = i + 1; j <= ES_ROW_MAX; j++) {
            int it, id, ip0, ipp, iq0, iqp;
            int jt, jd, jp0, jpp, jq0, jqp;
            sl_bgtf_es_matrix(i, &it, &id, NULL);
            sl_bgtf_es_switches(i, &ip0, &ipp, &iq0, &iqp);
            sl_bgtf_es_matrix(j, &jt, &jd, NULL);
            sl_bgtf_es_switches(j, &jp0, &jpp, &jq0, &jqp);
            CHECK(it != jt || id != jd || ip0 != jp0 || ipp != jpp
                  || iq0 != jq0 || iqp != jqp,
                  "rows %s and %s differ in the conditions or the switches, so "
                  "the commands name the row", sl_bgtf_es_name(i),
                  sl_bgtf_es_name(j));
        }
    }
    end_case();
}

/* THE SERVED-MESH IDENTITY THIS MODE HAS TO SATISFY. Every mesh the caller
 * serves takes either a setup or a cache hit, so the two sum to the served
 * count. The scenario drives a known number of each and checks the sum, which is
 * what makes the census's own equality a property of the module rather than of
 * one census line. */
#define ES_SERVED 5

/* ONE ROW'S WHOLE EXPECTED BRACKET, IN ORDER, ASSEMBLED FROM THE MODULE'S OWN
 * DECODERS. Writing each row's call sequence out by hand would be a second copy
 * of the table, and the first thing it could do is agree with a wrong one. It is
 * a function rather than inline code because the case below asks it for EVERY
 * row and not only the selected one: "this transcript is row R's and no other
 * row's" is the claim that makes the rows tellable apart, and it cannot be made
 * from one expectation.
 *
 * THE PROGRAM NAMES ARE EXACT, not ANY. A row's difference from its neighbours
 * is WHICH program each switch names and WHERE it sits - E's 0-then-rebind
 * against R's 0-then-rebind BEFORE the draw against Q's pair after it - so a
 * wildcard in the program argument would make three of the rows one expectation.
 * `prog` is read from the transcript's own first glUseProgram, which is the
 * common bracket's, so the test never invents a name. */
static int es_expect(struct expect *e, int mode, GLuint vbo, GLuint pool,
                     GLuint prog)
{
    int t = 1, d = 1, p0 = 0, pp = 0, q0 = 0, qp = 0;
    int n = 0;

    sl_bgtf_es_matrix(mode, &t, &d, NULL);
    sl_bgtf_es_switches(mode, &p0, &pp, &q0, &qp);

    n = so_push(e, n, TFGL_GENBUFFERS, 1, ANY, ANY, ANY, "the pool's name");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) pool, ANY, ANY,
                "bind to size it");
    n = so_push(e, n, TFGL_BUFFERDATA, GL_ARRAY_BUFFER, (long) NV * 16L,
                GL_STATIC_DRAW, 0, "four floats per vertex, no initial data");
    n = so_push(e, n, TFGL_GETBUFPARAM, GL_ARRAY_BUFFER, GL_BUFFER_SIZE,
                (long) NV * 16L, 0,
                "and the driver is asked whether it honoured the size");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                "and unbind it again");
    n = so_push(e, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY,
                "the prepass program, on EVERY row");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_dmode"), ANY, ANY, ANY,
                "the depth case, written under that program and never under 0");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk1"), ANY, ANY, ANY,
                "the first coefficient");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk2"), ANY, ANY, ANY,
                "the second coefficient");
    n = so_push(e, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                "the common bracket enables discard on EVERY row");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY, ANY,
                "the pass's own static vertex buffer");
    n = so_push(e, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR, (long) vbo,
                "three floats at the item's stride, out of that buffer");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                "unbound straight away");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
                "no stale element buffer across the bracket");
    /* THE OUTPUT BINDING IS KEPT ON EVERY ROW, T0 included. What a T0 row drops
     * is the ACTIVE CAPTURE, not the pool or its binding. */
    n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                (long) pool, ANY, "the pool is bound at index 0 on EVERY row");
    if (t)
        n = so_push(e, n, TFGL_BEGINTF, GL_POINTS, ANY, ANY, ANY,
                    "a T1 row opens the scope, in GL_POINTS to match the draw");
    if (!d)
        n = so_push(e, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                    "a D0 row disables discard for the draw and nothing else");
    if (p0)
        n = so_push(e, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY,
                    "a row with a pre-draw switch leaves the prepass program "
                    "AFTER every uniform was written");
    if (pp)
        n = so_push(e, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY,
                    "and a row whose pair is BEFORE the draw comes back to it "
                    "there, so the shader is current at the call");
    n = so_push(e, n, TFGL_DRAWARRAYS, GL_POINTS, 0, 0L, (long) pool,
                "EXACTLY glDrawArrays(GL_POINTS, 0, 0), issued on every row, "
                "with the pool still the recording target");
    if (q0)
        n = so_push(e, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY,
                    "a row whose pair is AFTER the draw leaves the program "
                    "there instead");
    if (qp)
        n = so_push(e, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY,
                    "and comes back to the prepass program after it");
    if (!d)
        n = so_push(e, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                    "and so does the common bracket's discard");
    if (t)
        n = so_push(e, n, TFGL_ENDTF, ANY, ANY, ANY, ANY,
                    "a T1 row closes the scope it opened - the pair is matched");
    n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0,
                ANY, "the indexed binding is released on EVERY row");
    n = so_push(e, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                "the common bracket's own disable");
    /* AND THE FIXED PIPELINE COMES BACK, THE SAME ON EVERY ROW, including the
     * one row that never rebound the program after its draw: this call is what
     * makes S's caller state identical to every other row's. */
    n = so_push(e, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY,
                "and the fixed pipeline comes back, the same on every row");
    /* AND THEN THE POST-RESET BOUNDARY'S OWN CALLS, IF AN ARM IS SELECTED. They
     * are pushed from the arm's decoder rather than from its name, exactly as
     * the row's conditional records above are pushed from the row's.
     *
     * WITH THE CONTROL OFF BOTH ANSWERS ARE 0 and this appends nothing, so every
     * expectation this function produced before the control existed is the same
     * sequence it produces now - which is what keeps the rows' own runs and
     * their digests unmoved.
     *
     * THEY COME AFTER THE COMMON glUseProgram(0) AND THERE IS NO SWITCH BETWEEN,
     * which is the arm's load-bearing property: program 0 is already current, so
     * the empty draw needs no glUseProgram and the expectation contains none.
     * The draw precedes the wait on the arm that issues both. */
    {
        int pr_d = 0, pr_fin = 0;
        sl_bgtf_pr_calls(sl_bgtf_post_reset(), &pr_d, &pr_fin);
        if (pr_d)
            n = so_push(e, n, TFGL_DRAWARRAYS, GL_POINTS, 0, 0L, 0L,
                        "the boundary's empty draw, under program 0 and with "
                        "the indexed binding already released");
        if (pr_fin)
            n = so_push(e, n, TFGL_FINISH, ANY, ANY, ANY, ANY,
                        "and the boundary's one glFinish, after the draw on the "
                        "arm that issues both");
    }
    return n;
}

/* The same comparison seq_is makes, as a PREDICATE that asserts nothing. The
 * discrimination check needs to ask whether a transcript matches a row without
 * recording a failure for the rows it is required not to match. */
static int seq_matches(long from, const struct expect *e, int n)
{
    long i;
    if (tf.nlog != from + (long) n) return 0;
    for (i = 0; i < (long) n; i++) {
        const struct sl_bgtfgl_call *r = &tf.log[from + i];
        if (r->kind != e[i].kind) return 0;
        if (e[i].a != ANY && r->a != e[i].a) return 0;
        if (e[i].b != ANY && r->b != e[i].b) return 0;
        if (e[i].c != ANY && r->c != e[i].c) return 0;
        if (e[i].d != ANY && r->d != e[i].d) return 0;
    }
    return 1;
}

static void es_scenario(int mode)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo, pool = 0u, prog = 0u;
    long mark, calls_at_hit, use_program_before;
    int es_t = 1, es_d = 1, es_p = 1;
    int es_p0 = 0, es_pp = 0, es_q0 = 0, es_qp = 0, es_switches;
    /* THE POST-RESET BOUNDARY ARM COMPOSED WITH THIS ROW, if a launch selected
     * one. Both answers are 0 with the control off, so every number below is the
     * one this case asserted before the control existed. */
    const int pr_mode = sl_bgtf_post_reset();
    int pr_d = 0, pr_fin = 0;
    int s, i;

    sl_bgtf_es_matrix(mode, &es_t, &es_d, &es_p);
    sl_bgtf_es_switches(mode, &es_p0, &es_pp, &es_q0, &es_qp);
    es_switches = es_p0 + es_pp + es_q0 + es_qp;
    sl_bgtf_pr_calls(pr_mode, &pr_d, &pr_fin);

    begin_case("empty-draw state: ONE row's empty draw, with the three conditions read "
               "AT the call, the common bracket unchanged around it and the caller's "
               "state restored at close");
    arm();
    CHECK(sl_bgtf_empty_state() == mode, "the mode answers %s (%d)",
          sl_bgtf_es_name(mode), sl_bgtf_empty_state());
    /* THE MODE RUNS ON THE ZERO-COUNT ARM, and that coupling is asserted rather
     * than assumed: every claim below about an EMPTY draw depends on it. */
    CHECK(sl_bgtf_zero_count() == 1,
          "the zero-count arm is in force, which is what makes the draw empty (%d)",
          sl_bgtf_zero_count());
    CHECK(sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0
          && sl_bgtf_private_vao() == 0,
          "and none of the three exclusive controls is (%d/%d/%d)",
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    mark = tf.nlog;
    b = snap();
    /* THE PROGRAM COUNT IS READ AS A DELTA, because sl_bgtf_ready made the
     * program current while it was building it: a cumulative total would be
     * comparing the bracket's switches against the capability path's as well.
     * The enables, disables, draws and begin/end counts need no baseline - the
     * capability path issues none of those - so only this one is differenced. */
    use_program_before = tf.use_program;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0 && s < SLOTS, "a slot was taken (%d)", s);
    {
        const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
        const struct sl_bgtfgl_call *up = first_of(mark, TFGL_USEPROGRAM);
        CHECK(base != NULL, "a recording buffer was bound at index 0");
        pool = base != NULL ? (GLuint) base->c : 0u;
        CHECK(pool != 0u && pool != vbo,
              "and it is a buffer of the module's own, not the input (%u vs %u)",
              (unsigned) pool, (unsigned) vbo);
        /* THE PREPASS PROGRAM'S NAME, TAKEN FROM THE BRACKET'S OWN FIRST
         * glUseProgram, which the common setup makes on every row. Every switch
         * below is then asserted against an exact name rather than a wildcard,
         * which is what distinguishes a switch to 0 from a switch back. */
        CHECK(up != NULL, "and the common setup made a program current");
        prog = up != NULL ? (GLuint) up->a : 0u;
        CHECK(prog != 0u,
              "which is the prepass program and not 0 (%u)", (unsigned) prog);
    }

    /* THE WHOLE BRACKET, IN ORDER, ASSEMBLED FROM THE MODE. The conditional
     * records are the only difference between the rows, and each is pushed from
     * the decoders' answers rather than from the row's name. */
    {
        struct expect e[40];
        const int n = es_expect(e, mode, vbo, pool, prog);
        seq_is(mark, e, n);
    }

    /* AND IT IS THIS ROW'S BRACKET AND NO OTHER ROW'S. The case above says the
     * transcript matches what the selected row describes; this says it matches
     * NOTHING ELSE the table can describe, which is the property a later gate
     * depends on. Without it, two rows whose expectations had collapsed into one
     * sequence would both pass their own run and be indistinguishable in any
     * record - and A, R and Q share both conditions AT the draw, so the draw-site
     * sample cannot separate them either. */
    {
        int m, matched = 0, which = -1;
        for (m = 0; m <= ES_ROW_MAX; m++) {
            struct expect e[40];
            const int n = es_expect(e, m, vbo, pool, prog);
            if (seq_matches(mark, e, n)) { matched++; which = m; }
        }
        CHECK(matched == 1 && which == mode,
              "the transcript matches EXACTLY ONE row's expected bracket, and it "
              "is %s (%d matched, first %s)", sl_bgtf_es_name(mode), matched,
              sl_bgtf_es_name(which));
    }

    /* THE THREE CONDITIONS AS THE DRAW FOUND THEM. This is the measurement the
     * row exists to produce, and it is unavailable anywhere else: the next three
     * calls put all three back. */
    CHECK(tf.st_at_draw_seen == 1L + (long) pr_d,
          "exactly %ld draw(s) were sampled in this bracket - the row's empty "
          "draw, plus the post-reset boundary's own on an arm that issues one "
          "(%ld)", 1L + (long) pr_d, tf.st_at_draw_seen);
    if (pr_d) {
        /* THE BOUNDARY'S DRAW IS THE LAST ONE SAMPLED, and its three conditions
         * are the ones the boundary's placement guarantees rather than the
         * row's: no scope open, discard off and program 0 current. The row's own
         * draw is checked by the ordered transcript above, which is where the
         * two are told apart by position; the sample below is the last draw, so
         * the row's conditions are deliberately NOT asserted against it here. */
        CHECK(tf.st_at_draw_tf_active == 0,
              "AT THE BOUNDARY DRAW, which is the last one, no feedback scope "
              "was open (%d)", tf.st_at_draw_tf_active);
        CHECK(tf.st_at_draw_discard == 0,
              "rasterizer discard was OFF, the common bracket having disabled it "
              "(%d)", tf.st_at_draw_discard);
        CHECK(tf.st_at_draw_program == 0u,
              "and program 0 was current, which is why the arm issues no "
              "glUseProgram (%u)", (unsigned) tf.st_at_draw_program);
    }
    else {
        /* WITH NO BOUNDARY DRAW the last sample IS the row's own, so the row's
         * three conditions are asserted against it directly. */
        CHECK(tf.st_at_draw_tf_active == es_t,
              "AT THE DRAW transform feedback was %s, as row %s requires (%d)",
              es_t ? "ACTIVE" : "INACTIVE", sl_bgtf_es_name(mode),
              tf.st_at_draw_tf_active);
        CHECK(tf.st_at_draw_discard == es_d,
              "AT THE DRAW rasterizer discard was %s (%d)", es_d ? "ON" : "OFF",
              tf.st_at_draw_discard);
        CHECK((tf.st_at_draw_program != 0u) == (es_p != 0),
              "AT THE DRAW the current program was %s (%u)",
              es_p ? "the prepass program" : "0",
              (unsigned) tf.st_at_draw_program);
    }
    /* THE TOTALS OVER EVERY DRAW THE BRACKET SAMPLED, which is what one sample
     * of the last draw cannot say. The boundary's draw adds a draw with no scope
     * open, discard off and program 0, so the row's own contribution is the
     * total minus a known constant rather than the total itself - and the arm
     * adds nothing to any of the three. */
    CHECK(tf.st_draws_tf_active == (es_t ? 1L : 0L)
          && tf.st_draws_discard_on == (es_d ? 1L : 0L)
          && tf.st_draws_program_nonzero == (es_p ? 1L : 0L),
          "EVERY draw in the bracket agreed with the row, not only the last one, "
          "and the boundary's own draw adds to none of the three (%ld/%ld/%ld)",
          tf.st_draws_tf_active, tf.st_draws_discard_on,
          tf.st_draws_program_nonzero);

    /* EXACTLY ONE EMPTY CALL PER ROW, PLUS THE BOUNDARY'S OWN ON AN ARM THAT
     * ISSUES ONE. The first number separates every row of this mode from the
     * state-only arm's omission; the second is what "empty" means, and it holds
     * for the boundary's draw as well - neither call ever passes a count. */
    CHECK(tf.draw_arrays == 1L + (long) pr_d,
          "the draw was ISSUED exactly once, and the boundary's %s (%ld)",
          pr_d ? "once more" : "not at all", tf.draw_arrays);
    CHECK(tf.draw_vertices == 0L,
          "and every one submitted ZERO vertices - no row and no arm ever passes "
          "a nonzero count (%ld)", tf.draw_vertices);
    CHECK(tf.finish_calls == (long) pr_fin,
          "the boundary's glFinish was issued %s, and nothing else in the "
          "bracket issues one (%ld)", pr_fin ? "once" : "NOT AT ALL",
          tf.finish_calls);

    /* THE SCOPE IS MATCHED OR ABSENT, AND NO INVALID CALL WAS MADE. */
    CHECK(tf.begin_tf == (es_t ? 1L : 0L) && tf.end_tf == (es_t ? 1L : 0L),
          "the begin/end pair was issued %s (%ld/%ld)",
          es_t ? "once, matched" : "NOT AT ALL, both omitted together",
          tf.begin_tf, tf.end_tf);
    CHECK(tf.v_tf_nested == 0 && tf.v_tf_unbalanced == 0,
          "with no nested or unbalanced scope (%ld/%ld)", tf.v_tf_nested,
          tf.v_tf_unbalanced);
    CHECK(tf.v_program_change_in_tf == 0,
          "and NO program change was made while feedback was active, which the "
          "API forbids (%ld)", tf.v_program_change_in_tf);
    CHECK(tf.uniform_rejected == 0,
          "every uniform was written to a program that was actually current, so "
          "none was written under program 0 (%ld)", tf.uniform_rejected);
    /* THE TRANSITIONS, COUNTED AS PAIRS. A D1 row makes none; a D0 row makes one
     * each way. The same for the program. */
    CHECK(tf.enables == (es_d ? 1L : 2L) && tf.disables == (es_d ? 1L : 2L),
          "discard was enabled and disabled %s (%ld/%ld)",
          es_d ? "once each, by the common bracket alone"
               : "twice each: the common bracket plus the draw's own pair",
          tf.enables, tf.disables);
    CHECK(tf.use_program - use_program_before == 2L + (long) es_switches,
          "the bracket made the program current %ld times, which is the common "
          "bracket's two plus this row's %d switch(es) (%ld)",
          2L + (long) es_switches, es_switches,
          tf.use_program - use_program_before);
    /* THE OUTPUT ALLOCATION AND ITS BINDING ARE THE SAME ON EVERY ROW. */
    CHECK(tf.bind_buffer_base == 2,
          "the indexed binding was made and released on this row too (%ld)",
          tf.bind_buffer_base);
    CHECK(tf.gen_buffers == 1 && tf.buffer_data == 1
          && tf.get_buffer_parameteriv == 1,
          "the pool was created, sized and VERIFIED (%ld/%ld/%ld)",
          tf.gen_buffers, tf.buffer_data, tf.get_buffer_parameteriv);
    CHECK(sl_bgtfgl_buffer_bytes(pool) == (long) NV * 16L,
          "and sized nverts * 4 floats, unchanged by the row (%ld, expected %ld)",
          sl_bgtfgl_buffer_bytes(pool), (long) NV * 16L);
    CHECK(tf.vptr.set && tf.vptr.size == 3 && tf.vptr.stride == (GLsizei) STR
          && tf.vptr.buf == vbo && tf.vptr.off == 0,
          "the input position pointer named the pass's own buffer at its own "
          "stride (set %d, size %d, stride %d, buf %u, off %ld)", tf.vptr.set,
          (int) tf.vptr.size, (int) tf.vptr.stride, (unsigned) tf.vptr.buf,
          tf.vptr.off);

    /* THE CALLER'S STATE AT CLOSE, WHICH IS WHERE EVERY ROW AGREES. This is the
     * other half of "narrowly scoped": the row differs at the draw and nowhere
     * else, so the state the caller is handed back is identical across all of
     * them and identical to Z_KNOWN's. */
    CHECK(tf.discard == 0, "discard is off afterwards, on every row (%d)",
          tf.discard);
    CHECK(tf.current == 0, "no program is current (%u)", (unsigned) tf.current);
    CHECK(tf.tf_buf == 0u, "nothing is left recording (%u)",
          (unsigned) tf.tf_buf);
    CHECK(tf.arr_buf == 0u, "no array buffer is left bound (%u)",
          (unsigned) tf.arr_buf);
    CHECK(tf.elem_buf == 0u, "and no element buffer either (%u)",
          (unsigned) tf.elem_buf);

    /* THE PRECONDITION VIOLATIONS THE SYNTHETIC GL RAISES, stated as the values
     * the row EXPECTS rather than as zero. A D0 row draws with discard off and a
     * program-0 row draws with no program current: both are the conditions the
     * row exists to create, so the stub's notes about them are expected here and
     * a zero would mean the row did not happen. The empty count is what makes
     * them harmless - no fragment can be produced by a draw over no vertices -
     * and that is an API-boundary fact, not a claim about a driver. */
    expect_draw_without_discard = (es_d ? 0L : 1L) + (long) pr_d;
    expect_draw_without_program = (es_p ? 0L : 1L) + (long) pr_d;
    CHECK(tf.v_draw_without_discard == expect_draw_without_discard,
          "the draw with discard %s is accounted for, together with the "
          "boundary's %s (%ld)",
          es_d ? "ON raises nothing" : "OFF is this row's own condition",
          pr_d ? "own, which is always at discard off" : "absence",
          tf.v_draw_without_discard);
    CHECK(tf.v_draw_without_program == expect_draw_without_program,
          "and so is the draw with %s, together with the boundary's %s (%ld)",
          es_p ? "a program current, which raises nothing"
               : "program 0, which is this row's own condition",
          pr_d ? "own, which is always under program 0" : "absence",
          tf.v_draw_without_program);
    /* AND THE BOUNDARY'S DRAW RECORDS NOTHING, which is why it raises no
     * recording note of its own. The shim's rule for that note is narrower than
     * its discard and program rules - `if (tf.tf_active)` guards it, because a
     * draw with no capture open cannot write a pool at all - and the boundary's
     * draw is issued with the scope closed AND the indexed binding released. So
     * this count stays 0 on every arm, and that zero is a statement about where
     * the boundary sits rather than an absence of evidence. */
    CHECK(tf.v_draw_without_tf_buffer == 0 && tf.v_tf_overrun == 0,
          "nothing recorded without a bound pool - the boundary's own draw has "
          "no capture open, so it cannot - and nothing overran one (%ld/%ld)",
          tf.v_draw_without_tf_buffer, tf.v_tf_overrun);

    /* THE WORK COUNTERS. The mode runs on the zero-count arm, so the candidate's
     * own numbers stay put exactly as they do on Z_KNOWN and no row can read as
     * transformed vertices or as TF-position-source coverage. */
    a = snap();
    CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices
          && a.walks == b.walks && a.reuses == b.reuses && a.binds == b.binds
          && a.raster_draws == b.raster_draws
          && a.raster_indices == b.raster_indices,
          "NO prepass, vertex, walk, pool reuse, bind or submission was counted "
          "(%lu/%lu/%lu/%lu/%lu/%lu/%lu)",
          a.prepasses - b.prepasses, a.vertices - b.vertices,
          a.walks - b.walks, a.reuses - b.reuses, a.binds - b.binds,
          a.raster_draws - b.raster_draws,
          a.raster_indices - b.raster_indices);
    CHECK(a.zc_setups - b.zc_setups == 1 && a.zc_calls - b.zc_calls == 1,
          "the zero-count arm's own cadence is still counted beneath this mode "
          "(%lu/%lu)", a.zc_setups - b.zc_setups, a.zc_calls - b.zc_calls);
    CHECK(a.so_setups == b.so_setups && a.so_omitted == b.so_omitted,
          "and nothing for the state-only control, whose OMISSION is a different "
          "experiment (%lu/%lu)", a.so_setups - b.so_setups,
          a.so_omitted - b.so_omitted);
    CHECK(a.es_setups - b.es_setups == 1,
          "one setup bracket was counted for the mode (%lu)",
          a.es_setups - b.es_setups);
    CHECK(a.es_draws - b.es_draws == 1,
          "and one ISSUED empty draw with it (%lu)", a.es_draws - b.es_draws);
    CHECK(a.es_draws - b.es_draws == a.es_setups - b.es_setups,
          "the two agree, because one bracket issues exactly one draw (%lu vs %lu)",
          a.es_draws - b.es_draws, a.es_setups - b.es_setups);
    CHECK(a.es_tf_begins - b.es_tf_begins == (es_t ? 1UL : 0UL)
          && a.es_tf_ends - b.es_tf_ends == (es_t ? 1UL : 0UL),
          "the begin/end counts are the row's and are EQUAL to each other "
          "(%lu/%lu)", a.es_tf_begins - b.es_tf_begins,
          a.es_tf_ends - b.es_tf_ends);
    CHECK(a.es_discard_offs - b.es_discard_offs == (es_d ? 0UL : 1UL)
          && a.es_discard_ons - b.es_discard_ons == (es_d ? 0UL : 1UL),
          "the discard transition and its restoration are EQUAL (%lu/%lu)",
          a.es_discard_offs - b.es_discard_offs,
          a.es_discard_ons - b.es_discard_ons);
    /* THE PROGRAM SWITCHES, COUNTED BY TARGET AND BY POSITION. The two totals
     * are every switch to 0 and every switch back, which E and R both make once
     * each - so the totals alone cannot tell those two apart, and the four
     * positional counts are what do. They are asserted against the decoder's own
     * answers, so a draw site that issued a call the table did not name, or
     * named one it did not issue, fails here. */
    CHECK(a.es_prog0_switches - b.es_prog0_switches
              == (unsigned long) (es_p0 + es_q0)
          && a.es_prog0_restores - b.es_prog0_restores
              == (unsigned long) (es_pp + es_qp),
          "the switches to program 0 and back are the row's own totals "
          "(%lu/%lu, expected %d/%d)",
          a.es_prog0_switches - b.es_prog0_switches,
          a.es_prog0_restores - b.es_prog0_restores,
          es_p0 + es_q0, es_pp + es_qp);
    CHECK(a.es_prog_pre_zero - b.es_prog_pre_zero == (unsigned long) es_p0
          && a.es_prog_pre_prog - b.es_prog_pre_prog == (unsigned long) es_pp
          && a.es_prog_post_zero - b.es_prog_post_zero == (unsigned long) es_q0
          && a.es_prog_post_prog - b.es_prog_post_prog == (unsigned long) es_qp,
          "and each one is counted WHERE it was issued (%lu/%lu/%lu/%lu, "
          "expected %d/%d/%d/%d)",
          a.es_prog_pre_zero - b.es_prog_pre_zero,
          a.es_prog_pre_prog - b.es_prog_pre_prog,
          a.es_prog_post_zero - b.es_prog_post_zero,
          a.es_prog_post_prog - b.es_prog_post_prog,
          es_p0, es_pp, es_q0, es_qp);
    /* AND THE DRAW IS COUNTED UNDER THE PROGRAM IT WAS MADE UNDER, which is read
     * from the calls the module issued rather than from the row's name: the two
     * sum to the issued draws, so a row whose switches and declared program
     * disagreed would be caught by the pair rather than by the table. */
    CHECK(a.es_draws_prog_tf - b.es_draws_prog_tf == (es_p ? 1UL : 0UL)
          && a.es_draws_prog0 - b.es_draws_prog0 == (es_p ? 0UL : 1UL),
          "the empty draw is counted under %s (%lu/%lu)",
          es_p ? "the prepass program" : "program 0",
          a.es_draws_prog_tf - b.es_draws_prog_tf,
          a.es_draws_prog0 - b.es_draws_prog0);
    CHECK(a.es_draws_prog_tf + a.es_draws_prog0 == a.es_draws,
          "and the two account for every issued draw in this process (%lu + %lu "
          "vs %lu)", a.es_draws_prog_tf, a.es_draws_prog0, a.es_draws);
    CHECK(a.es_refusals == b.es_refusals && a.es_refusals == 0,
          "the mode was not refused anywhere in this process (%lu)",
          a.es_refusals);
    CHECK(a.es_mode == (unsigned long) mode,
          "and the census carries the EFFECTIVE row, %s (%lu)",
          sl_bgtf_es_name(mode), a.es_mode);
    CHECK(a.refusals == b.refusals, "with no module refusal (%lu)",
          a.refusals - b.refusals);
    /* THE POST-RESET BOUNDARY'S OWN NUMBERS, COMPOSED WITH THIS ROW. One
     * bracket reached one boundary, and the two issued-call counts are the arm's
     * own answers - which is what says the control executed at this prepass
     * rather than only that it was selected. With the control off all of them
     * are 0, and the mode's and the arm's counts are independent in both
     * directions: the row's seventeen numbers above are unchanged by any arm,
     * and these are unchanged by any row. */
    CHECK(a.pr_setups - b.pr_setups == (pr_mode != SL_BGTF_PR_OFF ? 1UL : 0UL),
          "the boundary was reached %s (%lu)",
          pr_mode != SL_BGTF_PR_OFF ? "once, with every precondition proved"
                                    : "not at all, the control being off",
          a.pr_setups - b.pr_setups);
    CHECK(a.pr_skips == b.pr_skips && a.pr_skips == 0,
          "and nothing was skipped for an unproved precondition (%lu)",
          a.pr_skips);
    CHECK(a.pr_draws - b.pr_draws == (unsigned long) pr_d
          && a.pr_finishes - b.pr_finishes == (unsigned long) pr_fin,
          "the two issued-call counts are arm %s's own (%lu/%lu, expected "
          "%d/%d)", sl_bgtf_pr_name(pr_mode), a.pr_draws - b.pr_draws,
          a.pr_finishes - b.pr_finishes, pr_d, pr_fin);
    CHECK(a.pr_draws_prog0 == a.pr_draws,
          "every boundary draw in this process was made under program 0 (%lu vs "
          "%lu)", a.pr_draws_prog0, a.pr_draws);
    CHECK(a.pr_refusals == 0 && a.pr_mode == (unsigned long) pr_mode,
          "the control was not refused anywhere and the record carries the "
          "EFFECTIVE arm, %s (%lu/%lu)", sl_bgtf_pr_name(pr_mode),
          a.pr_refusals, a.pr_mode);

    /* THE SLOT IS UNWRITTEN AND UNBINDABLE, on every row. An empty call wrote no
     * output, so a caller that offers the index the prepass just returned must
     * still be refused - which is what stops any row rasterising storage no
     * transform filled. */
    b = snap();
    calls_at_hit = tf.calls;
    CHECK(sl_bgtf_bind_clip(s) == -1,
          "the slot this row's empty draw keyed is REFUSED by the binding");
    CHECK(snap().binds == b.binds, "no bind was counted (%lu)",
          snap().binds - b.binds);
    CHECK(tf.calls == calls_at_hit,
          "and the refusal cost no GL call (%ld -> %ld)", calls_at_hit,
          tf.calls);

    /* THE CACHE CADENCE IS THE ZERO-COUNT ARM'S, AND A HIT ADDS NOTHING. The
     * remaining served meshes take the same key, so each is a hit: no setup, no
     * draw, no begin/end pair, no transition and no GL call whatever. Together
     * with the one setup above, es_setups + es_reuses then accounts for every
     * mesh served, which is the identity the census prints. */
    b = snap();
    calls_at_hit = tf.calls;
    mark = tf.nlog;
    for (i = 1; i < ES_SERVED; i++)
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == s,
              "served mesh %d is a cache HIT on the same slot", i);
    a = snap();
    CHECK(tf.calls == calls_at_hit && tf.nlog == mark,
          "the %d hits added no GL call and no transcript record at all "
          "(%ld -> %ld, %ld -> %ld)", ES_SERVED - 1, calls_at_hit, tf.calls,
          mark, tf.nlog);
    CHECK(a.es_reuses - b.es_reuses == (unsigned long) (ES_SERVED - 1),
          "each hit was counted as the mode's own reuse (%lu)",
          a.es_reuses - b.es_reuses);
    CHECK(a.es_setups == b.es_setups && a.es_draws == b.es_draws
          && a.es_tf_begins == b.es_tf_begins
          && a.es_discard_offs == b.es_discard_offs
          && a.es_prog0_switches == b.es_prog0_switches
          && a.es_prog_pre_zero == b.es_prog_pre_zero
          && a.es_prog_pre_prog == b.es_prog_pre_prog
          && a.es_prog_post_zero == b.es_prog_post_zero
          && a.es_prog_post_prog == b.es_prog_post_prog
          && a.es_draws_prog_tf == b.es_draws_prog_tf
          && a.es_draws_prog0 == b.es_draws_prog0,
          "and added no setup, no draw and no transition of any kind, in any "
          "position (%lu/%lu/%lu/%lu/%lu)", a.es_setups - b.es_setups,
          a.es_draws - b.es_draws, a.es_tf_begins - b.es_tf_begins,
          a.es_discard_offs - b.es_discard_offs,
          a.es_prog0_switches - b.es_prog0_switches);
    CHECK(a.zc_reuses - b.zc_reuses == (unsigned long) (ES_SERVED - 1),
          "the zero-count arm's own reuse count is raised beside it, unchanged "
          "by this mode (%lu)", a.zc_reuses - b.zc_reuses);
    /* AND A HIT HAS NO BOUNDARY, so the control adds nothing for one either. */
    CHECK(a.pr_reuses - b.pr_reuses
              == (pr_mode != SL_BGTF_PR_OFF
                  ? (unsigned long) (ES_SERVED - 1) : 0UL),
          "each hit was counted as the control's own reuse, for which no "
          "boundary exists (%lu)", a.pr_reuses - b.pr_reuses);
    CHECK(a.pr_setups == b.pr_setups && a.pr_draws == b.pr_draws
          && a.pr_finishes == b.pr_finishes && a.pr_skips == b.pr_skips,
          "and the hits reached no boundary and issued no call of the control's "
          "(%lu/%lu/%lu/%lu)", a.pr_setups - b.pr_setups,
          a.pr_draws - b.pr_draws, a.pr_finishes - b.pr_finishes,
          a.pr_skips - b.pr_skips);
    CHECK(a.pr_setups + a.pr_skips + a.pr_reuses
              == (pr_mode != SL_BGTF_PR_OFF
                  ? (unsigned long) ES_SERVED : 0UL),
          "REACHED + SKIPPED + REUSES == the %d meshes served, so the control's "
          "account is complete too (%lu + %lu + %lu)", ES_SERVED, a.pr_setups,
          a.pr_skips, a.pr_reuses);
    CHECK(a.es_setups + a.es_reuses == (unsigned long) ES_SERVED,
          "SETUPS + REUSES == the %d meshes served, so the account is complete "
          "(%lu + %lu)", ES_SERVED, a.es_setups, a.es_reuses);
    end_case();
}

/* AN ALLOCATION THE DRIVER LEAVES EMPTY STILL REFUSES, on a selected row, and
 * nothing is claimed for the mode. The same shape as the zero-count arm's own
 * refusal case and for the same reason: the row must not be able to turn a
 * refusal into a setup, a draw or a transition. */
static void es_refusal(int mode)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s;

    begin_case("empty-draw state: an allocation the driver leaves EMPTY refuses on a "
               "selected row, counts nothing for the mode and leaves the slot "
               "unbindable");
    arm();
    CHECK(sl_bgtf_empty_state() == mode, "the mode is %s (%d)",
          sl_bgtf_es_name(mode), sl_bgtf_empty_state());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    tf.alloc_short_initial = 1;
    tf.alloc_bytes = 0L;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    tf.alloc_short_initial = 0;
    a = snap();
    CHECK(s == -1, "the draw is refused (%d)", s);
    CHECK(a.refusals - b.refusals == 1, "and the refusal is counted (%lu)",
          a.refusals - b.refusals);
    CHECK(a.es_setups == b.es_setups && a.es_draws == b.es_draws
          && a.es_reuses == b.es_reuses && a.es_tf_begins == b.es_tf_begins
          && a.es_tf_ends == b.es_tf_ends
          && a.es_discard_offs == b.es_discard_offs
          && a.es_discard_ons == b.es_discard_ons
          && a.es_prog0_switches == b.es_prog0_switches
          && a.es_prog0_restores == b.es_prog0_restores
          && a.es_prog_pre_zero == b.es_prog_pre_zero
          && a.es_prog_pre_prog == b.es_prog_pre_prog
          && a.es_prog_post_zero == b.es_prog_post_zero
          && a.es_prog_post_prog == b.es_prog_post_prog
          && a.es_draws_prog_tf == b.es_draws_prog_tf
          && a.es_draws_prog0 == b.es_draws_prog0,
          "NOTHING was counted for the mode - no setup, draw, reuse, scope, "
          "transition or program switch in any position");
    CHECK(a.zc_setups == b.zc_setups && a.zc_calls == b.zc_calls,
          "nor for the arm beneath it (%lu/%lu)", a.zc_setups - b.zc_setups,
          a.zc_calls - b.zc_calls);
    CHECK(tf.draw_arrays == 0, "no draw ran (%ld)", tf.draw_arrays);
    CHECK(tf.st_at_draw_seen == 0,
          "and no draw was sampled, so no condition is reported for a call that "
          "did not happen (%ld)", tf.st_at_draw_seen);
    /* AND THE POST-RESET BOUNDARY WAS NEVER REACHED, if an arm was selected.
     * The allocation refusal returns before the prepass bracket, so the
     * boundary is not merely skipped - it does not exist for this mesh. Both
     * counts therefore stay 0, and a positive pr_skips here would mean the
     * refusal had fallen through into a bracket it was supposed to abandon. */
    CHECK(a.pr_setups == b.pr_setups && a.pr_draws == b.pr_draws
          && a.pr_finishes == b.pr_finishes && a.pr_skips == b.pr_skips
          && a.pr_reuses == b.pr_reuses,
          "the post-reset boundary was never reached and issued nothing, and no "
          "skip was counted for a bracket the refusal abandoned (%lu/%lu/%lu/"
          "%lu/%lu)", a.pr_setups - b.pr_setups, a.pr_draws - b.pr_draws,
          a.pr_finishes - b.pr_finishes, a.pr_skips - b.pr_skips,
          a.pr_reuses - b.pr_reuses);
    CHECK(tf.finish_calls == 0,
          "and no glFinish was issued for a refused bracket (%ld)",
          tf.finish_calls);
    {
        int i;
        for (i = 0; i < SLOTS; i++)
            CHECK(sl_bgtf_bind_clip(i) == -1,
                  "slot %d cannot be bound after the refusal", i);
    }
    CHECK(snap().binds == a.binds, "and no bind was counted (%lu)",
          snap().binds - a.binds);
    end_case();
}

static void mode_es(int mode)
{
    es_table();
    es_scenario(mode);
    es_refusal(mode);
}

/* THE SELECTOR'S GRAMMAR, COUPLING, EXCLUSIVITY AND REFUSAL ACCOUNTING, one
 * setting per launch. The runner says what the mode must ANSWER and whether this
 * launch is one that asked for a mode and did not get one, because both the
 * coupling and the refusal are facts about the launch rather than about the
 * variable. */
static void mode_env_empty_state(int expect_mode, int want_refusal)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("empty-draw state selector: the grammar, the coupling to the "
               "zero-count arm, the exclusions and the refusal accounting");
    CHECK(sl_bgtf_empty_state() == expect_mode,
          "the mode answers %s (%s)", sl_bgtf_es_name(expect_mode),
          sl_bgtf_es_name(sl_bgtf_empty_state()));
    /* PARSED ONCE FOR THE LAUNCH. Repeated reads must cost nothing and must not
     * drift: the value is a launch constant, which is what lets one slot's tag
     * answer for a whole walk. */
    calls_before = tf.calls;
    for (i = 0; i < 64; i++)
        CHECK(sl_bgtf_empty_state() == expect_mode,
              "and answers the same on read %d", i);
    CHECK(tf.calls == calls_before,
          "reading it costs no GL call (%ld -> %ld)", calls_before, tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->es_refusals == (want_refusal ? 1UL : 0UL),
          "the launch is counted as %s, exactly once (%lu)",
          want_refusal ? "a REFUSAL" : "no refusal", t->es_refusals);
    CHECK(t->es_mode == (unsigned long) expect_mode,
          "and the recorded EFFECTIVE mode is %s (%lu)",
          sl_bgtf_es_name(expect_mode), t->es_mode);
    /* A REFUSED OR ABSENT MODE DID NO WORK OF ITS OWN, whatever else this launch
     * selected: a mode that is not in force must leave every one of its own
     * counts at 0, so no record can read a selected variable as work. */
    if (expect_mode == SL_BGTF_ES_OFF)
        CHECK(t->es_setups == 0 && t->es_draws == 0 && t->es_reuses == 0
              && t->es_tf_begins == 0 && t->es_tf_ends == 0
              && t->es_prog0_switches == 0 && t->es_prog0_restores == 0
              && t->es_prog_pre_zero == 0 && t->es_prog_pre_prog == 0
              && t->es_prog_post_zero == 0 && t->es_prog_post_prog == 0
              && t->es_draws_prog_tf == 0 && t->es_draws_prog0 == 0
              && t->es_discard_offs == 0 && t->es_discard_ons == 0,
              "and nothing at all was counted for a mode that is not in force");
    end_case();
}

/* ---- 18b. THE POST-RESET BOUNDARY CONTROL (SL_BG_TF_POST_RESET) ---------
 *
 * WHAT IS BEING MEASURED, AND THE REAL NONZERO PATH IS WHERE IT IS MEASURED.
 * The control adds calls at ONE point - after the prepass draw was issued and
 * its restoration ran, before this function returns to the caller that
 * rasterises - and it is independent of the zero-count arm and of every
 * empty-draw row, so the scenario below drives the arm the shipping question is
 * about: SL_BG_TF_CLIP=1 alone, a prepass that transforms every vertex with
 * feedback active and discard on, a written pool, and the boundary after all of
 * it. The composition with an empty-draw row is covered by the es scenario,
 * which is arm-aware.
 *
 * FOUR CLAIMS, AND THEY FAIL IN DIFFERENT WAYS:
 *
 *   - exactly the arm's own calls are issued, at the boundary and nowhere else,
 *     in the fixed order, with NO glUseProgram added;
 *   - the three conditions AT the boundary draw are the ones the placement
 *     guarantees - no scope open, discard off, program 0 current - measured from
 *     the stub's sample taken inside glDrawArrays, because state read afterwards
 *     describes the return state instead;
 *   - the prepass bracket, the pool and the caller's return state are the ones
 *     the pass has without the control, counters included;
 *   - no GL error state is consumed on any arm. */

/* THE ORDER THE ARMS MUST BE FOUND IN, as transcript indices. The claim is
 * ordinal, so the check is "the end of the feedback scope, the common disable
 * and the common switch to 0 all come strictly BEFORE the boundary's calls, and
 * the draw comes strictly before the wait" - which needs positions and not
 * counts. A kind that occurs more than once in the window is found by its LAST
 * occurrence, since the boundary is the last thing the bracket does. */
static long pr_last_of(long from, int kind)
{
    long i, found = -1;
    for (i = from; i < tf.nlog; i++)
        if (tf.log[i].kind == kind) found = i;
    return found;
}

static void pr_table(void)
{
    int m, i, j;

    begin_case("post-reset boundary: the decoder answers the unchanged path for OFF, "
               "names every arm with its own selector token, and describes every arm "
               "DISTINCTLY");
    {
        int d = -1, fin = -1;
        sl_bgtf_pr_calls(SL_BGTF_PR_OFF, &d, &fin);
        CHECK(d == 0 && fin == 0,
              "OFF issues NEITHER call, which is the unchanged path (%d/%d)",
              d, fin);
        CHECK(strcmp(sl_bgtf_pr_name(SL_BGTF_PR_OFF), "OFF") == 0,
              "and it is named OFF (%s)", sl_bgtf_pr_name(SL_BGTF_PR_OFF));
    }
    /* EVERY ARM, including the sentinels either side, so an out-of-range value
     * is covered by the same loop that covers the arms. */
    for (m = -3; m <= SL_BGTF_PR_BOTH + 3; m++) {
        int d = -1, fin = -1;
        sl_bgtf_pr_calls(m, &d, &fin);
        CHECK((d == 0 || d == 1) && (fin == 0 || fin == 1),
              "arm %d decodes to two 0/1 answers (%d/%d)", m, d, fin);
        CHECK(sl_bgtf_pr_name(m) != NULL,
              "and has a name that is never NULL (arm %d)", m);
        if (m <= 0 || m > SL_BGTF_PR_BOTH)
            CHECK(d == 0 && fin == 0 && strcmp(sl_bgtf_pr_name(m),
                                               m == 0 ? "OFF" : "?") == 0,
                  "and arm %d, which is not one of them, answers the UNCHANGED "
                  "path rather than a default arm (%d/%d, %s)", m, d, fin,
                  sl_bgtf_pr_name(m));
    }
    {
        int d, fin;
        sl_bgtf_pr_calls(SL_BGTF_PR_EMPTY, &d, &fin);
        CHECK(d == 1 && fin == 0,
              "POST_PROGRAM0_EMPTY issues the empty draw and no wait (%d/%d)",
              d, fin);
        sl_bgtf_pr_calls(SL_BGTF_PR_FINISH, &d, &fin);
        CHECK(d == 0 && fin == 1,
              "POST_FINISH issues the wait and no draw (%d/%d)", d, fin);
        sl_bgtf_pr_calls(SL_BGTF_PR_BOTH, &d, &fin);
        CHECK(d == 1 && fin == 1, "BOTH issues both (%d/%d)", d, fin);
        /* THE NAMES ARE THE SELECTOR TOKENS, which is what makes a record and a
         * launch spell an arm the same way - and is what the parser compares
         * against, so this case covers the grammar's vocabulary too. */
        CHECK(strcmp(sl_bgtf_pr_name(SL_BGTF_PR_EMPTY),
                     "POST_PROGRAM0_EMPTY") == 0
              && strcmp(sl_bgtf_pr_name(SL_BGTF_PR_FINISH), "POST_FINISH") == 0
              && strcmp(sl_bgtf_pr_name(SL_BGTF_PR_BOTH), "BOTH") == 0,
              "and the three arms are named by their own selector tokens "
              "(%s/%s/%s)", sl_bgtf_pr_name(SL_BGTF_PR_EMPTY),
              sl_bgtf_pr_name(SL_BGTF_PR_FINISH),
              sl_bgtf_pr_name(SL_BGTF_PR_BOTH));
        CHECK(strcmp(sl_bgtf_pr_name(99), "?") == 0,
              "while a value outside them has no name (%s)",
              sl_bgtf_pr_name(99));
    }
    /* EVERY ARM IS DESCRIBED DISTINCTLY by the two call answers together, so the
     * pair of issued-call counts names the arm from the commands alone. Two arms
     * sharing both would be indistinguishable in any record built from issued
     * commands. */
    for (i = 0; i <= SL_BGTF_PR_BOTH; i++) {
        for (j = i + 1; j <= SL_BGTF_PR_BOTH; j++) {
            int id, ifin, jd, jfin;
            sl_bgtf_pr_calls(i, &id, &ifin);
            sl_bgtf_pr_calls(j, &jd, &jfin);
            CHECK(id != jd || ifin != jfin,
                  "arms %s and %s differ in the calls they issue, so the "
                  "commands name the arm", sl_bgtf_pr_name(i),
                  sl_bgtf_pr_name(j));
        }
    }
    end_case();
}

/* THE WHOLE EXPECTED BRACKET OF THE REAL NONZERO PATH, IN ORDER, with the
 * boundary's own records appended from the arm's decoder. Written out here
 * rather than borrowed from es_expect because this path's draw carries the full
 * vertex count and that one's never does - the two sequences differ in the one
 * record the comparison is about. */
static int pr_expect(struct expect *e, GLuint vbo, GLuint pool, GLuint prog)
{
    int pr_d = 0, pr_fin = 0;
    int n = 0;

    sl_bgtf_pr_calls(sl_bgtf_post_reset(), &pr_d, &pr_fin);

    n = so_push(e, n, TFGL_GENBUFFERS, 1, ANY, ANY, ANY, "the pool's name");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) pool, ANY, ANY,
                "bind to size it");
    n = so_push(e, n, TFGL_BUFFERDATA, GL_ARRAY_BUFFER, (long) NV * 16L,
                GL_STATIC_DRAW, 0, "four floats per vertex, no initial data");
    n = so_push(e, n, TFGL_GETBUFPARAM, GL_ARRAY_BUFFER, GL_BUFFER_SIZE,
                (long) NV * 16L, 0,
                "and the driver is asked whether it honoured the size");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                "and unbind it again");
    n = so_push(e, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY,
                "the prepass program");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_dmode"), ANY, ANY, ANY,
                "the depth case");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk1"), ANY, ANY, ANY,
                "the first coefficient");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk2"), ANY, ANY, ANY,
                "the second coefficient");
    n = so_push(e, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                "discard on for the whole prepass");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY, ANY,
                "the pass's own static vertex buffer");
    n = so_push(e, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR, (long) vbo,
                "three floats at the item's stride, out of that buffer");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                "unbound straight away");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
                "no stale element buffer across the bracket");
    n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                (long) pool, ANY, "the pool is bound at index 0");
    n = so_push(e, n, TFGL_BEGINTF, GL_POINTS, ANY, ANY, ANY,
                "the scope opens in GL_POINTS");
    /* THE REAL DRAW, WITH THE WHOLE POOL. This is the record that makes the
     * scenario the nonzero path rather than any zero-count arm. */
    n = so_push(e, n, TFGL_DRAWARRAYS, GL_POINTS, 0, (long) NV, (long) pool,
                "and every vertex is transformed into the recording pool");
    n = so_push(e, n, TFGL_ENDTF, ANY, ANY, ANY, ANY,
                "the scope closes - the pair is matched");
    n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0,
                ANY, "the indexed binding is released");
    n = so_push(e, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                "the common bracket's own disable");
    n = so_push(e, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY,
                "and the fixed pipeline comes back");
    /* AND THEN THE BOUNDARY, WHICH IS WHERE THE ARMS DIFFER AND THE ONLY PLACE
     * THEY DO. No glUseProgram appears between the record above and these: the
     * draw is issued under the program that restoration already made current,
     * which is why it is the boundary and not a bracket of its own. */
    if (pr_d)
        n = so_push(e, n, TFGL_DRAWARRAYS, GL_POINTS, 0, 0L, 0L,
                    "the boundary's EMPTY draw, under program 0 and with "
                    "nothing bound at index 0");
    if (pr_fin)
        n = so_push(e, n, TFGL_FINISH, ANY, ANY, ANY, ANY,
                    "and the boundary's one glFinish, after the draw on the arm "
                    "that issues both");
    return n;
}

#define PR_SERVED 5

static void pr_scenario(int mode)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo, pool = 0u, prog = 0u;
    long mark, calls_at_hit, use_program_before;
    long i_end, i_dis, i_up0, i_draw, i_fin;
    int pr_d = 0, pr_fin = 0;
    int s, i;

    sl_bgtf_pr_calls(mode, &pr_d, &pr_fin);

    begin_case("post-reset boundary: ONE arm's calls at the boundary of a REAL "
               "nonzero prepass, in order, with no program switch added, the prepass "
               "bracket and the written pool unchanged, and the caller's state "
               "restored at close");
    arm();
    CHECK(sl_bgtf_post_reset() == mode, "the arm answers %s (%s)",
          sl_bgtf_pr_name(mode), sl_bgtf_pr_name(sl_bgtf_post_reset()));
    /* THE ARM RUNS ON THE REAL NONZERO PATH, and that is asserted rather than
     * assumed: every claim below about a transformed pool depends on it, and it
     * is the whole point of this scenario as against the es one. */
    CHECK(sl_bgtf_zero_count() == 0 && sl_bgtf_raster_cpu() == 0
          && sl_bgtf_empty_state() == SL_BGTF_ES_OFF,
          "the zero-count arm, the original-raster control and every empty-draw "
          "row are ABSENT, so this is the real transform path (%d/%d/%s)",
          sl_bgtf_zero_count(), sl_bgtf_raster_cpu(),
          sl_bgtf_es_name(sl_bgtf_empty_state()));
    CHECK(sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0
          && sl_bgtf_private_vao() == 0,
          "and none of the three exclusive controls is either (%d/%d/%d)",
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    mark = tf.nlog;
    b = snap();
    use_program_before = tf.use_program;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0 && s < SLOTS, "a slot was taken (%d)", s);
    {
        const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
        const struct sl_bgtfgl_call *up = first_of(mark, TFGL_USEPROGRAM);
        CHECK(base != NULL, "a recording buffer was bound at index 0");
        pool = base != NULL ? (GLuint) base->c : 0u;
        CHECK(pool != 0u && pool != vbo,
              "and it is a buffer of the module's own, not the input (%u vs %u)",
              (unsigned) pool, (unsigned) vbo);
        CHECK(up != NULL, "and the common setup made a program current");
        prog = up != NULL ? (GLuint) up->a : 0u;
        CHECK(prog != 0u, "which is the prepass program and not 0 (%u)",
              (unsigned) prog);
    }

    /* THE WHOLE BRACKET, IN ORDER, with the boundary's records appended from the
     * arm's own decoder rather than from its name. */
    {
        struct expect e[40];
        const int n = pr_expect(e, vbo, pool, prog);
        seq_is(mark, e, n);
    }

    /* AND THE ORDER AT THE BOUNDARY AS POSITIONS, which is the claim the arms
     * exist to make and the one a count cannot carry. The three restoring
     * commands come strictly before whatever the arm issues, and the draw comes
     * strictly before the wait. */
    i_end = pr_last_of(mark, TFGL_ENDTF);
    i_dis = pr_last_of(mark, TFGL_DISABLE);
    i_up0 = pr_last_of(mark, TFGL_USEPROGRAM);
    i_draw = pr_last_of(mark, TFGL_DRAWARRAYS);
    i_fin = pr_last_of(mark, TFGL_FINISH);
    CHECK(i_end >= 0 && i_dis > i_end && i_up0 > i_dis,
          "the feedback scope ended, then discard was disabled, then program 0 "
          "was made current (%ld < %ld < %ld)", i_end, i_dis, i_up0);
    CHECK(tf.log[i_up0].a == 0L,
          "and the last program this bracket made current is 0 (%ld)",
          tf.log[i_up0].a);
    if (pr_d) {
        CHECK(i_draw > i_up0,
              "the boundary's empty draw is issued AFTER all three of those "
              "(%ld > %ld)", i_draw, i_up0);
        CHECK(tf.log[i_draw].c == 0L && tf.log[i_draw].d == 0L,
              "with a count of 0 and nothing bound at index 0 (%ld/%ld)",
              tf.log[i_draw].c, tf.log[i_draw].d);
    } else {
        CHECK(i_draw < i_up0,
              "no draw was issued after the restoration, this arm issuing none "
              "(%ld < %ld)", i_draw, i_up0);
    }
    if (pr_fin) {
        CHECK(i_fin > i_up0,
              "the boundary's glFinish is issued after the restoration too "
              "(%ld > %ld)", i_fin, i_up0);
        if (pr_d)
            CHECK(i_fin > i_draw,
                  "and AFTER the empty draw, which is the fixed order on the "
                  "arm that issues both (%ld > %ld)", i_fin, i_draw);
    } else {
        CHECK(i_fin < 0, "and no glFinish was issued at all (%ld)", i_fin);
    }
    /* NO PROGRAM SWITCH IS ADDED BY ANY ARM. The bracket's two switches are the
     * common setup's and the common restoration's, and the boundary adds
     * neither - which is what makes "the program is never switched there, and
     * never while a feedback scope is open" a property of the calls. */
    CHECK(tf.use_program - use_program_before == 2L,
          "the bracket made the program current exactly twice, the common "
          "setup's and the common restoration's, with NO switch added at the "
          "boundary (%ld)", tf.use_program - use_program_before);
    CHECK(tf.v_program_change_in_tf == 0,
          "and no program was changed inside an open feedback scope (%ld)",
          tf.v_program_change_in_tf);

    /* THE CONDITIONS AT THE BOUNDARY DRAW, from the stub's sample taken inside
     * glDrawArrays. The three calls before it put all three back, so this
     * measurement is unavailable anywhere else. */
    CHECK(tf.st_at_draw_seen == 1L + (long) pr_d,
          "%ld draw(s) were sampled in this bracket (%ld)", 1L + (long) pr_d,
          tf.st_at_draw_seen);
    if (pr_d) {
        CHECK(tf.st_at_draw_tf_active == 0,
              "AT THE BOUNDARY DRAW no feedback scope was open (%d)",
              tf.st_at_draw_tf_active);
        CHECK(tf.st_at_draw_discard == 0,
              "rasterizer discard was OFF (%d)", tf.st_at_draw_discard);
        CHECK(tf.st_at_draw_program == 0u,
              "and PROGRAM 0 was current, which the arm relies on and never "
              "sets itself (%u)", (unsigned) tf.st_at_draw_program);
        CHECK(tf.st_draws_tf_active == 1L && tf.st_draws_discard_on == 1L
              && tf.st_draws_program_nonzero == 1L,
              "while the PREPASS draw had all three the other way, so the two "
              "draws are distinguishable by their own conditions (%ld/%ld/%ld)",
              tf.st_draws_tf_active, tf.st_draws_discard_on,
              tf.st_draws_program_nonzero);
    }

    /* THE CALLS, AS COUNTS. The prepass's own draw carries the whole pool; the
     * boundary's carries none. */
    CHECK(tf.draw_arrays == 1L + (long) pr_d,
          "%ld draw(s) were ISSUED in total (%ld)", 1L + (long) pr_d,
          tf.draw_arrays);
    CHECK(tf.draw_vertices == (long) NV,
          "and the vertices asked for are the PREPASS draw's alone, the "
          "boundary's adding none (%ld, expected %ld)", tf.draw_vertices,
          (long) NV);
    CHECK(tf.finish_calls == (long) pr_fin,
          "the glFinish was issued %s (%ld)", pr_fin ? "once" : "NOT AT ALL",
          tf.finish_calls);
    CHECK(tf.begin_tf == 1L && tf.end_tf == 1L,
          "the feedback pair was issued once, matched (%ld/%ld)", tf.begin_tf,
          tf.end_tf);
    CHECK(tf.enables == 1L && tf.disables == 1L,
          "and discard was enabled and disabled exactly once each, by the "
          "common bracket alone (%ld/%ld)", tf.enables, tf.disables);
    /* NO ERROR STATE IS CONSUMED, ON ANY ARM. The control never calls glGetError
     * and makes no claim about one, so this is 0 and the diagnostic's own
     * accounting is untouched. */
    CHECK(tf.get_error_calls == 0,
          "no glGetError was called, so no error state was consumed and no "
          "error claim is available from this arm (%ld)", tf.get_error_calls);

    /* THE TWO STUB NOTES THE BOUNDARY'S DRAW ACCOUNTS FOR, and the one it does
     * not. Discard is off at it and program 0 is current, which are the
     * placement's own conditions and not defects - a draw over no vertices
     * produces no fragment whatever the discard state and reads no uniform
     * whatever the program. The RECORDING note is different: the shim raises it
     * only for a draw with a capture OPEN, and the boundary's has none, so it
     * stays 0 and no pool could have been written or overrun. The PREPASS draw
     * raises none of the three. */
    expect_draw_without_discard = (long) pr_d;
    expect_draw_without_program = (long) pr_d;
    CHECK(tf.v_draw_without_discard == expect_draw_without_discard
          && tf.v_draw_without_program == expect_draw_without_program,
          "the boundary's draw accounts for the discard and program notes and "
          "the PREPASS draw for neither (%ld/%ld, expected %d each)",
          tf.v_draw_without_discard, tf.v_draw_without_program, pr_d);
    CHECK(tf.v_draw_without_tf_buffer == 0 && tf.v_tf_overrun == 0,
          "and nothing recorded without a bound pool or overran one, the "
          "boundary's draw having no capture open (%ld/%ld)",
          tf.v_draw_without_tf_buffer, tf.v_tf_overrun);

    /* THE CALLER'S STATE AT CLOSE, WHICH NO ARM MAY MOVE. */
    CHECK(tf.discard == 0, "discard is off afterwards (%d)", tf.discard);
    CHECK(tf.current == 0, "no program is current (%u)", (unsigned) tf.current);
    CHECK(tf.tf_buf == 0u, "nothing is left recording (%u)",
          (unsigned) tf.tf_buf);
    CHECK(tf.arr_buf == 0u, "no array buffer is left bound (%u)",
          (unsigned) tf.arr_buf);
    CHECK(tf.elem_buf == 0u, "and no element buffer either (%u)",
          (unsigned) tf.elem_buf);
    CHECK(tf.vptr.set && tf.vptr.size == 3 && tf.vptr.stride == (GLsizei) STR
          && tf.vptr.buf == vbo && tf.vptr.off == 0,
          "and the position pointer is the prepass's own, unmoved by the "
          "boundary (set %d, size %d, stride %d, buf %u, off %ld)", tf.vptr.set,
          (int) tf.vptr.size, (int) tf.vptr.stride, (unsigned) tf.vptr.buf,
          tf.vptr.off);

    /* THE PREPASS'S OWN WORK IS COUNTED AND ITS POOL IS WRITTEN, which is the
     * difference from every zero-count arm and the thing an arm must not
     * disturb. */
    a = snap();
    CHECK(a.prepasses - b.prepasses == 1 && a.vertices - b.vertices == NV,
          "one prepass transformed every vertex (%lu/%lu)",
          a.prepasses - b.prepasses, a.vertices - b.vertices);
    CHECK(a.walks - b.walks == 1, "and the walk is credited (%lu)",
          a.walks - b.walks);
    CHECK(a.zc_setups == b.zc_setups && a.zc_calls == b.zc_calls
          && a.es_setups == b.es_setups && a.es_draws == b.es_draws
          && a.so_setups == b.so_setups && a.so_omitted == b.so_omitted,
          "and NOTHING was counted for any zero-count, empty-draw or "
          "state-only arm, none of which is in force here");
    CHECK(sl_bgtf_bind_clip(s) == 1 || snap().binds > a.binds,
          "the slot the prepass WROTE can be bound, unlike every empty arm's");
    a = snap();
    CHECK(a.binds - b.binds >= 1, "and the bind is counted (%lu)",
          a.binds - b.binds);

    /* THE CONTROL'S OWN NUMBERS. One bracket reached one boundary; the two
     * issued-call counts are the arm's own answers, which is what says the arm
     * EXECUTED at this prepass rather than only that it was selected. */
    CHECK(a.pr_setups - b.pr_setups == 1,
          "the boundary was reached once, with every precondition proved (%lu)",
          a.pr_setups - b.pr_setups);
    CHECK(a.pr_skips - b.pr_skips == 0,
          "and nothing was skipped: no precondition was unproved (%lu)",
          a.pr_skips - b.pr_skips);
    CHECK(a.pr_draws - b.pr_draws == (unsigned long) pr_d
          && a.pr_finishes - b.pr_finishes == (unsigned long) pr_fin,
          "the pair of issued-call counts is arm %s's own, which names the arm "
          "from the commands alone (%lu/%lu, expected %d/%d)",
          sl_bgtf_pr_name(mode), a.pr_draws - b.pr_draws,
          a.pr_finishes - b.pr_finishes, pr_d, pr_fin);
    CHECK(a.pr_draws_prog0 == a.pr_draws,
          "every boundary draw was made under program 0 (%lu vs %lu)",
          a.pr_draws_prog0, a.pr_draws);
    CHECK(a.pr_refusals == 0, "the arm was not refused anywhere (%lu)",
          a.pr_refusals);
    CHECK(a.pr_mode == (unsigned long) mode,
          "and the record carries the EFFECTIVE arm, %s (%lu)",
          sl_bgtf_pr_name(mode), a.pr_mode);

    /* A CACHE HIT HAS NO BOUNDARY, so no call is added for one and the key is
     * not invalidated to manufacture a bracket. */
    b = snap();
    calls_at_hit = tf.calls;
    mark = tf.nlog;
    for (i = 1; i < PR_SERVED; i++)
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == s,
              "served mesh %d is a cache HIT on the same slot", i);
    a = snap();
    CHECK(tf.calls == calls_at_hit && tf.nlog == mark,
          "the %d hits added no GL call and no transcript record at all "
          "(%ld -> %ld, %ld -> %ld)", PR_SERVED - 1, calls_at_hit, tf.calls,
          mark, tf.nlog);
    CHECK(a.pr_reuses - b.pr_reuses == (unsigned long) (PR_SERVED - 1),
          "each hit was counted as the control's own reuse (%lu)",
          a.pr_reuses - b.pr_reuses);
    CHECK(a.pr_setups == b.pr_setups && a.pr_draws == b.pr_draws
          && a.pr_finishes == b.pr_finishes && a.pr_skips == b.pr_skips,
          "and reached no boundary and issued nothing (%lu/%lu/%lu/%lu)",
          a.pr_setups - b.pr_setups, a.pr_draws - b.pr_draws,
          a.pr_finishes - b.pr_finishes, a.pr_skips - b.pr_skips);
    CHECK(a.pr_setups + a.pr_skips + a.pr_reuses
              == (unsigned long) PR_SERVED,
          "REACHED + SKIPPED + REUSES == the %d meshes served, so the account "
          "is complete (%lu + %lu + %lu)", PR_SERVED, a.pr_setups, a.pr_skips,
          a.pr_reuses);
    /* THE ARM'S WHOLE SIGNATURE, PRINTED, for the reason the zero-count and
     * state-only arms print theirs: the CHECK macro is silent on a pass, so a
     * runner comparing one arm's numbers against another's - or against the
     * value this round recorded - has nothing to read otherwise. The line is
     * the counters and the stub's own glFinish count together, so it carries
     * both what the module COUNTED and what the fake driver SAW, and a run in
     * which those disagreed would show it here rather than only inside a
     * failing assertion. */
    printf("    PRBOUNDARY arm=%s reached=%lu draws=%lu draws_prog0=%lu "
           "finishes=%lu skips=%lu reuses=%lu gl_draws=%ld gl_finishes=%ld\n",
           sl_bgtf_pr_name(mode), a.pr_setups, a.pr_draws, a.pr_draws_prog0,
           a.pr_finishes, a.pr_skips, a.pr_reuses, tf.draw_arrays,
           tf.finish_calls);
    end_case();
}

static void mode_pr(int mode)
{
    pr_table();
    pr_scenario(mode);
}

/* THE SELECTOR'S GRAMMAR, COUPLING, EXCLUSIVITY AND REFUSAL ACCOUNTING, one
 * setting per launch. The runner says which arm must be in force - OFF for every
 * refusing and every absent launch - and whether this launch asked for an arm
 * and did not get one. */
static void mode_env_post_reset(int expect_mode, int want_refusal)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("post-reset boundary selector: the grammar, the coupling to "
               "SL_BG_TF_CLIP alone, the four exclusions and the refusal accounting");
    CHECK(sl_bgtf_post_reset() == expect_mode,
          "the arm answers %s (%s)", sl_bgtf_pr_name(expect_mode),
          sl_bgtf_pr_name(sl_bgtf_post_reset()));
    /* PARSED ONCE FOR THE LAUNCH. Repeated reads must cost nothing and must not
     * drift: the value is a launch constant, which is what lets the boundary
     * below read it per served draw without a second getenv. */
    calls_before = tf.calls;
    for (i = 0; i < 64; i++)
        CHECK(sl_bgtf_post_reset() == expect_mode,
              "and answers the same on read %d", i);
    CHECK(tf.calls == calls_before,
          "reading it costs no GL call (%ld -> %ld)", calls_before, tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->pr_refusals == (want_refusal ? 1UL : 0UL),
          "the launch is counted as %s, exactly once (%lu)",
          want_refusal ? "a REFUSAL" : "no refusal", t->pr_refusals);
    CHECK(t->pr_mode == (unsigned long) expect_mode,
          "and the recorded EFFECTIVE arm is %s (%lu)",
          sl_bgtf_pr_name(expect_mode), t->pr_mode);
    /* A REFUSED OR ABSENT ARM DID NO WORK OF ITS OWN, whatever else this launch
     * selected, and ISSUED NO CALL: an arm that is not in force must leave every
     * one of its own counts at 0 and must not have added a synchronisation point
     * anywhere, so no record can read a selected variable as work. */
    if (expect_mode == SL_BGTF_PR_OFF) {
        CHECK(t->pr_setups == 0 && t->pr_draws == 0 && t->pr_finishes == 0
              && t->pr_draws_prog0 == 0 && t->pr_skips == 0
              && t->pr_reuses == 0,
              "and nothing at all was counted for an arm that is not in force");
        CHECK(tf.finish_calls == 0,
              "no glFinish was issued anywhere in this process (%ld)",
              tf.finish_calls);
        CHECK(pr_last_of(0, TFGL_FINISH) < 0,
              "and the ordered transcript holds no record of one (%ld)",
              pr_last_of(0, TFGL_FINISH));
    }
    /* THE OTHER CONTROLS ARE LEFT EXACTLY AS THEIR OWN CLAUSES DECIDED THEM.
     * This control stands down alone and reaches into no other control's answer,
     * which is the decision the header records; a launch that asked for one of
     * the four alongside an arm must still get whatever that one's own rules
     * give it. The runner's own expectations cover which that is. */
    CHECK(sl_bgtf_state_only() == sl_bgtf_state_only()
          && sl_bgtf_pos_input() == sl_bgtf_pos_input(),
          "the older controls answer stably beside this one (%d/%d/%d/%d)",
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao(),
          sl_bgtf_zero_count());
    end_case();
}

static void st_post_reset_off(void)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("post-reset boundary default off: the control is not selected, no call "
               "was added anywhere in this process, nothing was counted for it, and "
               "the ordinary arm did real work");
    CHECK(sl_bgtf_post_reset() == SL_BGTF_PR_OFF,
          "SL_BG_TF_POST_RESET is absent, so the control is off (%s)",
          sl_bgtf_pr_name(sl_bgtf_post_reset()));
    calls_before = tf.calls;
    for (i = 0; i < 64; i++) (void) sl_bgtf_post_reset();
    CHECK(tf.calls == calls_before,
          "and reading it costs no GL call (%ld -> %ld)", calls_before,
          tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->pr_setups == 0 && t->pr_draws == 0 && t->pr_finishes == 0
          && t->pr_draws_prog0 == 0 && t->pr_skips == 0 && t->pr_reuses == 0
          && t->pr_refusals == 0 && t->pr_mode == 0,
          "nothing was counted for it in the whole process (%lu/%lu/%lu/%lu/"
          "%lu/%lu/%lu/%lu)", t->pr_setups, t->pr_draws, t->pr_finishes,
          t->pr_draws_prog0, t->pr_skips, t->pr_reuses, t->pr_refusals,
          t->pr_mode);
    /* NO SYNCHRONISATION POINT WAS ADDED, AND THIS IS WHAT CARRIES IT. glFinish
     * is a real symbol in this shim now - it has to be, because the control
     * calls it directly - so the link no longer refuses one and the claim rests
     * on the counter and on the transcript instead. Both are checked over the
     * WHOLE process, which has driven every other arm and every refusal above. */
    CHECK(tf.finish_calls == 0,
          "and NO glFinish was issued anywhere in this process (%ld)",
          tf.finish_calls);
    CHECK(pr_last_of(0, TFGL_FINISH) < 0,
          "with no record of one in the ordered transcript either (%ld)",
          pr_last_of(0, TFGL_FINISH));
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "while the ordinary arm ran prepasses over vertices (%lu / %lu)",
          t->prepasses, t->vertices);
    CHECK(t->binds >= 1, "and bound written pools (%lu)", t->binds);
    end_case();
}

/* ---- THE NEUTRAL-PROGRAM EMPTY-DRAW CONTROL (SL_BG_TF_NEUTRAL_PROG) ------
 *
 * WHAT IS BEING MEASURED. The control issues the zero-count arm's one empty
 * draw under a SECOND linked program instead of under the prepass program, with
 * row A's two conditions at the call. So the claims are:
 *
 *   - the program current AT the draw is the neutral one, which is measured from
 *     the stub's sample taken inside glDrawArrays because the restore puts the
 *     prepass program back one call later;
 *   - the count is 0 and the conditions are row A's - no feedback scope open,
 *     discard still on;
 *   - the two switches are issued in the right places and the bracket is handed
 *     to the common cleanup on the PREPASS program, not on the neutral one and
 *     not on 0;
 *   - a create or link failure REFUSES the served draw rather than issuing it
 *     under the prepass program, which would be row A under this control's name;
 *   - teardown deletes the program this control created and nothing else. */

#define NP_SERVED 5

/* Whether a name appears in the resolver record the shim is CURRENTLY holding -
 * that record is zeroed by sl_bgtfgl_reset like every other observation, so this
 * is a statement about the window since the last arm() and is read as one. */
static int np_asked_for(const char *name)
{
    int i;
    for (i = 0; i < tf.nasked; i++)
        if (tf.asked[i] != NULL && strcmp(tf.asked[i], name) == 0) return 1;
    return 0;
}

/* THE BRACKET THIS CONTROL ISSUES, IN ORDER. It is row A's bracket - the common
 * setup, no begin/end pair, discard left on - with exactly two records added:
 * one glUseProgram naming the NEUTRAL program immediately before the empty draw,
 * and one naming the PREPASS program immediately after it. The two names are
 * asserted exactly rather than as wildcards, which is what tells this bracket
 * from row A's, from row R's and from row E's.
 *
 * AND ONE MORE RECORD AT THE VERY FRONT, on the bracket that ACQUIRES. The
 * control's capability is taken before the cache is consulted and before any GL
 * state is written, and the last thing the acquire does on a successful link is
 * release its vertex shader - so a glDeleteShader naming that shader is the
 * first record of this window and appears nowhere else. `vs` is 0 for a bracket
 * served from an already-acquired context, which omits the record. */
static int np_expect(struct expect *e, GLuint vbo, GLuint pool, GLuint prog,
                     GLuint nprog, GLuint vs)
{
    int n = 0;

    if (vs != 0u)
        n = so_push(e, n, TFGL_DELETESHADER, (long) vs, ANY, ANY, ANY,
                    "THE ACQUIRE'S OWN RELEASE: the neutral program's vertex "
                    "shader, flagged immediately after the successful link, "
                    "before a single byte of bracket state is written");
    n = so_push(e, n, TFGL_GENBUFFERS, 1, ANY, ANY, ANY, "the pool's name");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) pool, ANY, ANY,
                "bind to size it");
    n = so_push(e, n, TFGL_BUFFERDATA, GL_ARRAY_BUFFER, (long) NV * 16L,
                GL_STATIC_DRAW, 0, "four floats per vertex, no initial data");
    n = so_push(e, n, TFGL_GETBUFPARAM, GL_ARRAY_BUFFER, GL_BUFFER_SIZE,
                (long) NV * 16L, 0,
                "and the driver is asked whether it honoured the size");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                "and unbind it again");
    n = so_push(e, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY,
                "the common setup's prepass program, unchanged");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_dmode"), ANY, ANY, ANY,
                "the depth case, written under the PREPASS program and never "
                "under the neutral one, which declares no uniform at all");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk1"), ANY, ANY, ANY,
                "the first coefficient");
    n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk2"), ANY, ANY, ANY,
                "the second coefficient");
    n = so_push(e, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                "discard on for the whole bracket, as row A has it");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY, ANY,
                "the pass's own static vertex buffer");
    n = so_push(e, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR, (long) vbo,
                "three floats at the item's stride, out of that buffer");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                "unbound straight away");
    n = so_push(e, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
                "no stale element buffer across the bracket");
    n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                (long) pool, ANY,
                "the pool is bound at index 0, which a T0 arm keeps");
    /* AND NO glBeginTransformFeedback: the pair is omitted together, exactly as
     * row A omits it, so no capture is open at either switch. */
    n = so_push(e, n, TFGL_USEPROGRAM, (long) nprog, ANY, ANY, ANY,
                "THE CONTROL'S ONE SWITCH: the NEUTRAL program, immediately "
                "before the draw and after every uniform was written");
    n = so_push(e, n, TFGL_DRAWARRAYS, GL_POINTS, 0, 0L, (long) pool,
                "EXACTLY glDrawArrays(GL_POINTS, 0, 0), with the pool still "
                "bound at index 0 and no capture open");
    n = so_push(e, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY,
                "THE CONTROL'S ONE RESTORE: back to the PREPASS program, not to "
                "0, which is the state the common cleanup has on every arm");
    n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0,
                ANY, "the indexed binding is released");
    n = so_push(e, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                "the common bracket's own disable");
    n = so_push(e, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY,
                "and the fixed pipeline comes back, unchanged");
    return n;
}

/* ONE SERVED DRAW UNDER THE CONTROL, then the cache cadence, then teardown. */
static void np_scenario(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo, pool = 0u, prog = 0u, nprog = 0u;
    long mark, calls_at_hit, use_program_before;
    int s, i;

    begin_case("neutral program: the zero-count arm's one empty draw issued under a "
               "newly linked program with NO feedback varyings, with row A's conditions "
               "at the call, the prepass program put back immediately after it and the "
               "caller's state restored at close");
    arm();
    /* BOTH DELETES RESOLVE, which is a precondition of the control rather than a
     * convenience: the acquire refuses outright without either one, and each of
     * those refusals has its own case below. */
    tf.provide_delete_program = 1;
    tf.provide_delete_shader = 1;
    CHECK(sl_bgtf_neutral_prog() == 1, "the control is in force (%d)",
          sl_bgtf_neutral_prog());
    CHECK(sl_bgtf_zero_count() == 1 && sl_bgtf_raster_cpu() == 1,
          "on the zero-count arm with the original-raster control, which is what "
          "makes the draw EMPTY (%d/%d)", sl_bgtf_zero_count(),
          sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_empty_state() == SL_BGTF_ES_OFF
          && sl_bgtf_post_reset() == SL_BGTF_PR_OFF
          && sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0
          && sl_bgtf_private_vao() == 0,
          "and with every control it is exclusive with ABSENT (%s/%s/%d/%d/%d)",
          sl_bgtf_es_name(sl_bgtf_empty_state()),
          sl_bgtf_pr_name(sl_bgtf_post_reset()), sl_bgtf_state_only(),
          sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    mark = tf.nlog;
    b = snap();
    use_program_before = tf.use_program;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0 && s < SLOTS, "a slot was taken (%d)", s);
    {
        const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
        const struct sl_bgtfgl_call *up = first_of(mark, TFGL_USEPROGRAM);
        CHECK(base != NULL, "a recording buffer was bound at index 0");
        pool = base != NULL ? (GLuint) base->c : 0u;
        CHECK(up != NULL, "and the common setup made a program current");
        prog = up != NULL ? (GLuint) up->a : 0u;
        /* THE NEUTRAL PROGRAM'S NAME IS THE ONE SAMPLED AT THE DRAW, taken from
         * the stub rather than from the module: a name read back out of the
         * transcript's switch would make "the switch named what the draw ran
         * under" true by construction. */
        nprog = tf.st_at_draw_program;
        CHECK(prog != 0u && nprog != 0u && nprog != prog,
              "and the program at the draw is a SECOND one, neither 0 nor the "
              "prepass program (prepass %u, at the draw %u)", (unsigned) prog,
              (unsigned) nprog);
    }

    /* THE WHOLE BRACKET, IN ORDER, with the two added switches named exactly
     * and the acquire's shader release at the front of it. */
    {
        struct expect e[40];
        const int n = np_expect(e, vbo, pool, prog, nprog, tf.last_shader);
        seq_is(mark, e, n);
    }

    /* THE CONDITIONS AT THE DRAW, WHICH ARE ROW A'S. Taken from the sample
     * inside glDrawArrays, because the restore and the cleanup put the program
     * and the discard state back immediately afterwards. */
    CHECK(tf.st_at_draw_seen == 1L,
          "exactly one draw was sampled in this bracket (%ld)",
          tf.st_at_draw_seen);
    CHECK(tf.st_at_draw_tf_active == 0,
          "AT THE DRAW no feedback scope was open, which is row A's T0 (%d)",
          tf.st_at_draw_tf_active);
    CHECK(tf.st_at_draw_discard == 1,
          "rasterizer discard was still ON, which is row A's D1 (%d)",
          tf.st_at_draw_discard);
    CHECK(tf.st_at_draw_program == nprog && nprog != prog,
          "and the NEUTRAL program was current, not the prepass program (%u vs "
          "%u)", (unsigned) tf.st_at_draw_program, (unsigned) prog);
    CHECK(tf.draw_arrays == 1L && tf.draw_vertices == 0L,
          "the call was issued exactly once and submitted ZERO vertices "
          "(%ld/%ld)", tf.draw_arrays, tf.draw_vertices);

    /* THE SECOND PROGRAM WAS BUILT WITHOUT A RECORDED VARYING, which is the one
     * declared difference from the prepass program. The prepass program's own
     * link named one, so the count of naming calls stays at ONE for the process
     * while two programs were linked. */
    CHECK(tf.create_program == 2L && tf.link == 2L,
          "two programs were created and linked in this context (%ld/%ld)",
          tf.create_program, tf.link);
    CHECK(tf.tf_varyings == 1L,
          "and glTransformFeedbackVaryings was called for ONE of them - the "
          "prepass program - so the neutral program captures nothing (%ld)",
          tf.tf_varyings);
    CHECK(tf.tf_varying_prog == prog,
          "and it named the PREPASS program (%u vs %u)",
          (unsigned) tf.tf_varying_prog, (unsigned) prog);
    CHECK(tf.varyings_after_link == 0,
          "with no varying named after a link anywhere (%ld)",
          tf.varyings_after_link);

    /* THE OBJECT LIFETIME OF THE ACQUIRE'S OWN SHADER, counted by NAME.
     * Two shaders were created in this context: the prepass program's, which
     * this module leaves to ordinary context destruction, and this control's,
     * which it must release itself. Exactly one glDeleteShader is therefore
     * expected here, and it must name the control's.
     *
     * WHY THE DELETE IS CORRECT ON THE SUCCESS PATH. The shader is attached to
     * a program that has just linked, so the call FLAGS it rather than freeing
     * it: the program keeps working and the object goes away with the program.
     * Nothing outside the acquire names that shader again, so issuing the call
     * there is the only point at which the object can be accounted for at all. */
    CHECK(tf.create_shader == 2L,
          "two vertex shaders were created in this context - the prepass "
          "program's and this control's (%ld)", tf.create_shader);
    CHECK(tf.delete_shader == 1L,
          "and exactly ONE glDeleteShader was issued, on the successful link "
          "path, so the control's shader is not left behind (%ld)",
          tf.delete_shader);
    CHECK(tf.last_deleted_shader == tf.last_shader,
          "and it named the shader the acquire created, which is the last one "
          "minted (%u vs %u)", (unsigned) tf.last_deleted_shader,
          (unsigned) tf.last_shader);
    CHECK(tf.live_shaders == 1L,
          "leaving ONE shader alive - the prepass program's, which this module "
          "deliberately leaves to context destruction (%ld)", tf.live_shaders);
    CHECK(tf.live_programs == 2L,
          "and both programs still alive before teardown (%ld)",
          tf.live_programs);
    CHECK(tf.delete_program == 0L,
          "with no program deleted yet - the link succeeded, so nothing was "
          "deleted during the acquire (%ld)", tf.delete_program);
    CHECK(tf.v_double_delete == 0 && tf.v_delete_unknown_shader == 0
          && tf.v_delete_unknown_program == 0,
          "and no object was deleted twice and none deleted that was never "
          "created (%ld/%ld/%ld)", tf.v_double_delete,
          tf.v_delete_unknown_shader, tf.v_delete_unknown_program);
    {
        const struct sl_bgtf_counters *t = sl_bgtf_stats();
        CHECK(t->np_shader_deletes == 1UL,
              "the module counts the shader release it issued (%lu)",
              t->np_shader_deletes);
        CHECK(t->np_fail_prog_deletes == 0UL,
              "and no failed-link program was deleted, no link having failed "
              "(%lu)", t->np_fail_prog_deletes);
    }

    /* NO INVALID CALL, AND NO UNIFORM UNDER THE NEUTRAL PROGRAM. */
    CHECK(tf.begin_tf == 0L && tf.end_tf == 0L,
          "the begin/end pair was omitted TOGETHER, as row A omits it (%ld/%ld)",
          tf.begin_tf, tf.end_tf);
    CHECK(tf.v_program_change_in_tf == 0,
          "so neither switch was made inside an open feedback scope (%ld)",
          tf.v_program_change_in_tf);
    CHECK(tf.uniform_rejected == 0,
          "every uniform was written to a program that was current, so none was "
          "written under the neutral program (%ld)", tf.uniform_rejected);
    {
        int u, under_neutral = 0;
        for (u = 0; u < tf.nu; u++) if (tf.u[u].prog == nprog) under_neutral++;
        CHECK(under_neutral == 0,
              "and no accepted uniform write landed in the neutral program at "
              "all (%d)", under_neutral);
    }
    CHECK(tf.use_program - use_program_before == 4L,
          "the bracket made the program current four times: the common setup's, "
          "this control's switch and restore, and the common cleanup's (%ld)",
          tf.use_program - use_program_before);
    CHECK(tf.enables == 1L && tf.disables == 1L,
          "discard was enabled and disabled once each, by the common bracket "
          "alone (%ld/%ld)", tf.enables, tf.disables);
    CHECK(tf.finish_calls == 0L,
          "and no glFinish was issued - this control adds no synchronisation "
          "point (%ld)", tf.finish_calls);
    CHECK(tf.get_error_calls == 0,
          "no glGetError was called, so no error state was consumed (%ld)",
          tf.get_error_calls);

    /* THE CALLER'S STATE AT CLOSE, which this control may not move. */
    CHECK(tf.discard == 0 && tf.current == 0 && tf.tf_buf == 0u
          && tf.arr_buf == 0u && tf.elem_buf == 0u,
          "discard is off, no program is current, nothing is recording and no "
          "buffer is left bound (%d/%u/%u/%u/%u)", tf.discard,
          (unsigned) tf.current, (unsigned) tf.tf_buf, (unsigned) tf.arr_buf,
          (unsigned) tf.elem_buf);
    CHECK(tf.vptr.set && tf.vptr.buf == vbo && tf.vptr.stride == (GLsizei) STR,
          "and the position pointer is the prepass's own (set %d, buf %u, "
          "stride %d)", tf.vptr.set, (unsigned) tf.vptr.buf,
          (int) tf.vptr.stride);

    /* THE WORK COUNTERS. The control runs on the zero-count arm and claims no
     * work, so the candidate's own numbers are untouched. */
    a = snap();
    CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices
          && a.walks == b.walks && a.binds == b.binds
          && a.raster_draws == b.raster_draws,
          "NO prepass, vertex, walk, bind or submission was counted "
          "(%lu/%lu/%lu/%lu/%lu)", a.prepasses - b.prepasses,
          a.vertices - b.vertices, a.walks - b.walks, a.binds - b.binds,
          a.raster_draws - b.raster_draws);
    CHECK(a.zc_setups - b.zc_setups == 1 && a.zc_calls - b.zc_calls == 1,
          "the zero-count arm's own cadence is still counted beneath this "
          "control (%lu/%lu)", a.zc_setups - b.zc_setups,
          a.zc_calls - b.zc_calls);
    CHECK(a.es_setups == b.es_setups && a.es_draws == b.es_draws
          && a.pr_setups == b.pr_setups && a.so_setups == b.so_setups,
          "and nothing for any control this one is exclusive with, none of which "
          "is in force");
    CHECK(a.np_setups - b.np_setups == 1 && a.np_draws - b.np_draws == 1,
          "one bracket issued one empty draw under this control (%lu/%lu)",
          a.np_setups - b.np_setups, a.np_draws - b.np_draws);
    CHECK(a.np_draws_neutral - b.np_draws_neutral == 1
          && a.np_draws_neutral == a.np_draws,
          "and every one of them was under the NEUTRAL program (%lu vs %lu)",
          a.np_draws_neutral, a.np_draws);
    CHECK(a.np_switches - b.np_switches == 1
          && a.np_restores - b.np_restores == 1
          && a.np_switches == a.np_restores,
          "the switch and the restore are counted once each and are EQUAL, so "
          "the bracket was not handed on under the neutral program (%lu/%lu)",
          a.np_switches, a.np_restores);
    CHECK(a.np_creates == 1 && a.np_create_fails == 0 && a.np_skips == 0,
          "one program was created, with no failure and no skipped draw "
          "(%lu/%lu/%lu)", a.np_creates, a.np_create_fails, a.np_skips);
    CHECK(a.np_refusals == 0 && a.refusals == b.refusals,
          "the control was not refused and no served draw was (%lu/%lu)",
          a.np_refusals, a.refusals - b.refusals);

    /* THE SLOT IS UNWRITTEN AND UNBINDABLE: an empty call wrote no output. */
    b = snap();
    calls_at_hit = tf.calls;
    CHECK(sl_bgtf_bind_clip(s) == -1,
          "the slot this empty draw keyed is REFUSED by the binding");
    CHECK(snap().binds == b.binds && tf.calls == calls_at_hit,
          "and the refusal cost no bind and no GL call");

    /* A CACHE HIT HAS NO EMPTY DRAW, so no switch of any kind is issued for one
     * and the key is not invalidated to manufacture a bracket. */
    b = snap();
    calls_at_hit = tf.calls;
    mark = tf.nlog;
    for (i = 1; i < NP_SERVED; i++)
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == s,
              "served mesh %d is a cache HIT on the same slot", i);
    a = snap();
    CHECK(tf.calls == calls_at_hit && tf.nlog == mark,
          "the %d hits added no GL call and no transcript record (%ld -> %ld)",
          NP_SERVED - 1, calls_at_hit, tf.calls);
    CHECK(a.np_reuses - b.np_reuses == (unsigned long) (NP_SERVED - 1),
          "each hit was counted as this control's own reuse (%lu)",
          a.np_reuses - b.np_reuses);
    CHECK(a.np_setups == b.np_setups && a.np_draws == b.np_draws
          && a.np_switches == b.np_switches && a.np_restores == b.np_restores,
          "and reached no bracket and issued nothing (%lu/%lu/%lu/%lu)",
          a.np_setups - b.np_setups, a.np_draws - b.np_draws,
          a.np_switches - b.np_switches, a.np_restores - b.np_restores);
    CHECK(a.np_setups + a.np_skips + a.np_reuses == (unsigned long) NP_SERVED,
          "REACHED + SKIPPED + REUSES == the %d meshes served, so the account is "
          "complete (%lu + %lu + %lu)", NP_SERVED, a.np_setups, a.np_skips,
          a.np_reuses);

    /* AND TEARDOWN DELETES THE PROGRAM THIS CONTROL CREATED, NAMED. Two programs
     * exist in this context and the module owns exactly one of them, so the
     * assertion is on the NAME and not only on the count: deleting the prepass
     * program would be a defect the count alone would pass. */
    b = snap();
    mark = tf.nlog;
    sl_bgtf_shutdown();
    a = snap();
    CHECK(tf.delete_program == 1L,
          "exactly one glDeleteProgram was issued at teardown (%ld)",
          tf.delete_program);
    CHECK(tf.last_deleted_program == nprog,
          "and it named the NEUTRAL program, not the prepass program (%u, "
          "neutral %u, prepass %u)", (unsigned) tf.last_deleted_program,
          (unsigned) nprog, (unsigned) prog);
    CHECK(tf.v_delete_current_program == 0,
          "with no program current at the call (%ld)",
          tf.v_delete_current_program);
    CHECK(a.np_deletes - b.np_deletes == 1 && a.np_deletes == a.np_creates,
          "the delete is counted and matches the create (%lu vs %lu)",
          a.np_deletes, a.np_creates);
    CHECK(tf.nlog == mark + 1,
          "and teardown added exactly that one record and nothing else (%ld)",
          tf.nlog - mark);
    /* AND A SECOND CALL DELETES NOTHING TWICE. */
    sl_bgtf_shutdown();
    CHECK(tf.delete_program == 1L,
          "a second teardown issues no further delete (%ld)",
          tf.delete_program);
    CHECK(tf.delete_shader == 1L,
          "and no further glDeleteShader either - the shader was released in "
          "the acquire and teardown has nothing to do with it (%ld)",
          tf.delete_shader);
    /* THE WHOLE-RUN LIFETIME ACCOUNT, BY NAME. Four objects were created in
     * this context: two shaders and two programs. This module owns exactly one
     * of each - the neutral control's - and the other two are the prepass
     * program and its shader, which it deliberately leaves to context
     * destruction. So after two teardowns: one delete of each kind, each naming
     * the control's own object, nothing deleted twice, nothing deleted that was
     * never created, and exactly the prepass pair still alive. */
    CHECK(tf.create_shader == 2L && tf.create_program == 2L,
          "two shaders and two programs were created in all (%ld/%ld)",
          tf.create_shader, tf.create_program);
    CHECK(tf.delete_shader == 1L && tf.delete_program == 1L,
          "and exactly one of each was deleted (%ld/%ld)", tf.delete_shader,
          tf.delete_program);
    CHECK(tf.v_double_delete == 0,
          "with nothing deleted twice (%ld)", tf.v_double_delete);
    CHECK(tf.v_delete_unknown_shader == 0 && tf.v_delete_unknown_program == 0,
          "and nothing deleted that this context never handed out (%ld/%ld)",
          tf.v_delete_unknown_shader, tf.v_delete_unknown_program);
    CHECK(tf.live_shaders == 1L && tf.live_programs == 1L,
          "leaving the prepass program and its shader alive, which is the pair "
          "this module leaves to context destruction (%ld/%ld)",
          tf.live_shaders, tf.live_programs);
    {
        const struct sl_bgtf_counters *t = sl_bgtf_stats();
        CHECK(t->np_deletes == 1UL && t->np_shader_deletes == 1UL
              && t->np_fail_prog_deletes == 0UL,
              "and the module's own account agrees: one program delete at "
              "teardown, one shader release in the acquire, no failed-link "
              "program (%lu/%lu/%lu)", t->np_deletes, t->np_shader_deletes,
              t->np_fail_prog_deletes);
        CHECK(t->np_shader_deletes == t->np_creates
              && t->np_deletes == t->np_creates,
              "each equal to the one program this control created (%lu/%lu vs "
              "%lu)", t->np_shader_deletes, t->np_deletes, t->np_creates);
    }
    /* THE CONTROL'S WHOLE SIGNATURE, PRINTED, for the reason the zero-count,
     * state-only and post-reset arms print theirs: the CHECK macro is silent on
     * a pass, so a runner comparing this run's numbers against the value the
     * round recorded has nothing to read otherwise. The line carries the
     * module's own counts and the fake driver's draw and delete counts
     * together, so a run in which those disagreed would show it here rather
     * than only inside a failing assertion. */
    a = snap();
    printf("    NPDRAW setups=%lu draws=%lu draws_neutral=%lu switches=%lu "
           "restores=%lu reuses=%lu skips=%lu creates=%lu create_fails=%lu "
           "deletes=%lu shader_deletes=%lu fail_prog_deletes=%lu "
           "gl_draws=%ld gl_deletes=%ld gl_shader_deletes=%ld "
           "gl_live_sh=%ld gl_live_pr=%ld gl_double=%ld\n",
           a.np_setups, a.np_draws, a.np_draws_neutral, a.np_switches,
           a.np_restores, a.np_reuses, a.np_skips, a.np_creates,
           a.np_create_fails, a.np_deletes, a.np_shader_deletes,
           a.np_fail_prog_deletes, tf.draw_arrays,
           tf.delete_program, tf.delete_shader, tf.live_shaders,
           tf.live_programs, tf.v_double_delete);
    end_case();
}

/* A CAPABILITY FAILURE REFUSES THE SERVED DRAW AND NEVER FALLS BACK TO ROW A.
 * Each shape is driven in its own context, because the acquire LATCHES per
 * context, and each must leave the draw unissued rather than issued under the
 * prepass program. `which` names the shape so a failure says which one. */
static void np_fail_shape(const char *which, int gate_delete, int gate_delete_sh,
                          int create_zero,
                          int fail_compile, int fail_link)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    long draws_before, shaders_before, sh_deletes_before, pr_deletes_before;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_delete_program = gate_delete;
    tf.provide_delete_shader = gate_delete_sh;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "%s: the walk is "
          "admitted", which);
    /* THE FAULT IS INJECTED AFTER THE PREPASS PROGRAM WAS BUILT, so what fails
     * is this control's own acquire and not the module's capability. */
    tf.create_program_zero = create_zero;
    tf.fail_compile = fail_compile;
    tf.fail_link = fail_link;
    b = snap();
    draws_before = tf.draw_arrays;
    shaders_before = tf.create_shader;
    sh_deletes_before = tf.delete_shader;
    pr_deletes_before = tf.delete_program;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    tf.create_program_zero = 0;
    tf.fail_compile = 0;
    tf.fail_link = 0;
    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(tf.draw_arrays == draws_before,
          "%s: and NO draw was issued - not under the neutral program and not "
          "under the prepass program, which would be row A (%ld)", which,
          tf.draw_arrays - draws_before);
    CHECK(a.np_create_fails - b.np_create_fails == 1,
          "%s: the failure is counted as this control's own (%lu)", which,
          a.np_create_fails - b.np_create_fails);
    CHECK(a.np_creates == b.np_creates && a.np_setups == b.np_setups
          && a.np_draws == b.np_draws && a.np_draws_neutral == b.np_draws_neutral
          && a.np_switches == b.np_switches,
          "%s: and NOTHING claims a neutral program or a neutral draw", which);
    CHECK(a.refusals - b.refusals == 1,
          "%s: the module counts the refused draw (%lu)", which,
          a.refusals - b.refusals);

    /* AND NOTHING IT CREATED SURVIVES THE REFUSAL, counted by name.
     *
     * The two missing-entry-point shapes must create NOTHING AT ALL: both
     * releases are resolved before the first create for exactly that reason, so
     * a context that cannot release is refused with no object made. The other
     * three get as far as a shader, and each releases it - the compile failure
     * and the no-program case free it outright, the link failure flags it and
     * deletes the program that would otherwise hold it. Only the link failure
     * deletes a program, and it does so during the ACQUIRE, while the context is
     * current, which is why teardown then has nothing to delete. */
    {
        const int made_shader = (gate_delete && gate_delete_sh);
        const long want_sh = made_shader ? 1L : 0L;
        const long want_pr = (made_shader && !create_zero && fail_link)
                             ? 1L : 0L;
        CHECK(tf.create_shader - shaders_before == want_sh,
              "%s: %ld shader object(s) were created (%ld)", which, want_sh,
              tf.create_shader - shaders_before);
        CHECK(tf.delete_shader - sh_deletes_before == want_sh,
              "%s: and exactly as many glDeleteShader calls were issued - every "
              "shader created here was released (%ld vs %ld)", which,
              tf.delete_shader - sh_deletes_before, want_sh);
        CHECK(tf.delete_program - pr_deletes_before == want_pr,
              "%s: and %ld glDeleteProgram call(s) - a program that failed to "
              "LINK is deleted here, where the context is current, and no other "
              "shape creates a program to delete (%ld)", which, want_pr,
              tf.delete_program - pr_deletes_before);
        CHECK(a.np_shader_deletes - b.np_shader_deletes
              == (unsigned long) want_sh
              && a.np_fail_prog_deletes - b.np_fail_prog_deletes
                 == (unsigned long) want_pr,
              "%s: and the module's own account agrees (%lu/%lu)", which,
              a.np_shader_deletes - b.np_shader_deletes,
              a.np_fail_prog_deletes - b.np_fail_prog_deletes);
        CHECK(tf.v_double_delete == 0 && tf.v_delete_unknown_shader == 0
              && tf.v_delete_unknown_program == 0,
              "%s: nothing was deleted twice and nothing deleted that was never "
              "created (%ld/%ld/%ld)", which, tf.v_double_delete,
              tf.v_delete_unknown_shader, tf.v_delete_unknown_program);
        CHECK(tf.live_shaders == 1L && tf.live_programs == 1L,
              "%s: and the only objects still alive are the PREPASS program and "
              "its shader, which this module leaves to context destruction - so "
              "the refused acquire leaked nothing (%ld/%ld)", which,
              tf.live_shaders, tf.live_programs);
    }
    /* AND THE FAILURE LATCHES: a second served draw compiles nothing again. */
    {
        const long compiles = tf.compile, creates = tf.create_program;
        const long sdels = tf.delete_shader, pdels = tf.delete_program;
        b = snap();
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "%s: a second served draw is refused too", which);
        CHECK(tf.compile == compiles && tf.create_program == creates,
              "%s: without compiling or creating anything again (%ld/%ld)",
              which, tf.compile - compiles, tf.create_program - creates);
        CHECK(tf.delete_shader == sdels && tf.delete_program == pdels,
              "%s: and without deleting anything again either, there being "
              "nothing left to delete (%ld/%ld)", which,
              tf.delete_shader - sdels, tf.delete_program - pdels);
        a = snap();
        CHECK(a.np_create_fails - b.np_create_fails == 1,
              "%s: and the second refusal is counted too (%lu)", which,
              a.np_create_fails - b.np_create_fails);
    }
    /* AND TEARDOWN DELETES NOTHING, there being no SUCCESSFULLY LINKED program
     * to delete. The link-failure shape is the one worth reading twice: its
     * program was deleted by the acquire and its name cleared there, so teardown
     * must not delete it a second time. A second teardown is driven for the same
     * reason. */
    {
        const long dels = tf.delete_program, sdels = tf.delete_shader;
        sl_bgtf_shutdown();
        sl_bgtf_shutdown();
        CHECK(tf.delete_program == dels && tf.delete_shader == sdels,
              "%s: two teardowns delete nothing, every object this acquire made "
              "having already been released inside it (%ld/%ld)", which,
              tf.delete_program - dels, tf.delete_shader - sdels);
        CHECK(tf.v_double_delete == 0,
              "%s: and in particular a failed-link program is not deleted twice "
              "(%ld)", which, tf.v_double_delete);
        CHECK(tf.live_shaders == 1L && tf.live_programs == 1L,
              "%s: with the prepass pair the only objects left alive (%ld/%ld)",
              which, tf.live_shaders, tf.live_programs);
    }
}

static void np_failures(void)
{
    begin_case("neutral program, FAILING CLOSED: a missing glDeleteProgram, a missing "
               "glDeleteShader, a shader "
               "that will not compile, a context that hands back no program and a link "
               "that fails each REFUSE the served draw, latch, release every object "
               "they did create, and never issue the draw "
               "under the prepass program");
    /* NO DELETE ENTRY POINT: the control declines to create an object it could
     * never release, which is the only shape that fails BEFORE glCreateShader -
     * and it is driven once per missing release, because the two are resolved
     * independently and either one alone must refuse. */
    np_fail_shape("no glDeleteProgram", 0, 1, 0, 0, 0);
    np_fail_shape("no glDeleteShader", 1, 0, 0, 0, 0);
    np_fail_shape("the shader will not compile", 1, 1, 0, 1, 0);
    np_fail_shape("glCreateProgram gives no object", 1, 1, 1, 0, 0);
    np_fail_shape("the link fails", 1, 1, 0, 0, 1);
    end_case();
}

static void st_neutral_prog_off(void)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("neutral program default off: the control is not selected, no second "
               "program was created anywhere in this process, no delete entry point was "
               "resolved, nothing was counted for it, and the ordinary arm did real work");
    CHECK(sl_bgtf_neutral_prog() == 0,
          "SL_BG_TF_NEUTRAL_PROG is absent, so the control is off (%d)",
          sl_bgtf_neutral_prog());
    calls_before = tf.calls;
    for (i = 0; i < 64; i++) (void) sl_bgtf_neutral_prog();
    CHECK(tf.calls == calls_before,
          "and reading it costs no GL call (%ld -> %ld)", calls_before,
          tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->np_setups == 0 && t->np_draws == 0 && t->np_draws_neutral == 0
          && t->np_switches == 0 && t->np_restores == 0 && t->np_reuses == 0
          && t->np_skips == 0 && t->np_creates == 0 && t->np_create_fails == 0
          && t->np_deletes == 0 && t->np_shader_deletes == 0
          && t->np_fail_prog_deletes == 0 && t->np_refusals == 0,
          "nothing was counted for it in the whole process "
          "(%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu)",
          t->np_setups, t->np_draws, t->np_draws_neutral, t->np_switches,
          t->np_restores, t->np_reuses, t->np_skips, t->np_creates,
          t->np_create_fails, t->np_deletes, t->np_shader_deletes,
          t->np_fail_prog_deletes, t->np_refusals);
    /* NO DELETE WAS EVER ISSUED. The CALL is the property, and the counter is
     * what carries it: the resolver RECORD cannot, because st_no_sync above
     * deliberately asks sl_gl_proc for glDeleteProgram itself to show it answers
     * NULL while the gate is off, so the name is in that record on every run of
     * this suite and an assertion about it would be about the fixture's own
     * question rather than about the module. The grammar case, which runs in a
     * process of its own and asks nothing, carries the resolver claim instead. */
    CHECK(tf.delete_program == 0L && tf.delete_shader == 0L,
          "and NEITHER delete was issued since the last arm, the control "
          "having created nothing to release (%ld/%ld)", tf.delete_program,
          tf.delete_shader);
    /* AND TEARDOWN IS A NO-OP ON AN OFF LAUNCH. */
    sl_bgtf_shutdown();
    CHECK(tf.delete_program == 0L && tf.delete_shader == 0L
          && sl_bgtf_stats()->np_deletes == 0
          && sl_bgtf_stats()->np_shader_deletes == 0,
          "teardown issues no delete and counts none (%ld/%ld/%lu/%lu)",
          tf.delete_program, tf.delete_shader,
          sl_bgtf_stats()->np_deletes,
          sl_bgtf_stats()->np_shader_deletes);
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "while the ordinary arm ran prepasses over vertices (%lu / %lu)",
          t->prepasses, t->vertices);
    end_case();
}

/* THE SWITCH'S GRAMMAR, COUPLING, EXCLUSIVITY AND REFUSAL ACCOUNTING, one
 * setting per launch. The runner says what the control must ANSWER and whether
 * this launch asked for it and did not get it. */
static void mode_env_neutral_prog(int expect_on, int want_refusal)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("neutral-program switch: the grammar, the coupling to the zero-count "
               "arm, the seven exclusions and the refusal accounting");
    CHECK(sl_bgtf_neutral_prog() == expect_on,
          "the control answers %d (%d)", expect_on, sl_bgtf_neutral_prog());
    /* PARSED ONCE FOR THE LAUNCH: repeated reads must cost nothing and must not
     * drift, which is what lets the draw site read it per served draw. */
    calls_before = tf.calls;
    for (i = 0; i < 64; i++)
        CHECK(sl_bgtf_neutral_prog() == expect_on,
              "and answers the same on read %d", i);
    CHECK(tf.calls == calls_before,
          "reading it costs no GL call (%ld -> %ld)", calls_before, tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->np_refusals == (want_refusal ? 1UL : 0UL),
          "the launch is counted as %s, exactly once (%lu)",
          want_refusal ? "a REFUSAL" : "no refusal", t->np_refusals);
    /* A REFUSED OR ABSENT CONTROL DID NO WORK AND CREATED NOTHING. */
    if (!expect_on) {
        CHECK(t->np_setups == 0 && t->np_draws == 0 && t->np_switches == 0
              && t->np_restores == 0 && t->np_creates == 0
              && t->np_create_fails == 0 && t->np_skips == 0,
              "and nothing at all was counted for a control that is not in "
              "force");
        CHECK(tf.delete_program == 0L && tf.delete_shader == 0L
              && np_asked_for("glDeleteProgram") == 0
              && np_asked_for("glDeleteShader") == 0,
              "neither delete was issued and neither name was ever asked for "
              "(%ld/%ld/%d/%d)",
              tf.delete_program, tf.delete_shader,
              np_asked_for("glDeleteProgram"), np_asked_for("glDeleteShader"));
    }
    /* AND EVERY OTHER CONTROL IS LEFT EXACTLY AS ITS OWN CLAUSES DECIDED IT.
     * This control stands down alone and writes no other control's answer; the
     * runner's own expectations cover what each of them is on each launch. */
    CHECK(sl_bgtf_state_only() == sl_bgtf_state_only()
          && sl_bgtf_pos_input() == sl_bgtf_pos_input(),
          "the older controls answer stably beside this one (%d/%d/%d/%d/%s/%s)",
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao(),
          sl_bgtf_zero_count(), sl_bgtf_es_name(sl_bgtf_empty_state()),
          sl_bgtf_pr_name(sl_bgtf_post_reset()));
    end_case();
}

/* ---- 18b. THE AUXILIARY-CONTEXT EMPTY-DRAW CONTROL (SL_BG_TF_AUX_EMPTY) --
 *
 * WHAT IS BEING MEASURED, AND WHY A COUNT ALONE CANNOT SAY IT. The control
 * relocates the zero-count arm's one empty draw to a SECOND, OBJECT-SHARING GL
 * context on the same window. Every claim is therefore about WHERE a call
 * landed, which is not a property of any call's arguments - so the shim stamps
 * every record with the context it was made on, and these cases read that stamp:
 *
 *   - the main context's own empty draw is NOT issued, and the omission is a
 *     positive record (ax_main_omits) rather than a count that failed to rise;
 *   - the main context's setup and restoration are otherwise byte-for-byte the
 *     ones every arm makes, in the same order, all on the MAIN context;
 *   - exactly one glDrawArrays(GL_POINTS, 0, 0) is issued, on the AUXILIARY
 *     context, with the SHARED prepass program bound there and discard enabled
 *     there - row A's two conditions, on the context the call is made on;
 *   - the auxiliary context's own two pieces of state are put back and the MAIN
 *     context is current again before anything else happens;
 *   - the share check is a real query, asked ON the auxiliary context, about the
 *     prepass program's name, and before one byte of bracket state is written;
 *   - no transform-feedback scope is opened on either context and nothing is
 *     bound for output on the auxiliary one, so no buffer output is produced or
 *     consumed anywhere;
 *   - every failure REFUSES the served draw rather than issuing the call on the
 *     main context, which would be the ordinary zero-count arm under this
 *     control's name;
 *   - a LOST main context issues nothing further at all - not the bracket's own
 *     restoration - which the shim makes measurable by leaving no context
 *     current after a failed switch back, so any call at all raises
 *     v_call_no_context;
 *   - teardown destroys the context this control created, exactly once, and does
 *     NOT destroy it on the lost path.
 */

#define AX_SERVED 5

/* THE BRACKET THIS CONTROL ISSUES, IN ORDER AND PER CONTEXT. It is the
 * zero-count arm's bracket - the common setup, no begin/end pair, discard left
 * on, the common restoration - with the one empty draw REMOVED from the main
 * context and five records added on the auxiliary one. Every main-context record
 * is required to have landed on the main context, which is what makes "the
 * relocation moved exactly one call and nothing else" checkable rather than
 * asserted.
 *
 * AND TWO RECORDS AT THE VERY FRONT, on the bracket that ACQUIRES: the share
 * check, which is one glIsProgram on the AUXILIARY context naming the prepass
 * program and answering TRUE. `want_share` is 0 for a bracket served from an
 * already-acquired context, which omits it. */
static int ax_expect(struct expect *e, int *want, GLuint vbo, GLuint pool,
                     GLuint prog, int want_share)
{
    int n = 0;

    if (want_share)
        n = ax_push(e, want, n, TFGL_ISPROGRAM, (long) prog, 1L, ANY, ANY, CTX_AUX,
                    "THE SHARE CHECK: glIsProgram naming the PREPASS program, "
                    "asked ON the auxiliary context and answering TRUE, before "
                    "a single byte of bracket state is written");
    n = ax_push(e, want, n, TFGL_GENBUFFERS, 1, ANY, ANY, ANY, CTX_MAIN,
                "the pool's name, on the MAIN context");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) pool, ANY, ANY,
                CTX_MAIN, "bind to size it");
    n = ax_push(e, want, n, TFGL_BUFFERDATA, GL_ARRAY_BUFFER, (long) NV * 16L,
                GL_STATIC_DRAW, 0, CTX_MAIN,
                "four floats per vertex, no initial data");
    n = ax_push(e, want, n, TFGL_GETBUFPARAM, GL_ARRAY_BUFFER, GL_BUFFER_SIZE,
                (long) NV * 16L, 0, CTX_MAIN,
                "and the driver is asked whether it honoured the size");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY, CTX_MAIN,
                "and unbind it again");
    n = ax_push(e, want, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY, CTX_MAIN,
                "the common setup's prepass program, unchanged");
    n = ax_push(e, want, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_dmode"), ANY, ANY, ANY,
                CTX_MAIN, "the depth case, written on the MAIN context");
    n = ax_push(e, want, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk1"), ANY, ANY, ANY,
                CTX_MAIN, "the first coefficient");
    n = ax_push(e, want, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk2"), ANY, ANY, ANY,
                CTX_MAIN, "the second coefficient");
    n = ax_push(e, want, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                CTX_MAIN, "the main context's discard, for the whole bracket");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY, ANY,
                CTX_MAIN, "the pass's own static vertex buffer");
    n = ax_push(e, want, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR, (long) vbo,
                CTX_MAIN, "three floats at the item's stride, out of that "
                "buffer");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY, CTX_MAIN,
                "unbound straight away");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
                CTX_MAIN, "no stale element buffer across the bracket");
    n = ax_push(e, want, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                (long) pool, ANY, CTX_MAIN,
                "the pool is bound at index 0 ON THE MAIN CONTEXT, which a T0 "
                "arm keeps - and nothing is ever bound for output on the "
                "auxiliary one");
    /* AND NO glBeginTransformFeedback ON EITHER CONTEXT: the pair is omitted
     * together, exactly as the zero-count arm omits it, so no capture is open
     * anywhere and the two context switches are made with nothing recording. */
    n = ax_push(e, want, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY, CTX_AUX,
                "THE AUXILIARY BRACKET OPENS: the SAME SHARED prepass program, "
                "bound on the auxiliary context");
    n = ax_push(e, want, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                CTX_AUX, "and discard enabled THERE - it is per-context state, "
                "so the main context's enable says nothing about this one");
    n = ax_push(e, want, n, TFGL_DRAWARRAYS, GL_POINTS, 0, 0L, ANY, CTX_AUX,
                "EXACTLY glDrawArrays(GL_POINTS, 0, 0), ON THE AUXILIARY "
                "CONTEXT, which is the whole of what this control moves");
    n = ax_push(e, want, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                CTX_AUX, "the auxiliary context's own discard goes back to "
                "GL's default");
    n = ax_push(e, want, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY, CTX_AUX,
                "and so does its program, which closes the auxiliary bracket");
    n = ax_push(e, want, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0,
                ANY, CTX_MAIN,
                "back on the MAIN context, the indexed binding is released");
    n = ax_push(e, want, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                CTX_MAIN, "the common bracket's own disable, unchanged");
    n = ax_push(e, want, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY, CTX_MAIN,
                "and the fixed pipeline comes back, unchanged");
    return n;
}

/* ONE SERVED DRAW UNDER THE CONTROL, then the cache cadence, then teardown. */
static void ax_scenario(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo, pool = 0u, prog = 0u;
    long mark, calls_at_hit, use_program_before;
    int s, i;

    begin_case("auxiliary context: the zero-count arm's one empty draw is NOT issued on "
               "the main context and IS issued on a second, object-sharing context with "
               "row A's conditions there, the main context's setup and restoration are "
               "otherwise unchanged, the main context is current again at close and "
               "teardown destroys exactly that second context");
    arm();
    /* THE SHARE CHECK'S ENTRY POINT RESOLVES, which is a precondition of the
     * control rather than a convenience: the acquire refuses outright without
     * it, and that refusal has its own case below. */
    tf.provide_is_program = 1;
    CHECK(sl_bgtf_aux_empty() == 1, "the control is in force (%d)",
          sl_bgtf_aux_empty());
    CHECK(sl_bgtf_zero_count() == 1 && sl_bgtf_raster_cpu() == 1,
          "on the zero-count arm with the original-raster control, which is what "
          "makes the draw EMPTY (%d/%d)", sl_bgtf_zero_count(),
          sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_empty_state() == SL_BGTF_ES_OFF
          && sl_bgtf_post_reset() == SL_BGTF_PR_OFF
          && sl_bgtf_neutral_prog() == 0 && sl_bgtf_state_only() == 0
          && sl_bgtf_pos_input() == 0 && sl_bgtf_private_vao() == 0,
          "and with every control it is exclusive with ABSENT (%s/%s/%d/%d/%d/%d)",
          sl_bgtf_es_name(sl_bgtf_empty_state()),
          sl_bgtf_pr_name(sl_bgtf_post_reset()), sl_bgtf_neutral_prog(),
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    /* AND NOTHING HAS BEEN CREATED YET. The capability is lazy, so a module that
     * is merely ready owns no second context. */
    CHECK(tf.aux_exists == 0 && tf.aux_created == 0L
          && tf.aux_create_calls == 0L,
          "no auxiliary context exists and none was even asked for before the "
          "first served draw (%d/%ld/%ld)", tf.aux_exists, tf.aux_created,
          tf.aux_create_calls);
    CHECK(tf.ctx_cur == CTX_MAIN,
          "and the MAIN context is current, as the caller handed it over (%d)",
          tf.ctx_cur);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    mark = tf.nlog;
    b = snap();
    use_program_before = tf.use_program;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0 && s < SLOTS, "a slot was taken (%d)", s);
    {
        const struct sl_bgtfgl_call *base = first_of(mark, TFGL_BINDBUFFERBASE);
        const struct sl_bgtfgl_call *up = first_of(mark, TFGL_USEPROGRAM);
        CHECK(base != NULL, "a recording buffer was bound at index 0");
        pool = base != NULL ? (GLuint) base->c : 0u;
        CHECK(up != NULL, "and the common setup made a program current");
        prog = up != NULL ? (GLuint) up->a : 0u;
        CHECK(prog != 0u, "which is the prepass program (%u)", (unsigned) prog);
    }

    /* THE WHOLE BRACKET, IN ORDER AND PER CONTEXT, with the share check at the
     * front of it and the one relocated call named exactly. */
    {
        struct expect e[40];
        int want[40];
        const int n = ax_expect(e, want, vbo, pool, prog, 1);
        seq_is(mark, e, n);
        /* AND EVERY ONE OF THEM ON THE CONTEXT IT BELONGS ON, which is the
         * claim no argument can carry. */
        seq_ctx_is(mark, e, want, n);
        CHECK(tf.nlog == mark + (long) n,
              "and NOTHING follows them - in particular no second draw anywhere "
              "(%ld records, expected %d)", tf.nlog - mark, n);
    }

    /* THE ONE CONTEXT CREATED, AND THE SHARE CHECK THAT VERIFIED IT. */
    CHECK(tf.aux_created == 1L && tf.aux_exists == 1,
          "exactly one auxiliary context was created and is alive (%ld/%d)",
          tf.aux_created, tf.aux_exists);
    CHECK(tf.is_program_calls == 1L && tf.is_program_last == prog,
          "the share check was made exactly once and named the PREPASS program "
          "(%ld calls, name %u, prepass %u)", tf.is_program_calls,
          (unsigned) tf.is_program_last, (unsigned) prog);
    CHECK(tf.is_program_on_aux == 1,
          "and it was asked ON THE AUXILIARY CONTEXT, which is the only context "
          "the answer means anything about (%d)", tf.is_program_on_aux);

    /* AND THROUGH THAT CONTEXT'S OWN POINTER, which is a different claim from
     * the one above and is the one a count could never carry.
     *
     * The platform documents that a function pointer belongs to the context it
     * was resolved on and that pointers from the main context must be assumed
     * invalid in a second one, so "the call was made on the auxiliary context"
     * is only half of what has to hold: the POINTER has to have come from there
     * too. The shim hands back a distinguishable pointer per context and each
     * one checks the current context at the call, so a module that resolved on
     * the main context and called on the auxiliary one raises
     * v_ptr_wrong_context - with every count, every argument and the whole
     * transcript still reading exactly as they do here. */
    CHECK(tf.proc_calls_on_aux == 2L,
          "two entry points were RESOLVED while the auxiliary context was "
          "current - its own glIsProgram and glUseProgram - which is where a "
          "pointer used there has to come from (%ld)", tf.proc_calls_on_aux);
    CHECK(tf.v_ptr_wrong_context == 0L,
          "and NOT ONE call in this run went through a pointer resolved on a "
          "different context from the one that was current at it - not the "
          "share check, and not either of the bracket's two program calls "
          "(%ld)", tf.v_ptr_wrong_context);

    /* THE CONDITIONS AT THE DRAW, WHICH ARE ROW A'S - AND ON WHICH CONTEXT.
     * Taken from the sample inside glDrawArrays, because the restore and the
     * cleanup put the state back immediately afterwards. */
    CHECK(tf.st_at_draw_seen == 1L,
          "exactly one draw was sampled in this bracket (%ld)",
          tf.st_at_draw_seen);
    CHECK(tf.st_at_draw_tf_active == 0,
          "AT THE DRAW no feedback scope was open, which is row A's T0 (%d)",
          tf.st_at_draw_tf_active);
    CHECK(tf.st_at_draw_discard == 1,
          "rasterizer discard was ON, which is row A's D1 - enabled on the "
          "auxiliary context, discard being per-context state (%d)",
          tf.st_at_draw_discard);
    CHECK(tf.st_at_draw_program == prog,
          "and the SHARED PREPASS PROGRAM was current, by the same name the "
          "main context uses, which is what sharing means (%u vs %u)",
          (unsigned) tf.st_at_draw_program, (unsigned) prog);
    CHECK(tf.draw_arrays == 1L && tf.draw_vertices == 0L,
          "the call was issued exactly once in this process and submitted ZERO "
          "vertices (%ld/%ld)", tf.draw_arrays, tf.draw_vertices);
    CHECK(tf.draws_on_aux == 1L && tf.draws_on_aux == tf.draw_arrays,
          "and EVERY draw this process made was on the AUXILIARY context - so "
          "the main context issued none (%ld of %ld)", tf.draws_on_aux,
          tf.draw_arrays);
    CHECK(tf.draw_vertices_on_aux == 0L,
          "the auxiliary draw submitted no vertex either (%ld)",
          tf.draw_vertices_on_aux);

    /* THE SWITCHES, AND WHERE THEY LEFT THINGS. */
    CHECK(tf.aux_switches == 2L && tf.main_switches == 2L,
          "two switches each way: the acquire's verification round trip and the "
          "draw's (%ld/%ld)", tf.aux_switches, tf.main_switches);
    CHECK(tf.ctx_cur == CTX_MAIN,
          "and the MAIN context is current at close, which is the state the "
          "caller handed over (%d)", tf.ctx_cur);
    CHECK(tf.v_call_no_context == 0,
          "no GL call was made with NO context current (%ld)",
          tf.v_call_no_context);
    /* AND EXACTLY SIX CALLS LANDED ON THE AUXILIARY CONTEXT: the share check,
     * and the five of the relocated bracket. A seventh would mean this control
     * did something there it does not declare. */
    CHECK(tf.calls_on_aux == 6L,
          "exactly six GL calls were made on the auxiliary context - the share "
          "check, the program, the discard enable, the draw, the discard "
          "disable and the program reset (%ld)", tf.calls_on_aux);

    /* NO OUTPUT WAS BOUND OR PRODUCED ON EITHER CONTEXT, which is the claim that
     * keeps cross-context buffer synchronisation out of this entirely. */
    CHECK(tf.begin_tf == 0L && tf.end_tf == 0L,
          "no feedback scope was opened or closed anywhere (%ld/%ld)",
          tf.begin_tf, tf.end_tf);
    CHECK(tf.v_program_change_in_tf == 0,
          "so no program was changed inside one, on either context (%ld)",
          tf.v_program_change_in_tf);
    CHECK(tf.get_buffer_sub_data == 0L,
          "and nothing was read back from any buffer (%ld)",
          tf.get_buffer_sub_data);
    CHECK(tf.get_error_calls == 0,
          "no glGetError was called, so no error state was consumed (%ld)",
          tf.get_error_calls);
    CHECK(tf.finish_calls == 0L,
          "and no glFinish - this control adds no synchronisation point (%ld)",
          tf.finish_calls);

    /* NO SECOND PROGRAM AND NO OBJECT OF ANY KIND WAS CREATED. This control
     * creates a CONTEXT, not an object, which is the whole difference between it
     * and the neutral-program control. */
    CHECK(tf.create_program == 1L && tf.create_shader == 1L,
          "one program and one shader exist in this context - the prepass "
          "program's - and this control made neither (%ld/%ld)",
          tf.create_program, tf.create_shader);
    CHECK(tf.delete_program == 0L && tf.delete_shader == 0L,
          "and no object was deleted, there being none this control owns "
          "(%ld/%ld)", tf.delete_program, tf.delete_shader);
    CHECK(tf.use_program - use_program_before == 4L,
          "the program was made current four times: the main context's setup "
          "and cleanup, and the auxiliary context's own bind and reset (%ld)",
          tf.use_program - use_program_before);

    /* THE CALLER'S STATE AT CLOSE, which this control may not move. */
    CHECK(tf.discard == 0 && tf.current == 0 && tf.tf_buf == 0u
          && tf.arr_buf == 0u && tf.elem_buf == 0u,
          "discard is off, no program is current, nothing is recording and no "
          "buffer is left bound (%d/%u/%u/%u/%u)", tf.discard,
          (unsigned) tf.current, (unsigned) tf.tf_buf, (unsigned) tf.arr_buf,
          (unsigned) tf.elem_buf);
    CHECK(tf.vptr.set && tf.vptr.buf == vbo && tf.vptr.stride == (GLsizei) STR,
          "and the position pointer is the prepass's own (set %d, buf %u, "
          "stride %d)", tf.vptr.set, (unsigned) tf.vptr.buf,
          (int) tf.vptr.stride);

    /* THE WORK COUNTERS, AND THE ONE EQUALITY THIS ARM DELIBERATELY BREAKS. */
    a = snap();
    CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices
          && a.walks == b.walks && a.binds == b.binds
          && a.raster_draws == b.raster_draws,
          "NO prepass, vertex, walk, bind or submission was counted "
          "(%lu/%lu/%lu/%lu/%lu)", a.prepasses - b.prepasses,
          a.vertices - b.vertices, a.walks - b.walks, a.binds - b.binds,
          a.raster_draws - b.raster_draws);
    CHECK(a.zc_setups - b.zc_setups == 1,
          "the zero-count arm's own bracket is still counted beneath this "
          "control (%lu)", a.zc_setups - b.zc_setups);
    CHECK(a.zc_calls == b.zc_calls,
          "but zc_calls does NOT rise, because that counter is of empty calls "
          "made on the MAIN context and this arm makes none there - which is "
          "the one equality of the zero-count arm this control breaks (%lu)",
          a.zc_calls - b.zc_calls);
    CHECK(a.ax_main_omits - b.ax_main_omits == 1,
          "and the omission is a POSITIVE record, not the absence of one (%lu)",
          a.ax_main_omits - b.ax_main_omits);
    CHECK(a.es_setups == b.es_setups && a.es_draws == b.es_draws
          && a.pr_setups == b.pr_setups && a.so_setups == b.so_setups
          && a.np_setups == b.np_setups && a.np_draws == b.np_draws,
          "and nothing for any control this one is exclusive with, none of "
          "which is in force");
    CHECK(a.ax_setups - b.ax_setups == 1 && a.ax_draws - b.ax_draws == 1,
          "one bracket issued one empty draw under this control (%lu/%lu)",
          a.ax_setups - b.ax_setups, a.ax_draws - b.ax_draws);
    CHECK(a.ax_draws_aux - b.ax_draws_aux == 1
          && a.ax_draws_aux == a.ax_draws,
          "and every one of them was on the AUXILIARY context (%lu vs %lu)",
          a.ax_draws_aux, a.ax_draws);
    CHECK(a.ax_switches - b.ax_switches == 1
          && a.ax_restores - b.ax_restores == 1
          && a.ax_switches == a.ax_restores,
          "the draw's switch and restore are counted once each and are EQUAL, "
          "so the bracket was not handed on with the wrong context current "
          "(%lu/%lu)", a.ax_switches, a.ax_restores);
    CHECK(a.ax_creates == 1 && a.ax_create_fails == 0 && a.ax_skips == 0
          && a.ax_share_fails == 0 && a.ax_switch_fails == 0
          && a.ax_lost == 0 && a.ax_destroys == 0,
          "one context was created and verified, with no failure of any kind "
          "and nothing destroyed yet (%lu/%lu/%lu/%lu/%lu/%lu/%lu)",
          a.ax_creates, a.ax_create_fails, a.ax_skips, a.ax_share_fails,
          a.ax_switch_fails, a.ax_lost, a.ax_destroys);
    CHECK(a.ax_refusals == 0 && a.refusals == b.refusals,
          "the control was not refused and no served draw was (%lu/%lu)",
          a.ax_refusals, a.refusals - b.refusals);

    /* THE SLOT IS UNWRITTEN AND UNBINDABLE: an empty call wrote no output, and
     * it wrote it on another context, which makes the point twice. */
    b = snap();
    calls_at_hit = tf.calls;
    CHECK(sl_bgtf_bind_clip(s) == -1,
          "the slot this empty draw keyed is REFUSED by the binding");
    CHECK(snap().binds == b.binds && tf.calls == calls_at_hit,
          "and the refusal cost no bind and no GL call");

    /* A CACHE HIT HAS NO EMPTY DRAW AND THEREFORE NO CONTEXT SWITCH, which is
     * the expensive part of this arm and the one a careless implementation would
     * pay for on every mesh. */
    b = snap();
    calls_at_hit = tf.calls;
    mark = tf.nlog;
    {
        const long aux_sw = tf.aux_switches, main_sw = tf.main_switches;
        for (i = 1; i < AX_SERVED; i++)
            CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == s,
                  "served mesh %d is a cache HIT on the same slot", i);
        a = snap();
        CHECK(tf.calls == calls_at_hit && tf.nlog == mark,
              "the %d hits added no GL call and no transcript record (%ld -> "
              "%ld)", AX_SERVED - 1, calls_at_hit, tf.calls);
        CHECK(tf.aux_switches == aux_sw && tf.main_switches == main_sw
              && tf.aux_switch_calls == 2L && tf.main_switch_calls == 2L,
              "and NO CONTEXT SWITCH WAS EVEN ATTEMPTED for any of them "
              "(%ld/%ld switched, %ld/%ld asked)", tf.aux_switches - aux_sw,
              tf.main_switches - main_sw, tf.aux_switch_calls,
              tf.main_switch_calls);
    }
    CHECK(a.ax_reuses - b.ax_reuses == (unsigned long) (AX_SERVED - 1),
          "each hit was counted as this control's own reuse (%lu)",
          a.ax_reuses - b.ax_reuses);
    CHECK(a.ax_setups == b.ax_setups && a.ax_draws == b.ax_draws
          && a.ax_switches == b.ax_switches && a.ax_restores == b.ax_restores
          && a.ax_main_omits == b.ax_main_omits,
          "and reached no bracket and issued nothing (%lu/%lu/%lu/%lu/%lu)",
          a.ax_setups - b.ax_setups, a.ax_draws - b.ax_draws,
          a.ax_switches - b.ax_switches, a.ax_restores - b.ax_restores,
          a.ax_main_omits - b.ax_main_omits);
    CHECK(a.ax_setups + a.ax_skips + a.ax_reuses == (unsigned long) AX_SERVED,
          "REACHED + SKIPPED + REUSES == the %d meshes served, so the account "
          "is complete (%lu + %lu + %lu)", AX_SERVED, a.ax_setups, a.ax_skips,
          a.ax_reuses);
    CHECK(a.ax_setups == a.ax_main_omits && a.ax_setups == a.ax_draws
          && a.ax_setups == a.ax_switches && a.ax_setups == a.ax_restores,
          "and the five per-bracket counts are EQUAL to one another, which is "
          "the invariant this arm has in place of zc_calls "
          "(%lu/%lu/%lu/%lu/%lu)", a.ax_setups, a.ax_main_omits, a.ax_draws,
          a.ax_switches, a.ax_restores);

    /* AND TEARDOWN DESTROYS THE CONTEXT THIS CONTROL CREATED, exactly once,
     * while the MAIN context is current - which is the precondition the backend
     * states and the reason this call is where it is. */
    b = snap();
    mark = tf.nlog;
    CHECK(tf.ctx_cur == CTX_MAIN,
          "the MAIN context is current at teardown, which destroying requires "
          "(%d)", tf.ctx_cur);
    sl_bgtf_shutdown();
    a = snap();
    CHECK(tf.aux_destroyed == 1L && tf.aux_exists == 0,
          "exactly one auxiliary context was destroyed and none is alive "
          "(%ld/%d)", tf.aux_destroyed, tf.aux_exists);
    CHECK(a.ax_destroys - b.ax_destroys == 1
          && a.ax_destroys == a.ax_creates,
          "the destroy is counted and matches the create (%lu vs %lu)",
          a.ax_destroys, a.ax_creates);
    CHECK(tf.nlog == mark,
          "and teardown added NO GL record at all - destroying a context is not "
          "a GL command (%ld)", tf.nlog - mark);
    CHECK(tf.delete_program == 0L && tf.delete_shader == 0L,
          "and deleted no object either (%ld/%ld)", tf.delete_program,
          tf.delete_shader);
    /* AND A SECOND CALL DESTROYS NOTHING TWICE. */
    sl_bgtf_shutdown();
    CHECK(tf.aux_destroyed == 1L && sl_bgtf_stats()->ax_destroys == 1UL,
          "a second teardown destroys nothing further (%ld/%lu)",
          tf.aux_destroyed, sl_bgtf_stats()->ax_destroys);
    CHECK(tf.v_call_no_context == 0 && tf.v_stale_name == 0,
          "with no call on an uncurrent context and no stale name anywhere in "
          "the run (%ld/%ld)", tf.v_call_no_context, tf.v_stale_name);
    CHECK(tf.v_ptr_wrong_context == 0L,
          "and no pointer was used on a context other than the one it was "
          "resolved on, over the whole run including teardown (%ld)",
          tf.v_ptr_wrong_context);
    /* AND NOTHING TERMINATED. The scenario is the one run in which nothing goes
     * wrong, so the lost-context exit path must not have been reached - which is
     * worth asserting here rather than only where it IS expected: a control that
     * ended the process on a healthy run would be a far worse defect than one
     * that failed to. */
    CHECK(tf.fatal_calls == 0L,
          "and the terminate-now path was NOT taken on a run with no failure in "
          "it (%ld)", tf.fatal_calls);
    /* THE CONTROL'S WHOLE SIGNATURE, PRINTED, for the reason every other arm
     * prints one: the CHECK macro is silent on a pass, so a runner comparing
     * this run's numbers against the value the round recorded has nothing to
     * read otherwise. The line carries the module's own counts and the fake
     * backend's context and draw numbers together, so a run in which those
     * disagreed would show it here rather than only inside a failing
     * assertion. */
    a = snap();
    printf("    AXDRAW setups=%lu omits=%lu draws=%lu draws_aux=%lu "
           "switches=%lu restores=%lu reuses=%lu skips=%lu creates=%lu "
           "create_fails=%lu share_fails=%lu switch_fails=%lu proc_fails=%lu "
           "lost=%lu fatals=%lu destroys=%lu gl_draws=%ld gl_draws_aux=%ld "
           "gl_calls_aux=%ld gl_aux_created=%ld gl_aux_destroyed=%ld "
           "gl_isprog=%ld gl_nocontext=%ld gl_proc_aux=%ld gl_ptr_wrong=%ld "
           "gl_fatals=%ld gl_after_fatal=%ld\n",
           a.ax_setups, a.ax_main_omits, a.ax_draws, a.ax_draws_aux,
           a.ax_switches, a.ax_restores, a.ax_reuses, a.ax_skips, a.ax_creates,
           a.ax_create_fails, a.ax_share_fails, a.ax_switch_fails,
           a.ax_proc_fails, a.ax_lost, a.ax_fatals,
           a.ax_destroys, tf.draw_arrays, tf.draws_on_aux, tf.calls_on_aux,
           tf.aux_created, tf.aux_destroyed, tf.is_program_calls,
           tf.v_call_no_context, tf.proc_calls_on_aux, tf.v_ptr_wrong_context,
           tf.fatal_calls, tf.calls_after_fatal);
    end_case();
}

/* A CAPABILITY OR SWITCH FAILURE REFUSES THE SERVED DRAW AND NEVER FALLS BACK
 * TO THE MAIN CONTEXT. Each shape is driven in its own synthetic context,
 * because the acquire LATCHES per context, and each must leave the empty draw
 * unissued rather than issued on the main context - which would be the ordinary
 * zero-count arm reported under this control's name.
 *
 * THE SHAPES AND WHAT EACH ONE PROVES:
 *
 *   `no glIsProgram`      the one entry point the share check needs does not
 *                         resolve, so NO CONTEXT IS CREATED AT ALL - the
 *                         ordering property, stated as a measurement
 *   `no context`          the backend offers none; nothing is created and the
 *                         main context is untouched
 *   `the switch fails`    the acquire's own switch to it fails, leaving the main
 *                         context current, so the context is destroyed again on
 *                         the spot
 *   `no sharing`          glIsProgram answers FALSE: the context exists, is
 *                         switchable, and is destroyed again because the name
 *                         this control would bind means nothing in it
 *   `the draw's switch`   the acquire succeeds and the DRAW's switch fails, so
 *                         the bracket is built, nothing is issued on either
 *                         context, the ordinary restoration runs and the draw is
 *                         refused - the one late refusal in the module
 *
 * The LOST shapes are driven separately below, because they are the only ones
 * whose claim is about what is NOT called afterwards. */
static void ax_fail_shape(const char *which, int gate_isprog, int create_fail,
                          int switch_fail, long switch_fail_after,
                          int share_false, const char *withhold_on_aux)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    long draws_before, created_before, destroyed_before;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = gate_isprog;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    /* THE FAULT IS INJECTED AFTER THE PREPASS PROGRAM WAS BUILT, so what fails
     * is this control's own acquire and not the module's capability. */
    tf.aux_create_fail = create_fail;
    tf.aux_create_fail_after = create_fail ? 0L : -1L;
    tf.aux_switch_fail = switch_fail;
    tf.aux_switch_fail_after = switch_fail ? switch_fail_after : -1L;
    tf.is_program_false = share_false;
    /* AND THE ONE NAME THE AUXILIARY CONTEXT WILL NOT RESOLVE, if this shape is
     * the resolve refusal. It is withheld ONLY on that context, so the main
     * context's set is complete and a module that reached for a main pointer
     * would sail through - which is precisely the defect this shape is here to
     * refuse. */
    tf.withhold_proc_on_aux = withhold_on_aux;
    b = snap();
    draws_before = tf.draw_arrays;
    created_before = tf.aux_created;
    destroyed_before = tf.aux_destroyed;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(tf.draw_arrays == draws_before,
          "%s: and NO draw was issued - not on the auxiliary context and not on "
          "the MAIN one, which would be the ordinary zero-count arm (%ld)",
          which, tf.draw_arrays - draws_before);
    CHECK(a.ax_draws == b.ax_draws && a.ax_draws_aux == b.ax_draws_aux
          && a.ax_creates == b.ax_creates,
          "%s: and NOTHING claims a verified context or an auxiliary draw",
          which);
    CHECK(a.ax_create_fails - b.ax_create_fails == 1,
          "%s: the failure is counted as this control's own (%lu)", which,
          a.ax_create_fails - b.ax_create_fails);
    CHECK(a.refusals - b.refusals == 1,
          "%s: the module counts the refused draw (%lu)", which,
          a.refusals - b.refusals);
    CHECK(a.ax_lost == 0,
          "%s: and NONE of these shapes loses the main context - each is a "
          "recoverable refusal (%lu)", which, a.ax_lost);
    CHECK(tf.ctx_cur == CTX_MAIN,
          "%s: the MAIN context is current after the refusal, which is what "
          "lets the caller draw from the CPU (%d)", which, tf.ctx_cur);
    CHECK(tf.v_call_no_context == 0,
          "%s: and no GL call was made with no context current (%ld)", which,
          tf.v_call_no_context);

    /* AND NO CONTEXT SURVIVES THE REFUSAL. The two shapes that fail before a
     * create make NONE AT ALL, which is what resolving the share check's entry
     * point first buys; the three that get one destroy it again, while the main
     * context is current, which is the only instant that is safe. */
    {
        const int made = (gate_isprog && !create_fail);
        const long want_made = made ? 1L : 0L;
        CHECK(tf.aux_created - created_before == want_made,
              "%s: %ld auxiliary context(s) were created (%ld)", which,
              want_made, tf.aux_created - created_before);
        CHECK(tf.aux_destroyed - destroyed_before == want_made,
              "%s: and exactly as many were destroyed, so the refusal left "
              "none behind (%ld vs %ld)", which,
              tf.aux_destroyed - destroyed_before, want_made);
        CHECK(tf.aux_exists == 0,
              "%s: no auxiliary context is alive (%d)", which, tf.aux_exists);
        CHECK(a.ax_destroys - b.ax_destroys == (unsigned long) want_made,
              "%s: and the module's own account agrees (%lu)", which,
              a.ax_destroys - b.ax_destroys);
    }
    /* AND THE SHAPE'S OWN COUNTER ROSE, not another's: the three failures are
     * deliberately not interchangeable, and a module that reported a share
     * failure as a switch failure would be telling a reader to look at the
     * wrong thing. */
    CHECK(a.ax_share_fails - b.ax_share_fails
          == (unsigned long) ((share_false && gate_isprog && !create_fail
                               && !switch_fail) ? 1 : 0),
          "%s: the share-check failure count is the shape's own (%lu)", which,
          a.ax_share_fails - b.ax_share_fails);
    CHECK(a.ax_switch_fails - b.ax_switch_fails
          == (unsigned long) ((switch_fail && gate_isprog && !create_fail)
                              ? 1 : 0),
          "%s: and so is the switch-failure count (%lu)", which,
          a.ax_switch_fails - b.ax_switch_fails);
    CHECK(a.ax_proc_fails - b.ax_proc_fails
          == (unsigned long) ((withhold_on_aux != NULL && gate_isprog
                               && !create_fail && !switch_fail) ? 1 : 0),
          "%s: and so is the count of a context that could not resolve its OWN "
          "entry points, which is a different fact from the main context not "
          "having them (%lu)", which, a.ax_proc_fails - b.ax_proc_fails);
    /* AND NO SHAPE TERMINATED. Every one of them leaves the MAIN context
     * VERIFIED current, which is exactly what makes it recoverable, so the exit
     * path belongs to none of them - and a shape that took it would have ended
     * the launch rather than failed a check. */
    CHECK(tf.fatal_calls == 0L && a.ax_fatals == 0,
          "%s: and the terminate-now path was NOT taken - this shape is a "
          "refusal, not a lost context (%ld/%lu)", which, tf.fatal_calls,
          a.ax_fatals);
    /* AND NO POINTER WAS USED ON THE WRONG CONTEXT, on the shapes that got far
     * enough to hold one at all. */
    CHECK(tf.v_ptr_wrong_context == 0L,
          "%s: and no entry point was invoked on a context other than the one "
          "it was resolved on (%ld)", which, tf.v_ptr_wrong_context);

    /* AND THE FAILURE LATCHES: a second served draw creates nothing again. */
    {
        const long creates = tf.aux_created, asks = tf.aux_create_calls;
        const long isp = tf.is_program_calls;
        b = snap();
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "%s: a second served draw is refused too", which);
        CHECK(tf.aux_created == creates && tf.aux_create_calls == asks
              && tf.is_program_calls == isp,
              "%s: without creating a context, asking for one, or checking "
              "sharing again (%ld/%ld/%ld)", which, tf.aux_created - creates,
              tf.aux_create_calls - asks, tf.is_program_calls - isp);
        a = snap();
        CHECK(a.ax_create_fails - b.ax_create_fails == 1,
              "%s: and the second refusal is counted too (%lu)", which,
              a.ax_create_fails - b.ax_create_fails);
    }
    /* AND TEARDOWN DESTROYS NOTHING, there being no context left. A second
     * teardown is driven for the same reason the neutral control's shapes drive
     * one: a destroy issued twice would be the defect a single call hides. */
    {
        const long dest = tf.aux_destroyed;
        sl_bgtf_shutdown();
        sl_bgtf_shutdown();
        CHECK(tf.aux_destroyed == dest,
              "%s: two teardowns destroy nothing, every context this acquire "
              "made having already been released inside it (%ld)", which,
              tf.aux_destroyed - dest);
        CHECK(tf.aux_exists == 0, "%s: and none is alive (%d)", which,
              tf.aux_exists);
    }
    tf.aux_create_fail = 0;
    tf.aux_switch_fail = 0;
    tf.is_program_false = 0;
    tf.aux_create_fail_after = -1L;
    tf.aux_switch_fail_after = -1L;
    tf.withhold_proc_on_aux = NULL;
}

/* THE SWITCH THAT FAILS AT THE DRAW, which is the module's one LATE refusal and
 * so cannot go through the shape above: the acquire must SUCCEED and the second
 * switch must fail, by which time the bracket has been built and has to be put
 * back. */
static void ax_draw_switch_fails(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    long mark, draws_before;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    CHECK(sl_bgtf_ready() == 1, "the draw's switch: the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "the draw's switch: the walk is admitted");
    /* LET THE ACQUIRE'S OWN SWITCH THROUGH AND FAIL THE NEXT ONE, which is the
     * draw's. Without the after-count this gate could only ever bite on the
     * verification round trip and the draw site would be unreachable. */
    tf.aux_switch_fail = 1;
    tf.aux_switch_fail_after = 1L;
    mark = tf.nlog;
    b = snap();
    draws_before = tf.draw_arrays;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    tf.aux_switch_fail = 0;
    tf.aux_switch_fail_after = -1L;
    CHECK(s == -1,
          "the draw's switch: the served draw is REFUSED, late and after the "
          "bracket (%d)", s);
    CHECK(tf.draw_arrays == draws_before,
          "the draw's switch: and NO draw was issued on either context (%ld)",
          tf.draw_arrays - draws_before);
    CHECK(a.ax_creates - b.ax_creates == 1 && a.ax_setups - b.ax_setups == 1,
          "the draw's switch: the context WAS created and verified and the "
          "bracket WAS reached, which is what makes this the late case "
          "(%lu/%lu)", a.ax_creates - b.ax_creates, a.ax_setups - b.ax_setups);
    CHECK(a.ax_switch_fails - b.ax_switch_fails == 1,
          "the draw's switch: counted as a switch failure, which is its own "
          "name (%lu)", a.ax_switch_fails - b.ax_switch_fails);
    CHECK(a.ax_draws == b.ax_draws && a.ax_draws_aux == b.ax_draws_aux
          && a.ax_restores == b.ax_restores,
          "the draw's switch: with no draw and no restore claimed (%lu/%lu/%lu)",
          a.ax_draws - b.ax_draws, a.ax_draws_aux - b.ax_draws_aux,
          a.ax_restores - b.ax_restores);
    CHECK(a.ax_main_omits - b.ax_main_omits == 1,
          "the draw's switch: and the main context's draw was still omitted, "
          "which is why nothing was issued anywhere (%lu)",
          a.ax_main_omits - b.ax_main_omits);
    CHECK(a.refusals - b.refusals == 1,
          "the draw's switch: the module counts the refused draw (%lu)",
          a.refusals - b.refusals);
    CHECK(a.ax_lost == 0,
          "the draw's switch: the MAIN context was never lost - a switch TO the "
          "auxiliary context that fails leaves it current (%lu)", a.ax_lost);
    CHECK(tf.ctx_cur == CTX_MAIN,
          "the draw's switch: and it is current (%d)", tf.ctx_cur);
    CHECK(tf.calls_on_aux == 1L,
          "the draw's switch: exactly ONE call ever landed on the auxiliary "
          "context in this run - the acquire's share check - and the draw's "
          "bracket added none (%ld)", tf.calls_on_aux);
    CHECK(tf.v_call_no_context == 0,
          "the draw's switch: no call was made with no context current (%ld)",
          tf.v_call_no_context);
    /* AND THE ORDINARY RESTORATION DID RUN, which is the whole reason this
     * refusal is taken late: the bracket's state had to be put back. The
     * transcript is the main-context bracket with NO draw in it. */
    {
        struct expect e[40];
        int want[40];
        int n = 0;
        n = ax_push(e, want, n, TFGL_ISPROGRAM, ANY, 1L, ANY, ANY, CTX_AUX,
                    "the acquire's share check");
        n = so_push(e, n, TFGL_GENBUFFERS, 1, ANY, ANY, ANY, "the pool's name");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, ANY, ANY, ANY,
                    "bind to size it");
        n = so_push(e, n, TFGL_BUFFERDATA, GL_ARRAY_BUFFER, (long) NV * 16L,
                    GL_STATIC_DRAW, 0, "sized");
        n = so_push(e, n, TFGL_GETBUFPARAM, GL_ARRAY_BUFFER, GL_BUFFER_SIZE,
                    (long) NV * 16L, 0, "and verified");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                    "and unbound");
        n = so_push(e, n, TFGL_USEPROGRAM, ANY, ANY, ANY, ANY,
                    "the prepass program");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_dmode"), ANY, ANY, ANY,
                    "the depth case");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk1"), ANY, ANY, ANY,
                    "the first coefficient");
        n = so_push(e, n, TFGL_UNIFORM, sl_bgtfgl_loc("u_fk2"), ANY, ANY, ANY,
                    "the second coefficient");
        n = so_push(e, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                    "discard on");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY,
                    ANY, "the input buffer");
        n = so_push(e, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR,
                    (long) vbo, "the position pointer");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                    "unbound again");
        n = so_push(e, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY, ANY,
                    "no stale element buffer");
        n = so_push(e, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                    ANY, ANY, "the pool at index 0");
        /* AND NO DRAW OF ANY KIND HERE, which is the point. */
        n = ax_push(e, want, n, TFGL_BINDBUFFERBASE,
                    GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0, ANY, CTX_MAIN,
                    "THE RESTORATION RAN ANYWAY, on the MAIN context: the "
                    "indexed binding is released");
        n = ax_push(e, want, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY,
                    ANY, CTX_MAIN, "discard off again");
        n = ax_push(e, want, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY, CTX_MAIN,
                    "and the fixed pipeline back, so the caller's state is the "
                    "one it handed over");
        seq_is(mark, e, n);
        /* THE PLACEMENT OF THE THREE RESTORATION CALLS AND THE SHARE CHECK,
         * which is all this window asks about: the twelve setup records between
         * them go through so_push and are covered by seq_is alone, because
         * their placement is not what this case is testing. */
        CHECK(tf.log[mark].ctx == CTX_AUX,
              "the acquire's share check landed on the AUXILIARY context (%d)",
              tf.log[mark].ctx);
        CHECK(tf.nlog >= 3
              && tf.log[tf.nlog - 1].ctx == CTX_MAIN
              && tf.log[tf.nlog - 2].ctx == CTX_MAIN
              && tf.log[tf.nlog - 3].ctx == CTX_MAIN,
              "and the three restoration calls all landed on the MAIN context, "
              "which is where the failed switch left things");
    }
    CHECK(tf.discard == 0 && tf.current == 0 && tf.tf_buf == 0u
          && tf.arr_buf == 0u && tf.elem_buf == 0u,
          "the draw's switch: and the caller's state is exactly as it was "
          "(%d/%u/%u/%u/%u)", tf.discard, (unsigned) tf.current,
          (unsigned) tf.tf_buf, (unsigned) tf.arr_buf, (unsigned) tf.elem_buf);
    /* THE LATCH, AND THEN TEARDOWN. The context was created and verified, so
     * this is the one failure shape where teardown DOES destroy something. */
    {
        const long sw = tf.aux_switch_calls;
        b = snap();
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "the draw's switch: a second served draw is refused by the latch");
        a = snap();
        CHECK(tf.aux_switch_calls == sw,
              "the draw's switch: without attempting another switch - the latch "
              "is at the capability, before the bracket (%ld)",
              tf.aux_switch_calls - sw);
        CHECK(a.ax_create_fails - b.ax_create_fails == 1
              && a.ax_setups == b.ax_setups,
              "the draw's switch: counted as a capability refusal and reaching "
              "no bracket (%lu/%lu)", a.ax_create_fails - b.ax_create_fails,
              a.ax_setups - b.ax_setups);
    }
    b = snap();
    sl_bgtf_shutdown();
    sl_bgtf_shutdown();
    a = snap();
    CHECK(tf.aux_destroyed == 1L && tf.aux_exists == 0,
          "the draw's switch: teardown destroys the verified context exactly "
          "once (%ld/%d)", tf.aux_destroyed, tf.aux_exists);
    CHECK(a.ax_destroys - b.ax_destroys == 1,
          "the draw's switch: counted once (%lu)", a.ax_destroys - b.ax_destroys);
}

/* THE LOST MAIN CONTEXT, which is the one outcome that is not a refusal but an
 * INVALID RUN - and the only one whose claim is about what is NOT called.
 *
 * HOW IT IS MADE MEASURABLE. The shim's failed switch back leaves NO context
 * current, so every GL call after it raises v_call_no_context. "The control
 * issues nothing further - not even the bracket's own restoration" is therefore
 * a count that must stay 0, not a reading of the source.
 *
 * `at_draw` picks which of the two switches back fails: the acquire's
 * verification round trip, or the draw's. The first creates a context and never
 * uses it; the second has already issued the draw. Both must then stop dead,
 * and NEITHER may destroy the context at teardown, because destroying needs a
 * main context that is current and that is exactly what could not be
 * established.
 *
 * ONE SHAPE PER PROCESS, AND THAT IS THE CONTROL'S OWN DOING. The lost latch
 * belongs to the PROCESS and is deliberately never cleared - not by a context
 * invalidation and not by anything else - so a second shape driven after the
 * first would be refused by that latch before reaching its own failure, and
 * would pass for the wrong reason. The runner therefore launches each of these
 * in its own process, which is the same rule the one-per-launch selectors
 * follow. Driving both here was tried and showed exactly that: the second shape
 * reported no loss, no draw and no context, because the first shape's latch had
 * already refused it. */
static void ax_lost_shape(int at_draw)
{
    const char *which = at_draw ? "lost at the draw" : "lost at the acquire";
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    long nocontext_before, destroyed_before;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    if (at_draw) {
        /* THE ACQUIRE'S ROUND TRIP SUCCEEDS AND THE DRAW'S SWITCH BACK FAILS.
         * The acquire makes one switch back of its own, so one is let through
         * and the next - the draw's - is the one that fails. */
        tf.main_switch_fail = 1;
        tf.main_switch_fail_after = 1L;
    } else {
        /* THE CREATE'S OWN SWITCH BACK FAILS, which is the earliest the main
         * context can be lost: the platform call that makes the context also
         * makes it current. */
        tf.aux_create_lost = 1;
        tf.main_switch_fail_after = 0L;
    }
    b = snap();
    nocontext_before = tf.v_call_no_context;
    destroyed_before = tf.aux_destroyed;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(a.ax_lost - b.ax_lost == 1,
          "%s: counted as a LOST main context, which is its own name and not a "
          "switch failure (%lu)", which, a.ax_lost - b.ax_lost);
    CHECK(a.ax_switch_fails == b.ax_switch_fails,
          "%s: and not as a switch failure, the two being different situations "
          "for a reader (%lu)", which, a.ax_switch_fails - b.ax_switch_fails);
    /* AND WHERE THE REFUSAL IS COUNTED DEPENDS ON WHICH SWITCH FAILED, which is
     * not a quirk but the distinction the two counters exist to keep. A loss at
     * the ACQUIRE is a capability that could not be established, so the served
     * draw is refused at the call site and ax_create_fails rises. A loss at the
     * DRAW is not: the capability WAS established and verified, and the refusal
     * is taken at the draw site itself, which raises `refusals` alone. Both
     * raise ax_lost, which is the number a reader of an invalid run looks at. */
    CHECK(a.ax_create_fails - b.ax_create_fails
          == (unsigned long) (at_draw ? 0 : 1),
          "%s: the capability-refusal count is the shape's own - a loss at the "
          "acquire is a capability that failed, a loss at the draw is not "
          "(%lu)", which, a.ax_create_fails - b.ax_create_fails);
    CHECK(a.refusals - b.refusals == 1,
          "%s: and the module counts the refused draw either way, exactly once "
          "(%lu)", which, a.refusals - b.refusals);
    /* THE PROPERTY THIS SHAPE EXISTS FOR: NOTHING WAS CALLED AFTER THE LOSS. */
    CHECK(tf.v_call_no_context == nocontext_before,
          "%s: NOT ONE GL CALL was made after the main context was lost - not "
          "the control's own, and not the bracket's restoration (%ld)", which,
          tf.v_call_no_context - nocontext_before);
    /* AND THE WAY THAT PROPERTY IS ENFORCED, WHICH IS THE POINT OF THIS ROUND.
     * A refusal only redirects ONE mesh: the caller rasters it with GL, every
     * later frame draws with GL, the window teardown issues GL and this build
     * registers exit handlers that issue GL. None of that is reachable from a
     * latch inside the diagnostic, so the loss ENDS THE PROCESS where it
     * happens. The product's exit path does not return; this fixture's records
     * the call and does, which is the only reason there is anything left to
     * assert - and every entry point's funnel counts calls made after it, so
     * "nothing follows" is measured over the whole API and not over the few
     * calls a case thought to name. */
    CHECK(tf.fatal_calls == 1L && a.ax_fatals == 1,
          "%s: the terminate-now path was taken EXACTLY ONCE, which is how "
          "'no further GL command' is enforced rather than hoped for "
          "(%ld/%lu)", which, tf.fatal_calls, a.ax_fatals);
    CHECK(tf.fatal_code == 95,
          "%s: and it asked for the diagnostic's own exit status, so a launch "
          "that ended this way is identifiable from the exit code alone (%d)",
          which, tf.fatal_code);
    CHECK(tf.calls_after_fatal == 0L,
          "%s: and NOT ONE GL entry point of any kind was called after it - the "
          "product would not have existed to make one (%ld)", which,
          tf.calls_after_fatal);
    CHECK(tf.ctx_cur < 0,
          "%s: and no context is current, which is the honest model of a switch "
          "that failed - what it left current is not something the platform "
          "promises (%d)", which, tf.ctx_cur);
    if (at_draw) {
        CHECK(a.ax_draws - b.ax_draws == 1 && a.ax_draws_aux - b.ax_draws_aux == 1,
              "%s: the draw HAD been issued on the auxiliary context before the "
              "switch back failed, which is why it is counted (%lu/%lu)", which,
              a.ax_draws - b.ax_draws, a.ax_draws_aux - b.ax_draws_aux);
        CHECK(a.ax_restores == b.ax_restores,
              "%s: but no restore is claimed (%lu)", which,
              a.ax_restores - b.ax_restores);
    } else {
        CHECK(a.ax_draws == b.ax_draws && a.ax_setups == b.ax_setups,
              "%s: no draw and no bracket - the loss happened at the create, "
              "before any of it (%lu/%lu)", which, a.ax_draws - b.ax_draws,
              a.ax_setups - b.ax_setups);
        CHECK(tf.is_program_calls == 0L,
              "%s: and the share check was never even asked, the context having "
              "been unusable from the moment it existed (%ld)", which,
              tf.is_program_calls);
    }
    /* AND IT LATCHES HARDER THAN ANY OTHER FAILURE: the latch is on the PROCESS,
     * so even a new main context does not resume it. */
    {
        const long asks = tf.aux_create_calls, sw = tf.aux_switch_calls;
        b = snap();
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "%s: a second served draw is refused", which);
        CHECK(tf.aux_create_calls == asks && tf.aux_switch_calls == sw,
              "%s: without asking the backend for anything (%ld/%ld)", which,
              tf.aux_create_calls - asks, tf.aux_switch_calls - sw);
        /* AND THE LATCHED REFUSAL IS COUNTED AT THE CALL SITE, on BOTH shapes:
         * once the process latch is up, every later served draw is refused by
         * the capability and raises ax_create_fails. That is the same number the
         * acquire shape raised on its first draw, which is why the count has to
         * be taken per refused draw and not once per failure. */
        CHECK(snap().ax_create_fails - b.ax_create_fails == 1,
              "%s: and the latched refusal IS counted as a capability refusal "
              "(%lu)", which, snap().ax_create_fails - b.ax_create_fails);
        sl_bgtf_invalidate();
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "%s: and a served draw after a context invalidation is refused "
              "too - the latch belongs to the PROCESS, not to a context", which);
        CHECK(tf.aux_create_calls == asks,
              "%s: still without asking the backend (%ld)", which,
              tf.aux_create_calls - asks);
        CHECK(tf.v_call_no_context == nocontext_before,
              "%s: and still not one GL call (%ld)", which,
              tf.v_call_no_context - nocontext_before);
    }
    /* AND TEARDOWN DESTROYS NOTHING, which is the deliberate part: a destroy
     * needs the main context current, and that is what was lost. The platform
     * releases the context with the process. */
    b = snap();
    sl_bgtf_shutdown();
    sl_bgtf_shutdown();
    a = snap();
    CHECK(tf.aux_destroyed == destroyed_before,
          "%s: teardown destroys NOTHING - a destroy needs the main context "
          "current (%ld)", which, tf.aux_destroyed - destroyed_before);
    CHECK(a.ax_destroys == b.ax_destroys,
          "%s: and counts none (%lu)", which, a.ax_destroys - b.ax_destroys);
    CHECK(tf.aux_exists == 1,
          "%s: the context is still alive and is left to the platform, which is "
          "what the module reports rather than calling into a state it cannot "
          "establish (%d)", which, tf.aux_exists);
    CHECK(tf.v_call_no_context == nocontext_before,
          "%s: and teardown made no GL call either (%ld)", which,
          tf.v_call_no_context - nocontext_before);
    CHECK(tf.calls_after_fatal == 0L && tf.fatal_calls == 1L,
          "%s: so over the WHOLE run after the loss - the latched draws, both "
          "teardowns and the invalidation between them - not one GL entry point "
          "was called, and the exit path was taken once (%ld/%ld)", which,
          tf.calls_after_fatal, tf.fatal_calls);
    /* THE GATES ARE CLEARED AND NOTHING ELSE IS. The shim is deliberately NOT
     * put back on the main context and the context is deliberately left alive:
     * nothing in the module can restore either, and a fixture that quietly did
     * so by hand would be hiding the fact that this outcome is terminal. This
     * shape is the last thing its process does, which is the only reason that is
     * acceptable here - see the note above. */
    tf.main_switch_fail = 0;
    tf.aux_create_lost = 0;
    tf.main_switch_fail_after = -1L;
}

static void ax_failures(void)
{
    begin_case("auxiliary context, FAILING CLOSED: a missing glIsProgram, a backend that "
               "offers no context, a switch that fails, a context that does not SHARE "
               "object names, and a switch that fails at the draw each REFUSE the served "
               "draw, latch, destroy any context they did create, and never issue the "
               "call on the main context");
    /* NO SHARE-CHECK ENTRY POINT: the control declines to create a context it
     * could never verify, which is the only shape that fails BEFORE the
     * create. */
    ax_fail_shape("no glIsProgram", 0, 0, 0, -1L, 0, NULL);
    ax_fail_shape("no context", 1, 1, 0, -1L, 0, NULL);
    ax_fail_shape("the acquire's switch fails", 1, 0, 1, 0L, 0, NULL);
    ax_fail_shape("no sharing", 1, 0, 0, -1L, 1, NULL);
    /* AND THE TWO THE AUXILIARY CONTEXT'S OWN ENTRY POINTS ADD. Each name is
     * withheld on that context ALONE, so the main context's set is complete
     * throughout: a module that answered the refusal by using the main
     * context's pointer would pass every count, every argument check and the
     * whole transcript, and is caught here only because the resolve is refused
     * where it is made. Both are driven rather than one, because a context that
     * can answer for the query and not the program bind is as unusable as the
     * reverse and the module must not discover it with bracket state already
     * written. */
    ax_fail_shape("the auxiliary context has no glIsProgram of its own", 1, 0, 0,
                  -1L, 0, "glIsProgram");
    ax_fail_shape("the auxiliary context has no glUseProgram of its own", 1, 0,
                  0, -1L, 0, "glUseProgram");
    ax_draw_switch_fails();
    end_case();
}

/* ONE LOST-CONTEXT SHAPE, IN ITS OWN PROCESS. `at_draw` picks which switch back
 * fails; see the note on ax_lost_shape for why the two cannot share a launch. */
static void ax_lost_case(int at_draw)
{
    begin_case("auxiliary context, THE MAIN CONTEXT LOST: a switch back that fails makes "
               "the run INVALID - the control issues nothing further, not even the "
               "bracket's restoration, refuses every later served draw through a latch "
               "that survives a context invalidation, and destroys nothing at teardown");
    ax_lost_shape(at_draw);
    end_case();
}

/* THE SWITCH **TO** THE AUXILIARY CONTEXT THAT ESTABLISHES NEITHER CONTEXT.
 *
 * WHY THIS IS A SEPARATE SHAPE FROM THE SWITCH THAT FAILS RECOVERABLY, and the
 * reason it exists at all. A failed switch is not one outcome but two, and the
 * platform does not say which one happened: the call documents only a success or
 * failure return and promises nothing about what remains current afterwards. So
 * the backend QUERIES which context is current and reports two different
 * answers - the MAIN context verified current, which is recoverable and lets the
 * bracket's restoration run; and neither context verifiable, which is not
 * recoverable at all. Treating the second as the first is exactly the defect
 * this shape refuses: the restoration's three calls would be issued on a context
 * nobody selected, and the mesh would then be rastered by a caller that believes
 * GL reaches the window.
 *
 * `at_draw` picks which switch: the acquire's own verification round trip, or
 * the one in the bracket. ONE SHAPE PER PROCESS, for the reason the lost shapes
 * are one per process - the latch belongs to the process. */
static void ax_unknown_switch_shape(int at_draw)
{
    const char *which = at_draw ? "the draw's switch is unknown"
                                : "the acquire's switch is unknown";
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    long nocontext_before, destroyed_before, draws_before;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    /* THE GATE IS ON THE SWITCH **TO** THE AUXILIARY CONTEXT. The acquire makes
     * one of its own, so letting that one through is what makes the draw's
     * reachable - the same after-count the recoverable shape uses, for the same
     * reason. */
    tf.aux_switch_unknown = 1;
    tf.aux_switch_unknown_after = at_draw ? 1L : 0L;
    b = snap();
    nocontext_before = tf.v_call_no_context;
    destroyed_before = tf.aux_destroyed;
    draws_before = tf.draw_arrays;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(a.ax_lost - b.ax_lost == 1 && a.ax_switch_fails == b.ax_switch_fails,
          "%s: counted as a LOST context and NOT as a recoverable switch "
          "failure - the two are different situations and the count a reader "
          "discards a run on is the first (%lu/%lu)", which,
          a.ax_lost - b.ax_lost, a.ax_switch_fails - b.ax_switch_fails);
    CHECK(tf.draw_arrays == draws_before,
          "%s: and NO draw was issued - not on the auxiliary context, which was "
          "never established, and not on the main one, which would be the "
          "ordinary zero-count arm (%ld)", which,
          tf.draw_arrays - draws_before);
    CHECK(tf.v_call_no_context == nocontext_before,
          "%s: NOT ONE GL call was made with no context current, which is what "
          "the bracket's restoration would have been (%ld)", which,
          tf.v_call_no_context - nocontext_before);
    CHECK(tf.fatal_calls == 1L && a.ax_fatals == 1 && tf.fatal_code == 95,
          "%s: the terminate-now path was taken exactly once, with the "
          "diagnostic's own exit status (%ld/%lu/%d)", which, tf.fatal_calls,
          a.ax_fatals, tf.fatal_code);
    CHECK(tf.calls_after_fatal == 0L,
          "%s: and NOT ONE GL entry point was called after it (%ld)", which,
          tf.calls_after_fatal);
    CHECK(tf.ctx_cur < 0,
          "%s: no context is current, which is the honest model of a switch the "
          "platform made no promise about (%d)", which, tf.ctx_cur);
    /* AND NOTHING IS DESTROYED, for the same reason the lost shapes destroy
     * nothing: a destroy needs the main context verified current. */
    b = snap();
    sl_bgtf_shutdown();
    sl_bgtf_shutdown();
    a = snap();
    CHECK(tf.aux_destroyed == destroyed_before && a.ax_destroys == b.ax_destroys,
          "%s: two teardowns destroy NOTHING and count none (%ld/%lu)", which,
          tf.aux_destroyed - destroyed_before, a.ax_destroys - b.ax_destroys);
    CHECK(tf.calls_after_fatal == 0L,
          "%s: and teardown called no GL entry point either (%ld)", which,
          tf.calls_after_fatal);
    tf.aux_switch_unknown = 0;
    tf.aux_switch_unknown_after = -1L;
}

static void ax_unknown_switch_case(int at_draw)
{
    begin_case("auxiliary context, A SWITCH THAT ESTABLISHES NEITHER CONTEXT: the backend "
               "distinguishes a failed switch that leaves the MAIN context verified "
               "current from one that leaves nothing verifiable, and the second makes the "
               "run INVALID - no draw, no restoration, no destroy, and the process ends");
    ax_unknown_switch_shape(at_draw);
    end_case();
}

/* THE DESTROY THE BACKEND REFUSES.
 *
 * The backend checks its own precondition now - the main context verified
 * current and the window alive - instead of trusting the caller's claim, and it
 * answers that it destroyed nothing when the check fails. At the ONE call site
 * that destroy has, that answer means the backend and the caller disagree about
 * a context the caller is about to delete, so it is the unknown outcome: the
 * teardown must not go on to release the main context and present a window.
 * This shape drives exactly that, which is the one path where a leak and a
 * termination are the same event. */
static void ax_destroy_refused_shape(void)
{
    const char *which = "the destroy is refused";
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0, "%s: the bracket ran and the arm is healthy (%d)", which, s);
    CHECK(tf.fatal_calls == 0L,
          "%s: with no termination so far (%ld)", which, tf.fatal_calls);
    /* THE REFUSAL IS ARMED ONLY FOR TEARDOWN, so the run up to it is the
     * ordinary scenario and the only thing under test is the teardown. */
    tf.aux_destroy_unknown = 1;
    b = snap();
    sl_bgtf_shutdown();
    a = snap();
    CHECK(tf.aux_destroy_refused == 1L && tf.aux_exists == 1,
          "%s: the backend refused the destroy and the context is still alive "
          "(%ld/%d)", which, tf.aux_destroy_refused, tf.aux_exists);
    CHECK(a.ax_destroys == b.ax_destroys,
          "%s: nothing is counted as destroyed, which a pair of matching counts "
          "would otherwise hide (%lu)", which, a.ax_destroys - b.ax_destroys);
    CHECK(a.ax_lost - b.ax_lost == 1 && a.ax_fatals - b.ax_fatals == 1
          && tf.fatal_calls == 1L && tf.fatal_code == 95,
          "%s: and it is the LOST outcome: the run is invalid and the process "
          "ends at the teardown rather than deleting the main context and "
          "presenting a window (%lu/%lu/%ld/%d)", which, a.ax_lost - b.ax_lost,
          a.ax_fatals - b.ax_fatals, tf.fatal_calls, tf.fatal_code);
    CHECK(tf.calls_after_fatal == 0L,
          "%s: with no GL entry point called after it (%ld)", which,
          tf.calls_after_fatal);
    tf.aux_destroy_unknown = 0;
}

static void ax_destroy_refused_case(void)
{
    begin_case("auxiliary context, A DESTROY THE BACKEND REFUSES: the teardown release is "
               "checked rather than assumed, and a refusal is the INVALID outcome - the "
               "context is left to the process, nothing is counted as destroyed, and the "
               "process ends instead of tearing the window down");
    ax_destroy_refused_shape();
    end_case();
}

static void st_aux_empty_off(void)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("auxiliary context default off: the control is not selected, NO SECOND GL "
               "CONTEXT was created anywhere in this process, the share check's entry "
               "point was never resolved, nothing was counted for it, and the ordinary "
               "arm did real work");
    CHECK(sl_bgtf_aux_empty() == 0,
          "SL_BG_TF_AUX_EMPTY is absent, so the control is off (%d)",
          sl_bgtf_aux_empty());
    calls_before = tf.calls;
    for (i = 0; i < 64; i++) (void) sl_bgtf_aux_empty();
    CHECK(tf.calls == calls_before,
          "and reading it costs no GL call (%ld -> %ld)", calls_before,
          tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->ax_setups == 0 && t->ax_main_omits == 0 && t->ax_draws == 0
          && t->ax_draws_aux == 0 && t->ax_switches == 0
          && t->ax_restores == 0 && t->ax_reuses == 0 && t->ax_skips == 0
          && t->ax_creates == 0 && t->ax_create_fails == 0
          && t->ax_share_fails == 0 && t->ax_switch_fails == 0
          && t->ax_lost == 0 && t->ax_destroys == 0 && t->ax_refusals == 0,
          "nothing was counted for it in the whole process "
          "(%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu)",
          t->ax_setups, t->ax_main_omits, t->ax_draws, t->ax_draws_aux,
          t->ax_switches, t->ax_restores, t->ax_reuses, t->ax_skips,
          t->ax_creates, t->ax_create_fails, t->ax_share_fails,
          t->ax_switch_fails, t->ax_lost, t->ax_destroys, t->ax_refusals);
    /* NO CONTEXT WAS CREATED AND THE BACKEND WAS NEVER EVEN ASKED, which is the
     * strongest form of "an off launch pays nothing": not a refused create, no
     * create at all. Unlike the neutral control's deletes, the resolver RECORD
     * can carry this one - nothing else in this suite asks for glIsProgram, so
     * the name's absence from the record is about the module. */
    CHECK(tf.aux_create_calls == 0L && tf.aux_created == 0L
          && tf.aux_exists == 0,
          "the backend was never asked for a context and none exists "
          "(%ld/%ld/%d)", tf.aux_create_calls, tf.aux_created, tf.aux_exists);
    CHECK(tf.aux_switch_calls == 0L && tf.main_switch_calls == 0L,
          "and no context switch was ever attempted in either direction "
          "(%ld/%ld)", tf.aux_switch_calls, tf.main_switch_calls);
    CHECK(np_asked_for("glIsProgram") == 0 && tf.is_program_calls == 0L,
          "the share check's entry point was never asked for and never called "
          "(%d/%ld)", np_asked_for("glIsProgram"), tf.is_program_calls);
    CHECK(tf.ctx_cur == CTX_MAIN && tf.calls_on_aux == 0L
          && tf.draws_on_aux == 0L,
          "the MAIN context is current and nothing ever landed on another one "
          "(%d/%ld/%ld)", tf.ctx_cur, tf.calls_on_aux, tf.draws_on_aux);
    /* AND TEARDOWN IS A NO-OP ON AN OFF LAUNCH. */
    sl_bgtf_shutdown();
    CHECK(tf.aux_destroyed == 0L && sl_bgtf_stats()->ax_destroys == 0,
          "teardown destroys nothing and counts none (%ld/%lu)",
          tf.aux_destroyed, sl_bgtf_stats()->ax_destroys);
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "while the ordinary arm ran prepasses over vertices (%lu / %lu)",
          t->prepasses, t->vertices);
    end_case();
}

/* THE SWITCH'S GRAMMAR, COUPLING, EXCLUSIVITY AND REFUSAL ACCOUNTING, one
 * setting per launch. The runner says what the control must ANSWER and whether
 * this launch asked for it and did not get it. */
static void mode_env_aux_empty(int expect_on, int want_refusal)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("auxiliary-context switch: the grammar, the coupling to the zero-count "
               "arm, the eight exclusions and the refusal accounting");
    CHECK(sl_bgtf_aux_empty() == expect_on,
          "the control answers %d (%d)", expect_on, sl_bgtf_aux_empty());
    /* PARSED ONCE FOR THE LAUNCH: repeated reads must cost nothing and must not
     * drift, which is what lets the draw site read it per served draw. */
    calls_before = tf.calls;
    for (i = 0; i < 64; i++)
        CHECK(sl_bgtf_aux_empty() == expect_on,
              "and answers the same on read %d", i);
    CHECK(tf.calls == calls_before,
          "reading it costs no GL call (%ld -> %ld)", calls_before, tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->ax_refusals == (want_refusal ? 1UL : 0UL),
          "the launch is counted as %s, exactly once (%lu)",
          want_refusal ? "a REFUSAL" : "no refusal", t->ax_refusals);
    /* A REFUSED OR ABSENT CONTROL DID NO WORK AND CREATED NOTHING - and for this
     * control "nothing" includes a GL CONTEXT, which is why the backend counts
     * are asserted beside the module's. */
    if (!expect_on) {
        CHECK(t->ax_setups == 0 && t->ax_draws == 0 && t->ax_switches == 0
              && t->ax_restores == 0 && t->ax_creates == 0
              && t->ax_create_fails == 0 && t->ax_skips == 0
              && t->ax_main_omits == 0,
              "and nothing at all was counted for a control that is not in "
              "force");
        CHECK(tf.aux_create_calls == 0L && tf.aux_created == 0L
              && tf.aux_switch_calls == 0L && tf.main_switch_calls == 0L
              && np_asked_for("glIsProgram") == 0,
              "NO AUXILIARY GL CONTEXT was asked for, created or switched to, "
              "and the share check's name was never resolved (%ld/%ld/%ld/%ld/"
              "%d)", tf.aux_create_calls, tf.aux_created, tf.aux_switch_calls,
              tf.main_switch_calls, np_asked_for("glIsProgram"));
    }
    /* AND EVERY OTHER CONTROL IS LEFT EXACTLY AS ITS OWN CLAUSES DECIDED IT.
     * This control stands down alone and writes no other control's answer; the
     * runner's own expectations cover what each of them is on each launch. */
    CHECK(sl_bgtf_state_only() == sl_bgtf_state_only()
          && sl_bgtf_pos_input() == sl_bgtf_pos_input(),
          "the older controls answer stably beside this one "
          "(%d/%d/%d/%d/%s/%s/%d)",
          sl_bgtf_state_only(), sl_bgtf_pos_input(), sl_bgtf_private_vao(),
          sl_bgtf_zero_count(), sl_bgtf_es_name(sl_bgtf_empty_state()),
          sl_bgtf_pr_name(sl_bgtf_post_reset()), sl_bgtf_neutral_prog());
    end_case();
}

/* ---- 19. THE POSITION-ONLY-INPUT CONTROL (SL_BG_TF_POS_INPUT) -----------
 *
 * WHAT IS BEING MEASURED. The control keeps the prepass draw and its
 * transform-feedback writes and narrows the CLIENT ARRAYS that draw
 * dereferences to the position array the program reads. So there are three
 * claims, and they fail in different ways:
 *
 *   - the draw still ran and the GPU work is still counted;
 *   - AT the draw, every non-position array was off and the position array was
 *     still on - measured from the stub's snapshot taken inside glDrawArrays,
 *     because state read after the draw describes the restore instead;
 *   - AFTER the draw, the client state equals the state before it FIELD FOR
 *     FIELD, including the client active unit, so the restore is exact rather
 *     than merely numerous.
 *
 * THE INHERITED STATE IS MULTI-UNIT ON PURPOSE. Coordinate arrays are installed
 * on units 0, 1 and 3 - with unit 2 deliberately OFF, so a restore that enables
 * the set it knows about rather than the set it found is visible - and the
 * inherited client active unit is 1 rather than 0, so a control that assumed
 * unit 0 or restored the wrong unit cannot pass.
 */

/* The inherited picture: which units carry a coordinate array, and which single
 * arrays are on. Unit 2 is off between two that are on; the edge-flag and index
 * arrays are off so that "only what was found" has something to leave alone. */
#define PI_UNITS_USED 4
static const GLboolean pi_t_in[PI_UNITS_USED] = { GL_TRUE, GL_TRUE, GL_FALSE, GL_TRUE };
#define PI_INHERITED_UNIT  (GL_TEXTURE0 + 1)

/* Install it. The position array is ON, because the prepass reads it and the
 * control must leave it alone. */
static void pi_install_inherited(void)
{
    sl_bgtfgl_test_client(PI_INHERITED_UNIT, pi_t_in, PI_UNITS_USED,
                          (GLboolean) GL_TRUE,    /* vertex - position        */
                          (GLboolean) GL_TRUE,    /* colour                   */
                          (GLboolean) GL_TRUE,    /* normal                   */
                          (GLboolean) GL_FALSE,   /* index - left off         */
                          (GLboolean) GL_FALSE,   /* edge flag - left off     */
                          (GLboolean) GL_TRUE,    /* secondary colour         */
                          (GLboolean) GL_TRUE);   /* fog coordinate           */
}

/* How many arrays that picture has enabled besides the position array: three
 * coordinate arrays and four single ones. Written out rather than counted from
 * the table, so a change to the table has to come past this number. */
#define PI_EXPECT_OFF 7

/* The whole client picture, for a field-for-field comparison. */
struct pi_snap {
    GLint     unit;
    GLboolean t[BGTFGL_UNITCAP];
    GLboolean v, c, n, i, e, s, f;
};

static struct pi_snap pi_now(void)
{
    struct pi_snap p;
    int u;
    p.unit = tf.client_unit;
    for (u = 0; u < BGTFGL_UNITCAP; u++) p.t[u] = tf.cli_t[u];
    p.v = tf.cli_v; p.c = tf.cli_c; p.n = tf.cli_n;
    p.i = tf.cli_i; p.e = tf.cli_e; p.s = tf.cli_s; p.f = tf.cli_f;
    return p;
}

/* The first field that differs, named, or NULL. A count of differences would
 * say that a restore was wrong; this says which array or unit it got wrong. */
static const char *pi_differs(const struct pi_snap *a, const struct pi_snap *b)
{
    static char buf[64];
    int u;
    if (a->unit != b->unit) return "the client active texture unit";
    for (u = 0; u < BGTFGL_UNITCAP; u++)
        if (a->t[u] != b->t[u]) {
            snprintf(buf, sizeof buf, "unit %d's coordinate array", u);
            return buf;
        }
    if (a->v != b->v) return "the POSITION array";
    if (a->c != b->c) return "the colour array";
    if (a->n != b->n) return "the normal array";
    if (a->i != b->i) return "the index array";
    if (a->e != b->e) return "the edge flag array";
    if (a->s != b->s) return "the secondary colour array";
    if (a->f != b->f) return "the fog coordinate array";
    return NULL;
}

/* A walk ready to serve prepasses, with the inherited client state installed and
 * the counters' window opened after it.
 *
 * THE TWO LIMITS ARE SEPARATE ARGUMENTS, because the correction they carry is
 * that they are different numbers: `units_reported` is the conventional
 * fixed-function count (GL_MAX_TEXTURE_UNITS) and `coords_reported` is the
 * coordinate-set count (GL_MAX_TEXTURE_COORDS) that the per-unit selector
 * selects among and that the sweep is bounded by. 0 means "report the fixture's
 * full width" for either. */
static GLuint pi_arm_walk(struct sl_bggpu_walk *w, int provide_selector,
                          long units_reported, long coords_reported)
{
    GLuint vbo;
    arm();
    tf.provide_client_active_texture = provide_selector;
    tf.units_reported = units_reported;
    tf.coords_reported = coords_reported;
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    pi_install_inherited();
    return vbo;
}

static void pi_scenario(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct pi_snap before, after;
    GLuint vbo;
    long first_client, k;
    int s, u;

    begin_case("position-only input: the prepass draw RUNS and does transform-feedback "
               "work, every non-position client array is off AT the draw across several "
               "units, and the exact inherited state and unit come back");
    vbo = pi_arm_walk(&w, 1, 0L, 0L);
    CHECK(sl_bgtf_pos_input() == 1, "the control is selected (%d)",
          sl_bgtf_pos_input());
    CHECK(sl_bgtf_state_only() == 0,
          "and the state-only control is not, so the draw is kept (%d)",
          sl_bgtf_state_only());

    before = pi_now();
    CHECK(before.unit == PI_INHERITED_UNIT,
          "the inherited client active unit is NOT unit 0 (%d, expected %d)",
          (int) before.unit, (int) PI_INHERITED_UNIT);

    first_client = tf.nlog;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    after = pi_now();
    CHECK(s >= 0 && s < SLOTS, "a slot was filled (%d)", s);

    /* ---- the draw still happened, and the work is still claimed ---------- */
    CHECK(tf.draw_arrays == 1 && tf.draw_vertices == (long) NV,
          "ONE real prepass draw over nverts vertices (%ld / %ld)",
          tf.draw_arrays, tf.draw_vertices);
    CHECK(a.prepasses - b.prepasses == 1
          && a.vertices - b.vertices == (unsigned long) NV,
          "counted as real transform-feedback work (%lu / %lu)",
          a.prepasses - b.prepasses, a.vertices - b.vertices);
    CHECK(a.so_setups == b.so_setups && a.so_omitted == b.so_omitted,
          "and NOT as the state-only control's setup (%lu / %lu)",
          a.so_setups - b.so_setups, a.so_omitted - b.so_omitted);
    CHECK(tf.tf_buf == 0u && tf.discard == 0 && tf.current == 0,
          "with the bracket restored as on every other arm (tf %u, discard %d, "
          "program %u)", (unsigned) tf.tf_buf, tf.discard,
          (unsigned) tf.current);

    /* ---- AT the draw, positions only ------------------------------------ */
    CHECK(tf.cli_at_draw_seen,
          "the stub sampled the client arrays at the draw");
    CHECK(tf.cli_at_draw_v,
          "the POSITION array was still enabled at the draw (%d)",
          (int) tf.cli_at_draw_v);
    CHECK(!tf.cli_at_draw_c && !tf.cli_at_draw_n && !tf.cli_at_draw_i
          && !tf.cli_at_draw_e && !tf.cli_at_draw_s && !tf.cli_at_draw_f,
          "and every non-position single array was OFF at it "
          "(c%d n%d i%d e%d s%d f%d)",
          (int) tf.cli_at_draw_c, (int) tf.cli_at_draw_n,
          (int) tf.cli_at_draw_i, (int) tf.cli_at_draw_e,
          (int) tf.cli_at_draw_s, (int) tf.cli_at_draw_f);
    for (u = 0; u < BGTFGL_UNITS; u++)
        CHECK(!tf.cli_at_draw_t[u],
              "unit %d's coordinate array was OFF at the draw (%d)", u,
              (int) tf.cli_at_draw_t[u]);
    CHECK(tf.cli_at_draw_unit == PI_INHERITED_UNIT,
          "and the draw ran under the INHERITED unit selection, not one the "
          "sweep left behind (%d, expected %d)", (int) tf.cli_at_draw_unit,
          (int) PI_INHERITED_UNIT);

    /* ---- AFTER the draw, exactly what was there ------------------------- */
    {
        const char *d = pi_differs(&before, &after);
        CHECK(d == NULL,
              "the client state came back field for field; first difference: %s",
              d == NULL ? "(none)" : d);
    }
    CHECK(tf.v_vertex_array_touched == 0,
          "the position array's own enable was NEVER written (%ld)",
          tf.v_vertex_array_touched);
    CHECK(tf.v_client_unit_range == 0 && tf.v_client_state_unknown == 0,
          "no unit outside the modelled range and no unmodelled token (%ld/%ld)",
          tf.v_client_unit_range, tf.v_client_state_unknown);

    /* ---- the counters say what was removed and put back ----------------- */
    CHECK(a.pi_draws - b.pi_draws == 1,
          "one draw ran with the isolation in force (%lu)",
          a.pi_draws - b.pi_draws);
    CHECK(a.pi_off - b.pi_off == (unsigned long) PI_EXPECT_OFF,
          "and %d arrays were disabled for it (%lu)", PI_EXPECT_OFF,
          a.pi_off - b.pi_off);
    CHECK(a.pi_back - b.pi_back == a.pi_off - b.pi_off,
          "with exactly as many put back (%lu off, %lu back)",
          a.pi_off - b.pi_off, a.pi_back - b.pi_back);
    CHECK(a.pi_units >= (unsigned long) BGTFGL_UNITS,
          "the sweep covered every unit the context reports (%lu, reported %d)",
          a.pi_units, BGTFGL_UNITS);
    CHECK(a.pi_refusals == b.pi_refusals,
          "and nothing was refused (%lu)", a.pi_refusals - b.pi_refusals);
    CHECK(tf.client_disable == PI_EXPECT_OFF && tf.client_enable == PI_EXPECT_OFF,
          "the stub saw the same numbers from the other side (%ld off, %ld on)",
          tf.client_disable, tf.client_enable);

    /* ---- EVERY QUERY PRECEDED EVERY WRITE, which is what makes "settled
     * before any state is written" a property of the order rather than a
     * promise: a refusal after a disable would have left partial state. And NO
     * POINTER COMMAND was issued once the isolation began - the arrays come back
     * pointing where they pointed, so the control alters no pointer. */
    {
        long first_write = -1, last_query = -1, ptr_after = 0;
        for (k = first_client; k < tf.nlog; k++) {
            const int kind = tf.log[k].kind;
            if (kind == TFGL_ISENABLED || kind == TFGL_GETINTEGERV) last_query = k;
            if (kind == TFGL_CLIENTSTATE && first_write < 0) first_write = k;
            if (first_write >= 0 && kind == TFGL_VERTEXPOINTER) ptr_after++;
        }
        CHECK(first_write > 0 && last_query > 0 && last_query < first_write,
              "every client-state QUERY ran before the first enable or disable "
              "(last query at %ld, first write at %ld)", last_query, first_write);
        CHECK(ptr_after == 0,
              "and the isolation issued NO pointer command (%ld)", ptr_after);
    }

    /* ---- the pool is real, so it binds -------------------------------- */
    CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_SINGLE,
          "the pool this draw WROTE binds and reports its depth - this arm does "
          "real work, unlike the state-only control's unwritten slot");
    sl_bgtf_unbind();
    (void) vbo;
    end_case();
}

/* THE CADENCE IS UNTOUCHED. A second draw at the same key is a cache hit: no
 * draw, so no isolation and not one client-state call. A new key is a miss and
 * isolates again. Without this the control could have become per-mesh work. */
static void pi_cadence(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct pi_snap before, after;
    int s, s2, sf;

    begin_case("position-only input: a cache HIT costs no isolation and no client-state "
               "call, and a cache MISS isolates again - the cadence is the key's");
    (void) pi_arm_walk(&w, 1, 0L, 0L);
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0, "the first draw was served (%d)", s);

    before = pi_now();
    b = snap();
    sl_bgtfgl_log_reset();
    s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    after = pi_now();
    CHECK(s2 == s, "the same key is served by the same slot (%d vs %d)", s2, s);
    CHECK(a.reuses - b.reuses == 1, "counted as a pool reuse (%lu)",
          a.reuses - b.reuses);
    CHECK(tf.draw_arrays == 0, "no draw ran for it (%ld)", tf.draw_arrays);
    CHECK(a.pi_draws == b.pi_draws,
          "so no isolation was performed (%lu)", a.pi_draws - b.pi_draws);
    CHECK(tf.client_state == 0 && tf.client_active_texture == 0
          && tf.is_enabled == 0,
          "and not one client-state call was made (%ld/%ld/%ld)",
          tf.client_state, tf.client_active_texture, tf.is_enabled);
    CHECK(pi_differs(&before, &after) == NULL,
          "the client state is untouched by a reuse");

    /* A MISS. FAR0's real opaque pair is a different key, so it isolates. */
    b = snap();
    sl_bgtfgl_log_reset();
    sf = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    after = pi_now();
    CHECK(sf >= 0 && sf != s, "a new key took its own slot (%d vs %d)", sf, s);
    CHECK(tf.draw_arrays == 1, "its draw ran (%ld)", tf.draw_arrays);
    CHECK(a.pi_draws - b.pi_draws == 1,
          "and isolated once more (%lu)", a.pi_draws - b.pi_draws);
    CHECK(a.pi_off - b.pi_off == (unsigned long) PI_EXPECT_OFF
          && a.pi_back - b.pi_back == (unsigned long) PI_EXPECT_OFF,
          "over the same arrays, all put back (%lu off, %lu back)",
          a.pi_off - b.pi_off, a.pi_back - b.pi_back);
    CHECK(pi_differs(&before, &after) == NULL,
          "with the client state exact again after it");
    sl_bgtf_unbind();
    end_case();
}

/* THE TWO WAYS THE REQUIRED STATE CANNOT BE ACCOUNTED FOR, and the one answer to
 * both: isolate NOTHING, report it, and let the draw run exactly as it does
 * without this control. Partial isolation reported as isolation is the failure
 * these cases exist to forbid. */
static void pi_unsupported(const char *which)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct pi_snap before, after;
    const int no_selector = strcmp(which, "noapi") == 0;
    const int no_count    = strcmp(which, "nocount") == 0;
    int s;

    begin_case(no_selector
               ? "position-only input: with NO glClientActiveTexture the isolation is "
                 "refused whole - the draw runs with its inherited arrays and no "
                 "isolation is claimed"
               : no_count
               ? "position-only input: a GL_MAX_TEXTURE_UNITS query that answers "
                 "NOTHING refuses the isolation rather than assuming one unit"
               : "position-only input: a coordinate-set count past the control's "
                 "ceiling refuses the isolation rather than sweeping part of the "
                 "range");
    /* The selector is withheld for the first case; for the others it resolves
     * and the COORDINATE-SET count is what cannot be used - the conventional
     * unit count is no longer read by the module at all. */
    (void) pi_arm_walk(&w, no_selector ? 0 : 1, 0L,
                       (!no_selector && !no_count) ? (long) (BGTFGL_UNITS + 1) : 0L);
    if (no_count) tf.getintegerv_fail = 1;

    before = pi_now();
    b = snap();
    sl_bgtfgl_log_reset();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    after = pi_now();
    tf.getintegerv_fail = 0;

    CHECK(s >= 0, "the draw was still served (%d)", s);
    CHECK(tf.draw_arrays == 1 && tf.draw_vertices == (long) NV,
          "the prepass draw RAN, exactly as without this control (%ld / %ld)",
          tf.draw_arrays, tf.draw_vertices);
    CHECK(a.prepasses - b.prepasses == 1,
          "and its transform-feedback work is counted (%lu)",
          a.prepasses - b.prepasses);
    CHECK(a.pi_draws == b.pi_draws,
          "NO isolation was claimed (%lu)", a.pi_draws - b.pi_draws);
    CHECK(a.pi_refusals - b.pi_refusals == 1,
          "the refusal was counted instead (%lu)", a.pi_refusals - b.pi_refusals);
    CHECK(a.pi_off == b.pi_off && a.pi_back == b.pi_back,
          "with nothing disabled and nothing re-enabled (%lu/%lu)",
          a.pi_off - b.pi_off, a.pi_back - b.pi_back);
    CHECK(tf.client_state == 0,
          "and not one client array was written (%ld)", tf.client_state);
    CHECK(pi_differs(&before, &after) == NULL,
          "the inherited state is EXACTLY as it was - no partial sweep");
    /* THE DRAW SAW THE INHERITED ARRAYS, which is the honest consequence of
     * refusing: this run is not an isolated one and must not read as one. */
    CHECK(tf.cli_at_draw_seen && tf.cli_at_draw_c && tf.cli_at_draw_t[0],
          "the draw dereferenced the inherited arrays (colour %d, unit 0 %d)",
          (int) tf.cli_at_draw_c, (int) tf.cli_at_draw_t[0]);
    if (no_selector)
        CHECK(tf.client_active_texture == 0,
              "and no unit was ever selected (%ld)", tf.client_active_texture);
    sl_bgtf_unbind();
    end_case();
}

/* ---- THE DOMAIN IS THE COORDINATE SETS', NOT THE CONVENTIONAL UNITS' -----
 *
 * WHAT THIS CASE IS FOR. glClientActiveTexture selects among texture COORDINATE
 * SETS (GL_MAX_TEXTURE_COORDS), and the OpenGL 2.1 spec allows the conventional
 * fixed-function unit count (GL_MAX_TEXTURE_UNITS) to be SMALLER - section 2.7
 * gives the coordinate sets, section 2.8 ranges the selector over them, section
 * 2.11.2 keeps the conventional units apart. A sweep bounded by the unit count
 * would therefore
 * leave the coordinate arrays above it ENABLED and feeding the real prepass
 * draw, while reporting a successful isolation.
 *
 * So this context reports TWO conventional units and FOUR coordinate sets, and
 * a coordinate array is enabled on set 3 - above the unit count, inside the
 * coordinate domain - with the inherited selector on set 3 as well, so a control
 * that restored the wrong one cannot pass either. The claim is the one the
 * ordinary scenario makes, measured where the smaller domain would have failed:
 * set 3's array is OFF at the draw, and the exact state comes back. */
static void pi_coords_domain(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct pi_snap before, after;
    const GLint sel = (GLint) (GL_TEXTURE0 + 3);
    int s, u;

    begin_case("position-only input: the sweep covers the COORDINATE SET domain, not the "
               "smaller conventional unit count - two units reported, four coordinate "
               "sets, an array enabled on set 3 and the inherited selector on set 3");
    arm();
    tf.provide_client_active_texture = 1;
    tf.units_reported  = 2L;   /* GL_MAX_TEXTURE_UNITS - the SMALLER number  */
    tf.coords_reported = 4L;   /* GL_MAX_TEXTURE_COORDS - the real domain    */
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    {
        GLuint vbo = input_vbo(NV, STR);
        CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    }
    /* The same picture as the ordinary scenario - set 2 off between two that are
     * on - but selected from set 3 rather than set 1. */
    sl_bgtfgl_test_client(sel, pi_t_in, PI_UNITS_USED,
                          (GLboolean) GL_TRUE,    /* vertex - position        */
                          (GLboolean) GL_TRUE,    /* colour                   */
                          (GLboolean) GL_TRUE,    /* normal                   */
                          (GLboolean) GL_FALSE,   /* index - left off         */
                          (GLboolean) GL_FALSE,   /* edge flag - left off     */
                          (GLboolean) GL_TRUE,    /* secondary colour         */
                          (GLboolean) GL_TRUE);   /* fog coordinate           */

    before = pi_now();
    CHECK(before.unit == sel,
          "the inherited selector is set 3, ABOVE the conventional unit count "
          "(%d, expected %d)", (int) before.unit, (int) sel);
    CHECK(before.t[3],
          "and set 3 carries an enabled coordinate array (%d)", (int) before.t[3]);

    b = snap();
    sl_bgtfgl_log_reset();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    after = pi_now();

    CHECK(s >= 0 && s < SLOTS, "a slot was filled (%d)", s);
    CHECK(tf.draw_arrays == 1 && tf.draw_vertices == (long) NV,
          "ONE real prepass draw over nverts vertices (%ld / %ld)",
          tf.draw_arrays, tf.draw_vertices);
    CHECK(a.prepasses - b.prepasses == 1,
          "counted as real transform-feedback work (%lu)",
          a.prepasses - b.prepasses);

    /* THE ASSERTION THE SMALLER DOMAIN FAILS. Set 3 is past the two
     * conventional units, so a sweep bounded by that count never reached it and
     * its array would still be feeding this draw. */
    CHECK(tf.cli_at_draw_seen, "the stub sampled the client arrays at the draw");
    CHECK(!tf.cli_at_draw_t[3],
          "SET 3's coordinate array was OFF at the draw, although it is past the "
          "conventional unit count (%d)", (int) tf.cli_at_draw_t[3]);
    for (u = 0; u < BGTFGL_UNITS; u++)
        CHECK(!tf.cli_at_draw_t[u],
              "set %d's coordinate array was OFF at the draw (%d)", u,
              (int) tf.cli_at_draw_t[u]);
    CHECK(tf.cli_at_draw_v, "the POSITION array was still enabled at the draw (%d)",
          (int) tf.cli_at_draw_v);
    CHECK(!tf.cli_at_draw_c && !tf.cli_at_draw_n && !tf.cli_at_draw_i
          && !tf.cli_at_draw_e && !tf.cli_at_draw_s && !tf.cli_at_draw_f,
          "and every non-position single array was OFF at it "
          "(c%d n%d i%d e%d s%d f%d)",
          (int) tf.cli_at_draw_c, (int) tf.cli_at_draw_n,
          (int) tf.cli_at_draw_i, (int) tf.cli_at_draw_e,
          (int) tf.cli_at_draw_s, (int) tf.cli_at_draw_f);
    CHECK(tf.cli_at_draw_unit == sel,
          "and the draw ran under the inherited set-3 selection (%d, expected %d)",
          (int) tf.cli_at_draw_unit, (int) sel);

    /* AND THE EXACT STATE COMES BACK, selector included. */
    {
        const char *d = pi_differs(&before, &after);
        CHECK(d == NULL,
              "the client state came back field for field; first difference: %s",
              d == NULL ? "(none)" : d);
    }
    CHECK(after.unit == sel,
          "the inherited set-3 selector was restored, not set 0 or a swept one "
          "(%d, expected %d)", (int) after.unit, (int) sel);
    CHECK(tf.v_vertex_array_touched == 0,
          "the position array's own enable was NEVER written (%ld)",
          tf.v_vertex_array_touched);
    CHECK(tf.v_client_unit_range == 0 && tf.v_client_state_unknown == 0
          && tf.v_unknown_enum == 0,
          "no unit outside the modelled range, no unmodelled client token and no "
          "unmodelled query (%ld/%ld/%ld)", tf.v_client_unit_range,
          tf.v_client_state_unknown, tf.v_unknown_enum);

    /* THE COUNTERS SAY WHICH DOMAIN WAS SWEPT, which is what makes the fix
     * legible in a census rather than only in a test: four, the coordinate set
     * count, and not two. */
    CHECK(a.pi_draws - b.pi_draws == 1,
          "one draw ran with the isolation in force (%lu)", a.pi_draws - b.pi_draws);
    CHECK(a.pi_units - b.pi_units == 4ul || a.pi_units == 4ul,
          "and the sweep's width is the COORDINATE SET count, 4, not the unit "
          "count 2 (%lu)", a.pi_units);
    CHECK(a.pi_off - b.pi_off == (unsigned long) PI_EXPECT_OFF,
          "%d arrays were disabled for it (%lu)", PI_EXPECT_OFF, a.pi_off - b.pi_off);
    CHECK(a.pi_back - b.pi_back == a.pi_off - b.pi_off,
          "with exactly as many put back (%lu off, %lu back)",
          a.pi_off - b.pi_off, a.pi_back - b.pi_back);
    CHECK(a.pi_refusals == b.pi_refusals,
          "and nothing was refused (%lu)", a.pi_refusals - b.pi_refusals);
    sl_bgtf_unbind();
    end_case();
}

/* ---- THE INHERITED SELECTOR IS VALIDATED BEFORE ANYTHING IS WRITTEN ------
 *
 * WHAT THESE CASES ARE FOR. The save has to put the inherited client active unit
 * back after the draw, so the control must first be told what it is. A query
 * that writes nothing, or that answers something this context cannot select
 * among, leaves no value to restore - and isolating anyway would mean disabling
 * arrays and then selecting an invalid set while reporting success.
 *
 * So the answer is validated as an actual GL_TEXTURE0 + index inside the
 * validated coordinate domain, BEFORE any selector or array-state mutation. Each
 * case below requires the same four things of a rejected answer: the context is
 * unchanged, NOT ONE selector, disable or enable call was made, the refusal and
 * the work counts are truthful, and the real prepass draw still ran.
 *
 *   selnone   the query writes NOTHING, so the save keeps its seeded value. It
 *             is a negative sentinel rather than 0 precisely so this case is
 *             distinguishable from an answer of unit 0.
 *   selbad    the query answers enum 0 - which is not GL_TEXTURE0, and is
 *             exactly what a zeroed save struct would have held.
 *   selover   the FIRST INVALID index: GL_TEXTURE0 + coords, one past the
 *             validated domain.
 *   selmax    the LAST VALID index: GL_TEXTURE0 + coords - 1, which must be
 *             ACCEPTED - a validation that rejected it would have made the
 *             widest legal selection unreachable. */
static void pi_selector(const char *which)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct pi_snap before, after;
    const int none  = strcmp(which, "selnone") == 0;
    const int bad   = strcmp(which, "selbad")  == 0;
    const int over  = strcmp(which, "selover") == 0;
    const int maxok = strcmp(which, "selmax")  == 0;
    const long coords = 4L;
    const GLint lastok = (GLint) (GL_TEXTURE0 + (coords - 1));
    int s;

    begin_case(none
               ? "position-only input: a GL_CLIENT_ACTIVE_TEXTURE query that writes "
                 "NOTHING refuses before any state is written - the seeded sentinel is "
                 "not a unit"
               : bad
               ? "position-only input: an answer of enum 0 - what a zeroed save would "
                 "hold - is not GL_TEXTURE0 and refuses before any state is written"
               : over
               ? "position-only input: the FIRST INVALID index, one past the validated "
                 "coordinate domain, refuses before any state is written"
               : "position-only input: the LAST VALID index is ACCEPTED, so the widest "
                 "legal selection is still isolated and restored");
    /* The last-valid case selects from that index for real, so the restore has
     * the same value to put back; the rejected cases keep the ordinary
     * inherited selector and have the ANSWER forced, which is what lets "the
     * context is unchanged" stay a real comparison. */
    (void) pi_arm_walk(&w, 1, 0L, coords);
    if (maxok) {
        sl_bgtfgl_test_client(lastok, pi_t_in, PI_UNITS_USED,
                              (GLboolean) GL_TRUE, (GLboolean) GL_TRUE,
                              (GLboolean) GL_TRUE, (GLboolean) GL_FALSE,
                              (GLboolean) GL_FALSE, (GLboolean) GL_TRUE,
                              (GLboolean) GL_TRUE);
        tf.client_unit_forced = 1;
        tf.client_unit_answer = (long) lastok;
    } else if (none) {
        tf.client_unit_fail = 1;
    } else if (bad) {
        tf.client_unit_forced = 1;
        tf.client_unit_answer = 0L;
    } else {
        tf.client_unit_forced = 1;
        tf.client_unit_answer = (long) (GL_TEXTURE0 + coords);
    }

    before = pi_now();
    b = snap();
    sl_bgtfgl_log_reset();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    after = pi_now();
    tf.client_unit_fail = 0;
    tf.client_unit_forced = 0;
    tf.client_unit_answer = 0L;

    /* THE DRAW IS KEPT ON EVERY ONE OF THESE PATHS. A refusal narrows nothing;
     * it does not skip the prepass. */
    CHECK(s >= 0, "the draw was still served (%d)", s);
    CHECK(tf.draw_arrays == 1 && tf.draw_vertices == (long) NV,
          "the prepass draw RAN over nverts vertices (%ld / %ld)",
          tf.draw_arrays, tf.draw_vertices);
    CHECK(a.prepasses - b.prepasses == 1,
          "and its transform-feedback work is counted (%lu)",
          a.prepasses - b.prepasses);
    CHECK(tf.v_unknown_enum == 0,
          "every query the module made is one this stub models (%ld)",
          tf.v_unknown_enum);

    if (maxok) {
        /* THE BOUNDARY THAT MUST BE ACCEPTED. */
        CHECK(a.pi_draws - b.pi_draws == 1,
              "the last valid index was ACCEPTED and the draw isolated (%lu)",
              a.pi_draws - b.pi_draws);
        CHECK(a.pi_refusals == b.pi_refusals,
              "with nothing refused (%lu)", a.pi_refusals - b.pi_refusals);
        CHECK(a.pi_off - b.pi_off == (unsigned long) PI_EXPECT_OFF
              && a.pi_back - b.pi_back == (unsigned long) PI_EXPECT_OFF,
              "over the arrays that were on, all put back (%lu off, %lu back)",
              a.pi_off - b.pi_off, a.pi_back - b.pi_back);
        CHECK(tf.cli_at_draw_seen && !tf.cli_at_draw_c
              && !tf.cli_at_draw_t[coords - 1],
              "the draw read positions only (colour %d, set %ld %d)",
              (int) tf.cli_at_draw_c, coords - 1,
              (int) tf.cli_at_draw_t[coords - 1]);
        CHECK(tf.cli_at_draw_unit == lastok,
              "under the inherited last-valid selection (%d, expected %d)",
              (int) tf.cli_at_draw_unit, (int) lastok);
        CHECK(pi_differs(&before, &after) == NULL,
              "and the client state came back field for field");
        CHECK(after.unit == lastok,
              "with the last-valid selector restored (%d, expected %d)",
              (int) after.unit, (int) lastok);
    } else {
        /* A REJECTED ANSWER CHANGES NOTHING AT ALL. */
        CHECK(a.pi_draws == b.pi_draws,
              "NO isolation was claimed (%lu)", a.pi_draws - b.pi_draws);
        CHECK(a.pi_refusals - b.pi_refusals == 1,
              "the refusal was counted instead (%lu)", a.pi_refusals - b.pi_refusals);
        CHECK(a.pi_off == b.pi_off && a.pi_back == b.pi_back,
              "with nothing disabled and nothing re-enabled (%lu/%lu)",
              a.pi_off - b.pi_off, a.pi_back - b.pi_back);
        /* THE ORDER IS THE PROPERTY. The validation runs before the per-unit
         * loop, so a rejected answer costs not one selector call and not one
         * array write - which is what "before any selector or array-state
         * mutation" means, measured rather than promised. */
        CHECK(tf.client_active_texture == 0,
              "NOT ONE unit was selected (%ld)", tf.client_active_texture);
        CHECK(tf.client_state == 0 && tf.client_enable == 0
              && tf.client_disable == 0,
              "and not one array was disabled or enabled (%ld/%ld/%ld)",
              tf.client_state, tf.client_enable, tf.client_disable);
        CHECK(pi_differs(&before, &after) == NULL,
              "the inherited state is EXACTLY as it was - no partial sweep");
        CHECK(tf.v_vertex_array_touched == 0,
              "and the position array's own enable was never written (%ld)",
              tf.v_vertex_array_touched);
        /* THE DRAW SAW THE INHERITED ARRAYS, which is the honest consequence of
         * refusing: this run is not an isolated one and must not read as one. */
        CHECK(tf.cli_at_draw_seen && tf.cli_at_draw_c && tf.cli_at_draw_t[0],
              "the draw dereferenced the inherited arrays (colour %d, set 0 %d)",
              (int) tf.cli_at_draw_c, (int) tf.cli_at_draw_t[0]);
    }
    sl_bgtf_unbind();
    end_case();
}

/* A PREPASS THAT REFUSES claims no isolation either: there is no draw to
 * isolate, so the client state must be untouched and the counters silent. */
static void pi_refusal(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct pi_snap before, after;
    int s;

    begin_case("position-only input: an allocation the driver leaves EMPTY refuses the "
               "draw, so there is nothing to isolate and nothing is claimed");
    (void) pi_arm_walk(&w, 1, 0L, 0L);
    tf.alloc_short_initial = 1;
    tf.alloc_bytes = 0L;
    before = pi_now();
    b = snap();
    sl_bgtfgl_log_reset();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    after = pi_now();
    tf.alloc_short_initial = 0;

    CHECK(s == -1, "the draw is refused (%d)", s);
    CHECK(tf.draw_arrays == 0, "no draw ran (%ld)", tf.draw_arrays);
    CHECK(a.pi_draws == b.pi_draws && a.pi_off == b.pi_off
          && a.pi_back == b.pi_back && a.pi_refusals == b.pi_refusals,
          "and the control counted nothing at all (%lu/%lu/%lu/%lu)",
          a.pi_draws - b.pi_draws, a.pi_off - b.pi_off,
          a.pi_back - b.pi_back, a.pi_refusals - b.pi_refusals);
    CHECK(tf.client_state == 0 && tf.client_active_texture == 0,
          "with no client-state call made (%ld/%ld)", tf.client_state,
          tf.client_active_texture);
    CHECK(pi_differs(&before, &after) == NULL,
          "and the inherited state untouched");
    CHECK(sl_bgtf_bind_clip(0) == -1 && sl_bgtf_bind_clip(1) == -1,
          "the invalid slot cannot be bound - an unproduced pool stays "
          "unbindable on this arm too");
    end_case();
}

static void mode_pi(const char *which)
{
    if (strcmp(which, "main") == 0) {
        pi_scenario();
        pi_cadence();
        pi_refusal();
    } else if (strcmp(which, "coords") == 0) {
        pi_coords_domain();
    } else if (strncmp(which, "sel", 3) == 0) {
        pi_selector(which);
    } else {
        pi_unsupported(which);
    }
}

/* ---- the environment modes -------------------------------------------- */

/* THE SWITCH IS PARSED ONCE PER PROCESS (sl_bgtf_opt_in caches in a static, and
 * sl_bgtf_invalidate deliberately does not re-read it), so one run can observe
 * one value. bgtftest.ps1 runs this mode once per setting. */
static void mode_env(int expect)
{
    int i, mismatch = 0;
    struct sl_bggpu_walk w = a_walk();

    sl_bgtfgl_reset();
    begin_case(expect ? "selection: SL_BG_TF_CLIP=1 selects the candidate"
                      : "selection: anything but \"1\" leaves the candidate OFF and "
                        "the state UNTRIED");
    CHECK(sl_bgtf_opt_in() == expect, "the switch answers %d", expect);
    for (i = 0; i < 64; i++) if (sl_bgtf_opt_in() != expect) mismatch++;
    CHECK(mismatch == 0,
          "64 further calls answer the same, so the switch is parsed once (%d differed)",
          mismatch);
    CHECK(tf.proc_calls == 0 && tf.calls == 0,
          "and reading it costs no resolve and no GL call (%ld / %ld)",
          tf.proc_calls, tf.calls);

    if (expect) {
        GLuint vbo;
        CHECK(sl_bgtf_ready() == 1, "the first walk resolves and compiles");
        CHECK(sl_bgtf_state() == 1, "and the state is READY (%d)", sl_bgtf_state());
        vbo = sl_bgtfgl_make_buffer((long) NV * (long) STR);
        CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "and a walk is admitted");
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) >= 0, "and served");
    } else {
        /* v0.4 "NOT 1" IS NO LONGER "OFF", AND THIS BRANCH HAD TO SPLIT. An
         * EXPLICIT "0" is a developer taking the prepass out of the launch and
         * is still off here; an ABSENT, empty or unrecognised value asks for
         * nothing and leaves the choice to the OPTIMIZED RENDERING row, which
         * this module deliberately cannot see - so readiness answers for the
         * PRODUCTION selection and "off" is the caller's not to ask. Asserting
         * not-ready for all four would have been asserting the defect that made
         * the production route unreachable.
         *
         * The off half below is unchanged. The row half checks the opposite -
         * that readiness is reached - and leaves "an off launch costs nothing"
         * to mode prod row_off, where the caller's contract is what is being
         * driven. */
        const char *cv = getenv("SL_BG_TF_CLIP");
        const int named_off = cv != NULL && strcmp(cv, "0") == 0;
        if (named_off) {
            /* AN OFF SELECTION IS NOT A CAPABILITY FAILURE. It must leave the
             * state UNTRIED, so a launch that is off costs nothing without
             * making OFF permanent for the context - and compile nothing. */
            CHECK(sl_bgtf_ready() == 0,
                  "an explicit SL_BG_TF_CLIP=0 is not ready");
            CHECK(sl_bgtf_state() == 0,
                  "and leaves the state UNTRIED (0), NOT UNAVAILABLE (-1): state=%d",
                  sl_bgtf_state());
            CHECK(strstr(sl_bgtf_why(), "SL_BG_TF_CLIP is 0") != NULL,
                  "the reason names the developer off: '%s'", sl_bgtf_why());
        } else {
            CHECK(sl_bgtf_ready() == 1,
                  "an ABSENT or unrecognised value leaves the row to decide, so "
                  "the PRODUCTION selection is ready - this is the gate whose "
                  "old sl_bgtf_opt_in test made the row's walks unreachable");
            CHECK(sl_bgtf_state() == 1,
                  "and the state is READY (%d)", sl_bgtf_state());
            CHECK(sl_bgtf_walk_admit() == 1 && sl_bgtf_production() == 1,
                  "the walk is admitted by the production route (%d/%d)",
                  sl_bgtf_walk_admit(), sl_bgtf_production());
        }
        if (named_off) {
            CHECK(tf.proc_calls == 0, "no entry point was resolved (%ld)",
                  tf.proc_calls);
            CHECK(tf.calls == 0, "no GL call was made (%ld)", tf.calls);
            CHECK(tf.create_shader == 0 && tf.shader_source == 0
                  && tf.compile == 0 && tf.create_program == 0 && tf.link == 0,
                  "and NOTHING WAS COMPILED: no shader created, sourced or "
                  "compiled, no program created or linked");
            /* A whole launch's worth of walks considered while off. */
            for (i = 0; i < 64; i++) (void) sl_bgtf_ready();
            CHECK(tf.proc_calls == 0 && tf.calls == 0 && sl_bgtf_state() == 0,
                  "64 walks considered while off cost no resolve, no call and "
                  "no latch");
            CHECK(sl_bgtf_begin_walk(&w, NV, 1u, STR, 1u) == 0,
                  "no walk is admitted");
            CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
                  "and no draw is served");
            sl_bgtf_bind_clip(0);
            sl_bgtf_unbind();
            CHECK(tf.calls == 0, "and the binding calls are no-ops too (%ld)",
                  tf.calls);
        } else {
            /* THE ROW'S HALF: readiness WAS reached, so entry points were
             * resolved and the program compiled - the opposite of the off
             * launch's list, and the thing the defective gate prevented. The
             * walk is then admitted and a draw served, which is what makes this
             * case a positive proof of the production route on the fake GL and
             * not merely an assertion about a predicate. */
            GLuint pvbo;
            int refused;
            CHECK(tf.proc_calls > 0 && tf.create_program > 0 && tf.link > 0,
                  "entry points were resolved and the program was compiled and "
                  "linked (%ld/%ld/%ld)", tf.proc_calls, tf.create_program,
                  tf.link);

            /* THE PRODUCTION ROUTE SERVES FROM THE AUXILIARY CONTEXT, which is
             * what this half had to be told. Readiness above is about the
             * twenty-four-name table on the MAIN context and answers 1 without
             * the auxiliary set, so the walk is admitted and begins - and then
             * the served draw needs glIsProgram, the real set and the fence set
             * that only the auxiliary arm uses. This fixture's old main-context
             * setup provided none of them, so the draw was refused; measured in
             * r746 as the one failure that survived clearing the runner's arm
             * token. The capability set is a property of the CONTEXT, so the
             * answer is to supply it here, not to accept a refusal as the
             * production route's normal outcome.
             *
             * SO THE REFUSAL IS KEPT AS ITS OWN CASE FIRST, rather than deleted
             * along with the defect it exposed. A missing capability must still
             * refuse the served draw, must create no auxiliary context, and in
             * particular must NOT fall back to the main-context prepass - the
             * arm measured one blue value apart from the original renderer. A
             * fixture that only proved the happy path would have nothing to say
             * about any of that. */
            pvbo = sl_bgtfgl_make_buffer((long) NV * (long) STR);
            CHECK(sl_bgtf_begin_walk(&w, NV, pvbo, STR, 1u) == 1,
                  "the row's walk is admitted");
            refused = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
            CHECK(refused < 0,
                  "and with the auxiliary capability set ABSENT its served draw "
                  "is REFUSED rather than served from somewhere else (%d)",
                  refused);
            CHECK(tf.aux_create_calls == 0L && tf.draw_arrays == 0L,
                  "no auxiliary context was created and nothing was drawn - in "
                  "particular not on the main context (%ld/%ld)",
                  tf.aux_create_calls, tf.draw_arrays);
            sl_bgtf_unbind();

            /* AND NOW THE SAME ROW-ON WALK WITH THE CAPABILITIES SUPPLIED, on a
             * fresh context so the refusal above cannot be what is measured.
             * This is prod row_on's own recipe, which is the arm the row
             * actually reaches. */
            arm();
            sl_bgtfgl_new_context();
            tf.provide_is_program = 1;
            tf.provide_real_set = 1;
            tf.provide_fence_set = 1;
            CHECK(sl_bgtf_walk_admit() == 1 && sl_bgtf_production() == 1,
                  "the walk is still admitted by the production route (%d/%d)",
                  sl_bgtf_walk_admit(), sl_bgtf_production());
            CHECK(sl_bgtf_ready() == 1,
                  "and readiness accepts it again on the new context");
            pvbo = input_vbo(NV, STR);
            CHECK(sl_bgtf_begin_walk(&w, NV, pvbo, STR, 1u) == 1,
                  "the row's walk begins");
            CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) >= 0,
                  "and its served draw is ACCEPTED, so an ordinary row-ON walk "
                  "reaches the prepass end to end");
            CHECK(tf.aux_create_calls == 1L && tf.draw_arrays > 0L,
                  "on exactly one auxiliary context, having really drawn "
                  "(%ld/%ld)", tf.aux_create_calls, tf.draw_arrays);
            sl_bgtf_unbind();
        }
    }
    end_case();
}

/* AN EMPTY VALUE, which the module treats as absent (`e == NULL || e[0] ==
 * '\0'`). It cannot be set from the shell on this platform: the Win32 and CRT
 * interfaces both DELETE a variable set to an empty string, so a launcher
 * cannot hand this process one. What this mode does is set it in-process and
 * then say which of the two branches it actually reached, so the coverage claim
 * matches what happened rather than what was intended. */
static void mode_env_empty(void)
{
    static char kv[] = "SL_BG_TF_CLIP=";
    const char *e;

    printf("envempty mode: an empty value, set in-process before the switch is read\n");
    putenv(kv);
    e = getenv("SL_BG_TF_CLIP");
    if (e == NULL)
        printf("    REACHED THE ABSENT BRANCH: this runtime deletes a variable set to "
               "an empty string, so getenv answers NULL. An empty VALUE is not "
               "representable here; the source treats both with one condition "
               "(e == NULL || e[0] == '\\0') and the absent half is what is covered.\n");
    else if (e[0] == '\0')
        printf("    REACHED THE EMPTY-VALUE BRANCH: getenv answers a zero-length "
               "string, so e[0] == '\\0' is the condition that ran.\n");
    else
        printf("    THE PUTENV DID NOT TAKE: getenv answers \"%s\". The case below is "
               "then about that value and NOT about an empty one.\n", e);
    mode_env(0);
}

/* THE STATE-ONLY SWITCH'S OWN GRAMMAR AND COUPLING, one setting per launch for
 * the same reason.
 *
 * `expect` is what the control must ANSWER - which is not the same as what the
 * variable says, because the answer is coupled: SL_BG_TF_STATE_ONLY=1 without
 * SL_BG_TF_CLIP=1 or without SL_BG_TF_RASTER_CPU=1 answers 0. That is the
 * property keeping the switch from becoming a second way into the candidate or a
 * way to expose a pool with nothing in it.
 *
 * AND A MISCOMBINED LAUNCH IS STILL THE ARM IT ASKED FOR. When the candidate is
 * selected but the control is not effective, this drives a prepass and requires
 * it to be the ORDINARY one - one draw, a written pool, a successful bind,
 * positive prepass and vertex counts - so "it changes nothing" is a measurement
 * of the admitted path rather than an absence of complaint. */
static void mode_env_state_only(int expect)
{
    int i, mismatch = 0;
    long calls_before;
    struct sl_bggpu_walk w = a_walk();

    sl_bgtfgl_reset();
    begin_case(expect
               ? "state-only selection: SL_BG_TF_STATE_ONLY=1 with both required "
                 "switches selects the control"
               : "state-only selection: anything but \"1\", or \"1\" without both "
                 "required switches, leaves the control OFF and the launch unchanged");
    CHECK(sl_bgtf_state_only() == expect, "the control answers %d", expect);
    calls_before = tf.calls;
    for (i = 0; i < 64; i++) if (sl_bgtf_state_only() != expect) mismatch++;
    CHECK(mismatch == 0,
          "64 further calls answer the same, so it is parsed once (%d differed)",
          mismatch);
    CHECK(tf.calls == calls_before && tf.proc_calls == 0,
          "and reading it costs no resolve and no GL call (%ld -> %ld / %ld)",
          calls_before, tf.calls, tf.proc_calls);
    CHECK(sl_bgtf_stats()->so_setups == 0 && sl_bgtf_stats()->so_omitted == 0
          && sl_bgtf_stats()->so_reuses == 0,
          "and reading it is not work: nothing is counted for it yet (%lu/%lu/%lu)",
          sl_bgtf_stats()->so_setups, sl_bgtf_stats()->so_omitted,
          sl_bgtf_stats()->so_reuses);

    if (sl_bgtf_opt_in() != 1) {
        /* THE CANDIDATE IS OFF, so the control must not have opened a door. The
         * state stays UNTRIED, nothing compiles, no walk is admitted and no draw
         * is served - exactly the off launch's list, unchanged by this
         * variable. */
        CHECK(sl_bgtf_ready() == 0, "with the candidate off, nothing is ready");
        CHECK(sl_bgtf_state() == 0, "the state stays UNTRIED (%d)", sl_bgtf_state());
        CHECK(tf.proc_calls == 0 && tf.calls == 0,
              "no entry point was resolved and no GL call made (%ld / %ld)",
              tf.proc_calls, tf.calls);
        CHECK(tf.create_shader == 0 && tf.create_program == 0 && tf.link == 0,
              "and nothing was compiled or linked");
        CHECK(sl_bgtf_begin_walk(&w, NV, 1u, STR, 1u) == 0, "no walk is admitted");
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "and no draw is served");
        CHECK(sl_bgtf_bind_clip(0) == -1, "and nothing can be bound");
        CHECK(sl_bgtf_stats()->so_setups == 0 && sl_bgtf_stats()->so_reuses == 0,
              "and the control still counted nothing (%lu/%lu)",
              sl_bgtf_stats()->so_setups, sl_bgtf_stats()->so_reuses);
    } else {
        /* THE CANDIDATE IS ON. Whichever way the control answered, the admitted
         * draw must be that arm's own: with the control off it is the ordinary
         * prepass, draw and bind included. */
        struct sl_bgtf_counters b, a;
        GLuint vbo;
        int s;
        CHECK(sl_bgtf_ready() == 1, "the candidate is ready");
        vbo = input_vbo(NV, STR);
        CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "a walk is admitted");
        b = snap();
        s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
        a = snap();
        CHECK(s >= 0, "and served (%d)", s);
        if (expect) {
            CHECK(tf.draw_arrays == 0 && a.so_setups - b.so_setups == 1,
                  "the control is effective: no draw, one setup (%ld / %lu)",
                  tf.draw_arrays, a.so_setups - b.so_setups);
            CHECK(sl_bgtf_bind_clip(s) == -1, "and the slot cannot be bound");
        } else {
            CHECK(tf.draw_arrays == 1 && tf.draw_vertices == (long) NV,
                  "the ORDINARY prepass ran: one draw over nverts (%ld / %ld)",
                  tf.draw_arrays, tf.draw_vertices);
            CHECK(a.prepasses - b.prepasses == 1
                  && a.vertices - b.vertices == (unsigned long) NV,
                  "counted as real work (%lu / %lu)", a.prepasses - b.prepasses,
                  a.vertices - b.vertices);
            CHECK(a.so_setups == b.so_setups && a.so_omitted == b.so_omitted,
                  "with nothing counted for the control (%lu/%lu)",
                  a.so_setups - b.so_setups, a.so_omitted - b.so_omitted);
            CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_SINGLE,
                  "and the written pool still binds");
        }
        sl_bgtf_unbind();
    }
    end_case();
}

/* THE POSITION-ONLY-INPUT SWITCH'S GRAMMAR, COUPLING AND EXCLUSIVITY, one
 * setting per launch.
 *
 * `expect` is what the control must ANSWER, which is not what the variable says:
 * the answer is coupled to SL_BG_TF_CLIP and SL_BG_TF_RASTER_CPU and is refused
 * outright when SL_BG_TF_STATE_ONLY asks for the other experiment at the same
 * time. `so_expect` is what the state-only control must answer in the same
 * launch, which is how the exclusivity is measured: on an ambiguous launch BOTH
 * are 0, and neither silently wins.
 *
 * AND A LAUNCH THAT IS NOT EFFECTIVE IS STILL THE ARM IT ASKED FOR. When the
 * candidate is selected but this control is not in force, the admitted draw must
 * be the ORDINARY one - a real draw, inherited arrays and all - so "it changes
 * nothing" is a measurement of the admitted path rather than an absence of
 * complaint. */
static void mode_env_pos_input(int expect, int so_expect)
{
    int i, mismatch = 0;
    long calls_before;
    struct sl_bggpu_walk w = a_walk();

    sl_bgtfgl_reset();
    begin_case(expect
               ? "position-only selection: SL_BG_TF_POS_INPUT=1 with both required "
                 "switches and no ambiguity selects the control"
               : "position-only selection: anything but \"1\", \"1\" without both "
                 "required switches, or \"1\" alongside SL_BG_TF_STATE_ONLY leaves "
                 "the control OFF and the launch unchanged");
    CHECK(sl_bgtf_pos_input() == expect, "the control answers %d", expect);
    CHECK(sl_bgtf_state_only() == so_expect,
          "and the state-only control answers %d in the same launch - the two "
          "are decided together and cannot disagree", so_expect);
    CHECK(!(sl_bgtf_pos_input() && sl_bgtf_state_only()),
          "the two are never BOTH in force (%d / %d)", sl_bgtf_pos_input(),
          sl_bgtf_state_only());
    calls_before = tf.calls;
    for (i = 0; i < 64; i++) if (sl_bgtf_pos_input() != expect) mismatch++;
    CHECK(mismatch == 0,
          "64 further calls answer the same, so it is parsed once (%d differed)",
          mismatch);
    CHECK(tf.calls == calls_before && tf.proc_calls == 0,
          "and reading it costs no resolve and no GL call (%ld -> %ld / %ld)",
          calls_before, tf.calls, tf.proc_calls);
    CHECK(sl_bgtf_stats()->pi_draws == 0 && sl_bgtf_stats()->pi_off == 0
          && sl_bgtf_stats()->pi_back == 0 && sl_bgtf_stats()->pi_refusals == 0,
          "and reading it is not work: nothing is counted for it yet "
          "(%lu/%lu/%lu/%lu)", sl_bgtf_stats()->pi_draws,
          sl_bgtf_stats()->pi_off, sl_bgtf_stats()->pi_back,
          sl_bgtf_stats()->pi_refusals);

    if (sl_bgtf_opt_in() != 1) {
        /* THE CANDIDATE IS OFF, so this control cannot have opened a door. */
        CHECK(sl_bgtf_ready() == 0, "with the candidate off, nothing is ready");
        CHECK(sl_bgtf_state() == 0, "the state stays UNTRIED (%d)", sl_bgtf_state());
        CHECK(tf.proc_calls == 0 && tf.calls == 0,
              "no entry point was resolved and no GL call made (%ld / %ld)",
              tf.proc_calls, tf.calls);
        CHECK(sl_bgtf_begin_walk(&w, NV, 1u, STR, 1u) == 0, "no walk is admitted");
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "and no draw is served");
    } else {
        struct sl_bgtf_counters b, a;
        struct pi_snap before, after;
        GLuint vbo;
        int s;
        CHECK(sl_bgtf_ready() == 1, "the candidate is ready");
        tf.provide_client_active_texture = 1;
        vbo = input_vbo(NV, STR);
        CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "a walk is admitted");
        pi_install_inherited();
        before = pi_now();
        b = snap();
        s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
        a = snap();
        after = pi_now();
        CHECK(s >= 0, "and served (%d)", s);
        CHECK(tf.draw_arrays == 1,
              "the real prepass draw ran either way (%ld)", tf.draw_arrays);
        if (expect) {
            CHECK(a.pi_draws - b.pi_draws == 1,
                  "the control is effective: the draw was isolated (%lu)",
                  a.pi_draws - b.pi_draws);
            CHECK(tf.cli_at_draw_seen && !tf.cli_at_draw_c,
                  "and the colour array was off at it (%d)",
                  (int) tf.cli_at_draw_c);
        } else {
            CHECK(a.pi_draws == b.pi_draws && a.pi_refusals == b.pi_refusals,
                  "the control is NOT effective: nothing was isolated and "
                  "nothing refused (%lu/%lu)", a.pi_draws - b.pi_draws,
                  a.pi_refusals - b.pi_refusals);
            CHECK(tf.client_state == 0,
                  "not one client array was written (%ld)", tf.client_state);
            CHECK(tf.cli_at_draw_seen && tf.cli_at_draw_c && tf.cli_at_draw_t[0],
                  "and the ORDINARY draw dereferenced the inherited arrays "
                  "(colour %d, unit 0 %d)", (int) tf.cli_at_draw_c,
                  (int) tf.cli_at_draw_t[0]);
        }
        CHECK(pi_differs(&before, &after) == NULL,
              "and the client state is exact after the draw either way");
        sl_bgtf_unbind();
    }
    end_case();
}

/* DEFAULT OFF, SAID AS A MEASUREMENT. Runs at the end of the main suite, where
 * both SL_BG_TF_POS_INPUT and SL_BG_TF_STATE_ONLY are absent: neither control
 * answered 1, nothing in the process was counted for either, and the ordinary
 * arm did the real work whose numbers they report as zero. */
static void st_pos_input_off(void)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("position-only default off: the control is not selected, nothing in this "
               "process was counted for it, and no client array was ever written");
    CHECK(sl_bgtf_pos_input() == 0,
          "SL_BG_TF_POS_INPUT is absent, so the control is off (%d)",
          sl_bgtf_pos_input());
    calls_before = tf.calls;
    for (i = 0; i < 64; i++) (void) sl_bgtf_pos_input();
    CHECK(tf.calls == calls_before,
          "and reading it costs no GL call (%ld -> %ld)", calls_before, tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->pi_draws == 0 && t->pi_off == 0 && t->pi_back == 0
          && t->pi_units == 0 && t->pi_refusals == 0,
          "nothing was counted for it in the whole process (%lu/%lu/%lu/%lu/%lu)",
          t->pi_draws, t->pi_off, t->pi_back, t->pi_units, t->pi_refusals);
    /* AND THE OFF PATH TOUCHED NO CLIENT STATE AT ALL, which is the strongest
     * form of "byte-unchanged" this fixture can state: with the control off the
     * module names none of the four client-state entry points and resolves no
     * selector. The counts are cumulative for the process. */
    CHECK(tf.client_state == 0 && tf.client_active_texture == 0,
          "no client array was enabled or disabled and no unit selected "
          "(%ld/%ld)", tf.client_state, tf.client_active_texture);
    CHECK(t->prepasses >= 1 && t->binds >= 1,
          "while the ordinary arm ran prepasses and bound pools (%lu/%lu)",
          t->prepasses, t->binds);
    end_case();
}

/* DEFAULT OFF, SAID AS A MEASUREMENT. Runs at the end of the main suite, where
 * SL_BG_TF_STATE_ONLY is absent: the control answered 0, nothing in the whole
 * process was counted for it, and the ordinary arm did the real work whose
 * numbers the control's arm reports as zero. Without the second half, "off is
 * unchanged" would be consistent with the candidate having stopped working. */
/* THE ZERO-VERTEX-COUNT SWITCH'S GRAMMAR, COUPLING AND EXCLUSIVITY, one setting
 * per launch.
 *
 * `expect` is what THIS control must answer, which is not what the variable
 * says: the answer is coupled to SL_BG_TF_CLIP and SL_BG_TF_RASTER_CPU and is
 * refused outright when any of the three other arms asks for its own experiment
 * in the same launch. `other_so`, `other_pi` and `other_pv` are what those three
 * must answer in the SAME launch, which is how the exclusivity is measured:
 * on an ambiguous launch BOTH of the pair are 0 and neither silently wins.
 *
 * AND A REFUSED REQUEST IS COUNTED, ONCE. A launch that asked for the control
 * and did not get it raises zc_refusals exactly once, which is what makes "it
 * refused rather than half-applied" a number rather than the absence of a
 * complaint. `want_refusal` says whether this launch is such a launch. */
static void mode_env_zero_count(int expect, int want_refusal, int other_so,
                                int other_pi, int other_pv)
{
    int i, mismatch = 0;
    long calls_before;
    struct sl_bggpu_walk w = a_walk();

    sl_bgtfgl_reset();
    begin_case(expect
               ? "zero-count selection: SL_BG_TF_ZERO_COUNT=1 with both required "
                 "switches and no ambiguity selects the control"
               : "zero-count selection: anything but \"1\", \"1\" without both "
                 "required switches, or \"1\" alongside another exclusive arm "
                 "leaves the control OFF and the launch unchanged");
    CHECK(sl_bgtf_zero_count() == expect, "the control answers %d", expect);
    CHECK(sl_bgtf_state_only() == other_so,
          "the state-only control answers %d in the same launch (%d)", other_so,
          sl_bgtf_state_only());
    CHECK(sl_bgtf_pos_input() == other_pi,
          "the position-only-input control answers %d (%d)", other_pi,
          sl_bgtf_pos_input());
    CHECK(sl_bgtf_private_vao() == other_pv,
          "and the private vertex-array control answers %d - all four are "
          "decided together and cannot disagree (%d)", other_pv,
          sl_bgtf_private_vao());
    CHECK(!(sl_bgtf_zero_count()
            && (sl_bgtf_state_only() || sl_bgtf_pos_input()
                || sl_bgtf_private_vao())),
          "this control is never in force beside any of the other three "
          "(%d / %d / %d / %d)", sl_bgtf_zero_count(), sl_bgtf_state_only(),
          sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_stats()->zc_refusals == (unsigned long) (want_refusal ? 1 : 0),
          "the refusal count for this launch is %d (%lu) - a launch that asked "
          "and did not get the control refuses ONCE, and a launch that never "
          "asked refuses nothing", want_refusal ? 1 : 0,
          sl_bgtf_stats()->zc_refusals);
    calls_before = tf.calls;
    for (i = 0; i < 64; i++) if (sl_bgtf_zero_count() != expect) mismatch++;
    CHECK(mismatch == 0,
          "64 further calls answer the same, so it is parsed once (%d differed)",
          mismatch);
    CHECK(sl_bgtf_stats()->zc_refusals == (unsigned long) (want_refusal ? 1 : 0),
          "and 64 reads did not raise the refusal again (%lu)",
          sl_bgtf_stats()->zc_refusals);
    CHECK(tf.calls == calls_before && tf.proc_calls == 0,
          "reading it costs no resolve and no GL call (%ld -> %ld / %ld)",
          calls_before, tf.calls, tf.proc_calls);
    CHECK(sl_bgtf_stats()->zc_setups == 0 && sl_bgtf_stats()->zc_calls == 0
          && sl_bgtf_stats()->zc_reuses == 0,
          "and reading it is not work: no setup, call or reuse is counted yet "
          "(%lu/%lu/%lu)", sl_bgtf_stats()->zc_setups,
          sl_bgtf_stats()->zc_calls, sl_bgtf_stats()->zc_reuses);

    if (sl_bgtf_opt_in() != 1) {
        /* THE CANDIDATE IS OFF, so this control cannot have opened a door. */
        CHECK(sl_bgtf_ready() == 0, "with the candidate off, nothing is ready");
        CHECK(sl_bgtf_state() == 0, "the state stays UNTRIED (%d)",
              sl_bgtf_state());
        CHECK(tf.proc_calls == 0 && tf.calls == 0,
              "no entry point was resolved and no GL call made (%ld / %ld)",
              tf.proc_calls, tf.calls);
        CHECK(tf.create_shader == 0 && tf.create_program == 0 && tf.link == 0,
              "and nothing was compiled or linked");
        CHECK(sl_bgtf_begin_walk(&w, NV, 1u, STR, 1u) == 0, "no walk is admitted");
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f) == -1,
              "and no draw is served");
        CHECK(tf.draw_arrays == 0, "so no call was issued (%ld)",
              tf.draw_arrays);
        CHECK(sl_bgtf_bind_clip(0) == -1, "and nothing can be bound");
        CHECK(sl_bgtf_stats()->zc_setups == 0 && sl_bgtf_stats()->zc_calls == 0,
              "and the control still counted nothing (%lu/%lu)",
              sl_bgtf_stats()->zc_setups, sl_bgtf_stats()->zc_calls);
    } else {
        /* THE CANDIDATE IS ON. Whichever way this control answered, the admitted
         * draw must be that arm's own: with the control not in force it is the
         * ordinary prepass, a real count and a bindable pool included. */
        struct sl_bgtf_counters b, a;
        GLuint vbo;
        int s;
        CHECK(sl_bgtf_ready() == 1, "the candidate is ready");
        vbo = input_vbo(NV, STR);
        CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "a walk is admitted");
        b = snap();
        s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
        a = snap();
        CHECK(s >= 0, "and served (%d)", s);
        if (expect) {
            CHECK(tf.draw_arrays == 1 && tf.draw_vertices == 0L,
                  "the control is effective: the call was ISSUED and asked for "
                  "ZERO vertices (%ld / %ld)", tf.draw_arrays,
                  tf.draw_vertices);
            CHECK(a.zc_setups - b.zc_setups == 1
                  && a.zc_calls - b.zc_calls == 1,
                  "one setup and one empty call (%lu / %lu)",
                  a.zc_setups - b.zc_setups, a.zc_calls - b.zc_calls);
            CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices,
                  "and no prepass or vertex was claimed (%lu / %lu)",
                  a.prepasses - b.prepasses, a.vertices - b.vertices);
            CHECK(sl_bgtf_bind_clip(s) == -1, "and the slot cannot be bound");
        } else if (sl_bgtf_state_only() == 1) {
            /* The OTHER arm won this launch outright, which only happens when
             * this control was never asked for. */
            CHECK(tf.draw_arrays == 0,
                  "the state-only arm is the effective one and omitted the call "
                  "(%ld)", tf.draw_arrays);
        } else {
            CHECK(tf.draw_arrays == 1 && tf.draw_vertices == (long) NV,
                  "the ORDINARY prepass ran: one call over nverts (%ld / %ld)",
                  tf.draw_arrays, tf.draw_vertices);
            CHECK(a.prepasses - b.prepasses == 1
                  && a.vertices - b.vertices == (unsigned long) NV,
                  "counted as real work (%lu / %lu)", a.prepasses - b.prepasses,
                  a.vertices - b.vertices);
            CHECK(a.zc_setups == b.zc_setups && a.zc_calls == b.zc_calls,
                  "with nothing counted for this control (%lu/%lu)",
                  a.zc_setups - b.zc_setups, a.zc_calls - b.zc_calls);
            CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_SINGLE,
                  "and the written pool still binds");
        }
        sl_bgtf_unbind();
    }
    /* AND A REFUSED LAUNCH DID NO WORK AT ALL, said as an ABSOLUTE over the
     * whole process rather than as a delta.
     *
     * WHY ABSOLUTE AND WHY HERE. A refused launch must not merely stop adding to
     * this control's counters - it must never have added to them, because the
     * switch is parsed once and a launch that was refused has no later chance to
     * become effective. A delta would be satisfied by a run that issued empty
     * calls before the window it was measured over. This is the check that a
     * triple request - which is what this case list was extended to cover -
     * really left the control inert rather than merely uncounted. */
    if (want_refusal) {
        const struct sl_bgtf_counters *t = sl_bgtf_stats();
        CHECK(t->zc_setups == 0 && t->zc_calls == 0 && t->zc_reuses == 0,
              "the control was REFUSED, so nothing in this whole process was "
              "set up, issued or reused for it (%lu/%lu/%lu)",
              t->zc_setups, t->zc_calls, t->zc_reuses);
        CHECK(t->zc_refusals == 1,
              "and the refusal was counted exactly ONCE for the launch, however "
              "many of its clauses spoke (%lu)", t->zc_refusals);
    }
    end_case();
}

/* ZERO-COUNT DEFAULT OFF, SAID AS A MEASUREMENT. */
static void st_zero_count_off(void)
{
    const struct sl_bgtf_counters *t;
    long calls_before, draws_before;
    int i;

    begin_case("zero-count default off: the control is not selected, nothing in this "
               "process was counted for it, and the ordinary arm issued real draws");
    CHECK(sl_bgtf_zero_count() == 0,
          "SL_BG_TF_ZERO_COUNT is absent, so the control is off (%d)",
          sl_bgtf_zero_count());
    calls_before = tf.calls;
    draws_before = tf.draw_arrays;
    for (i = 0; i < 64; i++) (void) sl_bgtf_zero_count();
    CHECK(tf.calls == calls_before && tf.draw_arrays == draws_before,
          "and reading it costs no GL call and issues no draw (%ld -> %ld, "
          "%ld -> %ld)", calls_before, tf.calls, draws_before, tf.draw_arrays);
    t = sl_bgtf_stats();
    CHECK(t->zc_setups == 0 && t->zc_calls == 0 && t->zc_reuses == 0
          && t->zc_refusals == 0,
          "nothing was counted for it in the whole process, refusals included "
          "(%lu/%lu/%lu/%lu)", t->zc_setups, t->zc_calls, t->zc_reuses,
          t->zc_refusals);
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "while the ordinary arm ran prepasses over vertices (%lu / %lu)",
          t->prepasses, t->vertices);
    CHECK(t->binds >= 1, "and bound written pools (%lu)", t->binds);
    /* AND IT TRANSFORMED VERTICES, which is read from the MODULE's own
     * process-lifetime counter and not from the stub's. The stub's per-call
     * numbers are reset by each case's arm(), so they say nothing here; the
     * module's counters are never reset, and an arm whose every call was empty
     * would carry 0 in this one. */
    CHECK(t->vertices >= (unsigned long) NV,
          "over at least one full pool's worth of vertices, which an arm of "
          "empty calls could not report (%lu)", t->vertices);
    end_case();
}

/* THE MODE'S DEFAULT-OFF CASE, in the main suite, which runs with the selector
 * absent. "Absent changes nothing" is the claim that makes Z_KNOWN comparable to
 * the known reference run, so it is asserted here as the list of things that did
 * not happen rather than left to follow from the branches. */
static void st_empty_state_off(void)
{
    const struct sl_bgtf_counters *t;
    long calls_before, draws_before;
    int i;

    begin_case("empty-draw state default off: no mode is selected, nothing in this "
               "process was counted for one, and the ordinary arm still drew");
    CHECK(sl_bgtf_empty_state() == SL_BGTF_ES_OFF,
          "SL_BG_TF_EMPTY_STATE is absent, so no mode is in force (%s)",
          sl_bgtf_es_name(sl_bgtf_empty_state()));
    calls_before = tf.calls;
    draws_before = tf.draw_arrays;
    for (i = 0; i < 64; i++) (void) sl_bgtf_empty_state();
    CHECK(tf.calls == calls_before && tf.draw_arrays == draws_before,
          "and reading it costs no GL call and issues no draw (%ld -> %ld, "
          "%ld -> %ld)", calls_before, tf.calls, draws_before, tf.draw_arrays);
    t = sl_bgtf_stats();
    CHECK(t->es_setups == 0 && t->es_draws == 0 && t->es_reuses == 0
          && t->es_tf_begins == 0 && t->es_tf_ends == 0
          && t->es_prog0_switches == 0 && t->es_prog0_restores == 0
          && t->es_prog_pre_zero == 0 && t->es_prog_pre_prog == 0
          && t->es_prog_post_zero == 0 && t->es_prog_post_prog == 0
          && t->es_draws_prog_tf == 0 && t->es_draws_prog0 == 0
          && t->es_discard_offs == 0 && t->es_discard_ons == 0
          && t->es_refusals == 0 && t->es_mode == 0,
          "nothing was counted for any mode in the whole process, refusals, the "
          "positional switch counts and the recorded mode included");
    /* THE OFF PATH IS THE ONE IT WOULD HAVE WITHOUT THIS SELECTOR, stated as the
     * GL facts an off launch still produces: the begin/end pair, the discard
     * bracket and the program switch are all the ordinary ones, so nothing was
     * omitted or added by a mode that is not selected. */
    CHECK(t->prepasses >= 1 && t->vertices >= (unsigned long) NV,
          "while the ordinary arm ran prepasses over full pools of vertices "
          "(%lu / %lu)", t->prepasses, t->vertices);
    CHECK(t->binds >= 1, "and bound written pools (%lu)", t->binds);
    CHECK(tf.v_program_change_in_tf == 0,
          "and no program was changed inside a feedback scope anywhere (%ld)",
          tf.v_program_change_in_tf);
    end_case();
}

static void st_state_only_off(void)
{
    const struct sl_bgtf_counters *t;
    long calls_before;
    int i;

    begin_case("state-only default off: the control is not selected, nothing in this "
               "process was counted for it, and the ordinary arm did real work");
    CHECK(sl_bgtf_state_only() == 0,
          "SL_BG_TF_STATE_ONLY is absent, so the control is off (%d)",
          sl_bgtf_state_only());
    calls_before = tf.calls;
    for (i = 0; i < 64; i++) (void) sl_bgtf_state_only();
    CHECK(tf.calls == calls_before,
          "and reading it costs no GL call (%ld -> %ld)", calls_before, tf.calls);
    t = sl_bgtf_stats();
    CHECK(t->so_setups == 0 && t->so_omitted == 0 && t->so_reuses == 0,
          "nothing was counted for it in the whole process (%lu/%lu/%lu)",
          t->so_setups, t->so_omitted, t->so_reuses);
    CHECK(t->prepasses >= 1 && t->vertices >= 1,
          "while the ordinary arm ran prepasses over vertices (%lu / %lu)",
          t->prepasses, t->vertices);
    CHECK(t->binds >= 1, "and bound written pools (%lu)", t->binds);
    end_case();
}

/* ---- the fixture ------------------------------------------------------- */

/* ======================================================================== *
 * THE AUXILIARY-CONTEXT REAL PREPASS (SL_BG_TF_AUX_REAL), default OFF.
 *
 * WHAT THESE CASES PROVE AND WHAT THEY CANNOT. They prove the module's own
 * logic: which entry points it resolves and ON WHICH CONTEXT, which calls it
 * issues there, in which ORDER, with which arguments and with the REAL NONZERO
 * COUNT, what state it sets explicitly on that context and puts back, which
 * objects it verifies rather than assumes, where its two synchronisation points
 * are, which slot it marks written, and what it refuses.
 *
 * THEY PROVE NOTHING ABOUT ANY DRIVER. Every GL entry point and both context
 * functions are C functions in sl_gfx_bgtf_testgl.c. There is no window, no real
 * GL context, no GPU, no shared object and no real transform feedback here, so
 * whether a real driver makes a second context's transform-feedback writes
 * visible to the first - which is the question the mode exists to ask - is
 * UNQUALIFIED by anything below. What is qualified is that the module asks it
 * correctly and fails closed when it cannot.
 * ======================================================================== */

#define AR_SERVED 5

/* The number of GL records the mode's own auxiliary bracket contains on the
 * healthy path, pinned here so a drift shows up as a count rather than only
 * inside a sequence comparison. */
#define AR_AUX_RECORDS 23

/* THE MODE'S WHOLE AUXILIARY BRACKET, AS AN ORDERED SEQUENCE WITH A CONTEXT PER
 * RECORD. This is the one place the sequence is written down, and both arms read
 * it: they differ in what the MAIN context rasterises afterwards and not in a
 * single call of this.
 *
 * The two object checks come FIRST, before any state is written there, which is
 * the ordering property the mode is written to; the array object is bound before
 * the pointer command that lands in it; the uniforms are written under the
 * program and before the draw, so none is ever written with no program current;
 * the feedback pair brackets exactly one draw; and the restoration is the
 * inverse order, innermost first. */
static int ar_expect(struct expect *e, int *want, GLuint prog, GLuint vbo,
                     GLuint pool, GLuint vao)
{
    int n = 0;
    n = ax_push(e, want, n, TFGL_ISBUFFER, (long) vbo, GL_TRUE, ANY, ANY,
                CTX_AUX, "the INPUT buffer is verified shared, there, first");
    n = ax_push(e, want, n, TFGL_ISBUFFER, (long) pool, GL_TRUE, ANY, ANY,
                CTX_AUX, "and so is the OUTPUT pool, before anything is bound");
    n = ax_push(e, want, n, TFGL_BINDVAO, (long) vao, ANY, ANY, ANY, CTX_AUX,
                "this mode's OWN array object, bound there - a container object "
                "is never shared");
    n = ax_push(e, want, n, TFGL_CLIENTSTATE, GL_VERTEX_ARRAY, 1, ANY, ANY,
                CTX_AUX, "the position array is ENABLED there: an array enable "
                "is per-context and the main context's says nothing about this "
                "one");
    n = ax_push(e, want, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY,
                CTX_AUX, "the SHARED prepass program, made current there");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_sc, from this module's own cache");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_post");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_org");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_C, all sixteen");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_fogless");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_dmode, this draw's own");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_fk1");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_fk2");
    n = ax_push(e, want, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                CTX_AUX, "discard ON there: it is per-context too");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY,
                ANY, CTX_AUX, "the shared input buffer, bound FRESHLY there - "
                "which is the bind-after-completion the visibility rule asks "
                "for");
    n = ax_push(e, want, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR,
                (long) vbo, CTX_AUX, "the prepass's own position pointer, at "
                "the item's stride");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                CTX_AUX, "and the global array binding left at 0");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY,
                ANY, CTX_AUX, "the element binding dropped there");
    n = ax_push(e, want, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER,
                0, (long) pool, ANY, CTX_AUX,
                "the shared pool bound to the indexed target, which writes THIS "
                "context's own default feedback object");
    n = ax_push(e, want, n, TFGL_BEGINTF, GL_POINTS, ANY, ANY, ANY, CTX_AUX,
                "the capture opens there");
    n = ax_push(e, want, n, TFGL_DRAWARRAYS, GL_POINTS, 0, (long) NV,
                (long) pool, CTX_AUX,
                "THE REAL PREPASS: one draw over every vertex, recording into "
                "the shared pool");
    n = ax_push(e, want, n, TFGL_ENDTF, ANY, ANY, ANY, ANY, CTX_AUX,
                "and closes, matched");
    n = ax_push(e, want, n, TFGL_FINISH, ANY, ANY, ANY, ANY, CTX_AUX,
                "the completion wait, on the context that WROTE");
    return n;
}

/* AND THE RESTORATION OF THAT CONTEXT'S OWN STATE, in the inverse order. Kept as
 * a second sequence rather than appended to the first because the first ends at
 * the synchronisation point, which is the instant the mode's claim about the
 * output is made - and a reader comparing the two halves should see where that
 * line is. */
static int ar_expect_restore(struct expect *e, int *want, GLuint vao)
{
    int n = 0;
    n = ax_push(e, want, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER,
                0, 0, ANY, CTX_AUX, "the indexed binding released there");
    n = ax_push(e, want, n, TFGL_DISABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                CTX_AUX, "discard off there");
    n = ax_push(e, want, n, TFGL_USEPROGRAM, 0, ANY, ANY, ANY, CTX_AUX,
                "no program there");
    n = ax_push(e, want, n, TFGL_CLIENTSTATE, GL_VERTEX_ARRAY, 0, ANY, ANY,
                CTX_AUX, "the position array disabled there");
    n = ax_push(e, want, n, TFGL_BINDVAO, 0, ANY, ANY, ANY, CTX_AUX,
                "and the array object unbound - the context is back at the "
                "defaults that are also its entry state");
    /* AND THE VERDICT READ, LAST ON THAT CONTEXT AND ON THAT CONTEXT. Its
     * PLACE is the claim: after the matched scope, after the completion wait and
     * after the restoration, so one sticky read covers every command this mode
     * issued there - and before the switch home, so it is the AUXILIARY
     * context's error state and not the main context's. `a` is the code it
     * answered, 0 on the healthy path; `b` is the shim's own record of which
     * context the read landed on, 1 being the auxiliary one. */
    n = ax_push(e, want, n, TFGL_GETERROR, 0, 1, ANY, ANY, CTX_AUX,
                "the error-state read: GL accepted every command of the real "
                "prepass, asked on the context that issued them");
    (void) vao;
    return n;
}

/* THE SAME COMPARISON AS seq_is, OVER A WINDOW IN THE MIDDLE OF THE TRANSCRIPT.
 *
 * WHY A SECOND FUNCTION. seq_is ends by requiring that the transcript holds
 * EXACTLY the records it was given and nothing after them, which is the right
 * question when the window being compared is the whole of what a call produced.
 * This mode's auxiliary bracket sits BETWEEN the main context's own common setup
 * and the main context's own restoration, both of which this mode leaves alone,
 * so requiring the log to end at it would be requiring something false. The
 * record-by-record comparison is seq_is's, unchanged; only the closing
 * assertion is dropped, and the sequence's own LENGTH is still pinned by the
 * caller comparing AR_AUX_RECORDS against what ar_expect built. */
static void seq_window_is(long from, const struct expect *e, int n)
{
    long i;
    for (i = 0; i < n; i++) {
        const struct sl_bgtfgl_call *r;
        if (from + i >= tf.nlog) {
            CHECK(0, "the transcript ended at record %ld, before '%s' (%s)",
                  i, e[i].what, kind_name(e[i].kind));
            return;
        }
        r = &tf.log[from + i];
        CHECK(r->kind == e[i].kind, "record %ld is %s, expected %s (%s)",
              i, kind_name(r->kind), kind_name(e[i].kind), e[i].what);
        if (r->kind != e[i].kind) continue;
        if (e[i].a != ANY) CHECK(r->a == e[i].a,
              "record %ld (%s, %s): a = %ld, expected %ld", i,
              kind_name(r->kind), e[i].what, r->a, e[i].a);
        if (e[i].b != ANY) CHECK(r->b == e[i].b,
              "record %ld (%s, %s): b = %ld, expected %ld", i,
              kind_name(r->kind), e[i].what, r->b, e[i].b);
        if (e[i].c != ANY) CHECK(r->c == e[i].c,
              "record %ld (%s, %s): c = %ld, expected %ld", i,
              kind_name(r->kind), e[i].what, r->c, e[i].c);
        if (e[i].d != ANY) CHECK(r->d == e[i].d,
              "record %ld (%s, %s): d = %ld, expected %ld", i,
              kind_name(r->kind), e[i].what, r->d, e[i].d);
    }
}

/* ONE ARM, END TO END. `gpu` selects which arm this process is running, and the
 * caller has already proved that the environment selected it. */
static void ar_scenario(int gpu)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct expect e[AR_AUX_RECORDS];
    int want[AR_AUX_RECORDS];
    GLuint vbo, pool = 0u, prog = 0u, vao;
    long mark, calls_at_hit, finish_before, drawv_before;
    int s, i, n;

    begin_case(gpu
        ? "auxiliary real prepass, GPU_SOURCE: the REAL nonzero prepass runs on "
          "a second object-sharing context with every context-local piece of "
          "state set explicitly there, the main context issues no prepass draw, "
          "the output's visibility is established in both directions, and the "
          "slot the shared pool belongs to is bindable"
        : "auxiliary real prepass, CPU_SOURCE: the same REAL nonzero prepass on "
          "the second context, with the main context rasterising the ORIGINAL "
          "position stream through the original-raster control, so the pool is "
          "written and never read");
    arm();
    /* THE CAPABILITY SURFACE THE MODE NEEDS, AND NOTHING MORE. Both gates
     * default to the refusing shape, so setting them here is what makes every
     * refusal case below a statement about the module. */
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    CHECK(sl_bgtf_aux_real() == (gpu ? SL_BGTF_AR_GPU : SL_BGTF_AR_CPU),
          "the mode is in force on the arm this process was launched for (%s)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_raster_cpu() == (gpu ? 0 : 1),
          "with the raster source the arm NAMES (%d)", sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_zero_count() == 0,
          "and the zero-count arm OFF, which is what makes the count real (%d)",
          sl_bgtf_zero_count());
    CHECK(sl_bgtf_aux_empty() == 0 && sl_bgtf_neutral_prog() == 0
          && sl_bgtf_empty_state() == SL_BGTF_ES_OFF
          && sl_bgtf_post_reset() == SL_BGTF_PR_OFF
          && sl_bgtf_state_only() == 0 && sl_bgtf_pos_input() == 0
          && sl_bgtf_private_vao() == 0,
          "and every control it is exclusive with ABSENT (%d/%d/%s/%s/%d/%d/%d)",
          sl_bgtf_aux_empty(), sl_bgtf_neutral_prog(),
          sl_bgtf_es_name(sl_bgtf_empty_state()),
          sl_bgtf_pr_name(sl_bgtf_post_reset()), sl_bgtf_state_only(),
          sl_bgtf_pos_input(), sl_bgtf_private_vao());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    prog = tf.last_program;
    /* NOTHING IS CREATED BEFORE THE FIRST SERVED DRAW. The capability is lazy,
     * so a module that is merely ready owns no second context and no array
     * object. */
    CHECK(tf.aux_exists == 0 && tf.aux_create_calls == 0L && tf.gen_vao == 0L,
          "no auxiliary context and no array object exist before the first "
          "served draw (%d/%ld/%ld)", tf.aux_exists, tf.aux_create_calls,
          tf.gen_vao);
    CHECK(tf.finish_calls == 0L,
          "and no synchronisation point has been added (%ld)", tf.finish_calls);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    b = snap();
    mark = tf.nlog;
    finish_before = tf.finish_calls;
    drawv_before = tf.draw_vertices;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s >= 0, "the served draw is ACCEPTED (%d)", s);
    a = snap();
    pool = tf.tf_varying_prog ? 0u : 0u;   /* not the varying's program */
    vao = tf.last_gen_vao;
    CHECK(vao != 0u, "an array object was generated for this mode (%u)",
          (unsigned) vao);
    CHECK(tf.gen_vao == 1L && tf.v_vao_on_main == 0L,
          "exactly one, and neither it nor any bind of it was reached on the "
          "MAIN context (%ld/%ld)", tf.gen_vao, tf.v_vao_on_main);

    /* THE POOL THIS BRACKET RECORDED INTO, taken from the transcript rather than
     * from the module's internals: the indexed binding names it. */
    {
        const struct sl_bgtfgl_call *r = first_of(mark, TFGL_BINDBUFFERBASE);
        CHECK(r != NULL, "the bracket bound a pool to the indexed target");
        if (r != NULL) pool = (GLuint) r->c;
    }

    /* THE WHOLE AUXILIARY SEQUENCE, IN ORDER AND WITH ITS PLACEMENT. The records
     * before it are the main context's own common setup, which this mode leaves
     * alone, so the comparison starts at the first glIsBuffer. */
    {
        long from = mark;
        while (from < tf.nlog && tf.log[from].kind != TFGL_ISBUFFER) from++;
        CHECK(from < tf.nlog, "the auxiliary bracket is in the transcript");
        /* THE BRACKET-START DRAIN COMES BEFORE THE FIRST OBJECT CHECK, which is
         * what makes the verdict read at the far end attributable to this
         * bracket: the error state is emptied before this mode issues anything
         * at all on that context, so a flag found at the end was raised by one
         * of this bracket's own commands. It is immediately before the first
         * glIsBuffer and on the auxiliary context - a read on the main context
         * here would empty the wrong state and consume an error belonging to the
         * ordinary rendering path. */
        if (from > mark) {
            const struct sl_bgtfgl_call *d = &tf.log[from - 1];
            CHECK(d->kind == TFGL_GETERROR && d->ctx == CTX_AUX && d->b == 1,
                  "the record immediately before the first object check is the "
                  "bracket-start error drain, on the AUXILIARY context (%s, ctx "
                  "%d, on_aux %ld)", kind_name(d->kind), d->ctx, d->b);
            CHECK(d->a == 0,
                  "and it found the state already empty, so nothing stale is "
                  "attributed to this bracket (0x%lX)", d->a);
        } else {
            CHECK(0, "there is a record before the first object check");
        }
        n = ar_expect(e, want, prog, vbo, pool, vao);
        CHECK(n == AR_AUX_RECORDS,
              "the bracket this fixture requires is %d records long (%d)",
              AR_AUX_RECORDS, n);
        seq_window_is(from, e, n);
        seq_ctx_is(from, e, want, n);
        /* AND THE RESTORATION OF THAT CONTEXT, immediately after it. */
        {
            struct expect r2[8];
            int w2[8];
            const int n2 = ar_expect_restore(r2, w2, vao);
            seq_window_is(from + n, r2, n2);
            seq_ctx_is(from + n, r2, w2, n2);
        }
    }

    /* THE REAL NONZERO COUNT, AS A NUMBER AND NOT ONLY AS A SEQUENCE ENTRY. */
    CHECK(tf.draws_on_aux == 1L && tf.draw_vertices_on_aux == (long) NV,
          "ONE draw on the auxiliary context, over every vertex of the mesh "
          "(%ld draws, %ld vertices)", tf.draws_on_aux,
          tf.draw_vertices_on_aux);
    CHECK(tf.draw_vertices - drawv_before == (long) NV,
          "and the whole run's vertex total rose by exactly that (%ld)",
          tf.draw_vertices - drawv_before);
    CHECK(a.ar_vertices - b.ar_vertices == (unsigned long) NV,
          "which the mode's own count agrees with - a run with prepasses and no "
          "vertices would be the EMPTY-draw control under this name (%lu)",
          a.ar_vertices - b.ar_vertices);
    CHECK(a.ar_prepasses - b.ar_prepasses == 1
          && a.ar_prepasses_aux == a.ar_prepasses,
          "one real prepass, and every one of them with the auxiliary context "
          "the last one this process made current (%lu vs %lu)",
          a.ar_prepasses, a.ar_prepasses_aux);
    CHECK(tf.draw_arrays == 1L,
          "and the MAIN context issued NO prepass draw at all - a draw there "
          "would be the ordinary candidate path under this mode's name (%ld "
          "draws in total, all of them on the other context)", tf.draw_arrays);
    CHECK(a.ar_main_omits - b.ar_main_omits == 1,
          "the omission is a POSITIVE record, not the absence of one (%lu)",
          a.ar_main_omits - b.ar_main_omits);

    /* THE TWO SYNCHRONISATION POINTS, AND WHERE EACH ONE IS. */
    CHECK(tf.finish_calls - finish_before == 2L,
          "exactly TWO synchronisation points were added for this bracket (%ld)",
          tf.finish_calls - finish_before);
    CHECK(a.ar_syncs - b.ar_syncs == 2,
          "which the mode's own count agrees with (%lu)",
          a.ar_syncs - b.ar_syncs);
    {
        const struct sl_bgtfgl_call *f = first_of(mark, TFGL_FINISH);
        CHECK(f != NULL && f->ctx == CTX_MAIN,
              "the FIRST is on the MAIN context, before the switch: the input "
              "buffer's writes have to have COMPLETED before the second context "
              "binds it (%d)", f != NULL ? f->ctx : -99);
    }
    {
        long i2, seen = 0;
        int last_ctx = -99;
        for (i2 = mark; i2 < tf.nlog; i2++)
            if (tf.log[i2].kind == TFGL_FINISH) { seen++; last_ctx = tf.log[i2].ctx; }
        CHECK(seen == 2 && last_ctx == CTX_AUX,
              "and the SECOND is on the AUXILIARY context, after the matched "
              "scope closed and before the switch home (%ld seen, last on "
              "context %d)", seen, last_ctx);
    }
    CHECK(a.ar_rebinds - b.ar_rebinds == 1,
          "and the output pool was re-bound on the MAIN context afterwards, "
          "which is the same visibility rule in the other direction (%lu)",
          a.ar_rebinds - b.ar_rebinds);
    /* AND THIS LAUNCH'S SYNCHRONISATION IS THE ORIGINAL ONE, ASSERTED HERE
     * RATHER THAN ONLY ON THE OFF PATH (SL_BG_TF_AUX_SYNC). The fence mode is a
     * SEPARATE selector, so a launch that armed an arm and did not set it must
     * behave exactly as it did before that selector existed: two drains, no
     * sync object, no flush, and no fence entry point resolved on either
     * context. The last of those is what keeps gl_proc_aux at thirteen in this
     * arm's frozen census line. */
    CHECK(sl_bgtf_ar_sync() == SL_BGTF_FX_FINISH,
          "the synchronisation mode is FINISH, which is what an absent "
          "SL_BG_TF_AUX_SYNC means (%s)",
          sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(tf.sync_created == 0L && tf.sync_deleted == 0L
          && tf.fence_calls_main == 0L && tf.fence_calls_aux == 0L
          && tf.wait_calls_main == 0L && tf.wait_calls_aux == 0L
          && tf.delete_calls_main == 0L && tf.delete_calls_aux == 0L,
          "and NO sync object was created, waited on or deleted on either "
          "context (%ld/%ld created/deleted; fence %ld/%ld, wait %ld/%ld, "
          "delete %ld/%ld main/aux)", tf.sync_created, tf.sync_deleted,
          tf.fence_calls_main, tf.fence_calls_aux, tf.wait_calls_main,
          tf.wait_calls_aux, tf.delete_calls_main, tf.delete_calls_aux);
    CHECK(tf.flush_calls == 0L,
          "nor was one glFlush issued anywhere in the run (%ld)",
          tf.flush_calls);
    CHECK(first_of(mark, TFGL_FLUSH) == NULL
          && first_of(mark, TFGL_FENCESYNC) == NULL
          && first_of(mark, TFGL_WAITSYNC) == NULL
          && first_of(mark, TFGL_DELETESYNC) == NULL,
          "and the bracket's transcript holds no flush and no sync record at "
          "all, which is the absence read from the ORDER and not from a count");
    {
        const struct sl_bgtf_fence_counters *fc = sl_bgtf_fence_stats();
        CHECK(fc->fx_mode == (unsigned long) SL_BGTF_FX_FINISH
              && fc->fx_asked == 0 && fc->fx_in_fences == 0
              && fc->fx_out_fences == 0 && fc->fx_in_flushes == 0
              && fc->fx_out_flushes == 0 && fc->fx_in_waits == 0
              && fc->fx_out_waits == 0 && fc->fx_deletes == 0
              && fc->fx_finish_omits == 0 && fc->fx_leaks == 0
              && fc->fx_fatal_drops == 0 && fc->fx_refusals == 0
              && fc->fx_proc_fails == 0,
              "and the fence path's own census is entirely zero on a launch "
              "that never selected it (mode %lu asked %lu fences %lu/%lu "
              "flushes %lu/%lu waits %lu/%lu deletes %lu omits %lu leaks %lu "
              "drops %lu refusals %lu proc_fails %lu)", fc->fx_mode,
              fc->fx_asked, fc->fx_in_fences, fc->fx_out_fences,
              fc->fx_in_flushes, fc->fx_out_flushes, fc->fx_in_waits,
              fc->fx_out_waits, fc->fx_deletes, fc->fx_finish_omits,
              fc->fx_leaks, fc->fx_fatal_drops, fc->fx_refusals,
              fc->fx_proc_fails);
    }
    /* AND THE RE-BIND IS THE CALLS, NOT THE COUNT. A counter raised beside a
     * pair of calls that were never issued would read as a satisfied visibility
     * rule, so the two records are required by kind, by argument and by
     * context - the pool bound and then the global binding returned to 0,
     * immediately after the last record on the auxiliary context. */
    {
        long i2, last_aux = -1;
        for (i2 = mark; i2 < tf.nlog; i2++)
            if (tf.log[i2].ctx == CTX_AUX) last_aux = i2;
        CHECK(last_aux >= 0 && last_aux + 2 < tf.nlog,
              "there are records after the last one on the auxiliary context "
              "(%ld of %ld)", last_aux, tf.nlog);
        if (last_aux >= 0 && last_aux + 2 < tf.nlog) {
            const struct sl_bgtfgl_call *r1 = &tf.log[last_aux + 1];
            const struct sl_bgtfgl_call *r2 = &tf.log[last_aux + 2];
            CHECK(r1->kind == TFGL_BINDBUFFER && r1->a == GL_ARRAY_BUFFER
                  && r1->b == (long) pool && r1->ctx == CTX_MAIN,
                  "the FIRST record after the switch home binds the OUTPUT POOL "
                  "on the MAIN context (%s target %ld name %ld ctx %d)",
                  kind_name(r1->kind), r1->a, r1->b, r1->ctx);
            CHECK(r2->kind == TFGL_BINDBUFFER && r2->a == GL_ARRAY_BUFFER
                  && r2->b == 0 && r2->ctx == CTX_MAIN,
                  "and the SECOND returns the global array binding to 0, so "
                  "nothing later reads a client pointer as an offset into the "
                  "pool (%s target %ld name %ld ctx %d)", kind_name(r2->kind),
                  r2->a, r2->b, r2->ctx);
        }
    }
    CHECK(a.ar_writes - b.ar_writes == 1,
          "so the write is recorded as COMPLETED - the one thing that marks a "
          "slot written (%lu)", a.ar_writes - b.ar_writes);

    /* AND GL WAS ASKED WHETHER IT ACCEPTED ANY OF IT, ON THE RIGHT CONTEXT AND
     * NOWHERE ELSE. Two reads per bracket - the drain that empties the state
     * before this mode issues anything there, and the verdict after the scope
     * closed and the context's state was put back - and BOTH on the auxiliary
     * context. The main-context count is the one that must be zero: glGetError
     * CLEARS what it returns, so a check made on the main context would both
     * miss this context's error and silently consume an error belonging to the
     * ordinary rendering path. */
    CHECK(tf.get_error_calls == 2L,
          "the bracket read the error state exactly TWICE - once to empty it "
          "and once for the verdict (%ld)", tf.get_error_calls);
    CHECK(tf.get_error_on_aux == 2L && tf.get_error_on_main == 0L,
          "both on the AUXILIARY context, and NOT ONCE on the main one: a read "
          "there would consume an error state this mode does not own (%ld "
          "aux/%ld main)", tf.get_error_on_aux, tf.get_error_on_main);
    CHECK(a.ar_gl_errors == 0 && a.ar_gl_error_refusals == 0
          && a.ar_gl_first_error == 0 && a.ar_gl_drain_caps == 0,
          "GL rejected nothing, so nothing is refused for it and no drain hit "
          "its bound (%lu/%lu/0x%lX/%lu)", a.ar_gl_errors,
          a.ar_gl_error_refusals, a.ar_gl_first_error, a.ar_gl_drain_caps);
    CHECK(a.ar_gl_pre_errors == 0 && a.ar_gl_pre_refusals == 0
          && a.ar_gl_first_pre_error == 0,
          "and nothing was flagged on that context before the bracket reached "
          "it either, so nothing was refused for that (%lu/%lu/0x%lX)",
          a.ar_gl_pre_errors, a.ar_gl_pre_refusals,
          a.ar_gl_first_pre_error);

    /* THE CONTEXT CAME BACK, AND NOTHING WENT WRONG. */
    CHECK(tf.ctx_cur == CTX_MAIN,
          "the MAIN context is current at close, as the caller handed it over "
          "(%d)", tf.ctx_cur);
    CHECK(a.ar_switches - b.ar_switches == 1
          && a.ar_restores - b.ar_restores == 1
          && a.ar_switches == a.ar_restores,
          "the bracket's switch and restore are counted once each and are EQUAL "
          "(%lu/%lu)", a.ar_switches, a.ar_restores);
    CHECK(a.ar_buf_checks - b.ar_buf_checks == 2 && a.ar_buf_fails == 0,
          "two objects were verified shared and neither answered FALSE "
          "(%lu/%lu)", a.ar_buf_checks - b.ar_buf_checks, a.ar_buf_fails);
    CHECK(a.ar_creates == 1 && a.ar_create_fails == 0 && a.ar_skips == 0
          && a.ar_proc_fails == 0 && a.ar_prog_fails == 0
          && a.ar_vao_fails == 0 && a.ar_switch_fails == 0
          && a.ar_sync_refusals == 0 && a.ar_lost == 0 && a.ar_fatals == 0,
          "one context was brought to a usable state with no failure of any "
          "kind (%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu)", a.ar_creates,
          a.ar_create_fails, a.ar_skips, a.ar_proc_fails, a.ar_prog_fails,
          a.ar_vao_fails, a.ar_switch_fails, a.ar_sync_refusals, a.ar_lost,
          a.ar_fatals);
    CHECK(a.refusals == b.refusals && a.ar_refusals == 0,
          "and no served draw and no launch was refused (%lu/%lu)",
          a.refusals - b.refusals, a.ar_refusals);
    CHECK(tf.v_ptr_wrong_context == 0L && tf.v_call_no_context == 0L
          && tf.v_vptr_without_vao == 0L && tf.v_draw_without_position == 0L
          && tf.v_vertex_array_touched == 0L,
          "no pointer was used on a context other than the one it was resolved "
          "on, no call was made with no context current, no pointer command "
          "landed outside this mode's own array object, no draw there found the "
          "position array disabled, and the MAIN context's own position array "
          "was never moved (%ld/%ld/%ld/%ld/%ld)", tf.v_ptr_wrong_context,
          tf.v_call_no_context, tf.v_vptr_without_vao,
          tf.v_draw_without_position, tf.v_vertex_array_touched);
    CHECK(tf.v_draw_without_discard == 0L && tf.v_draw_without_program == 0L
          && tf.v_draw_without_tf_buffer == 0L && tf.v_tf_nested == 0L
          && tf.v_tf_unbalanced == 0L && tf.v_program_change_in_tf == 0L
          && tf.v_tf_overrun == 0L,
          "and the prepass draw on that context satisfied every precondition a "
          "prepass draw has, with the scope matched and the pool large enough "
          "(%ld/%ld/%ld/%ld/%ld/%ld/%ld)", tf.v_draw_without_discard,
          tf.v_draw_without_program, tf.v_draw_without_tf_buffer,
          tf.v_tf_nested, tf.v_tf_unbalanced, tf.v_program_change_in_tf,
          tf.v_tf_overrun);
    CHECK(tf.vao_bound == 0u && tf.vao_bound_main == 0u,
          "that context's array object is unbound again and the MAIN context's "
          "binding was never written (%u/%u)", (unsigned) tf.vao_bound,
          (unsigned) tf.vao_bound_main);
    CHECK(tf.current == 0u && tf.discard == 0 && tf.tf_buf == 0u
          && tf.arr_buf == 0u && tf.elem_buf == 0u,
          "and the MAIN context's own state came back exactly as the ordinary "
          "restoration leaves it (%u/%d/%u/%u/%u)", (unsigned) tf.current,
          tf.discard, (unsigned) tf.tf_buf, (unsigned) tf.arr_buf,
          (unsigned) tf.elem_buf);

    /* THE ARM'S OWN COUNT, AND THE POOL'S ELIGIBILITY. */
    if (gpu) {
        CHECK(a.ar_gpu_submits - b.ar_gpu_submits == 1
              && a.ar_cpu_draws == b.ar_cpu_draws,
              "this bracket is counted as a GPU-SOURCE submission and not as a "
              "CPU-source draw (%lu/%lu)", a.ar_gpu_submits - b.ar_gpu_submits,
              a.ar_cpu_draws - b.ar_cpu_draws);
        b = snap();
        CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_FAR0,
              "and the slot IS bindable, because the write completed and was "
              "made visible here");
        CHECK(snap().binds - b.binds == 1,
              "the bind is counted (%lu)", snap().binds - b.binds);
        CHECK(tf.vptr.set && tf.vptr.buf == pool && tf.vptr.size == 4,
              "and the MAIN context's position pointer now names the SHARED "
              "pool, four floats wide (buf %u, size %d)",
              (unsigned) tf.vptr.buf, (int) tf.vptr.size);
    } else {
        CHECK(a.ar_cpu_draws - b.ar_cpu_draws == 1
              && a.ar_gpu_submits == b.ar_gpu_submits,
              "this bracket is counted as a CPU-SOURCE draw and not as a "
              "GPU-source submission (%lu/%lu)",
              a.ar_cpu_draws - b.ar_cpu_draws,
              a.ar_gpu_submits - b.ar_gpu_submits);
        /* AND THE POOL IS NEVER READ ON THIS ARM. The original-raster control is
         * what the caller consults, so sl_gfx_dl.c never offers the slot - but
         * the slot is written all the same, and that is the arm's whole content:
         * real off-context work whose output does not reach the image. */
        CHECK(sl_bgtf_bind_clip(s) == SL_BGGPU_DEPTH_FAR0,
              "the slot is written, so a DIRECT bind would succeed - what keeps "
              "the original positions on screen is the original-raster control "
              "in the caller, not an unwritten pool");
    }

    /* THE WORK COUNTS ROSE, which is what keeps a GPU_SOURCE run's raster draws
     * consistent with the prepasses behind them. */
    {
        const struct sl_bgtf_counters c = snap();
        CHECK(c.prepasses >= 1 && c.vertices >= (unsigned long) NV
              && c.walks >= 1,
              "a real prepass over real vertices in a real walk is claimed, "
              "because one really ran (%lu/%lu/%lu)", c.prepasses, c.vertices,
              c.walks);
    }

    /* A CACHE HIT RELOCATES NOTHING: no switch, no synchronisation point, no
     * call of any kind - which is where two pipeline drains per mesh would
     * otherwise be paid for nothing. */
    b = snap();
    calls_at_hit = tf.calls;
    mark = tf.nlog;
    finish_before = tf.finish_calls;
    {
        const long aux_sw = tf.aux_switches, main_sw = tf.main_switches;
        for (i = 1; i < AR_SERVED; i++)
            CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == s,
                  "served mesh %d is a cache HIT on the same slot", i);
        a = snap();
        CHECK(tf.calls == calls_at_hit && tf.nlog == mark,
              "the %d hits added no GL call and no transcript record (%ld -> "
              "%ld)", AR_SERVED - 1, calls_at_hit, tf.calls);
        CHECK(tf.finish_calls == finish_before,
              "and NO synchronisation point for any of them (%ld)",
              tf.finish_calls - finish_before);
        CHECK(tf.aux_switches == aux_sw && tf.main_switches == main_sw,
              "and NO CONTEXT SWITCH WAS EVEN ATTEMPTED (%ld/%ld)",
              tf.aux_switches - aux_sw, tf.main_switches - main_sw);
    }
    CHECK(a.ar_reuses - b.ar_reuses == (unsigned long) (AR_SERVED - 1),
          "each hit was counted as this mode's own reuse (%lu)",
          a.ar_reuses - b.ar_reuses);
    CHECK(a.ar_setups + a.ar_skips + a.ar_reuses == (unsigned long) AR_SERVED,
          "REACHED + SKIPPED + REUSES == the %d meshes served, so the account "
          "is complete (%lu + %lu + %lu)", AR_SERVED, a.ar_setups, a.ar_skips,
          a.ar_reuses);
    CHECK(a.ar_setups == a.ar_main_omits && a.ar_setups == a.ar_prepasses
          && a.ar_setups == a.ar_switches && a.ar_setups == a.ar_restores
          && a.ar_setups == a.ar_writes && a.ar_syncs == 2 * a.ar_setups,
          "and the per-bracket counts are EQUAL to one another with two "
          "synchronisation points each, which is this mode's own invariant "
          "(%lu/%lu/%lu/%lu/%lu/%lu)", a.ar_setups, a.ar_main_omits,
          a.ar_prepasses, a.ar_switches, a.ar_restores, a.ar_syncs);

    /* AND TEARDOWN DESTROYS THE CONTEXT, which takes the array object with it:
     * a container object belongs to one context, so no delete is issued or even
     * resolved for it. */
    b = snap();
    mark = tf.nlog;
    sl_bgtf_shutdown();
    CHECK(tf.aux_destroyed == 1L && tf.aux_exists == 0,
          "exactly one auxiliary context was destroyed and none is alive "
          "(%ld/%d)", tf.aux_destroyed, tf.aux_exists);
    CHECK(tf.nlog == mark,
          "teardown added NO GL record - destroying a context is not a GL "
          "command (%ld)", tf.nlog - mark);
    CHECK(tf.bind_vao == 2L,
          "and the array object was bound exactly twice in the whole run, both "
          "times on the auxiliary context: once to this mode's own object and "
          "once back to 0. Nothing deleted it, because the context's end "
          "releases a container object (%ld)", tf.bind_vao);
    CHECK(tf.fatal_calls == 0L,
          "and the terminate-now path was NOT taken on a run with no failure in "
          "it (%ld)", tf.fatal_calls);
    a = snap();
    printf("    ARDRAW arm=%s setups=%lu omits=%lu prepasses=%lu "
           "prepasses_aux=%lu vertices=%lu writes=%lu cpu_draws=%lu "
           "gpu_submits=%lu switches=%lu restores=%lu syncs=%lu rebinds=%lu "
           "reuses=%lu skips=%lu creates=%lu create_fails=%lu proc_fails=%lu "
           "prog_fails=%lu vao_fails=%lu buf_checks=%lu buf_fails=%lu "
           "switch_fails=%lu sync_refusals=%lu lost=%lu fatals=%lu "
           "gl_draws=%ld gl_draws_aux=%ld gl_drawv_aux=%ld gl_finish=%ld "
           "gl_isbuf=%ld gl_genvao=%ld gl_bindvao=%ld gl_calls_aux=%ld "
           "gl_proc_aux=%ld gl_ptr_wrong=%ld gl_vao_on_main=%ld "
           "gl_nocontext=%ld gl_fatals=%ld gl_after_fatal=%ld "
           "gl_errors=%lu gl_error_refusals=%lu gl_first_error=%lu "
           "gl_drain_caps=%lu gl_pre_errors=%lu gl_pre_refusals=%lu "
           "gl_first_pre_error=%lu "
           "gl_geterr=%ld gl_geterr_aux=%ld gl_geterr_main=%ld\n",
           sl_bgtf_ar_name((int) a.ar_mode), a.ar_setups, a.ar_main_omits,
           a.ar_prepasses, a.ar_prepasses_aux, a.ar_vertices, a.ar_writes,
           a.ar_cpu_draws, a.ar_gpu_submits, a.ar_switches, a.ar_restores,
           a.ar_syncs, a.ar_rebinds, a.ar_reuses, a.ar_skips, a.ar_creates,
           a.ar_create_fails, a.ar_proc_fails, a.ar_prog_fails, a.ar_vao_fails,
           a.ar_buf_checks, a.ar_buf_fails, a.ar_switch_fails,
           a.ar_sync_refusals, a.ar_lost, a.ar_fatals, tf.draw_arrays,
           tf.draws_on_aux, tf.draw_vertices_on_aux, tf.finish_calls,
           tf.is_buffer_calls, tf.gen_vao, tf.bind_vao, tf.calls_on_aux,
           tf.proc_calls_on_aux, tf.v_ptr_wrong_context, tf.v_vao_on_main,
           tf.v_call_no_context, tf.fatal_calls, tf.calls_after_fatal,
           a.ar_gl_errors, a.ar_gl_error_refusals, a.ar_gl_first_error,
           a.ar_gl_drain_caps, a.ar_gl_pre_errors, a.ar_gl_pre_refusals,
           a.ar_gl_first_pre_error,
           tf.get_error_calls, tf.get_error_on_aux, tf.get_error_on_main);
    end_case();
}

/* ==================================================================== *
 *  THE FENCE SYNCHRONISATION MODE (SL_BG_TF_AUX_SYNC=FENCE)
 *
 *  WHAT THESE CASES CAN AND CANNOT ESTABLISH, SAID FIRST. Every GL entry
 *  point below is a C function in sl_gfx_bgtf_testgl.c and both context
 *  functions are too. There is no window, no real GL context, no GPU, no
 *  shared object and no pipeline, so a fence here signals nothing and a wait
 *  returns at once. WHETHER A REAL DRIVER'S FENCE MAKES A SECOND CONTEXT'S
 *  TRANSFORM-FEEDBACK WRITES VISIBLE TO THE FIRST IS UNQUALIFIED BY EVERY
 *  CHECK IN THIS FILE. What is established is the module's own behaviour: the
 *  ORDER of the four moves in each direction, WHICH CONTEXT each landed on,
 *  which pointer was used on which context, that one object is created, waited
 *  on across the pair and deleted exactly once per handoff with none ever
 *  leaked, that a missing entry point or a refused creation REFUSES and never
 *  silently falls back to the drains, and that the FINISH mode and every off
 *  launch are untouched by all of it.
 * ==================================================================== */

static struct sl_bgtf_fence_counters fsnap(void)
{
    return *sl_bgtf_fence_stats();
}

/* The whole of a FENCE bracket's auxiliary-context window, from the first
 * object check to the producer's flush on that context. Twenty-six records
 * against the FINISH window's twenty-three: the consumer's wait and its delete
 * are inserted immediately before the first bind that reads the input buffer,
 * and the single completion drain at the far end becomes a fence and a flush.
 * NOTHING ELSE IN THE LIST DIFFERS FROM ar_expect's, record for record, which
 * is the claim that only the synchronisation changed. */
#define AR_FX_RECORDS 26
static int ar_fence_expect(struct expect *e, int *want, GLuint prog, GLuint vbo,
                           GLuint pool, GLuint vao, long handle)
{
    int n = 0;
    n = ax_push(e, want, n, TFGL_ISBUFFER, (long) vbo, GL_TRUE, ANY, ANY,
                CTX_AUX, "the INPUT buffer is verified shared, there, first");
    n = ax_push(e, want, n, TFGL_ISBUFFER, (long) pool, GL_TRUE, ANY, ANY,
                CTX_AUX, "and so is the OUTPUT pool, before anything is bound");
    /* THE CONSUMER'S SERVER WAIT, ON THE CONSUMER'S CONTEXT, BEFORE THE BINDS.
     * `a` is the handle the MAIN context's own glFenceSync answered, which is
     * what makes this a CROSS-context handoff rather than two unrelated calls;
     * `b` is the flags, which OpenGL 4.6 section 4.1.1 requires to be 0; `c` is
     * the shim's record that the timeout was TIMEOUT_IGNORED, which that same
     * section requires. */
    n = ax_push(e, want, n, TFGL_WAITSYNC, handle, 0, 1, ANY, CTX_AUX,
                "the auxiliary context's SERVER WAIT on the main context's "
                "fence, issued before any bind that reads the input buffer");
    /* AND THE OBJECT IS RETIRED IMMEDIATELY, WITH THIS CONTEXT'S POINTER. A
     * sync object is shared between contexts (OpenGL 4.6 Appendix D) and a
     * delete after a wait neither cancels nor outlives that wait (section 4.1),
     * so this is the whole of the input object's life. */
    n = ax_push(e, want, n, TFGL_DELETESYNC, handle, ANY, ANY, ANY, CTX_AUX,
                "and deletes it at once, there, so nothing is held across the "
                "prepass");
    n = ax_push(e, want, n, TFGL_BINDVAO, (long) vao, ANY, ANY, ANY, CTX_AUX,
                "this mode's OWN array object, bound there - a container object "
                "is never shared");
    n = ax_push(e, want, n, TFGL_CLIENTSTATE, GL_VERTEX_ARRAY, 1, ANY, ANY,
                CTX_AUX, "the position array is ENABLED there");
    n = ax_push(e, want, n, TFGL_USEPROGRAM, (long) prog, ANY, ANY, ANY,
                CTX_AUX, "the SHARED prepass program, made current there");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_sc, from this module's own cache");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_post");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_org");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_C, all sixteen");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_fogless");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_dmode, this draw's own");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_fk1");
    n = ax_push(e, want, n, TFGL_UNIFORM, ANY, (long) prog, ANY, ANY, CTX_AUX,
                "u_fk2");
    n = ax_push(e, want, n, TFGL_ENABLE, GL_RASTERIZER_DISCARD, ANY, ANY, ANY,
                CTX_AUX, "discard ON there: it is per-context too");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, (long) vbo, ANY,
                ANY, CTX_AUX, "the shared input buffer, bound FRESHLY there - "
                "which is the bind-after-the-wait this mode's rule asks for");
    n = ax_push(e, want, n, TFGL_VERTEXPOINTER, 3, GL_FLOAT, (long) STR,
                (long) vbo, CTX_AUX, "the prepass's own position pointer");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ARRAY_BUFFER, 0, ANY, ANY,
                CTX_AUX, "and the global array binding left at 0");
    n = ax_push(e, want, n, TFGL_BINDBUFFER, GL_ELEMENT_ARRAY_BUFFER, 0, ANY,
                ANY, CTX_AUX, "the element binding dropped there");
    n = ax_push(e, want, n, TFGL_BINDBUFFERBASE, GL_TRANSFORM_FEEDBACK_BUFFER,
                0, (long) pool, ANY, CTX_AUX,
                "the shared pool bound to the indexed target");
    n = ax_push(e, want, n, TFGL_BEGINTF, GL_POINTS, ANY, ANY, ANY, CTX_AUX,
                "the capture opens there");
    n = ax_push(e, want, n, TFGL_DRAWARRAYS, GL_POINTS, 0, (long) NV,
                (long) pool, CTX_AUX,
                "THE REAL PREPASS: one draw over every vertex, recording into "
                "the shared pool");
    n = ax_push(e, want, n, TFGL_ENDTF, ANY, ANY, ANY, ANY, CTX_AUX,
                "and closes, matched");
    /* AND THE PRODUCER'S FENCE AT EXACTLY THE INSTANT FINISH PUT ITS DRAIN:
     * after the matched scope and before this context's state is put back. The
     * condition and the flags are the two the specification constrains. */
    n = ax_push(e, want, n, TFGL_FENCESYNC, ANY, 0x9117, 0, ANY, CTX_AUX,
                "the output fence, on the context that WROTE, where the "
                "completion drain used to be");
    n = ax_push(e, want, n, TFGL_FLUSH, ANY, ANY, ANY, ANY, CTX_AUX,
                "and that context's own flush, explicitly - a consumer's flush "
                "bit does not reach a producer's command stream");
    return n;
}

/* ONE ARM UNDER THE FENCE MODE, END TO END. The caller has already proved the
 * environment selected both the arm and the mode. */
static void fx_scenario(int gpu)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct sl_bgtf_fence_counters fb, fa;
    struct expect e[AR_FX_RECORDS];
    int want[AR_FX_RECORDS];
    GLuint vbo, pool = 0u, prog = 0u, vao;
    long mark, in_handle = 0, out_handle = 0;
    int s, n;

    begin_case(gpu
        ? "auxiliary real prepass, GPU_SOURCE, FENCE synchronisation: the same "
          "real nonzero prepass on the second context, with the two pipeline "
          "drains replaced by two cross-context sync objects - a producer "
          "fence, an explicit producer flush and a consumer server wait in each "
          "direction - and the shared pool still bindable"
        : "auxiliary real prepass, CPU_SOURCE, FENCE synchronisation: the same "
          "replacement with the main context rasterising the ORIGINAL position "
          "stream");
    arm();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    /* AND THE FENCE ENTRY POINTS, WHICH DEFAULT TO ABSENT. Setting this gate
     * here is what makes the two capability cases below statements about the
     * module rather than about the shim. */
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_aux_real() == (gpu ? SL_BGTF_AR_GPU : SL_BGTF_AR_CPU),
          "the mode is in force on the arm this process was launched for (%s)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    prog = tf.last_program;
    CHECK(tf.sync_created == 0L && tf.flush_calls == 0L
          && tf.proc_calls_on_aux == 0L,
          "nothing was created, flushed or resolved on a second context before "
          "the first served draw - the capability is lazy (%ld/%ld/%ld)",
          tf.sync_created, tf.flush_calls, tf.proc_calls_on_aux);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");

    b = snap();
    fb = fsnap();
    mark = tf.nlog;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s >= 0, "the served draw is ACCEPTED (%d)", s);
    a = snap();
    fa = fsnap();
    vao = tf.last_gen_vao;

    /* THE MODE IS IN FORCE, AND IT IS IN FORCE BECAUSE BOTH HALVES HELD. */
    CHECK(sl_bgtf_ar_sync() == SL_BGTF_FX_FENCE,
          "the effective synchronisation mode is FENCE (%s)",
          sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(fa.fx_mode == (unsigned long) SL_BGTF_FX_FENCE && fa.fx_asked == 1,
          "the census records the EFFECTIVE mode and that it was asked for "
          "(%lu/%lu)", fa.fx_mode, fa.fx_asked);
    CHECK(fa.fx_proc_fails == 0,
          "and no fence entry point was missing on either context (%lu)",
          fa.fx_proc_fails);

    /* NOT ONE DRAIN. This is the replacement, and it is read as an absence of
     * glFinish beside a presence of the four fence moves - either alone would
     * also be satisfied by a mode that added a fence BESIDE the drains. */
    CHECK(tf.finish_calls == 0L,
          "NO glFinish was issued anywhere in this launch: the fence mode "
          "REPLACES the drains rather than adding to them (%ld)",
          tf.finish_calls);
    CHECK(fa.fx_finish_omits - fb.fx_finish_omits == 2,
          "and the two drains the bracket did not issue are a POSITIVE record "
          "(%lu)", fa.fx_finish_omits - fb.fx_finish_omits);
    CHECK(a.ar_syncs - b.ar_syncs == 2,
          "two handoffs were still established, which is what the mode's own "
          "count means under either value (%lu)", a.ar_syncs - b.ar_syncs);

    /* THE FOUR MOVES OF THE INPUT HANDOFF, EACH ON ITS OWN CONTEXT. */
    {
        const struct sl_bgtfgl_call *f = first_of(mark, TFGL_FENCESYNC);
        CHECK(f != NULL && f->ctx == CTX_MAIN,
              "the FIRST fence is created on the MAIN context, before the "
              "switch: it is the producer of the input buffer's writes (%d)",
              f != NULL ? f->ctx : -99);
        if (f != NULL) {
            in_handle = f->a;
            CHECK(f->a != 0, "and the creation answered a handle (%ld)", f->a);
            CHECK(f->b == 0x9117 && f->c == 0,
                  "with the condition SYNC_GPU_COMMANDS_COMPLETE and flags 0, "
                  "which are the only pair OpenGL 4.6 section 4.1.1 accepts "
                  "(0x%lX/%ld)", f->b, f->c);
        }
    }
    {
        const struct sl_bgtfgl_call *fl = first_of(mark, TFGL_FLUSH);
        CHECK(fl != NULL && fl->ctx == CTX_MAIN,
              "the producer's own glFlush follows it on the MAIN context - "
              "OpenGL 4.5 core section 4.1.2 is why it cannot be left to the "
              "consumer's flush bit (%d)", fl != NULL ? fl->ctx : -99);
    }
    CHECK(fa.fx_in_fences - fb.fx_in_fences == 1
          && fa.fx_in_fence_fails == 0
          && fa.fx_in_flushes - fb.fx_in_flushes == 1
          && fa.fx_in_waits - fb.fx_in_waits == 1
          && fa.fx_in_proved - fb.fx_in_proved == 1,
          "the input handoff is one fence, one flush, one wait and one proved "
          "chain (%lu/%lu/%lu/%lu/%lu)", fa.fx_in_fences - fb.fx_in_fences,
          fa.fx_in_fence_fails, fa.fx_in_flushes - fb.fx_in_flushes,
          fa.fx_in_waits - fb.fx_in_waits, fa.fx_in_proved - fb.fx_in_proved);

    /* THE POOL THIS BRACKET RECORDED INTO, from the transcript. */
    {
        const struct sl_bgtfgl_call *r = first_of(mark, TFGL_BINDBUFFERBASE);
        CHECK(r != NULL, "the bracket bound a pool to the indexed target");
        if (r != NULL) pool = (GLuint) r->c;
    }

    /* THE WHOLE AUXILIARY WINDOW, IN ORDER AND WITH ITS PER-RECORD CONTEXT. */
    {
        long from = mark;
        while (from < tf.nlog && tf.log[from].kind != TFGL_ISBUFFER) from++;
        CHECK(from < tf.nlog, "the auxiliary bracket is in the transcript");
        /* THE ARRIVAL DRAIN IS STILL FIRST, AND STILL BEFORE THE WAIT. A
         * WaitSync is a command this bracket issues on that context, so a drain
         * placed after it would be reading a state this bracket had already had
         * a hand in - which is exactly the attribution the drain exists to
         * make. */
        if (from > mark) {
            const struct sl_bgtfgl_call *d = &tf.log[from - 1];
            CHECK(d->kind == TFGL_GETERROR && d->ctx == CTX_AUX && d->b == 1,
                  "the record immediately before the first object check is "
                  "still the arrival drain, on the AUXILIARY context (%s, ctx "
                  "%d, on_aux %ld)", kind_name(d->kind), d->ctx, d->b);
            CHECK(d->a == 0, "and it found the state empty (0x%lX)", d->a);
        } else {
            CHECK(0, "there is a record before the first object check");
        }
        n = ar_fence_expect(e, want, prog, vbo, pool, vao, in_handle);
        CHECK(n == AR_FX_RECORDS,
              "the FENCE bracket this fixture requires is %d records long "
              "(%d)", AR_FX_RECORDS, n);
        seq_window_is(from, e, n);
        seq_ctx_is(from, e, want, n);
        /* AND THE RESTORATION, UNCHANGED FROM THE FINISH MODE'S, immediately
         * after it - which is the other half of "only the synchronisation
         * moved". */
        {
            struct expect r2[8];
            int w2[8];
            const int n2 = ar_expect_restore(r2, w2, vao);
            seq_window_is(from + n, r2, n2);
            seq_ctx_is(from + n, r2, w2, n2);
        }
        /* THE OUTPUT FENCE'S OWN HANDLE, which is the record three before the
         * restoration: the FENCESYNC this window ends one short of. */
        {
            long i2;
            for (i2 = from; i2 < from + n; i2++)
                if (tf.log[i2].kind == TFGL_FENCESYNC) out_handle = tf.log[i2].a;
        }
    }

    /* THE REAL NONZERO COUNT IS UNCHANGED BY THE MODE. */
    CHECK(tf.draws_on_aux == 1L && tf.draw_vertices_on_aux == (long) NV,
          "ONE draw on the auxiliary context, over every vertex (%ld/%ld)",
          tf.draws_on_aux, tf.draw_vertices_on_aux);
    CHECK(tf.draw_arrays == 1L,
          "and the MAIN context issued NO prepass draw (%ld)", tf.draw_arrays);
    CHECK(a.ar_main_omits - b.ar_main_omits == 1,
          "the omission is a positive record (%lu)",
          a.ar_main_omits - b.ar_main_omits);

    /* THE FOUR MOVES OF THE OUTPUT HANDOFF, MIRRORED. */
    CHECK(out_handle != 0 && out_handle != in_handle,
          "the output fence is a DIFFERENT object from the input one (%ld vs "
          "%ld)", out_handle, in_handle);
    CHECK(tf.last_wait == out_handle && tf.last_wait_on_aux == 0,
          "the LAST wait of the bracket is on the output fence and is issued on "
          "the MAIN context, after the verified switch home (%ld on_aux %d)",
          tf.last_wait, tf.last_wait_on_aux);
    CHECK(fa.fx_out_fences - fb.fx_out_fences == 1
          && fa.fx_out_fence_fails == 0
          && fa.fx_out_flushes - fb.fx_out_flushes == 1
          && fa.fx_out_waits - fb.fx_out_waits == 1
          && fa.fx_out_proved - fb.fx_out_proved == 1,
          "the output handoff is one fence, one flush, one wait and one proved "
          "chain (%lu/%lu/%lu/%lu/%lu)", fa.fx_out_fences - fb.fx_out_fences,
          fa.fx_out_fence_fails, fa.fx_out_flushes - fb.fx_out_flushes,
          fa.fx_out_waits - fb.fx_out_waits, fa.fx_out_proved - fb.fx_out_proved);
    /* AND THE OUTPUT WAIT AND DELETE COME BEFORE THE RE-BIND, in that order, on
     * the MAIN context. The rule the mode rests on is bind-AFTER-visibility, so
     * a wait issued after the bind would be a wait that guarded nothing. */
    {
        long i2, last_aux = -1;
        for (i2 = mark; i2 < tf.nlog; i2++)
            if (tf.log[i2].ctx == CTX_AUX) last_aux = i2;
        CHECK(last_aux >= 0 && last_aux + 4 < tf.nlog,
              "there are four records after the last one on the auxiliary "
              "context (%ld of %ld)", last_aux, tf.nlog);
        if (last_aux >= 0 && last_aux + 4 < tf.nlog) {
            const struct sl_bgtfgl_call *w1 = &tf.log[last_aux + 1];
            const struct sl_bgtfgl_call *w2 = &tf.log[last_aux + 2];
            const struct sl_bgtfgl_call *r1 = &tf.log[last_aux + 3];
            const struct sl_bgtfgl_call *r2 = &tf.log[last_aux + 4];
            CHECK(w1->kind == TFGL_WAITSYNC && w1->a == out_handle
                  && w1->b == 0 && w1->c == 1 && w1->ctx == CTX_MAIN,
                  "the FIRST record after the switch home is the MAIN "
                  "context's server wait on the output fence (%s a %ld flags "
                  "%ld ignored %ld ctx %d)", kind_name(w1->kind), w1->a, w1->b,
                  w1->c, w1->ctx);
            CHECK(w2->kind == TFGL_DELETESYNC && w2->a == out_handle
                  && w2->ctx == CTX_MAIN,
                  "the SECOND retires that object, there (%s a %ld ctx %d)",
                  kind_name(w2->kind), w2->a, w2->ctx);
            CHECK(r1->kind == TFGL_BINDBUFFER && r1->a == GL_ARRAY_BUFFER
                  && r1->b == (long) pool && r1->ctx == CTX_MAIN,
                  "and only THEN is the output pool bound on the MAIN context "
                  "(%s target %ld name %ld ctx %d)", kind_name(r1->kind),
                  r1->a, r1->b, r1->ctx);
            CHECK(r2->kind == TFGL_BINDBUFFER && r2->a == GL_ARRAY_BUFFER
                  && r2->b == 0 && r2->ctx == CTX_MAIN,
                  "with the global array binding returned to 0 (%s name %ld)",
                  kind_name(r2->kind), r2->b);
        }
    }

    /* EVERY WAIT WAS A CROSS-CONTEXT WAIT, which is the whole point of the
     * handoff: a wait taken on the context that created the fence would be a
     * handoff that went nowhere, and is legal GL that no count would notice. */
    CHECK(tf.v_sync_wait_own == 0L,
          "neither wait was taken on the context that CREATED its fence (%ld)",
          tf.v_sync_wait_own);
    CHECK(tf.wait_calls_aux == 1L && tf.wait_calls_main == 1L,
          "one wait on each context, which is what two opposite handoffs mean "
          "(%ld aux, %ld main)", tf.wait_calls_aux, tf.wait_calls_main);
    CHECK(tf.fence_calls_main == 1L && tf.fence_calls_aux == 1L,
          "and one creation on each, each on the PRODUCER's side (%ld/%ld)",
          tf.fence_calls_main, tf.fence_calls_aux);
    CHECK(tf.flush_calls_main == 1L && tf.flush_calls_aux == 1L,
          "and one flush on each, each on the PRODUCER's side (%ld/%ld)",
          tf.flush_calls_main, tf.flush_calls_aux);
    CHECK(tf.delete_calls_aux == 1L && tf.delete_calls_main == 1L,
          "and one delete on each, each issued by the context that was current "
          "with that context's own pointer (%ld/%ld)", tf.delete_calls_aux,
          tf.delete_calls_main);

    /* NOTHING LEAKED AND NOTHING WAS USED AFTER BEING RETIRED. */
    CHECK(tf.sync_created == 2L && tf.sync_deleted == 2L && tf.sync_live == 0L,
          "two objects were created and both were deleted: nothing is live "
          "(%ld created, %ld deleted, %ld live)", tf.sync_created,
          tf.sync_deleted, tf.sync_live);
    CHECK(tf.sync_peak == 1L,
          "and never more than ONE at a time - the input object is retired "
          "before the output one exists (%ld)", tf.sync_peak);
    CHECK(fa.fx_deletes - fb.fx_deletes == 2
          && fa.fx_deletes_aux - fb.fx_deletes_aux == 1
          && fa.fx_deletes_main - fb.fx_deletes_main == 1,
          "which the module's own count agrees with, split by context "
          "(%lu total, %lu aux, %lu main)", fa.fx_deletes - fb.fx_deletes,
          fa.fx_deletes_aux - fb.fx_deletes_aux,
          fa.fx_deletes_main - fb.fx_deletes_main);
    CHECK(fa.fx_leaks == 0 && fa.fx_fatal_drops == 0,
          "no bracket returned holding an object and none was abandoned "
          "(%lu/%lu)", fa.fx_leaks, fa.fx_fatal_drops);
    CHECK(tf.v_sync_unknown == 0L && tf.v_sync_double_delete == 0L,
          "no wait or delete named an object that was never minted or had "
          "already been retired, and none was deleted twice (%ld/%ld)",
          tf.v_sync_unknown, tf.v_sync_double_delete);
    CHECK(tf.v_sync_badfence == 0L && tf.v_sync_badwait == 0L,
          "every creation used the one accepted condition and flags, and every "
          "wait the one accepted flags and timeout (%ld/%ld)",
          tf.v_sync_badfence, tf.v_sync_badwait);

    /* AND THE POINTERS WERE THE RIGHT CONTEXT'S, which is a property of the
     * ADDRESS and not of any argument: the shim hands back a different function
     * per context for all three names and each knows which it belongs to. */
    CHECK(tf.v_ptr_wrong_context == 0L && tf.v_call_no_context == 0L,
          "no fence entry point was invoked on a context other than the one it "
          "was resolved on, and no call was made with no context current "
          "(%ld/%ld)", tf.v_ptr_wrong_context, tf.v_call_no_context);
    CHECK(tf.proc_calls_on_aux == 16L,
          "sixteen names were resolved on the auxiliary context: the two the "
          "shared acquire needs, the eleven the real prepass needs and the "
          "three this mode adds (%ld)", tf.proc_calls_on_aux);

    /* THE WRITE HELD AND THE SLOT IS BINDABLE. */
    CHECK(a.ar_writes - b.ar_writes == 1 && a.ar_rebinds - b.ar_rebinds == 1,
          "the whole chain held, so the slot is marked written exactly once "
          "(%lu/%lu)", a.ar_writes - b.ar_writes, a.ar_rebinds - b.ar_rebinds);
    CHECK(fa.fx_refusals == 0 && fa.fx_unproved_refusals == 0,
          "and no fence guard refused anything (%lu/%lu)", fa.fx_refusals,
          fa.fx_unproved_refusals);
    CHECK(sl_bgtf_bind_clip(s) >= 0,
          "the slot the pool belongs to is bindable from the caller (%d)", s);
    CHECK(a.ar_gl_errors == 0 && a.ar_gl_error_refusals == 0
          && a.ar_gl_pre_errors == 0 && a.ar_gl_pre_refusals == 0
          && a.ar_gl_drain_caps == 0,
          "and GL rejected nothing, on either drain (%lu/%lu/%lu/%lu/%lu)",
          a.ar_gl_errors, a.ar_gl_error_refusals, a.ar_gl_pre_errors,
          a.ar_gl_pre_refusals, a.ar_gl_drain_caps);
    CHECK(tf.get_error_on_main == 0L,
          "and the MAIN context's error state was never read, so this mode "
          "consumed no error belonging to the ordinary rendering path (%ld)",
          tf.get_error_on_main);

    /* A CACHE HIT COSTS NO FENCE AT ALL, which is the other half of "the cost
     * is per MISS": an identical key inside one walk is served from the slot
     * without entering the bracket, so no object is created, no context is
     * switched to and no flush is issued. */
    {
        const long cre = tf.sync_created, fls = tf.flush_calls;
        const long sw = tf.aux_switch_calls;
        const struct sl_bgtf_counters rb = snap();
        const int s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
        const struct sl_bgtf_counters ra = snap();
        CHECK(s2 == s && ra.reuses - rb.reuses == 1,
              "the identical key is served from the same slot as a REUSE "
              "(%d vs %d, %lu)", s2, s, ra.reuses - rb.reuses);
        CHECK(ra.ar_reuses - rb.ar_reuses == 1
              && ra.ar_prepasses == rb.ar_prepasses,
              "the mode counts the reuse and no second prepass (%lu/%lu)",
              ra.ar_reuses - rb.ar_reuses, ra.ar_prepasses - rb.ar_prepasses);
        CHECK(tf.sync_created == cre && tf.flush_calls == fls
              && tf.aux_switch_calls == sw,
              "and the HIT created no sync object, issued no flush and did not "
              "even ask for the second context (%ld/%ld/%ld)",
              tf.sync_created - cre, tf.flush_calls - fls,
              tf.aux_switch_calls - sw);
        CHECK(fsnap().fx_in_fences == fa.fx_in_fences
              && fsnap().fx_out_fences == fa.fx_out_fences
              && fsnap().fx_in_waits == fa.fx_in_waits
              && fsnap().fx_out_waits == fa.fx_out_waits,
              "which the fence census agrees with, field for field");
    }

    /* AND TEARDOWN LEAVES NOTHING LIVE. */
    sl_bgtf_shutdown();
    CHECK(tf.sync_live == 0L && tf.aux_destroyed == 1L && tf.aux_exists == 0,
          "teardown destroys the context with no sync object outstanding "
          "(%ld live, %ld destroyed, %d alive)", tf.sync_live,
          tf.aux_destroyed, tf.aux_exists);
    CHECK(tf.fatal_calls == 0L,
          "and the terminate-now path was NOT taken on a run with no failure "
          "in it (%ld)", tf.fatal_calls);
    a = snap();
    fa = fsnap();
    printf("    ARFENCE arm=%s sync=%s asked=%lu setups=%lu omits=%lu "
           "prepasses=%lu prepasses_aux=%lu vertices=%lu writes=%lu "
           "cpu_draws=%lu gpu_submits=%lu switches=%lu restores=%lu syncs=%lu "
           "rebinds=%lu reuses=%lu creates=%lu proc_fails=%lu buf_checks=%lu "
           "lost=%lu fatals=%lu "
           "in_fences=%lu in_fence_fails=%lu in_flushes=%lu in_waits=%lu "
           "in_proved=%lu out_fences=%lu out_fence_fails=%lu out_flushes=%lu "
           "out_waits=%lu out_proved=%lu deletes=%lu deletes_main=%lu "
           "deletes_aux=%lu leaks=%lu fatal_drops=%lu refusals=%lu "
           "unproved_refusals=%lu finish_omits=%lu fx_proc_fails=%lu "
           "fx_proc_fails_main=%lu fx_proc_fails_aux=%lu "
           "gl_finish=%ld gl_flush=%ld gl_flush_main=%ld gl_flush_aux=%ld "
           "gl_fence_main=%ld gl_fence_aux=%ld gl_wait_main=%ld "
           "gl_wait_aux=%ld gl_del_main=%ld gl_del_aux=%ld gl_sync_created=%ld "
           "gl_sync_deleted=%ld gl_sync_live=%ld gl_sync_peak=%ld "
           "gl_proc_aux=%ld gl_ptr_wrong=%ld gl_sync_unknown=%ld "
           "gl_sync_double=%ld gl_sync_badfence=%ld gl_sync_badwait=%ld "
           "gl_sync_waitown=%ld gl_geterr_main=%ld gl_fatals=%ld "
           "gl_after_fatal=%ld\n",
           sl_bgtf_ar_name((int) a.ar_mode),
           sl_bgtf_fx_name((int) fa.fx_mode), fa.fx_asked,
           a.ar_setups, a.ar_main_omits, a.ar_prepasses, a.ar_prepasses_aux,
           a.ar_vertices, a.ar_writes, a.ar_cpu_draws, a.ar_gpu_submits,
           a.ar_switches, a.ar_restores, a.ar_syncs, a.ar_rebinds, a.ar_reuses,
           a.ar_creates, a.ar_proc_fails, a.ar_buf_checks, a.ar_lost,
           a.ar_fatals,
           fa.fx_in_fences, fa.fx_in_fence_fails, fa.fx_in_flushes,
           fa.fx_in_waits, fa.fx_in_proved, fa.fx_out_fences,
           fa.fx_out_fence_fails, fa.fx_out_flushes, fa.fx_out_waits,
           fa.fx_out_proved, fa.fx_deletes, fa.fx_deletes_main,
           fa.fx_deletes_aux, fa.fx_leaks, fa.fx_fatal_drops, fa.fx_refusals,
           fa.fx_unproved_refusals, fa.fx_finish_omits, fa.fx_proc_fails,
           fa.fx_proc_fails_main, fa.fx_proc_fails_aux,
           tf.finish_calls, tf.flush_calls, tf.flush_calls_main,
           tf.flush_calls_aux, tf.fence_calls_main, tf.fence_calls_aux,
           tf.wait_calls_main, tf.wait_calls_aux, tf.delete_calls_main,
           tf.delete_calls_aux, tf.sync_created, tf.sync_deleted,
           tf.sync_live, tf.sync_peak, tf.proc_calls_on_aux,
           tf.v_ptr_wrong_context, tf.v_sync_unknown, tf.v_sync_double_delete,
           tf.v_sync_badfence, tf.v_sync_badwait, tf.v_sync_wait_own,
           tf.get_error_on_main, tf.fatal_calls, tf.calls_after_fatal);
    end_case();
}

/* A FENCE ENTRY POINT THAT DOES NOT RESOLVE ON ONE CONTEXT. Each of the two
 * contexts is failed independently, because the module resolves its MAIN set
 * before anything exists and its AUXILIARY set after the context is current -
 * two different instants with two different cleanups and two different lines.
 *
 * THE OUTCOME IS A REFUSAL AND NEVER A FALL BACK TO THE DRAINS, which is the
 * one property these cases exist for: a launch that asked for the fence path
 * and silently got two glFinish calls would report fence numbers for a drained
 * pipeline. */
static void fx_cap_shape(const char *which, const char *miss_main,
                         const char *miss_aux)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct sl_bgtf_fence_counters fa;
    GLuint vbo;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    tf.withhold_fence_main = miss_main;
    tf.withhold_fence_aux = miss_aux;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    fa = fsnap();
    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(tf.finish_calls == 0L,
          "%s: and the refusal did NOT fall back to the drains - not one "
          "glFinish was issued (%ld)", which, tf.finish_calls);
    CHECK(tf.flush_calls == 0L && tf.sync_created == 0L,
          "%s: nor was any flush issued or sync object created (%ld/%ld)",
          which, tf.flush_calls, tf.sync_created);
    CHECK(tf.draw_arrays == 0L,
          "%s: and NO prepass ran on either context (%ld)", which,
          tf.draw_arrays);
    CHECK(a.ar_prepasses == b.ar_prepasses && a.ar_writes == b.ar_writes,
          "%s: nothing claims a prepass or a completed write", which);
    CHECK(a.refusals - b.refusals == 1,
          "%s: the refused draw is counted once (%lu)", which,
          a.refusals - b.refusals);
    CHECK(fa.fx_proc_fails == 1,
          "%s: and the capability failure is counted exactly once (%lu)",
          which, fa.fx_proc_fails);
    CHECK(fa.fx_proc_fails_main == (unsigned long) (miss_main != NULL ? 1 : 0)
          && fa.fx_proc_fails_aux == (unsigned long) (miss_aux != NULL ? 1 : 0),
          "%s: under the CONTEXT that could not supply its set, which one "
          "number could not say (%lu main, %lu aux)", which,
          fa.fx_proc_fails_main, fa.fx_proc_fails_aux);
    CHECK(a.ar_proc_fails - b.ar_proc_fails == 1,
          "%s: and the mode's own resolve-failure count rose with it (%lu)",
          which, a.ar_proc_fails - b.ar_proc_fails);
    /* THE MAIN-CONTEXT FAILURE IS THE CHEAPEST ONE THERE IS: it happens before
     * the shared acquire, so no second context is ever created. The
     * auxiliary-context failure happens after, so one is created and must be
     * destroyed again with the main context verified current. */
    if (miss_main != NULL)
        CHECK(tf.aux_create_calls == 0L && tf.aux_exists == 0,
              "%s: the MAIN context's set is resolved BEFORE anything exists, "
              "so no auxiliary context was created at all (%ld/%d)", which,
              tf.aux_create_calls, tf.aux_exists);
    else
        CHECK(tf.aux_created == 1L && tf.aux_destroyed == 1L
              && tf.aux_exists == 0,
              "%s: the context that could not supply the set was destroyed "
              "again, with the main context verified current (%ld/%ld/%d)",
              which, tf.aux_created, tf.aux_destroyed, tf.aux_exists);
    CHECK(a.ar_lost == 0 && tf.fatal_calls == 0L,
          "%s: neither shape loses the main context - each is RECOVERABLE "
          "(%lu/%ld)", which, a.ar_lost, tf.fatal_calls);
    CHECK(tf.ctx_cur == CTX_MAIN,
          "%s: the MAIN context is current after the refusal (%d)", which,
          tf.ctx_cur);
    CHECK(tf.v_ptr_wrong_context == 0L,
          "%s: and the module did NOT reach for the other context's pointer, "
          "which is the fall back no count would notice (%ld)", which,
          tf.v_ptr_wrong_context);
    CHECK(fa.fx_leaks == 0 && fa.fx_fatal_drops == 0 && tf.sync_live == 0L,
          "%s: nothing is held and nothing was abandoned (%lu/%lu/%ld)", which,
          fa.fx_leaks, fa.fx_fatal_drops, tf.sync_live);
    /* AND THE LATCH: every later served draw is refused without trying again. */
    {
        const long creates = tf.aux_create_calls;
        CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == -1,
              "%s: the next served draw is refused too", which);
        CHECK(tf.aux_create_calls == creates,
              "%s: and no second creation was even attempted (%ld)", which,
              tf.aux_create_calls - creates);
    }
    tf.withhold_fence_main = NULL;
    tf.withhold_fence_aux = NULL;
}

/* A CREATION THAT ANSWERS 0, ON EITHER SIDE. OpenGL 4.6 section 4.1 allows
 * FenceSync to fail and answer zero, and a zero handle is not something any
 * later call may be given - so each side refuses, and the two refuse at very
 * different points: the input fence fails before the switch with nothing to
 * clean up, and the output fence fails with the prepass already issued and this
 * context's state still to put back. */
static void fx_create_shape(const char *which, int zero_aux)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct sl_bgtf_fence_counters fa;
    GLuint vbo;
    int s, sl2, bound = 0;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    if (zero_aux) tf.fence_zero_aux = 1;
    else          tf.fence_zero_main = 1;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    fa = fsnap();
    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(tf.finish_calls == 0L,
          "%s: and no drain was issued as a fall back (%ld)", which,
          tf.finish_calls);
    CHECK(a.ar_writes == b.ar_writes && a.ar_rebinds == b.ar_rebinds,
          "%s: no write is claimed and the pool is NOT re-bound on the main "
          "context (%lu/%lu)", which, a.ar_writes - b.ar_writes,
          a.ar_rebinds - b.ar_rebinds);
    CHECK(a.ar_cpu_draws == b.ar_cpu_draws
          && a.ar_gpu_submits == b.ar_gpu_submits,
          "%s: and neither arm's own count rose (%lu/%lu)", which,
          a.ar_cpu_draws - b.ar_cpu_draws,
          a.ar_gpu_submits - b.ar_gpu_submits);
    /* AND THE SLOT THIS BRACKET KEYED IS UNBINDABLE FROM EVERY CALLER, which is
     * what makes "a GPU-source draw is refused when a handoff is unproved" a
     * measurement rather than a sentence. */
    for (sl2 = 0; sl2 < 8; sl2++) if (sl_bgtf_bind_clip(sl2) >= 0) bound++;
    CHECK(bound == 0,
          "%s: NO slot can be bound after this refusal - the key is installed "
          "and the pool is not (%d bindable)", which, bound);
    CHECK(fa.fx_leaks == 0 && tf.sync_live == 0L
          && tf.v_sync_double_delete == 0L,
          "%s: nothing is held, nothing leaked and nothing was deleted twice "
          "(%lu/%ld/%ld)", which, fa.fx_leaks, tf.sync_live,
          tf.v_sync_double_delete);
    CHECK(a.ar_lost == 0 && tf.fatal_calls == 0L,
          "%s: and the refusal is RECOVERABLE - the main context was never "
          "lost (%lu/%ld)", which, a.ar_lost, tf.fatal_calls);
    CHECK(tf.ctx_cur == CTX_MAIN,
          "%s: with the MAIN context current, so the caller can draw from the "
          "CPU (%d)", which, tf.ctx_cur);
    if (zero_aux) {
        CHECK(fa.fx_out_fences == 1 && fa.fx_out_fence_fails == 1
              && fa.fx_out_waits == 0 && fa.fx_out_proved == 0,
              "%s: the OUTPUT creation was attempted, failed, and no wait "
              "followed it (%lu/%lu/%lu/%lu)", which, fa.fx_out_fences,
              fa.fx_out_fence_fails, fa.fx_out_waits, fa.fx_out_proved);
        CHECK(fa.fx_in_proved == 1 && fa.fx_in_waits == 1,
              "%s: while the INPUT handoff had already held, which is what "
              "makes the two independently attributable (%lu/%lu)", which,
              fa.fx_in_proved, fa.fx_in_waits);
        CHECK(fa.fx_unproved_refusals == 1,
              "%s: and the refusal is recorded as an UNPROVED handoff and not "
              "as a GL rejection (%lu)", which, fa.fx_unproved_refusals);
        CHECK(a.ar_gl_errors == b.ar_gl_errors,
              "%s: GL rejected nothing, which is the shape a verdict drain "
              "could not have told apart from this one (%lu)", which,
              a.ar_gl_errors - b.ar_gl_errors);
        /* AND THE PREPASS DID RUN AND THAT CONTEXT WAS STILL RESTORED. The
         * refusal is deferred past the restoration deliberately, so this
         * context is left at its entry defaults on this path as on every
         * other. */
        CHECK(tf.draws_on_aux == 1L,
              "%s: the real prepass HAD been issued on the auxiliary context "
              "(%ld)", which, tf.draws_on_aux);
        CHECK(tf.current == 0u && tf.discard == 0 && tf.tf_buf == 0u
              && tf.vao_bound == 0u,
              "%s: and that context was still put back to its entry defaults "
              "before the refusal was taken (prog %u discard %d tfb %u vao %u)",
              which, (unsigned) tf.current, tf.discard, (unsigned) tf.tf_buf,
              (unsigned) tf.vao_bound);
        CHECK(a.ar_restores - b.ar_restores == 1,
              "%s: with the switch home counted as a restore (%lu)", which,
              a.ar_restores - b.ar_restores);
    } else {
        CHECK(fa.fx_in_fences == 1 && fa.fx_in_fence_fails == 1
              && fa.fx_in_flushes == 0 && fa.fx_in_waits == 0,
              "%s: the INPUT creation was attempted, failed, and neither the "
              "flush nor the wait followed it (%lu/%lu/%lu/%lu)", which,
              fa.fx_in_fences, fa.fx_in_fence_fails, fa.fx_in_flushes,
              fa.fx_in_waits);
        CHECK(fa.fx_out_fences == 0 && fa.fx_refusals == 1,
          "%s: the output handoff was never reached and the refusal is counted "
          "once (%lu/%lu)", which, fa.fx_out_fences, fa.fx_refusals);
        CHECK(tf.aux_switch_calls == 2L,
              "%s: and the bracket made NO switch to the auxiliary context - "
              "only the acquire's own two round trips happened (%ld)", which,
              tf.aux_switch_calls);
        CHECK(tf.draws_on_aux == 0L && tf.draw_arrays == 0L,
              "%s: so no prepass ran anywhere (%ld/%ld)", which,
              tf.draws_on_aux, tf.draw_arrays);
    }
    /* AND THE LATCH. */
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == -1,
          "%s: the next served draw is refused too", which);
    CHECK(fsnap().fx_in_fences + fsnap().fx_out_fences
          == fa.fx_in_fences + fa.fx_out_fences,
          "%s: and the latch means no further creation was even attempted "
          "(%lu)", which,
          fsnap().fx_in_fences + fsnap().fx_out_fences);
    tf.fence_zero_main = 0;
    tf.fence_zero_aux = 0;
}

/* THE BRACKET'S SWITCH TO THE AUXILIARY CONTEXT FAILS WITH THE MAIN CONTEXT
 * VERIFIED CURRENT, AFTER THE INPUT FENCE WAS ALREADY CREATED. This is the one
 * recoverable path on which the module is HOLDING a sync object, so it is where
 * the lifetime rule is actually load bearing: the object must be deleted, on the
 * MAIN context, with the main context's own pointer, and exactly once. */
static void fx_switch_fail_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct sl_bgtf_fence_counters fa;
    GLuint vbo;
    long mark, aux_calls_before;
    int s;

    begin_case("auxiliary real prepass, FENCE: the bracket's switch fails with "
               "an input fence already created - the object is deleted on the "
               "MAIN context, exactly once, and nothing leaks");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "the walk is admitted");
    tf.aux_switch_fail = 1;
    /* THE ACQUIRE'S OWN TWO ROUND TRIPS COME FIRST, so the gate has to bite on
     * the THIRD switch - the one inside the bracket. */
    tf.aux_switch_fail_after = 2L;
    b = snap();
    mark = tf.nlog;
    aux_calls_before = tf.calls_on_aux;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    fa = fsnap();
    CHECK(s == -1, "the served draw is REFUSED (%d)", s);
    CHECK(a.ar_switch_fails - b.ar_switch_fails == 1,
          "under the switch-failure count and not another shape's (%lu)",
          a.ar_switch_fails - b.ar_switch_fails);
    CHECK(fa.fx_in_fences == 1 && fa.fx_in_flushes == 1 && fa.fx_in_waits == 0,
          "the fence and the producer's flush had both been issued; the wait "
          "never was, because the consumer context was never reached "
          "(%lu/%lu/%lu)", fa.fx_in_fences, fa.fx_in_flushes, fa.fx_in_waits);
    CHECK(tf.sync_created == 1L && tf.sync_deleted == 1L && tf.sync_live == 0L,
          "the object that WAS created is deleted: nothing is live (%ld/%ld/%ld)",
          tf.sync_created, tf.sync_deleted, tf.sync_live);
    CHECK(fa.fx_deletes == 1 && fa.fx_deletes_main == 1
          && fa.fx_deletes_aux == 0,
          "and the delete was issued on the MAIN context, which is the only "
          "one this path can name - the refusal states that NOTHING was issued "
          "on the other (%lu/%lu/%lu)", fa.fx_deletes, fa.fx_deletes_main,
          fa.fx_deletes_aux);
    /* AND NOT ONE FENCE COMMAND LANDED ON THE AUXILIARY CONTEXT, which is the
     * form the claim has to take: `calls_on_aux` also carries the acquire's own
     * visit, which happens lazily inside this same served draw, so a delta over
     * it would be comparing the bracket against the acquire. The three
     * per-context fence counts are the bracket's alone, and all three are
     * zero. */
    CHECK(tf.delete_calls_aux == 0L && tf.wait_calls_aux == 0L
          && tf.flush_calls_aux == 0L && tf.fence_calls_aux == 0L,
          "and NOT ONE fence command of any kind landed on the auxiliary "
          "context - the refusal states that nothing was issued there, and a "
          "delete issued there would make it false (%ld deletes, %ld waits, "
          "%ld flushes, %ld fences)", tf.delete_calls_aux, tf.wait_calls_aux,
          tf.flush_calls_aux, tf.fence_calls_aux);
    CHECK(aux_calls_before == 0L && tf.calls_on_aux > 0L,
          "and the four zeros above are a statement about the BRACKET rather "
          "than about a context nothing ever reached: the acquire's own lazy "
          "visit happened inside this same served draw and did call there "
          "(%ld before, %ld after)", aux_calls_before, tf.calls_on_aux);
    {
        const struct sl_bgtfgl_call *d = first_of(mark, TFGL_DELETESYNC);
        CHECK(d != NULL && d->ctx == CTX_MAIN && d->a == tf.last_fence,
              "the delete record names the handle the creation answered and is "
              "stamped with the MAIN context (%s a %ld ctx %d, created %ld)",
              d != NULL ? kind_name(d->kind) : "(none)", d != NULL ? d->a : -1,
              d != NULL ? d->ctx : -99, tf.last_fence);
    }
    CHECK(fa.fx_leaks == 0 && fa.fx_fatal_drops == 0,
          "nothing leaked and nothing was abandoned (%lu/%lu)", fa.fx_leaks,
          fa.fx_fatal_drops);
    CHECK(tf.v_sync_double_delete == 0L && tf.v_sync_unknown == 0L
          && tf.v_ptr_wrong_context == 0L,
          "the object was deleted exactly once, by a handle this shim minted, "
          "through the pointer of the context that was current (%ld/%ld/%ld)",
          tf.v_sync_double_delete, tf.v_sync_unknown, tf.v_ptr_wrong_context);
    CHECK(a.ar_lost == 0 && tf.fatal_calls == 0L && tf.ctx_cur == CTX_MAIN,
          "and the refusal is RECOVERABLE, with the main context current "
          "(%lu/%ld/%d)", a.ar_lost, tf.fatal_calls, tf.ctx_cur);
    CHECK(a.ar_writes == b.ar_writes,
          "no write is claimed (%lu)", a.ar_writes - b.ar_writes);
    end_case();
}

/* AN ERROR RAISED BY THE CONSUMER'S WAIT ITSELF. glWaitSync returns nothing, so
 * the only thing that can speak for it is the error state - and the bracket
 * already reads that, on the right context, after everything it issued there.
 * This case proves that the wait is INSIDE that verdict's reach: the flag is
 * injected at the wait and the bracket's existing rejection path refuses,
 * without one extra read and without ever touching the MAIN context's error
 * state. */
static void fx_wait_error_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct sl_bgtf_fence_counters fa;
    GLuint vbo;
    int s, sl2, bound = 0;

    begin_case("auxiliary real prepass, FENCE: an error raised by the "
               "auxiliary context's own wait is caught by the bracket's "
               "existing verdict drain, refuses the served draw, and leaves "
               "the MAIN context's error state untouched");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "the walk is admitted");
    /* THE FLAG IS RAISED BY THE WAIT AND BY NOTHING ELSE, which is what makes
     * the refusal attributable to it: the arrival drain has already run and
     * found the state empty by the time the wait is issued. */
    tf.inject_error_on = TFGL_WAITSYNC;
    tf.inject_error_after = 0L;
    tf.inject_error_code = GL_INVALID_VALUE;
    tf.inject_error_count = 1;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    fa = fsnap();
    CHECK(s == -1, "the served draw is REFUSED (%d)", s);
    CHECK(a.ar_gl_errors - b.ar_gl_errors == 1
          && a.ar_gl_error_refusals - b.ar_gl_error_refusals == 1,
          "the verdict drain found it and refused: this is a GL REJECTION and "
          "is counted as one (%lu/%lu)", a.ar_gl_errors - b.ar_gl_errors,
          a.ar_gl_error_refusals - b.ar_gl_error_refusals);
    CHECK(a.ar_gl_first_error == (unsigned long) GL_INVALID_VALUE,
          "with GL's own first code reported (0x%lX)", a.ar_gl_first_error);
    CHECK(a.ar_gl_pre_errors == b.ar_gl_pre_errors
          && a.ar_gl_pre_refusals == b.ar_gl_pre_refusals,
          "and the ARRIVAL census is untouched - the flag belongs to a command "
          "this bracket issued (%lu/%lu)",
          a.ar_gl_pre_errors - b.ar_gl_pre_errors,
          a.ar_gl_pre_refusals - b.ar_gl_pre_refusals);
    CHECK(tf.get_error_on_main == 0L,
          "the MAIN context's error state was NEVER read, so no error of the "
          "ordinary rendering path was consumed (%ld)", tf.get_error_on_main);
    /* AND THE READS ARE ALL ON THAT CONTEXT AND ALL FROM THE TWO EXISTING
     * DRAINS. Three: the arrival drain's one clear answer, and the verdict
     * drain's two - the injected code and then the clear answer that proves the
     * state was emptied. The WAIT ITSELF ADDS NO READ, which is the property
     * this case exists for: the fence path is inside the reach of a verdict the
     * bracket already took, so it costs no further round trip and no read on
     * the main context. */
    CHECK(tf.get_error_on_aux == 3L,
          "exactly three reads happened, all on the auxiliary context: the "
          "arrival drain's clear answer and the verdict drain's finding plus "
          "its own clear answer - the wait needs NO read of its own (%ld)",
          tf.get_error_on_aux);
    CHECK(tf.get_error_calls == 3L,
          "and the run's whole read count is those three (%ld)",
          tf.get_error_calls);
    CHECK(a.ar_writes == b.ar_writes && a.ar_rebinds == b.ar_rebinds,
          "no write is claimed and the pool is NOT re-bound (%lu/%lu)",
          a.ar_writes - b.ar_writes, a.ar_rebinds - b.ar_rebinds);
    for (sl2 = 0; sl2 < 8; sl2++) if (sl_bgtf_bind_clip(sl2) >= 0) bound++;
    CHECK(bound == 0,
          "and NO slot can be bound: a GPU-source draw cannot read this pool "
          "(%d bindable)", bound);
    CHECK(tf.sync_live == 0L && fa.fx_leaks == 0 && fa.fx_fatal_drops == 0,
          "both objects this bracket created were still retired - the rejection "
          "path deletes what it holds (%ld/%lu/%lu)", tf.sync_live,
          fa.fx_leaks, fa.fx_fatal_drops);
    CHECK(tf.sync_created == 2L && tf.sync_deleted == 2L,
          "two created, two deleted (%ld/%ld)", tf.sync_created,
          tf.sync_deleted);
    CHECK(a.ar_lost == 0 && tf.fatal_calls == 0L && tf.ctx_cur == CTX_MAIN,
          "and the rejection is RECOVERABLE with the main context current "
          "(%lu/%ld/%d)", a.ar_lost, tf.fatal_calls, tf.ctx_cur);
    tf.inject_error_on = 0;
    end_case();
}

/* A LOST CONTEXT WHILE A SYNC OBJECT IS HELD. After that verdict no GL command
 * of any kind may be issued - a delete as much as a draw - so the object is
 * ABANDONED and counted apart. `where` picks which of the two holds: 0 is the
 * switch TO the auxiliary context establishing neither, with the input fence
 * held; 1 is the switch home after the prepass ran, with the output fence
 * held. */
static void fx_lost_shape(const char *which, int where)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters a;
    struct sl_bgtf_fence_counters fa;
    GLuint vbo;
    long calls_at_fatal;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    if (where == 0) {
        tf.aux_switch_unknown = 1;
        tf.aux_switch_unknown_after = 2L;
    } else {
        tf.main_switch_fail = 1;
        /* THE ACQUIRE'S OWN TWO SWITCHES HOME MUST SUCCEED, so the gate bites
         * on the THIRD: the bracket's, after the output was written. */
        tf.main_switch_fail_after = 2L;
    }
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    fa = fsnap();
    calls_at_fatal = tf.calls_after_fatal;
    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(tf.fatal_calls == 1L,
          "%s: the process was ended exactly once, through the one funnel "
          "(%ld)", which, tf.fatal_calls);
    CHECK(calls_at_fatal == 0L,
          "%s: and NO GL command of any kind followed it - a glDeleteSync as "
          "much as a draw (%ld)", which, calls_at_fatal);
    CHECK(fa.fx_fatal_drops == 1,
          "%s: the sync object this bracket held was ABANDONED and counted "
          "apart, which is the correct action rather than a defect (%lu)",
          which, fa.fx_fatal_drops);
    CHECK(fa.fx_leaks == 0,
          "%s: and it is NOT counted as a leak - the process is ending and a "
          "delete is exactly as forbidden as a draw (%lu)", which,
          fa.fx_leaks);
    /* AND THE SHIM STILL HOLDS EXACTLY THAT ONE, which is what 'abandoned'
     * means from the other side. The totals differ by which path this is: the
     * input-side loss happens with ONE object ever created and none retired,
     * while the output-side loss happens after the input handoff completed in
     * full - created and deleted on the auxiliary context - so two were created
     * and one was retired. Counting both ways is the point: an abandoned object
     * must be the ONLY one outstanding, never a second leak hidden behind it. */
    CHECK(tf.sync_created == (where == 1 ? 2L : 1L)
          && tf.sync_deleted == (where == 1 ? 1L : 0L)
          && tf.sync_created - tf.sync_deleted == 1L,
          "%s: the shim still holds exactly ONE object and no more (%ld "
          "created, %ld deleted)", which, tf.sync_created, tf.sync_deleted);
    CHECK(a.ar_lost == 1 && a.ar_fatals == 1,
          "%s: and the mode records the loss once (%lu/%lu)", which, a.ar_lost,
          a.ar_fatals);
    CHECK(a.ar_writes == 0,
          "%s: no write is claimed, whatever is in the pool (%lu)", which,
          a.ar_writes);
    if (where == 1) {
        CHECK(a.ar_sync_refusals == 1,
              "%s: the output's visibility here was never established, which "
              "is what distinguishes this from a switch that failed before any "
              "work (%lu)", which, a.ar_sync_refusals);
        CHECK(fa.fx_out_fences == 1 && fa.fx_out_waits == 0,
              "%s: the output fence existed and no wait was ever taken on it "
              "(%lu/%lu)", which, fa.fx_out_fences, fa.fx_out_waits);
    } else {
        CHECK(fa.fx_in_fences == 1 && fa.fx_in_waits == 0,
              "%s: the input fence existed and no wait was ever taken on it "
              "(%lu/%lu)", which, fa.fx_in_fences, fa.fx_in_waits);
    }
    tf.aux_switch_unknown = 0;
    tf.main_switch_fail = 0;
}

/* THE SELECTOR'S GRAMMAR ON AN ARMED LAUNCH, MEASURED THROUGH A SERVED DRAW.
 *
 * WHY A DRAW AND NOT JUST THE ACCESSOR. sl_bgtf_ar_sync answers the EFFECTIVE
 * mode, and FENCE is effective only once both contexts have supplied their sets
 * - which happens in the lazy acquire, inside the first served draw. A launch
 * that asked for FENCE and had not yet drawn therefore answers FINISH, and that
 * is the right answer rather than a gap: nothing has established that this
 * process can issue a fence at all. So the case drives one bracket and then
 * reads both the mode and what the bracket actually issued.
 *
 * AND AN UNREADABLE TOKEN IS THE SAME CASE WITH A DIFFERENT EXPECTATION, which
 * is what makes "a mistyped request gets the measured path" a comparison: the
 * runner drives this twice, and the FINISH run must issue the two drains and no
 * fence at all. */
static void fx_token_case(const char *want)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_fence_counters fa;
    const int fence = strcmp(want, "FENCE") == 0;
    GLuint vbo;
    int s;

    begin_case("the fence selector's grammar on an ARMED launch, read through "
               "one served draw: an unreadable token keeps the ORIGINAL two "
               "drains and never silently obtains the path that was not "
               "measured");
    arm();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    /* THE FENCE SURFACE IS SUPPLIED ON BOTH RUNS, so a FINISH answer here is
     * the SELECTOR'S doing and not a capability failure - which is the only way
     * the two runs differ in exactly one thing. */
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "the walk is admitted");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s >= 0, "the served draw is ACCEPTED under either token (%d)", s);
    fa = fsnap();
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), want) == 0,
          "the EFFECTIVE synchronisation mode is %s (%s)", want,
          sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(fa.fx_asked == (unsigned long) (fence ? 1 : 0),
          "and the request is recorded as%s made, which is what separates "
          "'asked for' from 'in force' (%lu)", fence ? "" : " NOT",
          fa.fx_asked);
    CHECK(fa.fx_proc_fails == 0,
          "no capability failure was involved on either run, so the mode is "
          "the SELECTOR'S doing (%lu)", fa.fx_proc_fails);
    if (fence) {
        CHECK(tf.finish_calls == 0L && tf.sync_created == 2L
              && tf.flush_calls == 2L,
              "FENCE: no drain, two sync objects, two producer flushes "
              "(%ld/%ld/%ld)", tf.finish_calls, tf.sync_created,
              tf.flush_calls);
    } else {
        CHECK(tf.finish_calls == 2L && tf.sync_created == 0L
              && tf.flush_calls == 0L,
              "FINISH: the ORIGINAL two drains, no sync object and no flush - "
              "byte for byte the sequence the mode had before this selector "
              "existed (%ld/%ld/%ld)", tf.finish_calls, tf.sync_created,
              tf.flush_calls);
        CHECK(tf.proc_calls_on_aux == 13L,
              "and only THIRTEEN names were resolved on the auxiliary context, "
              "not sixteen: an unreadable token resolves no fence entry point "
              "(%ld)", tf.proc_calls_on_aux);
        CHECK(fa.fx_finish_omits == 0 && fa.fx_in_fences == 0
              && fa.fx_out_fences == 0,
              "with the whole fence census still zero (%lu/%lu/%lu)",
              fa.fx_finish_omits, fa.fx_in_fences, fa.fx_out_fences);
    }
    CHECK(fa.fx_leaks == 0 && fa.fx_fatal_drops == 0 && tf.sync_live == 0L,
          "and nothing is held on either run (%lu/%lu/%ld)", fa.fx_leaks,
          fa.fx_fatal_drops, tf.sync_live);
    end_case();
}

/* THE SELECTOR'S OWN GRAMMAR, AND WHAT AN OFF LAUNCH PAYS FOR IT. Its own
 * launch, because the key is parsed once per process. */
static void fx_selector_case(const char *want)
{
    struct sl_bgtf_fence_counters fa;

    begin_case("the fence selector's grammar and its cost on a launch that did "
               "not arm an arm, one setting per launch");
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), want) == 0,
          "the synchronisation mode answers %s for this launch's settings (%s)",
          want, sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    fa = fsnap();
    CHECK(fa.fx_in_fences == 0 && fa.fx_out_fences == 0 && fa.fx_deletes == 0
          && fa.fx_finish_omits == 0 && fa.fx_proc_fails == 0
          && fa.fx_refusals == 0 && fa.fx_leaks == 0,
          "and nothing in the fence census moved by deciding it "
          "(%lu/%lu/%lu/%lu/%lu/%lu/%lu)", fa.fx_in_fences, fa.fx_out_fences,
          fa.fx_deletes, fa.fx_finish_omits, fa.fx_proc_fails, fa.fx_refusals,
          fa.fx_leaks);
    CHECK(tf.calls == 0L && tf.proc_calls == 0L,
          "deciding it made no GL call and resolved nothing (%ld/%ld)",
          tf.calls, tf.proc_calls);
    CHECK(tf.sync_created == 0L && tf.flush_calls == 0L
          && tf.finish_calls == 0L,
          "no sync object, no flush and no drain (%ld/%ld/%ld)",
          tf.sync_created, tf.flush_calls, tf.finish_calls);
    end_case();
}

/* A CAPABILITY, VERIFICATION OR SWITCH FAILURE REFUSES THE SERVED DRAW AND NEVER
 * FALLS BACK TO THE MAIN CONTEXT. Each shape runs in its own synthetic context,
 * because the acquire latches per context, and each must leave the prepass
 * UNRUN on both contexts and the slot UNWRITTEN - a draw on the main context
 * would be the ordinary candidate path reported under this mode's name, and a
 * written slot would let a raster read a pool whose visibility was never
 * established.
 *
 *   `no real set`      one of the three names this mode alone needs does not
 *                      resolve on the auxiliary context, so it is destroyed
 *                      again and nothing runs
 *   `unshared program` glIsProgram answers FALSE there through this mode's own
 *                      visit
 *   `no array object`  that context will not give the mode one
 *   `unshared input`   the per-bracket glIsBuffer answers FALSE for the INPUT
 *                      buffer, which is the FIRST question asked
 *   `unshared pool`    the same, for the OUTPUT pool, the SECOND question - the
 *                      two failed independently, which one flag could not do
 *   `the switch fails` the bracket's own switch fails with the MAIN context
 *                      verified current, so the ordinary restoration runs and
 *                      the refusal is the one late one */
static void ar_fail_shape(const char *which, int provide_set,
                          const char *withhold_on_aux, long prog_false_after,
                          int vao_zero, long isbuf_false_after,
                          int switch_fail)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    long draws_before, finish_before;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = provide_set;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    /* THE FAULT IS INJECTED AFTER THE PREPASS PROGRAM WAS BUILT, so what fails
     * is this mode's own acquire or bracket and not the module's capability. */
    tf.withhold_proc_on_aux = withhold_on_aux;
    /* THE SHARED ACQUIRE ASKS glIsProgram FIRST AND THIS MODE RE-ASKS IT, on the
     * same context and in that order, so failing the SECOND is what exercises
     * THIS mode's own check: a flag would fail the first and the refusal would
     * belong to the shared acquire instead. */
    tf.is_program_false_after = prog_false_after;
    tf.gen_vao_zero = vao_zero;
    tf.is_buffer_false = isbuf_false_after >= 0;
    tf.is_buffer_false_after = isbuf_false_after;
    tf.aux_switch_fail = switch_fail;
    /* THE ACQUIRE'S OWN ROUND TRIPS COME FIRST, so a bracket-level switch
     * failure has to let those through: the gate bites on the third switch, the
     * one at the prepass. */
    tf.aux_switch_fail_after = switch_fail ? 2L : -1L;
    b = snap();
    draws_before = tf.draw_arrays;
    finish_before = tf.finish_calls;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(tf.draw_arrays == draws_before,
          "%s: and NO prepass ran - not on the auxiliary context and not on the "
          "MAIN one, which would be the ordinary candidate path (%ld)", which,
          tf.draw_arrays - draws_before);
    CHECK(a.ar_prepasses == b.ar_prepasses && a.ar_vertices == b.ar_vertices
          && a.ar_writes == b.ar_writes,
          "%s: and NOTHING claims a prepass, a vertex or a completed write",
          which);
    CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices
          && a.binds == b.binds,
          "%s: nor any of the candidate's own work (%lu/%lu/%lu)", which,
          a.prepasses - b.prepasses, a.vertices - b.vertices,
          a.binds - b.binds);
    CHECK(a.refusals - b.refusals == 1,
          "%s: the module counts the refused draw exactly once (%lu)", which,
          a.refusals - b.refusals);
    CHECK(a.ar_lost == 0 && tf.fatal_calls == 0L,
          "%s: and NONE of these shapes loses the main context - each is a "
          "RECOVERABLE refusal (%lu/%ld)", which, a.ar_lost, tf.fatal_calls);
    CHECK(tf.ctx_cur == CTX_MAIN,
          "%s: the MAIN context is current after the refusal, which is what "
          "lets the caller draw from the CPU (%d)", which, tf.ctx_cur);
    CHECK(tf.v_call_no_context == 0L && tf.v_ptr_wrong_context == 0L,
          "%s: no GL call was made with no context current, and no pointer on a "
          "context other than its own (%ld/%ld)", which, tf.v_call_no_context,
          tf.v_ptr_wrong_context);
    /* AND THE SLOT, IF ONE WAS KEYED, IS UNBINDABLE. The two bracket-level
     * shapes key a slot and mark it unwritten; the acquire-level ones never
     * reach the cache at all. */
    if (isbuf_false_after >= 0 || switch_fail) {
        CHECK(a.ar_setups - b.ar_setups == 1,
              "%s: the bracket is counted as REACHED, which is what makes the "
              "inequality with ar_prepasses the record of the failure (%lu)",
              which, a.ar_setups - b.ar_setups);
        CHECK(a.ar_cpu_draws == b.ar_cpu_draws
              && a.ar_gpu_submits == b.ar_gpu_submits,
              "%s: and neither arm's own count rose (%lu/%lu)", which,
              a.ar_cpu_draws - b.ar_cpu_draws,
              a.ar_gpu_submits - b.ar_gpu_submits);
        /* AND THE SLOT THIS BRACKET KEYED IS UNBINDABLE, FROM ANY CALLER. This
         * is the one assertion that makes "nothing is drawn from unwritten
         * output" a measurement: the bracket reached the cache, so a key IS
         * installed, and what must be false is that the pool behind it can be
         * bound. Every slot is asked, because the prepass returned -1 and so
         * named none. */
        {
            int sl2;
            int bound = 0;
            struct sl_bgtf_counters bb = snap();
            for (sl2 = 0; sl2 < 8; sl2++)
                if (sl_bgtf_bind_clip(sl2) >= 0) bound++;
            CHECK(bound == 0,
                  "%s: NO slot can be bound after this refusal - the key is "
                  "installed and the pool is not (%d bindable)", which, bound);
            CHECK(snap().binds == bb.binds,
                  "%s: and the refusals cost no bind (%lu)", which,
                  snap().binds - bb.binds);
        }
    } else {
        CHECK(a.ar_create_fails - b.ar_create_fails == 1
              && a.ar_setups == b.ar_setups,
              "%s: the failure is counted as the acquire's and no bracket was "
              "reached (%lu/%lu)", which, a.ar_create_fails - b.ar_create_fails,
              a.ar_setups - b.ar_setups);
        CHECK(tf.aux_exists == 0,
              "%s: and no auxiliary context survives the refusal (%d)", which,
              tf.aux_exists);
        CHECK(tf.finish_calls == finish_before,
              "%s: nor was any synchronisation point added (%ld)", which,
              tf.finish_calls - finish_before);
    }
    /* AND THE SHAPE'S OWN COUNTER ROSE, NOT ANOTHER'S. The shapes are
     * deliberately not interchangeable: a mode that reported an unshared buffer
     * as a switch failure would be telling a reader to look at the wrong
     * thing. */
    CHECK(a.ar_proc_fails - b.ar_proc_fails
          == (unsigned long) (withhold_on_aux != NULL ? 1 : 0),
          "%s: the resolve-failure count is the shape's own (%lu)", which,
          a.ar_proc_fails - b.ar_proc_fails);
    CHECK(a.ar_prog_fails - b.ar_prog_fails
          == (unsigned long) ((prog_false_after >= 0 && withhold_on_aux == NULL)
                              ? 1 : 0),
          "%s: and so is the unshared-program count (%lu)", which,
          a.ar_prog_fails - b.ar_prog_fails);
    CHECK(a.ar_vao_fails - b.ar_vao_fails
          == (unsigned long) ((vao_zero && withhold_on_aux == NULL
                               && prog_false_after < 0) ? 1 : 0),
          "%s: and so is the array-object count (%lu)", which,
          a.ar_vao_fails - b.ar_vao_fails);
    CHECK(a.ar_buf_fails - b.ar_buf_fails
          == (unsigned long) (isbuf_false_after >= 0 ? 1 : 0),
          "%s: and so is the unshared-object count (%lu)", which,
          a.ar_buf_fails - b.ar_buf_fails);
    CHECK(a.ar_switch_fails - b.ar_switch_fails
          == (unsigned long) (switch_fail ? 1 : 0),
          "%s: and so is the switch-failure count (%lu)", which,
          a.ar_switch_fails - b.ar_switch_fails);
    /* AND EVERY LATER SERVED DRAW IS REFUSED TOO, which is what the latch is
     * for: one unusable context must not pay a creation attempt per mesh. */
    {
        const long creates = tf.aux_create_calls;
        const int s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
        CHECK(s2 == -1, "%s: the next served draw is refused as well (%d)",
              which, s2);
        if (!switch_fail && isbuf_false_after < 0)
            CHECK(tf.aux_create_calls == creates,
                  "%s: and no second context creation was even attempted (%ld)",
                  which, tf.aux_create_calls - creates);
    }
}

/* THE SWITCH BACK THAT FAILS AFTER THE OUTPUT WAS WRITTEN. Its own launch,
 * because the lost latch belongs to the PROCESS by design.
 *
 * THIS IS THE ONE SHAPE WITH NO RECOVERABLE READING. The real prepass ran, the
 * pool holds whatever the transform wrote, and the process can no longer say
 * which context a command would reach - so the bracket's restoration must NOT
 * run, the slot must stay UNWRITTEN whatever is in the pool, and the process
 * ends. ar_sync_refusals is what distinguishes it from a switch that failed
 * before any work: the second leaves no output, this one leaves output whose
 * visibility here was never established. */
static void ar_sync_lost_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s;

    begin_case("auxiliary real prepass: a switch back that fails AFTER the "
               "transform wrote leaves the slot UNWRITTEN, issues nothing "
               "further on either context, reports the run INVALID and ends the "
               "process");
    arm();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    /* THE ACQUIRE'S OWN TWO SWITCHES HOME MUST SUCCEED, so the gate bites on
     * the THIRD: the one after the prepass has run. */
    tf.main_switch_fail = 1;
    tf.main_switch_fail_after = 2L;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    CHECK(s == -1, "the served draw is REFUSED (%d)", s);
    CHECK(a.ar_prepasses - b.ar_prepasses == 1
          && a.ar_vertices - b.ar_vertices == (unsigned long) NV,
          "the real prepass DID run and DID submit its vertices - this shape is "
          "not a refusal before the work (%lu/%lu)",
          a.ar_prepasses - b.ar_prepasses, a.ar_vertices - b.ar_vertices);
    CHECK(a.ar_writes == b.ar_writes && a.ar_rebinds == b.ar_rebinds,
          "and NOTHING is recorded as a completed, visible write (%lu/%lu)",
          a.ar_writes - b.ar_writes, a.ar_rebinds - b.ar_rebinds);
    CHECK(a.ar_sync_refusals - b.ar_sync_refusals == 1,
          "the failure is counted as a SYNCHRONISATION refusal, apart from a "
          "switch that failed before any work (%lu)",
          a.ar_sync_refusals - b.ar_sync_refusals);
    CHECK(a.ar_switch_fails == b.ar_switch_fails,
          "and NOT as that (%lu)", a.ar_switch_fails - b.ar_switch_fails);
    CHECK(a.ar_lost - b.ar_lost == 1 && a.ar_fatals - b.ar_fatals == 1,
          "the run is the DIAGNOSTIC-INVALID outcome and the terminate path was "
          "taken (%lu/%lu)", a.ar_lost - b.ar_lost, a.ar_fatals - b.ar_fatals);
    CHECK(tf.fatal_calls == 1L && tf.fatal_code == 95,
          "exactly once, with the diagnostic's own census number as the exit "
          "status (%ld/%d)", tf.fatal_calls, tf.fatal_code);
    CHECK(tf.calls_after_fatal == 0L,
          "and NO GL call of any kind followed the point at which the product "
          "would have stopped existing - not this mode's, not the bracket's "
          "restoration, not the caller's (%ld)", tf.calls_after_fatal);
    CHECK(tf.v_call_no_context == 0L,
          "so no call was made with no context current either (%ld)",
          tf.v_call_no_context);
    /* AND THE SLOT, IF THE FIXTURE'S INTERCEPTED TERMINATE LET THE BRACKET KEY
     * ONE, IS UNBINDABLE. In the product the terminate does not return and this
     * is unreachable; it is checked because the fixture makes it reachable and
     * the module must then still be right. */
    CHECK(sl_bgtf_bind_clip(0) == -1 && sl_bgtf_bind_clip(1) == -1,
          "no slot can be bound after this outcome");
    end_case();
}

/* GL REJECTING A COMMAND OF THE REAL PREPASS. One shape per launch, because the
 * capability latches per context and the report is once per launch.
 *
 * WHAT MAKES THIS DIFFERENT FROM EVERY OTHER REFUSAL SHAPE ABOVE. Those are
 * failures this process can see for itself: a switch that answered 0, a name
 * glIsBuffer called false, a context that would not resolve a pointer. This one
 * is a failure only the DRIVER knows about. Every call was made, every call
 * returned, the completion wait completed - and the transform did not run,
 * because the driver refused one of the commands. glFinish cannot tell the two
 * apart; the error state is the only thing that can.
 *
 * `on` IS THE COMMAND THE SYNTHETIC DRIVER REJECTS, named by transcript kind, so
 * each launch asks about one point in the sequence: the state setup, the feedback
 * begin, the real nonzero draw, or the scope's close. The injected flag is sticky
 * exactly as GL's is, so where in the sequence it was raised changes nothing
 * about the outcome - which is the property being asserted, not an accident of
 * one case.
 *
 * WHAT MUST HOLD IN EVERY SHAPE: the output pool is never bound on the main
 * context, nothing is rasterised from it, the slot is UNWRITTEN and unbindable
 * from any caller, the served draw is refused to the CPU path, the auxiliary
 * context is left restored and the main context is verified current, the failure
 * is counted once and reported once with the driver's own first code, and the
 * census does not claim a completed write. */
static void ar_gl_error_shape(const char *which, int on, long after,
                              long code, int count)
{
    struct sl_bgtf_counters b, a;
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    long draws_before, mark;
    int s, i;

    arm();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    /* THE FAULT IS ARMED AFTER THE PROGRAM WAS BUILT AND AFTER THE ACQUIRE'S
     * OWN VISIT would have run, so what is rejected is a command of the
     * bracket's prepass and not of the module's capability setup. */
    tf.inject_error_on = on;
    tf.inject_error_after = after;
    tf.inject_error_code = code;
    tf.inject_error_count = count;
    b = snap();
    draws_before = tf.draw_arrays;
    mark = tf.nlog;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();

    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(a.ar_gl_errors - b.ar_gl_errors == 1,
          "%s: the rejection is counted exactly once (%lu)", which,
          a.ar_gl_errors - b.ar_gl_errors);
    CHECK(a.ar_gl_error_refusals - b.ar_gl_error_refusals == 1,
          "%s: and so is the refusal it caused - the two are equal by "
          "construction and are kept apart so a reader can see it (%lu)", which,
          a.ar_gl_error_refusals - b.ar_gl_error_refusals);
    CHECK(a.ar_gl_first_error == (unsigned long) (code != 0 ? code
                                                  : GL_INVALID_OPERATION),
          "%s: and the DRIVER'S OWN first code is recorded, not a substitute "
          "(0x%lX)", which, a.ar_gl_first_error);
    CHECK(a.ar_gl_drain_caps == b.ar_gl_drain_caps,
          "%s: the bounded drain reached a clear state, so the bound was not "
          "the thing that ended it (%lu)", which,
          a.ar_gl_drain_caps - b.ar_gl_drain_caps);
    /* THE WHOLE POINT: NO OUTPUT BIND AND NO RASTERISATION FROM THE POOL. */
    CHECK(a.ar_writes == b.ar_writes && a.ar_rebinds == b.ar_rebinds,
          "%s: NOTHING is recorded as a completed write and the pool was NOT "
          "re-bound on the main context (%lu/%lu)", which,
          a.ar_writes - b.ar_writes, a.ar_rebinds - b.ar_rebinds);
    /* AND THAT IS THE CALLS AND NOT THE COUNT. A counter left unraised beside a
     * bind that WAS issued would read as a satisfied refusal, so the transcript
     * is asked directly: after the last record on the auxiliary context there
     * must be no bind of a nonzero buffer name on the MAIN context. The healthy
     * path puts exactly one there - the pool, bound so the reading context sees
     * the completed write - and this path must put none. */
    {
        long i2, last_aux = -1, binds_after = 0;
        for (i2 = mark; i2 < tf.nlog; i2++)
            if (tf.log[i2].ctx == CTX_AUX) last_aux = i2;
        CHECK(last_aux >= 0, "%s: the bracket did reach the auxiliary context "
              "(%ld)", which, last_aux);
        for (i2 = last_aux + 1; i2 > 0 && i2 < tf.nlog; i2++)
            if (tf.log[i2].kind == TFGL_BINDBUFFER && tf.log[i2].ctx == CTX_MAIN
                && tf.log[i2].a == GL_ARRAY_BUFFER && tf.log[i2].b != 0)
                binds_after++;
        CHECK(binds_after == 0,
              "%s: and NOT ONE bind of a buffer name was issued on the MAIN "
              "context after the switch home - the visibility re-bind the "
              "healthy path makes is exactly what a rejected transform must not "
              "get (%ld)", which, binds_after);
    }
    CHECK(a.ar_cpu_draws == b.ar_cpu_draws
          && a.ar_gpu_submits == b.ar_gpu_submits,
          "%s: and neither arm's own count rose, so no pool is reported as one "
          "a raster may read (%lu/%lu)", which,
          a.ar_cpu_draws - b.ar_cpu_draws, a.ar_gpu_submits - b.ar_gpu_submits);
    CHECK(a.prepasses == b.prepasses && a.vertices == b.vertices
          && a.binds == b.binds,
          "%s: and the candidate's own work counts are untouched - a run "
          "claiming prepasses GL refused would be telling a reader something "
          "false (%lu/%lu/%lu)", which, a.prepasses - b.prepasses,
          a.vertices - b.vertices, a.binds - b.binds);
    /* AND NO SLOT CAN BE BOUND, from ANY caller. The bracket reached the cache,
     * so a KEY is installed; what must be false is that the storage behind it
     * can be bound and rasterised. */
    {
        int bound = 0;
        const struct sl_bgtf_counters bb = snap();
        for (i = 0; i < 8; i++) if (sl_bgtf_bind_clip(i) >= 0) bound++;
        CHECK(bound == 0,
              "%s: NO slot is bindable after the rejection - the key is "
              "installed and the output is not (%d bindable)", which, bound);
        CHECK(snap().binds == bb.binds,
              "%s: and asking cost no bind (%lu)", which,
              snap().binds - bb.binds);
    }
    /* THE ISSUED/COMPLETED DISTINCTION, WHICH IS WHAT THE CENSUS MUST GET
     * RIGHT. The draw WAS issued - the module cannot know it was refused until
     * it reads the error state afterwards - so ar_prepasses rises, while
     * ar_writes does not. That inequality is the record of the rejection, and it
     * is the one thing a census may not smooth over. */
    CHECK(a.ar_prepasses - b.ar_prepasses == 1
          && a.ar_setups - b.ar_setups == 1,
          "%s: the draw was ISSUED and the bracket is counted as reached (%lu/"
          "%lu)", which, a.ar_prepasses - b.ar_prepasses,
          a.ar_setups - b.ar_setups);
    CHECK(a.ar_prepasses != a.ar_writes,
          "%s: and ISSUED is not COMPLETED - the inequality IS the record of "
          "what GL refused (%lu vs %lu)", which, a.ar_prepasses, a.ar_writes);
    CHECK(tf.draw_arrays - draws_before == 1L && tf.draws_on_aux == 1L,
          "%s: with that one draw issued on the AUXILIARY context and none on "
          "the main one (%ld/%ld)", which, tf.draw_arrays - draws_before,
          tf.draws_on_aux);
    /* CLEANUP: the auxiliary context is back at its entry defaults and the main
     * context is current. The verdict read is placed AFTER the restoration for
     * exactly this reason - the error path has no cleanup left to do. */
    CHECK(tf.ctx_cur == CTX_MAIN,
          "%s: the MAIN context is current after the refusal, which is what "
          "lets the caller draw from the CPU (%d)", which, tf.ctx_cur);
    CHECK(a.ar_switches - b.ar_switches == 1
          && a.ar_restores - b.ar_restores == 1,
          "%s: the switch and the verified switch home are counted once each "
          "(%lu/%lu)", which, a.ar_switches - b.ar_switches,
          a.ar_restores - b.ar_restores);
    CHECK(tf.vao_bound == 0u && tf.current == 0u && tf.discard == 0
          && tf.tf_buf == 0u && tf.arr_buf == 0u && tf.tf_active == 0,
          "%s: and that context was RESTORED before the refusal - no array "
          "object bound, no program current, discard off, no indexed binding "
          "and no open feedback scope (%u/%u/%d/%u/%u/%d)", which,
          (unsigned) tf.vao_bound, (unsigned) tf.current, tf.discard,
          (unsigned) tf.tf_buf, (unsigned) tf.arr_buf, tf.tf_active);
    CHECK(tf.v_tf_unbalanced == 0L && tf.v_tf_nested == 0L,
          "%s: the feedback scope was matched even on this path (%ld/%ld)",
          which, tf.v_tf_unbalanced, tf.v_tf_nested);
    /* RECOVERABLE, NOT TERMINAL. */
    CHECK(a.ar_lost == 0 && a.ar_fatals == 0 && tf.fatal_calls == 0L,
          "%s: this is a RECOVERABLE refusal - the main context was never lost "
          "and the terminate path was not taken (%lu/%lu/%ld)", which,
          a.ar_lost, a.ar_fatals, tf.fatal_calls);
    CHECK(a.ar_sync_refusals == b.ar_sync_refusals
          && a.ar_switch_fails == b.ar_switch_fails
          && a.ar_buf_fails == b.ar_buf_fails,
          "%s: and it is NOT reported as any of the other shapes - each sends a "
          "reader to look at a different thing (%lu/%lu/%lu)", which,
          a.ar_sync_refusals - b.ar_sync_refusals,
          a.ar_switch_fails - b.ar_switch_fails,
          a.ar_buf_fails - b.ar_buf_fails);
    /* THE READS: two on the auxiliary context, none on the main one. */
    CHECK(tf.get_error_on_main == 0L,
          "%s: not one error-state read landed on the MAIN context, so the "
          "ordinary path's error state was never consumed by this check (%ld)",
          which, tf.get_error_on_main);
    CHECK(tf.get_error_on_aux == tf.get_error_calls && tf.get_error_calls >= 2L,
          "%s: every read was on the auxiliary context, and the drain read "
          "until the state was clear (%ld of %ld)", which, tf.get_error_on_aux,
          tf.get_error_calls);
    CHECK(a.ar_gl_pre_errors == b.ar_gl_pre_errors,
          "%s: and nothing is attributed to a PRE-EXISTING error: the state was "
          "empty when the bracket arrived, which is what makes the verdict this "
          "bracket's own (%lu)", which,
          a.ar_gl_pre_errors - b.ar_gl_pre_errors);
    /* AND THE LATCH: one rejecting context does not pay a rejected bracket per
     * mesh, and the refusal is not reported again. */
    {
        const long reads = tf.get_error_calls;
        const int s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
        CHECK(s2 == -1, "%s: the next served draw is refused as well (%d)",
              which, s2);
        CHECK(tf.get_error_calls == reads,
              "%s: without reaching the context again at all - the capability "
              "LATCHED (%ld)", which, tf.get_error_calls - reads);
        CHECK(snap().ar_gl_errors == a.ar_gl_errors,
              "%s: so the rejection is still counted exactly once (%lu)", which,
              snap().ar_gl_errors);
    }
}

/* A DRIVER THAT WILL NOT COME BACK: every read answers a nonzero code and the
 * flag is never retired, which is the one shape in which the drain's BOUND is
 * what ends the loop. Driven at the bracket's START, where the consequence is
 * strongest: the error state could not be emptied, so this bracket's own verdict
 * could never have been attributed to it, and the mode refuses BEFORE writing a
 * single byte of state on that context. */
static void ar_gl_drain_cap_case(void)
{
    struct sl_bgtf_counters b, a;
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    long draws_before, calls_aux_before;
    int s, i;

    begin_case("auxiliary real prepass: an error state that cannot be emptied "
               "stops the drain at its BOUND rather than spinning, refuses "
               "before anything is bound or issued there, and leaves the slot "
               "unwritten");
    arm();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    /* THE FLAG IS RAISED BY THE ACQUIRE'S OWN glGenVertexArrays - the one call
     * this mode makes on that context OUTSIDE any bracket - and is never
     * retired, so the bracket's OPENING drain is what meets a state it cannot
     * empty. `inject_error_stuck` is what makes a read answer without clearing,
     * which is the implementation the bound exists for. A kind the bracket
     * itself issues would be met by the VERDICT drain instead, which is a
     * different claim. */
    tf.inject_error_on = TFGL_GENVAO;
    tf.inject_error_after = 0L;
    tf.inject_error_code = GL_OUT_OF_MEMORY;
    tf.inject_error_count = 1;
    tf.inject_error_stuck = 1;
    b = snap();
    draws_before = tf.draw_arrays;
    calls_aux_before = tf.calls_on_aux;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();

    CHECK(s == -1, "the served draw is REFUSED (%d)", s);
    CHECK(a.ar_gl_drain_caps - b.ar_gl_drain_caps == 1,
          "the drain is recorded as having reached its BOUND (%lu)",
          a.ar_gl_drain_caps - b.ar_gl_drain_caps);
    CHECK(tf.get_error_calls - 0L <= 9L && tf.get_error_calls >= 8L,
          "and it stopped after the bound's eight reads rather than spinning - "
          "one per error code GL 4.6 defines, which is every flag a conformant "
          "implementation can hold (%ld)", tf.get_error_calls);
    CHECK(tf.get_error_on_main == 0L,
          "every one of them on the auxiliary context and NONE on the main one "
          "(%ld)", tf.get_error_on_main);
    CHECK(a.ar_gl_errors - b.ar_gl_errors == 1
          && a.ar_gl_error_refusals - b.ar_gl_error_refusals == 1,
          "an unreadable error state is counted as an ERROR and never as a "
          "clean state - it cannot say a command was accepted (%lu/%lu)",
          a.ar_gl_errors - b.ar_gl_errors,
          a.ar_gl_error_refusals - b.ar_gl_error_refusals);
    CHECK(a.ar_gl_first_error == (unsigned long) GL_OUT_OF_MEMORY,
          "with the driver's own code recorded (0x%lX)", a.ar_gl_first_error);
    /* AND NOTHING IS CLAIMED AS A RETIRED PRE-EXISTING STATE. The codes this
     * drain saw are a PREFIX of a state it could not read to the end, not "what
     * was already flagged, now retired" - so counting them under
     * ar_gl_pre_errors would claim an attribution that was never established,
     * and the notice that says such codes are not a refusal would contradict
     * the refusal this shape makes. */
    CHECK(a.ar_gl_pre_errors == b.ar_gl_pre_errors
          && a.ar_gl_first_pre_error == 0,
          "and NOTHING is recorded as a retired pre-existing state - a capped "
          "drain establishes no attribution to claim one with (%lu/0x%lX)",
          a.ar_gl_pre_errors - b.ar_gl_pre_errors, a.ar_gl_first_pre_error);
    /* AND NOTHING WAS DONE THERE AT ALL. This refusal is before the object
     * checks, so the bracket issued the drain and then left. */
    CHECK(a.ar_buf_checks == b.ar_buf_checks,
          "not even the two object checks were asked - the refusal is BEFORE "
          "them (%lu)", a.ar_buf_checks - b.ar_buf_checks);
    CHECK(tf.draw_arrays == draws_before && a.ar_prepasses == b.ar_prepasses
          && a.ar_vertices == b.ar_vertices,
          "no prepass draw was issued on either context and nothing claims a "
          "vertex (%ld/%lu/%lu)", tf.draw_arrays - draws_before,
          a.ar_prepasses - b.ar_prepasses, a.ar_vertices - b.ar_vertices);
    CHECK(tf.bind_vao == 0L,
          "and this mode's array object was never BOUND - the bracket left "
          "before it wrote a single piece of that context's state (%ld)",
          tf.bind_vao);
    CHECK(tf.calls_on_aux > calls_aux_before,
          "the bracket did reach that context, which is what makes the "
          "assertions above about it rather than about a bracket that never "
          "arrived (%ld)", tf.calls_on_aux - calls_aux_before);
    CHECK(a.ar_writes == b.ar_writes && a.ar_rebinds == b.ar_rebinds,
          "nothing is a completed write and the pool was never re-bound "
          "(%lu/%lu)", a.ar_writes - b.ar_writes, a.ar_rebinds - b.ar_rebinds);
    {
        int bound = 0;
        for (i = 0; i < 8; i++) if (sl_bgtf_bind_clip(i) >= 0) bound++;
        CHECK(bound == 0, "and NO slot is bindable (%d)", bound);
    }
    CHECK(tf.ctx_cur == CTX_MAIN,
          "the MAIN context is verified current again (%d)", tf.ctx_cur);
    CHECK(a.ar_restores - b.ar_restores == 1,
          "which is counted as this bracket's restore (%lu)",
          a.ar_restores - b.ar_restores);
    CHECK(a.ar_lost == 0 && tf.fatal_calls == 0L,
          "this is a RECOVERABLE refusal (%lu/%ld)", a.ar_lost,
          tf.fatal_calls);
    end_case();
}

/* A REJECTION AND A LOST MAIN CONTEXT TOGETHER. The stronger outcome has to win:
 * once which context a command would reach is UNKNOWN, no further GL call may be
 * made at all - not the refusal's, not the bracket's restoration, not the
 * caller's - so the rejection's own recoverable path must give way to the one
 * terminal funnel. */
static void ar_gl_error_lost_case(void)
{
    struct sl_bgtf_counters b, a;
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    int s;

    begin_case("auxiliary real prepass: GL rejecting a command AND the main "
               "context then unverifiable goes to the one terminal funnel, with "
               "no GL call of any kind after it");
    arm();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    tf.inject_error_on = TFGL_DRAWARRAYS;
    tf.inject_error_after = 0L;
    tf.inject_error_code = GL_INVALID_OPERATION;
    tf.inject_error_count = 1;
    /* THE ACQUIRE'S OWN TWO SWITCHES HOME MUST SUCCEED, so the gate bites on the
     * THIRD: the one the rejection path takes. */
    tf.main_switch_fail = 1;
    tf.main_switch_fail_after = 2L;
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();

    CHECK(s == -1, "the served draw is REFUSED (%d)", s);
    CHECK(a.ar_gl_errors - b.ar_gl_errors == 1,
          "the rejection was seen and counted (%lu)",
          a.ar_gl_errors - b.ar_gl_errors);
    CHECK(a.ar_lost - b.ar_lost == 1 && a.ar_fatals - b.ar_fatals == 1,
          "and the LOST outcome wins: the run is reported INVALID and the "
          "terminate path was taken (%lu/%lu)", a.ar_lost - b.ar_lost,
          a.ar_fatals - b.ar_fatals);
    CHECK(tf.fatal_calls == 1L && tf.fatal_code == 95,
          "exactly once, with the diagnostic's own census number as the exit "
          "status (%ld/%d)", tf.fatal_calls, tf.fatal_code);
    CHECK(tf.calls_after_fatal == 0L,
          "and NO GL call of any kind followed the point at which the product "
          "would have stopped existing (%ld)", tf.calls_after_fatal);
    CHECK(a.ar_writes == b.ar_writes && a.ar_rebinds == b.ar_rebinds,
          "nothing is a completed write and the pool was never re-bound "
          "(%lu/%lu)", a.ar_writes - b.ar_writes, a.ar_rebinds - b.ar_rebinds);
    CHECK(sl_bgtf_bind_clip(0) == -1 && sl_bgtf_bind_clip(1) == -1,
          "and no slot can be bound after this outcome");
    CHECK(tf.get_error_on_main == 0L,
          "the error state was read only on the auxiliary context, here too "
          "(%ld)", tf.get_error_on_main);
    end_case();
}

/* AN ERROR THAT WAS ALREADY THERE WHEN THE BRACKET ARRIVED, WHICH REFUSES.
 *
 * WHAT THIS SHAPE ESTABLISHES. The flags are raised by the ACQUIRE'S own
 * glGenVertexArrays - the one call this mode makes on that context OUTSIDE any
 * bracket, so "already flagged when a bracket arrives" is driven and not
 * simulated - and the bracket's opening drain is what meets them. The outcome is
 * a refusal with its own attribution: counted under ar_gl_pre_errors and
 * ar_gl_pre_refusals rather than under the verdict's census, reported under its
 * own latch, and taken BEFORE a single byte of that context's state is written.
 *
 * WHY A REFUSAL IS THE RIGHT OUTCOME, THE ATTRIBUTION NOTWITHSTANDING. This
 * context exists for this diagnostic and nothing else touches it, so a flag
 * found on arrival came from that context's creation, from this mode's
 * capability acquire, or from an earlier visit of this mode's own. Whichever it
 * was, the context's health is UNPROVED from that point - the code may be a
 * GL_CONTEXT_LOST, which says the context is gone - and a bracket that continued
 * could report a completed write from a context that had already signalled a
 * fault. Retiring the codes is still what the drain does, so what is reported is
 * a statement about ARRIVAL and is never carried into the verdict.
 *
 * AND THE LATCH IS WHAT MAKES THAT AFFORDABLE. Refusing without latching would
 * cost one stale flag a full round trip per served mesh for the rest of the
 * launch. The first finding latches, so the cost is ONE bracket's switch and
 * drain and every later served draw goes to the CPU path without reaching that
 * context at all - which is asserted here as a count of GL calls and not as a
 * claim.
 *
 * `fatal` DRIVES THE STRONGER OUTCOME: the switch home after the refusal is made
 * unverifiable, so the one terminal funnel must win and no GL call of any kind
 * may follow it. */
static void ar_gl_pre_error_shape(const char *which, long code, int count,
                                  int fatal)
{
    struct sl_bgtf_counters b, a;
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    long draws_before, mark;
    int s, i;

    arm();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    tf.inject_error_on = TFGL_GENVAO;
    tf.inject_error_after = 0L;
    tf.inject_error_code = code;
    tf.inject_error_count = count;
    if (fatal) {
        /* THE ACQUIRE'S OWN TWO SWITCHES HOME MUST SUCCEED, so the gate bites on
         * the THIRD: the one this refusal takes. */
        tf.main_switch_fail = 1;
        tf.main_switch_fail_after = 2L;
    }
    b = snap();
    draws_before = tf.draw_arrays;
    mark = tf.nlog;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();

    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(a.ar_gl_pre_errors - b.ar_gl_pre_errors == (unsigned long) count,
          "%s: every code the opening drain retired is counted, under the count "
          "that names them as the bracket's INHERITANCE and not its own (%lu of "
          "%d)", which, a.ar_gl_pre_errors - b.ar_gl_pre_errors, count);
    CHECK(a.ar_gl_pre_refusals - b.ar_gl_pre_refusals == 1,
          "%s: and the refusal it caused is counted exactly once, apart from the "
          "verdict's own refusals (%lu)", which,
          a.ar_gl_pre_refusals - b.ar_gl_pre_refusals);
    CHECK(a.ar_gl_first_pre_error == (unsigned long) code,
          "%s: with GL's OWN first code recorded, not a substitute (0x%lX)",
          which, a.ar_gl_first_pre_error);
    /* THE ATTRIBUTION IS SEPARATE, which is the half of this a single refusal
     * count would lose: a reader must be able to tell a context that ARRIVED
     * unhealthy from one whose driver rejected this mode's own command. */
    CHECK(a.ar_gl_errors == b.ar_gl_errors
          && a.ar_gl_error_refusals == b.ar_gl_error_refusals
          && a.ar_gl_first_error == 0,
          "%s: and NOTHING is attributed to this bracket's own commands - the "
          "verdict's census is untouched (%lu/%lu/0x%lX)", which,
          a.ar_gl_errors - b.ar_gl_errors,
          a.ar_gl_error_refusals - b.ar_gl_error_refusals,
          a.ar_gl_first_error);
    CHECK(a.ar_gl_drain_caps == b.ar_gl_drain_caps,
          "%s: and the drain reached a clear state, so the BOUND was not what "
          "ended it - this is the found-an-error shape and not the unreadable "
          "one (%lu)", which, a.ar_gl_drain_caps - b.ar_gl_drain_caps);
    /* NOTHING WAS BOUND, ENABLED, ISSUED OR WRITTEN THERE. The refusal is taken
     * before the two object checks, so the bracket drained and left. */
    CHECK(a.ar_buf_checks == b.ar_buf_checks,
          "%s: not even the two object checks were asked - the refusal is BEFORE "
          "them (%lu)", which, a.ar_buf_checks - b.ar_buf_checks);
    CHECK(tf.bind_vao == 0L && tf.begin_tf == 0L && tf.end_tf == 0L,
          "%s: this mode's array object was never BOUND and no feedback scope "
          "was ever opened or closed (%ld/%ld/%ld)", which, tf.bind_vao,
          tf.begin_tf, tf.end_tf);
    CHECK(tf.draw_arrays == draws_before && tf.draws_on_aux == 0L
          && a.ar_prepasses == b.ar_prepasses
          && a.ar_vertices == b.ar_vertices,
          "%s: NO prepass draw was issued on either context and nothing claims "
          "a vertex (%ld/%ld/%lu/%lu)", which, tf.draw_arrays - draws_before,
          tf.draws_on_aux, a.ar_prepasses - b.ar_prepasses,
          a.ar_vertices - b.ar_vertices);
    CHECK(a.ar_writes == b.ar_writes && a.ar_rebinds == b.ar_rebinds,
          "%s: NOTHING is recorded as a completed write and the pool was NOT "
          "re-bound on the main context (%lu/%lu)", which,
          a.ar_writes - b.ar_writes, a.ar_rebinds - b.ar_rebinds);
    /* AND THAT IS THE CALLS AND NOT THE COUNT, on the same reasoning the verdict
     * shape records: a counter left unraised beside a bind that WAS issued would
     * read as a satisfied refusal, so the transcript is asked directly. */
    {
        long i2, last_aux = -1, binds_after = 0;
        for (i2 = mark; i2 < tf.nlog; i2++)
            if (tf.log[i2].ctx == CTX_AUX) last_aux = i2;
        CHECK(last_aux >= 0,
              "%s: the bracket did reach the auxiliary context, which is what "
              "makes these assertions about it rather than about a bracket that "
              "never arrived (%ld)", which, last_aux);
        for (i2 = last_aux + 1; i2 > 0 && i2 < tf.nlog; i2++)
            if (tf.log[i2].kind == TFGL_BINDBUFFER && tf.log[i2].ctx == CTX_MAIN
                && tf.log[i2].a == GL_ARRAY_BUFFER && tf.log[i2].b != 0)
                binds_after++;
        CHECK(binds_after == 0,
              "%s: and NOT ONE bind of a buffer name was issued on the MAIN "
              "context after the bracket left that one (%ld)", which,
              binds_after);
    }
    CHECK(a.ar_cpu_draws == b.ar_cpu_draws
          && a.ar_gpu_submits == b.ar_gpu_submits,
          "%s: and neither arm's own count rose, so no pool is reported as one "
          "a raster may read (%lu/%lu)", which,
          a.ar_cpu_draws - b.ar_cpu_draws, a.ar_gpu_submits - b.ar_gpu_submits);
    {
        int bound = 0;
        for (i = 0; i < 8; i++) if (sl_bgtf_bind_clip(i) >= 0) bound++;
        CHECK(bound == 0,
              "%s: and NO slot is bindable from any caller (%d bindable)", which,
              bound);
    }
    CHECK(tf.get_error_on_main == 0L,
          "%s: not one error-state read landed on the MAIN context, so the "
          "ordinary path's error state was never consumed (%ld)", which,
          tf.get_error_on_main);
    CHECK(tf.get_error_calls == (long) count + 1L
          && tf.get_error_on_aux == tf.get_error_calls,
          "%s: the opening drain read until the state was clear - one read per "
          "code plus the clear answer that ended it - and every read was on the "
          "auxiliary context; there is NO verdict read, because the bracket "
          "issued nothing to render a verdict on (%ld of %ld)", which,
          tf.get_error_on_aux, tf.get_error_calls);
    if (fatal) {
        CHECK(a.ar_lost - b.ar_lost == 1 && a.ar_fatals - b.ar_fatals == 1,
              "%s: the STRONGER outcome wins - the run is reported INVALID and "
              "the terminate path was taken (%lu/%lu)", which,
              a.ar_lost - b.ar_lost, a.ar_fatals - b.ar_fatals);
        CHECK(tf.fatal_calls == 1L && tf.fatal_code == 95,
              "%s: exactly once, with the diagnostic's own census number as the "
              "exit status (%ld/%d)", which, tf.fatal_calls, tf.fatal_code);
        CHECK(tf.calls_after_fatal == 0L,
              "%s: and NO GL call of any kind followed the point at which the "
              "product would have stopped existing (%ld)", which,
              tf.calls_after_fatal);
        return;
    }
    /* RECOVERABLE, AND NOT REPORTED AS ANY OTHER SHAPE. */
    CHECK(tf.ctx_cur == CTX_MAIN,
          "%s: the MAIN context is verified current again, which is what lets "
          "the caller draw from the CPU (%d)", which, tf.ctx_cur);
    CHECK(a.ar_switches - b.ar_switches == 1
          && a.ar_restores - b.ar_restores == 1,
          "%s: the switch and the VERIFIED switch home are counted once each "
          "(%lu/%lu)", which, a.ar_switches - b.ar_switches,
          a.ar_restores - b.ar_restores);
    CHECK(a.ar_lost == 0 && a.ar_fatals == 0 && tf.fatal_calls == 0L,
          "%s: this is a RECOVERABLE refusal - the main context was never lost "
          "and the terminate path was not taken (%lu/%lu/%ld)", which,
          a.ar_lost, a.ar_fatals, tf.fatal_calls);
    CHECK(a.ar_sync_refusals == b.ar_sync_refusals
          && a.ar_switch_fails == b.ar_switch_fails
          && a.ar_buf_fails == b.ar_buf_fails,
          "%s: and it is NOT reported as any of the other shapes - each sends a "
          "reader to look at a different thing (%lu/%lu/%lu)", which,
          a.ar_sync_refusals - b.ar_sync_refusals,
          a.ar_switch_fails - b.ar_switch_fails,
          a.ar_buf_fails - b.ar_buf_fails);
    /* AND THE LATCH, ASSERTED OVER REPEATED SERVED MESHES. Three more, each with
     * its own key, so this is a scene's worth of draws and not one repeat. */
    {
        const long reads = tf.get_error_calls;
        const long aux_calls = tf.calls_on_aux;
        const long aux_switches = tf.aux_switch_calls;
        const struct sl_bgtf_counters b2 = snap();
        struct sl_bgtf_counters a2;
        int k, refused = 0;
        for (k = 0; k < 3; k++)
            if (sl_bgtf_prepass(k == 0 ? SL_BGGPU_DEPTH_SINGLE
                                : k == 1 ? SL_BGGPU_DEPTH_FAR1
                                         : SL_BGGPU_DEPTH_FAR0, 0.0f, 0.0f) < 0)
                refused++;
        a2 = snap();
        CHECK(refused == 3,
              "%s: every one of three further served meshes is refused to the "
              "CPU path (%d of 3)", which, refused);
        CHECK(tf.aux_switch_calls == aux_switches
              && tf.calls_on_aux == aux_calls && tf.get_error_calls == reads,
              "%s: without one switch to that context, one GL call on it or one "
              "further error-state read - the capability LATCHED (%ld/%ld/%ld)",
              which, tf.aux_switch_calls - aux_switches,
              tf.calls_on_aux - aux_calls, tf.get_error_calls - reads);
        CHECK(a2.ar_gl_pre_refusals == a.ar_gl_pre_refusals
              && a2.ar_gl_pre_errors == a.ar_gl_pre_errors,
              "%s: so the finding is still counted exactly once, however many "
              "meshes follow it (%lu/%lu)", which, a2.ar_gl_pre_refusals,
              a2.ar_gl_pre_errors);
        CHECK(a2.ar_create_fails - b2.ar_create_fails == 3,
              "%s: and each later mesh is accounted where a latched capability "
              "refuses, rather than as a bracket that ran (%lu)", which,
              a2.ar_create_fails - b2.ar_create_fails);
        CHECK(a2.ar_prepasses == a.ar_prepasses && a2.ar_writes == a.ar_writes,
              "%s: no prepass and no write was claimed by any of them (%lu/%lu)",
              which, a2.ar_prepasses, a2.ar_writes);
    }
}

/* EVERY TERMINATION PATH THIS MODE CAN REACH, ONE PER LAUNCH.
 *
 * WHY THESE ARE DRIVEN SEPARATELY FROM THE REFUSALS ABOVE. The terminal funnel
 * is not a refusal: it is the one process-wide statement that nothing a launch
 * reports means anything. It is reached from the SHARED acquire's own switch,
 * from this mode's acquire, from the bracket, from the refusal paths' switches
 * home and from the BACKEND's answer to a destroy - five different places in two
 * functions written for two selectors - so a single launch cannot establish that
 * all of them attribute the outcome to the mode that was actually set. The
 * attribution is the runner's assertion, because it is about this process's own
 * stderr; what is asserted HERE is that each `where` really does terminate, that
 * it terminates exactly once, and that no GL call of any kind follows it. A
 * label check on a launch that did not terminate would be a check of nothing.
 *
 * `where` 0 the shared acquire's own switch to that context; 1 this mode's
 * acquire's switch; 2 the bracket's switch at the prepass; 3 the BACKEND
 * refusing to say a destroy was safe, reached through this mode's own refusal
 * and release. */
static void ar_lost_shape(const char *which, int where)
{
    struct sl_bgtf_counters b, a;
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    long draws_before;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    /* `where` 3 REACHES THE RELEASE THROUGH A REFUSAL: one of the three names
     * this mode alone needs does not resolve there, so the context is destroyed
     * again - and the backend will not say the destroy was safe. */
    tf.provide_real_set = (where == 3) ? 0 : 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    if (where == 3) {
        tf.withhold_proc_on_aux = "glGenVertexArrays";
        tf.aux_destroy_unknown = 1;
    } else {
        /* THE OCCURRENCE IS WHICH SWITCH, counted in the order this mode makes
         * them: the shared acquire's own is 0, this mode's acquire's is 1 and
         * the bracket's is 2. */
        tf.aux_switch_unknown = 1;
        tf.aux_switch_unknown_after = (long) where;
    }
    b = snap();
    draws_before = tf.draw_arrays;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();

    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(tf.fatal_calls == 1L && tf.fatal_code == 95,
          "%s: the terminate-now path was taken EXACTLY ONCE, with the "
          "diagnostic's own census number as the exit status (%ld/%d)", which,
          tf.fatal_calls, tf.fatal_code);
    CHECK(tf.calls_after_fatal == 0L,
          "%s: and NOT ONE GL entry point was called after the point at which "
          "the product would have stopped existing (%ld)", which,
          tf.calls_after_fatal);
    CHECK(a.ax_lost - b.ax_lost == 1,
          "%s: counted as a LOST context, which is what a reader discards the "
          "run on (%lu)", which, a.ax_lost - b.ax_lost);
    CHECK(tf.draw_arrays == draws_before,
          "%s: and NO draw was issued on either context (%ld)", which,
          tf.draw_arrays - draws_before);
    CHECK(a.ar_writes == b.ar_writes && a.ar_rebinds == b.ar_rebinds,
          "%s: nothing is a completed write and no pool was re-bound (%lu/%lu)",
          which, a.ar_writes - b.ar_writes, a.ar_rebinds - b.ar_rebinds);
    CHECK(sl_bgtf_bind_clip(0) == -1 && sl_bgtf_bind_clip(1) == -1,
          "%s: and no slot can be bound after this outcome", which);
    tf.aux_switch_unknown = 0;
    tf.aux_destroy_unknown = 0;
    tf.withhold_proc_on_aux = NULL;
}

/* A REFUSAL FROM INSIDE THE SHARED ACQUIRE, ON A LAUNCH THAT SELECTED THIS MODE.
 *
 * WHY THIS SHAPE EXISTS SEPARATELY FROM ar_fail_shape. The real-prepass mode
 * REUSES tf_ax_acquire for the probe, the create, the first switch, the resolve
 * of the two names both modes need, and the first share check - which is what
 * keeps the empty-draw control's own call sequence unchanged by this mode
 * existing. Those five refusals are therefore reported from a function written
 * for the OTHER selector, and a launch that set SL_BG_TF_AUX_REAL and hit one of
 * them was told that SL_BG_TF_AUX_EMPTY=1 had failed: a control the launch never
 * set, which makes a live refusal unattributable to the thing that caused it.
 *
 * WHAT IS ASSERTED HERE AND WHAT IS ASSERTED BY THE RUNNER. The counters, the
 * refusal and the cleanup are asserted here. The stderr TEXT is the runner's,
 * because it is this process's own stderr: the runner greps each launch for the
 * selector it set and for the ABSENCE of the other one. Both halves are needed -
 * naming the right selector is worth nothing if the older wording moved, since
 * earlier rounds' frozen evidence quotes it. */
static void ar_shared_refusal_shape(const char *which, int gate_isprog,
                                    int create_fail, int switch_fail,
                                    int share_false,
                                    const char *withhold_on_aux)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    long draws_before;
    int s;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = gate_isprog;
    tf.provide_real_set = 1;
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    tf.aux_create_fail = create_fail;
    tf.aux_create_fail_after = create_fail ? 0L : -1L;
    /* THE FIRST SWITCH IS THE SHARED ACQUIRE'S OWN, so a shape aimed at it bites
     * on occurrence 0 - which is what distinguishes it from the bracket-level
     * switch failure ar_fail_shape drives on occurrence 2. */
    tf.aux_switch_fail = switch_fail;
    tf.aux_switch_fail_after = switch_fail ? 0L : -1L;
    tf.is_program_false = share_false;
    tf.withhold_proc_on_aux = withhold_on_aux;
    b = snap();
    draws_before = tf.draw_arrays;
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();

    CHECK(s == -1, "%s: the served draw is REFUSED (%d)", which, s);
    CHECK(tf.draw_arrays == draws_before,
          "%s: and NO prepass ran on either context (%ld)", which,
          tf.draw_arrays - draws_before);
    /* THE REFUSED SERVED DRAW IS THIS MODE'S OWN COUNT, raised where this mode's
     * acquire found it had no context to use. The older control's
     * ax_create_fails is deliberately NOT raised: that counter belongs to the
     * empty-draw control's own call site, which a launch on this mode never
     * reaches - so a reader of a refused AUX_REAL launch finds the refusal under
     * ar_create_fails and the CAUSE under whichever of the shared acquire's own
     * counters the shape has. That split is the reason the stderr label matters:
     * the counters alone do not say which selector asked. */
    CHECK(a.ar_create_fails - b.ar_create_fails == 1,
          "%s: this mode counts the served draw it could not serve (%lu)",
          which, a.ar_create_fails - b.ar_create_fails);
    CHECK(a.ax_create_fails == b.ax_create_fails,
          "%s: and the OLDER control's own refusal count is untouched - its "
          "call site was never reached (%lu)", which,
          a.ax_create_fails - b.ax_create_fails);
    /* AND THE SHARED ACQUIRE'S OWN CAUSE COUNTER, where the shape has one. The
     * probe and the create refusals have none of their own - the backend and the
     * resolver have already said why on stderr - so the sum is 0 for those two
     * and exactly 1 for the other three. */
    {
        const unsigned long cause =
            (a.ax_switch_fails - b.ax_switch_fails)
            + (a.ax_proc_fails - b.ax_proc_fails)
            + (a.ax_share_fails - b.ax_share_fails);
        const unsigned long want =
            (unsigned long) ((switch_fail || share_false
                              || withhold_on_aux != NULL) ? 1 : 0);
        CHECK(cause == want,
              "%s: the shared acquire recorded this shape's own cause, and only "
              "it (%lu, expected %lu)", which, cause, want);
    }
    CHECK(a.ar_setups == b.ar_setups && a.ar_prepasses == b.ar_prepasses
          && a.ar_writes == b.ar_writes,
          "%s: no bracket was reached, nothing was issued and nothing claims a "
          "write", which);
    CHECK(a.ar_lost == 0 && tf.fatal_calls == 0L,
          "%s: this is a RECOVERABLE refusal (%lu/%ld)", which, a.ar_lost,
          tf.fatal_calls);
    CHECK(tf.ctx_cur == CTX_MAIN,
          "%s: the MAIN context is current after it, which is what lets the "
          "caller draw from the CPU (%d)", which, tf.ctx_cur);
    CHECK(tf.aux_exists == 0,
          "%s: and no auxiliary GL context survives the refusal (%d)", which,
          tf.aux_exists);
    /* AND NO ERROR STATE WAS READ. The check belongs to the bracket, which this
     * shape never reaches, so a read here would be one the mode cannot account
     * for - and on the main context it would consume the ordinary path's. */
    CHECK(tf.get_error_calls == 0L,
          "%s: and the error state was not read at all - the check is the "
          "bracket's and no bracket ran (%ld)", which, tf.get_error_calls);
    {
        int i, bound = 0;
        for (i = 0; i < 8; i++) if (sl_bgtf_bind_clip(i) >= 0) bound++;
        CHECK(bound == 0, "%s: and NO slot is bindable (%d)", which, bound);
    }
}

/* THE OFF PATH: with an OFF token, nothing of this mode happens at all. Part of
 * the DEFAULT suite, so every ordinary run of the fixture measures it.
 *
 * THE TOKEN IS WHAT SELECTS IT, and the runner sets it for every run of its
 * table. An opted-in launch that names NO arm reaches the opt-in candidate - the
 * auxiliary arm - so "absent" is no longer this path; the main-context prepass
 * these cases are about is selected by name, which is exactly the developer
 * control the candidate keeps for it. The candidate's own launches are driven
 * separately, one process each, by mode "optin". */
static void st_aux_real_off(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters a, before;
    GLuint vbo;
    int s;

    begin_case("the auxiliary real-prepass mode OFF: no second context, no "
               "entry point, no array object, no synchronisation point, and the "
               "prepass's own sequence unchanged");
    arm();
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_OFF,
          "the mode is OFF with an OFF token named (%s)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 0 && sl_bgtf_ar_optin_closed() == 0,
          "an off TOKEN is a selection, so nothing is implicit and nothing "
          "failed closed (%d/%d)", sl_bgtf_ar_implicit(),
          sl_bgtf_ar_optin_closed());
    CHECK(strcmp(sl_bgtf_ar_name(SL_BGTF_AR_OFF), "OFF") == 0
          && strcmp(sl_bgtf_ar_name(SL_BGTF_AR_CPU), "CPU_SOURCE") == 0
          && strcmp(sl_bgtf_ar_name(SL_BGTF_AR_GPU), "GPU_SOURCE") == 0
          && strcmp(sl_bgtf_ar_name(-1), "?") == 0
          && strcmp(sl_bgtf_ar_name(99), "?") == 0,
          "and the arm names are the SELECTOR TOKENS, with an out-of-range "
          "value answering \"?\" rather than an arm");
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    before = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    CHECK(s >= 0, "the served draw is accepted (%d)", s);
    a = snap();
    CHECK(tf.aux_create_calls == 0L && tf.aux_exists == 0,
          "NO auxiliary GL context was created or even asked for (%ld/%d)",
          tf.aux_create_calls, tf.aux_exists);
    CHECK(tf.aux_switch_calls == 0L && tf.main_switch_calls == 0L,
          "and no context switch was attempted in either direction (%ld/%ld)",
          tf.aux_switch_calls, tf.main_switch_calls);
    CHECK(tf.is_buffer_calls == 0L && tf.gen_vao == 0L && tf.bind_vao == 0L,
          "no object was verified, generated or bound for this mode (%ld/%ld/"
          "%ld)", tf.is_buffer_calls, tf.gen_vao, tf.bind_vao);
    CHECK(tf.finish_calls == 0L,
          "NO synchronisation point was added, which is what off means here "
          "(%ld)", tf.finish_calls);
    CHECK(tf.calls_on_aux == 0L && tf.proc_calls_on_aux == 0L,
          "and nothing was called or resolved on a second context (%ld/%ld)",
          tf.calls_on_aux, tf.proc_calls_on_aux);
    /* AND THE FENCE SELECTOR COSTS AN OFF LAUNCH NOTHING EITHER
     * (SL_BG_TF_AUX_SYNC). The key is read only from sites already behind the
     * real-prepass mode being in force, so an off launch does not read it at
     * all - which is why the answer here is FINISH without any environment
     * being set, and why no fence entry point is resolved, no sync object
     * exists and no flush is issued. The distinction matters beyond cost: a
     * flush is a real symbol in this shim now, so its absence has to be a
     * COUNT rather than a link error. */
    CHECK(sl_bgtf_ar_sync() == SL_BGTF_FX_FINISH,
          "the synchronisation mode answers FINISH on an off launch (%s)",
          sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(strcmp(sl_bgtf_fx_name(SL_BGTF_FX_FINISH), "FINISH") == 0
          && strcmp(sl_bgtf_fx_name(SL_BGTF_FX_FENCE), "FENCE") == 0
          && strcmp(sl_bgtf_fx_name(-1), "?") == 0
          && strcmp(sl_bgtf_fx_name(99), "?") == 0,
          "and the mode names are the SELECTOR TOKENS, with an out-of-range "
          "value answering \"?\" rather than a mode");
    CHECK(tf.flush_calls == 0L && tf.sync_created == 0L
          && tf.sync_deleted == 0L && tf.fence_calls_main == 0L
          && tf.fence_calls_aux == 0L && tf.wait_calls_main == 0L
          && tf.wait_calls_aux == 0L && tf.delete_calls_main == 0L
          && tf.delete_calls_aux == 0L,
          "no glFlush was issued and no sync object was created, waited on or "
          "deleted anywhere (%ld flushes, %ld created, %ld deleted)",
          tf.flush_calls, tf.sync_created, tf.sync_deleted);
    {
        const struct sl_bgtf_fence_counters *fc = sl_bgtf_fence_stats();
        CHECK(fc->fx_mode == 0 && fc->fx_asked == 0 && fc->fx_in_fences == 0
              && fc->fx_out_fences == 0 && fc->fx_in_flushes == 0
              && fc->fx_out_flushes == 0 && fc->fx_in_waits == 0
              && fc->fx_out_waits == 0 && fc->fx_in_proved == 0
              && fc->fx_out_proved == 0 && fc->fx_deletes == 0
              && fc->fx_deletes_main == 0 && fc->fx_deletes_aux == 0
              && fc->fx_leaks == 0 && fc->fx_fatal_drops == 0
              && fc->fx_refusals == 0 && fc->fx_unproved_refusals == 0
              && fc->fx_finish_omits == 0 && fc->fx_proc_fails == 0
              && fc->fx_proc_fails_main == 0 && fc->fx_proc_fails_aux == 0
              && fc->fx_in_fence_fails == 0 && fc->fx_out_fence_fails == 0,
              "and every one of the fence path's twenty-three counts is zero "
              "on an off launch");
    }
    /* AND NO ERROR STATE IS TOUCHED. The mode's error check is inside its own
     * bracket, which an off launch never reaches, so glGetError is not called
     * at all - on either context. This matters beyond "off costs nothing": a
     * read CONSUMES the flag it returns, so a check that leaked onto the default
     * path would swallow an error belonging to the ordinary rendering path and
     * leave nothing behind to say it had. */
    CHECK(tf.get_error_calls == 0L && tf.get_error_on_main == 0L
          && tf.get_error_on_aux == 0L,
          "NO error-state read of any kind was issued, so the default path's "
          "own error state is neither inspected nor CONSUMED (%ld/%ld/%ld)",
          tf.get_error_calls, tf.get_error_on_main, tf.get_error_on_aux);
    CHECK(a.ar_gl_errors == 0 && a.ar_gl_error_refusals == 0
          && a.ar_gl_pre_errors == 0 && a.ar_gl_pre_refusals == 0
          && a.ar_gl_drain_caps == 0
          && a.ar_gl_first_error == 0 && a.ar_gl_first_pre_error == 0,
          "and every one of the error check's own counts is zero "
          "(%lu/%lu/%lu/%lu/%lu/%lu/%lu)", a.ar_gl_errors,
          a.ar_gl_error_refusals, a.ar_gl_pre_errors, a.ar_gl_pre_refusals,
          a.ar_gl_drain_caps, a.ar_gl_first_error,
          a.ar_gl_first_pre_error);
    CHECK(a.ar_setups == 0 && a.ar_prepasses == 0 && a.ar_skips == 0
          && a.ar_reuses == 0 && a.ar_creates == 0 && a.ar_create_fails == 0
          && a.ar_refusals == 0 && a.ar_syncs == 0 && a.ar_mode == 0,
          "and every one of the mode's own counts is zero, the effective arm "
          "included (%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu/%lu)", a.ar_setups,
          a.ar_prepasses, a.ar_skips, a.ar_reuses, a.ar_creates,
          a.ar_create_fails, a.ar_refusals, a.ar_syncs, a.ar_mode);
    CHECK(tf.draw_arrays == 1L && tf.draw_vertices == (long) NV,
          "the ordinary prepass issued its one draw over every vertex, on the "
          "MAIN context (%ld/%ld)", tf.draw_arrays, tf.draw_vertices);
    CHECK(a.prepasses - before.prepasses == 1
          && a.vertices - before.vertices == (unsigned long) NV,
          "and claimed exactly that work - read as a DELTA, the module's "
          "census being cumulative for the process (%lu/%lu)",
          a.prepasses - before.prepasses, a.vertices - before.vertices);
    end_case();
}

/* ---- THE OPT-IN CANDIDATE (SL_BG_TF_CLIP with SL_BG_TF_AUX_REAL UNNAMED) ----
 *
 * ONE LAUNCH PER CASE, for the reason every selector case above is its own
 * launch: the arbiter decides once per process, so a second combination driven
 * in the same process would be answered from the first one's latch.
 *
 * WHAT THESE PIN, AND WHY EACH NEEDS A SERVED DRAW RATHER THAN AN ACCESSOR. The
 * question the candidate exists to answer is WHICH PREPASS RAN, and that is not
 * readable from a selector: the auxiliary arm and the main-context prepass are
 * both "the candidate" to a caller. So each case drives a bracket and reads what
 * was issued - a draw on the main context, a draw on the auxiliary one, or
 * neither - and the arm token is checked beside it rather than instead of it. */

/* THE IMPLICIT SELECTION, END TO END. An opted-in launch that named no arm must
 * reach the AUXILIARY context with FENCE, and the main context must issue no
 * prepass draw of its own: that draw is the known-mismatching path, and a launch
 * that silently took it would be indistinguishable from the candidate here. */
static void optin_implicit_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct sl_bgtf_fence_counters fa;
    GLuint vbo;
    int s;

    begin_case("the OPT-IN CANDIDATE: an opted-in launch that named no arm "
               "takes the auxiliary GPU_SOURCE arm with FENCE, and the MAIN "
               "context issues no prepass draw");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_GPU,
          "the effective arm is GPU_SOURCE without the selector being named "
          "(%s)", sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 1,
          "and it is recorded as the IMPLICIT selection, which is the only "
          "thing distinguishing it from an arm a reader named (%d)",
          sl_bgtf_ar_implicit());
    CHECK(sl_bgtf_ar_optin_closed() == 0,
          "nothing failed closed: the selector was absent, not unreadable (%d)",
          sl_bgtf_ar_optin_closed());
    CHECK(sl_bgtf_stats()->ar_refusals == 0,
          "and an unnamed arm is NOT a refusal - nothing was asked for and "
          "refused (%lu)", sl_bgtf_stats()->ar_refusals);
    CHECK(tf.aux_create_calls == 0L && tf.calls == 0L && tf.proc_calls == 0L,
          "deciding it created no context, made no GL call and resolved "
          "nothing (%ld/%ld/%ld)", tf.aux_create_calls, tf.calls,
          tf.proc_calls);
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    fa = fsnap();
    CHECK(s >= 0, "the served draw is ACCEPTED (%d)", s);
    CHECK(a.ar_prepasses - b.ar_prepasses == 1
          && a.ar_prepasses_aux - b.ar_prepasses_aux == 1,
          "the prepass ran, and it ran on the AUXILIARY context (%lu/%lu)",
          a.ar_prepasses - b.ar_prepasses, a.ar_prepasses_aux - b.ar_prepasses_aux);
    CHECK(a.ar_main_omits - b.ar_main_omits == 1,
          "and the MAIN context's own prepass draw was OMITTED, counted rather "
          "than merely absent (%lu)", a.ar_main_omits - b.ar_main_omits);
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), "FENCE") == 0,
          "the EFFECTIVE synchronisation mode is FENCE without the key being "
          "named (%s)", sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(tf.finish_calls == 0L && tf.sync_created == 2L && tf.flush_calls == 2L,
          "so the bracket spent no pipeline drain and established both handoffs "
          "with a sync object and a producer flush each (%ld/%ld/%ld)",
          tf.finish_calls, tf.sync_created, tf.flush_calls);
    CHECK(fa.fx_asked == 0,
          "while fx_asked stays 0: the candidate's FENCE is its own DEFAULT and "
          "not a request, which is what that field is about (%lu)", fa.fx_asked);
    CHECK(fa.fx_in_proved == 1 && fa.fx_out_proved == 1
          && a.ar_writes - b.ar_writes == 1,
          "both handoffs were proved and the slot is written (%lu/%lu/%lu)",
          fa.fx_in_proved, fa.fx_out_proved, a.ar_writes - b.ar_writes);
    CHECK(fa.fx_leaks == 0 && fa.fx_fatal_drops == 0 && tf.sync_live == 0L,
          "and no sync object is held afterwards (%lu/%lu/%ld)", fa.fx_leaks,
          fa.fx_fatal_drops, tf.sync_live);
    end_case();
}

/* THE DEFAULT-OFF INVARIANT. Without the opt-in switch the candidate does not
 * exist: no arm, no implicit selection, no fail-closed latch, FINISH as the
 * synchronisation answer WITHOUT the key being read, and not one GL call. This
 * is the case that would fail if the selection had been made a default. */
static void optin_default_off_case(void)
{
    struct sl_bggpu_walk w = a_walk();

    begin_case("WITHOUT THE OPT-IN SWITCH, nothing is decided until a walk "
               "asks: no arm, no implicit selection, no latch and no call");
    arm();
    /* v0.4 THIS CASE USED TO ASSERT "THE CANDIDATE IS DEFAULT OFF", INCLUDING
     * sl_bgtf_ready() == 0 - AND THAT ASSERTION WAS THE DEFECT. With the
     * OPTIMIZED RENDERING row selecting the prepass, a launch that names
     * nothing is the ORDINARY PRODUCTION launch, so "the module answers off"
     * is false of it and a readiness gate that agreed made the row's walks
     * unreachable. The row is not this module's to see; OFF is the caller not
     * asking, and mode prod row_off drives that contract.
     *
     * WHAT IS STILL TRUE AND STILL WORTH PINNING is everything BEFORE a walk
     * asks: the developer route is absent, nothing has been promoted, and
     * deciding all of it costs no GL call. So this case asks the pre-ask
     * questions and deliberately does NOT call sl_bgtf_ready or
     * sl_bgtf_begin_walk - calling them is what a row-ON walk does, and
     * mode prod row_on is where that is driven. */
    CHECK(sl_bgtf_opt_in() == 0,
          "this launch did not opt in (%d)", sl_bgtf_opt_in());
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_OFF,
          "and before any walk asks, no arm is in force (%s)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 0,
          "nothing was selected implicitly (%d)", sl_bgtf_ar_implicit());
    CHECK(sl_bgtf_production() == 0,
          "and no production selection is recorded yet (%d)",
          sl_bgtf_production());
    CHECK(sl_bgtf_ar_optin_closed() == 0,
          "and nothing failed closed (%d)", sl_bgtf_ar_optin_closed());
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), "FINISH") == 0,
          "the synchronisation answer is FINISH, reached without reading the "
          "key at all (%s)", sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(tf.calls == 0L && tf.proc_calls == 0L && tf.aux_create_calls == 0L
          && tf.draw_arrays == 0L && tf.sync_created == 0L
          && tf.finish_calls == 0L && tf.flush_calls == 0L,
          "and asking all of that issued no GL call, resolved nothing, created "
          "no context, drew nothing and synchronised nothing "
          "(%ld/%ld/%ld/%ld/%ld/%ld/%ld)", tf.calls, tf.proc_calls,
          tf.aux_create_calls, tf.draw_arrays, tf.sync_created,
          tf.finish_calls, tf.flush_calls);
    (void) w;
    end_case();
}

/* AN EXPLICIT OFF TOKEN KEEPS THE MAIN-CONTEXT PREPASS, which is the developer
 * control the measured comparison needs. The arm is OFF, nothing is implicit,
 * nothing failed closed, no second context exists - and the prepass draw IS
 * issued, on the main context. */
static void optin_explicit_off_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s;

    begin_case("an EXPLICIT off token keeps the MAIN-CONTEXT prepass: the "
               "diagnostic arm stays reachable by name and runs on this "
               "context");
    arm();
    CHECK(sl_bgtf_opt_in() == 1, "this launch opted in (%d)", sl_bgtf_opt_in());
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_OFF,
          "the arm is OFF because the launch said so (%s)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 0,
          "an off TOKEN is a selection and not an absence, so nothing is "
          "implicit (%d)", sl_bgtf_ar_implicit());
    CHECK(sl_bgtf_ar_optin_closed() == 0,
          "and an off token is not a failure, so nothing failed closed (%d)",
          sl_bgtf_ar_optin_closed());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    CHECK(s >= 0, "the served draw is ACCEPTED (%d)", s);
    CHECK(tf.draw_arrays == 1L && tf.draw_vertices == (long) NV,
          "the prepass issued its one draw over every vertex (%ld/%ld)",
          tf.draw_arrays, tf.draw_vertices);
    CHECK(a.prepasses - b.prepasses == 1 && a.ar_prepasses == b.ar_prepasses
          && a.ar_main_omits == b.ar_main_omits,
          "and it is the ORDINARY main-context prepass: the candidate's census "
          "moved and the auxiliary mode's did not (%lu/%lu/%lu)",
          a.prepasses - b.prepasses, a.ar_prepasses, a.ar_main_omits);
    CHECK(tf.aux_create_calls == 0L && tf.aux_exists == 0,
          "no auxiliary GL context was created (%ld/%d)", tf.aux_create_calls,
          tf.aux_exists);
    CHECK(tf.sync_created == 0L && tf.flush_calls == 0L,
          "and no sync object or producer flush belongs to this arm (%ld/%ld)",
          tf.sync_created, tf.flush_calls);
    end_case();
}

/* A NAMED ARM KEEPS ITS OWN DEFAULT SYNCHRONISATION. GPU_SOURCE spelled by name,
 * with no synchronisation key, must still be the ORIGINAL two drains: the
 * candidate's FENCE default belongs to the unnamed selection alone, and an A/B
 * control whose synchronisation moved because another launch's default changed
 * would not be a control. */
static void optin_explicit_named_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s;

    begin_case("a NAMED GPU_SOURCE arm with no synchronisation key keeps the "
               "ORIGINAL two drains, byte for byte as before the candidate "
               "existed");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_GPU,
          "the arm is the one the launch named (%s)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 0,
          "and it is NOT the implicit selection (%d)", sl_bgtf_ar_implicit());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    CHECK(s >= 0, "the served draw is ACCEPTED (%d)", s);
    CHECK(a.ar_prepasses_aux - b.ar_prepasses_aux == 1,
          "the prepass ran on the auxiliary context (%lu)",
          a.ar_prepasses_aux - b.ar_prepasses_aux);
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), "FINISH") == 0,
          "the effective synchronisation mode is FINISH (%s)",
          sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(tf.finish_calls == 2L && tf.sync_created == 0L
          && tf.flush_calls == 0L,
          "so the two drains were issued and no sync object or flush was "
          "(%ld/%ld/%ld)", tf.finish_calls, tf.sync_created, tf.flush_calls);
    end_case();
}

/* AN UNREADABLE ARM ON AN OPTED-IN LAUNCH FAILS CLOSED TO THE CPU RASTER PATH.
 * This is the case that would otherwise reach the main-context prepass under a
 * typo, so the assertion that matters is tf.draw_arrays: no prepass draw was
 * issued anywhere, on either context, for either served draw. */
static void optin_closed_body(const char *which, int want_ar_refusals)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s1, s2;

    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_opt_in() == 1, "%s: this launch opted in (%d)", which,
          sl_bgtf_opt_in());
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_OFF,
          "%s: the unreadable value selected nothing, so no arm is in force "
          "(%s)", which, sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_optin_closed() == 1,
          "%s: and the prepass has FAILED CLOSED (%d)", which,
          sl_bgtf_ar_optin_closed());
    CHECK(sl_bgtf_ar_implicit() == 0,
          "%s: a launch whose arm was refused is not the implicit selection "
          "either (%d)", which, sl_bgtf_ar_implicit());
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), "FINISH") == 0,
          "%s: and no fence path is half-established - the effective "
          "synchronisation answer is the off launch's own (%s)", which,
          sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(sl_bgtf_stats()->ar_refusals == (unsigned long) want_ar_refusals,
          "%s: the launch is%s counted as a refusal of a NAMED arm (%lu)",
          which, want_ar_refusals ? "" : " NOT",
          sl_bgtf_stats()->ar_refusals);
    CHECK(sl_bgtf_ready() == 1, "%s: the module is ready", which);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "%s: the walk is admitted", which);
    b = snap();
    s1 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    CHECK(s1 == -1 && s2 == -1,
          "%s: both served draws are REFUSED, not just the first (%d/%d)",
          which, s1, s2);
    CHECK(tf.draw_arrays == 0L,
          "%s: and NO prepass draw was issued on either context - the "
          "known-mismatching main-context path was never reached (%ld)", which,
          tf.draw_arrays);
    CHECK(tf.aux_create_calls == 0L && tf.aux_exists == 0,
          "%s: no auxiliary GL context was created either (%ld/%d)", which,
          tf.aux_create_calls, tf.aux_exists);
    CHECK(a.ar_skips - b.ar_skips == 2 && a.refusals - b.refusals == 2,
          "%s: each refused draw is counted once, in the mode's account and in "
          "the module's (%lu/%lu)", which, a.ar_skips - b.ar_skips,
          a.refusals - b.refusals);
    CHECK(a.prepasses == b.prepasses && a.binds == b.binds,
          "%s: nothing claims a prepass and nothing is bindable (%lu/%lu)",
          which, a.prepasses, a.binds);
    CHECK(tf.sync_created == 0L && tf.finish_calls == 0L
          && tf.flush_calls == 0L,
          "%s: and nothing was synchronised - no sync object, no drain, no "
          "flush (%ld/%ld/%ld)", which, tf.sync_created, tf.finish_calls,
          tf.flush_calls);
    CHECK(tf.proc_calls_on_aux == 0L,
          "%s: and not one entry point was resolved on an auxiliary context "
          "(%ld)", which, tf.proc_calls_on_aux);
}

/* AN UNREADABLE ARM. The refusal arrives before any prepass GL state exists, so
 * the launch rasters through the existing CPU path. */
static void optin_closed_case(void)
{
    begin_case("an UNREADABLE arm on an opted-in launch FAILS CLOSED: every "
               "served draw is refused to the existing CPU raster path and the "
               "MAIN-CONTEXT prepass is never reached");
    optin_closed_body("unreadable arm", 0);
    end_case();
}

/* AN UNREADABLE SYNCHRONISATION MODE, ON EITHER ARM. A value that is neither
 * FENCE nor FINISH selects NEITHER, so the arm it would have run is refused
 * rather than served with a mode nothing chose. `named_arm` says whether this
 * launch spelled the arm, which is the only thing that differs between the two:
 * a named arm's refusal is counted as one, an unnamed one's is not, because
 * nothing was asked for by name. */
static void optin_sync_closed_case(int named_arm)
{
    begin_case(named_arm
               ? "an UNREADABLE synchronisation value on a NAMED arm FAILS "
                 "CLOSED: the arm is refused rather than served with FINISH, "
                 "every served draw goes to the CPU raster path, and the "
                 "MAIN-CONTEXT prepass is never reached"
               : "an UNREADABLE synchronisation value on the OPT-IN CANDIDATE "
                 "FAILS CLOSED: the arm is refused rather than served with "
                 "FENCE, every served draw goes to the CPU raster path, and the "
                 "MAIN-CONTEXT prepass is never reached");
    optin_closed_body(named_arm ? "unreadable sync, named arm"
                                : "unreadable sync, candidate", named_arm);
    end_case();
}

/* THE SYNCHRONISATION MODE THE CANDIDATE ACTUALLY RUNS, for each of the three
 * supported combinations. `want` is the token that must be in force and `named`
 * whether the key chose it, so one case covers the default FENCE, an explicit
 * FENCE and an explicit FINISH - and the explicit FINISH is the launch the
 * earlier single startup sentence was false for. */
static void optin_sync_case(const char *want, int named)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct sl_bgtf_fence_counters fa;
    const int fence = strcmp(want, "FENCE") == 0;
    GLuint vbo;
    int s;

    begin_case("the OPT-IN CANDIDATE's synchronisation mode, read through one "
               "served draw: the candidate's own default is FENCE and an "
               "explicit token is honoured, each with the arm unchanged");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_GPU && sl_bgtf_ar_implicit() == 1,
          "the arm is the candidate's GPU_SOURCE and the selection is IMPLICIT "
          "whichever synchronisation mode is in force (%s/%d)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()), sl_bgtf_ar_implicit());
    CHECK(sl_bgtf_ar_optin_closed() == 0,
          "a readable token is not a refusal (%d)",
          sl_bgtf_ar_optin_closed());
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    fa = fsnap();
    CHECK(s >= 0, "the served draw is ACCEPTED (%d)", s);
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), want) == 0,
          "the EFFECTIVE synchronisation mode is %s (%s)", want,
          sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(fa.fx_asked == (unsigned long) (fence && named ? 1 : 0),
          "and the KEY is recorded as%s having selected FENCE, which is what "
          "separates a request from this candidate's default (%lu)",
          fence && named ? "" : " NOT", fa.fx_asked);
    if (fence) {
        CHECK(tf.finish_calls == 0L && tf.sync_created == 2L
              && tf.flush_calls == 2L,
              "FENCE: no drain, two sync objects, two producer flushes "
              "(%ld/%ld/%ld)", tf.finish_calls, tf.sync_created,
              tf.flush_calls);
    } else {
        CHECK(tf.finish_calls == 2L && tf.sync_created == 0L
              && tf.flush_calls == 0L,
              "FINISH: the two drains, no sync object and no flush - the "
              "candidate honours the token rather than its own default "
              "(%ld/%ld/%ld)", tf.finish_calls, tf.sync_created,
              tf.flush_calls);
    }
    CHECK(a.ar_prepasses_aux - b.ar_prepasses_aux == 1
          && a.ar_main_omits - b.ar_main_omits == 1,
          "the prepass ran on the AUXILIARY context under either mode, and the "
          "MAIN context issued none (%lu/%lu)",
          a.ar_prepasses_aux - b.ar_prepasses_aux,
          a.ar_main_omits - b.ar_main_omits);
    CHECK(a.ar_writes - b.ar_writes == 1,
          "and the slot is written (%lu)", a.ar_writes - b.ar_writes);
    CHECK(fa.fx_leaks == 0 && fa.fx_fatal_drops == 0 && tf.sync_live == 0L,
          "with nothing held afterwards (%lu/%lu/%ld)", fa.fx_leaks,
          fa.fx_fatal_drops, tf.sync_live);
    end_case();
}

/* A DIAGNOSTIC STANDS THE CANDIDATE DOWN RATHER THAN BEING REFUSED BY IT. An
 * opted-in launch that named no arm but selected a main-context diagnostic keeps
 * that diagnostic and its prepass: the candidate is not in force, nothing is
 * counted as a refusal, and no second context is created. The launch is STATED
 * rather than silent, which is a property of the output and is checked by the
 * seam check that reads the emitted lines. */
static void optin_standdown_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s;

    begin_case("a MAIN-CONTEXT DIAGNOSTIC stands the opt-in candidate down: the "
               "diagnostic keeps its own prepass and the candidate is not in "
               "force");
    arm();
    CHECK(sl_bgtf_opt_in() == 1, "this launch opted in (%d)", sl_bgtf_opt_in());
    CHECK(sl_bgtf_raster_cpu() == 1,
          "and selected the original-raster diagnostic (%d)",
          sl_bgtf_raster_cpu());
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_OFF,
          "so no auxiliary arm is in force (%s)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 0,
          "the candidate was not selected (%d)", sl_bgtf_ar_implicit());
    CHECK(sl_bgtf_ar_optin_closed() == 0,
          "and standing down is not failing closed: the diagnostic still runs "
          "(%d)", sl_bgtf_ar_optin_closed());
    CHECK(sl_bgtf_stats()->ar_refusals == 0,
          "nothing was asked for and refused (%lu)",
          sl_bgtf_stats()->ar_refusals);
    CHECK(sl_bgtf_ready() == 1, "the module is ready");
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk is admitted");
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    CHECK(s >= 0, "the served draw is ACCEPTED (%d)", s);
    CHECK(tf.draw_arrays == 1L && a.prepasses - b.prepasses == 1,
          "the diagnostic's own prepass ran on the MAIN context (%ld/%lu)",
          tf.draw_arrays, a.prepasses - b.prepasses);
    CHECK(a.ar_prepasses == b.ar_prepasses && a.ar_skips == b.ar_skips,
          "while the auxiliary mode neither ran nor refused anything "
          "(%lu/%lu)", a.ar_prepasses, a.ar_skips);
    CHECK(tf.aux_create_calls == 0L && tf.aux_exists == 0,
          "and no auxiliary GL context was created (%ld/%d)",
          tf.aux_create_calls, tf.aux_exists);
    end_case();
}

/* THE SELECTOR'S GRAMMAR, COUPLING, EXCLUSIVITY AND REFUSAL ACCOUNTING, one
 * setting per launch because the selector is parsed once per process. The runner
 * says what the mode must ANSWER and whether this launch asked for it and did
 * not get it. */
static void mode_env_aux_real(const char *want, int want_refusal)
{
    const int got = sl_bgtf_aux_real();
    begin_case("the auxiliary real-prepass selector's grammar, coupling and "
               "exclusivity, one setting per launch");
    CHECK(strcmp(sl_bgtf_ar_name(got), want) == 0,
          "the mode answers %s for this launch's settings (%s)", want,
          sl_bgtf_ar_name(got));
    CHECK(sl_bgtf_stats()->ar_refusals == (unsigned long) (want_refusal ? 1 : 0),
          "and the launch is%s counted as a refusal (%lu)",
          want_refusal ? "" : " NOT", sl_bgtf_stats()->ar_refusals);
    CHECK(sl_bgtf_stats()->ar_mode == (unsigned long) got,
          "the recorded arm is the EFFECTIVE one, not the requested one (%lu vs "
          "%d)", sl_bgtf_stats()->ar_mode, got);
    /* AND ASKING IS NOT CREATING. A refused launch, and an off one, must own no
     * second GL context - which is the fact a reader of a refused launch most
     * needs, this being one of the two modes that creates one. */
    CHECK(tf.aux_create_calls == 0L && tf.aux_exists == 0,
          "and no auxiliary GL context was created or asked for by deciding "
          "this (%ld/%d)", tf.aux_create_calls, tf.aux_exists);
    /* AND THE DECISION ITSELF COSTS NO GL CALL. */
    CHECK(tf.calls == 0L && tf.proc_calls == 0L,
          "deciding the selector made no GL call and resolved nothing (%ld/%ld)",
          tf.calls, tf.proc_calls);
    end_case();
}

/* ======================================================================== *
 * v0.4 THE PRODUCTION SELECTION: WHAT THE OPTIMIZED RENDERING ROW REACHES.
 *
 * WHAT THESE CASES CAN AND CANNOT DRIVE, STATED FIRST. The module does not read
 * the row - src/gfx/sl_gfx_dl.c latches one answer per walk and calls
 * sl_bgtf_walk_admit inside it - so these cases model the row by WHETHER THEY
 * ASK. A walk the row admitted is a call to sl_bgtf_walk_admit; a walk the row
 * refused is the absence of that call, which is exactly the caller's contract.
 * What that makes checkable here is the module's whole half of the selection:
 * which arm an unnamed launch reaches, what an absence or a typo cannot reach,
 * where a refusal lands, and that an unasked walk costs nothing. What it does
 * NOT make checkable is the row itself, the menu, or any image - the row's own
 * fixture is src/platform/sl_perf.c's, and the image is a GPU question.
 *
 * ONE CASE PER PROCESS, because the arbiter decides once per launch and every
 * case here is a different set of switches.
 *
 * NOT COMPILED OR RUN IN THE ROUND THAT ADDED THEM. These cases were authored
 * against the module's declared contract; the round that wrote them was
 * forbidden any build, so nothing below has been executed. */

/* THE ROW'S WALK REACHES THE AUXILIARY GPU_SOURCE ARM WITH FENCE, with no
 * SL_BG_TF_CLIP anywhere, and the MAIN context issues no prepass draw. */
static void prod_row_on_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    struct sl_bgtf_fence_counters fa;
    GLuint vbo;
    int s;

    begin_case("the PRODUCTION selection: a walk the OPTIMIZED RENDERING row "
               "admitted, with the developer switch NOT named, takes the "
               "auxiliary GPU_SOURCE arm with FENCE");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_opt_in() == 0,
          "the developer switch is NOT named on this launch (%d)",
          sl_bgtf_opt_in());
    /* BEFORE THE WALK ASKS, NOTHING IS SELECTED. A census runs on launches that
     * drew nothing, so the reader a census uses may not arm anything. */
    CHECK(sl_bgtf_production() == 0,
          "and before any walk asks, the production fact reads 0 - a census on "
          "a launch that drew nothing arms no selection (%d)",
          sl_bgtf_production());
    CHECK(tf.aux_create_calls == 0L && tf.calls == 0L && tf.proc_calls == 0L,
          "and reading it created no context, made no GL call and resolved "
          "nothing (%ld/%ld/%ld)", tf.aux_create_calls, tf.calls,
          tf.proc_calls);
    CHECK(sl_bgtf_walk_admit() == 1,
          "the walk the row admitted IS admitted to the prepass (%d)",
          sl_bgtf_walk_admit());
    CHECK(sl_bgtf_production() == 1,
          "and the launch now records the PRODUCTION selection, which the arm "
          "token cannot say (%d)", sl_bgtf_production());
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_GPU,
          "the effective arm is GPU_SOURCE without any selector being named "
          "(%s)", sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 1,
          "it is recorded as the IMPLICIT selection (%d)",
          sl_bgtf_ar_implicit());
    CHECK(sl_bgtf_ar_optin_closed() == 0,
          "nothing failed closed: no selector was offered at all (%d)",
          sl_bgtf_ar_optin_closed());
    CHECK(sl_bgtf_fence_stats()->fx_mode
          == (unsigned long) SL_BGTF_FX_FENCE
          && sl_bgtf_fence_stats()->fx_asked == 0,
          "the synchronisation mode is FENCE and NO key asked for it - it is "
          "the production default, which is the half the exact-image captures "
          "were taken under (%lu/%lu)", sl_bgtf_fence_stats()->fx_mode,
          sl_bgtf_fence_stats()->fx_asked);
    CHECK(sl_bgtf_stats()->ar_refusals == 0,
          "and an unnamed arm is NOT a refusal (%lu)",
          sl_bgtf_stats()->ar_refusals);
    /* v0.4 READINESS IS THE GATE THAT WAS BROKEN, so it is asserted by name and
     * with the reason it answered, not merely used. It once tested
     * sl_bgtf_opt_in and returned 0 here - no entry point resolved, no program
     * compiled - so the production route could not enter its prepass on any
     * launch, and no image could show it because the walk simply drew the CPU
     * path. The test is now the ADMISSION answer, and these three CHECKs are
     * what fail if it is put back. */
    CHECK(sl_bgtf_ready() == 1,
          "READINESS ACCEPTS THE PRODUCTION ROUTE: entry points are resolved "
          "and the program is compiled for a launch that named no switch");
    CHECK(sl_bgtf_state() == 1,
          "and the state is READY (%d), not UNTRIED - an off selection leaves "
          "it untried, and this launch is not off", sl_bgtf_state());
    CHECK(tf.proc_calls > 0L && tf.create_program > 0L && tf.link > 0L,
          "which is visible as real work on the fake GL: resolves %ld, programs "
          "%ld, links %ld - all zero under the old gate", tf.proc_calls,
          tf.create_program, tf.link);
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1, "the walk begins");
    b = snap();
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    a = snap();
    fa = fsnap();
    CHECK(s >= 0, "the served draw is ACCEPTED (%d)", s);
    CHECK(a.ar_gpu_submits - b.ar_gpu_submits == 1,
          "on the GPU_SOURCE arm, so the shared pool is what the main context "
          "will raster (%lu)", a.ar_gpu_submits - b.ar_gpu_submits);
    CHECK(tf.aux_exists == 1 && tf.aux_create_calls == 1L,
          "exactly one auxiliary context was created, and only once the first "
          "served draw needed it (%d/%ld)", tf.aux_exists,
          tf.aux_create_calls);
    CHECK(tf.finish_calls == 0L
          && fa.fx_in_fences > 0 && fa.fx_out_fences > 0,
          "and the handoffs were sync objects rather than pipeline drains, "
          "which is what FENCE means (%ld/%lu/%lu)", tf.finish_calls,
          fa.fx_in_fences, fa.fx_out_fences);
    CHECK(a.ar_syncs - b.ar_syncs == 2,
          "two handoffs were still established, which is what the mode's own "
          "count means under either value (%lu)", a.ar_syncs - b.ar_syncs);
    end_case();
}

/* THE ROW OFF IS THE ORIGINAL RENDERER, AND COSTS NOTHING. The caller does not
 * ask, so nothing here is promoted, created, resolved, drawn or synchronised -
 * which is the whole content of "OFF does nothing extra". */
static void prod_row_off_case(void)
{
    begin_case("the row OFF: the caller never asks, so no arm is promoted, no "
               "auxiliary context exists and not one GL call is issued");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_opt_in() == 0,
          "the developer switch is NOT named (%d)", sl_bgtf_opt_in());
    CHECK(sl_bgtf_production() == 0,
          "and with no walk asking, the production fact reads 0 (%d)",
          sl_bgtf_production());
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_OFF,
          "no arm is in force (%s)", sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 0,
          "nothing was selected implicitly (%d)", sl_bgtf_ar_implicit());
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), "FINISH") == 0,
          "the synchronisation answer is the off launch's own, reached without "
          "reading the key (%s)", sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    CHECK(tf.calls == 0L && tf.proc_calls == 0L && tf.aux_create_calls == 0L
          && tf.draw_arrays == 0L && tf.sync_created == 0L
          && tf.finish_calls == 0L && tf.flush_calls == 0L,
          "and the whole launch issued no GL call, resolved nothing, created "
          "no context, drew nothing and synchronised nothing "
          "(%ld/%ld/%ld/%ld/%ld/%ld/%ld)", tf.calls, tf.proc_calls,
          tf.aux_create_calls, tf.draw_arrays, tf.sync_created,
          tf.finish_calls, tf.flush_calls);
    end_case();
}

/* OFF -> ON -> OFF -> ON IN ONE PROCESS, AT WALK BOUNDARIES. Four walks: the
 * row off, on, off again, and on again. The module is asked only on the two ON
 * walks, which is the caller's contract, and what this pins is that the two off
 * walks add no context, no draw, no GL call and no synchronisation - so a menu
 * press acts at the next walk boundary in each direction and not inside a walk.
 *
 * AND THAT THE SECOND ON WALK IS SERVED FROM THE SAME ARM. The launch-level
 * promotion is not undone by a press, so the fourth walk is driven through the
 * renderer's whole chain and must serve a real draw while the auxiliary context
 * count stays at one. An earlier revision only read the admission answer there,
 * which cannot distinguish a reused arm from a re-created one.
 *
 * THE CHAIN IS admission -> sl_bgtf_ready() -> begin_walk -> prepass, the
 * renderer's own order. Readiness sets the state begin_walk requires, so
 * omitting it made a correct module look like a refusal; see the note on walk
 * 2 below. */
static void prod_transition_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;
    long create_after_on, draws_after_on, calls_after_on;
    unsigned long syncs_after_on;
    int s;

    begin_case("OFF -> ON -> OFF -> ON across FOUR walks in ONE process: the "
               "transition acts at walk boundaries, the two off walks add "
               "nothing at all, and the second ON walk reuses the arm");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;

    /* WALK 1, the row OFF: the caller does not ask.
     *
     * AN OFF WALK IS MEASURED AS "NO GL CALL AND NO SYNC", not only as no
     * context and no draw. The arm's cost is a context, a draw and a pair of
     * synchronisation handoffs, and a walk that issued any GL call at all
     * while the row was off would be doing work the OFF press is supposed to
     * have ended - so the whole call counter and the mode's own sync count are
     * what this reads, with the narrower context/draw counts kept beside them. */
    CHECK(tf.aux_create_calls == 0L && tf.draw_arrays == 0L && tf.calls == 0L,
          "walk 1 with the row off asks nothing and costs nothing: contexts "
          "%ld, draws %ld, GL calls %ld",
          tf.aux_create_calls, tf.draw_arrays, tf.calls);
    CHECK(snap().ar_syncs == 0uL && fsnap().fx_in_fences == 0uL
          && fsnap().fx_out_fences == 0uL,
          "and it established no synchronisation at all (%lu/%lu/%lu)",
          snap().ar_syncs, fsnap().fx_in_fences, fsnap().fx_out_fences);
    CHECK(sl_bgtf_production() == 0,
          "and no selection is in force after it (%d)", sl_bgtf_production());

    /* WALK 2, the row ON: the caller asks, and this walk is served.
     *
     * THE CHAIN IS THE RENDERER'S, which is what this case got wrong. The real
     * caller asks the admission, then sl_bgtf_ready(), then begins the walk,
     * then serves the draw - src/gfx/sl_gfx_dl.c, and
     * tools/windows/bgtfoptin-check.py pins the order (the admission is asked
     * BEFORE readiness, and readiness from exactly one place). Readiness is
     * what sets the module's state, and sl_bgtf_begin_walk refuses on
     * `g_state <= 0` (src/gfx/sl_gfx_bgtf.c:5140), so a fixture that skipped it
     * got begin_walk == 0 from a module that was behaving correctly. Measured
     * as three failures in r746: the omission was the fixture's, so the chain
     * is corrected here rather than the product's refusal relaxed. */
    CHECK(sl_bgtf_walk_admit() == 1, "walk 2 with the row on is admitted (%d)",
          sl_bgtf_walk_admit());
    CHECK(sl_bgtf_production() == 1 && sl_bgtf_aux_real() == SL_BGTF_AR_GPU,
          "and it is the production selection on the auxiliary GPU_SOURCE arm "
          "(%d/%s)", sl_bgtf_production(),
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ready() == 1,
          "readiness accepts the admitted walk, which is the call the renderer "
          "makes between admission and begin_walk");
    CHECK(sl_bgtf_state() == 1,
          "and the state is READY (%d), which is what begin_walk requires",
          sl_bgtf_state());
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 2u) == 1, "walk 2 begins");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s >= 0, "and its served draw is ACCEPTED (%d)", s);
    sl_bgtf_unbind();
    create_after_on = tf.aux_create_calls;
    draws_after_on = tf.draw_arrays;
    CHECK(create_after_on == 1L && draws_after_on > 0L,
          "one auxiliary context and at least one prepass draw exist now "
          "(%ld/%ld)", create_after_on, draws_after_on);
    syncs_after_on = snap().ar_syncs;
    calls_after_on = tf.calls;
    CHECK(syncs_after_on > 0uL,
          "and the served draw established its handoffs (%lu)", syncs_after_on);

    /* WALK 3, the row OFF again: the caller stops asking. */
    CHECK(tf.aux_create_calls == create_after_on
          && tf.draw_arrays == draws_after_on,
          "walk 3 with the row off again adds no context and no draw, so the "
          "OFF press acts at this walk's boundary (%ld/%ld)",
          tf.aux_create_calls, tf.draw_arrays);
    CHECK(tf.calls == calls_after_on && snap().ar_syncs == syncs_after_on,
          "and it adds no GL call and no synchronisation either (%ld/%lu)",
          tf.calls, snap().ar_syncs);
    /* AND THE LAUNCH-LEVEL PROMOTION IS NOT UNDONE BY A PRESS, which is the
     * one thing a reader could misread: the arm this launch promoted is still
     * the arm a later ON walk takes, and the row decides WHICH WALKS reach it
     * rather than whether the arm exists. Nothing is re-created for a later
     * ON walk either. */
    CHECK(sl_bgtf_walk_admit() == 1 && tf.aux_create_calls == create_after_on,
          "a fourth, ON walk is admitted again and re-creates nothing (%d/%ld)",
          sl_bgtf_walk_admit(), tf.aux_create_calls);

    /* WALK 4, THE SECOND ON WALK, DRIVEN RATHER THAN ONLY ADMITTED. The usage
     * text promises OFF -> ON -> OFF -> ON, and an admission answer alone does
     * not establish the thing that sentence claims: that the arm this launch
     * PROMOTED is still the arm a later ON walk serves from, with no second
     * context. So this walk goes through the same renderer chain and serves a
     * real draw, and the context count is required not to have moved.
     *
     * THE CALLER'S OWN BUFFER IS REUSED, deliberately. No context was created
     * or replaced since walk 2, so the name the caller minted there is still
     * the name it would hand in - and reusing it keeps the OFF walk's "no GL
     * call" accounting above honest by not minting anything between them. */
    CHECK(sl_bgtf_ready() == 1,
          "readiness still accepts it, from the state the first ON walk left");
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 4u) == 1, "walk 4 begins");
    s = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    CHECK(s >= 0, "and its served draw is ACCEPTED too (%d)", s);
    sl_bgtf_unbind();
    CHECK(tf.aux_create_calls == create_after_on,
          "the launch-level promotion was REUSED: still exactly one auxiliary "
          "context across both ON walks (%ld)", tf.aux_create_calls);
    CHECK(tf.draw_arrays > draws_after_on,
          "and the second ON walk really drew rather than merely being "
          "admitted (%ld -> %ld)", draws_after_on, tf.draw_arrays);
    CHECK(snap().ar_syncs > syncs_after_on,
          "with its own handoffs, so the arm was driven and not just named "
          "(%lu -> %lu)", syncs_after_on, snap().ar_syncs);
    end_case();
}

/* WHAT THE PRODUCTION SELECTION CANNOT REACH, one launch shape per call.
 *
 * `why` names the shape for the output. Every one of these is a launch whose
 * environment took the production selection away, and in every one the walk is
 * NOT admitted: no arm, no auxiliary context, no prepass on either context, and
 * in particular never the MAIN-CONTEXT prepass, which is the arm measured one
 * blue value apart from the original renderer's frames on the scene that
 * carries the defect.
 *
 * WHERE SUCH A WALK ACTUALLY DRAWS IS NOT THIS FIXTURE'S TO ASSERT, and saying
 * otherwise was a real defect rather than a wording slip. These cases drive
 * src/gfx/sl_gfx_bgtf.c alone; the routing lives in src/gfx/sl_gfx_dl.c's walk
 * chain, and the first draft of that chain sent every unadmitted walk to the
 * BACKGROUND VERTEX PROGRAM - itself one blue value apart - while a comment
 * here claimed the original renderer. The chain now ends in the CPU clip and
 * raster path unless a developer explicitly named the transform, and
 * tools/windows/bgtfoptin-check.py parses it, because no fixture over this
 * module can see it.
 *
 * WHY NO PREPASS IS CALLED IN THIS BODY, which is the one thing a reader could
 * take for a gap. The admission answering 0 IS the property: the caller's
 * contract is that a walk it does not admit never reaches sl_bgtf_begin_walk or
 * sl_bgtf_prepass, and src/gfx/sl_gfx_dl.c's else-if chain is what holds that.
 * Calling the prepass here anyway would drive the MAIN-CONTEXT prepass on a
 * launch the product never routes there, and would then assert that it did not
 * happen - a case that can only fail by being written. What is asserted instead
 * is that NOTHING the module did while deciding issued a call, created a
 * context or drew, and that the admission is 0. */
static void prod_standdown_body(const char *why)
{
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_opt_in() == 0,
          "%s: the developer switch is not named, so nothing admits a walk by "
          "name (%d)", why, sl_bgtf_opt_in());
    CHECK(sl_bgtf_walk_admit() == 0,
          "%s: the walk is NOT admitted, so no prepass of either kind takes it "
          "- where it then draws is the caller's chain (%d)", why,
          sl_bgtf_walk_admit());
    CHECK(sl_bgtf_production() == 0,
          "%s: and no production selection is recorded (%d)", why,
          sl_bgtf_production());
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_OFF,
          "%s: no arm is in force, so nothing the product routes here could "
          "reach the known-mismatching MAIN-CONTEXT prepass either (%s)", why,
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_ar_implicit() == 0,
          "%s: and nothing was selected implicitly (%d)", why,
          sl_bgtf_ar_implicit());
    CHECK(tf.draw_arrays == 0L,
          "%s: deciding all of that drew nothing on either context (%ld)", why,
          tf.draw_arrays);
    CHECK(tf.aux_create_calls == 0L && tf.aux_exists == 0,
          "%s: created no auxiliary GL context (%ld/%d)", why,
          tf.aux_create_calls, tf.aux_exists);
    CHECK(tf.calls == 0L && tf.proc_calls == 0L && tf.sync_created == 0L
          && tf.finish_calls == 0L,
          "%s: and made no GL call, resolved nothing and synchronised nothing "
          "(%ld/%ld/%ld/%ld)", why, tf.calls, tf.proc_calls, tf.sync_created,
          tf.finish_calls);
}

static void prod_standdown_case(const char *why, const char *headline)
{
    begin_case(headline);
    prod_standdown_body(why);
    end_case();
}

/* A REAL CAPABILITY FAILURE ON THE PRODUCTION ROUTE REFUSES AT READINESS, AND
 * THE DISTINCTION FROM A FAIL-CLOSED LAUNCH IS THE WHOLE POINT OF THE CASE.
 *
 * READINESS IS WHERE THIS ONE IS DECIDED. A withheld entry point is not a
 * selection question: the route was selected, the module tried, and the driver
 * cannot supply what the prepass needs. So readiness answers 0, latches
 * UNAVAILABLE, names the capability group, and the caller takes the walk to the
 * CPU path - counted as a WALK refusal by sl_gfx_dl.c, which is truthful
 * because no draw of it was ever served.
 *
 * CONTRAST WITH prod sync_bad, WHICH REFUSES PER DRAW. There the capability is
 * fine and the SELECTOR is unreadable, so readiness succeeds, the walk is
 * prepared, and each served draw is refused at the prepass site under ar_skips.
 * The two must not be confused in a census: one says the driver could not do
 * it, the other says the launch could not be read.
 *
 * AND NEITHER REACHES A MAIN-CONTEXT PREPASS. Nothing is compiled here, so
 * there is no program to issue one with. */
static void prod_capability_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    GLuint vbo;

    begin_case("a REAL capability failure on the PRODUCTION route refuses at "
               "READINESS, latches UNAVAILABLE and reaches no prepass of "
               "either kind");
    arm();
    sl_bgtfgl_new_context();
    vbo = input_vbo(NV, STR);
    tf.withhold_proc = "glTransformFeedbackVaryings";
    CHECK(sl_bgtf_opt_in() == 0,
          "the developer switch is not named (%d)", sl_bgtf_opt_in());
    CHECK(sl_bgtf_walk_admit() == 1,
          "the row's walk IS admitted - the selection is sound, only the "
          "driver is not (%d)", sl_bgtf_walk_admit());
    CHECK(sl_bgtf_ready() == 0,
          "readiness refuses, because a withheld entry point is a capability "
          "failure and not a selection answer");
    CHECK(sl_bgtf_state() == -1,
          "and it LATCHES UNAVAILABLE (%d), unlike an off selection, which "
          "leaves the state untried", sl_bgtf_state());
    CHECK(strstr(sl_bgtf_why(), "transform feedback entry points") != NULL,
          "naming the capability group rather than a selector: '%s'",
          sl_bgtf_why());
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 0,
          "so no walk is prepared, and the caller takes it to the CPU path as "
          "a WALK refusal");
    CHECK(sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2) == -1,
          "and no draw is served");
    CHECK(tf.draw_arrays == 0L && tf.aux_create_calls == 0L
          && tf.aux_exists == 0,
          "NO prepass draw on either context and no auxiliary context: nothing "
          "was compiled, so there is no program to issue one with (%ld/%ld/%d)",
          tf.draw_arrays, tf.aux_create_calls, tf.aux_exists);
    CHECK(sl_bgtf_stats()->ar_skips == 0,
          "and NOTHING is counted as a per-draw refusal of the arm: this "
          "launch never reached the prepass site (%lu)",
          sl_bgtf_stats()->ar_skips);
    end_case();
}

/* AN UNREADABLE SYNCHRONISATION VALUE ON A PRODUCTION LAUNCH FAILS IT CLOSED,
 * AND THE WALK STAYS ADMITTED. That is the difference from every shape above:
 * the walk is admitted so that each served draw travels through the ONE refusal
 * to the existing CPU clip and raster path, rather than leaving the choice to a
 * second predicate. It is never the main-context prepass and never the vertex
 * program. */
static void prod_sync_closed_case(void)
{
    struct sl_bggpu_walk w = a_walk();
    struct sl_bgtf_counters b, a;
    GLuint vbo;
    int s1, s2;

    begin_case("an UNREADABLE synchronisation value on a PRODUCTION launch "
               "FAILS CLOSED: the walk stays ADMITTED and every served draw is "
               "refused to the existing CPU clip and raster path");
    arm();
    sl_bgtfgl_new_context();
    tf.provide_is_program = 1;
    tf.provide_real_set = 1;
    tf.provide_fence_set = 1;
    CHECK(sl_bgtf_opt_in() == 0,
          "the developer switch is not named (%d)", sl_bgtf_opt_in());
    CHECK(sl_bgtf_walk_admit() == 1,
          "the walk the row admitted is still ADMITTED, which is what routes "
          "its draws through the one refusal (%d)", sl_bgtf_walk_admit());
    CHECK(sl_bgtf_ar_optin_closed() == 1,
          "the prepass has FAILED CLOSED (%d)", sl_bgtf_ar_optin_closed());
    CHECK(sl_bgtf_aux_real() == SL_BGTF_AR_OFF,
          "the value selected neither mode, so no arm is in force (%s)",
          sl_bgtf_ar_name(sl_bgtf_aux_real()));
    CHECK(sl_bgtf_production() == 0,
          "and no production selection is claimed: there is no arm to claim it "
          "for (%d)", sl_bgtf_production());
    CHECK(strcmp(sl_bgtf_fx_name(sl_bgtf_ar_sync()), "FINISH") == 0,
          "no fence path is half-established (%s)",
          sl_bgtf_fx_name(sl_bgtf_ar_sync()));
    /* READINESS SUCCEEDS HERE, WHICH IS WHAT PUTS THE REFUSAL AT THE PER-DRAW
     * SITE. The capability is fine and the SELECTOR is unreadable, so the walk
     * is prepared and each served draw is refused under ar_skips - the frozen
     * wording's "every served draw is refused to the EXISTING CPU raster path".
     * Refusing at readiness instead would record a WALK refusal for a launch
     * whose own line promises a per-draw one; prod cap is the case where a
     * readiness refusal IS the truthful answer. */
    CHECK(sl_bgtf_ready() == 1,
          "the module is READY: the selector is unreadable, the capability is "
          "not, so the refusal belongs at the per-draw site");
    CHECK(sl_bgtf_state() == 1,
          "and the state is READY (%d), not UNAVAILABLE - nothing about the "
          "driver failed", sl_bgtf_state());
    vbo = input_vbo(NV, STR);
    CHECK(sl_bgtf_begin_walk(&w, NV, vbo, STR, 1u) == 1,
          "the walk is admitted");
    b = snap();
    s1 = sl_bgtf_prepass(SL_BGGPU_DEPTH_FAR0, K1, K2);
    s2 = sl_bgtf_prepass(SL_BGGPU_DEPTH_SINGLE, 0.0f, 0.0f);
    a = snap();
    CHECK(s1 == -1 && s2 == -1,
          "both served draws are REFUSED, not just the first (%d/%d)", s1, s2);
    CHECK(tf.draw_arrays == 0L,
          "and NO prepass draw was issued on either context (%ld)",
          tf.draw_arrays);
    CHECK(tf.aux_create_calls == 0L && tf.aux_exists == 0
          && tf.sync_created == 0L,
          "no auxiliary context and no sync object was created (%ld/%d/%ld)",
          tf.aux_create_calls, tf.aux_exists, tf.sync_created);
    CHECK(a.ar_skips - b.ar_skips == 2 && a.refusals - b.refusals == 2,
          "each refused draw is counted once in the mode's account and in the "
          "module's (%lu/%lu)", a.ar_skips - b.ar_skips,
          a.refusals - b.refusals);
    CHECK(a.prepasses == b.prepasses && a.binds == b.binds,
          "and nothing claims a prepass or a bind (%lu/%lu)", a.prepasses,
          a.binds);
    end_case();
}

int main(int argc, char **argv)
{
    static char cfg[520];
    const char *tmp = getenv("TEMP");
    const char *path;
    const char *mode = argc >= 2 ? argv[1] : "main";

    printf("background GPU clip prepass fixture (src/gfx/sl_gfx_bgtf.c, SYNTHETIC GL)\n");
    printf("mode: %s\n\n", mode);

    /* The REAL settings store, pointed at a scratch file under %TEMP% so the
     * player's own config is never opened. The store is here only because
     * src/gfx/sl_gfx_worldasset.c links against it; the module under test has
     * no settings or policy dependency at all - its selection is the
     * environment switch alone. */
    if (tmp == NULL || *tmp == '\0') tmp = ".";
    snprintf(cfg, sizeof cfg, "SL_CONFIG=%s\\sl_bgtf_fixture_config.ini", tmp);
    putenv(cfg);
    path = strchr(cfg, '=') + 1;
    remove(path);
    sl_settings_init();

    real_pairs();

    if (strcmp(mode, "env") == 0) {
        const int expect = (argc >= 3 && strcmp(argv[2], "1") == 0) ? 1 : 0;
        mode_env(expect);
    } else if (strcmp(mode, "envempty") == 0) {
        mode_env_empty();
    } else if (strcmp(mode, "envso") == 0) {
        /* The state-only control's grammar and coupling, one setting per launch.
         * The runner says what the control must ANSWER, which is not the same as
         * what the variable says: the answer is coupled to the other two. */
        mode_env_state_only((argc >= 3 && strcmp(argv[2], "1") == 0) ? 1 : 0);
    } else if (strcmp(mode, "envpi") == 0) {
        /* The position-only-input switch's grammar, coupling and exclusivity,
         * one setting per launch. The runner says what EACH of the two
         * exclusive controls must answer, because the exclusivity is a fact
         * about the pair and not about either one. */
        mode_env_pos_input((argc >= 3 && strcmp(argv[2], "1") == 0) ? 1 : 0,
                           (argc >= 4 && strcmp(argv[3], "1") == 0) ? 1 : 0);
    } else if (strcmp(mode, "envzc") == 0) {
        /* The zero-count switch's grammar, coupling, exclusivity and refusal
         * accounting, one setting per launch. The runner says what EACH of the
         * four exclusive controls must answer and whether this launch is one
         * that asked for this control and did not get it, because both the
         * exclusivity and the refusal are facts about the launch rather than
         * about the variable. */
        mode_env_zero_count((argc >= 3 && strcmp(argv[2], "1") == 0) ? 1 : 0,
                            (argc >= 4 && strcmp(argv[3], "1") == 0) ? 1 : 0,
                            (argc >= 5 && strcmp(argv[4], "1") == 0) ? 1 : 0,
                            (argc >= 6 && strcmp(argv[5], "1") == 0) ? 1 : 0,
                            (argc >= 7 && strcmp(argv[6], "1") == 0) ? 1 : 0);
    } else if (strcmp(mode, "pi") == 0) {
        /* THE CONTROL'S OWN RUNS. The mode REFUSES unless the environment is
         * the one it is named for: a scenario driven under the wrong arm would
         * report the other arm's numbers under this one's name. */
        const char *which = argc >= 3 ? argv[2] : "main";
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1
            || sl_bgtf_pos_input() != 1) {
            printf("REFUSING: mode pi %s needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_RASTER_CPU=1 and SL_BG_TF_POS_INPUT=1 with "
                   "SL_BG_TF_STATE_ONLY absent (clip %d, raster_cpu %d, "
                   "pos_input %d, state_only %d).\n",
                   which, sl_bgtf_opt_in(), sl_bgtf_raster_cpu(),
                   sl_bgtf_pos_input(), sl_bgtf_state_only());
            return 2;
        }
        mode_pi(which);
    } else if (strcmp(mode, "so") == 0) {
        /* THE TWO ARMS OF THE STATE-ONLY COMPARISON, one per launch. The mode
         * REFUSES unless the environment is the one it is named for, because a
         * scenario run under the wrong arm would report the other arm's numbers
         * under this one's name - and the runner compares the two digests. */
        const char *which = argc >= 3 ? argv[2] : "ref";
        const int want = strcmp(which, "only") == 0;
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1
            || sl_bgtf_state_only() != want) {
            printf("REFUSING: mode so %s needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_RASTER_CPU=1 and the state-only control %s "
                   "(clip %d, raster_cpu %d, state_only %d).\n",
                   which, want ? "SELECTED" : "ABSENT", sl_bgtf_opt_in(),
                   sl_bgtf_raster_cpu(), sl_bgtf_state_only());
            return 2;
        }
        mode_so(want);
    } else if (strcmp(mode, "zc") == 0) {
        /* THE TWO ARMS OF THE ZERO-COUNT COMPARISON, one per launch, for the
         * reason the state-only pair is: the switch is parsed once, and the
         * runner compares the two digests. The mode REFUSES unless the
         * environment is the one it is named for, including that no OTHER
         * exclusive arm is in force - a scenario run under another arm would
         * report that arm's numbers under this one's name. */
        const char *which = argc >= 3 ? argv[2] : "ref";
        const int want = strcmp(which, "only") == 0;
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1
            || sl_bgtf_zero_count() != want || sl_bgtf_state_only() != 0
            || sl_bgtf_pos_input() != 0 || sl_bgtf_private_vao() != 0) {
            printf("REFUSING: mode zc %s needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_RASTER_CPU=1, the zero-count control %s and the "
                   "three exclusive controls ABSENT (clip %d, raster_cpu %d, "
                   "zero_count %d, state_only %d, pos_input %d, "
                   "private_vao %d).\n",
                   which, want ? "SELECTED" : "ABSENT", sl_bgtf_opt_in(),
                   sl_bgtf_raster_cpu(), sl_bgtf_zero_count(),
                   sl_bgtf_state_only(), sl_bgtf_pos_input(),
                   sl_bgtf_private_vao());
            return 2;
        }
        mode_zc(want);
    } else if (strcmp(mode, "es") == 0) {
        /* ONE ROW OF THE SIX-CONDITION MATRIX PER LAUNCH, because the selector
         * is parsed once for the process. The mode REFUSES unless the
         * environment is the row it was asked for AND the zero-count arm is in
         * force: a scenario driven under another row would report that row's
         * conditions under this one's name, and one driven without the arm
         * beneath it would report a NONEMPTY draw's conditions. */
        const char *which = argc >= 3 ? argv[2] : "A";
        const int want = strcmp(which, "A") == 0 ? SL_BGTF_ES_A
                       : strcmp(which, "B") == 0 ? SL_BGTF_ES_B
                       : strcmp(which, "C") == 0 ? SL_BGTF_ES_C
                       : strcmp(which, "E") == 0 ? SL_BGTF_ES_E
                       : strcmp(which, "R") == 0 ? SL_BGTF_ES_R
                       : strcmp(which, "S") == 0 ? SL_BGTF_ES_S
                       : strcmp(which, "Q") == 0 ? SL_BGTF_ES_Q
                       : -1;
        if (want < 0) {
            printf("REFUSING: mode es needs a row of A, B, C, E, R, S or Q, not "
                   "\"%s\".\n", which);
            return 2;
        }
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1
            || sl_bgtf_zero_count() != 1 || sl_bgtf_empty_state() != want
            || sl_bgtf_state_only() != 0 || sl_bgtf_pos_input() != 0
            || sl_bgtf_private_vao() != 0) {
            printf("REFUSING: mode es %s needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_RASTER_CPU=1, SL_BG_TF_ZERO_COUNT=1, "
                   "SL_BG_TF_EMPTY_STATE=%s and the three exclusive controls "
                   "ABSENT (clip %d, raster_cpu %d, zero_count %d, mode %s, "
                   "state_only %d, pos_input %d, private_vao %d).\n",
                   which, which, sl_bgtf_opt_in(), sl_bgtf_raster_cpu(),
                   sl_bgtf_zero_count(),
                   sl_bgtf_es_name(sl_bgtf_empty_state()),
                   sl_bgtf_state_only(), sl_bgtf_pos_input(),
                   sl_bgtf_private_vao());
            return 2;
        }
        mode_es(want);
    } else if (strcmp(mode, "enves") == 0) {
        /* The selector's grammar, coupling, exclusivity and refusal accounting,
         * one setting per launch. The runner says which row must be in force -
         * OFF for every refusing and every absent launch - and whether this
         * launch asked for a mode and did not get one. */
        const char *which = argc >= 3 ? argv[2] : "OFF";
        const int want = strcmp(which, "A") == 0 ? SL_BGTF_ES_A
                       : strcmp(which, "B") == 0 ? SL_BGTF_ES_B
                       : strcmp(which, "C") == 0 ? SL_BGTF_ES_C
                       : strcmp(which, "E") == 0 ? SL_BGTF_ES_E
                       : strcmp(which, "R") == 0 ? SL_BGTF_ES_R
                       : strcmp(which, "S") == 0 ? SL_BGTF_ES_S
                       : strcmp(which, "Q") == 0 ? SL_BGTF_ES_Q
                       : SL_BGTF_ES_OFF;
        es_table();
        mode_env_empty_state(want,
                             (argc >= 4 && strcmp(argv[3], "1") == 0) ? 1 : 0);
    } else if (strcmp(mode, "pr") == 0) {
        /* ONE ARM OF THE POST-RESET BOUNDARY PER LAUNCH, because the selector is
         * parsed once for the process. The mode REFUSES unless the environment
         * is the arm it was asked for AND the REAL NONZERO path is what is
         * running: an arm driven under the zero-count arm or the original-raster
         * control would report that arm's numbers under this one's name, and the
         * boundary's independence from both is exactly what this scenario is
         * for. */
        const char *which = argc >= 3 ? argv[2] : "POST_PROGRAM0_EMPTY";
        const int want = strcmp(which, "POST_PROGRAM0_EMPTY") == 0
                             ? SL_BGTF_PR_EMPTY
                       : strcmp(which, "POST_FINISH") == 0
                             ? SL_BGTF_PR_FINISH
                       : strcmp(which, "BOTH") == 0 ? SL_BGTF_PR_BOTH
                       : -1;
        if (want < 0) {
            printf("REFUSING: mode pr needs an arm of POST_PROGRAM0_EMPTY, "
                   "POST_FINISH or BOTH, not \"%s\".\n", which);
            return 2;
        }
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_post_reset() != want
            || sl_bgtf_raster_cpu() != 0 || sl_bgtf_zero_count() != 0
            || sl_bgtf_empty_state() != SL_BGTF_ES_OFF
            || sl_bgtf_state_only() != 0 || sl_bgtf_pos_input() != 0
            || sl_bgtf_private_vao() != 0) {
            printf("REFUSING: mode pr %s needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_POST_RESET=%s and every other control ABSENT - "
                   "this scenario is the REAL nonzero path (clip %d, arm %s, "
                   "raster_cpu %d, zero_count %d, empty_state %s, state_only "
                   "%d, pos_input %d, private_vao %d).\n",
                   which, which, sl_bgtf_opt_in(),
                   sl_bgtf_pr_name(sl_bgtf_post_reset()),
                   sl_bgtf_raster_cpu(), sl_bgtf_zero_count(),
                   sl_bgtf_es_name(sl_bgtf_empty_state()),
                   sl_bgtf_state_only(), sl_bgtf_pos_input(),
                   sl_bgtf_private_vao());
            return 2;
        }
        mode_pr(want);
    } else if (strcmp(mode, "envpr") == 0) {
        /* The selector's grammar, coupling, exclusivity and refusal accounting,
         * one setting per launch. The runner says which arm must be in force -
         * OFF for every refusing and every absent launch - and whether this
         * launch asked for an arm and did not get one. */
        const char *which = argc >= 3 ? argv[2] : "OFF";
        const int want = strcmp(which, "POST_PROGRAM0_EMPTY") == 0
                             ? SL_BGTF_PR_EMPTY
                       : strcmp(which, "POST_FINISH") == 0
                             ? SL_BGTF_PR_FINISH
                       : strcmp(which, "BOTH") == 0 ? SL_BGTF_PR_BOTH
                       : SL_BGTF_PR_OFF;
        pr_table();
        mode_env_post_reset(want,
                            (argc >= 4 && strcmp(argv[3], "1") == 0) ? 1 : 0);
    } else if (strcmp(mode, "np") == 0) {
        /* THE CONTROL'S OWN RUN. The mode REFUSES unless the environment is the
         * one it is named for, including that no control it is exclusive with is
         * in force: a scenario driven under another arm would report that arm's
         * numbers under this one's name. */
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1
            || sl_bgtf_zero_count() != 1 || sl_bgtf_neutral_prog() != 1
            || sl_bgtf_empty_state() != SL_BGTF_ES_OFF
            || sl_bgtf_post_reset() != SL_BGTF_PR_OFF
            || sl_bgtf_state_only() != 0 || sl_bgtf_pos_input() != 0
            || sl_bgtf_private_vao() != 0) {
            printf("REFUSING: mode np needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_RASTER_CPU=1, SL_BG_TF_ZERO_COUNT=1, "
                   "SL_BG_TF_NEUTRAL_PROG=1 and every control it is exclusive "
                   "with ABSENT (clip %d, raster_cpu %d, zero_count %d, "
                   "neutral %d, empty_state %s, post_reset %s, state_only %d, "
                   "pos_input %d, private_vao %d).\n",
                   sl_bgtf_opt_in(), sl_bgtf_raster_cpu(),
                   sl_bgtf_zero_count(), sl_bgtf_neutral_prog(),
                   sl_bgtf_es_name(sl_bgtf_empty_state()),
                   sl_bgtf_pr_name(sl_bgtf_post_reset()),
                   sl_bgtf_state_only(), sl_bgtf_pos_input(),
                   sl_bgtf_private_vao());
            return 2;
        }
        np_scenario();
        np_failures();
    } else if (strcmp(mode, "ax") == 0) {
        /* THE CONTROL'S OWN RUNS, one per launch where a launch is forced.
         *
         *   (none) / "main"  the scenario and the five recoverable fail-closed
         *                    shapes, which do not latch the process
         *   "lost_acquire"   the verification switch back fails
         *   "lost_draw"      the DRAW's switch back fails
         *   "unknown_acquire" the acquire's switch TO the auxiliary context
         *                    establishes NEITHER context
         *   "unknown_draw"   the same, at the draw
         *   "destroy_refused" the teardown's release is refused by the backend
         *
         * The five terminal shapes are their own launches because the lost latch
         * belongs to the PROCESS by design: a second one driven after the first
         * would be refused by that latch before reaching its own failure. The
         * mode REFUSES unless the environment is the one it is named for,
         * including that no control it is exclusive with is in force - a
         * scenario driven under another arm would report that arm's numbers
         * under this one's name. */
        const char *which = argc >= 3 ? argv[2] : "main";
        if (strcmp(which, "main") != 0 && strcmp(which, "lost_acquire") != 0
            && strcmp(which, "lost_draw") != 0
            && strcmp(which, "unknown_acquire") != 0
            && strcmp(which, "unknown_draw") != 0
            && strcmp(which, "destroy_refused") != 0) {
            printf("REFUSING: mode ax takes main, lost_acquire, lost_draw, "
                   "unknown_acquire, unknown_draw or destroy_refused, "
                   "not \"%s\".\n", which);
            return 2;
        }
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1
            || sl_bgtf_zero_count() != 1 || sl_bgtf_aux_empty() != 1
            || sl_bgtf_neutral_prog() != 0
            || sl_bgtf_empty_state() != SL_BGTF_ES_OFF
            || sl_bgtf_post_reset() != SL_BGTF_PR_OFF
            || sl_bgtf_state_only() != 0 || sl_bgtf_pos_input() != 0
            || sl_bgtf_private_vao() != 0) {
            printf("REFUSING: mode ax needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_RASTER_CPU=1, SL_BG_TF_ZERO_COUNT=1, "
                   "SL_BG_TF_AUX_EMPTY=1 and every control it is exclusive "
                   "with ABSENT (clip %d, raster_cpu %d, zero_count %d, "
                   "aux %d, neutral %d, empty_state %s, post_reset %s, "
                   "state_only %d, pos_input %d, private_vao %d).\n",
                   sl_bgtf_opt_in(), sl_bgtf_raster_cpu(),
                   sl_bgtf_zero_count(), sl_bgtf_aux_empty(),
                   sl_bgtf_neutral_prog(),
                   sl_bgtf_es_name(sl_bgtf_empty_state()),
                   sl_bgtf_pr_name(sl_bgtf_post_reset()),
                   sl_bgtf_state_only(), sl_bgtf_pos_input(),
                   sl_bgtf_private_vao());
            return 2;
        }
        if (strcmp(which, "lost_acquire") == 0)   ax_lost_case(0);
        else if (strcmp(which, "lost_draw") == 0) ax_lost_case(1);
        else if (strcmp(which, "unknown_acquire") == 0)
            ax_unknown_switch_case(0);
        else if (strcmp(which, "unknown_draw") == 0)
            ax_unknown_switch_case(1);
        else if (strcmp(which, "destroy_refused") == 0)
            ax_destroy_refused_case();
        else {
            ax_scenario();
            ax_failures();
        }
    } else if (strcmp(mode, "fxsel") == 0) {
        /* THE SYNCHRONISATION SELECTOR ON A LAUNCH THAT ARMED NO ARM
         * (SL_BG_TF_AUX_SYNC). The answer must be FINISH whatever the key says,
         * and deciding it must cost nothing: the module reads the key only from
         * sites already behind the real-prepass mode being in force, so an
         * unarmed launch performs no getenv for it at all. */
        sl_bgtfgl_reset();
        fx_selector_case("FINISH");
    } else if (strcmp(mode, "fx") == 0) {
        /* ONE ARM OR ONE FENCE SHAPE PER PROCESS, exactly as mode "ar" is: both
         * selectors are parsed once per launch and the lost latch belongs to the
         * PROCESS.
         *
         *   "gpu" / "cpu"   the arm named, end to end, under FENCE
         *   "cap_main"      glWaitSync does not resolve on the MAIN context
         *   "cap_aux"       glFenceSync does not resolve on the AUXILIARY one
         *   "cre_main"      the input fence creation answers 0
         *   "cre_aux"       the output fence creation answers 0
         *   "switch"        the bracket's switch fails with a fence held
         *   "wait_err"      the consumer's wait raises a GL error
         *   "lost_in"       the switch TO that context establishes neither,
         *                   with the input fence held
         *   "lost_out"      the switch home fails after the prepass ran, with
         *                   the output fence held
         *   "sel"           the selector answers FENCE on an armed launch and
         *                   an unreadable value falls back to FINISH
         *
         * The mode REFUSES unless the environment is the one it is named for,
         * the fence selector included: a FENCE scenario driven under FINISH
         * would report the original path's numbers under this mode's name. */
        const char *which = argc >= 3 ? argv[2] : "gpu";
        const int gpu = strcmp(which, "gpu") == 0;
        const int cpu = strcmp(which, "cpu") == 0;
        const int want = cpu ? SL_BGTF_AR_CPU : SL_BGTF_AR_GPU;
        const int selonly = strcmp(which, "sel") == 0;
        if (!gpu && !cpu && !selonly
            && strcmp(which, "cap_main") != 0 && strcmp(which, "cap_aux") != 0
            && strcmp(which, "cre_main") != 0 && strcmp(which, "cre_aux") != 0
            && strcmp(which, "switch") != 0 && strcmp(which, "wait_err") != 0
            && strcmp(which, "lost_in") != 0
            && strcmp(which, "lost_out") != 0) {
            printf("REFUSING: mode fx takes cpu, gpu, sel, cap_main, cap_aux, "
                   "cre_main, cre_aux, switch, wait_err, lost_in or lost_out, "
                   "not \"%s\".\n", which);
            return 2;
        }
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_aux_real() != want
            || sl_bgtf_raster_cpu() != (want == SL_BGTF_AR_CPU ? 1 : 0)) {
            printf("REFUSING: mode fx needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_AUX_REAL=%s and SL_BG_TF_RASTER_CPU %s (clip %d, "
                   "arm %s, raster_cpu %d).\n", sl_bgtf_ar_name(want),
                   want == SL_BGTF_AR_CPU ? "=1" : "off", sl_bgtf_opt_in(),
                   sl_bgtf_ar_name(sl_bgtf_aux_real()), sl_bgtf_raster_cpu());
            return 2;
        }
        sl_bgtfgl_reset();
        if (selonly) {
            /* THE GRAMMAR ON AN ARMED LAUNCH, where the key IS read. The runner
             * drives this twice, once with FENCE and once with an unreadable
             * value, and passes the expected answer in argv[3] - so "an
             * unreadable value falls back to the measured path" is a comparison
             * rather than an assertion. */
            fx_token_case(argc >= 4 ? argv[3] : "FENCE");
        } else if (gpu || cpu) {
            fx_scenario(gpu);
        } else if (strcmp(which, "cap_main") == 0) {
            begin_case("auxiliary real prepass, FENCE: a fence entry point the "
                       "MAIN context will not resolve REFUSES the mode before "
                       "any context exists, and never falls back to the drains");
            fx_cap_shape("the main context has no glWaitSync", "glWaitSync",
                         NULL);
            end_case();
        } else if (strcmp(which, "cap_aux") == 0) {
            begin_case("auxiliary real prepass, FENCE: a fence entry point the "
                       "AUXILIARY context will not resolve REFUSES the mode, "
                       "destroys that context again and never reaches for the "
                       "main context's pointer");
            fx_cap_shape("the auxiliary context has no glFenceSync", NULL,
                         "glFenceSync");
            end_case();
        } else if (strcmp(which, "cre_main") == 0) {
            begin_case("auxiliary real prepass, FENCE: an input fence creation "
                       "that answers 0 REFUSES the served draw before the "
                       "switch, with nothing created and nothing to clean up");
            fx_create_shape("the input fence answers 0", 0);
            end_case();
        } else if (strcmp(which, "cre_aux") == 0) {
            begin_case("auxiliary real prepass, FENCE: an output fence "
                       "creation that answers 0 after the prepass ran REFUSES "
                       "the served draw as an UNPROVED handoff - the pool is "
                       "never re-bound and no slot is bindable - while that "
                       "context is still restored");
            fx_create_shape("the output fence answers 0", 1);
            end_case();
        } else if (strcmp(which, "switch") == 0) {
            fx_switch_fail_case();
        } else if (strcmp(which, "wait_err") == 0) {
            fx_wait_error_case();
        } else if (strcmp(which, "lost_in") == 0) {
            begin_case("auxiliary real prepass, FENCE: a switch that "
                       "establishes NEITHER context with an input fence held "
                       "abandons the object rather than deleting it, and "
                       "issues no GL command of any kind afterwards");
            fx_lost_shape("the switch to that context is unknown", 0);
            end_case();
        } else {
            begin_case("auxiliary real prepass, FENCE: a switch home that "
                       "fails after the transform wrote abandons the OUTPUT "
                       "fence, claims no write, and issues no GL command of "
                       "any kind afterwards");
            fx_lost_shape("the switch home fails after the write", 1);
            end_case();
        }
    } else if (strcmp(mode, "ar") == 0) {
        /* ONE ARM OR ONE FAILURE SHAPE PER PROCESS. The selector is parsed once
         * per launch and the lost latch belongs to the PROCESS, so each is its
         * own run of this binary with its own environment.
         *
         *   "cpu" / "gpu"      the arm named, end to end
         *   "no_real_set"      a name only this mode needs is withheld on the
         *                      AUXILIARY context, so the main set is complete
         *                      and a mode reaching for a main pointer would
         *                      sail through - which is the defect this refuses
         *   "unshared_prog"    glIsProgram answers FALSE on that context
         *   "no_vao"           that context gives no array object
         *   "unshared_input"   the FIRST per-bracket glIsBuffer answers FALSE
         *   "unshared_pool"    the SECOND one does
         *   "bracket_switch"   the bracket's own switch fails with MAIN verified
         *   "sync_lost"        the switch back fails AFTER the transform wrote
         *
         * The mode REFUSES unless the environment is the one it is named for,
         * including that no control it is exclusive with is in force: a scenario
         * driven under another arm would report that arm's numbers under this
         * one's name. */
        const char *which = argc >= 3 ? argv[2] : "gpu";
        const int gpu = strcmp(which, "gpu") == 0;
        const int cpu = strcmp(which, "cpu") == 0;
        /* EVERY SHAPE BUT THE CPU ARM RUNS ON GPU_SOURCE, which is the arm whose
         * raster source is the pool and so the one on which "the slot stays
         * UNWRITTEN" is the statement that matters.
         *
         * A FAILURE SHAPE MAY BE DRIVEN ON EITHER ARM, THOUGH, and the arm it is
         * driven on is read from the RASTER SOURCE rather than assumed. The two
         * arms differ only in what the main context rasterises afterwards, which
         * a refused bracket never reaches, so a refusal shape is the same
         * statement on both - and the refusal TEXT names the arm, which is what
         * the label checks are about. The coherence the gate below enforces is
         * unchanged: the arm and its own raster source must still agree, or the
         * launch cannot say which of the two it is running. */
        const int want = (gpu || cpu)
                         ? (cpu ? SL_BGTF_AR_CPU : SL_BGTF_AR_GPU)
                         : (sl_bgtf_raster_cpu() == 1 ? SL_BGTF_AR_CPU
                                                      : SL_BGTF_AR_GPU);
        if (!gpu && !cpu && strcmp(which, "no_real_set") != 0
            && strcmp(which, "unshared_prog") != 0
            && strcmp(which, "no_vao") != 0
            && strcmp(which, "unshared_input") != 0
            && strcmp(which, "unshared_pool") != 0
            && strcmp(which, "bracket_switch") != 0
            && strcmp(which, "sync_lost") != 0
            && strcmp(which, "err_setup") != 0
            && strcmp(which, "err_begin") != 0
            && strcmp(which, "err_draw") != 0
            && strcmp(which, "err_end") != 0
            && strcmp(which, "err_restore") != 0
            && strcmp(which, "err_many") != 0
            && strcmp(which, "err_cap") != 0
            && strcmp(which, "err_lost") != 0
            && strcmp(which, "err_pre") != 0
            && strcmp(which, "err_pre_many") != 0
            && strcmp(which, "err_pre_ctxlost") != 0
            && strcmp(which, "err_pre_lost") != 0
            && strncmp(which, "fatal_", 6) != 0
            && strncmp(which, "lbl_", 4) != 0) {
            printf("REFUSING: mode ar takes cpu, gpu, no_real_set, "
                   "unshared_prog, no_vao, unshared_input, unshared_pool, "
                   "bracket_switch, sync_lost, err_setup, err_begin, err_draw, "
                   "err_end, err_restore, err_many, err_cap, err_lost, "
                   "err_pre, err_pre_many, err_pre_ctxlost, err_pre_lost, "
                   "fatal_<shared|acquire|bracket|backend> or "
                   "lbl_<probe|create|switch|proc|share>, not \"%s\".\n", which);
            return 2;
        }
        if (sl_bgtf_opt_in() != 1 || sl_bgtf_aux_real() != want
            || sl_bgtf_zero_count() != 0 || sl_bgtf_aux_empty() != 0
            || sl_bgtf_neutral_prog() != 0
            || sl_bgtf_empty_state() != SL_BGTF_ES_OFF
            || sl_bgtf_post_reset() != SL_BGTF_PR_OFF
            || sl_bgtf_state_only() != 0 || sl_bgtf_pos_input() != 0
            || sl_bgtf_private_vao() != 0
            || sl_bgtf_raster_cpu() != (want == SL_BGTF_AR_CPU ? 1 : 0)) {
            printf("REFUSING: mode ar needs SL_BG_TF_CLIP=1, "
                   "SL_BG_TF_AUX_REAL=%s, SL_BG_TF_RASTER_CPU %s and every "
                   "control it is exclusive with ABSENT (clip %d, arm %s, "
                   "raster_cpu %d, zero_count %d, aux_empty %d, neutral %d, "
                   "empty_state %s, post_reset %s, state_only %d, pos_input "
                   "%d, private_vao %d).\n",
                   sl_bgtf_ar_name(want),
                   want == SL_BGTF_AR_CPU ? "=1" : "off",
                   sl_bgtf_opt_in(), sl_bgtf_ar_name(sl_bgtf_aux_real()),
                   sl_bgtf_raster_cpu(), sl_bgtf_zero_count(),
                   sl_bgtf_aux_empty(), sl_bgtf_neutral_prog(),
                   sl_bgtf_es_name(sl_bgtf_empty_state()),
                   sl_bgtf_pr_name(sl_bgtf_post_reset()),
                   sl_bgtf_state_only(), sl_bgtf_pos_input(),
                   sl_bgtf_private_vao());
            return 2;
        }
        if (gpu || cpu) {
            ar_scenario(gpu);
        } else if (strcmp(which, "sync_lost") == 0) {
            ar_sync_lost_case();
        } else if (strcmp(which, "err_cap") == 0) {
            ar_gl_drain_cap_case();
        } else if (strcmp(which, "err_lost") == 0) {
            ar_gl_error_lost_case();
        } else if (strncmp(which, "err_pre", 7) == 0) {
            /* CODES ALREADY FLAGGED ON ARRIVAL. One drive per launch, because
             * the latch belongs to the process: a single code, several at once,
             * GL_CONTEXT_LOST - the one a reader would most want named - and the
             * same finding with the switch home then unverifiable, which is the
             * only one of the four that is terminal. The code is what varies;
             * the outcome must not. */
            begin_case("auxiliary real prepass: an error flagged on that "
                       "context BEFORE a bracket arrives REFUSES the served "
                       "draw and latches the capability - nothing is bound, "
                       "enabled, issued or written there, the attribution stays "
                       "separate from the verdict's, and no later mesh reaches "
                       "that context");
            if (strcmp(which, "err_pre") == 0)
                ar_gl_pre_error_shape("one pre-existing code",
                                      GL_INVALID_OPERATION, 1, 0);
            else if (strcmp(which, "err_pre_many") == 0)
                /* SEVERAL FLAGS AT ONCE, which GL permits and a single read
                 * would not clear: the drain has to retire all of them, and the
                 * FIRST is what the report names. */
                ar_gl_pre_error_shape("two pre-existing codes at once",
                                      GL_INVALID_ENUM, 2, 0);
            else if (strcmp(which, "err_pre_ctxlost") == 0)
                /* THE CODE THE ARGUMENT RESTS ON. GL_CONTEXT_LOST says the
                 * context is gone, and the drain cannot tell it from a harmless
                 * INVALID_ENUM except by the value - which is why ANY nonzero
                 * answer here refuses rather than only this one. */
                ar_gl_pre_error_shape("a pre-existing GL_CONTEXT_LOST",
                                      GL_CONTEXT_LOST, 1, 0);
            else
                ar_gl_pre_error_shape("a pre-existing code AND an unverifiable "
                                      "main context", GL_INVALID_OPERATION, 1,
                                      1);
            end_case();
        } else if (strncmp(which, "fatal_", 6) == 0) {
            /* THE FOUR TERMINATION PATHS THIS MODE CAN REACH, one per launch
             * because the lost latch belongs to the process. The runner reads
             * the stderr label; this fixture establishes that each one really
             * terminates, once, with no GL call after it. */
            begin_case("auxiliary real prepass: every termination path this mode "
                       "can reach ends the process exactly once, issues no GL "
                       "call after that point, writes no slot and leaves nothing "
                       "bindable");
            if (strcmp(which, "fatal_shared") == 0)
                ar_lost_shape("the SHARED acquire's own switch", 0);
            else if (strcmp(which, "fatal_acquire") == 0)
                ar_lost_shape("this mode's acquire's switch", 1);
            else if (strcmp(which, "fatal_bracket") == 0)
                ar_lost_shape("the bracket's switch at the prepass", 2);
            else
                ar_lost_shape("the BACKEND refusing to say a destroy was safe",
                              3);
            end_case();
        } else if (strncmp(which, "lbl_", 4) == 0) {
            /* THE FIVE REFUSALS THE SHARED ACQUIRE OWNS, each on a launch that
             * selected THIS mode. The runner reads the stderr label; this
             * fixture reads the counters and the cleanup. */
            begin_case("auxiliary real prepass: a refusal from inside the "
                       "SHARED acquire, on a launch that selected this mode - "
                       "counted by both controls, recoverable, and attributable "
                       "to the selector that was actually set");
            if (strcmp(which, "lbl_probe") == 0)
                ar_shared_refusal_shape("no glIsProgram to probe with", 0, 0, 0,
                                        0, NULL);
            else if (strcmp(which, "lbl_create") == 0)
                ar_shared_refusal_shape("a backend that creates nothing", 1, 1,
                                        0, 0, NULL);
            else if (strcmp(which, "lbl_switch") == 0)
                ar_shared_refusal_shape("the shared acquire's own switch "
                                        "failing", 1, 0, 1, 0, NULL);
            else if (strcmp(which, "lbl_proc") == 0)
                ar_shared_refusal_shape("the shared pair not resolving there", 1,
                                        0, 0, 0, "glUseProgram");
            else
                ar_shared_refusal_shape("a context that shares no names", 1, 0,
                                        0, 1, NULL);
            end_case();
        } else if (strncmp(which, "err_", 4) == 0) {
            /* ONE REJECTED COMMAND PER LAUNCH, named by where in the sequence it
             * sits. The flag GL raises is sticky, so the outcome must not depend
             * on which of these it was - which is the property these five
             * launches assert together and no one of them could. */
            begin_case("auxiliary real prepass: GL REJECTING a command of the "
                       "real prepass binds no output, rasterises nothing from "
                       "the pool, leaves the slot UNWRITTEN, refuses the served "
                       "draw to the CPU path, restores both contexts and reports "
                       "once with the driver's own code");
            if (strcmp(which, "err_setup") == 0)
                /* THE SECOND glEnable, NOT THE FIRST. The bracket's common
                 * setup enables the discard on the MAIN context before this
                 * mode's block runs and enables it again on the auxiliary one,
                 * so occurrence 0 is the main context's call. Aiming at it
                 * would raise a flag on the main context, which this mode must
                 * never read - and the launch would pass while testing
                 * nothing. */
                ar_gl_error_shape("a setup-state error", TFGL_ENABLE, 1L,
                                  GL_INVALID_ENUM, 1);
            else if (strcmp(which, "err_begin") == 0)
                ar_gl_error_shape("an error at the feedback BEGIN", TFGL_BEGINTF,
                                  0L, GL_INVALID_OPERATION, 1);
            else if (strcmp(which, "err_draw") == 0)
                ar_gl_error_shape("an error at the real nonzero DRAW",
                                  TFGL_DRAWARRAYS, 0L, GL_INVALID_OPERATION, 1);
            else if (strcmp(which, "err_end") == 0)
                ar_gl_error_shape("an error at the feedback END", TFGL_ENDTF, 0L,
                                  GL_INVALID_OPERATION, 1);
            else if (strcmp(which, "err_restore") == 0)
                ar_gl_error_shape("an error in that context's RESTORATION",
                                  TFGL_DISABLE, 0L, GL_INVALID_ENUM, 1);
            else
                /* SEVERAL FLAGS AT ONCE, which GL permits and a single read
                 * would not clear: the drain has to retire all of them, and the
                 * FIRST is what the report names. */
                ar_gl_error_shape("four codes flagged at once by the draw",
                                  TFGL_DRAWARRAYS, 0L, GL_INVALID_VALUE, 4);
            end_case();
        } else {
            begin_case("auxiliary real prepass: one capability, verification or "
                       "switch failure refuses the served draw, runs the "
                       "prepass on NEITHER context, leaves the slot unwritten "
                       "and latches");
            if (strcmp(which, "no_real_set") == 0)
                ar_fail_shape("no real set", 1, "glGenVertexArrays", -1L, 0,
                              -1L, 0);
            else if (strcmp(which, "unshared_prog") == 0)
                ar_fail_shape("unshared program", 1, NULL, 1L, 0, -1L, 0);
            else if (strcmp(which, "no_vao") == 0)
                ar_fail_shape("no array object", 1, NULL, -1L, 1, -1L, 0);
            else if (strcmp(which, "unshared_input") == 0)
                ar_fail_shape("unshared input", 1, NULL, -1L, 0, 0L, 0);
            else if (strcmp(which, "unshared_pool") == 0)
                ar_fail_shape("unshared pool", 1, NULL, -1L, 0, 1L, 0);
            else
                ar_fail_shape("the bracket's switch", 1, NULL, -1L, 0, -1L, 1);
            end_case();
        }
    } else if (strcmp(mode, "envar") == 0) {
        /* The selector's grammar, coupling, exclusivity and refusal accounting,
         * one setting per launch. The runner says which arm the mode must
         * ANSWER and whether this launch asked for one and did not get it. */
        mode_env_aux_real(argc >= 3 ? argv[2] : "OFF",
                          (argc >= 4 && strcmp(argv[3], "1") == 0) ? 1 : 0);
    } else if (strcmp(mode, "optin") == 0) {
        /* ONE CASE PER PROCESS, because the arbiter decides once per launch and
         * every case here is a different set of switches. Each sub-case states
         * the environment it needs and REFUSES rather than reporting another
         * launch's numbers under its own name.
         *
         *   "implicit"   SL_BG_TF_CLIP=1 and nothing else: the candidate
         *   "off"        with the opt-in switch ABSENT: default off
         *   "explicit"   SL_BG_TF_CLIP=1 with SL_BG_TF_AUX_REAL=OFF
         *   "named"      SL_BG_TF_CLIP=1 with SL_BG_TF_AUX_REAL=GPU_SOURCE
         *   "standdown"  SL_BG_TF_CLIP=1 with SL_BG_TF_RASTER_CPU=1
         *   "closed"     SL_BG_TF_CLIP=1 with an UNREADABLE SL_BG_TF_AUX_REAL
         *   "sync_default"  the candidate with no synchronisation key
         *   "sync_fence"    the candidate with SL_BG_TF_AUX_SYNC=FENCE
         *   "sync_finish"   the candidate with SL_BG_TF_AUX_SYNC=FINISH, which
         *                   is the launch an unconditional FENCE statement was
         *                   false for
         *   "sync_bad"      the candidate with an UNREADABLE SL_BG_TF_AUX_SYNC
         *   "sync_bad_named" SL_BG_TF_AUX_REAL=GPU_SOURCE with the same
         *   "cap_main"   the candidate with glWaitSync withheld on the MAIN
         *                context: the fence capability is missing, so the
         *                served draw is refused to the CPU raster path and
         *                never served by the drains or by the main context
         *
         * THE REQUESTED SYNCHRONISATION MODE IS READ FROM THE FENCE CENSUS
         * rather than from sl_bgtf_ar_sync, because that answers the EFFECTIVE
         * mode and the fence capability is established lazily inside the first
         * served draw - so before a draw it answers FINISH on every launch.
         * fx_mode carries the decision and fx_asked whether the KEY made it,
         * which is exactly what separates these three cases.
         *
         * TWO CASES SHARE ONE LAUNCH SHAPE. An unreadable arm and an unreadable
         * synchronisation value on the candidate both leave no arm in force,
         * nothing implicit and the latch raised, and both assert the same
         * in-process facts; what differs is which line the launch printed, which
         * the stderr verifier reads. The gate therefore accepts either for both
         * names rather than pretending to tell them apart. */
        const char *which = argc >= 3 ? argv[2] : "implicit";
        const int clip = sl_bgtf_opt_in();
        const int ar   = sl_bgtf_aux_real();
        const int imp  = sl_bgtf_ar_implicit();
        const int clo  = sl_bgtf_ar_optin_closed();
        const int rc   = sl_bgtf_raster_cpu();
        const unsigned long fxm = sl_bgtf_fence_stats()->fx_mode;
        const unsigned long fxa = sl_bgtf_fence_stats()->fx_asked;
        const unsigned long arf = sl_bgtf_stats()->ar_refusals;
        int ok;

        if (strcmp(which, "implicit") == 0 || strcmp(which, "cap_main") == 0
            || strcmp(which, "sync_default") == 0)
            ok = clip == 1 && ar == SL_BGTF_AR_GPU && imp == 1 && clo == 0
                 && rc == 0 && fxm == (unsigned long) SL_BGTF_FX_FENCE
                 && fxa == 0;
        else if (strcmp(which, "sync_fence") == 0)
            ok = clip == 1 && ar == SL_BGTF_AR_GPU && imp == 1 && clo == 0
                 && rc == 0 && fxm == (unsigned long) SL_BGTF_FX_FENCE
                 && fxa == 1;
        else if (strcmp(which, "sync_finish") == 0)
            ok = clip == 1 && ar == SL_BGTF_AR_GPU && imp == 1 && clo == 0
                 && rc == 0 && fxm == (unsigned long) SL_BGTF_FX_FINISH;
        else if (strcmp(which, "off") == 0)
            ok = clip == 0 && ar == SL_BGTF_AR_OFF && imp == 0 && clo == 0;
        else if (strcmp(which, "explicit") == 0)
            ok = clip == 1 && ar == SL_BGTF_AR_OFF && imp == 0 && clo == 0
                 && rc == 0;
        else if (strcmp(which, "named") == 0)
            ok = clip == 1 && ar == SL_BGTF_AR_GPU && imp == 0 && clo == 0
                 && rc == 0 && fxm == (unsigned long) SL_BGTF_FX_FINISH;
        else if (strcmp(which, "standdown") == 0)
            ok = clip == 1 && ar == SL_BGTF_AR_OFF && imp == 0 && clo == 0
                 && rc == 1;
        else if (strcmp(which, "closed") == 0
                 || strcmp(which, "sync_bad") == 0)
            ok = clip == 1 && ar == SL_BGTF_AR_OFF && imp == 0 && clo == 1
                 && arf == 0;
        else if (strcmp(which, "sync_bad_named") == 0)
            ok = clip == 1 && ar == SL_BGTF_AR_OFF && imp == 0 && clo == 1
                 && arf == 1;
        else {
            printf("REFUSING: mode optin takes implicit, off, explicit, named, "
                   "standdown, closed, sync_default, sync_fence, sync_finish, "
                   "sync_bad, sync_bad_named or cap_main, not \"%s\".\n", which);
            return 2;
        }
        if (!ok) {
            printf("REFUSING: mode optin %s was driven under the wrong launch "
                   "(clip %d, arm %s, implicit %d, failed_closed %d, "
                   "raster_cpu %d, fx_mode %lu, fx_asked %lu, ar_refusals "
                   "%lu). Each case is one set of switches and the arbiter "
                   "decides once per process.\n", which, clip,
                   sl_bgtf_ar_name(ar), imp, clo, rc, fxm, fxa, arf);
            return 2;
        }
        sl_bgtfgl_reset();
        if (strcmp(which, "implicit") == 0)        optin_implicit_case();
        else if (strcmp(which, "off") == 0)        optin_default_off_case();
        else if (strcmp(which, "explicit") == 0)   optin_explicit_off_case();
        else if (strcmp(which, "named") == 0)      optin_explicit_named_case();
        else if (strcmp(which, "standdown") == 0)  optin_standdown_case();
        else if (strcmp(which, "closed") == 0)     optin_closed_case();
        else if (strcmp(which, "sync_default") == 0)
            optin_sync_case("FENCE", 0);
        else if (strcmp(which, "sync_fence") == 0)
            optin_sync_case("FENCE", 1);
        else if (strcmp(which, "sync_finish") == 0)
            optin_sync_case("FINISH", 1);
        else if (strcmp(which, "sync_bad") == 0)
            optin_sync_closed_case(0);
        else if (strcmp(which, "sync_bad_named") == 0)
            optin_sync_closed_case(1);
        else {
            /* THE CANDIDATE'S OWN CAPABILITY REFUSAL, driven through the shape
             * the named arms already use: the arm in force is this launch's
             * implicit one, and what is withheld is a fence name the MAIN
             * context must supply. The shape's own assertions are the ones that
             * matter here - the served draw is refused, not one glFinish was
             * issued, and NO prepass ran on either context. */
            begin_case("the OPT-IN CANDIDATE with a fence entry point the MAIN "
                       "context will not resolve: the served draw is REFUSED to "
                       "the CPU raster path, with no fallback to the drains and "
                       "no prepass on either context");
            fx_cap_shape("the opt-in candidate's main context has no "
                         "glWaitSync", "glWaitSync", NULL);
            end_case();
        }
    } else if (strcmp(mode, "prod") == 0) {
        /* v0.4 THE PRODUCTION SELECTION, ONE CASE PER PROCESS.
         *
         *   "row_on"    nothing named: a row-admitted walk takes the auxiliary
         *               GPU_SOURCE arm with FENCE
         *   "row_off"   nothing named and the caller never asks: the original
         *               renderer, with no context, no draw and no GL call
         *   "transition" OFF -> ON -> OFF -> ON across four walks in one
         *               process, with the two off walks adding nothing
         *   "clip_off"  SL_BG_TF_CLIP=0: a developer takes the prepass out of
         *               the launch and the row may not put it back
         *   "arm_off"   SL_BG_TF_AUX_REAL=OFF with no developer switch: the
         *               MAIN-CONTEXT diagnostic is NOT what the row reaches
         *   "arm_bad"   an UNREADABLE SL_BG_TF_AUX_REAL with no developer
         *               switch: a typo selects nothing at all
         *   "diag"      SL_BG_TF_RASTER_CPU=1 with no developer switch: a
         *               main-context diagnostic stands the row's walks down
         *   "sync_bad"  an UNREADABLE SL_BG_TF_AUX_SYNC with no developer
         *               switch: the one shape that keeps the walk ADMITTED and
         *               refuses every served draw to the CPU clip path
         *   "cap"       nothing named and an entry point WITHHELD: a real
         *               capability failure, refused at READINESS with the state
         *               latched UNAVAILABLE, which is the other truthful
         *               refusal and must not read as sync_bad's
         *
         * THE LAUNCH IS VERIFIED BEFORE THE CASE RUNS, exactly as mode "optin"
         * verifies its own: each case is one set of switches, the arbiter
         * decides once per process, and a case driven under the wrong launch
         * REFUSES rather than reporting another launch's numbers under its own
         * name. The row itself is modelled by whether the case ASKS, because
         * the module deliberately does not read it. */
        const char *which = argc >= 3 ? argv[2] : "row_on";
        const int clip = sl_bgtf_opt_in();
        const int rc   = sl_bgtf_raster_cpu();
        const char *ar_env   = getenv("SL_BG_TF_AUX_REAL");
        const char *fx_env   = getenv("SL_BG_TF_AUX_SYNC");
        const char *clip_env = getenv("SL_BG_TF_CLIP");
        const int ar_named = ar_env != NULL && ar_env[0] != '\0';
        const int fx_named = fx_env != NULL && fx_env[0] != '\0';
        const int clip_zero = clip_env != NULL && strcmp(clip_env, "0") == 0;
        int ok;

        if (strcmp(which, "row_on") == 0 || strcmp(which, "row_off") == 0
            || strcmp(which, "transition") == 0 || strcmp(which, "cap") == 0)
            ok = clip == 0 && !clip_zero && !ar_named && !fx_named && rc == 0;
        else if (strcmp(which, "clip_off") == 0)
            ok = clip == 0 && clip_zero && !ar_named && !fx_named && rc == 0;
        else if (strcmp(which, "arm_off") == 0)
            ok = clip == 0 && !clip_zero && ar_named && !fx_named && rc == 0;
        else if (strcmp(which, "arm_bad") == 0)
            ok = clip == 0 && !clip_zero && ar_named && !fx_named && rc == 0;
        else if (strcmp(which, "diag") == 0)
            ok = clip == 0 && !clip_zero && !ar_named && !fx_named && rc == 1;
        else if (strcmp(which, "sync_bad") == 0)
            ok = clip == 0 && !clip_zero && !ar_named && fx_named && rc == 0;
        else {
            printf("REFUSING: mode prod takes row_on, row_off, transition, "
                   "clip_off, arm_off, arm_bad, diag, sync_bad or cap, not "
                   "\"%s\".\n", which);
            return 2;
        }
        if (!ok) {
            printf("REFUSING: mode prod %s was driven under the wrong launch "
                   "(clip %d, clip named 0 %d, arm named %d, sync named %d, "
                   "raster_cpu %d). Each case is one set of switches and the "
                   "arbiter decides once per process.\n", which, clip,
                   clip_zero, ar_named, fx_named, rc);
            return 2;
        }
        sl_bgtfgl_reset();
        if (strcmp(which, "row_on") == 0)            prod_row_on_case();
        else if (strcmp(which, "row_off") == 0)      prod_row_off_case();
        else if (strcmp(which, "transition") == 0)   prod_transition_case();
        else if (strcmp(which, "clip_off") == 0)
            prod_standdown_case("SL_BG_TF_CLIP=0",
                                "SL_BG_TF_CLIP=0 takes the prepass out of the "
                                "launch: the row may not put it back, and no "
                                "prepass takes the walk");
        else if (strcmp(which, "arm_off") == 0)
            prod_standdown_case("SL_BG_TF_AUX_REAL=OFF",
                                "an OFF token with no developer switch: the "
                                "known-mismatching MAIN-CONTEXT prepass is NOT "
                                "what the row reaches - no prepass takes the "
                                "walk, and the arm stays reachable only with "
                                "SL_BG_TF_CLIP=1");
        else if (strcmp(which, "arm_bad") == 0)
            prod_standdown_case("an unreadable SL_BG_TF_AUX_REAL",
                                "an UNREADABLE arm with no developer switch: a "
                                "typo selects nothing at all, and certainly "
                                "not a known mismatch");
        else if (strcmp(which, "diag") == 0)
            prod_standdown_case("SL_BG_TF_RASTER_CPU=1",
                                "a MAIN-CONTEXT diagnostic with no developer "
                                "switch stands the row's walks down, rather "
                                "than handing them the prepass the diagnostic "
                                "is about");
        else if (strcmp(which, "cap") == 0)          prod_capability_case();
        else                                         prod_sync_closed_case();
    } else if (strcmp(mode, "envax") == 0) {
        /* The switch's grammar, coupling, exclusivity and refusal accounting,
         * one setting per launch. The runner says what the control must ANSWER
         * and whether this launch asked for it and did not get it. */
        mode_env_aux_empty((argc >= 3 && strcmp(argv[2], "1") == 0) ? 1 : 0,
                           (argc >= 4 && strcmp(argv[3], "1") == 0) ? 1 : 0);
    } else if (strcmp(mode, "envnp") == 0) {
        /* The switch's grammar, coupling, exclusivity and refusal accounting,
         * one setting per launch. The runner says what the control must ANSWER
         * and whether this launch asked for it and did not get it. */
        mode_env_neutral_prog((argc >= 3 && strcmp(argv[2], "1") == 0) ? 1 : 0,
                              (argc >= 4 && strcmp(argv[3], "1") == 0) ? 1 : 0);
    } else if (strcmp(mode, "dump") == 0) {
        /* ONE SCENARIO PER PROCESS. The switch is parsed once and the one
         * capture attempt is spent for the life of the process, both on
         * purpose, so each scenario is its own run of this binary with its own
         * selector and its own scratch output directory. */
        const char *which = argc >= 3 ? argv[2] : "off";
        if (strcmp(which, "off") == 0)             d_mode_off();
        else if (strcmp(which, "sel") == 0)        d_mode_sel();
        else if (strcmp(which, "clock") == 0)      d_mode_clock();
        else if (strcmp(which, "far0") == 0)       d_mode_far0();
        else if (strcmp(which, "capacity") == 0)   d_mode_capacity();
        else if (strcmp(which, "ulp") == 0
                 || strcmp(which, "far1z") == 0)   d_mode_unequal(which);
        else if (strcmp(which, "disarm") == 0)
            d_mode_disarm(argc >= 4 ? argv[3] : "(unnamed)");
        else                                       d_mode_fail(which);
    } else {
        /* The full suite needs the switch ON: everything below is about what
         * the candidate does once selected, and the off selection has its own
         * runs of this binary. */
        sl_bgtfgl_reset();
        CHECK(sl_bgtf_opt_in() == 1,
              "SL_BG_TF_CLIP=1 is required for the main suite - run it through "
              "tools/windows/bgtftest.ps1");
        CHECK(sl_bgtf_state() == 0, "the module starts UNTRIED (%d)", sl_bgtf_state());
        CHECK(tf.calls == 0 && tf.proc_calls == 0,
              "and nothing has touched the fake driver yet");
        if (sl_bgtf_opt_in() != 1) {
            printf("\nREFUSING TO REPORT COVERAGE: the switch is off, so the suite below "
                   "would be measuring the off path under the wrong name.\n");
            return 2;
        }

        st_ready();
        st_walk();
        st_indexing();
        st_bounds();
        st_reuse();
        st_depth();
        st_raster();
        st_restore();
        st_proc_failures();
        st_object_failures();
        st_alloc_failure();
        st_alloc_verified();
        st_slot_refills();
        st_context();
        st_watchdogs();
        st_refusals();
        st_slots();
        st_counters();
        st_no_sync();
        st_state_only_off();
        st_pos_input_off();
        st_zero_count_off();
        st_empty_state_off();
        pr_table();
        st_post_reset_off();
        st_neutral_prog_off();
        st_aux_empty_off();
        st_aux_real_off();
    }

    remove(path);
    printf("\n%d cases, %d checks, %d failed\n", cases, checks, fails);
    printf("NOT COVERED: the prepass program's own arithmetic. The GLSL in "
           "src/gfx/sl_gfx_bgtf.c is handed to glShaderSource as text and recorded; it "
           "is never compiled or executed here, because this process has no GLSL "
           "compiler and no GPU. So what the program computes, whether a driver's "
           "transform feedback writes the pool in the canonical order these indices "
           "then read, whether the recorded varying's four floats reach the fixed "
           "pipeline as a clip position, and whether the resulting image matches the "
           "original path, are all unqualified. The one blue byte at pixel (71,113) "
           "that motivates the candidate is not addressed by anything above.\n");
    return fails == 0 ? 0 : 1;
}
