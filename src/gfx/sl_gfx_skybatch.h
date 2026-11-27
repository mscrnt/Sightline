/**
 * THE SKYDOME PASS'S INDEXED GPU SUBMISSION, selected by the OPTIMIZED
 * RENDERING row (src/platform/sl_perf.h), which is ON by default in a player
 * session and OFF whenever the settings store is inactive - trace replay,
 * headless health and the matching build. SL_SKY_BATCH=0|1 overrides it for
 * one launch, and SL_SKY_BATCH_FLOAT_COLOR still names a carrier for a
 * diagnostic; neither is needed by a player.
 *
 * WHAT IT REPLACES AND WHAT IT DOES NOT. The skydome pass (sky_draw in
 * sl_gfx_dl.c) submits one glColor4ub, one glTexCoord2f-or-4f and one
 * glVertex4f per INDEX inside glBegin(GL_TRIANGLES), and recomputes the
 * per-vertex shade (sl_wa_sky_shade / sl_wa_sky_shade_frac) and the cloud-plane
 * coordinate (sl_wa_cloud_coord) at every occurrence. A dome's index list
 * references each vertex several times - that is what an indexed mesh is - so
 * the shade and the cast are evaluated once per reference instead of once per
 * referenced vertex.
 *
 * This module prepares each UNIQUE REFERENCED VERTEX of a mesh once per
 * MATERIAL DRAW, uploads those records to a buffer object, and issues one
 * glDrawElements against compact indices cached per asset. Nothing else about
 * the pass moves: the uniform / material / texture / blend state, the texture
 * environment, the aperture, the CPU world-to-clip transform and the D-046
 * horizon fill all stay owned by sky_draw, and no shader is introduced.
 *
 * r398, THE CLOUD-ONLY CANDIDATE. Only CLOUD material draws (the material
 * carries SL_WA_MAT_CLOUD_PLANE) are batched. A PLAIN material draw is
 * DECLINED by design: this module submits nothing for it and keeps the pass
 * open, and sky_draw draws it through its original immediate loop, whose
 * command sequence for that mesh is the default-off one. See sl_skybatch_mesh
 * for the three answers. This changes WHICH draws are batched, not what any
 * batched draw submits: the cloud preparation, upload, pointers and
 * glDrawElements are the r367 revision's, unchanged.
 *
 * WHY ONCE PER MATERIAL DRAW AND NOT ONCE PER PASS. The per-vertex attribute
 * is a pure function of the vertex record, its clip position and the MATERIAL's
 * own state - whether the D-030 atmosphere shade applies, whether the material
 * carries SL_WA_MAT_CLOUD_PLANE, and the cloud layer's phase and eye, which the
 * pass resolves lazily at the first cloud material. Two materials of one dome
 * can therefore want different attributes for the same vertex id, and a cache
 * keyed on the vertex alone would hand the second material the first's values.
 * The preparation is keyed on (mesh, pass) for exactly that reason; the
 * TOPOLOGY - which vertices a mesh references, and in what order - is immutable
 * and is the only thing cached across draws.
 *
 * WHAT IS PRESERVED EXACTLY. The emitted triangle order is the asset's own
 * index order: the compact index at position k names the same original vertex
 * the immediate loop's k-th submission named. The clip position, the colour,
 * the projected texture q and the shaded byte rounding are the immediate
 * loop's expressions, operation for operation, including the truncating cast
 * of `shade * component / 255 + 0.5`. A non-cloud texture coordinate is
 * (u, v, 0, 1), which is what glTexCoord2f(u, v) leaves as the current
 * coordinate; a cloud coordinate is (q0, q1, 0, q2), the same four values
 * glTexCoord4f received.
 *
 * NO MEASURED BENEFIT IS CLAIMED HERE. The counters below report submissions
 * and prepared vertices against the index count the immediate path would have
 * submitted. That is a count of work avoided in the preparation, not a frame
 * time: nothing in this module has been run against a GPU, a window or a
 * frame, so no pixel and no rate has been measured.
 *
 * FAILURE IS DECIDED BEFORE ANY DRAW. sl_skybatch_pass_begin resolves every
 * entry point, validates the asset's topology, creates and sizes every buffer
 * and allocates every CPU array before it answers. A pass that cannot be
 * served answers 0 and draws nothing, so the caller's immediate path runs for
 * the whole pass and no triangle is submitted twice. The reverse order -
 * drawing and then falling back - would duplicate geometry, so it does not
 * exist in this interface.
 */
