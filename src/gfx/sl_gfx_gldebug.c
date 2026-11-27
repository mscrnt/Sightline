/* The opt-in GL debug-output diagnostic (SL_GL_DEBUG_DIAG). Contracts, the
 * selector's grammar and what this is and is not evidence of are documented in
 * src/gfx/sl_gfx_gldebug.h; this file holds only the decisions.
 *
 * NO PLATFORM HEADER IS INCLUDED HERE ON PURPOSE. Not <SDL2/SDL.h>, not
 * <GL/gl.h>: every SDL and GL call this diagnostic needs arrives as a function
 * pointer in struct sl_gldebug_host, which is what lets the whole module run on
 * a CPU with no driver. It also means this translation unit links against
 * neither SDL2 nor opengl32 - a property the fixture depends on and a reader can
 * check by searching this file for a call that is not through `h` or `s_*`.
 *
 * AND NO <stdio.h> EITHER. The formatter writes into a caller's buffer with its
 * own bounded appenders rather than through snprintf, for two reasons: mingw's
 * snprintf is the CRT's unless __USE_MINGW_ANSI_STDIO is in force, and that one
 * may return -1 and leave the buffer unterminated on truncation - a truncating
 * formatter cannot be built on a truncation report it does not trust; and the
 * callback runs inside the driver, where the less machinery it reaches the
 * better. The whole file therefore depends on no library at all - <stddef.h>
 * below is the one include, and it declares types and NULL rather than
 * anything to call. */
#include "sl_gfx_gldebug.h"
#include <stddef.h>

/* ---- THE NUMBERS, read out of the installed headers rather than recalled ---
 *
 * The house convention in this directory (see the SL_GL_* block at the top of
 * src/gfx/sl_gfx_sdl.c and the citations in src/gfx/sl_gfx_bgstate.c and
 * src/gfx/sl_gfx_dl.c): a token newer than the <GL/gl.h> this tree compiles
 * against is defined locally under an SL_ prefix with the header and line it was
 * read from. The SL_ prefix is not decoration - it CANNOT collide with the real
 * token, so no #ifndef guard is needed and no translation unit can end up
 * compiling two different values under one name.
 *
 * MSYS2 mingw32, GL/glext.h (Khronos registry header) and SDL2 2.32.10:
 *
 *   GL/glext.h:897   GL_CONTEXT_FLAGS               0x821E  (GL 3.0)
 *   GL/glext.h:2239  GL_CONTEXT_FLAG_DEBUG_BIT      0x0002  (GL 4.3 / KHR_debug)
 *   GL/glext.h:2238  GL_DEBUG_OUTPUT                0x92E0
 *   GL/glext.h:2203  GL_DEBUG_OUTPUT_SYNCHRONOUS    0x8242
 *   GL/glext.h:2207-2212  GL_DEBUG_SOURCE_*          0x8246-0x824B
 *   GL/glext.h:2213-2218  GL_DEBUG_TYPE_*            0x824C-0x8251
 *   GL/glext.h:2225-2227  GL_DEBUG_TYPE_MARKER/PUSH/POP  0x8268-0x826A
 *   GL/glext.h:2222-2224  GL_DEBUG_SEVERITY_HIGH/MEDIUM/LOW  0x9146-0x9148
 *   GL/glext.h:2228  GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
 *   SDL2/SDL_video.h:297  SDL_GL_CONTEXT_DEBUG_FLAG  0x0001  (SDL_GLcontextFlag)
 *
 * The SDL attribute KEY (SDL_GL_CONTEXT_FLAGS, SDL_video.h:278) is never named
 * here: the host's two accessors already stand for that one attribute, so the
 * enum stays where the real SDL header defines it. */
#define SL_GL_CONTEXT_FLAG_DEBUG_BIT   0x00000002u
#define SL_GL_DEBUG_OUTPUT             0x92E0u
#define SL_GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242u
#define SL_SDL_GL_CONTEXT_DEBUG_FLAG   0x0001

/* ---- MODULE STATE -------------------------------------------------------
 *
 * Three ints and two pointers, all zero in a launch that did not arm the
 * selector. No atomics: debug output is enabled SYNCHRONOUSLY, which means the
 * implementation calls back on the thread that made the offending GL call, and
 * every GL call in this build is made from the thread that owns the context
 * (the background worker pool submits no GL - src/platform/sl_bg_worker.c
 * produces vertex data and the owning thread issues the draws). An asynchronous
 * debug context could call back from a driver thread; this module never asks for
 * one, and that is the reason the SYNCHRONOUS enable is not optional. */
