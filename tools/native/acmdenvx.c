/* ENVMIXER dual evaluator - A_INIT construction and clamp checkpoints.
 *
 *   A  driven from a DECODED INSTRUCTION TABLE of the init chain (blob site,
 *      form, source registers, raw element field, destination). Registers are
 *      eight lanes; the accumulator is an explicit 48-bit signed value, not an
 *      unbounded integer; element selection is resolved from the raw field;
 *      the compare and the clip are modelled with carry, compare-result and
 *      extension registers and their side effects. A reads the hardware
 *      constant vector from the local extraction through literal load and lane
 *      semantics - it never uses a formula for it.
 *
 *   B  the independently derived scalar recurrence, using the compact
 *      fractional-position formula, signed maximum for the compare under its
 *      proven precondition, and the exact wrapped-difference rule for the clip.
 *
 * A and B share raw inputs only. They do NOT share the lane-interpretation
 * helper: A obtains lane k's fraction by loading data, B by computing it. A
 * lane-ordering error therefore cannot occur identically on both sides.
 *
 * The constant vector is read at runtime from a local, gitignored extraction.
 * No microcode bytes are compiled in or committed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DATA_PATH "bin/aspboot.data.bin"
#define RAMP_DMEM 0xB0          /* the handler's own constant-vector address */

static int MUT_A = 0, MUT_B = 0;   /* one-sided mutation controls */
/* CONSTRUCTED non-interference control: hold the carry register at zero - the
 * proven reachable state - but seed the compare-result and extension registers
 * with deliberately different patterns. If the non-interference argument is
 * right, the numerical outputs must be IDENTICAL across every seed on the whole
 * measured operand population. This challenges the claim instead of assuming
 * it, which the zero-initialised evaluator could never do. */
static int SEED = 0;

/* ---------------- 48-bit accumulator ------------------------------------- */
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

/* ---------------- A: decoded instruction table --------------------------- */
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

/* ---------------- B: independent scalar recurrence ------------------------ */
static uint16_t B_frac(int k)      /* B's OWN lane interpretation, by formula */
{
    if (MUT_B == 1) k = (k + 1) & 7;               /* B-only: shift the ramp */
    return (k < 7) ? (uint16_t)(0x2000 * (k + 1)) : 0xFFFFu;
}
static void B_construct(int16_t cvol, int16_t ratm, uint16_t ratl,
                        int16_t tgt, int16_t *hi_pre, int16_t *lo_pre, int16_t *hi_post)
{
    int k;
    for (k = 0; k < 8; k++) {
        uint16_t f = B_frac(k);
        int64_t env = ((int64_t)cvol << 16)
                    + (((int64_t)f * (int64_t)ratl) >> 16)
                    + ((int64_t)f * (int64_t)ratm);
        int16_t h = clamp_s16(env >> 16);
        /* B's own low-word rule, derived in B's terms: the accumulated value is
         * carried as a signed 32-bit pair, so a value outside that range
         * saturates the pair and the low half reads all-ones or zero. B does
         * not call A's extractor. */
        int16_t l;
        if (env > (int64_t)2147483647LL)       l = (int16_t)0xFFFF;
        else if (env < (int64_t)(-2147483647LL - 1)) l = (int16_t)0x0000;
        else                                    l = (int16_t)(env & 0xFFFF);
        hi_pre[k] = h; lo_pre[k] = l;
        if (ratm > 0) { uint16_t d = (uint16_t)((uint16_t)h - (uint16_t)tgt);
                        hi_post[k] = ((int16_t)d >= 0) ? tgt : h; }
        else          { hi_post[k] = (h > tgt) ? h : tgt; }
    }
}

/* ---------------- table verification against the source instructions ------
 * Every hard-coded transcription entry is decoded from the actual word at its
 * BLOB OFFSET (not IMEM - the image loads at IMEM 0x080) and checked field by
 * field. A mistaken transcription cannot reach the derivation notes. */
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

