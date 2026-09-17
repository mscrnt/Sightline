/**
 * GoldenEye texture decoder -> RGBA8. Decoder only; nothing here touches GL,
 * the display-list interpreter, or any global state.
 *
 * ---------------------------------------------------------------------------
 * BYTE ORDER - state it once, loudly, because getting this wrong is what cost
 * several hours on the collision-DL crash (2026-08-24).
 *
 * This decoder reads its input as a BYTE STREAM in wire (big-endian) order and
 * never casts it to u16/u32. That is not a stylistic choice, it is what the
 * data actually is:
 *
 *   - Texel data is produced byte-at-a-time by the inflaters in image.c and is
 *     never word-swapped by the native load path. Tree-wide, `grep -rln
 *     "sl_swap" src/` names exactly eight files: sl_swap_model.c,
 *     sl_swap_setup.c, model.c, prop.c, propobj.c, textrelated.c,
 *     image_bank.c, and this one. Not one of them swaps a texel or palette
 *     buffer. The two image-adjacent hits are checked below rather than
 *     waved past, because the T4 rule is "swap once at the load choke point"
 *     and a second swap here would be the exact class of bug that cost the
 *     collision-DL investigation its afternoon.
 *   - image_bank.c:180 DOES word-swap a whole region called the
 *     Globalimagetable (`sl_swap_words(pGlobalimagetable, size / 4)`), which
 *     sounds like it contradicts the above. It does not, and the distinction
 *     is worth stating plainly for whoever wires this decoder up:
 *
 *       The global image bank holds DISPLAY LISTS AND IMAGE DECLARATIONS, not
 *       pixels. "load global image bank.txt" describes it as 0x1400 bytes at
 *       ROM 0x29D160 that get scanned for rdp_settextureimage commands tagged
 *       ABCD, each naming an image to load; the sixteen globalDL_0x078 ...
 *       globalDL_0xa50 externs right above that swap in image_bank.c are the
 *       very offsets the note lists being handed to that scanner. The texels
 *       it REFERS to are decompressed separately, later, by texLoad into the
 *       texpool - and those are what this file decodes.
 *
 *       CORRECTED 2026-08-25 (B-027). This comment used to finish "structured
 *       data, so it goes to native word order like every other structured
 *       file", and that sentence was wrong in a way that cost the crosshair
 *       its texture. Structured does not imply word-structured. Only the DL
 *       half of the bank is words. The declarations are 0xC bytes of which
 *       just the leading u32 is a word ("image declarations.txt"): bytes
 *       0x4..0xB are eight independent u8 fields - width, height, level,
 *       format, bitdepth, clamp/mirror S, clamp/mirror T, pad. A blanket
 *       word swap reverses each group of four, and texSelect then read the
 *       crosshair's 32x32 RGBA32 level-0 image as 0x0, format 0x20, level 32
 *       - zero-area tile, mipmap branch, untextured white square.
 *       image_bank.c now swaps the two halves of the bank separately. The
 *       general rule this is an instance of: swapping "at the load choke
 *       point" still requires knowing the field widths at that point.
 *
 *     So: never hand sl_tex_decode a pointer into the global image bank and
 *     expect pixels. It will happily decode a display list into noise.
 *   - textrelated.c's two hits swap `struct font` (kerning table plus
 *     fontchar records) - again structure, not glyph bitmaps.
 *   - Palette entries are written BYTE-WISE big-endian by texInflate*:
 *         dst[n + 0] = palette[i] >> 8;
 *         dst[n + 1] = palette[i] & 0xff;
 *     so a 5551 entry is {high byte, low byte} on every host.
 *
 * So the T4 convention ("native word order everywhere, swap once at load")
 * applies to STRUCTURED files - model files, setups, display lists - and this
 * buffer is not one of those. It is opaque pixel bytes. Reading it byte-wise
 * is correct on N64 and on x86 alike, and stays correct no matter what the
 * DL/texture parse path decides to swap later. If some future load pass ever
 * word-swaps texel data, this file must change and this comment is the flag.
 *
 * The one place word order does show up is the odd-row swizzle below, and that
 * is a permutation of 4-byte groups - it never interprets a word's value, so
 * it too is endian-agnostic.
 * ---------------------------------------------------------------------------
 * WHERE THE FORMAT COMES FROM
 *
 * Notes, consulted first per project rule 7 (root: the goldeneye_docs
 * clone, notes/GE Documentation):
 *
 *   images text and font/Huffman/huffman compression types.txt
 *       The 13 image type codes 0..C and, in its seven value tables, #5 "N64
 *       image types (0=color, 1=YUV, 2=indexed, 3=IA, 4=I)" and #6 "N64 pixel
 *       sizes (0=4bit, 1=8bit, 2=16bit, 3=32bit)". Those two tables are what
 *       fixes the depth of each code, including the two non-obvious ones:
 *       type 2 is "24bit color image, forced to 32bit fixed alpha" and type 3
 *       is "15bit color image, with forced alpha" - both are STORED at the
 *       wider depth, so they decode exactly like RGBA32 and RGBA16.
 *   images text and font/image declarations.txt
 *       The declaration record and the format/bitdepth nibbles (0 rgba, 1 yuv,
 *       2 ci, 3 ia, 4 i; 0 4bit, 1 8bit, 2 16bit, 3 32bit), and the C0
 *       pseudo-command that names an image by ID.
 *   images text and font/load global image bank.txt
 *       The global image bank: 0x1400 bytes at ROM 0x29D160, loaded to bank 4,
 *       then walked for rdp_settextureimage commands tagged ABCD, each of
 *       which names an image declaration to load. So the "global image table"
 *       is a table of DECLARATIONS, not of pixels.
 *   note on compression.txt
 *       Textures are compressed: Rare's "1172" zlib variant for the paletted
 *       ones, plus a bespoke huffman scheme with its own nine methods. Both
 *       are decompressed before anything reaches this file - see the scope
 *       note below.
 *   Display Lists and Object Generation/Objects/Texture application (Rooms).txt
 *       The BB/level -> assembled image size table (level 5 = 0x800 = a 32x32
 *       16bit image, max level 7 = 0x2000), useful as a sanity bound.
 *
 * NOT IN THE NOTES - the odd-row swizzle. `gedocs.py search swizzle` returns
 * "no filename matches", and a content grep over the whole corpus for
 * interleav|swizzl|odd row|word.?swap turns up nothing about textures. The
 * corpus documents GoldenEye's structures, not the RDP's TMEM addressing. The
 * authority used instead is the decomp's own texSwapAltRowBytes()
 * (src/game/image.c:2168), which is Rare's implementation of it, and the
 * closest the notes come is huffman compression types.txt's value table #7,
 * "imageflip values for indexed types" (0x8000 / 0xC000). This is recorded as
 * a not_covered entry in docs/doc-routing.json.
 *
 * The rule, transcribed from texSwapAltRowBytes:
 *   - Rows are padded: to 4 texels for 32bpp and 16bpp, 8 for 8bpp, 16 for
 *     4bpp. Every row is therefore a whole number of 32-bit words, and an EVEN
 *     one (a multiple of four for 32bpp).
 *   - Even rows are stored plainly.
 *   - On ODD rows the 32-bit words are swapped in place: for 4/8/16bpp, word
 *     w trades with word w^1; for the 32bpp formats, with word w^2.
 *   - The operation is its own inverse, which is why decoding just applies the
 *     same permutation again.
 * texAlignIndices (image.c:317) independently confirms the padding: it rounds
 * each output row up with `(outptr + 7) & ~7`.
 *
 * SCOPE: decompression is not here. texLoad/texInflateZlib/texInflateNonZlib
 * already turn a compressed image into the padded, row-swapped, byte-order
 * neutral buffer this file consumes. Feeding raw compressed ROM bytes in will
 * produce garbage pixels - not a crash, but garbage.
 */
