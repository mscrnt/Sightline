/**
 * sl_front_options.c - the front end's Options screens (#41, #42).
 *
 *     mode select      1. SELECT MISSION  2. MULTIPLAYER  3. OPTIONS
 *     MENU_SL_OPTIONS  1. SETTINGS        2. CHEATS       3. BACK
 *     MENU_SL_SETTINGS a TAB STRIP over one content area:
 *                        CONTROL   LOOK UP/DOWN / AIM CONTROL /
 *                                  INVERT MOUSE Y                        (#41)
 *                                  MOUSE SENSITIVITY  -  <pct>%  + [bar] (#50)
 *                                  SCOPED SENSITIVITY -  <pct>%  + [bar] (#50)
 *                                  BINDINGS -> MENU_SL_BINDINGS          (#46)
 *                        GAMEPLAY  SPRINT  OFF ON                        (#42)
 *                                  SPRINT MODE  HOLD TOGGLE              (#56)
 *                                  CROUCH MODE  HOLD TOGGLE              (#56)
 *                        DISPLAY   WINDOW MODE    <mode>               (#52)
 *                                  RESOLUTION   -  <WxH>  +  [list]   (#52)
 *                                  VSYNC  OFF ON                      (#52)
 *                                  ASPECT RATIO  4:3  16:9  21:9  32:9   (#45)
 *                                  FIELD OF VIEW  -  <h16>  +      [bar] (#45, #50)
 *                        PAD       BUTTON LAYOUT  <preset>              (#63)
 *                                  STICK LAYOUT   <layout>              (#63)
 *                                  LOOK SENSITIVITY -  <pct>%  + [bar] (#51)
 *                                  LOOK DEADZONE    -  <pct>%  + [bar] (#51)
 *                                  MOVE DEADZONE    -  <pct>%  + [bar] (#51)
 *                                  CONTROLLER     <family>  (informational)
 *                                  BINDINGS -> MENU_SL_BINDINGS
 *                      ([bar] = the 007-mode slider under the row, set_slider_row)
 *                      (CONTROL STYLE, the first row of CONTROL since #41, left on
 *                       2026-09-20 with the N64 control styles - #63: the game side
 *                       is pinned to 1.1 Honey, sl_settings_apply.c)
 *                      + BACK on every tab, and the PREVIOUS tab
 *     MENU_SL_BINDINGS the bindings editor (src/native/sl_front_bindings.c),
 *                      BACK returns here with the cursor on BINDINGS
 *     (the DISPLAY tab first carried a #43 Phase A presentation row for one
 *      round, retired unshipped on 2026-09-18 - owner decision; #43 is parked
 *      for a dedicated graphics phase. It returned for #45 with the aspect
 *      row only: no PRESENTATION, no world-detail / texture / lighting rows,
 *      those are #43 / #47 / #48 when they come.)
 *     MENU_CHEAT       Rare's cheat menu, entered from CHEATS, unchanged
 *
 * BUILT THE WAY RARE BUILT THE SCREENS AROUND THEM, and that is the whole
 * design: each screen is an init / interface / constructor triple dispatched
 * from front.c's own switches; the interface hit-tests the ONE front-end
 * cursor (cursor_h_pos / cursor_v_pos) against row bands and value columns
 * exactly as interface_menu06_modesel and interface_menu15_cheat do, acts on
 * the same A / Z / START edge, offers the same PREVIOUS tab and the same B
 * button back, and calls frontUpdateControlStickPosition so the stick, the
 * keyboard (which drives the stick) and the native pointer
 * (sl_menu_pointer.c, hooked at the head of that function) all move the same
 * cursor; the constructor draws with frontPrintText and the same highlight
 * box (microcode_constructor_related_to_menus) and ends with the PREVIOUS
 * tab and frontDrawCursor. Nothing here is a second pointer, a second
 * highlight or a second input path: a mouse click is one N64 A edge on a
 * screen sl_game_pointer_menu_active lists, which is how every cursor menu
 * already works, and a click on a VALUE sets that value directly - the #40
 * watch rule - because the cursor was over it when A arrived.
 *
 * THE TAB STRIP (owner decision, #42: "use tabs in the options menu instead
 * of adding more options"). A tab is a hit band exactly like a row: the strip
 * is the band above the first content row, split into columns at each tab's
 * x, and A / Z / START / a click on a tab SELECTS it - the content area below
 * redraws from that tab's row list on the same screen, no menu change. The
 * strip is a small table (s_tabs[]) with a selected index, so a later issue
 * adds a tab by adding a row list.
 * Keyboard and controller reach the strip the way they reach any element of
 * a cursor menu: the stick (W/A/S/D on the keyboard) moves the cursor onto a
 * tab, confirm selects it; there is no page-switch button convention in the
 * front end to borrow - every Rare screen here is cursor + confirm, with B
 * and the PREVIOUS tab as the one way back - so none is invented. Drawn with
 * the existing means only: the selected tab carries the highlight box and
 * full-white text, the others the front end's dimmed 0x70 (the disabled
 * CHEATS / Multiplayer shade), and a hovered tab gets the box like any
 * hovered element.
 *
 * WHAT THE ROWS HOLD. Views onto two stores and nothing of their own:
 *   LOOK UP/DOWN, AIM CONTROL -> the native settings store
 *       (src/platform/sl_settings.c), the player's GLOBAL DEFAULTS, applied
 *       at every stage start through the game's own setters
 *       (src/native/sl_settings_apply.c) and mirrored back from every watch
 *       close. Look Up/Down is the pad-stick convention (reverse / upright)
 *       and never the mouse; Aim Control is hold / toggle.
 *   BUTTON LAYOUT (#63) -> the binding registry's preset (sl_bindings.c:
 *       choosing one seeds every action's PAD slots; CUSTOM shows when the
 *       editor holds something else) and STICK LAYOUT -> pad_stick_layout on
 *       the store (which thumb moves and which looks, read every poll by
 *       sl_input.c). One value cell each, advancing on a press, through the
 *       same two calls the watch's rows make (sl_watch_layout_name / _step,
 *       src/native/sl_watch_controller.c).
 *   INVERT MOUSE Y -> the platform layer's one state through
 *       sl_mouse_invert_y_get / _set (#39), the same two calls the watch's
 *       SIGHTLINE page makes (#44); the setter is what persists it.
 *   SPRINT (#42) -> sprint_enabled on the same store through
 *       sl_sprint_enabled / sl_sprint_enabled_set (sl_settings_apply.c), read
 *       every tick by the movement seam in bondview2.c, so a change takes
 *       effect on the next stage tick with no restart. The watch's SIGHTLINE
 *       page (#44) is the other view, through the same two calls.
 *   SPRINT MODE / CROUCH MODE (#56) -> sprint_mode / crouch_mode on the same
 *       store through sl_action_mode / sl_action_mode_set
 *       (sl_settings_apply.c; which 1 = SPRINT, 0 = CROUCH): HOLD (the
 *       default, today's behaviour) or TOGGLE, read every poll by the
 *       platform action layer, which drops the action's latch the poll it
 *       sees the mode change. Device-independent - a keyboard key and a pad
 *       button flip the same latch - so the rows sit on GAMEPLAY beside
 *       SPRINT, not on a device tab. The same two values as AIM CONTROL.
 *       The watch's SIGHTLINE -> GAMEPLAY child is the other view.
 *   ASPECT RATIO (#45) -> aspect_ratio on the same store through
 *       sl_aspect_ratio / sl_aspect_ratio_set (src/platform/sl_display.c),
 *       read by the native renderer once per frame at its frame reset
 *       (src/gfx/sl_gfx_dl.c rects_update) and by the SDL backend, which
 *       sets the WINDOW's width from its height (sl_gfx_sdl.c
 *       sdl_apply_aspect: 960x720 -> 1280x720 -> 2560x720), so a press
 *       applies to the very next frame drawn - this page included: the
 *       window widens, its own 3D backdrop fills it and its text stays at
 *       the same size in the centred 4:3 safe rect. A PRESENTATION setting:
 *       nothing in src/game reads it. Four values in display order over the
 *       append-only ids (set_short_value_row).
 *   WINDOW MODE / RESOLUTION / VSYNC (#52, 2026-09-20) -> window_mode,
 *       window_* / fullscreen_* and vsync on the same store, through the
 *       REQUEST seam of src/platform/sl_window.c (sl_window_mode_step /
 *       _size_step / _request_vsync): a press files a request, the SDL
 *       backend takes it at its next frame reset, asks SDL, reads back and
 *       commits only what SDL confirmed (src/gfx/sl_gfx_sdl.c
 *       sdl_apply_display) - so the rows print the APPLIED state
 *       (sl_window_state / _size_shown), never a copy and never a wish.
 *       WINDOW MODE is the one-value-cell row of BUTTON LAYOUT (WINDOWED /
 *       BORDERLESS / FULLSCREEN, advancing); RESOLUTION a `-` <WxH> `+`
 *       row stepping the list the display offers for the current mode
 *       (windowed: the heights that fit the desktop at the aspect's width;
 *       fullscreen: the display's modes), and INFORMATIONAL - dimmed, no
 *       cells, the CONTROLLER row's idiom - in BORDERLESS, where the
 *       desktop owns the size; VSYNC the OFF / ON two-value row. Output
 *       controls first, then ASPECT RATIO and FIELD OF VIEW (the content).
 *       The watch's SIGHTLINE -> DISPLAY child is the other view, same
 *       calls.
 *   THE RESOLUTION LIST (#52 follow-up, owner 2026-09-20: "instead of
 *       clicking through each of the resolutions, can it be a drop down?"):
 *       a press on the row's LABEL or its VALUE opens the current mode's
 *       list under the row (`-` and `+` keep stepping: they cost nothing);
 *       see THE DROPDOWN below the tab geometry for the rules. A pick files
 *       sl_window_request_size - the very call sl_window_size_step makes -
 *       so the transaction (take / read back / commit or restore) is the
 *       backend's, untouched. The watch's row keeps its latch + LEFT / RIGHT.
 *   FIELD OF VIEW (#45) -> fov_vertical on the same store through
 *       sl_fov_step / sl_fov_h16_displayed (sl_display.c): the store holds
 *       the vertical FOV, the row shows and steps the 16:9-equivalent
 *       horizontal in whole degrees (`-` / the number / `+`), default 91 =
 *       the exact original 60.00 vertical; applied by the renderer to the
 *       world projection at its next frame reset. RESET DEFAULTS in the
 *       bindings editors does not touch it.
 *   MOUSE SENSITIVITY / SCOPED SENSITIVITY (#50) -> mouse_sensitivity /
 *       scoped_mouse_sensitivity on the same store through sl_mouse_sens_get
 *       / sl_mouse_sens_step (src/platform/sl_input.c): percents, 10..300 in
 *       steps of 10, default 100 = the accepted feel; read by the gameplay
 *       mouse-look seam every poll, so a step is felt on the next poll. The
 *       same `-` / value / `+` row as FIELD OF VIEW, its three cells shifted
 *       one column right so the two long labels clear them. The watch's
 *       SIGHTLINE -> CONTROLS child is the other view, same two calls.
 *   THE BARS (#50 owner request, 2026-09-19): FIELD OF VIEW and the two
 *       sensitivities are SLIDER rows - the same cells over a 007-mode bar
 *       (set_slider_row; the geometry and the press / drag rules are at
 *       SET_BAR_*). A press on the bar sets the value at the cursor's
 *       fraction through sl_fov_set_fraction / sl_mouse_sens_set_fraction,
 *       which snap to the same grid the cells step on. FIELD OF VIEW's cells
 *       moved from columns 0..2 to 1..3 to share the slider row's layout.
 *   LOOK SENSITIVITY / LOOK DEADZONE / MOVE DEADZONE (#51, 2026-09-20) ->
 *       pad_look_sensitivity / pad_look_deadzone / pad_move_deadzone on the
 *       same store through sl_pad_tune_get / _step / _fraction /
 *       _set_fraction (src/platform/sl_input.c): the controller's look gain
 *       (25..200 step 5, 100 = the accepted feel) and the two inner
 *       deadzones as a percent of stick travel (0..40 step 1, 15 = the
 *       compiled 5000 raw units exactly), read by the pad seam every poll.
 *       The same slider row as the mouse percents, on the PAD tab. The
 *       watch's SIGHTLINE -> CONTROLS -> STICK TUNING child is the other
 *       view, same four calls.
 * CHEATS opens MENU_CHEAT through the same gamemode = GAMEMODE_CHEATS the
 * old third row set, so init_menu15_cheat, its rows, its toggles, its mouse
 * (sl_menu_pointer.c) and the demo's unlocked-cheats policy
 * (frontCheckIfCheatIsUnlocked) are all reused rather than duplicated. The
 * row is dimmed and unselectable while no cheat is unlocked
 * (is_cheat_menu_available, computed by interface_menu06_modesel on the way
 * here) - Rare's own disabled-entry semantic, the one the Multiplayer row
 * uses with one controller: the cursor falls through to the row above, so
 * the entry cannot light up and cannot activate.
 *
 * STRINGS. Labels are native static strings in the title table's own
 * convention (uppercase, LF-terminated), because the text tables are read
 * out of the ROM at run time and cannot grow; the values that exist in the
 * table are the table's (the eight style names, ON / OFF).
 *
 * Everything is !__sgi; front.c carries the dispatch cases, the mode-select
 * third row and the cheat menu's return, each guarded.
 */
