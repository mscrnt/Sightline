/* THE BACKGROUND PASS'S WHOLE VERTEX OUTPUT - the part that needs no driver.
 *
 * Read sl_bggpu_out_probe.h first: it holds the contract, the per-field oracle
 * and the two limits this probe does not pretend to close.
 *
 * This file builds the attribute inputs, computes the reference record for
 * every case, and owns the ONE comparison loop that a driver run would be
 * judged by - which is also the loop the negative controls below drive with
 * fabricated data, so what they exercise is the code that will judge a driver
 * and not a second copy of it.
 *
 * NO DRIVER IS REACHED FROM HERE. The no-GL build has no GL entry point
 * compiled in and does not link opengl32. The GL build additionally requires
 * --gl at run time. Neither is invoked by this stage.
 */
/* The same include order the depth probe uses: the synthetic GL shim first, so
 * the module under test is linked against it rather than a driver, then the
 * probe contract, then the real policy and settings whose own initialisers run
 * in main. The generated shader header is NOT included here - the pinned text
 * is reached through the accessors in sl_bggpu_depth_probe.c, which is the one
 * translation unit that holds it. */
#include "sl_gfx_bggpu_testgl.h"

#include "sl_bggpu_out_probe.h"
#include "sl_bgop_sink.h"

#include "sl_gfx_bggpu.h"
#include "sl_perf.h"
#include "sl_settings.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the check ledger --------------------------------------------------- */

static int checks, fails;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        checks++;                                                             \
        if (cond) { printf("    ok   "); }                                     \
        else      { printf("    FAIL "); fails++; }                            \
        printf(__VA_ARGS__);                                                  \
        printf("\n");                                                         \
    } while (0)

/* ---- the capture record layout, defined once --------------------------- */

static const struct sl_bgop_field g_fields[SL_BGOP_NFIELDS] = {
    { "gl_Position",     "position", 4,  0 },
    { "gl_FrontColor",   "front",    4,  4 },
    { "gl_BackColor",    "back",     4,  8 },
    { "gl_FogFragCoord", "fog",      1, 12 },
    { "gl_TexCoord[0]",  "tex0",     4, 13 },
    { "gl_TexCoord[1]",  "tex1",     4, 17 }
};

const struct sl_bgop_field *sl_bgop_fields(void) { return g_fields; }

/* ---- the attribute inputs ----------------------------------------------- */

/* THE TWO TEXTURE MATRICES, chosen so the product cannot round.
 *
 * `gl_TextureMatrix[i] * gl_MultiTexCoordi` carries no `precise`, so the driver
 * may reassociate and fuse it. Every entry here, and every coordinate below, is
 * a small dyadic rational, so each of the four products and each partial sum is
 * exactly representable in binary32 and NO association rounds. The comparison
 * for these cases is therefore exact because the arithmetic is exact - not
 * because a driver was assumed to evaluate in the reference's order.
 *
 * Column order, as the module's matrices are: M[col * 4 + row]. Both have
 * cross terms and a translation, so neither is diagonal or an identity, and the
 * two differ in every column. */
static const float M0_EXACT[16] = {
     2.0f,   0.5f,   0.0f,  0.0f,     /* column 0 */
    -1.0f,   0.25f,  4.0f,  0.0f,     /* column 1 */
     0.0f,   0.0f,   1.0f,  0.0f,     /* column 2 */
     1.5f,  -2.25f,  0.75f, 1.0f      /* column 3 */
};
static const float M1_EXACT[16] = {
     0.5f,  -2.0f,   0.0f,  0.0f,
     4.0f,   0.125f,-1.0f,  0.0f,
     0.0f,   0.0f,   2.0f,  0.0f,
    -0.5f,   3.25f,  1.5f,  1.0f
};

/* THE ROUNDING PAIR, for the advisory cases. Non-dyadic entries and
 * coordinates, so the four products and three sums DO round and the
 * association the driver chooses can change the last bits. Measured and
 * reported; never counted. */
static const float M0_ROUND[16] = {
     0.1f,   0.7f,   0.0f,  0.0f,
     0.3f,   1.1f,   0.9f,  0.0f,
     0.0f,   0.0f,   1.3f,  0.0f,
     0.17f, -0.93f,  0.41f, 1.0f
};
static const float M1_ROUND[16] = {
     1.7f,   0.19f,  0.0f,  0.0f,
    -0.23f,  0.61f,  1.9f,  0.0f,
     0.0f,   0.0f,   0.7f,  0.0f,
     0.37f,  0.11f, -0.29f, 1.0f
};

/* FOUR ATTRIBUTE VARIANTS, cycled across the depth table's cases so no case is
 * measured at a default. White, zero or default-coordinate inputs would make
 * these checks vacuous, which is the one thing the task forbids outright, so
 * every channel differs from every other, the alphas cover both ends, the fog
 * values differ in sign and magnitude, and the two coordinate sets are
 * independent of each other in every variant. */
struct variant {
    unsigned char col[4];
    float         fog;
    float         uv0[2], uv1[2];
    int           rounding;
    const char   *why;
};

static const struct variant VARIANTS[] = {
    { { 31, 138, 195, 93 },   0.375f,  {  3.0f, -5.0f }, { -1.25f,  6.0f }, 0,
      "four distinct channels, a partial alpha, a dyadic fog and two "
      "independent dyadic coordinate sets" },
    { { 0, 255, 1, 254 },    -2.5f,    { -8.0f,  0.5f }, {  2.75f, -3.0f }, 0,
      "the byte endpoints 0 and 255 beside 1 and 254, so the normalisation is "
      "exercised at both ends and one step inside each" },
    { { 200, 7, 64, 255 },    1234.5f, {  1.5f,  2.25f }, { -0.75f, 4.0f }, 0,
      "an opaque alpha with a large dyadic fog, which a 16-bit fog path would "
      "not carry" },
    { { 128, 127, 129, 1 },   0.0f,    {  0.1f,  0.3f }, {  0.7f,  1.9f }, 1,
      "the three bytes either side of half scale, a zero fog, and NON-DYADIC "
      "coordinates against the rounding matrices - the advisory texcoord case, "
      "where association is free to matter" }
};
#define NVARIANTS ((int) (sizeof VARIANTS / sizeof VARIANTS[0]))

static struct sl_bgop_attr g_attrs[SL_BGOP_MAX_CASES];
static int g_n;

const struct sl_bgop_attr *sl_bgop_attrs(void) { return g_attrs; }

int sl_bgop_build(void)
{
    int i, n = sl_bgdp_build_cases();
    if (n <= 0) return 0;
    if (n > SL_BGOP_MAX_CASES) n = SL_BGOP_MAX_CASES;
    for (i = 0; i < n; i++) {
        const struct variant *v = &VARIANTS[i % NVARIANTS];
        struct sl_bgop_attr *a = &g_attrs[i];
        memcpy(a->col, v->col, sizeof a->col);
        a->fog = v->fog;
        a->uv[0][0] = v->uv0[0]; a->uv[0][1] = v->uv0[1];
        a->uv[1][0] = v->uv1[0]; a->uv[1][1] = v->uv1[1];
        memcpy(a->M[0], v->rounding ? M0_ROUND : M0_EXACT, sizeof a->M[0]);
        memcpy(a->M[1], v->rounding ? M1_ROUND : M1_EXACT, sizeof a->M[1]);
        a->tex_class = v->rounding ? SL_BGOP_ADVISORY : SL_BGOP_EXACT;
        a->why = v->why;
    }
    g_n = n;
    return n;
}

int sl_bgop_class(int icase, int field)
{
    if (field == SL_BGOP_TEX0 || field == SL_BGOP_TEX1)
        return g_attrs[icase].tex_class;
    /* THE EXISTING DENORMAL ADVISORY, CARRIED FORWARD AND STILL SEPARATE. A
     * case the depth probe marks advisory has a denormal w, and GLSL's default
     * precision does not require denormal support: a driver that flushes to
     * zero takes the other side of the `c.w > 0.0` test, which changes
     * gl_Position.w and the clamp that writes gl_Position.z. So the POSITION
     * field of such a case is measured and reported and kept out of the count,
     * exactly as that probe keeps its z out of its own. The colour, the fog and
     * the texture coordinates do not depend on w, so they stay exact even for
     * that case - the advisory is identified field by field and is not a
     * tolerance applied to the case as a whole. */
    if (field == SL_BGOP_POS && sl_bgdp_cases()[icase].advisory)
        return SL_BGOP_ADVISORY;
    return SL_BGOP_EXACT;
}

/* ---- THE REFERENCE ------------------------------------------------------ */

/* The specified conversion from unsigned normalised fixed point: c / (2^b - 1).
 * One division, which IEEE-754 requires to be correctly rounded. */
static float unorm8(unsigned char c) { return (float) c / 255.0f; }

/* GLSL `mat4 * vec4` with M in column order: r[row] = sum over col of
 * M[col][row] * v[col]. Written in that order; for the EXACT cases no order
 * rounds, and for the ADVISORY cases this order is one of several a driver may
 * legitimately choose, which is why those are not counted. */
static void mat4_vec4(const float *M, const float *v, float *r)
{
    int row;
    for (row = 0; row < 4; row++)
        r[row] = M[0 * 4 + row] * v[0] + M[1 * 4 + row] * v[1]
               + M[2 * 4 + row] * v[2] + M[3 * 4 + row] * v[3];
}

void sl_bgop_reference(int i, struct sl_bgop_expect *e)
{
    const struct sl_bgdp_case *k = &sl_bgdp_cases()[i];
    const struct sl_bgop_attr *a = &g_attrs[i];
    struct sl_bgdp_ref r;
    /* A two-component texcoord array leaves z = 0 and w = 1: the specified
     * defaults for components an array does not supply. */
    float t0[4], t1[4];
    float mc0[4], mc1[4];
    int j;

    memset(e, 0, sizeof *e);
    sl_bgdp_reference(k, &r);

    /* gl_Position = c, AFTER the depth rewrite: x, y and w are the clip
     * product's own, z is what this case's mode leaves behind. The depth probe
     * compared only this z; x, y and w have never been compared with anything. */
    e->v[g_fields[SL_BGOP_POS].offset + 0] = r.c[0];
    e->v[g_fields[SL_BGOP_POS].offset + 1] = r.c[1];
    e->v[g_fields[SL_BGOP_POS].offset + 2] = r.z;
    e->v[g_fields[SL_BGOP_POS].offset + 3] = r.c[3];

    for (j = 0; j < 4; j++) {
        float u = unorm8(a->col[j]);
        e->v[g_fields[SL_BGOP_FRONT].offset + j] = u;
        e->v[g_fields[SL_BGOP_BACK].offset + j] = u;
    }

    e->v[g_fields[SL_BGOP_FOG].offset] = a->fog;

    mc0[0] = a->uv[0][0]; mc0[1] = a->uv[0][1]; mc0[2] = 0.0f; mc0[3] = 1.0f;
    mc1[0] = a->uv[1][0]; mc1[1] = a->uv[1][1]; mc1[2] = 0.0f; mc1[3] = 1.0f;
    mat4_vec4(a->M[0], mc0, t0);
    mat4_vec4(a->M[1], mc1, t1);
    for (j = 0; j < 4; j++) {
        e->v[g_fields[SL_BGOP_TEX0].offset + j] = t0[j];
        e->v[g_fields[SL_BGOP_TEX1].offset + j] = t1[j];
    }
}

/* ---- float bits, for the report ---------------------------------------- */

static unsigned bits_of(float f)
{
    unsigned u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static float ulp_up(float f)
{
    unsigned u = bits_of(f);
    float out;
    u += 1u;                 /* one step away from zero in the pattern */
    memcpy(&out, &u, sizeof out);
    return out;
}

/* ---- THE ONE COMPARISON LOOP ------------------------------------------- */

struct sl_bgop_tally {
    int compared;            /* components compared bit for bit              */
    int differing;           /* of those, how many differed                 */
    int advisory;            /* components measured but not counted          */
    int advisory_differing;
};

/* Bit for bit, through memcmp, with no tolerance anywhere. An ADVISORY field's
 * components are measured and reported and never enter `differing`, which is
 * what keeps the one unspecified product out of the verdict WITHOUT loosening
 * any comparison: a component is either compared exactly or reported as
 * measured. */
static void compare_all(const float *records, int report, struct sl_bgop_tally *t)
{
    int i, f, j;
    memset(t, 0, sizeof *t);
    for (i = 0; i < g_n; i++) {
        const float *rec = records + (size_t) i * SL_BGOP_RECORD_FLOATS;
        struct sl_bgop_expect e;
        sl_bgop_reference(i, &e);
        for (f = 0; f < SL_BGOP_NFIELDS; f++) {
            int cls = sl_bgop_class(i, f);
            for (j = 0; j < g_fields[f].comps; j++) {
                int o = g_fields[f].offset + j;
                int same = memcmp(&rec[o], &e.v[o], sizeof rec[o]) == 0;
                if (cls == SL_BGOP_ADVISORY) {
                    t->advisory++;
                    if (!same) {
                        t->advisory_differing++;
                        if (report)
                            printf("    ADVISORY %-28s %s[%d]: measured %.9g "
                                   "(0x%08x), reference %.9g (0x%08x)\n",
                                   sl_bgdp_cases()[i].name, g_fields[f].label, j,
                                   (double) rec[o], bits_of(rec[o]),
                                   (double) e.v[o], bits_of(e.v[o]));
                    }
                    continue;
                }
                t->compared++;
                if (same) continue;
                t->differing++;
                if (report)
                    printf("    FAIL %-28s %s[%d]: measured %.9g (0x%08x), "
                           "reference %.9g (0x%08x)\n",
                           sl_bgdp_cases()[i].name, g_fields[f].label, j,
                           (double) rec[o], bits_of(rec[o]),
                           (double) e.v[o], bits_of(e.v[o]));
            }
        }
    }
}

/* ---- THE READINESS GATE ------------------------------------------------ */

/* See the long note in sl_bggpu_out_probe.h. One rule, applied by the driver
 * runner and again by the caller below before it compares anything, and driven
 * on the CPU with injected counts by st_readiness_gate. */
int sl_bgop_counts_ready(const unsigned *counts, int n, char *why, int whycap)
{
    unsigned want = (n > 0) ? (unsigned) n : 0u;
    if (why != NULL && whycap > 0) why[0] = '\0';
    if (counts == NULL || n <= 0) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap, "no counts were reported at all");
        return 0;
    }
    if (counts[0] != 0u) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "the driver reported %u GL error(s) during setup, the cases "
                     "or cleanup; records taken alongside a GL error are not "
                     "evidence of agreement however exactly they match, so "
                     "nothing is compared (captures %u of %u, primitives %u)",
                     counts[0], counts[1], want, counts[2]);
        return 0;
    }
    if (counts[1] != want) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "only %u of %u cases completed a capture, so the rest were "
                     "never measured (GL errors %u, primitives %u)",
                     counts[1], want, counts[0], counts[2]);
        return 0;
    }
    if (counts[2] != want) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "the primitives-written total is %u, not the one per case "
                     "that %u cases require (GL errors %u, captures %u)",
                     counts[2], want, counts[0], counts[1]);
        return 0;
    }
    return 1;
}

/* THE CALLER'S WHOLE DECISION, with no GL in it: the gate FIRST, and the
 * comparison only if the gate opens. Factored out so the controls below drive
 * the same function the driver path drives - the ordering is the property
 * under test, and a control that called the predicate directly would not show
 * that the comparison is actually skipped.
 *
 * `*compared` reports whether the comparison was reached at all, which is how
 * a control proves that an errored run compares NOTHING rather than comparing
 * and then mentioning the error. Returns 0 only when the gate opened and
 * nothing differed. */
static int judge(const float *records, const unsigned *counts, int n, int report,
                 int *compared, struct sl_bgop_tally *t, char *why, int whycap)
{
    memset(t, 0, sizeof *t);
    if (compared != NULL) *compared = 0;
    if (!sl_bgop_counts_ready(counts, n, why, whycap))
        return 1;
    if (compared != NULL) *compared = 1;
    compare_all(records, report, t);
    return t->differing != 0;
}

/* ---- the fabricated record set ----------------------------------------- */

static float g_records[SL_BGOP_MAX_CASES * SL_BGOP_RECORD_FLOATS];

static void fill_with_reference(void)
{
    int i;
    for (i = 0; i < g_n; i++) {
        struct sl_bgop_expect e;
        sl_bgop_reference(i, &e);
        memcpy(g_records + (size_t) i * SL_BGOP_RECORD_FLOATS, e.v, sizeof e.v);
    }
}

static float *rec_of(int i) { return g_records + (size_t) i * SL_BGOP_RECORD_FLOATS; }

/* ---- 1. the record layout is self-consistent --------------------------- */

static void st_layout(void)
{
    int f, want = 0, ok = 1;
    printf("  the capture record: one layout, shared by the capture and the "
           "comparison\n");
    for (f = 0; f < SL_BGOP_NFIELDS; f++) {
        if (g_fields[f].offset != want) ok = 0;
        want += g_fields[f].comps;
    }
    CHECK(ok, "every field's offset is the sum of the component counts before it");
    CHECK(want == SL_BGOP_RECORD_FLOATS,
          "the six fields occupy exactly %d floats (%d), which is 4+4+4+1+4+4 and "
          "NOT six times four - gl_FogFragCoord is a float",
          SL_BGOP_RECORD_FLOATS, want);
    CHECK(SL_BGOP_BUFFER_FLOATS > SL_BGOP_RECORD_FLOATS,
          "the buffer is larger than the record (%d > %d), so the driver run can "
          "prove the layout with a sentinel tail instead of trusting it",
          SL_BGOP_BUFFER_FLOATS, SL_BGOP_RECORD_FLOATS);
    CHECK(g_fields[SL_BGOP_FOG].comps == 1,
          "gl_FogFragCoord is declared as one component");
}

/* ---- 1b. the shared case table was built cleanly ----------------------- */

/* The depth probe's table builder is reused here, and it reports its own
 * failures - an overflowed table among them. A short table read as a complete
 * one would quietly narrow the coverage this probe claims, so its counter is
 * read rather than assumed to be zero. */
static void st_builder(void)
{
    int bchecks = -1, bfails = -1;
    printf("\n  the shared case table: built by the depth probe's own builder\n");
    sl_bgdp_builder_ledger(&bchecks, &bfails);
    CHECK(bfails == 0,
          "the builder reported no failures of its own (%d), so the table is "
          "not a truncated one read as complete", bfails);
    CHECK(g_n > 0, "the table holds %d cases", g_n);
    {
        int i, s = 0, f0 = 0, f1 = 0;
        for (i = 0; i < g_n; i++) {
            int d = sl_bgdp_cases()[i].dmode;
            if (d == SL_BGGPU_DEPTH_FAR0) f0++;
            else if (d == SL_BGGPU_DEPTH_FAR1) f1++;
            else s++;
        }
        CHECK(s > 0 && f0 > 0 && f1 > 0,
              "and it spans all three depth cases - SINGLE %d, FAR0 %d, FAR1 %d "
              "- so the output comparison covers each of them", s, f0, f1);
    }
    (void) bchecks;
}

/* ---- 2. the inputs are not vacuous ------------------------------------- */

