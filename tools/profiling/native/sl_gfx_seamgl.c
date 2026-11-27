/* SYNTHETIC GL for the background draw seam. See sl_gfx_seamgl.h for how the
 * real host headers are kept while the driver is replaced (-D_GDI32_), what
 * this models and why the draw record rather than a counter is the point.
 *
 * THE STATE MODEL IS GL'S, NOT A CONVENIENCE. A client pointer command
 * captures the array buffer bound AT THE TIME IT RUNS, which is what makes its
 * pointer argument an offset into that buffer rather than a host address;
 * glBufferData is void and says nothing about whether storage was allocated.
 * Both are modelled rather than relaxed, because the claims under test are
 * about exactly those two rules.
 */
#include "sl_gfx_seamgl.h"

#include <string.h>

/* ---- THE FIXTURE'S OWN FAULT INJECTION ----------------------------------
 *
 * A suite that passes on the first build has proved nothing until it is shown
 * able to fail. Each BGTFSEAM_MUT_* breaks ONE thing the fake driver observes,
 * and tools/windows/bgtfseamtest.ps1 builds a separate executable for each and
 * requires it to FAIL. They are fault injection into the SYNTHETIC DRIVER,
 * never into the product: src/gfx/sl_gfx_dl.c and src/gfx/sl_gfx_bgtf.c are
 * compiled from the same unmodified source in every one of these builds, and a
 * mutant that passed would mean the corresponding assertion does not bite.
 *
 *   COUNTALL   every glDrawElements also reports itself to
 *              sl_bgtf_count_submission, as though the candidate had served
 *              it. That is precisely what the real gate (g_bgtf_src in
 *              sl_gfx_dl.c) exists to prevent, so it must break every
 *              assertion that an excluded, filtered, refused or CPU-fallback
 *              mesh contributes NO coverage.
 *   PTRSTICK   the first position pointer of a measurement window sticks: a
 *              later pointer command does not change what a draw reads. A
 *              restoration that happened would then be invisible, so it must
 *              break every assertion that the original source came back.
 *   ALLOCOK    the GL_BUFFER_SIZE query reports what the last glBufferData
 *              REQUESTED rather than what was installed - a driver whose
 *              metadata lies - so a short allocation reads as accepted and the
 *              prepass-refusal assertions must fail.
 *
 * None is defined in an ordinary build.
 */

struct seamgl sg;

/* A buffer name and the byte size the last glBufferData left on it. The sizes
 * are what make the overrun watchdog possible. */
static struct { GLuint name; long bytes; } bufs[SEAMGL_BUFFERS];
static int nbufs;
static GLuint buffer_seq;

/* The resolved entry points both modules ask for. Nothing else is here, so a
 * read-back or a fence answers NULL. glDeleteProgram and glDeleteShader are the
 * ONLY deletes: the neutral-program control (SL_BG_TF_NEUTRAL_PROG) owns one
 * program object and one shader object and releases each exactly once, and every
 * other delete still answers NULL. */
static const char *const seam_procs[] = {
    "glCreateShader", "glShaderSource", "glCompileShader", "glGetShaderiv",
    "glGetShaderInfoLog", "glCreateProgram", "glAttachShader", "glLinkProgram",
    "glGetProgramiv", "glGetProgramInfoLog", "glUseProgram", "glDeleteProgram",
    "glDeleteShader",
    "glGetUniformLocation", "glUniform1f", "glUniform2f", "glUniform3f",
    "glUniform4f", "glUniform1i", "glUniform1fv", "glUniformMatrix4fv",
    "glTransformFeedbackVaryings", "glBindBufferBase",
    "glBeginTransformFeedback", "glEndTransformFeedback",
    "glGenBuffers", "glBindBuffer", "glBufferData", "glBufferSubData",
    "glGetBufferParameteriv",
    "glActiveTexture", "glClientActiveTexture", "glMultiTexCoord2f",
    "glFogCoordf", "glFogCoordPointer"
};

/* Every entry point passes through here: one total call count, and the
 * per-context attribution in one place. A call made with no context current is
 * a violation outright - nothing in GL happens - and a call on the auxiliary
 * context is counted rather than refused, that being the whole point of the
 * control this shim models a capable platform for. */
static void seam_enter(void)
{
    sg.calls++;
    if (sg.ctx_cur < 0) sg.v_call_no_context++;
    else if (sg.ctx_cur > 0) sg.calls_on_aux++;
}

/* ---- THE BACKEND'S AUXILIARY GL CONTEXT, FAKED AND ALWAYS CAPABLE -------
 *
 * The product's src/gfx/sl_gfx_sdl.c supplies these four for real; this file
 * supplies them for the reason it supplies sl_gl_proc. A capable platform is
 * modelled deliberately: every refusal shape is the module fixture's, which can
 * gate each one independently, and what this fixture owns is the sequence the
 * REAL caller produces when nothing fails.
 *
 * A HELPER IS NOT A GL CALL. None of these goes through seam_enter - they are
 * platform calls, not entry points - so they do not raise `sg.calls`. What they
 * move is `sg.ctx_cur`, and every GL call after them is attributed by it. */
