/**
 * SYNTHETIC GL FOR THE BACKGROUND DRAW SEAM, offline - the fixture that drives
 * the REAL src/gfx/sl_gfx_dl.c against the REAL src/gfx/sl_gfx_bgtf.c.
 *
 * EVERY VALUE THIS SHIM PRODUCES IS FABRICATED. No window, no native graphics
 * API, no driver, no game. Nothing here links against opengl32 or SDL, so a
 * test built with it cannot reach a GPU even by accident.
 *
 * WHY A SECOND SYNTHETIC GL AND NOT sl_gfx_bgtf_testgl.h. That shim suppresses
 * <windows.h> and <GL/gl.h> and supplies its own types, tokens and the four
 * GL 1.1 entry points sl_gfx_bgtf.c calls. sl_gfx_dl.c calls sixty-six, uses
 * hundreds of tokens, and needs the real <windows.h> for VirtualQuery and the
 * performance counter - so suppressing the host headers for it is not
 * practical, and a near-copy of them would be a second maintained
 * transcription of the registry.
 *
 * HOW THE REAL HEADERS ARE KEPT AND THE DRIVER STILL REPLACED. The fixture
 * compiles with -D_GDI32_. mingw's wingdi.h reads
 *
 *     #ifdef _GDI32_
 *     #define WINGDIAPI
 *     #else
 *     #define WINGDIAPI DECLSPEC_IMPORT
 *     #endif
 *
 * (mingw32/include/wingdi.h:13-16, read out of the installed tree rather than
 * assumed), and GL/gl.h decorates every entry point with WINGDIAPI. With the
 * macro empty, glEnable is an ordinary undefined symbol instead of an import
 * from opengl32, and the definitions in sl_gfx_seamgl.c are what the link
 * resolves it to. So the real headers supply every type and token - no
 * transcription to drift - while this file supplies every entry point.
 *
 * THAT ALSO CHECKS THE CALLING CONVENTION, which the other shim's note records
 * it cannot: APIENTRY here is windows.h's own __stdcall rather than empty, so
 * the module's typedefs, sl_gfx_dl.c's direct calls and these definitions all
 * agree on a callee-cleaned frame. A mismatch would corrupt the caller's own
 * frame a few calls in, which is the measurement the module's header cites.
 *
 * WHAT THIS SHIM MODELS, and it is only what the seam's claims are about:
 *
 *   - the current program, the array and element bindings, the indexed
 *     transform-feedback binding and rasterizer discard;
 *   - the POSITION ARRAY as the last pointer command left it, with the array
 *     buffer that was bound AT THE TIME IT RAN - which is what makes a
 *     buffer-object pointer's argument an offset rather than an address;
 *   - the colour, texture-coordinate and normal arrays the same way, because
 *     the candidate's claim is that it moves ONLY the position array;
 *   - every buffer's byte size, and the allocation controls below;
 *   - and an ORDERED RECORD OF EVERY DRAW, each capturing the whole vertex
 *     source in force when it ran.
 *
 * THE DRAW RECORD IS THE POINT. "No stale position source survives" is not a
 * property of a counter: it is a property of what each individual draw read.
 * So every glDrawElements and glDrawArrays stores the position pointer's
 * buffer, offset, size, type and stride as they stood, and a test asks of each
 * draw which source it actually used rather than asking what the last call was.
 *
 * WHAT IS ABSENT IS PART OF THE CHECK. No glFinish, glFlush, glMapBuffer,
 * glUnmapBuffer, glGetBufferSubData, glGetError, and no fence or sync entry
 * point appears in sl_gl_proc's table here, so a resolve of one answers NULL.
 * glFinish, glFlush, glGetError and glReadPixels DO exist as symbols, because
 * sl_gfx_dl.c and sl_gfx_bgtf.c call them on paths this fixture does not drive
 * and the link needs them - which is why the read-back claim about
 * sl_gfx_bgtf.c is proven by the OTHER fixture, whose link provides no
 * read-back at all, and is not claimed here. glFlush is the newest of the four
 * and the only one of them this fixture CANNOT reach at all: it belongs to the
 * opt-in fence synchronisation mode (SL_BG_TF_AUX_SYNC), which needs a second
 * GL context this fixture does not model, so its definition here is inert and
 * exists for the link. See the note above it in sl_gfx_seamgl.c.
 *
 * glFinish IS NOW OBSERVED RATHER THAN INERT, and the symbol it already had is
 * the reason nothing about the link changed. The post-reset boundary control
 * (SL_BG_TF_POST_RESET) issues one glFinish at a point whose whole content is
 * where it sits, so the call is counted and its place in the call order is
 * kept; see the note above its definition in sl_gfx_seamgl.c for how that is
 * done without editing the generated inert include. It remains absent from
 * sl_gl_proc's table, because it is a GL 1.0 core export the module calls
 * directly, exactly as it calls glGetError.
 *
 * glGetError IS DELIBERATELY NOT RESOLVABLE even though the symbol exists. It
 * is a GL 1.0 core export that the buffer diagnostic calls directly, and an
 * entry in the resolver's table would be dead while quietly permitting a
 * regression to the extension-loader form - which on a driver whose
 * wglGetProcAddress answers only for extensions leaves the error state
 * permanently unobservable rather than observed to be clean.
 */
