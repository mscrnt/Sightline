#ifndef _WATCH_H_
#define _WATCH_H_
#include <ultra64.h>
#include <bondconstants.h>

#ifdef VERSION_EU
#define XOFFSET_1 65
#define YOFFSET_1 90
#define YOFFSET_WEAPTEXT 193
#define YOFFSET_ACTIONTEXT 172
#define YOFFSET_4 235
#define YOFFSET_5 214
#define YOFFSET_MISSIONSTATUS 0x43
#define YOFFSET_7 0x33
#define YOFFSET_8 0x26
#define YOFFSET_9 0x41
#define YINC 17
#define WATCHZOOM1 4.80000019073f
#define WATCHZOOM2 6.09999990463f
#define WATCHZOOM3 4.15000009537f
#else
#define XOFFSET_1 64
#define YOFFSET_1 80
#define YINC 15
#define YOFFSET_WEAPTEXT 167
#define YOFFSET_ACTIONTEXT 149
#define YOFFSET_4 203
#define YOFFSET_5 185
#define YOFFSET_MISSIONSTATUS 0x41
#define YOFFSET_7 0x31
#define YOFFSET_8 0x25
#define YOFFSET_9 0x3B
#define WATCHZOOM1 4.6f
#define WATCHZOOM2 5.9f
#define WATCHZOOM3 3.95f
#endif

/**
 * A static buffer for watch menu current screen rectangles
 * is added to the player struct. This is a compile time
 * const to know the size of the buffer.
 * Do not change this value until player struct is fully shiftable.
*/
#define WATCH_NUMBER_SCREENS 5

/**
 * Watch menu screen select menu UI layout should satisfy the following.
 * Width is the width of each rectangle, and spacer is the space
 * between the end of one rectangle and start of the next.
 * For "n" screens:
 * 
 *     (n * width) + ((n - 1) * spacer) = 600
 * 
 * Assume that all "spacer" space should add up to one rectangle. Then
 * 
 *     (n+1) * width = 600
 *     (n-1) * spacer = width
*/

#define WATCH_SCREEN_SELECT_RECTANGLE_MIN_X   (-299)
#define WATCH_SCREEN_SELECT_RECTANGLE_MAX_X   (301)
// default: 600
#define WATCH_SCREEN_SELECT_TOTAL_WIDTH       (WATCH_SCREEN_SELECT_RECTANGLE_MAX_X - WATCH_SCREEN_SELECT_RECTANGLE_MIN_X)
// default: 100
#define WATCH_SCREEN_SELECT_RECTANGLE_WIDTH   (s32)(WATCH_SCREEN_SELECT_TOTAL_WIDTH / (WATCH_NUMBER_SCREENS + 1))
// default: 25
#define WATCH_SCREEN_SELECT_SPACER_WIDTH      (s32)(WATCH_SCREEN_SELECT_RECTANGLE_WIDTH / (WATCH_NUMBER_SCREENS - 1))

/**
 * Horizontal spacing between watch menu screen select rectangles.
 * Default = 125.
*/
#define WATCH_SCREEN_SELECT_RECTANGLE_HSTEP (WATCH_SCREEN_SELECT_RECTANGLE_WIDTH + WATCH_SCREEN_SELECT_SPACER_WIDTH)

typedef enum WATCH_INDEX {
    WATCH_INDEX_MISSION_STATUS = 0,
    WATCH_INDEX_INVENTORY,
    WATCH_INDEX_CONTROL_OPTIONS,
    WATCH_INDEX_GAME_OPTIONS,
    WATCH_INDEX_MISSION_BRIEFING
} WATCH_INDEX;

typedef enum WATCH_CONTROLLER_OPTIONS_INDEX {
    CONTROLLER_OPTIONS_INDEX_STYLE = 0,
    CONTROLLER_OPTIONS_INDEX_INPUTS
} WATCH_CONTROLLER_OPTIONS_INDEX;

