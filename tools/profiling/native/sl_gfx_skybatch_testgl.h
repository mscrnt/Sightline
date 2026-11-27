/**
 * SYNTHETIC GL for the skydome batch module's offline tests
 * (src/gfx/sl_gfx_skybatch.c compiled with -DSL_SKYBATCH_TEST).
 *
 * EVERY NUMBER THIS HEADER'S IMPLEMENTATION PRODUCES IS FABRICATED. The
 * texture-unit count, the buffer names, the inherited client-array state and
 * the pointers below are invented by the test and prove nothing about any real
 * driver, any real frame or any pixel. What they do prove is the module's own
 * logic: which path it chooses, what it uploads, how it describes a record to
 * the draw, what state it saves and puts back, and what it refuses to do.
 *
 * There is NO window, NO native graphics API and NO game here. The stub is
 * plain C with no link-time dependency on opengl32 or SDL: a test built with it
 * cannot touch a GPU even by accident. The sl_gfx_profile_testgl.h idiom.
 *
 * WHAT IS ABSENT IS PART OF THE FIXTURE. There is no glFinish, no glFlush, no
 * glGetError, no glMapBuffer / glUnmapBuffer and no glReadPixels in this stub,
 * and the resolver below hands back none of them. A module that called one
 * would fail to LINK, which is a stronger check than an assertion: the absence
 * of the symbol is the proof that the pass introduces no synchronisation point,
 * and it is checked by the build rather than by a counter. For the same reason
 * no glUniform / glUseProgram entry point exists; an attempt to RESOLVE one is
 * recorded as a violation (v_program_proc), because a resolve is the only way
 * a module can reach a post-1.1 name on this target.
 *
 * THE STUB IS A WATCHDOG. It records a violation if a draw is issued while an
 * array the module does not set is still enabled, or with a coordinate array
 * enabled on a unit other than 0, or with no element buffer bound; if a buffer
 * name is used or deleted after being deleted; if a name from a previous
 * context generation is used at all (such a call is also REFUSED, so a stale
 * name cannot quietly behave); if a synchronisation or program entry point is
 * asked for; and if any entry point is called after the test marks the context
 * destroyed. A test asserts those counters are zero.
 */
#ifndef SL_GFX_SKYBATCH_TESTGL_H
#define SL_GFX_SKYBATCH_TESTGL_H

#include <stddef.h>

/* ---- the GL types the module uses --------------------------------------- */

typedef unsigned int   GLenum;
typedef unsigned int   GLuint;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned char  GLboolean;
typedef float          GLfloat;
typedef void           GLvoid;

#define APIENTRY                       /* the stub is cdecl throughout */

/* ---- the tokens, read from the headers in this MSYS2 tree ---------------
 *
 * The GL 1.1 values are mingw32/include/GL/gl.h:81, :101-102, :111-117,
 * :573-597; the post-1.1 values are mingw32/include/GL/glext.h:125, :184-185,
 * :354, :477-480, :488, :491, :499, :501, :503 and :514 - the same lines the
 * module cites for its own fallback block. The module guards that block with
 * `#ifndef GL_ARRAY_BUFFER` and friends, so defining the same tokens here with
 * the same values leaves one definition of each in the translation unit. */

#define GL_FALSE                              0
#define GL_TRUE                               1
#define GL_TRIANGLES                          0x0004
#define GL_BYTE                               0x1400
#define GL_UNSIGNED_BYTE                      0x1401
#define GL_SHORT                              0x1402
#define GL_UNSIGNED_SHORT                     0x1403
#define GL_UNSIGNED_INT                       0x1405
#define GL_FLOAT                              0x1406

#define GL_VERTEX_ARRAY                       0x8074
#define GL_NORMAL_ARRAY                       0x8075
#define GL_COLOR_ARRAY                        0x8076
#define GL_INDEX_ARRAY                        0x8077
#define GL_TEXTURE_COORD_ARRAY                0x8078
#define GL_EDGE_FLAG_ARRAY                    0x8079
#define GL_VERTEX_ARRAY_SIZE                  0x807A
#define GL_VERTEX_ARRAY_TYPE                  0x807B
#define GL_VERTEX_ARRAY_STRIDE                0x807C
#define GL_COLOR_ARRAY_SIZE                   0x8081
#define GL_COLOR_ARRAY_TYPE                   0x8082
#define GL_COLOR_ARRAY_STRIDE                 0x8083
#define GL_TEXTURE_COORD_ARRAY_SIZE           0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE           0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE         0x808A
#define GL_VERTEX_ARRAY_POINTER               0x808E
#define GL_COLOR_ARRAY_POINTER                0x8090
#define GL_TEXTURE_COORD_ARRAY_POINTER        0x8092

