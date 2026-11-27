/**
 * Per-scope frame-span and GPU-elapsed metric producer - opt-in.
 *
 * The contract, the nesting rules, the frame-identity rule and the
 * cost-when-off statement are in sl_gfx_profile.h. This file is the
 * implementation and the record of which part of the GL specification, and
 * which part of this tree's own frame loop, each decision rests on.
 *
 * ========================================================================
 * WHICH FRAME A ROW BELONGS TO, AND WHY IT IS READ AT THE CLOSE
 * ========================================================================
 *
 * The pumped-frame counter advances BETWEEN a frame's begin and its present.
 * In src/platform/sl_ultra_shim.c the loop body runs, in this order:
 *
 *   1425-1441  the previous frame's present: sl_pres_hold() is evaluated,
 *              sl_gfx_hold_present() is called when the swap is held,
 *              sl_gfx_end_frame() runs, and - only when NOT held - the
 *              regular present is noted (sl_pres_t_reg, sl_pres_note)
 *   1447       sl_gfx_poll(), and the quit path
 *   1457       sl_gfx_begin_frame()          <- this frame opens here
 *   1466-1496  pacing (sl_phase_pace_frame), the game and phase work, probes
 *   1497       sl_frame++                    <- the counter advances HERE
 *
 * So a frame that opens while sl_frame == F is presented at the top of the
 * NEXT iteration, by which time sl_frame == F+1. sl_gfx_end_frame and the
 * regular-present bookkeeping sit in the same block, at the same value.
 *
 * Reading the counter at the scope's OPEN therefore labels the row F while
 * the present it describes happened at F+1: the row joins to a different
 * physical frame, and on an alternating present grid the number it lands on
 * can belong to a frame whose swap was held. A synthetic probe makes it
 * concrete - open at 40, advance the pump to 41, present, and the row reads
 * 40.
 *
 * The identity is therefore taken at the CLOSE of the OUTERMOST open scope,
 * from sl_frames_completed() at that moment, which is the same counter value
 * the regular present is recorded at. No offset is added and no ratio between
 * a render and a pumped frame is invented: the number written is the one the
 * counter holds when the frame ends.
 *
 * NESTED SCOPES SHARE THE FRAME'S IDENTITY. A scope opened and closed inside
 * the frame's work (sl_ultra_shim.c 1466-1496) runs entirely before the
 * increment at 1497, so reading the counter at ITS close would give F while
 * its enclosing frame gives F+1, and the two rows of one frame would not join.
 * Instead a scope group is opened with the outermost scope, every record in
 * it is stamped with the one identity when that outermost scope closes, and
 * rows are not written until their identity is known. One frame, one number,
 * for every scope in it.
 *
 * A HELD FRAME IS A DIFFERENT SERIES. sl_ultra_shim.c presents regularly only
 * when sl_pres_hold() was false; a held frame still submits and still ends,
 * but no regular present is noted for it. Those rows carry the scope name
 * "frame-held" rather than "frame", so the "frame" series is exactly the
 * regular-present grid and a held frame's work can never be read as a
 * presented frame's. The consumer groups by scope name, so this needs no
 * extra column.
 *
 * ========================================================================
 * WHAT THE WHOLE-FRAME SPAN ACTUALLY CONTAINS
 * ========================================================================
 *
 * The "frame" scope opens in sdl_begin and closes in sdl_end. Between those
 * two points the loop above runs the pacing wait, the game and phase work,
 * the developer probes, the counter increment and the limit check, and then
 * the next iteration's backbuffer hash and hold decision. So the number in
 * cpu_submit_ms for this scope is the frame's CLEAR-TO-PRESENT WALL SPAN. It
 * is not renderer CPU submission time, it is not CPU execution time, and it
 * is not exclusive of anything: a pacing sleep, a game-logic spike and an
 * unrelated CPU gap all land inside it.
 *
 * gpu_elapsed_ms is likewise the GPU timeline delta between the two timer
 * queries, not exclusive GPU-busy time: idle between the two points, and work
 * another client submitted, are inside the interval.
 *
 * The two spans OVERLAP, so subtracting one from the other does not isolate a
 * bottleneck and must not be presented as doing so. A narrower, genuinely
 * renderer-only submission figure needs a narrower scope; the per-scope API
 * exists for that, and placing one is separate work from this file.
 *
 * ========================================================================
 * WHICH COUNTER, AND WHY TWO PATHS
 * ========================================================================
 *
 * Preferred: a PAIR of GL_TIMESTAMP counters, one at scope begin and one at
 * scope end, and the elapsed time is their difference. ARB_timer_query
 * (registry revision 13, 2014-08-09) makes this the only mechanism that
 * survives nesting: "QueryCounter timer queries can be used within a
 * BeginQuery / EndQuery block where the <target> is TIME_ELAPSED and it does
 * not affect the result of that query object." Its Issue (6) states the
 * opposite limit for the interval form - "An application can not perform
 * multiple occlusion queries or multiple timer queries simultaneously."
 *
 * Fallback: one GL_TIME_ELAPSED interval query per scope, used only when the
 * timestamp counter is absent. EXT_timer_query (revision 3, 2013-06-25)
 * defines no QueryCounter and no TIMESTAMP target at all - its only timer
 * target is TIME_ELAPSED_EXT - and it carries the same one-at-a-time rule:
 * "If BeginQuery is called with an <id> of zero, if the active query object
 * name for <target> is non-zero, or if <id> is the active query object name
 * for any query type, the error INVALID_OPERATION is generated." So on this
 * path a NESTED scope cannot be timed, and is reported unsupported rather than
 * given a number that belongs to another scope.
 *
 * UNIT. Both specs resolve the same way. ARB_timer_query 5.4: "the elapsed
 * time (in nanoseconds) is written to the corresponding query object as the
 * query result value", and its Issue (2) "What unit of time will time
 * intervals be returned in? RESOLVED: Nanoseconds (10^-9 seconds)."
 * EXT_timer_query repeats both. Nanoseconds are divided by 1e6 here, once, to
 * reach the milliseconds the CSV column is named in.
 *
 * WHY POLLING IS NOT WAITING. ARB_timer_query 6.1.6: "There may be an
 * indeterminate delay before the above query returns. If <pname> is
 * QUERY_RESULT_AVAILABLE, FALSE is returned if such a delay would be required;
 * otherwise TRUE is returned." That is the whole reason GL_QUERY_RESULT is
 * never read here until availability has been observed: the same section says
 * of the result query that "Querying the state for any given query object
 * forces that occlusion query to complete within a finite amount of time" -
 * i.e. reading the result is the blocking call, and asking whether it is
 * available is not. ARB_occlusion_query, which defines this machinery and the
 * token values, puts it plainly: "A result of TRUE means no wait would be
 * required; a result of FALSE means that some wait would occur."
 *
 * One availability pass runs per frame, at frame_begin, over the records in
 * flight - a LATER submission than the one being measured, which is what keeps
 * the collection off the submitting frame's critical path. Ordering is
 * guaranteed, so the pass could stop at the first record that is not ready:
 * "It must always be true that if any query object returns a result available
 * of TRUE, all queries of the same type issued prior to that query must also
 * return TRUE" (ARB_timer_query 6.1.6). It does not stop early, because the
 * ring holds at most SL_GFX_PROFILE_RING records and a full scan is cheaper
 * than the reasoning about mixed targets would be.
 *
 * ZERO COUNTER BITS IS NOT SUPPORT. Both specs allow an implementation to
 * report no usable counter: "The number of query counter bits may be zero, in
 * which case the counter contains no useful information" (ARB_timer_query
 * 6.1.6), and for the result in that case, "If the number of query counter
 * bits for target is zero, then the result is returned as a single integer
 * with the value zero". A zero returned for that reason is exactly the
 * fabricated number this producer must never emit, so GL_QUERY_COUNTER_BITS is
 * read for the chosen target and a zero disqualifies the path.
 *
 * EVERY NAME THE DRIVER HANDED OVER IS OWNED. The query pool is taken in one
 * glGenQueries call. A call that errors, or that comes back with fewer usable
 * names than were asked for, still leaves this process holding whatever names
 * it did produce, and those have to be deleted while the context is current.
 * The count of owned names is therefore recorded from what the call actually
 * wrote, BEFORE the pool is judged usable - recording it only after a full
 * pool was confirmed leaks every name of a short pool, because teardown then
 * believes it generated nothing.
 *
 * WHAT CONTEXT THIS RUNS IN. sl_gfx_sdl.c sets DOUBLEBUFFER, DEPTH_SIZE and
 * STENCIL_SIZE and NO SDL_GL_CONTEXT_* attribute at all, so SDL asks for no
 * version and no profile and the driver hands back its default - in practice a
 * compatibility context of whatever version the driver offers, and on a
 * software fallback possibly GL 1.1 with none of this present. Nothing here
 * may assume GL 3.3: the version string is parsed, the extension string is
 * consulted when the context exposes one, every entry point is resolved
 * through the backend's own resolver, and the capability is then PROBED with a
 * real GL call whose error code is checked. A resolved function pointer is not
 * evidence - drivers export names their context does not implement.
 */
