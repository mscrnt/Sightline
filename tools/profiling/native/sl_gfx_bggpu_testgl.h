/**
 * SYNTHETIC GL for the background pass's GPU vertex transform, offline.
 *
 * EVERY VALUE THIS HEADER'S IMPLEMENTATION PRODUCES IS FABRICATED. The program
 * and shader names, the compile and link statuses, the info logs and the
 * uniform locations are invented here and prove nothing about any driver. What
 * they do prove is the module's own logic: when it resolves entry points, when
 * it compiles, what it latches, what it uploads, in which order, and what it
 * refuses to do.
 *
 * There is NO window, NO native graphics API and NO game here. Plain C with no
 * link-time dependency on opengl32 or SDL, so a test built with it cannot reach
 * a GPU even by accident.
 *
 * HOW THIS SHIM GETS IN FRONT OF THE REAL GL HEADERS, and why that way.
 * src/gfx/sl_gfx_bggpu.c has NO test hook in its include block: it includes
 * <windows.h> under _WIN32 and then <GL/gl.h>, unconditionally, with no
 * SL_*_TEST macro around either (unlike src/gfx/sl_gfx_skybatch.c, which can
 * swap its own include). src/ may not be edited to add one. So this header is
 * injected ahead of the translation unit with `-include` and DEFINES THE HOST
 * HEADERS' OWN INCLUDE GUARDS, which makes the real headers expand to nothing
 * when the module asks for them:
 *
 *   _WINDOWS_, _INC_WINDOWS   mingw32/include/windows.h, lines 6-7 and 22-23,
 *                             the outer guard wrapping the whole file
 *   __gl_h_, __GL_H__         mingw32/include/GL/gl.h, lines 6-10, the two
 *                             nested guards wrapping the whole file
 *
 * Those four names were read out of the installed headers, not assumed. The
 * alternative - a directory on the include path holding stub windows.h and
 * GL/gl.h files - needs two more files on disk for the same effect; this way
 * the whole shim is one header and one implementation, which is the shape
 * sl_gfx_profile_testgl.h already has. The failure mode if a toolchain ever
 * uses different guard names is LOUD, not silent: the real headers expand,
 * their GL typedefs and tokens collide with the ones below, and the build
 * stops.
 *
 * APIENTRY IS EMPTY HERE, so the stub and the module's typedefs are both cdecl
 * and agree. The calling-convention fact the module's own comment records (the
 * entry points are __stdcall on Windows, and a typedef without APIENTRY
 * corrupts the caller's frame) is therefore NOT exercised by this fixture; it
 * needs a real opengl32. sl_gfx_profile_testgl.h makes the same trade for the
 * same reason.
 *
 * THE STUB IS ALSO A WATCHDOG. It counts every entry point call, records which
 * uniform location received which value in order, and records a VIOLATION if a
 * name from a previous fake context is used after the module was told the
 * context changed, if any call arrives after the test marks the context
 * destroyed, or if a shader's source is set on a name that was never created.
 * A test asserts those counters are zero; that is how "re-resolves against the
 * current context" is checked rather than asserted.
 *
 * WHAT IS ABSENT IS PART OF THE CHECK. There is no glFinish, no glFlush, no
 * glGetError, no buffer-map and no pixel-read entry point in this stub, and no
 * glDeleteShader or glDeleteProgram. If the module called one, the test
 * executable would fail to LINK - absence of the symbol is a stronger proof
 * than any counter, because it cannot be satisfied by a call that happens not
 * to run on the path a test drives.
 */
#ifndef SL_GFX_BGGPU_TESTGL_H
#define SL_GFX_BGGPU_TESTGL_H

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

/* ---- the GL types and tokens the module uses ---------------------------- */

typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;
typedef float         GLfloat;

#define APIENTRY                       /* the stub is cdecl throughout */
#define WINGDIAPI                      /* taken from windows.h by the real gl.h */

/* The registry's values, the same three the module falls back to when its
 * GL/gl.h does not have them (src/gfx/sl_gfx_bggpu.c, the #ifndef block). */
#define GL_VERTEX_SHADER  0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS    0x8B82

/* ---- what the module takes from its host -------------------------------- */

void *sl_gl_proc(const char *name);

/* ---- the recorded uniform writes ---------------------------------------- */

#define BGT_UMAX 256

enum { BGT_U1I = 1, BGT_U1F = 2, BGT_U3F = 3, BGT_U1FV = 4 };

/* One uniform write GL ACCEPTED, in call order. A write with no program
 * current, or to location -1, is not here: GL rejects both, and the stub
 * counts them separately (uniform_rejected) so a test can tell "the module
 * did not call" from "GL did not write". */
struct bgt_uwrite {
    int     kind;                  /* BGT_U1I / _U1F / _U3F / _U1FV        */
    GLint   loc;
    GLuint  prog;                  /* the program current when it landed   */
    GLint   ival;                  /* BGT_U1I                              */
    GLsizei count;                 /* BGT_U1FV                             */
    float   fv[16];                /* BGT_U1F / _U3F / _U1FV               */
};

/* ---- the test's control surface (all SYNTHETIC) ------------------------- */

struct sl_bggpu_testgl {
    /* what the fake driver refuses */
    const char *withhold_proc;     /* this entry point does not resolve     */
    const char *withhold_uniform;  /* this uniform's location is -1         */
    int create_shader_zero;        /* glCreateShader hands back 0           */
    int create_program_zero;       /* glCreateProgram hands back 0          */
    int fail_compile;              /* GL_COMPILE_STATUS reads 0             */
    int fail_link;                 /* GL_LINK_STATUS reads 0                */

    /* the fake context */
    int context;                   /* its generation; bgt_new_context bumps */
    int context_destroyed;         /* set by the test AFTER teardown        */

    /* what the stub observed */
    long calls;                    /* EVERY entry point call                */
    long proc_calls;
    long create_shader, shader_source, compile, get_shaderiv, get_shaderlog;
    long create_program, attach, link, get_programiv, get_programlog;
    long use_program, get_uniform_location;
    long uniform_calls;            /* every glUniform*, accepted or not     */
    long uniform_rejected;         /* ... that GL wrote nothing for         */
    GLuint current;                /* the program in use; 0 = fixed pipeline*/
    GLuint last_shader, last_program;
    const char *last_source;       /* the text handed to glShaderSource     */
    GLsizei last_source_count;

    /* violations - a test asserts each is zero */
    long v_stale_name;             /* a name from an earlier generation     */
    long v_call_after_context_destroyed;
    long v_source_without_shader;

    /* the accepted writes, in order */
    int nu;
    struct bgt_uwrite u[BGT_UMAX];
};

extern struct sl_bggpu_testgl bg;

void  bgt_reset(void);         /* a clean fake driver, generation 1, nothing resolved */
void  bgt_new_context(void);   /* the next generation: old names become stale        */
GLint bgt_loc(const char *name); /* the location this stub gives that uniform, or -1 */

/* The uniform names the module is expected to resolve, in the order it asks
 * for them. The test walks this to prove EVERY one resolved. */
extern const char *const bgt_uniform_names[8];

#endif /* SL_GFX_BGGPU_TESTGL_H */
