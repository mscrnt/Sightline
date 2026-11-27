/**
 * THE SKYDOME PASS'S INDEXED GPU SUBMISSION. See sl_gfx_skybatch.h for the
 * contract, what is preserved and what is not claimed.
 *
 * r474: an opt-in float-colour carrier for the served cloud draws
 * (SL_SKY_BATCH_FLOAT_COLOR=div|prod, read only once SL_SKY_BATCH=1 has
 * armed the path); see the header. In byte mode this module's upload is
 * the r405 module's, call for call.
 *
 * ---- GL STATE IS THE CORRECTNESS BOUNDARY HERE ---------------------------
 *
 * This module runs INSIDE a display-list walk. sky_draw is emitted on the
 * frame's first world triangle, so whatever client-array state the model, item
 * and background paths of sl_gfx_dl.c last left is inherited - and those paths
 * do set vertex, colour, texture-coordinate and fog-coordinate arrays, and the
 * #88 background path binds buffer objects. Nothing may be assumed to be zero,
 * off, or on unit 0 on entry.
 *
 * Two kinds of damage are possible and both are guarded:
 *
 *   OUTWARD - this module leaves an array, enable, pointer or binding changed,
 *   and the room whose list it interrupted draws through it. Guarded by saving
 *   every piece of state this module writes before writing any of it, and
 *   restoring all of it once per pass.
 *
 *   INWARD - an array this module does not set is still ENABLED, so
 *   glDrawElements dereferences a stale pointer belonging to another pass.
 *   An enabled normal, index, edge-flag, secondary-colour or fog-coordinate
 *   array, or a texture-coordinate array on a unit other than 0, is read by
 *   the draw whether or not this module wanted it. Guarded by explicitly
 *   disabling every such array for the duration of the pass and restoring each
 *   enable afterwards.
 *
 * WHY NOT glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT). It would cover most
 * of this, and the premise that it omits the buffer bindings turns out NOT to
 * hold: in the OpenGL 2.1 specification (December 1, 2006) table 6.2
 * "Attribute groups" maps the CLIENT stack's `vertex-array` group to
 * CLIENT_VERTEX_ARRAY_BIT, and table 6.8 "Vertex Array Data (cont.)" lists
 * ARRAY_BUFFER_BINDING, ELEMENT_ARRAY_BUFFER_BINDING and the per-array
 * VERTEX / NORMAL / COLOR / INDEX / TEXTURE_COORD / EDGE_FLAG /
 * SECONDARY_COLOR / FOG_COORD / VERTEX_ATTRIB _ARRAY_BUFFER_BINDING rows with
 * `vertex-array` in their attribute column and section 2.9 as their reference,
 * so the bindings ARE pushed. ARB_vertex_buffer_object (ARB extension 28,
 * revision 0.99.6) resolves the same question in its Issues section, under
 * "Does the buffer binding state push/pop?": resolved YES, on the client with
 * the rest of the vertex array state, for both the user-controlled binding
 * point and the per-array bindings.
 *
 * The explicit save is used anyway, for three reasons that survive that
 * finding:
 *
 *   1. The attribute stack is a budget, not a free operation: section 6.1.15
 *      of the same specification requires a depth of only 16, and raises
 *      STACK OVERFLOW past MAX_CLIENT_ATTRIB_STACK_DEPTH. A pass that runs
 *      inside an interpreter walk cannot know how much of that budget its
 *      caller is already holding, and a failed push followed by a successful
 *      pop corrupts someone else's state.
 *   2. The behaviour under the LATER compatibility profiles, where client
 *      vertex array state lives in a vertex array object, was NOT verified -
 *      the 4.x compatibility specification could not be read for this round
 *      (see the round's open limits). The explicit save does not depend on the
 *      answer.
 *   3. The save is also the only form in which what is restored is auditable:
 *      the offline test drives nonzero foreign bindings, a client unit other
 *      than 0 and arbitrary pointers through the pass and compares the whole
 *      recorded state before and after. A push/pop pair would be verifying the
 *      mock's own stack.
 *
 * glDrawElements leaves the current value of every attribute whose array was
 * enabled UNSPECIFIED - "if GL_COLOR_ARRAY is enabled, the value of the
 * current color is undefined after glDrawElements executes" (the OpenGL 2.1
 * reference page for glDrawElements). The immediate loop left the current
 * colour and texture coordinate at the LAST index's values, and sky_draw's
 * D-046 horizon fill runs after the mesh loop, so those two are restored
 * explicitly from the last submitted index rather than inherited from an
 * undefined state.
 *
 * NOTHING IS READ BACK FROM THE GPU. There is no glGetError, no glFinish, no
 * buffer map and no pixel read anywhere in this module: a buffer name is
 * trusted when it is nonzero, the same rule the #88 background path uses, and
 * a driver that genuinely fails behind that is a defect this module cannot
 * observe without a synchronisation point it is not allowed to introduce.
 */
#ifndef __sgi

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef SL_SKYBATCH_TEST
/* Offline mock build (-DSL_SKYBATCH_TEST). The stub supplies the GL types and
 * tokens, the entry points, the proc-address resolver and the environment -
 * all SYNTHETIC, and with no link-time dependency on opengl32 or SDL.
 * Everything below this include is the shipped logic, compiled unchanged.
 * The sl_gfx_profile_testgl.h idiom. */
#include "sl_gfx_skybatch_testgl.h"
#else
#ifdef _WIN32
/* windows.h must precede <GL/gl.h>, which takes APIENTRY and WINGDIAPI from
 * it - the same include order the rest of src/gfx uses. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>
#endif

#include "sl_gfx_worldasset.h"
#include "sl_gfx_skybatch.h"
/* The ONE player-facing performance selection and the policy that resolves it
 * into this module's two questions - may the path run, and with which carrier.
 * Host-clean (plain ints), so the offline build links the real policy and the
 * real settings store rather than a mock of either. */
#include "../platform/sl_perf.h"

/* ---- the post-1.1 tokens and entry points -------------------------------
 *
 * B-075's rule, and the reason this block exists at all: Win32's OpenGL
 * IMPORT SURFACE is frozen at 1.1 - opengl32.dll exports nothing newer - so
 * every entry point past it is resolved through the loader. The client-array
 * commands themselves (glDrawElements, glVertexPointer, glColorPointer,
 * glTexCoordPointer, glEnableClientState, glGetPointerv) are all GL 1.1 and
 * are declared by <GL/gl.h> on this toolchain, so only the GL 1.3 client unit
 * selector and the GL 1.5 buffer commands need resolving.
 *
 * APIENTRY IS LOAD-BEARING, and its absence was B-063: these entry points are
 * __stdcall, the CALLEE pops the arguments, and a bare `typedef void (*F)(...)`
 * is cdecl. Anything added here MUST carry APIENTRY.
 *
 * The enum values are glext.h's, read from the header in this same MSYS2 tree
 * rather than recalled - mingw32/include/GL/glext.h:477-480 (the buffer
 * targets and their bindings), :488 and :491 (the usage hints), :499, :501 and
 * :503 (the per-array bindings), :184-185 (the client unit and the unit
 * count), :125 (GL_TEXTURE0), :354 (the secondary colour array) and :514 (the
 * fog coordinate array). The ptrdiff_t size parameter is the #88 background
 * path's choice in sl_gfx_dl.c and matches khronos_ssize_t on this i686
 * target. */
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                       0x8892
#define GL_ELEMENT_ARRAY_BUFFER               0x8893
#define GL_ARRAY_BUFFER_BINDING               0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING       0x8895
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW                        0x88E0
#define GL_STATIC_DRAW                        0x88E4
#endif
#ifndef GL_VERTEX_ARRAY_BUFFER_BINDING
#define GL_VERTEX_ARRAY_BUFFER_BINDING        0x8896
#define GL_COLOR_ARRAY_BUFFER_BINDING         0x8898
#define GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING 0x889A
#endif
#ifndef GL_CLIENT_ACTIVE_TEXTURE
#define GL_CLIENT_ACTIVE_TEXTURE              0x84E1
#define GL_MAX_TEXTURE_UNITS                  0x84E2
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                           0x84C0
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY
#define GL_SECONDARY_COLOR_ARRAY              0x845E
#endif
#ifndef GL_FOG_COORD_ARRAY
#define GL_FOG_COORD_ARRAY                    0x8457
#endif

typedef void (APIENTRY *SKB_PFN_GENBUFFERS)(GLsizei, GLuint *);
typedef void (APIENTRY *SKB_PFN_DELETEBUFFERS)(GLsizei, const GLuint *);
typedef void (APIENTRY *SKB_PFN_BINDBUFFER)(GLenum, GLuint);
typedef void (APIENTRY *SKB_PFN_BUFFERDATA)(GLenum, ptrdiff_t, const GLvoid *, GLenum);
typedef void (APIENTRY *SKB_PFN_CLIENTACTIVETEXTURE)(GLenum);

static SKB_PFN_GENBUFFERS         p_GenBuffers;
static SKB_PFN_DELETEBUFFERS      p_DeleteBuffers;
static SKB_PFN_BINDBUFFER         p_BindBuffer;
static SKB_PFN_BUFFERDATA         p_BufferData;
static SKB_PFN_CLIENTACTIVETEXTURE p_ClientActiveTexture;

#ifndef SL_SKYBATCH_TEST
extern void *sl_gl_proc(const char *name);     /* the backend's resolver */
#endif

/* THE UNIT SWEEP'S CEILING. Every texture unit's coordinate array has to be
 * accounted for, because an enabled array on any unit is read by the draw. A
 * fixed ceiling keeps the saved state bounded; a context reporting more units
 * than this REFUSES the batch path rather than sweeping part of the range and
 * leaving the rest to feed the draw. GL_MAX_TEXTURE_UNITS is the fixed-function
 * limit and is 8 or fewer on the compatibility profiles this renderer runs on,
 * so 16 is headroom, not a guess that might bind. */
#define SKB_UNITS 16

/* ---- the saved client-array state --------------------------------------- */

struct skb_saved {
    int       held;                    /* this struct describes live GL state */
    GLint     client_unit;             /* GL_CLIENT_ACTIVE_TEXTURE            */
    GLint     units;                   /* the sweep's width, <= SKB_UNITS     */
    GLint     arr_buf, elem_buf;       /* the two user-controlled bindings    */

    GLboolean v_on;  GLint v_size, v_type, v_stride, v_buf;  GLvoid *v_ptr;
    GLboolean c_on;  GLint c_size, c_type, c_stride, c_buf;  GLvoid *c_ptr;
    GLboolean t_on[SKB_UNITS];
    GLint     t_size[SKB_UNITS], t_type[SKB_UNITS], t_stride[SKB_UNITS];
    GLint     t_buf[SKB_UNITS];
    GLvoid   *t_ptr[SKB_UNITS];

    /* enables only: this module never sets these arrays' pointers, and
     * disabling an array does not disturb the pointer it holds. */
    GLboolean n_on, i_on, e_on, s_on, f_on;
};

static struct skb_saved g_save;

/* ---- the per-asset topology cache and the per-context buffers ------------ */

static int       g_state;              /* 0 untried, 1 ready, -1 unavailable  */
static GLuint    g_vb, g_ib;           /* the attribute and element buffers   */
static const struct sl_wa_asset *g_asset;   /* the item the topology describes */
static const float *g_clip;            /* the pass's clip array               */

static unsigned *g_uniq;               /* per mesh, the referenced vertex ids */
static unsigned *g_lastslot;           /* per mesh, the slot of its LAST index */
/* The capacities the writing pass may not exceed, and what it actually wrote.
 * Held as state so each store is bounded against a stored number rather than
 * against the reasoning that produced it - and so the offline suite can read
 * both back and compare them. */
