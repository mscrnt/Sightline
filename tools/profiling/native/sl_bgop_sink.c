/* THE OUTPUT RECORD SINK. Read sl_bgop_sink.h first: it holds the contract, the
 * streaming and create-once rules, and why a partial artifact must say so.
 *
 * THE WRITER STREAMS AND THE READER REFUSES. Those are the two halves, and they
 * are deliberately asymmetric. The writer's input comes from this process, so
 * its job is to lose nothing and to be honest about what the cap dropped. The
 * reader's input is a file, so its job is the snapshot reader's: validate every
 * size and count before multiplying, require the completion flag, and refuse
 * rather than read what it can.
 */
#include "sl_bgop_sink.h"

#include "sl_bggpu_out_probe.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* The two record float counts cannot drift: sl_bgop_sink.h restates it to avoid
 * an include cycle, and this is the tie. A mismatch makes the array size
 * negative and the build stops here rather than writing rows whose length
 * disagrees with the layout in their own header. */
typedef char sl_bgor_record_floats_agree[
    (SL_BGOR_RECORD_FLOATS == SL_BGOP_RECORD_FLOATS) ? 1 : -1];
typedef char sl_bgor_nfields_agree[
    (SL_BGOR_NFIELDS == SL_BGOP_NFIELDS) ? 1 : -1];
/* And the row layout must account for every byte it declares. */
typedef char sl_bgor_row_fits[
    (SL_BGOR_R_CLASS + SL_BGOR_RECORD_FLOATS + 3u == SL_BGOR_RECORD_BYTES) ? 1 : -1];

/* ---- the refusal path --------------------------------------------------- */

static int sink_refuse(char *why, int whycap, const char *fmt, ...)
{
    va_list ap;
    if (why != NULL && whycap > 0) {
        va_start(ap, fmt);
        vsnprintf(why, (size_t) whycap - 1u, fmt, ap);
        va_end(ap);
        why[whycap - 1] = '\0';
    }
    return 1;
}

/* The row cap in force. Lowered by the offline controls so the PARTIAL path can
 * be driven without writing 52 MB on every build; never raised above the
 * compiled bound, so the artifact's memory and disk ceiling cannot be lifted by
 * a caller. */
static unsigned g_cap = SL_BGOR_MAX_RECORDS;

unsigned sl_bgor_set_cap(unsigned cap)
{
    if (cap <= SL_BGOR_MAX_RECORDS) g_cap = cap;
    return g_cap;
}

unsigned sl_bgor_cap(void) { return g_cap; }

/* The two injected failures. One-shot and self-clearing: each fires once and
 * disarms, so a control cannot leak its injection into the next one. */
static unsigned g_fail_row = 0xFFFFFFFFu;
static int      g_fail_close;

void sl_bgor_test_fail_row_at(unsigned row_index) { g_fail_row = row_index; }
void sl_bgor_test_fail_close(int on) { g_fail_close = on ? 1 : 0; }

static void put_le32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char) (v & 0xFFu);
    p[1] = (unsigned char) ((v >> 8) & 0xFFu);
    p[2] = (unsigned char) ((v >> 16) & 0xFFu);
    p[3] = (unsigned char) ((v >> 24) & 0xFFu);
}

/* ---- the header, written at open and rewritten at close ---------------- */

/* One function for both writes, so the two cannot describe different layouts.
 * At open the status word is zero and the counters are zero; at close they are
 * final and SL_BGOR_F_COMPLETE is set. */
