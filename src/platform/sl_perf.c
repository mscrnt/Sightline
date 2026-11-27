/**
 * sl_perf.c - the OPTIMIZED RENDERING row and the policy that resolves it.
 * See sl_perf.h for what the row selects, what it never reaches, and the one
 * precedence rule every answer here follows.
 *
 * WHY src/platform. Pure host arithmetic over the settings store and the
 * environment: no SDL, no GL, no game type - the same class as sl_settings.c,
 * sl_window.c and sl_presentation.c, so src/gfx, src/native and the offline
 * tests can all include the header and every answer can be driven with no
 * window and no context.
 *
 * WHAT IS CACHED AND WHAT IS NOT, which is the whole of this file's care.
 * The ENVIRONMENT cannot change inside a launch, so each switch is parsed once
 * and held; the ROW can change at any moment a menu is open, so it is read
 * from the store on every call. No answer below reaches getenv more than once
 * per switch per process, which is what lets a per-walk or per-pass caller ask
 * as often as it likes - but no answer is frozen at the value it had when the
 * process started either, which is what makes a menu press take effect. The
 * subsystems that used to freeze their own switch in a function static now ask
 * here instead, and each one LATCHES the answer for the length of the pass or
 * walk it is about to run, so a press can never split one of those in half.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sl_settings.h"
#include "sl_perf.h"

/* ---- the environment, parsed once -------------------------------------- */

/* A 0/1 developer switch: -1 absent or unrecognised (the row governs), else
 * the value it names. File scope rather than a function static so the offline
 * tests can drive several answers in one executable; the shipped build has no
 * way to clear them, so each variable is read exactly once. */
#define PERF_UNASKED (-2)

static int s_e_sky    = PERF_UNASKED;   /* SL_SKY_BATCH              */
static int s_e_carr   = PERF_UNASKED;   /* SL_SKY_BATCH_FLOAT_COLOR  */
static int s_e_xform  = PERF_UNASKED;   /* SL_BG_GPU_XFORM           */
static int s_e_bypass = PERF_UNASKED;   /* SL_BG_GPU_BYPASS          */
static int s_e_far    = PERF_UNASKED;   /* SL_PERF_BG_FAR            */
static int s_e_row    = PERF_UNASKED;   /* SL_PERF_OPT               */
static int s_row_chosen;                /* the row has been set this launch */

/* EXACTLY "0" OR EXACTLY "1", the SL_SHOW_FPS rule. Anything else - absent,
 * empty, longer, a word - is not a value this switch names, so the row
 * governs. A nonempty value that is neither is reported once, because a typo
 * in a developer switch that silently did nothing is the failure this line
 * exists to prevent. */
static int perf_env01(const char *name, int *slot)
{
    if (*slot == PERF_UNASKED) {
        const char *v = getenv(name);
        if (v != NULL && (v[0] == '0' || v[0] == '1') && v[1] == '\0') {
            *slot = v[0] - '0';
        } else {
            *slot = -1;
            if (v != NULL && *v != '\0')
                fprintf(stderr, "sightline perf: %s='%s' names neither 0 nor 1 - "
                                "the OPTIMIZED RENDERING row governs\n", name, v);
        }
    }
    return *slot;
}

/* THE CARRIER SWITCH: -1 absent, else SL_PERF_SKY_CARRIER_*. The r474 strings
 * "div" and "prod" exactly, and "byte" for the 36-byte upload that was the
 * sprint's own default once the path was armed - spelled so a developer can
 * ask for it rather than reaching it by leaving the switch out, which the row
 * now occupies. Any other value is not a carrier this switch names. */
static int perf_env_carrier(void)
{
    if (s_e_carr == PERF_UNASKED) {
        const char *v = getenv("SL_SKY_BATCH_FLOAT_COLOR");
        if      (v != NULL && strcmp(v, "div")  == 0) s_e_carr = SL_PERF_SKY_CARRIER_DIV;
        else if (v != NULL && strcmp(v, "prod") == 0) s_e_carr = SL_PERF_SKY_CARRIER_PROD;
        else if (v != NULL && strcmp(v, "byte") == 0) s_e_carr = SL_PERF_SKY_CARRIER_BYTE;
        else {
            s_e_carr = -1;
            if (v != NULL && *v != '\0')
                fprintf(stderr, "sightline perf: SL_SKY_BATCH_FLOAT_COLOR='%s' names no "
                                "carrier (div, prod, byte) - the policy's carrier is used\n", v);
        }
    }
    return s_e_carr;
}

