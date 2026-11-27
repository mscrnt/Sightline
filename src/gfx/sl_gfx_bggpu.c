/* The background pass's GPU vertex transform - opt-in.
 *
 * See sl_gfx_bggpu.h for what this is and is not. This file holds the entry
 * points, the shader text, the capability answer and the uniform upload. The
 * rendering predicates that decide which walk and which mesh may use it live
 * in sl_gfx_dl.c, beside the facts they are made of.
 *
 * THE ARITHMETIC IS THE CPU'S, DELIBERATELY. The expression order below is not
 * a convenience: `precise` plus the explicit parentheses are what stop the
 * compiler contracting the three multiplies and three adds into fused or
 * reassociated forms, and the resulting value is the one the render thread
 * computes in bg_vertex_range. Measured on this exact shader text (the r56/r59
 * GPU diagnostic, GL 4.6.0 NVIDIA 591.86 / GLSL 4.60): 38,654,208 vertices and
 * 270,579,456 component comparisons against the CPU reference, 0 mismatches in
 * every component. That campaign covered ONE camera transform at one pose on
 * one driver and one build, so it is evidence for this form, not a proof of GPU
 * arithmetic in general - and it covered the four clip components BEFORE any
 * depth selection, so neither the far clamp nor the far ranges below are inside
 * it. The components it did cover are exactly what SL_BGGPU_DEPTH_FAR1 draws
 * (the unclamped z) and what FAR0's `k1 * w - k2` is built from (the same w).
 *
 * WHY THERE IS NO SECOND SHADER. A fallback that dropped `precise`, or asked
 * for a lower language version that does not have it, would be a silent
 * arithmetic tolerance dressed as compatibility. A driver that cannot compile
 * this program gets the existing CPU path, which is exact by construction.
 *
 * APIENTRY ON EVERY TYPEDEF. These entry points are __stdcall on Windows. A
 * typedef without APIENTRY makes the caller clean up a callee-cleaned frame,
 * which corrupts the caller's own frame a few calls in and lands as a fault
 * somewhere else entirely; see the long note above SL_PFN_CREATESHADER in
 * sl_gfx_dl.c for the measurement. On Linux APIENTRY is empty and the two
 * conventions coincide, so carrying it costs nothing there. */
#ifndef __sgi

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
/* windows.h must precede <GL/gl.h>, which takes APIENTRY and WINGDIAPI from
 * it - the same include order the rest of src/gfx uses. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include "sl_gfx_bggpu.h"
/* The ONE player-facing performance selection and the policy that resolves it
 * into this module's question - may the transform be used. Host-clean (plain
 * ints), so the offline tests link the real policy and the real settings
 * store rather than a mock of either. */
#include "../platform/sl_perf.h"

/* The backend's own resolver: NULL before a context exists, and NULL when
 * this backend is not the one that came up, so every caller needs a
 * fallback - which this one has. Declared here because sl_gfx_sdl.c declares
 * it at its definition and src/gfx compiles against host headers with no
 * include path of its own. */
void *sl_gl_proc(const char *name);

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER   0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS  0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS     0x8B82
#endif

typedef GLuint (APIENTRY *SL_BGG_CREATESHADER)(GLenum);
typedef void   (APIENTRY *SL_BGG_SHADERSOURCE)(GLuint, GLsizei, const char **, const GLint *);
typedef void   (APIENTRY *SL_BGG_COMPILESHADER)(GLuint);
typedef void   (APIENTRY *SL_BGG_GETSHADERIV)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *SL_BGG_GETSHADERINFOLOG)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY *SL_BGG_CREATEPROGRAM)(void);
typedef void   (APIENTRY *SL_BGG_ATTACHSHADER)(GLuint, GLuint);
typedef void   (APIENTRY *SL_BGG_LINKPROGRAM)(GLuint);
typedef void   (APIENTRY *SL_BGG_GETPROGRAMIV)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *SL_BGG_GETPROGRAMINFOLOG)(GLuint, GLsizei, GLsizei *, char *);
typedef void   (APIENTRY *SL_BGG_USEPROGRAM)(GLuint);
typedef GLint  (APIENTRY *SL_BGG_GETUNIFORMLOCATION)(GLuint, const char *);
typedef void   (APIENTRY *SL_BGG_UNIFORM1F)(GLint, GLfloat);
typedef void   (APIENTRY *SL_BGG_UNIFORM3F)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *SL_BGG_UNIFORM1I)(GLint, GLint);
typedef void   (APIENTRY *SL_BGG_UNIFORM1FV)(GLint, GLsizei, const GLfloat *);

