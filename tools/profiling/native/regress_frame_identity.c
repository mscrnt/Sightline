/* Regression: which physical frame a row joins to.
 *
 * WHAT THIS REPRODUCES. The ordering of the real frame loop in
 * src/platform/sl_ultra_shim.c, which is the thing the first version of this
 * producer got wrong:
 *
 *   1425-1441  the previous frame's present: the hold decision, then
 *              sl_gfx_end_frame(), then - only when NOT held - the regular
 *              present is noted
 *   1457       sl_gfx_begin_frame()        <- the frame opens
 *   1466-1496  pacing, game and phase work, probes   (sl_frame unchanged)
 *   1497       sl_frame++                  <- the counter advances HERE
 *
 * So a frame that opens while the counter reads F is presented when it reads
 * F+1. Reading the counter at the scope's OPEN labels the row F, and the row
 * then joins to a different physical frame; on an alternating present grid the
 * number it lands on can belong to a frame whose swap was held.
 *
 * WHAT IS ASSERTED. Not "the producer agrees with itself" - the expectation is
 * taken from the loop: a row's frame must equal the counter value at the
 * moment the frame ENDED, because that is the value the regular present is
 * recorded at. The first presented frame therefore has to read 1, not 0, and
 * that single check is the original defect.
 *
 * Also asserted: nested scopes carry their enclosing frame's identity, a held
 * frame never appears in the regular-present series, a repeated counter value
 * produces one row rather than a duplicate the importer would reject, every
 * query name allocated is deleted, and no result is read before availability.
 *
 * SYNTHETIC. The GL, the clocks and the counter are all fabricated by the stub
 * (sl_gfx_profile_testgl.c). There is no window, no graphics API and no game
 * here; the executable links neither opengl32 nor SDL. The numbers prove the
 * producer's join logic and nothing about any real driver or frame.
 *
 * Usage: regress_frame_identity.exe <writable-scratch-directory>
 */
#include "sl_gfx_profile_testgl.h"
#include "sl_gfx_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, fails;

#define CHECK(cond, ...) do {                                                 \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            fails++;                                                          \
            printf("    FAIL line %d: ", __LINE__);                           \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

#define MAXE 64

/* What the loop says each ended frame's identity must be. */
static unsigned exp_frame[MAXE];
static int      exp_held[MAXE];
static int      n_ended;
static int      frame_open;

/* Rows as they came out of the producer. */
static unsigned row_frame[MAXE * 4];
static char     row_scope[MAXE * 4][24];
static int      n_rows;

static char g_path[1024];
static char g_buf[65536];

/* One iteration of the frame loop, in the real order. <end_held> describes the
 * frame being ended at the top of this iteration; <advance> is whether the
 * counter increments at 1497, which it does not when a render shares a pumped
 * frame with the one before it. */
static void iter(int end_held, int advance)
{
    if (frame_open) {
        /* 1425-1441. The identity the regular present is recorded at is the
         * counter value NOW, before any further increment. */
        exp_frame[n_ended] = tg.frame;
        exp_held[n_ended]  = end_held;
        n_ended++;
        if (end_held) sl_gfx_profile_frame_end_held();
        else          sl_gfx_profile_frame_end();
        frame_open = 0;
    }
    sl_gfx_profile_frame_begin();                       /* 1457 */
    frame_open = 1;
    tg.now_ms += 2.0; tg.gpu_ns += 1500000ULL;          /* 1466-1496, part one */
    sl_gfx_profile_scope_begin("dl-walk");              /* a nested scope      */
    tg.now_ms += 1.0; tg.gpu_ns += 1000000ULL;
    sl_gfx_profile_scope_end("dl-walk");
    tg.now_ms += 1.0; tg.gpu_ns += 500000ULL;           /* 1466-1496, part two */
    if (advance) tg.frame++;                            /* 1497                */
    sl_testgl_tick(1);
}

