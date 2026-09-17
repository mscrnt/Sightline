/**
 * GoldenEye texture decoder -> RGBA8 (Phase 1 groundwork).
 *
 * Pure, headless, allocation-free. No GL, no globals, no I/O: this header
 * describes a function you can call from a unit test with a hand-built
 * buffer. Wiring it to the display-list interpreter happens elsewhere.
 *
 * src/game/ must never include this. See docs/project-rules.md, "Layering goal".
 */
#ifndef SL_GFX_TEX_H
#define SL_GFX_TEX_H

#ifdef __sgi
#error "sl_gfx_tex.h is native-only; guard the include with #ifndef __sgi"
#endif

/*
 * Format codes. These are GoldenEye's OWN codes, not GBI G_IM_FMT_*, and the
 * values deliberately mirror TEXFORMAT_* in src/game/image.h one for one.
 * image.h is not included here because the native build compiles src/gfx/
 * against host headers only (tools/native/build.sh), with no -Iinclude, so
 * ultra64.h is not reachable. Keep the two lists in step.
 *
 * Authority for the code list: "images text and font/Huffman/huffman
 * compression types.txt" ("Image types are as such: 0 32bit color image ...
 * C 16 color indexed"), whose value tables #5 (N64 image type) and #6 (N64
 * pixel size) give the per-code depth used below.
 */
#define SL_TEX_RGBA32     0x00 /* 32bpp 8/8/8/8                              */
#define SL_TEX_RGBA16     0x01 /* 16bpp 5/5/5/1                              */
#define SL_TEX_RGB24      0x02 /* stored 32bpp - "forced to 32bit fixed alpha" */
#define SL_TEX_RGB15      0x03 /* stored 16bpp 5551 - "with forced alpha"    */
#define SL_TEX_IA16       0x04 /* 16bpp I8 A8                                */
#define SL_TEX_IA8        0x05 /*  8bpp I4 A4                                */
#define SL_TEX_IA4        0x06 /*  4bpp I3 A1                                */
#define SL_TEX_I8         0x07 /*  8bpp intensity (alpha = intensity)        */
#define SL_TEX_I4         0x08 /*  4bpp intensity (alpha = intensity)        */
#define SL_TEX_CI8_RGBA16 0x09 /*  8bpp index into a 5551 palette            */
#define SL_TEX_CI4_RGBA16 0x0a /*  4bpp index into a 5551 palette            */
#define SL_TEX_CI8_IA16   0x0b /*  8bpp index into an I8A8 palette           */
#define SL_TEX_CI4_IA16   0x0c /*  4bpp index into an I8A8 palette           */
/* The two below are NOT GoldenEye image types (the list above ends at C).
 * They are the RDP's texture unit fetching a 16-bit tile while the texture
 * LUT is enabled: the unit reads the 16-bit texel and palettises its HIGH
 * byte. GoldenEye reaches this on the sky.c sea (Frigate): texSelect loads
 * IMAGE_WATER_BLUE as the CI8 mip chain the texpool holds and enables the
 * 5551 LUT, then unk_092E50.c:381-384 re-declares tiles 0/1 as RGBA/16b
 * line 4 without touching the LUT. Measured 2026-09-14 against the
 * cartridge (parallel_n64, romintro.py stage 26): the RDP draws the blue
 * water; decoding those bytes as 5551 gives green noise whose channel
 * maxima (R<=16, G<=231, B<=90) are exactly what 24-entry index pairs read
 * as 5551 produce. The notes document the LUT field, not the unit's
 * fetch (docs/doc-routing.json not_covered). */
#define SL_TEX_CI16_RGBA16 0x0d /* 16bpp fetch, high byte -> 5551 palette    */
#define SL_TEX_CI16_IA16   0x0e /* 16bpp fetch, high byte -> I8A8 palette    */

