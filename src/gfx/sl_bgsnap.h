/* THE BACKGROUND DRAW SNAPSHOT - one frame's actual inputs, as a file.
 *
 * WHAT GAP THIS CLOSES. The synthetic vertex probe measures BGG_VS against the
 * CPU reference for constructed inputs and, on a driver, found 646 of 646
 * mandatory components identical. That says nothing about an actual scene: the
 * inputs were built by the probe, not read from an item, and nothing
 * rasterised. The stable one-pixel difference at (71,113) is therefore still
 * unexplained, and no arithmetic in the renderer is implicated by anything
 * measured so far.
 *
 * This format carries ONE FRAME of the background pass's REAL inputs and the
 * CPU reference it computed from them, so the same comparison can be made
 * against actual scene data instead of constructed data. It records; it
 * concludes nothing.
 *
 * WHAT IS IN IT, per draw: the mesh, the depth mode and its coefficient pair,
 * the index range, and the exact uniform set the draw carried - C, sc, org,
 * post, fogless - plus the texture matrices and the bound-state identity the
 * forwarded outputs depend on. Per vertex: the model position as the item
 * holds it, the byte colour and the texture coordinates as the arrays bind
 * them, and the clip position and fog value the CPU loop produced.
 *
 * EVERY FLOAT IS STORED AS ITS 32-BIT PATTERN, never as text and never
 * widened. The comparison this feeds is bit-for-bit, so a format that rounds
 * anywhere would destroy the only property under test.
 *
 * THE DATA IS ROM-DERIVED AND IS NEVER COMMITTED. A snapshot is produced from
 * the player's own extracted world asset, so it inherits exactly the boundary
 * texsources/, texpacks/ and worldassets/ already carry: the user brings the
 * source, a local tool writes the derived file, and neither is ours to
 * distribute. The output directory is ignored by Git, and the writer refuses
 * to overwrite, so a snapshot cannot be silently replaced by a later run.
 *
 * BOUNDED AND EXPLICIT, BY CONSTRUCTION. Every count has a maximum defined
 * here; every count is checked against its maximum BEFORE it is multiplied by
 * a record size, so an allocation size cannot be computed from an unvalidated
 * product. A reader that finds a count over its maximum, an offset outside the
 * file, or a file shorter than its own declared extent, refuses the file - it
 * does not clamp it and does not read what it can. A truncated snapshot is not
 * a smaller snapshot; it is unusable, and both sides say so.
 *
 * ONE SOURCE OF TRUTH. The writer in the renderer and the reader in the
 * offline probe both include this header, so the layout cannot drift between
 * them. Only fixed-width integers and byte arrays appear in the on-disk
 * records: no struct is written to the file directly, no padding is relied on,
 * and every field is stored little-endian through explicit byte stores.
 */
#ifndef SL_BGSNAP_H
#define SL_BGSNAP_H

/* "SNP1" little-endian, so a file whose first four bytes are wrong is refused
 * before any count in it is read. */
#define SL_BGSNAP_MAGIC        0x31504E53u
#define SL_BGSNAP_VERSION      1u

/* THE BOUNDS. These are the whole of the format's memory safety: a count that
 * exceeds its maximum is refused, and only then is it multiplied. The values
 * are sized for one background item's frame, which is tens of thousands of
 * vertices, with headroom - not for an arbitrary scene, because this format is
 * for one frame of one pass and a larger file is a sign of a defect rather
 * than a bigger picture. */
#define SL_BGSNAP_MAX_DRAWS    4096u
#define SL_BGSNAP_MAX_VERTICES 1048576u
#define SL_BGSNAP_MAX_INDICES  4194304u

/* Flags in the header. COMPLETE is written LAST, after every byte of payload
 * has been written and flushed, so a file without it is a failed or
 * interrupted snapshot and is refused by the reader rather than read as a
 * short one. FAILED records that the writer itself detected a problem and
 * names it in `failure`. */
