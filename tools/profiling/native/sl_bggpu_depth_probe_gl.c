/* THE DRIVER SIDE of the background pass's depth probe. Compiled only into
 * bggpudepthprobe-gl.exe (SL_BGGPU_PROBE_GL), and reached only when that binary
 * is passed --gl. Nothing here runs during an ordinary build.
 *
 * WHAT IT DOES. Creates its own hidden-window OpenGL context, compiles the
 * PINNED PRODUCTION shader text (sl_bgdp_glsl(), extracted from
 * src/gfx/sl_gfx_bggpu.c and hashed - see sl_bggpu_depth_probe.h), links it
 * with gl_Position captured through transform feedback, uploads each synthetic
 * case's eight uniforms under the module's own names, draws one point per case
 * with the rasterizer discarded, and reads the captured clip position back. The
 * depth component goes to the caller, which compares it with the CPU reference
 * bit for bit.
 *
 * WHY TRANSFORM FEEDBACK AND NOT A DEPTH READ-BACK. The question is what the
 * vertex program computes for gl_Position.z, exactly. A depth-buffer read gives
 * the value after clipping, the perspective divide and the depth-range
 * transform, none of which is the expression under test, and all of which lose
 * bits. Transform feedback hands back the vertex stage's own output in float32,
 * which is the only form in which an exact comparison means anything. The
 * earlier in-game probes (the r51-r59 package set) established this idiom here
 * and measured it bit-exact against the CPU on a real driver; the difference is
 * that they captured user-declared `out` varyings of a REWRITTEN program, and
 * this probe captures the built-in gl_Position of the PRODUCTION program,
 * because a rewritten shader cannot answer a question about the shipped one.
 *
 * THE ONE INTERFACE THIS DEPENDS ON. BGG_VS writes gl_Position and declares no
 * user outputs, so the capture names the built-in: glTransformFeedbackVaryings
 * with "gl_Position". Built-in varying capture by name is specified behaviour
 * (it entered with EXT_transform_feedback and is retained in core and
 * compatibility), but it is the one part of this setup that has not been
 * exercised on a driver here. If a driver refuses it the link fails, and the
 * failure message below names it explicitly so the result is diagnosable rather
 * than a mystery.
 *
 * THE PROGRAM IS NOT THE MODULE'S PROGRAM OBJECT, and cannot be: transform
 * feedback varyings are specified before link, and the module links its own.
 * What is identical is the vertex shader TEXT, which is what the arithmetic
 * under test lives in; the varying specification adds a capture and changes no
 * expression.
 *
 * NO GAME, NO RENDERER, NO ASSET AND NO ROM. The window is never shown, the
 * context is deleted before return, nothing is written to disk, and every input
 * is the caller's synthetic case table.
 */
#include <windows.h>
#include <GL/gl.h>

#include "sl_bggpu_depth_probe.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int sl_bgdp_gl_built(void) { return 1; }

/* ---- the tokens this probe needs that GL 1.1 headers predate ------------ */

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER                          0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS                         0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS                            0x8B82
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                           0x8892
#endif
#ifndef GL_DYNAMIC_READ
#define GL_DYNAMIC_READ                           0x88E9
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER
#define GL_TRANSFORM_FEEDBACK_BUFFER              0x8C8E
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS                    0x8C8C
#endif
#ifndef GL_RASTERIZER_DISCARD
#define GL_RASTERIZER_DISCARD                     0x8C89
#endif
#ifndef GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN  0x8C88
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT                           0x8866
#endif

/* ---- the entry points, __stdcall as the platform defines them -----------
 * APIENTRY is load-bearing: every GL entry point is __stdcall on Windows, and a
 * typedef without it corrupts the caller's frame on return. The module's own
 * typedefs carry it for the same reason, and the offline fixture cannot check
 * this because its stub is cdecl - so this is one of the things only a driver
 * run exercises. */
typedef GLuint (APIENTRY *PF_createshader)(GLenum);
typedef void   (APIENTRY *PF_shadersource)(GLuint, GLsizei, const char *const *, const GLint *);
typedef void   (APIENTRY *PF_u)(GLuint);
typedef void   (APIENTRY *PF_getshaderiv)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *PF_getlog)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY *PF_createprogram)(void);
typedef void   (APIENTRY *PF_attach)(GLuint, GLuint);
typedef void   (APIENTRY *PF_tfv)(GLuint, GLsizei, const char *const *, GLenum);
typedef GLint  (APIENTRY *PF_getuniformlocation)(GLuint, const char *);
typedef void   (APIENTRY *PF_uniform1f)(GLint, GLfloat);
typedef void   (APIENTRY *PF_uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *PF_uniform1i)(GLint, GLint);
typedef void   (APIENTRY *PF_uniform1fv)(GLint, GLsizei, const GLfloat *);
typedef void   (APIENTRY *PF_gen)(GLsizei, GLuint *);
typedef void   (APIENTRY *PF_bindbuffer)(GLenum, GLuint);
typedef void   (APIENTRY *PF_bufferdata)(GLenum, ptrdiff_t, const void *, GLenum);
typedef void   (APIENTRY *PF_bindbufferbase)(GLenum, GLuint, GLuint);
typedef void   (APIENTRY *PF_getbuffersubdata)(GLenum, ptrdiff_t, ptrdiff_t, void *);
typedef void   (APIENTRY *PF_begintf)(GLenum);
typedef void   (APIENTRY *PF_void)(void);
typedef void   (APIENTRY *PF_beginquery)(GLenum, GLuint);
typedef void   (APIENTRY *PF_endquery)(GLenum);
typedef void   (APIENTRY *PF_getqueryobjectuiv)(GLuint, GLenum, GLuint *);

static PF_createshader       p_CreateShader;
static PF_shadersource       p_ShaderSource;
static PF_u                  p_CompileShader, p_LinkProgram, p_UseProgram;
static PF_u                  p_DeleteShader, p_DeleteProgram;
static PF_getshaderiv        p_GetShaderiv, p_GetProgramiv;
static PF_getlog             p_GetShaderInfoLog, p_GetProgramInfoLog;
static PF_createprogram      p_CreateProgram;
static PF_attach             p_AttachShader;
static PF_tfv                p_TransformFeedbackVaryings;
static PF_getuniformlocation p_GetUniformLocation;
static PF_uniform1f          p_Uniform1f;
static PF_uniform3f          p_Uniform3f;
static PF_uniform1i          p_Uniform1i;
static PF_uniform1fv         p_Uniform1fv;
static PF_gen                p_GenBuffers, p_DeleteBuffers, p_GenQueries, p_DeleteQueries;
static PF_bindbuffer         p_BindBuffer;
static PF_bufferdata         p_BufferData;
static PF_bindbufferbase     p_BindBufferBase;
static PF_getbuffersubdata   p_GetBufferSubData;
static PF_begintf            p_BeginTransformFeedback;
static PF_void               p_EndTransformFeedback;
static PF_beginquery         p_BeginQuery;
static PF_endquery           p_EndQuery;
static PF_getqueryobjectuiv  p_GetQueryObjectuiv;

/* ---- the refusal path --------------------------------------------------- */

static char *g_why;
static int   g_whycap;

static int refuse(const char *fmt, ...)
{
    va_list ap;
    if (g_why != NULL && g_whycap > 0) {
        va_start(ap, fmt);
        vsnprintf(g_why, (size_t) g_whycap - 1u, fmt, ap);
        va_end(ap);
        g_why[g_whycap - 1] = '\0';
    }
    return 1;
}

