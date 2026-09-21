/* Native ACMD interpreter - stages 1-3. See sl_acmd.h for scope and authority.
 *
 * Every opcode below cites where its ENCODING comes from (include/PR/abi.h's
 * emit macros, which are sourced) and marks its execution SEMANTICS as
 * INFERRED where the microcode is the only real authority and the corpus is
 * silent on it. That split is deliberate: it is the difference between a fact
 * and a plausible guess, and this file contains both.
 */
#ifndef __sgi
#include "sl_acmd.h"

#include <string.h>

/* THE MICROCODE'S OWN DATA - the ENVMIXER per-lane ramp and the RESAMPLE
 * 64x4 polyphase table from the aspMain data segment. Both are ROM-derived
 * and are NOT compiled in: sl_ucode.c reads them out of the user's ROM at
 * start-up and sets them here through sl_acmd_set_ucode_tables, once, before
 * mainproc. Until then sl_acmd_exec refuses to run. There is deliberately no
 * default, no fallback and no generated header (the build-time generator
 * that used to fill these was retired at v0.2.0 - docs/releases/v0.2.0.md). */
#define SL_ENVMIX_LANES   8
#define SL_RESAMPLE_PHASES 64
#define SL_RESAMPLE_TAPS   4
static short sl_ucode_envmix_ramp[SL_ENVMIX_LANES];
static short sl_ucode_resample_tab[SL_RESAMPLE_PHASES * SL_RESAMPLE_TAPS];
static int   sl_ucode_tables_ready;

void sl_acmd_set_ucode_tables(const short *ramp, const short *taps) {
    memcpy(sl_ucode_envmix_ramp, ramp, sizeof sl_ucode_envmix_ramp);
    memcpy(sl_ucode_resample_tab, taps, sizeof sl_ucode_resample_tab);
    sl_ucode_tables_ready = 1;
}
int sl_acmd_ucode_tables_ready(void) { return sl_ucode_tables_ready; }

/* Numeric choices for POLEF, swept independently during derivation.
 * Defaults are the values that reproduced the cartridge. */
#ifndef SL_POLEF_SLOT
#define SL_POLEF_SLOT 3
#endif
#ifndef SL_POLEF_SHIFT
#define SL_POLEF_SHIFT 14
#endif
#ifndef SL_POLEF_ROUND
#define SL_POLEF_ROUND 0
#endif
/* The derived one-pole recursion is OFF by default. It is not a guess - its
 * structure is established (see A_POLEF) - but it reproduces the cartridge
 * WORSE than passing samples through untouched (78010 differing bytes vs
 * 75450), so shipping it on would mean adopting a model measurement rejects.
 * Build with -DSL_POLEF_IDENTITY=0 to enable it for further derivation. */
#ifndef SL_POLEF_IDENTITY
#define SL_POLEF_IDENTITY 1
#endif
/* 1 = scalar recurrence, 2 = 8-sample block expansion. The block form is the
 * algebraic expansion of the same recurrence - not a different model:
 *   y[k] = g*SUM(j=0..k) r^(k-j)*x[j]  +  r^(k+1)*y[-1]
 * The input path carries TWO Q14 factors (g and r^(k-j)) and the state path
 * ONE, so they cannot share a shift: >>28 and >>14 respectively. */
#ifndef SL_POLEF_MODE
#define SL_POLEF_MODE 2
#endif
#ifndef SL_POLEF_ACC64
#define SL_POLEF_ACC64 1
#endif
#ifndef SL_POLEF_TRUNC_PER_TAP
#define SL_POLEF_TRUNC_PER_TAP 0
#endif

/* ---- opcode numbers: include/PR/abi.h:34-49 ---------------------------- */
enum {
    A_SPNOOP = 0, A_ADPCM = 1, A_CLEARBUFF = 2, A_ENVMIXER = 3,
    A_LOADBUFF = 4, A_RESAMPLE = 5, A_SAVEBUFF = 6, A_SEGMENT = 7,
    A_SETBUFF = 8, A_SETVOL = 9, A_DMEMMOVE = 10, A_LOADADPCM = 11,
    A_MIXER = 12, A_INTERLEAVE = 13, A_POLEF = 14, A_SETLOOP = 15
};

/* ---- audio flags: include/PR/abi.h:56-58 -------------------------------- */
enum { A_INIT = 0x01, A_LOOP = 0x02, A_VOL = 0x04, A_AUX = 0x08 };
/* A_LEFT is 0x02 and A_RIGHT is 0x00; A_RATE is 0x00. abi.h:56-67. */
#define A_LEFT_BIT 0x02

/* ADPCM was gated off while its semantics were derived but unvalidated. It is
 * now ON, on the strength of tools/native/acmdvoicex.c: three evaluators - a
 * literal transcription of the vector sequence, an independently derived
 * scalar form, and THIS interpreter - agree on all 349712 samples and all 4427
 * final states of the fully grounded population, with six semantic controls
 * and a vacuity control all firing.
 *
 * That is a cross-check of the DERIVED SEMANTICS, not cartridge PCM
 * validation: no sample here has been compared against anything the console
 * produced. It can catch an internally inconsistent derivation; it cannot
 * catch one that is consistently wrong.
 *
 * DEFAULT 0, RESTORED 2026-08-30, then DEFAULT 1 from 2026-08-31 - and the
 * difference is not a change of mind, it is that the condition the 08-30 note
 * names has since been met. That note's objection was precise: "no sample here
 * has been compared against anything the console produced". They have been now.
 *
 * GROUNDING, against the cartridge ACMD capture with per-task before/after DRAM
 * brackets (tools/native/acmd_census.py, 435 aligned tasks):
 *
 *   frames byte-exact 435/435   bytes compared 5227776
 *   of which nonzero  3320001 (63.51%)   bytes differing 0
 *   max |err| 0   RMS 0.00 over 1869567 nonzero written samples
 *
 * A passing score proves nothing on its own, so the decode is CHALLENGED. Six
 * controls, each breaking one load-bearing decision (SL_ACMD_ADMUT above);
 * five move cartridge-visible bytes decisively:
 *
 *   1 nibble order        eligible 349712   bytes differing 1117034
 *   2 scale-skip removed  eligible  36832   bytes differing  466117
 *   3 predictor book      eligible  21857   bytes differing 1206448
 *   4 half history pair   eligible  43714   bytes differing 1107638
 *   6 residual tap        eligible 349712   bytes differing 1026184
 *
 * Control 2 is worth naming: "fixing" the blob's blez quirk at scale >= 12
 * breaks 194 frames, so the quirk is real, load-bearing, and correctly
 * reproduced rather than guessed.
 *
 * ONE DECISION IS NOT GROUNDED, and it is not being promoted quietly. Control 5
 * perturbs the 48-bit accumulator's low-32 wrap and its eligibility over the
 * whole capture is ZERO - the accumulator never leaves int32 range here, so
 * wrap and plain arithmetic are indistinguishable on this fixture. The wrap is
 * kept because it is what the microcode's VSAR HIGH:MID read implies, and every
 * occurrence is counted at runtime in sl_acmd_ungrounded_adwrap. That counter
 * is proven live rather than assumed: with its threshold lowered to 2^20 it
 * reads 158404 on this same capture, and at the real int32 threshold it reads
 * 0, with byte-exactness unchanged either way. So the zero is a measurement,
 * not an absence of instrumentation.
 *
 * Build with -DSL_ACMD_ADPCM=0 to restore the fail-loud path. */
#ifndef SL_ACMD_WRLOG
#define SL_ACMD_WRLOG 0
#endif

#ifndef SL_ACMD_ADPCM
#define SL_ACMD_ADPCM 1
#endif

/* The ADPCM accumulator's low-32 wrap is the one decode decision no
 * measurement grounds: control 5's eligibility over the whole capture is
 * ZERO, and neither the corpus nor the tree carries RSP vector ISA
 * semantics to derive it from. Default 0 = reach it and FAIL LOUD rather
 * than execute it silently. Set to 1 for diagnostic runs. */
#ifndef SL_ACMD_ADWRAP
#define SL_ACMD_ADWRAP 0
#endif

#ifndef SL_ACMD_BOOK_DMA
#define SL_ACMD_BOOK_DMA 1
#endif

static const char *const OPNAME[16] = {
    "SPNOOP", "ADPCM", "CLEARBUFF", "ENVMIXER", "LOADBUFF", "RESAMPLE",
    "SAVEBUFF", "SEGMENT", "SETBUFF", "SETVOL", "DMEMMOVE", "LOADADPCM",
    "MIXER", "INTERLEAVE", "POLEF", "SETLOOP"
};

const char *sl_acmd_opname(int op) {
    return (op >= 0 && op < 16) ? OPNAME[op] : "?";
}

