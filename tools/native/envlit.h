/* The ACCEPTED machine-checked literal evaluator, carried intact.
 * Its guarantee is the descriptor verification against the extracted text
 * segment, which runs at startup wherever this is used: if the transcription
 * differed from the accepted one in any field, verify_chain refuses to run.
 * Construction and clamps are unmodified; only chronological plumbing is
 * added alongside by the including tool. */
#ifndef ENVLIT_H
#define ENVLIT_H
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int MUT_A = 0;
static int64_t acc48(int64_t v)
{
    v &= 0xFFFFFFFFFFFFLL;
    if (v & 0x800000000000LL) v -= 0x1000000000000LL;
    return v;
}
static int16_t clamp_s16(int64_t v)
{ if (v > 32767) return 32767; if (v < -32768) return -32768; return (int16_t)v; }

/* Destination extraction. The HIGH forms take a signed clamp of the selected
 * accumulator field - that is the documented operation and is what ADPCM's
 * three-way validation already exercised. The LOW forms take the low slice; the
 * documented rule additionally clamps when the accumulator's upper portion says
 * the low slice is not faithful, so every extraction is instrumented and the
 * boundary count is reported rather than the rule being assumed harmless. */
static long long LOW_BOUNDARY = 0;
/* per-site boundary accounting: which instruction, and is its destination
 * live to the scored envelope pair or a dead scratch write? */
#define NSITE 6
static const int  BSITE[NSITE] = {0xb88,0xb8c,0xba0,0xbe4,0xbe8,0xbfc};
static const char*BNAME[NSITE] = {"VMUDL v23 L","VMADN v23 L","VMADN v21 L(LIVE)",
                                  "VMUDL v23 R","VMADN v23 R","VMADN v19 R(LIVE)"};
static const int  BLIVE[NSITE] = {0,0,1,0,0,1};
static long long  BHIT[NSITE];
static int CUR_SITE = -1;
static int16_t dest_high(int64_t a) { return clamp_s16(a >> 16); }
/* Three candidate low-result rules, kept SEPARATE. The clamp discriminator is
 * the accumulator's bits 47..31 - i.e. whether the value fits signed 32-bit -
 * NOT the bits immediately above the 16-bit result window. Ordinary envelope
 * highs therefore do not trigger it.
 *   RULE_RAW : the low 16 bits regardless          (guide: wrong when it fires)
 *   RULE_SAT32: low half of a saturated signed 32-bit pair (all-ones / zero)
 *   RULE_S16 : naive signed-16 clamp of the low field (max / min)
 * No candidate is placed in both evaluators until the cartridge selects one. */
enum { RULE_RAW = 0, RULE_SAT32 = 1, RULE_S16 = 2 };
/* SELECTED BY THE CARTRIDGE ORACLE, not by preference: on the 6 discriminating
 * A_INIT commands that are the last ENVMIXER writer to their state and whose
 * clamp fires, sat32 reproduces the after-image 6/6 while raw and s16 each
 * score 0/6. See tools/native/acmdenvlow.c. */
static int LOW_RULE = RULE_SAT32;
static long long CAND_DIFF_BC = 0, CAND_DIFF_AB = 0, CAND_DIFF_AC = 0;
static long long OVF_POS = 0, OVF_NEG = 0;   /* sign census over ALL events */
static long long OVF_POS_L = 0, OVF_NEG_L = 0, OVF_POS_R = 0, OVF_NEG_R = 0;
static int low_clamp_fires(int64_t a) { int64_t t = a >> 31; return !(t == 0 || t == -1); }
static int16_t low_raw (int64_t a) { return (int16_t)(a & 0xFFFF); }
/* UNGROUNDED-PATH COUNTER. The positive branch is cartridge-grounded (374/374
 * measured events, sat32 12/12 vs raw and s16 at 0/12). The NEGATIVE branch is
 * appendix-derived only - no capture exercises it - and those are different
 * kinds of evidence. Real content can reach it at runtime, so every taking of
 * that branch is counted and must be surfaced rather than passing silently.
 * The production interpreter carries the same counter when ENVMIXER lands. */
long long SL_ENV_LOW_NEG_UNGROUNDED = 0;
static int16_t low_sat32(int64_t a)
{ if (!low_clamp_fires(a)) return low_raw(a);
  if (a > 0) return (int16_t)0xFFFF;
  SL_ENV_LOW_NEG_UNGROUNDED++;          /* appendix-derived, not grounded */
  return (int16_t)0x0000; }
static int16_t low_s16 (int64_t a)
{ if (!low_clamp_fires(a)) return low_raw(a);
  return (a > 0) ? (int16_t)0x7FFF : (int16_t)0x8000; }
