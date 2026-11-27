/**
 * SYNTHETIC GL for the background pass's GPU CLIP PREPASS, offline
 * (src/gfx/sl_gfx_bgtf.c).
 *
 * EVERY VALUE THIS HEADER'S IMPLEMENTATION PRODUCES IS FABRICATED. The shader
 * and program names, the buffer names, the compile and link statuses, the info
 * logs and the uniform locations are invented here and prove nothing about any
 * driver. What they do prove is the module's own logic: which entry points it
 * resolves, what it compiles, what it latches, what it submits, in which
 * ORDER, which state it changes and puts back, and what it refuses to do.
 *
 * There is NO window, NO native graphics API and NO game here. Plain C with no
 * link-time dependency on opengl32 or SDL, so a test built with it cannot reach
 * a GPU even by accident.
 *
 * HOW THIS SHIM GETS IN FRONT OF THE REAL GL HEADERS, and why that way.
 * src/gfx/sl_gfx_bgtf.c has NO test hook in its include block: it includes
 * <windows.h> under _WIN32 and then <GL/gl.h>, unconditionally, with no
 * SL_*_TEST macro around either. src/ may not be edited to add one. So this
 * header is injected ahead of the translation unit with `-include` and DEFINES
 * THE HOST HEADERS' OWN INCLUDE GUARDS, which makes the real headers expand to
 * nothing when the module asks for them:
 *
 *   _WINDOWS_, _INC_WINDOWS   mingw32/include/windows.h, lines 6-7 and 22-23,
 *                             the outer guard wrapping the whole file
 *   __gl_h_, __GL_H__         mingw32/include/GL/gl.h, lines 6-10, the two
 *                             nested guards wrapping the whole file
 *
 * Those four names were read out of the installed headers in this MSYS2 tree,
 * not assumed; sl_gfx_bggpu_testgl.h records the same four for the same reason.
 * The failure mode if a toolchain ever uses different guard names is LOUD, not
 * silent: the real headers expand, their GL typedefs and tokens collide with
 * the ones below, and the build stops.
 *
 * APIENTRY IS EMPTY HERE, so the stub and the module's typedefs are both cdecl
 * and agree. The calling-convention fact the module's own comment records (the
 * entry points are __stdcall on Windows, and a typedef without APIENTRY
 * corrupts the caller's frame) is therefore NOT exercised by this fixture; it
 * needs a real opengl32.
 *
 * STORAGE IS MODELLED SEPARATELY FROM THE REQUEST, which is the whole point of
 * the allocation controls below. glBufferData is void and a driver that cannot
 * honour a size leaves the object at whatever it had, so a fake driver that
 * always installs the requested size cannot express the failure the module
 * exists to refuse. Here the INSTALLED size and the REQUESTED size are two
 * numbers: `alloc_short_initial`, `alloc_short_growth` and `alloc_install`
 * decide what a glBufferData actually leaves on the object, and
 * `getbufparam_fail` / `getbufparam_bytes` decide what the GL_BUFFER_SIZE query
 * then reports. A test can therefore inject a short initial allocation, a short
 * growth, an allocation that installs nothing at all, a query that writes
 * nothing, and a query that reports less than was installed - independently.
 *
 * THE STUB IS ALSO A WATCHDOG. It keeps an ORDERED TRANSCRIPT of every entry
 * point call with its arguments, models the state a prepass and a raster
 * binding actually change - the current program, the two buffer bindings, the
 * indexed transform-feedback binding, rasterizer discard, the position array
 * and each buffer's byte size - and records a VIOLATION if a name from a
 * previous fake context is used after the module was told the context changed,
 * if any call arrives after the test marks the context destroyed, if a shader's
 * source is set on a name that was never created, if a transform-feedback draw
 * runs without rasterizer discard or without a recording buffer or without a
 * program, if a transform-feedback scope nests or ends unopened, or if a draw
 * would write more vertices than the bound pool was sized for. Every case
 * asserts those counters are zero, and one case proves the stale-name watchdog
 * can fire, so the zeros mean something.
 *
 * WHAT IS ABSENT IS PART OF THE CHECK. No entry point named
 * glMapBuffer, glUnmapBuffer, glGetBufferSubData, glReadPixels, and no
 * query entry point and no sync entry point, exists in this stub AS A SYMBOL -
 * glFenceSync, glWaitSync and glDeleteSync being reachable only through the
 * resolver and only when a case sets `provide_fence_set`, so a DIRECT call to
 * one would still not link - and no
 * glDeleteBuffers either, nor glDeleteProgram or glDeleteShader AS SYMBOLS,
 * those two being reachable only through the resolver and only when a case asks
 * (see the two gates below). A DIRECT call to
 * one from the module would be an UNDEFINED SYMBOL AT LINK TIME and the fixture
 * would not build. That is checked by the BUILD over the whole translation unit
 * rather than by a counter, which can only speak for the paths a test drives.
 *
 * glFinish IS NO LONGER ON THAT LIST, AND THE WEAKENING IS STATED RATHER THAN
 * LEFT TO BE NOTICED. The post-reset boundary control (SL_BG_TF_POST_RESET)
 * issues one glFinish on two of its arms, and glFinish is a GL 1.0 core export
 * that wglGetProcAddress answers NULL for, so the module calls it directly as
 * it calls the other core names. It is therefore a real symbol here and the
 * build no longer refuses a glFinish anywhere in the module. "An off launch
 * adds no synchronisation point" becomes a COUNTER - `finish_calls`, which this
 * stub raises on every call - and a record in the ordered transcript, which the
 * off cases assert. The absence argument is unchanged in strength for every
 * other name above, and glFinish is not a read-back: it returns nothing and the
 * module reads nothing from it, so no round trip is introduced.
 *
 * glGetError IS NOT ON THAT LIST ANY MORE, AND THE CONTRACT IS WHY. It is a GL
 * 1.0 core export, and the module calls it directly for the same reason it
 * calls glEnable, glDisable, glVertexPointer and glDrawArrays directly:
 * wglGetProcAddress is documented to answer NULL for core functions, so
 * resolving it made the error state permanently unobservable on capable
 * drivers. So the stub defines it as a real symbol, like the other four core
 * names, and models the error state through `geterror_queue`. It is NOT a
 * read-back or a synchronisation point, so the link-time absence of those is
 * exactly as strong as it was.
 *
 * "OFF CONSUMES NO ERROR" IS THEREFORE A COUNTER AND NOT A SYMBOL, and the
 * counter is `get_error_calls`, which this stub raises on every call. An off
 * launch must leave it at 0 - the module names glGetError only on the armed
 * capture path - and the off case asserts that.
 *
 * ONE ENTRY POINT IS RESOLVABLE ON REQUEST, AND ONLY THAT. The production
 * buffer diagnostic (SL_BG_TF_DUMP) resolves glGetBufferSubData through
 * sl_gl_proc inside its one capture, so the stub can hand it back - from a
 * function whose own name is tfs_*, so the link-time absence above is untouched
 * - and only when a test sets `provide_readback`. It defaults to 0, which is
 * what makes "an off launch resolves no readback entry point" observable from
 * the other side: with the diagnostic off the module never asks, and with it on
 * against a driver that has none the resolve answers NULL and the capture
 * refuses.
 */
#ifndef SL_GFX_BGTF_TESTGL_H
#define SL_GFX_BGTF_TESTGL_H

/* ---- the host GL headers, suppressed (see the note above) ---------------- */

#ifndef _WINDOWS_
#define _WINDOWS_
#endif
#ifndef _INC_WINDOWS
#define _INC_WINDOWS
#endif
#ifndef __gl_h_
#define __gl_h_
#endif
#ifndef __GL_H__
#define __GL_H__
#endif

/* ptrdiff_t and size_t, which the real <GL/gl.h> would have pulled in through
 * <windows.h> or its own <stddef.h>. The module types glBufferData's size
 * parameter as ptrdiff_t (its own note: GLsizeiptr is a GL 1.5 name living in
 * glext.h, which this renderer does not include), so the name has to be here
 * before the module's typedef block. */
#include <stddef.h>

/* ---- the GL types and tokens the module uses ---------------------------- */

typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
/* GLbitfield, which the real <GL/gl.h> supplies at its line 24 with this same
 * width. glFenceSync's second parameter and glWaitSync's second are of this
 * type, so the name has to be here before the module's typedef block. */
typedef unsigned int  GLbitfield;
typedef int           GLint;
typedef int           GLsizei;
typedef float         GLfloat;
typedef void          GLvoid;
/* GLboolean and its two values, which the real <GL/gl.h> would have supplied.
 * glIsEnabled returns this type, and the module stores its answers in
 * GLboolean fields, so the name has to be here before the module is compiled. */
