/**
 * Per-scope frame-span and GPU-elapsed metric producer - opt-in.
 *
 * WHAT THIS IS. One file that records, for a named scope, the WALL time
 * between the scope's two ends and the time the GPU spent executing the work
 * submitted inside it, and appends one CSV row per scope per frame in the
 * exact column order the readiness importer's feature_metrics expects:
 *
 *     frame,scope,cpu_submit_ms,gpu_elapsed_ms,gpu_status
 *
 * The GPU number always comes from a GL timer query. There is no host-clock
 * fallback and no zero substituted for a number that was never measured: a
 * record with no completed query leaves gpu_elapsed_ms EMPTY and says so in
 * gpu_status. The importer rejects the alternative ('unavailable GPU time must
 * be blank'), and rightly - a host-timed "GPU" number is the measurement this
 * whole apparatus exists to avoid.
 *
 * WHAT cpu_submit_ms IS, FOR THE WHOLE-FRAME SCOPE. The frame's
 * CLEAR-TO-PRESENT WALL SPAN, and nothing narrower. The frame scope opens in
 * sdl_begin and closes in sdl_end, and between those two points the frame loop
 * in src/platform/sl_ultra_shim.c runs the pacing wait, the game and phase
 * work, the developer probes and the pumped-frame increment. So this number
 * contains game CPU time, pacing time and any unrelated CPU gap. It is NOT
 * renderer CPU submission time and NOT CPU execution time; the column keeps
 * the importer's name, and this is what that name carries for this scope.
 *
 * gpu_elapsed_ms is the GPU timeline delta between the scope's two timer
 * queries, which is not exclusive GPU-busy time either: idle between the two
 * points, and work another client submitted, fall inside it.
 *
 * The two spans OVERLAP. Subtracting one from the other does not isolate a
 * bottleneck and must not be presented as doing so. A genuinely renderer-only
 * submission figure needs a narrower scope, which is what the per-scope API
 * below exists for.
 *
 * WHICH FRAME A ROW BELONGS TO. The pumped-frame counter advances between a
 * frame's begin and its present: sl_ultra_shim.c calls sl_gfx_begin_frame at
 * 1457, increments sl_frame at 1497, and runs sl_gfx_end_frame with the
 * regular present at 1432-1440 of the NEXT iteration. The identity is
 * therefore read at the close of the OUTERMOST scope, which is the counter
 * value the regular present is recorded at, and every nested scope of that
 * frame is stamped with the same number so the rows of one frame join. No
 * offset is added and no ratio between a render and a pumped frame is
 * invented. See the long note at the top of sl_gfx_profile.c.
 *
 * A frame whose swap was held is a DIFFERENT SERIES, named "frame-held"
 * instead of "frame", because sl_ultra_shim.c notes a regular present only
 * when the swap was not held. The "frame" series is therefore exactly the
 * regular-present grid, and held work can never be read as presented work.
 *
 * WHAT IT IS NOT. It is off unless SL_RENDER_PROFILE names a CSV path. It
 * changes no simulation, no gameplay, no asset and no default. It is not a
 * speedup and it measures no optimization; it produces evidence a later
 * argument can rest on.
 *
 * COST WHEN OFF. One cached-static test per hooked call. The environment is
 * read once per process, at the first call; after that the frame path is two
 * calls (frame_begin, frame_end) that load a static int, see "off", and
 * return. No file is created, no GL entry point is resolved, no query object
 * is generated and no GL call is made.
 *
 * NEVER WAITS. The GPU result of a scope is polled on a LATER frame's
 * submission, and only through GL_QUERY_RESULT_AVAILABLE. GL_QUERY_RESULT is
 * never read until availability has been observed, and there is no glFinish,
 * no glGetInteger64v(GL_TIMESTAMP) sync point and no spin loop anywhere in
 * this subsystem. A scope whose result never arrives is reported as pending,
 * not waited for.
 *
 * src/game/ must never include this.
 */
#ifndef SL_GFX_PROFILE_H
#define SL_GFX_PROFILE_H

#ifdef __sgi
#error "sl_gfx_profile.h is native-only; guard the include with #ifndef __sgi"
#endif

/* Records in flight. A frame scope's query resolves within a frame or two, so
 * this is about a second of headroom at 60 Hz; the bound exists so that a
 * driver which stops retiring queries cannot grow this subsystem's memory or
 * its query-object count without limit. Full ring: the record is DROPPED - it
 * contributes no CSV row at all and is counted (dropped_ring). A dropped
 * record is visible downstream as a gap in the frame column, never as a row
 * with a made-up number in it. */
#define SL_GFX_PROFILE_RING      64

/* Scope nesting depth. Scopes are a strict LIFO stack; see the nesting rules
 * in sl_gfx_profile_scope_begin's comment. */
#define SL_GFX_PROFILE_MAX_DEPTH 4

/* Longest scope name stored, including the terminator. The importer requires
 * the name to match [a-z0-9_-]+, so a name is validated here rather than
 * written out and rejected at import. */
#define SL_GFX_PROFILE_NAME_MAX  24

