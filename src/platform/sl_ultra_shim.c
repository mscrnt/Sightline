/**
 * sl_ultra_shim.c - libultra on the host, recv-pump model.
 *
 * Design: docs/decisions/native-boot.md. The scheduler thread never runs;
 * instead, BLOCKING ON AN EMPTY QUEUE IS THE FRAME BOUNDARY. When the game
 * blocks on the frame queue, the pump advances fixed-step time and delivers
 * a retrace message. The game believes a scheduler exists; the scheduler is
 * the act of waiting.
 *
 * Time is deterministic from tick one: osGetCount is base + frame * step.
 * Phase 0 proved real time must never reach the sim.
 *
 * Host headers only - the SDK include tree shadows compiler headers.
 */
#include <stdarg.h>
#include "../sl_asan.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sl_acmd.h"

typedef int32_t  s32;
typedef uint32_t u32;
typedef int64_t  s64;
typedef uint64_t u64;

#define SL_LOG(...) do { if (getenv("SL_SHIM_LOG")) fprintf(stderr, "[shim] " __VA_ARGS__); } while (0)

/* ---- the real OSMesgQueue layout (32-bit), messages in the CALLER's buffer.
 * v1 overlaid a fat struct on the game's 24-byte object - silent corruption.
 */
typedef struct {
    void *mtqueue;      /* waiting threads - unused, single-threaded */
    void *fullqueue;
    s32   validCount;
    s32   first;
    s32   msgCount;
    void **msg;
} sl_OSMesgQueue;

void osCreateMesgQueue(sl_OSMesgQueue *q, void **buf, s32 count)
{
    q->mtqueue = q->fullqueue = 0;
    q->validCount = 0;
    q->first = 0;
    q->msgCount = count;
    q->msg = buf;
}

static unsigned sl_frame;
static u32 sl_count_extra;
/* VIs elapsed since the last game-frame boundary. The hardware's
 * waitForNextFrame busy-waits on osGetCount and hands updateFrameCounters the
 * number of VI periods that actually passed (frametiming.c:80-89), so that is
 * what the native default has to be too. Counted at the retrace, consumed and
 * cleared at the frame boundary in sl_ticks_next. */
static u32 sl_vi_elapsed;
static u32 sl_boot_elapsed;   /* frames of load time to inject, from env */
static FILE *sl_trace_f;
static u32 sl_trace_skip;
static u32 sl_trace_tick_no;
/* The scheduler thread never runs natively, so osSendMesg to its command
 * queue is intercepted and each task's own reply message is delivered to the
 * task's own reply queue on the next frame pump.  Mirror of OSScTask
 * (src/sched.h): 4 words, then OSTask (0x40, all fields 4-aligned on i386),
 * then msgQ/msg at 0x50/0x54. */
typedef struct sl_OSScTask_s {
    struct sl_OSScTask_s *next;
    unsigned state, flags;
    void *framebuffer;
    unsigned char list[0x40];
    sl_OSMesgQueue *msgQ;
    void *msg;
} sl_OSScTask;
extern sl_OSMesgQueue *sched_cmdQ;                 /* init.c */
static sl_OSScTask *sl_sc_tasks[8];
static unsigned sl_sc_head, sl_sc_count;

s32 osSendMesg(sl_OSMesgQueue *q, void *msg, s32 flag)
{
    (void) flag;
    if (q && q == sched_cmdQ) {
        sl_OSScTask *t = msg;
        if (sl_sc_count < 8)
            sl_sc_tasks[(sl_sc_head + sl_sc_count++) & 7] = t;
        SL_LOG("sched task @%p dl=%p msgQ=%p frame=%u\n",
               (void *) t, (void *) *(unsigned *) (t->list + 0x30),
               (void *) t->msgQ, sl_frame);
        return 0;
    }
    if (q->validCount >= q->msgCount)
        return -1;
    q->msg[(q->first + q->validCount) % q->msgCount] = msg;
    q->validCount++;
    return 0;
}

s32 osJamMesg(sl_OSMesgQueue *q, void *msg, s32 flag) { return osSendMesg(q, msg, flag); }

/* The cooperative stand-in for "the RSP has finished THIS task".
 *
 * amMain blocks on the audio reply queue inside the same iteration that
 * submitted the task (audi.c:633), so the audio manager's completion must be
 * available before its step ends - not on some later pump. Leaving it to the
 * pump's own delivery block below would put lastInfo one audio frame behind
 * and hand osAiSetNextBuffer the wrong buffer.
 *
 * Deliver the OLDEST pending task whose reply goes to q and compact it out of
 * the ring, so the pump's block - which exists to serve the queue the CALLER
 * is blocked on - never sees it. Returns 1 if one was completed.
 */
/* THE RSP'S SIDE OF AN AUDIO TASK.
 *
 * Wiring the accepted interpreter to the stream the manager now emits, so
 * something actually executes it. DEFAULT ON as of this change; SL_ACMD_EXEC=0
 * is the control that restores the old silence.
 *
 * The default used to be OFF, and the reason it gave is no longer true: it
 * named ENVMIXER and SETVOL as returning SL_ACMD_ERR_OPCODE and ADPCM as
 * gated off, so that a voice-bearing task would abort at its first unsupported
 * opcode. All three are implemented and ADPCM defaults on, and the fixture
 * says so rather than the comment: on facility/900/pinned seed the manager's
 * OWN stream runs 449 tasks, 449 complete without fault, 356263 commands, and
 * the submitted PCM goes from 0 nonzero samples to 291901 of 658592. Left off,
 * nothing executed the list and every buffer the game handed the DAC was the
 * zero-filled arena - the whole native audio path terminated in silence for a
 * reason that had nothing to do with audio.
 *
 * Completion is still NOT acceptance, and sl_acmd_exec_report keeps saying so:
 * the run leans on the ungrounded RESAMPLE state writeback and on the derived
 * ADPCM arithmetic, both counted as interface. A task that aborted produced no
 * audio and must not be counted as one that ran.
 *
 * The window is the music heap and nothing else. Every buffer an audio task
 * touches is allocated from it, and an address outside it returns
 * SL_ACMD_ERR_DRAM instead of reaching memory.
 *
 * Words are passed straight through: the native command list is already
 * host-order, which is exactly what sl_acmd_exec documents itself as taking,
 * so a transport byte-order mistake cannot be confused with a semantics one.
 */
static int sl_acmd_exec_on(void)
{
    static int m = -1;
    if (m < 0) { const char *e = getenv("SL_ACMD_EXEC"); m = !(e && !strcmp(e, "0")); }
    return m;
}

static sl_acmd_state sl_acmd_rsp;
static int sl_acmd_ready;
unsigned long sl_acmd_tasks_run, sl_acmd_tasks_ok, sl_acmd_cmds_run;
unsigned long sl_acmd_tasks_state, sl_acmd_tasks_rsinit, sl_acmd_tasks_adpcm;
unsigned long sl_acmd_err_n[8];
static int sl_acmd_first_err = -1;
static unsigned sl_acmd_first_err_op, sl_acmd_first_err_idx, sl_acmd_first_err_addr;

#ifdef SL_ALHEAP_TRACE
/* COMMAND-LEVEL BISECT of the produced signal, gated on SL_ACMD_BISECT=<task>.
 *
 * The submitted PCM is a smooth waveform multiplied by (-1)^n, and that is a
 * property of the OUTPUT. This finds the command that introduces it, without
 * touching the interpreter: sl_acmd_exec has no per-call setup - it is a plain
 * loop over commands with every piece of state in the sl_acmd_state - so
 * calling it one command at a time is exactly equivalent to calling it once
 * with all of them. The bisect is therefore not a second implementation of
 * anything, which is what would make its answer worthless.
 *
 * The metric is the lag-1 autocorrelation of the DMEM buffer region read as
 * big-endian s16. A smooth signal sits near +1; the (-1)^n modulation drives it
 * hard negative. Reported per command that CHANGES the region, so the first
 * negative r1 names its own producer.
 */
static double sl_bis_r1(const sl_acmd_state *s, uint32_t off, uint32_t bytes,
                        double *rms_out)
{
    long i, n = 0;
    double m = 0.0, ss = 0.0, cc = 0.0;
    static short v[SL_DMEM_SIZE / 2];
    *rms_out = 0.0;
    if (off + bytes > SL_DMEM_SIZE || bytes < 8) return 0.0;
    for (i = (long) off; i + 1 < (long) (off + bytes); i += 2)
        v[n++] = (short) ((s->dmem[i] << 8) | s->dmem[i + 1]);
    if (n < 4) return 0.0;
    for (i = 0; i < n; i++) m += v[i];
    m /= (double) n;
    for (i = 0; i < n; i++) ss += ((double) v[i] - m) * ((double) v[i] - m);
    for (i = 0; i + 1 < n; i++)
        cc += ((double) v[i] - m) * ((double) v[i + 1] - m);
    ss /= (double) n; cc /= (double) (n - 1);
    *rms_out = ss > 0.0 ? __builtin_sqrt(ss) : 0.0;
    return ss > 0.0 ? cc / ss : 0.0;
}

static const char *sl_bis_opname(int op)
{
    static const char *n[16] = {"SPNOOP","ADPCM","CLEARBUFF","ENVMIXER","LOADBUFF",
        "RESAMPLE","SAVEBUFF","SEGMENT","SETBUFF","SETVOL","DMEMMOVE","LOADADPCM",
        "MIXER","INTERLEAVE","POLEF","SETLOOP"};
    return n[op & 15];
}

static void sl_acmd_bisect(sl_acmd_state *st, const unsigned *w, unsigned n)
{
    unsigned k;
    fprintf(stderr, "ACMDBISECT commands=%u  (r1 of the region each command "
                    "WRITES - the live SETBUFF out/count - read big-endian s16)\n", n);
    for (k = 0; k < n; k++) {
        int op = (int) (w[2 * k] >> 24);
        uint32_t out, cnt; double r1, rms;
        int rc = sl_acmd_exec(st, w + 2 * k, 1);
        out = sl_acmd_sget(st, 0x02);
        cnt = sl_acmd_sget(st, 0x04);
        /* only the opcodes that PRODUCE sample data are worth a number; the
         * rest write no buffer and their r1 would just repeat the last one */
        if (op == 1 || op == 3 || op == 5 || op == 12 || op == 13 || op == 14) {
            r1 = sl_bis_r1(st, out, cnt, &rms);
            fprintf(stderr, "  %5u %-11s w0=%08x w1=%08x out=%04x cnt=%04x "
                            "rms=%9.1f r1=%+.3f%s\n",
                    k, sl_bis_opname(op), w[2 * k], w[2 * k + 1], out, cnt,
                    rms, r1, r1 < -0.3 ? "   ALTERNATING" : "");
            /* POLEF is a RECURSION, so its coefficients decide whether it
             * smooths or rings at Nyquist. Print the table LOADADPCM put there,
             * because "the filter is wrong" and "the filter was handed the
             * wrong numbers" are different defects with different owners. */
            if (op == 14) {
                int q; fprintf(stderr, "        coef[%d]:", st->coef_len);
                for (q = 0; q < st->coef_len && q < 16; q++)
                    fprintf(stderr, " %6d", st->coef[q]);
                fprintf(stderr, "\n");
            }
        }
        if (rc != SL_ACMD_OK) {
            fprintf(stderr, "  ABORT at %u: %s\n", k, sl_acmd_errstr(rc));
            return;
        }
    }
}

#endif /* SL_ALHEAP_TRACE */

static void sl_acmd_run_task(sl_OSScTask *t)
{
    const unsigned *w = *(const unsigned **) (t->list + 0x30);   /* data_ptr  */
    unsigned bytes    = *(const unsigned *)  (t->list + 0x34);   /* data_size */
    int rc;

    if (!sl_acmd_exec_on() || w == 0 || bytes < 8)
        return;
    if (!sl_acmd_ready) {
        void *base = 0; unsigned len = 0;
        extern void sl_music_heap_window(void **base, unsigned *len);
        sl_music_heap_window(&base, &len);
        if (base == 0 || len == 0)
            return;                       /* heap not up yet - nothing to run */
        sl_acmd_init(&sl_acmd_rsp, (unsigned char *) base,
                     (unsigned) (unsigned long) base, len);
        sl_acmd_ready = 1;
    }
    { extern unsigned long sl_acmd_adpcm_cmds;
      unsigned long b_st = sl_acmd_ungrounded_state,
                    b_rs = sl_acmd_ungrounded_rsinit,
                    b_ad = sl_acmd_adpcm_cmds;
#ifdef SL_ALHEAP_TRACE
      { static long bis = -2; static long seen;
        if (bis == -2) { const char *e = getenv("SL_ACMD_BISECT");
                         bis = e ? atol(e) : -1; }
        seen++;
        if (bis == seen) { sl_acmd_bisect(&sl_acmd_rsp, w, bytes / 8);
                           rc = SL_ACMD_OK; }
        else rc = sl_acmd_exec(&sl_acmd_rsp, w, bytes / 8); }
#else
      rc = sl_acmd_exec(&sl_acmd_rsp, w, bytes / 8);
#endif
      /* TASKS AFFECTED - a third quantity again, distinct from hits and from
       * commands. Taken at the task boundary because that is what a task
       * boundary is. */
      if (sl_acmd_ungrounded_state  != b_st) sl_acmd_tasks_state++;
      if (sl_acmd_ungrounded_rsinit != b_rs) sl_acmd_tasks_rsinit++;
      if (sl_acmd_adpcm_cmds        != b_ad) sl_acmd_tasks_adpcm++; }
    sl_acmd_tasks_run++;
    sl_acmd_cmds_run += bytes / 8;
    if (rc == SL_ACMD_OK) {
        sl_acmd_tasks_ok++;
    } else {
        if (rc >= 0 && rc < 8) sl_acmd_err_n[rc]++;
        if (sl_acmd_first_err < 0) {
            sl_acmd_first_err = rc;
            sl_acmd_first_err_op = (unsigned) sl_acmd_rsp.err_op;
            sl_acmd_first_err_idx = sl_acmd_rsp.err_index;
            /* the operand itself, so "outside the window" names a REGION
             * rather than being left to inference */
            if (sl_acmd_first_err_idx < bytes / 8)
                sl_acmd_first_err_addr = w[sl_acmd_first_err_idx * 2 + 1];
        }
    }
}

void sl_acmd_exec_report(void)
{
    int i;
    if (!sl_acmd_exec_on()) return;      /* silent when off - it is off by default */
    fprintf(stderr, "ACMDEXEC tasks=%lu completedWithoutFault=%lu commands=%lu\n",
            sl_acmd_tasks_run, sl_acmd_tasks_ok, sl_acmd_cmds_run);
    if (sl_acmd_tasks_run == 0) {
        fprintf(stderr, "  VACUOUS: no audio task was executed.\n");
        return;
    }
    for (i = 0; i < 8; i++)
        if (sl_acmd_err_n[i])
            fprintf(stderr, "  %-22s %lu task(s)\n", sl_acmd_errstr(i), sl_acmd_err_n[i]);
    /* UNGROUNDED PATHS. Each names a branch no measurement grounds. A clean
     * "449 of 449 completed" that silently leaned on one of these is a false
     * pass, so they are reported next to the completion count, never apart
     * from it. SL_ACMD_ADPCM defaults to 1 in this tree, so the ADPCM
     * arithmetic - derived but explicitly NOT accepted - is live here
     * (sl_acmd.c:82-83). */
    { extern unsigned long sl_acmd_cmds_state, sl_acmd_cmds_rsinit;
      extern unsigned long sl_acmd_adpcm_cmds, sl_acmd_adpcm_samples;
      /* Three DIFFERENT quantities per counter, because "counter hits" is not
       * "commands" and neither is "tasks". Units are derived at the increment
       * sites and stated here rather than left to the reader:
       *   negOverflow     one 48-bit accumulator low-half conversion that
       *                   overflowed NEGATIVE (sl_low_sat32, sl_acmd.c:172).
       *                   A per-sample-lane arithmetic event, not a command.
       *   resamplerState  one writeback of the 2-byte RESAMPLE state field at
       *                   +0x10..0x11 (sl_acmd.c:907). Per writeback, and one
       *                   RESAMPLE command performs many.
       *   resampleInit    one A_INIT-only resampler-state initialisation
       *                   copying 4 bytes from shared scratch (sl_acmd.c:946).
       */
      fprintf(stderr, "  ungrounded COUNTER HITS / commands / tasks:\n");
      fprintf(stderr, "    negOverflow    hits=%lu  (per accumulator conversion; "
                      "commands and tasks are 0 while hits are 0)\n",
              sl_acmd_ungrounded_neg);
      fprintf(stderr, "    resamplerState hits=%lu  commands=%lu  tasks=%lu  "
                      "(per 2-byte state writeback)\n",
              sl_acmd_ungrounded_state, sl_acmd_cmds_state, sl_acmd_tasks_state);
      fprintf(stderr, "    resampleInit   hits=%lu  commands=%lu  tasks=%lu  "
                      "(per 4-byte A_INIT scratch copy)\n",
              sl_acmd_ungrounded_rsinit, sl_acmd_cmds_rsinit, sl_acmd_tasks_rsinit);
      { extern unsigned long sl_acmd_cmds_state_local, sl_acmd_cmds_rsinit_local;
        extern unsigned long sl_acmd_adpcm_frames;
        /* ATTRIBUTION-KEY CONTROL. The local key can alias across tasks; the
         * monotonic one cannot. Under-counting by the local key is what proves
         * the monotonic key is load-bearing rather than incidental. */
        fprintf(stderr, "    attribution control: state monotonicKey=%lu "
                        "taskLocalKey=%lu | rsinit monotonicKey=%lu "
                        "taskLocalKey=%lu\n",
                sl_acmd_cmds_state, sl_acmd_cmds_state_local,
                sl_acmd_cmds_rsinit, sl_acmd_cmds_rsinit_local);
        if (sl_acmd_cmds_state_local < sl_acmd_cmds_state)
            fprintf(stderr, "      -> the task-local key ALIASES (%lu fewer); "
                            "the monotonic key is doing real work\n",
                    sl_acmd_cmds_state - sl_acmd_cmds_state_local);
        else
            fprintf(stderr, "      -> keys agree: no two attributed events "
                            "shared a task-local ordinal, so aliasing was "
                            "NOT exercised and the attribution is unproven\n");
        fprintf(stderr, "    ADPCM decode (derived, NOT accepted): commands=%lu "
                        "tasks=%lu decodeFramesCompleted=%lu residuals=%lu "
                        "(16/frame; NOT verified output writes)\n",
                sl_acmd_adpcm_cmds, sl_acmd_tasks_adpcm,
                sl_acmd_adpcm_frames, sl_acmd_adpcm_samples); } }
    { extern unsigned long sl_acmd_adpcm_cmds;
      if (sl_acmd_ungrounded_neg | sl_acmd_ungrounded_rsinit
          | sl_acmd_ungrounded_state | sl_acmd_adpcm_cmds)
        fprintf(stderr, "  *** execution relied on at least one path that no "
                        "measurement grounds, or the unaccepted decode path - "
                        "the completion count above is NOT an acceptance.\n"); }
    if (sl_acmd_first_err >= 0)
        fprintf(stderr, "  first fault: %s at command %u, opcode %s, "
                        "operand %#x\n  window %#x..%#x (the music heap)\n",
                sl_acmd_errstr(sl_acmd_first_err), sl_acmd_first_err_idx,
                sl_acmd_opname((int) sl_acmd_first_err_op),
                sl_acmd_first_err_addr, sl_acmd_rsp.dram_lo,
                sl_acmd_rsp.dram_lo + sl_acmd_rsp.dram_size);
}