static int write_header(struct sl_bgor *k, const struct sl_bgsnap *s)
{
    unsigned char head[SL_BGOR_HEADER_BYTES];
    const struct sl_bgop_field *fields = sl_bgop_fields();
    unsigned extent, i;

    memset(head, 0, sizeof head);
    put_le32(head + SL_BGOR_H_MAGIC,         SL_BGOR_MAGIC);
    put_le32(head + SL_BGOR_H_VERSION,       SL_BGOR_VERSION);
    put_le32(head + SL_BGOR_H_HEADER_BYTES,  SL_BGOR_HEADER_BYTES);
    put_le32(head + SL_BGOR_H_FLAGS,         k->flags);
    put_le32(head + SL_BGOR_H_RECORD_BYTES,  SL_BGOR_RECORD_BYTES);
    put_le32(head + SL_BGOR_H_RECORD_FLOATS, SL_BGOR_RECORD_FLOATS);
    put_le32(head + SL_BGOR_H_NFIELDS,       SL_BGOR_NFIELDS);
    put_le32(head + SL_BGOR_H_RECORDS,       k->records);
    put_le32(head + SL_BGOR_H_DROPPED,       k->dropped);
    put_le32(head + SL_BGOR_H_DRAWS,         k->draws);
    /* The cap bounds this product, so it cannot wrap: SL_BGOR_MAX_RECORDS times
     * SL_BGOR_RECORD_BYTES is about 52 MB. */
    extent = SL_BGOR_HEADER_BYTES + k->records * SL_BGOR_RECORD_BYTES;
    put_le32(head + SL_BGOR_H_FILE_BYTES,    extent);
    put_le32(head + SL_BGOR_H_MAX_RECORDS,   g_cap);
    put_le32(head + SL_BGOR_H_RECORD_OFF,    SL_BGOR_HEADER_BYTES);
    if (s != NULL) {
        put_le32(head + SL_BGOR_H_FRAME, s->frame);
        put_le32(head + SL_BGOR_H_READ,  s->read);
        put_le32(head + SL_BGOR_H_CLOUD, s->cloud);
        memcpy(head + SL_BGOR_H_EXE_SHA, s->exe_sha, 32u);
        memcpy(head + SL_BGOR_H_SRC_SHA, s->src_sha, 32u);
    }
    memcpy(head + SL_BGOR_H_FAILURE, k->failure, SL_BGOR_FAILURE_BYTES);
    for (i = 0u; i < SL_BGOR_NFIELDS; i++) {
        put_le32(head + SL_BGOR_H_FIELDS + 8u * i,      (unsigned) fields[i].comps);
        put_le32(head + SL_BGOR_H_FIELDS + 8u * i + 4u, (unsigned) fields[i].offset);
    }

    if (fseek(k->f, 0L, SEEK_SET) != 0) return 1;
    if (fwrite(head, 1u, sizeof head, k->f) != sizeof head) return 1;
    return 0;
}

int sl_bgor_open(struct sl_bgor *k, const char *path, const struct sl_bgsnap *s,
                 char *why, int whycap)
{
    FILE *probe;

    if (why != NULL && whycap > 0) why[0] = '\0';
    if (k == NULL) return sink_refuse(why, whycap, "OPEN: no sink was supplied");
    memset(k, 0, sizeof *k);
    if (path == NULL || *path == '\0')
        return sink_refuse(why, whycap, "OPEN: no artifact path was supplied");

    /* CREATE ONCE. An existing file is refused rather than truncated, so a
     * measurement already on disk cannot be destroyed by a later run. */
    probe = fopen(path, "rb");
    if (probe != NULL) {
        fclose(probe);
        return sink_refuse(why, whycap,
                           "EXISTS: %s is already there; the sink creates its "
                           "artifact once and will not overwrite an earlier "
                           "run's measurements", path);
    }

    k->f = fopen(path, "wb");
    if (k->f == NULL)
        return sink_refuse(why, whycap, "OPEN: %s could not be created", path);
    /* The provisional header: no status bit, so a file left behind by a run
     * that died here is refused by the reader rather than read as empty. */
    if (write_header(k, s) != 0) {
        fclose(k->f);
        k->f = NULL;
        /* The file is NOT removed. It carries no completion flag, so a reader
         * refuses it, and leaving it preserves the evidence that this path was
         * attempted - which removing it to free the filename would destroy. */
        return sink_refuse(why, whycap,
                           "WRITE: the provisional header could not be written "
                           "to %s", path);
    }
    return 0;
}