static void st_inputs_nontrivial(void)
{
    int i, white = 0, zerofog = 0, defcoord = 0, distinct = 0, adv = 0;
    printf("\n  the attribute inputs: non-trivial, or these checks mean "
           "nothing\n");
    for (i = 0; i < g_n; i++) {
        const struct sl_bgop_attr *a = &g_attrs[i];
        if (a->col[0] == 255 && a->col[1] == 255 && a->col[2] == 255 && a->col[3] == 255)
            white++;
        if (a->col[0] != a->col[1] && a->col[1] != a->col[2] && a->col[0] != a->col[2])
            distinct++;
        if (a->fog == 0.0f) zerofog++;
        if (a->uv[0][0] == 0.0f && a->uv[0][1] == 0.0f &&
            a->uv[1][0] == 0.0f && a->uv[1][1] == 0.0f) defcoord++;
        if (a->tex_class == SL_BGOP_ADVISORY) adv++;
    }
    CHECK(white == 0, "no case is measured at a white current colour (%d)", white);
    CHECK(distinct == g_n,
          "every case's R, G and B differ from each other (%d of %d)", distinct, g_n);
    CHECK(defcoord == 0,
          "no case is measured at default texture coordinates (%d)", defcoord);
    CHECK(zerofog < g_n,
          "the fog values are not all zero (%d of %d are)", zerofog, g_n);
    CHECK(adv > 0 && adv < g_n,
          "the rounding texcoord case is present AND is not the whole table "
          "(%d advisory of %d), so the exact cases carry the verdict", adv, g_n);
    CHECK(memcmp(M0_EXACT, M1_EXACT, sizeof M0_EXACT) != 0,
          "the two texture matrices differ from each other");
    /* THE EXISTING DENORMAL ADVISORY, STILL SEPARATE AND STILL PER FIELD. */
    {
        int den = 0, den_pos_adv = 0, den_other_exact = 0;
        for (i = 0; i < g_n; i++) {
            if (!sl_bgdp_cases()[i].advisory) continue;
            den++;
            if (sl_bgop_class(i, SL_BGOP_POS) == SL_BGOP_ADVISORY) den_pos_adv++;
            if (sl_bgop_class(i, SL_BGOP_FRONT) == SL_BGOP_EXACT &&
                sl_bgop_class(i, SL_BGOP_FOG) == SL_BGOP_EXACT) den_other_exact++;
        }
        CHECK(den > 0, "the depth probe's denormal advisory case is in the table (%d)", den);
        CHECK(den_pos_adv == den,
              "its POSITION is advisory, all %d of them (%d), because a driver that "
              "flushes a denormal w to zero takes the other side of `c.w > 0.0`",
              den, den_pos_adv);
        CHECK(den_other_exact == den,
              "and its colour and fog stay EXACT (%d of %d) - the advisory is "
              "identified field by field, not applied to the whole case",
              den_other_exact, den);
    }
    {
        static const float I[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        CHECK(memcmp(M0_EXACT, I, sizeof I) != 0 && memcmp(M1_EXACT, I, sizeof I) != 0,
              "neither texture matrix is the identity");
    }
}

/* ---- 3. the exact texcoord cases really are exact ---------------------- */

/* THE CLAIM THAT CARRIES THE TEXCOORD VERDICT, checked rather than asserted:
 * for the EXACT cases every product and every partial sum is representable, so
 * ANY association gives the identical result. Four different associations are
 * computed here and required to agree bit for bit. If a future edit to a matrix
 * or a coordinate broke representability, this is what would catch it - and the
 * case would have to become advisory rather than the comparison loosened. */
static void st_texcoord_association(void)
{
    int i, f, bad = 0, checked = 0;
    printf("\n  the exact texcoord cases: four associations, one answer\n");
    for (i = 0; i < g_n; i++) {
        const struct sl_bgop_attr *a = &g_attrs[i];
        if (a->tex_class != SL_BGOP_EXACT) continue;
        for (f = 0; f < 2; f++) {
            const float *M = a->M[f];
            float v[4];
            int row;
            v[0] = a->uv[f][0]; v[1] = a->uv[f][1]; v[2] = 0.0f; v[3] = 1.0f;
            for (row = 0; row < 4; row++) {
                float p0 = M[0 * 4 + row] * v[0], p1 = M[1 * 4 + row] * v[1];
                float p2 = M[2 * 4 + row] * v[2], p3 = M[3 * 4 + row] * v[3];
                float left  = ((p0 + p1) + p2) + p3;     /* the reference's order */
                float right = p0 + (p1 + (p2 + p3));
                float pair  = (p0 + p1) + (p2 + p3);
                float rev   = ((p3 + p2) + p1) + p0;
                checked++;
                if (memcmp(&left, &right, sizeof left) != 0 ||
                    memcmp(&left, &pair, sizeof left) != 0 ||
                    memcmp(&left, &rev, sizeof left) != 0)
                    bad++;
            }
        }
    }
    CHECK(checked > 0, "there are exact texcoord components to check (%d)", checked);
    CHECK(bad == 0,
          "all %d of them give the same bits under four different associations "
          "(%d disagreed), so the exact comparison rests on the arithmetic being "
          "exact and not on a driver associating the reference's way",
          checked, bad);
}

/* ---- 4. the comparison bites ------------------------------------------- */

static void st_bites(void)
{
    struct sl_bgop_tally t;
    int i, f;

    printf("\n  the comparison loop: it accepts the reference and rejects "
           "every alteration of it\n");

    fill_with_reference();
    compare_all(g_records, 0, &t);
    CHECK(t.differing == 0 && t.advisory_differing == 0,
          "POSITIVE CONTROL: the reference's own records produce no differences "
          "(%d of %d compared, %d advisory)",
          t.differing, t.compared, t.advisory);
    CHECK(t.compared > 0, "and it compared something (%d components)", t.compared);

    /* A COMPONENT SWAP INSIDE gl_Position. This is the gap item 1 closes: the
     * depth probe read component 2 alone, so exchanging x and y was invisible
     * to it. */
    fill_with_reference();
    for (i = 0; i < g_n; i++) {
        float *r = rec_of(i) + g_fields[SL_BGOP_POS].offset;
        float tmp = r[0]; r[0] = r[1]; r[1] = tmp;
    }
    compare_all(g_records, 0, &t);
    CHECK(t.differing > 0,
          "a swap of gl_Position.x and .y is caught (%d differing components) - "
          "the depth-only probe could not see it", t.differing);

    /* A CHANNEL SWAP INSIDE THE COLOUR. */
    fill_with_reference();
    for (i = 0; i < g_n; i++) {
        float *r = rec_of(i) + g_fields[SL_BGOP_FRONT].offset;
        float tmp = r[0]; r[0] = r[2]; r[2] = tmp;
    }
    compare_all(g_records, 0, &t);
    CHECK(t.differing > 0,
          "a swap of the colour's R and B channels is caught (%d)", t.differing);

    /* A FIELD SWAP between the two texture coordinate outputs. */
    fill_with_reference();
    for (i = 0; i < g_n; i++) {
        float *r = rec_of(i);
        float tmp[4];
        memcpy(tmp, r + g_fields[SL_BGOP_TEX0].offset, sizeof tmp);
        memcpy(r + g_fields[SL_BGOP_TEX0].offset,
               r + g_fields[SL_BGOP_TEX1].offset, sizeof tmp);
        memcpy(r + g_fields[SL_BGOP_TEX1].offset, tmp, sizeof tmp);
    }
    compare_all(g_records, 0, &t);
    CHECK(t.differing > 0,
          "an exchange of gl_TexCoord[0] and gl_TexCoord[1] is caught (%d)",
          t.differing);

    /* A CASE SWAP: each case given the next case's whole record. */
    if (g_n >= 2) {
        static float keep[SL_BGOP_RECORD_FLOATS];
        fill_with_reference();
        memcpy(keep, rec_of(0), sizeof keep);
        for (i = 0; i < g_n - 1; i++)
            memcpy(rec_of(i), rec_of(i + 1), sizeof keep);
        memcpy(rec_of(g_n - 1), keep, sizeof keep);
        compare_all(g_records, 0, &t);
        CHECK(t.differing > 0,
              "a rotation of the whole table by one case is caught (%d), so the "
              "loop compares each case against its own reference", t.differing);
    }

    /* ONE ULP, PER FIELD. Every field that carries the verdict must reject a
     * single-bit change; the advisory field must report one and not count it. */
    for (f = 0; f < SL_BGOP_NFIELDS; f++) {
        int moved = 0;
        fill_with_reference();
        for (i = 0; i < g_n; i++) {
            float *r = rec_of(i) + g_fields[f].offset;
            if (sl_bgop_class(i, f) != SL_BGOP_EXACT) continue;
            if (r[0] == 0.0f) continue;        /* a zero's neighbour is denormal */
            r[0] = ulp_up(r[0]);
            moved++;
        }
        if (moved == 0) {
            CHECK(1, "%s: no exact non-zero component to move one ULP, skipped",
                  g_fields[f].label);
            continue;
        }
        compare_all(g_records, 0, &t);
        CHECK(t.differing == moved,
              "%s: one ULP on %d component(s) is caught, and exactly those "
              "(%d) - no tolerance anywhere",
              g_fields[f].label, moved, t.differing);
    }

    /* THE ADVISORY FIELD IS SEPARATE, NOT TOLERATED. A one-ULP change in an
     * advisory texcoord must be REPORTED and must not fail the run, which is
     * the difference between an identified unspecified product and a global
     * slack. */
    {
        int moved = 0;
        fill_with_reference();
        for (i = 0; i < g_n; i++) {
            float *r = rec_of(i) + g_fields[SL_BGOP_TEX0].offset;
            if (sl_bgop_class(i, SL_BGOP_TEX0) != SL_BGOP_ADVISORY) continue;
            if (r[0] == 0.0f) continue;
            r[0] = ulp_up(r[0]);
            moved++;
        }
        compare_all(g_records, 0, &t);
        CHECK(moved > 0, "the advisory texcoord has a component to move (%d)", moved);
        CHECK(t.differing == 0,
              "a one-ULP change in an ADVISORY texcoord fails nothing (%d)",
              t.differing);
        CHECK(t.advisory_differing == moved,
              "and is reported as an advisory difference, all %d of them (%d)",
              moved, t.advisory_differing);
    }
}

/* ---- 4b. THE READINESS GATE BITES, with no driver and no GL ----------- */

/* The defect this closes: the runner accumulated GL errors from setup, the
 * cases and cleanup but returned 0 when the captures matched, and the caller
 * printed the error count AFTER comparing. A driver run with GL errors could
 * therefore PASS.
 *
 * These controls inject counts into the SAME function the driver path calls -
 * `judge`, which gates and only then compares - and require that an errored or
 * incomplete run compares NOTHING. The records handed in are the reference's
 * own, so they match exactly: that is the point. Exact-looking data must not
 * rescue a run the driver mishandled. No GL context and no second mock. */
static void st_readiness_gate(void)
{
    struct sl_bgop_tally t;
    char why[512];
    unsigned counts[3];
    int compared, bad;
    const unsigned n = (unsigned) g_n;

    printf("\n  the readiness gate: a GL error or an incomplete run refuses "
           "BEFORE anything is compared\n");

    /* THE TRUE COMPLETE CONTROL. */
    fill_with_reference();
    counts[0] = 0u; counts[1] = n; counts[2] = n;
    bad = judge(g_records, counts, g_n, 0, &compared, &t, why, (int) sizeof why);
    CHECK(bad == 0 && compared == 1 && t.differing == 0 && t.compared > 0,
          "POSITIVE CONTROL: a clean complete run (0 errors, %u/%u captures, "
          "%u primitives) opens the gate and compares %d components with %d "
          "differing", n, n, n, t.compared, t.differing);

    /* ONE GL ERROR, EVERYTHING ELSE VALID, DATA EXACTLY MATCHING. */
    fill_with_reference();
    counts[0] = 1u; counts[1] = n; counts[2] = n;
    bad = judge(g_records, counts, g_n, 0, &compared, &t, why, (int) sizeof why);
    CHECK(bad != 0 && compared == 0 && t.compared == 0,
          "one collected GL error refuses the run even though every record "
          "matches exactly, and compares NOTHING (%d components compared)",
          t.compared);
    CHECK(strstr(why, "GL error") != NULL,
          "and the reason names the GL error rather than a data difference");

    /* AN INCOMPLETE SET OF CAPTURES. */
    if (g_n >= 2) {
        fill_with_reference();
        counts[0] = 0u; counts[1] = n - 1u; counts[2] = n;
        bad = judge(g_records, counts, g_n, 0, &compared, &t, why, (int) sizeof why);
        CHECK(bad != 0 && compared == 0,
              "a run that completed %u of %u captures refuses and compares "
              "nothing", n - 1u, n);

        /* TOO FEW PRIMITIVES WRITTEN. */
        fill_with_reference();
        counts[0] = 0u; counts[1] = n; counts[2] = n - 1u;
        bad = judge(g_records, counts, g_n, 0, &compared, &t, why, (int) sizeof why);
        CHECK(bad != 0 && compared == 0,
              "a primitives-written total of %u for %u cases refuses and "
              "compares nothing", n - 1u, n);
    }

    /* TOO MANY PRIMITIVES WRITTEN - one per case, not at least one. */
    fill_with_reference();
    counts[0] = 0u; counts[1] = n; counts[2] = n + 1u;
    bad = judge(g_records, counts, g_n, 0, &compared, &t, why, (int) sizeof why);
    CHECK(bad != 0 && compared == 0,
          "a primitives-written total of %u for %u cases refuses too - the rule "
          "is one per case, not at least one", n + 1u, n);

    /* NO COUNTS AT ALL. */
    fill_with_reference();
    bad = judge(g_records, NULL, g_n, 0, &compared, &t, why, (int) sizeof why);
    CHECK(bad != 0 && compared == 0,
          "a run that reported no counts refuses and compares nothing");

    /* AND THE GATE DOES NOT MASK A REAL DIFFERENCE. A clean complete run whose
     * data differs must still be compared and must still fail, or the gate
     * would have replaced the comparison instead of preceding it. */
    fill_with_reference();
    {
        int i, moved = 0;
        for (i = 0; i < g_n; i++) {
            float *r = rec_of(i) + g_fields[SL_BGOP_FOG].offset;
            if (r[0] == 0.0f) continue;
            r[0] = ulp_up(r[0]);
            moved++;
        }
        counts[0] = 0u; counts[1] = n; counts[2] = n;
        bad = judge(g_records, counts, g_n, 0, &compared, &t, why, (int) sizeof why);
        CHECK(bad != 0 && compared == 1 && t.differing == moved,
              "a clean complete run with %d altered component(s) is still "
              "compared and still fails (%d differing), so the gate precedes "
              "the comparison rather than replacing it", moved, t.differing);
    }
}

/* ---- 5. what this probe CANNOT distinguish, stated -------------------- */

/* A limit found by building the thing: the production text assigns BOTH
 * gl_FrontColor and gl_BackColor from gl_Color, so the two outputs are equal by
 * construction and exchanging them is undetectable. That is a property of the
 * shader, not a defect in the probe, and it is recorded here rather than left
 * for a reader to assume the swap was covered. */
static void st_indistinguishable(void)
{
    struct sl_bgop_tally t;
    int i;
    printf("\n  what this probe cannot distinguish, measured rather than "
           "assumed\n");
    fill_with_reference();
    for (i = 0; i < g_n; i++) {
        float *r = rec_of(i);
        float tmp[4];
        memcpy(tmp, r + g_fields[SL_BGOP_FRONT].offset, sizeof tmp);
        memcpy(r + g_fields[SL_BGOP_FRONT].offset,
               r + g_fields[SL_BGOP_BACK].offset, sizeof tmp);
        memcpy(r + g_fields[SL_BGOP_BACK].offset, tmp, sizeof tmp);
    }
    compare_all(g_records, 0, &t);
    CHECK(t.differing == 0,
          "exchanging gl_FrontColor and gl_BackColor changes nothing (%d), "
          "because BGG_VS assigns both from gl_Color - so this probe cannot "
          "tell the two apart, and does not claim to", t.differing);
}

/* ======================================================================== *
 * THE SNAPSHOT PATH: actual frame inputs instead of constructed ones.
 *
 * Read the long note in sl_bggpu_out_probe.h for the per-field oracle and for
 * what this path does NOT establish. Everything below is CPU-only; the driver
 * side lives in the output-guarded section of sl_bggpu_depth_probe_gl.c and is
 * reached only through sl_bgop_gl_run_snapshot.
 * ======================================================================== */

/* ---- the classification, measured per component ------------------------ */

/* The four associations of `M[row] . v`, required to agree bit for bit before a
 * texcoord component is called EXACT. This is the same test st_texcoord_
 * association applies to the synthetic matrices, applied here to whatever the
 * snapshot recorded: the product carries no `precise`, so a component is exact
 * only where the arithmetic itself cannot round, and that is a property of the
 * numbers rather than something a driver can be assumed into. */
static int assoc_invariant(const float *M, const float *v, int row)
{
    float p0 = M[0 * 4 + row] * v[0], p1 = M[1 * 4 + row] * v[1];
    float p2 = M[2 * 4 + row] * v[2], p3 = M[3 * 4 + row] * v[3];
    float left  = ((p0 + p1) + p2) + p3;
    float right = p0 + (p1 + (p2 + p3));
    float pair  = (p0 + p1) + (p2 + p3);
    float rev   = ((p3 + p2) + p1) + p0;
    return memcmp(&left, &right, sizeof left) == 0 &&
           memcmp(&left, &pair,  sizeof left) == 0 &&
           memcmp(&left, &rev,   sizeof left) == 0;
}

static void snap_texinput(const struct sl_bgsnap_draw *d,
                          const struct sl_bgsnap_vertex *vx, int unit,
                          float *M, float *v)
{
    int n;
    for (n = 0; n < 16; n++) M[n] = sl_bgsnap_f32(d->texmat_bits[unit][n]);
    /* A two-component texcoord array leaves z = 0 and w = 1, the specified
     * defaults for the components an array does not supply. */
    v[0] = sl_bgsnap_f32(vx->uv_bits[unit][0]);
    v[1] = sl_bgsnap_f32(vx->uv_bits[unit][1]);
    v[2] = 0.0f;
    v[3] = 1.0f;
}

/* THE EXPECTED gl_Position.z FOR ONE VERTEX OF ONE DRAW, derived from the
 * draw's own recorded depth mode and coefficient pair exactly as BGG_VS
 * computes it (src/gfx/sl_bgsnap.h states the pair of recorded depths and the
 * rule; the mode values are src/gfx/sl_gfx_bggpu.h's own, checked against it at
 * compile time by the depth probe).
 *
 *   SINGLE  the recorded clip z, which already carries the far clamp
 *   FAR0    fk1 * w - fk2, in that association; the GLSL marks z `precise`, so
 *           the driver may neither reassociate nor fuse it
 *   FAR1    the recorded UNCLAMPED z
 *
 * Returns 0 for a mode that is none of the three. An unknown mode is not
 * guessed at and not defaulted to SINGLE: it has no oracle, because nothing
 * recorded says what the shader would do with it. */
static int snap_depth_expect(const struct sl_bgsnap_draw *d,
                             const struct sl_bgsnap_vertex *vx, float *out)
{
    *out = 0.0f;
    switch (d->dmode) {
    case SL_BGGPU_DEPTH_SINGLE:
        *out = sl_bgsnap_f32(vx->clip_bits[2]);
        return 1;
    case SL_BGGPU_DEPTH_FAR0:
        *out = sl_bgsnap_f32(d->fk1_bits) * sl_bgsnap_f32(vx->clip_bits[3])
             - sl_bgsnap_f32(d->fk2_bits);
        return 1;
    case SL_BGGPU_DEPTH_FAR1:
        *out = sl_bgsnap_f32(vx->zraw_bits);
        return 1;
    default:
        return 0;
    }
}

/* Whether a bound-state word is usable evidence AND says the thing it gates is
 * enabled. Three outcomes collapse to two here, and the comment is the
 * difference: an unrecorded word is absent evidence, and a recorded zero is
 * evidence that the array was OFF - in which case gl_Color came from the
 * current value rather than this vertex's record, so the vertex's bytes are not
 * its oracle either. Both leave the component without an oracle, for different
 * reasons, and only the mask can tell them apart.
 *
 * THIS IS FOR THE COLOUR ARRAY, WHICH IS A BOOLEAN. The fog array is not: it
 * carries three states and a recorded zero there is a REAL ORACLE rather than
 * an absence. Using this predicate for fog discarded that oracle for state 0
 * and, worse, returned "enabled" for state 2 - whose value is 2 and therefore
 * non-zero - which produced a confident comparison of unrecorded data against
 * the pool. The fog decision is sl_bgop_fog_plan and nothing else. */
static int snap_array_enabled(const struct sl_bgsnap_draw *d, unsigned word)
{
    return sl_bgsnap_state_recorded(d, word) && d->state[word] != 0u;
}

void sl_bgop_fog_plan(const struct sl_bgsnap_draw *d,
                      const struct sl_bgsnap_vertex *vx,
                      struct sl_bgop_fog_plan *p)
{
    static const float ZERO = 0.0f;

    if (p == NULL) return;
    memset(p, 0, sizeof *p);
    p->kind = SL_BGOP_FOG_NONE;
    p->why  = "no draw was supplied";
    if (d == NULL) return;

    /* The mask first: an unrecorded word says nothing, and its stored value -
     * whatever it is - must not be read as a state. */
    if (!sl_bgsnap_state_recorded(d, SL_BGSNAP_S_FOG_ARRAY)) {
        p->why = "the fog-array word was not recorded, so nothing is known "
                 "about where this draw's fog coordinate came from";
        return;
    }

    switch (d->state[SL_BGSNAP_S_FOG_ARRAY]) {
    case SL_BGSNAP_FOG_ARRAY_ON:
        p->kind       = SL_BGOP_FOG_POOL;
        p->bind_array = 1;
        p->why        = "the fog-coordinate array is enabled, so this draw "
                        "reads the per-vertex pool and the pool is the oracle";
        if (vx != NULL) {
            p->has_oracle  = 1;
            p->expect_bits = vx->fog_bits;
        }
        return;

    case SL_BGSNAP_FOG_OFF_ZERO:
        /* A REAL ORACLE, and not the pool. The pass disables the array and
         * sets the current coordinate to zero in the same branch, so every
         * vertex of this draw forwards exactly 0.0f - which is a different
         * number from the pool value the draw does not read. */
        p->kind        = SL_BGOP_FOG_ZERO;
        p->bind_array  = 0;
        p->set_current = 1;
        p->current     = ZERO;
        p->has_oracle  = 1;
        memcpy(&p->expect_bits, &ZERO, sizeof p->expect_bits);
        p->why         = "the array is disabled with the current coordinate set "
                         "to a known 0.0f, so the oracle is exactly 0.0f and "
                         "NOT the pool value this draw never reads";
        return;

    case SL_BGSNAP_FOG_OFF_UNKNOWN:
        /* NOTHING IS BOUND AND NOTHING IS SET. The effective coordinate was
         * never established, so there is no input to reproduce; binding the
         * pool would replay a value this draw did not use and then compare
         * against it, which is a fabricated measurement rather than a weak one. */
        p->why = "the array is disabled and the current coordinate was never "
                 "established, so the effective value is unknown: nothing is "
                 "bound, nothing is set, and the result is recorded as "
                 "unmeasured rather than compared";
        return;

    default:
        p->why = "the fog-array word holds an unrecognised value, which is "
                 "never read as array-on and is not guessed at";
        return;
    }
}

int sl_bgop_snap_class(const struct sl_bgsnap *s, unsigned draw, unsigned vert,
                       int field, int comp)
{
    const struct sl_bgsnap_draw *d;
    const struct sl_bgsnap_vertex *vx;

    if (s == NULL || s->draw == NULL || s->vertex == NULL) return SL_BGOP_NO_ORACLE;
    if (draw >= s->draws || vert >= s->vertices) return SL_BGOP_NO_ORACLE;
    if (field < 0 || field >= SL_BGOP_NFIELDS) return SL_BGOP_NO_ORACLE;
    if (comp < 0 || comp >= g_fields[field].comps) return SL_BGOP_NO_ORACLE;
    d  = &s->draw[draw];
    vx = &s->vertex[vert];

    switch (field) {
    case SL_BGOP_POS:
        /* z is derived from the draw's own depth mode; x, y and w are the clip
         * product, which no mode rewrites. */
        if (comp == 2) {
            float z;
            unsigned zb;
            if (!snap_depth_expect(d, vx, &z)) return SL_BGOP_NO_ORACLE;
            memcpy(&zb, &z, sizeof zb);
            if (sl_bgsnap_is_denormal(zb)) return SL_BGOP_ADVISORY;
            /* SINGLE's clamp is taken inside the shader on `c.w > 0.0` and
             * FAR0's product reads w directly, so for both a denormal w can
             * change the answer on a driver that flushes denormals - which
             * GLSL's default precision permits. FAR1 reads zraw alone. */
            if (d->dmode != SL_BGGPU_DEPTH_FAR1 &&
                sl_bgsnap_is_denormal(vx->clip_bits[3]))
                return SL_BGOP_ADVISORY;
            return SL_BGOP_EXACT;
        }
        return sl_bgsnap_is_denormal(vx->clip_bits[comp])
               ? SL_BGOP_ADVISORY : SL_BGOP_EXACT;

    case SL_BGOP_FRONT:
    case SL_BGOP_BACK:
        /* The MASK decides, not the value. A word whose bit is clear is absent
         * evidence; a word recorded as zero says the colour array was off, so
         * gl_Color was the current colour and not this vertex's bytes. */
        if (!snap_array_enabled(d, SL_BGSNAP_S_COLOR_ARRAY))
            return SL_BGOP_NO_ORACLE;
        return SL_BGOP_EXACT;

    case SL_BGOP_FOG: {
        /* THE THREE-STATE CONTRACT, through the one shared interpretation. The
         * earlier `recorded && value != 0` test was wrong twice over: state 0
         * is a real 0.0f oracle and was discarded, and state 2 has the value 2
         * - non-zero - so it was read as array-on and compared against the pool
         * on data whose effective value is unrecorded. */
        struct sl_bgop_fog_plan p;
        sl_bgop_fog_plan(d, vx, &p);
        if (!p.has_oracle) return SL_BGOP_NO_ORACLE;
        return sl_bgsnap_is_denormal(p.expect_bits)
               ? SL_BGOP_ADVISORY : SL_BGOP_EXACT;
    }

    case SL_BGOP_TEX0:
    case SL_BGOP_TEX1: {
        int unit = (field == SL_BGOP_TEX1) ? 1 : 0;
        unsigned need = unit ? (SL_BGSNAP_DF_TEXMAT1 | SL_BGSNAP_DF_UV1)
                             : SL_BGSNAP_DF_TEXMAT0;
        float M[16], v[4], r[4];
        unsigned bits;
        /* Without the matrix recorded there is nothing to multiply by, and for
         * unit 1 without its coordinate set bound there is nothing to multiply.
         * Either way the component has no reference at all. Unit 0's coordinate
         * set has no flag of its own in the format, so only its matrix is
         * required here. */
        if ((d->flags & need) != need) return SL_BGOP_NO_ORACLE;
        snap_texinput(d, vx, unit, M, v);
        if (!assoc_invariant(M, v, comp)) return SL_BGOP_ADVISORY;
        mat4_vec4(M, v, r);
        memcpy(&bits, &r[comp], sizeof bits);
        return sl_bgsnap_is_denormal(bits) ? SL_BGOP_ADVISORY : SL_BGOP_EXACT;
    }

    default:
        break;
    }
    return SL_BGOP_NO_ORACLE;
}

void sl_bgop_snap_expect(const struct sl_bgsnap *s, unsigned draw, unsigned vert,
                         float *v)
{
    const struct sl_bgsnap_draw *d;
    const struct sl_bgsnap_vertex *vx;
    int j, unit;

    if (v == NULL) return;
    memset(v, 0, sizeof(float) * SL_BGOP_RECORD_FLOATS);
    if (s == NULL || s->draw == NULL || s->vertex == NULL) return;
    if (draw >= s->draws || vert >= s->vertices) return;
    d  = &s->draw[draw];
    vx = &s->vertex[vert];

    /* x, y and w from the recorded clip position; z derived from the draw's own
     * depth mode. A mode that is none of the three leaves the z slot zero, and
     * the classification gives it no oracle, so the zero is never compared -
     * putting a guess there would make a report look like a comparison. */
    v[g_fields[SL_BGOP_POS].offset + 0] = sl_bgsnap_f32(vx->clip_bits[0]);
    v[g_fields[SL_BGOP_POS].offset + 1] = sl_bgsnap_f32(vx->clip_bits[1]);
    v[g_fields[SL_BGOP_POS].offset + 3] = sl_bgsnap_f32(vx->clip_bits[3]);
    (void) snap_depth_expect(d, vx, &v[g_fields[SL_BGOP_POS].offset + 2]);

    for (j = 0; j < 4; j++) {
        float u = unorm8(vx->col[j]);
        v[g_fields[SL_BGOP_FRONT].offset + j] = u;
        v[g_fields[SL_BGOP_BACK].offset + j] = u;
    }

    /* THE FOG SLOT, FROM THE SAME PLAN THE CLASSIFIER USES. Unconditionally
     * writing the pool value here filled the slot for state 0 with a number the
     * draw never reads and for state 2 with one whose effective value is
     * unrecorded. A state with no oracle leaves the slot at zero, and its class
     * keeps it out of every comparison, so the zero is never read as an
     * expectation. */
    {
        struct sl_bgop_fog_plan fp;
        sl_bgop_fog_plan(d, vx, &fp);
        if (fp.has_oracle)
            v[g_fields[SL_BGOP_FOG].offset] = sl_bgsnap_f32(fp.expect_bits);
    }

    for (unit = 0; unit < 2; unit++) {
        float M[16], c[4], r[4];
        int base = g_fields[unit ? SL_BGOP_TEX1 : SL_BGOP_TEX0].offset;
        snap_texinput(d, vx, unit, M, c);
        mat4_vec4(M, c, r);
        for (j = 0; j < 4; j++) v[base + j] = r[j];
    }
}

void sl_bgop_snap_compare_draw(const struct sl_bgsnap *s, unsigned draw,
                               const float *records, unsigned nverts,
                               int report, struct sl_bgop_snap_tally *t,
                               struct sl_bgor *sink)
{
    const struct sl_bgsnap_draw *d;
    unsigned k;

    if (s == NULL || t == NULL || records == NULL) return;
    if (s->draw == NULL || s->index == NULL || draw >= s->draws) return;
    d = &s->draw[draw];
    /* The caller must hand over the whole draw. A short record set is a
     * partial capture, and a partial capture of a draw is not a measurement of
     * that draw - so nothing is compared rather than part of it. */
    if (nverts != d->index_count) return;

    for (k = 0u; k < nverts; k++) {
        unsigned vi = s->index[d->first_index + k];
        const float *rec = records + (size_t) k * SL_BGOP_RECORD_FLOATS;
        float e[SL_BGOP_RECORD_FLOATS];
        unsigned row_measured[SL_BGOP_RECORD_FLOATS];
        unsigned row_reference[SL_BGOP_RECORD_FLOATS];
        unsigned char row_class[SL_BGOP_RECORD_FLOATS];
        int f, j;

        sl_bgop_snap_expect(s, draw, vi, e);
        for (f = 0; f < SL_BGOP_NFIELDS; f++) {
            for (j = 0; j < g_fields[f].comps; j++) {
                int o = g_fields[f].offset + j;
                int cls = sl_bgop_snap_class(s, draw, vi, f, j);
                int same;
                /* RETAINED FIRST, CLASSIFIED SECOND. Every component's measured
                 * bits, reference bits and class are captured here, before any
                 * branch can decide to stop caring about it. The NO_ORACLE
                 * components used to be counted and dropped at exactly this
                 * point, which threw away the measurements least recoverable by
                 * any other means. */
                row_measured[o]  = bits_of(rec[o]);
                row_reference[o] = bits_of(e[o]);
                row_class[o]     = (unsigned char) cls;
                if (cls == SL_BGOP_NO_ORACLE) {
                    t->no_oracle++;
                    continue;
                }
                same = memcmp(&rec[o], &e[o], sizeof rec[o]) == 0;
                if (cls == SL_BGOP_ADVISORY) {
                    t->advisory++;
                    if (!same) t->advisory_differing++;
                    if (!same && report && t->reported < SL_BGOP_SNAP_REPORT_CAP) {
                        printf("    ADVISORY draw %u vertex %u %s[%d]: measured "
                               "0x%08x, reference 0x%08x\n", draw, vi,
                               g_fields[f].label, j, bits_of(rec[o]),
                               bits_of(e[o]));
                        t->reported++;
                    }
                    continue;
                }
                t->compared++;
                if (same) continue;
                t->differing++;
                if (report && t->reported < SL_BGOP_SNAP_REPORT_CAP) {
                    printf("    DIFFERS draw %u vertex %u %s[%d]: measured "
                           "%.9g (0x%08x), reference %.9g (0x%08x)\n", draw, vi,
                           g_fields[f].label, j, (double) rec[o], bits_of(rec[o]),
                           (double) e[o], bits_of(e[o]));
                    t->reported++;
                }
            }
        }
        t->vertices++;
        if (sink != NULL) {
            char swhy[256];
            int written = 0;
            swhy[0] = '\0';
            if (sl_bgor_row(sink, draw, vi, row_measured, row_reference,
                            row_class, &written, swhy, (int) sizeof swhy) != 0) {
                /* A sink failure is recorded and does not touch the tally's
                 * comparison counts. The artifact is where the measurement is
                 * kept, not what decides it, and a verdict that moved when the
                 * disk filled would be a different measurement. */
                t->sink_failed++;
            } else if (written) {
                t->retained++;
            } else {
                /* The cap dropped it. Counted separately from a retained row,
                 * because the artifact is bounded and must not be reported as
                 * holding what it refused. */
                t->sink_dropped++;
            }
        }
    }
}

/* See the long note in sl_bggpu_out_probe.h. One rule, applied by the driver
 * runner and driven by st_retention_gate with injected tallies, so the decision
 * is regression-tested without a driver and without a full disk. */
int sl_bgop_retention_ready(const struct sl_bgop_snap_tally *t,
                            unsigned expect_vertices, int closed_ok,
                            char *why, int whycap)
{
    if (why != NULL && whycap > 0) why[0] = '\0';
    if (t == NULL) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap, "no retention tally was reported");
        return 0;
    }
    if (t->sink_failed != 0u) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "%u record write(s) failed, so the raw outputs are not on "
                     "disk; a run whose captures cannot be kept has measured "
                     "nothing that can be examined again, however exactly its "
                     "arithmetic matched (retained %u of %u vertices)",
                     t->sink_failed, t->retained, expect_vertices);
        return 0;
    }
    if (t->sink_dropped != 0u) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "the artifact's cap dropped %u record(s), so a suffix of "
                     "the capture is missing; retention is mandatory and a "
                     "prefix is not the measurement (retained %u of %u)",
                     t->sink_dropped, t->retained, expect_vertices);
        return 0;
    }
    if (t->retained != t->vertices) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "%u record(s) were retained for %u compared vertex/vertices, "
                     "so the artifact does not hold one row per comparison",
                     t->retained, t->vertices);
        return 0;
    }
    if (t->vertices != expect_vertices) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "%u vertex/vertices were compared where %u were expected, "
                     "so the run was short even though every row it produced "
                     "was retained", t->vertices, expect_vertices);
        return 0;
    }
    if (!closed_ok) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "the artifact's final header could not be written, so the "
                     "%u retained record(s) are not sealed and the file carries "
                     "no completion flag", t->retained);
        return 0;
    }
    return 1;
}