#ifndef SL_GFX_SKYBATCH_H
#define SL_GFX_SKYBATCH_H

#include <stdio.h>

struct sl_wa_asset;
struct sl_wa_vertex;

/* THE MATERIAL DRAW'S SHADING STATE - the whole of what the immediate loop
 * reads besides the vertex record and its clip position. The caller fills one
 * of these per material draw from the state it already holds, so this module
 * reads no renderer global and asks the game nothing. */
struct sl_skybatch_shade {
    int      atm;              /* D-030: shade as the game's cloud layer     */
    int      scr;              /* D-046: the screen-fraction mode (needs atm)*/
    int      cloud_plane;      /* the material carries SL_WA_MAT_CLOUD_PLANE */
    float    fog[3];           /* atm_fog, 0..255                            */
    float    cloud[3];         /* atm_cloud, 0..255                          */
    float    cen[3];           /* the dome's anchor - directions are from it */
    float    scr_rows[10];     /* sl_wa_sky_screen's rows (scr only)         */
    int      scr_fill[4];      /* ... and its fill flags                     */
    float    cloud_eye[3];     /* the game's eye for the cast (cloud_plane)  */
    float    cloud_height;
    float    cloud_phase;
    unsigned cloud_w, cloud_h;
};

/* ONE PREPARED VERTEX. This is the module's own payload, so its layout is the
 * module's to fix: clip position, colour, texture coordinate, interleaved in
 * the order the three pointers below are set. offsetof is used for the
 * pointers rather than written constants. */
struct sl_skybatch_vtx {
    float         clip[4];
    unsigned char col[4];
    float         st[4];
};

/* IS THE BATCH PATH SELECTED? sl_perf_sky_on(), asked LIVE - the row can move
 * while the game is running, so this answer is not cached here and no caller
 * may cache it either. Off touches no GL, allocates nothing, queries nothing
 * and uploads nothing, and an off answer no longer latches the module
 * unavailable: OFF -> ON re-resolves and re-creates at the next pass. */
int sl_skybatch_on(void);

/* ONE PREPARED VERTEX'S ATTRIBUTES, as the immediate loop computes them.
 * Pure: a function of the vertex record, its clip position and `s` alone, so
 * the offline test can drive it against a transcription of the immediate loop
 * with no GL present. */
void sl_skybatch_attr(const struct sl_wa_vertex *v, const float clip[4],
                      const struct sl_skybatch_shade *s,
                      struct sl_skybatch_vtx *out);

/* OPEN A SKY PASS. `clip` is the pass's CPU clip array, 4 floats per asset
 * vertex, exactly the array the immediate loop indexes.
 *
 * THIS IS THE BOUNDARY AT WHICH A SELECTION CHANGE TAKES EFFECT. The policy is
 * asked exactly once here, for the enablement and the carrier together, and
 * the carrier is latched for the whole pass; nothing between here and
 * sl_skybatch_pass_end asks again. A press therefore cannot split a pass - the
 * pass in flight finishes on the path it opened with - and cannot leave one
 * mesh's 36-byte records described to the draw as 48-byte ones. The float
 * staging an active carrier needs is also acquired here, so a topology cached
 * while the carrier was byte does not refuse every later pass.
 *
 * Answers 1 only when every entry point resolved, the asset's topology is
 * valid and cached, every buffer exists and is sized, every CPU array is
 * allocated, and the inherited GL client-array state has been saved and
 * replaced. Answers 0 otherwise, having drawn nothing and having left GL as
 * it was found. Call sl_skybatch_pass_end if and only if this answered 1. */
int sl_skybatch_pass_begin(const struct sl_wa_asset *a, const float *clip);