#ifndef __sgi

#include "sl_gfx_tex.h"

/* ------------------------------------------------------------------ */
/* format tables                                                      */

const char *sl_tex_strerror(int err)
{
    switch (err) {
    case SL_TEX_OK:            return "ok";
    case SL_TEX_ERR_ARGS:      return "null argument";
    case SL_TEX_ERR_FORMAT:    return "unknown texture format code";
    case SL_TEX_ERR_DIM:       return "bad dimensions";
    case SL_TEX_ERR_SHORT_SRC: return "source buffer shorter than the format requires";
    case SL_TEX_ERR_SHORT_PAL: return "palette buffer too small for an index used";
    case SL_TEX_ERR_SHORT_DST: return "destination buffer too small";
    default:                   return "unknown error";
    }
}

unsigned sl_tex_bpp(int fmt)
{
    switch (fmt) {
    case SL_TEX_RGBA32:
    case SL_TEX_RGB24:
        return 32;
    case SL_TEX_RGBA16:
    case SL_TEX_RGB15:
    case SL_TEX_IA16:
    case SL_TEX_CI16_RGBA16:
    case SL_TEX_CI16_IA16:
        return 16;
    case SL_TEX_IA8:
    case SL_TEX_I8:
    case SL_TEX_CI8_RGBA16:
    case SL_TEX_CI8_IA16:
        return 8;
    case SL_TEX_IA4:
    case SL_TEX_I4:
    case SL_TEX_CI4_RGBA16:
    case SL_TEX_CI4_IA16:
        return 4;
    default:
        return 0;
    }
}

unsigned sl_tex_palette_entries(int fmt)
{
    switch (fmt) {
    case SL_TEX_CI8_RGBA16:
    case SL_TEX_CI8_IA16:
    case SL_TEX_CI16_RGBA16:
    case SL_TEX_CI16_IA16:
        return 256;
    case SL_TEX_CI4_RGBA16:
    case SL_TEX_CI4_IA16:
        return 16;
    default:
        return 0;
    }
}

/*
 * Row stride in bytes, padded exactly as texSwapAltRowBytes assumes.
 * Its widths are expressed in 32-bit words:
 *      32bpp   (width + 3)  & 0xffc
 *      16bpp  ((width + 3)  & 0xffc) >> 1
 *       8bpp  ((width + 7)  & 0xff8) >> 2
 *       4bpp  ((width + 15) & 0xff0) >> 3
 * The masks there also truncate widths past 4095/2047/4095 texels; that is a
 * Rare quirk with no reachable effect (struct tex holds width in a u8) and is
 * not reproduced - SL_TEX_MAX_DIM rejects those widths outright instead.
 */
unsigned sl_tex_row_stride(int fmt, int width)
{
    unsigned w;

    if (width <= 0 || width > SL_TEX_MAX_DIM)
        return 0;

    w = (unsigned) width;

    switch (sl_tex_bpp(fmt)) {
    case 32: return ((w + 3u)  & ~3u) * 4u;   /* multiple of 16 */
    case 16: return ((w + 3u)  & ~3u) * 2u;   /* multiple of 8  */
    case 8:  return  (w + 7u)  & ~7u;         /* multiple of 8  */
    case 4:  return ((w + 15u) & ~15u) >> 1;  /* multiple of 8  */
    default: return 0;
    }
}

unsigned sl_tex_data_size(int fmt, int width, int height)
{
    unsigned stride = sl_tex_row_stride(fmt, width);

    if (stride == 0 || height <= 0 || height > SL_TEX_MAX_DIM)
        return 0;

    return stride * (unsigned) height;
}

/* ------------------------------------------------------------------ */
/* channel widening                                                   */

static unsigned char sl_x5to8(unsigned v)
{
    v &= 0x1fu;
    return (unsigned char) ((v << 3) | (v >> 2));
}

static unsigned char sl_x4to8(unsigned v)
{
    v &= 0xfu;
    return (unsigned char) (v * 0x11u);
}

static unsigned char sl_x3to8(unsigned v)
{
    v &= 7u;
    return (unsigned char) ((v << 5) | (v << 2) | (v >> 1));
}

static void sl_put(unsigned char *p, unsigned r, unsigned g, unsigned b, unsigned a)
{
    p[0] = (unsigned char) r;
    p[1] = (unsigned char) g;
    p[2] = (unsigned char) b;
    p[3] = (unsigned char) a;
}

