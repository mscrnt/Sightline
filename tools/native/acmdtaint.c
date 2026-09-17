/* Residual attribution over a repaired-bracket ACMD capture.
 *
 * Answers ONE question: for the bytes where this interpreter disagrees with the
 * cartridge, WHICH unvalidated arithmetic opcodes lie on their dependency
 * paths? It changes no arithmetic and executes no voice opcode.
 *
 * FULL BITMASK COMBINATIONS, never an else-if priority. An else-if
 * classification reports each byte under one opcode and destroys exactly the
 * overlap information needed to isolate one - a byte touched by both MIXER and
 * POLEF is evidence about neither on its own. All eight M/R/P combinations are
 * reported separately, plus the no-taint control.
 *
 * Frame-start DRAM counts as UNTAINTED: the captured "before" is cartridge
 * ground truth, so the question is only which arithmetic ran WITHIN the frame.
 *
 * VACUITY: a byte the cartridge left unchanged (after == before) can never be
 * positive evidence for arithmetic - any implementation that does not touch it
 * scores exact. Changed and unchanged are counted separately throughout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../../src/platform/sl_acmd.h"

#define M_TAINT 1   /* a MIXER contributed            */
#define R_TAINT 2   /* passed through output RESAMPLE */
#define P_TAINT 4   /* passed through POLEF           */
#define A_TAINT 8   /* voice ADPCM                    */
#define E_TAINT 16  /* ENVMIXER                       */

static const char *CLS[8] = {
    "(no arithmetic taint)", "MIXER only", "RESAMPLE only", "MIXER+RESAMPLE",
    "POLEF only", "MIXER+POLEF", "RESAMPLE+POLEF", "MIXER+RESAMPLE+POLEF"
};