#define SL_BGSNAP_F_COMPLETE   0x00000001u
#define SL_BGSNAP_F_FAILED     0x00000002u

/* Per-draw flags. The DF_ prefix is deliberate and load-bearing: the per-draw
 * FIELD OFFSETS below are SL_BGSNAP_D_*, and two names in both sets collided
 * when the flags shared that prefix. The later definition won, so
 * `flags & SL_BGSNAP_D_TEXMAT0` tested bit 112 and was false for every file
 * that could ever be written - a silent wrong answer rather than a build
 * failure, since the redefinition is only a warning and the build log already
 * carries unrelated ones. Measured by the preprocessor, not inferred. Flags
 * are DF_, offsets are D_, and the two cannot alias again.
 *
 * There is no DF_UV0: texture coordinate set 0 is bound by the background pass
 * for every draw it emits (sl_gfx_dl.c:18689), so unit 0's record is always
 * meaningful and a flag for it would only invite a reader to treat it as
 * optional. Set 1 is not always bound, which is why it has one. */
#define SL_BGSNAP_DF_GPU       0x00000001u  /* the draw went through BGG_VS   */
#define SL_BGSNAP_DF_FOGLESS   0x00000002u
#define SL_BGSNAP_DF_TEXMAT0   0x00000004u  /* texture matrix 0 was recorded  */
#define SL_BGSNAP_DF_TEXMAT1   0x00000008u
#define SL_BGSNAP_DF_UV1       0x00000010u  /* a second texcoord set is bound */

/* ---- the on-disk sizes, in bytes ---------------------------------------
 *
 * Stated as constants rather than as sizeof a struct: the file layout must not
 * depend on a compiler's padding or alignment choices, and both sides compute
 * offsets from these. */
#define SL_BGSNAP_HEADER_BYTES   256u
#define SL_BGSNAP_DRAW_BYTES     320u
/* pos 12 + col 4 + uv0 8 + uv1 8 + clip 16 + fog 4 + zraw 4. The per-vertex
 * field offsets below are the definition of this total, and a static assertion
 * on both sides ties the two together so an added field cannot leave this
 * constant behind - which is how the clamped/unclamped depth pair was added
 * without the record size silently disagreeing with the layout. */
#define SL_BGSNAP_VERTEX_BYTES   56u
#define SL_BGSNAP_INDEX_BYTES    4u

/* ---- the header layout, byte offsets ----------------------------------- */
#define SL_BGSNAP_H_MAGIC        0u     /* u32                               */
#define SL_BGSNAP_H_VERSION      4u     /* u32                               */
#define SL_BGSNAP_H_HEADER_BYTES 8u     /* u32, equals SL_BGSNAP_HEADER_BYTES */
#define SL_BGSNAP_H_FLAGS        12u    /* u32, SL_BGSNAP_F_*                 */
#define SL_BGSNAP_H_DRAWS        16u    /* u32, <= SL_BGSNAP_MAX_DRAWS        */
#define SL_BGSNAP_H_VERTICES     20u    /* u32, <= SL_BGSNAP_MAX_VERTICES     */
#define SL_BGSNAP_H_INDICES      24u    /* u32, <= SL_BGSNAP_MAX_INDICES      */
#define SL_BGSNAP_H_DRAW_OFF     28u    /* u32 byte offset of the draw table  */
#define SL_BGSNAP_H_VERTEX_OFF   32u    /* u32 byte offset of the vertices    */
#define SL_BGSNAP_H_INDEX_OFF    36u    /* u32 byte offset of the indices     */
#define SL_BGSNAP_H_FILE_BYTES   40u    /* u32 total extent the writer wrote  */
#define SL_BGSNAP_H_FRAME        44u    /* u32 the read-frame counter         */
#define SL_BGSNAP_H_READ         48u    /* u32 the read counter               */
#define SL_BGSNAP_H_CLOUD        52u    /* u32 the cloud-phase counter        */
#define SL_BGSNAP_H_DRAW_BYTES   56u    /* u32, equals SL_BGSNAP_DRAW_BYTES   */
#define SL_BGSNAP_H_VERTEX_BYTES 60u    /* u32, equals SL_BGSNAP_VERTEX_BYTES */
#define SL_BGSNAP_H_EXE_SHA      64u    /* 32 bytes, the running binary       */
#define SL_BGSNAP_H_SRC_SHA      96u    /* 32 bytes, the shader text's pin    */
#define SL_BGSNAP_H_RECIPE       128u   /* 96 bytes, NUL-padded ASCII         */
#define SL_BGSNAP_H_FAILURE      224u   /* 28 bytes, NUL-padded ASCII         */
#define SL_BGSNAP_H_INDEX_BYTES  252u   /* u32, equals SL_BGSNAP_INDEX_BYTES  */
/* 252 + 4 = 256 = SL_BGSNAP_HEADER_BYTES.
 *
 * The index element size is declared in the file like the draw and vertex
 * record sizes, so every array in the format is self-describing and a reader
 * never has to assume one of the three. It sits at the end because the fields
 * before it were already placed and a reader of an earlier draft depends on
 * their offsets; the four bytes come out of the failure string, which is a
 * short identifier rather than prose. */