/* Is the producer switched on for this process? Reads SL_RENDER_PROFILE once
 * and caches the answer, so a default run pays one getenv in the lifetime of
 * the process and nothing per frame beyond the cached test. Safe to call
 * before a GL context exists. */
int  sl_gfx_profile_active(void);

/* Called from the backend at both ends of a context's life. Everything cached
 * by this subsystem - the capability answer, the resolved entry points and the
 * query-object names - belongs to one context, so a new context must not
 * inherit names from a context that is gone. Deletes nothing: at
 * context-create there is nothing to delete, and at context-destroy
 * sl_gfx_profile_shutdown has already deleted the queries while the context
 * was still current. No-op when the producer is off. */
void sl_gfx_profile_context_invalidate(void);

/* Open a scope.
 *
 * NESTING RULES.
 *   - Scopes form a strict LIFO stack: the end call closes the most recently
 *     opened scope. Depth is bounded by SL_GFX_PROFILE_MAX_DEPTH; a begin past
 *     that bound opens no scope, is counted (misuse), and is still balanced by
 *     its matching end.
 *   - The outermost scope opens a SCOPE GROUP. Every record in the group takes
 *     its frame identity from the counter value at the moment that outermost
 *     scope closes, so all the rows of one frame carry one number and join.
 *     A record whose group never closes has no honest frame identity and
 *     produces no row (counted as misuse).
 *   - A nested scope is GPU-timed only on the GL_TIMESTAMP path, where the
 *     spec permits timestamp queries inside another query's interval. On the
 *     GL_TIME_ELAPSED fallback only ONE timer query may be active at a time,
 *     so a nested scope there is reported truthfully as unsupported with an
 *     empty GPU field - it is never silently attributed the outer scope's time.
 *   - Scope times are reported, never summed: an inner scope's span is
 *     contained in its outer scope's, and the importer treats each scope name
 *     as its own series.
 *
 * <name> must match [a-z0-9_-]+ and be shorter than SL_GFX_PROFILE_NAME_MAX.
 * A name that does not opens a balanced scope that records nothing, and is
 * counted (misuse). No-op when the producer is off. */
void sl_gfx_profile_scope_begin(const char *name);

/* Close the most recently opened scope. <name> is checked against the top of
 * the stack; a mismatch closes the top scope anyway (the stack is the
 * contract) and is counted (misuse). An end with no open scope is counted and
 * does nothing. Closing the outermost scope stamps the group's frame identity.
 * No-op when the producer is off. */
void sl_gfx_profile_scope_end(const char *name);

/* The whole-frame scope. frame_begin also runs the one non-blocking
 * availability pass over the records already in flight - that is the "later
 * submission" on which a previous frame's result is collected.
 *
 * TWO ENDS, because the frame loop has two. sl_ultra_shim.c presents
 * regularly only when the swap was not held (1434), so:
 *   frame_end()       the frame was presented  -> scope "frame"
 *   frame_end_held()  the swap was held        -> scope "frame-held"
 * Both close the scope and stamp the identity; they differ only in which
 * series the row lands in, which keeps "frame" equal to the regular-present
 * grid. No-op when the producer is off. */
void sl_gfx_profile_frame_begin(void);
void sl_gfx_profile_frame_end(void);
void sl_gfx_profile_frame_end_held(void);

/* Teardown, called by the backend while the GL context is STILL CURRENT.
 * Runs one last availability pass, writes every stamped but unresolved record
 * as pending with an empty GPU field, ends any query still active, deletes
 * every query name this process owns - including a short or partial pool that
 * was never used for timing - and closes the CSV. After it returns the
 * producer is off for the rest of the process. Whatever had been written
 * survives a partial failure: rows are flushed as they are written, and a
 * write failure stops further rows rather than discarding the file. */
void sl_gfx_profile_shutdown(void);

/* What the producer did, for the offline tests and for a one-line summary at
 * teardown. Counts, not measurements. */
struct sl_gfx_profile_counts {
    unsigned rows_ready;         /* rows written with a completed GPU time    */
    unsigned rows_pending;       /* rows written with an empty GPU field      */
    unsigned rows_unsupported;   /* rows written because GL cannot time this  */
    unsigned dropped_ring;       /* records with no free slot: no row at all  */
    unsigned dropped_duplicate;  /* same (frame,scope) again: no row at all   */
    unsigned dropped_error;      /* a GL query failure: no row at all         */
    unsigned misuse;             /* depth overflow, bad name, unbalanced end,
                                  * or a group that never closed             */
};
void sl_gfx_profile_counts_get(struct sl_gfx_profile_counts *out);

#ifdef SL_GFX_PROFILE_TEST
/* Offline mock build only. Returns every static to its initial state so one
 * test process can run independent scenarios, and closes the destination's
 * descriptor to exercise the write-failure latch. Not compiled into the game. */
void sl_gfx_profile_reset_for_test(void);
void sl_gfx_profile_break_file_for_test(void);
#endif

#endif /* SL_GFX_PROFILE_H */