static SL_BGG_CREATESHADER       p_CreateShader;
static SL_BGG_SHADERSOURCE       p_ShaderSource;
static SL_BGG_COMPILESHADER      p_CompileShader;
static SL_BGG_GETSHADERIV        p_GetShaderiv;
static SL_BGG_GETSHADERINFOLOG   p_GetShaderInfoLog;
static SL_BGG_CREATEPROGRAM      p_CreateProgram;
static SL_BGG_ATTACHSHADER       p_AttachShader;
static SL_BGG_LINKPROGRAM        p_LinkProgram;
static SL_BGG_GETPROGRAMIV       p_GetProgramiv;
static SL_BGG_GETPROGRAMINFOLOG  p_GetProgramInfoLog;
static SL_BGG_USEPROGRAM         p_UseProgram;
static SL_BGG_GETUNIFORMLOCATION p_GetUniformLocation;
static SL_BGG_UNIFORM1F          p_Uniform1f;
static SL_BGG_UNIFORM3F          p_Uniform3f;
static SL_BGG_UNIFORM1I          p_Uniform1i;
static SL_BGG_UNIFORM1FV         p_Uniform1fv;

/* THE PROGRAM.
 *
 * `#version 400 compatibility` for two reasons, both required rather than
 * preferred: `precise` is GLSL 4.00, and the outputs below are the
 * compatibility profile's built-ins. Writing them is what keeps the pass's
 * existing fixed-function fragment stage - texturing, the alpha test, GL's fog
 * stage - fed exactly as it is fed today.
 *
 * THE TEXTURE MATRIX IS APPLIED HERE BECAUSE A VERTEX PROGRAM REPLACES THE
 * STAGE THAT WOULD HAVE APPLIED IT. The fixed pipeline's coordinate is
 * `gl_TextureMatrix[i] * gl_MultiTexCoord[i]`, and a program that writes
 * gl_TexCoord[i] from the raw attribute silently drops whatever matrix is in
 * force. That is not hypothetical in this pass: the game's water
 * (SL_WA_MAT_GAME_WATER) loads a translation on units 0 and 1 for the phases
 * its controller holds this tick, so a water mesh would have lost its scroll.
 * Writing the product reproduces the fixed-function rule in every case,
 * including whatever matrix the interpreter's own list left in force on entry -
 * which is the point: the program has to draw the frame the fixed pipeline
 * draws, not the frame it would draw with the matrix assumed away. With the
 * identity the product is exact (a multiply by 1.0 and three adds of 0.0), so
 * the ordinary mesh's coordinate is unchanged bit for bit.
 *
 * Three other fixed-function vertex stages a program would also replace were
 * checked over the whole renderer by a tree-wide source search rather than
 * assumed, because an absence claim needs one - `grep -rn ... src/gfx/*.c`:
 *   - USER CLIP PLANES: GL_CLIP_PLANE appears nowhere in src/gfx, so
 *     gl_ClipVertex has nothing to carry.
 *   - TEXTURE COORDINATE GENERATION: glTexGen appears nowhere in src/gfx
 *     either (one comment names it to say why it was not used).
 *   - LIGHTING: GL_LIGHTING is enabled at exactly one place, the Nintendo logo
 *     screen, and disabled again in the same function; the background pass
 *     never runs under it, so the colour passes straight through as the fixed
 *     pipeline passes it through unlit.
 *
 * NO `noperspective` HERE. The background pass draws its ordinary meshes
 * through the fixed pipeline, so its colour and its fog coordinate interpolate
 * perspective-correctly today; writing the built-ins keeps that. B-131's
 * screen-linear shade rule belongs to the display-list path's own programs,
 * where the RDP's triangles are, and bringing it here would change pixels in a
 * path that is supposed to draw the same frame.
 *
 * THE DEPTH AT THE END IS WHICHEVER OF THE PASS'S THREE THE DRAW IS IN, chosen
 * by u_dmode (see SL_BGGPU_DEPTH_* in the header for the three and why the
 * choice cannot be read from the clip array):
 *
 *   u_dmode == 0  the single-range frame's far clamp, bg_vertex_range's own. It
 *                 is a comparison, not arithmetic.
 *   u_dmode == 1  the first range of a far frame, `u_fk1 * c.w - u_fk2`, which
 *                 is the pass's blanket rewrite `fz_k1 * clip[w] - fz_k2`
 *                 character for character, in that order. The w it multiplies
 *                 is the one the clamp never touches on either path, so the two
 *                 expressions take the same multiplicand.
 *   u_dmode == 2  the game's range of a far frame: the clip z as computed, which
 *                 is what the pass's per-vertex zraw holds and writes back.
 *
 * The clamp's branch is left exactly as it was written and gated by the mode, so
 * a single-range frame's depth is the expression that was already measured
 * against the CPU in the live pair - not a re-derivation of it.
 *
 * The reductions the clamp feeds on the CPU - the count of clamped vertices and
 * the largest w, which is what the far ranges themselves are derived from - are
 * not computed here, because this program is not read back. The pass computes
 * them in its own loop before any of this runs, and the walk predicate in
 * sl_gfx_dl.c is what keeps a frame whose other CPU reductions are consumed
 * per vertex (the panorama bins, the per-vertex displacement) on the CPU path. */
