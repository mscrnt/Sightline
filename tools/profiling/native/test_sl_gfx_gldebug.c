/* Offline tests for the opt-in GL debug-output diagnostic
 * (src/gfx/sl_gfx_gldebug.c).
 *
 * WHAT IS REAL IN THESE TESTS. The module itself: this file #includes
 * src/gfx/sl_gfx_gldebug.c, so every decision under test is the product's own
 * text, including its file statics - which is the only way the per-process
 * message cap and the install/shutdown lifecycle can be driven at all, and it
 * means the product carries no test hook and no second compilation mode. The
 * REAL GL HEADERS are included too (<GL/gl.h> and <GL/glext.h> from this same
 * MSYS2 tree), not for calling anything but so that the module's transcribed
 * callback types can be checked against Khronos's own GLDEBUGPROC and
 * PFNGLDEBUGMESSAGECALLBACKPROC by assignment, with no cast anywhere.
 *
 * WHAT IS SYNTHETIC. Every platform call. struct sl_gldebug_host is filled with
 * functions in this file that RECORD what they were asked and answer what a
 * scenario chose, so each of the module's refusal paths is reachable without a
 * driver that has to misbehave on cue. No window, no GL context, no SDL, no
 * GPU: nothing here links SDL2 or opengl32 and no GL function is ever called.
 *
 * WHAT THEY THEREFORE DO NOT ESTABLISH, and it is the larger half:
 *   - nothing about any real driver. Whether a given driver grants a debug
 *     context, whether it resolves glDebugMessageCallback, what it reports and
 *     whether it reports anything at all are the questions the fail-closed
 *     paths exist for, and they stay unmeasured here;
 *   - nothing about the v0.4 renderer. A diagnostic that reports nothing
 *     exonerates no code, and a diagnostic that reports something has not
 *     located a defect - the selector changes the driver's own behaviour, so
 *     a run under it is not the run being asked about;
 *   - nothing about the ABI beyond the compiler's own type check. That the
 *     callback is __stdcall is a COMPILE-TIME property here (the no-cast
 *     assignments below, under -Werror=incompatible-pointer-types); whether a
 *     real driver's call through it unwinds correctly is not exercised.
 *
 * EVERY CHECK HERE CAN FAIL. The negatives are listed with the suite and each
 * one drives a path that would otherwise be reachable only by a driver fault:
 * a malformed selector, a granted flag that is absent, an unreadable flag
 * source, a NULL entry point, a message storm past the cap, a reversed teardown
 * order, and a callback that reaches for the platform.
 *
 * Usage: test_sl_gfx_gldebug.exe <mode>     (modes: parse, arm, grant, lookup,
 *        callback, enable, lifecycle, default, format)
 * Exits 0 when the mode passes, 1 otherwise, printing one FAIL line per
 * failure. One mode per process because the module's cap counter, its armed
 * flag and its bound-callback state have process lifetime by construction.
 */

/* The real GL headers, for their TYPES only. -D_GDI32_ makes mingw's WINGDIAPI
 * empty (mingw32/include/wingdi.h:13-16) so gl.h's entry points are declared as
 * ordinary symbols rather than imports from opengl32 - the same device
 * tools/windows/bgtfseamtest.ps1 uses - which keeps this executable free of any
 * GL import even though the declarations are visible. None is referenced. */
#include <GL/gl.h>
#include <GL/glext.h>

/* And SDL2's video header, for SDL_GLcontextFlag (SDL_video.h:295-301) - again
 * for the values only, so an assertion about the debug-flag bit cannot agree
 * with a transcription error in the module. This header declares functions and
 * defines no main, so nothing links SDL2. */
#include <SDL2/SDL_video.h>

#include <stdio.h>
#include <string.h>

/* The module's own text. Its statics are this file's to reset between
 * scenarios, which is what makes the per-process properties drivable. */
#include "../../src/gfx/sl_gfx_gldebug.c"

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

/* ---- COMPILE-TIME: THE CALLBACK TYPES ARE KHRONOS'S --------------------
 *
 * These four initialisations carry no cast. Each is a hard compile error under
 * -Werror=incompatible-pointer-types if the module's transcribed types have
 * drifted from the registry header's in any respect the compiler can see -
 * parameter count, any parameter type, the return type, or the CALLING
 * CONVENTION, which on Win32 is the one that matters: a __cdecl callback handed
 * to glDebugMessageCallback would corrupt the stack on return, and nothing at
 * run time would say so clearly.
 *
 * The pair in each direction is deliberate: one proves the module's callback is
 * acceptable where a GLDEBUGPROC is wanted, the other proves a GLDEBUGPROC is
 * acceptable where the module's type is wanted. Only both together rule out a
 * type that is merely assignable one way.
 *
 * mock_dmc below has the registry's PFNGLDEBUGMESSAGECALLBACKPROC signature
 * written out with the real GLDEBUGPROC in it, and is assigned into the
 * module's setter typedef without a cast - so the type the module casts a
 * resolved void* to is checked against the registry type too. */
/* Written out with the REGISTRY's own types (GLenum, GLuint, GLsizei, GLchar,
 * APIENTRY) so that assigning it into the module's typedef is a check of the
 * module's transcription and not of this file's. It is never called. */
static void APIENTRY khronos_shaped_cb(GLenum source, GLenum type, GLuint id,
                                       GLenum severity, GLsizei length,
                                       const GLchar *message,
                                       const void *userParam)
{
    (void) source; (void) type; (void) id; (void) severity;
    (void) length; (void) message; (void) userParam;
}

static GLDEBUGPROC          s_khronos_from_module = sl_gldebug_cb;
static sl_gldebug_proc_t    s_module_from_khronos = khronos_shaped_cb;

static void APIENTRY mock_dmc(GLDEBUGPROC cb, const void *user);
static sl_gldebug_setter_t  s_setter_is_khronos_pfn = mock_dmc;
static PFNGLDEBUGMESSAGECALLBACKPROC s_khronos_pfn_from_module = mock_dmc;

/* ---- THE RECORDING PLATFORM --------------------------------------------
 *
 * Every member of struct sl_gldebug_host lands here. `calls` counts platform
 * calls of ANY kind, which is what makes "the callback touches no platform"
 * measurable: the callback is invoked with this host installed and the counter
 * must not move. `say` is counted separately, because saying a line is the one
 * thing the callback may do. */
#define SAY_MAX  80
#define ORDER_MAX 32
#define LINE_KEEP 600

/* ---- the GL tokens the mocks and the assertions name, from the real headers
 *
 * Taken from <GL/glext.h> above rather than retyped, so an assertion about a
 * numeric value cannot agree with a transcription error in the module. That is
 * the point of including the registry header in a file that calls no GL. */
#define T_DEBUG_OUTPUT       ((unsigned int) GL_DEBUG_OUTPUT)
#define T_DEBUG_SYNC         ((unsigned int) GL_DEBUG_OUTPUT_SYNCHRONOUS)
#define T_DEBUG_BIT          ((unsigned int) GL_CONTEXT_FLAG_DEBUG_BIT)

