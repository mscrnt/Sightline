/* WRITING ONE BACKGROUND FRAME'S INPUTS - the diagnostic's own interface.
 *
 * Read sl_bgsnap.h first: it defines the file this writes and the boundary the
 * data inherits.
 *
 * WHAT THIS IS FOR. The background pass's clip positions and forwarded vertex
 * outputs can be compared against a driver bit for bit, and for constructed
 * inputs they already have been. What has never been compared is an ACTUAL
 * frame: the inputs came from the probe, not from an item. This records one
 * frame's real inputs and the CPU reference computed from them, so the same
 * comparison can be made offline against real data.
 *
 * IT IS A DIAGNOSTIC AND IT IS OFF. Nothing here is a fix, a default, or a
 * release path. With the selector unset - which is every ordinary run - the
 * whole of this module costs one cached integer test per frame and per draw,
 * and no behaviour changes: no allocation, no file, no extra per-vertex work,
 * and not one different pixel. That property is the reason the gate is a
 * cached read of one environment variable rather than anything richer.
 *
 * WHY THE SELECTOR IS GENERIC. The moment is named by counters the pass
 * already maintains - the read, the frame and the cloud phase - and
 * optionally by a substring of the item's own path. No level name, geometry
 * offset or per-level constant appears here or is accepted by it. A selector
 * that encoded one level would have to be edited for the next one, and
 * per-level hand tuning is what this project's tooling exists to avoid.
 *
 * WHAT IT REFUSES TO DO. It writes once: a second selected frame in the same
 * process is ignored, so a snapshot cannot be silently replaced by a later
 * one. It will not overwrite an existing file. It validates every count
 * against the format's maxima BEFORE multiplying, and if a count is over its
 * maximum, an allocation fails, or a write is short, it records the reason in
 * the header's failure field and does NOT set the complete flag - so a reader
 * refuses the file rather than reading a truncated one as a smaller one.
 *
 * THE DATA IS ROM-DERIVED. It is written only under the ignored diagnostic
 * directory, never into the repository, and never committed. Same boundary as
 * texsources/, texpacks/ and worldassets/: the player brings the source, a
 * local tool writes the derived file, and neither is ours to distribute.
 */
#ifndef SL_BGSNAP_WRITE_H
#define SL_BGSNAP_WRITE_H

struct sl_wa_asset;

/* THE GATE. Returns non-zero only when the selector is set. Reads the
 * environment ONCE into a cached answer, so the ordinary run's cost is one
 * load and one branch; every other entry point below returns immediately when
 * this is zero. */
int sl_bgsnap_armed(void);

/* Is THIS the selected moment, and is nothing written yet? `path` is the
 * item's own path and may be NULL. Returns 0 for every frame of an ordinary
 * run without reading anything else. */
int sl_bgsnap_want(unsigned read, unsigned frame, unsigned cloud, const char *path);

/* Open the collection for one walk. `verts` is the item's vertex count; the
 * count is checked against the format's maximum here, before anything is
 * sized or allocated. Returns non-zero when collection is open. */
int sl_bgsnap_begin(const struct sl_wa_asset *a, unsigned verts,
                    unsigned read, unsigned frame, unsigned cloud);

/* THE GEOMETRY AND THE CPU REFERENCE, captured once per walk immediately
 * after the vertex loop has JOINED, so every array read here is complete and
 * no helper thread is still writing it.
 *
 * THIS IS ALSO THE ONLY MOMENT THE DEPTHS ARE THE LOOP'S OWN. Later stages
 * rewrite clip z per range and per mesh (the first range's coefficient pair,
 * and the per-mesh rezero), so a capture taken after them would record a
 * mutated depth as though the loop had produced it. `clip` and `zraw` are
 * copied here, before any of that.
 *
 * Fog and colour are deliberately NOT taken here: neither is decided yet at
 * this point in the pass. They arrive through sl_bgsnap_effective below. */
void sl_bgsnap_vertices(const struct sl_wa_asset *a, unsigned verts,
                        const float *clip, const float *zraw);

/* THE EFFECTIVE FOG AND COLOUR, captured after the pass has made its final
 * decisions about both - the remaster fog enable and the shot-out-light
 * colour replacement - and therefore recording what the draws actually bind
 * rather than what an earlier point in the pass would have suggested.
 *
 * Recording these at the geometry point gave the wrong answer twice over, and
 * both were measured: the remaster fog flag is still 0 when the loop joins and
 * 1 by the time the draws are issued, so a snapshot claimed no fog while the
 * draws bound a fog array; and the colour replacement had not been chosen yet,
 * so a snapshot asserted the item's own bytes while the draws bound different
 * ones.
 *
 *   `fogc`      the fog-coordinate array, or NULL when none is bound
 *   `colours`   the colour array actually bound, when it is a REPLACEMENT
 *               array rather than the item's own records; NULL means the
 *               item's own vertex colours are bound
 *   `disabled`  non-zero when the colour array is switched off entirely, in
 *               which case no per-vertex colour is bound at all and the
 *               colour oracle is withheld rather than guessed
 *
 * What this establishes is carried into every draw record's state mask, so the
 * draw site does not restate it and cannot disagree with it. */
void sl_bgsnap_effective(const struct sl_wa_asset *a, unsigned verts,
                         const float *fogc, const unsigned char *colours,
                         int disabled);

/* One draw, with the uniform set it carried and the state it was made under.
 * `C` is 16 floats in the module's own column order; `texmat0`/`texmat1` are
 * 16 floats each or NULL; `state` is 16 words and `state_set` the bitmask of
 * which of them were really read. Appends one record, or records a failure if
 * the draw table is full - it does not drop the draw silently. */
void sl_bgsnap_draw(unsigned mesh, unsigned flags, int dmode, float fk1, float fk2,
                    const unsigned *indices, unsigned index_count,
                    const float *C, const float *org, float sc, float post,
                    const float *texmat0, const float *texmat1,
                    const unsigned *state, unsigned state_set);

/* Finish: write the file and close collection. The complete flag is written
 * LAST, after every payload byte, so an interrupted write leaves a file a
 * reader refuses. Safe to call when collection was never opened. */
void sl_bgsnap_end(void);

/* Non-zero while collection is open for this walk. The pass reads this to
 * disable its own vertex-loop reduction for the selected frame, so the
 * snapshot records a whole-item transform rather than the reduced one - the
 * reduction is a performance path whose skipped vertices have no clip position
 * to record. */
int sl_bgsnap_collecting(void);

#endif /* SL_BGSNAP_WRITE_H */
