/* sl_worker_pool.c - at most two CPU workers for the open-hole trace batch.
 * See sl_worker_pool.h for the contract; the measurements are in docs/backlog.md
 * (B-PERF1, the fill queries).
 *
 * THE QUEUE WORD. One 32-bit value: bits 0-12 the claim cursor (0..4096), bits
 * 13-25 the batch size n (0..4096), bit 26 open. Publication stores (open, n,
 * 0); a claim moves the cursor by one, only while it is below n and the word
 * is open; drain clears the open bit. Every change is a compare-exchange or an
 * exchange of the whole word, and no counter moves past n.
 *
 * Self-test: -DSL_POOL_SELFTEST builds a main (tools/windows/worldassettest.ps1);
 * its hooks and the fault mutants (-DSL_POOL_MUT_*) exist only in that build. */
#include "sl_worker_pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ portable */

int sl_pool_count_cores(const unsigned long long *core_masks, int ncores, unsigned long long allowed)
{
    int i, n = 0;
    if (core_masks == NULL || ncores < 0) return -1;
    for (i = 0; i < ncores; i++)
        if (core_masks[i] & allowed) n++;
    return n;
}

int sl_pool_choose(const char *env, int cores, int *invalid)
{
    if (invalid) *invalid = 0;
    if (env == NULL || env[0] == '\0' || strcmp(env, "auto") == 0)
        return (cores >= 4) ? SL_POOL_MAX_WORKERS : 0;
    if (env[1] == '\0' && env[0] >= '0' && env[0] <= '0' + SL_POOL_MAX_WORKERS) return env[0] - '0';
    if (invalid) *invalid = 1;
    return 0;
}

/* Workers only on Windows/x86 (the x87 control word is part of the contract).
 * SL_POOL_FORCE_STUB builds the no-worker path on Windows too, so the gate
 * compiles and runs what a non-Windows build gets. */
#if defined(_WIN32) && (defined(__i386__) || defined(__x86_64__)) && !defined(SL_POOL_FORCE_STUB)
#define SL_POOL_THREADS 1
#endif

#ifdef SL_POOL_THREADS
/* -------------------------------------------------------------- Windows/x86 */
#include <windows.h>
#include <process.h>
#include <stdint.h>
#include <emmintrin.h>

#define QW_CUR_MASK 0x00001fffu
#define QW_N_SHIFT 13
#define QW_N_MASK 0x03ffe000u
#define QW_OPEN 0x04000000u

static struct sl_pool_job g_job[SL_POOL_MAX_JOBS];
static volatile LONG g_q;                 /* the queue word (closed, empty) */
static volatile LONG g_active;            /* workers counted inside the queue */
static volatile LONG g_quit;
static volatile LONG g_pool;              /* the live pool's serial */
static volatile LONG g_gen;
static unsigned short g_fpcw;
static unsigned int g_mxcsr;
static HANDLE g_sem;
static HANDLE g_thr[SL_POOL_MAX_WORKERS];
static int g_nthr;
static int g_stuck;                       /* a stopped pool not proven gone: g_old_* retained */
static HANDLE g_old_thr[SL_POOL_MAX_WORKERS], g_old_sem;
static int g_old_n;
static sl_pool_fn g_fn;
struct pool_warg { LONG pool; int slot; HANDLE sem; };
static struct pool_warg g_warg[SL_POOL_MAX_WORKERS];

#ifdef SL_POOL_SELFTEST
static volatile LONG t_hold, t_held, t_fail_at = -1;
static volatile LONG t_jobs_by_pool[8];
static volatile DWORD t_tid[SL_POOL_MAX_WORKERS];
#endif

static unsigned pool_cw(void) { unsigned short cw; __asm__ volatile ("fnstcw %0" : "=m" (cw)); return cw; }