static struct {
    /* answers a scenario chooses */
    int  sdl_get_rc;          /* SDL_GL_GetAttribute return code              */
    int  sdl_get_rc_after;    /* -1 never, else reads after this many succeed  */
                              /* start failing - which is how a readback is    */
                              /* made to fail only AFTER a write was attempted */
    int  sdl_get_value;       /* ... and the flags it writes                  */
    int  sdl_set_rc;          /* return code of EVERY set                     */
    int  sdl_set_stores_on_fail;  /* a set that REPORTS failure stores anyway  */
    int  sdl_set_rc_second;   /* -2 unset, else the rc of the 2nd set (the     */
                              /* restore attempt), so a failed restore is      */
                              /* reachable while the first set still fails     */
    int  gl_get_rc;           /* -1: GL_CONTEXT_FLAGS unreadable              */
    unsigned int gl_get_bits;
    int  proc_null;           /* glDebugMessageCallback does not resolve      */
    /* per-capability enable/readback behaviour, each independently scriptable:
     * en_rc[] is what gl_enable_verified answers, ise_rc[] whether a later
     * glIsEnabled can be trusted, ise_on[] what it reports. Indexed by cap. */
    int  en_rc_sync, en_rc_out;
    int  ise_rc_sync, ise_rc_out;
    int  ise_on_sync, ise_on_out;

    /* what happened */
    long calls;               /* every platform call except say               */
    long sdl_gets, sdl_sets, gl_gets, procs, enables, disables, isenables;
    int  last_set_flags;
    int  set_flags[ORDER_MAX];
    int  n_set;
    unsigned int enabled[ORDER_MAX];
    int  n_enabled;
    unsigned int disabled[ORDER_MAX];
    int  n_disabled;
    char order[ORDER_MAX][8]; /* "cbset", "cbclr", "dis" - the teardown order */
    int  n_order;
    char said[SAY_MAX][LINE_KEEP];
    int  n_said;
    /* what the module bound, and what mock_dmc was handed */
    GLDEBUGPROC bound;
    long bind_calls, unbind_calls;
    /* the attribute the fake SDL actually holds, so a readback is an
     * observation of stored state rather than a value the test hands back */
    int  attr;
} mk;

static void mock_reset(void)
{
    memset(&mk, 0, sizeof mk);
    mk.gl_get_rc = 0;         /* readable, no debug bit, unless a test says so */
    mk.sdl_get_rc_after = -1;
    mk.sdl_set_rc_second = -2;
    /* the healthy platform: every enable takes and reads back on */
    mk.en_rc_sync = SL_GLDEBUG_EN_OK;
    mk.en_rc_out  = SL_GLDEBUG_EN_OK;
    mk.ise_on_sync = 1;
    mk.ise_on_out  = 1;
    /* and the module's process state, which this file owns because it compiled
     * the module's text */
    s_armed = 0; s_active = 0; s_msgs = 0; s_set_cb = NULL; s_say = NULL;
}

static void note_order(const char *what)
{
    if (mk.n_order < ORDER_MAX) {
        strncpy(mk.order[mk.n_order], what, sizeof mk.order[0] - 1);
        mk.order[mk.n_order][sizeof mk.order[0] - 1] = '\0';
        mk.n_order++;
    }
}

/* The fake SDL holds the attribute, so a readback OBSERVES what a set left
 * behind. That is what makes "a set that reported failure stored anyway", and
 * the restore that follows it, scenarios rather than scripted answers. */
static int mock_sdl_get(int *flags)
{
    mk.calls++; mk.sdl_gets++;
    if (mk.sdl_get_rc != 0) return mk.sdl_get_rc;
    if (mk.sdl_get_rc_after >= 0 && mk.sdl_gets > mk.sdl_get_rc_after) return -1;
    *flags = mk.attr | mk.sdl_get_value;
    return 0;
}

static int mock_sdl_set(int flags)
{
    int rc;

    mk.calls++; mk.sdl_sets++;
    mk.last_set_flags = flags;
    if (mk.n_set < ORDER_MAX) mk.set_flags[mk.n_set++] = flags;
    rc = (mk.sdl_sets == 2 && mk.sdl_set_rc_second != -2)
         ? mk.sdl_set_rc_second : mk.sdl_set_rc;
    /* A set that SUCCEEDS stores. A set that FAILS stores only when the
     * scenario says the platform's return code and its effect disagree. */
    if (rc == 0 || mk.sdl_set_stores_on_fail) mk.attr = flags;
    return rc;
}

static int mock_gl_get(unsigned int *bits)
{
    mk.calls++; mk.gl_gets++;
    if (mk.gl_get_rc != 0) return mk.gl_get_rc;
    *bits = mk.gl_get_bits;
    return 0;
}

static int mock_enable_verified(unsigned int cap)
{
    mk.calls++; mk.enables++;
    if (mk.n_enabled < ORDER_MAX) mk.enabled[mk.n_enabled++] = cap;
    note_order("en");
    return (cap == T_DEBUG_SYNC) ? mk.en_rc_sync : mk.en_rc_out;
}

static int mock_is_enabled(unsigned int cap, int *on)
{
    mk.calls++; mk.isenables++;
    note_order("ise");
    if (cap == T_DEBUG_SYNC) {
        if (mk.ise_rc_sync != 0) return mk.ise_rc_sync;
        *on = mk.ise_on_sync;
    } else {
        if (mk.ise_rc_out != 0) return mk.ise_rc_out;
        *on = mk.ise_on_out;
    }
    return 0;
}

/* The registry's own signature, written out with the real GLDEBUGPROC - which
 * is what makes the no-cast assignment into sl_gldebug_setter_t above a check
 * of the module's type rather than of this file's. */
static void APIENTRY mock_dmc(GLDEBUGPROC cb, const void *user)
{
    (void) user;
    mk.calls++;
    if (cb == NULL) { mk.unbind_calls++; note_order("cbclr"); }
    else            { mk.bound = cb; mk.bind_calls++; note_order("cbset"); }
}

static void *mock_proc(const char *name)
{
    mk.calls++; mk.procs++;
    if (mk.proc_null) return NULL;
    if (strcmp(name, "glDebugMessageCallback") == 0) return (void *) mock_dmc;
    return NULL;
}

static void mock_disable(unsigned int cap)
{
    mk.calls++; mk.disables++;
    if (mk.n_disabled < ORDER_MAX) mk.disabled[mk.n_disabled++] = cap;
    note_order("dis");
}

static void mock_say(const char *line)
{
    if (mk.n_said < SAY_MAX) {
        strncpy(mk.said[mk.n_said], line, LINE_KEEP - 1);
        mk.said[mk.n_said][LINE_KEEP - 1] = '\0';
        mk.n_said++;
    }
}

static const struct sl_gldebug_host HOST = {
    mock_sdl_get, mock_sdl_set, mock_gl_get, mock_proc,
    mock_enable_verified, mock_is_enabled, mock_disable, mock_say
};

