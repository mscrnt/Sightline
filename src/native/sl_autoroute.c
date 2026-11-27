/**
 * Automated front-end route: the decision unit.
 *
 * WHAT THIS IS FOR. A production image has to be validated by reaching a
 * level the way a person reaches it. Forcing the stage before the main loop
 * (SL_BOOT_LEVEL, src/platform/sl_ultra_shim.c:3026-3030) skips save, control
 * and front-end initialisation entirely, and has faulted in the GL driver on
 * both the v0.3 and v0.4 images, so it answers nothing about menu entry. A
 * fixed-frame pad script is worse than useless here: the boot chain's screens
 * time out on accumulated elapsed VIs (the Nintendo-logo interface adds
 * g_ClockTimer and compares against 501), so the same frame number lands on
 * different screens from run to run - which is what docs/backlog.md records
 * beside the second playtest report.
 *
 * WHAT IT DOES INSTEAD. It confirms the five screens of the solo route in
 * order, each one when - and only when - the front end is in the state that
 * screen's own confirm branch runs in. Every transition is still performed by
 * front.c's own frontChangeMenu call, every init_menuXX constructor still
 * runs, and the stage is still requested by init_menu0B_runstage ->
 * bossSetLoadedStage. Nothing here writes g_StageNum, g_MainStageNum or
 * selected_stage; the call site writes the same variables that screen's human
 * confirm writes, and nothing else.
 *
 *   SL_AUTO_ROUTE=runway   confirm the solo route through to Runway
 *   SL_AUTO_ROUTE_DIFFICULTY=0|1|2   Agent / Secret Agent / 00 Agent (default 0)
 *   SL_AUTO_ROUTE_FOLDER=0|1|2|3     the save folder to select (default 0)
 *
 * NO GAME STATE, NO GAME HEADER. Every condition is evaluated over a
 * caller-supplied sl_ar_state. Nothing in this file is a frame count or a
 * timer: `poll` is printed and never tested. That is what makes the unit
 * host-testable, and it is also the property that keeps the route from being
 * timing-dependent.
 *
 * REFUSAL IS TERMINAL. Any precondition failure prints one refusal line and
 * sets the unit dead for the life of the process. There is no retry, no
 * re-arm and no second attempt, so a launcher that sees a refusal knows the
 * route stopped there and did nothing further.
 */
#ifndef __sgi

#include "sl_autoroute.h"

/* libc by declaration rather than by header: this unit is compiled both with
 * the game's include path and by a bare host driver, and the declarations
 * below are the only ones it needs. Same approach, and the same measured
 * reason, as src/native/sl_mission_dbg.c:52-56. */
extern int sprintf(char *, const char *, ...);

#ifdef SL_AR_HOST_TEST
/* The host driver supplies the environment and the sink, so the unit under
 * test is this file's own logic with no stream and no process environment. */
extern const char *sl_ar_host_getenv(const char *name);
#define SL_AR_GETENV sl_ar_host_getenv
static void (*g_ar_emit)(const char *line);
#else
extern char *getenv(const char *);
extern int fprintf(void *, const char *, ...);
extern void *stderr;
#define SL_AR_GETENV getenv
static void sl_ar_emit_stderr(const char *line)
{
    fprintf(stderr, "%s", line);
}
static void (*g_ar_emit)(const char *line) = sl_ar_emit_stderr;
#endif

/**
 * The routes this seam knows. A token names a mission in BOTH of the game's
 * mission id spaces, because the folder table carries both side by side and
 * they overlap: `mission` is an SP_LEVEL (campaign order, src/bondconstants.h
 * :1838-1852) and `stage_id` is a LEVELID (src/bondconstants.h:1703). The
 * route never writes stage_id anywhere. It is carried so the mission-select
 * step can REQUIRE that the id the game's own folder table yields for the
 * mission equals it, which is the check that separates selecting a mission
 * from poking a stage number.
 */
static const struct sl_ar_route {
    const char *token;
    int mission;
    int stage_id;
} SL_AR_ROUTES[] = {
    { "runway", 2, 35 }   /* SP_LEVEL_RUNWAY, LEVELID_RUNWAY */
};
#define SL_AR_ROUTE_COUNT ((int) (sizeof(SL_AR_ROUTES) / sizeof(SL_AR_ROUTES[0])))

static const char *const SL_AR_STEP_NAMES[SL_AR_STEP_COUNT] = {
    "FILE_SELECT", "MODE_SELECT", "MISSION_SELECT", "DIFFICULTY", "BRIEFING"
};