#ifndef SL_GFX_SEAMGL_H
#define SL_GFX_SEAMGL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stddef.h>

/* The post-1.1 tokens the fixture names when it reads the model. The real
 * GL/gl.h stops at 1.1, and src/gfx guards its own copies with #ifndef, so one
 * definition of each stands in the translation unit.
 *
 * THE FIRST FOUR ARE ONE GROUP AND MUST STAY ONE GROUP. sl_gfx_dl.c guards all
 * four behind a single `#ifndef GL_ARRAY_BUFFER` (its buffer-object block), so
 * defining only some of them here leaves the rest UNDEFINED in that
 * translation unit rather than defined twice - the whole group is skipped on
 * the strength of the first name. Measured: defining three of the four stopped
 * the build at GL_STREAM_DRAW, which is the loud failure and the reason this
 * note exists rather than a tidier per-token list. */
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER               0x8892
#define GL_ELEMENT_ARRAY_BUFFER       0x8893
#define GL_STREAM_DRAW                0x88E0
#define GL_STATIC_DRAW                0x88E4
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER
#define GL_TRANSFORM_FEEDBACK_BUFFER  0x8C8E
#endif
#ifndef GL_RASTERIZER_DISCARD
#define GL_RASTERIZER_DISCARD         0x8C89
#endif
#ifndef GL_BUFFER_SIZE
#define GL_BUFFER_SIZE                0x8764
#endif
/* The post-1.1 client-array tokens. The real <GL/gl.h> this fixture keeps
 * supplies the GL 1.1 six (GL_VERTEX_ARRAY through GL_EDGE_FLAG_ARRAY), so only
 * these six need a fallback; the values are the ones
 * src/gfx/sl_gfx_skybatch.c:131-166 records having read out of glext.h in this
 * same MSYS2 tree, plus GL_MAX_TEXTURE_COORDS from glext.h:649 - the COORDINATE
 * SET count, which is the domain the module's sweep is bounded by and which the
 * OpenGL 2.1 spec allows to exceed the conventional unit count. */
#ifndef GL_CLIENT_ACTIVE_TEXTURE
#define GL_CLIENT_ACTIVE_TEXTURE      0x84E1
#define GL_MAX_TEXTURE_UNITS          0x84E2
#endif
#ifndef GL_MAX_TEXTURE_COORDS
#define GL_MAX_TEXTURE_COORDS         0x8871
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                   0x84C0
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY
#define GL_SECONDARY_COLOR_ARRAY      0x845E
#endif
#ifndef GL_FOG_COORD_ARRAY
#define GL_FOG_COORD_ARRAY            0x8457
#endif

#define SEAMGL_DRAWS   512
#define SEAMGL_BUFFERS  64
/* The module's own unit ceiling (TF_UNITS in src/gfx/sl_gfx_bgtf.c), pinned here
 * rather than read because the module does not export it. One more than the
 * ceiling is modelled so a scenario can report a count the module must REFUSE. */
#define SEAMGL_UNITS   16
#define SEAMGL_UNITCAP (SEAMGL_UNITS + 1)

