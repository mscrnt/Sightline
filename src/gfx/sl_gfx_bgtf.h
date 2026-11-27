/**
 * The background pass's GPU CLIP PREPASS - selected by the OPTIMIZED RENDERING
 * row, and reachable by a developer switch.
 *
 * WHAT THIS IS. A transform-feedback prepass computes the background item's
 * clip positions on the GPU, and the raster draw that follows submits the
 * ORIGINAL fixed-function path (program 0) with those positions as its vertex
 * array and the pass's own RGBA8, UV and fog arrays and indexed order
 * unchanged. The arithmetic is character for character the one
 * sl_gfx_bggpu.c's program computes, which is in turn bg_vertex_range's
 * (sl_gfx_dl.c) in the same order.
 *
 * WHY IT EXISTS. One blue byte differs
 * at pixel (71,113), 143 against 142, created by a single distant-scenery draw
 * (mesh 29 / material 29, FAR1). Every recorded input to that draw matches
 * between the two arms - all 7923 indices, the RGBA bytes, the fog and UV
 * floats, the 18 texture mip uploads - and a CPU float32 reproduction of the
 * captured transform matches all 31692 clip components. The one measured
 * structural difference left is that the original arm rasterises through
 * fixed-function program 0 with a submitted clip4 array, while the optimized
 * arm rasterises through a custom vertex program. This prepass removes that
 * distinction from the raster draw while keeping the transform on the GPU.
 *
 * THE EXACT BACKEND CAUSE IS STILL UNRESOLVED, AND THE SELECTION DOES NOT REST
 * ON KNOWING IT. What is measured is the OUTCOME: the auxiliary GPU_SOURCE arm
 * produced frames byte-identical to the original renderer's on the viewpoints
 * recorded for this selection, while the main-context arm and the vertex
 * program each read one blue value apart there. So this is an exact-image
 * selection built on a measurement, not an explanation - no mechanism is named
 * anywhere in this file, nothing here carries a tolerance, nothing weakens an
 * exact-image gate, and a CPU fallback is a refusal, never coverage.
 *
 * WHY A SEPARATE MODULE RATHER THAN A BRANCH IN sl_gfx_bggpu.c. The existing
 * transform is validated for its clip components and is live behind the
 * OPTIMIZED RENDERING row. Editing it to carry a second, then-unmeasured
 * submission shape would have put an experiment inside the path a default
 * session already
 * takes. This module is reached only through its own developer switch and
 * leaves the other module's behaviour untouched.
 *
 * v0.4 THERE ARE NOW TWO SELECTIONS, AND THEY REACH DIFFERENT ARMS. The one
 * below is the MAIN-CONTEXT prepass, which is reachable only by name and is a
 * KNOWN MISMATCH: its frames read one blue value apart from the original
 * renderer's at (71,113), 142 against 143. What an ordinary session selects is
 * the AUXILIARY-CONTEXT real prepass on arm GPU_SOURCE with FENCE, whose frames
 * were measured BYTE EXACT there - see THE PRODUCTION SELECTION below and
 * sl_bgtf_walk_admit. Nothing on this file's default path may reach the
 * main-context arm by absence, by default or by a typo.
 *
 *   SL_BG_TF_CLIP=1            the developer route: every eligible walk, every
 *                              main-context diagnostic and every A/B arm,
 *                              whatever the OPTIMIZED RENDERING row says.
 *   the row, with nothing named  the production route: the auxiliary GPU_SOURCE
 *                              arm with FENCE, resolved per walk by the caller.
 *   SL_BG_TF_CLIP=0            a developer taking the prepass out of a player
 *                              session without moving the row.
 *
 * WHAT IS QUALIFIED AND WHAT IS NOT. The production arm's EXACTNESS is measured:
 * the auxiliary GPU_SOURCE arm under FENCE produced frames byte-identical to the
 * original renderer's on the Runway, Frigate and Facility viewpoints recorded
 * for this selection, including the 1920x1080 witnesses. NO FRAME TIME IS
 * CLAIMED BY ANY OF IT, and no frame rate is promised: the relative timing
 * screen taken beside those captures passed, and the original renderer and this
 * arm BOTH missed the absolute p99 target it was taken beside. No depth
 * expression of this path has been qualified on a GPU either. The offline
 * fixtures exercise this module's own logic under a synthetic GL, which proves
 * what it calls and in what order, and proves nothing about a driver.
 *
 * src/game/ must never include this.
 */
#ifndef SL_GFX_BGTF_H
#define SL_GFX_BGTF_H

#ifdef __sgi
#error "sl_gfx_bgtf.h is native-only; guard the include with #ifndef __sgi"
#endif

/* The walk's transform. The same fields and the same meanings as
 * sl_gfx_bggpu.h's struct, and deliberately the same type: both modules
 * compute the identical expression from the identical inputs, and giving the
 * candidate its own near-copy of the struct would let the two drift apart
 * silently. sl_gfx_dl.c fills one of these per walk and hands it to whichever
 * module the walk admitted. */
#include "sl_gfx_bggpu.h"

/* IS THE DEVELOPER SWITCH SET? SL_BG_TF_CLIP=1, parsed once for the launch.
 *
 * THIS IS NOT THE PRODUCTION SELECTION, and it is not the OPTIMIZED RENDERING
 * row. It is the developer route described above: it admits every eligible walk
 * whatever the row says, and it is what keeps the main-context diagnostics and
 * every A/B arm in this header reachable. The switch is read once because a
 * developer selection for one launch has no reason to change inside a run,
 * which also means no walk pays a getenv. 0 is the answer whenever the variable
 * is absent, empty or not "1".
 *
 * AND 0 IS NO LONGER THE WHOLE ANSWER TO "IS A PREPASS SELECTED?". An absent
 * switch leaves the choice to the row, so a caller asking whether THIS WALK
 * takes a prepass asks sl_bgtf_walk_admit; this one answers only whether the
 * developer named the switch. The exact text "0" is a developer naming the
 * prepass OFF for the launch and takes the production selection away with it;
 * an unrecognised value asked for nothing and leaves the row to decide, exactly
 * as absence does. */
int sl_bgtf_opt_in(void);

/* v0.4 DOES THIS WALK TAKE THE PREPASS? THE ONE PER-WALK ADMISSION ANSWER.
 *
 * 1 when the developer switch is set, and 1 when the PRODUCTION selection is in
 * force - the OPTIMIZED RENDERING row admitted this walk and no selector named
 * an arm, an OFF token, a main-context diagnostic or a capture. 0 otherwise, and
 * a 0 walk draws through the fully produced CPU clip and raster path.
 *
 * WHERE A 0 WALK ACTUALLY GOES IS THE CALLER'S CHAIN, NOT THIS ANSWER, and the
 * distinction is worth stating because getting it wrong once put a known
 * mismatch on the default path. This function says only that no prepass takes
 * the walk. src/gfx/sl_gfx_dl.c then draws the CPU clip positions, UNLESS a
 * developer has explicitly named the background vertex transform
 * (sl_perf_bg_xform_named) - which is a labelled DIAGNOSTIC and itself one blue
 * value apart from the original renderer. No absence, default or unreadable
 * value reaches either 142 arm; see that chain for the guarantee.
 *
 * THE ROW IS THE CALLER'S TO RESOLVE, ONCE PER WALK. src/gfx/sl_gfx_dl.c latches
 * one answer from sl_bggpu_opt_in() for the walk and calls this inside it;
 * nothing in this module reads or caches the row. That is what makes a menu
 * moving OFF -> ON -> OFF act at walk boundaries, and what keeps one walk's
 * reduction, admission and raster about ONE answer - a second live read here
 * could disagree with the caller's across a press.
 *
 * IT PROMOTES THE PRODUCTION ARM ON THE FIRST WALK THAT ASKS, which is why an
 * INACTIVE settings store needs no special case anywhere in this module: the row
 * answers OFF there before it reads anything, so the caller never asks, nothing
 * is promoted, no auxiliary context is created and no line is printed. A trace
 * replay, a headless health run and the matching build are byte for byte the
 * runs they were.
 *
 * A LAUNCH THAT FAILED CLOSED STILL ANSWERS 1. An unreadable SL_BG_TF_AUX_SYNC
 * on a production launch refuses the arm and every served draw goes to the
 * EXISTING CPU clip and raster path - the fully produced original positions. The
 * walk is admitted so that those draws travel through that one refusal; it is
 * never the main-context prepass and never the vertex program. */
int sl_bgtf_walk_admit(void);

/* v0.4 IS THE PREPASS IN THIS LAUNCH BY THE ROW RATHER THAN BY THE SWITCH?
 *
 * 1 once a walk the row admitted has put the auxiliary GPU_SOURCE arm in force.
 * 0 on a developer-switch launch - which reaches the identical arm by identical
 * commands, and is a different thing for a reader to be looking at - 0 on a
 * launch whose environment took the production selection away, 0 on one that
 * failed closed and reached no arm, and 0 while no walk has been admitted.
 *
 * PURE: IT DECIDES NOTHING AND PRINTS NOTHING. A census reads it, and a census
 * runs on launches that drew nothing, so asking it must not be what arms a
 * selection. */
int sl_bgtf_production(void);

/* IS THE ORIGINAL-RASTER CONTROL SELECTED? SL_BG_TF_RASTER_CPU=1, parsed once.
 *
 * WHAT A SELECTED LAUNCH DOES. An admitted walk still runs the prepass for
 * every eligible mesh - same depth, same coefficients, same slot keys, same
 * cache and reuse behaviour - and then rasterises from the ORIGINAL CPU
 * position stream rather than the prepass pool. sl_bgtf_bind_clip is NOT
 * called, so the TF position slot is never consumed and `binds` is not
 * incremented; the caller restores its own per-mesh source, which is the only
 * correct order because the prepass leaves the vertex pointer naming the
 * item's MODEL positions.
 *
 * WHAT IT ISOLATES, AND THE MEASUREMENT THAT MOTIVATES IT. The production
 * prepass pool and the CPU clip pool are equal in all 451456 bytes for the draw
 * that creates the one-blue-value defect, and all 31692 indexed components
 * agree, yet the pixel still differs. Equal submitted bytes with a differing
 * output leave the prepass's EXECUTION - program, transform-feedback binding,
 * rasterizer-discard bracket - as a candidate difference that the bytes cannot
 * explain. A selected launch keeps that execution and removes the pool from the
 * raster, so an image equal to the original arm's would place the phenomenon in
 * what the raster CONSUMED, and a differing one would place it at least partly
 * elsewhere. EITHER OUTCOME IS A RESULT; neither is a pass, a failure, a cause
 * or a fix.
 *
 * COVERAGE IS NOT CLAIMED FOR THESE DRAWS. They rasterise the original stream,
 * so sl_bgtf_count_submission is not called for them and they can never read as
 * TF-position-source coverage. The caller keeps its own bounded count of them,
 * which is what a census reads instead.
 *
 * IT SELECTS NOTHING ON ITS OWN: SL_BG_TF_CLIP is what admits a walk, and this
 * only changes what an admitted walk draws from. It is also NOT a performance
 * path - the prepass is paid for and its output discarded - so no frame time
 * from such a launch measures anything. 0 is the default and the answer
 * whenever the variable is absent, empty or not "1". */
int sl_bgtf_raster_cpu(void);

/* IS THE STATE-ONLY CONTROL SELECTED? SL_BG_TF_STATE_ONLY=1, parsed once, AND
 * MEANINGFUL ONLY WITH BOTH SL_BG_TF_CLIP=1 AND SL_BG_TF_RASTER_CPU=1.
 *
 * WHAT A SELECTED LAUNCH CHANGES, STATED AS THE ONE CALL IT DROPS. An admitted
 * draw still runs the whole prepass bracket - the program is made current, the
 * transform and this draw's depth and coefficient uniforms are uploaded, the
 * pool is created and its storage verified, the input position pointer is named,
 * the element binding is dropped, rasterizer discard is enabled, the indexed
 * transform-feedback binding is made, and the begin/end scope is opened and
 * closed - and every one of those is put back exactly as it is without this
 * control. The ONLY omission is the prepass's own glDrawArrays. The raster then
 * reads the ORIGINAL CPU position stream through SL_BG_TF_RASTER_CPU's existing
 * path, which is why that control is required rather than merely compatible.
 *
 * WHAT IT ISOLATES. Measured (r589): a launch under SL_BG_TF_RASTER_CPU alone -
 * the prepass executing, its output never consumed, the raster reading the
 * original positions - still produces the one blue byte at (71,113), 143 against
 * 142. So the difference does not live in which position bytes the raster
 * consumed. What is left inside the prepass is a program and binding transition
 * history AND a vertex draw executed under it. This control holds the first
 * constant and removes the second, so the two can be told apart. EITHER OUTCOME
 * IS AN OBSERVATION; neither is a pass, a failure, a cause or a fix.
 *
 * NO VERTEX DRAW MEANS NO WORK CLAIM. Nothing is transformed and no pool is
 * written, so `prepasses`, `vertices` and `reuses` stay 0 on this arm and the
 * cadence is carried by so_setups, so_omitted and so_reuses instead. An
 * unwritten pool is also REFUSED by sl_bgtf_bind_clip, so a caller that
 * mistakenly offered a setup-only slot cannot rasterise undefined bytes.
 *
 * THE CADENCE IS THE EXISTING ONE. A setup runs once per distinct slot key, at
 * the point a real prepass would have run - not once per mesh - so a run of
 * meshes sharing a depth pays one setup and the second range of a far frame pays
 * one more, exactly as the prepass does. Running it per mesh would have added a
 * second changing variable to a comparison that exists to have one.
 *
 * IT SELECTS NOTHING ON ITS OWN, AND A MISCOMBINATION IS REPORTED ONCE. Set
 * without SL_BG_TF_CLIP=1, or without SL_BG_TF_RASTER_CPU=1, it answers 0 and
 * changes nothing whatever: no walk is admitted that would not have been, no CPU
 * work is skipped, no pool is exposed and the arm that was selected behaves as
 * it does without this variable. Only the exact text "1" selects it; anything
 * else is off, and an unrecognised value is reported once rather than guessed
 * at. NOT A PERFORMANCE PATH: the setup is paid for and no output exists, so a
 * launch under it measures nothing. */
int sl_bgtf_state_only(void);

/* IS THE POSITION-ONLY-INPUT CONTROL SELECTED? SL_BG_TF_POS_INPUT=1, parsed
 * once, MEANINGFUL ONLY WITH BOTH SL_BG_TF_CLIP=1 AND SL_BG_TF_RASTER_CPU=1,
 * and MUTUALLY EXCLUSIVE WITH SL_BG_TF_STATE_ONLY.
 *
 * WHAT A SELECTED LAUNCH DOES. The prepass draw RUNS and its transform-feedback
 * writes happen - that is the difference from the state-only control, which
 * omits the draw and therefore measures no GPU work at all. What changes is the
 * set of CLIENT ARRAYS the draw dereferences: around the glDrawArrays and
 * nothing else, every enabled NON-POSITION client array is turned off, the draw
 * runs, and exactly those arrays are turned back on. The raster that follows
 * still reads the ORIGINAL CPU position stream through SL_BG_TF_RASTER_CPU's
 * existing path.
 *
 * WHAT IT ISOLATES, AND THE MEASUREMENT BEHIND IT. Measured (r594): the
 * state-only arm - the whole setup with no draw - reproduces the ORIGINAL image
 * exactly at all three moments, while the arm that executes the draw differs by
 * the one blue value at (71,113). The prepass program (TF_VS below) reads
 * gl_Vertex and nothing else, yet the draw inherits whatever client arrays the
 * pass had enabled: the background pass enables the position array, unit 0's
 * texture coordinate array, and conditionally the colour and fog-coordinate
 * arrays, and some branches enable unit 1's coordinate array. Those arrays are
 * dereferenced by a draw that has no use for them. This control removes exactly
 * that exposure and keeps everything else - the program, the uniforms, the pool,
 * the position pointer, rasterizer discard, the transform-feedback bracket, the
 * slot keys and the cache cadence - identical.
 *
 * EITHER OUTCOME IS AN OBSERVATION. If the defect persists, unused-array
 * exposure does not account for this case; if the images become exact, this path
 * is implicated and NO general mechanism, driver defect or fix is thereby
 * proven. Nothing here carries a tolerance.
 *
 * WHAT IS SAVED AND PUT BACK, EXACTLY. The ENABLES only, plus the client active
 * texture selection. Disabling a client array does not disturb the pointer, the
 * size, the type, the stride or the buffer binding that array holds, so none of
 * those is read or written and no pointer command is issued: the arrays come
 * back pointing where they pointed. Only arrays that were ON are turned back on
 * - an array that was off stays off, because blind-enabling one would hand the
 * following raster an array the pass never asked for - and the inherited client
 * active unit is restored LAST, after every unit this control selected.
 *
 * EVERY APPLICABLE UNIT, NOT UNIT 0. An enabled coordinate array on any unit is
 * dereferenced by the draw, so the sweep covers [0, GL_MAX_TEXTURE_UNITS) and a
 * context reporting more units than the bounded ceiling REFUSES to isolate
 * rather than sweeping part of the range and leaving the rest to feed the draw.
 *
 * IT FAILS SAFE AND NEVER PARTIALLY. Every capability question is settled before
 * any state is written: if the GL 1.3 client unit selector does not resolve, or
 * the unit count cannot be bounded, the control reports once and the draw runs
 * exactly as it does without this variable. There is no path on which some
 * arrays are disabled and not restored.
 *
 * AMBIGUITY IS REFUSED RATHER THAN RANKED. SL_BG_TF_STATE_ONLY omits the draw
 * and this control changes what the draw reads, so a launch asking for both is
 * asking for two different experiments at once. Both answer 0 and the
 * combination is reported once: neither silently wins. 0 is also the answer
 * whenever the variable is absent, empty or not "1", and an unrecognised value
 * is reported once rather than guessed at. NOT A PERFORMANCE PATH. */
int sl_bgtf_pos_input(void);

