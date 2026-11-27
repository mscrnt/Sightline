/**
 * THE BACKGROUND PASS'S RENDERER-STATE PROBE (SL_BG_GLSTATE) - developer-only,
 * DEFAULT OFF, and a diagnostic rather than a change to anything.
 *
 * WHAT IT RECORDS. The GL and host state in force at four labelled moments
 * around one selected background draw: before the GPU clip prepass's program
 * and setup, immediately after the prepass's own vertex draw and transform
 * feedback ended and BEFORE any of it is put back, immediately after it is put
 * back, and immediately before the selected nonempty indexed raster draw with
 * its element binding already made. It reads state; it changes none, repairs
 * none and concludes nothing.
 *
 * WHAT IT IS NOT. It is not a fix, not a candidate, not a performance path and
 * not evidence about a driver. No image, no frame time and no cause follows
 * from its output. An observation that two arms differ in some recorded word
 * localises nothing by itself: the fixed-function current attributes after an
 * array draw are left UNSPECIFIED by the OpenGL 2.1 specification (section
 * 2.8), so a difference in one of them is as consistent with two legal driver
 * choices as with anything else.
 *
 * WHY IT EXISTS, STATED AS A GAP RATHER THAN A HYPOTHESIS. Measured: direct
 * native captures reproduce one blue byte differing at pixel (71,113) - 143 on
 * the original arm against 142 on the arm selected by SL_PERF_OPT=1,
 * SL_BG_GPU_XFORM=1, SL_BG_TF_CLIP=1 and SL_BG_TF_RASTER_CPU=1 - while a replay
 * of the second arm's recorded GL call stream yields 143. Something the replay
 * does not reproduce from recorded calls therefore participates, and the
 * recorded calls are what the existing diagnostics describe. This probe
 * describes the STATE instead. It does not name a cause and nothing here may be
 * read as naming one.
 *
 * IT IS A SEPARATE DIAGNOSTIC FROM THE TRANSFORM-FEEDBACK BUFFER READBACK
 * (SL_BG_TF_DUMP, src/gfx/sl_gfx_bgtf.h), deliberately and in every respect:
 * its own switch, its own selector, its own output and its own one attempt. It
 * never arms that one and never calls into it. The two could not share a
 * selection even if that were wanted: that diagnostic selects AFTER the prepass
 * has run, which is too late to have observed PRE_SETUP, and it is reachable
 * only on draws a prepass pool actually sources - which is the one arm this
 * probe must NOT be restricted to, because the reference arm rasterises from the
 * original CPU positions and leaves that gate false.
 *
 * DEFAULT OFF, AND "OFF" IS A LIST OF THINGS THAT DO NOT HAPPEN. With
 * SL_BG_GLSTATE absent, empty, "0" or malformed in any way: no GL query of any
 * kind is made, no entry point is resolved, nothing is allocated, no file is
 * opened or created, no GL error is consumed, no texture selector is moved, and
 * the pass's own call sequence, draw order, indices and arrays are byte for byte
 * what they are without this probe compiled in. The whole cost of an off launch
 * is one cached integer test at each hook.
 *
 * NOTHING IS READ BACK FROM THE GPU AND NOTHING IS SYNCHRONISED. Only
 * glGetIntegerv, glGetFloatv, glGetPointerv, glIsEnabled and glGetString are
 * called, plus the two GL 1.3 texture selectors through the backend's resolver.
 * There is no glFinish, glFlush, fence, query object, buffer map or buffer,
 * pixel or framebuffer read anywhere in this module - the offline fixture
 * proves that over the whole translation unit at LINK time, because no such
 * symbol exists in the synthetic GL it is built against.
 *
 * glGetError IS NEVER CALLED. Reading it CONSUMES the error state, and a
 * diagnostic that silently swallowed an error the pass was about to observe
 * would corrupt the very run it is recording. A query that wrote nothing is
 * instead detected from the destination itself: every component is pre-filled
 * with a distinct sentinel and compared afterwards, so "unwritten" is a
 * property of the data and not of an error flag this module decided to drain.
 *
 * WHICH IS ALSO WHY AN ENABLE STATE COMES FROM A DESTINATION QUERY. A
 * return-only answer cannot be sentinel-checked, and the value returned for a
 * capability the implementation does not accept is the same false a supported
 * but disabled one returns - the difference lives only in the error flag this
 * module will not read. So the enable state is taken through glGetIntegerv's
 * destination, with the sentinel and a boolean validation, and the return-only
 * answer is recorded BESIDE it under its own status (RETURNED) which can never
 * be read as support. Neither an unwritten nor a non-boolean answer is ever
 * turned into a 0 or a 1. See bgs_q_ena for the two API references.
 *
 * AND WHY A RESTORED SELECTOR IS PROVED RATHER THAN ASSERTED. The traversal
 * moves the two texture selectors and puts them back; issuing that write is the
 * ATTEMPT, and a read-back agreeing with the inherited value is the only
 * evidence it took. A mismatch, an unwritten read-back or one that is not a
 * selectable set leaves the restore UNPROVED, stops any further traversal, and
 * makes the whole capture INCOMPLETE - with the rows already observed kept,
 * because they are what was measured. Nothing is written a second time to force
 * agreement: that would be the state repair this module must never perform.
 *
 * AND THE STOP IS THE PROBE'S, NOT ONE SNAPSHOT'S. An unproved restore raises a
 * latch that lives for the rest of the process. It is not cleared by a new
 * snapshot, a producer stage, a mesh or a walk, because the selector this module
 * left in an unknown state does not become known again by writing more to it.
 * Every later phase therefore refuses BOTH selector domains, carrying the first
 * failure's reason, and issues no selector write, domain decision, sweep or
 * repair attempt at all; the global, client-array and host observations, which
 * write nothing, continue as normal. Whichever phase it was raised in, the
 * capture is INCOMPLETE and nothing is counted as completed. The record carries
 * a `halt` line and a per-phase `halted` marker so a reader can see exactly
 * which moments were taken with the traversal already stopped.
 *
 * THE HOST FLOATING-POINT WORDS ARE READ AT SNAPSHOT ENTRY, before the first
 * resolver or GL call, and again after the queries under a separate label. A
 * driver entry point may leave the rounding mode changed, so a single reading
 * taken at the end would attribute this probe's own disturbance to the labelled
 * moment. No FP mode is ever written.
 *
 * THE CADENCE IS UNCHANGED. No cache miss is forced, no slot key is touched, no
 * prepass is added or omitted, and the selected draw is submitted exactly when
 * it would have been. One draw, in one walk, in one frame is recorded, the
 * attempt is spent for the life of the process, and the record cannot grow.
 *
 * src/game/ must never include this.
 */