static unsigned __stdcall pool_worker(void *arg)
{
    const struct pool_warg *wa = (const struct pool_warg *) arg;
    const LONG mypool = wa->pool;
    const HANDLE sem = wa->sem;
    LONG mygen = -1;
#ifdef SL_POOL_SELFTEST
    t_tid[wa->slot] = GetCurrentThreadId();
#endif
    for (;;) {
        if (WaitForSingleObject(sem, INFINITE) != WAIT_OBJECT_0) break;
#ifdef SL_POOL_SELFTEST
        if (t_hold) { InterlockedIncrement(&t_held); while (t_hold) Sleep(1); }
#endif
        InterlockedIncrement(&g_active);
#if !(defined(SL_POOL_SELFTEST) && defined(SL_POOL_MUT_NOPOOLCHECK))
        if (g_quit || g_pool != mypool) { InterlockedDecrement(&g_active); break; }
#endif
        for (;;) {
            const LONG q = g_q;
            const unsigned uq = (unsigned) q;
            const unsigned cur = uq & QW_CUR_MASK, n = (uq & QW_N_MASK) >> QW_N_SHIFT;
            unsigned i;
#if !(defined(SL_POOL_SELFTEST) && defined(SL_POOL_MUT_NOCLOSE))
            if (!(uq & QW_OPEN)) break;
#endif
            if (cur >= n) break;
            if (InterlockedCompareExchange(&g_q, (LONG) (uq + 1u), q) != q) continue;
            i = cur;
            if (InterlockedCompareExchange((volatile LONG *) &g_job[i].state, 2, 1) != 1) continue;
            /* after a successful claim (a full barrier) the batch's environment is visible */
            if (mygen != g_gen) {
                unsigned short cw = g_fpcw;
                unsigned int csr = g_mxcsr;
#if !(defined(SL_POOL_SELFTEST) && defined(SL_POOL_MUT_NOENV))
                __asm__ volatile ("fldcw %0" : : "m" (cw));
                _mm_setcsr(csr);
#else
                (void) cw; (void) csr;
#endif
                mygen = g_gen;
            }
            g_job[i].result = g_fn(g_job[i].eye, g_job[i].hq, g_job[i].sc);
            g_job[i].by = 1;
#ifdef SL_POOL_SELFTEST
            InterlockedIncrement(&t_jobs_by_pool[mypool & 7]);
#endif
            InterlockedExchange((volatile LONG *) &g_job[i].state, 3);
        }
        InterlockedDecrement(&g_active);
    }
    return 0;
}

int sl_pool_reap(unsigned long timeout_ms)
{
    DWORD r;
    int i;
    if (!g_stuck) return 1;
    r = WaitForMultipleObjects((DWORD) g_old_n, g_old_thr, TRUE, (DWORD) timeout_ms);
    if (r >= WAIT_OBJECT_0 + (DWORD) g_old_n) return 0;    /* timeout, failure, abandoned */
    for (i = 0; i < g_old_n; i++) CloseHandle(g_old_thr[i]);
    CloseHandle(g_old_sem);
    g_old_n = 0; g_old_sem = NULL; g_stuck = 0;
    InterlockedExchange(&g_quit, 0);
    return 1;
}

int sl_pool_start(int n, sl_pool_fn fn)
{
    int i;
    LONG pool;
    g_fn = fn;
    if (g_nthr != 0) return g_nthr;
    if (n > SL_POOL_MAX_WORKERS) n = SL_POOL_MAX_WORKERS;
    if (n <= 0 || fn == NULL) return 0;
    if (g_stuck && !sl_pool_reap(0)) return 0;
    g_sem = CreateSemaphoreA(NULL, 0, 0x7fffffff, NULL);
    if (g_sem == NULL) return 0;
    InterlockedExchange(&g_quit, 0);
    pool = InterlockedIncrement(&g_pool);
    for (i = 0; i < n; i++) {
        uintptr_t h;
        g_warg[i].pool = pool; g_warg[i].slot = i; g_warg[i].sem = g_sem;
#ifdef SL_POOL_SELFTEST
        if (t_fail_at == i) break;
#endif
        h = _beginthreadex(NULL, 256u * 1024u, pool_worker, &g_warg[i], 0, NULL);
        if (h == 0) break;
        g_thr[i] = (HANDLE) h;
        g_nthr++;
    }
    if (g_nthr == 0) { CloseHandle(g_sem); g_sem = NULL; }
    return g_nthr;
}

int sl_pool_workers(void) { return g_nthr; }

void sl_pool_drain(void)
{
    LONG q;
    if (g_nthr == 0) return;
    do { q = g_q; } while (InterlockedCompareExchange(&g_q, (LONG) ((unsigned) q & ~QW_OPEN), q) != q);
    while (g_active != 0) YieldProcessor();
    MemoryBarrier();
}