/* See the long note in sl_bggpu_out_probe.h. The decision comes from the run's
 * actual numbers, THEN the artifact is sealed, THEN the seal's own success
 * enters the status - in that order, because choosing the seal after judging
 * the status is what let a failed close qualify. */
int sl_bgop_finalize_records(struct sl_bgor *sink, int sink_open,
                             const struct sl_bgop_snap_tally *t,
                             unsigned expect_vertices, unsigned expect_draws,
                             int capture_ok, char *why, int whycap)
{
    char rwhy[384], swhy[256];
    int records_whole, closed_ok;

    if (why != NULL && whycap > 0) why[0] = '\0';
    if (t == NULL) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap, "no tally was reported, so nothing "
                     "about this run's retention is known");
        return 1;
    }
    if (!sink_open) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "no record artifact was open, so nothing was retained; "
                     "retention is mandatory and this run does not qualify");
        return 1;
    }

    /* ---- 1. THE ACTUAL NUMBERS, BEFORE ANYTHING IS WRITTEN ------------
     * `closed_ok` is passed as 1 deliberately: the seal has not happened yet,
     * and this call is asking only whether the ROWS are whole. The seal's own
     * outcome is folded in at step 3, where it is known. */
    rwhy[0] = '\0';
    records_whole = sl_bgop_retention_ready(t, expect_vertices, 1, rwhy,
                                            (int) sizeof rwhy);
    if (records_whole && t->draws_captured != expect_draws) {
        records_whole = 0;
        snprintf(rwhy, sizeof rwhy,
                 "%u draw(s) completed a capture where %u were expected, so the "
                 "artifact is missing a draw even though every row it does hold "
                 "was retained", t->draws_captured, expect_draws);
    }

    /* ---- 2. THE SEAL, CHOSEN FROM THOSE NUMBERS ----------------------
     * COMPLETE is requested only when every row is present. It says nothing
     * about whether the run qualified: a run whose arithmetic disagreed but
     * whose retention was whole is sealed COMPLETE and FAILED at once, because
     * those records are exactly what the disagreement has to be read from. */
    swhy[0] = '\0';
    /* THE FAILED MARK IS SET BEFORE THE SEAL, NOT AFTER IT. The seal writes the
     * status word and closes the file, so a mark applied afterwards would live
     * only in memory and never reach the artifact - leaving a COMPLETE
     * artifact on disk with no record that its run was refused. */
    if (!capture_ok) sl_bgor_failed(sink, "run refused");
    if (records_whole) {
        /* COMPLETE is requested because the rows are all there. FAILED may be
         * set alongside it: the two are independent, and those records are
         * exactly what a refused run has to be read from. */
        closed_ok = sl_bgor_close(sink, swhy, (int) sizeof swhy) == 0;
    } else {
        closed_ok = sl_bgor_seal_failed(sink, "records lost", swhy,
                                        (int) sizeof swhy) == 0;
    }

    /* ---- 3. AND ONLY NOW THE STATUS --------------------------------- */
    if (!records_whole) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "the records were not retained whole, so this run does NOT "
                     "qualify: %s. The artifact is sealed WITHOUT "
                     "SL_BGOR_F_COMPLETE and is kept. The comparison counts "
                     "stand as measured - %u compared, %u differing, %u "
                     "advisory, %u with no oracle - and none is altered by this "
                     "refusal.", rwhy, t->compared, t->differing, t->advisory,
                     t->no_oracle);
        return 1;
    }
    if (!closed_ok) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "all %u record(s) were written but the artifact could not "
                     "be sealed (%s), so SL_BGOR_F_COMPLETE was never written "
                     "and this run does NOT qualify", t->retained,
                     swhy[0] != '\0' ? swhy : "no reason given");
        return 1;
    }
    if (!capture_ok) {
        if (why != NULL && whycap > 0)
            snprintf(why, (size_t) whycap,
                     "every record was retained and sealed, but the capture or "
                     "the comparison itself refused, so this run does NOT "
                     "qualify; the artifact is COMPLETE and marked FAILED so "
                     "its %u record(s) can be read", t->retained);
        return 1;
    }
    return 0;
}

void sl_bgop_snap_summary(const struct sl_bgsnap *s,
                          const struct sl_bgop_snap_tally *t)
{
    if (t == NULL) return;
    printf("    draws captured %u, draws with no vertices %u, vertices "
           "compared %u\n", t->draws_captured, t->draws_empty, t->vertices);
    printf("    components: %u compared with %u differing, %u advisory with %u "
           "differing, %u with NO ORACLE\n", t->compared, t->differing,
           t->advisory, t->advisory_differing, t->no_oracle);
    /* RETENTION, REPORTED SEPARATELY FROM THE ARITHMETIC. The two are
     * independent facts about the run and neither is folded into the other: a
     * retention failure does not alter a comparison count, and matching
     * arithmetic does not excuse one. */
    printf("    retention: %u record(s) retained, %u dropped by the cap, %u "
           "write failure(s)\n", t->retained, t->sink_dropped, t->sink_failed);
    if (t->reported >= SL_BGOP_SNAP_REPORT_CAP)
        printf("    (the raw-bit listing stopped at %d lines; the counts above "
               "are complete)\n", SL_BGOP_SNAP_REPORT_CAP);
    printf("    WHAT THE COMPARED COUNT RESTS ON: gl_Position x, y and w against\n"
           "    the recorded CPU clip position; z derived from the draw's OWN\n"
           "    dmode and coefficient pair (SINGLE the clamped z, FAR0\n"
           "    fk1*w-fk2, FAR1 the unclamped z); the colour on the CONDITIONAL\n"
           "    c/255 assumption and only where SL_BGSNAP_S_COLOR_ARRAY is\n"
           "    RECORDED in SL_BGSNAP_D_STATE_SET and says the array was on; the\n"
           "    fog on the identity, and only where its own word is likewise\n"
           "    recorded and on; a texcoord component only where its four\n"
           "    associations agree bit for bit.\n");
    printf("    WHAT IT DOES NOT COVER: a dmode outside the three has NO ORACLE\n"
           "    for z and is not defaulted; an UNRECORDED state word is absent\n"
           "    evidence and a word recorded as zero says the array was off - in\n"
           "    both cases that field has no oracle, for different reasons; the\n"
           "    non-dyadic texcoord and denormal cases are ADVISORY and outside\n"
           "    the verdict; gl_FrontColor and gl_BackColor remain\n"
           "    indistinguishable, so their exchange is undetected; whether the\n"
           "    production pass ever enables texture unit 1 is still unproved;\n"
           "    and NO fixed-function normalisation - interpolation, clamping,\n"
           "    the raster stage - is measured or assumed by any number above.\n");
    if (s != NULL)
        printf("    frame %u, read %u, cloud %u, recipe \"%s\"\n",
               s->frame, s->read, s->cloud, s->recipe);
}

/* ======================================================================== *
 * THE FIXTURE: a snapshot written from a declared source table, so the reader
 * can be proved against something other than itself.
 *
 * The source is the two tables below. They are serialised to bytes by explicit
 * little-endian stores at the format header's OWN offsets, the file is read
 * back by the reader, and every field is required to survive bit for bit. The
 * patterns include a NaN, a denormal and a negative zero precisely because a
 * reader that widened, normalised or byte-swapped anything would change them
 * and a reader that did not cannot.
 *
 * The files are RETAINED, not removed: a refusal that cannot be re-examined is
 * a worse diagnostic than one that can.
 * ======================================================================== */

/* THREE DRAWS, because the properties under test need three. One draw per depth
 * mode, so gl_Position.z is exercised on each of SINGLE, FAR0 and FAR1 rather
 * than on one of them; and three distinct state-recording situations, so the
 * bitmask's three outcomes - recorded and enabled, not recorded at all, and
 * recorded as genuinely off - are each measured instead of described. */
#define FX_DRAWS       3u
#define FX_VERTS       3u
#define FX_INDICES     6u
#define FX_DRAW_OFF    SL_BGSNAP_HEADER_BYTES
#define FX_VERTEX_OFF  (FX_DRAW_OFF + FX_DRAWS * SL_BGSNAP_DRAW_BYTES)
#define FX_INDEX_OFF   (FX_VERTEX_OFF + FX_VERTS * SL_BGSNAP_VERTEX_BYTES)
#define FX_FILE_BYTES  (FX_INDEX_OFF + FX_INDICES * SL_BGSNAP_INDEX_BYTES)

/* The defects, one per fixture, each planting exactly one. */
enum {
    FX_CLEAN = 0,
    FX_BAD_MAGIC,
    FX_BAD_VERSION,
    FX_BAD_HEADER_BYTES,
    FX_BAD_DRAW_BYTES,
    FX_BAD_VERTEX_BYTES,
    FX_NO_COMPLETE,
    FX_COUNT_OVER_MAX,
    FX_COUNT_PRODUCT_OVERFLOW,
    FX_OFFSET_OUTSIDE,
    FX_OFFSET_SUM_OVERFLOW,
    FX_OVERLAP,
    FX_TRUNCATED,
    FX_DRAW_RANGE,
    FX_INDEX_VALUE,
    FX_BAD_INDEX_BYTES,
    FX_NDEFECTS
};

static const char *const FX_NAME[FX_NDEFECTS] = {
    "clean", "bad-magic", "bad-version", "bad-header-bytes", "bad-draw-bytes",
    "bad-vertex-bytes", "no-complete", "count-over-max",
    "count-product-overflow", "offset-outside", "offset-sum-overflow",
    "overlap", "truncated-mid-array", "draw-range-past-index-array",
    "index-value-past-vertex-count", "bad-index-bytes"
};

static struct sl_bgsnap_draw   fx_draw[FX_DRAWS];
static struct sl_bgsnap_vertex fx_vert[FX_VERTS];
static unsigned                fx_index[FX_INDICES];
static unsigned char           fx_exe[32], fx_src[32];
static char                    fx_dir[480];

