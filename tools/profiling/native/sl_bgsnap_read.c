/* THE SNAPSHOT READER. Read sl_bgsnap_read.h first: it holds the contract, the
 * refuse-never-clamp rule, the ordering requirement and the reason the two
 * TEXMAT flag bits are restated locally.
 *
 * THE SHAPE OF THIS FILE IS THE SHAPE OF THE VALIDATION ORDER. Each rule is one
 * block, in the order it must run, and every block ends in `refuse(...)` naming
 * itself. Nothing is allocated until every count, product, offset, sum and
 * overlap has passed, and nothing is decoded until the allocation succeeded.
 * The per-draw and per-index rules run after the decode because they are rules
 * about decoded values, and they still refuse the WHOLE file rather than
 * dropping the offending draw.
 */
#include "sl_bgsnap_read.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the refusal path --------------------------------------------------- */

static char *g_why;
static int   g_whycap;

static int refuse(const char *fmt, ...)
{
    va_list ap;
    if (g_why != NULL && g_whycap > 0) {
        va_start(ap, fmt);
        vsnprintf(g_why, (size_t) g_whycap - 1u, fmt, ap);
        va_end(ap);
        g_why[g_whycap - 1] = '\0';
    }
    return 1;
}

/* ---- the loads and the arithmetic guards -------------------------------- */

unsigned sl_bgsnap_u32(const unsigned char *p)
{
    return (unsigned) p[0] | ((unsigned) p[1] << 8)
         | ((unsigned) p[2] << 16) | ((unsigned) p[3] << 24);
}