/* ONE MATERIAL DRAW, and THREE ANSWERS (r398, the cloud-only candidate).
 *
 * SL_SKYBATCH_SERVED (1): a CLOUD material mesh - `s->cloud_plane` nonzero -
 * had its unique referenced vertices prepared under `s`, uploaded, and drawn
 * with one glDrawElements over its cached compact indices. An EMPTY mesh also
 * answers 1: nothing is drawn either way.
 *
 * SL_SKYBATCH_REFUSED (0): the mesh was NOT drawn and the pass has been
 * closed - the caller must then draw this mesh and every later one through its
 * own immediate path. Nothing is submitted on the 0 answer, so the fallback
 * cannot duplicate a triangle. A call with no pass open, a NULL `s` or an
 * out-of-range mesh answers 0 as well.
 *
 * SL_SKYBATCH_DECLINED (2): a nonempty PLAIN material mesh, which this
 * candidate does not batch by design. Nothing was prepared, uploaded, bound or
 * drawn and no GL call was made; the pass stays OPEN. The caller draws this
 * mesh through its own immediate loop and keeps offering the later meshes. A
 * decline is counted on its own (the `declined` and `declined-vertices`
 * fields), never as a refusal and never as an indexed draw.
 *
 * The literal returns in the implementation are these values; the names exist
 * so the caller's three branches say which answer each one handles. */
#define SL_SKYBATCH_REFUSED  0
#define SL_SKYBATCH_SERVED   1
#define SL_SKYBATCH_DECLINED 2
int sl_skybatch_mesh(unsigned mesh, const struct sl_skybatch_shade *s);

/* CLOSE THE PASS: restore the saved client-array state, the buffer bindings,
 * the client active texture unit and the current colour and texture
 * coordinate, which glDrawElements leaves undefined for every array that was
 * enabled. The current values are put back from the last index of the LAST
 * BATCHED mesh, and only when no declined mesh was drawn after it: a declined
 * mesh's immediate loop defines them itself, so a later restore must not
 * overwrite them (r398). Idempotent, and a no-op once sl_skybatch_mesh has
 * closed the pass itself. */
void sl_skybatch_pass_end(void);

/* THE GL CONTEXT HAS CHANGED. Every buffer name this module owns belonged to
 * the context being left, so none may be used or deleted now: the cache is
 * dropped WITHOUT touching GL. Call it right after a context is created, the
 * sl_bggpu_invalidate idiom - unconditional and cheap, so no opt-in ordering
 * can leave a stale name behind. */
void sl_skybatch_context_invalidate(void);

/* TEARDOWN WITH A CURRENT CONTEXT. Deletes every buffer this module owns and
 * frees its CPU storage. Must run BEFORE the context is deleted, for the same
 * reason sl_gfx_profile_shutdown does: a name can only be deleted while the
 * context that owns it is current. */
void sl_skybatch_shutdown(void);

/* The census line: whether the path was armed, how it resolved, passes,
 * material draws, indices submitted and unique vertices prepared against the
 * index count the immediate path would have submitted. Counts only. r398: it
 * also carries the mesh-level refusals and the deliberate declines (meshes and
 * their index counts), with the array fields of the r386 line printed as
 * literal zeros. A process that armed the path additionally prints ONE
 * terminal `final` line from an atexit handler registered once per process. */
void sl_skybatch_report(FILE *f);

/* ---- THE FLOAT-COLOUR CARRIER -------------------------------------------
 *
 * WHAT IT IS. The colour carrier for the CLOUD draws this module serves. In an
 * active carrier each served cloud mesh uploads 48-byte records (struct
 * sl_skybatch_fvtx) whose colour is four GL_FLOATs instead of four
 * GL_UNSIGNED_BYTEs. Each is built from the prepared 36-byte record of the
 * same slot: the position and the texture coordinate are copied bit for bit,
 * and only the colour's representation changes. The 36-byte record, its byte
 * formatter, the last-index record and the glColor4ub restore at pass end are
 * unchanged.
 *
 * THE THREE CARRIERS. "byte": the 36-byte upload, call for call the r405
 * module's. "div": each byte c becomes the correctly rounded binary32 of
 * c/255. "prod": the correctly rounded binary32 of the product
 * c * fl32(1/255). Both float carriers come from 256-entry tables of bit
 * patterns generated from exact rational arithmetic, so the conversion does no
 * floating-point arithmetic at run time.
 *
 * WHICH ONE IS SELECTED, AND BY WHAT. The OPTIMIZED RENDERING row selects
 * PROD and nothing else: its values were checked channel by channel, every
 * byte, against an independent oracle, and a Cradle capture measured
 * FLOAT_PROD identical to the original byte path. DIV and BYTE are reachable
 * only through SL_SKY_BATCH_FLOAT_COLOR, a developer switch, so no menu
 * position and no default can put an unvalidated carrier on screen. A launch
 * armed by SL_SKY_BATCH=1 with no carrier switch keeps BYTE, which is what
 * such a launch has always uploaded. src/platform/sl_perf.c holds that rule.
 *
 * WHAT IT DOES NOT DO. Plain meshes are still declined. A process whose first
 * pass opens in an active float carrier prints one "#474 skybatch: float
 * colour carrier" line, once.
 */
