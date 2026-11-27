/* Offline tests for the SL_RENDER_PROFILE producer.
 *
 * WHAT THESE TESTS ARE. A pure mock executable: the producer compiled with
 * -DSL_GFX_PROFILE_TEST against the SYNTHETIC GL, clock and frame counter in
 * sl_gfx_profile_testgl.c. No window, no native graphics API, no renderer, no
 * game. Every GL version string, counter-bit answer, timestamp and elapsed
 * value below is FABRICATED by the test; they prove the producer's logic, and
 * they prove nothing about any real driver or any real frame.
 *
 * WHAT IS REAL IN THEM. The files. The create-only refusal, the
 * flush-per-row preservation and the write-failure latch run against actual
 * files in a scratch directory given on the command line, through the same
 * open(O_CREAT|O_EXCL) and stdio calls the game would use.
 *
 * Usage: test_sl_gfx_profile.exe <writable-scratch-directory>
 */
#include "sl_gfx_profile_testgl.h"
#include "sl_gfx_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, fails, cases, case_fails;
static const char *scratch;
static const char *current_case;

#define CHECK(cond, ...) do {                                                 \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            fails++; case_fails++;                                            \
            printf("    FAIL line %d: ", __LINE__);                           \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

static char g_path[1024];
static char g_buf[65536];

static const char *path_for(const char *name)
{
    sprintf(g_path, "%s/%s", scratch, name);
    remove(g_path);
    return g_path;
}

/* The destination as it is on disk, right now. */
static int read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    g_buf[0] = '\0';
    if (f == NULL) return -1;
    n = fread(g_buf, 1, sizeof g_buf - 1, f);
    g_buf[n] = '\0';
    fclose(f);
    return (int) n;
}

static int count_lines(void)
{
    int n = 0;
    const char *p = g_buf;
    while (*p != '\0') { if (*p == '\n') n++; p++; }
    return n;
}

/* Line <i> of the file just read, 0 = the header, without its newline. */
static const char *line_at(int i)
{
    static char out[512];
    const char *p = g_buf;
    int k = 0;
    size_t n = 0;
    while (*p != '\0') {
        if (k == i) {
            while (p[n] != '\0' && p[n] != '\n' && n + 1 < sizeof out) n++;
            memcpy(out, p, n);
            out[n] = '\0';
            return out;
        }
        if (*p == '\n') k++;
        p++;
    }
    out[0] = '\0';
    return out;
}

static void begin_case(const char *name)
{
    cases++;
    case_fails = 0;
    current_case = name;
    printf("  %s\n", name);
    sl_gfx_profile_reset_for_test();
    sl_testgl_reset();
    sl_testgl_env_value = NULL;
}

static void end_case(void)
{
    printf("    %s\n", case_fails == 0 ? "ok" : "FAILED");
}

/* A fake driver that can time: GL 4.6 compatibility, both extensions listed,
 * 64 usable counter bits. SYNTHETIC. */
static void driver_timestamp(int ready_after)
{
    tg.version    = "4.6.0 SYNTHETIC-TEST-DRIVER";
    tg.extensions = "GL_ARB_occlusion_query GL_ARB_timer_query GL_EXT_timer_query";
    tg.have_query_objects = 1;
    tg.have_begin_end     = 1;
    tg.have_query_counter = 1;
    tg.have_ui64          = 1;
    tg.ts_bits = 64;
    tg.el_bits = 64;
    tg.ready_after = ready_after;
}

/* A fake driver with the interval query only, no timestamp counter, and only
 * the EXT spelling of the 64-bit getter. SYNTHETIC. */
static void driver_elapsed_only(int ready_after)
{
    tg.version    = "2.1.2 SYNTHETIC-TEST-DRIVER";
    tg.extensions = "GL_ARB_occlusion_query GL_EXT_timer_query";
    tg.have_query_objects = 1;
    tg.have_begin_end     = 1;
    tg.have_query_counter = 0;
    tg.have_ui64          = 1;
    tg.ui64_ext_only      = 1;
    tg.ts_bits = -1;
    tg.el_bits = 32;
    tg.ready_after = ready_after;
}

