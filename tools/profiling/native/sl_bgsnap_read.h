/* THE SNAPSHOT READER - the only thing between a ROM-derived file and this
 * probe's memory.
 *
 * WHAT IT IS FOR. src/gfx/sl_bgsnap.h defines a file that carries one frame of
 * the background pass's REAL inputs. That file is produced from the player's own
 * extracted world asset, so its bytes are not this repository's and are not
 * reviewed before they are parsed. Every count, offset and index in it is
 * attacker-shaped input as far as this probe is concerned, and the probe's
 * address space is on the other side of this one function.
 *
 * THE RULE, AND IT HAS NO EXCEPTIONS: REFUSE, NEVER CLAMP, NEVER PARTIALLY
 * READ. A truncated snapshot is not a smaller snapshot. A count over its
 * maximum is not a count to be reduced to the maximum. A draw whose index range
 * runs past the index array is not a draw to be shortened. Each of those is a
 * file this probe cannot use, and the reader returns the rule that fired by
 * name so the refusal is diagnosable instead of a mystery. Nothing is written
 * into `out` on any refusal beyond zeroing it.
 *
 * THE ORDER IS LOAD-BEARING. Every count is checked against its maximum through
 * SL_BGSNAP_COUNTS_OK BEFORE it is multiplied by a record size, so no size and
 * no allocation is ever computed from an unvalidated product. Each product and
 * each offset-plus-extent sum is then checked for 32-bit overflow EXPLICITLY,
 * because the maxima bound the products but say nothing about a declared offset:
 * `vertex_off` can be any 32-bit value, so `vertex_off + vertices * 52` can wrap
 * even when `vertices` is in bounds, and a wrapped sum compares small against a
 * file length it is nowhere near.
 *
 * BOUNDED MEMORY, ONE ALLOCATION. The decoded draws, vertices and indices live
 * in a single block whose size is the sum of three validated products. There is
 * no growth loop, no realloc and no per-record allocation, so the peak is a
 * function of the validated counts and of nothing the file can say afterwards.
 *
 * EVERY FIELD IS LOADED AS BYTES, little-endian, through explicit shifts. The
 * file's floats are carried as their 32-bit patterns and are kept as patterns in
 * the decoded form too: the comparison this feeds is bit-for-bit, so a reader
 * that widened a float anywhere would destroy the only property under test, and
 * one that memcpy'd a struct over the bytes would depend on this host's padding
 * and byte order rather than on the format.
 *
 * THE THREE RECORD SIZES ARE ALL READ FROM THE FILE, never assumed. The header
 * declares the header, draw, vertex AND index record sizes, and each is checked
 * against this build's constant before any count is multiplied by it. They are
 * the multipliers, so a file written with a different one would put every field
 * after the first at the wrong offset - which is a wrong answer rather than a
 * refusal unless the size itself is checked.
 *
 * A STATE WORD IS EVIDENCE ONLY WHEN ITS BIT IS SET. The per-draw state array
 * comes with SL_BGSNAP_D_STATE_SET, a bitmask saying which of the sixteen words
 * were actually read from the pass. The mask is consulted before any word is
 * used, through sl_bgsnap_state_recorded, because the states that matter most
 * are genuinely zero - blending off, culling disabled, an alpha reference of 0 -
 * and a reader that treated zero as "not recorded" would throw away exactly
 * those. An unset word is absent evidence and its value is not read.
 */
#ifndef SL_BGSNAP_READ_H
#define SL_BGSNAP_READ_H

#include "sl_bgsnap.h"

/* THE PROBE'S OWN PER-DRAW CAPTURE CEILING, which is not the format's.
 * SL_BGSNAP_MAX_INDICES allows a draw of four million vertices; one bulk
 * transform-feedback capture of that many 21-float records is 352 MB, which a
 * 32-bit process cannot hold and which no diagnostic needs. A draw past this
 * ceiling is REFUSED with its vertex count named - not captured in pieces and
 * not truncated - so the ceiling is a stated limit of the measurement rather
 * than a silent narrowing of it. */
#define SL_BGOP_SNAP_MAX_DRAW_VERTS 65536u

/* Large enough for the longest refusal any rule below can produce, including a
 * writer-supplied failure string. A reason that did not fit would be the one
 * reason a reader most needs. */
#define SL_BGSNAP_WHY_CAP 512

/* ---- the decoded forms -------------------------------------------------- */

/* Floats are kept as their 32-bit patterns throughout, for the reason in the
 * header note. sl_bgsnap_f32 is the only place a pattern becomes a float. */