static int                 s_armed;      /* sl_gldebug_arm requested the flag  */
static int                 s_active;     /* the callback is bound and enabled  */
static unsigned long       s_msgs;       /* messages printed this process      */
static sl_gldebug_setter_t s_set_cb;     /* resolved against THIS context      */
static void              (*s_say)(const char *line);   /* the callback's sink  */

/* The bounded appender the refusal and verdict lines are composed with. Defined
 * with the rest of the formatter below; declared here because the arm path
 * composes a line too and reads earlier in the file than the formatter does. */
static int gld_app(char *out, int outsz, int n, const char *s);

int sl_gldebug_parse(const char *raw)
{
    if (raw == NULL) return SL_GLDEBUG_SEL_ABSENT;
    if (raw[0] == '\0') return SL_GLDEBUG_SEL_OFF;
    if (raw[0] == '1' && raw[1] == '\0') return SL_GLDEBUG_SEL_ARMED;
    if (raw[0] == '0' && raw[1] == '\0') return SL_GLDEBUG_SEL_OFF;
    return SL_GLDEBUG_SEL_MALFORMED;
}

static void gldebug_say(const struct sl_gldebug_host *h, const char *line)
{
    if (h != NULL && h->say != NULL) h->say(line);
}

int sl_gldebug_arm(const char *raw, const struct sl_gldebug_host *h)
{
    int sel = sl_gldebug_parse(raw);
    int flags = 0;

    s_armed = 0;
    if (sel == SL_GLDEBUG_SEL_ABSENT || sel == SL_GLDEBUG_SEL_OFF) return 0;
    if (sel == SL_GLDEBUG_SEL_MALFORMED) {
        /* The verdict, not the value. */
        gldebug_say(h, "sightline gfx: SL_GL_DEBUG_DIAG is set to a malformed"
                       " value - the GL debug-output diagnostic stays OFF"
                       " (the only value that arms it is 1)");
        return 0;
    }
    if (h == NULL || h->sdl_set_ctx_flags == NULL
        || h->sdl_get_ctx_flags == NULL) {
        gldebug_say(h, "sightline gfx: SL_GL_DEBUG_DIAG=1 - REFUSED, this build"
                       " cannot read and request context flags");
        return 0;
    }
    /* THE EXISTING REQUESTED FLAGS ARE READ, AND AN UNREADABLE READ REFUSES.
     *
     * This attribute is PROCESS-WIDE requested state. OR-ing the debug bit into
     * what it already holds is the only safe write; assigning the bit alone
     * would CLEAR every other flag a launch had asked for. A read that fails
     * leaves this module with no idea what it would be clearing, so there is no
     * zero fallback and NO SET CALL IS MADE AT ALL - the caller's world is
     * exactly as it was, which is the same reasoning
     * src/gfx/sl_gfx_sdl_aux.inc:38-52 applies to the object-sharing attribute.
     *
     * Nothing else in this tree sets SDL_GL_CONTEXT_FLAGS today
     * (grep -rn 'SDL_GL_SetAttribute' src/ tools/ finds the three attributes in
     * src/gfx/sl_gfx_sdl.c:198,209,210 and the object-sharing attribute in
     * src/gfx/sl_gfx_sdl_aux.inc:209,290, and grep -rn 'SDL_GL_CONTEXT_FLAGS'
     * src/ tools/ comes back empty), so the read is EXPECTED to hand back 0.
     * That is not a reason to skip it: the expectation is about today's tree,
     * and a diagnostic that silently dropped a flag some later change asks for
     * would be a defect this module INTRODUCED rather than found. */
    if (h->sdl_get_ctx_flags(&flags) != 0) {
        gldebug_say(h, "sightline gfx: SL_GL_DEBUG_DIAG=1 - REFUSED, the"
                       " existing requested context flags could not be read,"
                       " so none were changed (no debug context is requested)");
        return 0;
    }
    if (h->sdl_set_ctx_flags(flags | SL_SDL_GL_CONTEXT_DEBUG_FLAG) != 0) {
        /* A SET THAT REPORTED FAILURE MAY STILL HAVE STORED. SDL keeps this
         * attribute in process-wide configuration and a query hands the same
         * field back (SDL2 release-2.32.10, src/video/SDL_video.c), so the
         * return code cannot establish what the attribute now holds - only a
         * readback can. This matters here and not merely for tidiness: a stored
         * debug bit would give this launch a DEBUG CONTEXT with no callback
         * bound and nothing reporting on it, which is a behaviour change the
         * diagnostic caused while refusing to run. */
        int after = 0;
        const char *tail = " (the attribute could not be re-read, so this"
                           " launch may still receive a debug context that"
                           " nothing reports on)";

        if (h->sdl_get_ctx_flags(&after) == 0) {
            if ((after & SL_SDL_GL_CONTEXT_DEBUG_FLAG) == 0) {
                tail = " (the attribute is unchanged)";
            } else if (h->sdl_set_ctx_flags(flags) == 0
                       && h->sdl_get_ctx_flags(&after) == 0
                       && (after & SL_SDL_GL_CONTEXT_DEBUG_FLAG) == 0) {
                tail = " (the failed set had stored anyway; the original value"
                       " was put back and proved by readback)";
            } else {
                tail = " (the failed set had stored anyway and could NOT be put"
                       " back, so this launch may still receive a debug context"
                       " that nothing reports on)";
            }
        }
        {
            char line[SL_GLDEBUG_LINE_MAX];
            int n = gld_app(line, (int) sizeof line, 0,
                            "sightline gfx: SL_GL_DEBUG_DIAG=1 - REFUSED, the"
                            " debug context flag could not be requested");
            (void) gld_app(line, (int) sizeof line, n, tail);
            gldebug_say(h, line);
        }
        return 0;
    }
    /* A successful set records a REQUEST, and that is all it records. Whether a
     * debug context was granted is decided after creation, by
     * sl_gldebug_install, from the CONTEXT's own flags; nothing before then may
     * say ACTIVE. */
    s_armed = 1;
    gldebug_say(h, "sightline gfx: SL_GL_DEBUG_DIAG=1 - a debug GL context is"
                   " REQUESTED (diagnostic: requesting one can itself change"
                   " driver validation, synchronisation and code paths, so"
                   " neither a clean log nor an absent fault under it is"
                   " evidence about a default launch)");
    return 1;
}

