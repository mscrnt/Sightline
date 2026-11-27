/* SYNTHETIC GL for the skydome batch module's offline tests. See the header for
 * what this does and does not prove, and for what is deliberately ABSENT from
 * it. No window, no native graphics API, no game: nothing here links against
 * opengl32 or SDL.
 *
 * NOTE WHAT IS MISSING, because the omission is the check. There is no
 * glFinish, no glFlush, no glGetError, no glMapBuffer / glUnmapBuffer and no
 * glReadPixels in this translation unit, and sl_gl_proc hands back none of
 * them. A module that called one would not LINK, so the absence of the symbol
 * is what proves the skydome pass introduces no synchronisation point - a
 * stronger statement than any assertion, and one the build makes rather than
 * the test. The same holds for every shader entry point: none exists, and a
 * RESOLVE attempt is recorded as a violation because a resolve is the only way
 * a post-1.1 name can be reached on this target. */
#include "sl_gfx_skybatch_testgl.h"

#include <string.h>

struct sl_skbgl tg;
struct sl_skbgl_call sl_skbgl_log[SKBGL_LOG];

/* The modelled client-array state. One struct, so a snapshot is a copy. */
static struct sl_skbgl_state st;

/* The buffer objects. Index 0 is never handed out: zero is the unbind. */
static struct {
    int  alive, deleted;
    long epoch;
    long size;
} buf[SKBGL_BUFFERS + 1];

static GLuint next_name;

/* ---- the bookkeeping ---------------------------------------------------- */

static void guard(void)
{
    tg.calls++;
    if (tg.context_destroyed) tg.v_call_after_context_destroyed++;
}

static void push(int kind, long a, long b, long c, long d, long off)
{
    if (tg.logged >= (long) SKBGL_LOG) { tg.v_log_full++; return; }
    sl_skbgl_log[tg.logged].kind = kind;
    sl_skbgl_log[tg.logged].a    = a;
    sl_skbgl_log[tg.logged].b    = b;
    sl_skbgl_log[tg.logged].c    = c;
    sl_skbgl_log[tg.logged].d    = d;
    sl_skbgl_log[tg.logged].off  = off;
    tg.logged++;
}

/* The unit index the client active texture selector names, or -1. */
static int unit_index(void)
{
    const int u = st.unit - GL_TEXTURE0;
    return (u >= 0 && u < SKBGL_UNITS) ? u : -1;
}

/* MAY THIS NAME BE USED? Zero always may - it is the unbind. Otherwise the name
 * has to be alive, undeleted and OF THIS CONTEXT GENERATION; a name from before
 * sl_skbgl_new_context(), and a name this context never handed out, are both
 * refused and recorded. Refusing rather than tolerating is what makes the
 * context-recreation property measurable: a stale name cannot quietly work. */
static int usable(GLuint name)
{
    if (name == 0u) return 1;
    if (name > (GLuint) SKBGL_BUFFERS || !buf[name].alive) {
        tg.v_stale_name++;
        return 0;
    }
    if (buf[name].deleted) { tg.v_use_after_delete++; return 0; }
    if (buf[name].epoch != tg.epoch) { tg.v_stale_name++; return 0; }
    return 1;
}

/* ---- the test's control surface ----------------------------------------- */

static void defaults(void)
{
    int u;
    memset(&st, 0, sizeof st);
    /* GL's own initial client-array state: nothing enabled, four components,
     * GL_FLOAT, stride 0, no pointer, no buffer, unit 0 selected. */
    st.unit = GL_TEXTURE0;
    st.v.size = 4; st.v.type = GL_FLOAT;
    st.c.size = 4; st.c.type = GL_FLOAT;
    for (u = 0; u < SKBGL_UNITS; u++) { st.t[u].size = 4; st.t[u].type = GL_FLOAT; }
    st.st[3] = 1.0f;                       /* (0, 0, 0, 1) */
}

