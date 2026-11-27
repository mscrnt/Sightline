/* SYNTHETIC GL for the background pass's GPU vertex transform. See the header
 * for how the shim gets in front of the real GL headers, what it fabricates
 * and what it refuses to provide. No window, no native graphics API, no game:
 * nothing here links against opengl32 or SDL.
 *
 * NOTE WHAT IS ABSENT, because the build is what checks it. There is no
 * glFinish, glFlush, glGetError, glMapBuffer, glUnmapBuffer, glReadPixels,
 * glGetUniformfv, glDeleteShader or glDeleteProgram in this file and no
 * declaration of one in the header. The module resolves every entry point it
 * uses through sl_gl_proc, so a call to one of those would be a resolve that
 * returns NULL here - and the two it would have to reach without sl_gl_proc,
 * a direct GL-1.1 call such as glFinish or glGetError, would be an undefined
 * symbol at LINK time. Either way the fixture stops, which is stronger than a
 * counter: a counter only sees the paths a test drives, whereas a missing
 * symbol is checked over the whole translation unit.
 */
#include "sl_gfx_bggpu_testgl.h"

#include <string.h>

struct sl_bggpu_testgl bg;

/* The uniforms the module resolves, in the order sl_bggpu_ready asks for
 * them. The locations are this stub's invention: 100 + the index, stable
 * across generations so a test can name a location by its uniform. */
const char *const bgt_uniform_names[8] = {
    "u_sc", "u_post", "u_org", "u_C", "u_fogless", "u_dmode", "u_fk1", "u_fk2"
};

#define BGT_LOC_BASE 100

/* A name carries the generation that minted it, so a name used after
 * bgt_new_context is detectable rather than merely suspicious. */
#define BGT_GEN(n)      ((int) ((n) >> 12))
#define BGT_MINT(seq)   ((GLuint) (((GLuint) bg.context << 12) | (GLuint) (seq)))

static int shader_seq, program_seq;

void bgt_reset(void)
{
    memset(&bg, 0, sizeof bg);
    bg.context = 1;
    shader_seq = 0;
    program_seq = 0;
}

void bgt_new_context(void)
{
    const struct sl_bggpu_testgl keep = bg;
    memset(&bg, 0, sizeof bg);
    bg.withhold_proc      = keep.withhold_proc;
    bg.withhold_uniform   = keep.withhold_uniform;
    bg.create_shader_zero = keep.create_shader_zero;
    bg.create_program_zero = keep.create_program_zero;
    bg.fail_compile       = keep.fail_compile;
    bg.fail_link          = keep.fail_link;
    bg.context            = keep.context + 1;
    shader_seq = 0;
    program_seq = 0;
}

GLint bgt_loc(const char *name)
{
    int i;
    for (i = 0; i < 8; i++)
        if (strcmp(name, bgt_uniform_names[i]) == 0) return BGT_LOC_BASE + i;
    return -1;
}

/* Every entry point passes through here: one total call count, and the
 * after-teardown watchdog in one place. */
static void enter(void)
{
    bg.calls++;
    if (bg.context_destroyed) bg.v_call_after_context_destroyed++;
}

/* A name the module hands back to GL must belong to the generation in force.
 * 0 is not a name - it is the fixed pipeline, and always legal. */
static void checkname(GLuint n)
{
    if (n != 0u && BGT_GEN(n) != bg.context) bg.v_stale_name++;
}

/* ---- the fake GL -------------------------------------------------------- */

static GLuint APIENTRY bgt_CreateShader(GLenum type)
{
    enter();
    bg.create_shader++;
    if (type != GL_VERTEX_SHADER) return 0u;
    if (bg.create_shader_zero) return 0u;
    bg.last_shader = BGT_MINT(++shader_seq);
    return bg.last_shader;
}

static void APIENTRY bgt_ShaderSource(GLuint sh, GLsizei count,
                                      const char **src, const GLint *len)
{
    enter();
    bg.shader_source++;
    checkname(sh);
    if (sh == 0u || sh != bg.last_shader) bg.v_source_without_shader++;
    /* THE TEXT IS RECORDED, NOT COMPILED. There is no GLSL compiler here, so
     * what a test can check is that the program text was handed over once, in
     * one string, with no explicit length (the module passes NULL, meaning
     * NUL-terminated). What the text COMPUTES is outside this fixture. */
    bg.last_source = (src != NULL && count > 0) ? src[0] : NULL;
    bg.last_source_count = count;
    (void) len;
}

static void APIENTRY bgt_CompileShader(GLuint sh)
{
    enter();
    bg.compile++;
    checkname(sh);
}

static void APIENTRY bgt_GetShaderiv(GLuint sh, GLenum pname, GLint *out)
{
    enter();
    bg.get_shaderiv++;
    checkname(sh);
    if (pname == GL_COMPILE_STATUS && out != NULL) *out = bg.fail_compile ? 0 : 1;
}

static void APIENTRY bgt_GetShaderInfoLog(GLuint sh, GLsizei cap, GLsizei *n, char *log)
{
    static const char text[] = "SYNTHETIC: the fake driver refused this shader";
    GLsizei k = (GLsizei) (sizeof text - 1u);
    enter();
    bg.get_shaderlog++;
    checkname(sh);
    if (log == NULL || cap <= 0) return;
    if (k > cap - 1) k = cap - 1;
    memcpy(log, text, (size_t) k);
    log[k] = '\0';
    if (n != NULL) *n = k;
}

static GLuint APIENTRY bgt_CreateProgram(void)
{
    enter();
    bg.create_program++;
    if (bg.create_program_zero) return 0u;
    bg.last_program = BGT_MINT(0x800 | ++program_seq);
    return bg.last_program;
}

