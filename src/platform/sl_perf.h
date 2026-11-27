/**
 * sl_perf.h - THE ONE PLAYER-FACING PERFORMANCE SELECTION, and the policy
 * that resolves it into a selection for each renderer subsystem.
 *
 * WHAT IT IS. One persisted row, OPTIMIZED RENDERING, shown and edited by both
 * settings views (the front end's OPTIONS -> SETTINGS -> DISPLAY tab and the
 * watch's SIGHTLINE -> GRAPHICS child). Nothing holds a second copy of it: a
 * subsystem asks this policy what the row selects, exactly as a visual
 * provider asks sl_presentation.h what the PRESENTATION row selects.
 *
 * WHAT IT IS NOT. It is not a master switch over everything the performance
 * sprint produced, and it does not reconstruct an older build.
 *
 *   ALWAYS ON, BOTH POSITIONS OF THE ROW. The accepted CPU work is compiled
 *   in and runs with its own eligibility, and this row does not reach it: the
 *   conditional diagnostic statistics, the fill-query workers (AUTO, with the
 *   physical-core / topology / fog / small-item / failure fallbacks), the
 *   background vertex helper, the cached renderer settings, and the older
 *   memo / SSE2 / buffer-object work. OFF does not switch any of them off,
 *   and no worker is started or stopped by this row.
 *
 *   SELECTED BY THE ROW. The skydome pass's indexed submission with the
 *   validated float colour carrier (src/gfx/sl_gfx_skybatch.c); the
 *   background pass's GPU clip computation (below); and the per-vertex CPU
 *   loop reduction that depends on it (src/gfx/sl_gfx_dl.c). OFF is the
 *   original immediate / CPU path for those three, which is what every build
 *   before this row did by default.
 *
 *   v0.4 WHICH GPU CLIP COMPUTATION THE BACKGROUND TAKES, because the row
 *   reaches two modules and the difference is an exact image. A walk the row
 *   admits goes to the AUXILIARY-CONTEXT real transform-feedback prepass on
 *   arm GPU_SOURCE with FENCE (src/gfx/sl_gfx_bgtf.c, sl_bgtf_walk_admit),
 *   whose frames were measured BYTE EXACT against the original renderer's on
 *   the viewpoints recorded for this selection. The vertex transform
 *   (src/gfx/sl_gfx_bggpu.c) is still what sl_perf_bg_xform answers for and is
 *   still what admits a walk at all, but it is reached only when a developer
 *   EXPLICITLY NAMES it (sl_perf_bg_xform_named, below), where it is labelled a
 *   DIAGNOSTIC. The reason is measured and not stylistic: the vertex
 *   transform's frames read ONE BLUE VALUE APART from the original renderer's
 *   at (71,113), 142 against 143, and so does the main-context prepass.
 *
 *   EVERY OTHER WAY OUT OF THE WALK ENDS AT THE CPU CLIP AND RASTER PATH - a
 *   prepass refusal, a selector that took the production selection away, and an
 *   unreadable one. None of them falls to either 142 arm. The first draft of
 *   this selection let them fall to the vertex transform, which put a known
 *   mismatch on the default path and reachable by a typo; src/gfx/sl_gfx_dl.c
 *   carries the corrected chain and the measurement behind it.
 *
 *   AND THE ROW PROMISES AN IMAGE, NOT A FRAME RATE. The exactness above is
 *   what its ON position rests on. TWO relative timing screens exist, on
 *   different binaries: an earlier 1920x1080 screen driven by the developer
 *   selectors on an earlier candidate binary, and a NARROW 640x480 Runway ABBA
 *   screen on the production executable through the row, which reports no
 *   material regression with optimization_verdict NOT_EVALUATED. Neither
 *   proves a speed-up, an absolute frame rate or 1080p60, so no frame rate and
 *   no frame time is promised by this row anywhere. docs/backlog.md B-OPTROW
 *   carries the figures and what is still owed.
 *
 * THE DEFAULT IS ON, AND ONLY IN A PLAYER SESSION. The stored row's default is
 * 1, so an ordinary launch with no environment selects the three above. With
 * an INACTIVE settings store - trace replay, headless health and the matching
 * build never call sl_settings_init - sl_perf_opt answers OFF before reading
 * anything, so the row's default cannot reach a recorded trace and those runs
 * keep the paths they have always taken.
 *
 * BE PRECISE ABOUT WHAT "INACTIVE" DOES AND DOES NOT SILENCE. It silences the
 * ROW. It does NOT silence a subsystem developer switch: an inactive run
 * launched with SL_SKY_BATCH=1 still arms the skydome path, and one with
 * SL_BG_GPU_XFORM=1 still admits the transform, because those switches are
 * read by the resolutions below and not by sl_perf_opt. That is deliberate -
 * it is how a diagnostic is driven through a headless run - and it means an
 * inactive run is on its accepted paths only when no such switch is set, which
 * is the case for every ordinary replay and every matching build.
 *
 * PRECEDENCE. There are two states, and which one a process is in depends on
 * ONE thing: whether the player has explicitly changed the row this launch
 * (sl_perf_opt_set).
 *
 *   BEFORE ANY EXPLICIT CHOICE. A SUBSYSTEM DEVELOPER SWITCH that names a
 *   value exactly - SL_SKY_BATCH, SL_SKY_BATCH_FLOAT_COLOR, SL_BG_GPU_XFORM,
 *   SL_BG_GPU_BYPASS - governs THAT subsystem alone, and selects the
 *   DIAGNOSTIC scope: the admissions and carriers the sprint's experiments
 *   were driven with, including the ones no run has qualified. Where no switch
 *   names a value, the row governs. The switches are parsed once per process
 *   (the environment does not change inside a launch) and are never written.
 *
 *   AFTER AN EXPLICIT CHOICE. The ROW governs every controlled choice - both
 *   enablements and the carrier - and no subsystem switch is consulted again
 *   for the rest of the launch. The carrier becomes PROD, which is the only
 *   carrier the row ever selects.
 *
 * The second state exists because the first one alone was WRONG: with
 * SL_BG_GPU_XFORM=1 or SL_SKY_BATCH=1 inherited, setting the row to OFF left
 * those subsystems running while both menus displayed OFF, and setting it to
 * ON in a session that had inherited SL_SKY_BATCH=1 left the byte carrier in
 * force. A row the player has just set has to be the row that is in force, or
 * the menu is reporting something that is not happening.
 *
 * What survives an explicit choice: SL_PERF_BG_FAR, which is neither an
 * enablement nor a carrier (see sl_perf_bg_far_admit), and nothing else.
 *
 * The row has one launch override of its own, SL_PERF_OPT=0|1, in force until
 * the row is changed - the sl_show_fps rule.
 *
 * HOST-CLEAN, like sl_settings.h and sl_presentation.h: plain ints and
 * const char * only, so src/platform, src/gfx and the offline tests can all
 * include it. src/game must not; the watch view reaches the row through the
 * two extern declarations in src/game/options.c, the sl_show_fps idiom.
 */
