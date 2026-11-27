/* sl_bg_worker.c - one CPU helper for the background pass's per-vertex loop
 * (v0.4, B-PERF1). See sl_bg_worker.h for the contract.
 *
 * SL_BGW_SELFTEST builds the self-test (tools/windows/bgworkertest.ps1): the
 * same functions with fault injection for the native waits and signals, a
 * thread-creation failure and an FP environment that does not take. The
 * mutants SL_BGW_MUT_* reproduce the defects the tests must catch. */
#include "sl_bg_worker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && (defined(__i386__) || defined(__x86_64__)) && !defined(SL_BGW_FORCE_STUB)
#define SL_BGW_THREADS 1
#endif

#ifdef SL_BGW_THREADS
/* ------------------------------------------------------------ Windows/x86 */
#include <windows.h>
#include <process.h>
#include <stdint.h>
#include <emmintrin.h>

enum { BGW_OFF, BGW_RUNNING, BGW_FAILED, BGW_STOPPED, BGW_RETAINED, BGW_LOST };
static int g_state = BGW_OFF;
static HANDLE g_thr, g_go, g_done;
static volatile LONG g_quit, g_busy, g_gen_post, g_gen_done, g_skipped;
static sl_bgw_fn volatile g_fn;
static void *volatile g_arg;
static volatile unsigned short g_cw;
static volatile unsigned g_csr;