int sl_gl_aux_create(void);
int sl_gl_aux_create(void)
{
    if (sg.aux_exists) return 1;
    sg.aux_exists = 1;
    sg.aux_created++;
    /* The create makes the new context current and the backend switches back, so
     * a caller returning from here is on the MAIN context. */
    sg.ctx_cur = 0;
    return 1;
}

int sl_gl_aux_make_current(void);
int sl_gl_aux_make_current(void)
{
    if (!sg.aux_exists) return 0;
    sg.ctx_cur = 1;
    sg.aux_switches++;
    return 1;
}

int sl_gl_main_make_current(void);
int sl_gl_main_make_current(void)
{
    sg.ctx_cur = 0;
    sg.main_switches++;
    return 1;
}

int sl_gl_aux_destroy(void);
int sl_gl_aux_destroy(void)
{
    if (!sg.aux_exists) return 0;
    sg.aux_exists = 0;
    sg.aux_destroyed++;
    return 1;
}

/* THE LOST-CONTEXT EXIT PATH. The product's ends the process without running
 * exit handlers, so that no GL command can follow a context nobody can name.
 * This shim models a platform on which nothing fails, so the claim this fixture
 * makes about it is that it is NEVER REACHED - which is worth recording rather
 * than leaving as an unresolved symbol, because a module that took this path on
 * a healthy platform would otherwise show up as a link error and not as a failed
 * assertion. */
void sl_gl_terminate_no_gl(int code);
void sl_gl_terminate_no_gl(int code)
{
    (void) code;
    sg.fatal_calls++;
}

/* THE AUXILIARY-CONTEXT CONTROL'S ONE QUERY, answered TRUE for a program name
 * this shim minted, which is a platform that honoured the sharing request. The
 * FALSE shape - a context that does not share names - is the module fixture's
 * case. Where it was asked is recorded, because the question is only meaningful
 * on the auxiliary context. */
static GLboolean APIENTRY seam_IsProgram(GLuint p)
{
    seam_enter();
    sg.is_program_calls++;
    sg.is_program_last = p;
    sg.is_program_on_aux = sg.ctx_cur > 0 ? 1 : 0;
    return (GLboolean) (p != 0u ? GL_TRUE : GL_FALSE);
}

static int buf_index(GLuint name)
{
    int i;
    for (i = 0; i < nbufs; i++) if (bufs[i].name == name) return i;
    return -1;
}

void sl_seamgl_reset(void)
{
    memset(&sg, 0, sizeof sg);
    /* GL'S OWN DEFAULT CLIENT ACTIVE UNIT is GL_TEXTURE0, and 0 is not it: the
     * token is 0x84C0, so a zeroed struct would describe a unit index of
     * -0x84C0 and every per-unit read would be out of range. Every client array
     * starts disabled, which is GL's default too. */
    sg.cli.unit = GL_TEXTURE0;
    /* `ctx_cur` needs no assignment and that is worth stating rather than
     * leaving a reader to look for it: 0 is the MAIN context, which is what a
     * backend hands a module, so the memset above is already the right answer.
     * `aux_exists` 0 is right for the same reason - no auxiliary context exists
     * until one is created. */
    memset(bufs, 0, sizeof bufs);
    nbufs = 0;
    buffer_seq = 0;
}

void sl_seamgl_window(void)
{
    const struct seamgl keep = sg;
    memset(&sg, 0, sizeof sg);
    sg.withhold_proc      = keep.withhold_proc;
    sg.gen_buffers_zero   = keep.gen_buffers_zero;
    sg.alloc_short_initial = keep.alloc_short_initial;
    sg.alloc_short_growth = keep.alloc_short_growth;
    sg.alloc_bytes        = keep.alloc_bytes;
    sg.alloc_short_name   = keep.alloc_short_name;
    sg.alloc_short_skip   = keep.alloc_short_skip;
    memcpy(sg.alloc_exempt, keep.alloc_exempt, sizeof sg.alloc_exempt);
    sg.getbufparam_fail   = keep.getbufparam_fail;
    sg.provide_readback   = keep.provide_readback;
    sg.units_reported     = keep.units_reported;
    sg.coords_reported    = keep.coords_reported;
    /* THE CLIENT ARRAYS ARE MODELLED STATE, so a measurement window keeps them
     * exactly as it keeps the bindings and the pointers: the pass enables them
     * once and a later walk inherits them, which is the condition the
     * position-only-input control exists to act on. Wiping them here would make
     * every restore read as correct against GL's defaults. */
    sg.cli                = keep.cli;
    sg.current            = keep.current;
    sg.arr_buf            = keep.arr_buf;
    sg.elem_buf           = keep.elem_buf;
    sg.tf_buf             = keep.tf_buf;
    sg.discard            = keep.discard;
    sg.tf_active          = keep.tf_active;
    sg.pos = keep.pos; sg.col = keep.col; sg.uv = keep.uv; sg.nrm = keep.nrm;
}

long sl_seamgl_buffer_bytes(GLuint name)
{
    const int i = buf_index(name);
    return i < 0 ? -1L : bufs[i].bytes;
}