/* Did the driver GRANT a debug context? ONE SOURCE DECIDES: the context's own
 * GL_CONTEXT_FLAGS.
 *
 * SDL_GL_GetAttribute IS NOT A SECOND SOURCE AND IS NEVER A FALLBACK. SDL
 * stores the attribute as REQUESTED state and a query hands that same field
 * back (SDL2 release-2.32.10, src/video/SDL_video.c), so on an unreadable GL
 * query SDL would simply echo this module's own request - accepting it would
 * make the whole grant check a loop that confirms what was asked for. That is
 * exactly the B-123 failure this tree already paid a round for: a 16-bit depth
 * request the driver ignored, believed honoured because the request was what
 * got read back. So an unreadable glGetIntegerv(GL_CONTEXT_FLAGS) REFUSES,
 * whatever SDL says, and SDL's answer is read only to be reported as ADVISORY
 * - it tells a reader what was asked for, never what was granted.
 *
 * Returns 1 granted, 0 the GL query answered and the bit is absent, -1 the GL
 * query could not be read. `advisory` is written -1 unknown, 0 SDL's record
 * carries no debug bit, 1 SDL's record carries it (i.e. the request). */
static int gldebug_granted(const struct sl_gldebug_host *h, int *advisory)
{
    unsigned int bits = 0;
    int flags = 0;

    *advisory = -1;
    if (h != NULL && h->sdl_get_ctx_flags != NULL
        && h->sdl_get_ctx_flags(&flags) == 0)
        *advisory = (flags & SL_SDL_GL_CONTEXT_DEBUG_FLAG) != 0;
    if (h == NULL || h->gl_get_ctx_flags == NULL
        || h->gl_get_ctx_flags(&bits) != 0)
        return -1;
    return (bits & SL_GL_CONTEXT_FLAG_DEBUG_BIT) != 0u;
}

/* The advisory tail for a refusal line: what SDL's attribute says, labelled as
 * the request it is. Never allowed to read as corroboration. */
static const char *gldebug_advisory_tail(int advisory)
{
    if (advisory == 1)
        return " (SDL's context-flags attribute does carry the debug bit, but"
               " that is this module's own REQUEST read back, not a grant)";
    if (advisory == 0)
        return " (SDL's context-flags attribute carries no debug bit either)";
    return " (SDL's context-flags attribute could not be read either)";
}

