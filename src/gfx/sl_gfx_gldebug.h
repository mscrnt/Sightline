/* ---- THE OPT-IN GL DEBUG-OUTPUT DIAGNOSTIC (SL_GL_DEBUG_DIAG) -----------
 *
 * WHAT THIS IS. KHR_debug / GL 4.3 debug output, armed only by an exact
 * SL_GL_DEBUG_DIAG=1 at launch. When it is armed the backend asks SDL for a
 * debug context before the window exists, and after the context is current this
 * module VERIFIES the granted flag, resolves glDebugMessageCallback, turns
 * GL_DEBUG_OUTPUT_SYNCHRONOUS and GL_DEBUG_OUTPUT on AND PROVES EACH TOOK
 * EFFECT, and only then binds a bounded stderr reporter. Any step that cannot
 * be verified refuses, undoes whatever it had changed, and the launch renders
 * exactly as it would have.
 *
 * EVERY GATE IS A READBACK, NEVER A RETURN CODE AND NEVER A REQUEST. Three
 * separate things could each be mistaken for proof, and none of them is:
 *   - that the debug flag was REQUESTED, or that SDL's attribute echoes the
 *     request back. SDL stores what was asked for, so its answer is ADVISORY
 *     ONLY: the grant is the CONTEXT's own GL_CONTEXT_FLAGS and nothing else.
 *     An unreadable GL query refuses, whatever SDL says.
 *   - that SDL_GL_SetAttribute returned success. A request that could not be
 *     made is not a request; arming refuses.
 *   - that glEnable returned (it returns nothing). Each capability is proved
 *     on by glGetError plus glIsEnabled before anything is bound.
 *
 * WHAT IT IS NOT EVIDENCE OF. Requesting a debug context CHANGES THE SUBJECT:
 * a driver may validate more, synchronise more, take different code paths and
 * allocate differently under one, so a run with the selector armed is not the
 * run a player gets. An absent crash under this selector therefore does not
 * show the default launch is sound, and a clean message log does not show the
 * defect lies in the driver. The only thing a message proves is that the
 * implementation reported that message on this run; silence proves nothing in
 * either direction. Debug output also only speaks when the implementation
 * generates something to say - a well-formed stream that produces a wrong
 * PICTURE is not an error, and nothing is logged for it
 * (https://wikis.khronos.org/opengl/Debug_Output).
 *
 * WHY THE LOGIC LIVES IN ITS OWN TRANSLATION UNIT. Every decision here -
 * whether the selector armed, whether the flag the driver GRANTED carries the
 * debug bit, whether the entry point resolved, what the callback prints and in
 * what order the teardown runs - is reachable on a CPU with no window, no
 * context and no driver, PROVIDED the platform calls arrive through pointers a
 * caller supplies. That is the whole shape of this interface: the module calls
 * no SDL and no GL function directly, src/gfx/sl_gfx_sdl.c supplies the real
 * ones, and tools/profiling/native/test_sl_gfx_gldebug.c supplies recording
 * mocks that can also fail on command. A mock that records EVERY platform call
 * is what makes "the callback calls no GL and no SDL" a measurement rather than
 * a promise.
 *
 * THE CALLING CONVENTION IS LOAD-BEARING. On Win32 a GL entry point and every
 * callback GL invokes are __stdcall (APIENTRY); a __cdecl callback handed to
 * glDebugMessageCallback corrupts the stack on return. The callback below is
 * declared with SL_GLDEBUG_APIENTRY and assigned into the PFN type WITHOUT A
 * CAST, so a convention or signature mismatch is a compile error rather than a
 * runtime corruption; the fixture repeats that assignment against Khronos's own
 * GLDEBUGPROC and PFNGLDEBUGMESSAGECALLBACKPROC from <GL/glext.h>.
 */
#ifndef SL_GFX_GLDEBUG_H
#define SL_GFX_GLDEBUG_H

