/* The audio microcode's own data tables, derived from the user's ROM at
 * start-up. Native-only; src/game/ must never include anything from here.
 *
 * WHY THIS FILE EXISTS. The ACMD interpreter (sl_acmd.c) needs two tables
 * that live in the aspMain RSP microcode's DATA segment, not in game data:
 * the ENVMIXER per-lane ramp (8 shorts) and the RESAMPLE 64x4 polyphase table
 * (256 shorts). Until v0.2.0 a build step (tools/native/gen_resample_tab.py)
 * read them out of a locally extracted copy of that segment and compiled them
 * into the executable as static const arrays - 528 bytes of ROM-derived data
 * inside sightline.exe, measured at 0x1fce00 / 0x1fce20 of the v0.1.0 build
 * (docs/releases/v0.1.0.md) - which project rule 2 does not allow to ship.
 * Now they are read out of the ROM the player supplies, in memory, once,
 * before the game boots. Nothing of them exists in the build tree or in the
 * executable: no table, no default, no fallback.
 *
 * THE DERIVATION is the one scripts/extract_asp_gsp_rsp.sh performs, with the
 * game's own inflater in place of dd + gzip:
 *   ROM 0x21990 (137616), 71760 bytes: the 1172-compressed code+data segment
 *     (a two-byte 0x11 0x72 container header - src/game/decompress.c
 *     rz_header_bytes - then raw deflate, src/inflate/inflate.c)
 *   inflated: 247120 bytes (0x3c550), loaded at RAM 0x80020d90
 *   aspMainData = RAM 0x8005d020..0x8005d2e0 -> inflated 0x3c290..0x3c550,
 *     704 bytes (the script's aspMainDataStart / aspMainDataEnd)
 *   the ramp at +0xB0 (8 big-endian shorts), the taps at +0xC0 (256
 *     big-endian shorts) - the offsets the retired generator used, so the
 *     values the interpreter sees are the ones it was derived and accepted
 *     with (tools/windows/ucodetabtest.ps1 proves the identity against an
 *     independent standard-library derivation).
 *
 * WHAT IS EMBEDDED, stated plainly: two SHA-1 DIGESTS - of the 71760-byte
 * compressed segment and of the 704-byte data segment. A digest of
 * ROM-derived data is not the data and cannot be turned back into it. They
 * are here so a wrong, patched, byte-swapped or truncated ROM is refused with
 * a message that names the cause instead of yielding a plausible-looking
 * wrong table, and so a mistake in the constants above cannot pass silently.
 *
 * THE INFLATER is the game's (decompress_entry), already in the native build.
 * It writes through a raw output pointer with no bound, which is why the
 * compressed segment is verified BEFORE it runs: the digest pins the stream
 * to the one whose inflated size is known, so the output buffer is exact by
 * construction. Its globals (inbuf, outbuf, huftlist, ...) are shared with
 * nothing native - tree-wide, `grep -rn "decompress_entry\|huftlist" src/`
 * finds inflate.c, inflate.h and the MIPS-only boot.s - and this runs on the
 * main thread before mainproc, so no other user of them exists yet. (The
 * game's own run-time inflater is the separate rz_* copy in src/game/zlib.c.)
 *
 * WHEN. main() calls sl_ucode_tables_derive() after sl_shim_configure() and
 * before mainproc(). The audio device is queue-driven on the main thread
 * (sl_audio.c: SDL_QueueAudio, no callback) and the interpreter runs from
 * sl_sc_complete_for on that same thread, so nothing can consume a command
 * list before this returns; sl_acmd_exec additionally refuses to run
 * (SL_ACMD_ERR_UNGROUNDED) if the tables were never set.
 *
 * LIFETIME. The tables live for the process in sl_acmd.c (static storage,
 * written once here); every buffer this file allocates is freed before it
 * returns. Failure is fatal to start-up - main() exits 3, the code
 * sl_rom_load uses for a ROM it cannot open - because a mixer with no tables
 * would be silent or wrong in a way nobody would connect to the cause.
 *
 * SL_UCODE_TABLES_DUMP=<path> writes <path>.ramp (16 bytes) and <path>.taps
 * (512 bytes) in host byte order after a successful derivation: the seam
 * tools/windows/ucodetabtest.ps1 compares against tools/native/
 * ucode_tables_oracle.py. That output is ROM-derived and belongs in a scratch
 * directory only.
 */
#ifndef __sgi
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sl_acmd.h"