/* One frame: open the frame scope, let the synthetic host clock and synthetic
 * GPU clock advance across the submission, close it, and advance both the
 * pumped-frame counter and the fake GPU's retire tick. */
static void run_frame(double cpu_ms, unsigned long long gpu_ns)
{
    sl_gfx_profile_frame_begin();
    tg.now_ms += cpu_ms;
    tg.gpu_ns += gpu_ns;
    sl_gfx_profile_frame_end();
    tg.frame++;
    sl_testgl_tick(1);
}

/* ---- the cases ----------------------------------------------------------- */

/* DEFAULT OFF. SL_RENDER_PROFILE unset: no file, no proc address resolved, no
 * GL call of any kind, no query object, no row. */
static void t_default_off(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T01 default off: unset SL_RENDER_PROFILE creates and calls nothing");
    p = path_for("t01.csv");
    sl_testgl_env_value = NULL;
    driver_timestamp(0);

    CHECK(sl_gfx_profile_active() == 0, "the producer reports itself active with no variable set");
    sl_gfx_profile_context_invalidate();
    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    sl_gfx_profile_scope_begin("extra");
    sl_gfx_profile_scope_end("extra");
    sl_gfx_profile_shutdown();

    CHECK(read_file(p) < 0, "a file was created at the unset destination");
    CHECK(tg.proc_calls == 0, "%ld proc address(es) resolved while off", tg.proc_calls);
    CHECK(tg.getstring_calls == 0, "%ld glGetString call(s) while off", tg.getstring_calls);
    CHECK(tg.gen_calls == 0, "%ld glGenQueries call(s) while off", tg.gen_calls);
    CHECK(tg.generated == 0, "%ld query object(s) generated while off", tg.generated);
    CHECK(tg.avail_calls == 0 && tg.result_calls == 0, "GL query calls were made while off");
    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_ready + c.rows_pending + c.rows_unsupported == 0, "rows were counted while off");
    CHECK(c.dropped_ring + c.dropped_duplicate + c.dropped_error + c.misuse == 0,
          "bookkeeping ran while off");
    end_case();
}

/* The timestamp path, with results that arrive two ticks late: a row appears
 * only once the result is available, the CPU and GPU numbers are exactly the
 * synthetic spans, and no result is ever read before availability. */
static void t_timestamp_delayed(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;
    int i;

    begin_case("T02 timestamp path, delayed-ready results, exact clock pairs");
    p = path_for("t02.csv");
    sl_testgl_env_value = p;
    driver_timestamp(2);

    for (i = 0; i < 6; i++) run_frame(4.0, 3000000ULL);

    sl_gfx_profile_counts_get(&c);
    /* frame F's end timestamp is issued at tick F and becomes available at
     * F+2, and the collection pass runs at the next frame's begin: so after
     * six frames the rows for frames 0..3 are written and 4 and 5 are not. */
    CHECK(c.rows_ready == 4, "rows_ready is %u, expected 4 before teardown", c.rows_ready);
    CHECK(c.rows_pending == 0, "rows_pending is %u before teardown", c.rows_pending);
    CHECK(tg.v_read_before_available == 0, "%ld result read(s) before availability",
          tg.v_read_before_available);

    sl_gfx_profile_shutdown();
    sl_gfx_profile_counts_get(&c);
    /* Teardown's own pass runs at tick 6, which retires frame 4; frame 5 is
     * reported pending with an empty GPU field, never waited for. */
    CHECK(c.rows_ready == 5, "rows_ready is %u after teardown, expected 5", c.rows_ready);
    CHECK(c.rows_pending == 1, "rows_pending is %u after teardown, expected 1", c.rows_pending);
    CHECK(tg.v_read_before_available == 0, "%ld result read(s) before availability",
          tg.v_read_before_available);

    CHECK(read_file(p) > 0, "the destination is missing");
    CHECK(count_lines() == 7, "%d line(s) in the file, expected 7", count_lines());
    CHECK(strcmp(line_at(0), "frame,scope,cpu_submit_ms,gpu_elapsed_ms,gpu_status") == 0,
          "header is '%s'", line_at(0));
    CHECK(strcmp(line_at(1), "0,frame,4.000000,3.000000,ready") == 0,
          "first row is '%s'", line_at(1));
    CHECK(strcmp(line_at(5), "4,frame,4.000000,3.000000,ready") == 0,
          "fifth row is '%s'", line_at(5));
    CHECK(strcmp(line_at(6), "5,frame,4.000000,,pending") == 0,
          "pending row is '%s'", line_at(6));
    end_case();
}

