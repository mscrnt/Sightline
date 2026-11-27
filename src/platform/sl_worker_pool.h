/* sl_worker_pool.h - at most two CPU workers for the open-hole trace batch
 * (v0.4, B-PERF1: the fill queries).
 *
 * The renderer (sl_gfx_dl.c, qw_dispatch) predicts a walk's open-hole traces
 * when nf_frame has fixed their inputs, writes each as an immutable job record
 * and publishes the batch; workers evaluate the jobs with the unchanged
 * sl_wa_fill_open; the render thread takes each answer in its own order at the
 * memo miss that asks it, and computes inline whatever no batch holds. Every
 * renderer global, the memo, the registration cache, every counter and every
 * GL call stay on the render thread.
 *
 * CONTRACT
 *  - Inputs: a job is the exact (eye, hole point, scale) bits; an answer is
 *    used only for an identical key.
 *  - The fill grid is immutable from publication until sl_pool_drain returns:
 *    the renderer drains before sl_wa_fill_set and at frame start.
 *  - Floating point: a worker loads the publisher's x87 control word and MXCSR
 *    after each claim (the library sqrt runs on the x87 under -fno-builtin);
 *    sl_pool_take answers 0 when the render thread's environment differs from
 *    the batch's, and the caller computes inline.
 *  - Ownership: the queue word (cursor | n | open) changes only by
 *    compare-exchange or exchange; the cursor never passes n. A worker counts
 *    itself in before reading the word and re-checks quit and its pool serial
 *    after; drain closes the word and waits for zero.
 *  - Lifetime: sl_pool_stop frees the pool only when every thread is proven
 *    gone; otherwise it keeps the handles, returns 0 and refuses a new pool
 *    until sl_pool_reap proves them gone.
 *
 * A build that is not Windows/x86 has no workers: sl_pool_start returns 0 and
 * the renderer stays on its original inline path. */
#ifndef SL_WORKER_POOL_H
#define SL_WORKER_POOL_H

#define SL_POOL_MAX_WORKERS 2
#define SL_POOL_MAX_JOBS 4096u

typedef int (*sl_pool_fn)(const float *eye, const float *hq, float sc);

struct sl_pool_job {
    float eye[3], hq[3], sc;    /* the trace's inputs, written before publication */
    volatile long state;        /* 1 queued, 2 running, 3 done */
    volatile long by;           /* 1 a worker computed it, 0 the render thread */
    int result;
};

/* Start up to n (clamped to SL_POOL_MAX_WORKERS) workers running fn; returns
 * how many started (0: none - the caller stays inline). */
int  sl_pool_start(int n, sl_pool_fn fn);
int  sl_pool_workers(void);
/* The render thread's records; valid to write between drain and publish. */
struct sl_pool_job *sl_pool_jobs(void);
/* Publish jobs [0, n), every one already written with state 1. */
void sl_pool_publish(unsigned n);
/* Job i's answer into *result: 1 = given (*how 1 a worker computed it, 0 the
 * render thread just did, 2 waited for a worker); 0 = the render thread's FP
 * environment differs from the batch's - compute it inline. */
int  sl_pool_take(unsigned i, int *result, int *how);
/* Close the queue and wait until no worker is inside it. */
void sl_pool_drain(void);
/* 1 = every thread proven gone and the pool freed; 0 = not proven (retained). */
int  sl_pool_stop(unsigned long timeout_ms);
int  sl_pool_reap(unsigned long timeout_ms);

/* TOPOLOGY. Physical cores at least one of whose logical processors the
 * process may run on; -1 when it cannot be told (another processor group, an
 * API failure, a non-Windows build). */
int  sl_pool_allowed_physical_cores(void);
/* The pure count behind it: core_masks[i] is core i's logical-processor mask. */
int  sl_pool_count_cores(const unsigned long long *core_masks, int ncores, unsigned long long allowed);
/* THE WORKER COUNT. env is SL_FILL_WORKERS: unset, empty or "auto" -> 2 when
 * cores >= 4, else 0 (an unknown topology, cores < 0, is 0); "0", "1", "2" ->
 * that count (0 disables, 1 and 2 force a count for measurement); anything
 * else -> 0 and *invalid = 1. */
int  sl_pool_choose(const char *env, int cores, int *invalid);

#endif /* SL_WORKER_POOL_H */