#ifndef __sgi

#include <stdio.h>
#include <ultra64.h>
#include <bondgame.h>
#include <boss.h>
#include <fr.h>
#include <music.h>
#include <snd.h>
#include "joy.h"
#include "front.h"
#include "file2.h"
#include "textrelated.h"
#include "language.h"
#include "assets/obseg/text/LtitleE.h"
#include "../platform/sl_settings.h"
#include "../platform/sl_display.h"
#include "../platform/sl_window.h"       /* #52: the request seam and the applied state */

/* front.c helpers with no header prototype - all non-static there; declared
 * here rather than in a decomp header this file has no business editing
 * (the sl_menu_pointer.c / sl_watch_pointer.c rule). Signatures copied from
 * front.c; frontPrintText takes its two font tables as s32 there. */
extern Gfx  *frontPrintText(Gfx *gdl, s32 *x, s32 *y, s8 *text, s32 second_font_table, s32 first_font_table, s32 arg6, s32 view_x, s32 view_y, s32 arg9, s32 arga);
extern Gfx  *frontSetupMenuBackground(Gfx *DL);
extern Gfx  *frontAddPreviousTabText(Gfx *DL);
extern Gfx  *frontDrawCursor(Gfx *DL);
extern u32   frontCheckCursorOnPreviousTab(void);
extern void  frontUpdateControlStickPosition(void);
extern void  disable_all_switches(Model *arg0);
extern void  set_item_visibility_in_objinstance(Model *objinstance, s32 item, s32 mode);
extern void  select_load_bond_picture(Model *objinstance, u32 bondID);
extern void  load_walletbond(void);
extern void  setCursorPOSforMode(int mode);
extern s32   tab_prev_selected;
extern s32   tab_prev_highlight;
extern s32   is_cheat_menu_available;

/* The one Invert Mouse Y state (src/platform/sl_input.c, #39). */
extern int   sl_mouse_invert_y_get(void);
extern void  sl_mouse_invert_y_set(int on);
/* The two sensitivity percents (src/platform/sl_input.c, #50): scoped = 0
 * MOUSE SENSITIVITY, 1 SCOPED SENSITIVITY. */
extern int   sl_mouse_sens_get(int scoped);
extern void  sl_mouse_sens_step(int scoped, int dir);
/* The slider rows' bar (#50): the fill fraction and the set from one, the
 * platform layer's own grid; and whether the left mouse button is held, the
 * pointer's "A held" for the drag. */
extern float sl_mouse_sens_fraction(int scoped);
extern void  sl_mouse_sens_set_fraction(int scoped, float t);
extern int   sl_input_pointer_lmb_held(void);
/* #51: the controller's three tuning rows (LOOK SENSITIVITY / LOOK DEADZONE
 * / MOVE DEADZONE) - the same four calls, `which` = SL_PAD_TUNE_*
 * (src/platform/sl_input.h; the ids are spelled here so this file keeps to
 * the host-clean settings header alone). */
#define SL_PAD_TUNE_LOOK_SENS     0
#define SL_PAD_TUNE_LOOK_DEADZONE 1
#define SL_PAD_TUNE_MOVE_DEADZONE 2
extern int   sl_pad_tune_get(int which);
extern void  sl_pad_tune_step(int which, int dir);
extern float sl_pad_tune_fraction(int which);
extern void  sl_pad_tune_set_fraction(int which, float t);
/* #63: the attached pad's family (sl_input.c), and the two LAYOUT rows'
 * name and step (src/native/sl_watch_controller.c; which: 0 BUTTON, 1 STICK). */
extern const char *sl_watch_layout_name(s32 which);
extern void  sl_watch_layout_step(s32 which, s32 dir);
extern int   sl_input_pad_family(void);
extern const char *sl_input_pad_family_name(int family);
/* The one Sprint setter (sl_settings_apply.c, #42 / #44) - the same call the
 * watch's SIGHTLINE page makes, so the two views share one state. */
extern int   sl_sprint_enabled(void);
extern void  sl_sprint_enabled_set(int on);
/* The two action modes (sl_settings_apply.c, #56): which 0 = CROUCH, 1 =
 * SPRINT; 0 HOLD / 1 TOGGLE. The same two calls the watch's GAMEPLAY child
 * makes. */
extern int   sl_action_mode(int which);
extern void  sl_action_mode_set(int which, int toggle);
/* The N64 include tree's <stdlib.h> has no getenv (sl_cheat.c's rule). */
extern char *getenv(const char *);

#define FONT_CHARS ((s32) ptrFontZurichBoldChars)
#define FONT       ((s32) ptrFontZurichBold)
#define CONFIRM    (START_BUTTON | Z_TRIG | A_BUTTON)
#define SHADE_DIM  0x70          /* the front end's "not available" shade */
#define SHADE_ON   0xA00000FF    /* the cheat menu's ON colour */

/* THE HIGHLIGHT WITNESS, SL_INPUT_DEBUG only: which row (and value column)
 * a screen's hit test resolved the cursor to, printed when it changes, with
 * the cursor it resolved. Mode select reports through it too (front.c's
 * native arm), so an injected or hand-held navigation can be checked against
 * the screen's OWN answer before a press. row -1 = the PREVIOUS tab; on the
 * Settings page row -2 = the tab strip, col = the tab under the cursor. */
void sl_front_witness(s32 menu, s32 row, s32 col)
{
    static s32 last_menu = -99, last_row = -99, last_col = -99;

    if (menu == last_menu && row == last_row && col == last_col)
        return;
    last_menu = menu; last_row = row; last_col = col;
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline front: highlight menu=%d row=%d col=%d cursor=(%.1f,%.1f)\n",
                (int) menu, (int) row, (int) col, cursor_h_pos, cursor_v_pos);
}

/* ---------------------------------------------------------------------- */
/* Options: SETTINGS / CHEATS / BACK - the mode-select layout, same rows.   */
/* ---------------------------------------------------------------------- */

/* Rare's mode-select geometry (front.c constructor_menu06_modesel): numeral
 * at x 0x96, label at 0xAA, rows 0x20 apart from 0xDC, highlight box from
 * (0x94, y-2) to (label width + 0xAF, y+0xE), hit thresholds 9 above each
 * row's text - plus the same SL_MODESEL_DY the mode select now applies, so
 * the two screens' groups sit at the same height. */
extern const s32 sl_modesel_dy;              /* front.c SL_MODESEL_DY */
#define OPT_ROW_Y(i)  (0xDC + (i) * 0x20 + sl_modesel_dy)
#define OPT_ROW_TOP(i) ((f32) (OPT_ROW_Y(i) - 9))

enum { OPT_ROW_SETTINGS, OPT_ROW_CHEATS, OPT_ROW_BACK };

static s32 s_opt_row;                        /* highlighted row */
static s32 s_opt_go;                         /* -1 none, else a MENU to open */
static s32 s_opt_return_row = OPT_ROW_SETTINGS;   /* where the cursor lands on entry */

static void opt_place_cursor(s32 row)
{
    cursor_h_pos = 126.0f;
    cursor_v_pos = (f32) (OPT_ROW_Y(row) + 6);
}

void sl_init_menu_options(void)
{
    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    s_opt_go = -1;
    load_walletbond();
    opt_place_cursor(s_opt_return_row);
    s_opt_return_row = OPT_ROW_SETTINGS;
}