#ifndef SL_GFX_BGSTATE_H
#define SL_GFX_BGSTATE_H

#ifdef __sgi
#error "sl_gfx_bgstate.h is native-only; guard the include with #ifndef __sgi"
#endif

/* IS THE PROBE ARMED? SL_BG_GLSTATE=read:frame:cloud:mesh:material:dmode with
 * an optional :item-path-substring, parsed ONCE for the launch.
 *
 * THE GRAMMAR IS THE BUFFER DIAGNOSTIC'S, ON PURPOSE. Each of the six numeric
 * fields is a bounded decimal or a single '*' for any, and dmode additionally
 * must be '*', 0, 1 or 2 (SL_BGGPU_DEPTH_SINGLE / _FAR0 / _FAR1). A validation
 * campaign names one draw across four arms, and two selectors with different
 * grammars for the same draw would be two chances to name different draws.
 *
 * dmode ACCEPTS '*' AND A CAMPAIGN SHOULD USE IT. The probe's draw site is
 * reached on the arm where no prepass runs at all, and the depth a mesh
 * computes is a fact about the range rather than about the prepass - so pinning
 * it is legitimate and is not required.
 *
 * IT FAILS CLOSED. Anything that cannot be read exactly - trailing junk, a
 * missing field, a value that would wrap, a dmode outside the three - leaves
 * the probe OFF and is reported once. Arming on an unreadable selector would
 * record the first draw that happened by, which is the failure a selector
 * exists to avoid.
 *
 * THE PROVENANCE IS REQUIRED TO ARM, not merely recorded if offered:
 * SL_BG_GLSTATE_EXE_SHA and SL_BG_GLSTATE_SRC_SHA must each be exactly 64
 * lowercase hex characters and not all zeros. A record that names no build is
 * not evidence, and an all-zero digest is what an unset field looks like.
 *
 * ARMED IS NOT SELECTED. This answers only whether the launch asked for a
 * record; which walk, which mesh and which draw is still the selector's
 * business. Every hook tests this first because it is one cached integer. */
int sl_bgstate_armed(void);

/* ---- the producer stages ------------------------------------------------ *
 *
 * ONE PREPASS SETUP, AS THE PRODUCER IT IS. A raster draw on this candidate
 * reads a pool some earlier prepass filled, and a run of meshes sharing a slot
 * key is served by ONE prepass - so the thing the recorded raster state has to
 * be linked to is a producer EVENT, not "the prepass that ran most recently
 * anywhere".
 *
 * EVERY FIELD IS THE MODULE'S OWN FACT AT THE CALL, so the probe cannot
 * disagree with sl_gfx_bgtf.c about which producer this is. `reused` is what
 * keeps a cache hit honest: a slot that already carried the key did NO work for
 * this draw, and reporting it as a freshly executed producer would attribute a
 * transform to a draw that never paid for one. A reuse is recorded as a LINK to
 * the earlier event, and when that event was not observed - it ran before the
 * selected walk, or past this probe's bounded producer count - the link says
 * so explicitly and the record is INCOMPLETE rather than silently short. */