#ifndef __sgi

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>                     /* open/close: the create-only destination */
#endif
#ifndef O_BINARY
#define O_BINARY 0                  /* no text translation to suppress */
#endif

#ifdef SL_GFX_PROFILE_TEST
/* Offline mock build (-DSL_GFX_PROFILE_TEST). The stub supplies the GL types
 * and tokens, the entry points, the proc-address resolver, the frame counter,
 * the environment and the clock - all SYNTHETIC. Everything below this include
 * is the shipped logic, compiled unchanged. */
#include "sl_gfx_profile_testgl.h"
#else
#ifdef _WIN32
/* windows.h must precede <GL/gl.h>, which takes APIENTRY and WINGDIAPI from
 * it - the same include order the rest of src/gfx uses. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>
#include <SDL2/SDL.h>
#endif

#include "sl_gfx_profile.h"

/* ---- the environment, the clock and the frame unit ----------------------- */

#ifndef SL_GFX_PROFILE_TEST
/* The backend's own resolver: NULL before a context exists, and NULL when this
 * backend is not the one that came up, so every caller needs a fallback - and
 * this one's fallback is to report unsupported. Declared here because
 * sl_gfx_sdl.c declares it at its definition and src/gfx compiles against host
 * headers with no include path of its own. */
void *sl_gl_proc(const char *name);

/* The pumped-frame counter (src/platform/sl_ultra_shim.c:
 * "unsigned sl_frames_completed(void) { return (unsigned) sl_frame; }"), taken
 * through a function so the linker cannot bind the name to anything else - the
 * reason that file hands the value over this way in the first place. Declared
 * at the point of use, as gunfire.c, sl_gfx_dl.c and sl_gfx_sdl.c all do. */
extern unsigned sl_frames_completed(void);

static const char *prof_env(const char *name) { return getenv(name); }

/* WALL time, the same monotonic counter the presented-frame meter in
 * sl_gfx_sdl.c uses. Read either side of the scope, so it includes everything
 * that happened in between - see the span note at the top of this file. */