/* Is `needle` a substring of any line said so far? */
static int said_has(const char *needle)
{
    int i;
    for (i = 0; i < mk.n_said; i++)
        if (strstr(mk.said[i], needle) != NULL) return 1;
    return 0;
}

/* The recorded call sequence, joined, so a whole ordering is one assertion.
 * "en" an enable-and-prove, "ise" a readback, "cbset"/"cbclr" the bind and the
 * clear, "dis" a disable. */
static const char *order_str(void)
{
    static char buf[ORDER_MAX * 8];
    int i, n = 0;

    buf[0] = '\0';
    for (i = 0; i < mk.n_order && n < (int) sizeof buf - 10; i++) {
        if (i > 0) buf[n++] = ',';
        strcpy(buf + n, mk.order[i]);
        n += (int) strlen(mk.order[i]);
    }
    buf[n] = '\0';
    return buf;
}

/* THE STRUCTURAL PROPERTY the install's ordering exists for: the callback is
 * never bound before BOTH capabilities have been enabled and proved. Checked
 * against the recorded sequence rather than against the source, so a reordering
 * of install's statements is what fails it. */
static int bind_after_two_enables(void)
{
    int i, ens = 0;

    for (i = 0; i < mk.n_order; i++) {
        if (strcmp(mk.order[i], "en") == 0) ens++;
        if (strcmp(mk.order[i], "cbset") == 0) return ens >= 2;
    }
    return 1;                           /* never bound: vacuously true */
}

/* Did any line said so far contain this text - used to prove a selector value
 * is NOT echoed into the log. */
static int said_leaks(const char *raw)
{
    return said_has(raw);
}

/* ======================== MODE: parse ==================================== */
/* The grammar, exactly. The negatives are the whole point: every one of them
 * would arm the diagnostic under a prefix match, a strtol parse, an atoi, or a
 * "first character is 1" test - which is what the older house helper and
 * several selectors in this tree do in one form or another. */
static int mode_parse(void)
{
    static const char *malformed[] = {
        "2", "01", "1 ", " 1", "1x", "11", "10", "x1", "true", "TRUE", "yes",
        "on", "ON", "1\t", "\t1", "1\n", "-1", "+1", "1.0", "0x1", "00", "0 ",
        " ", "1;1"
    };
    unsigned int i;

    CHECK(sl_gldebug_parse(NULL) == SL_GLDEBUG_SEL_ABSENT,
          "an unset variable is not ABSENT");
    CHECK(sl_gldebug_parse("") == SL_GLDEBUG_SEL_OFF,
          "an empty value is not an explicit OFF");
    CHECK(sl_gldebug_parse("1") == SL_GLDEBUG_SEL_ARMED,
          "\"1\" does not arm");
    CHECK(sl_gldebug_parse("0") == SL_GLDEBUG_SEL_OFF,
          "\"0\" is not an explicit OFF");
    for (i = 0; i < sizeof malformed / sizeof malformed[0]; i++)
        CHECK(sl_gldebug_parse(malformed[i]) == SL_GLDEBUG_SEL_MALFORMED,
              "value %u of the malformed set is not reported MALFORMED", i);
    printf("  parse: 1 arms, unset/empty/0 are silent, %u malformed forms refuse\n",
           (unsigned) (sizeof malformed / sizeof malformed[0]));
    return g_fail == 0;
}

/* ======================== MODE: arm ====================================== */
/* The pre-creation half: what is requested, what is said, and - for the two
 * silent verdicts - that NOTHING is requested and nothing is said. */