int sl_sc_complete_for(void *q)
{
    unsigned i, j;

    for (i = 0; i < sl_sc_count; i++) {
        sl_OSScTask *t = sl_sc_tasks[(sl_sc_head + i) & 7];
        if ((void *) t->msgQ != q)
            continue;
        sl_acmd_run_task(t);
        for (j = i; j + 1 < sl_sc_count; j++)
            sl_sc_tasks[(sl_sc_head + j) & 7] = sl_sc_tasks[(sl_sc_head + j + 1) & 7];
        sl_sc_count--;
        osSendMesg(t->msgQ, t->msg, 0);
        return 1;
    }
    return 0;
}

#ifdef SL_ALHEAP_TRACE
/* SL_AUDIO_CTL selects one lifecycle control; read here because audi.c is a
 * decomp file that cannot see host <stdlib.h>. Default 0 = no control. */
int sl_audio_ctl_mode(void)
{
    static int m = -1;
    if (m < 0) {
        const char *e = getenv("SL_AUDIO_CTL");
        m = e ? atoi(e) : 0;
    }
    return m;
}
#endif

/* ---- fixed-step time ---------------------------------------------------- */
#define SL_CYCLES_PER_FRAME 775875u   /* the game's own frame divisor: elapsed of t*775875 yields exactly t ticks through waitForNextFrame's rounding */
static u32 sl_frame;
#include "../gfx/sl_gfx.h"
#include "sl_input.h"
#include "sl_settings.h"

/* Defined with the style sidecar below; called from the frame pump above it. */
static void sl_cont_count_report(void);

static u32 sl_frame_limit = 1000;
static int sl_frame_limit_set;          /* SL_FRAMES named explicitly */

/* THE QUIT REQUEST (owner-observed 2026-09-20: the dossier's main menu had
 * no QUIT GAME, so at a large resolution or fullscreen there was no normal
 * way to close). The flag itself lives with the window's other lifecycle
 * state (src/platform/sl_window.c, where the self-test can reach it); the
 * front end's QUIT GAME row files it and THIS pump honours it at the next
 * frame boundary, taking the very path SDL_QUIT takes. Nothing exits from
 * menu code. */
extern int sl_quit_requested(void);

/* ---- real-time pacing ---------------------------------------------------
 * The pump is as fast as the host will go, which is right for replay and
 * wrong for playing: 3000 frames went by in 5 seconds, so a windowed run
 * sprinted through its frame budget and exited - the "window opens, shows
 * nothing, closes" report.
 *
 * Pacing is wall-clock only and touches no sim state, but it is still gated
 * on a live window so replay keeps running flat out. Time reaching the sim
 * is what Phase 0 ruled out; time reaching sleep(2) is not the same thing.
 */
#include <time.h>
#include <errno.h>

static double sl_fps = 60.0;            /* SL_FPS overrides; VI rate for NTSC */
static double sl_next_due;              /* monotonic seconds, next frame due */

static double sl_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

/* SL_TICK_DBG: game-time rate against the wall clock.
 *
 * The question this answers is whether the native build SIMULATES faster than
 * Rare's, or merely renders more frames.  Those are different: the sim clock
 * is `currentFrameCounter` (frametiming.c), which counts VI ticks and which
 * every timer, animation and AI delay is derived from.  On NTSC hardware it
 * advances 60 per wall second no matter what frame rate the renderer manages,
 * because `waitForNextFrame` converts ELAPSED CYCLES to ticks rather than
 * counting frames.  So the only honest measurement is ticks per wall second,
 * and frames per wall second beside it.  Reading either one alone is what
 * makes a doubled frame rate look like a doubled game speed.
 *
 * Inert unless named, and platform-only - `currentFrameCounter` is read, never
 * written, so this cannot perturb what it measures.
 */
void sl_tick_rate_report(void)
{
    extern s32 currentFrameCounter;
    static int on = -1;
    static double t0, tprev;
    static s32 c0, cprev;
    static u32 f0, fprev;
    double now, dt, dt0;

    if (on < 0) on = (getenv("SL_TICK_DBG") != NULL);
    if (!on) return;

    now = sl_now();
    if (t0 == 0.0) {
        t0 = tprev = now;
        c0 = cprev = currentFrameCounter;
        f0 = fprev = sl_frame;
        return;
    }
    dt = now - tprev;
    if (dt >= 2.0) {
        dt0 = now - t0;
        fprintf(stderr, "sl_tick: window ticks=%d frames=%u dt=%.3f "
                        "| tick/s=%.2f frame/s=%.2f tick/frame=%.4f "
                        "| cume tick/s=%.2f frame/s=%.2f\n",
                currentFrameCounter - cprev, sl_frame - fprev, dt,
                (currentFrameCounter - cprev) / dt,
                (sl_frame - fprev) / dt,
                (sl_frame - fprev) ? (double) (currentFrameCounter - cprev)
                                     / (double) (sl_frame - fprev) : 0.0,
                dt0 > 0.0 ? (currentFrameCounter - c0) / dt0 : 0.0,
                dt0 > 0.0 ? (sl_frame - f0) / dt0 : 0.0);
        tprev = now; cprev = currentFrameCounter; fprev = sl_frame;
    }
}

#ifdef SL_ALHEAP_TRACE
unsigned long sl_pace_calls, sl_pace_skips, sl_pace_resync, sl_pace_slept;
unsigned long sl_pace_catchup;
double sl_pace_slept_s;
unsigned long sl_fadv_calls;
#endif
/* THE VI IS A TIMEBASE, NOT A LOOP COUNTER.
 *
 * Returns how many VI periods elapsed IN ADDITION to the one just serviced -
 * i.e. how many retraces the wall clock says were missed while the frame took
 * too long. Zero whenever the loop keeps up, and zero unconditionally when
 * there is no window or no pacing, which is every trace replay: the early
 * return below is the same one that has always disabled pacing, so replay is
 * bit-identical and real time still never reaches the simulation.
 *
 * MEASURED 2026-09-02, facility intro, windowed, SL_AUDIO_STATS + SL_TICK_DBG:
 * the caller emitted exactly one retrace per loop iteration, so the VI rate
 * was min(60, whatever the frame loop achieved). Over the intro the game's own
 * per-frame work saturated the wall second - a phase timer around the loop
 * attributed 0.97-1.03 s of each 1.00 s window to the game's own frame work,
 * against 0.002 s presenting and 0.005-0.020 s for everything this function
 * drives - and the VI rate fell to 33-48 Hz. Two consequences, both the
 * reported defect:
 *
 *   the SIM ran slow - currentFrameCounter is driven by elapsed VIs, so at
 *   34 Hz the whole game ran at 57% speed;
 *
 *   the DEVICE starved - the audio manager is notified every second retrace,
 *   so PRODUCTION fell with the VI rate to 0.47-0.69 s of audio per wall
 *   second while the DAC consumed 1.00. Over the first 8.20 s only 5.93 s of
 *   audio was ever submitted; SDL played the missing 2.27 s as inserted
 *   silence, in fragments smaller than one 4096-byte device buffer (the queue
 *   sat pinned at 2880-3008 B, i.e. below one callback, for the whole
 *   section). That is the crackling.
 *
 * On hardware the VI interrupt is a 60 Hz timebase that does not care how long
 * a frame took; a slow frame simply spans more VIs, which is why the cartridge
 * histogram at sl_ticks_next is {1:108, 2:224} rather than all ones. Restoring
 * that is restoring Rare's behaviour, so it is the default. SL_VI_CATCHUP=0 is
 * the control that puts the loop-counter model back.
 *
 * The catch-up is CAPPED. An uncapped one turns a level-load stall into a
 * burst of hundreds of retraces; the 0.25 s resync above already handles the
 * long stalls, and this covers a 4x shortfall, which is deeper than anything
 * measured.
 */
#define SL_VI_CATCHUP_MAX 3

static int sl_vi_catchup_on(void)
{
    static int m = -1;
    if (m < 0) { const char *e = getenv("SL_VI_CATCHUP"); m = !(e && !strcmp(e, "0")); }
    return m;
}

static int sl_pace_frame(void)
{
    double now, period;

#ifdef SL_ALHEAP_TRACE
    sl_pace_calls++;
#endif
    if (!sl_gfx_active() || sl_fps <= 0.0) {
#ifdef SL_ALHEAP_TRACE
        sl_pace_skips++;
#endif
        return 0;
    }

    period = 1.0 / sl_fps;
    now = sl_now();

    /* First frame, or we fell far enough behind that catching up would mean
     * a burst of unpaced frames - resync instead of sprinting. */
    if (sl_next_due == 0.0 || now > sl_next_due + 0.25) {
        sl_next_due = now + period;
#ifdef SL_ALHEAP_TRACE
        sl_pace_resync++;
#endif
        return 0;
    }

    if (sl_next_due > now) {
        struct timespec req;
        double d = sl_next_due - now;
#ifdef SL_ALHEAP_TRACE
        sl_pace_slept++; sl_pace_slept_s += d;
#endif
        req.tv_sec = (long) d;
        req.tv_nsec = (long) ((d - (double) req.tv_sec) * 1e9);
        while (nanosleep(&req, &req) != 0 && errno == EINTR)
            ;                            /* the pager's SIGSEGV handler fires often */
        sl_next_due += period;
        return 0;
    }

    /* LATE. Whole periods already gone are retraces the hardware would have
     * taken and this loop did not. Hand them back to the caller and advance
     * the schedule past them, so the deficit is settled rather than carried. */
    {
        int missed = (int) ((now - sl_next_due) / period);
        if (!sl_vi_catchup_on()) missed = 0;
        if (missed > SL_VI_CATCHUP_MAX) missed = SL_VI_CATCHUP_MAX;
        sl_next_due += (double) (missed + 1) * period;
#ifdef SL_ALHEAP_TRACE
        sl_pace_catchup += (unsigned long) missed;
#endif
        return missed;
    }
}

/* The hang watchdog in sl_main.c watches frame PROGRESS. sl_frame is static
 * here, and an `extern unsigned sl_frame` in another file silently binds to an
 * unrelated FUNCTION symbol of the same name - which is exactly what happened:
 * the watchdog compared against a constant code address, so it would have
 * declared a hang on any session outlasting its budget. Hand the value over
 * through a function so the linker cannot get it wrong. */
unsigned sl_frames_completed(void) { return (unsigned) sl_frame; }

u32 osGetCount(void) { return 1000000u + sl_frame * SL_CYCLES_PER_FRAME + sl_count_extra; }
u64 osGetTime(void)  { return (u64) osGetCount(); }

/* ---- timers: delivered on the next pump of their queue ------------------ */
typedef struct { sl_OSMesgQueue *mq; void *msg; int armed; } sl_timer;
static sl_timer sl_timers[8];

s32 osSetTimer(void *timer, u64 countdown, u64 interval, sl_OSMesgQueue *mq, void *msg)
{
    unsigned i;
    (void) timer; (void) countdown; (void) interval;
    for (i = 0; i < 8; i++) {
        if (!sl_timers[i].armed) {
            sl_timers[i].mq = mq; sl_timers[i].msg = msg; sl_timers[i].armed = 1;
            return 0;
        }
    }
    return -1;
}
s32 osStopTimer(void *timer) { (void) timer; return 0; }

/* ---- the pump ----------------------------------------------------------- */
extern sl_OSMesgQueue gfxFrameMsgQ;          /* global in src/init.c */
extern sl_OSMesgQueue g_ContDisablePollSendMessageQueue;     /* src/joy.c */
extern sl_OSMesgQueue g_ContDisablePollReceiveMessageQueue;
extern sl_OSMesgQueue g_ContEnablePollSendMessageQueue;
extern sl_OSMesgQueue g_ContEnablePollReceiveMessageQueue;

#ifdef SL_ALHEAP_TRACE
/* Proof, not assumption, that removing the stand-in balanced driving the
 * handler: count retrace-path entries and joyPoll calls and require 1:1. */
unsigned long sl_retrace_n = 0, sl_joypoll_n = 0, sl_viadv_n = 0;
#endif
static struct { short type; char pad[30]; } sl_retrace_msg = { 1 /*OS_SC_RETRACE_MSG*/, {0} };

void sl_input_vi_advance(void);
void sl_vis_record_frame(void);
static void sl_w32be(unsigned char *p, u32 v);
static int sl_vis_active(void);


/* ONE VI RETRACE, and everything a VI retrace does.
 *
 * Factored out because it is now called more than once per loop iteration:
 * see sl_pace_frame. The order is load-bearing and unchanged - the input
 * stream advances, the AI FIFO drains, the game's own retrace handler runs,
 * and the audio manager steps on the notification that handler produced. */
static void sl_vi_retrace_once(void)
{
    /* ATOMIC with the joyPoll removal at the call site: __scHandleRetrace is
     * the game's OWN retrace handler and calls joyPoll itself (sched.c:321),
     * so the pump must not also call it - double-polling per retrace corrupts
     * the recorded-input stream and every measurement downstream of it on this
     * deterministic replay.
     *
     * Driving the handler also makes sc->frameCount live (sched.c:319). That
     * counter is what the audio client's notify condition reads (sched.c:334);
     * left at its permanent zero it reads "even" forever and would signal
     * audio every retrace instead of every second one.
     *
     * Its own RSP/RDP scheduling is inert here by construction: task
     * submissions are intercepted at the command queue (osSendMesg) and never
     * reach sc->cmdQ, so __scAppendList finds nothing and curRSPTask stays 0. */
    extern void __scHandleRetrace(void *sc);
    extern void *sl_os_scheduler_ptr(void);
#ifdef SL_ALHEAP_TRACE
    { extern unsigned long sl_retrace_n; sl_retrace_n++; }
#endif
    sl_input_vi_advance();
    /* The DAC drains whether or not the game looks at it, and the audio
     * manager sizes its next frame from what is left. One retrace, one drain -
     * the same 1:1 discipline the input advance is proven to. */
    { extern void sl_audio_ai_retrace(void); sl_audio_ai_retrace(); }
#ifdef SL_ALHEAP_TRACE
    { extern void sl_audio_observe_pre(void); sl_audio_observe_pre(); }
#endif
    __scHandleRetrace(sl_os_scheduler_ptr());
#ifdef SL_ALHEAP_TRACE
    /* between the handler and the step: the notify loop has run, so the frame
     * queue holds exactly what the live condition produced */
    { extern void sl_audio_observe_post(void); sl_audio_observe_post(); }
#endif
    { extern void sl_audio_step(void); sl_audio_step(); }
    sl_vi_elapsed++;
}

/*
 * Wait for a sequence player to reach AL_STOPPED, advancing the world while
 * waiting. src/music.c calls this in place of its three bare spins.
 *
 * WHY THE BARE SPIN CANNOT WORK HERE. Only __CSPVoiceHandler moves the player
 * out of AL_PLAYING (csplayer.c:238-297), it runs from alAudioFrame, and
 * alAudioFrame is reached from sl_audio_step - which this file drives from the
 * VI retrace above. On the console amMain owns its own thread and gets there
 * on its own; natively osCreateThread and osStartThread are no-ops (:871-873),
 * so the game thread spinning on the state is the same thread that would have
 * cleared it. Measured at src/music.c:912 before this existed: 2,000,000
 * consecutive reads of state=1 with g_AudioFrameCount frozen. That is a
 * softlock, not slowness, and it is what made the title sequence hang at the
 * gun barrel.
 *
 * ZERO COST WHEN ALREADY STOPPED, which is what protects the trace corpus. The
 * common case - and the only case any recorded trace reaches, since direct
 * boot never has a sequence playing at these call sites - tests the state once,
 * finds AL_STOPPED, pumps nothing and advances no input. Retraces are only
 * spent on the path that previously never returned.
 *
 * THE CAP IS A DIAGNOSTIC, NOT A POLICY. If a player ever fails to stop, the
 * old behaviour was an unbounded hang with no output; this gives up after a
 * generous bound and says so, which is a bug report rather than a frozen
 * window. 600 retraces is ten seconds of VI at NTSC rate, and the transition
 * measured here needs single digits.
 */
void sl_audio_wait_seqp_stopped(void *seqp)
{
    extern s32 alCSPGetState(void *seqp);
    unsigned spins = 0;

    if (seqp == NULL)
        return;
    while (alCSPGetState(seqp)) {
        if (++spins > 600) {
            fprintf(stderr, "sightline audio: sequence player %p still in "
                            "state %d after %u pumped retraces - giving up "
                            "rather than hanging\n",
                    seqp, (int) alCSPGetState(seqp), spins);
            return;
        }
        sl_vi_retrace_once();
    }
    /* SILENT ON THE ZERO PATH, LOUD OTHERWISE - because "this pumps nothing on
     * the paths traces take" is the whole determinism argument, and an argument
     * nobody can see fail is worth nothing. A retrace spent here advances the
     * recorded input stream, so any run that prints this line has had its VI
     * timing moved and is not comparable to one that did not. Direct boot and
     * the headless gate print nothing at all; if they ever start, that is the
     * regression, stated by the binary rather than inferred. */
    if (spins > 0)
        fprintf(stderr, "sightline audio: pumped %u retrace(s) waiting for "
                        "sequence player %p to stop\n", spins, seqp);
}