static double prof_now_ms(void)
{
    Uint64 f = SDL_GetPerformanceFrequency();
    if (f == 0) return 0.0;
    return (double) SDL_GetPerformanceCounter() * 1000.0 / (double) f;
}
#endif /* !SL_GFX_PROFILE_TEST */

/* ---- GL tokens ----------------------------------------------------------
 *
 * <GL/gl.h> on this toolchain declares GL 1.1, so none of these exist there.
 * Values are the registry's, not this file's guesses:
 *   QUERY_COUNTER_BITS 0x8864, QUERY_RESULT 0x8866, QUERY_RESULT_AVAILABLE
 *   0x8867  - ARB_occlusion_query, "New Tokens" (the ARB-suffixed names of the
 *             same values; GL 1.5 promoted them unchanged)
 *   TIME_ELAPSED 0x88BF, TIMESTAMP 0x8E28  - ARB_timer_query, "New Tokens"
 *             (EXT_timer_query gives TIME_ELAPSED_EXT the same 0x88BF)
 */
#ifndef GL_QUERY_COUNTER_BITS
#define GL_QUERY_COUNTER_BITS      0x8864
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT            0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE  0x8867
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED            0x88BF
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP               0x8E28
#endif

/* GL's uint64. 64 bits exactly, by the spec's own prototype
 * "void GetQueryObjectui64v(uint id, enum pname, uint64 *params);" - and this
 * is a POINTER parameter, so the width has to be right or the driver writes
 * past it. unsigned long long is 64 bits on this toolchain. */
typedef unsigned long long sl_prof_u64;

/* APIENTRY ON EVERY TYPEDEF. These entry points are __stdcall on Windows; a
 * typedef without APIENTRY makes the caller clean up a callee-cleaned frame
 * and corrupts its own frame a few calls later, landing as a fault somewhere
 * else entirely. The same note sits over the typedefs in sl_gfx_bggpu.c and
 * sl_gfx_dl.c, where it was measured. */
typedef void (APIENTRY *sl_pfn_GenQueries)(GLsizei, GLuint *);
typedef void (APIENTRY *sl_pfn_DeleteQueries)(GLsizei, const GLuint *);
typedef void (APIENTRY *sl_pfn_BeginQuery)(GLenum, GLuint);
typedef void (APIENTRY *sl_pfn_EndQuery)(GLenum);
typedef void (APIENTRY *sl_pfn_QueryCounter)(GLuint, GLenum);
typedef void (APIENTRY *sl_pfn_GetQueryiv)(GLenum, GLenum, GLint *);
typedef void (APIENTRY *sl_pfn_GetQueryObjectiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *sl_pfn_GetQueryObjectui64v)(GLuint, GLenum, sl_prof_u64 *);

static sl_pfn_GenQueries           p_GenQueries;
static sl_pfn_DeleteQueries        p_DeleteQueries;
static sl_pfn_BeginQuery           p_BeginQuery;
static sl_pfn_EndQuery             p_EndQuery;
static sl_pfn_QueryCounter         p_QueryCounter;
static sl_pfn_GetQueryiv           p_GetQueryiv;
static sl_pfn_GetQueryObjectiv     p_GetQueryObjectiv;
static sl_pfn_GetQueryObjectui64v  p_GetQueryObjectui64v;

/* ---- state --------------------------------------------------------------- */

enum {                              /* how, if at all, GL can time a scope */
    PROF_GL_UNTRIED = 0,
    PROF_GL_TIMESTAMP,              /* QueryCounter pairs; nesting works    */
    PROF_GL_ELAPSED,                /* one interval query; depth 0 only     */
    PROF_GL_NONE                    /* truthfully unsupported               */
};

enum { PROF_REC_FREE = 0, PROF_REC_OPEN, PROF_REC_CLOSED };

struct prof_rec {
    char         scope[SL_GFX_PROFILE_NAME_MAX];
    double       t0_ms;
    double       cpu_ms;
    double       gpu_ms;
    GLuint       q0, q1;            /* begin/end timestamps, or q0 = interval */
    unsigned     group;             /* the outermost scope this belongs to    */
    unsigned     frame;             /* valid only once framed is set          */
    int          framed;            /* the frame identity has been stamped    */
    int          gpu_done;          /* a GPU result has been collected        */
    int          state;
    int          gpu;               /* PROF_GL_* for THIS record              */
};

struct prof_scope { int slot; char name[SL_GFX_PROFILE_NAME_MAX]; };

static int    s_on = -1;            /* -1 untried, 0 off, 1 on              */
static FILE  *s_file;
static int    s_write_failed;
static int    s_gl = PROF_GL_UNTRIED;
static int    s_pool_ok;            /* the pool is complete and usable      */
static GLuint s_q[SL_GFX_PROFILE_RING * 2];
static int    s_q_n;                /* names this process OWNS and must free */
static struct prof_rec   s_rec[SL_GFX_PROFILE_RING];
static struct prof_scope s_stack[SL_GFX_PROFILE_MAX_DEPTH];
static int    s_depth;
static int    s_over_depth;         /* begins past MAX_DEPTH, awaiting ends  */
static int    s_elapsed_active;     /* a TIME_ELAPSED query is running       */
static int    s_cursor;
static unsigned s_group;            /* the scope group being filled          */
static struct sl_gfx_profile_counts s_n;

/* The last frame number a row was WRITTEN for, per scope name. The consumer
 * rejects a duplicate (frame,scope) pair outright, so a second record landing
 * on a frame number that scope has already used is dropped here instead of
 * poisoning the whole file at import. The pumped-frame counter does not
 * advance for every render, so this is reachable and not theoretical. The
 * check runs when the identity is stamped, because that is the first moment
 * the pair is known. */