/* 5551 -> RGBA8. The bit split is Rare's own, read straight off the palette
 * arithmetic in texShrinkPaletted (image.c): r = (c >> 11) & 0x1f,
 * g = (c >> 6) & 0x1f, b = (c >> 1) & 0x1f, a = c & 1. */
static void sl_put_rgba16(unsigned char *p, unsigned c)
{
    sl_put(p, sl_x5to8(c >> 11), sl_x5to8(c >> 6), sl_x5to8(c >> 1),
           (c & 1u) ? 0xffu : 0x00u);
}

/* I8A8. Same source: c = (colour >> 8) & 0xff is intensity, colour & 0xff is
 * alpha. */
static void sl_put_ia16(unsigned char *p, unsigned c)
{
    unsigned i = (c >> 8) & 0xffu;
    sl_put(p, i, i, i, c & 0xffu);
}

/* ------------------------------------------------------------------ */
/* decode                                                             */

/*
 * Byte offset within a row, after undoing the odd-row word swap.
 *
 * xorword is 0 on even rows and on unswapped data; 1 for 4/8/16bpp odd rows;
 * 2 for 32bpp odd rows. Because every padded row holds an even number of
 * words (a multiple of four at 32bpp), (word ^ xorword) can never leave the
 * row - so this cannot escape the stride, and the caller's bounds check on
 * stride * height is sufficient for the whole decode.
 */
static unsigned sl_tex_phys(unsigned logical, unsigned xorword)
{
    return (((logical >> 2) ^ xorword) << 2) | (logical & 3u);
}

unsigned sl_tex_pal_wrapped;

/* B-141. Resolve a CI index against the loaded entry count. Returns the
 * entry to read, or -1 when the index is out of range and the caller did not
 * ask for the wrap (SL_TEX_FLAG_PAL_WRAP). A palette with NO readable entry
 * is out of range whatever the flags say - there is nothing to wrap into. */
static int sl_tex_pal_index(unsigned idx, unsigned palentries, unsigned flags)
{
    if (idx < palentries)
        return (int) idx;
    if (palentries == 0u || !(flags & SL_TEX_FLAG_PAL_WRAP))
        return -1;
    sl_tex_pal_wrapped++;
    return (int) (idx % palentries);
}

int sl_tex_decode(const void *src, unsigned srcbytes,
                  int fmt, int width, int height, unsigned flags,
                  const void *palette, unsigned palbytes,
                  unsigned char *out_rgba, unsigned outbytes)
{
    const unsigned char *in = (const unsigned char *) src;
    const unsigned char *pal = (const unsigned char *) palette;
    unsigned bpp, stride, need, palentries, y, x;
    int swapped;

    if (src == 0 || out_rgba == 0)
        return SL_TEX_ERR_ARGS;

    bpp = sl_tex_bpp(fmt);
    if (bpp == 0)
        return SL_TEX_ERR_FORMAT;

    if (width <= 0 || height <= 0 ||
        width > SL_TEX_MAX_DIM || height > SL_TEX_MAX_DIM)
        return SL_TEX_ERR_DIM;

    stride = sl_tex_row_stride(fmt, width);
    need   = stride * (unsigned) height;

    if (srcbytes < need)
        return SL_TEX_ERR_SHORT_SRC;

    if (outbytes < (unsigned) width * (unsigned) height * 4u)
        return SL_TEX_ERR_SHORT_DST;

    palentries = sl_tex_palette_entries(fmt);
    if (palentries != 0 && pal == 0)
        return SL_TEX_ERR_ARGS;

    /* Cap the addressable entry count by what the caller says is readable.
     * A texture is allowed to carry fewer than the full 16/256 entries -
     * GoldenEye stores exactly numcolours of them (struct tex.unk0a + 1) -
     * so a short palette is normal, and only an index that actually reaches
     * past it is an error. */
    if (palentries != 0) {
        unsigned avail = palbytes >> 1;
        if (avail < palentries)
            palentries = avail;
    }

    swapped = (flags & SL_TEX_FLAG_ROWSWAPPED) != 0;
    sl_tex_pal_wrapped = 0;

    for (y = 0; y < (unsigned) height; y++) {
        const unsigned char *row = in + stride * y;
        unsigned char *dst = out_rgba + (unsigned) width * 4u * y;
        unsigned xw = 0;

        if (swapped && (y & 1u))
            xw = (bpp == 32) ? 2u : 1u;

        for (x = 0; x < (unsigned) width; x++) {
            unsigned idx, c, v, off;

            switch (bpp) {
            case 32:
                off = sl_tex_phys(x * 4u, xw);
                /* off is word-aligned here, so the whole texel lives in one
                 * word and a single mapped offset covers all four bytes. */
                sl_put(&dst[x * 4u], row[off], row[off + 1], row[off + 2],
                       row[off + 3]);
                break;

            case 16:
                off = sl_tex_phys(x * 2u, xw);
                c = ((unsigned) row[off] << 8) | row[off + 1];
                if (fmt == SL_TEX_CI16_RGBA16 || fmt == SL_TEX_CI16_IA16) {
                    /* The LUT-enabled 16-bit fetch: the texel's high byte
                     * is the palette index (sl_gfx_tex.h). */
                    {   int e = sl_tex_pal_index(c >> 8, palentries, flags);
                        if (e < 0)
                            return SL_TEX_ERR_SHORT_PAL;
                        idx = (unsigned) e;
                    }
                    c = ((unsigned) pal[idx * 2u] << 8) | pal[idx * 2u + 1u];
                    if (fmt == SL_TEX_CI16_IA16)
                        sl_put_ia16(&dst[x * 4u], c);
                    else
                        sl_put_rgba16(&dst[x * 4u], c);
                } else if (fmt == SL_TEX_IA16)
                    sl_put_ia16(&dst[x * 4u], c);
                else
                    sl_put_rgba16(&dst[x * 4u], c);
                break;

            case 8:
                off = sl_tex_phys(x, xw);
                v = row[off];
                if (fmt == SL_TEX_I8) {
                    sl_put(&dst[x * 4u], v, v, v, v);
                } else if (fmt == SL_TEX_IA8) {
                    unsigned i = sl_x4to8(v >> 4);
                    sl_put(&dst[x * 4u], i, i, i, sl_x4to8(v));
                } else {
                    int e = sl_tex_pal_index(v, palentries, flags);
                    if (e < 0)
                        return SL_TEX_ERR_SHORT_PAL;
                    idx = (unsigned) e;
                    c = ((unsigned) pal[idx * 2u] << 8) | pal[idx * 2u + 1u];
                    if (fmt == SL_TEX_CI8_IA16)
                        sl_put_ia16(&dst[x * 4u], c);
                    else
                        sl_put_rgba16(&dst[x * 4u], c);
                }
                break;

            default: /* 4 */
                off = sl_tex_phys(x >> 1, xw);
                /* High nibble is the left texel. From texShrinkPaletted:
                 * `(src8[j >> 1] >> 4) & 0xf` is pixel j, the low nibble is
                 * pixel j+1. */
                v = (x & 1u) ? (row[off] & 0xfu) : (row[off] >> 4);
                if (fmt == SL_TEX_I4) {
                    unsigned i = sl_x4to8(v);
                    sl_put(&dst[x * 4u], i, i, i, i);
                } else if (fmt == SL_TEX_IA4) {
                    /* 3 bits intensity, 1 bit alpha (image.h). */
                    unsigned i = sl_x3to8(v >> 1);
                    sl_put(&dst[x * 4u], i, i, i, (v & 1u) ? 0xffu : 0x00u);
                } else {
                    int e = sl_tex_pal_index(v, palentries, flags);
                    if (e < 0)
                        return SL_TEX_ERR_SHORT_PAL;
                    idx = (unsigned) e;
                    c = ((unsigned) pal[idx * 2u] << 8) | pal[idx * 2u + 1u];
                    if (fmt == SL_TEX_CI4_IA16)
                        sl_put_ia16(&dst[x * 4u], c);
                    else
                        sl_put_rgba16(&dst[x * 4u], c);
                }
                break;
            }
        }
    }

    return SL_TEX_OK;
}

