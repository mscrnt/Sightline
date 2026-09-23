/**
 * Display-list interpreter for the native port.
 *
 * The game builds its per-frame DL in RDRAM with 32-bit stores, so natively
 * the words are already host-order: the opcode is the top byte of word 0.
 * (Model DLs from ROM are the opposite case - sl_gfx_dl_swap rewrites those at
 * load. Confusing the two caused the collision-DL crash.)
 *
 * ---- B-012: BOTH word orders reach this interpreter -----------------------
 *
 * Not every ROM-resident list gets rewritten. `lvlRender` publishes the font
 * DL segment as segment 1 (src/game/lv.c:691), and that segment arrives by
 * `romCopy(ptr_font_DL, &_fontdlSegmentRomStart, size)` (lv.c:268) with no
 * swap anywhere: a tree-wide `grep -rn ptr_font_DL src/` returns exactly five
 * hits - the extern, the definition, the mempAlloc, the romCopy and the
 * gSPSegment - and none of them is a swap. Measured side by side in one frame:
 *
 *   segment 1 @c930a440  021400ba 00000000 011700ba 00008000 ...  opcode byte 0
 *   segment 5 @c84b7890  e7000000 00000000 ba001001 00010000 ...  opcode byte 3
 *
 * Read natively, the font list never meets its own `000000b8` enddl (byte-
 * swapped `b8000000`), so the walk ran off the end of the list, through the
 * rest of the font segment, and hit the pair `06000600 2103000a`. Decoded as
 * G_DL that names segment 1 + 0x03000a = c933a40a - misaligned by two, and
 * 22610 words of zeroed memory beyond the copied region. That single command
 * was 84.5% of all "unknown" opcodes per frame.
 *
 * Two theories were tested against this and are wrong, recorded so they are
 * not retried: the MW_SEGMENT index arithmetic is CORRECT (`bc000406
 * c930a400` in the frame list decodes as offset 4 / index 06 / segment 1, and
 * the F3DEX2 `hi06` form was observed exactly 0 times in 93 movewords), and
 * the walk never overran the `end` bound (`end-stops=0`).
 *
 * The fix is therefore not in seg_resolve: each list is sniffed for its own
 * word order on entry (list_swapped) and read through that lens. Data the
 * list points at - vertices - carries the same order as the list that names
 * it, which is what vtx_load does.
 *
 * Command set is ucode05 per Zoinkity's ucode05.txt, NOT stock F3DEX:
 *   01 matrix   04 vertex   06 displaylist
 *   B1 GE_TRI4 (Rare's, in F3DEX's G_TRI2 slot)
 *   BC moveword (segments)  BD popmatrix  BF tri1  B8 enddl
 *
 * gImmp21 layout: w0 = (op<<24) | (offset<<8) | index, w1 = data.
 * gSPSegment => moveword, index G_MW_SEGMENT(6), offset segment*4, w1 base.
 * osVirtualToPhysical is identity natively, so segment bases are host
 * pointers and a segment address resolves as seg[base] + (addr & 0xffffff).
 *
 * ---- matrices -------------------------------------------------------------
 *
 * ucode05.txt, "01 rsp_uc05_matrix G_MTX":
 *     gsSPMatrix(m, p) = gsDma1p(G_MTX, m, sizeof(Mtx), p)
 *   so w0 = (01 << 24) | (p << 16) | sizeof(Mtx), w1 = m, and the note's own
 *   table gives the three parameter bits:
 *     xxx0xxxx modelview  mul  nopush      xxx4xxxx modelview  mul  push
 *     xxx1xxxx projection mul  nopush      xxx5xxxx projection mul  push
 *     xxx2xxxx modelview  load nopush      xxx6xxxx modelview  load push
 *     xxx3xxxx projection load nopush      xxx7xxxx projection load push
 *   i.e. bit0 = projection, bit1 = load, bit2 = push. That is the F3D /
 *   non-F3DEX2 assignment, and it is the one this tree compiles: gbi.h defines
 *   G_MTX_PROJECTION 0x04 / G_MTX_PUSH 0x01 only under F3DEX_GBI_2, which is
 *   defined nowhere in the build (tools/native/build.sh DEFS, and a tree-wide
 *   grep finds F3DEX_GBI_2 only inside gbi.h/gs2dex.h's own #ifdefs). Reading
 *   the wrong half of that #ifdef would have swapped projection with push.
 *
 * The length field is what separates a real matrix command from garbage: it is
 * always sizeof(Mtx) == 64. Before that filter the walker reported 11 "matrix"
 * commands per frame with lengths of 186, 10784, 16031, 56688 ...; after it,
 * exactly the two the game actually issues. The walker still decodes a lot of
 * non-DL memory (see the `unknown` counter), so every operand is treated as
 * hostile.
 *
 * Mtx interior layout: the notes are SILENT here - `gedocs search matrix` and
 * `search mtx` both return nothing, and ucode05.txt says only "a 4x4 veiwing
 * matrix". The authority is therefore the SDK header in-tree, include/PR/gbi.h:
 *   "4x4 matrix, fixed point s15.16 format. First 8 words are integer portion
 *    of the 4x4 matrix, Last 8 words are the fraction portion"
 * with src/libultra/gu/mtxutil.c (guMtxF2L/guMtxL2F) showing the packing: word
 * k>>1 of the integer half holds elements k and k+1 in its high and low 16
 * bits, and word 8 + (k>>1) holds their fractions the same way.
 *
 * Word order is NATIVE, established by measurement rather than by argument.
 * The modelview matrix at segment 3 reads, as native u32:
 *   00010000 00000000 00000001 00000000  00000000 00010000 00000000 f0600001
 *   00000000 00000000 00000000 00000000  00000000 00000000 00000000 00000000
 * which decodes to identity with row 3 = (0, 0, -3996, 1) - an unmistakable
 * translate. Byte-swapped it decodes to noise. These matrices are built at
 * runtime by guMtxF2L compiled for the host, so they never pass through
 * sl_gfx_dl_swap; a tree-wide `grep -rn Mtx src/ | grep -i 'swap|SW32|SW16'`
 * comes back empty, i.e. nothing in the tree byte-swaps an Mtx anywhere.
 *
 * Matrix operands arrive in three flavours, all three observed:
 *   03000000  segment address    (top nibble 0, segment in bits 24-27; the
 *                                 addressing ucode05.txt's movemem entry
 *                                 documents as "0f000000 segment /
 *                                 00ffffff address or offset in file")
 *   4847be80  OS_K0_TO_PHYSICAL  (fr.c:716; natively that is ptr - 0x80000000,
 *                                 and 0x4847be80 + 0x80000000 = 0xc847be80,
 *                                 which is 0x40 below the segment-3 base seen
 *                                 in the same frame - the two consecutive
 *                                 dynAllocateMatrix slots)
 *   c8......  raw host pointer   (osVirtualToPhysical, identity natively)
 * Nothing is dereferenced until mincore() says the page is mapped, so a
 * malformed DL costs a rejected candidate rather than the process.
 */
#ifndef __sgi
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
/* No sys/mman.h on Win32. windows.h must also precede <GL/gl.h>, which
 * relies on it for APIENTRY and WINGDIAPI. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <GL/gl.h>
/* B-045. glFogCoordf is GL 1.4; Mesa's <GL/gl.h> declares only through 1.3
 * and leaves this one in <GL/glext.h>, behind GL_GLEXT_PROTOTYPES, which
 * nothing here defines.
 *
 * Without a prototype C89 assumes int(...) and applies DEFAULT ARGUMENT
 * PROMOTION, so the GLfloat was widened to a double and the entry point read
 * the low 32 bits of that double as its coordinate. Those bits are the tail
 * of the mantissa: they change completely for any small change in the value,
 * so every vertex got an unrelated number, and GL clamped it to no fog or
 * full fog. The camera moving a few units re-rolled it - which is exactly the
 * flicker, and also the "the factor saturates" reading, since most garbage
 * floats land outside 0..1.
 *
 * Nothing announced it: the native build compiles with -w, and the one
 * diagnostic that named it (-Wimplicit-function-declaration) was suppressed.
 * It is the only GL entry point in this file that <GL/gl.h> does not declare
 * - glMultiTexCoord2f, glActiveTexture and glBlendColor are all 1.3 or
 * earlier and are declared there - so a local prototype settles it without
 * pulling the whole glext prototype set in. */
#ifndef _WIN32
extern void glFogCoordf(GLfloat coord);
#endif

/* ---- Win32: the post-1.1 entry points, resolved at RUNTIME ---------------
 *
 * B-075. On Windows the two capability gates below - SL_MULTITEX (B-048's
 * TEXEL1 explosion fire) and SL_FOGSTAGE (B-045's fog coordinate, which is
 * what the Facility gas draws through) - both COMPILED OUT ENTIRELY, and the
 * accepted native renderer ran on Windows with two of its features absent.
 *
 * MEASURED, through the preprocessor, in the exact include order this file
 * uses (<windows.h> then <GL/gl.h>), on the toolchain build.ps1 selects
 * (i686-w64-mingw32-gcc 16.2.0, MSYS2 mingw32):
 *
 *     GL_VERSION_1_1     defined
 *     GL_VERSION_1_3     NOT defined      -> SL_MULTITEX off
 *     GL_VERSION_1_4     NOT defined      -> SL_FOGSTAGE off
 *     GL_TEXTURE0/1      NOT defined
 *     GL_FOG_COORD_SRC   NOT defined
 *     APIENTRY           WINAPI  (i.e. __stdcall)
 *
 * and confirmed in the shipped binary's PE IMPORT TABLE, which carried 31 GL
 * names, every one of them GL 1.1: no glActiveTexture, no glMultiTexCoord2f,
 * no glFogCoordf - and no glFogf/glFogi/glFogfv either, because SL_FOGSTAGE
 * had removed their call sites too.
 *
 * This is NOT a driver limitation. The context is a 4.5 compatibility
 * profile and does all of this in hardware. Win32's OpenGL IMPORT SURFACE is
 * frozen at 1.1 - opengl32.dll exports nothing newer - so anything past it
 * must be resolved through the loader, which is what sl_gl_proc already
 * exists to do and what the GL 2.0 shader path already does.
 *
 * APIENTRY IS LOAD-BEARING HERE, and its absence was B-063: these entry
 * points are __stdcall, the CALLEE pops the arguments, and a bare
 * `typedef void (*F)(GLenum)` is cdecl. See the block above the shader
 * typedefs for what that cost once. Anything added here MUST carry APIENTRY.
 *
 * The enum values are glext.h's, read from that header rather than recalled:
 * GL/glext.h:125-126 (GL_TEXTURE0/1) and GL/glext.h:508-509 (the fog pair).
 *
 * Every wrapper is NULL-safe, so a driver that genuinely lacks one degrades
 * to the pre-B-075 behaviour instead of faulting - and post11_ok() reports
 * which way it went rather than failing silently. */
#ifdef _WIN32
#define GL_TEXTURE0        0x84C0
#define GL_TEXTURE1        0x84C1
#define GL_FOG_COORD_SRC   0x8450
#define GL_FOG_COORD       0x8451
#define SL_GL_RUNTIME_POST11 1

/* ---- B-076: the GL 1.3 TEXTURE ENVIRONMENT COMBINE tokens --------------
 *
 * The same defect as B-075, one gate further on, and it is what kept B-051's
 * tinted glass wrong on Windows after B-075 landed.
 *
 * SL_TEXENV_COMBINE is defined only where the header declares GL_COMBINE and
 * its operand tokens. MEASURED through the preprocessor in this file's own
 * include order (<windows.h> then <GL/gl.h>) on the toolchain build.ps1
 * selects (i686-w64-mingw32-gcc, MSYS2 mingw32):
 *
 *     GL_VERSION_1_1   defined
 *     GL_COMBINE       NOT defined      GL_COMBINE_RGB    NOT defined
 *     GL_COMBINE_ALPHA NOT defined      GL_SOURCE0_RGB    NOT defined
 *     GL_PRIMARY_COLOR NOT defined      GL_INTERPOLATE    NOT defined
 *     GL_CONSTANT      NOT defined
 *     GL_COMBINE_ARB / GL_COMBINE_EXT   NOT defined either
 *
 * so SL_TEXENV_COMBINE was FALSE on Windows and every arm of texenv_set fell
 * through to the trailing `glTexEnvi(..., GL_MODULATE)`. The whole of B-051
 * was discarded at the last step: the equation was decoded, simplified and
 * classified correctly, the GL 2.0 alpha program compiled and LINKED - and
 * then the mode that binds it did not exist in the binary.
 *
 * MEASURED IN THE CONSUMED BINARY, 400 windowed frames of facility, before
 * this change: the glass word fc26a004 1f1093fb reached a draw with 90228
 * triangles and classified SHADER, "sl_aeq: alpha program ready" printed,
 * and the requested-vs-installed census read
 *
 *     want=5 calls=30076 -> got0=30076      (the alpha shader)
 *     want=4 calls=60087 -> got0=60087      (AEQ_LERP)
 *     want=2 calls=65631 -> got0=65631      (independent-alpha REPLACE)
 *
 * i.e. NOT ONE request was honoured. Corroborated independently by a per-frame
 * GL_INVALID_OPERATION (0x0502): aeq_apply_env pushes the shader's uniforms
 * unconditionally, and glUniform* with no program current is exactly that
 * error. The sl_aeq report meanwhile said "exact=696165 inexact=0", which is
 * a clean verdict from a path that never drew - the reason the census above
 * measures the INSTALLED mode and not the requested one.
 *
 * THIS IS A HEADER LIMIT, NOT A DRIVER ONE - same as B-075. The context is a
 * 4.5 compatibility profile and does texture-env combine in hardware. Nor
 * does it need the loader: glTexEnvi/glTexEnvfv are GL 1.1 and already in
 * opengl32.dll's import surface, so unlike B-075 there is no entry point to
 * resolve here. Only the ENUM VALUES are missing, and they are read from
 * <GL/glext.h> in this same MSYS2 tree rather than recalled:
 * mingw32/include/GL/glext.h:197-217.
 *
 * Guarded with #ifndef so that a header which does declare them keeps its own
 * definitions and this block becomes inert. */
#ifndef GL_COMBINE
#define GL_COMBINE         0x8570
#endif
#ifndef GL_COMBINE_RGB
#define GL_COMBINE_RGB     0x8571
#endif
#ifndef GL_COMBINE_ALPHA
#define GL_COMBINE_ALPHA   0x8572
#endif
#ifndef GL_INTERPOLATE
#define GL_INTERPOLATE     0x8575
#endif
#ifndef GL_CONSTANT
#define GL_CONSTANT        0x8576
#endif
#ifndef GL_PRIMARY_COLOR
#define GL_PRIMARY_COLOR   0x8577
#endif
#ifndef GL_SOURCE0_RGB
#define GL_SOURCE0_RGB     0x8580
#endif
#ifndef GL_RGB_SCALE
#define GL_RGB_SCALE       0x8573      /* the combine stage's RGB scale (1, 2 or 4) - #63 exposure */
#endif

/* ---- B-078: GL_CLAMP_TO_EDGE, the third instance of the same defect -----
 *
 * B-075 lost whole render paths to a missing capability macro and B-076 lost
 * B-051's alpha equation to missing enum VALUES. This is the same failure a
 * third time, and it is what the owner sees as a grid/stitch pattern over
 * every metal sliding door and every interactable console.
 *
 * tex_wrap() maps the RDP's clamp bit to GL_CLAMP_TO_EDGE. Further down this
 * file a fallback reads
 *
 *     #ifndef GL_CLAMP_TO_EDGE
 *     #define GL_CLAMP_TO_EDGE GL_CLAMP
 *     #endif
 *
 * and on Windows that fallback FIRES. MEASURED through the preprocessor in
 * this file's own include order (<windows.h> then <GL/gl.h>) on the toolchain
 * build.ps1 selects:
 *
 *     GL_VERSION_1_1     DEFINED       <- the controls: both took the DEFINED
 *     GL_REPEAT          DEFINED          branch in the same run, so ABSENT
 *     GL_CLAMP           DEFINED 0x2900   below is a reading and not an
 *     GL_CLAMP_TO_EDGE   ABSENT           empty instrument
 *     GL_MIRRORED_REPEAT ABSENT
 *     GL_TEXTURE_MAX_LEVEL / GL_GENERATE_MIPMAP  ABSENT
 *
 * GL_CLAMP and GL_CLAMP_TO_EDGE are NOT interchangeable. Both clamp the
 * coordinate, but GL_CLAMP's filter footprint at the edge of the image
 * includes the texture BORDER, whose default colour is (0,0,0,0). Under
 * GL_LINEAR - which every texture in this renderer uses - that lays a
 * half-texel dark, zero-alpha fringe around the outside of every clamped
 * image. Rare sets the clamp bit on exactly the odd-sized images B-077
 * catalogued, and a prop model is a grid of quads each sampling one of them,
 * so the fringe lands on every quad boundary at once. That is the pattern.
 *
 * MEASURED IN THE CONSUMED BINARY, windowed Facility replay, 4000 frames,
 * reading back what GL HOLDS rather than what this file asked for:
 *
 *     prop=158 PROP_GAS_PLANT_MET1_DO1, a Facility metal sliding door
 *       32x33 CI8  cms=2 cmt=2 -> INSTALLED s=0x2900 t=0x2900  GL_CLAMP
 *     prop=112 PROP_LABBENCH, the clean control alongside it
 *       32x32 CI4  cms=0 cmt=0 -> INSTALLED s=0x2901 t=0x2901  GL_REPEAT
 *
 * Over the whole census 20 of 54 distinct materials installed GL_CLAMP on at
 * least one axis and 34 installed GL_REPEAT on both, so the clamp reading is
 * a measurement and not an instrument stuck on one answer. After this change
 * the same run reads the same 20/34 split with 0x812F in place of 0x2900 and
 * every GL_REPEAT untouched - and the driver hands 0x812F back, so this was
 * a header limit and never a driver one.
 *
 * No entry point to resolve: glTexParameteri is GL 1.1 and already in
 * opengl32.dll's import surface, and the context is a 4.5 compatibility
 * profile. Only the VALUE was missing. It is read from <GL/glext.h> in this
 * same MSYS2 tree rather than recalled - mingw32/include/GL/glext.h:96 - and
 * #ifndef-guarded so a header that does declare it keeps its own.
 *
 * GL_MIRRORED_REPEAT is deliberately NOT added. tex_wrap has never honoured
 * the mirror bit on any platform, and Facility never sets it: over 1.7M
 * textured triangles the census recorded cms/cmt values of 0 and 2 only,
 * never 1 or 3. Adding it would be an unmeasured behaviour change stapled to
 * a measured one. */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE   0x812F
#endif
#ifndef GL_SOURCE1_RGB
#define GL_SOURCE1_RGB     0x8581
#endif
#ifndef GL_SOURCE0_ALPHA
#define GL_SOURCE0_ALPHA   0x8588
#endif
#ifndef GL_SOURCE1_ALPHA
#define GL_SOURCE1_ALPHA   0x8589
#endif
#ifndef GL_SOURCE2_ALPHA
#define GL_SOURCE2_ALPHA   0x858A
#endif
#ifndef GL_SOURCE2_RGB
#define GL_SOURCE2_RGB     0x8582
#endif
#ifndef GL_OPERAND0_RGB
#define GL_OPERAND0_RGB    0x8590
#endif
#ifndef GL_OPERAND1_RGB
#define GL_OPERAND1_RGB    0x8591
#endif
#ifndef GL_OPERAND2_RGB
#define GL_OPERAND2_RGB    0x8592
#endif
#ifndef GL_OPERAND0_ALPHA
#define GL_OPERAND0_ALPHA  0x8598
#endif
#ifndef GL_OPERAND1_ALPHA
#define GL_OPERAND1_ALPHA  0x8599
#endif
#ifndef GL_OPERAND2_ALPHA
#define GL_OPERAND2_ALPHA  0x859A
#endif
/* B-110. GL_PREVIOUS was never defined ANYWHERE in this file (tree-wide:
 * `grep -n "define GL_PREVIOUS" src/gfx/sl_gfx_dl.c` was empty), and its only
 * users were tex1_apply's B-107 water arm - which therefore could never have
 * compiled on this toolchain and shipped inside an always-false preprocessor
 * branch instead. Same header-token defect as B-075/B-076, one instance
 * further. Value from the same authority as the rest of this block,
 * mingw32/include/GL/glext.h. */
#ifndef GL_PREVIOUS
#define GL_PREVIOUS        0x8578
#endif

typedef void (APIENTRY *SL_PFN_ACTIVETEXTURE)(GLenum);
typedef void (APIENTRY *SL_PFN_MULTITEXCOORD2F)(GLenum, GLfloat, GLfloat);
typedef void (APIENTRY *SL_PFN_FOGCOORDF)(GLfloat);

static SL_PFN_ACTIVETEXTURE   p_ActiveTexture;
static SL_PFN_MULTITEXCOORD2F p_MultiTexCoord2f;
static SL_PFN_FOGCOORDF       p_FogCoordf;

extern void *sl_gl_proc(const char *name);

/* 0 untried, 1 ready, -1 unavailable. Resolved lazily: sl_gl_proc returns
 * NULL until the context exists, and every caller here runs during drawing,
 * so by the first call it does. */
static int sl_post11_state;

static int post11_ok(void)
{
    if (sl_post11_state != 0) return sl_post11_state > 0;
    p_ActiveTexture   = (SL_PFN_ACTIVETEXTURE)   sl_gl_proc("glActiveTexture");
    p_MultiTexCoord2f = (SL_PFN_MULTITEXCOORD2F) sl_gl_proc("glMultiTexCoord2f");
    p_FogCoordf       = (SL_PFN_FOGCOORDF)       sl_gl_proc("glFogCoordf");
    if (p_ActiveTexture == NULL || p_MultiTexCoord2f == NULL
        || p_FogCoordf == NULL) {
        sl_post11_state = -1;
        fprintf(stderr, "sl_gl: post-1.1 entry points unavailable"
                        " (glActiveTexture=%p glMultiTexCoord2f=%p"
                        " glFogCoordf=%p) - TEXEL1 and the fog stage stay"
                        " off (B-075)\n",
                (void *) p_ActiveTexture, (void *) p_MultiTexCoord2f,
                (void *) p_FogCoordf);
        return 0;
    }
    sl_post11_state = 1;
    fprintf(stderr, "sl_gl: post-1.1 entry points resolved - TEXEL1 (B-048)"
                    " and the fog stage (B-045) are live on Windows"
                    " (B-075)\n");
    return 1;
}

static void sl_glActiveTexture(GLenum t)
{ if (p_ActiveTexture != NULL) p_ActiveTexture(t); }
static void sl_glMultiTexCoord2f(GLenum t, GLfloat s, GLfloat tt)
{ if (p_MultiTexCoord2f != NULL) p_MultiTexCoord2f(t, s, tt); }
static void sl_glFogCoordf(GLfloat c)
{ if (p_FogCoordf != NULL) p_FogCoordf(c); }

#define glActiveTexture    sl_glActiveTexture
#define glMultiTexCoord2f  sl_glMultiTexCoord2f
#define glFogCoordf        sl_glFogCoordf
#else
/* Where the header declares them the direct calls stand exactly as they
 * were, so this change is invisible to the native build. */
#define post11_ok() 1
#endif
#include "sl_gfx.h"
#include "sl_gfx_tex.h"
#include "sl_gfx_texprov.h"           /* #47: the texture provider seam */
#include "../sl_asset_override.h"
#include "../platform/sl_display.h"     /* #45: the aspect selection and the one viewport fit */

/* B-110. THE PREPROCESSOR IS POSITIONAL, AND THIS DEFINITION USED TO SIT
 * NEXT TO texenv_set - five thousand lines below its first user. Every
 * `#if defined(SL_TEXENV_COMBINE)` ABOVE that point evaluated FALSE on every
 * platform, which silently compiled out tex1_apply's B-107 water arm (unit 1
 * fell to its #else, plain GL_MODULATE: the water multiplied by TEXEL1 a
 * second time instead of by SHADE, discarding the dark blue-green vertex
 * colours - measured at the owner's mark as a centre pixel of (61,69,107)
 * against a Gouraud shade cap of (45,65,84), which no correct x SHADE can
 * exceed) and aeq_apply_env's AEQ_LERP constant push. The definition now
 * sits here, after <GL/gl.h> and the B-076 token block, so every user in the
 * file sees one answer. The condition is unchanged; where the header already
 * declares the tokens this is exactly the old test, earlier. */
#if defined(GL_COMBINE) && defined(GL_COMBINE_RGB) && defined(GL_SOURCE0_RGB) \
    && defined(GL_PRIMARY_COLOR) && defined(GL_COMBINE_ALPHA)
#define SL_TEXENV_COMBINE 1
#endif

#define OP_MTX 0x01
#define OP_VTX 0x04
#define OP_DL  0x06
#define OP_TRI4 0xB1
#define OP_TRI1 0xBF
#define OP_ENDDL 0xB8
#define OP_MOVEWORD 0xBC
#define OP_POPMTX 0xBD
#define MW_SEGMENT 0x06
#define MW_FOG     0x08   /* gSPFogPosition - gbi.h:1300 G_MW_FOG */
#define MW_NUMLIGHT 0x02  /* gSPNumLights   - gbi.h:1297 G_MW_NUMLIGHT */

/* G_MTX parameter bits, ucode05.txt's table (== gbi.h's non-F3DEX2 half) */
#define MTX_PROJECTION 0x01
#define MTX_LOAD       0x02
#define MTX_PUSH       0x04
#define MTX_SIZE       64              /* sizeof(Mtx); the length field */

#define MTX_STACK_MAX  16              /* RSP's modelview stack is 10 deep */
#define MTX_CMD_MAX    4096            /* per-frame sanity cap on 01 commands */

struct vtx { short x, y, z; unsigned short flag; short s, t; unsigned char r,g,b,a; };

static unsigned int  g_seg[16];
int sl_dl_branch_dbg;          /* SL_DL_BRANCH_DBG=1, B-066 diagnosis */
static struct vtx    g_vbuf[32];
/* ---- B-021: where each loaded vertex ended up, in eye space --------------
 *
 * The RSP transforms a vertex WHEN THE 04 COMMAND LOADS IT, using the
 * modelview in effect at that moment, and parks the result in its 16-entry
 * buffer. A later matrix command does not reach back and re-transform what is
 * already loaded. That is precisely how GoldenEye deforms a limb without ever
 * touching the matrix stack: `dotube` (model.c:4683-4686) loads the proximal
 * ring of a tube under one bone matrix and the rest of the tube under the
 * next bone's, then draws triangles spanning both, and the tube bends at the
 * joint because each end was transformed by its own bone.
 *
 * This interpreter used to keep the raw model-space coords and hand them to
 * GL at DRAW time under whatever matrix was current then - so both ends of
 * every tube got the second bone's transform, and the limb stretched from its
 * correct distal end back to a wrong proximal point. With the v0 fix in place
 * that is what was left on screen: elongated arms and a spike out of one
 * shoulder.
 *
 * So the modelview is applied here, at load, and the triangle path draws
 * through the projection alone with GL's modelview held at identity. */
static float         g_veye[32][3];
/* Normalised depth (0..1) for each loaded vertex, taken with the projection
 * in effect AT LOAD TIME. The RSP computes a vertex's fog value when the 04
 * command loads it; ours was deriving depth at TRIANGLE-DRAW time from
 * whatever projection the list had reached by then, so the same surface got
 * different fog depending on which batch it landed in. Adjacent floor tiles
 * at identical distance came out one green, one clear - the patchwork the
 * owner photographed - and it moved between frames, which is the flicker. */
static float         g_vdepth[32];

/* ---- LIVE OWNER BUG MARK: where each loaded vertex CAME FROM -------------
 *
 * The seam round got as far as "tri=50 dl=20026730" and then spent hours by
 * hand turning that into "which command in that list, which G_VTX load, which
 * point-table entries". These five arrays are what make the second half cheap:
 * they are written once per vertex AT THE LOAD, which is the only moment the
 * source address is still in scope, and read only when a mark is written.
 *
 * PASSIVE. Nothing here is read by the draw path, no vertex datum is altered,
 * and the display list executes identically whether or not these are kept.
 * Cost is five stores per loaded vertex, beside a memcpy of 12 bytes per
 * vertex that was already happening.
 *
 * g_vsrc_seg is the G_VTX command's segment operand (its w1) - the address in
 * the game's own vocabulary, which is the form a point table is named by.
 * g_vsrc_p is where that resolved on the host. g_vsrc_i is the vertex's index
 * WITHIN that load, so source entry = g_vsrc_seg + g_vsrc_i * 12. */
static unsigned      g_vsrc_seg[32];
static unsigned char g_vsrc_i[32];
static const void   *g_vsrc_p[32];
static unsigned      g_vsrc_dl[32];    /* depth-1 list operand in force      */
static unsigned      g_vsrc_off[32];   /* byte offset of the G_VTX command   */

/* ---- B-125: AUTHORED REDRAWS AND THE RDP'S TOLERANT DEPTH COMPARE ---------
 *
 * Surface's dome (room 7, DL 20016140) draws the same quads TWICE: a shaded
 * interior skin, then a white exterior skin, from a fresh G_VTX load of the
 * SAME positions, same material and othermode (z-compare, z-write, opaque),
 * opposite triangulation diagonals. The cartridge shows the second skin,
 * outside and in. Three rounds tried to make GL agree by changing what the
 * depth compare is fed: GL_LEQUAL (B-122, exact ties), then a 16-bit
 * attachment (B-123 inert, B-124 real) so the skins would quantize to ties.
 * They do not. Measured at owner mark 20260912-230216-001, from the record's
 * own clip rows: the quads are not planar, so the two diagonals interpolate
 * depths that differ by +1.8, +3.0, -1.3 and +1.8 sixteen-bit buckets at the
 * four quad centres around the crosshair - a pyramid over each quad, zero on
 * its edges, that no storage width can collapse. Under 24 bits the earlier
 * skin wins the whole interior wherever the white one is farther (the
 * gores); under 16 it wins the pixels where the two land in different
 * buckets - a lattice of dashes along the bucket contours, reproduced by
 * an offline rasterization of the record's vertices at 84% pixel agreement
 * against the .bmp (24-bit prediction: 45%). That lattice is the residual
 * hash the owner rejected.
 *
 * What resolves the skins on hardware is the RDP compare's TOLERANCE, not
 * its width. The corpus documents the triangle's z-coefficient block
 * (ucode05_old.txt l.709-720: zbuf and its change over x, z, y) and says
 * nothing about how the compare consumes it (searched 2026-09-13: z-buffer,
 * dz, tolerance, coplanar, zmode - doc-routing not_covered). The semantic
 * below is the RDP reference behaviour: an opaque fragment passes when
 *
 *     sz - 8 * max(dzpix, dzmem) <= oz
 *
 * dzpix being the primitive's own |dz/dx|,|dz/dy| maximum rounded up to a
 * power of two, dzmem the stored one, both in the 18-bit z domain (screen z
 * 0..G_MAXZ with 8 fraction bits, gbi.h:1219), with a floor on dzmem in the
 * coarse ranges - so a far-range redraw always passes within 128 z-units,
 * 4.9e-4 of window depth, ten times the dome's whole tent difference. The
 * write is the fragment's own z, untouched. GL's polygon offset is the same
 * slope term but it moves the WRITE as well as the test, so applied
 * uniformly it cancels between the two skins - which is why the earlier
 * rounds reached for quantization. The asymmetry has to be built:
 *
 *   1  a G_VTX load whose positions were ALL already drawn by this depth-1
 *      submission flags its slots (the vertex-position set below: eye-space
 *      positions of every drawn triangle, per submission, reset per list);
 *   2  a triangle whose three slots all carry the flag is an authored redraw
 *      of a surface this list has already drawn. Its colour pass tests with
 *      a tolerance and writes NO depth, marking the pixels it won in the
 *      stencil plane;
 *   3  a depth-only pass through that mark writes the exact z and clears
 *      the mark. Same triangle, same positions, so the same pixels.
 *
 * THE TOLERANCE IS THE QUAD'S OWN, NOT THE HARDWARE'S. The first version
 * of this path gave every redraw the RDP's far-range magnitude, and the
 * owner's next replay was "a big fail": textures bleeding through geometry
 * in front of them, level-wide (run 20260913-002517). The marks' own
 * records named the mechanism: the flagged draws there were the ROAD pass -
 * a second material (1ffc93fc) over the SAME snow triangles, same
 * triangulation, needing no tolerance at all - and a tolerance of tens of
 * world units let any such pass win a band behind nearer geometry drawn
 * earlier, wherever the two converge in depth: every crest, every
 * silhouette. So the hardware magnitude is NOT imported. A redraw that
 * reuses its first pass's triangulation (every edge already drawn) is a
 * plain draw - its depths are bit-identical to the first pass's, GL_LEQUAL
 * gives the tie to the later pass, nothing moves. Only a RE-TRIANGULATED
 * quad gets a tolerance, and exactly the one it needs: the redraw triangle
 * (p, q, r) whose edge (p, r) was never drawn is the new diagonal; the
 * fourth vertex s is the one both drawn edges (p, q) and (q, r) had opposite
 * them in the first pass (the edge table below), and the two tents differ
 * by at most their difference AT THE DIAGONALS' CROSSING - the apex of the
 * pyramid, the only interior vertex of the two triangulations' overlay,
 * with the difference zero at the four corners. GL interpolates window
 * depth linearly in screen space, so each tent's value there is a linear
 * interpolation along its own diagonal at the crossing's parameter:
 *
 *     D = | z_p + t (z_r - z_p)  -  z_q + u (z_s - z_q) |
 *
 * t, u from the 2D intersection of p-r and q-s in NDC. NOT the midpoint
 * formula |(z_p + z_r)/2 - (z_q + z_s)/2|: that is the crossing only for a
 * screen-space parallelogram, and under perspective the crossing shifts -
 * measured at owner mark 20260913-054107-001 the quad under the crosshair
 * had t = 0.47, u = 0.53 and a true difference of 6.5e-6 against the
 * midpoint's 1.05e-6, so the white pass failed in a band and the dark
 * skin showed through as a narrow rectangle, view-dependent (B-127). The
 * larger of the two forms plus four 24-bit buckets for rounding is the
 * tolerance; for the dome 1e-6 .. 5e-5, at most a few world units at that
 * range, and the railing a metre in front of the dome stays in front.
 * Anything not recognised as one of those two shapes draws plainly, i.e.
 * exactly as before this path existed.
 *
 * Nothing names a level, room, list, texture or colour. Geometry that is
 * not a redraw keeps GL's exact 24-bit compare; the cartridge's far-range
 * pop-through sloppiness is not imported for anything. SL_Z_REDRAW=0
 * disables the path; without a stencil plane (the B-124 arm) it is inert
 * by construction. */
#define RD_SET_BITS 13                          /* 8192 positions per submission */
#define RD_EDGE_BITS 14                         /* 16384 first-pass edges       */
static struct { float p[3]; unsigned gen; } g_rd_set[1u << RD_SET_BITS];
static struct {
    unsigned gen, a, b;                         /* position slots, a < b        */
    int      opp[2];                            /* opposite vertices, first two */
    float    oppn[2][3];                        /* ... and their ndc x, y, and window depth */
    int      n;
} g_rd_edge[1u << RD_EDGE_BITS];
static unsigned      g_rd_gen = 1u, g_rd_n, g_rd_en;
static unsigned char g_vredraw[32];             /* slot came from a wholly-old load */
static int           g_rd_cur;                  /* the triangle being emitted is a re-triangulated redraw */
static float         g_rd_tol;                  /* ... and this is its tolerance, window depth */
static int           g_rd_stencil_bits, g_rd_depth_bits;
static unsigned      g_rd_loads, g_rd_loads_old, g_rd_tris, g_rd_tris_z, g_rd_tris_same;

static int redraw_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_Z_REDRAW");
                  on = !(v != NULL && *v == '0'); }
    return on;
}

static void rd_scope_reset(void)
{
    g_rd_gen++;
    if (g_rd_gen == 0u) {              /* wrapped: every stale tag is now live */
        memset(g_rd_set, 0, sizeof g_rd_set);
        memset(g_rd_edge, 0, sizeof g_rd_edge);
        g_rd_gen = 1u;
    }
    g_rd_n = 0; g_rd_en = 0;
    memset(g_vredraw, 0, sizeof g_vredraw);
}

static unsigned rd_hash(const float *p)
{
    unsigned h, k;
    memcpy(&k, &p[0], 4); h  = k * 0x9E3779B1u;
    memcpy(&k, &p[1], 4); h ^= k * 0x85EBCA77u; h = (h << 13) | (h >> 19);
    memcpy(&k, &p[2], 4); h ^= k * 0xC2B2AE3Du;
    return h ^ (h >> 16);
}

/* The slot of an eye-space position in this submission's drawn set, -1 when
 * absent (or, with insert, when the set is full). Open addressing over
 * generation-tagged entries; a set that fills beyond three quarters stops
 * inserting, so the worst case is "not detected", never "detected wrongly". */
static int rd_pos(const float *p, int insert)
{
    unsigned mask = (1u << RD_SET_BITS) - 1u;
    unsigned i = rd_hash(p) & mask, probes = 0;
    for (;;) {
        if (g_rd_set[i].gen != g_rd_gen) {
            if (!insert) return -1;
            if (g_rd_n >= (mask + 1u) * 3u / 4u) return -1;
            g_rd_set[i].gen = g_rd_gen;
            memcpy(g_rd_set[i].p, p, sizeof g_rd_set[i].p);
            g_rd_n++;
            return (int) i;
        }
        if (memcmp(g_rd_set[i].p, p, sizeof g_rd_set[i].p) == 0) return (int) i;
        i = (i + 1u) & mask;
        if (++probes > mask) return -1;
    }
}

static int rd_lookup(const float *p, int insert) { return rd_pos(p, insert) >= 0; }

/* The first-pass edge table: edge (a, b) of a drawn triangle, keyed by the
 * two position slots, remembering the vertex opposite it (two at most - a
 * mesh edge belongs to two triangles). find: the entry or -1. */
static int rd_edge(unsigned a, unsigned b, int insert)
{
    unsigned mask = (1u << RD_EDGE_BITS) - 1u;
    unsigned i, probes = 0;
    if (a > b) { unsigned t = a; a = b; b = t; }
    i = ((a * 0x9E3779B1u) ^ (b * 0x85EBCA77u) ^ ((b >> 7) * 0xC2B2AE3Du)) & mask;
    for (;;) {
        if (g_rd_edge[i].gen != g_rd_gen) {
            if (!insert) return -1;
            if (g_rd_en >= (mask + 1u) * 3u / 4u) return -1;
            g_rd_edge[i].gen = g_rd_gen; g_rd_edge[i].a = a; g_rd_edge[i].b = b;
            g_rd_edge[i].n = 0;
            g_rd_en++;
            return (int) i;
        }
        if (g_rd_edge[i].a == a && g_rd_edge[i].b == b) return (int) i;
        i = (i + 1u) & mask;
        if (++probes > mask) return -1;
    }
}

static void rd_edge_note(int a, int b, int opp, const float *oppn)
{
    int e;
    if (a < 0 || b < 0 || opp < 0 || a == b) return;
    e = rd_edge((unsigned) a, (unsigned) b, 1);
    if (e < 0) return;
    if (g_rd_edge[e].n >= 1 && g_rd_edge[e].opp[0] == opp) return;
    if (g_rd_edge[e].n >= 2) return;
    g_rd_edge[e].opp[g_rd_edge[e].n] = opp;
    memcpy(g_rd_edge[e].oppn[g_rd_edge[e].n], oppn, sizeof g_rd_edge[e].oppn[0]);
    g_rd_edge[e].n++;
}

/* Classify a redraw triangle (slots s[], ndc x/y/window-z rows n[]) against
 * the first pass. 1 with *tol = the re-triangulated quad's tent difference
 * at the diagonals' crossing; 0 for the same triangulation (plain draw) or
 * anything unrecognised. */
static int rd_classify(const int *s, const float (*n)[3], float *tol)
{
    int e[3], miss = -1, nmiss = 0, k;
    for (k = 0; k < 3; k++) {
        e[k] = rd_edge((unsigned) s[k], (unsigned) s[(k + 1) % 3], 0);
        if (e[k] < 0) { miss = k; nmiss++; }
    }
    if (nmiss != 1) return 0;
    {
        /* the missing edge (x, y) is the new diagonal; q is the third
         * vertex; s is the vertex both drawn edges (x, q) and (q, y) had
         * opposite them in the first pass */
        int ix = miss, iy = (miss + 1) % 3, iq = (miss + 2) % 3;
        int ea = rd_edge((unsigned) s[ix], (unsigned) s[iq], 0);
        int eb = rd_edge((unsigned) s[iq], (unsigned) s[iy], 0);
        int i, j, so = -1; const float *sn = NULL;
        if (ea < 0 || eb < 0) return 0;
        for (i = 0; i < g_rd_edge[ea].n && so < 0; i++)
            for (j = 0; j < g_rd_edge[eb].n; j++)
                if (g_rd_edge[ea].opp[i] == g_rd_edge[eb].opp[j]
                    && g_rd_edge[ea].opp[i] != s[ix]
                    && g_rd_edge[ea].opp[i] != s[iy]
                    && g_rd_edge[ea].opp[i] != s[iq]) {
                    so = g_rd_edge[ea].opp[i]; sn = g_rd_edge[ea].oppn[i]; break;
                }
        if (so < 0) return 0;
        {
            const float *p = n[ix], *r = n[iy], *q = n[iq];
            float dmid, dx, den, t, u, za, zb;
            /* the midpoint form, kept as a floor */
            dmid = (p[2] + r[2]) * 0.5f - (q[2] + sn[2]) * 0.5f;
            if (dmid < 0.0f) dmid = -dmid;
            /* the crossing of p-r and q-s in ndc; t along p-r, u along q-s */
            den = (r[0] - p[0]) * (sn[1] - q[1]) - (r[1] - p[1]) * (sn[0] - q[0]);
            if (den != 0.0f) {
                t = ((q[0] - p[0]) * (sn[1] - q[1]) - (q[1] - p[1]) * (sn[0] - q[0])) / den;
                u = ((q[0] - p[0]) * (r[1] - p[1])  - (q[1] - p[1]) * (r[0] - p[0]))  / den;
                if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
                if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
                za = p[2] + t * (r[2] - p[2]);
                zb = q[2] + u * (sn[2] - q[2]);
                dx = za - zb;
                if (dx < 0.0f) dx = -dx;
            } else {
                dx = dmid;
            }
            *tol = dx > dmid ? dx : dmid;
        }
        return 1;
    }
}

static unsigned      g_tris, g_verts, g_cmds, g_unknown;
/* 04 commands whose two redundant length fields disagreed, i.e. that were
 * not vertex commands. Separated from g_unknown so the filter below can
 * never drop real geometry silently. */
static unsigned      g_vtx_reject;
/* Vertex loads aimed at a slot other than 0. Kept because its being zero on
 * Facility and dam, and 37 on a streets frame with guards, is what separated
 * B-021's real cause from the matrix-stack theory. */
static unsigned      g_vtx_v0nz;
/* B-139. 04 commands whose operand resolved to nothing readable (see
 * vtx_unresolved). Also counted into g_vtx_reject. */
static unsigned      g_vtx_unres;
static float         g_min[3], g_max[3];

/* Per-opcode tally of everything the interpreter did not act on. The shape of
 * this histogram is the diagnosis: concentrated in 0xE0-0xFF (plus B6/B7/B9/
 * BA/BB) means legitimate unhandled RDP and RSP state commands, and names what
 * to implement next; spread evenly over 0x00-0xFF means walk() is grinding
 * through memory that is not a display list. */
static unsigned      g_op_hist[256];

/* Top-level list bound. Sub-lists reached through G_DL live in segment memory
 * outside [first, end), so only the depth-0 walk may be bounded by it. */
static const unsigned int *g_dl_end;

/* Where the commands are actually being read from: per-depth totals, how often
 * the depth-0 end bound fired, and how often the 100000-command runaway guard
 * tripped. Distinguishes "the top-level list overran" from "a G_DL target
 * resolved to the wrong place". */
static unsigned g_depth_cmds[10], g_end_stops, g_guard_stops, g_zero_runs;

/* Matrix state. Arrays are 16 floats in the N64's row-major, row-vector
 * order (v' = v * M). That is bit-for-bit the layout glLoadMatrixf wants:
 * GL reads element (row r, col c) of its column-vector matrix A from index
 * c*4 + r, and the column-vector form of v' = v*M is A = M^T, so
 * A[r][c] = M[c][r] = M_linear[c*4 + r]. The index mapping is the identity
 * and no transpose is needed. */
static float    g_mv[MTX_STACK_MAX][16];   /* modelview stack, top at g_mv_sp */
static int      g_mv_sp;
static float    g_proj[16];        /* the projection IN FORCE: g_proj_game with the #45 aspect applied */
static float    g_proj_game[16];   /* the projection exactly as the list loaded / multiplied it */
static unsigned g_mtx_cmds, g_mtx_proj, g_mtx_mv, g_mtx_load, g_mtx_mul;
static unsigned g_mtx_push, g_mtx_pop, g_mtx_bad;

/* Whether the matrices actually put geometry on screen is the one thing the
 * command counters cannot say, and with no way to read the presented window
 * back it is also the only honest substitute for looking at it: transform each
 * emitted vertex the way GL will, and count how many land inside the clip
 * volume. A transposed, byte-swapped or otherwise wrong matrix scores ~0%. */
static unsigned g_on, g_off;
static float    g_ndc_min[3], g_ndc_max[3];

static const float g_identity[16] = {
    1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1
};

/* Is this byte a command at all? The full ucode05.txt table:
 *   00-09  DMA          (02/05/07/08 are rsp_reserved0..3)
 *   AF     load_ucode,  B0-BF immediate (B1 = GE_TRI4, B8 = enddl)
 *   C0     rdp_noop,    C8-CF the RSP-generated triangle commands
 *   E4-FF  RDP pass-through (F1 is not in the table)
 * Everything else is not an opcode, so counting it is how a walk through
 * non-display-list memory announces itself. */
static int op_in_ucode05(unsigned op)
{
    if (op <= 0x09) return 1;
    if (op == 0xAF || (op >= 0xB0 && op <= 0xBF)) return 1;
    if (op == 0xC0 || (op >= 0xC8 && op <= 0xCF)) return 1;
    if (op >= 0xE4 && op != 0xF1) return 1;
    return 0;
}

/* Opcodes that carry weight when deciding a list's word order. ucode05.txt's
 * table names 00 "rsp noop" and 02/05/07/08 rsp_reserved0..3 - all real
 * command bytes, but ones a real list essentially never contains, while
 * zero-filled memory is nothing but 00. Scoring them would make a run of
 * zeros look exactly as much like a display list as a display list does; that
 * ambiguity is the whole bug, so they score nothing. */
static int op_strong(unsigned op)
{
    if (op == 0x00 || op == 0x02 || op == 0x05 || op == 0x07 || op == 0x08)
        return 0;
    return op_in_ucode05(op);
}

static unsigned int sl_bswap32(unsigned int v)
{
    return ((v >> 24) & 0x000000ffu) | ((v >> 8) & 0x0000ff00u) |
           ((v << 8) & 0x00ff0000u) | ((v << 24) & 0xff000000u);
}

#define RDW(w, sw) ((sw) ? sl_bswap32(w) : (w))

#define SNIFF_PAIRS 16

#define SL_SW16(v) ((((unsigned)(v) >> 8) & 0xffu) | (((unsigned)(v) << 8) & 0xff00u))

/* A plausible host pointer. Everything reachable from a DL lives in mapped
 * RDRAM or the kseg2 TLB window; anything else is a decode error and must NOT
 * be dereferenced. Treating a low segment-0 address as a direct pointer
 * segfaulted at 0x12760c. */
static int ptr_ok(unsigned long p)
{
    return p >= 0x00010000ul && p < 0xfffff000ul;
}

/* Is [p, p+len) actually mapped? ptr_ok() only rejects obvious nonsense; a
 * matrix operand can name a plausible-looking address that was never mapped,
 * and the skeleton's SIGSEGV handler only rescues the kseg2 pager window -
 * anything else exits(5). mincore() answers without touching the memory, and
 * reports mapped-but-PROT_NONE pages as mapped, so a genuine kseg2 address
 * still passes here and pages in on the read, exactly as intended. */
#ifdef _WIN32
/* B-072. The one measured bottleneck in Facility, and the reason a doorway
 * full of guards costs more than a wall: mem_readable is called once per
 * display-list operand and once per triangle's texture, and on Windows it
 * asked the kernel every single time.
 *
 * MEASURED on the Facility spawn, 30-frame windows, SL_FRAMEPROF=1:
 *
 *   tot=26.14  dl=25.20  pres=0.21  up=0.72 ms per frame
 *     inside dl: walk=24.93  tex=5.50 (958 calls)  memprobe=15.35 (4238 calls)
 *
 * 15.35 ms of a 26.14 ms frame - 59% of ALL frame work - in VirtualQuery, at
 * 3.6 us a call. It is not mincore(): mincore reads a bitmap the kernel
 * already has, while VirtualQuery walks the VAD tree and marshals a
 * MEMORY_BASIC_INFORMATION across the user/kernel boundary. The Linux and
 * Windows branches of this function look symmetrical and are three orders of
 * magnitude apart in cost. The scaling is per DL command and per triangle,
 * which is exactly the view-dependent term.
 *
 * The repair is not to probe less carefully - every probe still happens, and
 * every answer is still the kernel's. VirtualQuery already reports the whole
 * REGION containing the address, so one answer covers every address in it;
 * the old loop threw that away and asked again for the next operand in the
 * same region. Remembering the last few regions answers from the same fact
 * the kernel gave us.
 *
 * Bounded to one frame. sl_gfx_frame_dl clears this at the top of every list,
 * so a stale entry cannot outlive the frame that observed it, and the display
 * list being walked was built before the walk began. MEM_RESERVE and
 * MEM_COMMIT are both "not free", so the pager committing a kseg2 page
 * mid-frame cannot change an answer already given - which is what makes the
 * memo safe rather than merely fast.
 *
 * SL_DL_MEMCACHE=0 forces every probe back through the kernel, so the two
 * paths stay A/B-able against each other.
 */
#define VQ_CACHE_N 16
static struct { unsigned long base, end; int ok; } g_vq[VQ_CACHE_N];
static unsigned g_vq_n, g_vq_next;

/* src/platform/sl_ultra_shim.c - the kseg2 window the pager fills on touch. */
extern int sl_pager_covers(unsigned long addr);

/* B-139 witness: describe the region Windows reports at `a`, uncached, for
 * the unresolved-operand report. Debug path only; never on the hot path. */
static void vq_describe(unsigned long a, char *buf, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void *) a, &mbi, sizeof mbi) != sizeof mbi) {
        snprintf(buf, n, "VirtualQuery failed");
        return;
    }
    snprintf(buf, n, "region base=%08lx alloc=%08lx size=%08lx state=%s protect=%#lx",
             (unsigned long) mbi.BaseAddress, (unsigned long) mbi.AllocationBase,
             (unsigned long) mbi.RegionSize,
             mbi.State == MEM_FREE ? "FREE" : mbi.State == MEM_RESERVE ? "RESERVE"
                                            : mbi.State == MEM_COMMIT ? "COMMIT" : "?",
             (unsigned long) mbi.Protect);
}

static void vq_cache_reset(void) { g_vq_n = 0; g_vq_next = 0; }

/* THE PER-60-FRAME CENSUS IS OFF BY DEFAULT.  B-090.
 *
 * The block at the foot of sl_gfx_frame_dl prints about a hundred lines of
 * diagnostics - opcode histogram, texture census, both matrices, the fog and
 * combiner tallies - once every 60 rendered frames, and it used to do it
 * unconditionally in every WINDOWED run, which is to say in every run a
 * player ever sees.  stderr is unbuffered, so each line is its own WriteFile.
 *
 * MEASURED 2026-09-03, bracketed with QueryPerformanceCounter around the
 * block itself in a 1400-frame windowed front-end run: 12 of 12 censuses
 * cost between 276.18 ms and 527.49 ms.  The frame pump is single-threaded,
 * so that is the main loop stopped dead for a third of a second roughly
 * every two seconds.  Total run: 30.0 s wall for what should be 23 s of
 * frames, and every dt spike above 45 ms in the log sat on a census frame.
 *
 * This is native-only instrumentation, not Rare's pacing, so rule 5 does not
 * protect it.  It stays reachable from the same binary with SL_DL_CENSUS=1,
 * and the suppressed path still prints ONE line on the same cadence carrying
 * the headline counters - because a silent instrument and an instrument that
 * measured nothing are the two things this file refuses to let a reader
 * confuse.  Reports that live INSIDE the block (SL_BB_DBG, SL_CC_DBG and the
 * texture and matrix tallies) need SL_DL_CENSUS=1 as well; the heartbeat
 * line says so. */
static int dl_census_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_DL_CENSUS");
                  on = (v != NULL && *v != '0'); }
    return on;
}

static int vq_cache_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("SL_DL_MEMCACHE");
        on = !(v != NULL && *v == '0');
    }
    return on;
}

/* Is `a` inside a region that is not MEM_FREE? Sets *rend to the end of the
 * region the answer came from, so the caller's span walk can skip the whole
 * of it in one step - the same contract VirtualQuery itself offers. */
static int vq_region(unsigned long a, unsigned long *rend)
{
    MEMORY_BASIC_INFORMATION mbi;
    unsigned i;

    if (vq_cache_on()) {
        for (i = 0; i < g_vq_n; i++) {
            if (a >= g_vq[i].base && a < g_vq[i].end) {
                *rend = g_vq[i].end;
                return g_vq[i].ok;
            }
        }
    }
    if (VirtualQuery((void *) a, &mbi, sizeof mbi) != sizeof mbi) {
        /* No region to remember, and no ground to advance over. */
        *rend = a + 1;
        return 0;
    }
    {
        unsigned long base = (unsigned long) mbi.BaseAddress;
        unsigned long end  = base + (unsigned long) mbi.RegionSize;
        /* B-139. "Not MEM_FREE" was the rule here, and it is wrong by
         * exactly one case: a region that is RESERVED but not committed is
         * not free, and reading it is an access violation the VEH does not
         * rescue - only the kseg2 window's reservations page in on touch.
         * A 32-bit process has plenty of such regions (heap and driver
         * reservations), and a display-list operand that names one passes
         * the probe and faults on the read that follows it.
         *
         * OWNER CRASH, Aztec, run 20260916-074840-lvl28, frame 13397:
         * ACCESS VIOLATION READ at 0x04000000 with pc inside walk()'s G_VTX
         * memcpy (0x54470f, walk.part.0+0x4c3f; ra 0x542493 walk,
         * 0x546cfb sl_gfx_frame_dl, then rspGfxTaskStart rsp.c:246).
         * 0x04000000 is a G_VTX operand in segment form - segment 4
         * (SPSEGMENT_MODEL_VTX), offset 0, the first vertex batch of a
         * model whose segment was not set on that submission - which
         * dl_operand tried as a raw host pointer, and which this probe
         * accepted because SOMETHING is reserved there. The read then
         * faulted. Reserved-and-uncommitted is not readable; only inside
         * the pager's window is it (sl_ultra_shim.c sl_pager_covers). The
         * probe is the crash handler's own sl_win_readable rule
         * (sl_main.c) plus that one exception, and what used to fault now
         * resolves to NULL and is counted (see OP_VTX, g_vtx_unres). */
        int ok = (mbi.State == MEM_COMMIT
                  && (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0)
                 || sl_pager_covers(a);

        /* Defensive: a region that does not contain the address it was
         * queried with, or one that does not advance, would loop the caller
         * forever. Answer without remembering it. */
        if (a < base || a >= end) { *rend = a + 1; return ok; }
        *rend = end;
        if (vq_cache_on()) {
            if (g_vq_n < VQ_CACHE_N) i = g_vq_n++;
            else { i = g_vq_next; g_vq_next = (g_vq_next + 1) % VQ_CACHE_N; }
            g_vq[i].base = base; g_vq[i].end = end; g_vq[i].ok = ok;
        }
        return ok;
    }
}
#else
static void vq_describe(unsigned long a, char *buf, unsigned n)
{
    (void) a;
    snprintf(buf, n, "(no region query on this platform)");
}
#endif

static int mem_readable(unsigned long p, unsigned len)
{
    static long pagesz;
#ifndef _WIN32
    unsigned char vec[8];
#endif
    unsigned long first, last, span;

    if (!ptr_ok(p) || !ptr_ok(p + len - 1)) return 0;
#ifdef _WIN32
    if (pagesz <= 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        pagesz = (long) si.dwPageSize;
    }
#else
    if (pagesz <= 0) pagesz = sysconf(_SC_PAGESIZE);
#endif
    if (pagesz <= 0) pagesz = 4096;

    first = p & ~(unsigned long)(pagesz - 1);
    last  = (p + len - 1) & ~(unsigned long)(pagesz - 1);
    span  = last - first + (unsigned long) pagesz;
#ifdef _WIN32
    /* VirtualQuery is the mincore() analogue: it answers from the kernel's
     * region tree without touching the memory. MEM_RESERVE counts as mapped
     * alongside MEM_COMMIT, which is what preserves the property the
     * mincore() comment above relies on - the kseg2 window is reserved
     * PAGE_NOACCESS and committed per fault by the VEH pager, so a genuine
     * kseg2 address must pass here and page in on the read. A span can cross
     * several regions, so walk it rather than querying only the first.
     *
     * Answered from vq_region rather than called directly. See B-072 above it:
     * the two are not interchangeable in cost, and this is the hot path. */
    {
        unsigned long a = first, end = first + span;
        while (a < end) {
            unsigned long rend;
            if (!vq_region(a, &rend))
                return 0;
            a = rend;
        }
    }
    return 1;
#else
    if (span / (unsigned long) pagesz > sizeof vec) return 0;
    return mincore((void *) first, (size_t) span, vec) == 0;
#endif
}

/* Length of the leading run of strong opcodes under one word-order
 * hypothesis, plus a decisive bonus if that run reaches the list's own enddl.
 * Symmetric in the two hypotheses, so the comparison is a measurement rather
 * than a preference. Never reads a page mincore() has not confirmed. */
static int order_score(const unsigned int *dl, int swapped)
{
    int score = 0, i;

    for (i = 0; i < SNIFF_PAIRS; i++) {
        unsigned int w0;
        unsigned op;
        if (!mem_readable((unsigned long) (dl + 2 * i), 8)) break;
        w0 = RDW(dl[2 * i], swapped);
        op = (w0 >> 24) & 0xff;
        if (!op_strong(op)) break;
        score++;
        if (op == OP_ENDDL) { score += SNIFF_PAIRS; break; }
    }
    return score;
}

/* 1 when this list is stored in ROM/wire word order. Native wins ties: the
 * per-frame list the game builds with 32-bit stores is the common case, and
 * anything ambiguous is safer read the way it was written. */
static unsigned g_list_native, g_list_swapped;

static int list_swapped(const unsigned int *dl)
{
    int nat = order_score(dl, 0);
    int swp = order_score(dl, 1);
    if (swp > nat) { g_list_swapped++; return 1; }
    g_list_native++;
    return 0;
}

static const void *seg_resolve(unsigned int a)
{
    unsigned s = (a >> 24) & 0x0f;
    unsigned long p;

    if (a == 0) return NULL;
    if (g_seg[s] == 0) return NULL;          /* segment never set: unusable */
    p = (unsigned long) g_seg[s] + (a & 0x00ffffff);
    return ptr_ok(p) ? (const void *) p : NULL;
}

/* A G_DL branch target or a vertex-batch operand, resolved the same way a
 * matrix and a texture-image operand already are (mtx_resolve, tex_operand):
 * segment address first, but ONLY when the top nibble is clear and that
 * segment has actually been set, then the raw host pointer, then the
 * OS_K0_TO_PHYSICAL form.  These two operands were the only ones still going
 * through bare seg_resolve(), which reads bits 24-27 as a segment index
 * unconditionally - so a raw 0xc8xxxxxx pointer was read as "segment 8", a
 * segment nothing in the tree ever sets (`grep -rn gSPSegment src/
 * --include=*.c` sets only 0,1,2,3,4,5,14,15), and the target silently became
 * NULL.  Measured 2026-08-25: bgRenderRoomPrimary hands gSPDisplayList the
 * mempAlloc'd room list at 0xc8460810 and gSPSegment the room's vertices at
 * 0xc845f150; both dropped, so Facility's room geometry had NEVER rendered
 * natively and the only world pixels on screen came from props and
 * characters.  B-019. */
static const void *dl_operand(unsigned int a, unsigned int len)
{
    unsigned long cand;

    if (a == 0) return NULL;
    if ((a & 0xf0000000u) == 0 && g_seg[(a >> 24) & 0x0f] != 0) {
        const void *p = seg_resolve(a);
        if (p && mem_readable((unsigned long) p, len)) return p;
    }
    cand = (unsigned long) a;
    if (ptr_ok(cand) && mem_readable(cand, len)) return (const void *) cand;
    if (a < 0x80000000u) {
        cand = (unsigned long) a + 0x80000000ul;
        if (ptr_ok(cand) && mem_readable(cand, len)) return (const void *) cand;
    }
    return NULL;
}

/* Resolve a G_MTX operand to a readable 64-byte host address, or NULL.
 * Candidates in order; the first that is mapped wins. Segment form is tried
 * first and only when the top nibble is clear and that segment has actually
 * been set this frame, which is what keeps a raw 0xc8xxxxxx pointer from being
 * mistaken for "segment 8". */
static const unsigned int *mtx_resolve(unsigned int a)
{
    unsigned long cand;

    if (a == 0) return NULL;

    if ((a & 0xf0000000u) == 0 && g_seg[(a >> 24) & 0x0f] != 0) {
        const void *p = seg_resolve(a);
        if (p && mem_readable((unsigned long) p, MTX_SIZE))
            return (const unsigned int *) p;
    }
    cand = (unsigned long) a;                       /* osVirtualToPhysical */
    if (mem_readable(cand, MTX_SIZE))
        return (const unsigned int *) cand;
    if (a < 0x80000000u) {                          /* OS_K0_TO_PHYSICAL */
        cand = (unsigned long) a + 0x80000000ul;
        if (mem_readable(cand, MTX_SIZE))
            return (const unsigned int *) cand;
    }
    return NULL;
}

/* A texture-image operand, resolved the same three ways a matrix operand is
 * (see the header comment): segment address, OS_K0_TO_PHYSICAL, or a raw host
 * pointer. Both of the first two genuinely occur here - tex.c:506 hands
 * gDPSetTextureImage a `tex->data` KSEG0 pointer, while texLoad patches DL
 * words with `osVirtualToPhysical(tex->data)` (src/game/image.c:2476). Only a
 * mincore()-mapped candidate is ever returned; the full decoded size is
 * re-checked at decode time. */
static const unsigned char *tex_operand(unsigned int a)
{
    unsigned long cand;

    if (a == 0) return NULL;
    if ((a & 0xf0000000u) == 0 && g_seg[(a >> 24) & 0x0f] != 0) {
        const void *p = seg_resolve(a);
        if (p && mem_readable((unsigned long) p, 8))
            return (const unsigned char *) p;
    }
    cand = (unsigned long) a;
    if (mem_readable(cand, 8)) return (const unsigned char *) cand;
    if (a < 0x80000000u) {
        cand = (unsigned long) a + 0x80000000ul;
        if (mem_readable(cand, 8)) return (const unsigned char *) cand;
    }
    return NULL;
}

/* Vertex S/T carry the RDP's 5-bit texel fraction; see the citation at the
 * glTexCoord2f call. 1 texel == 32 units. */
#define SL_ST_SCALE 32

/* ED setscissor, in whole pixels. */
static int g_sciss[4];
/* Has this frame's first scissor been seen yet? The height it carries is
 * ASSIGNED rather than max'd, which is what lets the framebuffer shrink again
 * when the game leaves the 440x330 front end. See OP_SETSCISS. */
static int g_scr_h_adopted;

/* B-101. Non-zero only while the WITNESS re-walk of a doubled frame is
 * running. Until B-143 it was the only thing in this build that applied the
 * decoded scissor to GL; the production path now applies it too (see
 * sciss_on and OP_SETSCISS), and on a witness frame pass A runs WITHOUT it so
 * the witness keeps measuring what the scissor removes. See sl_gfx_frame_dl. */
static int g_wit_pass;
/* B-143. Non-zero during pass A of a witness frame - the one walk of the run
 * that deliberately ignores the scissor, so pass B has something to differ
 * from. Zero on every ordinary frame. */
static int g_wit_a;
/* B-143 census: scissors applied this frame, and boxes the RDP would have
 * treated as empty (x1 <= x0 or y1 <= y0), which are reported rather than
 * applied - see OP_SETSCISS. */
static unsigned g_sciss_applied, g_sciss_empty, g_sciss_frames_empty;

/* ---- B-143: THE RDP SCISSOR IS APPLIED ----------------------------------
 *
 * `ED setscissor` is issued by the game before EVERY room's display list
 * (bg.c:694 bgScissorCurrentPlayerViewF) with the room's PORTAL WINDOW - the
 * screen rectangle the visibility traversal reached the room through - and
 * the RDP draws nothing of that room outside it. B-101 declined to honour it
 * because recorded Dam/Depot play showed no difference and Frigate lost a
 * band; both readings were taken with the B-112 policy already replacing
 * every portal window by the full view, which is exactly the case where the
 * scissor cannot bite.
 *
 * MEASURED 2026-09-17, owner mark 20260916-232914-lvl33/mark-001 (Dam, room
 * 121, looking up at the massif): the cartridge's visible-room table confines
 * rooms 23/35/26-29/1/2 to the window [1,182]-[30,214] of 320x240, and
 * redirecting room 1's list to an ENDDL on the cartridge changes ZERO
 * pixels - nothing of room 1 reaches the screen there. Natively room 1 drew
 * 25 triangles across the whole view, two of them Rare's authored junk: a
 * long sliver quad in the sky (Tri4 b1000032 00002010 at +0x138) and a
 * fully-fogged wedge over the ridge (the owner's "missing mountain
 * texture"). Under the cartridge portal rule plus this scissor both vanish
 * and the ridge pairs with the cartridge (docs/backlog.md B-143).
 *
 * SL_SCISSOR=0 restores the ignored scissor from the same binary. The
 * witness (SL_SCISS_WITNESS) still compares an unscissored walk against a
 * scissored one: pass A of a witness frame is the one walk that skips it. */
static int sciss_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_SCISSOR");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}
/* The distinct scissor boxes the witness frame issued, in framebuffer
 * coordinates. Reported so a difference can be read against the boxes that
 * produced it rather than as a bare pixel count. */
static int g_wit_rects[64][4];
static int g_wit_rects_n;
/* Frame-entry values of the state sl_gfx_frame_dl deliberately does not reset
 * per frame, so the re-walk can be handed pass one's starting point. */
static int      g_wit_vp_rect[4], g_wit_vp_have;
static unsigned g_wit_scr_w, g_wit_scr_h;
/* SL_SCISS_WITNESS_SHRINK=<n>: THE POSITIVE CONTROL, and it is not optional
 * rigging.
 *
 * This witness answers a question whose expected answer is "no difference",
 * and this document has recorded three separate instruments whose silence
 * meant "never ran" rather than "measured nothing". A null result here is
 * only evidence if the same apparatus can be made to produce a LOUD one.
 *
 * With n set, every applied box is inset by n framebuffer pixels before it
 * reaches glScissor. The rectangle is then demonstrably wrong, and the
 * difference the run reports is demonstrably real - which proves glScissor is
 * live, that the coordinate mapping points at the right part of the window,
 * and that the read-back and the diff can both see a change. Run it once at
 * n=20 beside every n=0 sweep. */
static int wit_shrink(void)
{
    static int n = -1;
    if (n < 0) { const char *v = getenv("SL_SCISS_WITNESS_SHRINK");
                 n = (v != NULL && *v != '\0') ? atoi(v) : 0;
                 if (n < 0) n = 0; }
    return n;
}

/* SL_SCISS_WITNESS_NULL=1: THE NULL CONTROL, and it is the one that decides
 * whether any other reading here means anything.
 *
 * The whole instrument rests on one claim: that walking the same list a
 * second time reproduces the first walk exactly, so a differing pixel can
 * only be the scissor. That claim is testable. With this set the second walk
 * runs in full and simply never calls glScissor, and the difference MUST be
 * zero - not small, zero. Any non-zero reading means the re-walk is not
 * faithful (frame-scoped state that was missed, a cache that warmed, a
 * counter that moved) and every other number this witness prints is noise.
 * Run it before believing a null result. */
static int wit_null(void)
{
    static int n = -1;
    if (n < 0) { const char *v = getenv("SL_SCISS_WITNESS_NULL");
                 n = (v != NULL && *v != '0' && *v != '\0'); }
    return n;
}

/* s15.16, split integer half / fraction half - see the header comment. */
static void mtx_to_float(const unsigned int *w, float out[16])
{
    unsigned k;
    for (k = 0; k < 16; k++) {
        unsigned int iw = w[k >> 1], fw = w[8 + (k >> 1)];
        unsigned int hi = (k & 1) ? (iw & 0xffffu) : (iw >> 16);
        unsigned int lo = (k & 1) ? (fw & 0xffffu) : (fw >> 16);
        out[k] = (float) (int) ((hi << 16) | lo) / 65536.0f;
    }
}

/* out = a * b, row-major. G_MTX_MUL replaces the target with M * target, so
 * a vertex sees v * (M * C) = (v * M) * C. */
static void mtx_mul(const float a[16], const float b[16], float out[16])
{
    float t[16];
    int r, c, k;
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
            t[r * 4 + c] = s;
        }
    memcpy(out, t, sizeof t);
}

/* Put one just-loaded vertex through the current modelview, row-vector order
 * (v' = v * M) - the same convention g_mv is stored in. */
static void vtx_transform(unsigned slot)
{
    const struct vtx *v = &g_vbuf[slot];
    const float *m = g_mv[g_mv_sp];
    float x = (float) v->x, y = (float) v->y, z = (float) v->z;
    int j;
    for (j = 0; j < 3; j++)
        g_veye[slot][j] = x * m[j] + y * m[4 + j] + z * m[8 + j] + m[12 + j];

    {   /* ... and its depth, under the projection standing right now. */
        const float *e = g_veye[slot];
        float cz = e[0] * g_proj[2] + e[1] * g_proj[6]
                 + e[2] * g_proj[10] + g_proj[14];
        float cw = e[0] * g_proj[3] + e[1] * g_proj[7]
                 + e[2] * g_proj[11] + g_proj[15];
        /* RAW NDC z, which RANGES -1..1 - not the 0..1 remap.
         *
         * gbi.h:2760 says the fog input is "(eyespace z) ranges -1 to 1", and
         * that was measured rather than taken on trust: two runs differing
         * only in far plane agree to 2.89% under the -1..1 model (the
         * framebuffer's quantisation floor) and disagree by 23.12% with a
         * one-way bias under 0..1. fm/fo cannot tell the two apart - they are
         * affine in each other - and neither can the near plane; only the far
         * plane separates them.
         *
         * Remapping to 0..1 opened the band at z=0.96 where Rare opens it at
         * 0.98: 336 world units instead of 503, so the ramp was compressed
         * into the top of the range and nearly everything pinned at full fog.
         * Measured against the cartridge on one gassed floor column, alpha
         * near/mid/far: ROM 8/129/239, ours 130/193/247 - over-fogged 16x at
         * the near end. */
        g_vdepth[slot] = (cw > 1e-6f) ? (cz / cw) : 0.0f;
    }
}

/* glMatrixMode/glLoadMatrixf are illegal between glBegin and glEnd, and a
 * matrix command can land anywhere in the list, so the triangle batch is
 * opened lazily and closed before any GL state change. */
static unsigned g_dl_frame;        /* file-scope frame counter, for probes */
/* Frames in which the interpreter ACTUALLY EXECUTED - i.e. past the
 * sl_gfx_active() early return. g_dl_frame counts calls, including the ones
 * that immediately return doing nothing, so it cannot distinguish a headless
 * run from a rendering one. Every census guard below uses THIS one. */
static unsigned g_dl_ran;
static unsigned g_tri_id;          /* per-frame triangle number, for probes */

/* SL_TRI_ID: paint every triangle with its own number as a colour instead of
 * its material, so a bad pixel names the draw that made it.
 *
 * CAVEAT, AND IT IS LOAD-BEARING: this option is also consulted in tex_apply
 * (see the fog_viz_option() || tri_id_option() test there), where it DISABLES
 * TEXTURING for every triangle. A TRI_ID capture is therefore useless as
 * evidence about anything the texture participates in - alpha, blend,
 * whether a surface is visible at all - because it removes the very thing
 * under test. It answers "which triangle owns this pixel IF texturing is
 * off", never "which triangle owns this pixel".
 *
 * B-051, 2026-08-27: read as proof that a quad covered a region in the
 * shipped image. It did not; with its texture the same quad writes nothing.
 * Caught only because a second probe (SL_GLASS_MARK) contradicted it on the
 * same frame. Pair it with a probe that leaves texturing alone.
 *
 * This bug spent ten attempts on "why does that patch look wrong" when the
 * answerable question is "which triangle is that, and how does its state
 * differ from the correct-looking one beside it". The ID is per frame and
 * starts at 1, so black is "nothing drew here".
 *
 * Useful well past fog: glass, decals and blending all have the same
 * which-draw-was-that problem. */
static int tri_id_option(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TRI_ID");
                  on = (v != NULL && *v != '0'); }
    return on;
}
static int fog_viz_option(void);   /* SL_FOG_VIZ, defined below */
static int tri_id_option(void);     /* SL_TRI_ID, defined below */
static int glassmark_on(void);      /* SL_GLASS_MARK, defined below */
static int g_cc_a_prim1;            /* tentative def; the real one is below */
/* B-051 THE FIX. The alpha-equation modes, needed by tex_apply above the
 * block that defines them - see the AEQ_ comment further down. */
#define AEQ_OFF       0
#define AEQ_REPLACE   1
#define AEQ_MODULATE  2
#define AEQ_ADD       3   /* fixed-function GL_ADD - the FALLBACK, and inexact
                           * whenever the multiplier is not 1. Reached only if
                           * the shader program cannot be built. */
#define AEQ_LERP      4   /* (TEX - B) * C + B, i.e. a lerp between B and the
                           * texel by C. GL_INTERPOLATE expresses this exactly,
                           * per-vertex C included. */
#define AEQ_SHADER    5   /* a genuine multiply-add. No fixed-function texenv
                           * expresses it; see the alpha shader below. */
#define AEQ_NMODE     6
static int alpha_eq_on(void);       /* SL_ALPHA_EQ, defined below */
static int aeq_mode_now(void);      /* ...and its classification */
static int aeq_shader_avail(void);  /* is the alpha program usable? */
static int aeq_mode_enabled(int mode); /* SL_AEQ_MODES attribution mask */
static void aeq_apply_env(int mode);/* per-draw state for LERP / SHADER */
/* Triangles that took the shader, and triangles that wanted it and could not
 * have it. The second must stay 0; if it ever moves, some draw is silently
 * running the inexact GL_ADD fallback. Declared here because tex_apply, which
 * is above the AEQ block, is where the fallback is taken. */
static unsigned g_aeq_shader_tris, g_aeq_shader_fallback_tris;
/* B-051 WORK ITEM 3. What the multiply-add draws - which is the tinted glass -
 * are actually made of: the vertex colour they are shaded by, and the decoded
 * texture they sample. The pane's final RGB is texel * shade on a combiner
 * whose RGB muxes are byte-identical to the ordinary room word, so these two
 * numbers are the whole answer to "should it be black". Nothing here is
 * hardcoded and nothing here feeds a draw - it is read-only telemetry. */
static int g_aeqrgb_vlo[3] = { 256, 256, 256 };
static int g_aeqrgb_vhi[3] = { -1, -1, -1 };
static int g_aeqrgb_tmax[3] = { -1, -1, -1 };
static int g_aeqrgb_tmean[3] = { -1, -1, -1 };
static int g_aeqrgb_amin = -1, g_aeqrgb_amax = -1;
/* B-051 GLASS GRADIENT. PRIM.a on the multiply-add draws is Rare's
 * calculatedopacity (propobj.c glassCalculateOpacity), so logging it beside
 * the alpha this renderer actually computes puts the GAME-SIDE curve and the
 * RENDERED curve on the same line and makes "does the port reproduce the
 * gradient" a subtraction rather than an argument.
 *
 * The reference curve, measured game-side on the owner's recording as portal
 * 70 crosses CullDist: 255 251 241 231 222 213 205 198 191 184 179 173 168
 * 163 159 155 151 147 144 141 138 135 133 130 128 126 124 123 121 over ~27
 * frames. A renderer that snaps rather than grades will show a step here. */
static int g_aeqrgb_plo = 256, g_aeqrgb_phi = -1;
/* A frame draws MANY panes at once, so a min/max over them is useless - it
 * reported prim[0,255] on every frame of the transition and told me nothing.
 * Keep the DISTINCT PRIM.a values instead: one pane's gradient is then
 * visible as a value walking down the set frame by frame, which a range can
 * never show. 32 is well above the ~20 panes facility draws at once. */
#define AEQ_PRIMSET_MAX 32
static unsigned char g_aeqrgb_pset[AEQ_PRIMSET_MAX];
static int g_aeqrgb_pset_n, g_aeqrgb_pset_over;
static unsigned g_aeqrgb_n, g_aeqrgb_notex;
/* SL_AEQ_RGB=1 reports the above PER FRAME instead of once at exit, so a
 * number can be tied to a specific capture rather than smeared over a whole
 * run - the aggregate mixes every pane in the level and answers nothing.
 *
 * INSTRUMENT FAULT, measured 2026-08-27, and it is the usual shape: a probe
 * that CANNOT FIRE while reading perfectly healthy. Every counter this line
 * prints is written by aeq_note_word(), which is called only inside
 * `if (aeq_stats_on())` at the draw site. So SL_AEQ_RGB=1 ALONE prints
 * NOTHING, ever - not "no glass drew", just silence - and the run looks
 * successful. Measured back to back on the same recording: SL_AEQ_RGB=1 gave
 * 0 report lines over 9000 frames, SL_AEQ_RGB=1 SL_AEQ_STATS=1 gave 3090.
 * ALWAYS SET SL_AEQ_STATS=1 WITH IT. An empty log is not evidence of absence. */
static int aeq_rgb_dbg(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_AEQ_RGB");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}

static int g_in_batch;

/* B-131. The DL triangle batch draws under a vertex program whose only job
 * is the RDP's interpolation rule for shade and fog - see shade_linear_bind
 * below, defined with the other programs. Bound here, and only here, so the
 * 2D pass, the model-array path (fixed-function lighting) and the redraw
 * depth pass never see it. */
static void shade_linear_bind(int on);

/* B-142 invariant. A batch opened with unit 0 OFF while the B-051 alpha
 * program (texenv mode 5) is still bound: that program samples u_tex
 * unconditionally, so such a batch paints the PREVIOUS draw's texture over
 * an untextured surface. Must read 0 on every frame (sl_tex census line);
 * it read 88 tris a frame at the Depot yard before tex_apply's untextured
 * returns dropped the mode (Gitea #27, the red floor). */
static unsigned g_plain_under_prog;
static unsigned g_plain_prog_reset;      /* B-142: untextured returns that
                                          * had to leave mode 5 */

static void batch_begin(void)
{
    if (!g_in_batch) { shade_linear_bind(1); glBegin(GL_TRIANGLES); g_in_batch = 1; }
}

static void batch_end(void)
{
    if (g_in_batch) { glEnd(); g_in_batch = 0; shade_linear_bind(0); }
}

/* ======================= textures ========================================
 *
 * Every bit layout below is from Zoinkity's notes, cited per command; nothing
 * here is inferred from the decomp. Root: /mnt/projects/goldeneye_docs/notes/
 * "GE Documentation".
 *
 *   FD settextureimage  ucode05.txt "FD rdp_settextureimage"
 *       fmt 0x00E00000 (rgba 0, yuv 1, ci 2, ia 3, i 4), depth 0x00180000
 *       (4/8/16/32bit), width-1 0x00000FFF; lower word segment 0x0F000000 +
 *       address 0x00FFFFFF.
 *   F5 settile          ucode05_old.txt "F5 rdp_settile" (ucode05.txt's own
 *       entry says only "MOVED TO SETTILE.HTM", and that file is not in the
 *       corpus - the _old note carries the table)
 *       upper: fmt 0x00E00000, size 0x00180000, line 0x0003FE00, tmem 0x1FF
 *       lower: tile 0x07000000, palette 0x00F00000, clamp/mirror/mask/shift
 *              t 0x00080000/0x00040000/0x0003C000/0x00003C00 and s
 *              0x00000200/0x00000100/0x000000F0/0x0000000F
 *   F2 settilesize      ucode05.txt "F2 rdp_settilesize"
 *       upper uls 0x00FFF000 / ult 0x00000FFF, lower tile 0x07000000,
 *       lrs 0x00FFF000 / lrt 0x00000FFF, and the note's own formulas
 *       width = (lrs-uls)/4 + 1, height = (lrt-ult)/4 + 1.
 *   F3 loadblock        ucode05.txt "F3 rdp_loadblock" - same field layout,
 *       lrs is a texel count and dxt lives in the low 12 bits.
 *   F0 loadtlut         ucode05.txt (the entry above F2): upper x0 = entry
 *       count, lower tile 0x07000000 / count 0x00FFF000.
 *   BB texture          ucode05.txt "BB rsp_uc05_texture": level 0x00003800,
 *       tile 0x00000700, on 0x000000FF, and lower word s 0xFFFF0000 /
 *       t 0x0000FFFF. Confirmed against the note's own worked example
 *       "BB002801 FFFFFFFF  ;on, level 5, tile 0, ignore ST".
 *   ED setscissor       ucode05.txt "ED rdp_setscissor": ulx 0x00FFF000 /
 *       uly 0x00000FFF, lrx / lry likewise, both in 10.2 fixed point.
 *   B9/BA setothermode_l/h - ucode05.txt punts these to SETOTHERMODE_L.HTM
 *       and SETOTHERMODE_H.HTM, neither of which exists in the corpus, and a
 *       corpus-wide `grep -rn "TEXTLUT\|texlut\|G_TT_"` over
 *       /mnt/projects/goldeneye_docs/notes comes back EMPTY. So the notes are
 *       silent and include/PR/gbi.h is the authority, exactly as it was for
 *       the Mtx interior layout above: gsSPSetOtherMode(cmd, sft, len, data)
 *       packs w0 = cmd<<24 | sft<<8 | len with the ALREADY-SHIFTED value in
 *       w1, G_MDSFT_TEXTLUT = 14, G_TT_NONE/RGBA16/IA16 = 0/2/3 << 14.
 *       Cross-checked against the notes' own examples in "Texture application
 *       (Rooms).txt": "BA001402 00100000  ;2 cycle when textured" is
 *       sft=0x14=20=G_MDSFT_CYCLETYPE, len=2, data=G_CYC_2CYCLE(1<<20), and
 *       "BA001001 00010000  ;images use lod" is sft=16=G_MDSFT_TEXTLOD,
 *       len=1, data=G_TL_LOD(1<<16). Both land exactly, so the packing is
 *       measured, not assumed.
 *
 * The FD pointer names ALREADY-INFLATED texels, not compressed ROM bytes.
 * texLoad sets `tex->data = pool->leftpos` and then inflates into that same
 * pool->leftpos (src/game/image.c:2461-2469), and it is tex->data that
 * tex.c:506 hands to gDPSetTextureImage. Because that is a code-reading
 * argument, it is also measured at runtime: sl_tex reports the Shannon
 * entropy of the source bytes for every texture it decodes. Deflate/huffman
 * output is ~8.0 bits/byte by construction; see the reported figure.
 *
 * The Globalimagetable trap (image_bank.c word-swaps that bank because it
 * holds display lists and declarations, not pixels) is handled structurally
 * rather than by an address test: a texture is only ever decoded from the
 * source of an F3 loadblock, whose FD address must resolve, be mincore()-
 * mapped for the full decoded size, and carry dimensions an F2 settilesize
 * agreed to. An un-patched declaration in the global bank names an image ID
 * (the ABCD tag), not an address, and fails the very first of those.
 */

/* ---- B-023: the command stream itself ----------------------------------
 *
 * SL_TEX_TRACE=1 logs every texture-related opcode as it is interpreted.
 * With the tile state and the FD state disagreeing about a texture's format,
 * neither the tile nor the FD can say WHY on its own - only the order the
 * commands actually arrived in can, and that order is what GoldenEye's
 * texTrySetTileState (src/game/tex.c:190) is allowed to punch holes in.
 * Compiled in unconditionally but gated on the environment variable, so a
 * headless run never evaluates an argument. */
static int textrace_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("SL_TEX_TRACE") != NULL;
    return on;
}
#define TEXTRACE(...) do { if (textrace_on()) { \
        fprintf(stderr, "sl_textrace: " __VA_ARGS__); \
        fprintf(stderr, "\n"); } } while (0)

#define OP_SETTIMG   0xFD
#define OP_SETTILE   0xF5
#define OP_TILESIZE  0xF2
#define OP_LOADBLOCK 0xF3
#define OP_LOADTLUT  0xF0
#define OP_TEXTURE   0xBB
#define OP_SETSCISS  0xED
#define OP_SETOTHL   0xB9
#define OP_SETOTHH   0xBA
#define OP_LOADSYNC  0xE6
#define OP_PIPESYNC  0xE7

/* ======================= 2D: rectangles and colour =========================
 *
 * The 3D path above draws through the DL's own matrices. Everything below
 * draws in SCREEN space: the HUD, the watch, the menus and all text. Layouts,
 * cited per command, root /mnt/projects/goldeneye_docs/notes/"GE Documentation".
 *
 *   E4 texrect / E5 texrectflip  ucode05.txt "E4 rdp_texrect" (l.693-714):
 *       one word carries a corner as 00FFF000 x / 00000FFF y in 10.2 fixed,
 *       the other the same plus tile 0x07000000. It is a 3-command sequence,
 *       not a 64-bit one: the s/t half arrives as B4 and the dsdx/dtdy half as
 *       B3 - "Also can be used following textrects to set the s/t upper data
 *       word. In this case, rsp_rdphalf_2 is used to generate the lower data
 *       word" (ucode05.txt "B4 rsp_uc05_rdphalf_1", l.345). The measured
 *       histogram agrees exactly: E4=225 B4=225 B3=225 in one frame.
 *
 *       WHICH word holds which corner: the note and this tree's own emitter
 *       DISAGREE, and the disagreement is recorded rather than resolved by
 *       preference. ucode05.txt labels E4's upper word "upper left" and its
 *       lower word "lower right"; its E5 entry labels them the other way
 *       round. gbi.h - which is what actually produces these words, since the
 *       game builds this list itself - packs w0 from the macro's 3rd/4th
 *       arguments and w1 from its 1st/2nd, and every call site passes the
 *       SMALLER coordinate first (title2.c:118 `0, (i+12)<<2, (320<<2)-1,
 *       ((i+13)<<2)-1`), so w0 holds the LOWER-RIGHT and w1 the upper-left -
 *       the note's E5 labelling, not its E4 labelling. Rather than pick, the
 *       decoder takes min/max of the two corners, which is correct under
 *       either reading, and COUNTS which way each command actually came out.
 *
 *       MEASURED, Facility, the menu frame that issues 225 of them:
 *       `sl_2d: rect-order w0=lower-right(gbi)=225 w0=upper-left(note)=0`.
 *       Every single command puts the larger coordinate in w0, so gbi.h's
 *       packing is what the RDP actually receives and ucode05.txt's E4 entry
 *       has its two corner labels transposed. Recorded here rather than acted
 *       on: min/max costs nothing and stays right if a list ever disagrees.
 *
 *       The macro reached is the plain `#else` one at gbi.h:4707 - neither
 *       F3D_OLD nor F3DEX_GBI_2E nor F3DEX_GBI_2 is defined by
 *       tools/native/build.sh's DEFS, and a tree-wide
 *       `grep -rn "F3D_OLD\|F3DEX_GBI_2E" src/ include/` finds them only
 *       inside gbi.h's own #ifdefs. That branch is also the one that emits
 *       G_RDPHALF_1 then G_RDPHALF_2 (0xB4 then 0xB3), matching the note.
 *
 *       Fixed-point scales differ between the three commands and are NOT the
 *       vertex ST scale: the rectangle is 10.2 (4 units per pixel), s/t are
 *       10.5 like vertex ST (32 units per texel), and dsdx/dtdy are s5.10
 *       (1024 == one texel per pixel). The last is measured from the callers:
 *       title2.c:118 passes `1 << 10` for a 1:1 blit.
 *
 *   F6 fillrect      ucode05.txt "F6 rdp_fillrect" (l.957): lower right in the
 *       upper word, upper left in the lower word, x at 0x00FFC000 with its
 *       fraction at 0x00003000 and y at 0x00000FFC with its fraction at
 *       0x00000003. gbi.h's non-F3DEX_GBI_2E gDPFillRectangle packs exactly
 *       those shifts (_SHIFTL(lrx,14,10) | _SHIFTL(lry,2,10)), so note and
 *       emitter agree here - unlike E4.
 *   F7 setfillcolour ucode05.txt "F7 rdp_setfillcolour" (l.974): 16-bit 5551
 *       in the low half, "if both are used it indicates a 32bit colour". Both
 *       forms occur in this tree - GPACK_RGBA5551 replicated into both halves
 *       (fr.c:947) and a packed RGBA8888 (fr.c:951) - so the note's own rule
 *       (halves equal => 5551) is what selects between them.
 *   FA setprimcolour ucode05.txt "FA rdp_setprimcolour" (l.1013): upper word
 *       0000FF00 minimum level / 000000FF level, lower word rrggbbaa.
 *   FB setenvcolour / F8 setfogcolour / F9 setblendcolour - ucode05.txt sends
 *       all three to its "RDP GSetColor" block (l.1075): lower word rrggbbaa.
 *   FC setcombine    ucode05.txt punts to SETCOMBINE.HTM, which is not in the
 *       corpus - the same hole F5 settile and B9/BA setothermode have. As
 *       there, ucode05_old.txt carries the table ("FC rdp_setcombine",
 *       l.1062): upper a0 0x00F00000 / c0 0x000F8000 / Aa0 0x00007000 /
 *       Ac0 0x00000E00 / a1 0x000001E0 / c1 0x0000001F, lower b0 0xF0000000 /
 *       b1 0x0F000000 / Aa1 0x00E00000 / Ac1 0x001C0000 / d0 0x00038000 /
 *       Ab0 0x00007000 / Ad0 0x00000E00 / d1 0x000001C0 / Ab1 0x00000038 /
 *       Ad1 0x00000007, with mux codes 1=texel0, 3=primitive, 4=shade,
 *       5=environment and "1F 0" for the wide slots.
 *
 *       Only the shape that matters for text is classified. The RDP computes
 *       (a - b) * c + d, so a zero multiplier makes the colour output exactly
 *       d. GoldenEye's text combiner is textrelated.c:189,
 *       `gDPSetCombineLERP(0,0,0,PRIMITIVE, TEXEL0,0,PRIMITIVE,0, ...)`:
 *       colour = primitive, alpha = texel0.a * prim.a. That is the note's own
 *       "Many fonts, for instance, use I images blended colour(prim)->alpha"
 *       (the FA entry). Without it a font is an intensity texture with no
 *       colour at all, which is why this sits with texrect rather than after.
 *   B6/B7 clear/setgeometrymode  ucode05.txt "B6 rsp_uc05_cleargeometrymode"
 *       (l.362) and "B7" (l.390). Decoded and applied under SL_CULL, which
 *       since #25 (2026-09-15) defaults to the hardware sense on every draw.
 *       The bits are real - see GEOM_CULL_FRONT - and the sense is settled
 *       against the cartridge. Everything measured about it is at
 *       g_geom_seen and cull_apply.
 *   FF setcolourimage / FE setdepthimage  ucode05.txt l.1141/l.1122, both the
 *       gsSetImage layout FD uses: width-1 in 0x00000FFF, address in the lower
 *       word. Not no-ops, and this is the one that bites: zbufClearCurrentPlayer
 *       (viewport.c:94-108) points the COLOUR image at the z buffer, fills it
 *       with GPACK_ZDZ, and only viSetupCurrentPlayerView (fr.c:726) points it
 *       back. Drawing that fill would paint the screen. FF also carries the
 *       framebuffer width, which is where the 2D ortho gets its screen size.
 *   E9 fullsync / E8 tilesync  ucode05.txt l.758/l.748 - "no options",
 *       nothing to do on this backend. Consumed so they stop reading as
 *       unhandled.
 *   03 movemem       ucode05.txt "03 rsp_uc05_movemem" (l.163): type in
 *       0x00FF0000, 0x80 viewport, 0x86-0x94 lights, 0x98-0x9E matrix rows.
 *       Consumed and its types reported; the viewport is not applied because
 *       the GL viewport is the whole window (sl_gfx_sdl.c:62) and a split
 *       screen has no meaning until there is more than one player.
 */

#define OP_MOVEMEM     0x03
#define OP_CLRGEOM     0xB6
#define OP_SETGEOM     0xB7
#define OP_RDPHALF_2   0xB3
#define OP_RDPHALF_1   0xB4
#define OP_TEXRECT     0xE4
#define OP_TEXRECTFLIP 0xE5
#define OP_TILESYNC    0xE8
#define OP_FULLSYNC    0xE9
#define OP_FILLRECT    0xF6
#define OP_SETFILLCOL  0xF7
#define OP_SETFOGCOL   0xF8
#define OP_SETBLENDCOL 0xF9
#define OP_SETPRIMCOL  0xFA
#define OP_SETENVCOL   0xFB
#define OP_SETCOMBINE  0xFC
#define OP_SETZIMG     0xFE
#define OP_SETCIMG     0xFF

/* Geometry-mode bits that could change a pixel here, from ucode05.txt
 * "B6 rsp_uc05_cleargeometrymode" (l.362) and "B7" (l.390). The note tabulates
 * two microcode columns, "1" (F3DEX) and "2" (F3DEX2); this build is column 1,
 * because gbi.h picks between the same two on F3DEX_GBI_2 (include/PR/gbi.h:350
 * vs :356) and a tree-wide `grep -rn "F3DEX_GBI_2" src/ tools/ include/ Makefile*`
 * finds it only inside gbi.h's and gs2dex.h's own #ifdefs - never defined by
 * tools/native/build.sh's DEFS. Column 1 and gbi.h's #else branch agree
 * exactly: cull front 0x1000, cull back 0x2000. */
#define GEOM_CULL_FRONT 0x00001000u
#define GEOM_CULL_BACK  0x00002000u

/* Colour-combiner mux codes, ucode05_old.txt's "FC rdp_setcombine" table. */
#define CC_COMBINED    0
#define CC_TEXEL0      1
#define CC_TEXEL1      2
#define CC_PRIM        3
#define CC_SHADE       4
#define CC_ENV         5
/* Alpha-mux only: gbi.h G_ACMUX_0 / G_ACMUX_1 (include/PR/gbi.h:482-483). */
#define CC_A_ONE       6
#define CC_A_ZERO      7

/* The RGB c slot is FIVE bits and reaches codes the four-bit a/b slots cannot:
 * ucode05_old.txt "FC rdp_setcombine" gives A = primitive alpha and C =
 * environment alpha, matching gbi.h:462,464 G_CCMUX_PRIMITIVE_ALPHA /
 * G_CCMUX_ENV_ALPHA. Both are per-DRAW scalars, which is what lets a lerp
 * whose factor is one of them be carried exactly by a texture environment
 * constant. SHADE_ALPHA (11) is deliberately absent: it is per-vertex. */
#define CC_C_PRIM_ALPHA 10
#define CC_C_ENV_ALPHA  12

/* gbi.h: G_IM_FMT_RGBA/YUV/CI/IA/I = 0..4, G_IM_SIZ_4b/8b/16b/32b = 0..3 */
#define GBI_FMT_RGBA 0
#define GBI_FMT_CI   2
#define GBI_FMT_IA   3
#define GBI_FMT_I    4

/* The decode DESTINATION buffer's shape. g_texbuf is TEX_MAX_DIM squared at
 * RGBA8, and what tex_acquire actually has to guarantee is that w * h * 4 fits
 * inside it - an AREA, not a per-axis limit. TEX_MAX_DIM had been enforced per
 * axis as a proxy for that, under the comment "GE's largest is far below
 * this". See the guard in tex_acquire for the texture that comment was wrong
 * about, and for where the per-axis bound comes from instead. */
#define TEX_MAX_DIM    256
#define TEX_MAX_TEXELS ((unsigned long) TEX_MAX_DIM * (unsigned long) TEX_MAX_DIM)
#define TEX_CACHE_N  256

struct sl_tile {
    unsigned fmt, siz, line, tmem, pal;
    unsigned cms, cmt;                          /* clamp/mirror bits */
    unsigned masks, maskt;                      /* F5, s/t wrap exponent */
#define SL_TX_CLAMP 0x2u   /* gbi.h:393 G_TX_CLAMP, within the 2-bit cms/cmt */
    unsigned shifts, shiftt;                    /* F5, s/t coordinate shift */
    unsigned uls, ult, lrs, lrt;                /* F2, 10.2 fixed */
    float fs, ft;      /* B-109: presentation-only fraction of a quarter
                        * texel dropped by the F2 encoding, [0,1). Nonzero
                        * ONLY when the command that set this tile carried
                        * registered phase metadata - see sl_f2_frac_set. */
};

/* ==================== B-109: FRACTIONAL F2 ORIGINS =======================
 *
 * An F2 settilesize origin is an integer quarter-texel (ucode05.txt
 * l.888-905: upper word 00FFF000 uls / 00000FFF ult; gbi.h:3446). A native
 * writer that ANIMATES that origin from a continuous phase - the shared
 * water updater, src/game/unk_092E50.c sub_GAME_7F092E50 - must truncate at
 * the command, exactly as the cartridge did. At the cartridge's 2-4-VI game
 * frames the phase advanced 0.5-1.0 quarter-texels per rendered image, so
 * the truncated origin moved on nearly every frame the player saw. At the
 * native delta=1 cadence the same truncation freezes the origin for several
 * PRESENTED frames and then jumps - measured at the owner's Dam mark: S
 * holds 3-4 presents, T holds 7-10 - which is the owner's stepped water.
 *
 * The fix is presentation metadata, not a changed command: the writer
 * registers here, keyed by the F2 command's own address, the full continuous
 * phase the integer field was truncated from. The command bytes in the
 * display list are byte-identical to what the cartridge issues; diagnostics,
 * parity comparisons and the __sgi build see nothing new. When the walk
 * decodes an F2 it consults this registry, VALIDATES that the registered
 * phase truncates to the very integers the command carries (a stale or
 * foreign entry therefore degrades to the quantized origin, never to a
 * wrong one), and keeps only the fraction. The texcoord path adds that
 * fraction to the origin it already subtracts, so the water's origin moves
 * every presented frame by exactly what the game's own phase advanced.
 *
 * Nothing here is water-specific: any registered animated F2 gets the same
 * treatment, and an unregistered F2 - every other settilesize in the game -
 * decodes precisely as before (fs = ft = 0 on every decode that finds no
 * valid entry). Deterministic: the fraction is game simulation state, not
 * wall-clock interpolation. The registered addresses are the static setup
 * arrays' own command slots, C globals whose addresses nothing ever reuses.
 *
 * SL_F2_FRAC=0 restores the integer-quantized presentation from the same
 * binary. Default on. */
#define F2FRAC_N 8
struct sl_f2frac { const void *cmd; float s_q, t_q; };
static struct sl_f2frac g_f2frac[F2FRAC_N];
static unsigned g_f2frac_n;

void sl_f2_frac_set(const void *cmd, float s_q, float t_q);
void sl_f2_frac_set(const void *cmd, float s_q, float t_q)
{
    unsigned i;
    for (i = 0; i < g_f2frac_n; i++)
        if (g_f2frac[i].cmd == cmd) {
            g_f2frac[i].s_q = s_q; g_f2frac[i].t_q = t_q; return;
        }
    if (g_f2frac_n < F2FRAC_N) {
        g_f2frac[g_f2frac_n].cmd = cmd;
        g_f2frac[g_f2frac_n].s_q = s_q;
        g_f2frac[g_f2frac_n].t_q = t_q;
        g_f2frac_n++;
    }
}

static const struct sl_f2frac *f2frac_find(const void *cmd)
{
    unsigned i;
    for (i = 0; i < g_f2frac_n; i++)
        if (g_f2frac[i].cmd == cmd) return &g_f2frac[i];
    return NULL;
}

static int f2frac_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_F2_FRAC");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* The source of the most recent F3 loadblock. Kept for the report, and as the
 * fallback when the tmem map below has never seen a tile's tmem address.
 *
 * ---- B-023: why "the most recent loadblock" is not the answer -------------
 *
 * This used to BE the answer. The reasoning was that GoldenEye loads through
 * tile 7 (G_TX_LOADTILE) and renders from tile 0, and that
 * texWriteLoadToTmemAddr emits the pair back to back - FD, F5(7), E6, F3(7),
 * E7, then F5(0)/F2(0) - so the image the last loadblock pulled in is what
 * the following triangles draw with. Reproducing the RDP's tmem allocator was
 * dismissed as buying nothing, on the grounds that a tile's tmem address is
 * set AFTER the load that fills it.
 *
 * Both halves are wrong, and the command stream says so. Traced off surface
 * with SL_TEX_TRACE=1, the sequence around the perimeter treeline is:
 *
 *     FD  addr=c83f5d08 fmt=3            <- an IA8 image
 *     F3  loadblock src=c83f5d08         <- NO F5 first: tile 7 still holds
 *                                           tmem=0 from the previous setup,
 *                                           so texTrySetTileState elided it
 *     FD  addr=c83f5150 fmt=0            <- an RGBA16 image
 *     F5  tile=7 tmem=128                <- the LOAD tile's tmem, set BEFORE
 *     F3  loadblock src=c83f5150             the F3, not after
 *     F5  tile=0 fmt=3 siz=1 line=4 tmem=0   <- tile 0 describes tmem 0
 *     F2  tile=0 -> 32x32                        i.e. the FIRST image
 *     F5  tile=1 fmt=0 siz=2 line=16 tmem=128 <- tiles 1..6 are the RGBA16
 *     F2  tile=1 -> 64x17                        image's LOD chain
 *     ... tiles 2..6 at tmem 400/472/492/498/500 ...
 *
 * So GoldenEye batches several loads into DIFFERENT tmem addresses and then
 * configures tiles across all of them. "The last loadblock" named c83f5150
 * while the tile actually being rendered from, tile 0, described c83f5d08 -
 * and the renderer decoded the RGBA16 image's bytes through the IA8 tile's
 * 32x32/8bpp description. That is the black-and-white static: a correct
 * decoder handed the wrong pairing, exactly like B-018 and B-020 before it.
 *
 * The dismissal was also self-refuting, and the evidence was already in the
 * report: c83f5d08 appears in the dxt!=0 table as "drawn 0x0 x0" - loaded
 * every frame and, under the old rule, never rendered from once.
 *
 * The tmem map below is the minimum that fixes it. A loadblock names its own
 * tile, that tile's tmem is set before the load, so recording tmem -> source
 * at the F3 is well-defined; a render tile then resolves through its own tmem
 * instead of through whatever happened to load last. dxt is recorded per
 * entry too, because it belongs to the load and not to the frame - here the
 * two images in flight at once were loaded with dxt=512 and dxt=0, so a
 * single global would give one of them B-020's swizzle rule backwards. */
static const unsigned char *g_load_src;
static unsigned g_loadblocks;

/* tmem word address -> the image a loadblock put there. Eight tiles can name
 * at most eight distinct addresses at once, and GE's LOD chains reuse one
 * load across several tiles, so this never needs to be large.
 *
 * ---- B-128: A LOAD IS A RANGE, AND A LATER LOAD OVERWRITES WHAT IT COVERS.
 *
 * The map used to answer a tile by the load that STARTED at its address, and
 * a later load replaced an entry only when it started at the same word. That
 * is not what tmem does. A loadblock fills [tmem, tmem + words); a tile that
 * names any address in that span reads that block's bytes at that offset,
 * and whatever an earlier load left there is gone.
 *
 * The witness is the bullet tracer (issue #16). Its flare is an RGBA32 16x32
 * image with six authored mip levels, loaded by texSelect
 * (othermodemicrocode.c:602-686, the level != 0 branch) as ONE 700-texel
 * block at tmem 0 under G_CC_TRILERP, tiles 1..5 at words 128/160/168/172/174
 * inside it - the layout the game's own generator documents, `images text
 * and font/7F076D68 - generate DL for ImgDecl.txt` :982-987 "tmem +=
 * tilesize" per level. A treeline segment drawn earlier in the same frame
 * (B-118: detail at word 0, its RGBA16 64x23 strip at word 128) left an entry
 * at 128, so the beam's tile 1 answered with the STRIP's bytes decoded as
 * RGBA32 8x16 - rainbow noise - and mode 9 blended that in as TEXEL1.
 * SL_TEX_DUMP at the shot: entry 181 src=2010f1c0 (the flare, level 0
 * decodes to the orange glow) and entry 182 src=200a6880 fmt=0 8x16 with
 * fd-base 2010f1c0 - the "tile 1" image is a different address from the
 * load that filled the tile. B-119's single-image test - "tile 1 resolves to
 * nothing" - was the same assumption from the other side: it held only while
 * nothing stale sat at that word. That is also what the c=13 census called
 * "unresolved" 99..674 tris per frame at Surface: stale hits whose two-image
 * decode then failed and fell to plain modulate without mips.
 *
 * Now: an entry carries its span in words and a sequence number; a lookup
 * returns the most recent load whose span contains the address, with the
 * DRAM byte offset the address stands for inside that load. Word size: the
 * same note's line rule gives "width to nearest word" as (width+3)>>2 for
 * BOTH 16-bit (:885-890) and 32-bit (:870-875), i.e. 4 texels per 64-bit
 * word at either depth - the RDP keeps a 32-bit texel's RG and BA halves at
 * one word address in its two banks (gbi.h:431 G_IM_SIZ_32b_LINE_BYTES=2
 * against :430 _BYTES=4), so a word stands for 16 DRAM bytes at 32bpp and 8
 * at every other depth (16 texels at 4bpp, 8 at 8bpp, 4 at 16bpp). Tile 1 of
 * a mip chain then resolves to its own block at its own level, and "same
 * load as tile 0" is the exact one-image-plus-mips discriminator. At a
 * load's own start - every tile this map answered before - the answer is
 * unchanged; containment only adds answers a start-match refused or, as
 * here, mis-answered. SL_TMEM_SPAN=0 restores the start-match. */
#define TMEM_MAP_N 16
struct sl_tmem_ent {
    unsigned tmem;
    const unsigned char *src;
    unsigned dxt, texels, fmt, siz;
    unsigned words;                  /* span in 64-bit tmem words           */
    unsigned seq;                    /* store order; higher is more recent  */
};
static struct sl_tmem_ent g_tmem[TMEM_MAP_N];
static unsigned g_tmem_n, g_tmem_seq;
static unsigned g_tmem_hit, g_tmem_miss;   /* how often the map answers */
/* B-128: render-tile hits that landed INSIDE a load rather than at its
 * start, and c=13 draws whose tiles 0 and 1 resolved to ONE load (one image
 * plus mips) - each a case the exact-start map could not express. */
static unsigned g_tmem_inner, g_lodmip_same;

/* DRAM bytes one tmem word address stands for, by texel size. */
static unsigned tmem_word_bytes(unsigned siz)
{
    return siz == 3u ? 16u : 8u;
}

static unsigned tmem_span_words(unsigned texels, unsigned siz)
{
    unsigned bytes;
    switch (siz) {
    case 0:  bytes = (texels + 1u) / 2u; break;
    case 1:  bytes = texels; break;
    case 2:  bytes = texels * 2u; break;
    default: bytes = texels * 4u; break;
    }
    return (bytes + tmem_word_bytes(siz) - 1u) / tmem_word_bytes(siz);
}

/* Record what a loadblock just moved into `tmem`. A later load to the same
 * address replaces the entry; a full table evicts the oldest. */
static void tmem_store(unsigned tmem, const unsigned char *src, unsigned dxt,
                       unsigned texels, unsigned fmt, unsigned siz)
{
    unsigned i;
    if (src == NULL) return;
    for (i = 0; i < g_tmem_n; i++)
        if (g_tmem[i].tmem == tmem) break;
    if (i == g_tmem_n) {
        if (g_tmem_n >= TMEM_MAP_N) {
            unsigned k, oldest = 0;
            for (k = 1; k < g_tmem_n; k++)
                if (g_tmem[k].seq < g_tmem[oldest].seq) oldest = k;
            i = oldest;
        } else {
            g_tmem_n++;
        }
    }
    g_tmem[i].tmem = tmem; g_tmem[i].src = src; g_tmem[i].dxt = dxt;
    g_tmem[i].texels = texels; g_tmem[i].fmt = fmt; g_tmem[i].siz = siz;
    g_tmem[i].words = tmem_span_words(texels, siz);
    g_tmem[i].seq = ++g_tmem_seq;
}

/* SL_TMEM_SPAN=0 restores the exact-start lookup from the same binary, for
 * an A/B against B-128. Default on. */
static int tmem_span_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TMEM_SPAN");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* The most recent load whose span contains `tmem`; *off receives the DRAM
 * byte offset of that address inside the load (0 at the load's own start). */
static const struct sl_tmem_ent *tmem_resolve(unsigned tmem, unsigned *off)
{
    unsigned i;
    const struct sl_tmem_ent *best = NULL;
    for (i = 0; i < g_tmem_n; i++) {
        const struct sl_tmem_ent *e = &g_tmem[i];
        if (!tmem_span_on()) {
            if (e->tmem == tmem) { best = e; break; }
            continue;
        }
        if (tmem < e->tmem || tmem >= e->tmem + e->words) continue;
        if (best == NULL || e->seq > best->seq) best = e;
    }
    if (off != NULL)
        *off = best ? (tmem - best->tmem) * tmem_word_bytes(best->siz) : 0u;
    return best;
}

static const struct sl_tmem_ent *tmem_find(unsigned tmem)
{
    return tmem_resolve(tmem, NULL);
}

/* ---- B-020: whether the loaded pixels carry the odd-row word swap --------
 *
 * The decoder's SL_TEX_FLAG_ROWSWAPPED used to be passed unconditionally,
 * which is right for world textures and wrong for font glyphs. The two differ
 * in exactly one field of the F3 loadblock: its dxt.
 *
 * On the RDP, a load steps a row counter by dxt per 64-bit word and swaps the
 * two 32-bit halves as it writes ODD rows into tmem; the texel fetch applies
 * the same swap again when t is odd. With a non-zero dxt the two cancel and
 * the DRAM image is plain. With dxt = 0 the load never advances a row, so
 * nothing is swapped going in while the fetch still swaps coming out - a net
 * odd-row swap, which Rare pre-compensates for in the asset itself
 * (texSwapAltRowBytes, src/game/image.c:2168, called from image.c:237, :277,
 * :289, :293, :987, :1036, :1047 and :1051).
 *
 * Which path emits which is source-cited, not inferred:
 *   - World textures: src/game/tex.c:514 and :530,
 *     `gDPLoadBlock(gdl++, 7, 0, 0, len - 1, 0)` - dxt is the literal 0.
 *   - ASCII font glyphs: src/game/textrelated.c:243 goes through
 *     gDPLoadTextureBlock, which include/gbi_extension.h:252 expands to
 *     `gDPLoadBlock(..., CALC_DXT(width, siz##_BYTES))` - never 0.
 * Font glyph data also never passes through image.c at all: it is romCopy'd
 * raw out of the font table (7F0ACBAC in the notes' "allocate and initialize
 * font tables"), so there is nothing to pre-compensate for and the plain
 * reading is the correct one.
 *
 * Measured before writing this: the 0x5E small-font glyphs decoded straight
 * from the ROM table at 0x2E63F0 as I8 with a (width + 7) & 0xF8 stride are
 * legible letters unswapped, and are the exact blocky half-formed shapes
 * B-020 describes when the swap is applied.
 *
 * ---- What the two call sites above got wrong ------------------------------
 *
 * "dxt != 0 means font glyph" is NOT true, and the pair of citations was the
 * whole evidence for it. A tree-wide
 * `grep -rn "gDPLoadTextureBlock\|gDPLoadBlock" src/ --include=*.c` finds
 * three more emitters that pass a NON-ZERO dxt to world texture data:
 *   - src/game/tex.c:638 and :652, in texWriteLoadToTmemZero, which computes
 *     `dxt = sub_GAME_7F0CCB38(tex)` (tex.c:398) rather than passing 0, and
 *     whose lutmodeindex != 0 branch loads a TLUT right after - so these are
 *     the CI8 images, not glyphs.
 *   - src/game/othermodemicrocode.c:463, `gDPLoadBlock(gdl++, G_TX_LOADTILE,
 *     0, 0, lrs, sp138)`, likewise computed. (Its twin at :600 passes 0.)
 * Measured on streets and archives, those account for every non-glyph dxt != 0
 * load in a gameplay frame: CI8 32x64 and 64x32 character and prop textures,
 * plus an I8 64x64. Facility's lone one is an IA image at c83c4d78 with a
 * 32-byte line and a 512-texel block, loaded every frame and never rendered
 * from (`drawn 0x0 x0` in the report below).
 *
 * The RULE still holds, and it holds for a better reason than the call-site
 * argument: dxt is the RDP parameter that decides whether the load-side swap
 * happens at all, so "dxt == 0 <=> net odd-row swap" is a statement about the
 * hardware, independent of which C function wrote the command. Rare's
 * pre-compensation is conditional to match - image.c:236 applies it only when
 * `arg2 == 1 && forcenumimages > 0`, and image.c:253-293 only when `!arg2` -
 * so it is not the case that every world texture is pre-swapped.
 *
 * Confirmed by looking rather than by argument, which is the only thing that
 * could: SL_TEX_ROWSWAP forces either reading and SL_TEX_DUMP_DXTN restricts
 * the dump to exactly this set. The CI8 32x64 character texture at c8448748
 * and the I8 64x64 at c83eb158 are coherent images unswapped and shred into
 * the familiar odd-row stripes when the swap is forced on. So these loads
 * want no swap, the current rule gives them none, and no further
 * discriminator is needed. */
static unsigned g_load_dxt;
static unsigned g_load_dxt0, g_load_dxtn;    /* loads by kind, for telemetry */
static unsigned g_load_texels;               /* F3's lrs, a texel count       */

/* The evidence for the block above: every distinct dxt != 0 load in the frame,
 * with what the FD said and whether anything ever drew from it. Small fixed
 * table, deduplicated by source address, no allocation. */
#define DXTN_MAX 24
static const unsigned char *g_dxtn_src[DXTN_MAX];
static unsigned g_dxtn_fmt[DXTN_MAX], g_dxtn_siz[DXTN_MAX], g_dxtn_fdw[DXTN_MAX];
static unsigned g_dxtn_dxt[DXTN_MAX], g_dxtn_texels[DXTN_MAX];
static unsigned g_dxtn_w[DXTN_MAX], g_dxtn_h[DXTN_MAX], g_dxtn_draws[DXTN_MAX];
static unsigned g_dxtn_n;

/* Recorded at the F3 itself, not where a tile consumes it: in a Facility frame
 * with no text on screen the counter says one such load and NOTHING renders
 * from it, so a table filled at tile-resolve time is empty exactly when the
 * open question is being asked. Deduplicated by source address. */
static void dxtn_load(const unsigned char *src, unsigned fmt, unsigned siz,
                      unsigned fdw)
{
    unsigned i;
    for (i = 0; i < g_dxtn_n; i++) if (g_dxtn_src[i] == src) return;
    if (g_dxtn_n >= DXTN_MAX) return;
    i = g_dxtn_n++;
    g_dxtn_src[i] = src;
    g_dxtn_fmt[i] = fmt;  g_dxtn_siz[i] = siz;  g_dxtn_fdw[i] = fdw;
    g_dxtn_dxt[i] = g_load_dxt; g_dxtn_texels[i] = g_load_texels;
    g_dxtn_w[i] = g_dxtn_h[i] = g_dxtn_draws[i] = 0;
}

/* ...and again where a tile turns it into pixels, so "loaded but never drawn"
 * is distinguishable from "drawn". */
static void dxtn_drawn(const unsigned char *src, unsigned w, unsigned h)
{
    unsigned i;
    for (i = 0; i < g_dxtn_n; i++)
        if (g_dxtn_src[i] == src) {
            g_dxtn_w[i] = w; g_dxtn_h[i] = h; g_dxtn_draws[i]++;
            return;
        }
}

static struct sl_tile g_tile[8];

/* Current FD texture image. */
static unsigned              g_ti_fmt, g_ti_siz, g_ti_w;
static const unsigned char  *g_ti_addr;

/* Current TLUT source and its type (gbi.h G_TT_*: 0 none, 2 rgba16, 3 ia16).
 * g_tlut_addr points at the PALETTE, which is not the same address as the FD
 * image - see the F0 loadtlut case for why, and g_tlut_n/g_tlut_off for what
 * the command said. */
static const unsigned char  *g_tlut_addr;
static unsigned              g_tlut_mode;
static unsigned              g_tlut_n;      /* entries the F0 asked for */
static unsigned              g_tlut_off;    /* its byte offset from the FD  */
static unsigned              g_tlut_cmds, g_tlut_bad, g_tlut_off0;

/* B-051 STAGE 2. Per vertex SLOT, mirroring the RSP's own timing: whether
 * G_LIGHTING was in force when the slot was loaded, and the last three Vtx
 * bytes decoded as a SIGNED normal. The raw bytes stay untouched in g_vbuf -
 * this is an added reading of them, not a replacement, so nothing that reads
 * the colour today changes. gbi.h:364-365 gives G_LIGHTING 0x00020000 and
 * G_TEXTURE_GEN 0x00040000. */
/* Tentative declarations: the geometry mode and the combiner words are defined
 * with the commands that set them, further down. Same C idiom the file already
 * uses elsewhere; repeated tentative definitions of one static object are
 * allowed and resolve to the single later definition. */
static unsigned g_geom_mode;
static unsigned g_cc_w0, g_cc_w1;
static unsigned long g_vsrc[32];
static unsigned long g_vbase[32];

#define G_GEOM_LIGHTING     0x00020000u
#define G_GEOM_TEXTURE_GEN  0x00040000u
#define G_GEOM_TEXGEN_LIN   0x00080000u
static unsigned char g_vlit[32];
static signed char   g_vnrm[32][3];
static unsigned      g_vtx_lit_loads;
/* B-051 STAGE 4. The RSP's shade colour for a lit vertex, computed WHERE THE
 * RSP COMPUTES IT - at vertex load, under the matrix and the light state in
 * force at that moment - for the same reason vtx_transform and the texgen
 * generator run there (B-021: a later matrix does not reach back). Written
 * whenever G_LIGHTING is set; READ only under SL_LIGHT. */
static unsigned char g_vlrgb[32][3];
static unsigned char g_vlrgb_ok[32];

/* B-051 STAGE 3. The reflectance basis, and the coordinates generated from it.
 *
 * g_vgen[] holds the PRE-SCALE generated coordinate, 0..32768, produced where
 * the RSP produces it: at vertex load, from the normal, under the matrix in
 * force at that moment. The gSPTexture scale is applied at DRAW, where the
 * texture and its dimensions are known - see the emit path.
 *
 * WHY 0..32768 is the pre-scale domain, and it is not a guess: the pane
 * declares scale 3456 over a 54x54 map, and
 *     32768 * 3456 / 65536 / 32 = 54.0   exactly
 * so a full -1..+1 reflection sweep lands exactly on the 54-texel image. Any
 * other domain misses it. The /32 is the S10.5 texel fraction the notes settle
 * by worked example ("Absolute Basic Rendering Models.txt", see SL_ST_SCALE).
 *
 * DEFAULT OFF. SL_TEXGEN=1 arms it; without it g_vgen is written and never
 * read, exactly like stages 1 and 2. */
/* B-085. THE BASIS IS RESIDENT STATE, AND IT DOES NOT START AT ZERO.
 *
 * The two reflectance vectors live in RSP DMEM and PERSIST across display
 * lists: gSPLookAt writes them, and every later G_TEXTURE_GEN draw generates
 * against whatever is currently there, whether or not that draw issued a
 * gSPLookAt of its own. This renderer started them at all zeros and refused
 * to generate until an upload arrived, which is not "no basis" - it is a
 * DEGENERATE one. Both dot products are identically 0 for every normal, so
 * every generated coordinate collapses to the midpoint and the entire surface
 * samples a single texel.
 *
 * MEASURED, this is exactly the Nintendo logo. front.c:1688 issues
 * gSPNumLights and two gSPLight and NO gSPLookAt, so at its vertex loads
 * g_lookat_seen was 0 and texgen declined:
 *
 *   sl_texgen: DECLINED - geom has TEXTURE_GEN (mode=00062205) but the
 *              reflectance basis is incomplete (lookat_seen=0, cmds=0).
 *
 * with all 32 recorded surfaces reading basis=ABSENT, every one of 3036
 * vertices carrying the stored s=t=0, and the whole 32x32 I8 chrome ramp
 * therefore sampled at texel (0,0) under (TEXEL0 - 0) * SHADE + 0. The logo
 * drew as one flat tone. The texture itself decodes correctly - that was
 * checked, not assumed: rgbmean 82,82,82 over 1024 texels with a live alpha
 * range - so nothing about the bytes, the byte order, the format or the
 * upload is at fault. Only the coordinate is.
 *
 * WHY THE IDENTITY, and why these exact numbers. The sibling screen settles
 * it. constructor_menu04_goldeneyelogo (front.c:1964) builds its basis with
 *     guLookAtReflect(&m, la, 0,0,4000, 0,0,0, 0,1,0)
 * and constructor_menu01_nintendo (front.c:1739) aims its camera with
 *     matrix_4x4_set_lookat_target(&m, 0,0,4000, 0,0,0, 0,1,0)
 * - the SAME eye, target and up. What the Rare code actually uploads for that
 * camera was measured live rather than derived, and it is the identity:
 *
 *   sl_texgen:  lookR=(+127,  +0,  +0)  lookU=(  +0,+127,  +0)
 *
 * so seeding the identity is the basis the game itself installs one screen
 * later for the same view, not an invented constant. FTOFRAC8 puts +-1 at
 * +-127 (src/libultra/gu/lookatref.c), which is where the 127 comes from.
 *
 * SCOPE. This can only change a draw that sets G_TEXTURE_GEN BEFORE any
 * gSPLookAt has been issued; a real upload overwrites the seed and every
 * level that lights environment-mapped surfaces issues one (bg.c:1393,
 * gunfire.c:1526 and :1748). Facility measured 0 declines both before and
 * after, which is the containment claim stated as a number rather than as an
 * argument.
 *
 * NOT SOURCED, and deliberately recorded as such: the microcode's own DMEM
 * initialiser is not in this tree and the notes corpus is silent on texgen
 * and on gSPLookAt entirely (searched; see the doc-routing not_covered
 * entry). The claim being made is the narrow one - that the resident basis
 * for these logo screens is the identity, measured off the game's own upload
 * for the identical camera - and NOT the general one that F3D boots with an
 * identity lookat.
 *
 * SL_TEXGEN_DEFBASIS=0 restores the zero basis and the decline, from the same
 * binary. g_lookat_cmds still counts only REAL uploads, and g_lookat_default
 * stays set until one arrives, so the diagnostics keep telling the truth
 * about where the basis came from rather than being blinded by the seed. */
static signed char g_lookat[2][3] = { { 127, 0, 0 }, { 0, 127, 0 } };
static unsigned    g_lookat_seen = 3u, g_lookat_cmds;
static unsigned    g_lookat_default = 1u;   /* no real gSPLookAt yet */

static int texgen_defbasis(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEXGEN_DEFBASIS");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* The basis as the TEXGEN PATH SHOULD SEE IT. Kept as one function so the
 * decline check, the generator and the basis reporting cannot drift apart:
 * with the seed disarmed and no real upload yet, this is 0 and every consumer
 * takes the old declining path unchanged. */
static unsigned lookat_seen_eff(void)
{
    if (g_lookat_default && !texgen_defbasis()) return 0u;
    return g_lookat_seen;
}
static float       g_vgen[32][2];
static unsigned char g_vgen_ok[32];
static unsigned    g_texgen_verts, g_texgen_declined;
/* B-090. Vertices whose TRANSFORMED normal has zero length under
 * G_TEXTURE_GEN - the only population this file's texgen path treats
 * specially. Reported with the texture census rather than behind its own
 * flag, for B-088's reason: a count of zero and a count that was never
 * taken look identical in a log. Measured over one boot chain: 0 through
 * LEGAL and NINTENDO, 290 during RAREWARE (none of them reached by a
 * drawn G_TEXTURE_GEN triangle - that screen's textured-generated draws
 * measure 0 declining vertices in both arms), 1955 during
 * GOLDENEYE_LOGO. */
static unsigned    g_texgen_zeronrm;

/* SL_TEXGEN_SWAP=1: S <- lookat slot 1, T <- lookat slot 0. DEFAULT OFF.
 *
 * THE ARM, NOT A FIX. What is SOURCED is slot -> VECTOR:
 * src/libultra/gu/lookatref.c writes l[0].dir = Right and l[1].dir = Up, and
 * gbi.h's gSPLookAt (:2738) sends LookAtX (0x84) at `la` and LookAtY (0x82)
 * at `la + 16`, so 0x84 carries Right and 0x82 carries Up. What is NOT
 * sourced anywhere - not the notes corpus (see the doc-routing not_covered
 * entry), not gbi.h, not the SGI sources in this tree - is slot -> TEXTURE
 * AXIS. "S is the Right dot" was ASSUMED from the naming, never measured.
 * This toggle is the assumption's negation, so the pair can be measured
 * against the cartridge instead of argued from a name. */
static int vtx_nrm_window(void);      /* defined with the stage-2 probe */

static int texgen_swap(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEXGEN_SWAP");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}

/* ===================== B-051 STAGE 4: RSP LIGHTING ======================
 *
 * CENSUS BEFORE PIXELS. Everything below is CAPTURE and REPORT; the one
 * behaviour change is guarded by SL_LIGHT and defaults OFF.
 *
 * What arrives, and where it comes from (gbi.h, the plain-F3D branch this
 * build compiles - `grep -rn F3DEX_GBI src/ include/` finds those macros only
 * inside gbi.h's own #ifdefs, never defined by tools/native/build.sh):
 *
 *   G_MV_L0 = 0x86 ... G_MV_L7 = 0x94   (:1273-1280), stride 2 per light
 *   gSPLight(pkt, l, n) => movemem type ((n)-1)*2 + G_MV_L0, 16 bytes (:2560)
 *   Light_t (:1398): unsigned char col[3]; char pad1; unsigned char colc[3];
 *                    char pad2; signed char dir[3]; char pad3;
 *   Ambient_t (:1407): col[3], pad, colc[3], pad - NO direction.
 *   gSPNumLights(n) => moveword G_MW_NUMLIGHT 0x02, offset 0 (:2533, :1297)
 *   "the highest numbered light is always the ambient light" (:2550-2553)
 *
 * So gSPSetLights1 (:2593) sends the DIRECTIONAL light as LIGHT_1 -> 0x86 and
 * the AMBIENT as LIGHT_2 -> 0x88. The census the previous round took over
 * 2600 boot frames - 0x86 x791, 0x88 x791, never 0x8a - is exactly that
 * shape, and it is the whole of the lighting this renderer ever receives.
 *
 * The game's own values, for a known-positive to be checked against:
 *   bg.c:292   GlobalLight = gdSPDefLights1(150,150,150, 255,255,255,
 *                                           77,77,46)   - the WORLD light,
 *              issued by bgLevelRender (bg.c:1392) immediately before the
 *              gSPLookAt on the next line. That is the pane's light.
 *   gun.c:95   g_WeaponEnvmapLight = gdSPDefLights1(0x96,0x96,0x96,
 *                                    0xff,0xff,0xff, 0xb2,0x4d,0x2e)
 *   title.c:93 gunbarrelLights = gdSPDefLights1(0xDC,0xDC,0xDC,
 *                                    0xFF,0xFF,0xFF, 0x00,0x7F,0x00)
 * A dump that does not reproduce one of those three is reading the wrong
 * bytes, which is the failure this census exists to expose. */
#define SL_MAX_LIGHTS 8
struct sl_light { unsigned char col[3], colc[3]; signed char dir[3]; };
static struct sl_light g_light[SL_MAX_LIGHTS];   /* index 0 == G_MV_L0 */
static unsigned        g_light_seen;             /* bitmask of slots loaded */
static unsigned        g_numlights = 1;          /* G_MW_NUMLIGHT, NUML()   */
static unsigned        g_numlight_cmds, g_light_cmds;

/* Distinct lighting STATES issued this run, so "the pane's light is
 * GlobalLight" is a measurement and not a reading of bg.c. Keyed on the whole
 * state - numlights, every loaded slot's colour and direction - so two
 * different lights cannot collapse into one row. */
#define LT_CENSUS_N 16
static struct { unsigned numl, seen; struct sl_light l[SL_MAX_LIGHTS];
                unsigned n; } g_lt[LT_CENSUS_N];
static unsigned g_lt_n, g_lt_over;

static int light_on(void)
{
    static int on = -1;
    /* DEFAULT ON. Under G_LIGHTING the vertex bytes at 0x0C-0x0F are SIGNED
     * NORMALS, not colour - read signed they are unit vectors in FTOFRAC8
     * (magnitude ~126.6 across facility's pane, ~126.1 on boot-logo geometry),
     * which colour has no reason to be. The old default fed those bytes
     * straight to glColor, so the texture was multiplied by a direction
     * vector: on the pane that is a strong R>G>B cast AND a suppression of the
     * environment map's own variation.
     *
     * The equation is derived from gbi.h and the tree's own SGI sources, not
     * fitted: ambient.col + sum col_i * max(0, n_hat . d_hat_i), saturating at
     * 255, alpha untouched. NO CONSTANT WAS TUNED - facility's steady state is
     * one white directional plus a 150-grey ambient (GlobalLight in bg.c, byte
     * for byte), so the equation PREDICTS a neutral grey in [150,255] before a
     * pixel is drawn, and the pane's four vertices resolve to 150/246/150/150.
     * Measured pane mean moves 21.8,18.9,14.1 (cast) to 27.4,27.4,27.4 (none).
     *
     * Note this is inert without texgen: with no generated coordinates the
     * pane samples black, and black times any shade is black. The two flips
     * are independent switches but one visible change.
     *
     * SL_LIGHT=0 restores the raw normal-as-RGB path for A/B. */
    if (on < 0) { const char *v = getenv("SL_LIGHT");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* The equation, and what each step is sourced from.
 *
 * DOCUMENTED (gbi.h, this tree's own SGI headers):
 *   - the ambient light has col[] and NO direction (Ambient_t, :1407), and is
 *     the highest-numbered light (:2550-2553);
 *   - a directional light has col[] and a NORMALISED signed dir[] (:1398,
 *     "direction of light (normalized)"), FTOFRAC8 in lookatref.c putting
 *     +/-127 at +/-1.0;
 *   - the Vtx union's lit member is cn[4] = nx, ny, nz, ALPHA, so the fourth
 *     byte is alpha under G_LIGHTING exactly as it is without it. Alpha
 *     therefore needs no change at all, and this code makes none.
 *
 * NOT DOCUMENTED ANYWHERE IN REACH, and stated here as INFERRED so it can be
 * contradicted by measurement:
 *   - that the accumulation is ambient + sum(col_i * max(0, n.d_i)),
 *   - that the clamp against zero is a clamp rather than an abs,
 *   - that the sum saturates at 255 rather than wrapping,
 *   - the normalisation ORDER, which is the same open question texgen has
 *     (SL_TEXGEN_MODE) and is blocked on the same grounds. This uses the
 *     texgen mode-0 convention - normalise the transformed normal, then dot
 *     the uploaded direction - deliberately, so the two paths cannot disagree
 *     with each other and confound the arm.
 *
 * n . (M^T d) == (M n) . d is unconditional transpose algebra, so the
 * TRANSFORM is not in question here either; only where the unit-length step
 * falls.
 *
 * NOTHING IS TUNED. There is no scale factor, no gamma, no fudge: the inputs
 * are the bytes the game uploaded and the output is whatever they produce. If
 * that lands somewhere other than the cartridge, the number to report is
 * where it landed. */
static void light_vertex(const signed char *nrm, const float *m,
                         unsigned char *out /* [3] */)
{
    float e[3], len, acc[3];
    unsigned i;
    int j;
    for (j = 0; j < 3; j++)
        e[j] = (float) nrm[0] * m[j] + (float) nrm[1] * m[4 + j]
             + (float) nrm[2] * m[8 + j];
    len = sqrtf(e[0]*e[0] + e[1]*e[1] + e[2]*e[2]);
    if (len < 1e-6f) len = 1.0f;
    for (j = 0; j < 3; j++) e[j] /= len;

    /* Ambient first. NUML(n) is n for n>=1, so the ambient slot is index
     * g_numlights (0-based over G_MV_L0..): with one directional light the
     * ambient is G_MV_L1 = slot 1. If that slot never arrived, there is no
     * ambient term - not a default one. */
    if (g_numlights < SL_MAX_LIGHTS && (g_light_seen & (1u << g_numlights))) {
        acc[0] = (float) g_light[g_numlights].col[0];
        acc[1] = (float) g_light[g_numlights].col[1];
        acc[2] = (float) g_light[g_numlights].col[2];
    } else {
        acc[0] = acc[1] = acc[2] = 0.0f;
    }
    for (i = 0; i < g_numlights && i < SL_MAX_LIGHTS; i++) {
        float d[3], dl, dot = 0.0f;
        if (!(g_light_seen & (1u << i))) continue;
        d[0] = (float) g_light[i].dir[0];
        d[1] = (float) g_light[i].dir[1];
        d[2] = (float) g_light[i].dir[2];
        dl = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (dl < 1e-6f) continue;
        for (j = 0; j < 3; j++) dot += e[j] * (d[j] / dl);
        if (dot <= 0.0f) continue;
        acc[0] += (float) g_light[i].col[0] * dot;
        acc[1] += (float) g_light[i].col[1] * dot;
        acc[2] += (float) g_light[i].col[2] * dot;
    }
    for (j = 0; j < 3; j++) {
        if (acc[j] > 255.0f) acc[j] = 255.0f;
        if (acc[j] < 0.0f)   acc[j] = 0.0f;
        out[j] = (unsigned char) (acc[j] + 0.5f);
    }
}

static void light_note(void)
{
    unsigned i, j;
    for (i = 0; i < g_lt_n; i++) {
        if (g_lt[i].numl != g_numlights || g_lt[i].seen != g_light_seen)
            continue;
        if (memcmp(g_lt[i].l, g_light, sizeof g_light) != 0) continue;
        g_lt[i].n++;
        return;
    }
    if (g_lt_n >= LT_CENSUS_N) { g_lt_over++; return; }
    j = g_lt_n++;
    g_lt[j].numl = g_numlights; g_lt[j].seen = g_light_seen;
    memcpy(g_lt[j].l, g_light, sizeof g_light);
    g_lt[j].n = 1;
}

static unsigned g_lvtx_reported;

/* Per-vertex known-positive. Prints the EXACT lighting state that produced a
 * vertex, alongside the raw bytes, so "the pane's four vertices" can be a
 * table rather than a claim. Bounded by the same SL_VTX_NRM_FIRST/LAST record
 * window every other probe here uses, because without it the budget is spent
 * on whatever lit geometry loads first, which is never the surface under
 * investigation. */
static void light_vtx_note(unsigned k, const signed char *nrm,
                           const unsigned char *rgb, unsigned char alpha)
{
    unsigned i;
    if (getenv("SL_LIGHT_DBG") == NULL || g_lvtx_reported >= 48u) return;
    if (!vtx_nrm_window()) return;
    g_lvtx_reported++;
    fprintf(stderr, "sl_lvtx: slot=%u  n=(%+4d,%+4d,%+4d) |n|=%6.2f"
                    "  alpha=%3u  geom=%08x%s\n",
            k, nrm[0], nrm[1], nrm[2],
            sqrtf((float)(nrm[0]*nrm[0] + nrm[1]*nrm[1] + nrm[2]*nrm[2])),
            alpha, g_geom_mode,
            (g_geom_mode & G_GEOM_TEXTURE_GEN) ? " TEXTURE_GEN" : "");
    fprintf(stderr, "         numlights=%u seen=0x%02x", g_numlights,
            g_light_seen);
    for (i = 0; i < SL_MAX_LIGHTS; i++) {
        if (!(g_light_seen & (1u << i))) continue;
        /* The ambient slot is an Ambient_t (gbi.h:1407): col, colc, and NO
         * direction. Printing dir[] for it would print the two pad bytes and
         * whatever follows, and an earlier draft did exactly that - it read
         * "dir=-1,-1,-1", which invites the conclusion that the ambient has a
         * direction pointing somewhere. It does not, and light_vertex() never
         * reads it (the accumulation loop runs i < g_numlights). */
        if (i == g_numlights)
            fprintf(stderr, "  L%u{col=%3u,%3u,%3u AMBIENT, no direction}", i,
                    g_light[i].col[0], g_light[i].col[1], g_light[i].col[2]);
        else
            fprintf(stderr, "  L%u{col=%3u,%3u,%3u dir=%+4d,%+4d,%+4d}", i,
                    g_light[i].col[0], g_light[i].col[1], g_light[i].col[2],
                    g_light[i].dir[0], g_light[i].dir[1], g_light[i].dir[2]);
    }
    fprintf(stderr, "\n         drawn-as-colour(TODAY)=%3u,%3u,%3u"
                    "   RSP-lit=%3u,%3u,%3u\n",
            (unsigned char) nrm[0], (unsigned char) nrm[1],
            (unsigned char) nrm[2], rgb[0], rgb[1], rgb[2]);
}

/* Tentative declarations, same idiom as g_cc_w0 / g_geom_mode above: these are
 * defined with the commands that set them, further down the file. */
static unsigned g_tex_sscale, g_tex_tscale;
static unsigned g_st_tex_w, g_st_tex_h;

/* THE STATE-LEVEL POSITIVE CONTROL, and it comes before any pixel is looked
 * at: SL_TEXGEN_DBG=1 prints the normal, both dot products and the resulting
 * pre-scale coordinate for every generated vertex, deduped. If the four pane
 * vertices do not produce four DISTINCT coordinates spanning a sensible part
 * of the map, the generator is wrong and no capture is worth taking. */
static unsigned g_texgen_reported;

static void texgen_note(unsigned k, float ds, float dt,
                        const float *nraw, const float *nunit)
{
    const float *m = g_mv[g_mv_sp];
    float cl[3];
    int j;
    if (getenv("SL_TEXGEN_DBG") == NULL || g_texgen_reported >= 64u) return;
    if (!vtx_nrm_window()) return;
    g_texgen_reported++;
    /* THE NORMALISATION QUESTION, MEASURED RATHER THAN ARGUED.
     *
     * n.(M^T d) == (M n).d is unconditional transpose algebra, so the
     * TRANSFORM is not where a discrepancy can enter. Normalisation PLACEMENT
     * is: normalize(Mn).d is not generally normalize(n).(M^T d), because
     * |Mn| varies per normal under non-uniform scale. It only closes if the
     * matrix is rotation plus UNIFORM scale, so print the three column
     * lengths of the upper 3x3 and let the numbers say. Equal lengths ->
     * uniform -> the placement cannot matter for these draws. */
    for (j = 0; j < 3; j++)
        cl[j] = sqrtf(m[j*4+0]*m[j*4+0] + m[j*4+1]*m[j*4+1]
                    + m[j*4+2]*m[j*4+2]);
    {
        float sc = (float) g_tex_sscale / 65536.0f;
        float tc = (float) g_tex_tscale / 65536.0f;
        float texS = g_vgen[k][0] * sc / (float) SL_ST_SCALE;
        float texT = g_vgen[k][1] * tc / (float) SL_ST_SCALE;
        fprintf(stderr,
            "sl_texgen: slot=%u\n"
            "    lookR=(%+4d,%+4d,%+4d)  lookU=(%+4d,%+4d,%+4d)\n"
            "    n_raw=(%+4d,%+4d,%+4d)  |n_raw|=%6.2f\n"
            "    n_xf =(%+8.2f,%+8.2f,%+8.2f)  |n_xf|=%8.3f\n"
            "    n_hat=(%+7.4f,%+7.4f,%+7.4f)\n"
            "    dotR=%+7.4f  dotU=%+7.4f\n"
            "    gen  =(%8.1f,%8.1f)  pre-scale 0..32768\n"
            "    scale=(0x%04x,0x%04x)  ->  texel S=%7.2f T=%7.2f\n"
            "    (tile at LOAD time was %ux%u - NOT this draw's tile, which"
            " is resolved later in tex_apply; the emit path uses the right"
            " one, this line does not. UV omitted here for that reason.)\n"
            "    mv column lengths = %.4f %.4f %.4f  -> %s\n",
            k,
            g_lookat[0][0], g_lookat[0][1], g_lookat[0][2],
            g_lookat[1][0], g_lookat[1][1], g_lookat[1][2],
            g_vnrm[k][0], g_vnrm[k][1], g_vnrm[k][2],
            sqrtf((float) (g_vnrm[k][0]*g_vnrm[k][0]
                         + g_vnrm[k][1]*g_vnrm[k][1]
                         + g_vnrm[k][2]*g_vnrm[k][2])),
            nraw[0], nraw[1], nraw[2],
            sqrtf(nraw[0]*nraw[0]+nraw[1]*nraw[1]+nraw[2]*nraw[2]),
            nunit[0], nunit[1], nunit[2],
            ds, dt, g_vgen[k][0], g_vgen[k][1],
            g_tex_sscale, g_tex_tscale, texS, texT,
            g_st_tex_w, g_st_tex_h,
            cl[0], cl[1], cl[2],
            (fabsf(cl[0]-cl[1]) < 1e-4f && fabsf(cl[1]-cl[2]) < 1e-4f)
                ? "UNIFORM (normalisation placement cannot matter)"
                : "NON-UNIFORM (placement matters - investigate)");
    }
}

static int texgen_on(void)
{
    static int on = -1;
    /* DEFAULT ON. The old default was semantically wrong, not merely
     * incomplete: G_TEXTURE_GEN was tracked and never acted on, so an
     * environment-mapped surface - which carries s=t=0 and expects the RSP to
     * manufacture its coordinates from the vertex normals - sampled texel
     * (0,0) across its whole extent. On facility's tinted glass that texel is
     * rgb 0,0,0, which is the flat dark pane. The gSPTexture S/T scale and the
     * LOOKATX/Y basis were discarded on the same path, so even the bit's
     * inputs never arrived.
     *
     * The mapping is derived rather than documented - texture generation
     * happens inside the microcode - but it made a falsifiable prediction and
     * survived it: 32768 * scale / 65536 / 32 = texture width, derived on this
     * pane (3456 -> 54.0), predicted egypt's 32x32 must carry scale 2048, and
     * egypt returns exactly 0x0800. Two surfaces, two sizes, two scales.
     *
     * VALIDATED ON FACILITY ONLY. The owner confirmed this arm visually there.
     * Eight other levels carry texgen surfaces and are smoke-tested, not
     * cartridge-fidelity certified; egypt's path is 32x32 CI8+TLUT rather than
     * facility's IA8 and remains the standing caveat. The Right/Up
     * slot-to-axis assignment is RETAINED, NOT PROVEN - slot->vector is
     * sourced, slot->axis is not, and no view-matched cartridge frame of the
     * pane exists to settle it. See docs/doc-routing.json.
     *
     * SL_TEXGEN=0 restores the old path for A/B. */
    if (on < 0) { const char *v = getenv("SL_TEXGEN");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* SL_TEXGEN_MODE selects WHERE THE NORMALISATION HAPPENS, because the column
 * lengths measured 0.3789 / 0.2816 / 0.3789 on the pane's own draw - the
 * matrix is NON-UNIFORM, so the two orders give different numbers.
 *
 *   0  NORMALIZE TRANSFORMED NORMAL, THEN DOT BASIS
 *   1  TRANSFORM AND NORMALIZE BASIS, THEN DOT SOURCE NORMAL
 *
 * Both names are purely descriptive, deliberately. An earlier draft called
 * mode 1 "F3D-faithful"; the microcode's normalisation order is precisely the
 * unresolved fact, so naming one arm after the answer would mean that if it
 * won we could not tell whether it won on measurement or on its name.
 *
 * n.(M^T d) == (n.M).d is unconditional, so the TRANSFORM is not in question;
 * only where the unit-length step falls. Neither is asserted - they are two
 * arms to be measured against the cartridge. */
static int texgen_mode(void)
{
    static int m = -1;
    if (m < 0) { const char *v = getenv("SL_TEXGEN_MODE");
                 m = v ? atoi(v) : 0; }
    return m;
}

/* ============ THE ONE TEXTURE-COORDINATE GENERATOR ======================
 *
 * ONE FORMULA, TWO CALLERS, AND THAT IS THE POINT. This body used to live
 * inline in the G_TEXTURE_GEN arm of the vertex-load command and nowhere
 * else. It has a second caller now - override models, which need coordinates
 * manufactured per frame for exactly the same reason the RSP manufactures
 * them - and the second caller shares this function rather than carrying a
 * copy.
 *
 * WHY SHARING IS NOT A TIDINESS PREFERENCE HERE. A second, independently
 * derived implementation would be free to disagree with this one on the two
 * things nobody has settled: the V sense, and which reflectance slot feeds S
 * versus which feeds T. slot -> VECTOR is sourced (lookatref.c writes
 * l[0].dir = Right, l[1].dir = Up); slot -> AXIS is not sourced anywhere -
 * not the notes corpus, not gbi.h, not the SGI sources in this tree - which
 * is exactly why SL_TEXGEN_SWAP exists as an arm rather than a fix. A fork
 * would silently pick an answer to an open question and then the two logo
 * paths could differ without either being wrong on its own terms. Sharing
 * makes them wrong TOGETHER or right together, which is the only property
 * worth having while the question is open.
 *
 * WHAT IS SHARED, precisely: the transform (upper 3x3 of the modelview, the
 * n.(M^T d) == (M n).d identity), the normalisation-placement arms
 * (SL_TEXGEN_MODE), the slot->axis arm (SL_TEXGEN_SWAP), the resident basis
 * including its seeded identity (SL_TEXGEN_DEFBASIS), the clamp, and the
 * PRE-SCALE DOMAIN. Callers differ only in what they do with the result: the
 * display-list path applies the gSPTexture scale and the tile dimensions at
 * emit, the override path divides by the domain because a full sweep is meant
 * to cross its whole map.
 *
 * SL_TEXGEN_DOMAIN is the 0..32768 the B-051 note derives by worked example:
 * the facility pane declares scale 3456 over a 54x54 map and
 *     32768 * 3456 / 65536 / 32 = 54.0
 * exactly, so a full -1..+1 sweep lands exactly on the image. It is named
 * here rather than repeated as a literal so the two callers cannot drift.
 *
 * Returns 1 when the TRANSFORMED normal has zero length, so the caller can
 * count that population itself - the counters stay at the call sites because
 * they mean different things there (B-090's g_texgen_zeronrm is about Rare's
 * model data; the override's is about the author's).
 *
 * pre[]  PRE-SCALE coordinate pair, 0..SL_TEXGEN_DOMAIN.       required
 * d[]    the clamped dot products, -1..+1, for diagnostics.    may be NULL
 * e[]    the transformed normal.                               may be NULL
 * u[]    the transformed normal, unit length (zero if it had   may be NULL
 *        no length to begin with).
 */
#define SL_TEXGEN_DOMAIN 32768.0f

static int texgen_generate(const float n[3], const float *m,
                           float pre[2], float d[2], float e_out[3],
                           float u_out[3])
{
    float e[3], len, inv, ds = 0.0f, dt = 0.0f;
    int j, zero;
    /* SL_TEXGEN_SWAP. sA feeds S, sB feeds T. */
    const int sA = texgen_swap() ? 1 : 0;
    const int sB = 1 - sA;

    for (j = 0; j < 3; j++)
        e[j] = n[0] * m[j] + n[1] * m[4 + j] + n[2] * m[8 + j];
    len = sqrtf(e[0]*e[0] + e[1]*e[1] + e[2]*e[2]);
    zero = (len <= 1e-6f);
    inv = zero ? 0.0f : 1.0f / len;

    if (texgen_mode() == 1) {
        /* MODE 1: transform and normalise the BASIS, then dot the source
         * normal. */
        float nl = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        int w2;
        if (nl < 1e-6f) nl = 1.0f;
        for (w2 = 0; w2 < 2; w2++) {
            float dv[3], dl, acc = 0.0f;
            const int sl = (w2 == 0) ? sA : sB;
            for (j = 0; j < 3; j++)
                dv[j] = m[j*4+0] * ((float) g_lookat[sl][0] / 127.0f)
                      + m[j*4+1] * ((float) g_lookat[sl][1] / 127.0f)
                      + m[j*4+2] * ((float) g_lookat[sl][2] / 127.0f);
            dl = sqrtf(dv[0]*dv[0] + dv[1]*dv[1] + dv[2]*dv[2]);
            if (dl < 1e-6f) dl = 1.0f;
            for (j = 0; j < 3; j++)
                acc += (n[j] / nl) * (dv[j] / dl);
            if (w2 == 0) ds = acc; else dt = acc;
        }
    } else
    for (j = 0; j < 3; j++) {
        ds += (e[j] * inv) * ((float) g_lookat[sA][j] / 127.0f);
        dt += (e[j] * inv) * ((float) g_lookat[sB][j] / 127.0f);
    }

    if (ds < -1.0f) ds = -1.0f;
    if (ds >  1.0f) ds =  1.0f;
    if (dt < -1.0f) dt = -1.0f;
    if (dt >  1.0f) dt =  1.0f;

    pre[0] = (ds + 1.0f) * 0.5f * SL_TEXGEN_DOMAIN;
    pre[1] = (dt + 1.0f) * 0.5f * SL_TEXGEN_DOMAIN;

    if (d != NULL) { d[0] = ds; d[1] = dt; }
    if (e_out != NULL) { e_out[0] = e[0]; e_out[1] = e[1]; e_out[2] = e[2]; }
    if (u_out != NULL) { u_out[0] = e[0] * inv; u_out[1] = e[1] * inv;
                         u_out[2] = e[2] * inv; }
    return zero;
}

/* SL_TEXGEN_LOD=<n>: bind tile (rendertile + n) for G_TEXTURE_GEN draws.
 *
 * DIAGNOSTIC ONLY - THIS IS NOT A CANDIDATE FIX AND MUST NOT BE LANDED.
 *
 * The pane declares BB level=2 (three tiles: 54x54 tmem 0, 27x27 tmem 378,
 * 14x14 tmem 486) and its combiner's cycle-0 RGB is
 * (TEXEL1 - TEXEL0) * LOD_FRACTION + TEXEL0 - the mip lerp. The RDP picks the
 * pair of adjacent levels and the fraction between them PER PIXEL. Statically
 * binding one level answers only "does the missing mip pipeline move the
 * result", never "which level is correct" - there is no single correct level.
 * A real fix is the LOD pipeline, not a constant. */
/* SL_TEXGEN_NEUTRAL=1: for G_LIGHTING|G_TEXTURE_GEN draws ONLY, feed white
 * instead of the cn[] bytes as vertex RGB.
 *
 * ISOLATION ARM. DEFAULT OFF. NOT A FIX AND MUST NOT BE SHIPPED.
 *
 * Stage 2 established those bytes are NORMALS, yet this renderer still hands
 * them to glColor, so the environment texture is multiplied by (71,64,83),
 * (185,64,83), (185,192,83), (71,192,83) as if they were a colour. That is
 * certainly not the RSP's lighting result. White is not the right answer
 * either - the right answer needs the actual lighting inputs, G_MV_L0/L1
 * ambient and directional terms against the transformed normal. This arm
 * exists solely to QUANTIFY how much of the residual belongs to the missing
 * lighting semantics, by removing the wrong term without inventing a right
 * one. */
/* SL_TEXGEN_MARK=1: paint G_TEXTURE_GEN draws magenta and drop their texture,
 * so the surface can be LOCATED on screen before a crop is chosen. Default
 * OFF, diagnostic only. SL_GLASS_MARK cannot serve here: it keys on
 * g_cc_a_prim1, the tinted-glass combiner with the PRIM alpha addend, and
 * bunker2's texgen surface uses fc269a04 1f10ffff - no addend - so it would
 * mark nothing and the null would look like "no surface here". */
static int texgen_mark(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEXGEN_MARK");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}

static int texgen_neutral(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEXGEN_NEUTRAL");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}

static int texgen_lod(void)
{
    static int l = -1;
    if (l < 0) { const char *v = getenv("SL_TEXGEN_LOD");
                 l = v ? atoi(v) : 0; }
    return l;
}

/* SL_VTX_NRM=1, default OFF: report the loads that carried normals, once per
 * distinct slot content, so the two known-positive surfaces - the facility
 * pane and the boot logos - can be checked against numbers rather than
 * against an argument. Ordinary room geometry does not set G_LIGHTING and
 * must therefore report NOTHING here; a run that reports rooms is a broken
 * probe, which is the failure mode this print exists to expose. */
static unsigned g_vnrm_reported;

/* SL_VTX_NRM_FIRST/LAST bound the report by sl_record_index(), the unit the
 * shot windows and the run marks are already in. Without it a 9000-frame
 * replay spends the whole budget on whatever lit geometry happens to load
 * first, which is never the surface under investigation - the same fault
 * SL_TEX_DUMP_SRC exists to fix. */
static int vtx_nrm_window(void)
{
    static int checked;
    static unsigned long lo, hi;
    extern unsigned sl_record_index(void);
    unsigned long r;
    if (!checked) {
        const char *a = getenv("SL_VTX_NRM_FIRST");
        const char *b = getenv("SL_VTX_NRM_LAST");
        lo = a ? strtoul(a, NULL, 10) : 0ul;
        hi = b ? strtoul(b, NULL, 10) : 0xfffffffful;
        checked = 1;
    }
    r = (unsigned long) sl_record_index();
    return r >= lo && r <= hi;
}

static void vtx_nrm_note(unsigned k)
{
    if (getenv("SL_VTX_NRM") == NULL || g_vnrm_reported >= 64u) return;
    if (!vtx_nrm_window()) return;
    g_vnrm_reported++;
    fprintf(stderr, "sl_vnrm: slot=%u  raw=%3u,%3u,%3u,%3u"
                    "  signed normal=(%+4d,%+4d,%+4d)  s=%d t=%d"
                    "  geom=%08x%s%s\n",
            k, g_vbuf[k].r, g_vbuf[k].g, g_vbuf[k].b, g_vbuf[k].a,
            g_vnrm[k][0], g_vnrm[k][1], g_vnrm[k][2],
            (int) g_vbuf[k].s, (int) g_vbuf[k].t, g_geom_mode,
            (g_geom_mode & G_GEOM_TEXTURE_GEN) ? " TEXTURE_GEN" : "",
            (g_geom_mode & G_GEOM_TEXGEN_LIN) ? " TEXTURE_GEN_LINEAR" : "");
}

/* BB gSPTexture state - ALL of it.
 *
 * B-051 STAGE 1. This used to keep `on` and `tile` and throw the rest of the
 * command away, which is how the facility tinted-glass pane lost its texture
 * coordinates: the pane's own list issues
 *
 *     BB texture on=1 level=2 tile=0 s=3456 t=3456
 *
 * and 3456 (0x0D80) is the S/T SCALE the RSP multiplies the vertex coordinate
 * by. Discarding it makes every generated or supplied coordinate wrong by a
 * factor of 65536/3456, and discarding `level` hides that the pane declares a
 * three-level mip chain.
 *
 * Field packing: ucode05.txt "BB rsp_uc05_texture" - upper word level
 * 0x00003800, tile 0x00000700, on 0x000000FF; lower word s 0xFFFF0000,
 * t 0x0000FFFF. Checked against the note's OWN worked example,
 * "BB002801 FFFFFFFF  ;on, level 5, tile 0, ignore ST": (0x2801 >> 11) & 7 = 5,
 * (0x2801 >> 8) & 7 = 0, 0x2801 & 0xff = 1. All three land, so the packing is
 * confirmed by the corpus rather than assumed from gbi.h.
 *
 * The scale is 0.16 unsigned fixed point - 0xFFFF is the note's "ignore ST",
 * i.e. unity. NOTHING READS g_tex_sscale/g_tex_tscale YET: stage 1 only
 * stops throwing them away, so this commit cannot change a pixel. */
static unsigned g_tex_on, g_tex_tile;
static unsigned g_tex_level;              /* BB level: mip levels - 1 */
static unsigned g_tex_sscale, g_tex_tscale;   /* BB s/t, 0.16 fixed */

/* B-051 STAGE 1 CONTROL. SL_BB_DBG=1, default OFF, reports every DISTINCT
 * gSPTexture state a run issues, with a count. An instrument that cannot fail
 * is worthless, so this is deliberately a CENSUS rather than a filter: the
 * facility pane's 0x0d80 scale has to appear in a list beside every ordinary
 * surface's 0xffff, which is what makes "the pane is different" a measurement
 * instead of a claim about one line. */
/* Tentative declaration; defined with the OP_MOVEMEM case further down. */
static unsigned g_mm_type[16], g_mm_cnt[16], g_mm_n;

/* THE SECOND-ORACLE SURVEY. All twenty facility panes share ONE model, so
 * they are one oracle, not twenty; and the boot logo cannot serve because its
 * basis never arrives. This census answers, per run, WHICH distinct textured
 * surfaces are drawn with G_TEXTURE_GEN set and whether the reflectance basis
 * was complete at the time - i.e. whether a genuinely independent validation
 * surface exists anywhere. Keyed on the decoded texture identity so one model
 * drawn twenty times counts once. */
/* KEYED ON SOURCE IDENTITY AS WELL AS MATERIAL. Keying on decoded texture
 * plus combiner alone would collapse two genuinely independent models that
 * happen to share both into ONE entry - which would make a null result look
 * conclusive when it was an artefact of the key. `vsrc` is the address of the
 * draw's own vertex table (recorded per slot at load, B-043's g_vsrc), so one
 * model drawn twenty times counts once and two models sharing a material
 * count twice. */
#define TG_CENSUS_N 32
static struct { unsigned long vsrc; unsigned w, h, fmt, cc0, cc1, basis, n; }
    g_tg[TG_CENSUS_N];
static unsigned g_tg_n, g_tg_over, g_tg_tris;
static unsigned g_tg_w, g_tg_h, g_tg_fmt;

static void tg_note(unsigned long vsrc)
{
    unsigned i;
    g_tg_tris++;
    for (i = 0; i < g_tg_n; i++)
        if (g_tg[i].vsrc == vsrc && g_tg[i].w == g_tg_w &&
            g_tg[i].h == g_tg_h && g_tg[i].fmt == g_tg_fmt &&
            g_tg[i].cc0 == g_cc_w0 && g_tg[i].cc1 == g_cc_w1) {
            g_tg[i].n++; g_tg[i].basis |= lookat_seen_eff(); return;
        }
    if (g_tg_n >= TG_CENSUS_N) { g_tg_over++; return; }
    g_tg[g_tg_n].vsrc = vsrc; g_tg[g_tg_n].w = g_tg_w;
    g_tg[g_tg_n].h = g_tg_h; g_tg[g_tg_n].fmt = g_tg_fmt;
    g_tg[g_tg_n].cc0 = g_cc_w0; g_tg[g_tg_n].cc1 = g_cc_w1;
    g_tg[g_tg_n].basis = lookat_seen_eff(); g_tg[g_tg_n].n = 1;
    g_tg_n++;
}

#define BB_CENSUS_N 32
static struct { unsigned on, tile, level, s, t, n; } g_bb[BB_CENSUS_N];
static unsigned g_bb_n, g_bb_over;

static void bb_note(void)
{
    unsigned i;
    for (i = 0; i < g_bb_n; i++)
        if (g_bb[i].on == g_tex_on && g_bb[i].tile == g_tex_tile &&
            g_bb[i].level == g_tex_level && g_bb[i].s == g_tex_sscale &&
            g_bb[i].t == g_tex_tscale) { g_bb[i].n++; return; }
    if (g_bb_n >= BB_CENSUS_N) { g_bb_over++; return; }
    g_bb[g_bb_n].on = g_tex_on; g_bb[g_bb_n].tile = g_tex_tile;
    g_bb[g_bb_n].level = g_tex_level; g_bb[g_bb_n].s = g_tex_sscale;
    g_bb[g_bb_n].t = g_tex_tscale; g_bb[g_bb_n].n = 1;
    g_bb_n++;
}

void sl_bb_report(void)
{
    unsigned i;
    if (getenv("SL_BB_DBG") == NULL && getenv("SL_LIGHT_DBG") == NULL) return;
    /* REFUSAL, not a zero. See the g_dl_ran comment: without a window the
     * interpreter never runs, and every count below would be a truthful-
     * looking zero produced by an instrument that was never switched on. */
    if (g_dl_ran == 0u) {
        fprintf(stderr,
            "sl_census: REFUSING TO REPORT. The display-list interpreter"
            " executed 0 frames (sl_gfx_frame_dl returned early on all %u"
            " calls - no window, so sl_gfx_active() was false). Every census"
            " below would read zero for that reason alone and MUST NOT be"
            " taken as a negative result. Re-run under a window"
            " (xvfb-run ... SL_WINDOW=1).\n", g_dl_frame);
        return;
    }
    if (getenv("SL_LIGHT_DBG") != NULL) {
        unsigned j, s;
        fprintf(stderr, "sl_light: %u distinct lighting state(s)%s over %u"
                        " rendered frames; %u light movemems, %u numlight"
                        " movewords; numlights now %u\n",
                g_lt_n, g_lt_over ? " (+overflow)" : "", g_dl_ran,
                g_light_cmds, g_numlight_cmds, g_numlights);
        for (i = 0; i < g_lt_n; i++) {
            fprintf(stderr, "sl_light:   numlights=%u x%u  ",
                    g_lt[i].numl, g_lt[i].n);
            for (s = 0; s < SL_MAX_LIGHTS; s++) {
                if (!(g_lt[i].seen & (1u << s))) continue;
                fprintf(stderr, "L%u(0x%02x){col=%3u,%3u,%3u",
                        s, 0x86u + s * 2u, g_lt[i].l[s].col[0],
                        g_lt[i].l[s].col[1], g_lt[i].l[s].col[2]);
                if (s == g_lt[i].numl) fprintf(stderr, " AMBIENT}");
                else {
                    fprintf(stderr, " dir=%+4d,%+4d,%+4d",
                            g_lt[i].l[s].dir[0], g_lt[i].l[s].dir[1],
                            g_lt[i].l[s].dir[2]);
                    /* colc is documented as "a copy of" col. If it ever is
                     * not, the equation is reading the wrong field and this
                     * says so rather than leaving it to be assumed. */
                    for (j = 0; j < 3; j++)
                        if (g_lt[i].l[s].colc[j] != g_lt[i].l[s].col[j])
                            break;
                    fprintf(stderr, "%s}", (j < 3) ? " COLC!=COL" : "");
                }
                fprintf(stderr, "  ");
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "sl_light: known-positive - bg.c:292 GlobalLight is"
                        " ambient 150,150,150 / diffuse 255,255,255 /"
                        " dir +77,+77,+46, and bgLevelRender (bg.c:1392)"
                        " issues it for the WORLD. A run that draws the"
                        " facility and does NOT list that state is reading"
                        " the wrong bytes.\n");
        fprintf(stderr, "sl_light: lit vertex loads=%u\n", g_vtx_lit_loads);
    }
    if (getenv("SL_BB_DBG") == NULL) return;
    fprintf(stderr, "sl_tg: %u distinct G_TEXTURE_GEN surface(s)%s,"
                    " %u textured tris total\n",
            g_tg_n, g_tg_over ? " (+overflow)" : "", g_tg_tris);
    for (i = 0; i < g_tg_n; i++)
        fprintf(stderr, "sl_tg:   vtx=0x%08lx %ux%u slfmt=%u cc=%08x %08x"
                        "  x%u  basis=%s\n",
                g_tg[i].vsrc, g_tg[i].w, g_tg[i].h, g_tg[i].fmt,
                g_tg[i].cc0, g_tg[i].cc1, g_tg[i].n,
                g_tg[i].basis == 3u ? "COMPLETE"
                    : (g_tg[i].basis ? "PARTIAL" : "ABSENT"));
    fprintf(stderr, "sl_mm: movemem types this run (exact):");
    for (i = 0; i < g_mm_n; i++) {
        const char *nm = "";
        switch (g_mm_type[i]) {
        case 0x80: nm = "=G_MV_VIEWPORT"; break;
        case 0x82: nm = "=G_MV_LOOKATY";  break;
        case 0x84: nm = "=G_MV_LOOKATX";  break;
        case 0x86: nm = "=G_MV_L0";       break;
        case 0x88: nm = "=G_MV_L1";       break;
        case 0x8a: nm = "=G_MV_L2";       break;
        default: break;
        }
        fprintf(stderr, "  0x%02x%s x%u", g_mm_type[i], nm, g_mm_cnt[i]);
    }
    fprintf(stderr, "   (names from gbi.h:1265-1275)\n");
    fprintf(stderr, "sl_bb: %u distinct gSPTexture state(s)%s"
                    "   (0xffff scale is the notes' \"ignore ST\" = unity)\n",
            g_bb_n, g_bb_over ? " (+overflow)" : "");
    for (i = 0; i < g_bb_n; i++)
        fprintf(stderr, "sl_bb:   on=%u tile=%u level=%u"
                        "  s=0x%04x (%u)  t=0x%04x (%u)  x%u\n",
                g_bb[i].on, g_bb[i].tile, g_bb[i].level,
                g_bb[i].s, g_bb[i].s, g_bb[i].t, g_bb[i].t, g_bb[i].n);
}

/* Decode scratch and the cache. */
static unsigned char g_texbuf[TEX_MAX_DIM * TEX_MAX_DIM * 4];

struct sl_texent {
    const void *src;
    const void *pal;
    unsigned    key;                 /* content hash of the first bytes */
    unsigned    flags;               /* SL_TEX_FLAG_* the decode used */
    unsigned    fmt, w, h;
    GLuint      gl;
    /* B-099. The wrap modes this entry was UPLOADED with. glTexParameteri is
     * a property of the texture object, so a source that resolves to two
     * different modes would silently keep the first - the pre-existing
     * comment at the glTexParameteri calls asserts no GE tile does that, and
     * making the mode depend on the mask as well as the clamp bit is exactly
     * the change that could falsify it. Recorded so a mismatch is COUNTED
     * rather than assumed away. */
    GLint       wrap_s, wrap_t;
    unsigned char has_enh;           /* B-116: uploaded enhanced; part of the
                                      * cache key so an enhance/plain toggle or
                                      * a 2D reuse of a world source never
                                      * serves the wrong image */
    unsigned char has_mips;          /* B-119: uploaded with a mip pyramid for
                                      * the detail-blend far image; keyed so a
                                      * non-blend reuse never inherits it */
    /* #47. Which artwork this entry UPLOADED: 0 = the decode (ORIGINAL, or a
     * set that has no file for the id), else the provider generation the
     * replacement was taken under. Part of the cache key: a provider switch
     * bumps the generation, so an entry holding COMMUNITY HD's pixels can
     * never answer an XBLA or ORIGINAL resolve, and an entry holding the
     * decode keeps answering for an id no set replaces. */
    unsigned    rep_gen;
    unsigned    rep_id, rep_w, rep_h;/* the replacement's id and physical size */
    /* #47 PART A. The N64 texture number of the image this entry holds,
     * PLUS ONE (0 = none: an unregistered source - a mip level, a game-built
     * image, a font glyph). Read from the provider's own side table at
     * upload, so the per-id coverage census below can name what a draw
     * samples without a second identity mechanism. */
    unsigned    cov_id;
    unsigned    checksum;            /* of the decoded RGBA, for reporting */
    /* B-051. The DECODED alpha channel, summarised at decode time because the
     * RGBA buffer is not retained past the upload. A texel alpha of zero
     * discards the fragment under GL_MODULATE no matter what the combiner
     * computed, so this is the one texture property that can make a correctly
     * positioned, correctly coloured, correctly depth-tested quad invisible. */
    unsigned    amin, amax, azero, atot;
    unsigned    asum;
    /* B-051 WORK ITEM 3. The decoded RGB, for the same reason: the pane's
     * final colour is texel * shade, and "is black correct" is unanswerable
     * without knowing what the texel carries. */
    unsigned    rmax, gmax, bmax, rmean, gmean, bmean;
};
static struct sl_texent g_texcache[TEX_CACHE_N];
static unsigned g_texcache_n;

/* Telemetry. */
static unsigned g_tex_decoded, g_tex_hits, g_tex_uploads, g_tex_fail;
static unsigned g_tex_enhanced;    /* B-116: distinct world textures enhanced */
static unsigned g_tex_replaced;    /* #47: uploads that took a provider image */
static unsigned g_tex_hilited;     /* #47 PART A: uploads painted flat */
static unsigned g_tex_wrap_alias;      /* B-099: one source, two wrap modes */
static unsigned g_tex_palwrap_imgs, g_tex_palwrap_texels;  /* B-141, cumulative */

/* B-141 kill switch. SL_PAL_WRAP=0 restores the strict decode, under which a
 * CI index past the loaded palette rejects the whole texture (reject[decode])
 * and the draw goes untextured - the Depot yard's white floor. Default on. */
static int pal_wrap_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_PAL_WRAP");
                  on = (v == NULL || *v != '0'); }
    return on;
}
static unsigned g_tex_binds, g_tex_reject[8];
static float    g_tex_entropy_min = 9.0f, g_tex_entropy_max = -1.0f;
static unsigned g_tex_entropy_n;
static double   g_tex_entropy_sum;
static unsigned g_st_max_abs, g_st_tex_w, g_st_tex_h;
/* B-107. The bound render tile's F2 origin, in TEXELS (uls/4, ult/4), and
 * the same for the TEXEL1 tile. Subtracted from the per-vertex s/t exactly
 * where the RDP subtracts it - after the tile shift, before the divide by
 * the image extent - so an animated settilesize origin scrolls the sampling
 * window. Zero for every tile whose origin is zero, which is every load the
 * standard texWriteLoadToTmemAddr path emits, so nothing else moves. */
static float g_st_uls, g_st_ult;
/* B-117. The DIVISOR the render tile's shift applies to the incoming
 * coordinate: s' = s / g_st_sdiv. ucode05_old.txt "F5 rdp_settile": shift
 * 1..10 shifts right (divide by 2^n); 11..15 shifts LEFT by (16-n)
 * (multiply, i.e. divide by 1/2^(16-n)); 0 is no shift. */
static float g_st_sdiv = 1.0f, g_st_tdiv = 1.0f;
static float shift_scale(unsigned n)
{
    if (n == 0u) return 1.0f;
    if (n <= 10u) return (float) (1u << n);
    return 1.0f / (float) (1u << (16u - n));
}
static int tile_shift_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TILE_SHIFT");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}
static float g_tex1_uls, g_tex1_ult;
/* THE ONE RECORD of unit 0's texture-environment CONSTANT (the register
 * glTexEnvfv(GL_TEXTURE_ENV_COLOR) writes; every write in this file runs
 * with unit 0 active - tree-wide search 2026-09-09, all four writers are in
 * this file and every glActiveTexture(GL_TEXTURE1) block restores unit 0
 * before returning).
 *
 * Four families share that register: mode 8's PRIM_LOD_FRAC (the water),
 * mode 7's sky base colour, mode 4's alpha-lerp constant and mode 6's blend
 * factor. Each used to keep a private "value I last wrote" cache, and each
 * cache was blind to the other three writers, so a push could be skipped
 * while the REAL register held another family's value. Measured on Dam,
 * owner run 20260909-231311-lvl33 mark-001: the sky's K push was skipped
 * because its cache still said 10,30,60 while the register held the water's
 * animated PRIM_LOD_FRAC grey - the whole sky rendered grey, and the grey
 * tracked the water's fraction frame by frame (39 at frame 923, 47 at 925).
 *
 * One mirror of the actual register, one skip test, every writer through
 * here. Starts impossible so the first write always lands. */
static float g_envcol[4] = {-1.0f, -1.0f, -1.0f, -1.0f};

static void envcol_set(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    GLfloat col[4];
    if (g_envcol[0] == r && g_envcol[1] == g && g_envcol[2] == b
        && g_envcol[3] == a)
        return;
    col[0] = r; col[1] = g; col[2] = b; col[3] = a;
    batch_end();
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, col);
    g_envcol[0] = r; g_envcol[1] = g; g_envcol[2] = b; g_envcol[3] = a;
}
/* B-051: the alpha summary of the image decoded most recently, copied onto
 * its cache entry below so a draw can report the texture it is sampling. */
static unsigned g_dec_amin, g_dec_amax, g_dec_azero, g_dec_atot, g_dec_asum;
static unsigned g_dec_rmax, g_dec_gmax, g_dec_bmax;
static unsigned g_dec_rmean, g_dec_gmean, g_dec_bmean;
/* B-051. WHICH cache entry the last texture resolution used.
 *
 * Searching the cache by GL NAME is not sound: TEX_CACHE_N is 256 and one
 * facility replay decodes 2789 images, so the ring wraps and several entries
 * share a name. A by-name lookup returns whichever matching entry comes
 * first, which is usually a stale one describing a different image - and it
 * reports a plausible-looking format and alpha histogram for it. Record the
 * slot at the point of resolution instead, where it is exact. */
static int g_tex_slot = -1;
/* Are the S/T values consistent with 1 texel == 32 units? Count how many land
 * inside a plausible tiling range once divided by 32*width. */
static unsigned g_st_in, g_st_out;
/* Triangles that actually got a texture, and how many distinct ones. */
static unsigned g_tri_tex, g_tri_plain, g_tex_on_tris, g_tex_names[64], g_tex_names_n;
static unsigned g_tex_nosrc;

/* (fmt, siz) -> the decoder's own format code. GoldenEye's TEXFORMAT_* codes
 * and the GBI's fmt/siz pair are different alphabets; sl_gfx_tex.h documents
 * the first, ucode05.txt's FD table the second. This is the join. RGB24/RGB15
 * (GE codes 2 and 3) are stored at the wider depth and decode identically to
 * RGBA32/RGBA16, so the GBI pair cannot distinguish them and does not need
 * to. Returns -1 when the pair has no decoder (YUV). */
/* B-130. A 16-bit RGBA tile fetched while the texture LUT is ENABLED. The
 * RDP's texture unit does not consult the tile format to decide whether to
 * palettise; the LUT bit in othermode_h does, and for a 16-bit fetch the
 * index is the texel's HIGH byte. GoldenEye reaches exactly this on the
 * sky.c sea: texSelect (othermodemicrocode.c:378-499) loads IMAGE_WATER_BLUE
 * as the CI8 mip chain the texpool holds and issues gDPSetTextureLUT
 * (G_TT_RGBA16); unk_092E50.c:381-384 then re-declares tiles 0 and 1 as
 * RGBA/16b line 4 and never touches the LUT. Measured on Frigate, 2026-09-14
 * (SL_TEX_TRACE: FD fmt=2 siz=2, F0 loadtlut n=24, F5 tiles 0-5 fmt=2 siz=1,
 * then F5 tile 0/1 fmt=0 siz=2 line=4): this join returned RGBA16 and the
 * water decoded to green noise with channel maxima R<=16 G<=231 B<=90 -
 * precisely 24-entry index pairs read as 5551 - while the cartridge
 * (romintro.py, stage 26, ticks 1020-1600) draws deep blue water. The notes
 * give the LUT field (ucode05_old.txt:364 "2 texture LUT") and nothing about
 * the fetch; docs/doc-routing.json not_covered records it. The rule is
 * deliberately the ONE measured pair - RGBA/16b under a LUT - and nothing
 * else changes. SL_TLUT16=0 restores the literal 5551 read; the sl_tex
 * census line counts the resolutions that took this arm (tlut16=). */
static unsigned g_tex_tlut16;
static int tlut16_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TLUT16");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

static int gbi_to_sl_fmt(unsigned fmt, unsigned siz, unsigned tlut)
{
    switch (fmt) {
    case GBI_FMT_RGBA:
        if (siz == 3) return SL_TEX_RGBA32;
        if (siz == 2) {
            if ((tlut == 2u || tlut == 3u) && tlut16_on()) {
                g_tex_tlut16++;
                return (tlut == 3u) ? SL_TEX_CI16_IA16 : SL_TEX_CI16_RGBA16;
            }
            return SL_TEX_RGBA16;
        }
        /* B-095. RGBA/8b is not a format the SDK can express - gbi.h pairs
         * G_IM_FMT_RGBA with 16b and 32b only - but the RDP still FETCHES a
         * texel for it, and GoldenEye reaches it on a path that matters.
         *
         * MEASURED, one windowed run to the gun-barrel walk (gunbarrelTimer
         * advancing, tconfig=2013181C, the shadow image doshadow names):
         * texSelect (othermodemicrocode.c:378) looks the image up with
         * texFindInPool(aa[-4], NULL), and NULL means the GLOBAL texpool at
         * image.c:18. Bond's model textures do not live there - title.c:453
         * declares `struct texpool texturePool` on the STACK and
         * load_object_fill_header loads the tuxedo into it - so the lookup
         * returns NULL (measured: tex=00000000, against a global pool that is
         * populated and walkable, texturenums 2186..2176) and both branches
         * fall back to the image DECLARATION's own format/depth bytes.
         * Measured there: fmt=0 depth=1, i.e. RGBA/8b, on an image whose real
         * content is CI8. The declaration's fallback bytes are simply not the
         * truth about the image.
         *
         * That is Rare's behaviour, not a port defect. The authority
         * disassembly of this very function - goldeneye_docs
         * "images text and font/7F076D68 - generate DL for ImgDecl.txt",
         * //7F076E60 `JAL 7F0CBB0C / OR A1,R0,R0 ;A1=0: NULL (table at
         * 8008C720)` and //7F07744C `LBU S1,0007 (S7) ;S1=img.decl.+7:
         * format` - shows the cartridge passing the same NULL pool and taking
         * the same fallback. The pool choice sits in code with no __sgi
         * variant, so the cartridge reaches the RDP with RGBA/8b too. Rule 5:
         * reproduce it, do not correct it.
         *
         * Returning -1 here did not reproduce it. It made tile_texture answer
         * TR_FMT, which leaves texturing DISABLED, and a disabled unit reads
         * as white - so the quad took the combiner's non-texel term at vertex
         * alpha 0x50 and LIGHTENED the floor it was supposed to darken.
         *
         * I8 is the decode: the byte replicated across R,G,B AND alpha. Two
         * measurements support it and one control rejects the alternative.
         *   - The cartridge frame (parallel_n64, gunbarrelTimer=90) puts the
         *     floor beneath the shadow at 203..215 where the same rows just
         *     outside it read 220..226 - a dip of 10..19 on a background whose
         *     own vertical gradient is +2/row and was subtracted off.
         *   - The texels (dumped from the texpool, 32x32, a radial coverage
         *     blob: 0 at the rim, 189 at the centre, 177 distinct values) put
         *     an I8 draw at 205..213 over that same 220 background, a dip of
         *     7..15. Same sign, same order.
         *   - IA8 lands in the same range arithmetically but takes alpha from
         *     the LOW nibble of a smooth ramp, which cycles 0..15 every few
         *     texels; it would render this blob as speckle. The cartridge's is
         *     smooth. I8 is the one consistent with both numbers and shape.
         *
         * Deliberately 8b only. RGBA/4b also has no SDK meaning and is NOT
         * claimed here - nothing in this tree was measured reaching it, so it
         * keeps returning -1 rather than acquiring a guess.
         *
         * The notes were consulted first and are silent, as an earlier round
         * already recorded: docs/doc-routing.json not_covered carries "RDP
         * behaviour for an illegal format/size pair, specifically RGBA at
         * 8bpp", which names this exact image (07BC) and says settling it
         * "needs hardware or a reference emulator, not the notes". That is
         * what the parallel_n64 comparison above is. The entry is updated with
         * the answer rather than re-searched. (SETTILE.HTM, which ucode05.txt's
         * F5 entry defers to for the tile format fields, is not present in the
         * corpus - `find . -iname "settile*"` returns nothing.) */
        if (siz == 1) return SL_TEX_I8;
        return -1;
    case GBI_FMT_IA:
        if (siz == 2) return SL_TEX_IA16;
        if (siz == 1) return SL_TEX_IA8;
        if (siz == 0) return SL_TEX_IA4;
        return -1;
    case GBI_FMT_I:
        if (siz == 1) return SL_TEX_I8;
        if (siz == 0) return SL_TEX_I4;
        return -1;
    case GBI_FMT_CI:
        /* gbi.h G_TT_RGBA16 = 2, G_TT_IA16 = 3 (both << G_MDSFT_TEXTLUT).
         * With no LUT declared, assume the 5551 palette: it is what
         * texSetLutMode selects for every GE CI texture. */
        if (siz == 1) return (tlut == 3) ? SL_TEX_CI8_IA16 : SL_TEX_CI8_RGBA16;
        if (siz == 0) return (tlut == 3) ? SL_TEX_CI4_IA16 : SL_TEX_CI4_RGBA16;
        return -1;
    default:
        return -1;
    }
}

/* FNV-1a over the leading bytes. Guards the cache against a texpool slot
 * being reused for a different image at the same address across a level
 * load - address alone is not a safe key when the allocator recycles. */
static unsigned tex_content_key(const unsigned char *p, unsigned n)
{
    unsigned h = 2166136261u, i;
    if (n > 256) n = 256;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* Shannon entropy in bits/byte over the leading bytes: the measurement that
 * settles "compressed or inflated?" without trusting a reading of image.c. */
static float tex_entropy(const unsigned char *p, unsigned n)
{
    unsigned counts[256], i;
    float h = 0.0f;
    if (n > 4096) n = 4096;
    if (n == 0) return 0.0f;
    for (i = 0; i < 256; i++) counts[i] = 0;
    for (i = 0; i < n; i++) counts[p[i]]++;
    for (i = 0; i < 256; i++) {
        float pr;
        if (!counts[i]) continue;
        pr = (float) counts[i] / (float) n;
        /* log2(pr) via the identity log2(x) = log(x)/log(2), done with a
         * small series-free helper: use the frexp-style exponent plus a
         * short polynomial. Simpler here: accumulate -p*log2(p) using
         * repeated halving, which is exact enough for a diagnostic. */
        {
            float x = pr, l = 0.0f;
            while (x < 0.5f) { x *= 2.0f; l -= 1.0f; }
            /* x in [0.5,1): log2(x) ~ (x-1)*(1.4427 - 0.72*(x-1)) */
            l += (x - 1.0f) * (1.4427f - 0.72f * (x - 1.0f));
            h -= pr * l;
        }
    }
    return h;
}

/* GL_CLAMP_TO_EDGE is GL 1.2; fall back to GL_CLAMP where the header is
 * older. Mirroring needs GL_MIRRORED_REPEAT (1.4) and is not attempted -
 * GL_REPEAT is the closer of the two available approximations.
 *
 * B-078: THIS FALLBACK IS NOT EQUIVALENT AND MUST NOT BE REACHED SILENTLY.
 * GL_CLAMP blends the texture BORDER into the filter at the image edge, which
 * under GL_LINEAR draws a dark half-texel fringe around every clamped image.
 * On Windows the header really does lack the 1.2 enum, so this fired and put
 * that fringe on every clamped tile in the game; the _WIN32 block at the top
 * of this file now supplies the real value, which is why this is inert there.
 * Anywhere it is still reachable it is a KNOWN visual defect, not a neutral
 * degradation. */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE GL_CLAMP
#endif

/* ---- B-099: A TILE WITH NO MASK DOES NOT WRAP --------------------------
 *
 * The RDP's S/T address unit takes the wrap exponent from the F5 MASK field,
 * not from the clamp bit alone: with mask == 0 there is no bit position to
 * wrap at, so the coordinate is held inside the tile and the axis behaves as
 * CLAMPED whatever the clamp/mirror bits say. Sightline read only the clamp
 * bit and installed GL_REPEAT for every unmasked WRAP axis.
 *
 * MEASURED, MENU_DISPLAY_CAST, one binary, seed the only variable. The cast
 * credits draw one texrect per glyph and the affected cards are exactly the
 * ones whose letters have edge ink:
 *
 *   card 1 "Starring / 007 / James Bond"  - a vertical bar left of J and of
 *          B, and a horizontal rule under the 7
 *   card 4 "Also Featuring / Janus Operative / Xenia Onatopp" - the same bar
 *          left of J, A and O
 *   card 2 "Starring / Natalya Simonova"  - CLEAN, same font, same call
 *
 * textrelated.c:243 loads each glyph with gDPLoadTextureBlock(..., G_TX_WRAP,
 * G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, ...), and G_TX_WRAP and G_TX_NOMASK
 * are both 0 (gbi.h:391,394) - so the render tile's whole F5 lower word is
 * zero. That is the cartridge's own encoding, not the decomp's: the ROM's
 * font microcode generator writes the SAME word from register zero ("images
 * text and font/Fonts/7F0AD0F8 - font microcode generator.txt", the [ASCII]
 * settile at 7F0AD378: `SW R0,0004 (V1)  ;settile.l: tile=0`), over the tile
 * whose settilesize the same routine gives as (width rounded up to 8) - 1 by
 * (height - 1). Field positions from ucode05_old.txt's "F5 rdp_settile" lower
 * word: clamp s 0x00000200, mask s 0x000000F0, clamp t 0x00080000, mask t
 * 0x0003C000 - all zero here.
 *
 * Why the artefact is per-GLYPH rather than per-card. The glyph tile is
 * padded to a multiple of 8 texels wide, so a letter narrower than its
 * padding has blank columns on the right and wrapping brings in nothing. A
 * letter whose width is already a multiple of 8 has its own last column
 * there, and GL_REPEAT under GL_LINEAR magnification folds it onto the left
 * edge - the vertical bar. The T axis is never padded, so row 0 lands under
 * the last row on every glyph whose top row carries ink: the 7's crossbar,
 * drawn again as a rule beneath it.
 *
 * The notes are silent on the mask-zero rule itself. Tree-wide over the whole
 * corpus: `grep -rn -il mask notes/` names 71 files, and a content grep for
 * `mask.{0,40}(clamp|wrap)` and `clamp.{0,40}mask` returns ONLY ucode05_old's
 * F5 field table and the two DL generators that fill those fields - nothing
 * that states what the address unit DOES with them. A not_covered entry
 * records the gap; the rule is taken from the header's field split plus the
 * measurement above. Same gap B-088 hit from the other side, for SIZING.
 *
 * SL_TILE_CLAMP0=0 restores the pre-B-099 reading from the same binary. */
static int tile_clamp0_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("SL_TILE_CLAMP0");
        on = (v == NULL || (*v != '\0' && *v != '0'));
    }
    return on;
}
static unsigned g_tclamp0_s, g_tclamp0_t;   /* axes this rule clamped */
static GLint tex_wrap(unsigned cm, unsigned mask, unsigned *hit)
{
    if (cm & SL_TX_CLAMP) return GL_CLAMP_TO_EDGE;
    if (mask == 0u && tile_clamp0_on()) { (*hit)++; return GL_CLAMP_TO_EDGE; }
    return GL_REPEAT;
}

static const unsigned char *g_key_src;
static unsigned g_key_need, g_key_val;

/* GL binding actually in force, so a redundant bind costs nothing. */
static GLuint g_tex_gl_bound;
static int    g_tex_gl_on;

/* ---- SL_TEX_DUMP --------------------------------------------------------
 * Put a decoded texture and its palette on disk so both can be LOOKED at,
 * which is how B-017 was settled and how B-018's palette had to be. Off
 * unless SL_TEX_DUMP names a path prefix, so headless never reaches it, and
 * capped at a handful of images. For each one:
 *
 *   <prefix>-NN-tex.ppm   the decoded image, one pixel per texel
 *   <prefix>-NN-pal.ppm   the palette the F0 named, one 16x16 block per entry
 *   <prefix>-NN-fdbase.ppm  the palette reading from the FD BASE instead -
 *                         i.e. what this file did before the F0 offset was
 *                         honoured. Dumping the rejected hypothesis beside
 *                         the accepted one is what makes the comparison
 *                         decisive rather than merely plausible.
 */
static unsigned g_dump_n;

/* Build "<prefix>-NN-<tag>.ppm" by hand. sprintf here is NOT libc's:
 * src/sprintf.c defines one and that is the symbol that links, and it
 * produces nothing natively - the first version of this dumper wrote an empty
 * path, fopen("") returned NULL, and twelve images silently went nowhere
 * while every counter read healthy. Same trap sl_gfx_sdl.c:114 records for
 * SL_SHOT; fprintf is unaffected because the decomp does not define it. */
static void dump_path(char *out, unsigned cap, const char *pre,
                      unsigned id, const char *tag)
{
    unsigned n = 0;
    const char *s;
    for (s = pre; *s && n + 16 < cap; s++) out[n++] = *s;
    out[n++] = '-';
    /* THREE digits. Two silently capped usable output at 99 while the cap
     * itself was raised to 512: entries past that decoded and were reported in
     * the log but their files collided and were never inspectable. That hid
     * the ammo icon (entry 150) after the cap had already been raised once to
     * find it. */
    out[n++] = (char) ('0' + (id / 100u) % 10u);
    out[n++] = (char) ('0' + (id / 10u) % 10u);
    out[n++] = (char) ('0' + id % 10u);
    out[n++] = '-';
    for (s = tag; *s && n + 6 < cap; s++) out[n++] = *s;
    out[n++] = '.'; out[n++] = 'p'; out[n++] = 'p'; out[n++] = 'm';
    out[n] = '\0';
}

static void dump_ppm(const char *path, const unsigned char *rgb,
                     unsigned w, unsigned h)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    fwrite(rgb, 1, (size_t) w * h * 3u, f);
    fclose(f);
}

/* One palette -> a 16-wide grid of 16x16 swatches. `ia` selects the I8A8
 * reading over the 5551 one; both bit splits are the decoder's own. */
static void dump_palette(const char *path, const unsigned char *pal,
                         unsigned n, int ia)
{
    static unsigned char img[16 * 16 * 16 * 16 * 3];
    unsigned rows = (n + 15u) / 16u, x, y, i;
    unsigned w = 16u * 16u, h = rows * 16u;

    if (pal == NULL || n == 0u || rows > 16u) return;
    memset(img, 0, (size_t) w * h * 3u);
    for (i = 0; i < n; i++) {
        unsigned c = ((unsigned) pal[i * 2u] << 8) | pal[i * 2u + 1u];
        unsigned char r, g, b;
        if (ia) { r = g = b = (unsigned char) (c >> 8); }
        else {
            r = (unsigned char) (((c >> 11) & 0x1f) * 255u / 31u);
            g = (unsigned char) (((c >>  6) & 0x1f) * 255u / 31u);
            b = (unsigned char) (((c >>  1) & 0x1f) * 255u / 31u);
        }
        for (y = 0; y < 16u; y++)
            for (x = 0; x < 16u; x++) {
                unsigned px = (i % 16u) * 16u + x, py = (i / 16u) * 16u + y;
                unsigned char *o = &img[(py * w + px) * 3u];
                o[0] = r; o[1] = g; o[2] = b;
            }
    }
    dump_ppm(path, img, w, h);
}

static unsigned tex_roughness(const unsigned char *rgba, unsigned w, unsigned h);

/* SL_TEX_DUMP_IDS=<dir> - THE DECODE, KEYED BY THE GAME'S OWN TEXTURE NUMBER.
 *
 * The dump above is keyed by the order images happened to be decoded in, which
 * answers "what did this draw look like" and cannot answer "what is the game's
 * own artwork for id 0112". This one writes each decoded image ONCE per id,
 * in the pack format (SLTX, docs/texture-packs.md), so the ROM's artwork and a
 * pack's replacement for the same id are two files one reader opens - which is
 * what comparing a whole mapping offline needs.
 *
 * Diagnostic and off unless the variable is set: no allocation, no cost and no
 * call beyond a registry lookup when it is unset. The id comes from the
 * provider's existing side table (sl_texprov_id_of), so an image that never
 * passed through texLoad has none and is skipped rather than guessed at. */
static void tex_dump_by_id(const unsigned char *rgba, unsigned w, unsigned h,
                           const unsigned char *src_addr)
{
    static unsigned char seen[SL_TEXPROV_MAX_ID / 8u];
    static const char *dir;
    static int checked;
    unsigned char hdr[SL_TEXPROV_HEADER];
    unsigned nw = 0, nh = 0;
    char path[512];
    int id;
    FILE *f;

    if (!checked) { checked = 1; dir = getenv("SL_TEX_DUMP_IDS"); }
    if (dir == NULL || dir[0] == '\0') return;
    id = sl_texprov_id_of(src_addr, &nw, &nh);
    if (id < 0 || (unsigned) id >= SL_TEXPROV_MAX_ID) return;
    if (w == 0u || h == 0u || w > 0xffffu || h > 0xffffu) return;
    if (seen[id >> 3] & (unsigned char) (1u << (id & 7))) return;
    seen[id >> 3] |= (unsigned char) (1u << (id & 7));

    sprintf(path, "%s/%04x.sltx", dir, (unsigned) id);
    f = fopen(path, "wb");
    if (f == NULL) return;
    memset(hdr, 0, sizeof hdr);
    hdr[0] = SL_TEXPROV_MAGIC0; hdr[1] = SL_TEXPROV_MAGIC1;
    hdr[2] = SL_TEXPROV_MAGIC2; hdr[3] = SL_TEXPROV_MAGIC3;
    hdr[4] = (unsigned char) SL_TEXPROV_VERSION;
    hdr[8]  = (unsigned char) (id & 0xff);
    hdr[9]  = (unsigned char) ((id >> 8) & 0xff);
    /* n64 size = the pool's logical size; phys = what was decoded. For the
     * decode they are the same image, and a reader can check that they are. */
    hdr[12] = (unsigned char) (nw & 0xff);  hdr[13] = (unsigned char) ((nw >> 8) & 0xff);
    hdr[14] = (unsigned char) (nh & 0xff);  hdr[15] = (unsigned char) ((nh >> 8) & 0xff);
    hdr[16] = (unsigned char) (w & 0xff);   hdr[17] = (unsigned char) ((w >> 8) & 0xff);
    hdr[18] = (unsigned char) (h & 0xff);   hdr[19] = (unsigned char) ((h >> 8) & 0xff);
    {   unsigned payload = w * h * 4u;
        hdr[24] = (unsigned char) (payload & 0xff);
        hdr[25] = (unsigned char) ((payload >> 8) & 0xff);
        hdr[26] = (unsigned char) ((payload >> 16) & 0xff);
        hdr[27] = (unsigned char) ((payload >> 24) & 0xff);
    }
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(rgba, 1, (size_t) w * h * 4u, f);
    fclose(f);
}

/* SL_TEX_HILITE - WHICH PIXELS DOES THIS TEXTURE ACTUALLY PAINT.
 *
 * The provider's census counts UPLOADS, and the coverage census below counts
 * projected primitive AREA. Neither is a count of pixels on the screen: the
 * first is blind to how large a texture is drawn, the second is blind to
 * what is drawn in front of it. This is the measurement, and it needs no
 * renderer surgery at all - paint the textures in question a flat colour and
 * capture the frame; every pixel whose final colour depends on one of them
 * changes with the colour.
 *
 *   SL_TEX_HILITE=mapped    every texture the ACTIVE SET replaces
 *   SL_TEX_HILITE=<hex id>  one N64 texture number, under any set
 *   SL_TEX_HILITE_RGB=rrggbb  the flat colour (default ff00ff)
 *
 * Used in PAIRS: the same pose captured under two different colours, and the
 * mask is the pixels that differ. Differencing against an ordinary capture
 * instead would miss every pixel the hilited texture paints BLACK (a surface
 * at shade zero is black under any texel), and would falsely include nothing
 * - so the pair is the honest instrument and a single run is not.
 *
 * The decode's ALPHA is kept, so a cutout stays a cutout and the mask has the
 * shape of the artwork rather than of its bounding tile. The replacement is
 * deliberately NOT uploaded under this flag even when one exists: what is
 * being measured is where the id lands, which is a property of the draw, not
 * of the pack. Diagnostic; dark unless the variable is set. */
static int tex_hilite_id(void)
{
    static int checked, want = -2;          /* -2 off, -1 "mapped", else id */
    if (!checked) {
        const char *v = getenv("SL_TEX_HILITE");
        checked = 1;
        if (v != NULL && v[0] != '\0') {
            if (strcmp(v, "mapped") == 0) want = -1;
            else want = (int) strtoul(v, NULL, 16);
        }
    }
    return want;
}

/* Whether the per-id coverage census (below, at cover_account) wants the id
 * recorded on each cache entry. Only the variable's presence is read here -
 * the frame window is applied where the area is accumulated - because a cache
 * entry outlives the frame that created it. */
static int texcov_armed(void)
{
    static const char *p; static int checked;
    if (!checked) { checked = 1; p = getenv("SL_TEX_COVERAGE");
                    if (p != NULL && p[0] == '\0') p = NULL; }
    return p != NULL;
}

static const unsigned char *tex_hilite_rgb(void)
{
    static unsigned char rgb[3] = { 0xff, 0x00, 0xff };
    static int checked;
    if (!checked) {
        const char *v = getenv("SL_TEX_HILITE_RGB");
        checked = 1;
        if (v != NULL && strlen(v) >= 6u) {
            unsigned long c = strtoul(v, NULL, 16);
            rgb[0] = (unsigned char) ((c >> 16) & 0xff);
            rgb[1] = (unsigned char) ((c >> 8) & 0xff);
            rgb[2] = (unsigned char) (c & 0xff);
        }
    }
    return rgb;
}

static void tex_dump(const unsigned char *rgba, unsigned w, unsigned h,
                     int slfmt, const unsigned char *pal, unsigned paln,
                     const unsigned char *fdbase, const unsigned char *src_addr)
{
    static unsigned char rgb[TEX_MAX_DIM * TEX_MAX_DIM * 3];
    const char *pre = getenv("SL_TEX_DUMP");
    char path[512];
    unsigned i, id;

    /* Cap raised from 12 to 96 (2026-08-25). Twelve was too few to be useful:
     * a facility capture spent all of them on world geometry before the
     * viewmodel drew, so the arm and gun - the things actually being
     * investigated - never got dumped at all. SL_TEX_DUMP_SRC still narrows to
     * one source when that is what is wanted. */
    if (pre == NULL || g_dump_n >= 512u) return;
    /* SL_TEX_DUMP_SRC=<hex> restricts the dump to one source address, and
     * SL_TEX_DUMP_ROUGH=<0-255> to images at least that rough. A level decodes
     * ~100 images and the twelve slots go to whichever happen to come first,
     * which is never the one under investigation.
     *
     * The roughness filter is the one that works: the texpool base moves by a
     * megabyte between runs (measured - the same image reported c84f6110 on
     * one run and c83f6110 on the next), so an address copied out of one run's
     * report matches nothing in the next. Selecting on what the image LOOKS
     * like is stable across runs; selecting on where it landed is not. */
    {   const char *want = getenv("SL_TEX_DUMP_SRC");
        const char *rough = getenv("SL_TEX_DUMP_ROUGH");
        if (want != NULL) {
            unsigned long a = strtoul(want, NULL, 16);
            if (a != 0 && (unsigned long) src_addr != a) return;
        }
        if (rough != NULL &&
            tex_roughness(rgba, w, h) < (unsigned) strtoul(rough, NULL, 10))
            return;
    }
    /* SL_TEX_DUMP_DXTN restricts the dump to images loaded with a non-zero
     * dxt - the set B-020's rule reads as "plain". Without it a level's first
     * twelve textures are all dxt=0 world tiles and the ones in question
     * never get dumped. */
    if (getenv("SL_TEX_DUMP_DXTN") != NULL && g_load_dxt == 0) return;
    /* B-018 only ever needed CI images, so this used to return early for
     * everything else - which meant the one measurement that would have
     * settled B-020 in a minute could not be taken from inside the engine.
     * Every decoded texture is dumped now; the palette pair still only makes
     * sense for a CI image, so that part stays guarded. */
    id = g_dump_n++;

    for (i = 0; i < w * h; i++) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    dump_path(path, sizeof path, pre, id, "tex");
    dump_ppm(path, rgb, w, h);

    /* The decoded image answers "does this look right?"; the bytes behind it
     * answer "what IS this?", which is the question when the answer to the
     * first is no. Written raw so alternative readings - another bit depth,
     * another stride, the swizzle off - can be tried offline against the
     * exact bytes the decoder saw, rather than re-derived by argument. */
    if (getenv("SL_TEX_DUMP_RAW") != NULL) {
        /* SL_TEX_DUMP_RAW=<n> widens the window to n bytes: when every
         * reading of the declared extent is noise, the question becomes what
         * is actually AROUND the pointer, and a window that stops at the
         * declared size cannot answer it. */
        unsigned want = (unsigned) strtoul(getenv("SL_TEX_DUMP_RAW"), NULL, 10);
        unsigned need = sl_tex_data_size(slfmt, (int) w, (int) h);
        if (want > need) need = want;
        unsigned n;
        dump_path(path, sizeof path, pre, id, "raw");
        /* dump_path only ever writes .ppm; these are not pixels. */
        for (n = 0; path[n] != '\0'; n++) ;
        if (n >= 3u) { path[n-3] = 'b'; path[n-2] = 'i'; path[n-1] = 'n'; }
        if (need != 0 && src_addr != NULL &&
            mem_readable((unsigned long) src_addr, need)) {
            FILE *f = fopen(path, "wb");
            if (f != NULL) { fwrite(src_addr, 1, need, f); fclose(f); }
        }
    }

    if (sl_tex_palette_entries(slfmt) != 0) {
        dump_path(path, sizeof path, pre, id, "pal");
        dump_palette(path, pal, paln, g_tlut_mode == 3);

        dump_path(path, sizeof path, pre, id, "fdbase");
        if (fdbase && mem_readable((unsigned long) fdbase, paln * 2u))
            dump_palette(path, fdbase, paln, g_tlut_mode == 3);
    }

    fprintf(stderr, "sl_texdump: %02u src=%p fmt=%X %ux%u pal=%p n=%u "
                    "fd-base=%p off=%u dxt=%u\n",
            id, (const void *) src_addr, (unsigned) slfmt, w, h,
            (const void *) pal, paln,
            (const void *) fdbase, g_tlut_off, g_load_dxt);
}

/* B-023 diagnostic, defined with the rest of the tile-resolve machinery. */
static void noisy_note(const unsigned char *src, unsigned pal, int slfmt,
                       unsigned w, unsigned h, unsigned flags,
                       const unsigned char *rgba);

/* Look the texture up, decoding and uploading on a miss. Returns 0 when the
 * texture cannot be produced - the caller then draws untextured rather than
 * dereferencing anything unvalidated. */
/* B-108. THE WRAP PERIOD AND THE ROW STRIDE ARE DIFFERENT QUANTITIES, and a
 * tile is free to carry both: the RDP wraps a repeating axis at 2^mask
 * (ucode05_old.txt "F5 rdp_settile", mask s 0x000000F0 / mask t 0x0003C000;
 * gbi.h:3415 packs them) and then addresses TMEM at t * line + s, where
 * `line` is the row stride in 64-bit words (ucode05_old.txt's tmem entry,
 * B-077). When line-texels == 2^mask - every ordinary texture - one image
 * row per stride row falls out and the linear decode is exact. When they
 * differ, each SAMPLED row of the 2^mask-wide period is 2^mask consecutive
 * texels STARTING at its stride row, so successive rows overlap or skip:
 * that is not a special case, it is what t * line + s says. Dam's water is
 * the measured witness (src/game/unk_092E50.c): CI8, line=2 (16 texels a
 * row) under masks=maskt=5 (wrap at 32), so the RDP's visible 32x32 period
 * is a 16-byte-stride walk of a 1024-byte source, and sizing the GL repeat
 * to the 16-texel stride (B-102) doubled the water's S frequency. B-102's
 * stride stays - as the ADDRESSING rule; B-088's 2^mask stays - as the
 * PERIOD. This helper materialises exactly t*line+s for the decoder. */
/* ======================= B-116: world texture enhancement ================
 *
 * Owner ruling 2026-09-11 (Gitea #10 comment 113 accepted as correct AND
 * overruled as a stop condition): the cartridge's low-resolution world
 * textures, faithful as they are, read as harsh and blocky at the window's
 * 3x internal resolution, and Sightline intentionally adds a native
 * enhancement layer. This is NOT a fidelity fix - it is a deliberate,
 * toggleable presentation divergence.
 *
 * WHAT IT IS. A deterministic edge-aware RESAMPLE of the already-decoded RGBA
 * to a higher texel count, run once per distinct texture at upload and cached
 * in the same GL object the decode produced. No ROM-derived pixels are stored
 * anywhere - the enhanced image lives only in the process's live GL texture
 * for the session, regenerated from the user's own ROM on every launch,
 * exactly like the decoded texture it replaces. Nothing is written to disk and
 * nothing is committed.
 *
 * WHAT IT PRESERVES, each a property of a straight RGBA resample rather than a
 * hope: the S/T frequency (GL normalises coordinates to [0,1] whatever the
 * texel count, so a texture that repeated twice across a wall still repeats
 * twice - there are simply more texels per repeat); the alpha channel (a
 * fourth channel resampled identically, so palette/format-derived
 * transparency and cutouts survive); the wrap mode (the resample samples with
 * the tile's own wrap/clamp at the edges, so a repeating texture stays
 * seamless and a clamped one keeps its edge); the RDP/game state (this touches
 * only the pixels handed to glTexImage2D). It runs AFTER decode to RGBA, so CI
 * palettes are already resolved and nothing reconstructs a larger TMEM.
 *
 * WHAT IT EXCLUDES, by generic characteristic and never by level/address:
 * 2D draws (HUD, fonts, menus, crosshair - g_tex_world), the animated /
 * multi-tile water and mip-lerp family (g_cc_tex1lerp), and textures already
 * large enough that a useful integer factor would exceed the decode cap.
 *
 * ALGORITHM. Catmull-Rom bicubic. It has a wider, mildly sharpening kernel
 * than GL's built-in bilinear, so the magnified texel grid dissolves into
 * smooth gradients while edges keep some crispness - the "less blocky, still
 * detailed" the ruling asks for - and it is parameter-free, so there is no
 * per-pixel tuning to smuggle in. SL_TEX_ENHANCE=0 restores the plain decoded
 * upload from the same binary. */
static int enhance_option(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEX_ENHANCE");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* The integer factor to enhance a w x h image by: as large as keeps both axes
 * within the decode cap, preferring 4 then 3 then 2, and 1 (skip) when even
 * doubling would overflow. Only textures at or below 64 on a side are
 * enhanced - above that the cartridge already spent the texels. */
static unsigned enhance_factor(unsigned w, unsigned h)
{
    unsigned m = w > h ? w : h;
    if (m == 0u || m > 64u) return 1u;
    if (m * 4u <= TEX_MAX_DIM) return 4u;
    if (m * 3u <= TEX_MAX_DIM) return 3u;
    if (m * 2u <= TEX_MAX_DIM) return 2u;
    return 1u;
}

/* One coordinate wrapped/clamped into [0,n), matching the tile's own mode so a
 * repeating texture resamples seamlessly across its edge. clampflag != 0 is
 * GL_CLAMP-family (edge held); else the texture wraps. */
static int enh_index(int i, int n, int clampflag)
{
    if (clampflag) { if (i < 0) return 0; if (i >= n) return n - 1; return i; }
    i %= n; if (i < 0) i += n; return i;
}

static float enh_catrom(float t)
{
    float a = t < 0.0f ? -t : t;
    if (a < 1.0f) return 1.5f * a * a * a - 2.5f * a * a + 1.0f;
    if (a < 2.0f) return -0.5f * a * a * a + 2.5f * a * a - 4.0f * a + 2.0f;
    return 0.0f;
}

/* Resample src (w x h RGBA) into dst at (w*f x h*f), Catmull-Rom, per-channel,
 * with the tile's wrap/clamp at the edges. dst must hold (w*f)*(h*f)*4 bytes. */
static void enh_upscale(const unsigned char *src, unsigned w, unsigned h,
                        unsigned char *dst, unsigned f,
                        int clamp_s, int clamp_t)
{
    unsigned ow = w * f, oh = h * f, ox, oy;
    int iw = (int) w, ih = (int) h, c, m, n;
    for (oy = 0; oy < oh; oy++) {
        float sy = ((float) oy + 0.5f) / (float) f - 0.5f;
        int   ty = (int) (sy < 0.0f ? sy - 1.0f : sy);
        float fy = sy - (float) ty;
        float wy[4]; int ky[4];
        for (n = 0; n < 4; n++) {
            wy[n] = enh_catrom((float) (n - 1) - fy);
            ky[n] = enh_index(ty + n - 1, ih, clamp_t);
        }
        for (ox = 0; ox < ow; ox++) {
            float sx = ((float) ox + 0.5f) / (float) f - 0.5f;
            int   tx = (int) (sx < 0.0f ? sx - 1.0f : sx);
            float fx = sx - (float) tx;
            float wx[4]; int kx[4];
            unsigned char *o = dst + (oy * ow + ox) * 4u;
            for (m = 0; m < 4; m++) {
                wx[m] = enh_catrom((float) (m - 1) - fx);
                kx[m] = enh_index(tx + m - 1, iw, clamp_s);
            }
            for (c = 0; c < 4; c++) {
                float acc = 0.0f;
                for (n = 0; n < 4; n++) {
                    const unsigned char *row = src + (unsigned) ky[n] * w * 4u;
                    float r = 0.0f;
                    for (m = 0; m < 4; m++)
                        r += wx[m] * (float) row[(unsigned) kx[m] * 4u + (unsigned) c];
                    acc += wy[n] * r;
                }
                if (acc < 0.0f) acc = 0.0f;
                if (acc > 255.0f) acc = 255.0f;
                o[c] = (unsigned char) (acc + 0.5f);
            }
        }
    }
}

/* Set at the tile_texture call sites: 1 for the 3D world path (tex_apply),
 * 0 for the 2D texrect path. g_2d_on is not reliable here because
 * draw_texrect binds its texture BEFORE mode2d_begin runs. */
static int g_tex_world;

/* B-119. Set around the tile-1 resolve for a LOD_FRACTION detail-blend draw:
 * the far image (the treeline strip) is minified well past its base level at
 * distance, so its upload gets a box-generated GL mip pyramid and trilinear
 * minification - the per-pixel intra-chain selection the cartridge's tiles
 * 2..6 exist for. (The authored mips themselves cannot be GL levels: 64x17's
 * authored next level is 32x9 where GL requires 32x8.) */
static int g_want_lodmips;

/* Box-downsample one RGBA level into dst at half size (min 1 per axis). */
static void mip_box_halve(const unsigned char *s, unsigned w, unsigned h,
                          unsigned char *d, unsigned *ow, unsigned *oh)
{
    unsigned nw = w > 1u ? w >> 1 : 1u;
    unsigned nh = h > 1u ? h >> 1 : 1u;
    unsigned x, y, c;
    unsigned sx = w > 1u ? 2u : 1u;
    unsigned sy = h > 1u ? 2u : 1u;
    for (y = 0; y < nh; y++) {
        for (x = 0; x < nw; x++) {
            const unsigned char *p00 = s + ((y * sy) * w + (x * sx)) * 4u;
            const unsigned char *p10 = p00 + (sx > 1u ? 4u : 0u);
            const unsigned char *p01 = p00 + (sy > 1u ? w * 4u : 0u);
            const unsigned char *p11 = p01 + (sx > 1u ? 4u : 0u);
            unsigned char *o = d + (y * nw + x) * 4u;
            for (c = 0; c < 4u; c++)
                o[c] = (unsigned char)
                       (((unsigned) p00[c] + p10[c] + p01[c] + p11[c] + 2u) >> 2);
        }
    }
    *ow = nw; *oh = nh;
}

/* Tentative declaration so tex_acquire (above the combiner parser) can read
 * the animated/mip-lerp water flag; its defining declaration is further down
 * with the other g_cc_* combiner state. Same idiom the file uses for g_cc_w0. */
static int g_cc_tex1lerp;

static unsigned char g_rowbuf[TEX_MAX_DIM * TEX_MAX_DIM * 4];
static const unsigned char *tex_src_rows(const unsigned char *src, int slfmt,
                                         unsigned w, unsigned h,
                                         unsigned stridetex)
{
    unsigned rowb = sl_tex_row_stride(slfmt, (int) w);
    unsigned strb = sl_tex_row_stride(slfmt, (int) stridetex);
    unsigned i;
    if (rowb == 0u || strb == 0u ||
        (unsigned long) rowb * (unsigned long) h > sizeof g_rowbuf)
        return NULL;
    for (i = 0; i < h; i++)
        memcpy(g_rowbuf + (size_t) i * rowb, src + (size_t) i * strb, rowb);
    return g_rowbuf;
}

/* A decode rejection names its source ONCE under SL_TEX_TRACE. The census
 * line counts reject[decode] but cannot say which texture it was; Depot
 * (2026-09-15) reported up to 200 per run with fail 0 on every earlier level,
 * and only the source, format and size can turn that count into a first
 * wrong stage. Gated like every other trace: a normal run evaluates one int. */
static void tex_fail_trace(const unsigned char *src, int slfmt, unsigned w,
                           unsigned h, unsigned stridetex, unsigned texflags,
                           int rc)
{
    static const unsigned char *seen[16];
    static unsigned nseen;
    unsigned i;
    if (!textrace_on()) return;
    for (i = 0; i < nseen; i++)
        if (seen[i] == src) return;
    if (nseen < 16) seen[nseen++] = src;
    fprintf(stderr, "sl_textrace: decode REJECT src=%08lx slfmt=%d %ux%u"
                    " stride=%u flags=%08x rc=%d  bytes:",
            (unsigned long) (uintptr_t) src, slfmt, w, h, stridetex,
            texflags, rc);
    for (i = 0; i < 8u && mem_readable((unsigned long) src + i, 1u); i++)
        fprintf(stderr, " %02x", src[i]);
    fprintf(stderr, "\n");
}

static GLuint tex_acquire(const unsigned char *src, const unsigned char *pal,
                          unsigned palents, int slfmt, unsigned w, unsigned h,
                          unsigned cms, unsigned cmt, unsigned masks,
                          unsigned maskt, unsigned texflags,
                          unsigned stridetex)
{
    unsigned need, span, palneed, key, i, sum;
    unsigned char *dst = g_texbuf;
    const unsigned char *rows;
    struct sl_texent *e;
    int rc;
    /* #47. THE TEXTURE PROVIDER SEAM. Resolved below, once the source has
     * passed the same readability checks the decode needs: the provider's
     * replacement for THIS image (src is the decoded pointer texLoad
     * registered, w x h the tile's logical size), or NULL - ORIGINAL in
     * force, no file in the selected set, a mip level, a sub-window. A
     * replacement is uploaded IN PLACE of the decode at its own physical
     * size; everything else about the texture - the wrap modes, the tile
     * size the coordinates divide by, the combiner, the alpha path - is
     * decided from the same state as before, so the replacement is sampled
     * over exactly the region and repeat count the decode was. */
    const struct sl_texprov_image *rep = NULL;
    unsigned rep_gen = 0u;
    int hil = 0;                   /* #47 PART A: SL_TEX_HILITE selected this */
    /* B-116. Whether THIS draw is eligible for world-texture enhancement:
     * a 3D world draw, the feature on, not the animated/multi-tile water or
     * mip-lerp family (g_cc_tex1lerp), and small enough to gain a real integer
     * factor. Decided here so it enters the cache key - a 2D texrect reusing
     * the same source, or a run with the toggle flipped, gets its own entry
     * and never the wrong image. The factor is recomputed at upload. */
    int want_enh = g_tex_world && enhance_option() && !g_cc_tex1lerp
                   && enhance_factor(w, h) > 1u;
    int want_mips = g_want_lodmips;                    /* B-119 */
    /* B-119. Mutually exclusive by construction: a pyramid whose level 0 is
     * enhance-scaled while level 1 halves the UNSCALED base is not a GL mip
     * chain at all - the texture goes incomplete and samples as if disabled.
     * The mip request is the fidelity feature for its family; it wins. */
    if (want_mips) want_enh = 0;
    /* B-141. A CI index past the loaded palette wraps into it rather than
     * failing the whole texture (sl_gfx_tex.h SL_TEX_FLAG_PAL_WRAP). Part of
     * the cache key through texflags, so the kill switch (SL_PAL_WRAP=0, the
     * old reject) never shares an entry with the default. */
    if (pal_wrap_on()) texflags |= SL_TEX_FLAG_PAL_WRAP;

    /* B-095. THE BARREL BACKGROUND'S ROWS ARE 440 TEXELS WIDE, and this guard
     * used to be `w > TEX_MAX_DIM || h > TEX_MAX_DIM` with TEX_MAX_DIM 256.
     *
     * The comment on the constant - "GE's largest is far below this" - is true
     * of every texture that arrives through the image bank, and false of the
     * one the game BUILDS: titleRenderFolderMenuBackgroundLines (title2.c:22)
     * renders the 440x299 gun-barrel interior as 299 separate 440x1 8-bit
     * INTENSITY rows, because 440x299 cannot fit in the N64's 4KB TMEM. Every
     * one of those rows failed here.
     *
     * MEASURED, one windowed run, EYE INTRO proven reached by title.c's own
     * gunbarrel_mode (3 -> 5 -> 6 -> 7 -> 9) with 299 one-texel-tall texrects
     * per frame. Rows 0, 149 and 298 at DL frame 1141 all arrived CORRECT:
     * tile fmt=4 siz=1 line=55 tmem=0 cms=cmt=CLAMP masks=maskt=0, the F3
     * naming 220 16-bit texels (gbi.h's 8b_LOAD_BLOCK halving, so 440 bytes),
     * the tmem lookup HIT, the source readable, and the decoded bytes not
     * merely non-zero but correctly SHAPED - row 0 min=0 max=9 mean=2 (the
     * dark top of the barrel), row 149 min=0 max=255 mean=129, row 298 min=0
     * max=253 mean=52. The texrect was equally correct: s=t=0, dsdx=dtdy=1024
     * (one texel per pixel), and a tile extent of exactly 440x1.
     *
     * The FIRST wrong value in the whole chain was here. g_tex_reject[0] read
     * 1, 150 and 299 at those three rows - exactly one per row, the whole
     * screen - so tile_texture returned 0 with TR_ACQUIRE and draw_texrect
     * fell through to its untextured path. That is why the background reads as
     * a smooth black-to-white gradient and nothing else: the measured
     * primitive colour was 0,0,0 at row 0, 127,127,127 at row 149 and
     * 254,254,254 at row 298 - title2.c:54's own top-to-bottom ramp, drawn
     * with no texel to modulate. A failed BINDING presenting as a correctly
     * placed quad in the flat primitive colour is the same signature B-027
     * left on the crosshair.
     *
     * The bound is now the one that is actually load-bearing. AREA against the
     * destination buffer is the real invariant - sl_tex_decode is handed
     * w * h * 4 as its capacity and g_texbuf holds TEX_MAX_TEXELS * 4 - and it
     * is unchanged for every texture that passed before: 256x256 still fits
     * exactly, and 440x1 is 440 texels of the 65536 available. The per-axis
     * cap is the DECODER's own declared maximum, SL_TEX_MAX_DIM
     * (sl_gfx_tex.h:58), the constant its row buffers are already sized to -
     * g_src32[SL_TEX_MAX_DIM / 4] and work[SL_TEX_MAX_DIM], sl_gfx_tex.c:504
     * and :887 - so the two layers now state ONE bound instead of two.
     * sl_tex_row_stride and sl_tex_data_size already accepted 440 unchanged;
     * this file's 256 was the only gate in the path.
     *
     * AN EARLIER ATTEMPT TOOK THE AXIS CAP FROM glGetIntegerv
     * (GL_MAX_TEXTURE_SIZE) AND SILENTLY DID NOTHING. Measured on a rebuilt
     * binary: the rows still rejected with reject[dim] = 1/150/299 at rows
     * 0/149/298, byte for byte the pre-repair reading. tex_acquire runs INSIDE
     * an open glBegin batch - the block below calls batch_end() before
     * glGenTextures for exactly that reason - and glGetIntegerv between
     * glBegin and glEnd is GL_INVALID_OPERATION, so it never wrote the
     * variable and the cached fallback stood forever. Same trap, same
     * function, one screenful apart. No GL query belongs on this path. */
    if (src == NULL || w == 0 || h == 0 ||
        w > (unsigned) SL_TEX_MAX_DIM || h > (unsigned) SL_TEX_MAX_DIM ||
        (unsigned long) w * (unsigned long) h > TEX_MAX_TEXELS) {
        g_tex_reject[0]++; return 0;
    }
    need = sl_tex_data_size(slfmt, (int) w, (int) h);
    if (need == 0) { g_tex_reject[1]++; return 0; }
    /* B-108. The bytes the stride walk CONSUMES: h rows that start `strb`
     * apart and each read one period. Equal to `need` whenever the stride
     * matches the period (stridetex 0 means "no stride of its own"), which
     * keeps the readability bound and the content-hash span bit-identical
     * to the previous behaviour for every such tile. */
    span = need;
    if (stridetex != 0u && stridetex != w) {
        unsigned rowb = sl_tex_row_stride(slfmt, (int) w);
        unsigned strb = sl_tex_row_stride(slfmt, (int) stridetex);
        if (rowb != 0u && strb != 0u)
            span = (h - 1u) * strb + rowb;
    }
    if (!mem_readable((unsigned long) src, span)) { g_tex_reject[2]++; return 0; }

    palneed = sl_tex_palette_entries(slfmt);
    if (palneed) {
        /* The F0 says how many entries it loaded; a GE palette is routinely
         * shorter than the format's 16/256 (numcolours is a byte read off the
         * image, image.c:202). Trusting the format instead reads whatever
         * follows the palette in the texpool, so take the smaller. */
        unsigned have = palents ? palents : palneed;
        if (have > palneed) have = palneed;
        palneed = have * 2u;
        if (pal == NULL || !mem_readable((unsigned long) pal, palneed)) {
            g_tex_reject[3]++; return 0;
        }
    }

    /* #47. Ask the provider. Under ORIGINAL this is one store read and an
     * immediate NULL; under a set it is two hash probes (pointer -> id,
     * (set, id) -> resident image), the file read happening once per (set,
     * id). A replacement is already at its final size, so the B-116
     * enhancement never resamples it (want_enh off), and the generation it
     * was taken under joins the cache key below. */
    rep = sl_texprov_lookup(src, w, h);
    if (rep != NULL) { rep_gen = sl_texprov_generation(); want_enh = 0; }

    /* #47 PART A. SL_TEX_HILITE (see tex_hilite_id): this image is painted a
     * flat colour instead of its artwork, so a captured frame says where it
     * lands. Decided HERE, before the cache probe, because want_enh is part
     * of the cache key - deciding it after the probe would leave a stored
     * entry and a later lookup disagreeing and re-uploading every draw. */
    if (tex_hilite_id() != -2) {
        int want = tex_hilite_id();
        if (want == -1) hil = (rep != NULL);
        else {
            int id = sl_texprov_id_of(src, NULL, NULL);
            hil = (id >= 0 && id == want);
        }
        if (hil) want_enh = 0;
    }

    /* tex_acquire runs once per triangle, so memoise the content hash on the
     * (src, size) pair - without this the walker re-hashed 256 bytes ~1700
     * times a frame for the handful of distinct sources actually in play. */
    if (src == g_key_src && span == g_key_need) {
        key = g_key_val;
    } else {
        key = tex_content_key(src, span);
        g_key_src = src; g_key_need = span; g_key_val = key;
    }
    for (i = 0; i < g_texcache_n; i++) {
        e = &g_texcache[i];
        if (e->src == (const void *) src && e->key == key &&
            e->pal == (const void *) pal && e->flags == texflags &&
            e->fmt == (unsigned) slfmt && e->w == w && e->h == h &&
            e->has_enh == (unsigned char) want_enh &&
            e->has_mips == (unsigned char) want_mips &&
            e->rep_gen == rep_gen) {
            unsigned d_s = 0, d_t = 0;
            if (e->wrap_s != tex_wrap(cms, masks, &d_s) ||
                e->wrap_t != tex_wrap(cmt, maskt, &d_t))
                g_tex_wrap_alias++;
            g_tex_hits++;
            if (e->gl == 0) g_tex_reject[7]++;
            g_tex_slot = (int) i;
            return e->gl;
        }
    }

    /* B-108. A stride of its own means the source rows are not the sampled
     * rows; hand the decoder the t*line+s walk instead. `rows == src`
     * otherwise, so the ordinary path is untouched. */
    rows = src;
    if (span != need) {
        rows = tex_src_rows(src, slfmt, w, h, stridetex);
        if (rows == NULL) {
            g_tex_fail++; g_tex_reject[4]++;
            tex_fail_trace(src, slfmt, w, h, stridetex, texflags, -1);
            return 0;
        }
    }
    rc = sl_tex_decode(rows, need, slfmt, (int) w, (int) h,
                       texflags, pal, palneed,
                       dst, (unsigned) (w * h * 4));
    if (rc != SL_TEX_OK) {
        g_tex_fail++; g_tex_reject[4]++;
        tex_fail_trace(src, slfmt, w, h, stridetex, texflags, rc);
        return 0;
    }
    g_tex_decoded++;
    if (sl_tex_pal_wrapped != 0u) {
        g_tex_palwrap_imgs++;
        g_tex_palwrap_texels += sl_tex_pal_wrapped;
        if (textrace_on())
            fprintf(stderr, "sl_textrace: decode PAL-WRAP src=%p slfmt=%d"
                            " %ux%u entries=%u wrapped-texels=%u (B-141)\n",
                    (const void *) src, slfmt, w, h, palneed >> 1,
                    sl_tex_pal_wrapped);
    }
    tex_dump(dst, w, h, slfmt, pal, palneed >> 1, g_ti_addr, src);
    tex_dump_by_id(dst, w, h, src);
    noisy_note(src, (unsigned) (unsigned long) pal, slfmt, w, h, texflags, dst);

    {   /* entropy of the SOURCE, reported once per distinct texture */
        float ent = tex_entropy(src, need);
        if (ent < g_tex_entropy_min) g_tex_entropy_min = ent;
        if (ent > g_tex_entropy_max) g_tex_entropy_max = ent;
        g_tex_entropy_sum += ent;
        g_tex_entropy_n++;
    }

    sum = 0;
    for (i = 0; i < w * h * 4; i++) sum = sum * 31u + dst[i];

    /* B-051. The decoded alpha channel, summarised while dst is still live.
     * SL_TEX_ALPHA=1 prints one line per DECODE - not per bind - so the
     * control comes for free: a level decodes ~100 images and the opaque ones
     * must read 255/255, which is what makes a reading of 0 on one texture
     * mean something rather than looking exactly like a broken dumper. */
    {
        unsigned amin = 255, amax = 0, azero = 0, n = w * h;
        double asum = 0.0;
        /* B-051 WORK ITEM 3. The decoded RGB, alongside the alpha.
         *
         * The question the pane raises is whether its final BLACK is what the
         * decoded texture and the RGB combiner naturally produce, or something
         * the alpha work introduced. The RGB equation settles half of it -
         * the tinted-glass word fc26a004 1f1093fb and the ORDINARY textured
         * room word fc26a004 1f1093ff have byte-identical RGB muxes
         * (rgb0 = (TEX1-TEX0)*LODFRAC+TEX0, rgb1 = (COMBINED-0)*SHADE+0;
         * ucode05_old.txt's FC table, with 8..15 in the b mux and 7 in the d
         * mux reading as the constant zero). They differ in ONE nibble, Ad1,
         * the alpha addend. So the pane is shaded by the same path as every
         * wall in the level and nothing here can force it black.
         *
         * This is the other half: what the texture itself carries. Same
         * free control as the alpha histogram - a level decodes ~100 images
         * and the colourful ones must read non-zero, so a reading of ~0 on
         * one texture is a measurement rather than a broken dumper. */
        unsigned rmax = 0, gmax = 0, bmax = 0;
        double rsum = 0.0, gsum = 0.0, bsum = 0.0;
        for (i = 0; i < n; i++) {
            unsigned a = dst[i * 4u + 3u];
            unsigned r = dst[i * 4u], g = dst[i * 4u + 1u];
            unsigned b = dst[i * 4u + 2u];
            if (a < amin) amin = a;
            if (a > amax) amax = a;
            if (a == 0u) azero++;
            asum += (double) a;
            if (r > rmax) rmax = r;
            if (g > gmax) gmax = g;
            if (b > bmax) bmax = b;
            rsum += (double) r; gsum += (double) g; bsum += (double) b;
        }
        g_dec_amin = amin; g_dec_amax = amax; g_dec_azero = azero;
        g_dec_atot = n; g_dec_asum = (unsigned) (asum / (n ? n : 1));
        g_dec_rmax = rmax; g_dec_gmax = gmax; g_dec_bmax = bmax;
        g_dec_rmean = (unsigned) (rsum / (n ? n : 1));
        g_dec_gmean = (unsigned) (gsum / (n ? n : 1));
        g_dec_bmean = (unsigned) (bsum / (n ? n : 1));
        if (getenv("SL_TEX_ALPHA") != NULL)
            fprintf(stderr, "sl_texa: src=%p pal=%p fmt=%d %ux%u flags=%u"
                            " alpha min=%u max=%u mean=%u zero=%u/%u"
                            " rgbmax=%u,%u,%u rgbmean=%u,%u,%u\n",
                    (const void *) src, (const void *) pal, slfmt, w, h,
                    texflags, amin, amax, g_dec_asum, azero, n,
                    rmax, gmax, bmax,
                    (unsigned) (rsum / (n ? n : 1)),
                    (unsigned) (gsum / (n ? n : 1)),
                    (unsigned) (bsum / (n ? n : 1)));
    }

    if (g_texcache_n >= TEX_CACHE_N) {
        /* Full. Reuse slot 0's GL name rather than leaking; the cache is
         * sized well above the ~100 distinct textures a frame uses. */
        g_texcache_n = 0;
    }
    e = &g_texcache[g_texcache_n++];

    /* Close the triangle batch FIRST. glGenTextures between glBegin and
     * glEnd is GL_INVALID_OPERATION and silently leaves the name at zero -
     * measured: exactly one texture ever got a name (gl=1) and every later
     * cache hit returned 0, which showed up as 552 of 916 triangles drawing
     * untextured with no error anywhere. */
    batch_end();
    if (e->gl == 0) glGenTextures(1, &e->gl);
    e->src = src; e->pal = pal; e->key = key; e->flags = texflags;
    e->fmt = (unsigned) slfmt; e->w = w; e->h = h; e->checksum = sum;
    e->rmax = g_dec_rmax; e->gmax = g_dec_gmax; e->bmax = g_dec_bmax;
    e->rmean = g_dec_rmean; e->gmean = g_dec_gmean; e->bmean = g_dec_bmean;
    e->amin = g_dec_amin; e->amax = g_dec_amax; e->azero = g_dec_azero;
    e->atot = g_dec_atot; e->asum = g_dec_asum;

    glBindTexture(GL_TEXTURE_2D, e->gl);
    /* B-119: trilinear for the detail-blend's far image; bilinear otherwise. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    want_mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    e->has_mips = (unsigned char) want_mips;
    /* ucode05_old.txt "F5 rdp_settile" lower word: clamp s/t 0x00000200 /
     * 0x00080000, mirror s/t 0x00000100 / 0x00040000 - the same 1=mirror,
     * 2=clamp encoding gbi.h gives G_TX_MIRROR / G_TX_CLAMP. Recorded on the
     * texture object rather than at bind time because glTexParameteri is
     * illegal between glBegin and glEnd; a source drawn with two different
     * wrap modes keeps the first, which no GE tile does today. */
    e->wrap_s = tex_wrap(cms, masks, &g_tclamp0_s);
    e->wrap_t = tex_wrap(cmt, maskt, &g_tclamp0_t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, e->wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, e->wrap_t);
    e->has_enh = (unsigned char) want_enh;
    e->rep_gen = rep_gen;
    e->rep_id = rep ? rep->id : 0u;
    e->rep_w = rep ? rep->phys_w : 0u;
    e->rep_h = rep ? rep->phys_h : 0u;
    /* #47 PART A. One registry probe per UPLOAD (not per draw), so the
     * coverage census can rank a frame's ids without a second bridge. */
    {   int cid = texcov_armed() ? sl_texprov_id_of(src, NULL, NULL) : -1;
        e->cov_id = (cid >= 0 && (unsigned) cid < SL_TEXPROV_MAX_ID)
                        ? (unsigned) cid + 1u : 0u;
    }
    if (hil) {
        /* #47 PART A. The flat colour over the decode's own alpha - a cutout
         * stays a cutout. `dst` is the decode scratch and is dead after this
         * upload, so painting it here costs nothing and keeps the flag out of
         * every other branch. */
        const unsigned char *hc = tex_hilite_rgb();
        unsigned n = w * h, k;
        for (k = 0; k < n; k++) {
            dst[k * 4u + 0u] = hc[0];
            dst[k * 4u + 1u] = hc[1];
            dst[k * 4u + 2u] = hc[2];
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei) w, (GLsizei) h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, dst);
        g_tex_hilited++;
    } else
    if (rep != NULL) {
        /* #47. The provider's image, at ITS physical size, in place of the
         * decode. The same principle B-116 already relies on: the GL texture
         * is larger, the S/T coordinates the draw path emits divide by the
         * tile's logical w x h (tex_apply / draw_texrect), so the pattern
         * repeats exactly as before with more texels per repeat. The wrap
         * modes set above are the tile's own. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     (GLsizei) rep->phys_w, (GLsizei) rep->phys_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rep->rgba);
        g_tex_replaced++;
    } else
    if (want_enh) {
        /* B-116. Resample the decoded RGBA up by an integer factor and upload
         * THAT, with the tile's own wrap at the edges so a repeating texture
         * stays seamless. The GL texture is larger; the S/T coordinates the
         * draw path emits are unchanged (GL normalises to [0,1]), so the
         * pattern repeats exactly as before with more texels per repeat. */
        static unsigned char g_enhbuf[TEX_MAX_DIM * TEX_MAX_DIM * 4];
        unsigned f = enhance_factor(w, h);
        int clamp_s = (e->wrap_s != (GLint) GL_REPEAT);
        int clamp_t = (e->wrap_t != (GLint) GL_REPEAT);
        enh_upscale(dst, w, h, g_enhbuf, f, clamp_s, clamp_t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     (GLsizei) (w * f), (GLsizei) (h * f), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, g_enhbuf);
        g_tex_enhanced++;
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei) w, (GLsizei) h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, dst);
    }
    if (want_mips) {
        /* B-119. The complete pyramid down to 1x1 - these headers predate
         * GL_TEXTURE_MAX_LEVEL, so an incomplete chain would sample black.
         * #47: a replacement's pyramid halves from ITS level 0, so the two
         * scratch levels are sized to half the provider's largest image
         * (SL_TEXPROV_MAX_DIM / 2 squared), which also holds every decode. */
        static unsigned char mbuf0[(SL_TEXPROV_MAX_DIM / 2u) * (SL_TEXPROV_MAX_DIM / 2u) * 4u];
        static unsigned char mbuf1[(SL_TEXPROV_MAX_DIM / 2u) * (SL_TEXPROV_MAX_DIM / 2u) * 4u];
        const unsigned char *cur = (rep && !hil) ? rep->rgba : dst;
        unsigned char *nxt = mbuf0, *oth = mbuf1, *tmp;
        unsigned cw = (rep && !hil) ? rep->phys_w : w;
        unsigned ch = (rep && !hil) ? rep->phys_h : h;
        unsigned nw, nh, level = 1;
        while (cw > 1u || ch > 1u) {
            mip_box_halve(cur, cw, ch, nxt, &nw, &nh);
            glTexImage2D(GL_TEXTURE_2D, (GLint) level, GL_RGBA,
                         (GLsizei) nw, (GLsizei) nh, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nxt);
            cur = nxt; tmp = nxt; nxt = oth; oth = tmp;
            cw = nw; ch = nh; level++;
        }
    }
    g_tex_uploads++;
    g_tex_gl_bound = e->gl;      /* the upload left this bound */
    g_tex_slot = (int) (e - g_texcache);
    return e->gl;
}

/* The 2D path can leave the texture environment in a font-specific mode; 3D
 * geometry has to put it back. Defined with the rest of the 2D state below.
 * 0 = GL_MODULATE, 1 = colour replaced by the primitive colour. */
static int  g_texenv = -1;
static void texenv_set(int mode);


/* Reasons a tile could not be turned into a GL texture. Shared by the 3D and
 * the 2D paths so both report against the same vocabulary. */
#define TR_OK        0
#define TR_NOSRC     1                 /* no F3 loadblock has named pixels */
#define TR_TILESIZE  2                 /* F2 settilesize is inside-out */
#define TR_FMT       3                 /* fmt/siz pair has no decoder (YUV) */
#define TR_ACQUIRE   4                 /* decode/upload refused - see reject[] */

/* Which reading of the odd-row swizzle this load gets. The rule is B-020's -
 * dxt == 0 means the DRAM image carries the net swap Rare pre-compensated for,
 * anything else means it does not - and SL_TEX_ROWSWAP forces either answer so
 * both can be dumped and looked at. Unset keeps the rule. */
static unsigned rowswap_flag(unsigned dxt)
{
    static int opt = -1;
    if (opt < 0) {
        const char *s = getenv("SL_TEX_ROWSWAP");
        opt = (s != NULL && (*s == '0' || *s == '1')) ? *s - '0' : 2;
    }
    if (opt == 2) return (dxt == 0) ? SL_TEX_FLAG_ROWSWAPPED : 0u;
    return opt ? SL_TEX_FLAG_ROWSWAPPED : 0u;
}

/* ---- B-023: does the tile's own stride agree with the decoded width? -----
 *
 * The decoder derives its row stride from the WIDTH alone (sl_tex_row_stride,
 * Rare's texSwapAltRowBytes padding). The RDP is told the stride separately,
 * in the F5 settile `line` field, and nothing here has ever compared the two.
 * A disagreement is precisely the "line width disagreeing with the tile"
 * failure, so make it a measurement rather than an assumption.
 *
 * `line` is in 64-bit words. Authority: ucode05_old.txt "F5 rdp_settile",
 * whose worked example "F5600600 00FD8360 would load a 48x48 intensity+alpha
 * image with 4bit pixel data" decodes under this file's own field masks to
 * fmt=3 (IA), siz=0 (4bit), line=3 - and 48 texels at 4bpp is 24 bytes, i.e.
 * exactly 3 eight-byte words. So line * 8 is the byte stride.
 *
 * Small fixed table, deduplicated on (src, w, h), no allocation. */
#define STRD_MAX 32
static const unsigned char *g_strd_src[STRD_MAX];
static unsigned g_strd_fmt[STRD_MAX], g_strd_w[STRD_MAX], g_strd_h[STRD_MAX];
static unsigned g_strd_line[STRD_MAX], g_strd_want[STRD_MAX];
static unsigned g_strd_texels[STRD_MAX], g_strd_draws[STRD_MAX];
static unsigned g_strd_n, g_strd_bad;

static void strd_note(const unsigned char *src, int slfmt, unsigned w,
                      unsigned h, unsigned line)
{
    unsigned i, want = sl_tex_row_stride(slfmt, (int) w);

    for (i = 0; i < g_strd_n; i++)
        if (g_strd_src[i] == src && g_strd_w[i] == w && g_strd_h[i] == h) {
            g_strd_draws[i]++;
            return;
        }
    if (line * 8u != want) g_strd_bad++;
    if (g_strd_n >= STRD_MAX) return;
    i = g_strd_n++;
    g_strd_src[i] = src; g_strd_fmt[i] = (unsigned) slfmt;
    g_strd_w[i] = w; g_strd_h[i] = h;
    g_strd_line[i] = line; g_strd_want[i] = want;
    g_strd_texels[i] = g_load_texels; g_strd_draws[i] = 1;
}

/* ---- B-023: which decoded images actually LOOK like static? -------------
 *
 * With the stride cross-check above coming back clean, the offending texture
 * still has to be named before anything can be said about it, and "the band
 * on the horizon" is not a handle the engine has. So measure the thing the
 * screenshot shows: salt-and-pepper is high mean absolute difference between
 * horizontally ADJACENT texels, which no ordinary GoldenEye wall texture has.
 * Reported per distinct decoded image alongside its format and dimensions, so
 * the noisy one names itself. Diagnostic only - never on the render path when
 * the report is off. */
#define NOISY_MAX 64
static const unsigned char *g_noisy_src[NOISY_MAX];
static unsigned g_noisy_fmt[NOISY_MAX], g_noisy_w[NOISY_MAX];
static unsigned g_noisy_h[NOISY_MAX], g_noisy_pal[NOISY_MAX];
static unsigned g_noisy_flags[NOISY_MAX], g_noisy_rough[NOISY_MAX];
/* Everything the two commands SAID, so a disagreement between them is
 * visible rather than inferred: the FD's own fmt/siz/width, the tile's
 * fmt/siz/line, the F2 extents and the F3 texel count. */
static unsigned g_noisy_tfmt[NOISY_MAX], g_noisy_tsiz[NOISY_MAX];
static unsigned g_noisy_line[NOISY_MAX], g_noisy_ffmt[NOISY_MAX];
static unsigned g_noisy_fsiz[NOISY_MAX], g_noisy_fw[NOISY_MAX];
static unsigned g_noisy_tex[NOISY_MAX], g_noisy_uls[NOISY_MAX];
static unsigned g_noisy_lrs[NOISY_MAX], g_noisy_ult[NOISY_MAX];
static unsigned g_noisy_lrt[NOISY_MAX], g_noisy_tmem[NOISY_MAX];
static unsigned g_noisy_n;

/* Mean |dI| between horizontally adjacent texels, 0..255. */
static unsigned tex_roughness(const unsigned char *rgba, unsigned w, unsigned h)
{
    unsigned x, y, n = 0;
    unsigned long acc = 0;

    if (w < 2u || h == 0u) return 0u;
    for (y = 0; y < h; y++)
        for (x = 1; x < w; x++) {
            const unsigned char *a = rgba + ((y * w + x - 1u) * 4u);
            const unsigned char *b = rgba + ((y * w + x) * 4u);
            int d = (int) a[0] - (int) b[0];
            acc += (unsigned long) (d < 0 ? -d : d);
            n++;
        }
    return n ? (unsigned) (acc / n) : 0u;
}

static void noisy_note(const unsigned char *src, unsigned pal, int slfmt,
                       unsigned w, unsigned h, unsigned flags,
                       const unsigned char *rgba)
{
    unsigned i;
    for (i = 0; i < g_noisy_n; i++)
        if (g_noisy_src[i] == src && g_noisy_w[i] == w && g_noisy_h[i] == h)
            return;
    if (g_noisy_n >= NOISY_MAX) return;
    i = g_noisy_n++;
    g_noisy_src[i] = src; g_noisy_fmt[i] = (unsigned) slfmt;
    g_noisy_w[i] = w; g_noisy_h[i] = h;
    g_noisy_pal[i] = pal; g_noisy_flags[i] = flags;
    g_noisy_rough[i] = tex_roughness(rgba, w, h);
    {   const struct sl_tile *t = &g_tile[g_tex_tile & 7];
        g_noisy_tfmt[i] = t->fmt; g_noisy_tsiz[i] = t->siz;
        g_noisy_line[i] = t->line; g_noisy_tmem[i] = t->tmem;
        g_noisy_uls[i] = t->uls; g_noisy_lrs[i] = t->lrs;
        g_noisy_ult[i] = t->ult; g_noisy_lrt[i] = t->lrt;
        /* The FD fields must come from the load that filled THIS tile's tmem,
         * not from whichever settextureimage ran most recently - reading the
         * global here is the very confusion the tmem map exists to remove,
         * and it would report a mismatch on a correctly paired tile. */
        {   const struct sl_tmem_ent *e = tmem_find(t->tmem);
            if (e != NULL) {
                g_noisy_ffmt[i] = e->fmt; g_noisy_fsiz[i] = e->siz;
                g_noisy_tex[i] = e->texels;
            } else {
                g_noisy_ffmt[i] = g_ti_fmt; g_noisy_fsiz[i] = g_ti_siz;
                g_noisy_tex[i] = g_load_texels;
            }
        }
        g_noisy_fw[i] = g_ti_w;
    }
}

/* Resolve one RDP tile to a GL texture name, or 0 with *why set. The tile's
 * dimensions come from the notes' own formulas, ucode05.txt "F2
 * rdp_settilesize": width = (lrs - uls)/4 + 1, height = (lrt - ult)/4 + 1. */
/* ======= B-051 WORK ITEM 4: the EXACT pane draw's texture state ==========
 *
 * Where this comes from. SL_GLASS_MARK=2 (magenta, texture DROPPED) fills the
 * facility pane's aperture with magenta, and SL_GLASS_MARK=1 (magenta,
 * texture KEPT, so magenta is multiplied by the texel) comes back black on
 * the same frame of the same replay. Coverage, draw order, geometry and
 * alpha are therefore all sound and the RGB the pane SAMPLES is zero.
 *
 * What that pair cannot say is WHICH state produces the zero, because every
 * aggregate this file already prints - sl_aeqrgb, sl_texa, the sl_tex report
 * - spans every draw of its kind in the frame, and facility has twenty
 * tinted-glass panes. An aggregate cannot describe one of them. This is the
 * per-draw dump.
 *
 * SL_TEXPROBE=1. DEFAULT OFF, and it changes no draw state whatsoever: it
 * reads what tile_texture has already resolved and prints it. Once per
 * DISTINCT resolved state, so a static pane costs one line per run.
 *
 * What it prints, and why each field is there:
 *
 *   combiner word + cycle type      - so a line can be attributed to a draw
 *   F5 settile for the render tile  - fmt, siz, line, tmem, palette,
 *       clamp/mirror, and BOTH SHIFTS. Authority: ucode05_old.txt
 *       "F5 rdp_settile" (ucode05.txt's own F5 entry says only "MOVED TO
 *       SETTILE.HTM" and that file is not in the corpus). shifts/shiftt are
 *       printed because they were dropped here once already and cost a 4x
 *       sizing error - and unit 0's texcoord path STILL does not apply them,
 *       only unit 1 does, so a non-zero shift on tile 0 is a live defect
 *       rather than a curiosity.
 *   F2 settilesize, RAW 10.2 corners AND the derived extent, kept apart on
 *       purpose: ucode05.txt's width=(lrs-uls)/4+1 is the derivation, not
 *       the image size. B-048 measured a tile whose real image is sized from
 *       the LOAD instead, so "do not infer dimensions from settilesize
 *       alone" is a measured rule here, not caution.
 *   FD settextureimage fmt/siz/width/address (ucode05.txt "FD
 *       rdp_settextureimage").
 *   the F3 loadblock THIS TILE'S TMEM ADDRESS resolves to - the B-023 rule.
 *       "The last loadblock" is the wrong answer and the comment at g_load_src
 *       records why.
 *   TLUT address, mode and entry count.
 *   the resolved sl-format, GL name, cache slot, decoded extent, flags.
 *   the DECODED texel statistics OF THAT TEXTURE, read from its own cache
 *       entry - never from g_dec_*, which holds whatever decoded last. A
 *       cache HIT does not decode at all, so g_dec_* would be another
 *       texture's numbers wearing this one's label.
 *
 * SL_TEXPROBE_W0 / SL_TEXPROBE_W1 = <hex>  narrow to one combiner word.
 * SL_TEXPROBE_FIRST / SL_TEXPROBE_LAST     bound it by sl_record_index(),
 *       the unit the shot windows and the run marks are already in.
 * SL_TEXPROBE_DUMP=<prefix>  writes the resolved texture's RGB and its ALPHA
 *       as images. It RE-DECODES into the shared scratch buffer, because the
 *       upload does not retain the pixels and a cache hit never produced them.
 *
 * THE POSITIVE CONTROL IS BUILT IN AND NOT OPTIONAL. With no W0/W1 filter the
 * probe reports every distinct textured state in the window, so ordinary room
 * geometry in the same frame is dumped beside the pane, through the identical
 * code path. A glass probe that has never reported a known-good textured
 * surface correctly proves nothing about the glass.
 */
/* Tentative declarations: the combiner words and the cycle type are defined
 * with their own commands further down, and this probe sits beside
 * tile_texture, which is above them. Same C tentative-definition idiom the
 * file already uses for g_cc_a_prim1 and the aeq_ forward declarations. */
static unsigned g_cc_w0, g_cc_w1;
static unsigned g_cycle_type;

static int texprobe_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEXPROBE");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}

/* ST accumulation for the probed word. tile_texture cannot see the vertices,
 * so the range is gathered where they are emitted and reported at frame end.
 * In TEXELS: the stored s/t is texels<<5 - see the SL_ST_SCALE comment in the
 * vertex path, which the notes settle by worked example. */
static long g_tp_smin, g_tp_smax, g_tp_tmin, g_tp_tmax;
static unsigned g_tp_stn;
static int g_tp_word_now;              /* this draw matches the W0/W1 filter */

/* PER-DRAW, not per frame. The frame-level ST range above aggregates every
 * textured draw and is therefore the same kind of number that made this
 * investigation take as long as it has - facility draws twenty panes and an
 * aggregate cannot describe one of them.
 *
 * When texprobe_note() prints a state it arms g_tp_pending, and the NEXT
 * vertices emitted - which belong to that exact draw, because tex_apply runs
 * immediately before them - are captured and printed. That closes the last
 * gap in "why is texel * shade zero here": it reports the ST actually
 * sampled and the vertex colour actually multiplied in, for THIS quad. */
static int g_tp_pending, g_tp_pend_n;
static short g_tp_pv_s[8], g_tp_pv_t[8];
static unsigned char g_tp_pv_r[8], g_tp_pv_g[8], g_tp_pv_b[8], g_tp_pv_a[8];

static int texprobe_word_match(void)
{
    static int checked;
    static unsigned long w0, w1;
    if (!checked) {
        const char *a = getenv("SL_TEXPROBE_W0");
        const char *b = getenv("SL_TEXPROBE_W1");
        w0 = a ? strtoul(a, NULL, 16) : 0ul;
        w1 = b ? strtoul(b, NULL, 16) : 0ul;
        checked = 1;
    }
    if (w0 == 0ul && w1 == 0ul) return 1;
    if (w0 != 0ul && (unsigned long) g_cc_w0 != w0) return 0;
    if (w1 != 0ul && (unsigned long) g_cc_w1 != w1) return 0;
    return 1;
}

static int texprobe_in_window(void)
{
    static int checked;
    static unsigned long lo, hi;
    extern unsigned sl_record_index(void);
    unsigned r;
    if (!checked) {
        const char *a = getenv("SL_TEXPROBE_FIRST");
        const char *b = getenv("SL_TEXPROBE_LAST");
        lo = a ? strtoul(a, NULL, 10) : 0ul;
        hi = b ? strtoul(b, NULL, 10) : 0xfffffffful;
        checked = 1;
    }
    r = sl_record_index();
    return (unsigned long) r >= lo && (unsigned long) r <= hi;
}

#define TEXPROBE_SEEN_N 64
static unsigned g_tp_seen[TEXPROBE_SEEN_N];
static unsigned g_tp_seen_n, g_tp_dumped;

static void texprobe_note(const struct sl_tile *t, unsigned tile,
                          const unsigned char *src, unsigned dxt,
                          const struct sl_tmem_ent *ld,
                          int slfmt, unsigned w, unsigned h,
                          unsigned texflags, GLuint name, unsigned stridetex)
{
    extern unsigned sl_record_index(void);
    const struct sl_texent *te;
    unsigned key, i;
    unsigned dw, dh;

    if (!texprobe_on() || !texprobe_word_match() || !texprobe_in_window())
        return;

    /* One line per DISTINCT resolved state. The key deliberately includes the
     * combiner word AND the tile identity AND the resolved GL name: two panes
     * sharing a texture but described by different tile state are two
     * different answers to the question being asked. */
    key = 2166136261u;
#define TP_MIX(v) do { key ^= (unsigned) (v); key *= 16777619u; } while (0)
    TP_MIX(g_cc_w0); TP_MIX(g_cc_w1); TP_MIX(tile);
    TP_MIX(t->fmt); TP_MIX(t->siz); TP_MIX(t->line); TP_MIX(t->tmem);
    TP_MIX(t->pal); TP_MIX(t->cms); TP_MIX(t->cmt);
    TP_MIX(t->shifts); TP_MIX(t->shiftt);
    TP_MIX(t->uls); TP_MIX(t->ult); TP_MIX(t->lrs); TP_MIX(t->lrt);
    TP_MIX((unsigned) (unsigned long) src); TP_MIX((unsigned) name);
    TP_MIX((unsigned) slfmt); TP_MIX(w); TP_MIX(h); TP_MIX(texflags);
#undef TP_MIX
    for (i = 0; i < g_tp_seen_n; i++) if (g_tp_seen[i] == key) return;
    if (g_tp_seen_n >= TEXPROBE_SEEN_N) return;
    g_tp_seen[g_tp_seen_n++] = key;

    /* The decoded statistics come from the slot recorded AT RESOLUTION. A
     * by-name search is wrong: 256 cache slots share GL names and that error
     * has already been made in this file (see g_tex_slot). */
    te = (g_tex_slot >= 0) ? &g_texcache[g_tex_slot] : NULL;
    dw = ((t->lrs - t->uls) >> 2) + 1u;
    dh = ((t->lrt - t->ult) >> 2) + 1u;

    fprintf(stderr,
        "sl_texprobe: read=%u cc=%08x %08x cyc=%u tile=%u\n"
        "  F5   fmt=%u siz=%u line=%u tmem=%u pal=%u cms=%u cmt=%u"
        " shifts=%u shiftt=%u\n"
        "  F2   uls=%u ult=%u lrs=%u lrt=%u (10.2)  ->  derived %ux%u\n"
        "  FD   fmt=%u siz=%u width=%u addr=%p\n"
        "  F3   %s src=%p dxt=%u texels=%u fmt=%u siz=%u\n"
        "  TLUT addr=%p mode=%u entries=%u off=%u\n"
        "  GL   slfmt=%d name=%u slot=%d decoded=%ux%u stride=%u flags=%u"
        " (rowswapped=%u)\n",
        sl_record_index(), g_cc_w0, g_cc_w1, g_cycle_type, tile,
        t->fmt, t->siz, t->line, t->tmem, t->pal, t->cms, t->cmt,
        t->shifts, t->shiftt,
        t->uls, t->ult, t->lrs, t->lrt, dw, dh,
        g_ti_fmt, g_ti_siz, g_ti_w, (const void *) g_ti_addr,
        ld ? "tmem-map" : "FALLBACK-last-load",
        (const void *) src, dxt, ld ? ld->texels : 0u,
        ld ? ld->fmt : 0u, ld ? ld->siz : 0u,
        (const void *) g_tlut_addr, g_tlut_mode, g_tlut_n, g_tlut_off,
        slfmt, (unsigned) name, g_tex_slot, w, h, stridetex, texflags,
        (texflags & SL_TEX_FLAG_ROWSWAPPED) ? 1u : 0u);

    if (te != NULL)
        fprintf(stderr,
            "  TEXELS decoded rgbmax=%u,%u,%u rgbmean=%u,%u,%u"
            " alpha min=%u max=%u mean=%u zero=%u/%u"
            "  #47 uploaded=%s id=%04x phys=%ux%u gen=%u\n",
            te->rmax, te->gmax, te->bmax, te->rmean, te->gmean, te->bmean,
            te->amin, te->amax, te->asum, te->azero, te->atot,
            te->rep_gen ? "PROVIDER" : "decode", te->rep_id,
            te->rep_w, te->rep_h, te->rep_gen);
    else
        fprintf(stderr, "  TEXELS no cache slot recorded - REPORT NOTHING\n");

    g_tp_pending = 1;                  /* capture this draw's own vertices */
    g_tp_pend_n = 0;

    /* Re-decode and write the pixels. The upload does not retain them and a
     * cache hit never produced them, so this is the only way to SHOW the
     * texture rather than describe it. g_texbuf is the shared scratch the
     * decoder writes into and nothing retains it past the upload. */
    {   const char *pre = getenv("SL_TEXPROBE_DUMP");
        if (pre != NULL && g_tp_dumped < 32u) {
            unsigned need = sl_tex_data_size(slfmt, (int) w, (int) h);
            unsigned span = need;
            const unsigned char *rows = src;
            unsigned palneed = sl_tex_palette_entries(slfmt);
            const unsigned char *pal = g_tlut_addr;
            unsigned have = g_tlut_n ? g_tlut_n : palneed;
            if (have > palneed) have = palneed;
            palneed = have * 2u;
            /* B-108. The same stride walk the binding took, so the dump
             * shows the texture that was actually sampled. */
            if (stridetex != 0u && stridetex != w) {
                unsigned rowb = sl_tex_row_stride(slfmt, (int) w);
                unsigned strb = sl_tex_row_stride(slfmt, (int) stridetex);
                if (rowb != 0u && strb != 0u)
                    span = (h - 1u) * strb + rowb;
            }
            if (need != 0 && mem_readable((unsigned long) src, span) &&
                (span == need ||
                 (rows = tex_src_rows(src, slfmt, w, h, stridetex)) != NULL) &&
                (palneed == 0 ||
                 (pal != NULL && mem_readable((unsigned long) pal, palneed))) &&
                sl_tex_decode(rows, need, slfmt, (int) w, (int) h, texflags,
                              pal, palneed, g_texbuf,
                              (unsigned) (w * h * 4)) == SL_TEX_OK) {
                static unsigned char rgb[TEX_MAX_DIM * TEX_MAX_DIM * 3];
                static unsigned char av[TEX_MAX_DIM * TEX_MAX_DIM * 3];
                char path[512];
                unsigned n = w * h, j;
                unsigned id = g_tp_dumped++;
                for (j = 0; j < n; j++) {
                    rgb[j * 3 + 0] = g_texbuf[j * 4 + 0];
                    rgb[j * 3 + 1] = g_texbuf[j * 4 + 1];
                    rgb[j * 3 + 2] = g_texbuf[j * 4 + 2];
                    av[j * 3 + 0] = av[j * 3 + 1] = av[j * 3 + 2] =
                        g_texbuf[j * 4 + 3];
                }
                dump_path(path, sizeof path, pre, id, "rgb");
                dump_ppm(path, rgb, w, h);
                dump_path(path, sizeof path, pre, id, "alpha");
                dump_ppm(path, av, w, h);
                fprintf(stderr, "  DUMP %s-%02u-{rgb,alpha}.ppm  %ux%u\n",
                        pre, id, w, h);
            } else {
                fprintf(stderr, "  DUMP FAILED - re-decode refused"
                                " (need=%u readable=%d)\n",
                        need, mem_readable((unsigned long) src, need));
            }
        }
    }
}

/* B-088. How many tile resolutions the mask rule actually MOVED, per axis.
 * A count of zero and a count that was never taken look identical in a log,
 * so this is reported unconditionally with the texture census rather than
 * behind its own flag - the containment claim for Facility is this number. */
static unsigned g_tmask_w, g_tmask_h;

/* SL_TILE_MASK=0 restores the pre-B-088 sizing. Default ON. */
static int tile_mask_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TILE_MASK");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* B-102. 2^mask is the WRAP EXPONENT, not the row stride. See the block at
 * the resize below. SL_TILE_LINE=0 restores B-088's `1 << masks` width from
 * the same binary, so the two can be captured back to back. Default on: the
 * mask width is the defect. */
static int tile_line_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TILE_LINE");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* Texels one tmem row holds, from the tile's `line`. Defined below beside
 * TEXEL1, which was its first caller; declared here because the render tile
 * needs the same number and for the same reason. */
static unsigned tile_line_texels(const struct sl_tile *t);

/* B-108. The wrap period versus the row stride - see the block at
 * tex_src_rows. SL_TILE_PERIOD=0 restores B-102's width-from-stride sizing
 * from the same binary, so the two can be captured back to back. Default
 * on: the stride width is the defect. */
static int tile_period_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TILE_PERIOD");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* How many resizes took the row stride rather than the mask (B-102). */
static unsigned g_tline_w;

/* B-107. THE F2 WINDOW MAY BE INSIDE-OUT ON A WRAPPING AXIS.
 *
 * B-088 already established that settilesize is the CLAMP rectangle, not the
 * image extent, and on a repeating axis the extent comes from mask/line. The
 * missing consequence: on such an axis the clamp window's shape cannot be
 * grounds for rejecting the tile either. Dam's water is the witness - its
 * setup list (src/game/unk_092E50.c MipMap2C_Something2_Setup) carries
 * settilesize commands whose lrs/lrt are FIXED at 0 while the game animates
 * uls/ult through 0..255 every frame as the scroll, so lrs < uls on all but
 * a handful of frames. Measured live, windowed Dam, 900 frames: 12289 draws
 * under the water combiner, 110 bound, 12179 rejected TR_TILESIZE - 99.1% of
 * the water untextured, which is the owner's missing water texture. The
 * inside-out check survives on clamped axes, where the window IS the extent.
 * SL_TILE_WRAPWIN=0 restores the whole-rectangle reject. Default on. */
static int tile_wrapwin_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TILE_WRAPWIN");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

/* B-107. The tile origin (F2 uls/ult, 10.2 texels) as a COORDINATE OFFSET.
 * The RDP subtracts the tile's upper-left from the shifted per-pixel s/t
 * before mask/mirror/clamp; a renderer that ignores it samples the same
 * window forever, which is why the water never moved even when it was
 * textured. gbi.h:3446 gDPSetTileSize's uls/ult are the same 10.2 units the
 * F2 note gives (ucode05.txt l.888-905); the notes are silent on the
 * subtraction itself (not_covered) and the authority is the measured scroll:
 * the writer advances uls/ult every frame (fb2d812f's 4000-tick harness and
 * the live capture above) and the cartridge water visibly flows.
 * SL_TILE_UL=0 restores the origin-blind coordinates. Default on. */
static int tile_ul_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TILE_UL");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

static GLuint tile_texture(unsigned tile, unsigned *ow, unsigned *oh, int *why)
{
    const struct sl_tile *t = &g_tile[tile & 7];
    const struct sl_tmem_ent *ld;
    const unsigned char *src;
    unsigned w, h, dxt;
    unsigned stridetex = 0;    /* B-108: 0 = rows advance by the width */
    GLuint name;
    int slfmt;

    *why = TR_OK;

    /* B-023: resolve the pixels through THIS tile's tmem address, not through
     * whichever loadblock happened to run last. See the block at g_load_src.
     * A tmem address no load has filled falls back to the old rule rather
     * than dropping the triangle: the map is cold at the start of a task, and
     * an untextured hole is a worse answer than the previous one.
     * B-128: the answer is the most recent load COVERING the address, read
     * from the byte that address stands for inside it (see tmem_resolve). At
     * a load's own start - every tile this path resolved before - that is
     * the same source as ever. */
    {   unsigned off = 0;
        ld = tmem_resolve(t->tmem, &off);
        if (ld != NULL) {
            src = ld->src + off; dxt = ld->dxt; g_tmem_hit++;
            if (off != 0) g_tmem_inner++;
        }
    }
    if (ld == NULL) {
        src = g_load_src; dxt = g_load_dxt; g_tmem_miss++;
    }

    if (src == NULL)                      { *why = TR_NOSRC;    return 0; }
    /* B-107. Inside-out F2 windows reject the tile ONLY on an axis whose
     * extent actually derives from the window - a clamped axis, or one with
     * no mask. A wrapping axis takes its extent from mask/line below and its
     * clamp window is free to be degenerate; Dam's animated water tiles are
     * exactly that shape (lrs/lrt pinned at 0, uls/ult scrolling). See the
     * block at tile_wrapwin_on. */
    {
        int wrap_s = tile_wrapwin_on() && tile_mask_on()
                     && !(t->cms & SL_TX_CLAMP) && t->masks != 0u;
        int wrap_t = tile_wrapwin_on() && tile_mask_on()
                     && !(t->cmt & SL_TX_CLAMP) && t->maskt != 0u;
        if ((!wrap_s && t->lrs < t->uls) || (!wrap_t && t->lrt < t->ult))
            { *why = TR_TILESIZE; return 0; }
    }
    w = (t->lrs >= t->uls) ? ((t->lrs - t->uls) >> 2) + 1 : 0u;
    h = (t->lrt >= t->ult) ? ((t->lrt - t->ult) >> 2) + 1 : 0u;
    /* B-088. SETTILESIZE IS THE CLAMP RECTANGLE, NOT THE IMAGE EXTENT.
     *
     * On a REPEATING axis the RDP wraps the coordinate at 2^mask and samples
     * the whole of that span; uls/lrs bound the CLAMP window and say nothing
     * about how much image there is. Sizing the GL texture from the F2 span
     * alone is therefore right only while the two agree - which is what
     * B-077 measured on Facility ("2^mask equals the renderer's divisor on
     * every REPEATING axis; the six NPOT tiles that disagree are clamped on
     * exactly the disagreeing axis"). That is a statement about Facility,
     * not a property of the RDP, and the Rareware logo is where it breaks.
     *
     * MEASURED, RAREWARE. D_02004758 (assets/rarewarelogo.c:1245) issues
     *
     *     gsSPTexture(0x1C81, 0x1426, 0, 0, 1),
     *     gsDPSetTileSize(0, 46, 116, 124, 124),
     *
     * over a tile that title.c:363 loaded with gDPLoadTextureBlock(...,
     * G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, G_TX_WRAP, G_TX_WRAP, 5, 5,
     * ...) - so cms=cmt=WRAP and masks=maskt=5, i.e. the RDP wraps at 32 on
     * both axes over the full 32x32 environment map. The F2 span is
     * ((124-46)>>2)+1 = 20 by ((124-116)>>2)+1 = 3, and this renderer decoded
     * and bound a 20x3 image: 242 of the logo's 260 triangles sampled a
     * three-texel-tall strip of a 32x32 reflection map. The remaining 18
     * take the sibling tile, which carries the loadblock's own 0,0,124,124
     * and was therefore always 32x32 - which is exactly why the logo drew
     * with its shape and a texture but the wrong material finish.
     *
     * Authority for the field positions is include/PR/gbi.h:3415 gsDPSetTile
     * (`_SHIFTL(maskt,14,4)`, `_SHIFTL(masks,4,4)`) and :393 G_TX_CLAMP=0x2.
     * The notes corpus is silent on settilesize-versus-mask - searched, and a
     * not_covered entry records it - so the rule is taken from the header and
     * from the consequence measured above, and stated as narrowly as it can
     * be: an axis is resized ONLY when it repeats AND names a mask. A clamped
     * axis keeps the F2 span exactly as before, which is what leaves B-048's
     * shifted explosion tiles and B-077's six NPOT clamped tiles untouched.
     *
     * SL_TILE_MASK=0 restores the pre-B-088 sizing from the same binary. */
    /* B-102. 2^MASK IS THE WRAP EXPONENT. THE ROW STRIDE IS `line`.
     *
     * B-088 is right that settilesize is the clamp rectangle and wrong about
     * what to put in its place. It resized a repeating axis to 2^mask and
     * read that as the image extent; 2^mask is where the RDP WRAPS the
     * coordinate, and the two are equal only when the image is a power of two
     * wide. The width a decoder needs is the TMEM ROW STRIDE, and the tile
     * carries it: `line`, in 64-bit words per row.
     *
     * AUTHORITY, and it is the game's own display-list generator. `images
     * text and font/7F076D68 - generate DL for ImgDecl.txt` computes the two
     * fields from different quantities, three hundred lines apart: :864-:884
     * derives `line` per bit depth as the width rounded UP to a tmem word
     * ("width to nearest doubleword ... line"), while :948/:958 derive the
     * masks as "nearest root to width / height", i.e. ceil(log2(dim)). The
     * decomp holds the same generator - othermodemicrocode.c:494 emits
     * `line = (width + 7) >> 3` for an 8-bit image and :500-:501 emits the
     * masks through is_less_than_certain_power_of_2(), which is ceil(log2)
     * verbatim. A 56-wide 8-bit image therefore ships line=7 (56 texels) and
     * masks=6 (wrap at 64), and the two disagree BY CONSTRUCTION.
     *
     * MEASURED, THE EXPLOSION (Gitea #5). Every one of the sixteen explosion
     * lists in assets/oddtextures.c sets its smoke tile
     *
     *   gsDPSetTile(G_IM_FMT_IA, G_IM_SIZ_8b, 7, 0, G_TX_RENDERTILE, 0,
     *               G_TX_WRAP, MASK_64, 0, G_TX_WRAP, MASK_64, 0),
     *   gsDPSetTileSize(0, 0, 0, CALC_TILESIZE(56), CALC_TILESIZE(56)),
     *
     * over a 56x56 IA8 loadblock. The F2 span is 56 and matched the load;
     * B-088's rule saw cms=WRAP with masks=6 and resized it to 64, so the
     * decoder walked the source at 64 texels a row where the rows are 56
     * texels long. Every row lands 8 texels late, which is a SHEAR of the
     * whole image - the explosion's diagonal hatch of bright bars, present on
     * every explosion in both level-33 captures. Dumped and looked at, not
     * argued: the same bytes decoded at stride 64 are the diagonal comb, and
     * at stride 56 they are a round smoke puff.
     *
     * B-088's own witness is UNCHANGED, and by the same rule rather than by
     * exemption: the Rareware logo's tile is a 32x32 16-bit load, line=8, so
     * line*4 = 32 texels - the identical width its 2^masks=32 produced. That
     * is not a coincidence, it is what "the two agree on a power of two"
     * means, and it is why B-088 measured a fix while carrying a wrong reason.
     *
     * A tile with no `line` - every LOAD tile - keeps the mask rule; there is
     * no stride to read. The T axis is untouched: the tile carries no row
     * COUNT, and sizing a repeating axis from its mask is B-088's rule
     * unaltered. Over-reading the source past the image shrinks rather than
     * grows, since the width is now the smaller of the two readings.
     *
     * SL_TILE_LINE=0 restores B-088's width from the same binary. */
    if (tile_mask_on()) {
        unsigned lw = tile_line_on() ? tile_line_texels(t) : 0u;
        if (!(t->cms & SL_TX_CLAMP) && t->masks != 0u) {
            unsigned pw = 1u << t->masks;
            unsigned mw;
            if (tile_period_on()) {
                /* B-108. The GL repeat length is the RDP's WRAP PERIOD,
                 * 2^masks; the tile's own `line` addresses the rows and
                 * goes to the decoder as the stride instead of becoming
                 * the width. Equal on every ordinary texture, in which
                 * case stridetex stays 0 and nothing changes. */
                mw = pw;
                if (lw != 0u && lw != pw) { g_tline_w++; stridetex = lw; }
            } else {
                /* B-102's reading: width from the stride. */
                mw = lw != 0u ? lw : pw;
                if (lw != 0u && lw != pw) g_tline_w++;
            }
            if (mw != w) { g_tmask_w++; w = mw; }
        }
        if (!(t->cmt & SL_TX_CLAMP) && t->maskt != 0u) {
            unsigned mh = 1u << t->maskt;
            if (mh != h) { g_tmask_h++; h = mh; }
        }
    }
    /* B-107. An axis that reached here at 0 texels had an inside-out window
     * and no wrap extent to replace it (SL_TILE_MASK=0, or a masked axis the
     * toggles put back on the window). That is the old whole-rectangle
     * reject, kept rather than handing the decoder a zero dimension. */
    if (w == 0u || h == 0u)               { *why = TR_TILESIZE; return 0; }
    slfmt = gbi_to_sl_fmt(t->fmt, t->siz, g_tlut_mode);
    if (slfmt < 0)                        { *why = TR_FMT;      return 0; }
    if (dxt != 0) dxtn_drawn(src, w, h);
    strd_note(src, slfmt, w, h, t->line);
    name = tex_acquire(src, g_tlut_addr, g_tlut_n, slfmt, w, h,
                       t->cms, t->cmt, t->masks, t->maskt, rowswap_flag(dxt),
                       stridetex);
    if (name == 0)                        { *why = TR_ACQUIRE;  return 0; }
    *ow = w; *oh = h;
    g_tg_w = w; g_tg_h = h; g_tg_fmt = (unsigned) slfmt;  /* for tg_note */
    g_tp_word_now = texprobe_on() && texprobe_word_match();
    texprobe_note(t, tile & 7u, src, dxt, ld, slfmt, w, h,
                  rowswap_flag(dxt), name, stridetex);
    return name;
}

/* The RDP cycle type (B9 setothermode_h, 1 == G_CYC_2CYCLE) and the TEXEL1
 * product classifier, both set where their command is decoded and both read
 * here because tex1_apply is above their natural homes. See the citations at
 * the B9 and FC cases. */
static unsigned g_cycle_type;
static unsigned g_textlod;          /* B-144: BA G_MDSFT_TEXTLOD, 1 = G_TL_LOD */
static unsigned g_lodlerp_tile_tris; /* B-144 census: c=13 draws with LOD off */
static int      g_lodoff_draw;      /* B-144: this c=13 draw runs under G_TL_TILE */
static int      g_cc_tex1mul;
static int      g_cc_tex1lerp;      /* (TEXEL1-TEXEL0)*PRIM_LOD_FRAC+TEXEL0 */
static int      g_cc_tex1lodlerp;   /* B-118: same shape, LOD_FRACTION (c=13) */
static int      g_tex1_lodlive;     /* B-118: this draw blends by computed LOD */
static int      g_lodmip_draw;      /* B-119: family's single-image variant -
                                     * GL trilinear does the blend, unit 1 and
                                     * the vertex-alpha fraction stay out */
static unsigned char g_lodfrac_v[3] = {255,255,255}; /* B-119: per-vertex f */

/* ======================= TEXEL1: the second texture unit =================
 *
 * B-048. Until this existed the renderer bound ONE texture and ignored the
 * second, so every combiner that multiplies two texels produced only the
 * first one. The measured casualty is the explosion: all fifteen lists in
 * assets/oddtextures.c set `gsDPSetCombineMode(G_CC_INTERFERENCE,
 * G_CC_MODULATEIA2)` in 2-cycle and load TWO images - an IA smoke frame into
 * tile 0 and an RGBA16 FIRE frame into tile 1 - and G_CC_INTERFERENCE is
 * `TEXEL0, 0, TEXEL1, 0` (include/PR/gbi.h:557), i.e. the product. The fire
 * frame is where an explosion's colour lives; dropping it leaves the IA
 * smoke's intensity alone, which is a white smokeball by construction. The
 * quads are built from `g_ExplosionRenderPartDefaultVertex` (explosion.c:215
 * = ffffffff), so there is no vertex colour to fall back on either.
 *
 * Deliberately narrow, the same discipline as cc_tint and g_cc_a_envscale:
 * ONE mux shape is claimed, the exact product `(TEXEL0 - 0) * TEXEL1 + 0` in
 * the cycle that resolves the texel. A full RDP combiner is not attempted.
 *
 * The shape set is MEASURED, not assumed. Every distinct combiner reaching a
 * draw across the twenty-level boot sweep plus two explosion replays was
 * decoded (SL_CC_DBG=1 histogram, aggregated per level). Eleven of them name
 * TEXEL1 and exactly ONE is this shape:
 *
 *   fc111404 ff13ffff  (TEX0 - 0) * TEX1 + 0      <- the explosion, handled
 *   fc26a004 1f1093ff  (TEX1 - TEX0) * LODFRAC + TEX0   } the 2-cycle mip
 *   fc26a004 1f1493ff  ... and six more of the same shape } chain: TEXEL1 is
 *   fc272c04 1f1093ff  (TEX1 - TEX0) * PRIMLOD + TEX0    } the NEXT LOD level
 *
 * The LOD-lerp family is left exactly as it draws today - drawing the base
 * tile alone is LOD fraction 0, which is what a renderer with no mip chain
 * should do, and blending in the next level would be a mipmapping change
 * rather than a colour one. That is a separate question and not this task.
 *
 * WHICH tile is TEXEL1 is not decoded from the combiner - the mux names the
 * SLOT, not a tile. On the RDP the second cycle samples the render tile + 1,
 * which is why the explosion lists put smoke in G_TX_RENDERTILE (0,
 * gbi.h:388) and fire in tile 1 without any command naming "1" as a texel
 * source. The notes are silent on this - `grep -rn -i texel1` over the whole
 * corpus returns only the mux tables' own "2 texel1" rows - so a not_covered
 * entry records it.
 *
 * ---- why tile 1 needs its own dimensions -------------------------------
 *
 * tile_texture sizes a tile from its F2 settilesize, which is right for every
 * tile the renderer had met so far and WRONG for this one. The fire tile is
 *
 *   F5 tile=1 fmt=RGBA siz=16b line=4 tmem=392 cms/cmt=CLAMP shifts/shiftt=2
 *   F2 tile=1 s[0,220] t[0,220]        (10.2, so 56 x 56 texels)
 *   F3 texels=224                      (CALC_LRS(16,14,G_IM_SIZ_16b) + 1)
 *
 * 224 texels is 16 x 14, not 56 x 56. The two agree once the SHIFT is read:
 * the RDP shifts the incoming s/t right by `shift` before the tile lookup, so
 * the tile's 56-texel extent is 56 >> 2 = 14 texels of actual image, and the
 * quad's own s/t of 0..1760 (10.5, explosion.c:1017-1037) become 0..13.75.
 * The row stride is `line` - 4 64-bit words is 32 bytes is 16 texels at
 * 16bpp - so the image is 14 columns padded to 16, which is exactly what a
 * 14-texel row costs to align to tmem's 8-byte words.
 *
 * So the second unit sizes its texture from the LOAD (line for the width,
 * the F3 texel count for the height) rather than from settilesize, and
 * scales its texture coordinates by the shift. Unit 0 is untouched: its
 * resolution path is bit for bit what it was. */
static unsigned g_tex1_w, g_tex1_h;          /* the decoded image */
static unsigned g_tex1_shifts, g_tex1_shiftt;
static float g_tex1_sdiv = 1.0f, g_tex1_tdiv = 1.0f;   /* B-117 */
static int      g_tex1_gl_on;                /* GL_TEXTURE1 currently enabled */
static GLuint   g_tex1_gl_bound;
static unsigned g_tex1_tris, g_tex1_binds, g_tex1_fail;
static unsigned g_tex1_tile;                 /* which tile it resolved */
static unsigned g_tex1_shiftbig;             /* left-shift forms, telemetry */

/* SL_CC_TEX1=0 restores the pre-B-048 behaviour - one texture unit, TEXEL1
 * dropped - from the same binary, so the two can be captured back to back.
 * Default on: the old behaviour is the defect. */
static int cc_tex1_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CC_TEX1");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* B-107. SL_CC_TEX1LERP=0 restores the base-tile-only rendering of the
 * PRIM_LOD_FRAC lerp family (the water) from the same binary. Default on:
 * dropping the second layer is the defect the owner reported against the
 * cartridge. */
static int cc_tex1lerp_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CC_TEX1LERP");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* B-118. SL_CC_LODLERP=0 restores base-tile-only rendering of the
 * LOD_FRACTION detail-blend family from the same binary. Default on:
 * dropping the far image is why Surface's tree walls rendered as their
 * near-detail noise at every distance. */
static int cc_lodlerp_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CC_LODLERP");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* B-051. The tinted-glass pane's opacity, which lives in the CYCLE 1 alpha
 * addend. DEFAULT OFF, deliberately and unlike SL_CC_TEX1 above: the owner
 * builds this tree unannounced, and an unverified renderer change reaching
 * them by default is exactly how the fog regression happened. Flip it in its
 * own commit once the evidence in docs/backlog.md is accepted. */
static int cc_glass_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CC_GLASS");
                  on = (v != NULL && *v != '0'); }
    return on;
}

/* GL_TEXTURE1 and glActiveTexture are core GL 1.3; the window this backend
 * opens reports a 4.5 compatibility profile. Where a header predates it the
 * whole feature compiles out and explosions keep drawing white, which is the
 * behaviour before this change rather than a new failure. */
#if defined(SL_GL_RUNTIME_POST11) \
    || (defined(GL_TEXTURE0) && defined(GL_TEXTURE1) && defined(GL_VERSION_1_3))
#define SL_MULTITEX 1
#endif

/* Texels one tmem row holds, from the tile's `line` (64-bit words per row).
 * 0 when the tile carries no line, which is every LOAD tile. */
static unsigned tile_line_texels(const struct sl_tile *t)
{
    if (t->line == 0) return 0;
    switch (t->siz) {                    /* gbi.h G_IM_SIZ_4b/8b/16b/32b */
    case 0: return t->line * 16u;
    case 1: return t->line * 8u;
    case 2: return t->line * 4u;
    /* 32b is NOT 8 bytes per word over 4 bytes a texel. gbi.h:431 gives
     * G_IM_SIZ_32b_LINE_BYTES = 2 while :430 gives _BYTES = 4, because the
     * RDP splits a 32-bit texture across the two halves of tmem and `line`
     * counts one half - so a row is line*8 bytes of RG and as much again of
     * BA, i.e. line*4 TEXELS. MEASURED (B-102): a level-33 tile reports
     * `F5 fmt=0 siz=3 line=4` under `F2 s[2,62]`, a 16-texel span; line*4 is
     * 16 and line*2 was 8. This case was unreachable until the render tile
     * started reading its own stride - tile1_texture only ever resolves the
     * explosion's 16-bit fire - so it is a latent error being corrected, not
     * a regression being introduced. */
    default: return t->line * 4u;
    }
}

/* Resolve the TEXEL1 tile to a GL texture. Same tmem machinery as
 * tile_texture - the tile's own tmem address picks the loadblock that filled
 * it (B-023) - but sized from that load rather than from settilesize, for the
 * reason in the block above. */
static GLuint tile1_texture(unsigned tile, unsigned *ow, unsigned *oh)
{
    const struct sl_tile *t = &g_tile[tile & 7];
    const struct sl_tmem_ent *ld;
    unsigned w, h, lw;
    unsigned stridetex = 0;    /* B-108: 0 = rows advance by the width */
    int slfmt;

    /* B-128: the load covering the address, and only at its own start - an
     * address inside a load is a level of THAT load's image (tex_apply's
     * one-image test keeps such draws off this path), not a second image
     * sized by the load's texel count. */
    {   unsigned off = 0;
        ld = tmem_resolve(t->tmem, &off);
        if (ld == NULL || ld->src == NULL || off != 0) return 0;
    }
    lw = tile_line_texels(t);
    /* B-144. THE LOAD'S TEXEL COUNT IS IN THE LOAD'S TEXEL SIZE, THE ROWS ARE
     * IN THE TILE'S. GoldenEye loads every 4- and 8-bit image through a
     * 16-bit loadblock (tex.c's FD is siz=2 for CI8 - ucode05.txt F3 counts
     * texels in the FD's size), so `ld->texels` for a CI8 image is HALF its
     * texel count and the height derived from it was half the image. A
     * TEXEL1 tile that names the same image as the render tile - the c=13
     * family's "two tiles at one address" case, which the B-128 block at
     * tex_apply leaves on the two-unit path because "the lerp of an image
     * with itself is the image" - was therefore lerping the image with its
     * own TOP HALF stretched over the full height.
     *
     * MEASURED, the Dam security-gate panel (owner mark 20260917-071141,
     * Gitea #34): its bevel frame is 16 triangles under fc26a004 1f1093fb
     * over one 23x24 CI8-IA16 tile, tiles 0 and 1 both at tmem 0, loaded as
     * 288 16-bit texels. tile 1 resolved as 24x12 (the dumped pair p-000
     * 23x24 / p-001 24x12), and the frame drew as flat, per-triangle tones
     * (top bevel 73, inner slope 43) where the cartridge draws the image's
     * light and dark rows across every bevel (top 117, banded slope);
     * SL_CC_LODLERP=0 - render tile alone - matched the cartridge, which is
     * what named this path. Converting through bytes makes tile 1 the whole
     * 24x24 image and the two-unit lerp the identity it was assumed to be.
     * A load and tile of equal size (the explosion's 16-bit fire, B-048)
     * are unchanged; a wrapping masked axis still takes 2^mask below. */
    if (lw == 0) return 0;
    {
        unsigned bytes = (ld->texels << ld->siz) >> 1;     /* siz: 0=4b 1=8b 2=16b 3=32b */
        unsigned ttex  = (bytes << 1) >> t->siz;           /* in the TILE's texel size */
        if (ttex < lw) return 0;
        w = lw;
        h = ttex / lw;
    }
    /* B-144, same rule the render tile applies (tile_texture): on a CLAMPED
     * axis the F2 window IS the extent, so it bounds the load-derived size
     * from above - a load that carries the image plus its authored pyramid
     * would otherwise hand unit 1 the pyramid rows as image height. Only
     * ever shrinks; a window larger than the load's stride (the explosion's
     * shifted 56-texel F2 over a 16-texel stride, B-048) is left alone. A
     * width below the stride keeps the stride as the decoder's row step. */
    if ((t->cms & SL_TX_CLAMP) && t->lrs >= t->uls) {
        unsigned fw = ((t->lrs - t->uls) >> 2) + 1u;
        if (fw != 0u && fw < w) { stridetex = lw; w = fw; }
    }
    if ((t->cmt & SL_TX_CLAMP) && t->lrt >= t->ult) {
        unsigned fh = ((t->lrt - t->ult) >> 2) + 1u;
        if (fh != 0u && fh < h) h = fh;
    }
    /* B-108, same rule as the render tile: on a wrapping masked axis the
     * repeat length is the RDP's 2^mask wrap period and the tile's own
     * `line` addresses the rows. Dam's water tile 1 is the witness - the
     * same CI8 line=2 masks=maskt=5 state as tile 0, offset (90,150). A
     * clamped axis (the explosion's fire tile) keeps the load-derived
     * sizing exactly as before. */
    if (tile_mask_on() && tile_period_on()) {
        if (!(t->cms & SL_TX_CLAMP) && t->masks != 0u) {
            unsigned pw = 1u << t->masks;
            if (lw != pw) stridetex = lw;
            w = pw;
        }
        if (!(t->cmt & SL_TX_CLAMP) && t->maskt != 0u)
            h = 1u << t->maskt;
    }
    slfmt = gbi_to_sl_fmt(t->fmt, t->siz, g_tlut_mode);
    if (slfmt < 0) return 0;
    *ow = w; *oh = h;
    return tex_acquire(ld->src, g_tlut_addr, g_tlut_n, slfmt, w, h,
                       t->cms, t->cmt, t->masks, t->maskt,
                       rowswap_flag(ld->dxt), stridetex);
}

#ifdef SL_MULTITEX
/* Leave unit 1 off and the active unit back at 0. Every caller closes the
 * batch first - glEnable, glBindTexture and glActiveTexture are all illegal
 * between glBegin and glEnd, the same contract tex_apply already keeps. */
static void tex1_off(void)
{
    g_tex1_lodlive = 0;                                  /* B-118 */
    if (!g_tex1_gl_on) return;
    batch_end();
    glActiveTexture(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    g_tex1_gl_on = 0;
    g_tex1_gl_bound = 0;
}
#else
static void tex1_off(void) { g_tex1_gl_on = 0; g_tex1_lodlive = 0; }
#endif

/* Bind the TEXEL1 tile on unit 1 under GL_MODULATE, so the pipeline computes
 * primary * TEXEL0 * TEXEL1 in both colour and alpha. That is the whole
 * combiner for this shape: cycle 0 is TEXEL0 * TEXEL1 (rgb and alpha alike,
 * G_CC_INTERFERENCE) and cycle 1 scales it by SHADE (G_CC_MODULATEIA2),
 * which unit 0's own modulation against the vertex colour already applies. */
static int g_tex1_env = -1;    /* unit 1 env: 0 = MODULATE, 1 = B-107 lerp */

/* B-144. SL_TEXTLOD_GATE=0 lets the c=13 lerp run under G_TL_TILE as it did
 * before, for bisecting. Default on. */
static int textlod_gate_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEXTLOD_GATE");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}

static void tex1_apply(void)
{
#ifdef SL_MULTITEX
    GLuint name;
    unsigned w = 0, h = 0;
    const struct sl_tile *t;
    unsigned tile;
    int lerp;

    if (!post11_ok()) { tex1_off(); return; }
    lerp = g_cc_tex1lerp && cc_tex1lerp_on();
    /* B-118. The LOD_FRACTION detail-blend rides the same two-unit path as
     * the water's PRIM_LOD_FRAC lerp - unit 0 lerps the two tiles by the
     * texenv constant, unit 1 multiplies by SHADE (both families' cycle 1 is
     * (COMBINED-0)*SHADE+0). Only the SOURCE of the constant differs: the
     * water's is the FA prim-lod byte the game animates; this one is the
     * RDP's own texel-per-pixel LOD, approximated per triangle at emit. */
    /* B-144: g_lodmip_draw is also set for a c=13 draw under G_TL_TILE, so
     * the live lerp never arms there - see the block in tex_apply. */
    g_tex1_lodlive = g_cc_tex1lodlerp && cc_lodlerp_on() && !g_lodmip_draw;
    lerp = lerp || g_tex1_lodlive;
    if (!cc_tex1_on() || !(g_cc_tex1mul || lerp) || g_cycle_type != 1) {
        g_tex1_lodlive = 0;
        tex1_off(); return;
    }

    tile = (g_tex_tile + 1u) & 7u;
    g_want_lodmips = g_tex1_lodlive;   /* B-119: far image gets a pyramid */
    name = tile1_texture(tile, &w, &h);
    g_want_lodmips = 0;
    if (name == 0) {
        g_tex1_fail++; tex1_off();
        /* B-119. NEVER leave a crossbar mode armed over a dead unit: mode 9
         * names GL_TEXTURE1, and with unit 1 disabled that sample is
         * undefined - measured as opaque white across whole rooms. Plain
         * modulate is the single-unit truth for the draw. */
        if (g_texenv == 9) { batch_end(); texenv_set(0); }
        return;
    }

    t = &g_tile[tile];
    if (t->shifts > 10u || t->shiftt > 10u) g_tex1_shiftbig++;
    g_tex1_tile = tile;
    g_tex1_w = w; g_tex1_h = h;
    /* B-117. Both shift FORMS now, matching unit 0: right (1..10) divides,
     * left (11..15) multiplies. The old `> 10 ? 0` dropped the left form. */
    g_tex1_sdiv = shift_scale(t->shifts);
    g_tex1_tdiv = shift_scale(t->shiftt);
    g_tex1_shifts = t->shifts; g_tex1_shiftt = t->shiftt;
    /* B-107. Tile 1's own F2 origin, in texels - the water's second layer
     * scrolls at its own rate (the writer keeps it 90/150 ahead of tile 0's,
     * mod 256). Zero when the toggle is off or the origin is zero.
     * B-109: tile 1 carries the same registered fraction as tile 0 (the
     * writer offsets one continuous phase by an integer), so adding each
     * tile's own fraction preserves the (90,150) relative phase exactly. */
    {
        float fs = f2frac_on() ? t->fs : 0.0f;
        float ft = f2frac_on() ? t->ft : 0.0f;
        g_tex1_uls = tile_ul_on() ? ((float) t->uls + fs) * 0.25f : 0.0f;
        g_tex1_ult = tile_ul_on() ? ((float) t->ult + ft) * 0.25f : 0.0f;
    }
    g_tex1_tris++;

    if (!g_tex1_gl_on || g_tex1_env != lerp) {
        batch_end();
        glActiveTexture(GL_TEXTURE1);
        if (!g_tex1_gl_on) glEnable(GL_TEXTURE_2D);
        if (lerp) {
#if defined(SL_TEXENV_COMBINE) && defined(GL_INTERPOLATE) && defined(GL_CONSTANT)
            /* B-110. This arm was DEAD from the day B-107 landed: the guard
             * above read an SL_TEXENV_COMBINE that was then defined five
             * thousand lines further down, so the #else shipped instead and
             * the water multiplied by TEXEL1 twice with SHADE discarded -
             * the owner's "lighter / less blue-green". Proven from consumed
             * driver state (unit 1 read back ENV_MODE=GL_MODULATE with every
             * combine source at its GL default) and from GL_PREVIOUS below
             * having had no definition to compile against. The definition
             * now sits at the top of the file.
             * B-107. Unit 0 computed the raw-texel lerp (texenv mode 8);
             * this unit multiplies it by SHADE, which is the water
             * combiner's whole second cycle, (COMBINED - 0) * SHADE + 0.
             * ALPHA passes unit 0's result through unchanged, so the draw's
             * alpha is bit-identical to the base-tile rendering it
             * replaces. */
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
            glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
            glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
#else
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
#endif
        } else {
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
        glActiveTexture(GL_TEXTURE0);
        if (!g_tex1_gl_on) g_tex1_gl_bound = 0;
        g_tex1_gl_on = 1;
        g_tex1_env = lerp;
    }
    if (name != g_tex1_gl_bound) {
        batch_end();
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, name);
        glActiveTexture(GL_TEXTURE0);
        /* tex_acquire uploads through unit 0's binding point, so a decode
         * that happened just now left unit 0 bound to the tile-1 texture.
         * Forget the cached name rather than trusting it. */
        g_tex_gl_bound = 0;
        g_tex1_gl_bound = name;
        g_tex1_binds++;
    }
#else
    tex1_off();
#endif
}

/* B-035. Does the combiner in force name a TEXEL anywhere at all - either
 * cycle, colour or alpha?
 *
 * `BB`'s enable byte is the RSP's texture-coordinate switch, not the RDP's
 * sampler. A list that declines to re-issue BB inherits whatever the previous
 * draw left set, and consequently every 3D triangle in a Facility frame draws
 * textured (`sl_tex: tris textured=986 plain=0`). That is harmless while the
 * combiner asks for a texel and wrong when it does not: the in-game health
 * ring is drawn under G_CC_SHADE (bondview2.c:8617) and carries no BB of its
 * own, so on hardware its colour is exactly the shade colour, while here it
 * came out modulated by the tile the previous draw had bound - measured as
 * the correct hue scaled to 8/255, which reads as black.
 *
 * Set where FC is decoded; field layout from ucode05_old.txt's
 * "FC rdp_setcombine" table. Starts at 1 so a list that draws before issuing
 * its first FC keeps the old behaviour. */
static int      g_cc_texel = 1;
static unsigned g_cc_notex_tris;
/* The FC words in force, kept here rather than with the rest of the combiner
 * state because tex_apply is above it and needs them for the note below. */
static unsigned g_cc_w0, g_cc_w1;
/* Which combiners actually reach the gate, so a pixel change it causes can be
 * attributed to a named mode rather than argued about. Distinct w0/w1 pairs,
 * per frame. */
static unsigned g_cc_notex_w[4][3];
static unsigned g_cc_notex_n;

static void cc_notex_note(void)
{
    unsigned i;
    for (i = 0; i < g_cc_notex_n; i++)
        if (g_cc_notex_w[i][0] == g_cc_w0 && g_cc_notex_w[i][1] == g_cc_w1) {
            g_cc_notex_w[i][2]++; return;
        }
    if (g_cc_notex_n < 4) {
        g_cc_notex_w[g_cc_notex_n][0] = g_cc_w0;
        g_cc_notex_w[g_cc_notex_n][1] = g_cc_w1;
        g_cc_notex_w[g_cc_notex_n][2] = 1;
        g_cc_notex_n++;
    }
}

/* SL_CC_TEXGATE=0 restores the pre-B-035 behaviour (BB alone gates texturing),
 * so the two can be captured back to back. Default on: leaving it off is not a
 * neutral choice, it is the defect. */
static int cc_texgate(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_CC_TEXGATE");
                  on = (s == NULL || *s != '0'); }
    return on;
}

/* B-107. The FA prim LOD fraction, needed here by mode 8's constant push
 * in tex_apply; the definition sits with the colour registers below and
 * these tentative duplicates merge with it. */
static unsigned g_prim_lod_min, g_prim_lod_level;

/* B-142. AN UNTEXTURED DRAW MUST NOT RUN UNDER THE ALPHA PROGRAM. tex_apply
 * returns early for a draw that binds no texture (the combiner names no
 * texel, or the tile did not resolve) after disabling unit 0 - and left the
 * texture ENVIRONMENT as the previous draw set it. For every fixed-function
 * mode that is harmless: with GL_TEXTURE_2D off the environment is not
 * consulted. Mode 5 is a program (B-051, AEQ_FS), it stays bound across
 * batches by design (shade_linear_bind defers to it), and its fragment stage
 * samples u_tex unconditionally - so the untextured draw was painted with
 * whatever texture unit 0 still held. Measured at the Depot yard (owner mark
 * 20260916-130256-lvl30/mark-001): the floor's draw failed to acquire its
 * tile, the previous draw was the hazard-stripe pane in mode 5, and 88
 * shade-only triangles a frame came out as red x shade (Gitea #27, cause B).
 * The rule is the environment's, not the level's: leaving the program is
 * what texenv_set(0) already does for every mode change, so an untextured
 * draw simply asks for the plain environment. The next textured draw
 * re-establishes its own mode through tex_apply's `g_texenv != want`. */
static void texenv_set(int mode);
static void texenv_plain(void)
{
    if (g_texenv == 5) { g_plain_prog_reset++; batch_end(); texenv_set(0); }
}

static void tex_apply(void)
{
    GLuint name = 0;
    int why = TR_OK;
    int lodfam = 0;   /* B-119: this draw's combiner is the c=13 LOD family */
    int lodmip = 0;   /* B-119: the family's SINGLE-IMAGE variant (below)   */

    /* B-119. THE c=13 FAMILY HAS TWO AUTHORED SHAPES, split by where the
     * TEXEL1 tile's tmem points (measured at Surface's dome, mark 4 of run
     * 20260912-055925, census: 2290 unresolved TEXEL1 tris/frame vs 20
     * binds):
     *
     *   TWO-IMAGE DETAIL BLEND (the treeline): two loadblocks - a detail
     *       texture at tmem 0 and the far image at tmem 128 - and tiles 0/1
     *       each name a LOADED address, so both resolve through the tmem
     *       map. Mode 9 below realizes the blend across two units.
     *   ONE IMAGE PLUS ITS AUTHORED MIPS (the dome's snow shell, ground,
     *       the bullet tracer's flare): ONE loadblock carries base +
     *       pyramid, tile 0 names the load and tiles 1..n point INTO the
     *       block. Leaving mode 9 armed over an unresolved unit sampled a
     *       disabled unit: the whole building rendered opaque white.
     *       TEXEL0/TEXEL1 there are ADJACENT LEVELS OF THE SAME IMAGE, and
     *       GL's own trilinear minification IS that lerp, per pixel - so
     *       this variant draws single-unit (plain modulate) with a mip
     *       pyramid on the one image instead.
     *
     * B-128. The discriminator was "tile 1's tmem has no entry", which only
     * held while nothing stale sat at that word: the map answered by exact
     * load START, so the tracer's tile 1 (word 128, inside its own block)
     * resolved to the treeline strip an earlier draw of the same frame had
     * loaded at 128, and the flare blended with RGBA16 strip bytes read as
     * RGBA32 - the rainbow of issue #16. The map now answers by containment
     * (see tmem_resolve), so the test is the one the layouts actually
     * differ by: tile 1 at an address INSIDE tile 0's own load is a level
     * of that image - one image, mips; anywhere else it is a second image.
     * Two tiles at one address stay on the two-unit path they always took
     * (TEXEL1 there is TEXEL0's own image, and the lerp of an image with
     * itself is the image), so nothing outside the interior case moves.
     * Measured on the tracer witness: TEXEL1 unresolved 99..674 per frame
     * -> 0, and one-image-mips 0..50 -> ~2500 (the dome shell, the snow
     * ground and the flare, all previously stale-start hits that failed
     * the two-image decode and fell to plain modulate without mips). */
    if (g_cc_tex1lodlerp && cc_lodlerp_on() && cc_tex1_on()
        && g_cycle_type == 1) {
        const struct sl_tile *t0 = &g_tile[g_tex_tile & 7u];
        const struct sl_tile *t1 = &g_tile[(g_tex_tile + 1u) & 7u];
        const struct sl_tmem_ent *ld0 = tmem_find(t0->tmem);
        const struct sl_tmem_ent *ld1 = tmem_find(t1->tmem);
        int inner = tmem_span_on() && ld1 != NULL && ld1 == ld0
                    && t1->tmem != t0->tmem;
        lodfam = 1;
        lodmip = (ld1 == NULL || ld1->src == NULL || inner);
        if (inner) g_lodmip_same++;
    }
    /* B-144. THE LERP EXISTS ONLY WHILE THE RDP COMPUTES A LOD. The
     * LOD_FRACTION this family multiplies by is the per-pixel LOD the RDP
     * derives under G_TL_LOD; under G_TL_TILE (BA sft=16 data 0) no LOD is
     * computed, the fraction is 0, and (TEXEL1 - TEXEL0) * 0 + TEXEL0 is the
     * render tile alone, sampled without a pyramid. Every room list turns
     * LOD on before its images ("BA001001 00010000 ;images use lod"), which
     * is where B-118/B-119 measured the two-unit path; prop lists turn it
     * OFF again before their CI images - the Dam gate panel's list runs
     * `ba001001 00000000` then `bb000001 ffffffff` over a 23x24 CI8-IA16
     * tile with tile 1 == tile 0 - and the native lerp, driven by its own
     * per-triangle LOD estimate against a mip-pyramided unit 1, pulled those
     * faces toward a minified average: the panel's bevel frame drew as flat
     * per-triangle tones (owner mark 20260917-071141, top bevel 73, inner
     * slope 43) where the cartridge shows the texture's rows across every
     * bevel (117, banded); SL_CC_LODLERP=0 matched the cartridge and named
     * this path. Such a draw takes the single-unit branch below - plain
     * modulate, the alpha equation kept exactly as B-138 keeps it for the
     * mip variant - but WITHOUT the mip pyramid, since the hardware samples
     * tile 0 alone. Counted on the sl_cc census line; SL_TEXTLOD_GATE=0
     * reverts. */
    {
        int lodoff = lodfam && !lodmip && !g_textlod && textlod_gate_on();
        if (lodoff) { g_lodlerp_tile_tris++; lodmip = 1; g_lodoff_draw = 1; }
        else g_lodoff_draw = 0;
    }
    g_lodmip_draw = lodmip;

    if (g_tex_on && g_load_src == NULL) g_tex_nosrc++;
    /* B-035. The combiner names no texel, so the RDP would not sample one
     * whatever BB last said. Counted whether or not the gate is armed, so the
     * telemetry line reports the size of the population either way. */
    if (g_tex_on && !g_cc_texel) {
        g_cc_notex_tris++;
        cc_notex_note();
        if (cc_texgate()) {
            g_tri_plain++;
            tex1_off();
            if (g_tex_gl_on) { batch_end(); glDisable(GL_TEXTURE_2D); g_tex_gl_on = 0; }
            texenv_plain();
            return;
        }
    }
    if (g_tex_on) {
        unsigned w = 0, h = 0;
        unsigned rt = g_tex_tile;
        {
            if (texgen_lod() && (g_geom_mode & G_GEOM_TEXTURE_GEN))
                rt = (rt + (unsigned) texgen_lod()) & 7u;
            g_tex_world = 1;                /* B-116: world geometry */
            /* B-119: single-image variant; B-144: not under G_TL_TILE */
            g_want_lodmips = lodmip && !g_lodoff_draw;
            name = tile_texture(rt, &w, &h, &why);
            g_want_lodmips = 0;
            g_tex_world = 0;
        }
        /* B-107. Carry the resolved tile's origin to the coordinate path.
         * B-109 adds the registered fractional phase the F2 integer field
         * cannot carry - zero for every unregistered tile. */
        if (name != 0 && tile_ul_on()) {
            const struct sl_tile *rtt = &g_tile[rt & 7u];
            float fs = f2frac_on() ? rtt->fs : 0.0f;
            float ft = f2frac_on() ? rtt->ft : 0.0f;
            g_st_uls = ((float) rtt->uls + fs) * 0.25f;
            g_st_ult = ((float) rtt->ult + ft) * 0.25f;
        } else {
            g_st_uls = g_st_ult = 0.0f;
        }
        /* B-117. THE RENDER TILE'S COORDINATE SHIFT, previously consumed by
         * unit 1 only - the texprobe's own comment called a non-zero shift on
         * tile 0 "a live defect rather than a curiosity", and Surface's
         * perimeter treeline is where it came due: those draws carry
         * shifts=14 on the render tile, which per ucode05_old.txt
         * "F5 rdp_settile" shifts the S coordinate LEFT by (16 - shift) -
         * s x4 - for shift values 11..15, and RIGHT (s >> shift) for 1..10
         * (gbi.h:3401 gsDPSetTile packs the fields; G_TX_NOLOD=0). Dropping
         * it sampled the tree texture at a quarter of its authored horizontal
         * frequency, which smeared the ROM's distinct snow-dusted conifers
         * into the mottled noise the owner rejected - and it is why comment
         * 113's "S/T interpretation correct" was a false link: the check
         * compared raw S/T against OUR consumption, never against the tile's
         * shift field. SL_TILE_SHIFT=0 restores the old behaviour. */
        if (name != 0 && tile_shift_on()) {
            const struct sl_tile *rtt = &g_tile[rt & 7u];
            g_st_sdiv = shift_scale(rtt->shifts);
            g_st_tdiv = shift_scale(rtt->shiftt);
        } else {
            g_st_sdiv = g_st_tdiv = 1.0f;
        }
        if (name) { g_st_tex_w = w; g_st_tex_h = h; }
        else if (why == TR_FMT)      g_tex_reject[5]++;
        else if (why == TR_TILESIZE) g_tex_reject[6]++;
    }

    if (g_tex_on) g_tex_on_tris++;
    if (name == 0) {
        g_tri_plain++;
        tex1_off();
        if (g_tex_gl_on) { batch_end(); glDisable(GL_TEXTURE_2D); g_tex_gl_on = 0; }
        texenv_plain();
        return;
    }
    g_tri_tex++;
    {   unsigned i;
        for (i = 0; i < g_tex_names_n; i++) if (g_tex_names[i] == name) break;
        if (i == g_tex_names_n && g_tex_names_n < 64) g_tex_names[g_tex_names_n++] = name;
    }
    /* SL_GLASS_MARK=2 additionally drops the TEXTURE for the tinted-glass
     * pane, which is what separates "the pane is not drawn" from "the pane is
     * drawn and its texel makes it invisible". Kept distinct from =1 because
     * SL_TRI_ID already disables texturing for every triangle, and reading a
     * TRI_ID capture as evidence that a quad covers a region is therefore an
     * artefact - it removes the very thing under test. That error was made in
     * this investigation and caught only because SL_GLASS_MARK=1 contradicted
     * it on the same frame. */
    if (fog_viz_option() || tri_id_option()
        || (texgen_mark() && (g_geom_mode & G_GEOM_TEXTURE_GEN))
        || (g_cc_a_prim1 && glassmark_on() == 2)) {   /* probes draw values, not the scene */
        if (g_tex_gl_on) { batch_end(); glDisable(GL_TEXTURE_2D); g_tex_gl_on = 0; }
        return;
    }
    if (!g_tex_gl_on) { batch_end(); glEnable(GL_TEXTURE_2D); g_tex_gl_on = 1; }
    if (name != g_tex_gl_bound) {
        batch_end();
        glBindTexture(GL_TEXTURE_2D, name);
        g_tex_gl_bound = name;
        g_tex_binds++;
    }
    /* A font texrect may have left the environment replacing colour with the
     * primitive; geometry wants plain modulation back.
     *
     * SL_GLASS_MARK=3 substitutes the vertex-alpha environment for the glass
     * combiner only - see texenv_set mode 2. */
    {
        int want = 0, aeq_want = AEQ_OFF;
        if (g_cc_a_prim1 && glassmark_on() == 3) {
            want = 2;                       /* the earlier one-combiner probe */
        } else if (alpha_eq_on()) {
            /* B-051 THE FIX. RGB is UNTOUCHED in every mode below - only the
             * alpha stage changes, which is the whole point: the RDP runs the
             * two equations independently and GL_MODULATE couples them. */
            aeq_want = aeq_mode_now();
            if (!aeq_mode_enabled(aeq_want)) aeq_want = AEQ_OFF;  /* attribution */
            if (aeq_want == AEQ_SHADER) {
                /* The shader samples unit 0 only, so it must not take a draw
                 * the second texture unit is about to contribute to. That
                 * pairing does not occur in this game - tex1 needs the
                 * TEXEL0*TEXEL1 product combiner and an addend combiner is
                 * never that - but the fallback is counted rather than
                 * assumed away. */
                int tex1_live = cc_tex1_on() && g_cc_tex1mul
                                && g_cycle_type == 1;
                if (!tex1_live && aeq_shader_avail()) want = 5;
                else { want = 3; aeq_want = AEQ_ADD;
                       g_aeq_shader_fallback_tris++; }
            }
            else if (aeq_want == AEQ_REPLACE) want = 2;
            else if (aeq_want == AEQ_LERP)    want = 4;
            else if (aeq_want == AEQ_ADD)     want = 3;
        }
        /* B-107. The PRIM_LOD_FRAC lerp claims unit 0's environment: mode 8
         * computes the raw-texel lerp and unit 1 multiplies by SHADE. It
         * takes precedence over the alpha-equation modes by shape: the water
         * family's cycle-1 alpha addend is the constant 0 (Ad1 = 7), so no
         * aeq mode is losing a draw it could have classified - the glass
         * (Ad1 = PRIM) is a different word and never reaches here. */
        if (g_cc_tex1lerp && cc_tex1lerp_on() && cc_tex1_on()
            && g_cycle_type == 1) {
            want = 8; aeq_want = AEQ_OFF;
        }
        /* B-118/B-119. The LOD_FRACTION family: the two-image detail blend
         * takes its own mode 9 - the factor rides the vertex alpha
         * (per-vertex, Gouraud) and the output alpha is the far image's own,
         * per the family's authored alpha equation. The single-image mip
         * variant stays in plain modulate: its blend IS the trilinear
         * minification its mip pyramid (acquired above) performs. */
        if (lodfam && !lodmip) {
            want = 9; aeq_want = AEQ_OFF;
        }
        /* B-138. The single-image mip variant keeps whatever the ALPHA
         * equation above decided. It used to force plain modulate here, and
         * that discarded the cycle-1 alpha addend of every word whose cycle 0
         * is the mip lerp - which is exactly the tinted-glass word (B-051,
         * fc26a004 1f1093fb: RGB = mip-lerp * SHADE, alpha = mip-lerp.a *
         * SHADE.a + PRIMITIVE.a with PRIM.a = Rare's calculatedopacity). Under
         * plain modulate the pane's alpha collapsed to its texture's constant
         * 102/255, so every 0x2F pane drew at 40% whatever its opacity said,
         * and the rooms behind it - which the cartridge never traverses once
         * the portal is disabled at opacity 255 - showed through it.
         * Measured 2026-09-15 on Control's owner marks (run 20260915-231706,
         * marks 1-3): the pane draws reached tex_apply with texenv=0 and the
         * alpha-equation uniforms never uploaded; the cartridge draws the
         * same panes as an unbroken dark sheet (mean 32-34 in 320x240).
         * The shader's RGB is the same modulate and texture2D samples through
         * the same mip pyramid the plain path acquired, so nothing about the
         * mip lerp moves; only the alpha addend comes back. A trivial alpha
         * word still classifies to OFF above and still draws mode 0. */
        if (g_texenv != want) { batch_end(); texenv_set(want); }
        aeq_apply_env(aeq_want);
        /* Mode 8's lerp factor is the ANIMATED prim LOD fraction - the water
         * writer rewrites it every frame (unk_092E50.c, the FA low byte) -
         * so it is pushed whenever the value in force differs from the one
         * the constant currently holds, as recorded by the ONE mirror. */
        if (want == 8) {
            GLfloat f = (GLfloat) g_prim_lod_level / 255.0f;
            envcol_set(f, f, f, f);
        }
    }
    /* B-048. LAST, because tile1_texture can decode and upload, which binds
     * through unit 0 and would otherwise invalidate the bind just made. It
     * clears g_tex_gl_bound when that happens; re-establish unit 0 after. */
    tex1_apply();
    if (g_tex_gl_bound != name) {
        batch_end();
        glBindTexture(GL_TEXTURE_2D, name);
        g_tex_gl_bound = name;
        g_tex_binds++;
    }
    /* B-107. Mode 8 interpolates against unit 1's texel; if tile 1 failed to
     * resolve, referencing the disabled unit is undefined, so fall back to
     * plain modulation - the base-tile rendering this feature replaces. */
    if (g_texenv == 8 && !g_tex1_gl_on) { batch_end(); texenv_set(0); }
}


/* ======================= 2D state ========================================= */

/* Colour registers. Every one of these is rrggbbaa in the lower word - see
 * ucode05.txt's "RDP GSetColor" block, which FB, F8 and F9 all defer to. */
static unsigned char g_prim[4], g_env[4], g_fog[4], g_blend[4], g_fill[4];

/* B-045 probe. What the list asks for in the way of fog, per frame. */
static unsigned g_fogp_cmds;              /* G_MW_FOG movewords seen        */
static int      g_fogp_fm, g_fogp_fo;     /* the two signed 16-bit halves   */
static unsigned g_fog_geom_tris;          /* triangles drawn with G_FOG on  */
static unsigned g_fog_p_tris;             /* ... whose blender names CLR_FOG */
static unsigned g_fog_a_shade, g_fog_a_fog;  /* which alpha it multiplies by */
static unsigned char g_fog_col_at[4];     /* fog colour when the first one drew */
static float    g_fog_far_at, g_fog_near_at;  /* ... and its clip planes      */
static float    g_fog_proj_at[4];         /* proj[10],[11],[14],[15] with it  */
static int      g_fog_have_at;

static unsigned g_prim_lod_min, g_prim_lod_level;
static int      g_fill_is16;              /* F7 arrived as replicated 5551 */
static unsigned g_col_cmds[5];            /* prim, env, fog, blend, fill */

/* FC setcombine, classified rather than emulated - see the citation above.
 * g_cc_rgb_const is the mux code of the colour output when the multiplier is
 * zero (so the output is exactly d), else -1. */
static unsigned g_cc_cmds;
static int      g_cc_rgb_const = -1, g_cc_a_prim, g_cc_a_texel;
/* B-051. The tinted-glass pane: CYCLE 1's alpha, which nothing here read.
 * See cc_glass_on() and the classification in the FC case. */
static int      g_cc_a_prim1;
static unsigned g_cc_glass_tris;

/* ---- B-051 THE FIX. The RDP's alpha equation, represented independently
 *      of its colour equation. DEFAULT ON; SL_ALPHA_EQ=0 opts out. ---------
 *
 * Rule 7, ucode05_old.txt "FC rdp_setcombine": each cycle computes
 *
 *      alpha = (Aa - Ab) * Ac + Ad
 *
 * over its own three-bit mux list (0 COMB, 1 TEX0, 2 TEX1, 3 PRIM, 4 SHADE,
 * 5 ENV, 6 the constant 1, 7 the constant 0), and it is INDEPENDENT of the
 * colour equation beside it. GL_MODULATE is not: it forces
 *
 *      out.a = tex.a * in.a
 *
 * which has no additive term at any vertex alpha, and no way to say "alpha
 * does not consult the texture at all". Both of those are shapes the game
 * actually emits, so both were being silently dropped.
 *
 * MEASURED BLAST RADIUS, twenty campaign levels, 400 frames each
 * (SL_CC_ADDEND=1, union across the sweep). TEN distinct combiner words carry
 * an addend other than the constant 0 in the cycle that reaches memory:
 *
 *   fc26a004 1f1093fb  17/20 levels  1808913 tris  a1=(COMB-0)*SHADE+PRIM
 *   fc159804 5ffedbf8  13/20         2382046       a1=(0-0)*0+COMB
 *   fc26a004 1ffc93fc  11/20         1660564       a1=(0-0)*0+SHADE
 *   fcffffff fffe793c   8/20           20300       a1=(0-0)*0+SHADE
 *   fcffffff fffe7b3d   7/20          250143       a1=(0-0)*0+ENV
 *   fc159a04 5ffefff8   6/20          552309       a1=(0-0)*0+COMB
 *   fc26e404 1ffcfffc   3/20          211537       a1=(0-0)*0+SHADE
 *   fc26a004 1ffc93fd   3/20          214323       a1=(0-0)*0+ENV
 *   fc127e24 fffff9fc   2/20            4378       a1=(0-0)*0+SHADE
 *   fcffffff fffe7838   1/20            5970       a1=(0-0)*0+COMB
 *
 * THE ADDITIVE CASE IS EXACTLY ONE WORD - the tinted-glass pane. Do not widen
 * this change out of caution: the other nine are `(0 - 0) * 0 + X`, which is
 * not an addend but a REPLACE, "alpha is exactly X, with no texel term".
 *
 * AND THE NINE REPLACE FORMS ARE THE REGRESSION RISK, not the additive one.
 * They render today as tex.a * vertex.a; under a correct independent path
 * they stop consulting the texel and go MORE OPAQUE - 2382046 triangles on
 * fc159804 5ffedbf8 alone. A rooms-only sweep would not see it. That is the
 * B-040 shape and it is what the verification below has to look for.
 *
 * Three modes, chosen from the decoded equation and nothing else:
 *
 *   AEQ_REPLACE   the equation names no texel anywhere (expanded through
 *                 COMBINED into cycle 0). Compute it exactly per vertex and
 *                 tell GL to REPLACE alpha from the primary colour.
 *   AEQ_MODULATE  it names a texel and the addend is the constant 0. This is
 *                 today's behaviour and it is already right: hand back the
 *                 non-texel factor and let GL_MODULATE supply the texel.
 *   AEQ_ADD       it names a texel AND carries a real addend. Hand back the
 *                 addend and use GL_ADD(TEXTURE, PRIMARY).
 *
 * THE LIMIT OF AEQ_ADD, stated rather than hidden: GL 1.3's fixed-function
 * COMBINE_ALPHA has MODULATE and ADD but no multiply-add, so
 * `tex.a * SHADE.a + PRIM.a` is rendered as `tex.a + PRIM.a`. That is EXACT
 * when SHADE.a is 255 and an over-estimate otherwise. The one word in this
 * class is the glass pane, whose vertices measured SHADE.a = 255
 * (`rgba 474053ff`, SL_TRI_WATCH at read=10103), so it is exact there. If a
 * future combiner lands in this class with a graded shade alpha, this is the
 * approximation that will show first.
 */
static unsigned g_aeq_0a, g_aeq_0b, g_aeq_0c, g_aeq_0d;  /* cycle 0 */
static unsigned g_aeq_1a, g_aeq_1b, g_aeq_1c, g_aeq_1d;  /* cycle 1 */
static unsigned g_aeq_tris[AEQ_NMODE];
/* Which combiner words actually reach a draw in genuine-ADD mode, and how
 * many triangles each contributes. The coordinator's check: an ADD triangle
 * count must be attributable to specific words, or it is the same
 * unreconciled inconsistency that the source-membership classifier produced. */
#define AEQ_ADDW_MAX 48
static unsigned g_aeq_addw[AEQ_ADDW_MAX][2];
static unsigned g_aeq_addw_tris[AEQ_ADDW_MAX][AEQ_NMODE]; /* by AEQ_ mode */
static unsigned g_aeq_addw_n, g_aeq_addw_overflow;
/* B-051 WORK ITEM 1. The observed range of every non-texel alpha input, per
 * word, and a verdict on whether the GL representation reproduces the RDP
 * equation for the values actually seen.
 *
 * Why ranges: the ADD representation drops the multiply term's non-texel
 * factor (GL 1.3 has no fixed-function multiply-add), so `tex.a*SHADE.a+PRIM.a`
 * renders as `tex.a+PRIM.a`. That is exact when SHADE.a is 255 and wrong
 * otherwise, and the only way to know which happens in this game is to watch
 * the inputs on real draws. Ranges alone would not settle it either, so the
 * exactness verdict below evaluates BOTH sides at several texel values.
 *
 * Ranges are init'd lo=256 hi=-1 so a word that never drew is distinguishable
 * from one whose inputs are all zero - an empty range must not read as 0..0. */
static int      g_aeq_lo[AEQ_ADDW_MAX][3];   /* [0]=SHADE [1]=PRIM [2]=ENV */
static int      g_aeq_hi[AEQ_ADDW_MAX][3];
static unsigned g_aeq_ok[AEQ_ADDW_MAX], g_aeq_bad[AEQ_ADDW_MAX];
static int      g_aeq_err[AEQ_ADDW_MAX];     /* worst |GL - RDP|, 0..255 */
static int      g_aeq_err_sh[AEQ_ADDW_MAX];  /* the SHADE.a it happened at */
static unsigned g_aeq_exact_ok, g_aeq_exact_bad;
/* The cycle type the word drew under, kept so the report can re-derive the
 * PRODUCING cycle's equation. It is not a property of the word: BA
 * setothermode_h can arrive after the FC, which is why the mode is resolved at
 * draw time in the first place. 255 = never drew. */
static unsigned char g_aeq_cyc[AEQ_ADDW_MAX];
/* EVERY mode is attributed to its combiner word, not only ADD. "238k replace
 * triangles changed nothing" is only a finding if the words behind them can be
 * named; otherwise it is an absence. */
struct aeq_expr;
static void aeq_note_word(int mode, const struct aeq_expr *e,
                          const struct vtx *v);
static unsigned char aeq_vertex_alpha(int mode, const struct aeq_expr *e,
                                      const struct vtx *v);
static int aeq_repr_exact(int mode, const struct aeq_expr *e,
                          const struct vtx *v, int *worst);
/* SL_AEQ_WATCH=<lower combiner word, hex> reports, once per frame, whether
 * that word reached a draw and with how many triangles - so a moment where a
 * specific combiner is actually exercised can be FOUND rather than assumed. */
static unsigned g_aeq_watch_w1, g_aeq_watch_tris;
static int aeq_watch_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("SL_AEQ_WATCH");
        on = 0;
        if (v != NULL && *v != '\0') {
            g_aeq_watch_w1 = (unsigned) strtoul(v, NULL, 16);
            on = 1;
        }
    }
    return on;
}
void sl_alpha_eq_report(void);
/* DEFAULT ON. SL_ALPHA_EQ=0 opts OUT. Signed off by the owner after the
 * verification below stood up; see the accessor's own comment for the
 * evidence that earned it.
 *
 * Kept as history because the lesson outlived the state it described: this
 * was once flipped default-ON in a working tree BEFORE its verification
 * stood up, and the owner play-tested an unverified default-on renderer as a
 * result. The rule that came out of it - a global renderer change ships OFF
 * until its evidence is in - is why the texgen and lighting work above was
 * held at default OFF through several rounds of measurement. */
static int alpha_eq_on(void)
{
    static int on = -1;
    /* DEFAULT ON. The RDP runs INDEPENDENT rgb and alpha equations; fixed
     * function GL_MODULATE couples them (out.a = tex.a * in.a) and has no
     * additive term at all, so every alpha addend was dropped. Facility's
     * tinted glass is the one combiner in this game whose addend is non-zero:
     * (COMBINED.a - 0) * SHADE.a + PRIMITIVE.a, which saturates at the opaque
     * end. The old path rendered that pane flat at ~40% at every distance.
     *
     * Earned rather than assumed. Facility coverage: every changed pixel is
     * the pane going opaque; REPLACE 1.19M tris / 3 words and MODULATE 3.36M
     * / 4 words change nothing outside it; characters in motion and the
     * menu/watch path byte-identical; and the corrected path consumes Rare's
     * full opacity curve (251, 241, 231 ... 147) rather than snapping. A
     * twenty-level census found ten alpha words outside facility and every
     * one is REPLACE or MODULATE - no additive form exists elsewhere.
     *
     * SL_ALPHA_EQ=0 restores the old coupling for A/B. */
    if (on < 0) { const char *v = getenv("SL_ALPHA_EQ");
                  on = !(v != NULL && *v == '0' && v[1] == '\0'); }
    return on;
}
/* The census, the input ranges and the exactness probe are OFF unless asked
 * for: aeq_repr_exact evaluates the equation five times per vertex, which is
 * a real cost on a path that now runs by default. Arming it changes no pixel
 * - every counter it touches is write-only telemetry and tri_alpha's return
 * value does not depend on any of them - so an armed run and a normal run
 * render identically. That is asserted in the verification, not assumed. */
static int aeq_stats_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_AEQ_STATS");
                  on = (v != NULL && *v != '\0' && *v != '0');
                  if (on && alpha_eq_on()) {
                      void sl_aeq_selftest(void);
                      atexit(sl_alpha_eq_report);
                      sl_aeq_selftest();
                  } }
    return on;
}
/* SL_AEQ_MODES=<hex bitmask over the AEQ_ mode numbers> restricts the
 * corrected path to the modes named, leaving every other draw on the
 * PRE-FIX renderer. Default is all modes.
 *
 * This is an ATTRIBUTION instrument: with it, "aztec changed 138792 pixels"
 * becomes "REPLACE changed N of them, MODULATE M, LERP L, the shader S", and
 * a difference can be tied to the representation that caused it instead of to
 * the change as a whole. It is consulted in exactly two places - the texenv
 * choice and the vertex alpha - and it MUST be the same answer in both, or
 * the environment and the value it is fed disagree and the result describes
 * neither path. That is why it is one function and not two tests.
 *
 * With no mask set it is the identity, so a normal run pays one getenv. */
static int aeq_mode_enabled(int mode)
{
    static int mask = -1;
    static unsigned onlyw;
    static int onlyw_on = -1;
    if (mask < 0) {
        const char *v = getenv("SL_AEQ_MODES");
        mask = (v != NULL && *v != '\0') ? (int) strtoul(v, NULL, 16) : ~0;
    }
    if (onlyw_on < 0) {
        /* SL_AEQ_ONLYW=<lower combiner word, hex> narrows the attribution
         * from a MODE to a single WORD, which is what turns "MODULATE moved
         * 138500 pixels" into "this one combiner did". */
        const char *v = getenv("SL_AEQ_ONLYW");
        onlyw_on = (v != NULL && *v != '\0');
        if (onlyw_on) onlyw = (unsigned) strtoul(v, NULL, 16);
    }
    if (onlyw_on && g_cc_w1 != onlyw) return 0;
    return (mask & (1 << mode)) != 0;
}

/* The cycle that reaches memory: cycle 1 in 2-cycle, cycle 0 in 1-cycle -
 * the same rule rm_apply already applies to the blender. */
static void aeq_producing(unsigned *a, unsigned *b, unsigned *c, unsigned *d)
{
    if (g_cycle_type == 1) { *a = g_aeq_1a; *b = g_aeq_1b;
                             *c = g_aeq_1c; *d = g_aeq_1d; }
    else                   { *a = g_aeq_0a; *b = g_aeq_0b;
                             *c = g_aeq_0c; *d = g_aeq_0d; }
}
/* A SIMPLIFIED alpha expression: (mul_a - mul_b) * mul_c + add, with the
 * multiply term dropped entirely when it is identically zero.
 *
 * Leaf sources are never COMBINED. Resolving one substitutes cycle 0's own
 * SIMPLIFIED EXPRESSION in its place - not a boolean "is there a texel in
 * there", which is the bug this replaces. */
struct aeq_expr {
    int      has_mul;
    unsigned mul_a, mul_b, mul_c;
    int      mul_tex;
    int      has_add;
    unsigned add;
    int      add_tex;
};

static struct aeq_expr aeq_simplify(unsigned a, unsigned b, unsigned c,
                                    unsigned d, int depth);

/* Does this leaf source depend on a texel? COMBINED is answered by SIMPLIFYING
 * cycle 0 and asking the resulting expression, so the answer is a property of
 * the reduced equation rather than of which muxes happen to be mentioned. */
static int aeq_src_tex(unsigned m, int depth)
{
    struct aeq_expr e;
    if (m == CC_TEXEL0 || m == CC_TEXEL1) return 1;
    if (m == CC_COMBINED && depth == 0) {
        e = aeq_simplify(g_aeq_0a, g_aeq_0b, g_aeq_0c, g_aeq_0d, 1);
        return (e.has_mul && e.mul_tex) || (e.has_add && e.add_tex);
    }
    return 0;
}

/* decode -> SIMPLIFY. The identities that cover this game:
 *     (A - A) * C + D  ->  D          the difference is identically zero
 *     (A - B) * 0 + D  ->  D          the multiplier is the constant zero
 *     0 + D            ->  D          (the same two cases)
 *     X * Y + 0        ->  a plain multiply, no addend
 *     X * Y + D        ->  a genuine multiply-add
 * `X * 1 + 0 -> X` needs no special case: a multiplier of the constant 1 is
 * carried as an ordinary multiply and every consumer below treats it right. */
static struct aeq_expr aeq_simplify(unsigned a, unsigned b, unsigned c,
                                    unsigned d, int depth)
{
    struct aeq_expr e;
    e.has_mul = 0; e.mul_a = e.mul_b = e.mul_c = CC_A_ZERO; e.mul_tex = 0;
    e.has_add = 0; e.add = CC_A_ZERO; e.add_tex = 0;

    if (a == b || c == CC_A_ZERO) {
        /* The multiply term is identically zero: the equation IS D. */
        if (d == CC_COMBINED && depth == 0)
            return aeq_simplify(g_aeq_0a, g_aeq_0b, g_aeq_0c, g_aeq_0d, 1);
        e.has_add = (d != CC_A_ZERO);
        e.add     = d;
        e.add_tex = e.has_add ? aeq_src_tex(d, depth) : 0;
        return e;
    }
    e.has_mul = 1; e.mul_a = a; e.mul_b = b; e.mul_c = c;
    e.mul_tex = aeq_src_tex(a, depth) || aeq_src_tex(b, depth)
             || aeq_src_tex(c, depth);
    e.has_add = (d != CC_A_ZERO);
    e.add     = d;
    e.add_tex = e.has_add ? aeq_src_tex(d, depth) : 0;
    return e;
}

/* Is this mux a PER-DRAW value, i.e. one GL can carry in the texture
 * environment colour? SHADE is not: it is the vertex colour's alpha and
 * varies across a triangle, which is exactly the distinction that decides
 * whether GL_INTERPOLATE can stand in for the equation. */
static int aeq_src_perdraw(unsigned m)
{
    return m == CC_PRIM || m == CC_ENV || m == CC_A_ONE || m == CC_A_ZERO;
}

static int aeq_expr_tex(const struct aeq_expr *e)
{
    return (e->has_mul && e->mul_tex) || (e->has_add && e->add_tex);
}

/* ...then CLASSIFY the simplified expression, and only then pick the cheapest
 * genuinely-equivalent OpenGL representation. */
static int aeq_classify(const struct aeq_expr *e)
{
    if (!aeq_expr_tex(e))          return AEQ_REPLACE;
    if (!e->has_mul)               return AEQ_MODULATE;
    if (!e->has_add)               return AEQ_MODULATE;
    /* (TEX - B) * C + B is a LERP between B and the texel by C, and
     * GL_INTERPOLATE computes exactly that - including a C that varies per
     * vertex, which is what GL_ADD cannot do. fc159804 5ffedbf8's
     * (TEX0-ENV)*SHADE+ENV is this shape: 7.1M triangles across 13 of 20
     * levels, where GL_ADD's worst error measured 254. */
    if (e->add == e->mul_b && aeq_src_perdraw(e->add)
        && !aeq_src_tex(e->mul_c, 0))
        return AEQ_LERP;
    /* Anything else with a texel and a real addend is a multiply-add. */
    return AEQ_SHADER;
}


/* ---- B-051 VALIDATION GATE. Armed by SL_AEQ_STATS=1. ---------------------
 *
 * Runs the simplifier over the TEN combiner words the twenty-level census
 * (SL_CC_ADDEND=1) measured as carrying a non-zero addend, and shows its
 * working for each: the raw alpha equation, the simplified one, the mode, and
 * whether a texel is still SEMANTICALLY PRESENT after simplification.
 *
 * The working is printed rather than a bare total because a gate that asserts
 * only "9 and 1" can pass for the wrong reason - which is the failure mode
 * that has caught this session repeatedly. The census stays the reference even
 * though the twenty-level sweep does not: it is the only thing that can catch
 * a classifier that disagrees with what the game actually emits, and it
 * already caught one. */
static const char *aeq_name(unsigned m)
{
    static const char *n[8] = {"COMB","TEX0","TEX1","PRIM","SHADE","ENV","1","0"};
    return n[m & 7];
}
static void aeq_render(const struct aeq_expr *e, char *out, unsigned cap)
{
    char buf[128];
    unsigned n = 0;
    const char *s;
    buf[0] = '\0';
    if (e->has_mul) {
        sprintf(buf, "(%s-%s)*%s", aeq_name(e->mul_a), aeq_name(e->mul_b),
                aeq_name(e->mul_c));
        if (e->has_add) {
            strcat(buf, "+");
            strcat(buf, aeq_name(e->add));
        }
    } else if (e->has_add) {
        sprintf(buf, "%s", aeq_name(e->add));
    } else {
        sprintf(buf, "0");
    }
    for (s = buf; *s && n + 1 < cap; s++) out[n++] = *s;
    out[n] = '\0';
}

void sl_alpha_eq_report(void)
{
    unsigned i;
    fprintf(stderr, "sl_aeq: triangles by alpha mode AFTER SIMPLIFICATION -"
                    " replace=%u modulate=%u lerp=%u shader=%u add=%u"
                    " (add is the shader FALLBACK and should be 0)\n",
            g_aeq_tris[AEQ_REPLACE], g_aeq_tris[AEQ_MODULATE],
            g_aeq_tris[AEQ_LERP], g_aeq_tris[AEQ_SHADER],
            g_aeq_tris[AEQ_ADD]);
    if (g_aeq_shader_fallback_tris != 0)
        fprintf(stderr, "sl_aeq: %u triangle(s) wanted the shader and could"
                        " not have it (second texture unit live)\n",
                g_aeq_shader_fallback_tris);
    fprintf(stderr, "sl_aeq: %u distinct combiner word(s) reached a draw"
                    " (overflow=%u); per-word triangles by mode:\n",
            g_aeq_addw_n, g_aeq_addw_overflow);
    /* The CENSUS line: one row per word, carrying the simplified alpha
     * equation and the mode it classified to. Printed here rather than
     * reconstructed offline so a census table can never describe a different
     * decode than the one that drew - the mistake that mislabelled eleven of
     * twenty levels once already. */
    {
        static const char *mn[AEQ_NMODE] = {"OFF","REPLACE","MODULATE",
                                           "ADD","LERP","SHADER"};
        unsigned s0[4], s1[4], sc;
        s0[0]=g_aeq_0a; s0[1]=g_aeq_0b; s0[2]=g_aeq_0c; s0[3]=g_aeq_0d;
        s1[0]=g_aeq_1a; s1[1]=g_aeq_1b; s1[2]=g_aeq_1c; s1[3]=g_aeq_1d;
        sc = g_cycle_type;
        for (i = 0; i < g_aeq_addw_n; i++) {
            unsigned w0 = g_aeq_addw[i][0], w1 = g_aeq_addw[i][1];
            struct aeq_expr e;
            char simp[128];
            unsigned a, b, c, d;
            int mode;
            g_aeq_0a = (w0 >> 12) & 7u; g_aeq_0b = (w1 >> 12) & 7u;
            g_aeq_0c = (w0 >>  9) & 7u; g_aeq_0d = (w1 >>  9) & 7u;
            g_aeq_1a = (w1 >> 21) & 7u; g_aeq_1b = (w1 >>  3) & 7u;
            g_aeq_1c = (w1 >> 18) & 7u; g_aeq_1d =  w1        & 7u;
            g_cycle_type = g_aeq_cyc[i];
            aeq_producing(&a, &b, &c, &d);
            e = aeq_simplify(a, b, c, d, 0);
            mode = aeq_classify(&e);
            aeq_render(&e, simp, sizeof simp);
            fprintf(stderr, "sl_aeqcensus: %08x %08x cyc=%u mode=%-8s"
                            " simplified=%-26s tris=%u\n",
                    w0, w1, (unsigned) g_aeq_cyc[i], mn[mode], simp,
                    g_aeq_addw_tris[i][mode]);
        }
        g_aeq_0a=s0[0]; g_aeq_0b=s0[1]; g_aeq_0c=s0[2]; g_aeq_0d=s0[3];
        g_aeq_1a=s1[0]; g_aeq_1b=s1[1]; g_aeq_1c=s1[2]; g_aeq_1d=s1[3];
        g_cycle_type = sc;
    }
    for (i = 0; i < g_aeq_addw_n; i++)
        fprintf(stderr, "sl_aeq:   %08x %08x  replace=%-8u modulate=%-8u"
                        " lerp=%-8u shader=%-8u add=%-8u\n",
                g_aeq_addw[i][0], g_aeq_addw[i][1],
                g_aeq_addw_tris[i][AEQ_REPLACE],
                g_aeq_addw_tris[i][AEQ_MODULATE],
                g_aeq_addw_tris[i][AEQ_LERP],
                g_aeq_addw_tris[i][AEQ_SHADER],
                g_aeq_addw_tris[i][AEQ_ADD]);
    /* B-051 WORK ITEM 1. What the alpha inputs actually did, and whether the
     * GL representation reproduced the RDP equation at them. An empty range
     * prints as "-" rather than 0..0 so "never drew" cannot read as "was
     * zero". worsterr is the largest |GL - RDP| over five probe texel alphas;
     * anything above 1 is the fixed-function limitation biting. */
    fprintf(stderr, "sl_aeq: alpha INPUT RANGES and REPRESENTATION EXACTNESS"
                    " (5 texel probes/vertex, 1 LSB tolerance):\n");
    for (i = 0; i < g_aeq_addw_n; i++)
        /* An unseen range prints as its sentinel 256..-1, which is not a
         * value any 8-bit input can take - so "never drew" can never be
         * misread as "was zero". sprintf is deliberately NOT used: the
         * decomp defines its own and that is the symbol this binary links. */
        fprintf(stderr, "sl_aeq:   %08x %08x  shade=%d..%d prim=%d..%d"
                        " env=%d..%d  exact=%u inexact=%u worsterr=%d"
                        " (at shade=%d)%s\n",
                g_aeq_addw[i][0], g_aeq_addw[i][1],
                g_aeq_lo[i][0], g_aeq_hi[i][0],
                g_aeq_lo[i][1], g_aeq_hi[i][1],
                g_aeq_lo[i][2], g_aeq_hi[i][2],
                g_aeq_ok[i], g_aeq_bad[i], g_aeq_err[i],
                g_aeq_err[i] > 0 ? g_aeq_err_sh[i] : -1,
                g_aeq_addw_tris[i][AEQ_SHADER] != 0
                    ? "  (shader: exact by construction, not counted)"
                    : g_aeq_err[i] > 1 ? "  <<< NOT EXPRESSIBLE" : "");
    fprintf(stderr, "sl_aeq: exactness TOTAL exact=%u inexact=%u\n",
            g_aeq_exact_ok, g_aeq_exact_bad);
    if (g_aeqrgb_n != 0)
        fprintf(stderr, "sl_aeq: multiply-add draws (the tinted glass):"
                        " %u vertices, vertex rgb r[%d,%d] g[%d,%d] b[%d,%d];"
                        " texture rgbmax<=%d,%d,%d rgbmean<=%d,%d,%d"
                        " alpha[%d,%d]; %u had no resolved texture slot\n",
                g_aeqrgb_n,
                g_aeqrgb_vlo[0], g_aeqrgb_vhi[0],
                g_aeqrgb_vlo[1], g_aeqrgb_vhi[1],
                g_aeqrgb_vlo[2], g_aeqrgb_vhi[2],
                g_aeqrgb_tmax[0], g_aeqrgb_tmax[1], g_aeqrgb_tmax[2],
                g_aeqrgb_tmean[0], g_aeqrgb_tmean[1], g_aeqrgb_tmean[2],
                g_aeqrgb_amin, g_aeqrgb_amax, g_aeqrgb_notex);
}
void sl_aeq_selftest(void);
void sl_aeq_selftest(void)
{
    /* w0, w1 - the ten words, in census order (most levels first). */
    static const unsigned words[10][2] = {
        { 0xfc26a004u, 0x1f1093fbu },   /* tinted glass */
        { 0xfc159804u, 0x5ffedbf8u },
        { 0xfc26a004u, 0x1ffc93fcu },
        { 0xfcffffffu, 0xfffe793cu },
        { 0xfcffffffu, 0xfffe7b3du },
        { 0xfc159a04u, 0x5ffefff8u },
        { 0xfc26e404u, 0x1ffcfffcu },
        { 0xfc26a004u, 0x1ffc93fdu },
        { 0xfc127e24u, 0xfffff9fcu },
        { 0xfcffffffu, 0xfffe7838u }
    };
    static const char *modename[AEQ_NMODE] = {"OFF","REPLACE",
        "MODULATE","ADD","LERP","SHADER"};
    unsigned save0[4], save1[4];
    unsigned savecyc, i;
    unsigned n[AEQ_NMODE];
    int glass_mode = -1, texfree_all_replace = 1;

    save0[0]=g_aeq_0a; save0[1]=g_aeq_0b; save0[2]=g_aeq_0c; save0[3]=g_aeq_0d;
    save1[0]=g_aeq_1a; save1[1]=g_aeq_1b; save1[2]=g_aeq_1c; save1[3]=g_aeq_1d;
    savecyc = g_cycle_type;
    { unsigned m; for (m = 0; m < AEQ_NMODE; m++) n[m] = 0; }

    fprintf(stderr, "sl_aeqt: validation against the twenty-level census -"
                    " ten words carrying a non-zero addend\n");
    for (i = 0; i < 10u; i++) {
        unsigned w0 = words[i][0], w1 = words[i][1];
        struct aeq_expr e;
        char raw[128], simp[128];
        int mode;
        g_aeq_0a = (w0 >> 12) & 7u; g_aeq_0b = (w1 >> 12) & 7u;
        g_aeq_0c = (w0 >>  9) & 7u; g_aeq_0d = (w1 >>  9) & 7u;
        g_aeq_1a = (w1 >> 21) & 7u; g_aeq_1b = (w1 >>  3) & 7u;
        g_aeq_1c = (w1 >> 18) & 7u; g_aeq_1d =  w1        & 7u;
        g_cycle_type = 1;                      /* the census saw these 2-cycle */
        sprintf(raw, "(%s-%s)*%s+%s", aeq_name(g_aeq_1a), aeq_name(g_aeq_1b),
                aeq_name(g_aeq_1c), aeq_name(g_aeq_1d));
        e = aeq_simplify(g_aeq_1a, g_aeq_1b, g_aeq_1c, g_aeq_1d, 0);
        aeq_render(&e, simp, sizeof simp);
        mode = aeq_classify(&e);
        n[mode]++;
        if (w0 == 0xfc26a004u && w1 == 0x1f1093fbu) glass_mode = mode;
        if (!aeq_expr_tex(&e) && mode != AEQ_REPLACE) texfree_all_replace = 0;
        fprintf(stderr, "sl_aeqt: %08x %08x\n"
                        "sl_aeqt:   raw        %s\n"
                        "sl_aeqt:   cycle0     (%s-%s)*%s+%s\n"
                        "sl_aeqt:   simplified %s\n"
                        "sl_aeqt:   mode       %s\n"
                        "sl_aeqt:   texel present after simplification: %s\n",
                w0, w1, raw,
                aeq_name(g_aeq_0a), aeq_name(g_aeq_0b),
                aeq_name(g_aeq_0c), aeq_name(g_aeq_0d),
                simp, modename[mode], aeq_expr_tex(&e) ? "yes" : "no");
    }
    fprintf(stderr, "sl_aeqt: TOTALS replace=%u modulate=%u lerp=%u"
                    " shader=%u add=%u\n",
            n[AEQ_REPLACE], n[AEQ_MODULATE], n[AEQ_LERP], n[AEQ_SHADER],
            n[AEQ_ADD]);
    /* TWO predicates, and they are NOT the same test - which is the whole
     * reason this prints its working.
     *
     * The census (SL_CC_ADDEND) selected words by a SYNTACTIC test on one
     * cycle: "the producing cycle's Ad mux is not the constant 0". The
     * simplifier asks a SEMANTIC question: "does the fully simplified
     * equation contain a genuine multiply-plus-addend", and it follows
     * COMBINED into cycle 0 to answer it.
     *
     * They differ exactly on words whose cycle 1 is (0-0)*0+COMBINED:
     * syntactically Ad is COMBINED, not the constant 0, so the census counts
     * them; semantically the equation IS cycle 0's, which may be a real MAD
     * (5ffedbf8 -> (TEX0-ENV)*SHADE+ENV), a plain modulate (5ffefff8), or
     * texel-free (fffe7838). So "9 REPLACE + 1 ADD" was never derivable from
     * the census predicate - it conflated the two - and bending the
     * classifier to reach it would be fitting the instrument to the expected
     * answer, which is the error this whole gate exists to prevent.
     *
     * What IS invariant and worth failing on: the tinted-glass word must
     * classify ADD, and any word whose simplified form carries no texel must
     * classify REPLACE. Both are checked below. */
    fprintf(stderr, "sl_aeqt: totals are by the SEMANTIC test (simplified"
                    " equation); the census's ten were selected by the"
                    " SYNTACTIC test (Ad != 0 on one cycle). The two differ"
                    " on (0-0)*0+COMBINED forms - see each word's working"
                    " above.\n");
    fprintf(stderr, "sl_aeqt: INVARIANTS %s\n",
            (glass_mode == AEQ_SHADER && texfree_all_replace)
                ? "hold: the glass word is a genuine multiply-add (SHADER),"
                  " and every texel-free simplification is REPLACE."
                : "VIOLATED - the classifier is wrong and nothing downstream"
                  " of it means anything.");

    /* ---- KNOWN-POSITIVE CONTROL for the exactness probe. ----------------
     *
     * sl_alpha_eq_report's "inexact=0" is an exactly-zero result, and an
     * exactly-zero result from an instrument that has never been seen to fire
     * is worth nothing. So fire it, here, every time the self-test runs.
     *
     * The glass word's alpha is (COMBINED - 0) * SHADE + PRIM, and the ADD
     * representation renders it as tex.a + PRIM.a - dropping the * SHADE.a
     * factor. With SHADE.a = 255 that factor is the identity and the two
     * agree; with SHADE.a = 128 they cannot. Both are asserted, so the probe
     * has to distinguish them to pass:
     *
     *   NEGATIVE (shade 255) -> exact,   worst error 0
     *   POSITIVE (shade 128) -> INEXACT, worst error large
     *
     * If the positive ever reports exact, the field measurements above are
     * measuring nothing and must not be believed. */
    {
        struct vtx cv;
        struct aeq_expr ce;
        unsigned char sp = g_prim[3], se = g_env[3];
        int e255, e128, w255 = 0, w128 = 0, elerp, wlerp = 0;
        /* POSITIVE: the multiply-add (TEX0-0)*SHADE+40 rendered as GL_ADD,
         * which drops the * SHADE factor. Exact at SHADE 255, impossible at
         * SHADE 128. The mode is passed EXPLICITLY rather than taken from the
         * classifier, because the classifier now routes this shape to the
         * shader - and the probe exists to judge the FIXED-FUNCTION
         * representations, which are the only ones it can judge. */
        g_aeq_0a = 1u; g_aeq_0b = 7u; g_aeq_0c = 6u; g_aeq_0d = 7u; /* TEX0 */
        g_aeq_1a = 0u; g_aeq_1b = 7u; g_aeq_1c = 4u; g_aeq_1d = 3u; /* glass */
        g_cycle_type = 1;
        g_prim[3] = 40; g_env[3] = 0;
        ce = aeq_simplify(g_aeq_1a, g_aeq_1b, g_aeq_1c, g_aeq_1d, 0);
        cv.a = 255; e255 = aeq_repr_exact(AEQ_ADD, &ce, &cv, &w255);
        cv.a = 128; e128 = aeq_repr_exact(AEQ_ADD, &ce, &cv, &w128);
        /* NEGATIVE: the lerp (TEX0-ENV)*SHADE+ENV rendered as
         * GL_INTERPOLATE, at the SAME graded SHADE that breaks GL_ADD. This
         * one MUST come back exact, or the LERP representation is wrong and
         * 7.1M triangles across 13 levels are being drawn with it. */
        g_aeq_0a = 1u; g_aeq_0b = 5u; g_aeq_0c = 4u; g_aeq_0d = 5u;
        g_aeq_1a = 7u; g_aeq_1b = 7u; g_aeq_1c = 7u; g_aeq_1d = 0u;
        g_env[3] = 200;
        ce = aeq_simplify(g_aeq_1a, g_aeq_1b, g_aeq_1c, g_aeq_1d, 0);
        cv.a = 128; elerp = aeq_repr_exact(AEQ_LERP, &ce, &cv, &wlerp);
        g_prim[3] = sp; g_env[3] = se;
        fprintf(stderr, "sl_aeqt: exactness-probe CONTROLS:"
                        " GL_ADD on (TEX0-0)*SHADE+40 -> shade255 exact=%d"
                        " err=%d | shade128 exact=%d err=%d ;"
                        " GL_INTERPOLATE on (TEX0-ENV)*SHADE+ENV at shade128"
                        " -> exact=%d err=%d  ==> %s\n",
                e255, w255, e128, w128, elerp, wlerp,
                (e255 && !e128 && w128 > 1 && elerp)
                    ? "the probe fires on an inexpressible case and stays"
                      " quiet on an expressible one; inexact=0 in the field"
                      " is a measurement"
                    : "BROKEN - ignore every exactness number below");
    }

    g_aeq_0a=save0[0]; g_aeq_0b=save0[1]; g_aeq_0c=save0[2]; g_aeq_0d=save0[3];
    g_aeq_1a=save1[0]; g_aeq_1b=save1[1]; g_aeq_1c=save1[2]; g_aeq_1d=save1[3];
    g_cycle_type = savecyc;
}

static int aeq_mode_now(void)
{
    unsigned a, b, c, d;
    struct aeq_expr e;
    if (!alpha_eq_on()) return AEQ_OFF;
    aeq_producing(&a, &b, &c, &d);
    e = aeq_simplify(a, b, c, d, 0);
    return aeq_classify(&e);
}
/* One alpha mux, per vertex.
 *
 * `tv` is the value a TEXEL source takes. Every RENDERING caller passes
 * AEQ_TEX_OPAQUE (255), because GL supplies the real texel at the fragment
 * stage and which of MODULATE or ADD does that is what the mode selects. It
 * is an explicit PARAMETER rather than a global precisely so the exactness
 * probe below can evaluate the same equation at several texel values without
 * being able to leak that substitution into a draw. */
#define AEQ_TEX_OPAQUE 255u
static unsigned aeq_eval(unsigned a, unsigned b, unsigned c, unsigned d,
                         const struct vtx *v, int depth, unsigned tv);
static unsigned aeq_src_value(unsigned m, const struct vtx *v, int depth,
                              unsigned tv)
{
    switch (m) {
    case CC_TEXEL0: case CC_TEXEL1: return tv;
    case CC_PRIM:   return g_prim[3];
    case CC_SHADE:  return v->a;
    case CC_ENV:    return g_env[3];
    case CC_A_ONE:  return 255u;
    case CC_A_ZERO: return 0u;
    case CC_COMBINED:
        if (depth) return tv;               /* cycle 0 has no COMBINED input */
        return aeq_eval(g_aeq_0a, g_aeq_0b, g_aeq_0c, g_aeq_0d, v, 1, tv);
    default:        return 255u;
    }
}
static unsigned aeq_eval(unsigned a, unsigned b, unsigned c, unsigned d,
                         const struct vtx *v, int depth, unsigned tv)
{
    int A = (int) aeq_src_value(a, v, depth, tv);
    int B = (int) aeq_src_value(b, v, depth, tv);
    int C = (int) aeq_src_value(c, v, depth, tv);
    int D = (int) aeq_src_value(d, v, depth, tv);
    int r = (((A - B) * C) + 127) / 255 + D;    /* the RDP saturates */
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    return (unsigned) r;
}

/* The byte tri_alpha hands to glColor for this mode and expression - factored
 * out so the exactness probe predicts what the renderer WILL do rather than
 * what it is assumed to do. tri_alpha calls this same function. */
static unsigned char aeq_vertex_alpha(int mode, const struct aeq_expr *e,
                                      const struct vtx *v)
{
    /* What each texenv wants in the primary colour's alpha:
     *   ADD    the addend, GL supplies tex.a + it
     *   LERP   the interpolation WEIGHT, GL supplies tex*w + const*(1-w)
     *   SHADER the raw SHADE alpha - the shader reads gl_Color.a as SHADE and
     *          evaluates the equation itself, so nothing is pre-folded here */
    if (mode == AEQ_SHADER) return v->a;
    if (mode == AEQ_LERP)
        return (unsigned char) aeq_src_value(e->mul_c, v, 0, AEQ_TEX_OPAQUE);
    if (mode == AEQ_ADD)
        return (unsigned char) aeq_src_value(e->add, v, 0, AEQ_TEX_OPAQUE);
    if (!e->has_mul)
        return (unsigned char) (e->has_add
                                ? aeq_src_value(e->add, v, 0, AEQ_TEX_OPAQUE)
                                : 0u);
    return (unsigned char) aeq_eval(e->mul_a, e->mul_b, e->mul_c,
                                    e->has_add ? e->add : CC_A_ZERO, v, 0,
                                    AEQ_TEX_OPAQUE);
}

/* The LERP texenv's per-draw constant: the value the equation lerps AWAY from,
 * which is the addend (== mul_b by the classifier's own test). */
static unsigned char aeq_lerp_const(const struct aeq_expr *e,
                                    const struct vtx *v)
{
    return (unsigned char) aeq_src_value(e->add, v, 0, AEQ_TEX_OPAQUE);
}

/* What GL produces for a given texel alpha, per mode - the three texenv
 * configurations in texenv_set, written out arithmetically. */
static int aeq_gl_predict(int mode, unsigned char va, int tex, int k)
{
    int r;
    if (mode == AEQ_REPLACE) return (int) va;         /* COMBINE_ALPHA REPLACE */
    if (mode == AEQ_ADD)     r = tex + (int) va;      /* COMBINE_ALPHA ADD     */
    else if (mode == AEQ_LERP)                        /* COMBINE_ALPHA INTERP  */
        r = (tex * (int) va + k * (255 - (int) va) + 127) / 255;
    else                     r = (tex * (int) va + 127) / 255;   /* MODULATE   */
    return r > 255 ? 255 : r;
}

/* Does the chosen GL representation reproduce the RDP equation for THIS
 * vertex? Evaluated at five texel alphas rather than one, because the whole
 * question is what happens to the texel term - a single probe value cannot
 * distinguish `tex` from `tex*k` when k is 1 at that value.
 *
 * Tolerance is 1 LSB: GL's fixed-point alpha stage and the integer RDP
 * rounding here need not agree in the last bit, and a 1-count disagreement is
 * not the multiply-add limitation this is looking for. The worst error is
 * reported separately so a 1-LSB tolerance cannot hide a real one.
 *
 * This probe does NOT touch renderer state: the texel value is a parameter of
 * aeq_eval, never a global, so nothing it evaluates can reach a draw. */
static int aeq_repr_exact(int mode, const struct aeq_expr *e,
                          const struct vtx *v, int *worst)
{
    static const unsigned probe[5] = { 0u, 64u, 128u, 192u, 255u };
    unsigned char va = aeq_vertex_alpha(mode, e, v);
    int k = (mode == AEQ_LERP) ? (int) aeq_lerp_const(e, v) : 0;
    unsigned i;
    int bad = 0;
    *worst = 0;
    /* AEQ_SHADER evaluates the same equation this probe evaluates, so the
     * probe cannot tell them apart and a "0 inexact" for it would be a
     * tautology, not a measurement. Say so rather than count it. */
    if (mode == AEQ_SHADER) return 1;
    for (i = 0; i < 5u; i++) {
        int exact = (int) aeq_eval(e->mul_a, e->mul_b, e->mul_c,
                                   e->has_add ? e->add : CC_A_ZERO, v, 0,
                                   probe[i]);
        int got = aeq_gl_predict(mode, va, (int) probe[i], k);
        int d = got - exact;
        if (d < 0) d = -d;
        if (d > *worst) *worst = d;
        if (d > 1) bad = 1;
    }
    return !bad;
}

static void aeq_note_word(int mode, const struct aeq_expr *e,
                          const struct vtx *v)
{
    unsigned i;
    int in[3], k, worst = 0, ok;
    for (i = 0; i < g_aeq_addw_n; i++)
        if (g_aeq_addw[i][0] == g_cc_w0 && g_aeq_addw[i][1] == g_cc_w1) break;
    if (i == g_aeq_addw_n) {
        if (g_aeq_addw_n >= AEQ_ADDW_MAX) { g_aeq_addw_overflow++; return; }
        g_aeq_addw[i][0] = g_cc_w0;
        g_aeq_addw[i][1] = g_cc_w1;
        { int m; for (m = 0; m < AEQ_NMODE; m++) g_aeq_addw_tris[i][m] = 0; }
        g_aeq_lo[i][0] = g_aeq_lo[i][1] = g_aeq_lo[i][2] = 256;
        g_aeq_hi[i][0] = g_aeq_hi[i][1] = g_aeq_hi[i][2] = -1;
        g_aeq_ok[i] = g_aeq_bad[i] = 0;
        g_aeq_err[i] = 0; g_aeq_err_sh[i] = -1;
        g_aeq_cyc[i] = (unsigned char) g_cycle_type;
        g_aeq_addw_n++;
    }
    g_aeq_addw_tris[i][mode]++;
    in[0] = (int) v->a; in[1] = (int) g_prim[3]; in[2] = (int) g_env[3];
    for (k = 0; k < 3; k++) {
        if (in[k] < g_aeq_lo[i][k]) g_aeq_lo[i][k] = in[k];
        if (in[k] > g_aeq_hi[i][k]) g_aeq_hi[i][k] = in[k];
    }
    if (mode == AEQ_SHADER) {
        /* Counted, but kept OUT of the exact/inexact totals - see
         * aeq_repr_exact. An exactly-zero total has to stay meaningful. */
        g_aeq_shader_tris++;
        g_aeqrgb_n++;
        if ((int) v->r < g_aeqrgb_vlo[0]) g_aeqrgb_vlo[0] = v->r;
        if ((int) v->g < g_aeqrgb_vlo[1]) g_aeqrgb_vlo[1] = v->g;
        if ((int) v->b < g_aeqrgb_vlo[2]) g_aeqrgb_vlo[2] = v->b;
        if ((int) v->r > g_aeqrgb_vhi[0]) g_aeqrgb_vhi[0] = v->r;
        if ((int) v->g > g_aeqrgb_vhi[1]) g_aeqrgb_vhi[1] = v->g;
        if ((int) v->b > g_aeqrgb_vhi[2]) g_aeqrgb_vhi[2] = v->b;
        if ((int) g_prim[3] < g_aeqrgb_plo) g_aeqrgb_plo = g_prim[3];
        if ((int) g_prim[3] > g_aeqrgb_phi) g_aeqrgb_phi = g_prim[3];
        {
            int k, seen = 0;
            for (k = 0; k < g_aeqrgb_pset_n; k++)
                if (g_aeqrgb_pset[k] == g_prim[3]) { seen = 1; break; }
            if (!seen) {
                if (g_aeqrgb_pset_n < AEQ_PRIMSET_MAX)
                    g_aeqrgb_pset[g_aeqrgb_pset_n++] = g_prim[3];
                else g_aeqrgb_pset_over++;
            }
        }
        /* g_tex_slot is the slot recorded AT RESOLUTION - never a by-name
         * search, which returns a stale entry once the 256-slot ring wraps.
         * That fault is already on the record; this is the corrected form. */
        if (g_tex_slot >= 0) {
            const struct sl_texent *te = &g_texcache[g_tex_slot];
            if ((int) te->rmax > g_aeqrgb_tmax[0]) g_aeqrgb_tmax[0] = te->rmax;
            if ((int) te->gmax > g_aeqrgb_tmax[1]) g_aeqrgb_tmax[1] = te->gmax;
            if ((int) te->bmax > g_aeqrgb_tmax[2]) g_aeqrgb_tmax[2] = te->bmax;
            if ((int) te->rmean > g_aeqrgb_tmean[0]) g_aeqrgb_tmean[0] = te->rmean;
            if ((int) te->gmean > g_aeqrgb_tmean[1]) g_aeqrgb_tmean[1] = te->gmean;
            if ((int) te->bmean > g_aeqrgb_tmean[2]) g_aeqrgb_tmean[2] = te->bmean;
            if (g_aeqrgb_amin < 0 || (int) te->amin < g_aeqrgb_amin)
                g_aeqrgb_amin = te->amin;
            if ((int) te->amax > g_aeqrgb_amax) g_aeqrgb_amax = te->amax;
        } else g_aeqrgb_notex++;
    } else {
        ok = aeq_repr_exact(mode, e, v, &worst);
        if (ok) { g_aeq_ok[i]++;  g_aeq_exact_ok++;  }
        else    { g_aeq_bad[i]++; g_aeq_exact_bad++; }
    }
    if (mode != AEQ_SHADER && worst > g_aeq_err[i]) {
        g_aeq_err[i] = worst; g_aeq_err_sh[i] = in[0];
    }
}

/* g_cc_texel (B-035) is declared up with tex_apply, its only consumer. */
/* B-043. The cycle-0 alpha combiner in the restricted form
 * (a - 0) * ENVIRONMENT + 0, and which mux fills `a`. See tri_alpha. */
static int      g_cc_a_envscale, g_cc_a_envsrc;
static unsigned g_env_alpha_tris;

/* B-043 follow-up probe, SL_FADE_DBG=1, native telemetry only.
 *
 * g_cc_a_envscale is true exactly on the faded-character combiner (see
 * tri_alpha), so it is a ready-made selector for "the triangles the head
 * passthrough is made of". These counters answer, for that set alone: does
 * the game ask for back-face culling on it, what render mode is bound, and
 * how the windings split. Per frame, because a per-run total cannot say
 * which frame it came from - the lesson B-043 already paid for. */
static unsigned g_fd_tris, g_fd_ccw, g_fd_cw, g_fd_zero;
static unsigned g_fd_cull[4];
static unsigned g_fd_om[8], g_fd_om_n[8];
static unsigned g_fd_om_used;
static unsigned g_fd_geom_seen;

/* Watertightness of the character mesh, measured on MESH vertices rather than
 * on slot numbers.
 *
 * The existing g_edge_ok/g_edge_bad clears its table on every vertex load, so
 * it can only see the two halves of a quad that arrived in the same batch. A
 * character is hundreds of vertices delivered 16 at a time, so almost every
 * edge that matters is invisible to it - which is why it reported Rare's data
 * "consistently wound" while culling still opened holes.
 *
 * Keying on the source ADDRESS of the vertex fixes that: the same mesh point
 * loaded into two different slots in two different batches is one key. For a
 * closed, consistently wound mesh every interior edge is traversed once in
 * each direction; an edge traversed TWICE THE SAME WAY is two triangles
 * facing opposite ways, which is exactly the shape of a cull hole. */
static unsigned long g_vsrc[32];
#define FD_EDGE_SLOTS 8192u
static unsigned long g_fd_edge_u[FD_EDGE_SLOTS], g_fd_edge_v[FD_EDGE_SLOTS];
static unsigned char g_fd_edge_used[FD_EDGE_SLOTS];
static unsigned g_fd_e_ok, g_fd_e_bad, g_fd_e_new, g_fd_e_full;
/* Which of GE_TRI4's four sub-slots the triangle in hand came from, and how
 * the bad edges distribute over them. If the decode of one nibble pair were
 * wrong, the flips would pile onto one slot. */
static int      g_fd_slot;
static unsigned g_fd_bad_slot[5];    /* [4] = the BF single-triangle command */
static unsigned g_fd_slot_tris[5];
/* #25 cull witness: the triangle command's own words and emit_tri's
 * clip classification, for the mark record only. */
static unsigned g_tri_cw0, g_tri_cw1;
static int      g_tri_cls;

static int fd_edge_find(unsigned long u, unsigned long v, int insert)
{
    unsigned h = (unsigned) ((u * 2654435761ul) ^ (v * 40503ul));
    unsigned i;
    for (i = 0; i < 64u; i++) {
        unsigned s = (h + i) & (FD_EDGE_SLOTS - 1u);
        if (!g_fd_edge_used[s]) {
            if (!insert) return 0;
            g_fd_edge_used[s] = 1;
            g_fd_edge_u[s] = u; g_fd_edge_v[s] = v;
            return 0;
        }
        if (g_fd_edge_u[s] == u && g_fd_edge_v[s] == v) return 1;
    }
    g_fd_e_full++;
    return 0;
}

/* A triangle drawn TWICE registers all three of its edges as "same way twice"
 * without any winding being wrong, so the two cases have to be separated
 * before the bad-edge count means anything. Keyed on the sorted address
 * triple, so a repeat in any rotation or reflection is caught. */
static unsigned long g_fd_tri_k[FD_EDGE_SLOTS];
static unsigned char g_fd_tri_used[FD_EDGE_SLOTS];
static unsigned g_fd_tri_dup;

static void fd_tri_note(unsigned long a, unsigned long b, unsigned long c)
{
    unsigned long t, k;
    unsigned h, i;
    if (a > b) { t = a; a = b; b = t; }
    if (b > c) { t = b; b = c; c = t; }
    if (a > b) { t = a; a = b; b = t; }
    k = (a * 2654435761ul) ^ (b * 2246822519ul) ^ (c * 3266489917ul);
    h = (unsigned) (k >> 13);
    for (i = 0; i < 64u; i++) {
        unsigned s = (h + i) & (FD_EDGE_SLOTS - 1u);
        if (!g_fd_tri_used[s]) {
            g_fd_tri_used[s] = 1; g_fd_tri_k[s] = k; return;
        }
        if (g_fd_tri_k[s] == k) { g_fd_tri_dup++; return; }
    }
}

static int fd_dbg(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_FADE_DBG");
                  on = (s != NULL && *s >= '0' && *s <= '9') ? *s - '0' : 0; }
    return on;
}

static void fd_edge_note(unsigned long u, unsigned long v)
{
    if (u == 0 || v == 0 || u == v) return;
    if (fd_edge_find(v, u, 0))      g_fd_e_ok++;    /* opposite seen: good */
    else if (fd_edge_find(u, v, 0)) { g_fd_e_bad++; /* same way twice: bad */
                                      g_fd_bad_slot[g_fd_slot % 5]++; }
    else                          { g_fd_e_new++; fd_edge_find(u, v, 1); }
}

/* Window-space triangle coverage - see the area block in emit_tri. */
static unsigned g_tri_degen, g_tri_area, g_tri_clipped;
static double   g_tri_area_sum;
static float    g_tri_area_max;

/* ---- B-051 probe. WHERE does the tinted-glass pane land? SL_GLASSPOS=1. ---
 *
 * The pane is submitted with ~100 triangles while it is opaque and the window
 * is still a hole, so the open question is no longer "is it drawn" but "where
 * do those triangles go". This accumulates their NDC bounding box per frame,
 * next to the SAME box computed over every triangle in the frame - the
 * known-positive control, without which a plausible-looking glass box cannot
 * be told from a broken instrument.
 *
 * Reports only, changes nothing, and does not read cc_glass_on(): the shape
 * classifier g_cc_a_prim1 is what identifies the pane, and SL_CC_GLASS (which
 * is OFF and staying off) only decides whether the alpha is USED. */
static unsigned g_gp_tris, g_gp_behind, g_gp_all, g_gp_marked;
static float    g_gp_min[3], g_gp_max[3];      /* glass, NDC */
static float    g_gp_amin[3], g_gp_amax[3];    /* all triangles, NDC */
static double   g_gp_area;                     /* glass, signed px^2 at 640x480 */
static int glasspos_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_GLASSPOS");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}
/* SL_GLASS_MARK=1 paints the tinted-glass pane magenta at full alpha, with
 * texturing, blending and the depth test left exactly as they were.
 *
 * The bounding box above says where the pane's triangles ARE; this says
 * whether they REACH THE FRAMEBUFFER. Those are different questions and the
 * previous three rounds of this investigation kept answering one while
 * claiming the other. If the hole comes back magenta the pane is landing
 * there and the fault is its colour or its alpha; if the hole stays the
 * backdrop colour the pane is not covering it at all and the fault is
 * position, depth or rejection.
 *
 * CAVEAT, MEASURED THE HARD WAY (B-051, 2026-08-27). THAT LAST SENTENCE IS
 * WRONG AND COST A ROUND. Mode 1's colour is MULTIPLIED BY THE TEXEL under
 * GL_MODULATE, so on a texture whose RGB is near zero the magenta is
 * multiplied away and the frame comes back BYTE-IDENTICAL however well the
 * triangles drew. The facility pane is exactly such a texture. "0 pixels
 * changed" was read as "the pane contributes zero pixels" when it only meant
 * "the paint was multiplied by ~0", and the painted-magenta counter cannot
 * catch it - the branch IS reached, it just does not matter.
 *
 * A probe that could not have produced a different answer is not evidence.
 * Corroborate any negative from mode 1 with mode 2 or mode 3, which change
 * the pipeline rather than only the colour. */
static int glassmark_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_GLASS_MARK");
                  on = (v != NULL && *v != '\0') ? atoi(v) : 0; }
    return on;
}
static void gp_reset(void)
{
    int j;
    g_gp_tris = g_gp_behind = g_gp_all = g_gp_marked = 0;
    g_aeq_watch_tris = 0;
    g_gp_area = 0.0;
    for (j = 0; j < 3; j++) {
        g_gp_min[j] = g_gp_amin[j] =  1e30f;
        g_gp_max[j] = g_gp_amax[j] = -1e30f;
    }
}
static void gp_note(const float ndc[3][5], int is_glass)
{
    int i, j;
    float *lo = is_glass ? g_gp_min : g_gp_amin;
    float *hi = is_glass ? g_gp_max : g_gp_amax;
    if (!glasspos_on()) return;
    if (is_glass) g_gp_tris++; else g_gp_all++;
    for (i = 0; i < 3; i++) {
        if (ndc[i][3] == 0.0f) { if (is_glass) g_gp_behind++; return; }
    }
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            if (ndc[i][j] < lo[j]) lo[j] = ndc[i][j];
            if (ndc[i][j] > hi[j]) hi[j] = ndc[i][j];
        }
    if (is_glass) {
        float ax = (ndc[1][0] - ndc[0][0]) * 320.0f;
        float ay = (ndc[1][1] - ndc[0][1]) * 240.0f;
        float bx = (ndc[2][0] - ndc[0][0]) * 320.0f;
        float by = (ndc[2][1] - ndc[0][1]) * 240.0f;
        g_gp_area += (double) ((ax * by - ay * bx) * 0.5f);
    }
}

/* B6/B7. The RSP's geometry mode is a persistent register, set and cleared
 * rather than assigned, so this is sticky across the whole list exactly as the
 * hardware's is. Applied only under SL_CULL - see cull_apply. */
static unsigned g_geom_mode, g_geom_cmds;

/* B-113. Nonzero while the walk is inside a ROOM-geometry submission - set
 * at the depth-1 G_DL dispatch from the same g_BgRoomInfo lookup the mark
 * provenance uses (sl_mark_room_of_dl, src/native/sl_game_query.c), cleared
 * when the submission returns. Room meshes are statically authored with
 * consistent winding - SL_CULL=1 renders them correctly (measured at the
 * owner's Dam mountain mark) - while the animated-model path still carries
 * the dotube winding debt that made full sense-1 culling unshippable (the
 * truck tyre, re-measured 2026-09-10: still a crescent under SL_CULL=1).
 * The flag lets the default cull mode honour the game's cull bits exactly
 * where they are known to be safe. */
static int g_room_dl;

/* ---- Does anything actually ask to be culled, and would it matter? -------
 *
 * g_geom_mode is the mode as of the END of the list, which is the one number
 * that cannot answer either question: the cull bits are set and cleared many
 * times inside a frame and land back at 0x205. So the state is sampled where
 * it is used - per triangle - rather than where it is reported.
 *
 * g_geom_seen ORs every bit any B6 or B7 ever named, so "no level ever asks
 * for culling" becomes falsifiable. g_cull_tris counts triangles by the cull
 * state in force when they were emitted, indexed front | back<<1.
 *
 * The winding pair predicts the answer before the window is opened. Under
 * G_CULL_BACK the RSP draws only front faces, so of the triangles this
 * renderer emits under that mode, one winding is the set the hardware would
 * have kept and the other is the set it would have thrown away. On the closed
 * limbs and cylinders that carry the bit the kept set is the NEARER one, so
 * the winding with the smaller mean NDC z is the front face for this
 * projection. It called it right - counter-clockwise, on three of the four
 * levels - but only weakly (0.966 vs 0.978 on streets), which is why the
 * captures and not this number are what settled it. */
static unsigned g_geom_seen;
static unsigned g_cull_tris[4];
static unsigned g_cull_n_ccw, g_cull_n_cw;
static double   g_cull_z_ccw, g_cull_z_cw;

/* Timeline of the cull bits in DL order, with the triangle count at each
 * change. "Is cull-back genuinely in force while the ROOMS draw, or is it
 * left over from a model that drew earlier and never cleared it?" is not a
 * question a per-frame total can answer, and it is the whole question. */
#define GEOMLOG_MAX 24
static unsigned g_geomlog_tris[GEOMLOG_MAX], g_geomlog_mode[GEOMLOG_MAX];
static unsigned g_geomlog_n, g_geomlog_lost;
/* Big triangles by cull state - a wall is hundreds of px, a limb facet is a
 * few. If culling only ever touched small geometry it would not matter. */
static unsigned g_cull_big[4];

/* Winding consistency measured on the INDICES, with no geometry involved, so
 * near-degenerate facets cannot vote. Two triangles that share an edge are
 * consistently wound exactly when each traverses that edge in the OPPOSITE
 * direction - that is what "both faces point the same way" means. If a quad
 * arrives as {0,1,2} and {1,2,3} (a strip listed literally) the shared edge
 * 1->2 runs the same way in both and one of the pair faces backwards, which
 * is the exact shape of the holes culling opens: half a quad, clean diagonal.
 * Compared against the immediately preceding triangle, which for B1 is its
 * own neighbour in the same command. */
static unsigned g_edge_ok, g_edge_bad, g_edge_none;

/* Directed edges emitted since the last vertex load, as a 32x32 bit matrix.
 * Comparing only against the previous triangle misses the case that matters -
 * the two halves of a quad arriving in different B1 commands - so every
 * triangle is checked against every edge the current vertex batch has already
 * produced. A vertex load invalidates the slot numbers, so it clears the
 * table. */
static unsigned g_edge_seen[32];

static void edge_check(int a, int b, int c)
{
    int t[3], i, u, v;

    t[0] = a; t[1] = b; t[2] = c;
    if (a == b || b == c || a == c) return;    /* degenerate: no opinion */
    for (i = 0; i < 3; i++) {
        u = t[i]; v = t[(i + 1) % 3];
        if (g_edge_seen[v] & (1u << u))      g_edge_ok++;   /* v->u seen */
        else if (g_edge_seen[u] & (1u << v)) g_edge_bad++;  /* u->v again */
        else                                 g_edge_none++;
        g_edge_seen[u] |= 1u << v;
    }
}

static void geomlog(void)
{
    unsigned cull = g_geom_mode & (GEOM_CULL_FRONT | GEOM_CULL_BACK);
    if (g_geomlog_n != 0 && g_geomlog_mode[g_geomlog_n - 1] == cull) return;
    if (g_geomlog_n >= GEOMLOG_MAX) { g_geomlog_lost++; return; }
    g_geomlog_tris[g_geomlog_n] = g_tris;
    g_geomlog_mode[g_geomlog_n] = cull;
    g_geomlog_n++;
}

/* ---- Applying them ------------------------------------------------------
 *
 * SL_CULL selects between the readings so the window can decide, which is the
 * only thing that can:
 *   unset / 1  the default - RSP cull-back -> GL_BACK, cull-front ->
 *              GL_FRONT, on EVERY draw. The hardware sense (#25, below).
 *   0          no culling at all - what this file did before
 *   2          the inverse, i.e. GL's winding is opposite the RSP's here
 *   3          diagnostic: cull nothing, draw ONLY what 1 would have dropped
 *   4          B-043's former default: sense 1 only where the draw does not
 *              write depth. Kept as a bisect arm.
 *   5          B-113's arm: 4, plus sense 1 on room-geometry submissions.
 *
 * ---- #25: THE CARTRIDGE CULLS WHAT SENSE 1 CULLS - measured 2026-09-15 ----
 *
 * Mode 4 shipped on the premise that the depth buffer removes a back face
 * anyway, and stayed the default on two objections to sense 1: the Dam truck
 * tyre (B-043) and the Dam massif "losing most of its faces" (B-114). Neither
 * objection had ever been checked against the cartridge; both were judged
 * against this renderer's own over-drawn frame. The Surface 2 sniper tower
 * (Gitea #25) forced the question: its wooden platform is a zero-thickness
 * plane whose dark underside is submitted under G_CULL_BACK, coincident with
 * the lit top, so no depth test can settle it and mode 4 flickered.
 *
 * TWO WITNESSES, ONE RULE. Both triangles were marked with the cull-witness
 * fields (form, command words, clip class, signed NDC area in command order):
 *
 *   tower  Surface 2 room 3, mark-002 pose, B1 slot 0 of b1000032 00002010,
 *          slots {0,1,2}, all w > 0, guard-crossing (the fan path, winding
 *          preserved), area -4.50 -> clockwise, CULL_BACK authored.
 *   massif Dam room 1, dl 2001dd90+0xf8, B1 slot 0 of b1009552 87364310,
 *          slots {0,1,2}, all w > 0, inside the 2w box (the in-line RSP
 *          path, no clipping), area -0.39 -> clockwise, CULL_BACK authored.
 *
 * Same decode, same class of evaluation, same verdict: sense 1 drops both.
 * The cartridge (parallel_n64, the ROM at the SAME poses - the player's
 * tile, position and look angles poked to the marks' values; scratchpad
 * dam\romtele.py) draws the tower's planks lit and stable, and draws the
 * massif from room 121 at theta 70/105/140/175 with the sky and the far
 * ridge exactly where sense 1 puts them - the slabs mode 4 drew across
 * those views are the mountain's interior and the cartridge never shows
 * them (scratchpad dam\3way-r121.png). There was no shared winding error
 * to find: this file's screen-winding evaluation already agrees with the
 * RSP on both witnesses; the wrong stage was the POLICY of not applying it
 * to depth-writing draws. The Dam intro under SL_VI_CATCHUP=0, frames
 * 1000-1480 every 5, differs between modes 4 and 1 by at most 0.3% of
 * pixels (rock-edge slivers); the truck stays whole.
 *
 * The RSP's own test, for the record (D:\Projects\007\rsp\graphics\
 * gmain.s:784-888): clip codes AND 0x7030 rejects, OR 0x4343 goes to the
 * clipper; otherwise the cross product of the three SCREEN positions (DMEM
 * +0x18, y down) in command order, negated once per swap of the y-sort,
 * masked with the mode's 0x1000/0x2000 bits and the sign of the hi word. A
 * constant-sense screen-area sign, which is what tri_sign_of computes in
 * y-up NDC with the sense fixed by the streets/dam captures below.
 *
 * The sense is expressed by choosing WHICH face to cull rather than by
 * glFrontFace, because the 2D path flips y in its own glOrtho
 * (mode2d_begin) and must not inherit a winding convention from the 3D path.
 * For the same reason 2D turns culling off outright: a texrect quad is wound
 * by the rectangle builder, not by the game.
 *
 * ---- What the captures said, on streets, dam, archives and Facility -------
 *
 * The bits are NOT decoration. g_geom_seen is 001f3205 on all four, cull-back
 * is set at triangle 0 and toggles all frame, and 91-98% of triangles are
 * emitted under it - including 127 of 128 wall-sized ones on streets and 394
 * of 412 on dam. The game backface-culls the world, not just its props.
 *
 * Sense 2 is wrong and obviously so: streets loses the road surface, the
 * fronts of every building and the guards' faces, leaving the level as a
 * hollow shell viewed from inside. Sense 1 leaves the scene intact.
 *
 * Sense 1 is nevertheless OFF by default, because it is not purely a fix.
 * It removes back faces that were bleeding through - on archives a flat grey
 * slab that occluded the left third of the roof truss disappears and the
 * trusses become continuous - but it also opens holes where this renderer has
 * nothing behind the face it drops: a wedge of the far wall on archives, a
 * wedge of hillside on dam, both showing through to sky or black. 0.4% of
 * pixels change on streets, dam and Facility, 6% on archives, in both
 * directions.
 *
 * ---- Where the depth buffer cannot substitute, and why that is the default -
 *
 * B-043. Ignoring the cull bits is HARMLESS for opaque geometry, because the
 * depth buffer removes the far side of a closed mesh anyway - a back face is
 * behind a front face and loses the compare. It stops being harmless the
 * moment a draw stops WRITING depth, which is exactly what the game asks for
 * when it fades a character out: chrRenderProp (chr.c:2993) switches the
 * player model to PROP_TYPE_EXPLOSION+1, and modelApplyRenderModeType3
 * (model.c:3547) binds G_RM_AA_ZB_XLU_SURF2 - Z_CMP set, Z_UPD CLEAR. The
 * near polygons then write no depth, the far ones pass the compare against
 * whatever is behind the model, and Bond's face draws over the back of his
 * own head. Measured on the owner's own recorded session, Facility, the frame
 * the report was filed against: 730 triangles, om c41049d8, zcmp=1 zupd=0
 * zmode=2, and 728 of the 730 emitted with G_CULL_BACK set. The game asks to
 * be culled there and this file was declining.
 *
 * So the default honours the bits on exactly the draws where depth cannot do
 * the job, and leaves every opaque draw exactly as it was. That is strictly
 * closer to the RSP than ignoring them outright and cannot regress anything
 * the depth buffer was already handling, which is the property that made it
 * shippable without a console to check against.
 *
 * ---- Why not sense 1 everywhere, which is what the hardware does ----------
 *
 * HISTORICAL (superseded by the #25 block above, 2026-09-15: sense 1 IS the
 * default now, and the objection below was never checked against the
 * cartridge; the 2026-09-15 intro sweep shows the truck whole under it).
 *
 * Because it is measurably wrong HERE, and a picture says so. dam, intro
 * flythrough, shot 16 of a 1000-frame boot: with sense 1 on every draw the
 * truck's near tyre loses most of its geometry - a full treaded wheel becomes
 * a thin dark crescent with the chassis grating showing through it. Whatever
 * that geometry is (a dotube cylinder is the obvious suspect - its vertices
 * are loaded under two different bone matrices, see g_veye), this renderer
 * does not yet wind it the way the RSP does. Facility, archives and streets
 * are near-clean under sense 1; dam is not, and one level is enough.
 *
 * The character-model objection that reverted this in B-040 is GONE and was
 * two separate mistakes. Its test frame had the camera INSIDE Bond's head, so
 * "the head is almost entirely gone" was culling working, not failing. And
 * its "mixed winding for characters, ccw=896 cw=658" is what a closed mesh
 * looks like from outside - front faces and back faces - not evidence of
 * inconsistency. The genuine inconsistency was elsewhere and is fixed: see
 * the OP_TRI1 citation, 14 backwards triangles out of 730, all from the BF
 * command, now 0. */
static int g_cull_opt = -1;
static int g_cull_gl;                    /* face currently culled, 0 = none */

/* SL_CULL=3 is a diagnostic, not a mode: it culls nothing and draws ONLY the
 * triangles sense 1 WOULD have dropped. Whether applying the bits is safe is
 * a question about what disappears, and a picture of exactly that set answers
 * it without inference. */
static int cull_option(void)
{
    if (g_cull_opt < 0) {
        /* Default 1: the full hardware reading, on every draw - the
         * cartridge agrees with it at both #25 witnesses (the block above).
         * 4 is B-043's former default (bits honoured only where the draw
         * writes no depth) and 0 ignores them entirely; both are kept for
         * bisecting. */
        const char *s = getenv("SL_CULL");
        g_cull_opt = (s != NULL && *s >= '0' && *s <= '5') ? *s - '0' : 1;
    }
    return g_cull_opt;
}

/* Signed window-space area of a triangle, from the eye positions already in
 * g_veye, without touching any of the NDC statistics. Only the sign is used. */
static int tri_sign_of(int a, int b, int c)
{
    int idx[3], i, j;
    float n[3][2];

    idx[0] = a; idx[1] = b; idx[2] = c;
    for (i = 0; i < 3; i++) {
        const float *e = g_veye[idx[i]];
        float cl[4];
        for (j = 0; j < 4; j++)
            cl[j] = e[0] * g_proj[j] + e[1] * g_proj[4 + j]
                  + e[2] * g_proj[8 + j] + g_proj[12 + j];
        if (cl[3] <= 1e-6f) return 0;
        n[i][0] = cl[0] / cl[3];
        n[i][1] = cl[1] / cl[3];
    }
    {   float ar = (n[1][0] - n[0][0]) * (n[2][1] - n[0][1])
                 - (n[1][1] - n[0][1]) * (n[2][0] - n[0][0]);
        return (ar > 0.0f) ? 1 : (ar < 0.0f) ? -1 : 0;
    }
}

/* Would sense 1 have dropped this triangle? Back faces are the clockwise ones
 * in this NDC; see the g_cull_n_ccw block. */
static int cull_would_drop(int a, int b, int c)
{
    int s = tri_sign_of(a, b, c);
    if (s == 0) return 0;
    if ((g_geom_mode & GEOM_CULL_BACK)  && s < 0) return 1;
    if ((g_geom_mode & GEOM_CULL_FRONT) && s > 0) return 1;
    return 0;
}

/* ---- The near-saturated fan: the one more place depth cannot substitute --
 *
 * B-043's rule, restated: ignoring the cull bits is harmless exactly when
 * the depth buffer would have removed the back face anyway, because on a
 * closed mesh the back face is BEHIND a front face and loses the compare.
 * Mode 4 already honours the bits on draws that write no depth, which is one
 * way that premise fails. The eye-crossing fan emitted by emit_tri is the
 * other: its sub-near region is saturated to z := -w - the near plane - so
 * it wins EVERY depth compare by construction, and a back face that reaches
 * this path is drawn over the whole scene no matter what stands in front of
 * it. Nothing can occlude it, so nothing but the cull bits can remove it.
 *
 * MEASURED, Silo intro swirl, 2026-09-14 (owner run 20260914-073701-lvl20,
 * mark-001, reproduced under SL_VI_CATCHUP=0 at pumped frames 1637-1647). The
 * frame-filling wall - a flat lit-wall pink over the whole viewport in 16 of
 * 55 shots of the swirl window - is ONE eye-crossing room triangle per frame
 * (f1645: v0 = (25.3,-17.6,-16.1, w 13.9) in front and off the right, v1 and
 * v2 behind the eye at w -71 / -94, G_CULL_BACK set, depth written). Its
 * guard polygon has two generated vertices at the clip-space origin
 * (w = 0.01), so its fan spans the whole NDC box [-2,2]^2, and every fan
 * triangle is CLOCKWISE - a back face. The cartridge, which culls it, draws
 * Bond, the catwalk and the far wall through where it would have been
 * (tools/native/romintro.py ticks 1484-1494). The camera has passed to the
 * far side of that wall's plane; what fills the screen is its back.
 *
 * The triangles the owner's cartridge test proved must FILL (ad772496,
 * mark-003 v0 w 5.4 / v1 w -1.3 / v2 w 4.6; mark-004 19.6 / -7.7 / 4.2) are
 * walls the PLAYER FACES - front faces by definition - so this rule keeps
 * them by construction: their fan winds counter-clockwise and is emitted
 * exactly as before. Only the fan changes: ordinary opaque draws still go
 * through cull_apply untouched, so the Dam truck-tyre objection to sense 1
 * everywhere does not arise here.
 *
 * The facing is well defined on the fan - every vertex has w > 0 after the
 * guard, and Sutherland-Hodgman preserves winding, so the fan's screen
 * winding IS the projective winding of the w > 0 part of the source
 * triangle, which is also what the RSP sees after its own near retessellation
 * (the two polygons are portions of the same projective image). The sense
 * follows SL_CULL so a bisect still means one thing: 0 culls nothing here
 * either; 2 is the inverse reading; 1, 4 and 5 are the hardware sense; 3 is
 * emit_tri_slots' own diagnostic and is left to it. SL_NEARCULL=0 is the
 * kill switch for this rule alone. */
static unsigned g_nearcull_dropped;       /* fan triangles this rule removed */

static int nearcull_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_NEARCULL");
                  on = !(v != NULL && *v == '0'); }
    return on;
}

static int nearcull_would_drop(int a, int b, int c)
{
    int opt = cull_option(), s;
    if (!nearcull_on() || opt == 0 || opt == 3) return 0;
    s = tri_sign_of(a, b, c);
    if (s == 0) return 0;
    if (opt == 2) s = -s;
    if ((g_geom_mode & GEOM_CULL_BACK)  && s < 0) return 1;
    if ((g_geom_mode & GEOM_CULL_FRONT) && s > 0) return 1;
    return 0;
}

/* ---- B-100 WITNESS. The near plane: who gets dropped, and by whom? -------
 *
 * THE QUESTION THIS EXISTS TO ANSWER, AND THE ANSWER, WHICH IS "NO". This
 * block used to argue that the two sides clip against DIFFERENT sets of
 * planes: GL tests clip-space z against -w, while the G_MW_CLIP moveword
 * offers only `clip -x` / `clip -y` / `clip +x` / `clip +y` and no z entry
 * (ucode05.txt:567-570, identically ucode05_old.txt:483-486). That argument
 * is WRONG, and the error is worth naming because it was made twice. What
 * G_MW_CLIP enumerates is the PROGRAMMABLE planes - the ones a display list
 * can move. It says nothing about a FIXED near-z test compiled into the
 * microcode, and there is one.
 *
 * THE AUTHORITY IS THE CARTRIDGE'S OWN RSP MICROCODE, read 2026-09-09:
 * D:\Projects\007\rsp\graphics\gmain.s. Two sites settle it.
 *
 *   gmain.s:512-524, in the vertex routine. A vch/vcl pair compares the
 *   clip-space vector against its own unscaled w, cfc2 reads $vcc, and the
 *   result is masked with 0x703 for one vertex and 0x7030 for the other.
 *   In the F3DEX $vcc layout the low byte holds `coord <= +w` per lane and
 *   the high byte `coord >= -w`, lanes 0-2 being x,y,z. 0x703 therefore
 *   takes x,y from the POSITIVE side and x,y,AND Z from the NEGATIVE side:
 *   five planes, no far test, and the fifth is z >= -w - the near plane.
 *   (The asymmetry is itself the cross-check: it reproduces the N64's
 *   well-known absence of far clipping. Read with the bytes the other way
 *   round the microcode would far-clip and not near-clip, which the hardware
 *   demonstrably does not do.) A second vch/vcl pair against a scaled w
 *   supplies the clip-ratio codes for x and y only.
 *
 *   gmain.s:784-794, in the triangle routine. AND of the three vertices'
 *   codes masked with 0x7030 rejects the triangle outright; OR of them
 *   masked with 0x4343 sends it to the clipper. 0x7030 is the unscaled set
 *   above, near included. 0x4343 is scaled +-x, +-y plus bit 14 - which is
 *   the unscaled near bit, shifted into place by the `sll $8, $8, 4` at
 *   gmain.s:519. So the RSP both REJECTS on and RETESSELLATES against the
 *   near plane, at z = -w exactly, the same inequality GL applies.
 *
 * Equal znear plus equal matrices therefore IS equal consumed near clipping.
 * That agrees with the cartridge-paired truck measurement recorded in the
 * B-100 depth-clamp block below, which found the unclamped build and the
 * cartridge within 0.2 points on the see-through band.
 *
 * WHAT THIS CENSUS ACTUALLY COUNTS, and why its all-zero result was not the
 * exoneration it looked like. The classes are exhaustive and exclusive:
 *   FRONT     every vertex at or beyond the near plane. Both sides draw it.
 *   STRADDLE  crosses the near plane. Both sides clip and draw the far part.
 *   INSIDE    wholly nearer than the near plane, wholly in front of the eye.
 *             Both sides drop it.
 *   BEHIND    at least one vertex at or behind the eye. Both sides clip.
 * INSIDE was where the divergence was expected and it is empty on the whole
 * recorded Dam stream. But the owner's close-wall witnesses do not live
 * there: every one of them is BEHIND (a wall running past the camera has a
 * vertex behind the eye), and this function returns on nbehind != 0 before
 * it can see them. An all-zero INSIDE count says nothing about them.
 * Measured on run 20260909-100313-lvl33 - see docs/backlog.md B-100.
 *
 * area_inside is the window-space coverage the INSIDE class would have had,
 * summed, computed from x/w and y/w - which are well defined for every member
 * of that class precisely because w > 0 there. It is what turns "12 triangles
 * were dropped" into "a wall's worth of pixels was dropped", and without it a
 * count cannot tell a sliver at the screen edge from a see-through hole.
 *
 * Reports only. Set SL_NEARWIT=1. */
static unsigned g_scr_w, g_scr_h;      /* tentative; the real one is below */
static unsigned g_nw_front, g_nw_straddle, g_nw_inside, g_nw_behind;
static double   g_nw_area_inside, g_nw_area_straddle;
static float    g_nw_worst;            /* largest single INSIDE triangle, px */
static int nearwit_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_NEARWIT");
                  on = (v != NULL && *v != '\0' && *v != '0'); }
    return on;
}
static void nearwit_note(int a, int b, int c)
{
    int idx[3], i, j, nfront = 0, nnear = 0, nbehind = 0;
    float n[3][2];

    if (!nearwit_on()) return;
    idx[0] = a; idx[1] = b; idx[2] = c;
    for (i = 0; i < 3; i++) {
        const float *e = g_veye[idx[i]];
        float cl[4];
        for (j = 0; j < 4; j++)
            cl[j] = e[0] * g_proj[j] + e[1] * g_proj[4 + j]
                  + e[2] * g_proj[8 + j] + g_proj[12 + j];
        if (cl[3] <= 1e-6f) { nbehind++; n[i][0] = n[i][1] = 0.0f; continue; }
        n[i][0] = cl[0] / cl[3];
        n[i][1] = cl[1] / cl[3];
        /* GL's own near test, spelled out rather than inferred from a znear:
         * whatever g_proj encodes, this is the comparison GL will make. */
        if (cl[2] < -cl[3]) nnear++; else nfront++;
    }
    if (nbehind != 0)      { g_nw_behind++; return; }
    if (nnear == 0)        { g_nw_front++;  return; }
    {   float ax = (n[1][0] - n[0][0]) * (float) g_scr_w * 0.5f;
        float ay = (n[1][1] - n[0][1]) * (float) g_scr_h * 0.5f;
        float bx = (n[2][0] - n[0][0]) * (float) g_scr_w * 0.5f;
        float by = (n[2][1] - n[0][1]) * (float) g_scr_h * 0.5f;
        float ar = (ax * by - ay * bx) * 0.5f;
        if (ar < 0.0f) ar = -ar;
        if (nfront != 0) { g_nw_straddle++; g_nw_area_straddle += (double) ar; }
        else {
            g_nw_inside++; g_nw_area_inside += (double) ar;
            if (ar > g_nw_worst) g_nw_worst = ar;
        }
    }
}

static void cull_set(int face)
{
    if (face == g_cull_gl) return;
    batch_end();                    /* glEnable is illegal inside glBegin */
    if (face == 0) glDisable(GL_CULL_FACE);
    else {
        if (g_cull_gl == 0) glEnable(GL_CULL_FACE);
        glCullFace((GLenum) face);
    }
    g_cull_gl = face;
}

static void cull_apply(int zwrite)
{
    int opt = cull_option(), f, b, face = 0;

    /* Mode 4 is sense 1 restricted to draws that do not write depth, and
     * `zwrite` is that bit read out of the render mode by the caller. It is
     * passed in rather than read here because this function is defined ahead
     * of RM_Z_UPD and g_om_l, and because emit_tri calls cull_apply BEFORE
     * rm_apply - both must be able to close the batch before glBegin, so the
     * GL depth mask is not yet established for this triangle either. */
    /* Mode 4 is B-043's rule: honour the bits only where depth cannot
     * stand in for them (no z write). B-113 briefly extended the DEFAULT to
     * room geometry to cull the Dam mountain's interior wedge - and B-114
     * retreated it after the owner's replay: the same rooms lose MOST of
     * the massif under sense 1 from other viewpoints, and sense 2 loses
     * the complementary subset (measured at owner marks 20260910-230901
     * and 20260910-234448: each sense culls faces the other keeps, on one
     * continuous mountain). B-114 read that as a MIXED authored winding
     * that no constant rule could honour. #25 (2026-09-15) measured the
     * missing half: the cartridge, at the same room-121 poses, culls the
     * faces sense 1 culls - the "lost" massif was its interior, which
     * mode 4 had been drawing over the sky. Sense 2's complementary loss
     * is simply sense 2 being wrong. So the default is now 1; modes 4 and
     * 5 survive only as bisect arms. */
    if (opt == 5) opt = (zwrite && !g_room_dl) ? 0 : 1;
    else if (opt == 4) opt = zwrite ? 0 : 1;

    if (opt == 1 || opt == 2) {
        f = (g_geom_mode & GEOM_CULL_FRONT) != 0;
        b = (g_geom_mode & GEOM_CULL_BACK)  != 0;
        if (opt == 2) { int t = f; f = b; b = t; }
        if (f && b)   face = GL_FRONT_AND_BACK;
        else if (f)   face = GL_FRONT;
        else if (b)   face = GL_BACK;
    }
    cull_set(face);
}

/* Cycle type, from BA setothermode_h at G_MDSFT_CYCLETYPE. gbi.h's codes:
 * 0 = 1cycle, 1 = 2cycle, 2 = copy, 3 = fill. It decides where a FILLRECT
 * gets its colour, which is not a detail: the RDP takes the fill-colour
 * register only in FILL mode, and puts a 1-cycle rectangle through the colour
 * combiner like any other primitive. GoldenEye leans on exactly that -
 * draw_blackbox_to_screen (textrelated.c:181) sets 1-cycle XLU,
 * G_CC_PRIMITIVE and prim = 00000000, then fills the panel behind menu text.
 * Read as a fill-colour rectangle that is an opaque 320x221 slab over the
 * world; read through the combiner it is the transparent scrim it actually
 * is. This tree's cycle type is set by microcode_constructor
 * (textrelated.c:150), and the packing was already pinned by the notes' own
 * worked example "BA001402 00100000  ;2 cycle" - see the setothermode
 * citation in the texture block.
 *
 * g_cycle_type itself is DECLARED up with tex1_apply, which is above this
 * block and needs it - the same reason g_cc_texel and g_cc_w0/w1 live up
 * there rather than with the rest of the combiner state. */

/* ---- B9 setothermode_l: the RDP render mode ------------------------------
 *
 * B-026. Every surface the RDP would composite was being written opaque,
 * because the 3D path drew with GL_BLEND off unconditionally. The mode that
 * says otherwise is the low othermode register, written by B9.
 *
 * WHERE THE LAYOUT COMES FROM. ucode05.txt punts B9 to a SETOTHERMODE_L.HTM
 * that is not in the corpus - the same hole F5 settile and FC setcombine
 * have, and the same fallback closes it: ucode05_old.txt carries the table in
 * full (its "B9 rsp_uc05_setothermode_l" entry, l.296-350).
 *
 *   upper word  0000FF00 shift, 000000FF length, both plain - the note marks
 *               the F3DEX2 forms (32-shift-length, length-1) "PD only".
 *               Segment shifts: 0 alpha compare (len 2), 2 depth source
 *               (len 1), 3 render mode (len 1D), 10 blender (len 10).
 *   lower word  alpha compare 0-1; depth source 2; then the render mode:
 *               08 antialias, 10 depth compare, 20 depth update, 40 image
 *               read, 80 clear on converge, 100/200/300 the converge-ST
 *               (coverage) destination, 400/800/C00 depth interpolate /
 *               exclusive / decrement, 1000 converge x alpha, 2000 alpha
 *               converge selection, 4000 force blender, 8000 texture edge;
 *               and the blender itself in 16-31 as four 2-bit muxes twice
 *               over, one set for each cycle.
 *
 * CROSS-CHECKED THREE WAYS, because the note alone would not have been enough
 * to trust the blender half - its 10000000/20000000/40000000/80000000 rows
 * repeat the "blend 1 machine / alpha memory / 1 / 0" names that belong to the
 * B mux, in bit positions that are the P mux.
 *
 *   1. include/PR/gbi.h's G_MDSFT_ALPHACOMPARE/ZSRCSEL/RENDERMODE/BLENDER are
 *      0/2/3/16 with gsDPSetRenderMode passing length 29 - the note's
 *      0/2/3/10 and 1D exactly.
 *   2. gbi.h's GBL_c1(m1a,m1b,m2a,m2b) = m1a<<30|m1b<<26|m2a<<22|m2b<<18 and
 *      GBL_c2 = m1a<<28|m1b<<24|m2a<<20|m2b<<16 put the B mux at 16-17 (c2)
 *      and 18-19 (c1), the M mux at 20-21 and 22-23, the A mux at 24-25 and
 *      26-27 - which is where the note's own value rows land.
 *   3. A worked example inside THIS tree settles both halves at once.
 *      src/boss.c:588 carries the assembled words in a comment:
 *          gDPSetRenderMode(gdl++, G_RM_VISCVG, G_RM_VISCVG2);
 *                                          // 0xb900031d, 0x0fa54040
 *      w0 = B9 | sft 03 | len 1D is the render-mode segment, plainly packed.
 *      And RM_VISCVG(clk) = IM_RD | FORCE_BL | GBL_c##clk(G_BL_CLR_IN,
 *      G_BL_0, G_BL_CLR_BL, G_BL_A_MEM) evaluates, under the packing above,
 *      to 0x0C840000 | 0x03210000 | 0x40 | 0x4000 = 0x0FA54040. It lands on
 *      the nose, so the shift/length packing and the blender mux positions
 *      are measured against real assembled data, not assumed.
 *
 * WHAT MAPS TO WHAT. The blender is not a fixed equation; it is
 * out = P*A + M*B over four muxes. GL's is src*sfac + dst*dfac, so a mode is
 * only expressible when exactly one of P and M names the framebuffer:
 *
 *   P=CLR_IN, M=CLR_MEM  ->  sfac from A, dfac from B      (every XLU mode)
 *   P=CLR_MEM, M=CLR_IN  ->  the same pair, swapped        (debugmenu.c:48)
 *   otherwise            ->  the incoming colour wins outright; no blend.
 *
 * That last line is why the alpha-tested foliage that works today keeps
 * working: RM_TEX_EDGE is GBL_c(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1),
 * both colour muxes CLR_IN, so out = CLR_IN and this path leaves it alone.
 * And FORCE_BL gates the whole thing: without it the RDP runs the blender
 * only on partially-covered edge pixels, which is what makes G_RM_*OPA*
 * opaque in the first place.
 */
/* ---- Alpha compare, othermode_l bits 0-1 (gbi.h:596 G_MDSFT_ALPHACOMPARE,
 * gbi.h:681-683). The RDP tests the fragment's alpha before the blender:
 * NONE keeps every pixel, THRESHOLD compares against the blend colour's
 * alpha, DITHER compares against a per-pixel PSEUDO-RANDOM value - so a
 * surface at alpha a keeps roughly a/255 of its pixels, scattered, and
 * re-scattered every frame. That is the watch's static, and it is ordinary
 * RDP state rather than anything watch-specific. */
#define RM_AC_MASK       0x00000003u
#define AC_NONE          0u
#define AC_THRESHOLD     1u
#define AC_DITHER        3u

#define RM_AA_EN         0x00000008u
#define RM_Z_CMP         0x00000010u
#define RM_Z_UPD         0x00000020u
#define RM_IM_RD         0x00000040u
#define RM_CLR_ON_CVG    0x00000080u
#define RM_ZMODE         0x00000c00u
#define RM_ZMODE_OPA     0x00000000u
#define RM_ZMODE_INTER   0x00000400u
#define RM_ZMODE_XLU     0x00000800u
#define RM_ZMODE_DEC     0x00000c00u
#define RM_CVG_X_ALPHA   0x00001000u
#define RM_ALPHA_CVG_SEL 0x00002000u
#define RM_FORCE_BL      0x00004000u

/* Colour muxes (P and M): 0 CLR_IN, 1 CLR_MEM, 2 CLR_BL, 3 CLR_FOG. */
#define BL_CLR_IN   0u
#define BL_CLR_MEM  1u

/* The register itself. It is hardware state that persists across commands,
 * so it is set and cleared field-wise, exactly like the geometry mode. It is
 * seeded each frame with the state this renderer used BEFORE the mode was
 * read at all - depth compare and depth update on, blender off - so a list
 * that draws before its first B9 renders as it did yesterday rather than
 * under an invented default. g_rm_pre_tris counts exactly that case. */
#define RM_SEED (RM_Z_CMP | RM_Z_UPD | RM_ZMODE_OPA | RM_AA_EN | RM_IM_RD)
static unsigned g_om_l = RM_SEED;
static unsigned g_om_l_cmds, g_om_l_bad, g_rm_pre_tris;

/* Distinct (render mode, cycle type) pairs actually drawn with, and how many
 * triangles each took. Reading the decomp says which G_RM_* constants are
 * spelled; only this says which ones reach geometry. */
#define RM_HIST_MAX 24
static unsigned g_rm_hist_key[RM_HIST_MAX], g_rm_hist_cyc[RM_HIST_MAX];
static unsigned g_rm_hist_n[RM_HIST_MAX], g_rm_hist_used, g_rm_hist_lost;
static unsigned g_rm_blend_tris, g_rm_decal_tris, g_rm_nozwrite_tris;

/* Cached GL state. -1 is "not established", which is what a frame boundary or
 * a trip through the 2D ortho leaves behind. */
static int g_rm_blend = -1, g_rm_sfac, g_rm_dfac;
static int g_rm_ztest = -1, g_rm_zwrite = -1, g_rm_zoff = -1;
static int g_rm_atest = -1;      /* B-121: GL_ALPHA_TEST for the tree family */
static unsigned g_tree_zupd_skip; /* B-140: detail-blend draws whose authored
                                   * mode writes z - the override leaves them
                                   * alone; per frame, census */

/* B-122 kill switch. SL_Z_LEQUAL=0 restores GL's default GL_LESS, under
 * which an equal-depth coplanar redraw is rejected - the checkered-dome
 * state - so the two semantics can be captured back to back. Default on. */
static int zfunc_lequal_on(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_Z_LEQUAL");
                  on = (s == NULL || *s != '0'); }
    return on;
}

/* B-121 kill switch. SL_TREE_ZWRITE=0 restores the cartridge's order-only
 * resolution (z-write off) for the treeline family, so the far-over-near
 * overlap can be captured back to back against the depth-resolved fix. */
static int tree_zwrite_on(void)
{
    static int on = -1;
    if (on < 0) { const char *s = getenv("SL_TREE_ZWRITE");
                  on = (s == NULL || *s != '0'); }
    return on;
}

static void rm_invalidate(void)
{
    g_rm_blend = g_rm_ztest = g_rm_zwrite = g_rm_zoff = g_rm_atest = -1;
}

/* The A mux - the alpha multiplying P. 0 A_IN, 1 A_FOG, 2 A_SHADE, 3 zero.
 * A_IN is the combiner's alpha and A_SHADE the vertex's; this path feeds the
 * shade alpha through as the fragment alpha, so both land on GL_SRC_ALPHA.
 * Fog alpha is not modelled - there is no fog stage here yet - and taking the
 * fragment alpha for it is closer than taking one. */
static int rm_a_factor(unsigned a)
{
    if (a == 3u) return GL_ZERO;
    return GL_SRC_ALPHA;
}

/* The B mux - the alpha multiplying M. 0 1MA, 1 A_MEM, 2 one, 3 zero.
 * A_MEM is the framebuffer's own alpha, which this backend does not keep (the
 * GL context is not asked for a destination alpha channel), so it falls to
 * 1-alpha: every GoldenEye mode that pairs A_MEM with FORCE_BL also names a
 * colour mux this path declines to blend, so nothing reaching here uses it. */
static int rm_b_factor(unsigned b)
{
    switch (b) {
    case 0u:  return GL_ONE_MINUS_SRC_ALPHA;   /* 1MA */
    case 1u:  return GL_ONE_MINUS_SRC_ALPHA;   /* A_MEM, approximated */
    case 2u:  return GL_ONE;                   /* 1 */
    default:  return GL_ZERO;                  /* 0 */
    }
}

/* Note the mode this triangle is about to draw under. */
static void rm_hist_note(unsigned om)
{
    unsigned i;
    for (i = 0; i < g_rm_hist_used; i++)
        if (g_rm_hist_key[i] == om && g_rm_hist_cyc[i] == g_cycle_type) {
            g_rm_hist_n[i]++;
            return;
        }
    if (g_rm_hist_used >= RM_HIST_MAX) { g_rm_hist_lost++; return; }
    i = g_rm_hist_used++;
    g_rm_hist_key[i] = om;
    g_rm_hist_cyc[i] = g_cycle_type;
    g_rm_hist_n[i]   = 1;
}

/* Establish blending and depth behaviour for the triangle about to be drawn.
 * Every glEnable here is illegal between glBegin and glEnd, so a state change
 * closes the batch first - the same contract tex_apply and cull_apply keep. */
static void rm_apply(void)
{
    unsigned om = g_om_l;
    int two = (g_cycle_type == 1);        /* G_CYC_2CYCLE */
    unsigned p, a, m, b;
    int blend = 0, sfac = GL_ONE, dfac = GL_ZERO;
    int ztest, zwrite, zoff;

    if (g_om_l_cmds == 0) g_rm_pre_tris++;
    rm_hist_note(om);

    /* Which blender cycle produces the pixel. In 2-cycle mode the second one
     * is the last to run and so is the one that reaches memory; GoldenEye
     * leans on this - 45 sites spell G_RM_FOG_PRIM_A in the first slot and
     * the real surface mode in the second (`grep -rn G_RM_FOG_PRIM_A src/`). */
    if (two) {
        p = (om >> 28) & 3u; a = (om >> 24) & 3u;
        m = (om >> 20) & 3u; b = (om >> 16) & 3u;
    } else {
        p = (om >> 30) & 3u; a = (om >> 26) & 3u;
        m = (om >> 22) & 3u; b = (om >> 18) & 3u;
    }

    /* B-045 probe. The RDP's fog stage lives in the blender: a cycle whose P
     * mux names CLR_FOG (3) and whose M mux names the incoming pixel is a fog
     * blend, and the A mux says where its factor comes from - A_SHADE (2) is
     * the RSP's per-vertex fog value, A_FOG (1) the fog colour's own alpha. */
    {   /* Both cycles, not just the one that reaches memory: GoldenEye puts
         * its fog blend in slot ONE (cycle 0) and the surface mode in slot
         * two (bg.c:2105 G_RM_FOG_SHADE_A, model.c:3527 G_RM_FOG_PRIM_A), so
         * reading only the producing cycle sees no fog anywhere. */
        unsigned fp = (om >> 30) & 3u, fa = (om >> 26) & 3u;
        if (fp == 3u) {
            g_fog_p_tris++;
            if (fa == 2u) g_fog_a_shade++;
            else if (fa == 1u) g_fog_a_fog++;
        }
    }
    if (g_geom_mode & 0x00010000u) {                    /* gbi.h:363 G_FOG */
        g_fog_geom_tris++;
        if (!g_fog_have_at) {
            float m10 = g_proj[10], m14 = g_proj[14];
            g_fog_have_at = 1;
            memcpy(g_fog_col_at, g_fog, 4);
            g_fog_proj_at[0] = g_proj[10]; g_fog_proj_at[1] = g_proj[11];
            g_fog_proj_at[2] = g_proj[14]; g_fog_proj_at[3] = g_proj[15];
            g_fog_near_at = (m10 - 1.0f) != 0.0f ? m14 / (m10 - 1.0f) : 0.0f;
            g_fog_far_at  = (m10 + 1.0f) != 0.0f ? m14 / (m10 + 1.0f) : 0.0f;
        }
    }

    if (om & RM_FORCE_BL) {
        if (p == BL_CLR_IN && m == BL_CLR_MEM) {
            blend = 1; sfac = rm_a_factor(a); dfac = rm_b_factor(b);
        } else if (p == BL_CLR_MEM && m == BL_CLR_IN) {
            blend = 1; sfac = rm_b_factor(b); dfac = rm_a_factor(a);
        }
        /* Anything else - both muxes CLR_IN (TEX_EDGE, PCL), or either naming
         * the blend/fog colour (VISCVG) - resolves to the incoming colour
         * winning, which is what an unblended draw already does. */
    }
    if (blend && sfac == GL_ONE && dfac == GL_ZERO) blend = 0;

    ztest  = (om & RM_Z_CMP) != 0;
    zwrite = (om & RM_Z_UPD) != 0;
    /* B-121. THE TREELINE DETAIL-BLEND FAMILY RESOLVES BY DEPTH, NOT ORDER.
     * Surface's perimeter is layered concentric tree-wall rows in different
     * rooms, all in this XLU family with Z_UPD off (om c81049d8). With no
     * z-write, two overlapping rows resolve by submission order, and the
     * secondary pass draws by portal DEPTH which is not world distance for
     * these rooms - so a FAR row (room 9, ndc z 0.993) submitted after a
     * NEAR row (room 10, ndc z 0.986) blends OVER it (owner: "the treeline
     * in the background has a wrong z-plane and overlaps the treeline in
     * front"). The geometry carries correct relative depth; only the WRITE
     * is off. Giving this family a z-write makes overlap resolve by that
     * true depth, order-independent. Scoped to the c=13 family (water is
     * c=14, untouched) and kill-switched. The family's alpha is near-binary
     * (RGBA16 5551 strip), so the transparent sky-gap pixels this would
     * otherwise stamp into z are cut by a matching alpha test rather than
     * occluding the row behind the gaps.
     *
     * B-129. THE FAMILY IS TWO SHAPES AND ONLY ONE OF THEM IS THE TREELINE.
     * B-119 split the c=13 family into the two-image detail blend (the
     * treeline: mode 9, the strip's alpha) and one image plus its authored
     * mips (dome shell, snow ground, and every first-person weapon - the
     * viewmodel is 6-level CI/I chains under G_CC_TRILERP, B-034). This
     * override used to take the whole family, so the sniper rifle's
     * eyepiece - an I4 lens whose alpha IS its intensity, drawn under the
     * authored c4112048 (AA opaque, no z) - had its dark texels alpha-tested
     * away and the snow behind the gun shone through the scope (Gitea #17:
     * "the scope seems to be missing textures"). The cartridge draws it
     * opaque black: in OPA_SURF modes ALPHA_CVG_SEL replaces the pixel alpha
     * with the coverage, so the texel alpha never reaches the blender. The
     * z-write and the alpha test belong to the strip alone; the one-image
     * variant keeps its authored mode. Measured: eyepiece upper interior mean
     * 62 (p90 107) -> the cartridge's 0..40 band; SL_CC_LODLERP=0 had already
     * put it at 18, which is how the override was found. */
    {
        int treefam = g_cc_tex1lodlerp && cc_lodlerp_on() && cc_tex1_on()
                      && g_cycle_type == 1 && tree_zwrite_on()
                      && !g_lodmip_draw;
        int atest = 0;
        /* B-140. THE OVERRIDE IS FOR THE Z-WRITE-OFF STRIP ONLY. Its whole
         * premise is "the geometry carries correct relative depth; only the
         * WRITE is off": the z-write it adds replaces order with depth, and
         * the alpha test exists solely so the transparent gaps of a strip
         * that never wrote z do not start occluding the row behind them.
         * Neither has anything to say to a draw whose AUTHORED mode already
         * writes z. Dam's cliffs are the same two-image detail blend (tile 0
         * the 64x64 detail, tile 1 the far image, fc26e404 1ffcfffc, mode 9)
         * under the OPAQUE c8102078 - Z_CMP|Z_UPD, no alpha-compare bits -
         * and mode 9 emits the far image's own alpha, so every texel whose
         * far-image alpha fell under the 0.06 reference was discarded and
         * the fog-coloured backdrop showed through the mountain (owner:
         * "large holes expose the blue sky"; intro frame 1220 under
         * SL_VI_CATCHUP=0 and every room-121 pose, 2026-09-16; the cartridge
         * at the same frame draws the cliff whole - om_l 78 carries no
         * G_AC_THRESHOLD, so the RDP never compares alpha there). Keyed on
         * the render-mode bit the override was written against, never on a
         * level: an authored z-writing draw keeps its authored mode. The
         * treeline (c81049d8, Z_UPD off) is unchanged. The excluded
         * population is counted per frame (sl_cc census line). */
        if (treefam && (om & RM_Z_UPD)) { g_tree_zupd_skip++; treefam = 0; }
        if (treefam) { zwrite = 1; atest = 1; }
        if (atest != g_rm_atest) {
            batch_end();
            if (atest) {
                glEnable(GL_ALPHA_TEST);
                /* Cut only the strip's transparent SKY. The strip is RGBA16
                 * 5551 - alpha is 1-bit at source, softened to a fraction
                 * only by GL_LINEAR at the tree edges - so a low reference
                 * rejects the sky (alpha ~0) while keeping every graded edge
                 * texel, leaving the owner-accepted soft treeline look intact
                 * and removing only the z-stamping of the transparent gaps
                 * that would otherwise occlude the row behind them. */
                glAlphaFunc(GL_GREATER, 0.06f);
            } else {
                glDisable(GL_ALPHA_TEST);
            }
            g_rm_atest = atest;
        }
    }
    /* ZMODE. The RDP's decal and interpenetrate modes compare against the
     * framebuffer z with a tolerance rather than strictly; a polygon offset
     * is the standard stand-in. Negative pulls toward the viewer, which is
     * what lets a decal win against the surface it is lying on - aztec's
     * floor marking is exactly this case. */
    switch (om & RM_ZMODE) {
    case RM_ZMODE_DEC:   zoff = 2; break;
    case RM_ZMODE_INTER: zoff = 1; break;
    default:             zoff = 0; break;
    }

    if (blend != g_rm_blend || (blend && (sfac != g_rm_sfac || dfac != g_rm_dfac))) {
        batch_end();
        if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc((GLenum) sfac, (GLenum) dfac);
        } else {
            glDisable(GL_BLEND);
        }
        g_rm_blend = blend; g_rm_sfac = sfac; g_rm_dfac = dfac;
    }
    if (ztest != g_rm_ztest) {
        batch_end();
        if (ztest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        g_rm_ztest = ztest;
    }
    if (zwrite != g_rm_zwrite) {
        batch_end();
        glDepthMask(zwrite ? GL_TRUE : GL_FALSE);
        g_rm_zwrite = zwrite;
    }
    if (zoff != g_rm_zoff) {
        batch_end();
        if (zoff) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(zoff == 2 ? -1.0f : -0.5f,
                            zoff == 2 ? -2.0f : -1.0f);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        g_rm_zoff = zoff;
    }

    if (blend)  g_rm_blend_tris++;
    if (zoff)   g_rm_decal_tris++;
    if (!zwrite) g_rm_nozwrite_tris++;
}

/* ---- B-045: the RDP's fog stage --------------------------------------
 *
 * GoldenEye's "gas" is fog. There is no gas geometry anywhere: releasing the
 * facility tanks calls init_trigger_toxic_gas_effect (propobj.c:14121), which
 * makes handle_gas_damage (propobj.c:14154) call fogSwitchToSolosky2 every
 * tick, and that lerps the level's environment record toward its ALT row
 * (bgfog.c:511). For facility those two rows are
 *
 *   LEVELID_FACILITY                        near 10  far 5000  sky 10,20,10
 *   LEVELID_FACILITY + ENVIRONMENTDATA_ALT  near 10  far 1000  sky 40,80,40
 *
 * (bgfog.c:131-132), so the whole visual is: the fog colour becomes green,
 * the backdrop fill becomes the same green (sky.c:326 fills the viewport with
 * the environment colour when the level has no clouds) and the far clip plane
 * comes in from 5000 to 1000. Fog is what hides that clip - geometry reaches
 * full fog colour exactly where it is cut off.
 *
 * This renderer had no fog stage at all, so the fade was missing and the cut
 * was not: near geometry drew at full brightness, everything past the plane
 * was replaced by the flat green fill, and the boundary tracked the camera.
 * That is the owner's report, verbatim - "a single layer that moves forward
 * and back as I move towards it" - and it is a hard edge, not a volume.
 *
 * ---- what the list asks for, measured on the gassed room ---------------
 *
 *   sl_fog: f301 fill=428442 fogcol=408040ff fm=12800 fo=-12544 cmds=5
 *           fog-geom=1391 clr-fog=2431 a_shade=1391 a_fog=1040
 *
 * 1391 triangles a frame carry the G_FOG geometry bit and a blender whose
 * first cycle is (CLR_FOG, A_SHADE, CLR_IN, 1MA) - G_RM_FOG_SHADE_A, which
 * bg.c:2105-2143 puts in slot one of every room render mode - and 1040 more
 * carry (CLR_FOG, A_FOG, CLR_IN, 1MA), G_RM_FOG_PRIM_A, which model.c uses
 * for characters and props with a per-model constant in the fog colour's
 * alpha. Both were dropped: rm_apply reads only the cycle that reaches
 * memory, which in 2-cycle mode is the SECOND, and GoldenEye's fog always
 * sits in the first.
 *
 * ---- the factor -------------------------------------------------------
 *
 * gbi.h's own comment above gSPFogFactor gives the formula the RSP applies:
 * "alpha(fog) = (eyespace z) * fm + fo CLAMPED 0 to 255", with that z running
 * -1 to 1 - i.e. the perspective-divided z, not a distance. gSPFogPosition
 * (gbi.h:2777) packs fm = 128000/(max-min) and fo = (500-min)*256/(max-min),
 * so writing p for the normalised depth (z+1)/2 the two compose to
 *
 *     fog = 256 * (1000p - min) / (max - min)
 *
 * which is 0 at p = min/1000 and saturates at p = max/1000, exactly what the
 * macro's "range 0 to 1000: 0=nearplane, 1000=farplane" says. The measured
 * fm/fo above are 128000/10 and (500-990)*256/10, i.e. min=990 max=1000: the
 * fog band is the last one percent of the DEPTH BUFFER, which because that
 * buffer is 1/w is the outer HALF of the world - with the gas's 10..1000
 * planes, p=0.99 is 502 world units out.
 *
 * ---- how it is drawn --------------------------------------------------
 *
 * GL's fixed-function fog with an explicit per-vertex fog coordinate is the
 * same operation: under GL_LINEAR with start 0 and end 1 the factor is
 * f = 1 - coord and the fragment becomes f*C + (1-f)*Cfog, so handing it the
 * RSP's own value as the coordinate reproduces (CLR_FOG, A, CLR_IN, 1MA)
 * term for term. It applies after texturing and before the framebuffer
 * blend, which is where the RDP's first blender cycle sits relative to its
 * second, and it leaves alpha alone, which is what the blender does too.
 *
 * Deliberately narrow, the same discipline as cc_tint (B-046) and the TEXEL1
 * product (B-048): ONLY the two mux shapes above are claimed, only in
 * 2-cycle mode - in 1-cycle the first slot IS the surface blend and no fog
 * is meant - and A_SHADE additionally requires the G_FOG geometry bit, since
 * without it the RSP never computed a fog value to put in the shade alpha.
 * SL_FOG=0 restores the old behaviour from the same binary.
 *
 * glFogCoordf is core GL 1.4 and the window reports a 4.5 compatibility
 * profile; where a header predates it the feature compiles out and the gas
 * keeps drawing as it did, which is the behaviour before this change rather
 * than a new failure. */
#if defined(SL_GL_RUNTIME_POST11) \
    || (defined(GL_FOG_COORD_SRC) && defined(GL_FOG_COORD) && defined(GL_VERSION_1_4))
#define SL_FOGSTAGE 1
#endif

#define FOG_NONE   0
#define FOG_SHADE  1        /* A_SHADE - the RSP's per-vertex fog value  */
#define FOG_CONST  2        /* A_FOG   - the fog colour's own alpha      */

static int      g_fog_gl_on;
static unsigned char g_fog_gl_col[4];
static unsigned g_fog_tris_shade, g_fog_tris_const;
static unsigned g_fog_enables, g_fog_disables, g_fog_colchg;  /* batch breaks */
static unsigned g_fog_hist[10];       /* probe: the coordinate handed to GL */
static unsigned g_fog_hist_s[10], g_fog_hist_c[10];  /* ... split by mode */
static unsigned char g_fog_alpha_seen[8];    /* distinct A_FOG alphas drawn */
static unsigned g_fog_alpha_n[8], g_fog_alpha_used;
static int      g_fog_src_readback = -1;

/* DEFAULT OFF. This is an unverified global renderer change: 1391 room
 * triangles a frame on facility alone, on every level, in all geometry - a
 * wider blast radius than the A_FOG path below, which is gated for exactly
 * this reason. Shipping it default-on put flickering, black textures and
 * floating geometry in front of the owner. An unverified global renderer
 * change defaults off and earns its default back with a controlled A/B.
 * SL_FOG=1 turns it on. */
static int fog_option(void)
{
    static int on = -1;
    /* DEFAULT ON. Fog is not a feature to opt into - it is per-level data the
     * game already carries. Facility's environment records call for it; a
     * level whose records do not will not get it, because the data decides,
     * not a switch. SL_FOG=0 remains as an escape hatch.
     *
     * Earned rather than assumed: the flicker that kept this off was a
     * missing glFogCoordf prototype (GL 1.4, absent from Mesa's <GL/gl.h>),
     * so default argument promotion widened the float to a double and the
     * entry point read the mantissa tail - every vertex an independent coin
     * flip. With the declaration in place the flicker gate goes from an 84%
     * adjacent swing to 0.4% over 60 gassed frames, and the owner confirmed
     * it by eye. */
    if (on < 0) { const char *v = getenv("SL_FOG");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* The A_FOG half is a SEPARATE claim, and it is now ON by default.
 *
 * G_RM_FOG_PRIM_A takes its factor from the fog colour's own alpha, which
 * model.c recomputes per model - "uses the FOG Alpha value for applying
 * Fog/Lighting", modelApplyRenderModeType3's own comment - so it is not the
 * gas at all but the per-object distance shade on every character and prop
 * in every level. Measured on one gassed facility frame, the alphas actually
 * drawn are 00 x2352, 39 x192, c1 x192, d3 x192, fe x192: mostly nothing,
 * and four models steeply shaded, one of them at 0xfe which paints it
 * essentially solid fog colour.
 *
 * B-045 left this half off because whether the hardware really did it had
 * not been measured. It has been now, on the owner's Runway marks
 * (20260911-065439-lvl35), and dropping it IS a defect - the owner's
 * "objectives draw really far":
 *
 *   - the game-side chain is live and correct on this build: the Runway fog
 *     row (bgfog.c:188) carries near 6000 / maxvis 8000 / obfusc 800, far fog
 *     15000, and fogGetPropDistColor computes fogalpha = farK/zDepth + nearK
 *     with farK=-1381.3, nearK=1.0960 (measured via SL_TABLE_DBG). The prop
 *     under mark-001's crosshair (zDepth 14336, size 247) gets fogalpha
 *     0.9997; the emplacement at 10032 gets 0.9583.
 *   - the DL the game writes carries exactly that: SetFogColor with the lerped
 *     shade colour and that alpha, 2-cycle, om_l c4112078 = (CLR_FOG, A_FOG,
 *     CLR_IN, 1MA) in cycle one - measured in the mark's own candidate-draws.
 *   - this stage then discarded it: the mark records fog=0 on those draws and
 *     the prop reached the screen opaque and untinted at 96% of the far
 *     plane, where the cartridge paints it 99.97% fog colour.
 *
 * So every value upstream is right and this was the first wrong consumed
 * stage. The factor is per-level data (the fog row) times per-prop distance,
 * not a switch, so the data decides here too - a level whose row puts
 * fogalpha at 0 shades nothing. SL_FOG_PRIM=0 is the escape hatch back to
 * the unshaded presentation, same binary. */
/* SL_FOG_VIZ: draw the FOG FACTOR itself, as greyscale, with textures and
 * the fog stage out of the way. 0 is black, full fog is white.
 *
 * This exists to split one contradiction that stalled B-045 for a day: the
 * probe reports ~2600 triangles a frame carrying the G_FOG bit while fog
 * changes ZERO pixels on most frames. Both cannot be true. A smooth depth
 * field here means the factor is computed correctly and the fault is after
 * interpolation - combiner, blender, or GL state. A patchwork here means the
 * fault is before it, in the factor itself.
 *
 * It is a diagnostic, not a rendering path: it says where to look, and it
 * cannot be mistaken for the game because nothing is textured. */
/* SL_FOG_VIZ=2 additionally keeps GL's fog stage OFF, so the probe colour
 * reaches the framebuffer unfogged. With =1 the factor field is itself fogged
 * - a fragment at factor 1 is painted pure fog colour and its blue channel is
 * lost - which made "the factor saturates" and "the backdrop shows through"
 * indistinguishable in the capture. */
static int fog_viz_option(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_FOG_VIZ");
                  on = (v == NULL || *v == '0') ? 0 : atoi(v); }
    return on;
}

static int fog_prim_option(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_FOG_PRIM");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* SL_FOG_MARK=1: paint the fog stage MAGENTA instead of the list's colour.
 *
 * One question has cost this bug two wrong answers: a region that reads
 * exactly the environment colour is either geometry at full fog or the
 * backdrop fillrect showing through, and those two are the same pixels. They
 * are the same pixels under SL_FOG_VIZ too, because GL fogs the probe colour
 * as well. Giving fog a colour the level cannot produce separates them in one
 * capture. Diagnostic only; it never affects a normal run. */
static int fog_mark_option(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_FOG_MARK");
                  on = (v != NULL && *v != '0'); }
    return on;
}

/* Which fog the triangle about to draw is under. The blender's FIRST cycle -
 * bits 30/26/22/18 of the lower othermode word - is the one GoldenEye puts it
 * in; rm_apply reads the producing cycle and so cannot see this. */
static int fog_mode(void)
{
    unsigned p, a;
    if (!fog_option()) return FOG_NONE;
    if (g_cycle_type != 1) return FOG_NONE;          /* G_CYC_2CYCLE only */
    p = (g_om_l >> 30) & 3u;
    a = (g_om_l >> 26) & 3u;
    if (p != 3u) return FOG_NONE;                    /* not CLR_FOG       */
    if (a == 2u) return (g_geom_mode & 0x00010000u) ? FOG_SHADE : FOG_NONE;
    if (a == 1u) return fog_prim_option() ? FOG_CONST : FOG_NONE;
    return FOG_NONE;
}

/* Establish GL's fog state for the triangle about to draw. Every call here is
 * illegal between glBegin and glEnd, so a change closes the batch first - the
 * same contract tex_apply, cull_apply and rm_apply keep. */
static void fog_off(void);

static void fog_apply(int mode)
{
#ifdef SL_FOGSTAGE
    if (!post11_ok()) { fog_off(); return; }
    if (fog_viz_option() == 2) { fog_off(); return; }
    if (mode == FOG_NONE) {
        /* Was: disable GL fog, which called batch_end() and cut the draw
         * batch mid-frame every time an unfogged surface followed a fogged
         * one. With every vertex now carrying its own value, an unfogged
         * surface is simply one whose vertices are zero - nothing to switch,
         * and the batch survives. The 2D ortho pass still turns it off once
         * per frame via fog_off(), which is not per-surface churn. */
        return;
    }
    if (!g_fog_gl_on || memcmp(g_fog_gl_col, g_fog, 3) != 0) {
        GLfloat c[4];
        batch_end();
        c[0] = (GLfloat) g_fog[0] / 255.0f;
        c[1] = (GLfloat) g_fog[1] / 255.0f;
        c[2] = (GLfloat) g_fog[2] / 255.0f;
        c[3] = 1.0f;
        if (fog_mark_option()) { c[0] = 1.0f; c[1] = 0.0f; c[2] = 1.0f; }
        glFogfv(GL_FOG_COLOR, c);
        g_fog_colchg++;
        if (!g_fog_gl_on) {
            g_fog_enables++;
            glFogi(GL_FOG_MODE, GL_LINEAR);
            glFogf(GL_FOG_START, 0.0f);
            glFogf(GL_FOG_END,   1.0f);
            glFogi(GL_FOG_COORD_SRC, GL_FOG_COORD);
            glEnable(GL_FOG);
            g_fog_gl_on = 1;
            if (g_fog_src_readback < 0) {
                GLint got = 0;
                glGetIntegerv(GL_FOG_COORD_SRC, &got);
                g_fog_src_readback = (int) got;
            }
        }
        memcpy(g_fog_gl_col, g_fog, 3);
    }
#else
    (void) mode;
#endif
}

static void fog_off(void)
{
#ifdef SL_FOGSTAGE
    if (g_fog_gl_on) { batch_end(); glDisable(GL_FOG); g_fog_gl_on = 0; }
#else
    g_fog_gl_on = 0;
#endif
}

/* Make the paths that never fog NEUTRAL instead of DISABLING the stage.
 *
 * The two-tone plane-key-room door (Runway owner mark 20260911-165102) is
 * why. The door's quad is one G_TRI4 - one texture, one combiner, one
 * SetFogColor (0,0,0 alpha 102), identical per-vertex fog coordinates 0.4 -
 * and it rendered split along the triangle diagonal: the FIRST triangle
 * unfogged, its twin blended. Measured with per-triangle glGets (fog
 * enabled, colour black, GL_FOG_COORD_SRC correct, current coordinate 0.4
 * before BOTH triangles) and with per-triangle batch isolation, which does
 * NOT heal it; the only thing that does is the enable having happened on an
 * earlier frame. On this driver the first primitive after glEnable(GL_FOG)
 * rasterises unfogged, however correct the queried state is - and because
 * the frame reset forced fog off every frame, the frame's FIRST fogged
 * triangle paid that price every frame, stably. A door face pays it
 * visibly; a room corner pays it invisibly.
 *
 * B-045's own design already contains the answer: "no fog IS a value of
 * zero" - every 3D vertex carries an explicit coordinate, so the stage can
 * stay enabled once it has been needed at all, and a path that must not fog
 * (the 2D ortho, the model-array path, the list tail) just pins the CURRENT
 * fog coordinate to zero for the vertices it emits without coordinates.
 * LINEAR(0..1) at coordinate 0 is factor 1 - the incoming fragment,
 * untouched. glFogCoordf is an attribute, not state, so this needs no batch
 * break and cannot trip the enable-validation quirk. fog_off stays for the
 * diagnostic arms (post11 fallback, SL_FOG_VIZ=2). */
static void fog_neutral(void)
{
#ifdef SL_FOGSTAGE
    if (g_fog_gl_on) glFogCoordf(0.0f);
#endif
}

/* The RSP's fog value for one vertex, as a GL fog coordinate in 0..1. ndc is
 * the row ndc_account filled: [2] is the perspective-divided z and [3] is 1
 * only when the point is in front of the eye. A point behind it is about to
 * be clipped, and the near plane is the no-fog end, so it contributes 0. */
/* Fog distance in WORLD UNITS, taken from the game's own scaled band, and
 * measured on the vertex's eye-space position.
 *
 * The old form put the RSP's fm/fo through the perspective-divided z, which
 * meant it depended on whichever projection g_proj happened to hold. Measured
 * on the owner's runs, the NDC z the renderer logs runs z[-309.59, 1.00] with
 * the maximum pinned at exactly 1.00 every frame - the signature of geometry
 * projected past a far plane of 300 while the room extends far beyond it. So
 * everything past ~300 units clamped to full fog and everything nearer got
 * none: all-or-nothing per surface, flipping as the matrix changed, which is
 * both the uniform green and the flicker. It painted the tank room dark green
 * BEFORE the tanks were blown, where Facility's band sits at 4960..5000 and
 * nothing should be fogged.
 *
 * bgfog.c already scales the band into world units for us:
 * g_ScaledDifferenceFromFarFogIntensity .. g_ScaledFarFogIntensity, which is
 * 4960..5000 normally and 990..1000 once the gas lerps the environment. That
 * is the same 0x1C/0x24 pair the format note describes ("difference between
 * near and far ambient light" and "far ambient light value"), so this reads
 * the documented values rather than re-deriving them. */
static float fog_coord(int mode, float depth)
{
    extern float g_ScaledFarFogIntensity;
    extern float g_ScaledDifferenceFromFarFogIntensity;
    float f;
    if (mode == FOG_CONST) f = (float) g_fog[3];
    else f = depth * (float) g_fogp_fm + (float) g_fogp_fo;

    /* SL_FOG_DUMP=N: the arithmetic for the first N vertices of a frame,
     * BEFORE the clamp. A clamped zero and a computed zero look identical
     * afterwards, and which one this is decides where the bug lives. */
    {
        static unsigned dumped, dump_frame;
        const char *dv = getenv("SL_FOG_DUMP");
        unsigned cap = dv ? (unsigned) atoi(dv) : 0;
        if (cap && dump_frame != g_dl_frame) { dump_frame = g_dl_frame; dumped = 0; }
        if (cap && dumped < cap) {
            dumped++;
            fprintf(stderr, "sl_fogv: f%u mode=%d depth=%.6f fm=%d fo=%d "
                            "raw=%.2f -> %.4f\n",
                    g_dl_frame, mode, depth, g_fogp_fm, g_fogp_fo,
                    f, (f < 0.0f ? 0.0f : (f > 255.0f ? 255.0f : f)) / 255.0f);
        }
    }
    if (f < 0.0f) f = 0.0f;
    if (f > 255.0f) f = 255.0f;
    f /= 255.0f;
    {   unsigned b = (unsigned) (f * 9.999f);
        if (b > 9) b = 9;
        g_fog_hist[b]++;
        if (mode == FOG_CONST) {
            unsigned i;
            g_fog_hist_c[b]++;
            for (i = 0; i < g_fog_alpha_used; i++)
                if (g_fog_alpha_seen[i] == g_fog[3]) break;
            if (i == g_fog_alpha_used && g_fog_alpha_used < 8) {
                g_fog_alpha_seen[i] = g_fog[3];
                g_fog_alpha_n[i] = 0;
                g_fog_alpha_used++;
            }
            if (i < 8) g_fog_alpha_n[i]++;
        } else {
            g_fog_hist_s[b]++;
        }
    }
    return f;
}

/* The alpha a 3D vertex draws with. It used to be a hard 255, which was
 * invisible while the 3D path never blended and is the other half of B-026:
 * a correctly-enabled GL_SRC_ALPHA over a forced 1.0 still writes opaque.
 *
 * The RDP's alpha comes out of the combiner, and this file classifies rather
 * than emulates that (see g_cc_rgb_const). The same rule rect_colour applies
 * in 2D applies here: a combiner whose alpha names PRIMITIVE takes the
 * primitive's alpha, otherwise the vertex carries its own - which is the
 * shade alpha the A_SHADE blender mux names, and what a Vtx's fourth colour
 * byte has always been (the struct has carried `a` since the vertex loader
 * was written; only this call site discarded it). */
static unsigned char tri_alpha(const struct vtx *v)
{
    /* B-051 THE FIX, DEFAULT ON (SL_ALPHA_EQ=0 opts out). The RDP's alpha
     * equation, evaluated per vertex, with the texel left to GL. See the AEQ_
     * block. First, so it supersedes the three shape-matched special cases
     * below - all of which are instances of it. They are kept, not deleted,
     * because SL_ALPHA_EQ=0 is the A/B control: it has to reproduce the
     * pre-fix renderer exactly, and the only way it can is by still being
     * there. */
    if (alpha_eq_on()) {
        unsigned a, b, c, d;
        struct aeq_expr e;
        int mode;
        aeq_producing(&a, &b, &c, &d);
        e = aeq_simplify(a, b, c, d, 0);
        mode = aeq_classify(&e);
        if (!aeq_mode_enabled(mode)) goto legacy;   /* SL_AEQ_MODES */
        if (aeq_stats_on()) {
            g_aeq_tris[mode]++;
            aeq_note_word(mode, &e, v);
            if (aeq_watch_on() && g_cc_w1 == g_aeq_watch_w1)
                g_aeq_watch_tris++;
        }
        /* The value comes from the SIMPLIFIED expression, so it agrees with
         * the mode chosen from it. REPLACE and MODULATE both want the
         * equation with the texel taken as opaque - for REPLACE that IS the
         * answer, for MODULATE it is the factor GL multiplies the texel into.
         * ADD wants the simplified ADDEND alone, because GL_ADD supplies the
         * sum with the texel. aeq_vertex_alpha is that rule, factored out so
         * the exactness probe predicts the renderer rather than a copy of it. */
        return aeq_vertex_alpha(mode, &e, v);
    }
    /* B-043. GoldenEye fades a character out by scaling its alpha with the
     * ENVIRONMENT register, not the primitive one. chrRenderProp (chr.c:2994)
     * ORs chr->fadealpha into the low byte of the render data's env colour and
     * sets PropType = PROP_TYPE_EXPLOSION+1, which is the one branch of
     * modelApplyRenderModeType3/4 that reads that byte back
     * (model.c:3547-3565, model.c:3834-3852) - the VIEWER+1 branch beside it
     * hardcodes a = 0xFF, which is why reading only that branch made the fade
     * look unimplemented. The combiner it emits, measured on a Facility boot
     * as the ONLY one of this shape in the whole run (fc159a04 5ffefff8), is
     *
     *     cycle 0 alpha  (TEXEL0 - 0) * ENVIRONMENT + 0
     *     cycle 1 alpha  (0 - 0) * 0 + COMBINED
     *
     * i.e. texel alpha scaled by the env alpha. GL_MODULATE already multiplies
     * the texture's alpha into glColor's, so handing back the env alpha alone
     * reproduces that product exactly. Before this, tri_alpha returned the
     * vertex alpha and the whole ramp - measured stepping 246..16 across
     * frames 482-601, 61320 triangles - was discarded, which is why Bond's
     * head stayed solid as the level-start camera passed through it. */
legacy:                              /* SL_AEQ_MODES sent this draw here */
    if (g_cc_a_envscale) {
        unsigned base;
        g_env_alpha_tris++;
        switch (g_cc_a_envsrc) {
        case CC_TEXEL0: base = 255; break;   /* GL_MODULATE supplies texel.a */
        case CC_A_ONE:  base = 255; break;
        case CC_SHADE:  base = v->a; break;
        case CC_PRIM:   base = g_prim[3]; break;
        default:        base = 255; break;
        }
        return (unsigned char) ((base * (unsigned) g_env[3] + 127u) / 255u);
    }
    if (g_cc_a_prim) return g_prim[3];
    /* B-051. The tinted-glass pane's opacity lives in the CYCLE 1 alpha
     * addend, so it has to be read here rather than through g_cc_a_prim.
     *
     * Rare: alpha = COMBINED.a * SHADE.a + PRIM.a, saturating. At full
     * opacity PRIM.a is 255 and the sum saturates, so the pane is opaque
     * whatever the texel and shade carry - which is the case the owner sees
     * as a hole. At the clear end PRIM.a is 0 and the pane vanishes, which is
     * also what the game intends and what they see working up close.
     *
     * Returning PRIM.a alone is exact at BOTH ends and an approximation in
     * the graded band between TintDist and CullDist, where Rare would add the
     * texel-times-shade term on top. GL_MODULATE still multiplies the texel
     * alpha in afterwards, so a pane with a transparent texture stays
     * transparent. Classify, do not emulate - the same bargain the rest of
     * this file makes, and the honest statement of its limit.
     *
     * OFF by default. The distance behaviour and the portal disable that go
     * with it are RARE'S (propobj.c:5889) and are not touched by this. */
    if (g_cc_a_prim1 && cc_glass_on()) { g_cc_glass_tris++; return g_prim[3]; }
    return v->a;
}

/* FF/FE. Raw operands, plus the framebuffer width FF carries. */
static unsigned g_cimg_addr, g_zimg_addr, g_cimg_w, g_cimg_cmds, g_zimg_cmds;
static int      g_cimg_is_z;

/* 03 movemem: a bitmask over (type >> 1) & 0x1f so the report can name which
 * kinds occurred without keeping a 256-entry table. */
static unsigned g_movemem_types, g_movemem_cmds;
/* Exact movemem type census - see the note at the OP_MOVEMEM case. */
static unsigned g_mm_type[16], g_mm_cnt[16], g_mm_n;
static int g_vp_seen, g_vp_sx, g_vp_sy, g_vp_tx, g_vp_ty;

/* Screen geometry for the 2D ortho. 320x240 until FF and ED say otherwise. */
static unsigned g_scr_w = 320, g_scr_h = 240;

/* Pending texture rectangle, assembled across E4 (or E5) + B4 + B3. */
static int      g_tr_pending, g_tr_flip, g_tr_have_st;
static unsigned g_tr_tile;
static int      g_tr_ax, g_tr_ay, g_tr_bx, g_tr_by;   /* two corners, 10.2 */
static int      g_tr_s, g_tr_t, g_tr_dsdx, g_tr_dtdy;

/* 2D telemetry. */
static unsigned g_tr_cmds, g_tr_drawn, g_tr_textured, g_tr_flips;
static unsigned g_tr_reject[6];         /* indexed by TR_* plus TR_DEGEN */
static unsigned g_tr_offscreen, g_tr_abandoned;
static unsigned g_tr_order_w0lr, g_tr_order_w0ul;
static unsigned g_tr_inverted;          /* lry < uly - draws nothing on the RDP */
static unsigned g_fr_cmds, g_fr_drawn, g_fr_zskip, g_fr_degen;
static unsigned g_fr_tintskip;
static unsigned g_fr_filled, g_fr_combined;
static unsigned g_fr_extended, g_rh_extended;   /* #45: backdrops widened to the content rect */

/* THE POINTER CURSOR'S PLACEMENT TAG (acceptance repair, the widescreen
 * cursor). A texrect cannot name a position left of logical 0 - E4's corner
 * words are unsigned 10.2 - and the game's own draw_textured_rectangle clips
 * xl < 0 to 0 (bondwalk2.c:43), so the front end's cursor and the watch's
 * crosshair, both ordinary texrects in logical space, could never be drawn in
 * the bands a wider aspect adds beside the 4:3 safe rect. The native pointer
 * layers therefore precede the cursor's texrect with ONE tagged no-op,
 *
 *     w0 = C0 'SLC' (the opcode byte over the 24-bit marker 0x534C43)
 *     w1 = s16 xl4 << 16 | s16 yl4     the rect's TOP-LEFT in 1/4 logical px
 *
 * and this walker places the NEXT texrect's top-left there, keeping its size,
 * texture and s/t - the same quad, translated. Signed, so the left band is
 * reachable; the 2D ortho already spans the content rect (mode2d_begin), so
 * nothing else changes. The tag is emitted only when the native layer wants
 * the cursor somewhere other than where the game encoded it (the mouse owns
 * the front end's cursor; the watch's crosshair always), and it names the
 * very value the game's own arithmetic produces when the two agree - so at
 * 4:3, and everywhere inside the safe rect, the pixels are the ones the
 * untagged draw would have made. Consumed by the next texrect; cleared by a
 * fillrect and at the frame reset so a tag without its texrect cannot leak
 * onto a later one. The placed quad alone is drawn with the scissor lifted
 * (see draw_texrect): the level's [0,10]-[320,230] scissor would otherwise
 * clip a cursor at the top or bottom edge of the window. */
static int      g_cur_ovr;                       /* a placement is waiting */
static int      g_cur_x4, g_cur_y4;              /* the top-left, 1/4 logical px */
static unsigned g_cur_applied, g_cur_tags;       /* per frame: applied / seen */
static unsigned g_half_orphan;          /* B3/B4 with no texrect pending */
static double   g_2d_cover;             /* summed rect area / screen area */
static float    g_2d_bbox[4];
#define TR_DEGEN 5

/* 2D writes no depth, so a rectangle issued AFTER the frame's first triangle
 * paints over the world - correct for a HUD, catastrophic for a background.
 * Counting the split is the only way to tell those apart without looking at
 * the window, and it is also what would catch a background fill that has
 * drifted to the wrong end of the list. */
static unsigned g_2d_pre_geom_tr, g_2d_post_geom_tr;
static unsigned g_2d_pre_geom_fr, g_2d_post_geom_fr;
static double   g_2d_post_geom_cover;
/* ...and the biggest one of them, so it can be named rather than guessed at. */
static float    g_2d_big[4];
static float    g_2d_big_area;
static int      g_2d_big_is_fill;
/* ...and the colour it drew with, which is what separates an opaque slab over
 * the world from the transparent scrim GoldenEye actually asked for. */
static unsigned char g_2d_big_col[4];
static unsigned char g_2d_cur_col[4];

/* ==================== B-051 THE ALPHA SHADER ==============================
 *
 * WHY THIS EXISTS, and it is a measurement rather than a preference.
 *
 * The RDP's producing cycle computes  alpha = (Aa - Ab) * Ac + Ad  over its
 * own mux list (ucode05_old.txt, "FC rdp_setcombine"). After simplification
 * exactly one of those four positions is ever a texel in this game, and it is
 * always Aa - so every equation reduces to
 *
 *      alpha = tex.a * C + (D - B * C)
 *
 * with B, C and D non-texel. GL 1.3 fixed function offers one per-vertex
 * alpha channel (the primary colour) and one per-draw constant, and that is
 * not enough to carry both a per-vertex C and a per-vertex addend:
 *
 *   GL_MODULATE      tex.a * C                  exact when the addend is 0
 *   GL_INTERPOLATE   tex.a * C + K * (1 - C)    exact when D - B*C is K*(1-C)
 *   GL_ADD           tex.a + D                  exact only when C is 1
 *
 * The twenty-level census MEASURED both remaining cases, and both fail:
 *
 *   fc26a004 1f1093fb  (COMB-0)*SHADE+PRIM   shade measured 89..255 on dam,
 *                                            102..255 on silo, 127..255 on
 *                                            aztec. GL_ADD's worst error 166.
 *   fc159804 5ffedbf8  (TEX0-ENV)*SHADE+ENV  env 255, so GL_ADD saturates the
 *                                            whole surface. Worst error 254,
 *                                            7.1M triangles, 13 of 20 levels.
 *
 * The second is a LERP and GL_INTERPOLATE expresses it exactly, so it stays on
 * fixed function (texenv 4). The first cannot be expressed at all, and that is
 * what this program is for: it evaluates the decoded equation directly, in the
 * one place where no fixed-function arrangement reproduces it.
 *
 * SCOPE, deliberately narrow. Only draws whose alpha equation fixed function
 * cannot express take this path. Everything else keeps the exact texenv it
 * used before, which is what lets SL_ALPHA_EQ=0 stay byte-identical to the
 * pre-fix renderer and keeps the vast majority of pixels bit-for-bit
 * unchanged. A shader over the whole scene would have to reproduce the RGB
 * combine, the second texture unit and fog bit-exactly, and it would not.
 *
 * WHAT IT MUST REPRODUCE. Binding a program replaces the WHOLE fragment
 * stage, so anything fixed function was doing has to be done here too:
 *   - RGB is GL_MODULATE, i.e. texel * primary. Unchanged, per the brief.
 *   - FOG. fog_apply sets GL_LINEAR with start 0, end 1 over GL_FOG_COORD, so
 *     the factor is the same clamp the fixed pipeline computes. Fog does not
 *     touch alpha on either path.
 *   - There is no alpha test and no lighting anywhere in this renderer
 *     (grep -n "GL_ALPHA_TEST\|glAlphaFunc\|GL_LIGHTING" src/gfx/ is empty),
 *     so there is nothing else to carry.
 *
 * Entry points are GL 2.0 and Mesa's <GL/gl.h> stops at 1.3, so every one is
 * resolved through sl_gl_proc with a typedef written out in full - never
 * called without a prototype. See sl_gl_proc's comment for what that cost
 * once. If any of them is missing the program never comes up, aeq_shader_ok()
 * stays 0, and the classifier falls back to GL_ADD with the limitation the
 * exactness probe then reports honestly.
 */
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER   0x8B31
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS  0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS     0x8B82
#endif

extern void *sl_gl_proc(const char *name);

/* APIENTRY IS LOAD-BEARING ON WINDOWS, and its absence was B-063.
 *
 * On Win32 the GL entry points are __stdcall - the CALLEE pops the argument
 * bytes - while a bare `typedef void (*F)(GLint, GLint)` is cdecl, where the
 * CALLER owns them. Measured on this toolchain, in exactly the include order
 * this file uses (<windows.h> then <GL/gl.h>):
 *
 *     APIENTRY -> __attribute__((__stdcall__))
 *
 * So every call through an undecorated pointer left esp 8 bytes higher than
 * the compiler believed. gcc at -O0 pre-allocates the outgoing-argument area
 * once in the prologue and never re-adds after a call, so the drift is
 * CUMULATIVE across a function: aeq_shader_uniforms issues seven of these in
 * a row against a 0x24-byte frame, and by the fifth the stores to (%esp) are
 * writing over its own saved ebp and return address. Facility died on frame 3
 * with an EXECUTE fault - control transferred to a stack address - which is
 * what a shredded frame looks like from the outside, and which is why the
 * first report pointed at everything except the calling convention.
 *
 * On Linux APIENTRY is empty and the two conventions coincide, so this is
 * invisible there. Same class as B-061: a Windows ABI difference, not a
 * defect in the renderer or in Rare's code.
 *
 * Anything added to this block MUST carry APIENTRY. gl.h defines it on both
 * hosts, so it stays correct on Linux at no cost.
 */

typedef GLuint (APIENTRY *SL_PFN_CREATESHADER)(GLenum);
typedef void   (APIENTRY *SL_PFN_SHADERSOURCE)(GLuint, GLsizei, const char **, const GLint *);
typedef void   (APIENTRY *SL_PFN_COMPILESHADER)(GLuint);
typedef void   (APIENTRY *SL_PFN_GETSHADERIV)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *SL_PFN_GETSHADERINFOLOG)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY *SL_PFN_CREATEPROGRAM)(void);
typedef void   (APIENTRY *SL_PFN_ATTACHSHADER)(GLuint, GLuint);
typedef void   (APIENTRY *SL_PFN_LINKPROGRAM)(GLuint);
typedef void   (APIENTRY *SL_PFN_GETPROGRAMIV)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *SL_PFN_GETPROGRAMINFOLOG)(GLuint, GLsizei, GLsizei *, char *);
typedef void   (APIENTRY *SL_PFN_USEPROGRAM)(GLuint);
typedef GLint  (APIENTRY *SL_PFN_GETUNIFORMLOCATION)(GLuint, const char *);
typedef void   (APIENTRY *SL_PFN_UNIFORM1I)(GLint, GLint);
typedef void   (APIENTRY *SL_PFN_UNIFORM1F)(GLint, GLfloat);

static SL_PFN_CREATESHADER        p_CreateShader;
static SL_PFN_SHADERSOURCE        p_ShaderSource;
static SL_PFN_COMPILESHADER       p_CompileShader;
static SL_PFN_GETSHADERIV         p_GetShaderiv;
static SL_PFN_GETSHADERINFOLOG    p_GetShaderInfoLog;
static SL_PFN_CREATEPROGRAM       p_CreateProgram;
static SL_PFN_ATTACHSHADER        p_AttachShader;
static SL_PFN_LINKPROGRAM         p_LinkProgram;
static SL_PFN_GETPROGRAMIV        p_GetProgramiv;
static SL_PFN_GETPROGRAMINFOLOG   p_GetProgramInfoLog;
static SL_PFN_USEPROGRAM          p_UseProgram;
static SL_PFN_GETUNIFORMLOCATION  p_GetUniformLocation;
static SL_PFN_UNIFORM1I           p_Uniform1i;
static SL_PFN_UNIFORM1F           p_Uniform1f;

static GLuint g_aeq_prog;
static GLint  g_u_tex, g_u_a, g_u_b, g_u_c, g_u_d, g_u_prim, g_u_env, g_u_fog;
static int    g_aeq_prog_state;          /* 0 untried, 1 ready, -1 unavailable */

/* GLSL 1.10, compatibility profile - the same fixed-function state the rest
 * of this file sets is visible here as gl_Fog, gl_Color and gl_TexCoord, so
 * nothing has to be plumbed twice.
 *
 * src() mirrors aeq_src_value's mux list exactly. COMBINED resolves to the
 * texel for the same reason the CPU evaluator does: after simplification a
 * surviving COMBINED is cycle 0's texel term, and cycle 0 in every word that
 * reaches this path is a mip blend between TEXEL0 and TEXEL1 of one image. */
/* B-131: both stages are GLSL 1.30 so the colour and the fog coordinate can
 * travel as noperspective user varyings (v_col, v_fog) - the RDP's shade
 * interpolation rule; see SHADE_LINEAR_VS for the measurement and
 * SHADE_LINEAR_FS for why redeclared built-ins do not do it. Everything else
 * is the 1.10 program it was, valid unchanged under the compatibility
 * profile's 1.30. */
static const char *AEQ_VS =
"#version 130\n"
"noperspective out vec4 v_col;\n"
"noperspective out float v_fog;\n"
"void main() {\n"
"    gl_Position = ftransform();\n"
"    v_col = gl_Color;\n"
"    v_fog = gl_FogCoord;\n"
"    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"}\n";

static const char *AEQ_FS =
"#version 130\n"
"noperspective in vec4 v_col;\n"
"noperspective in float v_fog;\n"
"uniform sampler2D u_tex;\n"
"uniform int u_a, u_b, u_c, u_d, u_fog;\n"
"uniform float u_prim, u_env;\n"
"float src(int m, float t, float sh) {\n"
"    if (m == 3) return u_prim;\n"
"    if (m == 4) return sh;\n"
"    if (m == 5) return u_env;\n"
"    if (m == 6) return 1.0;\n"
"    if (m == 7) return 0.0;\n"
"    return t;\n"
"}\n"
"void main() {\n"
"    vec4 t = texture2D(u_tex, gl_TexCoord[0].st);\n"
"    float sh = v_col.a;\n"
"    float a = clamp((src(u_a, t.a, sh) - src(u_b, t.a, sh))\n"
"                    * src(u_c, t.a, sh) + src(u_d, t.a, sh), 0.0, 1.0);\n"
"    vec3 rgb = t.rgb * v_col.rgb;\n"
"    if (u_fog == 1) {\n"
"        float f = clamp((gl_Fog.end - v_fog) * gl_Fog.scale,\n"
"                        0.0, 1.0);\n"
"        rgb = mix(gl_Fog.color.rgb, rgb, f);\n"
"    }\n"
"    gl_FragColor = vec4(rgb, a);\n"
"}\n";

static GLuint aeq_compile(GLenum type, const char *src, const char *what)
{
    GLuint sh;
    GLint ok = 0;
    sh = p_CreateShader(type);
    if (sh == 0) return 0;
    p_ShaderSource(sh, 1, &src, NULL);
    p_CompileShader(sh);
    p_GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetShaderInfoLog(sh, (GLsizei) sizeof log, &n, log);
        fprintf(stderr, "sl_aeq: %s shader did not compile: %s\n", what, log);
        return 0;
    }
    return sh;
}

/* Built once, on first use, because it needs a live context. Reports what it
 * did either way: a silently absent shader would leave the ADD fallback
 * running and look like a working fix. */
static int aeq_shader_ok(void)
{
    GLuint vs, fs;
    GLint ok = 0;
    if (g_aeq_prog_state != 0) return g_aeq_prog_state > 0;
    g_aeq_prog_state = -1;

    p_CreateShader       = (SL_PFN_CREATESHADER)       sl_gl_proc("glCreateShader");
    p_ShaderSource       = (SL_PFN_SHADERSOURCE)       sl_gl_proc("glShaderSource");
    p_CompileShader      = (SL_PFN_COMPILESHADER)      sl_gl_proc("glCompileShader");
    p_GetShaderiv        = (SL_PFN_GETSHADERIV)        sl_gl_proc("glGetShaderiv");
    p_GetShaderInfoLog   = (SL_PFN_GETSHADERINFOLOG)   sl_gl_proc("glGetShaderInfoLog");
    p_CreateProgram      = (SL_PFN_CREATEPROGRAM)      sl_gl_proc("glCreateProgram");
    p_AttachShader       = (SL_PFN_ATTACHSHADER)       sl_gl_proc("glAttachShader");
    p_LinkProgram        = (SL_PFN_LINKPROGRAM)        sl_gl_proc("glLinkProgram");
    p_GetProgramiv       = (SL_PFN_GETPROGRAMIV)       sl_gl_proc("glGetProgramiv");
    p_GetProgramInfoLog  = (SL_PFN_GETPROGRAMINFOLOG)  sl_gl_proc("glGetProgramInfoLog");
    p_UseProgram         = (SL_PFN_USEPROGRAM)         sl_gl_proc("glUseProgram");
    p_GetUniformLocation = (SL_PFN_GETUNIFORMLOCATION) sl_gl_proc("glGetUniformLocation");
    p_Uniform1i          = (SL_PFN_UNIFORM1I)          sl_gl_proc("glUniform1i");
    p_Uniform1f          = (SL_PFN_UNIFORM1F)          sl_gl_proc("glUniform1f");
    if (p_CreateShader == NULL || p_ShaderSource == NULL
        || p_CompileShader == NULL || p_GetShaderiv == NULL
        || p_GetShaderInfoLog == NULL || p_CreateProgram == NULL
        || p_AttachShader == NULL || p_LinkProgram == NULL
        || p_GetProgramiv == NULL || p_GetProgramInfoLog == NULL
        || p_UseProgram == NULL || p_GetUniformLocation == NULL
        || p_Uniform1i == NULL || p_Uniform1f == NULL) {
        fprintf(stderr, "sl_aeq: GL 2.0 shader entry points unavailable -"
                        " the multiply-add alpha falls back to GL_ADD\n");
        return 0;
    }
    vs = aeq_compile(GL_VERTEX_SHADER, AEQ_VS, "vertex");
    fs = aeq_compile(GL_FRAGMENT_SHADER, AEQ_FS, "fragment");
    if (vs == 0 || fs == 0) return 0;
    g_aeq_prog = p_CreateProgram();
    if (g_aeq_prog == 0) return 0;
    p_AttachShader(g_aeq_prog, vs);
    p_AttachShader(g_aeq_prog, fs);
    p_LinkProgram(g_aeq_prog);
    p_GetProgramiv(g_aeq_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetProgramInfoLog(g_aeq_prog, (GLsizei) sizeof log, &n, log);
        fprintf(stderr, "sl_aeq: alpha program did not link: %s\n", log);
        g_aeq_prog = 0;
        return 0;
    }
    g_u_tex  = p_GetUniformLocation(g_aeq_prog, "u_tex");
    g_u_a    = p_GetUniformLocation(g_aeq_prog, "u_a");
    g_u_b    = p_GetUniformLocation(g_aeq_prog, "u_b");
    g_u_c    = p_GetUniformLocation(g_aeq_prog, "u_c");
    g_u_d    = p_GetUniformLocation(g_aeq_prog, "u_d");
    g_u_prim = p_GetUniformLocation(g_aeq_prog, "u_prim");
    g_u_env  = p_GetUniformLocation(g_aeq_prog, "u_env");
    g_u_fog  = p_GetUniformLocation(g_aeq_prog, "u_fog");
    p_UseProgram(g_aeq_prog);
    if (g_u_tex >= 0) p_Uniform1i(g_u_tex, 0);
    p_UseProgram(0);
    g_aeq_prog_state = 1;
    fprintf(stderr, "sl_aeq: alpha program ready - the multiply-add form of"
                    " the RDP alpha equation is evaluated exactly\n");
    return 1;
}

/* Push this draw's equation. Uniform changes are illegal inside glBegin/glEnd,
 * so the batch is closed first - and only when something actually changed,
 * because closing a batch per triangle would be a rasterisation change, not
 * just a slow one. */
static int g_aeq_u[4] = { -1, -1, -1, -1 };
static int g_aeq_u_prim = -1, g_aeq_u_env = -1, g_aeq_u_fog = -1;
static void aeq_shader_uniforms(const struct aeq_expr *e)
{
    int a = (int) e->mul_a, b = (int) e->mul_b, c = (int) e->mul_c;
    int d = (int) (e->has_add ? e->add : CC_A_ZERO);
    int pr = (int) g_prim[3], en = (int) g_env[3], fg = g_fog_gl_on ? 1 : 0;
    if (a == g_aeq_u[0] && b == g_aeq_u[1] && c == g_aeq_u[2]
        && d == g_aeq_u[3] && pr == g_aeq_u_prim && en == g_aeq_u_env
        && fg == g_aeq_u_fog)
        return;
    batch_end();
    p_Uniform1i(g_u_a, a); p_Uniform1i(g_u_b, b);
    p_Uniform1i(g_u_c, c); p_Uniform1i(g_u_d, d);
    p_Uniform1f(g_u_prim, (GLfloat) pr / 255.0f);
    p_Uniform1f(g_u_env,  (GLfloat) en / 255.0f);
    p_Uniform1i(g_u_fog, fg);
    g_aeq_u[0] = a; g_aeq_u[1] = b; g_aeq_u[2] = c; g_aeq_u[3] = d;
    g_aeq_u_prim = pr; g_aeq_u_env = en; g_aeq_u_fog = fg;
}

static int aeq_shader_avail(void) { return aeq_shader_ok(); }

/* ===================== B-131: the RDP's shade interpolation ==============
 *
 * The RDP interpolates SHADE - the vertex colour and, in its alpha, the fog
 * value the RSP computed per vertex - LINEARLY IN SCREEN SPACE. The notes
 * call it Gouraud (ucode05.txt:400, "shade smooth ... Gourand interpolation")
 * and give a perspective control for TEXTURE only (ucode05.txt:853, the
 * othermode "Texture Perspective" bit); nothing corrects the shade. GL's
 * fixed pipeline interpolates every varying PERSPECTIVE-CORRECTLY, which
 * reproduces a quantity that is affine across the triangle's plane in WORLD
 * space. The RSP's fog is fm * z_ndc + fo, and z_ndc is not world-affine
 * (it is 1 - k/z_eye), so the two rules only agree at the vertices.
 *
 * MEASURED, Surface 2, the owner's marks 20260914-234420 ("the ground is
 * lighting up in patches as I walk", a hard brightness step across the
 * snow). The spawn valley floor is triangles spanning 6..246 eye units
 * (mark-001 candidates seq 29-31; shade a4..ff per vertex, fog fm 2976 /
 * fo -2720 under the play row), the fog field itself (SL_FOG_VIZ=2) is a
 * smooth function of the screen row, and the crease is in the SHADE. At
 * the mark pose (theta 84.9, verta -21.4) the pre-fix frame rose 77 -> 132
 * over rows 280..460 through a diagonal crease; with this program the same
 * rows read 76 -> 102 with no crease, and the cartridge at the spawn pose
 * and the same pitch (tools/native/romintro.py with a C-up hold) reads
 * 47 -> 93 with the same mid-band plateau (rows 430..580: cartridge 89..93,
 * this 98..104, before 121..132). The residual offset is the cartridge's
 * darker 240-line output; the SHAPE is what the owner sees. Facility,
 * Runway and Silo controls move by a mean of 0.0 / 0.4 / 0.5 per channel -
 * the rule only matters where a triangle's depth range is large.
 *
 * THE FIX is the interpolation qualifier, nothing else: a program that is
 * the fixed pipeline's own transform (ftransform; the texture coordinates
 * pass through perspective-correct, as the RDP corrects texture) carrying
 * the colour and the fog coordinate as noperspective varyings, and a
 * fragment stage that reproduces the texture environments room geometry
 * draws under - see SHADE_LINEAR_FS for why a fragment stage is needed at
 * all. The B-051 alpha program carries the same qualifiers in its own
 * stages. Bound around the DL triangle batch only. SL_SHADE_LINEAR=0
 * restores fixed-function interpolation, same binary; the sl_shade census
 * line reports whether the program is live. */
static const char *SHADE_LINEAR_VS =
"#version 130\n"
"noperspective out vec4 v_col;\n"
"noperspective out float v_fog;\n"
"void main() {\n"
"    gl_Position = ftransform();\n"
"    v_col = gl_Color;\n"
"    v_fog = gl_FogCoord;\n"
"    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"    gl_TexCoord[1] = gl_MultiTexCoord1;\n"
"}\n";

/* The fragment stage is REQUIRED, and so are USER varyings, both measured:
 * with only the vertex stage bound (22676 batches of a run) the frame was
 * the fixed pipeline's - the driver's own fragment processing ignores the
 * qualifier on a built-in it consumes itself - and with both stages bound
 * but the qualifier on redeclared built-ins (noperspective out vec4
 * gl_FrontColor / in vec4 gl_Color, GLSL 1.30 form) the frame was STILL the
 * fixed pipeline's to within a mean of 1.2: NVIDIA 591.86 accepts the
 * redeclaration and interpolates the built-in perspective-correct anyway.
 * v_col / v_fog below are what actually changes the rasterisation (the
 * same pose then moves by a mean of 13). So the stage reproduces the
 * texture environments the room-geometry family draws under, and nothing
 * more: mode 0 (GL_MODULATE), mode 3 (B-051 AEQ_ADD: alpha tex + shade,
 * saturated) and mode 4 (B-051 AEQ_LERP: alpha tex * shade + K * (1 -
 * shade), K the texture-environment constant's alpha, read here through
 * gl_TextureEnvColor). Texturing off means the primary colour alone, as
 * GL_COMBINE with the unit disabled does. Fog is the fixed pipeline's own
 * GL_LINEAR(0..1) on the coordinate, exactly as the B-051 program does it,
 * because a fragment program replaces the fixed fog stage. Modes 6-9 (the
 * register lerp, the sky, the water lerp, the two-image LOD blend) and a
 * live second unit stay on the fixed pipeline and keep perspective-correct
 * shade: the sky and the two-unit blends are vertical strips whose depth
 * range per triangle is small, so the rule barely moves them; recorded as
 * residual debt, not silently covered. */
static const char *SHADE_LINEAR_FS =
"#version 130\n"
"noperspective in vec4 v_col;\n"
"noperspective in float v_fog;\n"
"uniform sampler2D u_tex;\n"
"uniform int u_texon, u_mode, u_fog;\n"
"void main() {\n"
"    vec4 c = v_col;\n"
"    if (u_texon == 1) {\n"
"        vec4 t = texture2D(u_tex, gl_TexCoord[0].st);\n"
"        c.rgb *= t.rgb;\n"
"        if (u_mode == 3) c.a = min(t.a + v_col.a, 1.0);\n"
"        else if (u_mode == 4) c.a = t.a * v_col.a\n"
"                              + gl_TextureEnvColor[0].a * (1.0 - v_col.a);\n"
"        else c.a = t.a * v_col.a;\n"
"    }\n"
"    if (u_fog == 1) {\n"
"        float f = clamp((gl_Fog.end - v_fog) * gl_Fog.scale,\n"
"                        0.0, 1.0);\n"
"        c.rgb = mix(gl_Fog.color.rgb, c.rgb, f);\n"
"    }\n"
"    gl_FragColor = c;\n"
"}\n";

static GLuint   g_lin_prog;
static GLint    g_lin_u_tex, g_lin_u_texon, g_lin_u_mode, g_lin_u_fog;
static int      g_lin_state;             /* 0 untried, 1 ready, -1 unavailable */
static int      g_lin_bound;
static int      g_lin_cur_texon = -1, g_lin_cur_mode = -1, g_lin_cur_fog = -1;
static unsigned g_lin_batches;

static int shade_linear_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_SHADE_LINEAR");
                  on = (v == NULL || *v != '0'); }
    return on;
}

static int shade_linear_ok(void)
{
    GLuint vs, fs;
    GLint ok = 0;
    if (g_lin_state != 0) return g_lin_state > 0;
    g_lin_state = -1;
    if (!aeq_shader_ok()) {              /* loads the entry points as well */
        fprintf(stderr, "sl_shade: no GL 2.0 program support - shade and fog"
                        " interpolate perspective-correct (fixed pipeline)\n");
        return 0;
    }
    vs = aeq_compile(GL_VERTEX_SHADER, SHADE_LINEAR_VS, "shade-linear vertex");
    fs = aeq_compile(GL_FRAGMENT_SHADER, SHADE_LINEAR_FS, "shade-linear fragment");
    if (vs == 0 || fs == 0) return 0;
    g_lin_prog = p_CreateProgram();
    if (g_lin_prog == 0) return 0;
    p_AttachShader(g_lin_prog, vs);
    p_AttachShader(g_lin_prog, fs);
    p_LinkProgram(g_lin_prog);
    p_GetProgramiv(g_lin_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei n = 0;
        log[0] = '\0';
        p_GetProgramInfoLog(g_lin_prog, (GLsizei) sizeof log, &n, log);
        fprintf(stderr, "sl_shade: linear program did not link: %s\n", log);
        g_lin_prog = 0;
        return 0;
    }
    g_lin_u_tex   = p_GetUniformLocation(g_lin_prog, "u_tex");
    g_lin_u_texon = p_GetUniformLocation(g_lin_prog, "u_texon");
    g_lin_u_mode  = p_GetUniformLocation(g_lin_prog, "u_mode");
    g_lin_u_fog   = p_GetUniformLocation(g_lin_prog, "u_fog");
    p_UseProgram(g_lin_prog);
    if (g_lin_u_tex >= 0) p_Uniform1i(g_lin_u_tex, 0);
    p_UseProgram(0);
    g_lin_state = 1;
    fprintf(stderr, "sl_shade: linear program ready - shade and fog interpolate"
                    " in screen space, the RDP's rule (SL_SHADE_LINEAR=0 reverts)\n");
    return 1;
}

/* Called by batch_begin (on=1) and batch_end (on=0). Mode 5 owns its own
 * program (texenv_set binds it and leaves it bound across batches), so the
 * batch never touches the binding while that mode is current. The program
 * takes only the environments its fragment stage reproduces (0, 3, 4) with
 * the second unit off; anything else draws on the fixed pipeline. The
 * uniforms are pushed here, before glBegin: every state they mirror changes
 * only through a batch_end, so the batch's first vertex sees them current. */
static void shade_linear_bind(int on)
{
    if (g_texenv == 5) {
        /* B-142 invariant: a batch opening untextured under the alpha
         * program. tex_apply's untextured returns make this unreachable. */
        if (on && !g_tex_gl_on) g_plain_under_prog++;
        return;
    }
    if (on) {
        int texon, mode, fog;
        if (!shade_linear_on() || !shade_linear_ok()) return;
        if (g_texenv != 0 && g_texenv != 3 && g_texenv != 4) return;
        if (g_tex1_gl_on) return;
        texon = g_tex_gl_on ? 1 : 0;
        mode  = g_texenv;
        fog   = g_fog_gl_on ? 1 : 0;
        p_UseProgram(g_lin_prog);
        if (texon != g_lin_cur_texon) { p_Uniform1i(g_lin_u_texon, texon); g_lin_cur_texon = texon; }
        if (mode  != g_lin_cur_mode)  { p_Uniform1i(g_lin_u_mode,  mode);  g_lin_cur_mode  = mode; }
        if (fog   != g_lin_cur_fog)   { p_Uniform1i(g_lin_u_fog,   fog);   g_lin_cur_fog   = fog; }
        g_lin_bound = 1;
        g_lin_batches++;
    } else if (g_lin_bound) {
        p_UseProgram(0);
        g_lin_bound = 0;
    }
}

/* Per-draw state for the chosen representation, pushed AFTER texenv_set so the
 * environment it configures is the current one. Nothing to do for REPLACE or
 * MODULATE: those carry everything they need in the vertex alpha. */
static void aeq_apply_env(int mode)
{
    unsigned a, b, c, d;
    struct aeq_expr e;
    if (mode != AEQ_LERP && mode != AEQ_SHADER) return;
    aeq_producing(&a, &b, &c, &d);
    e = aeq_simplify(a, b, c, d, 0);
    if (mode == AEQ_SHADER) { aeq_shader_uniforms(&e); return; }
#if defined(SL_TEXENV_COMBINE) && defined(GL_INTERPOLATE) && defined(GL_CONSTANT)
    {
        /* The value the equation lerps away from. aeq_src_perdraw gates the
         * LERP classification precisely so this cannot be a per-vertex
         * source, which is why evaluating it with no vertex is sound.
         * Through the ONE mirror - see envcol_set. */
        struct vtx nov;
        int k;
        nov.a = 0;
        k = (int) aeq_lerp_const(&e, &nov);
        envcol_set(0.0f, 0.0f, 0.0f, (GLfloat) k / 255.0f);
    }
#endif
}

/* GL_COMBINE is core GL 1.3. Where the header predates it the font path falls
 * back to GL_MODULATE, which tints the glyph by its own intensity as well as
 * by the primitive colour - visibly softer, never invisible.
 * B-110: SL_TEXENV_COMBINE is defined at the TOP of the file, after the
 * B-076 token block - it lived here once, below two of its users, and the
 * preprocessor silently compiled them out. See the block at the includes. */

/* B-090. The blend factor, pushed AFTER texenv_set so the environment it
 * configures is the current one - the same ordering rule aeq_apply_env states.
 * Cached, because the File Select background issues 299 rects a frame and they
 * all carry the same factor; only PRIMITIVE moves between them, and that
 * travels in the vertex colour. */
static void cc_lerp_apply(float k)
{
#if defined(SL_TEXENV_COMBINE) && defined(GL_INTERPOLATE) && defined(GL_CONSTANT)     && defined(GL_SOURCE2_RGB) && defined(GL_OPERAND2_RGB)
    /* Through the ONE mirror - see envcol_set. The 299-rects-a-frame File
     * Select case it was cached for still writes once: the mirror's skip
     * test is the same comparison the private cache made. */
    envcol_set(0.0f, 0.0f, 0.0f, (GLfloat) k);
#else
    (void) k;
#endif
}

/* glTexEnv is illegal between glBegin and glEnd, exactly like glGenTextures -
 * every caller closes the batch first. */
static void texenv_set(int mode)
{
    if (mode == g_texenv) return;
    /* Leaving the shader: put the fixed pipeline back FIRST, before any
     * glTexEnv below. Every other mode is fixed function, so this is the one
     * place the program can be left bound by accident - and a stray program
     * over the 2D path would repaint the HUD with a world shader. */
    if (g_texenv == 5 && p_UseProgram != NULL) p_UseProgram(0);
#ifdef SL_TEXENV_COMBINE
    if (mode == 5) {
        /* B-051. The alpha shader. RGB and fog are reproduced inside it; see
         * the block above for why nothing else has to be. */
        g_texenv = 5;
        p_UseProgram(g_aeq_prog);
        return;
    }
#endif
#if defined(SL_TEXENV_COMBINE) && defined(GL_INTERPOLATE) && defined(GL_CONSTANT) \
    && defined(GL_SOURCE2_ALPHA) && defined(GL_OPERAND2_ALPHA)
    if (mode == 4) {
        /* B-051 AEQ_LERP. RGB modulated exactly as mode 0; alpha is
         *     tex.a * primary.a + CONSTANT.a * (1 - primary.a)
         * which is the RDP's (TEX0 - B) * C + B with C in the vertex alpha and
         * B in the texture environment colour. EXACT, per-vertex C included -
         * which is what GL_ADD could not do. */
        g_texenv = 4;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_INTERPOLATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA, GL_SRC_ALPHA);
        return;
    }
#endif
#if defined(SL_TEXENV_COMBINE) && defined(GL_INTERPOLATE) && defined(GL_CONSTANT)     && defined(GL_SOURCE2_RGB) && defined(GL_OPERAND2_RGB)
    if (mode == 6) {
        /* B-090. RGB is the RDP's (TEXEL0 - K) * k + K, with K in the vertex
         * colour and k in the texture-environment CONSTANT's alpha. GL's
         * INTERPOLATE computes Arg0 * Arg2 + Arg1 * (1 - Arg2), which is that
         * blend exactly - no approximation, unlike the GL_ADD limit mode 3
         * carries. ALPHA is left as mode 0's GL_MODULATE deliberately: the
         * defect is in the colour equation, and leaving alpha alone keeps
         * every draw that lands in this class bit-identical in alpha to how it
         * drew before. */
        g_texenv = 6;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_CONSTANT);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        return;
    }
#endif
#ifdef SL_TEXENV_COMBINE
    if (mode == 1) {
        g_texenv = 1;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        return;
    }
#endif
#if defined(SL_TEXENV_COMBINE) && defined(GL_INTERPOLATE) && defined(GL_CONSTANT)     && defined(GL_SOURCE2_RGB) && defined(GL_OPERAND2_RGB)
    if (mode == 7) {
        /* B-098. The sky. RGB is the RDP's (SHADE - K) * TEXEL0 + K, with K in
         * the texture-environment CONSTANT and the factor in the TEXEL itself.
         * GL's INTERPOLATE computes Arg0 * Arg2 + Arg1 * (1 - Arg2), which is
         * that blend exactly.
         *
         * OPERAND2_RGB is GL_SRC_COLOR, not mode 6's GL_SRC_ALPHA: the RDP's c
         * slot is PER CHANNEL, so the red channel must be interpolated by the
         * texel's red and not by its alpha. A colour-valued Arg2 is OpenGL 1.4
         * (in 1.3 core OPERAND2_RGB took only the alpha operands), and this
         * context is a 4.5 compatibility profile - the same one B-076 measured
         * when it found the ENUMS missing from the headers while the driver
         * supported them. If a driver ever rejects it the request raises
         * GL_INVALID_ENUM and Arg2 keeps its previous value, which the
         * end-of-frame `sl_gl: N error(s)` drain reports rather than hiding.
         *
         * ALPHA is REPLACE from the vertex, because the measured alpha
         * equation is (0 - 0) * 0 + SHADE - the texel is not consulted at all.
         * That is deliberate and not incidental: leaving it as mode 0's
         * GL_MODULATE is what let an IA cloud image blend the sky away. */
        g_texenv = 7;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        return;
    }
#endif
#if defined(SL_TEXENV_COMBINE) && defined(GL_INTERPOLATE) && defined(GL_CONSTANT) \
    && defined(GL_SOURCE2_RGB) && defined(GL_OPERAND2_RGB) && defined(GL_TEXTURE1)
    if (mode == 8) {
        /* B-107. The water lerp's FIRST cycle, on unit 0:
         *     (TEXEL1 - TEXEL0) * PRIM_LOD_FRAC + TEXEL0
         * GL's INTERPOLATE computes Arg0 * Arg2 + Arg1 * (1 - Arg2), which
         * is that blend exactly, with the animated PRIM_LOD_FRAC carried in
         * the texture-environment CONSTANT (set per draw in tex_apply).
         * Arg0 names the OTHER unit's texel - GL_TEXTUREn combiner sources
         * are OpenGL 1.4's crossbar, the same vintage as mode 7's
         * colour-valued Arg2 and under the same contract: a driver that
         * refuses raises GL_INVALID_ENUM into the end-of-frame drain rather
         * than failing silently. Unit 1 (tex1_apply's lerp arm) then
         * multiplies by SHADE, completing the second cycle. ALPHA here is
         * mode 0's MODULATE, so alpha is unchanged from the base-tile
         * rendering this replaces. */
        g_texenv = 8;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE1);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_CONSTANT);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        return;
    }
    if (mode == 9) {
        /* B-119. The LOD_FRACTION detail-blend, corrected from B-118's
         * per-triangle constant. RGB is the same crossbar INTERPOLATE as
         * mode 8, but Arg2 is the PRIMARY COLOUR'S ALPHA - the per-vertex
         * LOD fraction the emit path computes and smuggles into glColor's
         * alpha - so the blend is Gouraud-interpolated PER PIXEL across each
         * triangle and continuous across a wall, where the constant popped
         * per triangle ("clear at one point, a mess at others"). The vertex
         * alpha is free for this: the family's authored alpha equation
         * (FC 1f14ffff, muxes per ucode05_old.txt "FC rdp_setcombine") is
         * (1 - 0) * TEXEL1 + 0 - the STRIP's own alpha, never SHADE's - and
         * that is what ALPHA below emits: REPLACE from the crossbar's
         * TEXTURE1. That one term is also the owner's "cardboard" fix: the
         * strip's zero-alpha sky shows between the tree tips again. */
        g_texenv = 9;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE1);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE1);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        return;
    }
#endif
#ifdef SL_TEXENV_COMBINE
    if (mode == 2) {
        /* B-051 EXPERIMENT, SL_GLASS_MARK=3. RGB modulated exactly as mode 0,
         * but ALPHA taken from the vertex alone - the texel's alpha is not
         * consulted.
         *
         * This is the shape any real fix would take, because the RDP's RGB and
         * ALPHA are INDEPENDENT equations while GL_MODULATE couples them
         * (out.a = tex.a * in.a). ucode05_old.txt's FC table gives the glass
         * pane's alpha as (COMBINED.a - 0) * SHADE.a + PRIMITIVE.a, and an
         * ADDEND has no GL_MODULATE expression at any vertex alpha.
         *
         * It is an experiment and not a fix: it is armed only for the glass
         * combiner, only under a probe flag, and it would need the B-048
         * standard (rooms, characters, viewmodel, HUD, font path, menus,
         * across levels) plus a ROM comparison before going anywhere near a
         * default. */
        g_texenv = 2;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        return;
    }
#endif
#ifdef SL_TEXENV_COMBINE
    if (mode == 3) {
        /* B-051 THE FIX, AEQ_ADD. RGB modulated as usual; alpha is
         * tex.a + primary.a, which is the RDP's additive alpha stage with the
         * multiply folded into the vertex value. GL clamps the sum, which is
         * the saturation ucode05_old.txt's combiner has. */
        g_texenv = 3;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_ADD);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        return;
    }
#endif
    g_texenv = 0;
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

/* Screen-space drawing mode: an ortho over the framebuffer, no depth test,
 * straight alpha blending. Entered lazily because a 2D command can appear
 * anywhere in the list, and left again the moment the list touches a matrix
 * or emits a triangle - so the 3D path never sees a state it did not set. */
static int g_2d_on;
static unsigned g_2d_enters;

/* ===================== viewport (B-046) ==================================
 *
 * The RSP's viewport, applied. It used to be recorded and dropped, on the
 * reasoning that "the GL viewport is the whole window and a split screen has
 * no meaning until there is more than one player" - true about split screen,
 * and wrong about one player, because GoldenEye does NOT render one player
 * full-frame. Measured on statue, dam and facility, every frame:
 *
 *     sl_vp: scale=(640,440) trans=(640,480) -> x[0,320] y[10,230] of 320x240
 *
 * i.e. a 320x220 letterbox inset ten rows from the top of a 320x240
 * framebuffer. Dropping it had two consequences, and the second is the one
 * that was reported:
 *
 *   1. every 3D pixel was stretched vertically by 240/220 and shifted up ten
 *      rows, and
 *   2. the world drew OVER the letterbox, which nothing else ever paints -
 *      so the full-screen overlays, which correctly cover exactly the
 *      viewport (the level-intro fade measures fillrect y[10.0,231.0], and
 *      the death overlay the same), left a strip of live world visible above
 *      and below them. That is the "gaps at the top and bottom" report: the
 *      overlay was never short, the world was too tall.
 *
 * Vp is s16 vscale[4] then s16 vtrans[4] in 2.2 fixed, so the rectangle is
 * trans +/- scale, quartered. The 2D path keeps the FULL window - texrect and
 * fillrect coordinates are framebuffer coordinates, not viewport ones - so
 * the viewport is switched on every 2D/3D transition alongside the ortho.
 *
 * SL_VP=0 restores the pre-B-046 behaviour from the same binary.
 *
 * ---- #45: the three rectangles -------------------------------------------
 *
 * Since #45 the window is no longer the rectangle the logical framebuffer is
 * stretched across. Three rectangles, all in GL window coordinates
 * (bottom-left origin), all derived by ONE helper (src/platform/sl_display.c,
 * sl_display_rects) from the window size, the logical framebuffer size the
 * list declares (g_scr_w x g_scr_h) and the selected aspect:
 *
 *   g_window_vp   the whole window, as GL reported it at the frame reset.
 *   g_content_vp  the selected aspect (4:3 / 16:9 / 32:9) fitted inside the
 *                 window, centred - pillarbox or letterbox bars outside it,
 *                 never a stretch. The 3D view fills THIS.
 *   g_win_vp      the SAFE rect: the logical 4:3 image fitted inside the
 *                 content rect, centred. This keeps its old name and its old
 *                 meaning - "the rectangle the logical framebuffer maps onto"
 *                 - so every formula below that scales logical coordinates
 *                 by g_win_vp[2] / g_scr_w keeps meaning exactly what it did.
 *                 At the 4:3 selection all three are the same rectangle and
 *                 nothing here changes by a pixel (the negative control).
 *   g_aspect_k    content width / safe width, >= 1 (1, 4/3, 8/3).
 *
 * How they are applied, and why the game does not need to know:
 *
 *   3D   the projection matrix the list loads gets its clip-space x divided
 *        by k (do_matrix), and the RSP viewport the list sets is mapped onto
 *        the safe rect and then widened by k about its own centre
 *        (vp_derive). The two cancel exactly over the safe rect - the image
 *        there is the 4:3 image, pixel for pixel - and the bands to either
 *        side receive the world the 4:3 frustum edge used to cut off. The
 *        vertical extent (fovy 60 over the same height) is untouched: this
 *        is a horizontal-plus widening, tan(hfov/2) = tan(30) * 4/3 * k.
 *   2D   the ortho spans the CONTENT rect but is scaled so that logical
 *        [0, g_scr_w] lands on the safe rect (mode2d_begin): every texrect,
 *        every HUD element, the watch face, the front end's text and the
 *        crosshair keep the 4:3 composition, centred and unstretched. The
 *        crosshair therefore stays over the aim ray without any game change,
 *        because the aim ray is a 4:3 screen-space fact and so is its image.
 *        A 2D primitive that spans the whole logical width - a fade, the
 *        letterbox strips, the sky's band (sky.c pins its edges to the
 *        viewport, :1300) - is a BACKDROP and is extended to the content
 *        edges (ext2d_*): the same plane, evaluated further out.
 *   scissor  mapped onto the safe rect like everything logical, with an edge
 *        that sits ON a framebuffer edge extended to the content edge, so
 *        the full-screen scissor never clips the bands and a per-room
 *        aperture (B-143) that reaches the screen edge stays conservative.
 *   pointer  sl_gfx_present_rect reports the SAFE rect, so the menu and
 *        watch pointers map window pixels into the same 4:3 image.
 *
 * The game's own projection, frustum planes, "on screen" flags, aim,
 * auto-aim and every script test stay at 4:3 at every aspect: src/game reads
 * none of this (tree-wide `grep -rn "sl_aspect\|sl_display" src/game` is
 * empty). What that costs - geometry the 4:3 traversal never submits cannot
 * appear in the bands - is measured and recorded on #45, not hidden. */
static int      g_win_vp[4];             /* the SAFE rect (see above) */
static int      g_window_vp[4];          /* the whole window, as GL was handed it */
static int      g_content_vp[4];         /* the selected aspect fitted in the window */
static double   g_aspect_k = 1.0;        /* content width / safe width */
static int      g_aspect_id;             /* the selection in force this frame */
static unsigned g_world_proj_addr;        /* #45 FOV: the world projection's address fr.c named, 0 = none */
static unsigned g_world_proj_addr2;       /* ... and the view-folded one bondview2.c named */
static int      g_proj_is_world;          /* the projection in force is the world's */
static int      g_viewmodel;              /* inside the first-person weapon's bracket ('SVM1'..'SVM0') */
static double   g_fov_s = 1.0;            /* the frame's FOV scale (sl_display_fov_scale) */
static int      g_vp_rect[4], g_vp_have; /* the game's viewport, in GL coords */
static int      g_vp_cur = -1;           /* 0 = window, 1 = game, -1 = unset */
static unsigned g_vp_applies;

static int vp_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_VP"); on = (v == NULL || *v != '0'); }
    return on;
}

/* #45. Derive the content and safe rects from the window, the logical size
 * and the selection. Called at the frame reset (once the window viewport is
 * known) and again whenever g_scr_w / g_scr_h change mid-list, so the fit
 * always describes the framebuffer the list is drawing into. The selection
 * is read once per frame here - a change in the Settings page lands on the
 * next frame drawn, and a frame never mixes two shapes. */
static void rects_update(void)
{
    struct sl_display_rects r;
    int aspect;

    aspect = sl_aspect_active() ? sl_aspect_ratio() : SL_ASPECT_4_3;
    g_fov_s = sl_display_fov_scale();        /* #45 FOV: 1.0 at the default */
    if (g_window_vp[2] <= 0 || g_window_vp[3] <= 0 || g_scr_w == 0 || g_scr_h == 0
        || !sl_display_rects(g_window_vp[2], g_window_vp[3], (int) g_scr_w, (int) g_scr_h, aspect, &r)) {
        memcpy(g_content_vp, g_window_vp, sizeof g_content_vp);
        memcpy(g_win_vp, g_window_vp, sizeof g_win_vp);
        g_aspect_k = 1.0;
        g_aspect_id = SL_ASPECT_4_3;
        return;
    }
    /* The helper speaks top-left origin (the pointer convention); GL's y
     * grows up, so the top edge becomes the distance from the bottom. */
    g_content_vp[0] = g_window_vp[0] + r.content[0];
    g_content_vp[1] = g_window_vp[1] + (g_window_vp[3] - (r.content[1] + r.content[3]));
    g_content_vp[2] = r.content[2];
    g_content_vp[3] = r.content[3];
    g_win_vp[0] = g_window_vp[0] + r.safe[0];
    g_win_vp[1] = g_window_vp[1] + (g_window_vp[3] - (r.safe[1] + r.safe[3]));
    g_win_vp[2] = r.safe[2];
    g_win_vp[3] = r.safe[3];
    g_aspect_k = r.k;
    g_aspect_id = aspect;
}

/* #45. The bars outside the content rect are painted black once per frame,
 * so a letterboxed or pillarboxed window shows black borders rather than the
 * backend's diagnostic clear tint (which stays INSIDE the content rect, where
 * "nothing drew here" is still worth seeing). Nothing to do when the content
 * rect is the window - the 4:3 window case, the negative control. */
static void bars_clear(void)
{
    int wx = g_window_vp[0], wy = g_window_vp[1], ww = g_window_vp[2], wh = g_window_vp[3];
    int cx = g_content_vp[0], cy = g_content_vp[1], cw = g_content_vp[2], ch = g_content_vp[3];
    if (cw <= 0 || ch <= 0 || (cx == wx && cy == wy && cw == ww && ch == wh))
        return;
    glEnable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if (cx > wx)               { glScissor(wx, wy, cx - wx, wh);                       glClear(GL_COLOR_BUFFER_BIT); }
    if (cx + cw < wx + ww)     { glScissor(cx + cw, wy, (wx + ww) - (cx + cw), wh);    glClear(GL_COLOR_BUFFER_BIT); }
    if (cy > wy)               { glScissor(wx, wy, ww, cy - wy);                       glClear(GL_COLOR_BUFFER_BIT); }
    if (cy + ch < wy + wh)     { glScissor(wx, cy + ch, ww, (wy + wh) - (cy + ch));    glClear(GL_COLOR_BUFFER_BIT); }
    glDisable(GL_SCISSOR_TEST);
}

/* The logical x range the CONTENT rect spans, given that logical [0, scr_w]
 * spans the safe rect: the safe rect is centred in the content rect, so the
 * bands add (k - 1) * scr_w / 2 on each side. Used by the 2D ortho and by the
 * backdrop extension. */
static double ext2d_left(void)  { return (double) g_scr_w * 0.5 * (1.0 - g_aspect_k); }
static double ext2d_right(void) { return (double) g_scr_w * 0.5 * (1.0 + g_aspect_k); }

/* GL's y grows UP and the N64's grows DOWN, so the top edge becomes the
 * distance from the BOTTOM. Scaled by the safe-rect/framebuffer ratio because
 * the window is not 320x240 - and then, #45, widened by k about the
 * viewport's own centre, which is what puts the 1/k-scaled projection's
 * [-1/k, 1/k] back onto the safe rect and its outer band onto the content
 * rect. For the full-width viewport GoldenEye sets (x[0,320]) the result IS
 * the content rect's width. */
static int g_vp_log[4];                  /* the game's viewport, logical l,t,r,b (#45 FOV) */

static void vp_derive(int l, int t, int r, int b)
{
    double sx, sy, cx, w;
    if (g_scr_w == 0 || g_scr_h == 0 || g_win_vp[2] <= 0 || g_win_vp[3] <= 0)
        return;
    sx = (double) g_win_vp[2] / (double) g_scr_w;
    sy = (double) g_win_vp[3] / (double) g_scr_h;
    if (r <= l || b <= t) return;
    g_vp_log[0] = l; g_vp_log[1] = t; g_vp_log[2] = r; g_vp_log[3] = b;
    cx = (double) g_win_vp[0] + ((double) l + (double) r) * 0.5 * sx;
    w  = (double) (r - l) * sx * g_aspect_k;
    g_vp_rect[0] = (int) (cx - w * 0.5 + 0.5);
    g_vp_rect[1] = g_win_vp[1] + (int) ((double) g_win_vp[3] - b * sy + 0.5);
    g_vp_rect[2] = (int) (w + 0.5);
    g_vp_rect[3] = (int) ((b - t) * sy + 0.5);
    g_vp_have = 1;
}

/* THE RDP SCISSOR RECTANGLE, IN WINDOW PIXELS.
 *
 * `ED setscissor` names its corners in the SAME space vp_derive reads - the
 * framebuffer the display list declares, top-left origin, y growing down -
 * so this is deliberately the same three lines against the same g_win_vp and
 * g_scr_w/g_scr_h rather than a second copy of the scaling formula. GL's
 * scissor box has a bottom-left origin, which is the flip in the y term.
 *
 * Called from OP_SETSCISS on every ordinary frame since B-143 (SL_SCISSOR=0
 * reverts), and from the B-101 witness pass; see sl_gfx_frame_dl. */
static void sciss_gl_apply(int l, int t, int r, int b)
{
    double sx, sy;
    int x, y, w, h;
    if (g_scr_w == 0 || g_scr_h == 0 || g_win_vp[2] <= 0 || g_win_vp[3] <= 0)
        return;
    if (wit_null()) return;            /* the null control; see wit_null */
    if (wit_shrink() > 0) {            /* the positive control; 0 normally */
        l += wit_shrink(); t += wit_shrink();
        r -= wit_shrink(); b -= wit_shrink();
    }
    if (r <= l || b <= t) return;
    sx = (double) g_win_vp[2] / (double) g_scr_w;
    sy = (double) g_win_vp[3] / (double) g_scr_h;
    x = g_win_vp[0] + (int) (l * sx + 0.5);
    y = g_win_vp[1] + (int) ((double) g_win_vp[3] - b * sy + 0.5);
    w = (int) ((r - l) * sx + 0.5);
    h = (int) ((b - t) * sy + 0.5);
    /* #45. An edge that sits on the framebuffer's edge extends to the content
     * rect's edge: the full-screen scissor must not clip the bands, and a
     * per-room aperture that reaches the screen edge can only be conservative
     * there. "On the edge" allows the one-pixel inset the room traversal's
     * root rectangle carries - bgUpdateCurrentPlayerScreenMinMax clamps the
     * screen box by bgViewRelated {1, 1, -1, -1} (bg.c:191, :5211-5262), so
     * the apertures of everything in view arrive as [1,10]-[319,230] of
     * 320x240 (measured, SL_SCISSOR_DBG). Interior edges stay where the
     * logical mapping puts them. At k == 1 both rects coincide and this
     * changes nothing. */
    if (g_aspect_k > 1.0) {
        int right = x + w;
        if (l <= 1) x = g_content_vp[0];
        if (r >= (int) g_scr_w - 1) right = g_content_vp[0] + g_content_vp[2];
        w = right - x;
        if (w <= 0) return;
    }
    if (getenv("SL_SCISSOR_DBG") != NULL) {
        static int left = 40;
        if (left > 0) { left--; fprintf(stderr, "sl_sciss_dbg: logical [%d,%d]-[%d,%d] of %ux%u -> gl %d,%d %dx%d (k=%.3f content x %d..%d)\n",
                                        l, t, r, b, g_scr_w, g_scr_h, x, y, w, h, g_aspect_k, g_content_vp[0], g_content_vp[0] + g_content_vp[2]); }
    }
    batch_end();               /* glEnable/glScissor are illegal in glBegin */
    glScissor(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
}

/* want: 1 = the game's viewport (3D), 0 = the whole window (2D). Illegal
 * between glBegin and glEnd, exactly like glTexEnv - callers close the batch. */
static void vp_use(int want)
{
    if (!vp_on() || !g_vp_have) return;
    if (want == g_vp_cur) return;
    g_vp_cur = want;
    g_vp_applies++;
    if (want)
        glViewport(g_vp_rect[0], g_vp_rect[1], g_vp_rect[2], g_vp_rect[3]);
    else
        /* #45: the 2D layer's viewport is the CONTENT rect; the ortho in
         * mode2d_begin is what maps logical [0, scr_w] onto the safe rect
         * inside it. Identical to the safe rect at k == 1. */
        glViewport(g_content_vp[0], g_content_vp[1], g_content_vp[2], g_content_vp[3]);
}

/* WHERE THE GAME IMAGE ACTUALLY LANDS IN THE WINDOW, in window pixels with the
 * top-left origin every pointer API uses.
 *
 * This is the ONE piece of presentation geometry the native menu pointer needs
 * (src/native/sl_menu_pointer.c), and it is deliberately the renderer's OWN
 * rectangle rather than a second copy of the scaling formula: g_win_vp is what
 * glGetIntegerv(GL_VIEWPORT) reported at the frame reset, i.e. the viewport the
 * 2D ortho is stretched across (glOrtho 0..g_scr_w, g_scr_h..0 at mode2d_begin).
 * If the presentation ever gains a letterbox or a pillarbox it gains it here,
 * and the pointer follows with no second edit. (#45 did exactly that: g_win_vp
 * is now the SAFE rect - the 4:3 image inside the selected-aspect content
 * rect - and this function needed no change. A pointer in a side band maps to
 * a fraction outside [0,1], which the callers already treat as "not over the
 * image".)
 *
 * GL's viewport origin is the BOTTOM-left, so the top edge is the distance from
 * the bottom subtracted from the window height - which is why the caller has to
 * supply win_h. It is not read from SDL here: this file has no SDL.
 *
 * NOT returned: the logical size. g_scr_w/g_scr_h describe the framebuffer the
 * display list declares, whereas the front end's cursor lives in the game's own
 * view rectangle (viGetViewLeft/Top/Width/Height). That is the game's fact to
 * state, not this file's, so the caller normalises against this rectangle and
 * maps into the view rectangle itself - see sl_menu_pointer.c.
 *
 * Returns 0 until a frame has established the viewport, and 0 for a degenerate
 * window; the caller must treat that as "no pointer this frame". */
int sl_gfx_present_rect(int win_h, int *x, int *y, int *w, int *h);
int sl_gfx_present_rect(int win_h, int *x, int *y, int *w, int *h)
{
    if (g_win_vp[2] <= 0 || g_win_vp[3] <= 0 || win_h <= 0)
        return 0;
    *x = g_win_vp[0];
    *y = win_h - (g_win_vp[1] + g_win_vp[3]);
    *w = g_win_vp[2];
    *h = g_win_vp[3];
    return 1;
}

/* THE CONTENT RECT the same way (#45: the selected aspect fitted in the
 * window; the safe rect above sits centred inside it, and the 2D ortho spans
 * it). The pointer layers confine the DRAWN cursor to this rectangle - the
 * cursor may cross the whole content rect, bands included - while the hit
 * tests keep the safe rect above. Identical to sl_gfx_present_rect at 4:3. */
int sl_gfx_content_rect(int win_h, int *x, int *y, int *w, int *h);
int sl_gfx_content_rect(int win_h, int *x, int *y, int *w, int *h)
{
    if (g_content_vp[2] <= 0 || g_content_vp[3] <= 0 || win_h <= 0)
        return 0;
    *x = g_content_vp[0];
    *y = win_h - (g_content_vp[1] + g_content_vp[3]);
    *w = g_content_vp[2];
    *h = g_content_vp[3];
    return 1;
}

static void mode2d_end(void)
{
    if (!g_2d_on) return;
    g_2d_on = 0;
    vp_use(1);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    /* The 2D ortho sets blending and depth behind rm_apply's back, so the
     * next triangle has to re-establish them rather than trust the cache. */
    rm_invalidate();
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(g_proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void mode2d_begin(void)
{
    fog_neutral();                   /* B-045 - the 2D ortho never fogs: its
                                      * vertices carry the CURRENT coordinate,
                                      * so pin it to zero rather than toggling
                                      * the enable (see fog_neutral) */
    if (g_2d_on) return;
    batch_end();
    /* B-048. Nothing in the 2D path emits a second texture coordinate, so a
     * unit left enabled by the last triangle would modulate every texrect by
     * whatever tile 1 held at (0,0). No 2D combiner in the game asks for
     * TEXEL1 - measured across the twenty-level sweep, every texrect draws
     * under a one-texel mux - so this is unconditional. */
    tex1_off();
    /* Enter the 2D path with the FIXED pipeline actually bound.
     *
     * The frame-boundary reset above sl_gfx_frame_dl's walk() is not
     * sufficient on its own, because it only runs BETWEEN frames. The 2D
     * overlay is drawn in the same list as the world and AFTER it, so a mode-5
     * draw earlier in the frame leaves the B-051 alpha program bound and
     * nothing in between takes it down.
     *
     * draw_texrect happens to self-heal: its textured branch calls
     * texenv_set(0|1), which sees g_texenv == 5 and unbinds. draw_fillrect
     * never calls texenv_set at all, so it inherited the program. MEASURED on
     * the facility intro, one binary, seed the only variable - the level
     * title's backing rect [30,203]-[219,219] is issued with colour 00000000,
     * alpha ZERO and therefore invisible under the fixed pipeline, and was
     * rasterised as an opaque black box through the world shader, which
     * computes its own alpha and never sees that vertex alpha. Boxed seed
     * fillrects-with-program-bound = 1, clean seed = 0, while the 225 title
     * texrects were identical in both and none of them ever had the program
     * bound. That is why the watch objectives screen - pure 2D, no geometry
     * before it - was already clean when this was not.
     *
     * Here rather than in draw_fillrect because this is the gate every 2D draw
     * passes through, so it also covers draw_texrect's untextured branch and
     * any 2D path added later. The early return above is safe: `grep -rn
     * "texenv_set(" src/` gives four call sites and only tex_apply's can pass
     * 5, and emit_tri calls mode2d_end before tex_apply - so no mode-5 bind
     * can happen while g_2d_on is 1.
     *
     * Costs nothing when the pipeline is already fixed function: texenv_set
     * returns immediately on an unchanged mode, and with texturing disabled
     * the texenv does not affect a fillrect's pixels at all. The only draws
     * this changes are the ones that were going through a shader they never
     * asked for. */
    texenv_set(0);
    g_2d_on = 1;
    g_2d_enters++;
    vp_use(0);                    /* 2D coordinates are framebuffer ones */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* N64 screen space has its origin top-left with y growing DOWN. Passing
     * bottom > top to glOrtho is what flips it, and it is the only place that
     * flip happens - the 3D path keeps the DL's own matrices untouched.
     *
     * #45: the viewport is the content rect, and the x range is widened
     * about the centre by k so that logical [0, scr_w] lands on the SAFE
     * rect - the 4:3 composition, centred, unstretched - while coordinates
     * outside it (the extended backdrops) reach the bands without being
     * clipped. At k == 1 this is glOrtho(0, scr_w, ...) exactly. */
    glOrtho(ext2d_left(), ext2d_right(), (double) g_scr_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    cull_set(0);                  /* quads are wound here, not by the game */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    rm_invalidate();
}

/* Establish the 3D modelview once per frame. It is the IDENTITY and stays
 * there: the DL's modelview is applied at vertex-load time instead (see
 * g_veye), which is what the RSP does and what lets a tube spanning two bone
 * matrices bend at the joint. A 01 matrix command therefore updates only the
 * software stack and never touches GL - it must not, since already-loaded
 * vertices keep the transform they were loaded with, and since a matrix
 * command can land mid-batch where glLoadIdentity would be illegal. */
static void gl_load_modelview(void)
{
    batch_end();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* ---- B-100. Removing GL's near clip: the obvious fix, MEASURED AND WRONG. --
 *
 * DEFAULT OFF. This is a bisect handle and a recorded refutation, not a fix.
 * The reasoning that produced it is written out because it is a good argument
 * that a measurement beat, and the next person to have the same idea should
 * meet the number before they spend a round on it.
 *
 * THE ARGUMENT, AND ITS FLAW, WHICH IS NOW NAMED. The RSP's PROGRAMMABLE clip
 * planes are four, and they are the sides: the G_MW_CLIP moveword takes
 * `clip -x`, `clip -y`, `clip +x`, `clip +y` and nothing else
 * (ucode05.txt:567-570; identically ucode05_old.txt:483-486). There is no z
 * entry to write in either revision of the note. GL, by contrast, clips
 * clip-space z against -w and +w unconditionally, so handed the game's own
 * projection it discards every triangle nearer to the eye than `znear` - and
 * znear is 5 eye units in Dam gameplay, 30 in the cinematic (viSetZRange at
 * bgfog.c:301 into guPerspectiveF at fr.c:709). That predicts exactly the
 * reported symptom.
 *
 * The flaw: "no PROGRAMMABLE z plane" is not "no z plane". The cartridge's
 * own microcode compiles a fixed near test in, at z = -w, and both rejects
 * and retessellates on it - D:\Projects\007\rsp\graphics\gmain.s:512-524 and
 * :784-794, worked out line by line in the nearwit_note block above. The two
 * sides clip against the SAME near plane. The measurement below had already
 * said so from the picture; the microcode now says so from the mechanism.
 *
 * THE POPULATION IS REAL. SL_NEARWIT counts it (see nearwit_note). On the Dam
 * truck cinematic, seed 4, up to 244 triangles PER FRAME lie wholly between
 * the eye and znear - w > 0 but z_clip < -w - and GL draws none of them.
 *
 * AND THE FIX IS STILL WRONG, which is the part that had to be measured.
 * GL_DEPTH_CLAMP (ARB_depth_clamp, core since 3.2) removes precisely the near
 * and far clip tests and clamps depth into the range instead. Turned on, it
 * closes the truck's exposed interior and the picture looks better. It also
 * moves that picture AWAY from the cartridge.
 *
 * Measured 2026-09-08, native against parallel_n64, paired ON TRUCK WORLD
 * POSITION rather than on frame index (native f779 against ROM f3932, prop 279
 * within 0.24 world units), both resampled to a common 320x240 grid and
 * classified by band rather than by whole-frame pixel count:
 *
 *                       sky        green
 *      cartridge       15.2%       29.9%
 *      clamp OFF       15.0%       24.9%     <- agrees with the ROM on sky
 *      clamp ON        14.1%       13.4%     <- and now the body is wrong too
 *
 * "sky" is the see-through measure - background showing where the truck body
 * should be - and the cartridge and the UNCLAMPED build agree on it to 0.2
 * points. So at the beat where the truck is closest, this build is not letting
 * more background through than the cartridge does, and switching the near clip
 * off does not move it toward the cartridge on either number.
 *
 * A SECOND MEASUREMENT, same day, same instrument. Over 3007 frames of the
 * recorded `dam` GAMEPLAY stream the INSIDE population is zero on every single
 * frame - znear of 5 is simply too close for that stream to reach. The
 * instrument was confirmed live on the same run rather than assumed (front
 * reaches 2850 and straddle reaches 3 in the same frames), because an
 * all-zero census is otherwise indistinguishable from a probe that never ran,
 * and this file has been burned by that three times.
 *
 * WHAT THAT LEAVES. Near-plane clipping is not what makes the truck expose its
 * interior, and it is not reachable at all on the recorded gameplay stream.
 *
 * AND THE CLOSE-WALL WITNESS, 2026-09-09, IS THE SAME ANSWER FROM THE OTHER
 * DIRECTION. On the owner's run 20260909-100313-lvl33 the see-through wedges
 * ARE this near clip's output, fitted rather than argued: for five triangles
 * across three marks, the measured sky/geometry boundary in the .bmp lands on
 * the z = -w cut of the recorded clip-space vertices to under 1.5 px over
 * seven sampled rows each (mark-011 room 115 dl 2000c6d0 +0x05c8 and +0x05f0,
 * mark-004 room 136 dl 2000e870 +0x0598 and +0x05a8, mark-003 room 132 dl
 * 2000a170 +0x0fb0). So the wedge is not a missing triangle, not a material,
 * and not a depth artefact - and, given the microcode above, not a
 * divergence either. Both machines cut in the same place.
 *
 * Anything still wrong at a close wall is therefore UPSTREAM of this stage:
 * what w is, not what is done with it. Do not spend another round here.
 * docs/backlog.md, B-100 and B-103.
 *
 * SL_DEPTH_CLAMP=1 turns it on for bisection. Unset and 0 are off, which is
 * the shipped behaviour and the one the measurement supports.
 * sl_gfx_depth_clamp_active() reports what actually happened, so a machine
 * whose driver refuses the enable says so instead of differing silently. */
#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP 0x864F
#endif
static int g_dclamp_state = -1;         /* -1 untried, 0 unavailable, 1 live */

int sl_gfx_depth_clamp_active(void);
int sl_gfx_depth_clamp_active(void)
{
    return g_dclamp_state > 0;
}

static void depth_clamp_init(void)
{
    const char *v;
    const GLubyte *ext;

    if (g_dclamp_state >= 0) return;
    g_dclamp_state = 0;
    v = getenv("SL_DEPTH_CLAMP");
    if (v == NULL || *v == '0' || *v == '\0') return;   /* the shipped path */
    /* Ask the driver rather than the header. mingw's <GL/gl.h> is a 1.1
     * header, so the token above is spelled out here; whether the enable is
     * honoured is a runtime fact and glGetError is what settles it. */
    ext = glGetString(GL_EXTENSIONS);
    if (ext == NULL || strstr((const char *) ext, "GL_ARB_depth_clamp") == NULL) {
        /* Core since 3.2, where the extension string may be absent from the
         * 1.x-style query. Try the enable anyway and let glGetError judge. */
        ext = NULL;
    }
    while (glGetError() != GL_NO_ERROR) { }     /* drain, so the test is ours */
    glEnable(GL_DEPTH_CLAMP);
    if (glGetError() != GL_NO_ERROR) {
        fprintf(stderr, "sl_near: GL_DEPTH_CLAMP refused by the driver"
                        " (GL \"%s\") - near clipping stays, B-100 will show\n",
                (const char *) glGetString(GL_VERSION));
        return;
    }
    g_dclamp_state = 1;
    fprintf(stderr, "sl_near: GL_DEPTH_CLAMP enabled%s - no near/far geometry"
                    " clip, matching the RSP's four side planes\n",
            ext != NULL ? " (ARB_depth_clamp advertised)" : " (core)");
}

/* #45. THE PROJECTION SEAM. g_proj = g_proj_game with clip-space x divided
 * by k - the four entries that feed clip x under v' = v * M are column 0,
 * i.e. linear indices 0, 4, 8, 12 (see ndc_account). Dividing them is a
 * post-scale S = diag(1/k, 1, 1, 1) on the OUTPUT, so it survives a later
 * G_MTX multiply on the projection stack (m * P * S) and it is exact: for
 * guPerspective(fovy, aspect) this is precisely what guPerspective(fovy,
 * aspect * k) would have produced (its x scale is cot(fovy/2) / aspect), and
 * for an orthographic load it is a centred x squeeze the widened viewport
 * undoes. Every mirror of GL's transform in this file reads g_proj, so the
 * NDC census, the clipper, the near-plane guard and the fog depth all see the
 * projection GL sees. At k == 1 g_proj is a copy of g_proj_game. */
/* #45 FIELD OF VIEW. The game names the player's WORLD projection to the
 * renderer (fr.c viSetupCurrentPlayerView, native arm: the physical address
 * gSPMatrix will carry), and only a load of THAT matrix gets the vertical
 * FOV scale s = tan(30) / tan(v_eff / 2) on clip x and y - a uniform
 * zoom-out that puts more world in the same viewport; the watch, the front
 * end and the title (their own matrices, other addresses) keep their
 * authored projection, so their pointer mirrors and their 2D text stay put.
 * s is 1.0 at the default setting, in the front end and in split-screen
 * (sl_display_fov_scale), so the default is exactly the pre-#45 arithmetic. */
void sl_gfx_note_world_projection(unsigned addr);
void sl_gfx_note_world_projection(unsigned addr)
{
    g_world_proj_addr = addr;
}

/* The second world projection: the view folded in (bondview2.c:8785, the
 * player's field_10E0), loaded by props, explosions and glass. */
void sl_gfx_note_world_projection2(unsigned addr);
void sl_gfx_note_world_projection2(unsigned addr)
{
    g_world_proj_addr2 = addr;
}

static void proj_apply_aspect(void)
{
    double sx = 1.0, sy = 1.0;
    memcpy(g_proj, g_proj_game, sizeof g_proj);
    if (g_aspect_k > 1.0)
        sx = 1.0 / g_aspect_k;
    if (g_proj_is_world && !g_viewmodel && g_fov_s != 1.0) {
        sx *= g_fov_s;
        sy  = g_fov_s;
    }
    if (sx != 1.0) {
        float f = (float) sx;
        g_proj[0]  *= f;
        g_proj[4]  *= f;
        g_proj[8]  *= f;
        g_proj[12] *= f;
    }
    if (sy != 1.0) {
        float f = (float) sy;
        g_proj[1]  *= f;
        g_proj[5]  *= f;
        g_proj[9]  *= f;
        g_proj[13] *= f;
    }
}

static void gl_load_projection(void)
{
    batch_end();
    mode2d_end();
    depth_clamp_init();
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(g_proj);
    glMatrixMode(GL_MODELVIEW);
}

#define RECT_COL_REPLACE   1   /* colour is exactly K; texture must not tint */
#define RECT_COL_MODULATE  2   /* colour is K and the texel scales it */
#define RECT_COL_LERP      3   /* colour is K lerped toward the texel by k    */

/* B-046. The constant tint the combiner applies to the pixel it produces.
 *
 * The RDP computes (a - b) * c + d per cycle. This file classifies rather than
 * emulates that, and until now it recognised exactly one shape - a ZERO
 * multiplier, whose output is exactly d (see g_cc_rgb_const). Everything else
 * fell through to opaque white, which silently DISCARDS every colour source a
 * combiner can name other than TEXEL0 and SHADE. That is the same class of
 * fault as B-035: the renderer honoured one input and quietly substituted
 * white for the rest.
 *
 * The shape added here is the ordinary modulate, (X - 0) * K + 0 with K a
 * colour register. Its result is X scaled by K, which GL_MODULATE reproduces
 * exactly when K is handed to glColor. Measured instances, all captured live
 * with SL_CC_DBG and all of them white today:
 *
 *   fc119623 ff2fffff  (TEXEL0 - 0) * PRIMITIVE + 0, both cycles
 *                      G_CC_MODULATEIA_PRIM. The death blood-drip overlay
 *                      (blood_animation.c:266, prim 96 00 00 b4 - measured
 *                      byte for byte in the histogram) and the GOLDENEYE
 *                      title text.
 *   fc129a25 ff37ffff  the ENVIRONMENT sibling of the same shape.
 *   fc169003 1f0c93ff  2-cycle: cycle 0 resolves a texel, cycle 1 scales
 *                      COMBINED by PRIMITIVE.
 *
 * Deliberately narrow. Only the exact shape (X - 0) * K + 0 is claimed, with
 * X naming TEXEL0 or COMBINED and K naming PRIMITIVE or ENVIRONMENT; the b
 * mux's 8..15 and the d mux's 7 are the constant zero (ucode05_old.txt "FC
 * rdp_setcombine"). A lerp toward ENVIRONMENT - (TEXEL0 - ENV) * SHADE + ENV,
 * which modelApplyRenderModeType3 emits and which measures 481 triangles a
 * frame on statue - is NOT this shape and is left exactly as it draws today.
 *
 * The cycle that produces the pixel is cycle 1 in 2-cycle mode and cycle 0
 * otherwise, so g_cycle_type is read at DRAW time rather than at FC time: B9
 * may set the cycle type either side of the FC that names the muxes. */
static unsigned g_cc_ra[2], g_cc_rb[2], g_cc_rc[2], g_cc_rd[2];
static unsigned char g_tint[3];
static int      g_tint_on, g_tint_shade;
static unsigned g_cc_tint_tris, g_cc_tint_rects;

/* SL_CC_TINT=0 restores the pre-B-046 behaviour so the two can be captured
 * back to back from one binary. Default on: the old behaviour is the defect. */
static int cc_tint_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CC_TINT");
                  on = (v == NULL || *v != '0'); }
    return on;
}

/* Fills out[3] and returns 1 when the pixel is a constant-scaled copy of its
 * texel. *shade says whether the scaled quantity still carries the vertex
 * colour - it does not when the output cycle reads TEXEL0 directly, and the
 * caller must then NOT multiply the vertex colour in on top. */
static int cc_tint(unsigned char out[3], int *shade)
{
    int cy = (g_cycle_type == 1) ? 1 : 0;        /* 1 == G_CYC_2CYCLE */
    unsigned a = g_cc_ra[cy], b = g_cc_rb[cy];
    unsigned c = g_cc_rc[cy], d = g_cc_rd[cy];

    *shade = 0;
    if (!cc_tint_on()) return 0;
    if (b < 8u || d != CC_A_ZERO) return 0;      /* subtrahend/addend not zero */
    if (a != CC_TEXEL0 && a != CC_COMBINED) return 0;
    if (c == CC_PRIM)     { out[0] = g_prim[0]; out[1] = g_prim[1];
                            out[2] = g_prim[2]; }
    else if (c == CC_ENV) { out[0] = g_env[0];  out[1] = g_env[1];
                            out[2] = g_env[2];  }
    else return 0;
    /* Reading COMBINED means cycle 0 produced it, and cycle 0 may have named
     * SHADE. Reading TEXEL0 means it did not. */
    if (a == CC_COMBINED)
        *shade = (g_cc_ra[0] == CC_SHADE || g_cc_rb[0] == CC_SHADE ||
                  g_cc_rc[0] == CC_SHADE || g_cc_rd[0] == CC_SHADE);
    return 1;
}

/* B-090. THE CONSTANT-FACTOR LERP TOWARD A COLOUR REGISTER, which is how
 * GoldenEye draws a SUBDUED background out of a bright image.
 *
 *      rgb = (TEXEL0 - K) * k + K          K a colour register, k a scalar
 *
 * ucode05_old.txt "FC rdp_setcombine" gives the RDP's colour as (a - b) * c + d
 * per cycle. When b and d name the SAME colour register and c names a per-draw
 * ALPHA scalar, the cycle is a plain linear blend from that register toward the
 * texel - the texel contributes only the fraction k, so a WHITE texel over a
 * dark register still comes out dark.
 *
 * MEASURED, the File Select background (front.c:2566-2571 sets the combiner and
 * the environment colour, title2.c:22 titleRenderFolderMenuBackgroundLines draws
 * it as 299 one-pixel-tall texrects of a 440x299 8-bit INTENSITY image):
 *
 *   sl_ccx: f2996 fc167e2c 33fdf6fb cyc=0 tr=299 prim=313131ff env=ffffff14
 *      rgb0=(TEX0-PRIM)*ENV.a+PRIM   a0=(0-0)*0+PRIM
 *
 * PRIMITIVE ramps 141414 -> 323232 down the screen (front.c:471-472) and ENV
 * alpha is 0x14, so the cartridge's background is a 20..50 grey gradient
 * carrying the gun barrel at 20/255 = 7.8% strength. Nothing here is a second
 * layer, a tint pass or an overlay: it is ONE draw whose combiner does the
 * darkening, and the intensity image really is bright.
 *
 * Before this, rect_colour recognised neither shape - g_cc_rgb_const wants a
 * ZERO multiplier (c >= 16) and c is 12 here, and cc_tint wants b and d to be
 * the constant zero while both name PRIMITIVE - so the rect fell to the final
 * `out = 255,255,255` and drew the raw intensity texel. That is the reported
 * defect: a background that should be dark grey rendering effectively white.
 *
 * DELIBERATELY NARROW, and narrower than cc_tint:
 *   - a must be TEXEL0. The lerp's far end is the sampled texel.
 *   - b must be a real colour register (b >= 8 is the constant ZERO) and must
 *     equal d, so the near end is ONE register rather than two.
 *   - c must be PRIMITIVE_ALPHA or ENVIRONMENT_ALPHA. SHADE_ALPHA is per-vertex
 *     and has no texture-environment constant to live in, so it is rejected and
 *     keeps drawing as it does today.
 * modelApplyRenderModeType3's (TEXEL0 - ENV) * SHADE + ENV is rejected by the c
 * test, as the note above cc_tint requires.
 *
 * Applies to rect_colour ONLY - texrects and fillrects. No triangle path
 * consults it, so world, character and viewmodel geometry are untouched by
 * construction. */
static float g_cc_lerp_k;                 /* the classified blend factor */
static unsigned g_cc_lerp_rects;

/* SL_CC_LERP=0 restores the pre-B-090 behaviour so both can be captured from
 * one binary. Default on: the old behaviour is the defect. */
static int cc_lerp_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CC_LERP");
                  on = (v == NULL || *v != '0'); }
    return on;
}

static int cc_lerp(unsigned char out[3])
{
    int cy = (g_cycle_type == 1) ? 1 : 0;        /* 1 == G_CYC_2CYCLE */
    unsigned a = g_cc_ra[cy], b = g_cc_rb[cy];
    unsigned c = g_cc_rc[cy], d = g_cc_rd[cy];
    unsigned kv;

    if (!cc_lerp_on()) return 0;
    if (a != CC_TEXEL0) return 0;
    if (b >= 8u) return 0;                       /* b 8..15 is the constant 0 */
    if (b != d) return 0;                        /* one register, both ends   */
    if (b == CC_PRIM)     { out[0] = g_prim[0]; out[1] = g_prim[1];
                            out[2] = g_prim[2]; }
    else if (b == CC_ENV) { out[0] = g_env[0];  out[1] = g_env[1];
                            out[2] = g_env[2];  }
    else return 0;
    if (c == CC_C_PRIM_ALPHA)     kv = g_prim[3];
    else if (c == CC_C_ENV_ALPHA) kv = g_env[3];
    else return 0;
    g_cc_lerp_k = (float) kv / 255.0f;
    return 1;
}

/* B-098. THE PER-TEXEL LERP FROM A COLOUR REGISTER TOWARD THE SHADE, which is
 * how GoldenEye draws a SKY: a base colour with clouds blended over it by the
 * cloud image's own intensity.
 *
 *      rgb = (SHADE - X) * TEXEL0 + X          X a colour register
 *      a   = SHADE.a
 *
 * X is the far end at texel 0 - the bare sky - and SHADE is the near end at
 * texel 1 - the lit cloud. The factor is the TEXEL, per channel and per pixel,
 * which is what distinguishes this from cc_lerp's per-draw scalar k and from
 * cc_tint's zero addend. All three are the RDP's one equation (a - b) * c + d
 * (ucode05_old.txt "FC rdp_setcombine"); they differ only in which slot is the
 * varying one.
 *
 * WHY IT MATTERS, and why "the sky is too dark" is this and not a tint.
 * GL_MODULATE computes SHADE * TEXEL0 and has no addend at all, so the whole
 * `X * (1 - TEXEL0)` term was dropped. X is the SKY BASE COLOUR, so the deficit
 * is largest exactly where the cloud image is darkest - i.e. over most of the
 * sky - and it is coloured, not neutral: Dam's X is 16,48,96, so dropping it
 * removes six times as much blue as red. A dark blue sky rendered without it
 * collapses toward black through neutral grey, which is the reported
 * "stormy twilight instead of blue".
 *
 * MEASURED IN THE CONSUMED BINARY, Dam, SL_CC_DBG=1, the combiner in force at
 * the sky draw:
 *
 *   sl_ccx: f0 fc40fe81 55fef97c cyc=0 tr=1 prim=00000000 env=103060ff tile=0
 *      rgb0=(SHADE-ENV)*TEX0+ENV  rgb1=(SHADE-ENV)*TEX0+ENV  a0=(0-0)*0+SHADE
 *
 * That word is not inferred from our own sky.c. `NTSC fog and sky.txt` (GE
 * Documentation/Background File Data/Fog Water and Sky, the "sample 'cloud'
 * mapping" listing at the end) records the cartridge's own sky display list,
 * and its combiner is literally `FC40FE81 55FEF97C` preceded by an
 * `FB......` SetEnvColor and followed by the `B4`/`B2` rdphalf pair that
 * carries the triangle. env=103060ff is Dam's fog-table row read back at draw
 * time (bgfog.c:184, red 0x10 green 0x30 blue 0x60 in the VERSION_US table
 * this build compiles).
 *
 * The SHADE end was already correct and is NOT what was wrong: the same run
 * measured the sky primitive's vertices at rgba=70,94,132 along the top edge
 * and 16,48,96 along the bottom, which is exactly skyChooseCloudVtxColour
 * (sky.c:186-188) evaluated at the two ends of its ramp - white cloud blended
 * in at the top, bare sky at the horizon. The cloud contribution was arriving;
 * the base underneath it was not.
 *
 * ALPHA is part of the same defect. The RDP's alpha here is (0 - 0) * 0 +
 * SHADE, a plain REPLACE with the vertex alpha, and sky.c:190 sets that to
 * 0xff - the sky is opaque. GL_MODULATE instead computes TEXEL0.a * SHADE.a,
 * so an IA cloud image made the sky partially TRANSPARENT over whatever the
 * backdrop held, darkening it a second time and independently.
 *
 * DELIBERATELY NARROW, the same discipline as cc_tint and cc_lerp:
 *   - a must be SHADE. The near end is the interpolated vertex colour.
 *   - c must be TEXEL0. The factor is the sampled texel, not a scalar.
 *   - b must be a real colour register (b >= 8 is the constant ZERO) and must
 *     equal d, so the far end is ONE register rather than two.
 * modelApplyRenderModeType3's (TEXEL0 - ENV) * SHADE + ENV has a and c the
 * other way round and is rejected by both tests, so it keeps drawing exactly
 * as it does today - the note above cc_tint requires that and it still holds.
 */
static int cc_skylerp_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CC_SKYLERP");
                  on = (v == NULL || *v != '0'); }
    return on;
}

static int cc_skylerp(unsigned char out[3])
{
    int cy = (g_cycle_type == 1) ? 1 : 0;        /* 1 == G_CYC_2CYCLE */
    unsigned a = g_cc_ra[cy], b = g_cc_rb[cy];
    unsigned c = g_cc_rc[cy], d = g_cc_rd[cy];

    if (!cc_skylerp_on()) return 0;
    if (a != CC_SHADE) return 0;
    if (c != CC_TEXEL0) return 0;
    if (b >= 8u) return 0;                       /* b 8..15 is the constant 0 */
    if (b != d) return 0;                        /* one register, both ends   */
    if (b == CC_PRIM)     { out[0] = g_prim[0]; out[1] = g_prim[1];
                            out[2] = g_prim[2]; }
    else if (b == CC_ENV) { out[0] = g_env[0];  out[1] = g_env[1];
                            out[2] = g_env[2];  }
    else return 0;
    return 1;
}

/* ---- TEMPORARY PROBE (B-046 investigation). SL_CC_DBG=1 to arm. ----------
 * A per-frame histogram keyed on the FC words in force at DRAW time, counting
 * triangles / texrects / fillrects separately and recording the colour
 * registers each was drawn with. Counters prove a command was consumed; this
 * says which combiner each PIXEL was produced under, which is what a "the
 * colour source was dropped" claim needs. Remove before landing. */
#define CCX_MAX 24
static unsigned g_ccx_w[CCX_MAX][2];
static unsigned g_ccx_n_tri[CCX_MAX], g_ccx_n_tr[CCX_MAX], g_ccx_n_fr[CCX_MAX];
static unsigned char g_ccx_prim[CCX_MAX][4], g_ccx_env[CCX_MAX][4];
static unsigned g_ccx_cyc[CCX_MAX], g_ccx_tex[CCX_MAX], g_ccx_n;

/* ---- B-051 blast-radius census. SL_CC_ADDEND=1. -------------------------
 *
 * The general fix for B-051 is to stop expressing the RDP's alpha equation as
 * a GL_MODULATE, because GL_MODULATE has no additive term and the RDP's does:
 * ucode05_old.txt's FC table gives alpha as (Aa - Ab) * Ac + Ad, and Ad1 = 7
 * is the constant ZERO. A combiner whose Ad is NOT 7 carries an addend that
 * this port currently drops.
 *
 * That change touches every textured surface in the game, so the question
 * that bounds its risk is: HOW MANY distinct combiner words actually carry a
 * non-zero addend? Every word that does not is unaffected by construction.
 * This counts them over the whole run rather than per frame, so a twenty-level
 * sweep produces one number.
 *
 * Counted at the point a combiner is SET, and separately at the point a
 * triangle is drawn under it, because a word that is set and never drawn is
 * not blast radius. */
#define CCA_MAX 64
static unsigned g_cca_w[CCA_MAX][2];
static unsigned g_cca_tris[CCA_MAX];
static unsigned g_cca_n, g_cca_overflow;
void sl_cc_addend_report(void);
static int cc_addend_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("SL_CC_ADDEND");
        on = (v != NULL && *v != '\0' && *v != '0');
        /* Reported at exit rather than per frame: the question is a per-RUN
         * census, and a twenty-level sweep should produce one number per
         * level rather than a million lines. */
        if (on) atexit(sl_cc_addend_report);
    }
    return on;
}
/* Ad0 is bits 11..9 of the LOWER word, Ad1 bits 2..0 - ucode05_old.txt.
 *
 * Only the addend of the cycle that REACHES MEMORY is blast radius: in
 * 2-cycle that is cycle 1, in 1-cycle it is cycle 0. Cycle 0's addend feeds
 * COMBINED and is already carried through the multiply, so counting it as
 * well makes the number look four times worse than it is - measured, on the
 * first version of this census: facility reported 4 words, of which two had
 * a cycle-1 addend of the constant 0 and are unaffected by construction. */
static void cca_note_tri(void)
{
    unsigned i, ad;
    if (!cc_addend_on()) return;
    ad = (g_cycle_type == 1) ? (g_cc_w1 & 7u) : ((g_cc_w1 >> 9) & 7u);
    if (ad == 7u) return;                    /* the addend is the constant 0 */
    for (i = 0; i < g_cca_n; i++)
        if (g_cca_w[i][0] == g_cc_w0 && g_cca_w[i][1] == g_cc_w1) {
            g_cca_tris[i]++;
            return;
        }
    if (g_cca_n >= CCA_MAX) { g_cca_overflow++; return; }
    g_cca_w[g_cca_n][0] = g_cc_w0;
    g_cca_w[g_cca_n][1] = g_cc_w1;
    g_cca_tris[g_cca_n] = 1;
    g_cca_n++;
}
void sl_cc_addend_report(void);
void sl_cc_addend_report(void)
{
    static const char *an[8] = {"COMB","TEX0","TEX1","PRIM","SHADE","ENV","1","0"};
    unsigned i;
    if (!cc_addend_on()) return;
    fprintf(stderr, "sl_cca: distinct combiner words with a NON-ZERO alpha"
                    " addend that reached a draw: %u (overflow=%u)\n",
            g_cca_n, g_cca_overflow);
    for (i = 0; i < g_cca_n; i++) {
        unsigned w0 = g_cca_w[i][0], w1 = g_cca_w[i][1];
        unsigned aa0 = (w0 >> 12) & 7u, ab0 = (w1 >> 12) & 7u;
        unsigned ac0 = (w0 >>  9) & 7u, ad0 = (w1 >>  9) & 7u;
        unsigned aa1 = (w1 >> 21) & 7u, ab1 = (w1 >>  3) & 7u;
        unsigned ac1 = (w1 >> 18) & 7u, ad1 =  w1        & 7u;
        fprintf(stderr, "sl_cca:   %08x %08x tris=%-8u"
                        " a0=(%s-%s)*%s+%s  a1=(%s-%s)*%s+%s\n",
                w0, w1, g_cca_tris[i],
                an[aa0], an[ab0], an[ac0], an[ad0],
                an[aa1], an[ab1], an[ac1], an[ad1]);
    }
}

static int ccx_dbg(void)
{
    static int on = -1;
    if (on < 0) on = (getenv("SL_CC_DBG") != NULL);
    return on;
}

static void ccx_note(int kind)
{
    unsigned i;
    if (!ccx_dbg()) return;
    for (i = 0; i < g_ccx_n; i++)
        if (g_ccx_w[i][0] == g_cc_w0 && g_ccx_w[i][1] == g_cc_w1) break;
    if (i == g_ccx_n) {
        if (g_ccx_n >= CCX_MAX) return;
        g_ccx_w[i][0] = g_cc_w0; g_ccx_w[i][1] = g_cc_w1;
        g_ccx_n_tri[i] = g_ccx_n_tr[i] = g_ccx_n_fr[i] = 0;
        g_ccx_n++;
    }
    if (kind == 0) g_ccx_n_tri[i]++;
    else if (kind == 1) g_ccx_n_tr[i]++;
    else g_ccx_n_fr[i]++;
    memcpy(g_ccx_prim[i], g_prim, 4);
    memcpy(g_ccx_env[i], g_env, 4);
    g_ccx_cyc[i] = g_cycle_type;
    g_ccx_tex[i] = (unsigned) g_tex_tile;
}
/* ---- end temporary probe ------------------------------------------------ */

/* Colour a 2D primitive draws with, derived from the classified combiner.
 * A zero multiplier makes the RDP's colour output exactly d, so d naming
 * PRIMITIVE or ENVIRONMENT means the texture must not tint the result - that
 * is the font case, and it is why texenv_set(1) exists. */
static int rect_colour(unsigned char out[4])
{
    int constant = 0;

    out[0] = out[1] = out[2] = out[3] = 255;
    if (g_cc_rgb_const == CC_PRIM) {
        out[0] = g_prim[0]; out[1] = g_prim[1]; out[2] = g_prim[2];
        constant = RECT_COL_REPLACE;
    } else if (g_cc_rgb_const == CC_ENV) {
        out[0] = g_env[0]; out[1] = g_env[1]; out[2] = g_env[2];
        constant = RECT_COL_REPLACE;
    } else {
        /* B-046. No zero multiplier, so the texel is not replaced - but it
         * may still be SCALED by a colour register, and until this existed
         * every such rect drew as plain white texel. The measured instance is
         * the death blood-drip overlay: G_CC_MODULATEIA_PRIM over a 4-bit
         * INTENSITY image (blood_animation.c:266,269), whose only colour is
         * the primitive's 96 00 00. White times an intensity ramp is exactly
         * the greyscale splat reported. */
        int shade;
        /* B-090 first: it wants b and d to NAME a register, cc_tint wants both
         * to be the constant zero, so the two are mutually exclusive and the
         * order is for readability rather than precedence. */
        if (cc_lerp(out)) {
            constant = RECT_COL_LERP;
            g_cc_lerp_rects++;
        } else if (cc_tint(out, &shade)) {
            constant = RECT_COL_MODULATE;
            g_cc_tint_rects++;
        }
    }
    if (g_cc_a_prim) out[3] = g_prim[3];
    return constant;
}

/* ---- #47 PART A. WHAT IS ON SCREEN, PER N64 TEXTURE NUMBER ---------------
 *
 * The provider's census counts UPLOADS - how many distinct images a run
 * replaced. That answers "did the provider work" and cannot answer the
 * owner's observation, "most of it looks the same", because an id replaced
 * once and drawn on a doorframe counts exactly as much as an id drawn across
 * the whole sky. This accumulates, per texture number, the SCREEN AREA of
 * the primitives that sample it, clipped to the viewport, so a frame's ids
 * can be RANKED by how much of the picture they paint.
 *
 * WHAT IT IS NOT, stated because the number invites the wrong reading: there
 * is no depth test here. It is projected, viewport-clipped primitive area -
 * overdraw counted, occlusion not - so it ranks and it bounds, it does not
 * measure visible pixels. SL_TEX_HILITE is the visible-pixel measurement and
 * the two are taken together so the estimate can be checked against it.
 *
 *   SL_TEX_COVERAGE=<file>        write the table here at report time
 *   SL_TEX_COVERAGE_FIRST=<n>     first DL frame counted (default 0)
 *   SL_TEX_COVERAGE_LAST=<n>      last DL frame counted (default: all)
 *
 * The frame window is how ONE settled pose is measured instead of a whole
 * run including its fade-in and its menus.
 */
#define TEXCOV_UNREG  SL_TEXPROV_MAX_ID     /* the bucket for "no texture id" */
struct sl_texcov_ent {
    double        area;          /* viewport-clipped primitive area, fb px */
    unsigned      prims;
    unsigned      w, h;          /* logical tile size last seen */
    unsigned char replaced;      /* the active set had a file for this id */
    unsigned char kind;          /* 1 geometry, 2 texrect, 3 both */
};
static struct sl_texcov_ent g_texcov[SL_TEXPROV_MAX_ID + 1u];
static double   g_texcov_tex, g_texcov_plain;
static unsigned g_texcov_frames, g_texcov_lastf = 0xffffffffu;

static int texcov_now(void)
{
    static long first = -2, last;
    if (!texcov_armed()) return 0;
    if (first == -2) {
        const char *a = getenv("SL_TEX_COVERAGE_FIRST");
        const char *b = getenv("SL_TEX_COVERAGE_LAST");
        first = (a != NULL) ? strtol(a, NULL, 10) : 0;
        last  = (b != NULL) ? strtol(b, NULL, 10) : 0x7fffffffL;
    }
    return (long) g_dl_frame >= first && (long) g_dl_frame <= last;
}

static void texcov_tick(void)
{
    if (g_dl_frame != g_texcov_lastf) { g_texcov_lastf = g_dl_frame;
                                        g_texcov_frames++; }
}

/* SL_TEX_COVERAGE_FRAME=1: the table describes ONE frame - cleared as each
 * frame starts, written out as the next one does. Without it the table is
 * the whole run (or the SL_TEX_COVERAGE_FIRST/_LAST window), which averages
 * a moving camera into something no single capture shows. A still pose reads
 * the same either way; a cutscene does not, which is why the switch exists. */
static int texcov_perframe(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_TEX_COVERAGE_FRAME");
                  on = (v != NULL && *v != '0'); }
    return on;
}

static void texcov_write(void)
{
    const char *p = getenv("SL_TEX_COVERAGE");
    FILE *cf;
    unsigned i;
    double fa;
    if (p == NULL || p[0] == '\0') return;
    cf = fopen(p, "wb");
    if (cf == NULL) return;
    fa = (double) g_scr_w * (double) g_scr_h
         * (double) (g_texcov_frames ? g_texcov_frames : 1u);
    fprintf(cf, "# frames=%u dl_frame=%u screen=%ux%u textured_area=%.1f"
                " untextured_area=%.1f frame_area=%.1f\n",
            g_texcov_frames, g_dl_frame, g_scr_w, g_scr_h,
            g_texcov_tex, g_texcov_plain, fa);
    fprintf(cf, "id,hex,area,share_of_frame,share_of_textured,"
                "prims,w,h,kind,replaced\n");
    for (i = 0; i <= SL_TEXPROV_MAX_ID; i++) {
        const struct sl_texcov_ent *c = &g_texcov[i];
        if (c->prims == 0u) continue;
        if (i == TEXCOV_UNREG) fprintf(cf, "-1,unregistered,");
        else                   fprintf(cf, "%u,%04x,", i, i);
        fprintf(cf, "%.1f,%.6f,%.6f,%u,%u,%u,%u,%u\n",
                c->area, c->area / fa,
                g_texcov_tex > 0.0 ? c->area / g_texcov_tex : 0.0,
                c->prims, c->w, c->h, c->kind, c->replaced);
    }
    fclose(cf);
}

/* Called as a DL frame begins. Publishes the frame that just ended, then
 * starts the next one empty. */
static void sl_texcov_frame_begin(void)
{
    if (!texcov_armed() || !texcov_perframe()) return;
    if (g_texcov_frames != 0u) texcov_write();
    memset(g_texcov, 0, sizeof g_texcov);
    g_texcov_tex = g_texcov_plain = 0.0;
    g_texcov_frames = 0u;
    g_texcov_lastf = 0xffffffffu;
}

/* `slot` is g_tex_slot - the cache entry the draw is bound to, or < 0 for an
 * untextured primitive. */
static void texcov_add(int slot, double area, int kind)
{
    unsigned id = TEXCOV_UNREG;
    struct sl_texcov_ent *c;
    if (area <= 0.0 || !texcov_now()) return;
    texcov_tick();
    if (slot < 0) { g_texcov_plain += area; return; }
    {   const struct sl_texent *e = &g_texcache[slot];
        if (e->cov_id != 0u) id = e->cov_id - 1u;
        c = &g_texcov[id];
        if (id != TEXCOV_UNREG) {
            c->w = e->w; c->h = e->h;
            if (e->rep_gen != 0u) c->replaced = 1;
        }
    }
    c->area += area;
    c->prims++;
    c->kind |= (unsigned char) kind;
    g_texcov_tex += area;
}

/* The area a triangle covers INSIDE the viewport, in framebuffer pixels.
 * Sutherland-Hodgman against the four NDC side planes, then the shoelace.
 * Without the clip a wall that extends far past the screen edge reports an
 * area many times the whole frame, which would rank the census by how far
 * offscreen a surface runs rather than by how much of it is seen. */
static double ndc_tri_area_clipped(float n[3][5])
{
    float px[16], py[16], qx[16], qy[16];
    int np = 3, nq, i, j, k;
    double a = 0.0;

    for (i = 0; i < 3; i++) { px[i] = n[i][0]; py[i] = n[i][1]; }
    for (k = 0; k < 4; k++) {
        nq = 0;
        for (i = 0; i < np; i++) {
            int i2 = (i + 1) % np;
            float d0, d1, t;
            switch (k) {
            case 0:  d0 = px[i]  + 1.0f; d1 = px[i2] + 1.0f; break;
            case 1:  d0 = 1.0f - px[i];  d1 = 1.0f - px[i2];  break;
            case 2:  d0 = py[i]  + 1.0f; d1 = py[i2] + 1.0f; break;
            default: d0 = 1.0f - py[i];  d1 = 1.0f - py[i2];  break;
            }
            if (d0 >= 0.0f && nq < 16) { qx[nq] = px[i]; qy[nq] = py[i]; nq++; }
            if ((d0 >= 0.0f) != (d1 >= 0.0f) && nq < 16) {
                t = d0 / (d0 - d1);
                qx[nq] = px[i] + t * (px[i2] - px[i]);
                qy[nq] = py[i] + t * (py[i2] - py[i]);
                nq++;
            }
        }
        np = nq;
        if (np < 3) return 0.0;
        for (j = 0; j < np; j++) { px[j] = qx[j]; py[j] = qy[j]; }
    }
    for (i = 0; i < np; i++) {
        int i2 = (i + 1) % np;
        a += (double) px[i] * (double) py[i2] - (double) px[i2] * (double) py[i];
    }
    if (a < 0.0) a = -a;
    /* NDC spans 2 x 2 over the whole viewport, so one NDC unit of area is
     * (w/2) * (h/2) framebuffer pixels. */
    return a * 0.5 * ((double) g_scr_w * 0.5) * ((double) g_scr_h * 0.5);
}

static void cover_account(float x0, float y0, float x1, float y1, int is_fill)
{
    float area;
    int post = (g_tris > 0);
    if (x0 < g_2d_bbox[0]) g_2d_bbox[0] = x0;
    if (y0 < g_2d_bbox[1]) g_2d_bbox[1] = y0;
    if (x1 > g_2d_bbox[2]) g_2d_bbox[2] = x1;
    if (y1 > g_2d_bbox[3]) g_2d_bbox[3] = y1;
    if (x0 < 0.0f) x0 = 0.0f;
    if (y0 < 0.0f) y0 = 0.0f;
    if (x1 > (float) g_scr_w) x1 = (float) g_scr_w;
    if (y1 > (float) g_scr_h) y1 = (float) g_scr_h;
    if (x1 <= x0 || y1 <= y0) return;
    area = (x1 - x0) * (y1 - y0);
    g_2d_cover += (double) area / (double) (g_scr_w * g_scr_h);
    if (post) {
        g_2d_post_geom_cover += (double) area / (double) (g_scr_w * g_scr_h);
        if (area > g_2d_big_area) {
            g_2d_big_area = area;
            g_2d_big[0] = x0; g_2d_big[1] = y0;
            g_2d_big[2] = x1; g_2d_big[3] = y1;
            g_2d_big_is_fill = is_fill;
            memcpy(g_2d_big_col, g_2d_cur_col, 4);
        }
    }
}

/* The pending E4/E5, now that B3 has supplied dsdx/dtdy. */
static void draw_texrect(void)
{
    float x0, y0, x1, y1, du, dv, u0, v0, u1, v1;
    unsigned tw = 0, th = 0;
    unsigned char col[4];
    GLuint name;
    int why = TR_OK, constant;
    int cursor = 0, sciss_was = 0;

    g_tr_pending = 0;

    /* Corner normalisation - correct under either the note's labelling or
     * gbi.h's, with a counter for which one this command actually used. */
    if (g_tr_ax > g_tr_bx)      g_tr_order_w0lr++;
    else if (g_tr_ax < g_tr_bx) g_tr_order_w0ul++;

    /* B-044. An INVERTED rectangle is not a small rectangle, it is no
     * rectangle at all: the RDP rasterises the spans from uly down to lry, so
     * lry < uly yields zero spans and the command draws nothing. Sorting the
     * corners with min/max - which is exactly what the normalisation below
     * does - turns such a command into a large VALID rectangle instead, and
     * because the glyph tiles wrap, the texture then repeats down it.
     *
     * GoldenEye emits this shape on purpose-built-wrong input.
     * textRenderGlyph's bottom-clip branch (src/game/textrelated.c:333)
     * computes lry as `clipY + clipHeight + text_y` and omits the * 4 that
     * every other coordinate in the same command carries, so a glyph
     * straddling the bottom edge of a text clip box arrives as lry ~= 200
     * against uly ~= 760. That is Rare's own arithmetic - the matching build
     * is byte-identical, so the cartridge issues the same command - and on
     * hardware it is invisible. Here it painted the clipped line repeated up
     * the screen: the watch equipment list showing a column of DETONATOR
     * while it scrolls. The defect is the renderer's tolerance, not the
     * game's arithmetic, so the correction belongs here (project rule 5).
     *
     * Which word holds which corner is decided per command from the X axis
     * rather than assumed. gbi.h:4602 (the plain-F3D branch this build
     * compiles) packs xh/yh - the LOWER RIGHT - into w0 and xl/yl into w1,
     * which is what 225 of 225 measured texrects already showed and what
     * ucode05.txt's E4 entry has transposed. Deriving it per command instead
     * of hard-coding it keeps that disagreement out of the decision: a
     * tree-wide `grep -rn gSPTextureRectangle src/` finds 19 call sites in 7
     * files outside src/gfx (debugmenu, spectrum, textrelated, title2, radar,
     * blood_animation, bondwalk2) and every one spells the X pair as x and
     * x + width, so the X ordering names the labelling and the Y is then
     * required to agree with it.
     *
     * SL_TR_INVERT=1 restores the old swap-and-draw for A/B capture. */
    if ((g_tr_ax > g_tr_bx && g_tr_ay < g_tr_by) ||
        (g_tr_ax < g_tr_bx && g_tr_ay > g_tr_by)) {
        static int allow = -1;
        if (allow < 0) {
            const char *v = getenv("SL_TR_INVERT");
            allow = (v != NULL && atoi(v) != 0);
        }
        g_tr_inverted++;
        if (!allow) return;
    }

    x0 = (float) (g_tr_ax < g_tr_bx ? g_tr_ax : g_tr_bx) / 4.0f;
    x1 = (float) ((g_tr_ax > g_tr_bx ? g_tr_ax : g_tr_bx) + 1) / 4.0f;
    y0 = (float) (g_tr_ay < g_tr_by ? g_tr_ay : g_tr_by) / 4.0f;
    y1 = (float) ((g_tr_ay > g_tr_by ? g_tr_ay : g_tr_by) + 1) / 4.0f;

    if (x1 <= x0 || y1 <= y0) { g_tr_reject[TR_DEGEN]++; return; }
    /* The pointer cursor's placement (see g_cur_ovr): the same rect, its
     * top-left moved to where the native layer put the pointer. The width
     * and height - and so the s/t sweep below - are the encoded ones. */
    if (g_cur_ovr) {
        float w = x1 - x0, h = y1 - y0;
        x0 = (float) g_cur_x4 / 4.0f;
        y0 = (float) g_cur_y4 / 4.0f;
        x1 = x0 + w;
        y1 = y0 + h;
        g_cur_ovr = 0;
        g_cur_applied++;
        cursor = 1;
    }
    if (x1 <= 0.0f || y1 <= 0.0f ||
        x0 >= (float) g_scr_w || y0 >= (float) g_scr_h)
        g_tr_offscreen++;

    name = tile_texture(g_tr_tile, &tw, &th, &why);
    if (name == 0) {
        g_tr_reject[why]++;
        tw = th = 1;
    }

    /* s/t are 10.5 (32 units per texel, same as vertex ST); dsdx/dtdy are
     * s5.10 (1024 == one texel per pixel). Different scales, same command. */
    u0 = (float) g_tr_s / 32.0f / (float) tw;
    v0 = (float) g_tr_t / 32.0f / (float) th;
    du = (float) g_tr_dsdx / 1024.0f / (float) tw;
    dv = (float) g_tr_dtdy / 1024.0f / (float) th;
    if (g_tr_flip) {
        /* E5 swaps which screen axis each texture axis follows. */
        u1 = u0 + (y1 - y0) * du;
        v1 = v0 + (x1 - x0) * dv;
    } else {
        u1 = u0 + (x1 - x0) * du;
        v1 = v0 + (y1 - y0) * dv;
    }

    constant = rect_colour(col);

    mode2d_begin();
    if (name) {
        if (!g_tex_gl_on) { glEnable(GL_TEXTURE_2D); g_tex_gl_on = 1; }
        if (name != g_tex_gl_bound) {
            glBindTexture(GL_TEXTURE_2D, name);
            g_tex_gl_bound = name;
            g_tex_binds++;
        }
        texenv_set(constant == RECT_COL_REPLACE ? 1 :
                   constant == RECT_COL_LERP    ? 6 : 0);
        if (constant == RECT_COL_LERP) cc_lerp_apply(g_cc_lerp_k);
        g_tr_textured++;
    } else if (g_tex_gl_on) {
        glDisable(GL_TEXTURE_2D);
        g_tex_gl_on = 0;
    }

    memcpy(g_2d_cur_col, col, 4);
    glColor4ub(col[0], col[1], col[2], col[3]);
    /* The placed cursor is drawn over the WHOLE content rect: the game's
     * scissor in force - in a level, [0,10]-[320,230] of 320x240 (measured,
     * SL_SCISSOR_DBG: gl 0,30 1280x660 on a 1280x720 window), so the top
     * and bottom 30 window pixels are outside it - is lifted for this ONE
     * quad and put back. Nothing else is drawn any differently. */
    if (cursor) {
        sciss_was = glIsEnabled(GL_SCISSOR_TEST) ? 1 : 0;
        if (sciss_was) glDisable(GL_SCISSOR_TEST);
    }
    glBegin(GL_QUADS);
    if (g_tr_flip) {
        glTexCoord2f(u0, v0); glVertex2f(x0, y0);
        glTexCoord2f(u0, v1); glVertex2f(x1, y0);
        glTexCoord2f(u1, v1); glVertex2f(x1, y1);
        glTexCoord2f(u1, v0); glVertex2f(x0, y1);
        g_tr_flips++;
    } else {
        glTexCoord2f(u0, v0); glVertex2f(x0, y0);
        glTexCoord2f(u1, v0); glVertex2f(x1, y0);
        glTexCoord2f(u1, v1); glVertex2f(x1, y1);
        glTexCoord2f(u0, v1); glVertex2f(x0, y1);
    }
    glEnd();
    if (cursor && sciss_was) glEnable(GL_SCISSOR_TEST);
    ccx_note(1);
    g_tr_drawn++;
    if (g_tris > 0) g_2d_post_geom_tr++; else g_2d_pre_geom_tr++;
    cover_account(x0, y0, x1, y1, 0);
    /* #47 PART A. The 2D half of the per-id coverage census - the HUD, the
     * menus and the gunsight reach the screen through here and nowhere near
     * emit_tri_slots. Same viewport clip cover_account applies. */
    if (texcov_armed()) {
        float cx0 = x0 < 0.0f ? 0.0f : x0, cy0 = y0 < 0.0f ? 0.0f : y0;
        float cx1 = x1 > (float) g_scr_w ? (float) g_scr_w : x1;
        float cy1 = y1 > (float) g_scr_h ? (float) g_scr_h : y1;
        if (cx1 > cx0 && cy1 > cy0)
            texcov_add(name ? g_tex_slot : -1,
                       (double) (cx1 - cx0) * (double) (cy1 - cy0), 2);
    }
}

static void draw_fillrect(int ix0, int iy0, int ix1, int iy1)
{
    float x0, y0, x1, y1;

    if (ix1 < ix0 || iy1 < iy0) { g_fr_degen++; return; }
    g_cur_ovr = 0;                          /* a cursor tag names a texrect, never a fill */
    /* zbufClearCurrentPlayer points the colour image at the z buffer and
     * fills it. Drawing that would paint the whole screen with a depth
     * pattern, so it is skipped rather than merely mis-coloured. */
    if (g_cimg_is_z) { g_fr_zskip++; return; }

    x0 = (float) ix0; y0 = (float) iy0;
    x1 = (float) (ix1 + 1); y1 = (float) (iy1 + 1);
    /* #45. A fill that spans the whole logical width is a backdrop - the
     * full-screen clear, a fade, the letterbox strips, the fog-colour sky
     * fill (sky.c:326) - and covers the content rect's bands too. A fill
     * that reaches only one edge is left alone: it may be a HUD element
     * anchored to that edge, and the safe rect is where the HUD lives. */
    if (g_aspect_k > 1.0 && ix0 <= 0 && ix1 >= (int) g_scr_w - 1) {
        x0 = (float) ext2d_left();
        x1 = (float) ext2d_right();
        g_fr_extended++;
    }

    mode2d_begin();
    if (g_tex_gl_on) { glDisable(GL_TEXTURE_2D); g_tex_gl_on = 0; }
    if (g_cycle_type == 3) {                /* G_CYC_FILL: the fill register */
        memcpy(g_2d_cur_col, g_fill, 4);
        g_fr_filled++;
    } else {                                /* everything else: the combiner */
        /* A fillrect has no texel for a modulate combiner to scale, so the
         * B-046 tint is deliberately NOT taken here - the rect keeps drawing
         * exactly as it did. Every full-screen fill GoldenEye issues spells
         * G_CC_PRIMITIVE (textrelated.c:184,199), the zero-multiplier shape
         * rect_colour already resolved, so this branch loses nothing that was
         * measured; the counter says if that ever stops being true. */
        int rc = rect_colour(g_2d_cur_col);
        if (rc == RECT_COL_MODULATE || rc == RECT_COL_LERP) {
            g_2d_cur_col[0] = g_2d_cur_col[1] = g_2d_cur_col[2] = 255;
            g_fr_tintskip++;
        }
        g_fr_combined++;
    }
    glColor4ub(g_2d_cur_col[0], g_2d_cur_col[1],
               g_2d_cur_col[2], g_2d_cur_col[3]);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0);
    glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
    ccx_note(2);
    g_fr_drawn++;
    if (g_tris > 0) g_2d_post_geom_fr++; else g_2d_pre_geom_fr++;
    cover_account(x0, y0, x1, y1, 1);
}

/* ===================== B-101: raw RDP triangles (the sky) ================
 *
 * THE DEFECT. Dam's sky was absent in the intro and in play. It is not a fog
 * value, a far plane, or a missing texture: the sky's triangles never reached
 * the framebuffer because this interpreter discarded every word of them.
 *
 * GoldenEye does not draw the sky with gSPVertex/gSP1Triangle. sky.c builds
 * RDP triangle commands BY HAND and pushes them through the ucode's
 * rdphalf mechanism, two words at a time. ucode05.txt is explicit, and names
 * this exact caller:
 *
 *   "B2 rsp_uc05_rdphalf_cont ... Usage unknown. Used by sky and water
 *    generators in GE. Follows B4 rdphalf_1 command. Acts as an indicator
 *    that the command continues with the given lower word. This is used only
 *    when operators longer than 64bits are in use."
 *
 * and gives the assembled form for the very pattern sky.c emits (its worked
 * example opens `B4000000 CE80....` / `B2000000 ....`, i.e. an 0xCE
 * shade-texture triangle). `G_TRI_SHADE_TXTR` is 0xCE at gbi.h:219.
 *
 * WHAT THIS BUILD DID WITH THEM. B4/B3 were implemented only as texrect s/t
 * operands and ignored unless a texrect was pending; 0xB2 was not decoded at
 * all and fell to the unknown-opcode counter. MEASURED on dam, direct boot,
 * SL_DL_CENSUS=1, every frame:
 *
 *   sl_ops: unknown=19 distinct=1  in-ucode05=19  off-command-set=0
 *   sl_ops: top: B2=19
 *   sl_2d: texrect cmds=0 drawn=0 ... orphan-halves=21
 *
 * 19 + 21 = 40 words, and 40 is exactly one 0xCE triangle: 8 edge + 16 shade
 * + 16 texture. texrect cmds=0 on those frames, so all 21 halves were the
 * sky's. Facility measures unknown=0, orphan-halves=0 - consistent, because
 * its environment row declares clouds=0 (bgfog.c:131) and sky.c:320 takes the
 * fill-rectangle path without ever building a triangle. Facility is therefore
 * NOT a witness for this code; dam is.
 *
 * UPSTREAM OR OURS. Ours. src/game/sky.c is byte-identical to the upstream
 * decomp's (md5 a63ce4e60936e0cbab1e067d71926347 against
 * D:\Projects\007\src\game\sky.c), so the cartridge issues these same words
 * and the RSP assembles them into triangles the RDP rasterises. Nothing in
 * src/game is wrong. A tree-wide `grep -rn RDPHALF src/` finds hits in
 * src/game/sky.c ONLY - src/gfx had no decoder to be wrong.
 *
 * THE DECODE. Words arrive in order and are simply concatenated; the first
 * carries the RDP command byte, which names the block layout and hence the
 * length. Rather than invert the RDP's edge walk, the three vertices are read
 * straight back out of it:
 *
 *   w0: cmd | lft<<23 | tile<<16 | YL      w1: YM<<16 | YH   (all 10.2)
 *   then XL, DxLDy, XH, DxHDy, XM, DxMDy               (all s15.16)
 *
 * YH/YM/YL are the three vertex heights, sorted. XL is emitted as the middle
 * vertex's x verbatim (sky.c:1696 passes `sp480->unk28 * 0.25f`), and XH is
 * the major edge biased back to the integer scanline (sky.c pre-subtracts
 * DxHDy * frac(YH) at :1288), so the top vertex is XH + DxHDy*frac and the
 * bottom is that plus DxHDy*(YL-YH). No inversion, no least squares.
 *
 * Attributes are planes, so each is evaluated at each vertex directly:
 *
 *   A(x,y) = A + DaDe*(y - yH) + DaDx*(x - xEdge(y))
 *
 * which is exact at all three corners and needs only the De and Dx gradients.
 * Each attribute's integer halves sit four words above its fraction halves,
 * two attributes to a word - the shape sky.c:1820-1842 builds by masking
 * 0xffff0000 for one group and shifting 0x0000ffff for the other.
 *
 * S and T are divided by W by the RDP per pixel. GL is asked for the same
 * thing rather than an approximation of it: glTexCoord4f carries the
 * homogeneous triple and GL performs the identical projective interpolation,
 * so a sky quad seen at a glancing angle is textured correctly rather than
 * affinely.
 *
 * The 2D ortho is the right target: mode2d_begin puts coordinates in
 * framebuffer space (which is what the RDP words already are), turns culling
 * off, and turns the depth test off. skyRender runs before bgLevelRender
 * (lv.c:741), so the sky lands first and the world draws over it - the
 * cartridge's own order.
 *
 * SL_RDPTRI=0 restores the discard from the same binary.
 */
#define OP_RDPHALF_CONT 0xB2

static unsigned g_rh_w[64];         /* assembled words of the pending command */
static unsigned g_rh_n;
static unsigned g_rh_tris, g_rh_drawn, g_rh_dropped, g_rh_notri, g_rh_overflow;
static unsigned g_rh_skylerp;    /* B-098: drawn under the classified sky lerp */
static unsigned g_rh_dbg_left = 6;      /* SL_RDPTRI_DBG print budget */

static int rdptri_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_RDPTRI"); on = (v == NULL || *v != '0'); }
    return on;
}

/* Two attributes share a word pair: the INTEGER halves live in wi and the
 * FRACTION halves in wf, high half first in both. Recombining the matching
 * halves gives back the s15.16 value the RDP interpolates. */
static float rh_hi(unsigned wi, unsigned wf)
{
    return (float) (int) ((wi & 0xffff0000u) | ((wf >> 16) & 0xffffu)) / 65536.0f;
}

static float rh_lo(unsigned wi, unsigned wf)
{
    return (float) (int) (((wi & 0xffffu) << 16) | (wf & 0xffffu)) / 65536.0f;
}

static float rh_clamp255(float v)
{
    return v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
}

/* How many words the command that has begun in g_rh_w[0] will occupy, or 0 if
 * it is not an RDP triangle and we should not be collecting it. */
static unsigned rh_need(unsigned cmd)
{
    if (cmd < 0xc8u || cmd > 0xcfu) return 0u;
    return 8u + ((cmd & 4u) ? 16u : 0u) + ((cmd & 2u) ? 16u : 0u)
              + ((cmd & 1u) ? 4u : 0u);
}

static void draw_rdp_tri(void)
{
    /* THE PRIMITIVE IS NOT ALWAYS A TRIANGLE.
     *
     * An RDP "triangle" carries THREE independent edges - the major edge H
     * spanning YH..YL, and two minor edges, M over YH..YM and L over YM..YL.
     * When the minor pair is collinear the shape is a triangle, but nothing
     * requires that, and GoldenEye's skyRenderFull deliberately exploits it:
     * it sets XH to the left screen edge and XM to the right (sky.c:2209 and
     * :2212, both with a zero slope) and covers the whole sky band with ONE
     * primitive. Reading it as a triangle drew half the band and left the
     * other half showing the backdrop - which is what "Dam has no sky" looked
     * like even after the words were being decoded at all.
     *
     * So the boundary is walked as a polygon instead, which subsumes the
     * triangle case: for a genuine triangle P0==P1 and P2==P3 and the fan
     * below degenerates to exactly three distinct corners.
     *
     *      P0 --------- P1        P0,P5 on the major edge
     *      |             |        P1,P2 on minor M   (YH..YM)
     *      P5 -- ... -- P2/P3     P3,P4 on minor L   (YM..YL)
     *             P4
     */
    float vx[6], vy[6], ex[6], ey[6];
    float col[6][4], st[6][3];
    unsigned cmd  = g_rh_w[0] >> 24;
    unsigned tile = (g_rh_w[0] >> 16) & 7u;
    int   shaded  = (cmd & 4u) != 0;
    int   textured= (cmd & 2u) != 0;
    unsigned sb   = 8u;                       /* shade block base            */
    unsigned tb   = 8u + (shaded ? 16u : 0u); /* texture block base          */
    float yH, yM, yL, XL, DxLDy, XH, DxHDy, XM, DxMDy, xTop, xTopM, frac;
    unsigned tw = 1, th = 1;
    GLuint name = 0;
    int why = TR_OK, i;
    const int NV = 6;

    g_rh_tris++;

    /* 10.2 fixed. YM/YH are full 16-bit fields; YL shares its word with the
     * tile and level bits and so is 14. sky.c clamps every y into the
     * viewport (sky.c:1301) before it gets here, so none of them is
     * negative. */
    yH = (float) (int) (short) (g_rh_w[1] & 0xffffu)          / 4.0f;
    yM = (float) (int) (short) ((g_rh_w[1] >> 16) & 0xffffu)  / 4.0f;
    yL = (float) (int) (g_rh_w[0] & 0x3fffu)                  / 4.0f;

    XL    = (float) (int) g_rh_w[2] / 65536.0f;
    DxLDy = (float) (int) g_rh_w[3] / 65536.0f;
    XH    = (float) (int) g_rh_w[4] / 65536.0f;
    DxHDy = (float) (int) g_rh_w[5] / 65536.0f;
    XM    = (float) (int) g_rh_w[6] / 65536.0f;
    DxMDy = (float) (int) g_rh_w[7] / 65536.0f;

    /* H and M both start at YH, and sky.c biases both back to the integer
     * scanline there (it subtracts Dx*frac(YH) at sky.c:1287-1289), so undo
     * it on both. L starts at YM and is emitted unbiased. */
    frac  = yH - (float) (int) yH;
    xTop  = XH + DxHDy * frac;
    xTopM = XM + DxMDy * frac;

    vx[0] = xTop;                        vy[0] = yH;
    vx[1] = xTopM;                       vy[1] = yH;
    vx[2] = xTopM + DxMDy * (yM - yH);   vy[2] = yM;
    vx[3] = XL;                          vy[3] = yM;
    vx[4] = XL    + DxLDy * (yL - yM);   vy[4] = yL;
    vx[5] = xTop  + DxHDy * (yL - yH);   vy[5] = yL;

    /* #45. The sky and sea bands are BACKDROPS pinned to the viewport's
     * edges: sky.c clamps every x into [viewleft, viewleft + width) (:819,
     * :1300) and skyRenderFull sets XH to the left edge and XM to the right
     * (:2209, :2212). A corner that sits on a framebuffer edge is moved out
     * to the content rect's edge BEFORE the attributes are evaluated, so the
     * same planes - colour, and the RDP's per-pixel s/w, t/w, 1/w - are
     * simply evaluated further along: a plane's perspective projection is
     * affine in screen space, so this is the sky plane continued, not a
     * stretch. An interior corner is never moved. Nothing happens at k == 1. */
    /* ex/ey: where each corner's ATTRIBUTES are evaluated - the game's own
     * screen space, in which the planes below are stated. vx/vy: where the
     * corner is DRAWN. They differ only under #45 (below). */
    for (i = 0; i < NV; i++) { ex[i] = vx[i]; ey[i] = vy[i]; }
    if (g_aspect_k > 1.0 || (g_proj_is_world && g_fov_s != 1.0)) {
        int moved = 0;
        int edge[6];
        float cx, cy, t_vp, b_vp, s = 1.0f;
        /* the edges are judged BEFORE the FOV scale moves the corners */
        t_vp = (g_vp_log[3] > g_vp_log[1]) ? (float) g_vp_log[1] : 0.0f;
        b_vp = (g_vp_log[3] > g_vp_log[1]) ? (float) g_vp_log[3] : (float) g_scr_h;
        cx = (g_vp_log[2] > g_vp_log[0]) ? ((float) g_vp_log[0] + (float) g_vp_log[2]) * 0.5f : (float) g_scr_w * 0.5f;
        cy = (g_vp_log[3] > g_vp_log[1]) ? (t_vp + b_vp) * 0.5f : (float) g_scr_h * 0.5f;
        for (i = 0; i < NV; i++) {
            edge[i] = 0;
            if (vx[i] <= 0.5f)                        edge[i] |= 1;
            else if (vx[i] >= (float) g_scr_w - 1.0f) edge[i] |= 2;
            if (vy[i] <= t_vp + 0.5f)                 edge[i] |= 4;
            else if (vy[i] >= b_vp - 0.5f)            edge[i] |= 8;
        }
        /* #45 FIELD OF VIEW: the sky is built in the game's 60-degree screen
         * space; the world projection is zoomed out by s about the viewport
         * centre, so the sky's corners are DRAWN through the same zoom - and
         * a corner that sat on an edge is then pushed back out to the edge
         * (the sky continues past it), exactly as the aspect bands are
         * covered. The attributes are evaluated at the corner's position in
         * the game's space (the zoom undone), so an extended corner samples
         * the same plane further out and a zoomed one samples what it had. */
        if (g_proj_is_world && g_fov_s != 1.0) {
            s = (float) g_fov_s;
            for (i = 0; i < NV; i++) {
                vx[i] = cx + (vx[i] - cx) * s;
                vy[i] = cy + (vy[i] - cy) * s;
            }
        }
        /* Vertically the edge is the VIEWPORT's, never the framebuffer's:
         * GoldenEye draws its one player into y[10,230] of 240 and paints
         * the ten rows above and below black itself (B-046), and that
         * letterbox is part of the accepted 4:3 image. Pushing a sky corner
         * to row 0 painted the sky over the top strip at every aspect but
         * 4:3 (owner replay 2026-09-18: "a light strip across the top"). */
        for (i = 0; i < NV; i++) {
            if (edge[i] & 1)      { vx[i] = (float) ext2d_left();  moved = 1; }
            else if (edge[i] & 2) { vx[i] = (float) ext2d_right(); moved = 1; }
            if (edge[i] & 4)      { vy[i] = t_vp;                  moved = 1; }
            else if (edge[i] & 8) { vy[i] = b_vp;                  moved = 1; }
            ex[i] = cx + (vx[i] - cx) / s;
            ey[i] = cy + (vy[i] - cy) / s;
        }
        if (moved) g_rh_extended++;
    }

    for (i = 0; i < NV; i++) {
        col[i][0] = col[i][1] = col[i][2] = col[i][3] = 255.0f;
        st[i][0] = st[i][1] = 0.0f; st[i][2] = 1.0f;
    }

    /* A(x,y) = A + DaDe*(y-yH) + DaDx*(x - xEdge(y)), xEdge(y) walking the
     * major edge from the top vertex - at the corner's position in the
     * game's own screen space (ex/ey). */
#define RH_EVAL(A, DADX, DADE, VI) \
    ((A) + (DADE) * (ey[VI] - yH) \
         + (DADX) * (ex[VI] - (xTop + DxHDy * (ey[VI] - yH))))

    if (shaded) {
        float R    = rh_hi(g_rh_w[sb+0], g_rh_w[sb+4]);
        float G    = rh_lo(g_rh_w[sb+0], g_rh_w[sb+4]);
        float B    = rh_hi(g_rh_w[sb+1], g_rh_w[sb+5]);
        float A    = rh_lo(g_rh_w[sb+1], g_rh_w[sb+5]);
        float DRDx = rh_hi(g_rh_w[sb+2], g_rh_w[sb+6]);
        float DGDx = rh_lo(g_rh_w[sb+2], g_rh_w[sb+6]);
        float DBDx = rh_hi(g_rh_w[sb+3], g_rh_w[sb+7]);
        float DADx = rh_lo(g_rh_w[sb+3], g_rh_w[sb+7]);
        float DRDe = rh_hi(g_rh_w[sb+8], g_rh_w[sb+12]);
        float DGDe = rh_lo(g_rh_w[sb+8], g_rh_w[sb+12]);
        float DBDe = rh_hi(g_rh_w[sb+9], g_rh_w[sb+13]);
        float DADe = rh_lo(g_rh_w[sb+9], g_rh_w[sb+13]);

        for (i = 0; i < NV; i++) {
            col[i][0] = rh_clamp255(RH_EVAL(R, DRDx, DRDe, i));
            col[i][1] = rh_clamp255(RH_EVAL(G, DGDx, DGDe, i));
            col[i][2] = rh_clamp255(RH_EVAL(B, DBDx, DBDe, i));
            col[i][3] = rh_clamp255(RH_EVAL(A, DADx, DADe, i));
        }
    }

    if (textured) {
        float S    = rh_hi(g_rh_w[tb+0], g_rh_w[tb+4]);
        float T    = rh_lo(g_rh_w[tb+0], g_rh_w[tb+4]);
        float W    = rh_hi(g_rh_w[tb+1], g_rh_w[tb+5]);
        float DSDx = rh_hi(g_rh_w[tb+2], g_rh_w[tb+6]);
        float DTDx = rh_lo(g_rh_w[tb+2], g_rh_w[tb+6]);
        float DWDx = rh_hi(g_rh_w[tb+3], g_rh_w[tb+7]);
        float DSDe = rh_hi(g_rh_w[tb+8], g_rh_w[tb+12]);
        float DTDe = rh_lo(g_rh_w[tb+8], g_rh_w[tb+12]);
        float DWDe = rh_hi(g_rh_w[tb+9], g_rh_w[tb+13]);

        for (i = 0; i < NV; i++) {
            st[i][0] = RH_EVAL(S, DSDx, DSDe, i);
            st[i][1] = RH_EVAL(T, DTDx, DTDe, i);
            st[i][2] = RH_EVAL(W, DWDx, DWDe, i);
        }
        name = tile_texture(tile, &tw, &th, &why);
        if (tw == 0) tw = 1;
        if (th == 0) th = 1;
    }
#undef RH_EVAL

    /* Degenerate spans draw nothing on the RDP either - it rasterises from
     * YH down to YL, so an empty or inverted range is no triangle at all. */
    if (yL <= yH) { g_rh_dropped++; return; }

    mode2d_begin();
    if (name) {
        unsigned char k[3];
        if (!g_tex_gl_on) { glEnable(GL_TEXTURE_2D); g_tex_gl_on = 1; }
        if (name != g_tex_gl_bound) {
            glBindTexture(GL_TEXTURE_2D, name);
            g_tex_gl_bound = name;
            g_tex_binds++;
        }
        /* B-098. Honour the combiner actually in force rather than assuming
         * one. The comment that used to sit here called GL_MODULATE "the
         * sky's combiner"; the sky's combiner is measurably
         * (SHADE - ENV) * TEXEL0 + ENV, and GL_MODULATE drops the ENV term -
         * i.e. the entire sky base colour. See cc_skylerp above.
         *
         * Anything this does not classify keeps the previous behaviour
         * exactly, so a raw RDP triangle emitted under some other combiner
         * draws as it drew before. */
        if (cc_skylerp(k)) {
            texenv_set(7);
            /* AFTER texenv_set, the ordering rule aeq_apply_env and
             * cc_lerp_apply both state: the environment it configures has to
             * be the current one. Through the ONE mirror - see envcol_set.
             * A private value cache here is exactly the grey-sky bug: the
             * water's mode 8 rewrote the register every frame, the fog row's
             * K never moved, so a "has my value changed" test skipped the
             * push forever while the register held the animated fraction. */
#if defined(SL_TEXENV_COMBINE) && defined(GL_INTERPOLATE) && defined(GL_CONSTANT)     && defined(GL_SOURCE2_RGB) && defined(GL_OPERAND2_RGB)
            envcol_set((GLfloat) k[0] / 255.0f,
                       (GLfloat) k[1] / 255.0f,
                       (GLfloat) k[2] / 255.0f, 1.0f);
#endif
            g_rh_skylerp++;
        } else {
            texenv_set(0);
        }
    } else if (g_tex_gl_on) {
        glDisable(GL_TEXTURE_2D);
        g_tex_gl_on = 0;
    }

    glBegin(GL_TRIANGLE_FAN);
    for (i = 0; i < NV; i++) {
        if (name) {
            /* S/W and T/W are what the RDP samples with; handing GL the
             * homogeneous triple makes it interpolate projectively and divide
             * per fragment, which is the same operation. sky.c scales S and T
             * by the same factor it scales W by 32767 (sky.c:1751-1753), so
             * that constant is what converts the ratio back to 10.5 texel
             * units, and 32 of those are one texel. */
            float k = 32767.0f / 32.0f;
            glTexCoord4f(st[i][0] * k / (float) tw,
                         st[i][1] * k / (float) th,
                         0.0f, st[i][2]);
        }
        glColor4ub((unsigned char) col[i][0], (unsigned char) col[i][1],
                   (unsigned char) col[i][2], (unsigned char) col[i][3]);
        glVertex2f(vx[i], vy[i]);
    }
    glEnd();
    ccx_note(1);
    g_rh_drawn++;

    if (getenv("SL_RDPTRI_DBG") != NULL && g_rh_dbg_left > 0) {
        g_rh_dbg_left--;
        fprintf(stderr, "sl_rdptri: cmd=%02X tile=%u tex=%u %ux%u why=%d"
                        "  yH=%.1f yM=%.1f yL=%.1f"
                        "  XH=%.1f/%.3f XM=%.1f/%.3f XL=%.1f/%.3f\n",
                cmd, tile, (unsigned) name, tw, th, why,
                yH, yM, yL, XH, DxHDy, XM, DxMDy, XL, DxLDy);
        fprintf(stderr, "sl_rdptri:   poly");
        for (i = 0; i < NV; i++) fprintf(stderr, " (%.1f,%.1f)", vx[i], vy[i]);
        fprintf(stderr, "\n");
        for (i = 0; i < NV; i++)
            fprintf(stderr, "sl_rdptri:   v%d rgba=%.0f,%.0f,%.0f,%.0f"
                            "  S=%.1f T=%.1f W=%.1f  ->uv=(%.3f,%.3f)\n",
                    i, col[i][0], col[i][1], col[i][2], col[i][3],
                    st[i][0], st[i][1], st[i][2],
                    st[i][2] != 0.0f ? st[i][0] / st[i][2] * 32767.0f / 32.0f / tw : 0.0f,
                    st[i][2] != 0.0f ? st[i][1] / st[i][2] * 32767.0f / 32.0f / th : 0.0f);
    }
}

/* Append one word to the command being assembled. The first word names the
 * command and therefore its length; anything that is not a triangle is
 * dropped on the spot rather than collected, so a stray half cannot desync
 * the stream indefinitely. */
static void rdphalf_push(unsigned w)
{
    unsigned need;

    if (!rdptri_on()) return;

    if (g_rh_n == 0 && rh_need(w >> 24) == 0u) { g_rh_notri++; return; }
    if (g_rh_n >= (unsigned) (sizeof g_rh_w / sizeof g_rh_w[0])) {
        g_rh_overflow++; g_rh_n = 0; return;
    }

    g_rh_w[g_rh_n++] = w;
    need = rh_need(g_rh_w[0] >> 24);
    if (need != 0u && g_rh_n >= need) {
        draw_rdp_tri();
        g_rh_n = 0;
    }
}

/* Undo OS_K0_TO_PHYSICAL so a colour-image and a depth-image operand can be
 * compared: zbufInit hands gDPSetDepthImage a raw KSEG0 pointer (viewport.c:81)
 * while zbufClearCurrentPlayer hands gDPSetColorImage the K0_TO_PHYSICAL form
 * of the same buffer (viewport.c:94), so the two differ by 0x80000000. */
static unsigned img_norm(unsigned a)
{
    return (a != 0 && a < 0x80000000u) ? a + 0x80000000u : a;
}

/* Mirror of the transform GL is about to perform: v' = v * MV * P, then the
 * perspective divide. Records NDC extents and the inside/outside tally. */
static void ndc_account(float x, float y, float z, float *out)
{
    float c[4], n[3];
    int j;

    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 0.0f;                       /* 1 once the point is in front */

    /* x/y/z arrive already through the modelview - vtx_transform applied the
     * one in effect when the vertex was LOADED, exactly as the RSP does - so
     * only the projection is left to apply here. */
    for (j = 0; j < 4; j++)
        c[j] = x * g_proj[j] + y * g_proj[4 + j] + z * g_proj[8 + j] + g_proj[12 + j];

    if (c[3] <= 1e-6f) { g_off++; return; }     /* behind the eye */
    for (j = 0; j < 3; j++) {
        n[j] = c[j] / c[3];
        if (n[j] < g_ndc_min[j]) g_ndc_min[j] = n[j];
        if (n[j] > g_ndc_max[j]) g_ndc_max[j] = n[j];
        out[j] = n[j];
    }
    out[3] = 1.0f;
    if (n[0] >= -1.0f && n[0] <= 1.0f && n[1] >= -1.0f && n[1] <= 1.0f &&
        n[2] >= -1.0f && n[2] <= 1.0f)
        g_on++;
    else
        g_off++;
}

static void do_matrix(unsigned p, unsigned len, unsigned int a)
{
    const unsigned int *raw;
    float m[16];

    if (len != MTX_SIZE) {                          /* not a real G_MTX */
        g_unknown++; g_op_hist[OP_MTX]++; return;
    }
    if (++g_mtx_cmds > MTX_CMD_MAX) return;

    raw = mtx_resolve(a);
    if (raw == NULL) { g_mtx_bad++; return; }
    mtx_to_float(raw, m);

    if (p & MTX_PROJECTION) {
        g_mtx_proj++;
        /* The RSP keeps no projection stack; PUSH is meaningless here. */
        if (p & MTX_LOAD) {
            memcpy(g_proj_game, m, sizeof m); g_mtx_load++;
            g_proj_is_world = (g_world_proj_addr != 0 && a == g_world_proj_addr)
                           || (g_world_proj_addr2 != 0 && a == g_world_proj_addr2);
        } else {
            mtx_mul(m, g_proj_game, g_proj_game);  g_mtx_mul++;
        }
        proj_apply_aspect();
        gl_load_projection();
        return;
    }

    g_mtx_mv++;
    if (p & MTX_PUSH) {
        g_mtx_push++;
        if (g_mv_sp + 1 < MTX_STACK_MAX) {
            memcpy(g_mv[g_mv_sp + 1], g_mv[g_mv_sp], sizeof g_mv[0]);
            g_mv_sp++;
        }
        /* stack full: drop the push and keep transforming the top, which is
         * wrong but bounded. Nothing in the tree pushes - a tree-wide
         * `grep -rn G_MTX_PUSH src/` is empty - so this is defence only. */
    }
    if (p & MTX_LOAD) { memcpy(g_mv[g_mv_sp], m, sizeof m); g_mtx_load++; }
    else              { mtx_mul(m, g_mv[g_mv_sp], g_mv[g_mv_sp]); g_mtx_mul++; }
    /* Software state only. GL's modelview is the identity throughout the 3D
     * path, and touching it here would also be illegal mid-batch. */
}

static void do_popmatrix(unsigned int w0)
{
    /* gbi.h (non-F3DEX2): gSPPopMatrix(pkt, n) = gImmp1(pkt, G_POPMTX, n),
     * whose w0 is _SHIFTL(c, 24, 8) alone - every low bit clear. That is the
     * same kind of shape check the length field gives G_MTX, and it matters:
     * before it, the walker's excursions through non-DL memory were handing
     * this path 8 "popmatrix" commands a frame. F3D's RSP pops one regardless
     * of n. ucode05.txt lists BD but documents no operand layout, so gbi.h is
     * the authority here. Unreachable from GE's own lists in any case:
     * `grep -rn "gSPPopMatrix\|gsSPPopMatrix\|G_POPMTX" src/` is empty. */
    if (w0 & 0x00ffffffu) { g_unknown++; g_op_hist[OP_POPMTX]++; return; }
    g_mtx_pop++;
    if (g_mv_sp > 0) g_mv_sp--;
}

/* ---- G_AC_DITHER execution -----------------------------------------------
 *
 * The RDP kills a fragment when its alpha loses against a per-pixel random
 * value. The closest thing a fixed-function GL context has to that is the
 * polygon stipple: a 32x32 screen-aligned bit mask consulted per rasterised
 * pixel. Filling it so that a bit is set with probability alpha/255, and
 * refilling it from a new seed every frame, reproduces the same statistics -
 * scattered rejection whose density tracks alpha and whose pattern changes
 * frame to frame.
 *
 * WHY NOT THE FRAGMENT SHADER. A discard in AEQ_FS would be per-fragment and
 * exact, but that program is bound only for draws the alpha-equation
 * classifier selects, and binding it for this one would replace the whole
 * fragment stage for an untextured G_CC_SHADE draw - the "static appears
 * because alpha handling globally broke" failure mode. The stipple is
 * orthogonal to the fragment stage: it cannot alter any colour or alpha, only
 * whether a pixel is rasterised at all.
 *
 * KNOWN DIFFERENCES FROM THE RDP, both measured and accepted:
 *   - the mask repeats every 32 pixels, where the RDP's noise does not;
 *   - it is per NATIVE pixel, and this backend renders larger than 320x240,
 *     so the grain is finer than the cartridge's. Neither changes the
 *     acceptance statistics (density, animation, alpha scaling).
 *   - density comes from the triangle's first vertex alpha, exact for a draw
 *     of uniform alpha, which the watch face is (options.c:1795 writes one
 *     value to every vertex).
 *
 * SL_AC_DITHER=0 parses and holds the state but declines to execute it - the
 * A/B control that separates "the mode survived the parser" from "this code
 * drew the effect". */
static int g_dith_on = -1;          /* GL enable state, -1 = unknown */
static int g_dith_a = -1;           /* density the pattern was built for */
static unsigned g_dith_seed = 1u;   /* bumped once per frame */

static int dither_exec_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_AC_DITHER");
                  on = (v == NULL || (*v != '\0' && *v != '0')); }
    return on;
}

static unsigned dith_hash(unsigned x, unsigned y, unsigned s)
{
    unsigned h = x * 374761393u + y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void dither_apply(unsigned alpha)
{
    int want = ((g_om_l & RM_AC_MASK) == AC_DITHER) && dither_exec_on();
    if (!want) {
        if (g_dith_on != 0) {
            batch_end();                /* glDisable is illegal inside glBegin */
            glDisable(GL_POLYGON_STIPPLE);
            g_dith_on = 0; g_dith_a = -1;
        }
        return;
    }
    if (g_dith_on == 1 && g_dith_a == (int) alpha) return;
    {
        GLubyte pat[128];
        unsigned x, y;
        batch_end();
        memset(pat, 0, sizeof pat);
        for (y = 0; y < 32u; y++)
            for (x = 0; x < 32u; x++)
                /* keep the pixel when alpha wins against the random value,
                 * which is the RDP's own comparison */
                if ((dith_hash(x, y, g_dith_seed) & 0xffu) < alpha)
                    pat[y * 4u + (x >> 3)] |= (GLubyte) (0x80u >> (x & 7u));
        glPolygonStipple(pat);
        glEnable(GL_POLYGON_STIPPLE);
        g_dith_on = 1; g_dith_a = (int) alpha;
    }
}

/* ---- LIVE OWNER BUG MARK: centre-screen draw provenance ------------------
 *
 * F8 while playing means "the defect is under the crosshair, right now".
 * Everything this block gathers is READ, never written: no GL call, no state
 * change, no toggle. The frame the owner marks has to be the frame the owner
 * was looking at, or the mark describes a picture nobody saw.
 *
 * That is not a stylistic preference. SL_TRI_ID and SL_GLASS_MARK both answer
 * "which draw covers this pixel" by REPLACING the draw's colour or dropping
 * its texture - and the comment at tex_apply records the resulting error:
 * a TRI_ID capture was read as evidence that a quad covered a region, when
 * disabling texturing is what made the quad visible in the first place. A
 * marking facility that alters the scene under test would repeat that by
 * construction, so this one cannot: it appends to an array and nothing else.
 *
 * WHAT IT COSTS WHEN IDLE. One `if (!g_mark_on)` per emitted triangle, and
 * two assignments per G_DL branch. g_mark_on is zero except during the single
 * display list that follows an F8 press.
 *
 * WHAT IT CANNOT DO. It is not a rasteriser. A triangle is recorded when its
 * PROJECTED BOUNDING BOX intersects the centre box, which over-reports: a
 * large triangle whose box straddles the centre is listed even if the pixel
 * at the centre belongs to something else. Over-reporting is the safe
 * direction - the alternative is a mark that silently omits the one draw
 * being hunted. Records carry NDC extents and depth so a reader can rank
 * them; the nearest small-box triangle is almost always the answer.
 *
 * If NOTHING intersects, the mark says so in as many words. That is evidence:
 * it means no world geometry reached the interpreter at the centre of the
 * screen, which is a different defect from geometry drawn wrongly. */
#define SL_MARK_TRIS 96
#define SL_MARK_DLS  64

struct sl_mark_tri {
    unsigned    tri;                 /* g_tri_id within the marked frame */
    unsigned    dl1, dlin;           /* depth-1 and innermost G_DL operand */
    unsigned    dl1_off, dlin_off;   /* byte offset of the command in each  */
    int         depth;
    int         sub;                 /* index into g_mark_sub, or negative  */
    float       x0, y0, x1, y1;      /* NDC bbox over in-front vertices */
    float       z0, z1;              /* NDC z */
    float       e0, e1;              /* eye-space distance */
    unsigned    geom, om_l, cc0, cc1, cyc;
    int         cull_gl, cull_opt, front;
    unsigned    texw, texh, texgl, texfmt, textile;
    int         texcache_slot;
    const void *texsrc;
    int         fogm;
    /* Render state AS THE TRIANGLE WAS DRAWN. These are the cached values
     * rm_apply and cull_apply just installed - emit_tri calls both before
     * batch_begin, so at this point they describe this draw and no other. */
    int         ztest, zwrite, zoff, blend, sfac, dfac;
    int         redraw;              /* B-125: drawn as an authored redraw   */
    int         sciss[4];            /* last decoded ED setscissor, pixels  */
    int         vprect[4];           /* the game viewport, GL window coords */
    /* PER-VERTEX SOURCE PROVENANCE. slot is the RSP vertex-buffer slot the
     * triangle command named; the rest is where that slot was last loaded
     * from and what it held. */
    int         slot[3];
    unsigned    vseg[3];             /* G_VTX segment operand for the load  */
    unsigned    vidx[3];             /* index of the vertex within the load */
    unsigned    voff[3];             /* offset of that G_VTX in its list    */
    unsigned    vdl[3];              /* depth-1 list the G_VTX ran under    */
    const void *vp[3];               /* host address of the source vertex   */
    short       mx[3], my[3], mz[3]; /* source coordinates, as interpreted  */
    short       vs[3], vt[3];
    unsigned char vr[3], vg[3], vb[3], va[3];
    float       eye[3][3];
    float       clip[3][4];
    float       ndc[3][3];
    float       scr[3][2];           /* GL window pixels, bottom-left origin */
    /* #25 CULL WITNESS. The command form the triangle came from (B1 slot
     * 0-3, or 4 for BF), the raw command words, the emit_tri classification
     * (bit0 crosses the near plane, bit1 a vertex at/behind the eye, bit2
     * outside the 2w guard box - nonzero means the fan path drew it), and
     * the signed NDC area of the vertices AS EMITTED in command order
     * (positive = counter-clockwise in GL's y-up NDC). The RSP evaluates
     * the same cross product on its y-down screen coordinates, so its sign
     * is the negation; `drop1` is what sense 1 does with it under the
     * geometry mode in force. */
    int         form;
    unsigned    cw0, cw1;
    int         cls;
    float       area;
    int         drop1;
};

/* ---- Rooms: SUBMITTED versus DRAWN --------------------------------------
 *
 * Four different facts get confused with each other, and "0 rooms" has meant
 * all four at once in this record before:
 *
 *   1  what the portal/visibility pass decided     (game side, and routinely
 *                                                   empty at a frame boundary)
 *   2  what bg.c asked the RSP to draw             (game side, the render list)
 *   3  what the interpreter was actually HANDED    (a G_DL submission)
 *   4  what the interpreter actually DREW          (>= 1 triangle came out)
 *
 * This table is 3 and 4, for the COMPLETE executed frame.
 *
 * ---- WHY IT USED TO STOP HALF WAY, AND WHAT THAT COST ---------------------
 *
 * It recorded G_DL only at depth 0 - "depth-1 branches" - so provenance ended
 * at the first submission that carried the rest of the frame inside it. In the
 * owner's Dam marks the frame list issues five or six submissions and the LAST
 * one is a list high in the game heap (20059xxx) holding two to three thousand
 * triangles: the props, the characters and the first-person viewmodel all
 * execute inside it, at depth >= 1, and none of them could ever appear. The
 * nineteen segment-5 lists gunRenderFirstPersonGunModels emits were therefore
 * absent from a GOOD mark for exactly the reason they were absent from a BAD
 * one, and the gun A/B that rested on that absence proved nothing.
 *
 * Triangle COUNTS were never wrong - everything inside the trailing list was
 * tallied against it, so the column summed to the frame - which is precisely
 * what made the table look whole while its structure was truncated. A count
 * that reconciles is not evidence of coverage.
 *
 * ---- WHAT A SUBMISSION IS, PER ucode05.txt --------------------------------
 *
 * "Display Lists and Object Generation/ucode05.txt" l.240-246 gives opcode 06
 * two forms, distinguished by bits 16-23 of the upper word:
 *
 *     xx00xxxx   push display list      - a CALL. The running list is
 *                                         suspended and resumed after the 06.
 *     xx01xxxx   branch to display list - a TAIL TRANSFER. The running list is
 *                                         REPLACED; it does not resume.
 *
 * and l.437-440 (B8 rsp_enddl): "If the current list was called by an 06
 * command, returns from branch" - so a CALL consumes a stack level and a
 * BRANCH does not. A branch is a sibling of the list it replaced, not a child
 * of it, and the two must not be drawn as the same relationship.
 *
 * Each node therefore records BOTH numbers: `call` is the logical stack level
 * the RSP would be at (a branch inherits its predecessor's), and `wdepth` is
 * the recursion level walk() itself reached. They agreed only on frames with
 * no tail transfer until the branch/call conflation was fixed; walk() now
 * iterates the branch form rather than recursing, so the two are expected to
 * match everywhere, and a disagreement is again a finding rather than a known
 * artefact.
 *
 * ---- WHAT IT DOES NOT DO --------------------------------------------------
 *
 * No de-duplication by address. Two submissions of the same list are two
 * facts, and collapsing them destroys execution order - the one thing this
 * table exists to preserve. The cost is a bigger table and an explicit
 * overflow report; the alternative is a shorter table that lies. */
#define SL_MARK_SUBS 1024
struct sl_mark_sub {
    unsigned addr;                   /* the G_DL operand, as written        */
    unsigned host;                   /* resolved target, low 32 bits, or 0  */
    unsigned off;                    /* byte offset of the 06 in its list   */
    unsigned tris;                   /* triangles emitted with this INNERMOST */
    unsigned centre;                 /* of those, ones meeting the box      */
    int      parent;                 /* enclosing CALL frame, -1 at the root */
    int      prev;                   /* node this one tail-replaced, or -1  */
    int      ord;                    /* execution order; 0 is the frame list */
    short    call;                   /* logical RSP stack level             */
    short    wdepth;                 /* walk() recursion level              */
    unsigned char kind;              /* 0 frame list, 1 call, 2 branch      */
};
static struct sl_mark_sub g_mark_sub[SL_MARK_SUBS];
static int      g_mark_sub_n;
static int      g_mark_sub_cur = -1; /* which node is executing right now   */
static int      g_mark_sub_ord;      /* submissions seen this frame         */
static unsigned g_mark_sub_lost;     /* submissions past the table capacity */
static unsigned g_mark_calls;        /* 06 push  forms executed             */
static unsigned g_mark_branches;     /* 06 branch forms executed            */
static short    g_mark_call_max;     /* deepest logical stack level reached */
static short    g_mark_wdepth_max;   /* deepest walk() recursion reached    */
static unsigned g_mark_tris_attr;    /* triangles landing on a recorded node */
static unsigned g_mark_tris_lost;    /* triangles under an UNRECORDED node  */

/* -2 in g_mark_sub_cur means "executing inside a submission the table could
 * not hold". It is distinct from -1 (no submission is executing, which after
 * the root node exists cannot happen) so a triangle can be counted as LOST
 * rather than silently attributed to whatever node happened to be current. */
#define SL_MARK_SUB_LOST (-2)

static int      g_mark_req;          /* F8 seen; arm the next display list  */
static int      g_mark_on;           /* collecting during THIS list         */
static int      g_mark_have;         /* a capture is waiting to be written  */
static struct sl_mark_tri g_mark_t[SL_MARK_TRIS];
static int      g_mark_n;            /* records kept (<= SL_MARK_TRIS)      */
static unsigned g_mark_hit;          /* records that intersected, uncapped  */
static unsigned g_mark_tris;         /* triangles emitted in the frame      */
static unsigned g_mark_frame;        /* g_dl_frame the capture came from    */
static unsigned g_mark_dl[SL_MARK_DLS];   /* depth-1 lists that DREW        */
static unsigned g_mark_dl_tris[SL_MARK_DLS];
static int      g_mark_dl_n;
static float    g_mark_box = -1.0f;  /* NDC half-extent of the centre box   */

/* The display list a triangle came from. Set only at a G_DL branch, so the
 * ordinary command path is untouched. g_dl_addr[1] is the list the FRAME list
 * branched to - for room geometry that is
 * OS_K0_TO_PHYSICAL(g_BgRoomInfo[room].ptr_expanded_mapping_info), which is
 * what lets a room be named without the renderer knowing what a room is. */
static unsigned g_dl_addr[12];
static int      g_dl_depth;

/* WHERE IN THE LIST, not merely WHICH LIST. g_dl_first[d] is the first word
 * of the list being walked at depth d; g_dl_cur[d] is the command word being
 * executed there. Their difference in bytes is the source DL offset - the
 * value that turns "dl=20026730" into "the 0x1a8th byte of that list", which
 * is the step the seam investigation had to do by hand.
 *
 * One pointer store per command, at the point the command is already being
 * read. Nothing reads these except the mark writer. */
static const unsigned int *g_dl_first[12];
static const unsigned int *g_dl_cur[12];

/* Byte offset of the command currently executing at `depth` within its own
 * list, or 0xffffffff when that cannot be established. */
static unsigned dl_off_at(int depth)
{
    if (depth < 0 || depth >= 12) return 0xffffffffu;
    if (g_dl_first[depth] == NULL || g_dl_cur[depth] == NULL) return 0xffffffffu;
    if (g_dl_cur[depth] < g_dl_first[depth]) return 0xffffffffu;
    return (unsigned) ((g_dl_cur[depth] - g_dl_first[depth]) * 4);
}

/* B-139. A 04 whose operand names nothing readable: not through its segment
 * (unset on this submission, or set to something unmapped), not as a raw
 * host pointer, not as OS_K0_TO_PHYSICAL. The load is dropped and COUNTED -
 * into g_vtx_reject, so the census's `rej` shows it, and into g_vtx_unres so
 * the two causes stay distinguishable - and REPORTED with everything that
 * names the producer: the segment table entry, the depth, the frame list's
 * branch operand (a room's identity when it is a room), the innermost list
 * and the offset inside it, and what Windows says lives at the two raw
 * candidates. The first eight per run print unconditionally: a fault that
 * became a silent drop would be a worse instrument than the crash was.
 * SL_SEG_DBG=1 prints every one. */
static unsigned g_vtx_unres_run;
static void vtx_unresolved(unsigned w0, unsigned w1, int depth)
{
    static int dbg = -1;
    unsigned s = (w1 >> 24) & 0x0f;
    char rb[160], kb[160];

    g_vtx_reject++;
    g_vtx_unres++;
    if (dbg < 0) dbg = (getenv("SL_SEG_DBG") != NULL);
    g_vtx_unres_run++;
    if (!dbg && g_vtx_unres_run > 8) return;

    vq_describe((unsigned long) w1, rb, sizeof rb);
    if (w1 < 0x80000000u)
        vq_describe((unsigned long) w1 + 0x80000000ul, kb, sizeof kb);
    else
        snprintf(kb, sizeof kb, "(no K0 form)");
    fprintf(stderr, "sl_vtx: f%u UNRESOLVED load w0=%08x w1=%08x segment %u"
                    " base=%08x depth=%d dl1=%08x dlin=%08x off=%u\n"
                    "sl_vtx:   raw %08x: %s\n"
                    "sl_vtx:   k0  %08lx: %s\n",
            g_dl_frame, w0, w1, s, g_seg[s], depth,
            (g_dl_depth >= 1) ? g_dl_addr[1] : 0u,
            (depth >= 0 && depth < 12) ? g_dl_addr[depth] : 0u,
            dl_off_at(depth), w1, rb,
            (w1 < 0x80000000u) ? (unsigned long) w1 + 0x80000000ul : 0ul, kb);
    if (!dbg && g_vtx_unres_run == 8)
        fprintf(stderr, "sl_vtx: further unresolved loads are counted only"
                        " (rej= on the sl_dl line; SL_SEG_DBG=1 prints each)\n");
}

/* #25 witness aim. SL_MARK_CX / SL_MARK_CY move the centre box to an NDC
 * point, and SL_MARK_AT=<frame> arms the capture at that g_dl_frame with no
 * key press, so a frame of a deterministic intro (SL_VI_CATCHUP=0) can be
 * marked at a chosen point. Probes only; unset means the F8 behaviour. */
static float g_mark_cx, g_mark_cy;
static unsigned g_mark_at;

static float mark_box(void)
{
    if (g_mark_box < 0.0f) {
        const char *cx = getenv("SL_MARK_CX"), *cy = getenv("SL_MARK_CY");
        g_mark_cx = cx ? (float) atof(cx) : 0.0f;
        g_mark_cy = cy ? (float) atof(cy) : 0.0f;
        /* Half-extent in NDC, so 0.03 is 3% of half-width either side of
         * centre - about 29 px across at 960 wide. Overridable for a tighter
         * or looser aim, but the owner needs no variable set: this default is
         * what ships and what the acceptance run used. */
        const char *v = getenv("SL_MARK_BOX");
        double d = (v != NULL) ? atof(v) : 0.03;
        if (!(d > 0.0) || d > 1.0) d = 0.03;
        g_mark_box = (float) d;
    }
    return g_mark_box;
}

/* Called from the input layer on the F8 edge. Only raises a flag - the
 * capture happens where the triangles are, on the next list. */
void sl_mark_request(void);
void sl_mark_request(void)
{
    g_mark_req = 1;
}

int  sl_mark_ready(void);
int  sl_mark_ready(void) { return g_mark_have; }
void sl_mark_consume(void);
void sl_mark_consume(void) { g_mark_have = 0; }

/* The DRAWN set, structured. sl_mark_render prints it as text for a human;
 * the platform layer needs the addresses themselves so it can put each one
 * through the game-side room resolver (src/native/sl_game_query.c) without
 * parsing its own report back out of a string. */
int sl_mark_dl_count(void);
int sl_mark_dl_count(void) { return g_mark_dl_n; }
unsigned sl_mark_dl_addr(int i);
unsigned sl_mark_dl_addr(int i)
{
    return (i >= 0 && i < g_mark_dl_n) ? g_mark_dl[i] : 0u;
}
unsigned sl_mark_dl_tris(int i);
unsigned sl_mark_dl_tris(int i)
{
    return (i >= 0 && i < g_mark_dl_n) ? g_mark_dl_tris[i] : 0u;
}

/* The SUBMITTED set is no longer exported one field at a time. It is a TREE
 * now - a node needs its parent, its kind, both depths and its ordinal to mean
 * anything - and six scalar accessors would hand the platform layer the parts
 * and leave it to re-invent the relationship. sl_mark_subs_render below prints
 * it here, where the semantics live, and takes the game-side room resolver as
 * a callback exactly as sl_mark_render already does. */

/* Open a submission node. Returns the new node index, SL_MARK_SUB_LOST when
 * the table is full. Marked frames only; the caller has already tested
 * g_mark_on. `kind` is 1 for a push/call and 2 for a branch/tail transfer.
 *
 * The two forms differ in exactly one place, and it is the whole point of the
 * function: a CALL nests under the node that is running, while a BRANCH
 * REPLACES it and therefore takes that node's parent as its own. Getting this
 * backwards would redraw a tail transfer as a child and re-introduce, one
 * level down, the same false nesting the old table had. */
static int mark_sub_open(unsigned w1, const void *t, int kind, int wdepth,
                         unsigned off)
{
    int cur = g_mark_sub_cur, idx;
    struct sl_mark_sub *e;

    g_mark_sub_ord++;
    if (kind == 2) g_mark_branches++; else g_mark_calls++;
    /* The two depth maxima are FRAME facts, so they are taken before the table
     * is consulted. Measured with the table deliberately shrunk to 8 entries:
     * counting them only for stored nodes reported max-walk-depth 1 on the
     * very frame whose overflow banner said 122 submissions were missing - an
     * integrity block that under-reports exactly when it is telling you it is
     * incomplete. */
    if (wdepth > (int) g_mark_wdepth_max) g_mark_wdepth_max = (short) wdepth;

    if (g_mark_sub_n >= SL_MARK_SUBS) { g_mark_sub_lost++; return SL_MARK_SUB_LOST; }
    idx = g_mark_sub_n++;
    e = &g_mark_sub[idx];
    e->addr   = w1;
    e->host   = (t != NULL) ? (unsigned) (unsigned long) t : 0u;
    e->off    = off;
    e->tris   = 0;
    e->centre = 0;
    e->prev   = (kind == 2) ? cur : -1;
    e->ord    = g_mark_sub_ord;
    e->kind   = (unsigned char) kind;
    e->wdepth = (short) wdepth;
    if (cur >= 0) {
        e->parent = (kind == 2) ? g_mark_sub[cur].parent : cur;
        e->call   = (short) (g_mark_sub[cur].call + (kind == 2 ? 0 : 1));
    } else {
        /* Either the root node is missing (it is not - it is planted when the
         * mark arms) or the enclosing submission overflowed the table. Both
         * mean this node has no recorded parent, and saying -1 is the truthful
         * answer rather than attaching it to an unrelated node. */
        e->parent = -1;
        e->call   = (short) (kind == 2 ? 0 : 1);
    }
    if (e->call > g_mark_call_max) g_mark_call_max = e->call;
    return idx;
}

/* Window-space depth (what glReadPixels(GL_DEPTH_COMPONENT) returns) back to
 * an EYE-SPACE distance, through the projection that was in force. Returns 0
 * when the matrix is not a perspective one or the value is the far plane,
 * which the caller reports as such rather than printing a huge number.
 *
 * Why it belongs here: g_proj is this file's, and the alternative is the
 * backend re-deriving near/far from a matrix it cannot see. */
int sl_mark_depth_to_eye(double d, double *eye_out);
int sl_mark_depth_to_eye(double d, double *eye_out)
{
    double A = (double) g_proj[10], B = (double) g_proj[14], ndc, den;
    if (eye_out == NULL) return 0;
    *eye_out = 0.0;
    if (g_proj[11] == 0.0f || B == 0.0) return 0;   /* not perspective */
    ndc = d * 2.0 - 1.0;                            /* [0,1] -> [-1,1] */
    den = ndc + A;
    if (den == 0.0) return 0;                       /* on the far plane */
    *eye_out = -B / den;                            /* eye z, negative ahead */
    if (*eye_out < 0.0) *eye_out = -*eye_out;
    return 1;
}

static void mark_note_tri(const float ndc[3][5], int fogm, const int idx[3])
{
    struct sl_mark_tri *m;
    float x0, y0, x1, y1, z0, z1, e0, e1, r;
    int i, front = 0;

    g_mark_tris++;
    /* EVERY triangle, before any filter. This is what makes "submitted but
     * drew nothing" a readable outcome rather than an absence.
     *
     * Attribution is EXCLUSIVE - the innermost submission running when the
     * triangle came out, not every submission enclosing it - so summing this
     * column over the whole table reproduces g_mark_tris exactly. That
     * identity is the integrity check the mark prints, and it only holds
     * because a triangle emitted with no node open is counted as LOST here
     * rather than dropped. */
    if (g_mark_sub_cur >= 0) {
        g_mark_sub[g_mark_sub_cur].tris++;
        g_mark_tris_attr++;
    } else {
        g_mark_tris_lost++;
    }

    x0 = y0 = z0 = e0 =  1e30f;
    x1 = y1 = z1 = e1 = -1e30f;
    for (i = 0; i < 3; i++) {
        if (ndc[i][3] == 0.0f) continue;         /* behind the eye */
        front++;
        if (ndc[i][0] < x0) x0 = ndc[i][0];
        if (ndc[i][0] > x1) x1 = ndc[i][0];
        if (ndc[i][1] < y0) y0 = ndc[i][1];
        if (ndc[i][1] > y1) y1 = ndc[i][1];
        if (ndc[i][2] < z0) z0 = ndc[i][2];
        if (ndc[i][2] > z1) z1 = ndc[i][2];
        if (ndc[i][4] < e0) e0 = ndc[i][4];
        if (ndc[i][4] > e1) e1 = ndc[i][4];
    }
    if (front == 0) return;         /* wholly behind the eye: draws nothing */

    r = mark_box();
    if (x1 < g_mark_cx - r || x0 > g_mark_cx + r
        || y1 < g_mark_cy - r || y0 > g_mark_cy + r) return;

    /* Which depth-1 list drew here, tallied whether or not the record itself
     * fits. This is the DRAWN set: lists that actually put a triangle on the
     * screen, as distinct from the rooms the visibility pass asked for. */
    {
        unsigned d1 = (g_dl_depth >= 1) ? g_dl_addr[1] : 0u;
        for (i = 0; i < g_mark_dl_n; i++)
            if (g_mark_dl[i] == d1) break;
        if (i == g_mark_dl_n && g_mark_dl_n < SL_MARK_DLS) {
            g_mark_dl[i] = d1; g_mark_dl_tris[i] = 0; g_mark_dl_n++;
        }
        if (i < g_mark_dl_n) g_mark_dl_tris[i]++;
    }

    g_mark_hit++;
    if (g_mark_sub_cur >= 0) g_mark_sub[g_mark_sub_cur].centre++;
    if (g_mark_n >= SL_MARK_TRIS) return;
    m = &g_mark_t[g_mark_n++];
    m->tri   = g_tri_id;
    m->dl1   = (g_dl_depth >= 1) ? g_dl_addr[1] : 0u;
    m->dlin  = (g_dl_depth >= 1 && g_dl_depth < 12) ? g_dl_addr[g_dl_depth] : 0u;
    m->dl1_off  = dl_off_at(1);
    m->dlin_off = dl_off_at(g_dl_depth);
    m->depth = g_dl_depth;
    m->sub   = g_mark_sub_cur;
    m->x0 = x0; m->y0 = y0; m->x1 = x1; m->y1 = y1;
    m->z0 = z0; m->z1 = z1; m->e0 = e0; m->e1 = e1;
    m->front    = front;
    m->geom     = g_geom_mode;
    m->om_l     = g_om_l;
    m->cc0      = g_cc_w0;
    m->cc1      = g_cc_w1;
    m->cyc      = g_cycle_type;
    m->cull_gl  = g_cull_gl;
    m->cull_opt = cull_option();
    m->fogm     = fogm;
    m->texw     = g_tex_gl_on ? g_st_tex_w : 0u;
    m->texh     = g_tex_gl_on ? g_st_tex_h : 0u;
    m->texgl    = (unsigned) g_tex_gl_bound;
    m->textile  = g_tex_tile;
    m->texcache_slot = g_tex_gl_on ? g_tex_slot : -1;
    m->texfmt   = (g_tex_gl_on && g_tex_slot >= 0)
                ? g_texcache[g_tex_slot].fmt : 0xffffffffu;
    m->texsrc   = (g_tex_gl_on && g_tex_slot >= 0)
                ? g_texcache[g_tex_slot].src : NULL;

    /* Material and depth state, read out of the caches rm_apply/cull_apply
     * have just written. Reading them is what makes "the draw is there but
     * invisible" separable from "the draw never happened". */
    m->ztest  = g_rm_ztest;
    m->zwrite = g_rm_zwrite;
    m->zoff   = g_rm_zoff;
    m->blend  = g_rm_blend;
    m->sfac   = g_rm_sfac;
    m->dfac   = g_rm_dfac;
    m->redraw = (g_rd_cur && g_rm_ztest && g_rm_zoff == 0);
    for (i = 0; i < 4; i++) { m->sciss[i] = g_sciss[i]; m->vprect[i] = g_vp_rect[i]; }

    /* #25 cull witness. The area is over the NDC ndc_account produced for
     * the three vertices in emission order, which for a fan triangle are
     * the fan's own (g_veye is borrowed) - so this is the winding GL culls
     * on. cull_would_drop reads the same g_veye through tri_sign_of. */
    m->form  = g_fd_slot;
    m->cw0   = g_tri_cw0;
    m->cw1   = g_tri_cw1;
    m->cls   = g_tri_cls;
    m->area  = (front == 3)
             ? (ndc[1][0] - ndc[0][0]) * (ndc[2][1] - ndc[0][1])
             - (ndc[1][1] - ndc[0][1]) * (ndc[2][0] - ndc[0][0])
             : 0.0f;
    m->drop1 = cull_would_drop(idx[0], idx[1], idx[2]);

    /* PER-VERTEX SOURCE PROVENANCE. */
    for (i = 0; i < 3; i++) {
        int s = idx[i];
        const struct vtx *v;
        int j;
        m->slot[i] = s;
        if (s < 0 || s >= 32) {
            m->vseg[i] = 0u; m->vidx[i] = 0u; m->voff[i] = 0xffffffffu;
            m->vdl[i]  = 0u; m->vp[i]   = NULL;
            m->mx[i] = m->my[i] = m->mz[i] = 0;
            m->vs[i] = m->vt[i] = 0;
            m->vr[i] = m->vg[i] = m->vb[i] = m->va[i] = 0;
            for (j = 0; j < 3; j++) { m->eye[i][j] = 0.0f; m->ndc[i][j] = 0.0f; }
            for (j = 0; j < 4; j++) m->clip[i][j] = 0.0f;
            m->scr[i][0] = m->scr[i][1] = 0.0f;
            continue;
        }
        v = &g_vbuf[s];
        m->vseg[i] = g_vsrc_seg[s];
        m->vidx[i] = g_vsrc_i[s];
        m->voff[i] = g_vsrc_off[s];
        m->vdl[i]  = g_vsrc_dl[s];
        m->vp[i]   = g_vsrc_p[s];
        m->mx[i] = v->x; m->my[i] = v->y; m->mz[i] = v->z;
        m->vs[i] = v->s; m->vt[i] = v->t;
        m->vr[i] = v->r; m->vg[i] = v->g; m->vb[i] = v->b; m->va[i] = v->a;
        for (j = 0; j < 3; j++) m->eye[i][j] = g_veye[s][j];
        /* Clip space, recomputed from the SAME projection ndc_account used.
         * Recomputing rather than plumbing it out keeps ndc_account - which
         * runs for every vertex of every frame - exactly as it was. */
        for (j = 0; j < 4; j++)
            m->clip[i][j] = g_veye[s][0] * g_proj[j]
                          + g_veye[s][1] * g_proj[4 + j]
                          + g_veye[s][2] * g_proj[8 + j] + g_proj[12 + j];
        for (j = 0; j < 3; j++) m->ndc[i][j] = ndc[i][j];
        /* Window pixels, through the viewport the game actually installed.
         * GL's convention: bottom-left origin, which is also the BMP's. */
        if (g_vp_rect[2] > 0 && g_vp_rect[3] > 0 && ndc[i][3] != 0.0f) {
            m->scr[i][0] = (float) g_vp_rect[0]
                         + (ndc[i][0] * 0.5f + 0.5f) * (float) g_vp_rect[2];
            m->scr[i][1] = (float) g_vp_rect[1]
                         + (ndc[i][1] * 0.5f + 0.5f) * (float) g_vp_rect[3];
        } else {
            m->scr[i][0] = m->scr[i][1] = -1.0f;
        }
    }
}

/* ---- B-100. Homogeneous emission, for the near-crossing path only. -------
 *
 * The crossing path below computes CLIP coordinates itself and must hand them
 * to GL unchanged. Doing that through the ordinary glVertex3f route is not
 * possible: that route submits EYE space and lets the loaded projection act,
 * so the projection would be applied a second time. So for those triangles
 * only, the GL projection is swapped to identity and the vertex is submitted
 * as glVertex4f(x, y, z, w) - already-transformed coordinates through an
 * identity transform, which is the definition of "do not transform again".
 *
 * The modelview is already identity on this path (see the g_veye block), so
 * only the projection is touched, and it is pushed and popped rather than
 * rebuilt. The swap closes the batch first, because glMatrixMode is illegal
 * between glBegin and glEnd, and it is a no-op when the requested state is
 * already current - so an ordinary triangle following a crossing one restores
 * the projection exactly once, and a run of ordinary triangles costs nothing.
 *
 * Perspective-correct interpolation is UNAFFECTED, and that is the point of
 * carrying w rather than dividing: GL interpolates by 1/w, and w here is the
 * same w the ordinary path would have produced. */
static int   g_clipemit;               /* this draw supplies clip coords     */
static float g_clipemit_pos[3][4];     /* ... and they are these             */
static int   g_clipemit_proj;          /* GL projection currently identity   */

static void clipemit_proj_set(int on)
{
    if (on == g_clipemit_proj) return;
    batch_end();                       /* glMatrixMode is illegal in glBegin */
    glMatrixMode(GL_PROJECTION);
    if (on) { glPushMatrix(); glLoadIdentity(); }
    else      glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    g_clipemit_proj = on;
}

static void emit_tri_slots(int a, int b, int c)
{
    int i, idx[3], fogm, rd;
    float ndc[3][5];
    idx[0] = a; idx[1] = b; idx[2] = c;
    for (i = 0; i < 3; i++)
        if (idx[i] < 0 || idx[i] >= 32) return;
    if (cull_option() == 3 && !cull_would_drop(a, b, c)) return;
    mode2d_end();                      /* geometry draws through the DL's own
                                        * matrices, never the 2D ortho */
    vp_use(1);                         /* ... and inside the game's viewport */
    tex_apply();                       /* may end the batch; must precede it */
#ifdef SL_MULTITEX
    /* THE LOD FRACTION for the detail-blend. The RDP's LOD is log2 of the
     * texel-per-pixel density; density is measured over the triangle's edge
     * pairs (raw S/T over SL_ST_SCALE, through the B-117 shift divisor)
     * against N64 framebuffer pixels - the window is 960 wide against the
     * declared 320, so one N64 pixel is 3 window px. */
    if (g_tex1_lodlive) {
        /* B-119, correcting B-118's per-triangle constant. The RDP computes
         * LOD_FRACTION per pixel; a per-triangle constant popped between
         * triangles and miscalibrated across long walls (the owner: "clear
         * at one point, a mess at others"). The corrected realization:
         *
         *   K per triangle:  rho * z is constant across a planar surface
         *       with a fixed texel-per-world-unit mapping (screen density
         *       falls as 1/z), so measure K = rho_pair * z_pair over the
         *       triangle's edges - in TILE 1 (map) texels, since the RDP's
         *       LOD is the MAP's density, not the shift-magnified detail's;
         *   f per VERTEX:    rho_v = K / z_v, and neighbouring triangles of
         *       the same wall share K, so shared vertices get equal f and
         *       the blend is CONTINUOUS across the wall (mode 9 interpolates
         *       it per pixel from the vertex alpha).
         *
         * The curve: the map's detail region is its magnification band -
         * all-detail by 2x magnified (rho 0.5), all-map at native density
         * (rho 1.0) - so f = clamp(2*rho - 1, min, 1), the linear form of
         * log2(rho)+1 over [0.5,1] (within 6%). The floor is the FA
         * command's own minimum LOD byte (gbi.h gsDPSetPrimColor packs
         * minlevel beside the lod fraction), so the authored "never fully
         * detail" clamp is honoured rather than invented. A frame's first
         * triangles before any valid K default to the map (f=1), never to
         * the detail. */
        static float K = 0.0f;
        float fmin = (float) g_prim_lod_min / 255.0f;
        int p;
        static const int pr[3][2] = { {0,1}, {1,2}, {0,2} };
        float Ktri = 0.0f;
        for (p = 0; p < 3; p++) {
            const struct vtx *va = &g_vbuf[idx[pr[p][0]]];
            const struct vtx *vb = &g_vbuf[idx[pr[p][1]]];
            const float *ea = g_veye[idx[pr[p][0]]];
            const float *eb = g_veye[idx[pr[p][1]]];
            float ds, dt, dtex, ax, ay, bx, by, dpx, r, zp;
            if (ea[2] > -1.0f || eb[2] > -1.0f) continue;  /* behind eye */
            ds = ((float) va->s - (float) vb->s) / g_tex1_sdiv / (float) SL_ST_SCALE;
            dt = ((float) va->t - (float) vb->t) / g_tex1_tdiv / (float) SL_ST_SCALE;
            if (ds < 0.0f) ds = -ds;
            if (dt < 0.0f) dt = -dt;
            dtex = ds > dt ? ds : dt;
            /* The GAME's projection, not the #45-widened one: this is the
             * N64's texel-per-pixel estimate at the N64's own pixel density,
             * and the safe rect keeps that density at every aspect (the
             * bands add pixels, they do not change pixels per texel). */
            ax = g_proj_game[0] * ea[0] / -ea[2] * 480.0f;
            ay = g_proj_game[5] * ea[1] / -ea[2] * 330.0f;
            bx = g_proj_game[0] * eb[0] / -eb[2] * 480.0f;
            by = g_proj_game[5] * eb[1] / -eb[2] * 330.0f;
            ax -= bx; ay -= by;
            if (ax < 0.0f) ax = -ax;
            if (ay < 0.0f) ay = -ay;
            dpx = ax > ay ? ax : ay;
            if (dpx < 0.25f) continue;                  /* degenerate pair */
            r = dtex / dpx * 3.0f;      /* map texels per N64 pixel        */
            zp = 0.5f * (-ea[2] + -eb[2]);
            if (r * zp > Ktri) Ktri = r * zp;
        }
        if (Ktri > 0.0f) K = Ktri;
        for (p = 0; p < 3; p++) {
            const float *e = g_veye[idx[p]];
            float z = -e[2];
            float f = 1.0f;
            if (K > 0.0f && z > 1.0f) {
                float rho = K / z;
                f = 2.0f * rho - 1.0f;
                if (f < fmin) f = fmin;
                if (f > 1.0f) f = 1.0f;
            }
            g_lodfrac_v[p] = (unsigned char) (f * 255.0f + 0.5f);
        }
    }
#endif
    cull_apply((g_om_l & RM_Z_UPD) != 0);   /* likewise - glEnable in glBegin */
    rm_apply();                        /* likewise - B-026 */
    dither_apply(g_vbuf[idx[0]].a);    /* likewise - alpha compare */
    fogm = fog_mode();                 /* likewise - B-045 */
    fog_apply(fogm);
    if (fogm == FOG_SHADE) g_fog_tris_shade++;
    else if (fogm == FOG_CONST) g_fog_tris_const++;
    clipemit_proj_set(g_clipemit);
    /* B-125. THE REDRAW'S COLOUR PASS: the RDP's tolerant compare, no depth
     * write, the won pixels marked in the stencil plane. Only for a
     * depth-tested draw that is not already a decal/interpenetrating one
     * (those carry rm_apply's own offset, the same stand-in). See the block
     * at g_rd_set for the semantic and the numbers behind it. */
    rd = g_rd_cur && g_rm_ztest && g_rm_zoff == 0;
    if (rd) {
        /* The quad's own tent difference (g_rd_tol, window depth) as a pure
         * units offset - no slope term, no range floor: the tolerance is
         * what this re-triangulation measurably needs and nothing more. */
        float units = g_rd_tol
                    * (float) ((1u << (g_rd_depth_bits > 0 ? g_rd_depth_bits : 24)) - 1u);
        batch_end();
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(0.0f, -units);
        glDepthMask(GL_FALSE);
        if (g_rm_zwrite) {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_ALWAYS, 1, 0xffu);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        }
        g_rd_tris++;
    }
    batch_begin();
    /* B-046. The colour register the combiner scales the pixel by, resolved
     * once per triangle. Without this the 3D path honoured only TEXEL0 and
     * SHADE and drew everything else opaque white - measured on the GOLDENEYE
     * title, 260 triangles a frame under fc119623 (TEXEL0 * PRIMITIVE) with
     * the primitive ramping from black, all of them drawn at full texel. */
    {
        int shade;
        g_tint_on = cc_tint(g_tint, &shade);
        g_tint_shade = shade;
        if (g_tint_on) g_cc_tint_tris++;
    }
    g_tri_id++;
    if (g_geom_mode & G_GEOM_TEXTURE_GEN) tg_note(g_vbase[idx[0]]);
    for (i = 0; i < 3; i++) {
        const struct vtx *v = &g_vbuf[idx[i]];
        const float *e = g_veye[idx[i]];
        /* B-045. The fog coordinate is the perspective-divided z put through
         * the RSP's fm/fo, so the projection has to be applied BEFORE the
         * vertex is emitted rather than after it - ndc_account is the same
         * call it always was, only moved up the loop body. */
        ndc_account(e[0], e[1], e[2], ndc[i]);
        ndc[i][4] = e[2] < 0.0f ? -e[2] : e[2];   /* eye-space distance */
#ifdef SL_FOGSTAGE
        /* EVERY vertex carries a value. Emitting nothing for an unfogged
         * vertex left it inheriting the last fogged surface's coordinate, so
         * a surface's fog depended on what happened to be drawn before it -
         * which changes between frames, and on coplanar decal surfaces reads
         * as z-fighting. The notes' model has no "off": the RSP computes a
         * fog value per vertex and the blender mixes toward the fog colour,
         * so no fog IS a value of zero. */
        glFogCoordf(fogm != FOG_NONE
                    ? (GLfloat) fog_coord(fogm, g_vdepth[idx[i]]) : 0.0f);
#endif
        if (g_cc_a_prim1 && glassmark_on()) {
            glColor4ub(255, 0, 255, 255);          /* SL_GLASS_MARK */
            g_gp_marked++;
        } else
        if (g_tint_on) {
            /* (X - 0) * K + 0. When X is TEXEL0 the RDP never reads SHADE, so
             * the vertex colour must NOT be multiplied in on top of K; when X
             * is COMBINED and cycle 0 named SHADE, it must. */
            unsigned r = g_tint_shade ? (unsigned) v->r : 255u;
            unsigned g = g_tint_shade ? (unsigned) v->g : 255u;
            unsigned b2 = g_tint_shade ? (unsigned) v->b : 255u;
            glColor4ub((unsigned char) ((r  * g_tint[0] + 127u) / 255u),
                       (unsigned char) ((g  * g_tint[1] + 127u) / 255u),
                       (unsigned char) ((b2 * g_tint[2] + 127u) / 255u),
                       tri_alpha(v));
        } else
        if (tri_id_option()) {
            /* 24 bits of ID spread over RGB, low bits in blue so adjacent
             * triangles are visibly different rather than a smooth ramp. */
            unsigned id = g_tri_id;
            glColor4ub((unsigned char) ((id >> 4) & 0xff),
                       (unsigned char) ((id >> 12) & 0xff),
                       (unsigned char) (((id & 0xf) << 4) | 0x08), 255);
        } else if (fog_viz_option()) {
            /* THREE INDEPENDENT FACTS, so black cannot mean two things.
             *   R = is this triangle classified as fogged at all?
             *   G = the depth Sightline believes this vertex has
             *   B = the fog factor that depth produces
             * Greyscale conflated "not classified" with "factor zero", and I
             * asserted the second without measuring the first. */
            float d  = g_vdepth[idx[i]];
            float ff = (fogm != FOG_NONE) ? fog_coord(fogm, d) : 0.0f;
            if (d < 0.0f) d = 0.0f;
            if (d > 1.0f) d = 1.0f;
            glColor4ub((unsigned char) (fogm != FOG_NONE ? 255 : 0),
                       (unsigned char) (d  * 255.0f + 0.5f),
                       (unsigned char) (ff * 255.0f + 0.5f), 255);
        } else
        if (texgen_mark() && (g_geom_mode & G_GEOM_TEXTURE_GEN))
            glColor4ub(255, 0, 255, 255);              /* locator */
        else
        if (light_on() && g_vlrgb_ok[idx[i]])
            /* B-051 STAGE 4, the one behaviour change, DEFAULT OFF.
             *
             * CONTAINMENT IS STRUCTURAL, not a level list: g_vlrgb_ok is set
             * only for vertices loaded while G_LIGHTING was in force, so a
             * draw that never sets that bit cannot reach this branch however
             * the toggle is set. Ordinary room geometry does not set
             * G_LIGHTING - which is exactly what the SL_VTX_NRM probe exists
             * to check, and a run that reports rooms means the probe is
             * broken rather than that the containment is.
             *
             * Alpha is UNCHANGED and deliberately so: cn[3] is alpha under
             * G_LIGHTING exactly as it is without it (gbi.h's Vtx union), so
             * tri_alpha(v) is already right and lighting has no business
             * touching it. Precedence over the neutral arm is explicit; the
             * two are alternative arms and are never meant to be combined. */
            glColor4ub(g_vlrgb[idx[i]][0], g_vlrgb[idx[i]][1],
                       g_vlrgb[idx[i]][2], tri_alpha(v));
        else
        if (texgen_neutral() && g_vgen_ok[idx[i]])
            glColor4ub(255, 255, 255, tri_alpha(v));   /* isolation arm */
        else
        /* B-119. Mode 9 reads the LOD fraction from the primary colour's
         * alpha (the family's real alpha is the strip's, emitted by the
         * texenv, so the vertex alpha is free to carry it). */
        glColor4ub(v->r, v->g, v->b,
                   g_tex1_lodlive ? g_lodfrac_v[i] : tri_alpha(v));
        if (g_tex_gl_on && g_st_tex_w) {
            /* S/T scale. The notes settle this by worked example rather than
             * by naming a fixed-point format: "Absolute Basic Rendering
             * Models.txt" builds a 32-unit square whose s values run 0x0000 to
             * 0x0020 and states that a 32x32 image "will fit precisely", and
             * that for a 64x64 image "the 0020 needs to be 0040". So one unit
             * of s spans one texel BEFORE the RDP's own 5-bit fraction, i.e.
             * the stored value is texels<<5 == 10.5 fixed point, and the
             * texture coordinate is s / 32 / width. SL_ST_SHIFT names it so
             * the measurement below (sl_tex: st-max) can contradict it. */
            unsigned th = g_st_tex_h ? g_st_tex_h : 1;
            unsigned a;
            a = (unsigned) (v->s < 0 ? -v->s : v->s);
            if (a > g_st_max_abs) g_st_max_abs = a;
            if (a <= 4u * SL_ST_SCALE * g_st_tex_w) g_st_in++; else g_st_out++;
            a = (unsigned) (v->t < 0 ? -v->t : v->t);
            if (a > g_st_max_abs) g_st_max_abs = a;
            /* B-051 WORK ITEM 4. The ST range of the PROBED draw, in raw
             * units; texels are these over SL_ST_SCALE. tile_texture cannot
             * see vertices, so the range is gathered here and reported at
             * frame end. It is the number that separates "the texture is
             * black" from "we are sampling somewhere black". */
            if (g_tp_pending && g_tp_pend_n < 8) {
                int k = g_tp_pend_n++;
                g_tp_pv_s[k] = v->s; g_tp_pv_t[k] = v->t;
                g_tp_pv_r[k] = v->r; g_tp_pv_g[k] = v->g;
                g_tp_pv_b[k] = v->b; g_tp_pv_a[k] = v->a;
                if (g_tp_pend_n == 3) {
                    int j;
                    fprintf(stderr, "  THIS DRAW's vertices"
                            " (tex %ux%u, s/t are texels = raw/%d):\n",
                            g_st_tex_w, g_st_tex_h, SL_ST_SCALE);
                    for (j = 0; j < 3; j++)
                        fprintf(stderr,
                            "    v%d  s=%6d t=%6d  (%7.2f,%7.2f texels"
                            "  u=%6.3f v=%6.3f)  rgba=%3u,%3u,%3u,%3u\n",
                            j, g_tp_pv_s[j], g_tp_pv_t[j],
                            (double) g_tp_pv_s[j] / (double) SL_ST_SCALE,
                            (double) g_tp_pv_t[j] / (double) SL_ST_SCALE,
                            (double) g_tp_pv_s[j] / (double) SL_ST_SCALE
                                / (double) (g_st_tex_w ? g_st_tex_w : 1),
                            (double) g_tp_pv_t[j] / (double) SL_ST_SCALE
                                / (double) (g_st_tex_h ? g_st_tex_h : 1),
                            g_tp_pv_r[j], g_tp_pv_g[j], g_tp_pv_b[j],
                            g_tp_pv_a[j]);
                    g_tp_pending = 0;
                }
            }
            if (g_tp_word_now) {
                if (g_tp_stn == 0) {
                    g_tp_smin = g_tp_smax = v->s;
                    g_tp_tmin = g_tp_tmax = v->t;
                } else {
                    if (v->s < g_tp_smin) g_tp_smin = v->s;
                    if (v->s > g_tp_smax) g_tp_smax = v->s;
                    if (v->t < g_tp_tmin) g_tp_tmin = v->t;
                    if (v->t > g_tp_tmax) g_tp_tmax = v->t;
                }
                g_tp_stn++;
            }
            /* B-051 STAGE 3, the one behaviour change, DEFAULT OFF.
             *
             * g_vgen_ok is set only for vertices loaded while G_TEXTURE_GEN
             * was in force, so this branch is structurally unreachable for
             * ordinary geometry however the toggle is set - containment is a
             * property of the condition, not of a level list.
             *
             * Here is where the gSPTexture scale applies: the RSP hands the
             * RDP a coordinate in S10.5 texels, so the pre-scale 0..32768
             * generated at load becomes gen * scale / 65536, and this GL path
             * wants that over 32 (the S10.5 fraction) and over the image
             * width. Unity scale (0xffff, the notes' "ignore ST") therefore
             * leaves an ordinary coordinate essentially untouched, which is
             * why nothing else in the frame can move. */
            if (texgen_on() && g_vgen_ok[idx[i]]) {
                float sc = (float) g_tex_sscale / 65536.0f;
                float tc = (float) g_tex_tscale / 65536.0f;
                /* The LOD DIAGNOSTIC was confounded on its first run and the
                 * numbers it produced were void: the generated coordinate is
                 * in texels of the BASE level, and dividing it by a SMALLER
                 * bound tile's width overflows UV, so clamp flattened the
                 * pane and the arm measured its own artefact. Tiles 1 and 2
                 * carry shifts/shiftt 1 and 2 for exactly this - the RDP
                 * shifts the incoming coordinate right per level
                 * (ucode05_old.txt "F5 rdp_settile", shiftt 0x00003C00 /
                 * shifts 0x0000000F; gbi.h:3401 gDPSetTile packs them) - so
                 * compensate with the bound tile's own shift. Still
                 * DIAGNOSTIC: it says whether the missing mip pipeline moves
                 * the result, never which level is right. */
                if (texgen_lod()) {
                    const struct sl_tile *lt =
                        &g_tile[(g_tex_tile + (unsigned) texgen_lod()) & 7u];
                    sc /= (float) (1u << (lt->shifts & 0xfu));
                    tc /= (float) (1u << (lt->shiftt & 0xfu));
                }
                glTexCoord2f(g_vgen[idx[i]][0] * sc
                                / (float) SL_ST_SCALE / (float) g_st_tex_w,
                             g_vgen[idx[i]][1] * tc
                                / (float) SL_ST_SCALE / (float) th);
            } else
            /* B-117: the RDP shifts the incoming coordinate per the render
             * tile's shift BEFORE the tile origin subtracts. */
            glTexCoord2f(((float) v->s / g_st_sdiv / (float) SL_ST_SCALE
                              - g_st_uls) / (float) g_st_tex_w,
                         ((float) v->t / g_st_tdiv / (float) SL_ST_SCALE
                              - g_st_ult) / (float) th);
#ifdef SL_MULTITEX
            /* B-048. The same s/t, through tile 1's own shift and size. The
             * RDP shifts the RSP's coordinate right by the tile's shift
             * before the lookup, so this is the whole of the difference
             * between the two units - see the block at tile1_texture.
             * B-107 adds tile 1's own F2 origin, subtracted after the shift
             * exactly as unit 0's is: the water's two tiles scroll at
             * different rates, and that difference IS the layered motion. */
            if (g_tex1_gl_on && g_tex1_w) {
                unsigned t1h = g_tex1_h ? g_tex1_h : 1;
                float ss = (float) v->s / g_tex1_sdiv;   /* B-117 both forms */
                float tt = (float) v->t / g_tex1_tdiv;
                glMultiTexCoord2f(GL_TEXTURE1,
                                  (ss / (float) SL_ST_SCALE - g_tex1_uls)
                                      / (float) g_tex1_w,
                                  (tt / (float) SL_ST_SCALE - g_tex1_ult)
                                      / (float) t1h);
            }
#endif
        }
        if (g_clipemit)
            glVertex4f(g_clipemit_pos[i][0], g_clipemit_pos[i][1],
                       g_clipemit_pos[i][2], g_clipemit_pos[i][3]);
        else
            glVertex3f(e[0], e[1], e[2]);
        if (e[0] < g_min[0]) g_min[0] = e[0];
        if (e[0] > g_max[0]) g_max[0] = e[0];
        if (e[1] < g_min[1]) g_min[1] = e[1];
        if (e[1] > g_max[1]) g_max[1] = e[1];
        if (e[2] < g_min[2]) g_min[2] = e[2];
        if (e[2] > g_max[2]) g_max[2] = e[2];
    }
    if (rd) {
        /* B-125. THE REDRAW'S DEPTH PASS: the exact z, written only through
         * the stencil mark the colour pass left, which is cleared behind it.
         * Colour off, offset off, compare ALWAYS (the mark IS the decision),
         * alpha test off (no texture or colour is emitted here, so it could
         * only reject what the colour pass already accepted). Then every
         * piece of state rm_apply's caches describe is put back exactly. */
        batch_end();
        if (g_rm_zwrite) {
            glDisable(GL_POLYGON_OFFSET_FILL);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_ALWAYS);
            if (g_rm_atest > 0) glDisable(GL_ALPHA_TEST);
            glStencilFunc(GL_EQUAL, 1, 0xffu);
            glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
            glBegin(GL_TRIANGLES);
            for (i = 0; i < 3; i++) {
                if (g_clipemit)
                    glVertex4f(g_clipemit_pos[i][0], g_clipemit_pos[i][1],
                               g_clipemit_pos[i][2], g_clipemit_pos[i][3]);
                else
                    glVertex3f(g_veye[idx[i]][0], g_veye[idx[i]][1],
                               g_veye[idx[i]][2]);
            }
            glEnd();
            glDisable(GL_STENCIL_TEST);
            if (g_rm_atest > 0) glEnable(GL_ALPHA_TEST);
            glDepthFunc(zfunc_lequal_on() ? GL_LEQUAL : GL_LESS);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            g_rd_tris_z++;
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        glDepthMask(g_rm_zwrite ? GL_TRUE : GL_FALSE);
    }
    cca_note_tri();
    /* LIVE OWNER BUG MARK. Observational only - see the block above
     * emit_tri. Nothing here can alter what was just rasterised: the
     * vertices are already submitted and this reads the same ndc rows
     * gp_note reads on the line below. */
    if (g_mark_on) mark_note_tri((const float (*)[5]) ndc, fogm, idx);
    gp_note((const float (*)[5]) ndc, 0);
    if (g_cc_a_prim1) gp_note((const float (*)[5]) ndc, 1);

    /* SL_TRI_WATCH=<first>[,<last>] - name the draw a PIXEL belongs to.
     *
     * SL_TRI_ID paints each triangle with its own number, so a region of the
     * framebuffer can be turned back into a triangle id by reading the colour.
     * This closes the loop: given the id, say which combiner, colours,
     * texture, render mode and fog classification produced it. Without it the
     * id is a number with nothing on the other end of it, which is how "the
     * backdrop shows through" survived three rounds as an explanation for a
     * region that a real quad was rasterising all along. */
    {
        static int lo = -2, hi;
        if (lo == -2) {
            const char *v = getenv("SL_TRI_WATCH");
            if (v == NULL) { lo = -1; }
            else { lo = atoi(v);
                   { const char *c = v; while (*c && *c != ',') c++;
                     hi = *c ? atoi(c + 1) : lo; } }
        }
        if (lo >= 0 && (int) g_tri_id >= lo && (int) g_tri_id <= hi) {
            extern unsigned sl_record_index(void);
            fprintf(stderr, "sl_triw: read=%u tri=%u cc=%08x %08x"
                            " prim=%02x%02x%02x%02x env=%02x%02x%02x%02x"
                            " om=%08x tex=%ux%u glass=%d tint=%d fog=%d"
                            " fogcol=%02x%02x%02x%02x geom=%08x"
                            " texenv=%d aeq-u=%d,%d,%d,%d prim-u=%d\n",
                    sl_record_index(), g_tri_id, g_cc_w0, g_cc_w1,
                    g_prim[0], g_prim[1], g_prim[2], g_prim[3],
                    g_env[0], g_env[1], g_env[2], g_env[3],
                    g_om_l, g_st_tex_w, g_st_tex_h,
                    g_cc_a_prim1, g_tint_on, fogm,
                    g_fog[0], g_fog[1], g_fog[2], g_fog[3], g_geom_mode,
                    g_texenv, g_aeq_u[0], g_aeq_u[1], g_aeq_u[2], g_aeq_u[3],
                    g_aeq_u_prim);
            fprintf(stderr, "sl_triw:   ndc (%.3f,%.3f,%.3f) (%.3f,%.3f,%.3f)"
                            " (%.3f,%.3f,%.3f)  rgba %02x%02x%02x%02x\n",
                    ndc[0][0], ndc[0][1], ndc[0][2],
                    ndc[1][0], ndc[1][1], ndc[1][2],
                    ndc[2][0], ndc[2][1], ndc[2][2],
                    g_vbuf[idx[0]].r, g_vbuf[idx[0]].g,
                    g_vbuf[idx[0]].b, g_vbuf[idx[0]].a);
            /* ...and the ALPHA half of the combiner word, spelled out.
             *
             * Rule 7. ucode05.txt punts FC to a SETCOMBINE.HTM that is not in
             * the corpus; ucode05_old.txt carries the table and is the
             * authority here for the fourth time. Its FC entry gives the
             * alpha muxes as THREE bits each - Aa1 00E00000, Ac1 001C0000,
             * Ab1 00000038, Ad1 00000007 - over a separate "alpha combiner"
             * value list (0 combined, 1 texel0, 2 texel1, 3 primitive,
             * 4 shade, 5 environment, 6 primlodfrac/1, 7 the constant 0).
             *
             * The point of printing it beside the texture is that the RDP's
             * RGB and ALPHA are INDEPENDENT equations, while GL_MODULATE
             * couples them (out.a = tex.a * in.a). An alpha equation with a
             * non-zero ADDEND cannot be expressed that way at all. */
            {
                static const char *an[8] = {"COMB","TEX0","TEX1","PRIM",
                                            "SHADE","ENV","1","0"};
                unsigned aa0 = (g_cc_w0 >> 12) & 7, ab0 = (g_cc_w1 >> 12) & 7;
                unsigned ac0 = (g_cc_w0 >>  9) & 7, ad0 = (g_cc_w1 >>  9) & 7;
                unsigned aa1 = (g_cc_w1 >> 21) & 7, ab1 = (g_cc_w1 >>  3) & 7;
                unsigned ac1 = (g_cc_w1 >> 18) & 7, ad1 =  g_cc_w1        & 7;
                fprintf(stderr, "sl_triw:   alpha0=(%s-%s)*%s+%s"
                                "  alpha1=(%s-%s)*%s+%s   [RDP saturates;"
                                " a non-zero addend cannot be a GL_MODULATE]\n",
                        an[aa0], an[ab0], an[ac0], an[ad0],
                        an[aa1], an[ab1], an[ac1], an[ad1]);
            }
            /* ...and the TEXTURE this triangle samples, found by the GL name
             * that is actually bound rather than by the tile state, so what
             * is reported is what the fragment stage will read. Under
             * GL_MODULATE the texel alpha multiplies the vertex alpha, so
             * `alpha max=0` here is sufficient on its own to explain an
             * invisible surface. */
            if (g_tex_slot >= 0) {
                const struct sl_texent *te = &g_texcache[g_tex_slot];
                unsigned k, share = 0;
                for (k = 0; k < TEX_CACHE_N; k++)
                    if (g_texcache[k].gl == te->gl) share++;
                fprintf(stderr, "sl_triw:   tex slot=%d gl=%u on=%d src=%p"
                                " pal=%p fmt=%u %ux%u flags=%u"
                                " alpha min=%u max=%u mean=%u zero=%u/%u"
                                " (%u entries share this gl name)\n",
                        g_tex_slot, (unsigned) te->gl, g_tex_gl_on,
                        te->src, te->pal, te->fmt, te->w, te->h,
                        te->flags, te->amin, te->amax, te->asum,
                        te->azero, te->atot, share);
            } else {
                fprintf(stderr, "sl_triw:   tex gl=%u on=%d NO SLOT RECORDED\n",
                        (unsigned) g_tex_gl_bound, g_tex_gl_on);
            }
        }
    }

    /* Window-space area of the triangle just emitted.
     *
     * This exists because B-019 produced the one shape of evidence that is
     * always a trap: every counter healthy, nothing on screen. 732 triangles
     * "textured", 729 of 2196 vertices inside the NDC cube, zero rejects -
     * and a capture with exactly two colours in it, the fill and the
     * letterbox. A vertex being inside the cube says nothing about whether
     * the TRIANGLE covers a pixel, so that is what gets measured here: the
     * cross product of two edges, scaled to the 320x240 framebuffer. A
     * collapsed triangle rasterises nothing while every count above it still
     * reads correct. */
    if (ndc[0][3] != 0.0f && ndc[1][3] != 0.0f && ndc[2][3] != 0.0f) {
        float ax = (ndc[1][0] - ndc[0][0]) * (float) g_scr_w * 0.5f;
        float ay = (ndc[1][1] - ndc[0][1]) * (float) g_scr_h * 0.5f;
        float bx = (ndc[2][0] - ndc[0][0]) * (float) g_scr_w * 0.5f;
        float by = (ndc[2][1] - ndc[0][1]) * (float) g_scr_h * 0.5f;
        float ar = (ax * by - ay * bx) * 0.5f;
        /* Signed while it still is: see g_cull_n_ccw. A positive cross product
         * in this NDC (y up, the projection carries no flip - measured, proj
         * row 1 is +1.732 on every level sampled) is a counter-clockwise
         * winding, which is GL's default front face. */
        if (g_geom_mode & (GEOM_CULL_FRONT | GEOM_CULL_BACK)) {
            double zc = ((double) ndc[0][2] + ndc[1][2] + ndc[2][2]) / 3.0;
            if (ar > 0.0f)      { g_cull_n_ccw++; g_cull_z_ccw += zc; }
            else if (ar < 0.0f) { g_cull_n_cw++;  g_cull_z_cw  += zc; }
        }
        if (ar < 0.0f) ar = -ar;
        if (ar > 500.0f)
            g_cull_big[((g_geom_mode & GEOM_CULL_FRONT) ? 1u : 0u) |
                       ((g_geom_mode & GEOM_CULL_BACK)  ? 2u : 0u)]++;
        g_tri_area_sum += (double) ar;
        if (ar < 0.25f) g_tri_degen++; else g_tri_area++;
        if (ar > g_tri_area_max) g_tri_area_max = ar;
        /* #47 PART A. The geometry half of the per-id coverage census. The
         * area just computed is the WHOLE triangle's, offscreen included,
         * which is right for "did this rasterise anything" and wrong for
         * "how much of the picture is this" - so the census takes its own,
         * clipped to the viewport. */
        if (texcov_armed())
            texcov_add(g_tex_gl_on ? g_tex_slot : -1,
                       ndc_tri_area_clipped(ndc), 1);
    } else {
        g_tri_clipped++;
    }
    g_cull_tris[((g_geom_mode & GEOM_CULL_FRONT) ? 1u : 0u) |
                ((g_geom_mode & GEOM_CULL_BACK)  ? 2u : 0u)]++;
    nearwit_note(a, b, c);          /* B-100 - see the nearwit_note block */

    /* B-043 probe. Off unless SL_FADE_DBG names it, because the edge table it
     * feeds is real work in a per-triangle path.
     *
     * SL_FADE_DBG=1 scopes it to the faded-character combiner - the same
     * selector tri_alpha uses, and the draw the bug was reported against.
     * SL_FADE_DBG=2 widens it to EVERY triangle the default cull mode acts
     * on: cull bits set and no depth write. That is what says whether the
     * winding this file computes can be trusted on the whole of that set, or
     * only on the character. */
    if (fd_dbg() &&
        (fd_dbg() == 2
             ? ((g_geom_mode & (GEOM_CULL_FRONT | GEOM_CULL_BACK)) != 0 &&
                !(g_om_l & RM_Z_UPD))
             : g_cc_a_envscale)) {
        unsigned i;
        int s = tri_sign_of(a, b, c);
        g_fd_tris++;
        if (s > 0) g_fd_ccw++; else if (s < 0) g_fd_cw++; else g_fd_zero++;
        g_fd_cull[((g_geom_mode & GEOM_CULL_FRONT) ? 1u : 0u) |
                  ((g_geom_mode & GEOM_CULL_BACK)  ? 2u : 0u)]++;
        g_fd_geom_seen |= g_geom_mode;
        g_fd_slot_tris[g_fd_slot % 5]++;
        fd_tri_note(g_vsrc[a], g_vsrc[b], g_vsrc[c]);
        fd_edge_note(g_vsrc[a], g_vsrc[b]);
        fd_edge_note(g_vsrc[b], g_vsrc[c]);
        fd_edge_note(g_vsrc[c], g_vsrc[a]);
        for (i = 0; i < g_fd_om_used; i++)
            if (g_fd_om[i] == g_om_l) break;
        if (i < 8) {
            if (i == g_fd_om_used) { g_fd_om[i] = g_om_l; g_fd_om_n[i] = 0;
                                     g_fd_om_used++; }
            g_fd_om_n[i]++;
        }
    }

    ccx_note(0);
    g_tris++;
}

/* ---- B-100 FIX, second form. GL REJECTS at the near plane; the RDP does not.
 *
 * WHAT IS ESTABLISHED, AND WHAT IS NOT. The owner tested the cartridge on
 * 2026-09-09: walking into a wall on the ROM does NOT open these holes. That
 * fixes the required OUTPUT and nothing more. The RSP/RDP algorithm is NOT
 * reverse-engineered here and this file does not claim it is - the notes are
 * silent on the fixed near and camera planes (G_MW_CLIP lists only the four
 * PROGRAMMABLE side planes, ucode05.txt:567-570, ucode05_old.txt:483-486; see
 * the not_covered entry in docs/doc-routing.json). What justifies this change
 * is not knowledge of the hardware but a DEMONSTRATED MATHEMATICAL FLAW in
 * the previous reconstruction, described next.
 *
 * WHY ad772496 DID NOT FIX THE WALL. It gated the whole path behind a
 * near_w_of_proj() that returned "not a perspective matrix, use the old path"
 * whenever g_proj[15] was nonzero. But g_proj is not always the textbook
 * perspective: a G_MTX on the projection stack WITHOUT G_MTX_LOAD multiplies
 * into it, and room geometry is drawn under exactly such a product. Measured
 * live, windowed Dam, 900 frames: on the draws that cross,
 *
 *     p12 = -136.6009   p13 = 130.5827   p14 = 205.175232   p15 = 264.236389
 *
 * and the census over 1.2M emit_tri calls read crossAll = 23732, handled = 0,
 * BYPASSED = 23732 - every near-crossing triangle took the old GL path. The
 * screenshot agreed independently: the hole boundary still followed GL's
 * ORIGINAL z = -w cut to under 0.6 px on six rows.
 *
 * AND SIMPLY DELETING THAT GUARD IS ALSO WRONG, which is why this is a
 * rewrite rather than a one-line removal. ad772496 saturated by scaling the
 * EYE vector along its ray. That preserves the screen position x/w and y/w
 * only when p12 == p13 == p15 == 0. The matrix above has none of them zero,
 * so the scaling MOVES geometry on screen; and with the guard gone the
 * affected population grew about 40x, reaching water, explosions, the
 * viewmodel and the sky.
 *
 * THE CORRECTION: SATURATE IN CLIP SPACE. Transform the vertex under the
 * matrix actually loaded, giving C = (x, y, z, w), and move it to the near
 * boundary by setting
 *
 *     z := -w        leaving x, y and w untouched
 *
 * The screen position is x/w and y/w, and neither x, y nor w is altered, so
 * the vertex CANNOT move on screen - for any projection, multiplied or not,
 * with no assumption about p12, p13 or p15. That is the invariant ad772496
 * lacked, and it holds by construction rather than by luck. The depth becomes
 * z/w = -1, the near end of the range, which is what the RDP produces when a
 * screen z runs off the end of the depth range. Perspective-correct
 * interpolation is untouched because w is untouched.
 *
 * BEHIND THE EYE is a different event and is still handled first, because a
 * vertex with w <= 0 has no valid screen position to preserve: those
 * triangles are clipped geometrically, in clip space, with position, S/T,
 * colour/alpha and fog depth interpolated, and the survivors are then
 * saturated. What changed across the forms below is WHERE that clip cuts.
 *
 * THE FINITE CAP WAS THE HOLE, and it took three forms to see it. The first
 * form cut the triangle at a fraction of its OWN largest w (wmax/4096); that
 * plane is not shared, so two triangles meeting along a crossing edge cut it
 * in different places and a crack opened BETWEEN them. The second form
 * derived one plane from the projection alone (|p14+p15|/4096), which welds
 * neighbours - and the bridge still showed the wedge, because the defect was
 * never only between triangles: the cap edge ITSELF is wrong.
 *
 * A triangle with vertices on both sides of the eye plane w = 0 projects to
 * an UNBOUNDED screen region. Walking along a crossing edge toward w = 0+,
 * (x/w, y/w) runs to infinity in the direction fixed by that edge's (x, y)
 * at its w = 0 point, and the two crossing edges head to infinity in
 * DIFFERENT directions; the true boundary between them is an arc of the line
 * at infinity. Any cap at small positive w joins two finite approximations
 * of those directions with a straight edge - a SECANT across that arc - and
 * when the directions diverge widely (a long deck triangle viewed steeply
 * down its length, the Dam walking bridge) the secant passes through the
 * viewport. Measured on the bridge witness, the visible hole boundary IS the
 * generated cap edge: luminance-gradient 58.58 and 47.34 against a
 * random-chord control mean of 19.95 (p90 32.88, max 53.08), while the real
 * shared mesh edge scores 20.10 and the fan's interior diagonals 12.1-12.5.
 * The owner confirmed on the cartridge, 2026-09-09, that the ROM opens no
 * such hole. No epsilon can fix it: the cap's distance from the projection
 * origin scales as 1/epsilon, so tuning only MOVES the chord (the shared
 * plane made wmin 0.115 where the old form had 0.027 under a measured
 * multiplied projection, |p14+p15| = 469 - closer, not further). And
 * clipping the capped polygon against strict frustum side planes afterwards
 * was measured catastrophic - cross=22993, fanTris=263, sat=0, near-total
 * mass discard - because sub-near geometry has enormous |x/w|, |y/w| and
 * lies outside every strict side plane. The cap must never be created.
 *
 * SO THE CAP IS GONE. The triangle - still a bounded planar simplex in clip
 * space; the unboundedness only appears after the divide - is clipped by
 * Sutherland-Hodgman against four GUARD PLANES through the clip-space
 * origin:
 *
 *     x <= G*w      -x <= G*w      y <= G*w      -y <= G*w
 *
 * Jointly these say |x| <= G*w and |y| <= G*w, which IMPLIES w >= 0, with
 * w = 0 possible only at the clip-space origin itself - so there is no
 * eye-plane epsilon left to tune. The guard cone is exactly the preimage
 * under projection of the NDC box [-G, G]^2, so the clipped polygon's
 * projected coverage is the original projective coverage of the w > 0 part
 * intersected with that box; for any G > 1 the viewport sits strictly inside
 * the box and coverage over viewport pixels is preserved EXACTLY. Every
 * GENERATED edge lies ON a guard plane and projects onto |x/w| = G or
 * |y/w| = G, wholly outside the viewport - so no artificial edge can cut
 * viewport pixels, by construction rather than by the value of G. G is not a
 * tuning constant: every G > 1 draws the same pixels; it only decides where
 * OFFSCREEN the generated edges land. GL then cuts the emitted fan back to
 * the real frustum with its own side planes, which is ordinary
 * projectively-correct clipping because everything it is handed already has
 * w >= 0.
 *
 * The intersection parameter is t = d_i / (d_i - d_k) with d = G*w -+ x or
 * G*w -+ y - LINEAR in clip space, so generated attributes interpolate with
 * exactly the projective correctness of the previous near-plane lerp. Inside
 * a sign-change branch d_i and d_k have strictly opposite signs (>= 0 vs
 * < 0), so the denominator cannot vanish: no epsilon there either. A
 * triangle wholly at or behind the eye clips to nothing and is dropped,
 * which is what both the ROM and GL do with it.
 *
 * CONTAINMENT. Three populations, three paths, decided in this order. A
 * triangle with no near-violating vertex takes the early return into
 * emit_tri_slots, which is the original emit_tri renamed and otherwise
 * unchanged, so ordinary room geometry, sky, water, explosions, bullet
 * impacts, ground markings and the viewmodel never reach this code. A
 * near-violating triangle whose vertices ALL have w > 0 is CLIPPED at the
 * near plane z + w >= 0 - the cut the cartridge itself was measured making
 * (99771d53, five triangles fitted to 1.3 px), and the guard planes never
 * see it. An earlier form saturated this population instead of cutting it;
 * that kept the z < -w region, whose screen-linear depth sweeps the whole
 * range and depth-fights mid-scene geometry along a line that jumps with
 * every frame's w - the flickering black bridge wedges of run
 * 20260909-220610. The two owner cartridge facts split exactly along this
 * population line: the wall that must FILL when the camera touches it
 * (ad772496) is an EYE-CROSSING triangle - its witness vertices carry
 * w = -1.3 / -7.7 - so only w <= 0 triangles keep their sub-near coverage,
 * through the guard clip with z := -w saturation. Draws under G_TEXTURE_GEN
 * are excluded too: a generated vertex would need generated texgen
 * coordinates, and reusing a parent's would be silently wrong rather than
 * merely approximate. SL_NEARSAT=0 restores the old GL behaviour for
 * bisection. */
#define NEAR_GUARD 2.0f
#define NEARG_MAX  12   /* 3 verts + 4 planes reach 7; the rest is headroom */

struct nearv { float e[3]; float c[4]; float d; struct vtx v; };

static int nearsat_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_NEARSAT");
                  on = !(v != NULL && *v == '0'); }
    return on;
}

/* B-131. Default ON: a clipper-generated vertex's fog depth is its own z/w.
 * SL_CLIPDEPTH=0 restores the lerp of the endpoints' depths, for the A/B. */
static int clipdepth_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_CLIPDEPTH");
                  on = !(v != NULL && *v == '0'); }
    return on;
}

/* Clip coordinates of an eye-space point under the projection in force - the
 * same product, in the same column-major order, that ndc_account forms and
 * that GL would form from glVertex3f. */
static void near_clip4(const float *e, float *c)
{
    int j;
    for (j = 0; j < 4; j++)
        c[j] = e[0] * g_proj[j] + e[1] * g_proj[4 + j]
             + e[2] * g_proj[8 + j] + g_proj[12 + j];
}

static void nearv_lerp(struct nearv *o, const struct nearv *p,
                       const struct nearv *q, float t)
{
    int j;
    for (j = 0; j < 3; j++) o->e[j] = p->e[j] + t * (q->e[j] - p->e[j]);
    for (j = 0; j < 4; j++) o->c[j] = p->c[j] + t * (q->c[j] - p->c[j]);
    o->d = p->d + t * (q->d - p->d);
    o->v = p->v;                       /* flag and the rest ride along */
    o->v.s = (short) (p->v.s + t * ((float) q->v.s - (float) p->v.s));
    o->v.t = (short) (p->v.t + t * ((float) q->v.t - (float) p->v.t));
    o->v.r = (unsigned char)
             (p->v.r + t * ((float) q->v.r - (float) p->v.r) + 0.5f);
    o->v.g = (unsigned char)
             (p->v.g + t * ((float) q->v.g - (float) p->v.g) + 0.5f);
    o->v.b = (unsigned char)
             (p->v.b + t * ((float) q->v.b - (float) p->v.b) + 0.5f);
    o->v.a = (unsigned char)
             (p->v.a + t * ((float) q->v.a - (float) p->v.a) + 0.5f);
}

/* One Sutherland-Hodgman pass against the half-space
 *
 *     px*x + py*y + pz*z + pw*w >= 0
 *
 * - a PLANE THROUGH THE CLIP-SPACE ORIGIN, which is what makes the pass
 * projectively exact with no epsilon anywhere: the intersection parameter
 * t = d_i / (d_i - d_k) is linear in clip space, and inside a sign-change
 * branch d_i and d_k have strictly opposite signs so the denominator cannot
 * vanish. Convex in, convex out, winding preserved, at most one vertex
 * gained per plane. Two callers: the four B-100 guard planes
 * (-+1, 0, 0, G) / (0, -+1, 0, G), and the near plane (0, 0, 1, 1), i.e.
 * z >= -w. The bound check is defensive only; convex input cannot hit it. */
static int nearh_clip(struct nearv *dst, const struct nearv *src, int n,
                      float px, float py, float pz, float pw)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        int k = (i + 1) % n;
        float di = px * src[i].c[0] + py * src[i].c[1]
                 + pz * src[i].c[2] + pw * src[i].c[3];
        float dk = px * src[k].c[0] + py * src[k].c[1]
                 + pz * src[k].c[2] + pw * src[k].c[3];
        if (di >= 0.0f && m < NEARG_MAX) dst[m++] = src[i];
        if ((di < 0.0f) != (dk < 0.0f) && m < NEARG_MAX) {
            /* Strictly opposite signs, so di - dk != 0: no epsilon. */
            float t = di / (di - dk);
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            nearv_lerp(&dst[m++], &src[i], &src[k], t);
        }
    }
    return m;
}

static void emit_tri(int a, int b, int c)
{
    struct nearv in[3], pa[NEARG_MAX], pb[NEARG_MAX];
    struct nearv *poly;
    struct vtx  sv[3];
    float se[3][3], sd[3];
    float wmax;
    int idx[3], i, j, n, cross, eyec, guard;

    idx[0] = a; idx[1] = b; idx[2] = c;
    for (i = 0; i < 3; i++)
        if (idx[i] < 0 || idx[i] >= 32) return;

    /* B-125. Decided from the SOURCE slots before any clipping borrows them.
     * Every triangle's positions join the submission's drawn set; a first
     * pass's edges join the edge table (a redraw's do not - the second
     * triangle of a re-triangulated quad must still see its new diagonal as
     * undrawn). A redraw - three slots from a wholly-old load - is then
     * classified: re-triangulated quad, with its own tent difference as the
     * tolerance, or a plain draw. */
    {
        int ps[3]; float nd[3][3]; int wok = 1;
        int flagged = g_vredraw[a] && g_vredraw[b] && g_vredraw[c];
        for (i = 0; i < 3; i++) {
            const float *e = g_veye[idx[i]];
            float c0 = e[0] * g_proj[0] + e[1] * g_proj[4] + e[2] * g_proj[8]  + g_proj[12];
            float c1 = e[0] * g_proj[1] + e[1] * g_proj[5] + e[2] * g_proj[9]  + g_proj[13];
            float c2 = e[0] * g_proj[2] + e[1] * g_proj[6] + e[2] * g_proj[10] + g_proj[14];
            float c3 = e[0] * g_proj[3] + e[1] * g_proj[7] + e[2] * g_proj[11] + g_proj[15];
            ps[i] = rd_pos(e, 1);
            if (c3 > 0.0f) {
                nd[i][0] = c0 / c3; nd[i][1] = c1 / c3;
                nd[i][2] = (c2 / c3) * 0.5f + 0.5f;
            } else wok = 0;
        }
        g_rd_cur = 0; g_rd_tol = 0.0f;
        if (flagged) {
            if (wok && redraw_on() && g_rd_stencil_bits > 0
                && ps[0] >= 0 && ps[1] >= 0 && ps[2] >= 0
                && rd_classify(ps, nd, &g_rd_tol)) {
                g_rd_cur = 1;
                g_rd_tol += 4.0f / 16777215.0f;
            } else {
                g_rd_tris_same++;
            }
        } else if (wok) {
            rd_edge_note(ps[0], ps[1], ps[2], nd[2]);
            rd_edge_note(ps[1], ps[2], ps[0], nd[0]);
            rd_edge_note(ps[2], ps[0], ps[1], nd[1]);
        }
    }

    /* Three distinct slots to borrow, the toggle on, and no texgen. There is
     * deliberately NO projection test here: having one is what made the
     * previous version miss every wall in the game. */
    g_tri_cls = 0;                     /* #25 witness: ordinary path unless set */
    if (!nearsat_on() || a == b || b == c || a == c
        || (g_geom_mode & G_GEOM_TEXTURE_GEN)) {
        emit_tri_slots(a, b, c);
        return;
    }

    cross = 0; eyec = 0; guard = 0; wmax = 0.0f;
    for (i = 0; i < 3; i++) {
        float gw;
        near_clip4(g_veye[idx[i]], in[i].c);
        if (in[i].c[2] < -in[i].c[3]) cross++;  /* GL would delete part or all */
        if (in[i].c[3] <= 0.0f) eyec++;         /* at or behind the eye plane */
        if (in[i].c[3] > wmax) wmax = in[i].c[3];
        /* B-132. Outside the RSP's clip box |x|,|y| <= 2w (the game sets the
         * clip ratio to 2: gSPClipRatio(FRUSTRATIO_2), src/game/lv.c:711),
         * with the vertex still in front of the eye. */
        gw = NEAR_GUARD * in[i].c[3];
        if (in[i].c[3] > 0.0f
            && (in[i].c[0] > gw || -in[i].c[0] > gw
                || in[i].c[1] > gw || -in[i].c[1] > gw))
            guard++;
    }
    /* B-132. THE GUARD CLIP IS THE RSP'S CLIP, NOT A SPECIAL CASE FOR
     * EYE-CROSSING TRIANGLES. The block comment above says G "only decides
     * where OFFSCREEN the generated edges land" - true for coverage, and
     * true for a perspective-correct attribute, and FALSE for shade: the RDP
     * interpolates shade and the fog alpha LINEARLY IN SCREEN SPACE over the
     * polygon the RSP hands it, so the polygon's vertices - where they land
     * and what clip-space lerp they carry - ARE the visible shading. A floor
     * triangle whose nearest vertex is a few units ahead of the eye and 167
     * below it projects that vertex at |y/w| in the tens; the RSP clips it
     * against the 2w planes and shades the clipped polygon, this path handed
     * GL the whole triangle. MEASURED on the owner's Surface 2 report ("the
     * ground lights up in patches as I walk"), spawn walk under
     * SL_VI_CATCHUP=0, a shot per sim step: the near floor band read 92
     * where the cartridge at the same eye position read 110, then jumped to
     * 109 the step the vertex crossed the near plane and this code took the
     * triangle - and tracked the cartridge to within 1 from there (shots
     * 60-66: 109/107/106/104/102/100 vs 109/107/106/104/103/102; shot 129:
     * 117 vs 117). SL_NEARSAT=0 (GL's own clipping, no fan) removed the jumps
     * by staying WRONG throughout (88-91 vs 117). So the post-cut fan was
     * right and the uncut triangle was the wrong stage; guard-band
     * violations now take the fan too, and the population without one is
     * unchanged. */
    if (cross == 0 && guard == 0) { emit_tri_slots(a, b, c); return; }  /* the ordinary path */
    if (wmax <= 0.0f) return;  /* wholly at or behind the eye; both sides drop */
    g_tri_cls = (cross ? 1 : 0) | (eyec ? 2 : 0) | (guard ? 4 : 0);  /* #25 witness */

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) in[i].e[j] = g_veye[idx[i]][j];
        in[i].d = g_vdepth[idx[i]];
        in[i].v = g_vbuf[idx[i]];
    }

    if (eyec == 0) {
        /* Every vertex has w > 0: CLIP AT THE NEAR PLANE, z + w >= 0 -
         * another plane through the clip-space origin, so the cut is
         * projectively exact with no epsilon. This is the cartridge's own
         * measured behaviour for this population (99771d53: five triangles,
         * three marks, the sky/geometry boundary fitted to the z = -w cut
         * to 1.3 px, and gmain.s:784-794 retessellates on the near bit).
         * Saturating instead of cutting kept the z < -w region - and for a
         * triangle with a tiny-positive-w vertex that region is enormous
         * (measured on the B-100 flicker witness, run 20260909-220610
         * mark-001: the walkway grate quad at 20032950+0x0190 reached
         * ndc y = -26.8 with all w > 0). Rasterised with screen-linear
         * depth, that footprint sweeps through the whole depth range and
         * its depth-test line crosses mid-scene geometry, blending
         * magnified texels over it - the flickering black wedges. The cut
         * removes exactly what the RSP removes. */
        n = 3; poly = in;
        if (cross) {
            n = nearh_clip(pa, in, 3, 0.0f, 0.0f, 1.0f, 1.0f);
            if (n < 3) return;   /* wholly nearer than near; the RSP drops it */
            poly = pa;
        }
        if (guard) {
            /* B-132: the same four planes the eye-crossing population takes,
             * on the (near-cut) polygon; w > 0 already holds throughout.
             * Measured both ways on the walk: skipping the guard on the
             * near-cut population re-creates the jumps (the polygon changes
             * as the vertex crosses the near plane); taking it everywhere
             * leaves the band continuous (largest step 2.0, the cartridge's
             * own 2.3) at a level 4-17 above the cartridge on some steps -
             * the residual is recorded, the jumps were the report. */
            n = nearh_clip(pb, poly, n, -1.0f,  0.0f, 0.0f, NEAR_GUARD);
            if (n < 3) return;
            n = nearh_clip(pa, pb, n,  1.0f,  0.0f, 0.0f, NEAR_GUARD);
            if (n < 3) return;
            n = nearh_clip(pb, pa, n,  0.0f, -1.0f, 0.0f, NEAR_GUARD);
            if (n < 3) return;
            n = nearh_clip(pa, pb, n,  0.0f,  1.0f, 0.0f, NEAR_GUARD);
            if (n < 3) return;
            poly = pa;
        }
    } else {
        /* Guard clip - see the block comment above. Four planes through the
         * clip-space origin whose intersection implies w >= 0; every edge
         * they generate projects outside the viewport by construction.
         * NO near-plane cut on this population: the owner's cartridge test
         * (ad772496 witness, w = -1.3 / -7.7 vertices) shows a wall the
         * camera touches must FILL, not open - its sub-near region is kept
         * and saturated below. */
        n = nearh_clip(pa, in, 3, -1.0f,  0.0f, 0.0f, NEAR_GUARD);
        if (n < 3) return;
        n = nearh_clip(pb, pa, n,  1.0f,  0.0f, 0.0f, NEAR_GUARD);
        if (n < 3) return;
        n = nearh_clip(pa, pb, n,  0.0f, -1.0f, 0.0f, NEAR_GUARD);
        if (n < 3) return;
        n = nearh_clip(pb, pa, n,  0.0f,  1.0f, 0.0f, NEAR_GUARD);
        /* w > 0 for every survivor except a vertex AT the clip-space origin
         * (x = y = w = 0), which projects nowhere and covers nothing. */
        for (i = 0, j = 0; i < n; i++)
            if (pb[i].c[3] > 0.0f) pa[j++] = pb[i];
        n = j;
        if (n < 3) return;   /* empty projective coverage; both sides drop */
        poly = pa;
    }

    /* Saturate z := -w. x, y and w are not touched, so x/w and y/w - the
     * screen position - are bit-identical before and after. On the guard
     * output this is the real semantics (the kept sub-near region draws at
     * the near depth); on the near-clipped output every vertex already
     * satisfies z >= -w up to float residue, so this only snaps generated
     * vertices exactly onto the plane they were cut at. */
    for (i = 0; i < n; i++)
        if (poly[i].c[2] < -poly[i].c[3]) poly[i].c[2] = -poly[i].c[3];

    /* B-131. THE DEPTH OF A GENERATED VERTEX IS ITS OWN z/w, not a lerp.
     *
     * nearv_lerp carried d - the raw NDC z the RSP's fog reads
     * (vtx_transform: cz/cw) - as p.d + t*(q.d - p.d), linear in the
     * clip-space parameter. z/w is a ratio of two functions that ARE linear
     * in t, so the lerp agrees with the truth only at the two ends and can
     * be wildly off between them: on Surface 2's spawn valley the floor is
     * triangles spanning 6..246 eye units (owner mark 20260914-234420
     * mark-001, candidates seq 29-31), and every one of them crosses the
     * guard planes, so the fan is built from generated vertices. The edge
     * from 6 to 246 units cut at t = 0.5 sits at w = 126: its true NDC z is
     * 1.002 - 4.004/126 = 0.970, fog (fm 2976, fo -2720) 167/255; the lerp
     * gave (0.335 + 0.986)/2 = 0.66, fog CLAMPED TO 0. A vertex a hundred
     * units out drawn as unfogged, next to one drawn right: that is the
     * owner's "ground lighting up in patches as I walk" - the cut moves
     * with the camera, so the patches do. The RSP derives a clipped vertex's
     * fog from the clipped vertex's own screen z, which is what this
     * restores: every survivor - lerped, saturated or original (for which
     * it is the identity) - takes d from the clip coordinates it will be
     * rasterised with. SL_CLIPDEPTH=0 keeps the lerp, for the A/B. */
    if (clipdepth_on())
        for (i = 0; i < n; i++)
            if (poly[i].c[3] > 1e-6f) poly[i].d = poly[i].c[2] / poly[i].c[3];

    /* Emit as a fan through the caller's own slots, so emit_tri_slots stays
     * exactly the function it was and every per-slot array it reads keeps the
     * parent vertex's entry. Saved and restored around the loop: the RSP's
     * vertex buffer must look untouched to the next command. */
    for (i = 0; i < 3; i++) {
        sv[i] = g_vbuf[idx[i]];
        for (j = 0; j < 3; j++) se[i][j] = g_veye[idx[i]][j];
        sd[i] = g_vdepth[idx[i]];
    }
    g_clipemit = 1;
    for (i = 2; i < n; i++) {
        int src[3];
        src[0] = 0; src[1] = i - 1; src[2] = i;
        for (j = 0; j < 3; j++) {
            int s = src[j];
            g_vbuf[idx[j]]    = poly[s].v;
            g_veye[idx[j]][0] = poly[s].e[0];
            g_veye[idx[j]][1] = poly[s].e[1];
            g_veye[idx[j]][2] = poly[s].e[2];
            g_vdepth[idx[j]]  = poly[s].d;
            g_clipemit_pos[j][0] = poly[s].c[0];
            g_clipemit_pos[j][1] = poly[s].c[1];
            g_clipemit_pos[j][2] = poly[s].c[2];
            g_clipemit_pos[j][3] = poly[s].c[3];
        }
        /* The saturated fan is the one place depth cannot stand in for the
         * cull bits - see nearcull_would_drop. Only the guard (eyec > 0)
         * output is saturated below the near plane; the near-clipped fan
         * keeps real depth and stays under cull_apply like any other draw. */
        if (eyec > 0 && nearcull_would_drop(a, b, c)) { g_nearcull_dropped++; continue; }
        emit_tri_slots(a, b, c);
    }
    g_clipemit = 0;
    clipemit_proj_set(0);              /* hand the projection back */
    for (i = 0; i < 3; i++) {
        g_vbuf[idx[i]] = sv[i];
        for (j = 0; j < 3; j++) g_veye[idx[i]][j] = se[i][j];
        g_vdepth[idx[i]] = sd[i];
    }
}

/* GE_TRI4 (B1), Rare's own four-triangle command. The authority is
 * "Absolute Basic Rendering Models.txt", section "B1 4-triangle draw routine",
 * which states the packing outright:
 *
 *     B100zzzz yxyxyxyx
 *     0000000z 000000yx   first triangle
 *     000000z0 0000yx00   second triangle
 *     00000z00 00yx0000   third triangle
 *     0000z000 yx000000   fourth triangle
 *
 * i.e. the four z indices are nibbles 0..3 of the UPPER word and the x/y pairs
 * are nibbles 0..7 of the LOWER word; bits 16-23 of w0 are structurally zero.
 * Its four worked examples all check out against this reading, including
 * B1007242 65413110 = {0,1,2} {1,3,4} {1,4,2} {5,6,7}.
 *
 * The previous decode treated all eight indices as one run of nibbles
 * straddling both words, starting at w0 bit 19. Measured over a live Facility
 * frame window, 8340 of 8340 B1 commands had w0 bits 16-23 == 0x00, and on
 * dam 9000 of 9000 - so its first index was ALWAYS 0. One triangle in every
 * four was therefore pinned to vertex 0 of the batch, which is what drew the
 * tall black spikes, and the other three were permutations of real-but-wrong
 * indices, which is why surfaces still half-formed. Sample from that frame,
 * b1005432 43232010, decodes here as the strip {0,1,2} {0,2,3} {2,3,4}
 * {3,4,5}; under the old reading it was {0,5,4} {3,2,4} {3,2,3} {2,0,1}.
 *
 * The note also gives the termination rule - "If all the points are set to 0,
 * that triangle is not drawn" - and it is load-bearing, not cosmetic: a
 * command carrying fewer than four triangles zero-fills the unused slots, and
 * b10000dc 0000c545 from the same frame is exactly that shape. Without the
 * skip those become real triangles on vertex 0 rather than nothing. */
static void tri4(unsigned int w0, unsigned int w1)
{
    int i;
    for (i = 0; i < 4; i++) {
        unsigned x = (w1 >> (8 * i))     & 0xf;
        unsigned y = (w1 >> (8 * i + 4)) & 0xf;
        unsigned z = (w0 >> (4 * i))     & 0xf;
        if ((x | y | z) == 0) continue;              /* unused slot */
        g_fd_slot = i;                               /* B-043 probe */
        edge_check((int) x, (int) y, (int) z);
        emit_tri((int) x, (int) y, (int) z);
    }
}

/* ================= native asset overrides: the draw ======================
 *
 * B-1xx / NATIVE ASSET OVERRIDE. The other half of this seam is
 * src/native/sl_asset_override.c, which owns path resolution, loading and
 * validation and never touches GL. This half owns the drawing and never
 * touches a file.
 *
 * WHY IT LIVES INSIDE THE DISPLAY-LIST WALKER, and not in a game constructor:
 * a custom logo has to appear in the SAME render order and under the SAME
 * matrices as the geometry it replaces. Both of those are properties of a
 * position in the display list, not of a moment in the frame - the projection
 * comes from a 01 command earlier in the list, the modelview from another, the
 * viewport from the RSP's own, and any GL issued outside the walk would be
 * ordered against the list only by luck. So the game hooks emit ONE bridge
 * command exactly where the original geometry went (opcode 0x02, "reserved0"
 * in ucode05.txt, plus a 40-bit magic - see src/sl_asset_override.h) and the
 * walker draws here, with g_proj and g_mv[g_mv_sp] holding precisely what the
 * original draw would have seen.
 *
 * The matrix needs no conversion. g_mv is row-major under a row-vector
 * convention (v' = v * M, see vtx_transform), and GL is column-major under a
 * column-vector convention (v' = M * v). Writing the first as
 * v'_j = sum_k m[k*4+j] * v_k and the second as v'_j = sum_k a[k*4+j] * v_k
 * shows the two storages are the same 16 floats in the same order, so
 * glLoadMatrixf takes g_mv[g_mv_sp] directly.
 *
 * WHAT THIS IS NOT: not a PBR renderer, and not an emulation of the N64
 * combiner. A custom asset is rendered as its own glTF material within the
 * supported subset - baseColorFactor, baseColorTexture, vertex colour, and one
 * fixed directional light when the model supplies normals. Metallic,
 * roughness, normal maps and every other PBR input are rejected at import
 * rather than silently approximated.
 *
 * FADE IS CHOREOGRAPHY, NOT ASSET DATA. Both boot screens fade their logo in
 * through a colour the screen computes per frame (Nintendo through its light,
 * Rareware through the primitive colour). The bridge command carries that
 * value so a custom model fades on exactly the original schedule; nothing else
 * about the screens' timing, camera, rotation or transitions is touched.
 *
 * GL STATE. This draw establishes what it owns and puts it back: client
 * arrays, lighting, colour material, normalise, two-side, alpha test, blend,
 * depth mask, texture enable and binding, cull face, colour, and both
 * matrices. Where this file already caches a piece of state the cache is used
 * (cull_set, texenv_set, fog_off, tex1_off) or explicitly invalidated
 * (g_tex_gl_on / g_tex_gl_bound, rm_invalidate) so the next DL command
 * re-establishes rather than trusting a stale belief. No blanket
 * glPushAttrib: the point is to know what changed.
 */

static unsigned g_aov_draws, g_aov_part_draws, g_aov_tris_drawn;
/* SL_PAD_DBG timing of the part-only draws (see draw_asset_override). */
static int      g_aov_dbg_time;         /* set with SL_PAD_DBG by the first whole draw */
static LONGLONG g_aov_part_t0, g_aov_part_ticks, g_aov_part_ticks_last;
static unsigned g_aov_part_n, g_aov_part_n_last;

/* ---- generated coordinates for an override model -------------------------
 *
 * THE DEFECT. This path fed glTexCoordPointer the model's BAKED uv array and
 * nothing else, so a reflection map sampled through it was welded to the
 * surface: the highlight turned WITH the spinning logo instead of sweeping
 * ACROSS it. The originals do not work that way - they set G_TEXTURE_GEN and
 * the coordinate is manufactured per frame from the vertex normal under the
 * matrix in force. No model file can express that, because what changes each
 * frame is the coordinate, not the mesh and not the picture.
 *
 * WHY NOT glTexGen(GL_SPHERE_MAP), which is one line and would have done. It
 * was measured, not dismissed. Algebraically GL_SPHERE_MAP reduces EXACTLY to
 * the N64 formula for an INFINITE viewer - verified at machine precision over
 * 200k random normals, front faces max deviation 2.22e-16 - but these screens
 * are not an infinite viewer. The Rareware boot screen is
 * guPerspective(60 deg, 320/240, 100, 5000) with the camera at z=880
 * (title.c:333, cameraPosition1[2]) over a logo of half-size ~207 units:
 * about 13 degrees of subtense. Measured at that real projection, sphere-map
 * deviates about 3.1 texels MEAN on a 32x32 map across a full rotation. Worse,
 * it is SINGULAR at the silhouette - as nz -> 0 the 2*sqrt(rx^2+ry^2+(rz+1)^2)
 * denominator collapses - and beveled letterforms are built out of near
 * edge-on side walls, so the blow-up lands on the rim of every glyph, which is
 * exactly where the eye goes. Generating on the CPU is exact under
 * perspective and has no denominator to collapse.
 *
 * AND IT IS AFFORDABLE, which is the other half of the trade and is measured
 * rather than assumed: this path was using 0.93 ms of a 16.7 ms frame at
 * 119,644 triangles before the change. The generation is one pass over the
 * vertex array, nine multiplies and a square root each.
 *
 * ONE ARRAY FOR THE WHOLE MODEL, filled once per draw. The format guarantees
 * every vertex belongs to exactly one primitive (the importer's own note on
 * the COLOR_0 fold), so a vertex cannot be wanted with generated coordinates
 * by one material and stored ones by another, and a single full-length array
 * bound per-primitive resolves it with no per-primitive vertex ranges to
 * track. It is grown, never shrunk, and lives for the process like the
 * texture names above - the model outlives the level. */
static float   *g_aov_gen;
static unsigned g_aov_gen_cap;
static unsigned g_aov_gen_verts, g_aov_gen_zeronrm;
static unsigned g_aov_gen_prims, g_aov_gen_declined, g_aov_gen_nonrm;

/* SL_TEXGEN_MODEL, the A/B from one binary, named to sit with the SL_TEXGEN*
 * switches that already govern the display-list generator - and it composes
 * with them rather than duplicating them: SL_TEXGEN_MODE, SL_TEXGEN_SWAP and
 * SL_TEXGEN_DEFBASIS reach this path too, because it is the same function.
 *
 *   0  OFF     - every material takes its baked UVs. This is the behaviour
 *                before generated coordinates existed, so it is the honest
 *                A-side of the comparison rather than an approximation of it.
 *   1  DEFAULT - per material, as the model and the texture registry resolved
 *                it at load (struct sl_amdl_mat::texgen).
 *   2  FORCE   - every textured material generates, whatever it declared.
 *                Diagnostic: it answers "is this material's decision the
 *                thing that is wrong" without editing and re-importing a
 *                model to find out.
 */
static int texgen_model(void)
{
    static int v = -1;
    if (v < 0) { const char *e = getenv("SL_TEXGEN_MODEL");
                 v = (e != NULL && *e != '\0') ? atoi(e) : 1; }
    return v;
}

/* Does this material take generated coordinates on THIS draw? Kept as one
 * function so the three places that must agree - whether to fill the array,
 * whether to bind a texture without baked UVs, and which pointer to bind -
 * cannot answer differently. */
static int aov_mat_generates(const struct sl_amdl *m,
                             const struct sl_amdl_mat *mt)
{
    const int mode = texgen_model();
    if (mode == 0) return 0;
    if (m->nrm == NULL) return 0;      /* nothing to generate FROM */
    if (mt->texture < 0) return 0;     /* nothing to generate INTO */
    if (mode >= 2) return 1;
    return mt->texgen != 0;
}

/* One upload per texture per process. The model outlives the level, so this
 * is not a cache with an eviction policy - it is a one-time promotion of
 * already-decoded RGBA8 into a GL name. */
static void aov_tex_upload(struct sl_amdl_tex *t)
{
    GLuint name = 0;
    unsigned char *mb0 = NULL, *mb1 = NULL;

    glGenTextures(1, &name);
    if (name == 0) return;
    glBindTexture(GL_TEXTURE_2D, name);
    /* MIPMAPPED (2026-09-20, #64). A controller model's 1024-square texture
     * lands on a pad ~300 px wide and on a button icon ~13 px wide: four to
     * eighty times minified, and under plain GL_LINEAR a minified texel is
     * one of the four nearest, so the DualSense's thin grey button symbols
     * fell between samples and vanished (measured: plain white discs on the
     * page and its icons). The complete box-filtered pyramid, the B-119
     * chain the game's own textures take (mip_box_halve), and trilinear
     * sampling; magnified textures - the logo screens' reflection maps -
     * sample level 0 exactly as before. Two scratch levels, freed here. */
    if (t->w > 1u || t->h > 1u) {
        size_t half = (size_t) ((t->w + 1u) / 2u) * (size_t) ((t->h + 1u) / 2u) * 4u;
        mb0 = (unsigned char *) malloc(half);
        mb1 = (unsigned char *) malloc(half);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    (mb0 != NULL && mb1 != NULL) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei) t->w, (GLsizei) t->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, t->rgba);
    if (mb0 != NULL && mb1 != NULL) {
        const unsigned char *cur = t->rgba;
        unsigned char *nxt = mb0, *oth = mb1, *tmp;
        unsigned cw = t->w, ch = t->h, nw, nh, level = 1;
        while (cw > 1u || ch > 1u) {
            mip_box_halve(cur, cw, ch, nxt, &nw, &nh);
            glTexImage2D(GL_TEXTURE_2D, (GLint) level, GL_RGBA,
                         (GLsizei) nw, (GLsizei) nh, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nxt);
            cur = nxt; tmp = nxt; nxt = oth; oth = tmp;
            cw = nw; ch = nh; level++;
        }
    }
    free(mb0);
    free(mb1);
    t->gl = (unsigned) name;
    /* The upload bound a name behind the DL texture cache's back. */
    g_tex_gl_bound = 0;
}

/* sel: 0 draws the whole model; p draws only the part with canonical id
 * p - 1, about its own pivot (the bridge command's <part> field,
 * src/sl_asset_override.h) - the watch page's standalone button icons. */
static void draw_asset_override(unsigned id, unsigned fade, unsigned sel)
{
    const struct sl_amdl *m = sl_asset_override_get_model((int) id);
    float k = (float) fade * (1.0f / 255.0f);
    int lit, use_col, use_uv, gen_any, gen_ok;
    unsigned i;
    unsigned sel_part = (unsigned) -1;      /* the part INDEX sel names, if any */

    if (m == NULL) return;
    if (sel != 0u) {
        for (i = 0; i < m->npart; i++)
            if (m->part[i].id == sel - 1u) { sel_part = i; break; }
        if (sel_part == (unsigned) -1) return;   /* this model has no such part */
    }

    lit     = (m->nrm != NULL);
    use_col = (m->col != NULL);
    use_uv  = (m->uv  != NULL);

    /* ---- generated coordinates, decided before any GL state moves -------
     *
     * gen_any is "some material wants them". gen_ok is "and they can be
     * produced", which is a separate question with a separate answer, because
     * the reflectance basis is RESIDENT STATE that a draw does not own. With
     * SL_TEXGEN_DEFBASIS armed (the default) the seeded identity means this is
     * always true on the two logo screens - neither issues a gSPLookAt of its
     * own, which is the whole subject of the B-085 note above - but disarming
     * the seed must decline here for the same reason the display-list path
     * declines: a degenerate basis dots to zero for every normal and would
     * collapse the entire surface onto one texel. Falling back to the baked
     * UVs is strictly better than that, and the counter says it happened. */
    gen_any = 0;
    for (i = 0; i < m->nmat; i++)
        if (aov_mat_generates(m, &m->mat[i])) { gen_any = 1; break; }
    gen_ok = gen_any && (lookat_seen_eff() == 3u);
    if (gen_any && !gen_ok) g_aov_gen_declined++;

    /* A material that asked for generation on a mesh with no NORMAL is the
     * one case worth naming out loud: it is an authoring mistake the file
     * cannot detect (the flag is per material, the normals are per mesh), and
     * silently drawing baked UVs would look like the feature not working. */
    if (!gen_any && m->nrm == NULL && texgen_model() != 0) {
        for (i = 0; i < m->nmat; i++) {
            if (!m->mat[i].texgen || m->mat[i].texture < 0) continue;
            if (g_aov_gen_nonrm++ == 0u)
                fprintf(stderr, "sl_gfx: asset override %u wants generated"
                                " texture coordinates but its mesh has no"
                                " NORMAL - drawing its stored UVs\n", id);
            break;
        }
    }

    if (gen_ok) {
        if (g_aov_gen_cap < m->nvert) {
            float *nb = (float *) realloc(g_aov_gen,
                                          (size_t) m->nvert * 2u * sizeof *nb);
            if (nb == NULL) { gen_ok = 0; }
            else { g_aov_gen = nb; g_aov_gen_cap = m->nvert; }
        }
    }
    if (gen_ok) {
        /* THE SAME GENERATOR THE RSP PATH USES, on the SAME matrix the draw
         * is about to load. Coordinates are produced HERE, per frame, from
         * the normal under the matrix in force - which is the entire point,
         * and the reason a per-frame pass exists at all rather than a cached
         * one: the matrix changes every frame, so a cache would be a way of
         * reintroducing the defect.
         *
         * The pre-scale domain is divided out rather than scaled by a
         * gSPTexture word, because there is no gSPTexture here to read: an
         * override material's texture is its own, bound directly, and a full
         * -1..+1 sweep is meant to cross the whole of it. That is exactly
         * what the display-list path's scale is TUNED to achieve on the
         * original tiles (32768 * 3456 / 65536 / 32 = 54.0 on a 54x54 map),
         * so this is the same intent expressed where the dimensions are
         * already normalised. */
        const float *mv = g_mv[g_mv_sp];
        for (i = 0; i < m->nvert; i++) {
            float n[3], pre[2];
            n[0] = m->nrm[i * 3u + 0u];
            n[1] = m->nrm[i * 3u + 1u];
            n[2] = m->nrm[i * 3u + 2u];
            if (texgen_generate(n, mv, pre, NULL, NULL, NULL))
                g_aov_gen_zeronrm++;
            g_aov_gen[i * 2u + 0u] = pre[0] * (1.0f / SL_TEXGEN_DOMAIN);
            g_aov_gen[i * 2u + 1u] = pre[1] * (1.0f / SL_TEXGEN_DOMAIN);
        }
        g_aov_gen_verts += m->nvert;
    }

    if (sel == 0u) g_aov_draws++; else g_aov_part_draws++;

    /* SL_PAD_DBG (#64): what a part-only draw COSTS, measured with glFinish
     * on both sides so the GPU's share is in the number (which perturbs the
     * frame - a diagnostic, off by default): the sum over the part draws
     * since the last whole draw, printed with the whole draw's line. */
    if (g_aov_dbg_time && sel != 0u) {
        LARGE_INTEGER t0;
        glFinish();
        QueryPerformanceCounter(&t0);
        g_aov_part_t0 = t0.QuadPart;
    }

    /* SL_PAD_DBG (#63 defect round 2): what this draw is about to do, once
     * per second per model - the lighting term MEASURED rather than
     * reasoned. The mean of N.L over the model's normals under the modelview
     * in force, with L the rig's own (0,0,1) in eye space, is the diffuse
     * factor the fixed pipeline will apply; the modelview's determinant
     * sign says whether the page's frame is mirrored (which is what flips
     * every normal's winding side under two-sided lighting). Off by
     * default; no behaviour attached. */
    {
        static int dbg = -1;
        static unsigned last_at[SL_ASSET_ID_COUNT];
        if (dbg < 0) { dbg = getenv("SL_PAD_DBG") != NULL; g_aov_dbg_time = dbg; }
        if (dbg && sel == 0u && (last_at[id] == 0u || g_aov_draws - last_at[id] >= 60u)) {
            /* the part draws since the last report: count and glFinish-bounded time */
            LARGE_INTEGER fq;
            unsigned pn = g_aov_part_n - g_aov_part_n_last;
            LONGLONG pt = g_aov_part_ticks - g_aov_part_ticks_last;
            QueryPerformanceFrequency(&fq);
            fprintf(stderr, "sl_pad_dbg: part-draws since last report: %u in %.2f ms total (%.3f ms each; glFinish-bounded, CPU+GPU)\n",
                    pn, fq.QuadPart ? 1000.0 * (double) pt / (double) fq.QuadPart : 0.0,
                    (pn && fq.QuadPart) ? 1000.0 * (double) pt / (double) fq.QuadPart / (double) pn : 0.0);
            g_aov_part_n_last = g_aov_part_n;
            g_aov_part_ticks_last = g_aov_part_ticks;
            const float *mv = g_mv[g_mv_sp];
            double ndl = 0.0, npos = 0.0;
            unsigned n = 0;
            float det = mv[0] * (mv[5] * mv[10] - mv[9] * mv[6])
                      - mv[4] * (mv[1] * mv[10] - mv[9] * mv[2])
                      + mv[8] * (mv[1] * mv[6]  - mv[5] * mv[2]);
            if (m->nrm != NULL) {
                unsigned step = m->nvert > 4096u ? m->nvert / 4096u : 1u;
                for (i = 0; i < m->nvert; i += step) {
                    const float *nn = &m->nrm[i * 3u];
                    /* the normal in eye space: the upper 3x3, column-major */
                    float ex = mv[0] * nn[0] + mv[4] * nn[1] + mv[8]  * nn[2];
                    float ey = mv[1] * nn[0] + mv[5] * nn[1] + mv[9]  * nn[2];
                    float ez = mv[2] * nn[0] + mv[6] * nn[1] + mv[10] * nn[2];
                    float len = (float) sqrt((double) ex * ex + (double) ey * ey + (double) ez * ez);
                    if (len > 0.0f) { ez /= len; ndl += ez; if (ez > 0.0f) npos += 1.0; n++; }
                }
            }
            last_at[id] = g_aov_draws;
            fprintf(stderr, "sl_pad_dbg: draw id=%u fade=%u k=%.3f exposure=%.1f lit=%d col=%d uv=%d"
                            " prims=%u tris=%u parts=%u mv-det=%s mean(N.L)=%.3f facing=%.0f%% of %u"
                            " mv=[%.3f %.3f %.3f | %.3f %.3f %.3f | %.3f %.3f %.3f]\n",
                    id, fade, k, m->exposure, lit, use_col, use_uv, m->nprim, m->nidx / 3u, m->npart,
                    det < 0.0f ? "NEG" : "pos", n ? ndl / n : 0.0, n ? 100.0 * npos / n : 0.0, n,
                    mv[0], mv[4], mv[8], mv[1], mv[5], mv[9], mv[2], mv[6], mv[10]);
            /* WHERE THE MODEL LANDS (#63 defect round 3): the eye-space depth
             * of the model's origin, the projection's near and far planes,
             * how many sampled vertices sit in front of the near plane, and
             * the projected bounding box in window pixels through the
             * viewport in force. A model that is drawn in full but clipped
             * away by the near plane - the Dam page, whose modelview arrived
             * at a fifth of its size - reads "near-culled=100%" and an empty
             * box, which is the one number that separates "did not draw"
             * from "drew, and nothing survived". */
            {
                const float *pj = g_proj;
                double A = (double) pj[10], B = (double) pj[14];
                double znear = 0.0, zfar = 0.0;
                unsigned step = m->nvert > 4096u ? m->nvert / 4096u : 1u;
                unsigned tot = 0, culled = 0, box = 0;
                float bx0 = 0.0f, by0 = 0.0f, bx1 = 0.0f, by1 = 0.0f;
                if (pj[11] != 0.0f && A != 1.0 && A != -1.0) {
                    znear = B / (A - 1.0);   /* GL: A = -(f+n)/(f-n), B = -2fn/(f-n) */
                    zfar  = B / (A + 1.0);
                }
                for (i = 0; i < m->nvert; i += step) {
                    const float *pp = &m->pos[i * 3u];
                    float ex = mv[0] * pp[0] + mv[4] * pp[1] + mv[8]  * pp[2] + mv[12];
                    float ey = mv[1] * pp[0] + mv[5] * pp[1] + mv[9]  * pp[2] + mv[13];
                    float ez = mv[2] * pp[0] + mv[6] * pp[1] + mv[10] * pp[2] + mv[14];
                    float cx = ex * pj[0] + ey * pj[4] + ez * pj[8]  + pj[12];
                    float cy = ex * pj[1] + ey * pj[5] + ez * pj[9]  + pj[13];
                    float cz = ex * pj[2] + ey * pj[6] + ez * pj[10] + pj[14];
                    float cw = ex * pj[3] + ey * pj[7] + ez * pj[11] + pj[15];
                    tot++;
                    if (cz < -cw) { culled++; continue; }   /* in front of the near plane */
                    if (cw > 0.0f && g_vp_rect[2] > 0 && g_vp_rect[3] > 0) {
                        float sx = (float) g_vp_rect[0] + (cx / cw * 0.5f + 0.5f) * (float) g_vp_rect[2];
                        float sy = (float) g_vp_rect[1] + (cy / cw * 0.5f + 0.5f) * (float) g_vp_rect[3];
                        if (!box) { bx0 = bx1 = sx; by0 = by1 = sy; box = 1; }
                        else {
                            if (sx < bx0) bx0 = sx;
                            if (sx > bx1) bx1 = sx;
                            if (sy < by0) by0 = sy;
                            if (sy > by1) by1 = sy;
                        }
                    }
                }
                fprintf(stderr, "sl_pad_dbg: place id=%u origin-depth=%.1f near=%.1f far=%.1f"
                                " near-culled=%.0f%% of %u viewport=%d,%d %dx%d box=%s%.0f,%.0f-%.0f,%.0f (gl px, bottom-left origin)\n",
                        id, (double) -mv[14], znear, zfar, tot ? 100.0 * culled / tot : 0.0, tot,
                        g_vp_rect[0], g_vp_rect[1], g_vp_rect[2], g_vp_rect[3],
                        box ? "" : "EMPTY ", bx0, by0, bx1, by1);
            }
        }
    }

    /* ---- enter -------------------------------------------------------- */
    batch_end();          /* glBegin is open across most of a list          */
    mode2d_end();         /* no-op unless a 2D command preceded us          */
    fog_neutral();        /* array vertices take the current coordinate     */
    tex1_off();
    texenv_set(0);        /* GL_MODULATE, fixed pipeline                    */
    vp_use(1);            /* the game's 3D viewport, not the whole window   */

    /* EXPOSURE (#63): a modulate whose result is scaled 2x or 4x
     * (GL_COMBINE with GL_RGB_SCALE), for a model whose texture is near
     * black - the watch's controllers. The texenv cache is told the mode is
     * foreign (g_texenv = -1 on leave) so the next draw re-establishes its
     * own. Only when the exposure is above 1: the logos take the plain
     * modulate they always took. */
    if (m->exposure > 1.0f) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, m->exposure >= 4.0f ? 4.0f : 2.0f);
    }

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(g_proj);

    /* A directional light is positioned in EYE space, which means setting it
     * while the modelview is identity. Do that first, then load the model
     * matrix - GL transforms normals by it and the light stays put relative
     * to the camera, so the logo lights the same way through its spin. */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (lit) {
        static const GLfloat lpos[4]  = { 0.0f, 0.0f, 1.0f, 0.0f };
        static const GLfloat black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        GLfloat amb[4], dif[4];

        amb[0] = amb[1] = amb[2] = 0.30f * k; amb[3] = 1.0f;
        dif[0] = dif[1] = dif[2] = 0.75f * k; dif[3] = 1.0f;
        glLightfv(GL_LIGHT0, GL_POSITION, lpos);
        glLightfv(GL_LIGHT0, GL_AMBIENT,  black);
        glLightfv(GL_LIGHT0, GL_DIFFUSE,  dif);
        glLightfv(GL_LIGHT0, GL_SPECULAR, black);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_COLOR_MATERIAL);
        glEnable(GL_LIGHT0);
        glEnable(GL_LIGHTING);
        /* The Nintendo screen scales its logo in from 0.018 to 1.1, so the
         * modelview is NOT orthonormal and unnormalised normals would make
         * the model darken and brighten as it grows. */
        glEnable(GL_NORMALIZE);
    }
    glLoadMatrixf(g_mv[g_mv_sp]);

    /* Both boot screens run with the Z buffer off (viSetUseZBuf(0)), so the
     * depth buffer holds whatever the previous screen left. An imported mesh
     * is an arbitrary solid and does need depth to resolve against itself, so
     * this draw clears depth for itself and puts the mask back. Sound here and
     * only here: the bridge command exists on the two logo screens alone, both
     * of which clear their colour buffer every frame anyway and have no other
     * depth user. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, (const GLvoid *) m->pos);
    if (lit) {
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, 0, (const GLvoid *) m->nrm);
    }
    if (use_uv || gen_ok) {
        /* The pointer is re-bound per primitive below when the two kinds are
         * mixed; this establishes the array and a sane default for a model
         * that only has one kind. */
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0,
                          (const GLvoid *) (use_uv ? m->uv : g_aov_gen));
    }
    if (use_col) {
        /* The importer folded baseColorFactor into COLOR_0, so the vertex
         * colour alone is the material colour wherever COLOR_0 exists. */
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, (const GLvoid *) m->col);
    }

    /* ---- primitives ---------------------------------------------------- */
    for (i = 0; i < m->nprim; i++) {
        const struct sl_amdl_prim *p = &m->prim[i];
        const struct sl_amdl_mat  *mt = &m->mat[p->material];
        int blend = (mt->flags & SL_AMDL_M_ALPHA_BLEND) != 0;
        int mask  = (mt->flags & SL_AMDL_M_ALPHA_MASK)  != 0;
        const struct sl_amdl_pose *pose = NULL;

        /* A part-only draw (sel): every other primitive is skipped. */
        if (sel != 0u && p->part != sel_part) continue;

        /* PARTS (#63). A primitive inside a part is stored about the part's
         * pivot, so its modelview is the model's, translated to the pivot,
         * then the part's pose: its own translation (a button's press) and
         * a rotation about its pivot (a stick's tilt, a trigger's pull).
         * Fixed-function GL, post-multiplied: MV * T(pivot) * T(move) * R.
         * A primitive outside every part - or a model with no part table -
         * draws under the plain modelview exactly as before. A part drawn
         * ALONE (sel) leaves the pivot out: the caller's modelview already
         * says where the pivot goes, and the pose applies about it. */
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(g_mv[g_mv_sp]);
        if (p->part != SL_PART_NONE && p->part < m->npart) {
            const struct sl_amdl_part *pt = &m->part[p->part];
            pose = &m->pose[p->part];
            if (sel != 0u)
                glTranslatef(pose->move[0], pose->move[1], pose->move[2]);
            else
                glTranslatef(pt->pivot[0] + pose->move[0],
                             pt->pivot[1] + pose->move[1],
                             pt->pivot[2] + pose->move[2]);
            if (pose->rot[0] != 0.0f)
                glRotatef(pose->rot[0] * (180.0f / 3.14159265f), 1.0f, 0.0f, 0.0f);
            if (pose->rot[1] != 0.0f)
                glRotatef(pose->rot[1] * (180.0f / 3.14159265f), 0.0f, 1.0f, 0.0f);
            if (pose->rot[2] != 0.0f)
                glRotatef(pose->rot[2] * (180.0f / 3.14159265f), 0.0f, 0.0f, 1.0f);
        }
        /* KHR_materials_unlit, per material. The light rig is built once
         * above when the mesh has normals; a material that declares
         * itself unlit switches it off for its own draw and takes the
         * fade in its colour instead, the same way a normal-less model
         * does. */
        int plit  = lit && (mt->flags & SL_AMDL_M_UNLIT) == 0;
        int pgen  = gen_ok && aov_mat_generates(m, mt);
        int ptex  = pgen || use_uv;

        if (lit) { if (plit) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING); }

        cull_set((mt->flags & SL_AMDL_M_DOUBLESIDED) ? 0 : (int) GL_BACK);

        /* Which coordinate array this primitive reads. A model can mix them -
         * a reflective badge beside a hand-painted panel is the ordinary case
         * - so the choice is per primitive, and it is safe to make it here
         * because every vertex belongs to exactly one primitive. */
        if (pgen) {
            glTexCoordPointer(2, GL_FLOAT, 0, (const GLvoid *) g_aov_gen);
            g_aov_gen_prims++;
        } else if (use_uv) {
            glTexCoordPointer(2, GL_FLOAT, 0, (const GLvoid *) m->uv);
        }

        if (mt->texture >= 0 && ptex) {
            struct sl_amdl_tex *t = &m->tex[mt->texture];
            if (t->gl == 0u) aov_tex_upload(t);
            if (t->gl != 0u) {
                if (!g_tex_gl_on) { glEnable(GL_TEXTURE_2D); g_tex_gl_on = 1; }
                glBindTexture(GL_TEXTURE_2D, (GLuint) t->gl);
                g_tex_gl_bound = (GLuint) t->gl;
            } else if (g_tex_gl_on) {
                glDisable(GL_TEXTURE_2D); g_tex_gl_on = 0;
            }
        } else if (g_tex_gl_on) {
            glDisable(GL_TEXTURE_2D); g_tex_gl_on = 0;
        }

        if (mask) { glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GEQUAL, mt->alpha_cutoff); }
        else        glDisable(GL_ALPHA_TEST);

        if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }

        if (!use_col) {
            /* Unlit models take the fade in the colour; lit ones already took
             * it in the light, so applying it twice would square it. A part's
             * tint (a held button's highlight) multiplies in here - the one
             * material override there is, no second pipeline. A model with
             * COLOR_0 carries its colour per vertex and takes no tint. */
            float s = plit ? 1.0f : k;
            float tr = 1.0f, tg = 1.0f, tb = 1.0f;
            if (pose != NULL) { tr = pose->tint[0]; tg = pose->tint[1]; tb = pose->tint[2]; }
            glColor4f(mt->base[0] * s * tr, mt->base[1] * s * tg,
                      mt->base[2] * s * tb, mt->base[3]);
        }

        glDrawElements(GL_TRIANGLES, (GLsizei) p->count, GL_UNSIGNED_INT,
                       (const GLvoid *) (m->idx + p->first));
        g_aov_tris_drawn += p->count / 3u;
    }

    /* ---- leave: everything this draw owns, put back ---------------------
     *
     * The target state is the one the 3D walk expects between commands:
     * identity modelview, g_proj loaded, no client arrays, no lighting, no
     * alpha test, no blend, depth writes on, texturing off with the cache
     * told so, and the render-mode cache invalidated so the next triangle
     * re-establishes blend and depth from its own render mode word. */
    glDisableClientState(GL_VERTEX_ARRAY);
    if (lit)     glDisableClientState(GL_NORMAL_ARRAY);
    if (use_uv || gen_ok) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (use_col) glDisableClientState(GL_COLOR_ARRAY);

    if (lit) {
        glDisable(GL_LIGHTING);
        glDisable(GL_LIGHT0);
        glDisable(GL_COLOR_MATERIAL);
        glDisable(GL_NORMALIZE);
        glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 0);
    }
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    if (g_tex_gl_on) { glDisable(GL_TEXTURE_2D); g_tex_gl_on = 0; }
    g_tex_gl_bound = 0;
    cull_set(0);
    rm_invalidate();
    if (m->exposure > 1.0f) {
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        g_texenv = -1;                    /* the cache re-establishes its mode */
        texenv_set(0);
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (g_aov_dbg_time && sel != 0u) {
        LARGE_INTEGER t1;
        glFinish();
        QueryPerformanceCounter(&t1);
        g_aov_part_ticks += t1.QuadPart - g_aov_part_t0;
        g_aov_part_n++;
    }
}

/* ---- LIST NESTING PROBE (SL_DEPTH_DBG), off by default -------------------
 *
 * walk() refuses to descend past depth 8, so "how deep does a frame actually
 * nest" decides whether that cap is inert or is silently dropping the tail of
 * a list. Nothing else reports it: the F8 record prints the depth of a
 * triangle it kept, never the depth the interpreter reached.
 *
 * It also separates the two uses of opcode 06. "Display Lists and Object
 * Generation/ucode05.txt" gives the encoding as `xx00xxxx push display list`
 * / `xx01xxxx branch to display list`, and B8 rsp_enddl as "if the current
 * list was called by an 06 command, returns from branch" - a BRANCH replaces
 * the running list and does not consume a stack level.
 *
 * MEASURED 2026-09-08, Dam gameplay, 400 consecutive frames: call=77,
 * branch=0, maxdepth=1, refused=0 every frame - which is why walk() recursing
 * for both forms went unnoticed for so long. A scene has to CHAIN before the
 * conflation costs anything, and a probe that samples one that does not
 * reports a clean number for a broken interpreter.
 *
 * The chaining scene is not rare and does not need a person to find it.
 * MEASURED 2026-09-09, `SL_BOOT_LEVEL=control` for 500 frames, no input at
 * all, pre-fix binary, its first frame:
 *
 *   sl_depth: frame 1 call=90 branch=8 maxdepth=9 refused=12
 *
 * Eight tail transfers, and because walk() recursed for each one they alone
 * carried it to depth 9 - past the `depth > 8` cap - so twelve submissions
 * were entered, recorded, and returned from without a command being read.
 * See the TAIL TRANSFER note in OP_DL. The branch form now iterates, so
 * `depth` counts stack levels and nothing else, and `refused` means what it
 * says. */
static unsigned sl_probe_call, sl_probe_branch, sl_probe_refused;
static unsigned sl_probe_maxdepth;

static void walk(const unsigned int *dl, int depth, int swapped)
{
    unsigned guard = 0;
    /* Whether [first, end) still describes the list being read. It does until
     * a tail transfer replaces the running list: the target of a branch is a
     * different list, and is no more bounded by the frame list's end than a
     * called sub-list is. See the branch arm of OP_DL. */
    int bounded = 1;
    /* B-066 diagnosis probe. Env-gated, off by default, no behaviour change. */
    extern int sl_dl_branch_dbg;
    if (depth > 8) sl_probe_refused++;
    if ((unsigned) depth > sl_probe_maxdepth) sl_probe_maxdepth = (unsigned) depth;
    if (dl == NULL || depth > 8) return;
    if (!ptr_ok((unsigned long) dl)) return;
    /* LIVE OWNER BUG MARK: the base this depth's offsets are measured from.
     * One store per walk, no behaviour attached - see dl_off_at. */
    if (depth < 12) { g_dl_first[depth] = dl; g_dl_cur[depth] = dl; }
    for (;;) {
        unsigned int w0, w1, op;
        if (++guard > 100000) {
            g_guard_stops++;
            if (sl_dl_branch_dbg)
                fprintf(stderr, "sl_dlb: GUARD STOP depth=%d at %p (started walking zeros)\n",
                        depth, (const void *) dl);
            return;
        }
        /* The frame list has a known end; honour it. walk() used to run until
         * it saw a B8, so one mis-parsed enddl let it read past the real list
         * into whatever followed. Only depth 0 is bounded - G_DL targets are
         * resolved through segments and legitimately live elsewhere. */
        if (bounded && depth == 0 && g_dl_end != NULL && dl + 2 > g_dl_end) {
            g_end_stops++;
            return;
        }
        if (!mem_readable((unsigned long) dl, 8)) return;
        if (depth < 12) g_dl_cur[depth] = dl;   /* one store; see dl_off_at */
        w0 = RDW(dl[0], swapped); w1 = RDW(dl[1], swapped);
        op = (w0 >> 24) & 0xff;
        g_cmds++;
        if (depth < 10) g_depth_cmds[depth]++;
        if (w0 == 0 && w1 == 0) g_zero_runs++;
        switch (op) {
        /* NATIVE ASSET OVERRIDE bridge. ucode05 opcode 0x02 is
         * "rsp_reserved0" and appears in no GoldenEye list; the 40-bit magic
         * on top of it means a stray 0x02 word cannot be mistaken for one.
         * See src/sl_asset_override.h. Anything that does not match falls
         * through to the unknown-opcode counter exactly as before. */
        case SL_AOV_DL_OP:
            if ((w0 & SL_AOV_DL_W0_MASK) == SL_AOV_DL_W0_BASE
                && (w1 & SL_AOV_DL_W1_MASK) == SL_AOV_DL_W1_BASE
                && (w0 & 0xffu) < (unsigned) SL_ASSET_ID_COUNT) {
                draw_asset_override(w0 & 0xffu, w1 & 0xffu,
                                    (w1 & SL_AOV_DL_W1_PART_MASK) >> SL_AOV_DL_W1_PART_SHIFT);
            } else {
                g_unknown++;
                g_op_hist[op]++;
            }
            break;
        case OP_ENDDL:
            return;
        case OP_MOVEWORD:
            if ((w0 & 0xff) == MW_SEGMENT)
                g_seg[((w0 >> 8) & 0xffff) / 4 & 0x0f] = w1;
            else if ((w0 & 0xff) == MW_FOG) {
                g_fogp_cmds++;
                g_fogp_fm = (short) ((w1 >> 16) & 0xffff);
                g_fogp_fo = (short) ( w1        & 0xffff);
            }
            /* B-051 STAGE 4. gSPNumLights (gbi.h:2533) is a moveword on
             * G_MW_NUMLIGHT = 0x02 (:1297) at G_MWO_NUMLIGHT = 0 (:1312),
             * carrying NUML(n). On this plain-F3D branch NUML(n) is
             * (n)*32+0x80000000 (:2517) - the microcode wants a byte offset
             * into its light memory, not a count - so the count has to be
             * recovered from it rather than read off. CHECKED against the
             * macro's own arithmetic: gSPNumLights(pkt, NUMLIGHTS_1) sends
             * NUML(1) = 2*32 + 0x80000000 = 0x80000040, and 0x40/32 - 1 = 1.
             * Range-checked; a word that does not decode to 1..7 is REPORTED
             * and ignored rather than silently believed. */
            else if ((w0 & 0xff) == MW_NUMLIGHT) {
                unsigned n = ((w1 & 0x7fffffffu) / 32u);
                n = (n >= 1u) ? n - 1u : 0u;
                g_numlight_cmds++;
                if (n >= 1u && n <= 7u) g_numlights = n;
                else if (getenv("SL_LIGHT_DBG") != NULL)
                    fprintf(stderr, "sl_light: NUMLIGHT word %08x decodes to"
                                    " %u - out of range, ignored\n", w1, n);
            }
            break;
        case OP_DL: {
            const void *t = dl_operand(w1, 8);
            int is_branch = (((w0 >> 16) & 0xff) == 1);
            if (sl_dl_branch_dbg) {
                unsigned sidx = (w1 >> 24) & 0x0f;
                const unsigned int *tw = (const unsigned int *) t;
                fprintf(stderr,
                    "sl_dlb: DL d=%d w1=%08x seg=%u g_seg[%u]=%08x -> %p%s",
                    depth, w1, sidx, sidx, g_seg[sidx], t, t ? "" : " (NULL)");
                if (t && mem_readable((unsigned long) t, 16))
                    fprintf(stderr, "  first4=%08x %08x %08x %08x",
                            tw[0], tw[1], tw[2], tw[3]);
                fprintf(stderr, "\n");
            }
            /* Each list carries its own word order: a native list can name a
             * ROM-order sub-list (segment 1 does exactly that) and vice versa,
             * so the order is decided per target, not inherited. */

            /* ---- TAIL TRANSFER. A BRANCH IS NOT A CALL, AND walk()
             * RECURSED FOR BOTH -----------------------------------------
             *
             * Not "B-066". docs/backlog.md already spends that number on the
             * windowed-Facility frame rate, which is what sl_dl_branch_dbg
             * (:443) was written for; naming this one after it would send the
             * next reader to the wrong entry.
             *
             * "Display Lists and Object Generation/ucode05.txt" l.240-246
             * gives opcode 06 two forms - `xx00xxxx push display list` and
             * `xx01xxxx branch to display list` - and l.437-440 (B8
             * rsp_enddl) settles what separates them: "If the current list
             * was called by an 06 command, returns from branch." A CALL
             * consumes a stack level and RESUMES afterwards; a BRANCH
             * REPLACES the running list and consumes no stack level at all.
             *
             * walk() recursed for both, so every tail transfer cost a C
             * recursion level the RSP never spends - and the `depth > 8` cap
             * at the top of this function is measured in exactly those
             * levels. A frame whose list is chained through more than eight
             * tail transfers therefore had everything past the eighth
             * silently dropped: walk() was still ENTERED for each remaining
             * list, so the submission was recorded and the frame's
             * provenance still reconciled, but it returned before reading a
             * single command and emitted nothing.
             *
             * MEASURED 2026-09-09, the two binaries built from this file
             * with and without this arm, nothing else differing.
             *
             * (a) THE DEFECT FIRES, and no person is needed to find it.
             * `SL_BOOT_LEVEL=control`, 500 frames, no input:
             *
             *     pre-fix   frame 1  branch=8 maxdepth=9 refused=12
             *     post-fix  frame 1  branch=8 maxdepth=1 refused=0
             *
             * Both builds see the SAME eight tail transfers, so this is the
             * depth MODEL changing and not the list.
             *
             * (b) WHAT THE FRAME DREW, same frame, the census line the two
             * builds print independently of the probe:
             *
             *     pre-fix   cmds=5824 vtx=3119 tris=1713
             *     post-fix  cmds=6173 vtx=3223 tris=1765
             *
             * Twelve refused submissions are 52 triangles and 104 vertices
             * that were being discarded.
             *
             * (c) THE CONTROL. Every frame of that run with branch=0, and
             * all 77 census lines of a 4599-frame Dam replay (98 of whose
             * frames branch six times, one short of the cap), are
             * BYTE-IDENTICAL between the two builds. Below the cap the
             * iterative form draws exactly what the recursive one drew; the
             * only frames that change are the ones that were losing work.
             *
             * (d) WHY IT LOOKS LIKE THE GUN. Owner run
             * 20260909-000116-lvl33 mark-002, Dam, post-fix: the frame list
             * chains EIGHT tail transfers, and 90 of its 116 submissions -
             * 2173 of the frame's 3173 triangles - hang off the eighth. The
             * last nineteen of them are the consecutive segment-5 lists
             * gunRenderFirstPersonGunModels emits, 572 triangles, the whole
             * viewmodel. Under the recursive form every one of those sat at
             * walk depth 9 and was refused. It is view-direction dependent
             * rather than position dependent because how many times the
             * frame's list gets chained depends on how much the view puts
             * into it, not on where the player is standing.
             *
             * The branch form is now what the microcode says it is: the
             * running list is replaced in place, at the SAME depth, costing
             * neither a stack level nor a C frame. The 100000-command guard
             * above still catches a list that branches to itself, and now
             * bounds a chain of any length instead of the C stack doing it. */
            if (is_branch) {
                sl_probe_branch++;
                /* Nothing to replace the running list with: it still ends
                 * here, exactly as it did when the recursive form fell
                 * through to the `return` below. */
                if (t == NULL || !ptr_ok((unsigned long) t)) return;
                if (depth < 12) g_dl_addr[depth] = w1;
                g_dl_depth = depth;
                if (g_mark_on)
                    g_mark_sub_cur = mark_sub_open(w1, t, 2, depth,
                                                   dl_off_at(depth));
                /* B-113. The running list is replaced; if the replacement
                 * is a room list (or stops being one), the flag follows it. */
                if (depth == 0) {
                    extern int sl_mark_room_of_dl(unsigned addr, int *kindout);
                    int rkind = 0;
                    g_room_dl = (sl_mark_room_of_dl(w1, &rkind) >= 0);
                    rd_scope_reset();          /* B-125: a new submission */
                }
                dl      = (const unsigned int *) t;
                swapped = list_swapped(t);
                bounded = 0;
                if (depth < 12) { g_dl_first[depth] = dl; g_dl_cur[depth] = dl; }
                continue;                  /* replaces; does not resume */
            }

            /* LIVE OWNER BUG MARK. Which list a triangle came from, recorded
             * ONLY at a branch so no other command pays for it. g_dl_addr[1]
             * is the operand the FRAME list branched with, which for room
             * geometry is OS_K0_TO_PHYSICAL(ptr_expanded_mapping_info) - the
             * one value that lets a room be named without this file knowing
             * what a room is. Restored on the way out so a sibling branch
             * cannot inherit its predecessor's identity. */
            if (t) {
                int pd = g_dl_depth;
                int psub = g_mark_sub_cur;
                int proom = g_room_dl;
                sl_probe_call++;
                /* B-113. Entering a depth-1 submission: is it a room's
                 * geometry? The same lookup the mark provenance trusts. */
                if (depth == 0) {
                    extern int sl_mark_room_of_dl(unsigned addr, int *kindout);
                    int rkind = 0;
                    g_room_dl = (sl_mark_room_of_dl(w1, &rkind) >= 0);
                    rd_scope_reset();          /* B-125: a new submission */
                }
                if (depth + 1 < 12) g_dl_addr[depth + 1] = w1;
                g_dl_depth = depth + 1;
                /* SUBMITTED, as distinct from DRAWN. A G_DL is the interpreter
                 * being HANDED a list; whether anything comes out of it is the
                 * next question, and the two were previously indistinguishable
                 * because only lists that reached the centre box were ever
                 * recorded.
                 *
                 * EVERY depth, not just depth 0. The version that recorded only
                 * depth 0 stopped describing the frame at the first submission
                 * that carried the remainder inside it, which in Dam is the
                 * trailing heap list holding the props, the characters and the
                 * viewmodel - see the table's own comment. Marked frames only:
                 * g_mark_on is zero except during the one list that follows an
                 * F8 press, so the ordinary cost here is the test itself. */
                if (g_mark_on)
                    g_mark_sub_cur = mark_sub_open(w1, t, 1, depth + 1,
                                                   dl_off_at(depth));
                walk(t, depth + 1, list_swapped(t));
                /* Control resumes in THIS list after a call, so this list's
                 * node becomes current again. The branch form never reaches
                 * here - it replaces the list above and does not return. */
                g_room_dl = proom;
                g_mark_sub_cur = psub;
                g_dl_depth = pd;
            }
            break;
        }
        case OP_VTX: {
            /* ucode05.txt's "--RARE vtx --" table, which is deliberately
             * printed next to the F3DEX one it does NOT match:
             *     upper  00F00000  number of points
             *            000FFFFF  number of bytes to grab
             *     lower  0f000000  segment / 00ffffff offset in point table
             * "Absolute Basic Rendering Models.txt" supplies the encoding of
             * the count with worked examples: 04F00100 is 16 points / 0x100
             * bytes and 04700080 is 8 points / 0x80, i.e. nibble+1.
             *
             * ---- B-021: bits 16-19 are v0, NOT part of the byte count ------
             *
             * The note's "000FFFFF number of bytes to grab" reads bits 16-19 as
             * the top nibble of a 20-bit length. That is true only when the
             * destination slot is 0, which is why it survived so long. This
             * build compiles gbi.h's plain-F3D branch (include/PR/gbi.h:1865 -
             * neither F3DEX_GBI nor F3DEX_GBI_2 is defined; build.sh DEFS, and
             * `grep -rn "F3DEX_GBI" src/ include/` finds them only inside
             * gbi.h/gs2dex.h/sptask.h's own #ifdefs):
             *
             *     gSPVertex(pkt,v,n,v0) =
             *         gDma1p(pkt, G_VTX, v, sizeof(Vtx)*(n), ((n)-1)<<4|(v0))
             *
             * and gDma1p puts that parameter byte at bits 16-23. So bits 20-23
             * are n-1 and bits 16-19 are v0, the destination slot.
             *
             * Measured, because the note and the header disagree and a
             * measurement outranks both. On a streets frame with guards in
             * view: 513 of 513 vertex commands had (w0 & 0xffff) == n*16, but
             * only 476 had the 20-bit field equal - and exactly 37 carried a
             * non-zero bits 16-19. Those 37 were precisely the 37 the old
             * length check rejected, so 37 vertex loads per frame were being
             * dropped outright. Facility and dam measure 0 and 0, which is why
             * the earlier "3360 of 3360" check looked clean on them.
             *
             * The 37 come from one call site. `grep -rn "gSPVertex" src/
             * --include=*.c` finds exactly one that passes a non-zero v0 in a
             * gameplay path: model.c:4686, inside `dotube` - the tube renderer
             * that draws character limbs. It loads the joint's proximal ring
             * with v0 = 0 and the rest of the tube with v0 = 2. Dropping the
             * second load left those slots holding a previous batch's
             * vertices, which is B-021's scattered legs and floating hands.
             * (glass2.c:355/:360 pass v0 = 4; explosion.c:1897 is a signature.)
             *
             * The two length fields are still redundant and still checked -
             * against the low 16 bits now - since a disagreement means this is
             * not a vertex command at all. */
            unsigned n     = ((w0 >> 20) & 0x0f) + 1;
            unsigned v0    = (w0 >> 16) & 0x0fu;
            unsigned nbyte =  w0 & 0xffffu;
            const struct vtx *src;
            if (nbyte != n * sizeof(struct vtx)) { g_vtx_reject++; break; }
            if (v0) g_vtx_v0nz++;
            /* Rare's tri4 addresses nibbles 0-F, so a load can never
             * legitimately reach past slot 15; bound to the buffer regardless. */
            if (v0 + n > sizeof g_vbuf / sizeof g_vbuf[0]) {
                g_vtx_reject++; break;
            }
            src = dl_operand(w1, n * sizeof(struct vtx));
            if (src &&
                mem_readable((unsigned long) src, n * sizeof(struct vtx))) {
                memcpy(&g_vbuf[v0], src, n * sizeof(struct vtx));
                /* Point data lives in the same file as the list that names it,
                 * so it carries the same word order. sl_model_dls_swap swaps
                 * the six leading u16s of a native model's vertices at load
                 * (src/native/sl_swap_model.c); a ROM-order list's have not
                 * been touched, and the colour bytes never need it. */
                if (swapped) {
                    unsigned k;
                    for (k = v0; k < v0 + n; k++) {
                        struct vtx *v = &g_vbuf[k];
                        v->x = (short) SL_SW16(v->x);
                        v->y = (short) SL_SW16(v->y);
                        v->z = (short) SL_SW16(v->z);
                        v->flag = (unsigned short) SL_SW16(v->flag);
                        v->s = (short) SL_SW16(v->s);
                        v->t = (short) SL_SW16(v->t);
                    }
                }
                {   /* The RSP transforms at load; so does this. See g_veye. */
                    unsigned k;
                    for (k = v0; k < v0 + n; k++) vtx_transform(k);
                }
                {   /* B-125. Is this load a re-load of positions this
                     * submission has already DRAWN? Decided per load, not
                     * per triangle: the second triangle of every quad in
                     * a mesh has three already-seen vertices, but its load
                     * brought new ones. Only a load with nothing new is
                     * the authored second pass. */
                    unsigned k; int old = 1;
                    g_rd_loads++;
                    for (k = v0; k < v0 + n; k++)
                        if (!rd_lookup(g_veye[k], 0)) { old = 0; break; }
                    for (k = v0; k < v0 + n; k++)
                        g_vredraw[k] = (unsigned char) old;
                    if (old) g_rd_loads_old++;
                }
                {   /* LIVE OWNER BUG MARK: source provenance, recorded where
                     * the source is still in scope. Five stores per vertex,
                     * read only when a mark is written. See g_vsrc_seg. */
                    unsigned k;
                    unsigned off = dl_off_at(depth);
                    unsigned d1  = (g_dl_depth >= 1) ? g_dl_addr[1] : 0u;
                    for (k = v0; k < v0 + n; k++) {
                        g_vsrc_seg[k] = w1;
                        g_vsrc_i[k]   = (unsigned char) (k - v0);
                        g_vsrc_p[k]   = (const void *) (src + (k - v0));
                        g_vsrc_dl[k]  = d1;
                        g_vsrc_off[k] = off;
                    }
                }
                {   /* B-051 STAGE 2. Under G_LIGHTING the Vtx's last four
                     * bytes are NOT a colour: gbi.h's own union names them
                     * cn[] and the lit branch reads them as the normal
                     * (nx, ny, nz, alpha), SIGNED. The routing file already
                     * records this trap costing the boot logos - "the Vtx
                     * cn[] bytes there are NORMALS, not colours - reading
                     * them as colours is what makes the logos a smooth
                     * rainbow that follows surface orientation" - and the
                     * facility tinted-glass pane is the second case: its four
                     * vertices read (71,64,83) (185,64,83) (185,192,83)
                     * (71,192,83) as colour, which is a symmetric
                     * (+/-71, +/-64, +83) normal set as signed bytes.
                     *
                     * Recorded AT LOAD, with the geometry mode in force at
                     * that moment, because that is when the RSP consumes it -
                     * the same reason vtx_transform runs here rather than at
                     * draw time (B-021: a later matrix does not reach back).
                     *
                     * INERT THIS STAGE. Nothing reads g_vnrm yet and the
                     * colour path is untouched; stage 3 is what uses it. */
                    unsigned k;
                    int lit = (g_geom_mode & G_GEOM_LIGHTING) != 0;
                    for (k = v0; k < v0 + n; k++) {
                        const struct vtx *v = &g_vbuf[k];
                        g_vlit[k] = (unsigned char) lit;
                        g_vnrm[k][0] = (signed char) v->r;
                        g_vnrm[k][1] = (signed char) v->g;
                        g_vnrm[k][2] = (signed char) v->b;
                        g_vgen_ok[k] = 0;
                        /* B-051 STAGE 4. Compute here, read at draw. This
                         * writes for every G_LIGHTING vertex whether or not
                         * SL_LIGHT is set, exactly like stage 2's normals and
                         * stage 3's coordinates - so the census and the
                         * known-positive can be taken with the renderer
                         * unchanged, and a capture proves the state was seen
                         * rather than that a toggle was passed. */
                        g_vlrgb_ok[k] = 0;
                        if (lit) {
                            light_vertex(g_vnrm[k], g_mv[g_mv_sp],
                                         g_vlrgb[k]);
                            g_vlrgb_ok[k] = 1;
                            light_vtx_note(k, g_vnrm[k], g_vlrgb[k], v->a);
                        }
                        /* B-051 STAGE 3. Generate here, not at draw: the RSP
                         * consumes the normal when the 04 loads the vertex,
                         * under the matrix then in force, which is the same
                         * reason vtx_transform runs in this loop (B-021 - a
                         * later 01 does not reach back).
                         *
                         * The normal is TRANSFORMED, not used in object
                         * space. F3D dots the object normal against the
                         * light/lookat direction after moving that direction
                         * into model space by the modelview transpose, and
                         *     n . (M^T d)  ==  (M n) . d
                         * so transforming the normal by the modelview's upper
                         * 3x3 and dotting against the uploaded direction is
                         * the same arithmetic with one fewer matrix. */
                        /* An instrument that cannot fail is worthless: a
                         * surface that wants generated coordinates and does
                         * not get them must SAY so. Without this the logo arm
                         * produced no output at all and looked exactly like
                         * "the probe is off". */
                        if ((g_geom_mode & G_GEOM_TEXTURE_GEN) &&
                            lookat_seen_eff() != 3u) {
                            g_texgen_declined++;
                            if (g_texgen_declined == 1u &&
                                getenv("SL_TEXGEN_DBG") != NULL)
                                fprintf(stderr,
                                    "sl_texgen: DECLINED - geom has"
                                    " TEXTURE_GEN (mode=%08x) but the"
                                    " reflectance basis is incomplete"
                                    " (lookat_seen=%u, cmds=%u,"
                                    " default-basis=%s). No"
                                    " coordinate can be generated.\n",
                                    g_geom_mode, lookat_seen_eff(),
                                    g_lookat_cmds,
                                    texgen_defbasis() ? "armed" : "DISARMED");
                        }
                        if ((g_geom_mode & G_GEOM_TEXTURE_GEN) &&
                            lookat_seen_eff() == 3u) {
                            /* B-090. THE GUARD PROTECTS THE DIVISION, NOT THE
                             * GENERATION.
                             *
                             * A zero-length normal used to skip generation
                             * entirely, leaving g_vgen_ok clear, and the emit
                             * path then fell back to the vertex's SUPPLIED
                             * s/t. G_TEXTURE_GEN admits no such fallback: the
                             * RSP manufactures a coordinate for every vertex
                             * it processes while the bit is set, and a vertex
                             * that expects generated coordinates carries
                             * s=t=0 precisely because they are meant to be
                             * replaced. Declining left those vertices reading
                             * texel (0,0).
                             *
                             * MEASURED, GOLDENEYE_LOGO: the letters are one
                             * material - 282 triangles, 846 emitted vertices,
                             * geometry mode 00062205 (G_LIGHTING |
                             * G_TEXTURE_GEN), combiner fc26a004 1f1093ff,
                             * one 32x32 RGBA/16b tile - and 49 of those 846
                             * vertices carry cn bytes (0,0,0). Their vertex
                             * records are otherwise intact (positions
                             * -1167,82,0 / -1218,29,0 / -1217,-51,0, alpha
                             * 255), so this is Rare's model data, not a
                             * truncated load. That tile is a pure vertical
                             * ramp - measured constant along S, spread 0 in
                             * all 32 rows - running red at T=0 to white at
                             * T=31, so the declined vertices sampled texel
                             * (0,0) = 255,0,0 and dragged RED across gold
                             * letter faces. The ring is a different draw
                             * entirely (57 triangles, geometry mode
                             * 00002205, no G_TEXTURE_GEN, vertex colour
                             * 255,0,0) and was already correct.
                             *
                             * The dot of a ZERO vector is zero whichever
                             * normalisation order is taken - it is the
                             * numerator that vanishes - so ds = dt = 0 and
                             * the generated coordinate is the MIDPOINT of the
                             * map. On this tile that is texel row 16 =
                             * 255,255,0, gold. Mode 1 already reaches the
                             * same answer for the same reason: its own
                             * `if (nl < 1e-6f) nl = 1.0f` leaves acc at 0.
                             *
                             * That behaviour now lives inside
                             * texgen_generate(), which returns whether the
                             * transformed normal had length so this site can
                             * keep counting its own population. The counter
                             * is what contains the claim: not a logo special
                             * case and not a colour change, only vertices
                             * whose transformed normal is zero under
                             * G_TEXTURE_GEN can take a different path. */
                            float n[3], pre[2], d[2], e[3], u[3];
                            n[0] = (float) g_vnrm[k][0];
                            n[1] = (float) g_vnrm[k][1];
                            n[2] = (float) g_vnrm[k][2];
                            if (texgen_generate(n, g_mv[g_mv_sp], pre, d, e, u))
                                g_texgen_zeronrm++;
                            g_vgen[k][0] = pre[0];
                            g_vgen[k][1] = pre[1];
                            g_vgen_ok[k] = 1;
                            g_texgen_verts++;
                            texgen_note(k, d[0], d[1], e, u);
                        }
                        if (lit) vtx_nrm_note(k);
                    }
                    if (lit) g_vtx_lit_loads++;
                }
                {   /* B-043 probe: where each slot's point came from, so the
                     * edge table can be keyed on the MESH vertex rather than
                     * on a slot number that a later load reassigns. */
                    unsigned k;
                    for (k = v0; k < v0 + n; k++) {
                        g_vsrc[k] = (unsigned long) (src + (k - v0));
                        /* B-051 SURVEY KEY. g_vsrc is the address of the
                         * INDIVIDUAL vertex, which is the wrong granularity
                         * for "which model is this": consecutive triangles of
                         * one model index different slots and so produce
                         * different addresses. Keying the G_TEXTURE_GEN
                         * census on it OVER-SPLIT one shared model into a row
                         * per vertex offset (measured: entries 0x20 apart,
                         * same material, x60 each), which would have made a
                         * handful of independent surfaces out of one. The
                         * load's BASE address is the model-level identity. */
                        g_vbase[k] = (unsigned long) src;
                    }
                }
                g_verts += n;
            } else {
                /* B-139. Nothing readable behind the operand: dropped,
                 * counted and reported rather than dereferenced. */
                vtx_unresolved(w0, w1, depth);
            }
            /* Slot numbers now mean different points; the edge table that
             * indexes by slot has to go with them. See g_edge_seen. */
            memset(g_edge_seen, 0, sizeof g_edge_seen);
            break;
        }
        case OP_MTX:
            do_matrix((w0 >> 16) & 0xff, w0 & 0xffff, w1);
            break;
        case OP_POPMTX:
            do_popmatrix(w0);
            break;
        case OP_TRI1:
            /* "Absolute Basic Rendering Models.txt", "BF single triangle draw
             * routine": BF000000 00zzyyxx - the indices are in the LOWER word
             * and each must be divided by 0xA. Its worked examples:
             * BF000000 00140A00 = {0,1,2} and 00968C82 = {13,14,15}.
             * The old decode read them out of w0, which is 0xBF000000 in every
             * such command, so every G_TRI1 drew the degenerate {0,0,0}.
             *
             * WHICH BYTE IS THE FIRST VERTEX is a separate question, and the
             * note cannot answer it: it writes both examples as SETS ({0,1,2}),
             * and a set has no winding. Its prose calls the low byte "x" and
             * says "the first point in the triangle is listed as x", and an
             * earlier pass took that at face value - correcting the command by
             * the note without ever measuring it, because tri1 is 0 in Facility
             * and dam ROOM geometry, which is all that had been looked at.
             *
             * CHARACTERS use it, and they are where the order shows. Authority
             * is include/PR/gbi.h:2042-2044, the plain-F3D branch this build
             * compiles (neither F3DEX_GBI nor F3DEX_GBI_2 defined - see the
             * gSPVertex citation in the vertex block):
             *
             *   __gsSP1Triangle_w1f(v0,v1,v2,flag) =
             *       _SHIFTL(flag,24,8) | _SHIFTL(v0*10,16,8)
             *     | _SHIFTL(v1*10,8,8) | _SHIFTL(v2*10,0,8)
             *
             * v0 is bits 16-23 - the note's "zz" - and v2 is the low byte. The
             * multiplier agrees with the note (10 decimal == 0xA), so only the
             * ORDER was wrong, and reversed order is reversed WINDING.
             *
             * Measured on Bond's model during the level-start swirl, keying a
             * directed-edge table on each vertex's source address so the two
             * halves of a shared edge are seen even when they arrive in
             * different 04 batches: of 2190 directed edges, 14 were traversed
             * twice in the SAME direction - two triangles facing opposite ways,
             * which is exactly what opens a hole under back-face culling. All
             * 14 came from this command, which contributes just 15 of the
             * model's 730 triangles; GE_TRI4's other 715 had zero. With the
             * order corrected the count is 0. */
            g_fd_slot = 4;                       /* B-043 probe */
            g_tri_cw0 = w0; g_tri_cw1 = w1;      /* #25 witness */
            emit_tri(((w1 >> 16) & 0xff) / 0xA, ((w1 >>  8) & 0xff) / 0xA,
                     ( w1        & 0xff) / 0xA);
            break;
        case OP_TRI4:
            g_tri_cw0 = w0; g_tri_cw1 = w1;      /* #25 witness */
            tri4(w0, w1);
            break;
        case OP_SETTIMG:
            /* ucode05.txt "FD rdp_settextureimage" */
            TEXTRACE("FD settimg fmt=%u siz=%u w=%u addr=%p",
                     (w0 >> 21) & 0x07, (w0 >> 19) & 0x03, (w0 & 0x0fff) + 1,
                     (const void *) tex_operand(w1));
            g_ti_fmt  = (w0 >> 21) & 0x07;
            g_ti_siz  = (w0 >> 19) & 0x03;
            g_ti_w    = (w0 & 0x0fff) + 1;
            g_ti_addr = tex_operand(w1);
            break;
        case OP_SETTILE: {
            /* ucode05_old.txt "F5 rdp_settile" */
            struct sl_tile *t = &g_tile[(w1 >> 24) & 0x07];
            t->fmt  = (w0 >> 21) & 0x07;
            t->siz  = (w0 >> 19) & 0x03;
            t->line = (w0 >> 9) & 0x1ff;
            t->tmem = w0 & 0x1ff;
            t->pal  = (w1 >> 20) & 0x0f;
            t->cmt  = (w1 >> 18) & 0x03;   /* 0x00080000 clamp | 0x00040000 mirror */
            t->cms  = (w1 >> 8) & 0x03;    /* 0x00000200 clamp | 0x00000100 mirror */
            /* B-048. shiftt 0x00003C00, shifts 0x0000000F. ucode05_old.txt's
             * F5 entry does not break the lower word out field by field, so
             * the authority here is gbi.h:3401 gDPSetTile, which
             * packs `_SHIFTL(shiftt,10,4)` and `_SHIFTL(shifts,0,4)` - and
             * the explosion lists are its own emitter (assets/oddtextures.c).
             * Values 0..10 shift the incoming s/t RIGHT; 11..15 shift LEFT by
             * 16-shift. Only right shifts occur in this game - see the
             * tex1_dims telemetry, which counts anything else. */
            t->shiftt = (w1 >> 10) & 0x0f;
            t->shifts =  w1        & 0x0f;
            /* B-088. The WRAP EXPONENTS, which this parser had never read.
             * Authority is include/PR/gbi.h:3415 gsDPSetTile, which packs
             * `_SHIFTL(maskt,14,4)` and `_SHIFTL(masks,4,4)` - the same
             * lower word whose shift fields B-048 took from :3401. The RDP
             * wraps a repeating axis at 2^mask; settilesize is the CLAMP
             * rectangle, not the image extent. See tile_texture. */
            t->maskt = (w1 >> 14) & 0x0f;
            t->masks = (w1 >>  4) & 0x0f;
            TEXTRACE("F5 settile tile=%u fmt=%u siz=%u line=%u tmem=%u"
                     " pal=%u cms=%u masks=%u shifts=%u cmt=%u maskt=%u shiftt=%u",
                     (w1 >> 24) & 0x07, t->fmt, t->siz, t->line, t->tmem,
                     t->pal, t->cms, t->masks, t->shifts, t->cmt, t->maskt,
                     t->shiftt);
            break;
        }
        case OP_TILESIZE: {
            /* ucode05.txt "F2 rdp_settilesize" */
            struct sl_tile *t = &g_tile[(w1 >> 24) & 0x07];
            t->uls = (w0 >> 12) & 0x0fff;
            t->ult =  w0        & 0x0fff;
            t->lrs = (w1 >> 12) & 0x0fff;
            t->lrt =  w1        & 0x0fff;
            /* B-109. Presentation-only fractional origin, when THIS command
             * was registered by a native writer and its phase still
             * truncates to the integers just decoded. Every other F2 -
             * including a stale entry - zeroes the fraction. */
            t->fs = t->ft = 0.0f;
            {
                const struct sl_f2frac *fe = f2frac_find(dl);
                if (fe != NULL &&
                    (unsigned) fe->s_q == t->uls &&
                    (unsigned) fe->t_q == t->ult) {
                    t->fs = fe->s_q - (float) t->uls;
                    t->ft = fe->t_q - (float) t->ult;
                }
            }
            TEXTRACE("F2 tilesize tile=%u s[%u,%u] t[%u,%u] -> %ux%u",
                     (w1 >> 24) & 0x07, t->uls, t->lrs, t->ult, t->lrt,
                     ((t->lrs - t->uls) >> 2) + 1, ((t->lrt - t->ult) >> 2) + 1);
            break;
        }
        case OP_LOADBLOCK:
            /* ucode05.txt "F3 rdp_loadblock". This is the moment pixels move,
             * and the only way a source address ever reaches the decoder. */
            g_loadblocks++;
            if (g_ti_addr != NULL) g_load_src = g_ti_addr;
            /* gbi.h:3444 puts dxt in the low 12 bits of w1. See the block at
             * g_load_dxt for why this one field decides the row swap. */
            g_load_dxt = w1 & 0xfffu;
            /* lrs, the texel count, ucode05.txt "F3 rdp_loadblock". Kept only
             * so the report can size the image the load moved. */
            g_load_texels = ((w1 >> 12) & 0xfffu) + 1u;
            /* Which tmem address this load fills: the tmem of the tile the F3
             * itself names (bits 24-26, the same tile field every other tile
             * command uses), which the F5 before it has already set. That is
             * what makes tmem -> source well-defined at the F3. */
            tmem_store(g_tile[(w1 >> 24) & 0x07].tmem, g_ti_addr, g_load_dxt,
                       g_load_texels, g_ti_fmt, g_ti_siz);
            TEXTRACE("F3 loadblock tile=%u tmem=%u src=%p texels=%u dxt=%u",
                     (w1 >> 24) & 0x07, g_tile[(w1 >> 24) & 0x07].tmem,
                     (const void *) g_load_src, g_load_texels, g_load_dxt);
            if (g_load_dxt == 0) g_load_dxt0++;
            else { g_load_dxtn++;
                   dxtn_load(g_ti_addr, g_ti_fmt, g_ti_siz, g_ti_w); }
            break;
        case OP_LOADTLUT: {
            /* ucode05.txt, the entry above F2, packs F0 like every other tile
             * load: w0 carries uls/ult and w1 tile/lrs/lrt, all 10.2 fixed.
             * This tree's own emitter agrees exactly - gDPLoadTLUT07(a,b,c,d)
             * in include/gbi_extension.h:162 writes w0 = a<<14 | b<<2 and
             * w1 = 07<<24 | c<<14 | d<<2, i.e. uls = a<<2 etc., so a/b/c are
             * the fields below in whole texels.
             *
             * The palette is NOT the FD image's base address, which is what
             * this case used to assume and what B-018's rainbow was. A
             * paletted GoldenEye texture gets ONE settextureimage - at
             * tex->data - then a loadblock, then the loadtlut against that
             * same still-current image: src/game/tex.c:522-552, repeated
             * verbatim at tex.c:632-670 and othermodemicrocode.c:455-479 and
             * :595-616. Tree-wide, `grep -rn "gDPLoadTLUT" src/` returns
             * those four sites plus textrelated.c's two gDPLoadTLUT_pal16
             * (Japanese font, which DOES set its own FD, giving uls=0) and
             * front.c's one gDPLoadTLUTCmd2 - so no GE path re-points the
             * image between the block load and the palette load.
             *
             * The palette therefore lives INSIDE the texture buffer, `uls`
             * texels past its start: tex.c:533 sets `uls = len`, the byte
             * length of the texel data itself. The uls/ult split right below
             * it exists only because the field is 10 bits wide - `if (0x3ff -
             * unk0a < uls) ult = 0x3ff - unk0a; uls -= ult` - so the true
             * offset is the SUM, addressed through the width the FD declared
             * (which GE always sets to 1). Entry count is lrs - uls + 1 =
             * tex->unk0a + 1 = numcolours, from image.c:202 `numcolours =
             * texReadBits(8) + 1` and image.c:215 `unk0a = numcolours - 1`.
             *
             * Both the address and the count are validated before use: this
             * arithmetic is driven by ROM data.
             */
            unsigned uls = (w0 >> 14) & 0x3ff;
            unsigned ult = (w0 >>  2) & 0x3ff;
            unsigned lrs = (w1 >> 14) & 0x3ff;
            unsigned tiw = g_ti_w ? g_ti_w : 1;
            unsigned off = (ult * tiw + uls) * 2u;

            g_tlut_cmds++;
            if (off == 0) g_tlut_off0++;
            g_tlut_off = off;
            g_tlut_n   = (lrs >= uls) ? (lrs - uls + 1u) : 0u;
            if (g_tlut_n > 256u) g_tlut_n = 256u;
            g_tlut_addr = NULL;
            if (g_ti_addr != NULL && g_tlut_n != 0u &&
                mem_readable((unsigned long) g_ti_addr + off, g_tlut_n * 2u))
                g_tlut_addr = g_ti_addr + off;
            else
                g_tlut_bad++;
            TEXTRACE("F0 loadtlut tile=%u tmem=%u img=%p n=%u addr=%p",
                     (unsigned) ((w1 >> 24) & 7u),
                     g_tile[(w1 >> 24) & 7u].tmem,
                     (const void *) g_ti_addr, g_tlut_n,
                     (const void *) g_tlut_addr);
            break;
        }
        case OP_TEXTURE:
            /* ucode05.txt "BB rsp_uc05_texture" - see the citation at the
             * g_tex_sscale declaration. B-051 STAGE 1: level and the S/T
             * scale are kept now instead of discarded. Inert this stage. */
            g_tex_on     = (w0 & 0xff) != 0;
            g_tex_tile   = (w0 >> 8) & 0x07;
            g_tex_level  = (w0 >> 11) & 0x07;
            g_tex_sscale = (w1 >> 16) & 0xffff;
            g_tex_tscale =  w1        & 0xffff;
            bb_note();
            TEXTRACE("BB texture on=%u tile=%u level=%u s=%u t=%u",
                     g_tex_on, g_tex_tile, g_tex_level,
                     g_tex_sscale, g_tex_tscale);
            break;
        case OP_SETOTHH:
            /* gbi.h gsSPSetOtherMode packing; see the citation block above. */
            if (((w0 >> 8) & 0xff) == 14)          /* G_MDSFT_TEXTLUT */
                g_tlut_mode = (w1 >> 14) & 0x03;
            else if (((w0 >> 8) & 0xff) == 20)     /* G_MDSFT_CYCLETYPE */
                g_cycle_type = (w1 >> 20) & 0x03;
            /* B-144. G_MDSFT_TEXTLOD = 16, len 1: G_TL_TILE (0) or G_TL_LOD
             * (1 << 16) - "BA001001 00010000 ;images use lod" in the notes'
             * own example (Texture application (Rooms).txt, cited at the
             * B9/BA block above), and the prop lists switch it OFF again
             * before their CI images ("ba001001 00000000"). It decides
             * whether the RDP computes a per-pixel LOD at all: with it off,
             * LOD_FRACTION is 0 and the c=13 lerp is TEXEL0. */
            else if (((w0 >> 8) & 0xff) == 16)     /* G_MDSFT_TEXTLOD */
                g_textlod = (w1 >> 16) & 0x01;
            break;
        case OP_SETSCISS: {
            /* ucode05.txt "ED rdp_setscissor", coordinates in 10.2 fixed */
            int x0 = (int) ((w0 >> 12) & 0x0fff) >> 2;
            int y0 = (int) ( w0        & 0x0fff) >> 2;
            int x1 = (int) ((w1 >> 12) & 0x0fff) >> 2;
            int y1 = (int) ( w1        & 0x0fff) >> 2;
            /* FF carries the framebuffer WIDTH but no height, so the tallest
             * scissor in the frame is what the 2D ortho gets for its height.
             * lvlRender issues a full-screen one every frame (lv.c:700,
             * `0, 0, viGetX(), viGetY()`), so this is not a guess.
             *
             * "in the FRAME" is load-bearing, and used not to hold. The rule
             * was raise-only and nothing ever reset it, so the height was in
             * fact the tallest scissor in the PROCESS. The game changes VI
             * size in BOTH directions - front.c:8555 sets 440x330 for the
             * front end, and bondview2.c:8204 sets it back through
             * getWidth320or440()/getHeight330or240() - so a height that could
             * only rise never followed it back down.
             *
             * Reaching a level THROUGH the menus therefore left g_scr_h at
             * 330 while g_scr_w correctly returned to 320 (FF assigns it),
             * and both consumers scaled by 720/330 instead of 720/240: the 2D
             * ortho (glOrtho below) and vp_derive alike, so world, weapon and
             * HUD all drew into the top 73% of the window with a black band
             * under them. Measured at 960x720 - the game asked for the same
             * viewport either way, scale=(640,440) trans=(640,480) ->
             * y[10,230], and native installed gl=[0 218 960 480] through the
             * menus against gl=[0 30 960 660] on direct boot. Direct boot
             * never passes through the front end, which is why only the menu
             * path showed it.
             *
             * So the frame's FIRST scissor ASSIGNS the height and later ones
             * only raise it. Measured over the front end and a level reached
             * through it: nothing is ever drawn before the first scissor of a
             * frame (0 rects and 0 triangles, every frame sampled), so this
             * cannot move the ortho under rectangles already on screen -
             * which is the hazard the raise-only rule was avoiding. Split
             * screen is unaffected: viSetupScreensForNumPlayers (fr.c:760)
             * issues the full-screen scissor before the per-player ones.
             *
             * B-143: adopted BEFORE the box is mapped to GL below.
             * sciss_gl_apply scales by g_scr_w/g_scr_h, so on the one frame
             * where the VI size changes (440x330 front end <-> 320x240
             * level) a box mapped first would be scaled by the OLD height
             * and clip the wrong part of the window for that frame. Same
             * rule, same numbers, ordered so the height a box is scaled by
             * is the height it was authored against. */
            if (y1 >= 64 && y1 <= 1024) {
                unsigned before = g_scr_h;
                if (!g_scr_h_adopted) { g_scr_h = (unsigned) y1; g_scr_h_adopted = 1; }
                else if (y1 > (int) g_scr_h) g_scr_h = (unsigned) y1;
                if (g_scr_h != before) rects_update();     /* #45: the fit follows the logical size */
            }
            if (x1 <= x0 || y1 <= y0) {
                /* B-143. An EMPTY box. The RDP would draw nothing under it;
                 * this build leaves the previous box in force instead, as it
                 * always has, and counts the event so the approximation is
                 * visible (sl_sciss census line). Never observed: bg.c only
                 * adds a room whose window passed bgIsRoomOnScreen. */
                g_sciss_empty++;
            }
            if (x1 > x0 && y1 > y0) { g_sciss[0] = x0; g_sciss[1] = y0;
                                      g_sciss[2] = x1; g_sciss[3] = y1;
                /* B-101 / B-143. The decoded box reaches GL here and NOWHERE
                 * else: on every ordinary frame (B-143, SL_SCISSOR=0 to
                 * revert) and on the witness re-walk, but never on pass A
                 * of a witness frame, which is the unscissored side of that
                 * comparison. Every distinct box is logged on the witness
                 * pass so the diff can be banded to the region under test
                 * instead of counted over the whole frame - a whole-frame
                 * count is not evidence (docs/backlog.md, B-099 "A/B capture
                 * is NOT a valid instrument here"). */
                if (g_wit_pass || (sciss_on() && !g_wit_a)) {
                    sciss_gl_apply(x0, y0, x1, y1);
                    g_sciss_applied++;
                }
                if (g_wit_pass) {
                    if (g_wit_rects_n < (int) (sizeof g_wit_rects / sizeof g_wit_rects[0])) {
                        int k, dup = 0;
                        for (k = 0; k < g_wit_rects_n; k++)
                            if (g_wit_rects[k][0] == x0 && g_wit_rects[k][1] == y0 &&
                                g_wit_rects[k][2] == x1 && g_wit_rects[k][3] == y1)
                            { dup = 1; break; }
                        if (!dup) {
                            g_wit_rects[g_wit_rects_n][0] = x0;
                            g_wit_rects[g_wit_rects_n][1] = y0;
                            g_wit_rects[g_wit_rects_n][2] = x1;
                            g_wit_rects[g_wit_rects_n][3] = y1;
                            g_wit_rects_n++;
                        }
                    }
                }
            }
            break;
        }
        case OP_TEXRECT:
        case OP_TEXRECTFLIP:
            /* ucode05.txt "E4 rdp_texrect": one word is a corner as
             * 00FFF000 x / 00000FFF y in 10.2 fixed, the other the same plus
             * tile 0x07000000. Which corner is which is settled by min/max at
             * draw time - see the citation block. Nothing is drawn until B3
             * arrives with dsdx/dtdy. */
            g_tr_cmds++;
            if (g_tr_pending) g_tr_abandoned++;
            g_tr_ax   = (int) ((w0 >> 12) & 0x0fff);
            g_tr_ay   = (int) ( w0        & 0x0fff);
            g_tr_tile = (w1 >> 24) & 0x07;
            g_tr_bx   = (int) ((w1 >> 12) & 0x0fff);
            g_tr_by   = (int) ( w1        & 0x0fff);
            g_tr_flip = (op == OP_TEXRECTFLIP);
            g_tr_s = g_tr_t = 0;
            g_tr_dsdx = g_tr_dtdy = 1 << 10;
            g_tr_have_st = 0;
            g_tr_pending = 1;
            break;
        case OP_RDPHALF_1:
            /* ucode05.txt "B4 rsp_uc05_rdphalf_1": the data word is the LOWER
             * word. B4 also precedes branch-on-z and load-ucode, so it is only
             * read as texrect s/t while a texrect is actually pending. With no
             * texrect pending it is a word of a hand-built RDP command - the
             * sky and water generators, per ucode05.txt's B2 entry. */
            if (!g_tr_pending) { g_half_orphan++; rdphalf_push(w1); break; }
            g_tr_s = (int) (short) ((w1 >> 16) & 0xffff);
            g_tr_t = (int) (short) ( w1        & 0xffff);
            g_tr_have_st = 1;
            break;
        case OP_RDPHALF_2:
            /* ucode05.txt "B3 rsp_uc05_rdphalf_2" - the second half. This is
             * the command that completes the rectangle. Outside a texrect it
             * closes a hand-built command instead (skyRenderFull ends its
             * block with one, sky.c:1933). */
            if (!g_tr_pending) { g_half_orphan++; rdphalf_push(w1); break; }
            g_tr_dsdx = (int) (short) ((w1 >> 16) & 0xffff);
            g_tr_dtdy = (int) (short) ( w1        & 0xffff);
            if (!g_tr_have_st) g_tr_s = g_tr_t = 0;
            draw_texrect();
            break;
        case OP_RDPHALF_CONT:
            /* ucode05.txt "B2 rsp_uc05_rdphalf_cont": continues the command
             * begun by a B4 with the given lower word, "used only when
             * operators longer than 64bits are in use". It is never a texrect
             * operand - only B4/B3 are - so it always feeds the assembler. */
            rdphalf_push(w1);
            break;
        case OP_FILLRECT:
            /* ucode05.txt "F6 rdp_fillrect": lower right in the upper word,
             * upper left in the lower, x at 0x00FFC000 and y at 0x00000FFC
             * (whole pixels; the fraction bits below each are never used). */
            g_fr_cmds++;
            draw_fillrect((int) ((w1 >> 14) & 0x3ff), (int) ((w1 >> 2) & 0x3ff),
                          (int) ((w0 >> 14) & 0x3ff), (int) ((w0 >> 2) & 0x3ff));
            break;
        case OP_SETFILLCOL: {
            /* ucode05.txt "F7 rdp_setfillcolour": 5551 in the low half, and
             * "if both are used it indicates a 32bit colour". Both shapes are
             * emitted by this tree (fr.c:947 and fr.c:951), so the note's own
             * rule is what tells them apart. */
            unsigned hi = (w1 >> 16) & 0xffff, lo = w1 & 0xffff;
            g_col_cmds[4]++;
            if (hi == lo) {
                unsigned r = (lo >> 11) & 0x1f, g = (lo >> 6) & 0x1f;
                unsigned b = (lo >> 1) & 0x1f;
                g_fill[0] = (unsigned char) ((r << 3) | (r >> 2));
                g_fill[1] = (unsigned char) ((g << 3) | (g >> 2));
                g_fill[2] = (unsigned char) ((b << 3) | (b >> 2));
                g_fill[3] = (unsigned char) ((lo & 1) ? 255 : 0);
                g_fill_is16 = 1;
            } else {
                g_fill[0] = (unsigned char) ((w1 >> 24) & 0xff);
                g_fill[1] = (unsigned char) ((w1 >> 16) & 0xff);
                g_fill[2] = (unsigned char) ((w1 >>  8) & 0xff);
                g_fill[3] = (unsigned char) ( w1        & 0xff);
                g_fill_is16 = 0;
            }
            break;
        }
        case OP_SETPRIMCOL:
            /* ucode05.txt "FA rdp_setprimcolour" */
            g_col_cmds[0]++;
            g_prim_lod_min   = (w0 >> 8) & 0xff;
            g_prim_lod_level =  w0       & 0xff;
            g_prim[0] = (unsigned char) ((w1 >> 24) & 0xff);
            g_prim[1] = (unsigned char) ((w1 >> 16) & 0xff);
            g_prim[2] = (unsigned char) ((w1 >>  8) & 0xff);
            g_prim[3] = (unsigned char) ( w1        & 0xff);
            break;
        case OP_SETENVCOL:                 /* ucode05.txt "RDP GSetColor" */
            g_col_cmds[1]++;
            g_env[0] = (unsigned char) ((w1 >> 24) & 0xff);
            g_env[1] = (unsigned char) ((w1 >> 16) & 0xff);
            g_env[2] = (unsigned char) ((w1 >>  8) & 0xff);
            g_env[3] = (unsigned char) ( w1        & 0xff);
            break;
        case OP_SETFOGCOL:
            g_col_cmds[2]++;
            g_fog[0] = (unsigned char) ((w1 >> 24) & 0xff);
            g_fog[1] = (unsigned char) ((w1 >> 16) & 0xff);
            g_fog[2] = (unsigned char) ((w1 >>  8) & 0xff);
            g_fog[3] = (unsigned char) ( w1        & 0xff);
            break;
        case OP_SETBLENDCOL:
            g_col_cmds[3]++;
            g_blend[0] = (unsigned char) ((w1 >> 24) & 0xff);
            g_blend[1] = (unsigned char) ((w1 >> 16) & 0xff);
            g_blend[2] = (unsigned char) ((w1 >>  8) & 0xff);
            g_blend[3] = (unsigned char) ( w1        & 0xff);
            break;
        case OP_SETCOMBINE: {
            /* ucode05_old.txt "FC rdp_setcombine". Classified, not emulated:
             * the RDP computes (a - b) * c + d, so a zero multiplier (the wide
             * c slot's 16-31 all mean 0) makes the colour output exactly d. */
            unsigned c0  = (w0 >> 15) & 0x1f;
            unsigned d0  = (w1 >> 15) & 0x07;
            unsigned aa0 = (w0 >> 12) & 0x07, ab0 = (w1 >> 12) & 0x07;
            unsigned ac0 = (w0 >>  9) & 0x07, ad0 = (w1 >>  9) & 0x07;
            g_cc_cmds++;
            g_cc_w0 = w0; g_cc_w1 = w1;
            g_cc_rgb_const = (c0 >= 16) ? (int) d0 : -1;
            /* B-046. Both cycles' RGB muxes, for cc_tint above. */
            g_cc_ra[0] = (w0 >> 20) & 0x0f;  g_cc_rb[0] = (w1 >> 28) & 0x0f;
            g_cc_rc[0] = c0;                 g_cc_rd[0] = d0;
            g_cc_ra[1] = (w0 >>  5) & 0x0f;  g_cc_rb[1] = (w1 >> 24) & 0x0f;
            g_cc_rc[1] =  w0        & 0x1f;  g_cc_rd[1] = (w1 >>  6) & 0x07;
            g_cc_a_prim  = (aa0 == CC_PRIM   || ab0 == CC_PRIM ||
                            ac0 == CC_PRIM   || ad0 == CC_PRIM);
            g_cc_a_texel = (aa0 == CC_TEXEL0 || ab0 == CC_TEXEL0 ||
                            ac0 == CC_TEXEL0 || ad0 == CC_TEXEL0);
            /* B-043. ENVIRONMENT as the sole alpha MULTIPLIER, with the
             * subtrahend and the addend both the "0" mux (7). That is the one
             * shape whose result is a plain scale of `a`, so it is the only
             * one this classify-don't-emulate path can carry exactly. Any
             * other appearance of ENV in the alpha muxes is left alone -
             * modelApplyRenderModeType3's VIEWER+1 branch spells
             * (TEXEL0 - ENV) * SHADE + ENV and must keep drawing as it does. */
            g_cc_a_envscale = (ac0 == CC_ENV && ab0 == CC_A_ZERO &&
                                                ad0 == CC_A_ZERO);
            /* B-048. The one shape that needs a second texture unit: the
             * exact product (TEXEL0 - 0) * TEXEL1 + 0, which is
             * G_CC_INTERFERENCE (gbi.h:557 `TEXEL0, 0, TEXEL1, 0`). b's
             * 8..15 and d's 7 are the constant ZERO (ucode05_old.txt "FC
             * rdp_setcombine"), the same reading cc_tint uses. The mip
             * chain's (TEXEL1 - TEXEL0) * LODFRAC + TEXEL0 is a different
             * shape and is deliberately NOT claimed - see tex1_apply. */
            g_cc_tex1mul = (g_cc_ra[0] == CC_TEXEL0 && g_cc_rb[0] >= 8u &&
                            g_cc_rc[0] == CC_TEXEL1 && g_cc_rd[0] == CC_A_ZERO);
            /* B-107. The second shape that needs the unit: the exact lerp
             * (TEXEL1 - TEXEL0) * PRIM_LOD_FRAC + TEXEL0, cycle-0 RGB. The
             * c-slot code 14 is gbi.h:466 G_CCMUX_PRIM_LOD_FRAC. This is the
             * Dam/Caverns water family (fc272c04..., written by
             * unk_092E50.c's MipMap2C setup lists, whose FA low byte the
             * game animates every frame): its "next LOD level" is the SAME
             * image through a tile whose origin scrolls at a different rate,
             * and the lerp between the two IS the water's top layer. The
             * LODFRAC family (c-slot 13, the real mip chains) keeps drawing
             * the base tile alone exactly as B-048 decided - a renderer with
             * no mip chain has no second level to blend. */
            g_cc_tex1lerp = (g_cc_ra[0] == CC_TEXEL1 && g_cc_rb[0] == CC_TEXEL0 &&
                             g_cc_rc[0] == 14u       && g_cc_rd[0] == CC_TEXEL0);
            /* B-118. The LOD_FRACTION sibling (c-slot 13). The comment above
             * dismissed it as "the real mip chains ... no second level to
             * blend", and that assumption is the reason Surface's perimeter
             * rendered as noise: measured at the owner's mark
             * (20260912-051649-lvl36 mark-001, SL_TEX_TRACE), the tree-wall
             * material loads TWO DIFFERENT IMAGES - tile 0 an IA8 32x32
             * noise DETAIL texture, tiles 1..6 a 64x17 RGBA16 TREELINE STRIP
             * with its own mip chain - and lerps them by LOD_FRACTION:
             * detail up close, trees at distance. Drawing the base tile
             * alone showed the noise at every distance. This is GoldenEye's
             * terrain detail-blend, not a mip chain. */
            g_cc_tex1lodlerp = (g_cc_ra[0] == CC_TEXEL1 && g_cc_rb[0] == CC_TEXEL0 &&
                                g_cc_rc[0] == 13u       && g_cc_rd[0] == CC_TEXEL0);
            g_cc_a_envsrc   = (int) aa0;
            /* B-051. The tinted-glass pane, and the reason it drew as a hole.
             *
             * Every alpha reading in this file used CYCLE 0 only. The pane
             * spells its opacity in CYCLE 1: measured live on facility,
             * `fc26a004 1f1093fb`, whose alpha stages are
             *     a0 = (TEX1 - TEX0) * COMB + TEX0     (the LOD lerp)
             *     a1 = (COMB - 0) * SHADE + PRIM
             * with prim = 000000ff. That PRIM addend IS the opacity
             * glassCalculateOpacity computed from distance (propobj.c:4203 ->
             * envcolour.word -> gDPSetPrimColor, model.c:3593), so a cycle-0
             * reading cannot see it and the pane inherited the vertex alpha.
             *
             * Mux offsets are ucode05_old.txt "FC rdp_setcombine", lower
             * word: Aa1 00E00000, Ac1 001C0000, Ab1 00000038, Ad1 00000007.
             *
             * Deliberately an EXACT shape, the same discipline as
             * g_cc_tex1mul and g_cc_a_envscale. Blast radius measured rather
             * than assumed: across the owner's facility session this shape
             * matches exactly ONE combiner word, the one above, 2869 times -
             * and B-048's mip-chain family is `...ff`, which differs only in
             * Ad1 (7 = the constant "0") and is NOT matched. One nibble
             * separates the glass from the family it was swept into. */
            {   unsigned aa1 = (w1 >> 21) & 7, ab1 = (w1 >> 3) & 7;
                unsigned ac1 = (w1 >> 18) & 7, ad1 =  w1       & 7;
                g_cc_a_prim1 = (aa1 == CC_COMBINED && ab1 == CC_A_ZERO &&
                                ac1 == CC_SHADE    && ad1 == CC_PRIM);
            }

            /* B-051 THE FIX. Store BOTH cycles' alpha muxes - see the AEQ_
             * block above. The mode is resolved at DRAW time, not here,
             * because which cycle reaches memory depends on the cycle type
             * and that arrives from BA setothermode_h, which a list may issue
             * after its FC. Classifying here would read a stale cycle type on
             * exactly the lists that switch mode mid-frame. */
            g_aeq_0a = (w0 >> 12) & 7; g_aeq_0b = (w1 >> 12) & 7;
            g_aeq_0c = (w0 >>  9) & 7; g_aeq_0d = (w1 >>  9) & 7;
            g_aeq_1a = (w1 >> 21) & 7; g_aeq_1b = (w1 >>  3) & 7;
            g_aeq_1c = (w1 >> 18) & 7; g_aeq_1d =  w1        & 7;
            /* B-035. Both cycles, colour and alpha. Deliberately the widest
             * possible reading of "uses a texel" - the gate below only fires
             * when NOTHING in the combiner mentions one, which is the only
             * case where dropping the texture is unambiguously what the RDP
             * would do. Cycle 1 is included because 1-cycle lists still carry
             * a second copy of the muxes and 2-cycle lists reach TEXEL0 there
             * through COMBINED. */
            {   unsigned ab = (w0 >> 20) & 0x0f, bb = (w1 >> 28) & 0x0f;
                unsigned cb = c0,                db = d0;
                unsigned a1 = (w0 >>  5) & 0x0f, b1 = (w1 >> 24) & 0x0f;
                unsigned c1 =  w0        & 0x1f, d1 = (w1 >>  6) & 0x07;
                unsigned aa1 = (w1 >> 21) & 0x07, ab1 = (w1 >> 3) & 0x07;
                unsigned ac1 = (w1 >> 18) & 0x07, ad1 =  w1       & 0x07;
                #define CC_IS_TEX(m)  ((m) == CC_TEXEL0 || (m) == CC_TEXEL1)
                #define CC_IS_TEXC(m) (CC_IS_TEX(m) || (m) == 8u || (m) == 9u)
                g_cc_texel = CC_IS_TEX(ab)  || CC_IS_TEX(bb)  ||
                             CC_IS_TEXC(cb) || CC_IS_TEX(db)  ||
                             CC_IS_TEX(a1)  || CC_IS_TEX(b1)  ||
                             CC_IS_TEXC(c1) || CC_IS_TEX(d1)  ||
                             CC_IS_TEX(aa0) || CC_IS_TEX(ab0) ||
                             CC_IS_TEX(ac0) || CC_IS_TEX(ad0) ||
                             CC_IS_TEX(aa1) || CC_IS_TEX(ab1) ||
                             CC_IS_TEX(ac1) || CC_IS_TEX(ad1);
                #undef CC_IS_TEXC
                #undef CC_IS_TEX
            }
            break;
        }
        case OP_CLRGEOM:                   /* ucode05.txt B6/B7 - tracked, not */
            g_geom_cmds++; g_geom_seen |= w1;           /* applied. See above. */
            g_geom_mode &= ~w1; geomlog(); break;
        case OP_SETGEOM:
            g_geom_cmds++; g_geom_seen |= w1;
            g_geom_mode |=  w1; geomlog(); break;
        case OP_SETCIMG:
            /* ucode05.txt "FF rdp_setcolourimage", the gsSetImage layout (so
             * the width field is width-1, as FD's already is here). */
            g_cimg_cmds++;
            g_cimg_addr = w1;
            g_cimg_w    = (w0 & 0x0fff) + 1;
            if (g_cimg_w >= 64 && g_cimg_w <= 1024 && g_scr_w != g_cimg_w) {
                g_scr_w = g_cimg_w;
                rects_update();                            /* #45: the fit follows the logical size */
            }
            g_cimg_is_z = (g_zimg_addr != 0 &&
                           img_norm(g_cimg_addr) == img_norm(g_zimg_addr));
            break;
        case OP_SETZIMG:
            /* ucode05.txt "FE rdp_setdepthimage" - the address is all that is
             * wanted, to recognise a colour image aimed at the z buffer. */
            g_zimg_cmds++;
            g_zimg_addr = w1;
            g_cimg_is_z = (g_zimg_addr != 0 && g_cimg_addr != 0 &&
                           img_norm(g_cimg_addr) == img_norm(g_zimg_addr));
            break;
        case OP_MOVEMEM:
            /* ucode05.txt "03 rsp_uc05_movemem": type in 0x00FF0000. */
            g_movemem_cmds++;
            g_movemem_types |= 1u << ((((w0 >> 16) & 0xff) >> 1) & 0x1f);
            /* The bitmask above is LOSSY - it folds 0x86/0x87 and 0x88/0x89
             * onto one bit each - so it cannot answer "which movemem types
             * does this path actually send". Keep the exact bytes. */
            {   unsigned t = (w0 >> 16) & 0xffu, i;
                for (i = 0; i < g_mm_n; i++)
                    if (g_mm_type[i] == t) { g_mm_cnt[i]++; break; }
                if (i == g_mm_n && g_mm_n < 16u) {
                    g_mm_type[g_mm_n] = t; g_mm_cnt[g_mm_n] = 1; g_mm_n++;
                }
            }
            /* TEMPORARY PROBE (B-046). Read the viewport the game actually
             * asks for, so "the 3D spills outside the letterbox" can be
             * settled by the Vp rather than argued from the fillrect.
             * Vp is s16 vscale[4] then s16 vtrans[4], 2.2 fixed. */
            /* B-051 STAGE 3. The two reflectance vectors, which never
             * reached this renderer at all - movemem captured the viewport
             * and dropped everything else, so G_TEXTURE_GEN had nothing to
             * generate from even if it had been implemented.
             *
             * gbi.h (the plain-F3D branch this build compiles, :1271-1272):
             * G_MV_LOOKATY = 0x82, G_MV_LOOKATX = 0x84. gSPLookAt (:2738)
             * expands to LookAtX at `la` and LookAtY at `(char *)la + 16`,
             * each sending sizeof(Light) = 16 bytes, and Light_t (:1400)
             * puts `signed char dir[3]` at offset 8. So X carries l[0].dir
             * and Y carries l[1].dir.
             *
             * WHICH vectors those are is settled by the decomp's own SGI
             * source, src/libultra/gu/lookatref.c: guLookAtReflect writes
             * l[0].dir = Right and l[1].dir = Up, both normalised and stored
             * through FTOFRAC8 (so +/-127 is +/-1). The game builds them from
             * the camera basis every frame - bondview2.c:8139 in gameplay,
             * front.c:1964 and :8085 for the boot logos. S is therefore the
             * dot with Right and T the dot with Up. */
            if ((((w0 >> 16) & 0xff) == 0x84) ||
                (((w0 >> 16) & 0xff) == 0x82)) {
                const signed char *L = (const signed char *) dl_operand(w1, 16);
                if (L != NULL) {
                    int which = (((w0 >> 16) & 0xff) == 0x84) ? 0 : 1;
                    g_lookat[which][0] = L[8];
                    g_lookat[which][1] = L[9];
                    g_lookat[which][2] = L[10];
                    g_lookat_seen |= 1u << which;
                    g_lookat_cmds++;
                    g_lookat_default = 0u;   /* a REAL basis has arrived */
                }
            }
            /* B-051 STAGE 4. The lights, which this renderer has been
             * throwing away for as long as it has existed: movemem kept the
             * viewport and, since stage 3, the two lookat vectors, and
             * dropped 0x86-0x94 entirely. gbi.h:1273-1280 gives G_MV_L0=0x86
             * through G_MV_L7=0x94 on a stride of 2, and gSPLight (:2560)
             * sends sizeof(Light)=16 bytes. Light_t (:1398) is col[3] pad
             * colc[3] pad dir[3] pad, so the offsets below are 0, 4 and 8.
             *
             * CAPTURE ONLY unless SL_LIGHT is set. */
            {   unsigned t = (w0 >> 16) & 0xffu;
                if (t >= 0x86u && t <= 0x94u && (t & 1u) == 0u) {
                    unsigned slot = (t - 0x86u) >> 1;
                    const unsigned char *L =
                        (const unsigned char *) dl_operand(w1, 16);
                    if (L != NULL && slot < SL_MAX_LIGHTS) {
                        int j;
                        for (j = 0; j < 3; j++) {
                            g_light[slot].col[j]  = L[j];
                            g_light[slot].colc[j] = L[4 + j];
                            g_light[slot].dir[j]  = (signed char) L[8 + j];
                        }
                        g_light_seen |= 1u << slot;
                        g_light_cmds++;
                        light_note();
                    }
                }
            }
            if ((((w0 >> 16) & 0xff) == 0x80)) {
                const short *vp = (const short *) dl_operand(w1, 16);
                if (vp != NULL) {
                    g_vp_seen = 1;
                    g_vp_sx = vp[0]; g_vp_sy = vp[1];
                    g_vp_tx = vp[4]; g_vp_ty = vp[5];
                    vp_derive((g_vp_tx - g_vp_sx) >> 2, (g_vp_ty - g_vp_sy) >> 2,
                              (g_vp_tx + g_vp_sx) >> 2, (g_vp_ty + g_vp_sy) >> 2);
                    /* A viewport can arrive mid-list; re-arm so the next
                     * primitive picks the new rectangle up. */
                    g_vp_cur = -1;
                }
            }
            break;
        case OP_SETOTHL: {
            /* ucode05_old.txt "B9 rsp_uc05_setothermode_l", cross-checked
             * against gbi.h and against boss.c:588's own assembled words -
             * see the render-mode citation block above. Field-wise write into
             * a persistent register: shift and length name the segment, and
             * the lower word already carries the value pre-shifted. */
            unsigned sft = (w0 >> 8) & 0xffu;
            unsigned len =  w0       & 0xffu;
            unsigned mask;
            g_om_l_cmds++;
            if (len == 0u || len > 32u || sft > 31u || sft + len > 32u) {
                g_om_l_bad++;            /* not a setothermode_l shape */
                break;
            }
            mask = (len == 32u) ? 0xffffffffu : ((1u << len) - 1u) << sft;
            /* MEASURED SEMANTICS for the RENDER-MODE-SHAPED B9 ONLY.
             *
             * gbi.h:3090 gDPSetRenderMode passes shift 3 length 29, and the
             * plain-F3D gSPSetOtherMode (gbi.h:2990 - neither F3DEX_GBI nor
             * F3DEX_GBI_2 is defined for this build) stores w1 UNMASKED. So
             * the data word of that one shape carries a COMPLETE othermode_l
             * value, alpha-compare bits 0-1 included; treating it field-wise
             * discards them. gbi.h:851 RM_AA_PCL_SURF puts G_AC_DITHER (=3)
             * in exactly those bits, and that is the watch's static.
             *
             * NARROW ON PURPOSE. Every other shape keeps the field-wise
             * write, so gDPSetAlphaCompare (shift 0 length 2, gbi.h:3080) and
             * gDPSetDepthSource (shift 2 length 1, gbi.h:3085) still modify
             * only their own field. Do not generalise this to `g_om_l = w1`:
             * that would let an explicit alpha-compare command zero the whole
             * render mode.
             *
             * THE THREE READINGS, AND HOW THE CARTRIDGE CHOSE BETWEEN THEM
             * (measured on a 100s facility watch dwell, /tmp/watchdwell.input;
             * the burst is deterministic - two passes put it at frames
             * 1316..1426 identically):
             *   (a) mask the data field-wise. Refuted: a census of every B9
             *       the game emits over 7300 frames finds exactly two shapes,
             *       and the ONLY word carrying ac=3 is the render-mode word
             *       0050004b. Explicit alpha-compare commands only ever carry
             *       w1=0. Under (a) nothing could ever select dither, yet the
             *       cartridge plainly dithers.
             *   (b) OR the data in unmasked, so bits 0-2 can only be set. Then
             *       dither would persist past the face onto the page-select
             *       rectangles, which draw straight after under G_RM_XLU_SURF
             *       (options.c:1786, ac=0). Refuted on pixels: with a bar mask
             *       taken from a quiet frame, bar-pixel frame-to-frame
             *       difference runs 0.38-0.68x the face's through the burst,
             *       never >=1. The bars are NOT cut out; they are translucent
             *       and the dithered face shows through them attenuated.
             *   (c) this one. The 17 render-mode words carrying ac=0 actively
             *       CLEAR dither, which is what keeps the bars and the quiet
             *       face clean. Consistent with every measurement above.
             *
             * The microcode-level reason is NOT established here - only the
             * behaviour the cartridge exhibits. */
            if (sft == 3u && len == 29u) mask = 0xffffffffu;
            g_om_l = (g_om_l & ~mask) | (w1 & mask);
            break;
        }
        case OP_FULLSYNC:                  /* ucode05.txt: "no options" */
        case OP_TILESYNC:
        case OP_LOADSYNC:
        case OP_PIPESYNC:
            break;
        case 0xC0:                         /* ucode05.txt "C0 rdp_noop" */
            /* #45 FIELD OF VIEW: the game brackets the first-person weapon
             * with two tagged no-ops (bondview2.c maybe_mp_interface, native
             * arm): 'SVM1' opens the VIEWMODEL, which keeps the 60-degree
             * projection widened by the aspect only, 'SVM0' closes it and the
             * world's FOV scale returns. The projection in force is re-applied
             * at each edge; any other no-op is what it always was. */
            if (w1 == 0x53564D31u || w1 == 0x53564D30u) {
                int want = (w1 == 0x53564D31u);
                if (want != g_viewmodel) {
                    g_viewmodel = want;
                    if (g_proj_is_world && g_fov_s != 1.0) {
                        proj_apply_aspect();
                        gl_load_projection();
                    }
                }
            }
            /* The pointer cursor's placement tag (g_cur_ovr): the marker is
             * in w0's low 24 bits, where gDPNoOpTag always leaves zero. */
            else if ((w0 & 0x00FFFFFFu) == 0x00534C43u) {
                g_cur_x4 = (int) (short) ((w1 >> 16) & 0xFFFFu);
                g_cur_y4 = (int) (short) (w1 & 0xFFFFu);
                g_cur_ovr = 1;
                g_cur_tags++;
            }
            break;
        default:
            g_unknown++;
            g_op_hist[op]++;
            break;
        }
        dl += 2;
    }
}

/* ---- SL_PHASE bucket B: time inside sl_gfx_frame_dl --------------------
 *
 * BOOT PROFILING PROBE, off unless SL_PHASE is set to something other than
 * "0". It accumulates into two doubles - it prints nothing and writes no
 * file, because a probe that logs per frame becomes the cost it is trying to
 * measure. The owning report lives in sl_ultra_shim.c and drains these once
 * per presented frame. See B-094 in docs/backlog.md.
 *
 * Uses the timer that already exists (clock_gettime(CLOCK_MONOTONIC)); this
 * adds no timing subsystem. Two reads per display list when armed, zero when
 * not - the gate is a cached getenv, so a normal run pays one branch.
 */
static int sl_phase_dl_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_PHASE"); on = (v != NULL && *v != '0'); }
    return on;
}

static double sl_phase_dl_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

static double g_phase_dl_t0;     /* start of the current call */
static double g_phase_dl_s;      /* whole call */
static double g_phase_walk_s;    /* the command walk alone */

/* Drain-and-zero, so the shim charges each presented frame only its own
 * lists. Called once per frame; a frame that issued no list reads zeros,
 * which is a real answer rather than a stale one. */
void sl_phase_dl_take(double *dl_s, double *walk_s)
{
    *dl_s = g_phase_dl_s;     g_phase_dl_s = 0.0;
    *walk_s = g_phase_walk_s; g_phase_walk_s = 0.0;
}

/* ================= B-101 WITNESS: does the scissor remove anything? =======
 *
 * Written when `ED setscissor` was decoded into g_sciss[] and applied to
 * nothing (a tree-wide `grep -rn "glScissor\|GL_SCISSOR" src/` came back
 * EMPTY on 2026-09-08). An ignored scissor can only ever make this renderer
 * draw MORE than the RDP would, never less, so switching it on would very
 * likely look fine - and looking fine proves nothing about whether it was
 * needed. What proves something is a frame where room geometry is VISIBLY
 * outside its portal box and the box would have removed it. B-143 (2026-09-17)
 * found that frame at the owner's Dam mark and the scissor is now applied on
 * every frame; the witness survives as the instrument that shows what it
 * removes, with pass A of a witness frame walking unscissored (g_wit_a).
 *
 * SL_SCISS_WITNESS=<read index> renders one frame TWICE, from the SAME
 * display list, in the SAME process, at the SAME instant of game time: once
 * as the build normally does, once with every `ED` honoured through
 * glScissor. It writes both images and their difference.
 *
 * WHY IN ONE FRAME RATHER THAN TWO BUILDS. The obvious instrument is two
 * builds captured at the same moment, and it does not work here. The
 * windowed launch is REAL-TIME PACED (src/platform/sl_ultra_shim.c:424), so
 * two runs of one level with one seed diverge in read index from frame 5
 * onward; docs/backlog.md records that whole-frame pixel counts then differ
 * even on a level the change provably cannot touch, which would have
 * "confirmed" a regression that could not exist. Two walks of one list have
 * no such freedom. Every input to the second walk is bit-identical to the
 * first, so every differing pixel IS the scissor and nothing else.
 *
 * The window shows the SCISSORED image on a witness frame, because that is
 * the pass that finished last. That is deliberate - SL_SHOT on the same
 * frame therefore captures pass B - and it is why the witness fires once.
 */
static int wit_index(void)
{
    static int idx = -2;
    if (idx == -2) {
        const char *v = getenv("SL_SCISS_WITNESS");
        idx = (v != NULL && *v != '\0') ? atoi(v) : -1;
    }
    return idx;
}

/* SL_SCISS_WITNESS_EVERY: sample every N read indices instead of firing once,
 * so a level can be SWEPT rather than guessed at. A single hand-picked frame
 * is how a null result gets mistaken for a clean one. */
static int wit_every(void)
{
    static int n = -1;
    if (n < 0) { const char *v = getenv("SL_SCISS_WITNESS_EVERY");
                 n = (v != NULL && *v != '\0') ? atoi(v) : 0;
                 if (n < 0) n = 0; }
    return n;
}

/* Whether the witness fires on THIS frame. Hoisted out of the witness block
 * (B-143) because the answer is now needed BEFORE the frame's first walk:
 * pass A of a witness frame has to skip the production scissor, and the
 * decision that a frame is a witness frame must be the same one the block at
 * the end of sl_gfx_frame_dl makes. The statics are written only there. */
static int g_wit_fired, g_wit_fired_at;
static int wit_due(int now)
{
    if (wit_index() < 0) return 0;
    return !g_wit_fired ? (now >= wit_index())
                        : (wit_every() > 0 && now - g_wit_fired_at >= wit_every());
}

/* SL_SCISS_WITNESS_MIN: write images only when the INTERIOR difference (see
 * below) reaches this many pixels. Default 1 - any interior difference at all
 * is worth looking at, and looking at it is the point. */
static int wit_min(void)
{
    static int n = -1;
    if (n < 0) { const char *v = getenv("SL_SCISS_WITNESS_MIN");
                 n = (v != NULL && *v != '\0') ? atoi(v) : 1;
                 if (n < 1) n = 1; }
    return n;
}


/* PPM by hand, exactly like sdl_shot and for the same reason: sprintf here is
 * NOT libc's (src/sprintf.c defines the symbol that links) and produces
 * nothing natively, so a formatted path silently becomes "" and fopen fails
 * with no file and no error. fprintf is unaffected. */
static void wit_ppm(const char *tag, int w, int h, const unsigned char *px)
{
    char path[256];
    size_t n = 0;
    const char *s, *pre = getenv("SL_SCISS_WITNESS_OUT");
    FILE *f;
    int y;
    if (pre == NULL || *pre == '\0') pre = "sciss-witness";
    for (s = pre; *s != '\0' && n < sizeof path - 32; s++) path[n++] = *s;
    path[n++] = '-';
    for (s = tag; *s != '\0' && n < sizeof path - 8; s++) path[n++] = *s;
    path[n++] = '.'; path[n++] = 'p'; path[n++] = 'p'; path[n++] = 'm';
    path[n] = '\0';
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "sl_sciss: WITNESS could not open %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = h - 1; y >= 0; y--)           /* GL bottom-left -> PPM top-left */
        fwrite(px + (size_t) y * (size_t) w * 3, 1, (size_t) w * 3, f);
    fclose(f);
    fprintf(stderr, "sl_sciss: WITNESS wrote %s (%dx%d)\n", path, w, h);
}

void sl_gfx_frame_dl(const void *first, const void *end)
{
    static unsigned frames;

    /* B-101. The witness re-walk is the SAME frame walked a second time, not
     * a new one: it must neither advance the frame counter nor reseed the
     * dither, or the two images would differ in a screen-wide noise pattern
     * that has nothing to do with the scissor. The mask CACHE is still
     * dropped, because pass one entered with it dropped too. */
    if (!g_wit_pass) {
        g_dl_frame++;
        sl_texcov_frame_begin();   /* #47 PART A, per-frame mode */

        /* A new seed every frame is what makes the dither MOVE rather than sit
         * there as a fixed screen pattern; the cached density is dropped with it
         * so the mask is rebuilt. */
        g_dith_seed = g_dl_frame + 1u;
    }
    g_dith_a = -1;

    /* Headless does NO rendering work and prints nothing: trace replay runs
     * this path constantly. */
    if (!sl_gfx_active()) return;
    if (first == NULL || end == NULL || end <= first) return;

    /* THE INTERPRETER-RAN COUNTER, and it is not a statistic.
     *
     * Every census in this file can report "none", and "none" is exactly what
     * a probe prints when it never ran: sl_gfx_frame_dl returns above this
     * line whenever there is no window, so a headless run - bootsweep is one,
     * and so is any trace replay - executes ZERO display-list commands while
     * looking like a healthy, successful run. Three separate instruments this
     * session could not tell "measured nothing" from "did not run", and each
     * time the empty output read as a conclusive negative.
     *
     * So the reports below REFUSE to print a census unless this counter is
     * non-zero, and print why instead. A null result is only evidence when
     * the instrument can show it was switched on. */
    g_dl_ran++;

    /* B-101. The two things this function deliberately does NOT reset per
     * frame - the game's viewport rectangle and the framebuffer size - are
     * exactly the two the witness re-walk has to be handed as pass one found
     * them, or the second walk is a walk of a different frame. Saved here,
     * before the resets, and restored at the recursion below. Four words on
     * every frame; the alternative is a second copy of the reset contract
     * that would go stale the first time this one changed. */
    if (!g_wit_pass) {
        memcpy(g_wit_vp_rect, g_vp_rect, sizeof g_wit_vp_rect);
        g_wit_vp_have = g_vp_have;
        g_wit_scr_w = g_scr_w;
        g_wit_scr_h = g_scr_h;
    }

    if (sl_phase_dl_on()) g_phase_dl_t0 = sl_phase_dl_now();

#ifdef _WIN32
    /* B-072. Address-space facts are remembered for one list and no longer.
     * Here, with the rest of the per-task state, for the same reason: the
     * region tree is external state this walk observes, not state it owns. */
    vq_cache_reset();
#endif
    memset(g_seg, 0, sizeof g_seg);
    memset(g_op_hist, 0, sizeof g_op_hist);
    memset(g_depth_cmds, 0, sizeof g_depth_cmds);
    g_end_stops = g_guard_stops = g_zero_runs = 0;
    { static int once; if (!once) { once = 1;
        sl_dl_branch_dbg = getenv("SL_DL_BRANCH_DBG") != NULL; } }
    g_list_native = g_list_swapped = 0;
    g_dl_end = (const unsigned int *) end;
    g_tris = g_verts = g_cmds = g_unknown = 0;
    g_vtx_reject = 0;
    g_vtx_v0nz = 0;
    g_vtx_unres = 0;
    g_mtx_cmds = g_mtx_proj = g_mtx_mv = g_mtx_load = g_mtx_mul = 0;
    g_mtx_push = g_mtx_pop = g_mtx_bad = 0;
    g_min[0] = g_min[1] = g_min[2] =  1e9f;
    g_max[0] = g_max[1] = g_max[2] = -1e9f;
    g_on = g_off = 0;
    g_ndc_min[0] = g_ndc_min[1] = g_ndc_min[2] =  1e9f;
    g_ndc_max[0] = g_ndc_max[1] = g_ndc_max[2] = -1e9f;

    /* The RSP starts each task with an empty matrix stack; so do we. Until the
     * list loads its own, both matrices are identity - which draws nothing,
     * and that is the honest result rather than a fitted box hiding it. */
    g_mv_sp = 0;
    memcpy(g_mv[0], g_identity, sizeof g_identity);
    memcpy(g_proj, g_identity, sizeof g_identity);
    memcpy(g_proj_game, g_identity, sizeof g_identity);
    g_proj_is_world = 0;
    g_viewmodel = 0;

    /* RDP texture state is per-task, exactly like the matrix stack: the
     * cache of decoded images persists, the parse state does not. */
    memset(g_tile, 0, sizeof g_tile);
    memset(g_tmem, 0, sizeof g_tmem);
    g_tmem_n = 0; g_tmem_hit = g_tmem_miss = 0;
    g_ti_fmt = g_ti_siz = g_ti_w = 0; g_ti_addr = NULL;
    g_tlut_addr = NULL; g_tlut_mode = 0;
    g_tlut_n = g_tlut_off = 0;
    g_tlut_cmds = g_tlut_bad = g_tlut_off0 = 0;
    /* B-051 WORK ITEM 4. The probed draw's ST range, per frame, in the same
     * place as work item 3's report so the two line up in one log. Printed in
     * texels as well as raw units: the raw value is what the vertex carries,
     * the texel value is what is looked up, and confusing them is how a shift
     * goes unnoticed. */
    if (texprobe_on() && g_tp_stn != 0) {
        extern unsigned sl_record_index(void);
        fprintf(stderr, "sl_texprobe_st: read=%u verts=%u"
                        " s[%ld,%ld] t[%ld,%ld] raw"
                        "  ->  s[%.2f,%.2f] t[%.2f,%.2f] texels"
                        "  tile0 decoded %ux%u\n",
                sl_record_index(), g_tp_stn,
                g_tp_smin, g_tp_smax, g_tp_tmin, g_tp_tmax,
                (double) g_tp_smin / (double) SL_ST_SCALE,
                (double) g_tp_smax / (double) SL_ST_SCALE,
                (double) g_tp_tmin / (double) SL_ST_SCALE,
                (double) g_tp_tmax / (double) SL_ST_SCALE,
                g_st_tex_w, g_st_tex_h);
    }
    g_tp_stn = 0;
    /* B-051 WORK ITEM 3. The frame just finished, before anything is reset. */
    if (aeq_rgb_dbg() && g_aeqrgb_n != 0) {
        extern unsigned sl_frames_completed(void);
        extern unsigned sl_record_index(void);
        int k;
        {
            /* The RDP's own equation for this word, at the texel alpha the
             * probe measured and SHADE.a=255 (measured on these draws):
             *     alpha = clamp(TEXEL.a * SHADE.a / 255 + PRIM.a)
             * PRIM.a is Rare's calculatedopacity. This is the RENDERED curve
             * to set against the game-side one. */
            int tex = g_aeqrgb_amax >= 0 ? g_aeqrgb_amax : 255;
            int lo = tex + (g_aeqrgb_plo <= 255 ? g_aeqrgb_plo : 0);
            int hi = tex + (g_aeqrgb_phi >= 0 ? g_aeqrgb_phi : 0);
            if (lo > 255) lo = 255;
            if (hi > 255) hi = 255;
            int k;
            (void) lo; (void) hi;
            fprintf(stderr, "sl_glassalpha: read=%u prim(opacity) distinct={",
                    sl_record_index());
            for (k = 0; k < g_aeqrgb_pset_n; k++)
                fprintf(stderr, "%s%d", k ? "," : "",
                        (int) g_aeqrgb_pset[k]);
            fprintf(stderr, "}%s texelA<=%d\n",
                    g_aeqrgb_pset_over ? "+more" : "", tex);
        }
        fprintf(stderr, "sl_aeqrgb: read=%u frame=%u verts=%u"
                        " vertex rgb r[%d,%d] g[%d,%d] b[%d,%d]"
                        " texture rgbmax<=%d,%d,%d rgbmean<=%d,%d,%d"
                        " alpha[%d,%d]\n",
                sl_record_index(), sl_frames_completed(), g_aeqrgb_n,
                g_aeqrgb_vlo[0], g_aeqrgb_vhi[0],
                g_aeqrgb_vlo[1], g_aeqrgb_vhi[1],
                g_aeqrgb_vlo[2], g_aeqrgb_vhi[2],
                g_aeqrgb_tmax[0], g_aeqrgb_tmax[1], g_aeqrgb_tmax[2],
                g_aeqrgb_tmean[0], g_aeqrgb_tmean[1], g_aeqrgb_tmean[2],
                g_aeqrgb_amin, g_aeqrgb_amax);
        for (k = 0; k < 3; k++) {
            g_aeqrgb_vlo[k] = 256; g_aeqrgb_vhi[k] = -1;
            g_aeqrgb_tmax[k] = -1; g_aeqrgb_tmean[k] = -1;
        }
        g_aeqrgb_amin = g_aeqrgb_amax = -1;
        g_aeqrgb_plo = 256; g_aeqrgb_phi = -1;
        g_aeqrgb_pset_n = 0; g_aeqrgb_pset_over = 0;
        g_aeqrgb_n = 0; g_aeqrgb_notex = 0;
    }
    g_tex_on = g_tex_tile = 0;
    g_tex_hits = g_tex_uploads = g_tex_fail = g_tex_binds = 0;
    g_tex_enhanced = 0;
    g_plain_under_prog = g_plain_prog_reset = 0;      /* B-142, per frame */
    g_st_max_abs = 0; g_st_tex_w = g_st_tex_h = 0;
    g_st_in = g_st_out = 0; g_load_src = NULL; g_loadblocks = 0;
    g_load_dxt = 0; g_load_dxt0 = g_load_dxtn = 0; g_load_texels = 0;
    g_dxtn_n = 0;
    g_strd_n = g_strd_bad = 0;
    g_noisy_n = 0;
    g_key_src = NULL; g_key_need = 0;
    g_tri_tex = g_tri_plain = g_tex_on_tris = g_tex_names_n = g_tex_nosrc = 0;
    memset(g_tex_reject, 0, sizeof g_tex_reject);
    g_sciss[0] = g_sciss[1] = g_sciss[2] = g_sciss[3] = 0;
    g_scr_h_adopted = 0;    /* the next scissor is this frame's first */
    /* B-143. Is this the unscissored pass A of a witness frame? Decided here,
     * before the walk, by the same test the witness block makes after it. */
    if (!g_wit_pass) {
        extern unsigned sl_record_index(void);
        g_wit_a = wit_due((int) sl_record_index());
        g_sciss_applied = 0;
        if (g_sciss_empty) g_sciss_frames_empty++;
        g_sciss_empty = 0;
    }
    /* B-046. begin_frame has just set the GL viewport back to the whole
     * window (sl_gfx_sdl.c:62), so this is where it can be read without
     * catching a viewport this walker set itself. g_vp_rect is NOT reset -
     * the viewport is task state like g_scr_w, and a list that issues no
     * movemem of its own keeps the one in force. */
    glGetIntegerv(GL_VIEWPORT, g_window_vp);
    /* #45: the content and safe rects for this frame, from the window just
     * read, the logical size in force and the selected aspect. */
    rects_update();
    bars_clear();
    g_vp_cur = -1;
    g_vp_applies = 0;
    g_nw_front = g_nw_straddle = g_nw_inside = g_nw_behind = 0;
    g_nw_area_inside = g_nw_area_straddle = 0.0; g_nw_worst = 0.0f;
    g_tri_degen = g_tri_area = g_tri_clipped = 0;
    g_tri_area_sum = 0.0; g_tri_area_max = 0.0f;

    /* The RDP's colour registers, combiner and image pointers are task state
     * exactly like the tile table, so they reset with it. g_scr_w/g_scr_h are
     * NOT reset here - they describe the framebuffer, and a list that
     * redeclares neither keeps the one in force. They are not constant for
     * the run, though: the game changes VI size in both directions, and the
     * height follows it at the frame's FIRST scissor instead (see OP_SETSCISS).
     * Re-deriving them anywhere later in a list would move the 2D ortho under
     * rectangles already drawn. */
    memset(g_prim, 0, sizeof g_prim);   memset(g_env,   0, sizeof g_env);
    memset(g_fog,  0, sizeof g_fog);    memset(g_blend, 0, sizeof g_blend);
    memset(g_fill, 0, sizeof g_fill);
    memset(g_col_cmds, 0, sizeof g_col_cmds);
    g_prim_lod_min = g_prim_lod_level = 0; g_fill_is16 = 0;
    g_cc_rgb_const = -1; g_cc_a_prim = g_cc_a_texel = 0;
    g_cc_a_envscale = 0; g_cc_a_envsrc = 0; g_cc_a_prim1 = 0;
    gp_reset();
    g_cc_texel = 1; g_cc_notex_tris = 0; g_cc_notex_n = 0;
    g_lodlerp_tile_tris = 0;             /* B-144 */
    g_ccx_n = 0;
    g_cc_tint_tris = g_cc_tint_rects = g_fr_tintskip = 0;
    g_cc_lerp_rects = 0;
    memset(g_cc_notex_w, 0, sizeof g_cc_notex_w);
    g_fd_tris = g_fd_ccw = g_fd_cw = g_fd_zero = 0;
    memset(g_fd_cull, 0, sizeof g_fd_cull);
    g_fd_om_used = 0; g_fd_geom_seen = 0;
    memset(g_fd_edge_used, 0, sizeof g_fd_edge_used);
    memset(g_fd_tri_used, 0, sizeof g_fd_tri_used);
    g_fd_tri_dup = 0;
    g_fd_e_ok = g_fd_e_bad = g_fd_e_new = g_fd_e_full = 0;
    memset(g_fd_bad_slot, 0, sizeof g_fd_bad_slot);
    memset(g_fd_slot_tris, 0, sizeof g_fd_slot_tris);
    memset(g_vsrc, 0, sizeof g_vsrc);
    g_cc_w0 = g_cc_w1 = g_cc_cmds = 0;
    g_geom_mode = g_geom_cmds = g_geom_seen = 0;
    memset(g_cull_tris, 0, sizeof g_cull_tris);
    memset(g_cull_big, 0, sizeof g_cull_big);
    g_cull_n_ccw = g_cull_n_cw = 0;
    g_cull_z_ccw = g_cull_z_cw = 0.0;
    g_nearcull_dropped = 0;
    g_geomlog_n = g_geomlog_lost = 0;
    g_edge_ok = g_edge_bad = g_edge_none = 0;
    memset(g_edge_seen, 0, sizeof g_edge_seen);
    g_cimg_addr = g_zimg_addr = g_cimg_w = 0;
    g_cimg_cmds = g_zimg_cmds = 0; g_cimg_is_z = 0;
    g_movemem_types = g_movemem_cmds = 0;
    g_tr_pending = g_tr_have_st = g_tr_flip = 0;
    g_tr_cmds = g_tr_drawn = g_tr_textured = g_tr_flips = 0;
    g_tr_offscreen = g_tr_abandoned = g_half_orphan = 0;
    g_tr_order_w0lr = g_tr_order_w0ul = g_tr_inverted = 0;
    /* A command half-assembled when the list ended is discarded rather than
     * carried: the next frame's words would complete it with the wrong ones. */
    g_rh_n = 0;
    g_rh_tris = g_rh_drawn = g_rh_dropped = g_rh_notri = g_rh_overflow = 0;
    g_rh_skylerp = 0;
    memset(g_tr_reject, 0, sizeof g_tr_reject);
    g_fr_cmds = g_fr_drawn = g_fr_zskip = g_fr_degen = 0;
    g_fr_extended = g_rh_extended = 0;
    g_cur_ovr = 0; g_cur_applied = g_cur_tags = 0;   /* the cursor placement tag never spans frames */
    g_fr_filled = g_fr_combined = 0;
    g_fogp_cmds = 0; g_fog_geom_tris = g_fog_p_tris = 0;
    g_tri_id = 0; g_room_dl = 0;
    g_fog_a_shade = g_fog_a_fog = 0; g_fog_have_at = 0;
    g_cycle_type = 0;
    g_textlod = 0;                       /* B-144: G_TL_TILE until a BA says LOD */
    g_om_l = RM_SEED;
    g_om_l_cmds = g_om_l_bad = g_rm_pre_tris = 0;
    g_rm_hist_used = g_rm_hist_lost = 0;
    g_rm_blend_tris = g_rm_decal_tris = g_rm_nozwrite_tris = 0;
    g_2d_pre_geom_tr = g_2d_post_geom_tr = 0;
    g_2d_pre_geom_fr = g_2d_post_geom_fr = 0;
    g_2d_post_geom_cover = 0.0;
    g_2d_big_area = 0.0f; g_2d_big_is_fill = 0;
    memset(g_2d_big_col, 0, 4); memset(g_2d_cur_col, 0, 4);
    g_2d_big[0] = g_2d_big[1] = g_2d_big[2] = g_2d_big[3] = 0.0f;
    g_2d_cover = 0.0; g_2d_enters = 0;
    g_2d_bbox[0] = g_2d_bbox[1] =  1e9f;
    g_2d_bbox[2] = g_2d_bbox[3] = -1e9f;

    glDisable(GL_TEXTURE_2D);
    g_tex_gl_on = 0;
    g_tex_gl_bound = 0;
    /* B-048. Unit 1 is re-established per draw; force it off and leave the
     * active unit at 0 so a frame always starts from the same state. */
    g_tex1_gl_on = 1; tex1_off();
    g_tex1_tris = g_tex1_binds = g_tex1_fail = 0;
    g_lodmip_same = g_tmem_inner = 0;
    g_tree_zupd_skip = 0;
    /* B-045. Fog is NOT forced off here any more - on this driver the first
     * primitive after glEnable(GL_FOG) rasterises unfogged (measured on the
     * Runway plane-key door, whose face quad is the frame's first fogged
     * geometry and rendered two-tone every frame), so the enable is kept
     * across frames once it has happened and the fogless paths pin the
     * current fog coordinate to zero instead - see fog_neutral. The
     * g_fog_gl_on / g_fog_gl_col caches stay untouched too, so they remain
     * truthful about the GL state they mirror. */
    fog_neutral();
    g_fog_tris_shade = g_fog_tris_const = 0;
    g_fog_enables = g_fog_disables = g_fog_colchg = 0;
    memset(g_fog_hist, 0, sizeof g_fog_hist);
    memset(g_fog_hist_s, 0, sizeof g_fog_hist_s);
    memset(g_fog_hist_c, 0, sizeof g_fog_hist_c);
    g_fog_alpha_used = 0;
    g_tex1_shiftbig = 0; g_tex1_tile = 0;
    g_tex1_w = g_tex1_h = 0;
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    /* B-122. EQUAL DEPTH PASSES; THE LATER COPLANAR DRAW WINS. Surface's
     * dome (room 7, DL 20017050) draws the same quads TWICE - a dark-shaded
     * pass at +0x110 and a white-shaded pass at +0x1c8, identical state,
     * identical plane, opposite triangulation diagonals - and the cartridge
     * shows the SECOND pass: a mostly white dome, dark only where the white
     * pass has no geometry (the observatory slit). Under GL's default
     * GL_LESS an equal-depth redraw is REJECTED, so the winner per half-quad
     * fell to sub-ulp interpolation differences between the two diagonals -
     * the checkered dome. GL_LEQUAL is the minimal semantic that reproduces
     * the cartridge outcome (the corpus is silent on the RDP compare's
     * equality/dzpix tolerance - doc-routing.json not_covered, 2026-09-12;
     * this rests on the measured double-draw and the ROM oracle).
     * SL_Z_LEQUAL=0 restores GL_LESS for A/B. */
    glDepthFunc(zfunc_lequal_on() ? GL_LEQUAL : GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    /* B-125. The redraw path starts each frame from a known plane: stencil
     * off, its per-submission set empty, and the bits this frame's bound
     * framebuffer actually has (a request the driver ignored was B-123's
     * whole failure; asking GL each frame is what makes it impossible here). */
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    {   GLint zb = 0, sb = 0;
        glGetIntegerv(GL_DEPTH_BITS, &zb);
        glGetIntegerv(GL_STENCIL_BITS, &sb);
        g_rd_depth_bits = (int) zb; g_rd_stencil_bits = (int) sb;
    }
    rd_scope_reset();
    g_rd_loads = g_rd_loads_old = g_rd_tris = g_rd_tris_z = g_rd_tris_same = 0;
    g_rd_cur = 0; g_rd_tol = 0.0f;
    rm_invalidate();
    glDisable(GL_CULL_FACE);
    g_cull_gl = 0;
    g_2d_on = 0;                         /* GL state is re-established below */
    /* Order matters, and getting it wrong leaked a world shader into the HUD.
     *
     * texenv_set() unbinds the B-051 alpha program only when the mode it is
     * LEAVING is 5 - that is the one fact it has to decide whether
     * glUseProgram(0) is owed. Assigning g_texenv = -1 first destroyed that
     * fact, so a frame ending in mode 5 left the program bound in GL while
     * every later texenv_set() quietly programmed fixed-function state the
     * still-bound shader overrode. draw_texrect then rasterised HUD text and
     * wall decals through a world shader: black boxes around glyphs, black
     * squares around bullet decals. Once desynced it re-desynced nearly every
     * frame - 40936 of 68161 texrects in one measured run.
     *
     * RNG exposed it and did not cause it: the seed changes which draw is
     * last before the boundary, i.e. whether mode 5 is the mode being left.
     * The historical constant seed happens never to end a frame in mode 5,
     * which is why this survived every test until boot seeds began to vary.
     *
     * The transferable rule: NEVER invalidate tracked state before using it
     * to restore the external state it describes. */
    texenv_set(0);   /* truthful teardown, while g_texenv still describes GL */
    g_texenv = -1;   /* only now forget it, to force a full re-issue */
    texenv_set(0);   /* re-establish the complete fixed-function texenv */
    gl_load_projection();
    gl_load_modelview();

    /* LIVE OWNER BUG MARK. Arm HERE, at the top of the one list that will be
     * walked, so the capture and the framebuffer the backend reads back a few
     * lines later in sl_gfx_end_frame belong to the SAME frame. A request
     * raised mid-frame by the input layer is taken by the next list, never by
     * a list already half-walked.
     *
     * The witness re-walk (B-101) is the same frame twice; arming on it would
     * capture the second pass's triangles under the first pass's screenshot,
     * so it is excluded by construction. */
    g_dl_depth = 0;
    g_dl_addr[0] = 0u;
    if (g_mark_at == 0u) {              /* #25 witness: SL_MARK_AT, once */
        const char *v = getenv("SL_MARK_AT");
        g_mark_at = (v != NULL && *v != '\0') ? (unsigned) atoi(v) : 0xffffffffu;
        if (g_mark_at == 0u) g_mark_at = 0xffffffffu;
    }
    if (g_mark_at == g_dl_frame && !g_wit_pass) g_mark_req = 1;
    if (g_mark_req && !g_wit_pass) {
        g_mark_req = 0;
        g_mark_on = 1;
        g_mark_n = 0; g_mark_hit = 0; g_mark_tris = 0; g_mark_dl_n = 0;
        g_mark_sub_n = 0; g_mark_sub_cur = -1; g_mark_sub_ord = 0;
        g_mark_sub_lost = 0;
        g_mark_calls = g_mark_branches = 0;
        g_mark_call_max = g_mark_wdepth_max = 0;
        g_mark_tris_attr = g_mark_tris_lost = 0;
        g_mark_frame = g_dl_frame;
        /* THE ROOT. The frame list is itself a submission - the one the task
         * handed the interpreter - and giving it a node is what makes "every
         * triangle belongs to exactly one node" true. Without it, geometry
         * emitted by the frame list directly, outside any 06, would have no
         * home and the emitted/attributed identity could never close. */
        g_mark_sub[0].addr   = (unsigned) (unsigned long) first;
        g_mark_sub[0].host   = (unsigned) (unsigned long) first;
        g_mark_sub[0].off    = 0u;
        g_mark_sub[0].tris   = 0;
        g_mark_sub[0].centre = 0;
        g_mark_sub[0].parent = -1;
        g_mark_sub[0].prev   = -1;
        g_mark_sub[0].ord    = 0;
        g_mark_sub[0].call   = 0;
        g_mark_sub[0].wdepth = 0;
        g_mark_sub[0].kind   = 0;
        g_mark_sub_n = 1;
        g_mark_sub_cur = 0;
    }
    if (sl_phase_dl_on()) {
        double w0 = sl_phase_dl_now();
        walk(first, 0, list_swapped((const unsigned int *) first));
        g_phase_walk_s += sl_phase_dl_now() - w0;
    } else {
        walk(first, 0, list_swapped((const unsigned int *) first));
    }
    if (g_mark_on) { g_mark_on = 0; g_mark_sub_cur = -1; g_mark_have = 1; }
    {   /* SL_DEPTH_DBG; read once, so the per-frame cost is a load */
        static int on = -1;
        if (on < 0) on = (getenv("SL_DEPTH_DBG") != NULL);
        if (on)
            /* tris is here because the probe's whole question is whether the
             * cap is inert. "branch=8 maxdepth=9 refused=30" and "branch=8
             * maxdepth=1 refused=0" are the same frame under the two branch
             * models, and only the triangle count says which one drew it. */
            fprintf(stderr, "sl_depth: frame %u call=%u branch=%u maxdepth=%u"
                            " refused=%u tris=%u\n", g_dl_frame, sl_probe_call,
                    sl_probe_branch, sl_probe_maxdepth, sl_probe_refused,
                    g_tris);
        sl_probe_call = sl_probe_branch = 0;
        sl_probe_refused = sl_probe_maxdepth = 0;
    }
    batch_end();
    mode2d_end();
    tex1_off();                          /* B-048 */
    fog_neutral();                       /* B-045 - see fog_neutral */
    if (g_tex_gl_on) { glDisable(GL_TEXTURE_2D); g_tex_gl_on = 0; }
    /* Hand the depth buffer back writable. The window backend's
     * glClear(GL_DEPTH_BUFFER_BIT) runs at the START of the next frame, and a
     * masked depth buffer swallows the clear silently - so a single
     * translucent triangle at the end of this list would otherwise poison
     * every frame after it. */
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    rm_invalidate();
    /* B-143. The scissor is list state, not window state: the last ED of a
     * frame must not survive into the backend's clear at the top of the next
     * one (sdl_begin - glClear obeys the scissor test) or into a mark's
     * read-back. The witness pass B disables it the same way after its own
     * walk. */
    glDisable(GL_SCISSOR_TEST);

    /* B-045 probe report. Per frame, so the output IS the gas window: the
     * environment the gas lerps to changes the FILL colour (sky.c:326 draws
     * the backdrop with it) and the FOG colour together, and pulls the far
     * clip plane in - all three are read back here from what the list said. */
    {
        static int on = -1;
        if (on < 0) on = (getenv("SL_FOG_DBG") != NULL);
        if (on) {
            fprintf(stderr, "sl_fog: f%u fill=%02x%02x%02x fogcol=%02x%02x%02x%02x"
                            " fm=%d fo=%d cmds=%u near=%.1f far=%.1f"
                            " proj[10,11,14,15]=%.4f,%.4f,%.1f,%.4f"
                            " fog-geom=%u clr-fog=%u a_shade=%u a_fog=%u"
                            " drawn shade=%u const=%u breaks e/d/c=%u/%u/%u fog=%s"
                            " src=%04x coord[0..9]=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                    frames, g_fill[0], g_fill[1], g_fill[2],
                    g_fog_col_at[0], g_fog_col_at[1], g_fog_col_at[2],
                    g_fog_col_at[3],
                    g_fogp_fm, g_fogp_fo, g_fogp_cmds,
                    g_fog_near_at, g_fog_far_at,
                    g_fog_proj_at[0], g_fog_proj_at[1], g_fog_proj_at[2],
                    g_fog_proj_at[3],
                    g_fog_geom_tris, g_fog_p_tris, g_fog_a_shade,
                    g_fog_a_fog, g_fog_tris_shade, g_fog_tris_const,
                    g_fog_enables, g_fog_disables, g_fog_colchg,
#ifdef SL_FOGSTAGE
                    fog_option() ? (fog_prim_option() ? "on+prim" : "on")
                                 : "off"
#else
                    "unavailable"
#endif
                    , g_fog_src_readback,
                    g_fog_hist[0], g_fog_hist[1], g_fog_hist[2], g_fog_hist[3],
                    g_fog_hist[4], g_fog_hist[5], g_fog_hist[6], g_fog_hist[7],
                    g_fog_hist[8], g_fog_hist[9]);
            fprintf(stderr, "sl_fog:    shade[0..9]=%u,%u,%u,%u,%u,%u,%u,%u,"
                            "%u,%u  const[0..9]=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                    g_fog_hist_s[0], g_fog_hist_s[1], g_fog_hist_s[2],
                    g_fog_hist_s[3], g_fog_hist_s[4], g_fog_hist_s[5],
                    g_fog_hist_s[6], g_fog_hist_s[7], g_fog_hist_s[8],
                    g_fog_hist_s[9],
                    g_fog_hist_c[0], g_fog_hist_c[1], g_fog_hist_c[2],
                    g_fog_hist_c[3], g_fog_hist_c[4], g_fog_hist_c[5],
                    g_fog_hist_c[6], g_fog_hist_c[7], g_fog_hist_c[8],
                    g_fog_hist_c[9]);
            {   unsigned i;
                fprintf(stderr, "sl_fog:    A_FOG alphas:");
                for (i = 0; i < g_fog_alpha_used; i++)
                    fprintf(stderr, " %02x x%u", g_fog_alpha_seen[i],
                            g_fog_alpha_n[i]);
                fprintf(stderr, "\n");
            }
        }
    }

    /* B-043 probe report. Per frame, and only on frames that actually drew
     * faded-character geometry, so the output IS the event window. */
    {
        if (fd_dbg() && g_fd_tris != 0) {
            unsigned i;
            fprintf(stderr, "sl_fade: frame %u tris=%u ccw=%u cw=%u zero=%u"
                            "  cull[none,f,b,fb]=%u,%u,%u,%u geom_seen=%08x\n",
                    frames, g_fd_tris, g_fd_ccw, g_fd_cw, g_fd_zero,
                    g_fd_cull[0], g_fd_cull[1], g_fd_cull[2], g_fd_cull[3],
                    g_fd_geom_seen);
            fprintf(stderr, "sl_fade:   mesh-edges paired=%u SAME-WAY-TWICE=%u"
                            " once=%u tableful=%u repeated-triangles=%u"
                            " bad-by-slot[t4:0,1,2,3|bf]=%u,%u,%u,%u|%u"
                            " tris-by-slot=%u,%u,%u,%u|%u\n",
                    g_fd_e_ok, g_fd_e_bad, g_fd_e_new, g_fd_e_full,
                    g_fd_tri_dup, g_fd_bad_slot[0], g_fd_bad_slot[1],
                    g_fd_bad_slot[2], g_fd_bad_slot[3], g_fd_bad_slot[4],
                    g_fd_slot_tris[0], g_fd_slot_tris[1], g_fd_slot_tris[2],
                    g_fd_slot_tris[3], g_fd_slot_tris[4]);
            for (i = 0; i < g_fd_om_used; i++)
                fprintf(stderr, "sl_fade:   om=%08x tris=%-6u zcmp=%d zupd=%d"
                                " zmode=%u forcebl=%d\n",
                        g_fd_om[i], g_fd_om_n[i],
                        (g_fd_om[i] & RM_Z_CMP) != 0,
                        (g_fd_om[i] & RM_Z_UPD) != 0,
                        (g_fd_om[i] & RM_ZMODE) >> 10,
                        (g_fd_om[i] & RM_FORCE_BL) != 0);
        }
    }

    /* ---- TEMPORARY PROBE report (B-046). SL_CC_DBG=1. ---------------- */
    if (ccx_dbg()) {
        /* a/b/d muxes are 4 bits, the c mux 5 - ucode05_old.txt
         * "FC rdp_setcombine". Separate tables: 8..15 differ per slot. */
        static const char *abn[16] = {"COMB","TEX0","TEX1","PRIM","SHADE",
            "ENV","NOISE/1","0","0","0","0","0","0","0","0","0"};
        static const char *cn[32] = {"COMB","TEX0","TEX1","PRIM","SHADE",
            "ENV","KEYSC","COMB.a","TEX0.a","TEX1.a","PRIM.a","SHADE.a",
            "ENV.a","LODFRAC","PRIMLOD","K5","0","0","0","0","0","0","0","0",
            "0","0","0","0","0","0","0","0"};
        static const char *dn[8]  = {"COMB","TEX0","TEX1","PRIM","SHADE",
            "ENV","1","0"};
        static const char *an[8]  = {"COMB","TEX0","TEX1","PRIM","SHADE",
            "ENV","1","0"};
        unsigned i;
        for (i = 0; i < g_ccx_n; i++) {
            unsigned w0 = g_ccx_w[i][0], w1 = g_ccx_w[i][1];
            unsigned a0 = (w0 >> 20) & 0x0f, b0 = (w1 >> 28) & 0x0f;
            unsigned c0 = (w0 >> 15) & 0x1f, d0 = (w1 >> 15) & 0x07;
            unsigned a1 = (w0 >>  5) & 0x0f, b1 = (w1 >> 24) & 0x0f;
            unsigned c1 =  w0        & 0x1f, d1 = (w1 >>  6) & 0x07;
            unsigned aa0 = (w0 >> 12) & 7, ab0 = (w1 >> 12) & 7;
            unsigned ac0 = (w0 >>  9) & 7, ad0 = (w1 >>  9) & 7;
            /* B-051: CYCLE 1's alpha muxes, which this probe never printed
             * and the classifier above never reads. ucode05_old.txt
             * "FC rdp_setcombine" lower word: Aa1 00E00000, Ac1 001C0000,
             * Ab1 00000038, Ad1 00000007. The tinted-glass pane puts
             * PRIMITIVE in Ad1, so a cycle-0-only reading cannot see it. */
            unsigned aa1 = (w1 >> 21) & 7, ab1 = (w1 >> 3) & 7;
            unsigned ac1 = (w1 >> 18) & 7, ad1 =  w1        & 7;
            fprintf(stderr,
                "sl_ccx: f%u %08x %08x cyc=%u tris=%-5u tr=%-4u fr=%-3u"
                " prim=%02x%02x%02x%02x env=%02x%02x%02x%02x tile=%u\n"
                "sl_ccx:    rgb0=(%s-%s)*%s+%s  rgb1=(%s-%s)*%s+%s"
                "  a0=(%s-%s)*%s+%s  a1=(%s-%s)*%s+%s\n",
                frames, w0, w1, g_ccx_cyc[i], g_ccx_n_tri[i], g_ccx_n_tr[i],
                g_ccx_n_fr[i],
                g_ccx_prim[i][0], g_ccx_prim[i][1], g_ccx_prim[i][2],
                g_ccx_prim[i][3], g_ccx_env[i][0], g_ccx_env[i][1],
                g_ccx_env[i][2], g_ccx_env[i][3], g_ccx_tex[i],
                abn[a0], abn[b0], cn[c0], dn[d0],
                abn[a1], abn[b1], cn[c1], dn[d1],
                an[aa0], an[ab0], an[ac0], an[ad0],
                an[aa1], an[ab1], an[ac1], an[ad1]);
        }
    }
    /* ---- end temporary probe report ---------------------------------- */

    /* ---- B-051 probe report. SL_GLASSPOS=1. -------------------------- */
    if (glasspos_on()) {
        extern unsigned sl_record_index(void);
        /* NDC -> the 640x480 framebuffer the captures are taken at, so the
         * box can be compared directly against a masked PPM. y is flipped:
         * NDC +1 is the top row. */
        if (aeq_watch_on())
            fprintf(stderr, "sl_aeqw: f%u read=%u word-tris=%u\n",
                    frames, sl_record_index(), g_aeq_watch_tris);
        fprintf(stderr, "sl_gpos: f%u read=%u glass-tris=%u behind=%u"
                        " all-tris=%u painted-magenta=%u\n",
                frames, sl_record_index(), g_gp_tris, g_gp_behind,
                g_gp_all, g_gp_marked);
        if (g_gp_tris > g_gp_behind && g_gp_min[0] < 1e29f)
            fprintf(stderr, "sl_gpos:   glass ndc x[%.3f,%.3f] y[%.3f,%.3f]"
                            " z[%.3f,%.3f]  px x[%.0f,%.0f] y[%.0f,%.0f]"
                            " area=%.0f\n",
                    g_gp_min[0], g_gp_max[0], g_gp_min[1], g_gp_max[1],
                    g_gp_min[2], g_gp_max[2],
                    (g_gp_min[0] + 1.0f) * 320.0f, (g_gp_max[0] + 1.0f) * 320.0f,
                    (1.0f - g_gp_max[1]) * 240.0f, (1.0f - g_gp_min[1]) * 240.0f,
                    g_gp_area < 0.0 ? -g_gp_area : g_gp_area);
        if (g_gp_amin[0] < 1e29f)
            fprintf(stderr, "sl_gpos:   all   ndc x[%.3f,%.3f] y[%.3f,%.3f]"
                            " z[%.3f,%.3f]  px x[%.0f,%.0f] y[%.0f,%.0f]\n",
                    g_gp_amin[0], g_gp_amax[0], g_gp_amin[1], g_gp_amax[1],
                    g_gp_amin[2], g_gp_amax[2],
                    (g_gp_amin[0] + 1.0f) * 320.0f, (g_gp_amax[0] + 1.0f) * 320.0f,
                    (1.0f - g_gp_amax[1]) * 240.0f, (1.0f - g_gp_amin[1]) * 240.0f);
    }
    /* ---- end B-051 probe report -------------------------------------- */

    if ((frames++ % 60) == 0) {
      if (!dl_census_on()) {
        /* B-125: redraw=<loads that re-loaded only drawn positions>/<tris
         * re-triangulated, drawn with a tolerance>/<redraw tris drawn plainly>
         * - a frame's authored multi-pass count, on the health line so a
         * run says whether the path fired and how narrowly. */
        fprintf(stderr, "sl_dl: frame %u cmds=%u vtx=%u(rej=%u) tris=%u unknown=%u redraw=%u/%u/%u  [census off; SL_DL_CENSUS=1]\n",
                g_dl_frame, g_cmds, g_verts, g_vtx_reject, g_tris, g_unknown,
                g_rd_loads_old, g_rd_tris, g_rd_tris_same);
        /* #45: the display shape in force this frame - the selection, the
         * three rectangles (window coordinates, GL origin) and k - plus how
         * many backdrops were widened. The 4:3 negative control reads
         * k=1.000 with content == safe == window and 0/0 here. */
        {
            int capped = 0;
            double veff = sl_display_fov_effective(g_aspect_id, &capped);
            double vtot = 2.0 * atan(tan(veff * 3.14159265358979323846 / 360.0) * sl_aspect_value(g_aspect_id)) * 180.0 / 3.14159265358979323846;
            fprintf(stderr, "sl_display: aspect=%s k=%.3f window=%dx%d content=%d,%d %dx%d safe=%d,%d %dx%d logical=%ux%u extended=%u/%u"
                            " fov=%d.%02d(h16=%d) eff=%.2f total=%.1f cap=%s s=%.4f world-proj=%s cursor=%u/%u\n",
                    sl_aspect_name(g_aspect_id), g_aspect_k, g_window_vp[2], g_window_vp[3],
                    g_content_vp[0], g_content_vp[1], g_content_vp[2], g_content_vp[3],
                    g_win_vp[0], g_win_vp[1], g_win_vp[2], g_win_vp[3],
                    g_scr_w, g_scr_h, g_fr_extended, g_rh_extended,
                    sl_fov_vertical() / 100, sl_fov_vertical() % 100, sl_fov_h16_displayed(),
                    veff, vtot, capped ? "engaged" : "off", g_fov_s,
                    g_world_proj_addr ? "named" : "none", g_cur_applied, g_cur_tags);
        }
      } else {
        fprintf(stderr, "sl_dl: frame %u cmds=%u vtx=%u(rej=%u) tris=%u unknown=%u "
                        "bounds x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n",
                frames, g_cmds, g_verts, g_vtx_reject, g_tris, g_unknown,
                g_min[0], g_max[0], g_min[1], g_max[1], g_min[2], g_max[2]);
        fprintf(stderr, "sl_vtx: loads-to-slot-nonzero=%u unresolved=%u\n",
                g_vtx_v0nz, g_vtx_unres);
        fprintf(stderr, "sl_mtx: cmds=%u proj=%u mv=%u load=%u mul=%u "
                        "push=%u pop=%u unresolved=%u sp=%d\n",
                g_mtx_cmds, g_mtx_proj, g_mtx_mv, g_mtx_load, g_mtx_mul,
                g_mtx_push, g_mtx_pop, g_mtx_bad, g_mv_sp);
        fprintf(stderr, "sl_mtx: proj [%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f"
                        " | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]\n",
                g_proj[0], g_proj[1], g_proj[2], g_proj[3],
                g_proj[4], g_proj[5], g_proj[6], g_proj[7],
                g_proj[8], g_proj[9], g_proj[10], g_proj[11],
                g_proj[12], g_proj[13], g_proj[14], g_proj[15]);
        {   /* Top unhandled opcodes, and how the mass splits between commands
             * that ARE in the ucode05 set (just not implemented here) and
             * values that are not commands at all - the latter is what a walk
             * through non-DL memory looks like. */
            unsigned i, r, distinct = 0, in_set = 0, off_set = 0;
            unsigned char taken[256];
            memset(taken, 0, sizeof taken);
            for (i = 0; i < 256; i++) {
                if (!g_op_hist[i]) continue;
                distinct++;
                if (op_in_ucode05(i)) in_set += g_op_hist[i];
                else                  off_set += g_op_hist[i];
            }
            fprintf(stderr, "sl_ops: unknown=%u distinct=%u  in-ucode05=%u"
                            "  off-command-set=%u\n",
                    g_unknown, distinct, in_set, off_set);
            fprintf(stderr, "sl_ops: top:");
            for (r = 0; r < 15; r++) {         /* repeated max scan: 15 passes */
                unsigned best = 256, bestv = 0;
                for (i = 0; i < 256; i++)
                    if (!taken[i] && g_op_hist[i] > bestv) { bestv = g_op_hist[i]; best = i; }
                if (best == 256) break;
                taken[best] = 1;
                fprintf(stderr, " %02X=%u%s", best, bestv,
                        op_in_ucode05(best) ? "" : "*");
            }
            fprintf(stderr, "   (* = not a ucode05 command)\n");
        }
        {
            unsigned d;
            fprintf(stderr, "sl_walk: zero-words=%u end-stops=%u guard-stops=%u"
                            " lists=%u/%u(native/swapped)  per-depth:",
                    g_zero_runs, g_end_stops, g_guard_stops,
                    g_list_native, g_list_swapped);
            for (d = 0; d < 10; d++)
                if (g_depth_cmds[d]) fprintf(stderr, " d%u=%u", d, g_depth_cmds[d]);
            fprintf(stderr, "\n");
        }
        sl_bb_report();          /* B-051 stage 1, SL_BB_DBG, default OFF */
        fprintf(stderr, "sl_tex: cached=%u decoded=%u hits=%u uploads=%u"
                        " binds=%u fail=%u  reject[dim,size,unmapped,pal,"
                        "decode,fmt,tilesize,nogl]=%u,%u,%u,%u,%u,%u,%u,%u"
                        "  tlut16=%u\n",
                g_texcache_n, g_tex_decoded, g_tex_hits, g_tex_uploads,
                g_tex_binds, g_tex_fail,
                g_tex_reject[0], g_tex_reject[1], g_tex_reject[2],
                g_tex_reject[3], g_tex_reject[4], g_tex_reject[5],
                g_tex_reject[6], g_tex_reject[7], g_tex_tlut16);
        fprintf(stderr, "sl_tex: B-116 world-enhanced=%u of %u decoded"
                        "  B-141 pal-wrap images=%u texels=%u (cumulative,"
                        " SL_PAL_WRAP)  #47 provider-replaced uploads=%u\n",
                g_tex_enhanced, g_tex_decoded,
                g_tex_palwrap_imgs, g_tex_palwrap_texels, g_tex_replaced);
        sl_texprov_report(stderr);       /* #47: the provider's own census */
        /* #47 PART A. The per-id coverage table, if it was asked for. CSV,
         * one row per texture number seen, plus the two aggregates the shares
         * are taken against. Written here so it lands in the same report the
         * provider's own census does. */
        if (texcov_armed()) {
            fprintf(stderr, "sl_texcov: frames=%u dl-frame=%u per-frame=%d"
                            " textured-area=%.0f untextured-area=%.0f"
                            " frame=%ux%u hilited=%u -> %s\n",
                    g_texcov_frames, g_dl_frame, texcov_perframe(),
                    g_texcov_tex, g_texcov_plain, g_scr_w, g_scr_h,
                    g_tex_hilited, getenv("SL_TEX_COVERAGE"));
            if (!texcov_perframe()) texcov_write();
        }
        fprintf(stderr, "sl_texenv: plain-under-alpha-program=%u (B-142"
                        " invariant, must be 0)  untextured-left-mode-5=%u\n",
                g_plain_under_prog, g_plain_prog_reset);
        fprintf(stderr, "sl_sciss: B-143 rdp-scissor=%s applied-this-frame=%u"
                        " empty-boxes-this-frame=%u frames-with-empty=%u"
                        " last=[%d,%d]-[%d,%d]\n",
                sciss_on() ? "on" : "off (SL_SCISSOR=0)",
                g_sciss_applied, g_sciss_empty, g_sciss_frames_empty,
                g_sciss[0], g_sciss[1], g_sciss[2], g_sciss[3]);
        fprintf(stderr, "sl_tex: src-entropy bits/byte min=%.2f avg=%.2f"
                        " max=%.2f over %u images   st-max=%u tile-w=%u\n",
                (g_tex_entropy_n ? g_tex_entropy_min : 0.0f),
                (g_tex_entropy_n ? (float) (g_tex_entropy_sum / g_tex_entropy_n) : 0.0f),
                (g_tex_entropy_n ? g_tex_entropy_max : 0.0f),
                g_tex_entropy_n, g_st_max_abs, g_st_tex_w);
        fprintf(stderr, "sl_tex: tris textured=%u plain=%u  BB-on at draw=%u"
                        "  distinct textures drawn=%u no-source=%u\n",
                g_tri_tex, g_tri_plain, g_tex_on_tris, g_tex_names_n, g_tex_nosrc);
        fprintf(stderr, "sl_tex: tlut cmds=%u bad=%u zero-offset=%u"
                        "  last: off=%u entries=%u mode=%u fd-w=%u\n",
                g_tlut_cmds, g_tlut_bad, g_tlut_off0,
                g_tlut_off, g_tlut_n, g_tlut_mode, g_ti_w);
        /* dxt0 loads are pre-swapped assets (tex.c:514/:530), dxtn loads are
         * plain (textrelated.c:243 via gbi_extension.h:252). See B-020. */
        fprintf(stderr, "sl_tex: loadblock dxt=0 %u  dxt!=0 %u\n",
                g_load_dxt0, g_load_dxtn);
        {   /* Every distinct image that reached a tile under a non-zero dxt,
             * i.e. everything the B-020 rule reads as "plain, do not unswizzle".
             * Glyphs belong here; anything else is the open question. */
            unsigned i;
            for (i = 0; i < g_dxtn_n; i++)
                fprintf(stderr, "sl_tex: dxt!=0 load %u: src=%p fd fmt=%u"
                                " siz=%u w=%u  dxt=%u texels=%u"
                                "  drawn %ux%u x%u\n",
                        i, (const void *) g_dxtn_src[i],
                        g_dxtn_fmt[i], g_dxtn_siz[i], g_dxtn_fdw[i],
                        g_dxtn_dxt[i], g_dxtn_texels[i],
                        g_dxtn_w[i], g_dxtn_h[i], g_dxtn_draws[i]);
        }
        {   /* B-023: the tile's declared stride against the one the decoder
             * derives from the width. Anything with want != line*8 is being
             * decoded at a stride the RDP was never told to use. */
            unsigned i;
            fprintf(stderr, "sl_tex: tile-mask resize w=%u h=%u rule=%s"
                            "  (B-088; zero means the F2 span already"
                            " equalled 2^mask on every repeating axis)\n",
                    g_tmask_w, g_tmask_h, tile_mask_on() ? "on" : "off");
            fprintf(stderr, "sl_tex: stride-width resize w=%u rule=%s"
                            "  (B-102; the subset of the w column above that"
                            " took the tile's `line` because it DISAGREED"
                            " with 2^masks - zero means the two agreed"
                            " everywhere and B-088's width stood)\n",
                    g_tline_w, tile_line_on() ? "on" : "off");
            fprintf(stderr, "sl_tex: mask-zero clamp s=%u t=%u rule=%s"
                            "  wrap-alias=%u"
                            "  (B-099; alias counts one source uploaded under"
                            " one wrap mode and later drawn asking another)\n",
                    g_tclamp0_s, g_tclamp0_t,
                    tile_clamp0_on() ? "on" : "off", g_tex_wrap_alias);
            fprintf(stderr, "sl_texgen: verts=%u declined=%u zero-normal=%u\n",
                    g_texgen_verts, g_texgen_declined, g_texgen_zeronrm);
            /* The override generator's own numbers, reported beside the
             * display-list generator's rather than under a flag of their
             * own - B-088's rule: a count of zero and a count that was
             * never taken look identical in a log. draws=0 says no override
             * drew; draws>0 with prims=0 says one drew and generated
             * nothing, which is a different failure and has to be
             * distinguishable from the first. */
            fprintf(stderr, "sl_texgen: model draws=%u part-draws=%u tris=%u"
                            "  generated prims=%u verts=%u zero-normal=%u"
                            "  declined=%u no-normals=%u  (SL_TEXGEN_MODEL=%d)\n",
                    g_aov_draws, g_aov_part_draws, g_aov_tris_drawn, g_aov_gen_prims,
                    g_aov_gen_verts, g_aov_gen_zeronrm, g_aov_gen_declined,
                    g_aov_gen_nonrm, texgen_model());
            fprintf(stderr, "sl_tex: stride disagreements=%u of %u distinct\n",
                    g_strd_bad, g_strd_n);
            for (i = 0; i < g_strd_n; i++)
                if (g_strd_line[i] * 8u != g_strd_want[i])
                    fprintf(stderr, "sl_tex: stride BAD src=%p fmt=%X %ux%u"
                                    " line=%u (%u bytes) want=%u texels=%u"
                                    " draws=%u\n",
                            (const void *) g_strd_src[i], g_strd_fmt[i],
                            g_strd_w[i], g_strd_h[i], g_strd_line[i],
                            g_strd_line[i] * 8u, g_strd_want[i],
                            g_strd_texels[i], g_strd_draws[i]);
        }
        {   /* B-023: does the tile's format agree with the FD's? Both are
             * written from tex->gbiformat - tex.c:445 for the render tile and
             * tex.c:506 for the image - so they cannot legitimately differ,
             * and every image is counted here rather than only the rough ones
             * so "the noisy ones are exactly the disagreeing ones" is a
             * measurement over the whole level and not eight samples. */
            unsigned i, dis = 0;
            for (i = 0; i < g_noisy_n; i++)
                if (g_noisy_tfmt[i] != g_noisy_ffmt[i]) dis++;
            fprintf(stderr, "sl_tex: tile-fmt vs FD-fmt: %u of %u disagree\n",
                    dis, g_noisy_n);
            for (i = 0; i < g_noisy_n; i++)
                if (g_noisy_tfmt[i] != g_noisy_ffmt[i])
                    fprintf(stderr, "sl_tex:   MISMATCH src=%p F5 fmt=%u siz=%u"
                                    " line=%u  FD fmt=%u  %ux%u rough=%u\n",
                            (const void *) g_noisy_src[i], g_noisy_tfmt[i],
                            g_noisy_tsiz[i], g_noisy_line[i], g_noisy_ffmt[i],
                            g_noisy_w[i], g_noisy_h[i], g_noisy_rough[i]);
        }
        {   /* B-023: the roughest decoded images, worst first. Static sits far
             * above anything a real wall texture scores. */
            unsigned i, k, shown = 0;
            for (k = 0; k < g_noisy_n && shown < 8u; k++) {
                unsigned best = 0, bi = g_noisy_n;
                for (i = 0; i < g_noisy_n; i++)
                    if (g_noisy_rough[i] != 0xffffffffu &&
                        g_noisy_rough[i] >= best) { best = g_noisy_rough[i]; bi = i; }
                if (bi >= g_noisy_n || best < 24u) break;
                fprintf(stderr, "sl_tex: rough %3u  src=%p sl-fmt=%X %ux%u"
                                " flags=%u | F5 fmt=%u siz=%u line=%u tmem=%u"
                                " | F2 s[%u,%u] t[%u,%u] | FD fmt=%u siz=%u"
                                " w=%u | F3 texels=%u\n",
                        best, (const void *) g_noisy_src[bi], g_noisy_fmt[bi],
                        g_noisy_w[bi], g_noisy_h[bi], g_noisy_flags[bi],
                        g_noisy_tfmt[bi], g_noisy_tsiz[bi], g_noisy_line[bi],
                        g_noisy_tmem[bi], g_noisy_uls[bi], g_noisy_lrs[bi],
                        g_noisy_ult[bi], g_noisy_lrt[bi], g_noisy_ffmt[bi],
                        g_noisy_fsiz[bi], g_noisy_fw[bi], g_noisy_tex[bi]);
                g_noisy_rough[bi] = 0xffffffffu;
                shown++;
            }
        }
        fprintf(stderr, "sl_tex: loadblocks=%u  st/(32*w) within 4 repeats:"
                        " %u of %u (%.1f%%)\n",
                g_loadblocks, g_st_in, g_st_in + g_st_out,
                (g_st_in + g_st_out) ? 100.0 * g_st_in / (g_st_in + g_st_out) : 0.0);
        {   /* First few cache entries: address, format, size, texel checksum.
             * With no way to see the window, this is the evidence that real
             * pixels were produced and that they are stable frame to frame. */
            unsigned i, n = g_texcache_n < 4 ? g_texcache_n : 4;
            fprintf(stderr, "sl_tex: samples:");
            for (i = 0; i < n; i++)
                fprintf(stderr, " [%p fmt=%X %ux%u sum=%08x gl=%u]",
                        g_texcache[i].src, g_texcache[i].fmt,
                        g_texcache[i].w, g_texcache[i].h, g_texcache[i].checksum,
                        (unsigned) g_texcache[i].gl);
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "sl_2d: texrect cmds=%u drawn=%u textured=%u flip=%u"
                        "  reject[nosrc,tilesize,fmt,acquire,degenerate]="
                        "%u,%u,%u,%u,%u  offscreen=%u abandoned=%u"
                        " orphan-halves=%u inverted=%u\n",
                g_tr_cmds, g_tr_drawn, g_tr_textured, g_tr_flips,
                g_tr_reject[TR_NOSRC], g_tr_reject[TR_TILESIZE],
                g_tr_reject[TR_FMT], g_tr_reject[TR_ACQUIRE],
                g_tr_reject[TR_DEGEN], g_tr_offscreen, g_tr_abandoned,
                g_half_orphan, g_tr_inverted);
        /* B-101. The sky's hand-built RDP triangles. "assembled" counts
         * complete commands recovered from the B4/B2/B3 word stream; a level
         * whose environment row sets clouds=0 (bgfog.c) never builds one and
         * correctly reports zero here. */
        fprintf(stderr, "sl_rdptri: assembled=%u drawn=%u degenerate=%u"
                        " non-triangle-halves=%u overflow=%u partial=%u"
                        " skylerp=%u  rdptri=%s skylerp=%s\n",
                g_rh_tris, g_rh_drawn, g_rh_dropped, g_rh_notri,
                g_rh_overflow, g_rh_n, g_rh_skylerp,
                rdptri_on() ? "on" : "off",
                cc_skylerp_on() ? "on" : "off");
        {   /* B-026. Which render modes reached geometry, and what each cost.
             * The decomp names 40-odd G_RM_* constants; only this says which
             * ones a level actually draws with. */
            unsigned i;
            fprintf(stderr, "sl_rm: B9 cmds=%u malformed=%u  tris blended=%u"
                            " decal/inter=%u depth-write-off=%u"
                            "  before-first-B9=%u  modes=%u(lost %u)\n",
                    g_om_l_cmds, g_om_l_bad, g_rm_blend_tris,
                    g_rm_decal_tris, g_rm_nozwrite_tris, g_rm_pre_tris,
                    g_rm_hist_used, g_rm_hist_lost);
            for (i = 0; i < g_rm_hist_used; i++) {
                unsigned om = g_rm_hist_key[i];
                int two = (g_rm_hist_cyc[i] == 1);
                fprintf(stderr, "sl_rm:   %08x cyc=%u tris=%-6u  zcmp=%d"
                                " zupd=%d zmode=%u forcebl=%d cvgxa=%d"
                                " blender P%u A%u M%u B%u\n",
                        om, g_rm_hist_cyc[i], g_rm_hist_n[i],
                        (om & RM_Z_CMP) != 0, (om & RM_Z_UPD) != 0,
                        (om & RM_ZMODE) >> 10, (om & RM_FORCE_BL) != 0,
                        (om & RM_CVG_X_ALPHA) != 0,
                        two ? ((om >> 28) & 3u) : ((om >> 30) & 3u),
                        two ? ((om >> 24) & 3u) : ((om >> 26) & 3u),
                        two ? ((om >> 20) & 3u) : ((om >> 22) & 3u),
                        two ? ((om >> 16) & 3u) : ((om >> 18) & 3u));
            }
        }
        fprintf(stderr, "sl_2d: fillrect cmds=%u drawn=%u (fill-colour=%u"
                        " combiner=%u) z-skipped=%u degenerate=%u  cycle=%u"
                        "  screen=%ux%u  ortho-entries=%u"
                        "  coverage=%.1f%% bbox x[%.0f,%.0f] y[%.0f,%.0f]\n",
                g_fr_cmds, g_fr_drawn, g_fr_filled, g_fr_combined,
                g_fr_zskip, g_fr_degen, g_cycle_type,
                g_scr_w, g_scr_h, g_2d_enters, 100.0 * g_2d_cover,
                g_2d_bbox[0], g_2d_bbox[2], g_2d_bbox[1], g_2d_bbox[3]);
        fprintf(stderr, "sl_2d: rect-order w0=lower-right(gbi)=%u"
                        " w0=upper-left(note)=%u   before/after first triangle:"
                        " texrect %u/%u fillrect %u/%u  over-world coverage"
                        "=%.1f%%\n",
                g_tr_order_w0lr, g_tr_order_w0ul,
                g_2d_pre_geom_tr, g_2d_post_geom_tr,
                g_2d_pre_geom_fr, g_2d_post_geom_fr,
                100.0 * g_2d_post_geom_cover);
        if (g_2d_big_area > 0.0f)
            fprintf(stderr, "sl_2d: largest over-world rect: %s x[%.1f,%.1f]"
                            " y[%.1f,%.1f] colour=%02x%02x%02x alpha=%u\n",
                    g_2d_big_is_fill ? "fillrect" : "texrect",
                    g_2d_big[0], g_2d_big[2], g_2d_big[1], g_2d_big[3],
                    g_2d_big_col[0], g_2d_big_col[1], g_2d_big_col[2],
                    g_2d_big_col[3]);
        fprintf(stderr, "sl_2d: prim=%02x%02x%02x%02x env=%02x%02x%02x%02x"
                        " fog=%02x%02x%02x%02x fill=%02x%02x%02x%02x(%s)"
                        "  cmds p/e/f/b/fill=%u/%u/%u/%u/%u\n",
                g_prim[0], g_prim[1], g_prim[2], g_prim[3],
                g_env[0], g_env[1], g_env[2], g_env[3],
                g_fog[0], g_fog[1], g_fog[2], g_fog[3],
                g_fill[0], g_fill[1], g_fill[2], g_fill[3],
                g_fill_is16 ? "5551" : "rgba8888",
                g_col_cmds[0], g_col_cmds[1], g_col_cmds[2],
                g_col_cmds[3], g_col_cmds[4]);
        fprintf(stderr, "sl_2d: combine cmds=%u last=%08x %08x rgb-const=%d"
                        " alpha uses prim=%d texel0=%d  texenv=%s\n",
                g_cc_cmds, g_cc_w0, g_cc_w1, g_cc_rgb_const,
                g_cc_a_prim, g_cc_a_texel,
                g_texenv == 1 ? "prim-replace" : "modulate");
        fprintf(stderr, "sl_cc: env-scaled-alpha tris=%u (B-043)  env-a=%u\n",
                g_env_alpha_tris, g_env[3]);
        fprintf(stderr, "sl_cc: constant-tinted tris=%u rects=%u"
                        " fillrects-left-white=%u (B-046)  tint=%s\n",
                g_cc_tint_tris, g_cc_tint_rects, g_fr_tintskip,
                cc_tint_on() ? "on" : "off");
        fprintf(stderr, "sl_cc: register-lerp rects=%u k=%.4f (B-090)"
                        "  lerp=%s\n",
                g_cc_lerp_rects, g_cc_lerp_k, cc_lerp_on() ? "on" : "off");
        fprintf(stderr, "sl_shade: linear=%s program=%s batches=%u (B-131)"
                        "  SL_SHADE_LINEAR\n",
                shade_linear_on() ? "on" : "off",
                g_lin_state > 0 ? "ready" : (g_lin_state < 0 ? "unavailable" : "untried"),
                g_lin_batches);
        fprintf(stderr, "sl_cc: TEXEL1 tris=%u binds=%u unresolved=%u"
                        " tile=%u %ux%u shift=%u/%u odd-shift=%u"
                        " (B-048)  tex1=%s%s\n",
                g_tex1_tris, g_tex1_binds, g_tex1_fail, g_tex1_tile,
                g_tex1_w, g_tex1_h, g_tex1_shifts, g_tex1_shiftt,
                g_tex1_shiftbig, cc_tex1_on() ? "on" : "off",
#if !defined(SL_MULTITEX)
                " (no multitexture in this GL header)");
#elif defined(SL_GL_RUNTIME_POST11)
                /* Compiled in, but on Win32 it is the RUNTIME resolution that
                 * decides. Say which, so "tex1=on" can never again mean the
                 * path was merely built. */
                sl_post11_state > 0 ? " (post-1.1 entry points resolved)"
                : sl_post11_state < 0 ? " (post-1.1 entry points UNRESOLVED)"
                : " (post-1.1 entry points not yet probed)");
#else
                "");
#endif
        fprintf(stderr, "sl_cc: tmem map hits=%u misses=%u inner-hits=%u"
                        " one-image-mips=%u (B-128)"
                        "  detail-blend z-upd authored=%u (B-140: no"
                        " tree override)\n",
                g_tmem_hit, g_tmem_miss, g_tmem_inner, g_lodmip_same,
                g_tree_zupd_skip);
        fprintf(stderr, "sl_cc: BB-on under a texel-free combiner tris=%u"
                        " (B-035)  gate=%s\n",
                g_cc_notex_tris, cc_texgate() ? "on" : "off");
        fprintf(stderr, "sl_cc: B-144 c=13 lerp under G_TL_TILE (LOD off, drawn"
                        " as TEXEL0) tris=%u  textlod-now=%u  gate=%s\n",
                g_lodlerp_tile_tris, g_textlod,
                textlod_gate_on() ? "on" : "off (SL_TEXTLOD_GATE=0)");
        {   unsigned i;
            for (i = 0; i < g_cc_notex_n; i++)
                fprintf(stderr, "sl_cc:   texel-free combiner %08x %08x"
                                " tris=%u\n",
                        g_cc_notex_w[i][0], g_cc_notex_w[i][1],
                        g_cc_notex_w[i][2]);
        }
        {   /* GL errors are otherwise completely silent, and the one that cost
             * a night here (glGenTextures inside glBegin) left a zero texture
             * name and no other symptom. Drained rather than reported once so
             * a stale error cannot be attributed to the wrong frame. */
            GLenum e; unsigned n = 0, first = 0;
            while ((e = glGetError()) != GL_NO_ERROR && n < 32) {
                if (!n) first = (unsigned) e;
                n++;
            }
            if (n) fprintf(stderr, "sl_gl: %u error(s) this frame, first=0x%04x\n",
                           n, first);
        }
        fprintf(stderr, "sl_geom: seen=%08x SL_CULL=%d cull-tris none=%u"
                        " front=%u back=%u both=%u"
                        "  winding under cull ccw=%u(meanz=%.3f)"
                        " cw=%u(meanz=%.3f)  near-fan culled=%u (SL_NEARCULL)\n",
                g_geom_seen, cull_option(),
                g_cull_tris[0], g_cull_tris[1], g_cull_tris[2],
                g_cull_tris[3],
                g_cull_n_ccw, g_cull_n_ccw ? g_cull_z_ccw / g_cull_n_ccw : 0.0,
                g_cull_n_cw,  g_cull_n_cw  ? g_cull_z_cw  / g_cull_n_cw  : 0.0,
                g_nearcull_dropped);
        {   unsigned i;
            fprintf(stderr, "sl_geom: big(>500px) none=%u front=%u back=%u"
                            " both=%u   timeline tri@cull:",
                    g_cull_big[0], g_cull_big[1], g_cull_big[2], g_cull_big[3]);
            for (i = 0; i < g_geomlog_n; i++)
                fprintf(stderr, " %u@%04x", g_geomlog_tris[i],
                        g_geomlog_mode[i]);
            fprintf(stderr, " (+%u more)\n", g_geomlog_lost);
            fprintf(stderr, "sl_geom: shared-edge winding consistent=%u"
                            " reversed=%u unpaired=%u\n",
                    g_edge_ok, g_edge_bad, g_edge_none);
        }
        if (g_vp_seen)
            fprintf(stderr, "sl_vp: gl=[%d %d %d %d] applies=%u %s | "
                            "scale=(%d,%d) trans=(%d,%d) -> x[%g,%g]"
                            " y[%g,%g] of %ux%u\n",
                    g_vp_rect[0], g_vp_rect[1], g_vp_rect[2], g_vp_rect[3],
                    g_vp_applies, vp_on() ? "on" : "off",
                    g_vp_sx, g_vp_sy, g_vp_tx, g_vp_ty,
                    (g_vp_tx - g_vp_sx) / 4.0, (g_vp_tx + g_vp_sx) / 4.0,
                    (g_vp_ty - g_vp_sy) / 4.0, (g_vp_ty + g_vp_sy) / 4.0,
                    g_scr_w, g_scr_h);
        fprintf(stderr, "sl_2d: geom cmds=%u mode=%08x seen=%08x (NOT applied)"
                        "  cimg=%08x w=%u zimg=%08x cimg-is-z=%d"
                        "  movemem=%u types=%08x\n",
                g_geom_cmds, g_geom_mode, g_geom_seen, g_cimg_addr, g_cimg_w,
                g_zimg_addr, g_cimg_is_z, g_movemem_cmds, g_movemem_types);
        fprintf(stderr, "sl_ndc: on=%u off=%u (%.1f%% on screen) "
                        "x[%.2f,%.2f] y[%.2f,%.2f] z[%.2f,%.2f]\n",
                g_on, g_off,
                (g_on + g_off) ? 100.0 * g_on / (g_on + g_off) : 0.0,
                g_ndc_min[0], g_ndc_max[0], g_ndc_min[1], g_ndc_max[1],
                g_ndc_min[2], g_ndc_max[2]);
        fprintf(stderr, "sl_tri: window-area px  covering=%u degenerate=%u"
                        " clipped=%u  mean=%.2f max=%.1f\n",
                g_tri_area, g_tri_degen, g_tri_clipped,
                (g_tri_area + g_tri_degen)
                    ? g_tri_area_sum / (double) (g_tri_area + g_tri_degen)
                    : 0.0,
                g_tri_area_max);
        fprintf(stderr, "sl_mtx: mv   [%.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f"
                        " | %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f]\n",
                g_mv[g_mv_sp][0], g_mv[g_mv_sp][1], g_mv[g_mv_sp][2], g_mv[g_mv_sp][3],
                g_mv[g_mv_sp][4], g_mv[g_mv_sp][5], g_mv[g_mv_sp][6], g_mv[g_mv_sp][7],
                g_mv[g_mv_sp][8], g_mv[g_mv_sp][9], g_mv[g_mv_sp][10], g_mv[g_mv_sp][11],
                g_mv[g_mv_sp][12], g_mv[g_mv_sp][13], g_mv[g_mv_sp][14], g_mv[g_mv_sp][15]);
      }
    }

    /* B-100 near-plane census. OUTSIDE the 60-frame census block on purpose:
     * the population under test is a transient - it exists only while a
     * surface is close - so a sample every sixtieth frame would miss it and
     * report a confident zero. Per frame, and only when asked for. */
    if (nearwit_on()) {
        extern unsigned sl_record_index(void);
        fprintf(stderr, "sl_near: read=%u front=%u straddle=%u INSIDE=%u"
                        " behind=%u  area_inside=%.0fpx worst=%.0fpx"
                        " area_straddle=%.0fpx  clamp=%d\n",
                sl_record_index(), g_nw_front, g_nw_straddle, g_nw_inside,
                g_nw_behind, g_nw_area_inside, (double) g_nw_worst,
                g_nw_area_straddle, sl_gfx_depth_clamp_active());
    }

    /* ---- B-101 witness. See the header comment above this function. ---- */
    if (!g_wit_pass && wit_index() >= 0) {
        extern unsigned sl_record_index(void);
        int now = (int) sl_record_index();
        int due = wit_due(now);
        g_wit_a = 0;                    /* B-143: pass A of this frame is over */
        if (due) {
            int w = g_window_vp[2], h = g_window_vp[3];   /* the whole window (#45) */
            unsigned char *a = NULL, *b = NULL;
            g_wit_fired = 1; g_wit_fired_at = now;
            if (w > 0 && h > 0) {
                a = (unsigned char *) malloc((size_t) w * h * 3);
                b = (unsigned char *) malloc((size_t) w * h * 3);
            }
            if (a == NULL || b == NULL) {
                fprintf(stderr, "sl_sciss: WITNESS no buffers (%dx%d)\n", w, h);
            } else {
                int save_vp[4];
                unsigned save_scr_w, save_scr_h;
                memcpy(save_vp, g_window_vp, sizeof save_vp);
                save_scr_w = g_scr_w; save_scr_h = g_scr_h;

                /* Pass A is on the back buffer right now; SDL_GL_SwapWindow
                 * has not run. Same read as sdl_shot, same reason. */
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, a);

                /* Hand pass B the state sdl_begin hands every frame, then the
                 * state this function was ENTERED with. glClear obeys the
                 * scissor test, so it is disabled first - a clear inside a
                 * stale box is how a "difference" gets manufactured. */
                glDisable(GL_SCISSOR_TEST);
                glViewport(save_vp[0], save_vp[1], save_vp[2], save_vp[3]);
                glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                memcpy(g_vp_rect, g_wit_vp_rect, sizeof g_vp_rect);
                g_vp_have = g_wit_vp_have;
                g_scr_w = g_wit_scr_w; g_scr_h = g_wit_scr_h;

                g_wit_rects_n = 0;
                g_wit_pass = 1;
                sl_gfx_frame_dl(first, end);
                g_wit_pass = 0;
                glDisable(GL_SCISSOR_TEST);

                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, b);
                g_scr_w = save_scr_w; g_scr_h = save_scr_h;

                {   /* THE DIFFERENCE, AND THE ONE PART OF IT THAT MEANS
                     * ANYTHING.
                     *
                     * Measured on Dam at read=400: 3774 differing pixels,
                     * every one of them in a strip about three window pixels
                     * wide at the extreme left and right edges. That is not
                     * room geometry - it is the horizontal inset of the
                     * frame's own full-screen box, which lvlRender issues as
                     * [1,10]-[319,230] rather than [0,10]-[320,230], scaled
                     * by 960/320. It appears on EVERY frame regardless of
                     * what is drawn, so counting it would give a permanent
                     * false positive.
                     *
                     * So the reported number is the INTERIOR difference: the
                     * difference inside the largest box the frame issued,
                     * inset by two framebuffer pixels on every side. A room
                     * bleeding past its portal box lands there; the edge
                     * artefact cannot. Both numbers are printed, because
                     * hiding the excluded one is how an exclusion stops being
                     * checkable. */
                    long i, npx = (long) w * h, diff = 0, inner = 0, removed = 0;
                    int x0 = w, y0 = h, x1 = -1, y1 = -1, x, y;
                    int bi = -1, ba = -1, k, nfull = 0;
                    int fl = 0, ft = 0, fr = (int) save_scr_w, fb = (int) save_scr_h;
                    int ix0 = 0, iy0 = 0, ix1 = w, iy1 = h;
                    double fx = (double) save_scr_w / (double) w;
                    double fy = (double) save_scr_h / (double) h;
                    long fbarea = (long) save_scr_w * (long) save_scr_h;
                    for (k = 0; k < g_wit_rects_n; k++) {
                        long ar = (long) (g_wit_rects[k][2] - g_wit_rects[k][0])
                                * (long) (g_wit_rects[k][3] - g_wit_rects[k][1]);
                        if (ar > ba) { ba = (int) ar; bi = k; }
                        /* THE FULL-SCREEN BOXES, INTERSECTED.
                         *
                         * lvlRender issues a whole-view scissor every frame
                         * and the front end issues a whole-FRAMEBUFFER one,
                         * and they do not agree to the pixel: measured on Dam
                         * the frame carries [0,0]-[320,240], [0,10]-[320,230]
                         * AND [1,10]-[319,230] together. Honouring the last
                         * of those trims one framebuffer pixel off each side
                         * and one row at the letterbox edge, which at 960x720
                         * is a three-pixel outline that appears on every
                         * frame whatever is drawn. That outline is a property
                         * of the boxes, not of any room, so counting it would
                         * be a permanent false positive - it is exactly the
                         * shape of error this file's B-099 entry warns about.
                         * Intersecting the full-screen boxes and insetting by
                         * two removes it and keeps everything a room could
                         * bleed into. */
                        if (ar * 5 >= fbarea * 2) {           /* >= 40% of the frame */
                            nfull++;
                            if (g_wit_rects[k][0] > fl) fl = g_wit_rects[k][0];
                            if (g_wit_rects[k][1] > ft) ft = g_wit_rects[k][1];
                            if (g_wit_rects[k][2] < fr) fr = g_wit_rects[k][2];
                            if (g_wit_rects[k][3] < fb) fb = g_wit_rects[k][3];
                        }
                    }
                    if (nfull > 0 && fx > 0.0 && fy > 0.0 && fr - fl > 8 && fb - ft > 8) {
                        /* framebuffer -> window pixels, top-left origin in
                         * both; the read-back buffer is bottom-up, so y is
                         * flipped when the loop indexes it. */
                        ix0 = (int) ((fl + 2) / fx);
                        ix1 = (int) ((fr - 2) / fx);
                        iy0 = (int) ((ft + 2) / fy);
                        iy1 = (int) ((fb - 2) / fy);
                        if (ix0 < 0) ix0 = 0;
                        if (iy0 < 0) iy0 = 0;
                        if (ix1 > w) ix1 = w;
                        if (iy1 > h) iy1 = h;
                    }
                    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
                        int ty = h - 1 - y;         /* top-left row of this GL row */
                        i = ((long) y * w + x) * 3;
                        if (a[i] == b[i] && a[i+1] == b[i+1] && a[i+2] == b[i+2])
                            continue;
                        diff++;
                        if (x < ix0 || x >= ix1 || ty < iy0 || ty >= iy1) continue;
                        inner++;
                        /* "to-clear" = pass B fell back to the GL clear colour
                         * (sl_gfx_sdl.c:107) where pass A had drawn: geometry
                         * the scissor took away rather than merely shifted. */
                        if (b[i] == 13 && b[i+1] == 15 && b[i+2] == 23) removed++;
                        if (x < x0) x0 = x;
                        if (x > x1) x1 = x;
                        if (ty < y0) y0 = ty;
                        if (ty > y1) y1 = ty;
                    }
                    fprintf(stderr,
                        "sl_sciss: WITNESS read=%u %dx%d  interior-diff=%ld"
                        "  to-clear=%ld  all-diff=%ld  boxes=%d"
                        "  interior=px[%d,%d]-[%d,%d]\n",
                        sl_record_index(), w, h, inner, removed, diff,
                        g_wit_rects_n, ix0, iy0, ix1, iy1);
                    if (inner < wit_min()) { free(a); a = NULL; free(b); b = NULL; }
                    else {
                        char tag[24];
                        int n = 0, d10;
                        if (x1 >= 0)
                            fprintf(stderr,
                                "sl_sciss: WITNESS interior bbox px=[%d,%d]-[%d,%d]"
                                "  fb=[%d,%d]-[%d,%d] of %ux%u\n",
                                x0, y0, x1, y1,
                                (int) (x0 * fx), (int) (y0 * fy),
                                (int) (x1 * fx), (int) (y1 * fy),
                                save_scr_w, save_scr_h);
                        for (k = 0; k < g_wit_rects_n; k++)
                            fprintf(stderr, "sl_sciss: WITNESS box %2d = [%d,%d]-[%d,%d]%s\n",
                                    k, g_wit_rects[k][0], g_wit_rects[k][1],
                                    g_wit_rects[k][2], g_wit_rects[k][3],
                                    k == bi ? "   <- largest" : "");
                        /* The images are meant to be LOOKED AT, and they are
                         * named by READ INDEX so a frame cannot be confused
                         * with one sampled somewhere else. */
                        for (d10 = 10000; d10 > 0; d10 /= 10)
                            tag[n++] = (char) ('0' + (now / d10) % 10);
                        tag[n] = '\0';
                        {   char t2[40]; int m = 0;
                            const char *s;
                            for (s = tag; *s; s++) t2[m++] = *s;
                            t2[m] = '\0';
                            wit_ppm(t2, w, h, a);
                            for (i = 0; i < npx * 3; i += 3) {
                                if (a[i] != b[i] || a[i+1] != b[i+1] || a[i+2] != b[i+2]) {
                                    a[i] = 255; a[i+1] = 0; a[i+2] = 255;
                                } else {
                                    a[i] >>= 1; a[i+1] >>= 1; a[i+2] >>= 1;
                                }
                            }
                            m = 0;
                            for (s = tag; *s; s++) t2[m++] = *s;
                            t2[m++] = 'd'; t2[m] = '\0';
                            wit_ppm(t2, w, h, a);      /* pass A, diff magenta */
                            m = 0;
                            for (s = tag; *s; s++) t2[m++] = *s;
                            t2[m++] = 's'; t2[m] = '\0';
                            wit_ppm(t2, w, h, b);      /* the scissored render */
                        }
                    }
                }
            }
            free(a); free(b);
        }
    }

    /* SL_PHASE bucket B closes here, so it covers EVERYTHING this function
     * does - the GL setup, the walk, the teardown and the periodic census
     * above - rather than only the part that looked expensive. */
    if (sl_phase_dl_on()) g_phase_dl_s += sl_phase_dl_now() - g_phase_dl_t0;
}

/* ---- LIVE OWNER BUG MARK: the renderer's half of the record --------------
 *
 * Renders what the interpreter saw into a caller-supplied buffer. The game's
 * half (camera, rooms, projection) is gathered separately in
 * src/native/sl_game_query.c, which can see a struct this file cannot; the
 * two halves are concatenated by sl_run_mark_full in src/platform/sl_main.c.
 *
 * snprintf, NOT sprintf. src/sprintf.c defines a libultra-oriented sprintf
 * that links ahead of libc's and produces nothing natively - the first
 * version of sdl_shot wrote an empty path that way and fopen("") failed
 * silently. `grep -rn snprintf src/sprintf.c` is empty, so snprintf is
 * libc's and is safe here.
 */
static int mark_cat(char *out, int n, int at, const char *fmt, ...)
{
    va_list ap;
    int k;
    if (at >= n - 1) return at;
    va_start(ap, fmt);
    k = vsnprintf(out + at, (size_t) (n - at), fmt, ap);
    va_end(ap);
    if (k < 0) return at;
    at += k;
    return (at > n - 1) ? n - 1 : at;
}

/* Classification. Confident answers only: a depth-1 list that IS a room's
 * primary or secondary geometry is named as such, because the resolver reads
 * g_BgRoomInfo and that is a lookup rather than an inference. Everything else
 * is "unknown", with the segment number recorded so a reader can look it up.
 *
 * No object-name resolver is built here and none is wanted. Naming a segment-5
 * list "a character or a prop" would be an inference dressed as a reading, and
 * this record has already been wrong once that way.
 *
 * Nothing here can classify a HUD or 2D draw, and nothing pretends to: the 2D
 * passes go through the texture-rectangle path, never emit_tri, and so cannot
 * appear as a candidate at all. A centre pixel painted by one of those shows up
 * as "no candidate draws" plus a [centre-pixel] colour, which is the honest
 * shape of that answer. */
static const char *mark_class(unsigned addr, int (*resolve)(unsigned, int *),
                              int *room_out)
{
    int kind = 0, room = -1;
    if (room_out != NULL) *room_out = -1;
    if (resolve != NULL) room = resolve(addr, &kind);
    if (room >= 0) {
        if (room_out != NULL) *room_out = room;
        return (kind == 2) ? "room-secondary" : "room-primary";
    }
    return "unknown";
}

/* ---- LIVE OWNER BUG MARK: complete submission provenance -----------------
 *
 * Every G_DL the interpreter executed during the marked frame, in execution
 * order, with the control-flow relationship between them and the triangles
 * each one emitted with no deeper submission running.
 *
 * The integrity block comes FIRST and is not optional. A provenance table is
 * evidence only if it covers the frame, and the previous one looked complete
 * while describing a fraction of it; a reader should not have to infer
 * coverage from the shape of a listing. frame-triangles == attributed +
 * unattributed is an identity by construction - mark_note_tri counts into
 * exactly one of the two - so the line that carries information is
 * `unattributed`, and a non-zero one prints PROVENANCE INCOMPLETE and why.
 *
 * TRIANGLES ONLY. The unit on both sides of the reconciliation is the emitted
 * triangle, g_mark_tris, incremented in mark_note_tri for every triangle the
 * interpreter rasterises. The 2D passes - texture rectangles and fill
 * rectangles - never reach emit_tri, so they appear on NEITHER side; they are
 * a legitimate draw class outside this system and are named in the report
 * rather than left to be discovered later as a discrepancy. */
static int sl_mark_sub_incl(int i)
{
    /* Inclusive triangle total: this node plus everything that ran under it.
     * Computed by climbing from every node to its ancestors rather than by
     * recursion over children, so a malformed parent chain cannot run away -
     * each climb is bounded by the table size. */
    int j;
    unsigned tot = 0;
    for (j = 0; j < g_mark_sub_n; j++) {
        int p = j, hops = 0;
        while (p >= 0 && hops++ <= g_mark_sub_n) {
            if (p == i) { tot += g_mark_sub[j].tris; break; }
            p = g_mark_sub[p].parent;
        }
    }
    return (int) tot;
}

int sl_mark_subs_render(char *out, int n, int (*resolve)(unsigned addr, int *kind));
int sl_mark_subs_render(char *out, int n, int (*resolve)(unsigned addr, int *kind))
{
    int at = 0, i;
    unsigned drew = 0, empty = 0;
    int complete;

    if (out == NULL || n < 64) return 0;
    out[0] = 0;

    complete = (g_mark_tris_lost == 0 && g_mark_sub_lost == 0);

    at = mark_cat(out, n, at,
        "[submitted-lists]\n"
        "Every G_DL the interpreter EXECUTED during the marked frame, at every\n"
        "nesting level, in execution order. Not the same question as the room\n"
        "request above: a room can be requested and never submitted, and a list\n"
        "can be submitted and emit nothing. Both are visible here.\n"
        "\n"
        "CONTROL FLOW, per Display Lists and Object Generation/ucode05.txt\n"
        "l.240-246 and l.437-440. Opcode 06 has two forms and they are NOT the\n"
        "same relationship:\n"
        "  call    xx00xxxx  push display list - the running list is suspended\n"
        "                    and RESUMES after the 06. Consumes a stack level.\n"
        "  branch  xx01xxxx  branch to display list - the running list is\n"
        "                    REPLACED and does not resume. Consumes NO stack\n"
        "                    level, so a branch is a SIBLING of the list it\n"
        "                    replaced, not a child of it.\n"
        "  frame             the list the task handed the interpreter.\n"
        "call-level is the RSP stack level; walk-depth is the recursion level\n"
        "this interpreter reached. The branch form is now iterated rather\n"
        "than recursed, so the two should now AGREE on every row; a\n"
        "disagreement is a finding about this interpreter, not about the list.\n"
        "\n"
        "tris is EXCLUSIVE: triangles emitted with this submission innermost.\n"
        "tris-incl adds everything that ran underneath it. The exclusive column\n"
        "sums to frame-triangles, which is the integrity check below.\n"
        "\n"
        "[provenance-integrity]\n"
        "provenance-complete   %s\n"
        "frame-triangles       %u   (every triangle the interpreter emitted)\n"
        "attributed-triangles  %u\n"
        "unattributed          %u\n"
        "submissions-recorded  %d  (cap %d)\n"
        "submissions-lost      %u\n"
        "calls                 %u   (06 push form)\n"
        "tail-transfers        %u   (06 branch form)\n"
        "max-call-level        %d\n"
        "max-walk-depth        %d   (walk() refuses to descend past 8)\n"
        "outside-this-system   2D texture and fill rectangles. They never reach\n"
        "                      emit_tri, so they are on neither side of the\n"
        "                      reconciliation above and are not missing from it.\n",
        complete ? "YES" : "NO",
        g_mark_tris, g_mark_tris_attr, g_mark_tris_lost,
        g_mark_sub_n, SL_MARK_SUBS, g_mark_sub_lost,
        g_mark_calls, g_mark_branches,
        (int) g_mark_call_max, (int) g_mark_wdepth_max);

    if (!complete)
        at = mark_cat(out, n, at,
            "\n*** PROVENANCE INCOMPLETE ***\n"
            "  %u submission(s) exceeded the %d-entry table and were not\n"
            "  recorded, and %u triangle(s) executed underneath them and could\n"
            "  not be attributed. The listing below is a FLOOR, not the frame.\n"
            "  Do not read an absence from it.\n",
            g_mark_sub_lost, SL_MARK_SUBS, g_mark_tris_lost);

    if (g_mark_sub_n == 0) {
        at = mark_cat(out, n, at,
            "\nNO SUBMISSION WAS RECORDED AT ALL - not even the frame list.\n"
            "The mark did not arm on a walk, which is a defect in the mark and\n"
            "not a finding about the frame.\n");
        return at;
    }

    at = mark_cat(out, n, at,
        "\n  ord  kind    call walk  dl-operand  +off     tris  tris-incl"
        "  centre  parent  what\n");

    for (i = 0; i < g_mark_sub_n && at < n - 512; i++) {
        const struct sl_mark_sub *e = &g_mark_sub[i];
        const char *kind = (e->kind == 0) ? "frame " :
                           (e->kind == 2) ? "branch" : "call  ";
        int room, kindout = 0;
        char what[96];

        if (e->tris > 0) drew++; else empty++;

        room = (resolve != NULL) ? resolve(e->addr, &kindout) : -1;
        if (room >= 0)
            snprintf(what, sizeof what, "room %d %s geometry", room,
                     (kindout == 2) ? "secondary" : "primary");
        else if (e->kind == 0)
            snprintf(what, sizeof what, "the frame list itself");
        else if ((e->addr & 0xf0000000u) == 0 && (e->addr >> 24) != 0)
            snprintf(what, sizeof what,
                     "segment %u list (src/bondconstants.h SPSEGMENT_*)",
                     (unsigned) ((e->addr >> 24) & 0x0fu));
        else
            snprintf(what, sizeof what,
                     "not in g_BgRoomInfo - not room geometry");

        at = mark_cat(out, n, at,
            "  %-4d %s  %-4d %-4d %08x    0x%04x %8u  %9d %7u  %6d  %s%s\n",
            e->ord, kind, (int) e->call, (int) e->wdepth, e->addr,
            e->off & 0xffffu, e->tris, sl_mark_sub_incl(i), e->centre,
            e->parent, what,
            (e->tris == 0) ? "  [DREW NOTHING]" : "");

        if (e->kind == 2 && e->prev >= 0 && e->prev < g_mark_sub_n
            && at < n - 200)
            at = mark_cat(out, n, at,
                "                                   tail transfer: replaced"
                " ord=%d (%08x), which does not resume\n",
                g_mark_sub[e->prev].ord, g_mark_sub[e->prev].addr);
    }

    if (i < g_mark_sub_n) {
        at = mark_cat(out, n, at,
            "  *** PROVENANCE INCOMPLETE: the report buffer filled after ord=%d."
            " %d recorded submission(s) are NOT printed. ***\n",
            (i > 0) ? g_mark_sub[i - 1].ord : 0, g_mark_sub_n - i);
        return at;
    }

    at = mark_cat(out, n, at,
        "  summary  %u submission(s) emitted at least one triangle,"
        " %u emitted none.\n", drew, empty);
    return at;
}

int sl_mark_render(char *out, int n, int (*resolve)(unsigned addr, int *kind));
int sl_mark_render(char *out, int n, int (*resolve)(unsigned addr, int *kind))
{
    int at = 0, i, j;

    if (out == NULL || n < 64) return 0;
    out[0] = '\0';

    at = mark_cat(out, n, at,
        "[renderer]\n"
        "dl-frame            %u\n"
        "interpreter-ran     %u lists\n"
        "framebuffer-decl    %ux%u\n"
        "gl-window-viewport  x=%d y=%d w=%d h=%d\n"
        "game-viewport       %s x=%d y=%d w=%d h=%d   (GL, bottom-left origin)\n"
        "rdp-scissor-last    x0=%d y0=%d x1=%d y1=%d  (ED setscissor, decoded;\n"
        "                    the frame's LAST box. Every box is applied to GL\n"
        "                    per draw since B-143 (%s) - a room's window\n"
        "                    is the [rooms] requested-windows entry above)\n"
        "projection-matrix   %.6f %.6f %.6f %.6f\n"
        "                    %.6f %.6f %.6f %.6f\n"
        "                    %.6f %.6f %.6f %.6f\n"
        "                    %.6f %.6f %.6f %.6f\n",
        g_mark_frame, g_dl_ran, g_scr_w, g_scr_h,
        g_win_vp[0], g_win_vp[1], g_win_vp[2], g_win_vp[3],
        g_vp_have ? "yes" : "NOT ESTABLISHED",
        g_vp_rect[0], g_vp_rect[1], g_vp_rect[2], g_vp_rect[3],
        g_sciss[0], g_sciss[1], g_sciss[2], g_sciss[3],
        sciss_on() ? "on" : "OFF, SL_SCISSOR=0",
        (double) g_proj[0],  (double) g_proj[1],  (double) g_proj[2],  (double) g_proj[3],
        (double) g_proj[4],  (double) g_proj[5],  (double) g_proj[6],  (double) g_proj[7],
        (double) g_proj[8],  (double) g_proj[9],  (double) g_proj[10], (double) g_proj[11],
        (double) g_proj[12], (double) g_proj[13], (double) g_proj[14], (double) g_proj[15]);

    /* The projection's own near/far, recovered from the matrix rather than
     * from the game's VideoSettings, so the two can be compared. For a
     * standard perspective matrix in this layout, m[10] = -(f+n)/(f-n) and
     * m[14] = -2fn/(f-n). Printed only when the matrix looks perspective. */
    if (g_proj[11] != 0.0f && g_proj[10] != 0.0f) {
        double A = (double) g_proj[10], B = (double) g_proj[14];
        double den = A - 1.0;
        if (den != 0.0) {
            double nz = B / den, fz = B / (A + 1.0);
            at = mark_cat(out, n, at,
                "proj-derived-near   %.3f\nproj-derived-far    %.3f\n", nz, fz);
        }
    }

    at = mark_cat(out, n, at,
        "\ncentre-box-extent   %.4f  (ndc x,y in [%.4f, %.4f]; SL_MARK_BOX)\n"
        "triangles-in-frame  %u\n"
        "triangles-at-centre %u\n"
        "records-kept        %d  (cap %d)\n",
        (double) mark_box(), -(double) mark_box(), (double) mark_box(),
        g_mark_tris, g_mark_hit, g_mark_n, SL_MARK_TRIS);
    at = mark_cat(out, n, at,
        "redraw-path         %s  (B-125; depth %d bits, stencil %d bits)\n"
        "redraw-loads        %u of %u G_VTX loads re-loaded only positions their\n"
        "                    submission had already drawn\n"
        "redraw-triangles    %u re-triangulated (drawn with their quad's own tent\n"
        "                    difference as tolerance; %u of those with the exact-z\n"
        "                    stencil pass), %u same-triangulation or unrecognised\n"
        "                    (drawn plainly)\n",
        (!redraw_on()) ? "OFF (SL_Z_REDRAW=0)"
                       : (g_rd_stencil_bits > 0 ? "on" : "INERT (no stencil plane)"),
        g_rd_depth_bits, g_rd_stencil_bits,
        g_rd_loads_old, g_rd_loads, g_rd_tris, g_rd_tris_z, g_rd_tris_same);

    at = mark_cat(out, n, at, "\n[candidate-draws]\n");

    if (g_mark_tris == 0) {
        at = mark_cat(out, n, at,
            "NO TRIANGLES WERE EMITTED AT ALL in the marked frame.\n"
            "  The display-list interpreter ran but rasterised no geometry.\n"
            "  This is not 'nothing at the centre' - it is 'nothing anywhere',\n"
            "  and it points at the list or the walk, not at the centre box.\n");
        return at;
    }
    if (g_mark_hit == 0) {
        at = mark_cat(out, n, at,
            "NO SUBMITTED TRIANGLE INTERSECTS THE CENTRE BOX.\n"
            "  %u triangles were emitted this frame and none of them projected\n"
            "  onto the centre of the screen. Whatever the owner is looking at\n"
            "  did not reach the renderer as world geometry: it is backdrop\n"
            "  fill, a 2D pass, or geometry that was never submitted. The\n"
            "  [centre-pixel] section says what colour it actually is.\n"
            "  No identity is invented here - there is none to give.\n",
            g_mark_tris);
        return at;
    }

    /* THIS IS A FILTER, NOT A RASTERISER, and it says so where it will be
     * read. A triangle is listed when the projected bounding box over its
     * in-front vertices meets the centre box, which over-reports: a large
     * triangle straddling the centre is listed even when the centre pixel
     * belongs to something else, and source vertices may sit far outside the
     * viewport. Over-reporting is the safe direction - the alternative is a
     * mark that silently omits the one draw being hunted. */
    at = mark_cat(out, n, at,
        "CANDIDATES, in submission order. This is a BOUNDING-BOX FILTER over\n"
        "the centre box - not fragment ownership. A listed triangle covers the\n"
        "centre pixel, or merely straddles it. Nearest first by eye distance:\n"
        "  ");
    {   /* insertion sort over at most SL_MARK_TRIS entries, nearest e0 first */
        int ord[SL_MARK_TRIS], cnt = 0;
        for (i = 0; i < g_mark_n; i++) {
            for (j = cnt; j > 0 && g_mark_t[ord[j - 1]].e0 > g_mark_t[i].e0; j--)
                ord[j] = ord[j - 1];
            ord[j] = i; cnt++;
        }
        for (i = 0; i < cnt && i < 24; i++)
            at = mark_cat(out, n, at, "tri=%u(%.0f) ",
                          g_mark_t[ord[i]].tri, (double) g_mark_t[ord[i]].e0);
        at = mark_cat(out, n, at, "%s\n", cnt > 24 ? "..." : "");
    }

    for (i = 0; i < g_mark_n; i++) {
        const struct sl_mark_tri *m = &g_mark_t[i];
        int room = -1;
        const char *cls = mark_class(m->dl1, resolve, &room);
        char roombuf[48];

        if (room >= 0)
            snprintf(roombuf, sizeof roombuf, " room=%d", room);
        else if ((m->dl1 & 0xf0000000u) == 0 && (m->dl1 >> 24) != 0)
            snprintf(roombuf, sizeof roombuf, " segment=%u",
                     (unsigned) ((m->dl1 >> 24) & 0x0fu));
        else
            roombuf[0] = '\0';

        at = mark_cat(out, n, at,
            "\n  seq=%-6u class=%s%s\n"
            "    dl-top      %08x + 0x%04x   depth-1 list, and the byte offset\n"
            "                                of the triangle command within it\n"
            "    dl-inner    %08x + 0x%04x   nesting-depth=%d submission-ord=%d\n"
            "    ndc         x[%7.4f,%7.4f] y[%7.4f,%7.4f] z[%8.4f,%8.4f]\n"
            "    eye-dist    [%10.2f,%10.2f]  vertices-in-front=%d/3\n",
            m->tri, cls, roombuf,
            m->dl1, m->dl1_off & 0xffffu,
            m->dlin, m->dlin_off & 0xffffu, m->depth,
            (m->sub >= 0 && m->sub < g_mark_sub_n) ? g_mark_sub[m->sub].ord : -1,
            (double) m->x0, (double) m->x1,
            (double) m->y0, (double) m->y1,
            (double) m->z0, (double) m->z1,
            (double) m->e0, (double) m->e1, m->front);

        for (j = 0; j < 3; j++) {
            at = mark_cat(out, n, at,
                "    v%d slot=%-2d src=%08x+%u  G_VTX @+0x%04x of %08x  host=%08lx\n"
                "       source %6d %6d %6d   st %6d %6d   rgba %02x%02x%02x%02x\n"
                "       eye    %10.3f %10.3f %10.3f\n"
                "       clip   %10.3f %10.3f %10.3f w=%10.3f\n"
                "       ndc    %8.4f %8.4f %8.4f   screen-px %8.1f %8.1f\n",
                j, m->slot[j], m->vseg[j], (unsigned) m->vidx[j],
                m->voff[j] & 0xffffu, m->vdl[j],
                (unsigned long) (const char *) m->vp[j],
                (int) m->mx[j], (int) m->my[j], (int) m->mz[j],
                (int) m->vs[j], (int) m->vt[j],
                m->vr[j], m->vg[j], m->vb[j], m->va[j],
                (double) m->eye[j][0], (double) m->eye[j][1], (double) m->eye[j][2],
                (double) m->clip[j][0], (double) m->clip[j][1],
                (double) m->clip[j][2], (double) m->clip[j][3],
                (double) m->ndc[j][0], (double) m->ndc[j][1], (double) m->ndc[j][2],
                (double) m->scr[j][0], (double) m->scr[j][1]);
        }

        at = mark_cat(out, n, at,
            "    material    cc=%08x %08x cycle=%u geom=%08x om_l=%08x fog=%d\n"
            "    depth       test=%d write=%d zoffset=%d redraw=%d  (redraw: B-125 -\n"
            "                an authored re-pass over positions this submission\n"
            "                already drew; tested with the RDP tolerance, its exact\n"
            "                z written through the stencil mark)\n"
            "    blend       on=%d sfac=0x%04x dfac=0x%04x\n"
            "    cull        gl=%d (0 = none) option=%d\n"
            "    scissor     x0=%d y0=%d x1=%d y1=%d\n"
            "    viewport    x=%d y=%d w=%d h=%d\n"
            "    texture     %ux%u tile=%u fmt=%d cache-slot=%d gl=%u src=%08lx\n",
            m->cc0, m->cc1, m->cyc, m->geom, m->om_l, m->fogm,
            m->ztest, m->zwrite, m->zoff, m->redraw,
            m->blend, (unsigned) m->sfac, (unsigned) m->dfac,
            m->cull_gl, m->cull_opt,
            m->sciss[0], m->sciss[1], m->sciss[2], m->sciss[3],
            m->vprect[0], m->vprect[1], m->vprect[2], m->vprect[3],
            m->texw, m->texh, m->textile,
            m->texfmt == 0xffffffffu ? -1 : (int) m->texfmt,
            m->texcache_slot, m->texgl,
            (unsigned long) (const char *) m->texsrc);
        at = mark_cat(out, n, at,
            "    cull-witness form=%s cmd=%08x %08x path=%s%s%s%s\n"
            "                ndc-area(cmd order, y-up)=%+.6f -> %s  "
            "sense-1-drops=%d  authored=%s%s\n",
            m->form == 4 ? "BF-tri1" :
            m->form == 0 ? "B1-slot0" : m->form == 1 ? "B1-slot1" :
            m->form == 2 ? "B1-slot2" : "B1-slot3",
            m->cw0, m->cw1,
            m->cls == 0 ? "ordinary" : "fan",
            (m->cls & 1) ? "+near" : "", (m->cls & 2) ? "+eye" : "",
            (m->cls & 4) ? "+guard" : "",
            (double) m->area,
            m->area > 0.0f ? "CCW (front under sense 1)" :
            m->area < 0.0f ? "CW (back under sense 1)" : "degenerate/behind",
            m->drop1,
            (m->geom & GEOM_CULL_FRONT) ? "CULL_FRONT " : "",
            (m->geom & GEOM_CULL_BACK)  ? "CULL_BACK"   : "(no cull bit)");
    }
    if (g_mark_hit > (unsigned) g_mark_n)
        at = mark_cat(out, n, at,
            "\n  %u further candidates were counted and NOT recorded (cap %d).\n",
            g_mark_hit - (unsigned) g_mark_n, SL_MARK_TRIS);
    return at;
}

#endif