/* THE CLIENT-ARRAY ENABLES, which is the state the position-only-input control
 * (SL_BG_TF_POS_INPUT) narrows around the prepass draw and puts back.
 *
 * Modelled rather than ignored because that control's whole claim is about which
 * arrays a DRAW dereferenced, and this shim is the only thing that can say. The
 * coordinate arrays are per unit, indexed by the client active unit in force at
 * the call - which is how GL works, and is what makes a wrong-unit restore
 * visible instead of merely suspected. */
struct seamgl_client {
    GLint     unit;                /* GL_CLIENT_ACTIVE_TEXTURE, as the enum */
    GLboolean t[SEAMGL_UNITCAP];   /* per-unit coordinate arrays            */
    GLboolean v, c, n, i, e, s, f; /* vertex, colour, normal, index,
                                    * edge flag, secondary colour, fog coord */
};

/* One client-array pointer, as the pointer command left it. `buf` is the array
 * buffer bound WHEN THE COMMAND RAN, which decides whether `off` is an offset
 * into that buffer or a host address. */
struct seamgl_ptr {
    int     set;
    GLint   size;
    GLenum  type;
    GLsizei stride;
    long    off;
    GLuint  buf;
};

/* ONE DRAW, with the whole vertex source it actually read.
 *
 * `pos` is the position array in force at the draw, not at the end of the
 * walk: a stale source is a property of the draw that read it, and nothing
 * sampled afterwards can recover which draw was wrong. */
struct seamgl_draw {
    int    arrays;                 /* 1 glDrawArrays, 0 glDrawElements   */
    GLenum mode;
    long   count;
    long   first;                  /* glDrawArrays only                  */
    long   elem_off;               /* glDrawElements' index argument     */
    GLuint elem_buf;               /* the element binding at the draw    */
    GLuint program;                /* the program current at the draw    */
    int    discard;
    /* WHETHER A FEEDBACK SCOPE WAS OPEN AT THE DRAW, sampled here for the
     * reason `program` and `discard` are: SL_BG_TF_EMPTY_STATE's rows differ in
     * exactly these three at exactly this point and each row restores the
     * common bracket's state immediately after, so the three read later are
     * identical on every row. A pool BINDING is not a capture - a T0 row keeps
     * the indexed binding and omits the begin/end pair - so tf_buf alone cannot
     * answer this. */
    int    tf_active;
    GLuint tf_buf;
    /* WHICH CONTEXT THIS DRAW LANDED ON: 0 main, 1 auxiliary, -1 none. The
     * auxiliary-context control (SL_BG_TF_AUX_EMPTY) issues its empty draw on a
     * second context, and "one draw, issued THERE" is not a property of any
     * argument - so it is sampled per draw here, beside the three conditions
     * above and for the same reason: the control switches back immediately
     * afterwards, so a context read at the end of the walk describes the
     * restore. */
    int    ctx;
    long   seq;                    /* where this draw sits in call order */
    GLuint arr_buf;                /* the generic array binding at it    */
    struct seamgl_ptr pos, col, uv, nrm;
    /* THE CLIENT ARRAYS AS THIS DRAW FOUND THEM, sampled at the draw and not
     * afterwards: "the prepass draw read positions only" is a property of that
     * draw, and state read at the end of a walk has already been restored. */
    struct seamgl_client cli;
};

struct seamgl {
    /* ---- what the fake driver refuses ------------------------------- */
    const char *withhold_proc;     /* this entry point does not resolve  */
    int  gen_buffers_zero;         /* glGenBuffers hands back 0          */
    /* THE STORAGE MODEL, as in sl_gfx_bgtf_testgl.h: a buffer NAME is not
     * allocated storage, so what a glBufferData INSTALLS is chosen apart from
     * what it was asked for, and the GL_BUFFER_SIZE query is what anything
     * downstream can learn the difference from.
     *
     * `alloc_short_name` narrows the injection to ONE buffer, which the seam
     * fixture needs: the pass's own vertex and index buffers are allocated
     * through the same entry point as the prepass pool, and failing all of
     * them would refuse the walk long before a prepass is reached. 0 means
     * every buffer. */
    int    alloc_short_initial;
    int    alloc_short_growth;
    long   alloc_bytes;
    GLuint alloc_short_name;
    /* HOW MANY QUALIFYING ALLOCATIONS TO LET THROUGH FIRST. "A prepass refusal
     * AFTER a success" needs the first pool of a walk to be allocated and the
     * second to fail, and a walk's pools are minted inside one call that no
     * test can interrupt - so the count is the control rather than the timing.
     * 0 shorts the first qualifying allocation. */
    int    alloc_short_skip;
    /* BUFFERS THE INJECTION MUST NEVER TOUCH. The pass's own vertex, index and
     * stream buffers are allocated through this same entry point, and failing
     * one of those refuses the walk outright - so a prepass-allocation failure
     * could not be reached at all without exempting them by name. */
    GLuint alloc_exempt[4];
    int    getbufparam_fail;