static void *resolve(const char *name, int *missing)
{
    void *fn = (void *) wglGetProcAddress(name);
    if (fn == NULL) {
        (*missing)++;
        if (*missing == 1) refuse("the driver does not export %s", name);
    }
    return fn;
}

static int resolve_all(void)
{
    int missing = 0;
    p_CreateShader  = (PF_createshader) resolve("glCreateShader", &missing);
    p_ShaderSource  = (PF_shadersource) resolve("glShaderSource", &missing);
    p_CompileShader = (PF_u) resolve("glCompileShader", &missing);
    p_GetShaderiv   = (PF_getshaderiv) resolve("glGetShaderiv", &missing);
    p_GetShaderInfoLog = (PF_getlog) resolve("glGetShaderInfoLog", &missing);
    p_CreateProgram = (PF_createprogram) resolve("glCreateProgram", &missing);
    p_AttachShader  = (PF_attach) resolve("glAttachShader", &missing);
    p_TransformFeedbackVaryings = (PF_tfv) resolve("glTransformFeedbackVaryings", &missing);
    p_LinkProgram   = (PF_u) resolve("glLinkProgram", &missing);
    p_GetProgramiv  = (PF_getshaderiv) resolve("glGetProgramiv", &missing);
    p_GetProgramInfoLog = (PF_getlog) resolve("glGetProgramInfoLog", &missing);
    p_UseProgram    = (PF_u) resolve("glUseProgram", &missing);
    p_DeleteShader  = (PF_u) resolve("glDeleteShader", &missing);
    p_DeleteProgram = (PF_u) resolve("glDeleteProgram", &missing);
    p_GetUniformLocation = (PF_getuniformlocation) resolve("glGetUniformLocation", &missing);
    p_Uniform1f   = (PF_uniform1f) resolve("glUniform1f", &missing);
    p_Uniform3f   = (PF_uniform3f) resolve("glUniform3f", &missing);
    p_Uniform1i   = (PF_uniform1i) resolve("glUniform1i", &missing);
    p_Uniform1fv  = (PF_uniform1fv) resolve("glUniform1fv", &missing);
    p_GenBuffers  = (PF_gen) resolve("glGenBuffers", &missing);
    p_DeleteBuffers = (PF_gen) resolve("glDeleteBuffers", &missing);
    p_GenQueries  = (PF_gen) resolve("glGenQueries", &missing);
    p_DeleteQueries = (PF_gen) resolve("glDeleteQueries", &missing);
    p_BindBuffer  = (PF_bindbuffer) resolve("glBindBuffer", &missing);
    p_BufferData  = (PF_bufferdata) resolve("glBufferData", &missing);
    p_BindBufferBase = (PF_bindbufferbase) resolve("glBindBufferBase", &missing);
    p_GetBufferSubData = (PF_getbuffersubdata) resolve("glGetBufferSubData", &missing);
    p_BeginTransformFeedback = (PF_begintf) resolve("glBeginTransformFeedback", &missing);
    p_EndTransformFeedback = (PF_void) resolve("glEndTransformFeedback", &missing);
    p_BeginQuery  = (PF_beginquery) resolve("glBeginQuery", &missing);
    p_EndQuery    = (PF_endquery) resolve("glEndQuery", &missing);
    p_GetQueryObjectuiv = (PF_getqueryobjectuiv) resolve("glGetQueryObjectuiv", &missing);
    return missing;
}

/* ---- the hidden window and its context ---------------------------------- */

struct ctx {
    HWND  wnd;
    HDC   dc;
    HGLRC rc;
    const char *cls;
};

static void ctx_down(struct ctx *c)
{
    if (c->rc != NULL) { wglMakeCurrent(NULL, NULL); wglDeleteContext(c->rc); c->rc = NULL; }
    if (c->dc != NULL) { ReleaseDC(c->wnd, c->dc); c->dc = NULL; }
    if (c->wnd != NULL) { DestroyWindow(c->wnd); c->wnd = NULL; }
    if (c->cls != NULL) { UnregisterClassA(c->cls, GetModuleHandleA(NULL)); c->cls = NULL; }
}

/* A window that is created and never shown: no WS_VISIBLE, no ShowWindow, and
 * the probe does not pump messages, so nothing of it reaches the screen. A
 * window is needed at all only because wglCreateContext wants a device context
 * with a pixel format, and the legacy entry point is what yields a
 * COMPATIBILITY profile - which `#version 400 compatibility` requires. */
static int ctx_up(struct ctx *c)
{
    static const char *const CLS = "sl_bgdp_probe_window";
    PIXELFORMATDESCRIPTOR pfd;
    WNDCLASSA wc;
    int pf;

    memset(c, 0, sizeof *c);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = CLS;
    if (RegisterClassA(&wc) == 0)
        return refuse("RegisterClassA failed (%lu)", (unsigned long) GetLastError());
    c->cls = CLS;
    c->wnd = CreateWindowExA(0, CLS, "sl_bgdp", WS_OVERLAPPED, 0, 0, 16, 16,
                             NULL, NULL, wc.hInstance, NULL);
    if (c->wnd == NULL)
        return refuse("CreateWindowExA failed (%lu)", (unsigned long) GetLastError());
    c->dc = GetDC(c->wnd);
    if (c->dc == NULL) return refuse("GetDC failed");

    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    pf = ChoosePixelFormat(c->dc, &pfd);
    if (pf == 0) return refuse("ChoosePixelFormat found no format");
    if (!SetPixelFormat(c->dc, pf, &pfd)) return refuse("SetPixelFormat failed");
    c->rc = wglCreateContext(c->dc);
    if (c->rc == NULL)
        return refuse("wglCreateContext failed (%lu)", (unsigned long) GetLastError());
    if (!wglMakeCurrent(c->dc, c->rc)) return refuse("wglMakeCurrent failed");
    return 0;
}

/* GL 4.0 or later, because the pinned text asks for `#version 400
 * compatibility` and `precise`. Refusing here names the version rather than
 * letting the shader compile fail with a driver-specific message. */
static int version_ok(void)
{
    const char *v = (const char *) glGetString(GL_VERSION);
    int major = 0, minor = 0;
    if (v == NULL) return refuse("glGetString(GL_VERSION) returned NULL");
    if (sscanf(v, "%d.%d", &major, &minor) != 2)
        return refuse("could not read a version out of '%s'", v);
    if (major < 4)
        return refuse("this context reports GL %d.%d; the pinned text needs "
                      "GL 4.0 or later for `#version 400 compatibility` and "
                      "`precise`", major, minor);
    return 0;
}

/* ---- the program, from the pinned text ---------------------------------- */

/* The varying list is a PARAMETER so the depth run and the vertex-output run
 * share one program build. The depth run passes the single built-in it has
 * always passed, so nothing about it changes; the output run passes the six
 * BGG_VS writes, in the order its record layout declares. */