/* A full ring. Nothing retires, so the ring fills, every further record is
 * dropped outright - no row, no fabricated number - and teardown reports the
 * ring's contents as pending. */
static void t_ring_full(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;
    int i;

    begin_case("T03 full ring: records beyond the bound are dropped, not invented");
    p = path_for("t03.csv");
    sl_testgl_env_value = p;
    driver_timestamp(-1);                   /* never becomes available */

    for (i = 0; i < SL_GFX_PROFILE_RING + 5; i++) run_frame(2.0, 1000000ULL);

    sl_gfx_profile_counts_get(&c);
    CHECK(c.dropped_ring == 5, "dropped_ring is %u, expected 5", c.dropped_ring);
    CHECK(c.rows_ready + c.rows_pending + c.rows_unsupported == 0,
          "%u row(s) written while nothing had retired",
          c.rows_ready + c.rows_pending + c.rows_unsupported);

    sl_gfx_profile_shutdown();
    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_pending == SL_GFX_PROFILE_RING, "rows_pending is %u, expected %d",
          c.rows_pending, SL_GFX_PROFILE_RING);
    CHECK(c.rows_ready == 0, "rows_ready is %u, expected 0", c.rows_ready);
    CHECK(tg.v_read_before_available == 0, "a result was read before availability");

    CHECK(read_file(p) > 0, "the destination is missing");
    CHECK(count_lines() == SL_GFX_PROFILE_RING + 1, "%d line(s), expected %d",
          count_lines(), SL_GFX_PROFILE_RING + 1);
    CHECK(strstr(g_buf, ",,pending\n") != NULL, "no pending row has an empty GPU field");
    CHECK(strstr(g_buf, ",0.000000,pending") == NULL, "a pending row carries a zero GPU time");
    end_case();
}

/* No usable counter: every entry point resolves, the extension is even listed,
 * but GetQueryiv refuses both targets. Rows say unsupported with an empty GPU
 * field, and no query object is ever generated. */
static void t_unsupported_counter(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T04 unsupported: the context refuses both timer targets");
    p = path_for("t04.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);
    tg.ts_bits = -1;                        /* GetQueryiv raises INVALID_ENUM */
    tg.el_bits = -1;

    run_frame(5.0, 0);
    run_frame(5.0, 0);
    run_frame(5.0, 0);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_unsupported == 3, "rows_unsupported is %u, expected 3", c.rows_unsupported);
    CHECK(c.rows_ready == 0 && c.rows_pending == 0, "a timed row was written with no counter");
    CHECK(tg.gen_calls == 0, "%ld glGenQueries call(s) with no usable counter", tg.gen_calls);
    CHECK(read_file(p) > 0, "the destination is missing");
    CHECK(strcmp(line_at(1), "0,frame,5.000000,,unsupported") == 0,
          "first row is '%s'", line_at(1));
    end_case();
}

/* Zero counter bits is not support: the spec allows an implementation to say
 * the counter "contains no useful information", and a zero result in that case
 * is exactly the fabricated number this producer must not emit. */
static void t_zero_counter_bits(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T05 unsupported: GL_QUERY_COUNTER_BITS reports zero bits");
    p = path_for("t05.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);
    tg.ts_bits = 0;
    tg.el_bits = 0;

    run_frame(5.0, 0);
    run_frame(5.0, 0);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_unsupported == 2, "rows_unsupported is %u, expected 2", c.rows_unsupported);
    CHECK(c.rows_ready == 0, "a row was timed against a zero-bit counter");
    CHECK(tg.counter_calls == 0, "%ld timestamp(s) issued against a zero-bit counter",
          tg.counter_calls);
    end_case();
}

