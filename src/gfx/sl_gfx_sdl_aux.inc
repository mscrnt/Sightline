/* ---- v0.4 THE AUXILIARY GL CONTEXT: production prepass and diagnostics ----
 *
 * WHAT THIS IS FOR AND WHAT USES IT. These five functions create, switch to and
 * destroy a SECOND, OBJECT-SHARING context on this same window. Two callers in
 * the background pass (src/gfx/sl_gfx_bgtf.c) reach them:
 *
 *   * THE PRODUCTION PATH. With the OPTIMIZED RENDERING row ON (the row's
 *     default, src/platform/sl_settings.c) and no developer selector naming an
 *     arm, tf_prod_arm() selects the auxiliary-context transform-feedback clip
 *     prepass on arm GPU_SOURCE, and the prepass calls sl_gl_aux_create and
 *     sl_gl_aux_make_current from sl_gfx_bgtf.c (first at :3898). This file
 *     is therefore on the default path of an optimized launch. It is NOT a
 *     developer-only seam.
 *   * THE DIAGNOSTICS. The selector-named arms (SL_BG_TF_AUX_REAL,
 *     SL_BG_TF_AUX_SYNC, SL_BG_TF_AUX_EMPTY's single empty draw) reach the same
 *     functions by name, and still do.
 *
 * HOW A REFUSAL LANDS. A context that cannot be created, a share that cannot
 * be verified, an unresolved probe or an unreadable or refused synchronisation
 * leaves the main context current and unchanged; the served draw is REFUSED
 * to the CPU clip path, and the refusal is reported once per launch. Only a
 * LOST state - a backend that cannot afterwards establish which context is
 * current - is not a fallback: the caller reports the diagnostic invalid and
 * the process is terminated without further GL (see TerminateProcess below).
 *
 * With the row OFF and no selector, no context is created here: `aux` stays
 * NULL and every function below is one pointer test.
 *
 * WHAT THIS COMMENT DOES NOT ESTABLISH. That the default path REACHES these
 * functions is a source-level fact. It is not live production image
 * acceptance: the recorded exact-image captures of this arm were taken with
 * the developer selectors, and a run of the row's own selection path end to
 * end is still owed (docs/backlog.md, B-OPTROW).
 *
 * WHY THIS IS A SEPARATE FILE, INCLUDED TEXTUALLY. The window and the main
 * context are static to sl_gfx_sdl.c and no header exports them, so the seam
 * has to live in that translation unit; and the seam is also the one part of
 * the backend whose FAILURE SEMANTICS a test has to be able to drive, which
 * needs an SDL whose answers a test chooses. A textual include is both: the
 * product compiles exactly one translation unit containing this text, with the
 * same statics and the same linkage it had as inline code, while
 * tools/profiling/native/test_sl_gfx_sdl_aux.c compiles THIS FILE against a
 * scripted SDL and drives every outcome below. There is no second build of the
 * product and no conditional compilation in the product path: the only macro
 * this file lets a caller pre-define is the terminate primitive, and leaving it
 * undefined - which the product does - is what selects the real one.
 *
 * WHY THE SEAM IS HERE AND NOT IN THE MODULE. The module declares these
 * prototypes and links against them, exactly as it declares sl_gl_proc - which
 * keeps it compilable against a synthetic GL that supplies its own set.
 *
 * SHARING IS REQUESTED, NOT ASSUMED. SDL_GL_SHARE_WITH_CURRENT_CONTEXT makes
 * the new context share object names with whatever is current at creation, so
 * the main context must be current when the create runs and the attribute is
 * set immediately before it and put back immediately after it - leaving it set
 * would make every later context in this process a share of whatever happened
 * to be current. Whether the driver honoured it is NOT decided here: the caller
 * verifies it with a query on the new context and refuses its own diagnostic if
 * the answer is no. This function reports only that a context was created.
 *
 * AND THE ATTRIBUTE'S PRIOR VALUE IS READ, PUT BACK, AND PROVED BY READBACK -
 * never by a return code. A read that fails BEFORE anything is written is a
 * refusal rather than a default: assuming 0 would be a guess about process-wide
 * state this file is about to overwrite, and since nothing has been overwritten
 * yet the caller's world is exactly as it was. Once a write has been ATTEMPTED
 * the question changes and so does the answer. The attribute is process-wide
 * REQUESTED state - SDL2 stores it in gl_config.share_with_current_context and
 * a query hands that same field back (src/video/SDL_video.c,
 * SDL_GL_SetAttribute and SDL_GL_GetAttribute, release-2.32.10) - so a set that
 * reported failure may still have stored, and a set that reported success may
 * not have stored what was asked. A return code therefore cannot establish what
 * the attribute now holds; only a readback can. A value READ BACK EQUAL TO THE
 * ORIGINAL is restored whatever the set returned, and a value that differs or
 * cannot be read at all is UNPROVED.
 *
 * AN UNPROVED ATTRIBUTE IS THE LOST OUTCOME, NOT A REFUSAL. What is left in an
 * unknown state is not this diagnostic's context but the configuration every
 * later context creation in this process reads, and that cannot be repaired
 * from here. A recoverable 0 would hand that process back to normal rendering
 * and to a later GL teardown with its context-creation configuration changed or
 * unknown, so it is never the answer: the attribute's state is proved, or the
 * process ends.
 *
 * THE PROFILE AND VERSION ATTRIBUTES ARE LEFT EXACTLY AS THE MAIN CONTEXT'S
 * CREATION LEFT THEM. sdl_init sets only a double buffer, a depth size and a
 * stencil size and asks for no profile or version at all, so the main context
 * is whatever compatibility context the platform gives by default; setting
 * nothing here is therefore what makes the auxiliary context the same kind of
 * context, and naming a profile or a version would make it a DIFFERENT one.
 * That is also why the two depth/stencil minimums are not re-stated: they are
 * still set from sdl_init and have not been touched since.
 *
 * THE QUERY DECIDES WHICH CONTEXT IS CURRENT - NOT A RETURN CODE. This is the
 * rule every function below is written to, and it is a rule about what the
 * platform documents. SDL_GL_MakeCurrent documents "Returns 0 on success or a
 * negative error code on failure" and says NOTHING about what remains current
 * after a failure (https://wiki.libsdl.org/SDL2/SDL_GL_MakeCurrent), so a
 * failed switch leaves this process with no documented claim about where a GL
 * command would land - and the claim is exactly what a caller that is about to
 * issue GL needs. SDL_GL_GetCurrentContext answers it directly: "Returns the
 * currently active OpenGL context or NULL on failure"
 * (https://wiki.libsdl.org/SDL2/SDL_GL_GetCurrentContext). So every switch
 * here, successful or failed, is followed by that query, and the answer - not
 * the return code - decides what this file reports. A successful switch whose
 * query disagrees is treated as a lost context, which is the only safe reading:
 * the two cannot both be true and the one that permits issuing GL is the one
 * that must not be believed.
 *
 * WHAT EACH RESULT MEANS TO A CALLER, and the one distinction the whole file
 * exists to keep: 1 is the intended context VERIFIED current, 0 is a refusal
 * with the MAIN context VERIFIED current AND the process-wide GL configuration
 * proved unchanged - recoverable, the caller may issue GL and draw from the CPU
 * exactly as before - and SL_GL_AUX_LOST (-1) is UNKNOWN, which has two causes
 * and one response. Either this process cannot say which context a GL command
 * would reach, or it cannot say how a later context would be CREATED because
 * the object-sharing attribute could not be proved restored. Both are states no
 * later GL may be issued from, and both name themselves on stderr so a reader
 * is told which one happened. The module turns the third outcome into an
 * immediate process termination, because "issue no more GL" is not something a
 * return value can enforce on a program with teardown paths and exit handlers
 * in it.
 */