static void APIENTRY bgt_AttachShader(GLuint prog, GLuint sh)
{
    enter();
    bg.attach++;
    checkname(prog);
    checkname(sh);
}

static void APIENTRY bgt_LinkProgram(GLuint prog)
{
    enter();
    bg.link++;
    checkname(prog);
}

static void APIENTRY bgt_GetProgramiv(GLuint prog, GLenum pname, GLint *out)
{
    enter();
    bg.get_programiv++;
    checkname(prog);
    if (pname == GL_LINK_STATUS && out != NULL) *out = bg.fail_link ? 0 : 1;
}

static void APIENTRY bgt_GetProgramInfoLog(GLuint prog, GLsizei cap, GLsizei *n, char *log)
{
    static const char text[] = "SYNTHETIC: the fake driver refused this link";
    GLsizei k = (GLsizei) (sizeof text - 1u);
    enter();
    bg.get_programlog++;
    checkname(prog);
    if (log == NULL || cap <= 0) return;
    if (k > cap - 1) k = cap - 1;
    memcpy(log, text, (size_t) k);
    log[k] = '\0';
    if (n != NULL) *n = k;
}

static void APIENTRY bgt_UseProgram(GLuint prog)
{
    enter();
    bg.use_program++;
    checkname(prog);
    bg.current = prog;
}

static GLint APIENTRY bgt_GetUniformLocation(GLuint prog, const char *name)
{
    enter();
    bg.get_uniform_location++;
    checkname(prog);
    if (name == NULL) return -1;
    if (bg.withhold_uniform != NULL && strcmp(name, bg.withhold_uniform) == 0) return -1;
    return bgt_loc(name);
}

/* THE UNIFORM RULE THE REAL GL HAS, modelled rather than relaxed: glUniform*
 * writes the program IN USE, so with no program current it raises
 * INVALID_OPERATION and writes nothing, and a location of -1 is silently
 * ignored. Both are counted as rejected and leave the log alone, so a test
 * reads the log as "what the program actually received". */
static struct bgt_uwrite *accept(int kind, GLint loc)
{
    struct bgt_uwrite *w;
    bg.uniform_calls++;
    if (bg.current == 0u || loc < 0 || bg.nu >= BGT_UMAX) {
        bg.uniform_rejected++;
        return NULL;
    }
    w = &bg.u[bg.nu++];
    memset(w, 0, sizeof *w);
    w->kind = kind;
    w->loc = loc;
    w->prog = bg.current;
    return w;
}

static void APIENTRY bgt_Uniform1f(GLint loc, GLfloat v)
{
    struct bgt_uwrite *w;
    enter();
    w = accept(BGT_U1F, loc);
    if (w != NULL) { w->count = 1; w->fv[0] = v; }
}

static void APIENTRY bgt_Uniform3f(GLint loc, GLfloat a, GLfloat b, GLfloat c)
{
    struct bgt_uwrite *w;
    enter();
    w = accept(BGT_U3F, loc);
    if (w != NULL) { w->count = 3; w->fv[0] = a; w->fv[1] = b; w->fv[2] = c; }
}

static void APIENTRY bgt_Uniform1i(GLint loc, GLint v)
{
    struct bgt_uwrite *w;
    enter();
    w = accept(BGT_U1I, loc);
    if (w != NULL) { w->count = 1; w->ival = v; }
}

static void APIENTRY bgt_Uniform1fv(GLint loc, GLsizei n, const GLfloat *v)
{
    struct bgt_uwrite *w;
    GLsizei i;
    enter();
    w = accept(BGT_U1FV, loc);
    if (w == NULL) return;
    w->count = n;
    for (i = 0; i < n && i < 16; i++) w->fv[i] = v[i];
}

/* ---- the resolver ------------------------------------------------------- */

/* The backend's own resolver, as the module declares it. Every name the module
 * asks for is here except the one a test withholds; nothing else resolves, so
 * a module that reached for a sync point or a delete would get NULL. */
void *sl_gl_proc(const char *name)
{
    struct { const char *name; void *fn; } table[] = {
        { "glCreateShader",       (void *) bgt_CreateShader },
        { "glShaderSource",       (void *) bgt_ShaderSource },
        { "glCompileShader",      (void *) bgt_CompileShader },
        { "glGetShaderiv",        (void *) bgt_GetShaderiv },
        { "glGetShaderInfoLog",   (void *) bgt_GetShaderInfoLog },
        { "glCreateProgram",      (void *) bgt_CreateProgram },
        { "glAttachShader",       (void *) bgt_AttachShader },
        { "glLinkProgram",        (void *) bgt_LinkProgram },
        { "glGetProgramiv",       (void *) bgt_GetProgramiv },
        { "glGetProgramInfoLog",  (void *) bgt_GetProgramInfoLog },
        { "glUseProgram",         (void *) bgt_UseProgram },
        { "glGetUniformLocation", (void *) bgt_GetUniformLocation },
        { "glUniform1f",          (void *) bgt_Uniform1f },
        { "glUniform3f",          (void *) bgt_Uniform3f },
        { "glUniform1i",          (void *) bgt_Uniform1i },
        { "glUniform1fv",         (void *) bgt_Uniform1fv }
    };
    size_t i;
    /* A resolve is not a GL call, so it does not raise bg.calls - but it is
     * still refused after teardown, and still counted, because "no entry point
     * is resolved while the row is off" is one of the properties. */
    bg.proc_calls++;
    if (bg.context_destroyed) bg.v_call_after_context_destroyed++;
    if (name == NULL) return NULL;
    if (bg.withhold_proc != NULL && strcmp(name, bg.withhold_proc) == 0) return NULL;
    for (i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(name, table[i].name) == 0) return table[i].fn;
    return NULL;
}