const char *sl_acmd_errstr(int e) {
    switch (e) {
    case SL_ACMD_OK:            return "ok";
    case SL_ACMD_ERR_OPCODE:    return "opcode not implemented (stage 1-3 only)";
    case SL_ACMD_ERR_DMEM:      return "DMEM access out of range";
    case SL_ACMD_ERR_DRAM:      return "physical address outside mapped window";
    case SL_ACMD_ERR_SEGMENT:   return "non-identity segment base";
    case SL_ACMD_ERR_UNGROUNDED:
        return "ungrounded semantic reached (ADPCM accumulator wrap); "
               "build -DSL_ACMD_ADWRAP=1 to permit it diagnostically";
    default:                    return "?";
    }
}

/* DMEM WRITE EVENTS. A tracker that diffs values sees value-change, not
 * writes: a store of the value already present is invisible to it, and a mover
 * that relabels its destination loses the source's provenance. Both are
 * instrumented here at the PHYSICAL operations instead. kind: 0 store (this
 * opcode produced the value), 1 move (provenance transfers from src in DMEM),
 * 2 load (provenance is the DRAM address in src). Compiled out by default. */
#ifndef SL_ACMD_DWLOG
#define SL_ACMD_DWLOG 0
#endif
/* RESAMPLER SEMANTIC CHALLENGES. Each perturbs ONE load-bearing decision so it
 * can be shown to move cartridge-visible consumer bytes. Compiled out by
 * default; production sees none of it.
 *   1 phase fraction zeroed      - tap group forced to 0, integer advance kept
 *   2 phase-index selection      - group index shifted by one
 *   3 integer advance            - sample advance shifted by one
 *   4 coefficient ordering       - the four coefficients applied in reverse,
 *                                 sample-to-tap mapping preserved
 *   5 history source             - the carried history quad perturbed
 *   6 source-tap mapping         - coefficients preserved and IN ORDER, but a
 *                                 different SET of samples is read (spacing
 *                                 doubled). A permutation would not be an
 *                                 independent control: sum tp[k]*x[p(k)] and
 *                                 sum tp[s(k)]*x[k] are the same family, so
 *                                 reversing the mapping is algebraically the
 *                                 same mutation as reversing the coefficients -
 *                                 measured, both scored 30812258 identically.
 * Each is enabled ONLY on commands that qualify for the semantic it claims,
 * and eligibility, application and effect are counted separately. */
/* ---- ENVMIXER, promoted to production ---------------------------------
 * Semantics as accepted: three independent evaluators agree byte-for-byte over
 * all 5734 measured commands, and the self-chained state is cartridge-exact at
 * all 1103 grounded cross-task boundaries. Output arithmetic is challenged by
 * five controls on marker-proven cartridge-visible populations.
 *
 * UNGROUNDED PATHS ARE COUNTED, NOT ASSUMED. Every one of them is a branch this
 * capture never exercised, so it is implemented from the handler and its use is
 * recorded at runtime rather than trusted silently. */
/* ADPCM DECODE CHALLENGES. The decoder scoring byte-exact against the cartridge
 * is necessary and not sufficient: a comparison that no perturbation can move
 * is vacuous. Each control below breaks ONE load-bearing decision in the decode
 * so it can be shown to move cartridge-visible bytes, and each is counted for
 * ELIGIBILITY separately from application - a control the capture never
 * exercises must report that rather than look like a pass.
 *   1 nibble order        - the two nibbles of each byte swapped
 *   2 scale-skip removed  - the blob's `blez` quirk at scale >= 12 "fixed",
 *                           which is precisely the hardware behaviour the
 *                           decoder reproduces deliberately
 *   3 predictor book      - book index shifted by one
 *   4 half history pair   - the second 8-lane half's (p2,p1) swapped
 *   5 accumulator wrap    - the 48-bit accumulator read WITHOUT the low-32
 *                           wrap, i.e. arithmetic instead of VSAR HIGH:MID
 *   6 residual tap        - the current-residual tap 0x800 halved
 * Compiled out by default; production sees none of it. */
#ifndef SL_ACMD_ADMUT
#define SL_ACMD_ADMUT 0
#endif
#if SL_ACMD_ADMUT
int sl_ad_mut = 0;
unsigned long sl_ad_elig = 0, sl_ad_applied = 0;
#endif
/* THE ONE ADPCM DECISION THE CAPTURE DOES NOT EXERCISE. Control 5 above
 * perturbs the 48-bit accumulator's low-32 wrap, and its eligibility over the
 * whole 435-task capture is ZERO: the accumulator never leaves int32 range, so
 * wrap and plain arithmetic are indistinguishable on this fixture. The wrap is
 * what the microcode's VSAR HIGH:MID read implies and it is kept, but it is
 * UNGROUNDED, so every occurrence is counted rather than trusted silently. A
 * nonzero value here means the fixture finally reached that branch and the
 * decode's output there rests on an unvalidated choice. */
unsigned long sl_acmd_ungrounded_adwrap = 0;
unsigned long sl_acmd_ungrounded_neg   = 0;  /* low-result negative overflow   */
unsigned long sl_acmd_ungrounded_rsinit = 0; /* RESAMPLE A_INIT writeback path */
unsigned long sl_acmd_ungrounded_state = 0;  /* resampler state bytes +0x0C..11 */
/* COMMANDS AFFECTED, which is NOT the same quantity as counter hits: the state
 * and rsinit counters fire per state-writeback inside a single RESAMPLE, and
 * sl_low_sat32's fires per accumulator conversion, so one command can produce
 * many hits. s->n_cmds is a monotonic command ordinal, so comparing against the
 * last ordinal counted attributes hits to distinct commands without
 * restructuring the interpreter. */
unsigned long sl_acmd_cmds_state = 0, sl_acmd_cmds_rsinit = 0;
static unsigned long sl_last_state_cmd = 0, sl_last_rsinit_cmd = 0;
/* CONTROL on the attribution key itself. s->n_cmds is monotonic across tasks
 * (the state object is initialised once, so it never resets), but that is an
 * argument, not evidence. These count the same events keyed on the TASK-LOCAL
 * command index, which CAN alias across task boundaries. If the local key
 * under-counts the monotonic one, aliasing is real and the monotonic key is
 * what avoids it; if they agree, no two attributed events shared a task-local
 * ordinal and the attribution was never exercised. Either way it is measured. */
unsigned long sl_acmd_cmds_state_local = 0, sl_acmd_cmds_rsinit_local = 0;
static unsigned long sl_last_state_local = 0xFFFFFFFFu,
                     sl_last_rsinit_local = 0xFFFFFFFFu;
/* The UNACCEPTED decode path's actual usage. */
unsigned long sl_acmd_adpcm_cmds = 0, sl_acmd_adpcm_samples = 0;
unsigned long sl_acmd_adpcm_frames = 0;

typedef struct { short hi[8], lo[8]; } sl_env;

static short sl_cl16(long long v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (short) v;
}
static long long sl_acc48(long long v) {
    v &= 0xFFFFFFFFFFFFLL;
    if (v & 0x800000000000LL) v -= 0x1000000000000LL;
    return v;
}
/* The low half of a 48-bit accumulator. The saturating form was selected by the
 * cartridge 12 of 12; the NEGATIVE branch is appendix-derived and was never
 * exercised (374 of 374 measured overflow events were positive), so it is
 * counted. */
static short sl_low_sat32(long long a) {
    long long t = a >> 31;
    if (t == 0 || t == -1) return (short) (a & 0xFFFF);
    if (a > 0) return (short) 0xFFFF;
    sl_acmd_ungrounded_neg++;
    return 0;
}
static short sl_vmulf(short a, short b) {
    return sl_cl16(((((long long) a * (long long) b) << 1) + 0x8000) >> 16);
}
static short sl_mixstep(short dst, short in, short g) {
    long long acc = (((long long) dst * (long long) 0x7FFF) << 1) + 0x8000;
    acc += ((long long) in * (long long) g) << 1;
    return sl_cl16(acc >> 16);
}
static void sl_env_construct(short cvol, short ratm, unsigned short ratl, sl_env *e) {
    int k;
    for (k = 0; k < 8; k++) {
        unsigned short f = (unsigned short) sl_ucode_envmix_ramp[k];
        long long a = sl_acc48(((long long) f * (long long) ratl) >> 16);
        a = sl_acc48(a + (long long) f * (long long) ratm);
        a = sl_acc48(a + (((long long) cvol) << 16));
        e->hi[k] = sl_cl16(a >> 16);
        e->lo[k] = sl_low_sat32(a);
    }
}
static void sl_env_clamp(sl_env *e, short tgt, short ratm) {
    int k;
    for (k = 0; k < 8; k++) {
        if (ratm > 0) {
            unsigned short d = (unsigned short)((unsigned short) e->hi[k]
                                              - (unsigned short) tgt);
            e->hi[k] = ((short) d >= 0) ? tgt : e->hi[k];
        } else {
            e->hi[k] = (e->hi[k] > tgt) ? e->hi[k] : tgt;
        }
    }
}
static void sl_env_advance(sl_env *e, short ratm, unsigned short ratl) {
    int k;
    for (k = 0; k < 8; k++) {
        unsigned int t = (unsigned int)(unsigned short) e->lo[k] + (unsigned int) ratl;
        e->lo[k] = (short)(t & 0xFFFF);
        e->hi[k] = sl_cl16((int) e->hi[k] + (int) ratm + (int)((t >> 16) & 1));
    }
}

