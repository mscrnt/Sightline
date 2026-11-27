/**
 * THE BACKGROUND PASS'S RENDERER-STATE PROBE (SL_BG_GLSTATE), DEFAULT OFF.
 *
 * See sl_gfx_bgstate.h for what this records, what follows from none of it, and
 * the list of things an off launch does not do. This file holds the switch, the
 * selector, the four phase snapshots, the producer ledger, the bounded query
 * table and the create-only record.
 *
 * THE ONLY GL THIS MODULE CALLS is glGetIntegerv, glGetFloatv, glGetPointerv,
 * glIsEnabled and glGetString, plus glActiveTexture and glClientActiveTexture
 * through the backend's resolver. There is no draw, no flush, no finish, no
 * fence, no query object, no buffer map and no buffer, pixel or framebuffer
 * read in it at all, and no enable, pointer, matrix, binding or program is ever
 * written. The two texture selectors are the ONE kind of state this module
 * moves, and only inside a traversal that restores the inherited value exactly.
 *
 * APIENTRY IS LOAD-BEARING ON THE TWO RESOLVED ENTRY POINTS. They are __stdcall
 * on Windows and the CALLEE pops the arguments, so a typedef without APIENTRY
 * corrupts the caller's frame - the same measurement sl_gfx_bgtf.c records for
 * its own resolved typedefs. On Linux APIENTRY is empty and the two agree.
 *
 * THE EXCLUSIVE-CREATION AND ATOMIC-PUBLICATION PRIMITIVES ARE THE SAME SHAPE
 * AS THE BUFFER DIAGNOSTIC'S (sl_gfx_bgtf.c) and are restated here rather than
 * shared, for a reason that is about blast radius rather than tidiness: those
 * are file-static inside a module whose behaviour this round must not touch, and
 * exporting them would put this probe's needs into that diagnostic's surface.
 * The semantics are reproduced exactly - one O_CREAT|O_EXCL operation that both
 * creates and claims, no probe to race, an existing path refused untouched
 * however it came to be there, partial bytes left where they are, and a seal
 * that is a publication rather than a sentence in a file - and the reasoning for
 * each is recorded at the function it belongs to.
 *
 * THE STATIC COST IS KNOWN AND BOUNDED. The phase snapshots are a fixed array
 * of BGSTATE_SNAPS records of BGSTATE_ROWS rows, which is a few hundred
 * kilobytes of zero-filled BSS present whether or not the probe is armed. That
 * is the price of a diagnostic that allocates NOTHING on an off launch and
 * nothing inside a frame, and it is paid in address space rather than in work.
 */
#ifndef __sgi

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* THE EXCLUSIVE-CREATION PRIMITIVE, AND WHY IT IS NOT fopen: the portable fopen
 * write modes all truncate, and C11's "x" is a mode character a runtime that
 * ignores it turns silently back into a truncating open. These are C-runtime
 * headers rather than platform-GUI ones, so an offline fixture that suppresses
 * <windows.h> for this translation unit still gets all three. */
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define SL_BGSTATE_FDOPEN(fd, mode) _fdopen((fd), (mode))
#define SL_BGSTATE_CLOSE(fd)        _close(fd)
#else
#include <unistd.h>
#define SL_BGSTATE_HAVE_LINK        1
#define SL_BGSTATE_FDOPEN(fd, mode) fdopen((fd), (mode))
#define SL_BGSTATE_CLOSE(fd)        close(fd)
#endif

#ifdef _WIN32
/* windows.h must precede <GL/gl.h>, which takes APIENTRY and WINGDIAPI from it
 * - the same include order the rest of src/gfx uses. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include "sl_gfx_bgstate.h"
#include "sl_gfx_bggpu.h"          /* SL_BGGPU_DEPTH_* - the depth vocabulary */

/* The backend's own resolver: NULL before a context exists, and NULL when this
 * backend is not the one that came up, so every caller needs a fallback. */
void *sl_gl_proc(const char *name);

/* ---- the post-1.1 tokens ------------------------------------------------ *
 *
 * Every value here was read out of the installed GL/glext.h in this toolchain
 * tree rather than recalled, and each is guarded so a header that already
 * defines it leaves exactly one definition in the translation unit. The GL 1.1
 * tokens this module uses are taken from <GL/gl.h> and are deliberately NOT
 * restated: a second transcription of the registry is a second thing to drift. */
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE                       0x84E0
#endif
#ifndef GL_CLIENT_ACTIVE_TEXTURE
#define GL_CLIENT_ACTIVE_TEXTURE                0x84E1
#endif
#ifndef GL_MAX_TEXTURE_UNITS
#define GL_MAX_TEXTURE_UNITS                    0x84E2
#endif
#ifndef GL_MAX_TEXTURE_COORDS
#define GL_MAX_TEXTURE_COORDS                   0x8871
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                             0x84C0
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM                      0x8B8D
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING                 0x85B5
#endif
#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING                 0x8894
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER_BINDING
#define GL_ELEMENT_ARRAY_BUFFER_BINDING         0x8895
#endif
#ifndef GL_VERTEX_ARRAY_BUFFER_BINDING
#define GL_VERTEX_ARRAY_BUFFER_BINDING          0x8896
#endif
#ifndef GL_NORMAL_ARRAY_BUFFER_BINDING
#define GL_NORMAL_ARRAY_BUFFER_BINDING          0x8897
#endif
#ifndef GL_COLOR_ARRAY_BUFFER_BINDING
#define GL_COLOR_ARRAY_BUFFER_BINDING           0x8898
#endif
#ifndef GL_INDEX_ARRAY_BUFFER_BINDING
#define GL_INDEX_ARRAY_BUFFER_BINDING           0x8899
#endif
#ifndef GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING
#define GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING   0x889A
#endif
#ifndef GL_EDGE_FLAG_ARRAY_BUFFER_BINDING
#define GL_EDGE_FLAG_ARRAY_BUFFER_BINDING       0x889B
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING
#define GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING 0x889C
#endif
#ifndef GL_FOG_COORD_ARRAY_BUFFER_BINDING
#define GL_FOG_COORD_ARRAY_BUFFER_BINDING       0x889D
#endif
#ifndef GL_TRANSFORM_FEEDBACK_BUFFER_BINDING
#define GL_TRANSFORM_FEEDBACK_BUFFER_BINDING    0x8C8F
#endif
#ifndef GL_RASTERIZER_DISCARD
#define GL_RASTERIZER_DISCARD                   0x8C89
#endif
#ifndef GL_CURRENT_FOG_COORD
#define GL_CURRENT_FOG_COORD                    0x8453
#endif
#ifndef GL_FOG_COORD_SRC
#define GL_FOG_COORD_SRC                        0x8450
#endif
#ifndef GL_FOG_COORD_ARRAY
#define GL_FOG_COORD_ARRAY                      0x8457
#endif
#ifndef GL_FOG_COORD_ARRAY_TYPE
#define GL_FOG_COORD_ARRAY_TYPE                 0x8454
#endif
#ifndef GL_FOG_COORD_ARRAY_STRIDE
#define GL_FOG_COORD_ARRAY_STRIDE               0x8455
#endif
#ifndef GL_FOG_COORD_ARRAY_POINTER
#define GL_FOG_COORD_ARRAY_POINTER              0x8456
#endif
#ifndef GL_CURRENT_SECONDARY_COLOR
#define GL_CURRENT_SECONDARY_COLOR              0x8459
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY
#define GL_SECONDARY_COLOR_ARRAY                0x845E
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY_SIZE
#define GL_SECONDARY_COLOR_ARRAY_SIZE           0x845A
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY_TYPE
#define GL_SECONDARY_COLOR_ARRAY_TYPE           0x845B
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY_STRIDE
#define GL_SECONDARY_COLOR_ARRAY_STRIDE         0x845C
#endif
#ifndef GL_SECONDARY_COLOR_ARRAY_POINTER
#define GL_SECONDARY_COLOR_ARRAY_POINTER        0x845D
#endif
#ifndef GL_SAMPLE_BUFFERS
#define GL_SAMPLE_BUFFERS                       0x80A8
#endif
#ifndef GL_SAMPLES
#define GL_SAMPLES                              0x80A9
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE                          0x809D
#endif
#ifndef GL_SAMPLE_ALPHA_TO_COVERAGE
#define GL_SAMPLE_ALPHA_TO_COVERAGE             0x809E
#endif
#ifndef GL_SAMPLE_ALPHA_TO_ONE
#define GL_SAMPLE_ALPHA_TO_ONE                  0x809F
#endif
#ifndef GL_SAMPLE_COVERAGE
#define GL_SAMPLE_COVERAGE                      0x80A0
#endif
#ifndef GL_SAMPLE_COVERAGE_VALUE
#define GL_SAMPLE_COVERAGE_VALUE                0x80AA
#endif
#ifndef GL_SAMPLE_COVERAGE_INVERT
#define GL_SAMPLE_COVERAGE_INVERT               0x80AB
#endif
#ifndef GL_BLEND_SRC_RGB
#define GL_BLEND_SRC_RGB                        0x80C9
#endif
#ifndef GL_BLEND_DST_RGB
#define GL_BLEND_DST_RGB                        0x80C8
#endif
#ifndef GL_BLEND_SRC_ALPHA
#define GL_BLEND_SRC_ALPHA                      0x80CB
#endif
#ifndef GL_BLEND_DST_ALPHA
#define GL_BLEND_DST_ALPHA                      0x80CA
#endif
#ifndef GL_BLEND_EQUATION_RGB
#define GL_BLEND_EQUATION_RGB                   0x8009
#endif
#ifndef GL_BLEND_EQUATION_ALPHA
#define GL_BLEND_EQUATION_ALPHA                 0x883D
#endif
#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP                          0x864F
#endif

typedef void (APIENTRY *SL_BGS_ACTIVETEXTURE)(GLenum);
typedef void (APIENTRY *SL_BGS_CLIENTACTIVETEXTURE)(GLenum);

/* ---- the bounds --------------------------------------------------------- *
 *
 * THE SELECTOR CEILING IS THE SAME 16 sl_gfx_bgtf.c's isolation uses, and for
 * the same reason it gives: GL_MAX_TEXTURE_COORDS is 8 or fewer on the
 * compatibility profiles this renderer runs on, so 16 is headroom rather than a
 * guess that might bind. The two DOMAINS are bounded independently against it -
 * the coordinate-set domain the client selector selects among, and the
 * conventional server unit domain - because the OpenGL 2.1 specification allows
 * them to differ (sections 2.7, 2.8 and 2.11.2) and a context reporting a wider
 * domain than this ceiling is REFUSED rather than swept in part. */
#define BGSTATE_UNITS       16

/* ROWS PER PHASE SNAPSHOT. The traversal emits about 310 on a context that
 * answers both domains at the ceiling - 55 of those capabilities now carry TWO
 * rows each, the destination answer and the return-only answer beside it, for
 * the reason recorded at bgs_q_ena - and the rest is headroom. An overflow is
 * recorded as its own fact rather than dropping rows silently. */
#define BGSTATE_ROWS       384

/* PRODUCERS WHOSE PHASES ARE RECORDED, and producer identities KEPT. The first
 * number bounds the work and the storage; the second is nearly free and is what
 * lets a cache hit on a later producer be reported as an identified but
 * UNOBSERVED link instead of as no link at all. */
#define BGSTATE_PRODUCERS    3
#define BGSTATE_LEDGER       8

/* One PRE_RASTER snapshot plus three per recorded producer. */
#define BGSTATE_SNAPS      (BGSTATE_PRODUCERS * 3 + 1)

/* COMPONENTS IN ONE ROW. Sixteen is the matrix queries' width and the widest
 * answer this module asks for. */
#define BGSTATE_VMAX        16

/* THE RUN'S CEILING, checked before any index is read. Sized for one background
 * item's frame with headroom; a larger one is a defect rather than a bigger
 * picture. The same number sl_gfx_bgtf.h uses for the same reason. */
#define BGSTATE_MAX_INDICES 4194304u

/* INDICES RECORDED VERBATIM. The run's identity is its count and its index
 * range; the whole sequence is the buffer diagnostic's business and copying it
 * here would make this record grow with the scene. */
#define BGSTATE_IX_SHOWN     8

/* ---- the unwritten-answer sentinel ------------------------------------- *
 *
 * WHY A SENTINEL AND NOT glGetError. Reading the error state CONSUMES it, and a
 * diagnostic that drained an error the pass was about to observe would corrupt
 * the run it exists to record. So every destination component is pre-filled
 * with a value and compared afterwards, and "unwritten" becomes a property of
 * the data.
 *
 * IT IS PER COMPONENT, which is what makes a PARTIAL answer visible. A context
 * that writes the first component of a sixteen-component matrix query and no
 * more is a real shape - the offline fixture's GL does exactly that - and a
 * single whole-row sentinel would have read that as a complete answer.
 *
 * 0 WOULD NOT DO. It is the value a zeroed destination holds by default and a
 * legal answer to most of these queries, so the two could not be told apart.
 * As a GLint this pattern is a large negative number that no enum, count,
 * binding, size, stride or mask answer can take; as a float it is a tiny
 * negative denormal that no state this module reads is set to. Neither is a
 * proof of impossibility, so a row whose answer happened to equal its sentinel
 * is reported UNKNOWN - which understates what was learned and never
 * overstates it. */