#define SL_GL_AUX_LOST (-1)
static SDL_GLContext aux;

/* Declared here as well as at each definition because the create below restores
 * the main context through the same verified switch every other caller uses,
 * rather than repeating SDL_GL_MakeCurrent and its verification a second time
 * with its own wording. */
int sl_gl_aux_create(void);
int sl_gl_aux_make_current(void);
int sl_gl_main_make_current(void);
int sl_gl_aux_destroy(void);
void sl_gl_terminate_no_gl(int code);

/* THE TERMINATE PRIMITIVE. Pre-definable so a test can observe that it was
 * reached; undefined - which is the product - selects the real one.
 *
 * WHY TerminateProcess AND NOT ExitProcess OR exit() ON WINDOWS. The caller
 * reaches sl_gl_terminate_no_gl only when which GL context a command would
 * reach is unknown, so the requirement is that NO further GL call happens -
 * including calls this process would otherwise make on its way out. exit()
 * disqualifies itself: "The exit function calls destructors for thread-local
 * objects, then calls - in last-in-first-out (LIFO) order - the functions that
 * are registered by atexit and _onexit, and then flushes all file buffers
 * before it terminates the process"
 * (https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/exit-exit-exit),
 * and this build registers atexit handlers that issue GL. _exit() avoids those
 * - "The _Exit and _exit functions terminate the process without destroying
 * thread-local objects or processing atexit or _onexit functions, and without
 * flushing stream buffers" (same page) - but the Windows process exit it
 * performs still runs DLL detach, and ExitProcess documents what that means:
 * "The entry-point functions of all loaded dynamic-link libraries (DLLs) are
 * called with DLL_PROCESS_DETACH"
 * (https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess),
 * which on this build means the GL driver's and SDL's own detach code. The same
 * page names the alternative and when to prefer it: "In contrast, if a process
 * terminates by calling TerminateProcess, the DLLs that the process is attached
 * to are not notified of the process termination. Therefore, if you do not know
 * the state of all threads in your process, it is better to call
 * TerminateProcess than ExitProcess." A lost current context is precisely a
 * state this process cannot characterise, so TerminateProcess is the choice,
 * and "DLLs are not notified" is the property that makes it complete: no user
 * atexit handler and no driver detach path runs, so no GL call can follow.
 *
 * THE COST IS STATED RATHER THAN HIDDEN: TerminateProcess documents that
 * "The state of global data maintained by dynamic-link libraries (DLLs) may be
 * compromised if TerminateProcess is used rather than ExitProcess"
 * (https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess),
 * and it flushes nothing. Both are acceptable here and neither is accidental:
 * the process is being ended because its GL state is already incoherent, no
 * file this build writes is more valuable than not drawing on an unknown
 * context, and the diagnostic's own last words are flushed below before the
 * call. On a non-Windows host the same requirement is met by _exit, which POSIX
 * defines as terminating without running atexit handlers.
 *
 * THE EXIT CODE IS THE DIAGNOSTIC'S CENSUS NUMBER so a launch that ended this
 * way is identifiable from a shell's exit status alone. */