static unsigned long g_uniq_cap, g_uniq_used, g_idx_cap, g_idx_used;
static size_t        g_elem_bytes;     /* the element upload's checked size   */
static unsigned *g_uoff;               /* mesh_count + 1 offsets into g_uniq  */
static unsigned *g_ioff;               /* mesh_count + 1 offsets, in elements */
static unsigned *g_stamp;              /* vertex_count: which mesh owns a slot*/
static unsigned *g_slot;               /* vertex_count: that mesh's slot       */
static struct sl_skybatch_vtx *g_stage;     /* g_umax prepared records        */
static unsigned  g_umax;               /* widest mesh, in unique vertices     */
static int       g_idx16;              /* the element type is UNSIGNED_SHORT  */
static int       g_pass;               /* a pass is open                      */
/* THE PASS'S LATCHED CARRIER. sl_skybatch_pass_begin asks the policy ONCE and
 * writes it here; every draw of that pass reads this and nothing else. The
 * selection can change at any moment (a menu press), and a pass that uploaded
 * 36-byte records for its first cloud mesh and 48-byte records for its second
 * would declare one stride and feed the draw another - so the question is
 * asked at the boundary and answered from the latch inside it. Set to BYTE
 * when no pass is open, which is also what every lifecycle path leaves. */
static int       g_pass_fmode;
static struct sl_skybatch_vtx g_last;  /* the last index's prepared record    */
static int       g_have_last;

/* Counters. Bounded: each is a count, and nothing here prints per mesh or per
 * frame - see skb_note for the cadence. */
static unsigned long g_c_passes, g_c_draws, g_c_idx, g_c_uniq, g_c_refused;
/* r398, ported from r386 :266-271 WITHOUT its array counters - this candidate
 * has no array path, so there is nothing for them to count. g_c_mesh_refused
 * is separate from g_c_refused so a mesh-level refusal is not reported as a
 * pass-level one; the two r380 fixes in sl_skybatch_mesh are what make every
 * active-pass mesh refusal reach it. */
static unsigned long g_c_mesh_refused;
/* r398: DELIBERATE DECLINES, counted on their own. A declined mesh is one this
 * candidate does not batch by design - a plain (non-cloud) material - and
 * which the caller then draws through its own immediate loop with the pass
 * still open. It is neither a refusal (nothing failed and nothing fell back)
 * nor an indexed draw (nothing was submitted here), so it is added to neither
 * family: g_c_declined counts declined nonempty meshes and g_c_dec_vtx the
 * index count the immediate loop then submits for them. */
static unsigned long g_c_declined, g_c_dec_vtx;
static unsigned long g_c_items;
static int           g_said_off, g_said_armed;

/* r386: the terminal snapshot's registration state.
 *
 * PROCESS LIFETIME, BY CONSTRUCTION. Neither flag is cleared anywhere:
 * not by sl_skybatch_context_invalidate, which clears g_state,
 * g_said_off and g_said_armed; not by sl_skybatch_shutdown, which
 * clears g_state; not by skb_free_cpu, which releases the CPU side;
 * and not by an asset replacement, which routes through skb_free_cpu.
 * So one process makes exactly ONE registration attempt however many
 * times the context is dropped, the asset is replaced, or the module
 * is shut down and re-armed.
 *
 * WHY NOT g_said_armed. sl_skybatch_context_invalidate sets it back to
 * 0, which is correct for an arming notice - the next context should
 * say it armed again - but it would make a registration flag retry on
 * every re-arm and print a second diagnostic. The two concerns need
 * two flags, so these are separate from the line above.
 *
 * g_snap_ok records a successful registration honestly and is
 * deliberately not printed: a handler that runs at all is itself the
 * proof that registration succeeded, and the failure case already has
 * its own distinct one-shot line. */
static int           g_snap_tried;     /* an attempt has been made    */
static int           g_snap_ok;        /* atexit accepted the handler */

/* ---- the attribute, the one copy of the immediate loop's expressions -----
 *
 * TRANSCRIBED FROM sky_draw's per-index body, operation for operation, and
 * pinned against a verbatim transcription of that body by the offline test
 * (tests/test_skybatch.c, case "reference"). The two live in different
 * translation units deliberately: the immediate path in sl_gfx_dl.c is left
 * byte for byte as it shipped, so that the default-off path carries no change
 * at all, and the harness is what keeps this copy honest.
 *
 * The casts matter. `(GLubyte) (expr)` truncates toward zero in C, and the
 * `+ 0.5f` inside the parentheses is what makes that truncation a round to
 * nearest - so the division by 255.0f must stay INSIDE the sum and the sum
 * INSIDE the cast, in that order, or a byte moves by one. */
#ifdef SL_SKYBATCH_TEST
unsigned long sl_skybatch_test_attr_calls;
#endif

void sl_skybatch_attr(const struct sl_wa_vertex *v, const float clip[4],
                      const struct sl_skybatch_shade *s,
                      struct sl_skybatch_vtx *out)
{
#ifdef SL_SKYBATCH_TEST
    sl_skybatch_test_attr_calls++;     /* the offline call-count probe only */
#endif
    out->clip[0] = clip[0];
    out->clip[1] = clip[1];
    out->clip[2] = clip[2];
    out->clip[3] = clip[3];

    if (s->atm) {
        /* D-030: the game's cloud shade along this vertex's direction from the
         * dome's anchor (the eye), times the bundle's own vertex colour. */
        float dd[3], sh[3];
        dd[0] = v->x - s->cen[0];
        dd[1] = v->y - s->cen[1];
        dd[2] = v->z - s->cen[2];
        if (s->scr)                      /* D-046: at its place on the screen */
            sl_wa_sky_shade_frac(s->fog, s->cloud,
                                 clip[3] > 1.0e-6f
                                 ? sl_wa_sky_screen_frac(s->scr_rows, s->scr_fill,
                                                         clip[0] / clip[3],
                                                         clip[1] / clip[3])
                                 : 1.0, sh);
        else
            sl_wa_sky_shade(s->fog, s->cloud, dd, sh);
        out->col[0] = (GLubyte) (sh[0] * (float) v->r / 255.0f + 0.5f);
        out->col[1] = (GLubyte) (sh[1] * (float) v->g / 255.0f + 0.5f);
        out->col[2] = (GLubyte) (sh[2] * (float) v->b / 255.0f + 0.5f);
        out->col[3] = v->a;
    } else {
        out->col[0] = v->r;
        out->col[1] = v->g;
        out->col[2] = v->b;
        out->col[3] = v->a;
    }

    if (s->cloud_plane) {
        float d[3], q[3];
        d[0] = v->x - s->cen[0];
        d[1] = v->y - s->cen[1];
        d[2] = v->z - s->cen[2];
        sl_wa_cloud_coord(d, s->cloud_eye, s->cloud_height, s->cloud_phase,
                          s->cloud_w, s->cloud_h, q);
        /* the immediate path's glTexCoord4f(q[0], q[1], 0.0f, q[2]) */
        out->st[0] = q[0];
        out->st[1] = q[1];
        out->st[2] = 0.0f;
        out->st[3] = q[2];
    } else {
        /* the immediate path's glTexCoord2f(v->u, v->v): a two-component
         * coordinate leaves the current coordinate at (s, t, 0, 1) */
        out->st[0] = v->u;
        out->st[1] = v->v;
        out->st[2] = 0.0f;
        out->st[3] = 1.0f;
    }
}

/* ---- the switch ---------------------------------------------------------- */

/* THE ANSWER IS NO LONGER CACHED HERE, and that is the point.
 *
 * It used to be: `SL_SKY_BATCH` was read once into a file static, so the
 * environment decided for the life of the process and nothing could change
 * its mind. The path is now selected by the OPTIMIZED RENDERING row, which a
 * player can move while the game is running, so this question has to be asked
 * again rather than remembered. src/platform/sl_perf.c holds the one
 * precedence rule (a subsystem developer switch, parsed once, governs that
 * subsystem for the launch; otherwise the row), so no getenv happens here and
 * none happens per call - the only live part is an index into the settings
 * table.
 *
 * WHAT ASKS, AND WHEN. sl_skybatch_pass_begin asks exactly once per pass and
 * LATCHES the answer with the carrier (g_pass_fmode below), so a press cannot
 * split a pass: the pass that is already submitting finishes on the path it
 * opened with, and the next one picks up the new selection. Nothing between
 * pass_begin and pass_end consults this function. */
int sl_skybatch_on(void)
{
    return sl_perf_sky_on();
}

/* ---- r474: the float-colour carrier (opt-in, default off) ----------------
 *
 * See sl_gfx_skybatch.h, "THE FLOAT-COLOUR CARRIER EXPERIMENT". Everything in
 * this section is reached only after sl_skybatch_on() answered 1, and in byte
 * mode (the switch absent, or any value but exactly "div" or "prod") nothing
 * here makes a GL call or allocates.
 *
 * EXACT BITS, NO ARITHMETIC. The two tables are binary32 bit patterns, one per
 * byte value, generated by scripts/derive_474.py from exact rationals:
 *   skb_fdiv[c]  is the correctly rounded binary32 of c/255 (ties to even);
 *   skb_fprod[c] is the correctly rounded binary32 of the exact product
 *                c * fl32(1/255), where fl32(1/255) is 0x3b808081.
 * A table entry is copied into a float with memcpy, which is how C defines
 * reinterpreting an object's bytes; a pointer cast would break the
 * effective-type rule (this build passes -fno-strict-aliasing, but nothing
 * here depends on that). No floating-point operation runs, so contraction,
 * reassociation, excess precision and fast-math flags cannot change a value.
 * The checks below make the copy width and the record layouts compile-time
 * facts. */
typedef char skb_assert_float_is_32_bits[(sizeof(float) == 4u
                                          && sizeof(unsigned int) == 4u) ? 1 : -1];
typedef char skb_assert_vtx_is_36_bytes[(sizeof(struct sl_skybatch_vtx) == 36u
                                         && offsetof(struct sl_skybatch_vtx, col) == 16u
                                         && offsetof(struct sl_skybatch_vtx, st) == 20u)
                                        ? 1 : -1];
typedef char skb_assert_fvtx_is_48_bytes[(sizeof(struct sl_skybatch_fvtx) == 48u
                                          && offsetof(struct sl_skybatch_fvtx, col) == 16u
                                          && offsetof(struct sl_skybatch_fvtx, st) == 32u)
                                         ? 1 : -1];