#define SL_BGSNAP_FAILURE_BYTES  28u
#define SL_BGSNAP_RECIPE_BYTES   96u

/* ---- the per-draw record layout, byte offsets within the record --------
 *
 * The uniform set is stored exactly as the draw carried it, as 32-bit
 * patterns: C in the module's own column order, then the scalars. */
#define SL_BGSNAP_D_FLAGS        0u     /* u32, SL_BGSNAP_DF_*                */
#define SL_BGSNAP_D_MESH         4u     /* u32 mesh index within the item      */
#define SL_BGSNAP_D_DMODE        8u     /* u32, SL_BGGPU_DEPTH_*               */
#define SL_BGSNAP_D_FK1          12u    /* u32 pattern                         */
#define SL_BGSNAP_D_FK2          16u    /* u32 pattern                         */
#define SL_BGSNAP_D_FIRST_INDEX  20u    /* u32 into the index array            */
#define SL_BGSNAP_D_INDEX_COUNT  24u    /* u32                                 */
#define SL_BGSNAP_D_SC           28u    /* u32 pattern                         */
#define SL_BGSNAP_D_POST         32u    /* u32 pattern                         */
#define SL_BGSNAP_D_ORG          36u    /* 3 x u32 pattern                     */
#define SL_BGSNAP_D_C            48u    /* 16 x u32 pattern                    */
#define SL_BGSNAP_D_TEXMAT0      112u   /* 16 x u32 pattern                    */
#define SL_BGSNAP_D_TEXMAT1      176u   /* 16 x u32 pattern                    */
#define SL_BGSNAP_D_STATE        240u   /* 16 x u32: the bound-state identity  */
#define SL_BGSNAP_D_STATE_SET    304u   /* u32 bitmask: which words are REAL   */
#define SL_BGSNAP_D_RESERVED     308u   /* 12 bytes, written as zero           */
/* 308 + 12 = 320 = SL_BGSNAP_DRAW_BYTES. */

/* The bound-state identity words, at SL_BGSNAP_D_STATE. Each is a value the
 * forwarded outputs or the raster result depend on, recorded so a comparison
 * can state the state it was made under instead of assuming a default.
 *
 * WHICH WORDS ARE REAL IS A SEPARATE BITMASK, SL_BGSNAP_D_STATE_SET, with bit
 * i set when word i was actually read from the pass. An earlier rule here made
 * an unset word indistinguishable from a recorded zero, and the states that
 * matter most are genuinely zero: blending off, culling disabled, an alpha
 * reference of 0. Under that rule a draw with blending legitimately off looked
 * like a draw whose blend state was never recorded, and the colour and fog
 * comparisons lost their oracle for no reason. The mask separates "not
 * recorded" from "recorded as zero", so a zero state costs nothing. A reader
 * must consult the mask before reading any word, and treat an unset word as
 * absent evidence rather than as a value. */