typedef enum WATCH_GAME_OPTIONS_INDEX {
    GAME_OPTIONS_INDEX_MUSIC = 0,
    GAME_OPTIONS_INDEX_FX,
    GAME_OPTIONS_INDEX_LOOK_UPDOWN,
    GAME_OPTIONS_INDEX_AUTO_AIM,
    GAME_OPTIONS_INDEX_AIM_CONTROL,
    GAME_OPTIONS_INDEX_SIGHT_ONSCREEN,
    GAME_OPTIONS_INDEX_LOOK_AHEAD,
    GAME_OPTIONS_INDEX_AMMO_ONSCREEN,
    GAME_OPTIONS_INDEX_SCREEN_SIZE,
    GAME_OPTIONS_INDEX_RATIO
} WATCH_GAME_OPTIONS_INDEX;
#ifndef __sgi
/* NATIVE ONLY (#44, 2026-09-18): a sixth solo-watch page, SIGHTLINE, for the
 * native-only settings - appended AFTER the five original pages so every index
 * the original build sees (WATCH_INDEX_*, WATCH_NUMBER_SCREENS, the player
 * struct's two five-slot buffers) is unchanged. It sits at the END of the
 * page ring: ... GAME OPTIONS <-> BRIEFING <-> SIGHTLINE <-> MISSION STATUS
 * ..., so the page bar's left-to-right order is the ring order with no
 * remapping. Its rows are views onto the platform / settings layers' ONE
 * state each (options.c, sl_draw_watch_sightline_page); nothing here reaches
 * game_options_entries or the save file. (The #39 "invert mouse y" row that
 * lived as an eleventh Game Options row moved here; Game Options is its
 * original ten rows again.)
 *
 * The page bar is rebuilt natively with six segments from the same formula
 * as the five above ((n + 1) * width = 600, (n - 1) * spacer = width) in a
 * per-frame dyn buffer (options.c draw_background_health_and_armor), because
 * the player struct's buffers are sized by WATCH_NUMBER_SCREENS and the
 * player is allocated at a fixed 0x2A80 (player.c initBONDdataforPlayer). */
#define WATCH_INDEX_SL_SIGHTLINE                 5
#define SL_WATCH_NUMBER_SCREENS                  6
#define SL_WATCH_SCREEN_SELECT_RECTANGLE_WIDTH   (s32)(WATCH_SCREEN_SELECT_TOTAL_WIDTH / (SL_WATCH_NUMBER_SCREENS + 1))
#define SL_WATCH_SCREEN_SELECT_SPACER_WIDTH      (s32)(SL_WATCH_SCREEN_SELECT_RECTANGLE_WIDTH / (SL_WATCH_NUMBER_SCREENS - 1))
#define SL_WATCH_SCREEN_SELECT_RECTANGLE_HSTEP   (SL_WATCH_SCREEN_SELECT_RECTANGLE_WIDTH + SL_WATCH_SCREEN_SELECT_SPACER_WIDTH)

/* The SIGHTLINE page (owner structure, #45 2026-09-19) is a menu of
 * SUB-MENUS, each a nested child view exactly like the BINDINGS child of #46
 * (a flag on the page, not a ring page; the six-segment bar keeps
 * representing the ring; BACK or Escape returns one level):
 *
 *     sightline            graphics  -> world detail      original (#43; #47 / #48 later)
 *                                       / back
 *                          gameplay  -> sprint  off on
 *                                       / sprint mode  hold toggle  (#56)
 *                                       / crouch mode  hold toggle  (#56) / back
 *                          display   -> window mode      WINDOWED (#52)
 *                                       / resolution       1280x720 (#52; informational in BORDERLESS)
 *                                       / vsync  off on (#52)
 *                                       / aspect ratio  -  16:9  + / [bar] field of view  91 / back
 *                          controls  -> button layout        DEFAULT (#63)
 *                                       / stick layout       DEFAULT (#63)
 *                                       / stick tuning -> (a second-level child, #51)
 *                                       / controller         xbox    (#63, informational)
 *                                       / invert mouse y  off on   / [bar] mouse sensitivity  100%
 *                                       / [bar] scoped sensitivity  100% (#50)
 *                                       / bindings -> (the #46 child) / back
 *     stick tuning (#51)   [bar] look sensitivity  100% / [bar] look deadzone  15%
 *                          / [bar] move deadzone  15% / back (one level: to controls)
 *     ([bar] = a MUSIC / FX-style slider over the label, SL_WROW_IS_SLIDER)
 *
 * The views are DATA (options.c sl_watch_views[]: a heading and a row list;
 * a row is a kind and an argument), the row index is one per level and the
 * back stack is a small stack of (view, row) pairs - two deep since #51,
 * because the CONTROLS child is full (nine rows reach the face's foot with
 * its two bars) and the controller's three tuning sliders open from it as a
 * child of their own; BACK returns one level, to CONTROLS on the row that
 * opened it. Every row drives the SAME setter the front end's tab calls - no
 * watch-local state, no sync. Row kinds and the pointer's cells
 * (sl_watch_pointer.c hit_sightline) follow the kind, not the view. */
