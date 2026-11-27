/* The BACKEND seam for the auxiliary GL context, driven against a scripted SDL.
 *
 * WHAT THIS COVERS THAT NOTHING ELSE DOES. The two fixtures for
 * src/gfx/sl_gfx_bgtf.c supply their own four backend functions, so everything
 * they prove is about the MODULE's reaction to answers a shim invented. The
 * answers themselves - what the real backend returns after a create, after a
 * switch that reported failure, after a switch that reported success, when the
 * object-sharing attribute cannot be read or put back, and when a destroy's
 * precondition cannot be verified - were tested by nothing at all. This file
 * compiles THE PRODUCT'S OWN TEXT for those functions, src/gfx/sl_gfx_sdl_aux.inc
 * exactly as src/gfx/sl_gfx_sdl.c includes it, against an SDL whose every answer
 * is scripted, and asserts the result the module is written to.
 *
 * WHY A TEXTUAL INCLUDE RATHER THAN A LINK. The seam uses the window and the
 * main context, which are static to the backend's translation unit, so it cannot
 * be linked against from outside; and the product must not gain a second
 * compilation mode to be testable. Including the same text is what makes this a
 * test OF the product rather than of a copy: there is no second definition to
 * drift, and an edit to the seam that broke a contract fails here.
 *
 * NOTHING REAL IS TOUCHED. No SDL header, no SDL library, no GL, no window and
 * no context: every SDL entry point below is a function in this file, the
 * terminate primitive is a counter, and the whole run is one process doing
 * arithmetic. What this file therefore does NOT establish is anything about a
 * real driver - whether SDL_GL_MakeCurrent ever fails on a given platform, what
 * it leaves current when it does, or whether a second context on one window
 * shares object names. Those are the reasons the fail-closed paths exist, and
 * they remain unmeasured; what is measured here is that each documented answer
 * produces the documented result.
 *
 * WHERE THE TERMINATION ITSELF IS PROVED, AND NOT HERE. The seam's answer to an
 * unaccountable GL state is SL_GL_AUX_LOST; ending the process is the MODULE's
 * response to that answer, and tools/profiling/native/test_sl_gfx_bgtf.c proves
 * it with its own backend shim - a create that answers LOST reaches the
 * terminate primitive exactly once and refuses every later draw. So what this
 * file establishes about the lost outcome is the half it owns: that each
 * unprovable platform answer produces LOST and NEVER a recoverable 0. The two
 * halves together are the chain from a readback that disagrees to a process
 * that stops; neither fixture covers both ends.
 */
#include <stdio.h>
#include <string.h>

/* ---- THE SCRIPTED SDL ---------------------------------------------------
 *
 * Only the entry points the seam uses, with every answer under the test's
 * control. The TYPES match SDL2's shape where the seam depends on it:
 * SDL_GLContext is an opaque pointer and SDL_Window is an opaque struct, so a
 * context value compares by identity exactly as it does in the product. */
typedef struct SDL_Window SDL_Window;
typedef void *SDL_GLContext;

typedef enum {
    SDL_GL_SHARE_WITH_CURRENT_CONTEXT = 1,
    SDL_GL_OTHER_ATTRIBUTE_FOR_THE_TEST = 2
} SDL_GLattr;

/* WHAT A SWITCH ACTUALLY DID, which is the whole point of the backend asking
 * instead of reading a return code. The platform documents a success or failure
 * return and promises nothing about what remains current, so the fake keeps the
 * two independent: `rc` is what the call RETURNS and `lands` is what the current
 * context BECOMES. Every combination the backend has to handle is then one line
 * in a scenario. */
#define MC_LANDS_REQUESTED 0       /* the requested context becomes current  */
#define MC_LANDS_UNCHANGED 1       /* whatever was current stays current     */
#define MC_LANDS_NONE      2       /* nothing is current afterwards          */
#define MC_LANDS_OTHER     3       /* the OTHER known context becomes current*/

#define SCRIPT_MAX 8