static int g_ar_checked;      /* the environment has been read */
static int g_ar_on;           /* a known route was named */
static int g_ar_dead;         /* refused or finished; never acts again */
static int g_ar_next;         /* the step index this route expects next */
static int g_ar_ready_said;   /* the readiness line has been printed */
static int g_ar_mission;
static int g_ar_stage_id;
static int g_ar_difficulty;
static int g_ar_folder;

static void sl_ar_say(const char *line)
{
    if (g_ar_emit != 0)
        g_ar_emit(line);
}

/* One refusal line, then the unit is dead. `detail` carries the offending
 * value so the code alone never has to be interpreted. */
static void sl_ar_refuse(const char *code, int poll, int detail)
{
    char buf[160];
    g_ar_dead = 1;
    sprintf(buf, "sl_autoroute: refused code=%s p%d detail=%d\n", code, poll, detail);
    sl_ar_say(buf);
}

/**
 * A strict one-digit parser. atoi is not used on purpose: it accepts "3x",
 * " 3" and "" and answers 0 for all of them, which would turn a typo in an
 * environment variable into a silently different route.
 */
static int sl_ar_digit(const char *s, int lo, int hi, int dflt, int *out)
{
    int v;
    if (s == 0 || s[0] == '\0') {
        *out = dflt;
        return 1;
    }
    if (s[1] != '\0' || s[0] < '0' || s[0] > '9')
        return 0;
    v = s[0] - '0';
    if (v < lo || v > hi)
        return 0;
    *out = v;
    return 1;
}

static int sl_ar_streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void sl_ar_arm(void)
{
    const char *tok;
    const char *s;
    char buf[200];
    int i;

    if (g_ar_checked)
        return;
    g_ar_checked = 1;

    tok = SL_AR_GETENV("SL_AUTO_ROUTE");
    /* The default player path ends here: unset, empty and "0" are all inert,
     * and nothing is printed, so an unarmed run is indistinguishable from a
     * build without this unit except for this one test. */
    if (tok == 0 || tok[0] == '\0' || sl_ar_streq(tok, "0"))
        return;

    for (i = 0; i < SL_AR_ROUTE_COUNT; i++) {
        if (sl_ar_streq(tok, SL_AR_ROUTES[i].token))
            break;
    }
    if (i == SL_AR_ROUTE_COUNT) {
        sl_ar_refuse("UNKNOWN_ROUTE", 0, -1);
        return;
    }
    g_ar_mission = SL_AR_ROUTES[i].mission;
    g_ar_stage_id = SL_AR_ROUTES[i].stage_id;

    /* 3 (007) is refused rather than supported: that difficulty routes the
     * front end through MENU_007_OPTIONS instead of MENU_BRIEFING
     * (src/game/front.c:4055-4060), which is a sixth screen this route does
     * not confirm. Refusing by name is honest; silently selecting 00 Agent
     * instead would not be. */
    s = SL_AR_GETENV("SL_AUTO_ROUTE_DIFFICULTY");
    if (!sl_ar_digit(s, 0, 2, 0, &g_ar_difficulty)) {
        sl_ar_refuse("BAD_DIFFICULTY", 0, -1);
        return;
    }
    s = SL_AR_GETENV("SL_AUTO_ROUTE_FOLDER");
    if (!sl_ar_digit(s, 0, 3, 0, &g_ar_folder)) {
        sl_ar_refuse("BAD_FOLDER", 0, -1);
        return;
    }

    g_ar_on = 1;
    sprintf(buf, "sl_autoroute: armed route=%s mission=%d stage=%d difficulty=%d"
                 " folder=%d steps=%d\n",
            SL_AR_ROUTES[i].token, g_ar_mission, g_ar_stage_id,
            g_ar_difficulty, g_ar_folder, SL_AR_STEP_COUNT);
    sl_ar_say(buf);
}

int sl_ar_armed(void)
{
    sl_ar_arm();
    return g_ar_on && !g_ar_dead;
}

/**
 * The readiness predicate: the front end is up, no stage load is pending and
 * no menu transition is in flight, and the screen's constructor has produced
 * its wallet. This is a state condition in five parts and contains no frame
 * number, which is the property the route is required to have.
 */
static int sl_ar_front_end_ready(const sl_ar_state *st)
{
    return st->stage == st->title_stage
        && st->pending_stage == st->no_stage
        && st->xition_reload <= st->menu_invalid
        && st->xition_prev <= st->menu_invalid
        && st->wallet_ready != 0;
}