#define SL_WROW_SUBMENU     0   /* arg = the child view id */
#define SL_WROW_DIMMED      1   /* visible, unselectable (the CHEATS-row convention) */
#define SL_WROW_TOGGLE_MINV 2   /* off / on cells at the toggles' columns */
#define SL_WROW_TOGGLE_SPRINT 3
#define SL_WROW_VALUE_ASPECT 4  /* `-` value `+` cells (#45) */
#define SL_WROW_VALUE_FOV   5
#define SL_WROW_BINDINGS    6   /* the action row opening the #46 child */
#define SL_WROW_BACK        7
#define SL_WROW_VALUE_MSENS 8   /* `-` <pct>% `+` cells (#50): MOUSE SENSITIVITY */
#define SL_WROW_VALUE_SSENS 9   /* SCOPED SENSITIVITY */
#define SL_WROW_VALUE_BLAYOUT 10 /* BUTTON LAYOUT (#63, 2026-09-20): a NAMED value row - the
                                  * label and the preset's name right-aligned at
                                  * SL_SIGHTLINE_X_BARVALUE, no `-` / `+` cells (the names
                                  * are too wide for the value column); the latched LEFT /
                                  * RIGHT step through the presets, a click on the name
                                  * steps up. Replaced SL_WROW_VALUE_PROFILE (10). */
#define SL_WROW_INFO_PAD    11  /* informational (#63): the attached controller's family,
                                 * unselectable like DIMMED, its value drawn */
#define SL_WROW_VALUE_SLAYOUT 12 /* STICK LAYOUT (#63), the same named-value idiom */
#define SL_WROW_VALUE_PSENS 13  /* LOOK SENSITIVITY (#51, 2026-09-20): the controller's look
                                 * gain, a slider row like MOUSE SENSITIVITY (sl_pad_tune_*) */