static struct {
    SDL_GLContext cur;             /* what the platform says is current      */
    SDL_GLContext made;            /* the context the next create hands back */
    int  create_null;              /* the create fails                       */
    int  create_makes_current;     /* the create makes its context current   */
    int  getattr_fail;             /* reading an attribute fails, from the   */
                                   /* very first read                        */
    int  getattr_fail_after;       /* -1 never, else reads after this many    */
                                   /* have been MADE all fail - which is how  */
                                   /* a readback is made to fail only AFTER   */
                                   /* a write has already been attempted      */
    int  setattr_fail_on;          /* -1 never, else fail setting this value */
    int  setattr_mutates_on_fail;  /* a set that REPORTS failure stores the  */
                                   /* value anyway - the platform's return   */
                                   /* code and its effect are independent    */
    int  setattr_wrong_on;         /* -1 never, else setting this value      */
                                   /* stores setattr_wrong_value instead,    */
                                   /* and REPORTS success                    */
    int  setattr_wrong_value;      /* what it stores in that case            */
    int  attr_share;               /* the stored attribute                   */
    int  mc_rc[SCRIPT_MAX];        /* per-call return codes                  */
    int  mc_lands[SCRIPT_MAX];     /* per-call landing                       */
    int  mc_n;                     /* how many of the two above are scripted */
    int  mc_i;                     /* the next one to use                    */
    long creates, deletes, mc_calls, getattrs, setattrs;
    int  last_set_attr, last_set_value;
    SDL_GLContext last_deleted;
    long term_calls;
    int  term_code;
    const char *err;
} fk;

static void fake_reset(void)
{
    memset(&fk, 0, sizeof fk);
    fk.create_makes_current = 1;   /* the platform's behaviour, not a choice */
    fk.setattr_fail_on = -1;
    fk.setattr_wrong_on = -1;
    fk.getattr_fail_after = -1;
    fk.err = "scripted";
}

static const char *SDL_GetError(void)
{
    return fk.err;
}

static SDL_GLContext SDL_GL_CreateContext(SDL_Window *w)
{
    (void) w;
    fk.creates++;
    if (fk.create_null) return NULL;
    if (fk.create_makes_current) fk.cur = fk.made;
    return fk.made;
}

static void SDL_GL_DeleteContext(SDL_GLContext c)
{
    fk.deletes++;
    fk.last_deleted = c;
    if (fk.cur == c) fk.cur = NULL;
}

static SDL_GLContext SDL_GL_GetCurrentContext(void)
{
    return fk.cur;
}

static int SDL_GL_MakeCurrent(SDL_Window *w, SDL_GLContext c)
{
    int rc = 0, lands = MC_LANDS_REQUESTED;
    const SDL_GLContext was = fk.cur;

    (void) w;
    fk.mc_calls++;
    if (fk.mc_i < fk.mc_n) {
        rc = fk.mc_rc[fk.mc_i];
        lands = fk.mc_lands[fk.mc_i];
        fk.mc_i++;
    }
    switch (lands) {
    case MC_LANDS_UNCHANGED: break;
    case MC_LANDS_NONE:      fk.cur = NULL; break;
    case MC_LANDS_OTHER:     fk.cur = (was == fk.made) ? NULL : fk.made; break;
    default:                 fk.cur = c; break;
    }
    return rc;
}

static int SDL_GL_GetAttribute(SDL_GLattr a, int *v)
{
    fk.getattrs++;
    if (fk.getattr_fail) return -1;
    if (fk.getattr_fail_after >= 0 && fk.getattrs > fk.getattr_fail_after)
        return -1;
    if (a == SDL_GL_SHARE_WITH_CURRENT_CONTEXT) *v = fk.attr_share;
    else *v = 0;
    return 0;
}

/* THE RETURN CODE AND THE EFFECT ARE SEPARATE HERE, exactly as they are for
 * MakeCurrent above and for the same reason: the platform's two answers are
 * independent and the seam is written to the one that can be observed. `rc` is
 * what the call RETURNS and the store is what the attribute BECOMES, so a
 * failure that mutated and a success that stored something else are each one
 * line in a scenario. */
static int SDL_GL_SetAttribute(SDL_GLattr a, int v)
{
    int store = v, rc = 0;

    fk.setattrs++;
    fk.last_set_attr = (int) a;
    fk.last_set_value = v;
    if (fk.setattr_wrong_on >= 0 && v == fk.setattr_wrong_on)
        store = fk.setattr_wrong_value;
    if (fk.setattr_fail_on >= 0 && v == fk.setattr_fail_on) {
        rc = -1;
        if (!fk.setattr_mutates_on_fail) return rc;
    }
    if (a == SDL_GL_SHARE_WITH_CURRENT_CONTEXT) fk.attr_share = store;
    return rc;
}

/* ---- THE BACKEND'S OWN STATICS, AS ITS TRANSLATION UNIT HAS THEM -------- */
static SDL_Window *win;
static SDL_GLContext ctx;

/* ---- AND THE TERMINATE PRIMITIVE, INTERCEPTED --------------------------
 *
 * The product's expands to TerminateProcess, which does not return; pre-defining
 * it here is the one thing this file changes about the text it compiles, and it
 * is the only way a test can observe that the path was reached. */
