/* THE BACKGROUND PASS'S WHOLE VERTEX OUTPUT, ON A DRIVER - the contract.
 *
 * WHAT GAP THIS CLOSES, and what it does NOT. The depth probe beside this one
 * (sl_bggpu_depth_probe.h) captures gl_Position through transform feedback and
 * then reads ONE component of it: gl_Position.z. BGG_VS writes six outputs:
 *
 *     gl_Position     = c                                  (x, y, z and w)
 *     gl_FrontColor   = gl_Color
 *     gl_BackColor    = gl_Color
 *     gl_FogFragCoord = gl_FogCoord
 *     gl_TexCoord[0]  = gl_TextureMatrix[0] * gl_MultiTexCoord0
 *     gl_TexCoord[1]  = gl_TextureMatrix[1] * gl_MultiTexCoord1
 *
 * Of those, one float of one output has ever been compared with the CPU path. A
 * matching z does not qualify x, y or w, and says nothing at all about the
 * forwarded colour, fog and texture coordinates. This probe captures all six
 * and compares every component.
 *
 * IT DOES NOT EXPLAIN THE SAMPLED PIXEL. Nothing here attributes the one blue
 * pixel at (71,113) to any output, expression or draw. The probe measures what
 * the vertex stage emits for synthetic inputs; a difference here would be a
 * lead, and an absence of difference here rules nothing in or out about a
 * rasterised frame. No result this probe can produce may be reported as that
 * pixel being explained or fixed.
 *
 * THE SHADER TEXT IS THE PINNED PRODUCTION TEXT, unchanged. This probe adds
 * capture names to the link and touches no expression. sl_bggpu_depth_probe's
 * pin still governs: a shader-text or uniform-list change refuses the build.
 *
 * WHERE THE ORACLE COMES FROM, PER FIELD. An exact comparison is only worth
 * making where something other than a second transcription says what the
 * answer must be. Each field is therefore classified, and the classification
 * is reported beside every number:
 *
 *   POSITION  EXACT. The expressions are marked `precise` in the production
 *             text, which forbids the driver reassociating or fusing them, and
 *             the CPU reference mirrors them in their own association. This is
 *             the same basis the depth probe already rests on, extended from
 *             one component to four.
 *
 *   COLOUR    EXACT, on a stated assumption. gl_Color is the current colour
 *             from a GL_UNSIGNED_BYTE4 array, and conversion from unsigned
 *             normalised fixed-point to floating point is specified as
 *             c / (2^b - 1), so c / 255. The assumption is that the single
 *             division is correctly rounded, which IEEE-754 requires of a
 *             divide. It is an assumption about arithmetic, not about a
 *             driver's choice, and it is recorded rather than buried.
 *
 *   FOG       EXACT. gl_FogCoord is a GL_FLOAT attribute and the assignment is
 *             an identity; there is no arithmetic to disagree about.
 *
 *   TEXCOORD  TWO CLASSES, because `gl_TextureMatrix[i] * gl_MultiTexCoordi`
 *             carries NO `precise` qualifier. The driver may reassociate the
 *             four products and three sums, and may fuse them, so in general
 *             there is no bit-exact oracle for this product and presenting a
 *             CPU transcription as one would be false.
 *               EXACT-BY-REPRESENTABILITY: the primary cases use matrices and
 *             coordinates whose every partial product and partial sum is
 *             exactly representable in binary32, so no rounding occurs on any
 *             association and every order gives the identical result. The
 *             comparison is then exact because the arithmetic is exact, not
 *             because the driver was assumed to associate a particular way.
 *             The matrices are non-identity and the two coordinate sets are
 *             independent, so this is not vacuous.
 *               ADVISORY: one case per unit uses values that DO round, where
 *             association is free to matter. Those are measured and reported
 *             with their raw bits and are kept out of the pass/fail count,
 *             exactly as the existing denormal-w case is. They are not a
 *             tolerance: nothing is compared loosely anywhere. A field is
 *             either compared bit for bit or reported as measured.
 *
 * THE ARRAY FORMATS ARE THE CALLER'S, read out of src/gfx/sl_gfx_dl.c:
 *   model position  glVertexPointer(3, GL_FLOAT, stride, ...)      :16845
 *   colour          glColorPointer(4, GL_UNSIGNED_BYTE, stride,...) :18710
 *   fog             glFogCoordPointer(GL_FLOAT, 0, ...)            :18685
 *   texcoord set 0  glTexCoordPointer(2, GL_FLOAT, stride, ...)     :18689
 * A two-component texcoord array leaves gl_MultiTexCoord0 = (u, v, 0, 1),
 * which is the specified default for the components an array does not supply,
 * and the reference uses exactly that.
 *
 * ONE FIDELITY LIMIT, STATED. The background pass shown at sl_gfx_dl.c:18689
 * binds texture coordinate set 0 only. BGG_VS reads gl_MultiTexCoord1 as well,
 * so this probe drives set 1 with an independent non-trivial array to keep that
 * output from being measured at its default - a zero or default coordinate
 * would make the second texcoord check vacuous. Whether the production
 * background pass ever enables unit 1 is NOT established by this probe and is
 * reported as an open limit, not assumed either way.
 */