/* ---- the GL 1.1 entry points this fixture OBSERVES ---------------------- */

void APIENTRY glEnable(GLenum cap)
{
    seam_enter();
    if (cap == GL_RASTERIZER_DISCARD) sg.discard = 1;
}

void APIENTRY glDisable(GLenum cap)
{
    seam_enter();
    if (cap == GL_RASTERIZER_DISCARD) sg.discard = 0;
}

/* ---- THE CLIENT-ARRAY ENABLES, MODELLED ---------------------------------
 *
 * These were inert until the position-only-input control needed them: that
 * control narrows the arrays a prepass draw dereferences and puts them back, and
 * a shim that discarded the enables could not say whether it did. The coordinate
 * arrays are indexed by the client active unit in force at the call, which is
 * how GL works and is what makes a wrong-unit restore observable.
 *
 * ONLY CLIENT ARRAYS ARE MODELLED. glIsEnabled below still answers GL_FALSE for
 * every server enable, exactly as it did before, so the pass's own
 * GL_SCISSOR_TEST, GL_FOG and GL_DEPTH_CLAMP reads are unchanged.
 */
static int cli_unit_index(void)
{
    const long u = (long) sg.cli.unit - (long) GL_TEXTURE0;
    if (u < 0 || u >= SEAMGL_UNITCAP) { sg.v_client_unit_range++; return -1; }
    return (int) u;
}

static GLboolean *cli_slot(GLenum cap)
{
    int u;
    switch (cap) {
    case GL_VERTEX_ARRAY:          return &sg.cli.v;
    case GL_COLOR_ARRAY:           return &sg.cli.c;
    case GL_NORMAL_ARRAY:          return &sg.cli.n;
    case GL_INDEX_ARRAY:           return &sg.cli.i;
    case GL_EDGE_FLAG_ARRAY:       return &sg.cli.e;
    case GL_SECONDARY_COLOR_ARRAY: return &sg.cli.s;
    case GL_FOG_COORD_ARRAY:       return &sg.cli.f;
    case GL_TEXTURE_COORD_ARRAY:
        u = cli_unit_index();
        return u < 0 ? NULL : &sg.cli.t[u];
    default:
        return NULL;
    }
}

void APIENTRY glEnableClientState(GLenum cap)
{
    GLboolean *p;
    seam_enter();
    sg.client_state++;
    sg.client_enable++;
    p = cli_slot(cap);
    if (p != NULL) *p = (GLboolean) GL_TRUE;
}

void APIENTRY glDisableClientState(GLenum cap)
{
    GLboolean *p;
    seam_enter();
    sg.client_state++;
    sg.client_disable++;
    p = cli_slot(cap);
    if (p != NULL) *p = (GLboolean) GL_FALSE;
}

/* A POINTER COMMAND CAPTURES THE ARRAY BUFFER BOUND NOW. That is why the
 * candidate brackets its own call with a bind and an unbind, and why a buffer
 * left bound afterwards would turn every later client pointer into an offset
 * into it. */
static void ptr_set(struct seamgl_ptr *p, GLint size, GLenum type,
                    GLsizei stride, const void *ptr)
{
    p->set = 1;
    p->size = size;
    p->type = type;
    p->stride = stride;
    p->off = (long) (size_t) ptr;
    p->buf = sg.arr_buf;
}

void APIENTRY glVertexPointer(GLint size, GLenum type, GLsizei stride,
                              const GLvoid *ptr)
{
    seam_enter();
    sg.vertex_pointer++;
#ifdef BGTFSEAM_MUT_PTRSTICK
    /* THE POINTER THAT WILL NOT MOVE. The first source of a measurement window
     * is the only one any later draw sees, so a restoration becomes invisible. */
    if (sg.pos.set) return;
#endif
    ptr_set(&sg.pos, size, type, stride, ptr);
}

void APIENTRY glColorPointer(GLint size, GLenum type, GLsizei stride,
                             const GLvoid *ptr)
{
    seam_enter();
    ptr_set(&sg.col, size, type, stride, ptr);
}

void APIENTRY glTexCoordPointer(GLint size, GLenum type, GLsizei stride,
                                const GLvoid *ptr)
{
    seam_enter();
    ptr_set(&sg.uv, size, type, stride, ptr);
}

void APIENTRY glNormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{
    seam_enter();
    ptr_set(&sg.nrm, 3, type, stride, ptr);
}

/* ONE DRAW, WITH THE WHOLE SOURCE IT READ. Sampling the state after the walk
 * cannot say which draw was wrong, so each one keeps its own copy. */