void sl_skbgl_reset(void)
{
    memset(&tg, 0, sizeof tg);
    memset(buf, 0, sizeof buf);
    memset(sl_skbgl_log, 0, sizeof sl_skbgl_log);
    next_name = 0;
    tg.epoch = 1;
    tg.max_texture_units = 4;
    defaults();
}

void sl_skbgl_log_reset(void)
{
    tg.calls = 0;
    tg.logged = 0;
    tg.proc_calls = 0;
    tg.gen_calls = tg.delete_calls = tg.generated = tg.deleted = 0;
    tg.draw_calls = 0;
    tg.last_upload_bytes = 0;
    memset(tg.last_upload, 0, sizeof tg.last_upload);
    tg.v_draw_foreign_array = 0;
    tg.v_draw_no_element_buffer = 0;
    tg.v_use_after_delete = 0;
    tg.v_stale_name = 0;
    tg.v_call_after_context_destroyed = 0;
    tg.v_program_proc = 0;
    tg.v_sync_proc = 0;
    tg.v_unknown_proc = 0;
    tg.v_unknown_enum = 0;
    tg.v_log_full = 0;
    memset(sl_skbgl_log, 0, sizeof sl_skbgl_log);
}

void sl_skbgl_new_context(void)
{
    tg.epoch++;
    defaults();
}

void sl_skbgl_snapshot(struct sl_skbgl_state *out)
{
    if (out != NULL) *out = st;
}

const char *sl_skbgl_diff(const struct sl_skbgl_state *a,
                          const struct sl_skbgl_state *b,
                          int include_current)
{
    static char name[48];
    int u, i;
    if (a == NULL || b == NULL) return "a null snapshot";
    if (a->unit     != b->unit)     return "GL_CLIENT_ACTIVE_TEXTURE";
    if (a->arr_buf  != b->arr_buf)  return "GL_ARRAY_BUFFER_BINDING";
    if (a->elem_buf != b->elem_buf) return "GL_ELEMENT_ARRAY_BUFFER_BINDING";

#define SKBGL_ARRDIFF(field, label)                                           \
    do {                                                                      \
        if (a->field.on     != b->field.on)     return label ".enable";       \
        if (a->field.size   != b->field.size)   return label ".size";         \
        if (a->field.type   != b->field.type)   return label ".type";         \
        if (a->field.stride != b->field.stride) return label ".stride";       \
        if (a->field.ptr    != b->field.ptr)    return label ".pointer";      \
        if (a->field.buf    != b->field.buf)    return label ".buffer";       \
    } while (0)

    SKBGL_ARRDIFF(v, "GL_VERTEX_ARRAY");
    SKBGL_ARRDIFF(c, "GL_COLOR_ARRAY");
#undef SKBGL_ARRDIFF

    for (u = 0; u < SKBGL_UNITS; u++) {
        const char *what = NULL;
        if (a->t[u].on     != b->t[u].on)     what = "enable";
        else if (a->t[u].size   != b->t[u].size)   what = "size";
        else if (a->t[u].type   != b->t[u].type)   what = "type";
        else if (a->t[u].stride != b->t[u].stride) what = "stride";
        else if (a->t[u].ptr    != b->t[u].ptr)    what = "pointer";
        else if (a->t[u].buf    != b->t[u].buf)    what = "buffer";
        if (what != NULL) {
            /* The unit is part of the answer: "a coordinate array moved" is
             * not a diagnosis, "unit 2's stride moved" is. */
            char digits[4];
            int k = 0;
            if (u >= 10) digits[k++] = (char) ('0' + u / 10);
            digits[k++] = (char) ('0' + u % 10);
            digits[k]   = '\0';
            name[0] = '\0';
            strcat(name, "GL_TEXTURE_COORD_ARRAY[unit ");
            strcat(name, digits);
            strcat(name, "].");
            strcat(name, what);
            return name;
        }
    }

    if (a->n_on != b->n_on) return "GL_NORMAL_ARRAY.enable";
    if (a->i_on != b->i_on) return "GL_INDEX_ARRAY.enable";
    if (a->e_on != b->e_on) return "GL_EDGE_FLAG_ARRAY.enable";
    if (a->s_on != b->s_on) return "GL_SECONDARY_COLOR_ARRAY.enable";
    if (a->f_on != b->f_on) return "GL_FOG_COORD_ARRAY.enable";

    if (include_current) {
        for (i = 0; i < 4; i++) if (a->col[i] != b->col[i]) return "the current colour";
        for (i = 0; i < 4; i++) if (a->st[i]  != b->st[i])  return "the current texture coordinate";
    }
    return NULL;
}

