/* sl_bg_worker.h - one CPU helper for the background pass's per-vertex loop
 * (v0.4, B-PERF1).
 *
 * The renderer (sl_gfx_dl.c, bg_draw_pass) splits the item's vertex loop into
 * two contiguous ranges: the render thread computes the lower one and this
 * helper the upper one, each into its own indices and its own reductions; the
 * render thread joins before anything reads the outputs. Every renderer global,
 * every allocation, every GL call and every order-sensitive diagnostic stay on
 * the render thread. The fill-query workers (sl_worker_pool.h) are separate:
 * with both in use the process runs at most 3 helper threads.
 *
 * CONTRACT
 *  - A job is a function and an argument. From sl_bgw_post until a join
 *    returns SL_BGW_DONE or SL_BGW_SKIPPED, everything the job reads stays
 *    allocated and unchanged, and every span it writes belongs to the job
 *    alone: no other thread reads, writes, moves or frees it. After
 *    SL_BGW_LOST the caller keeps every object the job references allocated
 *    and untouched until a later sl_bgw_stop returns 1 (the thread proven
 *    gone), and never consumes or reuses that data.
 *  - Floating point: the helper loads the posting thread's x87 control word and
 *    MXCSR before every job and reads them back; when they did not take, it
 *    runs nothing and the join returns SL_BGW_SKIPPED (the caller computes that
 *    range itself, which is safe: the helper provably no longer touches it).
 *  - Completion is proven by generation, never by a wait's return value: the
 *    helper publishes the job's generation (a full barrier after the job's
 *    writes) before it signals, and every wake re-checks it, so a lost or
 *    failed signal is still proven and a helper that exits without finishing is
 *    detected. sl_bgw_join waits as long as the job takes; it returns
 *    SL_BGW_LOST only when completion cannot be proven at all (a wait failed,
 *    or the thread ended without completing). After SL_BGW_LOST nothing can be
 *    posted and the helper is never restarted; the helper releases nothing a
 *    job references, and only a stop that proves the thread gone closes its
 *    handles.
 *  - Lifetime: sl_bgw_stop(ms) has ONE monotonic deadline for the whole stop:
 *    draining an outstanding job and then the thread's exit. It returns 1 only
 *    when the thread is proven gone (handles closed); otherwise 0, the handles
 *    and every referenced job stay retained, nothing can be posted, and a later
 *    sl_bgw_stop may finish the job.
 *
 * A build that is not Windows/x86 has no helper: sl_bgw_start returns 0 and the
 * renderer runs the loop on the render thread alone. */
#ifndef SL_BG_WORKER_H
#define SL_BG_WORKER_H

#define SL_BGW_DONE     1     /* the job ran to completion */
#define SL_BGW_SKIPPED  0     /* proven complete without running (FP environment did not take) */
#define SL_BGW_LOST   (-1)    /* completion not proven: the helper is lost */

typedef void (*sl_bgw_fn)(void *arg);

/* Start the helper once (no-op when running). 1 running, 0 not (never
 * started on this build, refused, failed, stopped, retained or lost). */
int  sl_bgw_start(void);
/* 1 posted; 0 not posted (no running helper, or a job in flight); SL_BGW_LOST
 * when the wake could not be signalled (the helper is lost). */
int  sl_bgw_post(sl_bgw_fn fn, void *arg);
/* Wait for the posted job: SL_BGW_DONE, SL_BGW_SKIPPED or SL_BGW_LOST. A join
 * with no job posted returns SL_BGW_LOST without changing anything. */
int  sl_bgw_join(void);
/* 1 = proven gone (or never started); 0 = not proven within the deadline. */
int  sl_bgw_stop(unsigned long deadline_ms);
int  sl_bgw_running(void);
/* One word for the census: "off" (never started), "running", "failed" (could
 * not start), "stopped", "retained" (stop not proven) or "lost". */
const char *sl_bgw_state(void);

#endif /* SL_BG_WORKER_H */