/* generated by scripts/derive_474.py; do not edit */
static const unsigned int skb_fdiv[256] = {
    0x00000000u, 0x3b808081u, 0x3c008081u, 0x3c40c0c1u, 0x3c808081u, 0x3ca0a0a1u, 0x3cc0c0c1u, 0x3ce0e0e1u,
    0x3d008081u, 0x3d109091u, 0x3d20a0a1u, 0x3d30b0b1u, 0x3d40c0c1u, 0x3d50d0d1u, 0x3d60e0e1u, 0x3d70f0f1u,
    0x3d808081u, 0x3d888889u, 0x3d909091u, 0x3d989899u, 0x3da0a0a1u, 0x3da8a8a9u, 0x3db0b0b1u, 0x3db8b8b9u,
    0x3dc0c0c1u, 0x3dc8c8c9u, 0x3dd0d0d1u, 0x3dd8d8d9u, 0x3de0e0e1u, 0x3de8e8e9u, 0x3df0f0f1u, 0x3df8f8f9u,
    0x3e008081u, 0x3e048485u, 0x3e088889u, 0x3e0c8c8du, 0x3e109091u, 0x3e149495u, 0x3e189899u, 0x3e1c9c9du,
    0x3e20a0a1u, 0x3e24a4a5u, 0x3e28a8a9u, 0x3e2cacadu, 0x3e30b0b1u, 0x3e34b4b5u, 0x3e38b8b9u, 0x3e3cbcbdu,
    0x3e40c0c1u, 0x3e44c4c5u, 0x3e48c8c9u, 0x3e4ccccdu, 0x3e50d0d1u, 0x3e54d4d5u, 0x3e58d8d9u, 0x3e5cdcddu,
    0x3e60e0e1u, 0x3e64e4e5u, 0x3e68e8e9u, 0x3e6cecedu, 0x3e70f0f1u, 0x3e74f4f5u, 0x3e78f8f9u, 0x3e7cfcfdu,
    0x3e808081u, 0x3e828283u, 0x3e848485u, 0x3e868687u, 0x3e888889u, 0x3e8a8a8bu, 0x3e8c8c8du, 0x3e8e8e8fu,
    0x3e909091u, 0x3e929293u, 0x3e949495u, 0x3e969697u, 0x3e989899u, 0x3e9a9a9bu, 0x3e9c9c9du, 0x3e9e9e9fu,
    0x3ea0a0a1u, 0x3ea2a2a3u, 0x3ea4a4a5u, 0x3ea6a6a7u, 0x3ea8a8a9u, 0x3eaaaaabu, 0x3eacacadu, 0x3eaeaeafu,
    0x3eb0b0b1u, 0x3eb2b2b3u, 0x3eb4b4b5u, 0x3eb6b6b7u, 0x3eb8b8b9u, 0x3ebababbu, 0x3ebcbcbdu, 0x3ebebebfu,
    0x3ec0c0c1u, 0x3ec2c2c3u, 0x3ec4c4c5u, 0x3ec6c6c7u, 0x3ec8c8c9u, 0x3ecacacbu, 0x3ecccccdu, 0x3ecececfu,
    0x3ed0d0d1u, 0x3ed2d2d3u, 0x3ed4d4d5u, 0x3ed6d6d7u, 0x3ed8d8d9u, 0x3edadadbu, 0x3edcdcddu, 0x3edededfu,
    0x3ee0e0e1u, 0x3ee2e2e3u, 0x3ee4e4e5u, 0x3ee6e6e7u, 0x3ee8e8e9u, 0x3eeaeaebu, 0x3eececedu, 0x3eeeeeefu,
    0x3ef0f0f1u, 0x3ef2f2f3u, 0x3ef4f4f5u, 0x3ef6f6f7u, 0x3ef8f8f9u, 0x3efafafbu, 0x3efcfcfdu, 0x3efefeffu,
    0x3f008081u, 0x3f018182u, 0x3f028283u, 0x3f038384u, 0x3f048485u, 0x3f058586u, 0x3f068687u, 0x3f078788u,
    0x3f088889u, 0x3f09898au, 0x3f0a8a8bu, 0x3f0b8b8cu, 0x3f0c8c8du, 0x3f0d8d8eu, 0x3f0e8e8fu, 0x3f0f8f90u,
    0x3f109091u, 0x3f119192u, 0x3f129293u, 0x3f139394u, 0x3f149495u, 0x3f159596u, 0x3f169697u, 0x3f179798u,
    0x3f189899u, 0x3f19999au, 0x3f1a9a9bu, 0x3f1b9b9cu, 0x3f1c9c9du, 0x3f1d9d9eu, 0x3f1e9e9fu, 0x3f1f9fa0u,
    0x3f20a0a1u, 0x3f21a1a2u, 0x3f22a2a3u, 0x3f23a3a4u, 0x3f24a4a5u, 0x3f25a5a6u, 0x3f26a6a7u, 0x3f27a7a8u,
    0x3f28a8a9u, 0x3f29a9aau, 0x3f2aaaabu, 0x3f2babacu, 0x3f2cacadu, 0x3f2dadaeu, 0x3f2eaeafu, 0x3f2fafb0u,
    0x3f30b0b1u, 0x3f31b1b2u, 0x3f32b2b3u, 0x3f33b3b4u, 0x3f34b4b5u, 0x3f35b5b6u, 0x3f36b6b7u, 0x3f37b7b8u,
    0x3f38b8b9u, 0x3f39b9bau, 0x3f3ababbu, 0x3f3bbbbcu, 0x3f3cbcbdu, 0x3f3dbdbeu, 0x3f3ebebfu, 0x3f3fbfc0u,
    0x3f40c0c1u, 0x3f41c1c2u, 0x3f42c2c3u, 0x3f43c3c4u, 0x3f44c4c5u, 0x3f45c5c6u, 0x3f46c6c7u, 0x3f47c7c8u,
    0x3f48c8c9u, 0x3f49c9cau, 0x3f4acacbu, 0x3f4bcbccu, 0x3f4ccccdu, 0x3f4dcdceu, 0x3f4ececfu, 0x3f4fcfd0u,
    0x3f50d0d1u, 0x3f51d1d2u, 0x3f52d2d3u, 0x3f53d3d4u, 0x3f54d4d5u, 0x3f55d5d6u, 0x3f56d6d7u, 0x3f57d7d8u,
    0x3f58d8d9u, 0x3f59d9dau, 0x3f5adadbu, 0x3f5bdbdcu, 0x3f5cdcddu, 0x3f5ddddeu, 0x3f5ededfu, 0x3f5fdfe0u,
    0x3f60e0e1u, 0x3f61e1e2u, 0x3f62e2e3u, 0x3f63e3e4u, 0x3f64e4e5u, 0x3f65e5e6u, 0x3f66e6e7u, 0x3f67e7e8u,
    0x3f68e8e9u, 0x3f69e9eau, 0x3f6aeaebu, 0x3f6bebecu, 0x3f6cecedu, 0x3f6dedeeu, 0x3f6eeeefu, 0x3f6feff0u,
    0x3f70f0f1u, 0x3f71f1f2u, 0x3f72f2f3u, 0x3f73f3f4u, 0x3f74f4f5u, 0x3f75f5f6u, 0x3f76f6f7u, 0x3f77f7f8u,
    0x3f78f8f9u, 0x3f79f9fau, 0x3f7afafbu, 0x3f7bfbfcu, 0x3f7cfcfdu, 0x3f7dfdfeu, 0x3f7efeffu, 0x3f800000u,
};

/* generated by scripts/derive_474.py; do not edit */
static const unsigned int skb_fprod[256] = {
    0x00000000u, 0x3b808081u, 0x3c008081u, 0x3c40c0c2u, 0x3c808081u, 0x3ca0a0a1u, 0x3cc0c0c2u, 0x3ce0e0e2u,
    0x3d008081u, 0x3d109091u, 0x3d20a0a1u, 0x3d30b0b1u, 0x3d40c0c2u, 0x3d50d0d2u, 0x3d60e0e2u, 0x3d70f0f2u,
    0x3d808081u, 0x3d888889u, 0x3d909091u, 0x3d989899u, 0x3da0a0a1u, 0x3da8a8a9u, 0x3db0b0b1u, 0x3db8b8b9u,
    0x3dc0c0c2u, 0x3dc8c8cau, 0x3dd0d0d2u, 0x3dd8d8dau, 0x3de0e0e2u, 0x3de8e8eau, 0x3df0f0f2u, 0x3df8f8fau,
    0x3e008081u, 0x3e048485u, 0x3e088889u, 0x3e0c8c8du, 0x3e109091u, 0x3e149495u, 0x3e189899u, 0x3e1c9c9du,
    0x3e20a0a1u, 0x3e24a4a5u, 0x3e28a8a9u, 0x3e2cacadu, 0x3e30b0b1u, 0x3e34b4b5u, 0x3e38b8b9u, 0x3e3cbcbdu,
    0x3e40c0c2u, 0x3e44c4c6u, 0x3e48c8cau, 0x3e4cccceu, 0x3e50d0d2u, 0x3e54d4d6u, 0x3e58d8dau, 0x3e5cdcdeu,
    0x3e60e0e2u, 0x3e64e4e6u, 0x3e68e8eau, 0x3e6ceceeu, 0x3e70f0f2u, 0x3e74f4f6u, 0x3e78f8fau, 0x3e7cfcfeu,
    0x3e808081u, 0x3e828283u, 0x3e848485u, 0x3e868687u, 0x3e888889u, 0x3e8a8a8bu, 0x3e8c8c8du, 0x3e8e8e8fu,
    0x3e909091u, 0x3e929293u, 0x3e949495u, 0x3e969697u, 0x3e989899u, 0x3e9a9a9bu, 0x3e9c9c9du, 0x3e9e9e9fu,
    0x3ea0a0a1u, 0x3ea2a2a3u, 0x3ea4a4a5u, 0x3ea6a6a7u, 0x3ea8a8a9u, 0x3eaaaaabu, 0x3eacacadu, 0x3eaeaeafu,
    0x3eb0b0b1u, 0x3eb2b2b3u, 0x3eb4b4b5u, 0x3eb6b6b7u, 0x3eb8b8b9u, 0x3ebababbu, 0x3ebcbcbdu, 0x3ebebebfu,
    0x3ec0c0c2u, 0x3ec2c2c4u, 0x3ec4c4c6u, 0x3ec6c6c8u, 0x3ec8c8cau, 0x3ecacaccu, 0x3eccccceu, 0x3ececed0u,
    0x3ed0d0d2u, 0x3ed2d2d4u, 0x3ed4d4d6u, 0x3ed6d6d8u, 0x3ed8d8dau, 0x3edadadcu, 0x3edcdcdeu, 0x3ededee0u,
    0x3ee0e0e2u, 0x3ee2e2e4u, 0x3ee4e4e6u, 0x3ee6e6e8u, 0x3ee8e8eau, 0x3eeaeaecu, 0x3eececeeu, 0x3eeeeef0u,
    0x3ef0f0f2u, 0x3ef2f2f4u, 0x3ef4f4f6u, 0x3ef6f6f8u, 0x3ef8f8fau, 0x3efafafcu, 0x3efcfcfeu, 0x3efeff00u,
    0x3f008081u, 0x3f018182u, 0x3f028283u, 0x3f038384u, 0x3f048485u, 0x3f058586u, 0x3f068687u, 0x3f078788u,
    0x3f088889u, 0x3f09898au, 0x3f0a8a8bu, 0x3f0b8b8cu, 0x3f0c8c8du, 0x3f0d8d8eu, 0x3f0e8e8fu, 0x3f0f8f90u,
    0x3f109091u, 0x3f119192u, 0x3f129293u, 0x3f139394u, 0x3f149495u, 0x3f159596u, 0x3f169697u, 0x3f179798u,
    0x3f189899u, 0x3f19999au, 0x3f1a9a9bu, 0x3f1b9b9cu, 0x3f1c9c9du, 0x3f1d9d9eu, 0x3f1e9e9fu, 0x3f1f9fa0u,
    0x3f20a0a1u, 0x3f21a1a2u, 0x3f22a2a3u, 0x3f23a3a4u, 0x3f24a4a5u, 0x3f25a5a6u, 0x3f26a6a7u, 0x3f27a7a8u,
    0x3f28a8a9u, 0x3f29a9aau, 0x3f2aaaabu, 0x3f2babacu, 0x3f2cacadu, 0x3f2dadaeu, 0x3f2eaeafu, 0x3f2fafb0u,
    0x3f30b0b1u, 0x3f31b1b2u, 0x3f32b2b3u, 0x3f33b3b4u, 0x3f34b4b5u, 0x3f35b5b6u, 0x3f36b6b7u, 0x3f37b7b8u,
    0x3f38b8b9u, 0x3f39b9bau, 0x3f3ababbu, 0x3f3bbbbcu, 0x3f3cbcbdu, 0x3f3dbdbeu, 0x3f3ebebfu, 0x3f3fbfc0u,
    0x3f40c0c2u, 0x3f41c1c3u, 0x3f42c2c4u, 0x3f43c3c5u, 0x3f44c4c6u, 0x3f45c5c7u, 0x3f46c6c8u, 0x3f47c7c9u,
    0x3f48c8cau, 0x3f49c9cbu, 0x3f4acaccu, 0x3f4bcbcdu, 0x3f4cccceu, 0x3f4dcdcfu, 0x3f4eced0u, 0x3f4fcfd1u,
    0x3f50d0d2u, 0x3f51d1d3u, 0x3f52d2d4u, 0x3f53d3d5u, 0x3f54d4d6u, 0x3f55d5d7u, 0x3f56d6d8u, 0x3f57d7d9u,
    0x3f58d8dau, 0x3f59d9dbu, 0x3f5adadcu, 0x3f5bdbddu, 0x3f5cdcdeu, 0x3f5ddddfu, 0x3f5edee0u, 0x3f5fdfe1u,
    0x3f60e0e2u, 0x3f61e1e3u, 0x3f62e2e4u, 0x3f63e3e5u, 0x3f64e4e6u, 0x3f65e5e7u, 0x3f66e6e8u, 0x3f67e7e9u,
    0x3f68e8eau, 0x3f69e9ebu, 0x3f6aeaecu, 0x3f6bebedu, 0x3f6ceceeu, 0x3f6dedefu, 0x3f6eeef0u, 0x3f6feff1u,
    0x3f70f0f2u, 0x3f71f1f3u, 0x3f72f2f4u, 0x3f73f3f5u, 0x3f74f4f6u, 0x3f75f5f7u, 0x3f76f6f8u, 0x3f77f7f9u,
    0x3f78f8fau, 0x3f79f9fbu, 0x3f7afafcu, 0x3f7bfbfdu, 0x3f7cfcfeu, 0x3f7dfdffu, 0x3f7eff00u, 0x3f800000u,
};