#ifndef SL_PERF_H
#define SL_PERF_H

/* ---- the row ------------------------------------------------------------ */

/* The value IN FORCE, 0 or 1: the stored row, overridden for one launch by
 * SL_PERF_OPT=0|1 until the row is changed, and 0 whenever the settings store
 * is inactive. This is what both menus print, so the displayed value is always
 * the value the policy below is resolving. */
int  sl_perf_opt(void);

/* Set the row, 0 or 1. Any other value is refused, and the value persists
 * through the store's own write-on-change.
 *
 * THIS IS THE CALL THAT TAKES CONTROL. From here to the end of the launch the
 * row governs both enablements and the carrier, and no subsystem developer
 * switch is consulted for them again - not SL_SKY_BATCH, not
 * SL_SKY_BATCH_FLOAT_COLOR, not SL_BG_GPU_XFORM, not SL_BG_GPU_BYPASS - nor is
 * the row's own SL_PERF_OPT override. Both menus call this and only this, so
 * what a menu displays after a press is what is in force. */
void sl_perf_opt_set(int on);

/* Is SL_PERF_OPT in force (the row not yet changed)? Diagnostics only. */
int  sl_perf_opt_env(void);

/* ---- what the policy selects, per subsystem ---------------------------- */

/* THE SKYDOME PASS. _on: may the indexed submission run at all. _carrier:
 * which colour carrier a served cloud draw uploads, as the
 * SL_SKYBATCH_FLOAT_* ids of sl_gfx_skybatch.h - BYTE is reachable only
 * through a developer switch, the row always selects PROD. Meaningful only
 * while _on answers 1. */
#define SL_PERF_SKY_CARRIER_BYTE 0
#define SL_PERF_SKY_CARRIER_DIV  1
#define SL_PERF_SKY_CARRIER_PROD 2
int sl_perf_sky_on(void);
int sl_perf_sky_carrier(void);