GLuint sl_skbgl_make_buffer(void)
{
    if (next_name >= (GLuint) SKBGL_BUFFERS) return 0u;
    next_name++;
    buf[next_name].alive   = 1;
    buf[next_name].deleted = 0;
    buf[next_name].epoch   = tg.epoch;
    buf[next_name].size    = 0;
    return next_name;
}

int sl_skbgl_buffer_alive(GLuint name)
{
    if (name == 0u || name > (GLuint) SKBGL_BUFFERS) return 0;
    return buf[name].alive && !buf[name].deleted;
}

long sl_skbgl_buffer_size(GLuint name)
{
    if (name == 0u || name > (GLuint) SKBGL_BUFFERS) return -1;
    return buf[name].size;
}

/* ---- the resolved, post-1.1 entry points -------------------------------- */

static void APIENTRY tg_GenBuffers(GLsizei n, GLuint *ids)
{
    GLsizei i;
    guard();
    tg.gen_calls++;
    push(SKBGL_GENBUFFERS, (long) n, 0, 0, 0, 0);
    for (i = 0; i < n; i++) {
        if (tg.gen_zero) { ids[i] = 0u; continue; }
        if (next_name >= (GLuint) SKBGL_BUFFERS) { ids[i] = 0u; continue; }
        next_name++;
        buf[next_name].alive   = 1;
        buf[next_name].deleted = 0;
        buf[next_name].epoch   = tg.epoch;
        buf[next_name].size    = 0;
        ids[i] = next_name;
        tg.generated++;
    }
    if (n > 0 && tg.logged > 0) sl_skbgl_log[tg.logged - 1].b = (long) ids[0];
}

static void APIENTRY tg_DeleteBuffers(GLsizei n, const GLuint *ids)
{
    GLsizei i;
    guard();
    tg.delete_calls++;
    push(SKBGL_DELETEBUFFERS, (long) n, n > 0 ? (long) ids[0] : 0, 0, 0, 0);
    for (i = 0; i < n; i++) {
        if (!usable(ids[i])) continue;      /* refused, and already recorded */
        if (ids[i] == 0u) continue;
        buf[ids[i]].deleted = 1;
        tg.deleted++;
    }
}

static void bind_target(GLenum target, GLuint name)
{
    if (!usable(name)) return;              /* refused, and already recorded */
    if      (target == GL_ARRAY_BUFFER)         st.arr_buf  = name;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) st.elem_buf = name;
    else                                        tg.v_unknown_enum++;
}

static void APIENTRY tg_BindBuffer(GLenum target, GLuint name)
{
    guard();
    push(SKBGL_BINDBUFFER, (long) target, (long) name, 0, 0, 0);
    bind_target(target, name);
}

