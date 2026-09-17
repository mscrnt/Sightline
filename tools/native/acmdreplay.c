/* Replay captured cartridge ACMD frames through the native interpreter.
 *
 * The acceptance harness for B-022 stages 1-3. tools/native/acmd_census.py
 * --capture writes, for each ZERO-VOICE audio frame, the command list plus the
 * audio DRAM window as it stood BEFORE and AFTER the real RSP executed that
 * list. This replays the same list through sl_acmd.c against the same input
 * DRAM and compares the result to what the cartridge actually produced.
 *
 * WHAT IS COMPARED, and why not the whole window: the captured "after" also
 * contains bytes the CPU and DMA wrote for reasons unrelated to the command
 * list, so a whole-window diff would report differences the interpreter never
 * caused. Only the bytes the list itself writes are compared - every SAVEBUFF
 * destination, sized by the SETBUFF in force at that point. That is the exact
 * set of bytes the RSP is responsible for, so it is both fair and complete.
 *
 * It also reports how much NONZERO data the window held, because a comparison
 * over an all-zero region is a weak test and saying so is part of the result.
 *
 * Build: see tools/native/acmdreplay.sh. Never linked into the game.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "../../src/platform/sl_acmd.h"

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static int nonzero(const uint8_t *p, uint32_t n) {
    uint32_t i, c = 0;
    for (i = 0; i < n; i++) if (p[i]) c++;
    return (int) c;
}

#if SL_ACMD_ADMUT
extern int sl_ad_mut;
extern unsigned long sl_ad_elig, sl_ad_applied;
#endif


/* ---- PROVENANCE RECORDER -------------------------------------------------
 * Same structure as the one proven in tools/native/acmdenvfull.c, kept
 * deliberately identical rather than reinvented: three DISTINCT things per
 * byte, which is the distinction this sprint has now conflated four times.
 *
 *   DW_*      the ACTUAL WRITER - which task/command/opcode physically stored
 *   DW_changed  whether that store CHANGED the value (a same-value write is
 *               still a write, and must not clear this or be hidden by it)
 *   OR_*      the SEMANTIC ORIGIN - transferred through movers, so a DMEMMOVE
 *             carries the source's origin while the actual writer stays the
 *             move itself
 *
 * Value-change is evaluated AFTER the physical store, so the log records the
 * pending write and settles it on the next event or an explicit flush.
 */
#if SL_ACMD_DWLOG
static int32_t  DW_task[SL_DMEM_SIZE], DW_cmd[SL_DMEM_SIZE];
static uint8_t  DW_op[SL_DMEM_SIZE], DW_changed[SL_DMEM_SIZE];
static int32_t  OR_task[SL_DMEM_SIZE], OR_cmd[SL_DMEM_SIZE];
static uint8_t  OR_op[SL_DMEM_SIZE], OR_kind[SL_DMEM_SIZE];
static uint32_t OR_src[SL_DMEM_SIZE];
static long long EV_writes, EV_samevalue;
static int32_t  TRK_task = -1, TRK_cmd = -1;
/* TRK_base + the interpreter's own within-call index gives the SAME absolute
 * command ordinal in both replay modes: whole-task sets base 0 and the
 * interpreter counts; per-command sets base k and the interpreter reports 0. */
static int32_t  TRK_base = 0;
extern int sl_acmd_cur_index;
/* THE CHANGED FLAG IS 'FINAL-WRITE-CHANGED', chosen explicitly. Ownership
 * transfer CLEARS it and the settling pass sets it only if THAT write altered
 * the byte. The alternative - ever-changed-since-reset - cannot be associated
 * with the final writer at all, which is what an evaluator needs. A
 * deliberately sticky implementation is kept as a control and must FAIL the
 * changing-then-same-value case. */
static int STICKY_MUTANT;
static int      TRK_on;
static sl_acmd_state *TRK_S;
static uint32_t PD_off, PD_len; static uint8_t PD_prev[4096]; static int PD_live;

static void trk_flush(void)
{
    uint32_t q;
    if (!PD_live || !TRK_S) { PD_live = 0; return; }
    for (q = 0; q < PD_len; q++) {
        uint32_t ad = PD_off + q;
        if (TRK_S->dmem[ad] != PD_prev[q]) DW_changed[ad] = 1;
        else EV_samevalue++;
    }
    PD_live = 0;
}