typedef unsigned char GLboolean;
#ifndef GL_FALSE
#define GL_FALSE                      0
#define GL_TRUE                       1
#endif

#define APIENTRY                       /* the stub is cdecl throughout */
#define WINGDIAPI                      /* taken from windows.h by the real gl.h */

/* The registry's values. The module guards the post-1.1 ones with `#ifndef`,
 * so defining them here with the same values leaves one definition of each in
 * the translation unit. GL_POINTS and GL_FLOAT have NO fallback in the module -
 * it takes them from <GL/gl.h> - so they must be here or it will not compile;
 * the values are mingw32/include/GL/gl.h:77 and :117. */
#define GL_POINTS                     0x0000
#define GL_FLOAT                      0x1406
#define GL_VERTEX_SHADER              0x8B31
#define GL_COMPILE_STATUS             0x8B81
#define GL_LINK_STATUS                0x8B82
#define GL_ARRAY_BUFFER               0x8892
#define GL_ELEMENT_ARRAY_BUFFER       0x8893
#define GL_TRANSFORM_FEEDBACK_BUFFER  0x8C8E
#define GL_INTERLEAVED_ATTRIBS        0x8C8C
#define GL_RASTERIZER_DISCARD         0x8C89
#define GL_STATIC_DRAW                0x88E4
/* THE ERROR CODES, for the auxiliary real prepass's own error check. GL 1.0
 * core tokens, so the real <GL/gl.h> is where the module gets them and these are
 * here for the same reason GL_POINTS is: this header stands in for that one.
 * ALL EIGHT GL 4.6 DEFINES ARE LISTED, not only the ones a case injects - the
 * drain's bound is "one flag per distinct code, and there are eight codes", and
 * a reader checking that argument should find the eight here. GL_CONTEXT_LOST is
 * 4.5's addition; the rest are 1.0's but for
 * GL_INVALID_FRAMEBUFFER_OPERATION, which is 3.0's. */
#define GL_NO_ERROR                       0
#define GL_INVALID_ENUM                   0x0500
#define GL_INVALID_VALUE                  0x0501
#define GL_INVALID_OPERATION              0x0502
#define GL_STACK_OVERFLOW                 0x0503
#define GL_STACK_UNDERFLOW                0x0504
#define GL_OUT_OF_MEMORY                  0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION  0x0506
#define GL_CONTEXT_LOST                   0x0507
/* ALLOCATION ACCEPTANCE. The module guards its own definition with #ifndef and
 * reads this parameter once per allocation, so defining it here with the
 * registry's value leaves one definition in the translation unit. */
#define GL_BUFFER_SIZE                0x8764
/* THE CLIENT-ARRAY TOKENS, for the position-only-input control. The six GL 1.1
 * array enables are mingw32/include/GL/gl.h:285-290 (0x8074..0x8079); the
 * post-1.1 five are glext.h's, the same values sl_gfx_skybatch.c records having
 * read out of the header in this tree. The module guards its own definitions of
 * the post-1.1 names with #ifndef, so defining them here leaves one definition
 * of each in the translation unit; the GL 1.1 six have no fallback in the module
 * because it takes them from <GL/gl.h>, so they must be here or it will not
 * compile. */
#define GL_VERTEX_ARRAY               0x8074
#define GL_NORMAL_ARRAY               0x8075
#define GL_COLOR_ARRAY                0x8076
#define GL_INDEX_ARRAY                0x8077
#define GL_TEXTURE_COORD_ARRAY        0x8078
#define GL_EDGE_FLAG_ARRAY            0x8079
#define GL_CLIENT_ACTIVE_TEXTURE      0x84E1
#define GL_MAX_TEXTURE_UNITS          0x84E2
#define GL_MAX_TEXTURE_COORDS         0x8871
#define GL_TEXTURE0                   0x84C0
#define GL_SECONDARY_COLOR_ARRAY      0x845E
#define GL_FOG_COORD_ARRAY            0x8457

/* ---- the GL core entry points the module calls DIRECTLY -------------------
 *
 * Exactly the five the module names outside its resolver, and nothing else, so
 * nothing else can be called. The module's own comment records why the split
 * exists: wglGetProcAddress is documented to return NULL for GL 1.1 core
 * functions, so routing these through the resolver would refuse the candidate
 * on a capable driver - and, for glGetError, would leave the error state
 * unobservable rather than observed to be clean.
 *
 * glGetError is reached ONLY from the armed diagnostic's capture, so an off
 * launch calls it zero times; `get_error_calls` is what says so. */

void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
GLenum glGetError(void);
/* The post-reset boundary control's one synchronisation call, direct for the
 * reason the four above are: a GL 1.0 core export that wglGetProcAddress
 * answers NULL for. See the note at the top of this file for what its presence
 * costs the link-time absence argument and what carries the claim instead. */
void glFinish(void);
/* AND THE FENCE PATH'S ONE DIRECT CORE NAME (SL_BG_TF_AUX_SYNC), on exactly the
 * same terms as glFinish: glFlush is GL 1.0 core, wglGetProcAddress is
 * documented to answer NULL for core functions, so the module calls it directly
 * and it has to be a symbol here.
 *
 * THE ABSENCE LIST LOSES ONE MORE NAME, AND THE WEAKENING IS STATED RATHER THAN
 * LEFT TO BE NOTICED. The top of this file records that no glFlush existed here
 * as a symbol, so a direct call to one from the module was an undefined symbol
 * at link time over the WHOLE translation unit. That is no longer true of
 * glFlush, and what replaces it is a COUNTER - `flush_calls`, split per context
 * into `flush_calls_main` and `flush_calls_aux` - plus the ordered transcript,
 * which the FINISH-mode and off cases assert is free of TFGL_FLUSH records. The
 * absence argument is unchanged in strength for every other name, THE THREE SYNC
 * ENTRY POINTS INCLUDED: glFenceSync, glWaitSync and glDeleteSync are reachable
 * only through the resolver and only when a case sets `provide_fence_set`, so
 * they are not symbols here and a direct call to one would still not link.
 *
 * glFlush IS NOT A READ-BACK AND NOT A WAIT. It returns nothing, reads nothing,
 * and the specification requires it to return without waiting for the commands
 * it flushed to complete, so its presence introduces no round trip. */
void glFlush(void);

/* AND THE FOUR THE POSITION-ONLY-INPUT CONTROL CALLS DIRECTLY. All GL 1.1 core,
 * so they are symbols here for the reason the five above are: wglGetProcAddress
 * answers NULL for core functions, and routing a core name through the resolver
 * would refuse the path on a capable driver. None of them is a read-back or a
 * synchronisation point - glIsEnabled and glGetIntegerv read CLIENT state, which
 * is held on the host and costs no round trip - so the link-time absence of
 * glFinish, glMapBuffer, glReadPixels and every fence, sync and query entry
 * point is exactly as strong as it was. The GL 1.3 per-unit SELECTOR is not here:
 * it is resolved, and the stub hands it back only when a test asks. */
GLboolean glIsEnabled(GLenum cap);
void glEnableClientState(GLenum cap);
void glDisableClientState(GLenum cap);
void glGetIntegerv(GLenum pname, GLint *params);

/* The loader the module resolves its post-1.1 names through. In the shipped
 * build this is the backend's; here it is the stub's, and it hands back only
 * the twenty-four names the module needs, less whichever one a test withholds,
 * plus the four a test may GATE ON - glGetBufferSubData,
 * glClientActiveTexture, glDeleteProgram and glDeleteShader, each belonging to
 * one default-off control and each answering NULL unless its own `provide_`
 * field is set.
 * glGetBufferParameteriv is one of the twenty-four: a GL_BUFFER_SIZE read is
 * metadata about an object rather than a pipeline synchronisation, which is why
 * it is present here while every read-back, fence and query entry point is
 * not. */
void *sl_gl_proc(const char *name);

/* ---- the ordered transcript --------------------------------------------- */