#ifndef SL_AUX_TERMINATE_NOW
#  ifdef _WIN32
#    include <windows.h>
#    define SL_AUX_TERMINATE_NOW(code) \
         ((void) TerminateProcess(GetCurrentProcess(), (UINT) (code)))
#  else
#    include <unistd.h>
#    define SL_AUX_TERMINATE_NOW(code) _exit((int) (code))
#  endif
#endif

/* Which context is current, answered by the platform rather than inferred. 1
 * the main context is current, 0 it is not or the query failed.
 *
 * ctx == NULL is NOT main: a process with no main context cannot have it
 * current, and answering 0 is what keeps every caller below fail-closed in that
 * case instead of treating a missing context as a verified one. */
static int aux_main_is_current(void)
{
    return ctx != NULL && SDL_GL_GetCurrentContext() == ctx;
}

/* 1 the auxiliary context is current, 0 it is not or the query failed. */
static int aux_aux_is_current(void)
{
    return aux != NULL && SDL_GL_GetCurrentContext() == aux;
}

/* WHAT THE OBJECT-SHARING ATTRIBUTE HOLDS, answered by the platform. 1 the
 * value was read into *v, 0 it could not be read and *v means nothing.
 *
 * *v IS NOT TOUCHED ON FAILURE BY THIS FUNCTION, and callers never read it
 * then: an unreadable attribute is a fact to act on, not a value to substitute
 * a default for. */