/* ---- THE CALLBACK -------------------------------------------------------
 *
 * WHAT IT MAY DO: format into its own stack buffer and hand one line to the
 * sink recorded at install. THAT IS ALL.
 *
 * WHAT IT MAY NOT DO, and why each is forbidden rather than merely avoided:
 *
 *   NO GL CALL. A synchronous callback runs inside the implementation, during
 *   the very call that raised the message; calling GL from there is undefined
 *   (https://wikis.khronos.org/opengl/Debug_Output). A glGetError in here would
 *   also consume the error the caller is about to read.
 *   NO SDL CALL. Same reason, plus SDL_GetError is not reentrant against the
 *   call that is still running.
 *   NO getenv AND NO ENVIRONMENT VALUE. A diagnostic stream is pasted into
 *   reports; nothing from the environment goes into it.
 *   NO ALLOCATION. The thread may be inside a driver lock, and a message storm
 *   must not be able to turn into allocator pressure.
 *
 * The sink is a function pointer so the fixture can drive the callback with a
 * host whose every other member records the fact it was called - which is how
 * "this makes no GL and no SDL call" is measured instead of asserted. */
static void SL_GLDEBUG_APIENTRY sl_gldebug_cb(unsigned int source,
                                              unsigned int type,
                                              unsigned int id,
                                              unsigned int severity,
                                              int length,
                                              const char *message,
                                              const void *user)
{
    char line[SL_GLDEBUG_LINE_MAX];

    (void) user;
    if (s_say == NULL) return;
    if (s_msgs < (unsigned long) SL_GLDEBUG_MSG_CAP) {
        s_msgs++;
        if (sl_gldebug_format(line, (int) sizeof line, source, type, id,
                              severity, length, message) > 0)
            s_say(line);
        return;
    }
    if (s_msgs == (unsigned long) SL_GLDEBUG_MSG_CAP) {
        s_msgs++;                       /* the cap line is printed exactly once */
        s_say("sightline gfx: GL debug output reached its per-process cap of 64"
              " message(s) - the callback stays bound and prints no more");
    }
}

/* The no-cast assignment that makes the calling convention a COMPILE-TIME
 * property of the product build and not only of the fixture. If sl_gldebug_cb
 * were __cdecl, or its parameter list drifted from GLDEBUGPROC's, this
 * initialisation would be an incompatible pointer type. (The product's src/gfx
 * flags include -w, so this is a diagnostic the FIXTURE must turn into an error
 * - it compiles this same text with -Werror=incompatible-pointer-types, and
 * repeats the assignment against Khronos's own GLDEBUGPROC from <GL/glext.h>.) */
static const sl_gldebug_proc_t s_cb_is_a_gldebugproc = sl_gldebug_cb;

static const char *gldebug_source_name(unsigned int v)
{
    switch (v) {
    case 0x8246u: return "API";
    case 0x8247u: return "WINDOW_SYSTEM";
    case 0x8248u: return "SHADER_COMPILER";
    case 0x8249u: return "THIRD_PARTY";
    case 0x824Au: return "APPLICATION";
    case 0x824Bu: return "OTHER";
    default:      return NULL;
    }
}

static const char *gldebug_type_name(unsigned int v)
{
    switch (v) {
    case 0x824Cu: return "ERROR";
    case 0x824Du: return "DEPRECATED_BEHAVIOR";
    case 0x824Eu: return "UNDEFINED_BEHAVIOR";
    case 0x824Fu: return "PORTABILITY";
    case 0x8250u: return "PERFORMANCE";
    case 0x8251u: return "OTHER";
    case 0x8268u: return "MARKER";
    case 0x8269u: return "PUSH_GROUP";
    case 0x826Au: return "POP_GROUP";
    default:      return NULL;
    }
}

static const char *gldebug_severity_name(unsigned int v)
{
    switch (v) {
    case 0x9146u: return "HIGH";
    case 0x9147u: return "MEDIUM";
    case 0x9148u: return "LOW";
    case 0x826Bu: return "NOTIFICATION";
    default:      return NULL;
    }
}

/* Append, bounded, always NUL-terminating, returning the new length. One byte
 * of `outsz` is always reserved for the terminator, so `n` can never reach
 * outsz and no caller needs to check anything. */