static struct { char name[SL_GFX_PROFILE_NAME_MAX]; unsigned frame; int used; }
       s_last[SL_GFX_PROFILE_MAX_DEPTH * 2];

/* ---- the switch ---------------------------------------------------------- */

int sl_gfx_profile_active(void)
{
    if (s_on < 0) {
        const char *v = prof_env("SL_RENDER_PROFILE");
        s_on = (v != NULL && *v != '\0');
    }
    return s_on == 1;
}

static const char *prof_path(void)
{
    const char *v = prof_env("SL_RENDER_PROFILE");
    return (v != NULL && *v != '\0') ? v : NULL;
}

static void prof_disable(void)
{
    s_on = 0;
    s_gl = PROF_GL_NONE;
}

/* ---- the CSV ------------------------------------------------------------- */

/* CREATE-ONLY, and atomically so: O_CREAT|O_EXCL fails if the path exists, so
 * there is no window between a check and a truncation in which something else
 * could create the file. An existing destination is refused and the producer
 * goes off for the process - a profiling run names a fresh path, and silently
 * overwriting a previous run's evidence is the failure this rule prevents. */
static int prof_open(void)
{
    const char *path = prof_path();
    int fd;

    if (s_file != NULL) return 1;
    if (path == NULL) { prof_disable(); return 0; }

    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
    if (fd < 0) {
        if (errno == EEXIST)
            fprintf(stderr, "sightline profile: SL_RENDER_PROFILE='%s' already exists - "
                            "refusing to overwrite it; profiling is off for this run\n", path);
        else
            fprintf(stderr, "sightline profile: SL_RENDER_PROFILE='%s' could not be created "
                            "(%s); profiling is off for this run\n", path, strerror(errno));
        prof_disable();
        return 0;
    }
    s_file = fdopen(fd, "wb");
    if (s_file == NULL) {
        close(fd);
        fprintf(stderr, "sightline profile: SL_RENDER_PROFILE='%s' opened but could not be "
                        "buffered; profiling is off for this run\n", path);
        prof_disable();
        return 0;
    }
    /* Binary mode and an explicit \n: the column order is the consumer's
     * METRIC_FIELDS and its reader compares the header list exactly. */
    if (fprintf(s_file, "frame,scope,cpu_submit_ms,gpu_elapsed_ms,gpu_status\n") < 0
        || fflush(s_file) != 0) {
        fprintf(stderr, "sightline profile: the header could not be written to '%s'; "
                        "profiling is off for this run\n", path);
        fclose(s_file); s_file = NULL;
        prof_disable();
        return 0;
    }
    fprintf(stderr, "sightline profile: SL_RENDER_PROFILE='%s' created; "
                    "one row per scope per frame\n", path);
    return 1;
}

/* One row, flushed. The flush is per row on purpose: the evidence has to
 * survive an abnormal exit, and "a partial failure preserves what was
 * written" is only true if the rows are actually on disk. This cost is paid
 * only on the opt-in path. A failed write latches and stops further rows; the
 * file is left holding everything that went before. */
static void prof_row(unsigned frame, const char *scope, double cpu_ms,
                     int have_gpu, double gpu_ms, const char *status)
{
    int n;

    if (s_file == NULL || s_write_failed) return;
    if (have_gpu)
        n = fprintf(s_file, "%u,%s,%.6f,%.6f,%s\n", frame, scope, cpu_ms, gpu_ms, status);
    else
        n = fprintf(s_file, "%u,%s,%.6f,,%s\n", frame, scope, cpu_ms, status);
    if (n < 0 || fflush(s_file) != 0) {
        s_write_failed = 1;
        fprintf(stderr, "sightline profile: the metric CSV could not be extended - "
                        "the %u row(s) already written are kept and nothing is added\n",
                s_n.rows_ready + s_n.rows_pending + s_n.rows_unsupported);
        return;
    }
    if (strcmp(status, "ready") == 0)            s_n.rows_ready++;
    else if (strcmp(status, "pending") == 0)     s_n.rows_pending++;
    else                                         s_n.rows_unsupported++;
}

/* ---- capability ---------------------------------------------------------- */

static int prof_gl_version_at_least(int want_major, int want_minor)
{
    const char *v = (const char *) glGetString(GL_VERSION);
    int major = 0, minor = 0;

    if (v == NULL) return 0;
    while (*v >= '0' && *v <= '9') { major = major * 10 + (*v - '0'); v++; }
    if (*v == '.') {
        v++;
        while (*v >= '0' && *v <= '9') { minor = minor * 10 + (*v - '0'); v++; }
    }
    return major > want_major || (major == want_major && minor >= want_minor);
}

/* 1 listed, 0 not listed, -1 the context exposes no extension string at all
 * (glGetString(GL_EXTENSIONS) is defined to return NULL in a core profile of
 * 3.2 or later, and this backend requests no profile, so the case is possible
 * without being the normal one). Token-exact, not a substring search: a
 * substring match would accept GL_EXT_timer_query_something for
 * GL_EXT_timer_query. */
static int prof_has_ext(const char *want)
{
    const char *s = (const char *) glGetString(GL_EXTENSIONS);
    size_t n = strlen(want);

    if (s == NULL) return -1;
    while (*s != '\0') {
        size_t k = 0;
        while (s[k] != '\0' && s[k] != ' ') k++;
        if (k == n && strncmp(s, want, n) == 0) return 1;
        s += k;
        while (*s == ' ') s++;
    }
    return 0;
}

static void prof_drain_errors(void)
{
    int guard = 16;                 /* the queue is finite; do not loop forever */
    while (guard-- > 0 && glGetError() != GL_NO_ERROR) { }
}