/* No query objects at all - a GL 1.1 software fallback. */
static void t_unsupported_no_entry_points(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T06 unsupported: this GL exposes no query objects");
    p = path_for("t06.csv");
    sl_testgl_env_value = p;
    tg.version = "1.1.0 SYNTHETIC-TEST-DRIVER";
    tg.extensions = "";
    tg.have_query_objects = 0;
    tg.have_begin_end = 0;
    tg.have_query_counter = 0;
    tg.have_ui64 = 0;

    run_frame(7.5, 0);
    run_frame(7.5, 0);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_unsupported == 2, "rows_unsupported is %u, expected 2", c.rows_unsupported);
    CHECK(read_file(p) > 0, "the destination is missing");
    CHECK(strcmp(line_at(1), "0,frame,7.500000,,unsupported") == 0,
          "first row is '%s'", line_at(1));
    end_case();
}

/* Nesting on the timestamp path: an inner scope gets its own measured row,
 * contained in the outer scope's, and neither is summed into the other. */
static void t_nesting_timestamp(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T07 nesting on the timestamp path: both scopes measured");
    p = path_for("t07.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);

    sl_gfx_profile_frame_begin();
    tg.now_ms += 1.0; tg.gpu_ns += 1000000ULL;      /* outer-only submission */
    sl_gfx_profile_scope_begin("inner");
    tg.now_ms += 2.0; tg.gpu_ns += 4000000ULL;      /* the inner scope */
    sl_gfx_profile_scope_end("inner");
    tg.now_ms += 1.0; tg.gpu_ns += 1000000ULL;
    sl_gfx_profile_frame_end();
    tg.frame++; sl_testgl_tick(1);

    run_frame(0.5, 500000ULL);                      /* a later submission collects */
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_ready == 3, "rows_ready is %u, expected 3", c.rows_ready);
    CHECK(c.misuse == 0, "misuse is %u on a correctly nested pair", c.misuse);
    CHECK(tg.v_double_begin == 0, "a second interval query was begun");
    CHECK(read_file(p) > 0, "the destination is missing");
    /* Row order follows the ring slot, not the order the scopes closed: the
     * collection pass walks the ring, and the outer scope took the lower slot
     * because it opened first. readiness.py groups by scope name and does not
     * depend on row order, so this is a fact about the file rather than a
     * requirement on it. The two scopes are reported, never summed: the
     * outer's 6 ms CONTAINS the inner's 4 ms. */
    CHECK(strcmp(line_at(1), "0,frame,4.000000,6.000000,ready") == 0,
          "outer row is '%s'", line_at(1));
    CHECK(strcmp(line_at(2), "0,inner,2.000000,4.000000,ready") == 0,
          "inner row is '%s'", line_at(2));
    end_case();
}

/* Nesting on the interval-only path: only one timer query may be active, so
 * the nested scope is reported unsupported rather than given the outer
 * scope's time, and the producer never begins a second interval query. */
static void t_nesting_elapsed(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T08 nesting on the interval path: the inner scope says unsupported");
    p = path_for("t08.csv");
    sl_testgl_env_value = p;
    driver_elapsed_only(0);

    sl_gfx_profile_frame_begin();
    tg.now_ms += 1.0; tg.gpu_ns += 1000000ULL;
    sl_gfx_profile_scope_begin("inner");
    tg.now_ms += 2.0; tg.gpu_ns += 4000000ULL;
    sl_gfx_profile_scope_end("inner");
    tg.now_ms += 1.0; tg.gpu_ns += 1000000ULL;
    sl_gfx_profile_frame_end();
    tg.frame++; sl_testgl_tick(1);

    run_frame(0.5, 500000ULL);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_unsupported == 1, "rows_unsupported is %u, expected 1 (the inner scope)",
          c.rows_unsupported);
    CHECK(c.rows_ready == 2, "rows_ready is %u, expected 2", c.rows_ready);
    CHECK(tg.v_double_begin == 0, "%ld second interval query begun", tg.v_double_begin);
    CHECK(tg.counter_calls == 0, "%ld timestamp(s) issued on the interval path",
          tg.counter_calls);
    CHECK(read_file(p) > 0, "the destination is missing");
    CHECK(strcmp(line_at(1), "0,inner,2.000000,,unsupported") == 0,
          "inner row is '%s'", line_at(1));
    CHECK(strcmp(line_at(2), "0,frame,4.000000,6.000000,ready") == 0,
          "outer row is '%s'", line_at(2));
    end_case();
}