static void put_u32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char) (v & 0xFFu);
    p[1] = (unsigned char) ((v >> 8) & 0xFFu);
    p[2] = (unsigned char) ((v >> 16) & 0xFFu);
    p[3] = (unsigned char) ((v >> 24) & 0xFFu);
}

/* A float into a stored 32-bit pattern. The fixture holds patterns, not floats,
 * so the one place a float becomes a pattern is here. */
static void fx_f32(unsigned *slot, float f)
{
    memcpy(slot, &f, sizeof *slot);
}

/* A deterministic spread of patterns. Not a random source: the check recomputes
 * nothing from it, it only has to put a different value in every slot. */
static unsigned fx_word(unsigned salt)
{
    unsigned x = salt * 2654435761u + 0x9E3779B9u;
    x ^= x >> 15; x *= 0x85EBCA6Bu; x ^= x >> 13;
    return x;
}

/* The dyadic matrix the probe already uses, so one draw's texcoords are
 * association-invariant and classify EXACT while the other's are not recorded
 * at all and classify NO ORACLE. Both outcomes are then measured rather than
 * described. */
static void fx_build(void)
{
    unsigned i, j;

    memset(fx_draw, 0, sizeof fx_draw);
    memset(fx_vert, 0, sizeof fx_vert);
    for (i = 0u; i < 32u; i++) { fx_exe[i] = (unsigned char) (0x10u + i);
                                 fx_src[i] = (unsigned char) (0xF0u - i); }

    for (i = 0u; i < FX_DRAWS; i++) {
        struct sl_bgsnap_draw *d = &fx_draw[i];
        d->mesh     = fx_word(0x100u + i * 64u + 1u);
        d->dmode    = i;      /* SINGLE, FAR0, FAR1 - one draw per mode      */
        d->sc_bits  = fx_word(0x100u + i * 64u + 7u);
        d->post_bits= fx_word(0x100u + i * 64u + 8u);
        for (j = 0u; j < 3u; j++)  d->org_bits[j] = fx_word(0x200u + i * 64u + j);
        for (j = 0u; j < 16u; j++) d->C_bits[j]   = fx_word(0x300u + i * 64u + j);
        for (j = 0u; j < 16u; j++) d->state[j]    = fx_word(0x600u + i * 64u + j) | 1u;
        /* Dyadic coefficients, so FAR0's fk1 * w - fk2 is exact for the dyadic
         * clip w values below and the comparison rests on the arithmetic rather
         * than on a rounding that happened to agree. */
        fx_f32(&d->fk1_bits, 2.0f);
        fx_f32(&d->fk2_bits, 0.5f);
        d->first_index = i * 2u;
        d->index_count = 2u;
        d->state_set   = 0xFFFFFFFFu;     /* every word recorded, by default */

        if (i == 1u) {
            /* DRAW 1: neither texture matrix recorded, and the two array words
             * hold NON-ZERO values while their MASK BITS ARE CLEAR. This is the
             * biting case for the mask: a classifier that read the value and
             * ignored the mask would call these EXACT, and they are absent
             * evidence. */
            d->flags = SL_BGSNAP_DF_GPU | SL_BGSNAP_DF_FOGLESS;
            for (j = 0u; j < 16u; j++) {
                d->texmat_bits[0][j] = fx_word(0x400u + i * 64u + j);
                d->texmat_bits[1][j] = fx_word(0x500u + i * 64u + j);
            }
            d->state[SL_BGSNAP_S_COLOR_ARRAY] = 1u;
            d->state[SL_BGSNAP_S_FOG_ARRAY]   = 1u;
            d->state_set &= ~(1u << SL_BGSNAP_S_COLOR_ARRAY);
            d->state_set &= ~(1u << SL_BGSNAP_S_FOG_ARRAY);
        } else {
            d->flags = SL_BGSNAP_DF_GPU | SL_BGSNAP_DF_TEXMAT0
                     | SL_BGSNAP_DF_TEXMAT1 | SL_BGSNAP_DF_UV1;
            for (j = 0u; j < 16u; j++) {
                memcpy(&d->texmat_bits[0][j], &M0_EXACT[j], sizeof(unsigned));
                memcpy(&d->texmat_bits[1][j], &M1_EXACT[j], sizeof(unsigned));
            }
            if (i == 0u) {
                /* DRAW 0: everything recorded and both arrays enabled - and
                 * SL_BGSNAP_S_BLEND_ON recorded as a GENUINE ZERO, its bit set.
                 * That pair is the whole point of the mask: blending off is a
                 * real measurement, and a rule that read zero as "unrecorded"
                 * would discard it. */
                d->state[SL_BGSNAP_S_COLOR_ARRAY] = 1u;
                d->state[SL_BGSNAP_S_FOG_ARRAY]   = 1u;
                d->state[SL_BGSNAP_S_BLEND_ON]    = 0u;
            } else {
                /* DRAW 2: the colour array RECORDED AS OFF. The word is zero
                 * and its bit is set, so this is evidence - evidence that
                 * gl_Color was the current colour and that this vertex's bytes
                 * are not its oracle. Distinct from draw 1's absent evidence,
                 * and only the mask separates the two. */
                d->state[SL_BGSNAP_S_COLOR_ARRAY] = 0u;
                /* THE SECOND MATERIAL OF THE SHARED-POOL PAIR. Draw 0 records
                 * the fog array ON and draw 2 records it OFF WITH A KNOWN ZERO,
                 * over the same non-zero vertex pool and with vertex 1 in both
                 * ranges - so the two draws must disagree about that one
                 * vertex's fog, and a consumer that read the pool for both
                 * cannot pass. */
                d->state[SL_BGSNAP_S_FOG_ARRAY]   = SL_BGSNAP_FOG_OFF_ZERO;
            }
        }
    }

    for (i = 0u; i < FX_VERTS; i++) {
        struct sl_bgsnap_vertex *v = &fx_vert[i];
        float n = (float) (i + 1u);
        for (j = 0u; j < 3u; j++) v->pos_bits[j] = fx_word(0x700u + i * 32u + j);
        for (j = 0u; j < 4u; j++)
            v->col[j] = (unsigned char) ((fx_word(0x800u + i * 32u + j) >> 11) & 0xFFu);
        /* Dyadic coordinates, so the texcoord product cannot round. */
        fx_f32(&v->uv_bits[0][0],  3.0f);
        fx_f32(&v->uv_bits[0][1], -5.0f);
        fx_f32(&v->uv_bits[1][0], -1.25f);
        fx_f32(&v->uv_bits[1][1],  6.0f);
        /* Dyadic clip components and a dyadic unclamped z, for the same reason:
         * FAR0's product and the two recorded depths must be exact so that a
         * difference in the comparison can only be a real difference. */
        fx_f32(&v->clip_bits[0],  2.0f * n);
        fx_f32(&v->clip_bits[1], -0.5f * n);
        fx_f32(&v->clip_bits[2],  0.75f * n);
        fx_f32(&v->clip_bits[3],  4.0f * n);
        fx_f32(&v->zraw_bits,     8.0f * n + 0.5f);
        v->fog_bits = fx_word(0xA00u + i);
    }
    /* THREE PATTERNS NO ARITHMETIC MAY TOUCH, planted on purpose: a quiet NaN,
     * a denormal and a negative zero. A reader that widened a float to double
     * and back, normalised a denormal, or canonicalised a NaN would change at
     * least one of these; one that moves bytes cannot.
     *
     * The denormal w on vertex 0 does double duty: vertex 0 is covered by the
     * SINGLE draw and by the FAR0 draw, both of which read w, so it also
     * exercises the depth advisory on exactly the two modes that depend on it. */
    fx_vert[0].clip_bits[3] = 0x00000001u;        /* the smallest denormal    */
    fx_vert[1].fog_bits     = 0x7FC00001u;        /* a quiet NaN              */
    fx_vert[2].clip_bits[0] = 0x80000000u;        /* negative zero            */

    for (i = 0u; i < FX_INDICES; i++) fx_index[i] = i % FX_VERTS;
}

static void fx_serialise(unsigned char *buf)
{
    unsigned i, j;

    memset(buf, 0, FX_FILE_BYTES);
    put_u32(buf + SL_BGSNAP_H_MAGIC,        SL_BGSNAP_MAGIC);
    put_u32(buf + SL_BGSNAP_H_VERSION,      SL_BGSNAP_VERSION);
    put_u32(buf + SL_BGSNAP_H_HEADER_BYTES, SL_BGSNAP_HEADER_BYTES);
    put_u32(buf + SL_BGSNAP_H_FLAGS,        SL_BGSNAP_F_COMPLETE);
    put_u32(buf + SL_BGSNAP_H_DRAWS,        FX_DRAWS);
    put_u32(buf + SL_BGSNAP_H_VERTICES,     FX_VERTS);
    put_u32(buf + SL_BGSNAP_H_INDICES,      FX_INDICES);
    put_u32(buf + SL_BGSNAP_H_DRAW_OFF,     FX_DRAW_OFF);
    put_u32(buf + SL_BGSNAP_H_VERTEX_OFF,   FX_VERTEX_OFF);
    put_u32(buf + SL_BGSNAP_H_INDEX_OFF,    FX_INDEX_OFF);
    put_u32(buf + SL_BGSNAP_H_FILE_BYTES,   FX_FILE_BYTES);
    put_u32(buf + SL_BGSNAP_H_FRAME,        0x11223344u);
    put_u32(buf + SL_BGSNAP_H_READ,         0x55667788u);
    put_u32(buf + SL_BGSNAP_H_CLOUD,        0x99AABBCCu);
    put_u32(buf + SL_BGSNAP_H_DRAW_BYTES,   SL_BGSNAP_DRAW_BYTES);
    put_u32(buf + SL_BGSNAP_H_VERTEX_BYTES, SL_BGSNAP_VERTEX_BYTES);
    put_u32(buf + SL_BGSNAP_H_INDEX_BYTES,  SL_BGSNAP_INDEX_BYTES);
    memcpy(buf + SL_BGSNAP_H_EXE_SHA, fx_exe, 32u);
    memcpy(buf + SL_BGSNAP_H_SRC_SHA, fx_src, 32u);
    memcpy(buf + SL_BGSNAP_H_RECIPE, "sl_bgop fixture, synthetic, no ROM", 34u);

    for (i = 0u; i < FX_DRAWS; i++) {
        const struct sl_bgsnap_draw *d = &fx_draw[i];
        unsigned char *p = buf + FX_DRAW_OFF + i * SL_BGSNAP_DRAW_BYTES;
        put_u32(p + SL_BGSNAP_D_FLAGS,       d->flags);
        put_u32(p + SL_BGSNAP_D_MESH,        d->mesh);
        put_u32(p + SL_BGSNAP_D_DMODE,       d->dmode);
        put_u32(p + SL_BGSNAP_D_FK1,         d->fk1_bits);
        put_u32(p + SL_BGSNAP_D_FK2,         d->fk2_bits);
        put_u32(p + SL_BGSNAP_D_FIRST_INDEX, d->first_index);
        put_u32(p + SL_BGSNAP_D_INDEX_COUNT, d->index_count);
        put_u32(p + SL_BGSNAP_D_SC,          d->sc_bits);
        put_u32(p + SL_BGSNAP_D_POST,        d->post_bits);
        for (j = 0u; j < 3u; j++)
            put_u32(p + SL_BGSNAP_D_ORG + 4u * j, d->org_bits[j]);
        for (j = 0u; j < 16u; j++) {
            put_u32(p + SL_BGSNAP_D_C + 4u * j,       d->C_bits[j]);
            put_u32(p + SL_BGSNAP_D_TEXMAT0 + 4u * j, d->texmat_bits[0][j]);
            put_u32(p + SL_BGSNAP_D_TEXMAT1 + 4u * j, d->texmat_bits[1][j]);
            put_u32(p + SL_BGSNAP_D_STATE + 4u * j,   d->state[j]);
        }
        put_u32(p + SL_BGSNAP_D_STATE_SET, d->state_set);
    }

    for (i = 0u; i < FX_VERTS; i++) {
        const struct sl_bgsnap_vertex *v = &fx_vert[i];
        unsigned char *p = buf + FX_VERTEX_OFF + i * SL_BGSNAP_VERTEX_BYTES;
        for (j = 0u; j < 3u; j++)
            put_u32(p + SL_BGSNAP_V_POS + 4u * j, v->pos_bits[j]);
        memcpy(p + SL_BGSNAP_V_COL, v->col, 4u);
        put_u32(p + SL_BGSNAP_V_UV0,      v->uv_bits[0][0]);
        put_u32(p + SL_BGSNAP_V_UV0 + 4u, v->uv_bits[0][1]);
        put_u32(p + SL_BGSNAP_V_UV1,      v->uv_bits[1][0]);
        put_u32(p + SL_BGSNAP_V_UV1 + 4u, v->uv_bits[1][1]);
        for (j = 0u; j < 4u; j++)
            put_u32(p + SL_BGSNAP_V_CLIP + 4u * j, v->clip_bits[j]);
        put_u32(p + SL_BGSNAP_V_FOG,  v->fog_bits);
        put_u32(p + SL_BGSNAP_V_ZRAW, v->zraw_bits);
    }

    for (i = 0u; i < FX_INDICES; i++)
        put_u32(buf + FX_INDEX_OFF + i * SL_BGSNAP_INDEX_BYTES, fx_index[i]);
}

/* Writes one fixture, with exactly one defect planted, and returns its path in
 * `path`. Binary mode is load-bearing: a text-mode write on this platform would
 * turn every 0x0A byte in a float pattern into 0x0D 0x0A and corrupt records
 * the defect never touched. */
static int fx_write(int defect, char *path, int pathcap)
{
    static unsigned char buf[FX_FILE_BYTES];
    unsigned bytes = FX_FILE_BYTES;
    FILE *f;

    fx_serialise(buf);
    switch (defect) {
    case FX_CLEAN: break;
    case FX_BAD_MAGIC:
        put_u32(buf + SL_BGSNAP_H_MAGIC, SL_BGSNAP_MAGIC ^ 0x01000000u); break;
    case FX_BAD_VERSION:
        put_u32(buf + SL_BGSNAP_H_VERSION, SL_BGSNAP_VERSION + 1u); break;
    case FX_BAD_HEADER_BYTES:
        put_u32(buf + SL_BGSNAP_H_HEADER_BYTES, SL_BGSNAP_HEADER_BYTES - 1u); break;
    case FX_BAD_DRAW_BYTES:
        put_u32(buf + SL_BGSNAP_H_DRAW_BYTES, SL_BGSNAP_DRAW_BYTES + 1u); break;
    case FX_BAD_VERTEX_BYTES:
        put_u32(buf + SL_BGSNAP_H_VERTEX_BYTES, SL_BGSNAP_VERTEX_BYTES - 4u); break;
    case FX_BAD_INDEX_BYTES:
        /* 8-byte indices. Unchecked this reads every index after the first
         * from the wrong offset and still passes every bounds test. */
        put_u32(buf + SL_BGSNAP_H_INDEX_BYTES, SL_BGSNAP_INDEX_BYTES * 2u); break;
    case FX_NO_COMPLETE:
        /* The writer's own failure path: FAILED set, COMPLETE never written. */
        put_u32(buf + SL_BGSNAP_H_FLAGS, SL_BGSNAP_F_FAILED);
        memcpy(buf + SL_BGSNAP_H_FAILURE, "interrupted", 11u);
        break;
    case FX_COUNT_OVER_MAX:
        put_u32(buf + SL_BGSNAP_H_VERTICES, SL_BGSNAP_MAX_VERTICES + 1u); break;
    case FX_COUNT_PRODUCT_OVERFLOW:
        /* A count whose product with its record size would wrap 32 bits:
         * 0xFFFFFFFF x 4. */
        put_u32(buf + SL_BGSNAP_H_INDICES, 0xFFFFFFFFu); break;
    case FX_OFFSET_OUTSIDE:
        put_u32(buf + SL_BGSNAP_H_INDEX_OFF, FX_FILE_BYTES + 4096u); break;
    case FX_OFFSET_SUM_OVERFLOW:
        put_u32(buf + SL_BGSNAP_H_VERTEX_OFF, 0xFFFFFFF0u); break;
    case FX_OVERLAP:
        put_u32(buf + SL_BGSNAP_H_VERTEX_OFF, FX_DRAW_OFF); break;
    case FX_TRUNCATED:
        /* The header is untouched and still declares the full extent; the file
         * simply stops in the middle of the index array. */
        bytes = FX_INDEX_OFF + 8u;
        break;
    case FX_DRAW_RANGE:
        put_u32(buf + FX_DRAW_OFF + SL_BGSNAP_DRAW_BYTES + SL_BGSNAP_D_INDEX_COUNT,
                99u);
        break;
    case FX_INDEX_VALUE:
        put_u32(buf + FX_INDEX_OFF + (FX_INDICES - 1u) * SL_BGSNAP_INDEX_BYTES,
                FX_VERTS);
        break;
    default: break;
    }

    snprintf(path, (size_t) pathcap, "%ssl_bgop_snap_%s.bin", fx_dir,
             FX_NAME[defect]);
    f = fopen(path, "wb");
    if (f == NULL) return 1;
    if (fwrite(buf, 1u, bytes, f) != bytes) { fclose(f); return 1; }
    fclose(f);
    return 0;
}

/* ---- 6. THE READER, AGAINST ITS OWN SOURCE --------------------------- */

static struct sl_bgsnap g_snap;

static void st_snap_roundtrip(void)
{
    char path[520], why[SL_BGSNAP_WHY_CAP];
    int rc;
    unsigned i, j, bad;

    printf("\n  the snapshot reader: a file written from a declared source "
           "table, read back, every field compared by bit pattern\n");

    fx_build();
    if (fx_write(FX_CLEAN, path, (int) sizeof path) != 0) {
        CHECK(0, "the clean fixture could not be written to %s", path);
        return;
    }
    rc = sl_bgsnap_read(path, &g_snap, why, (int) sizeof why);
    CHECK(rc == 0, "the clean fixture is accepted (%s)",
          rc == 0 ? "no refusal" : why);
    if (rc != 0) return;

    CHECK(g_snap.version == SL_BGSNAP_VERSION &&
          (g_snap.flags & SL_BGSNAP_F_COMPLETE) != 0u &&
          g_snap.draws == FX_DRAWS && g_snap.vertices == FX_VERTS &&
          g_snap.indices == FX_INDICES && g_snap.file_bytes == FX_FILE_BYTES &&
          g_snap.draw_off == FX_DRAW_OFF && g_snap.vertex_off == FX_VERTEX_OFF &&
          g_snap.index_off == FX_INDEX_OFF,
          "the header round-trips: %u draws, %u vertices, %u indices, extent %u, "
          "offsets %u/%u/%u", g_snap.draws, g_snap.vertices, g_snap.indices,
          g_snap.file_bytes, g_snap.draw_off, g_snap.vertex_off, g_snap.index_off);
    CHECK(g_snap.frame == 0x11223344u && g_snap.read == 0x55667788u &&
          g_snap.cloud == 0x99AABBCCu &&
          memcmp(g_snap.exe_sha, fx_exe, 32u) == 0 &&
          memcmp(g_snap.src_sha, fx_src, 32u) == 0 &&
          strcmp(g_snap.recipe, "sl_bgop fixture, synthetic, no ROM") == 0,
          "and so do the counters, the two 32-byte hashes and the recipe text");

    bad = 0u;
    for (i = 0u; i < FX_DRAWS; i++) {
        const struct sl_bgsnap_draw *a = &fx_draw[i], *b = &g_snap.draw[i];
        if (a->flags != b->flags || a->mesh != b->mesh || a->dmode != b->dmode ||
            a->fk1_bits != b->fk1_bits || a->fk2_bits != b->fk2_bits ||
            a->first_index != b->first_index || a->index_count != b->index_count ||
            a->sc_bits != b->sc_bits || a->post_bits != b->post_bits ||
            a->state_set != b->state_set) bad++;
        for (j = 0u; j < 3u; j++)  if (a->org_bits[j] != b->org_bits[j]) bad++;
        for (j = 0u; j < 16u; j++) {
            if (a->C_bits[j] != b->C_bits[j]) bad++;
            if (a->texmat_bits[0][j] != b->texmat_bits[0][j]) bad++;
            if (a->texmat_bits[1][j] != b->texmat_bits[1][j]) bad++;
            if (a->state[j] != b->state[j]) bad++;
        }
    }
    CHECK(bad == 0u,
          "every per-draw field of all %u draws survives bit for bit (%u "
          "mismatched) - the uniform set, both texture matrices, all 16 "
          "bound-state words and the mask saying which of them are real",
          FX_DRAWS, bad);
    /* The mask must not be uniform across the table, or a reader that ignored
     * it entirely would round-trip and every state-gated oracle would be
     * decided by luck. */
    CHECK(g_snap.draw[0].state_set != g_snap.draw[1].state_set,
          "and the mask differs between draws (0x%08X against 0x%08X), so a "
          "reader that dropped it could not pass this round-trip",
          g_snap.draw[0].state_set, g_snap.draw[1].state_set);

    bad = 0u;
    for (i = 0u; i < FX_VERTS; i++) {
        const struct sl_bgsnap_vertex *a = &fx_vert[i], *b = &g_snap.vertex[i];
        for (j = 0u; j < 3u; j++) if (a->pos_bits[j] != b->pos_bits[j]) bad++;
        if (memcmp(a->col, b->col, 4u) != 0) bad++;
        for (j = 0u; j < 2u; j++) {
            if (a->uv_bits[0][j] != b->uv_bits[0][j]) bad++;
            if (a->uv_bits[1][j] != b->uv_bits[1][j]) bad++;
        }
        for (j = 0u; j < 4u; j++) if (a->clip_bits[j] != b->clip_bits[j]) bad++;
        if (a->fog_bits != b->fog_bits) bad++;
        if (a->zraw_bits != b->zraw_bits) bad++;
    }
    CHECK(bad == 0u,
          "and every per-vertex field of all %u vertices (%u mismatched), the "
          "clamped clip z and the unclamped z among them", FX_VERTS, bad);
    /* The two depth fields must be DISTINCT in the fixture, or a reader that
     * read one of them twice would round-trip perfectly and still be wrong. */
    bad = 0u;
    for (i = 0u; i < FX_VERTS; i++)
        if (g_snap.vertex[i].clip_bits[2] == g_snap.vertex[i].zraw_bits) bad++;
    CHECK(bad == 0u,
          "and the clamped and unclamped depths differ for all %u vertices (%u "
          "equal), so a reader that read one of the two fields twice could not "
          "pass the round-trip", FX_VERTS, bad);

    CHECK(g_snap.vertex[0].clip_bits[3] == 0x00000001u &&
          g_snap.vertex[1].fog_bits == 0x7FC00001u &&
          g_snap.vertex[2].clip_bits[0] == 0x80000000u,
          "the smallest denormal (0x00000001), a quiet NaN (0x7FC00001) and a "
          "negative zero (0x80000000) come back as the same patterns - nothing "
          "was widened, normalised or canonicalised on the way through");

    bad = 0u;
    for (i = 0u; i < FX_INDICES; i++)
        if (g_snap.index[i] != fx_index[i]) bad++;
    CHECK(bad == 0u, "and all %u indices (%u mismatched)", FX_INDICES, bad);

    CHECK(g_snap.block != NULL &&
          (const void *) g_snap.draw == (const void *) g_snap.block &&
          (const void *) g_snap.vertex >= (const void *) g_snap.block &&
          (const void *) g_snap.index >= (const void *) g_snap.vertex,
          "the three decoded arrays were carved out of ONE allocation, in "
          "order, so the peak is the sum of three validated products and not a "
          "function of anything the file says afterwards");
}

