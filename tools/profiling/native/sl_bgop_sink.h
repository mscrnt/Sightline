/* THE OUTPUT RECORD SINK - every raw captured record, kept.
 *
 * WHAT GAP THIS CLOSES. The bulk per-draw capture compares one draw's records
 * and then reuses the capture buffer for the next draw, so the raw floats exist
 * only between the read-back and the next draw's prefill. The comparison kept
 * counts, printed at most a few dozen difference lines, and dropped every
 * component that had no oracle before recording anything about it. A driver run
 * over a real frame therefore measured tens of thousands of records and
 * retained almost none of them: the one expensive part of the exercise was
 * discarded, and a follow-up question about any particular vertex could only be
 * answered by running the whole thing again.
 *
 * This file is where the measurements go instead. EVERY record of EVERY draw,
 * with its measured bits, its reference bits and its per-component class -
 * including the components with NO ORACLE and the ADVISORY ones, which are
 * exactly the components a later question is most likely to be about.
 *
 * THE CONSOLE IS NOT THE ARTIFACT. Stdout keeps the compact summary and its
 * capped difference listing, because a per-vertex listing of a real frame is
 * megabytes that tell a reader less than the totals do. The full measurement
 * goes here, where it can be re-read offline without a driver.
 *
 * STREAMED, SO MEMORY IS BOUNDED BY ONE ROW. Rows are written as they are
 * produced. Nothing accumulates in memory, so the peak does not depend on the
 * frame's size, and a run that dies mid-way leaves the rows it had already
 * written rather than nothing at all.
 *
 * BOUNDED ON DISK TOO, AND HONEST ABOUT IT. SL_BGOR_MAX_RECORDS caps the row
 * count. Past the cap rows are NOT written and the artifact records how many
 * were dropped and sets SL_BGOR_F_PARTIAL. A partial artifact is a partial
 * artifact: the flag is in the header, a reader reports it as partial, and
 * nothing presents it as a complete measurement. The alternative - silently
 * keeping the first N and reading them as the whole - is the failure this flag
 * exists to prevent.
 *
 * CREATE ONCE. Opening refuses a path that already exists, so an earlier run's
 * measurements cannot be overwritten by a later one. The same rule the snapshot
 * format states for its own writer, for the same reason.
 *
 * COMPLETE IS WRITTEN LAST. The header is written once at open with no status
 * bit set and rewritten at close; SL_BGOR_F_COMPLETE appears only after every
 * row is written and flushed. A reader refuses an artifact without it rather
 * than reading a short one as a whole one.
 *
 * NOTHING ROM-DERIVED REACHES GIT. The default location is the process TEMP
 * directory, which is outside the working tree entirely - a stronger guarantee
 * than an ignore rule, since there is no repository path to ignore. A snapshot's
 * records are derived from the player's own extracted asset and inherit that
 * data's boundary exactly as the snapshot itself does.
 */
#ifndef SL_BGOP_SINK_H
#define SL_BGOP_SINK_H

#include "sl_bgsnap_read.h"

#include <stdio.h>

/* "SNPO" little-endian. */
#define SL_BGOR_MAGIC          0x4F504E53u
#define SL_BGOR_VERSION        1u

/* The record float count is restated here rather than taken from
 * sl_bggpu_out_probe.h, which declares the comparison that takes a sink and
 * would close an include cycle. sl_bgop_sink.c includes both and asserts the
 * two are equal at compile time, so they cannot drift. */
#define SL_BGOR_RECORD_FLOATS  21
#define SL_BGOR_NFIELDS        6

/* THE CAP IS THE FORMAT'S OWN INDEX MAXIMUM, and it is derived rather than
 * chosen so the two cannot disagree.
 *
 * A snapshot's total row count is the sum of its draws' index counts, which the
 * snapshot reader has already bounded by SL_BGSNAP_MAX_INDICES. Setting the cap
 * to that number means EVERY snapshot the reader accepts can be retained whole,
 * so no legal frame can be captured and then have a suffix dropped.
 *
 * An earlier bound of 262144 rows was below both this maximum and the 271080
 * indices one real background item carries, so the frame the exercise is aimed
 * at could not have been retained whole under it - the run would have captured
 * everything and silently kept a prefix.
 *
 * THE BYTE ACCOUNTING IS FINITE AND CHECKED. 4194304 rows at 200 bytes is
 * 838860800, and with the 256-byte header 838861056, which is inside 32 bits
 * (4294967295) - so a full artifact's extent is representable in the header's
 * own u32 field. That is arithmetic, not a hope: the preflight below computes
 * this snapshot's exact row count and byte extent with explicit overflow
 * guards and refuses before anything is created. */