static struct seamgl_draw *draw_rec(void)
{
    struct seamgl_draw *d;
    const long seq = ++sg.seq;
    if (sg.ndraws >= SEAMGL_DRAWS) { sg.v_draws_full++; return NULL; }
    d = &sg.draw[sg.ndraws++];
    memset(d, 0, sizeof *d);
    d->program  = sg.current;
    d->discard  = sg.discard;
    d->tf_active = sg.tf_active;
    d->tf_buf   = sg.tf_buf;
    /* AND WHICH CONTEXT THE DRAW LANDED ON, sampled here for the reason the
     * three conditions above are sampled here. */
    d->ctx      = sg.ctx_cur;
    d->elem_buf = sg.elem_buf;
    d->arr_buf  = sg.arr_buf;
    d->seq      = seq;
    d->pos = sg.pos; d->col = sg.col; d->uv = sg.uv; d->nrm = sg.nrm;
    /* THE CLIENT ARRAYS AS THIS DRAW FOUND THEM. Sampled here and not at the end
     * of the walk, because the position-only-input control puts them back
     * immediately after its draw: state read afterwards describes the restore,
     * not what the draw dereferenced. */
    d->cli = sg.cli;
    return d;
}

void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type,
                             const GLvoid *indices)
{
    struct seamgl_draw *d;
    seam_enter();
    sg.draw_elements++;
    d = draw_rec();
    if (d != NULL) {
        d->arrays = 0;
        d->mode = mode;
        d->count = (long) count;
        d->elem_off = (long) (size_t) indices;
    }
    (void) type;
#ifdef BGTFSEAM_MUT_COUNTALL
    /* EVERY DRAW CREDITED TO THE CANDIDATE, which is exactly what the real gate
     * in sl_gfx_dl.c exists to prevent. */
    if (count > 0) {
        extern void sl_bgtf_count_submission(int dmode, unsigned n);
        sl_bgtf_count_submission(0, (unsigned) count);
    }
#endif
}

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    struct seamgl_draw *d;
    seam_enter();
    sg.draw_arrays++;
    /* AND WHICH CONTEXT IT LANDED ON, as a count beside the per-draw stamp: the
     * auxiliary-context control's claim is that one draw was issued and issued
     * THERE, which a total alone would pass for an arm that drew on the main
     * context. */
    if (sg.ctx_cur > 0) sg.draws_on_aux++;
    /* THE PREPASS DRAW'S OWN PRECONDITIONS. A recording draw must produce no
     * fragment and must fit the pool bound at index 0: one vec4 per vertex, and
     * a pool sized for fewer would be a write past its end. */
    if (sg.tf_active) {
        if (!sg.discard) sg.v_draw_without_discard++;
        if (sg.tf_buf != 0u) {
            const long have = sl_seamgl_buffer_bytes(sg.tf_buf);
            const long long need = (long long) count * 4LL * (long long) sizeof(GLfloat);
            if (have < 0 || (long long) have < need) sg.v_tf_overrun++;
        }
    }
    d = draw_rec();
    if (d != NULL) {
        d->arrays = 1;
        d->mode = mode;
        d->count = (long) count;
        d->first = (long) first;
    }
}

/* glGetString is read by sl_gfx_dl.c's own capability probes, which strstr the
 * result: NULL there would fault, so an empty string is handed back and every
 * extension probe simply finds nothing. */
const GLubyte *APIENTRY glGetString(GLenum name)
{
    static const GLubyte empty[1] = { 0 };
    seam_enter();
    (void) name;
    return empty;
}

GLenum APIENTRY glGetError(void)       { seam_enter(); return GL_NO_ERROR; }

/* CLIENT ARRAYS ANSWER TRUTHFULLY; EVERYTHING ELSE STILL ANSWERS FALSE. The
 * second half is not a shortcut but the preservation of what this shim did
 * before: the pass reads GL_SCISSOR_TEST, GL_FOG and GL_DEPTH_CLAMP through
 * here, and those answers must not change because a different control needed
 * the client arrays modelled. */
GLboolean APIENTRY glIsEnabled(GLenum c)
{
    const GLboolean *p;
    seam_enter();
    sg.is_enabled++;
    p = cli_slot(c);
    return p != NULL ? *p : (GLboolean) GL_FALSE;
}

/* The two client-array parameters answer; every other one still answers 0, as
 * it did before. */
void APIENTRY glGetIntegerv(GLenum pname, GLint *params)
{
    GLint v = 0;
    seam_enter();
    sg.get_integerv++;
    if (pname == GL_MAX_TEXTURE_UNITS)
        v = (GLint) (sg.units_reported > 0 ? sg.units_reported : (long) SEAMGL_UNITS);
    else if (pname == GL_MAX_TEXTURE_COORDS)
        v = (GLint) (sg.coords_reported > 0 ? sg.coords_reported
                                            : (long) SEAMGL_UNITS);
    else if (pname == GL_CLIENT_ACTIVE_TEXTURE)
        v = sg.cli.unit;
    if (params != NULL) *params = v;
}

void APIENTRY glGetFloatv(GLenum pname, GLfloat *params)
{
    seam_enter();
    (void) pname;
    if (params != NULL) *params = 0.0f;
}

/* ---- the inert remainder, generated ------------------------------------- */

