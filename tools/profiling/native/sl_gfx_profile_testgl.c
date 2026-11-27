/* SYNTHETIC GL / clock / environment for the offline tests. See the header for
 * what this does and does not prove. No window, no native graphics API, no
 * game: nothing here links against opengl32 or SDL.
 *
 * NOTE WHAT IS ABSENT. There is no glFinish, no glFlush and no
 * glGetInteger64v in this stub. If the producer ever called one, the test
 * executable would fail to LINK - the absence of those symbols is the proof
 * that the producer never waits, and it is checked by the build, not by an
 * assertion. */
#include "sl_gfx_profile_testgl.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>

struct sl_testgl tg;

#define TQ_MAX 4096
#define TQ_NEVER LONG_MAX

static struct {
    int  alive, deleted, issued;
    long ready_tick;
    unsigned long long value;
} q[TQ_MAX + 1];

static GLuint  next_name;
static GLenum  gl_err;
static GLuint  active_elapsed;
static unsigned long long elapsed_start;

/* The producer's own tokens, repeated here because the stub has to recognise
 * them. The values are the registry's; see the citation block in
 * sl_gfx_profile.c. */
#define T_QUERY_COUNTER_BITS      0x8864
#define T_QUERY_RESULT            0x8866
#define T_QUERY_RESULT_AVAILABLE  0x8867
#define T_TIME_ELAPSED            0x88BF
#define T_TIMESTAMP               0x8E28

void sl_testgl_reset(void)
{
    memset(&tg, 0, sizeof tg);
    memset(q, 0, sizeof q);
    next_name = 0;
    gl_err = GL_NO_ERROR;
    active_elapsed = 0;
    elapsed_start = 0;
    tg.ts_bits = -1;            /* nothing is available until a test says so */
    tg.el_bits = -1;
    tg.ready_after = 0;
    tg.now_ms = 1000.0;
    tg.gpu_ns = 5000000000ULL;
    tg.tick = 0;
}

void sl_testgl_tick(long n) { tg.tick += n; }

static void guard(void)
{
    if (tg.context_destroyed) tg.v_call_after_context_destroyed++;
}

static long ready_tick_now(void)
{
    return tg.ready_after < 0 ? TQ_NEVER : tg.tick + tg.ready_after;
}

/* ---- the host side ------------------------------------------------------- */

/* One variable matters to the producer, and the test sets it here rather than
 * through putenv, so no real process environment is touched. NULL stands for
 * "unset", which is the default-off case. */
const char *sl_testgl_env_value;

const char *prof_env(const char *name)
{
    if (name != NULL && strcmp(name, "SL_RENDER_PROFILE") == 0)
        return sl_testgl_env_value;
    return NULL;
}

double   prof_now_ms(void)        { return tg.now_ms; }
unsigned sl_frames_completed(void) { return tg.frame; }

/* ---- the fake GL --------------------------------------------------------- */

static void APIENTRY tg_GenQueries(GLsizei n, GLuint *ids)
{
    GLsizei i;
    guard();
    tg.gen_calls++;
    if (tg.gen_fails) { gl_err = GL_INVALID_OPERATION; return; }
    for (i = 0; i < n; i++) {
        if (tg.gen_short && i == n - 1) { ids[i] = 0; continue; }
        if (next_name >= TQ_MAX) { gl_err = GL_INVALID_OPERATION; return; }
        next_name++;
        q[next_name].alive = 1;
        q[next_name].deleted = 0;
        ids[i] = next_name;
        tg.generated++;
    }
}

static void APIENTRY tg_DeleteQueries(GLsizei n, const GLuint *ids)
{
    GLsizei i;
    guard();
    tg.delete_calls++;
    for (i = 0; i < n; i++) {
        if (ids[i] == 0 || ids[i] > TQ_MAX) continue;
        if (q[ids[i]].alive && !q[ids[i]].deleted) {
            q[ids[i]].deleted = 1;
            tg.deleted++;
        }
    }
}

static void APIENTRY tg_QueryCounter(GLuint id, GLenum target)
{
    guard();
    tg.counter_calls++;
    if (target != T_TIMESTAMP) { gl_err = GL_INVALID_ENUM; return; }
    if (id == 0 || id > TQ_MAX || !q[id].alive) { gl_err = GL_INVALID_OPERATION; return; }
    if (q[id].deleted) { tg.v_use_after_delete++; gl_err = GL_INVALID_OPERATION; return; }
    q[id].issued = 1;
    q[id].value = tg.gpu_ns;
    q[id].ready_tick = ready_tick_now();
}

static void APIENTRY tg_BeginQuery(GLenum target, GLuint id)
{
    guard();
    tg.begin_calls++;
    if (target != T_TIME_ELAPSED) { gl_err = GL_INVALID_ENUM; return; }
    if (active_elapsed != 0) {
        /* EXT_timer_query / ARB_timer_query: only one timer query at a time. */
        tg.v_double_begin++;
        gl_err = GL_INVALID_OPERATION;
        return;
    }
    if (id == 0 || id > TQ_MAX || !q[id].alive) { gl_err = GL_INVALID_OPERATION; return; }
    if (q[id].deleted) { tg.v_use_after_delete++; gl_err = GL_INVALID_OPERATION; return; }
    active_elapsed = id;
    elapsed_start = tg.gpu_ns;
}