/* IS THE PRIVATE-PREPASS-VERTEX-ARRAY CONTROL SELECTED? SL_BG_TF_PRIVATE_VAO=1,
 * parsed once, MEANINGFUL ONLY WITH BOTH SL_BG_TF_CLIP=1 AND
 * SL_BG_TF_RASTER_CPU=1, and MUTUALLY EXCLUSIVE WITH SL_BG_TF_STATE_ONLY,
 * SL_BG_TF_POS_INPUT AND SL_BG_TF_DUMP.
 *
 * WHAT A SELECTED LAUNCH DOES. The prepass setup and its draw are bracketed by a
 * single context-owned vertex array object. The inherited binding is read, the
 * private object is bound, the position array is configured from the SAME input
 * buffer and stride the prepass already uses and is the ONLY array enabled on
 * it, the element binding is dropped INSIDE that bracket, the unchanged prepass
 * draw runs, and the inherited binding is put back - and the restore is read
 * back and only then counted - before the caller's own raster restoration.
 *
 * WHAT IT CHANGES ABOUT THE CALLER'S STATE, EXACTLY, AND THE SCOPE OF THAT
 * CLAIM IS THE PREPASS BRACKET. Without this control the prepass writes the
 * inherited array state directly: glVertexPointer renames the position array and
 * the element binding is set to 0 on whatever array object the caller had
 * current. With it, both of those land on the private object, so ACROSS THE
 * BRACKET the caller's descriptors, enables and element binding are the ones it
 * set. A NONZERO inherited object is preserved as carefully as object 0, because
 * a caller that had its own object current is exactly the case a save that
 * assumed 0 would corrupt.
 *
 * IT IS NOT A WHOLE-LIFECYCLE CLAIM ABOUT THE CALLER'S ARRAY OBJECT, and the
 * existing cleanup around it is deliberately unchanged. sl_bgtf_unbind still
 * clears the element binding at the walk's end exactly as it did before this
 * control existed, so the inherited object's element binding is NOT untouched
 * for the whole of a walk - only for the bracket. Nor is that walk-end clear a
 * demonstrated defect in an ordinary draw: src/gfx/sl_gfx_dl.c admits a walk
 * only when g_bgv_live (:19055 rejects !g_bgv_live before the walk begins), and
 * every admitted draw binds g_bgv_index for itself (:17078) and unbinds it
 * (:17082). This control adds no element write outside its own bracket and
 * changes no existing renderer write.
 *
 * WHAT A VERTEX ARRAY OBJECT DOES AND DOES NOT HOLD, which is why two
 * postconditions are still handled by hand. ARB_vertex_array_object
 * (https://registry.khronos.org/OpenGL/extensions/ARB/ARB_vertex_array_object.txt)
 * encapsulates the legacy and generic array descriptors, their enables and the
 * ELEMENT_ARRAY_BUFFER binding. It does NOT encapsulate the ARRAY_BUFFER
 * binding or the client active texture selection, and it cannot hold a
 * client-memory pointer. So the existing pair that binds the input buffer for
 * the pointer call and unbinds it immediately is unchanged and still load-bearing
 * - the global ARRAY_BUFFER binding is left at 0 exactly as before - and the
 * client selector is untouched here just as it is without this control. NO CLAIM
 * IS MADE that the object isolates the current attributes or all GL state.
 *
 * THE CADENCE IS THE EXISTING ONE. The private position descriptor is refreshed
 * on every real cache MISS, with that miss's own input buffer and stride, at the
 * point the prepass already configures the input. A cache HIT adds no bind, no
 * draw and no setup, and does not force a miss; the slot keys, the output pool
 * selection and the reuse accounting are untouched.
 *
 * IT FAILS SAFE AND SAYS SO, AND "SAFE" IS TWO DIFFERENT ANSWERS. The two core
 * entry points are resolved only on a launch that selected the control.
 *
 *   A KNOWN STATE REFUSES AND FALLS BACK. A missing entry point, an inherited
 *   binding the context will not state, and an object the context will not
 *   allocate all fail BEFORE any binding is written, so whatever the caller had
 *   current is still current. A bind the context does not confirm is answered by
 *   ONE cleanup bind back to the saved value plus ONE confirmation read-back, and
 *   if that read-back CONFIRMS the saved value the state is known again. In every
 *   one of these the setup proceeds exactly as it does without this variable, the
 *   refusal is counted and reported once with the message naming which known
 *   state is in force, and no isolation and no work is claimed.
 *
 *   AN UNKNOWN BINDING IS TERMINAL AND DOES NOT FALL BACK. If the restore at the
 *   end of the bracket is not confirmed, or the cleanup bind above is not
 *   confirmed, then the array object the caller's own descriptor restoration and
 *   the raster after it would read is UNKNOWN. There is no safe fallback from
 *   that: continuing would raster on an unnameable object while a counter
 *   recorded a refusal as though the state were understood. So the process ends
 *   instead - a terminal marker containing REFUSED is printed, the streams are
 *   flushed, and the renderer's existing narrow fatal (sl_fatalf, which exits
 *   nonzero and does not return) is called. NOTHING after that point runs: no
 *   further prepass, no cache publication, no marking of a slot filled or
 *   written, no GL cleanup, no CPU descriptor update and no draw. There is no
 *   retry: a context that did not honour one bind is not more trustworthy on a
 *   second, and a loop would turn an unknown binding into an unbounded one.
 *   `pv_terminal` carries it. THIS IS SCOPED TO THE EXPLICITLY ENABLED
 *   DIAGNOSTIC: it is reachable only from a launch that set all three switches,
 *   a default or off launch cannot reach it, and no other process is affected.
 *
 * glGetError is not called on any path, so no error flag is consumed and no
 * outcome above is inferred from one.
 *
 * THE OBJECT IS CONTEXT-LOCAL. sl_bgtf_invalidate makes NO GL call, because the
 * context may already be unusable, so it only forgets the cached name and the
 * resolved pointers; ordinary context destruction releases the object with
 * everything else. Nothing is deleted or bound in a dead context.
 *
 * IT SELECTS NOTHING ON ITS OWN AND NOT A PERFORMANCE PATH. 0 is the answer
 * whenever the variable is absent, empty or not "1", and an unrecognised value
 * is reported once rather than guessed at. */
int sl_bgtf_private_vao(void);

/* IS THE ZERO-VERTEX-COUNT CONTROL SELECTED? SL_BG_TF_ZERO_COUNT=1, parsed once,
 * MEANINGFUL ONLY WITH BOTH SL_BG_TF_CLIP=1 AND SL_BG_TF_RASTER_CPU=1, and
 * MUTUALLY EXCLUSIVE WITH SL_BG_TF_STATE_ONLY, SL_BG_TF_POS_INPUT,
 * SL_BG_TF_PRIVATE_VAO AND SL_BG_TF_DUMP.
 *
 * WHAT A SELECTED LAUNCH DOES, AS THE ONE NUMBER IT CHANGES. The prepass
 * bracket is kept whole - the program, the uniforms, the pool and its verified
 * storage, the input position pointer at the walk's own buffer and stride, the
 * dropped element binding, rasterizer discard, the indexed transform-feedback
 * binding and the begin/end scope - and restored exactly as it is without this
 * control. The prepass glDrawArrays is still ISSUED, once per real cache miss,
 * with the COUNT 0 in place of the vertex count. Nothing else differs from an
 * ordinary original-raster launch, no further GL call is made and no state is
 * swept.
 *
 * IT IS NOT THE OMITTED-CALL CASE AND THE DISTINCTION IS THE WHOLE POINT.
 * SL_BG_TF_STATE_ONLY omits the call; this one makes it. The two are therefore
 * exclusive, and the condition is on the ARGUMENT rather than on a second code
 * path for the same reason the state-only control's is: a duplicated bracket
 * would be a second thing that could drift, and a branch around the call would
 * turn this arm into the one already measured.
 *
 * A ZERO-COUNT CALL IS NOT A TRANSFORMED PREPASS, AND THE ACCOUNTING SAYS SO.
 * No vertex is submitted and no transform-feedback output is produced, so
 * `prepasses`, `vertices`, `reuses`, `binds` and the submission counts are
 * untouched on this arm and the control's own four numbers carry the cadence
 * instead. The slot takes its KEY, which is what keeps the cache cadence equal
 * to the reference's, and is marked UNWRITTEN - so sl_bgtf_bind_clip refuses it
 * exactly as it refuses a setup-only slot, and no caller can rasterise storage
 * no transform ever filled. A cache HIT adds no draw and no setup.
 *
 * WHAT IT ISOLATES, AND WHAT NEITHER OUTCOME ESTABLISHES. Measured (r594): the
 * whole prepass setup with its vertex draw OMITTED reproduces the ORIGINAL image
 * exactly at all three moments, while an arm that executes the draw differs by
 * one blue value at (71,113); a position-only input (r600) and a private vertex
 * array object (r629) did not remove that difference. What is left between the
 * two is the draw CALL itself. This control issues the call and submits no
 * vertex, so execution with no vertex work can be told apart from execution with
 * it. Neither result names a driver, a mechanism or a fix, and neither qualifies
 * the GPU path, its performance or any integration.
 *
 * AMBIGUITY IS REFUSED RATHER THAN RANKED, AND NOTHING IS HALF-APPLIED. Asked
 * for together with the state-only control, the position-only input or the
 * private vertex array, BOTH stay off and the launch is whichever arm
 * SL_BG_TF_CLIP and SL_BG_TF_RASTER_CPU selected; asked for with the buffer
 * diagnostic set, only this control stands down, because that diagnostic's one
 * attempt and grammar are its own and the combination cannot produce a capture
 * anyway. Set without either coupled switch it answers 0. Every one of those is
 * reported once per launch and counted as a refusal, so a launch that asked for
 * the control and did not get it is never silently half-applied.
 *
 * IT HOLDS NO STATE AND RESOLVES NOTHING. No entry point is resolved, no object
 * is created and no context-local name is cached, so sl_bgtf_invalidate has
 * nothing of this control's to forget. 0 is the answer whenever the variable is
 * absent, empty or not "1", and an unrecognised value is reported once rather
 * than guessed at: an off launch does no GL work for it and prints no marker.
 * NOT A PERFORMANCE PATH - the setup's cost is paid and nothing is drawn, so a
 * launch under this control measures no frame time whatever. */
int sl_bgtf_zero_count(void);

/* ======================================================================== *
 * THE EMPTY-DRAW STATE MODE. SL_BG_TF_EMPTY_STATE=A|B|C|E|R|S|Q, parsed once,
 * DEFAULT OFF, and COUPLED to SL_BG_TF_ZERO_COUNT=1, SL_BG_TF_CLIP=1 and
 * SL_BG_TF_RASTER_CPU=1.
 *
 * WHAT IT SPLITS. The zero-count control issues the prepass glDrawArrays with
 * the count 0 inside the whole prepass bracket, and that empty call reproduces
 * the one-blue-byte difference at (71,113), 143 against 142, at all three
 * recorded moments (r636, accepted r637). Three conditions hold at that call
 * and none of them has been varied: transform feedback is ACTIVE, rasterizer
 * discard is ON, and the current program is the prepass shader. This mode varies
 * them.
 *
 *   T  transform feedback active AT the empty draw
 *   D  rasterizer discard on    AT the empty draw
 *   P  the current program      AT the empty draw: the prepass shader, or 0
 *
 *   Z_KNOWN  T1 D1 shader   the existing zero-count arm. THIS SELECTOR ABSENT
 *   A        T0 D1 shader
 *   B        T1 D0 shader
 *   C        T0 D0 shader
 *   E        T0 D1 program 0
 *
 * Z/A/B/C are the 2x2 feedback-by-discard matrix; A against E compares the
 * program choice with feedback INACTIVE. There is no T1-with-program-0 row,
 * because glUseProgram while transform feedback is active is invalid
 * (https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/
 * glUseProgram.xml), so every row that switches the program is also a T0 row and
 * the table below is the only place that pairing is stated.
 *
 * THE A-TO-E COMPARISON IS CONFOUNDED, AND THAT IS WHAT THE NEXT THREE ROWS ARE
 * FOR. Measured: A, B and C read 142 at (71,113) while E and the original read
 * 143, at all three recorded moments. But E differs from A in three ways at
 * once - a glUseProgram(0) BEFORE the empty draw, program 0 CURRENT at the call,
 * and a glUseProgram back to the prepass program AFTER it - so "E is not A"
 * does not say which of the three carries the difference. R, S and Q separate
 * them by holding T0 D1 and the empty draw fixed and varying only the program
 * switches around the call:
 *
 *   R  T0 D1, 0 then the shader BEFORE the draw, nothing after it
 *      - the SWITCH PAIR happens, and the shader is current AT the call
 *   S  T0 D1, 0 before the draw and LEFT current until the common cleanup
 *      - E without E's post-draw rebind
 *   Q  T0 D1, the shader current AT the call, 0 then the shader AFTER it
 *      - A with a post-draw switch pair added
 *
 * So A, R and Q all draw with the shader current and differ only in where the
 * switch pair sits; E and S both draw with program 0 current and differ only in
 * whether the rebind follows. Nothing here names a cause: the OpenGL 4.6
 * compatibility specification section 10.5 states that the current values of
 * enabled generic vertex attributes are unaffected by the draw, so an
 * indeterminate-attribute account is not available, and no other mechanism is
 * established. The cause of the one blue byte is UNRESOLVED and these rows are a
 * bisection of the command history, not an explanation of it.
 *
 * ONE ENUM AND NOT INDEPENDENT FLAGS. Independent switches would admit
 * combinations that are invalid (a program switch inside an active feedback
 * scope) or already measured, and would let two of them be selected at once. A
 * single selector can name only the rows that exist, and every condition is
 * DERIVED from it by sl_bgtf_es_matrix and sl_bgtf_es_switches - so a census, a
 * fixture and the draw site cannot disagree about what a mode means.
 *
 * ABSENT OR "0" CHANGES NOTHING AT ALL. Every difference below is guarded on
 * the effective mode, so an off launch's GL call sequence - the zero-count arm's
 * included - is the one it has without this selector existing: no extra call, no
 * query, no state read and no census marker. Z_KNOWN is therefore the r636 arm
 * unchanged, which is what lets a fresh Z run be compared against the known
 * REFERENCE images before the other four are interpreted at all.
 *
 * WHAT CHANGES, AND THE SCOPE OF EVERY CHANGE IS THE EMPTY DRAW.
 *
 *   T0 omits glBeginTransformFeedback and glEndTransformFeedback TOGETHER - a
 *   matched pair, because an unmatched one is invalid
 *   (.../gl4/glBeginTransformFeedback.xml). The output allocation, its verified
 *   storage and the indexed GL_TRANSFORM_FEEDBACK_BUFFER binding and its release
 *   are KEPT, so what differs between a T1 and a T0 arm is the active capture
 *   and not the buffer's existence or its binding.
 *
 *   D0 disables rasterizer discard immediately before the empty draw and
 *   re-enables it immediately after, so the common bracket's own enable and
 *   disable are unchanged and the state the caller is handed back at close is
 *   the state it is handed on every other arm.
 *
 *   THE PROGRAM SWITCHES ARE FOUR INDEPENDENT CALLS, each one named by the row
 *   and each one issued in a fixed position: glUseProgram(0) and then
 *   glUseProgram(the prepass program) immediately BEFORE the empty draw, and the
 *   same two, in that same order, immediately AFTER it. A row selects any subset,
 *   and sl_bgtf_es_switches is the only place the subsets are written down.
 *
 *     E  before: 0            after: the prepass program
 *     R  before: 0, program   after: nothing
 *     S  before: 0            after: nothing
 *     Q  before: nothing      after: 0, program
 *
 *   The program and every uniform are set up in the common bracket BEFORE any of
 *   these, so no uniform is ever written while program 0 is current, and a row
 *   with any switch in it is a T0 row, so the program is never changed while
 *   feedback is active.
 *
 *   S IS THE ONE ROW THAT LEAVES PROGRAM 0 CURRENT past the draw, and it does
 *   not leave the caller anything different: the common cleanup's own
 *   glUseProgram(0) runs on every row, so the state handed back at close is the
 *   same one Z_KNOWN hands back. What S omits is the rebind BETWEEN the draw and
 *   that cleanup, which is exactly the one call that separates it from E.
 *
 * THE DRAW IS ALWAYS EXACTLY glDrawArrays(GL_POINTS, 0, 0) AND IS ALWAYS ISSUED.
 * No arm omits it - the omitted-call case is SL_BG_TF_STATE_ONLY and is already
 * measured (r594) - and no arm passes a nonzero count. GL_POINTS also matches
 * the primitive mode a T1 arm's glBeginTransformFeedback was opened with, which
 * that pairing requires.
 *
 * EVERYTHING ELSE IS THE ZERO-COUNT ARM'S. The CPU position, colour, UV, fog and
 * index source; the input buffer and its stride; the output allocation; the slot
 * keys, the generation, the depth cases and the cache cadence; and a cache HIT
 * adding no state change, no draw and no setup. Slots stay UNWRITTEN, so
 * sl_bgtf_bind_clip refuses them and no caller can rasterise storage no
 * transform filled.
 *
 * THE GRAMMAR FAILS CLOSED AND NEVER SUBSTITUTES A MODE. Exactly "A", "B", "C",
 * "E", "R", "S" or "Q" selects; absent, empty and "0" are off and silent.
 * ANYTHING else - a lowercase letter, a letter outside the seven, a number, two
 * letters - is REFUSED by name and the mode stays OFF: guessing which row was
 * meant would run one condition under another's name, which is the one failure a
 * multi-arm comparison cannot absorb. A missing coupled switch and a launch that
 * also requested one of the older exclusive controls are refused the same way,
 * and every refusal is counted once per launch. The rows are MUTUALLY EXCLUSIVE
 * by construction: one selector names one row, so no launch can be two of them.
 *
 * NO OUTCOME HERE PROVES A STATE BIT OR A DRIVER OPERATION. Omitting a
 * begin/end pair, switching discard and switching the program each change the
 * command history as well as the condition they name, so a difference between
 * two arms localises a COMBINATION under this recipe and nothing more. Not a
 * performance path: the setup is paid for and nothing is drawn. */
enum sl_bgtf_es_mode {
    SL_BGTF_ES_OFF = 0,      /* Z_KNOWN - the r636 arm, selector absent */
    SL_BGTF_ES_A   = 1,      /* T0 D1 shader    */
    SL_BGTF_ES_B   = 2,      /* T1 D0 shader    */
    SL_BGTF_ES_C   = 3,      /* T0 D0 shader    */
    SL_BGTF_ES_E   = 4,      /* T0 D1 program 0, 0 before / shader after   */
    SL_BGTF_ES_R   = 5,      /* T0 D1 shader,    0 and shader both before  */
    SL_BGTF_ES_S   = 6,      /* T0 D1 program 0, 0 before / nothing after  */
    SL_BGTF_ES_Q   = 7       /* T0 D1 shader,    0 and shader both after   */
};

/* The EFFECTIVE mode, decided once per launch. SL_BGTF_ES_OFF on any launch
 * that did not select one and on every launch whose request was refused. */
int sl_bgtf_empty_state(void);

/* THE MATRIX, DECODED FROM THE MODE AND FROM NOTHING ELSE.
 *
 * `tf_active`, `discard_on` and `program_tf` are the three conditions that hold
 * AT the empty draw; `program_tf` is 1 for the prepass shader and 0 for program
 * 0. SL_BGTF_ES_OFF answers 1/1/1, which is the zero-count arm's own state, so a
 * caller needs no separate off case. An out-of-range mode answers 1/1/1 too,
 * since that is the state nothing was changed from. Pure: no GL call, no getenv,
 * no state.
 *
 * `program_tf` IS DERIVED FROM THE SWITCHES BELOW rather than stored beside
 * them: the program current at the draw is 0 exactly when the row issues a
 * pre-draw switch to 0 and no pre-draw switch back. A stored copy could disagree
 * with the calls the draw site makes, and a row whose declared condition and
 * issued commands differ is the one defect this split cannot absorb. */
void sl_bgtf_es_matrix(int mode, int *tf_active, int *discard_on,
                       int *program_tf);