#define BGSTATE_SENT(i)  (0xA5A5A500u | (unsigned) (i))

/* THE ROW KINDS. 'E' is an enable state read through a DESTINATION query, which
 * is sentinel-checkable; 'R' is the value a RETURN-ONLY call handed back, which
 * is not. Keeping them apart is the whole of the second correction: a return
 * value cannot be told from a refusal to answer, so it may never wear the same
 * status as a destination answer. */
enum { BGS_I = 'I', BGS_F = 'F', BGS_E = 'E', BGS_P = 'P', BGS_H = 'H',
       BGS_R = 'R' };

/* RETURNED is deliberately not OK. It says "the call handed this back" and
 * nothing about whether the capability is one the implementation accepts. */
enum { BGS_OK = 0, BGS_PARTIAL = 1, BGS_UNKNOWN = 2, BGS_RETURNED = 3 };

#define BGS_NO_UNIT (-1)

struct bgs_row {
    const char   *name;
    unsigned      pname;
    unsigned      v[BGSTATE_VMAX];
    unsigned      mask;        /* bit i: component i still held its sentinel */
    short         unit;        /* the coordinate set or server unit, or -1   */
    unsigned char kind;
    unsigned char n;
    unsigned char status;
};

/* A DOMAIN'S OUTCOME FOR ONE SNAPSHOT. `why` is NULL when the domain was swept;
 * a non-NULL reason means NO row of that domain was emitted and the record says
 * so, rather than emitting the sets this module could reach and letting a
 * reader take a truncated sweep for a whole one.
 *
 * ATTEMPTED AND VERIFIED ARE TWO DIFFERENT FACTS, and conflating them was a
 * defect: a single `restored` flag was set when the restore WRITE was issued,
 * before the read-back that is the only evidence the write took. A domain could
 * then be serialised OK, and the capture sealed complete, while this probe had
 * in fact left the wrong texture unit selected for the draw that followed.
 *
 *   attempted   the inherited selection was re-issued
 *   verified    AND a read-back proved the selector now holds it
 *
 * A mismatch, a read-back the context did not write, or one that is not a
 * selectable set leaves `verified` 0 with `restore_why` saying which, and that
 * makes the whole capture INCOMPLETE. Nothing is written a second time to force
 * agreement: a corrective re-issue would be exactly the rendering-state repair
 * this module must never perform, and it would also destroy the evidence that
 * the first write did not take. */
struct bgs_domain {
    int         ok;
    const char *why;
    GLint       width;         /* the domain the context answered, or -1     */
    GLint       saved;         /* the inherited selector, or the sentinel    */
    GLint       after;         /* the selector re-read AFTER the restore     */
    int         moved;         /* the traversal selected at least one unit   */
    int         attempted;     /* the inherited selector was re-issued       */
    int         verified;      /* ... and a read-back PROVED it took         */
    const char *restore_why;   /* why it is not verified; "" when it is      */
};

struct bgs_snap {
    int               used;
    int               phase;         /* 1..4                                */
    int               producer;      /* ledger index, or -1                 */
    unsigned          nrows;
    int               overflow;
    /* THE TRAVERSAL WAS STOPPED after a restore this probe could not prove.
     * Carrying on to the second domain would have moved another selector while
     * the first was in an unknown state, which is the one thing a probe that
     * promises not to disturb a draw must not do. The rows already observed are
     * KEPT - they are evidence, and discarding them would lose what was
     * measured before the restore went unproved - and the halt is recorded. */
    int               halted;
    struct bgs_domain client, server;
    struct bgs_row    row[BGSTATE_ROWS];
};

/* A PREPASS SETUP AS AN EVENT. The key is the whole identity of the work, which
 * is what lets a later cache hit be resolved to the setup that actually ran
 * rather than to whatever ran last. */
struct bgs_event {
    int      used;
    unsigned id;                /* 1-based, in the order events were seen   */
    int      slot;
    unsigned gen;
    int      dmode;
    unsigned k1bits, k2bits;
    unsigned nverts;
    int      state_only;
    int      observed;          /* phases recorded for it                   */
    int      s_pre, s_draw, s_restore;   /* snapshot indices, or -1         */
    unsigned mesh_ordinal;      /* the walk's mesh counter when it ran      */
    unsigned reuse_hits;
};

enum { BGS_LINK_NONE = 0, BGS_LINK_FRESH, BGS_LINK_REUSED, BGS_LINK_UNOBSERVED };

/* ---- the module's state ------------------------------------------------- */

static int   g_optin = -1;          /* the switch, parsed once              */
static int   g_armed;               /* ... and the provenance accepted      */
static char  g_sel_text[224];
static unsigned g_w_read = ~0u, g_w_frame = ~0u, g_w_cloud = ~0u;
static unsigned g_w_mesh = ~0u, g_w_mat = ~0u;
static int      g_w_dmode = -2;     /* -2 any, else 0..2                    */
static char     g_w_item[64];
static unsigned char g_exe_sha[32], g_src_sha[32];

static int   g_done;                /* the one attempt is SPENT             */
static int   g_state;               /* 0 none, 1 complete, -1 refused       */
static char  g_status[256];
static struct sl_bgstate_counters g_c;

static int   g_walk_armed;          /* this walk's clock tuple matched      */
static unsigned g_mesh_ordinal;     /* meshes offered in this walk          */
static int   g_sel_live;            /* this mesh is the selected one        */
static struct sl_bgstate_site g_site;

static struct bgs_event g_ev[BGSTATE_LEDGER];
static unsigned g_ev_n;             /* events seen in this walk             */
static int      g_ev_cur = -1;      /* the event pre_setup opened           */
static int      g_link = -1;        /* the producer serving THIS mesh       */
static int      g_link_kind = BGS_LINK_NONE;

static struct bgs_snap  g_snap[BGSTATE_SNAPS];
static struct bgs_snap *g_fill;     /* the snapshot being filled            */
static int              g_snap_n;

static SL_BGS_ACTIVETEXTURE       p_ActiveTexture;
static SL_BGS_CLIENTACTIVETEXTURE p_ClientActiveTexture;

static int g_said;                  /* a malformed selector is reported once */

/* ---- the switch and the selector ---------------------------------------- *
 *
 * THE GRAMMAR FAILS CLOSED, and the parse is the buffer diagnostic's grammar
 * restated for this probe's own variable. The two were deliberately not shared:
 * one selector, one switch and one spent attempt per diagnostic is what keeps a
 * campaign able to arm either without arming the other. */

#define BGSTATE_MAX_DIGITS 10       /* 4294967295 is ten digits */

static int bgs_part(const char **s, unsigned *out)
{
    const char *p = *s;
    unsigned v = 0u;
    int n = 0;

    if (*p == '*') {
        p++;
        if (*p != ':' && *p != '\0') return 0;
        *out = ~0u;
        *s = (*p == ':') ? p + 1 : p;
        return 1;
    }
    while (*p >= '0' && *p <= '9') {
        if (++n > BGSTATE_MAX_DIGITS) return 0;
        v = v * 10u + (unsigned) (*p - '0');
        p++;
    }
    if (n == 0) return 0;
    /* A ten-digit value can still have wrapped, so the one case that can is
     * checked rather than assumed away. */
    if (n == BGSTATE_MAX_DIGITS) {
        char back[12];
        snprintf(back, sizeof back, "%u", v);
        if (strncmp(back, p - n, (size_t) n) != 0) return 0;
    }
    if (*p != ':' && *p != '\0') return 0;
    *out = v;
    *s = (*p == ':') ? p + 1 : p;
    return 1;
}

static int bgs_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* A PROVENANCE DIGEST IS REQUIRED TO ARM. A record that names no build is not
 * evidence, and an all-zero digest is what an unset field looks like - so both
 * the shape and the content are checked, and a refusal is reported with the
 * variable's own name rather than left as a silent off. */
static int bgs_sha_arg(const char *text, unsigned char *out, const char *what)
{
    int i, zero = 1;

    if (text == NULL || strlen(text) != 64u) {
        fprintf(stderr, "#95 SL_BG_GLSTATE: %s must be exactly 64 lowercase hex "
                        "characters naming the build this record describes - the "
                        "probe stays OFF\n", what);
        return 0;
    }
    for (i = 0; i < 32; i++) {
        const int hi = bgs_nibble(text[i * 2]);
        const int lo = bgs_nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            fprintf(stderr, "#95 SL_BG_GLSTATE: %s is not lowercase hex - the "
                            "probe stays OFF\n", what);
            return 0;
        }
        out[i] = (unsigned char) ((hi << 4) | lo);
        if (out[i] != 0u) zero = 0;
    }
    if (zero) {
        fprintf(stderr, "#95 SL_BG_GLSTATE: %s is all zeros, which is what an "
                        "unset field looks like - the probe stays OFF\n", what);
        return 0;
    }
    return 1;
}

static void bgs_disarm(const char *why)
{
    g_optin = 0;
    g_armed = 0;
    if (!g_said) {
        g_said = 1;
        fprintf(stderr, "#95 SL_BG_GLSTATE: %s. The expected form is "
                        "read:frame:cloud:mesh:material:dmode[:item-substring], "
                        "each numeric field a decimal or '*' and dmode one of "
                        "'*', 0, 1, 2 - the probe stays OFF\n", why);
    }
}

int sl_bgstate_armed(void)
{
    const char *v, *p;
    unsigned f[6];
    int i;

    if (g_optin >= 0) return g_armed;
    g_optin = 0;
    g_armed = 0;

    v = getenv("SL_BG_GLSTATE");
    if (v == NULL || *v == '\0' || strcmp(v, "0") == 0) return 0;

    p = v;
    for (i = 0; i < 6; i++) {
        if (!bgs_part(&p, &f[i])) { bgs_disarm("the selector could not be read"); return 0; }
    }
    /* The optional item substring is whatever is left, bounded. A ':' inside it
     * is kept: the field is the last one, so nothing after it is parsed. */
    g_w_item[0] = '\0';
    if (*p != '\0') {
        if (strlen(p) >= sizeof g_w_item) {
            bgs_disarm("the item substring is longer than this selector carries");
            return 0;
        }
        snprintf(g_w_item, sizeof g_w_item, "%s", p);
    }
    if (f[5] != ~0u && f[5] > 2u) {
        bgs_disarm("dmode must be '*', 0, 1 or 2");
        return 0;
    }
    if (!bgs_sha_arg(getenv("SL_BG_GLSTATE_EXE_SHA"), g_exe_sha,
                     "SL_BG_GLSTATE_EXE_SHA")
        || !bgs_sha_arg(getenv("SL_BG_GLSTATE_SRC_SHA"), g_src_sha,
                        "SL_BG_GLSTATE_SRC_SHA"))
        return 0;

    g_w_read  = f[0];
    g_w_frame = f[1];
    g_w_cloud = f[2];
    g_w_mesh  = f[3];
    g_w_mat   = f[4];
    g_w_dmode = (f[5] == ~0u) ? -2 : (int) f[5];
    snprintf(g_sel_text, sizeof g_sel_text, "%s", v);
    g_optin = 1;
    g_armed = 1;
    fprintf(stderr, "#95 SL_BG_GLSTATE armed: %s - ONE background draw's "
                    "renderer state is recorded and NOTHING is changed, repaired "
                    "or concluded\n", g_sel_text);
    return 1;
}

int         sl_bgstate_state(void)  { return g_state; }
const char *sl_bgstate_status(void) { return g_status; }
const struct sl_bgstate_counters *sl_bgstate_stats(void) { return &g_c; }

static void bgs_fail(const char *status)
{
    g_state = -1;
    snprintf(g_status, sizeof g_status, "%s", status);
    fprintf(stderr, "#95 SL_BG_GLSTATE: %s - no complete record was sealed and "
                    "nothing existing was overwritten\n", status);
}

/* ---- the host floating-point control words ------------------------------ *
 *
 * READ ONLY, AND NEVER WRITTEN. The rounding mode and the x87 precision control
 * decide what the CPU's own arithmetic produces, so they belong in a record
 * about a value that differs by one. Storing them is the whole of the
 * interaction: no fesetround, no _controlfp, no fldcw and no ldmxcsr appears in
 * this module, because changing the mode to observe it would change the frame
 * being observed.
 *
 * THE TARGET IS WHAT MAKES THIS SMALL. This renderer is built for i386 with
 * -msse2 -mfpmath=sse, so both registers exist and one instruction reads each.
 * Where they do not - any other architecture - the rows are recorded
 * UNSUPPORTED and no portability layer is introduced for them. */