void sl_acmd_dwlog(uint32_t off, uint32_t len, int op, int kind, uint32_t src)
{
    uint32_t q; int32_t curcmd;
    static uint32_t sv_t[4096], sv_c[4096], sv_o[4096], sv_k[4096], sv_s[4096];
    if (!TRK_on || !TRK_S) return;
    if (off + len > SL_DMEM_SIZE || len > 4096) return;
    trk_flush();
    EV_writes++;
    if (kind == 1) for (q = 0; q < len; q++) {      /* snapshot before copying */
        uint32_t sa = src + q;
        if (sa < SL_DMEM_SIZE) { sv_t[q] = (uint32_t) OR_task[sa]; sv_c[q] = (uint32_t) OR_cmd[sa];
            sv_o[q] = OR_op[sa]; sv_k[q] = OR_kind[sa]; sv_s[q] = OR_src[sa]; }
        else { sv_t[q] = sv_c[q] = 0xFFFFFFFFu; sv_o[q] = 0xFF; sv_k[q] = 0; sv_s[q] = 0; }
    }
    /* ONE definition of "which command is writing", used by BOTH the actual-
     * writer and the origin fields. Deriving them differently is exactly what
     * the equivalence gate caught: DW_cmd used this expression while OR_cmd
     * still used TRK_cmd, and the two replay modes disagreed on all 435 tasks. */
    curcmd = TRK_base + (sl_acmd_cur_index >= 0 ? sl_acmd_cur_index : 0);
    for (q = 0; q < len; q++) {
        uint32_t ad = off + q;
        DW_task[ad] = TRK_task;
        DW_cmd[ad] = curcmd;
        DW_op[ad] = (uint8_t) op;
        if (!STICKY_MUTANT) DW_changed[ad] = 0;   /* ownership transfer */
        if (kind == 1) { OR_task[ad] = (int32_t) sv_t[q]; OR_cmd[ad] = (int32_t) sv_c[q];
                         OR_op[ad] = (uint8_t) sv_o[q]; OR_kind[ad] = (uint8_t) sv_k[q];
                         OR_src[ad] = sv_s[q]; }
        else { OR_task[ad] = TRK_task; OR_cmd[ad] = curcmd; OR_op[ad] = (uint8_t) op;
               OR_kind[ad] = (uint8_t) kind; OR_src[ad] = (kind == 2) ? (src + q) : src; }
    }
    PD_off = off; PD_len = len; PD_live = 1;
    for (q = 0; q < len; q++) PD_prev[q] = TRK_S->dmem[off + q];
}

/* THE VALIDATOR'S OWN CONTROLS. This class of bug has recurred four times, so
 * the recorder is challenged before any number it produces is believed. Each
 * property is one the last-writer measurement depends on. */
static int trk_selftest(void)
{
    static sl_acmd_state st;
    int fails = 0;
    uint32_t A = 0x100, B = 0x200;

    memset(&st, 0, sizeof st);
    memset(DW_task, 0, sizeof DW_task); memset(DW_cmd, 0, sizeof DW_cmd);
    memset(DW_op, 0, sizeof DW_op);     memset(DW_changed, 0, sizeof DW_changed);
    memset(OR_cmd, 0, sizeof OR_cmd);   memset(OR_op, 0, sizeof OR_op);
    EV_writes = EV_samevalue = 0;
    TRK_S = &st; TRK_on = 1; TRK_task = 7;

    /* (1) a write that CHANGES the value */
    TRK_cmd = TRK_base = 1; sl_acmd_dwlog(A, 2, 3, 0, 0);
    st.dmem[A] = 0xAA; st.dmem[A+1] = 0xBB; trk_flush();
    if (DW_cmd[A] != 1)      { printf("  selftest: first writer not recorded\n"); fails++; }
    if (!DW_changed[A])      { printf("  selftest: value change not recorded\n"); fails++; }

    /* (2) a SAME-VALUE write from a different command: writer identity must
     *     move, value-change must NOT be set by it */
    DW_changed[A] = DW_changed[A+1] = 0;
    { long long sv = EV_samevalue;
      TRK_cmd = TRK_base = 2; sl_acmd_dwlog(A, 2, 5, 0, 0);
      st.dmem[A] = 0xAA; st.dmem[A+1] = 0xBB; trk_flush();
      if (DW_cmd[A] != 2)   { printf("  selftest: same-value write did not update writer\n"); fails++; }
      if (DW_changed[A])    { printf("  selftest: same-value write wrongly flagged changed\n"); fails++; }
      if (EV_samevalue != sv + 2) { printf("  selftest: same-value not counted\n"); fails++; } }

    /* (2b) THE CASE THE OLD SELF-TEST COULD NOT FAIL: a CHANGING writer
     *      followed by a SAME-VALUE writer. The old version cleared the flag by
     *      hand before its same-value arm, so a sticky implementation passed.
     *      Here nothing is cleared by hand - the recorder must clear on
     *      ownership transfer itself. */
    { TRK_cmd = TRK_base = 20; sl_acmd_dwlog(A, 2, 3, 0, 0);
      st.dmem[A] = 0x77; trk_flush();               /* A changes the value */
      if (!DW_changed[A]) { printf("  selftest: setup write not flagged changed\n"); fails++; }
      TRK_cmd = TRK_base = 21; sl_acmd_dwlog(A, 2, 5, 0, 0);
      st.dmem[A] = 0x77; trk_flush();               /* B writes the SAME value */
      if (DW_changed[A]) {
          printf("  selftest: STICKY - changed survived a same-value final write\n");
          fails++; } }

    /* (3) an intervening writer becomes the final writer over an earlier one */
    TRK_cmd = TRK_base = 3; sl_acmd_dwlog(A, 2, 6, 0, 0);
    st.dmem[A] = 0x11; trk_flush();
    if (DW_cmd[A] != 3)      { printf("  selftest: later writer did not supersede\n"); fails++; }

    /* (4) a MOVER transfers origin from the source while the actual writer
     *     stays the moving command */
    TRK_cmd = TRK_base = 4; sl_acmd_dwlog(B, 2, 3, 0, 0);      /* origin planted at B by cmd 4 */
    st.dmem[B] = 0x55; trk_flush();
    TRK_cmd = TRK_base = 5; sl_acmd_dwlog(A, 2, 10, 1, B);     /* move B -> A by cmd 5 */
    st.dmem[A] = 0x55; trk_flush();
    if (DW_cmd[A] != 5)      { printf("  selftest: mover is not the actual writer\n"); fails++; }
    if (OR_cmd[A] != 4)      { printf("  selftest: mover did not carry source origin\n"); fails++; }

    TRK_on = 0; TRK_S = 0;
    printf("provenance recorder selftest: %s (%d failure%s)\n",
           fails ? "FAILED" : "passed", fails, fails == 1 ? "" : "s");
    return fails;
}
#endif