#define SL_WROW_VALUE_PDZ   14  /* LOOK DEADZONE (#51): percent of stick travel, a slider */
#define SL_WROW_VALUE_MDZ   15  /* MOVE DEADZONE (#51): the move pair's, a slider */
#define SL_WROW_TOGGLE_SMODE 16 /* SPRINT MODE (#56, 2026-09-20): a toggle row whose two cells
                                 * read hold / toggle (the text table's aim-control strings)
                                 * at the toggles' columns; 0 hold, 1 toggle, through
                                 * sl_action_mode / _set (sl_settings_apply.c) */
#define SL_WROW_TOGGLE_CMODE 17 /* CROUCH MODE (#56): the same */
#define SL_WROW_VALUE_WMODE 18  /* WINDOW MODE (#52, 2026-09-20): a NAMED value row - windowed /
                                 * borderless / fullscreen right-aligned at SL_SIGHTLINE_X_BARVALUE;
                                 * the latched LEFT / RIGHT step, a click on the name steps up,
                                 * through the request seam of src/platform/sl_window.c (the
                                 * SDL backend applies at its next frame reset and commits what
                                 * it read back; the row prints the APPLIED state) */
#define SL_WROW_VALUE_RES   19  /* RESOLUTION (#52): the same named idiom over the display's list
                                 * for the current mode ("1280x720"); reported as SL_WROW_INFO_RES
                                 * by sl_sightline_row_kind while the size is not the player's
                                 * to choose (BORDERLESS: the desktop owns it) */
#define SL_WROW_INFO_RES    20  /* RESOLUTION, informational: unselectable like INFO_PAD, the
                                 * desktop's size drawn dimmed */
#define SL_WROW_TOGGLE_VSYNC 21 /* VSYNC (#52): off / on at the toggles' columns, the applied
                                 * swap interval; a cell files a request */
#define SL_WROW_VALUE_WDETAIL 22 /* WORLD DETAIL (#43, 2026-09-21): a NAMED value row - original /
                                  * enhanced right-aligned at SL_SIGHTLINE_X_BARVALUE (eight
                                  * glyphs, too wide for the toggles' columns); the latched
                                  * LEFT / RIGHT and a click on the name step to the other
                                  * profile, through sl_world_detail_step (sl_settings_apply.c) */
#define SL_WROW_VALUE_TEXTURES 23 /* TEXTURES (#47, 2026-09-21): the same NAMED idiom - original /
                                   * community hd / xbla; the latched LEFT / RIGHT and a click on
                                   * the name step through the three sets, wrapping, through
                                   * sl_textures_step (sl_settings_apply.c) */
/* The #56 mode rows: toggle rows with hold / toggle for their two cells. */
#define SL_WROW_IS_MODE(k) ((k) == SL_WROW_TOGGLE_SMODE || (k) == SL_WROW_TOGGLE_CMODE)
/* A value row of any kind: the same LEFT / RIGHT latch (a step down / up). */
#define SL_WROW_IS_VALUE(k) ((k) == SL_WROW_VALUE_ASPECT || (k) == SL_WROW_VALUE_FOV \
                             || (k) == SL_WROW_VALUE_MSENS || (k) == SL_WROW_VALUE_SSENS \
                             || (k) == SL_WROW_VALUE_BLAYOUT || (k) == SL_WROW_VALUE_SLAYOUT \
                             || (k) == SL_WROW_VALUE_WMODE || (k) == SL_WROW_VALUE_RES \
                             || (k) == SL_WROW_VALUE_WDETAIL || (k) == SL_WROW_VALUE_TEXTURES \
                             || SL_WROW_IS_PAD_TUNE(k))
/* The #51 controller tuning rows, and each one's id for sl_pad_tune_*. */
#define SL_WROW_IS_PAD_TUNE(k) ((k) == SL_WROW_VALUE_PSENS || (k) == SL_WROW_VALUE_PDZ || (k) == SL_WROW_VALUE_MDZ)
#define SL_WROW_PAD_TUNE_ID(k) ((k) == SL_WROW_VALUE_PSENS ? 0 : (k) == SL_WROW_VALUE_PDZ ? 1 : 2)
/* A NAMED value row: label + name, no cells (the pointer hits the name). */
#define SL_WROW_IS_NAMED(k) ((k) == SL_WROW_VALUE_BLAYOUT || (k) == SL_WROW_VALUE_SLAYOUT \
                             || (k) == SL_WROW_VALUE_WMODE || (k) == SL_WROW_VALUE_RES \
                             || (k) == SL_WROW_VALUE_WDETAIL || (k) == SL_WROW_VALUE_TEXTURES)