#define GL_FOG_COORD_ARRAY                    0x8457
#define GL_SECONDARY_COLOR_ARRAY              0x845E
#define GL_TEXTURE0                           0x84C0
#define GL_CLIENT_ACTIVE_TEXTURE              0x84E1
#define GL_MAX_TEXTURE_UNITS                  0x84E2
#define GL_ARRAY_BUFFER                       0x8892
#define GL_ELEMENT_ARRAY_BUFFER               0x8893
#define GL_ARRAY_BUFFER_BINDING               0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING       0x8895
#define GL_VERTEX_ARRAY_BUFFER_BINDING        0x8896
#define GL_COLOR_ARRAY_BUFFER_BINDING         0x8898
#define GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING 0x889A
#define GL_STREAM_DRAW                        0x88E0
#define GL_STATIC_DRAW                        0x88E4

/* ---- the GL 1.1 entry points the module calls directly ------------------
 *
 * Exactly the set src/gfx/sl_gfx_skybatch.c names outside its resolver: the
 * client-array commands, the two state readers, the enable pair, the draw and
 * the two current-value commands. Nothing else is declared, so nothing else
 * can be called. */

void      glGetIntegerv(GLenum pname, GLint *params);
void      glGetPointerv(GLenum pname, GLvoid **params);
GLboolean glIsEnabled(GLenum cap);
void      glEnableClientState(GLenum cap);
void      glDisableClientState(GLenum cap);
void      glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void      glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void      glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void      glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void      glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void      glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);

/* The loader the module resolves its post-1.1 names through. In the shipped
 * build this is the backend's; here it is the stub's, and it hands back only
 * the five names the module needs and only while the control struct says the
 * fake context has them. */
void *sl_gl_proc(const char *name);

/* ---- the recorded call log ----------------------------------------------
 *
 * One record per GL entry point entered, in order, so a test can assert what
 * was submitted and in which order rather than inferring it from a return
 * value. The log stops recording when it is full; `tg.calls` keeps counting,
 * and `tg.v_log_full` says the log is no longer a complete transcript. */

enum {
    SKBGL_GETINTEGERV = 1,
    SKBGL_GETPOINTERV,
    SKBGL_ISENABLED,
    SKBGL_ENABLECLIENT,
    SKBGL_DISABLECLIENT,
    SKBGL_VERTEXPOINTER,
    SKBGL_COLORPOINTER,
    SKBGL_TEXCOORDPOINTER,
    SKBGL_DRAWELEMENTS,
    SKBGL_COLOR4UB,
    SKBGL_TEXCOORD4F,
    SKBGL_GENBUFFERS,
    SKBGL_DELETEBUFFERS,
    SKBGL_BINDBUFFER,
    SKBGL_BUFFERDATA,
    SKBGL_CLIENTACTIVETEXTURE
};

/* The four fields carry what the kind needs, and nothing is kind-independent:
 *
 *   *POINTER     a = size, b = type, c = stride, d = the client unit in force
 *   BUFFERDATA   a = target, b = byte size, c = usage, d = 1 when data != NULL
 *   BINDBUFFER   a = target, b = name
 *   DRAWELEMENTS a = mode, b = count, c = index type, d = the element binding
 *   ENABLE/DISABLE/ISENABLED/GETINTEGERV/GETPOINTERV   a = the token
 *   COLOR4UB     a..d = the four components
 *   TEXCOORD4F   a..d = the four values, scaled by 1000 and truncated
 *   GENBUFFERS / DELETEBUFFERS   a = count, b = the first name
 *   CLIENTACTIVETEXTURE          a = the unit enum
 *
 * `off` is the pointer argument as the integer offset it is: a buffer-object
 * offset is what the module passes, so an integer is the honest record. */
struct sl_skbgl_call {
    int  kind;
    long a, b, c, d;
    long off;
};

#define SKBGL_LOG     4096
#define SKBGL_UNITS   16       /* the module's own SKB_UNITS ceiling */
#define SKBGL_BUFFERS 64

extern struct sl_skbgl_call sl_skbgl_log[SKBGL_LOG];