static int gld_app(char *out, int outsz, int n, const char *s)
{
    while (*s != '\0' && n < outsz - 1) out[n++] = *s++;
    out[n] = '\0';
    return n;
}

static int gld_app_u(char *out, int outsz, int n, unsigned int v)
{
    char d[12];
    int i = 0;

    do { d[i++] = (char) ('0' + (int) (v % 10u)); v /= 10u; } while (v != 0u && i < 11);
    while (i > 0 && n < outsz - 1) out[n++] = d[--i];
    out[n] = '\0';
    return n;
}

/* An unknown enum is REPORTED, as four hex digits, rather than rounded to
 * "OTHER": a vendor token nobody here has seen is exactly the thing a reader
 * would want to look up, and a name invented for it would be a lie. Every GL
 * debug enum is a 16-bit token, so the low half is the whole of it. */
static int gld_app_hex16(char *out, int outsz, int n, unsigned int v)
{
    static const char hx[] = "0123456789ABCDEF";
    int sh;

    n = gld_app(out, outsz, n, "0x");
    for (sh = 12; sh >= 0; sh -= 4) {
        if (n >= outsz - 1) break;
        out[n++] = hx[(v >> sh) & 0xFu];
    }
    out[n] = '\0';
    return n;
}

static int gld_app_enum(char *out, int outsz, int n, const char *name,
                        unsigned int v)
{
    if (name != NULL) return gld_app(out, outsz, n, name);
    return gld_app_hex16(out, outsz, n, v & 0xFFFFu);
}

int sl_gldebug_format(char *out, int outsz, unsigned int source,
                      unsigned int type, unsigned int id,
                      unsigned int severity, int length, const char *message)
{
    int n, room, i;

    if (out == NULL || outsz <= 0) return 0;
    out[0] = '\0';
    n = gld_app(out, outsz, 0, "sightline gfx: GL debug ");
    n = gld_app_enum(out, outsz, n, gldebug_source_name(source), source);
    n = gld_app(out, outsz, n, "/");
    n = gld_app_enum(out, outsz, n, gldebug_type_name(type), type);
    n = gld_app(out, outsz, n, " sev=");
    n = gld_app_enum(out, outsz, n, gldebug_severity_name(severity), severity);
    n = gld_app(out, outsz, n, " id=");
    n = gld_app_u(out, outsz, n, id);
    n = gld_app(out, outsz, n, ": ");

    if (message == NULL) {
        /* Reported, never dereferenced. A driver that passes NULL has still
         * told us a message of that source/type/severity occurred. */
        return gld_app(out, outsz, n, "(no text)");
    }
    /* The copy stops at the FIRST of: `length` when non-negative, the first
     * NUL, SL_GLDEBUG_TEXT_MAX, and the room left in `out` (one byte reserved
     * for the terminator). Each bound is here because the others can fail:
     * `length` is the implementation's and has been seen to include the
     * terminator; a NUL-terminated message passes `length` negative; the text
     * cap is this module's own policy against a multi-kilobyte shader log; and
     * the buffer bound is what makes the first three safe to be wrong. */
    room = outsz - n - 1;
    if (room > SL_GLDEBUG_TEXT_MAX) room = SL_GLDEBUG_TEXT_MAX;
    if (length >= 0 && length < room) room = length;
    for (i = 0; i < room && message[i] != '\0'; i++) {
        unsigned char c = (unsigned char) message[i];
        /* A newline or a control byte in a driver string would break the
         * one-message-one-line contract the sink and every reader rely on. */
        out[n + i] = (c < 0x20u || c == 0x7Fu) ? ' ' : message[i];
    }
    out[n + i] = '\0';
    return n + i;
}

/* THE ONE FAIL-CLOSED EXIT, so there is exactly one cleanup order to get right.
 *
 * ORDER: the callback is cleared FIRST, then both capabilities are disabled.
 * Clearing first is what closes the window in which the implementation holds a
 * pointer into a module that has stopped expecting calls - and the window is
 * real on this path, because a driver may already have queued messages against
 * the capabilities that DID take. Both capabilities are disabled without
 * tracking which of them took: glDisable on a capability that is already off is
 * defined and harmless, while a record of what to undo is state that can be
 * wrong.
 *
 * The clear is issued whenever a setter was resolved, bound or not. A cleanup
 * that is correct only for the orderings that actually bind is a second code
 * path, and this one has no way to be sure which it is on.
 *
 * STATE ENDS INACTIVE: a later sl_gldebug_shutdown is a no-op and a late
 * callback invocation formats nothing, because s_say is dropped here. */