#define SL_BGOR_MAX_RECORDS    SL_BGSNAP_MAX_INDICES

/* FOUR INDEPENDENT FACTS, not a status ladder - keeping them apart is the
 * whole point of having four bits, and two of them were previously conflated.
 *
 *   SEALED    the final counters were written. Until this is set the row count,
 *             the drop count and the extent in the header are the provisional
 *             zeros written at open, so a reader MUST refuse an unsealed
 *             artifact: its own header does not describe it.
 *   COMPLETE  every row that was offered to the sink was written. A statement
 *             about the ROWS ALL BEING THERE and about nothing else. It is
 *             never set when the cap dropped a row, so COMPLETE cannot be
 *             stamped over a missing suffix.
 *   PARTIAL   the cap refused rows. Mutually exclusive with COMPLETE by
 *             construction.
 *   FAILED    the run was refused, for any reason, and `failure` names it.
 *             INDEPENDENT of the rest: a run whose arithmetic disagreed can
 *             still have retained every record, and that artifact is SEALED,
 *             COMPLETE and FAILED at once - which is the one most worth
 *             reading, since withholding COMPLETE would make a reader refuse
 *             the very rows the disagreement has to be diagnosed from.
 *
 * WHY SEALED IS SEPARATE FROM COMPLETE. Requiring COMPLETE in order to read at
 * all made a capped or failed artifact unreadable, which destroyed the evidence
 * it was written to preserve: the rows were on disk and honestly counted, and
 * nothing could open them. SEALED answers "does this header describe this
 * file", COMPLETE answers "are all the rows here". A reader needs the first to
 * read anything and reports the second as a fact.
 *
 * So COMPLETE never means "the run qualified". The run's own status is the
 * runner's non-zero return and the FAILED bit here. */
#define SL_BGOR_F_COMPLETE     0x00000001u
#define SL_BGOR_F_PARTIAL      0x00000002u
#define SL_BGOR_F_FAILED       0x00000004u
#define SL_BGOR_F_SEALED       0x00000008u

#define SL_BGOR_HEADER_BYTES   256u
#define SL_BGOR_RECORD_BYTES   200u
#define SL_BGOR_FAILURE_BYTES  32u

/* ---- the header layout, byte offsets ----------------------------------- */
#define SL_BGOR_H_MAGIC         0u
#define SL_BGOR_H_VERSION       4u
#define SL_BGOR_H_HEADER_BYTES  8u
#define SL_BGOR_H_FLAGS         12u
#define SL_BGOR_H_RECORD_BYTES  16u
#define SL_BGOR_H_RECORD_FLOATS 20u
#define SL_BGOR_H_NFIELDS       24u
#define SL_BGOR_H_RECORDS       28u   /* rows actually written                */
#define SL_BGOR_H_DROPPED       32u   /* rows the cap refused                 */
#define SL_BGOR_H_DRAWS         36u   /* draws represented by those rows      */
#define SL_BGOR_H_FILE_BYTES    40u
#define SL_BGOR_H_MAX_RECORDS   44u   /* the cap in force, so a reader knows  */
#define SL_BGOR_H_FRAME         48u   /* the snapshot's own identity, carried */
#define SL_BGOR_H_READ          52u
#define SL_BGOR_H_CLOUD         56u
#define SL_BGOR_H_RECORD_OFF    60u   /* where the rows begin                 */
#define SL_BGOR_H_EXE_SHA       64u   /* 32 bytes                             */
#define SL_BGOR_H_SRC_SHA       96u   /* 32 bytes                             */
#define SL_BGOR_H_FAILURE       128u  /* 32 bytes, NUL-padded ASCII           */
#define SL_BGOR_H_FIELDS        160u  /* NFIELDS x (comps u32, offset u32)    */
#define SL_BGOR_H_RESERVED      208u  /* 48 bytes, written as zero            */
/* 208 + 48 = 256 = SL_BGOR_HEADER_BYTES.
 *
 * THE RECORD LAYOUT IS IN THE FILE. The component count and float offset of
 * each of the six fields are stored, so a reader never assumes the layout it
 * was compiled against: an artifact written by a build whose record layout
 * differed would otherwise have every field after the difference read from the
 * wrong offset, which is a wrong answer rather than a refusal. */