/* THE PROGRAM SWITCHES THE ROW ISSUES AROUND THE EMPTY DRAW, decoded from the
 * mode and from nothing else. This is the ONLY place the rows' subsets are
 * written down, and the draw site, the census and the fixtures all read it.
 *
 * Each output is 1 if the row issues that call and 0 if it does not:
 *
 *   pre_zero   glUseProgram(0)                 immediately BEFORE the draw
 *   pre_prog   glUseProgram(the prepass program) ... after pre_zero, before it
 *   post_zero  glUseProgram(0)                 immediately AFTER the draw
 *   post_prog  glUseProgram(the prepass program) ... after post_zero
 *
 * The order inside each pair is fixed - 0 first, then the program - so the four
 * answers name a complete call sequence and not just a set. SL_BGTF_ES_OFF and
 * every out-of-range mode answer 0/0/0/0, which is the zero-count arm's own
 * history: no program call is added. Pure: no GL call, no getenv, no state.
 *
 * NO ROW WITH A SWITCH IN IT IS A T1 ROW. glUseProgram inside an active
 * transform-feedback scope is invalid, so the table pairs every switching row
 * with tf_active 0; a fixture asserts that over this decoder, because the table
 * is where such a row could be introduced. */
void sl_bgtf_es_switches(int mode, int *pre_zero, int *pre_prog,
                         int *post_zero, int *post_prog);

/* The mode's name for a census or a fixture: "OFF", "A", "B", "C", "E", "R",
 * "S", "Q", or "?" for an out-of-range value. Never NULL. */
const char *sl_bgtf_es_name(int mode);

/* ======================================================================== *
 * THE POST-RESET BOUNDARY CONTROL. SL_BG_TF_POST_RESET=POST_PROGRAM0_EMPTY|
 * POST_FINISH|BOTH, parsed once, DEFAULT OFF, and COUPLED to SL_BG_TF_CLIP=1
 * alone.
 *
 * WHERE THE BOUNDARY IS, AND IT IS ONE POINT. Inside sl_bgtf_prepass, AFTER the
 * prepass draw has been issued, AFTER the matched transform-feedback scope has
 * been closed, AFTER the common bracket's own glDisable(GL_RASTERIZER_DISCARD)
 * and glUseProgram(0), after the private-vertex-array bracket has released
 * whatever it held, and BEFORE this function returns to the caller that issues
 * the original raster draw. So at the boundary, by the commands this process
 * made: no feedback scope is open, rasterizer discard is off, program 0 is
 * current, and no indexed transform-feedback binding is in force.
 *
 * WHAT EACH VALUE ISSUES THERE, AND NOTHING ELSE ANYWHERE:
 *
 *   OFF                   nothing. No call, no query, no state read, no census
 *                         marker - the pass is the one it is without this
 *                         selector existing.
 *   POST_PROGRAM0_EMPTY   exactly ONE glDrawArrays(GL_POINTS, 0, 0), under
 *                         program 0, which the common restoration above has
 *                         already made current. NO glUseProgram is issued: the
 *                         program needed is the one already there, so this arm
 *                         adds a draw and not a program history.
 *   POST_FINISH           exactly ONE glFinish.
 *   BOTH                  the empty draw FIRST and then the glFinish, in that
 *                         fixed order, so BOTH is POST_PROGRAM0_EMPTY followed
 *                         by POST_FINISH with each arm's own call at the same
 *                         position relative to the boundary. The order is fixed
 *                         in one place (sl_bgtf_pr_calls' two answers and the
 *                         two statements that read them) rather than being a
 *                         property of a row table.
 *
 * THESE ARE EXPERIMENT ARMS AND NOT A FIX. The one blue byte at (71,113) is
 * UNRESOLVED. Three Runway moments were recorded under the empty-draw rows: the
 * original path and rows E and S read 143 there while the zero-count arm and
 * rows A, R and Q read 142, with the rest of each image equal. A, R and Q issue
 * their empty draw under the prepass program and E and S under program 0. That
 * is a CORRELATION over a command history, not a mechanism: no driver operation
 * and no state bit is established by it, and this control adds a different
 * command at a different point rather than explaining it. Nothing here is a
 * performance path either - the prepass's own cost is unchanged and a
 * synchronisation point costs whatever the driver charges for it.
 *
 * INDEPENDENT OF SL_BG_TF_EMPTY_STATE AND SL_BG_TF_ZERO_COUNT, BY DESIGN. The
 * boundary is outside the empty draw's bracket and is reached on every arm that
 * issues a prepass draw, so this control composes with any empty-draw row and
 * with the arm beneath it, AND applies to the REAL NONZERO transform path -
 * SL_BG_TF_CLIP=1 with neither SL_BG_TF_RASTER_CPU nor SL_BG_TF_ZERO_COUNT -
 * where the prepass transforms every vertex and the pool is bound and
 * rasterised. That is why the coupling is SL_BG_TF_CLIP alone: requiring
 * SL_BG_TF_RASTER_CPU would confine the control to the original-raster arm and
 * make the real path untestable, which is the one path a shipping question is
 * about.
 *
 * AND MUTUALLY EXCLUSIVE WITH THE FOUR OLDER CONTROLS, each for its own reason.
 * Every one of these REFUSES this control rather than half-applying it, and the
 * requested older control is left exactly as its own clauses decided it - this
 * one stands down and reaches into no other control's state.
 *
 *   SL_BG_TF_STATE_ONLY   omits the prepass draw entirely, so the boundary's own
 *                         definition - after an ISSUED prepass draw - has no
 *                         instant to name on that arm. Structural, not an
 *                         attribution worry.
 *   SL_BG_TF_POS_INPUT    narrows the client arrays the prepass draw
 *                         dereferences and restores them before this boundary.
 *                         The two are different experiments on one bracket, and
 *                         a launch running both could not attribute a difference
 *                         to either.
 *   SL_BG_TF_PRIVATE_VAO  gives the prepass setup and its draw their own array
 *                         object, released before this boundary. Same
 *                         attribution problem, and the same answer.
 *   SL_BG_TF_DUMP         reads the prepass pool back BETWEEN the prepass and
 *                         the selected raster draw, which is the same interval
 *                         this control inserts calls into, so the diagnostic's
 *                         placement claim and this boundary would overlap. Only
 *                         THIS control stands down: the diagnostic's grammar,
 *                         its one attempt and its spent-attempt state are its
 *                         own, and turning it off from here would reach into a
 *                         state machine this control has no business in. The
 *                         presence test is the diagnostic's own off test, so no
 *                         selector of its is parsed and no line of its is
 *                         printed from here.
 *
 * THE GRAMMAR FAILS CLOSED AND NEVER SUBSTITUTES AN ARM. Exactly
 * "POST_PROGRAM0_EMPTY", "POST_FINISH" or "BOTH" selects; absent, empty, "0"
 * and exactly "OFF" are off and silent. ANYTHING else - a different case, a
 * leading or trailing space, an abbreviation, a near miss - is REFUSED by name
 * with its text quoted, and the control stays OFF. The tokens are compared
 * whole and nothing is normalised, for the reason SL_BG_TF_EMPTY_STATE's are:
 * choosing an arm on the strength of a guess about what a launch meant turns a
 * multi-arm comparison into one in which an arm may silently be another arm.
 * Every refusal is counted once per launch, where the switches are decided,
 * never per draw.
 *
 * AND IT FAILS CLOSED PER BRACKET TOO, WHICH IS A DIFFERENT GUARD. The boundary
 * runs only when every one of its preconditions is PROVED by the calls this
 * same bracket issued: the prepass draw was issued, the feedback scope was
 * opened and closed or never opened, the common disable of rasterizer discard
 * ran, and the common glUseProgram(0) ran. A bracket that cannot show all four
 * issues NOTHING at the boundary, counts the shortfall as pr_skips and names it
 * once on stderr. Nothing is read back from GL to decide this: every answer is
 * what this process ISSUED, because a glGet here would be a state read the rest
 * of this module deliberately does not make, and a driver's answer is not what
 * the precondition is about. In particular the program is never switched at the
 * boundary and never while a feedback scope is open - the scope being closed is
 * one of the four things the guard requires, and the arm needs no switch at all.
 *
 * NO GL ERROR STATE IS CONSUMED OR CLAIMED. glGetError is not called here on any
 * arm, so an armed buffer diagnostic's own error accounting is untouched and
 * NOTHING in this control is evidence that any call did or did not raise an
 * error. A reader wanting that would have to measure it, and this control does
 * not. */
enum sl_bgtf_pr_mode {
    SL_BGTF_PR_OFF    = 0,   /* the pass unchanged; no call is added    */
    SL_BGTF_PR_EMPTY  = 1,   /* POST_PROGRAM0_EMPTY                     */
    SL_BGTF_PR_FINISH = 2,   /* POST_FINISH                             */
    SL_BGTF_PR_BOTH   = 3    /* BOTH: the empty draw, then the glFinish */
};

/* The EFFECTIVE arm, decided once per launch. SL_BGTF_PR_OFF on any launch that
 * did not select one and on every launch whose request was refused. */
int sl_bgtf_post_reset(void);

/* WHICH OF THE TWO CALLS THE ARM ISSUES AT THE BOUNDARY, decoded from the arm
 * and from nothing else. This is the ONLY place the arms' call sets are written
 * down, and the boundary, the census and the fixtures all read it.
 *
 *   empty_draw   1 if the arm issues glDrawArrays(GL_POINTS, 0, 0)
 *   finish       1 if the arm issues glFinish
 *
 * The ORDER is not an output, because it is fixed: the empty draw precedes the
 * glFinish on the one arm that issues both. SL_BGTF_PR_OFF and every
 * out-of-range value answer 0/0 - the unchanged path, which is what makes a
 * value this module did not produce behave as though nothing were selected
 * rather than running an arm. Pure: no GL call, no getenv, no state. */
void sl_bgtf_pr_calls(int mode, int *empty_draw, int *finish);

/* The arm's name for a census or a fixture: "OFF", "POST_PROGRAM0_EMPTY",
 * "POST_FINISH", "BOTH", or "?" for an out-of-range value. It is the SELECTOR
 * TOKEN for the three selectable arms, so a record and a launch spell the arm
 * the same way. Never NULL. */
const char *sl_bgtf_pr_name(int mode);

/* ======================================================================== *
 * THE NEUTRAL-PROGRAM EMPTY-DRAW CONTROL (SL_BG_TF_NEUTRAL_PROG), DEFAULT OFF.
 *
 * WHAT IT CHANGES, AS THE ONE VARIABLE IT MOVES. On the zero-vertex-count arm
 * (SL_BG_TF_CLIP=1, SL_BG_TF_RASTER_CPU=1, SL_BG_TF_ZERO_COUNT=1) the prepass
 * issues exactly one glDrawArrays(GL_POINTS, 0, 0). SL_BG_TF_EMPTY_STATE's row A
 * issues that call with transform feedback INACTIVE, rasterizer discard ON and
 * the TF PREPASS PROGRAM current. This control issues the same call, at the same
 * point, with the same two conditions, under a SECOND program: a newly linked
 * minimal compatibility vertex shader with NO transform-feedback varyings
 * recorded. One glUseProgram selects it immediately before the call and one
 * selects the ordinary prepass program back immediately after it, and then the
 * ordinary common cleanup runs unchanged. So the only intended difference from
 * row A is WHICH PROGRAM IS BOUND FOR THAT EMPTY DRAW.
 *
 * WHAT MEASUREMENT MOTIVATES IT. r665 measured, over seven arms, that the
 * program current AT the zero-vertex draw decides the one differing pixel: with
 * the TF prepass program current, blue at (71,113) reads 142 against the
 * original path's 143; with program 0 current it reads 143. r669 then measured
 * that an empty draw under program 0 issued AFTER the prepass's boundary, and a
 * glFinish there, leave the 142 in place - so the effect belongs to the draw
 * inside the bracket and not to anything after it. Those two leave one question
 * open that neither can answer: whether the condition is "any linked shader
 * program is current" or specifically "the transform-feedback prepass program is
 * current". A neutral program with no recorded varyings separates them, because
 * it is a linked program that is not that one.
 *
 * NEITHER A FIX NOR A PERFORMANCE PATH, AND NO PIXEL IS CLAIMED. No cause is
 * named for the differing byte; this control adds a second program object, two
 * program switches and a compile to a path whose output is discarded, so a
 * launch under it is slower than every arm by construction and its frame times
 * measure nothing. No screen coordinate and no colour value appears anywhere in
 * its implementation.
 *
 * COUPLED TO THE ZERO-COUNT ARM, AND MEANINGLESS WITHOUT IT. The call this
 * control re-programs is the EMPTY draw, which exists only on an effective
 * zero-count arm, so SL_BG_TF_CLIP=1 and SL_BG_TF_RASTER_CPU=1 are terms of that
 * arm and therefore of this control. Set without the arm in force it answers 0
 * and changes nothing whatever.
 *
 * MUTUALLY EXCLUSIVE WITH EVERY OTHER DIAGNOSTIC ON THAT DRAW, and it stands
 * down alone rather than turning anything else off. SL_BG_TF_EMPTY_STATE varies
 * the state and the program switches at the same call, so the two would each
 * attribute the other's effect to itself. SL_BG_TF_POST_RESET adds calls after
 * the bracket under a guard that requires program 0 to be the last program made
 * current, which this control's restore deliberately is not. SL_BG_TF_STATE_ONLY
 * omits the draw there is no program to bind for; SL_BG_TF_POS_INPUT changes what
 * that draw reads; SL_BG_TF_PRIVATE_VAO changes the object it runs inside;
 * SL_BG_TF_DUMP reads a pool an empty call never wrote and keeps its own one
 * attempt; SL_BG_GLSTATE records the program current at its own phases and would
 * report this control's second program as the prepass's. Each is refused by the
 * RAW request, counted once per launch and named on stderr.
 *
 * THE GRAMMAR IS ONE STRICT SWITCH. Exactly "1" selects; absent, empty and "0"
 * are off and silent; anything else is reported once by name and leaves the
 * control off.
 *
 * AND IT FAILS CLOSED TWICE OVER, which is the property worth reading twice.
 *
 *   - AT CAPABILITY. The neutral program is created, compiled and linked once
 *     per context, lazily, on the first served draw that would use it, and
 *     BOTH releases - glDeleteProgram and glDeleteShader - are resolved BEFORE
 *     anything is created, so that no path can leave an object this control
 *     cannot release. Two objects are made there, a shader and a program, and
 *     every exit after each exists releases it: a failed compile, a program the
 *     context will not hand back and a failed link each free the shader, a
 *     failed link also deletes the program on the spot while the context is
 *     current, and a SUCCESSFUL link flags the shader, which the linked program
 *     then holds until teardown deletes the program. One create, one delete, per
 *     object, on every path. A missing entry point, a shader that
 *     will not compile, a program the context will not hand back, or a link that
 *     fails, REFUSES THE SERVED DRAW: the prepass returns -1, the caller draws
 *     from the CPU as it does on any refusal, the failure is counted and named
 *     once on stderr, and the state latches so nothing is retried per mesh. It
 *     never falls back to issuing the draw under the prepass program, because
 *     that is row A and a run that silently became row A would be reported as a
 *     neutral run.
 *   - AT THE DRAW. The site requires the zero-count arm in force, no empty-draw
 *     row, and none of the three older exclusive controls, all from the answers
 *     this process computed. A served draw that cannot show them issues NOTHING,
 *     counts np_skips and REFUSES, for the same reason: the alternative to
 *     refusing is a draw under a program nobody selected.
 *
 * NO GL STATE IS READ BACK AND NO ERROR STATE IS CONSUMED. The program at the
 * draw is what this process made current, not a glGet; glGetError is not called
 * on any path here, so an armed buffer diagnostic's accounting is untouched and
 * nothing here is evidence about what a driver did with any command. */

/* 1 when the control is in force for this launch, 0 otherwise. Decided once by
 * the one arbiter, together with the arm it refines and the controls it is
 * exclusive with, so a launch cannot be two experiments at once. */
int sl_bgtf_neutral_prog(void);