#if SL_ACMD_DWLOG
/* LAST-WRITER MEASUREMENT for the resampler-init consumption of DMEM
 * 0xF9C..0xF9F. Nothing in the RESAMPLE handler stores that range, so the
 * recorder's actual-writer entry immediately AFTER the command is exactly the
 * final physical writer BEFORE the consumption - no extra hook is needed.
 * Run as a separate pass into its own state so the scored run is untouched. */
#define SCR_LO 0xF9Cu
static long long LW_mixer_b, LW_other_b, LW_none_b, LW_total, LW_mixedgroups;
static long long LW_mixer_bad, LW_other_bad, LW_none_bad;
static long long LW_bad_same, LW_bad_changed;
static unsigned char LW_opseen[16];
static uint8_t LW_dram[1 << 20];

static void lastwriter_pass(const uint32_t *words, uint32_t n, uint32_t af,
                            const uint8_t *before, const uint8_t *after,
                            const uint8_t *work, uint32_t dram_lo, uint32_t dsz)
{
    static sl_acmd_state d;
    uint32_t k;
    memset(&d, 0, sizeof d);
    memcpy(LW_dram, before, dsz);
    sl_acmd_init(&d, LW_dram, dram_lo, dsz);
    memset(DW_cmd, 0xFF, sizeof DW_cmd);
    memset(DW_op,  0xFF, sizeof DW_op);
    TRK_S = &d; TRK_on = 1; TRK_task = (int32_t) af;

    for (k = 0; k < n; k++) {
        uint32_t w0 = words[2*k], w1 = words[2*k+1];
        int op = (int) ((w0 >> 24) & 0xFF);
        int isinit = (int) ((w0 >> 16) & 1);
        uint32_t a = w1 & 0xFFFFFF;
        TRK_cmd = (int32_t) k;
        if (sl_acmd_exec(&d, words + 2*k, 1) != SL_ACMD_OK) break;
        trk_flush();
        if (op == 5 && isinit && a >= dram_lo && a + 32 <= dram_lo + dsz) {
            /* PER BYTE. One representative byte cannot establish identity for
             * the other three: the differing population is byte-granular, and
             * four separate scratch bytes can have four separate last writers.
             * Classification therefore happens INSIDE this loop. */
            int q, first_op = -2, mixed = 0;
            LW_total++;
            for (q = 0; q < 4; q++) {
                uint32_t sa  = SCR_LO + (uint32_t) q;      /* scratch byte read */
                uint32_t idx = a - dram_lo + 12 + q;       /* DRAM byte written */
                int lw_op   = DW_op[sa];
                int differs = (work[idx] != after[idx]);
                int samev   = differs && (after[idx] == before[idx]);
                if (first_op == -2) first_op = lw_op;
                else if (lw_op != first_op) mixed = 1;
                if (lw_op == 0xFF)   { LW_none_b++;  if (differs) LW_none_bad++; }
                else if (lw_op == 3) { LW_mixer_b++; if (differs) LW_mixer_bad++; }
                else { LW_other_b++; if (differs) LW_other_bad++;
                       if (lw_op >= 0 && lw_op < 16) LW_opseen[lw_op] = 1; }
                if (differs) { if (samev) LW_bad_same++; else LW_bad_changed++; }
            }
            if (mixed) LW_mixedgroups++;
        }
    }
    TRK_on = 0; TRK_S = 0;
}
#endif

#if SL_ACMD_DWLOG
/* EQUIVALENCE GATE. The provenance pass executes ONE command per call; the
 * scored evaluator executes a whole task list per call. Those are not
 * lifecycle-equivalent by assumption - if sl_acmd_exec carries anything across
 * commands within a call, or resets anything per call, the provenance pass is
 * sampling a different execution and every number it produces is void.
 *
 * So both modes are run from identical initial state over every task and
 * required to agree on: success/failure, final DRAM, final DMEM, and - on the
 * recorder's own output - writer OPCODE and value-changed per byte. (Writer
 * COMMAND cannot be compared: in whole-task mode the harness cannot set it per
 * command, which is precisely why the per-command mode exists.) */
static long long EQ_tasks, EQ_rc, EQ_dram, EQ_dmem, EQ_op, EQ_chg;
static uint8_t EQ_dA[1 << 20], EQ_dB[1 << 20];
static uint8_t EQ_opA[SL_DMEM_SIZE], EQ_chgA[SL_DMEM_SIZE];
static int32_t EQ_taskA[SL_DMEM_SIZE], EQ_cmdA[SL_DMEM_SIZE];
static int32_t EQ_orTA[SL_DMEM_SIZE], EQ_orCA[SL_DMEM_SIZE];
static uint8_t EQ_orOA[SL_DMEM_SIZE], EQ_orKA[SL_DMEM_SIZE];
static uint32_t EQ_orSA[SL_DMEM_SIZE];
static long long EQ_task, EQ_cmd, EQ_orT, EQ_orC, EQ_orO, EQ_orK, EQ_orS;