/* APIENTRY, without dragging <GL/gl.h> (and <windows.h>) into every caller.
 * The GL headers this tree compiles against define APIENTRY as windows.h's
 * __stdcall, and <GL/glext.h>:2169 declares GLDEBUGPROC with it; when a caller
 * has already included a GL header the macro below resolves to that same
 * APIENTRY, and when nothing has, __stdcall is named directly. Both spell the
 * same convention, which is why the fixture's no-cast assignment against the
 * real GLDEBUGPROC is the check that they agree. */
#if defined(APIENTRY)
#  define SL_GLDEBUG_APIENTRY APIENTRY
#elif defined(_WIN32)
#  define SL_GLDEBUG_APIENTRY __stdcall
#else
#  define SL_GLDEBUG_APIENTRY
#endif

/* The callback and the entry point that installs it, spelled in plain C types
 * that are what the GL types ARE in this toolchain - GLenum/GLuint are
 * `unsigned int`, GLsizei is `int`, GLchar is `char`
 * (mingw32/include/GL/gl.h:22-31, GL/glext.h:565). Written out rather than
 * included so this header stays free of platform headers; proved equal to
 * Khronos's own typedefs by the fixture's no-cast assignments, which is the
 * only way a transcription like this can be kept honest. */
typedef void (SL_GLDEBUG_APIENTRY *sl_gldebug_proc_t)(unsigned int source,
                                                      unsigned int type,
                                                      unsigned int id,
                                                      unsigned int severity,
                                                      int length,
                                                      const char *message,
                                                      const void *user);
typedef void (SL_GLDEBUG_APIENTRY *sl_gldebug_setter_t)(sl_gldebug_proc_t cb,
                                                        const void *user);

/* ---- THE SELECTOR'S GRAMMAR ---------------------------------------------
 *
 * EXACT, and deliberately narrower than the value this module would have to
 * guess about. "1" arms it. "0" and an empty value are an explicit off, which
 * is indistinguishable in effect from the variable being absent and so says
 * nothing. EVERYTHING ELSE IS MALFORMED AND REFUSES: "2", "01", "1 ", " 1",
 * "1x", "true", "yes", "on", "ON". Refusing is what keeps a mistyped value from
 * arming a diagnostic that changes the driver's behaviour, and keeps the
 * opposite mistake - a value the author believed meant "off" silently arming -
 * impossible in both directions.
 *
 * THE VALUE IS NEVER PRINTED. The verdict is named; the text that produced it
 * is not. Environment values are not this module's to echo, and "malformed" is
 * the whole of what a reader needs to correct the launch. This differs on
 * purpose from the older tf_switch_1 helper in src/gfx/sl_gfx_bgtf.c, which
 * quotes the value it rejected. */
enum {
    SL_GLDEBUG_SEL_ABSENT    = 0,  /* unset: nothing is printed and nothing armed */
    SL_GLDEBUG_SEL_OFF       = 1,  /* "0" or "": an explicit off, equally silent   */
    SL_GLDEBUG_SEL_ARMED     = 2,  /* exactly "1"                                  */
    SL_GLDEBUG_SEL_MALFORMED = 3   /* anything else: refused, never armed          */
};

/* Pure: no I/O, no state, no platform call. `raw` is a getenv result, NULL
 * included. */
int sl_gldebug_parse(const char *raw);