#ifdef SL_BGW_SELFTEST
/* each counter makes that many of the next calls fail */
static volatile LONG t_inj_wait_join, t_inj_set_go, t_inj_set_done, t_inj_wait_go, t_inj_create, t_inj_fp;
static DWORD bgw_wait_go(void)
{
    if (t_inj_wait_go > 0) { InterlockedDecrement(&t_inj_wait_go); SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    return WaitForSingleObject(g_go, INFINITE);
}
static DWORD bgw_wait_done(DWORD ms)
{
    HANDLE hs[2];
    if (t_inj_wait_join > 0) { InterlockedDecrement(&t_inj_wait_join); SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    hs[0] = g_done; hs[1] = g_thr;
    return WaitForMultipleObjects(2, hs, FALSE, ms);
}
static BOOL bgw_set_go(void) { if (t_inj_set_go > 0) { InterlockedDecrement(&t_inj_set_go); return FALSE; } return SetEvent(g_go); }
static BOOL bgw_set_done(void) { if (t_inj_set_done > 0) { InterlockedDecrement(&t_inj_set_done); return FALSE; } return SetEvent(g_done); }
static int bgw_fp_fault(void) { if (t_inj_fp > 0) { InterlockedDecrement(&t_inj_fp); return 1; } return 0; }
static int bgw_create_fault(void) { if (t_inj_create > 0) { InterlockedDecrement(&t_inj_create); return 1; } return 0; }
#else
static DWORD bgw_wait_go(void) { return WaitForSingleObject(g_go, INFINITE); }
static DWORD bgw_wait_done(DWORD ms)
{
    HANDLE hs[2];
    hs[0] = g_done; hs[1] = g_thr;
    return WaitForMultipleObjects(2, hs, FALSE, ms);
}
static BOOL bgw_set_go(void) { return SetEvent(g_go); }
static BOOL bgw_set_done(void) { return SetEvent(g_done); }
#define bgw_fp_fault() 0
#define bgw_create_fault() 0
#endif

static unsigned short bgw_cw(void) { unsigned short cw; __asm__ volatile ("fnstcw %0" : "=m" (cw)); return cw; }

static unsigned __stdcall bgw_main(void *unused)
{
    (void) unused;
    for (;;) {
        if (bgw_wait_go() != WAIT_OBJECT_0) break;          /* the join sees the thread end */
        if (g_gen_done != g_gen_post) {                     /* a job is outstanding: run it first */
            const unsigned short cw = g_cw;
            const unsigned csr = g_csr;
            int ok;
#ifndef SL_BGW_MUT_NOFP
            __asm__ volatile ("fldcw %0" : : "m" (cw));
            _mm_setcsr(csr);
#endif
            ok = (bgw_cw() & 0x1f3fu) == (cw & 0x1f3fu) && (_mm_getcsr() & 0xffc0u) == (csr & 0xffc0u)
                 && !bgw_fp_fault();
            if (ok) { g_fn(g_arg); InterlockedExchange(&g_skipped, 0); }
            else InterlockedExchange(&g_skipped, 1);
            InterlockedExchange(&g_gen_done, g_gen_post);   /* full barrier: the job's writes precede it */
            (void) bgw_set_done();                          /* a failed signal is still proven by generation */
        }
        if (g_quit) break;
    }
    return 0;
}

int sl_bgw_start(void)
{
    uintptr_t h;
    if (g_state == BGW_RUNNING) return 1;
    if (g_state != BGW_OFF) return 0;                       /* never a second helper */
    g_go = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_done = CreateEventA(NULL, FALSE, FALSE, NULL);
    h = (g_go != NULL && g_done != NULL && !bgw_create_fault())
      ? _beginthreadex(NULL, 256u * 1024u, bgw_main, NULL, 0, NULL) : 0;
    if (h == 0) {
        if (g_go != NULL) CloseHandle(g_go);
        if (g_done != NULL) CloseHandle(g_done);
        g_go = g_done = NULL;
        g_state = BGW_FAILED;
        return 0;
    }
    g_thr = (HANDLE) h;
    g_state = BGW_RUNNING;
    return 1;
}

int sl_bgw_post(sl_bgw_fn fn, void *arg)
{
    if (g_state != BGW_RUNNING || g_busy || fn == NULL) return 0;
    g_fn = fn; g_arg = arg;
    g_cw = bgw_cw(); g_csr = _mm_getcsr();
    InterlockedExchange(&g_busy, 1);
    InterlockedIncrement(&g_gen_post);                      /* full barrier: the job is visible first */
    if (!bgw_set_go()) { g_state = BGW_LOST; return SL_BGW_LOST; }   /* busy stays: the job stays reserved */
    return 1;
}

/* Wait for the outstanding job. bounded 0 = as long as it takes (the join);
 * bounded 1 = until deadline, a GetTickCount64 value (the stop). 1 proven
 * complete, 0 not yet, SL_BGW_LOST when it can never be proven. */
static int bgw_wait_job(int bounded, ULONGLONG deadline)
{
    while (g_gen_done != g_gen_post) {
        DWORD r, slice = 100u;
        if (bounded) {
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline) return 0;
            if (deadline - now < slice) slice = (DWORD) (deadline - now);
        }
        r = bgw_wait_done(slice);
        if (r == WAIT_OBJECT_0 || r == WAIT_TIMEOUT) continue;     /* re-check the generation */
        if (r == WAIT_OBJECT_0 + 1) {                              /* the thread ended */
            if (g_gen_done == g_gen_post) break;
            return SL_BGW_LOST;
        }
        return SL_BGW_LOST;                                        /* the wait itself failed */
    }
    MemoryBarrier();
    return 1;
}

int sl_bgw_join(void)
{
    int r;
    if (g_state != BGW_RUNNING || !g_busy) return SL_BGW_LOST;
#if defined(SL_BGW_MUT_NOWAIT)
    r = 1;
#elif defined(SL_BGW_MUT_FAILOPEN)
    while (g_gen_done != g_gen_post)                        /* a failed wait ends the loop */
        if (bgw_wait_done(INFINITE) != WAIT_OBJECT_0) break;
    r = 1;
#else
    r = bgw_wait_job(0, 0);
#endif
    if (r != 1) { g_state = BGW_LOST; return SL_BGW_LOST; }
    InterlockedExchange(&g_busy, 0);
    return g_skipped ? SL_BGW_SKIPPED : SL_BGW_DONE;
}

int sl_bgw_stop(unsigned long deadline_ms)
{
    const ULONGLONG deadline = GetTickCount64() + deadline_ms;
    DWORD r;
    ULONGLONG now;
    if (g_state == BGW_OFF || g_state == BGW_FAILED || g_state == BGW_STOPPED) return 1;
    /* running, retained or lost: drain an outstanding job, then the thread's exit, all inside one deadline */
    if (g_busy && g_state != BGW_LOST) {
        const int w = bgw_wait_job(1, deadline);
        if (w == SL_BGW_LOST) g_state = BGW_LOST;
#ifdef SL_BGW_MUT_STOPFREE
        else if (w == 0) { }                                /* ignores the unfinished job */
#else
        else if (w == 0) { g_state = BGW_RETAINED; return 0; }
#endif
        else InterlockedExchange(&g_busy, 0);
    }
    InterlockedExchange(&g_quit, 1);
    if (!bgw_set_go() && g_state != BGW_LOST) { g_state = BGW_RETAINED; return 0; }
    now = GetTickCount64();
    r = WaitForSingleObject(g_thr, now >= deadline ? 0u : (DWORD) (deadline - now));
#ifdef SL_BGW_MUT_STOPFREE
    r = WAIT_OBJECT_0;                                      /* frees whatever the thread was doing */
#endif
    if (r != WAIT_OBJECT_0) { if (g_state != BGW_LOST) g_state = BGW_RETAINED; return 0; }
    /* proven gone: nothing references the job any more */
    CloseHandle(g_thr); CloseHandle(g_go); CloseHandle(g_done);
    g_thr = g_go = g_done = NULL;
    InterlockedExchange(&g_busy, 0);
    g_state = BGW_STOPPED;
    return 1;
}

int sl_bgw_running(void) { return g_state == BGW_RUNNING; }

const char *sl_bgw_state(void)
{
    switch (g_state) {
    case BGW_RUNNING:  return "running";
    case BGW_FAILED:   return "failed";
    case BGW_STOPPED:  return "stopped";
    case BGW_RETAINED: return "retained";
    case BGW_LOST:     return "lost";
    default:           return "off";
    }
}

#else
/* ------------------------------------------------- no helper: the serial path */
int  sl_bgw_start(void) { return 0; }
int  sl_bgw_post(sl_bgw_fn fn, void *arg) { (void) fn; (void) arg; return 0; }
int  sl_bgw_join(void) { return SL_BGW_LOST; }
int  sl_bgw_stop(unsigned long deadline_ms) { (void) deadline_ms; return 1; }
int  sl_bgw_running(void) { return 0; }
const char *sl_bgw_state(void) { return "off"; }
#endif

/* ================================================================ SELF-TEST */
#if defined(SL_BGW_SELFTEST) && defined(SL_BGW_THREADS)
static int t_pass, t_fail;
static void ck(int ok, const char *what) { if (ok) t_pass++; else { t_fail++; printf("FAIL: %s\n", what); } }
#define TN 200000u
static unsigned t_buf[TN];
struct t_job { unsigned lo, hi, tag; volatile LONG slow, started, ran; unsigned short cw_seen; unsigned csr_seen; };
static void t_fill(void *p)
{
    struct t_job *j = (struct t_job *) p;
    unsigned i;
    InterlockedExchange(&j->started, 1);
    j->cw_seen = bgw_cw(); j->csr_seen = _mm_getcsr();
    if (j->slow) Sleep((DWORD) j->slow);
    for (i = j->lo; i < j->hi; i++) t_buf[i] = j->tag ^ (i * 2654435761u);
    InterlockedExchange(&j->ran, 1);
}
static int t_end(const char *mode)
{
    printf("%s (%s): %d passed, %d failed\n", t_fail ? "BG WORKER SELFTEST FAILED" : "bg worker selftest", mode, t_pass, t_fail);
    return t_fail ? 1 : 0;
}
static void t_wait_started(struct t_job *j) { int w; for (w = 0; w < 2000 && !j->started; w++) Sleep(1); }

/* bgworkertest [mode]: main | startfail | waitfail | setgofail | setdonefail | helperexit | stopdeadline | stopdrain */
int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "main";
    struct t_job hj;
    unsigned k, i;
    memset(&hj, 0, sizeof hj);
    if (!strcmp(m, "startfail")) {
        t_inj_create = 1;
        ck(sl_bgw_start() == 0 && !strcmp(sl_bgw_state(), "failed"), "a failed thread creation leaves no helper (state failed)");
        ck(sl_bgw_post(t_fill, &hj) == 0 && sl_bgw_join() == SL_BGW_LOST && !hj.ran, "nothing is posted or claimed without a helper");
        ck(sl_bgw_start() == 0, "a failed start is not retried");
        ck(sl_bgw_stop(100) == 1, "stop with no helper is proven at once");
        return t_end(m);
    }
    if (!strcmp(m, "waitfail")) {
        ck(sl_bgw_start() == 1, "the helper starts");
        hj.hi = 1; hj.slow = 300; hj.tag = 99u; t_buf[0] = 0u;
        ck(sl_bgw_post(t_fill, &hj) == 1, "a slow job is posted");
        t_inj_wait_join = 1;
        ck(sl_bgw_join() == SL_BGW_LOST, "a failed wait does not claim completion");
        ck(!hj.ran && t_buf[0] == 0u, "the writer was still running: success would have been premature");
        ck(!strcmp(sl_bgw_state(), "lost") && sl_bgw_post(t_fill, &hj) == 0 && sl_bgw_start() == 0, "lost: no next post, no restart");
        ck(sl_bgw_join() == SL_BGW_LOST, "a second join still claims nothing");
        Sleep(450);
        ck(hj.ran && t_buf[0] == (99u ^ 0u), "the retained writer completed into memory that was never released");
        ck(sl_bgw_stop(2000) == 1 && !strcmp(sl_bgw_state(), "stopped"), "once the thread is proven gone the stop releases it");
        return t_end(m);
    }
    if (!strcmp(m, "setgofail")) {
        ck(sl_bgw_start() == 1, "the helper starts");
        hj.hi = 1; hj.tag = 7u;
        t_inj_set_go = 1;
        ck(sl_bgw_post(t_fill, &hj) == SL_BGW_LOST, "a wake that cannot be signalled is lost, not posted");
        ck(sl_bgw_join() == SL_BGW_LOST && sl_bgw_post(t_fill, &hj) == 0 && sl_bgw_start() == 0, "no completion claim, no reuse, no restart");
        return t_end(m);
    }
    if (!strcmp(m, "setdonefail")) {
        ck(sl_bgw_start() == 1, "the helper starts");
        hj.hi = 1; hj.slow = 20; hj.tag = 55u; t_buf[0] = 0u;
        t_inj_set_done = 1;
        ck(sl_bgw_post(t_fill, &hj) == 1 && sl_bgw_join() == SL_BGW_DONE && t_buf[0] == 55u,
           "a lost completion signal is still proven by the generation, and the job really completed");
        hj.tag = 56u; hj.slow = 0;
        ck(sl_bgw_post(t_fill, &hj) == 1 && sl_bgw_join() == SL_BGW_DONE && t_buf[0] == 56u, "the next job runs normally");
        ck(sl_bgw_stop(2000) == 1, "stop proven");
        return t_end(m);
    }
    if (!strcmp(m, "helperexit")) {
        ck(sl_bgw_start() == 1, "the helper starts");
        t_inj_wait_go = 1;
        hj.hi = 1; hj.tag = 3u; t_buf[0] = 0u;
        ck(sl_bgw_post(t_fill, &hj) == 1, "a job is posted (the helper's own wait then fails and it exits)");
        ck(sl_bgw_join() == SL_BGW_LOST && !hj.ran && t_buf[0] == 0u, "a helper that ends without completing is lost, nothing claimed");
        ck(sl_bgw_post(t_fill, &hj) == 0 && sl_bgw_start() == 0, "no reuse, no restart");
        ck(sl_bgw_stop(500) == 1, "the ended thread is proven gone and released");
        return t_end(m);
    }
    if (!strcmp(m, "stopdeadline")) {
        ULONGLONG t0;
        ck(sl_bgw_start() == 1, "the helper starts");
        hj.hi = 1; hj.slow = 600; hj.tag = 11u; t_buf[0] = 0u;
        ck(sl_bgw_post(t_fill, &hj) == 1, "a slow job is posted");
        t_wait_started(&hj);
        t0 = GetTickCount64();
        ck(sl_bgw_stop(100) == 0, "a stop whose deadline the job outlives is not proven");
        ck(GetTickCount64() - t0 < 400u, "and it returns near its deadline: one deadline covers drain and exit");
        ck(!strcmp(sl_bgw_state(), "retained") && !hj.ran, "retained while the writer still runs");
        ck(sl_bgw_post(t_fill, &hj) == 0 && sl_bgw_start() == 0, "nothing is posted to or restarted on a retained helper");
        Sleep(700);
        ck(hj.ran && t_buf[0] == 11u, "the retained job completed into memory that was never released");
        ck(sl_bgw_stop(2000) == 1 && !strcmp(sl_bgw_state(), "stopped"), "a repeated stop proves completion and exit and releases");
        ck(sl_bgw_stop(10) == 1, "a further stop is a proven no-op");
        return t_end(m);
    }
    if (!strcmp(m, "stopdrain")) {
        ck(sl_bgw_start() == 1, "the helper starts");
        hj.hi = 1; hj.slow = 50; hj.tag = 4242u; t_buf[0] = 0u;
        ck(sl_bgw_post(t_fill, &hj) == 1, "a job is posted");
        ck(sl_bgw_stop(2000) == 1, "a stop right after a post drains the job and proves the helper gone");
        ck(hj.ran && t_buf[0] == 4242u, "the posted job ran before the helper quit");
        ck(sl_bgw_start() == 0 && sl_bgw_post(t_fill, &hj) == 0, "a stopped helper is not restarted");
        return t_end(m);
    }
    /* main: many jobs with disjoint halves, refusals, FP environment and the FP-skip path */
    {
        unsigned bad = 0, stale = 0, dbl = 0;
        ck(sl_bgw_start() == 1 && sl_bgw_running(), "the helper starts");
        ck(sl_bgw_start() == 1, "a second start is the same helper");
        ck(sl_bgw_join() == SL_BGW_LOST && sl_bgw_running(), "a join with nothing posted claims nothing and changes nothing");
        for (k = 1; k <= 3000u; k++) {
            const unsigned mid = 1u + (k * 7919u) % (TN - 1u);
            struct t_job mj;
            for (i = 0; i < TN; i++) t_buf[i] = 0xdeadbeefu;
            memset(&hj, 0, sizeof hj);
            hj.lo = mid; hj.hi = TN; hj.tag = k; hj.slow = (k % 500u == 0u) ? 20 : 0;
            mj = hj; mj.lo = 0; mj.hi = mid; mj.slow = 0;
            if (sl_bgw_post(t_fill, &hj) != 1) { ck(0, "post"); break; }
            if (sl_bgw_post(t_fill, &hj) != 0) dbl++;
            t_fill(&mj);
            if (sl_bgw_join() != SL_BGW_DONE) { stale++; continue; }
            for (i = 0; i < TN; i++) if (t_buf[i] != (k ^ (i * 2654435761u))) { bad++; break; }
        }
        ck(bad == 0, "every job: both disjoint halves complete and correct after the join");
        ck(stale == 0, "every join proves the job ran");
        ck(dbl == 0, "a second post while a job is in flight is always refused");
    }
    {
        unsigned short cws[2] = { 0x27f, 0x37f };
        unsigned n, fpok = 1;
        for (n = 0; n < 4u; n++) {
            unsigned short cw = cws[n & 1u];
            unsigned csr = 0x1f80u | ((n & 2u) ? 0x8040u : 0u);
            __asm__ volatile ("fldcw %0" : : "m" (cw));
            _mm_setcsr(csr);
            memset(&hj, 0, sizeof hj); hj.hi = 1;
            if (sl_bgw_post(t_fill, &hj) != 1 || sl_bgw_join() != SL_BGW_DONE || (hj.cw_seen & 0x1f3fu) != (cw & 0x1f3fu)
                || (hj.csr_seen & 0xffc0u) != (csr & 0xffc0u))
                fpok = 0;
        }
        { unsigned short d = 0x27f; __asm__ volatile ("fldcw %0" : : "m" (d)); _mm_setcsr(0x1f80u); }
        ck(fpok, "the helper ran each job under the posting thread's x87 CW and MXCSR");
    }
    memset(&hj, 0, sizeof hj); hj.hi = 1; hj.tag = 8u; t_buf[0] = 0u;
    t_inj_fp = 1;
    ck(sl_bgw_post(t_fill, &hj) == 1 && sl_bgw_join() == SL_BGW_SKIPPED && !hj.ran && t_buf[0] == 0u,
       "an FP environment that does not take: proven complete, nothing ran (the caller computes it)");
    ck(sl_bgw_post(t_fill, &hj) == 1 && sl_bgw_join() == SL_BGW_DONE && hj.ran, "the next job runs normally");
    ck(sl_bgw_stop(2000) == 1 && !strcmp(sl_bgw_state(), "stopped"), "stop proven");
    return t_end(m);
}
#elif defined(SL_BGW_SELFTEST)
int main(void)
{
    int ok = sl_bgw_start() == 0 && sl_bgw_post(NULL, NULL) == 0 && sl_bgw_stop(10) == 1 && !strcmp(sl_bgw_state(), "off");
    printf("%s: no helper on this build\n", ok ? "bg worker selftest" : "BG WORKER SELFTEST FAILED");
    return ok ? 0 : 1;
}
#endif /* SL_BGW_SELFTEST */