/* ---- 7. AND IT REFUSES, ONE PLANTED DEFECT AT A TIME ----------------- */

/* Each fixture plants exactly one defect into bytes that are otherwise the
 * clean fixture's, so a refusal can only be the planted rule. The rule name is
 * required to be the one expected: a reader that refused everything with the
 * same message would satisfy "it refused" and prove nothing. */
static void st_snap_negatives(void)
{
    static const struct { int defect; const char *rule; } WANT[] = {
        { FX_BAD_MAGIC,              "MAGIC"           },
        { FX_BAD_VERSION,            "VERSION"         },
        { FX_BAD_HEADER_BYTES,       "HEADER_BYTES"    },
        { FX_BAD_DRAW_BYTES,         "DRAW_BYTES"      },
        { FX_BAD_VERTEX_BYTES,       "VERTEX_BYTES"    },
        { FX_NO_COMPLETE,            "INCOMPLETE"      },
        { FX_COUNT_OVER_MAX,         "COUNT_MAX"       },
        { FX_COUNT_PRODUCT_OVERFLOW, "COUNT_MAX"       },
        { FX_OFFSET_OUTSIDE,         "OFFSET"          },
        { FX_OFFSET_SUM_OVERFLOW,    "EXTENT_OVERFLOW" },
        { FX_OVERLAP,                "OVERLAP"         },
        { FX_TRUNCATED,              "TRUNCATED"       },
        { FX_DRAW_RANGE,             "DRAW_RANGE"      },
        { FX_INDEX_VALUE,            "INDEX_VALUE"     },
        { FX_BAD_INDEX_BYTES,        "INDEX_BYTES"     }
    };
    int n = (int) (sizeof WANT / sizeof WANT[0]), i;

    printf("\n  the snapshot reader refuses: one planted defect per fixture, "
           "and the rule that fired must be the planted one\n");
    printf("  (the fixtures are RETAINED under %s)\n", fx_dir);

    for (i = 0; i < n; i++) {
        char path[520], why[SL_BGSNAP_WHY_CAP];
        struct sl_bgsnap s;
        int rc;
        if (fx_write(WANT[i].defect, path, (int) sizeof path) != 0) {
            CHECK(0, "%s: the fixture could not be written", FX_NAME[WANT[i].defect]);
            continue;
        }
        why[0] = '\0';
        rc = sl_bgsnap_read(path, &s, why, (int) sizeof why);
        CHECK(rc != 0 && strncmp(why, WANT[i].rule, strlen(WANT[i].rule)) == 0 &&
              s.draw == NULL && s.vertex == NULL && s.index == NULL &&
              s.block == NULL,
              "%-30s REFUSED by %s, nothing read: %s",
              FX_NAME[WANT[i].defect], WANT[i].rule,
              rc != 0 ? why : "IT WAS ACCEPTED");
        sl_bgsnap_release(&s);
    }

    /* A count whose product with its record size wraps 32 bits is refused by
     * COUNT_MAX above, not by the overflow guard: the maxima are chosen so that
     * every count times its record size is below 2^32, which means no file can
     * reach the multiplication with a wrapping count. The guard is therefore
     * driven directly here, where it can be, rather than assumed from a
     * refusal that a different rule produced. */
    {
        unsigned got = 0xDEADBEEFu;
        CHECK(sl_bgsnap_extent(0xFFFFFFFFu, SL_BGSNAP_INDEX_BYTES, &got) == 0 &&
              got == 0u,
              "the extent guard itself rejects 0xFFFFFFFF x %u and yields no "
              "size (%u), so no allocation can be computed from a wrapping "
              "product", SL_BGSNAP_INDEX_BYTES, got);
        CHECK(sl_bgsnap_extent(SL_BGSNAP_MAX_VERTICES, SL_BGSNAP_VERTEX_BYTES, &got) == 1 &&
              got == SL_BGSNAP_MAX_VERTICES * SL_BGSNAP_VERTEX_BYTES,
              "and accepts the format's own maximum (%u x %u = %u), which is "
              "why COUNT_MAX necessarily fires first for a file",
              SL_BGSNAP_MAX_VERTICES, SL_BGSNAP_VERTEX_BYTES, got);
        CHECK(sl_bgsnap_sum(0xFFFFFFF0u, 0x20u, &got) == 0 && got == 0u,
              "the sum guard rejects 0xFFFFFFF0 + 0x20, which is the wrap an "
              "offset-plus-extent can reach even with a count in bounds (%u)",
              got);
    }

    /* A path that is not there at all, and one that is empty: both refuse by
     * name rather than returning an empty snapshot that reads as a frame with
     * nothing in it. */
    {
        char path[520], why[SL_BGSNAP_WHY_CAP];
        struct sl_bgsnap s;
        snprintf(path, sizeof path, "%ssl_bgop_snap_absent.bin", fx_dir);
        remove(path);
        CHECK(sl_bgsnap_read(path, &s, why, (int) sizeof why) != 0 &&
              strncmp(why, "OPEN", 4) == 0,
              "a snapshot that is not there is refused by OPEN, not read as an "
              "empty frame: %s", why);
        sl_bgsnap_release(&s);
        CHECK(sl_bgsnap_read(NULL, &s, why, (int) sizeof why) != 0 &&
              strncmp(why, "OPEN", 4) == 0, "and so is no path at all: %s", why);
        sl_bgsnap_release(&s);

        /* A file too short to hold the header: the one case where the format's
         * own identity cannot even be looked at. */
        {
            FILE *f;
            snprintf(path, sizeof path, "%ssl_bgop_snap_short.bin", fx_dir);
            f = fopen(path, "wb");
            if (f != NULL) { fwrite("SNP1", 1u, 4u, f); fclose(f); }
            CHECK(sl_bgsnap_read(path, &s, why, (int) sizeof why) != 0 &&
                  strncmp(why, "SHORT_HEADER", 12) == 0,
                  "a 4-byte file with the right magic is refused by "
                  "SHORT_HEADER before any count in it is read: %s", why);
            sl_bgsnap_release(&s);
        }
    }
}

/* ---- 8. the classification, on the fixture's own two draws ----------- */

/* The per-component classes are what decide which numbers a driver run would be
 * judged by, so they are measured on a snapshot whose recorded state is known
 * rather than described in a comment. The fixture's draw 0 records both texture
 * matrices and both array state words; its draw 1 records neither matrix and
 * leaves the colour-array word unrecorded. */
static void st_snap_classes(void)
{
    int pos_xyw = 0, col0 = 0, col1 = 0, col2 = 0, fog0 = 0, fog1 = 0, fog2 = 0;
    int tex0_exact = 0, tex1_noracle = 0, adv_denormal = 0;
    int j;

    printf("\n  the snapshot classification: which components have an oracle, "
           "measured on the fixture's recorded state\n");
    if (g_snap.draws != FX_DRAWS) {
        CHECK(0, "the clean fixture is not loaded, so nothing can be classified");
        return;
    }

    for (j = 0; j < 4; j++) {
        if (j != 2 &&
            sl_bgop_snap_class(&g_snap, 0u, 1u, SL_BGOP_POS, j) == SL_BGOP_EXACT)
            pos_xyw++;
        col0 += (sl_bgop_snap_class(&g_snap, 0u, 1u, SL_BGOP_FRONT, j) == SL_BGOP_EXACT);
        col1 += (sl_bgop_snap_class(&g_snap, 1u, 1u, SL_BGOP_FRONT, j) == SL_BGOP_NO_ORACLE);
        col2 += (sl_bgop_snap_class(&g_snap, 2u, 1u, SL_BGOP_FRONT, j) == SL_BGOP_NO_ORACLE);
        tex0_exact += (sl_bgop_snap_class(&g_snap, 0u, 1u, SL_BGOP_TEX0, j) == SL_BGOP_EXACT);
        tex1_noracle += (sl_bgop_snap_class(&g_snap, 1u, 1u, SL_BGOP_TEX1, j) == SL_BGOP_NO_ORACLE);
    }
    fog0 = sl_bgop_snap_class(&g_snap, 0u, 1u, SL_BGOP_FOG, 0) == SL_BGOP_EXACT;
    fog1 = sl_bgop_snap_class(&g_snap, 1u, 2u, SL_BGOP_FOG, 0) == SL_BGOP_EXACT;
    fog2 = sl_bgop_snap_class(&g_snap, 2u, 1u, SL_BGOP_FOG, 0) == SL_BGOP_EXACT;
    /* Vertex 0's clip w is the smallest denormal, so that one POSITION
     * component is ADVISORY and the other three are not. */
    adv_denormal = sl_bgop_snap_class(&g_snap, 0u, 0u, SL_BGOP_POS, 3) == SL_BGOP_ADVISORY;

    CHECK(pos_xyw == 3, "gl_Position x, y and w are EXACT (%d of 3)", pos_xyw);
    CHECK(adv_denormal == 1,
          "a POSITION component whose reference is a denormal is ADVISORY, not "
          "compared - GLSL's default precision does not require denormal support");

    /* ---- gl_Position.z, ON EACH OF THE THREE DEPTH MODES ---------------
     * The format records both the clamped clip z and the unclamped one, so the
     * expected z follows from the draw's own dmode and coefficient pair and
     * none of the three modes is left without an oracle. Each is checked
     * against the value the rule names, not merely for being EXACT: a
     * classifier that said EXACT while sl_bgop_snap_expect filled the slot from
     * the wrong field would pass a class check and compare the wrong number. */
    {
        float e[SL_BGOP_RECORD_FLOATS];
        int zo = g_fields[SL_BGOP_POS].offset + 2;
        int cls_single, cls_far0, cls_far1, cls_unknown;
        unsigned saved;
        int z_single_ok, z_far0_ok, z_far1_ok;

        cls_single = sl_bgop_snap_class(&g_snap, 0u, 1u, SL_BGOP_POS, 2);
        cls_far0   = sl_bgop_snap_class(&g_snap, 1u, 2u, SL_BGOP_POS, 2);
        cls_far1   = sl_bgop_snap_class(&g_snap, 2u, 1u, SL_BGOP_POS, 2);

        sl_bgop_snap_expect(&g_snap, 0u, 1u, e);
        z_single_ok = bits_of(e[zo]) == g_snap.vertex[1].clip_bits[2];
        sl_bgop_snap_expect(&g_snap, 2u, 1u, e);
        z_far1_ok = bits_of(e[zo]) == g_snap.vertex[1].zraw_bits;
        sl_bgop_snap_expect(&g_snap, 1u, 2u, e);
        {
            float want = sl_bgsnap_f32(g_snap.draw[1].fk1_bits)
                       * sl_bgsnap_f32(g_snap.vertex[2].clip_bits[3])
                       - sl_bgsnap_f32(g_snap.draw[1].fk2_bits);
            z_far0_ok = bits_of(e[zo]) == bits_of(want);
        }

        CHECK(cls_single == SL_BGOP_EXACT && z_single_ok,
              "SL_BGGPU_DEPTH_SINGLE: gl_Position.z is EXACT and its reference "
              "is the recorded CLAMPED clip z - the clamp is already applied in "
              "the record");
        CHECK(cls_far0 == SL_BGOP_EXACT && z_far0_ok,
              "SL_BGGPU_DEPTH_FAR0: EXACT, and its reference is fk1 * w - fk2 "
              "from the draw's OWN recorded coefficient pair, in that "
              "association - the GLSL marks z `precise`");
        CHECK(cls_far1 == SL_BGOP_EXACT && z_far1_ok,
              "SL_BGGPU_DEPTH_FAR1: EXACT, and its reference is the recorded "
              "UNCLAMPED z, which is a different number from the clamped one");

        /* A denormal w makes SINGLE's `c.w > 0.0` clamp and FAR0's product
         * advisory, and leaves FAR1 alone, because FAR1 reads zraw and not w. */
        CHECK(sl_bgop_snap_class(&g_snap, 0u, 0u, SL_BGOP_POS, 2) == SL_BGOP_ADVISORY &&
              sl_bgop_snap_class(&g_snap, 1u, 0u, SL_BGOP_POS, 2) == SL_BGOP_ADVISORY,
              "a denormal w makes z ADVISORY under SINGLE and FAR0, which both "
              "read w - the clamp through its own test and the product through "
              "its arithmetic");

        /* AND A MODE THAT IS NONE OF THE THREE IS NOT GUESSED AT. Driven in
         * memory rather than through a fixture, because an unknown mode is a
         * property of the classifier and not of the file's validity. */
        saved = g_snap.draw[2].dmode;
        g_snap.draw[2].dmode = 99u;
        cls_unknown = sl_bgop_snap_class(&g_snap, 2u, 1u, SL_BGOP_POS, 2);
        sl_bgop_snap_expect(&g_snap, 2u, 1u, e);
        CHECK(cls_unknown == SL_BGOP_NO_ORACLE && bits_of(e[zo]) == 0u,
              "a dmode of 99 leaves z with NO ORACLE and the expectation slot at "
              "zero - an unrecognised mode is not defaulted to SINGLE, because "
              "nothing recorded says what the shader would do with it");
        g_snap.draw[2].dmode = saved;
    }

    /* ---- THE STATE BITMASK, AND ITS THREE OUTCOMES --------------------- */
    CHECK(col0 == 4,
          "the colour is EXACT where SL_BGSNAP_S_COLOR_ARRAY is RECORDED and "
          "says the array was enabled (%d of 4 channels), on the conditional "
          "c/255 assumption and no other", col0);
    CHECK(col1 == 4 && g_snap.draw[1].state[SL_BGSNAP_S_COLOR_ARRAY] != 0u &&
          !sl_bgsnap_state_recorded(&g_snap.draw[1], SL_BGSNAP_S_COLOR_ARRAY),
          "and has NO ORACLE where the word is UNRECORDED (%d of 4) EVEN THOUGH "
          "its stored value is %u and non-zero - the mask decides, so a reader "
          "that read the value and ignored the mask would have called this "
          "EXACT on absent evidence",
          col1, g_snap.draw[1].state[SL_BGSNAP_S_COLOR_ARRAY]);
    CHECK(col2 == 4 && sl_bgsnap_state_recorded(&g_snap.draw[2], SL_BGSNAP_S_COLOR_ARRAY) &&
          g_snap.draw[2].state[SL_BGSNAP_S_COLOR_ARRAY] == 0u,
          "and NO ORACLE where the word is RECORDED AS ZERO (%d of 4) - that is "
          "evidence, not absence: the array was off, so gl_Color was the current "
          "colour and this vertex's bytes are not its oracle either", col2);
    CHECK(sl_bgsnap_state_recorded(&g_snap.draw[0], SL_BGSNAP_S_BLEND_ON) &&
          g_snap.draw[0].state[SL_BGSNAP_S_BLEND_ON] == 0u,
          "a GENUINELY ZERO state survives as a measurement: blending is "
          "recorded as off, with its bit set, and is reported as recorded rather "
          "than as unknown - which a rule reading zero as `not recorded` could "
          "not express at all");
    CHECK(sl_bgsnap_state_recorded(&g_snap.draw[0], 16u) == 0 &&
          sl_bgsnap_state_recorded(NULL, 0u) == 0,
          "and the mask accessor refuses a word index past the sixteen and a "
          "null draw, rather than reading past the array");

    CHECK(fog0 == 1 && fog1 == 0 && fog2 == 1,
          "the fog is EXACT by identity where SL_BGSNAP_S_FOG_ARRAY is recorded "
          "and enabled (draw 0 %d, draw 2 %d) and has no oracle where the word "
          "is unrecorded (draw 1 %d); the format states that the recorded fog "
          "is the value BOUND to gl_FogCoord, which is what makes the identity "
          "an oracle", fog0, fog2, fog1);
    CHECK(tex0_exact == 4,
          "a texcoord component is EXACT only where its four associations agree "
          "bit for bit; the fixture's dyadic matrix gives %d of 4", tex0_exact);
    CHECK(tex1_noracle == 4,
          "and has NO ORACLE where the draw recorded no matrix or no second "
          "coordinate set (%d of 4) - whether the production pass ever enables "
          "unit 1 is still unproved either way", tex1_noracle);

    /* THE SNAPSHOT COMPARISON BITES, driven with the reference's own records so
     * the positive control and one planted alteration use the same loop a
     * driver run would be judged by. */
    {
        static float recs[2 * SL_BGOP_RECORD_FLOATS];
        struct sl_bgop_snap_tally t;
        unsigned k;
        for (k = 0u; k < 2u; k++)
            sl_bgop_snap_expect(&g_snap, 0u, g_snap.index[k],
                                recs + (size_t) k * SL_BGOP_RECORD_FLOATS);
        memset(&t, 0, sizeof t);
        sl_bgop_snap_compare_draw(&g_snap, 0u, recs, 2u, 0, &t, NULL);
        CHECK(t.differing == 0u && t.compared > 0u && t.no_oracle == 0u,
              "POSITIVE CONTROL on the FULLY RECORDED draw: the references' own "
              "records produce no differences and NOTHING is left without an "
              "oracle (%u compared, %u differing, %u advisory, %u no-oracle) - "
              "every field is decided, which is what the state bitmask buys",
              t.compared, t.differing, t.advisory, t.no_oracle);

        /* AND ON THE PARTIALLY RECORDED DRAW, where the colour, the fog and
         * both texcoords have no oracle. It must still produce no differences
         * AND must count those components as no-oracle rather than as
         * agreement: an unmeasured thing reported as a pass is the one failure
         * mode this whole classification exists to prevent. */
        for (k = 0u; k < 2u; k++)
            sl_bgop_snap_expect(&g_snap, 1u, g_snap.index[2u + k],
                                recs + (size_t) k * SL_BGOP_RECORD_FLOATS);
        memset(&t, 0, sizeof t);
        sl_bgop_snap_compare_draw(&g_snap, 1u, recs, 2u, 0, &t, NULL);
        /* 17 undecidable components per vertex, counted rather than fitted:
         * front 4 + back 4 (the colour-array word is unrecorded) + fog 1 (so is
         * the fog-array word) + tex0 4 + tex1 4 (neither matrix was recorded).
         * Position contributes none - FAR0 is a known mode, so its z has an
         * oracle. Two vertices, so 34. */
        CHECK(t.differing == 0u && t.no_oracle == 34u && t.compared > 0u,
              "POSITIVE CONTROL on the PARTIALLY RECORDED draw: no differences, "
              "and the 17 undecidable components per vertex (front 4, back 4, "
              "fog 1, tex0 4, tex1 4) are counted as no-oracle on both (%u of "
              "34), never as agreement (%u compared, %u advisory)",
              t.no_oracle, t.compared, t.advisory);

        for (k = 0u; k < 2u; k++)
            sl_bgop_snap_expect(&g_snap, 0u, g_snap.index[k],
                                recs + (size_t) k * SL_BGOP_RECORD_FLOATS);
        recs[g_fields[SL_BGOP_POS].offset + 0] =
            ulp_up(recs[g_fields[SL_BGOP_POS].offset + 0]);
        memset(&t, 0, sizeof t);
        sl_bgop_snap_compare_draw(&g_snap, 0u, recs, 2u, 0, &t, NULL);
        CHECK(t.differing == 1u,
              "one ULP on gl_Position.x of one vertex is caught, and exactly "
              "that one (%u) - no tolerance anywhere on this path either",
              t.differing);
        memset(&t, 0, sizeof t);
        sl_bgop_snap_compare_draw(&g_snap, 0u, recs, 1u, 0, &t, NULL);
        CHECK(t.vertices == 0u && t.compared == 0u,
              "and a record set shorter than the draw compares NOTHING (%u "
              "vertices, %u components) - a partial capture of a draw is not a "
              "measurement of it", t.vertices, t.compared);
    }
}