static int16_t dest_low(int64_t a)
{
    if (low_clamp_fires(a)) {
        int q; int16_t ra = low_raw(a), rb = low_sat32(a), rc = low_s16(a);
        int isR = (CUR_SITE >= 0xbdc);
        LOW_BOUNDARY++;
        if (a > 0) { OVF_POS++; if (isR) OVF_POS_R++; else OVF_POS_L++; }
        else       { OVF_NEG++; if (isR) OVF_NEG_R++; else OVF_NEG_L++; }
        for (q = 0; q < NSITE; q++) if (BSITE[q] == CUR_SITE) { BHIT[q]++; break; }
        if (rb != rc) CAND_DIFF_BC++;
        if (ra != rb) CAND_DIFF_AB++;
        if (ra != rc) CAND_DIFF_AC++;
    }
    switch (LOW_RULE) {
    case RULE_SAT32: return low_sat32(a);
    case RULE_S16:   return low_s16(a);
    default:         return low_raw(a);
    }
}

enum { F_VXOR, F_VMUDL, F_VMADN, F_VMADM, F_VMADH };
/* funct codes of the forms this chain uses, for machine-checking the table */
static const int FORM_FUNCT[] = { 0x2C, 0x04, 0x0E, 0x0D, 0x0F };
/* `site` is a BLOB OFFSET into the extracted text segment, NOT an IMEM address.
 * The image loads at IMEM 0x080, so IMEM = blob + 0x80; mixing the two is the
 * coordinate error already paid for three times in this sprint. */
typedef struct { int site, form, vs, vt, elem, vd; } Ins;
#define NREG 32

/* left channel init chain, transcribed with its blob sites and raw element
 * fields; asserted against the encoding-derived operand list at startup */
static const Ins CHAIN_L[] = {
    {0xb80, F_VXOR,  21, 21,  0, 21},
    {0xb88, F_VMUDL, 30, 24, 10, 23},
    {0xb8c, F_VMADN, 30, 24,  9, 23},
    {0xb90, F_VMADM, 31,  0,  8, 22},
    {0xb94, F_VMADM, 31, 21, 15, 21},
    {0xb98, F_VMADH, 31, 20, 15, 20},
    {0xba0, F_VMADN, 31,  0,  8, 21},
};
static const Ins CHAIN_R[] = {
    {0xbdc, F_VXOR,  19, 19,  0, 19},
    {0xbe4, F_VMUDL, 30, 24, 13, 23},
    {0xbe8, F_VMADN, 30, 24, 12, 23},
    {0xbec, F_VMADM, 31,  0,  8, 22},
    {0xbf0, F_VMADM, 31, 19, 15, 19},
    {0xbf4, F_VMADH, 31, 18, 15, 18},
    {0xbfc, F_VMADN, 31,  0,  8, 19},
};

typedef struct {
    int16_t  r[NREG][8];
    int64_t  acc[8];
    uint16_t vco_lo, vco_hi, vcc_lo, vcc_hi, vce;
    int      sign_branch_taken;      /* must stay 0 on the measured path */
} Actx;

/* raw element field -> source lane, for the forms this chain uses */
static int sel(int elem, int lane) { return (elem >= 8) ? (elem - 8) : lane; }

static void A_run(Actx *c, const Ins *ch, int nins)
{
    int n, i;
    for (n = 0; n < nins; n++) {
        const Ins *p = &ch[n];
        CUR_SITE = p->site;
        for (i = 0; i < 8; i++) {
            int j = sel(p->elem, i);
            int16_t  vs_s = c->r[p->vs][i];
            int16_t  vt_s = c->r[p->vt][j];
            uint16_t vs_u = (uint16_t)vs_s, vt_u = (uint16_t)vt_s;
            switch (p->form) {
            case F_VXOR:
                c->r[p->vd][i] = 0; break;
            case F_VMUDL:                     /* unsigned x unsigned, >>16 */
                c->acc[i] = acc48(((int64_t)vs_u * (int64_t)vt_u) >> 16);
                c->r[p->vd][i] = dest_low(c->acc[i]); break;
            case F_VMADN:                     /* unsigned x signed, no shift */
                /* A-only mutation: read the UNSIGNED operand as signed. The
                 * constant vector's upper lanes exceed 0x7FFF, so this really
                 * changes the product rather than renaming a variable. */
                c->acc[i] = acc48(c->acc[i] +
                    (MUT_A == 1 ? (int64_t)vs_s : (int64_t)vs_u) * (int64_t)vt_s);
                c->r[p->vd][i] = dest_low(c->acc[i]); break;
            case F_VMADM:                     /* signed x unsigned, dest >>16 */
                c->acc[i] = acc48(c->acc[i] + (int64_t)vs_s * (int64_t)vt_u);
                c->r[p->vd][i] = dest_high(c->acc[i]); break;
            case F_VMADH:                     /* signed x signed, <<16 */
                if (MUT_A == 2) break;        /* A-only: omit a live product */
                c->acc[i] = acc48(c->acc[i] + (((int64_t)vs_s * (int64_t)vt_s) << 16));
                c->r[p->vd][i] = dest_high(c->acc[i]); break;
            }
        }
    }
}

/* literal compare: equality admitted via the carry bits; writes compare-result,
 * writes accumulator low, clears carry and extension */