/* A GL failure inside a query. The record that failed contributes no row, and
 * the producer turns honestly unsupported for the rest of the run rather than
 * writing a status the importer does not accept. */
static void t_query_failure(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T09 a failed availability query: dropped, then unsupported");
    p = path_for("t09.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);
    tg.fail_avail_at = 1;                   /* the first availability query errors */

    run_frame(3.0, 2000000ULL);
    run_frame(3.0, 2000000ULL);
    run_frame(3.0, 2000000ULL);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.dropped_error == 1, "dropped_error is %u, expected 1", c.dropped_error);
    CHECK(c.rows_ready == 0, "rows_ready is %u after the query failed", c.rows_ready);
    CHECK(c.rows_unsupported == 2, "rows_unsupported is %u, expected 2", c.rows_unsupported);
    CHECK(tg.v_read_before_available == 0, "a result was read after the failure");
    end_case();
}

/* CREATE-ONLY. An existing destination is refused, left byte-for-byte alone,
 * and the producer does not start: not one proc address, not one GL call. */
static void t_create_only(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;
    FILE *f;

    begin_case("T10 create-only: an existing destination is refused and untouched");
    p = path_for("t10.csv");
    f = fopen(p, "wb");
    CHECK(f != NULL, "the pre-existing file could not be written");
    if (f != NULL) { fputs("PRE-EXISTING EVIDENCE\n", f); fclose(f); }

    sl_testgl_env_value = p;
    driver_timestamp(0);
    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    sl_gfx_profile_shutdown();

    CHECK(sl_gfx_profile_active() == 0, "the producer stayed on after the refusal");
    CHECK(read_file(p) > 0, "the pre-existing file is gone");
    CHECK(strcmp(g_buf, "PRE-EXISTING EVIDENCE\n") == 0, "the pre-existing file was altered: '%s'", g_buf);
    CHECK(tg.proc_calls == 0, "%ld proc address(es) resolved after the refusal", tg.proc_calls);
    CHECK(tg.gen_calls == 0, "%ld glGenQueries call(s) after the refusal", tg.gen_calls);
    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_ready + c.rows_pending + c.rows_unsupported == 0, "rows were written after the refusal");
    end_case();
}

/* PARTIAL-WRITE PRESERVATION. Rows are flushed as they are written, so what
 * has been measured is on disk before any teardown runs - which is what makes
 * an abnormal exit survivable. */
static void t_partial_preserved(void)
{
    const char *p;
    int n;

    begin_case("T11 partial write: rows are on disk before teardown");
    p = path_for("t11.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);

    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);

    n = read_file(p);                       /* no shutdown has run */
    CHECK(n > 0, "nothing is on disk before teardown");
    CHECK(count_lines() == 4, "%d line(s) on disk before teardown, expected 4 "
          "(header plus three retired frames)", count_lines());
    CHECK(strcmp(line_at(1), "0,frame,4.000000,3.000000,ready") == 0,
          "first row on disk is '%s'", line_at(1));

    sl_gfx_profile_shutdown();
    CHECK(read_file(p) > 0, "the destination is missing after teardown");
    CHECK(count_lines() == 5, "%d line(s) after teardown, expected 5", count_lines());
    end_case();
}

/* The write-failure latch. The destination's descriptor is closed under the
 * stream, so the next row's flush fails with a real errno: the producer stops
 * adding rows, keeps the ones already written, and tears down cleanly. */
static void t_write_failure(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;
    unsigned rows_before;

    begin_case("T12 write failure: rows stop, what was written is kept");
    p = path_for("t12.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);

    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    sl_gfx_profile_counts_get(&c);
    rows_before = c.rows_ready;
    CHECK(rows_before == 2, "rows_ready is %u before the break, expected 2", rows_before);

    read_file(p);
    CHECK(count_lines() == 3, "%d line(s) before the break, expected 3", count_lines());

    sl_gfx_profile_break_file_for_test();
    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_ready == rows_before, "rows_ready grew to %u after the write failed",
          c.rows_ready);
    CHECK(read_file(p) > 0, "the destination is gone after the write failed");
    CHECK(count_lines() == 3, "%d line(s) after the write failed, expected the 3 kept",
          count_lines());
    CHECK(strcmp(line_at(0), "frame,scope,cpu_submit_ms,gpu_elapsed_ms,gpu_status") == 0,
          "the kept header is '%s'", line_at(0));
    end_case();
}