struct sl_bgsnap_draw {
    unsigned flags;              /* SL_BGSNAP_DF_*                           */
    unsigned mesh;
    unsigned dmode;
    unsigned fk1_bits, fk2_bits;
    unsigned first_index, index_count;
    unsigned sc_bits, post_bits;
    unsigned org_bits[3];
    unsigned C_bits[16];
    unsigned texmat_bits[2][16];
    unsigned state[16];          /* SL_BGSNAP_S_*, valid only where state_set */
    unsigned state_set;          /* bit i: word i was really read from the pass */
};

struct sl_bgsnap_vertex {
    unsigned      pos_bits[3];
    unsigned char col[4];        /* exactly the bytes the array binds        */
    unsigned      uv_bits[2][2];
    /* The clip product with the single-range far clamp ALREADY APPLIED to its
     * z, and that same vertex's z BEFORE the clamp. Both are needed: which one
     * gl_Position.z should equal depends on the draw's depth mode. */
    unsigned      clip_bits[4];
    unsigned      fog_bits;      /* the value BOUND to gl_FogCoord           */
    unsigned      zraw_bits;     /* the UNCLAMPED z                          */
};

struct sl_bgsnap {
    unsigned version, flags;
    unsigned draws, vertices, indices;
    unsigned draw_off, vertex_off, index_off, file_bytes;
    unsigned frame, read, cloud;
    unsigned char exe_sha[32], src_sha[32];
    /* One byte longer than the file's fields, so these are NUL-terminated here
     * whatever the file put in them. */
    char recipe[SL_BGSNAP_RECIPE_BYTES + 1u];
    char failure[SL_BGSNAP_FAILURE_BYTES + 1u];
    unsigned long file_length;   /* what the file actually measured           */

    struct sl_bgsnap_draw   *draw;
    struct sl_bgsnap_vertex *vertex;
    unsigned                *index;
    void                    *block;   /* the ONE allocation the three carve up */
};

/* ---- the reader --------------------------------------------------------- */

/* 0 when the file is usable and `out` is fully populated; non-zero on ANY
 * refusal, with `out` zeroed and `why` holding the rule that fired. The reason
 * begins with the rule's name in capitals - MAGIC, VERSION, HEADER_BYTES,
 * DRAW_BYTES, VERTEX_BYTES, INCOMPLETE, COUNT_MAX, EXTENT_OVERFLOW,
 * FILE_BYTES, OFFSET, OVERLAP, TRUNCATED, DRAW_RANGE, INDEX_VALUE, OPEN,
 * SHORT_HEADER, INDEX_BYTES or ALLOC - so a caller can state which rule refused
 * the file and not merely that something did. */
int sl_bgsnap_read(const char *path, struct sl_bgsnap *out, char *why, int whycap);

/* Returns the block to the allocator and zeroes `s`. Safe on a zeroed or
 * already-released snapshot, and safe on one a refused read left behind. */
void sl_bgsnap_release(struct sl_bgsnap *s);

/* ---- the arithmetic guards, exposed so they can be driven directly ------
 *
 * Both return 1 when the result is exact in 32 bits and 0 when it would wrap.
 * They are the only multiplication and the only addition the reader performs on
 * file-supplied numbers, and they are reachable from a check because a product
 * that wraps cannot be reached through a file: the maxima are chosen so that
 * every count times its record size is below 2^32, which means COUNT_MAX
 * necessarily refuses such a file first. The guard is the second line, so it is
 * exercised where it can be - here - rather than assumed. */
int sl_bgsnap_extent(unsigned count, unsigned size, unsigned *out);
int sl_bgsnap_sum(unsigned a, unsigned b, unsigned *out);

/* 1 when state word `word` of this draw was really read from the pass, 0 when
 * it was not. THE ONLY WAY A STATE WORD MAY BE REACHED. A word whose bit is
 * clear is absent evidence: its stored value says nothing and must not be read,
 * and in particular must not be taken as the value zero. A word whose bit is
 * set is the value it holds, zero included - which is the point of the mask,
 * since blending off, culling disabled and an alpha reference of 0 are all
 * genuinely zero and all worth knowing. */
int sl_bgsnap_state_recorded(const struct sl_bgsnap_draw *d, unsigned word);

/* The little-endian 32-bit load, by explicit shifts. */
unsigned sl_bgsnap_u32(const unsigned char *p);

/* A 32-bit pattern as the float it denotes. The one widening in the reader. */
float sl_bgsnap_f32(unsigned bits);

/* 1 when a pattern is a denormal - a zero exponent with a non-zero mantissa.
 * GLSL's default precision does not require denormal support, so a reference
 * value that is denormal has no bit-exact oracle on a driver that flushes it. */
int sl_bgsnap_is_denormal(unsigned bits);

#endif /* SL_BGSNAP_READ_H */