static const char *BGG_VS =
"#version 400 compatibility\n"
"uniform float u_sc;\n"
"uniform float u_post;\n"
"uniform vec3  u_org;\n"
"uniform float u_C[16];\n"
"uniform int   u_fogless;\n"
"uniform int   u_dmode;\n"
"uniform float u_fk1;\n"
"uniform float u_fk2;\n"
"void main() {\n"
"    precise float x = (gl_Vertex.x * u_sc - u_org.x) * u_post;\n"
"    precise float y = (gl_Vertex.y * u_sc - u_org.y) * u_post;\n"
"    precise float z = (gl_Vertex.z * u_sc - u_org.z) * u_post;\n"
"    precise vec4 c;\n"
"    c.x = ((x * u_C[0] + y * u_C[4]) + z * u_C[8])  + u_C[12];\n"
"    c.y = ((x * u_C[1] + y * u_C[5]) + z * u_C[9])  + u_C[13];\n"
"    c.z = ((x * u_C[2] + y * u_C[6]) + z * u_C[10]) + u_C[14];\n"
"    c.w = ((x * u_C[3] + y * u_C[7]) + z * u_C[11]) + u_C[15];\n"
"    if (u_dmode == 1) c.z = u_fk1 * c.w - u_fk2;\n"
"    else if (u_dmode == 0 && u_fogless == 0 && c.w > 0.0 && c.z > c.w) c.z = c.w;\n"
"    gl_Position = c;\n"
"    gl_FrontColor = gl_Color;\n"
"    gl_BackColor = gl_Color;\n"
"    gl_FogFragCoord = gl_FogCoord;\n"
"    gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;\n"
"    gl_TexCoord[1] = gl_TextureMatrix[1] * gl_MultiTexCoord1;\n"
"}\n";

static GLuint g_prog;
static GLint  g_u_sc = -1, g_u_post = -1, g_u_org = -1, g_u_C = -1, g_u_fogless = -1;
static GLint  g_u_dmode = -1, g_u_fk1 = -1, g_u_fk2 = -1;
/* 0 untried, 1 ready, -1 a REAL capability failure and only that: a selection
 * that is off leaves this at 0, so OFF -> ON can still resolve and compile.
 * See sl_bggpu_ready. */
static int    g_state;
static char   g_why[256];

int sl_bggpu_state(void) { return g_state; }
const char *sl_bggpu_why(void) { return g_why; }

static int bgg_refuse(const char *why)
{
    g_state = -1;
    snprintf(g_why, sizeof g_why, "%s", why);
    fprintf(stderr, "#88 background GPU transform unavailable: %s - the "
                    "background draws its own clip positions\n", why);
    return 0;
}

/* THE ANSWER IS NO LONGER CACHED HERE, and that is the point.
 *
 * It used to be: `SL_BG_GPU_XFORM` was read once into a function static, so
 * the environment decided for the life of the process. The transform is now
 * selected by the OPTIMIZED RENDERING row, which a player can move while the
 * game is running, so this question has to be asked again rather than
 * remembered. src/platform/sl_perf.c holds the one precedence rule (the
 * developer switch, parsed once, governs for the launch; otherwise the row)
 * and reports an unrecognised value itself, so no getenv happens here and
 * none happens per walk - the only live part is an index into the settings
 * table. The caller still LATCHES one answer per walk before it reduces a
 * loop or admits a draw (sl_gfx_dl.c), so a press cannot land inside a walk. */
int sl_bggpu_opt_in(void)
{
    return sl_perf_bg_xform();
}

/* Called from the SDL backend at both ends of a context's life (sl_gfx_sdl.c:
 * after SDL_GL_CreateContext, and before SDL_GL_DeleteContext). Everything
 * cached here belongs to one context: the program name, the uniform locations
 * and the entry points the readiness check resolved. Dropping all of it back to
 * "untried" is what makes the next eligible walk ask again rather than bind a
 * name from a context that is gone; while it is untried the background draws
 * its own clip positions, and if the new context cannot compile the program it
 * keeps drawing them. No GL call is made here - the context may already be
 * unusable - so the old objects are released by the driver with it. */