#ifndef SL_BGGPU_OUT_PROBE_H
#define SL_BGGPU_OUT_PROBE_H

#include "sl_bggpu_depth_probe.h"
#include "sl_bgsnap_read.h"

/* The record sink, by name only: sl_bgop_sink.h declares the row layout and
 * includes this header's record-float count through its own restatement, so a
 * full include here would close a cycle. Callers that pass a non-NULL sink
 * include sl_bgop_sink.h themselves. */
struct sl_bgor;

/* ---- the capture record, defined ONCE and shared ------------------------
 *
 * The interleaved transform-feedback record is laid out by the order of the
 * varying names and the component count of each varying. gl_FogFragCoord is a
 * float, not a vec4, so the record is NOT six times four floats - it is 21
 * floats, and an off-by-one in that layout would silently shift every field
 * after it and produce confident wrong numbers.
 *
 * So the layout is data, in one table, read by the capture and by the
 * comparison. It cannot be exercised against a driver offline, so the driver
 * run additionally proves it: the buffer is larger than the record and is
 * prefilled with a sentinel, and the capture is refused unless every float
 * past the declared end still holds that sentinel. A layout mistake then
 * becomes a diagnosable refusal instead of a wrong measurement.
 */
enum {
    SL_BGOP_POS = 0,
    SL_BGOP_FRONT,
    SL_BGOP_BACK,
    SL_BGOP_FOG,
    SL_BGOP_TEX0,
    SL_BGOP_TEX1,
    SL_BGOP_NFIELDS
};

#define SL_BGOP_RECORD_FLOATS 21      /* 4 + 4 + 4 + 1 + 4 + 4                */
#define SL_BGOP_BUFFER_FLOATS 32      /* the record plus the sentinel tail    */
#define SL_BGOP_MAX_CASES     SL_BGDP_MAX_CASES

struct sl_bgop_field {
    const char *varying;      /* the name handed to glTransformFeedbackVaryings */
    const char *label;
    int         comps;        /* 4, or 1 for gl_FogFragCoord                  */
    int         offset;       /* float offset of this field within the record */
};

const struct sl_bgop_field *sl_bgop_fields(void);

/* How a field's comparison is justified for a given case. */
enum {
    SL_BGOP_EXACT = 0,        /* compared bit for bit, oracle named in the header */
    SL_BGOP_ADVISORY,         /* measured and reported, kept out of the verdict   */
    /* NO ORACLE AT ALL, which is not the same as advisory. An advisory component
     * has a reference whose last bits a driver may legitimately choose
     * differently. A NO_ORACLE component has NO reference: nothing in the inputs
     * says what the answer must be, so the measured value is recorded and
     * nothing is claimed about it. It never enters either count. Only the
     * snapshot-driven comparison produces this class; the synthetic case table
     * supplies every input it compares, so none of its components is in it. */
    SL_BGOP_NO_ORACLE
};