enum {
    TFGL_USEPROGRAM = 1,
    TFGL_UNIFORM,
    TFGL_GENBUFFERS,
    TFGL_BINDBUFFER,
    TFGL_BUFFERDATA,
    TFGL_GETBUFPARAM,
    TFGL_BINDBUFFERBASE,
    TFGL_BEGINTF,
    TFGL_ENDTF,
    TFGL_DRAWARRAYS,
    TFGL_VERTEXPOINTER,
    TFGL_ENABLE,
    TFGL_DISABLE,
    TFGL_GETBUFSUBDATA,            /* the diagnostic's one readback */
    TFGL_GETERROR,
    TFGL_FINISH,                   /* the post-reset boundary's wait */
    TFGL_DELETEPROGRAM,            /* the neutral program's teardown */
    TFGL_DELETESHADER,             /* the neutral shader's release   */
    /* The auxiliary-context control's one query. In the transcript rather than
     * only in a counter because its PLACE is part of the claim: it has to be
     * asked on the auxiliary context, and before a single byte of bracket state
     * has been written. */
    TFGL_ISPROGRAM,
    /* THE REAL-PREPASS MODE'S THREE (SL_BG_TF_AUX_REAL). In the transcript
     * rather than only in counters because their PLACE is part of the claim: the
     * two object checks have to be asked on the auxiliary context BEFORE any
     * state is written there, and the array object has to be bound there before
     * the pointer command that lands in it. */
    TFGL_ISBUFFER,                 /* a shared-object QUERY          */
    TFGL_GENVAO,                   /* a vertex array object, created */
    TFGL_BINDVAO,                  /* ... and made current           */
    /* the position-only-input control's four */
    TFGL_ISENABLED,                /* a client-state QUERY           */
    TFGL_GETINTEGERV,              /* a unit-count or selection read */
    TFGL_CLIENTSTATE,              /* an enable or a disable         */
    TFGL_CLIENTACTIVETEX,          /* the per-unit selector          */
    /* THE FENCE PATH'S FOUR RECORDS (SL_BG_TF_AUX_SYNC). Each carries the
     * context stamp every record carries, which is the whole question a
     * cross-context handoff raises: a fence created on one context and waited on
     * from the other is a claim about WHERE, and no count of calls could make it.
     *   TFGL_FENCESYNC  a: the handle answered, as a long; b: the condition
     *   TFGL_WAITSYNC   a: the handle waited on; b: flags; c: 1 when the
     *                   timeout was TIMEOUT_IGNORED and 0 otherwise
     *   TFGL_DELETESYNC a: the handle deleted
     *   TFGL_FLUSH      no arguments; the producer's own flush */
    TFGL_FENCESYNC,
    TFGL_WAITSYNC,
    TFGL_DELETESYNC,
    TFGL_FLUSH
};

/* The four fields carry what the kind needs, and nothing is kind-independent:
 *
 *   USEPROGRAM      a = the program name
 *   UNIFORM         a = the location, b = the program it landed in (0 if GL
 *                   rejected the write)
 *   GENBUFFERS      a = the count asked for, b = the name handed back
 *   BINDBUFFER      a = the target, b = the name
 *   BUFFERDATA      a = the target, b = the byte size REQUESTED, c = the usage,
 *                   d = 1 when data != NULL; `off` = the byte size the fake
 *                   driver actually INSTALLED, which is the distinction the
 *                   allocation controls exist to express
 *   GETBUFPARAM     a = the target, b = the parameter, c = the value reported,
 *                   d = 1 when the stub wrote nothing (a query that failed)
 *   BINDBUFFERBASE  a = the target, b = the index, c = the name
 *   BEGINTF         a = the primitive mode
 *   ENDTF           -
 *   DRAWARRAYS      a = the mode, b = first, c = count, d = the recording
 *                   buffer bound at index 0
 *   GETBUFSUBDATA   a = the target, b = the byte size ASKED for, c = the byte
 *                   size the stub actually WROTE, d = the buffer bound to the
 *                   target when it ran; `off` = the byte offset
 *   GETERROR        a = the value handed back
 *   FINISH          - (the call carries no argument; its PLACE in this ordered
 *                   transcript is the whole of what it records)
 *   DELETEPROGRAM   a = the program name, b = 1 when that name was the CURRENT
 *                   program at the call
 *   VERTEXPOINTER   a = size, b = type, c = stride, d = the array buffer bound
 *                   when the pointer command ran; `off` = the pointer argument
 *                   as the integer offset it is, which is what a buffer-object
 *                   pointer command actually carries
 *   ENABLE/DISABLE  a = the token
 *   ISENABLED       a = the token asked about, b = the answer, c = the client
 *                   active texture unit in force when it ran, which is what
 *                   makes a per-unit query attributable to its unit
 *   CLIENTSTATE     a = the token, b = 1 for an enable and 0 for a disable,
 *                   c = the client active texture unit in force
 *   GETINTEGERV     a = the parameter, b = the value handed back
 *   CLIENTACTIVETEX a = the unit, as the GL_TEXTURE0-relative enum it is
 */
struct sl_bgtfgl_call {
    int  kind;
    long a, b, c, d;
    long off;
    /* WHICH CONTEXT THIS CALL LANDED ON: 0 main, 1 auxiliary, -1 none. Stamped
     * on every record by the one funnel every entry point passes through, so
     * per-context attribution is a property of the TRANSCRIPT and not of a
     * separate count that a test would have to trust. A control that claims to
     * issue one call on a second context is checked by reading this field on
     * exactly that record and on every record around it. */
    int  ctx;
};

#define BGTFGL_LOG     4096
#define BGTFGL_UMAX     256
#define BGTFGL_BUFFERS   64
/* The module's own ceiling (TF_UNITS), pinned here rather than read because the
 * module does not export it: a change to it has to come past this fixture. The
 * stub models one more than the ceiling so a test can report a unit count the
 * module must REFUSE, which a stub sized exactly at the ceiling could not. */
#define BGTFGL_UNITS     16
#define BGTFGL_UNITCAP   (BGTFGL_UNITS + 1)

/* ---- the recorded uniform writes ---------------------------------------- */

enum { BGTF_U1I = 1, BGTF_U1F = 2, BGTF_U3F = 3, BGTF_U1FV = 4 };

/* One uniform write GL ACCEPTED, in call order. A write with no program
 * current, or to location -1, is not here: GL rejects both, and the stub counts
 * them separately (uniform_rejected) so a test can tell "the module did not
 * call" from "GL did not write". */
struct sl_bgtfgl_uwrite {
    int     kind;                  /* BGTF_U1I / _U1F / _U3F / _U1FV        */
    GLint   loc;
    GLuint  prog;                  /* the program current when it landed    */
    GLint   ival;                  /* BGTF_U1I                              */
    GLsizei count;                 /* BGTF_U1FV                             */
    float   fv[16];                /* BGTF_U1F / _U3F / _U1FV               */
};

/* ---- the test's control surface (all SYNTHETIC) ------------------------- */

struct sl_bgtfgl {
    /* what the fake driver refuses */
    const char *withhold_proc;     /* this entry point does not resolve     */
    const char *withhold_uniform;  /* this uniform's location is -1         */
    int create_shader_zero;        /* glCreateShader hands back 0           */
    int create_program_zero;       /* glCreateProgram hands back 0          */
    int fail_compile;              /* GL_COMPILE_STATUS reads 0             */
    int fail_link;                 /* GL_LINK_STATUS reads 0                */
    int gen_buffers_zero;          /* glGenBuffers hands back a zero name   */

    /* WHAT THE STORAGE ACTUALLY BECOMES, which is not what was asked for.
     * A buffer NAME is not allocated storage: glBufferData is void, and a
     * driver that cannot honour the size leaves the object at whatever it
     * had. These express that, and without them a storage failure on a
     * NON-ZERO name cannot be injected at all.
     *
     *   alloc_short_initial  the FIRST glBufferData on a pool (the object is
     *                        still at 0 bytes) installs alloc_bytes instead
     *   alloc_short_growth   a LATER glBufferData on a pool that already has
     *                        storage installs alloc_bytes instead
     *   alloc_bytes          what those install. 0 means the request left the
     *                        object EMPTY, which is the harshest case: a name
     *                        with no storage behind it at all.
     *   alloc_install_none   the call changes the object's size not at all -
     *                        a driver that refused outright and left the
     *                        previous (possibly zero) storage in place */
    int  alloc_short_initial;
    int  alloc_short_growth;
    long alloc_bytes;
    int  alloc_install_none;

    /* AND WHAT THE QUERY THEN SAYS, modelled apart from the storage so the
     * two failure shapes are separable: storage that is short and a query
     * that is honest, versus storage that is right and a query that cannot
     * answer.
     *
     *   getbufparam_fail   the stub writes NOTHING through the out pointer,
     *                      so the caller keeps whatever it initialised - the
     *                      shape of a query that failed
     *   getbufparam_bytes  >= 0: report this instead of the installed size */
    int  getbufparam_fail;
    long getbufparam_bytes;