static struct sl_skybatch_fvtx *g_fstage;  /* g_fcap records, float mode only */
static unsigned g_fcap;
/* PROCESS LIFETIME, like g_snap_tried: never cleared, so the marker printed
 * from sl_skybatch_pass_begin appears at most once per process however many
 * times the context is dropped, the asset is replaced or the module re-arms. */
static int g_fsaid;
#ifdef SL_SKYBATCH_TEST
static int g_ffail;                        /* force the staging allocation to fail */
static unsigned long g_fmarkers;           /* times the marker was printed */
#endif

/* THE CARRIER, RESOLVED BY THE POLICY and no longer cached here. The three
 * SL_PERF_SKY_CARRIER_* ids are the three SL_SKYBATCH_FLOAT_* ids by
 * construction (checked below), so this is a rename and not a mapping: the
 * policy says byte / div / prod and this module uploads 36-byte records,
 * table-converted c/255 or table-converted c * fl32(1/255).
 *
 * THE ROW ONLY EVER SELECTS PROD. div and byte are reachable only through
 * SL_SKY_BATCH_FLOAT_COLOR, which is a developer switch, so no menu position
 * and no default can put an unvalidated carrier on screen. The exact-string
 * parsing that used to live here is sl_perf.c's, unchanged in effect: a typo
 * selects no carrier and the policy's own answer stands. */
typedef char skb_assert_carrier_ids_agree
    [(SL_PERF_SKY_CARRIER_BYTE == SL_SKYBATCH_FLOAT_OFF
      && SL_PERF_SKY_CARRIER_DIV == SL_SKYBATCH_FLOAT_DIV
      && SL_PERF_SKY_CARRIER_PROD == SL_SKYBATCH_FLOAT_PROD) ? 1 : -1];

int sl_skybatch_float_mode(void)
{
    return sl_perf_sky_carrier();
}

int sl_skybatch_float_colour(int mode, const unsigned char col[4], float out[4])
{
    const unsigned int *t;
    unsigned i;
    if (mode == SL_SKYBATCH_FLOAT_DIV)       t = skb_fdiv;
    else if (mode == SL_SKYBATCH_FLOAT_PROD) t = skb_fprod;
    else return 0;
    for (i = 0; i < 4u; i++) memcpy(&out[i], &t[col[i]], sizeof out[i]);
    return 1;
}

/* The float staging's byte size for n records. 0 (refuse) when n is zero or
 * the size would pass what glBufferData's ptrdiff_t carries on this target.
 * The aggregate ceiling below (SKB_MAX_AGG_INDICES) already keeps g_umax at or
 * below 12000000, which is 576000000 bytes here; this makes that a checked
 * property where the size is formed rather than a remembered one. */
static int skb_float_bytes(size_t n, size_t *bytes)
{
    if (n == 0u || n > (size_t) 0x7ffffffful / sizeof(struct sl_skybatch_fvtx)) return 0;
    *bytes = n * sizeof(struct sl_skybatch_fvtx);
    return 1;
}

#ifdef SL_SKYBATCH_TEST
/* Offline build only: drop the policy's cached environment answers (the mode
 * and the switch now live there, so there is one place to clear), force the
 * next staging allocation to fail, and read the staging and the marker count
 * from the inside. The marker's lifetime flag is deliberately NOT reset. */
void sl_skybatch_test_reset_float_mode(void) { sl_perf_test_reset(); }
void sl_skybatch_test_fail_float_alloc(int fail) { g_ffail = fail; }

void sl_skybatch_test_float_state(int *allocated, unsigned long *capacity,
                                  unsigned long *markers)
{
    if (allocated != NULL) *allocated = g_fstage != NULL;
    if (capacity  != NULL) *capacity  = (unsigned long) g_fcap;
    if (markers   != NULL) *markers   = g_fmarkers;
}

int sl_skybatch_test_float_bytes(unsigned long n, unsigned long *bytes)
{
    size_t b = 0u;
    const int ok = skb_float_bytes((size_t) n, &b);
    if (bytes != NULL) *bytes = ok ? (unsigned long) b : 0ul;
    return ok;
}
#endif

#ifdef SL_SKYBATCH_TEST
/* Offline build only (-DSL_SKYBATCH_TEST): drop the policy's cached
 * environment answers so one test executable can exercise both the armed and
 * the default-off path. The answer itself is no longer cached in this module -
 * it is the OPTIMIZED RENDERING row, read live - so what this clears is the
 * environment side of the precedence rule. Absent from the shipped
 * translation unit. */
void sl_skybatch_test_reset_switch(void) { sl_perf_test_reset(); }

/* r398, ported from r386 :406-427. Its two array outputs answer the constant 0
 * - this candidate has no array path, so nothing could be counted there - and
 * two declined outputs are added, so the suite reads from the inside every
 * counter the note, the report and the terminal snapshot print. */
void sl_skybatch_test_counters(unsigned long *passes,
                               unsigned long *idx_draws,
                               unsigned long *idx_vertices,
                               unsigned long *arr_draws,
                               unsigned long *arr_vertices,
                               unsigned long *uniq,
                               unsigned long *pass_refused,
                               unsigned long *mesh_refused,
                               unsigned long *declined,
                               unsigned long *declined_vertices)
{
    if (passes            != NULL) *passes            = g_c_passes;
    if (idx_draws         != NULL) *idx_draws         = g_c_draws;
    if (idx_vertices      != NULL) *idx_vertices      = g_c_idx;
    if (arr_draws         != NULL) *arr_draws         = 0ul;
    if (arr_vertices      != NULL) *arr_vertices      = 0ul;
    if (uniq              != NULL) *uniq              = g_c_uniq;
    if (pass_refused      != NULL) *pass_refused      = g_c_refused;
    if (mesh_refused      != NULL) *mesh_refused      = g_c_mesh_refused;
    if (declined          != NULL) *declined          = g_c_declined;
    if (declined_vertices != NULL) *declined_vertices = g_c_dec_vtx;
}

/* The writing pass's capacities against what it actually wrote. Equal counts
 * mean the pass filled exactly what it allocated; a capacity BELOW the used
 * count is what the previous revision produced on an overlapping layout, and
 * what the regression compares against. */
void sl_skybatch_test_capacity(unsigned long *uniq_cap, unsigned long *uniq_used,
                               unsigned long *idx_cap, unsigned long *idx_used)
{
    if (uniq_cap  != NULL) *uniq_cap  = g_uniq_cap;
    if (uniq_used != NULL) *uniq_used = g_uniq_used;
    if (idx_cap   != NULL) *idx_cap   = g_idx_cap;
    if (idx_used  != NULL) *idx_used  = g_idx_used;
}
#endif

/* A bounded diagnostic. The cadence is GEOMETRIC in the pass count - passes
 * 1, 64, 512, 4096, 32768 - so a whole run prints a handful of lines whatever
 * its length, and nothing prints per mesh or per frame. */
static void skb_note(const struct sl_wa_asset *a)
{
    if (g_c_passes != 1u && g_c_passes != 64u && g_c_passes != 512u
        && g_c_passes != 4096u && g_c_passes != 32768u) return;
    /* r398: r386's field names and order, so a reviewed terminal grammar needs
     * only the declined fields added. The array fields are LITERAL zeros in
     * the format: no counter exists that could make them anything else. The
     * parenthesised total is what the immediate path alone would have
     * submitted for the same work - one vertex per index of the indexed draws
     * AND of the declined meshes, which the immediate loop did submit. The
     * argument list is in format order and the same length as the conversion
     * list; scripts/derive_398.py checks that for every fprintf in this file. */
    fprintf(stderr,
            "#316 skybatch: skydome/%s passes %lu indexed-draws %lu "
            "indexed %lu array-draws 0 array-vertices 0 "
            "unique %lu mesh-refused %lu declined %lu declined-vertices %lu "
            "(the immediate path submits one vertex per index: %lu) "
            "element %s\n",
            a->path, g_c_passes, g_c_draws, g_c_idx, g_c_uniq,
            g_c_mesh_refused, g_c_declined, g_c_dec_vtx,
            g_c_idx + g_c_dec_vtx,
            g_idx16 ? "u16" : "u32");
}

void sl_skybatch_report(FILE *f)
{
    if (f == NULL) return;
    fprintf(f, "#316 skybatch: %s; items %lu passes %lu "
               "indexed-draws %lu indexed %lu array-draws 0 "
               "array-vertices 0 unique %lu refused %lu "
               "mesh-refused %lu declined %lu declined-vertices %lu\n",
            !sl_skybatch_on() ? "off (OPTIMIZED RENDERING is off, or "
                                "SL_SKY_BATCH says 0)"
                              : g_state > 0 ? "armed"
                              : g_state < 0 ? "armed but unavailable - the "
                                              "immediate path drew"
                                            : "armed, nothing drawn yet",
            g_c_items, g_c_passes, g_c_draws, g_c_idx, g_c_uniq, g_c_refused,
            g_c_mesh_refused, g_c_declined, g_c_dec_vtx);
}