void sl_bggpu_invalidate(void)
{
    g_state = 0;
    g_prog = 0;
    g_u_sc = g_u_post = g_u_org = g_u_C = g_u_fogless = -1;
    g_u_dmode = g_u_fk1 = g_u_fk2 = -1;
    g_why[0] = '\0';
}

int sl_bggpu_ready(void)
{
    GLuint vs;
    GLint ok = 0;

    if (g_state != 0) return g_state > 0;
    /* OFF DOES NOT POISON CAPABILITY. This used to latch g_state = -1, which
     * was sound while the answer was an environment variable read once: a
     * process that started with the transform off could never turn it on, so
     * there was nothing to re-ask, and -1 saved a wasted compile. The answer
     * is now a row a player can move, and a latched -1 would mean the FIRST
     * walk considered with the row off decided for the rest of the launch -
     * OFF -> ON would resolve no entry point, compile no program and silently
     * keep the CPU path for good. g_state stays 0 (untried), so the first walk
     * after the row goes ON resolves, compiles and links exactly as a process
     * that started ON would; a REAL failure below still latches -1 and is
     * still never retried. The reason string is written for the census and not
     * latched with a state. Nothing is parked or torn down by the row: an
     * already-compiled program keeps its name and its uniforms while the row
     * is off and is used again the moment it is on, and no CPU worker is
     * started or stopped by this. */
    if (!sl_bggpu_opt_in()) {
        snprintf(g_why, sizeof g_why, "%s",
                 "OPTIMIZED RENDERING is off, or SL_BG_GPU_XFORM says 0");
        return 0;
    }

    p_CreateShader       = (SL_BGG_CREATESHADER)       sl_gl_proc("glCreateShader");
    p_ShaderSource       = (SL_BGG_SHADERSOURCE)       sl_gl_proc("glShaderSource");
    p_CompileShader      = (SL_BGG_COMPILESHADER)      sl_gl_proc("glCompileShader");
    p_GetShaderiv        = (SL_BGG_GETSHADERIV)        sl_gl_proc("glGetShaderiv");
    p_GetShaderInfoLog   = (SL_BGG_GETSHADERINFOLOG)   sl_gl_proc("glGetShaderInfoLog");
    p_CreateProgram      = (SL_BGG_CREATEPROGRAM)      sl_gl_proc("glCreateProgram");
    p_AttachShader       = (SL_BGG_ATTACHSHADER)       sl_gl_proc("glAttachShader");
    p_LinkProgram        = (SL_BGG_LINKPROGRAM)        sl_gl_proc("glLinkProgram");
    p_GetProgramiv       = (SL_BGG_GETPROGRAMIV)       sl_gl_proc("glGetProgramiv");
    p_GetProgramInfoLog  = (SL_BGG_GETPROGRAMINFOLOG)  sl_gl_proc("glGetProgramInfoLog");
    p_UseProgram         = (SL_BGG_USEPROGRAM)         sl_gl_proc("glUseProgram");
    p_GetUniformLocation = (SL_BGG_GETUNIFORMLOCATION) sl_gl_proc("glGetUniformLocation");
    p_Uniform1f          = (SL_BGG_UNIFORM1F)          sl_gl_proc("glUniform1f");
    p_Uniform3f          = (SL_BGG_UNIFORM3F)          sl_gl_proc("glUniform3f");
    p_Uniform1i          = (SL_BGG_UNIFORM1I)          sl_gl_proc("glUniform1i");
    p_Uniform1fv         = (SL_BGG_UNIFORM1FV)         sl_gl_proc("glUniform1fv");
    if (p_CreateShader == NULL || p_ShaderSource == NULL || p_CompileShader == NULL
        || p_GetShaderiv == NULL || p_GetShaderInfoLog == NULL || p_CreateProgram == NULL
        || p_AttachShader == NULL || p_LinkProgram == NULL || p_GetProgramiv == NULL
        || p_GetProgramInfoLog == NULL || p_UseProgram == NULL || p_GetUniformLocation == NULL
        || p_Uniform1f == NULL || p_Uniform3f == NULL || p_Uniform1i == NULL
        || p_Uniform1fv == NULL)
        return bgg_refuse("GL 2.0 program entry points are not all present");

    vs = p_CreateShader(GL_VERTEX_SHADER);
    if (vs == 0) return bgg_refuse("glCreateShader gave no object");
    p_ShaderSource(vs, 1, &BGG_VS, NULL);
    p_CompileShader(vs);
    p_GetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        /* The driver's own words, not a guess at them: a refusal of
         * `#version 400 compatibility` and a refusal of `precise` read the
         * same from outside, and they are different facts. */
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetShaderInfoLog(vs, (GLsizei) sizeof log, &n, log);
        fprintf(stderr, "#88 background GPU transform: vertex program did not compile: %s\n", log);
        return bgg_refuse("the vertex program did not compile");
    }
    g_prog = p_CreateProgram();
    if (g_prog == 0) return bgg_refuse("glCreateProgram gave no object");
    p_AttachShader(g_prog, vs);
    p_LinkProgram(g_prog);
    ok = 0;
    p_GetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetProgramInfoLog(g_prog, (GLsizei) sizeof log, &n, log);
        fprintf(stderr, "#88 background GPU transform: program did not link: %s\n", log);
        g_prog = 0;
        return bgg_refuse("the program did not link");
    }
    g_u_sc      = p_GetUniformLocation(g_prog, "u_sc");
    g_u_post    = p_GetUniformLocation(g_prog, "u_post");
    g_u_org     = p_GetUniformLocation(g_prog, "u_org");
    g_u_C       = p_GetUniformLocation(g_prog, "u_C");
    g_u_fogless = p_GetUniformLocation(g_prog, "u_fogless");
    g_u_dmode   = p_GetUniformLocation(g_prog, "u_dmode");
    g_u_fk1     = p_GetUniformLocation(g_prog, "u_fk1");
    g_u_fk2     = p_GetUniformLocation(g_prog, "u_fk2");
    if (g_u_sc < 0 || g_u_post < 0 || g_u_org < 0 || g_u_C < 0 || g_u_fogless < 0
        || g_u_dmode < 0 || g_u_fk1 < 0 || g_u_fk2 < 0) {
        g_prog = 0;
        return bgg_refuse("a transform uniform did not resolve");
    }
    g_state = 1;
    fprintf(stderr, "#88 background GPU transform ready - the eligible background meshes "
                    "take their clip positions from the item's own model positions "
                    "(SL_BG_GPU_XFORM=0 or unset: the render thread computes them)\n");
    /* The shader object stays attached and neither object is deleted: the same
     * lifecycle the alpha and material programs and the pass's buffer objects
     * already have in this renderer. They are owned for the process and
     * released with the context at teardown, which is why there is no atexit
     * here - by then the context is gone and a delete would be a call into a
     * dead object. */
    return 1;
}