static int aux_share_read(int *v)
{
    return SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, v) == 0;
}

/* ASK FOR A VALUE AND REPORT WHETHER THE ATTRIBUTE IS PROVED TO HOLD IT. 1 a
 * readback says it does, 0 it does not or could not be read.
 *
 * THE SET'S RETURN CODE IS DELIBERATELY DISCARDED. It answers a different
 * question from the one every caller has - what the attribute holds NOW - and
 * it answers that one wrongly in both directions: a reported failure may have
 * stored the value anyway and a reported success may not have stored it. The
 * readback is the only evidence either way, so it is the only thing consulted.
 * Treating a proved value as a success even when the call said otherwise is not
 * leniency: it is the same rule the rest of this file applies to MakeCurrent,
 * where the query and not the return code decides. */
static int aux_share_set_proven(int want)
{
    int now = 0;

    (void) SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, want);
    return aux_share_read(&now) && now == want;
}

/* END THIS PROCESS WITHOUT ISSUING - OR PERMITTING - ANY FURTHER GL CALL.
 *
 * Called only from the diagnostic's lost-context paths, and it DOES NOT RETURN
 * in the product. The last diagnostic lines are flushed first, because the
 * primitive this uses flushes nothing; everything after the primitive exists
 * for the test build, where the primitive is intercepted and does return, and
 * the caller's own refusal path then runs. */
void sl_gl_terminate_no_gl(int code);
void sl_gl_terminate_no_gl(int code)
{
    fflush(NULL);
    SL_AUX_TERMINATE_NOW(code);
}

/* CREATE THE AUXILIARY CONTEXT, with the main context VERIFIED on the way in
 * and on the way out.
 *
 * 1 a context exists and the MAIN context is verified current; 0 none is usable,
 * the MAIN context is verified current AND the object-sharing attribute is
 * either untouched or proved equal to what it was; SL_GL_AUX_LOST either which
 * context is current is unknown or that attribute could not be proved restored
 * after a write to it was attempted. Idempotent: a second call with one already
 * created verifies and answers without creating anything.
 *
 * 0 NEVER CARRIES AN UNPROVED SHARING ATTRIBUTE. That is the invariant this
 * function's ordering exists to hold: the attribute is proved before the
 * context is switched or judged, so a caller that reads 0 may go on issuing GL
 * knowing BOTH where it lands and what a later context creation in this process
 * would ask for.
 *
 * THE ENTRY CHECK IS NOT CEREMONY. The caller's contract is that the main
 * context is current, and this function is about to ask the platform to share
 * object names with WHATEVER IS CURRENT. If that is not the main context, the
 * context created would share the wrong namespace, the refusal the caller would
 * get from its own share check would name the wrong cause, and - the reason
 * this is a lost rather than a recoverable answer - a process whose current
 * context is not the one it believes cannot safely issue GL at all. */