static int build_program_v(const char *const *vary, int nvary, GLuint *out_prog)
{
    const char *text = sl_bgdp_glsl();
    GLuint vs, prog;
    GLint ok = 0;
    char log[1024];
    GLsizei got = 0;

    vs = p_CreateShader(GL_VERTEX_SHADER);
    if (vs == 0) return refuse("glCreateShader gave 0");
    p_ShaderSource(vs, 1, &text, NULL);
    p_CompileShader(vs);
    p_GetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        log[0] = '\0';
        p_GetShaderInfoLog(vs, (GLsizei) sizeof log - 1, &got, log);
        log[sizeof log - 1] = '\0';
        return refuse("THE PRODUCTION SHADER TEXT DID NOT COMPILE on this "
                      "driver: %s", log);
    }
    prog = p_CreateProgram();
    if (prog == 0) return refuse("glCreateProgram gave 0");
    p_AttachShader(prog, vs);
    /* Before the link, and the one interface this probe leans on: the capture
     * names the BUILT-IN gl_Position, because the production text declares no
     * user output. */
    p_TransformFeedbackVaryings(prog, (GLsizei) nvary, vary, GL_INTERLEAVED_ATTRIBS);
    p_LinkProgram(prog);
    ok = 0;
    p_GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        log[0] = '\0';
        p_GetProgramInfoLog(prog, (GLsizei) sizeof log - 1, &got, log);
        log[sizeof log - 1] = '\0';
        return refuse("the program did not link with %d built-in varying(s) "
                      "captured through transform feedback, the first being "
                      "%s: %s. If the driver refuses built-in varying capture "
                      "the probe needs a different read-back and the outputs "
                      "are still UNMEASURED - it does NOT mean the shader is "
                      "wrong.", nvary, vary[0], log);
    }
    p_DeleteShader(vs);     /* the program holds its own reference */
    *out_prog = prog;
    return 0;
}

static int build_program(GLuint *out_prog)
{
    static const char *const vary[1] = { "gl_Position" };
    return build_program_v(vary, 1, out_prog);
}

/* ---- the run ------------------------------------------------------------ */

int sl_bgdp_gl_run(const struct sl_bgdp_case *cases, int n, float *out_z,
                   char *why, int whycap)
{
    struct ctx c;
    GLuint prog = 0, buf = 0, query = 0;
    GLint loc[8];
    float pos[3], captured[4];
    int i, rc = 0;

    g_why = why; g_whycap = whycap;
    if (why != NULL && whycap > 0) why[0] = '\0';
    if (cases == NULL || out_z == NULL || n <= 0) return refuse("nothing to run");

    if (ctx_up(&c) != 0) { ctx_down(&c); return 1; }
    if (version_ok() != 0 || resolve_all() != 0) { ctx_down(&c); return 1; }
    if (build_program(&prog) != 0) { ctx_down(&c); return 1; }

    /* Every uniform resolved by the module's OWN names, read out of the same
     * source as the text (sl_bgdp_uniform_names). A location of -1 here means
     * the link dropped a uniform the module depends on. */
    for (i = 0; i < 8; i++) {
        loc[i] = p_GetUniformLocation(prog, sl_bgdp_uniform_names()[i]);
        if (loc[i] < 0) {
            rc = refuse("the linked program has no location for %s",
                        sl_bgdp_uniform_names()[i]);
            goto done;
        }
    }

    p_GenBuffers(1, &buf);
    p_GenQueries(1, &query);
    if (buf == 0 || query == 0) { rc = refuse("glGenBuffers/glGenQueries gave 0"); goto done; }
    p_BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buf);
    p_BufferData(GL_TRANSFORM_FEEDBACK_BUFFER, (ptrdiff_t) sizeof captured,
                 NULL, GL_DYNAMIC_READ);
    p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buf);

    /* The input comes from a client-side array, so nothing may be bound to
     * GL_ARRAY_BUFFER; the shader reads gl_Vertex, which is this array. */
    p_BindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glEnable(GL_RASTERIZER_DISCARD);     /* no framebuffer, no fragment stage */
    p_UseProgram(prog);

    /* ONE CAPTURE PER CASE, read back before the next begins. Serialising this
     * way costs n round trips for n draws in the tens, and in exchange every
     * case's uniforms are uploaded with no feedback session open and each
     * capture's primitive count is checked on its own. */
    for (i = 0; i < n; i++) {
        const struct sl_bgdp_case *k = &cases[i];
        GLuint written = 0;

        out_z[i] = 0.0f;
        pos[0] = k->px; pos[1] = k->py; pos[2] = k->pz;

        p_Uniform1f(loc[0], (GLfloat) k->sc);                       /* u_sc     */
        p_Uniform1f(loc[1], (GLfloat) k->post);                     /* u_post   */
        p_Uniform3f(loc[2], (GLfloat) k->org[0], (GLfloat) k->org[1],
                    (GLfloat) k->org[2]);                           /* u_org    */
        p_Uniform1fv(loc[3], 16, (const GLfloat *) k->C);           /* u_C      */
        p_Uniform1i(loc[4], k->fogless ? 1 : 0);                    /* u_fogless*/
        p_Uniform1i(loc[5], (GLint) k->dmode);                      /* u_dmode  */
        p_Uniform1f(loc[6], (GLfloat) k->fk1);                      /* u_fk1    */
        p_Uniform1f(loc[7], (GLfloat) k->fk2);                      /* u_fk2    */

        captured[0] = captured[1] = captured[2] = captured[3] = 0.0f;
        p_BufferData(GL_TRANSFORM_FEEDBACK_BUFFER, (ptrdiff_t) sizeof captured,
                     captured, GL_DYNAMIC_READ);

        p_BeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
        p_BeginTransformFeedback(GL_POINTS);
        glDrawArrays(GL_POINTS, 0, 1);
        p_EndTransformFeedback();
        p_EndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);

        p_GetQueryObjectuiv(query, GL_QUERY_RESULT, &written);
        if (written != 1u) {
            rc = refuse("case %d (%s) captured %lu primitives, not 1",
                        i, k->name, (unsigned long) written);
            goto done;
        }
        p_GetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                           (ptrdiff_t) sizeof captured, captured);
        if (glGetError() != GL_NO_ERROR) {
            rc = refuse("case %d (%s) left a GL error behind", i, k->name);
            goto done;
        }
        out_z[i] = captured[2];          /* gl_Position.z, the depth under test */
    }

done:
    p_UseProgram(0);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (query != 0) p_DeleteQueries(1, &query);
    if (buf != 0) {
        p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
        p_BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
        p_DeleteBuffers(1, &buf);
    }
    if (prog != 0) p_DeleteProgram(prog);
    ctx_down(&c);
    return rc;
}

/* ======================================================================== *
 * THE WHOLE VERTEX OUTPUT, on the same context and the same loader.
 *
 * Read sl_bggpu_out_probe.h for the contract. This adds one run function and
 * three entry points; the depth run above is untouched, and the context, the
 * version gate, the loader and the program build are the ones it already uses.
 *
 * COMPILED ONLY FOR THE OUTPUT BUILD, and that is load-bearing rather than
 * tidy. The code below calls sl_bgop_fields(), which lives in
 * sl_bggpu_out_probe.c, and the ORDINARY DEPTH GL LINK does not include that
 * object (tools/windows/gpudepthprobe.ps1, the depth target's link line). Left
 * unconditional, this section put an unresolved external into the depth
 * binary's link: nothing removes an unreferenced-but-called symbol, so the
 * default GL rebuild failed outright. It was invisible here because the depth
 * binary was deliberately not rebuilt - its saved copy was being preserved -
 * and the CPU checks cannot see a link that was never run.
 *
 * SL_BGGPU_PROBE_OUTPUTS is therefore defined ONLY on the Outputs GL compile.
 * The depth path keeps its own source behaviour and its own independent link.
 * ======================================================================== */