#define SL_AUX_TERMINATE_NOW(code) \
    ((void) (fk.term_calls++, fk.term_code = (int) (code)))

/* THE TEXT UNDER TEST. The path is overridable for ONE purpose: a negative
 * control compiles this same file against a deliberately broken COPY of the
 * seam, in a scratch directory, and must FAIL. Left undefined - which is every
 * ordinary run - it is the product's own file, so a passing run is a statement
 * about the product and not about a copy. */
#ifndef SL_AUX_INC_PATH
#define SL_AUX_INC_PATH "../../../src/gfx/sl_gfx_sdl_aux.inc"
#endif
#include SL_AUX_INC_PATH

/* ---- THE HARNESS ------------------------------------------------------- */
static long checks, failed;
static const char *g_case = "";

static void CHECK(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failed++;
        printf("    FAIL [%s] %s\n", g_case, what);
    }
}

static void begin(const char *name)
{
    g_case = name;
    printf("  case: %s\n", name);
}

/* The two context values every scenario uses. Distinct, non-NULL, and never
 * dereferenced - identity is all the seam compares. */
static int main_ctx_obj, aux_ctx_obj;
#define MAIN_CTX ((SDL_GLContext) &main_ctx_obj)
#define AUX_CTX  ((SDL_GLContext) &aux_ctx_obj)
#define SOME_WINDOW ((SDL_Window *) &main_ctx_obj)

/* A backend with a window, a main context, and the main context current - which
 * is the state every caller of the seam is required to be in. */
static void arm_backend(void)
{
    fake_reset();
    fk.made = AUX_CTX;
    fk.cur  = MAIN_CTX;
    win = SOME_WINDOW;
    ctx = MAIN_CTX;
    aux = NULL;
}

static void script(int n, const int *rc, const int *lands)
{
    int i;
    fk.mc_n = n;
    fk.mc_i = 0;
    for (i = 0; i < n && i < SCRIPT_MAX; i++) {
        fk.mc_rc[i] = rc[i];
        fk.mc_lands[i] = lands[i];
    }
}

/* ---- THE SCENARIOS ----------------------------------------------------- */

/* THE HEALTHY PATH, AND THE ATTRIBUTE'S ROUND TRIP. The create must leave the
 * main context verified current and must put the object-sharing attribute back
 * to the value it read, not to a default: the attribute belongs to the process
 * and every later context creation reads it. */
static void t_create_ok(void)
{
    int v = -1;
    begin("create: a sharing context is made and the MAIN context is verified");
    arm_backend();
    fk.attr_share = 0;
    CHECK(sl_gl_aux_create() == 1, "the create answers 1");
    CHECK(aux == AUX_CTX, "and holds the context the platform handed back");
    CHECK(fk.cur == MAIN_CTX, "the MAIN context is current afterwards");
    CHECK(fk.creates == 1 && fk.deletes == 0, "one context was created, none deleted");
    CHECK(fk.setattrs == 2, "and it was set twice: to 1, then back");
    CHECK(fk.getattrs == 3,
          "and READ three times: the previous value, then once after EACH of "
          "the two writes - a write whose effect is not read back is a write "
          "this process cannot account for");
    (void) SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, &v);
    CHECK(v == 0, "the attribute is back to the value it had, not to a default");
    CHECK(fk.term_calls == 0, "and nothing terminated");
    /* AND IT IS IDEMPOTENT, still with a verified main context. */
    CHECK(sl_gl_aux_create() == 1, "a second create answers 1");
    CHECK(fk.creates == 1, "without creating anything");
}

/* THE ATTRIBUTE'S PRIOR VALUE OF 1 IS ALSO RESTORED AS 1. The failure this
 * guards is a restore that writes a constant: with a prior value of 0 that is
 * indistinguishable from correct, so the case is driven from 1. */
static void t_create_restores_one(void)
{
    int v = -1;
    begin("create: a previous attribute value of 1 is restored as 1");
    arm_backend();
    fk.attr_share = 1;
    CHECK(sl_gl_aux_create() == 1, "the create answers 1");
    (void) SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, &v);
    CHECK(v == 1, "and the attribute is 1 again, which a constant 0 would not be");
}

/* THE ENTRY CHECK. The caller's contract is that the main context is current,
 * and the create is about to ask for sharing with WHATEVER IS CURRENT - so a
 * current context that is not the main one is not a refusal but the unknown
 * outcome: nothing may be created, and nothing may be issued either. */