/* The attribute inputs for one case, in the caller's own formats. Indexed in
 * step with the depth probe's case table, which supplies the uniforms and the
 * model position; this supplies what BGG_VS forwards. */
struct sl_bgop_attr {
    unsigned char col[4];     /* GL_UNSIGNED_BYTE4, the colour array          */
    float         fog;        /* GL_FLOAT, the fog coordinate array           */
    float         uv[2][2];   /* two independent 2-component GL_FLOAT sets    */
    float         M[2][16];   /* the two texture matrices, column order       */
    int           tex_class;  /* SL_BGOP_EXACT or SL_BGOP_ADVISORY            */
    const char   *why;        /* what this input is here to exercise          */
};

/* The expected whole output for one case: the record, in record layout. */
struct sl_bgop_expect {
    float v[SL_BGOP_RECORD_FLOATS];
};

int  sl_bgop_build(void);                       /* returns the case count     */
const struct sl_bgop_attr *sl_bgop_attrs(void);
void sl_bgop_reference(int i, struct sl_bgop_expect *e);

/* The field classification for one case and field. */
int  sl_bgop_class(int icase, int field);

/* ---- the driver side ---------------------------------------------------- */

/* ONE DRIVER RUN, capturing all six outputs. Creates its own hidden-window
 * context, compiles the PINNED production text, links it with the six varyings
 * captured through transform feedback, uploads each case's uniforms and binds
 * its attribute arrays in the caller's formats, draws one point per case with
 * the rasterizer discarded, and writes SL_BGOP_RECORD_FLOATS floats per case
 * into `out`. Returns 0 on success. On any failure - a refused varying, a
 * missing readback, a sentinel that moved - it returns non-zero with the reason
 * in `why`, and the caller must then refuse qualification rather than compare.
 *
 * `counts` receives, in order: GL errors seen, captures completed, and the sum
 * of the primitives-written queries. */
int sl_bgop_gl_run(const struct sl_bgdp_case *cases, const struct sl_bgop_attr *attrs,
                   int n, float *out, unsigned *counts, char *why, int whycap);

/* 1 in the GL build, 0 otherwise. */
int sl_bgop_gl_built(void);

/* ---- THE READINESS GATE ------------------------------------------------
 *
 * THE ONE RULE, used by the driver runner and by the caller that would compare
 * its records - and exercised on the CPU with injected counts, so the decision
 * is regression-tested without a driver or a second GL mock.
 *
 * A GL ERROR IS FATAL AND IS CHECKED BEFORE ANY COMPARISON. The runner can
 * finish with every case captured and every sentinel intact while errors were
 * collected during setup, during a case, or during cleanup; it used to return 0
 * for that, and the caller merely printed the count after comparing. Records
 * produced alongside a GL error are not evidence of agreement, however exactly
 * they match: the state the driver was asked about is not the state it was in.
 * The same holds for an incomplete run - fewer captures than cases, or a
 * primitives-written total that is not one per case - which means some case was
 * never measured at all.
 *
 * Returns 1 only when `counts` describes a clean, complete run of `n` cases:
 * zero GL errors, exactly `n` captures completed and exactly `n` primitives
 * written. Otherwise it returns 0 and writes the reason, and the caller must
 * then report UNMEASURED and compare nothing rather than pass or fail on data
 * it cannot trust. The raw counts are kept and reported either way. */
int sl_bgop_counts_ready(const unsigned *counts, int n, char *why, int whycap);