int sl_pool_stop(unsigned long timeout_ms)
{
    DWORD r;
    int i, n = g_nthr;
    if (n == 0) return g_stuck ? 0 : 1;
    sl_pool_drain();
    InterlockedExchange(&g_quit, 1);
    InterlockedIncrement(&g_pool);            /* no worker of this pool takes another batch */
    ReleaseSemaphore(g_sem, n, NULL);
    r = WaitForMultipleObjects((DWORD) n, g_thr, TRUE, (DWORD) timeout_ms);
#if defined(SL_POOL_SELFTEST) && defined(SL_POOL_MUT_OLDSTOP)
    r = WAIT_OBJECT_0;                        /* mutant: free regardless */
#endif
    g_nthr = 0;
    if (r < WAIT_OBJECT_0 + (DWORD) n) {
        for (i = 0; i < n; i++) CloseHandle(g_thr[i]);
        CloseHandle(g_sem);
        g_sem = NULL;
        InterlockedExchange(&g_quit, 0);
        return 1;
    }
    for (i = 0; i < n; i++) g_old_thr[i] = g_thr[i];
    g_old_n = n; g_old_sem = g_sem; g_sem = NULL; g_stuck = 1;
    return 0;
}

struct sl_pool_job *sl_pool_jobs(void) { return g_job; }

void sl_pool_publish(unsigned n)
{
    g_fpcw = (unsigned short) pool_cw();
    g_mxcsr = _mm_getcsr();
    InterlockedIncrement(&g_gen);
    if (n > SL_POOL_MAX_JOBS) n = SL_POOL_MAX_JOBS;
    if (g_nthr == 0 || n == 0) {
        InterlockedExchange(&g_q, (LONG) ((n << QW_N_SHIFT) & QW_N_MASK));   /* closed */
        return;
    }
    InterlockedExchange(&g_q, (LONG) (QW_OPEN | ((n << QW_N_SHIFT) & QW_N_MASK)));   /* full barrier */
    ReleaseSemaphore(g_sem, g_nthr, NULL);
}

int sl_pool_take(unsigned i, int *result, int *how)
{
    LONG s;
    if ((pool_cw() & 0x1f3fu) != (g_fpcw & 0x1f3fu) || (_mm_getcsr() & 0xffc0u) != (g_mxcsr & 0xffc0u))
        return 0;
    s = InterlockedCompareExchange((volatile LONG *) &g_job[i].state, 2, 1);
    if (s == 1) {
        g_job[i].result = g_fn(g_job[i].eye, g_job[i].hq, g_job[i].sc);
        g_job[i].by = 0;
        g_job[i].state = 3;
        *result = g_job[i].result;
        if (how) *how = 0;
        return 1;
    }
    if (s == 2) {
        while (g_job[i].state != 3) YieldProcessor();
        MemoryBarrier();
        *result = g_job[i].result;
        if (how) *how = 2;
        return 1;
    }
    MemoryBarrier();
    *result = g_job[i].result;
    if (how) *how = g_job[i].by ? 1 : 0;
    return 1;
}

int sl_pool_allowed_physical_cores(void)
{
    DWORD_PTR pm = 0, sm = 0;
    DWORD len = 0;
    char *buf, *p;
    unsigned long long masks[256];
    int n = 0;
    if (GetActiveProcessorGroupCount() != 1) return -1;          /* more than one group: not told */
    if (!GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm) || pm == 0) return -1;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return -1;
    buf = (char *) malloc(len);
    if (buf == NULL) return -1;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) buf, &len)) {
        free(buf);
        return -1;
    }
    for (p = buf; p < buf + len && n < 256; p += ((PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) p)->Size) {
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX e = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) p;
        if (e->Relationship != RelationProcessorCore) continue;
        if (e->Processor.GroupCount != 1 || e->Processor.GroupMask[0].Group != 0) { free(buf); return -1; }
        masks[n++] = (unsigned long long) e->Processor.GroupMask[0].Mask;
    }
    free(buf);
    return sl_pool_count_cores(masks, n, (unsigned long long) pm);
}

#else
/* ------------------------------------------------- no workers: the stub path */
static struct sl_pool_job g_job[SL_POOL_MAX_JOBS];
int sl_pool_start(int n, sl_pool_fn fn) { (void) n; (void) fn; return 0; }
int sl_pool_workers(void) { return 0; }
struct sl_pool_job *sl_pool_jobs(void) { return g_job; }
void sl_pool_publish(unsigned n) { (void) n; }
int sl_pool_take(unsigned i, int *result, int *how) { (void) i; (void) result; (void) how; return 0; }
void sl_pool_drain(void) { }
int sl_pool_stop(unsigned long timeout_ms) { (void) timeout_ms; return 1; }
int sl_pool_reap(unsigned long timeout_ms) { (void) timeout_ms; return 1; }
int sl_pool_allowed_physical_cores(void) { return -1; }
#endif