/* ====================================================================== */
/* Self-test. Build and run with:
 *     gcc -m32 -std=gnu89 -Wall -DSL_TEX_SELFTEST src/gfx/sl_gfx_tex.c \
 *         -o build/native/sl_tex_selftest && build/native/sl_tex_selftest
 * Every fixture below is SYNTHESIZED in code. No ROM bytes, ever - see
 * project rules, non-negotiable 2.
 */
#ifdef SL_TEX_SELFTEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
static int g_checks;

static void ck(int cond, const char *what)
{
    g_checks++;
    if (!cond) {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}

static void ck_px(const unsigned char *p, int r, int g, int b, int a,
                  const char *what)
{
    g_checks++;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        g_fail++;
        printf("  FAIL  %s: got %u,%u,%u,%u want %d,%d,%d,%d\n",
               what, p[0], p[1], p[2], p[3], r, g, b, a);
    }
}

/*
 * Independent transcription of texSwapAltRowBytes (src/game/image.c:2168),
 * used to BUILD swizzled fixtures. The decoder must be its exact inverse,
 * which is the point of testing with it rather than against it.
 */
static void ref_swap_alt_rows(unsigned char *dst, int width, int height, int fmt)
{
    unsigned int *row = (unsigned int *) dst;
    int alignedwidth, x, y;
    unsigned int tmp;

    switch (sl_tex_bpp(fmt)) {
    case 32: alignedwidth = (width + 3) & ~3; break;
    case 16: alignedwidth = ((width + 3) & ~3) >> 1; break;
    case 8:  alignedwidth = ((width + 7) & ~7) >> 2; break;
    default: alignedwidth = ((width + 15) & ~15) >> 3; break;
    }

    row += alignedwidth;

    if (sl_tex_bpp(fmt) == 32) {
        for (y = 1; y < height; y += 2) {
            for (x = 0; x < alignedwidth; x += 4) {
                tmp = row[x + 0]; row[x + 0] = row[x + 2]; row[x + 2] = tmp;
                tmp = row[x + 1]; row[x + 1] = row[x + 3]; row[x + 3] = tmp;
            }
            row += alignedwidth * 2;
        }
    } else {
        for (y = 1; y < height; y += 2) {
            for (x = 0; x < alignedwidth; x += 2) {
                tmp = row[x + 0]; row[x + 0] = row[x + 1]; row[x + 1] = tmp;
            }
            row += alignedwidth * 2;
        }
    }
}

/* Write a big-endian 16-bit value. */
static void be16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char) (v >> 8);
    p[1] = (unsigned char) v;
}

#define W 5      /* deliberately not a multiple of the padding quantum, so */
#define H 4      /* every format exercises row padding                     */

/* Backed by u32 so the reference swapper below - which walks the buffer as
 * unsigned int *, exactly as texSwapAltRowBytes does - is always aligned. */
static unsigned int  g_src32[SL_TEX_MAX_DIM / 4];
static unsigned char *const g_src = (unsigned char *) g_src32;
#define SRCBYTES SL_TEX_MAX_DIM

static unsigned char g_out[W * H * 4];

/* Two synthesized palettes, chosen so the expected RGBA is trivially
 * hand-checkable: the 5551 one is a grey ramp with alpha in bit 0, the I8A8
 * one puts the index straight in the intensity byte. */
static unsigned char g_pal_rgba[512];   /* 5551: r=g=b=i&0x1f, a=i&1      */
static unsigned char g_pal_ia[512];     /* I8A8: I=i, A=(i*3)&0xff        */

static const unsigned char *pal_for(int fmt)
{
    if (fmt == SL_TEX_CI8_IA16 || fmt == SL_TEX_CI4_IA16 ||
        fmt == SL_TEX_CI16_IA16)
        return g_pal_ia;
    return g_pal_rgba;
}

/* Fill the padded rows with a recognisable value so any decode that strays
 * into padding shows up as a wrong pixel rather than a plausible one. */