    /* ---- THE DIAGNOSTIC'S READBACK, AND ITS FAILURE SHAPES -------------
     *
     * EVERY ONE OF THESE DEFAULTS TO OFF, which is the ordinary fixture and the
     * state in which "nothing is resolved" is a fact about the module rather
     * than about this stub.
     *
     *   provide_readback   sl_gl_proc answers for glGetBufferSubData. With this
     *                      0 a resolve answers NULL, which is the shape of a
     *                      driver without GL 1.5 buffer reads.
     *   geterror_queue     the values the DIRECT core glGetError hands back IN
     *                      ORDER, one per call, then 0 (GL_NO_ERROR) for ever.
     *                      This models a PRE-EXISTING error separately from one
     *                      raised by the readback, because those are different
     *                      facts and the record must not attribute one to the
     *                      other. There is no "provide" switch beside it: the
     *                      core export is always callable, which is the whole
     *                      point of calling it rather than resolving it, and
     *                      `get_error_calls` is what shows an off launch made
     *                      no call.
     *   getsub_fail        the read writes NOTHING - the destination keeps
     *                      whatever the caller put there, which is a real GL
     *                      error's shape
     *   getsub_bytes       >= 0: write only this many bytes, a SHORT read whose
     *                      tail is unwritten
     *   getsub_zero        write zeros - the outcome that compares equal to
     *                      nothing while looking like data, and which a
     *                      diagnostic must never report as a measurement */
    /* ---- SYNTHETIC GL ERRORS, RAISED BY A NAMED COMMAND ----------------
     *
     * WHY THIS IS NOT THE QUEUE ABOVE. `geterror_queue` answers the Nth CALL to
     * glGetError with a value, which models a value a driver hands back. It
     * cannot model the thing the auxiliary real prepass's check is about: a
     * driver REJECTING a particular command, after which the flag is set, is
     * sticky, belongs to the context that was current at that command, and is
     * retired by a read on that context. These fields model that.
     *
     *   inject_error_on    the transcript KIND (TFGL_*) whose call raises an
     *                      error. 0 is off and is not a kind - TFGL_USEPROGRAM
     *                      is 1 - so the default raises nothing.
     *   inject_error_after which occurrence of that kind: 0 the first, 1 the
     *                      second. Occurrences are counted across contexts,
     *                      because a case names a command and not a context.
     *   inject_error_code  the code to flag, 0 meaning GL_INVALID_OPERATION.
     *   inject_error_count how many DISTINCT flags that one command sets, which
     *                      is what makes a bounded drain testable: GL permits an
     *                      implementation to hold several at once, and a drain
     *                      that read only one would leave the rest. 0 means 1.
     *                      Each flag after the first carries the next code up
     *                      from inject_error_code, so a drain's answers are
     *                      distinguishable in order.
     *   inject_error_seen  how many calls of that kind have been seen - this
     *                      stub's own cursor, not a setting.
     *   inject_error_stuck the flagged state is NEVER retired by a read, which
     *                      is the implementation that will not come back and the
     *                      only way to reach a capped drain. A read still
     *                      answers a code and still counts.
     *
     * THE FLAGGED STATE IS PER CONTEXT, in `err_pending` / `nerr_pending`
     * indexed 0 MAIN and 1 AUXILIARY, because that is the one property a check
     * on the wrong context would get wrong: glGetError reads and CLEARS the
     * error state of the context that is current, so a module checking on the
     * main context would both miss the auxiliary context's error and silently
     * consume the main one's. Keeping two sets is what lets a case assert each
     * half of that separately. Answers are FIFO: the first code raised is the
     * first returned, so an ordered drain reads them in the order a driver set
     * them. */
    /* ---- THE POSITION-ONLY-INPUT CONTROL'S CAPABILITY SURFACE ----------
     *
     * BOTH DEFAULT TO THE REFUSING SHAPE, which is what makes "an off launch
     * resolves nothing" and "an unsupported context isolates nothing" facts
     * about the module rather than about this stub.
     *
     *   provide_client_active_texture  sl_gl_proc answers for
     *                      glClientActiveTexture. With this 0 a resolve answers
     *                      NULL, which is the shape of a context without the
     *                      GL 1.3 per-unit selector - and the module must then
     *                      isolate NOTHING rather than sweep the arrays that
     *                      need no selector and leave every unit's coordinate
     *                      array feeding the draw.
     *   units_reported     what glGetIntegerv(GL_MAX_TEXTURE_UNITS) answers -
     *                      the CONVENTIONAL fixed-function unit count. 0 means
     *                      "report BGTFGL_UNITS". The module under test no
     *                      longer reads this one; it is still modelled, and a
     *                      case sets it SMALLER than coords_reported to prove
     *                      the sweep is not bounded by it.
     *   coords_reported    what glGetIntegerv(GL_MAX_TEXTURE_COORDS) answers -
     *                      the COORDINATE SET count, which is the domain the
     *                      per-unit selector selects among and the one the sweep
     *                      is bounded by. 0 means "report BGTFGL_UNITS", so an
     *                      ordinary case gets the full sweep without naming a
     *                      number; a test sets it to 1, to a middle value, or
     *                      past the module's ceiling to require a refusal.
     *   getintegerv_fail   the stub writes NOTHING through the out pointer for
     *                      ANY query, so the caller keeps what it initialised -
     *                      the shape of a query that failed.
     *   client_unit_fail   the same, for GL_CLIENT_ACTIVE_TEXTURE ALONE: the
     *                      coordinate-domain query still answers, so the case
     *                      reaches the selector validation instead of refusing
     *                      one question earlier.
     *   client_unit_answer / client_unit_forced
     *                      what GL_CLIENT_ACTIVE_TEXTURE answers, INSTEAD of the
     *                      modelled selector. This is how an answer the context
     *                      could not legally give - enum 0, a set past the
     *                      domain - is put in front of the module without
     *                      moving the modelled state, so "the context is
     *                      unchanged" stays a real comparison. */
    int  provide_client_active_texture;
    long units_reported;
    long coords_reported;
    int  getintegerv_fail;
    int  client_unit_fail;
    int  client_unit_forced;
    long client_unit_answer;

    /* ---- THE NEUTRAL-PROGRAM CONTROL'S ONE CAPABILITY GATE -------------
     *
     * provide_delete_program  sl_gl_proc answers for glDeleteProgram. It is NOT
     *                      in the twenty-four-name table, for the reason
     *                      glGetBufferSubData and glClientActiveTexture are
     *                      not: the ordinary fixture's "exactly these
     *                      twenty-four, in this order" and the
     *                      withhold-each-one battery are about the names
     *                      sl_bgtf_ready needs, and this one is needed only by
     *                      the neutral-program control's own lazy acquire. With
     *                      this 0 a resolve answers NULL, which is BOTH the
     *                      default and the shape the control must refuse on -
     *                      it declines to create an object it could never
     *                      delete. A case sets it to 1 to let the control run.
     *
     * provide_delete_shader  the same gate for glDeleteShader, which the
     *                      control resolves in the same place and for the same
     *                      reason: it compiles ONE vertex shader, and a shader
     *                      object is a GL object nothing frees unless it is
     *                      asked. With this 0 a resolve answers NULL, which is
     *                      again both the default and a shape the control must
     *                      refuse on - the two gates are independent so a case
     *                      can withhold either one alone. Neither is in the
     *                      twenty-four-name table. */
    int  provide_delete_program;
    int  provide_delete_shader;

    /* ---- THE AUXILIARY-CONTEXT CONTROL'S CAPABILITY GATE ---------------
     *
     * provide_is_program  sl_gl_proc answers for glIsProgram. NOT in the
     *                      twenty-four-name table, for the reason the two
     *                      deletes above are not: it is needed only by the
     *                      auxiliary-context control's own lazy acquire, which
     *                      resolves it BEFORE creating any context so that no
     *                      context exists that cannot be verified. With this 0
     *                      a resolve answers NULL, which is both the default
     *                      and the shape the control must refuse on. */
    int  provide_is_program;