void sl_interface_menu_options(void)
{
    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);

    disable_all_switches(walletinst[0]);
    select_load_bond_picture(walletinst[0], fileGetBondForFolder(selected_folder_num));
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PAPER, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_OHMSS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_PHOTOBOND, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_EYESONLY, 1);

    tab_prev_highlight = FALSE;
    s_opt_row = OPT_ROW_SETTINGS;
    if (frontCheckCursorOnPreviousTab())
    {
        tab_prev_highlight = TRUE;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, CONFIRM))
        {
            tab_prev_selected = TRUE;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
        }
    }
    else if (OPT_ROW_TOP(OPT_ROW_BACK) <= cursor_v_pos)
    {
        s_opt_row = OPT_ROW_BACK;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, CONFIRM))
        {
            tab_prev_selected = TRUE;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
        }
    }
    else if (is_cheat_menu_available && (OPT_ROW_TOP(OPT_ROW_CHEATS) <= cursor_v_pos))
    {
        s_opt_row = OPT_ROW_CHEATS;
        if (joyGetButtonsPressedThisFrame(PLAYER_1, CONFIRM))
        {
            gamemode = GAMEMODE_CHEATS;        /* what the old third row set */
            s_opt_go = MENU_CHEAT;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, 0);
        }
    }
    else
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, CONFIRM))
        {
            s_opt_go = MENU_SL_SETTINGS;
            sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE_SFX, 0);
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }
    sl_front_witness(MENU_SL_OPTIONS, tab_prev_highlight ? -1 : s_opt_row, -1);
    frontUpdateControlStickPosition();

    if (s_opt_go >= 0)
    {
        s_opt_return_row = (s_opt_go == MENU_CHEAT) ? OPT_ROW_CHEATS : OPT_ROW_SETTINGS;
        frontChangeMenu((MENU) s_opt_go, FALSE);
        s_opt_go = -1;
        return;
    }
    if (tab_prev_selected)
    {
        frontChangeMenu(MENU_MODE_SELECT, FALSE);
        setCursorPOSforMode(GAMEMODE_CHEATS);   /* the OPTIONS row */
    }
}

/* Called by interface_menu15_cheat's PREVIOUS path (front.c): back to this
 * screen, cursor on CHEATS. */
void sl_front_options_return_from_cheats(void)
{
    s_opt_return_row = OPT_ROW_CHEATS;
    frontChangeMenu(MENU_SL_OPTIONS, FALSE);
}

