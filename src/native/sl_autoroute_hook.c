/**
 * Automated front-end route: the game-side glue.
 *
 * This is the ONLY translation unit of the route that reads or writes
 * front-end state. It holds no decision: each function below reads the state
 * the decision needs, asks src/native/sl_autoroute.c whether this screen is
 * to be confirmed, and - only on a yes - performs exactly the writes that
 * screen's own human-confirm branch performs. Every frontChangeMenu on the
 * route is still executed by src/game/front.c, so every init_menuXX
 * constructor, every briefing load and every wallet load still runs.
 *
 * WHY NATIVE, NOT src/game. Same argument as sl_cheat.c, sl_teleport.c and
 * sl_mission_dbg.c: src/game stays byte-identical to the matching build,
 * src/platform cannot see a game struct, and src/native is compiled with the
 * game include path while being included by nothing in src/game. The five
 * call sites in src/game/front.c are each a three-line `#ifndef __sgi` block
 * holding a local extern declaration and one call, which is the shape the
 * sl_front_witness site beside the mode-select hit test already uses.
 *
 * WHAT IS WRITTEN, AND WHERE THE SAME WRITE LIVES ON THE HUMAN PATH:
 *   selected_folder_num   front.c:2695  (the folder click)
 *   gamemode              front.c:3202  (the Single Player confirm)
 *   briefingpage          front.c:3837  (the mission confirm)
 *   selected_stage        front.c:3838  (the mission confirm)
 *   tab_next_selected     front.c:3839, :4039, :7238  (each screen's confirm)
 *   selected_difficulty   front.c:4038  (the difficulty confirm)
 * Nothing else is written. g_StageNum, g_MainStageNum and the boot-level path
 * are not touched: the stage is requested by init_menu0B_runstage ->
 * bossSetLoadedStage (front.c:7502-7504) exactly as on the human path.
 *
 * BOUNDARY. Every entry point returns on `!sl_ar_armed()` before reading any
 * game state, so with SL_AUTO_ROUTE unset the cost on the default player path
 * is one call and one test of a static int per front-end frame, and no game
 * variable is read or written.
 */
#ifndef __sgi

#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include "front.h"
#include "sl_autoroute.h"

/* Declared in no header this translation unit can reach; signatures verbatim
 * from their definitions. */
extern s32 get_highest_unlocked_difficulty_for_level(s32 arg0);  /* front.c:3579 */
extern s32 g_MainStageNum;                                        /* boss.c:162 */

/* This seam's own poll count. Only one front-end screen is up at a time, so
 * exactly one of the entry points below runs per front-end frame and this
 * counts those frames. It is carried into the log and is never a condition. */
static int g_arh_poll;

/**
 * The part of the state that is the same at every confirm site: the front end
 * is the stage that is up, no stage load is pending, no menu transition is in
 * flight, and the screen's constructor has produced its wallet.
 */
static void sl_arh_fill(sl_ar_state *st)
{
    g_arh_poll++;
    st->poll = g_arh_poll;
    st->stage = (int) bossGetStageNum();
    st->title_stage = (int) LEVELID_TITLE;
    st->pending_stage = (int) g_MainStageNum;
    st->no_stage = (int) LEVELID_NONE;
    st->xition_reload = (int) menu_update;
    st->xition_prev = (int) maybe_prev_menu;
    st->menu_invalid = (int) MENU_INVALID;
    st->wallet_ready = (walletinst[0] != NULL);
    st->unlocked_difficulty = -1;
    st->page_stage_id = -1;
}

/**
 * File select. Called from src/game/front.c after the folder picker and
 * BEFORE the screen's own advance test, which is the only window in which the
 * write survives: the block at front.c:2604-2619 consumes a folder number
 * set earlier in the frame and resets it to FOLDER_INVALID, and the advance
 * test at front.c:2755 reads it after that. This is also why an unattended
 * boot cannot pass this screen on its own and attract-loops back to the legal
 * screen after 30 s of no input (front.c:2766-2771).
 */