/* ---- SL_PHASE: where a boot frame's wall time actually goes -------------
 *
 * PROFILING PROBE, off unless SL_PHASE is set to something other than "0".
 * It answers one question and no others: when an otherwise identical run
 * loses rendered updates across a boot screen, WHICH PHASE consumed the
 * missing wall time? The answer it produced is B-094 in docs/backlog.md.
 *
 * FOUR BUCKETS, and the split is exhaustive by construction rather than by
 * hope. One presented frame spans two services of gfxFrameMsgQ; the boundary
 * marks below cut that span into
 *
 *   A  game/frame work   previous frame's end -> the retrace block below.
 *                        This is everything the GAME did, and it INCLUDES B.
 *   B  sl_gfx_frame_dl   accumulated in src/gfx/sl_gfx_dl.c, drained here.
 *                        A subset of A, reported beside it, not added to it.
 *   C  retrace/platform  the VI retraces, the audio service they drive,
 *                        presentation, window events and the per-frame
 *                        reports - the whole tail, minus D.
 *   D  sleep/headroom    wall time actually spent inside sl_pace_frame.
 *
 * so A + C + D is the frame, and the report prints the residual against the
 * measured interval wall so a hole in that claim is visible rather than
 * argued. D IS MEASURED, NOT COMPUTED. A pacer-slack figure derived from the
 * schedule lies when the loop is behind - it reports accumulating deficit as
 * negative headroom. Wall time inside the pacer cannot go negative: a loop
 * with nothing to spare simply reads D near zero, which is the true answer.
 *
 * IT MUST NOT BE THE COST IT MEASURES. So: no per-frame output, no file, no
 * capture. Four clock reads and a handful of adds per frame, accumulated in
 * memory, and ONE summary line per boot screen - emitted when the frontend
 * menu changes, which is the natural interval boundary and costs a single
 * integer compare per frame. current_menu is READ, never written.
 */
extern int current_menu;                 /* MENU, src/game/front.c:260 */
extern s32 g_ClockTimer;                 /* src/game/lv.c:94 */
extern s32 g_MenuTimer;                  /* src/game/front.c:264 */
void sl_phase_dl_take(double *dl_s, double *walk_s);   /* src/gfx/sl_gfx_dl.c */

static int sl_phase_on(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_PHASE"); on = (v != NULL && *v != '0'); }
    return on;
}

static double ph_t_frame_end;            /* end of the previous presented frame */
static double ph_t_work_end;             /* game work -> retrace boundary */
static double ph_d_s;                    /* pacer wall time, this frame */
static int    ph_missed;                 /* catch-up retraces, this frame */

/* interval accumulators */
static int      ph_menu = -0x7fff;
static unsigned ph_frames, ph_clk1, ph_clk2, ph_clk3, ph_catchup, ph_slept;
static double   ph_a, ph_b, ph_bw, ph_c, ph_d, ph_wall, ph_amax, ph_bmax;
static double   ph_t0;                   /* interval start, for the residual */
static s32      ph_mt0;

static const char *sl_phase_menu_name(int m)
{
    switch (m) {
    case 0:  return "LEGAL";
    case 1:  return "NINTENDO";
    case 2:  return "RAREWARE";
    case 3:  return "EYE_INTRO";
    case 4:  return "GOLDENEYE";
    case 5:  return "FILE_SELECT";
    default: return "menu";
    }
}

static void sl_phase_flush(void)
{
    double wall_meas;
    if (!sl_phase_on() || ph_frames == 0) return;
    wall_meas = ph_t_frame_end - ph_t0;
    fprintf(stderr,
        "SL_PHASE %-11s(%3d) frames=%4u wall=%7.3fs fps=%5.1f | "
        "A game=%6.3fs(%4.1f%%) B dl=%6.3fs(%4.1f%%, walk %6.3fs) "
        "C rt/plat=%6.3fs(%4.1f%%) D sleep=%6.3fs(%4.1f%%)\n",
        sl_phase_menu_name(ph_menu), ph_menu, ph_frames, ph_wall,
        ph_wall > 0.0 ? ph_frames / ph_wall : 0.0,
        ph_a, ph_wall > 0.0 ? 100.0 * ph_a / ph_wall : 0.0,
        ph_b, ph_wall > 0.0 ? 100.0 * ph_b / ph_wall : 0.0, ph_bw,
        ph_c, ph_wall > 0.0 ? 100.0 * ph_c / ph_wall : 0.0,
        ph_d, ph_wall > 0.0 ? 100.0 * ph_d / ph_wall : 0.0);
    fprintf(stderr,
        "SL_PHASE %-11s      clk{1:%u,2:%u,3+:%u} catchup=%u slept=%u/%u "
        "menutimer=%d..%d  worst A=%.4fs B=%.4fs  residual=%+.4fs\n",
        sl_phase_menu_name(ph_menu), ph_clk1, ph_clk2, ph_clk3, ph_catchup,
        ph_slept, ph_frames, (int) ph_mt0, (int) g_MenuTimer,
        ph_amax, ph_bmax, wall_meas - (ph_a + ph_c + ph_d));
    ph_frames = 0; ph_clk1 = ph_clk2 = ph_clk3 = ph_catchup = ph_slept = 0;
    ph_a = ph_b = ph_bw = ph_c = ph_d = ph_wall = ph_amax = ph_bmax = 0.0;
}

/* End of game/frame work. Everything after this call and before the frame end
 * is retrace, pacing and presentation. */
static void sl_phase_work_end(void)
{
    if (!sl_phase_on()) return;
    ph_t_work_end = sl_now();
    ph_d_s = 0.0;
    ph_missed = 0;
}

/* Wall time inside the pacer, measured rather than derived. */
static int sl_phase_pace_frame(void)
{
    int missed;
    double t0, dt;
    if (!sl_phase_on()) return sl_pace_frame();
    t0 = sl_now();
    missed = sl_pace_frame();
    dt = sl_now() - t0;
    ph_d_s += dt;
    if (dt > 0.0005) ph_slept++;             /* actually gave time back */
    ph_missed += missed;
    return missed;
}

static void sl_phase_frame_end(void)
{
    double now, a, cd, wall;
    double dl_s = 0.0, walk_s = 0.0;

    if (!sl_phase_on()) return;
    now = sl_now();
    sl_phase_dl_take(&dl_s, &walk_s);

    if (ph_t_frame_end == 0.0 || ph_t_work_end == 0.0) {  /* first frame: no span yet */
        ph_t_frame_end = now;
        ph_menu = current_menu;
        ph_t0 = now;
        ph_mt0 = g_MenuTimer;
        return;
    }

    if (current_menu != ph_menu) {
        sl_phase_flush();
        ph_menu = current_menu;
        ph_t0 = ph_t_frame_end;
        ph_mt0 = g_MenuTimer;
    }

    a    = ph_t_work_end - ph_t_frame_end;
    cd   = now - ph_t_work_end;
    wall = now - ph_t_frame_end;

    ph_frames++;
    ph_wall += wall;
    ph_a += a;
    ph_b += dl_s;
    ph_bw += walk_s;
    ph_d += ph_d_s;
    ph_c += cd - ph_d_s;
    ph_catchup += (unsigned) ph_missed;
    if (a > ph_amax) ph_amax = a;
    if (dl_s > ph_bmax) ph_bmax = dl_s;
    if (g_ClockTimer <= 1) ph_clk1++;
    else if (g_ClockTimer == 2) ph_clk2++;
    else ph_clk3++;

    ph_t_frame_end = now;
}

static void sl_pump(sl_OSMesgQueue *q)
{

    unsigned i;
    for (i = 0; i < 8; i++) {
        if (sl_timers[i].armed && sl_timers[i].mq == q) {
            sl_timers[i].armed = 0;
            osSendMesg(q, sl_timers[i].msg, 0);
            return;
        }
    }
    /* joy.c's poll handshakes and the poll cycle itself both live in
     * joyPoll(), which only the scheduler thread calls on hardware.  The
     * pump stands in for the scheduler: a block on a handshake ack queue
     * means a request is pending - run joyPoll and it consumes the request
     * and sends the ack. */
    if (q == &g_ContDisablePollReceiveMessageQueue
        || q == &g_ContEnablePollReceiveMessageQueue) {
        extern void joyPoll(void);
        joyPoll();
        if (q->validCount > 0)
            return;
    }
    if (q == &gfxFrameMsgQ) {
        /* A FINISHED GRAPHICS TASK IS NOT A VERTICAL RETRACE, which is why
         * this delivery now sits AHEAD of the retrace below. The comment here
         * always said "deliver its reply first"; the block was second, and
         * that one-line disagreement set the whole front end running at half
         * the update rate.
         *
         * MEASURED, windowed, SL_FPS=60, over the Legal screen (BOOTPROBE):
         *
         *   gfxsvc=242  taskret=121  retrace=242  game frames=121  missed=0
         *   g_ClockTimer == 2 on 121 of 121 frames
         *
         * Two pump services occur per game frame - this one delivering the
         * completed task, and one finishing the frame - and BOTH ran a
         * retrace. So every game frame was charged two VIs: sl_vi_elapsed was
         * 2, sl_ticks_next returned 2, deltaFrames was 2, and the game update
         * ran at 30 Hz behind a correctly paced 60 Hz VI. paced missed=0, so
         * the VI catch-up was not involved; SL_VI_CATCHUP=0 reproduces the
         * same 242/121 exactly.
         *
         * That pulls Rare's two clocks apart, and the split is visible on
         * screen. The menu timers accumulate g_ClockTimer, so they stayed
         * RIGHT - the Legal screen's 241 ticks still took 4.03 s. The logo
         * animation advances a fixed step per CONSTRUCTOR call, so it got half
         * the calls it should: the Nintendo logo took 250 steps across its 501
         * VIs where the console takes one per VI. Same screen duration, half
         * the rotation - which is exactly what comparing against the cartridge
         * showed.
         *
         * On hardware the reply to a finished RSP/RDP task arrives on its own
         * interrupt and costs no retrace whatever, so the repair is to stop
         * spending one here. No timer, threshold or animation step is touched.
         */
        if (sl_sc_count > 0) {
            sl_OSScTask *t = sl_sc_tasks[sl_sc_head & 7];
            sl_sc_head++;
            sl_sc_count--;
            osSendMesg(t->msgQ ? t->msgQ : q, t->msg, 0);
            /* Return only when the reply actually landed on the queue the
             * caller is blocked on - otherwise boss.c gets -1 from a BLOCKING
             * receive and reads a stale message pointer. With audio off every
             * task replies to gfxFrameMsgQ (rsp.c:272), so this is the same
             * behaviour as before; with audio on it is not. */
            if (!t->msgQ || (sl_OSMesgQueue *) t->msgQ == q)
                return;
        }
        /* SL_PHASE: game/frame work ends here - everything below this line
         * is retrace, pacing and presentation. */
        sl_phase_work_end();
        /* with a VI-target stream, polling is entirely target-driven in
         * sl_ticks_next; without one, stand in for the scheduler's
         * per-retrace joyPoll here */
        if (!sl_vis_active()) {
            sl_vi_retrace_once();
            /* ONE SLEEP PER VI RETRACE, and the retrace is the right boundary
             * because it is the clock the HARDWARE runs. sl_fps is documented
             * at its declaration as "VI rate for NTSC", and everything in the
             * block above is a VI-retrace side effect: the input stream
             * advances, the AI FIFO drains, __scHandleRetrace runs, and the
             * audio manager is notified on every second one.
             *
             * The sleep used to sit at the PRESENTATION boundary further down,
             * which is a different event and a rarer one. Measured on
             * facility/600 frames, windowed, SL_FPS=60:
             *
             *   PACE calls=300  slept=294  total=2.45s
             *   POLLBALANCE retraces=599
             *   AUDIOLIFE steps=299        wall 5.32s
             *
             * Two pump services occur per presented frame - one delivers the
             * completed GRAPHICS task, whose reply goes to gfxFrameMsgQ and so
             * takes the early return ABOVE (which, now that the block sits
             * ahead of this one, no longer spends a retrace), and one finishes
             * the frame - so pacing at the presentation site slept 300 times
             * where 599 retraces happened. The VI therefore ran at ~112 Hz instead of 60,
             * and every retrace-driven system ran with it: the audio manager
             * emitted ~56 tasks a second where the console emits ~30.
             *
             * That is a 2x audio PRODUCTION rate against a device that consumes
             * at 1x, so SDL's queue grew by one second of audio per real
             * second - measured at +0.997 s/s over 29 s, reaching 29.5 s of
             * backlog. Continuous music has no external timing reference and
             * still sounds correct behind that; a gunshot does not, which is
             * why effects appeared minutes late while music seemed fine.
             *
             * Pacing here makes the VI period the thing being held, so the
             * retrace-to-presentation ratio stays whatever the game asks for
             * rather than being fixed by where the sleep happens. */
            {
                /* The pacer is the only thing here that knows the wall clock,
                 * so it is also the only thing that can say how many VI
                 * periods went by unserviced. Run them: on hardware they
                 * happened whether or not the frame was ready. */
                int missed = sl_phase_pace_frame();
                while (missed-- > 0)
                    sl_vi_retrace_once();
            }
        }
        if (sl_trace_f != NULL) {
            if (sl_trace_skip > 0) {
                sl_trace_skip--;
            } else {
                extern void sl_state_capture(u32 tick);
                sl_state_capture(sl_trace_tick_no++);
            }
        }
        /* Frame boundary: the game has finished building this frame, so
         * present it and take window events. All no-ops under the null
         * backend, which is the default - trace replay must not sprout a
         * window. */
        sl_gfx_end_frame();
        /* The window closing (SDL_QUIT: the X, Alt+F4) and the front end's
         * QUIT GAME row (sl_quit_request, below) leave by the SAME path: the
         * backend torn down - the pointer grab dropped, the GL context and
         * the window destroyed, SDL_Quit restoring any exclusive mode - then
         * exit(0) with its atexit work (the save flush, the reports). */
        if (!sl_gfx_poll() || sl_quit_requested()) {
            fprintf(stderr, sl_quit_requested()
                            ? "sightline native: quit requested at frame %u\n"
                            : "sightline native: window closed at frame %u\n", sl_frame);
            if (sl_trace_f) fclose(sl_trace_f);
            sl_gfx_shutdown();
            exit(0);
        }
        sl_gfx_begin_frame();
        /* Only when the retrace block above was skipped. A VI-target stream
         * drives its own cadence through sl_ticks_next and never enters that
         * block, so without this it would not be paced at all - but it is
         * paced WITHOUT catching up: the missed count belongs to the
         * retrace-driven path above, so it is deliberately discarded here. */
        if (sl_vis_active())
            (void) sl_phase_pace_frame();
        sl_cont_count_report();
        sl_vis_record_frame();
        sl_tick_rate_report();
        sl_phase_frame_end();
        /* B-100 PROBE, temporary. Inert unless SL_CAM_DBG is set; see
         * src/native/sl_game_query.c. */
        { extern s32 sl_cam_probe_frame(s32 frame); (void) sl_cam_probe_frame((s32) sl_frame); }
        /* Developer mark-teleport, native side (src/native/sl_teleport.c).
         * Inert unless SL_TELEPORT is set: one getenv on the first frame, an
         * int test thereafter, so an ordinary launch pays nothing. */
        { extern void sl_teleport_poll(void); sl_teleport_poll(); }
        /* Developer cheats and starting weapon (src/native/sl_cheat.c).
         * Inert unless SL_CHEATS or SL_WEAPON is set, on the same terms. */
        { extern void sl_cheat_poll(void); sl_cheat_poll(); }
        /* Mission-progress probe (src/native/sl_mission_dbg.c). Inert unless
         * SL_MISSION_DBG is set, on the same terms. */
        { extern void sl_mission_probe_frame(s32 frame); sl_mission_probe_frame((s32) sl_frame); }
        /* Inventory witness (src/native/sl_inv_dbg.c). Inert unless
         * SL_INV_DBG is set, on the same terms. */
        { extern void sl_inv_probe_frame(s32 frame); sl_inv_probe_frame((s32) sl_frame); }
        /* Ejected-casing witness (src/native/sl_casing_dbg.c). Inert unless
         * SL_CASING_DBG is set, on the same terms. */
        { extern void sl_casing_probe_frame(s32 frame); sl_casing_probe_frame((s32) sl_frame); }
        /* Character-record pointer witness (src/native/sl_chr_watch.c).
         * Inert unless SL_CHR_WATCH is set, on the same terms. */
        { extern void sl_chr_watch_frame(s32 frame); sl_chr_watch_frame((s32) sl_frame); }
        /* Credits-roll layout witness (src/native/sl_credits_dbg.c). Inert
         * unless SL_CREDITS_DBG is set, on the same terms. */
        { extern void sl_credits_probe_frame(s32 frame); sl_credits_probe_frame((s32) sl_frame); }
        sl_frame++;
        /* limit 0 means run until the window is closed. A windowed run that
         * did not name SL_FRAMES gets that too: the 1000-frame default is a
         * bring-up guard so a headless hang cannot spin forever, and applying
         * it to someone playing the game just quits on them mid-level.
         * Decided here rather than in sl_shim_configure because that runs
         * before sl_gfx_init, when there is no window to ask about yet. */
        if (!sl_frame_limit_set && sl_gfx_active())
            sl_frame_limit = 0;
        if (sl_frame_limit != 0 && sl_frame >= sl_frame_limit) {
#ifdef SL_ALHEAP_TRACE
            { extern void sl_poll_balance_report(void); sl_poll_balance_report();
              extern void sl_audio_lifecycle_report(void);
              sl_audio_lifecycle_report();
              extern void sl_noteoff_report(void); sl_noteoff_report();
              extern void sl_duration_report(void); sl_duration_report();
              extern void sl_pvoice_report(void); sl_pvoice_report();
              extern void sl_sfx_report(void); sl_sfx_report(); }
#endif
            { extern void sl_acmd_exec_report(void); sl_acmd_exec_report(); }
            sl_phase_flush();
            fprintf(stderr, "sightline native: survived %u pumped frames\n", sl_frame);
            if (sl_trace_f) fclose(sl_trace_f);
            exit(0);
        }
        osSendMesg(q, &sl_retrace_msg, 0);
        return;
    }
    fprintf(stderr, "sightline native: blocking recv on unknown queue %p at frame %u - "
                    "this is the next thing to implement\n", (void *) q, sl_frame);
    exit(2);
}