/* ---- THE SNAPSHOT-DRIVEN COMPARISON ------------------------------------
 *
 * WHAT CHANGES, AND WHAT DOES NOT. The synthetic case table above supplies
 * every input it compares, so every component it compares has a reference. A
 * snapshot supplies inputs the probe did not choose, and for some outputs the
 * recorded inputs DO NOT DETERMINE the answer. Those components have NO ORACLE
 * and are recorded as measured, with no verdict attached. The limits the
 * synthetic path already carries are unchanged and are not widened to cover the
 * snapshot path:
 *
 *   gl_Position.x, .y, .w   EXACT, on the same basis as the synthetic path: the
 *                           expressions are `precise` and the recorded clip
 *                           position is what the CPU loop produced from the same
 *                           inputs in the same association.
 *   gl_Position.z           EXACT, derived from the draw's OWN recorded depth
 *                           mode and coefficient pair exactly as BGG_VS does -
 *                           SINGLE takes the recorded clamped z, FAR0 takes
 *                           fk1 * w - fk2, FAR1 takes the recorded unclamped z.
 *                           The format records BOTH the clamped and the
 *                           unclamped depth (SL_BGSNAP_V_CLIP and
 *                           SL_BGSNAP_V_ZRAW), which is what makes all three
 *                           modes derivable; one field alone left z without an
 *                           oracle. A dmode that is none of the three is NOT
 *                           guessed at: it has no oracle.
 *                           ADVISORY where the shader's own `c.w > 0.0` test or
 *                           its arithmetic depends on a denormal w, since the
 *                           SINGLE clamp and the FAR0 product both read w and a
 *                           driver that flushes denormals has not disagreed
 *                           about arithmetic. FAR1 reads zraw and not w, so it
 *                           is unaffected.
 *   colour                  EXACT on the SAME CONDITIONAL ASSUMPTION as the
 *                           synthetic path - c / 255 with a correctly rounded
 *                           divide - and only where SL_BGSNAP_S_COLOR_ARRAY is
 *                           RECORDED (its bit set in SL_BGSNAP_D_STATE_SET) and
 *                           records the array as enabled. An unrecorded word is
 *                           absent evidence, so whether gl_Color came from the
 *                           array is unknown; a recorded ZERO says the array was
 *                           off, so gl_Color was the current colour and this
 *                           vertex's bytes are not its oracle either. The two
 *                           are different facts and the mask is what tells them
 *                           apart. Front and back remain indistinguishable, as
 *                           the note at the comparison loop records: that is
 *                           unchanged and still unproved.
 *   fog                     EXACT by identity - gl_FogFragCoord = gl_FogCoord -
 *                           and only where SL_BGSNAP_S_FOG_ARRAY is RECORDED and
 *                           records the array as enabled. The format states that
 *                           SL_BGSNAP_V_FOG is the value BOUND to gl_FogCoord
 *                           rather than a separately derived quantity, which is
 *                           precisely what makes the identity an oracle.
 *   texcoord                MEASURED PER COMPONENT, never assumed. The product
 *                           carries no `precise`, so a component is EXACT only
 *                           where the arithmetic is exact: all four partial
 *                           products and three partial sums are computed under
 *                           four different associations and the component is
 *                           EXACT only if all four agree bit for bit. Where they
 *                           do not, it is ADVISORY - measured, reported, outside
 *                           the verdict - exactly as the non-dyadic synthetic
 *                           case is. Without the draw's matrix recorded, or
 *                           without its coordinate set bound, there is no oracle
 *                           at all. Whether the production background pass ever
 *                           enables unit 1 is STILL NOT ESTABLISHED by any of
 *                           this.
 *   any denormal reference  ADVISORY. GLSL's default precision does not require
 *                           denormal support, so a driver that flushes one to
 *                           zero has not disagreed about arithmetic. This is the
 *                           same rule the synthetic denormal-w case carries,
 *                           applied per component rather than per case.
 *
 * NOTHING ELSE IS ASSUMED TO MATCH. No fixed-function normalisation -
 * interpolation, clamping, the raster stage - is compared or claimed, because
 * nothing here measures any of it. A PASS from this path means: every component
 * that had an oracle agreed bit for bit, under the assumptions named above, for
 * the draws that were captured. It means nothing more.
 *
 * A STATE WORD IS REACHED ONLY THROUGH sl_bgsnap_state_recorded. The bitmask,
 * not the value, says whether a word is evidence. This matters because the
 * states that decide an oracle here are genuinely zero when they are off, so a
 * rule that read zero as "not recorded" would discard real evidence and lose
 * oracles for draws whose state was recorded perfectly well.
 */