    /* ---- THE REAL-PREPASS MODE'S CAPABILITY SURFACE (SL_BG_TF_AUX_REAL) -
     *
     * EVERY ONE DEFAULTS TO THE REFUSING SHAPE, which is what makes "an off
     * launch resolves nothing" and "a context that cannot supply this set is
     * refused" facts about the MODULE rather than about this stub.
     *
     * provide_real_set  sl_gl_proc answers for the three names that exist only
     *                   for this mode - glIsBuffer, glGenVertexArrays and
     *                   glBindVertexArray. NONE of them is in the
     *                   twenty-four-name table, for the reason the other gated
     *                   names are not: the ordinary fixture's "exactly these
     *                   twenty-four, in this order" and the withhold-each-one
     *                   battery are about the names sl_bgtf_ready needs. With
     *                   this 0 a resolve answers NULL, which is both the default
     *                   and a shape the mode must refuse on.
     * is_buffer_false   glIsBuffer answers GL_FALSE, the shape of a buffer name
     *                   that means nothing in the context being asked - which is
     *                   what an UNSHARED buffer object looks like from the
     *                   second context, and the one answer this mode must refuse
     *                   the served draw on rather than bind the name anyway.
     * is_buffer_false_after  -1 never; else let N questions answer TRUE first
     *                   and then answer FALSE once. 0 means the first. This is
     *                   how the INPUT buffer's check and the OUTPUT pool's check
     *                   are failed INDEPENDENTLY, which a single flag could not
     *                   express: the mode asks about the input first.
     * gen_vao_zero      glGenVertexArrays hands back a zero name - a context
     *                   that will not give this mode an array object of its own.
     *                   A vertex array object is a CONTAINER object and so is
     *                   never shared, which is why the mode must refuse rather
     *                   than fall back to the main context's or to the default
     *                   object.
     *
     * AND WHAT THE STUB OBSERVED, which is what the assertions read:
     *
     * is_buffer_calls / is_buffer_last / is_buffer_on_aux  the questions made,
     *                   the last name asked about, and whether the LAST one was
     *                   asked on the auxiliary context - which is where every one
     *                   of them must be
     * gen_vao / bind_vao / last_gen_vao  the array objects created, the binds
     *                   issued, and the last name handed out
     * vao_bound         the array object CURRENT in the fake driver, per
     *                   context: `vao_bound` is the auxiliary context's and
     *                   `vao_bound_main` the main one's, kept apart because a
     *                   vertex array object is per-context and a stub with one
     *                   field could not catch a bind that landed on the wrong
     *                   context
     * v_vao_on_main     a glGenVertexArrays or glBindVertexArray reached through
     *                   THIS MODE's resolved pointer while the MAIN context was
     *                   current. The module resolves them on the auxiliary
     *                   context and must call them only there, so this is the
     *                   container-object half of the pointer-identity guard
     * v_vptr_without_vao  a glVertexPointer issued on the AUXILIARY context with
     *                   no array object of this mode's bound. The pointer command
     *                   writes array-object state, so one issued against the
     *                   default object would be writing state the mode has not
     *                   accounted for - which is precisely the fall back it is
     *                   written to refuse
     * v_draw_without_position  a draw on the auxiliary context with the position
     *                   client array NOT enabled there. Array enables are
     *                   per-context, so a mode that assumed the main context's
     *                   enable transferred would draw from nothing, and no count
     *                   of calls would say so */
    int  provide_real_set;
    int  is_buffer_false;
    long is_buffer_false_after;
    int  gen_vao_zero;
    long is_buffer_calls;
    GLuint is_buffer_last;
    int  is_buffer_on_aux;
    long gen_vao, bind_vao;
    GLuint last_gen_vao;
    GLuint vao_bound, vao_bound_main;
    long v_vao_on_main;
    long v_vptr_without_vao;
    long v_draw_without_position;

    int  provide_readback;
    long geterror_queue[4];
    int  ngeterror_queue;
    int  getsub_fail;
    long getsub_bytes;
    int  getsub_zero;

    /* The synthetic error injection described above. BGTFGL_ERRCAP is one more
     * than the eight codes GL 4.6 defines, so a case can ask for more flags than
     * a conformant implementation could hold and the drain's bound is what stops
     * the loop rather than the store running out. */
#define BGTFGL_ERRCAP 9
    int  inject_error_on;
    long inject_error_after;
    long inject_error_code;
    int  inject_error_count;
    long inject_error_seen;
    int  inject_error_stuck;
    long err_pending[2][BGTFGL_ERRCAP];
    int  nerr_pending[2];
    /* AND WHERE EVERY READ LANDED. `get_error_calls` is the whole run's count,
     * kept as it was; these two split it by context so that "this mode never
     * reads the MAIN context's error state" is a number and not an argument. */
    long get_error_on_main;
    long get_error_on_aux;

    /* ---- HOST I/O FAILURES, INJECTED AT THE LINK ------------------------
     *
     * WHY THE LINKER AND NOT A HOOK IN src/. "An allocation that fails", "a
     * write that is short", "a flush that fails" and "a close that fails" are
     * four distinct outcomes the diagnostic promises to keep apart, and none of
     * them can be produced on demand from outside: a disk does not fill up when
     * a test asks it to. The alternative - a test hook inside the product's own
     * write path - would put a switch in the shipped source whose only purpose
     * is to break it, so the fault is injected by WRAPPING the C library
     * symbols at link time instead (-Wl,--wrap=malloc and friends, with the
     * wrappers compiled under BGTF_WRAP_IO in sl_gfx_bgtf_testgl.c). The
     * product source is byte-identical in those builds.
     *
     * `io_fault` is the master switch, so the fixture's own allocations and its
     * own printf are never the ones that fail: a case sets it immediately
     * before the capture and clears it immediately after.
     *
     * EACH COUNT IS -1 FOR "NEVER" and >= 0 for "let this many succeed first,
     * then fail once". -1 rather than 0 because 0 has to mean "fail the very
     * first", which is the commonest case.
     *
     * THE INJECTION IS SINGLE-SHOT: it clears `io_fault` on the way out. The
     * code that handles a failure reports it, and reporting goes through stdio,
     * which allocates - so a fault left armed would break the diagnostic's
     * error path instead of testing it.
     *
     * A SHORT WRITE REALLY WRITES THE SHORT PREFIX. Returning a short count
     * without writing anything would make the "partial bytes are preserved"
     * claim untestable, because there would be no partial bytes.
     *
     * ONE MORE INJECTION RIDES ON THE SAME CLOSE WRAPPER: a path that becomes
     * occupied by a COMPETING WRITER while the capture is in progress.
     * `race_on_fclose_after` makes the Nth close create `race_path` with
     * `race_bytes` before it returns, which is after one payload is on disk and
     * an instant before the next payload's own create runs. The create must then
     * refuse and the bytes that appeared must survive.
     *
     * THE NARROWER RACE IS NOT REPRODUCIBLE, AND THAT IS THE POINT. A file
     * appearing between a writer's "is this path free?" and its truncating open
     * is what the old probe-then-write design lost to; against one atomic
     * O_CREAT|O_EXCL there is no such window and no call inside it to interpose
     * on. The create primitive is not interposable here either - mingw declares
     * it __declspec(dllimport), so the reference is an import stub and --wrap
     * has no symbol to rewrite. What is testable is the consequence: whenever
     * and however the path became occupied, it is refused untouched. */
    int  io_fault;                 /* nothing below bites while this is 0   */
    long malloc_fail_after;        /* -1 never; else let N through, then NULL */
    long fwrite_short_after;       /* -1 never; else let N through, then short */
    long fflush_fail_after;        /* -1 never                                */
    long fclose_fail_after;        /* -1 never                                */
    long race_on_fclose_after;     /* -1 never; else let N closes through, then
                                    * create race_path before returning       */
    char race_bytes[128];          /* what the racing creator writes          */
    char race_path[640];           /* the path it creates                     */
    long race_fired;               /* races actually injected                 */
    long io_malloc, io_fwrite, io_fflush, io_fclose;   /* what was seen     */
    long io_injected;              /* faults actually injected              */

    /* the fake context */
    int context;                   /* its generation; new_context bumps it  */
    int context_destroyed;         /* set by the test AFTER teardown        */