/* glFinish IS OBSERVED HERE AND INERT THERE, AND THE RENAME IS WHY BOTH ARE
 * TRUE AT ONCE. The generated include carries one inert definition per GL 1.1
 * entry point this fixture did not need to observe, and glFinish was one of
 * them: sl_gfx_dl.c calls it on paths this fixture does not drive, so the link
 * needed a symbol and nothing needed its order. The post-reset boundary control
 * (SL_BG_TF_POST_RESET) issues one glFinish at a point whose whole content is
 * WHERE it sits, so the order is now exactly what has to be recorded.
 *
 * The include is GENERATED (tools/profiling/native/genseamstubs.py) and is not
 * edited by hand, so its definition is renamed out of the way for the length of
 * the include instead and an observed definition is written below. The inert
 * body still exists, still counts its call through seam_enter and is simply
 * never reached under that name; a future regeneration of the include is
 * unaffected, because nothing in it changed. */
#define glFinish seam_inert_glFinish
#include "sl_gfx_seamgl_inert.inc"
#undef glFinish

/* glFinish, OBSERVED: counted, and its place in the call order kept.
 *
 * WHAT IT CAN AND CANNOT SAY. This shim has no pipeline, so a synchronisation
 * point has nothing to wait on and the call does nothing but record itself.
 * `finish_calls` says how many were ISSUED and `seq_last_finish` says where the
 * last one sat relative to the sequence numbers the draws, the feedback end and
 * the diagnostic's read already carry - which is what makes "it is after the
 * prepass's restoration and before the raster draw" a comparison of numbers.
 * Whether a driver's glFinish would change anything about any pixel is outside
 * this process entirely and is claimed nowhere.
 *
 * IT IS NOT A READ-BACK: it returns nothing and writes nothing through a
 * pointer, so this shim's standing statement about read-back entry points is
 * untouched. The inert definition it replaces was already a symbol here, so the
 * link's own contract is unchanged as well. */
void APIENTRY glFinish(void)
{
    seam_enter();
    sg.finish_calls++;
    sg.seq_last_finish = ++sg.seq;
}

/* glFlush, A SYMBOL THIS LINK NEEDS AND THIS FIXTURE NEVER REACHES
 * (SL_BG_TF_AUX_SYNC).
 *
 * WHY IT IS HERE AT ALL. The opt-in fence synchronisation mode flushes the
 * PRODUCER'S context at each of its two cross-context handoffs, because OpenGL
 * 4.5 core section 4.1.2 is explicit that a consumer's flush bit does not reach
 * a producer's command stream. glFlush is a GL 1.0 core export the extension
 * loader answers NULL for, so the module calls it directly - which makes it a
 * symbol the link of ANY fixture containing that translation unit has to
 * provide, this one included, whether or not the fixture drives the mode.
 *
 * WHY IT IS INERT RATHER THAN OBSERVED. This fixture exists to measure the SEAM
 * between the prepass and the raster draw on ONE context; it has no second
 * context, never sets the fence selector, and so cannot reach either call site.
 * An observed definition would add a field to this shim's state for a call that
 * is never made here. It calls seam_enter so the total-call accounting stays
 * exact if a future case ever does reach it, and it takes no sequence number,
 * so the shape digest this fixture freezes cannot move because of it.
 *
 * IT IS NOT IN sl_gl_proc's TABLE, and that is unchanged: a resolve of glFlush
 * answers NULL here exactly as a resolve of glFinish or glGetError does, for the
 * reason recorded in the header - a core name routed through the extension
 * loader is refused on a perfectly capable driver. */
void APIENTRY glFlush(void)
{
    seam_enter();
}

/* ---- the resolved entry points ------------------------------------------ */

static GLuint shader_seq, program_seq;

static GLuint APIENTRY seam_CreateShader(GLenum t) { seam_enter(); (void) t; return ++shader_seq + 0x1000u; }
static void APIENTRY seam_ShaderSource(GLuint s, GLsizei n, const char **src, const GLint *l)
{ seam_enter(); (void) s; (void) n; (void) src; (void) l; }
static void APIENTRY seam_CompileShader(GLuint s) { seam_enter(); (void) s; }
static void APIENTRY seam_GetShaderiv(GLuint s, GLenum p, GLint *o)
{ seam_enter(); (void) s; (void) p; if (o != NULL) *o = 1; }
static void APIENTRY seam_GetShaderInfoLog(GLuint s, GLsizei c, GLsizei *n, char *g)
{ seam_enter(); (void) s; if (g != NULL && c > 0) g[0] = '\0'; if (n != NULL) *n = 0; }
static GLuint APIENTRY seam_CreateProgram(void) { seam_enter(); return ++program_seq + 0x2000u; }
static void APIENTRY seam_AttachShader(GLuint p, GLuint s) { seam_enter(); (void) p; (void) s; }
static void APIENTRY seam_LinkProgram(GLuint p) { seam_enter(); (void) p; }
static void APIENTRY seam_GetProgramiv(GLuint p, GLenum n, GLint *o)
{ seam_enter(); (void) p; (void) n; if (o != NULL) *o = 1; }
static void APIENTRY seam_GetProgramInfoLog(GLuint p, GLsizei c, GLsizei *n, char *g)
{ seam_enter(); (void) p; if (g != NULL && c > 0) g[0] = '\0'; if (n != NULL) *n = 0; }