int sl_ar_step(int step, const sl_ar_state *st)
{
    char buf[220];

    sl_ar_arm();
    if (!g_ar_on || g_ar_dead)
        return 0;
    if (st == 0)
        return 0;
    if (step < 0 || step >= SL_AR_STEP_COUNT) {
        sl_ar_refuse("BAD_STEP", st->poll, step);
        return 0;
    }

    /* Not ready is the ordinary case on the frames before the front end
     * settles, so it is silent and leaves the route armed. */
    if (!sl_ar_front_end_ready(st))
        return 0;

    /* Order is enforced over the whole route, not per screen. Arriving at a
     * screen this route has already confirmed, or at one ahead of the step it
     * expects, means something other than this route moved the front end. */
    if (step != g_ar_next) {
        sl_ar_refuse(step < g_ar_next ? "ROUTE_REGRESSED" : "ROUTE_SKIPPED",
                     st->poll, step);
        return 0;
    }

    if (!g_ar_ready_said) {
        g_ar_ready_said = 1;
        sprintf(buf, "sl_autoroute: ready p%d stage=%d pending=%d xition=%d,%d"
                     " wallet=%d\n",
                st->poll, st->stage, st->pending_stage, st->xition_reload,
                st->xition_prev, st->wallet_ready);
        sl_ar_say(buf);
    }

    if (step == SL_AR_STEP_MISSION_SELECT) {
        /* The mission must be reachable in this save, and the id the game's
         * own folder table yields for it must be the route's stage. */
        if (st->unlocked_difficulty < 0) {
            sl_ar_refuse("MISSION_LOCKED", st->poll, st->unlocked_difficulty);
            return 0;
        }
        if (st->page_stage_id != g_ar_stage_id) {
            sl_ar_refuse("PAGE_STAGE_MISMATCH", st->poll, st->page_stage_id);
            return 0;
        }
    }
    if (step == SL_AR_STEP_DIFFICULTY) {
        if (st->unlocked_difficulty < g_ar_difficulty) {
            sl_ar_refuse("DIFFICULTY_LOCKED", st->poll, st->unlocked_difficulty);
            return 0;
        }
        if (st->page_stage_id != g_ar_stage_id) {
            sl_ar_refuse("PAGE_STAGE_MISMATCH", st->poll, st->page_stage_id);
            return 0;
        }
    }
    if (step == SL_AR_STEP_BRIEFING) {
        if (st->page_stage_id != g_ar_stage_id) {
            sl_ar_refuse("PAGE_STAGE_MISMATCH", st->poll, st->page_stage_id);
            return 0;
        }
    }

    g_ar_next = step + 1;
    sprintf(buf, "sl_autoroute: step p%d n=%d name=%s\n",
            st->poll, step, SL_AR_STEP_NAMES[step]);
    sl_ar_say(buf);

    if (step == SL_AR_STEP_BRIEFING) {
        /* The last confirm. The call site sets this screen's own
         * tab_next_selected, and src/game/front.c:7290-7293 then calls
         * frontChangeMenu(MENU_RUN_STAGE, TRUE); the dispatcher runs
         * init_menu0B_runstage (front.c:9219), which is the bossSetLoadedStage
         * call (front.c:7502-7504). */
        sprintf(buf, "sl_autoroute: request p%d menu=MENU_RUN_STAGE"
                     " via=frontChangeMenu sink=init_menu0B_runstage"
                     " stage=%d difficulty=%d\n",
                st->poll, g_ar_stage_id, g_ar_difficulty);
        sl_ar_say(buf);
        g_ar_dead = 1;
        sprintf(buf, "sl_autoroute: done p%d steps=%d\n", st->poll, g_ar_next);
        sl_ar_say(buf);
    }
    return 1;
}

int sl_ar_mission(void)    { return g_ar_mission; }
int sl_ar_stage_id(void)   { return g_ar_stage_id; }
int sl_ar_difficulty(void) { return g_ar_difficulty; }
int sl_ar_folder(void)     { return g_ar_folder; }

#ifdef SL_AR_HOST_TEST
void sl_ar_test_reset(void)
{
    g_ar_checked = 0;
    g_ar_on = 0;
    g_ar_dead = 0;
    g_ar_next = 0;
    g_ar_ready_said = 0;
    g_ar_mission = 0;
    g_ar_stage_id = 0;
    g_ar_difficulty = 0;
    g_ar_folder = 0;
}

void sl_ar_test_set_emit(void (*emit)(const char *line))
{
    g_ar_emit = emit;
}
#endif

#endif /* !__sgi */