static int mode_arm(void)
{
    int r;

    /* absent: no platform call, no line. The default launch's own property. */
    mock_reset();
    r = sl_gldebug_arm(NULL, &HOST);
    CHECK(r == 0, "an unset selector armed");
    CHECK(mk.calls == 0, "an unset selector made %ld platform call(s)", mk.calls);
    CHECK(mk.n_said == 0, "an unset selector printed %d line(s)", mk.n_said);

    /* explicit off: identically silent */
    mock_reset();
    r = sl_gldebug_arm("0", &HOST);
    CHECK(r == 0, "an explicit off armed");
    CHECK(mk.calls == 0 && mk.n_said == 0,
          "an explicit off made %ld call(s) and printed %d line(s)",
          mk.calls, mk.n_said);

    /* malformed: refused, said once, and the VALUE IS NOT ECHOED */
    mock_reset();
    r = sl_gldebug_arm("tRuE", &HOST);
    CHECK(r == 0, "a malformed selector armed");
    CHECK(mk.calls == 0,
          "a malformed selector made %ld platform call(s)", mk.calls);
    CHECK(mk.n_said == 1, "a malformed selector said %d line(s), expected 1",
          mk.n_said);
    CHECK(said_has("malformed"), "the refusal does not name the verdict");
    CHECK(!said_leaks("tRuE"), "the refusal ECHOED the selector's value");

    /* armed: the debug bit is requested, exactly once, and REQUESTED is said
     * without any claim that it was granted */
    mock_reset();
    r = sl_gldebug_arm("1", &HOST);
    CHECK(r == 1, "an exact \"1\" did not arm");
    CHECK(mk.sdl_sets == 1, "the flag was set %ld time(s), expected 1",
          mk.sdl_sets);
    CHECK((mk.last_set_flags & SDL_GL_CONTEXT_DEBUG_FLAG) != 0,
          "the requested flags 0x%x carry no debug bit", mk.last_set_flags);
    CHECK(mk.n_said == 1, "arming said %d line(s), expected 1", mk.n_said);
    CHECK(said_has("REQUESTED"), "arming does not say REQUESTED");
    CHECK(!said_has("ACTIVE"), "arming claimed ACTIVE before any check");
    CHECK(said_has("evidence") || said_has("itself change"),
          "arming does not record that a debug context changes the subject");

    /* the bit is OR-ed into what a launch already asked for, never assigned */
    mock_reset();
    mk.sdl_get_value = SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
    (void) sl_gldebug_arm("1", &HOST);
    CHECK((mk.last_set_flags & SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG) != 0,
          "an unrelated requested flag was DROPPED (set 0x%x)",
          mk.last_set_flags);
    CHECK((mk.last_set_flags & SDL_GL_CONTEXT_DEBUG_FLAG) != 0,
          "the debug bit is absent from 0x%x", mk.last_set_flags);

    /* ---- F2 / r764c1: AN UNREADABLE ATTRIBUTE REFUSES, AND SETS NOTHING.
     *
     * The previous round defaulted the flags to zero and requested the debug
     * bit alone, which would have CLEARED any other requested flag this module
     * cannot see. There is no safe zero here: not knowing what is there means
     * not knowing what would be lost, so no set call is made at all. */
    mock_reset();
    mk.sdl_get_rc = -1;
    CHECK(sl_gldebug_arm("1", &HOST) == 0,
          "an unreadable context-flags attribute still armed");
    CHECK(mk.sdl_sets == 0,
          "an unreadable attribute still made %ld set call(s) - a zero fallback"
          " would clobber flags this module cannot see", mk.sdl_sets);
    CHECK(said_has("REFUSED") && said_has("could not be read"),
          "the read failure was not reported with its reason");
    CHECK(!said_has("REQUESTED"),
          "a refused arm still claimed REQUESTED");
    /* and the install that follows is INERT: nothing resolved, nothing enabled */
    mk.gl_get_bits = T_DEBUG_BIT;
    mk.calls = 0; mk.n_said = 0;
    CHECK(sl_gldebug_install(&HOST) == 0,
          "install activated after an arm that refused on a read failure");
    CHECK(mk.calls == 0 && mk.n_said == 0,
          "that install made %ld call(s) and said %d line(s)",
          mk.calls, mk.n_said);

    /* ---- F2 / r764c1: A FAILED SET REFUSES ARMING.
     *
     * A request that could not be made is not a request. The previous round
     * ignored the return code and said REQUESTED regardless. */
    mock_reset();
    mk.sdl_set_rc = -1;
    CHECK(sl_gldebug_arm("1", &HOST) == 0, "a failed attribute set still armed");
    CHECK(!said_has("REQUESTED") && !said_has("ACTIVE"),
          "a failed attribute set still claimed REQUESTED or ACTIVE");
    CHECK(said_has("could not be requested"),
          "the set failure was not reported with its reason");
    CHECK(said_has("unchanged"),
          "a failed set that stored nothing did not report the attribute as"
          " unchanged");
    /* install after it is inert too */
    mk.gl_get_bits = T_DEBUG_BIT;
    mk.calls = 0;
    CHECK(sl_gldebug_install(&HOST) == 0,
          "install activated after an arm that refused on a set failure");
    CHECK(mk.calls == 0, "that install made %ld call(s)", mk.calls);

    /* A FAILED SET THAT STORED ANYWAY is put back and proved by readback. SDL
     * keeps this attribute in process-wide state, so its return code and its
     * effect are independent; a stored debug bit would hand this launch a debug
     * context with nothing reporting on it. */
    mock_reset();
    mk.sdl_set_rc = -1;
    mk.sdl_set_stores_on_fail = 1;
    mk.sdl_set_rc_second = 0;           /* the restore itself succeeds */
    CHECK(sl_gldebug_arm("1", &HOST) == 0, "a failed-but-stored set armed");
    CHECK(mk.sdl_sets == 2,
          "the restore was attempted %ld time(s), expected one set then one"
          " restore", mk.sdl_sets);
    CHECK((mk.attr & SDL_GL_CONTEXT_DEBUG_FLAG) == 0,
          "the attribute still carries the debug bit (0x%x) after the restore",
          mk.attr);
    CHECK(said_has("put back"), "the restore was not reported");

    /* AND A RESTORE THAT CANNOT BE MADE IS SAID OUT LOUD, because the launch
     * may then still receive a debug context nothing reports on. */
    mock_reset();
    mk.sdl_set_rc = -1;
    mk.sdl_set_stores_on_fail = 1;      /* every set stores, every set fails */
    CHECK(sl_gldebug_arm("1", &HOST) == 0, "an unrestorable attribute armed");
    CHECK(said_has("could NOT be put back"),
          "an unrestorable attribute was not reported as such: \"%s\"",
          mk.n_said > 0 ? mk.said[mk.n_said - 1] : "");

    /* a platform that cannot set attributes at all refuses rather than arming */
    {
        struct sl_gldebug_host noset = HOST;
        mock_reset();
        noset.sdl_set_ctx_flags = NULL;
        CHECK(sl_gldebug_arm("1", &noset) == 0,
              "a platform with no attribute setter armed anyway");
        CHECK(said_has("REFUSED"), "that platform's refusal was not reported");
    }
    /* and one that cannot READ them either - the read is now mandatory */
    {
        struct sl_gldebug_host noget = HOST;
        mock_reset();
        noget.sdl_get_ctx_flags = NULL;
        CHECK(sl_gldebug_arm("1", &noget) == 0,
              "a platform with no attribute getter armed anyway");
        CHECK(mk.sdl_sets == 0, "it still made %ld set call(s)", mk.sdl_sets);
    }
    printf("  arm: silent when unset/off, refuses malformed without echoing it,"
           " OR-s the debug bit, refuses on either SDL failure without setting"
           " anything, restores a failed-but-stored set, never says ACTIVE\n");
    return g_fail == 0;
}

/* ======================== MODE: grant =================================== */
/* REQUESTED versus GRANTED - the property the review singles out, and the one
 * r764c1 corrects. The context's own GL_CONTEXT_FLAGS is THE ONLY SOURCE. SDL's
 * attribute is advisory: it stores what was asked for, so accepting it as a
 * fallback would make the grant check confirm this module's own request. An
 * unreadable GL query refuses, whatever SDL says. */
static int mode_grant(void)
{
    /* granted: the context's own flags carry the bit */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = T_DEBUG_BIT;
    CHECK(sl_gldebug_install(&HOST) == 1, "a granted debug context was REFUSED");
    CHECK(said_has("ACTIVE"), "a granted context did not report ACTIVE");
    CHECK(mk.gl_gets == 1, "GL_CONTEXT_FLAGS was read %ld time(s), expected 1",
          mk.gl_gets);

    /* NOT granted: readable, bit absent. The driver declined; nothing may
     * report ACTIVE, nothing may be enabled, nothing may be bound. */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = 0u;
    CHECK(sl_gldebug_install(&HOST) == 0,
          "a context granted WITHOUT the debug bit reported active");
    CHECK(said_has("REFUSED"), "a declined request was not reported REFUSED");
    CHECK(!said_has("ACTIVE"), "a declined request produced an ACTIVE claim");
    CHECK(mk.enables == 0, "a declined request still enabled %ld capability(ies)",
          mk.enables);
    CHECK(mk.bind_calls == 0, "a declined request still bound a callback");
    CHECK(mk.procs == 0,
          "a declined request still resolved %ld entry point(s)", mk.procs);

    /* a NEIGHBOURING bit is not the debug bit */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = (unsigned int) GL_CONTEXT_FLAG_ROBUST_ACCESS_BIT_ARB;
    CHECK(sl_gldebug_install(&HOST) == 0,
          "an unrelated context flag was read as a debug grant");

    /* ---- F1 / r764c1, THE CORRECTED CASE AND THE NEW NEGATIVE.
     *
     * GL unreadable while SDL ECHOES THE REQUEST. The previous round ACTIVATED
     * here, on an answer that was its own request coming back. It must refuse,
     * resolve nothing, enable nothing and bind nothing - and the line must say
     * in words that SDL's agreement is the request, not a grant. */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_rc = -1;
    mk.sdl_get_value = SDL_GL_CONTEXT_DEBUG_FLAG;
    mk.n_said = 0;
    CHECK(sl_gldebug_install(&HOST) == 0,
          "an unreadable GL grant ACTIVATED on SDL's echo of the request");
    CHECK(said_has("REFUSED"), "that case was not reported REFUSED");
    CHECK(!said_has("ACTIVE"), "that case produced an ACTIVE claim");
    CHECK(said_has("REQUEST"),
          "the refusal does not label SDL's answer as the request: \"%s\"",
          mk.n_said > 0 ? mk.said[0] : "");
    CHECK(mk.procs == 0, "it still resolved %ld entry point(s)", mk.procs);
    CHECK(mk.enables == 0, "it still enabled %ld capability(ies)", mk.enables);
    CHECK(mk.bind_calls == 0, "it still bound a callback");
    CHECK(mk.disables == 0,
          "it disabled %ld capability(ies) although it had changed none",
          mk.disables);
    CHECK(mk.n_said == 1, "it said %d line(s), expected 1", mk.n_said);

    /* GL unreadable and SDL says no: refused, same single source */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_rc = -1;
    mk.sdl_get_value = 0;
    CHECK(sl_gldebug_install(&HOST) == 0,
          "an unreadable GL grant activated");

    /* NO source readable at all: refused, with its own reason */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_rc = -1;
    mk.sdl_get_rc_after = 1;            /* the arm's read works, the install's fails */
    CHECK(sl_gldebug_install(&HOST) == 0,
          "an unverifiable grant reported active");
    CHECK(said_has("could not be read"),
          "an unverifiable grant was not reported as such");
    CHECK(mk.enables == 0 && mk.bind_calls == 0,
          "an unverifiable grant still changed debug state");

    /* AND SDL IS NEVER THE DECIDER THE OTHER WAY EITHER: the GL query says
     * granted while SDL's record carries no debug bit. The grant stands. */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = T_DEBUG_BIT;
    mk.attr = 0; mk.sdl_get_value = 0;  /* SDL disagrees */
    CHECK(sl_gldebug_install(&HOST) == 1,
          "a GL-proved grant was refused because SDL's record disagreed");

    printf("  grant: ONLY the context's own GL_CONTEXT_FLAGS activates; a"
           " declined bit, an unrelated bit, an unreadable query and an"
           " unreadable query with SDL echoing the request each refuse\n");
    return g_fail == 0;
}