static void fill_pad(int fmt)
{
    memset(g_src, 0xa5, SRCBYTES);
    (void) fmt;
}

static void test_rgba32(int swizzle)
{
    unsigned stride = sl_tex_row_stride(SL_TEX_RGBA32, W);
    unsigned x, y;
    int rc;

    fill_pad(SL_TEX_RGBA32);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned char *p = g_src + stride * y + x * 4;
            p[0] = (unsigned char) (x * 16 + 1);
            p[1] = (unsigned char) (y * 16 + 2);
            p[2] = (unsigned char) (x * 16 + y);
            p[3] = 0xff;
        }
    if (swizzle)
        ref_swap_alt_rows(g_src, W, H, SL_TEX_RGBA32);

    rc = sl_tex_decode(g_src, stride * H, SL_TEX_RGBA32, W, H,
                       swizzle ? SL_TEX_FLAG_ROWSWAPPED : 0,
                       0, 0, g_out, sizeof(g_out));
    ck(rc == SL_TEX_OK, "rgba32 decode returns ok");
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            ck_px(g_out + (y * W + x) * 4, x * 16 + 1, y * 16 + 2,
                  (x * 16 + y) & 0xff, 0xff, "rgba32 texel");
}

static void test_rgba16(int swizzle)
{
    unsigned stride = sl_tex_row_stride(SL_TEX_RGBA16, W);
    unsigned x, y;
    int rc;

    fill_pad(SL_TEX_RGBA16);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            be16(g_src + stride * y + x * 2,
                 ((x & 0x1f) << 11) | ((y & 0x1f) << 6) |
                 (((x + y) & 0x1f) << 1) | ((x + y) & 1));
    if (swizzle)
        ref_swap_alt_rows(g_src, W, H, SL_TEX_RGBA16);

    rc = sl_tex_decode(g_src, stride * H, SL_TEX_RGBA16, W, H,
                       swizzle ? SL_TEX_FLAG_ROWSWAPPED : 0,
                       0, 0, g_out, sizeof(g_out));
    ck(rc == SL_TEX_OK, "rgba16 decode returns ok");
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned r = (x & 0x1f), g = (y & 0x1f), b = ((x + y) & 0x1f);
            ck_px(g_out + (y * W + x) * 4,
                  (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2),
                  ((x + y) & 1) ? 0xff : 0x00, "rgba16 texel");
        }
}

static void test_ia16(int swizzle)
{
    unsigned stride = sl_tex_row_stride(SL_TEX_IA16, W);
    unsigned x, y;
    int rc;

    fill_pad(SL_TEX_IA16);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            be16(g_src + stride * y + x * 2, ((x * 17) << 8) | (y * 37));
    if (swizzle)
        ref_swap_alt_rows(g_src, W, H, SL_TEX_IA16);

    rc = sl_tex_decode(g_src, stride * H, SL_TEX_IA16, W, H,
                       swizzle ? SL_TEX_FLAG_ROWSWAPPED : 0,
                       0, 0, g_out, sizeof(g_out));
    ck(rc == SL_TEX_OK, "ia16 decode returns ok");
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            ck_px(g_out + (y * W + x) * 4, x * 17, x * 17, x * 17,
                  (y * 37) & 0xff, "ia16 texel");
}

/* The LUT-enabled 16-bit fetch. The fixture is the Frigate shape: CI8 index
 * bytes laid out as 16-bit texels, so texel x carries indices (2x, 2x+1) and
 * the decode must palettise the HIGH byte (2x) and ignore the low one. */
static void test_ci16(int fmt, int swizzle, const char *name)
{
    unsigned stride = sl_tex_row_stride(fmt, W);
    unsigned x, y;
    int rc;

    fill_pad(fmt);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            be16(g_src + stride * y + x * 2,
                 (((y * W + x) & 0xff) << 8) | 0xee);   /* low byte: a trap */
    if (swizzle)
        ref_swap_alt_rows(g_src, W, H, fmt);

    rc = sl_tex_decode(g_src, stride * H, fmt, W, H,
                       swizzle ? SL_TEX_FLAG_ROWSWAPPED : 0,
                       pal_for(fmt), 512, g_out, sizeof(g_out));
    ck(rc == SL_TEX_OK, name);

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned v = (y * W + x) & 0xff;
            unsigned char *p = g_out + (y * W + x) * 4;
            if (fmt == SL_TEX_CI16_RGBA16) {
                unsigned c = v & 0x1f, e = (c << 3) | (c >> 2);
                ck_px(p, e, e, e, (v & 1) ? 0xff : 0, "ci16/rgba16 texel");
            } else {
                ck_px(p, v, v, v, (v * 3) & 0xff, "ci16/ia16 texel");
            }
        }
}

static void test_8bit(int fmt, int swizzle, const char *name)
{
    unsigned stride = sl_tex_row_stride(fmt, W);
    unsigned x, y;
    int rc;

    fill_pad(fmt);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            g_src[stride * y + x] = (unsigned char) (y * W + x);
    if (swizzle)
        ref_swap_alt_rows(g_src, W, H, fmt);

    rc = sl_tex_decode(g_src, stride * H, fmt, W, H,
                       swizzle ? SL_TEX_FLAG_ROWSWAPPED : 0,
                       pal_for(fmt), 512, g_out, sizeof(g_out));
    ck(rc == SL_TEX_OK, name);

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned v = y * W + x;
            unsigned char *p = g_out + (y * W + x) * 4;
            if (fmt == SL_TEX_I8) {
                ck_px(p, v, v, v, v, "i8 texel");
            } else if (fmt == SL_TEX_IA8) {
                unsigned i = (v >> 4) * 17, a = (v & 0xf) * 17;
                ck_px(p, i, i, i, a, "ia8 texel");
            } else if (fmt == SL_TEX_CI8_RGBA16) {
                unsigned c = v & 0x1f, e = (c << 3) | (c >> 2);
                ck_px(p, e, e, e, (v & 1) ? 0xff : 0, "ci8/rgba16 texel");
            } else {
                ck_px(p, v, v, v, (v * 3) & 0xff, "ci8/ia16 texel");
            }
        }
}