/* ---- 9. the disabled path: nothing selected, nothing claimed --------- */

static int g_real_reads;            /* reader calls on a SELECTED snapshot */

/* The one switch that puts an actual frame in front of this probe. Nothing in
 * the build or the harness sets it, so the real-input mode is defined and
 * UNEXERCISED, which is what the report says. */
static const char *snap_selected(void)
{
    const char *p = getenv("SL_BGSNAP");
    return (p != NULL && *p != '\0') ? p : NULL;
}

static void st_snap_disabled(void)
{
    const char *sel = snap_selected();
    struct sl_bgop_tally t;

    printf("\n  the real-input mode: not exercised unless a snapshot is "
           "selected\n");
    if (sel == NULL) {
        CHECK(g_real_reads == 0,
              "SL_BGSNAP is unset, so no actual frame was read (%d real reads) "
              "and the real-input mode is reported as NOT EXERCISED - which is "
              "not the same as passed", g_real_reads);
    } else {
        CHECK(g_real_reads == 1,
              "SL_BGSNAP selects %s, so exactly one real read was attempted "
              "(%d)", sel, g_real_reads);
    }

    /* AND THE EXISTING BEHAVIOUR IS UNCHANGED. The fixtures above wrote files,
     * allocated and freed a block and drove a second comparison loop; if any of
     * that had disturbed the synthetic case table or its records, the original
     * positive control would no longer hold. It is re-run here for that reason
     * rather than for a second opinion on the same data. */
    fill_with_reference();
    compare_all(g_records, 0, &t);
    CHECK(t.differing == 0 && t.advisory_differing == 0 && t.compared > 0,
          "and the synthetic path is untouched by any of it: the reference's own "
          "records still produce no differences (%d of %d compared, %d advisory)",
          t.differing, t.compared, t.advisory);
    CHECK(g_n == sl_bgdp_build_cases() &&
          sl_bgop_class(0, SL_BGOP_FRONT) == SL_BGOP_EXACT,
          "the case table still holds its %d cases and still classifies the "
          "synthetic colour EXACT, so the added class has not leaked into the "
          "path that never produces it", g_n);
}

/* ---- 10. THE RECORD SINK: it keeps everything, and it refuses --------- */

/* WHAT THESE CLOSE. The capture buffer is reused per draw, the comparison kept
 * only counts, and the components with NO ORACLE were counted and dropped
 * before anything about them was recorded - so a driver run measured a whole
 * frame and retained almost none of it. The sink is the fix, and these are the
 * controls that prove it offline: a round-trip through the artifact, the partial
 * path, a truncation, and the refusals. Driven with fabricated records on the
 * CPU, so the code a driver run would use is the code exercised here.
 *
 * The artifacts are written beside the snapshot fixtures and RETAINED, for the
 * same reason those are. */
static void st_sink_controls(void)
{
    static struct sl_bgor_row rows[8];
    struct sl_bgor_info info;
    struct sl_bgop_snap_tally t;
    struct sl_bgor k;
    char path[520], why[256];
    unsigned i, no_oracle_rows = 0u, adv_rows = 0u;
    int rc;

    printf("\n  the record sink: every captured record retained, including the "
           "components with no oracle\n");
    if (g_snap.draws != FX_DRAWS) {
        CHECK(0, "the clean fixture is not loaded, so no records can be retained");
        return;
    }

    /* ---- the round trip -------------------------------------------------
     * Draw 0 (fully recorded) and draw 1 (neither matrix, neither array word)
     * are both driven, so the artifact carries rows whose components are EXACT,
     * ADVISORY and NO_ORACLE. */
    snprintf(path, sizeof path, "%ssl_bgop_sink_roundtrip.bin", fx_dir);
    remove(path);
    rc = sl_bgor_open(&k, path, &g_snap, why, (int) sizeof why);
    CHECK(rc == 0, "the artifact is created (%s)", rc == 0 ? path : why);
    if (rc != 0) return;

    memset(&t, 0, sizeof t);
    {
        static float recs[2 * SL_BGOP_RECORD_FLOATS];
        unsigned d;
        for (d = 0u; d < 2u; d++) {
            unsigned q;
            for (q = 0u; q < 2u; q++)
                sl_bgop_snap_expect(&g_snap, d, g_snap.index[d * 2u + q],
                                    recs + (size_t) q * SL_BGOP_RECORD_FLOATS);
            sl_bgop_snap_compare_draw(&g_snap, d, recs, 2u, 0, &t, &k);
        }
    }
    CHECK(t.retained == 4u && t.sink_failed == 0u,
          "four vertex comparisons across two draws produced four retained rows "
          "(%u) with no sink failure (%u)", t.retained, t.sink_failed);
    CHECK(t.no_oracle > 0u,
          "and the run had %u components with no oracle, which is what the "
          "artifact has to carry and the comparison counts alone cannot",
          t.no_oracle);
    rc = sl_bgor_close(&k, why, (int) sizeof why);
    CHECK(rc == 0, "the artifact closes and is marked complete (%s)",
          rc == 0 ? "complete" : why);

    rc = sl_bgor_read(path, &info, rows, 8u, why, (int) sizeof why);
    CHECK(rc == 0, "and reads back (%s)", rc == 0 ? "accepted" : why);
    if (rc != 0) return;
    CHECK(info.records == 4u && info.dropped == 0u && info.draws == 2u &&
          (info.flags & SL_BGOR_F_COMPLETE) != 0u &&
          (info.flags & SL_BGOR_F_PARTIAL) == 0u &&
          (info.flags & SL_BGOR_F_FAILED) == 0u,
          "with %u rows over %u draws, %u dropped, complete and not partial",
          info.records, info.draws, info.dropped);
    CHECK(info.frame == g_snap.frame && info.read == g_snap.read &&
          info.cloud == g_snap.cloud &&
          memcmp(info.exe_sha, g_snap.exe_sha, 32u) == 0 &&
          memcmp(info.src_sha, g_snap.src_sha, 32u) == 0,
          "and it carries the frame's own identity - frame %u, read %u, cloud "
          "%u and both 32-byte hashes - so the rows can be tied back to the "
          "snapshot they came from", info.frame, info.read, info.cloud);
    {
        int layout_ok = 1;
        for (i = 0u; i < SL_BGOP_NFIELDS; i++)
            if (info.comps[i] != (unsigned) g_fields[i].comps ||
                info.offset[i] != (unsigned) g_fields[i].offset) layout_ok = 0;
        CHECK(layout_ok && info.record_floats == SL_BGOP_RECORD_FLOATS &&
              info.record_bytes == SL_BGOR_RECORD_BYTES,
              "the record layout is IN the artifact - %u floats per record, %u "
              "bytes per row, and all six fields' component counts and offsets "
              "- so a reader never assumes the layout it was compiled against",
              info.record_floats, info.record_bytes);
    }

    /* EVERY COMPONENT OF EVERY ROW, BY BIT PATTERN, against what the
     * comparison would have seen. The references are recomputed here rather
     * than remembered, so this compares the artifact with the probe's own
     * expectation and not with a copy of what was written. */
    {
        unsigned bad = 0u;
        for (i = 0u; i < info.records; i++) {
            unsigned d = i / 2u, q = i % 2u;
            unsigned vi = g_snap.index[d * 2u + q];
            float e[SL_BGOP_RECORD_FLOATS];
            int j;
            if (rows[i].draw != d || rows[i].vert != vi) { bad++; continue; }
            sl_bgop_snap_expect(&g_snap, d, vi, e);
            for (j = 0; j < SL_BGOP_RECORD_FLOATS; j++) {
                int f, comp = 0, found = 0;
                for (f = 0; f < SL_BGOP_NFIELDS; f++)
                    if (j >= g_fields[f].offset &&
                        j < g_fields[f].offset + g_fields[f].comps) {
                        comp = j - g_fields[f].offset; found = f; break;
                    }
                if (rows[i].measured[j] != bits_of(e[j])) bad++;
                if (rows[i].reference[j] != bits_of(e[j])) bad++;
                if (rows[i].cls[j] !=
                    (unsigned char) sl_bgop_snap_class(&g_snap, d, vi, found, comp))
                    bad++;
            }
        }
        CHECK(bad == 0u,
              "every one of the %u x %d components round-trips bit for bit - "
              "measured, reference AND class (%u mismatched)",
              info.records, SL_BGOP_RECORD_FLOATS, bad);
    }
    for (i = 0u; i < info.records; i++) {
        int j, has_no = 0, has_adv = 0;
        for (j = 0; j < SL_BGOP_RECORD_FLOATS; j++) {
            if (rows[i].cls[j] == SL_BGOP_NO_ORACLE) has_no = 1;
            if (rows[i].cls[j] == SL_BGOP_ADVISORY) has_adv = 1;
        }
        no_oracle_rows += (unsigned) has_no;
        adv_rows += (unsigned) has_adv;
    }
    CHECK(no_oracle_rows > 0u && adv_rows > 0u,
          "rows carrying NO_ORACLE components (%u) and ADVISORY ones (%u) are "
          "BOTH in the artifact with their measured bits - the classes the "
          "comparison drops from its verdict are exactly the ones a later "
          "question needs, and they are no longer discarded",
          no_oracle_rows, adv_rows);

    /* ---- create once ---------------------------------------------------- */
    CHECK(sl_bgor_open(&k, path, &g_snap, why, (int) sizeof why) != 0 &&
          strncmp(why, "EXISTS", 6) == 0,
          "a second open on the same path is REFUSED by EXISTS, so an earlier "
          "run's measurements cannot be overwritten: %s", why);

    /* ---- the partial path, with the cap lowered ------------------------- */
    {
        unsigned restored;
        snprintf(path, sizeof path, "%ssl_bgop_sink_partial.bin", fx_dir);
        remove(path);
        CHECK(sl_bgor_set_cap(2u) == 2u,
              "the row cap is lowered to 2 for this control, which is how the "
              "partial path is driven without writing %u rows on every build",
              SL_BGOR_MAX_RECORDS);
        rc = sl_bgor_open(&k, path, &g_snap, why, (int) sizeof why);
        if (rc == 0) {
            static float recs[2 * SL_BGOP_RECORD_FLOATS];
            unsigned d;
            memset(&t, 0, sizeof t);
            for (d = 0u; d < 2u; d++) {
                unsigned q;
                for (q = 0u; q < 2u; q++)
                    sl_bgop_snap_expect(&g_snap, d, g_snap.index[d * 2u + q],
                                        recs + (size_t) q * SL_BGOP_RECORD_FLOATS);
                sl_bgop_snap_compare_draw(&g_snap, d, recs, 2u, 0, &t, &k);
            }
            rc = sl_bgor_close(&k, why, (int) sizeof why);
        }
        restored = sl_bgor_set_cap(SL_BGOR_MAX_RECORDS);
        CHECK(rc == 0 && restored == SL_BGOR_MAX_RECORDS,
              "the capped artifact closes and the cap is restored to %u",
              restored);
        rc = sl_bgor_read(path, &info, rows, 8u, why, (int) sizeof why);
        CHECK(rc == 0 && info.records == 2u && info.dropped == 2u &&
              (info.flags & SL_BGOR_F_PARTIAL) != 0u &&
              (info.flags & SL_BGOR_F_SEALED) != 0u &&
              (info.flags & SL_BGOR_F_COMPLETE) == 0u && info.max_records == 2u,
              "and it reads back as PARTIAL and NOT COMPLETE: %u rows written, "
              "%u DROPPED, under a cap of %u recorded in its own header. It is "
              "SEALED, so the rows it does hold are readable evidence; "
              "COMPLETE is withheld, so those %u rows can never be read as the "
              "whole measurement", info.records, info.dropped, info.max_records,
              info.records);
        CHECK(t.vertices == 4u && t.retained == 2u && t.sink_dropped == 2u &&
              t.sink_failed == 0u,
              "while the COMPARISON still covered all %u vertices - %u rows "
              "retained and %u counted as DROPPED, not as retained. The cap "
              "bounds the artifact and never the measurement, and a dropped row "
              "is neither a skipped comparison nor a kept record",
              t.vertices, t.retained, t.sink_dropped);
    }

    /* ---- truncation ----------------------------------------------------- */
    {
        char tpath[520];
        unsigned char buf[SL_BGOR_HEADER_BYTES + 4u * SL_BGOR_RECORD_BYTES];
        FILE *src, *dst;
        size_t got;
        snprintf(tpath, sizeof tpath, "%ssl_bgop_sink_truncated.bin", fx_dir);
        snprintf(path, sizeof path, "%ssl_bgop_sink_roundtrip.bin", fx_dir);
        remove(tpath);
        src = fopen(path, "rb");
        got = (src != NULL) ? fread(buf, 1u, sizeof buf, src) : 0u;
        if (src != NULL) fclose(src);
        dst = fopen(tpath, "wb");
        /* The header is untouched and still declares four rows; the file stops
         * in the middle of the third. */
        if (dst != NULL && got > SL_BGOR_HEADER_BYTES) {
            fwrite(buf, 1u, SL_BGOR_HEADER_BYTES + 2u * SL_BGOR_RECORD_BYTES
                             + SL_BGOR_RECORD_BYTES / 2u, dst);
            fclose(dst);
        }
        CHECK(sl_bgor_read(tpath, &info, rows, 8u, why, (int) sizeof why) != 0 &&
              strncmp(why, "TRUNCATED", 9) == 0,
              "an artifact cut off mid-row is REFUSED by TRUNCATED, not read as "
              "the rows it happens to hold: %s", why);
    }

    /* ---- the remaining refusals, one planted defect each ---------------- */
    {
        static const struct { const char *name; unsigned off; unsigned val;
                              const char *rule; } PLANT[] = {
            { "bad-magic",         SL_BGOR_H_MAGIC,       0x4F504E54u, "MAGIC"        },
            { "bad-version",       SL_BGOR_H_VERSION,     2u,          "VERSION"      },
            { "bad-record-bytes",  SL_BGOR_H_RECORD_BYTES, 199u,       "RECORD_BYTES" },
            { "bad-record-floats", SL_BGOR_H_RECORD_FLOATS, 20u,       "RECORD_FLOATS"},
            { "bad-layout",        SL_BGOR_H_FIELDS,      3u,          "LAYOUT"       },
            { "no-complete",       SL_BGOR_H_FLAGS,       0u,          "INCOMPLETE"   },
            { "rows-over-cap",     SL_BGOR_H_RECORDS,
              SL_BGOR_MAX_RECORDS + 1u,                                "COUNT_MAX"    }
        };
        int n = (int) (sizeof PLANT / sizeof PLANT[0]), p;
        unsigned char buf[SL_BGOR_HEADER_BYTES + 4u * SL_BGOR_RECORD_BYTES];
        unsigned char clean[sizeof buf];
        size_t got = 0u;
        FILE *f;
        snprintf(path, sizeof path, "%ssl_bgop_sink_roundtrip.bin", fx_dir);
        f = fopen(path, "rb");
        if (f != NULL) { got = fread(clean, 1u, sizeof clean, f); fclose(f); }

        for (p = 0; p < n; p++) {
            char ppath[520];
            snprintf(ppath, sizeof ppath, "%ssl_bgop_sink_%s.bin", fx_dir,
                     PLANT[p].name);
            remove(ppath);
            memcpy(buf, clean, got);
            put_u32(buf + PLANT[p].off, PLANT[p].val);
            f = fopen(ppath, "wb");
            if (f != NULL) { fwrite(buf, 1u, got, f); fclose(f); }
            why[0] = '\0';
            CHECK(sl_bgor_read(ppath, &info, rows, 8u, why, (int) sizeof why) != 0 &&
                  strncmp(why, PLANT[p].rule, strlen(PLANT[p].rule)) == 0,
                  "%-18s REFUSED by %s: %s", PLANT[p].name, PLANT[p].rule,
                  why[0] != '\0' ? why : "IT WAS ACCEPTED");
        }
    }

    /* ---- and a caller with too little room is refused, not part-served -- */
    snprintf(path, sizeof path, "%ssl_bgop_sink_roundtrip.bin", fx_dir);
    CHECK(sl_bgor_read(path, &info, rows, 3u, why, (int) sizeof why) != 0 &&
          strncmp(why, "CAPACITY", 8) == 0,
          "a 4-row artifact read with room for 3 is REFUSED by CAPACITY rather "
          "than part-read, so the first 3 cannot be mistaken for all of them: "
          "%s", why);

    /* ---- SETUP-FAILURE EVIDENCE IS KEPT, NOT DELETED -------------------
     *
     * An artifact from an attempt that failed before its first row records that
     * the attempt happened, at which path and against which frame. Removing it
     * to free the filename destroys that, so a sealed-failed sink LEAVES the
     * file: it carries no completion flag, a reader refuses it as INCOMPLETE,
     * and the next attempt at the same path is refused by EXISTS - which is the
     * create-once rule working rather than an obstacle to route around. */
    {
        char apath[520];
        FILE *probe;
        snprintf(apath, sizeof apath, "%ssl_bgop_sink_setupfail.bin", fx_dir);
        remove(apath);
        rc = sl_bgor_open(&k, apath, &g_snap, why, (int) sizeof why);
        if (rc == 0)
            rc = sl_bgor_seal_failed(&k, "setup", why, (int) sizeof why);
        probe = fopen(apath, "rb");
        CHECK(rc == 0 && probe != NULL,
              "a setup failure SEALS its artifact and LEAVES it on disk as the "
              "evidence that the attempt happened (%s)",
              probe != NULL ? "present" : "it was deleted");
        if (probe != NULL) fclose(probe);
        CHECK(sl_bgor_read(apath, &info, rows, 8u, why, (int) sizeof why) == 0 &&
              (info.flags & SL_BGOR_F_FAILED) != 0u &&
              (info.flags & SL_BGOR_F_COMPLETE) == 0u && info.records == 0u,
              "and it reads back as FAILED with NO completion flag and %u rows, "
              "so it is legible evidence that cannot be mistaken for a "
              "measurement - being readable and being complete are separate "
              "facts, and conflating them is what made this evidence "
              "unopenable", info.records);
        CHECK(sl_bgor_preflight(&g_snap, apath, &i, &no_oracle_rows, why,
                                (int) sizeof why) != 0 &&
              strncmp(why, "EXISTS", 6) == 0,
              "and the next attempt at that path is refused by EXISTS, so the "
              "evidence is not freed for a retry: %s", why);
    }
}

/* ---- 9b. THE THREE-STATE FOG CONTRACT, through the real reader -------- */

/* WHAT THIS CLOSES, AND WHY A DECODER CHECK IS NOT ENOUGH. The fog-array word
 * is per draw and carries three states, and four consumers read it: the
 * classifier, the reference builder, the GL input gather and the GL attribute
 * binding. Under a boolean reading, state 0 lost a real 0.0f oracle and state 2
 * - whose value is 2, and therefore non-zero - was read as array-on and
 * compared against the pool on data whose effective value is unrecorded. The
 * second is the serious one: a confident comparison against a fabricated input.
 *
 * So the fixture is SERIALIZED and read back through sl_bgsnap_read, and the
 * consumers are the probe's own. Draw 0 records the array ON and draw 2 records
 * it OFF WITH A KNOWN ZERO; both index the same non-zero vertex pool and both
 * ranges contain vertex 1. The two draws must therefore disagree about that one
 * vertex's fog - one expecting the pool pattern, the other exactly 0.0f - which
 * no boolean reading of the word can produce.
 *
 * No GL context is created anywhere here. */