/* ---- the row ------------------------------------------------------------ */

int sl_perf_opt(void)
{
    int e;
    /* AN INACTIVE STORE ANSWERS OFF, BEFORE ANY ENVIRONMENT IS READ. Trace
     * replay, headless health and the matching build never call
     * sl_settings_init, so they take the paths they have always taken and the
     * row's default cannot reach a recorded trace. A subsystem's own developer
     * switch still reaches those runs - that is what it is for - but it is read
     * by the resolutions below, not here. */
    if (!sl_settings_active()) return 0;
    e = s_row_chosen ? -1 : perf_env01("SL_PERF_OPT", &s_e_row);
    if (e >= 0) return e;
    return sl_settings_get(SL_SET_OPTIMIZED_RENDERING) == 1;
}

void sl_perf_opt_set(int on)
{
    if (on != 0 && on != 1) return;
    /* The one flag the whole precedence rule turns on. Set BEFORE the store
     * write, so a resolution asked from inside the write path (nothing does
     * today, and the ordering costs nothing) could not see the old state. */
    s_row_chosen = 1;
    sl_settings_set(SL_SET_OPTIMIZED_RENDERING, on);
}

int sl_perf_opt_env(void)
{
    if (!sl_settings_active() || s_row_chosen) return 0;
    return perf_env01("SL_PERF_OPT", &s_e_row) >= 0;
}

/* ---- the resolutions ---------------------------------------------------- */

/* AN EXPLICIT MENU CHOICE GOVERNS EVERY CONTROLLED CHOICE, not just the row's
 * own override. This is the whole of the precedence correction, and the defect
 * it removes was this: `sl_perf_opt_set` ended the SL_PERF_OPT override and
 * nothing else, so a session that had inherited SL_BG_GPU_XFORM=1 or
 * SL_SKY_BATCH=1 kept the transform and the indexed path ON after the player
 * set the row to OFF - the menu said OFF while the selected paths were still
 * running - and an inherited SL_SKY_BATCH=1 with no carrier switch left the
 * byte carrier in force after the player set the row to ON.
 *
 * So the rule has two halves, in this order:
 *
 *   BEFORE the player has touched the row, a subsystem switch governs its own
 *   subsystem. That is what makes a launch-time diagnostic work, and it is the
 *   only state in which one does.
 *
 *   AFTER the player has touched the row (s_row_chosen), the ROW governs every
 *   controlled choice - the two enablements and the carrier - and the
 *   subsystem switches are not consulted again for the rest of the launch.
 *   A choice made in a menu cannot be overruled by something the process
 *   inherited, because the menu is showing the player what it believes is in
 *   force.
 *
 * SL_PERF_BG_FAR IS DELIBERATELY NOT COVERED BY THIS. It is not an enablement
 * and not a carrier: it widens or narrows WHICH walks the transform - already
 * selected or not by the two halves above - may take, and with the transform
 * off it selects nothing at all. It is the arm a qualification run needs while
 * the far expressions are unqualified, and a menu press must not silently
 * disarm the run it is part of. See sl_perf_bg_far_admit. */
int sl_perf_sky_on(void)
{
    if (s_row_chosen) return sl_perf_opt();
    {
        const int e = perf_env01("SL_SKY_BATCH", &s_e_sky);
        if (e >= 0) return e;
    }
    return sl_perf_opt();
}

int sl_perf_sky_carrier(void)
{
    /* THE ROW SELECTS PROD AND NOTHING ELSE - the one carrier whose values were
     * checked channel by channel against an independent oracle - so an explicit
     * choice also ends an inherited carrier switch. Without this, a player who
     * set the row to ON in a session that had inherited SL_SKY_BATCH=1 would
     * get the byte upload while the row read ON, and one who had inherited
     * SL_SKY_BATCH_FLOAT_COLOR=div would get a carrier no menu offers. */
    if (s_row_chosen) return SL_PERF_SKY_CARRIER_PROD;
    {
        const int c = perf_env_carrier();
        if (c >= 0) return c;
    }
    /* A DEVELOPER SWITCH THAT ARMED THE PATH KEEPS THE SPRINT'S DEFAULT
     * CARRIER, which is the 36-byte byte upload: a run driven with
     * SL_SKY_BATCH=1 and no carrier switch submits exactly what it submitted
     * before this row existed, so every diagnostic taken that way still means
     * what it meant. */
    if (perf_env01("SL_SKY_BATCH", &s_e_sky) == 1) return SL_PERF_SKY_CARRIER_BYTE;
    return SL_PERF_SKY_CARRIER_PROD;
}