static void APIENTRY tg_BufferData(GLenum target, ptrdiff_t size,
                                   const GLvoid *data, GLenum usage)
{
    GLuint name;
    guard();
    push(SKBGL_BUFFERDATA, (long) target, (long) size, (long) usage,
         data != NULL ? 1 : 0, 0);
    if      (target == GL_ARRAY_BUFFER)         name = st.arr_buf;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) name = st.elem_buf;
    else { tg.v_unknown_enum++; return; }
    if (name == 0u || !usable(name)) {
        /* glBufferData with no buffer bound is an error in a real
         * implementation; here it is recorded and dropped. */
        if (name == 0u) tg.v_unknown_enum++;
        return;
    }
    buf[name].size = (long) size;
    if (target == GL_ARRAY_BUFFER) {
        size_t keep = (size_t) (size > 0 ? size : 0);
        if (keep > sizeof tg.last_upload) keep = sizeof tg.last_upload;
        tg.last_upload_bytes = (long) size;
        memset(tg.last_upload, 0, sizeof tg.last_upload);
        if (data != NULL && keep > 0u) memcpy(tg.last_upload, data, keep);
    }
}

static void APIENTRY tg_ClientActiveTexture(GLenum unit)
{
    guard();
    push(SKBGL_CLIENTACTIVETEXTURE, (long) unit, 0, 0, 0, 0);
    if ((int) unit < GL_TEXTURE0 || (int) unit >= GL_TEXTURE0 + SKBGL_UNITS) {
        tg.v_unknown_enum++;
        return;
    }
    st.unit = (int) unit;
}

void sl_skbgl_test_bind(GLenum target, GLuint name) { tg_BindBuffer(target, name); }
void sl_skbgl_test_unit(GLenum unit)                { tg_ClientActiveTexture(unit); }

/* ---- the GL 1.1 entry points -------------------------------------------- */

void glGetIntegerv(GLenum pname, GLint *params)
{
    const int u = unit_index();
    guard();
    push(SKBGL_GETINTEGERV, (long) pname, 0, 0, 0, 0);
    if (params == NULL) return;
    switch (pname) {
    case GL_MAX_TEXTURE_UNITS:                  *params = tg.max_texture_units; return;
    case GL_CLIENT_ACTIVE_TEXTURE:              *params = st.unit;              return;
    case GL_ARRAY_BUFFER_BINDING:               *params = (GLint) st.arr_buf;   return;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING:       *params = (GLint) st.elem_buf;  return;
    case GL_VERTEX_ARRAY_SIZE:                  *params = st.v.size;            return;
    case GL_VERTEX_ARRAY_TYPE:                  *params = st.v.type;            return;
    case GL_VERTEX_ARRAY_STRIDE:                *params = st.v.stride;          return;
    case GL_VERTEX_ARRAY_BUFFER_BINDING:        *params = (GLint) st.v.buf;     return;
    case GL_COLOR_ARRAY_SIZE:                   *params = st.c.size;            return;
    case GL_COLOR_ARRAY_TYPE:                   *params = st.c.type;            return;
    case GL_COLOR_ARRAY_STRIDE:                 *params = st.c.stride;          return;
    case GL_COLOR_ARRAY_BUFFER_BINDING:         *params = (GLint) st.c.buf;     return;
    default: break;
    }
    if (u < 0) { tg.v_unknown_enum++; return; }
    switch (pname) {
    case GL_TEXTURE_COORD_ARRAY_SIZE:           *params = st.t[u].size;         return;
    case GL_TEXTURE_COORD_ARRAY_TYPE:           *params = st.t[u].type;         return;
    case GL_TEXTURE_COORD_ARRAY_STRIDE:         *params = st.t[u].stride;       return;
    case GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING: *params = (GLint) st.t[u].buf;  return;
    default: tg.v_unknown_enum++; return;
    }
}

void glGetPointerv(GLenum pname, GLvoid **params)
{
    const int u = unit_index();
    guard();
    push(SKBGL_GETPOINTERV, (long) pname, 0, 0, 0, 0);
    if (params == NULL) return;
    if (pname == GL_VERTEX_ARRAY_POINTER) { *params = (GLvoid *) (size_t) st.v.ptr; return; }
    if (pname == GL_COLOR_ARRAY_POINTER)  { *params = (GLvoid *) (size_t) st.c.ptr; return; }
    if (pname == GL_TEXTURE_COORD_ARRAY_POINTER && u >= 0) {
        *params = (GLvoid *) (size_t) st.t[u].ptr;
        return;
    }
    tg.v_unknown_enum++;
}