    /* THE PRODUCTION BUFFER DIAGNOSTIC'S READBACK (SL_BG_TF_DUMP). Default 0:
     * sl_gl_proc then answers NULL for glGetBufferSubData, which is both the
     * ordinary fixture and the shape of a driver without GL 1.5 buffer reads.
     *
     * WHAT THIS SHIM'S READ IS FOR, and what it is not. The property this
     * fixture owns is PLACEMENT and RESTORATION: the read happens after the
     * prepass's transform feedback ended, before the selected indexed draw,
     * exactly once, and leaves every binding and pointer as it found them. Byte
     * fidelity is the other fixture's (sl_gfx_bgtf_testgl.c), which models the
     * pool's contents; here the read fills the destination with a deterministic
     * pattern so a record exists and the ordering can be read off. */
    /* WHAT THE TWO TEXTURE LIMITS ANSWER. `units_reported` is
     * GL_MAX_TEXTURE_UNITS, the CONVENTIONAL fixed-function count, which the
     * module no longer reads; `coords_reported` is GL_MAX_TEXTURE_COORDS, the
     * COORDINATE SET count that the per-unit selector selects among and that the
     * sweep is bounded by. 0 means "report SEAMGL_UNITS" for either, so an
     * ordinary scenario gets the full sweep without naming a number; a scenario
     * sets the coordinate count to 1, to a middle value, or past the module's
     * ceiling to require the isolation to refuse. */
    long   units_reported;
    long   coords_reported;

    int    provide_readback;

    /* ---- what the shim observed ------------------------------------- */
    long calls, proc_calls;
    long draw_elements, draw_arrays, vertex_pointer, buffer_data, gen_buffers;
    long get_buffer_parameteriv, begin_tf, end_tf, use_program;
    /* The neutral-program control's teardown delete (SL_BG_TF_NEUTRAL_PROG) and
     * the name the last one named, so a case can require that the object
     * released was the control's own program rather than the prepass program
     * the module leaves to ordinary context destruction. An off launch creates
     * no program and must leave both at 0. */
    long   delete_program;
    GLuint last_deleted_program;
    /* And the same control's shader release, which the acquire issues on every
     * path it can leave - success included, where the linked program holds the
     * flagged object. An off launch creates no shader and must leave both at 0. */
    long   delete_shader;
    GLuint last_deleted_shader;
    /* ---- THE AUXILIARY GL CONTEXT, MODELLED (SL_BG_TF_AUX_EMPTY) --------
     *
     * This shim models a CAPABLE platform, exactly as it does for the two
     * deletes above: a second context is always granted, always shares object
     * names, and every switch succeeds. The refusal cases - no context, no
     * sharing, a switch that fails, a main context that cannot be restored -
     * belong to the module fixture, which can gate each one; what this fixture
     * owns is that the REAL caller's walk puts every empty draw on the second
     * context and hands the main one back, with the raster sequence unchanged.
     *
     * `ctx_cur` is 0 main, 1 auxiliary, and every GL call passes through one
     * funnel that counts the ones made on the auxiliary context, so a control
     * that issued more there than its own two commands and one draw would show
     * it here. `aux_exists` is 1 while a context the module must destroy is
     * alive; `aux_destroyed` is the teardown release, which must happen exactly
     * once and only while the main context is current. */
    /* `proc_calls_on_aux` is the entry points RESOLVED while the auxiliary
     * context was current. A pointer is only valid on the context it was
     * resolved on, so a control that calls on that context has to resolve there
     * too, and this count is the one place in this fixture where that is
     * visible. `fatal_calls` is the lost-context exit path: the product's does
     * not return, this one records and does, and this fixture's claim is that it
     * is NEVER reached - every shape here is a healthy platform. */
    long   proc_calls_on_aux;
    long   fatal_calls;
    int    aux_exists;
    int    ctx_cur;
    long   aux_created, aux_destroyed;
    long   aux_switches, main_switches;
    long   calls_on_aux, draws_on_aux;
    long   is_program_calls;
    GLuint is_program_last;
    int    is_program_on_aux;      /* 1 when the LAST one was asked on aux */
    long generated;
    long get_buffer_sub_data;      /* the diagnostic's readbacks         */
    long get_buffer_sub_bytes;     /* bytes they asked for               */
    long get_error_calls;
    /* glFinish calls ISSUED. The post-reset boundary control issues one on two
     * of its arms; an off launch must leave this 0, which is what a fixture
     * asserts in place of the absence this symbol never had. */
    long finish_calls;
    /* the position-only-input control's own call counts */
    long is_enabled;               /* client-state QUERIES made          */
    long client_state;             /* enables and disables together      */
    long client_enable, client_disable;
    long client_active_texture;    /* per-unit selections made           */
    long get_integerv;

