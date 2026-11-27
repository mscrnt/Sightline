/**
 * The background pass's GPU vertex transform, admitted by the OPTIMIZED
 * RENDERING row (src/platform/sl_perf.h) and reached only for a walk the clip
 * prepass did not take.
 *
 * v0.4 WHAT THE ROW ACTUALLY SELECTS FOR AN ORDINARY SESSION, STATED FIRST
 * BECAUSE THIS MODULE IS NO LONGER IT. A walk the row admits goes to the
 * auxiliary-context transform-feedback clip prepass (src/gfx/sl_gfx_bgtf.c,
 * sl_bgtf_walk_admit), whose frames were measured BYTE EXACT against the
 * original renderer's; THIS program's were measured one blue value apart from
 * them at (71,113), 142 against 143. So sl_bggpu_opt_in is still what the row
 * resolves to and still what admits a walk at all, and the program below runs
 * ONLY WHERE A DEVELOPER EXPLICITLY NAMED IT - SL_BG_GPU_XFORM=1 before any
 * menu press, which sl_perf_bg_xform_named is the answer to. It is a labelled
 * DIAGNOSTIC and a known mismatch there.
 *
 * NOTHING ELSE REACHES IT, AND THAT IS A CORRECTION RATHER THAN A RESTATEMENT.
 * The first draft of the row's selection left this program as the FALL-THROUGH
 * of src/gfx/sl_gfx_dl.c's walk chain, so every walk the prepass did not admit
 * came here - including a launch whose SL_BG_TF_AUX_REAL could not be read.
 * That is a known mismatch reached by typo. The chain now ends in the CPU clip
 * and raster path instead, and a prepass REFUSAL draws the original CPU clip
 * positions for the same reason: falling here would put the differing byte
 * back.
 *
 * WHAT THIS IS. One GLSL vertex program that computes the background item's
 * clip position from the item's own model-space position, instead of the
 * render thread computing it per vertex into an array. It is the same
 * arithmetic in the same order as bg_vertex_range's (sl_gfx_dl.c): the room
 * space `(p * sc - org) * post`, then the pass's clip matrix in the pass's own
 * row order, then the depth the pass's own range writes - the one-range frame's
 * far clamp, or one of the two ranges of a far frame (SL_BGGPU_DEPTH_*).
 * Nothing else about the draw changes -
 * the program is VERTEX ONLY, so the pass's texturing, blend, alpha-test,
 * depth, polygon-offset and fog state stay the fixed functionality the pass
 * already sets, and the fog coordinate and the vertex colour are still the
 * arrays the pass already binds.
 *
 * WHAT IT IS NOT. It is not a measured speedup, and it does not replace the
 * per-vertex CPU loop on its own: the loop still runs for everything the frame
 * consumes on the CPU - the near-cut classification, the fog coordinate, the
 * far-plane reduction, the panorama bins and the coplanar push - unless the
 * dependent reduction (sl_gfx_dl.c) is also selected and this walk is one it
 * can reduce. See docs/backlog.md B-PERF1 for what a later removal needs.
 *
 * WHAT IS AND IS NOT QUALIFIED. NO DEPTH EXPRESSION OF THIS PROGRAM HAS BEEN
 * QUALIFIED - not the two-range pair, and not the one-range clamp either.
 *
 * The clip-component campaign exercised the CLIP COMPONENTS, which the program
 * computes BEFORE any depth case is selected; it stopped there, so it reaches
 * neither SL_BGGPU_DEPTH_SINGLE's far clamp nor SL_BGGPU_DEPTH_FAR0 / _FAR1
 * below. Reading it as covering SINGLE because SINGLE is "the simple case" is
 * the mistake this paragraph exists to prevent: the clamp is an expression the
 * campaign never evaluated.
 *
 * What follows from that is a DIFFERENCE IN TREATMENT, not a difference in
 * evidence. The two-range pair is additionally recorded in docs/backlog.md as
 * source-only, so the row REFUSES a two-range far walk to the original CPU
 * path (sl_perf_bg_far_admit) while that gap stands; the one-range case is
 * selected by the row and is therefore the part of the row's ON position that
 * is live on unqualified arithmetic. Both are listed as required rows of the
 * qualification batch. The refusal is a guard over missing evidence, not a
 * finding, and SL_PERF_BG_FAR=1 is the arm that exercises the far path with
 * everything else at the row's own values.
 *
 * WHY A SEPARATE FILE. It is a self-contained subsystem: its own entry points,
 * its own shader text, its own capability answer. sl_gfx_dl.c owns the
 * rendering predicates, because it is where this frame's facts are.
 *
 * src/game/ must never include this.
 */
#ifndef SL_GFX_BGGPU_H
#define SL_GFX_BGGPU_H

#ifdef __sgi
#error "sl_gfx_bggpu.h is native-only; guard the include with #ifndef __sgi"
#endif