/* ================================================================ SELF-TEST */
#if defined(SL_POOL_SELFTEST) && defined(SL_POOL_THREADS)
#include <math.h>

static int st_pass, st_fail;
static void ck(int ok, const char *what)
{
    if (ok) { st_pass++; return; }
    st_fail++;
    printf("FAIL: %s\n", what);
}
static unsigned envbits(void) { return (pool_cw() << 16) | (_mm_getcsr() & 0xffc0u); }
/* deterministic, variable cost; hq[2] < 0: the computing thread's FP environment */
static int st_fn(const float *e, const float *h, float sc)
{
    unsigned spin = ((unsigned) (h[0] * 7.0f)) % 300u, i;
    double acc = 0.0;
    if (h[2] < 0.0f) return (int) envbits();
    for (i = 0; i < spin; i++) acc += sqrt((double) e[0] + i);
    return (int) (((unsigned) (e[0] * 31.0f + h[1] * 17.0f + sc)) ^ (unsigned) (acc > 1e300)) & 0x7fffffff;
}
static unsigned st_rs = 2026u;
static unsigned st_rnd(void) { st_rs = st_rs * 1103515245u + 12345u; return st_rs >> 8; }
static void st_fill(unsigned n, int envprobe)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        g_job[i].eye[0] = (float) (st_rnd() % 1000u); g_job[i].eye[1] = g_job[i].eye[2] = 0.0f;
        g_job[i].hq[0] = (float) (st_rnd() % 997u); g_job[i].hq[1] = (float) (st_rnd() % 991u);
        g_job[i].hq[2] = (envprobe && st_rnd() % 8u == 0u) ? -1.0f : 1.0f;
        g_job[i].sc = 0.5f; g_job[i].by = 0; g_job[i].result = 0; g_job[i].state = 1;
    }
}
static unsigned long st_take_all(unsigned n, unsigned take)
{
    static unsigned order[SL_POOL_MAX_JOBS];
    unsigned long bad = 0;
    unsigned i;
    for (i = 0; i < n; i++) order[i] = i;
    for (i = n; i > 1; i--) { unsigned j = st_rnd() % i, t = order[i - 1]; order[i - 1] = order[j]; order[j] = t; }
    for (i = 0; i < take && i < n; i++) {
        int r = 0, how;
        const unsigned j = order[i];
        if (!sl_pool_take(j, &r, &how) || r != st_fn(g_job[j].eye, g_job[j].hq, g_job[j].sc)) bad++;
    }
    return bad;
}
static LONG st_pool_jobs(void) { LONG s = 0; int i; for (i = 0; i < 8; i++) s += t_jobs_by_pool[i]; return s; }
static int st_wait_held(void) { int k; for (k = 0; k < 2000 && t_held == 0; k++) Sleep(1); return t_held != 0; }

static void st_topology(void)
{
    /* eight physical cores of two logical processors each (SMT), lowest first */
    unsigned long long smt[8];
    unsigned long long solo[3] = { 1ull, 2ull, 4ull };
    int i, inv = 0, c;
    for (i = 0; i < 8; i++) smt[i] = 3ull << (2 * i);
    ck(sl_pool_count_cores(smt, 8, 0xffffull) == 8, "16 logical processors on 8 cores count as 8 physical cores");
    ck(sl_pool_count_cores(smt, 8, 0x55ull) == 4, "one logical processor on each of four cores (0x55) counts as 4");
    ck(sl_pool_count_cores(smt, 8, 0x0full) == 2, "four logical processors on two cores (0x0f) count as 2, not 4");
    ck(sl_pool_count_cores(solo, 3, 0x7ull) == 3, "three cores without SMT count as 3");
    ck(sl_pool_count_cores(NULL, 8, 1ull) == -1, "no core table: unknown");
    ck(sl_pool_choose(NULL, 4, &inv) == 2 && !inv, "unset at 4 physical cores: 2 workers");
    ck(sl_pool_choose("auto", 16, &inv) == 2 && !inv, "auto at 16: still 2 (the maximum)");
    ck(sl_pool_choose("", 3, &inv) == 0 && !inv, "3 physical cores: inline");
    ck(sl_pool_choose(NULL, sl_pool_count_cores(smt, 8, 0x0full), &inv) == 0, "SMT siblings do not make 4: inline");
    ck(sl_pool_choose(NULL, -1, &inv) == 0 && !inv, "unknown topology: inline");
    ck(sl_pool_choose("0", 16, &inv) == 0 && !inv, "0 disables");
    ck(sl_pool_choose("1", 2, &inv) == 1 && sl_pool_choose("2", 1, &inv) == 2 && !inv, "1 and 2 force the count");
    ck(sl_pool_choose("3", 16, &inv) == 0 && inv, "3 is invalid: inline, flagged");
    ck(sl_pool_choose("yes", 16, &inv) == 0 && inv && sl_pool_choose("22", 16, &inv) == 0 && inv
       && sl_pool_choose("-1", 16, &inv) == 0 && inv, "anything else is invalid: inline, flagged");
    c = sl_pool_allowed_physical_cores();
    ck(c == -1 || c >= 1, "this machine's allowed physical cores are counted or reported unknown");
    printf("  this process may run on %d physical core(s); default workers %d\n", c, sl_pool_choose(NULL, c, &inv));
}