/* The enable this token names, or NULL. One mapping, used by glIsEnabled and
 * the enable / disable pair, so the three can never disagree about which bit a
 * token selects. */
static int *enable_slot(GLenum cap)
{
    const int u = unit_index();
    switch (cap) {
    case GL_VERTEX_ARRAY:           return &st.v.on;
    case GL_COLOR_ARRAY:            return &st.c.on;
    case GL_TEXTURE_COORD_ARRAY:    return u >= 0 ? &st.t[u].on : NULL;
    case GL_NORMAL_ARRAY:           return &st.n_on;
    case GL_INDEX_ARRAY:            return &st.i_on;
    case GL_EDGE_FLAG_ARRAY:        return &st.e_on;
    case GL_SECONDARY_COLOR_ARRAY:  return &st.s_on;
    case GL_FOG_COORD_ARRAY:        return &st.f_on;
    default:                        return NULL;
    }
}

GLboolean glIsEnabled(GLenum cap)
{
    int *slot;
    guard();
    push(SKBGL_ISENABLED, (long) cap, 0, 0, 0, 0);
    slot = enable_slot(cap);
    if (slot == NULL) { tg.v_unknown_enum++; return GL_FALSE; }
    return *slot ? GL_TRUE : GL_FALSE;
}

void glEnableClientState(GLenum cap)
{
    int *slot;
    guard();
    push(SKBGL_ENABLECLIENT, (long) cap, 0, 0, 0, 0);
    slot = enable_slot(cap);
    if (slot == NULL) { tg.v_unknown_enum++; return; }
    *slot = 1;
}

void glDisableClientState(GLenum cap)
{
    int *slot;
    guard();
    push(SKBGL_DISABLECLIENT, (long) cap, 0, 0, 0, 0);
    slot = enable_slot(cap);
    if (slot == NULL) { tg.v_unknown_enum++; return; }
    *slot = 0;
}

/* A pointer command records the ARRAY_BUFFER binding in force into the array,
 * which is what makes `ptr` an offset into that buffer rather than an address -
 * section 2.9 of the OpenGL 2.1 specification. The stub models that, because it
 * is the behaviour the module's save-and-restore depends on. */
static void set_array(struct sl_skbgl_arr *arr, GLint size, GLenum type,
                      GLsizei stride, const GLvoid *ptr)
{
    arr->size   = (int) size;
    arr->type   = (int) type;
    arr->stride = (int) stride;
    arr->ptr    = (long) (size_t) ptr;
    arr->buf    = st.arr_buf;
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    guard();
    push(SKBGL_VERTEXPOINTER, (long) size, (long) type, (long) stride,
         (long) st.unit, (long) (size_t) ptr);
    set_array(&st.v, size, type, stride, ptr);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    guard();
    push(SKBGL_COLORPOINTER, (long) size, (long) type, (long) stride,
         (long) st.unit, (long) (size_t) ptr);
    set_array(&st.c, size, type, stride, ptr);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    const int u = unit_index();
    guard();
    push(SKBGL_TEXCOORDPOINTER, (long) size, (long) type, (long) stride,
         (long) st.unit, (long) (size_t) ptr);
    if (u < 0) { tg.v_unknown_enum++; return; }
    set_array(&st.t[u], size, type, stride, ptr);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    int u;
    guard();
    tg.draw_calls++;
    push(SKBGL_DRAWELEMENTS, (long) mode, (long) count, (long) type,
         (long) st.elem_buf, (long) (size_t) indices);

    if (mode != GL_TRIANGLES) tg.v_unknown_enum++;
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) tg.v_unknown_enum++;

    /* THE INWARD GUARD, measured. An array the module does not set that is
     * still enabled is dereferenced by this draw from a pointer belonging to
     * another pass, which is the damage the module's explicit sweep exists to
     * prevent. So is a coordinate array left enabled on any unit but 0. */
    if (st.n_on || st.i_on || st.e_on || st.s_on || st.f_on) tg.v_draw_foreign_array++;
    for (u = 1; u < SKBGL_UNITS; u++) if (st.t[u].on) tg.v_draw_foreign_array++;

    /* The indices argument is an offset, so a draw with no element buffer bound
     * would read it as a client address. */
    if (st.elem_buf == 0u) tg.v_draw_no_element_buffer++;
    else                   (void) usable(st.elem_buf);

    /* Every array this draw actually reads has to name a live buffer of this
     * generation. */
    if (st.v.on) (void) usable(st.v.buf);
    if (st.c.on) (void) usable(st.c.buf);
    for (u = 0; u < SKBGL_UNITS; u++) if (st.t[u].on) (void) usable(st.t[u].buf);
}