struct sl_skybatch_fvtx {
    float clip[4];
    float col[4];
    float st[4];
};

#define SL_SKYBATCH_FLOAT_OFF  0
#define SL_SKYBATCH_FLOAT_DIV  1
#define SL_SKYBATCH_FLOAT_PROD 2

/* THE CARRIER THE POLICY RESOLVES, asked live (sl_perf_sky_carrier): 0 byte,
 * 1 div, 2 prod. A PASS uses the value latched at sl_skybatch_pass_begin, not
 * this function, so a selection change cannot land inside one. */
int sl_skybatch_float_mode(void);

/* One RGBA byte colour under `mode`: writes four binary32 values and answers
 * 1 for SL_SKYBATCH_FLOAT_DIV or SL_SKYBATCH_FLOAT_PROD; answers 0 and writes
 * nothing for any other mode. Pure, so the offline suite checks every byte in
 * every channel against an independent oracle. */
int sl_skybatch_float_colour(int mode, const unsigned char col[4], float out[4]);

#ifdef SL_SKYBATCH_TEST
/* ---- offline build only (-DSL_SKYBATCH_TEST) ----------------------------
 * Absent from the shipped translation unit. These exist so the suite can
 * measure two properties from the inside rather than infer them:
 *
 *   the ACTUAL number of sl_skybatch_attr invocations, which is how
 *   "once per unique referenced vertex" is checked as a count rather than
 *   asserted in a comment (reset it immediately before the call under test and
 *   read it immediately after, so only that window is counted);
 *
 *   the unique-list and compact-index CAPACITIES against what the writing pass
 *   actually wrote, which is how the overlapping-range regression proves the
 *   bound holds WITHOUT needing an out-of-bounds write to crash first. */
extern unsigned long sl_skybatch_test_attr_calls;
void sl_skybatch_test_reset_switch(void);
void sl_skybatch_test_capacity(unsigned long *uniq_cap, unsigned long *uniq_used,
                               unsigned long *idx_cap, unsigned long *idx_used);
/* r398: every counter the note, the report and the terminal snapshot print,
 * read from the inside. The two array outputs always answer 0 (no array
 * path); `declined` and `declined_vertices` are the deliberate declines. */
void sl_skybatch_test_counters(unsigned long *passes,
                               unsigned long *idx_draws,
                               unsigned long *idx_vertices,
                               unsigned long *arr_draws,
                               unsigned long *arr_vertices,
                               unsigned long *uniq,
                               unsigned long *pass_refused,
                               unsigned long *mesh_refused,
                               unsigned long *declined,
                               unsigned long *declined_vertices);
/* The float-colour carrier from the inside - drop the POLICY's cached
 * environment answers (both test hooks do the same thing now, because the
 * module caches neither the switch nor the carrier), force the staging
 * allocation to fail, read the staging and the marker count, and drive the
 * checked staging-size function directly. */
void sl_skybatch_test_reset_float_mode(void);
void sl_skybatch_test_fail_float_alloc(int fail);
void sl_skybatch_test_float_state(int *allocated, unsigned long *capacity,
                                  unsigned long *markers);
int  sl_skybatch_test_float_bytes(unsigned long n, unsigned long *bytes);
#endif

#endif /* SL_GFX_SKYBATCH_H */