static Gfx *opt_draw_row(Gfx *DL, s32 row, s8 *numeral, s8 *label, s32 colour, s32 highlighted)
{
    s32 x, y, w, h;

    x = 0x96;
    y = OPT_ROW_Y(row);
    DL = frontPrintText(DL, &x, &y, numeral, FONT_CHARS, FONT, colour, viGetX(), viGetY(), 0, 0);
    textMeasure(&h, &w, (char *) label, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = 0xAA;
    y = OPT_ROW_Y(row);
    if (highlighted)
    {
        DL = microcode_constructor_related_to_menus(DL, 0x94, y - 2, w + 0xAF, y + 0xE, 0x32);
    }
    DL = frontPrintText(DL, &x, &y, label, FONT_CHARS, FONT, colour, viGetX(), viGetY(), 0, 0);
    return DL;
}

Gfx *sl_constructor_menu_options(Gfx *DL)
{
    static char l_settings[] = "SETTINGS\n";
    static char l_cheats[]   = "CHEATS\n";
    static char l_back[]     = "BACK\n";

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    DL = opt_draw_row(DL, OPT_ROW_SETTINGS, "1.\n", l_settings, 0xFF, s_opt_row == OPT_ROW_SETTINGS);
    DL = opt_draw_row(DL, OPT_ROW_CHEATS, "2.\n", l_cheats, is_cheat_menu_available ? 0xFF : SHADE_DIM, s_opt_row == OPT_ROW_CHEATS);
    DL = opt_draw_row(DL, OPT_ROW_BACK, "3.\n", l_back, 0xFF, s_opt_row == OPT_ROW_BACK);

    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}

/* ---------------------------------------------------------------------- */
/* Settings: the tab strip and its content rows - the cheat-menu layout.   */
/* ---------------------------------------------------------------------- */

/* Rare's cheat-menu geometry (constructor_menu15_cheat): label column at x
 * 0x37, value column at 0xB3, highlight box (x-2, y-1) .. (x + width + 5,
 * y + 0xE). Content rows are 0x20 apart from 0x50 (five slots, room to
 * breathe); the second value column sits at 0x10E, clear of the PREVIOUS tab
 * (TABS_LEFT_EDGE 390). Hit thresholds 9 above each row's text; value
 * columns 6 left of their text. The TAB STRIP sits one row above the first
 * content slot, at y 0x30, its tabs 0x5A apart from the label column - three
 * tabs fit left of the PREVIOUS tab (TABS_LEFT_EDGE 390). (0x50 ran GAMEPLAY
 * into DISPLAY, measured 2026-09-18 on the retired #43 strip; 0x5A cleared
 * it, and #45's DISPLAY tab uses that pitch.) The three-value ASPECT RATIO
 * row (#45) has short values, so its columns sit 0x33 apart from the first
 * value column rather than at the two-value row's 0x10E. */
#define SET_X_LABEL   0x37
#define SET_X_VAL0    0xB3
#define SET_X_VAL1    0x10E
#define SET_X_VAL3_1  (SET_X_VAL0 + 0x33)    /* short-value rows: the second column */
#define SET_X_VAL3_2  (SET_X_VAL0 + 0x66)    /* short-value rows: the third column */
#define SET_X_VAL3_3  (SET_X_VAL0 + 0x99)    /* short-value rows: the fourth column (32:9; ends at ~362, clear of PREVIOUS at 390) */
#define SET_ROW_Y(i)  (0x50 + (i) * 0x20)
#define SET_ROW_TOP(i) ((f32) (SET_ROW_Y(i) - 9))
#define SET_COL_TOP(x) ((f32) ((x) - 6))
/* THE SLIDER ROWS (#50 owner request, 2026-09-19: "same in the out of game
 * settings menu"): MOUSE SENSITIVITY, SCOPED SENSITIVITY and FIELD OF VIEW
 * draw as the 007-mode page's bars (front.c constructor_menu09_007options):
 * the label on its line, the bar under it - the track from x 55 to 355 at
 * y + 17 .. y + 28 in the box shade 0x32, the filled part to the value's
 * fraction in 0x64 - and the value on the label's line. The 007 page puts
 * only the percent there, right-aligned at 285; these rows keep their
 * `-` value `+` cells (SET_X_VAL3_1 / _2 / _3, the #50 columns) so the
 * keyboard and pad step as before, and add the bar as a fourth cell (col
 * SET_COL_BAR): a press on it sets the value at the cursor's fraction along
 * the track, snapped to the value's step grid, and the value follows the
 * cursor while A / Z or the left mouse button stays held - the 007 rule
 * (interface_menu09_007options: (cursor_h_pos - 55) / 300 under a held A).
 * A slot pitch of 0x20 holds the 007 layout's 33 exactly: the bar ends at
 * y + 28, the next row's box starts at y + 31, so the row below a slider
 * begins its hit band at the bar's foot (SET_BAR_BOTTOM) rather than 9
 * above its own text. ASPECT RATIO stays a four-cell row: four discrete
 * shapes, not a range. */
#define SET_BAR_X0     55
#define SET_BAR_W      300
#define SET_BAR_TOP    17                    /* below the label's y */
#define SET_BAR_BOTTOM 28
#define SET_COL_BAR    3                     /* the bar, in s_set_col */
#define SET_TAB_Y     0x30
#define SET_TAB_X(i)  (SET_X_LABEL + (i) * 0x5A)
#define SET_SLOTS     8                      /* slots per tab, BACK included: 5 until #46
                                              * added the BINDINGS row, 6 until #50 added
                                              * the two sensitivity rows; slot 7 sits at
                                              * y 0x130, inside the bindings editor's own
                                              * footer / message band (0x122 / 0x134) */
#define SET_ROW_BACK_MIN 5                   /* BACK's slot on a tab of up to five rows -
                                              * the accepted GAMEPLAY / DISPLAY layout;
                                              * a longer tab's BACK follows its last row
                                              * (set_row_back) */
#define SET_ROW_TABS  (-2)                   /* the strip, in the witness and the hit test */

/* The rows a tab can show. Each is a view onto one setting - except
 * SR_BINDINGS (#46), which OPENS the bindings editor (MENU_SL_BINDINGS). */
enum { SR_NONE = 0, SR_LOOK, SR_AIM, SR_MINV, SR_SPRINT, SR_BINDINGS, SR_ASPECT, SR_FOV,
       SR_MSENS, SR_SSENS,
       SR_BLAYOUT,      /* #63: BUTTON LAYOUT - the pad's preset (one value cell, advances) */
       SR_SLAYOUT,      /* #63: STICK LAYOUT - which thumb looks (the same idiom) */
       SR_PAD,          /* #63: the attached controller's family - informational */
       SR_PSENS,        /* #51: LOOK SENSITIVITY - the pad's look gain, a slider row */
       SR_PDZ,          /* #51: LOOK DEADZONE - the look pair's inner deadzone, a slider row */
       SR_MDZ,          /* #51: MOVE DEADZONE - the move pair's, a slider row */
       SR_SMODE,        /* #56: SPRINT MODE - HOLD / TOGGLE, a two-value row */
       SR_CMODE,        /* #56: CROUCH MODE - the same */
       SR_WMODE,        /* #52: WINDOW MODE - one value cell, advancing (WINDOWED / BORDERLESS / FULLSCREEN) */
       SR_RES,          /* #52: RESOLUTION - `-` <WxH> `+` through the display's list; informational in BORDERLESS */
       SR_VSYNC };      /* #52: VSYNC - OFF / ON, a two-value row */

/* THE TAB LIST. Adding a tab = one entry: its label and its content rows in
 * slot order (BACK takes the slot after the last row, SET_ROW_BACK_MIN at
 * the least, so at most SET_SLOTS - 1 rows). */
struct sl_settings_tab {
    const char   *label;                     /* native uppercase, LF-terminated */
    unsigned char rows[SET_SLOTS - 1];       /* SR_*, SR_NONE-terminated */
};

static const struct sl_settings_tab s_tabs[] = {
    { "CONTROL\n",  { SR_LOOK, SR_AIM, SR_MINV, SR_MSENS, SR_SSENS, SR_BINDINGS, SR_NONE } },   /* #41, #50, #46; CONTROL STYLE gone (#63) */
    /* #56 (2026-09-20): the two HOLD / TOGGLE rows after SPRINT - three
     * rows, so BACK stays in its accepted slot (SET_ROW_BACK_MIN). */
    { "GAMEPLAY\n", { SR_SPRINT, SR_SMODE, SR_CMODE, SR_NONE, SR_NONE, SR_NONE, SR_NONE } },     /* #42, #56 */
    /* #52 (2026-09-20): the three output rows BEFORE the two content rows
     * (the window first, then what is drawn in it) - five rows, so BACK
     * stays in its accepted slot (SET_ROW_BACK_MIN). */
    { "DISPLAY\n",  { SR_WMODE, SR_RES, SR_VSYNC, SR_ASPECT, SR_FOV, SR_NONE, SR_NONE } },       /* #52, #45 */
    /* #63: the pad's own tab (a fourth tab: CONTROL was full when it landed,
     * and the accepted rows do not move; three glyphs, so the strip's fourth
     * slot at x 0x145 clears the PREVIOUS tab at 390): the two layouts, the
     * family, and a BINDINGS shortcut to the same editor. */
    /* #51 (2026-09-20): the three tuning sliders after the two layouts, so
     * the tab reads top-down as "which stick / how it feels / what is
     * attached / the buttons"; seven rows, so BACK takes slot 7 (y 0x130,
     * the bindings editor's own footer line, still above PREVIOUS). */
    { "PAD\n",      { SR_BLAYOUT, SR_SLAYOUT, SR_PSENS, SR_PDZ, SR_MDZ, SR_PAD, SR_BINDINGS } },
};
#define SET_TABS ((s32) (sizeof s_tabs / sizeof s_tabs[0]))
#define SET_ROW_BINDINGS 5                   /* the CONTROL tab's slot of SR_BINDINGS */
static s32 s_set_open_tab, s_set_open_row;   /* where the bindings editor was opened from (#63: two tabs hold BINDINGS) */

static s32 s_set_tab;       /* selected tab */
static s32 s_set_row;       /* highlighted: SET_ROW_TABS, or a content slot */
static s32 s_set_col;       /* strip: the tab under the cursor; row: -1 label, 0/1 value */
static s32 s_set_go;        /* -1 none, else a MENU to open (the bindings editor) */
static s32 s_set_return_tab = -1, s_set_return_row;  /* where to land on re-entry */
static s32 s_set_drag;      /* a press landed on a slider's bar and the button is still held (#50) */

/* THE DROPDOWN (#52 follow-up, 2026-09-20). The RESOLUTION row's list, open
 * under the row: the current mode's sizes (sl_window_list_windowed_now /
 * sl_window_list_fullscreen - the lists the `-` / `+` cells step, never a
 * copy) in the cheat menu's dense idiom (front.c constructor_menu15_cheat:
 * rows 0x14 apart, the text boxed when the cursor is on it), in the value
 * column the size already sits in, over one backdrop box in the highlight
 * shade (the slider's track idiom - boxes of 0x32 stack, set_slider_row).
 * The rows under it - VSYNC, ASPECT RATIO, FIELD OF VIEW, BACK - are not
 * drawn while it is open, so nothing shows through and nothing under it can
 * be hit.
 *
 * NO SECOND CURSOR, NO SECOND INPUT PATH. The list is hit-tested by the one
 * front-end cursor like every row above it (a band per entry on cursor_v_pos,
 * the column on cursor_h_pos), so the stick, the keyboard (which drives the
 * stick), the wheel (stick pulses in a menu) and the pointer all move the
 * highlight the same way; confirm (A / Z / START, a click) on an entry PICKS
 * it and closes the list, confirm with the cursor left or right of the column
 * CANCELS (a click outside), B (Escape) cancels. While it is open nothing
 * else on the page reacts: the strip, the other rows, BACK and the PREVIOUS
 * tab are not hit-tested, and B does not leave the page. Opening places the
 * cursor on the current size (the way every screen places it on entry); a
 * still pointer leaves it there, its first motion takes it back.
 *
 * SCROLLING, when the list is longer than the DD_VISIBLE slots that fit
 * above the page's foot: a window of entries, the cursor confined to the
 * window's band after the stick / pointer moved it (the same clamp
 * frontUpdateControlStickPosition applies at the screen's edge), and a push
 * past the first or last entry scrolls the window one entry, at most once
 * every DD_COOL frames so a held key walks the list rather than flies it.
 * Opening centres the window on the current size. A dimmed `-` at the first
 * slot says smaller sizes lie above, a dimmed `+` at the last says larger
 * lie below - the row's own two glyphs, in the front end's "not available"
 * shade. Absence claim, tree-wide (2026-09-20): nothing in the front end
 * scrolls a vertical list - `grep -rn scroll src/game/front.c` finds 18
 * lines, every one the multiplayer character carousel's
 * mp_char_select_scroll_offset (a horizontal portrait strip); `grep -rni
 * scroll src/native/` finds, besides this file, only the bindings editor
 * saying "no scrolling" and the WATCH's bindings page (sl_watch_bindings.c
 * wb_scroll_to_row, a ring-face idiom, not the paper page); `grep -rniE
 * "dropdown|listbox|combobox" src/` finds nothing but this file. So this is
 * the smallest idiom the paper page supports: clipped rows and the row's
 * own glyphs, no new widget. */
#define DD_X         SET_X_VAL3_1              /* the entries' column: where the value sits */
#define DD_PITCH     0x14                      /* the cheat menu's row pitch */
#define DD_VISIBLE   8                         /* slots: the last box ends at y 0x84 + 7*0x14 + 0xE = 286, inside the cursor's reach (310) */
#define DD_COOL      4                         /* frames between two scrolls under a held push */
#define DD_MARK_DX   10                        /* the `-` / `+` marks, right of the widest entry */
#define DD_Y(j)      (SET_ROW_Y(s_dd_slot) + DD_PITCH * ((j) + 1))   /* visible slot j's text y */
#define DD_TOP(j)    ((f32) (DD_Y(j) - 9))     /* a slot's hit band starts 9 above its text, as a row's */

static s32 s_dd_open;       /* the list is open */
static s32 s_dd_slot;       /* the RESOLUTION row's slot */
static s32 s_dd_top;        /* the first entry shown */
static s32 s_dd_hover;      /* the entry under the cursor, -1 outside the column */
static s32 s_dd_cool;       /* frames until the next scroll may happen */
static s32 s_dd_w;          /* the widest entry's text width */

/* The current mode's list - the one the `-` / `+` cells step (sl_window.c
 * sl_window_size_step); NULL while the row is informational. */
static const struct sl_window_list *dd_list(void)
{
    if (!sl_window_size_editable())
        return NULL;
    return sl_window_mode_shown() == SL_WINDOW_FULLSCREEN ? sl_window_list_fullscreen()
                                                         : sl_window_list_windowed_now();
}

/* An entry's text, "1280x720\n", for the measure and the draw. */
static void dd_text(const struct sl_window_list *l, s32 i, char *buf)
{
    s32 k = 0;
    sl_window_size_text(l->w[i], l->h[i], buf, 12);
    while (buf[k] != '\0') k++;
    buf[k++] = '\n'; buf[k] = '\0';
}

/* The entries shown: min(n - top, DD_VISIBLE). */
static s32 dd_shown(const struct sl_window_list *l)
{
    s32 n = l->n - s_dd_top;
    return n > DD_VISIBLE ? DD_VISIBLE : n;
}

/* The column's hit band: the backdrop's span (the widest entry plus the
 * marks), 6 left of the text as a value column's. */
static s32 dd_in_column(void)
{
    return cursor_h_pos >= SET_COL_TOP(DD_X) && cursor_h_pos <= (f32) (DD_X + s_dd_w + DD_MARK_DX + 12);
}

static void dd_witness(s32 force)
{
    static s32 last_hover = -99, last_top = -99;
    if (!force && s_dd_hover == last_hover && s_dd_top == last_top)
        return;
    last_hover = s_dd_hover; last_top = s_dd_top;
    if (getenv("SL_INPUT_DEBUG") != NULL)
    {
        const struct sl_window_list *l = dd_list();
        fprintf(stderr, "sightline front: dropdown hover=%d top=%d n=%d size=%dx%d cursor=(%.1f,%.1f)\n",
                (int) s_dd_hover, (int) s_dd_top, l ? l->n : 0,
                (l && s_dd_hover >= 0) ? l->w[s_dd_hover] : 0, (l && s_dd_hover >= 0) ? l->h[s_dd_hover] : 0,
                cursor_h_pos, cursor_v_pos);
    }
}

/* Open the list under slot `slot`: the window centred on the current size,
 * the cursor on it. Nothing while the row is informational. */
static void dd_open(s32 slot)
{
    const struct sl_window_list *l = dd_list();
    char buf[16];
    s32 i, w, h, cur;

    if (l == NULL || l->n <= 0)
        return;
    s_dd_slot = slot;
    s_dd_w = 0;
    for (i = 0; i < l->n; i++)
    {
        dd_text(l, i, buf);
        textMeasure(&h, &w, buf, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        if (w > s_dd_w) s_dd_w = w;
    }
    sl_window_size_shown(&w, &h);
    cur = sl_window_list_find(l, w, h);
    if (cur < 0) cur = 0;
    s_dd_top = cur - DD_VISIBLE / 2;
    if (s_dd_top > l->n - DD_VISIBLE) s_dd_top = l->n - DD_VISIBLE;
    if (s_dd_top < 0) s_dd_top = 0;
    s_dd_hover = cur;
    s_dd_cool = 0;
    s_dd_open = 1;
    cursor_h_pos = (f32) (DD_X + 20);
    cursor_v_pos = (f32) (DD_Y(cur - s_dd_top) + 6);
    if (getenv("SL_INPUT_DEBUG") != NULL)
        fprintf(stderr, "sightline options: dropdown open n=%d cur=%d top=%d mode=%s\n",
                l->n, (int) cur, (int) s_dd_top, sl_window_mode_name(sl_window_mode_shown()));
    dd_witness(1);
}

/* The list's own frame (sl_interface_menu_settings while it is open): the
 * hit test, the pick / cancel, then the cursor's move and its confinement. */
static void dd_interface(void)
{
    const struct sl_window_list *l = dd_list();
    s32 j, shown;

    if (l == NULL || l->n <= 0)
    {
        s_dd_open = 0;                       /* the row went informational under it */
        return;
    }
    if (s_dd_top > l->n - 1) s_dd_top = l->n - 1;
    if (s_dd_top < 0) s_dd_top = 0;
    shown = dd_shown(l);

    /* The highlight follows the cursor only while A / Z are up, the page's
     * own rule, so a press acts on what was under the cursor when it came. */
    if (joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG) == 0)
    {
        s_dd_hover = -1;
        if (dd_in_column())
        {
            for (j = shown - 1; j >= 0; j--)
            {
                if (DD_TOP(j) <= cursor_v_pos)
                {
                    s_dd_hover = s_dd_top + j;
                    break;
                }
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, CONFIRM))
    {
        if (s_dd_hover >= 0 && s_dd_hover < l->n)
        {
            /* The pick: the same request the `-` / `+` cells file
             * (sl_window.c sl_window_size_step -> sl_window_request_size);
             * the backend takes it at its next frame reset, reads back and
             * commits or restores. The row prints the applied truth. */
            sl_window_request_size(l->w[s_dd_hover], l->h[s_dd_hover]);
            if (getenv("SL_INPUT_DEBUG") != NULL)
                fprintf(stderr, "sightline options: dropdown pick i=%d size=%dx%d req=%d\n",
                        (int) s_dd_hover, l->w[s_dd_hover], l->h[s_dd_hover], sl_window_request_pending());
        }
        else if (getenv("SL_INPUT_DEBUG") != NULL)
            fprintf(stderr, "sightline options: dropdown cancel (confirm outside the column)\n");
        s_dd_open = 0;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        if (getenv("SL_INPUT_DEBUG") != NULL)
            fprintf(stderr, "sightline options: dropdown cancel (B)\n");
        s_dd_open = 0;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }
    dd_witness(0);
    if (!s_dd_open)
    {
        /* Closed this frame (a pick or a cancel): the cursor goes back to
         * the row's value it opened from - the way every screen places the
         * cursor on entry - so a keyboard or pad player's next confirm is on
         * the row and never on whatever lay under the list (BACK); a still
         * pointer leaves it there, its first motion takes it back. */
        cursor_h_pos = (f32) (DD_X + 20);
        cursor_v_pos = (f32) (SET_ROW_Y(s_dd_slot) + 6);
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    frontUpdateControlStickPosition();
    if (!s_dd_open)
        return;                              /* the page's hit test resumes next frame */

    /* Confinement and the scroll: a push past the window's first or last
     * entry scrolls one, at most every DD_COOL frames, then the cursor is
     * held at the band's edge - so a tap scrolls one and a hold walks. */
    if (s_dd_cool > 0) s_dd_cool--;
    if (cursor_v_pos < DD_TOP(0) + 1.0f)
    {
        if (s_dd_top > 0 && s_dd_cool == 0) { s_dd_top--; s_dd_cool = DD_COOL; }
        cursor_v_pos = DD_TOP(0) + 1.0f;
    }
    else if (cursor_v_pos > DD_TOP(shown - 1) + (f32) DD_PITCH - 1.0f)
    {
        if (s_dd_top + shown < l->n && s_dd_cool == 0) { s_dd_top++; s_dd_cool = DD_COOL; }
        cursor_v_pos = DD_TOP(shown - 1) + (f32) DD_PITCH - 1.0f;
    }
}

/* Is a slot's row a slider (#50)? */
static s32 set_row_is_slider(s32 kind)
{
    return kind == SR_FOV || kind == SR_MSENS || kind == SR_SSENS
        || kind == SR_PSENS || kind == SR_PDZ || kind == SR_MDZ;
}

/* The #51 rows' id for the platform layer's four calls (sl_pad_tune_*),
 * -1 for any other kind. */
static s32 set_row_pad_tune(s32 kind)
{
    if (kind == SR_PSENS) return SL_PAD_TUNE_LOOK_SENS;
    if (kind == SR_PDZ)   return SL_PAD_TUNE_LOOK_DEADZONE;
    if (kind == SR_MDZ)   return SL_PAD_TUNE_MOVE_DEADZONE;
    return -1;
}

/* A slider row's value as the editors print it (the FOV row its degrees,
 * the percent rows their percent). */
static s32 set_row_value(s32 kind)
{
    if (kind == SR_FOV)
        return sl_fov_h16_displayed();
    if (set_row_pad_tune(kind) >= 0)
        return sl_pad_tune_get(set_row_pad_tune(kind));
    return sl_mouse_sens_get(kind == SR_SSENS);
}

/* BACK's slot on the selected tab: the slot after its last row, and never
 * above SET_ROW_BACK_MIN (so a short tab keeps the accepted layout). */
static s32 set_row_back(void)
{
    s32 n = 0;
    while (n < SET_SLOTS - 1 && s_tabs[s_set_tab].rows[n] != SR_NONE)
        n++;
    return n > SET_ROW_BACK_MIN ? n : SET_ROW_BACK_MIN;
}

/* The row kind in a slot of the selected tab, SR_NONE for an empty slot. */
static s32 set_row_kind(s32 slot)
{
    if (slot < 0 || slot >= set_row_back())
        return SR_NONE;
    return s_tabs[s_set_tab].rows[slot];
}

/* The top of a slot's hit band: 9 above its text, or the foot of the bar
 * above it when the slot above is a slider row (#50). */
static f32 set_row_top(s32 slot)
{
    if (slot > 0 && set_row_is_slider(set_row_kind(slot - 1)))
        return (f32) (SET_ROW_Y(slot - 1) + SET_BAR_BOTTOM + 1);
    return SET_ROW_TOP(slot);
}

/* The cursor's fraction along a slider's track, 0..1 (the 007 page's
 * (cursor_h_pos - 55) / 300, clamped). */
static f32 set_bar_fraction(void)
{
    f32 t = (cursor_h_pos - (f32) SET_BAR_X0) / (f32) SET_BAR_W;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

/* A slider row's value at a fraction of its bar (#50): the platform layer's
 * own snap to the value's grid (sl_input.c / sl_display.c). */
static void set_slide(s32 kind, f32 t)
{
    if (kind == SR_FOV)
        sl_fov_set_fraction(t);
    else if (kind == SR_MSENS || kind == SR_SSENS)
        sl_mouse_sens_set_fraction(kind == SR_SSENS, t);
    else if (set_row_pad_tune(kind) >= 0)
        sl_pad_tune_set_fraction(set_row_pad_tune(kind), t);
}

static f32 set_row_fill(s32 kind)
{
    if (kind == SR_FOV)
        return sl_fov_fraction();
    if (set_row_pad_tune(kind) >= 0)
        return sl_pad_tune_fraction(set_row_pad_tune(kind));
    return sl_mouse_sens_fraction(kind == SR_SSENS);
}

/* One press: on the strip, a tab SELECTS; on a row, a VALUE sets that value,
 * the LABEL (or the style's name) advances it - the #40 watch rules, on a
 * screen that has a cursor. */
static void set_activate(s32 row, s32 col)
{
    if (row == SET_ROW_TABS)
    {
        if (col >= 0 && col < SET_TABS)
            s_set_tab = col;
    }
    else if (row == set_row_back())
    {
        tab_prev_selected = TRUE;
    }
    else
    {
        switch (set_row_kind(row))
        {
        case SR_BLAYOUT:
        case SR_SLAYOUT:
            /* #63: one value cell - the label or the name advances to the
             * next preset / layout and wraps (the CONTROL STYLE row's rule);
             * a preset re-seeds the pad's bindings, a stick layout is live
             * on the next poll. */
            sl_watch_layout_step(set_row_kind(row) == SR_BLAYOUT ? 0 : 1, 1);
            break;
        case SR_LOOK:
            sl_settings_set(SL_SET_LOOK_UPDOWN, col < 0 ? !sl_settings_get(SL_SET_LOOK_UPDOWN) : col);
            break;
        case SR_AIM:
            sl_settings_set(SL_SET_AIM_CONTROL, col < 0 ? !sl_settings_get(SL_SET_AIM_CONTROL) : col);
            break;
        case SR_MINV:
            sl_mouse_invert_y_set(col < 0 ? !sl_mouse_invert_y_get() : col);
            break;
        case SR_SPRINT:
            sl_sprint_enabled_set(col < 0 ? !sl_sprint_enabled() : col);
            break;
        case SR_SMODE:
        case SR_CMODE:
            /* #56: the AIM CONTROL rule - a value cell sets HOLD (0) or
             * TOGGLE (1), the label flips; the action layer drops the
             * action's latch on the next poll. */
            {
                s32 which = set_row_kind(row) == SR_SMODE ? 1 : 0;
                sl_action_mode_set(which, col < 0 ? !sl_action_mode(which) : col);
            }
            break;
        case SR_WMODE:
            /* #52: the label or the name advances to the next mode and
             * wraps (the BUTTON LAYOUT rule); the request is taken by the
             * SDL backend at its next frame reset. */
            sl_window_mode_step(1);
            break;
        case SR_RES:
            /* #52: `-` (col 0) steps the size down through the display's
             * list for the current mode, `+` (col 2) steps it up; the value
             * (col 1) and the label OPEN the list (the dropdown, 2026-09-20
             * - it was a step up); nothing while the row is informational
             * (BORDERLESS: the desktop owns the size). */
            if (!sl_window_size_editable())
                break;
            if (col == 0 || col == 2)
                sl_window_size_step(col == 0 ? -1 : +1);
            else
                dd_open(row);
            break;
        case SR_VSYNC:
            /* #52: a value cell asks for OFF (0) / ON (1), the label flips;
             * the backend reads the swap interval back before it is stored. */
            {
                int vs = 0;
                sl_window_state(NULL, NULL, NULL, &vs);
                sl_window_request_vsync(col < 0 ? !vs : col);
            }
            break;
        case SR_BINDINGS:
            s_set_go = MENU_SL_BINDINGS;         /* #46: the editor screen */
            s_set_open_tab = s_set_tab;          /* #63: BACK returns here */
            s_set_open_row = row;
            break;
        case SR_ASPECT:
        {
            /* #45: a VALUE column sets that aspect (columns in display
             * order: 4:3, 16:9, 21:9, 32:9 over the append-only ids); the
             * LABEL advances to the next wider one and wraps, the CONTROL
             * STYLE rule for a many-valued row. */
            s32 a = col < 0 ? sl_aspect_by_order((sl_aspect_order_of(sl_aspect_ratio()) + 1) % SL_ASPECT_COUNT)
                            : sl_aspect_by_order(col);
            sl_aspect_ratio_set(a);
            break;
        }
        case SR_FOV:
            /* #45: `-` (col 0) steps the displayed 16:9-equivalent
             * horizontal FOV down one degree, `+` (col 2) and the number
             * (col 1) and the label step it up; the store holds the vertical
             * (sl_display.c), and the exact original is one step away from
             * the default's neighbours. The BAR (col SET_COL_BAR, #50) sets
             * the degree at the cursor's fraction along it and starts a drag. */
            if (col == SET_COL_BAR) { set_slide(SR_FOV, set_bar_fraction()); s_set_drag = 1; }
            else sl_fov_step(col == 0 ? -1 : +1);
            break;
        case SR_MSENS:
        case SR_SSENS:
            /* #50: the FOV row's rule - `-` (col 0) steps the percent down
             * one step, `+` (col 2), the number (col 1) and the label step
             * it up; the platform layer clamps and persists. The bar as the
             * FOV row's. */
            if (col == SET_COL_BAR) { set_slide(set_row_kind(row), set_bar_fraction()); s_set_drag = 1; }
            else sl_mouse_sens_step(set_row_kind(row) == SR_SSENS, col == 0 ? -1 : +1);
            break;
        case SR_PSENS:
        case SR_PDZ:
        case SR_MDZ:
            /* #51: the same rule, through the platform layer's controller
             * tuning calls (one grid step, clamped, persisted; the bar as
             * the FOV row's). The pad seam reads the store every poll. */
            if (col == SET_COL_BAR) { set_slide(set_row_kind(row), set_bar_fraction()); s_set_drag = 1; }
            else sl_pad_tune_step(set_row_pad_tune(set_row_kind(row)), col == 0 ? -1 : +1);
            break;
        default:
            break;
        }
    }
    /* The one diagnostic: what a press did, beside what it left behind. */
    if (getenv("SL_INPUT_DEBUG") != NULL)
    {
        int wm = 0, ww = 0, wh = 0, wv = 0, sw = 0, sh = 0;
        sl_window_state(&wm, &ww, &wh, &wv);
        sl_window_size_shown(&sw, &sh);
        fprintf(stderr, "sightline options: tab=%d row=%d col=%d -> look=%d aim=%d minv=%d sprint=%d aspect=%d(%s) fov=%d(h16=%d) msens=%d ssens=%d buttons=%s sticks=%s family=%s psens=%d pdz=%d mdz=%d smode=%d cmode=%d wmode=%d(%s) res=%dx%d(%s) vsync=%d req=%d\n",
                (int) s_set_tab, (int) row, (int) col,
                sl_settings_get(SL_SET_LOOK_UPDOWN),
                sl_settings_get(SL_SET_AIM_CONTROL), sl_mouse_invert_y_get(),
                sl_settings_get(SL_SET_SPRINT_ENABLED),
                sl_aspect_ratio(), sl_aspect_name(sl_aspect_ratio()),
                sl_fov_vertical(), sl_fov_h16_displayed(),
                sl_mouse_sens_get(0), sl_mouse_sens_get(1),
                sl_watch_layout_name(0), sl_watch_layout_name(1),
                sl_input_pad_family_name(sl_input_pad_family()),
                sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS), sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE),
                sl_pad_tune_get(SL_PAD_TUNE_MOVE_DEADZONE),
                sl_action_mode(1), sl_action_mode(0),
                wm, sl_window_mode_name(sl_window_mode_shown()), sw, sh, sl_window_size_editable() ? "editable" : "info",
                wv, sl_window_request_pending());
    }
}

void sl_init_menu_settings(void)
{
    tab_start_selected = FALSE;
    tab_next_selected = FALSE;
    tab_prev_selected = FALSE;
    tab_prev_highlight = FALSE;
    tab_next_highlight = FALSE;
    tab_start_highlight = FALSE;
    s_set_tab = 0;                           /* CONTROL, every entry */
    s_set_row = 0;
    s_set_col = -1;
    s_set_go = -1;
    s_set_drag = 0;
    s_dd_open = 0;                           /* the list never survives a screen change */
    /* Back from the bindings editor (#46): the tab and row it was opened
     * from, so BACK lands the cursor where the player left. */
    if (s_set_return_tab >= 0)
    {
        s_set_tab = s_set_return_tab;
        s_set_row = s_set_return_row;
        s_set_return_tab = -1;
    }
    load_walletbond();
    cursor_h_pos = 126.0f;
    cursor_v_pos = (f32) (SET_ROW_Y(s_set_row) + 6);
}

/* Called by the bindings editor's BACK (sl_front_bindings.c): back to the
 * Settings page, CONTROL tab, cursor on BINDINGS. */
void sl_front_settings_return_from_bindings(void)
{
    /* The tab and slot the editor was opened from (#63: CONTROL's slot
     * SET_ROW_BINDINGS, or the PAD tab's); before #63 always CONTROL's. */
    s_set_return_tab = s_set_open_tab;
    s_set_return_row = s_set_open_row;
    s_opt_return_row = OPT_ROW_SETTINGS;
    frontChangeMenu(MENU_SL_SETTINGS, FALSE);
}

void sl_interface_menu_settings(void)
{
    s32 i;

    viSetFovY(FOV_Y_F);
    viSetAspect(ASPECT_RATIO_SD);
    viSetZRange(100.0f, 10000.0f);
    viSetUseZBuf(FALSE);

    /* The RESOLUTION list, while open, is the whole page's input: nothing
     * below reacts until it closes (a pick, a click outside, B). */
    if (s_dd_open)
    {
        s_set_row = s_dd_slot;
        s_set_col = 1;
        tab_prev_highlight = FALSE;
        dd_interface();
        return;
    }

    /* A slider drag (#50) ends when neither A / Z nor the left mouse button
     * is held any more - the 007 page's rule, with the mouse button as the
     * pointer's A (a click is one A edge, not a held A). */
    if (s_set_drag && joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG) == 0 && !sl_input_pointer_lmb_held())
        s_set_drag = 0;

    /* As the cheat menu: the highlight follows the cursor only while A/Z are
     * up, so a press acts on what was under the cursor when it arrived - and
     * not during a slider drag, whose row the cursor may leave. */
    if (joyGetButtons(PLAYER_1, A_BUTTON | Z_TRIG) == 0 && !s_set_drag)
    {
        tab_prev_highlight = FALSE;
        s_set_row = SET_ROW_TABS;
        s_set_col = 0;
        if (frontCheckCursorOnPreviousTab())
        {
            tab_prev_highlight = TRUE;
        }
        else
        {
            /* Content slots from the bottom up; an EMPTY slot of this tab
             * falls through to the slot above it, the disabled-row rule.
             * Above the first slot is the strip. */
            for (i = set_row_back(); i >= 0; i--)
            {
                if (set_row_top(i) <= cursor_v_pos
                    && (i == set_row_back() || set_row_kind(i) != SR_NONE))
                {
                    s_set_row = i;
                    break;
                }
            }
            if (s_set_row == SET_ROW_TABS)
            {
                for (i = SET_TABS - 1; i >= 0; i--)
                {
                    if (SET_COL_TOP(SET_TAB_X(i)) <= cursor_h_pos)
                    {
                        s_set_col = i;
                        break;
                    }
                }
            }
            else
            {
                s_set_col = -1;
                if (set_row_kind(s_set_row) == SR_ASPECT)
                {
                    /* #45: four value columns, from the right */
                    if (SET_COL_TOP(SET_X_VAL3_3) <= cursor_h_pos)      s_set_col = 3;
                    else if (SET_COL_TOP(SET_X_VAL3_2) <= cursor_h_pos) s_set_col = 2;
                    else if (SET_COL_TOP(SET_X_VAL3_1) <= cursor_h_pos) s_set_col = 1;
                    else if (SET_COL_TOP(SET_X_VAL0) <= cursor_h_pos)   s_set_col = 0;
                }
                else if (set_row_is_slider(set_row_kind(s_set_row)))
                {
                    /* #45 / #50: the bar's line (from 2 above the track to
                     * its foot) is the bar cell across the track's width;
                     * the label's line holds `-`, the value, `+` in the #50
                     * columns (the FOV row's cells moved one column right to
                     * match, the slider rows being one row kind now). */
                    s32 y = SET_ROW_Y(s_set_row);
                    if (cursor_v_pos >= (f32) (y + SET_BAR_TOP - 2))
                    {
                        if (cursor_h_pos >= (f32) (SET_BAR_X0 - 2) && cursor_h_pos <= (f32) (SET_BAR_X0 + SET_BAR_W + 2))
                            s_set_col = SET_COL_BAR;
                    }
                    else if (SET_COL_TOP(SET_X_VAL3_3) <= cursor_h_pos)  s_set_col = 2;
                    else if (SET_COL_TOP(SET_X_VAL3_2) <= cursor_h_pos)  s_set_col = 1;
                    else if (SET_COL_TOP(SET_X_VAL3_1) <= cursor_h_pos)  s_set_col = 0;
                }
                else if (set_row_kind(s_set_row) == SR_RES)
                {
                    /* #52: `-` at the first value column, the size at the
                     * short-value second column, `+` at the fourth (the
                     * size is up to nine glyphs); no cells while the row is
                     * informational. */
                    if (!sl_window_size_editable())                        s_set_col = -1;
                    else if (SET_COL_TOP(SET_X_VAL3_3) <= cursor_h_pos)    s_set_col = 2;
                    else if (SET_COL_TOP(SET_X_VAL3_1) <= cursor_h_pos)    s_set_col = 1;
                    else if (SET_COL_TOP(SET_X_VAL0) <= cursor_h_pos)      s_set_col = 0;
                }
                else if (set_row_kind(s_set_row) != SR_BLAYOUT && set_row_kind(s_set_row) != SR_SLAYOUT
                    && set_row_kind(s_set_row) != SR_BINDINGS && set_row_kind(s_set_row) != SR_WMODE
                    && set_row_kind(s_set_row) != SR_PAD && s_set_row != set_row_back())
                {
                    if (SET_COL_TOP(SET_X_VAL1) <= cursor_h_pos)      s_set_col = 1;
                    else if (SET_COL_TOP(SET_X_VAL0) <= cursor_h_pos) s_set_col = 0;
                }
            }
        }
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, CONFIRM))
    {
        if (tab_prev_highlight)
            tab_prev_selected = TRUE;
        else
            set_activate(s_set_row, s_set_col);
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, B_BUTTON))
    {
        tab_prev_selected = TRUE;
        sndPlaySfx(g_musicSfxBufferPtr, DOOR_METAL_CLOSE2_SFX, 0);
    }
    else if (s_set_drag && set_row_is_slider(set_row_kind(s_set_row)))
    {
        /* The 007 rule (#50): while the press that landed on the bar is
         * held, the value follows the cursor along the track. The witness
         * line only when the value moves, so a still cursor is silent. */
        s32 kind = set_row_kind(s_set_row);
        s32 before = set_row_value(kind);
        set_slide(kind, set_bar_fraction());
        if (set_row_value(kind) != before && getenv("SL_INPUT_DEBUG") != NULL)
            fprintf(stderr, "sightline options: drag tab=%d row=%d t=%.2f -> fov=%d(h16=%d) msens=%d ssens=%d psens=%d pdz=%d mdz=%d\n",
                    (int) s_set_tab, (int) s_set_row, set_bar_fraction(),
                    sl_fov_vertical(), sl_fov_h16_displayed(), sl_mouse_sens_get(0), sl_mouse_sens_get(1),
                    sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS), sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE),
                    sl_pad_tune_get(SL_PAD_TUNE_MOVE_DEADZONE));
    }

    disable_all_switches(walletinst[0]);
    set_item_visibility_in_objinstance(walletinst[0], SW_TABS, 1);
    set_item_visibility_in_objinstance(walletinst[0], SW_BLANK, 1);
    sl_front_witness(MENU_SL_SETTINGS, tab_prev_highlight ? -1 : s_set_row, s_set_col);
    frontUpdateControlStickPosition();

    if (s_set_go >= 0)
    {
        frontChangeMenu((MENU) s_set_go, FALSE);
        s_set_go = -1;
        return;
    }
    if (tab_prev_selected)
    {
        s_opt_return_row = OPT_ROW_SETTINGS;
        frontChangeMenu(MENU_SL_OPTIONS, FALSE);
    }
}