static void test_4bit(int fmt, int swizzle, const char *name)
{
    unsigned stride = sl_tex_row_stride(fmt, W);
    unsigned x, y;
    int rc;

    fill_pad(fmt);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned v = (x + y * 3) & 0xf;
            unsigned char *b = &g_src[stride * y + (x >> 1)];
            if (x & 1)
                *b = (unsigned char) ((*b & 0xf0) | v);
            else
                *b = (unsigned char) ((*b & 0x0f) | (v << 4));
        }
    if (swizzle)
        ref_swap_alt_rows(g_src, W, H, fmt);

    rc = sl_tex_decode(g_src, stride * H, fmt, W, H,
                       swizzle ? SL_TEX_FLAG_ROWSWAPPED : 0,
                       pal_for(fmt), 32, g_out, sizeof(g_out));
    ck(rc == SL_TEX_OK, name);

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned v = (x + y * 3) & 0xf;
            unsigned char *p = g_out + (y * W + x) * 4;
            if (fmt == SL_TEX_I4) {
                unsigned i = v * 17;
                ck_px(p, i, i, i, i, "i4 texel");
            } else if (fmt == SL_TEX_IA4) {
                unsigned t = v >> 1;
                unsigned i = (t << 5) | (t << 2) | (t >> 1);
                ck_px(p, i, i, i, (v & 1) ? 0xff : 0, "ia4 texel");
            } else if (fmt == SL_TEX_CI4_RGBA16) {
                unsigned e = (v << 3) | (v >> 2);
                ck_px(p, e, e, e, (v & 1) ? 0xff : 0, "ci4/rgba16 texel");
            } else {
                ck_px(p, v, v, v, (v * 3) & 0xff, "ci4/ia16 texel");
            }
        }
}

static void test_alias_formats(void)
{
    /* RGB24 is stored 32bpp and RGB15 stored 16bpp per huffman compression
     * types.txt, so each must decode identically to its wide sibling. */
    unsigned char a[W * H * 4], b[W * H * 4];
    unsigned stride32 = sl_tex_row_stride(SL_TEX_RGBA32, W);
    unsigned stride16 = sl_tex_row_stride(SL_TEX_RGBA16, W);
    unsigned i;

    ck(sl_tex_row_stride(SL_TEX_RGB24, W) == stride32, "rgb24 stride == rgba32");
    ck(sl_tex_row_stride(SL_TEX_RGB15, W) == stride16, "rgb15 stride == rgba16");

    for (i = 0; i < SRCBYTES; i++)
        g_src[i] = (unsigned char) (i * 7 + 3);

    ck(sl_tex_decode(g_src, stride32 * H, SL_TEX_RGBA32, W, H, 0, 0, 0,
                     a, sizeof(a)) == SL_TEX_OK, "rgba32 for alias test");
    ck(sl_tex_decode(g_src, stride32 * H, SL_TEX_RGB24, W, H, 0, 0, 0,
                     b, sizeof(b)) == SL_TEX_OK, "rgb24 for alias test");
    ck(memcmp(a, b, sizeof(a)) == 0, "rgb24 decodes as rgba32");

    ck(sl_tex_decode(g_src, stride16 * H, SL_TEX_RGBA16, W, H, 0, 0, 0,
                     a, sizeof(a)) == SL_TEX_OK, "rgba16 for alias test");
    ck(sl_tex_decode(g_src, stride16 * H, SL_TEX_RGB15, W, H, 0, 0, 0,
                     b, sizeof(b)) == SL_TEX_OK, "rgb15 for alias test");
    ck(memcmp(a, b, sizeof(a)) == 0, "rgb15 decodes as rgba16");
}