/* The 32-byte resampler state scratch. s7 is set ONCE, at blob 0x004
 * (addi s7,zero,3984 = 0xF90), and is never reassigned anywhere in the
 * extracted text - a scan of every instruction writing register 23 across the
 * whole image returns that single hit. It is the same scratch base the mixer
 * uses, which is why the untouched bytes carry mixer state. */
#define SL_RESAMPLE_SCRATCH 0xF90
#define SL_ACMD_SCRATCH     0xF90

#ifndef SL_ACMD_RSMUT
#define SL_ACMD_RSMUT 0
#endif
#if SL_ACMD_RSMUT
/* Instrumentation STORAGE, not only the code that fills it, lives inside this
 * guard - a guard covering the writes while leaving the arrays at file scope
 * would still compile ~74 KB of globals into every build. Verified against the
 * built artefact rather than by reading: a production compile of this file has
 * no sl_rs symbols at all and bss 0. */
int sl_rs_mut = 0;
long long sl_rs_wb_hist = 0, sl_rs_wb_input = 0;
int sl_rs_eligible = 0;
uint32_t sl_rs_mA[512]; int sl_rs_mV[512], sl_rs_mF[512], sl_rs_mR[512], sl_rs_mN;
unsigned char sl_rs_win[512][128];   /* DMEM row-64 .. row+63 at writeback time */
long long sl_rs_elig = 0, sl_rs_applied = 0, sl_rs_taps_moved = 0, sl_rs_taps_seen = 0;
#endif
#if SL_ACMD_DWLOG
void sl_acmd_dwlog(uint32_t off, uint32_t len, int op, int kind, uint32_t src);
#define DWLOG(o,l,k,s2) sl_acmd_dwlog((o),(l),SL_CUROP,(k),(s2))
#else
#define DWLOG(o,l,k,s2) ((void)0)
#endif
#if SL_ACMD_DWLOG
static int SL_CUROP = -1;     /* only exists when the hook is compiled in, so
                               * production emits no store for it at all */
/* The command ordinal WITHIN the current exec call, exported for the same
 * reason the opcode is: a provenance recorder cannot name the exact writing
 * command from outside a whole-task call. Measurement guard only - absent from
 * the production binary, which is asserted by nm rather than assumed. */
int sl_acmd_cur_index = -1;
#endif

/* ---- DMEM is big-endian s16, as on hardware --------------------------- */
static int16_t dget(const sl_acmd_state *s, uint32_t off) {
    return (int16_t) ((s->dmem[off] << 8) | s->dmem[off + 1]);
}
static void dset(sl_acmd_state *s, uint32_t off, int32_t v) {
    if (v > 32767)  v = 32767;          /* saturate, as the RSP does */
    if (v < -32768) v = -32768;
    DWLOG(off, 2, 0, 0);
    s->dmem[off]     = (uint8_t) ((v >> 8) & 0xFF);
    s->dmem[off + 1] = (uint8_t) (v & 0xFF);
}

/* The audio state block is REAL DMEM at SL_ACMD_STATE, not cached fields - see
 * the layout note in sl_acmd.h. Halfwords big-endian, as `sh` leaves them. */
uint16_t sl_acmd_sget(const sl_acmd_state *s, uint32_t off) {
    return (uint16_t) ((s->dmem[SL_ACMD_STATE + off] << 8)
                     |  s->dmem[SL_ACMD_STATE + off + 1]);
}
void sl_acmd_sset(sl_acmd_state *s, uint32_t off, uint32_t v) {
    DWLOG(SL_ACMD_STATE + off, 2, 0, 0);
    s->dmem[SL_ACMD_STATE + off]     = (uint8_t) ((v >> 8) & 0xFF);
    s->dmem[SL_ACMD_STATE + off + 1] = (uint8_t) (v & 0xFF);
}
uint32_t sl_acmd_sget32(const sl_acmd_state *s, uint32_t off) {
    return ((uint32_t) sl_acmd_sget(s, off) << 16) | sl_acmd_sget(s, off + 2);
}
void sl_acmd_sset32(sl_acmd_state *s, uint32_t off, uint32_t v) {
    sl_acmd_sset(s, off, (v >> 16) & 0xFFFF);
    sl_acmd_sset(s, off + 2, v & 0xFFFF);
}
#define S_IN    sl_acmd_sget(s, 0x00)
#define S_OUT   sl_acmd_sget(s, 0x02)
#define S_COUNT sl_acmd_sget(s, 0x04)

static int dmem_ok(sl_acmd_state *s, uint32_t off, uint32_t len) {
    if (off + len > SL_DMEM_SIZE)
        return 0;
    if (off + len > s->dmem_high_water)
        s->dmem_high_water = off + len;
    return 1;
}

/* DRAM WRITE-EVENT HOOK. A byte takes a new writer epoch whenever a handler
 * PHYSICALLY writes it, whether or not the value changes. Detecting writes by
 * comparing after-images cannot see a store that writes the same value back -
 * exactly the writer class a provenance audit exists to exclude. Compiled out
 * entirely unless SL_ACMD_WRLOG is defined, so production is unchanged. */

#if SL_ACMD_WRLOG
void sl_acmd_wrlog(uint32_t addr, uint32_t len, int op);
#define WRLOG(a,l,o) sl_acmd_wrlog((a),(l),(o))
#else
#define WRLOG(a,l,o) ((void)0)
#endif

/* Physical -> host. A subtraction, because the census measured every addr
 * field physical and every segment base 0. */
static uint8_t *phys(sl_acmd_state *s, uint32_t addr, uint32_t len) {
    if (addr < s->dram_lo || addr + len > s->dram_lo + s->dram_size)
        return 0;
    return s->dram + (addr - s->dram_lo);
}

/* Segmented DRAM address: seg[w1>>24] + (w1 & 0xFFFFFF). MEASURED at
 * SETLOOP blob 0x3cc and LOADADPCM blob 0x21c - both shift w1 right by 24,
 * scale by 4 and load the base from the segment table at DMEM 800. Every
 * base the census measured is 0, so this reduces to w1, but the resolution
 * is performed rather than assumed. */
static uint32_t segaddr(const sl_acmd_state *s, uint32_t w1) {
    return s->seg[(w1 >> 24) & (SL_ACMD_NSEG - 1)] + (w1 & 0xFFFFFF);
}

void sl_acmd_init(sl_acmd_state *s, uint8_t *dram, uint32_t lo, uint32_t size) {
    memset(s, 0, sizeof *s);
    s->dram = dram;
    s->dram_lo = lo;
    s->dram_size = size;
}