static void APIENTRY tg_EndQuery(GLenum target)
{
    guard();
    tg.end_calls++;
    if (target != T_TIME_ELAPSED) { gl_err = GL_INVALID_ENUM; return; }
    if (active_elapsed == 0) { gl_err = GL_INVALID_OPERATION; return; }
    q[active_elapsed].issued = 1;
    q[active_elapsed].value = tg.gpu_ns - elapsed_start;
    q[active_elapsed].ready_tick = ready_tick_now();
    active_elapsed = 0;
}

static void APIENTRY tg_GetQueryiv(GLenum target, GLenum pname, GLint *params)
{
    int bits;
    guard();
    if (pname != T_QUERY_COUNTER_BITS) { gl_err = GL_INVALID_ENUM; return; }
    if (target == T_TIMESTAMP)          bits = tg.ts_bits;
    else if (target == T_TIME_ELAPSED)  bits = tg.el_bits;
    else { gl_err = GL_INVALID_ENUM; return; }
    /* A negative setting stands for "this context does not know the target":
     * GetQueryiv then raises INVALID_ENUM and writes nothing, exactly as a
     * driver without the extension does. */
    if (bits < 0) { gl_err = GL_INVALID_ENUM; return; }
    *params = (GLint) bits;
}

static void APIENTRY tg_GetQueryObjectiv(GLuint id, GLenum pname, GLint *params)
{
    guard();
    if (pname != T_QUERY_RESULT_AVAILABLE) { gl_err = GL_INVALID_ENUM; return; }
    tg.avail_calls++;
    if (tg.fail_avail_at != 0 && tg.avail_calls == tg.fail_avail_at) {
        gl_err = GL_INVALID_OPERATION;
        return;
    }
    if (id == 0 || id > TQ_MAX || !q[id].alive) { gl_err = GL_INVALID_OPERATION; return; }
    if (q[id].deleted) { tg.v_use_after_delete++; gl_err = GL_INVALID_OPERATION; return; }
    *params = (q[id].issued && tg.tick >= q[id].ready_tick) ? 1 : 0;
}

static void APIENTRY tg_GetQueryObjectui64v(GLuint id, GLenum pname,
                                            unsigned long long *params)
{
    guard();
    if (pname != T_QUERY_RESULT) { gl_err = GL_INVALID_ENUM; return; }
    tg.result_calls++;
    if (id == 0 || id > TQ_MAX || !q[id].alive) { gl_err = GL_INVALID_OPERATION; return; }
    if (q[id].deleted) { tg.v_use_after_delete++; gl_err = GL_INVALID_OPERATION; return; }
    /* THE WATCHDOG. Reading a result that is not in fact available is what a
     * driver turns into a stall. The producer must never get here. */
    if (!q[id].issued || tg.tick < q[id].ready_tick) tg.v_read_before_available++;
    *params = q[id].value;
}

const GLubyte *glGetString(GLenum name)
{
    guard();
    tg.getstring_calls++;
    if (name == GL_VERSION)    return (const GLubyte *) tg.version;
    if (name == GL_EXTENSIONS) return (const GLubyte *) tg.extensions;
    return NULL;
}

GLenum glGetError(void)
{
    GLenum e = gl_err;
    guard();
    gl_err = GL_NO_ERROR;
    return e;
}

void *sl_gl_proc(const char *name)
{
    guard();
    tg.proc_calls++;
    if (name == NULL) return NULL;
    if (strcmp(name, "glGenQueries") == 0)
        return tg.have_query_objects ? (void *) tg_GenQueries : NULL;
    if (strcmp(name, "glDeleteQueries") == 0)
        return tg.have_query_objects ? (void *) tg_DeleteQueries : NULL;
    if (strcmp(name, "glGetQueryiv") == 0)
        return tg.have_query_objects ? (void *) tg_GetQueryiv : NULL;
    if (strcmp(name, "glGetQueryObjectiv") == 0)
        return tg.have_query_objects ? (void *) tg_GetQueryObjectiv : NULL;
    if (strcmp(name, "glBeginQuery") == 0)
        return tg.have_begin_end ? (void *) tg_BeginQuery : NULL;
    if (strcmp(name, "glEndQuery") == 0)
        return tg.have_begin_end ? (void *) tg_EndQuery : NULL;
    if (strcmp(name, "glQueryCounter") == 0)
        return tg.have_query_counter ? (void *) tg_QueryCounter : NULL;
    if (strcmp(name, "glGetQueryObjectui64v") == 0)
        return (tg.have_ui64 && !tg.ui64_ext_only) ? (void *) tg_GetQueryObjectui64v : NULL;
    if (strcmp(name, "glGetQueryObjectui64vEXT") == 0)
        return (tg.have_ui64 && tg.ui64_ext_only) ? (void *) tg_GetQueryObjectui64v : NULL;
    return NULL;
}