static void t_create_not_main(void)
{
    begin("create: the MAIN context is not current at the request");
    arm_backend();
    fk.cur = AUX_CTX;
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST, "the create answers LOST");
    CHECK(fk.creates == 0, "nothing was created");
    CHECK(fk.setattrs == 0 && fk.getattrs == 0,
          "and the process-wide attribute was neither read nor written");
    begin("create: no context is current at the request");
    arm_backend();
    fk.cur = NULL;
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "a NULL answer from the query is not the main context either");
    CHECK(fk.creates == 0, "and nothing was created");
}

/* NO WINDOW OR NO MAIN CONTEXT AT ALL. Nothing was created and nothing was
 * switched, so this is a refusal: the caller's GL state is whatever it was. */
static void t_create_no_backend(void)
{
    begin("create: this backend has no window or no main context");
    arm_backend();
    win = NULL;
    CHECK(sl_gl_aux_create() == 0, "with no window the create refuses");
    arm_backend();
    ctx = NULL;
    CHECK(sl_gl_aux_create() == 0, "with no main context it refuses too");
    CHECK(fk.creates == 0, "and neither created anything");
}

/* THE ATTRIBUTE THAT CANNOT BE READ, AND THE ONE THAT CANNOT BE SET. Both
 * refuse with the main context verified current, and neither creates anything -
 * a read that failed would otherwise be restored as a guess. */
static void t_create_attr(void)
{
    begin("create: the attribute's previous value cannot be READ");
    arm_backend();
    fk.getattr_fail = 1;
    CHECK(sl_gl_aux_create() == 0, "the create refuses");
    CHECK(fk.creates == 0, "nothing was created");
    CHECK(fk.setattrs == 0, "and the attribute was not written at all");
    CHECK(fk.cur == MAIN_CTX, "the MAIN context is still current");

    begin("create: the attribute cannot be SET to 1 and did not change");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_fail_on = 1;        /* reports failure AND stores nothing      */
    CHECK(sl_gl_aux_create() == 0, "the create refuses");
    CHECK(fk.creates == 0, "nothing was created");
    CHECK(fk.cur == MAIN_CTX, "and the MAIN context is still current");
    CHECK(fk.attr_share == 0,
          "the attribute is still the value it had - which is what makes this "
          "refusal recoverable, and is READ rather than inferred from the "
          "failure");
    CHECK(fk.getattrs == 2,
          "it was read before the write and again after it: the refusal rests "
          "on the second read, not on the return code");
    CHECK(fk.setattrs == 1,
          "and nothing was put back, because nothing had changed");
}

/* THE SET THAT REPORTS FAILURE AND MUTATES ANYWAY - the one shape a return-code
 * reading gets exactly backwards. SDL2 stores this attribute in
 * gl_config.share_with_current_context and a query returns that same field, so
 * the request is IN EFFECT here however the call answered; believing the return
 * code would refuse with the process-wide setting left at 1, which is the
 * failure this case exists to catch. */
static void t_create_set_fails_but_mutated(void)
{
    begin("create: Set(1) reports failure but the attribute DID become 1");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_fail_on = 1;
    fk.setattr_mutates_on_fail = 1;
    CHECK(sl_gl_aux_create() == 1,
          "the create PROCEEDS: the readback says sharing was requested, and "
          "the readback is the evidence");
    CHECK(fk.creates == 1, "a context was created");
    CHECK(fk.attr_share == 0,
          "and the attribute is back to its previous value - a refusal that "
          "trusted the return code would have left it at 1 for every later "
          "context creation in this process");
    CHECK(fk.cur == MAIN_CTX, "the MAIN context is current");
    CHECK(fk.term_calls == 0, "and nothing terminated: every value was proved");

    begin("create: Set(1) reports failure, mutates, and the create then fails");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_fail_on = 1;
    fk.setattr_mutates_on_fail = 1;
    fk.create_null = 1;
    CHECK(sl_gl_aux_create() == 0,
          "the create was ATTEMPTED and refused, which is a recoverable 0");
    CHECK(fk.creates == 1, "the attempt happened");
    CHECK(fk.attr_share == 0, "and the attribute is proved back to 0");
    CHECK(fk.cur == MAIN_CTX, "with the MAIN context verified current");
}

/* THE SET THAT REPORTS SUCCESS AND STORES SOMETHING ELSE. Two sub-shapes: the
 * stored value happens to be the original, which needs no undoing, and a third
 * value, which does. Both refuse rather than terminate, because in both the
 * attribute's value is READ and PROVED equal to what it was. */