static void equiv_pass(const uint32_t *words, uint32_t n, const uint8_t *before,
                       uint32_t dram_lo, uint32_t dsz)
{
    static sl_acmd_state a, b;
    int rcA, rcB = SL_ACMD_OK; uint32_t k;

    memset(&a, 0, sizeof a); memcpy(EQ_dA, before, dsz);
    sl_acmd_init(&a, EQ_dA, dram_lo, dsz);
    memset(DW_op, 0xFF, sizeof DW_op); memset(DW_changed, 0, sizeof DW_changed);
    memset(DW_task, 0xFF, sizeof DW_task); memset(DW_cmd, 0xFF, sizeof DW_cmd);
    memset(OR_task, 0xFF, sizeof OR_task); memset(OR_cmd, 0xFF, sizeof OR_cmd);
    memset(OR_op, 0xFF, sizeof OR_op); memset(OR_kind, 0xFF, sizeof OR_kind);
    memset(OR_src, 0xFF, sizeof OR_src);
    TRK_S = &a; TRK_on = 1; TRK_task = -1; TRK_cmd = -1; TRK_base = 0;
    rcA = sl_acmd_exec(&a, words, n);
    trk_flush();
    memcpy(EQ_opA, DW_op, sizeof EQ_opA);   memcpy(EQ_chgA, DW_changed, sizeof EQ_chgA);
    memcpy(EQ_taskA, DW_task, sizeof EQ_taskA); memcpy(EQ_cmdA, DW_cmd, sizeof EQ_cmdA);
    memcpy(EQ_orTA, OR_task, sizeof EQ_orTA); memcpy(EQ_orCA, OR_cmd, sizeof EQ_orCA);
    memcpy(EQ_orOA, OR_op, sizeof EQ_orOA);   memcpy(EQ_orKA, OR_kind, sizeof EQ_orKA);
    memcpy(EQ_orSA, OR_src, sizeof EQ_orSA);

    memset(&b, 0, sizeof b); memcpy(EQ_dB, before, dsz);
    sl_acmd_init(&b, EQ_dB, dram_lo, dsz);
    memset(DW_op, 0xFF, sizeof DW_op); memset(DW_changed, 0, sizeof DW_changed);
    memset(DW_task, 0xFF, sizeof DW_task); memset(DW_cmd, 0xFF, sizeof DW_cmd);
    memset(OR_task, 0xFF, sizeof OR_task); memset(OR_cmd, 0xFF, sizeof OR_cmd);
    memset(OR_op, 0xFF, sizeof OR_op); memset(OR_kind, 0xFF, sizeof OR_kind);
    memset(OR_src, 0xFF, sizeof OR_src);
    TRK_S = &b; TRK_on = 1;
    for (k = 0; k < n; k++) {
        int r; TRK_cmd = (int32_t) k; TRK_base = (int32_t) k;
        r = sl_acmd_exec(&b, words + 2*k, 1);
        if (r != SL_ACMD_OK) { rcB = r; break; }
    }
    trk_flush();
    TRK_on = 0; TRK_S = 0;

    EQ_tasks++;
    if ((rcA == SL_ACMD_OK) != (rcB == SL_ACMD_OK)) EQ_rc++;
    if (memcmp(EQ_dA, EQ_dB, dsz) != 0)             EQ_dram++;
    if (memcmp(a.dmem, b.dmem, SL_DMEM_SIZE) != 0)  EQ_dmem++;
    if (memcmp(EQ_opA,  DW_op,      SL_DMEM_SIZE) != 0) EQ_op++;
    if (memcmp(EQ_chgA, DW_changed, SL_DMEM_SIZE) != 0) EQ_chg++;
    if (memcmp(EQ_taskA, DW_task, sizeof EQ_taskA) != 0) EQ_task++;
    if (memcmp(EQ_cmdA,  DW_cmd,  sizeof EQ_cmdA)  != 0) EQ_cmd++;
    if (memcmp(EQ_orTA, OR_task, sizeof EQ_orTA) != 0) EQ_orT++;
    if (memcmp(EQ_orCA, OR_cmd,  sizeof EQ_orCA) != 0) EQ_orC++;
    if (memcmp(EQ_orOA, OR_op,   sizeof EQ_orOA) != 0) EQ_orO++;
    if (memcmp(EQ_orKA, OR_kind, sizeof EQ_orKA) != 0) EQ_orK++;
    if (memcmp(EQ_orSA, OR_src,  sizeof EQ_orSA) != 0) EQ_orS++;
}
#endif