/* ======================================================================== *
 * THE AUXILIARY-CONTEXT EMPTY-DRAW CONTROL (SL_BG_TF_AUX_EMPTY), DEFAULT OFF.
 *
 * WHAT IT CHANGES, AS THE ONE VARIABLE IT MOVES. On the zero-vertex-count arm
 * (SL_BG_TF_CLIP=1, SL_BG_TF_RASTER_CPU=1, SL_BG_TF_ZERO_COUNT=1) the prepass
 * issues exactly one glDrawArrays(GL_POINTS, 0, 0) on the MAIN context, with
 * transform feedback INACTIVE, rasterizer discard ON and the prepass program
 * current. This control moves that one call to a SECOND, OBJECT-SHARING
 * compatibility context on the same window: the main context performs its
 * existing setup and its existing restoration but does NOT issue the call, and
 * at that exact point the auxiliary context is made current, the SAME shared
 * prepass program is bound there, discard is enabled there, the empty call is
 * issued there, that context's own program and discard state are put back, and
 * the main context is made current again. So the only intended difference from
 * the zero-count arm is WHICH CONTEXT THE EMPTY DRAW IS ISSUED ON.
 *
 * WHAT MEASUREMENT MOTIVATES IT, AND WHAT IS NOT PROVED. r665 measured, over
 * seven arms, that the program current AT the zero-vertex draw decides one
 * differing pixel byte; r669 measured that an empty draw after the bracket and a
 * glFinish there leave it in place; a later nine-image comparison found the two
 * arms differing in exactly one byte in each of three moments, and a run with
 * the shader text and the recorded varyings removed still differed - so the
 * difference does not depend on what the program declares. All of that is
 * CORRELATION. No driver mechanism is established, and this control tests one
 * guess about where the effect lives rather than acting on a known cause: that
 * issuing the call on a different context may leave the main context's
 * fixed-function raster output exact.
 *
 * NEITHER A FIX NOR A PERFORMANCE PATH, AND NO PIXEL IS CLAIMED. No cause is
 * named for the differing byte, no screen coordinate and no colour value appears
 * anywhere in its implementation, and nothing here is a candidate for shipping:
 * a context switch per mesh is slower than every arm by construction, so a
 * launch under it measures no frame time, and sharing BUFFER CONTENTS between
 * contexts - which a real transform-feedback path would need and which this
 * control deliberately does not do, since it consumes no output - is a separate
 * and untested question.
 *
 * COUPLED TO THE ZERO-COUNT ARM, AND MEANINGLESS WITHOUT IT, for the reason the
 * neutral-program control is: the call this control relocates is the EMPTY draw,
 * which exists only on an effective zero-count arm, so SL_BG_TF_CLIP=1 and
 * SL_BG_TF_RASTER_CPU=1 are terms of that arm and therefore of this control.
 * Set without the arm in force it answers 0 and changes nothing whatever.
 *
 * MUTUALLY EXCLUSIVE WITH EVERY OTHER DIAGNOSTIC ON THAT DRAW, and it stands
 * down alone rather than turning anything else off. SL_BG_TF_EMPTY_STATE varies
 * the state and the program switches at the same call; SL_BG_TF_NEUTRAL_PROG
 * changes which program that call runs under; SL_BG_TF_POST_RESET adds calls
 * after the bracket under a guard about the last program made current, which
 * says nothing once the call has moved to another context; SL_BG_TF_STATE_ONLY
 * omits the call there would be nothing to relocate; SL_BG_TF_POS_INPUT changes
 * what it reads and SL_BG_TF_PRIVATE_VAO the object it runs inside, neither of
 * which this control reproduces on the auxiliary context; SL_BG_TF_DUMP reads a
 * pool an empty call never wrote; SL_BG_GLSTATE samples the current program at
 * phases that bracket this draw and would report the main context's state for a
 * call made on another one. Each is refused by the RAW request, counted once per
 * launch and named on stderr.
 *
 * THE GRAMMAR IS ONE STRICT SWITCH. Exactly "1" selects; absent, empty and "0"
 * are off and silent; anything else is reported once by name and leaves the
 * control off.
 *
 * AND IT FAILS CLOSED AT EVERY STEP, WHICH IS THE PROPERTY WORTH READING TWICE,
 * because a diagnostic that fell back to the ordinary path on a failure would
 * report the ORIGINAL arm's picture as this control's result.
 *
 *   - AT CAPABILITY. The auxiliary context is created once per main context,
 *     lazily, on the first served draw that would use it, and only after
 *     glIsProgram has resolved - the one entry point the share check needs - so
 *     no context is created that cannot be verified. Creation leaves the MAIN
 *     context current, which the backend guarantees by switching back
 *     immediately: the platform call that creates a context makes it current.
 *     The capability is then verified rather than assumed: the auxiliary context
 *     is made current, glIsProgram is asked about the prepass program's name,
 *     and the main context is made current again. Anything short of a TRUE
 *     answer with both switches succeeding REFUSES THE SERVED DRAW - the prepass
 *     returns -1, the caller draws from the CPU as it does on any refusal, the
 *     failure is counted under its own name and reported once on stderr, and the
 *     state latches so nothing is retried per mesh. It never falls back to
 *     issuing the call on the main context, because that is the zero-count arm
 *     and a run that silently became that arm would be reported as this one.
 *   - AT THE DRAW. The site requires the zero-count arm in force, no empty-draw
 *     row, and none of the controls this one is exclusive with, all from the
 *     answers this process computed. A served draw that cannot show them issues
 *     NOTHING and counts ax_skips.
 *   - AT THE RESOLVE ON THE AUXILIARY CONTEXT. Entry-point addresses belong to
 *     the context they were resolved on, so the two the bracket needs there are
 *     resolved AFTER that context is current and are never taken from the main
 *     context's set. A missing one refuses: the context is destroyed again with
 *     the main context verified current and the mesh goes to the CPU path.
 *   - AT THE SWITCH TO THE AUXILIARY CONTEXT, when the MAIN context is VERIFIED
 *     current afterwards. That verification is the whole content of the word
 *     "recoverable": GL still reaches the main context, so the bracket's own
 *     restoration runs unchanged, nothing is issued on either context, and the
 *     served draw is refused.
 *   - AT ANY OUTCOME WHERE WHICH CONTEXT IS CURRENT IS UNKNOWN, which is not a
 *     refusal at all. A switch in either direction after which neither context
 *     can be verified current means this process cannot prove where a later
 *     command would land. The control DOES NOT RESTORE, destroys nothing, raises
 *     ax_lost, reports the run INVALID - and TERMINATES THE PROCESS at that
 *     point, without running exit handlers, because refusing the served draw
 *     would only redirect ONE mesh: the caller rasters it with GL, every later
 *     frame draws with GL, the window teardown issues GL and this build
 *     registers exit handlers that issue GL, none of which a latch in the
 *     diagnostic can reach. "No further GL command" is a property of the process
 *     ending, not of a return value.
 *
 * NO BUFFER OUTPUT IS CONSUMED, ON EITHER CONTEXT. The empty call writes
 * nothing, nothing is read back from it, the slot it keys is marked unwritten
 * exactly as the zero-count arm marks it, and no transform-feedback scope is
 * ever opened on either context - so cross-context buffer synchronisation,
 * which is the real question a shipping path would face, is not touched here and
 * is not qualified by anything this control does.
 *
 * NO GL STATE IS READ BACK EXCEPT THE SHARE CHECK, and glGetError is not called
 * on any path here, so an armed buffer diagnostic's accounting is untouched and
 * nothing here is evidence about what a driver did with any command. */

/* 1 when the control is in force for this launch, 0 otherwise. Decided once by
 * the one arbiter, together with the arm it refines and the controls it is
 * exclusive with, so a launch cannot be two experiments at once. */
int sl_bgtf_aux_empty(void);

/* ======================================================================== *
 * THE AUXILIARY-CONTEXT REAL PREPASS (SL_BG_TF_AUX_REAL) - THE SELECTOR IS
 * DEVELOPER-ONLY AND DEFAULT OFF; THE ARM IT NAMES IS WHAT THE ROW SELECTS.
 *
 * v0.4 READ THAT DISTINCTION FIRST, because the rest of this section was
 * written while both halves were developer-only. The KEY below is still
 * developer-only: nothing player-facing sets it, and an absent key names no
 * arm. But arm GPU_SOURCE with FENCE is now the PRODUCTION selection - what an
 * ordinary row-ON walk takes through sl_bgtf_walk_admit - so the commands this
 * section describes are the commands an ordinary session issues, and "an
 * experiment rather than a fix" is true of the SELECTOR's A/B arms and no
 * longer of the arm itself. CPU_SOURCE, the two synchronisation tokens and the
 * OFF tokens remain what they were, and each still requires SL_BG_TF_CLIP=1.
 *
 * WHAT IT IS AND HOW IT DIFFERS FROM THE CONTROL ABOVE. SL_BG_TF_AUX_EMPTY
 * relocates an EMPTY draw - zero vertices, no feedback scope, no output - to a
 * second object-sharing GL context, and so says nothing about whether real work
 * can run there. This mode runs the GENUINE prepass on that second context: the
 * prepass program, the real nonzero vertex count, a matched
 * transform-feedback scope, rasterizer discard, the pass's own input vertex
 * buffer at the item's stride, every uniform the program declares and the
 * indexed output binding - all of it on the auxiliary context, with the main
 * context's own prepass draw omitted exactly as the empty-draw control omits it.
 *
 * THE TWO ARMS, SELECTED BY VALUE. Both run the SAME auxiliary prepass and
 * differ only in what the MAIN context then rasterises:
 *
 *   CPU_SOURCE   the ORIGINAL per-mesh position stream is rasterised, through
 *                the existing original-raster control (SL_BG_TF_RASTER_CPU=1,
 *                which this arm REQUIRES). The transform-feedback pool is
 *                written and never read. What the arm isolates is the effect of
 *                real off-context GPU work ON ITS OWN: the image a frame
 *                produces comes from the same CPU positions it would without
 *                this mode, so a difference cannot be attributed to the pool's
 *                contents.
 *   GPU_SOURCE   the SHARED transform-feedback positions are bound and
 *                rasterised, which is the candidate itself. This arm REQUIRES
 *                SL_BG_TF_RASTER_CPU to be off, because the original-raster
 *                control is precisely what stops the pool being bound.
 *
 * A LAUNCH WHOSE SL_BG_TF_RASTER_CPU DOES NOT MATCH THE ARM IS REFUSED, not
 * silently reinterpreted: the arm names which source it is about, so a mismatch
 * is a launch that cannot say which of the two it is running.
 *
 * THE REAL NONZERO COUNT IS A TERM OF THE MODE. SL_BG_TF_ZERO_COUNT must NOT be
 * in force: that arm's whole content is a zero vertex count, which is the
 * opposite of what this mode measures, so the two are mutually exclusive rather
 * than composable.
 *
 * CONTEXT-LOCAL STATE IS SET EXPLICITLY ON THE AUXILIARY CONTEXT, AND NONE OF
 * IT IS INHERITED. Object NAMES are shared between the two contexts; BINDINGS,
 * enables and container objects are not. OpenGL 4.6 Appendix D ("Shared Objects
 * and Multiple Contexts") states which: buffer and program objects are shared,
 * while "container objects" - vertex array objects and transform feedback
 * objects among them - are NOT shared between contexts. So this mode, on the
 * auxiliary context:
 *
 *   - generates and binds its OWN vertex array object there, because a vertex
 *     array object is a container object and the main context's cannot be used;
 *   - enables the position client array there, because an array enable is
 *     per-context state held in the bound array object;
 *   - binds the shared input buffer and issues its own glVertexPointer there;
 *   - drops the element binding there;
 *   - makes the shared prepass program current there and WRITES EVERY UNIFORM
 *     IT DECLARES there, from values this module cached on the main context, so
 *     no cross-context argument about program state is relied on at all;
 *   - enables rasterizer discard there, discard being per-context;
 *   - binds the shared output buffer to the indexed transform-feedback target
 *     there, which writes the auxiliary context's OWN default transform
 *     feedback object - that object being a container object and so per-context;
 *   - and puts every one of those back to the context's defaults before
 *     switching away. The auxiliary context is this mode's alone and has never
 *     been used for anything else, so its default state is also its entry
 *     state and nothing is read back to discover it.
 *
 * EVERY ENTRY POINT IS RESOLVED ON THE AUXILIARY CONTEXT AND CALLED ONLY THERE.
 * The platform documents that a resolved address belongs to the context it was
 * resolved on; this mode therefore keeps its own set, and a name that does not
 * resolve there is a REFUSAL and never a fall back to the main context's
 * address. GL 1.1 core names are called directly, as elsewhere in this module.
 *
 * THE OBJECTS ARE VERIFIED SHARED RATHER THAN ASSUMED. glIsProgram is asked on
 * the auxiliary context for the prepass program once per context, and glIsBuffer
 * is asked there for the input buffer and for the output pool ONCE PER BRACKET -
 * per bracket because those two names vary with the walk and with the slot, so a
 * once-per-context check could not cover them. A FALSE answer refuses the served
 * draw: a draw under a name that means nothing in that context would be recorded
 * as this mode without being it.
 *
 * SYNCHRONISATION IS CONSERVATIVE, AND THE CHOICE IS DELIBERATE. Appendix D's
 * rules make a shared object's new contents visible in a second context only
 * when the writing commands have COMPLETED and the second context BINDS the
 * object after that completion. This mode therefore:
 *
 *   - issues glFinish on the MAIN context immediately before switching, so
 *     every command that may have written the input buffer has completed;
 *   - binds the input buffer and the output pool FRESHLY on the auxiliary
 *     context, which is the bind-after-completion the rule asks for;
 *   - issues glFinish on the AUXILIARY context after the matched feedback scope
 *     closes, so the transform's writes have completed;
 *   - and, after the verified switch back, re-binds the output pool on the MAIN
 *     context, which is the same rule applied in the other direction.
 *
 * glFinish RATHER THAN A FENCE, STATED AS A COST. A sync object with
 * glWaitSync would be the performant choice and is what a shipping path would
 * use; glFinish is the strongest completion guarantee available without
 * resolving any further entry point on either context, and it is a GL 1.0 core
 * export that needs no per-context pointer to get wrong. THE CONSEQUENCE IS
 * THAT THIS MODE IS NOT A PERFORMANCE PATH AND SUPPLIES NO TIMING EVIDENCE: two
 * full pipeline drains per served mesh are in it by construction, so a frame
 * time from a selected launch measures nothing.
 *
 * NOTHING IS DRAWN FROM UNWRITTEN OUTPUT. The slot's `written` tag is raised
 * only when the whole chain held - the switch was verified, every object
 * verified shared, the draw and the matched scope issued, the completion
 * glFinish issued, and the MAIN context verified current again. Any shortfall
 * leaves it 0, and sl_bgtf_bind_clip refuses an unwritten slot from every
 * caller, so the GPU_SOURCE arm cannot rasterise a pool no transform filled.
 *
 * AND IT FAILS CLOSED, IN THE SAME THREE OUTCOMES THE CONTROL ABOVE USES. A
 * capability, allocation, verification or switch failure with the MAIN context
 * VERIFIED current is a RECOVERABLE refusal: nothing is issued, the bracket's
 * own restoration runs, the served draw goes to the CPU path. A switch in either
 * direction after which neither context can be verified current is the UNKNOWN
 * outcome: no further GL command of any kind is issued, nothing is restored or
 * destroyed, the run is reported INVALID and the process ENDS at the point of
 * loss, through the same tf_ax_lost funnel - because a refusal would redirect
 * one mesh while the caller's raster draw, every later frame, the teardown and
 * this build's exit handlers would all go on issuing GL on an unknown context.
 *
 * OFF IS A LIST OF THINGS THAT DO NOT HAPPEN, AND THE OPT-IN SWITCH DECIDES
 * WHICH LAUNCHES ARE OFF. With SL_BG_TF_CLIP absent or 0 - which is every launch
 * that did not name it - no walk reaches any prepass at all, so with
 * SL_BG_TF_AUX_REAL in any state no GL context is created, no entry point is
 * resolved, no vertex array object is generated, no synchronisation point is
 * added, and the prepass's call sequence, its order and its arguments are
 * byte-for-byte what they are without this mode existing. The whole cost of such
 * a launch is one getenv and one cached integer test per served draw.
 *
 * WITH A PREPASS SELECTED, "ABSENT" IS NO LONGER "OFF": a launch that names no
 * arm takes the GPU_SOURCE arm with FENCE, an explicit "0" or "OFF" keeps the
 * main-context prepass as a labelled diagnostic, and an unreadable value fails
 * the prepass closed to the existing CPU raster path. See THE OPT-IN CANDIDATE
 * below for the measurement that distinguishes those three.
 *
 * v0.4 AND ON A PRODUCTION LAUNCH ONLY ONE OF THOSE THREE IS REACHABLE. The row
 * admits a walk only while no arm, no OFF token and no main-context diagnostic
 * is named (sl_bgtf_walk_admit), so an explicit "0" or "OFF" does not put the
 * row's walks on the main-context prepass - it takes the production selection
 * out of the launch and those walks draw the fully produced CPU clip and
 * raster path (src/gfx/sl_gfx_dl.c, which reaches the background vertex
 * transform only when a developer has explicitly named it). The
 * main-context arm stays reachable with SL_BG_TF_CLIP=1, which is the launch
 * every A/B arm here is written against, and the product cannot reach it at all.
 *
 * NO IMAGE AND NO PERFORMANCE CLAIM FOLLOWS FROM THIS. It is an experiment
 * about whether a real transform-feedback prepass CAN run on a second
 * object-sharing context and feed the main context's rasteriser. No cause is
 * named here for the one blue byte at (71,113). */
enum sl_bgtf_ar_mode {
    SL_BGTF_AR_OFF    = 0,   /* the pass unchanged; nothing is added    */
    SL_BGTF_AR_CPU    = 1,   /* CPU_SOURCE: original positions rastered */
    SL_BGTF_AR_GPU    = 2    /* GPU_SOURCE: the shared pool rastered    */
};

/* The EFFECTIVE arm, decided once per launch by the one arbiter together with
 * every control this mode is exclusive with. SL_BGTF_AR_OFF on any launch that
 * did not select one and on every launch whose request was refused. */
int sl_bgtf_aux_real(void);

/* The arm's name for a census or a fixture: "OFF", "CPU_SOURCE", "GPU_SOURCE",
 * or "?" for an out-of-range value. It is the SELECTOR TOKEN for the two
 * selectable arms, so a record and a launch spell the arm the same way. Never
 * NULL. Pure: no GL call, no getenv, no state. */
const char *sl_bgtf_ar_name(int mode);

/* ---- THE OPT-IN CANDIDATE: WHAT SL_BG_TF_CLIP=1 MEANS ON ITS OWN -----------
 *
 * THE SELECTION. A launch that opts into the background GPU clip prepass and
 * NAMES NO ARM takes the auxiliary-context real prepass on arm GPU_SOURCE with
 * FENCE synchronisation. No new path, call, object or key: the commands are the
 * ones the named arm already issues, and what changes is which of two existing
 * prepasses an unqualified opt-in reaches.
 *
 * THE MEASUREMENT IT RESTS ON, AND ITS BOUNDARY. On one Runway viewpoint at
 * three moments, one machine, one driver, from one seeded save: the auxiliary
 * GPU_SOURCE arm's frames were byte-identical to the original renderer's -
 * blue 143 at (71,113) - under FENCE and under FINISH alike, while the
 * MAIN-CONTEXT prepass produced blue 142 in the same scene. That is why the
 * default is this arm and not that one. It is ONE SCENE on ONE DRIVER: it is
 * evidence for the selection and NOT wider exact-image coverage, and no
 * performance claim accompanies it, because the frame times recorded with it
 * include startup and are not a benchmark.
 *
 * v0.4 IT IS THE DEVELOPER ROUTE TO THE SAME ARM THE ROW NOW SELECTS. This
 * clause is about SL_BG_TF_CLIP=1 with no arm named; the row reaches the
 * identical arm with the identical commands through sl_bgtf_walk_admit, and the
 * two are told apart by sl_bgtf_production rather than by anything either of
 * them issues. With the switch 0, or with it absent and the row off, nothing
 * here is consulted: no further environment read, no GL call, no context, no
 * file.
 *
 * THE MAIN-CONTEXT PREPASS IS KEPT, BY NAME, AS A DIAGNOSTIC. An explicit OFF
 * or 0 selects it and says so where the frames are produced - it is a KNOWN
 * MISMATCH on the scene above, and the A/B comparison the selection rests on
 * needs it reachable. CPU_SOURCE and GPU_SOURCE stay explicit, and FINISH and
 * FENCE stay explicit, so every arm of that comparison is still available.
 *
 * AND THE TWO WAYS IT REFUSES BOTH LAND ON THE EXISTING CPU RASTER PATH, never
 * on the main-context prepass:
 *
 *   - AN UNREADABLE ARM on an opted-in launch fails the prepass CLOSED. Every
 *     served draw is refused to the CPU raster path with the parser's reason and
 *     this module's own line; nothing is issued on either context. A mistyped
 *     selector must not be able to select a known mismatch.
 *   - AN UNREADABLE SYNCHRONISATION MODE does the same, and for both arms: the
 *     candidate's and a NAMED one. A value that is neither FENCE nor FINISH
 *     selects neither, so the arm it would have run is refused rather than
 *     served with a mode the launch did not choose. The refusal is keyed by the
 *     selector's name and never quotes the value.
 *   - A MISSING CAPABILITY - an auxiliary context that cannot be created or
 *     share-verified, an entry point absent on either context, or a fence name
 *     the fence path needs - refuses the served draw to the CPU raster path as
 *     it already did, and in particular NEVER falls back to FINISH or to the
 *     main context.
 *
 * ITS REFUSALS AND ITS CENSUS LINES ARE SPELLED IN THE SELECTORS' NAMES, and
 * the selection line says so where the selection is made. A refusal reading
 * "SL_BG_TF_AUX_SYNC=FENCE with SL_BG_TF_AUX_REAL=GPU_SOURCE" on a launch that
 * set neither names the MODE IN FORCE, which is what the earlier rounds' frozen
 * wording is about and what a reader needs to match on; the one line that is new
 * records that the launch named nothing, so the two together say what happened.
 *
 * A DIAGNOSTIC STANDS IT DOWN RATHER THAN REFUSING IT. A launch that opted in,
 * named no arm and selected one of the main-context diagnostics - the
 * original-raster control, state-only, position-only input, private vertex
 * array, zero count, an empty-draw row, the auxiliary empty-draw control, the
 * neutral program, the post-reset boundary, the buffer dump or the GL-state
 * probe - keeps the main-context prepass, because that is the prepass the
 * diagnostic is about. It is STATED, once per launch: a reader must never have
 * to infer which of the two prepasses produced the frames in front of them. */