#ifdef SL_BGGPU_PROBE_OUTPUTS

#include "sl_bggpu_out_probe.h"
#include "sl_bgop_sink.h"

/* The snapshot run sizes its client arrays and its capture buffer once, from
 * the snapshot's largest draw, so it allocates. The synthetic run above has
 * fixed-size locals and does not, which is why this include is here and not at
 * the top of the file. */
#include <stdlib.h>

#ifndef GL_FOG_COORD_ARRAY
#define GL_FOG_COORD_ARRAY  0x8457
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0         0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1         0x84C1
#endif

typedef void (APIENTRY *PF_activetexture)(GLenum);
typedef void (APIENTRY *PF_fogcoordpointer)(GLenum, GLsizei, const void *);
typedef void (APIENTRY *PF_fogcoordf)(GLfloat);

static PF_activetexture   p_ActiveTexture, p_ClientActiveTexture;
static PF_fogcoordpointer p_FogCoordPointer;
static PF_fogcoordf       p_FogCoordf;

/* The three the forwarded attributes need on top of the depth run's set:
 * glActiveTexture (GL 1.3) selects which unit's GL_TEXTURE matrix stack
 * glMatrixMode reaches, glClientActiveTexture (GL 1.3) selects which unit
 * glTexCoordPointer reaches, and glFogCoordPointer is GL 1.4. The rest -
 * glMatrixMode, glLoadMatrixf, glColorPointer, glTexCoordPointer - are GL 1.1
 * and come straight from opengl32. */
static int resolve_attr(void)
{
    int missing = 0;
    p_ActiveTexture = (PF_activetexture) resolve("glActiveTexture", &missing);
    p_ClientActiveTexture = (PF_activetexture) resolve("glClientActiveTexture", &missing);
    p_FogCoordPointer = (PF_fogcoordpointer) resolve("glFogCoordPointer", &missing);
    /* glFogCoordf is GL 1.4, like glFogCoordPointer, and is needed for the
     * draws whose fog-array state records the array disabled with the current
     * coordinate set to a known zero. Without it those draws cannot be replayed
     * faithfully at all, so it is a required entry point rather than an
     * optional one. */
    p_FogCoordf = (PF_fogcoordf) resolve("glFogCoordf", &missing);
    return missing;
}

int sl_bgop_gl_built(void) { return 1; }

/* A bit pattern the shader cannot produce from these inputs, used to prove the
 * record layout rather than trust it. The capture writes only the floats it
 * captures, so every float past the declared end of the record must still hold
 * this when the read-back returns. If the layout in sl_bgop_fields() were
 * wrong - one component miscounted, gl_FogFragCoord treated as a vec4 - the
 * record would be longer than declared and this tail would have moved. That
 * turns the one thing no offline check can exercise into a diagnosable refusal
 * instead of a set of confident wrong numbers. */
static const unsigned SENTINEL_BITS = 0x7FA5A5A5u;

static float sentinel_float(void)
{
    float f;
    memcpy(&f, &SENTINEL_BITS, sizeof f);
    return f;
}

static unsigned drain_errors(void)
{
    unsigned seen = 0u;
    while (glGetError() != GL_NO_ERROR) {
        seen++;
        if (seen > 64u) break;        /* a driver stuck in error, not a count */
    }
    return seen;
}

int sl_bgop_gl_run(const struct sl_bgdp_case *cases, const struct sl_bgop_attr *attrs,
                   int n, float *out, unsigned *counts, char *why, int whycap)
{
    const struct sl_bgop_field *fields = sl_bgop_fields();
    const char *vary[SL_BGOP_NFIELDS];
    struct ctx c;
    GLuint prog = 0, buf = 0, query = 0;
    GLint loc[8];
    float pos[3], uv0[2], uv1[2], fog;
    unsigned char col[4];
    float buffer[SL_BGOP_BUFFER_FLOATS];
    unsigned errors = 0u, completed = 0u, written_total = 0u;
    int i, j, rc = 0;

    g_why = why; g_whycap = whycap;
    if (why != NULL && whycap > 0) why[0] = '\0';
    if (counts != NULL) counts[0] = counts[1] = counts[2] = 0u;
    if (cases == NULL || attrs == NULL || out == NULL || n <= 0)
        return refuse("nothing to run");

    /* ONE SOURCE OF ORDER: the capture names come from the same table the
     * comparison reads the offsets out of, so the two cannot drift apart. */
    for (i = 0; i < SL_BGOP_NFIELDS; i++) vary[i] = fields[i].varying;

    if (ctx_up(&c) != 0) { ctx_down(&c); return 1; }
    if (version_ok() != 0 || resolve_all() != 0 || resolve_attr() != 0) {
        ctx_down(&c); return 1;
    }
    if (build_program_v(vary, SL_BGOP_NFIELDS, &prog) != 0) { ctx_down(&c); return 1; }

    for (i = 0; i < 8; i++) {
        loc[i] = p_GetUniformLocation(prog, sl_bgdp_uniform_names()[i]);
        if (loc[i] < 0) {
            rc = refuse("the linked program has no location for %s",
                        sl_bgdp_uniform_names()[i]);
            goto done;
        }
    }

    p_GenBuffers(1, &buf);
    p_GenQueries(1, &query);
    if (buf == 0 || query == 0) {
        rc = refuse("glGenBuffers/glGenQueries gave 0");
        goto done;
    }
    p_BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buf);
    p_BufferData(GL_TRANSFORM_FEEDBACK_BUFFER, (ptrdiff_t) sizeof buffer,
                 NULL, GL_DYNAMIC_READ);
    p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buf);

    /* THE CALLER'S OWN ARRAY FORMATS (src/gfx/sl_gfx_dl.c:16845, 18710, 18685,
     * 18689). Client-side arrays, so nothing may be bound to GL_ARRAY_BUFFER. */
    p_BindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, pos);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    glEnableClientState(GL_FOG_COORD_ARRAY);
    p_FogCoordPointer(GL_FLOAT, 0, &fog);
    p_ClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, uv0);
    p_ClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, uv1);
    p_ClientActiveTexture(GL_TEXTURE0);

    glEnable(GL_RASTERIZER_DISCARD);     /* no framebuffer, no fragment stage */
    p_UseProgram(prog);
    errors += drain_errors();            /* the setup's own, before any case */

    for (i = 0; i < n; i++) {
        const struct sl_bgdp_case *k = &cases[i];
        const struct sl_bgop_attr *a = &attrs[i];
        float *rec = out + (size_t) i * SL_BGOP_RECORD_FLOATS;
        GLuint produced = 0u;

        for (j = 0; j < SL_BGOP_RECORD_FLOATS; j++) rec[j] = 0.0f;

        pos[0] = k->px; pos[1] = k->py; pos[2] = k->pz;
        for (j = 0; j < 4; j++) col[j] = a->col[j];
        fog = a->fog;
        uv0[0] = a->uv[0][0]; uv0[1] = a->uv[0][1];
        uv1[0] = a->uv[1][0]; uv1[1] = a->uv[1][1];

        /* The two texture matrices onto the GL_TEXTURE stack of their own
         * units. glLoadMatrixf stores the sixteen floats as given, in column
         * order, so gl_TextureMatrix[i] is exactly this matrix. */
        p_ActiveTexture(GL_TEXTURE0);
        glMatrixMode(GL_TEXTURE);
        glLoadMatrixf(a->M[0]);
        p_ActiveTexture(GL_TEXTURE1);
        glMatrixMode(GL_TEXTURE);
        glLoadMatrixf(a->M[1]);
        p_ActiveTexture(GL_TEXTURE0);

        p_Uniform1f(loc[0], (GLfloat) k->sc);
        p_Uniform1f(loc[1], (GLfloat) k->post);
        p_Uniform3f(loc[2], (GLfloat) k->org[0], (GLfloat) k->org[1],
                    (GLfloat) k->org[2]);
        p_Uniform1fv(loc[3], 16, (const GLfloat *) k->C);
        p_Uniform1i(loc[4], k->fogless ? 1 : 0);
        p_Uniform1i(loc[5], (GLint) k->dmode);
        p_Uniform1f(loc[6], (GLfloat) k->fk1);
        p_Uniform1f(loc[7], (GLfloat) k->fk2);

        for (j = 0; j < SL_BGOP_BUFFER_FLOATS; j++) buffer[j] = sentinel_float();
        p_BufferData(GL_TRANSFORM_FEEDBACK_BUFFER, (ptrdiff_t) sizeof buffer,
                     buffer, GL_DYNAMIC_READ);

        p_BeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
        p_BeginTransformFeedback(GL_POINTS);
        glDrawArrays(GL_POINTS, 0, 1);
        p_EndTransformFeedback();
        p_EndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);

        p_GetQueryObjectuiv(query, GL_QUERY_RESULT, &produced);
        written_total += produced;
        if (produced != 1u) {
            rc = refuse("case %d (%s) captured %lu primitives, not 1 - the "
                        "outputs are UNMEASURED for it",
                        i, k->name, (unsigned long) produced);
            goto done;
        }

        for (j = 0; j < SL_BGOP_BUFFER_FLOATS; j++) buffer[j] = sentinel_float();
        p_GetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                           (ptrdiff_t) sizeof buffer, buffer);
        if (glGetError() != GL_NO_ERROR) {
            errors++;
            rc = refuse("case %d (%s) left a GL error behind; nothing is read "
                        "from a failed capture", i, k->name);
            goto done;
        }

        /* THE READ-BACK HAPPENED AT ALL. A buffer still holding the sentinel in
         * its first floats was never written, and a zeroed record is not the
         * same evidence as a measured one. */
        {
            unsigned b0;
            memcpy(&b0, &buffer[0], sizeof b0);
            if (b0 == SENTINEL_BITS) {
                rc = refuse("case %d (%s): the transform feedback buffer still "
                            "holds the sentinel at offset 0, so no read-back "
                            "happened and the outputs are UNMEASURED",
                            i, k->name);
                goto done;
            }
        }
        /* THE LAYOUT IS WHAT IT SAYS IT IS. */
        for (j = SL_BGOP_RECORD_FLOATS; j < SL_BGOP_BUFFER_FLOATS; j++) {
            unsigned bj;
            memcpy(&bj, &buffer[j], sizeof bj);
            if (bj != SENTINEL_BITS) {
                rc = refuse("case %d (%s): the capture wrote float %d, past the "
                            "declared %d-float record, so the record layout in "
                            "sl_bgop_fields() does not match what this driver "
                            "captured - every field after the mismatch would be "
                            "read from the wrong offset. UNMEASURED.",
                            i, k->name, j, SL_BGOP_RECORD_FLOATS);
                goto done;
            }
        }

        memcpy(rec, buffer, sizeof(float) * SL_BGOP_RECORD_FLOATS);
        completed++;
    }