static void t_create_set_succeeds_wrong_value(void)
{
    begin("create: Set(1) reports success but the attribute stayed at its "
          "previous value");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_wrong_on = 1;
    fk.setattr_wrong_value = 0;
    CHECK(sl_gl_aux_create() == 0,
          "the create refuses: sharing is not in effect, so a context would be "
          "a different experiment");
    CHECK(fk.creates == 0, "nothing was created");
    CHECK(fk.attr_share == 0, "the attribute holds its previous value");
    CHECK(fk.setattrs == 1,
          "and nothing was put back - it was proved unchanged, so there was "
          "nothing to undo");
    CHECK(fk.term_calls == 0, "nothing terminated");

    begin("create: Set(1) reports success and the attribute became a THIRD "
          "value, which is then put back");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_wrong_on = 1;
    fk.setattr_wrong_value = 7;    /* neither asked for nor the previous value */
    CHECK(sl_gl_aux_create() == 0, "the create refuses");
    CHECK(fk.creates == 0, "nothing was created");
    CHECK(fk.attr_share == 0,
          "and the stray value was restored to the previous one and PROVED");
    CHECK(fk.setattrs == 2, "which took a second write");
    CHECK(fk.term_calls == 0, "nothing terminated: the value is known");

    begin("create: the attribute became a THIRD value and CANNOT be put back");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_wrong_on = 1;
    fk.setattr_wrong_value = 7;
    fk.setattr_fail_on = 0;        /* putting 0 back reports failure, no store */
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "the create answers LOST: the process-wide setting is neither what "
          "was asked for nor what it was, and that is not a state to hand back "
          "to normal rendering as a recoverable refusal");
    CHECK(fk.creates == 0, "nothing was created");
    CHECK(aux == NULL, "and nothing is held");
}

/* THE READBACK THAT FAILS AFTER A WRITE HAS BEEN ATTEMPTED. Distinct from the
 * read that fails BEFORE one: there, nothing had been touched and a refusal
 * leaves the process as it was; here the attribute may or may not have changed
 * and no call can settle it, so the answer is LOST and not a refusal. */
static void t_create_readback_fails_after_write(void)
{
    begin("create: the readback fails after Set(1)");
    arm_backend();
    fk.attr_share = 0;
    fk.getattr_fail_after = 1;     /* the prior value reads; nothing after it */
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "the create answers LOST, never a recoverable 0: whether this "
          "process now requests shared object names cannot be established");
    CHECK(fk.creates == 0, "nothing was created");
    CHECK(fk.setattrs == 1,
          "and no restore was attempted - it could not have been proved "
          "either");
    CHECK(aux == NULL, "nothing is held");
}

/* THE RESTORE THAT CANNOT BE PROVED, ON BOTH PATHS. This is the correction's
 * centre: every one of these shapes used to answer 0, which told the caller it
 * could go on drawing while the process-wide context-creation setting was
 * changed or unknown. A created context is NOT destroyed on these paths - the
 * attribute and not the context is what cannot be accounted for, and nothing
 * further is called at all. */
static void t_create_restore_unproved(void)
{
    begin("create: the restore reports failure and the value did NOT go back");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_fail_on = 0;        /* setting 1 works, putting 0 back does not */
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "the create answers LOST rather than refusing with 0 - normal "
          "rendering may not resume with a changed sharing setting");
    CHECK(fk.creates == 1, "a context was created");
    CHECK(fk.deletes == 0,
          "and it is NOT destroyed: a destroy cannot mend a process-wide "
          "attribute, and nothing further is called");
    CHECK(aux == AUX_CTX, "it is still held, and left to the end of the process");
    CHECK(fk.mc_calls == 0,
          "no context switch was attempted either - the attribute is settled "
          "before anything else is");
    CHECK(fk.attr_share == 1, "the attribute is demonstrably still the wrong value");

    begin("create: the restore reports SUCCESS and stored the wrong value");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_wrong_on = 0;
    fk.setattr_wrong_value = 7;    /* putting 0 back "succeeds" and stores 7  */
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "the create answers LOST: a success the readback contradicts is not "
          "a restored attribute");
    CHECK(fk.creates == 1 && fk.deletes == 0, "the context is created and kept");
    CHECK(fk.mc_calls == 0, "and nothing further is called");

    begin("create: the readback fails after the restore attempt");
    arm_backend();
    fk.attr_share = 0;
    fk.getattr_fail_after = 2;     /* the prior value and the post-set value  */
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "the create answers LOST: the restore may well have worked - here it "
          "did - but an attribute that cannot be READ cannot be proved, and "
          "unproved is the fail-closed answer");
    CHECK(fk.creates == 1 && fk.deletes == 0, "the context is created and kept");
    CHECK(fk.setattrs == 2, "the restore WAS attempted");
    CHECK(fk.mc_calls == 0, "and nothing further is called");

    begin("create: the restore reports failure but the value IS back");
    arm_backend();
    fk.attr_share = 0;
    fk.setattr_fail_on = 0;
    fk.setattr_mutates_on_fail = 1;   /* reports failure, stores 0 anyway     */
    CHECK(sl_gl_aux_create() == 1,
          "the create SUCCEEDS: a value PROVED equal to the original is "
          "restored whatever the call returned - the same rule the switches "
          "follow, where the query and not the return code decides");
    CHECK(fk.creates == 1 && fk.deletes == 0, "the context is created and kept");
    CHECK(aux == AUX_CTX, "and is held for use");
    CHECK(fk.attr_share == 0, "the attribute is back to its previous value");
    CHECK(fk.cur == MAIN_CTX, "with the MAIN context verified current");
    CHECK(fk.term_calls == 0, "and nothing terminated");
}