static void st_fog_states(void)
{
    struct sl_bgop_fog_plan p;
    unsigned shared_vertex, pool_bits, zero_bits;
    float zf = 0.0f;
    int cls_on, cls_zero;

    printf("\n  the three-state fog contract: one shared interpretation, read "
           "back through the snapshot reader\n");
    if (g_snap.draws != FX_DRAWS || g_snap.vertices != FX_VERTS) {
        CHECK(0, "the clean fixture is not loaded, so the fog states cannot be "
                 "read back");
        return;
    }

    /* The vertex both materials draw: index 1 of draw 0 and index 0 of draw 2. */
    shared_vertex = g_snap.index[1];
    pool_bits = g_snap.vertex[shared_vertex].fog_bits;
    memcpy(&zero_bits, &zf, sizeof zero_bits);
    CHECK(shared_vertex == g_snap.index[4] && pool_bits != zero_bits &&
          g_snap.draw[0].state[SL_BGSNAP_S_FOG_ARRAY] == SL_BGSNAP_FOG_ARRAY_ON &&
          g_snap.draw[2].state[SL_BGSNAP_S_FOG_ARRAY] == SL_BGSNAP_FOG_OFF_ZERO &&
          sl_bgsnap_state_recorded(&g_snap.draw[0], SL_BGSNAP_S_FOG_ARRAY) &&
          sl_bgsnap_state_recorded(&g_snap.draw[2], SL_BGSNAP_S_FOG_ARRAY),
          "the serialized pair survives the reader: vertex %u is drawn by BOTH "
          "draw 0 (fog array ON) and draw 2 (OFF with a known zero), and its "
          "pool fog 0x%08x is NON-ZERO, so the two states cannot give the same "
          "answer", shared_vertex, pool_bits);

    /* ---- state 1: the pool is the oracle ------------------------------- */
    sl_bgop_fog_plan(&g_snap.draw[0], &g_snap.vertex[shared_vertex], &p);
    cls_on = sl_bgop_snap_class(&g_snap, 0u, shared_vertex, SL_BGOP_FOG, 0);
    CHECK(p.kind == SL_BGOP_FOG_POOL && p.has_oracle &&
          p.expect_bits == pool_bits && p.bind_array == 1 && p.set_current == 0,
          "ARRAY_ON: the oracle is the pool value 0x%08x, the replay BINDS the "
          "array and sets no current coordinate", p.expect_bits);

    /* ---- state 0: exactly 0.0f, NOT the pool --------------------------- */
    sl_bgop_fog_plan(&g_snap.draw[2], &g_snap.vertex[shared_vertex], &p);
    cls_zero = sl_bgop_snap_class(&g_snap, 2u, shared_vertex, SL_BGOP_FOG, 0);
    CHECK(p.kind == SL_BGOP_FOG_ZERO && p.has_oracle &&
          p.expect_bits == zero_bits && p.expect_bits != pool_bits &&
          p.bind_array == 0 && p.set_current == 1 && p.current == 0.0f,
          "OFF_ZERO: the oracle is EXACTLY 0.0f (0x%08x), which differs from "
          "this same vertex's pool value 0x%08x; the replay DISABLES the array "
          "and sets the current coordinate to 0.0f", p.expect_bits, pool_bits);
    CHECK(cls_on == SL_BGOP_EXACT && cls_zero == SL_BGOP_EXACT,
          "and BOTH states are EXACT, so state 0's real oracle is kept rather "
          "than discarded as the boolean reading did (on %d, zero %d)",
          cls_on, cls_zero);

    /* THE REFERENCE BUILDER, which is the consumer that decides what is
     * actually compared. The two draws must put DIFFERENT values in the same
     * vertex's fog slot. */
    {
        float e0[SL_BGOP_RECORD_FLOATS], e2[SL_BGOP_RECORD_FLOATS];
        int fo = g_fields[SL_BGOP_FOG].offset;
        sl_bgop_snap_expect(&g_snap, 0u, shared_vertex, e0);
        sl_bgop_snap_expect(&g_snap, 2u, shared_vertex, e2);
        CHECK(bits_of(e0[fo]) == pool_bits && bits_of(e2[fo]) == zero_bits &&
              bits_of(e0[fo]) != bits_of(e2[fo]),
              "the reference builder gives vertex %u the POOL 0x%08x under draw "
              "0 and EXACTLY 0.0f (0x%08x) under draw 2 - the same vertex, two "
              "answers, which an unconditional pool read could not produce",
              shared_vertex, bits_of(e0[fo]), bits_of(e2[fo]));
    }

    /* ---- state 2: NO oracle, and nothing bound or set ------------------
     * Driven in memory, because what is under test is the interpretation of a
     * state value rather than the file's validity. This is the state whose
     * value is NON-ZERO, so a boolean reading called it array-on. */
    {
        unsigned saved = g_snap.draw[2].state[SL_BGSNAP_S_FOG_ARRAY];
        float e[SL_BGOP_RECORD_FLOATS];
        int fo = g_fields[SL_BGOP_FOG].offset;
        int cls;

        g_snap.draw[2].state[SL_BGSNAP_S_FOG_ARRAY] = SL_BGSNAP_FOG_OFF_UNKNOWN;
        sl_bgop_fog_plan(&g_snap.draw[2], &g_snap.vertex[shared_vertex], &p);
        cls = sl_bgop_snap_class(&g_snap, 2u, shared_vertex, SL_BGOP_FOG, 0);
        sl_bgop_snap_expect(&g_snap, 2u, shared_vertex, e);
        CHECK(p.kind == SL_BGOP_FOG_NONE && p.has_oracle == 0 &&
              p.bind_array == 0 && p.set_current == 0 &&
              cls == SL_BGOP_NO_ORACLE && bits_of(e[fo]) == 0u,
              "OFF_UNKNOWN (value %u, which is NON-ZERO): NO oracle, nothing "
              "bound, nothing set, and the reference slot left at zero - the "
              "boolean reading called this array-on and compared it against the "
              "pool, which was a confident comparison of unrecorded data",
              SL_BGSNAP_FOG_OFF_UNKNOWN);

        /* AN UNRECOGNISED VALUE is never read as array-on either. */
        g_snap.draw[2].state[SL_BGSNAP_S_FOG_ARRAY] = 7u;
        sl_bgop_fog_plan(&g_snap.draw[2], &g_snap.vertex[shared_vertex], &p);
        CHECK(p.kind == SL_BGOP_FOG_NONE && p.has_oracle == 0 &&
              p.bind_array == 0 &&
              sl_bgop_snap_class(&g_snap, 2u, shared_vertex, SL_BGOP_FOG, 0)
                  == SL_BGOP_NO_ORACLE,
              "an unrecognised fog-array value of 7 gives NO oracle and binds "
              "nothing - it is not guessed at and never read as array-on");
        g_snap.draw[2].state[SL_BGSNAP_S_FOG_ARRAY] = saved;
    }

    /* ---- the mask bit clear ------------------------------------------- */
    sl_bgop_fog_plan(&g_snap.draw[1], &g_snap.vertex[g_snap.index[2]], &p);
    CHECK(p.kind == SL_BGOP_FOG_NONE && p.has_oracle == 0 && p.bind_array == 0 &&
          !sl_bgsnap_state_recorded(&g_snap.draw[1], SL_BGSNAP_S_FOG_ARRAY) &&
          g_snap.draw[1].state[SL_BGSNAP_S_FOG_ARRAY] != 0u,
          "an UNRECORDED fog-array word gives NO oracle and binds nothing even "
          "though its stored value is %u and non-zero - the mask is consulted "
          "before the value, every time",
          g_snap.draw[1].state[SL_BGSNAP_S_FOG_ARRAY]);

    /* ---- the fogless flag does not enter the decision ----------------- */
    {
        unsigned saved = g_snap.draw[2].flags;
        struct sl_bgop_fog_plan q;
        g_snap.draw[2].flags |= SL_BGSNAP_DF_FOGLESS;
        sl_bgop_fog_plan(&g_snap.draw[2], &g_snap.vertex[shared_vertex], &q);
        CHECK(q.kind == SL_BGOP_FOG_ZERO && q.has_oracle &&
              q.expect_bits == zero_bits,
              "SL_BGSNAP_DF_FOGLESS set on an OFF_ZERO draw changes NOTHING: "
              "the state word remains the only authority on where the fog "
              "coordinate comes from, and BGG_VS forwards gl_FogCoord whatever "
              "the fogless uniform says");
        g_snap.draw[2].flags = saved;
        g_snap.draw[0].flags |= SL_BGSNAP_DF_FOGLESS;
        sl_bgop_fog_plan(&g_snap.draw[0], &g_snap.vertex[shared_vertex], &q);
        CHECK(q.kind == SL_BGOP_FOG_POOL && q.expect_bits == pool_bits &&
              q.bind_array == 1,
              "and on an ARRAY_ON draw it still reads the pool and still binds "
              "the array, so the flag cannot be mistaken for a fog-off signal");
        g_snap.draw[0].flags &= ~SL_BGSNAP_DF_FOGLESS;
    }

    /* ---- and a null draw is refused rather than defaulted ------------- */
    sl_bgop_fog_plan(NULL, NULL, &p);
    CHECK(p.kind == SL_BGOP_FOG_NONE && p.has_oracle == 0 && p.bind_array == 0,
          "a missing draw gives NO oracle and binds nothing, so the decision "
          "has no default that reads the pool");
}

/* ---- 10b. THE RETENTION GATE BITES, with no driver and no full disk --- */

/* THE FAIL-OPEN THIS CLOSES. The runner opened the artifact, printed a line if
 * it could not, counted row failures and capped rows into the tally, and then
 * took its status from the arithmetic alone - so a run whose components all
 * matched returned success while its raw outputs were absent from disk. The
 * close error had the same shape: it left the status untouched.
 *
 * These controls inject tallies and failures into the SAME predicate the runner
 * applies, so what is exercised is the decision that will gate a driver run and
 * not a second copy of it. A matching arithmetic tally is handed in every time:
 * that is the point - exact-looking numbers must not rescue a run whose records
 * were lost. */
static void st_retention_gate(void)
{
    struct sl_bgop_snap_tally t;
    char why[512];
    char path[520], epath[520];
    unsigned rows = 0u, bytes = 0u, restored;
    int rc;

    printf("\n  the retention gate: a lost record refuses the run, however "
           "exactly the arithmetic matched\n");

    /* THE POSITIVE CONTROL: everything retained and sealed. */
    memset(&t, 0, sizeof t);
    t.vertices = 4u; t.retained = 4u; t.compared = 40u;
    CHECK(sl_bgop_retention_ready(&t, 4u, 1, why, (int) sizeof why) == 1,
          "POSITIVE CONTROL: 4 records for 4 comparisons, none dropped, none "
          "failed, sealed - the gate opens");

    /* A ROW WRITE FAILURE, with the arithmetic perfect. */
    memset(&t, 0, sizeof t);
    t.vertices = 4u; t.retained = 3u; t.sink_failed = 1u; t.compared = 40u;
    CHECK(sl_bgop_retention_ready(&t, 4u, 1, why, (int) sizeof why) == 0 &&
          strstr(why, "not on disk") != NULL,
          "one record write failure refuses the run even though %u components "
          "compared and none differed: %s", t.compared, why);

    /* A CAPPED ROW. */
    memset(&t, 0, sizeof t);
    t.vertices = 4u; t.retained = 2u; t.sink_dropped = 2u; t.compared = 40u;
    CHECK(sl_bgop_retention_ready(&t, 4u, 1, why, (int) sizeof why) == 0 &&
          strstr(why, "cap dropped") != NULL,
          "a capped record refuses too - a prefix is not the measurement: %s",
          why);

    /* A RETENTION COUNT MISMATCH, with nothing reported as failed or dropped. */
    memset(&t, 0, sizeof t);
    t.vertices = 4u; t.retained = 3u; t.compared = 40u;
    CHECK(sl_bgop_retention_ready(&t, 4u, 1, why, (int) sizeof why) == 0 &&
          strstr(why, "one row per comparison") != NULL,
          "3 records for 4 comparisons refuses even with no failure and no drop "
          "reported, so a row lost without either counter moving is still "
          "caught: %s", why);

    /* A SHORT RUN whose every produced row WAS retained. */
    memset(&t, 0, sizeof t);
    t.vertices = 3u; t.retained = 3u; t.compared = 30u;
    CHECK(sl_bgop_retention_ready(&t, 4u, 1, why, (int) sizeof why) == 0 &&
          strstr(why, "were expected") != NULL,
          "3 of 4 expected vertices refuses although every row produced was "
          "retained - the full per-draw count is part of the rule: %s", why);

    /* A FAILED SEAL. */
    memset(&t, 0, sizeof t);
    t.vertices = 4u; t.retained = 4u; t.compared = 40u;
    CHECK(sl_bgop_retention_ready(&t, 4u, 0, why, (int) sizeof why) == 0 &&
          strstr(why, "not sealed") != NULL,
          "and an unsealed artifact refuses with all 4 records written - the "
          "close error used to leave the status untouched: %s", why);

    /* AND THE GATE NEVER INVENTS A MISMATCH. */
    memset(&t, 0, sizeof t);
    t.vertices = 4u; t.retained = 0u; t.sink_failed = 4u;
    t.compared = 40u; t.differing = 0u;
    (void) sl_bgop_retention_ready(&t, 4u, 0, why, (int) sizeof why);
    CHECK(t.compared == 40u && t.differing == 0u && t.advisory == 0u &&
          t.no_oracle == 0u,
          "the gate alters NO comparison count while refusing (%u compared, %u "
          "differing) - a retention failure is reported as itself, never as a "
          "fabricated component mismatch", t.compared, t.differing);

    /* ---- THE PREFLIGHT, which is what keeps a context from being built -- */
    printf("\n  the retention preflight: refused before any GL context exists\n");

    /* ENOUGH CAPACITY. The fixture's six index positions across three draws. */
    snprintf(path, sizeof path, "%ssl_bgop_sink_preflight_ok.bin", fx_dir);
    remove(path);
    rc = sl_bgor_preflight(&g_snap, path, &rows, &bytes, why, (int) sizeof why);
    CHECK(rc == 0 && rows == FX_INDICES &&
          bytes == SL_BGOR_HEADER_BYTES + FX_INDICES * SL_BGOR_RECORD_BYTES,
          "enough capacity: the summed index counts give %u row(s) needing %u "
          "byte(s), inside the cap of %u, and the run may proceed", rows, bytes,
          sl_bgor_cap());

    /* OVER CAPACITY, with the cap lowered so no large file is involved. */
    restored = sl_bgor_set_cap(2u);
    rows = bytes = 0u;
    rc = sl_bgor_preflight(&g_snap, path, &rows, &bytes, why, (int) sizeof why);
    CHECK(rc != 0 && strncmp(why, "CAPACITY", 8) == 0 && rows == FX_INDICES,
          "over capacity: %u row(s) needed against a cap of %u is REFUSED, and "
          "the refusal states both figures: %s", rows, restored, why);
    CHECK(sl_bgor_set_cap(SL_BGOR_MAX_RECORDS) == SL_BGOR_MAX_RECORDS,
          "and the cap restores to the format's own index maximum (%u rows, "
          "%u bytes at %u per row) - every snapshot the reader accepts can be "
          "retained whole, so no legal frame is captured and then cut",
          SL_BGOR_MAX_RECORDS,
          SL_BGOR_HEADER_BYTES + SL_BGOR_MAX_RECORDS * SL_BGOR_RECORD_BYTES,
          SL_BGOR_RECORD_BYTES);
    CHECK(SL_BGOR_MAX_RECORDS == SL_BGSNAP_MAX_INDICES &&
          SL_BGOR_MAX_RECORDS >= 271080u,
          "the cap is the format's SL_BGSNAP_MAX_INDICES itself (%u), so it "
          "cannot drift below it, and it covers the 271080 index positions a "
          "real background item carries", SL_BGOR_MAX_RECORDS);

    /* AN EXISTING PATH is refused at preflight, before a context. */
    {
        FILE *f = fopen(path, "wb");
        if (f != NULL) { fputc('x', f); fclose(f); }
        CHECK(sl_bgor_preflight(&g_snap, path, &rows, &bytes, why,
                                (int) sizeof why) != 0 &&
              strncmp(why, "EXISTS", 6) == 0,
              "an existing artifact path is REFUSED by EXISTS at preflight, so "
              "create-once is enforced before a context is built rather than "
              "after: %s", why);
    }

    /* A MISSING DIRECTORY: the open fails and the run must refuse. */
    snprintf(epath, sizeof epath, "%ssl_bgop_absent_dir\\records.bin", fx_dir);
    CHECK(sl_bgor_preflight(&g_snap, epath, &rows, &bytes, why,
                            (int) sizeof why) == 0,
          "a path in a directory that does not exist passes preflight - "
          "preflight sizes and checks for collision, it does not probe "
          "writability");
    {
        struct sl_bgor k;
        CHECK(sl_bgor_open(&k, epath, &g_snap, why, (int) sizeof why) != 0 &&
              strncmp(why, "OPEN", 4) == 0,
              "and the open then REFUSES, which is the gate the runner applies "
              "before it builds a context: %s", why);
    }
    CHECK(sl_bgor_preflight(&g_snap, NULL, &rows, &bytes, why,
                            (int) sizeof why) != 0 &&
          sl_bgor_preflight(NULL, path, &rows, &bytes, why,
                            (int) sizeof why) != 0,
          "and a missing path or a missing snapshot refuses rather than sizing "
          "an artifact from nothing");

    /* ---- THE INJECTED ROW AND CLOSE FAILURES, END TO END -------------- */
    printf("\n  the injected record failures: the whole path, from the write to "
           "the gate\n");

    /* EVERY CASE BELOW GOES THROUGH sl_bgop_finalize_records, which is the
     * function the driver runner calls. It inspects the actual numbers, chooses
     * COMPLETE or failed from them, seals accordingly and only then folds the
     * seal's own outcome into the status. Sealing by hand in a control and then
     * asking a predicate about it is what left the real branch unexercised. */
    {
        /* `fault` drives one scenario: how many rows to let through, which row
         * to fail, whether to fail the close, whether the capture itself
         * refused, what the cap is, and what the finalization is told to
         * expect. The seal is never performed by the control. */
        static const struct {
            const char *name;
            unsigned    fail_row;      /* 0xFFFFFFFF for none                */
            int         fail_close;
            int         capture_ok;
            unsigned    cap;
            unsigned    expect_vertices, expect_draws;
            int         want_bad;      /* finalization must refuse            */
            int         want_readable; /* the artifact must be readable        */
            int         want_complete; /* the artifact must carry COMPLETE    */
            int         want_failed;   /* the artifact must carry FAILED      */
            unsigned    want_rows;
            const char *needle;        /* the refusal must name this          */
            const char *what;
        } FAULT[] = {
            { "all-good",      0xFFFFFFFFu, 0, 1, SL_BGOR_MAX_RECORDS, 2u, 1u,
              0, 1, 1, 0, 2u, NULL,
              "every record retained and sealed, capture clean" },
            { "row-failure",   1u,          0, 1, SL_BGOR_MAX_RECORDS, 2u, 1u,
              1, 1, 0, 1, 1u, "not on disk",
              "a row write fails on otherwise-matching arithmetic" },
            /* AN UNSEALED ARTIFACT IS NOT READABLE, and that is the r549 rule
             * holding: the seal is what makes the header describe the file, so
             * without it no count in it may be trusted. The run's refusal is
             * what the requirement asks for; legibility is not available here
             * because the final counters never landed. */
            { "close-failure", 0xFFFFFFFFu, 1, 1, SL_BGOR_MAX_RECORDS, 2u, 1u,
              1, 0, 0, 0, 0u, "could not be sealed",
              "every row written but the seal fails" },
            { "cap-drop",      0xFFFFFFFFu, 0, 1, 1u,                  2u, 1u,
              1, 1, 0, 1, 1u, "cap dropped",
              "the cap drops a row" },
            { "short-vertices",0xFFFFFFFFu, 0, 1, SL_BGOR_MAX_RECORDS, 3u, 1u,
              1, 1, 0, 1, 2u, "were expected",
              "fewer vertices compared than expected" },
            { "short-draws",   0xFFFFFFFFu, 0, 1, SL_BGOR_MAX_RECORDS, 2u, 2u,
              1, 1, 0, 1, 2u, "were expected",
              "fewer draws captured than expected" },
            /* RECORDS WHOLE, RUN REFUSED: COMPLETE and FAILED together. The
             * records are exactly what a refused run has to be read from, so
             * COMPLETE is not withheld - and FAILED must have reached the disk,
             * which it does not if the mark is applied after the seal. */
            { "capture-failed",0xFFFFFFFFu, 0, 0, SL_BGOR_MAX_RECORDS, 2u, 1u,
              1, 1, 1, 1, 2u, "capture or",
              "records whole, the capture itself refused" }
        };
        int n = (int) (sizeof FAULT / sizeof FAULT[0]), fi;

        for (fi = 0; fi < n; fi++) {
            static float recs[2 * SL_BGOP_RECORD_FLOATS];
            struct sl_bgor k;
            struct sl_bgor_info info;
            static struct sl_bgor_row rows_in[8];
            unsigned q, saved_compared, saved_differing;
            int bad = -1, read_ok;

            snprintf(path, sizeof path, "%ssl_bgop_final_%s.bin", fx_dir,
                     FAULT[fi].name);
            remove(path);
            (void) sl_bgor_set_cap(FAULT[fi].cap);
            memset(&t, 0, sizeof t);
            why[0] = '\0';
            if (sl_bgor_open(&k, path, &g_snap, why, (int) sizeof why) == 0) {
                if (FAULT[fi].fail_row != 0xFFFFFFFFu)
                    sl_bgor_test_fail_row_at(FAULT[fi].fail_row);
                for (q = 0u; q < 2u; q++)
                    sl_bgop_snap_expect(&g_snap, 0u, g_snap.index[q],
                                        recs + (size_t) q * SL_BGOP_RECORD_FLOATS);
                sl_bgop_snap_compare_draw(&g_snap, 0u, recs, 2u, 0, &t, &k);
                t.draws_captured = 1u;
                saved_compared = t.compared;
                saved_differing = t.differing;
                if (FAULT[fi].fail_close) sl_bgor_test_fail_close(1);
                why[0] = '\0';
                bad = sl_bgop_finalize_records(&k, 1, &t, FAULT[fi].expect_vertices,
                                               FAULT[fi].expect_draws,
                                               FAULT[fi].capture_ok, why,
                                               (int) sizeof why);
                CHECK(t.compared == saved_compared &&
                      t.differing == saved_differing && t.differing == 0u,
                      "%-15s finalization altered NO arithmetic count (%u "
                      "compared, %u differing) - no mismatch is invented to "
                      "force a status", FAULT[fi].name, t.compared, t.differing);
            }
            (void) sl_bgor_set_cap(SL_BGOR_MAX_RECORDS);
            sl_bgor_test_fail_row_at(0xFFFFFFFFu);
            sl_bgor_test_fail_close(0);

            CHECK((bad != 0) == (FAULT[fi].want_bad != 0) &&
                  (FAULT[fi].needle == NULL ||
                   strstr(why, FAULT[fi].needle) != NULL),
                  "%-15s %s -> %s%s%s", FAULT[fi].name, FAULT[fi].what,
                  bad ? "REFUSED" : "qualifies",
                  bad ? ": " : "", bad ? why : "");

            why[0] = '\0';
            read_ok = sl_bgor_read(path, &info, rows_in, 8u, why,
                                   (int) sizeof why) == 0;
            CHECK((read_ok != 0) == (FAULT[fi].want_readable != 0) &&
                  (!read_ok ||
                   (info.records == FAULT[fi].want_rows &&
                    ((info.flags & SL_BGOR_F_COMPLETE) != 0u) ==
                        (FAULT[fi].want_complete != 0) &&
                    ((info.flags & SL_BGOR_F_FAILED) != 0u) ==
                        (FAULT[fi].want_failed != 0))),
                  "%-15s its artifact: %s, %u row(s), COMPLETE %s, FAILED %s - "
                  "COMPLETE is never written when a row is missing and never "
                  "withheld when they are all there, and FAILED reaches the "
                  "disk because the mark precedes the seal",
                  FAULT[fi].name,
                  read_ok ? "readable" : "REFUSED (unsealed)",
                  read_ok ? info.records : 0u,
                  (read_ok && (info.flags & SL_BGOR_F_COMPLETE)) ? "set" : "absent",
                  (read_ok && (info.flags & SL_BGOR_F_FAILED)) ? "set" : "absent");
        }
    }

    /* The injections are one-shot, so nothing above can leak into a later
     * control or into a driver run. */
    {
        static float recs[2 * SL_BGOP_RECORD_FLOATS];
        struct sl_bgor k;
        unsigned q;
        snprintf(path, sizeof path, "%ssl_bgop_sink_afterinject.bin", fx_dir);
        remove(path);
        rc = sl_bgor_open(&k, path, &g_snap, why, (int) sizeof why);
        memset(&t, 0, sizeof t);
        if (rc == 0) {
            for (q = 0u; q < 2u; q++)
                sl_bgop_snap_expect(&g_snap, 0u, g_snap.index[q],
                                    recs + (size_t) q * SL_BGOP_RECORD_FLOATS);
            sl_bgop_snap_compare_draw(&g_snap, 0u, recs, 2u, 0, &t, &k);
            t.draws_captured = 1u;
            rc = sl_bgop_finalize_records(&k, 1, &t, 2u, 1u, 1, why,
                                          (int) sizeof why);
        }
        CHECK(rc == 0 && t.retained == 2u && t.sink_failed == 0u,
              "the next run after every injection retains both records, seals "
              "COMPLETE and qualifies - each injection is one-shot and disarms, "
              "so none can leak forward");
    }
}

