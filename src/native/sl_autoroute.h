/**
 * Automated front-end route: the decision unit's interface.
 *
 * This header declares a unit that holds NO game state and includes NO game
 * header. Every value it decides on is handed to it as a plain int by the
 * call site, and every write back into the front end is performed BY the call
 * site. That split is deliberate: it is what lets the decision logic -
 * arming, the readiness predicate, step ordering, the per-step preconditions,
 * the refusals and the exact stderr lines - be compiled and exercised on a
 * host with no game, no SDL and no window.
 *
 * The route itself is described in src/native/sl_autoroute_hook.c, which is
 * the only translation unit that reads or writes front-end globals.
 *
 * BOUNDARY: inert unless SL_AUTO_ROUTE names a known route. With the variable
 * unset, empty or "0", sl_ar_armed() returns 0 on its first call and every
 * call site returns before reading or writing any game state, so the default
 * player path reaches no line of this unit beyond that one test.
 */
#ifndef SL_AUTOROUTE_H
#define SL_AUTOROUTE_H

/* The confirm sites on the solo route, in the order a person passes them.
 * Each site passes its own constant; this unit never sees a MENU_* value, so
 * it cannot be made to act on a screen it was not called from. */
#define SL_AR_STEP_FILE_SELECT      0
#define SL_AR_STEP_MODE_SELECT      1
#define SL_AR_STEP_MISSION_SELECT   2
#define SL_AR_STEP_DIFFICULTY       3
#define SL_AR_STEP_BRIEFING         4
#define SL_AR_STEP_COUNT            5

/**
 * The front-end state a confirm decision is made against. Every field is read
 * by the call site from the game's own accessors or exported globals; nothing
 * here is a frame count and nothing here is a timer. `poll` is carried for
 * the log only and is never part of a condition.
 */
typedef struct sl_ar_state {
    /* This seam's OWN count of front-end confirm-site polls since boot. It is
     * deliberately not the shim's pumped-frame number, which this translation
     * unit cannot reach; the independent `sl_mission:` stage lines carry that
     * one. Printed, never tested. */
    int poll;
    int stage;                /* bossGetStageNum() */
    int title_stage;          /* LEVELID_TITLE */
    int pending_stage;        /* g_MainStageNum - the stage the loop will load */
    int no_stage;             /* LEVELID_NONE */
    int xition_reload;        /* menu_update */
    int xition_prev;          /* maybe_prev_menu */
    int menu_invalid;         /* MENU_INVALID; front.c tests `> MENU_INVALID` */
    int wallet_ready;         /* walletinst[0] != NULL - the constructor ran */
    int unlocked_difficulty;  /* the save's highest unlocked difficulty, -1 locked */
    int page_stage_id;        /* mission_folder_setup_entries[page].stage_id */
} sl_ar_state;

/**
 * Arms on first call and reports whether this unit may act at all.
 * 0 means the route is unarmed, already finished, or refused: the call site
 * must return immediately without reading or writing front-end state.
 */
int sl_ar_armed(void);

/**
 * The confirm decision for one screen. Returns 1 exactly when the call site
 * must perform that screen's own confirm writes, and 0 otherwise. A 1 is
 * returned at most once per step and the steps are returned in order; after
 * the last step there is no re-arm.
 */
int sl_ar_step(int step, const sl_ar_state *st);

/* The route's parameters, valid once sl_ar_armed() has returned 1. */
int sl_ar_mission(void);     /* SP_LEVEL index of the route's mission */
int sl_ar_stage_id(void);    /* LEVELID the folder table must yield for it */
int sl_ar_difficulty(void);  /* DIFFICULTY the difficulty screen must select */
int sl_ar_folder(void);      /* save folder the file screen must select */

#ifdef SL_AR_HOST_TEST
/* Host test only. Not compiled into the product, so the product cannot
 * re-arm a finished route by any call it is able to make. */
void sl_ar_test_reset(void);
void sl_ar_test_set_emit(void (*emit)(const char *line));
#endif

#endif /* SL_AUTOROUTE_H */
