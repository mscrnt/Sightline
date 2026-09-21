/**
 * sl_settings_apply.c - the game-facing half of the native settings store
 * (#41): the GLOBAL DEFAULTS in config.ini reach the game through the game's
 * own setters, at the game's own moments, and come back the same way.
 *
 * TWO SEAMS, both in src/game under #ifndef __sgi, both one call:
 *
 *   APPLY   init_watch_at_start_of_stage (options.c), right after its
 *           fileLoadSaveSettingsForSelectedFolder. That call is where the
 *           cartridge applies the per-folder save's options at every stage
 *           start - control type through cur_player_set_control_type, Look
 *           Up/Down through set_cur_player_look_vertical_inverted, Aim Control
 *           through cur_player_set_aim_control (file2.c:1283-1328). The
 *           native default is applied on top through the SAME three setters,
 *           so a player exists, the watch state has just been reset, and
 *           nothing the folder load did not already do is done. It is also
 *           the one point DIRECT BOOT reaches (lv.c:426 calls it for every
 *           non-title stage): SL_BOOT_LEVEL skips the front end and the
 *           folder load finds no save, and the config still lands - which is
 *           what the retired .style sidecar's apply-once in the frame pump
 *           existed for.
 *
 *   SYNC    fileSaveSettingsForFolder (file2.c), the cartridge's own "commit
 *           the options" point, which the game runs every time the solo watch
 *           CLOSES (trigger_solo_watch_menu -> deleteCurrentSelectedFolder ->
 *           fileClearSavefileForFolder) and on abort: confirm (options.c:788).
 *           The three values are mirrored into the store there, so a change
 *           made in the watch persists exactly as a change made in the front
 *           end does, and the front-end Options page shows it next time. One
 *           default, two views. The per-folder save keeps being written as
 *           Rare wrote it; this reads the same getters beside it.
 *
 * SCOPE RULES, the same ones fileLoadSettingsForFolder already applies: the
 * control style is a SINGLE-PLAYER value (the folder load forces Honey for
 * more than one player, so the APPLY seam touches the style only when
 * getPlayerCount() == 1, and the SYNC seam never reads it); Look Up/Down and
 * Aim Control are the global game_options_entries values and are applied and
 * mirrored whatever the player count, as the folder load does.
 *
 * THE CONTROL STYLE IS PINNED TO 1.1 HONEY (#63, owner decision 2026-09-20:
 * no N64 control styles on the native build - Sightline never has an N64
 * controller attached and the native layer owns every mapping). The eight
 * styles decided two things the native producers no longer leave to the
 * game: which N64 stick / C cluster walks and which looks (the keyboard,
 * the mouse and both pad sticks reach bondviewProcessInput through the
 * native movement channels, which set canNaturalTurn / canNaturalPitch
 * themselves, so the split is moot), and which N64 button fires, aims and
 * cycles (the native FIRE / AIM actions arrive as Z / R, the rest as
 * actions). Under Honey Z is FIRE and R is AIM, which is what every native
 * binding assumes; 1.3 / 1.4 swapped them and the 2.x styles read a second
 * pad. So the apply seam writes CONTROLLER_CONFIG_HONEY over whatever the
 * per-folder save carries, every stage start, in solo play. The __sgi build
 * keeps all eight; the cartridge save's control type keeps being written as
 * Rare wrote it and is simply not read into the native player.
 * CONSEQUENCE for an existing config: a control_style line is ignored (the
 * store's retired key); a keyboard player who had picked 1.3 Kissy or 1.4
 * Goodnight - under which the mouse's Z arrived as AIM and R as FIRE - gets
 * the Honey click back: left button FIRE, right button AIM. Recorded in
 * docs/divergences.md (D-010).
 *
 * INVERT MOUSE Y is not here: its one state is the platform layer's
 * (sl_input.c) and its seed and persistence go through that layer's own
 * setter (sl_mouse_invert_y_seed / _set). Nothing in src/game reads a file.
 *
 * INACTIVE STORE = NO-OP. Trace replay and headless health never initialise
 * the store (no writable save), sl_settings_active() is 0 and both seams
 * return before touching anything, so they replay bit-identically.
 */
#ifndef __sgi

#include <stdio.h>
#include <ultra64.h>
#include <bondgame.h>
#include "player.h"
#include "options.h"
#include "../platform/sl_settings.h"

extern char *getenv(const char *);
/* The action layer's HOLD / TOGGLE latch reset (src/platform/sl_action.c,
 * #56) - declared here rather than through sl_action.h, which wants the
 * host headers this unit does not see. */