void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    guard();
    push(SKBGL_COLOR4UB, (long) r, (long) g, (long) b, (long) a, 0);
    st.col[0] = r; st.col[1] = g; st.col[2] = b; st.col[3] = a;
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    guard();
    push(SKBGL_TEXCOORD4F, (long) (s * 1000.0f), (long) (t * 1000.0f),
         (long) (r * 1000.0f), (long) (q * 1000.0f), 0);
    st.st[0] = s; st.st[1] = t; st.st[2] = r; st.st[3] = q;
}

/* ---- the loader --------------------------------------------------------- */

void *sl_gl_proc(const char *name)
{
    tg.proc_calls++;
    if (name == NULL) return NULL;

    if (strcmp(name, "glGenBuffers") == 0)
        return tg.have_buffers ? (void *) tg_GenBuffers : NULL;
    if (strcmp(name, "glDeleteBuffers") == 0)
        return tg.have_buffers ? (void *) tg_DeleteBuffers : NULL;
    if (strcmp(name, "glBindBuffer") == 0)
        return tg.have_buffers ? (void *) tg_BindBuffer : NULL;
    if (strcmp(name, "glBufferData") == 0)
        return tg.have_buffers ? (void *) tg_BufferData : NULL;
    if (strcmp(name, "glClientActiveTexture") == 0)
        return tg.have_client_unit ? (void *) tg_ClientActiveTexture : NULL;

    /* NOTHING ELSE RESOLVES, and three families are recorded on the way out.
     * A synchronisation point and a shader cannot be reached any other way on
     * this target - Win32's OpenGL import surface is frozen at 1.1 - so an
     * attempt to resolve one is the whole of the evidence needed. */
    if (strncmp(name, "glFinish", 8) == 0
        || strncmp(name, "glFlush", 7) == 0
        || strncmp(name, "glGetError", 10) == 0
        || strncmp(name, "glMapBuffer", 11) == 0
        || strncmp(name, "glUnmapBuffer", 13) == 0
        || strncmp(name, "glReadPixels", 12) == 0
        || strncmp(name, "glGetBufferSubData", 18) == 0
        || strncmp(name, "glFenceSync", 11) == 0
        || strncmp(name, "glClientWaitSync", 16) == 0) {
        tg.v_sync_proc++;
        return NULL;
    }
    if (strncmp(name, "glUniform", 9) == 0
        || strncmp(name, "glUseProgram", 12) == 0
        || strncmp(name, "glCreateProgram", 15) == 0
        || strncmp(name, "glCreateShader", 14) == 0
        || strncmp(name, "glAttachShader", 14) == 0
        || strncmp(name, "glVertexAttrib", 14) == 0) {
        tg.v_program_proc++;
        return NULL;
    }
    tg.v_unknown_proc++;
    return NULL;
}