/* THE SAME THREE RESTORE SHAPES ON THE FAILED-CREATE PATH, where a verified
 * main context used to be enough to answer 0. It is not: the context that was
 * not created is the lesser half of the question, and the process-wide
 * attribute is the half that outlives this function. */
static void t_create_null_restore_unproved(void)
{
    begin("create: the platform refuses AND the restore cannot be proved");
    arm_backend();
    fk.attr_share = 0;
    fk.create_null = 1;
    fk.setattr_fail_on = 0;
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "the create answers LOST although the MAIN context never moved: a "
          "verified main context does not make an unknown sharing setting "
          "recoverable");
    CHECK(fk.cur == MAIN_CTX,
          "and the main context IS current, which is exactly why answering 0 "
          "here would be wrong rather than merely unlucky");
    CHECK(aux == NULL, "nothing is held");
    CHECK(fk.deletes == 0, "and nothing was destroyed");

    begin("create: the platform refuses AND the restore stored the wrong value");
    arm_backend();
    fk.attr_share = 0;
    fk.create_null = 1;
    fk.setattr_wrong_on = 0;
    fk.setattr_wrong_value = 7;
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST, "the create answers LOST");
    CHECK(aux == NULL, "nothing is held");

    begin("create: the platform refuses AND the readback fails afterwards");
    arm_backend();
    fk.attr_share = 0;
    fk.create_null = 1;
    fk.getattr_fail_after = 2;
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST, "the create answers LOST");
    CHECK(aux == NULL, "nothing is held");

    begin("create: the platform refuses and the restore is PROVED despite a "
          "failure return");
    arm_backend();
    fk.attr_share = 0;
    fk.create_null = 1;
    fk.setattr_fail_on = 0;
    fk.setattr_mutates_on_fail = 1;
    CHECK(sl_gl_aux_create() == 0,
          "the create REFUSES, recoverably: the attribute is proved back and "
          "the main context is verified current");
    CHECK(fk.attr_share == 0, "the attribute holds its previous value");
    CHECK(fk.cur == MAIN_CTX, "and the MAIN context is current");
    CHECK(fk.term_calls == 0, "nothing terminated");
}

/* THE CREATE THAT FAILS, with the main context still current and with it not. */
static void t_create_null(void)
{
    begin("create: the platform refuses, MAIN context still current");
    arm_backend();
    fk.create_null = 1;
    CHECK(sl_gl_aux_create() == 0, "the create refuses");
    CHECK(aux == NULL, "nothing is held");
    CHECK(fk.cur == MAIN_CTX, "and the MAIN context is verified current");
    CHECK(fk.deletes == 0, "with nothing to destroy");

    begin("create: the platform refuses AND the MAIN context is gone");
    arm_backend();
    fk.create_null = 1;
    fk.cur = NULL;                 /* set after arming: the failed create lost it */
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "a failed create whose aftermath cannot be verified is LOST, not a "
          "refusal - what a failed create leaves current is not documented");
}

/* THE CREATE'S OWN SWITCH BACK. Three outcomes: it worked; it reported failure
 * and the query says the main context is current anyway; and it left something
 * else current. Only the third is terminal, and on it the context is left
 * alive - a destroy needs a verified main context. */