    /* ---- THE AUXILIARY GL CONTEXT, MODELLED (SL_BG_TF_AUX_EMPTY) --------
     *
     * WHY THE SHIM MODELS THIS AT ALL. One control issues its empty draw on a
     * SECOND context and the claim is about WHERE each call landed, which is not
     * a property of any call's arguments. So this shim supplies the backend's
     * four context functions itself - sl_gl_aux_create,
     * sl_gl_aux_make_current, sl_gl_main_make_current, sl_gl_aux_destroy,
     * exactly as it supplies sl_gl_proc - keeps a notion of which context is
     * current, stamps every recorded call with it, and refuses to let a call
     * pass unattributed.
     *
     * THE CURRENT CONTEXT IS 0 MAIN, 1 AUXILIARY, -1 NONE. -1 is reachable only
     * by a failure the test injects, and any GL call made while it holds raises
     * v_call_no_context - which is the assertion that no call is ever made on an
     * uncurrent context.
     *
     * aux_exists       1 while a fake auxiliary context exists
     * aux_created      sl_gl_aux_create calls that CREATED one
     * aux_destroyed    sl_gl_aux_destroy calls that destroyed one
     * aux_switches     sl_gl_aux_make_current calls that SUCCEEDED
     * main_switches    sl_gl_main_make_current calls that SUCCEEDED
     * aux_switch_calls / main_switch_calls / aux_create_calls - calls MADE,
     *                  whatever they answered, so a module that asked and was
     *                  refused is distinguishable from one that never asked
     * calls_on_aux     GL entry-point calls made while the auxiliary context was
     *                  current. The per-context attribution a count can carry;
     *                  the transcript carries it per call
     * draws_on_aux     glDrawArrays calls made there, and draw_vertices_on_aux
     *                  the vertices they asked for - 0 on every honest run
     *
     * THE FAILURE INJECTIONS, each its own gate so each failure path is its own
     * case:
     *
     * aux_create_fail      sl_gl_aux_create answers 0: nothing created, MAIN
     *                      still current. The shape of a backend with no window
     *                      or a platform that refused the context
     * aux_create_lost      sl_gl_aux_create CREATES one and answers -1: the
     *                      main context could not be restored. The shape the
     *                      control must treat as INVALID
     * aux_switch_fail      sl_gl_aux_make_current answers 0 and leaves MAIN
     *                      current, which is what that failure means
     * main_switch_fail     sl_gl_main_make_current answers 0 and leaves the
     *                      current context NONE (-1), which is the honest model
     *                      of a switch that failed with no context established:
     *                      any GL call after it is a violation, which is exactly
     *                      the property the control's lost path must satisfy
     * is_program_false     glIsProgram answers GL_FALSE, the shape of a context
     *                      that does NOT share object names
     *
     * aux_switch_unknown   sl_gl_aux_make_current answers -1 and leaves the
     *                      current context NONE: the switch TO the auxiliary
     *                      context established neither context. This is a
     *                      DIFFERENT shape from aux_switch_fail and the
     *                      distinction is the whole point of the backend's three
     *                      answers - the first is recoverable because the MAIN
     *                      context is verified current, this one is not
     *                      recoverable at all
     * aux_destroy_unknown  sl_gl_aux_destroy answers -1, destroys nothing and
     *                      leaves the context alive: the backend could not
     *                      verify that deleting was safe
     * is_program_false     glIsProgram answers GL_FALSE, the shape of a context
     *                      that does NOT share object names
     *
     * aux_create_fail_after / aux_switch_fail_after / main_switch_fail_after let
     * N calls succeed before the gate bites, so a case can fail the SECOND
     * switch back - the one at the draw - while letting the acquire's own round
     * trip succeed. -1 means never, 0 means the first call.
     *
     * ---- THE PER-CONTEXT ENTRY POINT, MODELLED -------------------------
     *
     * WHY THE SHIM HANDS BACK DIFFERENT POINTERS PER CONTEXT. The platform
     * documents that "On Windows, function pointers are specific to the current
     * GL context" and that pointers from one context must be assumed invalid in
     * a second one (https://wiki.libsdl.org/SDL2/SDL_GL_GetProcAddress). A
     * module that resolved on the main context and called on the auxiliary one
     * is therefore wrong by contract while being invisible to every count and
     * every transcript - a real driver's crash is the only thing that would say
     * so. So this resolver answers with a DIFFERENT function for glIsProgram and
     * glUseProgram depending on which context is current, each of the pair knows
     * which context it belongs to, and either one invoked while the other's
     * context is current raises v_ptr_wrong_context. That is the synthetic
     * identity guard: it is a property of the POINTER and not of the call's
     * arguments, so no count could carry it.
     *
     * proc_calls_on_aux    resolves made while the auxiliary context was current
     * withhold_proc_on_aux a name the resolver answers NULL for ONLY on the
     *                      auxiliary context, which is the shape of a context
     *                      that cannot supply its own entry point - and the one
     *                      case in which the module must refuse rather than fall
     *                      back to the main context's pointer
     *
     * ---- THE TERMINATE SEAM, INTERCEPTED -------------------------------
     *
     * The backend's sl_gl_terminate_no_gl does not return in the product: it
     * ends the process without exit handlers so that no GL call can follow a
     * lost context. A fixture cannot let it do that and still assert anything,
     * so this shim supplies it, records it and RETURNS - which is also why the
     * module's refusal paths after it have to be correct.
     *
     * fatal_calls          times it was called. The assertion that the loss ends
     *                      the process is this count being 1
     * fatal_code           the exit status the last call asked for
     * calls_after_fatal    GL entry-point calls recorded after the first one.
     *                      THE property: 0 says that nothing was issued after
     *                      the point at which the product would have stopped
     *                      existing. It is counted at the same funnel every
     *                      other attribution uses */
    int  aux_exists;
    long aux_created, aux_destroyed;
    long aux_switches, main_switches;
    long aux_create_calls, aux_switch_calls, main_switch_calls;
    long calls_on_aux, draws_on_aux, draw_vertices_on_aux;
    int  ctx_cur;                  /* 0 main, 1 auxiliary, -1 none          */
    int  aux_create_fail, aux_create_lost;
    int  aux_switch_fail, main_switch_fail;
    int  aux_switch_unknown, aux_destroy_unknown;
    long aux_create_fail_after, aux_switch_fail_after, main_switch_fail_after;
    long aux_switch_unknown_after;
    long aux_destroy_refused;      /* destroys the backend refused           */
    int  is_program_false;
    /* -1 never; else let N questions answer as modelled first and then answer
     * FALSE once. 0 means the first. This is how the SHARED acquire's share
     * check and the real-prepass mode's OWN re-ask of it are failed
     * INDEPENDENTLY, which the flag above alone cannot express: the two are
     * asked in that order on the same context (SL_BG_TF_AUX_REAL). */
    long is_program_false_after;
    long is_program_calls;         /* glIsProgram calls made                */
    GLuint is_program_last;        /* the name the last one asked about     */
    int  is_program_on_aux;        /* 1 if the LAST one was asked on the aux
                                    * context, which is where it must be    */
    long proc_calls_on_aux;
    const char *withhold_proc_on_aux;
    long v_ptr_wrong_context;      /* a pointer used on the wrong context    */
    long fatal_calls;
    int  fatal_code;
    long calls_after_fatal;

    /* what the stub observed */
    long calls;                    /* EVERY entry point call                */
    long proc_calls;
    /* The names the module asked the resolver for, IN ORDER. A count alone
     * would pass for a module that resolved twenty-four of the wrong names,
     * so the list is kept and compared against sl_bgtfgl_proc_names. */
    const char *asked[64];
    int         nasked;
    long create_shader, shader_source, compile, get_shaderiv, get_shaderlog;
    long create_program, attach, link, get_programiv, get_programlog;
    long use_program, get_uniform_location, tf_varyings;
    /* THE NEUTRAL-PROGRAM CONTROL'S DELETES, AND WHICH NAME THEY NAMED.
     * `delete_program` is the calls ISSUED and `last_deleted_program` the name
     * of the last one, so a teardown case can require that the object deleted
     * was the neutral program and not, say, the prepass program this module
     * leaves to ordinary context destruction. `v_delete_current_program` is
     * raised when a delete names the program that is CURRENT: legal in GL,
     * which flags the object and frees it when it stops being current, but
     * never what this module's teardown should be doing, so it is recorded as
     * a note rather than refused. */
    long delete_program;
    GLuint last_deleted_program;
    long v_delete_current_program;
    /* THE SHADER OBJECT'S OWN LIFETIME, MODELLED RATHER THAN COUNTED ONLY.
     * `delete_shader` is the glDeleteShader calls ISSUED and
     * `last_deleted_shader` the last name one named, so a case can require that
     * the object released was the shader this acquire created.
     *
     * THE LIVE LEDGER IS WHAT MAKES A LEAK AND A DOUBLE DELETE OBSERVABLE, and
     * it is the reason a bare counter is not enough: "each created object is
     * deleted exactly once" is a statement about NAMES, and a pair of counts
     * that happened to match would pass for a module that deleted one object
     * twice and another not at all. `live_shaders` and `live_programs` are the
     * objects created here and not yet deleted; a test asserts they are 0 after
     * teardown, which is the leak check over the whole run.
     * `v_delete_unknown_shader` / `v_delete_unknown_program` are raised when a
     * delete names something this shim never handed out - which covers the
     * double delete, because the first delete removes the name from the ledger -
     * and `v_double_delete` counts exactly the sub-case where the name WAS
     * handed out and is being deleted a second time, so a double delete is
     * distinguishable from a fabricated name. */
    long delete_shader;
    GLuint last_deleted_shader;
    long v_delete_unknown_shader;
    long v_delete_unknown_program;
    long v_double_delete;
    long live_shaders;
    long live_programs;
    long uniform_calls;            /* every glUniform*, accepted or not     */
    long uniform_rejected;         /* ... that GL wrote nothing for         */
    long gen_buffers, bind_buffer, buffer_data, bind_buffer_base;
    long get_buffer_parameteriv;   /* GL_BUFFER_SIZE queries, accepted or not*/
    long get_buffer_sub_data;      /* the diagnostic's readbacks             */
    long get_buffer_sub_bytes;     /* bytes those reads ASKED for            */
    long get_error_calls;
    /* glFinish calls ISSUED. "An off launch adds no synchronisation point" is
     * this counter, because glFinish is now a real symbol here and the build no
     * longer refuses one; the off cases assert it is 0 and the arms assert it is
     * exactly one per reached boundary. */
    long finish_calls;