/* ---- the row layout, byte offsets within the row ----------------------- */
#define SL_BGOR_R_DRAW       0u    /* u32, which draw this row belongs to   */
#define SL_BGOR_R_VERT       4u    /* u32, which snapshot vertex it is      */
#define SL_BGOR_R_MEASURED   8u    /* 21 x u32 raw patterns, as captured    */
#define SL_BGOR_R_REFERENCE  92u   /* 21 x u32 raw patterns, the expectation */
#define SL_BGOR_R_CLASS      176u  /* 21 bytes, SL_BGOP_EXACT/ADVISORY/NO_ORACLE */
#define SL_BGOR_R_PAD        197u  /* 3 bytes, written as zero              */
/* 197 + 3 = 200 = SL_BGOR_RECORD_BYTES. */

/* ---- the writer -------------------------------------------------------- */

/* Opaque to everything but sl_bgop_sink.c, so no caller can write a row the
 * counters do not know about. */
struct sl_bgor {
    FILE    *f;
    unsigned records, dropped, draws, flags;
    unsigned last_draw;
    int      have_draw;
    char     failure[SL_BGOR_FAILURE_BYTES + 1u];
};

/* Creates `path`, which must NOT already exist, and writes the provisional
 * header. The snapshot supplies the identity carried into the header so the
 * rows can be tied back to the frame they came from. 0 on success; non-zero
 * with the rule named in `why` - EXISTS, OPEN or WRITE. */
int sl_bgor_open(struct sl_bgor *k, const char *path, const struct sl_bgsnap *s,
                 char *why, int whycap);

/* One row. Called once per vertex, with that vertex's measured and reference
 * patterns and its per-component classes.
 *
 * Returns 0 when the call was handled and non-zero on a WRITE ERROR. Whether
 * the row reached the disk is `*written`, which is a SEPARATE answer: past the
 * cap the row is not written, `dropped` counts it and SL_BGOR_F_PARTIAL is set,
 * and that is a success - the artifact is bounded and says so. Collapsing the
 * two would make a dropped row indistinguishable from a retained one at the
 * call site, which is how a capped artifact comes to be reported as holding
 * every record it was offered. */
int sl_bgor_row(struct sl_bgor *k, unsigned draw, unsigned vert,
                const unsigned *measured, const unsigned *reference,
                const unsigned char *cls, int *written, char *why, int whycap);

/* Records that the run itself failed, with a short identifier. The artifact
 * keeps whatever rows were already written and is marked FAILED, because the
 * rows up to a failure are the measurements that most need looking at. */
void sl_bgor_failed(struct sl_bgor *k, const char *reason);

/* Rewrites the header with the final counts and sets SL_BGOR_F_COMPLETE, then
 * closes. COMPLETE is the last thing written, so it cannot appear over
 * provisional counters. Refuses to set it when the cap dropped rows: a capped
 * artifact is missing rows and must not claim otherwise. 0 on success. */
int sl_bgor_close(struct sl_bgor *k, char *why, int whycap);

/* Seals the artifact as FAILED WITHOUT setting COMPLETE, then closes. The rows
 * already written and the final counters are kept; the file stays on disk.
 *
 * THE FILE IS NOT REMOVED, and that is deliberate. An artifact left by a
 * failed setup is the evidence that the attempt happened and how far it got,
 * and deleting it to free the filename for a retry destroys exactly that. The
 * next attempt at the same path is refused by EXISTS instead, which is the
 * create-once rule doing its job rather than an obstacle to work around. */
int sl_bgor_seal_failed(struct sl_bgor *k, const char *reason,
                        char *why, int whycap);

/* ---- the reader, which is how the controls prove the writer ------------ */

struct sl_bgor_info {
    unsigned version, flags;
    unsigned record_bytes, record_floats, nfields;
    unsigned records, dropped, draws, file_bytes, max_records;
    unsigned frame, read, cloud, record_off;
    unsigned char exe_sha[32], src_sha[32];
    char failure[SL_BGOR_FAILURE_BYTES + 1u];
    unsigned comps[SL_BGOR_NFIELDS], offset[SL_BGOR_NFIELDS];
};