static int read_rows(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *p;
    size_t n;

    if (f == NULL) return 0;
    n = fread(g_buf, 1, sizeof g_buf - 1, f);
    g_buf[n] = '\0';
    fclose(f);

    n_rows = 0;
    p = strchr(g_buf, '\n');                   /* skip the header */
    if (p == NULL) return 0;
    p++;
    while (*p != '\0' && n_rows < (int) (sizeof row_frame / sizeof row_frame[0])) {
        char *comma, *eol;
        eol = strchr(p, '\n');
        if (eol == NULL) break;
        *eol = '\0';
        comma = strchr(p, ',');
        if (comma != NULL) {
            char *c2;
            *comma = '\0';
            row_frame[n_rows] = (unsigned) strtoul(p, NULL, 10);
            c2 = strchr(comma + 1, ',');
            if (c2 != NULL) {
                size_t len = (size_t) (c2 - (comma + 1));
                if (len >= sizeof row_scope[0]) len = sizeof row_scope[0] - 1;
                memcpy(row_scope[n_rows], comma + 1, len);
                row_scope[n_rows][len] = '\0';
                n_rows++;
            }
        }
        p = eol + 1;
    }
    return n_rows;
}

static int rows_with(unsigned frame, const char *scope)
{
    int i, k = 0;
    for (i = 0; i < n_rows; i++)
        if (row_frame[i] == frame && strcmp(row_scope[i], scope) == 0) k++;
    return k;
}

/* Is this ended frame the FIRST end to use its counter value in <scope>'s
 * series? A later end sharing the value is the repeated-counter case, and the
 * producer must drop it rather than write a pair the importer rejects. */
static int first_use(int e, int held)
{
    int i;
    for (i = 0; i < e; i++)
        if (exp_frame[i] == exp_frame[e] && exp_held[i] == held) return 0;
    return 1;
}

/* The same question for the nested scope, whose series is shared by held and
 * presented frames: only the counter value distinguishes its rows. */