void sl_autoroute_file_select(void)
{
    sl_ar_state st;
    if (!sl_ar_armed())
        return;
    sl_arh_fill(&st);
    if (sl_ar_step(SL_AR_STEP_FILE_SELECT, &st))
        selected_folder_num = sl_ar_folder();   /* as front.c:2695 */
}

/**
 * Mode select. The screen's own tail (front.c:3221-3225) reads gamemode and
 * changes to MENU_MISSION_SELECT; this writes the value its Single Player
 * confirm branch writes (front.c:3202).
 */
void sl_autoroute_mode_select(void)
{
    sl_ar_state st;
    if (!sl_ar_armed())
        return;
    sl_arh_fill(&st);
    if (sl_ar_step(SL_AR_STEP_MODE_SELECT, &st))
        gamemode = GAMEMODE_SOLO;
}

/**
 * Mission select. The page and the stage come from the game's own folder
 * table, by the same two expressions the human confirm uses
 * (front.c:3837-3838): pull_and_display_text_for_folder_a0 is a pure scan of
 * mission_folder_setup_entries by its mission_num column (front.c:3487-3499),
 * and the stage is that row's stage_id. The decision unit then REQUIRES the
 * id the table yielded to be the route's stage before anything is written,
 * which is what distinguishes selecting a mission from poking a stage number.
 */
void sl_autoroute_mission_select(void)
{
    sl_ar_state st;
    s32 page;
    s32 stage;
    if (!sl_ar_armed())
        return;
    page = pull_and_display_text_for_folder_a0(sl_ar_mission());
    if (page < 0)
        return;
    stage = mission_folder_setup_entries[page].stage_id;
    sl_arh_fill(&st);
    st.unlocked_difficulty =
        (int) get_highest_unlocked_difficulty_for_level(
                  mission_folder_setup_entries[page].mission_num);
    st.page_stage_id = (int) stage;
    if (sl_ar_step(SL_AR_STEP_MISSION_SELECT, &st)) {
        briefingpage = page;                 /* as front.c:3837 */
        selected_stage = stage;              /* as front.c:3838 */
        tab_next_selected = 1;               /* as front.c:3839 */
    }
}

/**
 * Difficulty. The screen's own tail (front.c:4053-4063) sends 007 to
 * MENU_007_OPTIONS and everything else to MENU_BRIEFING; the decision unit
 * refuses difficulty 3 at arming time for exactly that reason, so this write
 * can only ever select a difficulty whose tail is the briefing.
 */
void sl_autoroute_difficulty(void)
{
    sl_ar_state st;
    if (!sl_ar_armed())
        return;
    sl_arh_fill(&st);
    st.unlocked_difficulty =
        (int) get_highest_unlocked_difficulty_for_level(
                  mission_folder_setup_entries[briefingpage].mission_num);
    st.page_stage_id = (int) mission_folder_setup_entries[briefingpage].stage_id;
    if (sl_ar_step(SL_AR_STEP_DIFFICULTY, &st)) {
        selected_difficulty = (DIFFICULTY) sl_ar_difficulty();  /* as front.c:4038 */
        tab_next_selected = TRUE;                               /* as front.c:4039 */
    }
}

/**
 * Briefing. The last confirm: the screen's own tail (front.c:7290-7293) calls
 * frontChangeMenu(MENU_RUN_STAGE, TRUE), the dispatcher runs
 * init_menu0B_runstage (front.c:9219) and that is where bossSetLoadedStage
 * and lvlSetSelectedDifficulty are called (front.c:7502-7504). The decision
 * unit goes permanently inert after this step, so the route cannot fire twice.
 */
void sl_autoroute_briefing(void)
{
    sl_ar_state st;
    if (!sl_ar_armed())
        return;
    sl_arh_fill(&st);
    st.unlocked_difficulty =
        (int) get_highest_unlocked_difficulty_for_level(
                  mission_folder_setup_entries[briefingpage].mission_num);
    st.page_stage_id = (int) mission_folder_setup_entries[briefingpage].stage_id;
    if (sl_ar_step(SL_AR_STEP_BRIEFING, &st))
        tab_next_selected = TRUE;            /* as front.c:7238 */
}

#endif /* !__sgi */