static void gldebug_unwind(const struct sl_gldebug_host *h)
{
    if (s_set_cb != NULL) s_set_cb(NULL, NULL);
    if (h != NULL && h->gl_disable != NULL) {
        h->gl_disable(SL_GL_DEBUG_OUTPUT);
        h->gl_disable(SL_GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }
    s_set_cb = NULL;
    s_say = NULL;                       /* a later stray call formats nothing */
    s_active = 0;
}

static void gldebug_fail_closed(const struct sl_gldebug_host *h,
                                const char *reason, const char *tail)
{
    char line[SL_GLDEBUG_LINE_MAX];
    int n;

    gldebug_unwind(h);
    n = gld_app(line, (int) sizeof line, 0,
                "sightline gfx: GL debug output REFUSED - ");
    n = gld_app(line, (int) sizeof line, n, reason);
    if (tail != NULL) (void) gld_app(line, (int) sizeof line, n, tail);
    gldebug_say(h, line);
}

static const char *gldebug_enable_why(int code)
{
    if (code == SL_GLDEBUG_EN_ERROR)
        return " raised a GL error (this context has no usable debug output)";
    if (code == SL_GLDEBUG_EN_OFF)
        return " was accepted and then read back OFF (the driver took the call"
               " and did nothing)";
    return " could not be proved on: glIsEnabled gave no trustworthy answer";
}

int sl_gldebug_install(const struct sl_gldebug_host *h)
{
    int granted, advisory = -1, on = 0, rc;
    void *p;

    s_active = 0;
    s_set_cb = NULL;                    /* never carried across a context */
    s_say = NULL;
    if (!s_armed) return 0;             /* the default launch: one int test */
    if (h == NULL) return 0;

    granted = gldebug_granted(h, &advisory);
    if (granted <= 0) {
        /* ONE LINE, and it carries SDL's answer only as the advisory it is.
         *
         * granted < 0 is the finding this round exists for: an unreadable
         * GL_CONTEXT_FLAGS used to fall through to SDL's attribute and could
         * ACTIVATE on it. SDL stores the REQUEST, so that fallback confirmed
         * what this module had just asked for. It now refuses, and the advisory
         * tail says in words why SDL's agreement is not evidence.
         *
         * granted == 0 is a driver that answered and declined. Nothing has been
         * enabled or bound on either path, so there is nothing to undo. */
        char line[SL_GLDEBUG_LINE_MAX];
        int n = gld_app(line, (int) sizeof line, 0,
                        granted < 0
                        ? "sightline gfx: GL debug output REFUSED - the"
                          " context's own GL_CONTEXT_FLAGS could not be read,"
                          " so no grant is proved and a request is not a grant"
                          " (no debug state changed)"
                        : "sightline gfx: GL debug output REFUSED - a debug"
                          " context was requested and the context's own"
                          " GL_CONTEXT_FLAGS carries no debug bit (no debug"
                          " state changed)");
        (void) gld_app(line, (int) sizeof line, n,
                       gldebug_advisory_tail(advisory));
        gldebug_say(h, line);
        return 0;
    }
    if (h->gl_get_proc == NULL
        || (p = h->gl_get_proc("glDebugMessageCallback")) == NULL) {
        /* Resolved BEFORE anything is enabled, so a context that cannot report
         * at all never has its debug state touched: GL_DEBUG_OUTPUT with no
         * bound callback only fills the implementation's internal message log,
         * which nothing here reads, at whatever the driver charges for it. */
        gldebug_say(h, "sightline gfx: GL debug output REFUSED -"
                       " glDebugMessageCallback unresolved against this context"
                       " (no debug state changed)");
        return 0;
    }
    if (h->gl_enable_verified == NULL || h->gl_is_enabled == NULL
        || h->say == NULL) {
        gldebug_say(h, "sightline gfx: GL debug output REFUSED - this build"
                       " cannot enable it and prove the enable (no debug state"
                       " changed)");
        return 0;
    }
    /* The one cast in the module, and it is unavoidable: GetProcAddress-shaped
     * lookups return a void pointer. What it is cast TO is the typedef the
     * no-cast initialisation above has already pinned to GLDEBUGPROC's shape,
     * and the callback is passed through WITHOUT a further cast, so the
     * convention is still checked by the compiler at the call.
     *
     * The setter is recorded HERE, before any enable, so that the single
     * fail-closed exit below can clear it whatever goes wrong next - including
     * on paths where no bind has happened yet. */
    s_set_cb = (sl_gldebug_setter_t) p;
    s_msgs = 0;

    /* SYNCHRONOUS FIRST, PROVED, AND BEFORE THE CALLBACK IS BOUND.
     *
     * glEnable returns nothing, so the only way to know a capability took is to
     * read it back; and the order matters for a reason stronger than tidiness.
     * Without GL_DEBUG_OUTPUT_SYNCHRONOUS the implementation may deliver
     * messages from a thread of its own choosing
     * (https://wikis.khronos.org/opengl/Debug_Output), while this module's
     * message counter and sink pointer are plain statics written on the
     * assumption of synchronous delivery. Proving SYNCHRONOUS before anything
     * can call back means NO INSTANT EXISTS in which the callback is bound
     * while that guarantee is unverified - the property holds by construction,
     * not by how fast the next two statements run. */
    rc = h->gl_enable_verified(SL_GL_DEBUG_OUTPUT_SYNCHRONOUS);
    if (rc != SL_GLDEBUG_EN_OK) {
        gldebug_fail_closed(h, "GL_DEBUG_OUTPUT_SYNCHRONOUS",
                            gldebug_enable_why(rc));
        return 0;
    }
    rc = h->gl_enable_verified(SL_GL_DEBUG_OUTPUT);
    if (rc != SL_GLDEBUG_EN_OK) {
        gldebug_fail_closed(h, "GL_DEBUG_OUTPUT", gldebug_enable_why(rc));
        return 0;
    }
    /* Both capabilities are proved on and no callback is bound, so the only
     * thing a message can reach right now is the driver's own log. Bind.
     *
     * The callback is handed over THROUGH the typedef variable above, so the
     * value that reaches the implementation is the one whose type the compiler
     * already checked, and the parameter it is passed as takes no cast either. */
    s_say = h->say;
    s_set_cb(s_cb_is_a_gldebugproc, NULL);

    /* AND BOTH ARE RE-PROVED AFTER THE BIND. Not redundant: the bind is a call
     * into the implementation like any other, and a driver that reset or
     * rejected debug state while installing a callback would otherwise be
     * reported ACTIVE. Cheap - two glIsEnabled - and it is the last gate before
     * the word ACTIVE is used. */
    if (h->gl_is_enabled(SL_GL_DEBUG_OUTPUT_SYNCHRONOUS, &on) != 0 || !on) {
        gldebug_fail_closed(h, "GL_DEBUG_OUTPUT_SYNCHRONOUS",
                            " no longer read back on after the callback was"
                            " bound");
        return 0;
    }
    if (h->gl_is_enabled(SL_GL_DEBUG_OUTPUT, &on) != 0 || !on) {
        gldebug_fail_closed(h, "GL_DEBUG_OUTPUT",
                            " no longer read back on after the callback was"
                            " bound");
        return 0;
    }
    s_active = 1;
    /* The ACTIVE line names WHAT WAS PROVED, and names only that. The grant
     * came from the context's own GL_CONTEXT_FLAGS - the single source this
     * module accepts - and both capabilities were read back on. SDL's attribute
     * is not mentioned here at all: it could only ever have echoed the request,
     * so quoting it beside the word ACTIVE would make a loop look like
     * corroboration. */
    gldebug_say(h, "sightline gfx: GL debug output ACTIVE - the context's own"
                   " GL_CONTEXT_FLAGS carries the debug bit, SYNCHRONOUS and"
                   " OUTPUT both read back on, callback bound, at most 64"
                   " messages per process");
    return 1;
}

void sl_gldebug_shutdown(const struct sl_gldebug_host *h)
{
    if (!s_active) return;
    /* THE SAME UNWIND THE FAILURE PATH USES, deliberately: there is exactly one
     * cleanup order in this file, so a teardown and a refused install cannot
     * drift apart. Both calls inside it need the owning context CURRENT, which
     * is why src/gfx/sl_gfx_sdl.c places this before SDL_GL_DeleteContext
     * rather than after. The only difference here is that a clean teardown
     * reports nothing and also forgets the arm, so a re-entered install on a
     * new context cannot activate without a new arm. */
    gldebug_unwind(h);
    s_armed = 0;
}