/* One walk's transform, read by the program as uniforms. The fields are the
 * background pass's own locals, under their own names, so the two can be read
 * side by side: `sc` the item scale times the level scale, `post` the
 * visibility factor the folded branch still owes (1.0 on the plain branch,
 * which carries it in the view), `org` the render origin taken out of the
 * space (zero on the plain branch), `C` the clip matrix the pass built, and
 * `fogless` the stage rule that withholds the far clamp. */
struct sl_bggpu_walk {
    float C[16];
    float org[3];
    float sc;
    float post;
    int   fogless;
};

/* IS THE TRANSFORM SELECTED? sl_perf_bg_xform(), asked LIVE: the OPTIMIZED
 * RENDERING row selects it, it is ON by default in a player session and OFF
 * whenever the settings store is inactive (trace replay, headless health, the
 * matching build), and SL_BG_GPU_XFORM=0|1 overrides it for one launch. No
 * walk pays a getenv - src/platform/sl_perf.c parses the switch once and the
 * live part is an index into the settings table - but the answer is NOT frozen
 * at what the process started with, so a caller that needs one coherent answer
 * for a walk must latch it (sl_gfx_dl.c does). */
int sl_bggpu_opt_in(void);

/* The capability answer, computed once with a live context and then cached:
 * every entry point resolved, the program compiled and linked, every uniform
 * located. 0 means the draw uses the existing path.
 *
 * A SELECTION THAT IS OFF IS NOT A CAPABILITY FAILURE. Answering 0 because the
 * row is off leaves the state UNTRIED, so the first walk after the row goes ON
 * resolves and compiles exactly as a process that started ON would; only a
 * real failure (a missing entry point, a program that will not compile or
 * link, a uniform that does not resolve) is cached, and that one is never
 * retried inside the context. An off row therefore costs a default run nothing
 * - no entry point is resolved and no shader is compiled while it stays off -
 * without making OFF permanent. */
int sl_bggpu_ready(void);

/* This walk's uniforms. Leaves no program current, and leaves the depth mode
 * at SL_BGGPU_DEPTH_SINGLE with zero coefficients, so the program's depth
 * state is defined before any draw selects it. 0 means the walk is not
 * eligible after all and must use the existing path. */
int sl_bggpu_use_walk(const struct sl_bggpu_walk *w);

/* WHICH DEPTH A DRAW WRITES. The background pass draws a frame in one depth
 * range, or - when part of the item lies past the game's far plane - in two
 * (sl_gfx_dl.c, #78). The CPU path realises the three cases by rewriting the
 * clip array's z between draws; a program that computes the clip position from
 * model positions cannot read those rewrites, so the case is a uniform and its
 * coefficients are the same sl_wa_far_range pair the rewrite uses.
 *
 *   SINGLE  the one-range frame: the far clamp bg_vertex_range applies.
 *   FAR0    the first range: z = k1 * w - k2, the pass's own blanket rewrite.
 *   FAR1    the game's range: the UNCLAMPED z, which is the per-vertex zraw
 *           the pass keeps and writes back for that range.
 *
 * FAR0's coefficients belong to the MESH, not just the range: a blended mesh
 * takes the blend overlap's pair (SL_WA_FAR_OVERLAP_BLEND) and every other mesh
 * the opaque pair, which is what bg_far_rezero realises on the CPU by rewriting
 * the mesh's own vertices for the length of its draw and putting them back
 * after it. FAR1 and SINGLE read neither coefficient. */
#define SL_BGGPU_DEPTH_SINGLE 0
#define SL_BGGPU_DEPTH_FAR0   1
#define SL_BGGPU_DEPTH_FAR1   2

/* This DRAW's depth, uploaded with the program ALREADY CURRENT (sl_bggpu_bind):
 * glUniform writes the program in use, so this is not a walk-level upload and
 * must not be called with the fixed pipeline bound. Called for every mesh drawn
 * through the program - including the second and later meshes of a run that
 * leaves the program bound, and the same mesh again in the other range -
 * because the uniform follows the mesh and the range, not the binding. */
void sl_bggpu_use_depth(int mode, float k1, float k2);

/* The program current (on != 0) or the fixed pipeline back (on == 0). The
 * caller owns the vertex pointer that goes with it. */
void sl_bggpu_bind(int on);

/* A context's two ends. The SDL backend calls this after it creates a GL
 * context and before it deletes one (sl_gfx_sdl.c), because everything cached
 * here - the program name, the uniform locations, the resolved entry points -
 * belongs to one context. The next eligible walk then asks again, and draws
 * its own clip positions until that succeeds. Makes no GL call. */
void sl_bggpu_invalidate(void);

/* For the census: 0 untried, 1 ready, -1 UNAVAILABLE - a real capability
 * failure, and only that: a selection that is off leaves the state untried
 * (see sl_bggpu_ready). The reason string carries the last refusal's words,
 * including an off selection's, and is "" until something refuses. */
int         sl_bggpu_state(void);
const char *sl_bggpu_why(void);

#endif /* SL_GFX_BGGPU_H */