/* r398: the terminal counter snapshot, ported from r386 :498-541. Its line
 * gains the declined fields and prints the array fields as the literal
 * zeros this candidate's report prints; nothing else about it changes.
 * void(void), for atexit.
 *
 * WHAT IT DOES. Reads only the scalar cumulative counters declared
 * above and writes exactly ONE line, uniquely identifiable by its
 * "final" keyword: the ordinary report at sl_skybatch_report puts a
 * state phrase and a semicolon after the prefix, and none of its four
 * state phrases is the word "final", so no ordinary report can be
 * mistaken for this record and this record cannot be mistaken for one.
 *
 * WHAT IT MUST NOT DO, and does not. No GL entry point is called and
 * none is even read; no buffer name, asset, clip array or staging
 * pointer is dereferenced; nothing is allocated or freed; there is no
 * context, query, readback or process work; and there is no cleanup -
 * the buffer deletion stays in sl_skybatch_shutdown, where it already
 * is. It also resets nothing and counts nothing, so its own execution
 * cannot change what a later reader would have seen.
 *
 * WHY IT IS STILL VALID AFTER A SHUTDOWN AND A RE-ARM. The counters
 * are never assigned anywhere in this file after their definitions -
 * neither sl_skybatch_shutdown, nor sl_skybatch_context_invalidate,
 * nor skb_free_cpu touches one - so they accumulate from process load
 * and the totals printed here are from-load cumulative whatever the
 * lifecycle did in between. Reading them needs no live context, which
 * is why this is safe to run after the last shutdown.
 *
 * I/O. One bounded fprintf to stderr, which an ordinary process exit
 * flushes; no new or blocking I/O beyond it. A kill or an abort may
 * never run atexit handlers at all, in which case the marker is simply
 * absent and the parser is required to read that as INCONCLUSIVE
 * rather than as a zero-refusal proof.
 *
 * The argument list below is in format order and the same length as
 * the conversion list, which scripts/derive_398.py checks. */
static void skb_final_snapshot(void)
{
    fprintf(stderr,
            "#316 skybatch: final items %lu passes %lu "
            "indexed-draws %lu indexed %lu array-draws 0 "
            "array-vertices 0 unique %lu refused %lu "
            "mesh-refused %lu declined %lu declined-vertices %lu\n",
            g_c_items, g_c_passes, g_c_draws, g_c_idx,
            g_c_uniq, g_c_refused, g_c_mesh_refused,
            g_c_declined, g_c_dec_vtx);
}

/* ---- allocation, with the overflow guard written out -------------------- */

/* n records of `elem` bytes, or 0 when the product would not fit a size_t.
 * The provider's own validation already bounds the counts (SL_WA_MAX_VERTICES
 * 1000000, SL_WA_MAX_INDICES 3000000, SL_WA_MAX_MESHES 1024, all checked at
 * parse), so no product below can overflow a 32-bit size_t - but the guard is
 * what makes that a checked property rather than a remembered one. */
static void *skb_alloc(size_t n, size_t elem)
{
    if (n == 0u || elem == 0u) return NULL;
    if (n > (size_t) -1 / elem) return NULL;
    return malloc(n * elem);
}

static void skb_free_cpu(void)
{
    free(g_uniq);     g_uniq = NULL;
    free(g_lastslot); g_lastslot = NULL;
    free(g_uoff);     g_uoff = NULL;
    free(g_ioff);     g_ioff = NULL;
    free(g_stamp);    g_stamp = NULL;
    free(g_slot);     g_slot = NULL;
    free(g_stage);    g_stage = NULL;
    free(g_fstage);   g_fstage = NULL;     /* r474 */
    g_fcap = 0u;
    g_umax = 0u;
    g_uniq_cap = g_uniq_used = g_idx_cap = g_idx_used = 0ul;
    g_elem_bytes = 0u;
    g_asset = NULL;
}

/* THE FLOAT STAGING, at the current g_umax width, beside g_stage. Released by
 * skb_free_cpu with everything else, so a context invalidate, a shutdown and
 * an asset change each free it.
 *
 * IT IS ALLOCATED ON DEMAND, NOT ONLY BY THE TOPOLOGY BUILD, and that is what
 * makes a same-asset BYTE -> FLOAT change work. The topology build returns
 * immediately for an asset it already describes (skb_topology: `a == g_asset`),
 * so a topology first built while the carrier was byte carries NO float
 * staging; the earlier revision then had sl_skybatch_pass_begin refuse every
 * later pass, which with a cached carrier was unreachable and with a row the
 * player can move would mean the selection appeared to do nothing for the rest
 * of the stage. sl_skybatch_pass_begin therefore allocates here when the
 * pass's latched carrier needs staging the cached topology has not got, before
 * any draw - and a refusal is still a refusal before any submission, which is
 * the only safe direction.
 *
 * FREE FIRST. The call is now reachable with staging already held (a narrower
 * asset after a wider one, in the same carrier), so the old block is released
 * rather than leaked, and g_fcap is cleared before the attempt so a failure
 * cannot leave a capacity describing storage that is gone. */
static int skb_float_alloc(void)
{
    size_t bytes;
    free(g_fstage);
    g_fstage = NULL;
    g_fcap = 0u;
    if (!skb_float_bytes((size_t) g_umax, &bytes)) return 0;
#ifdef SL_SKYBATCH_TEST
    if (g_ffail) return 0;
#endif
    g_fstage = (struct sl_skybatch_fvtx *) skb_alloc((size_t) g_umax, sizeof *g_fstage);
    if (g_fstage == NULL) return 0;
    g_fcap = g_umax;
    return 1;
}

/* ---- the entry points, resolved once per context ------------------------ */

static int skb_ready(void)
{
    if (g_state != 0) return g_state > 0;
    /* OFF DOES NOT POISON CAPABILITY. This used to latch g_state = -1, which
     * was sound while the answer was an environment variable read once: a
     * process that started with the path off could never turn it on, so there
     * was nothing to re-ask. The answer is now a row a player can move, and a
     * latched -1 would mean the FIRST frame drawn with the row off decided for
     * the rest of the launch - OFF -> ON would resolve no entry point, create
     * no buffer and silently keep the immediate loop. g_state stays 0
     * (untried) instead, so the first pass after the row goes ON resolves and
     * creates exactly as a process that started ON would. The caller refuses
     * this pass either way, and the refusal is not counted here: pass_begin
     * returns before any counter when the path is off. */
    if (!sl_skybatch_on()) return 0;

    /* r386: register the terminal snapshot as EARLY on the enabled path
     * as is feasible.
     *
     * WHERE THIS SITS. Two guards run above it. The fast return on the
     * line `if (g_state != 0) return g_state > 0;` means this body runs
     * only on the FIRST decision of a context, so it cannot execute per
     * frame. The SL_SKY_BATCH guard immediately above means a
     * default-off process returns before this point, and therefore
     * registers nothing and prints nothing at all.
     *
     * WHY HERE AND NOT AFTER g_state = 1. Below this point the enabled
     * path can still refuse twice - once if an entry point is missing,
     * once if a buffer name is not handed out - and each of those
     * refusals returns 0 after printing its own fallback line.
     * Registering only on success would leave an enabled run that fell
     * back with no terminal record at all, which is exactly the
     * evidence gap this change exists to close. Registering here covers
     * every enabled outcome.
     *
     * ONE ATTEMPT PER PROCESS. g_snap_tried is set BEFORE the atexit
     * call, so a failure can neither be retried nor print a second
     * diagnostic on a later frame or a later re-arm, and the flag is
     * never cleared by any lifecycle path. */
    if (!g_snap_tried) {
        g_snap_tried = 1;
        if (atexit(skb_final_snapshot) == 0) {
            g_snap_ok = 1;
        } else {
            fprintf(stderr, "#316 skybatch: snapshot unavailable - the "
                            "terminal counter snapshot could not be "
                            "registered, so coverage through the timed "
                            "window is not established by this run\n");
        }
    }

    p_GenBuffers          = (SKB_PFN_GENBUFFERS)          sl_gl_proc("glGenBuffers");
    p_DeleteBuffers       = (SKB_PFN_DELETEBUFFERS)       sl_gl_proc("glDeleteBuffers");
    p_BindBuffer          = (SKB_PFN_BINDBUFFER)          sl_gl_proc("glBindBuffer");
    p_BufferData          = (SKB_PFN_BUFFERDATA)          sl_gl_proc("glBufferData");
    p_ClientActiveTexture = (SKB_PFN_CLIENTACTIVETEXTURE) sl_gl_proc("glClientActiveTexture");

    /* EVERY ONE IS REQUIRED, and the client unit selector is not optional:
     * without it the inherited client active texture can neither be read nor
     * put back, and the texture-coordinate array this module sets would land
     * on whichever unit happened to be selected. A missing name is a refusal,
     * not a degraded path. */
    if (p_GenBuffers == NULL || p_DeleteBuffers == NULL || p_BindBuffer == NULL
        || p_BufferData == NULL || p_ClientActiveTexture == NULL) {
        g_state = -1;
        fprintf(stderr, "#316 skybatch: %s unavailable - the skydome draws "
                        "through the immediate path\n",
                p_ClientActiveTexture == NULL ? "glClientActiveTexture"
                                              : "buffer objects");
        return 0;
    }

    p_GenBuffers(1, &g_vb);
    p_GenBuffers(1, &g_ib);
    if (g_vb == 0u || g_ib == 0u) {
        /* Clean up the partial allocation: one name may have been handed out
         * even though the other was not. */
        if (g_vb != 0u) { p_DeleteBuffers(1, &g_vb); g_vb = 0u; }
        if (g_ib != 0u) { p_DeleteBuffers(1, &g_ib); g_ib = 0u; }
        g_state = -1;
        fprintf(stderr, "#316 skybatch: no buffer name - the skydome draws "
                        "through the immediate path\n");
        return 0;
    }
    g_state = 1;
    if (!g_said_armed) {
        g_said_armed = 1;
        fprintf(stderr, "#316 skybatch: armed - the skydome's cloud material "
                        "draws submit indexed buffers, one prepared record per "
                        "unique referenced vertex, and its plain material draws "
                        "are declined to the immediate path by design. "
                        "Selected by OPTIMIZED RENDERING (src/platform/sl_perf.h) "
                        "or by SL_SKY_BATCH; off draws the immediate path\n");
    }
    return 1;
}

/* ---- the topology, built once per asset ---------------------------------
 *
 * THE AGGREGATE IS NOT BOUNDED BY THE INDEX COUNT, and assuming it was is the
 * memory-safety defect this round corrects.
 *
 * `sl_wa_decode_arrays` validates a mesh range ONE MESH AT A TIME: the only
 * test is `first_index + index_count <= index_count` (src/gfx/sl_gfx_worldasset.c,
 * the mesh loop - read for this round rather than assumed). There is NO
 * cross-mesh disjointness contract anywhere in that parser, so two meshes may
 * legally cover the SAME range of the index list. The smallest witness: three
 * indices, two meshes both at first_index 0 with index_count 3. Each mesh
 * passes the parser and passes this module's own per-range check, and each
 * contributes its own three unique references - so the unique list needs SIX
 * slots while `index_count` is THREE.
 *
 * The previous revision sized the unique list from `a->index_count` and then
 * summed each mesh's unique count independently into the same array. On that
 * witness it wrote six slots into a three-slot allocation: a heap overflow on
 * a layout the format permits.
 *
 * The correction is to COUNT FIRST and allocate from the true aggregate.
 * Overlapping ranges are legal, so they are SUPPORTED rather than refused -
 * refusing a legal layout would silently drop the feature on it. What IS
 * refused is an aggregate past the ceiling below, before any list is written.
 *
 * WHY A CEILING AT ALL. With mesh_count <= SL_WA_MAX_MESHES (1024) and each
 * mesh's count <= SL_WA_MAX_INDICES (3000000), the aggregate can reach
 * 3072000000. That still fits the `unsigned long` accumulator used here (it is
 * below 4294967295, so the accumulation itself cannot wrap), but multiplied by
 * four bytes an index it is 12288000000 - far past what a 32-bit size_t or a
 * ptrdiff_t can express, so the byte size, not the count, is what has to be
 * bounded. The ceiling is four times the format's own per-asset index
 * ceiling: a dome whose meshes collectively reference more than four times its
 * own index list is a layout no bundle has been observed to produce, and at
 * that ceiling every derived byte size (12000000 * 4 = 48000000 for the unique
 * list and the same for the compact list) is comfortably inside both a 32-bit
 * size_t and a ptrdiff_t. Past it the pass refuses and the immediate loop
 * draws, which is a supported outcome rather than a failure.
 *
 * Which vertices each mesh references, in FIRST-REFERENCE order, and the
 * compact index list that names them, are immutable for the asset: a property
 * of the index list alone. The provider keeps an asset for the life of the
 * process (sl_wa_lookup writes a slot once and never evicts or frees a
 * returned asset - verified in src/gfx/sl_gfx_worldasset.c, where the only
 * free of the allocation is on the `bad:` path, before any caller can hold the
 * pointer). So identity is a sound cache key, and an asset change is a pointer
 * change.
 *
 * THE ORDER IS THE WHOLE POINT. compact[k] is the slot of the vertex the
 * immediate loop's k-th submission used, so one glDrawElements over compact[]
 * walks the asset's own triangles in the asset's own order. */