done:
    errors += drain_errors();        /* whatever the cases left behind */
    p_UseProgram(0);
    glDisable(GL_RASTERIZER_DISCARD);
    p_ClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    p_ClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_FOG_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (query != 0) p_DeleteQueries(1, &query);
    if (buf != 0) {
        p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
        p_BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
        p_DeleteBuffers(1, &buf);
    }
    if (prog != 0) p_DeleteProgram(prog);
    /* AND WHATEVER CLEANUP ITSELF RAISED, drained while the context is STILL
     * CURRENT - after ctx_down there is no context to ask, so an error raised
     * by the teardown would otherwise be collected by nobody and the run would
     * report zero errors it had actually caused. */
    errors += drain_errors();
    ctx_down(&c);
    if (counts != NULL) {
        counts[0] = errors;
        counts[1] = completed;
        counts[2] = written_total;
    }
    /* A GL ERROR IS FATAL, HERE AS WELL AS IN THE CALLER. The loop above can
     * finish with every case captured and every sentinel intact while errors
     * were collected during setup, during a case or during cleanup - and it
     * used to return 0 for that, which let an errored driver's records reach
     * the comparison. The readiness predicate is the caller's own
     * (sl_bgop_counts_ready), so there is one rule and not two, and it is
     * applied on both sides of the interface: this refuses, and the caller
     * refuses again before it compares anything. */
    if (rc == 0) {
        char gate[256];
        gate[0] = '\0';
        if (!sl_bgop_counts_ready(counts, n, gate, (int) sizeof gate))
            rc = refuse("%s", gate);
    }
    return rc;
}

/* ======================================================================== *
 * AN ACTUAL FRAME'S DRAWS: ONE BULK CAPTURE PER DRAW.
 *
 * WHY BULK, AND WHY PER DRAW. The synthetic run above captures one vertex per
 * case and reads it back before the next begins, which is right for tens of
 * constructed cases and wrong by three orders of magnitude for a real frame: a
 * background pass carries tens of thousands of vertices, and one round trip per
 * vertex is a synchronous stall per vertex. A draw is the natural unit instead -
 * all of its vertices are transformed under ONE uniform set - so each draw's
 * whole index range goes through a single GL_POINTS draw with the rasterizer
 * discarded and comes back in ONE glGetBufferSubData. One round trip per draw.
 *
 * THE INDEX RANGE IS DEREFERENCED ON THE CPU. Each index position becomes one
 * client-array vertex, in order, so the capture holds exactly one record per
 * index position and a repeated index yields its record twice - which is what
 * an indexed draw's vertex stage does. The alternative, an element array buffer,
 * would add a second upload path and a second thing to get wrong for no change
 * in what is measured.
 *
 * EVERY REFUSAL THE SYNTHETIC RUN HAS, PER DRAW, AND THE SAME GATE. The
 * primitives-written query must equal the draw's vertex count; the read-back
 * must have happened at all (the buffer's first float must no longer hold the
 * sentinel); the floats past the declared end of the last record must still hold
 * it, which is what proves the record layout for a capture of many records
 * rather than one; GL errors are drained before the first draw, after each draw
 * and after cleanup while the context is still current, and ANY of them is
 * fatal. The decision is sl_bgop_counts_ready - the caller's own predicate,
 * applied with the draw's vertex count as n - so there is one rule and not two,
 * and a draw that fails it refuses the WHOLE run and compares NOTHING.
 *
 * REFUSED, NOT CLAMPED. A draw whose vertex count exceeds
 * SL_BGOP_SNAP_MAX_DRAW_VERTS is refused with its count named rather than
 * captured in pieces: a partial capture of a draw is not a measurement of that
 * draw. The buffers are sized once, before the first draw, from the largest
 * count in the snapshot, so the peak is a function of the validated counts.
 *
 * WHAT IT COMPARES IS NOT DECIDED HERE. The per-component classification and
 * the comparison are sl_bgop_snap_class and sl_bgop_snap_compare_draw, on the
 * CPU, where they can be and are driven offline. This file captures and gates;
 * it does not judge. Components with no oracle are counted as such by that
 * comparison and never as agreement.
 * ======================================================================== */