static void st_random(unsigned batches)
{
    unsigned long bad = 0, postdrain = 0, unproven = 0;
    unsigned short cws[2] = { 0x37f, 0x27f };
    unsigned short keep_cw = (unsigned short) pool_cw();
    unsigned keep_csr = _mm_getcsr();
    unsigned k, i;
    int nw = 2;
    ck(sl_pool_start(nw, st_fn) == nw, "two workers start");
    for (k = 0; k < batches; k++) {
        const unsigned pick = st_rnd() % 8u;
        const unsigned n = pick == 0 ? 0 : pick == 1 ? 1 + st_rnd() % 3u : pick == 2 ? SL_POOL_MAX_JOBS : st_rnd() % 700u;
        unsigned take;
        unsigned short cw = cws[st_rnd() & 1u];
        unsigned csr = 0x1f80u | ((st_rnd() & 1u) ? 0x8040u : 0u);
        __asm__ volatile ("fldcw %0" : : "m" (cw));
        _mm_setcsr(csr);
        st_fill(n, 1);
        sl_pool_publish(n);
        take = (st_rnd() % 5u == 0u) ? 0u : (st_rnd() % 4u == 0u) ? st_rnd() % (n + 1u) : n;
        bad += st_take_all(n, take);
        sl_pool_drain();
        for (i = 0; i < n; i++) { g_job[i].result = -12345; g_job[i].state = 7; }
        for (i = 0; i < 64u; i++) YieldProcessor();
        if ((k & 63u) == 0u) Sleep(0);
        for (i = 0; i < n; i++) if (g_job[i].result != -12345 || g_job[i].state != 7) postdrain++;
        if (k % 1500u == 1499u) {
            if (!sl_pool_stop(5000)) unproven++;
            nw = (nw == 2) ? 1 : 2;
            if (sl_pool_start(nw, st_fn) != nw) { ck(0, "restart"); break; }
        }
    }
    if (!sl_pool_stop(5000)) unproven++;
    __asm__ volatile ("fldcw %0" : : "m" (keep_cw));
    _mm_setcsr(keep_csr);
    printf("  T1 %u batches: %lu wrong (FP probes included), %lu writes after drain, %lu stops not proven\n",
           batches, bad, postdrain, unproven);
    ck(bad == 0, "T1 every answer, FP environment probes included, is the render thread's own");
    ck(postdrain == 0, "T1 no worker writes a record after drain");
    ck(unproven == 0, "T1 every stop proves its threads gone");
}

static void st_partial(void)
{
    unsigned long bad = 0;
    unsigned k;
    t_fail_at = 1;
    ck(sl_pool_start(2, st_fn) == 1, "T2 the second thread failing to start leaves one worker");
    t_fail_at = -1;
    for (k = 0; k < 300u; k++) { unsigned n = st_rnd() % 300u; st_fill(n, 1); sl_pool_publish(n); bad += st_take_all(n, n); sl_pool_drain(); }
    ck(bad == 0, "T2 the partial pool answers correctly");
    ck(sl_pool_stop(5000) == 1, "T2 the partial pool stops, proven");
}