    /* ---- THE SYNC OBJECTS, MODELLED PER CONTEXT (SL_BG_TF_AUX_SYNC) -----
     *
     * WHY THE SHIM MODELS THESE AT ALL. The fence path's whole content is that a
     * handle created on one context is waited on from the other and deleted
     * exactly once, in that order, with the producer flushed in between. Every
     * one of those is a claim about WHICH CONTEXT and WHEN, and none of them is a
     * property of any call's arguments - so a count of calls could not carry one.
     * The model below keeps the handles with the context each was created on, and
     * the transcript carries the order and the per-call context stamp.
     *
     * NOTHING HERE WAITS FOR ANYTHING. There is no pipeline in this process, so a
     * fence signals nothing and a wait returns at once. What is modelled is
     * bookkeeping and nothing else; whether a real driver's fence makes one
     * context's transform-feedback writes visible to another is outside this
     * process entirely and is claimed nowhere.
     *
     * THE THREE ENTRY POINTS ARE RESOLVER-ONLY AND GATED. `provide_fence_set`
     * must be set for sl_gl_proc to answer for glFenceSync, glWaitSync or
     * glDeleteSync at all, which is what keeps "a FINISH launch resolves no fence
     * entry point" observable from the other side: with the selector absent the
     * module never asks, and with it set against a context that has none the
     * resolve answers NULL and the mode refuses.
     *
     * AND THEY ARE PER-CONTEXT PAIRS, on exactly the terms the program and
     * uniform names are: a resolve made while the auxiliary context is current
     * answers with THAT context's function, each of the pair knows which context
     * it belongs to, and either invoked while the other's context is current
     * raises v_ptr_wrong_context. A module that resolved on one and called on the
     * other is wrong by the platform's documented contract while being invisible
     * to every count.
     *
     * provide_fence_set     the resolver answers for the three names
     * withhold_fence_main   a fence name the resolver answers NULL for on the
     *                       MAIN context only, by name, which is the shape of a
     *                       main context that cannot supply its own set
     * withhold_fence_aux    the same on the AUXILIARY context only. Two fields
     *                       rather than one because the module resolves the two
     *                       sets at different points and the refusals are
     *                       different lines, so they have to be failable
     *                       independently
     * fence_zero_main / fence_zero_aux   glFenceSync answers 0 on that context,
     *                       which is the creation failure the specification
     *                       allows and the one the mode must refuse on
     * fence_zero_main_after / fence_zero_aux_after   -1 never; else let N
     *                       creations on that context succeed and then answer 0
     *                       once, so a later bracket's create can be failed while
     *                       the first is let through
     * fence_calls_main / fence_calls_aux   glFenceSync calls MADE on each
     * wait_calls_main / wait_calls_aux     glWaitSync calls MADE on each
     * delete_calls_main / delete_calls_aux glDeleteSync calls MADE on each
     * flush_calls / flush_calls_main / flush_calls_aux   glFlush, and where
     * sync_created / sync_deleted   handles minted and retired, over the run
     * sync_live             created minus deleted. THE leak assertion: 0 at the
     *                       end of every launch that did not terminate
     * sync_peak             the largest sync_live ever reached. One is the whole
     *                       of this mode's need, so a value above 1 says a
     *                       bracket held two at once
     * last_fence / last_fence_on_aux    the handle the last creation answered and
     *                       whether it was created on the auxiliary context
     * last_wait / last_wait_on_aux      the same for the last wait, which is how
     *                       "the input wait happened on the AUXILIARY context"
     *                       and "the output wait happened on the MAIN one" are
     *                       read without walking the transcript
     *
     * THE VIOLATIONS, each 0 on every honest run:
     *
     * v_sync_unknown        a wait or a delete naming a handle this shim never
     *                       minted, or one it has already retired. A use after
     *                       free, which no call count would notice
     * v_sync_double_delete  a second delete of the same handle
     * v_sync_badwait        a wait whose flags were not 0 or whose timeout was
     *                       not TIMEOUT_IGNORED, which OpenGL 4.6 section 4.1.1
     *                       makes an INVALID_VALUE
     * v_sync_badfence       a creation whose condition was not
     *                       SYNC_GPU_COMMANDS_COMPLETE or whose flags were not 0
     * v_sync_wait_own       a wait issued on the SAME context that created the
     *                       handle. Not illegal in GL, and not a defect in
     *                       general - but in this mode every wait is a
     *                       CROSS-context wait by construction, so one that is
     *                       not says the handoff went nowhere */
    int  provide_fence_set;
    const char *withhold_fence_main;
    const char *withhold_fence_aux;
    int  fence_zero_main, fence_zero_aux;
    long fence_zero_main_after, fence_zero_aux_after;
    long fence_calls_main, fence_calls_aux;
    long wait_calls_main, wait_calls_aux;
    long delete_calls_main, delete_calls_aux;
    long flush_calls, flush_calls_main, flush_calls_aux;
    long sync_created, sync_deleted, sync_live, sync_peak;
    long last_fence, last_wait;
    int  last_fence_on_aux, last_wait_on_aux;
    long v_sync_unknown, v_sync_double_delete;
    long v_sync_badwait, v_sync_badfence, v_sync_wait_own;
#define BGTFGL_SYNCS 16
    long sync_tab[BGTFGL_SYNCS];   /* the handle, as a long                  */
    int  sync_tab_ctx[BGTFGL_SYNCS];  /* the context it was CREATED on       */
    int  sync_tab_live[BGTFGL_SYNCS];
    int  nsync_tab;

    /* the position-only-input control's own call counts */
    long is_enabled;               /* client-state QUERIES made              */
    long client_state;             /* enables and disables, together         */
    long client_enable;            /* ... of which were ENABLES              */
    long client_disable;           /* ... and disables                       */
    long client_active_texture;    /* per-unit selections made               */
    long get_integerv;             /* GL_MAX_TEXTURE_UNITS / CLIENT_ACTIVE   */
    long alloc_shorted;            /* glBufferData calls that installed less */
    long begin_tf, end_tf, draw_arrays, vertex_pointer, enables, disables;
    long generated;                /* buffer names handed out               */
    long draw_vertices;            /* vertices asked for by glDrawArrays    */

    /* ---- THE MODELLED CLIENT-ARRAY STATE -------------------------------
     *
     * WHY CONTENTS, not just counts. "The arrays came back exactly as they were"
     * is a claim about STATE, and a count of enables and disables is satisfied
     * by a restore that re-enabled the wrong array or the right array on the
     * wrong unit. So the stub models the enables - per unit for the coordinate
     * arrays - and a test compares the state before the draw with the state
     * after it, field for field.
     *
     * `cli_t` is indexed by unit, and the index is taken from `client_unit` at
     * the moment of the call rather than from an argument: that is how GL works,
     * and it is why a restore that selects the wrong unit is visible here at all.
     * A call made with a unit outside the modelled range raises
     * v_client_unit_range instead of writing out of bounds. */
    GLint     client_unit;         /* GL_CLIENT_ACTIVE_TEXTURE, as the enum  */
    GLboolean cli_t[BGTFGL_UNITCAP];
    GLboolean cli_v, cli_c, cli_n, cli_i, cli_e, cli_s, cli_f;

    /* THE CLIENT ARRAYS AS THE LAST DRAW FOUND THEM, sampled inside
     * glDrawArrays. "The draw read positions only" is a property of the draw,
     * and state read afterwards describes the restore instead - so the snapshot
     * is taken where the draw is, and `cli_at_draw_seen` says one was taken at
     * all rather than leaving a zeroed struct to read as "everything off". */
    int       cli_at_draw_seen;
    GLint     cli_at_draw_unit;
    GLboolean cli_at_draw_t[BGTFGL_UNITCAP];
    GLboolean cli_at_draw_v, cli_at_draw_c, cli_at_draw_n, cli_at_draw_i;
    GLboolean cli_at_draw_e, cli_at_draw_s, cli_at_draw_f;

    /* THE THREE EMPTY-DRAW CONDITIONS AS THE LAST DRAW FOUND THEM, sampled
     * inside glDrawArrays for the reason the client arrays above are: transform
     * feedback active, rasterizer discard on and the current program are
     * properties OF THE DRAW, and the same three read after the call describe
     * the restoration instead. SL_BG_TF_EMPTY_STATE's arms differ from each
     * other in exactly these three values at exactly this point, so a test that
     * read them afterwards would find every arm identical - each one restores
     * the common bracket's state - and would be unable to tell any of them
     * apart.
     *
     * `st_at_draw_seen` counts the samples rather than flagging one, so a
     * bracket that issued two draws is distinguishable from one that issued a
     * single draw, and a zeroed struct cannot read as a draw that found
     * everything off. */
    long      st_at_draw_seen;
    int       st_at_draw_tf_active;
    int       st_at_draw_discard;
    GLuint    st_at_draw_program;
    /* The count of draws found with feedback active, with discard on, and with
     * a nonzero program. On a one-draw bracket each is 0 or 1 and says which
     * row ran; over a whole run they say that EVERY draw agreed, which one
     * sample of the last draw cannot. */
    long      st_draws_tf_active;
    long      st_draws_discard_on;
    long      st_draws_program_nonzero;