int sl_bgor_row(struct sl_bgor *k, unsigned draw, unsigned vert,
                const unsigned *measured, const unsigned *reference,
                const unsigned char *cls, int *written, char *why, int whycap)
{
    unsigned char row[SL_BGOR_RECORD_BYTES];
    unsigned i;

    if (written != NULL) *written = 0;
    if (k == NULL || k->f == NULL)
        return sink_refuse(why, whycap, "WRITE: the sink is not open");
    if (measured == NULL || reference == NULL || cls == NULL)
        return sink_refuse(why, whycap, "WRITE: a row was supplied incomplete");

    /* THE CAP, AND IT IS RECORDED RATHER THAN HIDDEN. Past it the row is not
     * written and the artifact will say how many were dropped and that it is
     * partial. Keeping the first N and letting a reader take them for all of
     * them is the failure SL_BGOR_F_PARTIAL exists to prevent.
     *
     * This is a SUCCESS with `*written` left 0, not an error: the bound did
     * what it is for. The caller learns which happened from `*written`, so a
     * dropped row cannot be counted as a retained one. */
    if (k->records >= g_cap) {
        k->dropped++;
        k->flags |= SL_BGOR_F_PARTIAL;
        return 0;
    }

    memset(row, 0, sizeof row);
    put_le32(row + SL_BGOR_R_DRAW, draw);
    put_le32(row + SL_BGOR_R_VERT, vert);
    for (i = 0u; i < SL_BGOR_RECORD_FLOATS; i++) {
        put_le32(row + SL_BGOR_R_MEASURED  + 4u * i, measured[i]);
        put_le32(row + SL_BGOR_R_REFERENCE + 4u * i, reference[i]);
        row[SL_BGOR_R_CLASS + i] = cls[i];
    }
    /* The injected row failure, which stands in for a full filesystem. It fires
     * once and disarms, and takes the same path a real short write takes. */
    if (k->records == g_fail_row) {
        g_fail_row = 0xFFFFFFFFu;
        return sink_refuse(why, whycap,
                           "WRITE: row %u (draw %u, vertex %u) could not be "
                           "written", k->records, draw, vert);
    }
    if (fwrite(row, 1u, sizeof row, k->f) != sizeof row)
        return sink_refuse(why, whycap,
                           "WRITE: row %u (draw %u, vertex %u) could not be "
                           "written", k->records, draw, vert);
    k->records++;
    if (!k->have_draw || k->last_draw != draw) {
        k->draws++;
        k->last_draw = draw;
        k->have_draw = 1;
    }
    if (written != NULL) *written = 1;
    return 0;
}

void sl_bgor_failed(struct sl_bgor *k, const char *reason)
{
    if (k == NULL) return;
    k->flags |= SL_BGOR_F_FAILED;
    if (reason != NULL) {
        memset(k->failure, 0, sizeof k->failure);
        strncpy(k->failure, reason, SL_BGOR_FAILURE_BYTES - 1u);
        k->failure[SL_BGOR_FAILURE_BYTES - 1u] = '\0';
    }
}

/* Patches one 32-bit header word in place. The close path updates only the
 * words that change - the counters and the status - rather than rewriting the
 * whole header: the frame identity and the shader hashes were written at open
 * from the snapshot, and a full rewrite at close has no snapshot to write them
 * from and would silently zero them. */
static int patch_word(struct sl_bgor *k, unsigned off, unsigned v)
{
    unsigned char w[4];
    put_le32(w, v);
    if (fseek(k->f, (long) off, SEEK_SET) != 0) return 1;
    if (fwrite(w, 1u, 4u, k->f) != 4u) return 1;
    return 0;
}

/* THE ONE SEAL PATH, used by both close and seal_failed, so the two cannot
 * write different headers. `complete` asks for SL_BGOR_F_COMPLETE; it is
 * granted only when no row was dropped, because a capped artifact is missing
 * rows and COMPLETE is a statement about the rows all being there. */