static void st_late(void)
{
    unsigned long bad = 0;
    unsigned i;
    int untouched = 1;
    ck(sl_pool_start(1, st_fn) == 1, "T3 one worker starts");
    t_held = 0; t_hold = 1;
    st_fill(50, 0); sl_pool_publish(50);
    ck(st_wait_held(), "T3 the worker is held after its wake");
    bad += st_take_all(50, 50);
    sl_pool_drain();
    st_fill(80, 0);                           /* the next records, NOT published */
    for (i = 0; i < 80u; i++) g_job[i].result = -777;
    t_hold = 0;
    Sleep(50);
    for (i = 0; i < 80u; i++) if (g_job[i].state != 1 || g_job[i].result != -777 || g_job[i].by != 0) untouched = 0;
    ck(untouched, "T3 a worker released after drain claims nothing from unpublished records");
    sl_pool_publish(80); bad += st_take_all(80, 80); sl_pool_drain();
    t_held = 0; t_hold = 1;
    st_fill(40, 0); sl_pool_publish(40);
    ck(st_wait_held(), "T3 the worker is held again");
    bad += st_take_all(40, 40); sl_pool_drain();
    st_fill(400, 0); sl_pool_publish(400);
    t_hold = 0;                               /* released after the next publication */
    bad += st_take_all(400, 400); sl_pool_drain();
    ck(bad == 0, "T3 every answer correct across the delayed wakes");
    ck(sl_pool_stop(5000) == 1, "T3 stop proven");
}

static void st_witness(void)
{
    HANDLE old;
    LONG before;
    unsigned long bad = 0;
    unsigned k;
    int r;
    ck(sl_pool_start(1, st_fn) == 1, "T4 one worker starts");
    Sleep(20);
    old = OpenThread(SYNCHRONIZE, FALSE, t_tid[0]);
    ck(old != NULL, "T4 a handle of the test's own to the worker");
    t_held = 0; t_hold = 1;
    st_fill(5, 0); sl_pool_publish(5);
    ck(st_wait_held(), "T4 the worker is held between its wake and counting itself in");
    bad += st_take_all(5, 5);
    r = sl_pool_stop(200);                    /* drain sees no active worker; the join times out */
    ck(r == 0, "T4 stop reports the pool NOT proven gone");
    ck(!(r == 1 && old != NULL && WaitForSingleObject(old, 0) != WAIT_OBJECT_0), "T4 no pool freed while its thread lives");
    ck(sl_pool_start(2, st_fn) == 0, "T4 no new pool while the old one is retained");
    before = st_pool_jobs();
    st_fill(300, 0); sl_pool_publish(300);
    t_hold = 0;
    Sleep(100);
    bad += st_take_all(300, 300); sl_pool_drain();
    ck(old != NULL && WaitForSingleObject(old, 3000) == WAIT_OBJECT_0, "T4 the old worker exits once released");
    ck(st_pool_jobs() == before, "T4 no worker computes anything between the failed stop and the reap");
    ck(sl_pool_reap(2000) == 1, "T4 reap proves the old pool gone and frees it");
    ck(sl_pool_start(2, st_fn) == 2, "T4 a new pool starts afterwards");
    for (k = 0; k < 200u; k++) { unsigned n = st_rnd() % 400u; st_fill(n, 0); sl_pool_publish(n); bad += st_take_all(n, n); sl_pool_drain(); }
    ck(bad == 0, "T4 every answer correct before, during and after");
    ck(sl_pool_stop(5000) == 1, "T4 the new pool stops, proven");
    if (old != NULL) CloseHandle(old);
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : "all";
    const unsigned batches = argc > 2 ? (unsigned) atoi(argv[2]) : 6000u;
    if (!strcmp(only, "all") || !strcmp(only, "topology")) st_topology();
    if (!strcmp(only, "all") || !strcmp(only, "random")) st_random(batches);
    if (!strcmp(only, "all") || !strcmp(only, "partial")) st_partial();
    if (!strcmp(only, "all") || !strcmp(only, "late")) st_late();
    if (!strcmp(only, "all") || !strcmp(only, "witness")) st_witness();
    printf("%s: %d passed, %d failed\n", st_fail ? "WORKER POOL SELFTEST FAILED" : "worker pool selftest", st_pass, st_fail);
    return st_fail ? 1 : 0;
}
#elif defined(SL_POOL_SELFTEST)
int main(void)
{
    int inv = 0;
    int ok = sl_pool_start(2, NULL) == 0 && sl_pool_allowed_physical_cores() == -1 && sl_pool_choose(NULL, -1, &inv) == 0;
    printf("%s: no workers on this build\n", ok ? "worker pool selftest" : "WORKER POOL SELFTEST FAILED");
    return ok ? 0 : 1;
}
#endif /* SL_POOL_SELFTEST */