/* Every format, hit with each malformed shape. Nothing here may fault. */
static void test_malformed(void)
{
    static const int fmts[] = {
        SL_TEX_RGBA32, SL_TEX_RGBA16, SL_TEX_RGB24, SL_TEX_RGB15,
        SL_TEX_IA16, SL_TEX_IA8, SL_TEX_IA4, SL_TEX_I8, SL_TEX_I4,
        SL_TEX_CI8_RGBA16, SL_TEX_CI4_RGBA16, SL_TEX_CI8_IA16, SL_TEX_CI4_IA16,
        SL_TEX_CI16_RGBA16, SL_TEX_CI16_IA16
    };
    unsigned n = sizeof(fmts) / sizeof(fmts[0]);
    unsigned i;
    char msg[96];

    for (i = 0; i < n; i++) {
        int f = fmts[i];
        unsigned need = sl_tex_data_size(f, W, H);
        int rc;

        /* truncated source, one byte short of what the format demands */
        rc = sl_tex_decode(g_src, need - 1, f, W, H, SL_TEX_FLAG_ROWSWAPPED,
                           pal_for(f), 512, g_out, sizeof(g_out));
        sprintf(msg, "fmt %02x truncated src -> SHORT_SRC", f);
        ck(rc == SL_TEX_ERR_SHORT_SRC, msg);

        /* absurd dimensions */
        rc = sl_tex_decode(g_src, SRCBYTES, f, 100000, 100000, 0,
                           pal_for(f), 512, g_out, sizeof(g_out));
        sprintf(msg, "fmt %02x absurd dims -> DIM", f);
        ck(rc == SL_TEX_ERR_DIM, msg);

        /* zero and negative dimensions */
        rc = sl_tex_decode(g_src, SRCBYTES, f, 0, H, 0,
                           pal_for(f), 512, g_out, sizeof(g_out));
        sprintf(msg, "fmt %02x zero width -> DIM", f);
        ck(rc == SL_TEX_ERR_DIM, msg);

        rc = sl_tex_decode(g_src, SRCBYTES, f, W, -1, 0,
                           pal_for(f), 512, g_out, sizeof(g_out));
        sprintf(msg, "fmt %02x negative height -> DIM", f);
        ck(rc == SL_TEX_ERR_DIM, msg);

        /* undersized destination */
        rc = sl_tex_decode(g_src, SRCBYTES, f, W, H, 0,
                           pal_for(f), 512, g_out, W * H * 4 - 1);
        sprintf(msg, "fmt %02x short dst -> SHORT_DST", f);
        ck(rc == SL_TEX_ERR_SHORT_DST, msg);

        /* null source */
        rc = sl_tex_decode(0, need, f, W, H, 0, pal_for(f), 512,
                           g_out, sizeof(g_out));
        sprintf(msg, "fmt %02x null src -> ARGS", f);
        ck(rc == SL_TEX_ERR_ARGS, msg);

        /* null destination */
        rc = sl_tex_decode(g_src, need, f, W, H, 0, pal_for(f), 512,
                           0, sizeof(g_out));
        sprintf(msg, "fmt %02x null dst -> ARGS", f);
        ck(rc == SL_TEX_ERR_ARGS, msg);

        if (sl_tex_palette_entries(f)) {
            /* missing palette */
            rc = sl_tex_decode(g_src, need, f, W, H, 0, 0, 0,
                               g_out, sizeof(g_out));
            sprintf(msg, "fmt %02x null palette -> ARGS", f);
            ck(rc == SL_TEX_ERR_ARGS, msg);

            /* palette present but too short for the indices in the data:
             * g_src is 0xa5 everywhere here, so index 0xa (CI4) / 0xa5 (CI8)
             * is used and a 2-entry palette cannot serve it */
            memset(g_src, 0xa5, SRCBYTES);
            rc = sl_tex_decode(g_src, need, f, W, H, 0, g_pal_rgba, 4,
                               g_out, sizeof(g_out));
            sprintf(msg, "fmt %02x short palette -> SHORT_PAL", f);
            ck(rc == SL_TEX_ERR_SHORT_PAL, msg);

            /* B-141. The same data under SL_TEX_FLAG_PAL_WRAP decodes, every
             * texel lands on entry (idx mod 2), and the wrap is counted:
             * index 0xa (CI4) / 0xa5 (CI8) mod 2 = 0 / 1. A palette with no
             * readable entry still rejects - nothing to wrap into. */
            rc = sl_tex_decode(g_src, need, f, W, H, SL_TEX_FLAG_PAL_WRAP,
                               g_pal_rgba, 4, g_out, sizeof(g_out));
            sprintf(msg, "fmt %02x short palette + PAL_WRAP -> OK", f);
            ck(rc == SL_TEX_OK, msg);
            sprintf(msg, "fmt %02x PAL_WRAP counts every texel", f);
            ck(sl_tex_pal_wrapped == (unsigned) (W * H), msg);
            {
                unsigned e = (sl_tex_bpp(f) == 4) ? 0u : 1u;
                unsigned char want[4];
                unsigned pc = ((unsigned) g_pal_rgba[e * 2] << 8)
                            | g_pal_rgba[e * 2 + 1];
                if (f == SL_TEX_CI8_IA16 || f == SL_TEX_CI4_IA16 ||
                    f == SL_TEX_CI16_IA16)
                    sl_put_ia16(want, pc);
                else
                    sl_put_rgba16(want, pc);
                sprintf(msg, "fmt %02x PAL_WRAP texel 0 is entry %u", f, e);
                ck(memcmp(g_out, want, 4) == 0, msg);
                sprintf(msg, "fmt %02x PAL_WRAP last texel is entry %u", f, e);
                ck(memcmp(g_out + (W * H - 1) * 4, want, 4) == 0, msg);
            }
            rc = sl_tex_decode(g_src, need, f, W, H, SL_TEX_FLAG_PAL_WRAP,
                               g_pal_rgba, 1, g_out, sizeof(g_out));
            sprintf(msg, "fmt %02x empty palette + PAL_WRAP -> SHORT_PAL", f);
            ck(rc == SL_TEX_ERR_SHORT_PAL, msg);
        }
    }

    /* unknown format codes (0x0d/0x0e are the LUT-enabled 16-bit fetches
     * since B-130; 0x0f is the first code with no decoder) */
    ck(sl_tex_decode(g_src, SRCBYTES, 0x0f, W, H, 0, g_pal_rgba,
                     512, g_out, sizeof(g_out)) == SL_TEX_ERR_FORMAT,
       "format 0x0f -> FORMAT");
    ck(sl_tex_decode(g_src, SRCBYTES, -1, W, H, 0, g_pal_rgba,
                     512, g_out, sizeof(g_out)) == SL_TEX_ERR_FORMAT,
       "format -1 -> FORMAT");
    ck(sl_tex_decode(g_src, SRCBYTES, 0x7fffffff, W, H, 0, g_pal_rgba,
                     512, g_out, sizeof(g_out)) == SL_TEX_ERR_FORMAT,
       "format INT_MAX -> FORMAT");
    ck(sl_tex_bpp(0x0f) == 0, "sl_tex_bpp rejects 0x0f");
    ck(sl_tex_row_stride(SL_TEX_I8, 0) == 0, "stride(0) == 0");
    ck(sl_tex_data_size(SL_TEX_I8, W, 0) == 0, "data_size(h=0) == 0");
}

/* Decode into a heap-sized-to-fit buffer with a guard byte, proving the
 * decoder writes exactly width*height*4 and not one byte more. */
static void test_no_overwrite(void)
{
    static unsigned char buf[W * H * 4 + 1];
    unsigned stride = sl_tex_row_stride(SL_TEX_RGBA16, W);
    int rc;

    memset(g_src, 0x5a, SRCBYTES);
    memset(buf, 0xcc, sizeof(buf));
    rc = sl_tex_decode(g_src, stride * H, SL_TEX_RGBA16, W, H,
                       SL_TEX_FLAG_ROWSWAPPED, 0, 0, buf, W * H * 4);
    ck(rc == SL_TEX_OK, "guard-byte decode ok");
    ck(buf[W * H * 4] == 0xcc, "decoder wrote no byte past width*height*4");
}

/*
 * Fuzz pass. Every buffer is malloc'd to its EXACT size, so under
 * -fsanitize=address the redzones turn any read past `srcbytes`/`palbytes` or
 * any write past width*height*4 into a hard failure rather than a silent one.
 * Inputs are random bytes at random dimensions in random formats, including
 * invalid format codes and short buffers - the shape of data that comes off a
 * user's ROM when something upstream is wrong.
 */