struct sl_bgstate_producer {
    int      slot;            /* the slot sl_bgtf_prepass is serving from   */
    unsigned gen;             /* the walk generation in its key             */
    int      dmode;           /* SL_BGGPU_DEPTH_SINGLE / _FAR0 / _FAR1      */
    unsigned k1bits, k2bits;  /* the coefficient key, as BITS               */
    unsigned nverts;          /* the walk's vertex count                    */
    int      reused;          /* 1: an already-filled slot served this draw */
    int      state_only;      /* the prepass's own vertex draw was omitted  */
};

/* PHASE 1, BEFORE THE PREPASS'S PROGRAM AND SETUP. Called with the state the
 * prepass INHERITED - no program made current yet, no uniform written, no
 * discard, no pointer moved, no transform-feedback binding. That is why it
 * cannot be driven from a selection made after the prepass: by then this state
 * no longer exists anywhere to be read.
 *
 * It is offered for every served draw of a matched walk and recorded for the
 * first bounded few; beyond the bound the identity is kept and the snapshot is
 * not, which the record states. */
void sl_bgstate_pre_setup(const struct sl_bgstate_producer *p);

/* PHASE 2, AFTER THE PREPASS'S REAL glDrawArrays AND ITS glEndTransformFeedback
 * AND BEFORE ANY RESTORATION. The program is still current, the indexed
 * transform-feedback binding is still made and rasterizer discard is still on:
 * that is the state this phase exists to carry, and reading it after the
 * restoration - which phase 3 does - cannot recover it. */
void sl_bgstate_post_draw_raw(const struct sl_bgstate_producer *p);

/* PHASE 3, AFTER THE ORDINARY RESTORATION. The transform-feedback binding is
 * dropped, discard is off and program 0 is current again. The pair of phases 2
 * and 3 is what makes "the restoration put back what it changed" a comparison
 * rather than an assertion. */
void sl_bgstate_post_restore(const struct sl_bgstate_producer *p);

/* A CACHE HIT: THIS DRAW WAS SERVED BY AN ALREADY-FILLED SLOT. No prepass ran,
 * so no phase is recorded; what is recorded is the LINK from this draw to the
 * producer event that actually filled the slot, found by its key. A link to an
 * event this probe never observed is an explicit incomplete and never a
 * fabricated producer. */
void sl_bgstate_reuse(const struct sl_bgstate_producer *p);

/* ---- the draw site ------------------------------------------------------ *
 *
 * THE WALK, THE MESH AND THE DRAW ARE THREE SEPARATE GATES, and they are
 * separate because the producer stages happen before the mesh that consumes
 * them is known. The clock tuple arms the WALK, which is what lets phase 1 be
 * captured at all; the full identity selects the MESH; and the record is taken
 * at the one place a run is really submitted, because a bind is not a draw and
 * a mesh whose triangles are all filtered submits nothing. */

/* A BACKGROUND WALK BEGINS, with the draw site's own three clocks. Arms the
 * producer capture when the tuple matches and disarms it otherwise, so a
 * producer from another frame can never be linked to this record. */
void sl_bgstate_begin_walk(unsigned read, unsigned dl_frame, unsigned cloud);

/* A MESH BEGINS. Drops the per-mesh selection and the per-mesh producer link,
 * so neither outlives the mesh it belongs to. */
void sl_bgstate_mesh_begin(void);

/* THE DRAW THE SEAM OFFERS. Every field is a fact the pass already holds where
 * the call is made; nothing is derived inside this module.
 *
 * THE FOUR ARM FLAGS ARE RECORDED AND NONE OF THEM GATES. `pool_source` is
 * false on the reference arm - it rasterises the original CPU positions - and
 * `candidate` is false on the original arm, so gating on either would lose
 * exactly one of the two arms a comparison needs. They are written into the
 * record so a reader can see which arm produced it rather than inferred from
 * the environment.
 *
 * `item` is NOT assumed to be NUL-terminated; `item_bytes` bounds it. */