int sl_gl_aux_create(void);
int sl_gl_aux_create(void)
{
    int share = 0;
    int now = 0;
    int restored;

    if (win == NULL || ctx == NULL) {
        /* NO WINDOW OR NO MAIN CONTEXT AT ALL. Nothing was created, nothing was
         * switched, and there is no main context to verify - so this is a
         * refusal and not a loss: the caller's GL state is whatever it was,
         * which is what 0 means everywhere below. */
        return 0;
    }
    if (!aux_main_is_current()) {
        fprintf(stderr, "sightline gfx: the auxiliary GL context was NOT "
                        "created: the MAIN context is not current at the "
                        "request (%s), so which context a GL command would "
                        "reach is unknown and sharing object names with the "
                        "current one would share the wrong namespace\n",
                SDL_GetError());
        return SL_GL_AUX_LOST;
    }
    if (aux != NULL) return 1;
    /* THE ATTRIBUTE'S PREVIOUS VALUE IS READ SO IT CAN BE PUT BACK, and a read
     * that fails REFUSES rather than assuming the documented default: the value
     * belongs to the process, this function is about to overwrite it, and a
     * guess here would be restored over whatever a later context creation
     * needed. */
    if (!aux_share_read(&share)) {
        fprintf(stderr, "sightline gfx: the auxiliary GL context was NOT "
                        "created: the object-sharing attribute's previous "
                        "value could not be READ (%s), so it could not be "
                        "restored afterwards and is left untouched. The main "
                        "context is current and unchanged\n", SDL_GetError());
        return 0;
    }
    /* THE REQUEST, AND THE READBACK THAT SAYS WHAT IT ACTUALLY DID. From here
     * to the restore below, a write to process-wide state has been ATTEMPTED,
     * so no path may leave without the attribute's value established. */
    (void) SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    if (!aux_share_read(&now)) {
        /* THE ATTRIBUTE WAS WRITTEN TO AND CANNOT BE READ. Nothing was created
         * and nothing can be put back either, because a restore would also be
         * unprovable: this process can no longer say what configuration its
         * next context creation would use. That is not recoverable by
         * refusing - a refusal would hand normal rendering a process whose
         * context-creation state is unknown - so it is the lost outcome. */
        fprintf(stderr, "sightline gfx: the object-sharing attribute was asked "
                        "to change and its value can no longer be READ (%s). "
                        "Whether this process now requests shared object names "
                        "for every later GL context CANNOT BE ESTABLISHED, and "
                        "an unprovable process-wide GL configuration may not be "
                        "carried into normal rendering or a GL teardown\n",
                SDL_GetError());
        return SL_GL_AUX_LOST;
    }
    if (now != 1) {
        /* THE REQUEST IS NOT IN EFFECT, whatever the call returned, so no
         * context is created: one without shared object names would be a
         * different experiment. The attribute's value is known here, which is
         * what makes this a refusal - but it is known to be one of two things,
         * and only one of them needs undoing. */
        if (now != share && !aux_share_set_proven(share)) {
            /* MUTATED TO SOMETHING ELSE AND NOT PROVABLY PUT BACK. */
            fprintf(stderr, "sightline gfx: the object-sharing attribute was "
                            "changed to a value that was neither asked for nor "
                            "the previous one, and it could not be proved "
                            "restored (%s). No auxiliary GL context was "
                            "created, but every later GL context this process "
                            "creates may now request shared object names - "
                            "that cannot be undone from here\n",
                    SDL_GetError());
            return SL_GL_AUX_LOST;
        }
        fprintf(stderr, "sightline gfx: the auxiliary GL context cannot be "
                        "asked to SHARE objects (%s), so none is created - a "
                        "context without shared names would be a different "
                        "experiment. The object-sharing attribute is PROVED to "
                        "hold its previous value, and the main context is "
                        "current and unchanged\n", SDL_GetError());
        return 0;
    }
    aux = SDL_GL_CreateContext(win);
    /* PUT THE ATTRIBUTE BACK IMMEDIATELY AND PROVE IT BY READBACK, and keep
     * that answer: it decides below whether this process may continue at all.
     * The attribute is process-wide requested state rather than a property of
     * either context, so which one the create left current does not affect
     * this. */
    restored = aux_share_set_proven(share);
    if (!restored) {
        /* THE PROCESS-WIDE ATTRIBUTE IS NOT PROVABLY WHAT IT WAS. This is the
         * lost outcome whether or not a context was created, and nothing
         * further is called on either account: no switch, no destroy, no
         * query. A destroy would not help - the attribute, not the context, is
         * what cannot be accounted for - and the caller's termination path is
         * reached before any further GL precisely so that no later context
         * creation in this process can use a configuration nobody can state.
         * A context that exists is left to the end of the process, which is
         * immediate. */
        fprintf(stderr, "sightline gfx: the object-sharing attribute's "
                        "previous value could not be PROVED RESTORED (%s): a "
                        "readback does not agree with it, or could not be made "
                        "at all. Every later GL context this process creates "
                        "may request shared object names, which cannot be "
                        "undone from here, so this process may not go on to "
                        "normal rendering or a GL teardown%s\n",
                SDL_GetError(),
                aux != NULL ? ". An auxiliary context was created and is NOT "
                              "destroyed: nothing further is called"
                            : ". No auxiliary context was created");
        return SL_GL_AUX_LOST;
    }
    if (aux == NULL) {
        /* NOTHING WAS CREATED. What is current after a failed create is not
         * documented either, so it is queried rather than assumed. */
        if (!aux_main_is_current()) {
            fprintf(stderr, "sightline gfx: the auxiliary GL context was not "
                            "created AND the MAIN context is no longer current "
                            "(%s), so which context a GL command would reach "
                            "is unknown\n", SDL_GetError());
            return SL_GL_AUX_LOST;
        }
        fprintf(stderr, "sightline gfx: the auxiliary GL context was not "
                        "created (%s); the main context is verified current "
                        "and unchanged\n", SDL_GetError());
        return 0;
    }
    /* THE CREATE MADE THE NEW CONTEXT CURRENT - that is the platform's
     * behaviour, not a choice - so the main context is restored at once, before
     * any caller can issue a command believing otherwise. The restore is driven
     * through the same verified switch every other caller uses. */
    if (sl_gl_main_make_current() != 1) {
        /* THE SWITCH BACK FAILED AND THE MAIN CONTEXT IS NOT CURRENT. The
         * context is deliberately NOT destroyed: a destroy needs a verified
         * main context, which is exactly what could not be established.
         * sl_gl_main_make_current has already said so on stderr. */
        return SL_GL_AUX_LOST;
    }
    /* THE ATTRIBUTE WAS ALREADY PROVED RESTORED ABOVE, before this switch, so
     * there is no attribute case left to handle here: the only way past that
     * gate is with the process-wide value proved equal to what it was. That
     * ordering is deliberate. An unproved attribute is terminal and a destroy
     * cannot mend it, so it is settled before anything else is attempted;
     * reaching this line means the single remaining question - which context is
     * current - has just been answered by the verified switch above. */
    return 1;
}