#define SNAP_TAIL_FLOATS (SL_BGOP_BUFFER_FLOATS - SL_BGOP_RECORD_FLOATS)

/* The client arrays and the capture buffer, carved out of one block sized from
 * the snapshot's own largest draw. */
struct snap_bufs {
    float         *pos;      /* 3 floats per vertex                          */
    float         *fog;      /* 1                                            */
    float         *uv0;      /* 2                                            */
    float         *uv1;      /* 2                                            */
    float         *cap;      /* SL_BGOP_RECORD_FLOATS per vertex, plus a tail */
    unsigned char *col;      /* 4 bytes per vertex                           */
    void          *block;
    unsigned       verts;    /* what the above are sized for                 */
};

static void snap_bufs_free(struct snap_bufs *b)
{
    if (b->block != NULL) free(b->block);
    memset(b, 0, sizeof *b);
}

/* Every array's byte length is a multiple of 4 and the byte array comes last,
 * so carving the block at the running total keeps each float pointer 4-byte
 * aligned behind malloc's own stronger guarantee. The sizes are bounded by
 * SL_BGOP_SNAP_MAX_DRAW_VERTS, which the caller has already enforced. */
static int snap_bufs_alloc(struct snap_bufs *b, unsigned verts)
{
    unsigned char *p;
    size_t fpv = 3u + 1u + 2u + 2u;             /* floats per vertex, inputs  */
    size_t need;

    memset(b, 0, sizeof *b);
    if (verts == 0u || verts > SL_BGOP_SNAP_MAX_DRAW_VERTS) return 1;
    need = (size_t) verts * fpv * sizeof(float)
         + ((size_t) verts * SL_BGOP_RECORD_FLOATS + SNAP_TAIL_FLOATS) * sizeof(float)
         + (size_t) verts * 4u;
    b->block = malloc(need);
    if (b->block == NULL) return 1;
    p = (unsigned char *) b->block;
    b->pos = (float *) p; p += (size_t) verts * 3u * sizeof(float);
    b->fog = (float *) p; p += (size_t) verts * 1u * sizeof(float);
    b->uv0 = (float *) p; p += (size_t) verts * 2u * sizeof(float);
    b->uv1 = (float *) p; p += (size_t) verts * 2u * sizeof(float);
    b->cap = (float *) p;
    p += ((size_t) verts * SL_BGOP_RECORD_FLOATS + SNAP_TAIL_FLOATS) * sizeof(float);
    b->col = p;
    b->verts = verts;
    return 0;
}