struct sl_bgstate_site {
    unsigned    read;         /* sl_record_index() at the draw site         */
    unsigned    dl_frame;     /* g_dl_frame - the DRAW-SITE middle counter  */
    unsigned    cloud;        /* g_sky_cloud_phase                         */
    unsigned    pumped;       /* sl_frames_completed() AT THE DRAW SITE -
                               * recorded, never selected on, and never
                               * required to equal the screenshot line's own
                               * later sample of the same mutable counter   */
    const char *item;
    unsigned    item_bytes;
    unsigned    mesh;
    unsigned    material;
    int         dmode;        /* the depth this mesh and range computed     */
    float       k1, k2;       /* this draw's coefficient pair               */
    unsigned    nverts;       /* the item's vertex count                    */
    int         arm_candidate;    /* the walk is on the clip-prepass candidate */
    int         arm_pool_source;  /* a prepass pool is the vertex source      */
    int         arm_cpu_raster;   /* a prepass ran and the ORIGINAL stream
                                   * rasterises (SL_BG_TF_RASTER_CPU)        */
    int         arm_vbo_indices;  /* the indices come from the pass's own
                                   * element buffer rather than host memory  */
};

/* IS THIS MESH THE SELECTED ONE? Answered inside a matched walk only, and
 * without reference to which source the raster will read. A no-match costs no
 * GL call. */
int sl_bgstate_select(const struct sl_bgstate_site *s);

/* PHASE 4, IMMEDIATELY BEFORE THE SELECTED NONEMPTY INDEXED DRAW, with its
 * element binding already made and this run's own index sequence in hand.
 *
 * THIS IS ALSO WHERE THE RECORD IS WRITTEN, because this is the last phase: a
 * record composed earlier could not state whether this phase happened, and a
 * record written later would be written after a draw it claims to precede.
 *
 * `indices` is read for the run's identity - its count and its index range -
 * and the pointer itself is opaque. Nothing is dereferenced beyond that range
 * and no vertex, colour, fog or coordinate array is read at all. */
void sl_bgstate_pre_raster(const unsigned *indices, unsigned count);

/* 0 nothing attempted, 1 a COMPLETE record was sealed, -1 the one attempt was
 * spent and the record is INCOMPLETE or REFUSED. The status text carries the
 * outcome's own words and is "" until an attempt is made. */
int         sl_bgstate_state(void);
const char *sl_bgstate_status(void);

/* THE CENSUS. The rejection reasons are separate numbers because "the clock
 * tuple did not match" and "the mesh did not match" are different facts, and a
 * run that recorded nothing must be able to say which. The query counts are
 * what make "an off launch asks the context nothing" an observable zero rather
 * than a claim. */
struct sl_bgstate_counters {
    unsigned long walks;            /* walks offered while armed            */
    unsigned long walk_match;       /* ... whose clock tuple matched        */
    unsigned long mesh_offers;      /* meshes offered inside a matched walk */
    unsigned long ident_miss;       /* item, mesh, material or depth        */
    unsigned long selected;         /* offers that answered yes             */
    unsigned long late_offers;      /* offers after the attempt was spent   */
    unsigned long producers;        /* producer events seen                 */
    unsigned long producers_bound;  /* ... whose phases were recorded       */
    unsigned long producers_over;   /* ... past the bound: identity only    */
    unsigned long reuse_links;      /* cache hits linked to an event        */
    unsigned long reuse_unlinked;   /* ... whose producer was not observed  */
    unsigned long phases;           /* phase snapshots taken                */
    unsigned long queries;          /* GL state queries made, all kinds     */
    unsigned long rows_unknown;     /* rows no query wrote at all           */
    unsigned long rows_partial;     /* rows a query wrote only part of      */
    /* A DESTINATION ENABLE QUERY THAT WROTE SOMETHING THAT IS NOT A BOOLEAN.
     * It has not answered an enable state, so the row is UNKNOWN and is counted
     * in rows_unknown as well - this number says how many of those were a
     * written non-boolean rather than an unwritten destination, because the two
     * are different facts about the context. */
    unsigned long rows_nonboolean;
    unsigned long domain_refusals;  /* a selector domain could not be swept */
    /* THE RESTORE NUMBERS, AND WHY THERE ARE THREE. `selector_restores` once
     * counted the restore WRITE, which is the attempt and not the evidence, so
     * a probe that left the wrong unit selected still raised it. The three are
     * now distinct: how many restores were issued, how many a read-back PROVED,
     * and how many could not be proved. A swept domain whose restore was not
     * proved makes the capture INCOMPLETE, so `restore_failures` is never
     * compatible with a sealed complete record. A domain that was never moved
     * raises none of the three: there was nothing to put back. */
    unsigned long selector_restore_attempts;
    unsigned long selector_restores;     /* ... that a read-back VERIFIED    */
    unsigned long selector_restore_failures;
    unsigned long captures;         /* attempts claimed                     */
    unsigned long completed;        /* records sealed COMPLETE              */
};
const struct sl_bgstate_counters *sl_bgstate_stats(void);

#endif /* SL_GFX_BGSTATE_H */