/* ---- THE PLATFORM, INJECTED --------------------------------------------
 *
 * Every member is a call this module would otherwise make itself. A NULL member
 * is a platform that cannot do that thing, and each path below says what it
 * does then; nothing is assumed to succeed and no return code is trusted where
 * a readback can be had instead.
 *
 *   sdl_get_ctx_flags  SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &v): 0 on
 *                      success. Read BEFORE the request so the debug bit is
 *                      OR-ed into whatever else a launch asked for rather than
 *                      replacing it. A read that FAILS refuses the whole
 *                      request: there is no zero fallback, because assuming 0
 *                      would CLEAR flags this module cannot see. Read again
 *                      after creation for the advisory note only - never as
 *                      proof of a grant.
 *   sdl_set_ctx_flags  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, v): 0 on
 *                      success. Requested before SDL_CreateWindow, which is
 *                      the only point at which it has any effect
 *                      (https://wiki.libsdl.org/SDL2/SDL_GL_SetAttribute). A
 *                      failure refuses arming, and the attribute is read back
 *                      to see whether the failed set stored anyway.
 *   gl_get_ctx_flags   glGetIntegerv(GL_CONTEXT_FLAGS) with its GL error
 *                      checked: 0 when the value was READ, non-zero when the
 *                      implementation refused the query (a pre-3.0 context
 *                      raises GL_INVALID_ENUM and writes nothing). THE ONLY
 *                      SOURCE OF A GRANT. Unreadable means REFUSED.
 *   gl_get_proc        SDL_GL_GetProcAddress. Windows resolves GL entry points
 *                      per context, so this is called only with the context
 *                      current and its answer is never cached across contexts
 *                      (https://wiki.libsdl.org/SDL2/SDL_GL_GetProcAddress).
 *   gl_enable_verified glEnable(cap), then PROVE it: 0 only when glEnable
 *                      raised no GL error AND glIsEnabled(cap) reports
 *                      GL_TRUE. The distinct non-zero codes below let the
 *                      refusal name which half failed, which is the difference
 *                      between "this driver has no debug output" and "this
 *                      driver accepted the call and did nothing".
 *   gl_is_enabled      glIsEnabled(cap) alone: 0 when the answer was
 *                      DETERMINED (written through `on`), non-zero when the
 *                      query itself could not be trusted. Used to re-prove
 *                      both capabilities after the callback is bound.
 *   gl_disable         glDisable(cap). Disabling a capability that is already
 *                      off is harmless, which is why the fail-closed path
 *                      disables both without tracking which took.
 *   say                ONE line of diagnostic text, newline supplied by the
 *                      sink. The only member the callback is allowed to use.
 */
enum {
    SL_GLDEBUG_EN_OK      = 0,  /* no GL error and glIsEnabled says GL_TRUE  */
    SL_GLDEBUG_EN_ERROR   = 1,  /* the glEnable itself raised a GL error     */
    SL_GLDEBUG_EN_OFF     = 2,  /* no error, but the capability reads OFF    */
    SL_GLDEBUG_EN_UNKNOWN = 3   /* glIsEnabled could not be trusted          */
};

struct sl_gldebug_host {
    int   (*sdl_get_ctx_flags)(int *flags);
    int   (*sdl_set_ctx_flags)(int flags);
    int   (*gl_get_ctx_flags)(unsigned int *bits);
    void *(*gl_get_proc)(const char *name);
    int   (*gl_enable_verified)(unsigned int cap);
    int   (*gl_is_enabled)(unsigned int cap, int *on);
    void  (*gl_disable)(unsigned int cap);
    void  (*say)(const char *line);
};

/* BEFORE SDL_CreateWindow. Parses the selector, reports a malformed value,
 * and on an exact "1" OR-s the debug context flag into the existing requested
 * flags and says REQUESTED. Returns non-zero when the request was MADE AND
 * REPORTED SUCCESSFUL - which is still NOT a claim that it was granted; that is
 * sl_gldebug_install's to establish.
 *
 * REFUSES, and says so, without arming when: the existing requested flags
 * cannot be read (no set call is made at all, so nothing this module cannot see
 * can be cleared), or the set itself fails. A failed set is followed by a
 * readback, because SDL stores the attribute in process-wide state and a set
 * that reported failure may still have stored (SDL2 release-2.32.10,
 * src/video/SDL_video.c); if the bit is there the original value is put back
 * and the outcome - restored, or NOT restorable - is reported, since a launch
 * that silently kept a debug context nobody is reporting on would be a defect
 * this diagnostic INTRODUCED.
 *
 * With the selector absent or explicitly off this makes NO platform call and
 * prints nothing, so a launch that does not ask for the diagnostic has the
 * context attributes, the log and the behaviour it had before this module
 * existed. */