s32 osRecvMesg(sl_OSMesgQueue *q, void **msg, s32 flag)
{
    if (q->validCount == 0) {
        if (flag == 0 /*NOBLOCK*/)
            return -1;
        sl_pump(q);
        if (q->validCount == 0)
            return -1;
    }
    if (msg)
        *msg = q->msg[q->first];
    q->first = (q->first + 1) % q->msgCount;
    q->validCount--;
    return 0;
}

/* ---- threads: bookkeeping only; the main thread IS the process ---------- */
void osCreateThread(void *t, s32 id, void (*entry)(void *), void *arg, void *sp, s32 pri)
{ (void) t; (void) arg; (void) sp; (void) pri; SL_LOG("osCreateThread id=%d entry=%p\n", id, (void *) entry); }
void osStartThread(void *t)              { (void) t; }
void osStopThread(void *t)               { (void) t; }
void osSetThreadPri(void *t, s32 pri)    { (void) t; (void) pri; }
void osDestroyThread(void *t)            { (void) t; }
void osYieldThread(void)                 { }

/* ---- PI: the cartridge is the ROM file ---------------------------------- */
static unsigned char *sl_rom;
static long sl_rom_size;

static void sl_rom_load(void)
{
    const char *path = getenv("SL_ROM");
    FILE *f;
    if (!path) path = "build/u/ge007.u.z64";
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "sightline native: cannot open ROM %s\n", path); exit(3); }
    fseek(f, 0, SEEK_END); sl_rom_size = ftell(f); fseek(f, 0, SEEK_SET);
    sl_rom = malloc(sl_rom_size);
    if (fread(sl_rom, 1, sl_rom_size, f) != (size_t) sl_rom_size) exit(3);
    fclose(f);
    SL_LOG("ROM loaded: %s (%ld bytes)\n", path, sl_rom_size);
}

/* The whole image, read-only, for a native consumer that derives from the
 * ROM rather than DMAs from it (sl_ucode.c). Loads on first use exactly as
 * the PI paths do, so the SL_ROM contract is unchanged. */
const unsigned char *sl_rom_bytes(long *size)
{
    if (!sl_rom) sl_rom_load();
    *size = sl_rom_size;
    return sl_rom;
}

static long sl_rom_off(u32 devAddr) { return (long)(devAddr & 0x0FFFFFFFu); }

s32 osPiReadIo(u32 devAddr, u32 *data)
{
    long off = sl_rom_off(devAddr);
    if (!sl_rom) sl_rom_load();
    if (off + 4 <= sl_rom_size) {
        *data = ((u32) sl_rom[off] << 24) | ((u32) sl_rom[off+1] << 16)
              | ((u32) sl_rom[off+2] << 8) | sl_rom[off+3];
    } else {
        *data = 0;   /* past-end reads (the 0xFFB000 token probe) read as empty */
    }
    return 0;
}
s32 osPiWriteIo(u32 devAddr, u32 data) { (void) devAddr; (void) data; return 0; }
s32 osPiGetStatus(void) { return 0; }

s32 osPiRawStartDma(s32 dir, u32 devAddr, void *dram, u32 size)
{
    /* B-038.  Hardware DMA does not respect the allocator - it writes where the
     * game points it, including arena memory that has not been handed out as a
     * formal allocation. bgLoadRoomVtxData does exactly that. Tell ASan this
     * span is live before the copy, or every room load reports a
     * use-after-poison that is a faithful model of the hardware, not a bug.
     *
     * This deliberately WIDENS what ASan considers valid, so it can mask a
     * genuine DMA overrun. That is the right trade: modelling the hardware
     * honestly matters more than catching a class of bug the game's own DMA
     * sizes make unlikely. */
    SL_ASAN_UNPOISON(dram, (unsigned long) size);

    long off = sl_rom_off(devAddr);
    if (!dram) {
        /* stubbed allocators (audio-library heap) can hand callers NULL;
         * skipping the copy keeps a headless boot moving and fails loudly
         * later if anything actually reads the missing data */
        SL_LOG("DMA skipped: null dram for rom+0x%lx (%u bytes)\n", off, size);
        return -1;
    }
    if (!sl_rom) sl_rom_load();
    if (dir != 0) return 0;                        /* writes to cart: ignore */
    if (off < 0 || off >= sl_rom_size) { memset(dram, 0, size); return 0; }
    if (off + (long) size > sl_rom_size) size = (u32)(sl_rom_size - off);
    memcpy(dram, sl_rom + off, size);
    return 0;
}

s32 osPiStartDma(void *iomesg, s32 pri, s32 dir, u32 devAddr, void *dram, u32 size, sl_OSMesgQueue *mq)
{
    (void) iomesg; (void) pri;
    osPiRawStartDma(dir, devAddr, dram, size);
    if (mq) osSendMesg(mq, (void *) 0, 0);         /* completion message */
    return 0;
}
void osCreatePiManager(void *a, s32 b, void *c, s32 d) { (void) a; (void) b; (void) c; (void) d; }

/* ---- SP tasks: complete instantly; the oracle taps here later (T2) ------ */
void osSpTaskLoad(void *task)     { (void) task; }
void osSpTaskStartGo(void *task)  {
    extern void sl_gfx_task(void *task);
    SL_LOG("SP task @%p frame=%u\n", task, sl_frame);
    if (sl_gfx_active()) sl_gfx_task(task);
}
void osSpTaskYield(void)          { }
s32  osSpTaskYielded(void *task)  { (void) task; return 0; }

/* ---- AI: the audio DAC ---------------------------------------------------
 * The single seam where finished PCM leaves the game. A tree-wide
 * `grep -rn "osAi" src/ tools/ include/` finds four call sites outside
 * libultra, all of them in src/audi.c and covering three functions:
 * osAiSetFrequency (audi.c:340), osAiSetNextBuffer (522) and osAiGetLength
 * (529, 590). A companion `grep -rn "AI_DRAM_ADDR|AI_LEN_REG|AI_STATUS" src/`
 * outside libultra comes back EMPTY, so nothing bypasses these three by
 * poking the AI registers directly. This really is the whole boundary - the
 * audio equivalent of osContGetReadData for input.
 *
 * sl_audio.c opens a device only when a window is up or SL_AUDIO=1, so
 * headless replay reaches these and does nothing.
 */
unsigned int sl_audio_set_frequency(unsigned int rate);
unsigned int sl_audio_get_length(void);
int          sl_audio_submit(void *buf, unsigned int len);

s32 osAiSetFrequency(u32 freq)              { return (s32) sl_audio_set_frequency(freq); }
u32 osAiGetLength(void)                     { return sl_audio_get_length(); }
s32 osAiSetNextBuffer(void *buf, u32 size)  { return sl_audio_submit(buf, size); }

/* ---- VI ------------------------------------------------------------------ */
void osCreateViManager(s32 pri)                   { (void) pri; }
void osViSetEvent(sl_OSMesgQueue *mq, void *msg, u32 retraceCount) { (void) mq; (void) msg; (void) retraceCount; }
void osViSetMode(void *mode)                      { (void) mode; }
void osViSetSpecialFeatures(u32 f)                { (void) f; }
void osViSetXScale(float v)                       { (void) v; }
void osViSetYScale(float v)                       { (void) v; }
void osViSwapBuffer(void *fb)                     { (void) fb; }
void osViBlack(u32 active)                        { (void) active; }
void osViRepeatLine(u32 active)                   { (void) active; }
void *osViGetNextFramebuffer(void)                { static char fb[4]; return fb; }
void *osViGetCurrentFramebuffer(void)             { static char fb[4]; return fb; }

/* ---- events, interrupts, caches, TLB, misc ------------------------------ */
void osSetEventMesg(s32 event, sl_OSMesgQueue *mq, void *msg) { (void) event; (void) mq; (void) msg; }
u32  osSetIntMask(u32 mask)               { (void) mask; return 0; }
void osInitialize(void)                   { }
void osInvalDCache(void *a, s32 n)        { (void) a; (void) n; }
void osInvalICache(void *a, s32 n)        { (void) a; (void) n; }
void osWritebackDCache(void *a, s32 n)    { (void) a; (void) n; }
void osWritebackDCacheAll(void)           { }
u32  osVirtualToPhysical(void *a)         { return (u32)(uintptr_t) a; }
void osUnmapTLB(s32 idx)                  { (void) idx; }
u32  __osGetFpcCsr(void)                  { return 0; }
u32  __osSetFpcCsr(u32 v)                 { (void) v; return 0; }
u32  __osGetTLBHi(s32 idx)                { (void) idx; return 0; }
void osMapTLBRdb(void)                    { }

/* ---- controllers ---------------------------------------------------------
 * Controller 0 is always present, matching the emulator side where the
 * input plugin reports pad 0 connected whether or not a stream is loaded.
 *
 * SL_INPUT=<file> replays a recorded stream: one 4-byte big-endian word per
 * VI RETRACE.  This prose used to say "per CONTROLLER READ", and that was the
 * record/replay fidelity bug (2026-08-26): the stream is advanced by
 * sl_input_vi_advance(), once per retrace, and reads LATCH the current record
 * and consume nothing.  Reads are therefore NOT the stream's unit.  The
 * recorder below wrote one word per read, so a frame in which the game polled
 * twice emitted two records where replay consumes one, and the stream slid out
 * of phase with simulation time the further in you got.  Word
 * layout, CORRECTED 2026-08-25 - this comment used to read "buttons hi,
 * buttons lo, stick x, stick y", which is the reverse of what the code below
 * does and cost a day's worth of synthesised streams that decoded to a stick
 * deflection and no buttons at all:
 *
 *     byte 0  stick y      byte 2  buttons lo
 *     byte 1  stick x      byte 3  buttons hi
 * Neutral input after end of stream.
 */
typedef struct { unsigned short button; signed char stick_x, stick_y; unsigned char err, pad; } sl_OSContPad;
typedef struct { unsigned short type; unsigned char status, err; } sl_OSContStatus;

static unsigned char *sl_input_data;
/* The live pad, latched ONCE PER VI RETRACE in sl_input_vi_advance().  Reads
 * return this rather than sampling the keyboard afresh, which is what makes
 * live capture and replay the same shape: one value per retrace, latched, with
 * reads consuming nothing.  It is also what the SI does on hardware. */
static unsigned short sl_live_btn;
static signed char    sl_live_x, sl_live_y;
static int            sl_live_latched;
/* The second pad, latched on the same retrace as the first. */
static unsigned short sl_live_btn2;
static signed char    sl_live_x2, sl_live_y2;

/*
 * Whether to present TWO controllers instead of one.
 *
 * The 2.x control styles are the only ones in the game that can express modern
 * twin-stick or mouse-look (see sl_input.h), and they are unreachable unless
 * the game believes a second controller is plugged in: joyGetControllerCount
 * (src/joy.c:273) walks g_ConnectedControllers, and
 * controllerCheckDualControllerTypesAllowed (src/game/options.c:436) needs it
 * to be at least 2.
 *
 * The condition is exactly "live native input", and both halves matter:
 *
 *   sl_input_data == NULL  a recorded stream is four bytes per retrace - ONE
 *                          pad - and this milestone does not widen that
 *                          format. Replay must see what it always saw.
 *   sl_gfx_active()        no window means headless: trace replay and the
 *                          native health gate, neither of which has a live
 *                          device to read and both of which must be
 *                          bit-identical to before this existed.
 *
 * So determinism is untouched by construction, the same argument that governs
 * the live-input branch in osContGetReadData below.
 */
static int sl_live_two_pads(void)
{
    return sl_input_data == NULL && sl_gfx_active();
}

/**
 * The SAME condition, exported.
 *
 * src/platform/sl_input.c also has to decide whether live input exists, for
 * the native keyboard/mouse movement channels (src/native/sl_move_channels.c).
 * It must not answer that question with its own copy of the test: a second
 * copy is a second thing that can drift, and the thing it would drift on is
 * exactly the determinism guarantee - a recorded stream is four bytes per
 * retrace, ONE pad, and a headless run has no window. One predicate, one
 * definition, both callers.
 */
int sl_live_input_active(void)
{
    return sl_live_two_pads();
}
static unsigned sl_input_len;      /* bytes */
static unsigned sl_input_pos;      /* read index */
static unsigned sl_input_reads;
static unsigned sl_vi_advances;

/* The coordinate a mark is stamped in: replay is indexed by controller reads,
 * so this is what lets "I saw it here" survive into a replay. Defined here,
 * under the counter - the first attempt sat beside the vi_advance PROTOTYPE,
 * 267 lines above the declaration, and did not compile. */
/* The index a mark is stamped in MUST be the one the recorder writes, which
 * is the retrace counter - `input` holds one record per VI advance. It was
 * briefly the pad-read counter instead. The two run 1:1 today, so nothing
 * misbehaved, but nothing ENFORCES it either, and an unenforced ratio between
 * two counters is exactly what the replay-fidelity bug was. */
unsigned sl_record_index(void) { return (unsigned) sl_vi_advances; }