/* ======================== MODE: lookup ================================== */
/* The entry point. A granted debug context whose glDebugMessageCallback does
 * not resolve must FAIL CLOSED - and in particular must not enable
 * GL_DEBUG_OUTPUT, which without a bound callback only makes the driver fill a
 * log nothing reads, at whatever that costs. */
static int mode_lookup(void)
{
    struct sl_gldebug_host h;

    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = T_DEBUG_BIT;
    mk.proc_null = 1;
    CHECK(sl_gldebug_install(&HOST) == 0,
          "an unresolved entry point reported active");
    CHECK(mk.procs == 1, "the entry point was looked up %ld time(s)", mk.procs);
    CHECK(mk.enables == 0,
          "an unresolved entry point still enabled %ld capability(ies)",
          mk.enables);
    CHECK(mk.bind_calls == 0, "an unresolved entry point still bound something");
    CHECK(said_has("glDebugMessageCallback"),
          "the refusal does not name the entry point that failed");
    CHECK(s_set_cb == NULL, "a NULL lookup left a setter pointer behind");

    /* a platform with no lookup at all: the same refusal, no crash */
    mock_reset();
    h = HOST;
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = T_DEBUG_BIT;
    h.gl_get_proc = NULL;
    CHECK(sl_gldebug_install(&h) == 0, "a platform with no lookup reported active");
    CHECK(mk.enables == 0, "it still enabled %ld capability(ies)", mk.enables);

    /* a platform that cannot enable, or cannot PROVE an enable: refused before
     * anything is bound, so no callback is left installed against state that
     * was never turned on */
    mock_reset();
    h = HOST;
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = T_DEBUG_BIT;
    h.gl_enable_verified = NULL;
    CHECK(sl_gldebug_install(&h) == 0, "a platform with no enable reported active");
    CHECK(mk.bind_calls == 0,
          "a platform with no enable bound a callback it could not arm");

    mock_reset();
    h = HOST;
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = T_DEBUG_BIT;
    h.gl_is_enabled = NULL;
    CHECK(sl_gldebug_install(&h) == 0,
          "a platform that cannot read a capability back reported active");
    CHECK(mk.enables == 0 && mk.bind_calls == 0,
          "it enabled %ld capability(ies) and bound %ld callback(s) with no way"
          " to prove either", mk.enables, mk.bind_calls);

    /* and a shutdown after any of those refusals is a no-op: nothing to undo,
     * and in particular no GL call while a context may already be gone */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.proc_null = 1;
    mk.gl_get_bits = T_DEBUG_BIT;
    (void) sl_gldebug_install(&HOST);
    mk.calls = 0; mk.disables = 0; mk.unbind_calls = 0;
    sl_gldebug_shutdown(&HOST);
    CHECK(mk.calls == 0,
          "a shutdown after a refused install made %ld platform call(s)",
          mk.calls);

    printf("  lookup: a NULL entry point, a missing lookup and a missing enable"
           " each fail closed with no debug state changed\n");
    return g_fail == 0;
}

/* ======================== MODE: callback ================================ */
/* The bound callback: that it IS the module's own, that invoking it touches no
 * platform call but `say`, that it reports the fields, and that the cap holds
 * with exactly one suppression line. */