struct sl_bgop_snap_tally {
    unsigned draws_captured;      /* draws whose capture completed cleanly     */
    unsigned draws_empty;         /* draws with no vertices; nothing to capture */
    unsigned vertices;            /* vertices captured and compared            */
    unsigned compared, differing;
    unsigned advisory, advisory_differing;
    unsigned no_oracle;           /* components recorded with no reference      */
    unsigned reported;            /* raw-bit lines printed, which is capped     */
    unsigned retained;            /* rows the sink actually wrote               */
    unsigned sink_dropped;        /* rows the sink's cap refused; it is bounded */
    unsigned sink_failed;         /* sink write failures; never alters a verdict */
};

/* How many raw-bit difference lines the summary will print before it stops
 * naming individual components and reports only totals. A real frame carries
 * tens of thousands of vertices, so an uncapped report is megabytes and tells a
 * reader less than the first few lines plus a count. */
#define SL_BGOP_SNAP_REPORT_CAP 32

/* The class of one component of one field of one vertex of one draw. */
int sl_bgop_snap_class(const struct sl_bgsnap *s, unsigned draw, unsigned vert,
                       int field, int comp);

/* The expected record for one vertex, in record layout. Components with no
 * oracle are left zero and are never compared. */
void sl_bgop_snap_expect(const struct sl_bgsnap *s, unsigned draw, unsigned vert,
                         float *v);

/* ONE DRAW'S CAPTURED RECORDS against their references. `records` holds
 * `nverts` records of SL_BGOP_RECORD_FLOATS floats; `nverts` is the draw's
 * index count and the vertices are the ones its index range names, in that
 * order. Accumulates into `t` and prints at most SL_BGOP_SNAP_REPORT_CAP raw-bit
 * lines across the whole run.
 *
 * `sink`, WHEN NOT NULL, RETAINS EVERYTHING. One row per vertex carrying the
 * measured bits, the reference bits and the per-component class - for ALL 21
 * components, the NO_ORACLE and ADVISORY ones included. Retention happens
 * before the class decides anything, which is the point: the components without
 * an oracle are the ones a later question is most likely to be about, and they
 * were previously counted and dropped. The console listing stays capped; the
 * artifact is where the measurement lives. A sink write failure is recorded in
 * `t->sink_failed` and does not alter the comparison, because a comparison that
 * changed with the artifact's fortunes would be a different measurement. */
void sl_bgop_snap_compare_draw(const struct sl_bgsnap *s, unsigned draw,
                               const float *records, unsigned nverts,
                               int report, struct sl_bgop_snap_tally *t,
                               struct sl_bgor *sink);

/* The compact human summary: the totals, the assumptions they rest on and the
 * limits they do not close. A few lines, whatever the vertex count. */
void sl_bgop_snap_summary(const struct sl_bgsnap *s,
                          const struct sl_bgop_snap_tally *t);