/* ---------------- constructed result-clamp boundary control ---------------- */
static int result_clamp_control(void)
{
    /* An accumulator whose selected high field lies outside s16: a raw slice
     * cast and a signed clamp give different answers. */
    int64_t a = ((int64_t)0x1234 << 16) + 0x5678;   /* high field = 0x1234 -> in range */
    int64_t b = ((int64_t)0x7FFFF << 16);           /* high field far outside s16      */
    int16_t raw_b = (int16_t)((b >> 16) & 0xFFFF);
    int16_t cl_b  = dest_high(b);
    printf("CONSTRUCTED result-clamp boundary control (hardware semantics, not measured)\n");
    printf("  in-range   : raw %d  clamped %d\n",
           (int16_t)((a>>16)&0xFFFF), dest_high(a));
    printf("  out-of-range: raw %d  clamped %d\n", raw_b, cl_b);
    printf("  %s\n\n", (raw_b != cl_b)
        ? "CONTROL FIRES: signed clamping differs from a raw slice cast."
        : "CONTROL DID NOT FIRE - do not trust the extraction model.");
    return raw_b != cl_b;
}

/* ---------------- constructed accumulator-boundary control ---------------- */
static int acc_boundary_control(void)
{
    /* A product large enough to leave the 48-bit window: an unbounded integer
     * keeps growing where the hardware accumulator wraps. */
    int64_t big = 0x7FFFFFFFFFFFLL;
    int64_t unbounded = big + 0x4000LL;
    int64_t hw = acc48(big + 0x4000LL);
    printf("CONSTRUCTED accumulator-boundary control (hardware semantics, not measured)\n");
    printf("  unbounded int64 : %lld\n", (long long)unbounded);
    printf("  48-bit accumulator : %lld\n", (long long)hw);
    printf("  %s\n\n", (hw != unbounded)
        ? "CONTROL FIRES: the 48-bit accumulator wraps where an unbounded integer does not."
        : "CONTROL DID NOT FIRE - do not trust the accumulator model.");
    return hw != unbounded;
}

/* ---- CONSTRUCTED clamp controls: flag states the game never reaches --------
 * A must be a literal transcription, so outside the proven precondition it must
 * DIFFER from B's reductions; inside it, it must AGREE. Neither property alone
 * is sufficient - agreeing everywhere would mean A is just B, and differing
 * everywhere would mean the measured baseline was luck. */
/* EXHAUSTIVE enumeration of the compare's flag space against signed maximum.
 * The earlier claim that they coincide "under every flag state" was asserted
 * from six sampled states and then used to retire a precondition - which is
 * exactly where sampling must not stand in for enumeration. The space is
 * finite and small, so it is enumerated rather than argued:
 *   3 per-lane flag bits            -> 8 combinations
 *   operand grid covering both signs, zero, the representable extremes, and
 *   every ordering and equality relation between the two operands.
 * Reported as an enumerated result over a named finite space, not as a proof. */
static int compare_degeneracy_enumeration(long long *cells, long long *differ)
{
    static const int16_t V[] = { -32768, -32767, -1000, -1, 0, 1, 1000, 32766, 32767 };
    const int NV = (int)(sizeof V / sizeof *V);
    int fl, a, b;
    *cells = 0; *differ = 0;
    for (fl = 0; fl < 8; fl++) {
        uint16_t lo = (fl & 1) ? 0xFFFF : 0, hi = (fl & 2) ? 0xFFFF : 0,
                 ce = (fl & 4) ? 0xFFFF : 0;
        for (a = 0; a < NV; a++) for (b = 0; b < NV; b++) {
            Actx c; int i; int16_t vs = V[a], vt = V[b];
            memset(&c, 0, sizeof c);
            c.vco_lo = lo; c.vco_hi = hi; c.vce = ce;
            for (i = 0; i < 8; i++) c.r[20][i] = vs;
            A_compare(&c, 20, 20, vt);
            for (i = 0; i < 8; i++) {
                int16_t m = (vs > vt) ? vs : vt;
                (*cells)++;
                if (c.r[20][i] != m) (*differ)++;
            }
        }
    }
    return *differ == 0;
}