static int first_identity(int e)
{
    int i;
    for (i = 0; i < e; i++)
        if (exp_frame[i] == exp_frame[e]) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    /* end_held, advance. Step 0 ends nothing. The holds sit away from the
     * non-advancing step so a held identity and a repeated identity are
     * separate cases rather than one confounded one. */
    static const struct { int held, advance; } SCHED[] = {
        { 0, 1 },   /* 0: opens the first frame                              */
        { 0, 1 },   /* 1: ends it, presented                                 */
        { 0, 1 },   /* 2                                                     */
        { 1, 1 },   /* 3: ends a HELD frame                                  */
        { 0, 0 },   /* 4: presented; the counter does NOT advance after it    */
        { 0, 1 },   /* 5: ends a frame sharing the previous counter value     */
        { 1, 1 },   /* 6: ends a HELD frame                                  */
        { 0, 1 },   /* 7                                                     */
        { 0, 1 }    /* 8                                                     */
    };
    const int NS = (int) (sizeof SCHED / sizeof SCHED[0]);
    struct sl_gfx_profile_counts c;
    int i, e, presented_expected = 0, held_expected = 0, dup_expected = 0;
    int ident_expected = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <writable-scratch-directory>\n", argv[0]);
        return 2;
    }
    sprintf(g_path, "%s/regress-frame-identity.csv", argv[1]);
    remove(g_path);

    printf("Frame-identity regression (SYNTHETIC GL, clock and counter)\n");

    sl_gfx_profile_reset_for_test();
    sl_testgl_reset();
    sl_testgl_env_value = g_path;
    tg.version    = "4.6.0 SYNTHETIC-TEST-DRIVER";
    tg.extensions = "GL_ARB_occlusion_query GL_ARB_timer_query";
    tg.have_query_objects = tg.have_begin_end = 1;
    tg.have_query_counter = tg.have_ui64 = 1;
    tg.ts_bits = tg.el_bits = 64;
    tg.ready_after = 0;

    for (i = 0; i < NS; i++) iter(SCHED[i].held, SCHED[i].advance);
    /* Close the last open frame the way the loop would on its next pass. */
    exp_frame[n_ended] = tg.frame; exp_held[n_ended] = 0; n_ended++;
    sl_gfx_profile_frame_end();
    frame_open = 0;
    sl_gfx_profile_shutdown();

    CHECK(read_rows(g_path) > 0, "the destination has no rows");
    sl_gfx_profile_counts_get(&c);

    /* ---- the defect itself ------------------------------------------------
     * The first frame opened while the counter read 0 and was presented while
     * it read 1. A row saying 0 is the original bug. */
    CHECK(n_ended > 0 && exp_frame[0] == 1,
          "the harness expected the first end at counter 1, got %u", exp_frame[0]);
    CHECK(rows_with(1, "frame") == 1,
          "the first presented frame is not reported at counter 1 (rows at 1: %d)",
          rows_with(1, "frame"));
    CHECK(rows_with(0, "frame") == 0,
          "a row was emitted at counter 0, which is the open-time value, not the "
          "present-time value");

    /* ---- every end lands on the value the present was recorded at ---------
     * Exactly one row per (counter, series), whether this end wrote it or an
     * earlier end that shared the counter value did: a repeat must not add a
     * second row, and must not displace the first. */
    for (e = 0; e < n_ended; e++) {
        const char *scope = exp_held[e] ? "frame-held" : "frame";
        if (first_use(e, exp_held[e])) {
            if (exp_held[e]) held_expected++; else presented_expected++;
        } else {
            dup_expected++;
        }
        if (first_identity(e)) ident_expected++;
        CHECK(rows_with(exp_frame[e], scope) == 1,
              "end %d: counter %u series %s has %d row(s), expected exactly 1",
              e, exp_frame[e], scope, rows_with(exp_frame[e], scope));
        /* The nested scope of that same frame must carry the SAME identity. */
        CHECK(rows_with(exp_frame[e], "dl-walk") == 1,
              "end %d: the nested scope at counter %u has %d row(s), expected "
              "exactly 1 - a nested scope must join its enclosing frame",
              e, exp_frame[e], rows_with(exp_frame[e], "dl-walk"));
    }

    /* The repeat shows up here: one row per distinct (counter, series), so a
     * ninth end sharing a counter value adds nothing to the file. */
    CHECK(n_rows == presented_expected + held_expected + ident_expected,
          "%d row(s) in the file, expected %d (%d presented + %d held + %d nested)",
          n_rows, presented_expected + held_expected + ident_expected,
          presented_expected, held_expected, ident_expected);

    /* ---- a held frame is never read as a presented one -------------------- */
    for (e = 0; e < n_ended; e++) {
        if (!exp_held[e]) continue;
        CHECK(rows_with(exp_frame[e], "frame") == 0,
              "counter %u was a HELD frame but appears in the regular-present "
              "series", exp_frame[e]);
    }
    CHECK(held_expected == 2, "the schedule should hold 2 frames, counted %d", held_expected);
    CHECK(dup_expected == 1, "the schedule should repeat 1 counter value, counted %d",
          dup_expected);

    /* ---- the file is importable: no duplicate (frame,scope) pair ---------- */
    {
        int dup = 0;
        for (i = 0; i < n_rows; i++) {
            int j;
            for (j = i + 1; j < n_rows; j++)
                if (row_frame[i] == row_frame[j]
                    && strcmp(row_scope[i], row_scope[j]) == 0) dup++;
        }
        CHECK(dup == 0, "%d duplicate (frame,scope) pair(s) in the file", dup);
    }
    CHECK(c.dropped_duplicate == 2,
          "dropped_duplicate is %u, expected 2 (the repeated counter value's "
          "frame and nested scope)", c.dropped_duplicate);

    /* ---- the query pool, and the no-wait rule ----------------------------- */
    CHECK(tg.generated == tg.deleted,
          "%ld query name(s) generated but %ld deleted", tg.generated, tg.deleted);
    CHECK(tg.v_read_before_available == 0, "%ld result read(s) before availability",
          tg.v_read_before_available);
    CHECK(tg.v_use_after_delete == 0, "a deleted query was used");
    tg.context_destroyed = 1;
    sl_gfx_profile_shutdown();
    CHECK(tg.v_call_after_context_destroyed == 0,
          "%ld GL call(s) after the context was destroyed",
          tg.v_call_after_context_destroyed);

    printf("  ends %d: %d presented, %d held, %d repeated; rows %d\n",
           n_ended, presented_expected, held_expected, dup_expected, n_rows);
    printf("  fixture: %s\n", g_path);
    printf("\n%d check(s), %d failure(s)\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