int sl_bgop_gl_run_snapshot(const struct sl_bgsnap *s,
                            struct sl_bgop_snap_tally *t, int report,
                            char *why, int whycap)
{
    const struct sl_bgop_field *fields = sl_bgop_fields();
    const char *vary[SL_BGOP_NFIELDS];
    struct snap_bufs b;
    struct ctx c;
    struct sl_bgor sink;
    char sink_path[520], sink_why[256];
    int sink_open = 0;
    GLuint prog = 0, buf = 0, query = 0;
    GLint loc[8];
    float Cf[16], Mf[16];
    unsigned errors = 0u, maxv = 0u;
    unsigned pre_rows = 0u, pre_bytes = 0u;
    unsigned di, i, j;
    int rc = 0;

    memset(&sink, 0, sizeof sink);
    sink_path[0] = '\0';
    g_why = why; g_whycap = whycap;
    if (why != NULL && whycap > 0) why[0] = '\0';
    if (t != NULL) memset(t, 0, sizeof *t);
    memset(&b, 0, sizeof b);
    if (s == NULL || t == NULL || s->draw == NULL || s->vertex == NULL ||
        s->index == NULL)
        return refuse("nothing to run: no snapshot was supplied");
    if (s->draws == 0u)
        return refuse("the snapshot holds no draws, so there is nothing to "
                      "capture and nothing to compare");

    /* THE CEILING, BEFORE ANY CONTEXT EXISTS. A draw too large to capture whole
     * refuses the run here, where the refusal costs nothing, rather than after
     * a context and a program have been built. */
    for (di = 0u; di < s->draws; di++) {
        unsigned v = s->draw[di].index_count;
        if (v > SL_BGOP_SNAP_MAX_DRAW_VERTS)
            return refuse("draw %u carries %u vertices, past this probe's "
                          "declared per-draw capture ceiling of %u; it is "
                          "refused rather than captured in pieces, because a "
                          "partial capture of a draw is not a measurement of "
                          "that draw", di, v, SL_BGOP_SNAP_MAX_DRAW_VERTS);
        if (v > maxv) maxv = v;
    }
    if (maxv == 0u)
        return refuse("every draw in the snapshot carries no vertices, so "
                      "nothing could be captured");
    if (snap_bufs_alloc(&b, maxv) != 0)
        return refuse("the client arrays and the capture buffer for the "
                      "largest draw (%u vertices) could not be allocated", maxv);

    /* THE RETENTION PREFLIGHT, AND IT GATES THE WHOLE RUN - before a window,
     * before a context, before a single GL call.
     *
     * RETENTION IS MANDATORY. The capture is the expensive part and the records
     * are the only durable product; a run that cannot keep them has measured
     * nothing that can be examined again. An earlier arrangement printed a
     * line when the sink would not open and then built the context and captured
     * anyway, which let a run whose arithmetic matched return success while its
     * raw outputs existed nowhere. So everything knowable without a driver is
     * decided here: the exact row count this frame will produce, that count
     * against the cap, the byte extent with its overflow guards, and that the
     * artifact's path is free.
     *
     * A refusal here costs nothing and creates nothing. */
    sl_bgor_default_path(sink_path, (int) sizeof sink_path, "snapshot");
    sink_why[0] = '\0';
    if (sl_bgor_preflight(s, sink_path, &pre_rows, &pre_bytes, sink_why,
                          (int) sizeof sink_why) != 0) {
        snap_bufs_free(&b);
        return refuse("RETENTION PREFLIGHT REFUSED, so no GL context was "
                      "created and nothing was captured: %s (this frame needs "
                      "%u row(s) and %u byte(s))", sink_why, pre_rows,
                      pre_bytes);
    }
    if (sl_bgor_open(&sink, sink_path, s, sink_why, (int) sizeof sink_why) != 0) {
        snap_bufs_free(&b);
        return refuse("THE RECORD ARTIFACT COULD NOT BE CREATED, so no GL "
                      "context was created and nothing was captured: %s",
                      sink_why);
    }
    sink_open = 1;
    printf("    retaining every captured record in %s (%u row(s), %u byte(s))\n",
           sink_path, pre_rows, pre_bytes);

    for (i = 0u; i < SL_BGOP_NFIELDS; i++) vary[i] = fields[i].varying;

    /* A SETUP THAT NEVER REACHED A DRAW SEALS ITS ARTIFACT AS FAILED AND LEAVES
     * IT. Zero rows, no completion flag, the reason in the header: a reader
     * refuses it as INCOMPLETE and cannot read it as a measurement, while the
     * file itself records that this path was attempted and how far it got.
     * Removing it to free the filename would delete that evidence, and the next
     * attempt being refused by EXISTS is the create-once rule working rather
     * than an obstacle. */
    if (ctx_up(&c) != 0 || version_ok() != 0 || resolve_all() != 0 ||
        resolve_attr() != 0 ||
        build_program_v(vary, SL_BGOP_NFIELDS, &prog) != 0) {
        char swhy[256];
        ctx_down(&c);
        snap_bufs_free(&b);
        (void) sl_bgor_seal_failed(&sink, "setup", swhy, (int) sizeof swhy);
        return 1;
    }
    for (i = 0u; i < 8u; i++) {
        loc[i] = p_GetUniformLocation(prog, sl_bgdp_uniform_names()[i]);
        if (loc[i] < 0) {
            rc = refuse("the linked program has no location for %s",
                        sl_bgdp_uniform_names()[i]);
            goto done;
        }
    }

    p_GenBuffers(1, &buf);
    p_GenQueries(1, &query);
    if (buf == 0 || query == 0) {
        rc = refuse("glGenBuffers/glGenQueries gave 0");
        goto done;
    }
    p_BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buf);
    p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, buf);

    /* The caller's own array formats, bound ONCE: the pointers are stable and
     * only the bytes behind them change per draw. */
    p_BindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, b.pos);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, b.col);
    /* THE FOG-COORDINATE ARRAY IS NOT ENABLED HERE. Its pointer is set once,
     * because the storage does not move, but whether the array is READ is a
     * per-draw property of the three-state fog contract and is decided inside
     * the loop. Enabling it for the whole run replayed every draw from the pool,
     * including the draws whose material disabled the array and used a current
     * coordinate of zero - a fidelity error in the replay itself, not merely in
     * the verdict, because the shader then forwarded a value the real draw
     * never read. */
    p_FogCoordPointer(GL_FLOAT, 0, b.fog);
    glDisableClientState(GL_FOG_COORD_ARRAY);
    p_ClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, b.uv0);
    p_ClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, b.uv1);
    p_ClientActiveTexture(GL_TEXTURE0);

    glEnable(GL_RASTERIZER_DISCARD);
    p_UseProgram(prog);
    errors += drain_errors();            /* the setup's own, before any draw */

    for (di = 0u; di < s->draws; di++) {
        const struct sl_bgsnap_draw *d = &s->draw[di];
        unsigned V = d->index_count, nfloats, counts[3];
        unsigned before = t->vertices;
        struct sl_bgop_fog_plan fog;
        GLuint produced = 0u;
        char gate[256];

        if (V == 0u) { t->draws_empty++; continue; }
        nfloats = V * SL_BGOP_RECORD_FLOATS + SNAP_TAIL_FLOATS;

        /* THIS DRAW'S FOG SOURCE, from the shared interpretation the classifier
         * and the reference builder use. Per draw, because the pass toggles the
         * array per material over one shared pool. `vx` is NULL: only the
         * binding decision is wanted here, and the expectation is per vertex. */
        sl_bgop_fog_plan(d, NULL, &fog);

        /* THE GATHER: one client-array vertex per index position, in order. */
        for (i = 0u; i < V; i++) {
            const struct sl_bgsnap_vertex *vx =
                &s->vertex[s->index[d->first_index + i]];
            b.pos[3u * i + 0u] = sl_bgsnap_f32(vx->pos_bits[0]);
            b.pos[3u * i + 1u] = sl_bgsnap_f32(vx->pos_bits[1]);
            b.pos[3u * i + 2u] = sl_bgsnap_f32(vx->pos_bits[2]);
            for (j = 0u; j < 4u; j++) b.col[4u * i + j] = vx->col[j];
            /* THE POOL IS GATHERED ONLY WHERE THE DRAW READS IT. For a draw
             * whose array is disabled the gathered value is never bound, and
             * filling it from the pool anyway would leave a value this draw
             * does not use sitting in the array the next draw might enable. */
            b.fog[i] = fog.bind_array ? sl_bgsnap_f32(vx->fog_bits) : 0.0f;
            b.uv0[2u * i + 0u] = sl_bgsnap_f32(vx->uv_bits[0][0]);
            b.uv0[2u * i + 1u] = sl_bgsnap_f32(vx->uv_bits[0][1]);
            b.uv1[2u * i + 0u] = sl_bgsnap_f32(vx->uv_bits[1][0]);
            b.uv1[2u * i + 1u] = sl_bgsnap_f32(vx->uv_bits[1][1]);
        }

        /* THE TEXTURE MATRICES, AND ONLY THE ONES THE DRAW RECORDED. A matrix
         * that was not recorded is not loaded and is not substituted with an
         * identity: the classification gives that unit's components NO ORACLE,
         * and loading a guess would turn a thing that is not known into a
         * number that looks measured. */
        if ((d->flags & SL_BGSNAP_DF_TEXMAT0) != 0u) {
            for (j = 0u; j < 16u; j++) Mf[j] = sl_bgsnap_f32(d->texmat_bits[0][j]);
            p_ActiveTexture(GL_TEXTURE0);
            glMatrixMode(GL_TEXTURE);
            glLoadMatrixf(Mf);
        }
        if ((d->flags & SL_BGSNAP_DF_TEXMAT1) != 0u) {
            for (j = 0u; j < 16u; j++) Mf[j] = sl_bgsnap_f32(d->texmat_bits[1][j]);
            p_ActiveTexture(GL_TEXTURE1);
            glMatrixMode(GL_TEXTURE);
            glLoadMatrixf(Mf);
        }
        p_ActiveTexture(GL_TEXTURE0);

        /* THE FOG SOURCE, APPLIED PER DRAW AND FAITHFUL TO THE RECORDED STATE.
         *   array on            the array is enabled and read from the pool.
         *   array off, zero     the array is disabled and the current
         *                       coordinate is set to the recorded 0.0f, which
         *                       reproduces what the pass did in the branch that
         *                       disabled it.
         *   no oracle           the array is disabled and NOTHING is set. The
         *                       effective coordinate was never recorded, so
         *                       there is no input to reproduce; binding the
         *                       pool would replay a value the draw did not use
         *                       and then compare against it. The captured fog
         *                       is retained with its class NO_ORACLE, which is
         *                       what "recorded as unmeasured" means here.
         * The other outputs of such a draw are still faithfully reproduced and
         * still compared; only its fog is withheld. */
        if (fog.bind_array) {
            glEnableClientState(GL_FOG_COORD_ARRAY);
        } else {
            glDisableClientState(GL_FOG_COORD_ARRAY);
            if (fog.set_current) p_FogCoordf((GLfloat) fog.current);
        }

        /* The draw's own uniform set, from the recorded patterns. */
        for (j = 0u; j < 16u; j++) Cf[j] = sl_bgsnap_f32(d->C_bits[j]);
        p_Uniform1f(loc[0], (GLfloat) sl_bgsnap_f32(d->sc_bits));
        p_Uniform1f(loc[1], (GLfloat) sl_bgsnap_f32(d->post_bits));
        p_Uniform3f(loc[2], (GLfloat) sl_bgsnap_f32(d->org_bits[0]),
                    (GLfloat) sl_bgsnap_f32(d->org_bits[1]),
                    (GLfloat) sl_bgsnap_f32(d->org_bits[2]));
        p_Uniform1fv(loc[3], 16, Cf);
        p_Uniform1i(loc[4], (d->flags & SL_BGSNAP_DF_FOGLESS) ? 1 : 0);
        p_Uniform1i(loc[5], (GLint) d->dmode);
        p_Uniform1f(loc[6], (GLfloat) sl_bgsnap_f32(d->fk1_bits));
        p_Uniform1f(loc[7], (GLfloat) sl_bgsnap_f32(d->fk2_bits));

        /* The sentinel across the WHOLE region, records and tail alike, so the
         * read-back's own arrival and the record layout are both provable. */
        for (j = 0u; j < nfloats; j++) b.cap[j] = sentinel_float();
        p_BufferData(GL_TRANSFORM_FEEDBACK_BUFFER,
                     (ptrdiff_t) ((size_t) nfloats * sizeof(float)),
                     b.cap, GL_DYNAMIC_READ);

        p_BeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
        p_BeginTransformFeedback(GL_POINTS);
        glDrawArrays(GL_POINTS, 0, (GLsizei) V);
        p_EndTransformFeedback();
        p_EndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);

        p_GetQueryObjectuiv(query, GL_QUERY_RESULT, &produced);
        if ((unsigned) produced != V) {
            rc = refuse("draw %u captured %lu primitives, not the %u its index "
                        "range names - its outputs are UNMEASURED and nothing "
                        "is compared", di, (unsigned long) produced, V);
            goto done;
        }

        for (j = 0u; j < nfloats; j++) b.cap[j] = sentinel_float();
        p_GetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                           (ptrdiff_t) ((size_t) nfloats * sizeof(float)), b.cap);
        if (glGetError() != GL_NO_ERROR) {
            errors++;
            rc = refuse("draw %u left a GL error behind; nothing is read from a "
                        "failed capture", di);
            goto done;
        }

        {
            unsigned b0;
            memcpy(&b0, &b.cap[0], sizeof b0);
            if (b0 == SENTINEL_BITS) {
                rc = refuse("draw %u: the transform feedback buffer still holds "
                            "the sentinel at offset 0, so no read-back happened "
                            "and the outputs are UNMEASURED", di);
                goto done;
            }
        }
        for (j = V * SL_BGOP_RECORD_FLOATS; j < nfloats; j++) {
            unsigned bj;
            memcpy(&bj, &b.cap[j], sizeof bj);
            if (bj != SENTINEL_BITS) {
                rc = refuse("draw %u: the capture wrote float %u, past the end "
                            "of the %u declared %d-float records, so the record "
                            "layout in sl_bgop_fields() does not match what this "
                            "driver captured and every field after the mismatch "
                            "would be read from the wrong offset. UNMEASURED.",
                            di, j, V, SL_BGOP_RECORD_FLOATS);
                goto done;
            }
        }

        /* THE GATE, AND THE CALLER'S OWN PREDICATE. n is this draw's vertex
         * count: one record completed and one primitive written per vertex, and
         * zero GL errors collected anywhere so far. */
        counts[0] = errors;
        counts[1] = V;
        counts[2] = (unsigned) produced;
        gate[0] = '\0';
        if (!sl_bgop_counts_ready(counts, (int) V, gate, (int) sizeof gate)) {
            rc = refuse("draw %u: %s", di, gate);
            goto done;
        }

        /* COMPARED AND RETAINED IN ONE PASS, while b.cap still holds this
         * draw's raw floats. The next iteration prefills that buffer with the
         * sentinel, so anything not written here is gone. */
        sl_bgop_snap_compare_draw(s, di, b.cap, V, report, t,
                                  sink_open ? &sink : NULL);
        if (t->vertices != before + V) {
            rc = refuse("draw %u: the comparison covered %u of its %u vertices, "
                        "so the run is not a measurement of it",
                        di, t->vertices - before, V);
            goto done;
        }
        t->draws_captured++;
    }