/* THE NEUTRAL-PROGRAM CONTROL'S ONE DELETE (SL_BG_TF_NEUTRAL_PROG). Resolved
 * rather than a symbol, so the note at the top of this file is unchanged for
 * every other delete; the modelled `current` is deliberately left alone, which
 * is GL's own behaviour for deleting a program that is in use. */
static void APIENTRY seam_DeleteProgram(GLuint p)
{
    seam_enter();
    sg.delete_program++;
    sg.last_deleted_program = p;
}

/* AND ITS SHADER'S RELEASE, on the same terms. Resolved rather than a symbol,
 * so the note at the top of this file is unchanged for every other delete. No
 * attachment is modelled here, so a delete of a shader a program still holds is
 * recorded as the ISSUED call it is; what a driver then frees, and when, is
 * outside what this shim speaks for. */
static void APIENTRY seam_DeleteShader(GLuint s)
{
    seam_enter();
    sg.delete_shader++;
    sg.last_deleted_shader = s;
}

static void APIENTRY seam_UseProgram(GLuint p)
{
    seam_enter();
    sg.use_program++;
    /* THE API'S OWN RULE, MODELLED AND NOT RELAXED. glUseProgram inside a
     * begin/end scope is invalid, so a change made while a feedback scope is
     * open is recorded as a violation rather than quietly applied. Counted
     * BEFORE the state moves, so the condition read is the one at the call.
     * SL_BG_TF_EMPTY_STATE's one program-0 row is also a feedback-INACTIVE row,
     * and this is where that stays a measurement at the real caller rather than
     * a property of the table the row came from. */
    if (sg.tf_active) sg.v_program_change_in_tf++;
    sg.current = p;
}

/* Every uniform resolves, at a location that is its own index. */
static GLint APIENTRY seam_GetUniformLocation(GLuint p, const char *n)
{
    static GLint next;
    seam_enter();
    (void) p; (void) n;
    return next++;
}

static void APIENTRY seam_Uniform1f(GLint l, GLfloat a) { seam_enter(); (void) l; (void) a; }
static void APIENTRY seam_Uniform2f(GLint l, GLfloat a, GLfloat b)
{ seam_enter(); (void) l; (void) a; (void) b; }
static void APIENTRY seam_Uniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c)
{ seam_enter(); (void) l; (void) a; (void) b; (void) c; }
static void APIENTRY seam_Uniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d)
{ seam_enter(); (void) l; (void) a; (void) b; (void) c; (void) d; }
static void APIENTRY seam_Uniform1i(GLint l, GLint a) { seam_enter(); (void) l; (void) a; }
static void APIENTRY seam_Uniform1fv(GLint l, GLsizei n, const GLfloat *v)
{ seam_enter(); (void) l; (void) n; (void) v; }
static void APIENTRY seam_UniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v)
{ seam_enter(); (void) l; (void) n; (void) t; (void) v; }
static void APIENTRY seam_TransformFeedbackVaryings(GLuint p, GLsizei n,
                                                    const char *const *v, GLenum m)
{ seam_enter(); (void) p; (void) n; (void) v; (void) m; }

static void APIENTRY seam_GenBuffers(GLsizei n, GLuint *out)
{
    GLsizei i;
    seam_enter();
    sg.gen_buffers++;
    if (out == NULL) return;
    for (i = 0; i < n; i++) {
        if (sg.gen_buffers_zero) { out[i] = 0u; continue; }
        out[i] = ++buffer_seq + 0x4000u;
        if (nbufs < SEAMGL_BUFFERS) {
            bufs[nbufs].name = out[i];
            bufs[nbufs].bytes = 0L;
            nbufs++;
        }
        sg.generated++;
    }
}

static void APIENTRY seam_BindBuffer(GLenum target, GLuint name)
{
    seam_enter();
    if (name != 0u && buf_index(name) < 0) sg.v_unknown_buffer++;
    if (target == GL_ARRAY_BUFFER)              sg.arr_buf = name;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) sg.elem_buf = name;
}

/* THE REQUEST AND THE STORAGE ARE TWO DIFFERENT NUMBERS. See the note in
 * sl_gfx_bgtf_testgl.c: glBufferData is void, so "the call returned" is not
 * evidence that storage exists, and the installed size is chosen here
 * independently of the request. `alloc_short_name` narrows the injection to one
 * buffer, because the pass's own vertex and index buffers come through this
 * same entry point and failing those would refuse the walk outright. */
static void APIENTRY seam_BufferData(GLenum target, ptrdiff_t size,
                                     const void *data, GLenum usage)
{
    GLuint name;
    int i, initial;
    long installed;
    seam_enter();
    sg.buffer_data++;
    (void) data; (void) usage;
    name = (target == GL_ARRAY_BUFFER) ? sg.arr_buf : sg.elem_buf;
    i = buf_index(name);
    if (i < 0) return;
    initial = bufs[i].bytes == 0L;
    installed = (long) size;
    {
        int exempt = 0, k;
        for (k = 0; k < (int) (sizeof sg.alloc_exempt / sizeof sg.alloc_exempt[0]); k++)
            if (sg.alloc_exempt[k] != 0u && sg.alloc_exempt[k] == name) exempt = 1;
        if (!exempt && (sg.alloc_short_name == 0u || sg.alloc_short_name == name)
            && (initial ? sg.alloc_short_initial : sg.alloc_short_growth)) {
            if (sg.alloc_short_skip > 0) sg.alloc_short_skip--;  /* let this one through */
            else                         installed = sg.alloc_bytes;
        }
    }
    bufs[i].bytes = installed;
}