static int seal(struct sl_bgor *k, int complete, char *why, int whycap)
{
    unsigned extent;
    int bad = 0;

    if (why != NULL && whycap > 0) why[0] = '\0';
    if (k == NULL || k->f == NULL)
        return sink_refuse(why, whycap, "WRITE: the sink is not open");

    /* Every row is on its way to the device before the completion flag is
     * written, so COMPLETE cannot appear in a file whose rows did not. */
    if (fflush(k->f) != 0) {
        fclose(k->f);
        k->f = NULL;
        return sink_refuse(why, whycap,
                           "WRITE: the rows could not be flushed, so "
                           "SL_BGOR_F_COMPLETE was not written");
    }

    /* The cap bounds this product, so it cannot wrap. */
    extent = SL_BGOR_HEADER_BYTES + k->records * SL_BGOR_RECORD_BYTES;
    bad |= patch_word(k, SL_BGOR_H_RECORDS,    k->records);
    bad |= patch_word(k, SL_BGOR_H_DROPPED,    k->dropped);
    bad |= patch_word(k, SL_BGOR_H_DRAWS,      k->draws);
    bad |= patch_word(k, SL_BGOR_H_FILE_BYTES, extent);
    if (k->failure[0] != '\0') {
        if (fseek(k->f, (long) SL_BGOR_H_FAILURE, SEEK_SET) != 0 ||
            fwrite(k->failure, 1u, SL_BGOR_FAILURE_BYTES, k->f)
                != SL_BGOR_FAILURE_BYTES) bad = 1;
    }
    /* THE INJECTED CLOSE FAILURE, which stands in for a device error on the
     * final header. It takes the same path a real one takes, so the gate above
     * it is exercised rather than asserted. */
    if (g_fail_close) { g_fail_close = 0; bad = 1; }

    /* THE STATUS WORD LAST, and only when everything before it went down. A
     * status written before the counters would let a reader see a sealed
     * artifact whose row count was still the provisional zero.
     *
     * SEALED always: the counters above are now final, which is what makes the
     * file readable at all. COMPLETE only when it was asked for AND no row was
     * dropped, so it cannot be stamped over a missing suffix. */
    if (!bad && fflush(k->f) == 0) {
        k->flags |= SL_BGOR_F_SEALED;
        if (complete && k->dropped == 0u) k->flags |= SL_BGOR_F_COMPLETE;
        bad |= patch_word(k, SL_BGOR_H_FLAGS, k->flags);
    } else {
        bad = 1;
    }
    if (fclose(k->f) != 0) bad = 1;
    k->f = NULL;
    if (bad)
        return sink_refuse(why, whycap,
                           "WRITE: the final header could not be written, so "
                           "the artifact is not complete");
    return 0;
}

int sl_bgor_close(struct sl_bgor *k, char *why, int whycap)
{
    return seal(k, 1, why, whycap);
}

int sl_bgor_seal_failed(struct sl_bgor *k, const char *reason,
                        char *why, int whycap)
{
    sl_bgor_failed(k, reason);
    /* COMPLETE is NOT requested. The rows written so far and the final
     * counters are kept, the file stays on disk as the evidence of the attempt,
     * and a reader refuses it as INCOMPLETE rather than reading a prefix as a
     * whole measurement. */
    return seal(k, 0, why, whycap);
}

/* ---- the preflight ----------------------------------------------------- */

int sl_bgor_preflight(const struct sl_bgsnap *s, const char *path,
                      unsigned *out_rows, unsigned *out_bytes,
                      char *why, int whycap)
{
    unsigned rows = 0u, bytes = 0u, i;
    FILE *probe;

    if (out_rows != NULL) *out_rows = 0u;
    if (out_bytes != NULL) *out_bytes = 0u;
    if (why != NULL && whycap > 0) why[0] = '\0';
    if (s == NULL || s->draw == NULL)
        return sink_refuse(why, whycap,
                           "OPEN: no snapshot was supplied to size the "
                           "artifact from");
    if (path == NULL || *path == '\0')
        return sink_refuse(why, whycap, "OPEN: no artifact path was supplied");

    /* THE EXACT ROW COUNT THIS RUN WILL PRODUCE: one row per index position of
     * every draw. Accumulated through the overflow guard rather than by plain
     * addition, because the sum of 4096 draws' index counts is not bounded by
     * any one of them. */
    for (i = 0u; i < s->draws; i++) {
        if (!sl_bgsnap_sum(rows, s->draw[i].index_count, &rows))
            return sink_refuse(why, whycap,
                               "ROWS_OVERFLOW: the index counts of the first %u "
                               "draws already sum past 32 bits, so the row "
                               "count cannot be computed", i + 1u);
    }
    if (out_rows != NULL) *out_rows = rows;

    if (rows > g_cap)
        return sink_refuse(why, whycap,
                           "CAPACITY: this frame needs %u rows and the cap in "
                           "force is %u; retention is mandatory, so the run is "
                           "refused here - before any context exists - rather "
                           "than captured and then cut to a prefix",
                           rows, g_cap);

    /* THE BYTE EXTENT, with the guards, so the artifact's own u32 extent field
     * cannot wrap. The cap makes this safe today; the guards make it safe for
     * any cap. */
    if (!sl_bgsnap_extent(rows, SL_BGOR_RECORD_BYTES, &bytes) ||
        !sl_bgsnap_sum(bytes, SL_BGOR_HEADER_BYTES, &bytes))
        return sink_refuse(why, whycap,
                           "BYTES_OVERFLOW: %u rows of %u bytes plus the %u-byte "
                           "header does not fit the 32-bit extent the artifact "
                           "records", rows, SL_BGOR_RECORD_BYTES,
                           SL_BGOR_HEADER_BYTES);
    if (out_bytes != NULL) *out_bytes = bytes;

    /* CREATE ONCE, CHECKED HERE rather than after a context has been built. An
     * existing artifact is an earlier run's measurements or an earlier
     * failure's evidence; either way this run does not get that filename. */
    probe = fopen(path, "rb");
    if (probe != NULL) {
        fclose(probe);
        return sink_refuse(why, whycap,
                           "EXISTS: %s is already there; the artifact is "
                           "created once, and an earlier run's records or "
                           "failure evidence are not removed to free the name",
                           path);
    }
    return 0;
}