int main(int argc, char **argv) {
#if SL_ACMD_DWLOG
    /* The recorder gates everything downstream of it: a provenance number
     * from an unchallenged logger is worth nothing. */
    if (trk_selftest()) { printf("REFUSING to score: recorder failed its own controls\n"); return 3; }
    /* NON-VACUITY: the sticky implementation must FAIL the controls. A
     * self-test that passes under both semantics is testing nothing. */
    { int mf; STICKY_MUTANT = 1; printf("  [control] sticky mutant: "); mf = trk_selftest();
      STICKY_MUTANT = 0;
      if (!mf) { printf("REFUSING to score: sticky mutant PASSED the controls, "
                        "so they do not test the flag semantic\n"); return 3; } }
#endif
    const char *path = argc > 1 ? argv[1] : "/tmp/sl-acmd3/zerovoice.bin";
    int want_frame = argc > 2 ? atoi(argv[2]) : -1;
    FILE *f = fopen(path, "rb");
    uint8_t hdr[20];
    uint32_t nframes, dram_lo, dsz, fi;
    uint8_t *before, *after, *work, *mask, *smask, *soff, *sop, *sini, *senv, *sorig;
    int seen_env = 0;
    long long cd_init_env = 0, cd_init_noenv = 0, cd_cont = 0;
    long long cd_preserved = 0, cd_rewritten = 0;
    /* Localise the state residue: which offset inside the 32-byte block,
     * and which opcode owns that block. RESAMPLE and ADPCM keep separate
     * columns because they are different state layouts. */
    long long srng[2][5]; long long snrng[2][5];
    long long sbytes = 0, sbytes_nz = 0, sdiff = 0, soverlap = 0; int sframes = 0;
    uint32_t *words;
    int total_ok = 0, total_bad = 0, first_bad_frame = -1;
    long long cmp_bytes = 0, cmp_nonzero = 0, diff_bytes = 0;
    /* Sample-domain error over the nonzero written samples: a byte count
     * says how much differs, these say by how much. */
    long long err_n = 0; double err_sq = 0.0; int err_max = 0;
    /* BRACKET VALIDITY. See the guard printed at the end. */
    long long brk_frames = 0, brk_bytes = 0;

    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }
#if SL_ACMD_ADMUT
    /* Select an ADPCM negative control. Eligibility and application are
     * reported so a control the capture never exercises is visible as such
     * rather than passing silently. */
    { const char *e = getenv("AD_MUT"); if (e) sl_ad_mut = atoi(e); }