int sl_perf_bg_xform(void)
{
    if (s_row_chosen) return sl_perf_opt();
    {
        const int e = perf_env01("SL_BG_GPU_XFORM", &s_e_xform);
        if (e >= 0) return e;
    }
    return sl_perf_opt();
}

/* v0.4 WHETHER A DEVELOPER NAMED THE VERTEX TRANSFORM. See the header for why
 * the renderer needs this beside _xform: that answer says whether a GPU clip
 * computation may run, and the row and this switch select DIFFERENT ones, only
 * one of which is exact.
 *
 * THE PRECEDENCE IS _xform's OWN, with one term removed. A press ends every
 * subsystem switch's say, so the first clause is the same clause _xform has;
 * what is left out is _xform's fallback to the row, because the row is
 * precisely the case this must answer 0 for. Only an exact "1" names the
 * transform - perf_env01 answers -1 for absent, empty and unrecognised, and 0
 * for an explicit off, which _xform turns into a selection that is off
 * altogether. No key is read that _xform does not read, and it is read once. */
int sl_perf_bg_xform_named(void)
{
    if (s_row_chosen) return 0;
    return perf_env01("SL_BG_GPU_XFORM", &s_e_xform) == 1;
}

int sl_perf_bg_bypass(void)
{
    if (s_row_chosen) return sl_perf_opt();
    {
        const int e = perf_env01("SL_BG_GPU_BYPASS", &s_e_bypass);
        if (e >= 0) return e;
    }
    return sl_perf_opt();
}

int sl_perf_bg_far_admit(void)
{
    /* THE EXPLICIT ARM FIRST, so a qualification run can exercise the far path
     * with every other selection at the row's own values. */
    const int e = perf_env01("SL_PERF_BG_FAR", &s_e_far);
    if (e >= 0) return e;
    /* AN EXPLICIT MENU CHOICE ENDS THE IMPLICIT LEGACY ADMISSION. The header
     * says only the explicit far flag survives a press, and the line below is
     * how that becomes true: SL_BG_GPU_XFORM=1 widened the admission in the
     * sprint's diagnostic scope, and leaving it honoured after the player had
     * chosen the row let an inherited variable put the unqualified two-range
     * walk back on the GPU behind a menu the player had just set. One switch
     * survives a press, by name, and it is the one a qualification run binds
     * on purpose - not one a session happened to inherit. */
    if (s_row_chosen) return 0;
    if (perf_env01("SL_BG_GPU_XFORM", &s_e_xform) == 1) return 1;
    return 0;
}

const char *sl_perf_why(void)
{
    static char line[320];
    static const char *const carrier[3] = { "byte", "div", "prod" };
    const int c = sl_perf_sky_carrier();
    snprintf(line, sizeof line,
             "row=%d%s store=%s sky=%d/%s bg_xform=%d bg_bypass=%d bg_far=%s",
             sl_perf_opt(), sl_perf_opt_env() ? " (SL_PERF_OPT in force)" : "",
             sl_settings_active() ? "active" : "inactive",
             sl_perf_sky_on(), (c >= 0 && c < 3) ? carrier[c] : "?",
             sl_perf_bg_xform(), sl_perf_bg_bypass(),
             sl_perf_bg_far_admit() ? "admitted (developer switch)"
                                    : "refused to the original path");
    return line;
}

#ifdef SL_PERF_TEST
/* Offline builds only: drop every cached environment answer and the row's
 * launch-override latch, so one executable can drive every combination. The
 * shipped translation unit has no way to reach this. */
void sl_perf_test_reset(void)
{
    s_e_sky = s_e_carr = s_e_xform = s_e_bypass = s_e_far = s_e_row = PERF_UNASKED;
    s_row_chosen = 0;
}
#endif