/* Does this context actually answer for <target>, with usable counter bits?
 * The probe is the evidence; the resolved pointer and the extension string are
 * only the explanation. GetQueryiv on a target the context does not implement
 * generates INVALID_ENUM, and zero bits means "contains no useful
 * information" - both disqualify. */
static int prof_counter_usable(GLenum target)
{
    GLint bits = 0;

    prof_drain_errors();
    p_GetQueryiv(target, GL_QUERY_COUNTER_BITS, &bits);
    if (glGetError() != GL_NO_ERROR) return 0;
    return bits > 0;
}

/* Take ownership of whatever glGenQueries actually wrote, independently of
 * whether the pool is complete. The names are compacted to the front so
 * teardown can delete exactly s_q_n of them in one call; compaction is the
 * identity when the pool is full, and a short pool is never used for timing,
 * so no record ever indexes across a hole. */
static void prof_pool_adopt(void)
{
    int i, k = 0;

    for (i = 0; i < SL_GFX_PROFILE_RING * 2; i++)
        if (s_q[i] != 0) s_q[k++] = s_q[i];
    for (i = k; i < SL_GFX_PROFILE_RING * 2; i++) s_q[i] = 0;
    s_q_n = k;
}

static void prof_gl_detect(void)
{
    int ts_ext, el_ext, core33, base, i, gen_err;
    const char *ver;

    s_gl = PROF_GL_NONE;
    ver = (const char *) glGetString(GL_VERSION);
    if (ver == NULL) ver = "?";

    p_GenQueries          = (sl_pfn_GenQueries)          sl_gl_proc("glGenQueries");
    p_DeleteQueries       = (sl_pfn_DeleteQueries)       sl_gl_proc("glDeleteQueries");
    p_BeginQuery          = (sl_pfn_BeginQuery)          sl_gl_proc("glBeginQuery");
    p_EndQuery            = (sl_pfn_EndQuery)            sl_gl_proc("glEndQuery");
    p_QueryCounter        = (sl_pfn_QueryCounter)        sl_gl_proc("glQueryCounter");
    p_GetQueryiv          = (sl_pfn_GetQueryiv)          sl_gl_proc("glGetQueryiv");
    p_GetQueryObjectiv    = (sl_pfn_GetQueryObjectiv)    sl_gl_proc("glGetQueryObjectiv");
    p_GetQueryObjectui64v = (sl_pfn_GetQueryObjectui64v) sl_gl_proc("glGetQueryObjectui64v");
    if (p_GetQueryObjectui64v == NULL)
        p_GetQueryObjectui64v = (sl_pfn_GetQueryObjectui64v) sl_gl_proc("glGetQueryObjectui64vEXT");

    /* The query-object machinery itself: GL 1.5 / ARB_occlusion_query. Without
     * it there is nothing to build either path on. */
    base = (p_GenQueries != NULL && p_DeleteQueries != NULL && p_GetQueryiv != NULL
            && p_GetQueryObjectiv != NULL && p_GetQueryObjectui64v != NULL);
    if (!base) {
        fprintf(stderr, "sightline profile: this GL exposes no query objects "
                        "(GL \"%s\") - GPU time is reported unsupported\n", ver);
        return;
    }

    ts_ext = prof_has_ext("GL_ARB_timer_query");
    el_ext = prof_has_ext("GL_EXT_timer_query");
    core33 = prof_gl_version_at_least(3, 3);

    if (p_QueryCounter != NULL && (ts_ext == 1 || core33)
        && prof_counter_usable(GL_TIMESTAMP)) {
        s_gl = PROF_GL_TIMESTAMP;
    } else if (p_BeginQuery != NULL && p_EndQuery != NULL
               && (el_ext == 1 || ts_ext == 1 || core33)
               && prof_counter_usable(GL_TIME_ELAPSED)) {
        s_gl = PROF_GL_ELAPSED;
    } else {
        fprintf(stderr, "sightline profile: no usable GL timer counter "
                        "(GL \"%s\"; ARB_timer_query %s, EXT_timer_query %s, "
                        "glQueryCounter %s) - GPU time is reported unsupported\n",
                ver,
                ts_ext == 1 ? "listed" : (ts_ext == 0 ? "absent" : "unlistable"),
                el_ext == 1 ? "listed" : (el_ext == 0 ? "absent" : "unlistable"),
                p_QueryCounter != NULL ? "resolved" : "missing");
        return;
    }

    /* The whole pool in one call, at detection, so no frame ever generates a
     * query object. Two per record: the timestamp path needs a begin and an
     * end, and the interval path uses the first of each pair. */
    prof_drain_errors();
    for (i = 0; i < SL_GFX_PROFILE_RING * 2; i++) s_q[i] = 0;
    p_GenQueries((GLsizei) (SL_GFX_PROFILE_RING * 2), s_q);
    gen_err = (glGetError() != GL_NO_ERROR);
    prof_pool_adopt();              /* own the names BEFORE judging the pool */

    if (gen_err || s_q_n != SL_GFX_PROFILE_RING * 2) {
        s_gl = PROF_GL_NONE;
        s_pool_ok = 0;
        fprintf(stderr, "sightline profile: the query-object pool came back with %d of %d "
                        "name(s)%s - GPU time is reported unsupported, and the %d name(s) "
                        "this process owns are deleted at teardown\n",
                s_q_n, SL_GFX_PROFILE_RING * 2,
                gen_err ? " and a GL error" : "", s_q_n);
        return;
    }
    s_pool_ok = 1;
    fprintf(stderr, "sightline profile: GPU time from %s (GL \"%s\")\n",
            s_gl == PROF_GL_TIMESTAMP ? "GL_TIMESTAMP counter pairs"
                                      : "GL_TIME_ELAPSED interval queries, depth 0 only",
            ver);
}