#endif
    if (fread(hdr, 1, 20, f) != 20 || memcmp(hdr, "SLACMD01", 8)) {
        fprintf(stderr, "bad capture magic\n"); return 2;
    }
    nframes = be32(hdr + 8);
    dram_lo = be32(hdr + 12);
    dsz     = be32(hdr + 16);
    printf("capture: %u frames, DRAM window 0x%06x..0x%06x (%u bytes)\n",
           nframes, dram_lo, dram_lo + dsz, dsz);

    before = malloc(dsz); after = malloc(dsz);
    work   = malloc(dsz); mask  = malloc(dsz); smask = malloc(dsz);
    soff   = malloc(dsz); sop   = malloc(dsz);
    sini   = malloc(dsz); senv  = malloc(dsz); sorig = malloc(dsz);
    memset(srng, 0, sizeof srng); memset(snrng, 0, sizeof snrng);
    words  = malloc(4096 * 8);
    if (!before || !after || !work || !mask || !smask || !soff || !sop
        || !sini || !senv || !sorig || !words) return 2;

    for (fi = 0; fi < nframes; fi++) {
        uint8_t fh[12];
        uint32_t af, vf, n, k;
        sl_acmd_state st;
        int rc;
        uint32_t setin = 0, setout = 0, setcnt = 0;
        long long fdiff = 0, fcmp = 0, fnz = 0;

        if (fread(fh, 1, 12, f) != 12) break;
        af = be32(fh); vf = be32(fh + 4); n = be32(fh + 8);
        if (n > 4096) { fprintf(stderr, "frame %u: n=%u too large\n", af, n); break; }
        for (k = 0; k < n; k++) {
            uint8_t cw[8];
            if (fread(cw, 1, 8, f) != 8) { fprintf(stderr, "short read\n"); return 2; }
            words[2 * k]     = be32(cw);
            words[2 * k + 1] = be32(cw + 4);
        }
        if (fread(before, 1, dsz, f) != dsz) { fprintf(stderr, "short before\n"); return 2; }
        if (fread(after,  1, dsz, f) != dsz) { fprintf(stderr, "short after\n"); return 2; }

        /* Build the mask of bytes this list writes: every SAVEBUFF, sized by
         * the SETBUFF count in force when it executes. Decoding is repeated
         * here deliberately rather than reaching into the interpreter - the
         * harness must be able to disagree with it. */
        memset(mask, 0, dsz);
        for (k = 0; k < n; k++) {
            uint32_t w0 = words[2 * k], w1 = words[2 * k + 1];
            int op = (int)((w0 >> 24) & 0xFF);
            if (op == 8) {                      /* SETBUFF */
                setin  = w0 & 0xFFFF;
                setout = (w1 >> 16) & 0xFFFF;
                setcnt = w1 & 0xFFFF;
                (void) setin; (void) setout;
            } else if (op == 6) {               /* SAVEBUFF */
                if (w1 >= dram_lo && w1 + setcnt <= dram_lo + dsz)
                    memset(mask + (w1 - dram_lo), 1, setcnt);
            }
        }

        /* SECOND MASK: the STATE buffers, which the SAVEBUFF mask above does
         * NOT cover. That omission is exactly why this harness and the
         * RESAMPLE full-state oracle looked like they contradicted each other:
         * one scores the PCM written to DRAM, the other the 32-byte state
         * blocks, and "0 bytes differing" from the first says nothing at all
         * about the second. Scored and reported separately, never merged.
         *
         * Soundness: the whole task is executed before comparing, so a byte
         * written many times is still comparable against the end-of-task
         * snapshot. Last-writer identity matters for ATTRIBUTING a mismatch to
         * a command, not for detecting one. */
        memset(smask, 0, dsz);
        memset(sorig, 0, dsz);
        seen_env = 0;
        for (k = 0; k < n; k++) {
            uint32_t w0 = words[2 * k], w1 = words[2 * k + 1];
            int op = (int)((w0 >> 24) & 0xFF);
            if (op == 3) seen_env = 1;          /* ENVMIXER seen this task */
            if (op == 5 || op == 1) {           /* RESAMPLE, ADPCM */
                uint32_t a = w1 & 0xFFFFFF;
                if (a >= dram_lo && a + 32 <= dram_lo + dsz) {
                    uint32_t q;
                    for (q = 0; q < 32; q++) {
                        smask[a - dram_lo + q] = 1;
                        soff[a - dram_lo + q] = (uint8_t) q;
                        sop [a - dram_lo + q] = (uint8_t) (op == 5 ? 0 : 1);
                        /* the LAST writer wins, which is what end-of-task
                         * comparison sees */
                        sini[a - dram_lo + q] = (uint8_t) ((w0 >> 16) & 1);
                        /* ORIGIN, not last writer. +0x0C..0F is only WRITTEN by
                         * an A_INIT; a CONTINUE preserves it. So attributing a
                         * mismatch there to the last RESAMPLE names a command
                         * that did not produce the value. Track the last A_INIT
                         * separately - that is where the bytes come from - and
                         * whether an ENVMIXER preceded THAT one. */
                        if (op == 5 && ((w0 >> 16) & 1)) {
                            sorig[a - dram_lo + q] = 1;
                            senv [a - dram_lo + q] = (uint8_t) seen_env;
                        }
                    }
                }
            }
        }

        {   /* BRACKET VALIDITY, counted here right after the mask is built.
             * An earlier version of this block never landed - the counters were
             * declared and printed but never incremented, so the guard reported
             * "0 changed" on a capture whose bracket was in fact correct, and
             * that false verdict was acted on. Keep the increment adjacent to
             * the mask so the two cannot drift apart again. */
            long long bd = 0;
            for (k = 0; k < dsz; k++)
                if (mask[k] && before[k] != after[k]) bd++;
            if (bd) { brk_frames++; brk_bytes += bd; }
        }

        memcpy(work, before, dsz);
        sl_acmd_init(&st, work, dram_lo, dsz);

        /* Diagnostic mode. Attributing a differing byte to a command is
         * ONLY valid for the LAST SAVEBUFF that writes it: the captured
         * "after" is the end-of-frame state, and these buffers are rewritten
         * many times per frame (0x2d7a70 is written six times in af=226). An
         * earlier version of this walk compared every SAVEBUFF against the
         * final state and reported the first one as a mismatch, which was an
         * artifact of the diagnostic and not a fact about the interpreter.
         * So: run the frame, then bucket each differing byte by the last
         * command that wrote it. */
        if (want_frame >= 0 && af == (uint32_t) want_frame) {
            static int32_t lastw[1 << 20];
            static int32_t lastp[1 << 20];
            uint32_t si = 0, so = 0, sc = 0, kk, q;
            int last_polef = -1;
            for (q = 0; q < dsz; q++) { lastw[q] = -1; lastp[q] = -1; }
            for (kk = 0; kk < n; kk++) {
                uint32_t w0 = words[2*kk], w1 = words[2*kk+1];
                int op = (int)((w0 >> 24) & 0xFF);
                if (op == 8) { si = w0 & 0xFFFF; so = (w1>>16)&0xFFFF; sc = w1 & 0xFFFF; (void)si; (void)so; }
                if (op == 14) last_polef = (int) kk;
                if (op == 6 && w1 >= dram_lo && w1 + sc <= dram_lo + dsz)
                    for (q = 0; q < sc; q++) {
                        lastw[w1 - dram_lo + q] = (int32_t) kk;
                        lastp[w1 - dram_lo + q] = last_polef;
                    }
            }
            memcpy(work, before, dsz);
            sl_acmd_init(&st, work, dram_lo, dsz);
            if (sl_acmd_exec(&st, words, n) == SL_ACMD_OK) {
                {   /* One pass, bucketed by last-writer index (n <= 4096),
                     * so pre-POLEF and post-POLEF writers can be compared
                     * directly. This is the isolation test: if every writer
                     * with no POLEF upstream is EXACT and every writer with
                     * one differs, POLEF is the sole remaining error. */
                    static uint32_t btot[4100], bdif[4100];
                    static int bpol[4100];
                    uint32_t i3;
                    uint32_t pre_t = 0, pre_d = 0, post_t = 0, post_d = 0;
                    int pre_w = 0, pre_bad = 0, post_w = 0, post_bad = 0;
                    for (i3 = 0; i3 <= n && i3 < 4100; i3++) {
                        btot[i3] = bdif[i3] = 0; bpol[i3] = -1;
                    }
                    for (q = 0; q < dsz; q++) {
                        int32_t w = lastw[q];
                        if (!mask[q] || w < 0 || w >= 4100) continue;
                        btot[w]++;
                        bpol[w] = lastp[q];
                        if (work[q] != after[q]) bdif[w]++;
                    }
                    printf("--- writer isolation, af=%u ---\n", af);
                    for (i3 = 0; i3 < 4100; i3++) {
                        if (!btot[i3]) continue;
                        if (bpol[i3] < 0) {
                            pre_w++; pre_t += btot[i3]; pre_d += bdif[i3];
                            if (bdif[i3]) pre_bad++;
                        } else {
                            post_w++; post_t += btot[i3]; post_d += bdif[i3];
                            if (bdif[i3]) post_bad++;
                        }
                    }
                    printf("  writers with NO POLEF upstream : %d  bytes=%u  diff=%u  (writers differing: %d)\n",
                           pre_w, pre_t, pre_d, pre_bad);
                    printf("  writers WITH POLEF upstream    : %d  bytes=%u  diff=%u  (writers differing: %d)\n",
                           post_w, post_t, post_d, post_bad);
                }
                printf("--- end ---\n");
            }
            memcpy(work, before, dsz);
            sl_acmd_init(&st, work, dram_lo, dsz);
        }

        rc = sl_acmd_exec(&st, words, n);
        if (rc != SL_ACMD_OK) {
            printf("af=%u vf=%u n=%u  EXEC FAILED at cmd %u (%s): %s\n",
                   af, vf, n, st.err_index, sl_acmd_opname(st.err_op),
                   sl_acmd_errstr(rc));
            total_bad++;
            if (first_bad_frame < 0) first_bad_frame = (int) af;
            continue;
        }

        /* s16 sample error, big-endian pairs, only where the cartridge wrote
         * something nonzero. */
        for (k = 0; k + 1 < dsz; k += 2) {
            int a1, b1, d1;
            if (!mask[k] || !mask[k + 1]) continue;
            a1 = (int16_t)((work[k] << 8) | work[k + 1]);
            b1 = (int16_t)((after[k] << 8) | after[k + 1]);
            if (!b1) continue;
            d1 = a1 - b1; if (d1 < 0) d1 = -d1;
            err_n++; err_sq += (double) d1 * d1;
            if (d1 > err_max) err_max = d1;
        }
#if SL_ACMD_DWLOG
        equiv_pass(words, n, before, dram_lo, dsz);
        lastwriter_pass(words, n, af, before, after, work, dram_lo, dsz);
#endif
        {   /* score the state blocks, on their own mask and own counters */
            long long fs = 0;
            for (k = 0; k < dsz; k++) {
                if (!smask[k]) continue;
                sbytes++;
                if (after[k]) sbytes_nz++;
                {   int o = soff[k], col = sop[k], r;
                    r = (o < 8) ? 0 : (o < 10) ? 1 : (o < 12) ? 2 : (o < 16) ? 3 : 4;
                    snrng[col][r]++;
                    if (work[k] != after[k]) { sdiff++; fs++; srng[col][r]++;
                        if (col == 0 && r == 3) {      /* RESAMPLE +0x0C..0F */
                            if (!sorig[k])     cd_cont++;        /* no A_INIT wrote it in this task */
                            else if (senv[k])  cd_init_env++;
                            else               cd_init_noenv++;
                            /* Did the CARTRIDGE simply preserve the byte where
                             * we wrote the scratch? If after == before, the
                             * hardware left it alone and the A_INIT write is
                             * the thing that should not have happened. */
                            if (after[k] == before[k]) cd_preserved++;
                            else                       cd_rewritten++; } } }
            }
            if (fs) sframes++;
            /* The two masks are claimed disjoint; claims get measured. */
            for (k = 0; k < dsz; k++) if (smask[k] && mask[k]) soverlap++;
        }
        for (k = 0; k < dsz; k++) {
            if (!mask[k]) continue;
            fcmp++;
            if (after[k]) { cmp_nonzero++; fnz++; }
            if (work[k] != after[k]) {
                if (fdiff == 0 && first_bad_frame < 0) {
                    printf("af=%u vf=%u: FIRST DIFF at phys 0x%06x  "
                           "native=0x%02x cartridge=0x%02x\n",
                           af, vf, dram_lo + k, work[k], after[k]);
                }
                fdiff++;
            }
        }
        cmp_bytes += fcmp;
        diff_bytes += fdiff;
        if (fdiff) {
            total_bad++;
            if (first_bad_frame < 0) first_bad_frame = (int) af;
        } else {
            total_ok++;
        }
        if (fnz > 0 || fdiff)
            printf("af=%-5u vf=%-5u n=%-5u compared=%-7lld "
                   "nonzero_compared=%-7lld diff=%lld%s\n",
                   af, vf, n, fcmp, fnz, fdiff,
                   fdiff ? "   <== MISMATCH" : "   exact");
        (void) nonzero;
    }
    fclose(f);

    printf("\n=== BRACKET VALIDITY ===\n");
    printf("frames whose targeted bytes differ between before and after: %lld\n", brk_frames);
    printf("  bytes so differing                                       : %lld\n", brk_bytes);
    if (!brk_frames) {
        printf("\nBRACKET INVALID - REFUSING TO SCORE.\n"
               "  In every frame the bytes this command list writes are IDENTICAL\n"
               "  in the captured before and after. The pair therefore does not\n"
               "  contain the RSP's output for that list, so any exact/differing\n"
               "  count below would be measured against the wrong ground truth.\n"
               "  A null interpreter that writes nothing scores perfectly here.\n"
               "  Fix the capture so the snapshot follows RSP completion.\n");
        return 4;
    }