done:
    errors += drain_errors();
    p_UseProgram(0);
    glDisable(GL_RASTERIZER_DISCARD);
    p_ClientActiveTexture(GL_TEXTURE1);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    p_ClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_FOG_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (query != 0) p_DeleteQueries(1, &query);
    if (buf != 0) {
        p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
        p_BindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0);
        p_DeleteBuffers(1, &buf);
    }
    if (prog != 0) p_DeleteProgram(prog);
    errors += drain_errors();            /* what the teardown itself raised */
    ctx_down(&c);
    snap_bufs_free(&b);

    /* A GL ERROR RAISED AFTER THE LAST DRAW IS STILL FATAL. The per-draw gate
     * above cannot have seen it, so the whole run is re-gated here against the
     * vertices that were actually compared: records taken alongside a GL error
     * are not evidence of agreement however exactly they match. */
    if (rc == 0 && errors != 0u)
        rc = refuse("the run collected %u GL error(s), the last of them after "
                    "the final draw or during cleanup; %u draws and %u vertices "
                    "had been compared, and that comparison is withdrawn rather "
                    "than reported", errors, t->draws_captured, t->vertices);
    if (rc == 0 && t->draws_captured == 0u)
        rc = refuse("no draw was captured, so nothing was compared");
    if (rc == 0 && t->differing != 0u)
        rc = refuse("%u component(s) with an oracle differed from the recorded "
                    "reference", t->differing);

    /* ---- THE ARTIFACT IS SEALED, THEN RETENTION GATES THE STATUS --------
     *
     * A run that failed on its arithmetic or its capture still seals a COMPLETE
     * artifact when every row was retained, and is marked FAILED beside it: the
     * records are exactly what a disagreement has to be diagnosed from, and
     * withholding the completion flag would make the reader refuse them. A run
     * whose RETENTION failed seals without COMPLETE, because then the rows
     * really are missing.
     *
     * AND THEN THE RETENTION GATE DECIDES THE STATUS, which is the fail-open
     * this closes. The old arrangement let the close error leave `rc`
     * untouched, so a run whose components all matched returned 0 while its
     * records were absent. The gate is the caller's own predicate
     * (sl_bgop_retention_ready), applied here on the same tally, and it can
     * only ever turn a success into a refusal - never the other way round, and
     * never by altering a comparison count. */
    {
        char fwhy[512];
        unsigned expect_draws = 0u;
        int final_bad;

        /* The draws a clean run must have captured: every draw that carries
         * vertices. A draw skipped for holding none is not a missing capture,
         * and counting it as one would refuse every snapshot with an empty
         * draw in it. */
        for (di = 0u; di < s->draws; di++)
            if (s->draw[di].index_count != 0u) expect_draws++;

        fwhy[0] = '\0';
        final_bad = sl_bgop_finalize_records(&sink, sink_open, t, t->vertices,
                                            expect_draws, rc == 0, fwhy,
                                            (int) sizeof fwhy);
        printf("    retention: %u record(s) for %u comparison(s) over %u of %u "
               "draw(s), %u dropped, %u write failure(s) -> %s\n",
               t->retained, t->vertices, t->draws_captured, expect_draws,
               t->sink_dropped, t->sink_failed,
               final_bad ? "REFUSED" : "COMPLETE");
        if (final_bad != 0) {
            if (rc == 0) rc = refuse("%s", fwhy);
            else printf("    and: %s\n", fwhy);
        }
    }
    return rc;
}

#endif /* SL_BGGPU_PROBE_OUTPUTS */