static void A_compare(Actx *c, int vd, int vs, int16_t vt_scalar)
{
    int i; c->vcc_hi = 0;
    for (i = 0; i < 8; i++) {
        int lo = (c->vco_lo >> i) & 1;
        int ce = (c->vce >> i) & 1;
        /* Equality is admitted by the complement of the carry LOW bit OR the
         * extension bit. The carry HIGH half is deliberately NOT part of this
         * predicate - an earlier version wrongly included it. */
        int eq = (!lo) | ce;
        int16_t s = c->r[vs][i];
        int take = (s > vt_scalar) || (s == vt_scalar && eq);
        if (take) c->vcc_hi |= (uint16_t)(1u << i);
        c->r[vd][i] = take ? s : vt_scalar;
        c->acc[i] = (c->acc[i] & ~0xFFFFLL) | (uint16_t)c->r[vd][i];
    }
    c->vco_lo = c->vco_hi = 0; c->vce = 0;
}
/* literal clip: branch on the incoming carry sign bit; the non-sign branch
 * recomputes from the operands. The sign branch is unreachable on the measured
 * path (carry proven zero) and is flagged if ever entered. */
static void A_clip(Actx *c, int vd, int vs, int16_t vt_scalar)
{
    int i;
    for (i = 0; i < 8; i++) {
        int sign = (c->vco_lo >> i) & 1;
        int eq   = !((c->vco_hi >> i) & 1);   /* equality complement of carry hi */
        int ce   = (c->vce >> i) & 1;
        int16_t s = c->r[vs][i];
        int16_t chosen;
        if (sign) {
            /* sign path: addition of the two operands, its carry, and the low
             * compare-result recomputed under the extension/carry/zero
             * predicates when equality holds; the selected value is the
             * NEGATED second operand when that bit is set. */
            uint32_t sum = (uint32_t)(uint16_t)s + (uint32_t)(uint16_t)vt_scalar;
            int carry = (sum >> 16) & 1;
            if (eq) {
                int bit = ce ? ((sum & 0x1FFFF) <= 0x10000) : (!carry && (sum & 0xFFFF) == 0);
                if (bit) c->vcc_lo |= (uint16_t)(1u << i);
                else     c->vcc_lo &= (uint16_t)~(1u << i);
            }
            chosen = ((c->vcc_lo >> i) & 1) ? (int16_t)(-(int32_t)vt_scalar) : s;
            /* high half preserved on this path */
            c->sign_branch_taken = 1;
        } else {
            /* non-sign path: wrapped subtraction; the high compare-result is
             * recomputed from its signed result when equality holds. */
            uint16_t d = (uint16_t)((uint16_t)s - (uint16_t)vt_scalar);
            if (eq) {
                if ((int16_t)d >= 0) c->vcc_hi |= (uint16_t)(1u << i);
                else                 c->vcc_hi &= (uint16_t)~(1u << i);
            }
            chosen = ((c->vcc_hi >> i) & 1) ? vt_scalar : s;
            /* low half preserved on this path */
        }
        c->r[vd][i] = chosen;
        c->acc[i] = (c->acc[i] & ~0xFFFFLL) | (uint16_t)chosen;
    }
    c->vco_lo = c->vco_hi = 0; c->vce = 0;
}

static int verify_chain(const uint8_t *tx, size_t txn, const Ins *ch, int n,
                        const char *name, long long *checked, long long *bad)
{
    int i, ok = 1;
    for (i = 0; i < n; i++) {
        const Ins *p = &ch[i];
        uint32_t w; int op, vecbit, elem, vt, vs, vd, fn;
        if ((size_t)p->site + 4 > txn) {
            printf("  %s[%d] site %#05x beyond extraction\n", name, i, p->site); (*bad)++; ok=0; continue; }
        w = ((uint32_t)tx[p->site]<<24)|((uint32_t)tx[p->site+1]<<16)|
            ((uint32_t)tx[p->site+2]<<8)|tx[p->site+3];
        op=(w>>26)&0x3F; vecbit=(w>>25)&1; elem=(w>>21)&0xF;
        vt=(w>>16)&0x1F; vs=(w>>11)&0x1F; vd=(w>>6)&0x1F; fn=w&0x3F;
        (*checked)++;
        if (op!=0x12 || !vecbit || fn!=FORM_FUNCT[p->form] ||
            vd!=p->vd || vs!=p->vs || vt!=p->vt || elem!=p->elem) {
            printf("  MISMATCH %s[%d] blob %#05x: table(funct %#04x vd%d vs%d vt%d e%d)"
                   " vs decoded(op %#x vec%d funct %#04x vd%d vs%d vt%d e%d)\n",
                   name,i,p->site,FORM_FUNCT[p->form],p->vd,p->vs,p->vt,p->elem,
                   op,vecbit,fn,vd,vs,vt,elem);
            (*bad)++; ok=0;
        }
    }
    return ok;
}


#endif