float sl_bgsnap_f32(unsigned bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

int sl_bgsnap_is_denormal(unsigned bits)
{
    return (bits & 0x7F800000u) == 0u && (bits & 0x007FFFFFu) != 0u;
}

/* The division test rather than a wider type: there is no guaranteed wider
 * unsigned here, and `count > MAX / size` is exact in 32 bits for any non-zero
 * size. */
int sl_bgsnap_extent(unsigned count, unsigned size, unsigned *out)
{
    if (out != NULL) *out = 0u;
    if (size == 0u) return 0;
    if (count > 0xFFFFFFFFu / size) return 0;
    if (out != NULL) *out = count * size;
    return 1;
}

int sl_bgsnap_sum(unsigned a, unsigned b, unsigned *out)
{
    if (out != NULL) *out = 0u;
    if (a > 0xFFFFFFFFu - b) return 0;
    if (out != NULL) *out = a + b;
    return 1;
}

/* ---- the release path --------------------------------------------------- */

int sl_bgsnap_state_recorded(const struct sl_bgsnap_draw *d, unsigned word)
{
    if (d == NULL || word >= 16u) return 0;
    return (d->state_set & (1u << word)) != 0u;
}

void sl_bgsnap_release(struct sl_bgsnap *s)
{
    if (s == NULL) return;
    if (s->block != NULL) free(s->block);
    memset(s, 0, sizeof *s);
}

/* ---- one half-open byte range against another --------------------------- */

/* Both ranges are already known not to wrap, so the comparison is ordinary.
 * A zero-length range overlaps nothing, which is why a count of zero is not
 * required to carry a meaningful offset. */
static int ranges_overlap(unsigned a_off, unsigned a_len,
                          unsigned b_off, unsigned b_len)
{
    if (a_len == 0u || b_len == 0u) return 0;
    return a_off < b_off + b_len && b_off < a_off + a_len;
}

/* ---- the one array rule, applied to each of the three ------------------- */

/* THE FOUR THINGS AN ARRAY'S PLACEMENT MUST SATISFY, in the order they must be
 * asked: its extent must not overflow (the caller has already done that and
 * passes it in), it must start after the header, its end must not wrap, its end
 * must be within the writer's own declared extent, and its end must be within
 * the bytes the file actually has. The last two are different rules and are
 * named differently: an end past `file_bytes` is a header that contradicts
 * itself, an end past the file's length is a file that was cut short. */
static int array_placed(const char *what, unsigned off, unsigned len,
                        unsigned file_bytes, unsigned long file_length,
                        unsigned *out_end)
{
    unsigned end;
    *out_end = 0u;
    if (len == 0u) return 0;          /* nothing to place, nothing to check */
    if (off < SL_BGSNAP_HEADER_BYTES)
        return refuse("OFFSET: the %s array starts at byte %u, inside the "
                      "%u-byte header; an array may not overlap the header",
                      what, off, SL_BGSNAP_HEADER_BYTES);
    if (!sl_bgsnap_sum(off, len, &end))
        return refuse("EXTENT_OVERFLOW: the %s array's offset %u plus its "
                      "extent %u bytes wraps 32 bits, so its end cannot be "
                      "compared with any file length",
                      what, off, len);
    if (end > file_bytes)
        return refuse("OFFSET: the %s array runs to byte %u, past the %u bytes "
                      "the header declares the file to be",
                      what, end, file_bytes);
    if ((unsigned long) end > file_length)
        return refuse("TRUNCATED: the %s array runs to byte %u but the file is "
                      "%lu bytes; a truncated snapshot is unusable, not smaller",
                      what, end, file_length);
    *out_end = end;
    return 0;
}

/* ---- the read ----------------------------------------------------------- */

int sl_bgsnap_read(const char *path, struct sl_bgsnap *out, char *why, int whycap)
{
    unsigned char head[SL_BGSNAP_HEADER_BYTES];
    unsigned char rec[SL_BGSNAP_DRAW_BYTES];
    unsigned draw_len = 0u, vert_len = 0u, idx_len = 0u;
    unsigned draw_end = 0u, vert_end = 0u, idx_end = 0u;
    unsigned magic, header_bytes, draw_bytes, vertex_bytes, index_bytes;
    unsigned total = 0u, want;
    unsigned long file_length;
    long told;
    FILE *f;
    unsigned char *cursor;
    unsigned i, j;

    g_why = why; g_whycap = whycap;
    if (why != NULL && whycap > 0) why[0] = '\0';
    if (out == NULL) return refuse("OPEN: no snapshot structure was supplied");
    memset(out, 0, sizeof *out);
    if (path == NULL || *path == '\0')
        return refuse("OPEN: no snapshot path was supplied");

    f = fopen(path, "rb");
    if (f == NULL)
        return refuse("OPEN: %s could not be opened for reading", path);

    /* ---- 0. how long the file actually is -------------------------------
     * Measured before anything in it is believed, because every later rule
     * compares a declared extent against this number. */
    if (fseek(f, 0L, SEEK_END) != 0 || (told = ftell(f)) < 0L) {
        fclose(f);
        return refuse("OPEN: the length of %s could not be determined", path);
    }
    file_length = (unsigned long) told;
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return refuse("OPEN: %s could not be rewound after measuring it", path);
    }
    out->file_length = file_length;

    /* ---- 1. the header is there at all ---------------------------------- */
    if (file_length < (unsigned long) SL_BGSNAP_HEADER_BYTES) {
        fclose(f);
        return refuse("SHORT_HEADER: the file is %lu bytes, shorter than the "
                      "%u-byte header the format begins with",
                      file_length, SL_BGSNAP_HEADER_BYTES);
    }
    if (fread(head, 1u, sizeof head, f) != sizeof head) {
        fclose(f);
        return refuse("SHORT_HEADER: the %u-byte header could not be read in "
                      "full", SL_BGSNAP_HEADER_BYTES);
    }

    /* ---- 2. the identity, BEFORE any count in it is read ---------------- */
    magic = sl_bgsnap_u32(head + SL_BGSNAP_H_MAGIC);
    if (magic != SL_BGSNAP_MAGIC) {
        fclose(f);
        return refuse("MAGIC: the first four bytes are 0x%08X, not the format's "
                      "0x%08X", magic, SL_BGSNAP_MAGIC);
    }
    out->version = sl_bgsnap_u32(head + SL_BGSNAP_H_VERSION);
    if (out->version != SL_BGSNAP_VERSION) {
        fclose(f);
        return refuse("VERSION: the file declares version %u; this reader "
                      "implements version %u and will not guess at the "
                      "difference", out->version, SL_BGSNAP_VERSION);
    }

    /* ---- 3. the record sizes the file was written with ------------------
     * Checked before the counts are multiplied by anything, because these are
     * the multipliers. A file written with a different per-draw or per-vertex
     * size would have every field after the first at the wrong offset. */
    header_bytes = sl_bgsnap_u32(head + SL_BGSNAP_H_HEADER_BYTES);
    if (header_bytes != SL_BGSNAP_HEADER_BYTES) {
        fclose(f);
        return refuse("HEADER_BYTES: the file declares a %u-byte header, not "
                      "%u", header_bytes, SL_BGSNAP_HEADER_BYTES);
    }
    draw_bytes = sl_bgsnap_u32(head + SL_BGSNAP_H_DRAW_BYTES);
    if (draw_bytes != SL_BGSNAP_DRAW_BYTES) {
        fclose(f);
        return refuse("DRAW_BYTES: the file declares %u bytes per draw, not %u",
                      draw_bytes, SL_BGSNAP_DRAW_BYTES);
    }
    vertex_bytes = sl_bgsnap_u32(head + SL_BGSNAP_H_VERTEX_BYTES);
    if (vertex_bytes != SL_BGSNAP_VERTEX_BYTES) {
        fclose(f);
        return refuse("VERTEX_BYTES: the file declares %u bytes per vertex, "
                      "not %u", vertex_bytes, SL_BGSNAP_VERTEX_BYTES);
    }
    /* The index element size is declared in the file like the other two, so it
     * is checked like the other two. Unchecked, a file written with 8-byte
     * indices would have every index after the first read from the wrong offset
     * and would still pass every bounds test - a wrong answer, not a refusal. */
    index_bytes = sl_bgsnap_u32(head + SL_BGSNAP_H_INDEX_BYTES);
    if (index_bytes != SL_BGSNAP_INDEX_BYTES) {
        fclose(f);
        return refuse("INDEX_BYTES: the file declares %u bytes per index, not "
                      "%u", index_bytes, SL_BGSNAP_INDEX_BYTES);
    }

    /* ---- 4. the writer finished -----------------------------------------
     * SL_BGSNAP_F_COMPLETE is written last, after every payload byte. Its
     * absence means the writer failed or was interrupted, and such a file is
     * REFUSED rather than read as a short one - which is the whole reason the
     * flag is written last. */
    out->flags = sl_bgsnap_u32(head + SL_BGSNAP_H_FLAGS);
    if ((out->flags & SL_BGSNAP_F_COMPLETE) == 0u) {
        unsigned fl = out->flags;
        fclose(f);
        memcpy(out->failure, head + SL_BGSNAP_H_FAILURE, SL_BGSNAP_FAILURE_BYTES);
        out->failure[SL_BGSNAP_FAILURE_BYTES] = '\0';
        return refuse("INCOMPLETE: SL_BGSNAP_F_COMPLETE is absent (flags "
                      "0x%08X%s); the writer did not finish, so this is a "
                      "failed or interrupted snapshot and not a short one%s%s",
                      fl, (fl & SL_BGSNAP_F_FAILED) ? ", SL_BGSNAP_F_FAILED set" : "",
                      out->failure[0] != '\0' ? ", writer reason: " : "",
                      out->failure[0] != '\0' ? out->failure : "");
    }

    /* ---- 5. THE COUNTS, AGAINST THEIR MAXIMA, BEFORE ANY MULTIPLICATION -
     * This is the rule src/gfx/sl_bgsnap.h calls the whole of the format's
     * memory safety, and it is the reason no size below is computed from an
     * unchecked number. */
    out->draws    = sl_bgsnap_u32(head + SL_BGSNAP_H_DRAWS);
    out->vertices = sl_bgsnap_u32(head + SL_BGSNAP_H_VERTICES);
    out->indices  = sl_bgsnap_u32(head + SL_BGSNAP_H_INDICES);
    if (!SL_BGSNAP_COUNTS_OK(out->draws, out->vertices, out->indices)) {
        unsigned d = out->draws, v = out->vertices, x = out->indices;
        fclose(f);
        return refuse("COUNT_MAX: a count is over its maximum - draws %u of %u, "
                      "vertices %u of %u, indices %u of %u; the count is "
                      "refused, not reduced, and nothing is multiplied by it",
                      d, SL_BGSNAP_MAX_DRAWS, v, SL_BGSNAP_MAX_VERTICES,
                      x, SL_BGSNAP_MAX_INDICES);
    }

    /* ---- 6. the three products, each checked for overflow EXPLICITLY ----
     * The maxima above bound these, so none of the three can wrap for a file
     * that reached here. The guards stay because the maxima and the record
     * sizes are both editable constants and a future pair of them need not
     * keep that property; the check costs a division and removes the need to
     * re-derive it. */
    if (!sl_bgsnap_extent(out->draws, SL_BGSNAP_DRAW_BYTES, &draw_len) ||
        !sl_bgsnap_extent(out->vertices, SL_BGSNAP_VERTEX_BYTES, &vert_len) ||
        !sl_bgsnap_extent(out->indices, SL_BGSNAP_INDEX_BYTES, &idx_len)) {
        fclose(f);
        return refuse("EXTENT_OVERFLOW: a count times its record size does not "
                      "fit 32 bits (draws %u x %u, vertices %u x %u, indices "
                      "%u x %u)", out->draws, SL_BGSNAP_DRAW_BYTES,
                      out->vertices, SL_BGSNAP_VERTEX_BYTES,
                      out->indices, SL_BGSNAP_INDEX_BYTES);
    }

    /* ---- 7. the declared extent against the bytes that exist ------------ */
    out->file_bytes = sl_bgsnap_u32(head + SL_BGSNAP_H_FILE_BYTES);
    if (out->file_bytes < SL_BGSNAP_HEADER_BYTES) {
        unsigned fb = out->file_bytes;
        fclose(f);
        return refuse("FILE_BYTES: the header declares the file to be %u bytes, "
                      "less than the %u-byte header it begins with",
                      fb, SL_BGSNAP_HEADER_BYTES);
    }
    if ((unsigned long) out->file_bytes > file_length) {
        unsigned fb = out->file_bytes;
        fclose(f);
        return refuse("TRUNCATED: the header declares %u bytes but the file is "
                      "%lu; a truncated snapshot is unusable, not smaller",
                      fb, file_length);
    }

    /* ---- 8. where the three arrays say they are ------------------------- */
    out->draw_off   = sl_bgsnap_u32(head + SL_BGSNAP_H_DRAW_OFF);
    out->vertex_off = sl_bgsnap_u32(head + SL_BGSNAP_H_VERTEX_OFF);
    out->index_off  = sl_bgsnap_u32(head + SL_BGSNAP_H_INDEX_OFF);
    if (array_placed("draw", out->draw_off, draw_len, out->file_bytes,
                     file_length, &draw_end) != 0 ||
        array_placed("vertex", out->vertex_off, vert_len, out->file_bytes,
                     file_length, &vert_end) != 0 ||
        array_placed("index", out->index_off, idx_len, out->file_bytes,
                     file_length, &idx_end) != 0) {
        fclose(f);
        return 1;                 /* array_placed already named the rule */
    }

    /* ---- 9. and that they do not sit on top of one another -------------- */
    if (ranges_overlap(out->draw_off, draw_len, out->vertex_off, vert_len)) {
        unsigned a = out->draw_off, b = out->vertex_off;
        fclose(f);
        return refuse("OVERLAP: the draw array [%u,%u) and the vertex array "
                      "[%u,%u) overlap; one of them would be read as the other",
                      a, draw_end, b, vert_end);
    }
    if (ranges_overlap(out->draw_off, draw_len, out->index_off, idx_len)) {
        unsigned a = out->draw_off, b = out->index_off;
        fclose(f);
        return refuse("OVERLAP: the draw array [%u,%u) and the index array "
                      "[%u,%u) overlap", a, draw_end, b, idx_end);
    }
    if (ranges_overlap(out->vertex_off, vert_len, out->index_off, idx_len)) {
        unsigned a = out->vertex_off, b = out->index_off;
        fclose(f);
        return refuse("OVERLAP: the vertex array [%u,%u) and the index array "
                      "[%u,%u) overlap", a, vert_end, b, idx_end);
    }

    /* ---- 10. the rest of the header, which carries no sizes ------------- */
    out->frame = sl_bgsnap_u32(head + SL_BGSNAP_H_FRAME);
    out->read  = sl_bgsnap_u32(head + SL_BGSNAP_H_READ);
    out->cloud = sl_bgsnap_u32(head + SL_BGSNAP_H_CLOUD);
    memcpy(out->exe_sha, head + SL_BGSNAP_H_EXE_SHA, 32u);
    memcpy(out->src_sha, head + SL_BGSNAP_H_SRC_SHA, 32u);
    memcpy(out->recipe, head + SL_BGSNAP_H_RECIPE, SL_BGSNAP_RECIPE_BYTES);
    out->recipe[SL_BGSNAP_RECIPE_BYTES] = '\0';
    memcpy(out->failure, head + SL_BGSNAP_H_FAILURE, SL_BGSNAP_FAILURE_BYTES);
    out->failure[SL_BGSNAP_FAILURE_BYTES] = '\0';

    /* ---- 11. ONE ALLOCATION, from three validated products -------------- */
    if (!sl_bgsnap_extent(out->draws, (unsigned) sizeof(struct sl_bgsnap_draw), &want) ||
        !sl_bgsnap_sum(total, want, &total)) {
        fclose(f);
        return refuse("EXTENT_OVERFLOW: the decoded draw array does not fit 32 "
                      "bits");
    }
    if (!sl_bgsnap_extent(out->vertices, (unsigned) sizeof(struct sl_bgsnap_vertex), &want) ||
        !sl_bgsnap_sum(total, want, &total)) {
        fclose(f);
        return refuse("EXTENT_OVERFLOW: the decoded vertex array does not fit "
                      "32 bits");
    }
    if (!sl_bgsnap_extent(out->indices, (unsigned) sizeof(unsigned), &want) ||
        !sl_bgsnap_sum(total, want, &total)) {
        fclose(f);
        return refuse("EXTENT_OVERFLOW: the decoded index array does not fit 32 "
                      "bits");
    }
    /* Every member of all three decoded forms is 4 bytes wide or a 4-byte array
     * of bytes, so each struct's size is a multiple of 4 and carving the block
     * at the running total keeps every pointer 4-byte aligned behind malloc's
     * own stronger guarantee. */
    out->block = (total > 0u) ? malloc((size_t) total) : malloc(1u);
    if (out->block == NULL) {
        unsigned t = total;
        fclose(f);
        memset(out, 0, sizeof *out);
        return refuse("ALLOC: the %u bytes the three validated counts need "
                      "could not be allocated", t);
    }
    cursor = (unsigned char *) out->block;
    out->draw = (struct sl_bgsnap_draw *) cursor;
    cursor += (size_t) out->draws * sizeof(struct sl_bgsnap_draw);
    out->vertex = (struct sl_bgsnap_vertex *) cursor;
    cursor += (size_t) out->vertices * sizeof(struct sl_bgsnap_vertex);
    out->index = (unsigned *) cursor;

    /* ---- 12. the draw table, one record at a time ----------------------- */
    if (out->draws > 0u && fseek(f, (long) out->draw_off, SEEK_SET) != 0) {
        fclose(f);
        sl_bgsnap_release(out);
        return refuse("TRUNCATED: byte %u, where the draw array starts, could "
                      "not be reached", out->draw_off);
    }
    for (i = 0u; i < out->draws; i++) {
        struct sl_bgsnap_draw *d = &out->draw[i];
        if (fread(rec, 1u, SL_BGSNAP_DRAW_BYTES, f) != SL_BGSNAP_DRAW_BYTES) {
            unsigned ii = i;
            fclose(f);
            sl_bgsnap_release(out);
            return refuse("TRUNCATED: draw %u of %u could not be read in full",
                          ii, out->draws);
        }
        d->flags       = sl_bgsnap_u32(rec + SL_BGSNAP_D_FLAGS);
        d->mesh        = sl_bgsnap_u32(rec + SL_BGSNAP_D_MESH);
        d->dmode       = sl_bgsnap_u32(rec + SL_BGSNAP_D_DMODE);
        d->fk1_bits    = sl_bgsnap_u32(rec + SL_BGSNAP_D_FK1);
        d->fk2_bits    = sl_bgsnap_u32(rec + SL_BGSNAP_D_FK2);
        d->first_index = sl_bgsnap_u32(rec + SL_BGSNAP_D_FIRST_INDEX);
        d->index_count = sl_bgsnap_u32(rec + SL_BGSNAP_D_INDEX_COUNT);
        d->sc_bits     = sl_bgsnap_u32(rec + SL_BGSNAP_D_SC);
        d->post_bits   = sl_bgsnap_u32(rec + SL_BGSNAP_D_POST);
        for (j = 0u; j < 3u; j++)
            d->org_bits[j] = sl_bgsnap_u32(rec + SL_BGSNAP_D_ORG + 4u * j);
        for (j = 0u; j < 16u; j++) {
            d->C_bits[j]         = sl_bgsnap_u32(rec + SL_BGSNAP_D_C + 4u * j);
            d->texmat_bits[0][j] = sl_bgsnap_u32(rec + SL_BGSNAP_D_TEXMAT0 + 4u * j);
            d->texmat_bits[1][j] = sl_bgsnap_u32(rec + SL_BGSNAP_D_TEXMAT1 + 4u * j);
            d->state[j]          = sl_bgsnap_u32(rec + SL_BGSNAP_D_STATE + 4u * j);
        }
        /* The words are decoded unconditionally; which of them MEAN anything is
         * this mask, and sl_bgsnap_state_recorded is the only way a caller is
         * meant to reach one. Decoding a word whose bit is clear costs nothing
         * and keeps the decode a straight transcription of the record. */
        d->state_set = sl_bgsnap_u32(rec + SL_BGSNAP_D_STATE_SET);
    }

    /* ---- 13. the vertices, in chunks, decoded field by field ------------ */
    if (out->vertices > 0u && fseek(f, (long) out->vertex_off, SEEK_SET) != 0) {
        fclose(f);
        sl_bgsnap_release(out);
        return refuse("TRUNCATED: byte %u, where the vertex array starts, could "
                      "not be reached", out->vertex_off);
    }
    {
        /* A fixed staging buffer, sized in records: the decode is per record
         * whatever the count, so there is no buffer whose size the file picks. */
        enum { CHUNK = 64 };
        static unsigned char vbuf[CHUNK * SL_BGSNAP_VERTEX_BYTES];
        unsigned done = 0u;
        while (done < out->vertices) {
            unsigned have = out->vertices - done;
            unsigned k;
            if (have > (unsigned) CHUNK) have = (unsigned) CHUNK;
            if (fread(vbuf, 1u, (size_t) have * SL_BGSNAP_VERTEX_BYTES, f)
                != (size_t) have * SL_BGSNAP_VERTEX_BYTES) {
                unsigned dd = done;
                fclose(f);
                sl_bgsnap_release(out);
                return refuse("TRUNCATED: the vertex array stops after %u of %u "
                              "records", dd, out->vertices);
            }
            for (k = 0u; k < have; k++) {
                const unsigned char *p = vbuf + (size_t) k * SL_BGSNAP_VERTEX_BYTES;
                struct sl_bgsnap_vertex *v = &out->vertex[done + k];
                for (j = 0u; j < 3u; j++)
                    v->pos_bits[j] = sl_bgsnap_u32(p + SL_BGSNAP_V_POS + 4u * j);
                memcpy(v->col, p + SL_BGSNAP_V_COL, 4u);
                v->uv_bits[0][0] = sl_bgsnap_u32(p + SL_BGSNAP_V_UV0);
                v->uv_bits[0][1] = sl_bgsnap_u32(p + SL_BGSNAP_V_UV0 + 4u);
                v->uv_bits[1][0] = sl_bgsnap_u32(p + SL_BGSNAP_V_UV1);
                v->uv_bits[1][1] = sl_bgsnap_u32(p + SL_BGSNAP_V_UV1 + 4u);
                for (j = 0u; j < 4u; j++)
                    v->clip_bits[j] = sl_bgsnap_u32(p + SL_BGSNAP_V_CLIP + 4u * j);
                v->fog_bits  = sl_bgsnap_u32(p + SL_BGSNAP_V_FOG);
                v->zraw_bits = sl_bgsnap_u32(p + SL_BGSNAP_V_ZRAW);
            }
            done += have;
        }
    }

    /* ---- 14. the indices ------------------------------------------------ */
    if (out->indices > 0u && fseek(f, (long) out->index_off, SEEK_SET) != 0) {
        fclose(f);
        sl_bgsnap_release(out);
        return refuse("TRUNCATED: byte %u, where the index array starts, could "
                      "not be reached", out->index_off);
    }
    {
        enum { ICHUNK = 1024 };
        static unsigned char ibuf[ICHUNK * SL_BGSNAP_INDEX_BYTES];
        unsigned done = 0u;
        while (done < out->indices) {
            unsigned have = out->indices - done;
            unsigned k;
            if (have > (unsigned) ICHUNK) have = (unsigned) ICHUNK;
            if (fread(ibuf, 1u, (size_t) have * SL_BGSNAP_INDEX_BYTES, f)
                != (size_t) have * SL_BGSNAP_INDEX_BYTES) {
                unsigned dd = done;
                fclose(f);
                sl_bgsnap_release(out);
                return refuse("TRUNCATED: the index array stops after %u of %u "
                              "entries", dd, out->indices);
            }
            for (k = 0u; k < have; k++)
                out->index[done + k] =
                    sl_bgsnap_u32(ibuf + (size_t) k * SL_BGSNAP_INDEX_BYTES);
            done += have;
        }
    }

    fclose(f);

    /* ---- 15. the decoded per-draw ranges against the index array --------
     * A range that runs past the array would read indices belonging to no
     * draw, or none at all. The file is refused; the draw is not shortened. */
    for (i = 0u; i < out->draws; i++) {
        const struct sl_bgsnap_draw *d = &out->draw[i];
        unsigned end;
        if (!sl_bgsnap_sum(d->first_index, d->index_count, &end)) {
            unsigned ii = i, fi = d->first_index, ic = d->index_count;
            sl_bgsnap_release(out);
            return refuse("DRAW_RANGE: draw %u declares first_index %u plus "
                          "index_count %u, which wraps 32 bits", ii, fi, ic);
        }
        if (end > out->indices) {
            unsigned ii = i, fi = d->first_index, ic = d->index_count;
            unsigned nidx = out->indices;
            sl_bgsnap_release(out);
            return refuse("DRAW_RANGE: draw %u covers indices [%u,%u) - "
                          "first_index %u plus index_count %u - but the index "
                          "array holds %u; the range is refused, not clipped",
                          ii, fi, end, fi, ic, nidx);
        }
        /* The probe's per-draw CAPTURE ceiling is deliberately NOT applied
         * here. It is a limit of what one transform-feedback capture can hold,
         * not a statement about the file, and a reader that refused on it would
         * be calling a valid snapshot malformed. The capture path applies it
         * and names itself when it does. */
    }

    /* ---- 16. and every index value against the vertex count ------------- */
    for (i = 0u; i < out->indices; i++) {
        if (out->index[i] >= out->vertices) {
            unsigned ii = i, val = out->index[i], nv = out->vertices;
            sl_bgsnap_release(out);
            return refuse("INDEX_VALUE: index %u is %u but the vertex array "
                          "holds %u; every index must be strictly less than "
                          "the vertex count", ii, val, nv);
        }
    }

    return 0;
}