#if SL_ACMD_ADMUT
    printf("\nADPCM CHALLENGE  [sl_ad_mut = %d]  eligible=%lu applied=%lu\n",
           sl_ad_mut, sl_ad_elig, sl_ad_applied);
#endif
    { extern unsigned long sl_acmd_ungrounded_adwrap;
      printf("\nADPCM accumulator-wrap branch taken: %lu\n",
             sl_acmd_ungrounded_adwrap); }
    printf("\n=== STATE BUFFERS (RESAMPLE/ADPCM 32-byte blocks) ===\n");
    printf("DISJOINT from the SAVEBUFF mask below, scored separately: a\n"
           "\"0 bytes differing\" PCM result says nothing about these bytes.\n");
    printf("state bytes compared          : %lld\n", sbytes);
    printf("  of which cartridge-nonzero  : %lld\n", sbytes_nz);
    printf("state bytes differing         : %lld\n", sdiff);
    printf("frames with a state difference: %d\n", sframes);
    printf("bytes in BOTH masks (must be 0): %lld\n", soverlap);
    {   static const char *RN[5] = { "+0x00..07", "+0x08..09", "+0x0A..0B",
                                     "+0x0C..0F", "+0x10..1F" };
        int r;
        printf("  by offset in the 32-byte block   RESAMPLE          ADPCM\n");
        for (r = 0; r < 5; r++)
            printf("    %-10s   %8lld / %-8lld   %8lld / %-8lld\n", RN[r],
                   srng[0][r], snrng[0][r], srng[1][r], snrng[1][r]);
        printf("    (differing / compared)\n");
        printf("  RESAMPLE +0x0C..0F mismatches by last writer:\n");
        printf("    no A_INIT wrote it this task (carried) %lld\n", cd_cont);
        printf("    A_INIT with an ENVMIXER earlier       %lld\n", cd_init_env);
        printf("    A_INIT with NO ENVMIXER (task-start)  %lld\n", cd_init_noenv);
        printf("  of those differing bytes, the CARTRIDGE value was:\n");
        printf("    unchanged from task start (preserved) %lld\n", cd_preserved);
        printf("    changed during the task (rewritten)   %lld\n", cd_rewritten); }