static void t_create_switch_back(void)
{
    int rc[2], lands[2];

    begin("create: the switch back reports failure and the query says MAIN");
    arm_backend();
    rc[0] = -1; lands[0] = MC_LANDS_REQUESTED;
    script(1, rc, lands);
    CHECK(sl_gl_aux_create() == 1,
          "the create SUCCEEDS: the query decides which context is current, not "
          "the return code");
    CHECK(fk.cur == MAIN_CTX, "and the main context is the one that is current");

    begin("create: the switch back leaves NO context current");
    arm_backend();
    rc[0] = -1; lands[0] = MC_LANDS_NONE;
    script(1, rc, lands);
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST, "the create answers LOST");
    CHECK(fk.deletes == 0,
          "and the context is NOT destroyed - a destroy needs a verified main "
          "context, which is exactly what is missing");
    CHECK(aux == AUX_CTX, "it is still held, and left to the end of the process");

    begin("create: the switch back reports success and the AUXILIARY context is current");
    arm_backend();
    rc[0] = 0; lands[0] = MC_LANDS_UNCHANGED;   /* the create left aux current */
    script(1, rc, lands);
    CHECK(sl_gl_aux_create() == SL_GL_AUX_LOST,
          "a successful switch whose query disagrees is LOST: the answer that "
          "would permit issuing GL on an unidentified context is the one that "
          "must not be believed");
    CHECK(fk.deletes == 0, "and nothing is destroyed");
}

/* THE SWITCH TO THE AUXILIARY CONTEXT: every combination of return code and
 * landing the backend has to distinguish. */
static void t_aux_make_current(void)
{
    int rc[1], lands[1];

    begin("switch to aux: it works");
    arm_backend();
    CHECK(sl_gl_aux_create() == 1, "a context exists");
    CHECK(sl_gl_aux_make_current() == 1, "the switch answers 1");
    CHECK(fk.cur == AUX_CTX, "and that context is current");

    begin("switch to aux: it reports failure and the MAIN context is current");
    arm_backend();
    (void) sl_gl_aux_create();
    rc[0] = -1; lands[0] = MC_LANDS_UNCHANGED;
    script(1, rc, lands);
    CHECK(sl_gl_aux_make_current() == 0,
          "the switch answers 0 - a REFUSAL with the main context verified, "
          "which is what makes it recoverable");
    CHECK(fk.cur == MAIN_CTX, "and GL still reaches the main context");

    begin("switch to aux: it reports failure and NOTHING is current");
    arm_backend();
    (void) sl_gl_aux_create();
    rc[0] = -1; lands[0] = MC_LANDS_NONE;
    script(1, rc, lands);
    CHECK(sl_gl_aux_make_current() == SL_GL_AUX_LOST,
          "the switch answers LOST, which is a different outcome from the one "
          "above and the whole reason the two are not one answer");

    begin("switch to aux: it reports failure and the AUXILIARY context is current");
    arm_backend();
    (void) sl_gl_aux_create();
    rc[0] = -1; lands[0] = MC_LANDS_REQUESTED;
    script(1, rc, lands);
    CHECK(sl_gl_aux_make_current() == 1,
          "the switch SUCCEEDS: the query decides");

    begin("switch to aux: it reports success and the MAIN context is current");
    arm_backend();
    (void) sl_gl_aux_create();
    rc[0] = 0; lands[0] = MC_LANDS_UNCHANGED;
    script(1, rc, lands);
    CHECK(sl_gl_aux_make_current() == 0,
          "the switch REFUSES, because nothing may be issued on a context that "
          "is not current whatever the call returned");

    begin("switch to aux: there is no auxiliary context");
    arm_backend();
    CHECK(sl_gl_aux_make_current() == 0,
          "it refuses with the main context verified current");
    CHECK(fk.mc_calls == 0, "without switching anything");
    arm_backend();
    fk.cur = NULL;
    CHECK(sl_gl_aux_make_current() == SL_GL_AUX_LOST,
          "and with nothing current it answers LOST rather than claiming the "
          "main context is still there");
}

/* THE SWITCH BACK TO THE MAIN CONTEXT. It has two answers and not three, by
 * contract: it is called only while the auxiliary context is current, so
 * anything other than a verified main context is the unknown outcome. */