static int mode_callback(void)
{
    long calls_before;
    int i, caps;

    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = T_DEBUG_BIT;
    CHECK(sl_gldebug_install(&HOST) == 1, "install refused a granted context");
    CHECK(mk.bind_calls == 1, "the callback was bound %ld time(s), expected 1",
          mk.bind_calls);
    CHECK(mk.bound == sl_gldebug_cb,
          "the pointer handed to glDebugMessageCallback is not the module's"
          " callback");
    CHECK(mk.enables == 2, "install enabled %ld capability(ies), expected 2",
          mk.enables);
    CHECK(mk.n_enabled == 2 && mk.enabled[0] == T_DEBUG_SYNC
          && mk.enabled[1] == T_DEBUG_OUTPUT,
          "install enabled 0x%x then 0x%x, expected SYNCHRONOUS then OUTPUT",
          mk.n_enabled > 0 ? mk.enabled[0] : 0u,
          mk.n_enabled > 1 ? mk.enabled[1] : 0u);
    /* F3 / r764c1: the whole install sequence, and the structural property it
     * exists for - both capabilities proved on BEFORE the bind, both re-proved
     * after it. */
    CHECK(strcmp(order_str(), "en,en,cbset,ise,ise") == 0,
          "install's call sequence is \"%s\", expected"
          " \"en,en,cbset,ise,ise\"", order_str());
    CHECK(bind_after_two_enables(),
          "the callback was bound before both capabilities were proved on");
    CHECK(mk.isenables == 2,
          "the capabilities were re-proved %ld time(s) after the bind,"
          " expected 2", mk.isenables);

    /* THE MEASUREMENT: a callback invocation makes no platform call. `calls`
     * counts every member of the host except say, so a GL call, an SDL call or
     * an entry-point resolve from inside the callback moves it. */
    mk.n_said = 0;
    calls_before = mk.calls;
    mk.bound(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, 1281u,
             GL_DEBUG_SEVERITY_HIGH, -1, "GL_INVALID_ENUM in glEnable", NULL);
    CHECK(mk.calls == calls_before,
          "the callback made %ld platform call(s)", mk.calls - calls_before);
    CHECK(mk.n_said == 1, "one message produced %d line(s)", mk.n_said);
    CHECK(said_has("API") && said_has("ERROR") && said_has("HIGH")
          && said_has("1281") && said_has("GL_INVALID_ENUM in glEnable"),
          "the reported line is missing a field: \"%s\"",
          mk.n_said > 0 ? mk.said[0] : "");

    /* THE CAP. The module has already printed one, so 63 more reach the cap
     * and everything after it produces exactly one suppression line. */
    mk.n_said = 0;
    for (i = 0; i < SL_GLDEBUG_MSG_CAP + 40; i++)
        mk.bound(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_PERFORMANCE, (unsigned) i,
                 GL_DEBUG_SEVERITY_LOW, -1, "a storm", NULL);
    caps = 0;
    for (i = 0; i < mk.n_said; i++)
        if (strstr(mk.said[i], "cap") != NULL) caps++;
    CHECK(caps == 1, "the cap was reported %d time(s), expected exactly 1", caps);
    /* One message was already printed above, so of the CAP+40 calls exactly
     * CAP-1 are reported and one is the cap line: CAP lines in all, and 40
     * calls that produce nothing at all. */
    CHECK(mk.n_said == SL_GLDEBUG_MSG_CAP,
          "past the cap the callback said %d line(s), expected %d",
          mk.n_said, SL_GLDEBUG_MSG_CAP);
    CHECK(mk.calls == calls_before,
          "the storm made %ld platform call(s)", mk.calls - calls_before);

    /* A message arriving after shutdown must format nothing: the sink is
     * dropped, so a driver that calls a cleared callback late is harmless. */
    sl_gldebug_shutdown(&HOST);
    mk.n_said = 0;
    sl_gldebug_cb(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, 7u,
                  GL_DEBUG_SEVERITY_HIGH, -1, "after the end", NULL);
    CHECK(mk.n_said == 0,
          "a message after shutdown still printed %d line(s)", mk.n_said);

    printf("  callback: the module's own pointer is bound, an invocation makes"
           " no platform call, the cap reports once, a late call is inert\n");
    return g_fail == 0;
}

/* ======================== MODE: lifecycle =============================== */
/* Teardown. The callback is cleared BEFORE debug output is disabled, both
 * happen, and a second shutdown does nothing. The order assertion is what would
 * catch the reversed sequence - a disable followed by an unbind leaves a window
 * in which the implementation still holds the module's pointer. */
static int mode_lifecycle(void)
{
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.gl_get_bits = T_DEBUG_BIT;
    CHECK(sl_gldebug_install(&HOST) == 1, "install refused a granted context");

    mk.n_order = 0;
    sl_gldebug_shutdown(&HOST);
    CHECK(mk.unbind_calls == 1, "the callback was cleared %ld time(s), expected 1",
          mk.unbind_calls);
    CHECK(mk.disables == 2, "shutdown disabled %ld capability(ies), expected 2",
          mk.disables);
    CHECK(mk.n_order >= 1 && strcmp(mk.order[0], "cbclr") == 0,
          "shutdown's first act was \"%s\", expected clearing the callback",
          mk.n_order > 0 ? mk.order[0] : "nothing");
    CHECK(mk.n_order == 3 && strcmp(mk.order[1], "dis") == 0
          && strcmp(mk.order[2], "dis") == 0,
          "shutdown's sequence is not clear-then-disable-twice (%d step(s))",
          mk.n_order);
    CHECK(mk.n_disabled == 2 && mk.disabled[0] == T_DEBUG_OUTPUT
          && mk.disabled[1] == T_DEBUG_SYNC,
          "shutdown disabled 0x%x then 0x%x, expected OUTPUT then SYNCHRONOUS",
          mk.n_disabled > 0 ? mk.disabled[0] : 0u,
          mk.n_disabled > 1 ? mk.disabled[1] : 0u);
    CHECK(s_set_cb == NULL && s_say == NULL && s_active == 0,
          "shutdown left module state behind");

    /* idempotent: a second call makes no platform call, which matters because
     * the only legal instant for these calls has passed by then */
    mk.calls = 0; mk.n_order = 0;
    sl_gldebug_shutdown(&HOST);
    CHECK(mk.calls == 0, "a second shutdown made %ld platform call(s)", mk.calls);

    /* and the diagnostic does not re-arm itself across a context: after a
     * shutdown, an install with no new arm is a no-op */
    mk.calls = 0; mk.n_said = 0;
    CHECK(sl_gldebug_install(&HOST) == 0,
          "install activated without a new arm after a shutdown");
    CHECK(mk.calls == 0 && mk.n_said == 0,
          "that install made %ld call(s) and said %d line(s)",
          mk.calls, mk.n_said);

    printf("  lifecycle: clear-then-disable, both capabilities, idempotent,"
           " and no re-arm across a context\n");
    return g_fail == 0;
}

/* ======================== MODE: enable ================================== */
/* F3 / r764c1. glEnable returns nothing, so "it was called" is not "it took".
 * Six ways an enable can fail to take effect, each one its own scenario, and
 * each must REFUSE, undo what it changed in the right order, and leave module
 * state inactive. The previous round enabled both capabilities and reported
 * ACTIVE without reading either back. */
static int one_enable_failure(const char *what, const char *expect_in_line)
{
    int before = g_fail;

    mk.gl_get_bits = T_DEBUG_BIT;
    mk.n_said = 0;
    mk.n_order = 0;
    CHECK(sl_gldebug_install(&HOST) == 0, "%s: install reported ACTIVE", what);
    CHECK(!said_has("ACTIVE"), "%s: an ACTIVE claim was printed", what);
    CHECK(said_has("REFUSED"), "%s: no REFUSED line", what);
    CHECK(said_has(expect_in_line),
          "%s: the refusal does not name \"%s\": \"%s\"", what, expect_in_line,
          mk.n_said > 0 ? mk.said[0] : "");
    CHECK(mk.n_said == 1, "%s: said %d line(s), expected 1", what, mk.n_said);

    /* CLEANUP ORDER: the callback is cleared FIRST, then both capabilities are
     * disabled. A disable-then-clear leaves a window in which the driver holds
     * a pointer into a module that has stopped expecting calls. */
    CHECK(strstr(order_str(), "cbclr,dis,dis") != NULL,
          "%s: the cleanup sequence is \"%s\", expected a clear then two"
          " disables", what, order_str());
    CHECK(mk.unbind_calls == 1,
          "%s: the callback was cleared %ld time(s), expected 1", what,
          mk.unbind_calls);
    CHECK(mk.n_disabled == 2 && mk.disabled[0] == T_DEBUG_OUTPUT
          && mk.disabled[1] == T_DEBUG_SYNC,
          "%s: disabled 0x%x then 0x%x, expected OUTPUT then SYNCHRONOUS", what,
          mk.n_disabled > 0 ? mk.disabled[0] : 0u,
          mk.n_disabled > 1 ? mk.disabled[1] : 0u);
    CHECK(bind_after_two_enables(),
          "%s: the callback was bound before both capabilities were proved",
          what);

    /* STATE ENDS INACTIVE: a shutdown is a no-op and a late callback is inert */
    CHECK(s_active == 0 && s_set_cb == NULL && s_say == NULL,
          "%s: module state survived the refusal", what);
    mk.calls = 0;
    sl_gldebug_shutdown(&HOST);
    CHECK(mk.calls == 0,
          "%s: a shutdown after the refusal made %ld platform call(s)", what,
          mk.calls);
    mk.n_said = 0;
    sl_gldebug_cb(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, 1u,
                  GL_DEBUG_SEVERITY_HIGH, -1, "late", NULL);
    CHECK(mk.n_said == 0,
          "%s: a late callback invocation printed %d line(s)", what,
          mk.n_said);
    return g_fail == before;
}