/* Decode flags. */
#define SL_TEX_FLAG_ROWSWAPPED 0x1 /* source has the odd-row word swap applied */
/* B-141. A CI index past the loaded palette entries does not fail the decode:
 * it wraps into the entries that ARE loaded (idx mod entries). The RDP never
 * rejects such a texel - the LUT is the upper half of TMEM and an index past
 * the F0-loaded count reads whatever stale entry sits there (ucode05.txt F0
 * describes the LOAD only; the fetch is corpus-silent, docs/doc-routing.json
 * not_covered). GoldenEye authors exactly this on the type-1 detail command:
 * TARDETAIL (images.def, flags 3/8/D/2) sets tile 0 at tmem word 19 with mask
 * s 7 / t 2, a 128x4 window that runs past the 16x16 image's 53-entry LUT
 * range, and the cartridge draws the Depot yard textured where a rejecting
 * decoder left the whole draw untextured (Gitea #27). The stale entry is not
 * reproducible; wrapping keeps every texel in the texture's own palette,
 * which is the cartridge's LOOK (dark tarmac noise) if not its bytes.
 * Without the flag the strict SL_TEX_ERR_SHORT_PAL contract is unchanged.
 * sl_tex_pal_wrapped counts the texels wrapped by the last decode. */
#define SL_TEX_FLAG_PAL_WRAP   0x2
extern unsigned sl_tex_pal_wrapped;

/* Return codes. 0 is success; everything else is a reason. */
#define SL_TEX_OK            0
#define SL_TEX_ERR_ARGS      1 /* null pointer where one is required          */
#define SL_TEX_ERR_FORMAT    2 /* format code is not one of the 13            */
#define SL_TEX_ERR_DIM       3 /* width/height zero, negative or absurd       */
#define SL_TEX_ERR_SHORT_SRC 4 /* srcbytes is smaller than the format demands */
#define SL_TEX_ERR_SHORT_PAL 5 /* palette buffer too small for an index used  */
#define SL_TEX_ERR_SHORT_DST 6 /* out buffer smaller than width*height*4      */

/* Largest dimension accepted. GoldenEye's own struct tex stores width and
 * height as u8, and texInflate* rejects width*height >= 4097, so this is
 * generous; it exists to make the stride arithmetic below un-overflowable. */
#define SL_TEX_MAX_DIM 1024

/* Human-readable form of a return code. Never null. */
const char *sl_tex_strerror(int err);

/* Bits per stored texel for a format code, or 0 if the code is unknown. */
unsigned sl_tex_bpp(int fmt);

/* Bytes per row INCLUDING GoldenEye's row padding, or 0 on bad input.
 * Rows are padded to 8 bytes (16 for the 32bpp formats) - see the comment on
 * sl_tex_decode for where that comes from. */
unsigned sl_tex_row_stride(int fmt, int width);

/* Total bytes of texel data for one image, or 0 on bad input.
 * NB: for a paletted texture the palette follows this many bytes on. */
unsigned sl_tex_data_size(int fmt, int width, int height);

/* Palette entries a format can address (16, 256), or 0 when unpaletted. */
unsigned sl_tex_palette_entries(int fmt);

/**
 * Decode one image into out_rgba as tightly packed R,G,B,A bytes, top row
 * first, width*height*4 bytes total.
 *
 * src/srcbytes  texel data and the number of bytes readable at src. Nothing
 *               past srcbytes is ever touched.
 * fmt           one of SL_TEX_*.
 * width/height  in texels. 1..SL_TEX_MAX_DIM.
 * flags         SL_TEX_FLAG_ROWSWAPPED if the caller's data still carries the
 *               odd-row word swap (it does, for anything that came out of
 *               texLoad).
 * palette/palbytes  16-bit palette entries and their byte count, for the CI
 *               formats; ignored otherwise and may be null.
 * out_rgba/outbytes  destination and its capacity.
 *
 * Returns SL_TEX_OK, or one of SL_TEX_ERR_* with out_rgba left untouched.
 * Malformed input is an error return, never a fault: this data comes off a
 * user's ROM and is not trusted.
 */
int sl_tex_decode(const void *src, unsigned srcbytes,
                  int fmt, int width, int height, unsigned flags,
                  const void *palette, unsigned palbytes,
                  unsigned char *out_rgba, unsigned outbytes);

#endif /* SL_GFX_TEX_H */