/* Four times SL_WA_MAX_INDICES. See the derivation above: the count could
 * legally reach 3072000000, and it is the BYTE size derived from it that must
 * stay inside a 32-bit size_t and a ptrdiff_t. */
#define SKB_MAX_AGG_INDICES 12000000ul

static int skb_topology(const struct sl_wa_asset *a)
{
    unsigned m, k;
    unsigned long agg_i = 0ul, agg_u = 0ul;    /* counted BEFORE any list write */
    unsigned long wrote_u = 0ul, wrote_i = 0ul;
    void *compact = NULL;
    size_t esz;

    if (a == g_asset) return 1;
    skb_free_cpu();

    /* The counts the provider validated, re-checked here because everything
     * below sizes an allocation from them. */
    if (a->vertex_count == 0u || a->vertex_count > SL_WA_MAX_VERTICES) return 0;
    if (a->index_count  == 0u || a->index_count  > SL_WA_MAX_INDICES)  return 0;
    if (a->mesh_count   == 0u || a->mesh_count   > SL_WA_MAX_MESHES)   return 0;
    if (a->indices == NULL || a->vertices == NULL) return 0;

    /* These five are sized from the per-asset counts alone, so they are safe
     * before the aggregate is known. The two LISTS whose size depends on the
     * aggregate are allocated after the counting pass. */
    g_uoff     = (unsigned *) skb_alloc((size_t) a->mesh_count + 1u, sizeof *g_uoff);
    g_ioff     = (unsigned *) skb_alloc((size_t) a->mesh_count + 1u, sizeof *g_ioff);
    g_lastslot = (unsigned *) skb_alloc((size_t) a->mesh_count, sizeof *g_lastslot);
    g_stamp    = (unsigned *) skb_alloc((size_t) a->vertex_count, sizeof *g_stamp);
    g_slot     = (unsigned *) skb_alloc((size_t) a->vertex_count, sizeof *g_slot);
    if (g_uoff == NULL || g_ioff == NULL || g_lastslot == NULL
        || g_stamp == NULL || g_slot == NULL) { skb_free_cpu(); return 0; }
    memset(g_stamp, 0, (size_t) a->vertex_count * sizeof *g_stamp);

    /* ---- PASS ONE: COUNT ONLY. Nothing is written to a list here, because
     * the lists do not exist yet - that is the whole point of the pass. It
     * also validates every range and every index, and fixes each mesh's
     * offsets. A mesh whose range leaves the index list, or whose index names
     * a vertex the asset does not carry, REFUSES the asset: the immediate loop
     * would read past the vertex array there, so there is no behaviour to
     * preserve. */
    for (m = 0; m < a->mesh_count; m++) {
        const struct sl_wa_mesh *mh = &a->meshes[m];
        unsigned n = 0u;
        /* Both offsets are below the ceiling, which the previous iteration
         * checked, so neither narrowing cast can lose a value. */
        g_uoff[m] = (unsigned) agg_u;
        g_ioff[m] = (unsigned) agg_i;
        if (mh->index_count > a->index_count
            || mh->first_index > a->index_count - mh->index_count) {
            skb_free_cpu(); return 0;
        }
        for (k = 0; k < mh->index_count; k++) {
            const unsigned vi = a->indices[mh->first_index + k];
            if (vi >= a->vertex_count) { skb_free_cpu(); return 0; }
            /* The stamp is m + 1, so zeroed storage means "no mesh yet" and
             * no mesh index can collide with it. */
            if (g_stamp[vi] != m + 1u) {
                g_stamp[vi] = m + 1u;
                n++;
            }
        }
        /* mh->index_count <= a->index_count <= 3000000 and there are at most
         * 1024 meshes, so neither sum can wrap an unsigned long; the ceiling
         * is what keeps the derived BYTE sizes expressible. */
        agg_i += (unsigned long) mh->index_count;
        agg_u += (unsigned long) n;
        if (agg_i > SKB_MAX_AGG_INDICES || agg_u > SKB_MAX_AGG_INDICES) {
            skb_free_cpu(); return 0;
        }
        if (n > g_umax) g_umax = n;
    }
    g_uoff[a->mesh_count] = (unsigned) agg_u;
    g_ioff[a->mesh_count] = (unsigned) agg_i;
    if (agg_u == 0ul || agg_i == 0ul) { skb_free_cpu(); return 0; }

    /* The element type. A u16 index addresses 0..65535, so a mesh of up to
     * 65536 unique vertices fits; anything wider takes u32. One type for the
     * whole asset, because one buffer holds every mesh's indices. */
    g_idx16 = (g_umax <= 65536u);
    esz = g_idx16 ? sizeof(unsigned short) : sizeof(unsigned);

    /* ---- the aggregate-sized lists, now that the aggregate is known.
     * skb_alloc refuses any product that would not fit a size_t, so the
     * ceiling above and this guard are independent: either one alone keeps the
     * allocation honest. */
    g_uniq  = (unsigned *) skb_alloc((size_t) agg_u, sizeof *g_uniq);
    g_stage = (struct sl_skybatch_vtx *) skb_alloc((size_t) g_umax, sizeof *g_stage);
    compact = skb_alloc((size_t) agg_i, esz);
    if (g_uniq == NULL || g_stage == NULL || compact == NULL) {
        free(compact); skb_free_cpu(); return 0;
    }
    /* THE FLOAT STAGING IS NOT ALLOCATED HERE ANY MORE. It used to be, which
     * put it on the one path that does not run when the asset is already
     * described - so a topology built in byte mode could never acquire it, and
     * a later pass in a float carrier had nothing to upload from. There is one
     * owner now, sl_skybatch_pass_begin, which asks for it whenever the pass's
     * latched carrier needs a width the cached topology has not got. This
     * build leaves g_fstage as skb_free_cpu left it: NULL. */
    /* The capacities the writing pass below may not exceed. Recorded as state
     * so the bound is checked against a stored number rather than against the
     * reasoning that produced it. */
    g_uniq_cap = agg_u;
    g_idx_cap  = agg_i;

    /* The element buffer's byte size, as one checked conversion rather than a
     * cast inside the call. */
    {
        const size_t cbytes = (size_t) agg_i * esz;
        if (cbytes / esz != (size_t) agg_i                /* the product wrapped */
            || cbytes > (size_t) 0x7ffffffful) {          /* past ptrdiff_t */
            free(compact); skb_free_cpu(); return 0;
        }
        g_elem_bytes = cbytes;
    }

    /* ---- PASS TWO: write the unique list, the compact indices and each
     * mesh's last slot. The stamp array is re-walked rather than kept per mesh,
     * which is what keeps the CPU storage one slot per vertex instead of one
     * per mesh and vertex. Every write is bounded against the capacity
     * recorded above, so the safety is local to the store and does not rest on
     * the two passes agreeing. */
    memset(g_stamp, 0, (size_t) a->vertex_count * sizeof *g_stamp);
    for (m = 0; m < a->mesh_count; m++) {
        const struct sl_wa_mesh *mh = &a->meshes[m];
        unsigned n = 0u, slot = 0u;
        for (k = 0; k < mh->index_count; k++) {
            const unsigned vi = a->indices[mh->first_index + k];
            if (g_stamp[vi] != m + 1u) {
                g_stamp[vi] = m + 1u;
                g_slot[vi]  = n;
                if ((unsigned long) g_uoff[m] + n >= g_uniq_cap) {
                    free(compact); skb_free_cpu(); return 0;
                }
                g_uniq[g_uoff[m] + n] = vi;
                wrote_u++;
                n++;
            }
            slot = g_slot[vi];
            if ((unsigned long) g_ioff[m] + k >= g_idx_cap) {
                free(compact); skb_free_cpu(); return 0;
            }
            if (g_idx16) ((unsigned short *) compact)[g_ioff[m] + k] = (unsigned short) slot;
            else         ((unsigned *) compact)[g_ioff[m] + k]       = slot;
            wrote_i++;
        }
        /* P2: the slot of this mesh's LAST index, so the draw can take the
         * current colour and coordinate from the record it already prepared
         * instead of formatting that vertex a second time. */
        g_lastslot[m] = slot;
    }
    g_uniq_used = wrote_u;
    g_idx_used  = wrote_i;

    /* The element buffer is immutable for the asset, so it is uploaded ONCE
     * and never again - the per-draw traffic is the prepared attributes
     * alone. The vertex buffer is left unsized here: each draw respecifies it
     * at that mesh's own width, which is what keeps a sparse mesh from paying
     * for the whole asset's vertices. */
    p_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ib);
    p_BufferData(GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t) g_elem_bytes,
                 (const GLvoid *) compact, GL_STATIC_DRAW);
    p_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    free(compact);

    g_asset = a;
    g_c_items++;
    fprintf(stderr,
            "#316 skybatch: skydome/%s topology - %u vertices, %u indices, "
            "%u meshes, %lu aggregate indices, %lu unique references over all "
            "meshes, widest mesh %u, %s indices\n",
            a->path, a->vertex_count, a->index_count, a->mesh_count,
            agg_i, agg_u, g_umax, g_idx16 ? "u16" : "u32");
    return 1;
}

/* ---- the GL state bracket ----------------------------------------------- */

/* THE SAVE IS SEPARATE FROM THE INSTALL, and the separation is not tidiness.
 *
 * Building an asset's topology UPLOADS its element buffer, which means binding
 * GL_ELEMENT_ARRAY_BUFFER and putting it back to zero. Done before the save,
 * that is the binding the save then records - so the pass restored zero
 * instead of whatever the interpreter had bound, on exactly the first pass of
 * every asset. The offline suite caught it (case "state", first difference
 * GL_ELEMENT_ARRAY_BUFFER_BINDING), which is why the save now happens before
 * anything in this module writes a binding, and the topology build sits
 * between the save and the install with a restore on its failure path.
 *
 * `units` is GL_MAX_TEXTURE_UNITS, queried ONCE by the caller. The whole
 * bracket is one save and one restore per sky pass, and nothing here caches a
 * query across passes: the state is re-read every pass, because the pass
 * before it did not own it. */