/* ---- THE FOG CONTRACT, INTERPRETED ONCE -------------------------------
 *
 * SL_BGSNAP_S_FOG_ARRAY is PER DRAW and carries THREE states, because the
 * background pass toggles the fog-coordinate array per material over a shared
 * vertex pool: one walk can issue a fogged draw and an unfogged draw from the
 * same pool, so no walk-level answer is true for both.
 *
 * ONE FUNCTION, THREE CONSUMERS. The classifier, the reference builder and the
 * GL replay all need this decision, and three copies of the branch is how a
 * state comes to be handled correctly in one place and wrongly in another. The
 * plan below answers all three questions at once - what to bind, what to set,
 * and what the expectation is - so the offline controls exercise the same logic
 * the driver path would run.
 *
 *   state 1, FOG_ARRAY_ON       the draw reads the per-vertex pool. Bind the
 *                               array; the oracle is the recorded pool value.
 *   state 0, FOG_OFF_ZERO       the array is disabled and the current
 *                               coordinate is a KNOWN 0.0f, set by the pass in
 *                               the same branch that disables the array. The
 *                               oracle is EXACTLY 0.0f and is NOT the pool
 *                               value, which this draw does not read. This is a
 *                               real oracle and discarding it loses evidence.
 *   state 2, FOG_OFF_UNKNOWN    the array is disabled and the current
 *                               coordinate was never established, so the
 *                               effective value is unknown. NO ORACLE, and the
 *                               replay binds nothing and sets nothing: the draw
 *                               cannot be faithfully reproduced, so its fog is
 *                               recorded as measured-with-no-oracle instead of
 *                               being compared against a fabricated input.
 *   mask bit clear, or any
 *   other value                 NO ORACLE, and NEVER treated as array-on. An
 *                               unrecorded word is absent evidence, and an
 *                               unrecognised value is not guessed at.
 *
 * SL_BGSNAP_DF_FOGLESS DOES NOT ENTER THIS DECISION. It is a uniform the depth
 * path reads; BGG_VS forwards gl_FogFragCoord = gl_FogCoord whatever it says.
 * The state word is the only authority on where that coordinate comes from, and
 * treating the flag as a fog-off signal would answer from the wrong field. */
enum {
    SL_BGOP_FOG_POOL = 0,     /* bind the array; oracle is the pool value    */
    SL_BGOP_FOG_ZERO,         /* current-fog-0; oracle is exactly 0.0f       */
    SL_BGOP_FOG_NONE          /* no oracle, and nothing may be bound or set  */
};

struct sl_bgop_fog_plan {
    int      kind;            /* SL_BGOP_FOG_*                               */
    int      bind_array;      /* the replay binds the fog-coordinate array    */
    int      set_current;     /* the replay sets the current coordinate       */
    float    current;         /* the value to set when set_current            */
    int      has_oracle;      /* an expectation is defined                    */
    unsigned expect_bits;     /* the expected gl_FogFragCoord pattern         */
    const char *why;          /* the state, named, for a report               */
};

/* `vx` may be NULL when only the per-draw replay decision is wanted; the
 * expectation is then undefined and `has_oracle` is 0, because the pool value
 * is per vertex. */
void sl_bgop_fog_plan(const struct sl_bgsnap_draw *d,
                      const struct sl_bgsnap_vertex *vx,
                      struct sl_bgop_fog_plan *p);

/* ---- THE RETENTION GATE -----------------------------------------------
 *
 * THE DEFECT THIS CLOSES, and it was a fail-open. The runner reported a sink
 * that would not open, counted row failures into `sink_failed`, counted capped
 * rows into `sink_dropped`, and then returned its status from the ARITHMETIC
 * alone. A run whose components all matched could therefore return 0 - qualify
 * - while its raw outputs were missing from disk entirely. The expensive part
 * of the exercise is the capture; a run that cannot keep it has not measured
 * anything that can be looked at again, and must not pass.
 *
 * RETENTION IS MANDATORY. This returns 1 only when ALL of the following hold:
 *   - no sink write failure at all (`sink_failed` zero)
 *   - no row dropped by the cap (`sink_dropped` zero)
 *   - a row retained for every vertex compared (`retained` == `vertices`)
 *   - the vertex count the caller expected, so a short run is caught even when
 *     every row it did produce was retained
 *   - the final close succeeded, which is `closed_ok`
 * Otherwise it returns 0 and writes the reason.
 *
 * IT TOUCHES NO COMPARISON COUNT. `compared`, `differing`, `advisory` and
 * `no_oracle` are the arithmetic's own and are reported beside this, never
 * folded into it. A sink failure is not turned into a fabricated component
 * mismatch to force a non-zero status: the two are separate facts and a reader
 * is told which one refused the run. Equally, matching arithmetic does not
 * excuse a retention failure.
 *
 * This is the predicate the runner applies and the predicate the offline
 * controls drive, not two copies of one rule - the same arrangement
 * sl_bgop_counts_ready already uses, and for the same reason. */