/* A GL failure inside a query turns the producer honestly unsupported for the
 * rest of the run rather than being written as a number or as a status the
 * consumer does not accept: the record that failed contributes no row and is
 * counted, and every later scope reports unsupported. The query objects stay
 * owned so teardown still deletes exactly what was generated. */
static void prof_gl_fail(const char *what)
{
    if (s_gl == PROF_GL_NONE) return;
    s_gl = PROF_GL_NONE;
    fprintf(stderr, "sightline profile: %s failed in GL - GPU time is reported "
                    "unsupported from here on\n", what);
}

/* ---- the ring ------------------------------------------------------------ */

static int prof_name_ok(const char *name)
{
    size_t i;

    if (name == NULL || *name == '\0') return 0;
    for (i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (i + 1 >= SL_GFX_PROFILE_NAME_MAX) return 0;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

/* Has this scope already produced a row for this frame number? */
static int prof_duplicate(const char *name, unsigned frame)
{
    int i, free_slot = -1;

    for (i = 0; i < (int) (sizeof s_last / sizeof s_last[0]); i++) {
        if (s_last[i].used && strcmp(s_last[i].name, name) == 0) {
            if (s_last[i].frame == frame) return 1;
            s_last[i].frame = frame;
            return 0;
        }
        if (!s_last[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        s_last[free_slot].used = 1;
        s_last[free_slot].frame = frame;
        strcpy(s_last[free_slot].name, name);
    }
    return 0;
}

static int prof_alloc(void)
{
    int i;

    for (i = 0; i < SL_GFX_PROFILE_RING; i++) {
        int k = (s_cursor + i) % SL_GFX_PROFILE_RING;
        if (s_rec[k].state == PROF_REC_FREE) {
            s_cursor = (k + 1) % SL_GFX_PROFILE_RING;
            memset(&s_rec[k], 0, sizeof s_rec[k]);
            s_rec[k].q0 = s_pool_ok ? s_q[k * 2]     : 0;
            s_rec[k].q1 = s_pool_ok ? s_q[k * 2 + 1] : 0;
            return k;
        }
    }
    return -1;
}

/* A record is written once BOTH facts about it exist: which frame it belongs
 * to, and either its GPU time or the knowledge that there will not be one. */
static void prof_try_write(struct prof_rec *r)
{
    if (r->state != PROF_REC_CLOSED || !r->framed) return;
    if (r->gpu == PROF_GL_NONE) {
        /* GL cannot time this scope: either it never could (no timer counter,
         * or a nested scope on the interval-only path), or a query failed and
         * prof_gl_fail has said so once. The CPU span IS measured either way,
         * so the row carries it with the GPU field EMPTY. */
        prof_row(r->frame, r->scope, r->cpu_ms, 0, 0.0, "unsupported");
        r->state = PROF_REC_FREE;
        return;
    }
    if (r->gpu_done) {
        prof_row(r->frame, r->scope, r->cpu_ms, 1, r->gpu_ms, "ready");
        r->state = PROF_REC_FREE;
    }
}

/* The frame identity, taken at the close of the outermost scope and stamped on
 * every record of that scope group - see the note at the top of this file. */
static void prof_stamp_group(void)
{
    unsigned frame = sl_frames_completed();
    int i;

    for (i = 0; i < SL_GFX_PROFILE_RING; i++) {
        struct prof_rec *r = &s_rec[i];
        if (r->state != PROF_REC_CLOSED || r->framed || r->group != s_group) continue;
        if (prof_duplicate(r->scope, frame)) {
            s_n.dropped_duplicate++;
            r->state = PROF_REC_FREE;
            continue;
        }
        r->frame = frame;
        r->framed = 1;
        prof_try_write(r);
    }
}

/* One non-blocking pass over the records awaiting a GPU result. Called from
 * frame_begin - a later submission than the one being collected - and once
 * more at teardown. Reads GL_QUERY_RESULT only where GL_QUERY_RESULT_AVAILABLE
 * has just said no wait would be required. */
static void prof_poll(void)
{
    int i;

    for (i = 0; i < SL_GFX_PROFILE_RING; i++) {
        struct prof_rec *r = &s_rec[i];
        GLint avail = 0;
        GLuint last;
        sl_prof_u64 a = 0, b = 0;

        if (r->state != PROF_REC_CLOSED || r->gpu_done) continue;
        if (r->gpu == PROF_GL_NONE) { prof_try_write(r); continue; }

        if (s_gl == PROF_GL_NONE) {          /* GL failed after this was issued */
            s_n.dropped_error++;
            r->state = PROF_REC_FREE;
            continue;
        }

        /* The LAST query of the record decides: results of one target become
         * available in issue order, so the end timestamp cannot be ready
         * before the begin timestamp. */
        last = (r->gpu == PROF_GL_TIMESTAMP) ? r->q1 : r->q0;
        prof_drain_errors();
        p_GetQueryObjectiv(last, GL_QUERY_RESULT_AVAILABLE, &avail);
        if (glGetError() != GL_NO_ERROR) {
            prof_gl_fail("the availability query");
            s_n.dropped_error++;
            r->state = PROF_REC_FREE;
            continue;
        }
        if (!avail) continue;                /* no wait is taken: try next frame */

        p_GetQueryObjectui64v(last, GL_QUERY_RESULT, &b);
        if (r->gpu == PROF_GL_TIMESTAMP)
            p_GetQueryObjectui64v(r->q0, GL_QUERY_RESULT, &a);
        if (glGetError() != GL_NO_ERROR) {
            prof_gl_fail("the result query");
            s_n.dropped_error++;
            r->state = PROF_REC_FREE;
            continue;
        }
        if (r->gpu == PROF_GL_TIMESTAMP && b < a) {
            /* A counter that ran backwards is not a measurement. No row. */
            s_n.dropped_error++;
            r->state = PROF_REC_FREE;
            continue;
        }
        r->gpu_ms = (double) (r->gpu == PROF_GL_TIMESTAMP ? (b - a) : b) / 1000000.0;
        r->gpu_done = 1;
        prof_try_write(r);
    }
}

/* ---- scopes -------------------------------------------------------------- */

void sl_gfx_profile_scope_begin(const char *name)
{
    struct prof_rec *r;
    int slot, gpu;

    if (!sl_gfx_profile_active()) return;
    if (!prof_open()) return;
    if (s_gl == PROF_GL_UNTRIED) prof_gl_detect();

    if (s_depth == 0) s_group++;    /* a new scope group, whatever happens below */

    if (s_depth >= SL_GFX_PROFILE_MAX_DEPTH) {
        s_over_depth++;
        s_n.misuse++;
        return;
    }
    if (!prof_name_ok(name)) {
        s_stack[s_depth].slot = -1;
        s_stack[s_depth].name[0] = '\0';
        s_depth++;
        s_n.misuse++;
        return;
    }

    /* WHICH COUNTER THIS SCOPE GETS. Timestamp pairs nest; a TIME_ELAPSED
     * interval does not, and a second BeginQuery on that target is an
     * INVALID_OPERATION, so a nested scope on the fallback path is recorded
     * with no GPU time and says unsupported. */
    gpu = s_gl;
    if (s_gl == PROF_GL_ELAPSED && (s_depth > 0 || s_elapsed_active))
        gpu = PROF_GL_NONE;

    slot = prof_alloc();
    if (slot < 0) {
        s_stack[s_depth].slot = -1;
        s_stack[s_depth].name[0] = '\0';
        s_depth++;
        s_n.dropped_ring++;
        return;
    }

    r = &s_rec[slot];
    strcpy(r->scope, name);
    r->group = s_group;
    r->gpu = gpu;
    r->state = PROF_REC_OPEN;

    if (gpu == PROF_GL_TIMESTAMP) {
        prof_drain_errors();
        p_QueryCounter(r->q0, GL_TIMESTAMP);
        if (glGetError() != GL_NO_ERROR) { prof_gl_fail("the begin timestamp"); r->gpu = PROF_GL_NONE; }
    } else if (gpu == PROF_GL_ELAPSED) {
        prof_drain_errors();
        p_BeginQuery(GL_TIME_ELAPSED, r->q0);
        if (glGetError() != GL_NO_ERROR) { prof_gl_fail("the interval query"); r->gpu = PROF_GL_NONE; }
        else s_elapsed_active = 1;
    }

    s_stack[s_depth].slot = slot;
    strcpy(s_stack[s_depth].name, name);
    s_depth++;
    /* The CPU clock is read LAST, so the bookkeeping above is outside the
     * interval the row reports; the matching end reads it first. */
    r->t0_ms = prof_now_ms();
}

void sl_gfx_profile_scope_end(const char *name)
{
    double now;
    struct prof_rec *r;
    int slot;

    if (!sl_gfx_profile_active()) return;
    now = prof_now_ms();

    if (s_over_depth > 0) { s_over_depth--; return; }
    if (s_depth <= 0) { s_n.misuse++; return; }

    s_depth--;
    slot = s_stack[s_depth].slot;
    if (name != NULL && s_stack[s_depth].name[0] != '\0'
        && strcmp(name, s_stack[s_depth].name) != 0)
        s_n.misuse++;               /* LIFO is the contract: the top closes anyway */

    if (slot >= 0) {
        r = &s_rec[slot];
        r->cpu_ms = now - r->t0_ms;
        if (!(r->cpu_ms >= 0.0)) {  /* also catches a NaN clock */
            if (r->gpu == PROF_GL_ELAPSED) { p_EndQuery(GL_TIME_ELAPSED); s_elapsed_active = 0; }
            s_n.dropped_error++;
            r->state = PROF_REC_FREE;
        } else {
            if (r->gpu == PROF_GL_TIMESTAMP) {
                prof_drain_errors();
                p_QueryCounter(r->q1, GL_TIMESTAMP);
                if (glGetError() != GL_NO_ERROR) { prof_gl_fail("the end timestamp"); r->gpu = PROF_GL_NONE; }
            } else if (r->gpu == PROF_GL_ELAPSED) {
                prof_drain_errors();
                p_EndQuery(GL_TIME_ELAPSED);
                s_elapsed_active = 0;
                if (glGetError() != GL_NO_ERROR) { prof_gl_fail("the interval query's end"); r->gpu = PROF_GL_NONE; }
            }
            r->state = PROF_REC_CLOSED;
        }
    }

    /* The outermost scope has just closed, so the frame this group belongs to
     * is the one the counter names NOW. Done even when the outermost scope
     * itself got no ring slot, because its nested scopes still need stamping. */
    if (s_depth == 0) prof_stamp_group();
}

/* The whole-frame scope. The name differs by whether the swap was held,
 * because only a presented frame appears on the regular-present grid
 * (sl_ultra_shim.c 1434: the regular present is noted when NOT held). */
static void prof_frame_end(int presented)
{
    if (!sl_gfx_profile_active()) return;
    if (!presented && s_depth > 0 && s_stack[s_depth - 1].slot >= 0
        && strcmp(s_stack[s_depth - 1].name, "frame") == 0)
        strcpy(s_rec[s_stack[s_depth - 1].slot].scope, "frame-held");
    sl_gfx_profile_scope_end("frame");
}

void sl_gfx_profile_frame_begin(void)
{
    if (!sl_gfx_profile_active()) return;
    if (!prof_open()) return;
    if (s_gl == PROF_GL_UNTRIED) prof_gl_detect();
    prof_poll();                    /* the later submission that collects */
    sl_gfx_profile_scope_begin("frame");
}

void sl_gfx_profile_frame_end(void)      { prof_frame_end(1); }
void sl_gfx_profile_frame_end_held(void) { prof_frame_end(0); }

void sl_gfx_profile_context_invalidate(void)
{
    int i;

    if (!sl_gfx_profile_active()) return;
    /* Query names belong to the context that generated them. Nothing is
     * deleted here: at context-create there is nothing to delete, and at
     * context-destroy sl_gfx_profile_shutdown has already deleted them while
     * the context was current. Records still in flight cannot be collected
     * from a context that no longer owns their queries, so they are dropped. */
    for (i = 0; i < SL_GFX_PROFILE_RING; i++) {
        if (s_rec[i].state != PROF_REC_FREE) {
            s_n.dropped_error++;
            s_rec[i].state = PROF_REC_FREE;
        }
        s_rec[i].q0 = s_rec[i].q1 = 0;
    }
    for (i = 0; i < SL_GFX_PROFILE_RING * 2; i++) s_q[i] = 0;
    s_q_n = 0;
    s_gl = PROF_GL_UNTRIED;
    s_pool_ok = 0;
    s_elapsed_active = 0;
    s_depth = 0;
    s_over_depth = 0;
    p_GenQueries = NULL; p_DeleteQueries = NULL; p_BeginQuery = NULL;
    p_EndQuery = NULL; p_QueryCounter = NULL; p_GetQueryiv = NULL;
    p_GetQueryObjectiv = NULL; p_GetQueryObjectui64v = NULL;
}

void sl_gfx_profile_shutdown(void)
{
    int i;

    if (s_on != 1) return;

    /* The context is still current here, which is the whole reason this is a
     * backend hook and not an atexit handler. */
    if (s_elapsed_active && p_EndQuery != NULL) {
        p_EndQuery(GL_TIME_ELAPSED);
        s_elapsed_active = 0;
    }
    prof_poll();                    /* one last pass, still without waiting */

    for (i = 0; i < SL_GFX_PROFILE_RING; i++) {
        struct prof_rec *r = &s_rec[i];
        if (r->state == PROF_REC_CLOSED && r->framed) {
            /* The result never arrived, and nothing here waits for it. The row
             * carries the measured CPU span and an EMPTY GPU field. */
            prof_row(r->frame, r->scope, r->cpu_ms, 0, 0.0, "pending");
        } else if (r->state == PROF_REC_CLOSED || r->state == PROF_REC_OPEN) {
            /* Closed but never stamped means its outermost scope never closed,
             * so the frame it belongs to is unknown and no honest row exists. */
            s_n.misuse++;
        }
        r->state = PROF_REC_FREE;
    }

    /* Every name this process owns, deleted while the context is current -
     * including a short or partial pool that was never used for timing. */
    if (s_q_n > 0 && p_DeleteQueries != NULL) {
        prof_drain_errors();
        p_DeleteQueries((GLsizei) s_q_n, s_q);
    }
    for (i = 0; i < SL_GFX_PROFILE_RING * 2; i++) s_q[i] = 0;
    s_q_n = 0;
    s_pool_ok = 0;

    if (s_file != NULL) {
        fprintf(stderr, "sightline profile: %u ready, %u pending, %u unsupported row(s); "
                        "dropped %u ring, %u duplicate, %u error; %u misuse\n",
                s_n.rows_ready, s_n.rows_pending, s_n.rows_unsupported,
                s_n.dropped_ring, s_n.dropped_duplicate, s_n.dropped_error, s_n.misuse);
        fclose(s_file);
        s_file = NULL;
    }
    s_depth = 0;
    s_over_depth = 0;
    s_gl = PROF_GL_NONE;
    s_on = 0;                       /* off for the rest of the process */
}

void sl_gfx_profile_counts_get(struct sl_gfx_profile_counts *out)
{
    if (out != NULL) *out = s_n;
}

#ifdef SL_GFX_PROFILE_TEST
/* Offline mock build only. Closes the destination's file DESCRIPTOR and leaves
 * the stream, so the next row's flush fails for real with a real errno - the
 * only honest way to exercise the write-failure latch without a contrived
 * error flag inside the producer. Not compiled into the game. */
void sl_gfx_profile_break_file_for_test(void)
{
    if (s_file != NULL) close(fileno(s_file));
}

void sl_gfx_profile_reset_for_test(void)
{
    int i;
    if (s_file != NULL) { fclose(s_file); s_file = NULL; }
    s_on = -1;
    s_write_failed = 0;
    s_gl = PROF_GL_UNTRIED;
    s_pool_ok = 0;
    s_q_n = 0;
    s_depth = 0;
    s_over_depth = 0;
    s_elapsed_active = 0;
    s_cursor = 0;
    s_group = 0;
    memset(&s_n, 0, sizeof s_n);
    memset(s_rec, 0, sizeof s_rec);
    memset(s_stack, 0, sizeof s_stack);
    memset(s_last, 0, sizeof s_last);
    for (i = 0; i < SL_GFX_PROFILE_RING * 2; i++) s_q[i] = 0;
    p_GenQueries = NULL; p_DeleteQueries = NULL; p_BeginQuery = NULL;
    p_EndQuery = NULL; p_QueryCounter = NULL; p_GetQueryiv = NULL;
    p_GetQueryObjectiv = NULL; p_GetQueryObjectui64v = NULL;
}
#endif

#endif /* !__sgi */