typedef struct {
    long long total, changed, diff, nzs, n;
    double sq; int mx;
    unsigned char frames[512];
} Bucket;

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/tmp/w/fixed.bin";
    int break_rule = argc > 2 ? atoi(argv[2]) : 0;   /* control 2 */
    FILE *f = fopen(path, "rb");
    uint8_t hdr[20];
    uint32_t nf, lo, dsz, fi;
    uint8_t *before, *after, *work, *dt;
    uint32_t *words;
    static int32_t lastw[1 << 20];
    static uint8_t dmt[0x1000];
    Bucket B[8];
    long long fired[32];
    long long ncmd[16];
    long long vac_same = 0, vac_chg = 0;
    int frame_no = 0;

    memset(B, 0, sizeof B);
    memset(fired, 0, sizeof fired);
    memset(ncmd, 0, sizeof ncmd);

    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }
    if (fread(hdr, 1, 20, f) != 20 || memcmp(hdr, "SLACMD01", 8)) {
        fprintf(stderr, "bad capture magic\n"); return 2;
    }
    nf = be32(hdr + 8); lo = be32(hdr + 12); dsz = be32(hdr + 16);
    printf("capture: %u frames, window 0x%06x..0x%06x\n", nf, lo, lo + dsz);
    if (break_rule)
        printf("*** CONTROL: propagation rule %d DELIBERATELY BROKEN ***\n", break_rule);

    before = malloc(dsz); after = malloc(dsz);
    work = malloc(dsz); dt = malloc(dsz); words = malloc(4096 * 8);

    for (fi = 0; fi < nf; fi++) {
        uint8_t fh[12];
        uint32_t af, n, k, si = 0, so = 0, sc = 0, q;
        sl_acmd_state st;

        if (fread(fh, 1, 12, f) != 12) break;
        af = be32(fh); n = be32(fh + 8);
        if (n > 4096) break;
        for (k = 0; k < n; k++) {
            uint8_t cw[8];
            if (fread(cw, 1, 8, f) != 8) return 2;
            words[2 * k] = be32(cw); words[2 * k + 1] = be32(cw + 4);
        }
        if (fread(before, 1, dsz, f) != dsz) return 2;
        if (fread(after, 1, dsz, f) != dsz) return 2;

        memset(dmt, 0, sizeof dmt);
        memset(dt, 0, dsz);
        for (q = 0; q < dsz; q++) lastw[q] = -1;

        for (k = 0; k < n; k++) {
            uint32_t w0 = words[2 * k], w1 = words[2 * k + 1];
            int op = (int)((w0 >> 24) & 0xFF);
            ncmd[op & 15]++;
            switch (op) {
            case 8: si = w0 & 0xFFFF; so = (w1 >> 16) & 0xFFFF; sc = w1 & 0xFFFF; break;
            case 2: { uint32_t d = w0 & 0xFFFF, c = w1 & 0xFFFF;
                      if (d + c <= 0x1000) { memset(dmt + d, 0, c); fired[2]++; } } break;
            case 10:{ uint32_t i = w0 & 0xFFFF, o = (w1 >> 16) & 0xFFFF, c = w1 & 0xFFFF;
                      if (i + c <= 0x1000 && o + c <= 0x1000) { memmove(dmt + o, dmt + i, c); fired[10]++; } } break;
            case 4: if (w1 >= lo && w1 + sc <= lo + dsz && si + sc <= 0x1000) {
                        memcpy(dmt + si, dt + (w1 - lo), sc); fired[4]++; } break;
            case 6: if (w1 >= lo && w1 + sc <= lo + dsz && so + sc <= 0x1000) {
                        if (break_rule != 1) memcpy(dt + (w1 - lo), dmt + so, sc);
                        for (q = 0; q < sc; q++) lastw[w1 - lo + q] = (int32_t) k;
                        fired[6]++; } break;
            case 13:{ uint32_t L = (w1 >> 16) & 0xFFFF, R = w1 & 0xFFFF, j;
                      if (L + sc <= 0x1000 && R + sc <= 0x1000 && so + 2 * sc <= 0x1000) {
                        for (j = 0; j < sc; j += 2) {
                          dmt[so + 2*j] = dmt[so + 2*j + 1] = dmt[L + j];
                          dmt[so + 2*j + 2] = dmt[so + 2*j + 3] = dmt[R + j]; }
                        fired[13]++; } } break;
            case 5: if (si + sc <= 0x1000 && so + sc <= 0x1000) {   /* output RESAMPLE */
                        uint32_t j; memmove(dmt + so, dmt + si, sc);
                        if (break_rule != 2)
                            for (j = 0; j < sc; j++) dmt[so + j] |= R_TAINT;
                        fired[5]++; } break;
            case 12:{ uint32_t i = (w1 >> 16) & 0xFFFF, o = w1 & 0xFFFF, j;   /* MIXER */
                      if (i + sc <= 0x1000 && o + sc <= 0x1000) {
                        for (j = 0; j < sc; j++)
                            dmt[o + j] |= (uint8_t)(dmt[i + j] | M_TAINT);
                        fired[12]++; } } break;
            case 14: if (so + sc <= 0x1000) {                        /* POLEF */
                        uint32_t j;
                        if (break_rule != 3)
                            for (j = 0; j < sc; j++) dmt[so + j] |= P_TAINT;
                        fired[14]++; } break;
            case 1:  if (so + sc <= 0x1000) { uint32_t j;
                        for (j = 0; j < sc; j++) dmt[so + j] |= A_TAINT; fired[1]++; } break;
            case 3:  if (so + sc <= 0x1000) { uint32_t j;
                        for (j = 0; j < sc; j++) dmt[so + j] |= E_TAINT; fired[3]++; } break;
            }
        }

        memcpy(work, before, dsz);
        sl_acmd_init(&st, work, lo, dsz);
        if (sl_acmd_exec(&st, words, n) != SL_ACMD_OK) { frame_no++; continue; }

        for (q = 0; q + 1 < dsz; q += 2) {
            int c0, a1, b1, d1, ch;
            if (lastw[q] < 0) continue;
            c0 = dt[q] & (M_TAINT | R_TAINT | P_TAINT);
            b1 = (int16_t)((after[q] << 8) | after[q + 1]);
            a1 = (int16_t)((work[q] << 8) | work[q + 1]);
            ch = (after[q] != before[q]) || (after[q + 1] != before[q + 1]);
            B[c0].total += 2;
            if (ch) { B[c0].changed += 2; vac_chg += 2; } else vac_same += 2;
            if (work[q] != after[q]) B[c0].diff++;
            if (work[q + 1] != after[q + 1]) B[c0].diff++;
            if (ch && b1) {
                B[c0].nzs++;
                d1 = a1 - b1; if (d1 < 0) d1 = -d1;
                B[c0].n++; B[c0].sq += (double) d1 * d1;
                if (d1 > B[c0].mx) B[c0].mx = d1;
                if (frame_no < 512) B[c0].frames[frame_no] = 1;
            }
        }
        frame_no++;
    }
    fclose(f);

    printf("\n=== TAINT RULES FIRED (known-positive: each must be > 0) ===\n");
    printf("  CLEARBUFF=%lld LOADBUFF=%lld SAVEBUFF=%lld DMEMMOVE=%lld INTERLEAVE=%lld\n",
           fired[2], fired[4], fired[6], fired[10], fired[13]);
    printf("  MIXER=%lld  RESAMPLE=%lld  POLEF=%lld  ADPCM=%lld  ENVMIXER=%lld\n",
           fired[12], fired[5], fired[14], fired[1], fired[3]);
    printf("\n=== VOICE-ABSENCE CHECK (verified, not assumed) ===\n");
    printf("  ADPCM commands=%lld  ENVMIXER commands=%lld  -> %s\n",
           ncmd[1], ncmd[3],
           (ncmd[1] == 0 && ncmd[3] == 0) ? "CONFIRMED voice-free" : "VOICE PRESENT");

    printf("\n=== RESIDUAL ATTRIBUTION MATRIX (repaired bracket) ===\n");
    printf("%-24s %10s %10s %8s %8s %8s %9s  frames\n",
           "class", "final", "changed", "diff", "nonzero", "max|e|", "RMS");
    for (int c = 0; c < 8; c++) {
        int nfm = 0;
        if (!B[c].total) continue;
        for (int i = 0; i < 512; i++) nfm += B[c].frames[i];
        printf("%-24s %10lld %10lld %8lld %8lld %8d %9.2f  %d\n",
               CLS[c], B[c].total, B[c].changed, B[c].diff, B[c].nzs, B[c].mx,
               B[c].n ? sqrt(B[c].sq / (double) B[c].n) : 0.0, nfm);
    }
    printf("\n=== VACUITY ===\n");
    printf("  final bytes the cartridge CHANGED   : %lld\n", vac_chg);
    printf("  final bytes the cartridge left same : %lld\n", vac_same);
    if (!vac_chg)
        printf("  REFERENCE IS VACUOUS - no positive arithmetic evidence here.\n");
    return 0;
}