static unsigned g_rng = 0x9e3779b9u;

static unsigned rnd(unsigned n)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 16) % (n ? n : 1u);
}

static void test_fuzz(unsigned iters)
{
    unsigned i, bad = 0;

    for (i = 0; i < iters; i++) {
        int fmt = (int) rnd(18) - 2;          /* -2..15: straddles 0x00..0x0c */
        int w   = (int) rnd(40) - 1;          /* -1..38, so 0 and -1 occur    */
        int h   = (int) rnd(40) - 1;
        unsigned need = sl_tex_data_size(fmt, w, h);
        unsigned srcn, paln, outn, k;
        unsigned char *src, *pal, *out;
        int rc;

        /* sometimes exact, sometimes short, sometimes generous */
        srcn = need ? (need + rnd(3) - 1) : rnd(64);
        paln = rnd(4) ? rnd(520) : 0;
        outn = (w > 0 && h > 0) ? (unsigned) w * (unsigned) h * 4u : 0;
        if (rnd(4) == 0 && outn)
            outn -= 1;                        /* deliberately one short       */

        src = srcn ? (unsigned char *) malloc(srcn) : 0;
        pal = paln ? (unsigned char *) malloc(paln) : 0;
        out = outn ? (unsigned char *) malloc(outn) : 0;

        for (k = 0; k < srcn; k++) src[k] = (unsigned char) rnd(256);
        for (k = 0; k < paln; k++) pal[k] = (unsigned char) rnd(256);

        rc = sl_tex_decode(src, srcn, fmt, w, h, rnd(2) ? SL_TEX_FLAG_ROWSWAPPED : 0,
                           pal, paln, out, outn);
        if (rc < SL_TEX_OK || rc > SL_TEX_ERR_SHORT_DST)
            bad++;

        free(src);
        free(pal);
        free(out);
    }

    ck(bad == 0, "fuzz: every return code is a documented one");
    printf("  fuzz: %u random decodes, no fault, no undocumented return\n", iters);
}

static void test_swizzle_is_involution(void)
{
    /* Decoding swizzled data must equal decoding the plain data. This is the
     * property that catches an off-by-one in the word xor. */
    static unsigned char plain[W * H * 4], swz[W * H * 4];
    static unsigned char work[SL_TEX_MAX_DIM];
    static const int fmts[] = {
        SL_TEX_RGBA32, SL_TEX_RGBA16, SL_TEX_IA16, SL_TEX_I8, SL_TEX_I4
    };
    unsigned i, k;
    char msg[64];

    for (k = 0; k < sizeof(fmts) / sizeof(fmts[0]); k++) {
        int f = fmts[k];
        unsigned need = sl_tex_data_size(f, W, H);

        for (i = 0; i < sizeof(work); i++)
            work[i] = (unsigned char) (i * 31 + 11);
        ck(sl_tex_decode(work, need, f, W, H, 0, pal_for(f), 512,
                         plain, sizeof(plain)) == SL_TEX_OK, "plain decode");

        ref_swap_alt_rows(work, W, H, f);
        ck(sl_tex_decode(work, need, f, W, H, SL_TEX_FLAG_ROWSWAPPED,
                         pal_for(f), 512, swz, sizeof(swz)) == SL_TEX_OK,
           "swizzled decode");

        sprintf(msg, "fmt %02x unswizzle inverts texSwapAltRowBytes", f);
        ck(memcmp(plain, swz, sizeof(plain)) == 0, msg);
    }
}

int main(void)
{
    unsigned i;
    int sw;

    /* palettes: entry i is 5551 (r=i&0x1f,g=0,b=0,a=1) read as RGBA16, and
     * I8A8 (i, 0x80) read as IA16. One buffer serves both by construction. */
    for (i = 0; i < 256; i++) {
        unsigned c5 = i & 0x1f;
        be16(g_pal_rgba + i * 2,
             (c5 << 11) | (c5 << 6) | (c5 << 1) | (i & 1));
        be16(g_pal_ia + i * 2, (i << 8) | ((i * 3) & 0xff));
    }

    printf("sl_tex selftest: %dx%d fixtures, both row orders\n", W, H);

    for (sw = 0; sw <= 1; sw++) {
        printf("  --- %s ---\n", sw ? "row-swapped" : "plain");
        test_rgba32(sw);
        test_rgba16(sw);
        test_ia16(sw);
        test_8bit(SL_TEX_I8, sw, "i8 decode returns ok");
        test_8bit(SL_TEX_IA8, sw, "ia8 decode returns ok");
        test_4bit(SL_TEX_I4, sw, "i4 decode returns ok");
        test_4bit(SL_TEX_IA4, sw, "ia4 decode returns ok");
    }

    /* Paletted formats: index values must stay inside the palette, so these
     * run on their own fixtures rather than the 0..N ramp above. */
    for (sw = 0; sw <= 1; sw++) {
        printf("  --- paletted, %s ---\n", sw ? "row-swapped" : "plain");
        test_4bit(SL_TEX_CI4_RGBA16, sw, "ci4/rgba16 decode returns ok");
        test_4bit(SL_TEX_CI4_IA16, sw, "ci4/ia16 decode returns ok");
        test_8bit(SL_TEX_CI8_RGBA16, sw, "ci8/rgba16 decode returns ok");
        test_8bit(SL_TEX_CI8_IA16, sw, "ci8/ia16 decode returns ok");
        test_ci16(SL_TEX_CI16_RGBA16, sw, "ci16/rgba16 decode returns ok");
        test_ci16(SL_TEX_CI16_IA16, sw, "ci16/ia16 decode returns ok");
    }

    printf("  --- aliases, swizzle, bounds, malformed ---\n");
    test_alias_formats();
    test_swizzle_is_involution();
    test_no_overwrite();
    test_malformed();
    test_fuzz(200000);

    printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS",
           g_checks, g_fail);
    return g_fail != 0;
}

#endif /* SL_TEX_SELFTEST */

#endif /* !__sgi */