/* Teardown deletes exactly the query objects it generated, while the context
 * is still valid - and makes no GL call once the context is gone. */
static void t_teardown_deletes(void)
{
    const char *p;

    begin_case("T13 teardown: every query deleted while the context is valid");
    p = path_for("t13.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);

    run_frame(4.0, 3000000ULL);
    run_frame(4.0, 3000000ULL);
    CHECK(tg.generated == SL_GFX_PROFILE_RING * 2, "%ld query object(s) generated, expected %d",
          tg.generated, SL_GFX_PROFILE_RING * 2);
    CHECK(tg.gen_calls == 1, "%ld glGenQueries call(s), expected one", tg.gen_calls);
    CHECK(tg.deleted == 0, "queries were deleted before teardown");

    sl_gfx_profile_shutdown();
    CHECK(tg.deleted == SL_GFX_PROFILE_RING * 2, "%ld query object(s) deleted, expected %d",
          tg.deleted, SL_GFX_PROFILE_RING * 2);
    CHECK(tg.delete_calls == 1, "%ld glDeleteQueries call(s), expected one", tg.delete_calls);
    CHECK(tg.v_use_after_delete == 0, "a deleted query was used");

    /* The context is now gone. Nothing the backend may still call may touch GL. */
    tg.context_destroyed = 1;
    sl_gfx_profile_context_invalidate();
    sl_gfx_profile_frame_begin();
    sl_gfx_profile_frame_end();
    sl_gfx_profile_shutdown();
    CHECK(tg.v_call_after_context_destroyed == 0,
          "%ld GL call(s) after the context was destroyed", tg.v_call_after_context_destroyed);
    end_case();
}

/* Misuse is counted and contained: a depth overflow, an invalid scope name and
 * an unbalanced end leave the file valid and the stack balanced. */
static void t_misuse(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;
    int i;

    begin_case("T14 misuse: depth overflow, bad name, unbalanced end");
    p = path_for("t14.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);

    sl_gfx_profile_scope_end("nothing-is-open");        /* +1 misuse */
    sl_gfx_profile_scope_begin("Bad Name");             /* +1 misuse, balanced */
    sl_gfx_profile_scope_end("Bad Name");

    sl_gfx_profile_frame_begin();                       /* depth 1 */
    for (i = 0; i < 5; i++) sl_gfx_profile_scope_begin("deep");
    for (i = 0; i < 5; i++) sl_gfx_profile_scope_end("deep");
    sl_gfx_profile_frame_end();
    tg.frame++; sl_testgl_tick(1);

    run_frame(1.0, 1000000ULL);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    /* one unbalanced end, one invalid name, two begins past the depth bound
     * (frame + three nested fill the stack), and three of the five "deep"
     * scopes share frame 0 with the first, which the duplicate rule drops. */
    CHECK(c.misuse == 4, "misuse is %u, expected 4", c.misuse);
    CHECK(c.dropped_duplicate == 2, "dropped_duplicate is %u, expected 2", c.dropped_duplicate);
    CHECK(c.rows_ready >= 2, "rows_ready is %u, expected at least 2", c.rows_ready);
    CHECK(read_file(p) > 0, "the destination is missing");
    CHECK(strstr(g_buf, "Bad Name") == NULL, "an invalid scope name reached the file");
    end_case();
}

/* The duplicate rule. The pumped-frame counter does not advance for every
 * render, and readiness.py rejects a repeated (frame,scope) pair outright, so
 * a second record for a frame number already used is dropped here. */
static void t_duplicate_frame(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T15 duplicate (frame,scope) pairs are dropped, not written");
    p = path_for("t15.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);

    /* Two frame scopes without the pumped-frame counter advancing. */
    sl_gfx_profile_frame_begin();
    tg.now_ms += 2.0; tg.gpu_ns += 1000000ULL;
    sl_gfx_profile_frame_end();
    sl_testgl_tick(1);
    sl_gfx_profile_frame_begin();
    tg.now_ms += 2.0; tg.gpu_ns += 1000000ULL;
    sl_gfx_profile_frame_end();
    tg.frame++; sl_testgl_tick(1);

    run_frame(2.0, 1000000ULL);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.dropped_duplicate == 1, "dropped_duplicate is %u, expected 1", c.dropped_duplicate);
    CHECK(read_file(p) > 0, "the destination is missing");
    CHECK(count_lines() == 3, "%d line(s), expected 3 (two distinct frames)", count_lines());
    end_case();
}

/* glGenQueries failing at detection: no path is taken and rows say
 * unsupported. */
static void t_gen_failure(void)
{
    const char *p;
    struct sl_gfx_profile_counts c;

    begin_case("T16 a failed or short query pool: unsupported, and every "
               "allocated name still released");
    p = path_for("t16.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);
    tg.gen_fails = 1;

    run_frame(6.0, 0);
    run_frame(6.0, 0);
    sl_gfx_profile_shutdown();

    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_unsupported == 2, "rows_unsupported is %u, expected 2", c.rows_unsupported);
    CHECK(c.rows_ready == 0, "a row was timed with no query objects");
    CHECK(tg.counter_calls == 0, "%ld timestamp(s) issued with no query objects",
          tg.counter_calls);
    CHECK(tg.generated == 0, "%ld name(s) generated by a failed call", tg.generated);
    CHECK(tg.generated == tg.deleted,
          "%ld name(s) allocated but %ld deleted after a failed generation",
          tg.generated, tg.deleted);

    sl_gfx_profile_reset_for_test();
    sl_testgl_reset();
    p = path_for("t16b.csv");
    sl_testgl_env_value = p;
    driver_timestamp(0);
    tg.gen_short = 1;                       /* a zero name comes back */
    run_frame(6.0, 0);
    sl_gfx_profile_shutdown();
    sl_gfx_profile_counts_get(&c);
    CHECK(c.rows_unsupported == 1, "a short pool was accepted (rows_unsupported %u)",
          c.rows_unsupported);
    CHECK(tg.generated == SL_GFX_PROFILE_RING * 2 - 1,
          "%ld name(s) generated, expected %d from a short pool",
          tg.generated, SL_GFX_PROFILE_RING * 2 - 1);
    CHECK(tg.generated == tg.deleted,
          "%ld name(s) allocated from a short pool but %ld deleted - every name "
          "the driver handed over has to be released", tg.generated, tg.deleted);
    CHECK(tg.delete_calls == 1, "%ld glDeleteQueries call(s), expected one",
          tg.delete_calls);
    CHECK(tg.v_use_after_delete == 0, "a deleted query was used");
    CHECK(tg.counter_calls == 0, "%ld timestamp(s) issued against a short pool",
          tg.counter_calls);
    tg.context_destroyed = 1;
    sl_gfx_profile_shutdown();
    CHECK(tg.v_call_after_context_destroyed == 0,
          "%ld GL call(s) after the context was destroyed",
          tg.v_call_after_context_destroyed);
    end_case();
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <writable-scratch-directory>\n", argv[0]);
        return 2;
    }
    scratch = argv[1];
    printf("SL_RENDER_PROFILE producer: offline mock tests\n");
    printf("  SYNTHETIC GL, clock and frame counter; no window, no GPU, no game.\n");
    printf("  ring %d, max depth %d\n\n", SL_GFX_PROFILE_RING, SL_GFX_PROFILE_MAX_DEPTH);

    t_default_off();
    t_timestamp_delayed();
    t_ring_full();
    t_unsupported_counter();
    t_zero_counter_bits();
    t_unsupported_no_entry_points();
    t_nesting_timestamp();
    t_nesting_elapsed();
    t_query_failure();
    t_create_only();
    t_partial_preserved();
    t_write_failure();
    t_teardown_deletes();
    t_misuse();
    t_duplicate_frame();
    t_gen_failure();

    printf("\n%d case(s), %d check(s), %d failure(s)\n", cases, checks, fails);
    (void) current_case;
    return fails == 0 ? 0 : 1;
}