int sl_bgop_retention_ready(const struct sl_bgop_snap_tally *t,
                            unsigned expect_vertices, int closed_ok,
                            char *why, int whycap);

/* ---- THE FINALIZATION, WHICH DECIDES BEFORE IT WRITES ------------------
 *
 * THE DEFECT THIS CLOSES. The seal was chosen and then the status was judged,
 * so a close that failed left the status untouched, and the COMPLETE/failed
 * choice was made at the call site rather than from the run's actual numbers.
 * Worse for the controls: a test that sealed a failed artifact by hand never
 * reached the real branch, which is exactly how the fault survived being
 * "tested".
 *
 * So the decision and the write live here, in that order:
 *   1. the ACTUAL numbers are inspected first - sink write errors, cap drops,
 *      retained against compared vertices, compared against expected vertices,
 *      and captured against expected draws
 *   2. SL_BGOR_F_COMPLETE is requested ONLY when every row is present. It is a
 *      statement about the rows and is independent of whether the run
 *      qualified, so a run with whole retention and disagreeing arithmetic is
 *      sealed COMPLETE and FAILED together - the records are what a
 *      disagreement is diagnosed from
 *   3. the artifact is sealed accordingly, and the seal's own success is then
 *      part of the overall status
 *
 * Returns 0 only when the records are whole, the seal succeeded AND
 * `capture_ok` was non-zero. `why` names whichever of those failed.
 *
 * NO ARITHMETIC COUNT IS TOUCHED. `compared`, `differing`, `advisory` and
 * `no_oracle` are read for the report and never written, and no component
 * mismatch is invented to force a non-zero status: a retention failure is
 * reported as a retention failure. Both the driver runner and the offline fault
 * controls call this, so there is one branch and not two. */
int sl_bgop_finalize_records(struct sl_bgor *sink, int sink_open,
                             const struct sl_bgop_snap_tally *t,
                             unsigned expect_vertices, unsigned expect_draws,
                             int capture_ok, char *why, int whycap);

/* ---- the driver side of the snapshot run -------------------------------
 *
 * ONE BULK TRANSFORM-FEEDBACK CAPTURE PER DRAW, not one per vertex. A draw's
 * whole index range is transformed in a single GL_POINTS draw with the
 * rasterizer discarded and read back once, which is one round trip per draw
 * instead of one per vertex - the difference between seconds and hours for a
 * real frame. The index range is dereferenced on the CPU into client arrays, so
 * each index position yields exactly one captured record and a repeated index
 * yields its record twice, as an indexed draw would.
 *
 * THE SAME REFUSALS AS THE SYNTHETIC RUN, PER DRAW: the primitives-written
 * query must equal the draw's vertex count, the read-back must have happened
 * (the buffer's first float must no longer hold the sentinel), the floats past
 * the declared end of the last record must still hold it, GL errors are drained
 * before, during and after and ANY of them is fatal, and sl_bgop_counts_ready -
 * the caller's own predicate, not a second copy of it - must open before a
 * single component is compared. A draw that fails any of these refuses the
 * WHOLE run and compares NOTHING.
 *
 * A draw whose index count exceeds SL_BGOP_SNAP_MAX_DRAW_VERTS is REFUSED with
 * its count named. It is not captured in pieces: a partial capture of a draw is
 * not a measurement of that draw.
 *
 * Returns 0 only when every draw was captured cleanly and nothing with an
 * oracle differed. `why` holds the refusal otherwise. */
int sl_bgop_gl_run_snapshot(const struct sl_bgsnap *s,
                            struct sl_bgop_snap_tally *t, int report,
                            char *why, int whycap);

#endif /* SL_BGGPU_OUT_PROBE_H */