/* THE BACKGROUND PASS. _xform: may the GPU vertex transform be used.
 * _bypass: may the per-vertex CPU loop be reduced for a walk the transform
 * takes - the reduction is only sound while the transform takes the walk, and
 * the call site keeps that dependency and its repair. */
int sl_perf_bg_xform(void);
int sl_perf_bg_bypass(void);

/* v0.4 DID A DEVELOPER NAME THE VERTEX TRANSFORM, as opposed to the row
 * selecting the background's GPU clip computation?
 *
 * WHY THIS EXISTS, AND IT IS NOT A NEW SWITCH. _xform answers WHETHER the
 * background may use a GPU clip computation; it cannot say WHICH, and the two
 * available ones are not interchangeable. The row selects the exact-image
 * auxiliary prepass; the vertex transform's own frames were measured one blue
 * value apart from the original renderer's at (71,113), 142 against 143. So
 * the renderer needs to tell "the row said yes" from "a developer asked for
 * THIS implementation", and the only thing that can say so is the precedence
 * _xform already resolves. No environment key is added: this reads
 * SL_BG_GPU_XFORM, which _xform reads, through the same one parse.
 *
 * 1 only while SL_BG_GPU_XFORM names exactly "1" AND the player has not set
 * the row this launch. An explicit "0" answers 0 here and turns the whole
 * selection off through _xform, so it never reaches a choice between the two.
 * After an explicit menu choice the ROW governs every controlled choice and no
 * subsystem switch is consulted again - so this answers 0 from that point,
 * which is what stops an inherited variable putting the 142 path back behind a
 * menu that had just been set. Absent, empty or unrecognised: 0.
 *
 * WHAT A 1 MEANS FOR THE PICTURE. The walk draws through the vertex transform,
 * which is a DIAGNOSTIC and a known mismatch. src/gfx/sl_gfx_dl.c states that
 * where the frames are produced. */
int sl_perf_bg_xform_named(void);

/* MAY A TWO-RANGE FAR WALK BE ADMITTED to the GPU transform?
 *
 * WHAT THE ROW DOES TODAY, AND WHY IT IS A GUARD AND NOT A VERDICT. The
 * transform realises the two-range far frame's depth as a uniform pair
 * (SL_BGGPU_DEPTH_FAR0 / _FAR1); docs/backlog.md records those far-depth
 * expressions as source-only, with no run qualifying their clamp or their
 * ranges. Until that qualification exists the ROW refuses such a walk, which
 * draws through the original CPU path - the walk that has always shipped. The
 * refusal is a GUARD over a gap in the evidence, not a finding that the far
 * path is wrong, and it is not how the gap gets closed.
 *
 * NOT A CLAIM ABOUT THE ONE-RANGE CASE. The clip-component campaign exercised
 * the clip components, which are computed BEFORE a depth case is selected, so
 * it covers no depth expression - SL_BGGPU_DEPTH_SINGLE's far clamp included.
 * The one-range case is selected by the row and the two-range pair is guarded;
 * neither is qualified, and both are required rows of the qualification batch.
 *
 * THE ANSWERS, IN ORDER, so the gap CAN be closed by measurement:
 *   SL_PERF_BG_FAR=0|1   names it outright - the arm a qualification run uses
 *                        to exercise the far path with every other selection
 *                        at the row's own values. THE ONLY ONE THAT SURVIVES
 *                        AN EXPLICIT MENU CHOICE;
 *   after a press        the row, which refuses. An inherited SL_BG_GPU_XFORM
 *                        is NOT consulted here once the player has chosen:
 *                        honouring it would let a variable the session
 *                        happened to inherit put the unqualified two-range
 *                        walk back on the GPU behind a menu that had just
 *                        been set;
 *   SL_BG_GPU_XFORM=1    before any press, the sprint's diagnostic scope,
 *                        which admitted the far walk, so a diagnostic taken
 *                        that way still means what it meant;
 *   otherwise            the row, which refuses.
 * When the far expressions are qualified, the refusals are what change; the
 * explicit arm stays as it is. */
int sl_perf_bg_far_admit(void);

/* The census line: the row, its override, and each resolution with the reason
 * it came out that way. Counts and states only; makes no decision. */
const char *sl_perf_why(void);

#ifdef SL_PERF_TEST
/* OFFLINE BUILDS ONLY (-DSL_PERF_TEST). Drop every cached environment answer
 * and the row's launch-override latch, so one executable can drive every
 * combination. Absent from the shipped translation unit, which therefore has
 * no way to re-read an environment it read once. */
void sl_perf_test_reset(void);
#endif

#endif /* SL_PERF_H */