/* HOW THE ARM IN FORCE WAS CHOSEN. 1 for the opt-in candidate - the clip prepass
 * with no arm named - and 0 for an arm the launch spelled and for every launch
 * with no arm in force. The arm token cannot carry this, and the two selections
 * issue the same commands, so this is what separates a census of the candidate
 * from a census of an A/B arm somebody set up. */
int sl_bgtf_ar_implicit(void);

/* WHETHER THE PREPASS HAS FAILED CLOSED FOR AN UNREADABLE ARM. 1 only on a
 * launch that opted in and then offered a value the arm's grammar cannot read:
 * on such a launch every served draw is refused to the existing CPU raster path
 * and no prepass runs on either context. 0 everywhere else, the explicit off
 * tokens included - those select the main-context diagnostic and are not a
 * failure. */
int sl_bgtf_ar_optin_closed(void);

/* ---- HOW THAT MODE SYNCHRONISES, SELECTED SEPARATELY (SL_BG_TF_AUX_SYNC) ----
 *
 * TWO SYNCHRONISATION POINTS, AND NOTHING ELSE, ARE WHAT THIS SELECTS. The mode
 * above establishes a cross-context handoff twice per served mesh: once so the
 * auxiliary context sees what the main context wrote into the input buffer, and
 * once so the main context sees what the transform wrote into the output pool.
 * FINISH is the original, and it spends a full pipeline drain on each - two
 * glFinish calls per bracket, which is the strongest completion guarantee
 * obtainable without resolving one further entry point, and which is also why a
 * FINISH launch's frame time measures nothing: each bracket stops the CPU until
 * the server has emptied its queue, so the number produced is the drain's cost
 * and not the mode's. FENCE replaces those two drains with two sync objects.
 *
 * WHAT A FENCE DOES NOT DO, STATED BEFORE ANYTHING ELSE. glWaitSync is a
 * SERVER-SIDE wait: it is issued into the consumer context's command stream and
 * returns immediately on the CPU, so under FENCE a bracket that has issued both
 * handoffs has PROVED ORDERING and has NOT established that either transfer has
 * completed at the instant the call returns. Nothing in this module may say a
 * write is complete on the CPU under FENCE; what it may say is that the
 * consumer's commands are ordered after the producer's, which is the property
 * the output pool's readers need and the only one claimed. Removing the two
 * drains therefore removes a measured CPU cost and leaves a frame time that is
 * no longer dominated by it - which makes a FENCE frame time something a
 * controlled benchmark could measure, NOT something this module has measured.
 *
 * IT CHANGES NOTHING ELSE: the real prepass,
 * the arm that decides the raster source, the object checks, the state written on
 * each context, the uniform values, the draw, the matched feedback scope and both
 * error verdicts are the same commands in the same order under either value.
 *
 * WHY A SEPARATE SELECTOR RATHER THAN TWO MORE ARM TOKENS. The arm names the
 * RASTER SOURCE and nothing else, and every arm needs both synchronisation
 * modes available for comparison - so folding the choice into the arm would
 * multiply the token set, change what sl_bgtf_ar_name answers, and move the
 * census and terminal lines that quote those tokens. Here the two questions stay
 * independent: one selector says what is rasterised, the other says how
 * visibility is established.
 *
 * THE SELECTOR IS READ ONLY ON A LAUNCH THAT ARMED AN ARM, AND THAT IS
 * STRUCTURAL RATHER THAN ASSERTED. Every site that consults it is already behind
 * the real-prepass mode being in force, so a launch with SL_BG_TF_AUX_REAL off
 * performs no getenv for this key, prints nothing about it, resolves nothing for
 * it and cannot have its call sequence changed by it. The cost of that choice is
 * stated plainly: a launch that sets SL_BG_TF_AUX_SYNC=FENCE without arming an
 * arm is answered with SILENCE, because the alternative is an environment read
 * and a possible line on the default path, which is the one thing that must stay
 * byte-identical.
 *
 * "FINISH" SELECTS IT EXPLICITLY AND "FENCE" SELECTS THE FENCE PATH, ON EVERY
 * ARM. What an absent or empty key means depends on WHICH ARM is in force,
 * because the two arms' defaults were established by different evidence; an
 * unreadable nonempty value is answered the same way on both (third item):
 *
 *   - ON A NAMED ARM (SL_BG_TF_AUX_REAL=CPU_SOURCE or GPU_SOURCE), absent and
 *     empty leave FINISH in force, byte for byte as before. Every earlier
 *     round's frozen output describes a named arm whose unqualified default is
 *     two glFinish calls per bracket, and an A/B control that silently changed
 *     synchronisation would not be a control.
 *   - ON THE OPT-IN CANDIDATE (no arm named), absent and empty leave FENCE in
 *     force. FENCE is the half of the combination the candidate was measured as,
 *     and it is also the half that does not spend two pipeline drains per served
 *     mesh.
 *   - AN UNREADABLE NONEMPTY VALUE SELECTS NEITHER MODE, ON EITHER ARM, and so
 *     refuses the arm rather than being answered with one of them. Choosing a
 *     mode for such a launch ran a synchronisation it had not selected, whichever
 *     of the two was chosen. Every served draw goes to the existing CPU raster
 *     path, no auxiliary context is created, no sync object is created and the
 *     MAIN-CONTEXT prepass is NOT what it falls to. The line is keyed by the
 *     selector's name and does not quote the value.
 *
 * So a launch obtains the mode it named, or its arm's own default, or no arm at
 * all - and never a mode nothing selected.
 *
 * v0.4 WHAT THE EXACT-IMAGE EVIDENCE COVERS, AND WHAT IT DOES NOT. The
 * auxiliary GPU_SOURCE arm under FENCE and under FINISH produced frames
 * byte-identical to the original renderer's (blue 143 at (71,113)) on the
 * Runway, Frigate and Facility viewpoints recorded for this selection,
 * including the 1920x1080 witnesses. That is what the row's ON position rests
 * on and it is a claim about THE IMAGE ONLY.
 *
 * NO FRAME TIME AND NO FRAME RATE FOLLOWS FROM IT. The relative timing screen
 * taken beside those captures passed, and the original renderer and this arm
 * BOTH missed the absolute p99 target that screen was taken beside - so the row
 * promises no frame rate, and FENCE against FINISH has still not been benchmarked
 * on one driver and scene. A fence removes two pipeline drains per bracket and
 * nothing in this header or its checks measures what that is worth. The
 * remaining exact-image coverage - the scenes these viewpoints do not reach -
 * is recorded in docs/backlog.md B-OPTROW and is not closed by anything here. */
enum sl_bgtf_fx_mode {
    SL_BGTF_FX_FINISH = 0,   /* two glFinish per bracket - the original  */
    SL_BGTF_FX_FENCE  = 1    /* two sync objects per bracket instead     */
};

/* The EFFECTIVE synchronisation mode. SL_BGTF_FX_FENCE only when an arm is in
 * force AND the fence path was selected for it AND every fence entry point
 * resolved on both contexts; SL_BGTF_FX_FINISH otherwise.
 *
 * SELECTED FOR IT covers both ways the fence path is reached, which is what
 * makes this description true of the candidate as well as of a named arm: the
 * KEY naming "FENCE", and the opt-in candidate's own default, which is the fence
 * path with no key named. A named arm with no key keeps FINISH. An INVALID
 * nonempty key selects neither - it refuses the arm, so this answers FINISH
 * while no arm is in force and no synchronisation of either kind is issued.
 *
 * SL_BGTF_FX_FINISH on every off launch, without the key being read. */
int sl_bgtf_ar_sync(void);

/* The synchronisation mode's name for a census or a fixture: "FINISH", "FENCE",
 * or "?" for an out-of-range value. It is the SELECTOR TOKEN for both, so a
 * record and a launch spell the mode the same way. Never NULL. Pure: no GL call,
 * no getenv, no state. */
const char *sl_bgtf_fx_name(int mode);

/* ---- WHAT THE FENCE PATH DID, KEPT APART FROM THE MODE'S OWN CENSUS --------
 *
 * WHY A SECOND STRUCT RATHER THAN MORE FIELDS IN struct sl_bgtf_counters. That
 * struct's ar_ members are printed in full by a product census line, token for
 * token, and a capture package reads exactly the fields that line emits. Adding
 * a member to it therefore either moves that line - which the FINISH mode and
 * every off launch must not do - or adds a member the line does not print, which
 * is a census that no longer describes its own struct. A separate struct with its
 * own accessor has neither problem: the existing line is untouched, every value
 * below is reachable, and which fields belong to which question stays legible.
 *
 * EVERY VALUE IS A CALL THIS PROCESS ISSUED OR A DECISION IT TOOK, never a
 * driver's answer, with the two declared exceptions that a sync object's
 * creation answers 0 or a handle and that the mode's own error drains are reads.
 *
 * fx_mode           the EFFECTIVE mode, as sl_bgtf_ar_sync answers it
 * fx_asked          1 when the KEY selected FENCE on an armed launch, whatever
 *                   became of it, so a refused request is distinguishable from
 *                   one that was never made. It is about the key and not about
 *                   the mode: the opt-in candidate's FENCE is its own default
 *                   rather than a request, so fx_mode reads FENCE beside
 *                   fx_asked 0 on such a launch, and the census's selection
 *                   token is what says which of the two happened
 * fx_proc_fails     capability refusals: a fence entry point that did not
 *                   resolve. The mode refuses and NEVER falls back to FINISH,
 *                   because a launch that reported fence timings while draining
 *                   the pipeline would be the one unreadable outcome
 * fx_proc_fails_main / fx_proc_fails_aux  the same, split by which context
 *                   could not supply its set
 * fx_in_fences      glFenceSync issued on the MAIN context, after the commands
 *                   that may have written the input buffer
 * fx_in_fence_fails those that answered 0, which is the creation failure
 * fx_in_flushes     glFlush issued on the MAIN context, explicitly, for the
 *                   reason recorded at the implementation
 * fx_in_waits       glWaitSync issued on the AUXILIARY context
 * fx_in_proved      input handoffs whose whole chain held
 * fx_out_fences / fx_out_fence_fails / fx_out_flushes / fx_out_waits /
 * fx_out_proved     the same five in the other direction: the fence and the
 *                   flush on the AUXILIARY context after the matched scope, the
 *                   wait on the MAIN context after the verified switch home
 * fx_deletes        glDeleteSync calls issued, and fx_deletes_main /
 *                   fx_deletes_aux which context issued each. A sync object is
 *                   shared, so either context may delete one, and the pointer
 *                   used is always the one resolved on the context that is
 *                   current
 * fx_leaks          sync objects still held at a point from which this bracket
 *                   RETURNS. It is 0 on every path by construction and is
 *                   counted rather than asserted so that an edit which broke
 *                   that says so in a number
 * fx_fatal_drops    sync objects deliberately NOT deleted because a lost-context
 *                   verdict had already been reached. Kept APART from fx_leaks:
 *                   after that verdict no GL command of any kind may be issued,
 *                   so abandoning the object is the correct action and the
 *                   process is ending anyway
 * fx_refusals       served draws refused by a fence guard specifically
 * fx_unproved_refusals  those refused because a handoff was not proved, which is
 *                   the subset that answers "was a GPU-source draw ever served
 *                   on an unproved handoff" with a number
 * fx_finish_omits   glFinish calls this mode did NOT issue because the fence
 *                   path replaced them. Counted at the point the drain would
 *                   have been issued, so it is fx_in_fences + fx_out_fences
 *                   exactly - two per COMPLETED bracket - and it is the one
 *                   positive record that the replacement happened rather than
 *                   being added alongside the drains
 * fx_writes         prepasses this mode COMPLETED: the slot tagged written,
 *                   which is the one state that lets a caller bind and
 *                   rasterise the output pool. Counted on the fence path only,
 *                   at the tag rather than at the proof, so that the two are
 *                   separate records of the same bracket and an edit which
 *                   tagged a write outside the proved-handoff gate shows up as
 *                   fx_writes exceeding fx_out_proved.
 *
 *                   WHY IT IS HERE AND NOT DERIVED FROM ar_writes. "Every
 *                   completed prepass proved both of its handoffs" is the one
 *                   claim a capture must be able to reject, and a reader of the
 *                   fence line cannot make it from the fence fields alone
 *                   without a count of completed prepasses in the same struct.
 *                   Reading ar_writes instead would make the claim depend on
 *                   joining two census lines and on the assumption that every
 *                   write on that launch was a fence write
 * fx_live           sync objects this process is holding right now: raised at
 *                   each creation that answered a handle and lowered at each
 *                   delete and each abandon. It is 0 at every point outside a
 *                   bracket, so a census reading it nonzero is a defect report
 *                   and not a state
 * fx_peak           the highest value fx_live ever reached. The two handles are
 *                   single locals and the input object is released before the
 *                   output object is created, so no path overlaps them and this
 *                   is AT MOST 1, which is rule A4 and holds whatever happened.
 *                   It is 1 on a run that created an object and 0 on a run whose
 *                   FIRST create answered 0, which held none; it is MEASURED
 *                   rather than asserted because an edit that held one across
 *                   the other would otherwise be invisible to a capture */
struct sl_bgtf_fence_counters {
    unsigned long fx_mode;
    unsigned long fx_asked;
    unsigned long fx_proc_fails;
    unsigned long fx_proc_fails_main;
    unsigned long fx_proc_fails_aux;
    unsigned long fx_in_fences;
    unsigned long fx_in_fence_fails;
    unsigned long fx_in_flushes;
    unsigned long fx_in_waits;
    unsigned long fx_in_proved;
    unsigned long fx_out_fences;
    unsigned long fx_out_fence_fails;
    unsigned long fx_out_flushes;
    unsigned long fx_out_waits;
    unsigned long fx_out_proved;
    unsigned long fx_deletes;
    unsigned long fx_deletes_main;
    unsigned long fx_deletes_aux;
    unsigned long fx_leaks;
    unsigned long fx_fatal_drops;
    unsigned long fx_refusals;
    unsigned long fx_unproved_refusals;
    unsigned long fx_finish_omits;
    unsigned long fx_writes;
    unsigned long fx_live;
    unsigned long fx_peak;
};
const struct sl_bgtf_fence_counters *sl_bgtf_fence_stats(void);

/* ---- THE ACCEPTANCE INVARIANTS A CAPTURE APPLIES TO THESE COUNTERS --------
 *
 * The product emits every member above as one bounded key=value line, FENCE
 * only, immediately after the AUX_REAL census (src/gfx/sl_gfx_dl.c). These are
 * the conditions under which that line is evidence that the fence path ran and
 * proved both of its handoffs. They are listed here because the STRUCT is their
 * authority: a counter added above without a rule here is a field nothing
 * checks. Write W for fx_writes.
 *
 * MODE
 *   M1  the line exists at all only when the effective mode is FENCE, so a
 *       capture with no such line is a FINISH capture whatever was requested
 *   M2  fx_mode is 1 and fx_asked is 1
 *
 * BOTH HANDOFFS ON EVERY COMPLETED PREPASS - the claim the round exists for
 *   H1  fx_out_proved is exactly W. The output proof is counted at the same
 *       bracket point as the write and nowhere else, so a write without its
 *       output proof, or a proof without its write, breaks this
 *   H2  fx_in_proved is W + fx_out_fence_fails. The input handoff is also
 *       recorded as proved on the one refusal path that happens after it - an
 *       output fence that answered 0 - so that a reader of that refusal is sent
 *       to the output side and not to both
 *
 * PER DIRECTION, PER CONTEXT, ON A RUN WITH NO FAILURE (every counter in F1
 * zero)
 *   D1  fx_in_fences  == fx_in_flushes  == fx_in_waits  == fx_deletes_aux  == W
 *       the input fence and its producer flush on the MAIN context, the wait on
 *       the AUXILIARY one, the delete on the context that waited
 *   D2  fx_out_fences == fx_out_flushes == fx_out_waits == fx_deletes_main == W
 *       the same in the other direction
 *   D3  fx_deletes is fx_deletes_main + fx_deletes_aux, and 2W
 *   D4  fx_finish_omits is 2W - the two drains per bracket that were replaced
 *
 * ALWAYS, WHATEVER HAPPENED
 *   A1  fx_finish_omits == fx_in_fences + fx_out_fences, because the omission is
 *       counted where the drain would have been issued and before the create
 *   A2  (fx_in_fences - fx_in_fence_fails) + (fx_out_fences - fx_out_fence_fails)
 *       == fx_deletes + fx_fatal_drops + fx_live: every object that was created
 *       was deleted, abandoned under a lost-context verdict, or is still held
 *   A3  fx_live is 0 and fx_leaks is 0 - nothing is held at a census, and
 *       nothing was held at a bracket's return
 *   A4  fx_peak is at most 1
 *
 * FAILURE - any of these nonzero means the line is NOT evidence of a proved
 * fence run, and a capture checker REJECTS it rather than reporting it
 *   F1  fx_proc_fails, fx_proc_fails_main, fx_proc_fails_aux,
 *       fx_in_fence_fails, fx_out_fence_fails, fx_refusals,
 *       fx_unproved_refusals, fx_leaks, fx_fatal_drops
 *
 * AGAINST THE AUX_REAL CENSUS ON THE SAME LAUNCH, which is printed immediately
 * before it and whose 43 tokens are unchanged by this line's existence
 *   X1  the arm token is the same in both lines
 *   X2  ar_writes == W: the same completed prepasses, counted independently in
 *       the two structs
 *   X3  ar_syncs == fx_in_flushes + fx_out_flushes. Under FINISH each of the two
 *       drains raises ar_syncs; under FENCE each successful handoff open does,
 *       at the same point - so the mode's synchronisation-point count per
 *       bracket is two either way and ar_syncs is 2W on a clean FENCE run, the
 *       same value the same work reports under FINISH
 *   X4  ar_prepasses >= W and ar_setups >= 1
 *
 * WHAT NONE OF IT ESTABLISHES. Every number is a call this process issued or a
 * decision it took. A proved handoff is an ORDERING this process established in
 * the consumer's command stream, not a completion observed on the CPU and not a
 * pixel. No field is a frame time and no combination of them is a benchmark. */

/* THE CAPABILITY ANSWER, computed once against a live context and then cached.
 * Resolves the transform-feedback and buffer entry points, compiles and links
 * the prepass program with its one recorded output varying, and locates every
 * uniform. 0 means this walk uses the existing path.
 *
 * A SELECTION THAT IS OFF LEAVES THE STATE UNTRIED, exactly as the other
 * module's does, so an off launch compiles nothing and pays nothing. Only a
 * real failure - a missing entry point, a program that will not compile or
 * link, a uniform or varying that does not resolve, a buffer that cannot be
 * allocated - latches UNAVAILABLE, and that is never retried inside the
 * context. */
int sl_bgtf_ready(void);