static int mode_enable(void)
{
    /* 1. the SYNCHRONOUS enable raises a GL error */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.en_rc_sync = SL_GLDEBUG_EN_ERROR;
    (void) one_enable_failure("sync enable errors", "GL error");

    /* 2. it is accepted and reads back OFF - the driver took the call and did
     *    nothing, which no return code could ever have revealed */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.en_rc_sync = SL_GLDEBUG_EN_OFF;
    (void) one_enable_failure("sync reads off", "read back OFF");

    /* 3. the readback itself cannot be trusted */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.en_rc_sync = SL_GLDEBUG_EN_UNKNOWN;
    (void) one_enable_failure("sync unprovable", "glIsEnabled");

    /* 4. SYNCHRONOUS is fine and OUTPUT fails - the refusal must name the one
     *    that failed, and the one that took must still be disabled */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.en_rc_out = SL_GLDEBUG_EN_ERROR;
    if (one_enable_failure("output enable errors", "GL_DEBUG_OUTPUT")) {
        CHECK(mk.enables == 2,
              "output-enable failure: %ld enable(s), expected SYNCHRONOUS then"
              " OUTPUT", mk.enables);
    }

    /* 5. BOTH enables prove on, the callback is bound, and the re-proof then
     *    finds SYNCHRONOUS off. This is the case where a clear is not
     *    bookkeeping: a callback really was installed. */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.ise_on_sync = 0;
    if (one_enable_failure("sync off after the bind", "no longer read back")) {
        CHECK(mk.bind_calls == 1,
              "that case bound %ld callback(s), expected 1", mk.bind_calls);
        CHECK(strstr(order_str(), "cbset") != NULL
              && strstr(order_str(), "cbset,ise,cbclr,dis,dis") != NULL,
              "the bound-then-refused sequence is \"%s\"", order_str());
    }

    /* 6. the post-bind readback of OUTPUT cannot be trusted */
    mock_reset();
    (void) sl_gldebug_arm("1", &HOST);
    mk.ise_rc_out = -1;
    (void) one_enable_failure("output unreadable after the bind",
                              "GL_DEBUG_OUTPUT");

    printf("  enable: six ways an enable fails to take effect - an error, a"
           " silent no-op, an untrustworthy readback, each on either"
           " capability, and a post-bind regression - all REFUSE, clear before"
           " disabling, and end inactive\n");
    return g_fail == 0;
}

/* ======================== MODE: default ================================= */
/* The path every player launch takes. Nothing requested, nothing resolved,
 * nothing enabled, nothing printed, at arm, at install and at shutdown alike -
 * the whole claim that an absent selector preserves behaviour. */
static int mode_default(void)
{
    mock_reset();
    CHECK(sl_gldebug_arm(NULL, &HOST) == 0, "the default launch armed");
    CHECK(sl_gldebug_install(&HOST) == 0, "the default launch installed");
    sl_gldebug_shutdown(&HOST);
    CHECK(mk.calls == 0,
          "the default launch made %ld platform call(s) across the whole"
          " lifecycle", mk.calls);
    CHECK(mk.n_said == 0, "the default launch printed %d line(s)", mk.n_said);
    CHECK(mk.sdl_sets == 0, "the default launch requested a context attribute");
    CHECK(mk.procs == 0, "the default launch resolved an entry point");
    CHECK(s_armed == 0 && s_active == 0 && s_set_cb == NULL,
          "the default launch left module state set");

    /* an install reached without any arm at all - the ordering a future edit
     * could introduce - is still a no-op rather than an unverified activation */
    mock_reset();
    mk.gl_get_bits = T_DEBUG_BIT;
    CHECK(sl_gldebug_install(&HOST) == 0, "install without an arm activated");
    CHECK(mk.calls == 0, "install without an arm made %ld call(s)", mk.calls);

    /* and a NULL host cannot be dereferenced on any of the three entry points */
    mock_reset();
    CHECK(sl_gldebug_arm("1", NULL) == 0, "a NULL host armed");
    CHECK(sl_gldebug_install(NULL) == 0, "a NULL host installed");
    sl_gldebug_shutdown(NULL);
    CHECK(1, "a NULL host survived all three entry points");

    printf("  default: no attribute requested, no lookup, no enable, no line,"
           " no state - and a NULL host is survivable\n");
    return g_fail == 0;
}

/* ======================== MODE: format ================================== */
/* The formatter's bounds. Each clause here is a way a driver string can be
 * hostile, and each is a separate bound in the implementation. */