#define SL_BGSNAP_S_BLEND_ON     0u
#define SL_BGSNAP_S_BLEND_SRC    1u
#define SL_BGSNAP_S_BLEND_DST    2u
#define SL_BGSNAP_S_DEPTH_FUNC   3u
#define SL_BGSNAP_S_DEPTH_MASK   4u
#define SL_BGSNAP_S_FOG_MODE     5u
#define SL_BGSNAP_S_TEXENV0      6u
#define SL_BGSNAP_S_TEXENV1      7u
#define SL_BGSNAP_S_TEX0_NAME    8u
#define SL_BGSNAP_S_TEX1_NAME    9u
#define SL_BGSNAP_S_UNIT1_ON     10u
#define SL_BGSNAP_S_COLOR_ARRAY  11u
/* SL_BGSNAP_S_FOG_ARRAY IS PER DRAW, NOT PER WALK, and it carries three
 * states rather than a boolean.
 *
 * The background pass toggles the fog-coordinate array PER MATERIAL, because
 * the vertex pool is shared: a material whose program consumes the stage-fog
 * contract reads the array, and one that does not is given a coordinate of
 * zero instead (sl_gfx_dl.c:19092-19099, which disables the array and calls
 * glFogCoordf(0.0f) in the same breath). So one walk can issue a fogged draw
 * and an unfogged draw from the same fog pool, and a single walk-level answer
 * is false for at least one of them.
 *
 * The three states exist because the two disabled cases have different
 * evidence behind them, and collapsing them would either invent an oracle or
 * discard one:
 *
 *   0  the array is DISABLED and the current fog coordinate is a KNOWN 0.0f.
 *      The pass sets it at the moment it disables the array, so the forwarded
 *      gl_FogFragCoord for every vertex of this draw is exactly 0.0f - a
 *      justified oracle, and NOT the per-vertex pool value, which this draw
 *      does not read.
 *   1  the array is ENABLED and the draw reads the per-vertex pool, so the
 *      recorded per-vertex fog is the oracle.
 *   2  the array is DISABLED and the current fog coordinate was NOT
 *      established by this pass, so the effective value is unknown and the
 *      oracle is WITHHELD. This is the walk-level-off case, where nothing
 *      here set the current value.
 *
 * A reader must branch on the value and must not read the per-vertex pool as
 * the oracle for state 0 or 2. The mask bit still says whether the word was
 * recorded at all. */
#define SL_BGSNAP_S_FOG_ARRAY    12u
#define SL_BGSNAP_FOG_OFF_ZERO   0u   /* disabled, current fog a known 0.0f  */
#define SL_BGSNAP_FOG_ARRAY_ON   1u   /* enabled, the pool is the oracle     */
#define SL_BGSNAP_FOG_OFF_UNKNOWN 2u  /* disabled, current fog unestablished  */
#define SL_BGSNAP_S_CULL         13u
#define SL_BGSNAP_S_ALPHA_FUNC   14u
#define SL_BGSNAP_S_ALPHA_REF    15u

/* ---- the per-vertex record layout, byte offsets within the record ------
 *
 * The inputs the item holds and the arrays bind, then the two outputs the CPU
 * loop produced for the same vertex. A draw that reads no second texcoord set
 * leaves uv1 zero and does not set SL_BGSNAP_DF_UV1. */