static void skb_save(GLint units)
{
    GLint u;

    memset(&g_save, 0, sizeof g_save);

    if (units < 1) units = 1;                 /* a context that will not say */
    if (units > SKB_UNITS) units = SKB_UNITS; /* pass_begin refused this case */
    g_save.units = units;

    glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &g_save.client_unit);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &g_save.arr_buf);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &g_save.elem_buf);

    /* The position array. Its pointer is an OFFSET when a buffer was bound
     * into it, which is why the per-array binding is saved beside it: the two
     * are only meaningful together, and the restore re-binds before it calls
     * the Pointer command, which is what records the binding back into the
     * array. */
    g_save.v_on = glIsEnabled(GL_VERTEX_ARRAY);
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE,   &g_save.v_size);
    glGetIntegerv(GL_VERTEX_ARRAY_TYPE,   &g_save.v_type);
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE, &g_save.v_stride);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &g_save.v_buf);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &g_save.v_ptr);

    g_save.c_on = glIsEnabled(GL_COLOR_ARRAY);
    glGetIntegerv(GL_COLOR_ARRAY_SIZE,   &g_save.c_size);
    glGetIntegerv(GL_COLOR_ARRAY_TYPE,   &g_save.c_type);
    glGetIntegerv(GL_COLOR_ARRAY_STRIDE, &g_save.c_stride);
    glGetIntegerv(GL_COLOR_ARRAY_BUFFER_BINDING, &g_save.c_buf);
    glGetPointerv(GL_COLOR_ARRAY_POINTER, &g_save.c_ptr);

    /* EVERY unit's coordinate array, not just the one this pass writes. An
     * array left enabled on unit 3 by another pass is still dereferenced by
     * the draw below, so each is saved and each is turned off. */
    for (u = 0; u < units; u++) {
        p_ClientActiveTexture((GLenum) (GL_TEXTURE0 + u));
        g_save.t_on[u] = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_SIZE,   &g_save.t_size[u]);
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_TYPE,   &g_save.t_type[u]);
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_STRIDE, &g_save.t_stride[u]);
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING, &g_save.t_buf[u]);
        glGetPointerv(GL_TEXTURE_COORD_ARRAY_POINTER, &g_save.t_ptr[u]);
    }

    /* The arrays this module never sets. Their ENABLES are what matter: an
     * enabled array feeds the draw from a pointer this pass knows nothing
     * about. The tokens are legal arguments to the GL 1.1 client-state
     * commands whatever the header's version, so no entry point is needed for
     * the post-1.1 pair. */
    g_save.n_on = glIsEnabled(GL_NORMAL_ARRAY);
    g_save.i_on = glIsEnabled(GL_INDEX_ARRAY);
    g_save.e_on = glIsEnabled(GL_EDGE_FLAG_ARRAY);
    g_save.s_on = glIsEnabled(GL_SECONDARY_COLOR_ARRAY);
    g_save.f_on = glIsEnabled(GL_FOG_COORD_ARRAY);
    g_save.held = 1;
}

/* The state this module wants, installed over the saved one. Nothing here
 * reads GL back: everything it needs was taken by skb_save. */
static void skb_install(void)
{
    GLint u;
    const GLint units = g_save.units;

    if (g_save.n_on) glDisableClientState(GL_NORMAL_ARRAY);
    if (g_save.i_on) glDisableClientState(GL_INDEX_ARRAY);
    if (g_save.e_on) glDisableClientState(GL_EDGE_FLAG_ARRAY);
    if (g_save.s_on) glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
    if (g_save.f_on) glDisableClientState(GL_FOG_COORD_ARRAY);

    for (u = units - 1; u >= 1; u--) {
        p_ClientActiveTexture((GLenum) (GL_TEXTURE0 + u));
        if (g_save.t_on[u]) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    /* UNIT 0, and it is not a preference: glTexCoord* is defined as
     * glMultiTexCoord* on GL_TEXTURE0, so the array form of what the immediate
     * loop submitted is unit 0's coordinate array and no other. */
    p_ClientActiveTexture((GLenum) GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    /* The element binding is set once for the pass; the attribute binding and
     * the three pointers are set per draw, because glBufferData respecifies
     * the storage each time. */
    p_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ib);
}

static void skb_restore(void)
{
    GLint u;
    if (!g_save.held) return;
    g_save.held = 0;

    /* The current colour and texture coordinate first, while this module's own
     * arrays are still the ones enabled: glDrawElements left both undefined,
     * and the immediate loop would have left them at the last index's values.
     * sky_draw's D-046 horizon fill reads the current colour it sets itself,
     * but nothing may be left in an undefined state for the list that resumes
     * after this pass. */
    if (g_have_last) {
        glColor4ub(g_last.col[0], g_last.col[1], g_last.col[2], g_last.col[3]);
        glTexCoord4f(g_last.st[0], g_last.st[1], g_last.st[2], g_last.st[3]);
    }

    /* Every unit's coordinate array, pointer and binding together. */
    for (u = 0; u < g_save.units; u++) {
        p_ClientActiveTexture((GLenum) (GL_TEXTURE0 + u));
        p_BindBuffer(GL_ARRAY_BUFFER, (GLuint) g_save.t_buf[u]);
        glTexCoordPointer((GLint) g_save.t_size[u], (GLenum) g_save.t_type[u],
                          (GLsizei) g_save.t_stride[u], g_save.t_ptr[u]);
        if (g_save.t_on[u]) glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        else                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    /* The inherited unit, put back last so nothing above can leave it moved. */
    p_ClientActiveTexture((GLenum) g_save.client_unit);

    p_BindBuffer(GL_ARRAY_BUFFER, (GLuint) g_save.v_buf);
    glVertexPointer((GLint) g_save.v_size, (GLenum) g_save.v_type,
                    (GLsizei) g_save.v_stride, g_save.v_ptr);
    if (g_save.v_on) glEnableClientState(GL_VERTEX_ARRAY);
    else             glDisableClientState(GL_VERTEX_ARRAY);

    p_BindBuffer(GL_ARRAY_BUFFER, (GLuint) g_save.c_buf);
    glColorPointer((GLint) g_save.c_size, (GLenum) g_save.c_type,
                   (GLsizei) g_save.c_stride, g_save.c_ptr);
    if (g_save.c_on) glEnableClientState(GL_COLOR_ARRAY);
    else             glDisableClientState(GL_COLOR_ARRAY);

    if (g_save.n_on) glEnableClientState(GL_NORMAL_ARRAY);
    if (g_save.i_on) glEnableClientState(GL_INDEX_ARRAY);
    if (g_save.e_on) glEnableClientState(GL_EDGE_FLAG_ARRAY);
    if (g_save.s_on) glEnableClientState(GL_SECONDARY_COLOR_ARRAY);
    if (g_save.f_on) glEnableClientState(GL_FOG_COORD_ARRAY);

    /* The two user-controlled bindings last: the per-array restores above each
     * moved GL_ARRAY_BUFFER, so this is what puts the binding the pass
     * inherited back where it was. */
    p_BindBuffer(GL_ARRAY_BUFFER, (GLuint) g_save.arr_buf);
    p_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint) g_save.elem_buf);
}

/* ---- the pass ----------------------------------------------------------- */

int sl_skybatch_pass_begin(const struct sl_wa_asset *a, const float *clip)
{
    GLint units = 1;

    g_pass = 0;
    g_have_last = 0;
    g_pass_fmode = SL_SKYBATCH_FLOAT_OFF;
    if (!sl_skybatch_on()) return 0;
    if (a == NULL || clip == NULL) return 0;
    if (!skb_ready()) return 0;

    /* THE LATCH. One question to the policy per pass, and every draw of this
     * pass reads the answer from here. See g_pass_fmode: the stride, the
     * record size and the pointer offsets all follow the carrier, so a
     * carrier that changed between two meshes of one pass would describe the
     * buffer wrongly to the draw. Asked after skb_ready, so a pass that is
     * going to be refused for a missing entry point does not latch anything. */
    g_pass_fmode = sl_perf_sky_carrier();

    /* THE UNIT COUNT IS A PRECONDITION, not a clamp. A context with more
     * fixed-function units than the sweep can hold would leave the units past
     * the ceiling enabled and feeding the draw, so the pass is refused
     * instead - a refusal before any submission, which is the only safe
     * direction. */
    glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
    if (units > SKB_UNITS) {
        if (!g_said_off) {
            g_said_off = 1;
            fprintf(stderr, "#316 skybatch: %d fixed-function texture units "
                            "exceeds the %d this pass can account for - the "
                            "skydome draws through the immediate path\n",
                    (int) units, SKB_UNITS);
        }
        g_c_refused++;
        return 0;
    }

    /* THE SAVE COMES FIRST, before anything in this module writes a GL
     * binding - and the topology build below writes one, because uploading an
     * element buffer means binding it. Everything after this point restores
     * on its way out. */
    skb_save(units);

    if (!skb_topology(a)) {
        skb_restore();                 /* the topology build may have bound */
        if (!g_said_off) {
            g_said_off = 1;
            fprintf(stderr, "#316 skybatch: skydome/%s topology refused or "
                            "unallocatable - the skydome draws through the "
                            "immediate path\n", a->path);
        }
        g_c_refused++;
        return 0;
    }
    if (g_stage == NULL || g_umax == 0u) {
        skb_restore();
        g_c_refused++;
        return 0;
    }
    /* AN ACTIVE FLOAT CARRIER NEEDS ITS STAGING AT THE FULL WIDTH, and this is
     * where it is acquired. A topology cached while the carrier was byte
     * carries none, and a topology built for a narrower asset carries too
     * little; either way the allocation is attempted HERE, before any draw,
     * and only an allocation that FAILS refuses the pass. That is what makes
     * the row's OFF -> ON on a stage already loaded take effect at the next
     * pass instead of being refused for the life of the asset.
     *
     * An allocation failure is the original immediate path for this pass, not
     * a degraded one: nothing has been submitted at this point, so the
     * caller's own loop draws the whole dome. */
    if (g_pass_fmode > 0 && (g_fstage == NULL || g_fcap < g_umax)
        && !skb_float_alloc()) {
        skb_restore();
        if (!g_said_off) {
            g_said_off = 1;
            fprintf(stderr, "#474 skybatch: the float colour staging could not be "
                            "allocated for skydome/%s - the skydome draws through "
                            "the immediate path\n", a->path);
        }
        g_c_refused++;
        return 0;
    }

    g_clip = clip;
    skb_install();
    g_pass = 1;
    g_c_passes++;
    /* THE MARKER, once per process (g_fsaid is never cleared), on the first
     * pass that opened in an active float carrier. Absent in byte mode and
     * with the path off. One fixed grammar for exact parsing; it names the
     * LATCHED carrier, which is what this pass will upload, and no longer
     * quotes the developer switch as though that were the only way to reach
     * the carrier - PROD is now what the OPTIMIZED RENDERING row selects. */
    if (!g_fsaid && g_pass_fmode > 0) {
        g_fsaid = 1;
#ifdef SL_SKYBATCH_TEST
        g_fmarkers++;
#endif
        fprintf(stderr, "#474 skybatch: float colour carrier %s - served cloud "
                        "draws upload 48-byte records with a GL_FLOAT colour "
                        "(%s)\n",
                g_pass_fmode == SL_SKYBATCH_FLOAT_DIV ? "div" : "prod",
                g_pass_fmode == SL_SKYBATCH_FLOAT_DIV
                    ? "div: a diagnostic carrier, reachable only through "
                      "SL_SKY_BATCH_FLOAT_COLOR"
                    : "prod: the carrier whose values were checked against an "
                      "independent oracle, and the one the row selects");
    }
    return 1;
}