extern void sl_action_latch_reset(void);

/* options.c setters with no header prototype (the getters have one). */
extern void set_cur_player_look_vertical_inverted(u32 on);
extern void cur_player_set_aim_control(u32 on);

void sl_settings_apply_player_defaults(void)
{
    int look, aim;

    /* A NEW STAGE (#56): the HOLD / TOGGLE latches do not carry into it - a
     * toggled crouch or sprint from the previous attempt, or from the level
     * before, is over. This is the one point every stage start reaches
     * (the front-end flow, a restart after death, SL_BOOT_LEVEL), before
     * the store check because the latches are the action layer's and not
     * the store's (they are simply 0 wherever there is no live input). */
    sl_action_latch_reset();

    if (!sl_settings_active())
        return;

    look  = sl_settings_get(SL_SET_LOOK_UPDOWN);
    aim   = sl_settings_get(SL_SET_AIM_CONTROL);

    /* The pin (see the header): 1.1 Honey, whatever the folder save said. */
    if (getPlayerCount() == 1)
        cur_player_set_control_type(CONTROLLER_CONFIG_HONEY);
    set_cur_player_look_vertical_inverted(look != 0);
    cur_player_set_aim_control(aim != 0);

    /* The witness reads BACK through the game's own getters, so the line
     * proves the game state and not merely the intent. */
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline settings: applied player defaults control_style=%d (pinned)"
                        " look_updown=%d aim_control=%d (players=%d) -> game reads"
                        " style=%d look=%d aim=%d\n",
                (int) CONTROLLER_CONFIG_HONEY, look, aim, (int) getPlayerCount(),
                cur_player_get_control_type(),
                (int) get_cur_player_look_vertical_inverted(),
                (int) cur_player_get_aim_control());
}

void sl_settings_sync_from_game(void)
{
    if (!sl_settings_active())
        return;
    /* The control style is pinned and no longer a setting: nothing to mirror. */
    sl_settings_set(SL_SET_LOOK_UPDOWN, get_cur_player_look_vertical_inverted() != 0);
    sl_settings_set(SL_SET_AIM_CONTROL, cur_player_get_aim_control() != 0);
}

/**
 * SPRINT (#42): is the Gameplay setting ON? The one read the movement seam in
 * bondviewProcessInput makes (declared locally there, inside its own
 * #ifndef __sgi, exactly as sl_action_channels_get is). Not applied at a
 * moment and not cached: the setter below writes the store directly, so a
 * change takes effect on the next tick with no restart, and an INACTIVE
 * store (replay, headless) answers the table default, 0 - the seam then does
 * nothing and the cartridge's movement runs unchanged. No game field is
 * touched and nothing is mirrored back.
 *
 * THE ONE SETTER (#44): sl_sprint_enabled_set is what both views call - the
 * front end's GAMEPLAY tab (sl_front_options.c) and the solo watch's
 * SIGHTLINE page (options.c). Neither holds a copy; the store is the state,
 * and its write-on-change is the persistence, so a change made in the watch
 * shows in the front end and after a restart exactly as one made there.
 */
int sl_sprint_enabled(void)
{
    return sl_settings_get(SL_SET_SPRINT_ENABLED) != 0;
}

void sl_sprint_enabled_set(int on)
{
    sl_settings_set(SL_SET_SPRINT_ENABLED, on != 0);
}

/**
 * CROUCH MODE / SPRINT MODE (#56): HOLD (0) or TOGGLE (1) for the two
 * actions - `which` 0 = CROUCH, 1 = SPRINT. The same shape as the Sprint
 * pair above and for the same reason: both editors (the front end's
 * GAMEPLAY tab, the watch's SIGHTLINE -> GAMEPLAY child) call these two and
 * hold no copy; the store is the state and its write-on-change the
 * persistence. The consumer is the platform action layer, which reads the
 * store every poll (sl_action_modes_apply) and drops the action's latch the
 * poll it sees the mode change - so a change made in either editor is live
 * on the next poll and never leaves a stale latch behind. Nothing in
 * src/game reads these.
 */
int sl_action_mode(int which)
{
    return sl_settings_get(which == 0 ? SL_SET_CROUCH_MODE : SL_SET_SPRINT_MODE) == SL_ACTION_MODE_TOGGLE;
}

void sl_action_mode_set(int which, int toggle)
{
    sl_settings_set(which == 0 ? SL_SET_CROUCH_MODE : SL_SET_SPRINT_MODE,
                    toggle ? SL_ACTION_MODE_TOGGLE : SL_ACTION_MODE_HOLD);
}

#endif /* !__sgi */