int sl_bggpu_use_walk(const struct sl_bggpu_walk *w)
{
    if (g_state <= 0 || w == NULL) return 0;
    p_UseProgram(g_prog);
    p_Uniform1f(g_u_sc, (GLfloat) w->sc);
    p_Uniform1f(g_u_post, (GLfloat) w->post);
    p_Uniform3f(g_u_org, (GLfloat) w->org[0], (GLfloat) w->org[1], (GLfloat) w->org[2]);
    p_Uniform1fv(g_u_C, 16, (const GLfloat *) w->C);
    p_Uniform1i(g_u_fogless, w->fogless ? 1 : 0);
    /* The depth state starts at the one-range frame's, so the program never
     * holds a mode or a coefficient from a previous walk. Every draw sets its
     * own through sl_bggpu_use_depth; this is the defined starting value, not
     * the value any draw relies on. */
    p_Uniform1i(g_u_dmode, SL_BGGPU_DEPTH_SINGLE);
    p_Uniform1f(g_u_fk1, 0.0f);
    p_Uniform1f(g_u_fk2, 0.0f);
    p_UseProgram(0);                 /* the caller binds it per mesh */
    return 1;
}

/* THIS DRAW'S DEPTH. glUniform writes the program in use, so the caller has the
 * program current already (sl_bggpu_bind) and this leaves the binding alone -
 * which is the whole reason it is separate from the walk's upload: a run of
 * eligible meshes keeps one binding and still needs one of these per mesh, and
 * the same mesh needs a different one in each of a far frame's two ranges.
 * k1 and k2 are uploaded whatever the mode so the program never carries a
 * stale pair; only mode FAR0 reads them. */
void sl_bggpu_use_depth(int mode, float k1, float k2)
{
    if (g_state <= 0) return;
    p_Uniform1i(g_u_dmode, (GLint) mode);
    p_Uniform1f(g_u_fk1, (GLfloat) k1);
    p_Uniform1f(g_u_fk2, (GLfloat) k2);
}

void sl_bggpu_bind(int on)
{
    if (g_state <= 0 || p_UseProgram == NULL) return;
    p_UseProgram(on ? g_prog : 0u);
}

#endif /* !__sgi */