#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#define BGSTATE_HAVE_X87 1
static unsigned bgs_x87cw(void)
{
    unsigned short w = 0u;
    __asm__ __volatile__("fnstcw %0" : "=m" (w));
    return (unsigned) w;
}
#if defined(__SSE__) || defined(__SSE2__)
#define BGSTATE_HAVE_MXCSR 1
static unsigned bgs_mxcsr(void)
{
    unsigned w = 0u;
    __asm__ __volatile__("stmxcsr %0" : "=m" (w));
    return w;
}
#endif
#endif

/* ---- the bounded query table -------------------------------------------- */

static struct bgs_row *bgs_row_new(const char *name, unsigned pname, int kind,
                                   int n, int unit)
{
    struct bgs_row *r;
    int i;

    if (g_fill == NULL) return NULL;
    if (g_fill->nrows >= (unsigned) BGSTATE_ROWS) { g_fill->overflow = 1; return NULL; }
    r = &g_fill->row[g_fill->nrows++];
    memset(r, 0, sizeof *r);
    r->name   = name;
    r->pname  = pname;
    r->kind   = (unsigned char) kind;
    r->n      = (unsigned char) n;
    r->unit   = (short) unit;
    r->status = BGS_UNKNOWN;
    for (i = 0; i < n && i < BGSTATE_VMAX; i++) r->v[i] = BGSTATE_SENT(i);
    return r;
}

/* THE ANSWER'S STATUS, DECIDED FROM THE DESTINATION. A component still holding
 * its sentinel was not written, and the three outcomes are kept apart because
 * they are different facts: an answer, a partial answer, and no answer at all.
 * A partial one keeps the components that WERE written - discarding them would
 * throw away the evidence a context's own shortfall produced. */
static void bgs_row_settle(struct bgs_row *r)
{
    unsigned i, sent = 0u;

    for (i = 0u; i < (unsigned) r->n && i < BGSTATE_VMAX; i++)
        if (r->v[i] == BGSTATE_SENT(i)) { r->mask |= 1u << i; sent++; }
    if (sent == 0u)              r->status = BGS_OK;
    else if (sent == (unsigned) r->n) { r->status = BGS_UNKNOWN; g_c.rows_unknown++; }
    else                         { r->status = BGS_PARTIAL; g_c.rows_partial++; }
}

static void bgs_q_int(const char *name, unsigned pname, int n, int unit)
{
    struct bgs_row *r = bgs_row_new(name, pname, BGS_I, n, unit);
    GLint tmp[BGSTATE_VMAX];
    int i;

    if (r == NULL) return;
    for (i = 0; i < n; i++) memcpy(&tmp[i], &r->v[i], sizeof tmp[i]);
    glGetIntegerv((GLenum) pname, tmp);
    g_c.queries++;
    for (i = 0; i < n; i++) memcpy(&r->v[i], &tmp[i], sizeof r->v[i]);
    bgs_row_settle(r);
}

/* RAW BITS, NOT A PRINTED FLOAT. A record whose purpose is a value that differs
 * by one cannot round-trip its evidence through a decimal conversion, so the
 * float's own bit pattern is what is stored and what is written out. */
static void bgs_q_flt(const char *name, unsigned pname, int n, int unit)
{
    struct bgs_row *r = bgs_row_new(name, pname, BGS_F, n, unit);
    GLfloat tmp[BGSTATE_VMAX];
    int i;

    if (r == NULL) return;
    for (i = 0; i < n; i++) memcpy(&tmp[i], &r->v[i], sizeof tmp[i]);
    glGetFloatv((GLenum) pname, tmp);
    g_c.queries++;
    for (i = 0; i < n; i++) memcpy(&r->v[i], &tmp[i], sizeof r->v[i]);
    bgs_row_settle(r);
}

/* THE ENABLE STATE COMES FROM THE DESTINATION QUERY, AND glIsEnabled's RETURN
 * IS RECORDED BESIDE IT RATHER THAN TAKEN FOR IT.
 *
 * WHY A RETURN-ONLY ANSWER IS NOT SUPPORT EVIDENCE. glIsEnabled returns a
 * boolean by value, so there is no destination to leave unwritten and nothing
 * to compare a sentinel against - which meant a `false` was recorded as a
 * confident "this capability is off". But `false` is ALSO what the call returns
 * for a capability the implementation does not accept: it sets GL_INVALID_ENUM
 * and answers false, and the two outcomes are indistinguishable from the return
 * value alone. Microsoft's own reference states both halves - the accepted
 * capability list, the GL_INVALID_ENUM on anything else, and "false otherwise"
 * (https://learn.microsoft.com/en-us/windows/win32/opengl/glisenabled). Reading
 * the error flag would settle it and is exactly what this module must not do,
 * because that read CONSUMES state the pass may be about to observe.
 *
 * WHAT REPLACES IT. The same state has a DESTINATION query: boolean enable
 * states are among the values glGetIntegerv writes through its pointer
 * (https://learn.microsoft.com/en-us/windows/win32/opengl/glgetintegerv). A
 * destination can be pre-filled with a sentinel, so a capability the context
 * will not answer for is UNKNOWN rather than "disabled", and the ordinary
 * sentinel machinery already does that work. This is a reuse of the existing
 * query path, not a capability framework: nothing is cached, nothing is probed
 * ahead of time and no extension string is consulted.
 *
 * AND THE ANSWER IS VALIDATED AS A BOOLEAN. A destination query that writes
 * something other than GL_FALSE or GL_TRUE has not answered an enable state,
 * so the row becomes UNKNOWN with its own count rather than being coerced to 0
 * or 1. An unwritten answer and a non-boolean answer are both refusals to
 * answer; neither is allowed to become a state.
 *
 * THE RETURN IS STILL WORTH RECORDING, because it is what the renderer's own
 * code paths see, and a disagreement between the two is itself an observation.
 * It gets its own row, its own kind and its own status - RETURNED, which says
 * only "the call handed this back" and can never be read as OK. */
static void bgs_q_ena(const char *name, const char *ret_name, unsigned cap,
                      int unit)
{
    struct bgs_row *r = bgs_row_new(name, cap, BGS_E, 1, unit);
    struct bgs_row *q;
    GLint dest;

    if (r != NULL) {
        memcpy(&dest, &r->v[0], sizeof dest);
        glGetIntegerv((GLenum) cap, &dest);
        g_c.queries++;
        memcpy(&r->v[0], &dest, sizeof r->v[0]);
        bgs_row_settle(r);
        if (r->status == BGS_OK && r->v[0] != (unsigned) GL_FALSE
            && r->v[0] != (unsigned) GL_TRUE) {
            /* WRITTEN, BUT NOT A BOOLEAN. The raw value is kept - it is what the
             * context said - and the status refuses to call it an enable state. */
            r->status = BGS_UNKNOWN;
            r->mask = 1u;
            g_c.rows_nonboolean++;
            g_c.rows_unknown++;
        }
    }

    q = bgs_row_new(ret_name, cap, BGS_R, 1, unit);
    if (q != NULL) {
        q->v[0] = (unsigned) (glIsEnabled((GLenum) cap) != (GLboolean) 0);
        g_c.queries++;
        q->mask = 0u;
        q->status = BGS_RETURNED;
    }
}

/* ONE NAME, TWO ROWS. The capability's token supplies both row names, so the
 * destination row and the return-only row can never drift apart or be given to
 * different capabilities by a typo at a call site. */
#define BGS_ENA(cap, unit) bgs_q_ena(#cap, #cap "@ISENABLED_RETURN", (cap), (unit))

/* A POINTER IS RECORDED AND NEVER FOLLOWED. The value identifies which array a
 * draw would read; dereferencing it would read the player's own extracted item
 * into a diagnostic, which is not this record's business and is not done.
 *
 * ONE 32-BIT COMPONENT, AND THAT IS THE TARGET'S OWN WIDTH. This renderer is
 * built for i386, where a pointer is four bytes and the sentinel can therefore
 * be carried in the destination the query writes. On a wider target the high
 * half would be lost, which is why this module is native-only for this one and
 * says so rather than pretending to a portable encoding. */
static void bgs_q_ptr(const char *name, unsigned pname, int unit)
{
    struct bgs_row *r = bgs_row_new(name, pname, BGS_P, 1, unit);
    GLvoid *p;

    if (r == NULL) return;
    p = (GLvoid *) (size_t) r->v[0];
    glGetPointerv((GLenum) pname, &p);
    g_c.queries++;
    r->v[0] = (unsigned) (size_t) p;
    bgs_row_settle(r);
}

static void bgs_row_host(const char *name, unsigned value, int supported)
{
    struct bgs_row *r = bgs_row_new(name, 0u, BGS_H, 1, BGS_NO_UNIT);

    if (r == NULL) return;
    if (!supported) { r->status = BGS_UNKNOWN; r->mask = 1u; g_c.rows_unknown++; return; }
    r->v[0] = value;
    r->mask = 0u;
    r->status = BGS_OK;
}

/* ---- the selector domains ----------------------------------------------- *
 *
 * EVERY CAPABILITY QUESTION IS SETTLED BEFORE ANY SELECTOR MOVES, which makes
 * "the context is left exactly as it was" a property of the order rather than a
 * promise: the entry point is resolved, the domain width is read, and the
 * inherited selection is read and validated while nothing has been selected. A
 * refusal therefore returns with the selector where it was found, and the
 * traversal that follows a success cannot fail in a way that leaves it moved.
 *
 * THE SENTINEL FOR AN UNANSWERED SELECTOR IS NEGATIVE AND NOT 0. The legal
 * answer is GL_TEXTURE0 + index and GL_TEXTURE0 is 0x84C0, so a save still
 * holding this value means the context did not answer - which is a refusal
 * rather than unit 0, and 0 is itself one of the answers it cannot be. */
#define BGS_SEL_NONE (-1)

/* ---- THE TRAVERSAL-HALT LATCH ------------------------------------------- *
 *
 * IT BELONGS TO THE PROBE, NOT TO A SNAPSHOT, and that distinction is the whole
 * of this correction. The first implementation recorded the halt in the
 * snapshot being filled, which a new snapshot zeroed - so an unproved restore at
 * PRE_SETUP was followed by POST_DRAW_RAW, POST_RESTORE and PRE_RASTER each
 * deciding, sweeping and RE-WRITING both selectors from scratch. The promise
 * "no further selector traversal after an unproved restoration" held for the
 * rest of one snapshot and for nothing after it, which is the opposite of what
 * a draw this module exists not to disturb needs. A server-domain failure did
 * not even halt its own snapshot, because the per-snapshot flag was raised only
 * on the client branch.
 *
 * ONCE SET IT IS NEVER CLEARED. Not by a new snapshot, not by a producer stage,
 * not by a mesh, and not by sl_bgstate_begin_walk - which resets the snapshot
 * store and the producer ledger and must not reset this. The selector state this
 * probe left behind is unknown for the rest of the process, and no later phase
 * can make it known again by writing more.
 *
 * WHAT STILL HAPPENS AFTER IT IS SET. Everything that reads and writes nothing:
 * the global rows, the client-array rows, the host floating-point words. What
 * does NOT happen is any further selector write, domain decision, sweep or
 * repair attempt - both domains are explicitly REFUSED in every later snapshot,
 * carrying the reason the first failure recorded. Rows and phase and producer
 * evidence already observed are kept, because they are what was measured.
 *
 * THE FIRST FAILURE OWNS THE REASON. A later one cannot overwrite it: the
 * question a reader has is which disturbance went unproved FIRST, since
 * everything after it is already suspect.
 *
 * THIS IS NOT A CONTEXT-RESET FRAMEWORK. There is no re-arming, no recovery
 * path and no second chance; the latch has one transition and no way back. */
static int         g_halt;
static const char *g_halt_why = "";
static const char *g_halt_domain = "";
static int         g_halt_phase;

/* Forward declaration: the latch is raised from bgs_domain_restore, which is
 * defined above the snapshot machinery that reads it. */
static void bgs_halt(int client, const char *why);

/* A DOMAIN'S FIELDS BEFORE ANY QUESTION IS ASKED. The two selector fields take
 * the sentinel rather than 0, because 0 is a legal answer to neither selector
 * query and a zeroed struct would read as "the context answered enum 0". */
static void bgs_domain_init(struct bgs_domain *d)
{
    memset(d, 0, sizeof *d);
    d->width = -1;
    d->saved = BGS_SEL_NONE;
    d->after = BGS_SEL_NONE;
    d->restore_why = "";
}