#define SL_BGSNAP_V_POS          0u     /* 3 x u32 pattern, model position     */
#define SL_BGSNAP_V_COL          12u    /* 4 bytes, exactly as the array binds */
#define SL_BGSNAP_V_UV0          16u    /* 2 x u32 pattern                     */
#define SL_BGSNAP_V_UV1          24u    /* 2 x u32 pattern                     */
#define SL_BGSNAP_V_CLIP         32u    /* 4 x u32 pattern, the CPU reference  */
#define SL_BGSNAP_V_FOG          48u    /* u32 pattern, the bound fog value    */
#define SL_BGSNAP_V_ZRAW         52u    /* u32 pattern, the UNCLAMPED z        */
/* 52 + 4 = 56 = SL_BGSNAP_VERTEX_BYTES, asserted at compile time by both
 * sides.
 *
 * WHAT THE TWO DEPTH FIELDS MEAN, because "the clip position" alone is
 * ambiguous and the two readings are different numbers. Measured from the
 * loop (sl_gfx_dl.c:17843-17848): it computes the four clip components, stores
 * the UNCLAMPED z into its own array, and only then applies the single-range
 * far clamp to the z it keeps. So:
 *
 *   SL_BGSNAP_V_CLIP   the clip product, with the SINGLE-RANGE FAR CLAMP
 *                      ALREADY APPLIED to its z component. x, y and w are the
 *                      product's own and are never rewritten.
 *   SL_BGSNAP_V_ZRAW   that same vertex's z BEFORE the clamp.
 *
 * Both are recorded because neither alone makes gl_Position.z comparable for
 * all three depth modes. The expected z for a draw is derived from the draw's
 * own recorded dmode and coefficient pair, exactly as BGG_VS computes it:
 *
 *   SL_BGGPU_DEPTH_SINGLE  clip[2]              (the clamp, already applied)
 *   SL_BGGPU_DEPTH_FAR0    fk1 * clip[3] - fk2  (the first range's pair)
 *   SL_BGGPU_DEPTH_FAR1    zraw                 (the game's range, unclamped)
 *
 * With both fields recorded, all four components of gl_Position have a defined
 * expectation and none needs to be left without an oracle.
 *
 * SL_BGSNAP_V_FOG is the value BOUND TO gl_FogCoord for this vertex - the
 * contents of the fog-coordinate array the draw binds, not a separately
 * derived quantity. That is what makes the fog oracle an identity:
 * `gl_FogFragCoord = gl_FogCoord` forwards it unchanged. A reader may compare
 * it exactly only under that reading, which is why the reading is stated here
 * rather than left to be assumed. */

/* ---- the FIRST of several validation rules -----------------------------
 *
 * THIS MACRO CHECKS THREE COUNTS AGAINST THREE MAXIMA. That is all it does,
 * and the wording matters: an earlier comment here claimed it also proved the
 * arrays fit the declared extent with no product overflowing, which it never
 * did. A reader trusting that would have skipped the extent, offset and
 * overlap checks on the strength of one bounds test - which is the exact class
 * of defect this format's prose warns about, committed in the prose itself.
 *
 * Its one job is to make the multiplication safe: both sides call it BEFORE
 * multiplying a count by a record size, so no size is ever computed from an
 * unchecked count. Because every maximum times its record size is well under
 * 2^32, a count that passes here cannot produce a wrapping product.
 *
 * EVERYTHING ELSE IS STILL REQUIRED, separately, by whoever reads a file: that
 * each declared offset plus its extent neither wraps 32 bits nor runs past the
 * declared file length, that the declared length is not longer than the file,
 * that the three arrays do not overlap each other or the header, that each
 * draw's index range lies inside the index array, and that every index is less
 * than the vertex count. A declared offset is NOT bounded by the maxima above,
 * so offset-plus-extent can wrap even when every count is in range. */
#define SL_BGSNAP_COUNTS_OK(draws, verts, idx)                                \
    ((unsigned) (draws) <= SL_BGSNAP_MAX_DRAWS &&                             \
     (unsigned) (verts) <= SL_BGSNAP_MAX_VERTICES &&                          \
     (unsigned) (idx)   <= SL_BGSNAP_MAX_INDICES)

#endif /* SL_BGSNAP_H */