/* MAKE THE AUXILIARY CONTEXT CURRENT. 1 it is verified current; 0 it is not and
 * the MAIN context is verified current - which is why that failure is
 * recoverable; SL_GL_AUX_LOST neither could be established. */
int sl_gl_aux_make_current(void);
int sl_gl_aux_make_current(void)
{
    if (aux == NULL || win == NULL) {
        /* NOTHING TO SWITCH TO, so nothing was switched - but "the main context
         * is still current" is a claim, so it is verified before being made. */
        if (aux_main_is_current()) return 0;
        fprintf(stderr, "sightline gfx: the auxiliary GL context could not be "
                        "made current (there is none) AND the MAIN context is "
                        "not current either (%s), so which context a GL "
                        "command would reach is unknown\n", SDL_GetError());
        return SL_GL_AUX_LOST;
    }
    if (SDL_GL_MakeCurrent(win, aux) != 0) {
        if (aux_main_is_current()) {
            fprintf(stderr, "sightline gfx: the auxiliary GL context could not "
                            "be made current (%s); the MAIN context is "
                            "verified current and GL commands still reach "
                            "it\n", SDL_GetError());
            return 0;
        }
        if (aux_aux_is_current()) {
            /* THE CALL REPORTED FAILURE AND THE QUERY SAYS OTHERWISE. The
             * query is the authority on which context is current - see the rule
             * at the top of this file - so this is a success, reported because
             * the disagreement is worth a reader's attention. */
            fprintf(stderr, "sightline gfx: the switch to the auxiliary GL "
                            "context reported failure (%s) but that context is "
                            "VERIFIED current; the query decides\n",
                    SDL_GetError());
            return 1;
        }
        fprintf(stderr, "sightline gfx: the switch to the auxiliary GL context "
                        "failed and NEITHER context is current (%s), so which "
                        "context a GL command would reach is unknown\n",
                SDL_GetError());
        return SL_GL_AUX_LOST;
    }
    if (!aux_aux_is_current()) {
        /* A SUCCESSFUL SWITCH WHOSE QUERY DISAGREES. Both cannot be true, and
         * the one that would permit issuing GL on an unidentified context is
         * the one that must not be believed. */
        if (aux_main_is_current()) {
            fprintf(stderr, "sightline gfx: the switch to the auxiliary GL "
                            "context reported success but the MAIN context is "
                            "the one that is current (%s); the query decides "
                            "and nothing is issued on the auxiliary one\n",
                    SDL_GetError());
            return 0;
        }
        fprintf(stderr, "sightline gfx: the switch to the auxiliary GL context "
                        "reported success and NO known context is current "
                        "(%s), so which context a GL command would reach is "
                        "unknown\n", SDL_GetError());
        return SL_GL_AUX_LOST;
    }
    return 1;
}