/* THIS WALK'S INPUT POOL AND TRANSFORM.
 *
 * `in_vbo` and `in_stride` name the buffer the item's model-space positions
 * already live in and the stride between them - the pass's own static vertex
 * buffer, which the caller owns. Reading that buffer is what keeps the prepass
 * from re-uploading a pool the GPU already holds.
 *
 * `nverts` is the vertex count: the prepass reads the WHOLE pool as GL_POINTS
 * in canonical vertex order, so output vertex i is input vertex i and the
 * original indices index the output directly. An indexed prepass would emit
 * one output per index rather than per vertex, which is a duplicated stream
 * and not an indexable clip pool.
 *
 * `gen` is the walk's identity - sl_gfx_dl.c's own per-walk counter. It
 * participates in the reuse key, so a new walk can never read a buffer filled
 * for a previous one even if its transform happens to compare equal.
 *
 * Returns 0 if the walk is not eligible after all, in which case the caller
 * must keep the existing CPU path AND must not leave its per-vertex loop
 * reduced - see the repair note on sl_bgtf_prepass.
 */
int sl_bgtf_begin_walk(const struct sl_bggpu_walk *w, unsigned nverts,
                       unsigned in_vbo, unsigned in_stride, unsigned gen);

/* ENSURE A CLIP POOL FOR THIS DRAW'S DEPTH, and return its slot.
 *
 * The depth case belongs to the DRAW, not to the walk: a frame draws in one
 * range or in two, and FAR0's coefficients belong to the MESH, because a
 * blended mesh takes the blend overlap's pair and every other mesh the opaque
 * pair. A prepass output is therefore keyed by (walk, dmode, k1, k2) and NOT
 * by the walk alone. One shared first-range buffer would hand a blended mesh
 * the opaque mesh's coefficients, which is why there is a slot per distinct
 * pair rather than a single FAR0 buffer.
 *
 *   SL_BGGPU_DEPTH_SINGLE  the one-range frame's far clamp, in the original
 *                          comparison's own form.
 *   SL_BGGPU_DEPTH_FAR0    the first range, k1 * w - k2, in the original float
 *                          ordering.
 *   SL_BGGPU_DEPTH_FAR1    the game's range: the UNCLAMPED z as computed.
 *
 * A slot already filled for the same key is REUSED and no GPU work is done, so
 * a run of meshes sharing a depth pays one prepass and the second range of a
 * far frame pays one more. Returns a slot index >= 0, or -1 on refusal.
 *
 * ON REFUSAL THE CALLER OWES THE CPU ARRAYS. A walk admitted here may have had
 * its per-vertex loop reduced, and the data that loop would have written is
 * what the frame's other readers consume. The caller must recompute it before
 * any reader runs. sl_gfx_dl.c does this by refusing the walk at admission -
 * before the reduction's repair point - so the repair that already exists for
 * the other module's refusals covers this one too, rather than a second repair
 * path being introduced. */
int sl_bgtf_prepass(int dmode, float k1, float k2);

/* BIND THE SLOT'S CLIP POOL AS THE POSITION ARRAY, with program 0 current.
 *
 * AN UNWRITTEN POOL IS NOT A POOL. A slot carries its key as soon as the key is
 * installed - that is what makes the reuse cadence work - but it carries its
 * CONTENTS only once a prepass draw has actually written them. Those are two
 * different facts, and this call requires the second: a setup-only slot
 * (SL_BG_TF_STATE_ONLY), a slot keyed by a zero-count call
 * (SL_BG_TF_ZERO_COUNT, whose draw submitted no vertex) or one whose allocation
 * was refused is REFUSED here even if a caller offers it, because binding it
 * would rasterise storage no transform ever filled. The guard is here rather
 * than left to one caller's discipline for the same reason the generation check
 * is.
 *
 * Leaves the fixed pipeline bound - that is the whole point of the candidate -
 * and switches ONLY the position array to the prepass output. The pass's
 * colour, UV and fog arrays are client arrays and are not touched, which is
 * why the array buffer is bound for the glVertexPointer call and unbound
 * immediately after it: a buffer left bound would make every later client
 * pointer be read as an offset into it.
 *
 * RETURNS THE POOL'S DEPTH MODE, or -1 if the slot could not be bound. The
 * caller holds that value as the depth of the source that is now actually
 * current, which is what it must count submissions against - the depth the
 * walk last computed is not the same thing once a mesh falls back. */
int sl_bgtf_bind_clip(int slot);

/* ONE ACTUAL NONEMPTY SUBMISSION on a prepass pool, called at the one place a
 * background run is really drawn.
 *
 * A BIND IS NOT A DRAW. One bind can serve several runs of a mesh, and a mesh
 * whose triangles are all filtered submits none at all, so counting coverage
 * at the bind both over- and under-counted. `dmode` is the depth of the pool
 * that is the vertex source for THIS run, so a run drawing from the CPU array
 * contributes nothing whatever the walk was admitted as. An empty run is not a
 * submission. */
void sl_bgtf_count_submission(int dmode, unsigned n);

/* Put back what the prepass and the clip binding changed: the program, the
 * array and element bindings, the indexed transform-feedback binding, the
 * vertex source and rasterizer discard. Called before the pass's own drawing
 * continues, so no original state is left disturbed. */
void sl_bgtf_unbind(void);

/* A context's two ends, called by the SDL backend around create and delete
 * (sl_gfx_sdl.c). Everything cached here - the program, the buffers, the
 * uniform locations, the resolved entry points - belongs to one context.
 * Makes NO GL call: the context may already be unusable, and the driver
 * releases the objects with it. */
void sl_bgtf_invalidate(void);

/* THE ONE TEARDOWN THAT DOES MAKE A GL CALL, and the reason it is a second
 * function rather than a line inside the one above. sl_bgtf_invalidate is called
 * at a context's CREATE as well as at its end, where every name it holds belongs
 * to a context that is already gone and a delete would be issued against the
 * wrong object; its contract is therefore that it calls no GL at all. This is
 * called only while the context that owns the objects is still current, which is
 * the only instant a delete is legal, and it deletes exactly one thing: the
 * NEUTRAL-PROGRAM control's SUCCESSFULLY LINKED program object
 * (SL_BG_TF_NEUTRAL_PROG). That control's vertex shader is not a case here - the
 * acquire releases it on every path, and the flagged object is freed by this
 * delete along with the program holding it - and a program that failed to LINK
 * was already deleted by the acquire while its context was current, so this
 * never sees one. The ordinary
 * prepass program and the slot buffers are deliberately not deleted here - the
 * driver releases them with the context, which is what sl_bgtf_invalidate's note
 * records - so this adds no call on any launch that did not create a neutral
 * program. An off launch created none and this is one integer test.
 *
 * AND IT RELEASES ONE NON-OBJECT RESOURCE: the AUXILIARY-CONTEXT control's
 * second GL context (SL_BG_TF_AUX_EMPTY), destroyed through the backend here for
 * the same reason the program delete is here - the window and the main context
 * are still valid at this instant and are not after it. The destroy is NOT a GL
 * command and creates no object, so it is unaffected by the delete contract
 * above; what it does require is a main context that is current, which is why it
 * cannot move into sl_bgtf_invalidate.
 *
 * THE ONE CASE THAT DESTROYS NOTHING is a control that LOST the main context: a
 * switch back that failed leaves this process unable to prove which context is
 * current, so the teardown deliberately calls neither GL nor the backend and
 * reports the auxiliary context as undestroyed. The platform releases it with
 * the process. An off launch created no context and this is one pointer test. */
void sl_bgtf_shutdown(void);

/* THE COUNTERS, kept distinct on purpose. A refusal that fell back to the CPU
 * must never be able to read as GPU coverage, so the prepasses actually run,
 * the vertices actually transformed, the raster draws actually routed through
 * program 0 with a prepass pool, the reuses, and the refusals and repairs are
 * separate numbers rather than one "used" flag. */