#ifdef SL_PERF_SELFTEST
/* ---- the fixture (-DSL_PERF_SELFTEST -DSL_PERF_TEST beside sl_settings.c)
 *
 * The scratch config lands under %TEMP% through SL_CONFIG, so the player's own
 * config is never opened. Every answer is driven through the REAL settings
 * store: the row is read back from the file the store wrote.
 */
static int g_pass, g_fail;
static void ck(int cond, const char *what)
{
    if (cond) { g_pass++; }
    else      { g_fail++; printf("FAIL: %s\n", what); }
}

static void put(const char *kv) { putenv((char *) kv); }

/* TEXT MODE, deliberately: the store writes through a text-mode FILE *, so on
 * Windows its "\n" reaches the file as CRLF and only a text-mode read
 * translates it back. A binary read here would miss every needle that ends in
 * a newline - which is every needle, since the key has to be matched with its
 * line ending to tell `optimized_rendering=1` from a longer key. This is
 * sl_settings.c's own read_text, for the same reason. */
static int file_has(const char *path, const char *needle)
{
    char buf[8192];
    size_t n;
    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;
    n = fread(buf, 1u, sizeof buf - 1u, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

int main(void)
{
    static char cfg[520];
    const char *tmp = getenv("TEMP");
    const char *path;

    printf("sl_perf self-test (the OPTIMIZED RENDERING row and its policy)\n\n");

    if (tmp == NULL || *tmp == '\0') tmp = ".";
    snprintf(cfg, sizeof cfg, "SL_CONFIG=%s\\sl_perf_selftest_config.ini", tmp);
    put(cfg);
    path = strchr(cfg, '=') + 1;
    remove(path);

    /* 1. AN INACTIVE STORE. Before sl_settings_init the row answers OFF and
     *    so does every resolution that depends on it - whatever the row's
     *    default is, and even with SL_PERF_OPT=1 set. This is the trace /
     *    headless / matching case. */
    put("SL_PERF_OPT=1");
    ck(!sl_settings_active(), "the store starts INACTIVE");
    ck(sl_perf_opt() == 0, "an inactive store answers OFF");
    ck(sl_perf_opt() == 0 && sl_perf_sky_on() == 0 && sl_perf_bg_xform() == 0
       && sl_perf_bg_bypass() == 0,
       "an inactive store selects nothing, with SL_PERF_OPT=1 set");
    ck(!sl_perf_opt_env(), "SL_PERF_OPT is not in force while the store is inactive");
    /* A developer switch still reaches an inactive run: that is its purpose. */
    put("SL_SKY_BATCH=1");
    sl_perf_test_reset();
    ck(sl_perf_sky_on() == 1 && sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_BYTE,
       "SL_SKY_BATCH=1 arms the path in an inactive run, with the sprint's byte carrier");
    put("SL_SKY_BATCH=");
    put("SL_PERF_OPT=");
    sl_perf_test_reset();

    /* 2. THE DEFAULT, on a missing file. The row is ON in a player session,
     *    and it selects the sky's PROD carrier, the background transform and
     *    the dependent loop reduction - but NOT the unqualified far walk. */
    sl_settings_init();
    ck(sl_settings_active(), "sl_settings_init made the store active");
    ck(sl_settings_get(SL_SET_OPTIMIZED_RENDERING) == 1,
       "optimized_rendering defaults to 1 on a missing file");
    ck(sl_perf_opt() == 1, "the row is ON by default in a player session");
    ck(sl_perf_sky_on() == 1 && sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD,
       "the row selects the skydome path with the validated PROD carrier");
    ck(sl_perf_bg_xform() == 1 && sl_perf_bg_bypass() == 1,
       "the row selects the background GPU transform and the dependent loop reduction");
    ck(sl_perf_bg_far_admit() == 0,
       "the row REFUSES the two-range far walk: it is not live-qualified");

    /* 3. THE ROW IS WRITTEN ON CHANGE, and read back. */
    sl_perf_opt_set(0);
    ck(sl_perf_opt() == 0 && sl_settings_get(SL_SET_OPTIMIZED_RENDERING) == 0
       && file_has(path, "optimized_rendering=0\n"),
       "OFF is stored and written");
    ck(sl_perf_sky_on() == 0 && sl_perf_bg_xform() == 0 && sl_perf_bg_bypass() == 0,
       "OFF selects none of the three");
    sl_perf_opt_set(1);
    ck(sl_perf_opt() == 1 && file_has(path, "optimized_rendering=1\n"),
       "ON is stored and written again");

    /* 4. AN INVALID SET IS REFUSED, and the stored value stands. */
    sl_perf_opt_set(2);
    sl_perf_opt_set(-1);
    sl_perf_opt_set(7);
    ck(sl_perf_opt() == 1 && sl_settings_get(SL_SET_OPTIMIZED_RENDERING) == 1,
       "2 / -1 / 7 are refused; the row stays ON");

    /* 5. THE ROW'S LAUNCH OVERRIDE, the sl_show_fps rule: SL_PERF_OPT is the
     *    value IN FORCE until the row is changed, and an unrecognised value
     *    leaves the row governing - so what the menus print is never a lie. */
    sl_perf_opt_set(1);
    put("SL_PERF_OPT=0");
    sl_perf_test_reset();
    ck(sl_perf_opt() == 0 && sl_settings_get(SL_SET_OPTIMIZED_RENDERING) == 1,
       "SL_PERF_OPT=0 is the value in force; the store keeps 1");
    ck(sl_perf_opt_env(), "the override reports itself in force");
    put("SL_PERF_OPT=x");
    sl_perf_test_reset();
    ck(sl_perf_opt() == 1 && !sl_perf_opt_env(),
       "SL_PERF_OPT=x names no value: the store's 1 governs");
    put("SL_PERF_OPT=0");
    sl_perf_test_reset();
    ck(sl_perf_opt() == 0, "SL_PERF_OPT=0 in force again");
    sl_perf_opt_set(1);
    ck(sl_perf_opt() == 1 && !sl_perf_opt_env(),
       "a set ENDS the override: the row governs with SL_PERF_OPT=0 still set");
    sl_perf_opt_set(0);
    ck(sl_perf_opt() == 0, "and the row can still be switched off afterwards");
    put("SL_PERF_OPT=");
    sl_perf_test_reset();
    sl_perf_opt_set(1);

    /* 6. A SUBSYSTEM SWITCH GOVERNS ITS OWN SUBSYSTEM ONLY, and never the row.
     *    With the row ON, SL_SKY_BATCH=0 takes the sky back to the immediate
     *    path and leaves the background selections alone; the row still reads
     *    ON, because that is what the player chose. */
    put("SL_SKY_BATCH=0");
    sl_perf_test_reset();
    ck(sl_perf_opt() == 1, "the row still reads ON");
    ck(sl_perf_sky_on() == 0, "SL_SKY_BATCH=0 takes the sky off");
    ck(sl_perf_bg_xform() == 1 && sl_perf_bg_bypass() == 1,
       "the background selections are untouched by the sky's switch");
    put("SL_SKY_BATCH=");
    put("SL_BG_GPU_XFORM=0");
    sl_perf_test_reset();
    ck(sl_perf_sky_on() == 1 && sl_perf_bg_xform() == 0,
       "SL_BG_GPU_XFORM=0 takes the transform off and leaves the sky on");
    ck(sl_perf_bg_far_admit() == 0, "a switch that says 0 admits no far walk either");
    put("SL_BG_GPU_XFORM=1");
    sl_perf_test_reset();
    ck(sl_perf_bg_xform() == 1 && sl_perf_bg_far_admit() == 1,
       "SL_BG_GPU_XFORM=1 is the DIAGNOSTIC scope: the far walk is admitted");
    /* v0.4 AND IT IS WHAT NAMES THE VERTEX TRANSFORM, which is a different
     * question from whether a GPU clip computation may run. The row selects the
     * exact-image clip prepass; this switch selects the transform, whose frames
     * were measured one blue value apart from the original renderer's. Only an
     * exact "1" names it; an explicit "0" turns the whole selection off instead,
     * and absence leaves the choice to the row. See sl_perf_bg_xform_named. */
    ck(sl_perf_bg_xform_named() == 1,
       "SL_BG_GPU_XFORM=1 NAMES the vertex transform, so the renderer may run "
       "it as a labelled diagnostic");
    put("SL_BG_GPU_XFORM=0");
    sl_perf_test_reset();
    ck(sl_perf_bg_xform_named() == 0 && sl_perf_bg_xform() == 0,
       "an explicit 0 names nothing and turns the selection off, so no choice "
       "between the two clip computations is reached at all");
    put("SL_BG_GPU_XFORM=");
    sl_perf_test_reset();
    ck(sl_perf_bg_xform_named() == 0 && sl_perf_bg_xform() == 1,
       "and with the switch absent the ROW selects - the transform is not "
       "named, so the renderer takes the clip prepass");
    put("SL_BG_GPU_XFORM=yes");
    sl_perf_test_reset();
    ck(sl_perf_bg_xform_named() == 0,
       "an unrecognised value names nothing either: a typo may not select the "
       "known-mismatching transform");
    put("SL_BG_GPU_XFORM=");
    put("SL_BG_GPU_BYPASS=0");
    sl_perf_test_reset();
    ck(sl_perf_bg_xform() == 1 && sl_perf_bg_bypass() == 0,
       "SL_BG_GPU_BYPASS=0 reduces no loop while the transform still runs");
    put("SL_BG_GPU_BYPASS=");
    sl_perf_test_reset();

    /* 6a. THE FAR-WALK ARM. The row guards the two-range far walk because its
     *     depth expressions are unqualified, SL_PERF_BG_FAR names the answer
     *     outright so a qualification run can exercise it with every other
     *     selection at the row's own values, and SL_BG_GPU_XFORM=1 keeps the
     *     sprint's wider scope. The explicit arm wins over both. */
    ck(sl_perf_bg_far_admit() == 0, "the row guards the far walk");
    put("SL_PERF_BG_FAR=1");
    sl_perf_test_reset();
    ck(sl_perf_bg_far_admit() == 1 && sl_perf_bg_xform() == 1 && sl_perf_opt() == 1
       && sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD,
       "SL_PERF_BG_FAR=1 admits the far walk and changes nothing else");
    put("SL_BG_GPU_XFORM=1");
    put("SL_PERF_BG_FAR=0");
    sl_perf_test_reset();
    ck(sl_perf_bg_far_admit() == 0,
       "the explicit arm wins over the diagnostic scope's wider admission");
    put("SL_PERF_BG_FAR=");
    sl_perf_test_reset();
    ck(sl_perf_bg_far_admit() == 1, "and without it the diagnostic scope admits again");
    put("SL_BG_GPU_XFORM=");
    sl_perf_test_reset();
    ck(sl_perf_bg_far_admit() == 0, "back to the row's guard");

    /* 6b. AN EXPLICIT MENU CHOICE OVERRIDES AN INHERITED SUBSYSTEM SWITCH.
     *     This is the precedence defect's own regression, driven through the
     *     production functions every menu and every subsystem calls. Each case
     *     starts from an INHERITED exact override, then presses the row OFF,
     *     then presses it ON, and asserts what is in force at each step - so a
     *     menu can never display one thing while another is running. */
    {
        /* (a) An inherited transform switch. Before any press it governs (the
         *     diagnostic state). A press OFF takes it; a press ON gives the
         *     row's own ON; and the switch is never consulted again. */
        put("SL_BG_GPU_XFORM=1");
        put("SL_PERF_OPT=");
        sl_perf_test_reset();
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);   /* the store, not a press */
        ck(sl_perf_bg_xform() == 1 && sl_perf_bg_far_admit() == 1,
           "inherited SL_BG_GPU_XFORM=1 governs before any press, diagnostic scope");
        sl_perf_opt_set(0);
        ck(sl_perf_opt() == 0 && sl_perf_bg_xform() == 0,
           "menu OFF takes the transform even with SL_BG_GPU_XFORM=1 inherited");
        ck(sl_perf_bg_bypass() == 0 && sl_perf_sky_on() == 0,
           "and nothing else the row controls is left running");
        sl_perf_opt_set(1);
        ck(sl_perf_opt() == 1 && sl_perf_bg_xform() == 1,
           "menu ON gives the row's own ON, not the switch's");
        /* THE IMPLICIT LEGACY FAR ADMISSION ENDS WITH THE PRESS. An earlier
         * revision asserted this was 1 and called it "the explicit far arm
         * surviving", which it was not: SL_PERF_BG_FAR was unset here and the
         * 1 came from the INHERITED SL_BG_GPU_XFORM. That mislabelling is what
         * let an inherited variable keep the unqualified two-range walk on the
         * GPU behind a menu the player had just set. The two switches are
         * checked separately below, which is the only way the difference can
         * be seen. */
        ck(sl_perf_bg_far_admit() == 0,
           "after a press, an inherited SL_BG_GPU_XFORM=1 admits NO far walk");
        /* v0.4 AND THE NAMING ENDS WITH THE PRESS TOO, for the same measured
         * reason: if it survived, an inherited variable would put the vertex
         * transform - one blue value apart from the original renderer at
         * (71,113) - back behind a row the player had just set, with both menus
         * displaying ON for a selection that is not the row's. */
        ck(sl_perf_bg_xform_named() == 0,
           "and an inherited SL_BG_GPU_XFORM=1 NAMES nothing after a press, so "
           "the row's clip prepass is what an ON row runs");
        put("SL_BG_GPU_XFORM=");
        sl_perf_test_reset();

        /* (a2) THE EXPLICIT FAR ARM, on its own and with nothing inherited:
         *      this is the one switch a press does not end, because a
         *      qualification run binds it on purpose. */
        put("SL_PERF_BG_FAR=1");
        sl_perf_test_reset();
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
        ck(sl_perf_bg_far_admit() == 1, "SL_PERF_BG_FAR=1 admits before any press");
        sl_perf_opt_set(1);
        ck(sl_perf_bg_far_admit() == 1 && sl_perf_bg_xform() == 1,
           "and still admits after a press ON: the explicit arm survives");
        sl_perf_opt_set(0);
        ck(sl_perf_bg_far_admit() == 1 && sl_perf_bg_xform() == 0,
           "it survives a press OFF too, and selects nothing while the transform is off");
        put("SL_PERF_BG_FAR=0");
        sl_perf_test_reset();
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
        sl_perf_opt_set(1);
        ck(sl_perf_bg_far_admit() == 0,
           "SL_PERF_BG_FAR=0 refuses after a press: the explicit arm governs in both directions");
        /* (a3) BOTH SWITCHES SET: the explicit arm wins, before and after a
         *      press, so the inherited one can never widen the scope. */
        put("SL_PERF_BG_FAR=0");
        put("SL_BG_GPU_XFORM=1");
        sl_perf_test_reset();
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
        ck(sl_perf_bg_far_admit() == 0,
           "explicit 0 beats an inherited SL_BG_GPU_XFORM=1 before any press");
        sl_perf_opt_set(1);
        ck(sl_perf_bg_far_admit() == 0, "and after a press");
        put("SL_PERF_BG_FAR=");
        sl_perf_test_reset();
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
        ck(sl_perf_bg_far_admit() == 1,
           "with the explicit arm removed, the inherited switch admits again before a press");
        sl_perf_opt_set(1);
        ck(sl_perf_bg_far_admit() == 0, "and a press ends it");
        put("SL_BG_GPU_XFORM=");
        sl_perf_test_reset();

        /* (b) An inherited SKY switch with NO carrier switch. Before a press
         *     that is the sprint's byte arm; after a press the row's carrier is
         *     prod, because the row never selects byte. */
        put("SL_SKY_BATCH=1");
        sl_perf_test_reset();
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
        ck(sl_perf_sky_on() == 1 && sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_BYTE,
           "inherited SL_SKY_BATCH=1 is the byte arm before any press");
        sl_perf_opt_set(1);
        ck(sl_perf_sky_on() == 1 && sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD,
           "a press ON moves the carrier to prod: the row never leaves byte in force");
        sl_perf_opt_set(0);
        ck(sl_perf_sky_on() == 0,
           "and a press OFF takes the sky path with SL_SKY_BATCH=1 still inherited");
        put("SL_SKY_BATCH=");
        sl_perf_test_reset();

        /* (c) An inherited CARRIER switch. div is a diagnostic carrier no menu
         *     offers, so a press must end it. */
        put("SL_SKY_BATCH_FLOAT_COLOR=div");
        sl_perf_test_reset();
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
        ck(sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_DIV,
           "inherited div governs before any press");
        sl_perf_opt_set(1);
        ck(sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD,
           "a press ON ends an inherited div carrier");
        put("SL_SKY_BATCH_FLOAT_COLOR=");
        sl_perf_test_reset();

        /* (d) An inherited BYPASS switch, and the reverse direction: a switch
         *     that says 0 must not hold the row down after a press either. */
        put("SL_BG_GPU_BYPASS=1");
        put("SL_SKY_BATCH=0");
        sl_perf_test_reset();
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
        ck(sl_perf_bg_bypass() == 1 && sl_perf_sky_on() == 0,
           "inherited BYPASS=1 and SL_SKY_BATCH=0 each govern before any press");
        sl_perf_opt_set(0);
        ck(sl_perf_bg_bypass() == 0, "menu OFF takes the reduction too");
        sl_perf_opt_set(1);
        ck(sl_perf_bg_bypass() == 1 && sl_perf_sky_on() == 1,
           "menu ON overrides BOTH an inherited 1 and an inherited 0");
        put("SL_BG_GPU_BYPASS=");
        put("SL_SKY_BATCH=");
        sl_perf_test_reset();

        /* (e) THE BASELINES STILL HOLD. With no environment at all a pressed
         *     row is just the row, and an INACTIVE store is OFF whatever was
         *     pressed - the trace / headless / matching case cannot be reached
         *     by a press, because no menu runs there. */
        sl_settings_set(SL_SET_OPTIMIZED_RENDERING, 1);
        ck(sl_perf_opt() == 1 && sl_perf_sky_on() == 1
           && sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD
           && sl_perf_bg_xform() == 1 && sl_perf_bg_bypass() == 1
           && sl_perf_bg_far_admit() == 0,
           "no environment: the default-ON row selects prod and guards the far walk");
        sl_perf_opt_set(0);
        ck(sl_perf_opt() == 0 && sl_perf_sky_on() == 0 && sl_perf_bg_xform() == 0
           && sl_perf_bg_bypass() == 0,
           "no environment: a pressed OFF selects nothing");
        sl_perf_opt_set(1);
    }

    /* 7. THE CARRIER. The row never selects byte or div; each developer
     *    spelling selects exactly itself, and an unrecognised one leaves the
     *    policy's carrier in force. */
    ck(sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD, "the row's carrier is prod");
    put("SL_SKY_BATCH_FLOAT_COLOR=div");
    sl_perf_test_reset();
    ck(sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_DIV, "div names div");
    put("SL_SKY_BATCH_FLOAT_COLOR=byte");
    sl_perf_test_reset();
    ck(sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_BYTE, "byte names the 36-byte upload");
    put("SL_SKY_BATCH_FLOAT_COLOR=PROD");
    sl_perf_test_reset();
    ck(sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD,
       "PROD is not one of the exact strings: the policy's carrier stands");
    put("SL_SKY_BATCH_FLOAT_COLOR=prod");
    put("SL_SKY_BATCH=1");
    sl_perf_test_reset();
    ck(sl_perf_sky_on() == 1 && sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD,
       "the sprint's own SL_SKY_BATCH=1 + prod combination is unchanged");
    put("SL_SKY_BATCH_FLOAT_COLOR=");
    sl_perf_test_reset();
    ck(sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_BYTE,
       "SL_SKY_BATCH=1 with no carrier switch keeps the sprint's byte upload");
    put("SL_SKY_BATCH=");
    sl_perf_test_reset();
    ck(sl_perf_sky_carrier() == SL_PERF_SKY_CARRIER_PROD,
       "with no switch at all the row's prod is back");

    /* 8. THE ROW OFF, with no switch: nothing is selected, and the carrier
     *    question is moot but still answers the policy's own value. */
    sl_perf_opt_set(0);
    ck(sl_perf_sky_on() == 0 && sl_perf_bg_xform() == 0 && sl_perf_bg_bypass() == 0
       && sl_perf_bg_far_admit() == 0,
       "OFF selects nothing and admits nothing");
    ck(sl_perf_why() != NULL && strstr(sl_perf_why(), "row=0") != NULL,
       "the census line reports the row");
    sl_perf_opt_set(1);
    ck(strstr(sl_perf_why(), "row=1") != NULL
       && strstr(sl_perf_why(), "sky=1/prod") != NULL
       && strstr(sl_perf_why(), "refused to the original path") != NULL,
       "the census line reports the row, the carrier and the far-walk refusal");

    remove(path);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
#endif /* SL_PERF_SELFTEST */