/* ---- 11. THE DRIVER ROUTING, decided on the CPU ----------------------- */

/* WHICH DRIVER RUNS HAPPEN, AND HOW MANY CONTEXTS THEY COST. The synthetic run
 * and the actual-frame run each create their own context, so a run that wants
 * only the actual work was paying for a second context and for re-measuring the
 * 34 synthetic cases that a previous run had already accepted.
 *
 * `--real` routes to the actual work alone. The default is unchanged: without
 * it both paths run exactly as before, in the same order.
 *
 * This is a function rather than a condition inside main so the control below
 * drives the same decision main does, instead of a second copy of it. It
 * touches no GL and creates nothing. */
struct bgop_route {
    int synthetic_driver;   /* the 34-case synthetic driver run happens      */
    int actual_driver;      /* the snapshot's bulk per-draw capture happens   */
    int contexts;           /* GL contexts the two together will create      */
};

static void bgop_route(int want_gl, int want_real, int have_snapshot,
                       struct bgop_route *r)
{
    memset(r, 0, sizeof *r);
    /* Neither path reaches a driver without --gl, whatever else is asked for. */
    r->synthetic_driver = want_gl && !want_real;
    r->actual_driver    = want_gl && have_snapshot;
    r->contexts         = r->synthetic_driver + r->actual_driver;
}

static void st_route(void)
{
    static const struct { int gl, real, snap, synth, actual, ctx;
                          const char *what; } CASE[] = {
        { 0, 0, 0, 0, 0, 0, "the default with no driver asked for: nothing runs" },
        { 0, 0, 1, 0, 0, 0, "a snapshot selected but no --gl: still nothing" },
        { 1, 0, 0, 1, 0, 1, "--gl with no snapshot: the synthetic run alone" },
        { 1, 0, 1, 1, 1, 2, "--gl with a snapshot: BOTH, which is two contexts" },
        { 1, 1, 1, 0, 1, 1, "--gl --real: the actual work ALONE, one context" },
        { 0, 1, 1, 0, 0, 0, "--real without --gl: no context at all" },
        { 1, 1, 0, 0, 0, 0, "--real with no snapshot: nothing to run" }
    };
    int n = (int) (sizeof CASE / sizeof CASE[0]), i, bad = 0, saved = 0;

    printf("\n  the driver routing: which runs happen, and how many GL contexts "
           "they cost\n");
    for (i = 0; i < n; i++) {
        struct bgop_route r;
        bgop_route(CASE[i].gl, CASE[i].real, CASE[i].snap, &r);
        if (r.synthetic_driver != CASE[i].synth ||
            r.actual_driver != CASE[i].actual || r.contexts != CASE[i].ctx) bad++;
        else if (CASE[i].gl && CASE[i].real && CASE[i].snap) saved = 1;
        CHECK(r.synthetic_driver == CASE[i].synth &&
              r.actual_driver == CASE[i].actual && r.contexts == CASE[i].ctx,
              "%-55s -> synthetic %d, actual %d, contexts %d", CASE[i].what,
              r.synthetic_driver, r.actual_driver, r.contexts);
    }
    CHECK(bad == 0 && saved,
          "so --real performs ONLY the intended actual work, in ONE context "
          "instead of two, and re-measures none of the already-accepted "
          "synthetic cases (%d routing mismatches)", bad);
    {
        /* THE DEFAULT IS UNCHANGED, stated as its own check: without --real the
         * routing is exactly what it was, both runs in both orders. */
        struct bgop_route a, b;
        bgop_route(1, 0, 1, &a);
        bgop_route(1, 0, 0, &b);
        CHECK(a.synthetic_driver == 1 && a.actual_driver == 1 &&
              b.synthetic_driver == 1 && b.actual_driver == 0,
              "and the default without --real still runs the synthetic path in "
              "both cases and the actual path whenever a snapshot is selected, "
              "so nothing about the existing behaviour moved");
    }
    CHECK(sl_bgop_gl_built() == 0 || sl_bgop_gl_built() == 1,
          "the routing was decided with no GL context and no driver call - it "
          "is a predicate over three flags (this build's GL side: %d)",
          sl_bgop_gl_built());
}

/* THE REAL-INPUT MODE, reported whether or not it ran.
 *
 * With no snapshot selected this prints what the mode would do and states
 * plainly that it did NOT run - an unexercised capability reported as
 * unexercised, never as a pass. With one selected it reads it, prints a compact
 * census of what the file holds and what fraction of its components would have
 * an oracle, and says that the GPU comparison itself is still unexercised
 * because reaching it needs a driver and --gl.
 *
 * The census is counts, not records: a real frame is tens of thousands of
 * vertices and a per-vertex listing would be megabytes that tell a reader less
 * than the totals do. */
static void report_real_input(int want_gl)
{
    const char *sel = snap_selected();
    char why[SL_BGSNAP_WHY_CAP];
    struct sl_bgsnap s;
    unsigned d, cls[3];

    printf("\n  the actual-frame path (src/gfx/sl_bgsnap.h)\n");
    if (sel == NULL) {
        printf("    REAL-INPUT MODE: NOT EXERCISED. No snapshot is selected "
               "(SL_BGSNAP is unset),\n"
               "    so no actual frame was read, no draw was captured and "
               "nothing was compared\n"
               "    against real scene data. The checks above exercise the "
               "reader and the\n"
               "    classification on synthetic fixtures only.\n");
        printf("    WHAT SELECTING ONE WOULD DO: read the file through "
               "sl_bgsnap_read, which\n"
               "    refuses any malformed or incomplete snapshot by name, then "
               "- in the -gl\n"
               "    binary with --gl - capture each draw's whole index range in "
               "ONE transform\n"
               "    feedback capture and compare every component that has an "
               "oracle.\n");
        return;
    }

    g_real_reads++;
    printf("    a snapshot is selected: %s\n", sel);
    if (sl_bgsnap_read(sel, &s, why, (int) sizeof why) != 0) {
        printf("    REFUSED: %s\n", why);
        printf("    NOT QUALIFIED: the file was not read, so nothing from it "
               "was classified or compared.\n");
        checks++; fails++;
        return;
    }
    printf("    accepted: %u draws, %u vertices, %u indices, extent %u of %lu "
           "bytes\n", s.draws, s.vertices, s.indices, s.file_bytes,
           s.file_length);
    printf("    frame %u, read %u, cloud %u, recipe \"%s\"\n",
           s.frame, s.read, s.cloud, s.recipe);
    cls[0] = cls[1] = cls[2] = 0u;
    for (d = 0u; d < s.draws; d++) {
        const struct sl_bgsnap_draw *dr = &s.draw[d];
        unsigned k;
        for (k = 0u; k < dr->index_count; k++) {
            unsigned vi = s.index[dr->first_index + k];
            int f, j;
            for (f = 0; f < SL_BGOP_NFIELDS; f++)
                for (j = 0; j < g_fields[f].comps; j++) {
                    int c = sl_bgop_snap_class(&s, d, vi, f, j);
                    if (c == SL_BGOP_EXACT) cls[0]++;
                    else if (c == SL_BGOP_ADVISORY) cls[1]++;
                    else cls[2]++;
                }
        }
    }
    printf("    components: %u would be compared EXACT, %u ADVISORY, %u have NO "
           "ORACLE\n", cls[0], cls[1], cls[2]);
    printf("    THE GPU COMPARISON ITSELF IS UNEXERCISED: it needs the -gl "
           "binary and --gl,\n"
           "    and %s.\n",
           want_gl ? "that run reports separately below"
                   : "neither was used here");
    sl_bgsnap_release(&s);
}

/* ---- the report -------------------------------------------------------- */

static void print_inputs(void)
{
    int i, j;
    printf("\n  the probe inputs and the expected output record, per case "
           "(%d cases, %d floats each):\n", g_n, SL_BGOP_RECORD_FLOATS);
    for (i = 0; i < g_n; i++) {
        const struct sl_bgdp_case *k = &sl_bgdp_cases()[i];
        const struct sl_bgop_attr *a = &g_attrs[i];
        struct sl_bgop_expect e;
        int f;
        sl_bgop_reference(i, &e);
        printf("   %2d %-30s dmode=%d%s\n", i, k->name, k->dmode,
               a->tex_class == SL_BGOP_ADVISORY ? "  [texcoord advisory]" : "");
        printf("      in  colour=(%u,%u,%u,%u) fog=%.9g uv0=(%.9g,%.9g) "
               "uv1=(%.9g,%.9g)\n",
               a->col[0], a->col[1], a->col[2], a->col[3], (double) a->fog,
               (double) a->uv[0][0], (double) a->uv[0][1],
               (double) a->uv[1][0], (double) a->uv[1][1]);
        printf("      why %s\n", a->why);
        for (f = 0; f < SL_BGOP_NFIELDS; f++) {
            printf("      %-9s", g_fields[f].label);
            for (j = 0; j < g_fields[f].comps; j++) {
                int o = g_fields[f].offset + j;
                printf(" %.9g(0x%08x)", (double) e.v[o], bits_of(e.v[o]));
            }
            printf("  [%s]\n",
                   sl_bgop_class(i, f) == SL_BGOP_EXACT ? "exact" : "advisory");
        }
    }
}

/* ---- the driver run, or the line that says what it would be ------------ */

static int run_or_describe(int want_gl)
{
    static float measured[SL_BGOP_MAX_CASES * SL_BGOP_RECORD_FLOATS];
    struct sl_bgop_tally t;
    unsigned counts[3] = { 0u, 0u, 0u };
    char why[512];

    if (!want_gl) {
        printf("\n  NO DRIVER RAN. %s\n",
               sl_bgop_gl_built()
               ? "This build can create a context; it was not asked to (--gl)."
               : "This build has no GL entry point compiled in and does not "
                 "link opengl32, so it cannot create one.");
        printf("  WHAT A DRIVER RUN WOULD DO: compile the pinned production text as\n"
               "  a GL_VERTEX_SHADER in a hidden-window GL 4.0 compatibility context,\n"
               "  link it with these six varyings captured through transform feedback\n"
               "  - gl_Position, gl_FrontColor, gl_BackColor, gl_FogFragCoord,\n"
               "  gl_TexCoord[0], gl_TexCoord[1] - upload each case's eight uniforms\n"
               "  by the module's own names, bind the colour as GL_UNSIGNED_BYTE4,\n"
               "  the fog as GL_FLOAT and the two texture coordinate sets as\n"
               "  2-component GL_FLOAT in the caller's formats, load the two texture\n"
               "  matrices onto the GL_TEXTURE stack of units 0 and 1, draw one\n"
               "  GL_POINTS vertex per case with GL_RASTERIZER_DISCARD enabled, and\n"
               "  read back %d floats per case. %d draws, no window shown, no file\n"
               "  written, nothing installed.\n", SL_BGOP_RECORD_FLOATS, g_n);
        printf("  Command: tools\\windows\\gpudepthprobe.ps1 -Outputs -RunGl\n");
        return 0;
    }

    why[0] = '\0';
    printf("\n  DRIVER RUN\n");
    if (sl_bgop_gl_run(sl_bgdp_cases(), g_attrs, g_n, measured, counts,
                       why, (int) sizeof why) != 0) {
        /* A REFUSAL IS NOT A RESULT. A varying the driver will not capture, a
         * read-back that did not happen, a sentinel that moved - each leaves
         * every output unmeasured, so nothing is compared and the run is
         * UNQUALIFIED rather than passed or failed on fabricated numbers. */
        printf("    REFUSED: the driver run did not complete: %s\n",
               why[0] != '\0' ? why : "no reason given");
        printf("    GL errors %u, captures completed %u, primitives written %u\n",
               counts[0], counts[1], counts[2]);
        printf("    NOT QUALIFIED: no output was measured, so none is compared.\n");
        fails++; checks++;
        return 1;
    }
    /* THE GATE, BEFORE THE COMPARISON. The raw counts are printed first so
     * they are on the record whichever way this goes. */
    printf("    GL errors %u, captures completed %u, primitives written %u\n",
           counts[0], counts[1], counts[2]);
    {
        int compared = 0;
        char gate[512];
        int bad = judge(measured, counts, g_n, 1, &compared, &t,
                        gate, (int) sizeof gate);
        if (!compared) {
            printf("    REFUSED: %s\n", gate[0] != '\0' ? gate : "no reason given");
            printf("    NOT QUALIFIED: the run was not clean and complete, so "
                   "nothing was compared.\n");
            fails++; checks++;
            return 1;
        }
        checks += t.compared;
        fails += t.differing;
        printf("    %d components compared, %d differ; %d advisory components, "
               "%d differ\n", t.compared, t.differing, t.advisory,
               t.advisory_differing);
        return bad;
    }
}

/* ---- the bulk per-draw driver run, or the line that says what it would be -
 *
 * THE WHOLE PATH, END TO END, and it refuses rather than reports at every step
 * it cannot complete: a snapshot must be selected, it must pass the reader, the
 * build must have a GL side, --gl must have been asked for, and then every draw
 * must capture cleanly and pass the readiness gate before one component is
 * compared. Reaching the comparison needs all five. Anything short of that is
 * reported as UNEXERCISED or REFUSED and never as a pass. */
static int run_snapshot_or_describe(int want_gl)
{
    const char *sel = snap_selected();
    struct sl_bgop_snap_tally t;
    struct sl_bgsnap s;
    char why[SL_BGSNAP_WHY_CAP];
    int rc;

    printf("\n  THE BULK PER-DRAW CAPTURE (one transform feedback capture per "
           "draw)\n");
    if (sel == NULL || !want_gl || !sl_bgop_gl_built()) {
        printf("    NOT EXERCISED. It needs all three of: a snapshot selected "
               "through SL_BGSNAP\n"
               "    (%s), the -gl binary (%s) and --gl (%s). No draw was "
               "captured,\n"
               "    no GL context was created and nothing was compared against "
               "an actual frame.\n",
               sel != NULL ? "selected" : "NOT selected",
               sl_bgop_gl_built() ? "this is it" : "this is NOT it",
               want_gl ? "given" : "NOT given");
        return 0;
    }

    if (sl_bgsnap_read(sel, &s, why, (int) sizeof why) != 0) {
        printf("    REFUSED by the reader: %s\n", why);
        printf("    NOT QUALIFIED: no draw was captured, so none is compared.\n");
        checks++; fails++;
        return 1;
    }
    why[0] = '\0';
    rc = sl_bgop_gl_run_snapshot(&s, &t, 1, why, (int) sizeof why);
    sl_bgop_snap_summary(&s, &t);
    if (rc != 0) {
        printf("    REFUSED: %s\n", why[0] != '\0' ? why : "no reason given");
        printf("    NOT QUALIFIED: the counts above are what was gathered "
               "before the refusal and are NOT a measurement.\n");
        checks++; fails++;
    } else {
        /* Only the components that HAD an oracle enter the ledger. The
         * advisory and no-oracle counts are reported and counted nowhere, which
         * is what keeps an unmeasured thing from reading as a passed one. */
        checks += (int) t.compared;
        printf("    %u components with an oracle agreed bit for bit; %u "
               "advisory and %u no-oracle components were recorded and decide "
               "nothing.\n", t.compared, t.advisory, t.no_oracle);
    }
    sl_bgsnap_release(&s);
    return rc;
}

/* ---- main -------------------------------------------------------------- */

int main(int argc, char **argv)
{
    static char cfg[520];
    const char *tmp = getenv("TEMP");
    const char *path;
    int want_gl = 0, want_real = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gl") == 0) want_gl = 1;
        /* THE DIRECT REAL-INPUT MODE. Routes the driver work to the actual
         * frame alone: one GL context instead of two, and none of the 34
         * synthetic cases re-measured. The CPU checks below run in both modes
         * because they cost no context and they are the verification. */
        else if (strcmp(argv[i], "--real") == 0) want_real = 1;
        else {
            fprintf(stderr, "bggpuoutprobe: unknown argument %s "
                            "(only --gl and --real are accepted)\n", argv[i]);
            return 2;
        }
    }
    if (want_gl && !sl_bgop_gl_built()) {
        fprintf(stderr, "bggpuoutprobe: --gl was asked for, but this build has "
                        "no GL side. Build bggpuoutprobe-gl.exe.\n");
        return 2;
    }

    printf("background GPU vertex-output probe (src/gfx/sl_gfx_bggpu.c, BGG_VS)\n");
    printf("SYNTHETIC inputs only - no ROM data, no asset, no installed file.\n");
    printf("Nothing here attributes the sampled pixel at (71,113) to any output,\n");
    printf("expression or draw, and no result below may be reported as that pixel\n");
    printf("being explained or fixed.\n\n");

    if (tmp == NULL || *tmp == '\0') tmp = ".";
    /* The fixtures' own scratch, beside the config the probe already writes
     * there. They are RETAINED after the run: a refusal that cannot be
     * re-examined is a worse diagnostic than one that can. */
    snprintf(fx_dir, sizeof fx_dir, "%s\\", tmp);
    snprintf(cfg, sizeof cfg, "SL_CONFIG=%s\\sl_bggpu_out_probe_config.ini", tmp);
    putenv(cfg);
    path = strchr(cfg, '=') + 1;
    remove(path);
    sl_perf_test_reset();
    sl_settings_init();
    bgt_reset();

    if (sl_bgop_build() <= 0) {
        fprintf(stderr, "bggpuoutprobe: no cases were built\n");
        remove(path);
        return 1;
    }

    printf("  the pinned production text: %u bytes, glsl %s\n",
           sl_bgdp_glsl_bytes(), sl_bgdp_glsl_sha256());
    printf("  uniforms %s\n\n", sl_bgdp_uniforms_sha256());

    st_layout();
    st_builder();
    st_inputs_nontrivial();
    st_texcoord_association();
    st_bites();
    st_readiness_gate();
    st_indistinguishable();
    st_snap_roundtrip();
    st_snap_negatives();
    st_snap_classes();
    st_fog_states();
    st_sink_controls();
    st_retention_gate();
    st_route();
    report_real_input(want_gl);
    st_snap_disabled();
    print_inputs();

    /* THE ROUTED DRIVER WORK. The decision is bgop_route's, which st_route
     * above drives with the same function over every flag combination. */
    {
        struct bgop_route route;
        const char *sel = snap_selected();
        bgop_route(want_gl, want_real, sel != NULL, &route);

        /* FAIL CLOSED. --real names the actual frame as the thing to measure;
         * with no snapshot selected there is nothing to measure, and silently
         * doing nothing would report a clean run that measured nothing at all. */
        if (want_real && sel == NULL) {
            printf("\n  --real was asked for but no snapshot is selected "
                   "(SL_BGSNAP is unset),\n"
                   "  so there is no actual frame to measure. This is a "
                   "REFUSAL, not an empty pass.\n");
            checks++; fails++;
        }

        if (route.synthetic_driver || !want_real) {
            (void) run_or_describe(want_gl);
        } else {
            printf("\n  the synthetic driver run is SKIPPED by --real: its 34 "
                   "cases are not\n"
                   "  re-measured and its GL context is not created. Run "
                   "without --real for it.\n");
        }
        (void) run_snapshot_or_describe(want_gl);
    }

    sl_bgsnap_release(&g_snap);
    remove(path);
    printf("\n%d checks, %d failed\n", checks, fails);
    if (!want_gl)
        printf("NOT QUALIFIED by this run: no driver evaluated BGG_VS. The checks "
               "above fix the capture record's layout, prove the inputs are not "
               "vacuous, prove the exact texcoord cases are association-invariant, "
               "and prove the comparison loop rejects component, channel, field and "
               "case swaps and a single ULP in every field that carries the verdict "
               "- they say nothing about what a driver computes, and nothing about "
               "the sampled pixel.\n");
    return fails == 0 ? 0 : 1;
}