static int clamp_literal_controls(void)
{
    /* Two DIFFERENT expectations, because the two instructions differ:
     *
     *  COMPARE - its flag sensitivity is numerically DEGENERATE. The flags only
     *  affect whether equality is admitted, and on equality both candidate
     *  selections return the same number. So the literal compare equals signed
     *  maximum under EVERY flag state, not merely under the precondition. That
     *  is a proof about the operation, not a lucky measurement, and it means B's
     *  reduction needs no precondition at all.
     *
     *  CLIP - genuinely flag-sensitive: the carry low bit selects the path, the
     *  carry HIGH bit gates equality recomputation, and the extension changes
     *  the sign-path predicate. Outside the precondition it must diverge from
     *  B's reduced wrapped rule. */
    struct { const char *name; uint16_t vco_lo, vco_hi, vce; int clip_differ; } cs[] = {
        { "all flags clear (the proven precondition)", 0x00, 0x00, 0x00, 0 },
        { "carry lo set - selects the sign path",      0xFF, 0x00, 0x00, 1 },
        { "carry lo + extension set",                  0xFF, 0x00, 0xFF, 1 },
        { "carry HIGH set - gates clip equality",      0x00, 0xFF, 0x00, 1 },
        { "sign path, equality suppressed",            0xFF, 0xFF, 0x00, 1 },
        { "sign path, extension set",                  0xFF, 0x00, 0xFF, 1 },
    };
    int16_t ops[8] = { -300, -1, 0, 1, 300, 1000, -1000, 32767 };
    int16_t tgt = 1;
    unsigned t; int bad = 0, cmp_ever_differed = 0;
    printf("CONSTRUCTED literal-clamp controls (hardware semantics, not measured)\n");
    for (t = 0; t < sizeof cs / sizeof *cs; t++) {
        Actx c; int i, dc = 0, dl = 0;
        memset(&c, 0, sizeof c);
        c.vco_lo = cs[t].vco_lo; c.vco_hi = cs[t].vco_hi; c.vce = cs[t].vce;
        for (i = 0; i < 8; i++) c.r[20][i] = ops[i];
        A_compare(&c, 20, 20, tgt);
        for (i = 0; i < 8; i++) { int16_t b = (ops[i] > tgt) ? ops[i] : tgt;
                                  if (c.r[20][i] != b) dc++; }
        memset(&c, 0, sizeof c);
        c.vco_lo = cs[t].vco_lo; c.vco_hi = cs[t].vco_hi; c.vce = cs[t].vce;
        for (i = 0; i < 8; i++) c.r[20][i] = ops[i];
        A_clip(&c, 20, 20, tgt);
        for (i = 0; i < 8; i++) { uint16_t d = (uint16_t)((uint16_t)ops[i] - (uint16_t)tgt);
                                  int16_t b = ((int16_t)d >= 0) ? tgt : ops[i];
                                  if (c.r[20][i] != b) dl++; }
        printf("  %-42s cmp-diff %d (expect 0)  clip-diff %d (expect %s)\n",
               cs[t].name, dc, dl, cs[t].clip_differ ? ">0" : "0");
        if (dc != 0) { cmp_ever_differed = 1; bad++; }
        if (cs[t].clip_differ) { if (dl == 0) bad++; }
        else                   { if (dl != 0) bad++; }
    }
    { long long cells = 0, differ = 0;
      int exhaustive = compare_degeneracy_enumeration(&cells, &differ);
      printf("\n  EXHAUSTIVE compare enumeration: 8 flag combinations x 81 operand\n");
      printf("    pairs x 8 lanes = %lld cells; literal compare differs from signed\n", cells);
      printf("    maximum in %lld of them.\n", differ);
      printf("    %s\n", exhaustive
        ? "Enumerated zero over that finite space - the precondition may be retired\n"
          "      for the COMPARE only. This is an enumerated result, not a proof."
        : "NON-ZERO - the precondition STANDS and B's reduction is re-scoped.");
      printf("\n  VACUITY NOTE: the compare control cannot fire, by the very\n");
      printf("    degeneracy it was meant to test. It is reported separately and is\n");
      printf("    NOT counted among the firing controls; all discriminating power\n");
      printf("    for the clamps sits in the clip rows above.\n");
      (void)cmp_ever_differed; }
    printf("  %s\n\n", bad == 0
        ? "CONTROLS PASS: the clip diverges from B outside the precondition and\n"
          "    agrees inside it; the compare is degenerate-equal to signed max throughout."
        : "CONTROLS FAILED - expectations not met.");
    return bad == 0;
}