#if SL_ACMD_DWLOG
    printf("\n  COMMAND-AT-A-TIME vs WHOLE-TASK EQUIVALENCE (gates everything below)\n");
    printf("    tasks compared %lld | rc differs %lld | DRAM differs %lld | "
           "DMEM differs %lld | writer-op differs %lld | changed-flag differs %lld\n",
           EQ_tasks, EQ_rc, EQ_dram, EQ_dmem, EQ_op, EQ_chg);
    printf("    writer-task differs %lld | writer-cmd differs %lld | origin task/cmd/op/kind/src "
           "differ %lld/%lld/%lld/%lld/%lld\n", EQ_task, EQ_cmd, EQ_orT, EQ_orC, EQ_orO, EQ_orK, EQ_orS);
    if (EQ_rc || EQ_dram || EQ_dmem || EQ_op || EQ_chg || EQ_task || EQ_cmd
        || EQ_orT || EQ_orC || EQ_orO || EQ_orK || EQ_orS)
        printf("    *** NOT EQUIVALENT - the last-writer numbers below are VOID ***\n");
    printf("\n  FINAL PHYSICAL WRITER of DMEM 0xF9C..0xF9F just before each\n");
    printf("  resampler-init consumption (%lld consumptions):\n", LW_total);
    printf("    PER BYTE (4 bytes x %lld consumptions):\n", LW_total);
    printf("      last writer the mixer (op 3)   %lld bytes, %lld of them differing\n", LW_mixer_b, LW_mixer_bad);
    printf("      last writer another opcode     %lld bytes, %lld of them differing\n", LW_other_b, LW_other_bad);
    printf("      no writer since task start     %lld bytes, %lld of them differing\n", LW_none_b, LW_none_bad);
    printf("      four-byte groups whose bytes have DIFFERENT last writers: %lld\n", LW_mixedgroups);
    { int q; printf("    opcodes seen as last writer:");
      for (q = 0; q < 16; q++) if (LW_opseen[q]) printf(" %d", q);
      printf("\n"); }
    printf("    differing bytes: %lld same-value at task end, %lld changed\n", LW_bad_same, LW_bad_changed);
#endif
    printf("\n=== stage 1-3 replay ===\n");
    printf("frames byte-exact : %d\n", total_ok);
    printf("frames differing  : %d\n", total_bad);
    printf("bytes compared    : %lld\n", cmp_bytes);
    printf("  of which nonzero: %lld  (%.2f%%)\n", cmp_nonzero,
           cmp_bytes ? 100.0 * (double) cmp_nonzero / (double) cmp_bytes : 0.0);
    printf("bytes differing   : %lld\n", diff_bytes);
    printf("sample error over %lld nonzero written samples:\n", err_n);
    printf("  max |err|       : %d\n", err_max);
    printf("  RMS  err        : %.2f\n",
           err_n ? sqrt(err_sq / (double) err_n) : 0.0);
    if (cmp_nonzero == 0)
        printf("NOTE: every compared byte was zero. This proves the plumbing\n"
               "      (cadence, lengths, addressing) and proves NOTHING about\n"
               "      the mix/FX arithmetic, which silence cannot exercise.\n");
    return total_bad ? 1 : 0;
}