struct sl_bgtf_counters {
    unsigned long prepasses;        /* prepass submissions that ran          */
    unsigned long vertices;         /* vertices actually transformed by them */
    unsigned long reuses;           /* draws served by an already-filled slot*/
    unsigned long binds;            /* pools made the vertex source          */
    unsigned long raster_draws;     /* ACTUAL nonempty runs on a prepass pool*/
    unsigned long raster_indices;   /* indices in them                       */
    unsigned long dcalls[3];        /* those runs by the SOURCE's depth mode  */
    unsigned long refusals;         /* walks or draws refused to the CPU     */
    unsigned long walks;            /* walks that submitted a prepass        */
    unsigned long slot_refills;     /* a free or prior-walk slot taken       */
    unsigned long slot_evictions;   /* a CURRENT-walk key displaced: pressure*/
    /* THE STATE-ONLY CONTROL'S OWN NUMBERS (SL_BG_TF_STATE_ONLY), DIAGNOSTIC
     * and never coverage. A setup that ran with its vertex draw omitted
     * transformed nothing and wrote no pool, so it must not raise `prepasses`,
     * `vertices` or `reuses`: those stay 0 on that arm, and that zero beside a
     * positive so_setups is what distinguishes "the setup ran and nothing was
     * drawn" from "the prepass never ran". These three carry the cadence
     * instead - the setup brackets that ran, the prepass draws omitted, and the
     * draws an already-keyed setup-only slot served. The first two are equal by
     * construction, because the omission IS the one dropped call, and a
     * discriminator can check that equality. */
    unsigned long so_setups;
    unsigned long so_omitted;
    unsigned long so_reuses;
    /* THE POSITION-ONLY-INPUT CONTROL'S OWN NUMBERS (SL_BG_TF_POS_INPUT). This
     * arm DOES do real GPU work, so `prepasses`, `vertices` and the rest stay
     * truthful and positive beside these: these say only how much client-array
     * exposure was removed from each draw and put back.
     *
     * pi_draws   prepass draws that ran with the isolation actually IN FORCE -
     *            not draws offered, and not the switch being set
     * pi_off     non-position client arrays disabled, summed over those draws
     * pi_back    arrays turned back on. EQUAL to pi_off on every honest run:
     *            the restore re-enables exactly what the save found enabled, so
     *            a difference is a missed or an invented restore and a gate can
     *            read it as one
     * pi_units   the WIDEST sweep any draw performed, so a census carries the
     *            range that was actually accounted for rather than an
     *            assumption that it was unit 0. The range is the TEXTURE
     *            COORDINATE SET domain (GL_MAX_TEXTURE_COORDS), which is what
     *            the per-unit selector selects among and which the OpenGL 2.1
     *            spec allows to exceed the conventional unit count
     * pi_refusals  draws on which the isolation was REFUSED and the ordinary
     *            draw ran instead - an unresolvable client unit selector, a
     *            coordinate domain unanswered or past the bounded ceiling, or an
     *            inherited client selector the context will not state as a
     *            selectable set. A refusal is never isolation, and the two
     *            counts are kept apart so a run cannot report coverage it did
     *            not have. */
    unsigned long pi_draws;
    unsigned long pi_off;
    unsigned long pi_back;
    unsigned long pi_units;
    unsigned long pi_refusals;
    /* THE PRIVATE-PREPASS-VERTEX-ARRAY CONTROL'S OWN NUMBERS
     * (SL_BG_TF_PRIVATE_VAO). This arm does real GPU work, so `prepasses`,
     * `vertices` and the rest stay truthful and positive beside these: these say
     * only how much of that work actually ran inside the private object, and how
     * much did not.
     *
     * pv_binds     prepass setups on which the private object was bound AND the
     *              bind was confirmed by a read-back - not setups offered, and
     *              not the switch being set
     * pv_draws     prepass draws that ran under a confirmed bind. A future
     *              controller can require this to be positive before it believes
     *              a run exercised the control at all, which the switch alone
     *              cannot establish
     * pv_restores  inherited bindings put back AND CONFIRMED by a read-back. A
     *              restore the context does not confirm is never counted here -
     *              it is terminal and raises pv_terminal instead - so this
     *              number never includes an unverified one. On an honest run it
     *              equals pv_binds
     * pv_nonzero   those restores whose inherited binding was NONZERO - the case
     *              a save assuming object 0 would have corrupted, carried as its
     *              own number so a census says whether it was exercised
     * pv_refusals  setups on which the control was REFUSED and the prepass ran
     *              exactly as it does without it, WITH THE INHERITED BINDING
     *              KNOWN - an unresolved entry point, an inherited binding the
     *              context would not state, an object it would not allocate, or
     *              a bind it did not confirm whose ONE cleanup bind back to the
     *              saved value WAS confirmed. A refusal is never isolation and
     *              never work. An unconfirmed restore or an unconfirmed cleanup
     *              is NOT here, because neither leaves a state to fall back to
     * pv_terminal  the unprovable cases, which do not fall back: a restore the
     *              context would not confirm, or a cleanup bind after an
     *              unconfirmed bind that the context would not confirm either.
     *              The binding the following raster would read is unknown, so
     *              the process ENDS rather than continuing - see the fail-safe
     *              note above. It follows that any census a reader ever sees
     *              carries 0 here: a positive value exists only in the terminal
     *              marker on the process's own stream, which is where to look
     * pv_reuses    draws a cache HIT served while the control was selected. No
     *              bind, no draw and no setup was added for them, which is why
     *              they are their own number rather than part of pv_binds */
    unsigned long pv_binds;
    unsigned long pv_draws;
    unsigned long pv_restores;
    unsigned long pv_nonzero;
    unsigned long pv_refusals;
    unsigned long pv_reuses;
    unsigned long pv_terminal;
    /* THE ZERO-VERTEX-COUNT CONTROL'S OWN NUMBERS (SL_BG_TF_ZERO_COUNT),
     * DIAGNOSTIC and never coverage. A call that submitted no vertex produced no
     * output and wrote no pool, so it must not raise `prepasses`, `vertices`,
     * `reuses`, `binds` or either submission count: those stay 0 on this arm,
     * and that zero beside a positive zc_calls is what distinguishes "the setup
     * ran and a call submitting nothing was made" BOTH from "the prepass
     * transformed vertices" and from "the call was omitted"
     * (SL_BG_TF_STATE_ONLY, whose so_omitted says the opposite thing).
     *
     * zc_setups   setup brackets that ran under the control
     * zc_calls    prepass glDrawArrays calls ISSUED with the count 0. EQUAL to
     *             zc_setups by construction, because the bracket issues exactly
     *             one, so a discriminator can check that equality and a reader
     *             can tell an issued empty call from an omitted one
     * zc_reuses   draws an already-keyed slot served while the control was
     *             selected. No call and no setup was added for them, which is
     *             why they are their own number rather than part of `reuses` -
     *             a slot this arm keyed has no pool to reuse
     * zc_refusals launches that asked for the control and did not get it: a
     *             conflicting arm, the buffer diagnostic set, or a missing
     *             coupled switch. Raised ONCE per launch at the point the
     *             switches are decided, never per draw. It follows that any
     *             census line a reader ever sees carries 0 here, because the
     *             line is printed only on a launch where the control IS
     *             effective - a refused launch prints the reason on stderr and
     *             no marker at all */
    unsigned long zc_setups;
    unsigned long zc_calls;
    unsigned long zc_reuses;
    unsigned long zc_refusals;
    /* THE EMPTY-DRAW STATE MODE'S OWN NUMBERS (SL_BG_TF_EMPTY_STATE),
     * DIAGNOSTIC and never coverage. This mode runs on the zero-count path, so
     * `prepasses`, `vertices`, `reuses`, `binds` and both submission counts stay
     * 0 on every one of its arms exactly as they do on Z_KNOWN, and the
     * zero-count control's own four numbers stay truthful beside these. What
     * these add is WHICH COMMANDS WERE ACTUALLY ISSUED at and around the empty
     * draw - the thing a selected variable cannot establish and the thing that
     * tells the rows apart.
     *
     * EVERY ONE IS RAISED AT THE CALL SITE, immediately after the command it
     * counts, and never from the requested mode. A launch that selected a mode
     * and whose arm issued nothing therefore reads as es_setups 0 rather than as
     * the mode's name standing in for work.
     *
     * es_setups     setup brackets that ran under the mode
     * es_draws      empty draws ISSUED under it. EQUAL to es_setups by
     *               construction, since one bracket issues exactly one call, so
     *               a reader can treat an inequality as a missed or an invented
     *               draw
     * es_reuses     draws an already-keyed slot served while a mode was
     *               selected. No call, no setup and no state change was added
     *               for them. es_setups + es_reuses is the COMPLETE served-mesh
     *               account: every mesh the caller served took one or the other,
     *               so a census can check that sum against its own mesh count
     *               and a shortfall is unaccounted work rather than a rounding
     * es_tf_begins  glBeginTransformFeedback calls issued under the mode
     * es_tf_ends    glEndTransformFeedback calls issued under it. EQUAL to
     *               es_tf_begins on every honest run, because the pair is
     *               matched or omitted together and an unmatched one is invalid.
     *               Both are 0 on a T0 arm and equal to es_setups on a T1 arm,
     *               which is what distinguishes the two without consulting the
     *               selector
     * es_prog0_switches  every glUseProgram(0) the row issued around the empty
     *               draw, whichever side of it. 0 on A, B and C; 1 on E, R, S
     *               and Q
     * es_prog0_restores  every glUseProgram(the prepass program) the row issued
     *               around it. EQUAL to es_prog0_switches on E, R and Q; 1 LOWER
     *               on S, which is the only row that leaves program 0 current
     *               past the draw and so has no rebind to count - an inequality
     *               is therefore the S row's own signature and not, on that row,
     *               a missed restoration
     * es_prog_pre_zero   WHERE each of those calls was, which is what tells the
     * es_prog_pre_prog   rows apart: the totals above are 1/1 for both E and R.
     * es_prog_post_zero  Each counts the one position, raised immediately after
     * es_prog_post_prog  the call it counts. Per setup bracket the four read
     *               0/0/0/0 on A, B and C, 1/0/0/1 on E, 1/1/0/0 on R, 1/0/0/0
     *               on S and 0/0/1/1 on Q - eight distinct tuples with the two
     *               conditions above them, so a reader can name the row from the
     *               commands alone without consulting the selector
     * es_draws_prog_tf   empty draws ISSUED with the prepass program current,
     * es_draws_prog0     and with program 0 current. Taken from what this
     *               process made current, so the pair sums to es_draws and says
     *               which program the call was made under rather than which one
     *               the row's name implies. A, B, C, R and Q raise the first; E
     *               and S raise the second
     * es_discard_offs    discard transitions to OFF made at the empty draw
     * es_discard_ons     restorations to ON after it. EQUAL to es_discard_offs
     *               on every honest run; both 0 on a D1 arm
     * es_refusals   launches that asked for a mode and did not get one: an
     *               unreadable selector value, a missing coupled switch, or one
     *               of the older exclusive controls requested in the same
     *               launch. Raised ONCE per launch where the switches are
     *               decided, never per draw - so any census line a reader sees
     *               carries 0 here, because the line is printed only on a launch
     *               whose mode IS effective and a refused launch prints the
     *               reason on stderr and no marker
     * es_mode       the EFFECTIVE mode as enum sl_bgtf_es_mode, so a record
     *               carries which row produced these numbers rather than
     *               leaving a reader to infer it from the pattern of zeros. 0 on
     *               an off or refused launch
     *
     * ISSUED IS NOT VERIFIED. These count commands this process made. Nothing
     * here reads GL state back, and no value below is evidence about what a
     * driver did with a command. */
    unsigned long es_setups;
    unsigned long es_draws;
    unsigned long es_reuses;
    unsigned long es_tf_begins;
    unsigned long es_tf_ends;
    unsigned long es_prog0_switches;
    unsigned long es_prog0_restores;
    unsigned long es_prog_pre_zero;
    unsigned long es_prog_pre_prog;
    unsigned long es_prog_post_zero;
    unsigned long es_prog_post_prog;
    unsigned long es_draws_prog_tf;
    unsigned long es_draws_prog0;
    unsigned long es_discard_offs;
    unsigned long es_discard_ons;
    unsigned long es_refusals;
    unsigned long es_mode;
    /* THE POST-RESET BOUNDARY CONTROL'S OWN NUMBERS (SL_BG_TF_POST_RESET),
     * DIAGNOSTIC and never coverage. The control adds calls AFTER the prepass
     * bracket has closed and changes nothing inside it, so every other number
     * in this struct - the candidate's own work counts, the zero-count arm's
     * four and the empty-draw mode's seventeen - reads exactly as it does with
     * this control absent, whichever arm is selected. What these add is WHICH
     * CALLS WERE ACTUALLY ISSUED at the boundary and at how many prepasses, so
     * a run can show which arm executed at every applicable prepass rather than
     * only which arm was asked for.
     *
     * EVERY ONE IS RAISED AT THE CALL SITE, immediately after the command it
     * counts, and never from the requested arm.
     *
     * pr_setups    prepass brackets that REACHED the boundary with the control
     *              in force and every precondition proved. This is the
     *              denominator a reader needs: an arm that was selected and
     *              never reached reads as pr_setups 0 rather than as the arm's
     *              name standing in for work
     * pr_draws     glDrawArrays(GL_POINTS, 0, 0) calls ISSUED at the boundary.
     *              EQUAL to pr_setups on POST_PROGRAM0_EMPTY and on BOTH,
     *              because each reached boundary issues exactly one; 0 on
     *              POST_FINISH
     * pr_finishes  glFinish calls ISSUED at the boundary. EQUAL to pr_setups on
     *              POST_FINISH and on BOTH; 0 on POST_PROGRAM0_EMPTY. The two
     *              counts together name the arm from the commands alone:
     *              (pr_draws, pr_finishes) per reached boundary is (1,0), (0,1)
     *              and (1,1) on the three arms and (0,0) on none of them
     * pr_draws_prog0  those empty draws issued with program 0 current, taken
     *              from what this process made current - the common
     *              restoration's own glUseProgram(0), whose having run is one
     *              of the preconditions. EQUAL to pr_draws on every honest run;
     *              an inequality means a draw was issued at the boundary
     *              without that call accounted for, which is the precondition
     *              failing silently rather than failing closed
     * pr_skips     prepass brackets at which the control was in force and the
     *              boundary issued NOTHING because a precondition was not
     *              proved - no prepass draw was issued in the bracket, a
     *              feedback scope was left open, the common discard disable did
     *              not run, or the common glUseProgram(0) did not. Counted per
     *              bracket and reported once per launch. A skip is never a call
     *              and never work, so it is kept apart from pr_setups and a run
     *              cannot report an arm it did not execute
     * pr_reuses    draws a cache HIT served while the control was selected. No
     *              setup ran, so no boundary exists for them and no call was
     *              added; pr_setups + pr_skips + pr_reuses is then the complete
     *              served-mesh account for the control
     * pr_refusals  launches that asked for an arm and did not get one: an
     *              unreadable selector value, a missing SL_BG_TF_CLIP=1, or one
     *              of the four older exclusive controls requested in the same
     *              launch. Raised ONCE per launch where the switches are
     *              decided, never per draw - so any census line a reader sees
     *              carries 0 here, because the line is printed only on a launch
     *              whose arm IS effective and a refused launch prints the reason
     *              on stderr and no marker
     * pr_mode      the EFFECTIVE arm as enum sl_bgtf_pr_mode, so a record
     *              carries which arm produced these numbers rather than leaving
     *              a reader to infer it. 0 on an off or refused launch
     *
     * ISSUED IS NOT VERIFIED. These count commands this process made. Nothing
     * here reads GL state back, no glGetError is called on any arm, and no value
     * below is evidence about what a driver did with any command. */
    unsigned long pr_setups;
    unsigned long pr_draws;
    unsigned long pr_finishes;
    unsigned long pr_draws_prog0;
    unsigned long pr_skips;
    unsigned long pr_reuses;
    unsigned long pr_refusals;
    unsigned long pr_mode;
    /* THE NEUTRAL-PROGRAM EMPTY-DRAW CONTROL'S OWN NUMBERS
     * (SL_BG_TF_NEUTRAL_PROG), DIAGNOSTIC and never coverage. The control runs on
     * the zero-count arm, whose own four counts and the candidate's work counts
     * read exactly as they do with this control absent: `prepasses`, `vertices`
     * and `walks` are untouched, the slot is marked unwritten, and the draw is
     * still the one empty call that arm issues. What these add is WHICH PROGRAM
     * THAT CALL WAS ISSUED UNDER and whether the two switches around it happened.
     *
     * EVERY ONE IS RAISED AT THE CALL SITE, immediately after the command it
     * counts, and never from the switch being set.
     *
     * np_setups    prepass brackets that issued the empty draw with this control
     *              in force. The denominator: a control that was selected and
     *              never reached a bracket reads as np_setups 0 rather than as
     *              the switch standing in for work
     * np_draws     empty draws ISSUED at those brackets. EQUAL to np_setups on
     *              every honest run, because one bracket issues exactly one
     * np_draws_neutral  those draws issued with the NEUTRAL program the last one
     *              this bracket made current, taken from what this process
     *              issued and not from a glGet. EQUAL to np_draws on every
     *              honest run; an inequality means a draw was issued under a
     *              program the control did not select, which is the one outcome
     *              that would be reported as a neutral run without being one
     * np_switches  glUseProgram(the neutral program) calls ISSUED before those
     *              draws. EQUAL to np_draws
     * np_restores  glUseProgram(the ordinary prepass program) calls ISSUED after
     *              them. EQUAL to np_draws, which is what says the bracket was
     *              handed back to the common cleanup in the state that cleanup
     *              expects rather than left on the second program
     * np_reuses    draws a cache HIT served while the control was in force. No
     *              bracket ran, so no empty draw and no switch exists for them;
     *              np_setups + np_skips + np_reuses is the complete served-mesh
     *              account for the control
     * np_skips     served draws REFUSED because a site precondition was not
     *              proved - the zero-count arm not in force, an empty-draw row
     *              in force, or one of the three older exclusive controls in
     *              force. Nothing is issued and the prepass returns -1, so the
     *              caller draws from the CPU; this is never a draw under another
     *              program
     * np_creates   neutral programs created and successfully linked. 1 per
     *              context on a run that reached a bracket, 0 on an off launch
     * np_create_fails  served draws refused because the neutral program could
     *              not be made: glDeleteProgram or glDeleteShader did not
     *              resolve, the shader did not compile, the context handed back
     *              no program object, or the link failed. The state latches, so
     *              a failing context refuses every later served draw and
     *              compiles nothing again
     * np_deletes   glDeleteProgram calls ISSUED at teardown, while the owning
     *              context was still current. 1 per created program
     * np_shader_deletes  glDeleteShader calls ISSUED by the acquire. EQUAL to
     *              the shader objects it created, on every path: the three exits
     *              after a shader exists that end in a refusal free it, and the
     *              successful exit flags it while the linked program holds it,
     *              so np_shader_deletes == np_creates + (the create_fails that
     *              got as far as a shader). The invariant a reader can check
     *              without that breakdown is the one this counter exists for:
     *              it never exceeds the creates and never falls short of them,
     *              because no path leaves this function with a shader alive
     * np_fail_prog_deletes  glDeleteProgram calls ISSUED by the acquire for a
     *              program that FAILED TO LINK, deleted on the spot because the
     *              context is current there and that name is the only reference
     *              to the object. Disjoint from np_deletes by construction: the
     *              name is cleared immediately after, so teardown never sees a
     *              failed-link program and no program is deleted twice.
     *              np_deletes + np_fail_prog_deletes is the whole program
     *              account for a context that reached teardown
     * np_refusals  launches that asked for the control and did not get it: the
     *              zero-count arm not in force, or any of the exclusive controls
     *              or the two diagnostics requested in the same launch. Raised
     *              ONCE per launch where the switches are decided, never per
     *              draw - so any census line a reader sees carries 0 here,
     *              because the line is printed only on a launch whose control IS
     *              in force and a refused launch prints the reason on stderr
     *
     * ISSUED IS NOT VERIFIED. These count commands this process made. Nothing
     * here reads GL state back, no glGetError is called, and no value below is
     * evidence about what a driver did with any command or about any pixel. */
    unsigned long np_setups;
    unsigned long np_draws;
    unsigned long np_draws_neutral;
    unsigned long np_switches;
    unsigned long np_restores;
    unsigned long np_reuses;
    unsigned long np_skips;
    unsigned long np_creates;
    unsigned long np_create_fails;
    unsigned long np_deletes;
    unsigned long np_shader_deletes;
    unsigned long np_fail_prog_deletes;
    unsigned long np_refusals;
    /* THE AUXILIARY-CONTEXT EMPTY-DRAW CONTROL'S OWN NUMBERS
     * (SL_BG_TF_AUX_EMPTY), DIAGNOSTIC and never coverage. The control runs on
     * the zero-count arm, whose own counts and the candidate's work counts read
     * exactly as they do with this control absent, with ONE declared exception:
     * the empty draw is not issued on the MAIN context at all. `zc_calls`
     * therefore does not rise on this arm, and ax_main_omits is the positive
     * record of each call the main context did not make.
     *
     * EVERY ONE IS RAISED AT THE CALL SITE, immediately after the command or
     * the context switch it counts, and never from the switch being set.
     *
     * ax_setups    prepass brackets reached with this control in force. The
     *              denominator: a control selected and never reaching a bracket
     *              reads as ax_setups 0 rather than as the switch standing in
     *              for work
     * ax_main_omits  empty draws the MAIN context did NOT issue in those
     *              brackets. EQUAL to ax_setups on every honest run, and the
     *              one thing that distinguishes this arm from row A positively
     *              rather than by the absence of a count
     * ax_draws     glDrawArrays(GL_POINTS, 0, 0) calls ISSUED on the AUXILIARY
     *              context. EQUAL to ax_setups on every honest run
     * ax_draws_aux those draws issued while the auxiliary context was the last
     *              one this process made current, taken from the switch this
     *              process issued and not from a query. EQUAL to ax_draws on
     *              every honest run; an inequality means a draw was issued with
     *              the wrong context current, which is the one outcome that
     *              would be reported as an auxiliary run without being one
     * ax_switches  successful switches TO the auxiliary context AT THOSE DRAWS.
     *              EQUAL to ax_draws. The acquire's own verification round trip
     *              is deliberately NOT counted here - these two are per-bracket
     *              counts and that equality is what a reader checks, so folding
     *              a one-off into them would make every run read as one switch
     *              too many. That the round trip happened is recorded by
     *              ax_creates instead: a verified context cannot exist without
     *              it
     * ax_restores  successful switches BACK to the main context after those
     *              draws, on the same terms. EQUAL to ax_draws, which is what
     *              says the bracket was handed back with the caller's own
     *              context current
     * ax_reuses    draws a cache HIT served while the control was in force. No
     *              bracket ran, so no draw and no switch exists for them;
     *              ax_setups + ax_skips + ax_reuses is the complete served-mesh
     *              account for the control
     * ax_skips     served draws REFUSED because a site precondition was not
     *              proved - the zero-count arm not in force, an empty-draw row
     *              in force, or one of the controls this one is exclusive with
     *              in force. Nothing is issued and the prepass returns -1
     * ax_creates   auxiliary contexts created, SHARE-VERIFIED and left with the
     *              main context current. 1 per main context on a run that
     *              reached a bracket, 0 on an off launch
     * ax_create_fails  served draws refused because the auxiliary context could
     *              not be established: glIsProgram did not resolve on the main
     *              context, the backend offered no window or main context,
     *              creation failed, the object-sharing attribute's previous
     *              value could not be read or put back, the verification switch
     *              failed, the auxiliary context did not resolve its OWN entry
     *              points, the context did not share names, or which context is
     *              current became unknown. Counted PER REFUSED DRAW at the
     *              call site, not once per failure inside the acquire - the
     *              acquire latches and answers from its cache afterwards, so a
     *              count inside it would record the first refusal and drop every
     *              later one, while "a failing context refuses EVERY served
     *              draw" is exactly what this number has to carry. The three
     *              counts below name WHICH failure it was, once, where it is
     *              known
     * ax_share_fails  verification switches on which glIsProgram answered FALSE
     *              for the prepass program, meaning the new context does NOT
     *              share object names with the main one. The draw is refused:
     *              a draw under a name that is not the prepass program in that
     *              context would be reported as this arm without being it
     * ax_switch_fails  switches to the auxiliary context that failed with the
     *              MAIN context VERIFIED current. That verification is what
     *              makes the outcome recoverable: GL still reaches the main
     *              context, so the bracket's own restoration runs and the served
     *              draw is refused with nothing issued in either context
     * ax_proc_fails  served draws refused because the auxiliary context did not
     *              resolve its OWN glIsProgram and glUseProgram. Entry-point
     *              addresses belong to the context they were resolved on, so the
     *              main context's are never used there; the context is destroyed
     *              again with the main context verified current and the draw
     *              goes to the CPU path
     * ax_lost      outcomes in which which GL context a command would reach
     *              became UNKNOWN: a switch, in either direction, after which
     *              neither context could be verified current. This is the
     *              DIAGNOSTIC-INVALID outcome and the one a reader must not
     *              mistake for anything else - and it is not a refusal, because
     *              there is nothing to continue from. The control issues no
     *              further GL command of any kind, performs no restoration that
     *              would run on the wrong context, destroys nothing, and ENDS
     *              THE PROCESS at the point of loss (see ax_fatals). A run with
     *              ax_lost nonzero measures nothing
     * ax_fatals    times the terminate-now path was taken. At most 1, and in the
     *              product it is 1 only in the launch's final instant: the call
     *              does not return, so no teardown and no exit handler follows
     *              it. It can be observed as 1 with the process still running
     *              only in a fixture that intercepts the terminate, which is
     *              exactly what makes "the loss ends the process" a measurement
     *              rather than a claim about code nobody can reach
     * ax_destroys  auxiliary contexts destroyed, at a refusal inside the acquire
     *              or at teardown, in either case with the main context VERIFIED
     *              current and the window alive. EQUAL to ax_creates on a run
     *              that reached teardown with ax_lost 0
     * ax_refusals  launches that asked for the control and did not get it.
     *              Raised ONCE per launch where the switches are decided, never
     *              per draw
     *
     * ISSUED IS NOT VERIFIED, with the single exception named above: the share
     * check reads glIsProgram, which is a QUERY about a name and not about what
     * a driver did with a command. Nothing else here reads GL state back, no
     * glGetError is called, and no value below is evidence about any pixel. */
    unsigned long ax_setups;
    unsigned long ax_main_omits;
    unsigned long ax_draws;
    unsigned long ax_draws_aux;
    unsigned long ax_switches;
    unsigned long ax_restores;
    unsigned long ax_reuses;
    unsigned long ax_skips;
    unsigned long ax_creates;
    unsigned long ax_create_fails;
    unsigned long ax_share_fails;
    unsigned long ax_switch_fails;
    unsigned long ax_proc_fails;
    unsigned long ax_lost;
    unsigned long ax_fatals;
    unsigned long ax_destroys;
    unsigned long ax_refusals;
    /* THE AUXILIARY-CONTEXT REAL-PREPASS MODE'S OWN NUMBERS
     * (SL_BG_TF_AUX_REAL), DIAGNOSTIC and never coverage. Every one is raised at
     * the call site, immediately after the command, the context switch or the
     * verification it counts, and never from the selector being set.
     *
     * ar_mode      the EFFECTIVE arm (sl_bgtf_ar_mode), so a record carries the
     *              arm that ran rather than the one requested. 0 beside a
     *              positive ar_refusals on a refused launch
     * ar_setups    prepass brackets reached with the mode in force. The
     *              denominator: a mode selected and never reaching a bracket
     *              reads as ar_setups 0 rather than as the selector standing in
     *              for work
     * ar_main_omits  prepass draws the MAIN context did NOT issue in those
     *              brackets. EQUAL to ar_setups on every honest run, and the
     *              positive record that the main context drew nothing
     * ar_prepasses  REAL glDrawArrays(GL_POINTS, 0, nverts) calls issued on the
     *              AUXILIARY context, inside a matched feedback scope
     * ar_prepasses_aux  those issued while the auxiliary context was the last
     *              one this process made current, taken from the switch this
     *              process issued and not from a query. EQUAL to ar_prepasses on
     *              every honest run; an inequality means a draw was issued with
     *              the wrong context current, which is the one outcome that
     *              would be reported as an auxiliary run without being one
     * ar_vertices  vertices those draws SUBMITTED. NONZERO is the whole point of
     *              this mode, so a run reporting ar_prepasses without
     *              ar_vertices is the empty-draw control under another name
     * ar_writes    output writes that COMPLETED and were made visible: the draw
     *              and its matched scope issued, the completion glFinish issued
     *              on the auxiliary context, the MAIN context verified current
     *              again and the pool re-bound there. Only a bracket counted
     *              here marks its slot written, so this is also the count of
     *              slots the GPU_SOURCE arm may bind
     * ar_cpu_draws  served draws handed back on the CPU_SOURCE arm. The pool was
     *              written and is never read on this arm, the original position
     *              stream being what rasterises
     * ar_gpu_submits  served draws handed back on the GPU_SOURCE arm with a
     *              COMPLETED write behind them, so the shared pool is eligible
     *              to be bound and rasterised. ar_cpu_draws + ar_gpu_submits
     *              <= ar_setups, the difference being brackets that refused
     * ar_switches  successful switches TO the auxiliary context AT THOSE
     *              BRACKETS. EQUAL to ar_prepasses. The acquire's own
     *              verification round trips are deliberately NOT counted here,
     *              for the reason ax_switches excludes its own: these are
     *              per-bracket counts and that equality is what a reader checks.
     *              That the round trips happened is recorded by ar_creates
     * ar_restores  successful switches BACK to the main context after those
     *              brackets, on the same terms. EQUAL to ar_prepasses, which is
     *              what says each bracket was handed back with the caller's own
     *              context current
     * ar_syncs     synchronisation operations this mode ISSUED: one glFinish on
     *              the main context before each switch and one on the auxiliary
     *              context after each matched scope. 2 per completed bracket on
     *              an honest run. An off launch leaves it 0, which is what makes
     *              "off adds no synchronisation point" a number
     * ar_rebinds   output-pool re-binds issued on the MAIN context after a
     *              verified switch back - the bind-after-completion half of the
     *              visibility rule, on the reading side. EQUAL to ar_writes
     * ar_reuses    served draws a cache HIT answered while the mode was in
     *              force. No bracket ran, so no draw, no switch and no
     *              synchronisation exists for them; ar_setups + ar_skips +
     *              ar_reuses is the complete served-mesh account for the mode
     * ar_skips     served draws REFUSED because a site precondition was not
     *              proved - the zero-count arm in force, an empty-draw row in
     *              force, the original-raster control not matching the arm, or
     *              one of the controls this mode is exclusive with in force.
     *              Nothing is issued and the prepass returns -1. It also carries
     *              the draws refused because an OPTED-IN launch offered an
     *              unreadable arm: that launch has no arm in force, so this
     *              mode's census line does not print and the count is read from
     *              the struct by a fixture, while the module's own census shows
     *              the refusals and no prepass
     * ar_creates   auxiliary contexts this mode brought to a state it can use:
     *              created, share-verified, their own entry points resolved
     *              there, their own vertex array object generated there, and the
     *              MAIN context verified current. 1 per main context on a run
     *              that reached a bracket, 0 on an off launch
     * ar_create_fails  served draws refused because that state could not be
     *              established. Counted PER REFUSED DRAW at the call site and
     *              not once per failure inside the acquire, for the reason
     *              ax_create_fails is: the acquire latches and answers from its
     *              cache afterwards, so a count inside it would record the first
     *              refused draw and drop every later one, while "a failing
     *              context refuses EVERY served draw" is what this has to carry
     * ar_proc_fails  acquires refused because the auxiliary context did not
     *              resolve one of the entry points this mode calls there.
     *              Entry-point addresses belong to the context they were
     *              resolved on, so the main context's are never used instead
     * ar_prog_fails  acquires refused because glIsProgram, asked on the
     *              auxiliary context through that context's own pointer,
     *              answered FALSE for the prepass program
     * ar_vao_fails  acquires refused because that context would not give this
     *              mode a vertex array object of its own
     * ar_buf_checks  glIsBuffer questions asked on the auxiliary context: two
     *              per bracket, the input buffer and the output pool
     * ar_buf_fails  brackets refused because one of those answered FALSE, so a
     *              name this mode would have bound means nothing in that
     *              context. Nothing is issued and the served draw is refused
     * ar_switch_fails  switches to the auxiliary context that failed with the
     *              MAIN context VERIFIED current. That verification is what
     *              makes the outcome recoverable
     * ar_sync_refusals  brackets refused at or after a synchronisation step,
     *              which on today's code means a verified switch back that could
     *              not be obtained after the completion glFinish. Kept apart
     *              from ar_switch_fails because a failure BEFORE any work and a
     *              failure AFTER the output was written are different facts: the
     *              second leaves a pool whose visibility was never established,
     *              and the slot is marked unwritten for exactly that reason
     * ar_lost      outcomes in which which GL context a command would reach
     *              became UNKNOWN. The DIAGNOSTIC-INVALID outcome: no further GL
     *              command is issued, no restoration runs, nothing is destroyed,
     *              and the process ENDS at the point of loss. A run with ar_lost
     *              nonzero measures nothing
     * ar_fatals    times the terminate-now path was taken. At most 1, and
     *              observable as 1 with the process still running only in a
     *              fixture that intercepts the terminate
     * ar_refusals  launches that asked for the mode and did not get it. Raised
     *              ONCE per launch where the selectors are decided, never per
     *              draw
     * ar_gl_pre_errors  GL error codes that were ALREADY flagged on the
     *              auxiliary context when a bracket reached it, retired by the
     *              drain at the top of the bracket. They belong to commands the
     *              bracket did not issue - the acquire's own visit, or a context
     *              creation - so they are counted HERE and never under
     *              ar_gl_errors, which is reserved for a command of this mode's
     *              own bracket that GL rejected. The attribution is separate;
     *              the CONSEQUENCE is the same, because a context that was
     *              already in an error state has an UNPROVED health - the code
     *              may be a GL_CONTEXT_LOST raised by the context's creation or
     *              by this mode's capability acquire - and a diagnostic whose
     *              dedicated context is in an unknown condition may not report
     *              a write. On an honest run this is 0 at every bracket, because
     *              nothing outside this mode's bracket touches that context
     * ar_gl_pre_refusals  served draws REFUSED for that, which is at most 1 per
     *              launch: the first such finding LATCHES the capability before
     *              anything is bound, enabled or issued on that context, so no
     *              later served draw reaches it again. Kept apart from
     *              ar_gl_error_refusals so that a reader can see WHICH of the
     *              two findings refused a launch - one says the context arrived
     *              unhealthy, the other that GL rejected this mode's own command
     * ar_gl_first_pre_error  the FIRST such code, as GL's own value, or 0 if
     *              none was ever found
     * ar_gl_errors  brackets whose own commands left an error behind on the
     *              auxiliary context, read there after the matched scope closed
     *              and the context's state was put back - or whose error state
     *              could not be emptied under the drain's bound, which is
     *              counted here as well because an unreadable error state cannot
     *              say that a command was accepted. GL REJECTING A COMMAND IS
     *              WHAT THIS COUNTS, which no other value here can see: glFinish
     *              completes the commands that were issued and does not turn a
     *              rejected one into a transform that ran
     * ar_gl_error_refusals  served draws REFUSED because of that. Equal to
     *              ar_gl_errors, the one being this mode's response to the
     *              other, and separate from it so that a reader can see the
     *              equality rather than assume it
     * ar_gl_first_error  the FIRST code a bracket's own drain answered, as GL's
     *              own value, or 0 if none ever did
     * ar_gl_drain_caps  drains that reached their bound with the error state
     *              still not empty. The drain is bounded so that its termination
     *              does not rest on the driver answering GL_NO_ERROR; a nonzero
     *              value here says an implementation would not come back, and
     *              every such drain is treated as an error and never as a clean
     *              state
     *
     * ISSUED IS NOT VERIFIED, with the four declared exceptions: the share
     * check, the two per-bracket glIsBuffer questions - each a QUERY ABOUT A
     * NAME and not about what a driver did with a command - and the per-bracket
     * ERROR-STATE DRAIN, which is about exactly that and is why the seven
     * values above exist. The drain is issued on the auxiliary context only and
     * so never consumes the main context's error state. Nothing else reads GL
     * state back, and no value below is evidence about any pixel or any frame
     * time. */
    unsigned long ar_mode;
    unsigned long ar_setups;
    unsigned long ar_main_omits;
    unsigned long ar_prepasses;
    unsigned long ar_prepasses_aux;
    unsigned long ar_vertices;
    unsigned long ar_writes;
    unsigned long ar_cpu_draws;
    unsigned long ar_gpu_submits;
    unsigned long ar_switches;
    unsigned long ar_restores;
    unsigned long ar_syncs;
    unsigned long ar_rebinds;
    unsigned long ar_reuses;
    unsigned long ar_skips;
    unsigned long ar_creates;
    unsigned long ar_create_fails;
    unsigned long ar_proc_fails;
    unsigned long ar_prog_fails;
    unsigned long ar_vao_fails;
    unsigned long ar_buf_checks;
    unsigned long ar_buf_fails;
    unsigned long ar_switch_fails;
    unsigned long ar_sync_refusals;
    unsigned long ar_lost;
    unsigned long ar_fatals;
    unsigned long ar_refusals;
    unsigned long ar_gl_pre_errors;
    unsigned long ar_gl_pre_refusals;
    unsigned long ar_gl_first_pre_error;
    unsigned long ar_gl_errors;
    unsigned long ar_gl_error_refusals;
    unsigned long ar_gl_first_error;
    unsigned long ar_gl_drain_caps;
};
const struct sl_bgtf_counters *sl_bgtf_stats(void);