    /* the modelled state */
    GLuint current;                /* the program in use; 0 = fixed pipeline*/
    GLuint arr_buf;                /* GL_ARRAY_BUFFER_BINDING               */
    GLuint elem_buf;               /* GL_ELEMENT_ARRAY_BUFFER_BINDING       */
    GLuint tf_buf;                 /* the indexed TF binding at index 0     */
    int    discard;                /* GL_RASTERIZER_DISCARD                 */
    int    tf_active;              /* inside a transform-feedback scope     */
    GLuint last_shader, last_program;
    const char *last_source;       /* the text handed to glShaderSource     */
    GLsizei last_source_count;

    /* the recorded output varying, and WHEN it was recorded. The module's own
     * note says naming it after the link leaves a program that captures
     * nothing, so the ORDER is the property: varyings_after_link must stay 0
     * and link_saw_varyings must be 1. */
    const char *tf_varying0;
    GLsizei     tf_varying_count;
    GLenum      tf_varying_mode;
    GLuint      tf_varying_prog;
    int         link_saw_varyings;
    long        varyings_after_link;

    /* the position array, as the last pointer command left it */
    struct {
        int    set;
        GLint  size;
        GLenum type;
        GLsizei stride;
        long   off;
        GLuint buf;
    } vptr;

    /* violations - every case asserts each is zero */
    long v_stale_name;             /* a name from an earlier generation, or
                                    * one this context never handed out      */
    long v_call_after_context_destroyed;
    /* A GL CALL MADE WITH NO CONTEXT CURRENT. Reachable only through an injected
     * switch failure, and the one property the auxiliary-context control's lost
     * path exists to satisfy: once a switch back to the main context has failed,
     * the control must issue nothing at all - not its own calls, and not the
     * bracket's restoration. Every case asserts this stays 0. */
    long v_call_no_context;
    long v_source_without_shader;
    long v_draw_without_discard;   /* a prepass draw could reach a fragment  */
    long v_draw_without_tf_buffer; /* recording with nothing bound at 0      */
    long v_draw_without_program;
    long v_tf_nested;
    long v_tf_unbalanced;
    /* A PROGRAM CHANGE WHILE TRANSFORM FEEDBACK IS ACTIVE, which the API
     * forbids outright - glUseProgram is invalid inside a begin/end scope
     * (gl4/glUseProgram.xml). It is modelled rather than relaxed for the reason
     * the uniform-under-program-0 rule is: SL_BG_TF_EMPTY_STATE has one
     * program-0 row, and "that row never changes the program while feedback is
     * active" has to be a measurement taken where the call is, not a reading of
     * a table that says which row it is. A fixture asserts this stays 0 on every
     * arm, so an edit that paired P0 with T1 would be caught here even if the
     * table it came from looked consistent. */
    long v_program_change_in_tf;
    long v_tf_overrun;             /* the draw writes past the bound pool    */
    long v_unknown_enum;           /* a token the stub does not model        */
    long v_log_full;               /* the transcript is no longer complete   */
    long v_read_inside_tf;         /* a readback inside an open TF scope      */
    long v_read_past_end;          /* a readback past the installed storage   */
    long v_pool_write_past_end;    /* a TEST installed more than was sized    */
    long v_client_unit_range;      /* a unit outside the modelled range       */
    long v_client_state_unknown;   /* a client-array token not modelled here  */
    long v_vertex_array_touched;   /* the POSITION array's own enable moved -
                                    * the one client array this control must
                                    * never disturb, so the stub watches it
                                    * rather than leaving it to a case         */

    /* the accepted uniform writes, in order */
    int nu;
    struct sl_bgtfgl_uwrite u[BGTFGL_UMAX];

    /* the ordered transcript */
    long nlog;
    struct sl_bgtfgl_call log[BGTFGL_LOG];
};

extern struct sl_bgtfgl tf;

/* A clean fake driver: generation 1, nothing resolved, GL's own default state
 * (no program, no binding, discard off), every counter zero. */
void sl_bgtfgl_reset(void);

/* Zero the counters, the transcript and the uniform log, leaving the modelled
 * state and the buffer registry alone - a measurement window. */
void sl_bgtfgl_log_reset(void);

/* THE CONTEXT WAS RECREATED. Advances the generation, so every name handed out
 * before now is stale: using one is a violation. The modelled state goes back
 * to GL's defaults, because a new context has new state. */
void sl_bgtfgl_new_context(void);

/* The location this stub gives that uniform, or -1. */
GLint sl_bgtfgl_loc(const char *name);

/* A buffer name for the TEST to own - the pass's own static vertex buffer,
 * which the module reads rather than creates. Minted in the current generation
 * with a recorded byte size. Not a GL call and not counted. */
GLuint sl_bgtfgl_make_buffer(long bytes);

/* The byte size the stub has recorded for a name, or -1 if it has none. */
long sl_bgtfgl_buffer_bytes(GLuint name);

/* ---- THE POOL'S CONTENTS, WHICH THIS STUB MODELS ----------------------
 *
 * WHY CONTENTS AT ALL, when the rest of the shim models only sizes. The
 * diagnostic's whole output is BYTES, and a comparison contract that cannot be
 * shown to carry one changed bit is not a contract. So a buffer carries a byte
 * store, a test installs synthetic "transform-feedback output" into it, and the
 * readback hands exactly those bytes back - which lets a test open the files the
 * product wrote and assert they hold the installed pattern unchanged.
 *
 * THE BYTES ARE FABRICATED. No GPU produced them and no shader computed them;
 * what they prove is that the record carries whatever the pool held, not that
 * any driver would hold that.
 *
 * `sl_bgtfgl_pool_write` installs bytes at an offset in a buffer's store,
 * growing nothing: a write past the installed size is refused and counted as
 * v_pool_write_past_end, so a test cannot quietly install more than the module
 * allocated. `sl_bgtfgl_pool_read` reads them back for comparison. The store is
 * capped per buffer (BGTFGL_POOL_CAP) so a fixture cannot be made to allocate
 * an absurd amount by asking for an absurd vertex count. */
#define BGTFGL_POOL_CAP 65536u          /* 64 KiB a buffer: the fixture's pools
                                         * are a few thousand bytes, and
                                         * BGTFGL_BUFFERS of these has to stay a
                                         * sane amount of static storage */

int  sl_bgtfgl_pool_write(GLuint name, long off, const unsigned char *src, long n);
int  sl_bgtfgl_pool_read(GLuint name, long off, unsigned char *dst, long n);

/* STATE THE MODULE COULD HAVE INHERITED, installed directly. "Puts back what
 * it changed" needs something to be false about: the module's own restore
 * would read as a no-op against GL's defaults, so a test dirties the state
 * first and then asserts the restore. Not a GL call and not counted - it
 * installs the same fields the resolved entry points write. */
void sl_bgtfgl_test_state(GLuint prog, GLuint arr, GLuint elem, GLuint tfb, int discard);

/* INHERITED CLIENT-ARRAY STATE, INSTALLED DIRECTLY, for the same reason: "the
 * arrays came back exactly as they were" needs something to be false about, and
 * the position-only-input control's restore would read as a no-op against a
 * context with nothing enabled. `t` is the per-unit coordinate enables and `nt`
 * how many are given; the rest are the single arrays in the struct's own order,
 * the position array included so a case can prove it was never moved. Not a GL
 * call, not counted, and it does not pass through the stub's own client-state
 * watchdogs - which is what makes those watchdogs about the MODULE. */
void sl_bgtfgl_test_client(GLint client_unit, const GLboolean *t, int nt,
                           GLboolean v, GLboolean c, GLboolean n, GLboolean i,
                           GLboolean e, GLboolean s, GLboolean f);

/* The uniform names the module is expected to resolve, in the order it asks
 * for them, and the entry points it is expected to resolve, in the order it
 * asks for those. A test walks these to prove EVERY one is required. */
#define BGTFGL_UNIFORMS 8
#define BGTFGL_PROCS   24
extern const char *const sl_bgtfgl_uniform_names[BGTFGL_UNIFORMS];
extern const char *const sl_bgtfgl_proc_names[BGTFGL_PROCS];

#endif /* SL_GFX_BGTF_TESTGL_H */