static void bgs_domain_decide(struct bgs_domain *d, unsigned max_pname,
                              unsigned sel_pname, int have_entry,
                              const char *no_entry_why)
{
    GLint width = -1, sel = BGS_SEL_NONE, index;

    bgs_domain_init(d);

    if (!have_entry) { d->why = no_entry_why; g_c.domain_refusals++; return; }

    glGetIntegerv((GLenum) max_pname, &width);
    g_c.queries++;
    d->width = width;
    if (width < 1) {
        d->why = "the context did not answer the domain width";
        g_c.domain_refusals++;
        return;
    }
    if (width > BGSTATE_UNITS) {
        d->why = "the context reports a domain wider than this probe's bounded "
                 "ceiling, and a part of it is not a sweep";
        g_c.domain_refusals++;
        return;
    }
    glGetIntegerv((GLenum) sel_pname, &sel);
    g_c.queries++;
    d->saved = sel;
    index = sel - GL_TEXTURE0;
    if (sel == BGS_SEL_NONE || index < 0 || index >= width) {
        d->why = "the context did not answer the inherited selection with a "
                 "selectable set, so it could not be put back";
        g_c.domain_refusals++;
        return;
    }
    d->ok = 1;
}

/* THE INHERITED SELECTION IS PUT BACK, AND THE RESTORE IS THEN PROVED.
 *
 * RE-ISSUING THE SELECTOR IS ONLY THE ATTEMPT. The evidence is the read-back
 * that follows, and until it agrees the restore is UNPROVED - not "done". Three
 * answers are failures, and they are kept apart because they are different
 * facts: a read-back the context did not write at all, one that is not a
 * selectable set of this domain, and one that is a legal set but NOT the
 * inherited one, which means this probe left the wrong unit selected for the
 * draw that follows.
 *
 * THE SENTINEL IS WHAT MAKES "DID NOT WRITE" VISIBLE. `back` is seeded with a
 * value no legal selector enum can take (the answer is GL_TEXTURE0 + index and
 * GL_TEXTURE0 is 0x84C0), so a query that writes nothing is distinguishable
 * from one that answers - and from unit 0, which is itself a legal answer.
 *
 * NOTHING IS WRITTEN A SECOND TIME. A corrective re-issue would be the
 * rendering-state repair this module must never perform, and it would destroy
 * the evidence that the first write did not take. The failure is recorded, the
 * capture becomes INCOMPLETE, and the caller stops traversing.
 *
 * A DOMAIN THAT WAS NEVER MOVED IS VERIFIED BY CONSTRUCTION, and that is not
 * the same as a vacuous pass: `attempted` stays 0, so a reader can tell "there
 * was nothing to put back" from "it was put back and proved". It must not be
 * reported as a failure either, because the inherited selection was never
 * disturbed. */
static void bgs_domain_restore(struct bgs_domain *d, unsigned sel_pname,
                               int client)
{
    GLint back, index;
    const char *fail = NULL;

    if (!d->ok) return;
    if (!d->moved) {
        d->verified = 1;
        d->restore_why = "";
        return;
    }

    if (client) p_ClientActiveTexture((GLenum) d->saved);
    else        p_ActiveTexture((GLenum) d->saved);
    d->attempted = 1;
    g_c.selector_restore_attempts++;

    back = BGS_SEL_NONE;
    glGetIntegerv((GLenum) sel_pname, &back);
    g_c.queries++;
    d->after = back;

    index = back - GL_TEXTURE0;
    if (back == BGS_SEL_NONE) {
        fail = "the context did not answer the selector after the restore, so "
               "the restore is UNPROVED";
    } else if (index < 0 || index >= d->width) {
        fail = "the selector read back after the restore is not a selectable "
               "set of this domain, so the restore is UNPROVED";
    } else if (back != d->saved) {
        fail = "the selector read back after the restore differs from the "
               "inherited selection: this probe LEFT THE WRONG UNIT SELECTED";
    }

    if (fail != NULL) {
        /* ONE EXIT FOR EVERY FAILURE SHAPE, which is what makes the latch below
         * impossible to miss: a fourth shape added later cannot forget to raise
         * it, and the client and the server domain reach it by the same path. */
        d->restore_why = fail;
        g_c.selector_restore_failures++;
        bgs_halt(client, fail);
        return;
    }

    d->verified = 1;
    d->restore_why = "";
    g_c.selector_restores++;
}

/* RAISE THE LATCH. The phase is taken from the snapshot being filled, so the
 * record can say which labelled moment the first unproved restore happened in.
 * A second call is a no-op: the FIRST failure owns the reason. */
static void bgs_halt(int client, const char *why)
{
    if (g_halt) return;
    g_halt = 1;
    g_halt_why = why;
    g_halt_domain = client ? "client_coord_sets" : "server_units";
    g_halt_phase = (g_fill != NULL) ? g_fill->phase : 0;
    fprintf(stderr, "#95 SL_BG_GLSTATE: the %s selector's restore could not be "
                    "proved at phase %d - %s. NO further selector traversal is "
                    "attempted for the rest of this process, and this capture is "
                    "INCOMPLETE\n", g_halt_domain, g_halt_phase, why);
}

/* ---- one phase snapshot ------------------------------------------------- */