/* MAKE THE MAIN CONTEXT CURRENT. 1 it is verified current; SL_GL_AUX_LOST it is
 * not, which is not recoverable - nothing after it can know which context a GL
 * command would reach.
 *
 * THERE IS NO 0 HERE, deliberately. This function is called only while the
 * auxiliary context is current, so every outcome other than a verified main
 * context is the unknown one, and giving that outcome two spellings would let a
 * caller read one of them as recoverable. */
int sl_gl_main_make_current(void);
int sl_gl_main_make_current(void)
{
    if (ctx == NULL || win == NULL) {
        fprintf(stderr, "sightline gfx: the MAIN GL context cannot be made "
                        "current - this process has no window or no main "
                        "context - so which context a GL command would reach "
                        "is unknown\n");
        return SL_GL_AUX_LOST;
    }
    if (SDL_GL_MakeCurrent(win, ctx) != 0) {
        if (aux_main_is_current()) {
            /* REPORTED FAILURE, VERIFIED CURRENT. The query decides; the
             * disagreement is reported. */
            fprintf(stderr, "sightline gfx: the switch back to the MAIN GL "
                            "context reported failure (%s) but that context is "
                            "VERIFIED current; the query decides\n",
                    SDL_GetError());
            return 1;
        }
        fprintf(stderr, "sightline gfx: THE MAIN GL CONTEXT COULD NOT BE MADE "
                        "CURRENT AGAIN (%s). Which context a GL command would "
                        "reach is now unknown, so no further GL command may be "
                        "issued by this process\n", SDL_GetError());
        return SL_GL_AUX_LOST;
    }
    if (!aux_main_is_current()) {
        fprintf(stderr, "sightline gfx: the switch back to the MAIN GL context "
                        "reported success and that context is NOT current "
                        "(%s). Which context a GL command would reach is "
                        "unknown, so no further GL command may be issued by "
                        "this process\n", SDL_GetError());
        return SL_GL_AUX_LOST;
    }
    return 1;
}

/* DESTROY THE AUXILIARY CONTEXT. 1 it was destroyed, 0 there was none,
 * SL_GL_AUX_LOST it still exists and was NOT destroyed because the precondition
 * could not be verified.
 *
 * THE MAIN CONTEXT MUST BE CURRENT AT THIS CALL and the window must still
 * exist. That used to be a precondition on the caller alone; it is CHECKED here
 * now, because destroying a context while it is the current one would leave
 * this thread with none and the caller cannot prove which context is current
 * any better than this file can. A failed check destroys nothing and says the
 * resource is left to the process's end. */
int sl_gl_aux_destroy(void);
int sl_gl_aux_destroy(void)
{
    if (aux == NULL) return 0;
    if (win == NULL || !aux_main_is_current()) {
        fprintf(stderr, "sightline gfx: the auxiliary GL context was NOT "
                        "destroyed: the MAIN context is not verified current "
                        "(%s), and deleting a context from a thread that may "
                        "have it current would leave that thread with none. "
                        "The context is left to the end of the process\n",
                SDL_GetError());
        return SL_GL_AUX_LOST;
    }
    SDL_GL_DeleteContext(aux);
    aux = NULL;
    return 1;
}