/* A row the cursor never rests on. */
#define SL_WROW_IS_INERT(k) ((k) == SL_WROW_DIMMED || (k) == SL_WROW_INFO_PAD || (k) == SL_WROW_INFO_RES)
/* The SLIDER rows (#50 owner request, 2026-09-19: "make them sliders like
 * music and fx in the watch"): FIELD OF VIEW, MOUSE SENSITIVITY and SCOPED
 * SENSITIVITY draw as the Game Options page's MUSIC / FX bars - the 600x20
 * face rectangle of draw_music_volume_slider, filled to the value by
 * update_volume_slider_verts, with the label under it and the value
 * right-aligned to the bar's right edge. ASPECT RATIO stays a `-` value `+`
 * row: it is four discrete shapes, not a range. A slider row is taller by
 * SL_SIGHTLINE_SLIDER_EXTRA (the bar's line); its bar's top sits
 * SL_SIGHTLINE_BAR_ABOVE framebuffer pixels above its label's y, the bar
 * ending on the label as the MUSIC bar ends on "music" (z -275..-255 over
 * y 0x26: measured 2026-09-19 through the page projection, fb y 32.3..38.7).
 * The bar's x extent is the tracks' (the page bar's WATCH_SCREEN_SELECT_*),
 * which projects to fb x 64.67..255.96 at WATCHZOOM3 (measured) - the label
 * column XOFFSET_1 is its left edge, SL_SIGHTLINE_X_BARVALUE its right. */
#define SL_WROW_IS_SLIDER(k) ((k) == SL_WROW_VALUE_FOV \
                              || (k) == SL_WROW_VALUE_MSENS || (k) == SL_WROW_VALUE_SSENS \
                              || SL_WROW_IS_PAD_TUNE(k))
#define SL_SIGHTLINE_SLIDER_EXTRA      8
#define SL_SIGHTLINE_BAR_ABOVE         8
#define SL_SIGHTLINE_BAR_X0            WATCH_SCREEN_SELECT_RECTANGLE_MIN_X
#define SL_SIGHTLINE_BAR_W             WATCH_SCREEN_SELECT_TOTAL_WIDTH
#define SL_SIGHTLINE_BAR_H             20
#define SL_SIGHTLINE_X_BARVALUE        0x100
#define SL_WVIEW_SIGHTLINE  0
#define SL_WVIEW_GAMEPLAY   1
#define SL_WVIEW_DISPLAY    2
#define SL_WVIEW_CONTROLS   3
#define SL_WVIEW_STICK      4   /* STICK TUNING (#51): CONTROLS' second-level child */
#define SL_WVIEW_GRAPHICS   5   /* GRAPHICS (#43, 2026-09-21): world detail, back */
#define SL_WVIEW_COUNT      6
#define SL_WVIEW_ROWS_MAX   9   /* CONTROLS: button layout, stick layout, stick tuning, controller, invert, two sensitivities, bindings, back (#63, #51) */
#define SL_WVIEW_DEPTH_MAX  2   /* the back stack: SIGHTLINE -> CONTROLS -> STICK TUNING */
/* The #45 value rows' three cells: `-` steps down, `+` steps up, the value
 * itself steps up (the single-value idiom of the control-style row, with the
 * two arrows as cells the pointer can hit - four aspect columns did not read
 * cleanly on the round face beside the toggles' two). Centred like the
 * toggles' OFF / ON. */
#define SL_SIGHTLINE_X_MINUS           0xC0
#define SL_SIGHTLINE_X_VALUE           0xE1
#define SL_SIGHTLINE_X_PLUS            0x102
/* The view model, for the pointer layer: the current view's rows. */
s32         sl_sightline_view(void);                 /* SL_WVIEW_* in force */
s32         sl_sightline_row_count(void);            /* rows of the view in force (BACK included) */
s32         sl_sightline_row_kind(s32 row);          /* SL_WROW_* */
const char *sl_sightline_row_label(s32 row);         /* the label text, LF-terminated */
/* One press on a row: value < 0 = the label (a sub-menu opens, BACK returns,
 * BINDINGS opens the child, a toggle / value row latches); value >= 0 = a
 * cell (toggles: 0 off / 1 on; value rows: 0 `-`, 1 the value, 2 `+`; a
 * slider row has no cells - 0 / 2 are its step down / up, the latched
 * LEFT / RIGHT). */
void        sl_sightline_click(s32 row, s32 value);
/* A slider row's bar (#50): set the value at a fraction 0..1 along the bar,
 * snapped to the value's own step grid (a click or a drag on the bar,
 * sl_watch_pointer.c). Any other row: nothing. */
