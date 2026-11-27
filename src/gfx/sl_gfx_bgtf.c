/* The background pass's GPU CLIP PREPASS - selected by the OPTIMIZED RENDERING
 * row, and reachable by a developer switch.
 *
 * v0.4 IT IS NO LONGER DEVELOPER-ONLY, which is the first thing to know about
 * every gate in this file. An ordinary session with the row ON takes the
 * AUXILIARY-CONTEXT real prepass on arm GPU_SOURCE with FENCE for its eligible
 * walks; SL_BG_TF_CLIP=1 is the developer route to the same arm and to every
 * diagnostic here; SL_BG_TF_CLIP=0 takes the prepass out of a launch. The
 * admission answer is sl_bgtf_walk_admit, and a gate in this file that tests
 * sl_bgtf_opt_in instead admits only the developer route - which is exactly how
 * sl_bgtf_ready silently made the production selection unreachable once.
 *
 * WHAT IS AND IS NOT QUALIFIED. The auxiliary GPU_SOURCE arm's EXACTNESS is
 * measured: byte-identical frames against the original renderer on the Runway,
 * Frigate and Facility viewpoints recorded for this selection, including the
 * 1920x1080 witnesses. NO FRAME TIME AND NO FRAME RATE FOLLOWS FROM THAT, and
 * the MAIN-CONTEXT prepass this file also implements remains a KNOWN MISMATCH,
 * one blue value apart at (71,113), kept reachable by name alone. See
 * sl_gfx_bgtf.h for the two selections and every control's own terms.
 *
 * This file holds the entry points, the prepass
 * program, the capability answer, the keyed slot cache and the state
 * restoration. The rendering predicates that decide which walk and which mesh
 * may use it live in sl_gfx_dl.c, beside the facts they are made of.
 *
 * THE ARITHMETIC IS COPIED CHARACTER FOR CHARACTER from sl_gfx_bggpu.c's
 * program, which is bg_vertex_range's in the same order. `precise` plus the
 * explicit parentheses are what stop the compiler contracting the multiplies
 * and adds into fused or reassociated forms. The ONE difference is the
 * destination: this program writes a recorded output varying instead of
 * gl_Position, because nothing is rasterised by the prepass. Changing the
 * destination does not change the value, and keeping the expression text
 * identical is what makes that checkable by reading the two side by side.
 *
 * WHY THERE IS NO SECOND, LOWER-VERSION SHADER. A fallback that dropped
 * `precise` would be an arithmetic tolerance dressed as compatibility. A driver
 * that cannot compile this gets the existing path.
 *
 * CORE 1.1 ENTRY POINTS ARE CALLED DIRECTLY, NOT RESOLVED, and that split is
 * deliberate. wglGetProcAddress is documented to return NULL for GL 1.1 core
 * functions, so routing glVertexPointer, glDrawArrays, glEnable, glDisable or
 * glGetError through the resolver would refuse the candidate - or, for
 * glGetError, make the error state unobservable - on a perfectly capable
 * driver. Those five are called directly; the buffer and transform-feedback
 * entry points are GL 1.5 and 3.0 and must be resolved.
 *
 * glGetError IS ONE OF THE FIVE, and it moved there because the resolver was
 * the wrong loader for it: a driver whose wglGetProcAddress answers only for
 * extensions handed back NULL, the diagnostic then recorded -1 for both
 * readings, and a gate that refuses an unobservable error state refused every
 * capture. The call is made ONLY on the armed diagnostic's capture path, so an
 * off launch still adds no GL call and consumes no error flag.
 *
 * APIENTRY ON EVERY TYPEDEF. These entry points are __stdcall on Windows. A
 * typedef without APIENTRY makes the caller clean up a callee-cleaned frame,
 * which corrupts the caller's own frame a few calls in and lands as a fault
 * somewhere else entirely; see the note above SL_PFN_CREATESHADER in
 * sl_gfx_dl.c for the measurement. On Linux APIENTRY is empty and the two
 * conventions coincide, so carrying it costs nothing there.
 *
 * THERE IS NO READBACK, NO glFinish AND NO QUERY SYNCHRONISATION HERE, and
 * that is a correctness claim rather than an optimisation. The prepass writes a
 * buffer and the raster draw that follows reads it in the same command stream,
 * which GL already orders; a fence or a map would add a stall for nothing. The
 * offline fixture proves the absence by LINKING against a synthetic GL that
 * defines no such entry point - a call would fail to link, which no counter can
 * be talked out of. */
#ifndef __sgi

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* THE EXCLUSIVE-CREATION PRIMITIVE, AND WHY IT IS NOT fopen. The portable
 * fopen write modes all truncate, and the "x" mode that would not is a C11
 * addition this platform's C runtime does not reliably honour - a mode
 * character it ignores would silently restore the truncating behaviour, which
 * is the defect being removed. O_CREAT|O_EXCL is the one operation that both
 * creates and claims, it is CREATE_NEW on Windows and the POSIX atom
 * everywhere else, and the descriptor is handed to the stream layer so the
 * short-write, flush and close distinctions below are unchanged.
 *
 * These are C-runtime headers, not platform-GUI ones: the offline fixture
 * suppresses <windows.h> for this translation unit and still gets all three. */
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define SL_BGTF_FDOPEN(fd, mode) _fdopen((fd), (mode))
#define SL_BGTF_CLOSE(fd)        _close(fd)
#else
#include <unistd.h>
#define SL_BGTF_HAVE_LINK        1
#define SL_BGTF_FDOPEN(fd, mode) fdopen((fd), (mode))
#define SL_BGTF_CLOSE(fd)        close(fd)
#endif

#ifdef _WIN32
/* windows.h must precede <GL/gl.h>, which takes APIENTRY and WINGDIAPI from
 * it - the same include order the rest of src/gfx uses. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include "sl_gfx_bgtf.h"
/* v0.4 THE RENDERER-STATE PROBE (SL_BG_GLSTATE), default off and one cached
 * integer test at each of the four hooks below. It is a SEPARATE diagnostic
 * from this file's buffer readback (SL_BG_TF_DUMP): its own switch, selector,
 * output and spent attempt, and nothing here arms or calls that one. The
 * prepass is where three of its four moments exist at all - the state the
 * prepass inherited, the state immediately after its draw and transform
 * feedback ended, and the state after the restoration - so the hooks are here
 * and the probe's own logic is in its own translation unit. */
#include "sl_gfx_bgstate.h"

/* The backend's own resolver: NULL before a context exists, and NULL when this
 * backend is not the one that came up, so every caller needs a fallback -
 * which this one has. */
void *sl_gl_proc(const char *name);

/* ---- THE BACKEND'S AUXILIARY GL CONTEXT (SL_BG_TF_AUX_EMPTY) ------------
 *
 * The window and the main context belong to the backend and no header exports
 * them, so the calls one diagnostic in this file needs are declared here and
 * linked against, exactly as sl_gl_proc above is. That is also what keeps this
 * module compilable against a synthetic GL: a fixture supplies its own set and
 * a context switch becomes an observable event in the transcript.
 *
 * THE RESULT SPACE IS THE SAME THREE ANSWERS EVERYWHERE, and the distinction
 * between the last two is the one every fail-closed path below turns on:
 *
 *   1                 the intended context is VERIFIED current. The backend
 *                     queried the platform for which context is current and got
 *                     the one it meant; this is not a reading of a return code.
 *   0                 a REFUSAL with the MAIN context VERIFIED current. GL may
 *                     still be issued and it reaches the main context, so the
 *                     caller's mesh goes down the CPU path exactly as any other
 *                     prepass refusal sends it. RECOVERABLE.
 *   SL_BGTF_AUX_LOST  UNKNOWN, from either of two causes: this process cannot
 *                     say which context a GL command would reach, or it cannot
 *                     say how a later context would be CREATED because the
 *                     process-wide object-sharing attribute could not be proved
 *                     restored. NOT recoverable, and not a refusal: there is no
 *                     state from which to continue, so the control terminates
 *                     the process - see sl_gl_terminate_no_gl. The backend
 *                     names which cause on stderr before returning.
 *
 *   sl_gl_aux_create        1 a context exists and the main context is verified
 *                           current; 0 none is usable, main verified current and
 *                           the object-sharing attribute untouched or proved
 *                           equal to what it was; LOST. The platform call that
 *                           creates a context makes it current, so the backend
 *                           switches back and verifies before returning. The
 *                           sharing attribute it must set and put back is
 *                           process-wide, so it is proved by readback rather
 *                           than by a return code, and an unproved one is LOST
 *                           and never a refusal. Idempotent.
 *   sl_gl_aux_make_current  1 the auxiliary context is verified current; 0 it is
 *                           not and the MAIN one is verified current; LOST.
 *   sl_gl_main_make_current 1 the main context is verified current; LOST. There
 *                           is no 0: this is called only while the auxiliary
 *                           context is current, so every other outcome is the
 *                           unknown one.
 *   sl_gl_aux_destroy       1 destroyed; 0 there was none; LOST it still exists
 *                           and was not destroyed, the main context not being
 *                           verified current. The window must still be alive and
 *                           the main context current - CHECKED by the backend,
 *                           not assumed of the caller, which is why the only
 *                           call site is still sl_bgtf_shutdown.
 *   sl_gl_terminate_no_gl   DOES NOT RETURN in the product. Ends the process
 *                           without running exit handlers or library detach
 *                           code, so that no GL call of any kind can follow a
 *                           lost context. A fixture may intercept it, and then
 *                           it returns and the caller's own refusal path runs.
 *
 * WHY A RETURN VALUE IS NOT ENOUGH FOR THE THIRD ANSWER, which is the whole
 * reason the terminate exists. The served draw's refusal only sends ONE mesh to
 * the CPU path: the caller then draws it with GL, every later frame draws with
 * GL, teardown issues GL, and this build registers exit handlers that issue GL.
 * None of that is reachable from a counter in this file. So "no further GL
 * command" is enforced by ending the process at the point of loss, and the
 * latches below remain only so that a fixture which intercepts the terminate
 * still observes the refusal.
 *
 * SHARING IS REQUESTED BY THE BACKEND AND VERIFIED HERE. Whether the driver
 * honoured the request is a separate question, settled by a query on the new
 * context; a create that succeeded is not evidence of sharing. */
#define SL_BGTF_AUX_LOST (-1)
int sl_gl_aux_create(void);
int sl_gl_aux_make_current(void);
int sl_gl_main_make_current(void);
int sl_gl_aux_destroy(void);
void sl_gl_terminate_no_gl(int code);

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER            0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS           0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS              0x8B82
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER             0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER     0x8893
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER
#define GL_TRANSFORM_FEEDBACK_BUFFER 0x8C8E
#endif
#ifndef GL_INTERLEAVED_ATTRIBS
#define GL_INTERLEAVED_ATTRIBS      0x8C8C
#endif
#ifndef GL_RASTERIZER_DISCARD
#define GL_RASTERIZER_DISCARD       0x8C89
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW              0x88E4
#endif
#ifndef GL_BUFFER_SIZE
#define GL_BUFFER_SIZE              0x8764
#endif
/* THE CLIENT-ARRAY TOKENS THE POSITION-ONLY-INPUT CONTROL NEEDS.
 *
 * The values are glext.h's, and they are not recalled here: they are the same
 * ones src/gfx/sl_gfx_skybatch.c:131-166 records having read out of
 * mingw32/include/GL/glext.h in this same MSYS2 tree - :184 for the client
 * unit, :125 for GL_TEXTURE0, :354 for the secondary colour array and :514 for
 * the fog coordinate array. Two modules now define the same four, each under
 * its own #ifndef, so a toolchain whose header supplies them leaves one
 * definition of each in either translation unit.
 *
 * GL_MAX_TEXTURE_COORDS IS READ FROM THE SAME HEADER, at glext.h:649, and it is
 * the limit this control's sweep is bounded by. GL_MAX_TEXTURE_UNITS, which the
 * sky batch defines at :185 and which this module no longer names, is a
 * DIFFERENT and possibly SMALLER number - see the sweep's own note below.
 *
 * THE 1.1 TOKENS ARE NOT HERE because <GL/gl.h> already has them:
 * GL_VERTEX_ARRAY, GL_COLOR_ARRAY, GL_NORMAL_ARRAY, GL_INDEX_ARRAY,
 * GL_EDGE_FLAG_ARRAY and GL_TEXTURE_COORD_ARRAY are all GL 1.1. The post-1.1
 * pair are legal arguments to the GL 1.1 client-state commands whatever the
 * header's version, so naming them needs no new entry point - only the per-unit
 * SELECTOR does, and that is resolved. */
#ifndef GL_CLIENT_ACTIVE_TEXTURE
#define GL_CLIENT_ACTIVE_TEXTURE    0x84E1
#endif
#ifndef GL_MAX_TEXTURE_COORDS
#define GL_MAX_TEXTURE_COORDS       0x8871
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                 0x84C0
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY
#define GL_SECONDARY_COLOR_ARRAY    0x845E
#endif
#ifndef GL_FOG_COORD_ARRAY
#define GL_FOG_COORD_ARRAY          0x8457
#endif
/* THE PRIVATE-PREPASS-VERTEX-ARRAY CONTROL'S ONE TOKEN. GL_VERTEX_ARRAY_BINDING
 * is ARB_vertex_array_object's and GL 3.0 core; the value is the one
 * src/gfx/sl_gfx_bgstate.c:105-106 already defines under its own #ifndef and
 * queries at :990, so the two modules agree and a toolchain whose header
 * supplies it leaves one definition in either translation unit. */
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING     0x85B5
#endif

typedef GLuint (APIENTRY *SL_TF_CREATESHADER)(GLenum);
typedef void   (APIENTRY *SL_TF_SHADERSOURCE)(GLuint, GLsizei, const char **, const GLint *);
typedef void   (APIENTRY *SL_TF_COMPILESHADER)(GLuint);
typedef void   (APIENTRY *SL_TF_GETSHADERIV)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *SL_TF_GETSHADERINFOLOG)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY *SL_TF_CREATEPROGRAM)(void);
typedef void   (APIENTRY *SL_TF_ATTACHSHADER)(GLuint, GLuint);
typedef void   (APIENTRY *SL_TF_LINKPROGRAM)(GLuint);
typedef void   (APIENTRY *SL_TF_GETPROGRAMIV)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *SL_TF_GETPROGRAMINFOLOG)(GLuint, GLsizei, GLsizei *, char *);
typedef void   (APIENTRY *SL_TF_USEPROGRAM)(GLuint);
typedef GLint  (APIENTRY *SL_TF_GETUNIFORMLOCATION)(GLuint, const char *);
typedef void   (APIENTRY *SL_TF_UNIFORM1F)(GLint, GLfloat);
typedef void   (APIENTRY *SL_TF_UNIFORM3F)(GLint, GLfloat, GLfloat, GLfloat);
typedef void   (APIENTRY *SL_TF_UNIFORM1I)(GLint, GLint);
typedef void   (APIENTRY *SL_TF_UNIFORM1FV)(GLint, GLsizei, const GLfloat *);
typedef void   (APIENTRY *SL_TF_TFVARYINGS)(GLuint, GLsizei, const char *const *, GLenum);
typedef void   (APIENTRY *SL_TF_GENBUFFERS)(GLsizei, GLuint *);
typedef void   (APIENTRY *SL_TF_BINDBUFFER)(GLenum, GLuint);
/* ptrdiff_t rather than GLsizeiptr: that name is GL 1.5 and lives in glext.h,
 * which this renderer does not include. sl_gfx_dl.c and sl_gfx_skybatch.c type
 * the same entry point the same way, so all three agree. */
typedef void   (APIENTRY *SL_TF_BUFFERDATA)(GLenum, ptrdiff_t, const GLvoid *, GLenum);
typedef void   (APIENTRY *SL_TF_BINDBUFFERBASE)(GLenum, GLuint, GLuint);
typedef void   (APIENTRY *SL_TF_GETBUFFERPARAMETERIV)(GLenum, GLenum, GLint *);
typedef void   (APIENTRY *SL_TF_BEGINTF)(GLenum);
typedef void   (APIENTRY *SL_TF_ENDTF)(void);
/* THE DIAGNOSTIC'S ONE RESOLVED ENTRY POINT, AND IT IS RESOLVED NOWHERE ELSE.
 * glGetBufferSubData is not named outside the capture, so an off launch never
 * asks for it: that is what makes "off resolves no readback entry point" a
 * property of the code rather than of a counter. It is GL 1.5 and must be
 * resolved.
 *
 * glGetError IS NOT HERE. It is GL 1.0 core and is called directly - see the
 * note at the top of this file - because wglGetProcAddress is documented to
 * answer NULL for core functions and a NULL there made the error state
 * permanently unobservable rather than observed to be clean. */
typedef void   (APIENTRY *SL_TF_GETBUFFERSUBDATA)(GLenum, ptrdiff_t, ptrdiff_t, GLvoid *);

/* THE POSITION-ONLY-INPUT CONTROL'S ONE RESOLVED ENTRY POINT, resolved nowhere
 * else and only on the first draw that actually isolates, so a launch without
 * the control never asks for it.
 *
 * glClientActiveTexture is GL 1.3 and must be resolved; the four client-state
 * commands around it - glIsEnabled, glGetIntegerv, glEnableClientState and
 * glDisableClientState - are GL 1.1 core and are called DIRECTLY, for the reason
 * recorded at the top of this file: wglGetProcAddress is documented to answer
 * NULL for core functions, so routing a core name through the resolver refuses
 * the path on a capable driver. src/gfx/sl_gfx_skybatch.c makes exactly this
 * split for exactly these names.
 *
 * APIENTRY IS LOAD-BEARING. These entry points are __stdcall and the CALLEE pops
 * the arguments, so a typedef without APIENTRY corrupts the caller's frame. */
typedef void   (APIENTRY *SL_TF_CLIENTACTIVETEXTURE)(GLenum);

/* THE PRIVATE-PREPASS-VERTEX-ARRAY CONTROL'S TWO RESOLVED ENTRY POINTS, resolved
 * nowhere else and only on a launch that selected the control, so a launch
 * without it never asks for them.
 *
 * glGenVertexArrays and glBindVertexArray are ARB_vertex_array_object / GL 3.0
 * and must be resolved. glGetIntegerv - which reads the inherited binding and
 * confirms both the bind and the restore - and the two client-state commands
 * that enable the private object's position array are GL 1.1 core and are called
 * DIRECTLY, for the reason recorded at the top of this file: wglGetProcAddress is
 * documented to answer NULL for core functions, so routing a core name through
 * the resolver refuses the path on a capable driver.
 *
 * glDeleteVertexArrays IS DELIBERATELY NOT HERE. The one object belongs to the
 * context and ordinary context destruction releases it; the invalidation hook
 * makes no GL call at all because the context may already be unusable, so a
 * delete entry point would exist only to be called at the one moment it must not
 * be. See sl_bgtf_invalidate.
 *
 * APIENTRY IS LOAD-BEARING. These entry points are __stdcall and the CALLEE pops
 * the arguments, so a typedef without APIENTRY corrupts the caller's frame. */
typedef void   (APIENTRY *SL_TF_GENVERTEXARRAYS)(GLsizei, GLuint *);
typedef void   (APIENTRY *SL_TF_BINDVERTEXARRAY)(GLuint);

/* THE NEUTRAL-PROGRAM CONTROL'S ONE RESOLVED ENTRY POINT, resolved nowhere else
 * and only on a launch that selected the control, so a launch without it never
 * asks for it.
 *
 * glDeleteProgram is GL 2.0 and must be resolved. It is the one DELETE this
 * module resolves at all, and the reason it exists here while the private
 * vertex-array control's does not is a difference in WHEN the object can be
 * released: that control's object is dropped by the invalidation hook, which
 * makes no GL call because the context may already be gone, whereas this
 * control's program is deleted by sl_bgtf_shutdown, which the backend calls
 * while the owning context is still current. See that function.
 *
 * IT IS REQUIRED BEFORE THE PROGRAM IS CREATED, not when it is deleted. A
 * context that will not hand back a delete entry point cannot be given an object
 * this control is responsible for, so the resolve is part of the capability
 * question and its absence refuses the control rather than leaving an object
 * behind.
 *
 * APIENTRY IS LOAD-BEARING. This entry point is __stdcall and the CALLEE pops the
 * arguments, so a typedef without APIENTRY corrupts the caller's frame. */
typedef void   (APIENTRY *SL_TF_DELETEPROGRAM)(GLuint);

/* THE SHADER OBJECT'S OWN RELEASE, on the same terms as the program's.
 *
 * WHY IT IS REQUIRED RATHER THAN OPTIONAL. The control compiles one vertex
 * shader, and a shader object is a GL object with its own name and its own
 * reference: a program that holds it keeps it alive, but nothing ever frees it
 * unless this code asks. Resolving this BEFORE glCreateShader is what makes "no
 * path leaves an object this control cannot release" a property of the sequence
 * rather than a promise - the same reason glDeleteProgram is resolved first.
 *
 * THE ONE RULE THE SEQUENCE RELIES ON: glDeleteShader on a shader that is still
 * ATTACHED to a program does not free it, it FLAGS it, and the object goes away
 * when the last program holding it is detached or deleted. So the call is legal
 * and correct immediately after a successful link - the linked program keeps
 * working, and the shader is released with the program at teardown - which is
 * why no glDetachShader is needed and none is resolved.
 *
 * APIENTRY IS LOAD-BEARING, for the reason given just above. */
typedef void   (APIENTRY *SL_TF_DELETESHADER)(GLuint);

/* THE ONE QUERY THE AUXILIARY-CONTEXT CONTROL MAKES (SL_BG_TF_AUX_EMPTY).
 *
 * WHY IT IS A QUERY AT ALL, in a module whose rule is that it reads nothing
 * back. "Two contexts share object names" is not something this process can
 * ISSUE: it is a fact about what the platform granted, and a control that
 * assumed it would bind a name that means something else - or nothing - in the
 * second context, and would report the result as this arm. glIsProgram on the
 * prepass program's name, asked ON the auxiliary context, is the narrowest
 * question that settles it. It is a query about a NAME, not about what a driver
 * did with a command, so it is not a read-back of output and it consumes no
 * error state.
 *
 * ASKED THROUGH A POINTER RESOLVED ON THE AUXILIARY CONTEXT ITSELF, which is
 * the rule stated at p_ax_IsProgram below and is not an optimisation.
 *
 * APIENTRY IS LOAD-BEARING, for the reason given twice above. */
typedef GLboolean (APIENTRY *SL_TF_ISPROGRAM)(GLuint);

static SL_TF_CREATESHADER       p_CreateShader;
static SL_TF_SHADERSOURCE       p_ShaderSource;
static SL_TF_COMPILESHADER      p_CompileShader;
static SL_TF_GETSHADERIV        p_GetShaderiv;
static SL_TF_GETSHADERINFOLOG   p_GetShaderInfoLog;
static SL_TF_CREATEPROGRAM      p_CreateProgram;
static SL_TF_ATTACHSHADER       p_AttachShader;
static SL_TF_LINKPROGRAM        p_LinkProgram;
static SL_TF_GETPROGRAMIV       p_GetProgramiv;
static SL_TF_GETPROGRAMINFOLOG  p_GetProgramInfoLog;
static SL_TF_USEPROGRAM         p_UseProgram;
static SL_TF_GETUNIFORMLOCATION p_GetUniformLocation;
static SL_TF_UNIFORM1F          p_Uniform1f;
static SL_TF_UNIFORM3F          p_Uniform3f;
static SL_TF_UNIFORM1I          p_Uniform1i;
static SL_TF_UNIFORM1FV         p_Uniform1fv;
static SL_TF_TFVARYINGS         p_TransformFeedbackVaryings;
static SL_TF_GENBUFFERS         p_GenBuffers;
static SL_TF_BINDBUFFER         p_BindBuffer;
static SL_TF_BUFFERDATA         p_BufferData;
static SL_TF_BINDBUFFERBASE     p_BindBufferBase;
static SL_TF_GETBUFFERPARAMETERIV p_GetBufferParameteriv;
static SL_TF_BEGINTF            p_BeginTransformFeedback;
static SL_TF_ENDTF              p_EndTransformFeedback;
/* NULL until the ONE capture resolves it, and NULL again with the context. */
static SL_TF_GETBUFFERSUBDATA   p_GetBufferSubData;
/* NULL until the first isolating draw resolves it, and NULL again with the
 * context: an entry point belongs to a context like every other. */
static SL_TF_CLIENTACTIVETEXTURE p_ClientActiveTexture;
/* NULL until a selected launch's first offered setup resolves them, and NULL
 * again with the context. */
static SL_TF_GENVERTEXARRAYS    p_GenVertexArrays;
static SL_TF_BINDVERTEXARRAY    p_BindVertexArray;
static SL_TF_DELETEPROGRAM      p_DeleteProgram;
static SL_TF_DELETESHADER       p_DeleteShader;
/* ---- THE AUXILIARY CONTEXT'S OWN ENTRY POINTS (SL_BG_TF_AUX_EMPTY) ------
 *
 * EVERY POINTER ABOVE BELONGS TO THE MAIN CONTEXT AND IS CALLED ONLY THERE.
 * The two the auxiliary-context control needs on the auxiliary context are
 * resolved AGAIN, after that context has been made current, and kept apart from
 * the main set for the whole of that context's life.
 *
 * WHY, AND WHY IT IS NOT A PRECAUTION. The resolver is the platform's:
 * "On Windows, function pointers are specific to the current GL context" and
 * "If you recreate your context or create a second context, you should assume
 * that any existing function pointers aren't valid to use with it" -
 * https://wiki.libsdl.org/SDL2/SDL_GL_GetProcAddress, which adds "you should
 * expect crashes if you don't respect it". The platform call underneath says the
 * same of the addresses themselves: "Extension functions supported in one
 * rendering context are not necessarily available in a separate rendering
 * context. Thus, for a given rendering context in an application, use the
 * function addresses returned by the wglGetProcAddress function only"
 * (https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglgetprocaddress).
 * So a main-context pointer invoked while the auxiliary context is current is
 * undefined by the documented contract, whatever a given driver happens to do -
 * and the one thing this control exists to produce is a trustworthy reading, so
 * it cannot rest on a driver's tolerance.
 *
 * A MISSING POINTER IS A REFUSAL, NEVER A FALLBACK TO THE MAIN SET. The
 * auxiliary context is destroyed and the served draw goes to the CPU path. That
 * is the whole of the policy: there is no path on which a pointer resolved on
 * one context is invoked on another.
 *
 * AND THE MAIN CONTEXT'S ADDRESS FOR THE QUERY IS KEPT AS void *, DELIBERATELY.
 * It is a PROBE and nothing else - it answers "does this build's GL expose
 * glIsProgram at all", which is what makes "no context is created that cannot be
 * verified" a property of the sequence rather than a promise - and storing it
 * untyped is what makes invoking it impossible rather than merely forbidden. A
 * typed pointer here would be one keystroke away from the defect this whole
 * block exists to remove.
 *
 * THE GL 1.1 CALLS ARE NOT IN THIS SET, and that is a difference in kind rather
 * than an exception. glEnable, glDisable and glDrawArrays are called directly,
 * through the link-time import of the system GL library, so this module holds no
 * pointer to them to resolve per context and none to get wrong; the platform's
 * resolver covers "extension functions", which "are not exported by OpenGL"
 * (same wgl page), and these are exported. The dispatch those direct calls go
 * through is the implementation's own, against whatever context the thread has
 * current - which is the auxiliary one at each of those three call sites, by the
 * verified switch that precedes them. The boundary: if a future edit needs any
 * entry point on the auxiliary context that must be RESOLVED, it belongs in this
 * set and not in the main one. */
static void                     *g_ax_probe;
static SL_TF_ISPROGRAM          p_ax_IsProgram;
static SL_TF_USEPROGRAM         p_ax_UseProgram;
/* ---- THE AUXILIARY CONTEXT'S REAL-PREPASS SET (SL_BG_TF_AUX_REAL) -------
 *
 * THE SAME RULE AS THE PAIR ABOVE, APPLIED TO A LARGER SET. The real prepass
 * needs more than a program switch on the auxiliary context, and every one of
 * these names is resolved THERE, after that context has been made current, and
 * invoked only while it is current. The rule and the two platform citations are
 * at p_ax_IsProgram; nothing about them is weaker for a bigger set.
 *
 * WHICH NAMES, AND WHY EACH IS HERE RATHER THAN CALLED DIRECTLY. All of them are
 * post-1.1 and so must be resolved: glIsBuffer, glBindBuffer and glBufferData's
 * family are GL 1.5, glBindBufferBase and the feedback pair are GL 3.0, the
 * vertex-array pair is GL 3.0 / ARB_vertex_array_object, and the four uniform
 * writers are GL 2.0. glEnable, glDisable, glDrawArrays, glVertexPointer,
 * glEnableClientState, glDisableClientState and glFinish are GL 1.0/1.1 core
 * exports called directly, for the reason stated at p_ax_IsProgram: the
 * platform's resolver covers extension functions, these are exported, and this
 * module holds no per-context pointer to them to get wrong.
 *
 * NO DELETE FOR THE VERTEX ARRAY OBJECT IS IN THIS SET, AND THAT IS A PROPERTY
 * OF WHAT THE OBJECT IS. A vertex array object is a CONTAINER object: OpenGL
 * 4.6 Appendix D lists container objects as the ones NOT shared between
 * contexts, which is exactly why this mode has to make its own there - and the
 * other half of that fact is that a container object belongs to one context and
 * is released when that context is destroyed. The auxiliary context is this
 * mode's alone and is destroyed through the backend at teardown, so there is
 * nothing for a glDeleteVertexArrays to free that the destroy does not free,
 * and resolving one would mean a teardown-time switch back to a context that is
 * about to stop existing.
 *
 * A MISSING POINTER IS A REFUSAL AND NEVER A FALLBACK, exactly as above: the
 * context is destroyed again with the main context verified current and the
 * served draw goes to the CPU path. */
typedef GLboolean (APIENTRY *SL_TF_ISBUFFER)(GLuint);
static SL_TF_ISBUFFER           p_ax_IsBuffer;
static SL_TF_BINDBUFFER         p_ax_BindBuffer;
static SL_TF_BINDBUFFERBASE     p_ax_BindBufferBase;
static SL_TF_BEGINTF            p_ax_BeginTransformFeedback;
static SL_TF_ENDTF              p_ax_EndTransformFeedback;
static SL_TF_GENVERTEXARRAYS    p_ax_GenVertexArrays;
static SL_TF_BINDVERTEXARRAY    p_ax_BindVertexArray;
static SL_TF_UNIFORM1F          p_ax_Uniform1f;
static SL_TF_UNIFORM3F          p_ax_Uniform3f;
static SL_TF_UNIFORM1I          p_ax_Uniform1i;
static SL_TF_UNIFORM1FV         p_ax_Uniform1fv;

/* ---- THE FENCE PATH'S THREE NAMES, ON BOTH CONTEXTS (SL_BG_TF_AUX_SYNC) --
 *
 * THE SYNC OBJECT'S HANDLE IS KEPT AS void * AND NOT AS GLsync, DELIBERATELY.
 * GLsync is a GL 3.2 type and this module includes a GL 1.1 header, so naming it
 * would either require a header this build does not have or a local typedef that
 * a real <GL/glext.h> would collide with. OpenGL 4.6 section 4.1 defines GLsync
 * as an opaque POINTER type, so void * is the same object at the ABI and nothing
 * here dereferences it: it is created by one call, passed to two, and dropped.
 *
 * THE 64-BIT TIMEOUT IS SPELLED OUT FOR THE SAME REASON. glWaitSync's third
 * argument is a GLuint64, which is 64 bits on every platform including the 32-bit
 * one this builds for, so a wrong width here would misalign the whole frame on a
 * __stdcall call. `unsigned long long` is that width by the language, and the
 * only value ever passed is GL_TIMEOUT_IGNORED, which OpenGL 4.6 section 4.1.1
 * requires glWaitSync's timeout to be.
 *
 * APIENTRY IS ON EVERY ONE OF THEM, for the reason recorded at the top of this
 * file: these entry points are __stdcall on Windows and the callee cleans the
 * frame, so a typedef without it corrupts the caller's.
 *
 * ALL THREE ARE POST-1.1, SO ALL THREE ARE RESOLVED PER CONTEXT. glFenceSync,
 * glWaitSync and glDeleteSync are GL 3.2, which puts them squarely under the
 * discipline recorded at p_ax_IsProgram: an address resolved on one context is
 * not valid on another, so there are TWO sets and each is invoked only while its
 * own context is current. glFlush is GL 1.0 core and is called directly, exactly
 * as glFinish, glEnable, glDrawArrays and glGetError are, and for the same
 * reason - the platform's resolver answers for extension functions, these are
 * exported, and this module holds no per-context pointer for them to get wrong.
 *
 * glClientWaitSync IS DELIBERATELY NOT HERE. The consumer in both directions
 * does a SERVER wait: it tells its own context's command stream to wait, and
 * returns immediately. A client wait would block the calling thread, which is
 * the cost the fence path exists to remove, so the name is not resolved at all
 * and no path can reach for it.
 *
 * A MISSING POINTER IS A REFUSAL AND NEVER A FALL BACK TO FINISH. A launch that
 * asked for the fence path and silently got two pipeline drains would report
 * fence numbers for a drained pipeline, which is the one outcome that cannot be
 * read at all. */
#define SL_BGTF_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define SL_BGTF_TIMEOUT_IGNORED            0xFFFFFFFFFFFFFFFFull
typedef void * SL_TF_SYNC;
typedef SL_TF_SYNC (APIENTRY *SL_TF_FENCESYNC)(GLenum, GLbitfield);
typedef void       (APIENTRY *SL_TF_WAITSYNC)(SL_TF_SYNC, GLbitfield,
                                              unsigned long long);
typedef void       (APIENTRY *SL_TF_DELETESYNC)(SL_TF_SYNC);
/* THE MAIN CONTEXT'S SET. Resolved while the MAIN context is current and invoked
 * only there: the producer fence and flush for the input handoff, the consumer
 * wait for the output handoff, and the delete of whichever object this context
 * is the one holding when a path has to drop it. */
static SL_TF_FENCESYNC          p_FenceSync;
static SL_TF_WAITSYNC           p_WaitSync;
static SL_TF_DELETESYNC         p_DeleteSync;
/* AND THE AUXILIARY CONTEXT'S, the same three names resolved again on that
 * context after it was verified current, for the mirror-image three jobs. */
static SL_TF_FENCESYNC          p_ax_FenceSync;
static SL_TF_WAITSYNC           p_ax_WaitSync;
static SL_TF_DELETESYNC         p_ax_DeleteSync;

/* THE PREPASS PROGRAM.
 *
 * Compare the body line for line with BGG_VS in sl_gfx_bggpu.c: the three room
 * coordinates and the four clip components are the same expressions in the same
 * order with the same parentheses, and the two depth cases are the same two
 * statements. The differences are confined to what a prepass must change:
 *
 *   - the result goes to `v_clip`, a recorded output varying, instead of
 *     gl_Position, because nothing is rasterised here;
 *   - none of the fixed-function outputs are written, because the raster draw
 *     that follows is the fixed pipeline itself and feeds its own colour, fog
 *     and texture coordinates from the pass's arrays. Writing them here would
 *     be dead work, and - more to the point - the texture matrix a vertex
 *     program would have had to apply by hand is applied by the fixed pipeline
 *     in the draw that actually rasterises, which is the distinction this
 *     candidate exists to restore.
 *
 * FAR1 IS THE ABSENCE OF BOTH STATEMENTS, not a third one: the game's range
 * draws the clip z exactly as computed, so mode 2 falls through with c.z
 * untouched. SINGLE's clamp is a comparison rather than arithmetic and is left
 * in its original form; FAR0's `u_fk1 * c.w - u_fk2` keeps the original
 * multiply-then-subtract order on the same w the clamp never touches. */
static const char *TF_VS =
"#version 400 compatibility\n"
"uniform float u_sc;\n"
"uniform float u_post;\n"
"uniform vec3  u_org;\n"
"uniform float u_C[16];\n"
"uniform int   u_fogless;\n"
"uniform int   u_dmode;\n"
"uniform float u_fk1;\n"
"uniform float u_fk2;\n"
"out vec4 v_clip;\n"
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
"    v_clip = c;\n"
"}\n";

/* THE NEUTRAL-PROGRAM CONTROL'S VERTEX SHADER (SL_BG_TF_NEUTRAL_PROG).
 *
 * MINIMAL IS THE POINT, AND SO IS WHAT IT DOES NOT DECLARE. The control's whole
 * content is that the empty draw runs under a LINKED PROGRAM THAT IS NOT THE
 * PREPASS PROGRAM, so this one has no uniforms to write, no output varying to
 * record, and nothing whose arithmetic could differ from anything: the body is
 * the fixed pipeline's own transform written out, which is the least a
 * compatibility-profile vertex shader can say and still be a legal one.
 *
 * NO TRANSFORM-FEEDBACK VARYING IS NAMED FOR IT, which is the difference that
 * makes the comparison. The prepass program is linked with `v_clip` recorded
 * through glTransformFeedbackVaryings; this program is linked with that call
 * never made, so it captures nothing and could not be the vertex stage of a
 * feedback capture even if one were open. The draw this control issues submits
 * no vertices, so the body never executes on any invocation - it exists to make
 * the link legal, not to compute.
 *
 * THE SAME VERSION DIRECTIVE AS TF_VS, deliberately. A driver that refuses
 * `#version 400 compatibility` refuses both, so the two programs differ in what
 * they declare and not in whether a context will accept the dialect - which is
 * what keeps a failed neutral link attributable to the link rather than to the
 * version line. */
static const char *TF_NP_VS =
"#version 400 compatibility\n"
"void main() {\n"
"    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"}\n";

static GLuint g_prog;
static GLint  g_u_sc = -1, g_u_post = -1, g_u_org = -1, g_u_C = -1, g_u_fogless = -1;
static GLint  g_u_dmode = -1, g_u_fk1 = -1, g_u_fk2 = -1;
static int    g_state;                 /* 0 untried, 1 ready, -1 unavailable */
static char   g_why[256];
static int    g_optin = -1;            /* the switch, parsed once            */
/* v0.4 WAS THE DEVELOPER SWITCH NAMED OFF, as distinct from not named at all?
 *
 * `g_optin` answers 0 for both, which was the whole answer while the prepass was
 * developer-only. It is no longer: the PRODUCTION selection below admits a walk
 * the OPTIMIZED RENDERING row admitted, so "nobody named the switch" has to mean
 * "the row decides" while "a developer named 0" has to keep meaning "no prepass
 * this launch, whatever the row says". A developer forcing the switch off is the
 * one way to take the prepass out of a player session without moving the row,
 * which is what a bisection against the original renderer needs.
 *
 * -1 until the switch is parsed, 1 for the exact text "0", 0 for every other
 * state - absent, empty, "1" and an unrecognised value alike. Set by the one
 * parser in sl_bgtf_opt_in, so there is no second read of the key. */
static int    g_clip_off = -1;
static int    g_rcpu = -1;             /* the raster-source control, likewise */
static int    g_sonly = -1;            /* the state-only control, likewise   */
static int    g_pin = -1;              /* the position-only-input control    */
static int    g_pvao = -1;             /* the private-vertex-array control   */
static int    g_zc = -1;               /* the zero-vertex-count control      */
static int    g_es = -1;               /* the empty-draw state mode          */
static int    g_pr = -1;               /* the post-reset boundary control    */
static int    g_np = -1;               /* the neutral-program control        */
/* THE NEUTRAL PROGRAM, ITS CAPABILITY STATE AND ITS ONE REPORTED FAILURE.
 *
 * `g_np_prog` is the object, 0 until one is linked. `g_np_state` is 0 untried,
 * 1 ready and -1 a LATCHED failure: a context that could not make the program
 * refuses every later served draw without compiling anything again, which is
 * what keeps one exhausted context from paying a compile per mesh. Both belong
 * to a CONTEXT and are cleared by sl_bgtf_invalidate.
 *
 * `g_np_said` belongs to the LAUNCH rather than to a context, for the reason
 * `g_pr_said` does: the failure is one fact about this run and a context
 * recreation does not earn a second copy of the same statement. It is therefore
 * deliberately not cleared by sl_bgtf_invalidate. */
static GLuint g_np_prog;
static int    g_np_state;
static int    g_np_said;
static int    g_ax = -1;               /* the auxiliary-context control      */
/* THE AUXILIARY CONTEXT'S CAPABILITY STATE, ITS LOST LATCH AND ITS ONE REPORTED
 * FAILURE (SL_BG_TF_AUX_EMPTY).
 *
 * `g_ax_state` is 0 untried, 1 verified and -1 a LATCHED failure: a main context
 * that could not be given a verified auxiliary context refuses every later
 * served draw without trying again, which keeps one exhausted context from
 * paying a creation attempt per mesh. It belongs to a MAIN CONTEXT and is
 * cleared by sl_bgtf_invalidate.
 *
 * `g_ax_live` is 1 while an auxiliary context exists that this process is
 * responsible for destroying. It is kept SEPARATE from `g_ax_state` because the
 * two are not the same fact: a context that was created and then failed its
 * share check is unusable (state -1) and still has to be destroyed (live 1), and
 * a teardown reading only the state would leave it behind.
 *
 * `g_ax_lost` IS THE ONE LATCH THAT IS NOT ABOUT A CONTEXT BUT ABOUT THIS
 * PROCESS. It is raised wherever the backend answers that which context is
 * current is UNKNOWN, which means nothing here can say which context a later GL
 * command would reach.
 *
 * AND IN THE PRODUCT IT IS UNREACHABLE AS A STATE, which is the point of saying
 * so here. Every site that raises it reports the run invalid and terminates the
 * process immediately (tf_ax_lost), so no later served draw, no teardown and no
 * exit handler runs at all. The latch is kept for two reasons: a fixture
 * intercepts the terminate, and this code must behave correctly when it returns
 * rather than relying on a call that cannot be observed; and defence in depth
 * costs one integer test. It is deliberately NOT cleared by sl_bgtf_invalidate -
 * a context recreation does not restore knowledge that was lost.
 *
 * `g_ax_said` belongs to the LAUNCH rather than to a context, for the reason
 * `g_np_said` does, and is likewise not cleared by sl_bgtf_invalidate. */
static int    g_ax_state;
static int    g_ax_live;
static int    g_ax_lost;
static int    g_ax_said;
static int    g_ar = -1;               /* the real-prepass mode's arm        */
/* THE REAL-PREPASS MODE'S OWN CAPABILITY STATE (SL_BG_TF_AUX_REAL).
 *
 * `g_ar_state` is 0 untried, 1 ready and -1 a LATCHED failure, on the same terms
 * as `g_ax_state` and kept SEPARATE from it because the two name different
 * capabilities of the same context: a context can be share-verified for the
 * empty-draw control's two entry points and still refuse one of the larger set
 * this mode needs. It belongs to a MAIN CONTEXT and is cleared by
 * sl_bgtf_invalidate.
 *
 * `g_ar_vao` is this mode's own vertex array object ON THE AUXILIARY CONTEXT, 0
 * until one is generated there. It is a container object of that context and is
 * released when that context is destroyed - see the entry-point set above - so
 * it is cleared wherever the context is forgotten and never deleted by hand.
 *
 * `g_ar_said` belongs to the LAUNCH rather than to a context, for the reason
 * `g_ax_said` does, and is likewise not cleared by sl_bgtf_invalidate. */
static int    g_ar_state;
static GLuint g_ar_vao;
static int    g_ar_said;
/* THE OPT-IN CANDIDATE'S TWO FACTS, DECIDED BY THE ONE ARBITER AND READ NOWHERE
 * ELSE (SL_BG_TF_CLIP=1 with SL_BG_TF_AUX_REAL NOT REQUESTED).
 *
 * `g_ar_implicit` is 1 when the arm in force was selected by the candidate's own
 * default rather than NAMED by the launch. It is the difference between "this
 * launch asked for GPU_SOURCE" and "this launch asked for the clip prepass and
 * the clip prepass is this arm", and it is the fact a census needs: the two run
 * the same commands, and only one of them is an A/B arm a reader set up.
 *
 * WHY THE DEFAULT IS THIS ARM AND NOT THE MAIN-CONTEXT PREPASS. Measured on one
 * Runway viewpoint at three moments, one machine and one driver: the auxiliary
 * GPU_SOURCE prepass under either synchronisation mode produced frames
 * byte-identical to the original renderer's, blue 143 at (71,113), while the
 * MAIN-context prepass produced blue 142 in the same scene from the same seeded
 * save. The main-context prepass is therefore retained as a DIAGNOSTIC with a
 * known mismatch and is never what an unqualified opt-in selects.
 *
 * `g_ar_closed` IS THE FAIL-CLOSED LATCH, and it is about the shapes a selector
 * grammar cannot otherwise answer safely: a launch that opted into the clip
 * prepass and then offered a value that cannot be read. There are two such
 * values and the latch covers both:
 *
 *   - AN UNREADABLE ARM (SL_BG_TF_AUX_REAL). Before this latch that launch ran
 *     the main-context prepass - the known-mismatching path - under a typo.
 *   - AN UNREADABLE SYNCHRONISATION MODE (SL_BG_TF_AUX_SYNC) on a launch with an
 *     arm in force, the candidate's and a named one alike. Before this latch
 *     such a launch was served with one of the two modes chosen for it, which
 *     ran a synchronisation it had not selected; and refusing the arm without
 *     the latch would have handed an opted-in walk to the main-context prepass.
 *
 * With it, every served draw is refused to the EXISTING CPU raster path, which
 * is what every other capability failure in this file already does. It is set
 * only on a launch that opted in - the arm's case asks the opt-in answer
 * directly, and the synchronisation case is behind an arm being in force, which
 * nothing but an opted-in launch can be - so a launch without the opt-in switch
 * is untouched by it.
 *
 * `g_ar_optin_said` and `g_ar_closed_said` are not needed as separate latches -
 * the arbiter's body runs once per launch - and are absent for that reason; each
 * line below is printed from inside that one-shot body. */
static int    g_ar_implicit;
static int    g_ar_closed;
/* v0.4 THE PRODUCTION SELECTION'S TWO FACTS, SPLIT BY WHO IS ALLOWED TO DECIDE
 * EACH (the OPTIMIZED RENDERING row, src/platform/sl_perf.h).
 *
 * `g_prod_elig` is a statement about THE ENVIRONMENT ONLY: this launch named no
 * arm, named no main-context diagnostic, did not name the developer switch at
 * all and offered no unreadable selector, so the only thing left that can decide
 * whether a walk reaches a prepass is the row. The arbiter computes it from
 * answers it already has, writes nothing else for it and PRINTS NOTHING about
 * it. -1 until the arbiter has run.
 *
 * `g_prod_armed` is the launch-level promotion: -1 until a ROW-ADMITTED WALK has
 * asked (sl_bgtf_walk_admit), then 0 or 1. The two are separate because the
 * environment cannot say whether the row admitted anything, and the arbiter runs
 * on launches that draw nothing at all - a census on a trace replay, a headless
 * health run, the matching build. Deciding the promotion there would have put
 * this selection's startup line and its fail-closed refusal into runs whose
 * settings store is INACTIVE and which therefore never reach a prepass: the
 * arbiter's own output has to stay the output those runs already have. So the
 * promotion is taken at the first walk the row admitted, and a launch that
 * admits none is byte for byte the launch it was.
 *
 * `g_prod_said` is 1 only once the arm is actually in force by this route, so a
 * census can tell a production launch from a developer one that reached the
 * identical arm, and from a production launch that failed closed and reached no
 * arm at all. It is the ONE thing read by a census, which is why reading it
 * promotes nothing (sl_bgtf_production). */
static int    g_prod_elig = -1;
static int    g_prod_armed = -1;
static int    g_prod_said;
/* THE TWO ERROR-STATE REPORTS, EACH ONCE PER LAUNCH AND SEPARATELY
 * (SL_BG_TF_AUX_REAL).
 *
 * `g_ar_gl_said` gates the report of an error this mode's OWN commands left
 * behind on the auxiliary context. `g_ar_gl_pre_said` gates the report of error
 * codes that were already flagged there when a bracket arrived. Both are
 * refusals and both latch the capability; what differs is the ATTRIBUTION, which
 * is why they are counted and reported apart - one sends a reader to the
 * context's creation or to this mode's acquire, the other to the command the
 * bracket itself issued - and neither may suppress or be suppressed by the
 * other.
 *
 * NEITHER SHARES g_ar_said, and that is the point of having them. A launch whose
 * objects were not shared and whose next bracket was rejected by the driver has
 * two things to tell a reader, and one latch would print whichever came first
 * and silence the other.
 *
 * Both belong to the LAUNCH rather than to a context, for the reason g_ar_said
 * does, and are likewise not cleared by sl_bgtf_invalidate: the failure is one
 * fact about this run and a context recreation does not earn a second copy of
 * the same statement. */
static int    g_ar_gl_said;
static int    g_ar_gl_pre_said;
/* THE SYNCHRONISATION MODE'S OWN STATE (SL_BG_TF_AUX_SYNC).
 *
 * `g_fx` is the REQUESTED mode, -1 until the key has been read. It is read
 * lazily, and only from sites already behind the real-prepass mode being in
 * force, so an off launch never performs the getenv - which is what makes the
 * default path's invariance structural rather than asserted.
 *
 * `g_fx_ok` is whether the fence path's six entry points were all resolved, 0
 * until the capability ran. It is the difference between "FENCE was asked for"
 * and "FENCE is in force", and only the second may change a single call.
 *
 * `g_fx_said` gates the CAPABILITY refusal's report and `g_fx_cre_said` the
 * report of a sync object a context would not create. They are separate latches
 * because they name different facts - a context that cannot supply the entry
 * points and a context that supplies them and then answers 0 - and one latch
 * would let whichever happened first silence the other. Both belong to the LAUNCH
 * rather than to a context, for the reason g_ar_said does, and are likewise not
 * cleared by sl_bgtf_invalidate. */
static int    g_fx = -1;
static int    g_fx_ok;
static int    g_fx_said;
static int    g_fx_cre_said;
/* THE WALK'S UNIFORM VALUES, CACHED ON THE MAIN CONTEXT AND REWRITTEN ON THE
 * AUXILIARY ONE (SL_BG_TF_AUX_REAL).
 *
 * WHY THEY ARE CACHED AT ALL. sl_bgtf_begin_walk writes the five walk-level
 * uniforms once per walk with the prepass program current on the MAIN context,
 * and the per-draw bracket writes the other three. A uniform's value is state of
 * the PROGRAM object, which is shared - so an argument could be made that the
 * auxiliary context would see them. Caching the values and writing all eight
 * again on that context removes the argument entirely: nothing this mode does
 * depends on cross-context visibility of program state, only on the buffer
 * visibility the synchronisation above establishes.
 *
 * THE DEFAULT PATH PAYS NOTHING FOR THEM. These are plain assignments in
 * begin_walk - no GL call is added, no call is reordered, and an off launch
 * never reads them back. */
static GLfloat g_wu_sc, g_wu_post, g_wu_org[3], g_wu_C[16];
static GLint   g_wu_fogless;
/* ONE LINE PER LAUNCH FOR AN UNPROVED BOUNDARY PRECONDITION. The condition is
 * per bracket and the count is per bracket, but a scene's worth of identical
 * lines would bury the run's own output and tell a reader nothing the count does
 * not. It belongs to the LAUNCH rather than to a context, so it is deliberately
 * not cleared by sl_bgtf_invalidate: a context recreation does not earn a second
 * copy of the same statement. */
static int    g_pr_said;

static struct sl_bgtf_counters g_c;

/* ---- the empty-draw state matrix ----------------------------------------
 *
 * THE ROWS, WRITTEN DOWN ONCE. sl_bgtf_es_matrix and sl_bgtf_es_switches are the
 * only readers, the draw site and the census both go through them, and the
 * fixtures assert over them, so no second copy of the table exists to drift from
 * this one.
 *
 * WHAT A ROW HOLDS. `tf_active` and `discard_on` are the two conditions at the
 * empty draw. The four switch fields are the glUseProgram calls the row issues
 * around it, in fixed positions: 0 and then the prepass program BEFORE the draw,
 * and the same two in the same order AFTER it. The program CURRENT at the draw
 * is not stored - it is derived in sl_bgtf_es_matrix from the two pre-draw
 * fields, so a row cannot declare one program and issue the calls for another.
 *
 * THE INVALID PAIRING IS ABSENT BY CONSTRUCTION. glUseProgram while transform
 * feedback is active is invalid, so no row pairs tf_active 1 with any switch; a
 * fixture asserts that over this table rather than over the draw site's
 * branches, because the table is where such a row could be introduced. */
struct tf_es_row {
    int tf_active, discard_on;
    int pre_zero, pre_prog, post_zero, post_prog;
    const char *name;
};

static const struct tf_es_row g_es_rows[] = {
    /*  T  D  pre0 preP post0 postP  name */
    {   1, 1,   0,   0,    0,    0, "OFF" }, /* Z_KNOWN, nothing changed */
    {   0, 1,   0,   0,    0,    0, "A"   },
    {   1, 0,   0,   0,    0,    0, "B"   },
    {   0, 0,   0,   0,    0,    0, "C"   },
    {   0, 1,   1,   0,    0,    1, "E"   },
    {   0, 1,   1,   1,    0,    0, "R"   },
    {   0, 1,   1,   0,    0,    0, "S"   },
    {   0, 1,   0,   0,    1,    1, "Q"   }
};
#define TF_ES_ROWS ((int) (sizeof g_es_rows / sizeof g_es_rows[0]))

/* THE ROW, OR THE UNCHANGED STATE. An out-of-range mode answers row 0, not a
 * default row: the zero-count arm's own state is what nothing was changed from,
 * so a caller handed a value this module did not produce behaves as though no
 * mode were selected rather than running one. */
static const struct tf_es_row *tf_es_row(int mode)
{
    return (mode > 0 && mode < TF_ES_ROWS) ? &g_es_rows[mode] : &g_es_rows[0];
}

void sl_bgtf_es_matrix(int mode, int *tf_active, int *discard_on,
                       int *program_tf)
{
    const struct tf_es_row *r = tf_es_row(mode);
    if (tf_active  != NULL) *tf_active  = r->tf_active;
    if (discard_on != NULL) *discard_on = r->discard_on;
    /* DERIVED, NOT STORED. The program at the draw is 0 exactly when the row
     * switched to 0 before the call and did not switch back before it. Row 0's
     * two zeroes therefore answer 1, which is the prepass program the common
     * bracket left current. */
    if (program_tf != NULL)
        *program_tf = (r->pre_zero && !r->pre_prog) ? 0 : 1;
}

void sl_bgtf_es_switches(int mode, int *pre_zero, int *pre_prog,
                         int *post_zero, int *post_prog)
{
    const struct tf_es_row *r = tf_es_row(mode);
    if (pre_zero  != NULL) *pre_zero  = r->pre_zero;
    if (pre_prog  != NULL) *pre_prog  = r->pre_prog;
    if (post_zero != NULL) *post_zero = r->post_zero;
    if (post_prog != NULL) *post_prog = r->post_prog;
}

const char *sl_bgtf_es_name(int mode)
{
    if (mode == 0) return g_es_rows[0].name;
    if (mode > 0 && mode < TF_ES_ROWS) return g_es_rows[mode].name;
    return "?";
}

/* ---- the post-reset boundary's arms -------------------------------------
 *
 * THE ARMS, WRITTEN DOWN ONCE. sl_bgtf_pr_calls and sl_bgtf_pr_name are the only
 * readers, the boundary and the census both go through them, and the fixtures
 * assert over them, so no second copy of this table exists to drift from it.
 *
 * `name` IS THE SELECTOR TOKEN, not a label beside one. The parser compares the
 * offered value against these same strings, so the grammar and the record cannot
 * disagree about how an arm is spelled, and an arm cannot be added to one without
 * the other.
 *
 * THE ORDER OF THE TWO CALLS IS NOT IN THE TABLE, because it is fixed for every
 * arm: the empty draw precedes the glFinish. Storing it would admit a row whose
 * declared order and issued order differed, which is the one defect this split
 * cannot absorb. */
struct tf_pr_arm {
    int empty_draw, finish;
    const char *name;
};

static const struct tf_pr_arm g_pr_arms[] = {
    /* draw finish  name (the selector token)   */
    {   0,   0,    "OFF"                  },  /* the pass unchanged  */
    {   1,   0,    "POST_PROGRAM0_EMPTY"  },
    {   0,   1,    "POST_FINISH"          },
    {   1,   1,    "BOTH"                 }
};
#define TF_PR_ARMS ((int) (sizeof g_pr_arms / sizeof g_pr_arms[0]))

/* THE ARM, OR THE UNCHANGED PATH. An out-of-range value answers arm 0, not a
 * default arm: nothing added is what nothing was changed from, so a caller
 * handed a value this module did not produce behaves as though nothing were
 * selected rather than running an arm. */
static const struct tf_pr_arm *tf_pr_arm(int mode)
{
    return (mode > 0 && mode < TF_PR_ARMS) ? &g_pr_arms[mode] : &g_pr_arms[0];
}

void sl_bgtf_pr_calls(int mode, int *empty_draw, int *finish)
{
    const struct tf_pr_arm *a = tf_pr_arm(mode);
    if (empty_draw != NULL) *empty_draw = a->empty_draw;
    if (finish     != NULL) *finish     = a->finish;
}

const char *sl_bgtf_pr_name(int mode)
{
    if (mode == 0) return g_pr_arms[0].name;
    if (mode > 0 && mode < TF_PR_ARMS) return g_pr_arms[mode].name;
    return "?";
}

/* ---- the position-only-input control's saved client state ----------------
 *
 * THE SWEEP'S CEILING, and the same reason src/gfx/sl_gfx_skybatch.c:184-191
 * gives for its own: every coordinate array has to be accounted for, because an
 * array enabled on ANY unit is dereferenced by the draw. A fixed ceiling keeps
 * the saved state bounded, and a context reporting a wider domain than this
 * REFUSES to isolate rather than sweeping part of the range and leaving the rest
 * to feed the draw. The domain is GL_MAX_TEXTURE_COORDS, which is 8 or fewer on
 * the compatibility profiles this renderer runs on, so 16 is headroom rather
 * than a guess that might bind. */
#define TF_UNITS 16

/* THE SAVED SELECTOR'S "NOT ANSWERED" VALUE, and it is not 0.
 *
 * A zeroed save cannot be told apart from a query that wrote nothing, because 0
 * is a value a struct holds by default. This one is negative, and no legal
 * answer to GL_CLIENT_ACTIVE_TEXTURE is: the answer is GL_TEXTURE0 + index, and
 * GL_TEXTURE0 is 0x84C0. So a save still holding this value means the context
 * did not answer, which is a refusal rather than unit 0. */
#define TF_CLI_UNIT_NONE (-1)

/* WHAT WAS INHERITED, held only across one draw.
 *
 * ENABLES AND THE UNIT SELECTION, AND NOTHING ELSE. Disabling a client array
 * leaves its pointer, size, type, stride and buffer binding exactly as they
 * were, so none of those is read or written and no pointer command is issued on
 * any path: the arrays come back pointing where they pointed. That is what keeps
 * this control from altering pointers or current attributes globally, and it is
 * why this save is a fraction of the one the sky batch needs - that module
 * installs its own pointers, and this one installs none.
 *
 * GL_VERTEX_ARRAY IS ABSENT ON PURPOSE. It is the position input the prepass
 * exists to read, so it is neither saved nor touched: an array this control must
 * not disturb is better not named than named and skipped.
 *
 * GENERIC VERTEX ATTRIBUTE ARRAYS ARE ABSENT FOR A MEASURED REASON, not an
 * assumption. Searched over the whole tree: glEnableVertexAttribArray,
 * glDisableVertexAttribArray and glVertexAttribPointer appear NOWHERE in this
 * repository, so no generic array can be enabled for the prepass draw to
 * dereference and there is no such state to account for. If one is ever
 * introduced, this sweep stops being complete and must grow with it. */
struct tf_cli_saved {
    int       held;                    /* a draw's worth of live GL state     */
    GLint     coords;                  /* the sweep's width, <= TF_UNITS      */
    GLint     client_unit;             /* GL_CLIENT_ACTIVE_TEXTURE inherited  */
    GLboolean t_on[TF_UNITS];          /* per-unit coordinate arrays          */
    GLboolean c_on, n_on, i_on, e_on, s_on, f_on;
    unsigned  off;                     /* arrays this draw actually disabled  */
};

static struct tf_cli_saved g_cli;
static int g_pin_said;                 /* a refusal is reported once          */

/* ---- the private-prepass-vertex-array control's state --------------------
 *
 * THE "NOT ANSWERED" VALUE FOR AN INHERITED BINDING, AND IT IS NOT 0. Object 0
 * is the default array object and a perfectly ordinary answer to
 * GL_VERTEX_ARRAY_BINDING, so a zeroed save cannot be told apart from a query
 * that wrote nothing - and putting back 0 on a caller that had its own object
 * current is exactly the corruption this control exists to avoid. This value is
 * negative, which no object name is, so a save still holding it means the
 * context did not answer and the control refuses rather than assuming 0. */
#define TF_VAO_NONE (-1)

/* THE ONE CONTEXT-OWNED OBJECT and what is held across a setup.
 *
 * `name` is generated once per context and reused for every setup, because the
 * descriptor this control installs is the same shape every time and generating
 * one per draw would be a resource churn for no isolation. It is a NAME FROM ONE
 * CONTEXT and is forgotten, never deleted or bound, when that context ends.
 *
 * `saved` and `held` live only across one setup's bracket: `held` is what makes
 * the restore issuable at most once and never against a context that was not
 * actually left with the private object bound. */
static GLuint g_pvao_name;
static GLint  g_pvao_saved = TF_VAO_NONE;
static int    g_pvao_held;
static int    g_pvao_said;             /* a refusal is reported once          */

/* THE KEYED SLOT CACHE.
 *
 * A slot holds one prepass output pool and the key it was filled for. The key
 * is the whole identity of the work: the walk generation, the depth mode, and
 * FAR0's two coefficients. Nothing is reused across a different walk even if
 * the numbers compare equal, because the generation differs; and a blended
 * mesh's first range can never be served by the opaque pair's pool, because
 * the coefficients are in the key.
 *
 * FOUR SLOTS, and the number is a consequence rather than a preference: one
 * walk needs SINGLE, or it needs FAR1 plus a FAR0 pool per distinct
 * coefficient pair, and the pass has two (opaque and blend overlap). Three
 * covers the worst observed case and the fourth absorbs an item that carries
 * another pair without evicting on every draw. An eviction is counted, not
 * hidden, so a frame that needed more than this says so in the census instead
 * of quietly re-running prepasses.
 *
 * The coefficients are compared as BITS, not with ==. Two keys that differ only
 * in the sign of a zero must not be treated as the same pool, and a NaN
 * coefficient must not make a key equal to nothing including itself - either
 * would silently hand a draw the wrong pool. */
#define TF_SLOTS 4

struct tf_slot {
    GLuint   buf;
    unsigned cap_verts;      /* vertices the buffer is sized for */
    unsigned gen;            /* walk generation it was filled for */
    int      dmode;
    unsigned k1bits, k2bits;
    unsigned nverts;         /* vertices actually written */
    int      filled;
    /* IS THERE OUTPUT BEHIND THE KEY? `filled` says the slot carries this key -
     * which is what the reuse cadence is keyed on - and this says a prepass draw
     * actually wrote the pool. They are the same thing on every ordinary arm and
     * come apart under SL_BG_TF_STATE_ONLY, where the key is installed with the
     * vertex draw omitted. Anything that would READ the storage - the binding
     * and the production diagnostic's selector - requires this one, so an
     * unwritten pool can never be rasterised or recorded. */
    int      written;
    unsigned long used;      /* for the eviction choice: least recently used */
};

static struct tf_slot g_slot[TF_SLOTS];
static unsigned long  g_tick;            /* monotonic, for recency */

/* This walk's state, latched by sl_bgtf_begin_walk. */
static unsigned g_gen;
static unsigned g_nverts;
static GLuint   g_in_vbo;
static unsigned g_in_stride;
static int      g_walk_live;
static int      g_walk_counted;

/* THE RENDERER-STATE PROBE'S PRODUCER KEY, filled from this module's own facts
 * at the call so the probe cannot disagree with this file about which prepass
 * setup a draw is being served by. It is composed here rather than at each of
 * the four hooks so those stay one line each. */
static void bgtf_state_key(struct sl_bgstate_producer *k, int slot, int dmode,
                           unsigned k1b, unsigned k2b, int reused,
                           int state_only)
{
    k->slot = slot;
    k->gen = g_gen;
    k->dmode = dmode;
    k->k1bits = k1b;
    k->k2bits = k2b;
    k->nverts = g_nverts;
    k->reused = reused;
    k->state_only = state_only;
}

static unsigned f2bits(float f)
{
    unsigned u;
    memcpy(&u, &f, sizeof u);
    return u;
}

int sl_bgtf_state(void) { return g_state; }
const char *sl_bgtf_why(void) { return g_why; }
const struct sl_bgtf_counters *sl_bgtf_stats(void) { return &g_c; }

/* THE FENCE PATH'S OWN CENSUS, IN ITS OWN STRUCT (SL_BG_TF_AUX_SYNC). Kept apart
 * from g_c for the reason recorded in the header: the ar_ members of g_c are
 * printed in full by a product census line, so a member added there either moves
 * that line or is a field the line does not print. */
static struct sl_bgtf_fence_counters g_fc;
const struct sl_bgtf_fence_counters *sl_bgtf_fence_stats(void) { return &g_fc; }

const char *sl_bgtf_fx_name(int mode)
{
    switch (mode) {
    case SL_BGTF_FX_FINISH: return "FINISH";
    case SL_BGTF_FX_FENCE:  return "FENCE";
    default:                return "?";
    }
}

static int tf_refuse(const char *why)
{
    g_state = -1;
    snprintf(g_why, sizeof g_why, "%s", why);
    fprintf(stderr, "#95 background GPU clip prepass unavailable: %s - the "
                    "background keeps the existing path\n", why);
    return 0;
}

/* THE DEVELOPER SWITCH, parsed once for the launch, AND NOW ONE OF TWO WAYS IN.
 *
 * WHAT IT STILL IS. The switch that admits a walk to the prepass whatever the
 * OPTIMIZED RENDERING row says, and the one that keeps every main-context
 * diagnostic reachable: SL_BG_TF_CLIP=1 is the launch every A/B arm in this file
 * is written against, and its meaning, its grammar and its output are unchanged.
 *
 * WHAT IT IS NO LONGER. The only way in. The row admits a walk on its own
 * through the PRODUCTION selection (sl_bgtf_walk_admit), so an absent switch
 * means "the row decides" rather than "off". A developer who wants the prepass
 * out of a player session names 0 and gets exactly that, which is the one answer
 * an absent key can no longer carry.
 *
 * Only the exact text "1" selects it; anything else, including an empty value,
 * leaves this answer 0, and an unrecognised value is reported once rather than
 * guessed at. An unrecognised value is NOT an explicit off - it asked for
 * nothing, so it leaves the row to decide, exactly as absence does. */
int sl_bgtf_opt_in(void)
{
    if (g_optin < 0) {
        const char *e = getenv("SL_BG_TF_CLIP");
        g_clip_off = 0;
        if (e == NULL || e[0] == '\0') {
            g_optin = 0;
        } else if (strcmp(e, "1") == 0) {
            g_optin = 1;
        } else if (strcmp(e, "0") == 0) {
            g_optin = 0;
            g_clip_off = 1;
        } else {
            g_optin = 0;
            fprintf(stderr, "#95 SL_BG_TF_CLIP=\"%s\" is not 0 or 1 - the GPU clip "
                            "prepass candidate stays off\n", e);
        }
    }
    return g_optin;
}

/* v0.4 WAS THE DEVELOPER SWITCH NAMED OFF? Parsed by the one function above, so
 * this adds no environment read on any path; the call orders the parse first
 * rather than assuming a caller already did. */
static int tf_clip_named_off(void)
{
    (void) sl_bgtf_opt_in();
    return g_clip_off == 1;
}

/* THE ORIGINAL-RASTER CONTROL, parsed once for the launch.
 *
 * WHAT IT CHANGES. With the candidate selected, the prepass still runs - the
 * same program, the same depth and coefficients, the same slot keys and the
 * same cache behaviour - but the raster draw that follows reads the ORIGINAL
 * CPU position stream instead of the prepass pool. What varies between this and
 * an ordinary candidate launch is therefore whether the prepass's OUTPUT is
 * consumed, not whether the prepass ran.
 *
 * WHY THE DISTINCTION IS WORTH ISOLATING. Measured: the production prepass pool
 * and the CPU clip pool are equal in all 451456 bytes for the draw that creates
 * the defect, all 31692 indexed components agree, and the one blue byte at
 * (71,113) still differs. Equal submitted bytes with a differing pixel leave
 * the prepass's EXECUTION - its program, its transform-feedback binding, its
 * rasterizer-discard bracket - as a difference the submitted bytes cannot
 * account for. This control removes the pool from the raster while keeping that
 * execution, so the two can be told apart.
 *
 * IT SELECTS NOTHING BY ITSELF. SL_BG_TF_CLIP decides whether a walk reaches the
 * candidate at all; this only changes what an already-admitted walk rasterises
 * from. Set on its own it has no effect of any kind.
 *
 * NEITHER A FIX NOR A PERFORMANCE PATH. The prepass's cost is paid and its
 * output is then discarded, so a launch under this control is slower than both
 * arms by construction and its frame times measure nothing. Only the exact text
 * "1" selects it; anything else is off, and an unrecognised value is reported
 * once rather than guessed at. */
int sl_bgtf_raster_cpu(void)
{
    if (g_rcpu < 0) {
        const char *e = getenv("SL_BG_TF_RASTER_CPU");
        if (e == NULL || e[0] == '\0') {
            g_rcpu = 0;
        } else if (strcmp(e, "1") == 0) {
            g_rcpu = 1;
        } else if (strcmp(e, "0") == 0) {
            g_rcpu = 0;
        } else {
            g_rcpu = 0;
            fprintf(stderr, "#95 SL_BG_TF_RASTER_CPU=\"%s\" is not 0 or 1 - the "
                            "original-raster control stays off\n", e);
        }
    }
    return g_rcpu;
}

/* THE STATE-ONLY CONTROL, parsed once for the launch and COUPLED.
 *
 * WHAT IT CHANGES, AS THE ONE CALL IT DROPS. The prepass bracket is kept whole -
 * program, uniforms, pool and its verified storage, input position pointer,
 * dropped element binding, rasterizer discard, the indexed transform-feedback
 * binding and the begin/end scope - and restored exactly as it is without this
 * control. Only glDrawArrays is omitted. See the header for the measurement that
 * motivates the split: with the original positions already rasterising, the
 * prepass's EXECUTION is all that is left, and this separates the vertex draw
 * from the state and program transitions around it.
 *
 * THE COUPLING IS THE ANSWER, NOT A WARNING BESIDE IT. The control is meaningful
 * only with SL_BG_TF_CLIP=1 - nothing else admits a walk - and only with
 * SL_BG_TF_RASTER_CPU=1, because with the prepass's draw omitted there is no
 * output for a raster to read and binding the pool is exactly what must not
 * happen. So the two are required terms of this function's own answer: set
 * without either, it answers 0 and the launch behaves as it does without this
 * variable at all. Reporting a miscombination while honouring it would have
 * handed a selected arm an unwritten pool.
 *
 * REPORTED ONCE PER LAUNCH, for the same reason the other two are: a per-draw
 * report would print once a mesh, and the mistake is in the launch. Only the
 * exact text "1" selects it; anything else is off.
 *
 * AND IT IS DECIDED TOGETHER WITH THE POSITION-ONLY-INPUT CONTROL, below: the
 * two are mutually exclusive, so one arbiter answers for both and they cannot
 * disagree about which arm a launch selected. */

/* THE EMPTY-DRAW STATE SELECTOR'S GRAMMAR, read once and failing CLOSED.
 *
 * Returns the mode, or -1 for a value that was offered and cannot be read.
 * Absent, empty and "0" are SL_BGTF_ES_OFF and silent: an off launch asked for
 * nothing and nothing is reported.
 *
 * ONE EXACT TOKEN PER ROW, AND NOTHING IS NORMALISED. "a" is not "A" and "A "
 * is not "A". Accepting a near miss means choosing a row on the strength of a
 * guess about what the launch meant, and a six-arm comparison in which one arm
 * may silently be another arm is not a comparison. The caller reports the
 * refusal by name and leaves the mode off. */
static int tf_es_parse(const char *e)
{
    if (e == NULL || e[0] == '\0') return SL_BGTF_ES_OFF;
    if (e[1] != '\0') {
        if (strcmp(e, "0") == 0) return SL_BGTF_ES_OFF;   /* unreachable: len 1 */
        return -1;                                         /* two or more bytes */
    }
    switch (e[0]) {
    case '0': return SL_BGTF_ES_OFF;
    case 'A': return SL_BGTF_ES_A;
    case 'B': return SL_BGTF_ES_B;
    case 'C': return SL_BGTF_ES_C;
    case 'E': return SL_BGTF_ES_E;
    case 'R': return SL_BGTF_ES_R;
    case 'S': return SL_BGTF_ES_S;
    case 'Q': return SL_BGTF_ES_Q;
    default:  return -1;
    }
}

/* THE POST-RESET BOUNDARY SELECTOR'S GRAMMAR, read once and failing CLOSED.
 *
 * Returns the arm, or -1 for a value that was offered and cannot be read.
 * Absent, empty, "0" and exactly "OFF" are SL_BGTF_PR_OFF and silent: an off
 * launch asked for nothing and nothing is reported. "OFF" is admitted as a token
 * because the header names it as one of the four values; it is spelled exactly
 * and is the same string the table carries, so an off launch and an off record
 * agree.
 *
 * ONE EXACT TOKEN PER ARM, COMPARED WHOLE, AND NOTHING IS NORMALISED. The
 * comparison is against the TABLE'S OWN names, so the grammar cannot admit a
 * spelling the record does not use or refuse one it does. No case folding, no
 * trimming, no prefix or abbreviation: "both" is not "BOTH", "BOTH " is not
 * "BOTH", and "POST" is not an arm. Accepting a near miss means choosing an arm
 * on the strength of a guess about what the launch meant, and a comparison in
 * which one arm may silently be another arm is not a comparison. The caller
 * reports the refusal by name, quoting what was offered, and leaves the control
 * off. */
static int tf_pr_parse(const char *e)
{
    int i;
    if (e == NULL || e[0] == '\0') return SL_BGTF_PR_OFF;
    if (strcmp(e, "0") == 0) return SL_BGTF_PR_OFF;
    /* Arm 0's own name is "OFF", so the loop covers the explicit off token and
     * the three selectable arms from the one table. */
    for (i = 0; i < TF_PR_ARMS; i++)
        if (strcmp(e, g_pr_arms[i].name) == 0) return i;
    return -1;
}

/* One strict 0/1 switch, with an unrecognised value reported once by name. */
/* THE REAL-PREPASS MODE'S SELECTOR, READ EXACTLY AS THE TWO ABOVE ARE: an arm,
 * SL_BGTF_AR_OFF for every silent off token (absent, empty, "0", "OFF"), or -1
 * for a value that was OFFERED and cannot be read. Nothing is case-folded,
 * trimmed or abbreviated, so "cpu_source", "CPU", "CPU_SOURCE " and
 * "GPU_SOURCEX" are all unreadable values rather than near misses. */
static int tf_ar_parse(const char *e)
{
    if (e == NULL || e[0] == '\0') return SL_BGTF_AR_OFF;
    if (strcmp(e, "0") == 0 || strcmp(e, "OFF") == 0) return SL_BGTF_AR_OFF;
    if (strcmp(e, "CPU_SOURCE") == 0) return SL_BGTF_AR_CPU;
    if (strcmp(e, "GPU_SOURCE") == 0) return SL_BGTF_AR_GPU;
    /* KEYED BY THE SELECTOR AND NOT BY WHAT IT WAS GIVEN. The line used to quote
     * the value, which put an environment value in this module's output; the
     * accepted tokens are what a reader needs in order to fix the launch, and
     * the key names which launch argument to look at. The same rule is applied
     * to the synchronisation selector's own refusal. */
    fprintf(stderr, "#95 SL_BG_TF_AUX_REAL was given a value that is not one of "
                    "CPU_SOURCE, GPU_SOURCE, OFF or 0, so it asks for nothing "
                    "and the auxiliary-context real-prepass mode stays OFF. NO "
                    "AUXILIARY GL CONTEXT IS CREATED\n");
    return -1;
}

const char *sl_bgtf_ar_name(int mode)
{
    switch (mode) {
    case SL_BGTF_AR_OFF: return "OFF";
    case SL_BGTF_AR_CPU: return "CPU_SOURCE";
    case SL_BGTF_AR_GPU: return "GPU_SOURCE";
    default:             return "?";
    }
}

static int tf_switch_1(const char *var, const char *what)
{
    const char *e = getenv(var);
    if (e == NULL || e[0] == '\0') return 0;
    if (strcmp(e, "1") == 0) return 1;
    if (strcmp(e, "0") == 0) return 0;
    fprintf(stderr, "#95 %s=\"%s\" is not 0 or 1 - %s stays off\n", var, e, what);
    return 0;
}

/* THE OPT-IN CANDIDATE STANDING DOWN FOR A DIAGNOSTIC, IN ONE LINE AND ONE PLACE
 * (SL_BG_TF_CLIP=1 with SL_BG_TF_AUX_REAL NOT REQUESTED).
 *
 * WHY IT IS SAID AT ALL. A launch that opted into the clip prepass and selected
 * one of the diagnostic controls keeps the MAIN-CONTEXT prepass, which is the
 * arm that diagnostic is about - so this is not a refusal and nothing is being
 * taken away. But the main-context prepass is also the arm measured one blue
 * value apart from the original renderer on the scene that carries the defect,
 * and a reader of such a launch's output must never have to infer which of the
 * two prepasses produced the frames in front of them. The alternative to this
 * line is a silent fall to the known-mismatching path, which is the one outcome
 * this selection exists to remove.
 *
 * NO VALUE OF ANY KEY IS ECHOED HERE. The controls that stood it down each print
 * their own line when they are refused or coupled; this one states which prepass
 * the launch is, which is a property of the launch and not of its environment.
 *
 * ONCE PER LAUNCH, like every other statement the arbiter makes: its body runs
 * once and both call sites are inside it. */
static void tf_ar_optin_standdown(void)
{
    fprintf(stderr, "#95 the background GPU clip prepass's OPT-IN CANDIDATE "
                    "STANDS DOWN: no arm was named, but this launch selected a "
                    "DIAGNOSTIC of the MAIN-CONTEXT prepass, so that is the "
                    "prepass it runs and the auxiliary-context arm is not "
                    "selected. NO AUXILIARY GL CONTEXT IS CREATED. The "
                    "main-context prepass is the arm whose frames were measured "
                    "ONE BLUE VALUE APART from the original renderer's on the "
                    "scene that carries the defect, so a frame from this launch "
                    "is DIAGNOSTIC EVIDENCE and not the candidate's output\n");
}

/* v0.4 THE UNREADABLE SYNCHRONISATION MODE FAILING THE PREPASS CLOSED, IN ONE
 * LINE AND ONE PLACE (SL_BG_TF_AUX_SYNC on a launch with an arm in force).
 *
 * WHY IT IS A FUNCTION NOW. Two selections can reach an arm - the developer
 * switch's opt-in candidate, decided by the arbiter, and the row's PRODUCTION
 * selection, promoted at the first walk the row admitted - and an unreadable
 * synchronisation value has to refuse BOTH, with the same latch and the same
 * words. A second copy of the sentence would be a second thing to drift, and a
 * reader matching on the frozen wording would see one of the two launches say
 * something slightly different about the identical outcome.
 *
 * IT SAYS THE SAME THING TO BOTH BECAUSE THE OUTCOME IS THE SAME. The arm the
 * launch would have run is refused, every served draw goes to the existing CPU
 * raster path, no auxiliary context and no sync object is created, and it does
 * NOT fall to the main-context prepass - the arm measured one blue value apart
 * from the original renderer's frames on the scene that carries the defect. The
 * line is keyed by the selector and quotes no value of it. */
static void tf_ar_sync_standdown(void)
{
    g_ar_closed = 1;
    fprintf(stderr, "#95 SL_BG_TF_AUX_SYNC was given a value that is "
                    "neither FENCE nor FINISH, so it selects NEITHER "
                    "synchronisation mode and the auxiliary-context "
                    "real-prepass arm this launch would have run is "
                    "REFUSED. The background GPU clip prepass FAILS CLOSED: "
                    "every served draw is refused to the EXISTING CPU raster "
                    "path, NO AUXILIARY GL CONTEXT IS CREATED, no sync "
                    "object is created and no prepass is issued on either "
                    "context. It does NOT fall to the MAIN-CONTEXT prepass, "
                    "whose frames were measured ONE BLUE VALUE APART from "
                    "the original renderer's on the scene that carries the "
                    "defect - a known mismatch is not something a mistyped "
                    "selector may select. Spell FENCE or FINISH, or name "
                    "neither\n");
}

/* THE SYNCHRONISATION SELECTOR'S GRAMMAR, READ EXACTLY AS THE ARM'S IS
 * (SL_BG_TF_AUX_SYNC): SL_BGTF_FX_FINISH or SL_BGTF_FX_FENCE for the two exact
 * tokens, -1 for a value that was OFFERED and cannot be read, and -2 for a
 * launch that named nothing, which is the state the arm's own default answers.
 *
 * NOTHING IS CASE-FOLDED, TRIMMED OR ABBREVIATED, exactly as tf_ar_parse does
 * nothing of the kind: "fence", "Fence" and "FENCE " are unreadable values and
 * not near misses, so a request either names the token or does not ask. Pure: it
 * prints nothing and decides nothing, so the one statement about an unreadable
 * value is made where the consequence is. */
#define TF_FX_UNSET (-2)

static int tf_fx_parse(const char *e)
{
    if (e == NULL || e[0] == '\0') return TF_FX_UNSET;
    if (strcmp(e, "FINISH") == 0) return SL_BGTF_FX_FINISH;
    if (strcmp(e, "FENCE") == 0) return SL_BGTF_FX_FENCE;
    return -1;
}

/* THE SYNCHRONISATION MODE, DECIDED ONCE AND WITH THE ARM (SL_BG_TF_AUX_SYNC).
 *
 * WHY IT IS DECIDED HERE AND NOT AT THE FIRST SERVED DRAW. Two properties need
 * it, and neither is obtainable from a lazy read:
 *
 *   - THE EFFECTIVE MODE HAS TO BE SAYABLE WHERE THE ARM IS ANNOUNCED. A line
 *     that names the arm and then names a synchronisation mode the launch may
 *     have overridden is false for the launch that overrode it, and a false
 *     startup line is not repaired by a later census being right.
 *   - AN UNREADABLE VALUE HAS TO REFUSE BEFORE ANY GL STATE EXISTS. The refusal
 *     is the arm standing down, which is a decision about the launch; taken
 *     inside a bracket it would be taken after that bracket had already written
 *     state on one context or the other.
 *
 * AND THE OFF LAUNCH STILL READS NOTHING. The one call site is behind an arm
 * being in force, so a launch with no arm performs no getenv for this key,
 * prints nothing about it and cannot have its call sequence changed by it -
 * which is the property the default path's byte-for-byte invariance rests on.
 *
 * AN UNREADABLE VALUE REFUSES THE ARM RATHER THAN CHOOSING ONE FOR IT. Both
 * earlier behaviours were wrong for the same reason, measured rather than
 * argued: serving such a launch with FENCE ran a mode the launch had not
 * selected, and serving it with FINISH ran the other one; and on an opted-in
 * launch the arm standing down without a refusal would have handed the walk to
 * the MAIN-CONTEXT prepass, whose frames were measured ONE BLUE VALUE APART from
 * the original renderer's on the scene that carries the defect. So the caller
 * raises the fail-closed latch and every served draw goes to the existing CPU
 * raster path.
 *
 * Answers the mode in force, or -1 for an unreadable value. `named` is written
 * with whether the KEY chose the mode, which is what separates a request from
 * the candidate's default in both the line below and the fence census. */
static int tf_fx_decide(int implicit, int *named)
{
    const int raw = tf_fx_parse(getenv("SL_BG_TF_AUX_SYNC"));

    *named = (raw == SL_BGTF_FX_FINISH || raw == SL_BGTF_FX_FENCE);
    if (raw == -1) {
        /* THE MODE IS LEFT AT THE ORIGINAL, AND NOTHING WILL ISSUE IT. The arm
         * is about to stand down, so no bracket runs under either mode; this
         * keeps every later answer the one an off launch gives rather than
         * leaving a half-selected fence path readable from an accessor. */
        g_fx = SL_BGTF_FX_FINISH;
        g_fc.fx_mode = (unsigned long) SL_BGTF_FX_FINISH;
        return -1;
    }
    /* THE DEFAULT BELONGS TO THE ARM THAT IS IN FORCE, and the two arms have
     * different ones because they were established by different evidence.
     *
     * A NAMED ARM KEEPS FINISH, byte for byte: every earlier round's frozen
     * output describes a named arm whose unqualified default is two glFinish
     * calls per bracket, and an A/B comparison whose control silently changed
     * synchronisation would not be one.
     *
     * THE OPT-IN CANDIDATE DEFAULTS TO FENCE, because FENCE is the half of the
     * measured combination it was measured as: the auxiliary GPU_SOURCE arm with
     * FENCE produced frames byte-identical to the original renderer's on the
     * scene that carries the defect, and FENCE is also the half that does not
     * spend two pipeline drains per served mesh - which a candidate anyone is
     * meant to be able to turn on cannot afford. */
    if (raw == TF_FX_UNSET)
        g_fx = implicit ? SL_BGTF_FX_FENCE : SL_BGTF_FX_FINISH;
    else
        g_fx = raw;
    /* THE KEY'S OWN REQUEST, RECORDED SEPARATELY FROM THE MODE. fx_asked is 1
     * only when the KEY selected FENCE, so the candidate's default reads FENCE
     * beside fx_asked 0 and the two are distinguishable in a census. */
    if (g_fx == SL_BGTF_FX_FENCE && *named) g_fc.fx_asked = 1ul;
    g_fc.fx_mode = (unsigned long) g_fx;
    return g_fx;
}

/* THE TWO EXCLUSIVE DIAGNOSTIC ARMS, DECIDED ONCE AND TOGETHER.
 *
 * WHY ONE ARBITER RATHER THAN TWO PARSERS. The state-only control omits the
 * prepass draw; the position-only-input control changes what that draw reads. A
 * launch asking for both is asking for two different experiments of one walk,
 * and whichever answered first would silently win while the other reported
 * something else. Deciding both here means the refusal is a single fact and the
 * two answers are derived from it.
 *
 * AMBIGUITY IS REFUSED, NOT RANKED. Both asked for: both answer 0, reported
 * once, and the launch is whatever SL_BG_TF_CLIP and SL_BG_TF_RASTER_CPU
 * selected on their own. Neither is given precedence, because a precedence rule
 * would run one experiment under the other's name.
 *
 * THE COUPLING IS PART OF EACH ANSWER. Both need SL_BG_TF_CLIP=1 - nothing else
 * admits a walk - and both need SL_BG_TF_RASTER_CPU=1, the state-only arm
 * because an omitted draw leaves no output for a raster to read, and this arm
 * because the comparison it belongs to rasterises the original stream. Set
 * without either, the control answers 0 and changes nothing whatever. */
static void tf_decide_controls(void)
{
    int so, pi, pv, zc, zc_asked;
    /* THE EMPTY-DRAW STATE MODE IS DECIDED HERE TOO, for the reason every other
     * control is: it is coupled to the zero-count arm and exclusive with the
     * three older ones, so one arbiter answers for every arm and no two can
     * disagree about which one a launch selected.
     *
     * `es_raw` is the parse of what the launch asked for: a mode, OFF, or -1 for
     * a value that was offered and cannot be read. `es` is what is in force
     * after the coupling and exclusivity questions. They differ exactly on a
     * refused launch, which is what makes a refusal countable without consulting
     * the environment a second time. */
    int es, es_raw;
    const char *es_text;
    /* THE POST-RESET BOUNDARY CONTROL IS DECIDED HERE TOO, for the reason every
     * other control is: it is exclusive with the four older ones, so one arbiter
     * answers for every arm and no two can disagree about which experiment a
     * launch selected.
     *
     * `pr_raw` is the parse of what the launch asked for: an arm, OFF, or -1 for
     * a value that was offered and cannot be read. `pr` is what is in force
     * after the coupling and exclusivity questions. They differ exactly on a
     * refused launch, which is what makes a refusal countable without consulting
     * the environment a second time.
     *
     * IT IS NOT COUPLED TO THE ZERO-COUNT ARM OR TO ANY EMPTY-DRAW ROW. The
     * boundary sits outside the empty draw's bracket and exists on every arm
     * that issues a prepass draw, the REAL nonzero transform path included, so
     * coupling it to either would confine it to the one arm a shipping question
     * is not about. `es` and `zc` are therefore read nowhere in its clauses. */
    int pr, pr_raw;
    const char *pr_text;
    /* THE NEUTRAL-PROGRAM CONTROL IS DECIDED HERE TOO, for the reason every other
     * control is: it is coupled to the zero-count arm and exclusive with every
     * other diagnostic that touches the same empty draw, so one arbiter answers
     * for every arm and no two can disagree about which experiment a launch
     * selected.
     *
     * `np_asked` is what the launch requested and `np` is what is in force after
     * the coupling and exclusivity questions. They differ exactly on a refused
     * launch, which is what makes a refusal countable without consulting the
     * environment a second time. An absent switch consults nothing else: every
     * clause below is reached only on a launch that actually selected the
     * control, so an off launch's reported sequence is the one it has without
     * this control existing. */
    int np, np_asked;
    /* THE AUXILIARY-CONTEXT CONTROL IS DECIDED HERE TOO, for the reason every
     * other control is: it is coupled to the zero-count arm and exclusive with
     * every other diagnostic that touches the same empty draw, so one arbiter
     * answers for every arm and no two can disagree about which experiment a
     * launch selected.
     *
     * `ax_asked` is what the launch requested and `ax` is what is in force after
     * the coupling and exclusivity questions. They differ exactly on a refused
     * launch, which is what makes a refusal countable without consulting the
     * environment a second time. An absent switch consults nothing else: every
     * clause below is reached only on a launch that actually selected the
     * control, so an off launch's reported sequence is the one it has without
     * this control existing. */
    int ax, ax_asked;
    /* THE REAL-PREPASS MODE IS DECIDED HERE TOO, for the reason every other
     * control is: it is exclusive with every one of them, so one arbiter answers
     * for every arm and no two can disagree about which experiment a launch
     * selected.
     *
     * `ar_raw` is the parse of what the launch asked for: an arm, OFF, or -1 for
     * a value that was offered and cannot be read. `ar` is what is in force
     * after the coupling and exclusivity questions. They differ exactly on a
     * refused launch, which is what makes a refusal countable without consulting
     * the environment a second time. */
    int ar, ar_raw;
    const char *ar_text;
    /* WHETHER THE LAUNCH NAMED AN ARM AT ALL, which is a THIRD state beside the
     * two above and the one the opt-in candidate turns on.
     *
     * `ar_absent` is 1 for an unset or empty value and 0 for every value that
     * was offered, the explicit off tokens "0" and "OFF" included. The parser
     * answers SL_BGTF_AR_OFF for all four, so `ar_raw` alone cannot tell "did
     * not ask" from "asked for off" - and those two must not be answered the
     * same way: an explicit off token is a developer selecting the main-context
     * DIAGNOSTIC arm and keeps it, while saying nothing is a launch that asked
     * for the clip prepass and gets the arm that was measured.
     *
     * AN EMPTY VALUE COUNTS AS UNSET, which is the rule every other selector in
     * this file already follows: absent and empty are one silent off state
     * throughout, and a shell that exports a key with no value has not named a
     * token. */
    int ar_absent;
    /* THE SYNCHRONISATION MODE IS DECIDED HERE TOO, with the arm rather than at
     * the first served draw: the arm's announcement has to be able to NAME the
     * mode in force, and an unreadable value has to refuse before any bracket
     * has written state on either context. See tf_fx_decide.
     *
     * `fx_named` is whether the KEY chose the mode rather than the arm's own
     * default choosing it. It is what makes the line below true for an explicit
     * FINISH, an explicit FENCE and the candidate's default alike. */
    int fx_named = 0;
    /* THE RAW REQUESTS, LATCHED BEFORE ANY BRANCH BELOW CAN MUTATE THEM.
     *
     * WHY A SECOND SET OF VARIABLES RATHER THAN READING so/pi/pv AGAIN. The
     * branches below resolve the OLDER controls' conflicts by clearing their
     * effective flags - the state-only/position-only pair clears both, and the
     * private control's own pair clauses clear it and its partner. By the time
     * this control's exclusivity questions are asked, an effective flag of 0 no
     * longer distinguishes "was not requested" from "was requested and then
     * resolved away".
     *
     * AND THAT DIFFERENCE WAS A DEFECT, measured rather than argued. With
     * SL_BG_TF_CLIP=1 and SL_BG_TF_RASTER_CPU=1, a launch requesting
     * SL_BG_TF_ZERO_COUNT=1 together with SL_BG_TF_PRIVATE_VAO=1 and
     * SL_BG_TF_STATE_ONLY=1 had the private/state-only clause fire first and
     * clear BOTH of those, after which this control's `zc && so` and `zc && pv`
     * questions were both false and it was SILENTLY SELECTED with zc_refusals 0.
     * The same held for POS_INPUT in place of STATE_ONLY. A launch that asked
     * for three different experiments at once must not be handed one of them.
     *
     * SO THIS CONTROL'S EXCLUSIONS ARE DECIDED FROM WHAT THE LAUNCH REQUESTED,
     * never from what survived. NOTHING ELSE CHANGES: the older controls'
     * clauses, their order, their messages and their own resolution are
     * untouched, and no precedence between them is invented here - the three
     * raw values are read and not written. */
    int raw_so, raw_pi, raw_pv;

    if (g_sonly >= 0) return;          /* all latch in one step, or none      */

    so = tf_switch_1("SL_BG_TF_STATE_ONLY", "the state-only control");
    pi = tf_switch_1("SL_BG_TF_POS_INPUT", "the position-only-input control");
    /* THE PRIVATE-VERTEX-ARRAY CONTROL IS DECIDED HERE TOO, for the reason the
     * other two are: it is exclusive with both of them, so one arbiter answers
     * for all three and they cannot disagree about which arm a launch selected.
     *
     * AN ABSENT SWITCH CONSULTS NOTHING ELSE. This is the only variable read for
     * it when it is absent: the exclusivity questions below - including the one
     * about the buffer diagnostic, whose own switch has a grammar and a spent
     * attempt of its own - are asked only on a launch that actually selected it,
     * so an off launch's reported sequence is the one it has without this
     * control existing. */
    pv = tf_switch_1("SL_BG_TF_PRIVATE_VAO",
                     "the private prepass vertex-array control");
    /* THE ZERO-VERTEX-COUNT CONTROL IS DECIDED HERE TOO, for the reason the
     * other three are: it is exclusive with all of them, so one arbiter answers
     * for every arm and they cannot disagree about which one a launch selected.
     * An absent switch consults nothing else - every exclusivity and coupling
     * question below is asked only on a launch that actually selected it, so an
     * off launch's reported sequence is the one it has without this control
     * existing. `zc_asked` is kept because a launch that asked for the control
     * and did not get it is a REFUSAL and is counted as one; an absent, 0 or
     * unrecognised value never asked, so it is not a refusal of anything. */
    zc = tf_switch_1("SL_BG_TF_ZERO_COUNT",
                     "the zero-vertex-count prepass draw control");
    zc_asked = zc;
    /* READ HERE, WITH EVERY OTHER SWITCH, AND RESOLVED AFTER THEM ALL. The one
     * getenv is the whole cost on an off launch: the mode's own clauses are
     * reached only when a mode was actually asked for, so an absent selector
     * adds no further read, no GL call and no marker and the zero-count arm's
     * sequence is the one it has without this selector existing. */
    es_text = getenv("SL_BG_TF_EMPTY_STATE");
    es_raw = tf_es_parse(es_text);
    es = es_raw > 0 ? es_raw : SL_BGTF_ES_OFF;
    /* READ HERE, WITH EVERY OTHER SWITCH, AND RESOLVED AFTER THEM ALL, for the
     * reason the selector above is: the one getenv is the whole cost on an off
     * launch, because this control's clauses are reached only when an arm was
     * actually asked for. An absent selector adds no further read, no GL call
     * and no marker, and every arm's sequence - the real nonzero path's
     * included - is the one it has without this selector existing. */
    pr_text = getenv("SL_BG_TF_POST_RESET");
    pr_raw = tf_pr_parse(pr_text);
    pr = pr_raw > 0 ? pr_raw : SL_BGTF_PR_OFF;
    /* READ HERE, WITH EVERY OTHER SWITCH, AND RESOLVED AFTER THEM ALL, for the
     * reason the two selectors above are: the one getenv is the whole cost on an
     * off launch, because this control's clauses are reached only when it was
     * actually asked for. An unrecognised value is reported by the shared switch
     * reader and never asked for anything, so it is not a refusal of this
     * control. */
    np = tf_switch_1("SL_BG_TF_NEUTRAL_PROG",
                     "the neutral-program empty-draw control");
    np_asked = np;
    /* READ HERE, WITH EVERY OTHER SWITCH, AND RESOLVED AFTER THEM ALL, for the
     * reason the selectors above are: the one getenv is the whole cost on an off
     * launch, because this control's clauses are reached only when it was
     * actually asked for. An unrecognised value is reported by the shared switch
     * reader and never asked for anything, so it is not a refusal of this
     * control. */
    ax = tf_switch_1("SL_BG_TF_AUX_EMPTY",
                     "the auxiliary-context empty-draw control");
    ax_asked = ax;
    /* READ HERE, WITH EVERY OTHER SELECTOR, AND RESOLVED AFTER THEM ALL, for the
     * reason the selectors above are: the one getenv is the whole cost on an off
     * launch, because this mode's clauses are reached only when an arm was
     * actually asked for. An absent selector adds no further read, no GL call,
     * no GL context and no marker, and every arm's sequence - the real nonzero
     * path's included - is the one it has without this selector existing. */
    ar_text = getenv("SL_BG_TF_AUX_REAL");
    ar_raw = tf_ar_parse(ar_text);
    ar = ar_raw > 0 ? ar_raw : SL_BGTF_AR_OFF;
    ar_absent = (ar_text == NULL || ar_text[0] == '\0');
    /* AND AN UNREADABLE ARM ON AN OPTED-IN LAUNCH FAILS THE WALK CLOSED, decided
     * here with the parse and acted on at the one site that would otherwise
     * reach the main-context prepass.
     *
     * WHAT IT PREVENTS. The parser reports an unreadable value and leaves the
     * mode OFF, which is correct for the mode; but on a launch that has opted
     * into the clip prepass, "the mode is off" is not "nothing happens" - it is
     * the MAIN-CONTEXT prepass, the arm measured at blue 142 against the
     * original renderer's 143. A typed selector must not be the thing that
     * selects a known mismatch. Every served draw is refused to the existing CPU
     * raster path instead, with the parser's own line as the reason.
     *
     * ONLY ON AN OPTED-IN LAUNCH, so a launch without the opt-in switch reaches
     * no prepass of either kind and is not touched by this latch. The opt-in
     * answer is already cached by the time the clauses below ask for it, so this
     * adds no environment read on any path. */
    if (ar_raw < 0 && sl_bgtf_opt_in() == 1) {
        g_ar_closed = 1;
        fprintf(stderr, "#95 that unreadable arm was offered by a launch that "
                        "OPTED INTO the background GPU clip prepass, so the "
                        "prepass FAILS CLOSED: every served draw is refused to "
                        "the EXISTING CPU raster path and NO prepass is issued "
                        "on either context. The alternative would have been the "
                        "MAIN-CONTEXT prepass, whose frames were measured ONE "
                        "BLUE VALUE APART from the original renderer's on the "
                        "scene that carries the defect - a known mismatch is not "
                        "something a mistyped selector may select. Spell an arm "
                        "token or spell an OFF token\n");
    }
    /* HERE, AND BEFORE THE FIRST BRANCH. Every switch has been read and nothing
     * has been resolved yet, so these three are exactly what the launch asked
     * for. They are read below and never written. */
    raw_so = so;
    raw_pi = pi;
    raw_pv = pv;

    if (so && pi) {
        fprintf(stderr, "#95 SL_BG_TF_STATE_ONLY=1 and SL_BG_TF_POS_INPUT=1 are "
                        "MUTUALLY EXCLUSIVE - one omits the prepass draw and the "
                        "other changes what that draw reads, so a launch asking "
                        "for both is asking for two experiments at once. BOTH stay "
                        "off and this launch is whichever arm SL_BG_TF_CLIP and "
                        "SL_BG_TF_RASTER_CPU selected\n");
        g_sonly = 0;
        g_pin = 0;
        g_pvao = 0;
        /* AND SO DOES THIS ONE, on the one path that returns early. A launch
         * that already asked for two different experiments gets none of them,
         * and a zero-count request inside it is refused with the rest rather
         * than surviving into an arm nobody selected.
         *
         * IT IS SAID BY NAME AND NOT ONLY COUNTED. The line above names the
         * other two controls, so a reader of this launch's stream would see no
         * statement about THIS one while its answer had silently become 0 -
         * which is the difference between refusing a control and dropping it. */
        if (zc_asked) {
            g_c.zc_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_ZERO_COUNT=1 was asked for in the same "
                            "launch as that ambiguous pair, so it is REFUSED with "
                            "them: a launch that cannot say which experiment it is "
                            "running does not get a third one either. The "
                            "zero-vertex-count control stays off\n");
        }
        g_zc = 0;
        /* AND SO DOES THE EMPTY-DRAW STATE MODE, on this same early path and for
         * the same reason. It is a refinement of the zero-count arm, which has
         * just been refused, so there is no arm for it to refine; a mode left
         * standing here would name a condition at a draw this launch never
         * reaches. It is said by name as well as counted. */
        if (es_raw != SL_BGTF_ES_OFF) {
            g_c.es_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_EMPTY_STATE was set in the same launch "
                            "as that ambiguous pair, so it is REFUSED with them: "
                            "the mode is a refinement of the zero-count arm and "
                            "that arm is refused here, so there is no empty draw "
                            "for a condition to hold at. The empty-draw state "
                            "mode stays OFF\n");
        }
        g_es = SL_BGTF_ES_OFF;
        /* AND SO DOES THE POST-RESET BOUNDARY CONTROL, on this same early path.
         * It is exclusive with each of the two controls named above
         * individually, and this launch requested both, so its own exclusivity
         * rule already refuses it here; the clause below would say the same
         * thing, and this path never reaches it. It is said by name as well as
         * counted, because a reader of this launch's stream must not see two
         * controls refused while a third quietly became 0. */
        if (pr_raw != SL_BGTF_PR_OFF) {
            g_c.pr_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_POST_RESET=%s was asked for in the "
                            "same launch as that ambiguous pair, so it is "
                            "REFUSED with them: this control is exclusive with "
                            "EACH of those two, and a launch that cannot say "
                            "which experiment it is running does not get "
                            "another one. The post-reset boundary control stays "
                            "OFF\n", sl_bgtf_pr_name(pr_raw));
        }
        g_pr = SL_BGTF_PR_OFF;
        /* AND SO DOES THE NEUTRAL-PROGRAM CONTROL, on this same early path and
         * for the same reason the two selectors above do. It is a refinement of
         * the zero-count arm, which has just been refused, so there is no empty
         * draw for a second program to be bound at; and it is exclusive with
         * each of the two controls named above individually, so its own rule
         * already refuses it here. It is said by name as well as counted,
         * because a reader of this launch's stream must not see three controls
         * refused while a fourth quietly became 0. */
        if (np_asked) {
            g_c.np_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1 was asked for in the "
                            "same launch as that ambiguous pair, so it is "
                            "REFUSED with them: this control is exclusive with "
                            "EACH of those two and needs the zero-count arm "
                            "that is refused here, so there is no empty draw "
                            "for a second program to be bound at. The "
                            "neutral-program empty-draw control stays off\n");
        }
        g_np = 0;
        /* AND SO DOES THE AUXILIARY-CONTEXT CONTROL, on this same early path and
         * for the reason the one above does. It is a refinement of the
         * zero-count arm, which has just been refused, so there is no empty draw
         * for a second context to issue; and it is exclusive with each of the
         * two controls named above individually, so its own rule already refuses
         * it here. It is said by name as well as counted, because a reader of
         * this launch's stream must not see four controls refused while a fifth
         * quietly became 0 - and silence about THIS one would be the worst of
         * the five to leave, a control that creates a GL context being one a
         * reader needs to know did not run. */
        if (ax_asked) {
            g_c.ax_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 was asked for in the "
                            "same launch as that ambiguous pair, so it is "
                            "REFUSED with them: this control is exclusive with "
                            "EACH of those two and needs the zero-count arm "
                            "that is refused here, so there is no empty draw "
                            "for a second context to issue. NO AUXILIARY GL "
                            "CONTEXT IS CREATED and the auxiliary-context "
                            "empty-draw control stays off\n");
        }
        g_ax = 0;
        /* AND SO DOES THE REAL-PREPASS MODE, on this same early path and for the
         * reason the five above do: it is exclusive with EACH of the two
         * controls this launch requested, so its own rule already refuses it
         * here. It is said by name as well as counted, because a reader of this
         * launch's stream must not see five controls refused while a sixth
         * quietly became 0 - and this is the second of the two that create a GL
         * context, which is the fact a reader most needs stated. */
        if (ar_raw != SL_BGTF_AR_OFF) {
            g_c.ar_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s was asked for in the same "
                            "launch as that ambiguous pair, so it is REFUSED "
                            "with them: this mode is exclusive with EACH of "
                            "those two, and a launch that cannot say which "
                            "experiment it is running does not get another one. "
                            "NO AUXILIARY GL CONTEXT IS CREATED and the "
                            "auxiliary-context real-prepass mode stays OFF\n",
                    sl_bgtf_ar_name(ar_raw));
        }
        g_ar = SL_BGTF_AR_OFF;
        /* AND THE OPT-IN CANDIDATE STANDS DOWN ON THIS EARLY PATH TOO, for the
         * reason it stands down for any other diagnostic: this launch named two
         * mutually exclusive controls of the MAIN-CONTEXT prepass, so that is
         * the prepass it runs, and a reader must be told which one that is. The
         * clause above speaks for a launch that NAMED an arm; this speaks for a
         * launch that named none and opted in, which is the newer state and the
         * one a silent fall would leave unstated. */
        if (ar_absent && sl_bgtf_opt_in() == 1) tf_ar_optin_standdown();
        return;
    }

    if (so && (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1)) {
        fprintf(stderr, "#95 SL_BG_TF_STATE_ONLY=1 needs BOTH SL_BG_TF_CLIP=1 "
                        "and SL_BG_TF_RASTER_CPU=1 (clip %d, original-raster %d) "
                        "- omitting the prepass draw leaves no output for a "
                        "raster to read, so the state-only control stays off and "
                        "this launch is whichever arm those two selected\n",
                sl_bgtf_opt_in(), sl_bgtf_raster_cpu());
        so = 0;
    }
    if (pi && (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1)) {
        fprintf(stderr, "#95 SL_BG_TF_POS_INPUT=1 needs BOTH SL_BG_TF_CLIP=1 and "
                        "SL_BG_TF_RASTER_CPU=1 (clip %d, original-raster %d) - "
                        "nothing else admits a walk to the prepass, and the "
                        "comparison this control belongs to rasterises the original "
                        "position stream, so the position-only-input control stays "
                        "off and this launch is whichever arm those two selected\n",
                sl_bgtf_opt_in(), sl_bgtf_raster_cpu());
        pi = 0;
    }

    /* THE PRIVATE-VERTEX-ARRAY CONTROL'S OWN TERMS, asked in the order a reader
     * needs them: what it is exclusive with, then what it requires.
     *
     * EXCLUSIVE WITH THE STATE-ONLY CONTROL, AND NEITHER WINS. That control's
     * whole content is the omission of the prepass draw; this one's is the
     * object that draw and its setup run inside. A launch asking for both is
     * asking for an isolated bracket around a draw that does not happen, which
     * is not either experiment, so BOTH stay off exactly as the pair above does.
     *
     * EXCLUSIVE WITH THE POSITION-ONLY-INPUT CONTROL, AND NEITHER WINS. That one
     * narrows the inherited client arrays the draw dereferences; this one gives
     * the draw its own array object, on which every array but position is off
     * from the start. Running both would mean two different accounts of which
     * arrays fed one draw, with the counts of each attributing the other's
     * effect to itself, so BOTH stay off.
     *
     * EXCLUSIVE WITH THE BUFFER DIAGNOSTIC, AND ONLY THIS CONTROL STANDS DOWN.
     * That diagnostic's one attempt, its grammar and its spent-attempt state are
     * its own, and turning it off from here would reach into a state machine this
     * control has no business in. The combination also cannot produce a capture:
     * the diagnostic records a draw whose vertex source IS a prepass pool, and
     * this control requires SL_BG_TF_RASTER_CPU=1, under which the pool is never
     * bound. The presence test is the diagnostic's own off test, so no selector
     * is parsed and no line of its is printed from here.
     *
     * AND IT NEEDS BOTH COUPLED SWITCHES. SL_BG_TF_CLIP=1 because nothing else
     * admits a walk to the prepass, and SL_BG_TF_RASTER_CPU=1 because the
     * comparison this control belongs to rasterises the original position
     * stream. Set without either, it answers 0 and changes nothing whatever. */
    if (pv && so) {
        fprintf(stderr, "#95 SL_BG_TF_PRIVATE_VAO=1 and SL_BG_TF_STATE_ONLY=1 are "
                        "MUTUALLY EXCLUSIVE - one omits the prepass draw and the "
                        "other is about the vertex array object that draw and its "
                        "setup run inside, so a launch asking for both is asking for "
                        "an isolated bracket around a draw that does not happen. BOTH "
                        "stay off and this launch is whichever arm SL_BG_TF_CLIP and "
                        "SL_BG_TF_RASTER_CPU selected\n");
        so = 0;
        pv = 0;
    }
    if (pv && pi) {
        fprintf(stderr, "#95 SL_BG_TF_PRIVATE_VAO=1 and SL_BG_TF_POS_INPUT=1 are "
                        "MUTUALLY EXCLUSIVE - one narrows the INHERITED client arrays "
                        "the prepass draw dereferences and the other gives that draw "
                        "its OWN array object with only the position array enabled, so "
                        "each would attribute the other's effect to itself. BOTH stay "
                        "off and this launch is whichever arm SL_BG_TF_CLIP and "
                        "SL_BG_TF_RASTER_CPU selected\n");
        pi = 0;
        pv = 0;
    }
    if (pv) {
        const char *d = getenv("SL_BG_TF_DUMP");
        if (d != NULL && d[0] != '\0' && !(d[0] == '0' && d[1] == '\0')) {
            fprintf(stderr, "#95 SL_BG_TF_PRIVATE_VAO=1 with SL_BG_TF_DUMP set is "
                            "AMBIGUOUS - the buffer diagnostic records a draw whose "
                            "vertex source IS a prepass pool, and this control requires "
                            "SL_BG_TF_RASTER_CPU=1, under which the pool is never bound. "
                            "The private vertex-array control stays off; the diagnostic "
                            "is left exactly as its own switch and one attempt decide\n");
            pv = 0;
        }
    }
    if (pv && (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1)) {
        fprintf(stderr, "#95 SL_BG_TF_PRIVATE_VAO=1 needs BOTH SL_BG_TF_CLIP=1 and "
                        "SL_BG_TF_RASTER_CPU=1 (clip %d, original-raster %d) - nothing "
                        "else admits a walk to the prepass, and the comparison this "
                        "control belongs to rasterises the original position stream, so "
                        "the private vertex-array control stays off and this launch is "
                        "whichever arm those two selected\n",
                sl_bgtf_opt_in(), sl_bgtf_raster_cpu());
        pv = 0;
    }

    /* THE ZERO-VERTEX-COUNT CONTROL'S OWN TERMS, asked in the order a reader
     * needs them: what it is exclusive with, then what it requires. Every one of
     * them REFUSES the control rather than half-applying it, and every refusal
     * is counted once at the end of this function.
     *
     * EXCLUSIVE WITH THE STATE-ONLY CONTROL, AND NEITHER WINS. That control's
     * whole content is the OMISSION of the prepass draw and this one's is that
     * the draw is MADE with no vertices, so the two are the opposite halves of
     * one question: a launch asking for both is asking for a call that both does
     * and does not happen. Ranking them would run one experiment under the
     * other's name - and the omitted-call arm is already measured (r594) - so
     * BOTH stay off exactly as the first pair above does.
     *
     * EXCLUSIVE WITH THE POSITION-ONLY-INPUT CONTROL, AND NEITHER WINS. That one
     * narrows the client arrays a draw DEREFERENCES; a draw over no vertices
     * dereferences none of them, so the two accounts of one call would each
     * attribute the other's effect to itself. BOTH stay off.
     *
     * EXCLUSIVE WITH THE PRIVATE VERTEX-ARRAY CONTROL, AND NEITHER WINS. That
     * one is about the array object the setup and its draw run inside, measured
     * with a full vertex count (r629); this one empties the count on the
     * inherited object, which is the ordinary reference's own policy. Running
     * both would make a result attributable to either change. BOTH stay off.
     *
     * EXCLUSIVE WITH THE BUFFER DIAGNOSTIC, AND ONLY THIS CONTROL STANDS DOWN,
     * for the reason the private control stands down from it: that diagnostic's
     * one attempt, its grammar and its spent-attempt state are its own, and
     * turning it off from here would reach into a state machine this control has
     * no business in. The combination also cannot produce a capture twice over -
     * the diagnostic records a draw whose vertex source IS a prepass pool, this
     * control requires SL_BG_TF_RASTER_CPU=1 under which the pool is never
     * bound, and a zero-count call writes no pool to record. The presence test
     * is the diagnostic's own off test, so no selector of its is parsed and no
     * line of its is printed from here.
     *
     * AND IT NEEDS BOTH COUPLED SWITCHES. SL_BG_TF_CLIP=1 because nothing else
     * admits a walk to the prepass, and SL_BG_TF_RASTER_CPU=1 because an empty
     * call writes no output for a raster to read and binding the pool is exactly
     * what must not happen - the same reason the state-only control is coupled.
     * Set without either, it answers 0 and changes nothing whatever.
     *
     * EVERY ONE OF THE THREE EXCLUSIONS IS ASKED OF THE RAW REQUEST, not of the
     * effective flag. The clauses above resolve the older controls by CLEARING
     * them, so a launch that requested this control alongside TWO of them would
     * otherwise reach here with both already 0 and be selected silently - see
     * the note on raw_so/raw_pi/raw_pv at the top of this function for the
     * measured case. The partner's effective flag is still cleared below, which
     * is what keeps each PAIR's existing "BOTH stay off" behaviour exactly as it
     * was; on a triple request that assignment is a no-op because the earlier
     * clause already made it. */
    if (zc && raw_so) {
        fprintf(stderr, "#95 SL_BG_TF_ZERO_COUNT=1 and SL_BG_TF_STATE_ONLY=1 are "
                        "MUTUALLY EXCLUSIVE - one OMITS the prepass draw and the "
                        "other MAKES it with the count 0, so a launch asking for "
                        "both is asking for a call that both does and does not "
                        "happen. BOTH stay off and this launch is whichever arm "
                        "SL_BG_TF_CLIP and SL_BG_TF_RASTER_CPU selected\n");
        so = 0;
        zc = 0;
    }
    if (zc && raw_pi) {
        fprintf(stderr, "#95 SL_BG_TF_ZERO_COUNT=1 and SL_BG_TF_POS_INPUT=1 are "
                        "MUTUALLY EXCLUSIVE - one narrows the client arrays the "
                        "prepass draw DEREFERENCES and the other leaves it no "
                        "vertex to dereference any array for, so each would "
                        "attribute the other's effect to itself. BOTH stay off and "
                        "this launch is whichever arm SL_BG_TF_CLIP and "
                        "SL_BG_TF_RASTER_CPU selected\n");
        pi = 0;
        zc = 0;
    }
    if (zc && raw_pv) {
        fprintf(stderr, "#95 SL_BG_TF_ZERO_COUNT=1 and SL_BG_TF_PRIVATE_VAO=1 are "
                        "MUTUALLY EXCLUSIVE - one gives the prepass setup and its "
                        "draw their own vertex array object and the other empties "
                        "that draw's count on the INHERITED object, so a result "
                        "would be attributable to either change. BOTH stay off and "
                        "this launch is whichever arm SL_BG_TF_CLIP and "
                        "SL_BG_TF_RASTER_CPU selected\n");
        pv = 0;
        zc = 0;
    }
    if (zc) {
        const char *d = getenv("SL_BG_TF_DUMP");
        if (d != NULL && d[0] != '\0' && !(d[0] == '0' && d[1] == '\0')) {
            fprintf(stderr, "#95 SL_BG_TF_ZERO_COUNT=1 with SL_BG_TF_DUMP set is "
                            "AMBIGUOUS - the buffer diagnostic records a draw whose "
                            "vertex source IS a prepass pool, this control requires "
                            "SL_BG_TF_RASTER_CPU=1 under which the pool is never "
                            "bound, and a zero-count call writes no pool to record. "
                            "The zero-vertex-count control stays off; the diagnostic "
                            "is left exactly as its own switch and one attempt "
                            "decide\n");
            zc = 0;
        }
    }
    if (zc && (sl_bgtf_opt_in() != 1 || sl_bgtf_raster_cpu() != 1)) {
        fprintf(stderr, "#95 SL_BG_TF_ZERO_COUNT=1 needs BOTH SL_BG_TF_CLIP=1 and "
                        "SL_BG_TF_RASTER_CPU=1 (clip %d, original-raster %d) - "
                        "nothing else admits a walk to the prepass, and a call that "
                        "submits no vertex writes no output for a raster to read, so "
                        "the zero-vertex-count control stays off and this launch is "
                        "whichever arm those two selected\n",
                sl_bgtf_opt_in(), sl_bgtf_raster_cpu());
        zc = 0;
    }
    /* ONE REFUSAL PER LAUNCH, AND ONLY FOR A LAUNCH THAT ASKED. Several of the
     * clauses above can speak on one launch - a request against two other arms
     * at once is two true statements - and the outcome is still the single fact
     * that this control was asked for and is not in force. */
    if (zc_asked && !zc) g_c.zc_refusals++;

    /* THE EMPTY-DRAW STATE MODE'S OWN TERMS, asked last because it refines the
     * arm the clauses above have just settled. Every one of them REFUSES the
     * mode rather than substituting a row, and every refusal is counted once at
     * the end of this function.
     *
     * AN UNREADABLE VALUE IS A REFUSAL, NOT A DEFAULT. The grammar admits four
     * tokens; anything else was offered and cannot be read, and choosing a row
     * for it would run one condition under another's name.
     *
     * IT NEEDS THE ZERO-COUNT ARM IN FORCE, not merely requested. The mode's
     * whole content is which conditions hold at the EMPTY DRAW, and that draw
     * exists only on an effective zero-count arm - so the coupling is on `zc`,
     * the answer the clauses above produced, and a launch whose zero-count
     * request was itself refused cannot carry a mode past it. SL_BG_TF_CLIP=1
     * and SL_BG_TF_RASTER_CPU=1 are terms of `zc` already and are named in the
     * message so a reader sees which one was missing rather than only that
     * something was.
     *
     * AND IT IS REFUSED BY THE RAW REQUEST FOR ANY OLDER EXCLUSIVE CONTROL, for
     * the measured reason the zero-count control's own exclusions are: the
     * clauses above resolve those controls by CLEARING them, so a launch that
     * requested two of them would reach here with both already 0. Asking the raw
     * values means a launch that asked for several experiments at once is handed
     * none of them. These clauses are reached only when a mode was requested, so
     * an off launch prints nothing of the mode's. */
    if (es_raw < 0) {
        g_c.es_refusals++;
        fprintf(stderr, "#95 SL_BG_TF_EMPTY_STATE=\"%s\" is not one of A, B, C, "
                        "E, R, S, Q or 0 - the modes are exact single tokens and "
                        "nothing is normalised or guessed at, because selecting "
                        "a neighbouring row would run one condition under "
                        "another's name. The empty-draw state mode stays OFF and "
                        "this launch is the ordinary zero-count arm if its own "
                        "switches selected one\n",
                es_text != NULL ? es_text : "");
        es = SL_BGTF_ES_OFF;
    } else if (es_raw != SL_BGTF_ES_OFF) {
        if (raw_so || raw_pi || raw_pv) {
            g_c.es_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_EMPTY_STATE=%s with "
                            "SL_BG_TF_STATE_ONLY=%d, SL_BG_TF_POS_INPUT=%d or "
                            "SL_BG_TF_PRIVATE_VAO=%d requested is AMBIGUOUS - "
                            "this mode varies the state at the zero-count arm's "
                            "empty draw, and each of those three is a different "
                            "experiment on the same draw. The empty-draw state "
                            "mode stays OFF; the requested control is left "
                            "exactly as the clauses above decided it\n",
                    sl_bgtf_es_name(es_raw), raw_so, raw_pi, raw_pv);
            es = SL_BGTF_ES_OFF;
        } else if (!zc) {
            g_c.es_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_EMPTY_STATE=%s needs the ZERO-COUNT ARM "
                            "IN FORCE: SL_BG_TF_ZERO_COUNT=1 with "
                            "SL_BG_TF_CLIP=1 and SL_BG_TF_RASTER_CPU=1 (clip %d, "
                            "original-raster %d, zero-count effective %d) - the "
                            "mode says which conditions hold at the EMPTY DRAW, "
                            "and that draw exists only on that arm. The "
                            "empty-draw state mode stays OFF\n",
                    sl_bgtf_es_name(es_raw), sl_bgtf_opt_in(),
                    sl_bgtf_raster_cpu(), zc);
            es = SL_BGTF_ES_OFF;
        }
    }

    /* THE POST-RESET BOUNDARY CONTROL'S OWN TERMS, asked last because it is the
     * only one whose effect is OUTSIDE the prepass bracket the clauses above
     * settle. Every one of them REFUSES the control rather than half-applying
     * it, and every refusal is counted once at the end of this function. These
     * clauses are reached only when an arm was requested, so an off launch
     * prints nothing of this control's.
     *
     * AN UNREADABLE VALUE IS A REFUSAL, NOT A DEFAULT. The grammar admits four
     * tokens; anything else was offered and cannot be read, and choosing an arm
     * for it would issue one set of calls under another's name.
     *
     * EXCLUSIVE WITH THE STATE-ONLY CONTROL FOR A STRUCTURAL REASON, which is
     * worth separating from the two attribution reasons below. That control's
     * whole content is the OMISSION of the prepass draw, and this control's
     * boundary is defined as the point after that draw has been ISSUED - so on
     * that arm there is no instant this boundary names. It is not that the two
     * results would be hard to attribute; it is that one of them does not exist.
     *
     * EXCLUSIVE WITH THE POSITION-ONLY-INPUT AND PRIVATE-VERTEX-ARRAY CONTROLS
     * FOR AN ATTRIBUTION REASON. Both bracket the prepass draw and both have
     * closed their bracket before this boundary, so neither STATE conflicts with
     * it; what conflicts is the reading. Each is a different experiment on one
     * prepass, and a launch running two of them cannot attribute an observed
     * difference to either - which is the same ground the existing pairs refuse
     * each other on.
     *
     * EXCLUSIVE WITH THE BUFFER DIAGNOSTIC, AND ONLY THIS CONTROL STANDS DOWN,
     * for the reason the private and zero-count controls stand down from it: its
     * one attempt, its grammar and its spent-attempt state are its own, and
     * turning it off from here would reach into a state machine this control has
     * no business in. The overlap is real rather than theoretical - that
     * diagnostic's read is placed after the prepass's feedback ended and before
     * the selected indexed draw, which is the interval this boundary sits in, so
     * its placement claim and these calls would be about the same instant. The
     * presence test is the diagnostic's own off test, so no selector of its is
     * parsed and no line of its is printed from here.
     *
     * AND IT NEEDS ONE COUPLED SWITCH AND ONLY ONE. SL_BG_TF_CLIP=1, because
     * nothing else admits a walk to the prepass and the boundary is inside it.
     * SL_BG_TF_RASTER_CPU is deliberately NOT required: the boundary exists on
     * the REAL nonzero transform path too, and requiring the original-raster
     * control would confine this control to an arm that never binds the pool.
     * Set without SL_BG_TF_CLIP=1, it answers OFF and changes nothing whatever.
     *
     * EVERY EXCLUSION IS ASKED OF THE RAW REQUEST, not of the effective flag,
     * for the measured reason recorded at the top of this function: the clauses
     * above resolve the older controls by CLEARING them, so a launch that
     * requested two of them would otherwise reach here with both already 0 and
     * be handed this control silently. The partner's flags are NOT written here
     * - this control stands down alone and each older control is left exactly as
     * its own clauses decided it. */
    if (pr_raw < 0) {
        g_c.pr_refusals++;
        fprintf(stderr, "#95 SL_BG_TF_POST_RESET=\"%s\" is not one of "
                        "POST_PROGRAM0_EMPTY, POST_FINISH, BOTH, OFF or 0 - the "
                        "arms are exact whole tokens and nothing is normalised, "
                        "case-folded, trimmed or abbreviated, because issuing "
                        "one arm's calls under another arm's name is the one "
                        "failure a multi-arm comparison cannot absorb. The "
                        "post-reset boundary control stays OFF and this launch "
                        "is whatever its other switches selected\n",
                pr_text != NULL ? pr_text : "");
        pr = SL_BGTF_PR_OFF;
    } else if (pr_raw != SL_BGTF_PR_OFF) {
        if (raw_so) {
            g_c.pr_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_POST_RESET=%s with "
                            "SL_BG_TF_STATE_ONLY=1 requested is NOT A "
                            "COMBINATION - this control's boundary is the point "
                            "AFTER the prepass draw has been issued, and that "
                            "control OMITS that draw, so on its arm the "
                            "boundary names no instant at all. The post-reset "
                            "boundary control stays OFF; the state-only control "
                            "is left exactly as the clauses above decided it\n",
                    sl_bgtf_pr_name(pr_raw));
            pr = SL_BGTF_PR_OFF;
        } else if (raw_pi || raw_pv) {
            g_c.pr_refusals++;
            fprintf(stderr, "#95 SL_BG_TF_POST_RESET=%s with "
                            "SL_BG_TF_POS_INPUT=%d or SL_BG_TF_PRIVATE_VAO=%d "
                            "requested is AMBIGUOUS - each of those brackets the "
                            "prepass draw and closes its bracket before this "
                            "boundary, so neither conflicts with it in STATE, "
                            "but each is a different experiment on the same "
                            "prepass and a launch running two of them cannot "
                            "attribute a difference to either. The post-reset "
                            "boundary control stays OFF; the requested control is "
                            "left exactly as the clauses above decided it\n",
                    sl_bgtf_pr_name(pr_raw), raw_pi, raw_pv);
            pr = SL_BGTF_PR_OFF;
        } else {
            const char *d = getenv("SL_BG_TF_DUMP");
            if (d != NULL && d[0] != '\0' && !(d[0] == '0' && d[1] == '\0')) {
                g_c.pr_refusals++;
                fprintf(stderr, "#95 SL_BG_TF_POST_RESET=%s with "
                                "SL_BG_TF_DUMP set is AMBIGUOUS - the buffer "
                                "diagnostic's one read is placed after the "
                                "prepass's transform feedback ended and before "
                                "the selected indexed draw, which is the "
                                "interval this boundary sits in, so its "
                                "placement claim and these calls would be about "
                                "the same instant. The post-reset boundary "
                                "control stays OFF; the diagnostic is left "
                                "exactly as its own switch and one attempt "
                                "decide\n", sl_bgtf_pr_name(pr_raw));
                pr = SL_BGTF_PR_OFF;
            } else if (sl_bgtf_opt_in() != 1) {
                g_c.pr_refusals++;
                fprintf(stderr, "#95 SL_BG_TF_POST_RESET=%s needs "
                                "SL_BG_TF_CLIP=1 (clip %d) - nothing else "
                                "admits a walk to the prepass, and this "
                                "control's boundary is INSIDE that prepass. "
                                "SL_BG_TF_RASTER_CPU is deliberately not "
                                "required: the boundary exists on the real "
                                "nonzero transform path too. The post-reset "
                                "boundary control stays OFF\n",
                        sl_bgtf_pr_name(pr_raw), sl_bgtf_opt_in());
                pr = SL_BGTF_PR_OFF;
            }
        }
    }

    /* THE NEUTRAL-PROGRAM CONTROL'S OWN TERMS, asked last because it refines the
     * same empty draw the clauses above have settled and because it is exclusive
     * with every one of them. Each clause REFUSES the control rather than
     * half-applying it, no clause writes another control's answer, and the one
     * refusal is counted once at the end of this function. These clauses are
     * reached only when the control was requested, so an off launch prints
     * nothing of this control's.
     *
     * EXCLUSIVE WITH THE EMPTY-DRAW STATE MODE, AND NEITHER IS RANKED. That mode
     * varies the two conditions and the four glUseProgram positions around the
     * same call; this control replaces the program bound for it. A launch asking
     * for both is asking for two accounts of one draw's program, each of which
     * would attribute the other's effect to itself. The control stands down and
     * the mode is left exactly as its own clauses decided it, so a reader's row
     * name still means what that mode says it means.
     *
     * EXCLUSIVE WITH THE POST-RESET BOUNDARY CONTROL, FOR A STRUCTURAL REASON.
     * That control's guard requires program 0 to be the LAST program this
     * bracket made current. This control's restore deliberately leaves the
     * ordinary prepass program current until the common cleanup's own
     * glUseProgram(0) - which is the same instant on every arm - so the two are
     * not in conflict about state, but the boundary's reading of "which program
     * was current" would be about a bracket that held three programs rather than
     * two. The draw-site guard refuses it a second time for the same reason, so
     * the property holds whether or not this arbiter is consulted.
     *
     * EXCLUSIVE WITH THE THREE OLDER CONTROLS. The state-only control OMITS the
     * draw there would be a program to bind for, which is structural rather than
     * attribution: on its arm this control names no call at all. The
     * position-only-input control changes what the draw READS and the private
     * vertex-array control changes the OBJECT it runs inside, and each is a
     * different experiment on the same draw.
     *
     * EXCLUSIVE WITH THE BUFFER DIAGNOSTIC AND WITH THE RENDERER-STATE PROBE,
     * AND ONLY THIS CONTROL STANDS DOWN - the reason the zero-count and private
     * controls stand down from the first of them. Each has its own switch,
     * grammar and spent state, and turning one off from here would reach into a
     * state machine this control has no business in. The overlap is real in both
     * cases: the diagnostic records a draw whose vertex source IS a prepass pool
     * and an empty call writes no pool to record, and the probe samples the
     * current program at phases that bracket this draw, so it would record this
     * control's second program as the one the prepass used. The presence test is
     * each one's own off test, so no selector of theirs is parsed and no line of
     * theirs is printed from here.
     *
     * AND IT NEEDS THE ZERO-COUNT ARM IN FORCE, not merely requested, for the
     * reason the empty-draw mode does: the call this control re-programs is the
     * EMPTY draw, and that draw exists only on an effective zero-count arm. The
     * coupling is therefore on `zc` - the answer the clauses above produced - and
     * SL_BG_TF_CLIP=1 and SL_BG_TF_RASTER_CPU=1 are terms of it already and are
     * named in the message so a reader sees which one was missing.
     *
     * EVERY EXCLUSION IS ASKED OF THE RAW REQUEST, not of the effective flag,
     * for the measured reason recorded at the top of this function: the clauses
     * above resolve the older controls by CLEARING them, so a launch that
     * requested two of them would otherwise reach here with both already 0 and
     * be handed this control silently. */
    if (np) {
        if (es_raw != SL_BGTF_ES_OFF) {
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1 with "
                            "SL_BG_TF_EMPTY_STATE set is AMBIGUOUS - that mode "
                            "varies the conditions and the program switches at "
                            "the SAME empty draw this control re-programs, so "
                            "each would attribute the other's effect to "
                            "itself. The neutral-program empty-draw control "
                            "stays off; the empty-draw state mode is left "
                            "exactly as the clauses above decided it\n");
            np = 0;
        } else if (pr_raw != SL_BGTF_PR_OFF) {
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1 with "
                            "SL_BG_TF_POST_RESET set is NOT A COMBINATION - "
                            "that control's boundary runs only when program 0 "
                            "is the LAST program the bracket made current, and "
                            "this control leaves the ordinary prepass program "
                            "current until the common cleanup, so the "
                            "boundary's reading of which program was current "
                            "would be about a bracket holding a third one. The "
                            "neutral-program empty-draw control stays off; the "
                            "post-reset boundary control is left exactly as the "
                            "clauses above decided it\n");
            np = 0;
        } else if (raw_so) {
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1 with "
                            "SL_BG_TF_STATE_ONLY=1 requested is NOT A "
                            "COMBINATION - this control re-programs the prepass "
                            "draw and that control OMITS it, so on its arm "
                            "there is no call for a second program to be bound "
                            "at. The neutral-program empty-draw control stays "
                            "off; the state-only control is left exactly as the "
                            "clauses above decided it\n");
            np = 0;
        } else if (raw_pi || raw_pv) {
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1 with "
                            "SL_BG_TF_POS_INPUT=%d or SL_BG_TF_PRIVATE_VAO=%d "
                            "requested is AMBIGUOUS - one changes what the "
                            "prepass draw READS and the other the array object "
                            "it runs INSIDE, and this control changes the "
                            "program it runs UNDER, so a launch running two of "
                            "them cannot attribute a difference to either. The "
                            "neutral-program empty-draw control stays off; the "
                            "requested control is left exactly as the clauses "
                            "above decided it\n", raw_pi, raw_pv);
            np = 0;
        } else {
            const char *d = getenv("SL_BG_TF_DUMP");
            const char *g = getenv("SL_BG_GLSTATE");
            if (d != NULL && d[0] != '\0' && !(d[0] == '0' && d[1] == '\0')) {
                fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1 with "
                                "SL_BG_TF_DUMP set is AMBIGUOUS - the buffer "
                                "diagnostic records a draw whose vertex source "
                                "IS a prepass pool, this control runs only on "
                                "the zero-count arm under which the pool is "
                                "never bound, and an empty call writes no pool "
                                "to record. The neutral-program empty-draw "
                                "control stays off; the diagnostic is left "
                                "exactly as its own switch and one attempt "
                                "decide\n");
                np = 0;
            } else if (g != NULL && g[0] != '\0'
                       && !(g[0] == '0' && g[1] == '\0')) {
                fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1 with "
                                "SL_BG_GLSTATE set is AMBIGUOUS - that probe "
                                "records the CURRENT PROGRAM at phases that "
                                "bracket this draw, so a record sealed under "
                                "this control would name its second program as "
                                "the one the prepass used. The neutral-program "
                                "empty-draw control stays off; the probe is "
                                "left exactly as its own switch and provenance "
                                "decide\n");
                np = 0;
            } else if (!zc) {
                fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1 needs the "
                                "ZERO-COUNT ARM IN FORCE: SL_BG_TF_ZERO_COUNT=1 "
                                "with SL_BG_TF_CLIP=1 and "
                                "SL_BG_TF_RASTER_CPU=1 (clip %d, "
                                "original-raster %d, zero-count effective %d) - "
                                "the call this control re-programs is the EMPTY "
                                "draw, and that draw exists only on that arm. "
                                "The neutral-program empty-draw control stays "
                                "off\n",
                        sl_bgtf_opt_in(), sl_bgtf_raster_cpu(), zc);
                np = 0;
            }
        }
    }
    /* ONE REFUSAL PER LAUNCH, AND ONLY FOR A LAUNCH THAT ASKED, exactly as the
     * zero-count control's is counted: the clauses above are an if/else chain so
     * at most one speaks, and the outcome either way is the single fact that this
     * control was asked for and is not in force. */
    if (np_asked && !np) g_c.np_refusals++;

    /* THE AUXILIARY-CONTEXT CONTROL'S OWN TERMS, asked in the order a reader
     * needs them and resolved exactly as the control above it is.
     *
     * EXCLUSIVE WITH EVERY DIAGNOSTIC THAT TOUCHES THE SAME CALL, AND THIS
     * CONTROL IS THE ONE THAT STANDS DOWN. The empty-draw mode varies the
     * conditions and the program switches at the call this control MOVES, so
     * each would attribute the other's effect to itself. The neutral-program
     * control changes which program that call runs under, which is a second
     * variable on the same call and the one question closest to this one - a
     * launch asking for both would not be able to say whether a difference
     * belonged to the program or to the context. The post-reset boundary runs
     * under a guard about the last program the BRACKET made current, which says
     * nothing once the call it follows has moved to another context. The
     * state-only control omits the call there would be nothing to relocate; the
     * position-only-input control changes what it reads and the private
     * vertex-array control the object it runs inside, and this control
     * deliberately reproduces NEITHER on the auxiliary context, so a launch
     * running one of them with this one would relocate a call without its
     * declared surroundings.
     *
     * EXCLUSIVE WITH THE BUFFER DIAGNOSTIC AND WITH THE RENDERER-STATE PROBE,
     * AND ONLY THIS CONTROL STANDS DOWN, for the reason the controls above give:
     * each has its own switch, grammar and spent state, and turning one off from
     * here would reach into a state machine this control has no business in. The
     * overlap is real in both cases - the diagnostic records a draw whose vertex
     * source IS a prepass pool and an empty call on another context writes no
     * pool to record, and the probe samples the program current at phases that
     * bracket this draw and would report the MAIN context's state for a call
     * made on a different one. The presence test is each one's own off test, so
     * no selector of theirs is parsed and no line of theirs is printed here.
     *
     * AND IT NEEDS THE ZERO-COUNT ARM IN FORCE, not merely requested: the call
     * this control relocates is the EMPTY draw, which exists only on an
     * effective zero-count arm. The coupling is therefore on `zc` - the answer
     * the clauses above produced - and SL_BG_TF_CLIP=1 and SL_BG_TF_RASTER_CPU=1
     * are terms of it already and are named in the message so a reader sees
     * which one was missing.
     *
     * EVERY EXCLUSION IS ASKED OF THE RAW REQUEST, not of the effective flag,
     * for the measured reason recorded at the top of this function. `np_asked`
     * is the neutral control's raw request and is written nowhere after it is
     * read, so a launch that asked for both gets neither silently: the control
     * above resolves its own clauses first and keeps whatever they left it, and
     * this one stands down here. */
    if (ax) {
        if (es_raw != SL_BGTF_ES_OFF) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 with "
                            "SL_BG_TF_EMPTY_STATE set is AMBIGUOUS - that mode "
                            "varies the conditions and the program switches at "
                            "the SAME empty draw this control MOVES to a second "
                            "context, so each would attribute the other's "
                            "effect to itself. NO AUXILIARY GL CONTEXT IS "
                            "CREATED and the auxiliary-context empty-draw "
                            "control stays off; the empty-draw state mode is "
                            "left exactly as the clauses above decided it\n");
            ax = 0;
        } else if (np_asked) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 with "
                            "SL_BG_TF_NEUTRAL_PROG=1 requested is AMBIGUOUS - "
                            "one changes WHICH PROGRAM the empty draw runs "
                            "under and this one WHICH CONTEXT it is issued on, "
                            "so a launch running both could not say which of "
                            "the two a difference belonged to. NO AUXILIARY GL "
                            "CONTEXT IS CREATED and the auxiliary-context "
                            "empty-draw control stays off; the neutral-program "
                            "control is left exactly as the clauses above "
                            "decided it\n");
            ax = 0;
        } else if (pr_raw != SL_BGTF_PR_OFF) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 with "
                            "SL_BG_TF_POST_RESET set is NOT A COMBINATION - "
                            "that control's boundary runs only when program 0 "
                            "is the LAST program the bracket made current, and "
                            "once the draw it follows has moved to another "
                            "context that reading is about a bracket whose "
                            "draw it does not contain. NO AUXILIARY GL CONTEXT "
                            "IS CREATED and the auxiliary-context empty-draw "
                            "control stays off; the post-reset boundary control "
                            "is left exactly as the clauses above decided it\n");
            ax = 0;
        } else if (raw_so) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 with "
                            "SL_BG_TF_STATE_ONLY=1 requested is NOT A "
                            "COMBINATION - this control MOVES the prepass draw "
                            "to a second context and that control OMITS it, so "
                            "on its arm there is no call to relocate. NO "
                            "AUXILIARY GL CONTEXT IS CREATED and the "
                            "auxiliary-context empty-draw control stays off; "
                            "the state-only control is left exactly as the "
                            "clauses above decided it\n");
            ax = 0;
        } else if (raw_pi || raw_pv) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 with "
                            "SL_BG_TF_POS_INPUT=%d or SL_BG_TF_PRIVATE_VAO=%d "
                            "requested is AMBIGUOUS - one changes what the "
                            "prepass draw READS and the other the array object "
                            "it runs INSIDE, this control changes the CONTEXT "
                            "it is issued on and reproduces NEITHER of those "
                            "surroundings there, so a launch running two of "
                            "them cannot attribute a difference to either. NO "
                            "AUXILIARY GL CONTEXT IS CREATED and the "
                            "auxiliary-context empty-draw control stays off; "
                            "the requested control is left exactly as the "
                            "clauses above decided it\n", raw_pi, raw_pv);
            ax = 0;
        } else {
            const char *d = getenv("SL_BG_TF_DUMP");
            const char *g = getenv("SL_BG_GLSTATE");
            if (d != NULL && d[0] != '\0' && !(d[0] == '0' && d[1] == '\0')) {
                fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 with "
                                "SL_BG_TF_DUMP set is AMBIGUOUS - the buffer "
                                "diagnostic records a draw whose vertex source "
                                "IS a prepass pool, this control runs only on "
                                "the zero-count arm under which the pool is "
                                "never bound, and an empty call on a second "
                                "context writes no pool to record. NO "
                                "AUXILIARY GL CONTEXT IS CREATED and the "
                                "auxiliary-context empty-draw control stays "
                                "off; the diagnostic is left exactly as its own "
                                "switch and one attempt decide\n");
                ax = 0;
            } else if (g != NULL && g[0] != '\0'
                       && !(g[0] == '0' && g[1] == '\0')) {
                fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 with "
                                "SL_BG_GLSTATE set is AMBIGUOUS - that probe "
                                "records the CURRENT PROGRAM at phases that "
                                "bracket this draw, and with the draw issued "
                                "on another context a record sealed under this "
                                "control would describe the MAIN context's "
                                "state for a call it never made. NO AUXILIARY "
                                "GL CONTEXT IS CREATED and the "
                                "auxiliary-context empty-draw control stays "
                                "off; the probe is left exactly as its own "
                                "switch and provenance decide\n");
                ax = 0;
            } else if (!zc) {
                fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1 needs the "
                                "ZERO-COUNT ARM IN FORCE: SL_BG_TF_ZERO_COUNT=1 "
                                "with SL_BG_TF_CLIP=1 and "
                                "SL_BG_TF_RASTER_CPU=1 (clip %d, "
                                "original-raster %d, zero-count effective %d) - "
                                "the call this control relocates is the EMPTY "
                                "draw, and that draw exists only on that arm. "
                                "NO AUXILIARY GL CONTEXT IS CREATED and the "
                                "auxiliary-context empty-draw control stays "
                                "off\n",
                        sl_bgtf_opt_in(), sl_bgtf_raster_cpu(), zc);
                ax = 0;
            }
        }
    }
    /* ONE REFUSAL PER LAUNCH, AND ONLY FOR A LAUNCH THAT ASKED, exactly as the
     * controls above count theirs: the clauses are an if/else chain so at most
     * one speaks, and the outcome either way is the single fact that this
     * control was asked for and is not in force. */
    if (ax_asked && !ax) g_c.ax_refusals++;

    /* THE REAL-PREPASS MODE'S OWN TERMS, asked in the order a reader needs them
     * and resolved exactly as every control above is.
     *
     * EXCLUSIVE WITH EVERY OTHER CONTROL IN THIS FILE, AND THIS MODE IS THE ONE
     * THAT STANDS DOWN. It is LAST in the chain for that reason: the older
     * controls resolve their own clauses first and keep whatever those left
     * them, and this one is the one that stands down beside them. The reasons
     * differ by control and each is named where it is refused, but they come to
     * one thing: this mode relocates the WHOLE prepass bracket to a second
     * context, so any control that varies the bracket's count, conditions,
     * program, input, array object, boundary or the context the call is on would
     * be describing a bracket this mode does not run on the context that control
     * is about.
     *
     * AND IT NEEDS THE REAL NONZERO COUNT. SL_BG_TF_ZERO_COUNT's whole content
     * is a zero vertex count, which is the opposite of what this mode measures,
     * so the two are mutually exclusive rather than composable - and the
     * coupling is on `zc`, the answer the clauses above produced, so a
     * zero-count request that was itself refused does not refuse this mode.
     *
     * AND THE ARM MUST MATCH THE ORIGINAL-RASTER CONTROL. CPU_SOURCE rasterises
     * the ORIGINAL positions, which is what SL_BG_TF_RASTER_CPU=1 does and
     * nothing else does; GPU_SOURCE rasterises the SHARED POOL, which that same
     * control is precisely what prevents. So each arm requires its own value of
     * it and a mismatch is REFUSED rather than reinterpreted: the arm names
     * which source the launch is about, and a launch whose raster source
     * disagrees with its arm cannot say which of the two it is running.
     *
     * EVERY EXCLUSION IS ASKED OF THE RAW REQUEST, not of the effective flag,
     * for the measured reason recorded at the top of this function. */
    if (ar != SL_BGTF_AR_OFF) {
        if (sl_bgtf_opt_in() != 1) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s needs SL_BG_TF_CLIP=1 "
                            "(clip %d) - nothing else admits a walk to the "
                            "prepass, and there is no prepass to relocate "
                            "without one. NO AUXILIARY GL CONTEXT IS CREATED "
                            "and the auxiliary-context real-prepass mode stays "
                            "OFF\n", sl_bgtf_ar_name(ar), sl_bgtf_opt_in());
            ar = SL_BGTF_AR_OFF;
        } else if (ax_asked) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with SL_BG_TF_AUX_EMPTY=1 "
                            "requested is AMBIGUOUS - both relocate a prepass "
                            "draw to a second context and they differ in "
                            "exactly the thing a reader would be comparing: one "
                            "issues an EMPTY call with no feedback scope and no "
                            "output, this one the REAL transform. A launch "
                            "running both could not say which of the two a "
                            "difference belonged to. NO AUXILIARY GL CONTEXT IS "
                            "CREATED for this mode and it stays OFF; the "
                            "empty-draw control is left exactly as the clauses "
                            "above decided it\n", sl_bgtf_ar_name(ar));
            ar = SL_BGTF_AR_OFF;
        } else if (es_raw != SL_BGTF_ES_OFF) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with SL_BG_TF_EMPTY_STATE "
                            "set is AMBIGUOUS - that mode varies the conditions "
                            "and the program switches AT a draw on the MAIN "
                            "context, and this mode does not make that draw "
                            "there at all. NO AUXILIARY GL CONTEXT IS CREATED "
                            "and the auxiliary-context real-prepass mode stays "
                            "OFF; the empty-draw state mode is left exactly as "
                            "the clauses above decided it\n",
                    sl_bgtf_ar_name(ar));
            ar = SL_BGTF_AR_OFF;
        } else if (np_asked) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with "
                            "SL_BG_TF_NEUTRAL_PROG=1 requested is AMBIGUOUS - "
                            "that control binds a DIFFERENT program at the "
                            "prepass draw and this mode runs the prepass "
                            "program on a DIFFERENT CONTEXT, so a launch "
                            "running both could not say which of the two a "
                            "difference belonged to. NO AUXILIARY GL CONTEXT IS "
                            "CREATED for this mode and it stays OFF; the "
                            "neutral-program control is left exactly as the "
                            "clauses above decided it\n", sl_bgtf_ar_name(ar));
            ar = SL_BGTF_AR_OFF;
        } else if (pr_raw != SL_BGTF_PR_OFF) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with SL_BG_TF_POST_RESET "
                            "set is NOT A COMBINATION - that control's boundary "
                            "runs only when the bracket can show the prepass "
                            "draw it issued and program 0 last made current on "
                            "THIS context, and this mode issues that draw on "
                            "another one. NO AUXILIARY GL CONTEXT IS CREATED "
                            "and the auxiliary-context real-prepass mode stays "
                            "OFF; the post-reset boundary control is left "
                            "exactly as the clauses above decided it\n",
                    sl_bgtf_ar_name(ar));
            ar = SL_BGTF_AR_OFF;
        } else if (raw_so) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with "
                            "SL_BG_TF_STATE_ONLY=1 requested is NOT A "
                            "COMBINATION - this mode RELOCATES the prepass draw "
                            "and that control OMITS it, so on its arm there is "
                            "no transform to run anywhere. NO AUXILIARY GL "
                            "CONTEXT IS CREATED and the auxiliary-context "
                            "real-prepass mode stays OFF; the state-only "
                            "control is left exactly as the clauses above "
                            "decided it\n", sl_bgtf_ar_name(ar));
            ar = SL_BGTF_AR_OFF;
        } else if (raw_pi || raw_pv) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with "
                            "SL_BG_TF_POS_INPUT=%d or SL_BG_TF_PRIVATE_VAO=%d "
                            "requested is AMBIGUOUS - one narrows the client "
                            "arrays the prepass draw dereferences and the other "
                            "gives it a private array object ON THE MAIN "
                            "CONTEXT, and this mode sets the client array and "
                            "generates an array object of its own on the "
                            "AUXILIARY context, so two accounts of the same "
                            "state would each attribute the other's effect to "
                            "itself. NO AUXILIARY GL CONTEXT IS CREATED for "
                            "this mode and it stays OFF; the requested control "
                            "is left exactly as the clauses above decided it\n",
                    sl_bgtf_ar_name(ar), raw_pi, raw_pv);
            ar = SL_BGTF_AR_OFF;
        } else if (zc) {
            /* THE COUPLING IS ASKED AFTER THE EXCLUSIONS, and the order is
             * load-bearing rather than incidental. Two of the controls above -
             * the empty-draw state mode and the auxiliary empty-draw control -
             * REQUIRE the zero-count arm, so a launch asking for either of them
             * together with this mode necessarily has that arm in force. Asking
             * the coupling first would answer every such launch with the
             * zero-count sentence and never name the control the launch
             * actually also asked for, which is the more useful of the two
             * facts: a reader needs to be told WHICH second experiment was
             * requested. So the exclusions speak first and this clause is left
             * with the launches whose only quarrel is the count. */
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with the ZERO-COUNT ARM "
                            "IN FORCE is NOT A COMBINATION - that arm's whole "
                            "content is a ZERO vertex count and this mode's is "
                            "the REAL NONZERO one, so a launch asking for both "
                            "is asking for a prepass that submits no vertices "
                            "and for the vertices it submits. NO AUXILIARY GL "
                            "CONTEXT IS CREATED and the auxiliary-context "
                            "real-prepass mode stays OFF; the zero-count arm is "
                            "left exactly as the clauses above decided it\n",
                    sl_bgtf_ar_name(ar));
            ar = SL_BGTF_AR_OFF;
        } else if (ar == SL_BGTF_AR_CPU && sl_bgtf_raster_cpu() != 1) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=CPU_SOURCE needs "
                            "SL_BG_TF_RASTER_CPU=1 (original-raster %d) - that "
                            "arm is DEFINED as the one whose MAIN context "
                            "rasterises the ORIGINAL position stream, and the "
                            "original-raster control is the only thing that "
                            "selects it. NO AUXILIARY GL CONTEXT IS CREATED and "
                            "the auxiliary-context real-prepass mode stays "
                            "OFF\n", sl_bgtf_raster_cpu());
            ar = SL_BGTF_AR_OFF;
        } else if (ar == SL_BGTF_AR_GPU && sl_bgtf_raster_cpu() == 1) {
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=GPU_SOURCE needs "
                            "SL_BG_TF_RASTER_CPU off (original-raster %d) - "
                            "that arm is DEFINED as the one whose MAIN context "
                            "rasterises the SHARED transform-feedback pool, and "
                            "the original-raster control is precisely what "
                            "stops that pool being bound. NO AUXILIARY GL "
                            "CONTEXT IS CREATED and the auxiliary-context "
                            "real-prepass mode stays OFF\n",
                    sl_bgtf_raster_cpu());
            ar = SL_BGTF_AR_OFF;
        } else {
            /* THE TWO DIAGNOSTICS, TESTED BY PRESENCE AND NOTHING ELSE, for the
             * reason the controls above give: each has its own switch, grammar
             * and spent state, and turning one off from here would reach into a
             * state machine this mode has no business in. The overlap is real in
             * both cases - the buffer diagnostic reads a pool back on the MAIN
             * context while this mode's writes are established visible there by
             * a synchronisation of its own, so a capture under it would be
             * measuring this mode's sync rather than the pool; and the probe
             * samples the program and bindings current at phases that bracket a
             * draw this mode makes on another context. No selector of theirs is
             * parsed here and no line of theirs is printed. */
            const char *d = getenv("SL_BG_TF_DUMP");
            const char *g = getenv("SL_BG_GLSTATE");
            if (d != NULL && d[0] != '\0' && !(d[0] == '0' && d[1] == '\0')) {
                fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with SL_BG_TF_DUMP "
                                "set is AMBIGUOUS - that diagnostic reads the "
                                "pool back on the MAIN context, and under this "
                                "mode the pool was written on another one and "
                                "is made visible here by a synchronisation this "
                                "mode issues, so a capture would be recording "
                                "that synchronisation and not the transform. NO "
                                "AUXILIARY GL CONTEXT IS CREATED and the "
                                "auxiliary-context real-prepass mode stays OFF; "
                                "the diagnostic is left exactly as its own "
                                "switch and one attempt decide\n",
                        sl_bgtf_ar_name(ar));
                ar = SL_BGTF_AR_OFF;
            } else if (g != NULL && g[0] != '\0'
                       && !(g[0] == '0' && g[1] == '\0')) {
                fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s with SL_BG_GLSTATE "
                                "set is AMBIGUOUS - that probe records the "
                                "CURRENT PROGRAM and the bindings at phases "
                                "that bracket this draw, and with the draw and "
                                "its whole setup issued on another context a "
                                "record sealed under this mode would describe "
                                "the MAIN context's state for calls it never "
                                "made. NO AUXILIARY GL CONTEXT IS CREATED and "
                                "the auxiliary-context real-prepass mode stays "
                                "OFF; the probe is left exactly as its own "
                                "switch and provenance decide\n",
                        sl_bgtf_ar_name(ar));
                ar = SL_BGTF_AR_OFF;
            }
        }
    }
    /* v0.4 THE OPT-IN CANDIDATE'S OWN SELECTION, ASKED LAST AND OF NOTHING THE
     * LAUNCH NAMED (SL_BG_TF_CLIP=1 with SL_BG_TF_AUX_REAL NOT REQUESTED).
     *
     * WHAT IT DECIDES. A launch that opted into the background GPU clip prepass
     * and named no arm takes the AUXILIARY-CONTEXT GPU_SOURCE arm. The commands
     * are the same ones the named arm issues: this adds no path, no call and no
     * object, and it changes WHICH of two existing paths an unqualified opt-in
     * reaches.
     *
     * WHY NOT THE MAIN-CONTEXT PREPASS. That is what this selection replaces.
     * Measured on one Runway viewpoint at three moments, one machine and one
     * driver, from one seeded save: the auxiliary GPU_SOURCE arm's frames were
     * byte-identical to the original renderer's, blue 143 at (71,113), under
     * both synchronisation modes; the main-context prepass produced blue 142 in
     * the same scene. So the opt-in cannot reach the main-context prepass by
     * default - and because that path remains the diagnostic a comparison needs,
     * it stays reachable by NAME, which is what the explicit off tokens are for.
     *
     * v0.4 IT IS THE DEVELOPER ROUTE TO THE ARM THE ROW ALSO SELECTS. This
     * clause once said there was no settings row and no menu item; there is
     * both, and an ordinary row-ON walk reaches this same arm through
     * sl_bgtf_walk_admit. What this clause decides is narrower than it was:
     * what SL_BG_TF_CLIP=1 MEANS once given, for a launch that named no arm.
     * The two routes issue identical commands and are told apart by
     * sl_bgtf_production, never by anything either of them emits into a frame.
     *
     * AND IT IS ASKED OF THE SAME EXCLUSIONS THE NAMED ARM IS ASKED OF, in the
     * same shapes - the RAW requests for the three older controls, the effective
     * zero-count arm, the presence of the two diagnostics - so a launch cannot
     * be handed this arm and one of those at once. THE OUTCOME DIFFERS FROM A
     * NAMED ARM'S, though: a named arm is REFUSED and counted, because the
     * launch asked for something it cannot have, while an unnamed one STANDS
     * DOWN, because the launch asked for a diagnostic and that diagnostic is
     * precisely the main-context prepass. The stand-down is stated rather than
     * silent: a reader of such a launch must not have to infer which prepass ran.
     *
     * THE RASTER SOURCE IS PART OF THE SAME QUESTION. GPU_SOURCE is defined as
     * the arm whose main context rasterises the shared pool, which is what the
     * original-raster control prevents, so a launch holding that control is one
     * of those diagnostics too and this stands down for it by name.
     *
     * NO ENVIRONMENT KEY IS ADDED ANYWHERE, AND TWO ARE READ A SECOND TIME.
     * Every term of the stand-down question is an answer the clauses above
     * already computed from keys this function has already read, with one
     * exception stated exactly: the two DIAGNOSTICS below are tested by presence
     * through their own getenv calls, SL_BG_TF_DUMP and SL_BG_GLSTATE, which the
     * named arm's clauses read in the same way a few lines above. So the set of
     * keys this module reads is unchanged, and what is added on a launch that
     * opted in and named no arm is two reads of keys already in that set. An off
     * launch reaches neither, because the opt-in answer is a term of the
     * condition. */
    if (ar == SL_BGTF_AR_OFF && ar_absent && sl_bgtf_opt_in() == 1) {
        if (raw_so || raw_pi || raw_pv || zc || ax_asked || np_asked
            || es_raw != SL_BGTF_ES_OFF || pr_raw != SL_BGTF_PR_OFF
            || sl_bgtf_raster_cpu() == 1) {
            tf_ar_optin_standdown();
        } else {
            const char *d = getenv("SL_BG_TF_DUMP");
            const char *g = getenv("SL_BG_GLSTATE");
            if ((d != NULL && d[0] != '\0' && !(d[0] == '0' && d[1] == '\0'))
                || (g != NULL && g[0] != '\0'
                    && !(g[0] == '0' && g[1] == '\0'))) {
                /* THE TWO DIAGNOSTICS STAND THIS DOWN TOO, for the reasons the
                 * named arm's own clauses give: one reads the pool back on the
                 * MAIN context, where under this arm the contents arrive through
                 * a synchronisation this arm issues, and the other samples the
                 * program and bindings current at phases bracketing a draw this
                 * arm makes on another context. A launch running either is
                 * capturing the main context's prepass, so it keeps it. */
                tf_ar_optin_standdown();
            } else {
                /* THE ARM IS GRANTED HERE AND ANNOUNCED BELOW, after the
                 * synchronisation mode has been decided. The announcement names
                 * the mode in force, and nothing can name it before it is
                 * decided - which is what made the earlier single statement
                 * false for a launch that chose the other one. */
                ar = SL_BGTF_AR_GPU;
                g_ar_implicit = 1;
            }
        }
    }
    /* v0.4 AND THE SYNCHRONISATION MODE IS DECIDED WITH THE ARM, FOR BOTH ARMS
     * (SL_BG_TF_AUX_SYNC). See tf_fx_decide for why it is here rather than at
     * the first served draw, and for what each value answers.
     *
     * AN UNREADABLE VALUE REFUSES THE ARM AND FAILS THE WALK CLOSED. This is the
     * one outcome neither earlier behaviour had: serving such a launch with
     * FENCE ran a mode it had not selected, serving it with FINISH ran the
     * other, and standing the arm down without the latch would have handed an
     * opted-in walk to the MAIN-CONTEXT prepass - the arm measured ONE BLUE
     * VALUE APART from the original renderer's frames on the scene that carries
     * the defect. Every served draw goes to the existing CPU raster path
     * instead, refused at the one site that is reached before any GL state of
     * the bracket is written.
     *
     * IT IS KEYED BY THE SELECTOR AND NEVER BY ITS VALUE. The launch offered
     * something unreadable; naming the key and the two tokens it accepts is what
     * a reader needs, and echoing the value would put an environment value in
     * this module's output for no gain.
     *
     * A VALID VALUE CHANGES NOTHING FOR A NAMED ARM. FINISH and FENCE named on a
     * named arm are the modes they were, decided from the same key by the same
     * exact-token grammar, and a named arm with no key keeps FINISH - so a valid
     * named-arm launch's output and call sequence are byte-for-byte what they
     * were before this clause existed. */
    if (ar != SL_BGTF_AR_OFF && tf_fx_decide(g_ar_implicit, &fx_named) < 0) {
        tf_ar_sync_standdown();
        ar = SL_BGTF_AR_OFF;
        g_ar_implicit = 0;
    }
    /* ONE REFUSAL PER LAUNCH, AND ONLY FOR A LAUNCH THAT ASKED, exactly as every
     * control above counts its own: the clauses are an if/else chain so at most
     * one speaks, and the outcome either way is the single fact that this mode
     * was asked for and is not in force. An UNREADABLE arm value asked for
     * nothing - it is reported by the parser and is not a refusal - which is the
     * same rule the two older selectors follow.
     *
     * IT IS ASKED AFTER THE SYNCHRONISATION DECISION, so a NAMED arm refused for
     * an unreadable synchronisation value is counted here with every other
     * refusal of a named arm rather than being dropped. An UNNAMED arm refused
     * the same way is not counted: nothing was asked for by name, and the line
     * above is the whole statement about that launch. */
    if (ar_raw > 0 && ar == SL_BGTF_AR_OFF) g_c.ar_refusals++;

    /* v0.4 AND THE CANDIDATE ANNOUNCES ITSELF, NAMING THE MODE IN FORCE.
     *
     * THE LINE IS TRUE FOR ALL THREE SUPPORTED COMBINATIONS, which is why it is
     * printed here and built from the decisions above rather than from the
     * selection's own expectations: the candidate's default FENCE, an explicit
     * FENCE and an explicit FINISH are each named by the token that is actually
     * in force, and the clause after it says which of the two decided. A single
     * sentence claiming FENCE was false for the launch that named FINISH, and a
     * later census being accurate does not unsay a false startup line.
     *
     * ONLY THE CANDIDATE PRINTS IT. A named arm's output is unchanged: it had no
     * line here before and has none now, because the thing this states - that an
     * opt-in with no arm named reached an auxiliary arm - is not true of it. */
    if (ar != SL_BGTF_AR_OFF && g_ar_implicit) {
        fprintf(stderr, "#95 the background GPU clip prepass's OPT-IN CANDIDATE "
                        "is in force: no arm was named, so the EFFECTIVE mode is "
                        "the auxiliary-context real prepass on arm GPU_SOURCE "
                        "with %s synchronisation, %s. That arm's frames were "
                        "measured byte-identical to the original renderer's on "
                        "the scene that carries the defect under BOTH "
                        "synchronisation modes; the MAIN-CONTEXT prepass, "
                        "measured one blue value apart there, is NOT selected "
                        "and is reachable only by name. THIS LAUNCH REACHED THE "
                        "ARM BY NAMING SL_BG_TF_CLIP, not through the OPTIMIZED "
                        "RENDERING row, which selects the same arm for an "
                        "ordinary session; no release acceptance follows from "
                        "either. "
                        "EVERY LATER LINE ABOUT THIS MODE IS SPELLED IN THE "
                        "SELECTORS' NAMES - SL_BG_TF_AUX_REAL=GPU_SOURCE with "
                        "SL_BG_TF_AUX_SYNC=%s - because that is the mode in "
                        "force, and this line is what records how it was "
                        "reached\n",
                sl_bgtf_fx_name(g_fx),
                fx_named ? "which this launch NAMED"
                         : "which no key named: it is this candidate's own "
                           "default",
                sl_bgtf_fx_name(g_fx));
    }
    /* AND AN EXPLICIT OFF TOKEN KEEPS THE MAIN-CONTEXT PREPASS AND IS LABELLED AS
     * WHAT IT IS (SL_BG_TF_AUX_REAL=OFF or 0 with the opt-in switch).
     *
     * IT IS A SELECTION, NOT AN ABSENCE, which is why it is answered here and
     * not by the clause above: a developer spelling an off token is asking for
     * the ORIGINAL main-context prepass, and the A/B comparison the measured
     * result came from needs exactly that. Nothing is refused, nothing is
     * changed, and no auxiliary context is created.
     *
     * WHAT THE LINE ADDS IS THE LABEL. That arm's frames were measured one blue
     * value apart from the original renderer's on the scene that carries the
     * defect, so a launch holding it is producing diagnostic evidence rather
     * than the candidate's output, and it says so where the frames are produced
     * rather than only in a document. No value of any key is echoed: the two off
     * tokens are not distinguished here, because what the line is about is which
     * prepass runs. */
    if (ar == SL_BGTF_AR_OFF && !ar_absent && ar_raw == SL_BGTF_AR_OFF
        && sl_bgtf_opt_in() == 1) {
        fprintf(stderr, "#95 SL_BG_TF_AUX_REAL names an OFF token, so this "
                        "launch runs the MAIN-CONTEXT prepass deliberately: the "
                        "auxiliary-context arm is not selected and NO AUXILIARY "
                        "GL CONTEXT IS CREATED. That is the DIAGNOSTIC arm and a "
                        "KNOWN MISMATCH - its frames were measured ONE BLUE "
                        "VALUE APART from the original renderer's on the scene "
                        "that carries the defect - and it is kept reachable by "
                        "name because the comparison the candidate rests on "
                        "needs it\n");
    }

    /* v0.4 AND WHETHER THE ROW IS LEFT TO DECIDE: THE PRODUCTION SELECTION'S
     * ENVIRONMENT TERMS, computed here and acted on nowhere near here.
     *
     * WHAT THIS STATEMENT IS. The one place that answers "has this launch's
     * environment left the choice of prepass to the OPTIMIZED RENDERING row?".
     * Every term is an answer the clauses above already produced from keys this
     * function has already read, except the two DIAGNOSTICS, which are tested by
     * presence through the same two getenv calls the named arm's clauses make a
     * few lines above - so the module's key set is unchanged.
     *
     * NOTHING IS DECIDED, PROMOTED OR PRINTED HERE, which is the property the
     * inactive paths rest on. This function runs on launches that draw nothing: a
     * census on a trace replay, a headless health run, the matching build. Those
     * runs never reach a prepass, because the row answers OFF before it reads
     * anything while the settings store is inactive (src/platform/sl_perf.h), and
     * their output must stay the output they already have. So the arm, the
     * synchronisation mode, the fail-closed latch and every line about them are
     * left to sl_bgtf_walk_admit, which is reached only from a walk the row
     * admitted.
     *
     * THE TERMS, AND WHY EACH ONE IS A TERM:
     *
     *   THE DEVELOPER SWITCH NAMED NOTHING. Named 1, the opt-in candidate above
     *   has already decided this launch and the production selection has nothing
     *   to add; named 0, a developer has taken the prepass out of this launch on
     *   purpose and the row may not put it back.
     *   NO ARM WAS NAMED AND THE VALUE WAS READABLE. `ar_absent` excludes the
     *   explicit OFF tokens and `ar_raw == SL_BGTF_AR_OFF` excludes both a named
     *   arm and an unreadable value. An explicit off token names the
     *   MAIN-CONTEXT prepass, which is a KNOWN MISMATCH - one blue value apart
     *   from the original renderer's frames on the scene that carries the
     *   defect - and the product may not reach it by any route, so a launch
     *   naming it is not a production launch and draws the fully produced CPU
     *   clip and raster path instead (src/gfx/sl_gfx_dl.c's chain, which reaches
     *   the background vertex transform only when a developer has explicitly
     *   named THAT); the arm stays reachable with the developer switch, which is the
     *   launch every A/B arm is written against. An unreadable value is excluded
     *   for the same reason the latch above exists: a typo must not select a
     *   known mismatch, and here it selects nothing at all.
     *   NO MAIN-CONTEXT DIAGNOSTIC WAS REQUESTED. Each of these is an experiment
     *   ON the main-context prepass, so a launch holding one is asking for that
     *   prepass; with the developer switch the candidate STANDS DOWN to it and
     *   says so, and without the switch there is no production selection to
     *   stand down - the walk draws the original renderer. The RAW requests are
     *   read, not the effective flags, for the reason recorded at the top of this
     *   function.
     *   NEITHER CAPTURE IS ARMED. The buffer diagnostic reads the pool back on
     *   the MAIN context and the renderer-state probe samples the program and
     *   bindings at phases bracketing a draw an auxiliary arm makes elsewhere, so
     *   a launch running either is capturing the main context's prepass.
     *
     * AN UNREADABLE SYNCHRONISATION VALUE IS NOT A TERM HERE, and that is
     * deliberate: it is the one shape that must FAIL CLOSED rather than fall
     * away, so it is answered by tf_fx_decide at the promotion, where the same
     * refusal and the same words serve both selections. */
    g_prod_elig = 0;
    if (sl_bgtf_opt_in() != 1 && !tf_clip_named_off()
        && ar_absent && ar_raw == SL_BGTF_AR_OFF
        && !raw_so && !raw_pi && !raw_pv && !zc_asked && !ax_asked && !np_asked
        && es_raw == SL_BGTF_ES_OFF && pr_raw == SL_BGTF_PR_OFF
        && sl_bgtf_raster_cpu() != 1) {
        const char *d = getenv("SL_BG_TF_DUMP");
        const char *g = getenv("SL_BG_GLSTATE");
        if (!(d != NULL && d[0] != '\0' && !(d[0] == '0' && d[1] == '\0'))
            && !(g != NULL && g[0] != '\0' && !(g[0] == '0' && g[1] == '\0')))
            g_prod_elig = 1;
    }

    g_sonly = so;
    g_pin = pi;
    g_pvao = pv;
    g_zc = zc;
    g_es = es;
    g_pr = pr;
    g_np = np;
    g_ax = ax;
    g_ar = ar;
    /* THE EFFECTIVE ARM IS RECORDED, NOT THE REQUESTED ONE, so a census or a
     * record carries the arm that actually ran. A refused launch leaves this 0
     * beside a positive ar_refusals. */
    g_c.ar_mode = (unsigned long) ar;
    /* THE EFFECTIVE ARM IS RECORDED, NOT THE REQUESTED ONE, so a census or a
     * record carries the arm that actually ran. A refused launch leaves this 0
     * beside a positive pr_refusals. */
    g_c.pr_mode = (unsigned long) pr;
    /* THE EFFECTIVE MODE IS RECORDED, NOT THE REQUESTED ONE, so a census or a
     * record carries the row that actually ran. A refused launch leaves this 0
     * beside a positive es_refusals. */
    g_c.es_mode = (unsigned long) es;
}

int sl_bgtf_state_only(void)
{
    tf_decide_controls();
    return g_sonly;
}

/* THE POSITION-ONLY-INPUT CONTROL. See the header for what a selected launch
 * does, what it saves and puts back, and the r594 measurement that motivates
 * it. The answer is decided by the arbiter above, together with the state-only
 * control it is exclusive with. */
int sl_bgtf_pos_input(void)
{
    tf_decide_controls();
    return g_pin;
}

/* THE PRIVATE-PREPASS-VERTEX-ARRAY CONTROL. See the header for what a selected
 * launch does, what a vertex array object does and does not hold, and the two
 * global postconditions that are therefore still handled by hand. The answer is
 * decided by the arbiter above, together with the two controls it is exclusive
 * with. */
int sl_bgtf_private_vao(void)
{
    tf_decide_controls();
    return g_pvao;
}

/* THE ZERO-VERTEX-COUNT CONTROL. See the header for what a selected launch does,
 * why an issued empty call is a different measurement from an omitted one, and
 * why the slot it keys is marked unwritten. The answer is decided by the arbiter
 * above, together with the three controls it is exclusive with, so a launch
 * cannot be two arms at once. */
int sl_bgtf_zero_count(void)
{
    tf_decide_controls();
    return g_zc;
}

/* THE EMPTY-DRAW STATE MODE. See the header for the rows, why no row pairs an
 * active feedback scope with a program switch, and what each arm narrows to the
 * empty draw. The answer is decided by the arbiter above, together with the arm
 * it refines and the three controls it is exclusive with. */
int sl_bgtf_empty_state(void)
{
    tf_decide_controls();
    return g_es;
}

/* THE POST-RESET BOUNDARY CONTROL. See the header for where the boundary is,
 * what each arm issues there, the fixed order on the arm that issues both, why
 * it is independent of the zero-count arm and of every empty-draw row, and why
 * each of the four older controls refuses it. The answer is decided by the
 * arbiter above, together with those four, so a launch cannot be two
 * experiments at once. */
int sl_bgtf_post_reset(void)
{
    tf_decide_controls();
    return g_pr;
}

/* THE NEUTRAL-PROGRAM EMPTY-DRAW CONTROL. See the header for what a selected
 * launch issues, which measurements leave the question it asks open, and why
 * each of the six older controls and the two diagnostics refuse it. The answer
 * is decided by the arbiter above, together with the arm it refines and all of
 * them, so a launch cannot be two experiments at once. */
int sl_bgtf_neutral_prog(void)
{
    tf_decide_controls();
    return g_np;
}

/* THE AUXILIARY-CONTEXT CONTROL'S SWITCH (SL_BG_TF_AUX_EMPTY), parsed once and
 * decided by the arbiter above together with the arm it refines and every
 * control it is exclusive with, so a launch cannot be two experiments at once.
 * Pure: no GL call, no context creation, no state of its own. */
int sl_bgtf_aux_empty(void)
{
    tf_decide_controls();
    return g_ax;
}

/* THE AUXILIARY-CONTEXT REAL-PREPASS MODE'S SELECTOR (SL_BG_TF_AUX_REAL),
 * parsed once and decided by the arbiter above together with every control it is
 * exclusive with and with the raster source its arm names, so a launch cannot be
 * two experiments at once. Pure: no GL call, no context creation, no state of
 * its own. */
int sl_bgtf_aux_real(void)
{
    tf_decide_controls();
    return g_ar;
}

/* HOW THAT ARM WAS CHOSEN, which the arm token itself cannot say. 1 for the
 * opt-in candidate - the clip prepass selected with no arm named - and 0 for an
 * arm the launch spelled and for every launch with no arm in force. The two run
 * the same commands, so this is the only thing that distinguishes a census of
 * the candidate from a census of an A/B arm somebody set up. Pure: the arbiter
 * it consults is the same one-shot decision every other accessor here uses. */
int sl_bgtf_ar_implicit(void)
{
    tf_decide_controls();
    return g_ar_implicit;
}

/* WHETHER THE PREPASS HAS FAILED CLOSED FOR AN UNREADABLE SELECTOR. 1 only on a
 * launch that opted into the clip prepass and then offered a value one of the
 * two selectors' grammars cannot read - the arm (SL_BG_TF_AUX_REAL), or the
 * synchronisation mode (SL_BG_TF_AUX_SYNC) with an arm in force. On such a
 * launch every served draw is refused to the existing CPU raster path and no
 * prepass is issued on either context. 0 everywhere else, the explicit off
 * tokens included - those are a selection of the main-context diagnostic and not
 * a failure. */
int sl_bgtf_ar_optin_closed(void)
{
    tf_decide_controls();
    return g_ar_closed;
}

/* ======================================================================== *
 * v0.4 THE PRODUCTION SELECTION: WHAT THE OPTIMIZED RENDERING ROW REACHES.
 *
 * WHAT IS PROMOTED HERE, AND IT IS NOT A NEW PATH. The arm, the synchronisation
 * mode and every command issued under them are the auxiliary-context real
 * prepass on arm GPU_SOURCE with FENCE - the same arm, the same decoder, the
 * same bracket and the same refusals a developer reaches by naming
 * SL_BG_TF_AUX_REAL=GPU_SOURCE with SL_BG_TF_AUX_SYNC=FENCE. What this function
 * adds is the ROUTE: a launch that named nothing, whose row admitted a walk,
 * takes that arm instead of the background pass's vertex program.
 *
 * WHY THAT ARM AND NOT THE MAIN-CONTEXT PREPASS OR THE VERTEX PROGRAM. On the
 * Runway, Frigate and Facility viewpoints recorded for this selection, including
 * the 1920x1080 witnesses, the auxiliary GPU_SOURCE arm's frames were BYTE
 * EXACT against the original renderer's, while the MAIN-CONTEXT prepass and the
 * vertex program read one blue value apart from it at (71,113), 142 against 143.
 * Exactness is what this selection rests on and it is the whole claim: no frame
 * time follows from it. The relative timing screen recorded for it passed, and
 * both the original renderer and this arm MISSED the absolute p99 target that
 * screen was taken beside, so NO FRAME-RATE PROMISE OF ANY KIND is attached to
 * the row's ON position. See docs/backlog.md B-OPTROW.
 *
 * AND IT IS LAZY FOR A REASON THE INACTIVE PATHS DEPEND ON. Nothing here runs
 * until a walk the row admitted asks, so a run whose settings store is inactive
 * - trace replay, headless health, the matching build - reaches no promotion, no
 * synchronisation decision, no fail-closed latch and no line: the row answers
 * OFF there before it reads anything (src/platform/sl_perf.h), the caller's own
 * per-walk answer is therefore 0, and this function is never called at all.
 *
 * THE TWO OUTCOMES, AND NEITHER IS THE MAIN-CONTEXT ARM:
 *
 *   THE ARM IS IN FORCE. The synchronisation mode is decided from the same key
 *   by the same grammar, with the implicit default - FENCE - because that is the
 *   half of the combination the exact-image captures were taken under and the
 *   half that does not spend two pipeline drains per served mesh. The selection
 *   is stated once on stderr, so a reader never has to infer which prepass
 *   produced the frames in front of them.
 *   AN UNREADABLE SYNCHRONISATION VALUE FAILS IT CLOSED. The arm stands down
 *   with the one refusal both selections share, the latch is raised, and every
 *   served draw is refused to the EXISTING CPU clip and raster path - the fully
 *   produced original positions, not the main-context prepass and not the vertex
 *   program. The walk is still ADMITTED, which is what routes those draws
 *   through that refusal rather than leaving the choice to a second predicate.
 *
 * ONCE PER LAUNCH, not once per walk: every term is an environment answer or a
 * decision over one, and the environment cannot change inside a launch. The ROW
 * is not a term of it - the caller asks the row per walk and only calls this
 * behind that answer - so nothing here caches what a player can move. */
static int tf_prod_arm(void)
{
    int fx_named = 0;

    if (g_prod_armed >= 0) return g_prod_armed;
    tf_decide_controls();
    if (g_prod_elig != 1) {
        g_prod_armed = 0;
        return 0;
    }
    /* THE WALK IS ADMITTED EITHER WAY, which is what makes the refusal below a
     * refusal to the CPU clip path rather than a second selection. */
    g_prod_armed = 1;
    if (tf_fx_decide(1, &fx_named) < 0) {
        tf_ar_sync_standdown();
        return 1;        /* admitted, and every served draw refuses to the CPU */
    }
    /* AND IT IS RECORDED AS THE PRODUCTION SELECTION beside the implicit
     * record, which the arm token and that record together still cannot say: a
     * developer naming no arm reaches the identical arm by the identical
     * commands, and the difference between the two is which of them a reader is
     * looking at. */
    g_ar = SL_BGTF_AR_GPU;
    g_ar_implicit = 1;
    g_prod_said = 1;
    g_c.ar_mode = (unsigned long) SL_BGTF_AR_GPU;
    fprintf(stderr, "#95 the background GPU clip prepass is the "
                    "PRODUCTION selection of the OPTIMIZED RENDERING row: "
                    "this walk was "
                    "admitted by the row, no selector named an arm, so the "
                    "EFFECTIVE mode is the auxiliary-context real prepass on "
                    "arm GPU_SOURCE with %s synchronisation. That arm's frames "
                    "were measured BYTE EXACT against the original renderer's "
                    "on the viewpoints recorded for this selection; the "
                    "MAIN-CONTEXT prepass and the background vertex program, "
                    "each measured one blue value apart there, are NOT selected "
                    "and the main-context prepass is reachable only by name "
                    "with SL_BG_TF_CLIP=1. EXACTNESS IS THE WHOLE CLAIM: NO "
                    "FRAME RATE AND NO FRAME TIME IS PROMISED BY THE ROW. "
                    "EVERY LATER LINE ABOUT THIS MODE IS SPELLED IN THE "
                    "SELECTORS' NAMES - SL_BG_TF_AUX_REAL=GPU_SOURCE with "
                    "SL_BG_TF_AUX_SYNC=%s - because that is the mode in force, "
                    "and this line is what records how it was reached\n",
            sl_bgtf_fx_name(g_fx), sl_bgtf_fx_name(g_fx));
    return 1;
}

/* DOES THIS WALK TAKE THE PREPASS? ASKED PER WALK, AND THE ROW IS THE CALLER'S.
 *
 * THE TWO ROUTES, IN THE ORDER THEY ARE ASKED:
 *
 *   THE DEVELOPER SWITCH. SL_BG_TF_CLIP=1 admits every eligible walk whatever
 *   the row says, exactly as it did when it was the only route, and every
 *   main-context diagnostic and every A/B arm stays reachable behind it.
 *   THE PRODUCTION SELECTION. With the switch not named, the OPTIMIZED
 *   RENDERING row admits the walk and this answers for the arm - see
 *   tf_prod_arm. A developer naming SL_BG_TF_CLIP=0, naming an arm, naming an
 *   OFF token, naming a main-context diagnostic or arming either capture takes
 *   the production selection out of the launch, and such a walk draws through
 *   the fully produced CPU clip and raster path. WHERE IT ACTUALLY GOES IS THE
 *   CALLER'S CHAIN: src/gfx/sl_gfx_dl.c draws the CPU clip positions unless a
 *   developer has explicitly named the background vertex transform, which is a
 *   labelled diagnostic and itself one blue value apart. No absence, default or
 *   unreadable value reaches either differing arm.
 *
 * THE ROW IS NOT READ HERE, AND THAT IS THE POINT. src/gfx/sl_gfx_dl.c latches
 * one answer from sl_bggpu_opt_in() per walk and calls this inside it, so the
 * row is resolved ONCE PER WALK and never cached for the launch: a menu moving
 * OFF -> ON -> OFF acts at the next walk boundary in each direction. Reading the
 * row a second time here would give a walk two answers that a press between them
 * could make disagree, which is the one thing a walk's selection may not do -
 * the reduction, the admission and the raster all have to be about the same
 * answer. There is no second reader and no cached copy of the row in this
 * module.
 *
 * AND OFF COSTS NOTHING EXTRA. With the row off the caller never calls this, so
 * no arm is promoted, no auxiliary GL context is created, no synchronisation
 * object exists, no entry point is resolved and not one GL call of this module's
 * is issued - the pass is the one that ships. */
int sl_bgtf_walk_admit(void)
{
    if (sl_bgtf_opt_in() == 1) return 1;
    return tf_prod_arm();
}

/* IS THE PREPASS IN THIS LAUNCH BY THE ROW RATHER THAN BY THE SWITCH?
 *
 * 1 once a walk the row admitted has promoted the arm, 0 on a developer-switch
 * launch, on a launch whose environment took the production selection away, and
 * on every launch where no walk has been admitted yet.
 *
 * PURE: IT DECIDES NOTHING AND PROMOTES NOTHING. A census reads it, and a census
 * runs on launches that drew nothing - so asking it must not be what arms a
 * selection or prints a line. That is why it reads the promotion rather than
 * taking it, and why a run that admitted no walk reads 0 here and says the
 * prepass was off, which is what happened. */
int sl_bgtf_production(void)
{
    return g_prod_said == 1;
}

/* THE NEUTRAL PROGRAM, CREATED ONCE PER CONTEXT AND FAILING CLOSED.
 *
 * 1 when `g_np_prog` is a linked program this control may bind, 0 when it is
 * not - and 0 is a REFUSAL of the served draw, never a fallback. The caller
 * returns -1 without issuing anything, which leaves the mesh on the CPU path
 * exactly as any other prepass refusal does. The one thing this must never do is
 * answer 0 and let the draw proceed: that draw would be issued under the prepass
 * program, which is SL_BG_TF_EMPTY_STATE's row A, and the run would be recorded
 * as a neutral one without being one.
 *
 * LAZY, AND ONLY ON A LAUNCH THAT SELECTED THE CONTROL. Nothing here runs for an
 * off launch: no entry point is resolved, no shader is compiled and no object is
 * created, so the default path's resolved set and call sequence are what they are
 * without this control existing. The capability is asked at the first served draw
 * rather than in sl_bgtf_ready for the same reason - that function runs on every
 * launch with SL_BG_TF_CLIP=1 and must not compile a second program for one that
 * will never use it.
 *
 * THE FAILURE LATCHES AND IS NAMED ONCE. A context that cannot make the program
 * refuses every later served draw without compiling anything again, which is the
 * same reason the buffer-creation failure above latches: one exhausted context
 * otherwise pays a compile per mesh for the rest of the frame while the picture
 * is the CPU path's either way.
 *
 * THE ENTRY POINTS ARE THE ONES sl_bgtf_ready ALREADY RESOLVED, with the two
 * exceptions of glDeleteProgram and glDeleteShader, which are resolved here and
 * BOTH required BEFORE any object is created. That order is what makes "no path
 * leaves an object this control cannot release" a property of the sequence
 * rather than a promise: two objects are made here, a shader and a program, so
 * both releases have to be in hand before the first create. No
 * glTransformFeedbackVaryings call is made for this program, which is the whole
 * difference between it and the prepass program.
 *
 * EVERY EXIT AFTER THE SHADER EXISTS RELEASES IT, success included. The shader
 * is a local object with one purpose - to be linked into this program - and
 * nothing outside this function ever names it again, so each of the five exits
 * below issues exactly one glDeleteShader for it: the three failure exits free
 * it outright, and the success exit FLAGS it, because it is attached and the
 * linked program keeps it alive until the program itself is deleted. One create,
 * one delete, on every path.
 *
 * A FAILED LINK IS DELETED HERE AND NOT LATER. The context is current during an
 * acquire, which is the only instant a delete is legal, and the name is this
 * function's only reference to the object: dropping it without deleting it
 * leaks the program for the life of the context. So the program is deleted
 * before the name is cleared, and sl_bgtf_shutdown then sees no program and
 * deletes nothing twice. */
static int tf_np_acquire(void)
{
    GLuint vs;
    GLint ok = 0;

    if (g_np_state != 0) return g_np_state > 0;
    /* The prepass program's own capability is a precondition rather than a
     * coincidence: the sixteen GL 2.0 entry points below are resolved by
     * sl_bgtf_ready, and this control's restore binds `g_prog` itself. */
    if (g_state <= 0 || g_prog == 0u) {
        g_np_state = -1;
        return 0;
    }
    if (p_DeleteProgram == NULL)
        p_DeleteProgram = (SL_TF_DELETEPROGRAM) sl_gl_proc("glDeleteProgram");
    if (p_DeleteShader == NULL)
        p_DeleteShader = (SL_TF_DELETESHADER) sl_gl_proc("glDeleteShader");
    /* BOTH RELEASES, BEFORE THE FIRST CREATE. Asked as one condition because
     * the answer is the same either way: an acquire that cannot release both of
     * the objects it would make creates neither. */
    if (p_DeleteProgram == NULL || p_DeleteShader == NULL) {
        const char *missing = (p_DeleteProgram == NULL) ? "glDeleteProgram"
                                                        : "glDeleteShader";
        g_np_state = -1;
        if (!g_np_said) {
            g_np_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1: %s did "
                            "not resolve, so the neutral program or its shader "
                            "would be an object this control could never "
                            "release. Nothing "
                            "was created, the served draw is REFUSED to the CPU "
                            "path rather than issued under the prepass program, "
                            "and this is reported once per launch\n", missing);
        }
        return 0;
    }
    vs = p_CreateShader(GL_VERTEX_SHADER);
    if (vs == 0) {
        g_np_state = -1;
        if (!g_np_said) {
            g_np_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1: glCreateShader gave "
                            "no object for the neutral program's vertex shader. "
                            "The served draw is REFUSED to the CPU path rather "
                            "than issued under the prepass program, and this is "
                            "reported once per launch\n");
        }
        return 0;
    }
    p_ShaderSource(vs, 1, &TF_NP_VS, NULL);
    p_CompileShader(vs);
    p_GetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetShaderInfoLog(vs, (GLsizei) sizeof log, &n, log);
        /* THE OBJECT IS FREED BEFORE THE REFUSAL. It is attached to nothing, so
         * this release is immediate rather than a flag. */
        p_DeleteShader(vs);
        g_c.np_shader_deletes++;
        g_np_state = -1;
        if (!g_np_said) {
            g_np_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1: the neutral program's "
                            "vertex shader did not compile: %s. The served draw "
                            "is REFUSED to the CPU path rather than issued under "
                            "the prepass program, and this is reported once per "
                            "launch\n", log);
        }
        return 0;
    }
    g_np_prog = p_CreateProgram();
    if (g_np_prog == 0u) {
        /* NO PROGRAM TO HOLD IT, so the shader is freed outright here too. */
        p_DeleteShader(vs);
        g_c.np_shader_deletes++;
        g_np_state = -1;
        if (!g_np_said) {
            g_np_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1: glCreateProgram gave "
                            "no object for the neutral program. The served draw "
                            "is REFUSED to the CPU path rather than issued under "
                            "the prepass program, and this is reported once per "
                            "launch\n");
        }
        return 0;
    }
    p_AttachShader(g_np_prog, vs);
    /* AND NO OUTPUT VARYING IS RECORDED. glTransformFeedbackVaryings takes
     * effect at link time, so NOT calling it before this link is what makes this
     * a program that captures nothing - which is the one declared difference
     * from the prepass program and the whole content of the comparison. */
    p_LinkProgram(g_np_prog);
    ok = 0;
    p_GetProgramiv(g_np_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetProgramInfoLog(g_np_prog, (GLsizei) sizeof log, &n, log);
        /* BOTH OBJECTS ARE RELEASED BEFORE THE NAME IS DROPPED. A failed link
         * still leaves a real program object, and this name is the only
         * reference to it: clearing the name first would leak it for the life
         * of the context. The context is current throughout an acquire, which
         * is the only instant a delete is legal, so the delete belongs HERE and
         * not in sl_bgtf_shutdown - which, with the name cleared below, then
         * sees no program and deletes nothing twice. The shader is detached by
         * the program's own deletion, so flagging it is enough; the order of the
         * two deletes does not matter for that reason, and the program is
         * deleted first so that nothing holds a flagged shader afterwards. */
        p_DeleteProgram(g_np_prog);
        g_c.np_fail_prog_deletes++;
        p_DeleteShader(vs);
        g_c.np_shader_deletes++;
        g_np_prog = 0u;
        g_np_state = -1;
        if (!g_np_said) {
            g_np_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_NEUTRAL_PROG=1: the neutral program "
                            "did not link: %s. The served draw is REFUSED to "
                            "the CPU path rather than issued under the prepass "
                            "program, and this is reported once per launch\n",
                    log);
        }
        return 0;
    }
    /* AND THE SUCCESSFUL PATH RELEASES IT TOO. The shader is attached, so this
     * FLAGS it rather than freeing it: the linked program is unaffected and
     * keeps the object alive, and the object goes away with the program at
     * teardown. Without this call the shader would outlive every use of it and
     * be released only by the context's destruction, which is a leak for the
     * life of the context and the one the counters below make observable. */
    p_DeleteShader(vs);
    g_c.np_shader_deletes++;
    g_np_state = 1;
    g_c.np_creates++;
    return 1;
}

/* THE EXIT STATUS A TERMINATED LAUNCH CARRIES. The diagnostic's own census
 * number, so a launch that ended this way is identifiable from a shell's exit
 * status without reading its output. */
#define SL_BGTF_AUX_EXIT 95

/* WHICH SELECTOR A SHARED-ACQUIRE REFUSAL BELONGS TO (SL_BG_TF_AUX_EMPTY /
 * SL_BG_TF_AUX_REAL).
 *
 * THE PROBLEM THIS SOLVES. tf_ax_acquire does the probe, the create, the
 * auxiliary-context resolve and the share check, and the real-prepass mode
 * REUSES all four rather than repeating them - which is what keeps the
 * empty-draw control's own call sequence unchanged by the newer mode existing.
 * The cost is that its five refusal lines were written for one selector, so a
 * launch that set SL_BG_TF_AUX_REAL and hit one of those shapes was told that
 * SL_BG_TF_AUX_EMPTY=1 had failed - naming a control the launch never set, which
 * makes a live refusal unattributable.
 *
 * `g_ax_for_ar` IS WHO IS ASKING, not what is in force. It is raised around the
 * one call into tf_ax_acquire from the real-prepass mode's own acquire and
 * cleared immediately after, so it is 1 for exactly the window in which that
 * function's refusals belong to the newer selector. The two modes are mutually
 * exclusive - the arbiter refuses the newer one when the older is requested - so
 * the flag never has to describe a launch that set both.
 *
 * THE OLDER WORDING IS BYTE-IDENTICAL, which is a requirement and not a
 * courtesy: earlier rounds' frozen evidence quotes those lines, and the
 * substituted text for an off launch is exactly the string that was there
 * before. Each answer is a fixed literal rather than a formatted buffer: there
 * are two arms, the token is part of the selector's grammar, and a literal
 * cannot truncate or run out of room.
 *
 * THE TERMINAL FUNNEL USES THE SAME ANSWERS, through the mode flag below rather
 * than through this one. tf_ax_lost is not a refusal - it is the one
 * process-wide statement that nothing this launch reports means anything, and it
 * is reached from both modes' sites and from the backend's answers - but a
 * reader of a terminated launch needs the same attribution a refused one gets,
 * and more urgently: that outcome is the strongest thing either mode can report
 * and naming the wrong selector in it points an investigation at a control the
 * launch never set. The OLDER wording is unmoved on its own mode, which is what
 * the earlier rounds' runners match on, because the literal they match is this
 * function's own first answer and not a second copy of it. */
static int g_ax_for_ar;

static const char *tf_ax_sel(void)
{
    if (!g_ax_for_ar)           return "SL_BG_TF_AUX_EMPTY=1";
    if (g_ar == SL_BGTF_AR_CPU) return "SL_BG_TF_AUX_REAL=CPU_SOURCE";
    if (g_ar == SL_BGTF_AR_GPU) return "SL_BG_TF_AUX_REAL=GPU_SOURCE";
    return "SL_BG_TF_AUX_REAL=?";
}

/* WHETHER THE REAL-PREPASS MODE IS IN FORCE ON THIS LAUNCH, as the arbiter left
 * it rather than as it was requested: `g_ar` holds the EFFECTIVE arm, forced to
 * SL_BGTF_AR_OFF by every exclusion - the empty-draw control's own request among
 * them - so a nonzero answer here means this launch is running this mode and no
 * other that shares the auxiliary context. It is -1 until the selectors are
 * parsed, which no site that can terminate is reachable before. */
static int tf_ar_in_force(void)
{
    return g_ar == SL_BGTF_AR_CPU || g_ar == SL_BGTF_AR_GPU;
}

/* THE SYNCHRONISATION MODE THIS LAUNCH SELECTED, ANSWERED FROM THE ONE DECISION
 * (SL_BG_TF_AUX_SYNC). The key is read, parsed and decided by tf_fx_decide, with
 * the arm and once per launch; this reads that answer and nothing else.
 *
 * WHY THE READ IS NOT HERE ANY MORE. Two properties needed the decision to be
 * taken with the arm rather than at the first site that asked for it: the arm's
 * announcement has to be able to NAME the mode in force, and an unreadable value
 * has to refuse the arm before any bracket has written GL state. A lazy read
 * could give neither, and a second reader of the same key could disagree with
 * the first. See tf_fx_decide for the grammar and for what each value answers.
 *
 * THE GUARD IS WHAT KEEPS THE DEFAULT PATH UNTOUCHED, and it is now the only
 * thing this function does besides reading the decision: FINISH without touching
 * the environment when no arm is in force. An off launch performs no getenv for
 * this key, prints nothing about it and resolves nothing for it. The second
 * clause cannot be reached with an arm in force - the arbiter decides both
 * together - and answers the off launch's own value if it ever were. */
static int tf_fx_requested(void)
{
    if (!tf_ar_in_force()) return SL_BGTF_FX_FINISH;
    if (g_fx < 0) return SL_BGTF_FX_FINISH;
    return g_fx;
}

/* AND WHETHER THE FENCE PATH IS ACTUALLY IN FORCE. Both halves are required: the
 * fence path was selected for the arm in force (named by SL_BG_TF_AUX_SYNC=FENCE,
 * or the opt-in candidate's own default) AND the capability answered. Every
 * site that may issue a fence command asks this one function, so there is no
 * path on which a half-established fence mode reaches a call. */
static int tf_fx_in_force(void)
{
    return g_fx_ok && tf_fx_requested() == SL_BGTF_FX_FENCE;
}

int sl_bgtf_ar_sync(void)
{
    return tf_fx_in_force() ? SL_BGTF_FX_FENCE : SL_BGTF_FX_FINISH;
}

/* THE LOST-CONTEXT OUTCOME, IN ONE PLACE: SAY IT, LATCH IT, END THE PROCESS.
 *
 * WHAT MAKES THIS DIFFERENT FROM EVERY OTHER FAILURE IN THIS FILE. The others
 * are refusals: one served draw goes to the CPU path, the caller rasters it, and
 * the run continues with its numbers intact. This one is not a refusal because
 * there is nothing left to refuse FROM. Once which context a GL command reaches
 * is unknown, the caller's own raster draw, every later frame, the window
 * teardown and the exit handlers this build registers are all GL commands with
 * an unknown destination - and none of them is reachable from a latch in this
 * file. So the process ends here, at the point of loss.
 *
 * THE REPORT IS UNCONDITIONAL, unlike every other message this control prints.
 * The others are once per launch so a scene's worth of refusals cannot bury a
 * run's output; this one happens at most once per process by construction, and a
 * reader who sees a window from such a launch has to be told that what is in it
 * is not a result - so an earlier recoverable refusal must not be able to
 * suppress it.
 *
 * AND IT RETURNS IN A FIXTURE, which is the only reason the statements after the
 * call to it exist. The terminate is a backend seam; a fixture supplies its own
 * and records that it was reached, and the caller's refusal path then runs so
 * that the properties of the refusal - nothing issued, nothing destroyed, every
 * later draw refused - stay measurable. In the product it does not return. */
static void tf_ax_lost(const char *what)
{
    g_ax_lost  = 1;
    g_ax_state = -1;
    g_ax_said  = 1;
    g_c.ax_lost++;
    g_c.ax_fatals++;
    /* WHOSE LAUNCH THIS WAS, in the one line that says the launch means nothing.
     * The flag is set from the EFFECTIVE mode rather than from whoever happens
     * to be on the call stack, because this funnel is reached from the bracket,
     * from the acquire, from the release and from the backend's own answers - so
     * "who is asking" is not available at every one of them, while "which mode
     * is in force" is a property of the launch and is. It is set and not
     * restored: in the product the call below does not return, and in a fixture
     * that intercepts it the capability has latched by then anyway. */
    g_ax_for_ar = tf_ar_in_force();
    fprintf(stderr, "#95 %s: THE DIAGNOSTIC IS INVALID. %s, "
                    "so this process can no longer say which GL context a "
                    "command would reach or how a later context would be "
                    "created. "
                    "Nothing this launch reports about any pixel means "
                    "anything, and a picture that looks like the original path "
                    "is NOT a result. THIS PROCESS IS BEING TERMINATED NOW, "
                    "without running any exit handler and without any further "
                    "GL command of any kind - not this control's, not the "
                    "bracket's restoration, not the caller's raster draw and "
                    "not the window's teardown\n", tf_ax_sel(), what);
    sl_gl_terminate_no_gl(SL_BGTF_AUX_EXIT);
}

/* RELEASE THE AUXILIARY CONTEXT AND ACCOUNT FOR IT. 1 there is none left - it
 * was destroyed, or there was none to destroy; 0 the backend could not verify
 * that destroying was safe, which is the unknown outcome and has already ended
 * the process in the product.
 *
 * `g_ax_live` IS NOT CLEARED ON THE UNKNOWN OUTCOME. It names a resource the
 * platform still holds, and the whole point of that outcome is that this process
 * may not call into the platform to find out. */
static int tf_ax_release(void)
{
    const int d = sl_gl_aux_destroy();

    if (d == SL_BGTF_AUX_LOST) {
        tf_ax_lost("the auxiliary GL context could not be destroyed because the "
                   "MAIN context is not verifiably current");
        return 0;
    }
    if (d == 1) g_c.ax_destroys++;
    g_ax_live = 0;
    return 1;
}

/* THE AUXILIARY CONTEXT, ESTABLISHED ONCE PER MAIN CONTEXT AND FAILING CLOSED.
 *
 * 1 when a second, OBJECT-SHARING context exists, has been VERIFIED to share the
 * prepass program's name, and the MAIN context is current; 0 otherwise - and 0 is
 * a REFUSAL of the served draw, never a fallback. The caller returns -1 without
 * issuing anything, which leaves the mesh on the CPU path exactly as any other
 * prepass refusal does. The one thing this must never do is answer 0 and let the
 * draw proceed on the main context: that is the ordinary zero-count arm, and the
 * run would be recorded as an auxiliary-context one without being one.
 *
 * LAZY, AND ONLY ON A LAUNCH THAT SELECTED THE CONTROL. Nothing here runs for an
 * off launch: no entry point is resolved and no context is created, so the
 * default path's resolved set, its call sequence and the number of GL contexts
 * this process owns are what they are without this control existing. The
 * capability is asked at the first served draw rather than in sl_bgtf_ready for
 * the same reason the neutral program's is - that function runs on every launch
 * with SL_BG_TF_CLIP=1 and must not create a GL context for one that will never
 * use it.
 *
 * THE ORDER IS THE PROPERTY, in two steps rather than one. glIsProgram is
 * PROBED on the main context before anything is created, so no context is
 * created that this build's GL could never be asked about at all; then the
 * context is created, which the backend leaves with the MAIN context verified
 * current; then the auxiliary context is made current and ITS OWN entry points
 * are resolved there, because a pointer from one context is not valid on another
 * - see p_ax_IsProgram; then the share check is asked through the pointer that
 * context handed back; then the switch back. A failure at any step refuses,
 * latches, and is named once per launch.
 *
 * AND THE SHARE CHECK IS A CHECK, NOT AN ASSUMPTION. A create that succeeded is
 * not evidence that the platform honoured the sharing request: an unshared
 * context would answer FALSE for the prepass program's name, and binding that
 * name there would bind something else or nothing. So a FALSE answer refuses the
 * draw and the context is destroyed on the spot, while the main context is
 * verified current and the window is alive - the only instant at which that is
 * safe.
 *
 * THE ONE FAILURE THAT DOES NOT CLEAN UP is a lost main context. When the
 * backend answers that which context is current is UNKNOWN, this process cannot
 * prove which context a later call would reach, so this function makes no
 * further call of any kind: no query, no destroy, no restoration. It raises
 * ax_lost, reports the run INVALID, and ENDS THE PROCESS - because a refusal
 * would only redirect one mesh while every later frame, the teardown and the
 * exit handlers would go on issuing GL. */
static int tf_ax_acquire(void)
{
    int r;
    GLboolean shared;

    /* THE PROCESS LATCH FIRST, before even the per-context cache: once the main
     * context is lost, no context's capability means anything. */
    if (g_ax_lost) return 0;
    if (g_ax_state != 0) return g_ax_state > 0;
    /* The prepass program's own capability is a precondition rather than a
     * coincidence: its NAME is what the share check asks about and what the
     * draw on the auxiliary context binds. */
    if (g_state <= 0 || g_prog == 0u) {
        g_ax_state = -1;
        return 0;
    }
    if (g_ax_probe == NULL)
        g_ax_probe = sl_gl_proc("glIsProgram");
    /* THE PROBE, BEFORE THE FIRST CREATE. A context this control could not ask
     * about is a context it must not use, so it does not make one. The address
     * this answers with is the MAIN context's and is never invoked - it is kept
     * as void * so that it cannot be; the pointer the query is actually asked
     * through is resolved on the auxiliary context below. */
    if (g_ax_probe == NULL) {
        g_ax_state = -1;
        if (!g_ax_said) {
            g_ax_said = 1;
            fprintf(stderr, "#95 %s: glIsProgram did not "
                            "resolve, so whether a second context shares this "
                            "one's object names could never be established. NO "
                            "AUXILIARY GL CONTEXT WAS CREATED, the served draw "
                            "is REFUSED to the CPU path rather than issued on "
                            "the main context - which would be the ordinary "
                            "zero-count arm - and this is reported once per "
                            "launch\n", tf_ax_sel());
        }
        return 0;
    }
    r = sl_gl_aux_create();
    if (r == 0) {
        /* NOTHING WAS CREATED AND THE MAIN CONTEXT IS UNTOUCHED. The backend
         * has already said why on stderr; this is the control's own account of
         * the refusal. */
        g_ax_state = -1;
        if (!g_ax_said) {
            g_ax_said = 1;
            fprintf(stderr, "#95 %s: no auxiliary GL context "
                            "could be created on this backend. The main context "
                            "is current and unchanged, the served draw is "
                            "REFUSED to the CPU path rather than issued on it, "
                            "and this is reported once per launch\n",
                    tf_ax_sel());
        }
        return 0;
    }
    if (r == SL_BGTF_AUX_LOST) {
        /* WHICH CONTEXT IS CURRENT IS UNKNOWN. The context - if one was made at
         * all - is deliberately NOT destroyed and nothing further is called: a
         * destroy requires a verified main context, which is exactly what could
         * not be established. `g_ax_live` is raised because the backend may hold
         * a context this process can no longer release, and nothing is called to
         * find out which. */
        g_ax_live = 1;
        tf_ax_lost("an auxiliary GL context was requested and the backend could "
                   "not establish afterwards either that the MAIN context is "
                   "current or that the process-wide object-sharing attribute "
                   "still holds the value it had - its own line above says "
                   "which");
        return 0;
    }
    g_ax_live = 1;
    /* THE SHARE CHECK, AS ONE SWITCH, ONE RESOLVE, ONE QUERY AND ONE SWITCH
     * BACK - the resolve being this round's addition and the reason it is here
     * rather than above the create: a pointer is only valid on the context it
     * was resolved on, so it cannot be obtained before that context is
     * current. */
    r = sl_gl_aux_make_current();
    if (r == SL_BGTF_AUX_LOST) {
        /* THE SWITCH ESTABLISHED NEITHER CONTEXT. Nothing is destroyed and
         * nothing is issued, for the reason just above. */
        tf_ax_lost("the switch to the auxiliary GL context left neither it nor "
                   "the MAIN context verifiably current");
        return 0;
    }
    if (r == 0) {
        /* THE MAIN CONTEXT IS VERIFIED CURRENT - that is what this failure means
         * - so the context can be, and is, destroyed here. */
        g_c.ax_switch_fails++;
        g_ax_state = -1;
        if (!tf_ax_release()) return 0;
        if (!g_ax_said) {
            g_ax_said = 1;
            fprintf(stderr, "#95 %s: the auxiliary GL context "
                            "was created and could not be made current, so it "
                            "was destroyed again while the main context was "
                            "still current. The served draw is REFUSED to the "
                            "CPU path rather than issued on the main context, "
                            "and this is reported once per launch\n",
                    tf_ax_sel());
        }
        return 0;
    }
    /* THE VERIFICATION ROUND TRIP IS DELIBERATELY NOT COUNTED in ax_switches
     * and ax_restores. Those two are PER-BRACKET counts, equal to ax_draws on an
     * honest run, and that equality is what a reader checks; folding this
     * one-off into them would make every run read as one switch too many and
     * would break the one invariant the arm has in place of zc_calls. That this
     * round trip happened is already recorded, by ax_creates - a verified
     * context cannot exist without it. Its FAILURES are counted, under
     * ax_switch_fails and ax_lost, because a failure is not a one-off fact about
     * a context but the reason a served draw was refused. */
    /* THE AUXILIARY CONTEXT'S OWN ENTRY POINTS, RESOLVED ON IT AND USED ONLY ON
     * IT. This is the first instant at which that is possible - the resolver
     * answers for whatever context is current - and the pointers are cached for
     * this context's lifetime, so the per-mesh bracket below resolves nothing.
     * Both are resolved here rather than one here and one at the draw, because a
     * context that can answer for one and not the other is a context this
     * control cannot use, and finding that out at the draw would mean finding it
     * out with bracket state already written. */
    p_ax_IsProgram  = (SL_TF_ISPROGRAM)  sl_gl_proc("glIsProgram");
    p_ax_UseProgram = (SL_TF_USEPROGRAM) sl_gl_proc("glUseProgram");
    if (p_ax_IsProgram == NULL || p_ax_UseProgram == NULL) {
        /* A MISSING POINTER IS A REFUSAL AND NEVER A FALL BACK TO THE MAIN
         * CONTEXT'S. The main set's addresses are documented as belonging to the
         * context they were resolved on, so invoking one here is exactly the
         * defect the separate set exists to remove - see p_ax_IsProgram. The
         * context is switched away from and destroyed, which needs the main
         * context verified current and so cannot be folded into one step. */
        g_c.ax_proc_fails++;
        g_ax_state = -1;
        p_ax_IsProgram  = NULL;
        p_ax_UseProgram = NULL;
        if (sl_gl_main_make_current() != 1) {
            tf_ax_lost("the auxiliary GL context could not resolve its own "
                       "entry points and the MAIN context could not be verified "
                       "current again");
            return 0;
        }
        if (!tf_ax_release()) return 0;
        if (!g_ax_said) {
            g_ax_said = 1;
            fprintf(stderr, "#95 %s: the auxiliary GL context "
                            "did not resolve its OWN glIsProgram and "
                            "glUseProgram. Entry-point addresses belong to the "
                            "context they were resolved on, so the main "
                            "context's are NOT used there and no draw is made "
                            "on an unverifiable one: the context was destroyed "
                            "again while the main context was verified current, "
                            "the served draw is REFUSED to the CPU path, and "
                            "this is reported once per launch\n", tf_ax_sel());
        }
        return 0;
    }
    shared = p_ax_IsProgram(g_prog);
    if (sl_gl_main_make_current() != 1) {
        /* THE SWITCH BACK FAILED. Whatever the query answered is irrelevant: the
         * process no longer knows which context it is on. Nothing further is
         * called, the context is left undestroyed, and the process ends. */
        tf_ax_lost("the share check was made on the auxiliary GL context and "
                   "the MAIN context could not be verified current again");
        return 0;
    }
    /* AND THE SWITCH BACK IS NOT COUNTED EITHER, for the reason just given. */
    if (!shared) {
        /* THE PLATFORM DID NOT HONOUR THE SHARING REQUEST. The prepass
         * program's name means nothing in that context, so a draw there would
         * bind something this control did not select. The main context is
         * verified current, so the destroy is safe and is done here - and the
         * pointers that context handed back go with it: they name entry points
         * in a context that is about to stop existing. */
        g_c.ax_share_fails++;
        g_ax_state = -1;
        p_ax_IsProgram  = NULL;
        p_ax_UseProgram = NULL;
        if (!tf_ax_release()) return 0;
        if (!g_ax_said) {
            g_ax_said = 1;
            fprintf(stderr, "#95 %s: the auxiliary GL context "
                            "does NOT share object names with the main one - "
                            "glIsProgram answered FALSE there for the prepass "
                            "program - so the name this control would bind means "
                            "nothing in it. The context was destroyed again, the "
                            "served draw is REFUSED to the CPU path rather than "
                            "issued on the main context, and this is reported "
                            "once per launch\n", tf_ax_sel());
        }
        return 0;
    }
    g_ax_state = 1;
    g_c.ax_creates++;
    return 1;
}

/* The diagnostic's own per-draw selection, cleared wherever the thing it names
 * stops existing. Declared here because the three lifetime hooks below are what
 * clear it; everything else about the diagnostic is at the end of this file. */
static int dump_clear_selection(void);

/* FORGET THE REAL-PREPASS MODE'S OWN AUXILIARY-CONTEXT STATE (SL_BG_TF_AUX_REAL)
 * WITHOUT CALLING ANYTHING. Every pointer here was resolved on a context that is
 * ending or has already stopped being usable, and the vertex array object is a
 * container object of that same context - so there is nothing to call and
 * nothing to delete: dropping the names is the whole of the release. Used by the
 * acquire's own refusals and by both lifetime hooks, so one place says what this
 * mode holds and no site can forget half of it. */
static void tf_ar_forget(void)
{
    p_ax_IsBuffer               = NULL;
    p_ax_BindBuffer             = NULL;
    p_ax_BindBufferBase         = NULL;
    p_ax_BeginTransformFeedback = NULL;
    p_ax_EndTransformFeedback   = NULL;
    p_ax_GenVertexArrays        = NULL;
    p_ax_BindVertexArray        = NULL;
    p_ax_Uniform1f              = NULL;
    p_ax_Uniform3f              = NULL;
    p_ax_Uniform1i              = NULL;
    p_ax_Uniform1fv             = NULL;
    /* AND THE FENCE PATH'S THREE ON THAT CONTEXT, with its capability
     * (SL_BG_TF_AUX_SYNC). `g_fx_ok` is the JOINT answer over both contexts'
     * sets, so losing one set is losing the capability: it is cleared here and
     * the next acquire re-establishes it or refuses. The MAIN context's three
     * are NOT dropped here, and that is the distinction this function's name
     * carries - tf_ar_leave reaches it with the main context alive and its
     * addresses still valid, so dropping them would be a statement about a
     * context that did not end. They are dropped where the main context's own
     * set is, in sl_bgtf_invalidate. */
    p_ax_FenceSync              = NULL;
    p_ax_WaitSync               = NULL;
    p_ax_DeleteSync             = NULL;
    g_fx_ok                     = 0;
    g_ar_vao                    = 0u;
}

/* THE BOUND ON ONE ERROR DRAIN (SL_BG_TF_AUX_REAL).
 *
 * OpenGL 4.6 section 2.3.1 says an implementation MAY hold several flag /
 * error-code pairs at once, that GetError returns the code for one of them and
 * CLEARS that one, and that GetError must therefore be called repeatedly until
 * it answers GL_NO_ERROR before the error state is known to be empty. At most
 * one flag can be set per distinct code, and that revision defines eight codes -
 * INVALID_ENUM, INVALID_VALUE, INVALID_OPERATION, STACK_OVERFLOW,
 * STACK_UNDERFLOW, OUT_OF_MEMORY, INVALID_FRAMEBUFFER_OPERATION and
 * CONTEXT_LOST - so eight reads retire every flag a conformant implementation
 * can be holding.
 *
 * THE BOUND IS NOT AN OPTIMISATION, IT IS THE TERMINATION ARGUMENT. A drain
 * written as "read until NO_ERROR" has its termination in the driver's hands,
 * and this mode's whole premise is that the driver on the other side of these
 * calls is the thing under examination. Eight is the number above; a ninth
 * nonzero answer means the implementation will not come back, and the loop stops
 * instead of spinning inside a frame. */
#define SL_BGTF_AR_ERR_CAP 8u

/* READ AND RETIRE THE CURRENT CONTEXT'S GL ERROR STATE, UNDER THAT BOUND
 * (SL_BG_TF_AUX_REAL). Answers how many error codes were retired; writes the
 * FIRST of them to *first, which is left 0 when there were none, and raises
 * *capped when the bound was reached with the state still not empty.
 *
 * `*capped` IS A FAILURE AND NEVER A CLEAN STATE. It says the error state could
 * not be proved empty, which is weaker than "there was an error" in form and not
 * in consequence: every caller treats it as one, because a bracket whose error
 * state is unknown cannot be the bracket that wrote a usable pool.
 *
 * IT IS CALLED ONLY WHILE THE AUXILIARY CONTEXT IS VERIFIED CURRENT, and that
 * placement is the whole of what makes it the AUXILIARY context's error state:
 * glGetError reads the error state of whatever context is current. This module
 * issues it from exactly two places under this mode, both inside the bracket's
 * switched-to region and both after a switch this process verified. THE MAIN
 * CONTEXT'S ERROR STATE IS NEVER READ BY THIS MODE and so is never CONSUMED by
 * it - which matters because a read clears what it returns, so a check on the
 * wrong context would silently swallow an error belonging to the ordinary
 * rendering path. The one other site in this file that reads the error state
 * belongs to a readback diagnostic the arbiter refuses alongside this mode, so
 * the two cannot be in force in one launch.
 *
 * THE CALL IS THE CORE EXPORT, DIRECTLY, on the discipline recorded at
 * p_ax_IsProgram: names after GL 1.1 are resolved on the context they are used
 * on, GL 1.0/1.1 core exports are called directly. glGetError is GL 1.0, and for
 * it the direct form is not merely allowed but required - a resolver that
 * answers only for extension functions hands back NULL on a perfectly capable
 * driver, and a NULL there would make the error state permanently UNOBSERVABLE,
 * which is the one outcome a check like this must not have. There is therefore
 * no per-context pointer for this name to get wrong, and which context the read
 * reaches is a property of the call's PLACE rather than of an address. */
static unsigned tf_ar_drain(unsigned long *first, int *capped)
{
    unsigned n = 0u;

    *first = 0ul;
    *capped = 0;
    for (;;) {
        GLenum e;
        if (n >= SL_BGTF_AR_ERR_CAP) {
            *capped = 1;
            break;
        }
        e = glGetError();
        if (e == GL_NO_ERROR) break;
        if (n == 0u) *first = (unsigned long) e;
        n++;
    }
    return n;
}

/* ---- THE FENCE PATH'S FOUR MOVES (SL_BG_TF_AUX_SYNC) --------------------
 *
 * WHY THESE ARE FUNCTIONS AND NOT INLINE BRANCHES. The one property the whole
 * mode rests on is that FINISH's call sequence is unchanged, and the cheapest way
 * to make that checkable is for the FINISH case to be a single early return in
 * one place per synchronisation point rather than a condition repeated at each.
 * Each function below is therefore the WHOLE of one handoff half, under either
 * mode, and the bracket calls it where the glFinish used to be.
 *
 * WHY THE PRODUCER FLUSHES EXPLICITLY, AND WHY GL_SYNC_FLUSH_COMMANDS_BIT IS NOT
 * USED. OpenGL 4.5 core section 4.1.2 describes that bit as an aid to
 * ClientWaitSync on the SAME context: it causes a flush of the context the wait
 * is issued on, equivalent to a glFlush, and the specification is explicit that
 * the bit is not a substitute for flushing the context that CREATED the fence. A
 * fence created on one context and waited on from another is exactly the case the
 * bit cannot cover - the waiting context's flush does not reach the producer's
 * command stream, so a producer whose commands are still buffered has a fence
 * that will never signal and a consumer that waits forever. The flush is
 * therefore issued on the PRODUCER, by this process, at each handoff, and the
 * consumer's wait carries flags 0.
 *
 * WHY THE WAIT IS A SERVER WAIT. OpenGL 4.6 section 4.1.1 gives WaitSync as the
 * command that makes the GL SERVER block until a sync object signals, returning
 * to the application immediately; ClientWaitSync blocks the calling thread
 * instead. Blocking the thread is what FINISH already does and is the cost this
 * path exists to remove, so ClientWaitSync is not resolved and cannot be reached.
 * WaitSync's flags must be 0 and its timeout must be TIMEOUT_IGNORED, which is
 * what both call sites pass and the only pair they can pass.
 *
 * WHY A SYNC OBJECT MAY CROSS THE TWO CONTEXTS AT ALL. OpenGL 4.6 Appendix D
 * lists sync objects among the objects SHARED between contexts in a share group,
 * as against container objects - vertex array objects, framebuffers and the rest
 * - which are not, and which is why this mode makes its own array object on the
 * auxiliary context. So a fence created on one context is a legal argument to a
 * WaitSync or a DeleteSync issued on the other. The ADDRESSES are still
 * per-context, which is a separate fact and is why there are two resolved sets.
 *
 * WHY A DELETE IMMEDIATELY AFTER THE WAIT IS CORRECT AND NOT EARLY. OpenGL 4.6
 * section 4.1 states that DeleteSync marks a sync object for deletion and does
 * not block, and that an object whose fence command has not yet signalled is
 * deleted only once the command has completed and all waiters have been
 * satisfied. The wait already issued is such a waiter, so deleting straight after
 * it neither cancels the wait nor outlives it: one create, one wait, one delete,
 * per handoff, and nothing held across a frame.
 *
 * AND WHAT HAPPENS TO AN OBJECT A FAILURE PATH IS STILL HOLDING. It is deleted on
 * the MAIN context after the verified switch home, never on the auxiliary one -
 * which is not an arbitrary choice: the refusal messages those paths print state
 * that NOTHING was bound, enabled or issued on that context, and a delete issued
 * there would make them false. Main created the input fence, so main deleting it
 * is also the owner-side delete. The one exception is a lost-context verdict,
 * after which no GL command of any kind may be issued: the object is abandoned,
 * counted apart as fx_fatal_drops, and the process is ending. */

/* THE OBJECT POPULATION, RAISED AT EVERY CREATION THAT ANSWERED A HANDLE AND
 * LOWERED AT EVERY RELEASE. fx_live is what a census reads to say nothing is
 * held outside a bracket; fx_peak is what says the two handles never overlapped.
 * Both are plain arithmetic on this process's own records - no GL call, no query
 * - and they are MEASURED rather than asserted because the design's claim that
 * at most one object exists at a time is exactly the kind of statement an edit
 * can falsify without changing one issued command or one pixel. */
static void tf_fx_held(void)
{
    g_fc.fx_live++;
    if (g_fc.fx_live > g_fc.fx_peak) g_fc.fx_peak = g_fc.fx_live;
}

/* A RELEASE, WHICHEVER KIND. A delete and an abandon are different facts and are
 * counted apart; both end this process's hold on the object, which is the one
 * thing fx_live tracks. Clamped at 0 so that a release without a matching hold
 * cannot wrap the counter into a number no reader could interpret - it would
 * instead leave fx_live below the created-minus-accounted identity the header
 * states, which is where such an edit is meant to show up. */
static void tf_fx_released(void)
{
    if (g_fc.fx_live > 0ul) g_fc.fx_live--;
}

/* THE INPUT HANDOFF'S PRODUCER HALF, ON THE MAIN CONTEXT. Answers 1 when the
 * bracket may go on to the switch, 0 for a recoverable refusal it has already
 * reported and latched. In FINISH mode it is one glFinish and nothing else. */
static int tf_fx_in_open(int fx, SL_TF_SYNC *s, int ar_mode)
{
    if (!fx) {
        glFinish();
        g_c.ar_syncs++;
        return 1;
    }
    /* THE DRAIN THAT IS NOT ISSUED, COUNTED. "The fence replaced the glFinish"
     * is otherwise an absence, and an absence is what a later edit that added a
     * fence BESIDE the drains would also look like. */
    g_fc.fx_finish_omits++;
    *s = p_FenceSync(SL_BGTF_SYNC_GPU_COMMANDS_COMPLETE, 0);
    g_fc.fx_in_fences++;
    if (*s == NULL) {
        /* NOTHING WAS CREATED, so there is nothing to delete and no leak to
         * account for. The main context is current and untouched, the auxiliary
         * one was never switched to, and the capability latches so one context
         * that will not make a sync object does not pay a failed create per
         * mesh. */
        g_fc.fx_in_fence_fails++;
        g_fc.fx_refusals++;
        g_ar_state = -1;
        if (!g_fx_cre_said) {
            g_fx_cre_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_AUX_SYNC=FENCE with "
                            "SL_BG_TF_AUX_REAL=%s: glFenceSync answered 0 on the "
                            "MAIN context, so the input handoff has no fence to "
                            "wait on and the auxiliary context cannot be shown "
                            "what this one wrote. NOTHING was issued on the "
                            "auxiliary context - it was never made current - no "
                            "sync object exists to delete, the served draw is "
                            "REFUSED to the CPU path, the slot stays UNWRITTEN, "
                            "the capability LATCHES, and this is reported once "
                            "per launch\n", sl_bgtf_ar_name(ar_mode));
        }
        return 0;
    }
    tf_fx_held();
    /* THE PRODUCER'S OWN FLUSH, EXPLICITLY, for the reason above: the consumer's
     * wait cannot flush this context's stream, and an unflushed fence never
     * signals. */
    glFlush();
    g_fc.fx_in_flushes++;
    g_c.ar_syncs++;
    return 1;
}

/* THE INPUT HANDOFF'S CONSUMER HALF, ON THE AUXILIARY CONTEXT, issued after the
 * arrival drain and the two object checks and immediately before the first bind
 * that reads the input buffer. Raises *waited when the wait was issued, which is
 * one of the two halves the written slot is gated on. Deletes the object here,
 * with THIS context's pointer, for the lifetime reason above. */
static void tf_fx_in_close(int fx, SL_TF_SYNC *s, int *waited)
{
    if (!fx) return;
    p_ax_WaitSync(*s, 0, SL_BGTF_TIMEOUT_IGNORED);
    g_fc.fx_in_waits++;
    *waited = 1;
    p_ax_DeleteSync(*s);
    g_fc.fx_deletes++;
    g_fc.fx_deletes_aux++;
    tf_fx_released();
    *s = NULL;
}

/* THE OUTPUT HANDOFF'S PRODUCER HALF, ON THE AUXILIARY CONTEXT, issued at the
 * instant FINISH issues its completion drain: immediately after the matched
 * feedback scope closes and before this context's state is put back. Answers 1
 * when the bracket may go on, 0 for a recoverable refusal it has reported.
 *
 * THE RESTORATION STILL RUNS ON THE 0 ANSWER, and that is the caller's structure
 * rather than this function's: this returns, the five restoring calls run, the
 * verdict drain runs, and only then is the refusal taken - so the auxiliary
 * context is back at its entry defaults on this path exactly as on every other,
 * and no program is left current and no discard enabled behind it. */
static int tf_fx_out_open(int fx, SL_TF_SYNC *s, int ar_mode)
{
    if (!fx) {
        glFinish();
        g_c.ar_syncs++;
        return 1;
    }
    g_fc.fx_finish_omits++;
    *s = p_ax_FenceSync(SL_BGTF_SYNC_GPU_COMMANDS_COMPLETE, 0);
    g_fc.fx_out_fences++;
    if (*s == NULL) {
        g_fc.fx_out_fence_fails++;
        g_fc.fx_refusals++;
        g_ar_state = -1;
        if (!g_fx_cre_said) {
            g_fx_cre_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_AUX_SYNC=FENCE with "
                            "SL_BG_TF_AUX_REAL=%s: glFenceSync answered 0 on the "
                            "AUXILIARY context after the matched feedback scope, "
                            "so the transform's writes have no fence the MAIN "
                            "context could wait on and their visibility there can "
                            "never be established. The transform may well have "
                            "run; a pool whose visibility is unproved must never "
                            "be rasterised, so the pool is NOT re-bound, the slot "
                            "stays UNWRITTEN, nothing is rasterised from it, the "
                            "served draw is REFUSED to the CPU path, the "
                            "capability LATCHES, and this is reported once per "
                            "launch\n", sl_bgtf_ar_name(ar_mode));
        }
        return 0;
    }
    tf_fx_held();
    glFlush();
    g_fc.fx_out_flushes++;
    g_c.ar_syncs++;
    return 1;
}

/* THE OUTPUT HANDOFF'S CONSUMER HALF, ON THE MAIN CONTEXT, issued after the
 * VERIFIED switch home and before the pool is re-bound there. Raises *waited,
 * the second of the two halves the written slot is gated on.
 *
 * WHY NO ERROR STATE IS READ HERE, STATED RATHER THAN LEFT OUT. Reading it would
 * mean a glGetError on the MAIN context, and a read CONSUMES the flag it
 * returns - so this mode would swallow an error belonging to the ordinary
 * rendering path and leave nothing behind to say it had. That invariant is
 * recorded at tf_ar_drain and is not weakened for this path. What proves this
 * wait instead: its sync object was answered non-zero by a FenceSync on the
 * other context; the bracket's verdict drain, taken on that context and after
 * that FenceSync and its flush, came back clean; the address was resolved on THIS
 * context and verified non-NULL at the acquire; and WaitSync's only two error
 * conditions in OpenGL 4.6 section 4.1.1 are a handle that is not a sync object
 * and a flags/timeout pair other than 0 and TIMEOUT_IGNORED, neither of which
 * this call site can present. THE BOUNDARY IS THEREFORE EXPLICIT: this one call
 * is proved by construction and by the specification, not by a measurement. */
static void tf_fx_out_close(int fx, SL_TF_SYNC *s, int *waited)
{
    if (!fx) return;
    p_WaitSync(*s, 0, SL_BGTF_TIMEOUT_IGNORED);
    g_fc.fx_out_waits++;
    *waited = 1;
    p_DeleteSync(*s);
    g_fc.fx_deletes++;
    g_fc.fx_deletes_main++;
    tf_fx_released();
    *s = NULL;
}

/* DELETE AN OBJECT A REFUSAL PATH IS STILL HOLDING, ON THE MAIN CONTEXT and only
 * where that context is VERIFIED current. A no-op when nothing is held and under
 * FINISH, where nothing ever is. */
static void tf_fx_drop_main(SL_TF_SYNC *s)
{
    if (*s == NULL) return;
    p_DeleteSync(*s);
    g_fc.fx_deletes++;
    g_fc.fx_deletes_main++;
    tf_fx_released();
    *s = NULL;
}

/* ABANDON AN OBJECT BECAUSE A LOST-CONTEXT VERDICT HAS BEEN REACHED. ISSUES NO
 * GL COMMAND, which is the whole point: after that verdict this process cannot
 * say which context a command would reach, so a delete is exactly as forbidden
 * as a draw. Counted apart from fx_leaks because it is the correct action and not
 * a defect - the process ends at the point of loss. */
static void tf_fx_abandon(SL_TF_SYNC *s)
{
    if (*s == NULL) return;
    g_fc.fx_fatal_drops++;
    tf_fx_released();
    *s = NULL;
}

/* LEAVE THE AUXILIARY CONTEXT, DESTROY IT AND REFUSE, FROM INSIDE THE ACQUIRE'S
 * OWN VISIT (SL_BG_TF_AUX_REAL). Always answers 0: every caller is a refusal.
 *
 * THE ORDER IS THE PROPERTY. The auxiliary context is current when this is
 * called, so the switch home comes first and is VERIFIED - a destroy requires a
 * verified main context, and an unverifiable one is the UNKNOWN outcome, which
 * makes no further call at all and ends the process. Only then is the context
 * destroyed, and only then are the names dropped: they name entry points in a
 * context that is about to stop existing.
 *
 * BOTH LATCHES ARE SET. The context is gone, so the empty-draw control's own
 * capability state is no longer true either - and although the two modes cannot
 * both be in force, leaving `g_ax_state` at 1 beside a destroyed context would
 * be a false statement about a context in a cache. */
static int tf_ar_leave(const char *what)
{
    g_ar_state = -1;
    g_ax_state = -1;
    if (sl_gl_main_make_current() != 1) {
        tf_ax_lost("the real-prepass mode's capability check refused on the "
                   "auxiliary GL context and the MAIN context could not be "
                   "verified current again");
        return 0;
    }
    tf_ar_forget();
    p_ax_IsProgram  = NULL;
    p_ax_UseProgram = NULL;
    if (!tf_ax_release()) return 0;
    if (!g_ar_said) {
        g_ar_said = 1;
        fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s: the auxiliary GL context %s. "
                        "The context was destroyed again while the main context "
                        "was verified current, the served draw is REFUSED to "
                        "the CPU path rather than run on the main context - "
                        "which would be the ordinary candidate path under this "
                        "mode's name - and this is reported once per launch\n",
                sl_bgtf_ar_name(g_ar), what);
    }
    return 0;
}

/* THE REAL-PREPASS MODE'S CAPABILITY, ESTABLISHED ONCE PER MAIN CONTEXT AND
 * FAILING CLOSED (SL_BG_TF_AUX_REAL).
 *
 * 1 when a second, OBJECT-SHARING context exists that has resolved EVERY entry
 * point this mode calls on it, has answered glIsProgram TRUE there for the
 * prepass program through its OWN pointer, has given this mode a vertex array
 * object of its own, and is left with the MAIN context VERIFIED current. 0
 * otherwise - and 0 is a REFUSAL of the served draw, never a fallback. The one
 * thing this must never do is answer 0 and let the prepass proceed on the main
 * context: that is the ordinary candidate path, and the run would be recorded as
 * an auxiliary real prepass without being one.
 *
 * IT BUILDS ON THE EMPTY-DRAW CONTROL'S ACQUIRE RATHER THAN REPEATING IT. The
 * probe before the first create, the create itself, the share check and the
 * resolve of glIsProgram and glUseProgram on the auxiliary context are exactly
 * the same work with exactly the same three outcomes, so tf_ax_acquire is called
 * and its answer taken. The two modes are mutually exclusive, so nothing of the
 * empty-draw control's own behaviour is reachable on a launch that selected this
 * one - and the shared acquire's counters (ax_creates, ax_create_fails and the
 * rest) carry the context's establishment while this mode's own carry what it
 * adds on top.
 *
 * WHAT IT ADDS IS ONE MORE VISIT TO THAT CONTEXT. A second verified switch, the
 * larger entry-point set resolved there, the share check asked again through
 * this mode's own pointer so the mode has its own evidence, the vertex array
 * object generated there, that context's state put back, and the verified switch
 * home. It is a second round trip rather than work folded into tf_ax_acquire so
 * that the empty-draw control's own sequence - which a frozen census line
 * records call for call - is not changed by this mode existing.
 *
 * LAZY, AND ONLY ON A LAUNCH THAT SELECTED AN ARM. Nothing here runs for an off
 * launch: no entry point is resolved, no context is created and no object is
 * generated, so the default path's resolved set, its call sequence and the
 * number of GL contexts this process owns are what they are without this mode.
 *
 * A REFUSAL DESTROYS WHAT IT MADE, and only while the MAIN context is verified
 * current, which is the one instant that is safe. An UNKNOWN current context
 * makes no further call of any kind: no query, no destroy, no restoration - it
 * reports the run invalid and ENDS THE PROCESS through the one funnel. */
static int tf_ar_acquire(void)
{
    int r;
    int got;
    GLboolean shared;
    GLuint vao = 0u;

    /* THE PROCESS LATCH FIRST, before even the per-context cache: once the main
     * context is lost, no context's capability means anything. */
    if (g_ax_lost) return 0;
    if (g_ar_state != 0) return g_ar_state > 0;
    /* THE FENCE PATH'S MAIN-CONTEXT SET, RESOLVED FIRST AND BEFORE ANYTHING
     * EXISTS TO CLEAN UP (SL_BG_TF_AUX_SYNC).
     *
     * WHY HERE AND NOT LOWER. These three belong to the MAIN context, so the
     * only legal instant to resolve them is while it is current - which it is at
     * entry, and is not again until the switch home far below. Resolving them
     * BEFORE the shared acquire is what makes their failure the cheapest refusal
     * in this function: no context has been created, no object generated and no
     * GL command issued, so the refusal has nothing to undo and leaves the main
     * context exactly as it was found.
     *
     * AND ONLY ON A LAUNCH THAT ASKED FOR THE FENCE PATH. tf_fx_requested
     * answers FINISH without reading the environment when no arm is in force,
     * and a FINISH launch skips this block entirely - so the number of names this
     * mode resolves on either context under its original synchronisation is
     * unchanged, which is a frozen count in this fixture's own evidence.
     *
     * A MISSING NAME REFUSES AND NEVER FALLS BACK TO FINISH, for the reason
     * recorded at the typedefs: a launch that reported fence numbers while
     * draining the pipeline twice per bracket could not be read at all. */
    if (tf_fx_requested() == SL_BGTF_FX_FENCE) {
        p_FenceSync  = (SL_TF_FENCESYNC)  sl_gl_proc("glFenceSync");
        p_WaitSync   = (SL_TF_WAITSYNC)   sl_gl_proc("glWaitSync");
        p_DeleteSync = (SL_TF_DELETESYNC) sl_gl_proc("glDeleteSync");
        if (p_FenceSync == NULL || p_WaitSync == NULL || p_DeleteSync == NULL) {
            p_FenceSync  = NULL;
            p_WaitSync   = NULL;
            p_DeleteSync = NULL;
            g_fx_ok = 0;
            g_c.ar_proc_fails++;
            g_fc.fx_proc_fails++;
            g_fc.fx_proc_fails_main++;
            g_ar_state = -1;
            if (!g_fx_said) {
                g_fx_said = 1;
                fprintf(stderr, "#95 SL_BG_TF_AUX_SYNC=FENCE with "
                                "SL_BG_TF_AUX_REAL=%s: the MAIN GL context did "
                                "not resolve glFenceSync, glWaitSync and "
                                "glDeleteSync, which are GL 3.2 and which the "
                                "fence path issues on it. The request is REFUSED "
                                "rather than served by the original two glFinish "
                                "calls, because a launch that reported fence "
                                "numbers for a drained pipeline could not be read "
                                "at all. NO AUXILIARY GL CONTEXT IS CREATED, no "
                                "entry point is resolved on one, the served draw "
                                "is REFUSED to the CPU path, and this is reported "
                                "once per launch\n", sl_bgtf_ar_name(g_ar));
            }
            return 0;
        }
    }
    /* THE SHARED ESTABLISHMENT. Its own three outcomes are already handled
     * inside it: 0 refuses with the main context verified current and this mode
     * latches beside it, and the unknown outcome has already reported the run
     * invalid and terminated the process.
     *
     * AND ITS REFUSALS ARE ATTRIBUTED TO THIS SELECTOR WHILE IT RUNS HERE. The
     * flag is raised for exactly the duration of this one call and cleared
     * immediately, so the probe, create, resolve and share refusals inside it
     * name SL_BG_TF_AUX_REAL with this launch's arm token - see tf_ax_sel. It is
     * cleared on every path, including the one where the call did not come
     * back the way a success does, because the next launch's reading of those
     * lines must not depend on how this one left. */
    g_ax_for_ar = 1;
    got = tf_ax_acquire();
    g_ax_for_ar = 0;
    if (!got) {
        g_ar_state = -1;
        return 0;
    }
    r = sl_gl_aux_make_current();
    if (r == SL_BGTF_AUX_LOST) {
        tf_ax_lost("the real-prepass mode's switch to the auxiliary GL context "
                   "left neither it nor the MAIN context verifiably current");
        return 0;
    }
    if (r == 0) {
        /* THE MAIN CONTEXT IS VERIFIED CURRENT - that is what this failure
         * means - so the context can be, and is, destroyed here. */
        g_c.ar_switch_fails++;
        g_ar_state = -1;
        g_ax_state = -1;
        p_ax_IsProgram  = NULL;
        p_ax_UseProgram = NULL;
        if (!tf_ax_release()) return 0;
        if (!g_ar_said) {
            g_ar_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s: the auxiliary GL context "
                            "could not be made current for the real prepass's "
                            "own capability check, so it was destroyed again "
                            "while the main context was still current. The "
                            "served draw is REFUSED to the CPU path rather than "
                            "run on the main context, and this is reported once "
                            "per launch\n", sl_bgtf_ar_name(g_ar));
        }
        return 0;
    }
    /* THIS CONTEXT'S OWN ENTRY POINTS FOR THIS MODE, RESOLVED ON IT AND USED
     * ONLY ON IT. This is the first instant at which that is possible, and the
     * pointers are cached for the context's lifetime, so the per-mesh bracket
     * resolves nothing. They are resolved TOGETHER rather than one at a time
     * because a context that can answer for some and not others is a context
     * this mode cannot use, and finding that out at a draw would mean finding it
     * out with bracket state already written. */
    p_ax_IsBuffer     = (SL_TF_ISBUFFER)     sl_gl_proc("glIsBuffer");
    p_ax_BindBuffer   = (SL_TF_BINDBUFFER)   sl_gl_proc("glBindBuffer");
    p_ax_BindBufferBase =
        (SL_TF_BINDBUFFERBASE) sl_gl_proc("glBindBufferBase");
    p_ax_BeginTransformFeedback =
        (SL_TF_BEGINTF) sl_gl_proc("glBeginTransformFeedback");
    p_ax_EndTransformFeedback =
        (SL_TF_ENDTF) sl_gl_proc("glEndTransformFeedback");
    p_ax_GenVertexArrays =
        (SL_TF_GENVERTEXARRAYS) sl_gl_proc("glGenVertexArrays");
    p_ax_BindVertexArray =
        (SL_TF_BINDVERTEXARRAY) sl_gl_proc("glBindVertexArray");
    p_ax_Uniform1f    = (SL_TF_UNIFORM1F)    sl_gl_proc("glUniform1f");
    p_ax_Uniform3f    = (SL_TF_UNIFORM3F)    sl_gl_proc("glUniform3f");
    p_ax_Uniform1i    = (SL_TF_UNIFORM1I)    sl_gl_proc("glUniform1i");
    p_ax_Uniform1fv   = (SL_TF_UNIFORM1FV)   sl_gl_proc("glUniform1fv");
    if (p_ax_IsBuffer == NULL || p_ax_BindBuffer == NULL
        || p_ax_BindBufferBase == NULL || p_ax_BeginTransformFeedback == NULL
        || p_ax_EndTransformFeedback == NULL || p_ax_GenVertexArrays == NULL
        || p_ax_BindVertexArray == NULL || p_ax_Uniform1f == NULL
        || p_ax_Uniform3f == NULL || p_ax_Uniform1i == NULL
        || p_ax_Uniform1fv == NULL) {
        g_c.ar_proc_fails++;
        return tf_ar_leave("did not resolve every entry point the real prepass "
                         "calls on it. Entry-point addresses belong to the "
                         "context they were resolved on, so the main context's "
                         "are NOT used there");
    }
    /* AND THE FENCE PATH'S THREE ON THIS CONTEXT, UNDER THE SAME RULE
     * (SL_BG_TF_AUX_SYNC). The main context's three were resolved at the top of
     * this function on the main context; these are the same NAMES resolved
     * again, here, on the context they will be invoked on. There is no path on
     * which one set's address is used while the other's context is current, and
     * that is the whole of the policy - the two platform citations at
     * p_ax_IsProgram are not weaker for a sync object than for a uniform write.
     *
     * A SEPARATE BLOCK AND A SEPARATE MESSAGE, deliberately. The eleven above
     * are what the real prepass needs under either synchronisation mode, and a
     * context that supplies them but not these is a context that can run the
     * prepass with FINISH and cannot run it with FENCE - which is a different
     * fact and is told as one, so a reader is pointed at the selector rather
     * than at the mode. The counters are split the same way.
     *
     * THE CAPABILITY IS DECLARED ONLY HERE, with both sets in hand, which is why
     * `g_fx_ok` is set at this one place: a flag raised after the main set
     * resolved would be true across the whole of the auxiliary visit below,
     * where it would be false. */
    if (tf_fx_requested() == SL_BGTF_FX_FENCE) {
        p_ax_FenceSync  = (SL_TF_FENCESYNC)  sl_gl_proc("glFenceSync");
        p_ax_WaitSync   = (SL_TF_WAITSYNC)   sl_gl_proc("glWaitSync");
        p_ax_DeleteSync = (SL_TF_DELETESYNC) sl_gl_proc("glDeleteSync");
        if (p_ax_FenceSync == NULL || p_ax_WaitSync == NULL
            || p_ax_DeleteSync == NULL) {
            g_c.ar_proc_fails++;
            g_fc.fx_proc_fails++;
            g_fc.fx_proc_fails_aux++;
            return tf_ar_leave("did not resolve glFenceSync, glWaitSync and "
                             "glDeleteSync, which SL_BG_TF_AUX_SYNC=FENCE issues "
                             "on it. The request is REFUSED rather than served by "
                             "the original two glFinish calls, because a launch "
                             "that reported fence numbers for a drained pipeline "
                             "could not be read at all. Entry-point addresses "
                             "belong to the context they were resolved on, so the "
                             "main context's are NOT used there");
        }
        /* BOTH SETS ARE IN HAND. This is the one assignment that puts the fence
         * path in force, and every site that may issue a fence command reads it
         * through tf_fx_in_force. */
        g_fx_ok = 1;
    }
    /* THE SHARE CHECK, ASKED AGAIN THROUGH THIS MODE'S OWN VISIT. The shared
     * acquire already asked it, and asking it here as well is not redundant
     * belt: that answer was about a context state this function has since
     * re-entered, and the one question this mode's whole output rests on - that
     * the program NAME it is about to bind means the prepass program in this
     * context - is one it should hold its own evidence for. The pointer is the
     * auxiliary context's, resolved by the shared acquire on this same
     * context. */
    shared = p_ax_IsProgram(g_prog);
    if (!shared) {
        g_c.ar_prog_fails++;
        return tf_ar_leave("does NOT share object names with the main one - "
                         "glIsProgram answered FALSE there for the prepass "
                         "program - so the name this mode would bind means "
                         "nothing in it");
    }
    /* THIS MODE'S OWN VERTEX ARRAY OBJECT, ON THIS CONTEXT. A vertex array
     * object is a CONTAINER object and is therefore not shared, which is both
     * why the main context's cannot be used and why this one is released with
     * the context rather than deleted. A context that will not give one refuses
     * the mode: falling back to the default object would mean the prepass's
     * array state landing on whatever that context's default object holds, which
     * is state this mode has not accounted for. */
    p_ax_GenVertexArrays(1, &vao);
    if (vao == 0u) {
        g_c.ar_vao_fails++;
        return tf_ar_leave("would not give the real prepass a vertex array object "
                         "of its own. A vertex array object is a CONTAINER "
                         "object and is not shared between contexts, so the "
                         "main context's cannot be used and the default object "
                         "holds state this mode has not accounted for");
    }
    g_ar_vao = vao;
    if (sl_gl_main_make_current() != 1) {
        /* THE SWITCH BACK FAILED. The process no longer knows which context it
         * is on, so nothing further is called: the context is left undestroyed,
         * the object is left where it is, and the process ends. */
        tf_ax_lost("the real-prepass mode's capability check ran on the "
                   "auxiliary GL context and the MAIN context could not be "
                   "verified current again");
        return 0;
    }
    g_ar_state = 1;
    g_c.ar_creates++;
    return 1;
}

/* A CONTEXT'S END. Makes no GL call: the context may already be unusable and
 * the driver releases the program and the buffers with it. Everything cached
 * goes back to untried, including the slot buffer names - a name from a dead
 * context must never be bound. The switch is NOT re-read, because it belongs to
 * the launch rather than to a context.
 *
 * THE DIAGNOSTIC'S SPENT ATTEMPT IS NOT CLEARED HERE, and that is deliberate:
 * the one attempt belongs to the PROCESS, not to a context, so a context
 * recreation must not hand a failed capture a second try. What is cleared is
 * the resolved readback entry point - it belongs to a context like every
 * other - and any selection naming a slot this call is about to drop.
 * glGetError is not among them: it is a direct core call and holds no cached
 * pointer to drop. */
void sl_bgtf_invalidate(void)
{
    int i;
    (void) dump_clear_selection();
    p_GetBufferSubData = NULL;
    /* THE CLIENT UNIT SELECTOR BELONGS TO A CONTEXT like every other resolved
     * entry point, so a new context resolves it again. The saved client state
     * goes with it: it describes GL state that no longer exists, and `held` is
     * cleared so a restore can never be issued against a context that never had
     * the arrays disabled. The switches are NOT re-read - they belong to the
     * launch - and `g_pin_said` is kept for the same reason: a refusal is
     * reported once per launch, not once per context. */
    p_ClientActiveTexture = NULL;
    memset(&g_cli, 0, sizeof g_cli);
    /* THE PRIVATE ARRAY OBJECT IS FORGOTTEN, NOT DELETED OR UNBOUND. This hook
     * makes no GL call at all - the context may already be unusable - and the
     * two calls that would be the alternatives are both wrong here: a delete in
     * a dead context is the call that must not be made, and a bind would hand a
     * name from a context that is ending to whatever comes next. Ordinary
     * context destruction releases the object with the program and the buffers.
     *
     * `held` and the saved binding go with it. They describe a bracket around a
     * draw in a context that no longer exists, so a restore must never be
     * issuable from them; the saved value goes back to the sentinel rather than
     * to 0, which is a real object name. The entry points belong to a context
     * like every other and are resolved again by a new one. The switch is NOT
     * re-read - it belongs to the launch - and `g_pvao_said` is kept for the same
     * reason `g_pin_said` is: a refusal is reported once per launch. */
    p_GenVertexArrays = NULL;
    p_BindVertexArray = NULL;
    g_pvao_name = 0u;
    g_pvao_saved = TF_VAO_NONE;
    g_pvao_held = 0;
    /* THE NEUTRAL PROGRAM IS FORGOTTEN HERE AND DELETED IN sl_bgtf_shutdown.
     * This hook makes no GL call for the reason the whole note above gives, and
     * a delete is exactly the call that must not be made from it: it runs at a
     * context's CREATE as well as at its end, where the name belongs to a
     * context that is already gone. The capability state goes back to untried so
     * a new context creates its own program and resolves its own entry point,
     * and the name is dropped so nothing can bind one from a dead context. The
     * SWITCH is not re-read - it belongs to the launch - and `g_np_said` is kept
     * for the reason `g_pr_said` is: a failure is reported once per launch, not
     * once per context.
     *
     * THE SHADER IS NOT A CASE HERE. The acquire releases it on every path it
     * can leave, success included, so there is never a shader object for this
     * hook or for teardown to account for - only the two entry points, which
     * belong to a context like every other. */
    p_DeleteProgram = NULL;
    p_DeleteShader = NULL;
    g_np_prog = 0u;
    g_np_state = 0;
    /* THE AUXILIARY CONTEXT IS FORGOTTEN AS A CAPABILITY AND DESTROYED IN
     * sl_bgtf_shutdown, exactly as the neutral program is, and this hook calls
     * neither GL nor the backend for the reason the whole note above gives: it
     * runs at a main context's CREATE as well as at its end, where a destroy
     * would be issued with no context current.
     *
     * `g_ax_live` IS DELIBERATELY NOT CLEARED. It names an object the platform
     * still holds, not a cached answer, so clearing it here would be this
     * module forgetting a context it is responsible for; the capability state
     * goes back to untried so a new main context runs its own create and its own
     * share check, which is what a context-local answer means, while the live
     * flag keeps teardown able to release what exists. A backend that recreated
     * its main context mid-run would therefore reach teardown with one auxiliary
     * context to destroy, which is the honest account.
     *
     * `g_ax_lost` IS NOT CLEARED EITHER, and that is the stronger case: it is a
     * fact about this PROCESS rather than about a context, and a context
     * recreation does not restore knowledge of which context a command reaches.
     * A control that resumed after it would be issuing commands with no
     * destination it could name. `g_ax_said` is kept for the reason `g_np_said`
     * is: a failure is reported once per launch, not once per context.
     *
     * THE ENTRY POINTS BELONG TO A CONTEXT LIKE EVERY OTHER and are dropped -
     * all three, and for two different reasons. The main context's probe
     * address was resolved on a context that is ending. The auxiliary pair were
     * resolved on the AUXILIARY context, which this hook does not destroy, so
     * dropping them is not about that context ending: it is that the next
     * acquire resolves its own, on whatever context it verifies, and a pointer
     * surviving in this file across a capability reset is a pointer whose
     * context nothing can state. */
    g_ax_probe      = NULL;
    p_ax_IsProgram  = NULL;
    p_ax_UseProgram = NULL;
    /* AND THE FENCE PATH'S MAIN-CONTEXT SET, on exactly the terms above
     * (SL_BG_TF_AUX_SYNC): these three were resolved on the context that is
     * ending, so the names are dropped without a call. The auxiliary three and
     * the joint capability flag go with tf_ar_forget below. The SELECTOR is not
     * re-read, it belonging to the launch. */
    p_FenceSync     = NULL;
    p_WaitSync      = NULL;
    p_DeleteSync    = NULL;
    g_ax_state = 0;
    /* AND THE REAL-PREPASS MODE'S LARGER SET AND ITS ARRAY OBJECT, on the same
     * terms (SL_BG_TF_AUX_REAL). Its capability state goes back to untried so a
     * new main context establishes its own; the array object's NAME is dropped
     * without a delete, because it is a container object of a context this hook
     * does not and must not call into - see tf_ar_forget. The SELECTOR is not
     * re-read, it belonging to the launch, and `g_ar_said` is kept for the
     * reason `g_ax_said` is. */
    tf_ar_forget();
    g_ar_state = 0;
    g_state = 0;
    g_prog = 0;
    g_u_sc = g_u_post = g_u_org = g_u_C = g_u_fogless = -1;
    g_u_dmode = g_u_fk1 = g_u_fk2 = -1;
    g_why[0] = '\0';
    for (i = 0; i < TF_SLOTS; i++) memset(&g_slot[i], 0, sizeof g_slot[i]);
    g_walk_live = 0;
    g_walk_counted = 0;
    g_gen = 0;
    g_in_vbo = 0;
    g_in_stride = 0;
    g_nverts = 0;
}

/* A CONTEXT'S END WHILE IT IS STILL CURRENT, which is a different instant from
 * the hook above and the only one at which a delete is legal.
 *
 * ONE OBJECT, AND ONLY THIS CONTROL'S SUCCESSFULLY LINKED PROGRAM. The
 * neutral-program control (SL_BG_TF_NEUTRAL_PROG) is the only thing in this
 * module that creates objects it is responsible for releasing: the ordinary
 * prepass program and the slot buffers are left to ordinary context
 * destruction, for the reason sl_bgtf_invalidate records, and nothing about
 * them changes here. The control's OTHER object, the vertex shader, never
 * reaches this function - the acquire releases it on every path it can leave,
 * success included, and the linked program holds the flagged object until this
 * delete frees it with the program.
 *
 * NO LAUNCH WITHOUT THE CONTROL PAYS A CALL. `g_np_prog` is 0 unless a program
 * was created AND linked, so an off launch, a refused launch and a launch whose
 * acquire failed all fall through this as one integer test. A program that
 * failed to LINK was already deleted by the acquire, while the context was
 * current, and its name cleared there - so this never sees one and no path
 * deletes a program twice.
 *
 * THE NAME IS CLEARED BEFORE ANYTHING ELSE CAN RUN, and the capability goes back
 * to untried, so a second call deletes nothing twice and a backend that calls
 * this and then recreates a context starts from no program at all. The switch is
 * not re-read: it belongs to the launch. */
void sl_bgtf_shutdown(void)
{
    if (g_np_prog != 0u && p_DeleteProgram != NULL) {
        p_DeleteProgram(g_np_prog);
        g_c.np_deletes++;
    }
    g_np_prog = 0u;
    g_np_state = 0;
    /* AND THE AUXILIARY-CONTEXT CONTROL'S SECOND GL CONTEXT
     * (SL_BG_TF_AUX_EMPTY), which is not a GL object and is not deleted with a
     * GL command - but which does require the main context CURRENT and the
     * window alive, both of which hold here and at no later point. A launch that
     * created none pays one integer test.
     *
     * A LOST MAIN CONTEXT DESTROYS NOTHING, and the live flag is kept rather
     * than cleared so that nothing later reads this as a context that was
     * released. Destroying requires the main context to be verified current,
     * which is precisely what could not be established; calling into that is
     * the one thing the lost latch exists to prevent. The platform releases the
     * context with the process, and the line below says so once.
     *
     * THAT BRANCH IS UNREACHABLE IN THE PRODUCT, and saying so is part of its
     * justification: a lost context ends the process at the point of loss, so no
     * teardown runs after one. It is kept because a fixture intercepts the
     * terminate and this function must still be correct when control reaches it
     * - which is what makes "a lost context destroys nothing" an assertion a
     * test can make rather than a claim about code nobody can run.
     *
     * THE FLAG IS CLEARED BY THE RELEASE ITSELF on the ordinary path, so a
     * second call destroys nothing twice and a backend that calls this and then
     * recreates a context starts from no auxiliary context at all. It is NOT
     * pre-cleared here, which it used to be: on the one path where the release
     * reports the unknown outcome, the context demonstrably still exists, and a
     * flag cleared in advance would have recorded it as released.
     *
     * AND THE DESTROY ITSELF IS CHECKED. The backend verifies its own
     * precondition now and answers that it destroyed nothing if the main context
     * is not verifiably current - which, at this one call site, would mean the
     * backend disagrees with the caller about a context the caller is about to
     * delete. tf_ax_release turns that answer into the lost outcome rather than
     * a silent leak, so a teardown that cannot release the context does not then
     * go on to delete the main one and present a window. */
    if (g_ax_live && g_ax_lost) {
        if (!g_ax_said) {
            g_ax_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1: the auxiliary GL context "
                            "is NOT destroyed at teardown, because the main "
                            "context was lost and a destroy needs it verified "
                            "current. The platform releases the context with "
                            "the process\n");
        }
    } else if (g_ax_live) {
        (void) tf_ax_release();
    }
    /* THE AUXILIARY CONTEXT'S ENTRY POINTS GO WITH IT. They were resolved on a
     * context that no longer exists on every path that reaches here except the
     * lost one, where nothing may be called through them at all. */
    p_ax_IsProgram  = NULL;
    p_ax_UseProgram = NULL;
    /* AND SO DOES THE REAL-PREPASS MODE'S LARGER SET, together with its VERTEX
     * ARRAY OBJECT (SL_BG_TF_AUX_REAL). The object is a container object of the
     * auxiliary context: on every path that reaches here except the lost one
     * that context has just been destroyed and the object with it, and on the
     * lost one nothing may be called at all - so dropping the name is the whole
     * of the release and no glDeleteVertexArrays is resolved anywhere in this
     * file. */
    tf_ar_forget();
}

int sl_bgtf_ready(void)
{
    static const char *const varying[1] = { "v_clip" };
    GLuint vs;
    GLint ok = 0;

    if (g_state != 0) return g_state > 0;
    /* OFF DOES NOT POISON CAPABILITY, for the same reason the other module
     * gives: leaving the state untried costs an off launch nothing - no entry
     * point is resolved and no program is compiled - without making OFF
     * permanent for the context. A REAL failure below latches -1 and is never
     * retried.
     *
     * v0.4 AND THE QUESTION IS WHETHER A PREPASS IS SELECTED AT ALL, NOT
     * WHETHER THE DEVELOPER SWITCH IS SET. This test read `!sl_bgtf_opt_in()`,
     * which was the whole answer while the switch was the only route in. With
     * the OPTIMIZED RENDERING row selecting the prepass it became a DEFECT that
     * no image could show and no text check caught: sl_gfx_dl.c admitted an
     * ordinary row-ON walk through sl_bgtf_walk_admit, called this, got 0
     * because SL_BG_TF_CLIP was absent, and drew the CPU path - so the
     * production selection could never enter its prepass on any launch. The
     * admission answer is therefore what this asks, and the two can no longer
     * disagree about which launches reach a prepass.
     *
     * ASKING THE ADMISSION HERE IS NOT A SECOND SELECTION. It is the same one,
     * latched: the developer route answers from a switch parsed once, and the
     * production route promotes once per launch and then answers from that. So
     * this adds no environment read, no GL call and no per-walk cost, and a
     * walk cannot be admitted by one answer and prepared against another.
     *
     * THE ROW IS STILL THE CALLER'S, WHICH IS WHERE "OFF COSTS NOTHING" NOW
     * LIVES. This function does not read the row - nothing in this file does -
     * so on a row-OFF launch it is simply never called: sl_gfx_dl.c asks the
     * admission inside the walk's own latched row answer and only then asks
     * this. That is checkable rather than asserted, and
     * tools/windows/bgtfoptin-check.py parses the chain for it.
     *
     * A FAIL-CLOSED LAUNCH IS STILL ADMITTED HERE, deliberately, and that is
     * what keeps the counters truthful. An unreadable SL_BG_TF_AUX_SYNC leaves
     * no arm in force and raises the fail-closed latch; the walk is prepared,
     * and each SERVED DRAW is then refused at the per-draw prepass site, where
     * ar_skips and refusals are counted. Refusing here instead would record a
     * WALK refusal (g_bgtf_r_prog) for a launch whose own frozen wording says
     * every served DRAW is refused - and it is the developer fail-closed route's
     * existing behaviour, so the two routes stay one path. */
    if (!sl_bgtf_walk_admit()) {
        snprintf(g_why, sizeof g_why, "%s",
                 tf_clip_named_off()
                     ? "SL_BG_TF_CLIP is 0: a developer took the GPU clip "
                       "prepass out of this launch"
                     : "no GPU clip prepass is selected: no walk was admitted "
                       "by the OPTIMIZED RENDERING row, or a developer selector "
                       "took the production selection away");
        return 0;
    }

    p_CreateShader       = (SL_TF_CREATESHADER)       sl_gl_proc("glCreateShader");
    p_ShaderSource       = (SL_TF_SHADERSOURCE)       sl_gl_proc("glShaderSource");
    p_CompileShader      = (SL_TF_COMPILESHADER)      sl_gl_proc("glCompileShader");
    p_GetShaderiv        = (SL_TF_GETSHADERIV)        sl_gl_proc("glGetShaderiv");
    p_GetShaderInfoLog   = (SL_TF_GETSHADERINFOLOG)   sl_gl_proc("glGetShaderInfoLog");
    p_CreateProgram      = (SL_TF_CREATEPROGRAM)      sl_gl_proc("glCreateProgram");
    p_AttachShader       = (SL_TF_ATTACHSHADER)       sl_gl_proc("glAttachShader");
    p_LinkProgram        = (SL_TF_LINKPROGRAM)        sl_gl_proc("glLinkProgram");
    p_GetProgramiv       = (SL_TF_GETPROGRAMIV)       sl_gl_proc("glGetProgramiv");
    p_GetProgramInfoLog  = (SL_TF_GETPROGRAMINFOLOG)  sl_gl_proc("glGetProgramInfoLog");
    p_UseProgram         = (SL_TF_USEPROGRAM)         sl_gl_proc("glUseProgram");
    p_GetUniformLocation = (SL_TF_GETUNIFORMLOCATION) sl_gl_proc("glGetUniformLocation");
    p_Uniform1f          = (SL_TF_UNIFORM1F)          sl_gl_proc("glUniform1f");
    p_Uniform3f          = (SL_TF_UNIFORM3F)          sl_gl_proc("glUniform3f");
    p_Uniform1i          = (SL_TF_UNIFORM1I)          sl_gl_proc("glUniform1i");
    p_Uniform1fv         = (SL_TF_UNIFORM1FV)         sl_gl_proc("glUniform1fv");
    /* The transform-feedback capability itself: without these four there is no
     * prepass, and the candidate refuses rather than approximating one. */
    p_TransformFeedbackVaryings =
        (SL_TF_TFVARYINGS)     sl_gl_proc("glTransformFeedbackVaryings");
    p_BindBufferBase     = (SL_TF_BINDBUFFERBASE)     sl_gl_proc("glBindBufferBase");
    p_BeginTransformFeedback =
        (SL_TF_BEGINTF)        sl_gl_proc("glBeginTransformFeedback");
    p_EndTransformFeedback =
        (SL_TF_ENDTF)          sl_gl_proc("glEndTransformFeedback");
    p_GenBuffers         = (SL_TF_GENBUFFERS)         sl_gl_proc("glGenBuffers");
    p_BindBuffer         = (SL_TF_BINDBUFFER)         sl_gl_proc("glBindBuffer");
    p_BufferData         = (SL_TF_BUFFERDATA)         sl_gl_proc("glBufferData");
    /* ALLOCATION ACCEPTANCE, asked at allocation time and nowhere else. A
     * buffer NAME is not storage: glBufferData is void and a driver that
     * cannot honour the size leaves the object at whatever it had. Querying
     * GL_BUFFER_SIZE once per allocation is a metadata read about an object,
     * not a pipeline synchronisation, so it adds no per-frame stall - and it
     * is the only way to learn the request was accepted without reading the
     * contents back. */
    p_GetBufferParameteriv =
        (SL_TF_GETBUFFERPARAMETERIV) sl_gl_proc("glGetBufferParameteriv");

    if (p_CreateShader == NULL || p_ShaderSource == NULL || p_CompileShader == NULL
        || p_GetShaderiv == NULL || p_GetShaderInfoLog == NULL || p_CreateProgram == NULL
        || p_AttachShader == NULL || p_LinkProgram == NULL || p_GetProgramiv == NULL
        || p_GetProgramInfoLog == NULL || p_UseProgram == NULL || p_GetUniformLocation == NULL
        || p_Uniform1f == NULL || p_Uniform3f == NULL || p_Uniform1i == NULL
        || p_Uniform1fv == NULL)
        return tf_refuse("GL 2.0 program entry points are not all present");
    if (p_TransformFeedbackVaryings == NULL || p_BindBufferBase == NULL
        || p_BeginTransformFeedback == NULL || p_EndTransformFeedback == NULL)
        return tf_refuse("transform feedback entry points are not all present");
    if (p_GenBuffers == NULL || p_BindBuffer == NULL || p_BufferData == NULL
        || p_GetBufferParameteriv == NULL)
        return tf_refuse("buffer object entry points are not all present");

    vs = p_CreateShader(GL_VERTEX_SHADER);
    if (vs == 0) return tf_refuse("glCreateShader gave no object");
    p_ShaderSource(vs, 1, &TF_VS, NULL);
    p_CompileShader(vs);
    p_GetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        /* The driver's own words: a refusal of `#version 400 compatibility`
         * and a refusal of `precise` read the same from outside, and they are
         * different facts. */
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetShaderInfoLog(vs, (GLsizei) sizeof log, &n, log);
        fprintf(stderr, "#95 background GPU clip prepass: program did not compile: %s\n", log);
        return tf_refuse("the prepass program did not compile");
    }
    g_prog = p_CreateProgram();
    if (g_prog == 0) return tf_refuse("glCreateProgram gave no object");
    p_AttachShader(g_prog, vs);
    /* THE OUTPUT VARYING IS RECORDED BEFORE THE LINK, which is the only order
     * that works: glTransformFeedbackVaryings takes effect at link time, so
     * naming it afterwards leaves a program that captures nothing and fails
     * later at glBeginTransformFeedback instead of here. */
    p_TransformFeedbackVaryings(g_prog, 1, varying, GL_INTERLEAVED_ATTRIBS);
    p_LinkProgram(g_prog);
    ok = 0;
    p_GetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetProgramInfoLog(g_prog, (GLsizei) sizeof log, &n, log);
        fprintf(stderr, "#95 background GPU clip prepass: program did not link: %s\n", log);
        g_prog = 0;
        return tf_refuse("the prepass program did not link");
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
        return tf_refuse("a prepass uniform did not resolve");
    }
    g_state = 1;
    /* v0.4 THE SUCCESS LINE NAMES WHICH SELECTION PREPARED THIS, because its
     * old tail - "SL_BG_TF_CLIP unset or 0: the existing paths" - became false
     * the moment the row could select the prepass: an unset switch is now the
     * ordinary production launch this very line is being printed for.
     *
     * NAMED FROM sl_bgtf_production AND NOT FROM THE SWITCH, which is a rule
     * about this whole function rather than a preference. The developer switch
     * is not mentioned anywhere inside the four functions a row-ON walk passes
     * through - readiness, begin_walk, the prepass and the auxiliary acquire -
     * so that a gate on it cannot reappear disguised as a report.
     * tools/windows/bgtfoptin-check.py enforces exactly that, and it caught
     * this line reaching for the switch while the fix was being written. */
    fprintf(stderr, "#95 background GPU clip prepass ready - eligible background meshes "
                    "take their clip positions from a transform-feedback pool and "
                    "rasterise through the original fixed-function path (%s)\n",
            sl_bgtf_production()
                ? "selected by the OPTIMIZED RENDERING row"
                : "the developer route; the row selects the same arm for an "
                  "ordinary session");
    return 1;
}

int sl_bgtf_begin_walk(const struct sl_bggpu_walk *w, unsigned nverts,
                       unsigned in_vbo, unsigned in_stride, unsigned gen)
{
    g_walk_live = 0;
    g_walk_counted = 0;
    /* A SELECTION BELONGS TO ONE DRAW OF ONE WALK. A new walk's generation
     * makes every slot key of the previous one unmatchable, so a selection held
     * across this point could only ever name a pool that is no longer the one
     * it was accepted for. */
    (void) dump_clear_selection();
    if (g_state <= 0 || w == NULL || nverts == 0u || in_vbo == 0u || in_stride == 0u) {
        g_c.refusals++;
        return 0;
    }
    /* THE POOL SIZE IS BOUNDED BEFORE ANYTHING IS ALLOCATED. Four floats per
     * vertex, and the product must not wrap: a count that overflows the byte
     * size would allocate a short buffer and let the prepass write past it.
     * The limit is expressed on the count so the multiply is never performed
     * on an unchecked value. */
    if (nverts > (0x7FFFFFFFu / (4u * (unsigned) sizeof(GLfloat)))) {
        g_c.refusals++;
        return 0;
    }
    g_gen = gen;
    g_nverts = nverts;
    g_in_vbo = (GLuint) in_vbo;
    g_in_stride = in_stride;
    /* The transform is uploaded once per walk with the program current, and the
     * depth uniforms are left at a defined starting value rather than a
     * previous walk's: every prepass sets its own through sl_bgtf_prepass. */
    p_UseProgram(g_prog);
    p_Uniform1f(g_u_sc, (GLfloat) w->sc);
    p_Uniform1f(g_u_post, (GLfloat) w->post);
    p_Uniform3f(g_u_org, (GLfloat) w->org[0], (GLfloat) w->org[1], (GLfloat) w->org[2]);
    p_Uniform1fv(g_u_C, 16, (const GLfloat *) w->C);
    p_Uniform1i(g_u_fogless, w->fogless ? 1 : 0);
    p_Uniform1i(g_u_dmode, SL_BGGPU_DEPTH_SINGLE);
    p_Uniform1f(g_u_fk1, 0.0f);
    p_Uniform1f(g_u_fk2, 0.0f);
    p_UseProgram(0);
    /* THE SAME FIVE VALUES, KEPT (SL_BG_TF_AUX_REAL), so that the real-prepass
     * mode can write every uniform the program declares on the AUXILIARY context
     * from values this module holds rather than from the shared program object's
     * state. Plain assignments: no GL call is added, nothing is reordered, the
     * calls above are byte-for-byte what they were, and an off launch never
     * reads them back. They are written AFTER the calls rather than before so
     * that what is kept is what was actually handed to GL, in the same
     * conversions. */
    {
        int i;
        g_wu_sc      = (GLfloat) w->sc;
        g_wu_post    = (GLfloat) w->post;
        g_wu_org[0]  = (GLfloat) w->org[0];
        g_wu_org[1]  = (GLfloat) w->org[1];
        g_wu_org[2]  = (GLfloat) w->org[2];
        for (i = 0; i < 16; i++) g_wu_C[i] = (GLfloat) w->C[i];
        g_wu_fogless = w->fogless ? 1 : 0;
    }
    g_walk_live = 1;
    return 1;
}

/* Find a slot already holding this exact key, or -1. */
static int tf_find(int dmode, unsigned k1b, unsigned k2b)
{
    int i;
    for (i = 0; i < TF_SLOTS; i++)
        if (g_slot[i].filled && g_slot[i].gen == g_gen && g_slot[i].dmode == dmode
            && g_slot[i].k1bits == k1b && g_slot[i].k2bits == k2b
            && g_slot[i].nverts == g_nverts)
            return i;
    return -1;
}

/* Which slot to fill: a free one, then a slot from an older walk, then the
 * least recently used. Choosing an older walk's slot before a current one is
 * what keeps a frame that needs three pools from evicting a pool it is still
 * going to draw from. */
static int tf_victim(void)
{
    int i, best = 0;
    unsigned long oldest;
    for (i = 0; i < TF_SLOTS; i++)
        if (!g_slot[i].filled) return i;
    for (i = 0; i < TF_SLOTS; i++)
        if (g_slot[i].gen != g_gen) return i;
    oldest = g_slot[0].used;
    for (i = 1; i < TF_SLOTS; i++)
        if (g_slot[i].used < oldest) { oldest = g_slot[i].used; best = i; }
    return best;
}

/* ---- THE POSITION-ONLY-INPUT ISOLATION (SL_BG_TF_POS_INPUT) -------------
 *
 * The saved state, its bounded unit ceiling and what is deliberately absent from
 * it are declared with the module's other statics above.
 *
 * ISOLATE THE PREPASS DRAW'S CLIENT INPUTS TO POSITION. 1 if the isolation is in
 * force and tf_pos_restore must run, 0 if the draw is to proceed exactly as it
 * does without this control.
 *
 * EVERY CAPABILITY QUESTION IS SETTLED BEFORE ANY STATE IS WRITTEN, which is
 * what makes "never partially" a property of the order rather than a promise:
 * the entry point is resolved, the coordinate domain is bounded and the
 * inherited selector is validated while nothing has been disabled and no unit
 * has been selected, so a refusal returns with the context untouched. After that
 * point nothing can fail - glIsEnabled and glDisableClientState have no failure
 * this code can observe - so there is no path on which some arrays are off and
 * not restored. */
static int tf_pos_isolate(void)
{
    GLint coords = 0, u, saved_unit, index;

    if (!sl_bgtf_pos_input()) return 0;

    if (p_ClientActiveTexture == NULL)
        p_ClientActiveTexture =
            (SL_TF_CLIENTACTIVETEXTURE) sl_gl_proc("glClientActiveTexture");
    if (p_ClientActiveTexture == NULL) {
        /* NO PER-UNIT SELECTOR, SO NO ISOLATION AT ALL. Disabling only the
         * arrays that need no selector would leave every unit's coordinate
         * array feeding the draw, which is a different experiment wearing this
         * one's name. The draw runs as it would without this control. */
        g_c.pi_refusals++;
        if (!g_pin_said) {
            g_pin_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_POS_INPUT: glClientActiveTexture does not "
                            "resolve, so the per-unit coordinate arrays cannot be "
                            "accounted for - the prepass draw runs with its inherited "
                            "client arrays and NO isolation is claimed\n");
        }
        return 0;
    }

    /* THE DOMAIN IS THE COORDINATE ARRAYS', NOT THE CONVENTIONAL UNITS'.
     *
     * GL_MAX_TEXTURE_COORDS is the number of texture COORDINATE SETS, which is
     * what glClientActiveTexture selects among and therefore how many per-unit
     * coordinate arrays can be enabled. GL_MAX_TEXTURE_UNITS counts the
     * conventional fixed-function units, and the OpenGL 2.1 spec allows it to be
     * SMALLER: section 2.7 gives the coordinate sets as GL_MAX_TEXTURE_COORDS,
     * section 2.8 ranges glClientActiveTexture's argument over those sets, and
     * section 2.11.2 separates the conventional unit count from them. The
     * citation is the OpenGL 2.1 specification,
     * https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf (printed pages
     * 21, 26 and 47; PDF pages 34, 39 and 60). Sweeping the unit count
     * on a context where it is the smaller number would leave the coordinate
     * arrays above it enabled and feeding the draw - partial isolation reported
     * as isolation - so the larger, actually-selectable domain is the one swept
     * and there is NO fallback or clamp to the smaller count.
     *
     * IT MUST BE ANSWERED, NOT ASSUMED. `coords` is seeded with a value the
     * query cannot legally produce, so a query that writes nothing is
     * distinguishable from one that answers. A context that will not say how
     * wide the domain is cannot have its coordinate arrays accounted for, and
     * guessing one would sweep set 0 and leave the rest feeding the draw. The
     * sky batch clamps such a context to one unit because it goes on to install
     * its own arrays on unit 0 regardless; this control has nothing to install
     * and only a claim to make, so it refuses instead. */
    coords = -1;
    glGetIntegerv(GL_MAX_TEXTURE_COORDS, &coords);
    if (coords < 1) {
        g_c.pi_refusals++;
        if (!g_pin_said) {
            g_pin_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_POS_INPUT: the context did not answer "
                            "GL_MAX_TEXTURE_COORDS (%d), so the per-unit coordinate "
                            "arrays cannot be accounted for - the prepass draw runs "
                            "with its inherited client arrays and NO isolation is "
                            "claimed\n", (int) coords);
        }
        return 0;
    }
    if (coords > TF_UNITS) {
        g_c.pi_refusals++;
        if (!g_pin_said) {
            g_pin_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_POS_INPUT: the context reports %d texture "
                            "coordinate sets, past this control's bounded ceiling of "
                            "%d - sweeping part of the range would leave the rest "
                            "feeding the draw, so the prepass draw runs with its "
                            "inherited client arrays and NO isolation is claimed\n",
                    (int) coords, TF_UNITS);
        }
        return 0;
    }

    /* THE INHERITED SELECTOR IS READ AND VALIDATED BEFORE ANYTHING IS WRITTEN.
     *
     * This read is the last capability question, and it is asked here - ahead of
     * the array reads below, which are harmless, and well ahead of the per-unit
     * loop, which MOVES the selector - because the restore depends on the
     * answer. A query that writes nothing leaves the sentinel; an answer outside
     * GL_TEXTURE0 .. GL_TEXTURE0 + coords - 1 is not a set this context can
     * select among, and 0 is one such answer. Either way the value cannot be put
     * back, so isolating would mean disabling arrays and then restoring the
     * selector to something invalid while reporting a successful isolation.
     * Nothing has been selected or disabled at this point, so the refusal leaves
     * the context exactly as it was. */
    saved_unit = TF_CLI_UNIT_NONE;
    glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &saved_unit);
    index = saved_unit - GL_TEXTURE0;
    if (saved_unit == TF_CLI_UNIT_NONE || index < 0 || index >= coords) {
        g_c.pi_refusals++;
        if (!g_pin_said) {
            g_pin_said = 1;
            fprintf(stderr, "#95 SL_BG_TF_POS_INPUT: the context did not answer "
                            "GL_CLIENT_ACTIVE_TEXTURE with a selectable coordinate "
                            "set (%d, expected GL_TEXTURE0 + 0..%d) - the inherited "
                            "selection could not be put back, so the prepass draw "
                            "runs with its inherited client arrays and NO isolation "
                            "is claimed\n", (int) saved_unit, (int) (coords - 1));
        }
        return 0;
    }

    memset(&g_cli, 0, sizeof g_cli);
    g_cli.coords = coords;
    g_cli.client_unit = saved_unit;

    /* THE SAVE. */
    g_cli.c_on = glIsEnabled(GL_COLOR_ARRAY);
    g_cli.n_on = glIsEnabled(GL_NORMAL_ARRAY);
    g_cli.i_on = glIsEnabled(GL_INDEX_ARRAY);
    g_cli.e_on = glIsEnabled(GL_EDGE_FLAG_ARRAY);
    g_cli.s_on = glIsEnabled(GL_SECONDARY_COLOR_ARRAY);
    g_cli.f_on = glIsEnabled(GL_FOG_COORD_ARRAY);
    for (u = 0; u < coords; u++) {
        p_ClientActiveTexture((GLenum) (GL_TEXTURE0 + u));
        g_cli.t_on[u] = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
    }

    /* THE DISABLE, over exactly what was found enabled. An array that was off
     * is not touched, so the count is the exposure actually removed rather than
     * the number of arrays this control knows about. */
    for (u = coords - 1; u >= 0; u--) {
        if (!g_cli.t_on[u]) continue;
        p_ClientActiveTexture((GLenum) (GL_TEXTURE0 + u));
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        g_cli.off++;
    }
    if (g_cli.c_on) { glDisableClientState(GL_COLOR_ARRAY);           g_cli.off++; }
    if (g_cli.n_on) { glDisableClientState(GL_NORMAL_ARRAY);          g_cli.off++; }
    if (g_cli.i_on) { glDisableClientState(GL_INDEX_ARRAY);           g_cli.off++; }
    if (g_cli.e_on) { glDisableClientState(GL_EDGE_FLAG_ARRAY);       g_cli.off++; }
    if (g_cli.s_on) { glDisableClientState(GL_SECONDARY_COLOR_ARRAY); g_cli.off++; }
    if (g_cli.f_on) { glDisableClientState(GL_FOG_COORD_ARRAY);       g_cli.off++; }

    /* THE INHERITED SELECTION IS PUT BACK BEFORE THE DRAW, so the draw runs
     * under exactly the unit selection the pass had. The selection does not
     * decide what a draw reads - only which unit a later client coordinate
     * command addresses - but leaving it moved across the draw would be this
     * control altering state the draw did not need changed. */
    p_ClientActiveTexture((GLenum) g_cli.client_unit);

    g_cli.held = 1;
    g_c.pi_draws++;
    g_c.pi_off += g_cli.off;
    if ((unsigned long) coords > g_c.pi_units)
        g_c.pi_units = (unsigned long) coords;
    return 1;
}

/* PUT BACK EXACTLY WHAT WAS FOUND ENABLED, and nothing else.
 *
 * ONLY ARRAYS THAT WERE ON ARE TURNED BACK ON. Enabling the set this control
 * knows about, rather than the set it found, would hand the raster that follows
 * an array the pass never asked for - with whatever pointer it happened to hold.
 * `pi_back` counts the re-enables, and it equals `pi_off` on every honest run,
 * so a missed or an invented restore shows up as a number rather than as a
 * picture nobody can account for.
 *
 * THE INHERITED UNIT IS RESTORED LAST, after every unit this function selected,
 * so nothing above can leave it moved. */
static void tf_pos_restore(void)
{
    GLint u;

    if (!g_cli.held) return;
    g_cli.held = 0;

    for (u = 0; u < g_cli.coords; u++) {
        if (!g_cli.t_on[u]) continue;
        p_ClientActiveTexture((GLenum) (GL_TEXTURE0 + u));
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        g_c.pi_back++;
    }
    p_ClientActiveTexture((GLenum) g_cli.client_unit);

    if (g_cli.c_on) { glEnableClientState(GL_COLOR_ARRAY);           g_c.pi_back++; }
    if (g_cli.n_on) { glEnableClientState(GL_NORMAL_ARRAY);          g_c.pi_back++; }
    if (g_cli.i_on) { glEnableClientState(GL_INDEX_ARRAY);           g_c.pi_back++; }
    if (g_cli.e_on) { glEnableClientState(GL_EDGE_FLAG_ARRAY);       g_c.pi_back++; }
    if (g_cli.s_on) { glEnableClientState(GL_SECONDARY_COLOR_ARRAY); g_c.pi_back++; }
    if (g_cli.f_on) { glEnableClientState(GL_FOG_COORD_ARRAY);       g_c.pi_back++; }
}

/* ---- THE PRIVATE PREPASS VERTEX ARRAY (SL_BG_TF_PRIVATE_VAO) -------------
 *
 * The one object, its sentinel and what is held across a setup are declared with
 * the module's other statics above.
 *
 * GIVE THIS SETUP AND ITS DRAW THEIR OWN ARRAY OBJECT. 1 if the private object
 * is bound and tf_vao_release must run, 0 if the setup is to proceed exactly as
 * it does without this control.
 *
 * EVERY CAPABILITY QUESTION IS SETTLED BEFORE ANY STATE IS WRITTEN, which is
 * what makes "never half active" a property of the order rather than a promise.
 * The two entry points are resolved, the inherited binding is read and validated,
 * and the object is generated - which writes no binding - while nothing has been
 * bound. Only then is the bind issued, and the ONE thing that can still go wrong
 * after it is the bind itself not taking: that is read back, and a bind the
 * context does not confirm puts the inherited binding straight back and refuses.
 *
 * A REFUSAL IS NEVER ISOLATION AND NEVER WORK. On every 0 answer the setup below
 * runs precisely as it does without this control - writing the inherited array
 * state, exactly as it does today - the refusal is counted, and nothing is
 * claimed. glGetError is not called on any path here, so no error flag is
 * consumed and no refusal is inferred from one.
 *
 * WHAT THIS DOES NOT DO. The ARRAY_BUFFER binding and the client active texture
 * selection are not array-object state (ARB_vertex_array_object), so this
 * function neither saves nor touches them: the existing bind/unbind pair around
 * the pointer call remains the thing that leaves the global ARRAY_BUFFER binding
 * at 0, and the client selector is untouched here just as it is without this
 * control. No claim is made that the object isolates the current attributes or
 * all GL state. */
/* A REFUSAL IS ONLY FOR A KNOWN STATE, AND THE MESSAGE SAYS WHICH KNOWN STATE.
 *
 * `state` is the second half of the sentence and is NOT boilerplate: a single
 * blanket clause saying the inherited setup is in force was wrong on the one
 * path where the binding had been written and not confirmed, which is exactly
 * the case where a reader most needs to know. Every caller below passes the
 * clause true of its own path, and the path where the state is UNKNOWN does not
 * reach this function at all - it is terminal (tf_vao_terminal). */
static int tf_vao_refuse(const char *why, const char *state)
{
    g_c.pv_refusals++;
    if (!g_pvao_said) {
        g_pvao_said = 1;
        fprintf(stderr, "#95 SL_BG_TF_PRIVATE_VAO: %s - %s, so the prepass setup "
                        "runs exactly as it does without this control and NO private "
                        "vertex array and NO isolation is claimed\n", why, state);
    }
    return 0;
}

/* THE RENDERER'S EXISTING NARROW FATAL. No header declares it; every caller
 * declares it locally, which is what src/gfx/sl_gfx_dl.c:18106 does for its own
 * two uses at :18159 and :18167. Defined at
 * src/platform/sl_ultra_shim.c:3493-3496 as fprintf(stderr, fmt, v) followed by
 * exit(2), so the signature is (const char *, unsigned) and THE CALL DOES NOT
 * RETURN. The only other definition in the tree is a self-test stub in
 * src/native/sl_swap_model.c:623, which is inside #ifdef SL_SWAP_SELFTEST and
 * belongs to a different fixture's translation unit. */
void sl_fatalf(const char *fmt, unsigned v);

/* AN UNPROVABLE CALLER BINDING ENDS THIS PROCESS INSTEAD OF RENDERING.
 *
 * WHY A REFUSAL IS THE WRONG ANSWER HERE. Every other failure in this control
 * leaves a state somebody can name: nothing was bound, or the inherited object
 * was read back and confirmed current. This one does not. When a restore - or
 * the cleanup bind after a bind that did not take - is issued and the context
 * will not confirm the saved value, the array object the next draw would read
 * its descriptors from is UNKNOWN. Falling back then would mean the caller
 * reinstating its CPU position descriptor, and the pass rasterising, on whatever
 * object happens to be current, with a counter recording a refusal as though the
 * state were understood. Reporting without preventing that is the defect this
 * replaces.
 *
 * SO NOTHING AFTER THIS POINT RUNS. No further prepass, no cache publication and
 * no marking of a slot filled or written, no GL cleanup, no CPU descriptor
 * update and no draw: the process ends here. The marker is printed and the
 * streams are flushed BEFORE the fatal, because a terminal claim that never
 * reached the log is not evidence.
 *
 * THE SCOPE IS THE EXPLICITLY ENABLED DIAGNOSTIC AND NOTHING ELSE. This is
 * reachable only from a launch that set SL_BG_TF_PRIVATE_VAO=1 together with
 * SL_BG_TF_CLIP=1 and SL_BG_TF_RASTER_CPU=1. A default launch, an off launch and
 * a CONFIRMED fallback never reach it, and no process other than this one is
 * affected. sl_quit_request is deliberately NOT used: it is examined at the next
 * frame boundary, so the current frame would go on to raster on the unknown
 * object, which is the whole thing being prevented.
 *
 * DOES NOT RETURN. The abort below is unreachable on the real path and is the
 * structural guard for that sentence: if sl_fatalf were ever replaced by
 * something that returned - a stub, or a callback - a terminal failure must
 * still not become a continuation. It also makes a masked fatal detectable from
 * outside, because the process would then end with abort's status instead of
 * the fatal's 2. */
static void tf_vao_terminal(const char *why)
{
    g_c.pv_terminal++;
    /* THE COUNTER, THE STATUS AND THE REASON STAY TRUTHFUL. No restore is
     * counted, because none was proven; the module latches UNAVAILABLE, because
     * it is; and the reason carries this failure's own words rather than a
     * previous one's. */
    g_state = -1;
    snprintf(g_why, sizeof g_why,
             "private vertex array: %s", why);
    fprintf(stderr, "#95 SL_BG_TF_PRIVATE_VAO: REFUSED - %s. The vertex array "
                    "object the following raster would take its descriptors from "
                    "is UNKNOWN, so this process STOPS here rather than "
                    "rastering, publishing a pool, marking a slot filled or "
                    "written, cleaning up GL state or letting the caller restore "
                    "a CPU descriptor on state nobody can name. A refusal would "
                    "have reported this and allowed all of it. This is the "
                    "explicitly enabled diagnostic ending ITSELF: a default or "
                    "off launch and a CONFIRMED fallback are unaffected, and no "
                    "other process is touched\n", why);
    fflush(stderr);
    fflush(NULL);
    sl_fatalf("#95 SL_BG_TF_PRIVATE_VAO: REFUSED - terminal, unprovable caller "
              "vertex array binding (%u); stopping\n",
              (unsigned) g_c.pv_terminal);
    abort();
}

static int tf_vao_acquire(void)
{
    GLint saved, now;

    if (!sl_bgtf_private_vao()) return 0;

    if (p_GenVertexArrays == NULL || p_BindVertexArray == NULL) {
        p_GenVertexArrays =
            (SL_TF_GENVERTEXARRAYS) sl_gl_proc("glGenVertexArrays");
        p_BindVertexArray =
            (SL_TF_BINDVERTEXARRAY) sl_gl_proc("glBindVertexArray");
    }
    if (p_GenVertexArrays == NULL || p_BindVertexArray == NULL)
        return tf_vao_refuse("glGenVertexArrays or glBindVertexArray does not "
                             "resolve, so there is no vertex array object to give "
                             "the prepass",
                             "nothing was generated and NO binding was written, so "
                             "the inherited vertex array state is untouched and "
                             "known");

    /* THE INHERITED BINDING IS READ AND VALIDATED BEFORE ANYTHING IS WRITTEN,
     * and 0 is a VALID answer rather than the absence of one. Object 0 is the
     * default array object and an ordinary thing for a caller to have current,
     * so the sentinel is negative: a query that writes nothing leaves it, and a
     * context that will not state what is bound cannot have that binding put
     * back. Binding anyway would mean isolating a draw and then restoring the
     * caller to a guess - and a caller with its OWN object current is exactly
     * the case a guess of 0 would corrupt. Nothing has been bound at this point,
     * so the refusal leaves the context exactly as it was. */
    saved = TF_VAO_NONE;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved);
    if (saved < 0)
        return tf_vao_refuse("the context did not answer GL_VERTEX_ARRAY_BINDING, "
                             "so the inherited vertex array object could not be put "
                             "back",
                             "the question was asked BEFORE anything was bound, so "
                             "no binding was written and whatever the caller had "
                             "current is still current");

    if (g_pvao_name == 0u) {
        /* ONE OBJECT PER CONTEXT, generated on the first setup that needs it.
         * glGenVertexArrays only reserves a name - it binds nothing - so a
         * failure here leaves no state to put back. A context that will not
         * give a name is a refusal and not an empty object: an unallocated name
         * is 0, which is the DEFAULT object, and binding that would isolate
         * nothing while reporting that it had. */
        p_GenVertexArrays(1, &g_pvao_name);
        if (g_pvao_name == 0u)
            return tf_vao_refuse("the context would not allocate a vertex array "
                                 "object (glGenVertexArrays gave the name 0, which "
                                 "is the DEFAULT object and would isolate nothing)",
                                 "glGenVertexArrays reserves a name and binds "
                                 "nothing, so NO binding was written and the "
                                 "inherited vertex array state is untouched and "
                                 "known");
    }

    p_BindVertexArray(g_pvao_name);
    /* THE BIND IS CONFIRMED, NOT ASSUMED. glBindVertexArray is void, so the only
     * way to learn the private object is actually current is to ask. An
     * unconfirmed bind would leave the setup below writing the CALLER's array
     * state while this control counted an isolation, which is the one false
     * claim this control must not be able to make. A bind that did not take is
     * put back immediately - to the binding that was read above, whatever it was
     * - and refused. */
    now = TF_VAO_NONE;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &now);
    if (now != (GLint) g_pvao_name) {
        /* THE CLEANUP IS ONE BIND AND ONE CONFIRMATION, AND THE ANSWER DECIDES
         * WHICH KIND OF FAILURE THIS IS.
         *
         * A bind was issued, so unlike every refusal above this path has written
         * to the binding and cannot assume anything about it. The saved value is
         * named once and read back once - no retry loop, because a context that
         * did not honour one bind is not more trustworthy on a second, and a
         * loop would turn an unknown state into an unbounded one.
         *
         * CONFIRMED: the inherited object is current again, the state is known,
         * and the ordinary fallback may continue exactly as it does for the
         * refusals above. NOT CONFIRMED - whether the readback names another
         * object or writes nothing at all, which the negative sentinel makes the
         * same answer - the binding the following raster would read is UNKNOWN
         * and this is terminal. */
        GLint back = TF_VAO_NONE;
        p_BindVertexArray((GLuint) saved);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &back);
        if (back != saved)
            tf_vao_terminal("the private vertex array object was bound, the "
                            "context did not confirm it, and the one cleanup bind "
                            "back to the inherited object was NOT confirmed "
                            "either");
        return tf_vao_refuse("the context did not confirm the private vertex array "
                             "object as bound, so the setup would have written the "
                             "CALLER's array state",
                             "the one cleanup bind back to the inherited object WAS "
                             "confirmed by a read-back, so the inherited vertex "
                             "array object is current again and the state is known");
    }

    /* ONLY THE POSITION ARRAY IS ENABLED ON IT. A freshly generated object has
     * every array disabled, and the prepass program reads gl_Vertex and nothing
     * else, so this one enable is the whole of what the draw needs - and every
     * other array stays off rather than being disabled, which is a different
     * and weaker statement. The enable is an array-object property, so it lands
     * on the private object and the caller's enables are untouched. */
    glEnableClientState(GL_VERTEX_ARRAY);

    g_pvao_saved = saved;
    g_pvao_held = 1;
    g_c.pv_binds++;
    if (saved != 0) g_c.pv_nonzero++;
    return 1;
}

/* PUT THE INHERITED BINDING BACK, AND COUNT IT ONLY IF THE CONTEXT CONFIRMS IT.
 *
 * THE EXACT INHERITED OBJECT, INCLUDING A NONZERO ONE. The value put back is the
 * one that was read before the bind, not 0: a caller that had its own array
 * object current must find it current again, and restoring 0 to such a caller
 * would be this control corrupting the state it exists to protect.
 *
 * A RESTORE IS A CLAIM, SO IT IS READ BACK, AND AN UNCONFIRMED ONE IS TERMINAL.
 * `pv_restores` counts restores the context agreed to, so it is never inflated
 * by one that did not happen and it equals pv_binds on an honest run. A restore
 * the context will NOT confirm is a different thing from a refusal: the object
 * the caller's own descriptor restoration and the raster that follows would read
 * is unknown, so this does not return - see tf_vao_terminal, and note that this
 * function is called BEFORE the slot is marked filled or written and before the
 * caller reinstates its position stream, which is what makes "nothing after the
 * terminal point runs" a property of the order.
 *
 * THIS FUNCTION WRITES NO ELEMENT BINDING, AND THAT CLAIM IS ABOUT THE PRIVATE
 * PREPASS BRACKET ONLY. The element binding the prepass drops is dropped inside
 * the bracket (:1644), on the private object, and this control adds no element
 * write outside it. It does NOT follow that the inherited object's element
 * binding is untouched for the rest of the walk: sl_bgtf_unbind still clears the
 * element binding at the walk's end (:1833), exactly as it did before this
 * control existed, and that is unchanged on purpose. Nor is that a demonstrated
 * defect in an ordinary draw - src/gfx/sl_gfx_dl.c admits a walk only when
 * g_bgv_live (:19055 rejects !g_bgv_live before the walk begins), and every
 * admitted draw binds g_bgv_index for itself (:17078) and unbinds it (:17082).
 * No whole-lifecycle immutability of the caller's array object is claimed by
 * this control. */
static void tf_vao_release(void)
{
    GLint now;

    if (!g_pvao_held) return;
    g_pvao_held = 0;

    p_BindVertexArray((GLuint) g_pvao_saved);
    now = TF_VAO_NONE;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &now);
    /* ONE CONFIRMATION, AND NO RETRY. A context that did not honour this bind is
     * not more trustworthy on a second one, and a loop would convert an unknown
     * binding into an unbounded wait. The negative sentinel makes "it named
     * another object" and "it wrote nothing" the same answer, which is right:
     * both leave the binding unproven. */
    if (now != g_pvao_saved)
        tf_vao_terminal("the one bind back to the inherited vertex array object "
                        "was issued and the context did NOT confirm it");
    g_c.pv_restores++;
    g_pvao_saved = TF_VAO_NONE;
}

int sl_bgtf_prepass(int dmode, float k1, float k2)
{
    unsigned k1b, k2b;
    int s, state_only, private_vao, zero_count;
    /* THE MODE AND THE CONDITIONS IT NAMES, read once per served draw and DERIVED
     * from the mode by the one table. `es_tf` and `es_discard` are the state AT
     * the empty draw; the four switch answers are the glUseProgram calls the row
     * issues around it. With no mode selected the table answers the zero-count
     * arm's own 1/1 and four zeroes, so every branch below is a no-op and the
     * sequence is byte-for-byte the one this function has without the mode.
     *
     * `es_cur_prog` IS WHAT THIS PROCESS MADE CURRENT, not a state read. The
     * common setup below leaves the prepass program current on every arm, so it
     * starts at 1 and each switch updates it; the draw counts under whichever
     * program the calls above it actually selected. Nothing is read back from
     * GL. */
    int es_mode, es_tf, es_discard;
    int es_pre_zero, es_pre_prog, es_post_zero, es_post_prog;
    int es_cur_prog = 1;
    /* THE POST-RESET BOUNDARY CONTROL'S ARM AND ITS FOUR PRECONDITIONS
     * (SL_BG_TF_POST_RESET), default off.
     *
     * `pr_mode` is the arm; `pr_draw` and `pr_finish` are the two calls it
     * issues, decoded by the one table. With no arm selected both are 0, every
     * branch below is a no-op and the sequence is byte-for-byte the one this
     * function has without the control.
     *
     * THE FOUR FLAGS ARE WHAT THIS BRACKET ISSUED, NOT WHAT GL REPORTS. Each is
     * raised immediately after the command it records, and the boundary runs
     * only with all four raised - so a bracket that took an early return, left a
     * feedback scope open, or had the common restoration edited out of it
     * issues NOTHING at the boundary instead of issuing a draw into a state
     * nobody accounted for. No glGet of any kind is added to decide this: a
     * driver's answer is not what the precondition is about, and the rest of
     * this module does not read state back either.
     *
     *   pr_drew      the prepass glDrawArrays was issued in this bracket
     *   pr_tf_open   a feedback scope is OPEN - the one flag that must be 0
     *   pr_disc_off  the common bracket's glDisable(GL_RASTERIZER_DISCARD) ran
     *   pr_prog0     the common bracket's glUseProgram(0) ran
     *
     * `es_cur_prog` carries the last program this bracket made current, through
     * the common setup, the empty-draw row's own switches and the common
     * restoration alike, so the boundary can require program 0 to be current
     * from the calls it issued rather than from the order the source happens to
     * be in. */
    int pr_mode, pr_draw = 0, pr_finish = 0;
    int pr_drew = 0, pr_tf_open = 0, pr_disc_off = 0, pr_prog0 = 0;
    /* THE NEUTRAL-PROGRAM CONTROL'S ANSWER AND ITS ONE RECORD
     * (SL_BG_TF_NEUTRAL_PROG), default off.
     *
     * `neutral` is 1 only when the control is in force, the site's own
     * preconditions hold and a linked neutral program exists. With it 0 every
     * branch below is a no-op and the sequence is byte-for-byte the one this
     * function has without the control.
     *
     * `np_cur` IS WHAT THIS PROCESS MADE CURRENT, not a state read: 1 while the
     * NEUTRAL program is the last program this bracket selected. It is raised
     * immediately after the switch and cleared immediately after the restore, so
     * the draw counts under whichever program the calls above it actually
     * selected rather than under the switch's name. Nothing is read back from
     * GL. */
    int neutral = 0, np_cur = 0;
    /* THE AUXILIARY-CONTEXT CONTROL'S ANSWER AND ITS TWO RECORDS
     * (SL_BG_TF_AUX_EMPTY), default off.
     *
     * `aux` is 1 only when the control is in force, the site's own preconditions
     * hold and a VERIFIED auxiliary context exists. With it 0 every branch below
     * is a no-op and the sequence is byte-for-byte the one this function has
     * without the control.
     *
     * `ax_cur` IS WHAT THIS PROCESS MADE CURRENT, not a query: 1 while the
     * AUXILIARY context is the one this bracket last switched to. It is raised
     * immediately after a SUCCESSFUL switch and cleared immediately after a
     * successful switch back, so the draw counts under whichever context the
     * calls above it actually established rather than under the switch's name.
     *
     * `ax_bad` IS THE BRACKET'S OWN REFUSAL, raised when a switch fails. The
     * control cannot refuse by returning from the middle of the bracket - the
     * common setup has already written state that has to be put back - so the
     * site records the refusal, issues nothing, lets the ordinary restoration
     * run and refuses at the end. `ax_dead` is the stronger case: a lost main
     * context, where the restoration itself must NOT run, because it would issue
     * its calls on a context nobody selected. */
    int aux = 0, ax_cur = 0, ax_bad = 0, ax_dead = 0;
    /* THE REAL-PREPASS MODE'S ARM AND ITS FOUR RECORDS (SL_BG_TF_AUX_REAL),
     * default off.
     *
     * `ar_mode` is the arm and `arl` is 1 only when an arm is in force, the
     * site's own preconditions hold and a VERIFIED auxiliary context with every
     * entry point and the array object this mode needs exists on it. With `arl`
     * 0 every branch below is a no-op and the sequence is byte-for-byte the one
     * this function has without the mode.
     *
     * `ar_cur` IS WHAT THIS PROCESS MADE CURRENT, not a query, on exactly the
     * terms `ax_cur` is: raised immediately after a SUCCESSFUL switch and
     * cleared immediately after a successful switch back, so the draw counts
     * under whichever context the calls above it actually established.
     *
     * `ar_wrote` IS THE WHOLE CHAIN HELD. It is raised at one place only - after
     * the output handoff's producer half on the auxiliary context, which is a
     * completion glFinish under FINISH and a fence with its producer flush under
     * FENCE, then the verified switch back, then the consumer half on the main
     * context under FENCE, then the output pool's re-bind there - and it is the
     * ONLY thing that marks the slot written. Under FENCE it is additionally
     * gated on both cross-context waits having been ISSUED, which is an ordering
     * this process established and not a completion it observed. So a pool whose
     * visibility was not
     * established cannot be bound, rasterised or recorded from any caller.
     *
     * `ar_bad` IS THE BRACKET'S OWN REFUSAL and `ar_dead` the stronger case, the
     * two meaning exactly what `ax_bad` and `ax_dead` mean: the first leaves the
     * MAIN context verified current so the ordinary restoration runs and the
     * refusal is taken at the end, and the second must NOT let that restoration
     * run at all. */
    int ar_mode, arl = 0, ar_cur = 0, ar_wrote = 0, ar_bad = 0, ar_dead = 0;
    struct tf_slot *sl;

    if (g_state <= 0 || !g_walk_live) { g_c.refusals++; return -1; }
    if (dmode != SL_BGGPU_DEPTH_SINGLE && dmode != SL_BGGPU_DEPTH_FAR0
        && dmode != SL_BGGPU_DEPTH_FAR1) { g_c.refusals++; return -1; }
    /* Read once per served draw and cached inside the switch, so a refusal above
     * pays nothing. The value is a launch constant: it cannot change between the
     * draws of a walk, which is what lets one slot's `written` tag answer for the
     * whole of it. */
    state_only = sl_bgtf_state_only();
    /* Read the same way and at the same point, for the same reason: the answer
     * is a launch constant and a refusal above pays nothing for it. With the
     * control off this is one cached integer test per served draw and no call of
     * any kind is added below. */
    private_vao = sl_bgtf_private_vao();
    /* Read the same way and at the same point, for the same reason: a launch
     * constant, and a refusal above pays nothing for it. With the control off
     * this is one cached integer test per served draw and no call of any kind is
     * added below. */
    zero_count = sl_bgtf_zero_count();
    /* Read the same way and at the same point, for the same reason: the mode is
     * a launch constant and a refusal above pays nothing for it. With no mode
     * selected this is one cached integer test plus one pure table read per
     * served draw, and no call of any kind is added below. */
    es_mode = sl_bgtf_empty_state();
    sl_bgtf_es_matrix(es_mode, &es_tf, &es_discard, NULL);
    sl_bgtf_es_switches(es_mode, &es_pre_zero, &es_pre_prog, &es_post_zero,
                        &es_post_prog);
    /* Read the same way and at the same point, for the same reason: the arm is a
     * launch constant and a refusal above pays nothing for it. With no arm
     * selected this is one cached integer test plus one pure table read per
     * served draw, and no call of any kind is added below. */
    pr_mode = sl_bgtf_post_reset();
    sl_bgtf_pr_calls(pr_mode, &pr_draw, &pr_finish);

    /* v0.4 THE NEUTRAL-PROGRAM CONTROL'S SITE PRECONDITIONS AND ITS CAPABILITY
     * (SL_BG_TF_NEUTRAL_PROG), default off: one cached integer test and nothing
     * else when it is off.
     *
     * HERE, BEFORE THE CACHE IS CONSULTED AND BEFORE ANY GL STATE IS WRITTEN,
     * which is what makes a refusal cost nothing and leave nothing behind. Every
     * question that can refuse this control is asked at a point where no program
     * is current, no uniform has been written, discard is off and no binding has
     * been made - so a served draw that is refused returns with the context
     * exactly as the caller handed it over and the mesh goes down the CPU path,
     * which is what every other prepass refusal does. Asking later would mean
     * either unwinding a half-built bracket or issuing the draw under the
     * prepass program, and the second of those is row A under this control's
     * name.
     *
     * THE SITE'S OWN GUARD, FROM THE ANSWERS THIS PROCESS COMPUTED. The arbiter
     * already refuses every one of these combinations, so on a shipped build
     * this is belt and braces - the same reason the post-reset boundary requires
     * `!private_vao` that its own arbiter clause already guarantees. What it buys
     * is that the property holds whether or not the arbiter is consulted: the
     * call this control re-programs must be the EMPTY draw (`zero_count`), no
     * empty-draw row may be varying the same call's program (`es_mode` OFF), and
     * none of the three older controls may be bracketing it.
     *
     * A SHORTFALL REFUSES THE SERVED DRAW AND ISSUES NOTHING. It is counted as
     * np_skips and the draw is not made at all, rather than made under whatever
     * program the bracket would have left current. */
    if (sl_bgtf_neutral_prog()) {
        if (!zero_count || es_mode != SL_BGTF_ES_OFF || state_only
            || private_vao || sl_bgtf_pos_input()) {
            g_c.np_skips++;
            g_c.refusals++;
            return -1;
        }
        if (!tf_np_acquire()) {
            g_c.np_create_fails++;
            g_c.refusals++;
            return -1;
        }
        neutral = 1;
    }

    /* v0.4 THE AUXILIARY-CONTEXT CONTROL'S SITE PRECONDITIONS AND ITS
     * CAPABILITY (SL_BG_TF_AUX_EMPTY), default off: one cached integer test and
     * nothing else when it is off.
     *
     * HERE, BEFORE THE CACHE IS CONSULTED AND BEFORE ANY GL STATE IS WRITTEN,
     * for the reason the control above it does it here: a refusal then costs
     * nothing and leaves nothing behind. No program is current, no uniform has
     * been written, discard is off, no binding has been made and - the one that
     * matters most for this control - the MAIN context is current and this
     * process has issued no switch. So a served draw refused here returns with
     * the context exactly as the caller handed it over and the mesh goes down
     * the CPU path, which is what every other prepass refusal does. Creating a
     * GL context in the middle of a half-built bracket is the alternative, and
     * the other alternative is issuing the call on the main context, which is
     * the ordinary zero-count arm under this control's name.
     *
     * THE SITE'S OWN GUARD, FROM THE ANSWERS THIS PROCESS COMPUTED. The arbiter
     * already refuses every one of these combinations, so on a shipped build
     * this is belt and braces - what it buys is that the property holds whether
     * or not the arbiter is consulted. The call this control moves must be the
     * EMPTY draw (`zero_count`), no empty-draw row may be varying the same
     * call's conditions (`es_mode` OFF), and none of the other controls on that
     * draw may be bracketing it - the neutral-program control included, because
     * a bracket holding both would both rebind the program and move the context.
     *
     * A SHORTFALL REFUSES THE SERVED DRAW AND ISSUES NOTHING. It is counted as
     * ax_skips and no context is switched to, rather than a draw made wherever
     * the bracket would have left things. */
    if (sl_bgtf_aux_empty()) {
        if (!zero_count || es_mode != SL_BGTF_ES_OFF || state_only
            || private_vao || sl_bgtf_pos_input()
            || sl_bgtf_neutral_prog()) {
            g_c.ax_skips++;
            g_c.refusals++;
            return -1;
        }
        /* THE REFUSAL IS COUNTED HERE AND NOT INSIDE THE ACQUIRE, which is
         * where the neutral-program control counts its own and for the same
         * measured reason: the acquire LATCHES, so after the first failure it
         * returns from its cache without reaching any of its own clauses. A
         * count inside it would record the FIRST refused draw and silently drop
         * every later one, and "a failing context refuses every served draw" is
         * exactly what the count has to carry. The specific failure - a missing
         * entry point, no context, a failed switch, no sharing, a lost main
         * context - is counted once inside, where it is known. */
        if (!tf_ax_acquire()) {
            g_c.ax_create_fails++;
            g_c.refusals++;
            return -1;
        }
        aux = 1;
    }

    /* v0.4 THE REAL-PREPASS MODE'S SITE PRECONDITIONS AND ITS CAPABILITY
     * (SL_BG_TF_AUX_REAL), default off: one cached integer test and nothing else
     * when it is off.
     *
     * HERE, BEFORE THE CACHE IS CONSULTED AND BEFORE ANY GL STATE IS WRITTEN,
     * for the reason the two controls above do it here: a refusal then costs
     * nothing and leaves nothing behind. No program is current, no uniform has
     * been written, discard is off, no binding has been made and the MAIN
     * context is current with no switch issued. So a served draw refused here
     * returns with the context exactly as the caller handed it over and the mesh
     * goes down the CPU path.
     *
     * THE SITE'S OWN GUARD, FROM THE ANSWERS THIS PROCESS COMPUTED. The arbiter
     * already refuses every one of these combinations, so on a shipped build
     * this is belt and braces - what it buys is that the property holds whether
     * or not the arbiter is consulted. The count this mode relocates must be the
     * REAL NONZERO one (`!zero_count`), no empty-draw row may be varying the
     * conditions at a draw this bracket does not make here (`es_mode` OFF), and
     * none of the other controls on that draw may be bracketing it - the
     * empty-draw auxiliary control included, because two modes cannot both own
     * the second context's state in one bracket.
     *
     * AND THE ARM'S OWN RASTER SOURCE IS REQUIRED AT THE SITE TOO. CPU_SOURCE
     * needs the original-raster control in force and GPU_SOURCE needs it off,
     * which is what makes each arm's name true of the frame it produces rather
     * than only of the selector that asked for it.
     *
     * A SHORTFALL REFUSES THE SERVED DRAW AND ISSUES NOTHING. It is counted as
     * ar_skips, no context is switched to and no synchronisation point is
     * added. */
    ar_mode = sl_bgtf_aux_real();
    /* v0.4 AND AN UNREADABLE SELECTOR ON AN OPTED-IN LAUNCH REFUSES EVERY SERVED
     * DRAW HERE, WHICH IS WHERE FAILING CLOSED IS A REFUSAL AND NOT A WORD.
     *
     * The arbiter has already said this once per launch - for an unreadable arm
     * or an unreadable synchronisation mode, each with its own line - and this is
     * the site that acts on it. The refusal is taken at the same point every
     * other shortfall is taken: BEFORE the cache is consulted and before any GL
     * state of the bracket is written, with the main context current, no switch
     * issued, no program bound, no uniform written, discard off and no binding
     * made. So the mesh goes down the CPU raster path with the context exactly as
     * the caller handed it over, and nothing of either prepass has run.
     *
     * WITHOUT IT THIS LAUNCH WOULD RUN THE MAIN-CONTEXT PREPASS, because "the
     * auxiliary mode is off" is not "no prepass" on an opted-in launch. Both
     * latch cases leave the arm OFF, so `ar_mode` is necessarily OFF here and the
     * two branches cannot both be taken. */
    if (g_ar_closed) {
        g_c.ar_skips++;
        g_c.refusals++;
        return -1;
    }
    if (ar_mode != SL_BGTF_AR_OFF) {
        const int rcpu = sl_bgtf_raster_cpu();
        if (zero_count || es_mode != SL_BGTF_ES_OFF || state_only
            || private_vao || sl_bgtf_pos_input() || sl_bgtf_neutral_prog()
            || sl_bgtf_aux_empty()
            || (ar_mode == SL_BGTF_AR_CPU && rcpu != 1)
            || (ar_mode == SL_BGTF_AR_GPU && rcpu == 1)) {
            g_c.ar_skips++;
            g_c.refusals++;
            return -1;
        }
        /* THE REFUSAL IS COUNTED HERE AND NOT INSIDE THE ACQUIRE, for the
         * measured reason the two controls above give: the acquire LATCHES, so
         * after the first failure it returns from its cache without reaching any
         * of its own clauses, and a count inside it would record the FIRST
         * refused draw and silently drop every later one. The specific failure -
         * an entry point missing on that context, an unshared program name, no
         * array object, a failed switch - is counted once inside, where it is
         * known. */
        if (!tf_ar_acquire()) {
            g_c.ar_create_fails++;
            g_c.refusals++;
            return -1;
        }
        arl = 1;
    }

    /* AND THE CONDITIONS AT THE EMPTY DRAW ARE ROW A'S ON EITHER CONTROL, which
     * is the one line both of them need and the reason it sits outside both
     * blocks above rather than being written twice. Exactly one of them can be
     * in force - each site guard names the other - so this is a single
     * condition and not a precedence. */
    /* AND THE MAIN CONTEXT OPENS NO FEEDBACK SCOPE UNDER THE REAL-PREPASS MODE
     * (SL_BG_TF_AUX_REAL), under the same one condition that guards both
     * feedback calls. The capture this mode is about is opened and closed on the
     * AUXILIARY context, so a scope on this one would be a second, empty capture
     * held open across a context switch - and `es_tf` guarding the PAIR is what
     * makes omitting it structurally matched rather than matched by inspection.
     * The indexed output binding the common setup makes on this context is KEPT:
     * binding a buffer to that target is legal while no capture is active, and
     * leaving the common bracket otherwise untouched is what keeps every line of
     * it the statement it is on every other arm. */
    if (neutral || aux || arl) {
        /* AND THE CONDITIONS AT THE EMPTY DRAW ARE ROW A'S, SET BY THE ONE
         * CONDITION THAT GUARDS BOTH FEEDBACK CALLS. Row A is T0 D1: transform
         * feedback INACTIVE at the call and rasterizer discard ON. Discard is
         * already on for the whole bracket and `es_discard` is 1 with no row
         * selected, so nothing is done for D. For T, `es_tf` is the single
         * condition on both glBeginTransformFeedback and its matching end, so
         * clearing it here omits the PAIR - a begin without an end is invalid,
         * and one condition guarding both is what makes the pair structurally
         * matched rather than matched by inspection. An empty-draw row cannot be
         * in force here, so this is the only thing that writes `es_tf`. */
        es_tf = 0;
    }

    /* FAR1 AND SINGLE READ NEITHER COEFFICIENT, so they are keyed with the
     * coefficients zeroed. Keying them on whatever pair the caller happened to
     * pass would split one pool into several identical ones and run a prepass
     * per distinct pair for a value neither mode uses. */
    if (dmode == SL_BGGPU_DEPTH_FAR0) {
        k1b = f2bits(k1);
        k2b = f2bits(k2);
    } else {
        k1b = f2bits(0.0f);
        k2b = f2bits(0.0f);
    }

    s = tf_find(dmode, k1b, k2b);
    if (s >= 0) {
        /* THE CADENCE IS THE KEY'S, ON BOTH ARMS. A slot already carrying this
         * key serves this draw without a second setup, which is what keeps the
         * state-only arm's setup count comparable to the prepass count it is
         * being read against. The hit is counted as the control's own reuse
         * rather than as `reuses`, because `reuses` means a real pool was
         * reused and a setup-only slot has no pool to reuse. */
        g_slot[s].used = ++g_tick;
        /* v0.4 THE ZERO-VERTEX-COUNT CONTROL'S OWN REUSE COUNT
         * (SL_BG_TF_ZERO_COUNT), default off: one cached integer test and no
         * call. A HIT ADDS NOTHING AND FORCES NOTHING - no setup runs, no call
         * is issued, and the key is NOT invalidated to manufacture a miss. It is
         * counted here rather than as `reuses` for the reason the state-only
         * arm's is: `reuses` means a real pool was reused, and a slot this arm
         * keyed carries no output to reuse. */
        if (state_only)      g_c.so_reuses++;
        else if (zero_count) g_c.zc_reuses++;
        else                 g_c.reuses++;
        /* v0.4 THE EMPTY-DRAW STATE MODE'S OWN REUSE COUNT
         * (SL_BG_TF_EMPTY_STATE), default off: one cached integer test and no
         * call.
         *
         * A HIT ADDS NOTHING AND FORCES NOTHING - no setup, no empty draw, no
         * begin/end pair, no program or discard transition - and the key is NOT
         * invalidated to manufacture a miss. It is counted BESIDE zc_reuses
         * rather than instead of it, because the zero-count arm's own cadence
         * must stay readable on every one of these rows; es_setups + es_reuses
         * is then the complete served-mesh account for the mode. */
        if (es_mode != SL_BGTF_ES_OFF) g_c.es_reuses++;
        /* v0.4 THE POST-RESET BOUNDARY CONTROL'S OWN REUSE COUNT
         * (SL_BG_TF_POST_RESET), default off: one cached integer test and no
         * call.
         *
         * A HIT HAS NO BOUNDARY, so there is nothing for this control to do at
         * one. No setup bracket runs, no prepass draw is issued, and therefore
         * no point exists that is "after the prepass draw and before the raster
         * draw" for this mesh - the key is NOT invalidated to manufacture one.
         * It is counted BESIDE every other arm's reuse count rather than
         * instead of any, because this control composes with all of them;
         * pr_setups + pr_skips + pr_reuses is then the complete served-mesh
         * account for the control. */
        if (pr_mode != SL_BGTF_PR_OFF) g_c.pr_reuses++;
        /* v0.4 THE NEUTRAL-PROGRAM CONTROL'S OWN REUSE COUNT
         * (SL_BG_TF_NEUTRAL_PROG), default off: one cached integer test and no
         * call.
         *
         * A HIT HAS NO EMPTY DRAW, so there is no call for this control to
         * re-program and no program switch of any kind is issued. No setup
         * bracket runs and the key is NOT invalidated to manufacture a miss. It
         * is counted BESIDE zc_reuses rather than instead of it, because the
         * zero-count arm's own cadence must stay readable; np_setups + np_skips
         * + np_reuses is then the complete served-mesh account for the
         * control. */
        if (neutral) g_c.np_reuses++;
        /* v0.4 THE AUXILIARY-CONTEXT CONTROL'S OWN REUSE COUNT
         * (SL_BG_TF_AUX_EMPTY), default off: one cached integer test and no
         * call.
         *
         * A HIT HAS NO EMPTY DRAW, so there is nothing for this control to
         * relocate and NO CONTEXT SWITCH OF ANY KIND IS ISSUED - which is the
         * one thing worth saying out loud here, a switch being the expensive
         * part of this arm and a hit being where a careless implementation would
         * pay for it anyway. No setup bracket runs and the key is NOT
         * invalidated to manufacture a miss. Counted beside zc_reuses rather
         * than instead of it, so the zero-count arm's cadence stays readable;
         * ax_setups + ax_skips + ax_reuses is then the complete served-mesh
         * account for the control. */
        if (aux) g_c.ax_reuses++;
        /* v0.4 THE REAL-PREPASS MODE'S OWN REUSE COUNT (SL_BG_TF_AUX_REAL),
         * default off: one cached integer test and no call.
         *
         * A HIT HAS NO PREPASS, so there is nothing for this mode to relocate:
         * NO CONTEXT SWITCH AND NO SYNCHRONISATION POINT OF ANY KIND IS ISSUED,
         * which is the thing worth saying out loud - the two synchronisation
         * points per bracket being the expensive part of this mode under either
         * value of SL_BG_TF_AUX_SYNC, and a hit being where a careless
         * implementation would pay for them anyway. No setup bracket
         * runs and the key is NOT invalidated to manufacture a miss. On the
         * GPU_SOURCE arm the hit's slot carries whatever `written` its own
         * bracket left, so a hit on a slot whose write never completed is still
         * unbindable. */
        if (arl) g_c.ar_reuses++;
        /* v0.4 THE PRIVATE-VERTEX-ARRAY CONTROL'S OWN REUSE COUNT
         * (SL_BG_TF_PRIVATE_VAO), default off: one cached integer test and no
         * call.
         *
         * A HIT ADDS NOTHING AND FORCES NOTHING. No setup runs for this draw, so
         * there is no array state to isolate: no object is bound, no descriptor
         * is written, no draw is issued, and the key is NOT invalidated to
         * manufacture a miss. The hit is carried as its own number rather than
         * as a bind, because a bind that did not happen is not one. */
        if (private_vao) g_c.pv_reuses++;
        /* v0.4 THE PROBE'S CACHE LINK (SL_BG_GLSTATE), default off: one cached
         * integer test and no call.
         *
         * A REUSED SLOT DID NO WORK FOR THIS DRAW, and the probe is told so
         * rather than being left to infer it. Nothing here is a producer stage:
         * no setup ran, so no phase exists to observe, and the probe resolves
         * this hit to the EVENT whose key the slot carries. A hit on a slot
         * filled before the probe's walk arming - or past its bounded ledger -
         * is reported as an identified but UNOBSERVED producer and makes the
         * record incomplete, which is the honest answer and is not a
         * fabricated stage. */
        if (sl_bgstate_armed()) {
            struct sl_bgstate_producer pk;
            bgtf_state_key(&pk, s, dmode, k1b, k2b, 1, state_only);
            sl_bgstate_reuse(&pk);
        }
        return s;
    }

    s = tf_victim();
    sl = &g_slot[s];
    /* REFILL AND EVICTION ARE DIFFERENT FACTS, and conflating them made the
     * census claim cache pressure on every ordinary walk. Taking a free slot,
     * or one left behind by a PREVIOUS walk, is a refill: that slot's pool can
     * never be wanted again, because a key carries its walk generation. Only
     * displacing a key belonging to the CURRENT walk is pressure - a pool this
     * same walk may still want - and only that is an eviction. */
    if (!sl->filled)             g_c.slot_refills++;
    else if (sl->gen != g_gen)   g_c.slot_refills++;
    else                         g_c.slot_evictions++;

    if (sl->buf == 0u) {
        p_GenBuffers(1, &sl->buf);
        if (sl->buf == 0u) {
            /* A BUFFER THE CONTEXT WILL NOT GIVE IS A CAPABILITY FAILURE, and
             * it latches like one. Returning -1 alone left the state ready and
             * asked for a name again on the next draw, which turned one
             * exhausted context into one glGenBuffers per draw for the rest of
             * the frame - a cost that grows with the scene while the picture is
             * the CPU path's either way. Latching matches what the header
             * promises for an allocation failure and is cleared only by
             * sl_bgtf_invalidate, so a new context tries again. */
            g_c.refusals++;
            tf_refuse("a transform-feedback buffer could not be created");
            return -1;
        }
        sl->cap_verts = 0u;
    }
    /* THE STORAGE IS SIZED AND THE REQUEST IS VERIFIED BEFORE THE SLOT IS
     * TRUSTED, and the capacity is recorded only after the driver agrees.
     *
     * Marking cap_verts sufficient straight after the void glBufferData was
     * wrong in a way that could rasterise: a driver that refused the size
     * leaves the object smaller than asked, and the slot would then have been
     * stamped filled and drawn from - an undersized or undefined pool rendered
     * instead of the CPU fallback this module promises. GL_BUFFER_SIZE is read
     * once per allocation to learn whether the request was accepted.
     *
     * A refusal here leaves the slot INVALID: cap_verts goes back to 0 and
     * `filled` is never set, so nothing downstream can mistake it for a pool.
     * The state this function touched is put back on the way out, which is why
     * the discard/program/binding restore is in one place at the end rather
     * than duplicated on each exit. */
    if (sl->cap_verts < g_nverts) {
        const ptrdiff_t want = (ptrdiff_t) ((size_t) g_nverts * 4u * sizeof(GLfloat));
        GLint got = -1;
        sl->cap_verts = 0u;
        sl->filled = 0;
        sl->written = 0;
        p_BindBuffer(GL_ARRAY_BUFFER, sl->buf);
        p_BufferData(GL_ARRAY_BUFFER, want, NULL, GL_STATIC_DRAW);
        p_GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &got);
        p_BindBuffer(GL_ARRAY_BUFFER, 0);
        if (got < 0 || (ptrdiff_t) got < want) {
            g_c.refusals++;
            tf_refuse("transform-feedback storage was not allocated at the "
                      "requested size");
            return -1;
        }
        sl->cap_verts = g_nverts;
    }

    /* THE PREPASS ITSELF.
     *
     * Rasterizer discard is on for the whole of it, so no fragment is produced
     * and the framebuffer cannot be touched. The input is the pass's own static
     * vertex buffer at the item's stride, read as GL_POINTS over [0, nverts) -
     * canonical vertex order, one output per input vertex, which is what makes
     * the original indices index the output.
     *
     * The element binding is dropped for the duration: glDrawArrays ignores it,
     * but leaving a stale element buffer bound across a draw that does not use
     * it is the kind of state that makes a later indexed draw read the wrong
     * buffer, and putting it back is sl_bgtf_unbind's job.
     *
     * THE STATE-ONLY CONTROL OMITS ONE CALL FROM THIS BLOCK AND NOTHING ELSE
     * (SL_BG_TF_STATE_ONLY, coupled to SL_BG_TF_RASTER_CPU). Every line around
     * the draw runs, in this order, with the same arguments, and the restoration
     * below and sl_bgtf_unbind's are untouched - so what differs between that arm
     * and the original-raster control is the EXECUTION of the vertex draw and
     * not the state, program or binding history around it. A condition on the
     * draw rather than a second code path is what keeps that true: a duplicated
     * bracket would be a second thing that could drift. */

    /* v0.4 THE PROBE'S PHASE 1, PRE_SETUP (SL_BG_GLSTATE), default off: one
     * cached integer test and no call.
     *
     * HERE AND NOWHERE LATER. This is the last point at which the state the
     * prepass INHERITED still exists: no program is current, no uniform has
     * been written, discard is off, no pointer has moved and no indexed
     * transform-feedback binding is made. A diagnostic that selected its draw
     * after the prepass - as this file's buffer readback does, correctly for
     * what it measures - cannot recover this moment, which is why the probe
     * arms on the WALK's clocks and observes bounded producers rather than
     * waiting for a mesh to be chosen. */
    if (sl_bgstate_armed()) {
        struct sl_bgstate_producer pk;
        bgtf_state_key(&pk, s, dmode, k1b, k2b, 0, state_only);
        sl_bgstate_pre_setup(&pk);
    }

    /* v0.4 THE PRIVATE-VERTEX-ARRAY CONTROL BRACKETS THE SETUP AND THE DRAW
     * (SL_BG_TF_PRIVATE_VAO), default off and one cached test when it is off.
     *
     * WHY HERE AND NOT EARLIER. The probe's PRE_SETUP phase above is the last
     * point at which the state the prepass INHERITED still exists, and
     * GL_VERTEX_ARRAY_BINDING is one of the things it reads. Acquiring before
     * that hook would have the probe record this control's own object as the
     * state the prepass was handed, which is false. Acquiring after it means the
     * inherited binding is what phase 1 sees, the private object is what phase 2
     * sees, and the inherited one again is what phase 3 sees - all three honest,
     * and all three at their existing moments with their existing producers.
     *
     * WHAT MOVES INSIDE THE BRACKET, AND THE BRACKET IS THE WHOLE SCOPE OF THE
     * CLAIM. The position descriptor the pointer call below installs and the
     * element binding dropped after it are array-object state, so with the
     * control in force they land on the private object and the caller's own are
     * left alone FOR THESE CALLS. That is the whole difference; the calls
     * themselves, their order and their arguments are unchanged. It is NOT a
     * statement about the rest of the walk - sl_bgtf_unbind still clears the
     * element binding at the walk's end (:1833), unchanged, and src/gfx/
     * sl_gfx_dl.c's admitted draws bind and unbind g_bgv_index for themselves
     * (:17078, :17082) under an admission that rejects !g_bgv_live (:19055).
     *
     * WHAT DOES NOT. The ARRAY_BUFFER binding is not array-object state, so the
     * bind/unbind pair around the pointer call is still what leaves the global
     * binding at 0 and is unchanged on every arm. A refusal leaves all of it
     * exactly as it is without this control. */
    private_vao = private_vao && tf_vao_acquire();

    p_UseProgram(g_prog);
    p_Uniform1i(g_u_dmode, (GLint) dmode);
    p_Uniform1f(g_u_fk1, (GLfloat) k1);
    p_Uniform1f(g_u_fk2, (GLfloat) k2);

    glEnable(GL_RASTERIZER_DISCARD);
    p_BindBuffer(GL_ARRAY_BUFFER, g_in_vbo);
    glVertexPointer(3, GL_FLOAT, (GLsizei) g_in_stride, (const GLvoid *) 0);
    p_BindBuffer(GL_ARRAY_BUFFER, 0);
    p_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, sl->buf);
    /* v0.4 THE EMPTY-DRAW STATE MODE'S T CONDITION (SL_BG_TF_EMPTY_STATE),
     * default off and one cached test when it is off.
     *
     * THE PAIR IS OMITTED TOGETHER OR NOT AT ALL. `es_tf` guards this call and
     * the matching glEndTransformFeedback below, and nothing between them can
     * change it - it is a launch constant read once above - so a T0 arm issues
     * neither and a T1 arm issues both. An unmatched begin or end is invalid, so
     * one condition guarding both calls is what makes the pair structurally
     * matched rather than matched by inspection.
     *
     * WHAT A T0 ARM KEEPS. The output allocation and its verified storage above,
     * and the indexed GL_TRANSFORM_FEEDBACK_BUFFER binding just made and its
     * release below. Binding a buffer to that target is legal while no capture
     * is active, so what differs between T1 and T0 here is the ACTIVE CAPTURE
     * and not the pool's existence, its size or its binding. */
    if (es_tf) {
        p_BeginTransformFeedback(GL_POINTS);
        if (es_mode != SL_BGTF_ES_OFF) g_c.es_tf_begins++;
        /* THE SCOPE IS NOW OPEN, recorded for the post-reset boundary's own
         * guard. It is raised here rather than derived from `es_tf` so that the
         * guard reads the command that was ISSUED: a begin without its matching
         * end leaves this 1 and the boundary issues nothing, which is what
         * makes "the program is never switched and no call is added while a
         * feedback scope is open" a property of the calls rather than of the
         * table that chose them. */
        pr_tf_open = 1;
    }
    if (!state_only) {
        /* THE POSITION-ONLY-INPUT CONTROL BRACKETS THIS DRAW AND NOTHING ELSE
         * (SL_BG_TF_POS_INPUT), default off and one cached test when it is off.
         *
         * THE DRAW AND ITS TRANSFORM-FEEDBACK WRITES ARE KEPT. That is the whole
         * difference from the state-only control above: this arm does real GPU
         * work and counts it, and only the set of client arrays the draw
         * dereferences is narrowed to the position array the program reads.
         *
         * THE BRACKET IS AS TIGHT AS THE CLAIM. It opens immediately before the
         * draw and closes immediately after it, inside the transform-feedback
         * scope, so the program, the uniforms, the pool, the position pointer,
         * the element binding, rasterizer discard and the begin/end pair are the
         * same calls in the same order with the same arguments as on every other
         * arm. Client array enables are consumed at draw time, so narrowing them
         * here is exactly what the draw sees and nothing earlier is affected.
         *
         * A REFUSAL IS NOT ISOLATION. tf_pos_isolate answers 0 when it could not
         * account for the whole state - no per-unit selector, an unanswered or
         * out-of-ceiling coordinate domain, or an inherited selector the context
         * will not state as a selectable set - and then the draw below runs
         * precisely as it does without this control, with the refusal counted
         * and reported once.
         * The restore is called only on the 1 answer, so there is no path on
         * which arrays are disabled and left that way. */
        const int iso = tf_pos_isolate();
        /* v0.4 THE ZERO-VERTEX-COUNT CONTROL REPLACES THIS CALL'S COUNT AND
         * NOTHING ELSE (SL_BG_TF_ZERO_COUNT), default off and one cached test
         * when it is off.
         *
         * THE CALL IS STILL MADE, AND THAT IS THE MEASUREMENT. A branch AROUND
         * this statement would be the state-only control's omission, which is
         * already measured and is the one thing this arm must not be mistaken
         * for. So the condition is on the ARGUMENT: the same entry point, in the
         * same position inside the transform-feedback scope, with the same mode
         * and the same first vertex, and 0 where the count goes. glDrawArrays is
         * an external entry point with side effects the compiler cannot see, so
         * the call cannot be elided for having nothing to draw.
         *
         * `zero_count` is this launch's cached answer and cannot change between
         * the draws of a walk, which is what lets one slot's `written` tag below
         * answer for the whole of it. The count is counted HERE, immediately
         * after the call that was issued, rather than beside the cadence at the
         * end: what it records is a call made, not a bracket entered. */
        /* v0.4 THE EMPTY-DRAW STATE MODE'S D AND P CONDITIONS
         * (SL_BG_TF_EMPTY_STATE), default off and two cached tests when it is
         * off.
         *
         * THE BRACKET IS AS TIGHT AS THE CLAIM, which is why it opens here and
         * not in the common setup. The common bracket's own glEnable of
         * rasterizer discard, its program and every uniform are already in
         * place, unchanged and in the same order as on every other arm; these
         * two calls change the state only for the ONE draw below and the two
         * after it put the common bracket's state back, so the restoration
         * below and sl_bgtf_unbind's see what they see on Z_KNOWN.
         *
         * THE ORDER IS LOAD-BEARING TWICE OVER. The uniforms are written in the
         * common setup under the prepass program, so a switch to program 0 here
         * can never precede a uniform write - no uniform is ever written while
         * program 0 is current. And a row with any switch in it has `es_tf` 0,
         * which the one table guarantees, so the program is never changed while
         * transform feedback is active.
         *
         * THE FOUR SWITCH POSITIONS ARE INDEPENDENT, which is what lets one row
         * hold the program at the draw fixed and move only the calls around it:
         * A, R and Q all draw under the prepass program, and E and S both draw
         * under program 0. A row that issues nothing is every other arm's own
         * history, so A, B, C and an absent selector add no program call at all.
         *
         * WITHIN A SIDE THE ORDER IS 0 AND THEN THE PROGRAM, so R's pair ends
         * with the prepass program current at the draw and Q's ends with it
         * current afterwards. A row that asked for the program without the 0
         * before it would be a redundant rebind of what is already current; no
         * row does, and the table is where that would be introduced.
         *
         * EACH COUNT IS RAISED IMMEDIATELY AFTER THE COMMAND IT COUNTS, so what
         * is recorded is a transition ISSUED and not a mode selected. The paired
         * totals keep their meaning - every switch to 0 and every switch back -
         * and the four positional counts are what tell E from R and S from Q. */
        if (!es_discard) {
            glDisable(GL_RASTERIZER_DISCARD);
            g_c.es_discard_offs++;
        }
        if (es_pre_zero) {
            p_UseProgram(0);
            g_c.es_prog0_switches++;
            g_c.es_prog_pre_zero++;
            es_cur_prog = 0;
        }
        if (es_pre_prog) {
            p_UseProgram(g_prog);
            g_c.es_prog0_restores++;
            g_c.es_prog_pre_prog++;
            es_cur_prog = 1;
        }
        /* v0.4 THE NEUTRAL-PROGRAM CONTROL'S ONE SWITCH BEFORE THE CALL
         * (SL_BG_TF_NEUTRAL_PROG), default off and one cached test when it is
         * off.
         *
         * THE BRACKET IS AS TIGHT AS THE CLAIM, which is why it opens here and
         * not in the common setup. Every line of the common bracket - the
         * program, the three uniforms, the discard enable, the input pointer,
         * the dropped element binding and the indexed output binding - has run
         * unchanged and in the same order as on every other arm; this one call
         * changes which program is current for the ONE draw below, and the one
         * after it puts the common bracket's own program back.
         *
         * NO UNIFORM IS EVER WRITTEN UNDER IT. The uniforms are written in the
         * common setup under the prepass program, which is strictly above this
         * point, so a switch here can never precede a uniform write - and the
         * neutral program declares none to write.
         *
         * AND NO PROGRAM IS CHANGED WHILE A FEEDBACK SCOPE IS OPEN. `es_tf` is 0
         * whenever this control is in force - set once, above, where the control
         * is admitted - so the begin/end pair was omitted and no capture can be
         * active at this switch. That is a property of the one condition that
         * guards both of those calls rather than of the order these statements
         * happen to be in.
         *
         * IT IS PLACED AFTER THE EMPTY-DRAW ROWS' OWN SWITCHES, which are dead
         * here: a row cannot be in force alongside this control, and the site
         * guard above refuses the draw outright if one is. Keeping this call
         * immediately above the draw means the program current AT the call is
         * this control's and nothing else's. */
        if (neutral) {
            p_UseProgram(g_np_prog);
            g_c.np_switches++;
            np_cur = 1;
        }
        /* v0.4 THE AUXILIARY-CONTEXT CONTROL'S WHOLE EFFECT, AT ONE SITE
         * (SL_BG_TF_AUX_EMPTY), default off and one cached test when it is off.
         *
         * THE MAIN CONTEXT'S OWN DRAW IS NOT ISSUED, and that is the only thing
         * this control takes away from the ordinary sequence. Every line of the
         * common bracket above - the program, the three uniforms, the discard
         * enable, the input pointer, the dropped element binding and the indexed
         * output binding - has run unchanged and in the same order as on every
         * other arm, and every line of the common restoration below runs
         * unchanged too. The omission is counted rather than merely done, so
         * "the main context issued no empty draw" is a POSITIVE record a reader
         * can check and not the absence of one.
         *
         * AND THE CALL IS MADE ON THE SECOND CONTEXT INSTEAD, as four commands
         * between two switches: bind the SAME SHARED prepass program there,
         * enable discard there, issue exactly glDrawArrays(GL_POINTS, 0, 0),
         * then put that context's own two pieces of state back. The restore on
         * that side is to GL's defaults - discard off, no program - because the
         * auxiliary context is this control's alone, has never been used for
         * anything else, and so its default state is also its entry state; no
         * value is read back to discover it.
         *
         * THE CONDITIONS AT THE CALL ARE ROW A'S, on the context it is made on:
         * transform feedback INACTIVE - no feedback scope is ever opened on
         * either context, `es_tf` being 0 whenever this control is in force - and
         * rasterizer discard ON, enabled here because discard is per-context
         * state and the main context's enable above says nothing about this one.
         *
         * NO OUTPUT IS BOUND AND NONE IS CONSUMED. The indexed output binding
         * the common setup made belongs to the main context; nothing is bound on
         * the auxiliary one, nothing is read back from either, and the slot this
         * bracket keys is marked unwritten exactly as the zero-count arm marks
         * it. Cross-context buffer synchronisation is therefore untouched here.
         *
         * AND A FAILED SWITCH ISSUES NOTHING, in the three different ways the
         * three outcomes require. A switch TO the auxiliary context that leaves
         * the MAIN context VERIFIED current is recoverable: the restoration
         * below is exactly the right thing to run and the refusal is taken at
         * the end. A switch TO it that leaves neither verifiably current, and a
         * switch BACK that fails, are the same outcome as each other and a
         * different kind of thing from the first: this process cannot name the
         * context a command would reach, so the restoration must NOT run - it
         * would issue its three calls on a context nobody selected - nothing
         * further is issued, and the process ENDS, because a refusal would hand
         * the mesh to a caller that is about to raster it with GL. */
        if (aux) {
            int ax_r;

            g_c.ax_main_omits++;
            ax_r = sl_gl_aux_make_current();
            if (ax_r == SL_BGTF_AUX_LOST) {
                /* THE SWITCH ESTABLISHED NEITHER CONTEXT. This is not the
                 * recoverable shape below: the bracket's restoration must NOT
                 * run, because it would issue its three calls on a context
                 * nobody selected, and there is no state to continue from. The
                 * process ends here. ax_dead is raised for the fixture's sake,
                 * where the terminate returns. */
                ax_dead = 1;
                tf_ax_lost("the switch to the auxiliary GL context at the draw "
                           "left neither it nor the MAIN context verifiably "
                           "current");
            } else if (ax_r == 0) {
                g_c.ax_switch_fails++;
                g_ax_state = -1;
                ax_bad = 1;
                if (!g_ax_said) {
                    g_ax_said = 1;
                    fprintf(stderr, "#95 SL_BG_TF_AUX_EMPTY=1: the auxiliary GL "
                                    "context could not be made current at the "
                                    "draw. The MAIN context is still current, "
                                    "so this bracket's own restoration runs and "
                                    "NOTHING was issued on either context; the "
                                    "served draw is REFUSED to the CPU path and "
                                    "this is reported once per launch\n");
                }
            } else {
                ax_cur = 1;
                g_c.ax_switches++;
                /* THROUGH THE AUXILIARY CONTEXT'S OWN POINTER, which the
                 * acquire resolved on this context and which is the only
                 * glUseProgram this control may use here - see p_ax_IsProgram
                 * for why a main-context address is not valid on this one.
                 * glEnable, glDisable and glDrawArrays are direct calls to the
                 * link-time GL imports and hold no per-context pointer to get
                 * wrong; the context they reach is the one this thread has
                 * current, which the verified switch above established. */
                p_ax_UseProgram(g_prog);
                glEnable(GL_RASTERIZER_DISCARD);
                glDrawArrays(GL_POINTS, 0, (GLsizei) 0);
                g_c.ax_draws++;
                if (ax_cur) g_c.ax_draws_aux++;
                glDisable(GL_RASTERIZER_DISCARD);
                p_ax_UseProgram(0);
                if (sl_gl_main_make_current() != 1) {
                    ax_dead = 1;
                    tf_ax_lost("the empty draw was issued on the auxiliary GL "
                               "context and the MAIN context could not be "
                               "verified current again");
                } else {
                    ax_cur = 0;
                    g_c.ax_restores++;
                }
            }
        }
        /* v0.4 THE REAL-PREPASS MODE'S WHOLE EFFECT, AT ONE SITE
         * (SL_BG_TF_AUX_REAL), default off and one cached test when it is off.
         *
         * THE MAIN CONTEXT'S OWN PREPASS DRAW IS NOT ISSUED, and that is the
         * only thing this mode takes away from the ordinary sequence. Every line
         * of the common bracket above has run unchanged and in the same order as
         * on every other arm - with the one declared exception that the feedback
         * pair was omitted on THIS context, stated where `es_tf` is cleared - and
         * every line of the common restoration below runs unchanged too. The
         * omission is counted rather than merely done, so "the main context
         * issued no prepass draw" is a POSITIVE record a reader can check.
         *
         * AND THE WHOLE PREPASS IS BUILT AND RUN ON THE SECOND CONTEXT INSTEAD.
         * Nothing is inherited there: object NAMES are shared, bindings, enables
         * and container objects are not, so each is set explicitly. The sequence
         * is the same shape as the main bracket's, in the same order, with the
         * real count - and then that context's own state is put back to its
         * defaults, which for a context this mode alone has ever used is also
         * its entry state. No value is read back to discover it.
         *
         * THE TWO SYNCHRONISATION POINTS ARE WHERE THE VISIBILITY RULES PUT
         * THEM, AND WHAT IS ISSUED AT EACH DEPENDS ON SL_BG_TF_AUX_SYNC. A
         * shared object's new contents reach a second context only when the
         * writing commands are ORDERED BEFORE the reading ones and the reading
         * context BINDS the object afterwards.
         *
         * UNDER FINISH, the ordering is obtained by completion: glFinish here,
         * on the main context, completes whatever wrote the input buffer; the
         * binds on the auxiliary context below are the bind-after-completion for
         * the read side there; glFinish there completes the transform's writes;
         * and the re-bind on the main context after the verified switch back is
         * the same rule in the other direction. Every one of those is a CPU
         * stall, which is why a FINISH launch's frame time measures the drains
         * and not the mode.
         *
         * UNDER FENCE, the same two points carry a fence plus the producer's own
         * glFlush on the writing context and a glWaitSync on the reading one.
         * That wait is SERVER-SIDE: it returns at once on the CPU, so at the
         * instant the bracket goes on, the transfer is ORDERED and NOT known to
         * be complete. Nothing on that path may say a write completed on the
         * CPU; the written tag is gated on both waits having been issued, and
         * the bind-after rule is satisfied because the bind follows the wait in
         * the same command stream. The two modes therefore differ in the
         * GUARANTEE's strength and in its CPU cost, not in which commands read
         * which object.
         *
         * NEITHER IS A PERFORMANCE RESULT. FINISH is the measured default
         * because it is the strongest guarantee with the fewest resolved entry
         * points; FENCE removes the two stalls and is unqualified on both
         * pixels and frame time until a controlled benchmark and an exact-image
         * capture say otherwise. The boundary is stated in the header: THIS MODE
         * IS NOT A SHIPPING PATH UNDER EITHER VALUE.
         *
         * THE OBJECTS ARE VERIFIED SHARED, PER BRACKET, BEFORE A SINGLE BYTE OF
         * STATE IS WRITTEN ON THAT CONTEXT. The input buffer belongs to the walk
         * and the output pool to the slot, so both names vary and a
         * once-per-context check could not cover them. A FALSE answer leaves
         * that context exactly as it was found, refuses the served draw and
         * reports once.
         *
         * AND A FAILED SWITCH ISSUES NOTHING, in the three ways the backend's
         * three answers require - the same three the empty-draw control above
         * handles, with one addition this mode has and that one does not: a
         * switch BACK that fails AFTER the output was written is counted as a
         * SYNC refusal as well, and the slot stays unwritten, because a pool
         * whose visibility was never established must never be rasterised. */
        if (arl) {
            int ar_r;
            /* THE SYNCHRONISATION MODE, ASKED ONCE PER BRACKET AND ANSWERED FROM
             * A CACHED PAIR (SL_BG_TF_AUX_SYNC). 0 is FINISH, which is what every
             * launch that did not ask for the other thing gets, and under which
             * every statement below that mentions a fence is a function whose
             * first line returns.
             *
             * `fx_in` and `fx_out` are the two sync objects a FENCE bracket
             * holds, one at a time and never both: the input object is deleted on
             * the auxiliary context before any output object is created. They are
             * NULL under FINISH on every path.
             *
             * `fx_inw` and `fx_outw` are whether each wait was ISSUED. The written
             * slot is gated on both under FENCE, which is what "refuse the draw
             * if either handoff is unproved" is in code. */
            const int fx = tf_fx_in_force();
            SL_TF_SYNC fx_in = NULL, fx_out = NULL;
            int fx_inw = 0, fx_outw = 0, fx_oute = 0;

            g_c.ar_main_omits++;
            /* MAIN -> AUX INPUT VISIBILITY, FIRST HALF: completion under FINISH,
             * a producer fence and an explicit producer flush under FENCE. The
             * refusal this can answer is recoverable, has already been reported
             * and latched, holds no sync object and made no switch - so the
             * bracket's own restoration runs below exactly as it does for a
             * refused switch. */
            if (!tf_fx_in_open(fx, &fx_in, ar_mode)) {
                ar_bad = 1;
            } else if ((ar_r = sl_gl_aux_make_current()) == SL_BGTF_AUX_LOST) {
                /* THE SWITCH ESTABLISHED NEITHER CONTEXT. The bracket's
                 * restoration must NOT run - it would issue its calls on a
                 * context nobody selected - and there is no state to continue
                 * from. The process ends here; ar_dead is raised for the
                 * fixture's sake, where the terminate returns. */
                ar_dead = 1;
                g_c.ar_lost++;
                g_c.ar_fatals++;
                /* AND THE INPUT FENCE IS ABANDONED, NOT DELETED. No GL command
                 * of any kind may be issued past this point, a delete included -
                 * see tf_fx_abandon. */
                tf_fx_abandon(&fx_in);
                tf_ax_lost("the real-prepass mode's switch to the auxiliary GL "
                           "context at the prepass left neither it nor the MAIN "
                           "context verifiably current");
            } else if (ar_r == 0) {
                g_c.ar_switch_fails++;
                g_ar_state = -1;
                ar_bad = 1;
                /* THE MAIN CONTEXT IS VERIFIED CURRENT, which is what this
                 * failure means, so the input fence is deleted here with this
                 * context's own pointer. */
                tf_fx_drop_main(&fx_in);
                if (!g_ar_said) {
                    g_ar_said = 1;
                    fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s: the auxiliary GL "
                                    "context could not be made current at the "
                                    "prepass. The MAIN context is still "
                                    "current, so this bracket's own restoration "
                                    "runs and NOTHING was issued on either "
                                    "context; the served draw is REFUSED to the "
                                    "CPU path, the slot stays UNWRITTEN, and "
                                    "this is reported once per launch\n",
                            sl_bgtf_ar_name(ar_mode));
                }
            } else {
                unsigned long ar_e0 = 0ul;
                int ar_ecap = 0;
                unsigned ar_epre;

                ar_cur = 1;
                g_c.ar_switches++;
                /* THIS CONTEXT'S ERROR STATE, EMPTIED BEFORE THIS MODE ISSUES
                 * ANYTHING ON IT.
                 *
                 * WHY A DRAIN HERE AT ALL, AND WHAT IT BUYS. A GL error flag is
                 * STICKY: it stays set until a GetError retires it. That is the
                 * property the drain after the matched scope relies on - one
                 * read there is a verdict over the setup, the uniforms, the
                 * bindings, the begin, the real draw, the end and the
                 * restoration together - and it is also the property that makes
                 * the verdict meaningless unless the state was empty when the
                 * bracket started. A flag left over from something this bracket
                 * did not issue would FAKE a failure here, and the bracket's own
                 * output would be thrown away for someone else's error.
                 *
                 * AND ANY NONZERO ANSWER HERE REFUSES, BEFORE ANYTHING IS BOUND,
                 * ENABLED OR ISSUED ON THAT CONTEXT. The attribution is still
                 * separate - codes found here were raised by commands this
                 * bracket did not issue, so they are counted under
                 * ar_gl_pre_errors and never under ar_gl_errors - but the
                 * CONSEQUENCE is the same as a rejection's, for a reason the
                 * attribution does not soften: THIS CONTEXT EXISTS FOR THIS
                 * DIAGNOSTIC AND NOTHING ELSE. Nothing outside this mode's own
                 * bracket touches it, so a flag found on arrival was raised by
                 * that context's creation, by this mode's capability acquire, or
                 * by an earlier visit of this mode's own - and whichever of the
                 * three it was, the context's health is UNPROVED from that point
                 * on. The code may be GL_CONTEXT_LOST, which says the context is
                 * gone; the drain cannot tell that case from a harmless
                 * INVALID_ENUM except by the code, and a diagnostic that
                 * continued would be reporting a completed write from a context
                 * that had already signalled a fault before the bracket began.
                 * Retiring the codes is still what the drain does, so what is
                 * reported is a statement about arrival and not an echo carried
                 * into the verdict.
                 *
                 * REFUSING WITHOUT LATCHING WOULD BE THE WRONG TRADE, and it is
                 * the one the latch answers: a stale flag would otherwise refuse
                 * every served draw for the rest of the launch, one full round
                 * trip at a time. The first finding latches the capability, so
                 * the cost is ONE bracket's switch and drain and every later
                 * served draw goes to the CPU path without reaching that context
                 * at all - the same discipline the verdict drain's refusal
                 * follows.
                 *
                 * A CAPPED DRAIN REFUSES TOO, and keeps its own account. If the
                 * bound is reached with the state still not empty, the state
                 * could not be emptied, so the verdict below could not be
                 * attributed to this bracket either. */
                ar_epre = tf_ar_drain(&ar_e0, &ar_ecap);
                /* THE PRE-EXISTING ACCOUNT IS ONLY MADE WHEN THE DRAIN
                 * SUCCEEDED, which is what the order of these two clauses says.
                 * A capped drain retired some codes and did not reach a clear
                 * state, so the codes it saw are not "what was already flagged,
                 * now retired" - they are a prefix of a state nobody can read to
                 * the end. Counting them under ar_gl_pre_errors would claim an
                 * attribution that was not established. Both outcomes refuse;
                 * which of the two counters and which of the two messages a
                 * reader gets is the only thing this order decides. */
                if (ar_ecap) {
                    g_c.ar_gl_drain_caps++;
                    g_c.ar_gl_errors++;
                    g_c.ar_gl_error_refusals++;
                    if (g_c.ar_gl_first_error == 0ul)
                        g_c.ar_gl_first_error = ar_e0;
                    g_ar_state = -1;
                    ar_bad = 1;
                    if (!g_ar_gl_said) {
                        g_ar_gl_said = 1;
                        fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s: the auxiliary "
                                        "GL context's error state could NOT be "
                                        "emptied - glGetError answered a nonzero "
                                        "code %u times in a row, the first being "
                                        "0x%lX, and the drain is bounded so that "
                                        "its termination does not rest on the "
                                        "driver. An error state that cannot be "
                                        "read cannot say whether this mode's own "
                                        "commands were accepted, so NOTHING was "
                                        "bound, enabled or issued on that "
                                        "context, the served draw is REFUSED to "
                                        "the CPU path, the slot stays UNWRITTEN, "
                                        "and this is reported once per launch\n",
                                sl_bgtf_ar_name(ar_mode), SL_BGTF_AR_ERR_CAP,
                                ar_e0);
                    }
                    if (sl_gl_main_make_current() != 1) {
                        ar_dead = 1;
                        g_c.ar_lost++;
                        g_c.ar_fatals++;
                        tf_fx_abandon(&fx_in);
                        tf_ax_lost("the auxiliary GL context's error state could "
                                   "not be emptied under its bound and the MAIN "
                                   "context could not be verified current again");
                    } else {
                        ar_cur = 0;
                        g_c.ar_restores++;
                        /* AND THE INPUT FENCE, DELETED ON THE MAIN CONTEXT NOW
                         * THAT IT IS VERIFIED CURRENT AGAIN - never on the
                         * auxiliary one, because the refusal above states that
                         * nothing was issued there. */
                        tf_fx_drop_main(&fx_in);
                    }
                } else if (ar_epre != 0u) {
                    /* CODES ALREADY FLAGGED ON ARRIVAL: THE SAME REFUSAL, UNDER
                     * ITS OWN NAME. Counted apart from the verdict's census and
                     * reported under its own latch, so a reader is sent to the
                     * context's creation or to this mode's acquire rather than to
                     * a command of the bracket - and the bracket does not run,
                     * because the health of a context dedicated to this
                     * diagnostic is unproved once anything has flagged an error
                     * on it. Nothing is bound, enabled or issued here, the slot
                     * is never marked written, the pool is never re-bound on the
                     * main context, and the capability latches. */
                    g_c.ar_gl_pre_errors += ar_epre;
                    g_c.ar_gl_pre_refusals++;
                    if (g_c.ar_gl_first_pre_error == 0ul)
                        g_c.ar_gl_first_pre_error = ar_e0;
                    g_ar_state = -1;
                    ar_bad = 1;
                    if (!g_ar_gl_pre_said) {
                        g_ar_gl_pre_said = 1;
                        fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s: the auxiliary "
                                        "GL context already had %u error code(s) "
                                        "flagged when a prepass bracket reached "
                                        "it, the first being 0x%lX. They were "
                                        "raised by commands this bracket did not "
                                        "issue - that context's creation, this "
                                        "mode's capability acquire, or an earlier "
                                        "visit - so they are counted apart under "
                                        "ar_gl_pre_errors and ar_gl_pre_refusals "
                                        "and are NOT attributed to this bracket's "
                                        "own commands. THEY ARE A REFUSAL ALL THE "
                                        "SAME: this context exists for this "
                                        "diagnostic alone, so an error flagged on "
                                        "it before a bracket began leaves its "
                                        "health UNPROVED - the code may be a "
                                        "GL_CONTEXT_LOST - and a write reported "
                                        "from such a context would mean nothing. "
                                        "NOTHING was bound, enabled or issued "
                                        "there, the served draw is REFUSED to the "
                                        "CPU path, the slot stays UNWRITTEN, the "
                                        "capability LATCHES so no later served "
                                        "draw reaches that context, and this is "
                                        "reported once per launch\n",
                                sl_bgtf_ar_name(ar_mode), ar_epre, ar_e0);
                    }
                    /* AND THE SWITCH HOME IS VERIFIED, as on every other refusal
                     * path here: an unverifiable main context is the stronger
                     * outcome and wins, with no GL call of any kind after it. */
                    if (sl_gl_main_make_current() != 1) {
                        ar_dead = 1;
                        g_c.ar_lost++;
                        g_c.ar_fatals++;
                        tf_fx_abandon(&fx_in);
                        tf_ax_lost("the auxiliary GL context already carried an "
                                   "error code when a prepass bracket reached it "
                                   "and the MAIN context could not be verified "
                                   "current again");
                    } else {
                        ar_cur = 0;
                        g_c.ar_restores++;
                        tf_fx_drop_main(&fx_in);
                    }
                } else {
                /* THE TWO OBJECTS, ASKED ABOUT THROUGH THIS CONTEXT'S OWN
                 * POINTER AND BEFORE ANYTHING IS BOUND OR ENABLED. */
                g_c.ar_buf_checks += 2u;
                if (!p_ax_IsBuffer(g_in_vbo) || !p_ax_IsBuffer(sl->buf)) {
                    g_c.ar_buf_fails++;
                    g_ar_state = -1;
                    ar_bad = 1;
                    if (!g_ar_said) {
                        g_ar_said = 1;
                        fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s: glIsBuffer "
                                        "answered FALSE on the auxiliary GL "
                                        "context for the prepass input buffer "
                                        "or for the output pool, so a name this "
                                        "mode would have bound means nothing "
                                        "there. NOTHING was bound, enabled or "
                                        "issued on that context, the served "
                                        "draw is REFUSED to the CPU path, the "
                                        "slot stays UNWRITTEN, and this is "
                                        "reported once per launch\n",
                                sl_bgtf_ar_name(ar_mode));
                    }
                    if (sl_gl_main_make_current() != 1) {
                        ar_dead = 1;
                        g_c.ar_lost++;
                        g_c.ar_fatals++;
                        tf_fx_abandon(&fx_in);
                        tf_ax_lost("an object the real prepass needs was not "
                                   "shared with the auxiliary GL context and "
                                   "the MAIN context could not be verified "
                                   "current again");
                    } else {
                        ar_cur = 0;
                        g_c.ar_restores++;
                        tf_fx_drop_main(&fx_in);
                    }
                } else {
                    /* MAIN -> AUX INPUT VISIBILITY, SECOND HALF: the consumer
                     * context's own server wait on the producer's fence, issued
                     * HERE - after the arrival drain and after both object
                     * checks, and immediately before the first bind that will
                     * read the input buffer. Under FINISH this is nothing at
                     * all, the completion having already happened on the other
                     * side, and the binds below are the whole of the read side's
                     * half. */
                    tf_fx_in_close(fx, &fx_in, &fx_inw);
                    /* THIS CONTEXT'S OWN STATE, ALL OF IT, EXPLICITLY. The
                     * array object first, because the pointer command and the
                     * element binding below are state held IN it. */
                    p_ax_BindVertexArray(g_ar_vao);
                    glEnableClientState(GL_VERTEX_ARRAY);
                    p_ax_UseProgram(g_prog);
                    /* EVERY UNIFORM THE PROGRAM DECLARES, from this module's own
                     * cache rather than from the shared program object's state,
                     * so nothing here rests on cross-context visibility of
                     * program state. The three per-draw ones carry this draw's
                     * own arguments, exactly as the main bracket writes them. */
                    p_ax_Uniform1f(g_u_sc, g_wu_sc);
                    p_ax_Uniform1f(g_u_post, g_wu_post);
                    p_ax_Uniform3f(g_u_org, g_wu_org[0], g_wu_org[1],
                                   g_wu_org[2]);
                    p_ax_Uniform1fv(g_u_C, 16, g_wu_C);
                    p_ax_Uniform1i(g_u_fogless, g_wu_fogless);
                    p_ax_Uniform1i(g_u_dmode, (GLint) dmode);
                    p_ax_Uniform1f(g_u_fk1, (GLfloat) k1);
                    p_ax_Uniform1f(g_u_fk2, (GLfloat) k2);
                    glEnable(GL_RASTERIZER_DISCARD);
                    p_ax_BindBuffer(GL_ARRAY_BUFFER, g_in_vbo);
                    glVertexPointer(3, GL_FLOAT, (GLsizei) g_in_stride,
                                    (const GLvoid *) 0);
                    p_ax_BindBuffer(GL_ARRAY_BUFFER, 0);
                    p_ax_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                    p_ax_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                                        sl->buf);
                    p_ax_BeginTransformFeedback(GL_POINTS);
                    glDrawArrays(GL_POINTS, 0, (GLsizei) g_nverts);
                    g_c.ar_prepasses++;
                    if (ar_cur) g_c.ar_prepasses_aux++;
                    g_c.ar_vertices += g_nverts;
                    p_ax_EndTransformFeedback();
                    /* AUX -> MAIN OUTPUT VISIBILITY, FIRST HALF: completion
                     * under FINISH, a producer fence and an explicit producer
                     * flush under FENCE, at the same instant either way - the
                     * matched feedback scope has just closed and this context's
                     * state has not yet been put back.
                     *
                     * A 0 ANSWER DOES NOT SHORT-CIRCUIT THE RESTORATION. The
                     * refusal is deferred to the verdict below, so the five
                     * restoring calls, the drain and the verified switch home all
                     * run exactly as they do on the healthy path and this context
                     * is left at its entry defaults on this path too. */
                    fx_oute = !tf_fx_out_open(fx, &fx_out, ar_mode);
                    /* AND THIS CONTEXT'S STATE BACK, in the inverse order. */
                    p_ax_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
                    glDisable(GL_RASTERIZER_DISCARD);
                    p_ax_UseProgram(0);
                    glDisableClientState(GL_VERTEX_ARRAY);
                    p_ax_BindVertexArray(0);
                    /* AND WHETHER GL ACCEPTED ANY OF IT, ASKED ON THIS CONTEXT
                     * AND ONCE.
                     *
                     * WHY THERE IS A CHECK HERE AT ALL. The output handoff's
                     * producer half above - a glFinish under FINISH, a fence with
                     * its producer flush under FENCE - orders or completes the
                     * commands that were ISSUED; neither turns a command the
                     * driver REJECTED into a transform that ran. Under FENCE the
                     * reason is stronger still: that half returns without waiting
                     * for anything, so it cannot have observed a rejection even
                     * in principle. Without this
                     * read, a driver that refused the feedback begin, the real
                     * draw or the scope's close would leave the pool holding
                     * whatever was in it, and everything below - the verified
                     * switch home, the re-bind, ar_wrote, the slot's written tag -
                     * would go on to say a write completed. On the GPU_SOURCE arm
                     * the caller would then bind and rasterise that pool. An
                     * issued command is not an accepted one, and this is the only
                     * thing in this bracket that can tell the two apart.
                     *
                     * WHY ONE READ COVERS THE WHOLE SEQUENCE. An error flag is
                     * sticky until a GetError retires it, so the state read here
                     * carries anything raised by the object checks, the array
                     * object, the client-state enable, the program, all eight
                     * uniforms, the discard enable, the pointer command, the three
                     * buffer bindings, the indexed binding, the begin, the real
                     * nonzero draw, the end and the restoration just above. The
                     * state was emptied at the top of this bracket, so what is
                     * here belongs to those commands and to nothing earlier.
                     *
                     * WHY IT IS AFTER THE RESTORATION RATHER THAN BEFORE IT. Two
                     * reasons, and the second is the one that matters. The
                     * restoration's own calls are this mode's commands too, so a
                     * driver that rejected one of them belongs in this verdict;
                     * and placing the read last means the auxiliary context is
                     * ALREADY back at its entry defaults on the error path, so the
                     * refusal below has no cleanup left to do and cannot leave a
                     * program current or the discard enabled behind it. The cost
                     * is that an error is attributed to the sequence rather than
                     * to one call in it, which is what a bounded, single-read
                     * check can honestly claim; the first code is reported so a
                     * reader has the driver's own word for what kind it was.
                     *
                     * AND THE SYNCHRONISATION POINTS ARE WHERE THEY WERE, UNDER
                     * EITHER VALUE OF SL_BG_TF_AUX_SYNC. The output handoff's
                     * producer half is issued before this read on every path -
                     * the completion glFinish under FINISH, the fence and its
                     * producer flush under FENCE - and each raises ar_syncs at
                     * that same point, so ar_syncs is two per bracket whichever
                     * mode is in force and whether or not the driver accepted
                     * anything, and the healthy sequence this round inherited is
                     * unchanged call for call on the FINISH path. */
                    ar_epre = tf_ar_drain(&ar_e0, &ar_ecap);
                    if (ar_epre != 0u || ar_ecap) {
                        /* GL REJECTED SOMETHING, OR WOULD NOT SAY. Either way
                         * there is no completed write to claim: ar_wrote stays 0,
                         * the pool is NOT re-bound on the main context, nothing
                         * is rasterised from it, and the served draw is refused to
                         * the CPU path through this bracket's own late refusal.
                         * The capability latches, so one rejecting context does
                         * not pay a rejected bracket per mesh. */
                        g_c.ar_gl_errors++;
                        g_c.ar_gl_error_refusals++;
                        if (ar_ecap) g_c.ar_gl_drain_caps++;
                        if (g_c.ar_gl_first_error == 0ul)
                            g_c.ar_gl_first_error = ar_e0;
                        g_ar_state = -1;
                        ar_bad = 1;
                        if (!g_ar_gl_said) {
                            g_ar_gl_said = 1;
                            /* THE CLAUSE THAT NAMES WHAT WAS SYNCHRONISED IS
                             * MODE-DEPENDENT, and the FINISH text is the
                             * original byte for byte. Saying "whatever the
                             * pipeline drain completed" on a FENCE launch would
                             * name a call that was not issued and would credit a
                             * completion that was never established, which is
                             * precisely the ambiguity this mode's evidence has
                             * to avoid; the FENCE text names the ordering that
                             * was in fact obtained instead. Nothing else in the
                             * line moves, on either mode. */
                            fprintf(stderr, "#95 SL_BG_TF_AUX_REAL=%s: GL "
                                            "REJECTED a command of the real "
                                            "prepass on the auxiliary GL context "
                                            "- glGetError answered %u code(s) "
                                            "there, the first being 0x%lX%s. The "
                                            "transform's output therefore means "
                                            "nothing %s, so the pool is NOT "
                                            "re-bound here, the slot stays "
                                            "UNWRITTEN, nothing is rasterised "
                                            "from it, the served draw is REFUSED "
                                            "to the CPU path, and this is "
                                            "reported once per launch\n",
                                    sl_bgtf_ar_name(ar_mode), ar_epre, ar_e0,
                                    ar_ecap ? " and the bounded drain did not "
                                              "reach a clear state" : "",
                                    fx ? "whatever the fence that was taken over "
                                         "it orders, and a rejected command "
                                         "cannot be ordered into one that ran"
                                       : "whatever the pipeline drain completed");
                        }
                        if (sl_gl_main_make_current() != 1) {
                            /* THE REJECTION AND A LOST MAIN CONTEXT TOGETHER.
                             * The stronger outcome wins: nothing further is
                             * issued at all, through the one funnel. */
                            ar_dead = 1;
                            g_c.ar_lost++;
                            g_c.ar_fatals++;
                            tf_fx_abandon(&fx_out);
                            tf_ax_lost("GL rejected a command of the real "
                                       "prepass on the auxiliary GL context and "
                                       "the MAIN context could not be verified "
                                       "current again");
                        } else {
                            ar_cur = 0;
                            g_c.ar_restores++;
                            tf_fx_drop_main(&fx_out);
                        }
                    } else if (fx_oute) {
                        /* THE OUTPUT FENCE COULD NOT BE CREATED, AND GL REJECTED
                         * NOTHING - which is the one shape the clause above cannot
                         * express. The transform's commands were issued and
                         * accepted; what does not exist is anything the main
                         * context could wait on, so the output's visibility there
                         * can never be established. It is therefore a REFUSAL of
                         * exactly the same strength as a rejection: no write is
                         * claimed, the pool is not re-bound, and the switch home is
                         * verified with an unverifiable main context taking the
                         * stronger outcome. The report was made by
                         * tf_fx_out_open, under its own latch, and nothing is held
                         * here to delete - the create is what failed. */
                        ar_bad = 1;
                        g_fc.fx_unproved_refusals++;
                        /* AND THE INPUT HANDOFF IS STILL RECORDED AS PROVED,
                         * because it was: its fence was created, its producer
                         * flushed, its wait issued on the consumer's context,
                         * and the verdict drain that just came back clean is the
                         * read that proves GL accepted that wait. The two
                         * handoffs are counted independently so that a reader of
                         * this refusal is sent to the OUTPUT side rather than to
                         * both - which is the whole reason there are two sets of
                         * fields rather than one. */
                        if (fx_inw) g_fc.fx_in_proved++;
                        if (sl_gl_main_make_current() != 1) {
                            ar_dead = 1;
                            g_c.ar_sync_refusals++;
                            g_c.ar_lost++;
                            g_c.ar_fatals++;
                            tf_ax_lost("the output handoff's fence could not be "
                                       "created on the auxiliary GL context and "
                                       "the MAIN context could not be verified "
                                       "current again");
                        } else {
                            ar_cur = 0;
                            g_c.ar_restores++;
                        }
                    } else if (sl_gl_main_make_current() != 1) {
                        /* THE WORK WAS DONE AND ITS VISIBILITY WAS NEVER
                         * ESTABLISHED. ar_wrote stays 0, so the slot is marked
                         * unwritten whatever is in the pool - and nothing
                         * further is issued at all. */
                        ar_dead = 1;
                        g_c.ar_sync_refusals++;
                        g_c.ar_lost++;
                        g_c.ar_fatals++;
                        tf_fx_abandon(&fx_out);
                        tf_ax_lost("the real prepass ran on the auxiliary GL "
                                   "context and the MAIN context could not be "
                                   "verified current again, so the output's "
                                   "visibility here was never established");
                    } else {
                        ar_cur = 0;
                        g_c.ar_restores++;
                        /* AUX -> MAIN OUTPUT VISIBILITY, SECOND HALF: under
                         * FINISH the reading context BINDS the object after the
                         * write completed; under FENCE it first tells its own
                         * command stream to wait on the producer's fence and then
                         * binds. The global array binding is left at 0, as every
                         * other bind/unbind pair in this function leaves it, so
                         * nothing later reads a client pointer as an offset into
                         * this pool. */
                        tf_fx_out_close(fx, &fx_out, &fx_outw);
                        /* AND NEITHER HANDOFF MAY BE UNPROVED AT THIS POINT.
                         * Under FINISH both are a completion that already
                         * happened and there is nothing to hold; under FENCE each
                         * is a wait this process ISSUED on the consumer's
                         * context, and the slot's written tag - the one thing
                         * that lets any caller bind and rasterise this pool - is
                         * gated on both. The gate is a condition and not an
                         * assertion because the consequence of an unproved
                         * handoff is a refusal and not a report: a GPU-source
                         * draw reading a pool whose visibility was never
                         * established is the one outcome this mode must not
                         * produce, whatever the drains and the switches said. */
                        if (fx && !(fx_inw && fx_outw)) {
                            g_ar_state = -1;
                            ar_bad = 1;
                            g_fc.fx_refusals++;
                            g_fc.fx_unproved_refusals++;
                            if (!g_fx_cre_said) {
                                g_fx_cre_said = 1;
                                fprintf(stderr, "#95 SL_BG_TF_AUX_SYNC=FENCE with "
                                                "SL_BG_TF_AUX_REAL=%s: a prepass "
                                                "bracket reached the end of its "
                                                "chain with one of the two "
                                                "cross-context handoffs UNPROVED - "
                                                "the input wait on the auxiliary "
                                                "context was %s and the output wait "
                                                "on the main context was %s. A pool "
                                                "whose visibility was not "
                                                "established must never be "
                                                "rasterised, so it is NOT re-bound, "
                                                "the slot stays UNWRITTEN, the "
                                                "served draw is REFUSED to the CPU "
                                                "path, the capability LATCHES, and "
                                                "this is reported once per launch\n",
                                        sl_bgtf_ar_name(ar_mode),
                                        fx_inw ? "issued" : "NOT issued",
                                        fx_outw ? "issued" : "NOT issued");
                            }
                        } else {
                            if (fx) {
                                g_fc.fx_in_proved++;
                                g_fc.fx_out_proved++;
                            }
                            p_BindBuffer(GL_ARRAY_BUFFER, sl->buf);
                            p_BindBuffer(GL_ARRAY_BUFFER, 0);
                            g_c.ar_rebinds++;
                            ar_wrote = 1;
                            g_c.ar_writes++;
                            /* AND THE COMPLETED PREPASS, RECORDED IN THE FENCE
                             * PATH'S OWN STRUCT AS WELL. It is counted at the
                             * TAG rather than beside the two proofs above so
                             * that the proof and the tag are independent records
                             * of the same bracket: an edit which tagged a write
                             * from outside this gate would raise fx_writes
                             * without raising fx_out_proved, which is the one
                             * shape a capture must be able to reject and which
                             * no image and no ar_ count would show. */
                            if (fx) g_fc.fx_writes++;
                        }
                    }
                }
                } /* the opening drain's own clean answer: the one shape that
                   * reaches the objects, the state and the draw */
            }
            /* AND NOTHING MAY BE HELD AT THE POINT THIS BRACKET RETURNS
             * (SL_BG_TF_AUX_SYNC). Every path above either deletes what it
             * created or abandons it under a lost-context verdict, so both
             * handles are NULL here on every one of them. It is a COUNT rather
             * than an assertion so that an edit which broke that says so in a
             * number a fixture can read, instead of leaking silently; the handles
             * are cleared as well, because a bracket that leaked one must not
             * also be the bracket that double-deletes it. No GL is issued here:
             * which context is current differs by path, and a delete on the wrong
             * one would be worse than the leak. */
            if (fx_in != NULL || fx_out != NULL) {
                g_fc.fx_leaks++;
                fx_in = NULL;
                fx_out = NULL;
            }
        }
        /* THE MAIN CONTEXT'S DRAW, WHICH THE AUXILIARY CONTROL OMITS. Guarded
         * rather than moved, so that every other arm's sequence - the real
         * nonzero path's and the zero-count arm's alike - is the statement it
         * always was, and the one arm that relocates the call is the one that
         * does not make it here. `zc_calls` counts calls made on THIS context,
         * so it does not rise on that arm and ax_main_omits above is what
         * accounts for the difference. */
        if (!aux && !arl) {
            glDrawArrays(GL_POINTS, 0,
                         zero_count ? (GLsizei) 0 : (GLsizei) g_nverts);
            if (zero_count) g_c.zc_calls++;
        }
        /* AND A LOST MAIN CONTEXT STOPS HERE - IN A FIXTURE. Everything below
         * this point issues GL, and this process can no longer say where it
         * would land. In the product the terminate above did not return, so this
         * is unreachable; it is kept because a fixture intercepts the terminate
         * and the refusal is then the behaviour under test. Reaching it in the
         * product would mean the terminate returned, and refusing is the right
         * thing to do in that case too. */
        if (ax_dead || ar_dead) {
            g_c.refusals++;
            return -1;
        }
        /* THE EMPTY DRAW UNDER THE NEUTRAL PROGRAM, COUNTED WHERE IT WAS ISSUED
         * AND UNDER WHAT THIS BRACKET ACTUALLY SELECTED. `zero_count` is 1 by
         * the coupling, so the call above is exactly
         * glDrawArrays(GL_POINTS, 0, 0); np_cur is the switch that was issued
         * rather than the switch that was requested, so np_draws_neutral equals
         * np_draws on an honest run and an inequality says a draw was issued at
         * this site under a program this control did not select - which is the
         * one outcome that would be reported as a neutral run without being
         * one. */
        if (neutral) {
            g_c.np_draws++;
            if (np_cur) g_c.np_draws_neutral++;
        }
        /* THE PREPASS DRAW WAS ISSUED, recorded immediately after the call for
         * the post-reset boundary's own guard. That control's boundary is
         * DEFINED as the point after this call, so a bracket that did not make
         * it - the state-only arm, which this control refuses, or any later
         * edit that put a branch around the statement above - leaves this 0 and
         * the boundary issues nothing. The count is of a call made, not of an
         * arm selected.
         *
         * AND NOT ON THE ARM THAT DID NOT MAKE IT ON THIS CONTEXT. The
         * auxiliary-context control relocates the call, so no prepass draw was
         * issued in this bracket on the main context and this stays 0 - which is
         * the same rule the sentence above states, applied to the one arm that
         * exercises it. The post-reset boundary is refused alongside that
         * control twice over, by the arbiter and by the site guard, so this
         * guard changes no reachable behaviour today; it is here because the
         * flag's meaning is "a call was made HERE" and leaving it raised would
         * make that false. */
        if (!aux && !arl) pr_drew = 1;
        /* THE EMPTY DRAW, COUNTED WHERE IT WAS ISSUED. On every one of this
         * mode's rows the call above is exactly glDrawArrays(GL_POINTS, 0, 0) -
         * `zero_count` is 1 by the coupling - so this count and es_setups are
         * the mode's statement that the call happened and submitted nothing.
         *
         * AND UNDER WHICH PROGRAM, from what the calls above selected rather
         * than from the row's name. The two counts sum to es_draws, so a row
         * whose declared program and issued switches disagreed would show up
         * here as the wrong one of the pair. */
        if (es_mode != SL_BGTF_ES_OFF) {
            g_c.es_draws++;
            if (es_cur_prog) g_c.es_draws_prog_tf++;
            else             g_c.es_draws_prog0++;
        }
        /* AND CLOSED IN THE INVERSE ORDER, innermost first. A row that rebinds
         * here goes back to the prepass program - not to 0 - because the common
         * restoration below is what sets 0.
         *
         * A ROW MAY ALSO LEAVE PROGRAM 0 CURRENT, which is S, and that leaves
         * the caller nothing different: the common restoration below issues
         * glUseProgram(0) on every arm, so the state at close is the state every
         * other arm hands back. What S omits is the one call between this draw
         * and that cleanup. */
        /* v0.4 THE NEUTRAL-PROGRAM CONTROL'S ONE RESTORE AFTER THE CALL
         * (SL_BG_TF_NEUTRAL_PROG), default off and one cached test when it is
         * off.
         *
         * BACK TO THE ORDINARY PREPASS PROGRAM, NOT TO 0, which is what hands
         * the bracket to the common cleanup in the state that cleanup has on
         * every other arm: it issues glUseProgram(0) itself a few lines below,
         * and `pr_prog0`/`es_cur_prog` are maintained there. Restoring to 0
         * here instead would make the cleanup's call redundant on this arm
         * alone, which is a second difference from row A and not the one this
         * control exists to measure.
         *
         * IMMEDIATELY AFTER THE DRAW, so the interval under the neutral program
         * contains exactly one call. np_restores equals np_draws on an honest
         * run, and an inequality would mean the bracket was handed on with the
         * second program still current. */
        if (neutral) {
            p_UseProgram(g_prog);
            g_c.np_restores++;
            np_cur = 0;
        }
        if (es_post_zero) {
            p_UseProgram(0);
            g_c.es_prog0_switches++;
            g_c.es_prog_post_zero++;
            es_cur_prog = 0;
        }
        if (es_post_prog) {
            p_UseProgram(g_prog);
            g_c.es_prog0_restores++;
            g_c.es_prog_post_prog++;
            es_cur_prog = 1;
        }
        if (!es_discard) {
            glEnable(GL_RASTERIZER_DISCARD);
            g_c.es_discard_ons++;
        }
        if (iso) tf_pos_restore();
        /* THE PRIVATE-VERTEX-ARRAY CONTROL'S DRAW COUNT, raised only on a
         * CONFIRMED bind - `private_vao` is already the acquire's answer, not
         * the switch - so a run whose binds were all refused reports 0 draws
         * under the control beside a positive refusal count. */
        if (private_vao) g_c.pv_draws++;
    }
    /* THE MATCHING END, UNDER THE SAME ONE CONDITION as the begin above, so the
     * pair cannot be split by any edit that does not change both. */
    if (es_tf) {
        p_EndTransformFeedback();
        if (es_mode != SL_BGTF_ES_OFF) g_c.es_tf_ends++;
        /* AND THE SCOPE IS NOW CLOSED, cleared under the same one condition as
         * the flag was raised, so the pair cannot be split by any edit that
         * does not change both. */
        pr_tf_open = 0;
    }

    /* v0.4 THE PROBE'S PHASE 2, POST_DRAW_RAW (SL_BG_GLSTATE), default off.
     *
     * BEFORE THE RESTORATION, WHICH IS THE WHOLE POINT OF THE PLACEMENT. The
     * program is still current, the indexed transform-feedback binding is still
     * made and rasterizer discard is still on. The three calls below put all
     * three back, so reading this state after them - which phase 3 does - cannot
     * recover it, and the pair is what makes "the restoration put back what it
     * changed" a comparison rather than an assertion. */
    if (sl_bgstate_armed()) {
        struct sl_bgstate_producer pk;
        bgtf_state_key(&pk, s, dmode, k1b, k2b, 0, state_only);
        sl_bgstate_post_draw_raw(&pk);
    }

    p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glDisable(GL_RASTERIZER_DISCARD);
    pr_disc_off = 1;
    p_UseProgram(0);
    /* THE COMMON RESTORATION'S TWO CALLS, RECORDED FOR THE POST-RESET
     * BOUNDARY'S GUARD. Each flag is raised immediately after the call it
     * records, and `es_cur_prog` is carried to 0 here because this call is the
     * last glUseProgram the bracket makes on every arm - the empty-draw rows'
     * own switches sit before it and one of them deliberately leaves program 0
     * current until exactly this point. That is what lets the boundary below
     * require program 0 from the calls this process issued rather than from the
     * order the statements happen to be in. Nothing is read back from GL. */
    pr_prog0 = 1;
    es_cur_prog = 0;

    /* v0.4 THE PRIVATE-VERTEX-ARRAY BRACKET CLOSES HERE (SL_BG_TF_PRIVATE_VAO),
     * which is BEFORE the caller's own raster restoration: this function returns
     * straight into sl_gfx_dl.c, which names the original per-mesh position
     * stream again, and that has to happen on the array object the caller owns.
     * `held` is what makes this a no-op on every other arm, so an off launch
     * pays one integer test and no call. */
    tf_vao_release();

    /* v0.4 THE PROBE'S PHASE 3, POST_RESTORE (SL_BG_GLSTATE), default off. The
     * ordinary restoration above has run and nothing else has happened since. */
    if (sl_bgstate_armed()) {
        struct sl_bgstate_producer pk;
        bgtf_state_key(&pk, s, dmode, k1b, k2b, 0, state_only);
        sl_bgstate_post_restore(&pk);
    }

    /* v0.4 THE POST-RESET BOUNDARY (SL_BG_TF_POST_RESET), default off: two
     * cached integer tests and no call.
     *
     * HERE AND NOWHERE ELSE, AND THE PLACEMENT IS THE WHOLE DEFINITION. The
     * prepass draw has been issued; the matched feedback scope, if the arm
     * opened one, has been closed; the indexed output binding has been released;
     * rasterizer discard has been disabled and program 0 made current by the
     * common restoration above; the private-vertex-array bracket has released
     * whatever it held. Nothing has happened since but the probe's own phase-3
     * observation, which issues no GL call. This function then returns into
     * src/gfx/sl_gfx_dl.c, which names the original per-mesh position stream and
     * issues the raster draw - so this is the last instant inside the prepass
     * and the first one after all of it, which is the instant the arms name.
     *
     * AFTER THE PROBE'S PHASE 3 AND NOT BEFORE IT. That phase's own contract is
     * that the ordinary restoration has run and NOTHING ELSE HAS HAPPENED SINCE;
     * inserting a call before it would make that statement false and would
     * change what an armed probe records on every arm of this control. Putting
     * the calls after it leaves the probe reading exactly what it read before.
     *
     * AND IT FAILS CLOSED, PER BRACKET, ON FOUR THINGS THIS BRACKET ISSUED. A
     * bracket that cannot show the prepass draw, a closed feedback scope, the
     * common discard disable and the common glUseProgram(0) - with program 0 the
     * last program it made current - issues NOTHING here, counts the shortfall
     * and names it once. The prepass's own early returns are all above this
     * point and never reach it; `private_vao` is required to be 0 as well, which
     * the arbiter already guarantees by refusing that control, so the guard
     * holds whether or not the arbiter is consulted. `neutral` is required to be
     * 0 on the same terms and for a reason of its own: that control leaves the
     * ordinary prepass program current until the common restoration below, so a
     * bracket of its does satisfy `es_cur_prog == 0` at this point, and what the
     * arms could not then attribute is which of three programs the bracket's
     * history was about. The arbiter refuses the pair; this is what makes the
     * refusal structural.
     *
     * THE ARM NEEDS NO PROGRAM SWITCH AND MAKES NONE. Program 0 is already
     * current, which is one of the things the guard requires, so the empty draw
     * is issued under it without a glUseProgram of any kind - and the scope
     * being closed is another, so no program is ever changed while transform
     * feedback is active. The order on the arm that issues both is the draw and
     * then the wait, stated once, here.
     *
     * NO ERROR STATE IS TOUCHED. glGetError is not called on any arm, so an
     * armed buffer diagnostic's own accounting is untouched and nothing here is
     * evidence that any call did or did not raise an error.
     *
     * THESE ARE EXPERIMENT ARMS AND NOT A FIX. No cause is named for the one
     * blue byte at (71,113); see the header. */
    if (pr_mode != SL_BGTF_PR_OFF) {
        if (pr_drew && !pr_tf_open && pr_disc_off && pr_prog0
            && es_cur_prog == 0 && !private_vao && !neutral) {
            g_c.pr_setups++;
            if (pr_draw) {
                glDrawArrays(GL_POINTS, 0, (GLsizei) 0);
                g_c.pr_draws++;
                /* AND UNDER WHICH PROGRAM, from what this bracket made current
                 * rather than from the arm's name. The guard above admits only
                 * program 0, so this equals pr_draws on every honest run and an
                 * inequality says a draw was issued at the boundary without the
                 * restoration accounted for. */
                if (es_cur_prog == 0) g_c.pr_draws_prog0++;
            }
            if (pr_finish) {
                glFinish();
                g_c.pr_finishes++;
            }
        } else {
            /* A PRECONDITION THAT IS NOT PROVED IS A SKIP, AND A SKIP IS NOT A
             * CALL. The bracket is left exactly as it is without this control,
             * the shortfall is counted per bracket so a run cannot report an arm
             * it did not execute, and the reason is named once per launch rather
             * than once per mesh - a repeated line would bury the run's own
             * output and says nothing a reader does not already have. */
            g_c.pr_skips++;
            if (!g_pr_said) {
                g_pr_said = 1;
                fprintf(stderr, "#95 SL_BG_TF_POST_RESET=%s issued NOTHING at "
                                "the post-reset boundary of at least one prepass "
                                "bracket: prepass draw issued %d, feedback scope "
                                "left open %d, common discard disable %d, common "
                                "glUseProgram(0) %d, program 0 last made current "
                                "%d, private vertex array held %d, neutral "
                                "program in force %d - all seven must hold. The "
                                "bracket is unchanged and the shortfall is "
                                "counted as pr_skips; this is reported once per "
                                "launch\n",
                        sl_bgtf_pr_name(pr_mode), pr_drew, pr_tf_open,
                        pr_disc_off, pr_prog0, es_cur_prog == 0, private_vao,
                        neutral);
            }
        }
    }

    /* THE KEY IS INSTALLED ON BOTH ARMS; THE CONTENTS ARE NOT.
     *
     * `filled` carries the key, so the cache, the generation, the depth and
     * FAR0's coefficient pairs behave identically under the control and a run of
     * meshes sharing a key still pays one setup. `written` carries the OUTPUT,
     * and a setup whose draw was omitted has none - so the binding and the
     * production diagnostic refuse this slot rather than reading storage no
     * transform ever filled.
     *
     * AND NO WORK IS CLAIMED FOR IT. `prepasses`, `vertices` and `walks` count
     * submissions that ran and vertices that were transformed; with the draw
     * omitted neither happened, so they are untouched and the control's own
     * bounded counts record the cadence instead. */
    sl->gen = g_gen;
    sl->dmode = dmode;
    sl->k1bits = k1b;
    sl->k2bits = k2b;
    sl->nverts = g_nverts;
    sl->filled = 1;
    /* AND A ZERO-COUNT CALL WROTE NOTHING EITHER (SL_BG_TF_ZERO_COUNT). The call
     * was made and submitted no vertex, so the transform produced no output and
     * this slot carries its KEY without CONTENTS - the same split the state-only
     * arm makes, reached by a different route. sl_bgtf_bind_clip and the
     * production diagnostic both require `written`, so an unwritten slot cannot
     * be bound, rasterised or recorded from ANY caller, including a direct
     * sl_bgtf_bind_clip; no second guard is added here, because the existing one
     * is already the only way to read the storage. */
    /* AND UNDER THE REAL-PREPASS MODE THE TAG IS THE CHAIN, NOT THE ARM
     * (SL_BG_TF_AUX_REAL). On that mode neither `state_only` nor `zero_count` is
     * in force, so the expression above would answer 1 for every bracket this
     * mode reached - including one whose switch failed, whose objects were not
     * shared, or whose output was written on the other context and never made
     * visible here. `ar_wrote` is raised at exactly one place, after the output
     * handoff's producer half - the completion glFinish under FINISH, the fence
     * with its producer flush under FENCE - the verified switch back, the
     * consumer-side wait under FENCE and the pool's re-bind, so it is the only
     * honest answer: anything less leaves the slot UNWRITTEN and
     * sl_bgtf_bind_clip refuses it from every caller. */
    if (arl) sl->written = ar_wrote;
    else     sl->written = (state_only || zero_count) ? 0 : 1;
    sl->used = ++g_tick;
    if (state_only) {
        g_c.so_setups++;
        g_c.so_omitted++;
    } else if (zero_count) {
        /* NO WORK IS CLAIMED FOR AN EMPTY CALL. `prepasses`, `vertices` and
         * `walks` count submissions that ran and vertices that were actually
         * transformed; this call transformed none, so all three are untouched
         * and the control's own bounded counts record the cadence instead. One
         * setup bracket ran and it issued exactly one call, so this count equals
         * zc_calls on every honest run and a reader can check that equality. */
        g_c.zc_setups++;
        /* v0.4 THE EMPTY-DRAW STATE MODE'S SETUP COUNT (SL_BG_TF_EMPTY_STATE).
         * Raised beside zc_setups rather than instead of it, because the mode
         * runs ON this arm and that arm's own cadence must stay readable. One
         * bracket issued exactly one empty draw, so this equals es_draws on
         * every honest run; together with es_reuses it accounts for every mesh
         * the caller served. No work is claimed for any of it: `prepasses`,
         * `vertices` and `walks` are untouched here exactly as they are on
         * Z_KNOWN, and the slot is marked unwritten below. */
        if (es_mode != SL_BGTF_ES_OFF) g_c.es_setups++;
        /* v0.4 THE NEUTRAL-PROGRAM CONTROL'S SETUP COUNT
         * (SL_BG_TF_NEUTRAL_PROG). Raised beside zc_setups rather than instead
         * of it, because the control runs ON that arm and that arm's own cadence
         * must stay readable. One bracket issued exactly one empty draw under
         * one switch and one restore, so this equals np_draws, np_switches and
         * np_restores on every honest run; together with np_reuses and np_skips
         * it accounts for every mesh the caller served. No work is claimed for
         * any of it: `prepasses`, `vertices` and `walks` are untouched here
         * exactly as they are on the zero-count arm, and the slot is marked
         * unwritten below. */
        if (neutral) g_c.np_setups++;
        /* v0.4 THE AUXILIARY-CONTEXT CONTROL'S SETUP COUNT
         * (SL_BG_TF_AUX_EMPTY). Raised beside zc_setups for the reason the two
         * above are, with one difference that has to be read with it: on this
         * arm zc_calls does NOT rise, because the empty call was made on the
         * other context. So this count equals ax_main_omits, ax_draws,
         * ax_switches and ax_restores on every honest run, and NOT zc_calls -
         * which is the equality a reader of the zero-count arm would otherwise
         * expect and the one thing this arm deliberately breaks. Together with
         * ax_reuses and ax_skips it accounts for every mesh the caller served.
         * No work is claimed for any of it: `prepasses`, `vertices` and `walks`
         * are untouched here exactly as they are on the zero-count arm, and the
         * slot is marked unwritten below.
         *
         * A BRACKET WHOSE SWITCH FAILED REACHES THIS POINT AND IS COUNTED HERE
         * TOO, which is deliberate: ax_setups is the brackets this control
         * reached, and a failed switch raises ax_switch_fails beside it while
         * ax_draws stays behind. The inequality is the record of the failure. */
        if (aux) g_c.ax_setups++;
    } else if (arl) {
        /* v0.4 THE REAL-PREPASS MODE'S OWN ACCOUNT (SL_BG_TF_AUX_REAL), in its
         * own arm of this chain rather than inside the one below it, so that the
         * default path's statement is the one it always was and this mode's
         * cadence is readable beside it.
         *
         * ar_setups IS THE BRACKETS THIS MODE REACHED, the refused ones
         * included: a bracket whose switch or object check failed is counted
         * here while ar_prepasses stays behind, and the inequality is the record
         * of the failure. Together with ar_reuses and ar_skips it accounts for
         * every mesh the caller served.
         *
         * THE ARM'S OWN COUNT IS RAISED FROM WHAT THE BRACKET DID. CPU_SOURCE
         * counts every bracket it reached, the original positions being what
         * rasterises whether or not the pool was written; GPU_SOURCE counts only
         * a bracket with a COMPLETED, VISIBLE write behind it, because only such
         * a slot can be bound. A GPU_SOURCE bracket that refused therefore
         * raises neither, which is what keeps ar_gpu_submits a count of pools a
         * raster may actually read.
         *
         * AND THE WORK COUNTS RISE ONLY ON A COMPLETED WRITE. `prepasses`,
         * `vertices` and `walks` mean submissions that RAN and vertices that
         * were actually transformed, and under this mode both really happened -
         * on the other context, with the real count. They are raised here rather
         * than withheld because on the GPU_SOURCE arm the caller then rasterises
         * that pool and raises `raster_draws` for it, and a run reporting raster
         * draws against zero prepasses would be telling a reader something
         * false. A bracket whose write did not complete claims nothing. */
        g_c.ar_setups++;
        if (ar_mode == SL_BGTF_AR_CPU)   g_c.ar_cpu_draws++;
        else if (ar_wrote)               g_c.ar_gpu_submits++;
        if (ar_wrote) {
            g_c.prepasses++;
            g_c.vertices += g_nverts;
            if (!g_walk_counted) { g_walk_counted = 1; g_c.walks++; }
        }
    } else {
        g_c.prepasses++;
        g_c.vertices += g_nverts;
        if (!g_walk_counted) { g_walk_counted = 1; g_c.walks++; }
    }
    /* v0.4 AND THE AUXILIARY-CONTEXT CONTROL'S OWN LATE REFUSAL
     * (SL_BG_TF_AUX_EMPTY), taken HERE and not at the switch that failed.
     *
     * WHY THE REFUSAL IS LATE ON THIS ONE PATH. Every other refusal in this
     * function is taken before any GL state is written, which is what lets it
     * return at once. A switch to the auxiliary context can only fail at the
     * draw, by which time the common setup has written the program, the
     * uniforms, the discard enable, the pointer and two bindings - so returning
     * from there would hand the caller a half-built bracket. The main context is
     * still current when that switch fails, so the right thing is exactly what
     * happens: nothing is issued on either context, the ordinary restoration
     * runs unchanged, the slot is keyed and marked unwritten as this arm always
     * marks it, and the served draw is refused here. The caller draws from the
     * CPU, which is what it does on every refusal.
     *
     * A LOST MAIN CONTEXT NEVER REACHES THIS LINE: it returns from the draw
     * site, before the restoration, for the reason stated there. */
    if (ax_bad) {
        g_c.refusals++;
        return -1;
    }
    /* v0.4 AND THE REAL-PREPASS MODE'S OWN LATE REFUSAL (SL_BG_TF_AUX_REAL),
     * taken HERE and not at the step that failed, for exactly the reason the
     * refusal above it is late: the only failures that can reach this point are
     * a switch that failed at the prepass and an object the auxiliary context
     * did not share, and by then the common setup has written the program, the
     * uniforms, the discard enable, the pointer and the bindings on the MAIN
     * context - so returning from there would hand the caller a half-built
     * bracket. The main context is verified current in both cases, so the right
     * thing is what happens: nothing was issued on the auxiliary context, the
     * ordinary restoration runs unchanged, the slot is keyed and marked
     * UNWRITTEN, and the served draw is refused here. The caller draws from the
     * CPU, which is what it does on every refusal.
     *
     * A LOST OR UNVERIFIABLE CONTEXT NEVER REACHES THIS LINE: it returns from
     * the prepass site, before the restoration, for the reason stated there. */
    if (ar_bad) {
        g_c.refusals++;
        return -1;
    }
    return s;
}

int sl_bgtf_bind_clip(int slot)
{
    /* THE SLOT MUST BELONG TO THIS WALK. `filled` alone would let a caller
     * holding an index from a previous walk bind a pool computed under a
     * different transform. sl_gfx_dl.c only ever passes back what
     * sl_bgtf_prepass just returned, so this is unreachable there - which is
     * exactly why it is cheap to check here rather than left as a property of
     * one caller's discipline.
     *
     * AND IT MUST HAVE OUTPUT BEHIND IT. `filled` means the key is installed,
     * which a setup-only slot (SL_BG_TF_STATE_ONLY) also satisfies, and binding
     * one would hand the raster storage no transform ever wrote. `written` is
     * what says a prepass draw filled it, and requiring it here is what makes an
     * unwritten pool unbindable from any caller - including the ordinary one, if
     * a later edit ever let a state-only slot reach it. A refusal costs no GL
     * call and raises no bind, so the count stays the truthful one. */
    if (g_state <= 0 || slot < 0 || slot >= TF_SLOTS || !g_slot[slot].filled
        || !g_slot[slot].written || g_slot[slot].gen != g_gen) return -1;
    /* PROGRAM 0 - THE FIXED PIPELINE - IS WHAT RASTERISES. This is the whole
     * candidate: the transform happened in the prepass, and the draw that
     * follows is the original path with the original arrays.
     *
     * Only the POSITION array moves to the prepass pool. The array buffer is
     * bound for the glVertexPointer call and unbound immediately, because a
     * buffer left bound to GL_ARRAY_BUFFER makes every later client pointer -
     * the pass's colour, UV and fog arrays - be read as an offset into it
     * instead of as a host address. */
    p_UseProgram(0);
    p_BindBuffer(GL_ARRAY_BUFFER, g_slot[slot].buf);
    glVertexPointer(4, GL_FLOAT, 0, (const GLvoid *) 0);
    p_BindBuffer(GL_ARRAY_BUFFER, 0);
    g_c.binds++;
    return g_slot[slot].dmode;
}

/* ONE ACTUAL SUBMISSION ON A PREPASS POOL, counted where the draw really
 * happens.
 *
 * A BIND IS NOT A DRAW, and counting one as the other was the defect this
 * replaces. Binding a pool happens before the mesh's runs are known: the draw
 * loop may then split the mesh into several runs, or filter every triangle and
 * submit none. Counting at the bind therefore credited coverage to a mesh that
 * drew nothing, and missed the fact that one bind can serve several runs.
 *
 * The caller passes the depth of the pool that is ACTUALLY the vertex source
 * for this run, not the depth the walk last computed, so a run that fell back
 * to the CPU array contributes nothing here however the walk was admitted.
 * An empty run is not a submission. */
void sl_bgtf_count_submission(int dmode, unsigned n)
{
    if (n == 0u) return;
    g_c.raster_draws++;
    g_c.raster_indices += n;
    if ((unsigned) dmode < 3u) g_c.dcalls[dmode]++;
}

void sl_bgtf_unbind(void)
{
    /* THE SELECTION DIES WITH THE BINDING, and before the early return: the
     * thing a selection names is a pool that is the vertex source, and this is
     * the point at which none is. Clearing it after the `g_state` test would
     * leave a selection alive across an unbind on a module that had latched
     * unavailable in between. */
    (void) dump_clear_selection();
    /* v0.4 THE PRIVATE-VERTEX-ARRAY BRACKET IS CLOSED HERE IF ANYTHING EVER
     * LEAVES IT OPEN (SL_BG_TF_PRIVATE_VAO), default off and one integer test.
     *
     * sl_bgtf_prepass closes its own bracket on the one straight-line path that
     * opens it, so on today's code `held` is never set at this point. The guard
     * is here anyway, and BEFORE the `g_state` test, because an early
     * abandonment while the context is still valid must leave the caller's state
     * restored and no half-active control - and a module that latched
     * unavailable between the bind and this call is exactly the case the
     * `g_state` test would otherwise skip the restore for. A restore the context
     * will not confirm is TERMINAL here as it is anywhere else, and it is
     * deliberately asked before the cleanup below: the element and buffer
     * bindings that follow would be written on an unknown array object. */
    if (g_pvao_held) tf_vao_release();
    if (g_state <= 0) return;
    /* Everything this module touched, put back as it was found: no program, no
     * array or element binding, no indexed transform-feedback binding, and
     * rasterizer discard off. The vertex source itself is the caller's to
     * restore, because only the caller knows which of its own arrays the next
     * draw reads.
     *
     * THIS ELEMENT CLEAR IS PRE-EXISTING AND UNCHANGED, which is why the private
     * vertex-array control's own notes are scoped to its bracket and claim
     * nothing about a walk's whole lifecycle. It ran here before that control
     * existed and still runs with no private object bound, so the object it
     * writes is the caller's - and that is not a demonstrated defect in an
     * ordinary draw, because src/gfx/sl_gfx_dl.c admits a walk only when
     * g_bgv_live (:19055) and every admitted draw binds g_bgv_index for itself
     * (:17078) and unbinds it (:17082). */
    p_UseProgram(0);
    p_BindBuffer(GL_ARRAY_BUFFER, 0);
    p_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    p_BindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glDisable(GL_RASTERIZER_DISCARD);
    g_walk_live = 0;
}

/* ======================================================================== *
 * THE PRODUCTION BUFFER DIAGNOSTIC (SL_BG_TF_DUMP).
 *
 * See the header for what this measures, what follows from neither outcome, and
 * the list of things an off launch does not do. This section holds the switch,
 * the selector, the one bounded readback, the record and the seal.
 *
 * IT IS NOT A SECOND IMPLEMENTATION OF ANYTHING ABOVE. The pool it reads is the
 * one sl_bgtf_prepass filled and sl_bgtf_bind_clip made the vertex source; the
 * key it checks is the slot cache's own; the depth cases are the same three.
 * Nothing here compiles a shader, starts a transform, or produces a vertex -
 * compiling a copy of TF_VS into another program or another context and calling
 * its output the production output is exactly the mistake the previous output
 * probe made, and it is why this reads the live slot instead.
 *
 * THE ORDINARY PATH IS INDEPENDENT OF ALL OF IT. A missing readback entry
 * point, a refused selector, a failed allocation or a failed write changes
 * nothing about which walk is admitted, which slot a draw gets, which pool is
 * bound or what is rasterised: the only product state this section writes is
 * its own, and every exit leaves the GL bindings exactly as it found them.
 */

/* ---- the switch and the selector --------------------------------------- */

static int g_dump_optin = -1;          /* the switch, parsed once           */
static int g_dump_armed;               /* ... and the provenance accepted   */

/* The selected moment and draw. ~0u in a numeric field means ANY, which only an
 * explicit '*' produces; -2 is the depth's wildcard, because 0, 1 and 2 are all
 * real depth modes and no unsigned value is free to mean "any" there. */
static unsigned g_dw_read  = ~0u, g_dw_frame = ~0u, g_dw_cloud = ~0u;
static unsigned g_dw_mesh  = ~0u, g_dw_mat   = ~0u;
static int      g_dw_dmode = -2;
static char     g_dw_item[64];
static char     g_dw_sel[160];

/* THE PROVENANCE, SUPPLIED AND VALIDATED, for the reason the background
 * snapshot's writer records: a capture that does not say which binary and which
 * source produced it is not evidence, and hashing the running image from inside
 * the graphics path would mean reading the whole image at a frame boundary for a
 * value the launcher already has. So the launcher passes them in and this
 * validates them - 64 lowercase hex characters, not all zeros, or the
 * diagnostic does not arm. An all-zero digest is what an unset field looks
 * like, and a capture may not claim a provenance nobody established. */
static unsigned char g_dump_exe_sha[32];
static unsigned char g_dump_src_sha[32];

/* ---- the one attempt, and the outcome ---------------------------------- */

static int   g_dump_done;              /* the attempt is SPENT              */
static int   g_dump_state;             /* 0 none, 1 complete, -1 refused    */
static char  g_dump_status[192];
static struct sl_bgtf_dump_counters g_dc;

/* The accepted draw, copied out of the offer so the capture cannot read a
 * caller's struct that has since moved on. The pointers inside it are the
 * pass's own arrays and are valid for exactly the draw that was accepted, which
 * is why the selection is cleared at every point that draw stops being
 * current. */
static struct sl_bgtf_dump_site g_dump_sel;
static int                      g_dump_selected;

int sl_bgtf_dump_state(void) { return g_dump_state; }
const char *sl_bgtf_dump_status(void) { return g_dump_status; }
const struct sl_bgtf_dump_counters *sl_bgtf_dump_stats(void) { return &g_dc; }

static int dump_clear_selection(void)
{
    const int was = g_dump_selected;
    g_dump_selected = 0;
    memset(&g_dump_sel, 0, sizeof g_dump_sel);
    return was;
}

/* THE OUTCOME, RECORDED ONCE AND NEVER UPGRADED. The first reason is the cause;
 * a later consequence of it must not overwrite the thing that went wrong. */
static void dump_fail(const char *status)
{
    if (g_dump_state == 0) {
        g_dump_state = -1;
        snprintf(g_dump_status, sizeof g_dump_status, "%s", status);
        fprintf(stderr, "#95 background TF buffer diagnostic: %s - the one attempt is "
                        "SPENT and this record is NOT a measurement. It is neither a "
                        "zero-difference result nor a CPU-array substitute, and no "
                        "further draw, frame or slot is tried.\n", status);
    }
}

/* ---- the grammar, which fails closed ----------------------------------- */

/* A component is a single '*' or a bounded decimal, and never anything else.
 * The overflow test covers the ADDITION as well as the multiply, so a value
 * naming a moment that cannot exist cannot wrap into one that can. On success
 * *s is advanced past the component and past one ':' if present. */
#define SL_BGTF_DUMP_MAX_DIGITS 10      /* 4294967295 is ten digits */

static int dump_part(const char **s, unsigned *out)
{
    const char *p = *s;
    unsigned v = 0u;
    int digits = 0;

    if (p == NULL || *p == '\0') return 0;      /* an absent field is not any */
    if (*p == '*') {
        p++;
        if (*p != '\0' && *p != ':') return 0;  /* '*7' is not a wildcard     */
        *out = ~0u;
    } else {
        while (*p >= '0' && *p <= '9') {
            const unsigned d = (unsigned) (*p - '0');
            if (++digits > SL_BGTF_DUMP_MAX_DIGITS) return 0;
            if (v > (0xFFFFFFFFu - d) / 10u) return 0;
            v = v * 10u + d;
            p++;
        }
        if (digits == 0) return 0;              /* neither a digit nor '*'    */
        if (*p != '\0' && *p != ':') return 0;  /* trailing junk, e.g. '29x'  */
        if (v == ~0u) return 0;                 /* ~0u is the wildcard        */
        *out = v;
    }
    if (*p == ':') p++;
    *s = p;
    return 1;
}

static int dump_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;                                  /* one spelling: lowercase    */
}

static int dump_sha256_arg(const char *text, unsigned char *out, const char *what)
{
    int i, nonzero = 0;
    if (text == NULL || *text == '\0') {
        fprintf(stderr, "#95 background TF buffer diagnostic: REFUSED - %s was not "
                        "supplied. A capture with no provenance cannot be tied to a "
                        "build, so nothing is recorded.\n", what);
        return 0;
    }
    if (strlen(text) != 64u) {
        fprintf(stderr, "#95 background TF buffer diagnostic: REFUSED - %s is %u "
                        "characters, not the 64 of a sha256\n",
                what, (unsigned) strlen(text));
        return 0;
    }
    for (i = 0; i < 32; i++) {
        const int hi = dump_hex_nibble(text[i * 2]);
        const int lo = dump_hex_nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            fprintf(stderr, "#95 background TF buffer diagnostic: REFUSED - %s is not "
                            "64 lowercase hex characters\n", what);
            memset(out, 0, 32u);
            return 0;
        }
        out[i] = (unsigned char) ((hi << 4) | lo);
        if (out[i] != 0u) nonzero = 1;
    }
    if (!nonzero) {
        fprintf(stderr, "#95 background TF buffer diagnostic: REFUSED - %s is all "
                        "zeros, which is what an unset field looks like; a capture may "
                        "not claim a provenance nobody established\n", what);
        memset(out, 0, 32u);
        return 0;
    }
    return 1;
}

static void dump_disarm(void)
{
    g_dump_armed = 0;
    g_dump_optin = 0;
    g_dw_read = g_dw_frame = g_dw_cloud = g_dw_mesh = g_dw_mat = 0u;
    g_dw_dmode = -1;                  /* not -2: no depth mode is -1        */
    g_dw_item[0] = '\0';
    memset(g_dump_exe_sha, 0, sizeof g_dump_exe_sha);
    memset(g_dump_src_sha, 0, sizeof g_dump_src_sha);
}

int sl_bgtf_dump_opt_in(void)
{
    const char *v;
    unsigned dm = 0u;

    if (g_dump_optin >= 0) return g_dump_optin;
    g_dump_optin = 0;
    v = getenv("SL_BG_TF_DUMP");
    if (v == NULL || *v == '\0' || (v[0] == '0' && v[1] == '\0')) return 0;
    if (strlen(v) >= sizeof g_dw_sel) {
        fprintf(stderr, "#95 background TF buffer diagnostic: REFUSED - SL_BG_TF_DUMP "
                        "is %u characters, over the %u the selector allows. Nothing is "
                        "recorded; a truncated selector names a different draw.\n",
                (unsigned) strlen(v), (unsigned) (sizeof g_dw_sel - 1u));
        dump_disarm();
        return 0;
    }
    snprintf(g_dw_sel, sizeof g_dw_sel, "%s", v);
    {
        const char *p = g_dw_sel;
        if (!dump_part(&p, &g_dw_read) || !dump_part(&p, &g_dw_frame)
            || !dump_part(&p, &g_dw_cloud) || !dump_part(&p, &g_dw_mesh)
            || !dump_part(&p, &g_dw_mat) || !dump_part(&p, &dm)) {
            fprintf(stderr, "#95 background TF buffer diagnostic: REFUSED - "
                            "SL_BG_TF_DUMP=%s is not read:frame:cloud:mesh:material:"
                            "dmode[:item], where each of the six is a decimal below "
                            "4294967295 or '*' for any. Nothing is recorded: an "
                            "unreadable selector is a mistake, and recording the first "
                            "draw that happens by would be the wrong answer to it.\n",
                    g_dw_sel);
            dump_disarm();
            return 0;
        }
        /* THE DEPTH IS THE ONE FIELD WITH A CLOSED SET. SL_BGGPU_DEPTH_* is the
         * whole of it, so a number outside the three is a selector naming a
         * range that does not exist and is refused rather than never matching
         * quietly - which would look identical to an absent draw. */
        if (dm == ~0u) {
            g_dw_dmode = -2;
        } else if (dm == (unsigned) SL_BGGPU_DEPTH_SINGLE
                   || dm == (unsigned) SL_BGGPU_DEPTH_FAR0
                   || dm == (unsigned) SL_BGGPU_DEPTH_FAR1) {
            g_dw_dmode = (int) dm;
        } else {
            fprintf(stderr, "#95 background TF buffer diagnostic: REFUSED - dmode %u is "
                            "not one of %d (the one-range clamp), %d (the first range) "
                            "or %d (the game's range), and not '*'\n",
                    dm, SL_BGGPU_DEPTH_SINGLE, SL_BGGPU_DEPTH_FAR0,
                    SL_BGGPU_DEPTH_FAR1);
            dump_disarm();
            return 0;
        }
        /* The remainder, if any, is the item-path substring, bounded the same
         * way and refused rather than truncated. */
        if (*p != '\0') {
            if (strlen(p) >= sizeof g_dw_item) {
                fprintf(stderr, "#95 background TF buffer diagnostic: REFUSED - the "
                                "item component is %u characters, over the %u "
                                "allowed\n", (unsigned) strlen(p),
                        (unsigned) (sizeof g_dw_item - 1u));
                dump_disarm();
                return 0;
            }
            snprintf(g_dw_item, sizeof g_dw_item, "%s", p);
        }
    }
    if (!dump_sha256_arg(getenv("SL_BG_TF_DUMP_EXE_SHA"), g_dump_exe_sha,
                         "SL_BG_TF_DUMP_EXE_SHA (the running executable's sha256)")
        || !dump_sha256_arg(getenv("SL_BG_TF_DUMP_SRC_SHA"), g_dump_src_sha,
                            "SL_BG_TF_DUMP_SRC_SHA (the pinned source set's sha256)")) {
        dump_disarm();
        return 0;
    }
    g_dump_optin = 1;
    g_dump_armed = 1;
    fprintf(stderr, "#95 background TF buffer diagnostic ARMED by SL_BG_TF_DUMP=%s - "
                    "ONE selected nonempty submission's actual transform-feedback pool "
                    "will be read back once and kept beside the CPU clip pool and that "
                    "draw's own indices (draw-site read %u, g_dl_frame %u, cloud %u, "
                    "mesh %u, material %u, dmode %d, item '%s'; ~0 and -2 mean any). "
                    "THE CLOCKS ARE THE DRAW SITE'S, not the screenshot's: a screenshot "
                    "reading read 140 / frame 279 / cloud 139 is draw-site 139:139:139. "
                    "The readback is synchronous, so this run's frame times are not a "
                    "measurement.\n",
            g_dw_sel, g_dw_read, g_dw_frame, g_dw_cloud, g_dw_mesh, g_dw_mat,
            g_dw_dmode, g_dw_item);
    return 1;
}

/* ---- the selection ----------------------------------------------------- */

static int dump_match(unsigned want, unsigned have)
{
    return want == ~0u || want == have;
}

/* The item's path is a fixed array in the asset and is NOT assumed to be
 * NUL-terminated, so the search is bounded by the array's own size rather than
 * by a terminator the record may not carry. */
static int dump_item_match(const char *item, unsigned bytes)
{
    const size_t n = strlen(g_dw_item);
    unsigned i;
    if (item == NULL || bytes == 0u || n == 0u) return 0;
    if ((size_t) bytes < n) return 0;
    for (i = 0u; (size_t) i + n <= (size_t) bytes; i++)
        if (memcmp(item + i, g_dw_item, n) == 0) return 1;
    return 0;
}

/* One line per distinct rejection reason, so a run that captured nothing says
 * why without a line per draw. */
static void dump_say_once(int *said, const char *why)
{
    if (*said) return;
    *said = 1;
    fprintf(stderr, "#95 background TF buffer diagnostic: NO MATCH - %s. Nothing is "
                    "recorded and the attempt is NOT spent.\n", why);
}

int sl_bgtf_dump_select(const struct sl_bgtf_dump_site *s)
{
    static int said_slot, said_bounds;
    const struct tf_slot *sl;
    unsigned k1b, k2b;

    if (!sl_bgtf_dump_opt_in()) return 0;
    if (s == NULL) return 0;
    if (g_dump_done) {
        /* THE ATTEMPT IS SPENT, AND A LATER MATCHING DRAW MUST NOT CAPTURE. One
         * output set per process: a second record would be a second moment
         * reported under the first one's identity.
         *
         * NOTHING IS PRINTED HERE, and the silence is the point. The offers are
         * still counted and the census still carries late_offers, so the fact
         * remains reportable. What is suppressed is one line: the shared
         * rejection wording ends "Nothing is recorded and the attempt is NOT
         * spent", which is true of every other refusal - a no-match costs no
         * attempt - and false of this one, where later draws are declined
         * BECAUSE the attempt was spent on a record that already exists.
         *
         * Measured: a run that sealed a COMPLETE record printed its
         * PUBLISHED/COMPLETE line and then this one, and a reader gating on the
         * absence of failure words read a successful capture as a failure
         * without any saved payload being wrong. The suppression is confined to
         * the already-done case; every real refusal and error message, and the
         * strict status gate, are untouched. */
        g_dc.late_offers++;
        return 0;
    }
    g_dc.offers++;

    if (!dump_match(g_dw_read, s->read) || !dump_match(g_dw_frame, s->dl_frame)
        || !dump_match(g_dw_cloud, s->cloud)) {
        g_dc.clock_miss++;
        return 0;
    }
    if (!dump_match(g_dw_mesh, s->mesh) || !dump_match(g_dw_mat, s->material)
        || (g_dw_dmode != -2 && g_dw_dmode != s->dmode)
        || (g_dw_item[0] != '\0' && !dump_item_match(s->item, s->item_bytes))) {
        g_dc.ident_miss++;
        return 0;
    }

    /* THE SLOT MUST STILL BE THE ONE THE SELECTOR NAMES, and every term is a
     * way the pool could be the wrong one: a module that has latched, a walk
     * that has closed, an index outside the cache, a slot that was never
     * filled, a buffer name of zero, a PREVIOUS walk's generation, another
     * depth, another vertex count, or the other coefficient pair of the same
     * range. A slot that merely shares the pool with another mesh fails the
     * key, not the index, which is why the key is checked and not the index
     * alone.
     *
     * `written` IS AMONG THEM, and it is the term that cannot be inferred from
     * the others: a setup-only slot (SL_BG_TF_STATE_ONLY) holds the key exactly
     * and has no contents, so reading it back would record unwritten bytes as a
     * transform-feedback capture. The arms are mutually exclusive in the pass
     * today - a state-only launch never binds, so no draw is ever offered - and
     * this is checked here anyway, because a record of undefined storage is the
     * one failure this diagnostic must not be able to produce. */
    if (g_state <= 0 || !g_walk_live) {
        g_dc.slot_reject++;
        dump_say_once(&said_slot, "the module is not ready or no walk is live at the "
                                  "offered draw");
        return 0;
    }
    if (s->slot < 0 || s->slot >= TF_SLOTS) {
        g_dc.slot_reject++;
        dump_say_once(&said_slot, "the offered slot index is outside the slot cache");
        return 0;
    }
    sl = &g_slot[s->slot];
    k1b = (s->dmode == SL_BGGPU_DEPTH_FAR0) ? f2bits(s->k1) : f2bits(0.0f);
    k2b = (s->dmode == SL_BGGPU_DEPTH_FAR0) ? f2bits(s->k2) : f2bits(0.0f);
    if (!sl->filled || !sl->written || sl->buf == 0u
        || sl->gen != g_gen || sl->dmode != s->dmode
        || sl->nverts != s->nverts || sl->nverts != g_nverts
        || sl->k1bits != k1b || sl->k2bits != k2b || sl->cap_verts < s->nverts) {
        g_dc.slot_reject++;
        dump_say_once(&said_slot, "the offered slot does not hold the selected key - "
                                  "the walk generation, the depth mode, the coefficient "
                                  "bits, the vertex count or the pool's own storage "
                                  "disagrees, so this is a stale or another mesh's pool");
        return 0;
    }

    /* WHAT THE RECORD NEEDS, before anything is accepted. A missing CPU array
     * is the case that matters most: without it the capture could only write
     * the GPU pool and a reader would be invited to compare it against
     * nothing. */
    if (s->nverts == 0u || s->nverts > SL_BGTF_DUMP_MAX_VERTS
        || s->clip == NULL || s->zraw == NULL || s->w == NULL) {
        g_dc.bounds_reject++;
        dump_say_once(&said_bounds, "the offered draw has no CPU clip array, no raw "
                                    "depth array, no transform, or a vertex count of "
                                    "zero or over the diagnostic's maximum");
        return 0;
    }

    g_dump_sel = *s;
    g_dump_selected = 1;
    g_dc.selected++;
    return 1;
}

/* ---- the record's own little-endian stores ----------------------------- */

static void dump_put_u32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char) (v & 0xFFu);
    p[1] = (unsigned char) ((v >> 8) & 0xFFu);
    p[2] = (unsigned char) ((v >> 16) & 0xFFu);
    p[3] = (unsigned char) ((v >> 24) & 0xFFu);
}

/* A float's own 32 bits, never its value. memcpy rather than a pointer cast:
 * the cast is undefined behaviour the compiler may exploit, and these bits are
 * the only thing in the record that must survive exactly. */
static void dump_put_f32(unsigned char *p, float f)
{
    dump_put_u32(p, f2bits(f));
}

/* ---- sha256, over the bytes this record actually writes ----------------
 *
 * WHY IT IS COMPUTED HERE while the executable's and the source's digests are
 * supplied. Those two are about a build the launcher has already hashed; these
 * are about bytes that exist for the first time inside this function, so there
 * is nobody else to ask. It runs once, on a path that already contains a
 * synchronous readback, over rather less than a megabyte - and it is the only
 * thing that binds the sidecar's identity to the payload a reader opens. No
 * dependency is added: this is the published algorithm in forty lines. */
struct dump_sha {
    unsigned           h[8];
    unsigned long long len;
    unsigned char      buf[64];
    unsigned           n;
};

static const unsigned dump_sha_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static unsigned dump_ror(unsigned x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void dump_sha_block(struct dump_sha *c, const unsigned char *p)
{
    unsigned w[64], a, b, cc, d, e, f, g, h, i;
    for (i = 0u; i < 16u; i++)
        w[i] = ((unsigned) p[i * 4u] << 24) | ((unsigned) p[i * 4u + 1u] << 16)
             | ((unsigned) p[i * 4u + 2u] << 8) | (unsigned) p[i * 4u + 3u];
    for (i = 16u; i < 64u; i++) {
        const unsigned s0 = dump_ror(w[i - 15u], 7u) ^ dump_ror(w[i - 15u], 18u)
                          ^ (w[i - 15u] >> 3);
        const unsigned s1 = dump_ror(w[i - 2u], 17u) ^ dump_ror(w[i - 2u], 19u)
                          ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g  = c->h[6]; h = c->h[7];
    for (i = 0u; i < 64u; i++) {
        const unsigned S1 = dump_ror(e, 6u) ^ dump_ror(e, 11u) ^ dump_ror(e, 25u);
        const unsigned ch = (e & f) ^ ((~e) & g);
        const unsigned t1 = h + S1 + ch + dump_sha_k[i] + w[i];
        const unsigned S0 = dump_ror(a, 2u) ^ dump_ror(a, 13u) ^ dump_ror(a, 22u);
        const unsigned mj = (a & b) ^ (a & cc) ^ (b & cc);
        const unsigned t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g;  c->h[7] += h;
}

/* One buffer, hashed whole, into 64 lowercase hex characters plus a NUL. */
static void dump_sha256_hex(const unsigned char *data, size_t bytes, char *out65)
{
    static const char hex[] = "0123456789abcdef";
    struct dump_sha c;
    unsigned char tail[128];
    size_t i;
    unsigned pad, j;
    unsigned long long bits;

    c.h[0] = 0x6a09e667u; c.h[1] = 0xbb67ae85u; c.h[2] = 0x3c6ef372u;
    c.h[3] = 0xa54ff53au; c.h[4] = 0x510e527fu; c.h[5] = 0x9b05688cu;
    c.h[6] = 0x1f83d9abu; c.h[7] = 0x5be0cd19u;
    c.len = 0ull;
    c.n = 0u;

    for (i = 0u; i + 64u <= bytes; i += 64u) dump_sha_block(&c, data + i);
    c.n = (unsigned) (bytes - i);
    if (c.n != 0u) memcpy(c.buf, data + i, c.n);
    bits = (unsigned long long) bytes * 8ull;

    memset(tail, 0, sizeof tail);
    memcpy(tail, c.buf, c.n);
    tail[c.n] = 0x80u;
    pad = (c.n < 56u) ? 64u : 128u;
    for (j = 0u; j < 8u; j++)
        tail[pad - 1u - j] = (unsigned char) ((bits >> (8u * j)) & 0xFFu);
    dump_sha_block(&c, tail);
    if (pad == 128u) dump_sha_block(&c, tail + 64u);

    for (j = 0u; j < 8u; j++) {
        const unsigned v = c.h[j];
        out65[j * 8u + 0u] = hex[(v >> 28) & 0xFu];
        out65[j * 8u + 1u] = hex[(v >> 24) & 0xFu];
        out65[j * 8u + 2u] = hex[(v >> 20) & 0xFu];
        out65[j * 8u + 3u] = hex[(v >> 16) & 0xFu];
        out65[j * 8u + 4u] = hex[(v >> 12) & 0xFu];
        out65[j * 8u + 5u] = hex[(v >> 8) & 0xFu];
        out65[j * 8u + 6u] = hex[(v >> 4) & 0xFu];
        out65[j * 8u + 7u] = hex[v & 0xFu];
    }
    out65[64] = '\0';
}

static void dump_hex32(const unsigned char *raw, char *out65)
{
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out65[i * 2] = hex[(raw[i] >> 4) & 0xFu];
        out65[i * 2 + 1] = hex[raw[i] & 0xFu];
    }
    out65[64] = '\0';
}

/* ---- the sidecar, built in memory before it is written -----------------
 *
 * WHY IN MEMORY. The sidecar carries the record's own verdict, so every fact it
 * states - including whether the payload files were written whole - has to be
 * decided before the first byte of it goes out. Writing it incrementally made
 * the one field that must never be wrong depend on something that had not
 * happened yet: a close that failed after a "complete" line was already on
 * disk. Built here, the text is final and the write is just a write, with the
 * same create-only, short-write, flush and close distinctions as the payloads.
 *
 * THE OVERFLOW IS A FAILURE, NOT A TRUNCATION. A truncated JSON object is
 * unparseable, which a reader refuses - but it would be refused for the wrong
 * reason and with no words, so the overflow is recorded as its own outcome. */
struct dump_buf {
    char  *p;
    size_t cap, n;
    int    over;
};

static void dump_bp(struct dump_buf *b, const char *fmt, ...)
{
    va_list ap;
    int k;
    if (b->over || b->n >= b->cap) { b->over = 1; return; }
    va_start(ap, fmt);
    k = vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t) k >= b->cap - b->n) { b->over = 1; return; }
    b->n += (size_t) k;
}

/* A JSON string body, bounded by a byte count rather than a terminator - the
 * item's path is a fixed array and may not carry one - with the two characters
 * that would end the string early escaped and every other control byte dropped
 * rather than emitted raw. A sidecar that does not parse is a sidecar a reader
 * cannot act on, so this is not a cosmetic concern. */
static void dump_bjson(struct dump_buf *b, const char *s, unsigned bytes)
{
    unsigned i;
    if (s == NULL) return;
    for (i = 0u; i < bytes && s[i] != '\0'; i++) {
        const unsigned char c = (unsigned char) s[i];
        if (c == '"' || c == '\\') dump_bp(b, "\\%c", (int) c);
        else if (c >= 0x20u && c < 0x7Fu) dump_bp(b, "%c", (int) c);
        else dump_bp(b, "?");
    }
}

/* ---- the output files -------------------------------------------------- */

static const char *dump_dir(void)
{
    const char *d = getenv("SL_BG_TF_DUMP_DIR");
    /* The default is an ignored diagnostic directory beside the background
     * snapshots, so the output inherits the same boundary by location as well
     * as by rule: it is derived from the player's own extracted item and is
     * never ours to distribute. */
    return (d != NULL && *d != '\0') ? d : "bgtfdumps";
}

/* EXCLUSIVE CREATION, NOT A PROBE FOLLOWED BY A TRUNCATION.
 *
 * WHY THE PROBE WAS WRONG. An fopen("rb") that answers NULL was read as "the
 * path is free", and the fopen("wb") that followed then TRUNCATED whatever was
 * actually there. Two real conditions break that reasoning. An existing file
 * the process cannot open for READING - one another handle holds with read
 * sharing denied, say - fails the probe and is still writable, so the record
 * already on disk is destroyed by the very code whose job is to preserve it.
 * And a file created by anything else BETWEEN the two calls is created into a
 * window the probe has already declared empty. Neither is a hypothetical kind
 * of race: both are decided by the gap between a question and an action that
 * the file system never promised to keep closed.
 *
 * WHAT REPLACES IT. One call that both creates and claims: O_CREAT|O_EXCL,
 * which is CREATE_NEW on this platform and the same single atomic operation on
 * every other. There is no probe to race, readability is never consulted, and
 * an existing path is REFUSED BY THE CREATE ITSELF - so no byte of it is
 * touched, whoever holds it and whenever it appeared.
 *
 * The errno distinction is kept because the two outcomes are different facts: a
 * path that is already taken, and a path that could not be created for any
 * other reason (a directory that does not exist, a permission, a full disk).
 *
 * Returns NULL on success or the status words of the one thing that went wrong.
 * Whatever bytes reached the file are LEFT THERE, because a partial payload
 * with the seal withheld is evidence and a deleted one is nothing. */
static const char *dump_write_file(const char *path, const unsigned char *data,
                                   size_t bytes)
{
    FILE *f;
    int fd;

    errno = 0;
#ifdef _WIN32
    fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
               _S_IREAD | _S_IWRITE);
#else
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
#endif
    if (fd < 0)
        return (errno == EEXIST) ? "incomplete:file-exists"
                                 : "incomplete:file-create-failed";
    f = SL_BGTF_FDOPEN(fd, "wb");
    if (f == NULL) {
        /* The file now EXISTS and is empty. It is left there rather than
         * removed: create-only means a later run must refuse this path, and an
         * empty file that refuses is a truer record of what happened than a
         * path swept clean. */
        (void) SL_BGTF_CLOSE(fd);
        return "incomplete:file-create-failed";
    }
    if (bytes != 0u && fwrite(data, 1u, bytes, f) != bytes) {
        (void) fflush(f);
        (void) fclose(f);
        return "incomplete:short-write";
    }
    if (fflush(f) != 0) {
        (void) fclose(f);
        return "incomplete:flush-failed";
    }
    if (fclose(f) != 0) return "incomplete:close-failed";
    return NULL;
}

/* PUBLICATION: AN ATOMIC MOVE THAT REFUSES AN OCCUPIED DESTINATION.
 *
 * WHY THE SIDECAR NEEDS ONE AT ALL. The sidecar carries the record's own
 * verdict, and `complete` has to be decided before the first byte of it is
 * composed - so the text can say "complete" and then its own write, flush or
 * close can fail. A reader opening the file would then find a full, parseable,
 * digest-matching object claiming completeness while the runtime reported a
 * failure, and the standalone seal rule would accept it. The fix is not a
 * better message: it is that the final NAME cannot exist until the bytes behind
 * it are known to be on disk. So the sidecar is written to an exclusive
 * STAGING path, flushed and closed, and only then moved to its final name.
 *
 * NO-REPLACE, AND THAT IS THE PLATFORM'S OWN GUARANTEE RATHER THAN A CHECK
 * HERE. The C library's rename fails when the destination exists on this
 * platform, which is exactly the create-only rule; a probe-then-rename would
 * reintroduce the window this function exists to remove. Where rename replaces
 * silently, link() plus unlink() is the no-replace form: link fails with EEXIST
 * on an occupied destination and never overwrites.
 *
 * ON FAILURE THE STAGING FILE STAYS. It is the evidence of what was composed,
 * and the absence of the final name is what withholds the seal. Nothing is
 * deleted and nothing is overwritten. */
static const char *dump_publish_file(const char *staging, const char *final_name)
{
    errno = 0;
#if defined(_WIN32) || !defined(SL_BGTF_HAVE_LINK)
    if (rename(staging, final_name) != 0)
        return (errno == EEXIST || errno == EACCES)
                   ? "incomplete:sidecar-final-name-exists"
                   : "incomplete:sidecar-publish-failed";
#else
    if (link(staging, final_name) != 0)
        return (errno == EEXIST) ? "incomplete:sidecar-final-name-exists"
                                 : "incomplete:sidecar-publish-failed";
    (void) unlink(staging);
#endif
    return NULL;
}

/* ---- the capture ------------------------------------------------------- */

/* The sentinel the readback destination is filled with before the read, so a
 * region the driver did not write is VISIBLE rather than indistinguishable from
 * a legitimate zero - an all-zero destination is the single most dangerous
 * outcome here, because it compares equal to nothing and unequal to everything
 * while looking like data.
 *
 * THE TEST IS PER VERTEX, NOT PER BYTE, and the arithmetic is why. A single
 * byte of real float data equals this one about once in 256, so on a pool of
 * this size a byte-level test would fire on thousands of perfectly good bytes.
 * All SIXTEEN bytes of a vertex matching is one chance in 256^16, so a vertex
 * still holding the sentinel after the read is an unwritten vertex and not a
 * coincidence. */
#define SL_BGTF_DUMP_SENTINEL 0xA5u

static int dump_host_is_little_endian(void)
{
    const unsigned one = 1u;
    unsigned char b[4];
    memcpy(b, &one, sizeof b);
    return b[0] == 1u;
}

static void dump_run(const unsigned *ix, unsigned count)
{
    const struct sl_bgtf_dump_site *s = &g_dump_sel;
    const struct tf_slot *sl;
    unsigned char *gpu = NULL, *cpu = NULL, *idb = NULL;
    size_t clipbytes, idxbytes;
    unsigned lo = 0u, hi = 0u, i;
    unsigned z_vs_zraw_differs = 0u;
    unsigned untouched_verts = 0u, first_untouched = ~0u;
    int gpu_all_zero = 1, gpu_untouched;
    long err_pre = -1L, err_post = -1L;
    const char *wfail = NULL;
    char base[512], path[600], stage[640];
    char gsha[65], csha[65], isha[65], esha[65], ssha[65];

    /* ---- the bounds, before any allocation and before any read ---------- */
    if (ix == NULL || count == 0u) {
        dump_fail("refused:no-indices");
        return;
    }
    if (count > SL_BGTF_DUMP_MAX_INDICES) {
        dump_fail("refused:index-count-over-max");
        return;
    }
    if (s->nverts == 0u || s->nverts > SL_BGTF_DUMP_MAX_VERTS) {
        dump_fail("refused:vertex-count-over-max");
        return;
    }
    /* THE PRODUCTS, EXPRESSED ON COUNTS THAT ARE ALREADY BOUNDED: sixteen bytes
     * a vertex and four an index. The limits are on the counts so neither
     * multiply is ever performed on an unchecked value. */
    if (s->nverts > (0x7FFFFFFFu / 16u) || count > (0x7FFFFFFFu / 4u)) {
        dump_fail("refused:byte-size-overflow");
        return;
    }
    lo = hi = ix[0];
    for (i = 0u; i < count; i++) {
        if (ix[i] >= s->nverts) {
            dump_fail("refused:index-past-vertex-count");
            return;
        }
        if (ix[i] < lo) lo = ix[i];
        if (ix[i] > hi) hi = ix[i];
    }
    if (!dump_host_is_little_endian()) {
        /* The payload is DEFINED as little-endian binary32 and little-endian
         * uint32. A big-endian host would have to swap, and a diagnostic that
         * silently swapped is a diagnostic whose byte order is a guess, so it
         * refuses instead. */
        dump_fail("refused:host-not-little-endian");
        return;
    }

    /* ---- the slot, RE-CHECKED at the draw ------------------------------- */
    /* A slot can be displaced between the bind that accepted it and the run
     * that draws from it, and the record's whole value is that its bytes are
     * the bytes this draw read. */
    if (g_state <= 0 || !g_walk_live) {
        dump_fail("refused:module-not-ready");
        return;
    }
    if (s->slot < 0 || s->slot >= TF_SLOTS) {
        dump_fail("refused:slot-key-changed");
        return;
    }
    sl = &g_slot[s->slot];
    /* `written` is re-checked here with the rest of the key for the reason given
     * at the selector: a slot that holds the key with no contents behind it -
     * a setup whose vertex draw was omitted, or an allocation that was refused -
     * would otherwise be read back as a capture of unwritten storage. */
    if (!sl->filled || !sl->written || sl->buf == 0u
        || sl->gen != g_gen || sl->dmode != s->dmode
        || sl->nverts != s->nverts) {
        dump_fail("refused:slot-key-changed");
        return;
    }
    if (sl->cap_verts < s->nverts) {
        dump_fail("refused:pool-storage-short");
        return;
    }

    /* ---- the readback entry point, resolved HERE and nowhere else ------- */
    p_GetBufferSubData =
        (SL_TF_GETBUFFERSUBDATA) sl_gl_proc("glGetBufferSubData");
    if (p_GetBufferSubData == NULL) {
        dump_fail("refused:no-getbuffersubdata");
        return;
    }
    clipbytes = (size_t) s->nverts * 16u;
    idxbytes = (size_t) count * 4u;
    gpu = (unsigned char *) malloc(clipbytes);
    cpu = (unsigned char *) malloc(clipbytes);
    idb = (unsigned char *) malloc(idxbytes);
    if (gpu == NULL || cpu == NULL || idb == NULL) {
        free(gpu); free(cpu); free(idb);
        dump_fail("refused:allocation-failed");
        return;
    }

    /* ---- the pre-existing GL error, recorded and NEVER attributed ------- */
    /* Whatever is already flagged belongs to whoever raised it. It is read
     * before the readback so the value after it is about the readback alone,
     * and both are written into the record as separate fields. Reading it does
     * consume the flag, which is one more reason this path is opt-in only.
     *
     * THE CALL IS THE CORE EXPORT, DIRECTLY. glGetError is GL 1.0 core and is
     * not resolved: a resolver that answers only for extensions hands back NULL
     * on a capable driver, and the -1 that produced made the error state
     * permanently NOT OBSERVABLE - which a gate that will not accept an
     * unobservable error state refuses every time. The two readings stay
     * separate, and this is the only place either is taken. */
    err_pre = (long) glGetError();

    /* ---- ONE BOUNDED READBACK ------------------------------------------ */
    /* Placed after the prepass's glEndTransformFeedback and immediately before
     * the raster draw this run is about. Only the ACTIVE vertex count is read -
     * never sl->cap_verts, which may be a larger capacity an earlier item left
     * on the slot, and whose tail is that item's bytes.
     *
     * The generic array binding is taken and given back; the indexed
     * transform-feedback binding is not touched, and NO pointer command is
     * issued - a client pointer command captures the array buffer bound at the
     * time it runs, so the glVertexPointer the clip binding already made keeps
     * the pool as its source and the draw that follows reads exactly what it
     * would have read. No glFinish and no query: glGetBufferSubData on a buffer
     * the prepass just wrote already orders itself against that write, and that
     * is also why this is a stall and not a measurement. */
    memset(gpu, (int) SL_BGTF_DUMP_SENTINEL, clipbytes);
    p_BindBuffer(GL_ARRAY_BUFFER, sl->buf);
    p_GetBufferSubData(GL_ARRAY_BUFFER, (ptrdiff_t) 0, (ptrdiff_t) clipbytes, gpu);
    p_BindBuffer(GL_ARRAY_BUFFER, 0);
    g_dc.readbacks++;
    err_post = (long) glGetError();

    for (i = 0u; i < s->nverts; i++) {
        const unsigned char *r = gpu + (size_t) i * 16u;
        unsigned j, sent = 0u;
        for (j = 0u; j < 16u; j++) {
            if (r[j] == (unsigned char) SL_BGTF_DUMP_SENTINEL) sent++;
            if (r[j] != 0u) gpu_all_zero = 0;
        }
        if (sent == 16u) {
            untouched_verts++;
            if (first_untouched == ~0u) first_untouched = i;
        }
    }
    gpu_untouched = (untouched_verts == s->nverts);

    /* ---- the CPU reference, at THIS draw -------------------------------- */
    /* EVERY COMPONENT IS THE CONTEMPORANEOUS CLIP ARRAY'S OWN BITS, in every
     * depth mode: x = clip[0], y = clip[1], z = clip[2], w = clip[3]. The array
     * is read at the submission site, after the bypass repair and after this
     * range's own depth rewrite, so clip[2] IS the z the original path would
     * have submitted for this draw - the blanket FAR0 rewrite, bg_far_rezero's
     * adjustment for a blended mesh, SINGLE's far clamp and FAR1's write-back of
     * the unclamped value are all already in it.
     *
     * NOTHING IS SUBSTITUTED INTO THE PAYLOAD, AND THAT IS THE MEASUREMENT.
     * Writing `zraw` into the z component for FAR1 - on the reasoning that the
     * pass writes the same value back - replaced the bits the record exists to
     * carry with bits derived from a different array. When the two agree the
     * substitution is invisible; when they do NOT agree it manufactures
     * agreement with the GPU pool at exactly the vertices where the submitted
     * clip bits were the thing in question. So the payload is the clip array,
     * unconditionally.
     *
     * `zraw` SURVIVES AS A COUNT AND NOT AS BYTES. The number of vertices where
     * clip[2] and zraw disagree is recorded in the sidecar as separate evidence
     * about the two ARRAYS. It is not a component of either payload, and the
     * sidecar says so, so a reader cannot mistake one for the other. */
    for (i = 0u; i < s->nverts; i++) {
        unsigned char *r = cpu + (size_t) i * 16u;
        dump_put_f32(r + 0u, s->clip[i * 4u + 0u]);
        dump_put_f32(r + 4u, s->clip[i * 4u + 1u]);
        dump_put_f32(r + 8u, s->clip[i * 4u + 2u]);
        dump_put_f32(r + 12u, s->clip[i * 4u + 3u]);
        if (f2bits(s->clip[i * 4u + 2u]) != f2bits(s->zraw[i])) z_vs_zraw_differs++;
    }
    for (i = 0u; i < count; i++) dump_put_u32(idb + (size_t) i * 4u, ix[i]);

    /* ---- the digests, over exactly the bytes written -------------------- */
    dump_sha256_hex(gpu, clipbytes, gsha);
    dump_sha256_hex(cpu, clipbytes, csha);
    dump_sha256_hex(idb, idxbytes, isha);
    dump_hex32(g_dump_exe_sha, esha);
    dump_hex32(g_dump_src_sha, ssha);

    /* ---- the record ----------------------------------------------------- */
    snprintf(base, sizeof base, "%s/tfclip-%u-%u-%u-m%u-t%u-d%d",
             dump_dir(), s->read, s->dl_frame, s->cloud, s->mesh, s->material,
             s->dmode);
    /* The sidecar's staging name, composed here so the publication step has
     * nothing left to decide. The suffix ends in ".staging" rather than ".json"
     * so a reader globbing for records cannot pick it up. */
    snprintf(stage, sizeof stage, "%s.json.staging", base);

    snprintf(path, sizeof path, "%s.gpuclip.bin", base);
    wfail = dump_write_file(path, gpu, clipbytes);
    if (wfail == NULL) {
        snprintf(path, sizeof path, "%s.cpuclip.bin", base);
        wfail = dump_write_file(path, cpu, clipbytes);
    }
    if (wfail == NULL) {
        snprintf(path, sizeof path, "%s.idx.bin", base);
        wfail = dump_write_file(path, idb, idxbytes);
    }

    /* THE READBACK'S OWN OUTCOME IS DECIDED BEFORE THE SEAL, and the bytes are
     * written either way: a destination the driver did not touch, one that is
     * entirely zero, or a read that raised a GL error cannot be called a
     * measurement, but the bytes obtained are preserved with the seal withheld
     * rather than deleted or replaced with zeros. */
    if (err_post > 0L)            dump_fail("incomplete:readback-gl-error");
    else if (gpu_untouched)       dump_fail("incomplete:readback-wrote-nothing");
    else if (untouched_verts != 0u) dump_fail("incomplete:readback-partial");
    else if (gpu_all_zero)        dump_fail("incomplete:readback-all-zero");
    else if (wfail != NULL)       dump_fail(wfail);

    /* ---- the sidecar: decided first, then staged, then published -------- */
    {
        static char jsonbuf[16384];
        struct dump_buf b;
        const struct sl_bggpu_walk *w = s->w;
        const char *st = (g_dump_state == -1) ? g_dump_status : "complete";
        const int complete = (g_dump_state != -1);

        b.p = jsonbuf; b.cap = sizeof jsonbuf; b.n = 0u; b.over = 0;
        dump_bp(&b, "{\n");
        dump_bp(&b, "  \"schema\": \"sightline.bgtf.clipdump\",\n");
        dump_bp(&b, "  \"schema_version\": 1,\n");
        dump_bp(&b, "  \"status\": \"%s\",\n", st);
        dump_bp(&b, "  \"complete\": %s,\n", complete ? "true" : "false");
        dump_bp(&b, "  \"what\": \"one selected background submission's ACTUAL "
                    "transform-feedback clip pool, the CPU clip pool the original path "
                    "would have submitted for the same draw, and that draw's exact "
                    "index sequence. Records only: neither equality nor inequality "
                    "identifies a cause, and the synchronous readback makes this run's "
                    "frame times not a measurement.\",\n");
        dump_bp(&b, "  \"seal\": \"this record is COMPLETE only if all four files "
                    "exist AT THEIR FINAL NAMES, each payload's length and sha256 equal "
                    "the values below, this object parses whole, and complete is true. A "
                    "truncated payload fails its digest and a truncated sidecar does not "
                    "parse, so neither can be read as a smaller record.\",\n");
        /* THE PUBLICATION RULE IS PART OF THE SEAL, because without it a
         * readable sidecar could contradict a failed close. The text below is
         * composed before its own write can fail, so `complete` here is a
         * statement about the readback and the payloads - not about this file
         * reaching disk. What makes the two agree is the NAME: this object is
         * written to a staging path and moved to the final name only after its
         * write, flush and close all succeeded, so a sidecar at the final name
         * is one whose own bytes are known to be there. A file at the staging
         * name is composed evidence that was never published and is NOT a
         * record, whatever it says about itself. */
        dump_bp(&b, "  \"publication\": {\n");
        dump_bp(&b, "    \"staging_suffix\": \".json.staging\",\n");
        dump_bp(&b, "    \"rule\": \"this sidecar is written to <base>.json.staging "
                    "with exclusive creation, then flushed and closed, and only a "
                    "successful write AND flush AND close is followed by an atomic "
                    "no-replace move to <base>.json. A failure at any of those four "
                    "steps leaves the staging file in place and never creates the final "
                    "name.\",\n");
        dump_bp(&b, "    \"reader_rule\": \"a record is the sidecar at <base>.json and "
                    "nothing else. A <base>.json.staging file MUST NOT be read as a "
                    "record even though it parses, carries matching payload digests and "
                    "may say complete: true - it is exactly the failed-close case, and "
                    "its presence beside a published record would itself be a "
                    "defect.\",\n");
        dump_bp(&b, "    \"preservation\": \"failed staging files and partial payloads "
                    "are left untouched. Nothing is deleted and no existing path is "
                    "overwritten, at any step.\"\n");
        dump_bp(&b, "  },\n");
        /* THE LAYOUT, DECLARED RATHER THAN IMPLIED. A reader must not have to
         * infer an element width or a byte order from a file size. */
        dump_bp(&b, "  \"layout\": {\n");
        dump_bp(&b, "    \"clip_element\": \"binary32\",\n");
        dump_bp(&b, "    \"clip_components\": 4,\n");
        dump_bp(&b, "    \"clip_component_order\": \"x,y,z,w\",\n");
        dump_bp(&b, "    \"clip_stride_bytes\": 16,\n");
        dump_bp(&b, "    \"clip_padding_bytes\": 0,\n");
        dump_bp(&b, "    \"index_element\": \"uint32\",\n");
        dump_bp(&b, "    \"index_stride_bytes\": 4,\n");
        dump_bp(&b, "    \"endianness\": \"little\",\n");
        dump_bp(&b, "    \"model_positions\": \"NOT IN THIS RECORD - both pools are "
                    "clip space; the item's own model positions are the background "
                    "snapshot's, and mixing the two would make a comparison "
                    "meaningless\",\n");
        dump_bp(&b, "    \"vertex_order\": \"canonical: pool vertex i is item vertex i, "
                    "so the indices index both pools directly\",\n");
        dump_bp(&b, "    \"index_order\": \"the run's exact submitted order\"\n");
        dump_bp(&b, "  },\n");
        dump_bp(&b, "  \"files\": {\n");
        dump_bp(&b, "    \"gpuclip\": { \"name\": \"tfclip-%u-%u-%u-m%u-t%u-d%d"
                    ".gpuclip.bin\", \"bytes\": %lu, \"sha256\": \"%s\" },\n",
                s->read, s->dl_frame, s->cloud, s->mesh, s->material, s->dmode,
                (unsigned long) clipbytes, gsha);
        dump_bp(&b, "    \"cpuclip\": { \"name\": \"tfclip-%u-%u-%u-m%u-t%u-d%d"
                    ".cpuclip.bin\", \"bytes\": %lu, \"sha256\": \"%s\" },\n",
                s->read, s->dl_frame, s->cloud, s->mesh, s->material, s->dmode,
                (unsigned long) clipbytes, csha);
        dump_bp(&b, "    \"indices\": { \"name\": \"tfclip-%u-%u-%u-m%u-t%u-d%d"
                    ".idx.bin\", \"bytes\": %lu, \"sha256\": \"%s\" }\n",
                s->read, s->dl_frame, s->cloud, s->mesh, s->material, s->dmode,
                (unsigned long) idxbytes, isha);
        dump_bp(&b, "  },\n");
        dump_bp(&b, "  \"write_failure\": %s%s%s,\n",
                wfail != NULL ? "\"" : "", wfail != NULL ? wfail : "null",
                wfail != NULL ? "\"" : "");
        /* THE CLOCKS, ALL OF THEM, SEPARATELY, AND ALL FOUR TAKEN AT THE DRAW
         * SITE. Three of them are the gate: sl_record_index(), g_dl_frame and
         * the cloud phase. The fourth is sl_frames_completed() sampled at the
         * same site, which is the counter the screenshot line prints as
         * `frame=` - but sampled HERE, during the display-list walk, and NOT at
         * the screenshot.
         *
         * THOSE TWO SAMPLES OF THE SAME COUNTER ARE NOT ONE READING, and that
         * is why this field is named for the site it was taken at.
         * sl_frames_completed returns the mutable sl_frame
         * (src/platform/sl_ultra_shim.c:978), incremented by sl_frame_advance
         * (:3543) and by the message pump (:1497); the screenshot's own sample
         * happens later in the frame, through the pump's sl_gfx_end_frame
         * (:1432) -> sdl_end (src/gfx/sl_gfx_sdl.c:1303) -> sdl_shot (:886). So
         * the two can differ by any number of pump increments, and a gate that
         * required them equal would be requiring something nobody measured.
         *
         * WHAT IS MEASURED (r557): a screenshot reading read 140 / frame 279 /
         * cloud 139 corresponds to the DRAW-SITE tuple 139:139:139. That fixes
         * the three gate clocks. It fixes NOTHING about draw_site_pumped, which
         * the r557 snapshot did not contain: that field is recorded as a u32 and
         * is never selected on, never required to equal the screenshot's 279,
         * and never substituted by a guessed value. The screenshot's pumped
         * frame is observed and gated separately, from the screenshot evidence
         * itself. */
        dump_bp(&b, "  \"clock\": {\n");
        dump_bp(&b, "    \"draw_site_read\": %u,\n", s->read);
        dump_bp(&b, "    \"draw_site_dl_frame\": %u,\n", s->dl_frame);
        dump_bp(&b, "    \"draw_site_cloud_phase\": %u,\n", s->cloud);
        dump_bp(&b, "    \"draw_site_pumped\": %u,\n", s->pumped);
        dump_bp(&b, "    \"gate_tuple\": \"%u:%u:%u\",\n",
                s->read, s->dl_frame, s->cloud);
        dump_bp(&b, "    \"note\": \"draw_site_read is sl_record_index() AT THE DRAW "
                    "SITE and draw_site_dl_frame is g_dl_frame. draw_site_pumped is "
                    "sl_frames_completed() AT THE SAME SITE - the same counter the "
                    "screenshot line prints as frame=, but a DIFFERENT SAMPLE OF IT, "
                    "taken during the display-list walk rather than at the screenshot, "
                    "so the two are not required to be equal and this one is recorded "
                    "only. The gate is gate_tuple and nothing else: a control naming "
                    "the screenshot's read, or selecting on any pumped value, would "
                    "name a different draw.\"\n");
        dump_bp(&b, "  },\n");
        dump_bp(&b, "  \"selector\": \"");
        dump_bjson(&b, g_dw_sel, (unsigned) sizeof g_dw_sel);
        dump_bp(&b, "\",\n");
        dump_bp(&b, "  \"draw\": {\n");
        dump_bp(&b, "    \"item\": \"");
        dump_bjson(&b, s->item, s->item_bytes > 120u ? 120u : s->item_bytes);
        dump_bp(&b, "\",\n");
        dump_bp(&b, "    \"mesh\": %u,\n", s->mesh);
        dump_bp(&b, "    \"material\": %u,\n", s->material);
        dump_bp(&b, "    \"dmode\": %d,\n", s->dmode);
        dump_bp(&b, "    \"dmode_name\": \"%s\",\n",
                s->dmode == SL_BGGPU_DEPTH_SINGLE ? "SINGLE"
                : s->dmode == SL_BGGPU_DEPTH_FAR0 ? "FAR0" : "FAR1");
        /* THE PAYLOAD'S PROVENANCE, STATED COMPONENT BY COMPONENT, because the
         * one thing a reader cannot check from the bytes is where they came
         * from. Every component of cpuclip is the contemporaneous clip array's
         * own bits in every depth mode; zraw is not a component of either
         * payload and is not substituted into one. */
        dump_bp(&b, "    \"cpu_z_source\": \"clip[2] as it stands at this draw\",\n");
        dump_bp(&b, "    \"cpu_payload_provenance\": \"cpuclip is the CPU clip array "
                    "read at the submission site: x=clip[0], y=clip[1], z=clip[2], "
                    "w=clip[3], for every depth mode. No component is derived, "
                    "recomputed or taken from another array - in particular the "
                    "unclamped per-vertex depth (zraw) is NOT written into z, because "
                    "substituting it would manufacture agreement with gpuclip at "
                    "exactly the vertices where the submitted clip bits are in "
                    "question.\",\n");
        dump_bp(&b, "    \"clip_z_differs_from_zraw\": %u,\n", z_vs_zraw_differs);
        dump_bp(&b, "    \"clip_z_vs_zraw_note\": \"a count over the two ARRAYS and not "
                    "a property of either payload: the vertices where clip[2] and the "
                    "pass's unclamped depth hold different bits. For FAR1 the pass "
                    "writes the unclamped value back into clip[2] before the range "
                    "draws, so a nonzero count there is staleness worth seeing; for "
                    "SINGLE and FAR0 the two are expected to differ because the range "
                    "rewrote clip[2]. Either way the payload carries clip[2].\",\n");
        dump_bp(&b, "    \"vertices\": %u,\n", s->nverts);
        dump_bp(&b, "    \"indices\": %u,\n", count);
        dump_bp(&b, "    \"index_min\": %u,\n", lo);
        dump_bp(&b, "    \"index_max\": %u,\n", hi);
        dump_bp(&b, "    \"triangles\": %u\n", count / 3u);
        dump_bp(&b, "  },\n");
        dump_bp(&b, "  \"pool\": {\n");
        dump_bp(&b, "    \"slot\": %d,\n", s->slot);
        dump_bp(&b, "    \"buffer_name\": %lu,\n", (unsigned long) sl->buf);
        dump_bp(&b, "    \"walk_generation\": %u,\n", sl->gen);
        dump_bp(&b, "    \"slot_dmode\": %d,\n", sl->dmode);
        dump_bp(&b, "    \"slot_k1_bits\": \"0x%08x\",\n", sl->k1bits);
        dump_bp(&b, "    \"slot_k2_bits\": \"0x%08x\",\n", sl->k2bits);
        dump_bp(&b, "    \"slot_nverts\": %u,\n", sl->nverts);
        dump_bp(&b, "    \"slot_capacity_verts\": %u,\n", sl->cap_verts);
        dump_bp(&b, "    \"bytes_read\": %lu,\n", (unsigned long) clipbytes);
        dump_bp(&b, "    \"read_note\": \"only the ACTIVE vertex count was read; "
                    "slot_capacity_verts may be larger and its tail belongs to an "
                    "earlier item\",\n");
        dump_bp(&b, "    \"sentinel_byte\": \"0x%02x\",\n",
                (unsigned) SL_BGTF_DUMP_SENTINEL);
        dump_bp(&b, "    \"untouched_vertices\": %u,\n", untouched_verts);
        dump_bp(&b, "    \"first_untouched_vertex\": %ld,\n",
                first_untouched == ~0u ? -1L : (long) first_untouched);
        dump_bp(&b, "    \"untouched_note\": \"a vertex whose whole 16 bytes still hold "
                    "the sentinel was NOT written by the readback; any non-zero count "
                    "withholds the seal\",\n");
        dump_bp(&b, "    \"readback_wrote_nothing\": %s,\n",
                gpu_untouched ? "true" : "false");
        dump_bp(&b, "    \"gpu_all_zero\": %s\n", gpu_all_zero ? "true" : "false");
        dump_bp(&b, "  },\n");
        /* THE DRAW'S TRANSFORM AND DEPTH BITS, as 32-bit patterns: a decimal
         * rendering would round the one thing a reader must be able to compare
         * exactly. */
        dump_bp(&b, "  \"transform_bits\": {\n");
        dump_bp(&b, "    \"sc\": \"0x%08x\",\n", f2bits(w->sc));
        dump_bp(&b, "    \"post\": \"0x%08x\",\n", f2bits(w->post));
        dump_bp(&b, "    \"org\": [\"0x%08x\", \"0x%08x\", \"0x%08x\"],\n",
                f2bits(w->org[0]), f2bits(w->org[1]), f2bits(w->org[2]));
        dump_bp(&b, "    \"fogless\": %d,\n", w->fogless ? 1 : 0);
        dump_bp(&b, "    \"k1\": \"0x%08x\",\n", f2bits(s->k1));
        dump_bp(&b, "    \"k2\": \"0x%08x\",\n", f2bits(s->k2));
        dump_bp(&b, "    \"C\": [");
        for (i = 0u; i < 16u; i++)
            dump_bp(&b, "%s\"0x%08x\"", i != 0u ? ", " : "", f2bits(w->C[i]));
        dump_bp(&b, "]\n");
        dump_bp(&b, "  },\n");
        dump_bp(&b, "  \"gl\": {\n");
        dump_bp(&b, "    \"error_before_readback\": %ld,\n", err_pre);
        dump_bp(&b, "    \"error_after_readback\": %ld,\n", err_post);
        dump_bp(&b, "    \"error_source\": \"glGetError, called DIRECTLY as the GL 1.0 "
                    "core export and only on this armed capture path - not resolved "
                    "through the extension loader, which answers NULL for core "
                    "functions on some drivers\",\n");
        dump_bp(&b, "    \"error_note\": \"two separate readings, taken immediately "
                    "before and immediately after the one readback. A non-zero "
                    "error_before_readback belongs to whoever raised it and is NEVER "
                    "attributed to this readback; only error_after_readback withholds "
                    "the seal. -1 would mean the error state was NOT OBSERVABLE, which "
                    "is not success and is not 'no error' - the direct core call does "
                    "not produce it, so a -1 in either field is a defect rather than a "
                    "clean reading.\",\n");
        dump_bp(&b, "    \"readbacks\": %lu,\n", g_dc.readbacks);
        dump_bp(&b, "    \"glfinish\": false,\n");
        dump_bp(&b, "    \"queries\": false\n");
        dump_bp(&b, "  },\n");
        dump_bp(&b, "  \"build\": {\n");
        dump_bp(&b, "    \"exe_sha256\": \"%s\",\n", esha);
        dump_bp(&b, "    \"src_sha256\": \"%s\"\n", ssha);
        dump_bp(&b, "  },\n");
        dump_bp(&b, "  \"compare\": {\n");
        dump_bp(&b, "    \"rule\": \"compare all four uint32 component patterns of "
                    "gpuclip against cpuclip, per index, in the exact order the index "
                    "file gives. No tolerance, no normalisation, no padding: a "
                    "difference of one bit is a difference, and +0.0 and -0.0 are "
                    "different patterns.\",\n");
        dump_bp(&b, "    \"offset_rule\": \"index i names byte offset i*16 in both clip "
                    "files; component c of that vertex is the little-endian uint32 at "
                    "i*16 + c*4\",\n");
        dump_bp(&b, "    \"called_out_vertices\": [5060, 5074, 5058],\n");
        dump_bp(&b, "    \"called_out_triangle\": 1320\n");
        dump_bp(&b, "  }\n");
        dump_bp(&b, "}\n");

        if (b.over) {
            dump_fail("incomplete:sidecar-overflow");
        } else {
            /* STAGE, THEN PUBLISH, AND NOTHING BETWEEN THEM. The staging write
             * carries the whole short-write / flush / close distinction; only
             * when all three succeeded does the final name come into existence,
             * and the move refuses an occupied destination without touching it.
             * A failure at any step leaves the staging file where it is. */
            const char *sfail =
                dump_write_file(stage, (const unsigned char *) jsonbuf, b.n);
            if (sfail == NULL) {
                snprintf(path, sizeof path, "%s.json", base);
                sfail = dump_publish_file(stage, path);
            }
            if (sfail != NULL) dump_fail(sfail);
        }
    }

    if (g_dump_state == 0) {
        g_dump_state = 1;
        g_dc.completed++;
        snprintf(g_dump_status, sizeof g_dump_status, "%s", "complete");
        /* THE BANNER IS PRINTED ONLY HERE, and here is AFTER the sidecar was
         * written, flushed, closed and moved to its final name - so the words
         * COMPLETE and the existence of %s.json are the same fact. Any failure
         * in that sequence has already latched the refusal through dump_fail,
         * which leaves g_dump_state at -1 and this branch untaken. */
        fprintf(stderr, "#95 background TF buffer diagnostic: wrote %s.{gpuclip,cpuclip,"
                        "idx}.bin and PUBLISHED %s.json - COMPLETE. %u vertices (%lu "
                        "bytes a pool), %u indices, mesh %u material %u dmode %d, "
                        "draw-site clock %u:%u:%u (draw-site pumped %u, recorded and "
                        "not selected on), slot %d buffer %lu generation %u. One "
                        "readback, one attempt, create-only, staged then published. "
                        "This is a record and not a verdict.\n",
                base, base, s->nverts, (unsigned long) clipbytes, count, s->mesh,
                s->material, s->dmode, s->read, s->dl_frame, s->cloud, s->pumped,
                s->slot, (unsigned long) sl->buf, sl->gen);
    }

    free(gpu); free(cpu); free(idb);
}

void sl_bgtf_dump_capture(const unsigned *indices, unsigned count)
{
    if (!g_dump_armed || g_dump_done || !g_dump_selected) return;
    /* THE ATTEMPT IS CLAIMED HERE, and here is BEFORE anything is resolved,
     * allocated, read back or opened. Spending it first is what makes a failure
     * final: a diagnostic that claimed its attempt only on success would retry
     * at the next matching draw, the next frame or another slot, and the record
     * it eventually wrote would carry an identity it had not measured. */
    g_dump_done = 1;
    g_dump_selected = 0;
    g_dc.captures++;
    dump_run(indices, count);
    memset(&g_dump_sel, 0, sizeof g_dump_sel);
}

#endif /* !__sgi */