int sl_gldebug_arm(const char *raw, const struct sl_gldebug_host *h);

/* AFTER the context is created AND current. In order: proves the GRANTED debug
 * flag from the context's own GL_CONTEXT_FLAGS, resolves
 * glDebugMessageCallback, enables and PROVES GL_DEBUG_OUTPUT_SYNCHRONOUS, then
 * GL_DEBUG_OUTPUT, then binds the reporter, then re-proves both capabilities.
 * Returns non-zero only when every one of those succeeded - the ACTIVE case.
 *
 * WHY THE ENABLES COME BEFORE THE BIND. A callback bound while
 * GL_DEBUG_OUTPUT_SYNCHRONOUS is unproved could be invoked from a driver
 * thread, and this module's message counter and sink pointer are plain statics
 * written on the assumption of synchronous delivery
 * (https://wikis.khronos.org/opengl/Debug_Output). Enabling and proving first
 * means NO INSTANT EXISTS in which the callback is bound while the synchronous
 * guarantee is unverified - the property holds by construction rather than by
 * timing. The cost is that between the OUTPUT enable and the bind the driver
 * may log a message to its own internal queue, which nothing reads; that is
 * microseconds of a driver's own bookkeeping, against an unsynchronised call
 * into this module.
 *
 * Any failure is REFUSED: the callback is cleared FIRST, then both
 * capabilities are disabled, the reason is reported, module state ends
 * INACTIVE so a later shutdown is a no-op and a late callback prints nothing.
 * A no-op returning 0 when sl_gldebug_arm did not arm. */
int sl_gldebug_install(const struct sl_gldebug_host *h);

/* BEFORE the context is destroyed, while it is STILL CURRENT. Clears the
 * callback first and only then disables debug output, so no window exists in
 * which the implementation holds a pointer into a module that has stopped
 * expecting calls. A no-op unless sl_gldebug_install reported ACTIVE. */
void sl_gldebug_shutdown(const struct sl_gldebug_host *h);

/* ---- THE MESSAGE FORMATTER, pure ---------------------------------------
 *
 * Writes one bounded line into `out` and returns its length. `out` is ALWAYS
 * NUL-terminated and never longer than outsz-1; a buffer too small for the
 * whole line is filled and the line is truncated, which the return value
 * reports. 0 means nothing was written: `out` NULL, or outsz <= 0. Source, type
 * and severity are named where this module knows the token and printed as four
 * hex digits where it does not, so an unknown enum is reported rather than
 * hidden. `id` is the implementation's own, printed unsigned.
 *
 * THE TEXT IS BOUNDED TWICE, because neither bound can be trusted alone. GL
 * passes `length` as the message length, or a negative value meaning the
 * message is NUL-terminated; a driver has also been known to include the
 * terminator in `length`. So the copy stops at the FIRST of: `length` when it
 * is non-negative, the first NUL, SL_GLDEBUG_TEXT_MAX, and the room left in
 * `out`. A NULL message is reported as such rather than dereferenced. No
 * allocation: callers pass a buffer of SL_GLDEBUG_LINE_MAX. */
#define SL_GLDEBUG_TEXT_MAX 256
#define SL_GLDEBUG_LINE_MAX 512

int sl_gldebug_format(char *out, int outsz, unsigned int source,
                      unsigned int type, unsigned int id,
                      unsigned int severity, int length, const char *message);

/* How many messages one PROCESS prints before the callback goes quiet. A driver
 * under a synchronous debug context can report per draw call, so an uncapped
 * reporter turns a frame into megabytes of stderr and hides the first message -
 * the one that matters - behind the thousandth. One line records the cap having
 * been reached, and is printed exactly once. */
#define SL_GLDEBUG_MSG_CAP 64

#endif /* SL_GFX_GLDEBUG_H */
