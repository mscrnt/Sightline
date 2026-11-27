/**
 * SYNTHETIC GL, clock, environment and frame counter for the offline tests.
 *
 * EVERY NUMBER THIS HEADER'S IMPLEMENTATION PRODUCES IS FABRICATED. The GL
 * version strings, extension strings, counter-bit answers, timestamps and
 * elapsed values are invented by the test and prove nothing about any real
 * driver or any real frame. What they do prove is the producer's own logic:
 * which path it chooses, when it reads a result, what it writes, what it
 * refuses to write, and what it deletes.
 *
 * There is NO window, NO native graphics API and NO game here. The stub is
 * plain C with no link-time dependency on opengl32 or SDL: a test built with
 * it cannot touch a GPU even by accident.
 *
 * The stub also acts as a WATCHDOG on the producer. It counts availability
 * queries and result reads, and it records a violation if a result is read
 * before availability was observed, if a second interval query is begun while
 * one is active, if a query is used after being deleted, or if any GL entry
 * point is called after the test marks the context destroyed. A test asserts
 * those counters are zero; that is how "never waits" and "deletes while the
 * context is valid" are checked rather than asserted.
 */
#ifndef SL_GFX_PROFILE_TESTGL_H
#define SL_GFX_PROFILE_TESTGL_H

#include <stddef.h>

/* ---- the GL types and tokens the producer uses --------------------------- */

typedef unsigned int   GLenum;
typedef unsigned int   GLuint;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;

#define APIENTRY                            /* the stub is cdecl throughout */

#define GL_NO_ERROR     0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_OPERATION 0x0502
#define GL_VERSION      0x1F02
#define GL_EXTENSIONS   0x1F03

const GLubyte *glGetString(GLenum name);
GLenum         glGetError(void);

/* ---- what the producer takes from its host ------------------------------- */

void       *sl_gl_proc(const char *name);
unsigned    sl_frames_completed(void);
const char *prof_env(const char *name);
double      prof_now_ms(void);

/* ---- the test's control surface (all SYNTHETIC) ------------------------- */

struct sl_testgl {
    /* what the fake driver is */
    const char *version;        /* glGetString(GL_VERSION); NULL to withhold  */
    const char *extensions;     /* glGetString(GL_EXTENSIONS); NULL = none    */
    int have_query_objects;     /* glGenQueries & friends resolve             */
    int have_begin_end;         /* glBeginQuery/glEndQuery resolve            */
    int have_query_counter;     /* glQueryCounter resolves                    */
    int have_ui64;              /* glGetQueryObjectui64v resolves             */
    int ui64_ext_only;          /* only the ...ui64vEXT name resolves         */
    int ts_bits;                /* GL_QUERY_COUNTER_BITS for GL_TIMESTAMP;
                                 * -1 makes GetQueryiv raise INVALID_ENUM     */
    int el_bits;                /* the same for GL_TIME_ELAPSED               */
    int gen_fails;              /* glGenQueries raises an error               */
    int gen_short;              /* glGenQueries hands back a zero name        */

    /* how the fake GPU behaves */
    int ready_after;            /* ticks before a result becomes available;
                                 * a negative value means never               */
    unsigned long long gpu_ns;  /* the fake GPU clock, in nanoseconds         */
    int fail_avail_at;          /* raise an error on the Nth availability
                                 * query (1-based); 0 = never                 */

    /* the host side */
    double   now_ms;            /* what prof_now_ms returns                   */
    unsigned frame;             /* what sl_frames_completed returns           */
    int      context_destroyed; /* set by the test AFTER teardown             */

    /* what the stub observed */
    long tick;                  /* advanced by the test: fake GPU progress    */
    long proc_calls, getstring_calls;
    long gen_calls, delete_calls, generated, deleted;
    long avail_calls, result_calls, counter_calls, begin_calls, end_calls;
    long v_read_before_available;
    long v_double_begin;
    long v_use_after_delete;
    long v_call_after_context_destroyed;
};

extern struct sl_testgl tg;

/* What prof_env reports for SL_RENDER_PROFILE. NULL means unset, which is the
 * default-off case; the real process environment is never touched. */
extern const char *sl_testgl_env_value;

void sl_testgl_reset(void);         /* a clean fake driver, nothing resolved */
void sl_testgl_tick(long n);        /* advance the fake GPU by n ticks       */

#endif /* SL_GFX_PROFILE_TESTGL_H */