static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/w/all.bin";
    int16_t RAMP_A[8];
    FILE *f; uint8_t h[20]; uint32_t nf, dsz, fi; uint8_t *skip; uint8_t db[0x2c0];
    long long desc_checked=0, desc_bad=0;
    long long ninit=0, pre_n=0, pre_m=0, post_n=0, post_m=0, cmds_bad=0, sign_hits=0;
    MUT_A = (argc>2)?atoi(argv[2]):0; MUT_B = (argc>3)?atoi(argv[3]):0;
    SEED  = (argc>4)?atoi(argv[4]):0;
    if (MUT_A == 3) LOW_RULE = RULE_RAW;   /* A-only: revert to raw */
    if (MUT_A == 4) LOW_RULE = RULE_S16;   /* A-only: losing candidate */

    if (!acc_boundary_control()) return 3;
    if (!result_clamp_control()) return 3;
    if (!clamp_literal_controls()) return 3;

    /* A's constant vector: read from the local extraction, loaded with literal
     * quadword/lane semantics. Never compiled in, never committed. */
    { FILE *d = fopen(DATA_PATH, "rb");
      if (!d) { fprintf(stderr,"FATAL: %s missing - A cannot load the hardware "
                        "constant vector; refusing to substitute a formula.\n", DATA_PATH);
                return 2; }
      if (fread(db,1,sizeof db,d) != sizeof db) { fprintf(stderr,"FATAL: short read\n"); return 2; }
      fclose(d);
      { int k; for (k=0;k<8;k++)
          RAMP_A[k] = (int16_t)((db[RAMP_DMEM+2*k]<<8) | db[RAMP_DMEM+2*k+1]); } }

    /* machine-check the transcription table against the source instructions */
    { FILE *t = fopen("bin/aspboot.text.bin","rb");
      static uint8_t tx[0x1000]; size_t txn;
      if (!t) { fprintf(stderr,"FATAL: bin/aspboot.text.bin missing - cannot "
                        "verify the instruction table; refusing to run.\n"); return 2; }
      txn = fread(tx,1,sizeof tx,t); fclose(t);
      printf("Transcription table verification (blob offsets, image loads at IMEM 0x080)\n");
      { int okL = verify_chain(tx,txn,CHAIN_L,(int)(sizeof CHAIN_L/sizeof *CHAIN_L),
                               "CHAIN_L",&desc_checked,&desc_bad);
        int okR = verify_chain(tx,txn,CHAIN_R,(int)(sizeof CHAIN_R/sizeof *CHAIN_R),
                               "CHAIN_R",&desc_checked,&desc_bad);
        printf("  descriptors checked: %lld   mismatches: %lld\n\n", desc_checked, desc_bad);
        if (!okL || !okR) { fprintf(stderr,"FATAL: transcription does not match the "
                                    "source instructions; refusing to run.\n"); return 2; } } }

    f = fopen(path,"rb"); if(!f){perror(path);return 2;}
    fread(h,1,20,f); nf=be32(h+8); dsz=be32(h+16); skip=malloc(dsz);
    for (fi=0; fi<nf; fi++) {
        uint8_t fh[12]; uint32_t n,k,*w; uint16_t blk[0x20]; int have[0x20];
        if (fread(fh,1,12,f)!=12) break; n=be32(fh+8); if(n>4096) break;
        w=malloc(8*n);
        for(k=0;k<n;k++){uint8_t c[8];fread(c,1,8,f);w[2*k]=be32(c);w[2*k+1]=be32(c+4);}
        fread(skip,1,dsz,f); fseek(f,dsz,SEEK_CUR);
        memset(have,0,sizeof have);
        for (k=0;k<n;k++){
            uint32_t w0=w[2*k], w1=w[2*k+1];
            int op=(int)((w0>>24)&0xFF), fl=(int)((w0>>16)&0xFF);
            if (op==9){
                uint16_t v=(uint16_t)(w0&0xFFFF),hi=(uint16_t)((w1>>16)&0xFFFF),lw=(uint16_t)(w1&0xFFFF);
                if(fl&8){blk[0x1C]=v;have[0x1C]=1;blk[0x1E]=lw;have[0x1E]=1;}
                else if(fl&4){int o=(fl&2)?0x06:0x08;blk[o]=v;have[o]=1;}
                else{int o=(fl&2)?0x10:0x16;blk[o]=v;have[o]=1;blk[o+2]=hi;have[o+2]=1;blk[o+4]=lw;have[o+4]=1;}
            } else if (op==3 && (fl&1)) {
                Actx c; int i, bad=0;
                int16_t bhL[8],blL[8],bpL[8],bhR[8],blR[8],bpR[8];
                if(!(have[0x06]&&have[0x08]&&have[0x10]&&have[0x12]&&have[0x14]
                     &&have[0x16]&&have[0x18]&&have[0x1A])) continue;
                ninit++;
                /* ONE context, handler order: left then right, flags carried */
                memset(&c,0,sizeof c);
                if (SEED) {   /* carry stays 0; the other two are perturbed */
                    c.vcc_hi = (uint16_t)(SEED*0x9E37); c.vcc_lo = (uint16_t)(SEED*0x1234);
                    c.vce    = (uint16_t)(SEED*0x5A5A);
                }
                for(i=0;i<8;i++){ c.r[31][i]=1; c.r[30][i]=RAMP_A[i]; }
                c.r[24][0]=(int16_t)blk[0x10]; c.r[24][1]=(int16_t)blk[0x12];
                c.r[24][2]=(int16_t)blk[0x14]; c.r[24][3]=(int16_t)blk[0x16];
                c.r[24][4]=(int16_t)blk[0x18]; c.r[24][5]=(int16_t)blk[0x1A];
                c.r[24][6]=(int16_t)blk[0x1C]; c.r[24][7]=(int16_t)blk[0x1E];
                c.r[20][7]=(int16_t)blk[0x06];        /* LSV -> lane 7 */
                A_run(&c, CHAIN_L, (int)(sizeof CHAIN_L/sizeof *CHAIN_L));
                { int16_t preL[8], preLo[8];
                  memcpy(preL,c.r[20],sizeof preL); memcpy(preLo,c.r[21],sizeof preLo);
                  B_construct((int16_t)blk[0x06],(int16_t)blk[0x12],blk[0x14],
                              (int16_t)blk[0x10],bhL,blL,bpL);
                  for(i=0;i<8;i++){ pre_n+=2;
                    if(preL[i]!=bhL[i]){pre_m++;bad=1;}
                    if(preLo[i]!=blL[i]){pre_m++;bad=1;} }
                  if((int16_t)blk[0x12] > 0) A_clip(&c,20,20,(int16_t)blk[0x10]);
                  else                        A_compare(&c,20,20,(int16_t)blk[0x10]);
                  for(i=0;i<8;i++){ post_n++; if(c.r[20][i]!=bpL[i]){post_m++;bad=1;} } }
                c.r[18][7]=(int16_t)blk[0x08];
                A_run(&c, CHAIN_R, (int)(sizeof CHAIN_R/sizeof *CHAIN_R));
                { int16_t preR[8], preRo[8];
                  memcpy(preR,c.r[18],sizeof preR); memcpy(preRo,c.r[19],sizeof preRo);
                  B_construct((int16_t)blk[0x08],(int16_t)blk[0x18],blk[0x1A],
                              (int16_t)blk[0x16],bhR,blR,bpR);
                  for(i=0;i<8;i++){ pre_n+=2;
                    if(preR[i]!=bhR[i]){pre_m++;bad=1;}
                    if(preRo[i]!=blR[i]){pre_m++;bad=1;} }
                  if((int16_t)blk[0x18] > 0) A_clip(&c,18,18,(int16_t)blk[0x16]);
                  else                        A_compare(&c,18,18,(int16_t)blk[0x16]);
                  for(i=0;i<8;i++){ post_n++; if(c.r[18][i]!=bpR[i]){post_m++;bad=1;} } }
                if (c.sign_branch_taken) sign_hits++;
                if (bad) cmds_bad++;
            }
        }
        free(w);
    }
    printf("A_INIT checkpoints  (A-mutation=%d  B-mutation=%d)\n\n", MUT_A, MUT_B);
    printf("  A_INIT commands scored              : %lld\n", ninit);
    printf("  PRE-CLAMP construction compared     : %lld   mismatches: %lld\n", pre_n, pre_m);
    printf("  POST-CLAMP numerical compared       : %lld   mismatches: %lld\n", post_n, post_m);
    printf("  commands with any mismatch          : %lld\n", cmds_bad);
    printf("  A clip sign-branch entered (must be 0): %lld\n", sign_hits);
    printf("  low-form extraction boundary hits     : %lld\n", LOW_BOUNDARY);
    { int q; long long live=0,dead=0;
      printf("\n  boundary events by site (must sum to the total):\n");
      for(q=0;q<NSITE;q++){ printf("    blob %#05x  %-20s %8lld  %s\n",
            BSITE[q],BNAME[q],BHIT[q],BLIVE[q]?"LIVE to envelope":"dead scratch");
            if(BLIVE[q]) live+=BHIT[q]; else dead+=BHIT[q]; }
      printf("    live-to-envelope %lld + dead-destination %lld = %lld (total %lld)\n",
             live,dead,live+dead,LOW_BOUNDARY); }
    printf("\n  candidate low-rule divergence on the boundary population:\n");
    printf("    raw vs sat32 differ : %lld\n", CAND_DIFF_AB);
    printf("    raw vs s16   differ : %lld\n", CAND_DIFF_AC);
    printf("    sat32 vs s16 differ : %lld   <- what the cartridge must decide\n", CAND_DIFF_BC);
    printf("\n  overflow sign over ALL %lld boundary events:\n", LOW_BOUNDARY);
    printf("    LEFT  positive %lld  negative %lld\n", OVF_POS_L, OVF_NEG_L);
    printf("    RIGHT positive %lld  negative %lld\n", OVF_POS_R, OVF_NEG_R);
    printf("    TOTAL positive %lld  negative %lld\n", OVF_POS, OVF_NEG);
    printf("  ungrounded NEGATIVE-branch takings this run : %lld\n",
           SL_ENV_LOW_NEG_UNGROUNDED);
    printf("\n%s\n", (pre_m==0 && post_m==0)
        ? "A and B agree on construction AND post-clamp values."
        : "A and B DISAGREE.");
    return (pre_m||post_m) ? 1 : 0;
}