int sl_acmd_exec(sl_acmd_state *s, const uint32_t *words, uint32_t n) {
    uint32_t k;

    /* No tables, no execution: a mixer running RESAMPLE against zeros would
     * produce silence that looks like an audio bug rather than a start-up
     * one. sl_ucode.c sets them before mainproc; this is the belt to that
     * brace, and it is reported through the ordinary error path. */
    if (!sl_ucode_tables_ready) {
        s->err_op = -1;
        s->err_index = 0;
        return SL_ACMD_ERR_UNGROUNDED;
    }

    for (k = 0; k < n; k++) {
        uint32_t w0 = words[2 * k], w1 = words[2 * k + 1];
        int op = (int) ((w0 >> 24) & 0xFF);
        uint8_t *p;

#if SL_ACMD_DWLOG
        SL_CUROP = op;
        sl_acmd_cur_index = (int) k;
#endif
        s->n_cmds++;
        s->n_by_op[op & 15]++;
        s->err_op = op;
        s->err_index = k;

        switch (op) {

        case A_SEGMENT:     /* abi.h aSegment: w1 = seg<<24 | base */
            {
                uint32_t seg = (w1 >> 24) & 0xFF, base = w1 & 0xFFFFFF;
                if (base != 0)
                    return SL_ACMD_ERR_SEGMENT;   /* never observed; refuse */
                if (seg < SL_ACMD_NSEG)
                    s->seg[seg] = base;
            }
            break;

        case A_SETBUFF:     /* abi.h aSetBuffer(f,i,o,c). Handler blob 0x26c:
                             * bgtz on flags&A_AUX selects WHICH field group is
                             * written; the two groups are disjoint, so an aux
                             * SETBUFF leaves in/out/count untouched. */
            {
                uint32_t f = (w0 >> 16) & 0xFF;
                uint16_t a = (uint16_t) ((w0 & 0xFFFF) + SL_DMEM_BUFBASE);
                uint16_t b = (uint16_t) (((w1 >> 16) & 0xFFFF) + SL_DMEM_BUFBASE);
                s->setbuff_flags = (uint8_t) f;
                if (f & A_AUX) {
                    sl_acmd_sset(s, 0x0A, a);
                    sl_acmd_sset(s, 0x0C, b);
                    sl_acmd_sset(s, 0x0E, (w1 & 0xFFFF) + SL_DMEM_BUFBASE);
                } else {
                    sl_acmd_sset(s, 0x00, a);
                    sl_acmd_sset(s, 0x02, b);
                    sl_acmd_sset(s, 0x04, w1 & 0xFFFF);   /* NOT offset */
                }
            }
            break;

        case A_CLEARBUFF:   /* abi.h aClearBuffer(d,c) */
            {
                /* Handler blob 0x15c. A zero count clears NOTHING (beq at
                 * 0x160). Otherwise the loop writes 16 bytes an iteration and
                 * re-tests before the decrement, so the effective length is
                 * BLOCK-ROUNDED to ceil(count/16)*16, not count. Every count
                 * this capture contains is a multiple of 16, so no observed
                 * frame distinguishes the two - implemented from the derived
                 * handler rather than left wrong until something exposes it. */
                uint32_t d = (w0 & 0xFFFF) + SL_DMEM_BUFBASE, c = w1 & 0xFFFF;
                if (c) {
                    uint32_t n = ((c + 15) / 16) * 16;
                    if (!dmem_ok(s, d, n)) return SL_ACMD_ERR_DMEM;
                    DWLOG(d, n, 0, 0);
                    memset(s->dmem + d, 0, n);
                }
            }
            break;

        case A_DMEMMOVE:    /* abi.h aDMEMMove(i,o,c) */
            {
                uint32_t i = (w0 & 0xFFFF) + SL_DMEM_BUFBASE,
                         o = ((w1 >> 16) & 0xFFFF) + SL_DMEM_BUFBASE,
                         c = w1 & 0xFFFF;
                if (!dmem_ok(s, i, c) || !dmem_ok(s, o, c))
                    return SL_ACMD_ERR_DMEM;
                DWLOG(o, c, 1, i);   /* mover: provenance comes from i */
                memmove(s->dmem + o, s->dmem + i, c);   /* may overlap */
            }
            break;

        case A_ENVMIXER:
            {
                int init = (int)((w0 >> 16) & 1), aux = (int)((w0 >> 16) & 8);
                uint32_t st = w1 & 0xFFFFFF;
                uint32_t in = S_IN, d3 = S_OUT;
                uint32_t d2 = sl_acmd_sget(s, 0x0A), d1 = sl_acmd_sget(s, 0x0C),
                         d0 = sl_acmd_sget(s, 0x0E);
                int cnt = (int) S_COUNT, g, G, step = aux ? 16 : 0;
                short tgtL, ratmL, tgtR, ratmR, dry, wet;
                unsigned short ratlL, ratlR;
                sl_env L, R;
                uint32_t sc = SL_ACMD_SCRATCH;

                tgtL = (short) sl_acmd_sget(s, 0x10);
                ratmL = (short) sl_acmd_sget(s, 0x12);
                ratlL = sl_acmd_sget(s, 0x14);
                tgtR = (short) sl_acmd_sget(s, 0x16);
                ratmR = (short) sl_acmd_sget(s, 0x18);
                ratlR = sl_acmd_sget(s, 0x1A);
                dry = (short) sl_acmd_sget(s, 0x1C);
                wet = (short) sl_acmd_sget(s, 0x1E);
                /* A_AUX clear redirects both aux destinations to a 16-byte
                 * scratch that never advances, so those writes are discarded */
                if (!aux) { d1 = d0 = sc + 80; }
                G = (cnt > 0) ? ((cnt + 15) / 16) : 1;

                p = phys(s, st, 80);
                if (!p) return SL_ACMD_ERR_DRAM;

                if (init) {
                    sl_env_construct((short) sl_acmd_sget(s, 0x06), ratmL, ratlL, &L);
                    sl_env_construct((short) sl_acmd_sget(s, 0x08), ratmR, ratlR, &R);
                } else {
                    int q;
                    for (q = 0; q < 8; q++) {
                        L.hi[q] = (short)((p[2*q]      << 8) | p[2*q + 1]);
                        L.lo[q] = (short)((p[16 + 2*q] << 8) | p[17 + 2*q]);
                        R.hi[q] = (short)((p[32 + 2*q] << 8) | p[33 + 2*q]);
                        R.lo[q] = (short)((p[48 + 2*q] << 8) | p[49 + 2*q]);
                    }
                    /* the parameter vector is RELOADED from the saved state, not
                     * taken from the live task block - the handler's delay-slot
                     * load is overwritten before its first use */
                    tgtL  = (short)((p[64] << 8) | p[65]);
                    ratmL = (short)((p[66] << 8) | p[67]);
                    ratlL = (unsigned short)((p[68] << 8) | p[69]);
                    tgtR  = (short)((p[70] << 8) | p[71]);
                    ratmR = (short)((p[72] << 8) | p[73]);
                    ratlR = (unsigned short)((p[74] << 8) | p[75]);
                    dry   = (short)((p[76] << 8) | p[77]);
                    wet   = (short)((p[78] << 8) | p[79]);
                }

                if (!dmem_ok(s, sc, 96)) return SL_ACMD_ERR_DMEM;

                if (init) {                       /* peeled first output group */
                    int k;
                    sl_env_clamp(&L, tgtL, ratmL);
                    sl_env_clamp(&R, tgtR, ratmR);
                    for (k = 0; k < 8; k++) {
                        short iv = dget(s, in + 2*k);
                        short gl = sl_vmulf(L.hi[k], dry), wl = sl_vmulf(L.hi[k], wet);
                        short gr = sl_vmulf(R.hi[k], dry), wr = sl_vmulf(R.hi[k], wet);
                        dset(s, d3 + 2*k, sl_mixstep(dget(s, d3 + 2*k), iv, gl));
                        dset(s, d1 + 2*k, sl_mixstep(dget(s, d1 + 2*k), iv, wl));
                        dset(s, d2 + 2*k, sl_mixstep(dget(s, d2 + 2*k), iv, gr));
                        dset(s, d0 + 2*k, sl_mixstep(dget(s, d0 + 2*k), iv, wr));
                    }
                    in += 16; d3 += 16; d2 += 16; d1 += step; d0 += step;
                }
                sl_env_advance(&L, ratmL, ratlL);      /* the pre-loop advance */
                for (g = 0; g < G - (init ? 1 : 0); g++) {
                    int k;
                    sl_env_clamp(&L, tgtL, ratmL);
                    sl_env_advance(&R, ratmR, ratlR);
                    /* the LEFT pair is stored INSIDE the loop, before the
                     * trailing left advance - blob 0xc50..0xd18 */
                    for (k = 0; k < 8; k++) {
                        dset(s, sc + 2*k, L.hi[k]);
                        dset(s, sc + 16 + 2*k, L.lo[k]);
                    }
                    for (k = 0; k < 8; k++) {
                        short iv = dget(s, in + 2*k);
                        short gl = sl_vmulf(L.hi[k], dry), wl = sl_vmulf(L.hi[k], wet);
                        dset(s, d3 + 2*k, sl_mixstep(dget(s, d3 + 2*k), iv, gl));
                        dset(s, d1 + 2*k, sl_mixstep(dget(s, d1 + 2*k), iv, wl));
                    }
                    sl_env_clamp(&R, tgtR, ratmR);
                    sl_env_advance(&L, ratmL, ratlL);
                    for (k = 0; k < 8; k++) {
                        short iv = dget(s, in + 2*k);
                        short gr = sl_vmulf(R.hi[k], dry), wr = sl_vmulf(R.hi[k], wet);
                        dset(s, d2 + 2*k, sl_mixstep(dget(s, d2 + 2*k), iv, gr));
                        dset(s, d0 + 2*k, sl_mixstep(dget(s, d0 + 2*k), iv, wr));
                    }
                    in += 16; d3 += 16; d2 += 16; d1 += step; d0 += step;
                }
                { int k;
                  for (k = 0; k < 8; k++) {
                      dset(s, sc + 32 + 2*k, R.hi[k]);
                      dset(s, sc + 48 + 2*k, R.lo[k]);
                  }
                  /* the parameter vector written back is the one this command
                   * RAN WITH, not whatever the live task block now holds */
                  dset(s, sc + 64, tgtL);  dset(s, sc + 66, ratmL);
                  dset(s, sc + 68, (short) ratlL);
                  dset(s, sc + 70, tgtR);  dset(s, sc + 72, ratmR);
                  dset(s, sc + 74, (short) ratlR);
                  dset(s, sc + 76, dry);   dset(s, sc + 78, wet); }
                WRLOG(st, 80, A_ENVMIXER);
                memcpy(p, s->dmem + sc, 80);
            }
            break;

        case A_LOADBUFF:    /* abi.h aLoadBuffer(s): DMA count bytes DRAM->DMEM
                             * at the setbuff INPUT offset. INFERRED from the
                             * emitters: src/libultra/audio/save.c:38-41 pairs
                             * aSetBuffer(...,c) with the transfer that follows,
                             * and the census shows SETBUFF always immediately
                             * preceding. Pure memcpy - no byte order here. */
            if (!dmem_ok(s, S_IN, S_COUNT)) return SL_ACMD_ERR_DMEM;
            p = phys(s, w1, S_COUNT);
            if (!p) return SL_ACMD_ERR_DRAM;
            DWLOG(S_IN, S_COUNT, 2, w1);
            memcpy(s->dmem + S_IN, p, S_COUNT);
            break;

        case A_SAVEBUFF:    /* abi.h aSaveBuffer(s): DMEM->DRAM from the
                             * setbuff OUTPUT offset. INFERRED, same basis. */
            if (!dmem_ok(s, S_OUT, S_COUNT)) return SL_ACMD_ERR_DMEM;
            p = phys(s, w1, S_COUNT);
            if (!p) return SL_ACMD_ERR_DRAM;
            WRLOG(w1, S_COUNT, A_SAVEBUFF);
            memcpy(p, s->dmem + S_OUT, S_COUNT);
            break;

        case A_INTERLEAVE:  /* abi.h aInterleave(l,r): w1 = L<<16 | R.
                             * INFERRED: count is the PER-CHANNEL byte count
                             * (save.c:38 sets outCount<<1 before it, then
                             * outCount<<2 before the save of the stereo
                             * result). Moves 2-byte units, so no byte order. */
            {
                uint32_t L = ((w1 >> 16) & 0xFFFF) + SL_DMEM_BUFBASE,
                         R = (w1 & 0xFFFF) + SL_DMEM_BUFBASE;
                uint32_t c = S_COUNT, j;
                if (!dmem_ok(s, L, c) || !dmem_ok(s, R, c) ||
                    !dmem_ok(s, S_OUT, c * 2))
                    return SL_ACMD_ERR_DMEM;
                for (j = 0; j < c; j += 2) {
                    DWLOG(S_OUT + 2 * j + 0, 2, 1, L + j);
                    DWLOG(S_OUT + 2 * j + 2, 2, 1, R + j);
                    s->dmem[S_OUT + 2 * j + 0] = s->dmem[L + j + 0];
                    s->dmem[S_OUT + 2 * j + 1] = s->dmem[L + j + 1];
                    s->dmem[S_OUT + 2 * j + 2] = s->dmem[R + j + 0];
                    s->dmem[S_OUT + 2 * j + 3] = s->dmem[R + j + 1];
                }
            }
            break;

        case A_MIXER:       /* abi.h aMix(f,g,i,o): w0 gain, w1 = i<<16 | o.
                             * SEMANTICS DERIVED FROM THE CARTRIDGE MICROCODE,
                             * not fitted and not copied from an HLE. The
                             * aspMain handler for opcode 12 runs, per 8-lane
                             * group:
                             *   VMULF vDst, vDst, v31[e6]   v31[6] = 0x7FFF
                             *   VMACF vDst, vSrc, v30[e0]   v30[0] = gain
                             * VMULF SETS the 48-bit accumulator to
                             * (a*b)<<1 + 0x8000 - the <<1 is the "fractional"
                             * doubling and 0x8000 is its rounding constant -
                             * and VMACF ADDS (a*b)<<1 with no further constant.
                             * The stored result is clamp_s16(acc >> 16).
                             *
                             * The destination therefore passes through a
                             * multiply by 0x7FFF, which is NOT exact unity.
                             * That near-miss is intrinsic to the hardware op,
                             * and it is why `dst + ((src*gain)>>15)` - the
                             * obvious reading, and what mature HLE
                             * implementations use - disagrees with the
                             * cartridge. Measured: the derived form is exact on
                             * all 11469 samples of the MIXER-only oracle
                             * (6369 two-node chains, 5100 four-node), where
                             * that form was wrong on 2510 and 3266.
                             *
                             * The handler consumes 32 bytes per iteration,
                             * matching the measured 32-byte-aligned counts. */
            {
                int32_t g = (int16_t) (w0 & 0xFFFF);
                /* MIXER carries its OWN DMEM operands and adds 0x5c0 to both
                 * of them: blob 0xd5c addi s3,s3,1472 (w1 low = dst) and
                 * blob 0xd64 addi s4,s4,1472 (w1 high = src). Only the count
                 * comes from the state block, unshifted. */
                uint32_t i = ((w1 >> 16) & 0xFFFF) + SL_DMEM_BUFBASE,
                         o = (w1 & 0xFFFF) + SL_DMEM_BUFBASE;
                uint32_t c = S_COUNT, j;
                if (!dmem_ok(s, i, c) || !dmem_ok(s, o, c))
                    return SL_ACMD_ERR_DMEM;
                for (j = 0; j < c; j += 2) {
                    long long acc = ((long long) dget(s, o + j) * 0x7FFF) << 1;
                    acc += 0x8000;                                  /* VMULF */
                    acc += ((long long) dget(s, i + j) * g) << 1;   /* VMACF */
                    acc >>= 16;
                    if (acc >  32767) acc =  32767;
                    if (acc < -32768) acc = -32768;
                    dset(s, o + j, (int32_t) acc);
                }
            }
            break;

        case A_LOADADPCM:   /* abi.h aLoadADPCM(c,d). In the zero-voice frames
                             * this is a POLE FILTER coefficient table, not a
                             * voice codebook: the census measured all 25 of
                             * them per frame immediately followed by POLEF
                             * (25/25). Stored big-endian s16. */
            {
                uint32_t c = w0 & 0xFFFF, j;
                if (c > sizeof s->coef) c = sizeof s->coef;
                p = phys(s, w1, c);
                if (!p) return SL_ACMD_ERR_DRAM;
                for (j = 0; j + 1 < c; j += 2)
                    s->coef[j / 2] = (int16_t) ((p[j] << 8) | p[j + 1]);
                s->coef_len = (int) (c / 2);
                /* The DMA lands at absolute DMEM 0x4C0 (blob 0x22c). POLEF
                 * reads coef[] above; ADPCM indexes the DMEM copy by
                 * predictor, so both consumers see the same bytes. */
#if SL_ACMD_BOOK_DMA
                {
                    uint32_t cb = w0 & 0xFFFF;
                    if (cb > SL_DMEM_SIZE - SL_ADPCM_BOOK)
                        cb = SL_DMEM_SIZE - SL_ADPCM_BOOK;
                    p = phys(s, segaddr(s, w1), cb);
                    if (!p) return SL_ACMD_ERR_DRAM;
                    DWLOG(SL_ADPCM_BOOK, cb, 2, w1);
                    memcpy(s->dmem + SL_ADPCM_BOOK, p, cb);
                }
#endif
            }
            break;

        case A_POLEF:
            {
                /* DERIVED from the aspMain opcode-14 handler.
                 *   coefficients: 16 shorts at DMEM 0x4C0 from LOADADPCM;
                 *     tail[m] = coef[8+m] is r^(m+1) in Q14 (unscaled),
                 *     scaled[m] = (tail[m] * (gain<<2)) >> 16 = gain*r^(m+1)
                 *   state: 4 shorts from the segmented w1 address; the tap uses
                 *     element 7 = state[3]; the element-6 tap multiplies the
                 *     table's first 8 words, measured all-zero, so contributes
                 *     nothing.
                 *   block: 8 samples (16 bytes) per iteration.
                 *   arithmetic: VMUDH/VMADH accumulate (a*b)<<16 with NO
                 *     intermediate extraction, then VSAR HIGH/MID recombined
                 *     x4 == acc>>14, clamped. So truncation happens ONCE per
                 *     output sample, not once per recurrence step - which is
                 *     exactly where the earlier scalar recurrence was wrong.
                 *   state writeback: the last 4 outputs of the final block. */
                uint32_t c = S_COUNT, base, k, j;
                int32_t gainq = (int16_t) (w0 & 0xFFFF);
                int32_t tail[8], scaled[8];
                int32_t prev;
                if (!dmem_ok(s, S_IN, c) || !dmem_ok(s, S_OUT, c))
                    return SL_ACMD_ERR_DMEM;
                p = phys(s, w1 & 0xFFFFFF, 8);
                if (!p) return SL_ACMD_ERR_DRAM;
                for (j = 0; j < 8; j++) {
                    tail[j]   = s->coef[8 + j];
                    scaled[j] = (int32_t) (((long long) tail[j] * (gainq << 2)) >> 16);
                }
                prev = ((w0 >> 16) & 1) ? 0 : (int16_t) ((p[6] << 8) | p[7]);
                for (base = 0; base + 16 <= c; base += 16) {
                    int32_t y[8];
                    for (k = 0; k < 8; k++) {
                        long long acc = (long long) tail[k] * prev;
                        acc += (long long) gainq * dget(s, S_IN + base + 2 * k);
                        for (j = 1; j <= k; j++)
                            acc += (long long) scaled[j - 1]
                                 * dget(s, S_IN + base + 2 * (k - j));
                        acc >>= 14;
                        if (acc >  32767) acc =  32767;
                        if (acc < -32768) acc = -32768;
                        y[k] = (int32_t) acc;
                    }
                    for (k = 0; k < 8; k++) dset(s, S_OUT + base + 2 * k, y[k]);
                    prev = y[7];
                }
                /* State writeback: EIGHT bytes, the last four outputs of the
                 * final block. Measured from the handler, not inferred - the
                 * writeback DMA length immediate is 7, so 7+1 = 8 bytes, and
                 * its source is the output pointer minus 8. This previously
                 * wrote only the final sample at offset 6, leaving the other
                 * three shorts untouched; the zero-voice oracle could not see
                 * it because that oracle scores only bytes the interpreter
                 * chooses to write, so bytes never written are invisible to it.
                 * Found by full-DRAM comparison against the cartridge. */
                if (base >= 16) {
                    int k2;
                    WRLOG(w1 & 0xFFFFFF, 8, A_POLEF);
                    for (k2 = 0; k2 < 4; k2++) {
                        int32_t v = dget(s, S_OUT + base - 8 + 2 * k2);
                        p[2*k2]     = (uint8_t) ((v >> 8) & 0xFF);
                        p[2*k2 + 1] = (uint8_t) (v & 0xFF);
                    }
                }
                (void) prev;
            }
            break;

        case A_RESAMPLE:
            {
                /* DERIVED from the address-generation chain, instruction by
                 * instruction (see the decode in the commit message):
                 *   acc_lane = phase + 2*lane*pitch, Q16 (0x10000 = 1 sample)
                 *   integer advance = acc >> 16
                 *   phase index     = (acc & 0xFFFF) >> 10, one of 64 groups
                 *   coeff address   = 0xC0 + index*8  -> 4 shorts per group
                 *   input address   = t0 + 2*intadv, t0 = dmemin - 8 with the
                 *                     4 history shorts written there, so taps
                 *                     read buf[intadv .. intadv+3] where
                 *                     buf[0..3] = history, buf[4+m] = input[m]
                 * Products are formed with VMULF and reduced with VADD. */
                uint32_t c = S_COUNT, n, k;
                int32_t pitch = (int32_t)(w0 & 0xFFFF);
                /* Flag bit 1 gates the read side of the input-overlap mechanism
                 * (blob 0x868 andi t2,a3,2 -> 0x874..0x884). The capture never
                 * sets it: 7803 commands with flags 0x00 and 106 with 0x01, and
                 * not one with bit 1. Deriving it is possible; validating it is
                 * not, so it FAILS LOUD rather than being guessed at. */
                if ((w0 >> 16) & 2) return SL_ACMD_ERR_OPCODE;
                int32_t hist[4], step, nout = (int32_t)(c / 2);
                unsigned int acc;
                if (!dmem_ok(s, S_IN, c) || !dmem_ok(s, S_OUT, c))
                    return SL_ACMD_ERR_DMEM;
                p = phys(s, w1 & 0xFFFFFF, 32);
                if (!p) return SL_ACMD_ERR_DRAM;
                for (k = 0; k < 4; k++)
                    hist[k] = ((w0 >> 16) & 1) ? 0
                            : (int16_t)((p[2*k] << 8) | p[2*k+1]);
                acc  = ((w0 >> 16) & 1) ? 0u
                     : (unsigned int)((p[8] << 8) | p[9]);
                step = pitch << 1;
#if SL_ACMD_RSMUT
                {   /* QUALIFICATION, per the semantic each control claims */
                    int isinit = (int)((w0 >> 16) & 1);
                    unsigned ph0 = acc;
                    int frac_nz = (!isinit) && ph0 != 0 && (ph0 & 0x3FF) != 0;
                    int hist_read = 0;
                    { int32_t nn; for (nn = 0; nn < nout && !hist_read; nn++) {
                        unsigned a2 = ph0 + (unsigned)nn * (unsigned)step;
                        int32_t ia2 = (int32_t)(a2 >> 16);
                        int kk; for (kk = 0; kk < 4; kk++)
                            if (ia2 + kk < 4) { hist_read = 1; break; } } }
                    switch (sl_rs_mut) {
                    case 1: case 2: sl_rs_eligible = frac_nz; break;
                    case 3: case 4: case 6: sl_rs_eligible = !isinit; break;
                    case 5: sl_rs_eligible = (!isinit) && hist_read; break;
                    default: sl_rs_eligible = 0; break; }
                    if (sl_rs_mut) { if (sl_rs_eligible) { sl_rs_elig++; sl_rs_applied++; } }
                }
                if (sl_rs_mut == 5 && sl_rs_eligible)
                    for (k = 0; k < 4; k++) hist[k] ^= 0x0040;
#endif

                for (n = 0; (int32_t) n < nout; n++) {
                    int32_t ia = (int32_t)(acc >> 16);
                    int32_t idx = (int32_t)((acc & 0xFFFF) >> 10);
                    const short *tp;
#if SL_ACMD_RSMUT
                    if (sl_rs_eligible) {
                        if (sl_rs_mut == 1) idx = 0;
                        if (sl_rs_mut == 2) idx = (idx + 1) & 63;
                        if (sl_rs_mut == 3) {
                            /* count only where the SOURCE ADDRESS actually moves */
                            int kk; for (kk = 0; kk < 4; kk++) { sl_rs_taps_seen++;
                                if (((ia + kk) < 4) != ((ia + 1 + kk) < 4)
                                    || (ia + kk) != (ia + 1 + kk)) sl_rs_taps_moved++; }
                            ia = ia + 1; } }
#endif
                    tp = &sl_ucode_resample_tab[idx * 4];
                    long long sum = 0;
                    for (k = 0; k < 4; k++) {
                        int32_t jsel = (int32_t) k;
                        int32_t j, xv; long long cf;
#if SL_ACMD_RSMUT
                        /* 6 changes WHICH SAMPLES are read - a different set,
                         * not a reordering - leaving coefficients in order */
                        if (sl_rs_mut == 6 && sl_rs_eligible) jsel = 2 * (int32_t) k;
#endif
                        j = ia + jsel;
                        xv = (j < 4) ? hist[j & 3] : dget(s, S_IN + 2 * (j - 4));
                        cf = tp[k];
#if SL_ACMD_RSMUT
                        /* 4 reverses the COEFFICIENTS, leaving the mapping */
                        if (sl_rs_mut == 4 && sl_rs_eligible) cf = tp[3 - k];
#endif
                        long long pr = ((cf * xv) << 1) + 0x8000;
                        pr >>= 16;
                        if (pr >  32767) pr =  32767;
                        if (pr < -32768) pr = -32768;
                        sum += pr;
                    }
                    if (sum >  32767) sum =  32767;
                    if (sum < -32768) sum = -32768;
                    dset(s, S_OUT + 2 * n, (int32_t) sum);
                    acc += (unsigned int) step;
                }
                /* State writeback: the handler DMAs 32 bytes back after the
                 * loop, so the next invocation continues from the samples the
                 * taps will need and from the running phase fraction. */
                {
                    int32_t fa = (int32_t)(acc >> 16);
#if SL_ACMD_RSMUT
                    /* Does the WRITEBACK quad ever source from carried history?
                     * If it never does, history perturbation cannot move the
                     * state score, and the state population is not a control
                     * for it. Counted rather than assumed. */
                    { int kk; for (kk = 0; kk < 4; kk++)
                        if (fa + kk < 4) sl_rs_wb_hist++; else sl_rs_wb_input++; }
#endif
                    /* Physical extent, from the handler's own store block at
                     * blob 0x0a58..0x0a90: +0x00..0B and +0x10..1F. Nothing
                     * writes +0x0C..0F on either path - that gap FALLS OUT of
                     * the store mapping, it is not assumed. */
#if SL_ACMD_RSMUT
                    if (sl_rs_mut == 11) {        /* OLD production behaviour  */
                        WRLOG(w1 & 0xFFFFFF, 10, A_RESAMPLE);
                    } else
#endif
                    { WRLOG(w1 & 0xFFFFFF,      12, A_RESAMPLE);
                      WRLOG((w1 & 0xFFFFFF)+16, 16, A_RESAMPLE); }
                    for (k = 0; k < 4; k++) {
                        int32_t j = fa + (int32_t) k;
                        int32_t xv = (j < 4) ? hist[j & 3]
                                   : dget(s, S_IN + 2 * (j - 4));
                        p[2*k]   = (uint8_t)((xv >> 8) & 0xFF);
                        p[2*k+1] = (uint8_t)(xv & 0xFF);
                    }
#if SL_ACMD_RSMUT
                    { unsigned pacc = acc;
                      if (sl_rs_mut == 9) pacc ^= 0x40;        /* stored phase  */
                      p[8] = (uint8_t)((pacc >> 8) & 0xFF);
                      p[9] = (uint8_t)(pacc & 0xFF); }
#else
                    p[8] = (uint8_t)((acc >> 8) & 0xFF);   /* SSV v23[0],8(s7)  blob 0x0a58 */
                    p[9] = (uint8_t)(acc & 0xFF);
#endif
                    /* +0x0A..0B and +0x10..1F, derived instruction by instruction
                     * from blob 0x0a5c..0x0a90 rather than from any field label:
                     *
                     *   0a5c LDV v16,0(s1)      s1 = the quad source address
                     *   0a68 addi s1,s1,8       step past the quad
                     *   0a6c sub  a1,s1,in      offset from the input base
                     *   0a70 andi a0,a1,15      misalignment within a 16-byte row
                     *   0a74 sub  s1,s1,a0      round the address DOWN to that row
                     *   0a78 beq  a0,zero,0a84  aligned -> store zero
                     *   0a80 sub  a0,16,a0      else store 16 - misalignment
                     *   0a84 sh   a0,10(s7)     -> +0x0A..0B
                     *   0a88 LDV v3,0(s1) / 0a8c LDV v3[8],8(s1)
                     *   0a90 SQV v3,16(s7)      -> +0x10..1F
                     *
                     * So +0x10..1F is the 16-byte ALIGNED INPUT ROW spanning the
                     * buffer boundary, and +0x0A..0B is the byte count by which
                     * the NEXT command rewinds its input pointer to re-consume
                     * it - the read side at blob 0x874..0x884 writes those 16
                     * bytes just below the input pointer and subtracts this
                     * count. It is a carried input OVERLAP, so a 4-tap filter
                     * can straddle two command buffers. */
                    {
                        int32_t s1 = (int32_t) S_IN + 2 * fa;   /* after the +8 */
                        int32_t mis = (s1 - (int32_t) S_IN) & 15;
                        int32_t row = s1 - mis;
                        int32_t rew = mis ? (16 - mis) : 0;
                        int32_t kk;
#if SL_ACMD_RSMUT
                        /* STATE-GATE controls: each perturbs something the
                         * WRITEBACK itself consumes. The six output controls
                         * cannot serve here - measured, they leave the state
                         * score identical. */
                        if (sl_rs_mut ==  7) row += 16;        /* row address   */
                        if (sl_rs_mut ==  8) rew ^= 2;         /* rewind count  */
#endif
#if SL_ACMD_RSMUT
                        if (sl_rs_mN < 512) { sl_rs_mA[sl_rs_mN] = w1 & 0xFFFFFF;
                            sl_rs_mV[sl_rs_mN] = mis; sl_rs_mF[sl_rs_mN] = fa;
                            sl_rs_mR[sl_rs_mN] = row;
                            { int wq; for (wq = 0; wq < 128; wq++) {
                                int ad = row - 64 + wq;
                                sl_rs_win[sl_rs_mN][wq] =
                                    (ad >= 0 && ad < SL_DMEM_SIZE) ? s->dmem[ad] : 0; } }
                            sl_rs_mN++; }
#endif
#if SL_ACMD_RSMUT
                        if (sl_rs_mut == 11) goto rs_writeback_done;
#endif
                        /* +0x10..11 is written from a source the documented
                         * LDV/SQV semantics do not account for: measured, its
                         * value is the halfword one BYTE below row whenever
                         * mis == 0 and row = 6 (mod 16). 468 bytes across the
                         * capture remain ungrounded here, so every write of them
                         * is counted rather than presented as settled. */
                        sl_acmd_ungrounded_state++;
                        if (sl_last_state_cmd != s->n_cmds) {
                            sl_last_state_cmd = s->n_cmds; sl_acmd_cmds_state++; }
                        if (sl_last_state_local != s->err_index) {
                            sl_last_state_local = s->err_index;
                            sl_acmd_cmds_state_local++; }
                        p[10] = (uint8_t)((rew >> 8) & 0xFF);
                        p[11] = (uint8_t)(rew & 0xFF);
                        for (kk = 0; kk < 16; kk++)
                            p[16 + kk] = (row + kk >= 0 && row + kk < SL_DMEM_SIZE)
                                       ? s->dmem[row + kk] : 0;
                    }
#if SL_ACMD_RSMUT
                    rs_writeback_done: ;
#endif
                    /* +0x0C..0F: no LOCAL STORE on either path - but the
                     * outbound DMA copies the whole 32-byte scratch, so these
                     * four bytes ARE written, with whatever the scratch holds.
                     * s7 = 0xF90 (blob 0x004, addi s7,zero,3984), the SAME
                     * scratch the mixer uses, so in practice they carry the
                     * mixer's state: measured, 356 of 424 A_INIT instances have
                     * a prior ENVMIXER command in the same task as their actual
                     * last writer, 68 are task-start. Modelling the DMA rather
                     * than the local stores is what makes them observable. */
                    if (((w0 >> 16) & 1)
#if SL_ACMD_RSMUT
                        && sl_rs_mut != 10        /* outbound DMA semantics    */
#endif
                       ) {
                        /* A_INIT only. On a CONTINUATION the handler DMAs the
                         * 32 bytes IN first (blob 0x840, skipped by the bgtz at
                         * 0x838), so the scratch already holds the DRAM value
                         * and copying it back out preserves it - which is why
                         * leaving these bytes alone is correct there. On an
                         * A_INIT there is no inbound DMA, so the scratch still
                         * holds whatever last used it and the outbound DMA
                         * carries that. Derived from the control flow, not
                         * fitted: modelling the DMA unconditionally makes
                         * continuations wrong (measured: 4605 vs 668). */
                        int gq;
                        /* A_INIT-only path: these four bytes come from whatever
                         * last used the shared scratch. 151 of them are grounded
                         * through an in-task mixer writer; the rest are
                         * task-start DMEM this model does not carry. Counted. */
                        sl_acmd_ungrounded_rsinit++;
                        if (sl_last_rsinit_cmd != s->n_cmds) {
                            sl_last_rsinit_cmd = s->n_cmds; sl_acmd_cmds_rsinit++; }
                        if (sl_last_rsinit_local != s->err_index) {
                            sl_last_rsinit_local = s->err_index;
                            sl_acmd_cmds_rsinit_local++; }
                        for (gq = 0; gq < 4; gq++)
                            p[12 + gq] = s->dmem[SL_RESAMPLE_SCRATCH + 0x0C + gq];
                    }
                }
            }
            break;

        case A_SETLOOP:     /* abi.h aSetLoop(a). Handler blob 0x3cc, nine
                             * instructions: resolve the segmented w1 and store
                             * it to the audio state block at +0x10. Nothing
                             * else - it does not touch in/out/count. */
            sl_acmd_sset32(s, 0x10, segaddr(s, w1));
            break;

        case A_ADPCM:
#if !SL_ACMD_ADPCM
            return SL_ACMD_ERR_OPCODE;   /* derived, not yet accepted */
#else
            /* abi.h aADPCMdec(f,s): w0 flags, w1 = segmented state address.
             *
             * DERIVED FROM THE MICROCODE, blob 0x3f0..0x688, not from a
             * plausible ADPCM decoder. Every constant below is read out of
             * bin/aspboot.data.bin rather than chosen:
             *   DMEM 0x30 holds four masks isolating successive nibbles of a
             *     halfword; DMEM 0x38 holds four scalers renormalising each
             *     back to the top four bits. Their product is identical in
             *     every lane, so an unpacked residual is signed4 << 12 and the
             *     sign extension is free - the handler contains no sign logic.
             *   DMEM 0x00 supplies the constant vector: one element is the
             *     VSAR recombine multiplier (32) and another the
             *     current-residual tap (2048), which is what makes the
             *     residual pass at unity through the >>11.
             *
             * Layout: the 32-byte incoming state is DMA'd to dmemout and the
             * decoded frames start at dmemout+32 (blob 0x4a8 advances s3
             * before the loop). That prefix is deliberate - it is the history
             * a following RESAMPLE reads. Writeback source is the LAST block
             * written (s3-32) and the destination is ALWAYS the resolved w1,
             * never the loop address: A_LOOP redirects the INPUT only. */
            {
                uint32_t flags = (w0 >> 16) & 0xFF;
                uint32_t st = segaddr(s, w1);
                uint32_t in = S_IN, out = S_OUT;
                int32_t  cnt = (int32_t) S_COUNT;
                int16_t  hist[8];
                int nf, f, h, j, i;

                if (!dmem_ok(s, out, 32)) return SL_ACMD_ERR_DMEM;
                DWLOG(out, 32, 0, 0);
                memset(s->dmem + out, 0, 32);
                if (!(flags & A_INIT)) {
                    /* blob 0x460: the delay slot always sets the source to the
                     * resolved w1; only A_LOOP falls through to [state+0x10]. */
                    uint32_t src = (flags & A_LOOP) ? sl_acmd_sget32(s, 0x10) : st;
                    p = phys(s, src, 32);
                    if (!p) return SL_ACMD_ERR_DRAM;
                    DWLOG(out, 32, 2, w1 & 0xFFFFFF);
                    memcpy(s->dmem + out, p, 32);
                }
                /* blob 0x4a4: history is the SECOND half of the state block. */
                for (j = 0; j < 8; j++) hist[j] = dget(s, out + 16 + 2 * j);
                out += 32;

                nf = (cnt <= 0) ? 0 : (cnt + 31) / 32;
                /* Usage of the derived-but-unaccepted decode path. The frame
                 * count is incremented INSIDE the loop, after the bounds checks
                 * that can return early: counting nf*16 up front would have
                 * been a pre-loop PROJECTION of what the command intended, not
                 * a measurement of what it decoded. The unit is DECODE-LOOP
                 * ITERATIONS COMPLETED, 16 residuals each - not verified output
                 * writes, which is a further step this does not claim. */
                sl_acmd_adpcm_cmds++;
                for (f = 0; f < nf; f++) {
                    uint32_t hp = in + 9 * f, book;
                    int32_t r[16];
                    int hdr, pred, scale;

                    if (!dmem_ok(s, hp, 9) || !dmem_ok(s, out, 32))
                        return SL_ACMD_ERR_DMEM;
                    sl_acmd_adpcm_frames++;
                    sl_acmd_adpcm_samples += 16UL;
                    hdr   = s->dmem[hp];
                    pred  = hdr & 15;
                    scale = hdr >> 4;
                    book  = SL_ADPCM_BOOK + (uint32_t) pred * 32;
#if SL_ACMD_ADMUT
                    if (sl_ad_mut == 3) { sl_ad_elig++; sl_ad_applied++;
                        book = SL_ADPCM_BOOK + (uint32_t) ((pred + 1) & 15) * 32; }
#endif
                    if (book + 32 > SL_DMEM_SIZE) return SL_ACMD_ERR_DMEM;

                    for (i = 0; i < 16; i++) {
                        int b = s->dmem[hp + 1 + (i >> 1)];
                        int nib = (i & 1) ? (b & 15) : (b >> 4);
                        int32_t v;
#if SL_ACMD_ADMUT
                        if (sl_ad_mut == 1) { sl_ad_elig++; sl_ad_applied++;
                            nib = (i & 1) ? (b >> 4) : (b & 15); }
#endif
                        v = (int16_t) (uint16_t) (nib << 12);
                        /* blob 0x554 blez t6 with t6 = 12 - scale: at scale 12
                         * and above the scale multiply is SKIPPED, leaving the
                         * residual at <<12 rather than <<scale. Reproduced, not
                         * corrected - it is the hardware's behaviour. */
#if SL_ACMD_ADMUT
                        /* eligibility is the population where the quirk FIRES */
                        if (sl_ad_mut == 2) { if (12 - scale <= 0) {
                            sl_ad_elig++; sl_ad_applied++;
                            v = (v * (int32_t) (0x8000 >> 11)) >> 16; } }
#endif
                        if (12 - scale > 0)
                            v = (v * (int32_t) (0x8000 >> (11 - scale))) >> 16;
                        r[i] = v;
                    }

                    /* Two 8-lane halves; the second takes its history from the
                     * first (blob 0x614/0x61c read v28 elements 6 and 7). */
                    for (h = 0; h < 2; h++) {
                        int16_t res[8];
                        int32_t p2 = hist[6], p1 = hist[7];
#if SL_ACMD_ADMUT
                        if (sl_ad_mut == 4) { sl_ad_elig++; sl_ad_applied++;
                            p2 = hist[7]; p1 = hist[6]; }
#endif
                        for (j = 0; j < 8; j++) {
                            int64_t acc;
                            int32_t w;
                            acc  = (int64_t) dget(s, book + 2 * j) * p2;
                            acc += (int64_t) dget(s, book + 16 + 2 * j) * p1;
                            for (i = 0; i < j; i++)
                                acc += (int64_t)
                                    dget(s, book + 16 + 2 * (j - 1 - i)) * r[8 * h + i];
#if SL_ACMD_ADMUT
                            if (sl_ad_mut == 6) { sl_ad_elig++; sl_ad_applied++;
                                acc += (int64_t) r[8 * h + j] * 0x400; }
                            else
#endif
                            acc += (int64_t) r[8 * h + j] * 0x800;
                            /* The 48-bit accumulator is read back as VSAR
                             * HIGH:MID, which is exactly the low 32 bits of the
                             * sum taken as signed - so wrap, then >>11. */
#if SL_ACMD_ADMUT
                            if (sl_ad_mut == 5) {
                                /* eligible ONLY where the wrap actually bites */
                                int64_t full = acc >> 11;
                                if (acc > (int64_t) 2147483647 ||
                                    acc < (int64_t) (-2147483647 - 1)) {
                                    sl_ad_elig++; sl_ad_applied++; }
                                w = (int32_t) (full > 2147483647 ? 2147483647
                                    : full < (-2147483647 - 1) ? (-2147483647 - 1)
                                    : full);
                            } else
#endif
                            { if (acc > (int64_t) 2147483647 ||
                                  acc < (int64_t) (-2147483647 - 1)) {
                                  sl_acmd_ungrounded_adwrap++;
                                  /* FAIL LOUD. A counter is not fail-loud: it
                                   * records that production executed a
                                   * semantic nobody has grounded, after the
                                   * fact. The corpus has no RSP vector ISA
                                   * material (gedocs 'VSAR accumulator
                                   * vector' is empty) and the tree has none
                                   * either, so the wrap cannot be derived -
                                   * and it is measured UNREACHABLE on the
                                   * whole 435-task capture, so refusing it by
                                   * default costs the grounded path nothing.
                                   * -DSL_ACMD_ADWRAP=1 permits it for
                                   * diagnostic runs. */
                                  if (!SL_ACMD_ADWRAP)
                                      return SL_ACMD_ERR_UNGROUNDED;
                              }
                              w = (int32_t) (uint32_t) (acc & 0xFFFFFFFFu);
                              w >>= 11; }
                            if (w >  32767) w =  32767;
                            if (w < -32768) w = -32768;
                            res[j] = (int16_t) w;
                        }
                        for (j = 0; j < 8; j++) {
#if SL_ACMD_RSMUT
                            /* DECODED-PCM marker: perturbs the decoder's output
                             * samples so the DRAM bytes DESCENDED from decoded
                             * PCM can be identified by measurement rather than
                             * by a taint model that would have to replicate
                             * every mover between here and the save. */
                            if (sl_rs_mut == 12) res[j] ^= 0x0040;
#endif
                            dset(s, out + 16 * h + 2 * j, res[j]);
                            hist[j] = res[j];
                        }
                    }
                    out += 32;
                }

                /* blob 0x668: 32 bytes from s3-32 back to the resolved w1. */
                p = phys(s, st, 32);
                if (!p) return SL_ACMD_ERR_DRAM;
                if (!dmem_ok(s, out - 32, 32)) return SL_ACMD_ERR_DMEM;
                WRLOG(st, 32, A_ADPCM);
                memcpy(p, s->dmem + out - 32, 32);
            }
#endif
            break;

        case A_SETVOL:
            /* abi.h aSetVolume(f,v,t,r). Handler blob 0x2a8..0x308: a PURE
             * STATE WRITER - no arithmetic, no DMA, no DMEM buffer access.
             * Five DISJOINT field groups, selected by two bits, every store a
             * halfword (`sh`), in the handler's own test order:
             *
             *   A_AUX          t8+0x1C = w0 low, t8+0x1E = w1 low
             *   A_VOL |A_LEFT  t8+0x06 = w0 low
             *   A_VOL |A_RIGHT t8+0x08 = w0 low
             *   A_RATE|A_LEFT  t8+0x10 = w0 low, +0x12 = w1 high, +0x14 = w1 low
             *   A_RATE|A_RIGHT t8+0x16 = w0 low, +0x18 = w1 high, +0x1A = w1 low
             *
             * A_RATE is 0x00 and A_RIGHT is 0x00, so **flags == 0 is a real
             * combination that selects the last group** - it is NOT "no flags"
             * and must never be treated as absent or as a skip. The capture
             * emits complete sets: exactly 206 of each of the five.
             *
             * The A_RATE|A_LEFT group occupies t8+0x10..0x13, which is the same
             * storage as SETLOOP's 32-bit store. That aliasing is real on
             * hardware and is reproduced here because the block is real DMEM.
             *
             * SIGNEDNESS IS NOT SETTLED. `sh` says nothing about it, so no sign
             * is imposed here; the consumer decides, and the consumer is
             * ENVMIXER, which is not yet derived. The emitter is asymmetric
             * (rate mantissas signed, rate low words unsigned, volumes and
             * targets signed), which is a prior for reading ENVMIXER - not a
             * licence to stamp a sign on the bytes now. */
            {
                uint32_t f  = (w0 >> 16) & 0xFF;
                uint32_t v0 = w0 & 0xFFFF;
                uint32_t hi = (w1 >> 16) & 0xFFFF, lw = w1 & 0xFFFF;
                if (f & A_AUX) {
                    sl_acmd_sset(s, 0x1C, v0);
                    sl_acmd_sset(s, 0x1E, lw);
                } else if (f & A_VOL) {
                    sl_acmd_sset(s, (f & A_LEFT_BIT) ? 0x06 : 0x08, v0);
                } else {
                    uint32_t o = (f & A_LEFT_BIT) ? 0x10 : 0x16;
                    sl_acmd_sset(s, o,     v0);
                    sl_acmd_sset(s, o + 2, hi);
                    sl_acmd_sset(s, o + 4, lw);
                }
            }
            break;

        case A_SPNOOP:
            break;

        default:
            return SL_ACMD_ERR_OPCODE;   /* voice opcodes land here, loudly */
        }
    }
    s->err_op = -1;
    return SL_ACMD_OK;
}
#endif /* __sgi */