/* ---- r474: the two uploads ----------------------------------------------
 *
 * skb_byte_submit is the r405 module's upload and pointer block MOVED, not
 * edited: its body is that block's bytes (scripts/check_474.py compares
 * them), so byte mode binds, uploads n x 36 bytes and declares the same three
 * pointers, stride and offsets in the same order as before. */
static void skb_byte_submit(unsigned n, const struct sl_skybatch_shade *s)
{
    p_BindBuffer(GL_ARRAY_BUFFER, g_vb);
    p_BufferData(GL_ARRAY_BUFFER,
                 (ptrdiff_t) ((size_t) n * sizeof *g_stage),
                 (const GLvoid *) g_stage, GL_STREAM_DRAW);
    glVertexPointer(4, GL_FLOAT, (GLsizei) sizeof *g_stage,
                    (const GLvoid *) offsetof(struct sl_skybatch_vtx, clip));
    glColorPointer(4, GL_UNSIGNED_BYTE, (GLsizei) sizeof *g_stage,
                   (const GLvoid *) offsetof(struct sl_skybatch_vtx, col));
    /* The declared component count matches what the immediate path
     * submits for this material: the non-cloud branch above stages
     * (s, t, 0, 1) against a glTexCoord2f call, while the cloud branch
     * stages a projective q and needs all four. The stored record, the
     * 36-byte stride and the offsets are unchanged either way. */
    glTexCoordPointer(s->cloud_plane ? 4 : 2, GL_FLOAT,
                      (GLsizei) sizeof *g_stage,
                      (const GLvoid *) offsetof(struct sl_skybatch_vtx, st));
}

/* skb_float_submit is the experiment. Each float record is built from the
 * prepared byte record in the same slot - the position and texture coordinate
 * by memcpy, so their bits cannot change, and the colour through
 * sl_skybatch_float_colour - and then the same calls, in the same order, as
 * the byte upload, with a 48-byte stride, offsets 0 / 16 / 32 and a GL_FLOAT
 * colour. The caller checked n <= g_fcap, and skb_float_bytes bounded
 * g_fcap * 48 when the staging was allocated. */
static void skb_float_submit(unsigned n, int fm, const struct sl_skybatch_shade *s)
{
    unsigned j;
    for (j = 0; j < n; j++) {
        memcpy(g_fstage[j].clip, g_stage[j].clip, sizeof g_fstage[j].clip);
        (void) sl_skybatch_float_colour(fm, g_stage[j].col, g_fstage[j].col);
        memcpy(g_fstage[j].st, g_stage[j].st, sizeof g_fstage[j].st);
    }
    p_BindBuffer(GL_ARRAY_BUFFER, g_vb);
    p_BufferData(GL_ARRAY_BUFFER,
                 (ptrdiff_t) ((size_t) n * sizeof *g_fstage),
                 (const GLvoid *) g_fstage, GL_STREAM_DRAW);
    glVertexPointer(4, GL_FLOAT, (GLsizei) sizeof *g_fstage,
                    (const GLvoid *) offsetof(struct sl_skybatch_fvtx, clip));
    glColorPointer(4, GL_FLOAT, (GLsizei) sizeof *g_fstage,
                   (const GLvoid *) offsetof(struct sl_skybatch_fvtx, col));
    glTexCoordPointer(s->cloud_plane ? 4 : 2, GL_FLOAT,
                      (GLsizei) sizeof *g_fstage,
                      (const GLvoid *) offsetof(struct sl_skybatch_fvtx, st));
}

int sl_skybatch_mesh(unsigned mesh, const struct sl_skybatch_shade *s)
{
    const struct sl_wa_asset *a = g_asset;
    unsigned n, j, ic;
    const unsigned *uq;
    size_t esz, off;
    int fm;                            /* r474: the float mode */

    if (!g_pass || a == NULL || s == NULL || mesh >= a->mesh_count) {
        /* Nothing was submitted for this mesh, so closing the pass here and
         * letting the caller draw it immediately cannot duplicate a triangle. */
        /* r380(a), ported: an ACTIVE pass falling back here is a REAL
         * refusal - the caller draws this mesh immediately instead - so it
         * is counted. The increment sits INSIDE the g_pass branch
         * deliberately: a call made with no pass open, which is what the
         * default-off API path does, is an uncounted no-op and must stay
         * one. */
        if (g_pass) { skb_restore(); g_pass = 0; g_c_mesh_refused++; }
        return 0;
    }

    ic = a->meshes[mesh].index_count;
    if (ic == 0u) return 1;            /* nothing to draw, and nothing to fall
                                        * back to: the immediate loop's own
                                        * loop body would not run either */

    /* r398 THE DELIBERATE DECLINE. This candidate batches the CLOUD material
     * only; a plain material's draw is left to the caller's immediate loop,
     * whose command sequence for that mesh is then exactly the default-off
     * one. Decided here: AFTER the closed-pass and invalid-argument guard
     * above, so a decline is only ever answered with a pass open; AFTER the
     * empty-mesh answer, because an empty mesh submits nothing either way and
     * writes no current attribute, so it keeps answering 1 and leaves
     * g_have_last alone; and BEFORE anything is prepared, uploaded, bound or
     * drawn - this branch makes no GL call and touches no staging.
     *
     * The pass stays OPEN, so a later cloud mesh is still batched, and the
     * caller must not close it (sl_gfx_skybatch.h, SL_SKYBATCH_DECLINED).
     *
     * g_have_last is CLEARED because the immediate loop is about to define the
     * current colour and texture coordinate itself, from this mesh's last
     * index. A record kept from an EARLIER batched mesh would otherwise be
     * restored over them at pass end - the cloud -> plain order - and leave
     * the list that resumes after the pass different from what the
     * default-off path leaves. A later batched mesh sets it again from its own
     * last index, which is then the last submission of the pass. */
    if (s->cloud_plane == 0) {
        g_c_declined++;
        g_c_dec_vtx += (unsigned long) ic;
        g_have_last = 0;
        return SL_SKYBATCH_DECLINED;
    }

    n  = g_uoff[mesh + 1u] - g_uoff[mesh];
    uq = &g_uniq[g_uoff[mesh]];
    if (n == 0u || n > g_umax) {
        /* r380(b): a defensive invariant branch, and still a real
         * active-pass refusal, so it is counted on the same footing
         * as every other one. */
        skb_restore(); g_pass = 0; g_c_mesh_refused++;
        return 0;
    }

    /* THE PASS'S LATCHED CARRIER, not a fresh question: a press between two
     * meshes of one pass must not change the record size the draw was told
     * about. In an active carrier the staging must hold this mesh; pass_begin
     * acquired it and refused otherwise, and this restates the invariant where
     * it is used, decided before anything is prepared or sent. */
    fm = g_pass_fmode;
    if (fm > 0 && (g_fstage == NULL || n > g_fcap)) {
        skb_restore(); g_pass = 0; g_c_mesh_refused++;
        return 0;
    }

    /* ONE PREPARED RECORD PER UNIQUE REFERENCED VERTEX, under THIS material's
     * state. Nothing survives from the previous draw: g_stage is overwritten
     * for every slot this mesh uses before any of it is uploaded, so a vertex
     * two materials share cannot carry the first material's shade or a
     * previous frame's cloud phase into the second. */
    for (j = 0; j < n; j++)
        sl_skybatch_attr(&a->vertices[uq[j]], &g_clip[(size_t) uq[j] * 4u],
                         s, &g_stage[j]);

    esz = g_idx16 ? sizeof(unsigned short) : sizeof(unsigned);
    off = (size_t) g_ioff[mesh] * esz;

    /* glBufferData RESPECIFIES the storage rather than writing into storage a
     * draw already in flight may be reading, so the per-draw traffic is this
     * mesh's own prepared records and nothing more - no whole-asset upload for
     * a sparse mesh, and no growth with the pass's length. */
    if (fm > 0) skb_float_submit(n, fm, s);   /* r474: the experiment     */
    else        skb_byte_submit(n, s);        /* the r405 upload, moved  */

    glDrawElements(GL_TRIANGLES, (GLsizei) ic,
                   g_idx16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                   (const GLvoid *) off);

    /* The last index's record, for the current colour and coordinate the
     * restore puts back - the values the immediate loop would have left.
     *
     * TAKEN FROM THE RECORD ALREADY PREPARED, not formatted again. The loop
     * above prepares every unique referenced vertex exactly once, and the
     * last index necessarily names one of them, so its record is already in
     * g_stage; g_lastslot[mesh] is the slot the topology build recorded for it.
     * Formatting it a second time (the previous revision) made the pass call
     * the formatter n + 1 times for n unique vertices, which both broke the
     * once-per-unique contract this candidate exists to keep and understated
     * nothing while overstating the work done. The slot is bounded against
     * this mesh's own width before it is used. */
    {
        const unsigned ls = g_lastslot[mesh];
        if (ls < n) {
            g_last = g_stage[ls];
            g_have_last = 1;
        }
    }

    g_c_draws++;
    g_c_idx  += (unsigned long) ic;
    g_c_uniq += (unsigned long) n;
    return 1;
}

void sl_skybatch_pass_end(void)
{
    if (!g_pass) return;               /* already closed by a refusal */
    skb_restore();
    g_pass = 0;
    g_pass_fmode = SL_SKYBATCH_FLOAT_OFF;   /* no pass open carries no carrier */
    /* At the END of the pass, not its start: counted at the start the line
     * reports the pass's own draws as zero, which is a line that says
     * nothing. */
    if (g_asset != NULL) skb_note(g_asset);
    g_clip = NULL;
}

/* ---- lifecycle ---------------------------------------------------------- */

void sl_skybatch_context_invalidate(void)
{
    /* NO GL HERE. Every name below belonged to the context being left, so
     * neither using nor DELETING one is legal now; they are simply dropped,
     * and the next pass resolves its entry points and creates its buffers
     * against the context that is current then. */
    g_vb = g_ib = 0u;
    g_state = 0;
    g_pass = 0;
    g_pass_fmode = SL_SKYBATCH_FLOAT_OFF;
    g_have_last = 0;
    g_said_off = 0;
    g_said_armed = 0;
    memset(&g_save, 0, sizeof g_save);
    p_GenBuffers = NULL; p_DeleteBuffers = NULL; p_BindBuffer = NULL;
    p_BufferData = NULL; p_ClientActiveTexture = NULL;
    skb_free_cpu();
}

void sl_skybatch_shutdown(void)
{
    /* A pass can only be open if the caller was interrupted mid-pass; close it
     * first so the state it saved is put back while the context still exists. */
    if (g_pass) { skb_restore(); g_pass = 0; g_pass_fmode = SL_SKYBATCH_FLOAT_OFF; }
    if (g_state > 0 && p_DeleteBuffers != NULL) {
        if (g_vb != 0u) p_DeleteBuffers(1, &g_vb);
        if (g_ib != 0u) p_DeleteBuffers(1, &g_ib);
    }
    /* r398: the r386 D2 condition, ported, with the declined count added. A
     * mesh refusal or a deliberate decline is a real outcome, so a run whose
     * only event was one must still print its line. (Every decline happens
     * inside a counted pass, so g_c_passes already covers it; the term states
     * the property instead of relying on that.) */
    if (g_c_passes != 0u || g_c_refused != 0u
        || g_c_mesh_refused != 0u || g_c_declined != 0u) sl_skybatch_report(stderr);
    g_vb = g_ib = 0u;
    g_state = 0;
    g_have_last = 0;
    p_GenBuffers = NULL; p_DeleteBuffers = NULL; p_BindBuffer = NULL;
    p_BufferData = NULL; p_ClientActiveTexture = NULL;
    skb_free_cpu();
}

#endif /* !__sgi */