static int mode_format(void)
{
    char buf[SL_GLDEBUG_LINE_MAX];
    char big[SL_GLDEBUG_TEXT_MAX * 4];
    char small[24];
    int n, i;

    /* known tokens are NAMED */
    n = sl_gldebug_format(buf, (int) sizeof buf, GL_DEBUG_SOURCE_SHADER_COMPILER,
                          GL_DEBUG_TYPE_PORTABILITY, 42u,
                          GL_DEBUG_SEVERITY_NOTIFICATION, -1, "note");
    CHECK(n == (int) strlen(buf), "the returned length %d is not strlen %d",
          n, (int) strlen(buf));
    CHECK(strstr(buf, "SHADER_COMPILER") != NULL
          && strstr(buf, "PORTABILITY") != NULL
          && strstr(buf, "NOTIFICATION") != NULL
          && strstr(buf, "id=42") != NULL
          && strstr(buf, "note") != NULL,
          "a named line is missing a field: \"%s\"", buf);

    /* an UNKNOWN token is reported as hex, not folded into OTHER */
    n = sl_gldebug_format(buf, (int) sizeof buf, 0x1234u, 0x5678u, 0u, 0x9ABCu,
                          -1, "x");
    CHECK(strstr(buf, "0x1234") != NULL && strstr(buf, "0x5678") != NULL
          && strstr(buf, "0x9ABC") != NULL,
          "an unknown enum was not reported as hex: \"%s\"", buf);
    CHECK(strstr(buf, "OTHER") == NULL,
          "an unknown enum was folded into a name it does not have: \"%s\"",
          buf);

    /* `length` BOUNDS the text even when the string runs on. A driver that
     * passes a length shorter than the buffer means it. */
    n = sl_gldebug_format(buf, (int) sizeof buf, GL_DEBUG_SOURCE_API,
                          GL_DEBUG_TYPE_ERROR, 1u, GL_DEBUG_SEVERITY_HIGH,
                          5, "abcdefghijklmnop");
    CHECK(strstr(buf, "abcde") != NULL, "the bounded text is absent: \"%s\"", buf);
    CHECK(strstr(buf, "abcdef") == NULL,
          "the copy ran past the `length` the driver gave: \"%s\"", buf);

    /* a NUL inside `length` still stops the copy */
    n = sl_gldebug_format(buf, (int) sizeof buf, GL_DEBUG_SOURCE_API,
                          GL_DEBUG_TYPE_ERROR, 1u, GL_DEBUG_SEVERITY_HIGH,
                          20, "ab\0cdefghijklmnopqr");
    CHECK(strstr(buf, "ab") != NULL && strstr(buf, "cdef") == NULL,
          "a NUL inside `length` did not stop the copy: \"%s\"", buf);

    /* the module's own TEXT CAP holds against an unbounded driver string */
    for (i = 0; i < (int) sizeof big - 1; i++) big[i] = 'A';
    big[sizeof big - 1] = '\0';
    n = sl_gldebug_format(buf, (int) sizeof buf, GL_DEBUG_SOURCE_API,
                          GL_DEBUG_TYPE_ERROR, 1u, GL_DEBUG_SEVERITY_HIGH,
                          -1, big);
    CHECK(n < (int) sizeof buf, "the line %d overran its buffer of %d",
          n, (int) sizeof buf);
    CHECK(n == (int) strlen(buf), "the length %d disagrees with strlen %d",
          n, (int) strlen(buf));
    {   /* exactly SL_GLDEBUG_TEXT_MAX text bytes, no more */
        const char *p = strstr(buf, "AAA");
        int run = 0;
        CHECK(p != NULL, "the capped text is absent");
        if (p != NULL) while (p[run] == 'A') run++;
        CHECK(run == SL_GLDEBUG_TEXT_MAX,
              "the text ran to %d bytes, expected the %d-byte cap",
              run, SL_GLDEBUG_TEXT_MAX);
    }

    /* a `length` LONGER than the real string is harmless: the NUL wins */
    n = sl_gldebug_format(buf, (int) sizeof buf, GL_DEBUG_SOURCE_API,
                          GL_DEBUG_TYPE_ERROR, 1u, GL_DEBUG_SEVERITY_HIGH,
                          100000, "short");
    CHECK(strstr(buf, "short") != NULL && n == (int) strlen(buf),
          "an over-long `length` broke the line: \"%s\"", buf);

    /* control bytes become spaces: one message is one line, always */
    n = sl_gldebug_format(buf, (int) sizeof buf, GL_DEBUG_SOURCE_API,
                          GL_DEBUG_TYPE_ERROR, 1u, GL_DEBUG_SEVERITY_HIGH,
                          -1, "two\nlines\rand\ta tab\x7f");
    CHECK(strchr(buf, '\n') == NULL && strchr(buf, '\r') == NULL
          && strchr(buf, '\t') == NULL && strchr(buf, 0x7f) == NULL,
          "a control byte survived into the line: \"%s\"", buf);

    /* a NULL message is REPORTED, never dereferenced */
    n = sl_gldebug_format(buf, (int) sizeof buf, GL_DEBUG_SOURCE_API,
                          GL_DEBUG_TYPE_ERROR, 9u, GL_DEBUG_SEVERITY_LOW,
                          -1, NULL);
    CHECK(n > 0 && strstr(buf, "(no text)") != NULL,
          "a NULL message was not reported: \"%s\"", buf);

    /* a buffer too small TRUNCATES and terminates; it never overruns */
    memset(small, 0x5A, sizeof small);
    n = sl_gldebug_format(small, 16, GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR,
                          1u, GL_DEBUG_SEVERITY_HIGH, -1, "message");
    CHECK(n <= 15, "a 16-byte buffer produced %d characters", n);
    CHECK(small[15] == '\0' || n < 15, "the small buffer is not terminated");
    CHECK(small[16] == 0x5A && small[23] == 0x5A,
          "the formatter wrote past the size it was given");

    /* the degenerate arguments */
    CHECK(sl_gldebug_format(NULL, 100, 0u, 0u, 0u, 0u, -1, "x") == 0,
          "a NULL output buffer was written to");
    buf[0] = 'Z';
    CHECK(sl_gldebug_format(buf, 0, 0u, 0u, 0u, 0u, -1, "x") == 0
          && buf[0] == 'Z',
          "a zero-size buffer was written to");
    CHECK(sl_gldebug_format(buf, 1, 0u, 0u, 0u, 0u, -1, "x") == 0
          && buf[0] == '\0',
          "a one-byte buffer was not left as an empty string");

    printf("  format: names known tokens, hexes unknown ones, honours"
           " `length`, the NUL, the %d-byte cap and the buffer, flattens"
           " control bytes, survives NULL\n", SL_GLDEBUG_TEXT_MAX);
    return g_fail == 0;
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "";
    int ok;

    /* The compile-time assertions above are the point; referencing them here
     * keeps them from being optimised into nothing and keeps the compiler from
     * reporting them unused. */
    if (s_khronos_from_module == NULL || s_module_from_khronos == NULL
        || s_setter_is_khronos_pfn == NULL || s_khronos_pfn_from_module == NULL) {
        printf("FAIL: a callback type assertion resolved to NULL\n");
        return 1;
    }
    mock_reset();
    if      (strcmp(mode, "parse")     == 0) ok = mode_parse();
    else if (strcmp(mode, "arm")       == 0) ok = mode_arm();
    else if (strcmp(mode, "grant")     == 0) ok = mode_grant();
    else if (strcmp(mode, "lookup")    == 0) ok = mode_lookup();
    else if (strcmp(mode, "callback")  == 0) ok = mode_callback();
    else if (strcmp(mode, "enable")    == 0) ok = mode_enable();
    else if (strcmp(mode, "lifecycle") == 0) ok = mode_lifecycle();
    else if (strcmp(mode, "default")   == 0) ok = mode_default();
    else if (strcmp(mode, "format")    == 0) ok = mode_format();
    else {
        printf("usage: %s parse|arm|grant|lookup|callback|enable|lifecycle"
               "|default|format\n", argv[0]);
        return 2;
    }
    printf("  %s: %d check(s) passed, %d failed\n", mode, g_pass, g_fail);
    return ok ? 0 : 1;
}