static Gfx *set_text(Gfx *DL, s32 x, s32 y, s8 *text, s32 colour, s32 boxed)
{
    s32 w, h, px = x, py = y;

    if (boxed)
    {
        textMeasure(&h, &w, (char *) text, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        DL = microcode_constructor_related_to_menus(DL, x - 2, y - 1, x + w + 5, y + 0xE, 0x32);
    }
    return frontPrintText(DL, &px, &py, text, FONT_CHARS, FONT, colour, viGetX(), viGetY(), 0, 0);
}

/* A two-value row: label, then the two values with the current one in the
 * cheat menu's ON colour, the box on whichever element the cursor is over. */
static Gfx *set_two_value_row(Gfx *DL, s32 slot, s8 *label, s8 *v0, s8 *v1, s32 current)
{
    s32 y = SET_ROW_Y(slot);
    s32 on = s_set_row == slot && !tab_prev_highlight;

    DL = set_text(DL, SET_X_LABEL, y, label, 0xFF, on && s_set_col < 0);
    DL = set_text(DL, SET_X_VAL0, y, v0, current == 0 ? SHADE_ON : 0xFF, on && s_set_col == 0);
    DL = set_text(DL, SET_X_VAL1, y, v1, current == 1 ? SHADE_ON : 0xFF, on && s_set_col == 1);
    return DL;
}

/* A short-value row (#45): the same rule with up to four closer-spaced
 * columns; `current` is the lit column (-1 for none). */
static Gfx *set_short_value_row(Gfx *DL, s32 slot, s8 *label, s8 *v0, s8 *v1, s8 *v2, s8 *v3, s32 current)
{
    s32 y = SET_ROW_Y(slot);
    s32 on = s_set_row == slot && !tab_prev_highlight;

    DL = set_text(DL, SET_X_LABEL, y, label, 0xFF, on && s_set_col < 0);
    DL = set_text(DL, SET_X_VAL0, y, v0, current == 0 ? SHADE_ON : 0xFF, on && s_set_col == 0);
    DL = set_text(DL, SET_X_VAL3_1, y, v1, current == 1 ? SHADE_ON : 0xFF, on && s_set_col == 1);
    DL = set_text(DL, SET_X_VAL3_2, y, v2, current == 2 ? SHADE_ON : 0xFF, on && s_set_col == 2);
    if (v3 != NULL)
        DL = set_text(DL, SET_X_VAL3_3, y, v3, current == 3 ? SHADE_ON : 0xFF, on && s_set_col == 3);
    return DL;
}

/* A slider row (#50): `-` <value> `+` in the short-value columns 1..3 (the
 * label's column and column 0 left clear for an eighteen-glyph label), the
 * number lit as the value, and under the line the 007-mode bar - the track
 * (x 55..355, y + 17..28, the box shade) and the fill to `fill` in the
 * 007 page's darker 0x64, boxed when the cursor is on it like any hovered
 * element. `pct` prints a `%` after the digits. The digits are built by
 * hand (the decomp's sprintf produces nothing natively). */
static Gfx *set_slider_row(Gfx *DL, s32 slot, s8 *label, s32 value, s32 pct, f32 fill)
{
    static char v_minus[] = "-\n";
    static char v_plus[]  = "+\n";
    char v[8];
    s32 y = SET_ROW_Y(slot);
    s32 on = s_set_row == slot && !tab_prev_highlight;
    s32 i = 0;
    s32 x1 = SET_BAR_X0 + (s32) ((f32) SET_BAR_W * fill + 0.5f);

    if (value >= 100) v[i++] = (char) ('0' + (value / 100) % 10);
    v[i++] = (char) ('0' + (value / 10) % 10);
    v[i++] = (char) ('0' + value % 10);
    if (pct) v[i++] = '%';
    v[i++] = '\n';
    v[i] = '\0';
    DL = microcode_constructor_related_to_menus(DL, SET_BAR_X0, y + SET_BAR_TOP, SET_BAR_X0 + SET_BAR_W, y + SET_BAR_BOTTOM, 0x32);
    if (x1 > SET_BAR_X0)
        DL = microcode_constructor_related_to_menus(DL, SET_BAR_X0, y + SET_BAR_TOP, x1, y + SET_BAR_BOTTOM, 0x64);
    if (on && s_set_col == SET_COL_BAR)
        DL = microcode_constructor_related_to_menus(DL, SET_BAR_X0 - 2, y + SET_BAR_TOP - 2, SET_BAR_X0 + SET_BAR_W + 2, y + SET_BAR_BOTTOM + 2, 0x32);
    DL = set_text(DL, SET_X_LABEL, y, label, 0xFF, on && s_set_col < 0);
    DL = set_text(DL, SET_X_VAL3_1, y, (s8 *) v_minus, 0xFF, on && s_set_col == 0);
    DL = set_text(DL, SET_X_VAL3_2, y, (s8 *) v, SHADE_ON, on && s_set_col == 1);
    DL = set_text(DL, SET_X_VAL3_3, y, (s8 *) v_plus, 0xFF, on && s_set_col == 2);
    return DL;
}

Gfx *sl_constructor_menu_settings(Gfx *DL)
{
    static char l_blayout[] = "BUTTON LAYOUT\n";   /* #63 */
    static char l_slayout[] = "STICK LAYOUT\n";
    static char l_look[]  = "LOOK UP/DOWN\n";
    static char l_aim[]   = "AIM CONTROL\n";
    static char l_minv[]  = "INVERT MOUSE Y\n";
    static char l_sprint[] = "SPRINT\n";
    static char l_smode[] = "SPRINT MODE\n";     /* #56 */
    static char l_cmode[] = "CROUCH MODE\n";
    static char l_bindings[] = "BINDINGS\n";
    static char l_aspect[] = "ASPECT RATIO\n";
    static char v_4_3[]   = "4:3\n";
    static char v_16_9[]  = "16:9\n";
    static char v_21_9[]  = "21:9\n";
    static char v_32_9[]  = "32:9\n";
    static char l_fov[]   = "FIELD OF VIEW\n";
    static char l_msens[] = "MOUSE SENSITIVITY\n";
    static char l_ssens[] = "SCOPED SENSITIVITY\n";
    static char l_psens[] = "LOOK SENSITIVITY\n";     /* #51 */
    static char l_pdz[]   = "LOOK DEADZONE\n";
    static char l_mdz[]   = "MOVE DEADZONE\n";
    static char l_back[]  = "BACK\n";
    static char l_wmode[] = "WINDOW MODE\n";    /* #52 */
    static char l_res[]   = "RESOLUTION\n";
    static char l_vsync[] = "VSYNC\n";
    static char v_minus[] = "-\n";
    static char v_plus[]  = "+\n";
    static char v_res[16];
    static char v_wmode[16];
    static char v_reverse[] = "REVERSE\n";
    static char v_upright[] = "UPRIGHT\n";
    static char v_hold[]    = "HOLD\n";
    static char v_toggle[]  = "TOGGLE\n";
    static char l_pad[]     = "CONTROLLER\n";
    static char v_layout[20];
    s8 *v_off = (s8 *) langGet(getStringID(LTITLE, TITLE_STR_116_OFF));
    s8 *v_on  = (s8 *) langGet(getStringID(LTITLE, TITLE_STR_115_ON));
    s32 i, on;

    DL = viSetFillColor(DL, 0, 0, 0);
    DL = viFillScreen(DL);
    DL = frontSetupMenuBackground(DL);
    DL = microcode_constructor(DL);

    /* The strip: the selected tab boxed and white, the rest dimmed; a hovered
     * tab boxed too, like any hovered element. */
    for (i = 0; i < SET_TABS; i++)
    {
        s32 sel = i == s_set_tab;
        s32 hov = s_set_row == SET_ROW_TABS && s_set_col == i && !tab_prev_highlight;
        DL = set_text(DL, SET_TAB_X(i), SET_TAB_Y, (s8 *) s_tabs[i].label,
                      sel ? 0xFF : SHADE_DIM, sel || hov);
    }

    /* The selected tab's rows, in their slots - down to the RESOLUTION row
     * while its list is open: the list overlays the slots under it. */
    for (i = 0; i < set_row_back(); i++)
    {
        s32 y = SET_ROW_Y(i);
        if (s_dd_open && i > s_dd_slot)
            break;
        switch (set_row_kind(i))
        {
        case SR_BLAYOUT:
        case SR_SLAYOUT:
        {
            /* #63: the layout's name as the one value cell, lit as the value,
             * boxed with the label (the CONTROL STYLE row's shape). */
            const char *n = sl_watch_layout_name(set_row_kind(i) == SR_BLAYOUT ? 0 : 1);
            s32 k = 0;
            while (n[k] != '\0' && k < 17) { v_layout[k] = n[k]; k++; }
            v_layout[k++] = '\n'; v_layout[k] = '\0';
            on = s_set_row == i && !tab_prev_highlight;
            DL = set_text(DL, SET_X_LABEL, y, set_row_kind(i) == SR_BLAYOUT ? l_blayout : l_slayout, 0xFF, on);
            DL = set_text(DL, SET_X_VAL0, y, (s8 *) v_layout, SHADE_ON, on);
            break;
        }
        case SR_LOOK:
            DL = set_two_value_row(DL, i, l_look, v_reverse, v_upright, sl_settings_get(SL_SET_LOOK_UPDOWN));
            break;
        case SR_AIM:
            DL = set_two_value_row(DL, i, l_aim, v_hold, v_toggle, sl_settings_get(SL_SET_AIM_CONTROL));
            break;
        case SR_MINV:
            DL = set_two_value_row(DL, i, l_minv, v_off, v_on, sl_mouse_invert_y_get() ? 1 : 0);
            break;
        case SR_SPRINT:
            DL = set_two_value_row(DL, i, l_sprint, v_off, v_on, sl_sprint_enabled() ? 1 : 0);
            break;
        case SR_SMODE:
            /* #56: HOLD / TOGGLE, the AIM CONTROL row's two values. */
            DL = set_two_value_row(DL, i, l_smode, v_hold, v_toggle, sl_action_mode(1) ? 1 : 0);
            break;
        case SR_CMODE:
            DL = set_two_value_row(DL, i, l_cmode, v_hold, v_toggle, sl_action_mode(0) ? 1 : 0);
            break;
        case SR_BINDINGS:
            /* #46: one label that opens the editor, boxed like any row. */
            on = s_set_row == i && !tab_prev_highlight;
            DL = set_text(DL, SET_X_LABEL, y, l_bindings, 0xFF, on);
            break;
        case SR_ASPECT:
            /* #45: the display shape in display order; the current one lit
             * like any value. */
            DL = set_short_value_row(DL, i, l_aspect, v_4_3, v_16_9, v_21_9, v_32_9,
                                     sl_aspect_order_of(sl_aspect_ratio()));
            break;
        case SR_FOV:
            /* #45 / #50: `-  <h16>  +` over the bar, the number in the ON
             * colour (it is the value). */
            DL = set_slider_row(DL, i, l_fov, sl_fov_h16_displayed(), 0, set_row_fill(SR_FOV));
            break;
        case SR_MSENS:
            DL = set_slider_row(DL, i, l_msens, sl_mouse_sens_get(0), 1, set_row_fill(SR_MSENS));
            break;
        case SR_SSENS:
            DL = set_slider_row(DL, i, l_ssens, sl_mouse_sens_get(1), 1, set_row_fill(SR_SSENS));
            break;
        case SR_PSENS:
            /* #51: the controller's three, the same slider row; the
             * deadzones print a percent of stick travel. */
            DL = set_slider_row(DL, i, l_psens, sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS), 1, set_row_fill(SR_PSENS));
            break;
        case SR_PDZ:
            DL = set_slider_row(DL, i, l_pdz, sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE), 1, set_row_fill(SR_PDZ));
            break;
        case SR_MDZ:
            DL = set_slider_row(DL, i, l_mdz, sl_pad_tune_get(SL_PAD_TUNE_MOVE_DEADZONE), 1, set_row_fill(SR_MDZ));
            break;
        case SR_WMODE:
        {
            /* #52: the mode's name as the one value cell, lit as the
             * value, boxed with the label (the BUTTON LAYOUT row's shape);
             * the APPLIED mode, which is the store's once the backend has
             * committed a request. */
            const char *n = sl_window_mode_name(sl_window_mode_shown());
            s32 k = 0;
            while (n[k] != '\0' && k < 13) { v_wmode[k] = n[k]; k++; }
            v_wmode[k++] = '\n'; v_wmode[k] = '\0';
            on = s_set_row == i && !tab_prev_highlight;
            DL = set_text(DL, SET_X_LABEL, y, l_wmode, 0xFF, on);
            DL = set_text(DL, SET_X_VAL0, y, (s8 *) v_wmode, SHADE_ON, on);
            break;
        }
        case SR_RES:
        {
            /* #52: `-` <WxH> `+` - the applied size of the current mode
             * (windowed: the client; fullscreen: the mode). In BORDERLESS
             * the desktop's size, dimmed and unboxed, the CONTROLLER row's
             * informational idiom: nothing to choose. */
            int rw = 0, rh = 0, k = 0;
            sl_window_size_shown(&rw, &rh);
            sl_window_size_text(rw, rh, v_res, 12);
            while (v_res[k] != '\0') k++;
            v_res[k++] = '\n'; v_res[k] = '\0';
            on = s_set_row == i && !tab_prev_highlight;
            if (s_dd_open)
            {
                /* The list is open: the value boxed as the open control,
                 * the two cells dimmed (inert until it closes). */
                DL = set_text(DL, SET_X_LABEL, y, l_res, 0xFF, 0);
                DL = set_text(DL, SET_X_VAL0, y, (s8 *) v_minus, SHADE_DIM, 0);
                DL = set_text(DL, SET_X_VAL3_1, y, (s8 *) v_res, SHADE_ON, 1);
                DL = set_text(DL, SET_X_VAL3_3, y, (s8 *) v_plus, SHADE_DIM, 0);
            }
            else if (sl_window_size_editable())
            {
                DL = set_text(DL, SET_X_LABEL, y, l_res, 0xFF, on && s_set_col < 0);
                DL = set_text(DL, SET_X_VAL0, y, (s8 *) v_minus, 0xFF, on && s_set_col == 0);
                DL = set_text(DL, SET_X_VAL3_1, y, (s8 *) v_res, SHADE_ON, on && s_set_col == 1);
                DL = set_text(DL, SET_X_VAL3_3, y, (s8 *) v_plus, 0xFF, on && s_set_col == 2);
            }
            else
            {
                DL = set_text(DL, SET_X_LABEL, y, l_res, SHADE_DIM, 0);
                DL = set_text(DL, SET_X_VAL3_1, y, (s8 *) v_res, SHADE_DIM, 0);
            }
            break;
        }
        case SR_VSYNC:
        {
            /* #52: OFF / ON, the applied swap interval. */
            int vs = 0;
            sl_window_state(NULL, NULL, NULL, &vs);
            DL = set_two_value_row(DL, i, l_vsync, v_off, v_on, vs ? 1 : 0);
            break;
        }
        case SR_PAD:
        {
            /* #63: informational - the family SDL classified the driving
             * pad as (XBOX / PLAYSTATION / GENERIC / NONE). Never boxed:
             * there is nothing to choose, the device says what it is. */
            static char v_fam[16];
            const char *n = sl_input_pad_family_name(sl_input_pad_family());
            s32 k = 0;
            while (n[k] != '\0' && k < 14) { v_fam[k] = n[k]; k++; }
            v_fam[k++] = '\n'; v_fam[k] = '\0';
            DL = set_text(DL, SET_X_LABEL, y, l_pad, SHADE_DIM, 0);
            DL = set_text(DL, SET_X_VAL0, y, (s8 *) v_fam, SHADE_DIM, 0);
            break;
        }
        default:
            break;
        }
    }

    if (s_dd_open)
    {
        /* THE LIST: one backdrop box over the window's span (the entries'
         * column, the widest entry plus the marks), each shown entry in the
         * cheat menu's pitch - the applied size in the ON colour, the entry
         * under the cursor boxed - and the dimmed `-` / `+` marks when the
         * window is not at the list's start / end. */
        const struct sl_window_list *l = dd_list();
        if (l != NULL && l->n > 0)
        {
            static char v_entry[16];
            s32 shown = dd_shown(l), cur, cw, ch, j;
            sl_window_size_shown(&cw, &ch);
            cur = sl_window_list_find(l, cw, ch);
            DL = microcode_constructor_related_to_menus(DL, DD_X - 2, DD_Y(0) - 1,
                                                        DD_X + s_dd_w + DD_MARK_DX + 12, DD_Y(shown - 1) + 0xE, 0x32);
            for (j = 0; j < shown; j++)
            {
                s32 e = s_dd_top + j;
                dd_text(l, e, v_entry);
                if (s_dd_hover == e)
                    DL = microcode_constructor_related_to_menus(DL, DD_X - 2, DD_Y(j) - 1,
                                                                DD_X + s_dd_w + DD_MARK_DX + 12, DD_Y(j) + 0xE, 0x32);
                DL = set_text(DL, DD_X, DD_Y(j), (s8 *) v_entry, e == cur ? SHADE_ON : 0xFF, 0);
            }
            if (s_dd_top > 0)
                DL = set_text(DL, DD_X + s_dd_w + DD_MARK_DX, DD_Y(0), (s8 *) v_minus, SHADE_DIM, 0);
            if (s_dd_top + shown < l->n)
                DL = set_text(DL, DD_X + s_dd_w + DD_MARK_DX, DD_Y(shown - 1), (s8 *) v_plus, SHADE_DIM, 0);
        }
    }
    else
    {
        on = s_set_row == set_row_back() && !tab_prev_highlight;
        DL = set_text(DL, SET_X_LABEL, SET_ROW_Y(set_row_back()), l_back, 0xFF, on);
    }

    DL = frontAddPreviousTabText(DL);
    DL = frontDrawCursor(DL);
    return DL;
}

#endif /* !__sgi */
