/* xtask.h - ONE cross-task state scorer, used by every subsystem that carries
 * per-voice state across task boundaries.
 *
 * Written because the mixer and resampler scorers were derived separately and
 * drifted: four distinct instrument bugs were found in the resampler one, each
 * caught only by a result contradicting something already established. Every
 * one of them was a property of the SCAFFOLDING, not of the model:
 *
 *   1. scoring every continuation as if it were a cross-task boundary, so
 *      intra-task consumers were compared against stale carried state;
 *   2. scoring once per COMMAND rather than once per distinct state block;
 *   3. applying the carried state AFTER the command loop instead of before it,
 *      so state was overlaid and scored post-execution;
 *   4. a propagation control corrupting a block with no later consumer - the
 *      injection applied, and nothing moved.
 *
 * Each is structurally impossible here: xt_task_begin/xt_consume enforce the
 * once-per-block-per-task rule, xt_consume refuses a block whose last writer is
 * this same task, the API separates "before execution" from "after execution"
 * by having two entry points, and xt_inject records whether the injected block
 * is ever consumed again so a vacuous control cannot pass silently.
 *
 * UNGROUNDED BYTES ARE EXCLUDED, NOT ABSORBED: a caller supplies a mask, the
 * excluded bytes are counted separately, and the score is explicitly scoped to
 * the rest.
 */
#ifndef SL_XTASK_H
#define SL_XTASK_H
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define XT_MAX  256
#define XT_BLK  128
#define XT_SEEN 256

typedef struct {
    const char *name;
    int      blksz;
    const uint8_t *ungrounded;      /* blksz bytes; 1 = exclude from scoring */
    uint32_t addr[XT_MAX];
    uint8_t  data[XT_MAX][XT_BLK];
    int      valid[XT_MAX], lasttask[XT_MAX], n;
    uint32_t seen[XT_SEEN]; int nseen, task;
    /* injected blocks, so a control that never gets consumed cannot pass */
    uint32_t inj_addr[XT_MAX]; uint8_t inj_hit[XT_MAX]; int inj_n, inj_consumed;
    long long edges, bytes, mis, ungr, resets, injections;
    int first_task, first_byte;   /* where the first mismatch appeared */
} xt_state;

static void xt_init(xt_state *x) { x->first_task = -1; x->first_byte = -1; }
static int xt_slot(xt_state *x, uint32_t a)
{
    int i; for (i = 0; i < x->n; i++) if (x->addr[i] == a) return i;
    if (x->n < XT_MAX) { x->addr[x->n] = a; x->valid[x->n] = 0;
                         x->lasttask[x->n] = -1; return x->n++; }
    return -1;
}
static void xt_task_begin(xt_state *x, int task) { x->nseen = 0; x->task = task; }

/* Returns 1 if this block is a GENUINE cross-task consumer that has now been
 * scored and overlaid; 0 otherwise. MUST be called before the task executes. */
static int xt_consume(xt_state *x, uint32_t a, int is_init,
                      const uint8_t *before, uint8_t *dram_at)
{
    int sl, z, b;
    for (z = 0; z < x->nseen; z++) if (x->seen[z] == a) return 0;  /* once per block */
    if (x->nseen < XT_SEEN) x->seen[x->nseen++] = a;
    sl = xt_slot(x, a); if (sl < 0) return 0;
    if (is_init) { x->valid[sl] = 0; x->resets++; return 0; }      /* segment start */
    if (!x->valid[sl]) return 0;
    if (x->lasttask[sl] == x->task) return 0;                      /* not cross-task */
    x->edges++;
    for (b = 0; b < x->blksz; b++) {
        if (x->ungrounded && x->ungrounded[b]) { x->ungr++; continue; }
        x->bytes++;
        if (x->data[sl][b] != before[b]) { x->mis++;
            if (x->first_task < 0) { x->first_task = x->task; x->first_byte = b; } }
    }
    /* Count each injected block at most ONCE, at its first later cross-task
     * consumption. Counting every subsequent consumption would inflate the
     * figure with reads long after the block was re-saved correctly, which
     * answers no question worth asking. */
    for (z = 0; z < x->inj_n; z++) if (x->inj_addr[z] == a) {
        if (!x->inj_hit[z]) { x->inj_consumed++; x->inj_hit[z] = 1; } break; }
    memcpy(dram_at, x->data[sl], x->blksz);
    return 1;
}
/* MUST be called after the task executes. */
static void xt_save(xt_state *x, uint32_t a, const uint8_t *dram_at, int task)
{
    int sl = xt_slot(x, a); if (sl < 0) return;
    memcpy(x->data[sl], dram_at, x->blksz);
    x->valid[sl] = 1; x->lasttask[sl] = task;
}
static void xt_inject(xt_state *x, uint32_t a, int byte, uint8_t mask)
{
    int sl, z;
    /* ONCE per block. Injecting per command XORs a block hit twice back to its
     * correct value, so the control silently cancels itself and reports fewer
     * mismatches than it caused - measured: 5 injections, 5 blocks consumed,
     * and only 1 mismatch, because the rest had been flipped an even number of
     * times. */
    for (z = 0; z < x->inj_n; z++) if (x->inj_addr[z] == a) return;
    sl = xt_slot(x, a); if (sl < 0 || !x->valid[sl]) return;
    x->data[sl][byte] ^= mask; x->injections++;
    if (x->inj_n < XT_MAX) x->inj_addr[x->inj_n++] = a;
}
static void xt_report(const xt_state *x)
{
    printf("  CROSS-TASK SCORER [%s]\n", x->name);
    printf("    grounded cross-task consumers : %lld\n", x->edges);
    printf("    GROUNDED bytes compared       : %lld\n", x->bytes);
    printf("    mismatches vs cartridge       : %lld\n", x->mis);
    printf("    ungrounded bytes EXCLUDED     : %lld\n", x->ungr);
    printf("    A_INIT segment resets         : %lld\n", x->resets);
    if (x->mis) printf("    first mismatch: task %d, byte +0x%02X\n",
                       x->first_task, x->first_byte);
    if (x->injections) {
        printf("    injections applied            : %lld\n", x->injections);
        printf("    injected blocks with a later cross-task consumer: %d of %d%s\n",
               x->inj_consumed, x->inj_n,
               x->inj_consumed ? "" : "   <- VACUOUS: nothing could have moved");
    }
}
#endif