void sl_input_init(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;

    if (f == NULL) {
        fprintf(stderr, "sightline native: SL_INPUT open failed: %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    sl_input_data = malloc(n);
    fread(sl_input_data, 1, n, f);
    fclose(f);
    sl_input_len = (unsigned) n;
    fprintf(stderr, "sightline native: input stream %s (%lu reads)\n", path, (unsigned long) (n / 4));
}

s32 osContInit(sl_OSMesgQueue *mq, unsigned char *bitpattern, void *status)
{
    sl_OSContStatus *st = status;
    (void) mq;
    memset(st, 0, 4 * sizeof *st);
    st[0].type = 0x0005;            /* CONT_ABSOLUTE | CONT_JOYPORT */
    st[1].err = st[2].err = st[3].err = 0x8;   /* CONT_NO_RESPONSE_ERROR */
    *bitpattern = 1;
    if (sl_live_two_pads()) {
        st[1].type = 0x0005;
        st[1].err  = 0;
        *bitpattern = 3;            /* ports 1 and 2 occupied */
    }
    return 0;
}

s32 osContStartReadData(sl_OSMesgQueue *mq) { if (mq) osSendMesg(mq, (void *) 0, 0); return 0; }

void osContGetReadData(void *pads_)
{
    sl_OSContPad *pads = pads_;

    memset(pads, 0, 4 * sizeof *pads);
    pads[1].err = pads[2].err = pads[3].err = 0x8;
    /* the libretro frontend advances the stream once per VI regardless of
     * SI activity, and the core latches it - game reads see the CURRENT
     * record and consume nothing.  The advance lives with the VI polls. */
    if (sl_input_data != NULL) {
        if (sl_input_pos + 4 <= sl_input_len) {
            unsigned char *b = sl_input_data + sl_input_pos;
            pads[0].button  = (unsigned short) ((b[3] << 8) | b[2]);
            pads[0].stick_x = (signed char) b[1];
            pads[0].stick_y = (signed char) b[0];
        }
    } else if (sl_gfx_active()) {
        /* Live keyboard and mouse, and ONLY here: a recorded stream always
         * wins, and with no window sl_gfx_active() is 0, so headless replay
         * takes the branch above or stays neutral exactly as before.  That is
         * the whole determinism argument - there is no third producer.
         * (Tree-wide: osContGetReadData has one game-side caller, src/joy.c:476;
         *  src/libultrare/io/contreaddata.c is the N64 build's copy and is not
         *  linked natively.) */
        if (sl_live_latched) {
            pads[0].button  = sl_live_btn;
            pads[0].stick_x = sl_live_x;
            pads[0].stick_y = sl_live_y;
        } else {
            /* before the first retrace there is no latch yet */
            sl_input_live_get(&pads[0].button, &pads[0].stick_x, &pads[0].stick_y);
        }
        /* The second pad, for the 2.x styles. Present whenever live input is,
         * so that the game counts two controllers and offers those styles at
         * all; it reads neutral under every 1.x style. */
        if (sl_live_two_pads()) {
            pads[1].err = 0;
            if (sl_live_latched) {
                pads[1].button  = sl_live_btn2;
                pads[1].stick_x = sl_live_x2;
                pads[1].stick_y = sl_live_y2;
            } else {
                sl_input_live_get2(&pads[1].button, &pads[1].stick_x,
                                   &pads[1].stick_y);
            }
        }
    }
    /* Stick is printed too, and that is not cosmetic: "I cannot rotate" is
     * invisible in a button-only dump, because under control style 1.2 the
     * stick IS the look axis and the buttons stay at 0000 while you turn. */
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "poll %u record=%u button=%04x stick=(%d,%d)\n",
                sl_input_reads, sl_input_pos / 4, pads[0].button,
                (int) pads[0].stick_x, (int) pads[0].stick_y);
    sl_input_reads++;

    /* A mark raised by SIGUSR1 is written here, not in the handler - fopen and
     * fprintf are not async-signal-safe. This is also the only place that runs
     * whether or not there is a window, which is what makes it verifiable. */
    {
        extern int  sl_mark_pending_take(void);
        extern void sl_run_mark(unsigned);
        if (sl_mark_pending_take()) sl_run_mark((unsigned) sl_input_reads);
    }
}

/*
 * SL_INPUT_RECORD=<file> writes the stream SL_INPUT replays, in the SAME unit
 * replay consumes it: ONE WORD PER VI RETRACE, emitted here.
 *
 * It used to be written from the controller-read path, one word per read, and
 * that is the record/replay fidelity bug of 2026-08-26.  Replay advances the
 * stream once per retrace; live play polls from a scheduler running against
 * real time, so the number of reads inside one retrace varies with frame rate.
 * Recording per read therefore emitted a stream whose records did not
 * correspond one-to-one with the retraces replay would feed them to, and the
 * two slid apart as the run went on - faithful early, wrong later.  Measured
 * on the owner's marks run: replayed, Bond died at read ~12161 of 16547 at a
 * spot the owner walked through unharmed.
 *
 * Nothing about the FORMAT changed, so the 119 streams in tools/trace/inputs
 * remain valid as-is and need no conversion: they came from the libretro
 * frontend, which already advances once per VI.  This makes the native
 * recorder agree with them instead of disagreeing.
 *
 * Byte order matches the reader above - byte 0 stick y, byte 1 stick x,
 * byte 2 buttons lo, byte 3 buttons hi.
 */
static void sl_input_record_vi(void)
{
    static FILE *rec;
    static int   rec_tried;
    unsigned short btn;
    signed char x, y;
    unsigned char w[4];

    if (!rec_tried) {
        const char *path = getenv("SL_INPUT_RECORD");
        rec_tried = 1;
        if (path != NULL) {
            rec = fopen(path, "wb");
            if (rec == NULL)
                fprintf(stderr, "sightline native: SL_INPUT_RECORD open "
                                "failed: %s\n", path);
            else
                fprintf(stderr, "sightline native: recording input -> %s\n",
                        path);
        }
    }
    if (rec == NULL)
        return;

    /* whatever this retrace's reads will see: the stream record just advanced
     * to, or the pad just latched */
    if (sl_input_data != NULL) {
        if (sl_input_pos + 4 <= sl_input_len) {
            unsigned char *b = sl_input_data + sl_input_pos;
            btn = (unsigned short) ((b[3] << 8) | b[2]);
            x   = (signed char) b[1];
            y   = (signed char) b[0];
        } else {
            btn = 0; x = 0; y = 0;       /* neutral past end of stream */
        }
    } else {
        btn = sl_live_btn; x = sl_live_x; y = sl_live_y;
    }

    w[0] = (unsigned char) y;
    w[1] = (unsigned char) x;
    w[2] = (unsigned char) (btn & 0xff);
    w[3] = (unsigned char) ((btn >> 8) & 0xff);
    fwrite(w, 1, 4, rec);
    /* flushed every retrace: a crash or a hang must not lose the tail, which
     * is precisely the part worth replaying. */
    fflush(rec);
}

/*
 * SL_VIS_RECORD=<file>: the correspondence a live capture was missing.
 *
 * The input stream is indexed by VI retraces, but the SIMULATION advances per
 * game frame, so what a frame does depends on how many VIs elapsed inside it.
 * Replaying natively that ratio is rock steady - measured 2.000 VI per frame,
 * every frame - while a windowed session on a real display drops frames under
 * load and does not hold any fixed ratio.  Feed a stream recorded at a varying
 * ratio to a replay running at a fixed one and the input lines up early and
 * slides later, which is exactly the observed failure.
 *
 * SL_VIS already consumes this correspondence: one u32 BE per game frame, the
 * ABSOLUTE VI index at that frame's boundary.  It existed only for emulator
 * traces, extracted after the fact by nativediff.py --extract-ticks from
 * frame-counter jumps; nothing produced it for a native live session.  This
 * writes it at the frame boundary, where the number is known exactly rather
 * than reconstructed.
 *
 * This is a SIDECAR, so the input format does not change and the 119 streams
 * in tools/trace/inputs stay valid as-is, with their own .vis files where they
 * have them.  A stream with no .vis replays exactly as it does today.
 */
/*
 * ---- THE MOVEMENT SIDECAR (SL_MOVE / SL_MOVE_RECORD) ---------------------
 *
 * MEASURED 2026-09-08, and it is why this exists. The owner recorded two Dam
 * witnesses natively - `dam-seam-00.input`, 14180 bytes, and
 * `dam-truck-00.input`, 2248 bytes - and both replay with Bond STANDING STILL.
 * Decoded, `dam-seam-00.input` holds 3545 records with the stick at (0,0) in
 * EVERY ONE of them and no d-pad in any of them; the only buttons present are
 * Z, B and START. Replayed, the camera probe (SL_CAM_DBG) shows the player
 * frozen from frame 2401 to the end of the run. The files are not empty and
 * not corrupt - they are FAITHFUL RECORDINGS OF THE PAD, and the owner does
 * not play with the pad.
 *
 * THE DEFECT. Native keyboard and mouse do not drive the N64 stick. They
 * supply four movement channels and a linear look pair through
 * sl_move_channels_set / sl_mouse_look_set (src/native/sl_move_channels.c),
 * published by src/platform/sl_input.c and consumed at the seam in
 * bondviewProcessInput. sl_input_record_vi below writes sl_live_btn/x/y and
 * NOTHING ELSE, so every one of those six numbers was dropped on the floor.
 * Tree-wide, the only producer is sl_input.c:1931 and the only file writer was
 * the pad recorder: `grep -rn "sl_move_channels_set\|sl_mouse_look_set" src/`.
 * A session played with keyboard and mouse therefore recorded a stream that
 * was NON-EMPTY AND INERT - the worst shape a witness can have, because it
 * passes every "did it record?" check and reproduces nothing.
 *
 * THE FIX IS A SIDECAR, for the same reason .vis is one: the 4-byte-per-
 * retrace input format does not change, so the 119 streams in
 * tools/trace/inputs stay valid as they are, and a stream with no .move
 * replays EXACTLY as it does today - byte for byte, by construction, because
 * sl_move_data stays NULL and nothing below runs.
 *
 * THE UNIT IS THE RETRACE, matching the input stream record for record. That
 * is deliberate and it is the lesson of the 2026-08-26 fidelity bug: the
 * channels are published once per presented FRAME, and recording in frame
 * units would produce a sidecar whose records do not correspond one-to-one
 * with the retraces a replay feeds them to. Recorded here and consumed here,
 * one per call to sl_input_vi_advance, the two cannot slide apart.
 *
 * Record layout, 16 bytes, big-endian like every other stream here:
 *
 *     0  u8   flags   bit0 four channels active, bit1 linear look active
 *     1  s8   walk    + = forward          2  s8  strafe  + = right
 *     3  s8   turn    + = right            4  s8  pitch   + = down
 *     5..7    zero, reserved
 *     8  u32  yaw   IEEE754 float bits, degrees, + = right
 *    12  u32  pitch IEEE754 float bits, degrees, + = up
 *
 * The four fit in s8 because every producer and consumer of them works in the
 * game's own +/-70 unit (bondview2.c:5635 and friends divide by 70.0f). The
 * look pair is stored as raw float bits rather than a fixed-point unit so that
 * there is no scale to get wrong and nothing to document: what was published
 * is what is replayed.
 *
 * A WITHHELD FRAME IS DATA. flags bit0 clear replays as "the channels were not
 * active", which is what makes letting go of the keys, opening the watch and
 * a menu coming up replay as themselves instead of as the last active frame
 * repeated. That is why the recorder reads sl_move_channels_peek rather than
 * sl_move_channels_get.
 */
#define SL_MOVE_MAGIC   0x534C4D56u     /* "SLMV" */
#define SL_MOVE_RECSZ   16u

extern void sl_move_channels_set(int active, int walk, int strafe, int turn,
                                 int pitch);
extern void sl_move_channels_peek(int *active, int *walk, int *strafe,
                                  int *turn, int *pitch);
extern void sl_mouse_look_set(int active, float yaw_deg, float pitch_deg);
extern void sl_mouse_look_peek(int *active, float *yaw_deg, float *pitch_deg);

static unsigned char *sl_move_data;     /* replay: the loaded sidecar */
static unsigned       sl_move_len, sl_move_pos;
static FILE          *sl_move_rec;
static int            sl_move_rec_tried;

/**
 * Whether a recorded movement sidecar owns the channels this run.
 *
 * ONE definition, two callers, exactly as sl_live_input_active is: the live
 * poll in src/platform/sl_input.c must not publish over a replay. Without
 * this it would, and it would do so silently - `live` is 0 during any replay,
 * so the poll calls sl_move_channels_set(0, ...) every frame and would clear
 * the replayed values between retraces. A RECORDED STREAM ALWAYS WINS, which
 * is already the rule the pad follows in osContGetReadData above.
 */
int sl_move_replay_active(void)
{
    return sl_move_data != NULL;
}

void sl_move_init(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    u32 magic, recsz;

    if (f == NULL) {
        fprintf(stderr, "sightline native: SL_MOVE open failed: %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 8) {
        fprintf(stderr, "sightline native: SL_MOVE %s is %ld bytes - not a "
                        "movement sidecar\n", path, n);
        exit(2);
    }
    sl_move_data = malloc((size_t) n);
    if (sl_move_data == NULL || fread(sl_move_data, 1, (size_t) n, f) != (size_t) n) {
        fprintf(stderr, "sightline native: SL_MOVE %s short read\n", path);
        exit(2);
    }
    fclose(f);

    /* Validated rather than assumed. A sidecar handed to the wrong build, or
     * truncated by a crash mid-write, must say so here instead of replaying as
     * a stream of plausible-looking garbage - this file has a standing rule
     * that an instrument which cannot fail is not an instrument. */
    magic = ((u32) sl_move_data[0] << 24) | ((u32) sl_move_data[1] << 16)
          | ((u32) sl_move_data[2] << 8)  | sl_move_data[3];
    recsz = ((u32) sl_move_data[4] << 24) | ((u32) sl_move_data[5] << 16)
          | ((u32) sl_move_data[6] << 8)  | sl_move_data[7];
    if (magic != SL_MOVE_MAGIC || recsz != SL_MOVE_RECSZ) {
        fprintf(stderr, "sightline native: SL_MOVE %s has magic %08x recsz %u"
                        " - expected %08x / %u\n", path,
                (unsigned) magic, (unsigned) recsz,
                (unsigned) SL_MOVE_MAGIC, (unsigned) SL_MOVE_RECSZ);
        exit(2);
    }
    sl_move_data += 8;
    sl_move_len   = (unsigned) n - 8u;
    sl_move_pos   = 0;
    fprintf(stderr, "sightline native: movement sidecar %s (%lu retraces) - "
                    "keyboard/mouse movement is replayed\n",
            path, (unsigned long) (sl_move_len / SL_MOVE_RECSZ));
}

/* Publish the record for THIS retrace. Past the end of the sidecar the
 * channels go inactive rather than holding the last value, which is the same
 * rule the input stream follows (neutral after end of stream). */
static void sl_move_publish_vi(void)
{
    unsigned char *b;
    union { u32 u; float f; } yaw, pit;
    int flags;

    if (sl_move_data == NULL)
        return;
    if (sl_move_pos + SL_MOVE_RECSZ > sl_move_len) {
        sl_move_channels_set(0, 0, 0, 0, 0);
        sl_mouse_look_set(0, 0.0f, 0.0f);
        return;
    }
    b = sl_move_data + sl_move_pos;
    sl_move_pos += SL_MOVE_RECSZ;

    flags  = b[0];
    yaw.u  = ((u32) b[8]  << 24) | ((u32) b[9]  << 16)
           | ((u32) b[10] << 8)  | b[11];
    pit.u  = ((u32) b[12] << 24) | ((u32) b[13] << 16)
           | ((u32) b[14] << 8)  | b[15];

    sl_move_channels_set(flags & 1, (int) (signed char) b[1],
                         (int) (signed char) b[2], (int) (signed char) b[3],
                         (int) (signed char) b[4]);
    sl_mouse_look_set((flags >> 1) & 1, yaw.f, pit.f);
}

/* Write the record for THIS retrace, in the same unit and at the same point in
 * sl_input_vi_advance that sl_input_record_vi writes the pad, so record N of
 * the sidecar and record N of the .input stream describe one instant. */
static void sl_move_record_vi(void)
{
    unsigned char w[SL_MOVE_RECSZ];
    union { u32 u; float f; } yaw, pit;
    int active = 0, walk = 0, strafe = 0, turn = 0, pitch = 0, mlon = 0;
    float myaw = 0.0f, mpit = 0.0f;

    if (!sl_move_rec_tried) {
        const char *path = getenv("SL_MOVE_RECORD");
        sl_move_rec_tried = 1;
        if (path != NULL) {
            sl_move_rec = fopen(path, "wb");
            if (sl_move_rec == NULL) {
                fprintf(stderr, "sightline native: SL_MOVE_RECORD open "
                                "failed: %s\n", path);
            } else {
                unsigned char head[8];
                sl_w32be(head, SL_MOVE_MAGIC);
                sl_w32be(head + 4, SL_MOVE_RECSZ);
                fwrite(head, 1, 8, sl_move_rec);
                fprintf(stderr, "sightline native: recording keyboard/mouse "
                                "movement -> %s\n", path);
            }
        }
    }
    if (sl_move_rec == NULL)
        return;

    /* On a REPLAY this records what the sidecar just published, so that
     * re-recording a replay round-trips - the same property the pad recorder
     * above has, and the thing tools/trace/ roundtrip checking relies on. */
    sl_move_channels_peek(&active, &walk, &strafe, &turn, &pitch);
    sl_mouse_look_peek(&mlon, &myaw, &mpit);

    /* Clamped, not truncated. The four are the game's own +/-70 unit and every
     * producer already stays inside it, but a value that ever left the range
     * must not wrap sign in an s8 and replay as a hard turn the other way. */
    if (walk   >  127) walk   =  127;  if (walk   < -128) walk   = -128;
    if (strafe >  127) strafe =  127;  if (strafe < -128) strafe = -128;
    if (turn   >  127) turn   =  127;  if (turn   < -128) turn   = -128;
    if (pitch  >  127) pitch  =  127;  if (pitch  < -128) pitch  = -128;

    yaw.f = myaw;
    pit.f = mpit;

    memset(w, 0, sizeof w);
    w[0] = (unsigned char) ((active ? 1 : 0) | (mlon ? 2 : 0));
    w[1] = (unsigned char) (signed char) walk;
    w[2] = (unsigned char) (signed char) strafe;
    w[3] = (unsigned char) (signed char) turn;
    w[4] = (unsigned char) (signed char) pitch;
    sl_w32be(w + 8,  yaw.u);
    sl_w32be(w + 12, pit.u);
    fwrite(w, 1, SL_MOVE_RECSZ, sl_move_rec);
    /* flushed every retrace, for the same reason the pad stream is: a crash or
     * a hang must keep the tail, which is the part worth replaying. */
    fflush(sl_move_rec);
}

/* "SVI1": a VI-target stream whose phase was corrected when it was recorded.
 * Absent = a legacy or emulator-produced stream, handled exactly as before. */
#define SL_VIS_MAGIC 0x53564931u
static int sl_vis_stamped;
static FILE *sl_vis_rec;
static int   sl_vis_rec_tried;

void sl_vis_record_frame(void)
{
    unsigned char w[4];

    {   /* the frame boundary is the natural place to notice a state change,
         * and it runs whether or not this run is writing a .vis */
        extern void sl_watch_frame(unsigned);
        sl_watch_frame((unsigned) sl_vi_advances);
    }

    if (!sl_vis_rec_tried) {
        const char *path = getenv("SL_VIS_RECORD");
        sl_vis_rec_tried = 1;
        if (path != NULL) {
            sl_vis_rec = fopen(path, "wb");
            if (sl_vis_rec == NULL)
                fprintf(stderr, "sightline native: SL_VIS_RECORD open failed: "
                                "%s\n", path);
            else
                fprintf(stderr, "sightline native: recording vi/frame -> %s\n",
                        path);
        }
    }
    if (sl_vis_rec == NULL)
        return;

    /* STAMP AND PHASE. The count is written AFTER the pump's two advances for
     * this frame, while sl_ticks_next consumes the target BEFORE the game's
     * sample consume - one frame of phase error. Replaying an unstamped native
     * capture therefore needed SL_VIS_LAG=2, and at the default it diverged:
     * a ONE-UNIT bounding-box difference at frame 781 amplified into a
     * different room by frame 1801, which read as "replay is broken" for most
     * of a day.
     *
     * Correcting it in the RECORDER rather than the replayer means nobody has
     * to know a magic setting. The magic first word marks a stream as already
     * corrected; a file without it is left to behave exactly as before, which
     * is what keeps the 119 emulator traces from nativediff.py valid - those
     * are produced at a different phase and are correct at lag 0 already. */
    if (!sl_vis_stamped) {
        sl_vis_stamped = 1;
        sl_w32be(w, SL_VIS_MAGIC);
        fwrite(w, 1, 4, sl_vis_rec);
    }
    sl_w32be(w, sl_vi_advances > 2 ? sl_vi_advances - 2 : 0);
    fwrite(w, 1, 4, sl_vis_rec);
    fflush(sl_vis_rec);
}

void sl_input_vi_advance(void)
{
#ifdef SL_ALHEAP_TRACE
    sl_viadv_n++;
#endif
    if (sl_input_data != NULL) {
        if (sl_input_pos + 4 <= sl_input_len)
            sl_input_pos += 4;
    } else if (sl_gfx_active()) {
        /* latch the live pad once for this retrace */
        sl_input_live_get(&sl_live_btn, &sl_live_x, &sl_live_y);
        /* and the second, on the SAME retrace: the two pads must describe one
         * instant, or a diagonal input would arrive split across two frames. */
        sl_input_live_get2(&sl_live_btn2, &sl_live_x2, &sl_live_y2);
        sl_live_latched = 1;
    }
    sl_vi_advances++;
    /* The movement sidecar rides the SAME retrace as the pad, in this order:
     * publish first so a replayed record is in place before the recorder reads
     * it back, which is what makes re-recording a replay round-trip. */
    sl_move_publish_vi();
    sl_input_record_vi();
    sl_move_record_vi();
}

/* SL_RATE_DBG: the ratio the fidelity bug turns on.  Replay advances one
 * record per retrace; if live reads-per-retrace is not exactly 1, a per-read
 * recording cannot line up with it. */
void sl_input_rate_report(void)
{
    if (getenv("SL_RATE_DBG") == NULL)
        return;
    fprintf(stderr, "sl_rate: reads=%u vi_advances=%u ratio=%.6f\n",
            sl_input_reads, sl_vi_advances,
            sl_vi_advances ? (double) sl_input_reads / sl_vi_advances : 0.0);
}

s32 osContStartQuery(sl_OSMesgQueue *mq)    { if (mq) osSendMesg(mq, (void *) 0, 0); return 0; }
/*
 * Not a duplicate of osContInit's status block, and getting this wrong is
 * silent: joyCheckStatus (src/joy.c:203) calls osContInit only ONCE, guarded by
 * g_ContNeedsInit. Every later call takes the else branch and rebuilds
 * g_ConnectedControllers from the errno fields THIS function reports (:225-236),
 * and joyPoll re-runs it every 120 frames. Report one controller here and the
 * second pad would vanish two seconds after it appeared.
 */
void osContGetQuery(void *status)
{
    sl_OSContStatus *st = status;
    memset(st, 0, 4 * sizeof *st);
    st[0].type = 0x0005;
    st[1].err = st[2].err = st[3].err = 0x8;
    if (sl_live_two_pads()) {
        st[1].type = 0x0005;
        st[1].err  = 0;
    }
}

/* ---- EEPROM --------------------------------------------------------------
 * SL_EEPROM=<file> loads a save image.  Accepts either a bare EEPROM image
 * or the libretro save bundle the trace corpus stores (EEPROM occupies the
 * first 0x800 bytes).  Served as the 8-byte blocks of a 4Kbit part; writes
 * land in memory only (replays never persist).
 */
static unsigned char sl_eeprom[0x800];
static int sl_eeprom_loaded;

/* The cartridge save. Without one of these the game starts from defaults on
 * every launch, and nothing the player changes in the options can survive -
 * osEepromWrite lands in RAM and the process takes it to the grave. That is
 * what made the control style keep reverting to Honey.
 *
 * A trace replay names an existing image and MUST have it: a missing one there
 * means the run is not the run that was recorded, and failing loudly is right.
 * A player's save is the opposite case - the first launch legitimately has no
 * file yet - so SL_EEPROM_RW names a read/write save that is created on demand
 * and flushed at exit.
 */
static char sl_eeprom_path[512];
static int  sl_eeprom_writable;

/* The player's control style, remembered across runs - and since #41 the
 * Look Up/Down, Aim Control and Invert Mouse Y defaults with it.
 *
 * DIRECT BOOT never restores it, and that is measured rather than assumed: a
 * 1200-frame run that boots straight into a level performs ZERO osEepromRead
 * and ZERO osEepromWrite calls. The normal flow reaches
 * fileLoadSettingsForFolder (file2.c:1283) through file.c:68, which is what
 * applies the saved control type; SL_BOOT_LEVEL jumps past it entirely. So the
 * cartridge save is never read, never written, and initBONDdataforPlayer forces
 * CONTROLLER_CONFIG_HONEY on top (player.c:502) - whatever the player picks in
 * the options menu is gone the moment the process ends.
 *
 * Writing the game's own save format from here would mean reimplementing
 * Rare's checksums and block layout to persist one integer. The first answer
 * was a one-integer ".style" sidecar beside the save, read here and pushed in
 * through the game's own setter once a player existed (sl_style_load /
 * sl_style_save / sl_style_apply_once, 2026-09-01 .. 2026-09-17). #41
 * replaced that with the native settings store (src/platform/sl_settings.c,
 * %LOCALAPPDATA%\sightline\config.ini): the same integer plus the three
 * other control defaults, applied at every stage start by
 * src/native/sl_settings_apply.c through the SAME setters the per-folder
 * save load uses (init_watch_at_start_of_stage -> fileLoadSettingsForFolder,
 * then the native default on top). Rule 5 holds as before: nothing about the
 * game's behaviour changes, the player's own choice is simply still there.
 *
 * NATIVE DEFAULT: 1.2 SOLITAIRE, not 1.1 Honey. Owner decision, 2026-09-01,
 * carried into the store's table. This does not change Rare's default -
 * player.c:502 still forces Honey in initBONDdataforPlayer and is untouched;
 * it is the value the save-less native boot fills in. Why Solitaire is the
 * better cold start, and it is structural rather than taste: Honey never
 * assigns canNaturalPitch - it stays 0 from the reset at bondview2.c:4785 and
 * :5899 gates analog pitch on it - so a PAD under Honey has no free analog
 * pitch axis at all outside aim mode. Solitaire sets canNaturalPitch =
 * !insightaimmode (:5197) and keeps pitch analog.
 *
 * THE SIDECAR IS NOT READ ANY MORE (2026-09-20, #63): the control style left
 * the store with the N64 control styles themselves (the game side is pinned
 * to 1.1 Honey by sl_settings_apply.c; the native layer owns every mapping),
 * so there is nothing to import "<save>.style" into. The one-time import
 * #41 kept for it (sl_settings_import_legacy_style) is gone; a sidecar
 * beside a save is simply ignored. The Solitaire note above is history.
 *
 * Reached only from sl_eeprom_init_rw, i.e. only when SL_EEPROM_RW names a
 * writable save (or the demo's own save). Trace replay and native-health
 * never take that path, so neither sees a config: the store stays inactive
 * and the apply glue is a no-op, which is what keeps them bit-identical. */
static void sl_settings_start(const char *save_path)
{
    (void) save_path;
    sl_settings_init();
    /* Invert Mouse Y: config -> the one native state, as a SEED (not an
     * explicit set), so a later UI change wins over it for the run and
     * persists. The store is active from here on, so the developer override
     * SL_MOUSE_INVERT read at the first poll is ignored in this session
     * (read_env, sl_input.c): the config is the one authority. */
    sl_mouse_invert_y_seed(sl_settings_get(SL_SET_MOUSE_INVERT_Y));
    /* The binding registry (#46): defaults plus the store's bind. lines,
     * loaded here - before the first poll - so gameplay input begins on the
     * persisted bindings. A reload, not an init, so a default-only table an
     * earlier evaluation may have built cannot shadow the config. */
    {
        extern void sl_bindings_reload(void);
        sl_bindings_reload();
    }
}

/*
 * Report what the GAME concluded about controller count, once per change.
 *
 * The shim's intent and the game's belief are separate facts - joyCheckStatus
 * rebuilds g_ConnectedControllers from osContGetQuery every 120 frames, and
 * joyGetStickX returns 0 for a pad whose bit is clear - so presenting a second
 * pad is not the same as the game accepting one. This is what distinguishes
 * "the second controller is offered" from "the second controller is live", and
 * it is the only instrument that separates them.
 */
static void sl_cont_count_report(void)
{
    extern int sl_game_controller_count(void);
    static int last = -1;
    int n;

    if (getenv("SL_INPUT_DEBUG") == NULL)
        return;
    n = sl_game_controller_count();
    if (n == last)
        return;
    last = n;
    fprintf(stderr, "sightline input: game sees %d controller(s)%s\n", n,
            n >= 2 ? " - 2.x control styles are selectable" : "");
}

static void sl_eeprom_flush(void)
{
    FILE *f;

    if (!sl_eeprom_writable || sl_eeprom_path[0] == '\0')
        return;
    f = fopen(sl_eeprom_path, "wb");
    if (f == NULL) {
        fprintf(stderr, "sightline native: cannot write save %s\n",
                sl_eeprom_path);
        return;
    }
    fwrite(sl_eeprom, 1, sizeof sl_eeprom, f);
    fclose(f);
}

void sl_eeprom_init(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        fprintf(stderr, "sightline native: SL_EEPROM open failed: %s\n", path);
        exit(2);
    }
    fread(sl_eeprom, 1, sizeof sl_eeprom, f);
    fclose(f);
    sl_eeprom_loaded = 1;
    fprintf(stderr, "sightline native: eeprom image %s\n", path);
}

#ifdef SL_DEMO_BUILD
/* ---- THE DEMO SAVE ------------------------------------------------------
 *
 * The demo keeps its own cartridge save, and it is NOT the one the owner
 * plays. Two independent requirements meet here:
 *
 *   IT MUST NOT DEPEND ON THE ENVIRONMENT. A shipped demo has to behave the
 *   same however it is started, so the path is resolved in the binary rather
 *   than read from SL_EEPROM_RW. There is no demo env var to forget.
 *
 *   IT MUST NOT TOUCH THE NORMAL SAVE. tools/windows/play.ps1 sets
 *   SL_EEPROM_RW to %LOCALAPPDATA%\sightline\eeprom.bin on EVERY launch, so a
 *   demo build that merely "defaulted" when the variable was absent would
 *   still write the owner's real save the moment it was launched the ordinary
 *   way. The demo therefore IGNORES SL_EEPROM and SL_EEPROM_RW outright and
 *   forces its own file. Overriding beats defaulting because the guarantee is
 *   then a property of the build, not of how it happened to be launched.
 *
 * Same directory ladder the rest of Sightline already uses for player data
 * (LOCALAPPDATA, then USERPROFILE\AppData\Local, then TEMP, then the current
 * directory) - see sl_asset_override_dir() in src/native/sl_asset_override.c
 * and the $save block in play.ps1. Outside the repository in every case: rule
 * 2 keeps player and ROM-derived bytes out of the tree.
 *
 * Deleting the file resets the DEMO only, and the demo starting from defaults
 * is a legitimate first launch rather than an error.
 */
#ifdef _WIN32
/* _mkdir from the CRT rather than the Win32 directory call, and that choice is
 * MEASURED rather than stylistic. This file includes <windows.h> further down
 * (for the timing and console code), so a hand-written prototype up here is a
 * SECOND declaration of a function the real header also declares - with void*
 * where it uses LPSECURITY_ATTRIBUTES - which is a hard "conflicting types"
 * error, not a warning. <direct.h> is a small CRT header with no such overlap,
 * and _mkdir is all this needs. */
#include <direct.h>
#endif

/* Create each component of a directory path. Existing components fail
 * harmlessly (ERROR_ALREADY_EXISTS), which is why no return is inspected: the
 * only thing that matters is whether the save itself can be opened afterwards,
 * and sl_eeprom_init_rw already reports that. */
static void sl_demo_mkdir_p(char *dir)
{
#ifdef _WIN32
    char *q;

    for (q = dir; *q != '\0'; q++) {
        if ((*q == '\\' || *q == '/') && q != dir) {
            char saved = *q;
            *q = '\0';
            /* Skip the bare drive prefix ("C:"), which is not creatable. */
            if (!(q - dir == 2 && dir[1] == ':'))
                _mkdir(dir);
            *q = saved;
        }
    }
    _mkdir(dir);
#else
    (void) dir;
#endif
}

static const char *sl_demo_save_path(void)
{
    static char path[512];
    static char dir[480];
    static int  done;
    const char *base;

    if (done) return path;
    done = 1;

    base = getenv("LOCALAPPDATA");
    if (base == NULL || base[0] == '\0') {
        static char up[400];
        const char *u = getenv("USERPROFILE");
        if (u != NULL && u[0] != '\0'
            && strlen(u) + sizeof "\\AppData\\Local" < sizeof up) {
            strcpy(up, u);
            strcat(up, "\\AppData\\Local");
            base = up;
        }
    }
    if (base == NULL || base[0] == '\0') base = getenv("TEMP");
    if (base == NULL || base[0] == '\0') base = ".";

    if (strlen(base) + sizeof "\\sightline\\demo\\eeprom.bin" >= sizeof dir) {
        /* Nowhere sane to put it; fall back beside the process rather than
         * truncating a path and writing to a half-formed name. */
        strcpy(path, "sightline-demo-eeprom.bin");
        return path;
    }

    strcpy(dir, base);
    strcat(dir, "\\sightline\\demo");
    sl_demo_mkdir_p(dir);

    strcpy(path, dir);
    strcat(path, "\\eeprom.bin");
    return path;
}
#endif /* SL_DEMO_BUILD */

void sl_eeprom_init_rw(const char *path)
{
    FILE *f;
    size_t n = strlen(path);

    if (n >= sizeof sl_eeprom_path) {
        fprintf(stderr, "sightline native: save path too long\n");
        return;
    }
    memcpy(sl_eeprom_path, path, n + 1);
    sl_eeprom_writable = 1;

    f = fopen(path, "rb");
    if (f != NULL) {
        fread(sl_eeprom, 1, sizeof sl_eeprom, f);
        fclose(f);
        fprintf(stderr, "sightline native: save %s\n", path);
    } else {
        fprintf(stderr, "sightline native: new save %s\n", path);
    }
    sl_eeprom_loaded = 1;
    sl_settings_start(path);
    /* atexit rather than a call at each exit site: the pump leaves through
     * exit(0) from two places and the window-close path from a third. */
    atexit(sl_eeprom_flush);
}

s32 osEepromProbe(sl_OSMesgQueue *mq) { (void) mq; return 1; /* EEPROM_TYPE_4K */ }
s32 osEepromRead(sl_OSMesgQueue *mq, unsigned char block, unsigned char *buf)
{ (void) mq; memcpy(buf, sl_eeprom + block * 8, 8); return 0; }
s32 osEepromWrite(sl_OSMesgQueue *mq, unsigned char block, unsigned char *buf)
{ (void) mq; memcpy(sl_eeprom + block * 8, buf, 8); return 0; }
s32 osEepromLongRead(sl_OSMesgQueue *mq, unsigned char block, unsigned char *buf, s32 n)
{ (void) mq; memcpy(buf, sl_eeprom + block * 8, n); return 0; }
s32 osEepromLongWrite(sl_OSMesgQueue *mq, unsigned char block, unsigned char *buf, s32 n)
{ (void) mq; memcpy(sl_eeprom + block * 8, buf, n); return 0; }

/* ---- native trace output -------------------------------------------------
 * SL_TRACE_OUT=<file> arms per-tick state capture: the pump calls the
 * game-side reader (sl_state_capture) once per retrace delivery - the same
 * boundary the emulator harness samples at (state after the previous frame
 * was processed) - and the records land here.
 *
 * Record: u32 tick, u32 fc, 16B composite, u16 n, then n * (u16 key + 8B).
 * All big-endian, framed by the "SLNATIVE" magic; the Python side adapts.
 * SL_TRACE_SKIP (default 1) discards leading retraces to mirror the
 * emulator's discarded warm-up frame.
 */
static void sl_trace_open(const char *path)
{
    sl_trace_f = fopen(path, "wb");
    if (sl_trace_f == NULL) {
        fprintf(stderr, "sightline native: SL_TRACE_OUT open failed: %s\n", path);
        exit(2);
    }
    fwrite("SLNATIVE", 1, 8, sl_trace_f);
    fprintf(stderr, "sightline native: tracing to %s\n", path);
}

static void sl_w32be(unsigned char *p, u32 v)
{ p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

void sl_trace_tick(u32 tick, u32 fc, const unsigned char *composite16,
                   const unsigned short *keys, const unsigned char *hashes8,
                   int nents)
{
    unsigned char head[26];
    int i;

    sl_w32be(head, tick);
    sl_w32be(head + 4, fc);
    memcpy(head + 8, composite16, 16);
    head[24] = (unsigned char) (nents >> 8);
    head[25] = (unsigned char) nents;
    fwrite(head, 1, 26, sl_trace_f);
    for (i = 0; i < nents; i++) {
        unsigned char kb[2];
        kb[0] = (unsigned char) (keys[i] >> 8);
        kb[1] = (unsigned char) keys[i];
        fwrite(kb, 1, 2, sl_trace_f);
        fwrite(hashes8 + i * 8, 1, 8, sl_trace_f);
    }
}

/* ---- recorded frame timing ----------------------------------------------
 * SL_TICKS=<file>: one u16 (big-endian) per game frame - how many VIs that
 * frame took on the emulator, extracted from the trace's frame-counter
 * jumps.  The emulator's frame cadence is physical (emulated CPU time);
 * natively it is replay data, exactly like the input stream.  Default 1
 * when absent or exhausted.
 */
static unsigned char *sl_ticks_data;
static unsigned sl_ticks_len, sl_ticks_pos;

/* SL_VIS=<file>: one u32 (big-endian) per game frame - the ABSOLUTE trace
 * tick index at that frame's boundary.  The frame counter is a rounded
 * cycle division and drifts a few VIs either side of the true VI count;
 * the input stream is indexed by true VIs, so the poll head follows these
 * targets, not the counter deltas.  Extracted alongside SL_TICKS by
 * nativediff.py --extract-ticks. */
static unsigned char *sl_vis_data;
static unsigned sl_vis_len, sl_vis_pos;
static u32 sl_vi_polls;
static u32 sl_vis_lag;      /* SL_VIS_LAG: records held back per frame */
static u32 sl_vis_prev;     /* previous frame's target: its delta IS deltaFrames */
static u32 sl_vis_pending;  /* ...for the NEXT frame - see sl_ticks_next */

static int sl_vis_active(void) { return sl_vis_data != NULL; }

void sl_vis_init(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (f == NULL) {
        fprintf(stderr, "sightline native: SL_VIS open failed: %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    sl_vis_data = malloc(n);
    fread(sl_vis_data, 1, n, f);
    fclose(f);
    sl_vis_len = (unsigned) n;
    if (sl_vis_len >= 4
        && (((u32) sl_vis_data[0] << 24) | ((u32) sl_vis_data[1] << 16)
            | ((u32) sl_vis_data[2] << 8) | sl_vis_data[3]) == SL_VIS_MAGIC) {
        sl_vis_data += 4;                 /* phase already corrected at record */
        sl_vis_len  -= 4;
        fprintf(stderr, "sightline native: VI target stream %s (%lu frames, "
                        "phase-corrected)\n", path,
                (unsigned long) (sl_vis_len / 4));
    } else {
        fprintf(stderr, "sightline native: VI target stream %s (%lu frames)\n",
                path, (unsigned long) (sl_vis_len / 4));
    }
}

void sl_ticks_init(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (f == NULL) {
        fprintf(stderr, "sightline native: SL_TICKS open failed: %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    sl_ticks_data = malloc(n);
    fread(sl_ticks_data, 1, n, f);
    fclose(f);
    sl_ticks_len = (unsigned) n;
    fprintf(stderr, "sightline native: tick stream %s (%lu frames)\n", path, (unsigned long) (n / 2));
}

u32 sl_ticks_next(void)
{
    u32 t = 1;
    u32 target = 0;
    int have_target = 0;
    /* the boss pre-loop frame has no pump retrace behind it, leaving its
     * elapsed one VI short of the emulator's accounting; this runs inside
     * that frame, before the tick formula reads the clock */
    static int first_frame_done;
    if (!first_frame_done) {
        first_frame_done = 1;
        sl_count_extra += SL_CYCLES_PER_FRAME;
    }

    /* THE TARGET IS READ FIRST, because it carries TWO things and only one of
     * them used to be taken out of it: where the poll head must reach by the
     * end of this frame (below), and how many VIs this frame spans - which is
     * deltaFrames, the simulation's whole rate.
     *
     * MEASURED 2026-09-08, and it is why the hoist exists. Under a VI-target
     * stream the pump skips sl_vi_retrace_once entirely (see the
     * !sl_vis_active() guard in sl_pump), so sl_vi_elapsed is never
     * incremented and the else branch below returned its "no stream" fallback
     * of 1 on EVERY frame. osGetCount therefore advanced one VI per frame
     * whatever the recording did, updateFrameCounters got deltaFrames=1, and
     * currentFrameCounter - the clock every timer, animation and AI delay
     * derives from (frametiming.c:44-68) - ran at a rate the recording never
     * had. Replaying a native capture of Dam WITH its .vis diverged at exactly
     * the same camera sample as replaying it without one: the intro camera cut
     * at frame 345 in the recording and never cut at all in the replay.
     *
     * The delta is the honest source for it. sl_vis_record_frame writes the
     * ABSOLUTE VI index at each frame boundary, so target[i] - target[i-1] is
     * the VI count that frame spanned, by construction - the same number
     * sl_vi_elapsed would have held on the recording side. Measured on a
     * windowed re-record of the owner's Dam stream the deltas run 1..4
     * ({1:855, 2:641, 3:347, 4:155} over 2000 frames), so this is not a
     * constant being recovered.
     *
     * PRECEDENCE, and the corpus is untouched by it: an explicit SL_TICKS
     * stream still wins. Every emulator trace has one - nativediff.py
     * --extract-ticks writes the SL_TICKS file and the .vis together - so the
     * 119 streams in tools/trace/inputs take the branch they always took.
     * This fills in the case that had no answer: a stream with a .vis and no
     * .ticks, which is exactly what a NATIVE recording produces. */
    if (sl_vis_data != NULL && sl_vis_pos + 4 <= sl_vis_len) {
        target = ((u32) sl_vis_data[sl_vis_pos] << 24)
               | ((u32) sl_vis_data[sl_vis_pos + 1] << 16)
               | ((u32) sl_vis_data[sl_vis_pos + 2] << 8)
               |  (u32) sl_vis_data[sl_vis_pos + 3];
        sl_vis_pos += 4;
        have_target = 1;
    }

    if (sl_ticks_data != NULL && sl_ticks_pos + 2 <= sl_ticks_len) {
        t = ((u32) sl_ticks_data[sl_ticks_pos] << 8) | sl_ticks_data[sl_ticks_pos + 1];
        sl_ticks_pos += 2;
        if (t == 0) t = 1;
    } else if (have_target) {
        /* HELD BACK ONE FRAME, and that is not a fudge - it is where the two
         * numbers are written.
         *
         * sl_ticks_next runs at the TOP of a frame; sl_vis_record_frame runs at
         * that frame's BOUNDARY, after every VI the frame spanned. So the
         * target stored at index i is the VI count at the END of frame i,
         * which means target[i] - target[i-1] is the span of frame i as seen
         * from its end - and the value this function must return at the top of
         * frame i is the span of the frame that just finished.
         *
         * MEASURED with SL_TICK_DBG on a windowed re-record and its replay:
         * the recorder's own sequence opens t = 1,1,1,1,3,3,3,3,3,4,3,4 while
         * the undelayed delta produced 1,1,1,3,3,3,3,3,4,3,4,3 - the same
         * sequence one frame early. On the camera trace that read as a replay
         * that matched the recording exactly and led it by one sample forever,
         * which is a phase error wearing a divergence's clothes.
         *
         * The zero clamp covers the recorder's own phase correction, which
         * subtracts two from the opening boundaries and floors them at zero;
         * held back, those floors land on the frames whose true span was 1
         * anyway, so the reconstructed sequence matches the recorded one from
         * frame 0. */
        t = sl_vis_pending ? sl_vis_pending : 1;
        sl_vis_pending = target > sl_vis_prev ? target - sl_vis_prev : 0;
        sl_vis_prev = target;
    } else {
        /* NO RECORDED VI STREAM: the elapsed VIs are the ones that actually
         * happened, not a constant.
         *
         * This used to be a hardcoded 1, which made the deterministic clock
         * advance ONE VI per game frame however many retraces the frame
         * spanned. Measured against the cartridge over 600 VIs of steady
         * gameplay, anchored 120 VIs after mission_state reaches 1:
         *
         *   cartridge  currentFrameCounter +1.00 per VI   (deltaFrames
         *              histogram {1:108, 2:224, ...} - mostly two VIs a frame)
         *   native     currentFrameCounter +0.50 per VI   (600 over 1200
         *              retraces, exactly one per game frame)
         *
         * currentFrameCounter is the canonical simulation clock - every timer,
         * animation and AI delay derives from it (frametiming.c:44-68) - so at
         * half rate the whole simulation ran at half speed. The constant was
         * masked while the retrace itself ran at ~112 Hz, because two errors of
         * opposite sign roughly cancelled; pacing the retrace correctly
         * exposed it.
         *
         * Taking the real count reproduces the hardware definition rather than
         * approximating it, and it leaves the retrace rate and the audio
         * manager's cadence untouched - both are driven by the retrace, which
         * this does not move. */
        t = sl_vi_elapsed ? sl_vi_elapsed : 1;
    }
    sl_vi_elapsed = 0;
    /* per frame the clock has already moved twice - the pump's retrace
     * delivery and sl_frame_advance - so contribute t minus those two;
     * unsigned wrap handles the t==1 case (a net -1 VI) exactly */
    sl_count_extra += (t - 2) * SL_CYCLES_PER_FRAME;
    /* the scheduler ran joyPoll on every VI.  Poll (advance the stream,
     * then read) until the head reaches this frame's absolute VI target -
     * this runs before joyConsumeSamples in the frame, so the game's
     * consume window covers exactly the VIs the emulator's did, load VIs
     * and counter-rounding drift included.  The 20-sample ring wraps
     * exactly as on hardware. */
    if (have_target) {
        extern void joyPoll(void);
        /* On hardware the scheduler polls throughout the frame, so some of a
         * frame's VIs land AFTER the game's joyConsumeSamples - the game's
         * curlast therefore trails the newest polled sample.  The pump does
         * the whole batch at the frame boundary, before the consume, which
         * puts the game one frame ahead on analog stick values (measured:
         * emulator curlast=record 694 where native had 696 at fc 715).
         * SL_VIS_LAG holds the batch back by that many records. */
        if (target > sl_vis_lag)
            target -= sl_vis_lag;
        else
            target = 0;
        /* read-then-advance: VI i's poll latches record i, so after N
         * polls the last record seen is N-1 - the emulator's consume at
         * a frame whose counter first appears at sample i_k has seen
         * records through i_k - 1 exactly */
        while (sl_vi_polls < target) {
            joyPoll();
            sl_input_vi_advance();
            sl_vi_polls++;
        }
    }
    /* SL_TICK_DBG: the frame's simulation rate, beside the VI accounting it
     * comes from. deltaFrames IS the simulation clock (frametiming.c:44), so
     * "record and replay disagree" is answerable here in one diff rather than
     * inferred from where a camera ended up N frames later. */
    {
        static int dbg = -1;
        if (dbg < 0) { const char *e = getenv("SL_TICK_DBG");
                       dbg = (e != NULL && *e != '\0' && *e != '0'); }
        if (dbg)
            fprintf(stderr, "sl_tick: f%u t=%u vi=%u target=%u\n",
                    (unsigned) sl_frame, (unsigned) t,
                    (unsigned) sl_vi_advances,
                    have_target ? (unsigned) target : 0u);
    }
    return t;
}

/* detail dump support for the state reader */
int sl_detail_active;
void sl_detail_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* ---- boot parameter env helpers (game-side native branches call these) -- */
long sl_env_s32(const char *name, long dflt)
{
    const char *v = getenv(name);
    return v ? strtol(v, NULL, 0) : dflt;
}

float sl_env_f32(const char *name, float dflt)
{
    const char *v = getenv(name);
    return v ? (float) atof(v) : dflt;
}
s32 osPfsInit(sl_OSMesgQueue *mq, void *pfs, s32 channel) { (void) mq; (void) pfs; (void) channel; return -1; }

/* data the OS layer references */
u64 osClockRate = 62500000;
s32 osTvType = 1;                              /* NTSC */
char osViModeTable[56 * 32];                   /* opaque mode blobs, never read natively */

/* entry configuration */
void sl_shim_configure(void)
{
    const char *n = getenv("SL_FRAMES");
    const char *v;
    {
        extern void sl_input_rate_report(void);
        atexit(sl_input_rate_report);
    }

    if (n) {
        sl_frame_limit = (u32) atoi(n);
        sl_frame_limit_set = 1;
    }
    if ((v = getenv("SL_FPS")) != NULL)
        sl_fps = atof(v);

    if ((v = getenv("SL_INPUT")) != NULL)
        sl_input_init(v);

    /* THE MOVEMENT SIDECAR, found the way .vis is meant to be: beside the
     * stream it belongs to. A live native session records `<run>/input` and
     * `<run>/input.move` together, so naming the one must not require naming
     * the other - a witness that needs two environment variables set correctly
     * is a witness that will arrive with one of them missing.
     *
     * An explicit SL_MOVE always wins, and a MISSING <input>.move is silent
     * and inert: streams recorded before this existed, and all 119 emulator
     * streams in tools/trace/inputs, replay byte-identically because
     * sl_move_data stays NULL. Only an explicitly named SL_MOVE that cannot be
     * opened is fatal, because that one was asked for. */
    if ((v = getenv("SL_MOVE")) != NULL) {
        sl_move_init(v);
    } else if ((v = getenv("SL_INPUT")) != NULL) {
        char side[512];
        FILE *probe;
        size_t vl = strlen(v);
        if (vl + 6 < sizeof side) {
            memcpy(side, v, vl);
            memcpy(side + vl, ".move", 6);
            probe = fopen(side, "rb");
            if (probe != NULL) { fclose(probe); sl_move_init(side); }
        }
    }
#ifdef SL_DEMO_BUILD
    /* DEMO POLICY: the save is the build's, not the environment's. See
     * sl_demo_save_path above for why this OVERRIDES rather than defaults -
     * play.ps1 always sets SL_EEPROM_RW to the owner's real save, so
     * defaulting would still have written it. */
    (void) v;
    sl_eeprom_init_rw(sl_demo_save_path());
#else
    if ((v = getenv("SL_EEPROM")) != NULL)
        sl_eeprom_init(v);
    else if ((v = getenv("SL_EEPROM_RW")) != NULL)
        sl_eeprom_init_rw(v);
#endif

    /* runtime equivalents of the SL_DIRECT_BOOT_* build-time initialisers:
     * poked before mainproc runs, so the value is in place before the boot
     * check reads it - the same no-window property the initialisers have */
    if ((v = getenv("SL_TRACE_OUT")) != NULL)
        sl_trace_open(v);
    sl_trace_skip = 1;
    if ((v = getenv("SL_TICKS")) != NULL)
        sl_ticks_init(v);
    /* THE VI-TARGET SIDECAR, found beside the stream exactly as .move is.
     *
     * The comment on the .move discovery above says it is "found the way .vis
     * is meant to be" - and .vis discovery did not exist. Tree-wide search for
     * a `.vis` suffix over src/ and tools/windows/ on 2026-09-08 came back with
     * this file's SL_VIS_RECORD block and nothing that looks one up: every
     * caller had to name SL_VIS by hand, and the two that do (tools/native/
     * fogflicker.sh, frozenframe.sh) point at a run directory. So a stream
     * copied out of a run and handed on arrived with its .move but not its
     * .vis, and replayed at whatever VI-per-frame cadence the replay happened
     * to run at instead of the one it was recorded at. Measured on the owner's
     * Dam sessions: recorded 1.22 VI/frame, headless replay 1.00, windowed
     * replay 1.90. Nothing in the recording survives that.
     *
     * Same three rules as .move: an explicit SL_VIS wins, a missing
     * <input>.vis is silent and inert - sl_vis_data stays NULL and the 119
     * emulator streams in tools/trace/inputs replay byte-identically, none of
     * them having a .vis beside it - and only a named SL_VIS that cannot be
     * opened is fatal, because that one was asked for. */
    if ((v = getenv("SL_VIS")) != NULL) {
        sl_vis_init(v);
    } else if ((v = getenv("SL_INPUT")) != NULL) {
        char side[512];
        FILE *probe;
        size_t vl = strlen(v);
        if (vl + 5 < sizeof side) {
            memcpy(side, v, vl);
            memcpy(side + vl, ".vis", 5);
            probe = fopen(side, "rb");
            if (probe != NULL) { fclose(probe); sl_vis_init(side); }
        }
    }
    if ((v = getenv("SL_VIS_LAG")) != NULL)
        sl_vis_lag = (u32) atoi(v);
    if ((v = getenv("SL_BOOT_ELAPSED_FRAMES")) != NULL)
        sl_boot_elapsed = (u32) atoi(v);
    if ((v = getenv("SL_TRACE_SKIP")) != NULL)
        sl_trace_skip = (u32) atoi(v);

    if ((v = getenv("SL_BOOT_LEVEL")) != NULL) {
        extern s32 g_StageNum;
        g_StageNum = (s32) strtol(v, NULL, 0);
        fprintf(stderr, "sightline native: direct boot level %s\n", v);
    }
    if ((v = getenv("SL_BOOT_DIFFICULTY")) != NULL) {
        extern s32 g_SelectedDifficulty;
        g_SelectedDifficulty = (s32) strtol(v, NULL, 0);
    }

    /* Registered unconditionally, so a run where the audio path was never
     * reached at all reports "no buffers were ever submitted" rather than
     * printing nothing and being mistaken for success. */
    {
        /* MEASURED 2026-09-02: this was gated behind SL_AUDIO_STATS while the
         * comment above claimed it was unconditional, so the silence detector
         * in sl_audio_report - including the explicit "OUTPUT IS DIGITAL
         * SILENCE" line - never fired on an ordinary run. That is exactly the
         * outcome the comment was written to prevent, and it is why a Windows
         * build that produces no audible sound could still be reported as
         * "audio passes on mechanism": device open, submits counted, and the
         * one check that would have contradicted it switched off. */
        void sl_audio_report(void);
        atexit(sl_audio_report);
    }
}

/* ---- crash handler: MIPS-specific by nature; natively we have gdb -------- */
void crashInit(void)                       { }
void crashAppendChar(char c)               { (void) c; }
void *crashRenderFrame(void *gdl)          { return gdl; }

/* ---- the game heap ------------------------------------------------------
 * On the N64 the pool spans _bssSegmentEnd to a TLB block computed from the
 * boot stack - memory-map arithmetic with no native meaning (and the source
 * of the first native crash: a 4KB stub carved up as megabytes). Natively
 * the heap is an explicit allocation handed to the one caller.
 */
#ifdef _WIN32
/* Win32 has no mmap. The three fixed-address reservations below become
 * VirtualAlloc; see the notes at each site for what actually differs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#define SL_HEAP_BYTES (16u * 1024u * 1024u)

void *sl_native_heap(u32 *size)
{
    static void *heap;
    if (!heap) {
#ifdef _WIN32
        /* MAP_32BIT has no analogue and needs none: a 32-bit Win32 process
         * has no user address above 4GB to hand out in the first place.
         *
         * THE BASE IS LOAD-BEARING, and asking for NULL was B-066.
         *
         * A display-list operand is either a SEGMENTED address - segment
         * index in bits 24-27, 24-bit offset below - or a raw host pointer,
         * and the two are told apart by (a & 0xf0000000) == 0 meaning
         * "segmented" (sl_gfx_dl.c dl_operand). That test is only sound while
         * no host pointer has a zero top nibble, i.e. while the heap lives at
         * or above 0x10000000. Linux held that invariant by accident of its
         * allocator; VirtualAlloc(NULL) on this Windows host returned
         * 0x0f200000, INSIDE the segmented range, and the property silently
         * stopped being true.
         *
         * MEASURED consequence: the game hands gSPDisplayList raw heap
         * pointers like 0f222eb0, dl_operand read that as segment 15 + offset
         * 0x222eb0, resolved it against g_seg[15]=0f331070 to 0f553f20 - a
         * different, readable, ZERO-FILLED address in the same arena - and
         * the walker then ground through 100000 G_SPNOOPs to its guard limit.
         * Six of those per frame is 600000 of the 602042 commands walked, and
         * is why windowed Facility ran at 1-2 fps.
         *
         * So the heap is placed explicitly above the segmented range. The
         * fallback chain matters more than the specific value; if every
         * preferred base is unavailable we still start, but we say so, because
         * the ambiguity comes back and silence is what cost this the first
         * time.
         */
        {
            static const unsigned long prefer[] = {
                0x20000000ul, 0x30000000ul, 0x40000000ul, 0x10000000ul
            };
            unsigned i;
            for (i = 0; i < sizeof prefer / sizeof prefer[0] && heap == NULL; i++)
                heap = VirtualAlloc((void *) prefer[i], SL_HEAP_BYTES,
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (heap == NULL)
                heap = VirtualAlloc(NULL, SL_HEAP_BYTES,
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        }
        if (heap == NULL) {
            fprintf(stderr, "sl_native_heap: VirtualAlloc(%u) failed, err %lu\n",
                    SL_HEAP_BYTES, (unsigned long) GetLastError());
            exit(6);
        }
        if ((unsigned long) heap < 0x10000000ul)
            fprintf(stderr,
                    "*** sightline: game heap at %p is INSIDE the segmented-address"
                    " range.\n*** Display-list operands are ambiguous there and geometry"
                    " will be lost (B-066).\n", heap);
        fprintf(stderr, "sightline native: game heap %u MB at %p\n",
                SL_HEAP_BYTES >> 20, heap);
#else
        heap = mmap(0, SL_HEAP_BYTES, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (heap == MAP_FAILED) { perror("sl_native_heap"); exit(6); }
#endif
        SL_LOG("game heap: %u MB at %p\n", SL_HEAP_BYTES >> 20, heap);
    }
    *size = SL_HEAP_BYTES;
    return heap;
}

/* ---- the cartridge pager -------------------------------------------------
 * GoldenEye demand-pages assets: pointers baked in ROM data target the
 * kseg2 TLB window (0xC0xxxxxx); touching an unmapped page faults, and the
 * exception handler loads rom[_gameSegmentRomStart + (vaddr & 0xFFE000)] in
 * 8KB pages (tlb_manage.c:264-266). Natively: the window is PROT_NONE and
 * the SIGSEGV pager performs the same decode against the ROM file.
 */
#define SL_TLB_WINDOW_BASE 0xC0000000u
#define SL_TLB_WINDOW_SIZE 0x01000000u
#define SL_TLB_PAGE        0x2000u

extern char _gameSegmentRomStart;   /* absolute: the segment's ROM offset */

void sl_pager_init(void)
{
#ifdef _WIN32
    /* MEASURED on Windows 11 x64, 32-bit process: without
     * IMAGE_FILE_LARGE_ADDRESS_AWARE the user VA ceiling is 0x7ffeffff and
     * this reservation fails with ERROR_INVALID_ADDRESS (487) - 0xC0000000 is
     * simply not addressable. Linked with -Wl,--large-address-aware the
     * ceiling becomes 0xfffeffff under WOW64 and the request returns the
     * requested base exactly. The check below is therefore load-bearing: it
     * is what catches a build that lost the link flag. */
    void *w = VirtualAlloc((void *) SL_TLB_WINDOW_BASE, SL_TLB_WINDOW_SIZE,
                           MEM_RESERVE, PAGE_NOACCESS);
    if (w != (void *) SL_TLB_WINDOW_BASE) {
        fprintf(stderr,
                "sl_pager_init: kseg2 window at %#x unavailable (got %p, "
                "err %lu).\n  A 32-bit Windows process reaches 0xC0000000 "
                "only when linked\n  --large-address-aware.\n",
                SL_TLB_WINDOW_BASE, w, (unsigned long) GetLastError());
        exit(7);
    }
#else
    void *w = mmap((void *) SL_TLB_WINDOW_BASE, SL_TLB_WINDOW_SIZE,
                   PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (w != (void *) SL_TLB_WINDOW_BASE) {
        perror("sl_pager_init: kseg2 window");
        exit(7);
    }
#endif
    SL_LOG("pager: kseg2 window at %p (%u MB)\n", w, SL_TLB_WINDOW_SIZE >> 20);
}

/* Is `addr` inside the window the pager above serves? The display-list
 * walker's readability probe (src/gfx/sl_gfx_dl.c mem_readable) needs this
 * fact and nothing else from here: INSIDE the window a reserved, not-yet-
 * committed page is readable, because the fault handler commits it and
 * fills it from the ROM; OUTSIDE the window a reserved page is a fault the
 * process does not survive. B-139. */
int sl_pager_covers(unsigned long addr)
{
    return addr >= SL_TLB_WINDOW_BASE && addr < SL_TLB_WINDOW_BASE + SL_TLB_WINDOW_SIZE;
}

/* returns 1 if the fault was ours and was paged in */
int sl_pager_fault(unsigned long addr)
{
    unsigned long page;
    long rom_off;
    if (addr < SL_TLB_WINDOW_BASE || addr >= SL_TLB_WINDOW_BASE + SL_TLB_WINDOW_SIZE)
        return 0;
    if (!sl_rom) sl_rom_load();
    page = addr & ~(unsigned long)(SL_TLB_PAGE - 1);
    rom_off = (long)(unsigned long) &_gameSegmentRomStart + (long)(addr & 0xFFE000u);
#ifdef _WIN32
    /* Commit inside the reservation. VirtualAlloc(MEM_COMMIT) on an already
     * committed page is a documented no-op, which matches mprotect's
     * idempotence here. */
    if (VirtualAlloc((void *) page, SL_TLB_PAGE, MEM_COMMIT, PAGE_READWRITE) == NULL)
        return 0;
#else
    if (mprotect((void *) page, SL_TLB_PAGE, PROT_READ | PROT_WRITE) != 0)
        return 0;
#endif
    if (rom_off >= 0 && rom_off < sl_rom_size) {
        long n = sl_rom_size - rom_off;
        if (n > (long) SL_TLB_PAGE) n = SL_TLB_PAGE;
        memcpy((void *) page, sl_rom + rom_off, n);
    }
    SL_LOG("pager: 0x%lx <- rom+0x%lx\n", page, rom_off);
    return 1;
}

/* ---- big-endian header repair, layer 2 of T4 -----------------------------
 * DMA delivers ROM bytes verbatim; multi-byte header fields are big-endian.
 * The animation relocator (initanitable.c) is the single choke point where
 * every ModelAnimation header is touched once - it LE-read BE offsets, added
 * the heap base, and wrote back garbage. Swapping the header there, before
 * relocation, makes the native representation match what the code expects.
 * A seen-set guards against double swap (entries can appear in both tables
 * and arrive again through the modelSetAnimation funnel).  Keyed as a
 * bitmap over the permanent animation table (one bit per 0x40 header slot);
 * the old bounded array silently re-swapped entries past its cap, toggling
 * headers back to big-endian.
 */
static unsigned char *sl_anim_bitmap;
static unsigned sl_anim_base, sl_anim_limit;

void sl_anim_swap_region(void *base, unsigned size)
{
    sl_anim_base = (unsigned) base;
    sl_anim_limit = sl_anim_base + size;
    sl_anim_bitmap = calloc(size / 4 / 8 + 1, 1);   /* headers sit at arbitrary 4-byte offsets */
}

static u32 sl_bswap32(u32 v) { return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v & 0xFF00u) << 8) | (v << 24); }
static unsigned short sl_bswap16(unsigned short v) { return (unsigned short)((v >> 8) | (v << 8)); }

void sl_anim_header_swap(void *anim_)
{
    unsigned char *a = (unsigned char *) anim_;
    u32 *w; unsigned short *h; unsigned i;
    unsigned addr = (unsigned) anim_;
    unsigned bit;
    if (sl_anim_bitmap == NULL || addr < sl_anim_base || addr >= sl_anim_limit) {
        fprintf(stderr, "sightline native: anim header outside table @%p\n", anim_);
        exit(2);
    }
    bit = (addr - sl_anim_base) / 4;
    if (sl_anim_bitmap[bit >> 3] & (1u << (bit & 7)))
        return;
    sl_anim_bitmap[bit >> 3] |= (unsigned char) (1u << (bit & 7));
    /* The wire header is 0x14 bytes: s32 address@0; u16 frames@4; u8 u8;
     * bitDescriptors@8; u16@C; u16@E; bitStream@10.  The struct's
     * unk14..unk3C are never read from table entries (grep: only
     * unk04/06/07/0C/0E are consumed), and entries pack tighter than the
     * struct - sweeping 0x14..0x2C word-swapped the DESCRIPTOR blobs of
     * adjacent entries, which the byte-positional descriptor reader then
     * misparsed (Bond's body anim decoded root y=-915 instead of ~1087). */
    w = (u32 *)(a + 0x00); *w = sl_bswap32(*w);
    h = (unsigned short *)(a + 0x04); *h = sl_bswap16(*h);
    w = (u32 *)(a + 0x08); *w = sl_bswap32(*w);
    h = (unsigned short *)(a + 0x0C); *h = sl_bswap16(*h);
    h = (unsigned short *)(a + 0x0E); *h = sl_bswap16(*h);
    w = (u32 *)(a + 0x10); *w = sl_bswap32(*w);
    (void) i;
}

/* The host-clock boot seed.
 *
 * SOURCE, exactly: clock_gettime(CLOCK_REALTIME) folded to 32 bits as
 * tv_sec * 1000000000 + tv_nsec. Measured on this host rather than assumed -
 * clock_getres reports 1ns, and the smallest tick actually observed between
 * two successive reads is 10ns - so launches, which are hundreds of
 * milliseconds apart at the very best, land on uncorrelated values.
 *
 * Deliberately NOT an OS entropy API. We are reproducing a cartridge's boot
 * nondeterminism, not generating a secret; getrandom(2) and its equivalents
 * would buy portability work for no behavioural gain.
 *
 * It lives here, in src/platform, because it is a HOST fact. src/game must not
 * learn host APIs - that layering is what keeps the AI track shippable and the
 * server build headless (docs/project-rules.md, "Layering goal").
 *
 * osGetCount() ITSELF IS LEFT DETERMINISTIC. It is useful platform timing with
 * real other callers - frametiming.c, sched.c, speed_graph.c, usb.c - and
 * making the clock lie in order to fix the RNG would be a far larger change
 * than the defect warrants, with a much wider blast radius. Only the value
 * arriving at the game's seed call changes. */
static u32 sl_host_boot_seed(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (u32) ((unsigned long long) ts.tv_sec * 1000000000ull +
                  (unsigned long long) ts.tv_nsec);
}

/* WHO OWNS THE GAME RNG'S BOOT STATE.
 *
 * boss.c:389 does randomSetSeed(osGetCount()) - on the cartridge that is the
 * CPU cycle counter at boss start, so the whole chain a run gets is BOOT
 * TIMING. Real entropy, different every power-on. Every draw from the level
 * load onward hangs off it: the intro camera pick, the spawn look angle, AI
 * reaction timers.
 *
 * Natively, osGetCount() is the shim's DETERMINISTIC frame clock, so that
 * call is not entropy at all. Measured, facility, this build: osGetCount()
 * returns 1000000 at seed time on every launch (sl_frame is still 0), giving
 * g_randomSeed = 00000000000f4241 every time. That is why native replay has
 * been reproducible - not by design, but because an accident upstream of it
 * happened to be constant. This function is where that stops being an
 * accident and becomes a decision.
 *
 * The state, not the seed argument. randomSetSeed(x) stores (s64)(s32)x + 1,
 * so the argument is not recoverable from the state in general and the state
 * is what the next randomGetNext() consumes. SL_RNG_SEED is therefore the
 * 64-bit STATE, in hex - what to be holding, not what to be given.
 *
 * PRECEDENCE:
 *   1. SL_RNG_SEED set        -> use it exactly. Replay, and explicit tests.
 *   2. a replay (SL_INPUT) with no recorded seed -> leave boss.c's
 *                                deterministic seed alone. This is what keeps
 *                                recordings made before the seed was recorded
 *                                replaying as they always did.
 *   3. an ordinary launch     -> a host-clock seed, so boot varies the way the
 *                                cartridge's does.
 *
 * COMPATIBILITY WITH RECORDINGS THAT PREDATE THIS. A run directory captured
 * before the seed was recorded has no SL_RNG_SEED line in its env sidecar, and
 * nothing can reconstruct one - the state is not derivable from the frame
 * count or the input stream. Such a recording replays on rule 2, which is the
 * seed it was recorded on. The existing corpus therefore keeps replaying
 * exactly as before; it is not invalidated, and it is not silently
 * re-interpreted either. Rule 2 is why rule 3 tests SL_INPUT rather than
 * simply randomising whenever no seed was given: a replay must never be
 * handed fresh entropy.
 *
 * The state is announced on stderr on EVERY native launch, not only when it
 * was forced. A run whose seed is not written down is a run that cannot be
 * asked about afterwards, and that is the whole failure this exists to end. */
/**
 * SEED= out of a stream's .spec sidecar, or NULL.
 *
 * Two shapes, because the repository has two: a captured run writes
 * `<run>/input.spec` beside `<run>/input`, and tools/trace/inputs holds
 * `<name>.spec` beside `<name>.input`. Both are tried, appended form first.
 *
 * Parsed by KEY, not by position - the existing sidecars are lines of
 * `LEVEL=... DIFFICULTY=... TOUGH=...` and a SEED= may sit anywhere in them,
 * so every file already in the tree stays valid and simply has no SEED= to
 * find. Nothing here fails the run: a missing or malformed sidecar returns
 * NULL and the caller falls through to the behaviour it always had.
 */
static const char *sl_seed_from_spec(const char *stream, char *out, size_t outsz)
{
    char path[600];
    char line[512];
    FILE *f = NULL;
    size_t n;
    int attempt;

    if (stream == NULL) return NULL;
    n = strlen(stream);
    if (n + 6 >= sizeof path) return NULL;

    for (attempt = 0; attempt < 2 && f == NULL; attempt++) {
        if (attempt == 0) {
            memcpy(path, stream, n);
            memcpy(path + n, ".spec", 6);
        } else {
            /* strip a trailing ".input" and try <base>.spec */
            if (n < 6 || strcmp(stream + n - 6, ".input") != 0) return NULL;
            memcpy(path, stream, n - 6);
            memcpy(path + n - 6, ".spec", 6);
        }
        f = fopen(path, "rb");
    }
    if (f == NULL) return NULL;

    while (fgets(line, sizeof line, f) != NULL) {
        char *k = line;
        while (*k != '\0') {
            if ((k == line || k[-1] == ' ' || k[-1] == '\t')
                && strncmp(k, "SEED=", 5) == 0) {
                size_t i = 0;
                k += 5;
                while (i + 1 < outsz && isxdigit((unsigned char) k[i])) {
                    out[i] = k[i];
                    i++;
                }
                out[i] = '\0';
                fclose(f);
                if (i == 0) return NULL;
                fprintf(stderr, "sightline native: seed %s from %s\n",
                        out, path);
                return out;
            }
            k++;
        }
    }
    fclose(f);
    return NULL;
}

void sl_rng_seed_override(void)
{
    extern u64 g_randomSeed;
    extern void randomSetSeed(u32);
    extern void sl_run_note_rng_seed(unsigned long long);
    const char *v = getenv("SL_RNG_SEED");
    const char *how = NULL;

    static char specbuf[32];

    if (v == NULL && getenv("SL_INPUT") != NULL) {
        /* THE SEED TRAVELS WITH THE STREAM. A stream is not replayable without
         * the RNG state it was recorded under - boss.c seeds at boss start and
         * the intro camera pick, the spawn look angle and every AI reaction
         * timer hang off it - and until 2026-09-08 that state lived only in the
         * run directory's `env` file. A stream copied out of a run and handed
         * on therefore arrived WITHOUT it and fell through to the boot default
         * below: it replayed cleanly, and it replayed a different run.
         *
         * So the sidecar is read here, in the two shapes the repository
         * actually has: `<stream>.spec` (what a captured run writes beside
         * `input`) and `<base>.spec` with a trailing `.input` stripped (what
         * tools/trace/inputs holds). An explicit SL_RNG_SEED still wins, and a
         * stream with no .spec - every one recorded before this existed -
         * keeps the deterministic boot default exactly as it did. */
        v = sl_seed_from_spec(getenv("SL_INPUT"), specbuf, sizeof specbuf);
        if (v != NULL)
            how = "SEED= from the stream's .spec sidecar";
    }

    if (v != NULL) {
        g_randomSeed = strtoull(v, NULL, 16);
        if (getenv("SL_RNG_SEED") != NULL)
            how = "explicit SL_RNG_SEED";
    } else if (getenv("SL_INPUT") != NULL) {
        how = "replay, NO recorded seed and no .spec sidecar"
              " - deterministic boot default, so this is NOT the recorded run";
    } else {
        /* Through the game's OWN randomSetSeed, unmodified - precisely what
         * the cartridge does with osGetCount(). The seeding algorithm,
         * randomGetNext and every consumer of them are untouched; the only
         * thing that changed is that the value handed to it is a real clock
         * again. */
        randomSetSeed(sl_host_boot_seed());
        how = "host clock";
    }
    fprintf(stderr, "sightline native: RNG state %016llx (%s)\n",
            (unsigned long long) g_randomSeed, how);
    if (v == NULL)
        fprintf(stderr, "sightline native: reproduce this run with "
                        "SL_RNG_SEED=%016llx\n",
                (unsigned long long) g_randomSeed);
    sl_run_note_rng_seed((unsigned long long) g_randomSeed);
}

/* fatal-error helper for game-side native code that cannot include host
 * stdio (the SDK include tree shadows the compiler headers) */
void sl_fatalf(const char *fmt, unsigned v)
{
    fprintf(stderr, fmt, v);
    exit(2);
}

/* The explicit tick boundary for frametiming.c's native branch.
 *
 * Deliberately does NOT pace. This is a tick boundary; the recv-pump is the
 * PRESENTATION boundary, and that is where a frame is actually shown, so
 * that is where the sleep belongs. Both sites bump sl_frame, which is why
 * the counter advances about twice per presented frame - pacing here as
 * well would sleep twice per frame and halve the rate.
 *
 * It does need the same limit rule as the pump, or a windowed run walks out
 * through this door at 1000 frames while the pump believes it is unlimited.
 */
/* B-112 / B-143. The native conservative portal-visibility policy's switch.
 * The policy itself lives in src/game/bg.c's portal VISOP cases (native
 * branches; the __sgi text is verbatim); this helper only answers the
 * toggle, the same seam idiom as sl_frame_advance for frametiming.c.
 *
 * DEFAULT OFF since B-143 (2026-09-17): the cartridge's aperture-rectangle
 * window is the rule, SL_PORTAL_WINDOW=1 opts back into B-112's full-view
 * window. B-112 was shipped on 2026-09-10 against a native-only symptom - a
 * skyline that popped as cliff rooms left the visible packet - measured while
 * the RDP scissor was still ignored (B-101), so the geometry that "vanished"
 * was geometry the cartridge never draws: on hardware those rooms are
 * confined by the scissor to their portal window, a few dozen pixels. With
 * the scissor applied (sl_gfx_dl.c, B-143) the pop is invisible by
 * construction, and the full-view window's own cost is what the owner
 * reported at Dam mark 20260916-232914/mark-001: room 1's authored sliver
 * quad across the sky and a fully-fogged wedge cut into the ridge, both
 * absent on the cartridge. Paired at three Dam poses (that mark, and two
 * room-123 skyline views) - the cartridge rule with the scissor matches the
 * cartridge at all three; the B-112 window matches at none of the poses
 * where the two differ. docs/backlog.md B-143. */
int sl_portal_conservative(void)
{
    static int on = -1;
    if (on < 0) { const char *v = getenv("SL_PORTAL_WINDOW");
                  on = (v != NULL && *v == '1' && v[1] == '\0'); }
    return on;
}

void sl_frame_advance(void)
{
#ifdef SL_ALHEAP_TRACE
    sl_fadv_calls++;
#endif
    sl_frame++;
    if (!sl_frame_limit_set && sl_gfx_active())
        sl_frame_limit = 0;
    if (sl_frame_limit != 0 && sl_frame >= sl_frame_limit) {
        fprintf(stderr, "sightline native: survived %u frames\n", sl_frame);
        exit(0);
    }
}