static void bgs_rows_global(void)
{
    bgs_q_int("GL_CURRENT_PROGRAM", GL_CURRENT_PROGRAM, 1, BGS_NO_UNIT);
    bgs_q_int("GL_VERTEX_ARRAY_BINDING", GL_VERTEX_ARRAY_BINDING, 1, BGS_NO_UNIT);
    bgs_q_int("GL_ARRAY_BUFFER_BINDING", GL_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);
    bgs_q_int("GL_ELEMENT_ARRAY_BUFFER_BINDING", GL_ELEMENT_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);
    bgs_q_int("GL_TRANSFORM_FEEDBACK_BUFFER_BINDING", GL_TRANSFORM_FEEDBACK_BUFFER_BINDING, 1, BGS_NO_UNIT);
    BGS_ENA(GL_RASTERIZER_DISCARD, BGS_NO_UNIT);
    bgs_q_int("GL_ACTIVE_TEXTURE", GL_ACTIVE_TEXTURE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_CLIENT_ACTIVE_TEXTURE", GL_CLIENT_ACTIVE_TEXTURE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_MAX_TEXTURE_UNITS", GL_MAX_TEXTURE_UNITS, 1, BGS_NO_UNIT);
    bgs_q_int("GL_MAX_TEXTURE_COORDS", GL_MAX_TEXTURE_COORDS, 1, BGS_NO_UNIT);

    /* The current attributes. The specification leaves these UNSPECIFIED after
     * an array draw (OpenGL 2.1, section 2.8), which is why they are recorded
     * and why a difference in one of them proves nothing on its own. */
    bgs_q_flt("GL_CURRENT_COLOR", GL_CURRENT_COLOR, 4, BGS_NO_UNIT);
    bgs_q_flt("GL_CURRENT_SECONDARY_COLOR", GL_CURRENT_SECONDARY_COLOR, 4, BGS_NO_UNIT);
    bgs_q_flt("GL_CURRENT_NORMAL", GL_CURRENT_NORMAL, 3, BGS_NO_UNIT);
    bgs_q_flt("GL_CURRENT_FOG_COORD", GL_CURRENT_FOG_COORD, 1, BGS_NO_UNIT);
    bgs_q_flt("GL_CURRENT_INDEX", GL_CURRENT_INDEX, 1, BGS_NO_UNIT);

    BGS_ENA(GL_FOG, BGS_NO_UNIT);
    bgs_q_int("GL_FOG_MODE", GL_FOG_MODE, 1, BGS_NO_UNIT);
    bgs_q_flt("GL_FOG_DENSITY", GL_FOG_DENSITY, 1, BGS_NO_UNIT);
    bgs_q_flt("GL_FOG_START", GL_FOG_START, 1, BGS_NO_UNIT);
    bgs_q_flt("GL_FOG_END", GL_FOG_END, 1, BGS_NO_UNIT);
    bgs_q_flt("GL_FOG_COLOR", GL_FOG_COLOR, 4, BGS_NO_UNIT);
    bgs_q_flt("GL_FOG_INDEX", GL_FOG_INDEX, 1, BGS_NO_UNIT);
    bgs_q_int("GL_FOG_COORD_SRC", GL_FOG_COORD_SRC, 1, BGS_NO_UNIT);
    bgs_q_int("GL_FOG_HINT", GL_FOG_HINT, 1, BGS_NO_UNIT);

    bgs_q_int("GL_MATRIX_MODE", GL_MATRIX_MODE, 1, BGS_NO_UNIT);
    bgs_q_flt("GL_MODELVIEW_MATRIX", GL_MODELVIEW_MATRIX, 16, BGS_NO_UNIT);
    bgs_q_flt("GL_PROJECTION_MATRIX", GL_PROJECTION_MATRIX, 16, BGS_NO_UNIT);

    bgs_q_int("GL_VIEWPORT", GL_VIEWPORT, 4, BGS_NO_UNIT);
    bgs_q_flt("GL_DEPTH_RANGE", GL_DEPTH_RANGE, 2, BGS_NO_UNIT);
    BGS_ENA(GL_DEPTH_TEST, BGS_NO_UNIT);
    bgs_q_int("GL_DEPTH_FUNC", GL_DEPTH_FUNC, 1, BGS_NO_UNIT);
    bgs_q_int("GL_DEPTH_WRITEMASK", GL_DEPTH_WRITEMASK, 1, BGS_NO_UNIT);
    BGS_ENA(GL_DEPTH_CLAMP, BGS_NO_UNIT);

    BGS_ENA(GL_BLEND, BGS_NO_UNIT);
    bgs_q_int("GL_BLEND_SRC", GL_BLEND_SRC, 1, BGS_NO_UNIT);
    bgs_q_int("GL_BLEND_DST", GL_BLEND_DST, 1, BGS_NO_UNIT);
    bgs_q_int("GL_BLEND_SRC_RGB", GL_BLEND_SRC_RGB, 1, BGS_NO_UNIT);
    bgs_q_int("GL_BLEND_DST_RGB", GL_BLEND_DST_RGB, 1, BGS_NO_UNIT);
    bgs_q_int("GL_BLEND_SRC_ALPHA", GL_BLEND_SRC_ALPHA, 1, BGS_NO_UNIT);
    bgs_q_int("GL_BLEND_DST_ALPHA", GL_BLEND_DST_ALPHA, 1, BGS_NO_UNIT);
    bgs_q_int("GL_BLEND_EQUATION_RGB", GL_BLEND_EQUATION_RGB, 1, BGS_NO_UNIT);
    bgs_q_int("GL_BLEND_EQUATION_ALPHA", GL_BLEND_EQUATION_ALPHA, 1, BGS_NO_UNIT);

    bgs_q_int("GL_COLOR_WRITEMASK", GL_COLOR_WRITEMASK, 4, BGS_NO_UNIT);
    BGS_ENA(GL_DITHER, BGS_NO_UNIT);
    bgs_q_int("GL_SHADE_MODEL", GL_SHADE_MODEL, 1, BGS_NO_UNIT);
    BGS_ENA(GL_CULL_FACE, BGS_NO_UNIT);
    bgs_q_int("GL_CULL_FACE_MODE", GL_CULL_FACE_MODE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_FRONT_FACE", GL_FRONT_FACE, 1, BGS_NO_UNIT);
    BGS_ENA(GL_ALPHA_TEST, BGS_NO_UNIT);
    bgs_q_int("GL_ALPHA_TEST_FUNC", GL_ALPHA_TEST_FUNC, 1, BGS_NO_UNIT);
    bgs_q_flt("GL_ALPHA_TEST_REF", GL_ALPHA_TEST_REF, 1, BGS_NO_UNIT);
    BGS_ENA(GL_SCISSOR_TEST, BGS_NO_UNIT);
    bgs_q_int("GL_SCISSOR_BOX", GL_SCISSOR_BOX, 4, BGS_NO_UNIT);
    BGS_ENA(GL_POLYGON_OFFSET_FILL, BGS_NO_UNIT);
    bgs_q_flt("GL_POLYGON_OFFSET_FACTOR", GL_POLYGON_OFFSET_FACTOR, 1, BGS_NO_UNIT);
    bgs_q_flt("GL_POLYGON_OFFSET_UNITS", GL_POLYGON_OFFSET_UNITS, 1, BGS_NO_UNIT);
    BGS_ENA(GL_LIGHTING, BGS_NO_UNIT);
    BGS_ENA(GL_COLOR_MATERIAL, BGS_NO_UNIT);

    bgs_q_int("GL_SAMPLE_BUFFERS", GL_SAMPLE_BUFFERS, 1, BGS_NO_UNIT);
    bgs_q_int("GL_SAMPLES", GL_SAMPLES, 1, BGS_NO_UNIT);
    BGS_ENA(GL_MULTISAMPLE, BGS_NO_UNIT);
    BGS_ENA(GL_SAMPLE_ALPHA_TO_COVERAGE, BGS_NO_UNIT);
    BGS_ENA(GL_SAMPLE_ALPHA_TO_ONE, BGS_NO_UNIT);
    BGS_ENA(GL_SAMPLE_COVERAGE, BGS_NO_UNIT);
    bgs_q_flt("GL_SAMPLE_COVERAGE_VALUE", GL_SAMPLE_COVERAGE_VALUE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_SAMPLE_COVERAGE_INVERT", GL_SAMPLE_COVERAGE_INVERT, 1, BGS_NO_UNIT);

    bgs_q_int("GL_UNPACK_ALIGNMENT", GL_UNPACK_ALIGNMENT, 1, BGS_NO_UNIT);
    bgs_q_int("GL_PACK_ALIGNMENT", GL_PACK_ALIGNMENT, 1, BGS_NO_UNIT);
}

/* THE CLIENT ARRAYS THAT NEED NO SELECTOR. Enable, size, type, stride, host
 * pointer and buffer binding for each, because "which array a draw reads" is
 * all six and not the enable alone. */
static void bgs_rows_arrays(void)
{
    BGS_ENA(GL_VERTEX_ARRAY, BGS_NO_UNIT);
    bgs_q_int("GL_VERTEX_ARRAY_SIZE", GL_VERTEX_ARRAY_SIZE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_VERTEX_ARRAY_TYPE", GL_VERTEX_ARRAY_TYPE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_VERTEX_ARRAY_STRIDE", GL_VERTEX_ARRAY_STRIDE, 1, BGS_NO_UNIT);
    bgs_q_ptr("GL_VERTEX_ARRAY_POINTER", GL_VERTEX_ARRAY_POINTER, BGS_NO_UNIT);
    bgs_q_int("GL_VERTEX_ARRAY_BUFFER_BINDING", GL_VERTEX_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);

    BGS_ENA(GL_COLOR_ARRAY, BGS_NO_UNIT);
    bgs_q_int("GL_COLOR_ARRAY_SIZE", GL_COLOR_ARRAY_SIZE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_COLOR_ARRAY_TYPE", GL_COLOR_ARRAY_TYPE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_COLOR_ARRAY_STRIDE", GL_COLOR_ARRAY_STRIDE, 1, BGS_NO_UNIT);
    bgs_q_ptr("GL_COLOR_ARRAY_POINTER", GL_COLOR_ARRAY_POINTER, BGS_NO_UNIT);
    bgs_q_int("GL_COLOR_ARRAY_BUFFER_BINDING", GL_COLOR_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);

    BGS_ENA(GL_NORMAL_ARRAY, BGS_NO_UNIT);
    bgs_q_int("GL_NORMAL_ARRAY_TYPE", GL_NORMAL_ARRAY_TYPE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_NORMAL_ARRAY_STRIDE", GL_NORMAL_ARRAY_STRIDE, 1, BGS_NO_UNIT);
    bgs_q_ptr("GL_NORMAL_ARRAY_POINTER", GL_NORMAL_ARRAY_POINTER, BGS_NO_UNIT);
    bgs_q_int("GL_NORMAL_ARRAY_BUFFER_BINDING", GL_NORMAL_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);

    BGS_ENA(GL_INDEX_ARRAY, BGS_NO_UNIT);
    bgs_q_int("GL_INDEX_ARRAY_TYPE", GL_INDEX_ARRAY_TYPE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_INDEX_ARRAY_STRIDE", GL_INDEX_ARRAY_STRIDE, 1, BGS_NO_UNIT);
    bgs_q_ptr("GL_INDEX_ARRAY_POINTER", GL_INDEX_ARRAY_POINTER, BGS_NO_UNIT);
    bgs_q_int("GL_INDEX_ARRAY_BUFFER_BINDING", GL_INDEX_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);

    BGS_ENA(GL_EDGE_FLAG_ARRAY, BGS_NO_UNIT);
    bgs_q_int("GL_EDGE_FLAG_ARRAY_STRIDE", GL_EDGE_FLAG_ARRAY_STRIDE, 1, BGS_NO_UNIT);
    bgs_q_ptr("GL_EDGE_FLAG_ARRAY_POINTER", GL_EDGE_FLAG_ARRAY_POINTER, BGS_NO_UNIT);
    bgs_q_int("GL_EDGE_FLAG_ARRAY_BUFFER_BINDING", GL_EDGE_FLAG_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);

    BGS_ENA(GL_SECONDARY_COLOR_ARRAY, BGS_NO_UNIT);
    bgs_q_int("GL_SECONDARY_COLOR_ARRAY_SIZE", GL_SECONDARY_COLOR_ARRAY_SIZE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_SECONDARY_COLOR_ARRAY_TYPE", GL_SECONDARY_COLOR_ARRAY_TYPE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_SECONDARY_COLOR_ARRAY_STRIDE", GL_SECONDARY_COLOR_ARRAY_STRIDE, 1, BGS_NO_UNIT);
    bgs_q_ptr("GL_SECONDARY_COLOR_ARRAY_POINTER", GL_SECONDARY_COLOR_ARRAY_POINTER, BGS_NO_UNIT);
    bgs_q_int("GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING", GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);

    BGS_ENA(GL_FOG_COORD_ARRAY, BGS_NO_UNIT);
    bgs_q_int("GL_FOG_COORD_ARRAY_TYPE", GL_FOG_COORD_ARRAY_TYPE, 1, BGS_NO_UNIT);
    bgs_q_int("GL_FOG_COORD_ARRAY_STRIDE", GL_FOG_COORD_ARRAY_STRIDE, 1, BGS_NO_UNIT);
    bgs_q_ptr("GL_FOG_COORD_ARRAY_POINTER", GL_FOG_COORD_ARRAY_POINTER, BGS_NO_UNIT);
    bgs_q_int("GL_FOG_COORD_ARRAY_BUFFER_BINDING", GL_FOG_COORD_ARRAY_BUFFER_BINDING, 1, BGS_NO_UNIT);
}

/* THE CLIENT COORDINATE-SET DOMAIN. glClientActiveTexture selects among
 * coordinate SETS, bounded by GL_MAX_TEXTURE_COORDS, and the six answers below
 * are per set. This is a DIFFERENT domain from the server unit sweep further
 * down, and the two are decided and bounded separately because the OpenGL 2.1
 * specification allows the conventional unit count to be the smaller number. */
static void bgs_rows_coord_sets(struct bgs_domain *d)
{
    GLint u;

    if (!d->ok) return;
    for (u = 0; u < d->width; u++) {
        p_ClientActiveTexture((GLenum) (GL_TEXTURE0 + u));
        d->moved = 1;
        BGS_ENA(GL_TEXTURE_COORD_ARRAY, (int) u);
        bgs_q_int("GL_TEXTURE_COORD_ARRAY_SIZE", GL_TEXTURE_COORD_ARRAY_SIZE, 1, (int) u);
        bgs_q_int("GL_TEXTURE_COORD_ARRAY_TYPE", GL_TEXTURE_COORD_ARRAY_TYPE, 1, (int) u);
        bgs_q_int("GL_TEXTURE_COORD_ARRAY_STRIDE", GL_TEXTURE_COORD_ARRAY_STRIDE, 1, (int) u);
        bgs_q_ptr("GL_TEXTURE_COORD_ARRAY_POINTER", GL_TEXTURE_COORD_ARRAY_POINTER, (int) u);
        bgs_q_int("GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING", GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING, 1, (int) u);
    }
}

/* THE SERVER FIXED-FUNCTION UNIT DOMAIN. glActiveTexture selects among the
 * conventional units, bounded by GL_MAX_TEXTURE_UNITS, and these three answers
 * are per unit: the CURRENT texture coordinates, the unit's texture matrix and
 * whether its two-dimensional target is enabled. */
static void bgs_rows_units(struct bgs_domain *d)
{
    GLint u;

    if (!d->ok) return;
    for (u = 0; u < d->width; u++) {
        p_ActiveTexture((GLenum) (GL_TEXTURE0 + u));
        d->moved = 1;
        bgs_q_flt("GL_CURRENT_TEXTURE_COORDS", GL_CURRENT_TEXTURE_COORDS, 4, (int) u);
        bgs_q_flt("GL_TEXTURE_MATRIX", GL_TEXTURE_MATRIX, 16, (int) u);
        BGS_ENA(GL_TEXTURE_2D, (int) u);
    }
}

/* THE HOST FLOATING-POINT WORDS, AND THE TIMING IS THE POINT.
 *
 * AT ENTRY MEANS BEFORE THIS MODULE HAS DONE ANYTHING AT ALL. These two words
 * are read as the FIRST act of a snapshot: before the resolver is called,
 * before the first GL query, before any selector moves. Reading them at the end
 * - which is what the first implementation did - described the state AFTER this
 * probe's own driver calls, and a driver entry point is entitled to leave the
 * rounding mode or the x87 precision control changed. The record would then have
 * attributed the probe's own disturbance to the labelled moment, which is the
 * opposite of what the moment is for.
 *
 * THE POST-QUERY PAIR IS KEPT, SEPARATELY LABELLED, AND NEVER OVERWRITES THE
 * ENTRY PAIR. A difference between the two is an observation about what the
 * calls in between did; collapsing them into one pair of rows would throw that
 * away and leave the surviving pair ambiguous about which it was.
 *
 * NOTHING IS WRITTEN, AT EITHER POINT. No fldcw, no ldmxcsr, no _controlfp and
 * no fesetround appears anywhere in this module: changing the mode in order to
 * observe it would change the frame being observed, and a probe that repaired an
 * FP mode would be altering the arithmetic the record exists to describe. */
static void bgs_rows_host_entry(void)
{
#ifdef BGSTATE_HAVE_MXCSR
    bgs_row_host("HOST_MXCSR@ENTRY", bgs_mxcsr(), 1);
#else
    bgs_row_host("HOST_MXCSR@ENTRY", 0u, 0);
#endif
#ifdef BGSTATE_HAVE_X87
    bgs_row_host("HOST_X87_CONTROL_WORD@ENTRY", bgs_x87cw(), 1);
#else
    bgs_row_host("HOST_X87_CONTROL_WORD@ENTRY", 0u, 0);
#endif
}

static void bgs_rows_host_after(void)
{
#ifdef BGSTATE_HAVE_MXCSR
    bgs_row_host("HOST_MXCSR@AFTER_QUERIES", bgs_mxcsr(), 1);
#else
    bgs_row_host("HOST_MXCSR@AFTER_QUERIES", 0u, 0);
#endif
#ifdef BGSTATE_HAVE_X87
    bgs_row_host("HOST_X87_CONTROL_WORD@AFTER_QUERIES", bgs_x87cw(), 1);
#else
    bgs_row_host("HOST_X87_CONTROL_WORD@AFTER_QUERIES", 0u, 0);
#endif
}

/* ONE PHASE, OBSERVED. Returns the snapshot's index, or -1 when the bounded
 * store is full - which is a fact the record states rather than a silent drop.
 *
 * THE ENTRY POINTS ARE RESOLVED HERE AND NOT CACHED ACROSS SNAPSHOTS. A cached
 * function pointer belongs to the context that answered for it, and this module
 * has no context hook to invalidate one; resolving per snapshot costs at most
 * two resolver calls on the armed path and nothing at all on an off launch. */
static int bgs_snapshot(int phase, int producer)
{
    struct bgs_snap *s;
    int idx;

    if (g_snap_n >= BGSTATE_SNAPS) return -1;
    idx = g_snap_n++;
    s = &g_snap[idx];
    memset(s, 0, sizeof *s);
    s->used = 1;
    s->phase = phase;
    s->producer = producer;
    g_fill = s;
    bgs_domain_init(&s->client);
    bgs_domain_init(&s->server);

    /* THE HOST FLOATING-POINT WORDS FIRST, BEFORE THE RESOLVER AND BEFORE THE
     * FIRST GL CALL. See bgs_rows_host_entry for why the order is the evidence. */
    bgs_rows_host_entry();

    p_ActiveTexture = (SL_BGS_ACTIVETEXTURE) sl_gl_proc("glActiveTexture");
    p_ClientActiveTexture =
        (SL_BGS_CLIENTACTIVETEXTURE) sl_gl_proc("glClientActiveTexture");

    bgs_rows_global();
    bgs_rows_arrays();

    /* THE LATCH IS CONSULTED BEFORE EITHER DOMAIN IS TOUCHED, which is what
     * makes the halt outlive the snapshot that raised it. An earlier phase whose
     * restore went unproved left a selector in an unknown state, and deciding,
     * sweeping and re-writing both selectors here would be this module
     * disturbing the draw again - several more times, once per remaining phase -
     * while reporting each sweep as if it were sound.
     *
     * The rows above and the host words below are unaffected: they read and
     * write nothing. */
    if (g_halt) {
        s->halted = 1;
        s->client.why = g_halt_why;
        s->server.why = g_halt_why;
        g_c.domain_refusals += 2u;
    } else {
        /* THE CLIENT DOMAIN FIRST, THEN THE SERVER DOMAIN, each decided, swept
         * and restored before the other is touched. Interleaving them would
         * leave two selectors moved at once for no gain and would make a refusal
         * in the second one's decision happen with the first one's already in
         * progress. */
        bgs_domain_decide(&s->client, GL_MAX_TEXTURE_COORDS,
                          GL_CLIENT_ACTIVE_TEXTURE,
                          p_ClientActiveTexture != NULL,
                          "glClientActiveTexture does not resolve, so the per-set "
                          "client arrays cannot be accounted for");
        bgs_rows_coord_sets(&s->client);
        bgs_domain_restore(&s->client, GL_CLIENT_ACTIVE_TEXTURE, 1);

        if (g_halt) {
            /* THIS snapshot's client restore is the one that just failed. The
             * server domain is refused rather than swept, for the same reason
             * every later phase's will be. */
            s->halted = 1;
            s->server.why = g_halt_why;
            g_c.domain_refusals++;
        } else {
            bgs_domain_decide(&s->server, GL_MAX_TEXTURE_UNITS, GL_ACTIVE_TEXTURE,
                              p_ActiveTexture != NULL,
                              "glActiveTexture does not resolve, so the per-unit "
                              "server state cannot be accounted for");
            bgs_rows_units(&s->server);
            bgs_domain_restore(&s->server, GL_ACTIVE_TEXTURE, 0);
            /* AND THE SERVER DOMAIN'S OWN FAILURE HALTS TOO. The first
             * implementation raised the halt only on the client branch, so a
             * server restore that went unproved left every later phase free to
             * sweep both selectors again. */
            if (g_halt) s->halted = 1;
        }
    }

    bgs_rows_host_after();

    g_fill = NULL;
    g_c.phases++;
    return idx;
}

/* ---- the producer ledger ------------------------------------------------ */

static int bgs_key_eq(const struct bgs_event *e,
                      const struct sl_bgstate_producer *p)
{
    return e->used && e->slot == p->slot && e->gen == p->gen
           && e->dmode == p->dmode && e->k1bits == p->k1bits
           && e->k2bits == p->k2bits;
}

static int bgs_ev_find(const struct sl_bgstate_producer *p)
{
    int i;
    for (i = 0; i < BGSTATE_LEDGER; i++)
        if (bgs_key_eq(&g_ev[i], p)) return i;
    return -1;
}

void sl_bgstate_pre_setup(const struct sl_bgstate_producer *p)
{
    struct bgs_event *e;
    int i = -1, k;

    if (!sl_bgstate_armed() || !g_walk_armed || g_done || p == NULL) return;

    for (k = 0; k < BGSTATE_LEDGER; k++)
        if (!g_ev[k].used) { i = k; break; }
    g_c.producers++;
    if (i < 0) {
        /* THE LEDGER IS FULL, so this setup gets no identity and the draws it
         * serves can only say that their producer was not observed. Dropping it
         * is bounded behaviour; inventing a slot for it would make a later
         * cache hit resolve to the wrong event. */
        g_c.producers_over++;
        g_ev_cur = -1;
        g_link = -1;
        g_link_kind = BGS_LINK_UNOBSERVED;
        return;
    }
    e = &g_ev[i];
    memset(e, 0, sizeof *e);
    e->used = 1;
    e->id = ++g_ev_n;
    e->slot = p->slot;
    e->gen = p->gen;
    e->dmode = p->dmode;
    e->k1bits = p->k1bits;
    e->k2bits = p->k2bits;
    e->nverts = p->nverts;
    e->state_only = p->state_only;
    e->mesh_ordinal = g_mesh_ordinal;
    e->s_pre = e->s_draw = e->s_restore = -1;
    g_ev_cur = i;
    g_link = i;
    g_link_kind = BGS_LINK_FRESH;

    if (e->id <= (unsigned) BGSTATE_PRODUCERS) {
        e->s_pre = bgs_snapshot(1, i);
        e->observed = (e->s_pre >= 0);
        if (e->observed) g_c.producers_bound++;
    } else {
        g_c.producers_over++;
    }
}

void sl_bgstate_post_draw_raw(const struct sl_bgstate_producer *p)
{
    if (!sl_bgstate_armed() || !g_walk_armed || g_done || p == NULL) return;
    if (g_ev_cur < 0 || !bgs_key_eq(&g_ev[g_ev_cur], p)) return;
    if (!g_ev[g_ev_cur].observed) return;
    g_ev[g_ev_cur].s_draw = bgs_snapshot(2, g_ev_cur);
}

void sl_bgstate_post_restore(const struct sl_bgstate_producer *p)
{
    if (!sl_bgstate_armed() || !g_walk_armed || g_done || p == NULL) return;
    if (g_ev_cur < 0 || !bgs_key_eq(&g_ev[g_ev_cur], p)) return;
    if (!g_ev[g_ev_cur].observed) return;
    g_ev[g_ev_cur].s_restore = bgs_snapshot(3, g_ev_cur);
}

/* A REUSED SLOT IS NEVER A FRESHLY EXECUTED PRODUCER. The hit is resolved to
 * the event whose key it matches, and when no observed event carries that key -
 * it was filled before this walk was armed, or past the ledger - the link says
 * UNOBSERVED and the record is incomplete. No snapshot is taken: there is no
 * setup here to observe, and taking one would put a phase in the record that
 * never happened. */
void sl_bgstate_reuse(const struct sl_bgstate_producer *p)
{
    int i;

    if (!sl_bgstate_armed() || !g_walk_armed || g_done || p == NULL) return;
    i = bgs_ev_find(p);
    if (i < 0) {
        g_c.reuse_unlinked++;
        g_link = -1;
        g_link_kind = BGS_LINK_UNOBSERVED;
        return;
    }
    g_ev[i].reuse_hits++;
    g_c.reuse_links++;
    g_link = i;
    g_link_kind = BGS_LINK_REUSED;
}

/* ---- the walk, the mesh and the selection ------------------------------- */

static int bgs_match(unsigned want, unsigned have)
{
    return want == ~0u || want == have;
}

static int bgs_item_match(const char *item, unsigned bytes)
{
    char buf[256];
    unsigned n;

    if (g_w_item[0] == '\0') return 1;
    if (item == NULL) return 0;
    n = bytes < (unsigned) (sizeof buf - 1u) ? bytes : (unsigned) (sizeof buf - 1u);
    memcpy(buf, item, n);
    buf[n] = '\0';
    return strstr(buf, g_w_item) != NULL;
}

void sl_bgstate_begin_walk(unsigned read, unsigned dl_frame, unsigned cloud)
{
    if (!sl_bgstate_armed()) return;
    g_c.walks++;
    /* THE PER-WALK STATE IS DROPPED WHETHER OR NOT THIS WALK MATCHES, which is
     * what stops a producer from one frame being linked to a draw in another.
     * The ledger is per walk because a slot key carries a walk generation: an
     * event from a previous walk can never serve a draw in this one. */
    memset(g_ev, 0, sizeof g_ev);
    g_ev_n = 0u;
    g_ev_cur = -1;
    g_link = -1;
    g_link_kind = BGS_LINK_NONE;
    g_mesh_ordinal = 0u;
    g_sel_live = 0;
    if (g_done) { g_walk_armed = 0; return; }
    g_walk_armed = bgs_match(g_w_read, read) && bgs_match(g_w_frame, dl_frame)
                   && bgs_match(g_w_cloud, cloud);
    if (g_walk_armed) {
        g_c.walk_match++;
        /* The snapshot store is per RECORD, not per walk, and the record is one
         * draw's. A matched walk that selected no mesh leaves its snapshots
         * behind, so the store is reset at the start of each matched walk -
         * otherwise the first matched walk's producers would fill it and the
         * walk that actually carries the selected draw would find it full. */
        g_snap_n = 0;
    }
}

void sl_bgstate_mesh_begin(void)
{
    if (!sl_bgstate_armed()) return;
    g_sel_live = 0;
    g_link = -1;
    g_link_kind = BGS_LINK_NONE;
    g_ev_cur = -1;
    if (g_walk_armed) g_mesh_ordinal++;
}

int sl_bgstate_select(const struct sl_bgstate_site *s)
{
    if (!sl_bgstate_armed() || !g_walk_armed || s == NULL) return 0;
    g_c.mesh_offers++;
    if (g_done) { g_c.late_offers++; return 0; }
    if (!bgs_match(g_w_mesh, s->mesh) || !bgs_match(g_w_mat, s->material)
        || (g_w_dmode != -2 && g_w_dmode != s->dmode)
        || !bgs_item_match(s->item, s->item_bytes)) {
        g_c.ident_miss++;
        return 0;
    }
    g_c.selected++;
    g_site = *s;
    g_sel_live = 1;
    return 1;
}

/* ---- the record's digest ------------------------------------------------ *
 *
 * SHA-256 over the payload AS IT IS WRITTEN. The sidecar states the digest of
 * the bytes that reached the file, so it is computed from the same buffer the
 * write consumed rather than from a second pass over the file - which could
 * read back something the write did not produce. */
struct bgs_sha {
    unsigned h[8];
    unsigned char buf[64];
    size_t n;
    unsigned long long bits;
};

static const unsigned bgs_sha_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static unsigned bgs_ror(unsigned x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void bgs_sha_block(struct bgs_sha *c, const unsigned char *p)
{
    unsigned w[64], a, b, cc, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((unsigned) p[i * 4] << 24) | ((unsigned) p[i * 4 + 1] << 16)
             | ((unsigned) p[i * 4 + 2] << 8) | (unsigned) p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        const unsigned s0 = bgs_ror(w[i - 15], 7) ^ bgs_ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const unsigned s1 = bgs_ror(w[i - 2], 17) ^ bgs_ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6]; h = c->h[7];
    for (i = 0; i < 64; i++) {
        const unsigned S1 = bgs_ror(e, 6) ^ bgs_ror(e, 11) ^ bgs_ror(e, 25);
        const unsigned ch = (e & f) ^ ((~e) & g);
        const unsigned S0 = bgs_ror(a, 2) ^ bgs_ror(a, 13) ^ bgs_ror(a, 22);
        const unsigned mj = (a & b) ^ (a & cc) ^ (b & cc);
        t1 = h + S1 + ch + bgs_sha_k[i] + w[i];
        t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

static void bgs_sha_init(struct bgs_sha *c)
{
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u; c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->n = 0u;
    c->bits = 0ull;
}

static void bgs_sha_feed(struct bgs_sha *c, const void *data, size_t bytes)
{
    const unsigned char *p = (const unsigned char *) data;
    size_t i;

    c->bits += (unsigned long long) bytes * 8ull;
    for (i = 0u; i < bytes; i++) {
        c->buf[c->n++] = p[i];
        if (c->n == 64u) { bgs_sha_block(c, c->buf); c->n = 0u; }
    }
}

static void bgs_sha_hex(struct bgs_sha *c, char *out65)
{
    static const char hx[] = "0123456789abcdef";
    unsigned char tail[72];
    const unsigned long long bits = c->bits;
    size_t pad;
    int i;

    pad = (c->n < 56u) ? (56u - c->n) : (120u - c->n);
    memset(tail, 0, sizeof tail);
    tail[0] = 0x80u;
    bgs_sha_feed(c, tail, pad);
    c->bits = bits;                 /* the padding is not message length */
    for (i = 7; i >= 0; i--) tail[7 - i] = (unsigned char) (bits >> (i * 8));
    for (i = 0; i < 8; i++) c->buf[c->n++] = tail[i];
    bgs_sha_block(c, c->buf);
    c->n = 0u;
    for (i = 0; i < 8; i++) {
        int k;
        for (k = 0; k < 4; k++) {
            const unsigned char byte = (unsigned char) (c->h[i] >> ((3 - k) * 8));
            out65[i * 8 + k * 2]     = hx[(byte >> 4) & 0xFu];
            out65[i * 8 + k * 2 + 1] = hx[byte & 0xFu];
        }
    }
    out65[64] = '\0';
}

static void bgs_hex32(const unsigned char *raw, char *out65)
{
    static const char hx[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; i++) {
        out65[i * 2]     = hx[(raw[i] >> 4) & 0xFu];
        out65[i * 2 + 1] = hx[raw[i] & 0xFu];
    }
    out65[64] = '\0';
}

/* ---- the output files --------------------------------------------------- */

static const char *bgs_dir(void)
{
    const char *d = getenv("SL_BG_GLSTATE_DIR");
    /* An ignored diagnostic directory beside the other background diagnostics'
     * output. The record is derived from the player's own extracted item and is
     * never ours to distribute, so the default inherits that boundary by
     * location as well as by rule. */
    return (d != NULL && *d != '\0') ? d : "bgstateprobe";
}

/* THE PAYLOAD WRITER. One O_CREAT|O_EXCL operation both creates and claims the
 * path, so an existing file is refused BY THE CREATE ITSELF - no byte of it is
 * touched, whoever holds it and whenever it appeared - and there is no probe for
 * a competing writer to race. Every line is fed to the digest and then written,
 * so the sidecar's digest is of exactly the bytes the write consumed.
 *
 * WHATEVER BYTES REACHED THE FILE ARE LEFT THERE. A partial payload with the
 * seal withheld is evidence; a deleted one is nothing. */
struct bgs_out {
    FILE          *f;
    struct bgs_sha sha;
    size_t         bytes;
    const char    *fail;
};

static void bgs_put(struct bgs_out *o, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    int k;

    if (o->fail != NULL) return;
    va_start(ap, fmt);
    k = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t) k >= sizeof line) { o->fail = "incomplete:line-overflow"; return; }
    bgs_sha_feed(&o->sha, line, (size_t) k);
    if (fwrite(line, 1u, (size_t) k, o->f) != (size_t) k) {
        o->fail = "incomplete:short-write";
        return;
    }
    o->bytes += (size_t) k;
}

/* A text body with the characters that would end a quoted field escaped and
 * every other control byte dropped rather than emitted raw. Bounded by a byte
 * count rather than a terminator: the item's path is a fixed array and may not
 * carry one. */
static void bgs_put_text(struct bgs_out *o, const char *s, unsigned bytes)
{
    unsigned i;
    if (s == NULL) return;
    for (i = 0u; i < bytes && s[i] != '\0'; i++) {
        const unsigned char c = (unsigned char) s[i];
        if (c == '"' || c == '\\') bgs_put(o, "\\%c", (int) c);
        else if (c >= 0x20u && c < 0x7Fu) bgs_put(o, "%c", (int) c);
        else bgs_put(o, "?");
    }
}

static const char *bgs_open(struct bgs_out *o, const char *path)
{
    int fd;

    memset(o, 0, sizeof *o);
    bgs_sha_init(&o->sha);
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
    o->f = SL_BGSTATE_FDOPEN(fd, "wb");
    if (o->f == NULL) {
        /* The file now EXISTS and is empty. It is left there rather than
         * removed: create-only means a later run must refuse this path, and an
         * empty file that refuses is a truer record of what happened than a path
         * swept clean. */
        (void) SL_BGSTATE_CLOSE(fd);
        return "incomplete:file-create-failed";
    }
    return NULL;
}

static const char *bgs_close(struct bgs_out *o)
{
    if (o->f == NULL) return "incomplete:file-create-failed";
    if (o->fail != NULL) {
        (void) fflush(o->f);
        (void) fclose(o->f);
        return o->fail;
    }
    if (fflush(o->f) != 0) { (void) fclose(o->f); return "incomplete:flush-failed"; }
    if (fclose(o->f) != 0) return "incomplete:close-failed";
    o->f = NULL;
    return NULL;
}

/* PUBLICATION: AN ATOMIC MOVE THAT REFUSES AN OCCUPIED DESTINATION.
 *
 * The sidecar carries the record's own verdict, and `complete` has to be decided
 * before the first byte of it is composed - so the text can say "complete" and
 * then its own write, flush or close can fail. A reader opening the file would
 * then find a full, parseable, digest-matching object claiming completeness
 * while the runtime reported a failure. The fix is not a better message: the
 * final NAME cannot exist until the bytes behind it are known to be on disk. So
 * the sidecar is written to an exclusive STAGING path, flushed and closed, and
 * only then moved.
 *
 * NO-REPLACE IS THE PLATFORM'S OWN GUARANTEE rather than a check here: the C
 * library's rename fails when the destination exists on this platform, and where
 * rename replaces silently, link() plus unlink() is the no-replace form. A
 * probe-then-rename would reintroduce the window this exists to remove. On
 * failure the staging file STAYS: it is the evidence of what was composed, and
 * the absence of the final name is what withholds the seal. */
static const char *bgs_publish(const char *staging, const char *final_name)
{
    errno = 0;
#if defined(_WIN32) || !defined(SL_BGSTATE_HAVE_LINK)
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

/* ---- the record -------------------------------------------------------- */

static const char *bgs_phase_name(int phase)
{
    switch (phase) {
    case 1:  return "PRE_SETUP";
    case 2:  return "POST_DRAW_RAW";
    case 3:  return "POST_RESTORE";
    case 4:  return "PRE_RASTER";
    default: return "UNKNOWN";
    }
}

/* RETURNED is its own word and is deliberately not OK: it says the call handed
 * this value back and says NOTHING about whether the capability is one the
 * implementation accepts. A reader that treats it as an enable state has been
 * warned by the name. */
static const char *bgs_status_name(int st)
{
    switch (st) {
    case BGS_OK:       return "OK";
    case BGS_PARTIAL:  return "PARTIAL";
    case BGS_RETURNED: return "RETURNED";
    default:           return "UNKNOWN";
    }
}

static const char *bgs_link_name(int kind)
{
    switch (kind) {
    case BGS_LINK_FRESH:      return "FRESH";
    case BGS_LINK_REUSED:     return "REUSED";
    case BGS_LINK_UNOBSERVED: return "UNOBSERVED";
    default:                  return "NONE";
    }
}

static int bgs_host_little_endian(void)
{
    const unsigned one = 1u;
    unsigned char b[4];
    memcpy(b, &one, sizeof b);
    return b[0] == 1u;
}

/* A DOMAIN'S LINE, AND THE STATUS IS NO LONGER JUST "WAS IT SWEPT".
 *
 * Three outcomes, not two. REFUSED means the domain could not be swept at all
 * and no row of it was emitted. SWEPT means it was swept AND the inherited
 * selector was put back and PROVED. UNRESTORED means it was swept and the
 * restore could not be proved - the rows are real evidence and are kept, but
 * the selector state this probe left behind is unknown, so this alone makes the
 * capture INCOMPLETE. `attempted` and `verified` are serialised separately,
 * because "the write was issued" and "the read-back agreed" are what the first
 * implementation conflated. */
static void bgs_put_domain(struct bgs_out *o, const char *which,
                           const struct bgs_domain *d)
{
    const char *status = !d->ok ? "REFUSED" : (d->verified ? "SWEPT" : "UNRESTORED");

    bgs_put(o, "  domain %s %s width %d saved 0x%04X after 0x%04X moved %d "
               "attempted %d verified %d why \"", which, status,
            (int) d->width, (unsigned) (d->saved & 0xFFFF),
            (unsigned) (d->after & 0xFFFF), d->moved, d->attempted,
            d->verified);
    if (d->why != NULL) bgs_put_text(o, d->why, (unsigned) strlen(d->why));
    bgs_put(o, "\" restore_why \"");
    if (d->restore_why != NULL)
        bgs_put_text(o, d->restore_why, (unsigned) strlen(d->restore_why));
    bgs_put(o, "\"\n");
}

static void bgs_put_snap(struct bgs_out *o, const struct bgs_snap *s)
{
    unsigned i, k;

    /* THE HALT MARKER IS PER PHASE, so a reader can see exactly which labelled
     * moments were taken with the traversal already stopped - and therefore
     * which domains were refused by the latch rather than by the context. */
    bgs_put(o, "phase %d %s producer %d halted %d\n", s->phase,
            bgs_phase_name(s->phase),
            s->producer >= 0 ? (int) g_ev[s->producer].id : -1, s->halted);
    bgs_put_domain(o, "client_coord_sets", &s->client);
    bgs_put_domain(o, "server_units", &s->server);
    bgs_put(o, "  rows %u overflow %d\n", s->nrows, s->overflow);
    for (i = 0u; i < s->nrows; i++) {
        const struct bgs_row *r = &s->row[i];
        bgs_put(o, "  q %s unit %d kind %c n %u status %s mask %08X v",
                r->name, (int) r->unit, (int) r->kind, (unsigned) r->n,
                bgs_status_name((int) r->status), r->mask);
        for (k = 0u; k < (unsigned) r->n && k < BGSTATE_VMAX; k++)
            bgs_put(o, " %08X", r->v[k]);
        bgs_put(o, "\n");
    }
    bgs_put(o, "endphase %d\n", s->phase);
}

/* IS THE RECORD COMPLETE? Only when the last phase was taken AND the draw's
 * producer story is whole: either there was no producer at all - which is the
 * truth on the arm where no prepass runs, and is complete FOR THAT ARM - or the
 * link resolved to an event whose three phases were all recorded.
 *
 * AN UNKNOWN ROW DOES NOT MAKE A RECORD INCOMPLETE. A context that will not
 * answer a query has been observed not answering it, and that observation is
 * the record's; the counts of unknown, partial and refused are written beside
 * it so a reader can weigh them. Completeness here is about whether the four
 * MOMENTS and the producer LINK are whole, which is what an interpretation
 * depends on. */
static const char *bgs_completeness(void)
{
    const struct bgs_event *e;
    int k;

    /* A SWEPT SELECTOR WHOSE RESTORE WAS NOT PROVED REFUSES COMPLETENESS, in
     * ANY phase of the capture and before the producer story is even consulted.
     * This is the correction's whole point: a capture whose probe may have left
     * the wrong texture unit selected cannot be read as a clean observation of
     * the moment it claims, however whole its producer link is. The per-unit
     * rows and the partial phases stay in the record - they are what was
     * measured - and the status says why none of it is complete. */
    for (k = 0; k < BGSTATE_SNAPS; k++) {
        if (!g_snap[k].used) continue;
        if (g_snap[k].client.ok && !g_snap[k].client.verified)
            return "incomplete:client-selector-restore-unproved";
        if (g_snap[k].server.ok && !g_snap[k].server.verified)
            return "incomplete:server-selector-restore-unproved";
    }

    /* AND THE LATCH ALONE IS ENOUGH, whichever snapshots survive. The loop above
     * names the specific domain while the snapshot that failed is still in the
     * store, which is the more useful reason; but sl_bgstate_begin_walk resets
     * that store on a new matched walk and does NOT reset the latch, so a
     * capture taken after such a crossing would otherwise find every surviving
     * snapshot refused-but-consistent and read as complete. It is not: a
     * selector this probe left in an unknown state is still unknown. */
    if (g_halt) return "incomplete:selector-traversal-halted-unproved-restore";

    if (g_link_kind == BGS_LINK_NONE) return NULL;
    if (g_link_kind == BGS_LINK_UNOBSERVED || g_link < 0)
        return "incomplete:producer-not-observed";
    e = &g_ev[g_link];
    if (!e->observed || e->s_pre < 0) return "incomplete:producer-pre-setup-missing";
    if (e->s_draw < 0)    return "incomplete:producer-post-draw-missing";
    if (e->s_restore < 0) return "incomplete:producer-post-restore-missing";
    return NULL;
}

static void bgs_run(const unsigned *ix, unsigned count)
{
    char base[512], payload[560], sidecar[560], stage[600];
    char esha[65], ssha[65], psha[65];
    struct bgs_out out;
    struct bgs_out side;
    const char *fail = NULL, *gap;
    const char *dir = bgs_dir();
    unsigned lo = 0u, hi = 0u, i;
    int snap4, k;

    /* BOUNDS BEFORE ANYTHING IS TOUCHED. A count past the ceiling is a defect
     * rather than a bigger record, and the refusal happens before a file is
     * created or an index is read. */
    if (count > BGSTATE_MAX_INDICES) {
        bgs_fail("refused:index-count-past-ceiling");
        return;
    }
    if (ix == NULL) {
        bgs_fail("refused:no-index-pointer");
        return;
    }
    lo = hi = ix[0];
    for (i = 1u; i < count; i++) {
        if (ix[i] < lo) lo = ix[i];
        if (ix[i] > hi) hi = ix[i];
    }

    /* PHASE 4, TAKEN BEFORE THE RECORD IS COMPOSED and therefore before any
     * file exists. The draw this precedes has not happened; composing first and
     * observing afterwards would record the state of a frame one draw further
     * on. */
    snap4 = bgs_snapshot(4, g_link);
    if (snap4 < 0) {
        bgs_fail("refused:phase-store-full");
        return;
    }

    bgs_hex32(g_exe_sha, esha);
    bgs_hex32(g_src_sha, ssha);
    snprintf(base, sizeof base, "%s/bgstate-%u-%u-%u-%u-%u-%d", dir,
             g_site.read, g_site.dl_frame, g_site.cloud, g_site.mesh,
             g_site.material, g_site.dmode);
    snprintf(payload, sizeof payload, "%s.state.txt", base);
    snprintf(sidecar, sizeof sidecar, "%s.json", base);
    snprintf(stage, sizeof stage, "%s.json.staging", base);

    fail = bgs_open(&out, payload);
    if (fail != NULL) { bgs_fail(fail); return; }

    bgs_put(&out, "#bgstate 1\n");
    bgs_put(&out, "# Sightline background renderer-state probe. OBSERVATIONS ONLY:\n");
    bgs_put(&out, "# nothing here is a cause, a fix, a qualification or a timing\n");
    bgs_put(&out, "# measurement. Unwritten and unsupported answers are UNKNOWN, a\n");
    bgs_put(&out, "# partly written answer is PARTIAL with a per-component mask, and a\n");
    bgs_put(&out, "# REFUSED selector domain emits no rows of that domain at all.\n");
    bgs_put(&out, "# Floats are raw bit patterns. Pointers are opaque and were never\n");
    bgs_put(&out, "# dereferenced. glGetError was NEVER called, so no error state was\n");
    bgs_put(&out, "# consumed; no buffer, pixel or framebuffer was read and nothing was\n");
    bgs_put(&out, "# flushed, finished or synchronised.\n");
    bgs_put(&out, "# A kind-E row is an enable state from a DESTINATION query, so an\n");
    bgs_put(&out, "# unwritten or non-boolean answer is UNKNOWN and never a 0 or a 1. A\n");
    bgs_put(&out, "# kind-R row is what a RETURN-ONLY call handed back; its status is\n");
    bgs_put(&out, "# RETURNED and it is NOT evidence that the capability is supported,\n");
    bgs_put(&out, "# because the same false is returned for a capability the\n");
    bgs_put(&out, "# implementation does not accept.\n");
    bgs_put(&out, "# A domain reads SWEPT only when the inherited selector was put back\n");
    bgs_put(&out, "# AND a read-back proved it; UNRESTORED means the rows are real and\n");
    bgs_put(&out, "# the selector this probe left behind is unknown, which alone makes\n");
    bgs_put(&out, "# the capture INCOMPLETE. No selector was ever written twice.\n");
    bgs_put(&out, "# HOST_*@ENTRY was read before this module's first resolver or GL\n");
    bgs_put(&out, "# call; HOST_*@AFTER_QUERIES after them. No FP mode was written.\n");
    bgs_put(&out, "switch SL_BG_GLSTATE\n");
    bgs_put(&out, "selector \"");
    bgs_put_text(&out, g_sel_text, (unsigned) strlen(g_sel_text));
    bgs_put(&out, "\"\n");
    bgs_put(&out, "provenance exe %s\n", esha);
    bgs_put(&out, "provenance src %s\n", ssha);
    bgs_put(&out, "host little_endian %d\n", bgs_host_little_endian());
    bgs_put(&out, "bounds units %d rows %d producers %d ledger %d snaps %d\n",
            BGSTATE_UNITS, BGSTATE_ROWS, BGSTATE_PRODUCERS, BGSTATE_LEDGER,
            BGSTATE_SNAPS);
    bgs_put(&out, "site read %u dl_frame %u cloud %u draw_site_pumped %u\n",
            g_site.read, g_site.dl_frame, g_site.cloud, g_site.pumped);
    bgs_put(&out, "site mesh %u material %u dmode %d nverts %u\n",
            g_site.mesh, g_site.material, g_site.dmode, g_site.nverts);
    {
        unsigned k1b, k2b;
        memcpy(&k1b, &g_site.k1, sizeof k1b);
        memcpy(&k2b, &g_site.k2, sizeof k2b);
        bgs_put(&out, "site k1bits %08X k2bits %08X\n", k1b, k2b);
    }
    bgs_put(&out, "site item \"");
    bgs_put_text(&out, g_site.item, g_site.item_bytes);
    bgs_put(&out, "\"\n");
    bgs_put(&out, "site arm candidate %d pool_source %d cpu_raster %d "
                  "vbo_indices %d\n", g_site.arm_candidate,
            g_site.arm_pool_source, g_site.arm_cpu_raster,
            g_site.arm_vbo_indices);
    bgs_put(&out, "run count %u index_lo %u index_hi %u\n", count, lo, hi);
    bgs_put(&out, "run first");
    for (i = 0u; i < count && i < (unsigned) BGSTATE_IX_SHOWN; i++)
        bgs_put(&out, " %u", ix[i]);
    bgs_put(&out, "\n");
    /* THE TRAVERSAL-HALT LATCH, as one line a reader sees before any phase.
     * `latched 1` means some phase's restore went unproved and EVERY phase from
     * that point on refused both selector domains without writing a selector
     * again. The phase named is the one the FIRST failure happened in. */
    bgs_put(&out, "halt latched %d phase %d domain \"", g_halt, g_halt_phase);
    bgs_put_text(&out, g_halt_domain, (unsigned) strlen(g_halt_domain));
    bgs_put(&out, "\" why \"");
    bgs_put_text(&out, g_halt_why, (unsigned) strlen(g_halt_why));
    bgs_put(&out, "\"\n");
    bgs_put(&out, "link kind %s producer %d\n", bgs_link_name(g_link_kind),
            g_link >= 0 ? (int) g_ev[g_link].id : -1);
    bgs_put(&out, "producers seen %lu bound %lu over %lu reuse_links %lu "
                  "reuse_unlinked %lu\n", g_c.producers, g_c.producers_bound,
            g_c.producers_over, g_c.reuse_links, g_c.reuse_unlinked);
    for (k = 0; k < BGSTATE_LEDGER; k++) {
        const struct bgs_event *e = &g_ev[k];
        if (!e->used) continue;
        bgs_put(&out, "producer %u slot %d gen %u dmode %d k1bits %08X "
                      "k2bits %08X nverts %u state_only %d observed %d "
                      "mesh_ordinal %u reuse_hits %u\n",
                e->id, e->slot, e->gen, e->dmode, e->k1bits, e->k2bits,
                e->nverts, e->state_only, e->observed, e->mesh_ordinal,
                e->reuse_hits);
    }
    for (k = 0; k < BGSTATE_SNAPS; k++)
        if (g_snap[k].used) bgs_put_snap(&out, &g_snap[k]);

    /* THE CONTEXT'S OWN WORDS, bounded and escaped. glGetString is a string
     * query and not a read-back or a synchronisation point; the extension list
     * is deliberately NOT asked for, because it is unbounded and this record
     * must not grow with a driver's vocabulary. */
    {
        static const struct { const char *name; unsigned pname; } ctx[3] = {
            { "GL_VERSION",  (unsigned) GL_VERSION },
            { "GL_VENDOR",   (unsigned) GL_VENDOR },
            { "GL_RENDERER", (unsigned) GL_RENDERER }
        };
        for (k = 0; k < 3; k++) {
            const GLubyte *t = glGetString((GLenum) ctx[k].pname);
            g_c.queries++;
            bgs_put(&out, "context %s \"", ctx[k].name);
            if (t != NULL) bgs_put_text(&out, (const char *) t, 160u);
            bgs_put(&out, "\"\n");
        }
    }

    bgs_put(&out, "counters walks %lu walk_match %lu mesh_offers %lu "
                  "ident_miss %lu selected %lu late_offers %lu phases %lu "
                  "queries %lu rows_unknown %lu rows_partial %lu "
                  "rows_nonboolean %lu domain_refusals %lu "
                  "selector_restore_attempts %lu selector_restores_verified %lu "
                  "selector_restore_failures %lu\n",
            g_c.walks, g_c.walk_match, g_c.mesh_offers, g_c.ident_miss,
            g_c.selected, g_c.late_offers, g_c.phases, g_c.queries,
            g_c.rows_unknown, g_c.rows_partial, g_c.rows_nonboolean,
            g_c.domain_refusals, g_c.selector_restore_attempts,
            g_c.selector_restores, g_c.selector_restore_failures);
    bgs_put(&out, "end\n");

    fail = bgs_close(&out);
    if (fail != NULL) { bgs_fail(fail); return; }
    bgs_sha_hex(&out.sha, psha);

    /* THE SEAL IS COMPOSED ONLY AFTER THE PAYLOAD IS KNOWN TO BE ON DISK, and
     * the verdict it carries is decided here - with the payload closed - rather
     * than promised earlier. */
    gap = bgs_completeness();
    fail = bgs_open(&side, stage);
    if (fail != NULL) { bgs_fail(fail); return; }
    bgs_put(&side, "{\n");
    bgs_put(&side, "  \"format\": \"sightline-bgstate-probe-1\",\n");
    bgs_put(&side, "  \"complete\": %s,\n", gap == NULL ? "true" : "false");
    bgs_put(&side, "  \"status\": \"%s\",\n", gap == NULL ? "complete" : gap);
    bgs_put(&side, "  \"disclaimer\": \"observations only; no cause, fix, "
                   "qualification or timing follows from this record\",\n");
    bgs_put(&side, "  \"selector\": \"");
    bgs_put_text(&side, g_sel_text, (unsigned) strlen(g_sel_text));
    bgs_put(&side, "\",\n");
    bgs_put(&side, "  \"exe_sha256\": \"%s\",\n", esha);
    bgs_put(&side, "  \"src_sha256\": \"%s\",\n", ssha);
    bgs_put(&side, "  \"payload\": \"bgstate-%u-%u-%u-%u-%u-%d.state.txt\",\n",
            g_site.read, g_site.dl_frame, g_site.cloud, g_site.mesh,
            g_site.material, g_site.dmode);
    bgs_put(&side, "  \"payload_bytes\": %lu,\n", (unsigned long) out.bytes);
    bgs_put(&side, "  \"payload_sha256\": \"%s\",\n", psha);
    bgs_put(&side, "  \"glgeterror_called\": false,\n");
    bgs_put(&side, "  \"gpu_readback\": false,\n");
    bgs_put(&side, "  \"link_kind\": \"%s\",\n", bgs_link_name(g_link_kind));
    bgs_put(&side, "  \"phases_recorded\": %lu,\n", g_c.phases);
    bgs_put(&side, "  \"queries\": %lu,\n", g_c.queries);
    bgs_put(&side, "  \"rows_unknown\": %lu,\n", g_c.rows_unknown);
    bgs_put(&side, "  \"rows_partial\": %lu,\n", g_c.rows_partial);
    bgs_put(&side, "  \"rows_nonboolean\": %lu,\n", g_c.rows_nonboolean);
    bgs_put(&side, "  \"domain_refusals\": %lu,\n", g_c.domain_refusals);
    bgs_put(&side, "  \"selector_restore_attempts\": %lu,\n",
            g_c.selector_restore_attempts);
    bgs_put(&side, "  \"selector_restores_verified\": %lu,\n",
            g_c.selector_restores);
    /* A SEALED COMPLETE RECORD CAN NEVER CARRY A RESTORE FAILURE, because an
     * unproved restore is itself an incompleteness. The number is written so a
     * reader does not have to take that on trust. */
    bgs_put(&side, "  \"selector_restore_failures\": %lu,\n",
            g_c.selector_restore_failures);
    /* THE LATCH, IN THE SEAL. A record whose traversal was halted can never be
     * complete, so a reader checking one field learns whether any phase after
     * the first failure wrote a selector - none did. */
    bgs_put(&side, "  \"traversal_halted\": %s,\n", g_halt ? "true" : "false");
    bgs_put(&side, "  \"halt_phase\": %d,\n", g_halt_phase);
    bgs_put(&side, "  \"halt_domain\": \"");
    bgs_put_text(&side, g_halt_domain, (unsigned) strlen(g_halt_domain));
    bgs_put(&side, "\"\n");
    bgs_put(&side, "}\n");
    fail = bgs_close(&side);
    if (fail != NULL) { bgs_fail(fail); return; }

    fail = bgs_publish(stage, sidecar);
    if (fail != NULL) { bgs_fail(fail); return; }

    if (gap != NULL) { bgs_fail(gap); return; }
    g_state = 1;
    g_c.completed++;
    snprintf(g_status, sizeof g_status, "complete");
    fprintf(stderr, "#95 SL_BG_GLSTATE: a COMPLETE renderer-state record was "
                    "sealed at %s (payload %lu bytes, sha256 %s). It records "
                    "state and concludes nothing\n", sidecar,
            (unsigned long) out.bytes, psha);
}

void sl_bgstate_pre_raster(const unsigned *indices, unsigned count)
{
    if (!sl_bgstate_armed() || !g_sel_live || count == 0u) return;
    if (g_done) { g_c.late_offers++; return; }
    /* THE ATTEMPT IS SPENT BEFORE ANYTHING IS TOUCHED - before a query is made,
     * a file is created or an index is read - so a failure cannot retry at the
     * next run of this mesh, the next mesh or the next frame. */
    g_done = 1;
    g_c.captures++;
    bgs_run(indices, count);
}

#endif /* !__sgi */