/* ---- the whole of the client-array state the stub models ----------------
 *
 * One array's five pieces are only meaningful together: an enable, the three
 * pointer-command parameters and the buffer that was bound into the array when
 * the pointer command ran (which is what a real implementation records, and
 * what makes `ptr` an offset rather than an address). */
struct sl_skbgl_arr {
    int         on;
    int         size, type, stride;
    long        ptr;               /* the offset the pointer command carried */
    unsigned    buf;               /* the per-array binding */
};

struct sl_skbgl_state {
    int                 unit;      /* GL_CLIENT_ACTIVE_TEXTURE               */
    unsigned            arr_buf;   /* GL_ARRAY_BUFFER_BINDING                */
    unsigned            elem_buf;  /* GL_ELEMENT_ARRAY_BUFFER_BINDING        */
    struct sl_skbgl_arr v, c;
    struct sl_skbgl_arr t[SKBGL_UNITS];
    int                 n_on, i_on, e_on, s_on, f_on;
    unsigned char       col[4];    /* the current colour                     */
    float               st[4];     /* the current texture coordinate         */
};

/* ---- the test's control surface (all SYNTHETIC) ------------------------- */

struct sl_skbgl {
    /* what the fake context is */
    int  max_texture_units;        /* the GL_MAX_TEXTURE_UNITS answer        */
    int  have_buffers;             /* the four GL 1.5 buffer names resolve   */
    int  have_client_unit;         /* glClientActiveTexture resolves         */
    int  gen_zero;                 /* glGenBuffers hands back a zero name    */
    int  context_destroyed;        /* set by the test AFTER teardown         */

    /* The context GENERATION. sl_skbgl_new_context() advances it, which makes
     * every name handed out before that point STALE: using or deleting one is
     * a violation and the call is refused. */
    long epoch;

    /* what the stub observed */
    long calls;                    /* GL entry points entered                */
    long logged;                   /* records in sl_skbgl_log                */
    long proc_calls;
    long gen_calls, delete_calls, generated, deleted;
    long draw_calls;

    /* the last ARRAY_BUFFER upload, so a test can read the bytes the module
     * actually staged rather than only their count. Truncated at 256. */
    long          last_upload_bytes;
    unsigned char last_upload[256];

    /* the watchdog */
    long v_draw_foreign_array;     /* a draw with an unset array enabled     */
    long v_draw_no_element_buffer;
    long v_use_after_delete;
    long v_stale_name;             /* a name from an earlier generation, or
                                    * one this context never handed out      */
    long v_call_after_context_destroyed;
    long v_program_proc;           /* a shader / uniform name was asked for  */
    long v_sync_proc;              /* a synchronisation name was asked for   */
    long v_unknown_proc;
    long v_unknown_enum;           /* a token the stub does not model        */
    long v_log_full;
};

extern struct sl_skbgl tg;

/* A clean fake context: GL's own client-array defaults, no name handed out,
 * nothing resolved, every counter zero, generation 1. */
void sl_skbgl_reset(void);

/* Zero the counters, the log and the recorded upload, leaving the modelled
 * state and the buffer names alone - a measurement window. */
void sl_skbgl_log_reset(void);

/* THE CONTEXT WAS RECREATED. Advances the generation, so every name handed out
 * before now is stale. The modelled client-array state is reset to GL's
 * defaults, because a new context has new state. */
void sl_skbgl_new_context(void);

/* The modelled state as it stands. */
void sl_skbgl_snapshot(struct sl_skbgl_state *out);

/* The FIRST field in which two snapshots differ, or NULL when they agree.
 * `include_current` 0 leaves the current colour and texture coordinate out of
 * the comparison: a pass that drew is REQUIRED to redefine them (the header's
 * glDrawElements rule), so they are not part of the bracket. */
const char *sl_skbgl_diff(const struct sl_skbgl_state *a,
                          const struct sl_skbgl_state *b,
                          int include_current);

/* A buffer name for the TEST to own, so "deletes only names it owns" has
 * something to be false about. Not a GL call and not counted. */
GLuint sl_skbgl_make_buffer(void);
int    sl_skbgl_buffer_alive(GLuint name);
long   sl_skbgl_buffer_size(GLuint name);

/* The test driving the fake context through the same internals the resolved
 * entry points use, so the state it installs is state the module could have
 * inherited. Counted like any call; a test resets the log afterwards. */
void sl_skbgl_test_bind(GLenum target, GLuint name);
void sl_skbgl_test_unit(GLenum unit);

#endif /* SL_GFX_SKYBATCH_TESTGL_H */