struct sl_bgor_row {
    unsigned      draw, vert;
    unsigned      measured[SL_BGOR_RECORD_FLOATS];
    unsigned      reference[SL_BGOR_RECORD_FLOATS];
    unsigned char cls[SL_BGOR_RECORD_FLOATS];
};

/* Reads the artifact into `info` and up to `cap` rows into `rows`. The same
 * discipline the snapshot reader applies, and for the same reason - the file is
 * input: magic, version and the three sizes first, then SL_BGOR_F_COMPLETE
 * required, then the row count against SL_BGOR_MAX_RECORDS BEFORE it is
 * multiplied, then the declared extent against the file's actual length.
 *
 * REFUSES, NEVER CLAMPS. An artifact holding more rows than `cap` is refused
 * rather than read in part, so a caller cannot mistake the first `cap` rows for
 * all of them. `why` names the rule: MAGIC, VERSION, HEADER_BYTES,
 * RECORD_BYTES, RECORD_FLOATS, NFIELDS, LAYOUT, INCOMPLETE, COUNT_MAX,
 * EXTENT_OVERFLOW, TRUNCATED, CAPACITY, OPEN or SHORT_HEADER. */
int sl_bgor_read(const char *path, struct sl_bgor_info *info,
                 struct sl_bgor_row *rows, unsigned cap, char *why, int whycap);

/* ---- THE PREFLIGHT, which runs before anything is created -------------
 *
 * WHAT IT IS FOR. Retention is mandatory: a capture whose records cannot be
 * kept is not a measurement worth taking, so the decision has to be made BEFORE
 * a GL context exists. Everything this checks is knowable from the snapshot and
 * the filesystem, with no driver and no capture:
 *
 *   - the summed index count over every draw, which is the exact number of rows
 *     the run will produce, accumulated with an explicit overflow guard
 *   - that row count against the cap in force
 *   - the byte extent those rows need, header included, with explicit overflow
 *     guards, so the artifact's own u32 extent field cannot wrap
 *   - that the path does not already exist, so create-once is enforced before
 *     a context is built rather than after
 *
 * `out_rows` and `out_bytes` receive the exact figures either way, so a refusal
 * can state what the frame needed and what was available instead of only that
 * it was too big.
 *
 * Returns 0 when the run may proceed. Non-zero names the rule: ROWS_OVERFLOW,
 * CAPACITY, BYTES_OVERFLOW, EXISTS or OPEN. */
int sl_bgor_preflight(const struct sl_bgsnap *s, const char *path,
                      unsigned *out_rows, unsigned *out_bytes,
                      char *why, int whycap);

/* The cap currently in force, which a refusal quotes. */
unsigned sl_bgor_cap(void);

/* ---- the test seams, for the offline controls -------------------------
 *
 * The retention gate has to be provable without a driver and without filling a
 * disk, so the two failures that only hardware or a full filesystem would
 * otherwise produce are injectable. Each is one-shot and self-clearing, so a
 * control cannot leak an injected failure into a later one.
 *
 * These exist because the alternative is an ungated path: a row write that
 * fails and a close that fails are exactly the cases the fail-open defect hid,
 * and a control that could not reach them would be asserting the gate rather
 * than exercising it. */
void sl_bgor_test_fail_row_at(unsigned row_index);   /* 0xFFFFFFFF to disable */
void sl_bgor_test_fail_close(int on);

/* Lowers the row cap for the offline controls, which must drive the PARTIAL
 * path and the over-capacity preflight without writing hundreds of megabytes. It only ever lowers: a
 * request above SL_BGOR_MAX_RECORDS is ignored, so no caller can raise the
 * bound this way. The cap in force is written into the artifact's own header
 * (SL_BGOR_H_MAX_RECORDS), so a partial artifact states which cap produced it.
 * Returns the cap now in force. */
unsigned sl_bgor_set_cap(unsigned cap);

/* The default artifact path for a run, built into `out`. SL_BGOP_SINK selects
 * one explicitly; otherwise it is placed in the process TEMP directory, which
 * is outside the working tree, so no ROM-derived measurement lands on a
 * repository path by default. */
void sl_bgor_default_path(char *out, int cap, const char *tag);

#endif /* SL_BGOP_SINK_H */