static void t_main_make_current(void)
{
    int rc[1], lands[1];

    begin("switch back: it works");
    arm_backend();
    (void) sl_gl_aux_create();
    (void) sl_gl_aux_make_current();
    CHECK(sl_gl_main_make_current() == 1, "the switch answers 1");
    CHECK(fk.cur == MAIN_CTX, "and the main context is current");

    begin("switch back: it reports failure and the MAIN context is current anyway");
    arm_backend();
    (void) sl_gl_aux_create();
    (void) sl_gl_aux_make_current();
    rc[0] = -1; lands[0] = MC_LANDS_REQUESTED;
    script(1, rc, lands);
    CHECK(sl_gl_main_make_current() == 1, "the query decides and it answers 1");

    begin("switch back: it fails and leaves nothing current");
    arm_backend();
    (void) sl_gl_aux_create();
    (void) sl_gl_aux_make_current();
    rc[0] = -1; lands[0] = MC_LANDS_NONE;
    script(1, rc, lands);
    CHECK(sl_gl_main_make_current() == SL_GL_AUX_LOST, "it answers LOST");

    begin("switch back: it reports success and the AUXILIARY context is current");
    arm_backend();
    (void) sl_gl_aux_create();
    (void) sl_gl_aux_make_current();
    rc[0] = 0; lands[0] = MC_LANDS_UNCHANGED;
    script(1, rc, lands);
    CHECK(sl_gl_main_make_current() == SL_GL_AUX_LOST,
          "it answers LOST: a success the query contradicts is not a success");

    begin("switch back: there is no main context");
    arm_backend();
    (void) sl_gl_aux_create();
    (void) sl_gl_aux_make_current();
    ctx = NULL;
    CHECK(sl_gl_main_make_current() == SL_GL_AUX_LOST,
          "it answers LOST and never claims a context it does not have");
}

/* THE DESTROY, WHOSE PRECONDITION IS NOW CHECKED RATHER THAN ASSUMED.
 * Destroying the context that is current would leave the thread with none, and
 * the caller can prove which context is current no better than the backend
 * can. */
static void t_destroy(void)
{
    begin("destroy: there is nothing to destroy");
    arm_backend();
    CHECK(sl_gl_aux_destroy() == 0, "it answers 0");
    CHECK(fk.deletes == 0, "and deletes nothing");

    begin("destroy: the MAIN context is current, as required");
    arm_backend();
    (void) sl_gl_aux_create();
    CHECK(sl_gl_aux_destroy() == 1, "it answers 1");
    CHECK(fk.deletes == 1 && fk.last_deleted == AUX_CTX,
          "and the context deleted is the auxiliary one");
    CHECK(aux == NULL, "the backend holds none afterwards");
    CHECK(sl_gl_aux_destroy() == 0, "a second destroy answers 0");
    CHECK(fk.deletes == 1, "and deletes nothing twice");

    begin("destroy: the AUXILIARY context is current");
    arm_backend();
    (void) sl_gl_aux_create();
    (void) sl_gl_aux_make_current();
    CHECK(sl_gl_aux_destroy() == SL_GL_AUX_LOST,
          "it REFUSES: deleting the current context would leave this thread "
          "with none");
    CHECK(fk.deletes == 0, "nothing was deleted");
    CHECK(aux == AUX_CTX, "and the context is still held");

    begin("destroy: no context is current at all");
    arm_backend();
    (void) sl_gl_aux_create();
    fk.cur = NULL;
    CHECK(sl_gl_aux_destroy() == SL_GL_AUX_LOST, "it refuses");
    CHECK(fk.deletes == 0, "and deletes nothing");

    begin("destroy: the window is gone");
    arm_backend();
    (void) sl_gl_aux_create();
    win = NULL;
    CHECK(sl_gl_aux_destroy() == SL_GL_AUX_LOST, "it refuses");
    CHECK(fk.deletes == 0, "and deletes nothing");
}

/* THE TERMINATE. What can be asserted here is that it reaches the platform
 * primitive with the code it was given and that it does not come back through
 * any SDL call on the way. What it does in the product - end the process without
 * running exit handlers or library detach code - is a property of that primitive
 * and is documented where it is chosen, not something this file can observe. */
static void t_terminate(void)
{
    begin("terminate: the primitive is reached with the caller's code");
    arm_backend();
    sl_gl_terminate_no_gl(95);
    CHECK(fk.term_calls == 1, "the primitive was invoked exactly once");
    CHECK(fk.term_code == 95, "with the code it was given");
    CHECK(fk.mc_calls == 0 && fk.creates == 0 && fk.deletes == 0,
          "and no SDL call of any kind was made on the way out");
}

int main(void)
{
    printf("test_sl_gfx_sdl_aux: the BACKEND seam "
           "(src/gfx/sl_gfx_sdl_aux.inc) against a scripted SDL\n");
    t_create_ok();
    t_create_restores_one();
    t_create_not_main();
    t_create_no_backend();
    t_create_attr();
    t_create_set_fails_but_mutated();
    t_create_set_succeeds_wrong_value();
    t_create_readback_fails_after_write();
    t_create_restore_unproved();
    t_create_null_restore_unproved();
    t_create_null();
    t_create_switch_back();
    t_aux_make_current();
    t_main_make_current();
    t_destroy();
    t_terminate();
    printf("test_sl_gfx_sdl_aux: %ld checks, %ld failed\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