static void APIENTRY seam_BufferSubData(GLenum target, ptrdiff_t off,
                                        ptrdiff_t size, const void *data)
{ seam_enter(); (void) target; (void) off; (void) size; (void) data; }

/* ALLOCATION ACCEPTANCE, asked once per allocation. A query that writes nothing
 * leaves the caller its own initial value, which is the shape of a real GL
 * error and a different fact from storage that is simply short. */
static void APIENTRY seam_GetBufferParameteriv(GLenum target, GLenum pname, GLint *out)
{
    GLuint name;
    long have;
    seam_enter();
    sg.get_buffer_parameteriv++;
    name = (target == GL_ARRAY_BUFFER) ? sg.arr_buf : sg.elem_buf;
    have = sl_seamgl_buffer_bytes(name);
    if (have < 0) have = 0;
    (void) pname;
#ifdef BGTFSEAM_MUT_ALLOCOK
    /* THE DRIVER THAT LIES: whatever was asked for is reported as allocated, so
     * no short allocation can ever be seen to fail. */
    have = 0x7FFFFFFFL;
#endif
    if (sg.getbufparam_fail) return;
    if (out != NULL) *out = (GLint) have;
}

static void APIENTRY seam_BindBufferBase(GLenum target, GLuint index, GLuint name)
{
    seam_enter();
    if (name != 0u && buf_index(name) < 0) sg.v_unknown_buffer++;
    if (target == GL_TRANSFORM_FEEDBACK_BUFFER && index == 0u) sg.tf_buf = name;
}

static void APIENTRY seam_BeginTransformFeedback(GLenum mode)
{ seam_enter(); sg.begin_tf++; sg.tf_active = 1; (void) mode; }

static void APIENTRY seam_EndTransformFeedback(void)
{
    seam_enter();
    sg.end_tf++;
    sg.tf_active = 0;
    sg.seq_last_end_tf = ++sg.seq;
}

/* THE DIAGNOSTIC'S READBACK, resolvable only when a test asks. The function is
 * seam_* rather than glGetBufferSubData, so nothing here becomes a symbol the
 * product could reach by a direct call.
 *
 * The destination is filled with a deterministic pattern rather than a modelled
 * pool: this fixture's claims are about WHERE the read happens, how many times,
 * and what it leaves behind, and a pattern is enough for a record to exist. The
 * byte-fidelity claims belong to tools/profiling/native/sl_gfx_bgtf_testgl.c,
 * whose buffers carry contents.
 *
 * WHAT IS SAMPLED HERE IS THE POINT. The array binding the read took, the
 * indexed transform-feedback binding and the position pointer are all recorded
 * AT THE READ, so a test can assert the read left each of them as the draw
 * afterwards needed it - which no counter and no end-of-walk sample can say. */
static void APIENTRY seam_GetBufferSubData(GLenum target, ptrdiff_t off,
                                           ptrdiff_t size, void *out)
{
    seam_enter();
    sg.get_buffer_sub_data++;
    sg.get_buffer_sub_bytes += (long) size;
    sg.seq_last_subdata = ++sg.seq;
    if (sg.seq_first_subdata == 0L) sg.seq_first_subdata = sg.seq_last_subdata;
    sg.subdata_arr_buf_at = sg.arr_buf;
    sg.subdata_tf_buf_at = sg.tf_buf;
    sg.subdata_pos_at = sg.pos;
    /* A READ INSIDE AN OPEN SCOPE would be reading a pool still being written;
     * the seam's own watchdog for it is the discard counter's neighbour. */
    if (sg.tf_active) sg.v_read_inside_tf++;
    if (out != NULL && size > 0) {
        unsigned char *p = (unsigned char *) out;
        ptrdiff_t i;
        for (i = 0; i < size; i++)
            p[i] = (unsigned char) ((unsigned) (off + i) * 31u + 7u);
    }
    (void) target;
}

static void APIENTRY seam_ActiveTexture(GLenum t) { seam_enter(); (void) t; }
/* THE PER-UNIT SELECTOR, now tracked rather than discarded: the unit it leaves
 * selected is what decides which coordinate array every later client call
 * addresses, so a control that restored the wrong unit is only visible if this
 * is modelled. */
static void APIENTRY seam_ClientActiveTexture(GLenum t)
{
    seam_enter();
    sg.client_active_texture++;
    sg.cli.unit = (GLint) t;
    (void) cli_unit_index();            /* range-check it, and count a miss */
}
static void APIENTRY seam_MultiTexCoord2f(GLenum t, GLfloat s, GLfloat v)
{ seam_enter(); (void) t; (void) s; (void) v; }
static void APIENTRY seam_FogCoordf(GLfloat c) { seam_enter(); (void) c; }
static void APIENTRY seam_FogCoordPointer(GLenum t, GLsizei s, const void *p)
{ seam_enter(); (void) t; (void) s; (void) p; }