void sl_bgor_abandon(struct sl_bgor *k, const char *path)
{
    /* THE FILE IS LEFT ON DISK. An artifact from an attempt that failed before
     * its first row still records that the attempt happened, at which path and
     * with which frame identity; removing it to free the filename deletes that
     * evidence. It carries no SL_BGOR_F_COMPLETE, so a reader refuses it as
     * INCOMPLETE and cannot mistake it for a measurement. */
    (void) path;
    if (k == NULL) return;
    if (k->f != NULL) { fclose(k->f); k->f = NULL; }
    memset(k, 0, sizeof *k);
}

/* ---- the reader -------------------------------------------------------- */

int sl_bgor_read(const char *path, struct sl_bgor_info *info,
                 struct sl_bgor_row *rows, unsigned cap, char *why, int whycap)
{
    unsigned char head[SL_BGOR_HEADER_BYTES];
    unsigned char row[SL_BGOR_RECORD_BYTES];
    const struct sl_bgop_field *fields = sl_bgop_fields();
    unsigned extent = 0u, want = 0u, i, j;
    unsigned long file_length;
    long told;
    FILE *f;

    if (why != NULL && whycap > 0) why[0] = '\0';
    if (info == NULL)
        return sink_refuse(why, whycap, "OPEN: no info structure was supplied");
    memset(info, 0, sizeof *info);
    if (path == NULL || *path == '\0')
        return sink_refuse(why, whycap, "OPEN: no artifact path was supplied");