    /* ---- the ORDER, as a monotonic sequence number --------------------
     *
     * "The read is after the transform feedback ended and before the draw" is a
     * claim about ORDER, and nothing else in this shim records order: the draw
     * record is per draw and the counters are totals. So one counter ticks on
     * each of the four calls that matter and each of them keeps the value it
     * saw, which makes the placement a comparison of three numbers rather than
     * an inference from three totals. */
    long seq;                      /* ticks on every ordered call        */
    long seq_last_end_tf;
    long seq_first_subdata, seq_last_subdata;
    /* Where the LAST glFinish sat. The post-reset boundary's placement claim is
     * ordinal - after the prepass's own restoration, before the raster draw this
     * function returns into - so the number is what makes it checkable against
     * the draw record's own `seq` values rather than inferred from totals. */
    long seq_last_finish;

    /* THE STATE THE READBACK MUST NOT DISTURB, sampled at the read itself: the
     * generic array binding it took, the indexed transform-feedback binding,
     * and the position pointer. A restoration is a property of what the DRAW
     * afterwards reads, and these are what say whether the read moved it. */
    GLuint subdata_arr_buf_at;     /* the array binding when it ran      */
    GLuint subdata_tf_buf_at;
    struct seamgl_ptr subdata_pos_at;

    /* ---- the modelled state ---------------------------------------- */
    GLuint current, arr_buf, elem_buf, tf_buf;
    int    discard, tf_active;
    struct seamgl_ptr pos, col, uv, nrm;
    /* The live client-array enables, which every draw snapshots. */
    struct seamgl_client cli;

    /* ---- violations: every case asserts each is zero ---------------- */
    long v_tf_overrun;             /* a prepass draw past its pool       */
    /* A GL CALL MADE WITH NO CONTEXT CURRENT. Unreachable in this fixture, which
     * models a platform where every switch succeeds, and asserted anyway: it is
     * what would catch a control that issued a call between a failed switch and
     * a restore if this shim ever grew a failure gate. */
    long v_call_no_context;
    long v_draw_without_discard;   /* a recording draw could rasterise   */
    long v_unknown_buffer;         /* a name this shim never handed out  */
    long v_client_unit_range;      /* a unit outside the modelled range  */
    long v_draws_full;             /* the draw record is incomplete      */
    long v_read_inside_tf;         /* a readback inside an open TF scope  */
    long v_program_change_in_tf;   /* glUseProgram inside a begin/end
                                    * scope, which the API forbids       */

    /* ---- the ordered draw record ----------------------------------- */
    int ndraws;
    struct seamgl_draw draw[SEAMGL_DRAWS];
};

extern struct seamgl sg;

/* A clean fake driver: GL's own default state, every counter zero, no buffer
 * names outstanding. */
void sl_seamgl_reset(void);

/* Zero the counters and the draw record, leaving the modelled state and the
 * buffer registry alone - a measurement window around one walk. */
void sl_seamgl_window(void);

/* The byte size this shim has recorded for a name, or -1 if it has none. */
long sl_seamgl_buffer_bytes(GLuint name);

/* The loader both modules resolve their post-1.1 names through. */
void *sl_gl_proc(const char *name);

#endif /* SL_GFX_SEAMGL_H */