struct huft;
const unsigned char *sl_rom_bytes(long *size);                        /* sl_ultra_shim.c */
unsigned decompress_entry(void *src, void *dst, struct huft *hlist);  /* src/inflate/inflate.c */
void sl_sha1(const void *data, unsigned n, unsigned char out[20]);    /* sl_sha1.c */

#define SL_UCODE_ROM_SIZE     12582912L
#define SL_UCODE_CDATA_ROM    137616L     /* scripts/extract_asp_gsp_rsp.sh: dd skip=  */
#define SL_UCODE_CDATA_LEN    71760L      /*                                  count= */
#define SL_UCODE_INFLATED_LEN 247120L     /* 0x3c550 = aspMainDataEnd - RAMSTART       */
#define SL_UCODE_SEG_OFF      0x3c290L    /* 0x8005d020 - 0x80020d90                   */
#define SL_UCODE_SEG_LEN      0x2c0L      /* 704                                       */
#define SL_UCODE_RAMP_OFF     0xB0
#define SL_UCODE_RAMP_N       8
#define SL_UCODE_TAPS_OFF     0xC0
#define SL_UCODE_TAPS_N       256
/* gzip's inflate never needs more than ~1.5k table entries for lbits 9 /
 * dbits 6; Rare's copy indexes a caller-supplied array instead of malloc, so
 * the array is sized with a wide margin and its stride over-covers the
 * 8-byte struct huft on any host. */
#define SL_UCODE_HUFT_ENTRIES 16384
#define SL_UCODE_HUFT_STRIDE  16
/* A wrong constant above overruns THIS, not the heap, and is then reported
 * by the length check. */
#define SL_UCODE_OUT_SLACK    0x10000
#define SL_UCODE_IN_PAD       64

/* SHA-1 of ROM 0x21990..0x21990+71760 (the compressed segment) and of the
 * inflated 704-byte aspMain data segment. Digests, not data - see above. */
static const unsigned char sl_ucode_cdata_sha1[20] = {
    0x91,0x43,0xd0,0x48,0xa7,0x65,0xe6,0x81,0xe9,0xf3,
    0xde,0x45,0xee,0xa1,0xb1,0xda,0x99,0x29,0x11,0xd4
};
static const unsigned char sl_ucode_seg_sha1[20] = {
    0x5b,0xcb,0x27,0x5d,0x75,0x52,0x95,0xa6,0x60,0xa7,
    0x15,0xe8,0x3d,0xd7,0xd6,0x6c,0xea,0x19,0x6a,0x0d
};

static void sl_ucode_fail(const char *why)
{
    fprintf(stderr,
        "sightline ucode: cannot derive the audio microcode tables from the ROM: %s\n"
        "  Sightline ships no ROM-derived data: the ENVMIXER ramp and the RESAMPLE\n"
        "  table are read out of your own GoldenEye 007 (U) dump at start-up. A\n"
        "  plain, uncompressed, big-endian .z64 of 12582912 bytes (SHA-1\n"
        "  abe01e4aeb033b6c0836819f549c791b26cfde83) is required.\n", why);
}

static void sl_ucode_hex(const unsigned char *d, char out[41])
{
    static const char h[] = "0123456789abcdef";
    int i;
    for (i = 0; i < 20; i++) { out[2 * i] = h[d[i] >> 4]; out[2 * i + 1] = h[d[i] & 15]; }
    out[40] = 0;
}

static short sl_ucode_be16(const unsigned char *p)
{
    return (short) (unsigned short) (((unsigned) p[0] << 8) | p[1]);
}

static void sl_ucode_dump(const short *ramp, const short *taps)
{
    const char *base = getenv("SL_UCODE_TABLES_DUMP");
    char path[1024];
    FILE *f;
    if (base == NULL || strlen(base) + 8 > sizeof path) return;
    sprintf(path, "%s.ramp", base);
    f = fopen(path, "wb");
    if (f) { fwrite(ramp, sizeof(short), SL_UCODE_RAMP_N, f); fclose(f); }
    sprintf(path, "%s.taps", base);
    f = fopen(path, "wb");
    if (f) { fwrite(taps, sizeof(short), SL_UCODE_TAPS_N, f); fclose(f); }
    fprintf(stderr, "sightline ucode: tables dumped to %s.ramp / %s.taps (ROM-derived; scratch only)\n",
            base, base);
}