/* 0 untried, 1 ready, -1 a REAL capability failure and only that. The reason
 * string carries the last refusal's words and is "" until something refuses. */
int         sl_bgtf_state(void);
const char *sl_bgtf_why(void);

/* ======================================================================== *
 * THE PRODUCTION BUFFER DIAGNOSTIC (SL_BG_TF_DUMP) - developer-only, OFF.
 *
 * WHAT IT MEASURES AND WHAT IT DOES NOT. One selected nonempty background
 * submission's ACTUAL transform-feedback output pool is read back once and kept
 * beside the CPU clip pool the original path would have submitted for the same
 * draw, plus that draw's exact index sequence and the draw's identity. It
 * records; it concludes nothing. Equal pools would exclude differing submitted
 * clip bits at this draw and would still not name a raster, interpolation or
 * state mechanism; unequal pools would localise a transform or output gap. No
 * image, no frame time and no cause follows from this output.
 *
 * IT IS NOT A PERFORMANCE PATH AND SUPPLIES NO TIMING EVIDENCE. The readback is
 * synchronous by construction - glGetBufferSubData on a buffer the prepass just
 * wrote drains the work that fills it - so a run that captures has a stall in it
 * and its frame times are not a measurement of anything.
 *
 * DEFAULT OFF, AND "OFF" IS A LIST OF THINGS THAT DO NOT HAPPEN. With
 * SL_BG_TF_DUMP absent, empty, "0", or malformed in any way: no entry point is
 * resolved (in particular glGetBufferSubData is never asked for), nothing is
 * allocated, no file is opened or created, no GL call is added, no GL error is
 * consumed, and the candidate's own raster call sequence, draw order, indices,
 * depth, colour, fog and UV arrays are byte-for-byte what they are without this
 * diagnostic compiled in. The whole cost of an off launch is one cached integer
 * test per offered draw.
 *
 * THE SELECTION IS GENERIC. The moment is named by the counters the pass
 * already maintains and by the item's own path, the mesh and material by their
 * indices, and the range by SL_BGGPU_DEPTH_*. No level name, geometry offset or
 * per-level correction appears here or is accepted by the selector: a selector
 * that encoded one level would have to be edited for the next one.
 *
 * THE CLOCKS ARE FOUR DIFFERENT NUMBERS AND ARE KEPT APART. The screenshot
 * line in the SDL backend prints `read=` from sl_record_index() and `frame=`
 * from sl_frames_completed(); the DRAW SITE's middle counter is g_dl_frame,
 * which is a different number again. Measured (r557): a screenshot reading
 * read 140 / frame 279 / cloud 139 corresponds to the draw-site tuple
 * 139:139:139 - sl_record_index 139, g_dl_frame 139, cloud 139 - which is the
 * tuple the snapshot file written for that moment carries. So the selector's
 * three numeric clock fields are the DRAW-SITE counters, and a control naming
 * the screenshot's 140 would select a different draw.
 *
 * THE FOURTH IS sl_frames_completed() SAMPLED AT THE DRAW SITE, and it is
 * recorded as `draw_site_pumped` rather than as the screenshot's frame.
 * sl_frames_completed returns the mutable sl_frame
 * (src/platform/sl_ultra_shim.c:978), which sl_frame_advance (:3543) and the
 * message pump (:1497) both increment; this diagnostic samples it inside the
 * display-list walk, while the screenshot samples it later through
 * sl_gfx_end_frame (:1432) -> sdl_end (src/gfx/sl_gfx_sdl.c:1303) -> sdl_shot
 * (:886). Two samples of one mutable counter at two points in a frame are not
 * one reading, and the r557 snapshot that fixed the three gate clocks did not
 * contain this one at all. So it is a recorded u32: it is NEVER selected on,
 * NEVER required to equal the screenshot's 279, and never replaced by a guessed
 * value. The screenshot's pumped frame is observed and gated separately, from
 * the screenshot evidence. All four are written into the sidecar so a reader can
 * check which clock, at which site, a number came from.
 *
 * ONE ATTEMPT PER PROCESS, CLAIMED BEFORE ANYTHING IS TOUCHED. The attempt is
 * spent the moment a draw is accepted, before any entry point is resolved,
 * anything is allocated, the GPU is read or a file is opened - so a failure
 * cannot retry at the next matching draw, the next frame or another slot. Every
 * distinguishable failure is its own refusal with its own words, and a refusal
 * is NEVER a zero-difference result, an all-zero or stale buffer, or the CPU
 * array reported as a transform-feedback capture.
 *
 * OUTPUT IS CREATE-ONLY BY EXCLUSIVE CREATION, not by asking first. Every file
 * is opened with O_CREAT|O_EXCL - one operation that both creates and claims -
 * so an existing path is refused without a byte of it being touched however it
 * came to be there, whether or not this process can read it, and even if it
 * appeared a moment ago. A probe followed by a truncating open would have
 * destroyed an unreadable existing record and would have had a window for one
 * created between the two calls.
 *
 * AND THE SEAL IS A PUBLICATION, not a sentence in a file. The sidecar states
 * whether the record is complete, so that statement has to be composed before
 * the sidecar's own write can fail - which once meant a full, parseable,
 * digest-matching object saying "complete" could survive a close that failed
 * while the runtime reported the failure. So the sidecar is written to
 * <base>.json.staging, flushed and closed, and only then moved to <base>.json
 * by an atomic no-replace move. A sidecar at the final name is one whose bytes
 * are known to be on disk; a staging file is composed evidence that was never
 * published and is NOT a record. Failed staging files and partial payloads are
 * left exactly where they are - nothing is deleted and nothing is overwritten.
 */

/* THE DRAW THE SEAM OFFERS. Every field is a fact the pass already holds at the
 * submission site; nothing here is derived inside the module, so the module
 * cannot disagree with the pass about which draw this is.
 *
 * `clip` is the CPU clip pool AS IT STANDS AT THIS DRAW - after the bypass
 * repair and after this range's own depth rewrite, which is the only point at
 * which it is the array the original path would have submitted. ALL FOUR
 * COMPONENTS OF THE RECORDED CPU PAYLOAD COME FROM IT, in every depth mode.
 *
 * `zraw` is the per-vertex depth before the one-range clamp. It is passed for
 * ONE purpose: counting the vertices where it and clip[2] hold different bits,
 * which is evidence about the two arrays. It is NOT written into the payload.
 * Substituting it into z for FAR1 - on the reasoning that the pass writes the
 * same value back into clip[2] - discarded the bits the record exists to carry
 * and manufactured agreement with the GPU pool at exactly the vertices where
 * the submitted clip bits were the question.
 *
 * Model positions are NOT part of this capture and must not be mixed into it:
 * the pools here are clip space, and the item's own model positions are the
 * background snapshot's business.
 *
 * `w` is the walk's transform, read for the record's identity only. */
struct sl_bgtf_dump_site {
    unsigned    read;         /* sl_record_index() at the draw site        */
    unsigned    dl_frame;     /* g_dl_frame - the DRAW-SITE middle counter */
    unsigned    cloud;        /* g_sky_cloud_phase                        */
    unsigned    pumped;       /* sl_frames_completed() AT THE DRAW SITE -
                               * recorded as draw_site_pumped, never selected
                               * on and never required to equal the
                               * screenshot's own later sample of the same
                               * mutable counter                           */
    const char *item;         /* the item's own path; NOT assumed to be
                               * NUL-terminated - `item_bytes` bounds it   */
    unsigned    item_bytes;
    unsigned    mesh;
    unsigned    material;
    int         dmode;        /* the depth of the pool ACTUALLY bound     */
    int         slot;         /* the slot sl_bgtf_prepass returned        */
    float       k1, k2;       /* this draw's coefficient pair             */
    unsigned    nverts;       /* the item's vertex count                  */
    const float *clip;        /* 4 floats a vertex, at THIS draw          */
    const float *zraw;        /* 1 float a vertex, the unclamped depth    */
    const struct sl_bggpu_walk *w;
};

/* THE BOUNDS, checked before any count is multiplied by a record size. Sized
 * for one background item's frame with headroom; a larger one is a defect
 * rather than a bigger picture. */
#define SL_BGTF_DUMP_MAX_VERTS    1048576u
#define SL_BGTF_DUMP_MAX_INDICES  4194304u

/* IS THE DIAGNOSTIC ARMED? SL_BG_TF_DUMP=read:frame:cloud:mesh:material:dmode
 * with an optional :item-path-substring, parsed ONCE for the launch. Each of
 * the six numeric fields is a bounded decimal or a single '*' for any; dmode
 * additionally must be 0, 1 or 2 (SL_BGGPU_DEPTH_SINGLE / _FAR0 / _FAR1).
 *
 * THE GRAMMAR FAILS CLOSED. Anything that cannot be read exactly - trailing
 * junk, a missing field, a value that would wrap, a dmode outside the three -
 * leaves the diagnostic OFF and is reported once. Arming on an unreadable
 * selector would record the first draw that happened by, which is the failure
 * this diagnostic exists to avoid.
 *
 * THE PROVENANCE IS REQUIRED TO ARM, not merely recorded if offered:
 * SL_BG_TF_DUMP_EXE_SHA and SL_BG_TF_DUMP_SRC_SHA must each be exactly 64
 * lowercase hex characters and not all zeros. A capture that names no build is
 * not evidence, and an all-zero digest is what an unset field looks like. */
int sl_bgtf_dump_opt_in(void);

/* IS THIS DRAW THE SELECTED ONE? Answered only for a draw whose pool is
 * ACTUALLY the vertex source - the caller gates on its own source flag, not on
 * the walk's admission - and only when the slot still holds the selected key:
 * this walk's generation, this depth, this vertex count and the coefficient
 * bits the depth implies. A stale slot, another mesh sharing the pool, a
 * wrong-depth pool or a pool whose storage is short is REFUSED and is not a
 * match. A no-match costs no attempt and no GL call. */
int sl_bgtf_dump_select(const struct sl_bgtf_dump_site *s);

/* CAPTURE, at the one place a background run is really submitted and
 * immediately before that indexed draw. `indices` and `count` are the run's own
 * exact submitted sequence.
 *
 * The attempt is claimed first, then the counts, the index range and the byte
 * products are bounded, then - and only then - glGetBufferSubData is resolved,
 * the buffers are allocated and the pool is read ONCE for the ACTIVE vertex
 * count, never for a larger capacity a previous item left on the slot. The
 * generic array binding is put back exactly; the indexed transform-feedback
 * binding and the retained glVertexPointer source are not touched, and no
 * pointer command is issued, so the raster draw that follows reads exactly what
 * it would have read. No glFinish, no query and no second transform. */
void sl_bgtf_dump_capture(const unsigned *indices, unsigned count);

/* 0 nothing attempted, 1 a COMPLETE record was sealed, -1 the one attempt was
 * spent and the record is INCOMPLETE or REFUSED. The status text carries the
 * outcome's own words and is "" until an attempt is made. */
int         sl_bgtf_dump_state(void);
const char *sl_bgtf_dump_status(void);

/* THE DIAGNOSTIC'S OWN CENSUS, kept apart from the candidate's. The rejection
 * reasons are separate numbers because "the clock did not match" and "the slot
 * did not hold the key" are different facts, and a run that captured nothing
 * must be able to say which. */
struct sl_bgtf_dump_counters {
    unsigned long offers;         /* draws offered while armed             */
    unsigned long clock_miss;     /* the clock tuple did not match         */
    unsigned long ident_miss;     /* item, mesh, material or depth         */
    unsigned long slot_reject;    /* invalid, stale or wrong-key slot      */
    unsigned long bounds_reject;  /* a count or a pointer the record needs */
    unsigned long selected;       /* offers that answered yes              */
    unsigned long captures;       /* attempts claimed                      */
    unsigned long completed;      /* records sealed COMPLETE               */
    unsigned long readbacks;      /* glGetBufferSubData calls made          */
    unsigned long late_offers;    /* offers after the attempt was spent     */
};
const struct sl_bgtf_dump_counters *sl_bgtf_dump_stats(void);

#endif /* SL_GFX_BGTF_H */