void *sl_gl_proc(const char *name)
{
    static const struct { const char *name; void *fn; } table[] = {
        { "glCreateShader",              (void *) seam_CreateShader },
        { "glShaderSource",              (void *) seam_ShaderSource },
        { "glCompileShader",             (void *) seam_CompileShader },
        { "glGetShaderiv",               (void *) seam_GetShaderiv },
        { "glGetShaderInfoLog",          (void *) seam_GetShaderInfoLog },
        { "glCreateProgram",             (void *) seam_CreateProgram },
        { "glAttachShader",              (void *) seam_AttachShader },
        { "glLinkProgram",               (void *) seam_LinkProgram },
        { "glGetProgramiv",              (void *) seam_GetProgramiv },
        { "glGetProgramInfoLog",         (void *) seam_GetProgramInfoLog },
        { "glUseProgram",                (void *) seam_UseProgram },
        /* The neutral-program control's one delete. Unlike the other fixture's,
         * it is unconditional here: this shim models a capable context and the
         * refusal-on-a-missing-delete case belongs to the module fixture, which
         * can gate the resolve. */
        { "glDeleteProgram",             (void *) seam_DeleteProgram },
        { "glDeleteShader",              (void *) seam_DeleteShader },
        /* The auxiliary-context control's one query, unconditional here for the
         * same reason the two deletes are: this shim models a capable platform,
         * and the refuse-on-a-missing-entry-point and refuse-on-FALSE cases both
         * belong to the module fixture, which can gate them. */
        { "glIsProgram",                 (void *) seam_IsProgram },
        { "glGetUniformLocation",        (void *) seam_GetUniformLocation },
        { "glUniform1f",                 (void *) seam_Uniform1f },
        { "glUniform2f",                 (void *) seam_Uniform2f },
        { "glUniform3f",                 (void *) seam_Uniform3f },
        { "glUniform4f",                 (void *) seam_Uniform4f },
        { "glUniform1i",                 (void *) seam_Uniform1i },
        { "glUniform1fv",                (void *) seam_Uniform1fv },
        { "glUniformMatrix4fv",          (void *) seam_UniformMatrix4fv },
        { "glTransformFeedbackVaryings", (void *) seam_TransformFeedbackVaryings },
        { "glBindBufferBase",            (void *) seam_BindBufferBase },
        { "glBeginTransformFeedback",    (void *) seam_BeginTransformFeedback },
        { "glEndTransformFeedback",      (void *) seam_EndTransformFeedback },
        { "glGenBuffers",                (void *) seam_GenBuffers },
        { "glBindBuffer",                (void *) seam_BindBuffer },
        { "glBufferData",                (void *) seam_BufferData },
        { "glBufferSubData",             (void *) seam_BufferSubData },
        { "glGetBufferParameteriv",      (void *) seam_GetBufferParameteriv },
        /* glGetError IS NOT HERE, AND THAT IS THE CONTRACT. It is a GL 1.0 core
         * export and both sl_gfx_dl.c and the buffer diagnostic call it
         * directly, which is why it exists as a real symbol in this file. A
         * resolver entry for it would be dead, and would quietly permit a
         * regression back to the extension-loader form: on a driver whose
         * wglGetProcAddress answers only for extensions that resolve hands back
         * NULL, and the error state becomes permanently unobservable. */
        { "glActiveTexture",             (void *) seam_ActiveTexture },
        { "glClientActiveTexture",       (void *) seam_ClientActiveTexture },
        { "glMultiTexCoord2f",           (void *) seam_MultiTexCoord2f },
        { "glFogCoordf",                 (void *) seam_FogCoordf },
        { "glFogCoordPointer",           (void *) seam_FogCoordPointer }
    };
    size_t i;
    sg.proc_calls++;
    /* AND WHICH CONTEXT THE RESOLVE WAS MADE ON. An entry point is only valid on
     * the context it was resolved on, so a control that calls on the auxiliary
     * context has to resolve there; this fixture models one GL state rather than
     * one per context, so what it can state is WHERE the resolve happened, and
     * the per-pointer identity claim belongs to the module fixture. */
    if (sg.ctx_cur > 0) sg.proc_calls_on_aux++;
    if (name == NULL) return NULL;
    if (sg.withhold_proc != NULL && strcmp(name, sg.withhold_proc) == 0) return NULL;
    /* THE DIAGNOSTIC'S READ, and only when a test asks. Kept out of the table
     * so an ordinary run of this fixture answers NULL for it, which is what
     * makes "nothing is resolved while the diagnostic is off" observable from
     * this side rather than asserted from the module's. */
    if (sg.provide_readback && strcmp(name, "glGetBufferSubData") == 0)
        return (void *) seam_GetBufferSubData;
    for (i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(name, table[i].name) == 0) return table[i].fn;
    (void) seam_procs;
    return NULL;
}