    f = fopen(path, "rb");
    if (f == NULL)
        return sink_refuse(why, whycap, "OPEN: %s could not be opened", path);
    if (fseek(f, 0L, SEEK_END) != 0 || (told = ftell(f)) < 0L) {
        fclose(f);
        return sink_refuse(why, whycap, "OPEN: the length of %s is unknown", path);
    }
    file_length = (unsigned long) told;
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return sink_refuse(why, whycap, "OPEN: %s could not be rewound", path);
    }
    if (file_length < (unsigned long) SL_BGOR_HEADER_BYTES ||
        fread(head, 1u, sizeof head, f) != sizeof head) {
        fclose(f);
        return sink_refuse(why, whycap,
                           "SHORT_HEADER: the file is %lu bytes, shorter than "
                           "the %u-byte header", file_length,
                           SL_BGOR_HEADER_BYTES);
    }

    if (sl_bgsnap_u32(head + SL_BGOR_H_MAGIC) != SL_BGOR_MAGIC) {
        unsigned m = sl_bgsnap_u32(head + SL_BGOR_H_MAGIC);
        fclose(f);
        return sink_refuse(why, whycap,
                           "MAGIC: the first four bytes are 0x%08X, not the "
                           "sink's 0x%08X", m, SL_BGOR_MAGIC);
    }
    info->version = sl_bgsnap_u32(head + SL_BGOR_H_VERSION);
    if (info->version != SL_BGOR_VERSION) {
        unsigned v = info->version;
        fclose(f);
        return sink_refuse(why, whycap,
                           "VERSION: the artifact declares version %u; this "
                           "reader implements %u", v, SL_BGOR_VERSION);
    }
    if (sl_bgsnap_u32(head + SL_BGOR_H_HEADER_BYTES) != SL_BGOR_HEADER_BYTES) {
        unsigned h = sl_bgsnap_u32(head + SL_BGOR_H_HEADER_BYTES);
        fclose(f);
        return sink_refuse(why, whycap,
                           "HEADER_BYTES: the artifact declares a %u-byte "
                           "header, not %u", h, SL_BGOR_HEADER_BYTES);
    }

    /* The three multipliers, before any count is multiplied by one of them. */
    info->record_bytes  = sl_bgsnap_u32(head + SL_BGOR_H_RECORD_BYTES);
    info->record_floats = sl_bgsnap_u32(head + SL_BGOR_H_RECORD_FLOATS);
    info->nfields       = sl_bgsnap_u32(head + SL_BGOR_H_NFIELDS);
    if (info->record_bytes != SL_BGOR_RECORD_BYTES) {
        unsigned rb = info->record_bytes;
        fclose(f);
        return sink_refuse(why, whycap,
                           "RECORD_BYTES: the artifact declares %u bytes per "
                           "row, not %u", rb, SL_BGOR_RECORD_BYTES);
    }
    if (info->record_floats != SL_BGOR_RECORD_FLOATS) {
        unsigned rf = info->record_floats;
        fclose(f);
        return sink_refuse(why, whycap,
                           "RECORD_FLOATS: the artifact declares %u floats per "
                           "record, not %u", rf, SL_BGOR_RECORD_FLOATS);
    }
    if (info->nfields != SL_BGOR_NFIELDS) {
        unsigned nf = info->nfields;
        fclose(f);
        return sink_refuse(why, whycap,
                           "NFIELDS: the artifact declares %u fields, not %u",
                           nf, SL_BGOR_NFIELDS);
    }

    /* THE RECORD LAYOUT THE ARTIFACT WAS WRITTEN WITH, against this build's.
     * A layout difference puts every field after it at the wrong offset, which
     * is a set of confident wrong numbers rather than a refusal. */
    for (i = 0u; i < SL_BGOR_NFIELDS; i++) {
        info->comps[i]  = sl_bgsnap_u32(head + SL_BGOR_H_FIELDS + 8u * i);
        info->offset[i] = sl_bgsnap_u32(head + SL_BGOR_H_FIELDS + 8u * i + 4u);
        if (info->comps[i] != (unsigned) fields[i].comps ||
            info->offset[i] != (unsigned) fields[i].offset) {
            unsigned ii = i, c = info->comps[i], o = info->offset[i];
            fclose(f);
            return sink_refuse(why, whycap,
                               "LAYOUT: field %u is %u component(s) at float "
                               "offset %u in the artifact but %d at %d in this "
                               "build; every field after it would be read from "
                               "the wrong offset", ii, c, o,
                               fields[ii].comps, fields[ii].offset);
        }
    }

    /* COMPLETE, OR NOTHING. An artifact without it is a run that died or a
     * header that was never rewritten; its rows are not a whole measurement. */
    info->flags = sl_bgsnap_u32(head + SL_BGOR_H_FLAGS);
    memcpy(info->failure, head + SL_BGOR_H_FAILURE, SL_BGOR_FAILURE_BYTES);
    info->failure[SL_BGOR_FAILURE_BYTES] = '\0';
    /* SEALED, OR NOTHING CAN BE READ. Until the final counters were written the
     * header holds the provisional zeros from open, so an unsealed artifact
     * does not describe its own contents and no count in it may be trusted.
     *
     * COMPLETE is NOT required here, and that is deliberate: a capped or failed
     * artifact has its rows on disk and honestly counted, and refusing to open
     * it would destroy the evidence it exists to preserve. The flags come back
     * in `info` and the caller is told which facts hold - whether every row is
     * present is SL_BGOR_F_COMPLETE's answer, not this function's. */
    if ((info->flags & SL_BGOR_F_SEALED) == 0u) {
        unsigned fl = info->flags;
        fclose(f);
        return sink_refuse(why, whycap,
                           "INCOMPLETE: SL_BGOR_F_SEALED is absent (flags "
                           "0x%08X); the final counters were never written, so "
                           "this header does not describe this file and no "
                           "count in it can be trusted%s%s", fl,
                           info->failure[0] != '\0' ? ", failure: " : "",
                           info->failure[0] != '\0' ? info->failure : "");
    }

    /* The row count against the cap BEFORE it is multiplied. */
    info->records     = sl_bgsnap_u32(head + SL_BGOR_H_RECORDS);
    info->dropped     = sl_bgsnap_u32(head + SL_BGOR_H_DROPPED);
    info->draws       = sl_bgsnap_u32(head + SL_BGOR_H_DRAWS);
    info->file_bytes  = sl_bgsnap_u32(head + SL_BGOR_H_FILE_BYTES);
    info->max_records = sl_bgsnap_u32(head + SL_BGOR_H_MAX_RECORDS);
    info->frame       = sl_bgsnap_u32(head + SL_BGOR_H_FRAME);
    info->read        = sl_bgsnap_u32(head + SL_BGOR_H_READ);
    info->cloud       = sl_bgsnap_u32(head + SL_BGOR_H_CLOUD);
    info->record_off  = sl_bgsnap_u32(head + SL_BGOR_H_RECORD_OFF);
    memcpy(info->exe_sha, head + SL_BGOR_H_EXE_SHA, 32u);
    memcpy(info->src_sha, head + SL_BGOR_H_SRC_SHA, 32u);

    if (info->records > SL_BGOR_MAX_RECORDS) {
        unsigned r = info->records;
        fclose(f);
        return sink_refuse(why, whycap,
                           "COUNT_MAX: the artifact declares %u rows, past the "
                           "cap of %u; the count is refused, not reduced, and "
                           "nothing is multiplied by it", r,
                           SL_BGOR_MAX_RECORDS);
    }
    if (!sl_bgsnap_extent(info->records, SL_BGOR_RECORD_BYTES, &extent) ||
        !sl_bgsnap_sum(extent, SL_BGOR_HEADER_BYTES, &extent)) {
        fclose(f);
        return sink_refuse(why, whycap,
                           "EXTENT_OVERFLOW: %u rows of %u bytes plus the "
                           "header does not fit 32 bits", info->records,
                           SL_BGOR_RECORD_BYTES);
    }
    if ((unsigned long) extent > file_length) {
        unsigned e = extent;
        fclose(f);
        return sink_refuse(why, whycap,
                           "TRUNCATED: the artifact declares %u rows, which "
                           "need %u bytes, but the file is %lu; a truncated "
                           "artifact is unusable, not smaller", info->records,
                           e, file_length);
    }
    if (info->record_off != SL_BGOR_HEADER_BYTES) {
        unsigned ro = info->record_off;
        fclose(f);
        return sink_refuse(why, whycap,
                           "TRUNCATED: the rows are declared to begin at byte "
                           "%u, not immediately after the %u-byte header", ro,
                           SL_BGOR_HEADER_BYTES);
    }

    /* REFUSED, NOT PART-READ. A caller that asked for fewer rows than the
     * artifact holds would otherwise take the first `cap` of them for all of
     * them. */
    want = info->records;
    if (rows == NULL && want > 0u) {
        fclose(f);
        return sink_refuse(why, whycap,
                           "CAPACITY: the artifact holds %u rows and no row "
                           "buffer was supplied", want);
    }
    if (want > cap) {
        unsigned w = want;
        fclose(f);
        return sink_refuse(why, whycap,
                           "CAPACITY: the artifact holds %u rows and the caller "
                           "has room for %u; it is refused rather than read in "
                           "part", w, cap);
    }

    for (i = 0u; i < want; i++) {
        if (fread(row, 1u, sizeof row, f) != sizeof row) {
            unsigned ii = i;
            fclose(f);
            return sink_refuse(why, whycap,
                               "TRUNCATED: row %u of %u could not be read in "
                               "full", ii, want);
        }
        rows[i].draw = sl_bgsnap_u32(row + SL_BGOR_R_DRAW);
        rows[i].vert = sl_bgsnap_u32(row + SL_BGOR_R_VERT);
        for (j = 0u; j < SL_BGOR_RECORD_FLOATS; j++) {
            rows[i].measured[j]  = sl_bgsnap_u32(row + SL_BGOR_R_MEASURED + 4u * j);
            rows[i].reference[j] = sl_bgsnap_u32(row + SL_BGOR_R_REFERENCE + 4u * j);
            rows[i].cls[j]       = row[SL_BGOR_R_CLASS + j];
        }
    }
    fclose(f);
    return 0;
}

void sl_bgor_default_path(char *out, int cap, const char *tag)
{
    const char *tmp = getenv("SL_BGOP_SINK");
    if (tmp != NULL && *tmp != '\0') {
        snprintf(out, (size_t) cap, "%s", tmp);
        return;
    }
    tmp = getenv("TEMP");
    if (tmp == NULL || *tmp == '\0') tmp = ".";
    snprintf(out, (size_t) cap, "%s\\sl_bgop_records_%s.bin", tmp,
             (tag != NULL) ? tag : "run");
}