int sl_ucode_tables_derive(void)
{
    long romsz = 0;
    const unsigned char *rom = sl_rom_bytes(&romsz);
    const unsigned char *seg;
    unsigned char *in = NULL, *out = NULL, *hl = NULL;
    unsigned char dg[20];
    char hx[41], msg[200];
    short ramp[SL_UCODE_RAMP_N], taps[SL_UCODE_TAPS_N];
    unsigned wp;
    int k, nonzero = 0, ok = 0;

    if (rom == NULL || romsz <= 0) { sl_ucode_fail("no ROM is loaded"); return 1; }
    if (romsz != SL_UCODE_ROM_SIZE) {
        sprintf(msg, "the ROM is %ld bytes; a plain .z64 dump is %ld", romsz, SL_UCODE_ROM_SIZE);
        sl_ucode_fail(msg);
        return 1;
    }
    if (!(rom[0] == 0x80 && rom[1] == 0x37 && rom[2] == 0x12 && rom[3] == 0x40)) {
        sl_ucode_fail("the file does not start with the big-endian .z64 magic "
                      "(a .n64 / .v64 dump is byte-swapped; convert or re-dump)");
        return 1;
    }
    if (!(rom[SL_UCODE_CDATA_ROM] == 0x11 && rom[SL_UCODE_CDATA_ROM + 1] == 0x72)) {
        sl_ucode_fail("no 1172 container header at ROM 0x21990 (not a GoldenEye 007 (U) image)");
        return 1;
    }
    sl_sha1(rom + SL_UCODE_CDATA_ROM, (unsigned) SL_UCODE_CDATA_LEN, dg);
    if (memcmp(dg, sl_ucode_cdata_sha1, 20) != 0) {
        sl_ucode_hex(dg, hx);
        sprintf(msg, "the compressed code/data segment at ROM 0x21990 (71760 bytes) has "
                     "SHA-1 %s, not the supported dump's (a patched or corrupt ROM)", hx);
        sl_ucode_fail(msg);
        return 1;
    }

    in  = (unsigned char *) malloc(SL_UCODE_CDATA_LEN + SL_UCODE_IN_PAD);
    out = (unsigned char *) malloc(SL_UCODE_INFLATED_LEN + SL_UCODE_OUT_SLACK);
    hl  = (unsigned char *) calloc(SL_UCODE_HUFT_ENTRIES, SL_UCODE_HUFT_STRIDE);
    if (in == NULL || out == NULL || hl == NULL) { sl_ucode_fail("out of memory"); goto done; }
    memcpy(in, rom + SL_UCODE_CDATA_ROM, SL_UCODE_CDATA_LEN);
    memset(in + SL_UCODE_CDATA_LEN, 0, SL_UCODE_IN_PAD);

    wp = decompress_entry(in, out, (struct huft *) hl);
    if (wp != (unsigned) SL_UCODE_INFLATED_LEN) {
        sprintf(msg, "the segment inflated to %u bytes, expected %ld", wp, SL_UCODE_INFLATED_LEN);
        sl_ucode_fail(msg);
        goto done;
    }
    seg = out + SL_UCODE_SEG_OFF;
    sl_sha1(seg, (unsigned) SL_UCODE_SEG_LEN, dg);
    if (memcmp(dg, sl_ucode_seg_sha1, 20) != 0) {
        sl_ucode_fail("the aspMain data segment (inflated 0x3c290..0x3c550) does not "
                      "verify against its digest");
        goto done;
    }
    for (k = 0; k < SL_UCODE_RAMP_N; k++) {
        ramp[k] = sl_ucode_be16(seg + SL_UCODE_RAMP_OFF + 2 * k);
        if (ramp[k] != 0) nonzero = 1;
    }
    for (k = 0; k < SL_UCODE_TAPS_N; k++)
        taps[k] = sl_ucode_be16(seg + SL_UCODE_TAPS_OFF + 2 * k);
    if (!nonzero) {
        /* the generator's own refusal, kept: a zero ramp makes every envelope
         * construct to the same value, which is plausible-looking and wrong */
        sl_ucode_fail("the ENVMIXER ramp read as all zeros - refusing it");
        goto done;
    }

    sl_acmd_set_ucode_tables(ramp, taps);
    sl_ucode_dump(ramp, taps);
    fprintf(stderr, "sightline ucode: audio microcode tables derived from the ROM at "
                    "start-up (aspMain data segment, %d ramp lanes + %d taps; none "
                    "compiled in)\n", SL_UCODE_RAMP_N, SL_UCODE_TAPS_N);
    ok = 1;

done:
    if (in)  { memset(in, 0, SL_UCODE_CDATA_LEN); free(in); }
    if (out) { memset(out, 0, SL_UCODE_INFLATED_LEN); free(out); }
    if (hl)  free(hl);
    return ok ? 0 : 1;
}
#endif /* __sgi */