void        sl_sightline_slide(s32 row, f32 t);
void        sl_sightline_back(void);                 /* one level up; nothing at the top */
void        sl_sightline_request_back(void);         /* Escape: taken on the next navigation tick */
s32         sl_sightline_child_open(void);           /* a child view (not the top) is in force */
/* Layout: the heading where the Control Options page puts "control style"
 * (XOFFSET_1, 0x1A); the rows from the "controller" row's y, YINC apart,
 * each slider row (and every row below it) SL_SIGHTLINE_SLIDER_EXTRA lower
 * for its bar - so the y is a walk over the view in force, not a formula. */
#define SL_SIGHTLINE_HEADING_Y         0x1A
#define SL_SIGHTLINE_ROW_Y0            0x2B
s32         sl_sightline_row_y(s32 row);             /* the label's y in the view in force */
/* The child view's list: the same heading line, rows from the same y, this
 * many visible at once (the Game Options page reaches y 209 with its eighth
 * toggle row; nine rows from 0x2B end at 179 + 10). */
#define SL_WATCH_BINDINGS_VISIBLE      9
#define SL_WATCH_BINDINGS_ROW_Y(i)     (0x2B + (i) * YINC)
#define SL_WATCH_BINDINGS_MSG_Y        (0x2B + SL_WATCH_BINDINGS_VISIBLE * YINC)
/* The value columns, LEFT-ALIGNED (the toggle rows centre theirs, but a
 * fourteen-glyph value - PRESS A BUTTON - centred on the outer columns ran
 * into a fifteen-glyph label; measured 2026-09-18, ~7.5 fb units a glyph).
 * An ACTION row shows ONE value (the slot the SLOT row selects) at X_VALUE,
 * past PREVIOUS WEAPON's end; the two-value rows (DEVICE, SLOT, BUTTON
 * MODE) sit at X_V0 / X_V1 so CONTROLLER and SECONDARY end before the
 * round face's right edge on the top rows (near fb 300). */
#define SL_WATCH_BINDINGS_X_VALUE      0xB4
#define SL_WATCH_BINDINGS_X_V0         0x9C
#define SL_WATCH_BINDINGS_X_V1         0xE4
#endif

typedef enum WATCH_BRIEF_INDEX {
    BRIEF_INDEX_BACKGROUND = 0,
    BRIEF_INDEX_M,
    BRIEF_INDEX_Q,
    BRIEF_INDEX_MONEYPENNY,
    BRIEF_INDEX_OBJECTIVES
} WATCH_BRIEF_INDEX;

struct game_options {
    u16 text[4];
    u32 current_value;
};

extern struct game_options game_options_entries[];

void reset_controller_options_index(void);

void reset_game_options_index(void);

void zero_D_800409A4(void);

f32 watchWrapAroundPI(f32 arg0);

f32 sub_GAME_7F0A95C4(f32 param_1, f32 param_2, f32 param_3);

SCREEN_RATIO_OPTION get_screen_ratio(void);

void set_screen_ratio(SCREEN_RATIO_OPTION ratio_option);

s32 cur_player_get_autoaim(void);
u32 cur_player_get_lookahead(void);
void cur_player_set_autoaim(u32 param_1);
int cur_player_get_control_type(void);
u32 cur_player_get_sight_onscreen_control(void);
u32 cur_player_get_ammo_onscreen_setting(void);
u32 cur_player_get_aim_control(void);
u32 cur_player_get_screen_setting(void);
u16 call_sndGetSfxSlotFirstNaturalVolume(void);
u32 get_cur_player_look_vertical_inverted(void);
u16 get_mTrack2Vol(void);
void set_mTrack2Vol(u16 param_1);
void sub_GAME_7F0A91A0(u16 arg0);
void cur_player_set_control_type(int type);

// Do not declare a public prototype for this method. lvlStageLoad is expecting
// this to be defined with no arguments, but the actual method is defined with
// one argument.
//void init_watch_at_start_of_stage(int a);

Gfx *draw_watch_current_page(Gfx *gdl, Mtx *arg1, s32 watch_transitioning);
void sub_GAME_7F0A69A8(void);

#endif


