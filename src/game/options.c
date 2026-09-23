#include <ultra64.h>
#include <bondconstants.h>
#include <boss.h>
#include <fr.h>
#include <music.h>
#include <os_extension.h>
#include <snd.h>
#include <random.h>
#include "options.h"
#include "bondview.h"
#include "dyn.h"
#include "file.h"
#include "front.h"
#include "language.h"
#include "player.h"
#include "textrelated.h"
#include "glass.h"
#include "frametiming.h"
#include "assets/obseg/text/LoptionE.h"
#ifndef __sgi
/* NATIVE ONLY - zero tokens for IDO, so the matching build is byte-identical.
 *
 * This file calls joyGetStickX, joyGetStickY, joy7000C174 and joy7000C284 with
 * NO PROTOTYPE IN SCOPE - it is the only file in the tree that does (verified
 * by compiling every file under src/ and src/game/ with -Wall and grepping for
 * the implicit declaration of those four; four call sites, all here). An
 * implicitly declared function returns `int`; all four actually return `s8`.
 *
 * On MIPS that is harmless - IDO sign-extends an s8 return into the return
 * register, so the caller's `int` is the right value and the ROM behaves
 * correctly. On the x86-32 native target it is NOT: an s8 return only defines
 * %al, the caller reads all of %eax, and a stick of -80 arrives as 176.
 *
 * MEASURED, headless, with a synthetic pad stream holding stick_y = -80:
 *   before   is_holding_greater_than_2E_up_on_stick() = 1
 *            is_holding_greater_than_2E_down_on_stick() = 0
 * i.e. pushing DOWN read as UP, because (176 < 0x2e) is false. The same fault
 * makes is_holding_..._left_on_stick() (`x < -0x2d`) unreachable for any stick
 * value at all. That is the owner's "vertical menu navigation does nothing,
 * horizontal works": both vertical directions became UP and both horizontal
 * directions became RIGHT, so only one axis appeared to respond.
 *
 * This is a NATIVE ABI defect, not one of Rare's - rule 5 is satisfied because
 * the fix restores the ROM's behaviour rather than changing it. joy.h is
 * self-contained and its four declarations match src/joy.c exactly, so the only
 * thing this changes is that the native compiler now knows the return type. */
#include "joy.h"

/* THE WATCH'S POINTER (#40, src/native/sl_watch_pointer.c). Three seams and
 * no more: the frame hook, called at the head of the interactive branch of
 * draw_watch_current_page with the page's own matrix so the native layer can
 * hit-test the pointer against what THIS frame draws (the text rows in
 * framebuffer space, the page bar and the volume tracks through the same
 * projection the RSP is handed); the cursor draw at the end of the same
 * branch (the game's own crosshair, half size, over the page); and the equip
 * take, read where A and Z equip on the equipment page so a click on a list
 * row equips through the same lines. Everything else the pointer does is a
 * write to the same globals the stick path writes, from the native side, so
 * there is no second highlight. */
extern void sl_watch_pointer_frame(Mtx *pagemtx);
extern Gfx *sl_watch_pointer_draw(Gfx *gdl);
extern s32  sl_watch_pointer_take_equip(void);

/* NATIVE (#44): the SIGHTLINE watch page - its row index and navigation
 * (defined beside the other watch_screenN_navigation functions below), its
 * draw (beside the other page draws) and the six-segment page bar. The two
 * settings the page shows live elsewhere and are reached by their one setter
 * each: Invert Mouse Y in the platform layer (sl_input.c, #39), Sprint in the
 * native settings store through sl_settings_apply.c (#42). No value is held
 * here; the front end's Settings page reads and writes the same two. */
extern u32  sl_sightline_row_index;
void sl_reset_sightline_row_index(void);
void sl_watch_sightline_navigation(void);
Gfx *sl_draw_watch_sightline_page(Gfx *gdl, Mtx *param_2);
extern int  sl_mouse_invert_y_get(void);
extern void sl_mouse_invert_y_set(int on);
extern int  sl_sprint_enabled(void);
extern void sl_sprint_enabled_set(int on);
/* #56: the GAMEPLAY child's SPRINT MODE / CROUCH MODE (HOLD / TOGGLE),
 * through the same two calls the front end's GAMEPLAY tab makes
 * (sl_settings_apply.c; which 0 = CROUCH, 1 = SPRINT). */
extern int  sl_action_mode(int which);
extern void sl_action_mode_set(int which, int toggle);
/* #45: the DISPLAY tab's two settings, through the same accessors the front
 * end uses (src/platform/sl_display.c) - no watch-local state. */
extern int         sl_aspect_ratio(void);
extern void        sl_aspect_ratio_set(int aspect);
extern int         sl_aspect_by_order(int column);
extern int         sl_aspect_order_of(int aspect);
extern const char *sl_aspect_name(int aspect);
extern int         sl_fov_h16_displayed(void);
extern void        sl_fov_step(int delta);
/* #50: the CONTROLS child's two sensitivity percents, through the same two
 * calls the front end's CONTROL tab makes (src/platform/sl_input.c). */
extern int         sl_mouse_sens_get(int scoped);
extern void        sl_mouse_sens_step(int scoped, int dir);
/* The SLIDER rows (#50 owner request, 2026-09-19): the three value rows'
 * fill fractions and their set-from-a-fraction, the platform layer's own
 * (sl_input.c / sl_display.c: the same grid and clamps as the steps), and
 * the face z under a framebuffer y through the page's projection
 * (sl_watch_pointer.c), so the bar drawn on the face sits over its label. */
extern float       sl_mouse_sens_fraction(int scoped);
extern void        sl_mouse_sens_set_fraction(int scoped, float t);
/* #51: the STICK TUNING child's three controller rows, the same four calls
 * (src/platform/sl_input.c sl_pad_tune_*; which = SL_WROW_PAD_TUNE_ID). */
extern int         sl_pad_tune_get(int which);
extern void        sl_pad_tune_step(int which, int dir);
extern float       sl_pad_tune_fraction(int which);
extern void        sl_pad_tune_set_fraction(int which, float t);
extern float       sl_fov_fraction(void);
extern void        sl_fov_set_fraction(float t);
extern f32         sl_watch_face_z_at_fb_y(Mtx *pagemtx, f32 y);
/* #63: the attached pad's family (sl_input.c), the device-matched controller
 * draw the Control Options page makes (src/native/sl_watch_controller.c),
 * and the two LAYOUT rows both the Control Options page and the CONTROLS
 * child show (2026-09-20, the N64 CONTROL STYLE and the ORIGINAL / MODERN
 * PROFILE gone): BUTTON LAYOUT is the binding registry's preset
 * (sl_bindings.c, seeding the pad slots), STICK LAYOUT the settings store's
 * row the pad mapping reads. The names and the step are the native layer's
 * (sl_watch_controller.c sl_watch_layout_*); nothing here holds a copy. */
extern int         sl_input_pad_family(void);
extern const char *sl_input_pad_family_name(int family);
extern int         sl_watch_controller_modern(void);
extern Gfx        *sl_watch_controller_draw(Gfx *gdl, Mtxf *finalmtx, s32 green);
extern void        sl_watch_controller_probe(Mtxf *finalmtx);   /* SL_PAD_DBG: the facing, both arms */
extern const char *sl_watch_layout_name(s32 which);        /* 0 BUTTON, 1 STICK */
extern void        sl_watch_layout_step(s32 which, s32 dir);
extern Gfx        *sl_watch_layout_rows_draw(Gfx *gdl);    /* the Control Options page's two rows */
/* #52 (2026-09-20): the DISPLAY child's WINDOW MODE / RESOLUTION / VSYNC,
 * through the request seam the front end's DISPLAY tab uses
 * (src/platform/sl_window.h): the rows print the state the SDL backend
 * applied and read back, a step files a request the backend takes at its
 * next frame reset - and commits only what SDL confirmed. No copy here. */
extern int         sl_window_mode_shown(void);
extern const char *sl_window_mode_name(int mode);
extern void        sl_window_mode_step(int dir);
extern int         sl_window_size_shown(int *w, int *h);
extern int         sl_window_size_editable(void);
extern void        sl_window_size_step(int dir);
extern void        sl_window_size_text(int w, int h, char *buf, int n);
extern int         sl_window_state(int *mode, int *w, int *h, int *vsync);
extern void        sl_window_request_vsync(int on);
extern int         sl_window_request_pending(void);
/* #43 (2026-09-21): the GRAPHICS child's WORLD DETAIL, through the same
 * accessor pair the front end's DISPLAY tab uses (sl_settings_apply.c: the
 * profile id, its name from the store, the step to the other profile). The
 * render seams read the store themselves; no copy here. */
extern int         sl_world_detail(void);
extern const char *sl_world_detail_name(int detail);
extern void        sl_world_detail_step(int dir);
/* #47 (2026-09-21): the GRAPHICS child's TEXTURES, the same shape: the set
 * id, its name from the store, the step through the three sets. The
 * renderer's provider reads the store itself; no copy here. */
extern int         sl_textures(void);
extern const char *sl_textures_name(int set);
extern void        sl_textures_step(int dir);
#endif

#define WATCH_BACKGROUND_VERTEX_COUNT 30

#define WATCH_VOL_ADJUST_STEP 1024

#if defined(VERSION_US)
#define WATCH_ROTATION_FRAMES speedgraphframes
#else
#define WATCH_ROTATION_FRAMES jpD_800484D0
#endif

#if defined(VERSION_EU)
#define WATCH_PERSPECTIVE_FOVY    52.5f
#define WATCH_PERSPECTIVE_ASPECT  1.283847f
#else
#define WATCH_PERSPECTIVE_FOVY    50.5f
#define WATCH_PERSPECTIVE_ASPECT  1.3333334f
#endif

#if defined(VERSION_EU)
#define OPTLABELS_ROW1_Y    0x5c
#define OPTLABELS_ROW2_Y    0x7a
#define OPTLABELS_ROW_PITCH 0x1e
#define OPTLABELS_COL_RET   0x5a
#define OPTLABELS_HINT_Y    0xe1
#else
#define OPTLABELS_ROW1_Y    0x52
#define OPTLABELS_ROW2_Y    0x6b
#define OPTLABELS_ROW_PITCH 0x19
#define OPTLABELS_COL_RET   0x4b
#define OPTLABELS_HINT_Y    0xc3
#endif

// bss
Mtx gfx_background_8007B0A0;
Mtx gfx_background_8007B0E0;

u32 D_80040990 = 0;
u32 watch_screen_index = 0;
u32 controller_options_index = 0;
u32 game_options_index = 0;
// data
//D:800409A0
s32 mission_brief_index = BRIEF_INDEX_OBJECTIVES;
//D:800409A4
s32 D_800409A4 = 0;
//D:800409A8
s32 watch_item_is_actively_selected = 0;
//D:800409AC
s32 D_800409AC = 0;
//D:800409B0
s32 watch_inventory_text_y = 0;
//D:800409B4
s32 watch_inventory_text_target_y = 0;
//D:800409B8
s32 g_curWatchItemIndex = 0;
//D:800409BC
f32 watch_inventory_cursor_pos = 0.0f;
//D:800409C0
bool watch_inventory_text_is_settled = FALSE;
//D:800409C4
s32 D_800409C4 = 0;
//D:800409C8
f32 D_800409C8 = 0.0f;
//D:800409CC
f32 D_800409CC = 0.0f;
//D:800409D0
s32 D_800409D0 = -1;
//D:800409D4
f32 D_800409D4 = 0.0f;
//D:800409D8
s32 D_800409D8 = 8;
//D:800409DC
u16 game_control_styles[] = {
    /*1.1 honey*/    getStringID(LOPTIONS, OPTION_STR_09_11HONEY_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*action*/getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF), /*fire*/getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF),    /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF),    /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF), /*pause*/getStringID(LOPTIONS, OPTION_STR_04_PAUSE_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF),
    /*1.2 solitaire*/getStringID(LOPTIONS, OPTION_STR_0A_12SOLITAIRE_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*action*/getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF), /*fire*/getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF),    /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF),    /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF), /*pause*/getStringID(LOPTIONS, OPTION_STR_04_PAUSE_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF),
    /*1.3 kissy*/    getStringID(LOPTIONS, OPTION_STR_0B_13KISSY_LF),   /*fire*/getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF), /*action*/getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF),  /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF), /*pause*/getStringID(LOPTIONS, OPTION_STR_04_PAUSE_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF),
    /*1.4 goodnight*/getStringID(LOPTIONS, OPTION_STR_0C_14GOODNIGHT_LF),   /*fire*/getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF), /*action*/getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF),  /*aim*/getStringID(LOPTIONS, OPTION_STR_01_AIM_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*weapon*/getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF), /*move*/getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF), /*pause*/getStringID(LOPTIONS, OPTION_STR_04_PAUSE_LF), /*look*/getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF),
    /*2.1 plenty*/   getStringID(LOPTIONS, OPTION_STR_0D_21PLENTY_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),
    /*2.2 galore*/   getStringID(LOPTIONS, OPTION_STR_0E_22GALORE_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),
    /*2.3 domino*/   getStringID(LOPTIONS, OPTION_STR_0F_23DOMINO_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),
    /*2.4 goodhead*/ getStringID(LOPTIONS, OPTION_STR_10_24GOODHEAD_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),      /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),     /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF),    /*?*/getStringID(LOPTIONS, OPTION_STR_07_QUESTION_LF)
};

struct game_options game_options_entries[] = {
    { {getStringID(LOPTIONS, OPTION_STR_11_LOOKUPDOWN_LF), getStringID(LOPTIONS, OPTION_STR_1C_REVERSE_LF), getStringID(LOPTIONS, OPTION_STR_1B_UPRIGHT_LF), 0}, 0}, //look up/down, reverse, upright
    { {getStringID(LOPTIONS, OPTION_STR_12_AUTOAIM_LF), getStringID(LOPTIONS, OPTION_STR_1A_OFF_LF), getStringID(LOPTIONS, OPTION_STR_19_ON_LF), 0}, 1}, //autoaim, off, on
    { {getStringID(LOPTIONS, OPTION_STR_14_AIMCONTROL_LF), getStringID(LOPTIONS, OPTION_STR_1E_HOLD_LF), getStringID(LOPTIONS, OPTION_STR_1D_TOGGLE_LF), 0}, 0}, //aim control, hold, toggle
    { {getStringID(LOPTIONS, OPTION_STR_15_SIGHTONSCREEN_LF), getStringID(LOPTIONS, OPTION_STR_1A_OFF_LF), getStringID(LOPTIONS, OPTION_STR_19_ON_LF), 0}, 1}, //sight on screen, off, on
    { {getStringID(LOPTIONS, OPTION_STR_13_LOOKAHEAD_LF), getStringID(LOPTIONS, OPTION_STR_1A_OFF_LF), getStringID(LOPTIONS, OPTION_STR_19_ON_LF), 0}, 1}, //look ahead, off, on
    { {getStringID(LOPTIONS, OPTION_STR_16_AMMOONSCREEN_LF), getStringID(LOPTIONS, OPTION_STR_1A_OFF_LF), getStringID(LOPTIONS, OPTION_STR_19_ON_LF), 0}, 1}, //ammo on screen, off, on
    { {getStringID(LOPTIONS, OPTION_STR_17_SCREEN_LF), getStringID(LOPTIONS, OPTION_STR_1F_FULL_LF), getStringID(LOPTIONS, OPTION_STR_20_WIDE_LF), getStringID(LOPTIONS, OPTION_STR_21_CINEMA_LF)}, 0}, //screen, full, wide, cinema
    { {getStringID(LOPTIONS, OPTION_STR_18_RATIO_LF), getStringID(LOPTIONS, OPTION_STR_22_NORMAL_LF), getStringID(LOPTIONS, OPTION_STR_23_169_LF), 0}, 0} //ratio, normal, 16:9
};

//D:80040ADC
u32 controlstick_lr_enabled = 0;
//D:80040AE0
u32 watch_stick_y_nav_ready = 0;
//D:80040AE4
u32 watch_stick_y_prev_active = 0;
//D:80040AE8
f32 D_80040AE8 = 0.0f;
//D:80040AEC
f32 D_80040AEC = 0.0f;
//D:80040AF0
f32 D_80040AF0 = 45.0f;
//D:80040AF4
u32 D_80040AF4 = 0xFF00A0;
//D:80040AF8
u32 D_80040AF8 = 0xA;
//D:80040AFC
u32 D_80040AFC = 0xFF;

//D:80040B00
u32 D_80040B00 = 0xA;

//D:80040B04
s32 g_WatchBackgroundGreen = 0xE0;

//D:80040B08
u32 g_WatchStaticScanlineAlpha = 0;

//D:80040B0C
u32 D_80040B0C = 0xFFA0;
//D:80040B10
u32 D_80040B10 = 0xF800;
//D:80040B14
f32 D_80040B14 = 0.0f;
//D:80040B18
f32 D_80040B18 = 0.0f;
//D:80040B1C
f32 D_80040B1C = 2.5f;
//D:80040B20
f32 g_WatchControllerPitch = 0.0f;
//D:80040B24
f32 g_WatchControllerSpinAngle = 0.0f;
//D:80040B28
f32 g_WatchControllerSpinSpeed = 0.0f;
//D:80040B2C
s32 D_80040B2C = 0;
//D:80040B30
f32 D_80040B30 = 0.0f;
//D:80040B34
f32 D_80040B34 = 0.0f;
//D:80040B38
f32 D_80040B38 = 0.0f;
//D:80040B3C
s32 D_80040B3C = 0;
//D:80040B40
s32 g_WatchStaticScanlineY = 0;
//D:80040B44
u16 D_80040B44 = 0x1;
//D:80040B48
u32 D_80040B48 = 0x32;
//D:80040B4C
u32 D_80040B4C = 0x32;
//D:80040B50
u32 D_80040B50 = 0x32;
//D:80040B54
u32 D_80040B54 = 0x32;
//D:80040B58
u16 mTrack2Vol = 0x7FFF;

//D:80040B5C
coord3d g_ControllerPos = {0.0f, 200.0f, -200.0f};

typedef struct WatchContButtonPositions {
    f32 words[55]; /* 0xdc bytes */
} WatchContButtonPositions;

//D:80040B68
// 3D positions for the buttons at the sides of the screen for the 1x control styles.
WatchContButtonPositions g_1ContButtonPositions[] = {{
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f,
    -900.0f, 200.0f, -45.0f,     // Start
    715.0f, 200.0f, 393.0f,      // Joy Stick
    -875.0f, 200.0f, -210.0f,    // D-Pad
    900.0f, 200.0f, -260.0f,     // C-Up
    900.0f, 200.0f, -160.0f,     // C-Down
    850.0f, 200.0f, -210.0f,     // C-Left
    950.0f, 200.0f, -210.0f,     // C-Right
    900.0f, 200.0f, 128.0f,      // A
    900.0f, 200.0f, -45.0f,      // B
    -820.0f, 200.0f, -389.0f,    // L
    820.0f, 200.0f, -389.0f,     // R
    -830.0f, 200.0f, 78.0f       // Z
}};

//D:80040C44
// 3D positions for the buttons of the left controller for the 2x control styles.
WatchContButtonPositions g_2ContLeftButtonPositions[] = {{
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, -600.0f, 200.0f,
    500.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, -600.0f, 200.0f,
    240.0f, -600.0f, 200.0f,
    110.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f,
    2000.0f, -600.0f, 200.0f,
    320.0f
}};

//D:80040D20
// 3D positions for the buttons of the right controller for the 2x control styles.
WatchContButtonPositions g_2ContRightButtonPositions[] = {{
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    2000.0f, 0.0f, 2000.0f, 2000.0f,
    0.0f, 2000.0f, 600.0f, 200.0f,
    500.0f, 2000.0f, 0.0f, 2000.0f,
    2000.0f, 0.0f, 2000.0f, 2000.0f,
    0.0f, 2000.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f, 2000.0f,
    600.0f, 200.0f, 240.0f, 600.0f,
    200.0f, 110.0f, 2000.0f, 0.0f,
    2000.0f, 2000.0f, 0.0f, 2000.0f,
    600.0f, 200.0f, 320.0f
}};

//D:80058440
const char D_80058440[];
//D:80058444
const char D_80058444[];
//D:80058448
const char aC_2[];
//D:80058450
const char D_80058450[];
//D:80058454
const char D_80058454[];

// forward declarations

void set_page_rectangle_colors(s32 watch_screen_index, struct WatchVertex *vertices);
Gfx *draw_watch_mission_status_page(Gfx *gdl, Mtx *param_2);
Gfx *unused_draw_watch_inventory_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_watch_inventory_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_watch_control_options_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_watch_game_options_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_watch_mission_briefing_page(Gfx *gdl, Mtx *param_2);
Gfx *draw_background_health_and_armor_transitioning(Gfx *gdl, Mtx *param_2);
Gfx *draw_background_health_and_armor(Gfx *gdl, Mtx *arg1, s32 zoom_squish);
void game_option_select_value(u32 *param_1, u32 param_2);
void watch_adjust_volume_slider(u16* arg0);
Gfx *sub_GAME_7F0A3B40(Gfx *gdl, s32 *arg1);
void update_volume_slider_verts(struct WatchVertex *verts, f32 fill_amount, s32 transition_width);
void sub_GAME_7F0A9684(s8 contpadnum, s32 *counter, f32 *value, f32 *step);
Gfx *display_text_buttons_dual_control(Gfx *gdl);
Gfx *sub_GAME_7F0A9AB8(Gfx *gdl);

// end forward declarations



void nullsub_7F0A4860(void)
{

}


void init_watch_at_start_of_stage(int stage)
{
    watch_screen_index = WATCH_INDEX_MISSION_STATUS;
    controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
    game_options_index = GAME_OPTIONS_INDEX_MUSIC;
    mission_brief_index = BRIEF_INDEX_OBJECTIVES;
    D_800409A4 = 0;
    watch_item_is_actively_selected = 0;
    D_800409AC = 0;
    watch_inventory_text_y = 0;
    watch_inventory_text_target_y = 0;
    g_curWatchItemIndex = 0;
    watch_inventory_cursor_pos = 0.0f;
    watch_inventory_text_is_settled = FALSE;
    D_800409C4 = 0;
    D_800409C8 = 0.0f;
    D_800409CC = 0.0f;
    D_800409D0 = -1;
    D_800409D4 = 0.0f;

    if (j_text_trigger ? 1 : 0 && 1)
    {
    }

    g_CurrentPlayer->neg_vspacing_for_control_type_entry = 0;
    g_CurrentPlayer->cur_player_control_type_1 = CONTROLLER_CONFIG_HONEY;
    g_CurrentPlayer->cur_player_control_type_0 = CONTROLLER_CONFIG_HONEY;
    g_CurrentPlayer->cur_player_control_type_2 = 0.0f;
    g_CurrentPlayer->has_set_control_type_data = TRUE;
    D_800409D8 = 8;

    controlstick_lr_enabled = 0;
    watch_stick_y_nav_ready = 0;
    watch_stick_y_prev_active = 0;
    D_80040AE8 = 0.0f;
    D_80040AEC = 0.0f;
    D_80040AF0 = 45.0f;
    D_80040AF4 = 0xff00a0;
    D_80040AF8 = 10;
    D_80040AFC = 0xff;
    D_80040B00 = 10;
    g_WatchBackgroundGreen = 0xe0;
    g_WatchStaticScanlineAlpha = 0;
    D_80040B0C = 0xffa0;
    D_80040B10 = 0xf800;
    D_80040B14 = 0.0f;
    D_80040B18 = 0.0f;
    D_80040B1C = 2.5f;
    g_WatchControllerPitch = 0.0f;
    g_WatchControllerSpinAngle = 0.0f;
    g_WatchControllerSpinSpeed = 0.0f;
    D_80040B2C = 0;
    D_80040B30 = 0.0f;
    D_80040B34 = 0.0f;
    D_80040B38 = 0.0f;
    D_80040B3C = 0;
    g_WatchStaticScanlineY = 0;
    D_80040B44 = 1;
    D_80040B48 = 0x32;
    D_80040B4C = 0x32;
    D_80040B50 = 0x32;
    D_80040B54 = 0x32;
    fileLoadSaveSettingsForSelectedFolder(stage);
#ifndef __sgi
    /* NATIVE ONLY (#41): the player's GLOBAL DEFAULTS from the native settings
     * store, applied on top of the folder's options through the same three
     * setters the folder load above just used (src/native/sl_settings_apply.c).
     * Here, and not earlier, because this is the one point every stage start
     * reaches - the front-end flow and SL_BOOT_LEVEL alike - after the player
     * exists and after the save has had its say. A no-op when the store is
     * inactive (trace replay, headless health). */
    {
        extern void sl_settings_apply_player_defaults(void);
        sl_settings_apply_player_defaults();
    }
#endif
    mission_failed_or_aborted = FALSE;
}


void controller_deadzone_related(void)
{
    if (10 < joyGetStickX(PLAYER_1))
    {
        D_80040B50 = D_80040B50 + 1;
    }
    if (joyGetStickX(PLAYER_1) < -10)
    {
        D_80040B50 = D_80040B50 + -1;
    }
    if (10 < joyGetStickY(PLAYER_1))
    {
        D_80040B54 = D_80040B54 + -1;
    }
    if (joyGetStickY(PLAYER_1) < -10)
    {
        D_80040B54 = D_80040B54 + 1;
    }
}


Gfx * sub_GAME_7F0A4B40(Gfx *DL)
{
    if (10 < joyGetStickX(PLAYER_1))
    {
        D_80040B48 += 1;
    }

    if (joyGetStickX(PLAYER_1) < -10)
    {
        D_80040B48 -= 1;
    }

    if (10 < joyGetStickY(PLAYER_1))
    {
        D_80040B4C -= 1;
    }

    if (joyGetStickY(PLAYER_1) < -10)
    {
        D_80040B4C += 1;
    }

    gDPSetRenderMode(DL++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(DL++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(DL++, 0, 0, 0xFF, 0x00, 0x00, 0xFF);
    gDPFillRectangle(DL++, D_80040B48, D_80040B4C, D_80040B48+1, D_80040B4C+1);

    {
        u8 buffer [0x12];
        struct font * pFontFile;
        struct fontchar * pFontChars;
        s32 y;
        s32 x;

        pFontFile = ptrFontBankGothic;
        pFontChars = ptrFontBankGothicChars;
        sprintf(buffer,"%d, %d\n",D_80040B48,D_80040B4C);

        DL = microcode_constructor(DL++);

        textMeasure(&x, &y, buffer, pFontChars, pFontFile, 0);
        DL = textRender(DL, &D_80040B48, &D_80040B4C, buffer, pFontChars, pFontFile, 0xff0000ff, y, x, 0, 0);
        // HACK: what is this: ((s32*)pFontChars)[0x224]
        D_80040B4C = (D_80040B4C - ((s32*)pFontChars)[0x224]) + 1;
    }

    return DL;
}


u32 controllerCheckDualControllerTypesAllowed(void)
{
    if (joyGetControllerCount() >= 2)
    {
        if (cur_player_get_control_type() >= 4)
        {
            return 1;
        }
    }
    return 0;
}


int cur_player_get_control_type(void){
  return g_CurrentPlayer->cur_player_control_type_0;
}


void cur_player_set_control_type(int type)
{
    int langsize;

    g_CurrentPlayer->cur_player_control_type_0 = type;
    g_CurrentPlayer->cur_player_control_type_1 = type;
    g_CurrentPlayer->cur_player_control_type_2 = (float)type;

    langsize = j_text_trigger ? 14 : 10;

    g_CurrentPlayer->neg_vspacing_for_control_type_entry = -(langsize * type);
    g_CurrentPlayer->has_set_control_type_data = TRUE;

}

u32 get_cur_player_look_vertical_inverted(void)
{
    return game_options_entries[PLAYER_OPTION_LOOK].current_value;
}

void set_cur_player_look_vertical_inverted(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_LOOK].current_value = param_1;
}

s32 cur_player_get_autoaim(void)
{
    return game_options_entries[PLAYER_OPTION_AUTOAIM].current_value;
}

void cur_player_set_autoaim(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_AUTOAIM].current_value = param_1;
}

u32 cur_player_get_lookahead(void)
{
    return game_options_entries[PLAYER_OPTION_LOOKAHEAD].current_value;
}

void cur_player_set_lookahead(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_LOOKAHEAD].current_value = param_1;
}

u32 cur_player_get_aim_control(void)
{
    return game_options_entries[PLAYER_OPTION_AIM].current_value;
}

void cur_player_set_aim_control(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_AIM].current_value = param_1;
}

u32 cur_player_get_sight_onscreen_control(void)
{
    return game_options_entries[PLAYER_OPTION_SIGHT].current_value;
}
void cur_player_set_sight_onscreen_control(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_SIGHT].current_value = param_1;
}

u32 cur_player_get_ammo_onscreen_setting(void)
{
    return game_options_entries[PLAYER_OPTION_AMMODISPLAY].current_value;
}
void cur_player_set_ammo_onscreen_setting(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_AMMODISPLAY].current_value = param_1;
}

u32 cur_player_get_screen_setting(void)
{
    return game_options_entries[PLAYER_OPTION_SCREEN].current_value;
}
void cur_player_set_screen_setting(u32 param_1)
{
    game_options_entries[PLAYER_OPTION_SCREEN].current_value = param_1;
}

SCREEN_RATIO_OPTION get_screen_ratio(void)
{
    return game_options_entries[PLAYER_OPTION_RATIO].current_value;
}

void set_screen_ratio(SCREEN_RATIO_OPTION ratio_option)
{
    game_options_entries[PLAYER_OPTION_RATIO].current_value = ratio_option;
}


void watch_play_beep_sound(void) {

    if (watch_item_is_actively_selected == 1) {
        watch_item_is_actively_selected = 0;

    } else {
        watch_item_is_actively_selected = 1;
        sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
    }
}


void reset_watch_item_is_actively_selected(void){
  watch_item_is_actively_selected = 0;
}


u32 is_holding_greater_than_2E_left_on_stick(void)
{
    return (joyGetStickX(PLAYER_1) < -0x2d);
}


u32 is_holding_greater_than_2E_right_on_stick(void)
{
    return ((joyGetStickX(PLAYER_1) < 0x2e) ^ 1);
}


u32 get_controlstick_lr_enabled(void) {
  return controlstick_lr_enabled;
}


void set_controlstick_lr_disabled(void) {
  controlstick_lr_enabled = 0;
}


s32 sub_GAME_7F0A4FB0(void)
{
    return is_holding_greater_than_2E_left_on_stick() && get_controlstick_lr_enabled();
}


s32 sub_GAME_7F0A4FEC(void)
{
    return is_holding_greater_than_2E_right_on_stick() && get_controlstick_lr_enabled();
}


u32 is_holding_greater_than_2E_up_on_stick(void)
{
    return (joyGetStickY(PLAYER_1) < 0x2e) ^ 1;
}


u32 is_holding_greater_than_2E_down_on_stick(void)
{
    return (joyGetStickY(PLAYER_1) < -0x2d);
}


u32 get_watch_stick_y_nav_ready(void)
{
    return watch_stick_y_nav_ready;
}


void disable_watch_stick_y_nav_ready(void)
{
    watch_stick_y_nav_ready = 0;
}


s32 sub_GAME_7F0A5088(void)
{
    return is_holding_greater_than_2E_up_on_stick() && get_watch_stick_y_nav_ready();
}


s32 sub_GAME_7F0A50C4(void)
{
    return is_holding_greater_than_2E_down_on_stick() && get_watch_stick_y_nav_ready();
}


u32 is_holding_less_than_10_up_on_stick(void)
{
    return (joyGetStickY(PLAYER_1) < 0x10) ^ 1;
}


u32 is_holding_less_than_10_down_on_stick(void)
{
    return (joyGetStickY(PLAYER_1) < -0xf);
}


u32 watch_stick_y_was_active(void)
{
    return watch_stick_y_prev_active;
}


void reset_watch_stick_y_latch(void) 
{
    watch_stick_y_prev_active = 0;
}


s32 watch_stick_y_pressed_up(void)
{
    return is_holding_less_than_10_up_on_stick() && !watch_stick_y_was_active();
}


s32 watch_stick_y_pressed_down(void)
{
    return is_holding_less_than_10_down_on_stick() && !watch_stick_y_was_active();
}


void sub_GAME_7F0A51D8(void)
{
    g_WatchBackgroundGreen = 0x80;
    sndPlaySfx(g_musicSfxBufferPtr, WATCH_STATIC_SFX, NULL);
    return;
}


void sub_GAME_7F0A5210(void)
{
    set_controlstick_lr_disabled();
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, NULL);
    if ((D_80040B10 << 0x10) < randomGetNext()) {
        sub_GAME_7F0A51D8();
    }
    return;
}


// initial pause screen: WATCH_INDEX_MISSION_STATUS
void watch_screen0_navigation(void)
{
    s32 goto_watch_screen_index_4;
    s32 goto_watch_screen_index_1;

    if (watch_item_is_actively_selected == 0)
    {
        goto_watch_screen_index_4 = FALSE;
        goto_watch_screen_index_1 = FALSE;

        if (get_debug_gunwatchpos_flag() == 0)
        {
            if (joyGetButtonsPressedThisFrame(PLAYER_1, L_TRIG|L_CBUTTONS))
            {
                goto_watch_screen_index_4 = TRUE;
            }
            if (joyGetButtonsPressedThisFrame(PLAYER_1, R_TRIG|R_CBUTTONS))
            {
                goto_watch_screen_index_1 = TRUE;
            }
        }


        if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD)) || (sub_GAME_7F0A4FB0()))
        {
            goto_watch_screen_index_4 = TRUE;
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD)) || (sub_GAME_7F0A4FEC()))
        {
            goto_watch_screen_index_1 = TRUE;
        }

        if (goto_watch_screen_index_4)
        {
#ifndef __sgi
            /* NATIVE (#44): LEFT from the first page wraps onto the LAST page
             * of the ring, which is now SIGHTLINE (options.h). The original
             * arm - straight to the briefing - is the #else, verbatim. */
            watch_screen_index = WATCH_INDEX_SL_SIGHTLINE;
            sl_reset_sightline_row_index();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM3, 15.0f);
#else
            watch_screen_index = WATCH_INDEX_MISSION_BRIEFING;
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
#endif
        }

        if (goto_watch_screen_index_1)
        {
            watch_screen_index = WATCH_INDEX_INVENTORY;
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
            return;
        }
    }
    else if ((D_800409A4) && (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON)))
    {
        D_800409A4 = 0;
        set_missionstate(MISSION_STATE_0);
        bossRunTitleStage();
        mission_failed_or_aborted = TRUE;
        deleteCurrentSelectedFolder();
    }
}


// pause screen: WATCH_INDEX_INVENTORY
void watch_screen1_navigation(void)
{
    s32 goto_watch_screen_index_0;
    s32 goto_watch_screen_index_2;

    if (watch_item_is_actively_selected == 0)
    {
        goto_watch_screen_index_0 = FALSE;
        goto_watch_screen_index_2 = FALSE;

        if (get_debug_gunwatchpos_flag() == FALSE)
        {
            if (joyGetButtonsPressedThisFrame(PLAYER_1, L_TRIG|L_CBUTTONS))
            {
                goto_watch_screen_index_0 = TRUE;
            }
            if (joyGetButtonsPressedThisFrame(PLAYER_1, R_TRIG|R_CBUTTONS))
            {
                goto_watch_screen_index_2 = TRUE;
            }
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_JPAD)) || (sub_GAME_7F0A4FB0()))
        {
            goto_watch_screen_index_0 = TRUE;
        }

        if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_JPAD)) || (sub_GAME_7F0A4FEC()))
        {
            goto_watch_screen_index_2 = TRUE;
        }

        if (goto_watch_screen_index_0)
        {
            watch_screen_index = WATCH_INDEX_MISSION_STATUS;
            zero_D_800409A4();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM2, 15.0f);

        }

        if (goto_watch_screen_index_2)
        {
            watch_screen_index = WATCH_INDEX_CONTROL_OPTIONS;
            set_controlstick_lr_disabled();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM3, 15.0f);
        }
    }
}


void unused_watch_screen_navigation(void) {

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            watch_screen_index = WATCH_INDEX_INVENTORY;
            set_controlstick_lr_disabled();
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            watch_screen_index = WATCH_INDEX_CONTROL_OPTIONS;
            reset_controller_options_index();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM3, 15.0f);
        }
    }
}


// WATCH_INDEX_CONTROL_OPTIONS
void watch_screen2_navigation(void) {

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_INVENTORY;
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_GAME_OPTIONS;
            reset_game_options_index();
            set_controlstick_lr_disabled();
        }
    }
}


// WATCH_INDEX_GAME_OPTIONS
void watch_screen3_navigation(void) {

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_CONTROL_OPTIONS;
            reset_controller_options_index();
            set_controlstick_lr_disabled();
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_MISSION_BRIEFING;
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
        }
    }
}


// WATCH_INDEX_MISSION_BRIEFING
void watch_screen4_navigation(void) {

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            watch_screen_index = WATCH_INDEX_GAME_OPTIONS;
            reset_game_options_index();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM3, 15.0f);
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if (watch_item_is_actively_selected == 0)
        {
#ifndef __sgi
            /* NATIVE (#44): RIGHT from the briefing reaches SIGHTLINE, the
             * page appended after it; the original arm (back to the first
             * page) is the #else, verbatim, and now lives in
             * sl_watch_sightline_navigation's RIGHT. */
            watch_screen_index = WATCH_INDEX_SL_SIGHTLINE;
            sl_reset_sightline_row_index();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM3, 15.0f);
#else
            watch_screen_index = WATCH_INDEX_MISSION_STATUS;
            zero_D_800409A4();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM2, 15.0f);
#endif
        }
    }
}

#ifndef __sgi
/* NATIVE (#44): the SIGHTLINE page's navigation - the same shape as
 * watch_screen4_navigation (page change on L/R, only while nothing is
 * latched) plus the row step of sub_GAME_7F0A5998 (up/down, the stick's
 * y latch, wrapping, the select latch dropped on a row change). LEFT returns
 * to the briefing (its zoom, WATCHZOOM1); RIGHT wraps onto the first page
 * exactly as the briefing's RIGHT did (D_800409A4 cleared, WATCHZOOM2). */
u32 sl_sightline_row_index = 0;

/* The page's nested BINDINGS child (#46, src/native/sl_watch_bindings.c):
 * while it is open it owns the page's navigation and its drawing, and the
 * ring's L/R do nothing - its BACK returns here (to the CONTROLS view). */
extern s32  sl_watch_bindings_is_open(void);
extern void sl_watch_bindings_open(void);
extern void sl_watch_bindings_close(void);
extern void sl_watch_bindings_navigation(void);
extern Gfx *sl_watch_bindings_draw(Gfx *gdl);
extern char *getenv(const char *);
extern int fprintf(void *, const char *, ...);
extern void *stderr;

/* THE VIEWS (options.h): a heading and a row list each. The SIGHTLINE page
 * itself is view 0; a SUBMENU row opens the child it names; every child ends
 * in BACK. GRAPHICS was DIMMED (visible, unselectable - the CHEATS-row
 * convention) while nothing shipped there; #43's WORLD DETAIL row opened it
 * on 2026-09-21 (#47 / #48 add their rows to the same child when they come). */
struct sl_watch_row  { const char *label; u8 kind; u8 arg; };
struct sl_watch_view { const char *heading; struct sl_watch_row rows[SL_WVIEW_ROWS_MAX]; u8 count; };

static const struct sl_watch_view sl_watch_views[SL_WVIEW_COUNT] = {
    { "sightline\n", { { "graphics\n", SL_WROW_SUBMENU, SL_WVIEW_GRAPHICS },
                       { "gameplay\n", SL_WROW_SUBMENU, SL_WVIEW_GAMEPLAY },
                       { "display\n",  SL_WROW_SUBMENU, SL_WVIEW_DISPLAY },
                       { "controls\n", SL_WROW_SUBMENU, SL_WVIEW_CONTROLS } }, 4 },
    { "gameplay\n",  { { "sprint\n",         SL_WROW_TOGGLE_SPRINT, 0 },
                       { "sprint mode\n",    SL_WROW_TOGGLE_SMODE, 0 },    /* #56: hold / toggle */
                       { "crouch mode\n",    SL_WROW_TOGGLE_CMODE, 0 },    /* #56 */
                       { "back\n",           SL_WROW_BACK, 0 } }, 4 },
    /* #52 (2026-09-20): the three output rows before the two content rows,
     * as the front end's DISPLAY tab - six rows with one bar, well inside
     * the face (CONTROLS holds nine with two). */
    { "display\n",   { { "window mode\n",    SL_WROW_VALUE_WMODE, 0 },     /* #52: windowed / borderless / fullscreen */
                       { "resolution\n",     SL_WROW_VALUE_RES, 0 },       /* #52: the list; informational in borderless */
                       { "vsync\n",          SL_WROW_TOGGLE_VSYNC, 0 },    /* #52: off / on */
                       { "aspect ratio\n",   SL_WROW_VALUE_ASPECT, 0 },
                       { "field of view\n",  SL_WROW_VALUE_FOV, 0 },
                       { "back\n",           SL_WROW_BACK, 0 } }, 6 },
    { "controls\n",  { { "button layout\n",  SL_WROW_VALUE_BLAYOUT, 0 },   /* #63: the pad's preset */
                       { "stick layout\n",   SL_WROW_VALUE_SLAYOUT, 0 },   /* #63: which thumb looks */
                       { "stick tuning\n",   SL_WROW_SUBMENU, SL_WVIEW_STICK }, /* #51: the second-level child */
                       { "controller\n",     SL_WROW_INFO_PAD, 0 },        /* #63: the attached family */
                       { "invert mouse y\n", SL_WROW_TOGGLE_MINV, 0 },
                       { "mouse sensitivity\n",  SL_WROW_VALUE_MSENS, 0 },   /* #50 */
                       { "scoped sensitivity\n", SL_WROW_VALUE_SSENS, 0 },   /* #50 */
                       { "bindings\n",       SL_WROW_BINDINGS, 0 },
                       { "back\n",           SL_WROW_BACK, 0 } }, 9 },
    /* #51 (2026-09-20): the controller's three sliders. A child of CONTROLS
     * rather than three more rows on it: CONTROLS already reaches the face's
     * foot with its two bars (nine rows, BACK at y 179 - the bindings
     * child's own limit), and two more bars would put BACK on the page bar.
     * BACK here returns to CONTROLS on the row that opened this. */
    { "stick tuning\n", { { "look sensitivity\n", SL_WROW_VALUE_PSENS, 0 },
                          { "look deadzone\n",    SL_WROW_VALUE_PDZ, 0 },
                          { "move deadzone\n",    SL_WROW_VALUE_MDZ, 0 },
                          { "back\n",             SL_WROW_BACK, 0 } }, 4 },
    /* #43 (2026-09-21): the render-visibility profile, the front end's
     * DISPLAY tab's WORLD DETAIL row - here under GRAPHICS, the child the
     * owner's structure reserved for it. */
    { "graphics\n",  { { "world detail\n",   SL_WROW_VALUE_WDETAIL, 0 },   /* #43: original / enhanced */
                       { "textures\n",       SL_WROW_VALUE_TEXTURES, 0 },  /* #47: original / community hd / xbla */
                       { "back\n",           SL_WROW_BACK, 0 } }, 3 },
};

static s32 sl_wview;              /* the view in force (SL_WVIEW_*) */
/* THE BACK STACK (#51): the (view, row) to return to at each level, so a
 * child of a child (STICK TUNING under CONTROLS) returns to CONTROLS on the
 * row that opened it, and CONTROLS to SIGHTLINE on its row - one level per
 * BACK / Escape, as the header promises. Depth 0 is the SIGHTLINE page. */
static s32 sl_wview_stack_view[SL_WVIEW_DEPTH_MAX];
static s32 sl_wview_stack_row[SL_WVIEW_DEPTH_MAX];
static s32 sl_wview_depth;
static s32 sl_wview_back_req;     /* Escape asked for one level up */

s32 sl_sightline_view(void)          { return sl_wview; }
s32 sl_sightline_row_count(void)     { return sl_watch_views[sl_wview].count; }
s32 sl_sightline_child_open(void)    { return sl_wview != SL_WVIEW_SIGHTLINE; }
s32 sl_sightline_row_kind(s32 row)
{
    s32 kind;
    if (row < 0 || row >= sl_watch_views[sl_wview].count) return SL_WROW_DIMMED;
    kind = sl_watch_views[sl_wview].rows[row].kind;
    /* #52: RESOLUTION is a control only while the size is the player's to
     * choose; in BORDERLESS (the desktop owns it) the row reports itself
     * informational, so the cursor, the pointer and the draw all follow the
     * one kind. */
    if (kind == SL_WROW_VALUE_RES && !sl_window_size_editable())
        return SL_WROW_INFO_RES;
    return kind;
}
const char *sl_sightline_row_label(s32 row)
{
    if (row < 0 || row >= sl_watch_views[sl_wview].count) return "\n";
    return sl_watch_views[sl_wview].rows[row].label;
}

static void sl_sightline_witness(const char *what)
{
    if (getenv("SL_INPUT_DEBUG") != NULL)
    {
        const char *h = sl_watch_views[sl_wview].heading;
        s32 n = 0;
        while (h[n] != '\0' && h[n] != '\n') n++;
        fprintf(stderr, "sightline watch: %s -> view=%d(%.*s) row=%u\n",
                what, (int) sl_wview, (int) n, h, sl_sightline_row_index);
    }
}

static void sl_sightline_open_view(s32 view)
{
    if (view <= SL_WVIEW_SIGHTLINE || view >= SL_WVIEW_COUNT) return;
    if (sl_wview_depth >= SL_WVIEW_DEPTH_MAX) return;    /* no deeper child exists */
    sl_wview_stack_view[sl_wview_depth] = sl_wview;
    sl_wview_stack_row[sl_wview_depth] = (s32) sl_sightline_row_index;
    sl_wview_depth++;
    sl_wview = view;
    sl_sightline_row_index = 0;
    disable_watch_stick_y_nav_ready();
    reset_watch_item_is_actively_selected();       /* the child starts unlatched (watch_play_beep_sound would latch) */
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
    sl_sightline_witness("open");
}

void sl_sightline_back(void)
{
    if (sl_wview == SL_WVIEW_SIGHTLINE || sl_wview_depth <= 0) return;
    sl_wview_depth--;
    sl_wview = sl_wview_stack_view[sl_wview_depth];
    sl_sightline_row_index = (u32) sl_wview_stack_row[sl_wview_depth];
    disable_watch_stick_y_nav_ready();
    reset_watch_item_is_actively_selected();
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
    sl_sightline_witness("back");
}

void sl_sightline_request_back(void)
{
    sl_wview_back_req = 1;
}

void sl_reset_sightline_row_index(void)
{
    sl_sightline_row_index = 0;
    sl_wview = SL_WVIEW_SIGHTLINE;
    sl_wview_depth = 0;
    sl_wview_back_req = 0;
    sl_watch_bindings_close();
}

/* The row step of sub_GAME_7F0A5998 over the view in force, skipping a
 * DIMMED row (the cursor falls past it, as the front end's disabled rows). */
static void sl_sightline_step_row(s32 dir)
{
    s32 count = sl_sightline_row_count();
    s32 tries = count;
    do {
        s32 r = (s32) sl_sightline_row_index + dir;
        if (r >= count) r = 0;
        if (r < 0) r = count - 1;
        sl_sightline_row_index = (u32) r;
    } while (SL_WROW_IS_INERT(sl_sightline_row_kind((s32) sl_sightline_row_index)) && --tries > 0);
    disable_watch_stick_y_nav_ready();
    reset_watch_item_is_actively_selected();
    sl_sightline_witness(dir < 0 ? "up" : "down");
}

void sl_watch_sightline_navigation(void)
{
    if (sl_watch_bindings_is_open())
    {
        sl_watch_bindings_navigation();
        return;
    }
    if (sl_wview_back_req)
    {
        sl_wview_back_req = 0;
        sl_sightline_back();
        return;
    }

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
        sl_sightline_step_row(-1);
    else if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
        sl_sightline_step_row(+1);
    if (SL_WROW_IS_INERT(sl_sightline_row_kind((s32) sl_sightline_row_index)))
        sl_sightline_step_row(+1);                /* never rest on GRAPHICS or an info row */

    /* Inside a child the ring's L/R belong to the latched row; only the
     * SIGHTLINE page itself steps the ring. */
    if (sl_wview != SL_WVIEW_SIGHTLINE)
        return;

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) || (sub_GAME_7F0A4FB0()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_MISSION_BRIEFING;
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM1, 15.0f);
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) || (sub_GAME_7F0A4FEC()))
    {
        if ((joyGetButtons(PLAYER_1, Z_TRIG) == 0) && (watch_item_is_actively_selected == 0))
        {
            watch_screen_index = WATCH_INDEX_MISSION_STATUS;
            zero_D_800409A4();
            sub_GAME_7F0A5210();
            trigger_watch_zoom(WATCHZOOM2, 15.0f);
        }
    }
}
#endif

void controller_options_controlstyle_navigation(void)
{
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            controller_options_index = CONTROLLER_OPTIONS_INDEX_INPUTS;
            disable_watch_stick_y_nav_ready();
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            controller_options_index = CONTROLLER_OPTIONS_INDEX_INPUTS;
            disable_watch_stick_y_nav_ready();
        }
    }
}


void controller_options_inputs_navigation(void)
{
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
            disable_watch_stick_y_nav_ready();
            return;
        }
    }
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        if (watch_item_is_actively_selected == 0)
        {
            controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
            disable_watch_stick_y_nav_ready();
        }
    }
}


void sub_GAME_7F0A5998(void)
{
    s32 aux;

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        game_options_index = game_options_index - 1;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
    else if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        game_options_index = game_options_index + 1;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }

    aux = game_options_index;

    if (aux >= 10)
    {
        game_options_index = GAME_OPTIONS_INDEX_MUSIC;
        return;
    }

    if (aux < 0)
    {
        game_options_index = GAME_OPTIONS_INDEX_RATIO;
    }
}


void game_options_music_volume_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        game_options_index = GAME_OPTIONS_INDEX_RATIO;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        game_options_index = GAME_OPTIONS_INDEX_FX;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void game_options_fx_volume_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        game_options_index = GAME_OPTIONS_INDEX_MUSIC;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        game_options_index = GAME_OPTIONS_INDEX_LOOK_UPDOWN;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void game_options_inventory_navigation(void)
{
    s32 count;
    s32 item_line_height;
    s32 selected_item_line_height;

    count = bondinvCountTotalItemsInInv();

    if (!get_debug_gunwatchpos_flag())
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD | U_CBUTTONS) || joyGetStickY(PLAYER_1) >= 0x47)
        {
            if (((s32) watch_inventory_cursor_pos > 0) && !watch_item_is_actively_selected)
            {
                watch_inventory_cursor_pos -= 1.0f;
            }
        }
        else
        {
            if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD | D_CBUTTONS) || joyGetStickY(PLAYER_1) < -0x46)
            {
                goto down_body;
            }

            goto after_updown;

down_body:
            if (((s32) watch_inventory_cursor_pos < count - 1) && !watch_item_is_actively_selected)
            {
                watch_inventory_cursor_pos += 1.0f;
            }

after_updown:
            ;
        }

        if (joyGetButtons(PLAYER_1, U_JPAD | U_CBUTTONS))
        {
            if (((s32) watch_inventory_cursor_pos > 0) && !watch_item_is_actively_selected)
            {
                watch_inventory_cursor_pos -= 0.1f;
            }
        }
        else if (joyGetButtons(PLAYER_1, D_JPAD | D_CBUTTONS))
        {
            if (((s32) watch_inventory_cursor_pos < count - 1) && !watch_item_is_actively_selected)
            {
                watch_inventory_cursor_pos += 0.1f;
            }
        }
    }

    if (joyGetStickY(PLAYER_1) >= 0x1f
        && joyGetStickY(PLAYER_1) < 0x46
        && g_curWatchItemIndex > 0
        && !watch_item_is_actively_selected)
    {
        watch_inventory_cursor_pos -= (f32) joyGetStickY(PLAYER_1) / 300.0f;
    }
    else if (joyGetStickY(PLAYER_1) < -0x1e
        && joyGetStickY(PLAYER_1) >= -0x45
        && (s32) watch_inventory_cursor_pos < count - 1
        && !watch_item_is_actively_selected)
    {
        watch_inventory_cursor_pos -= (f32) joyGetStickY(PLAYER_1) / 300.0f;
    }

    if (watch_stick_y_pressed_up() && g_curWatchItemIndex > 0 && !watch_item_is_actively_selected)
    {
        watch_inventory_cursor_pos -= 1.0f;
    }
    else if (watch_stick_y_pressed_down() && g_curWatchItemIndex < count - 1 && !watch_item_is_actively_selected)
    {
        watch_inventory_cursor_pos += 1.0f;
    }

    if (is_holding_less_than_10_up_on_stick() || is_holding_less_than_10_down_on_stick())
    {
        watch_stick_y_prev_active = 1;
    }
    else
    {
        watch_stick_y_prev_active = 0;
    }

    if ((f32) count - 0.5f < watch_inventory_cursor_pos)
    {
        watch_inventory_cursor_pos = (f32) count - 0.5f;
    }

    if (watch_inventory_cursor_pos < -0.5f)
    {
        watch_inventory_cursor_pos = -0.5f;
    }

    // The current item is determined by the integer part of the cursor's position.
    g_curWatchItemIndex = (s32) watch_inventory_cursor_pos;

    if (j_text_trigger)
    {
        item_line_height = 14;
    }
    else
    {
        item_line_height = 12;
    }

    selected_item_line_height = j_text_trigger ? 14 : 12;
    watch_inventory_text_target_y = (2 * selected_item_line_height) + (-g_curWatchItemIndex * item_line_height);

    if (watch_inventory_text_target_y < watch_inventory_text_y)
    {
        watch_inventory_text_y = (watch_inventory_text_y - ((watch_inventory_text_y - watch_inventory_text_target_y) / 3)) - 1;
        watch_inventory_text_is_settled = FALSE;
    }
    else if (watch_inventory_text_y < watch_inventory_text_target_y)
    {
        watch_inventory_text_y = (watch_inventory_text_y + ((watch_inventory_text_target_y - watch_inventory_text_y) / 3)) + ((0, 1));
        watch_inventory_text_is_settled = FALSE;
    }
    else
    {
        watch_inventory_text_is_settled = TRUE;
    }

    if (((f32) g_curWatchItemIndex + 0.55f < watch_inventory_cursor_pos)
        && !joyGetButtons(PLAYER_1, 0xffff)) // Any button
    {
        watch_inventory_cursor_pos -= 0.1f;
    }
    else if (watch_inventory_cursor_pos <= (f32) g_curWatchItemIndex + 0.45f
        && !joyGetButtons(PLAYER_1, 0xffff)) // Any button
    {
        watch_inventory_cursor_pos += 0.1f;
    }
}


void sub_GAME_7F0A611C(f32 *arg0, s32 *arg1, s32 arg2, s32 *arg3, s32 *arg4, s32 *arg5, s32 arg6, s32 arg7, s32 arg8)
{
    if (!get_debug_gunwatchpos_flag())
    {
        if (joyGetButtonsPressedThisFrame(PLAYER_1, U_JPAD | U_CBUTTONS) || joyGetStickY(PLAYER_1) >= 0x47)
        {
            if ((s32)*arg0 > 0 && arg7)
            {
                *arg0 -= 1.0f;
            }
        }
        else if (joyGetButtonsPressedThisFrame(PLAYER_1, D_JPAD | D_CBUTTONS) || joyGetStickY(PLAYER_1) < -0x46)
        {
            if ((s32)*arg0 < arg2 - 1 && arg7)
            {
                *arg0 += 1.0f;
            }
        }

        if (joyGetButtons(PLAYER_1, U_JPAD | U_CBUTTONS))
        {
            if ((s32)*arg0 > 0 && arg7)
            {
                *arg0 -= 0.1f;
            }
        }
        else if (joyGetButtons(PLAYER_1, D_JPAD | D_CBUTTONS))
        {
            if ((s32)*arg0 < arg2 - 1 && arg7)
            {
                *arg0 += 0.1f;
            }
        }
    }

    if (joyGetStickY(PLAYER_1) >= 0x1f && joyGetStickY(PLAYER_1) < 0x46 && *arg1 > 0 && arg7)
    {
        *arg0 -= (f32)joyGetStickY(PLAYER_1) / 300.0f;
    }
    else if (joyGetStickY(PLAYER_1) < -0x1e && joyGetStickY(PLAYER_1) >= -0x45 && (s32)*arg0 < arg2 - 1 && arg7)
    {
        *arg0 -= (f32)joyGetStickY(PLAYER_1) / 300.0f;
    }

    if (watch_stick_y_pressed_up() && *arg1 > 0 && arg7)
    {
        *arg0 -= 1.0f;
    }
    else if (watch_stick_y_pressed_down() && *arg1 < arg2 - 1 && arg7)
    {
        *arg0 += 1.0f;
    }

    if (is_holding_less_than_10_up_on_stick() || is_holding_less_than_10_down_on_stick())
    {
        watch_stick_y_prev_active = 1;
    }
    else
    {
        watch_stick_y_prev_active = 0;
    }

    if ((f32)arg2 - 0.5f < *arg0)
    {
        *arg0 = (f32)arg2 - 0.5f;
    }

    if (*arg0 < -0.5f)
    {
        *arg0 = -0.5f;
    }

    *arg1 = (s32)*arg0;
    *arg4 = (arg6 * arg8) + (-*arg1 * arg8);

    if (*arg4 < *arg3)
    {
        *arg3 = (*arg3 - ((*arg3 - *arg4) / 3)) - 1;
        *arg5 = 0;
    }
    else if (*arg3 < *arg4)
    {
        *arg3 = (((*arg4 - *arg3) / 3) + *arg3) + 1;
        *arg5 = 0;
    }
    else
    {
        *arg5 = 1;
    }

    if ((f32)*arg1 + 0.55f < *arg0)
    {
        if (!joyGetButtons(PLAYER_1, ANY_BUTTON))
        {
            *arg0 -= 0.1f;
            return;
        }
    }

    if (*arg0 <= (f32)*arg1 + 0.45f)
    {
        if (!joyGetButtons(PLAYER_1, ANY_BUTTON))
        {
            *arg0 += 0.1f;
        }
    }
}


void mission_brief_background_navigation(void)
{
    if ((joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)) || (sub_GAME_7F0A5088()))
    {
        mission_brief_index = BRIEF_INDEX_OBJECTIVES;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }

    if ((joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD)) || (sub_GAME_7F0A50C4()))
    {
        mission_brief_index = BRIEF_INDEX_M;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void mission_brief_m_briefing_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        mission_brief_index = BRIEF_INDEX_BACKGROUND;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        mission_brief_index = BRIEF_INDEX_Q;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void mission_brief_q_branch_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        mission_brief_index = BRIEF_INDEX_M;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        mission_brief_index = BRIEF_INDEX_MONEYPENNY;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}

void mission_brief_moneypenny_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        mission_brief_index = BRIEF_INDEX_Q;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        mission_brief_index = BRIEF_INDEX_OBJECTIVES;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}

void mission_brief_objectives_navigation(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) || sub_GAME_7F0A5088())
    {
        mission_brief_index = BRIEF_INDEX_MONEYPENNY;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
        return;
    }

    if (joyGetButtonsPressedThisFrame(PLAYER_1, D_CBUTTONS|D_JPAD) || sub_GAME_7F0A50C4())
    {
        mission_brief_index = BRIEF_INDEX_BACKGROUND;
        disable_watch_stick_y_nav_ready();
        reset_watch_item_is_actively_selected();
    }
}


void build_watch_static_scanline_vertices(Vtx *vertices)
{
    s32 halfWidth; // Half width for the scanline
    Vtx *vertex;
    s32 zoffs;
    s32 greenChannelIndex;
    s32 side;

    // Fit the width of the scanline to the watch's green circle
    halfWidth = sqrtf(213444.0f - ((f32) (g_WatchStaticScanlineY * g_WatchStaticScanlineY)));

    for (zoffs = 0; zoffs != 8; zoffs += 4)
    {
        for (side = -1; side != 3; side += 2)
        {
            vertex = vertices;

            vertex->v.ob[0] = halfWidth * side;
            vertex->v.ob[1] = 0;
            vertex->v.ob[2] = zoffs + g_WatchStaticScanlineY;

            vertex->v.cn[1] = 0xA0;
            vertex->v.flag = 0;
            vertex->v.tc[0] = 0;
            vertex->v.tc[1] = 0;
            vertex->v.cn[0] = 0;

            greenChannelIndex = 1;
            vertex->v.cn[greenChannelIndex] = 0xA0;
            vertex->v.cn[2] = 0;
            vertex->v.cn[3] = g_WatchStaticScanlineAlpha;

            vertices++;
        }
    }
}


void sub_GAME_7F0A69A8(void)
{
    if (joyGetControllerCount() < 2)
    {
        D_800409D8 = 4;
    }
    else
    {
        D_800409D8 = 8;
    }
    reset_watch_item_is_actively_selected();
    watch_screen_index = WATCH_INDEX_MISSION_STATUS;
    mission_brief_index = BRIEF_INDEX_OBJECTIVES;
    D_800409C8 = 0.999f;
    D_800409CC = 0.9999f;
    bondinvDetermineEquippedItem();
}


/**
 * Address 0x7F0A6A2C. (VERSION_US, VERSION_JP)
 * Address 0x7F0A5D78. (VERSION_EU)
*/
f32 watchWrapAroundPI(f32 arg0)
{
    if (arg0 > M_PI_F)
    {
        arg0 = arg0 - M_TAU_F;
    }
    else if (arg0 < M_MINUS_PI_F)
    {
        arg0 = arg0 + M_TAU_F;
    }
    return arg0;
}


extern f32 jpD_800484D0;
void sub_GAME_7F0A6A80(void)
{
    u32 temp_1;
    s32 temp_2;
    s32 temp_3;
    u32 random_value;

    if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON))
    {
        set_open_close_solo_watch_menu_to1();
    }

    if (controlstick_lr_enabled == 0)
    {
        if ((joyGetStickX(PLAYER_1) >= -0xA) && (joyGetStickX(PLAYER_1) < 0xB))
        {
            controlstick_lr_enabled = 1;
        }
        else if ((joyGetStickX(PLAYER_1) < 0xB) && (joy7000C174(PLAYER_1) >= 0xB))
        {
            controlstick_lr_enabled = 1;
        }
        else if ((joyGetStickX(PLAYER_1) >= -0xA) && (joy7000C174(PLAYER_1) < -0xA))
        {
            controlstick_lr_enabled = 1;
        }
    }

    if (watch_stick_y_nav_ready == 0)
    {
        if ((joyGetStickY(PLAYER_1) >= -0xA) && (joyGetStickY(PLAYER_1) < 0xB))
        {
            watch_stick_y_nav_ready = 1;
        }
        else if ((joyGetStickY(PLAYER_1) < 0xB) && (joy7000C284(PLAYER_1) >= 0xB))
        {
            watch_stick_y_nav_ready = 1;
        }
        else if ((joyGetStickY(PLAYER_1) >= -0xA) && (joy7000C284(PLAYER_1) < -0xA))
        {
            watch_stick_y_nav_ready = 1;
        }
    }

    temp_2 = D_80040AF8;
    if (temp_2 < 0)
    {
        D_80040AF4 = D_80040AF4 + 0xFFF00000;
    }
    D_80040AF8 = temp_2 - 1;

    if (D_80040AF4 < 0x5F00A1U)
    {
        D_80040AF4 = 0xFF00A0U;
        D_80040AF8 = 0xF;
    }

    temp_3 = D_80040B00;
    if (temp_3 < 0)
    {
        D_80040AFC = D_80040AFC - 0x10;
    }
    D_80040B00 = temp_3 - 1;

    if (D_80040AFC < 0x60U)
    {
        D_80040AFC = 0xFFU;
        D_80040B00 = 0xF;
    }
    #ifdef VERSION_US
    D_80040B14 += ((D_80040B1C * speedgraphframes * M_TAU_F) / 360.0f);
    #else
    D_80040B14 += ((D_80040B1C * jpD_800484D0 * M_TAU_F) / 360.0f);
    #endif

    D_80040B14 = watchWrapAroundPI(D_80040B14);

    temp_1 = D_80040B0C << 0x10;
    if (temp_1 < randomGetNext())
    {
        sub_GAME_7F0A51D8();
    }

    if (g_WatchBackgroundGreen < 0xE0)
    {
        random_value = randomGetNext();
        g_WatchBackgroundGreen += (random_value >> 0x1E);
    }

    if (g_WatchBackgroundGreen > 0xe0) {
        g_WatchBackgroundGreen = 0xe0;
    }

    g_WatchStaticScanlineAlpha = ((-g_WatchBackgroundGreen * 4) + 0x380);
    g_WatchStaticScanlineY = g_WatchStaticScanlineY - 4;

    if (g_WatchStaticScanlineY >= 0x157) {
        g_WatchStaticScanlineY = -0x156;
    }

    if (g_WatchStaticScanlineY < -0x156) {
        g_WatchStaticScanlineY = 0x156;
    }

    D_80040B44 = (s16)D_80040B44 + 1;
    D_80040B44 = (s16)D_80040B44 & 1;

    switch (watch_screen_index)
    {
        case WATCH_INDEX_MISSION_STATUS:
            watch_screen0_navigation();
            break;

        case WATCH_INDEX_CONTROL_OPTIONS:
            switch (controller_options_index)
            {
                case CONTROLLER_OPTIONS_INDEX_STYLE:
                    controller_options_controlstyle_navigation();
                    break;

                case CONTROLLER_OPTIONS_INDEX_INPUTS:
                    controller_options_inputs_navigation();
            }
            watch_screen2_navigation();
            break;

        case WATCH_INDEX_GAME_OPTIONS:
            switch (game_options_index)
            {
                case GAME_OPTIONS_INDEX_MUSIC:
                    game_options_music_volume_navigation();
                    break;

                case GAME_OPTIONS_INDEX_FX:
                    game_options_fx_volume_navigation();
                    break;

                case GAME_OPTIONS_INDEX_LOOK_UPDOWN:
                case GAME_OPTIONS_INDEX_AUTO_AIM:
                case GAME_OPTIONS_INDEX_AIM_CONTROL:
                case GAME_OPTIONS_INDEX_SIGHT_ONSCREEN:
                case GAME_OPTIONS_INDEX_LOOK_AHEAD:
                case GAME_OPTIONS_INDEX_AMMO_ONSCREEN:
                case GAME_OPTIONS_INDEX_SCREEN_SIZE:
                case GAME_OPTIONS_INDEX_RATIO:
                    sub_GAME_7F0A5998();
            }
            watch_screen3_navigation();
            break;

        case WATCH_INDEX_MISSION_BRIEFING:
            watch_screen4_navigation();
            break;

        case WATCH_INDEX_INVENTORY:
            watch_screen1_navigation();
#ifndef __sgi
            break;

        case WATCH_INDEX_SL_SIGHTLINE:               /* #44, native page */
            sl_watch_sightline_navigation();
#endif
    }
}



Gfx *sub_GAME_7F0A6EE8(Gfx *DL)
{
    gSPSetGeometryMode(DL++, G_CULL_BACK);
    gDPSetCycleType(DL++, G_CYC_1CYCLE);
    gDPPipelineMode(DL++, G_PM_1PRIMITIVE);
    gDPSetScissor(DL++, G_SC_NON_INTERLACE, 0, 0,viGetX(), viGetY() );
    gDPSetTextureLOD(DL++, G_TL_TILE);
    gDPSetTextureLUT(DL++, G_TT_NONE);
    gDPSetTextureDetail(DL++, G_TD_CLAMP);
    gDPSetTexturePersp(DL++, G_TP_PERSP);
    gDPSetTextureFilter(DL++, G_TF_BILERP);
    gDPSetTextureConvert(DL++, G_TC_FILT);
    gDPSetCombineLERP(DL++, 0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE,  0, 0, 0, SHADE);
    gDPSetCombineKey(DL++, G_CK_NONE);
    gDPSetAlphaCompare(DL++, G_AC_NONE);
    gDPSetRenderMode(DL++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetColorDither(DL++, G_CD_MAGICSQ);

    return DL;
}


void set_page_rectangle_colors(s32 watch_screen_index, struct WatchVertex *vertices)
{
    s32 i;

    // Unselected rectangles.
    for (i = 0; i < 20; i++)
    {
        vertices[i].color.r = 0x20;
        vertices[i].color.g = 0x70;
        vertices[i].color.b = 0x20;
    }

    // Currently selected page rectangle.
    for (i = watch_screen_index * 4; i <= watch_screen_index * 4 + 3; i++)
    {
        vertices[i].color.r = 0x50;
        vertices[i].color.g = 0xF0;
        vertices[i].color.b = 0x50;

        // Currently selected page rectangle, but something else is in focus e.g. toggling options or manipulating the controller on the controller screen.
        if (watch_item_is_actively_selected)
        {
            vertices[i].color.r = 0x30;
            vertices[i].color.g = 0xA0;
            vertices[i].color.b = 0x30;
        }
    }
}


/**
 * @param gdl:
 * @param arg1: Something about watch view matrix.
 * @param zoom_squish: When set, will "unfold" the interior watch watch area (green + bars)
 * based on view distance during pause animation. This is used when starting to pause and bring
 * the watch up, and exiting pause menu to resume game play. If this is disabled then
 * the interior area will always be the same size as the watch container.
*/
Gfx *draw_background_health_and_armor(Gfx *gdl, Mtx *arg1, s32 zoom_squish)
{
    int i;
    struct WatchVertex *sp48;
    struct WatchVertex *sp44;
    Gfx *sp40;
    Gfx *sp3C;

    s32 stack_pad[6];

    f32 scale;

    sp48 = dynAllocateVertices(WATCH_BACKGROUND_VERTEX_COUNT);
    sp44 = dynAllocateVertices(WATCH_BACKGROUND_VERTEX_COUNT);
    sp40 = dynAllocate(0xF8);
    sp3C = dynAllocate(0xF8);

    /**
     * It seems at this point in rendering the background watch arms (hour,minute,second)
     * have already been drawn. Whatever is about to be drawn next is oriented
     * to the second hand of the watch.
     * The following commands reset the orientation for the background green
     * area and health bars. Or, commenting this out will draw the background
     * and slowly spin it around in sync with the second hand.
    */
    gDPPipeSync(gdl++);
    gDPSetCycleType(gdl++, G_CYC_1CYCLE);
    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gDPSetPrimColor(gdl++, 0, 0, 0xE6, 0xE6, 0xE6, 0x00);
    gSPMatrix(gdl++, arg1, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    // end initial setup.

    scale = 1;

    if (check_watch_page_transistion_running())
    {
        scale = (g_CurrentPlayer->zoomintime * (g_CurrentPlayer->zoominfovynew - g_CurrentPlayer->zoominfovyold))
            / g_CurrentPlayer->zoomintimemax;

        if (scale < 0.0f)
        {
            scale = -scale;
        }

        if (scale > 1)
        {
            scale = 1;
        }

        scale = scale * scale;
    }

    if (zoom_squish == 1)
    {
        scale = 0.05f;
        g_WatchBackgroundGreen = 0xE0;

        if (g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x4 || g_CurrentPlayer->watch_animation_state == WATCH_ANIMATION_0x6)
        {
            scale = bondviewWatchAnimationRelated();
        }
    }

    guScale(&gfx_background_8007B0A0, 0.25f, 0.25f, 0.25f);

    gSPMatrix(gdl++, &gfx_background_8007B0A0, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);

    if (zoom_squish == 0)
    {
        gSPClearGeometryMode(gdl++, G_CULL_BOTH);
        // draw body armor bars
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_body_armor_bar_gdl));
        // draw health bars
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_health_bar_gdl));
    }

    /**
     * This section renders main background, side health bar & body armor bars while zooming in or out.
    */
    guScale(&gfx_background_8007B0E0, 1, 1, scale);

    gSPMatrix(gdl++, &gfx_background_8007B0E0, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);

    if (zoom_squish == 1)
    {
        gSPClearGeometryMode(gdl++, G_CULL_BOTH);
        // draw body armor bars
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_body_armor_bar_gdl));
        // draw health bars
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(&g_CurrentPlayer->watch_health_bar_gdl));
    }
    /**
     * End health bar zoom section
    */

    sub_GAME_7F0A33F8(sp44, WATCH_BACKGROUND_VERTEX_COUNT, 0.92f, 0);
    draw_watch_background(sp3C, OS_PHYSICAL_TO_K0(sp44), WATCH_BACKGROUND_VERTEX_COUNT, 0);

    gDPPipeSync(gdl++);
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gdl++, 0, 0, 0x00, 0xFF, 0x00, 0x00);
    gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(sp3C));
    gDPPipeSync(gdl++);

    /**
     * This section renders the green background area of the watch menu.
    */
    if (g_WatchBackgroundGreen < 0xE0)
    {
        sub_GAME_7F0A33F8(sp48, WATCH_BACKGROUND_VERTEX_COUNT, 0.899999976158f, 0);
        draw_watch_background(sp40, OS_PHYSICAL_TO_K0(sp48), WATCH_BACKGROUND_VERTEX_COUNT, 0);

        gDPSetRenderMode(gdl++, G_RM_AA_PCL_SURF, G_RM_AA_PCL_SURF2);
    }
    else
    {
        sub_GAME_7F0A33F8(sp48, WATCH_BACKGROUND_VERTEX_COUNT, 0.899999976158f, 1);
        draw_watch_background(sp40, OS_PHYSICAL_TO_K0(sp48), WATCH_BACKGROUND_VERTEX_COUNT, 1);

        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    }

    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(sp40));
    /**
     * // end green background area.
    */

    /**
     * This section renders the green rectangles/page select at the bottom of the screen.
     * This is setup in bondview trigger_solo_watch_menu.
    */
    gDPSetRenderMode(gdl++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
#ifndef __sgi
    /* NATIVE (#44): the page bar has SIX segments (options.h,
     * SL_WATCH_NUMBER_SCREENS). The player struct's five-slot buffers that
     * trigger_solo_watch_menu fills cannot hold a sixth (the player is a fixed
     * 0x2A80 allocation and the static bar's vertices follow the fifth
     * rectangle), so the bar is rebuilt here every frame in the same per-frame
     * dyn buffer the green background above already uses: the same
     * setup_watch_rectangles / sub_GAME_7F0A3B40 pair with the six-segment
     * spacing, coloured as set_page_rectangle_colors colours the five, and
     * drawn under the same matrices and modes in place of the player's list.
     * The player's list is still built on open and simply not drawn. */
    {
        struct WatchVertex *bar_vtx = (struct WatchVertex *) dynAllocateVertices(SL_WATCH_NUMBER_SCREENS * 4);
        Gfx *bar_dl = dynAllocate((SL_WATCH_NUMBER_SCREENS * 2 + 1) * sizeof(Gfx));
        struct WatchVertex *v = bar_vtx;
        Gfx *d = bar_dl;
        s32 x, k;

        for (x = 0; x < SL_WATCH_NUMBER_SCREENS * SL_WATCH_SCREEN_SELECT_RECTANGLE_HSTEP; x += SL_WATCH_SCREEN_SELECT_RECTANGLE_HSTEP)
        {
            struct WatchVertex *first = v;
            v = setup_watch_rectangles(v, x, 0, SL_WATCH_SCREEN_SELECT_RECTANGLE_WIDTH, 0x14, -0x12B, 0x136);
            d = sub_GAME_7F0A3B40(d, OS_K0_TO_PHYSICAL(first));
        }
        gSPEndDisplayList(d);

        for (k = 0; k < SL_WATCH_NUMBER_SCREENS * 4; k++)
        {
            bar_vtx[k].color.r = 0x20;
            bar_vtx[k].color.g = 0x70;
            bar_vtx[k].color.b = 0x20;
        }
        for (k = (s32) watch_screen_index * 4; k <= (s32) watch_screen_index * 4 + 3; k++)
        {
            bar_vtx[k].color.r = 0x50;
            bar_vtx[k].color.g = 0xF0;
            bar_vtx[k].color.b = 0x50;
            if (watch_item_is_actively_selected)
            {
                bar_vtx[k].color.r = 0x30;
                bar_vtx[k].color.g = 0xA0;
                bar_vtx[k].color.b = 0x30;
            }
        }
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(bar_dl));
    }
#else
    gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(g_CurrentPlayer->buffer_for_watch_greenbackdrop_DL));
#endif
    /**
     * // end green rectangles/page select section
    */

    for (i=0; i<WATCH_BACKGROUND_VERTEX_COUNT; i++)
    {
        sp48[i].color.a = (s8)g_WatchBackgroundGreen;
        sp44[i].color.a = (s8)g_WatchBackgroundGreen;
    }

    if (g_WatchBackgroundGreen < 0xE0)
    {
        // Create the thin green scanline that moves up the screen while the watch does static.
        build_watch_static_scanline_vertices(g_CurrentPlayer->buffer_for_watch_static_vertices);

        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
        gSPDisplayList(gdl++, OS_PHYSICAL_TO_K0(g_CurrentPlayer->buffer_for_watch_static_DL));
    }

    return gdl;
}


Gfx *draw_background_health_and_armor_transitioning(Gfx *gdl, Mtx *param_2)
{
    return draw_background_health_and_armor(gdl, param_2, 1);
}


Gfx *draw_abort_cancel_confirm(Gfx *gdl)
{
    s32 sp7C;
    s32 sp78;
    s32 sp74;
    s32 sp70;
    s32 sp6C;
    s32 sp68;
    s32 sp64;
    s32 sp60;

    s32 pFontFile;
    s32 pFontChars;
    s32 sp54;
    s32 sp50;
    s32 sp4C;

    pFontFile = ptrFontBankGothic;
    pFontChars = ptrFontBankGothicChars;
    sp54 = langGet(getStringID(LOPTIONS, OPTION_STR_24_ABORT_LF)); //abort:
    sp50 = langGet(getStringID(LOPTIONS, OPTION_STR_25_CONFIRM_LF)); //confirm
    sp4C = langGet(getStringID(LOPTIONS, OPTION_STR_26_CANCEL_LF)); //cancel
    sp7C = 0x51;

    sp78 = (j_text_trigger ? 0xF : 0) + 0xBD;

    sp74 = (j_text_trigger ? 0xA : 0) + 0x88;

    sp70 = sp6C = sp68 = (j_text_trigger ? 3 : 0) + (PAL ? 0x4E : 0x4C);

    if (watch_item_is_actively_selected != 0)
    {
        if (D_800409A4 == 0)
        {
            if ((joyGetStickX(PLAYER_1) >= 0x2E) || (joyGetButtons(PLAYER_1, 0x111) != 0))
            {
                D_800409A4 = 1;
            }
        }
        else
        {
            if (D_800409A4 != 0)
            {
                if ((joyGetStickX(PLAYER_1) < -0x2D) || (joyGetButtons(PLAYER_1, 0x222) != 0))
                {
                    D_800409A4 = 0;
                }
            }
        }
    }

    if (watch_item_is_actively_selected != 0)
    {
        textMeasure(&sp60, &sp64, sp54, pFontChars, pFontFile, 0);

        gdl = textRender(gdl, &sp7C, &sp70, sp54, pFontChars, pFontFile, 0xA0FFA0F0, sp64, sp60, 0, 0);

        if (D_800409A4 != 0)
        {
            gdl = textRenderOutlined(gdl, &sp78, &sp6C, sp50, pFontChars, pFontFile, -1, 0x7000A0, viGetX(), viGetY(), 0, 0);
            gdl = textRender(gdl, &sp74, &sp68, sp4C, pFontChars, pFontFile, 0xFF00B0, viGetX(), viGetY(), 0, 0);
        }
        else
        {
            if (D_800409A4 == 0)
            {
                gdl = textRender(gdl, &sp78, &sp6C, sp50, pFontChars, pFontFile, 0xFF00B0, viGetX(), viGetY(), 0, 0);
                gdl = textRenderOutlined(gdl, &sp74, &sp68, sp4C, pFontChars, pFontFile, -1, 0x7000A0, viGetX(), viGetY(), 0, 0);
            }
        }
    }
    else
    {
        textMeasure(&sp60, &sp64, sp54, pFontChars, pFontFile, 0);
        gdl = textRender(gdl, &sp7C, &sp70, sp54, pFontChars, pFontFile, 0x800080, sp64, sp60, 0, 0);
        gdl = textRender(gdl, &sp78, &sp6C, sp50, pFontChars, pFontFile, 0x800080, viGetX(), viGetY(), 0, 0);
        gdl = textRender(gdl, &sp74, &sp68, sp4C, pFontChars, pFontFile, 0x800080, viGetX(), viGetY(), 0, 0);
    }

    return gdl;
}


Gfx *draw_text_mission_status(Gfx *gdl)
{
    s32 txtptr_1;
    s32 txtptr_2;
    s32 sp64;
    s32 sp60;
    s32 sp5C;
    s32 sp58;
    s32 pFontFile;
    s32 pFontChars;
    s32 sp4C;
    s32 joffset;

    txtptr_1 = langGet(getStringID(LOPTIONS, OPTION_STR_27_MISSIONSTATUS_LF)); //mission status:
    pFontFile = ptrFontBankGothic;
    pFontChars = ptrFontBankGothicChars;

    if (objectiveIsAllComplete())
    {
        sp4C = 0xFF00B0;
        txtptr_2 = langGet(getStringID(LOPTIONS, OPTION_STR_28_COMPLETE_LF)); //complete
    }
    else
    {
        sp4C = D_80040AF4;
        txtptr_2 = langGet(getStringID(LOPTIONS, OPTION_STR_29_INCOMPLETE_LF)); //incomplete
    }

    gdl = microcode_constructor(gdl);
    textMeasure(&sp5C, &sp58, txtptr_1, pFontChars, pFontFile, 0);
    sp64 = 0x51;
    sp60 = YOFFSET_MISSIONSTATUS;
    gdl = textRender(gdl, &sp64, &sp60, txtptr_1, pFontChars, pFontFile, 0xFF00B0, sp58, sp5C, 0, 0);

    if (j_text_trigger)
    {
        joffset = 0x22;
    }
    else
    {
        joffset = 0;
    }

    sp64 = sp64 + sp58 + joffset + 4;
    sp60 = sp60 - sp5C;
    textMeasure(&sp5C, &sp58, txtptr_2, pFontChars, pFontFile, 0);
    gdl = textRender(gdl, &sp64, &sp60, txtptr_2, pFontChars, pFontFile, sp4C, sp58, sp5C, 0, 0);
    gdl = draw_abort_cancel_confirm(gdl);

    return gdl;
}


Gfx *empty_draw_function(Gfx *gdl) {
  return gdl;
}


Gfx *draw_text_q_watch_v201_beta(Gfx *gdl)
{
    s32 txtptr;
    s32 sp50;
    s32 sp4C;
    s32 sp48;
    s32 sp44;
    s32 pFontFile;
    s32 pFontChars;
    s32 joffset;

    txtptr = langGet(getStringID(LOPTIONS, OPTION_STR_2B_QWATCHVERSION_LF)); //q watch v2.01 beta

    if (j_text_trigger)
    {
        joffset = -5;
    }
    else
    {
        joffset = 0;
    }
    sp50 = joffset + 0x65;
    sp4C = YOFFSET_7;
    sp48 = 0;
    sp44 = 0;
    pFontFile = ptrFontBankGothic;
    pFontChars = ptrFontBankGothicChars;
    gdl = microcode_constructor(gdl);
    textMeasure(&sp48, &sp44, txtptr, pFontChars, pFontFile, 0);
    gdl = textRender(gdl, &sp50, &sp4C, txtptr, pFontChars, pFontFile, 0xFF00B0, sp44, sp48, 0, 0);
    return gdl;
}




#ifndef _BONDWALK_H_
#define _BONDWALK_H_
typedef struct GunModelFileRecord {
    void *item_header;
    char *item_file_name;
    s32   has_no_model;
    void *item_weapon_stats;
    u16   upper_watch_text;
    u16   lower_watch_text;
    f32   watch_pos_x, watch_pos_y, watch_pos_z;
    f32   x_rotation,  y_rotation;
    u16   weapon_of_choice_text, watch_equipment_text;
    f32   equip_watch_x, equip_watch_y, equip_watch_z;
} GunModelFileRecord;
#endif
extern GunModelFileRecord gitem_structs[];
/* forward declarations so the compiler uses f32 calling convention ($f0) */
extern f32 bondinvGetVposWatchForIndex(s32 index);
extern f32 bondinvGetHposWatchForIndex(s32 index);
extern f32 bondinvGetDepthWatchForIndex(s32 index);
extern f32 bondinvGetDifferent45AngleForIndex(s32 index);
extern f32 bondinvGetXrotWatchForIndex(s32 index);
extern f32 bondinvGetYrotWatchForIndex(s32 index);
Gfx* draw_current_hand_item_and_ammo(Gfx* gdl) {
    Mtx* sp114;
    u16 perspNorm;
    Mtxf matrix2;
    Mtxf matrix;
    s32 sp8C;
    s32 sp88;
    s32 sp84;
    s32 sp80;
    s32 sp7C;
    s32 sp78;
    s32 temp_v0;
    s32 sp70;
    f32 sp6C;
    f32 sp68;
    f32 sp64;
    f32 sp60;
    f32 rotx;
    f32 roty;
    s8* text;
    s8* text2;
    struct GunModelFileRecord *gitem;

    sp114 = dynAllocateMatrix();
    sp84 = 0;
    sp80 = 0;

    sp7C = ptrFontBankGothic;
    sp78 = ptrFontBankGothicChars;

    temp_v0 = bondinvGetCurEquippedItem();
    sp70 = bondinvGetTextbyInvIndex(temp_v0);
    sp6C = bondinvGetVposWatchForIndex(temp_v0);
    sp68 = bondinvGetHposWatchForIndex(temp_v0);
    sp64 = bondinvGetDepthWatchForIndex(temp_v0);
    sp60 = bondinvGetDifferent45AngleForIndex(temp_v0);
    rotx = bondinvGetXrotWatchForIndex(temp_v0);
    roty = bondinvGetYrotWatchForIndex(temp_v0);
    text = bondinvGetFirstTitlebyIndex(temp_v0);
    text2 = bondinvGetSecondTitlebyIndex(temp_v0);

    if (get_debug_gunwatchpos_flag() != 0) {
        gitem = &gitem_structs[getCurrentPlayerWeaponId(0)];

        if (joyGetButtons(0, 2) != 0) {
           gitem->watch_pos_y -= 2.0f;
        }

        if (joyGetButtons(0, 1) != 0) {
            gitem->watch_pos_y += 2.0f;
        }

        if (joyGetButtons(0, 4) != 0) {
           gitem->watch_pos_x += 2.0f;
        }

        if (joyGetButtons(0, 8) != 0) {
            gitem->watch_pos_x -= 2.0f;
        }

        if (joyGetButtons(0, 0x20) != 0) {
            gitem->watch_pos_z *= 0.98000002f;
        }

        if (joyGetButtons(0, 0x10) != 0) {
             gitem->watch_pos_z *= 1.0204082f;
        }
#ifdef DEBUG

            osSyncPrintf("gun watch pos x=%f[CL,CR] y=%f[CD,CU] z=%f[TL,TR] ", gitem->watch_pos_x, gitem->watch_pos_y, gitem->watch_pos_z);

#endif
    }


#if defined(LEFTOVERDEBUG)
    guPerspective(sp114, &perspNorm, sp60, 1.33333337f, 10.0f, 10000.0f, 1.0f);
#else
    guPerspective(sp114, &perspNorm, sp60, 1.2838470f, 10.0f, 10000.0f, 1.0f);
#endif

    gSPMatrix(gdl++, osVirtualToPhysical(sp114), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

    matrix_4x4_set_rotation_around_y((roty * 6.2831855f) / 360.0f, &matrix2);
    matrix_4x4_set_rotation_around_z(6.2831855f - ((rotx * 6.2831855f) / 360.0f), &matrix);
    matrix_4x4_multiply_in_place(&matrix, &matrix2);
    matrix_4x4_set_lookat_target(&matrix, sp64, sp6C, sp68, 0.0f, sp6C, sp68, 0.0f, 1.0f, 0.0f);
    matrix_4x4_multiply_in_place(&matrix, &matrix2);

    gdl = sub_GAME_7F0A6EE8(gdl);

    if (g_WatchBackgroundGreen < 0xE0) {
        gdl = set_enviro_fog_for_items_in_solo_watch_menu(gdl, sp70, &matrix2, g_WatchBackgroundGreen + 1, 0x64DC6428);
    } else {
        gdl = set_enviro_fog_for_items_in_solo_watch_menu(gdl, sp70, &matrix2, 0xFF, 0x64DC6428);
    }

    gdl = microcode_constructor(gunDrawWatchAmmoDisplay(gdl));

    sp8C = 0x60;
#if defined(LEFTOVERDEBUG)
    sp88 = 0xA0;
#else
    sp88 = 0xBC;
#endif
    textMeasure(&sp84, &sp80, text, sp78, sp7C, 0);
    gdl = textRender(gdl, &sp8C, &sp88, text, sp78, sp7C, 0xFF00B0, sp80, sp84, 0, 0);

#if defined(LEFTOVERDEBUG)
    sp88 = 0xAA;
#else
    sp88 = 0xC6;
#endif
    textMeasure(&sp84, &sp80, text2, sp78, sp7C, 0);
    gdl = textRender(gdl, &sp8C, &sp88, text2, sp78, sp7C, 0xFF00B0, sp80, sp84, 0, 0);

    return gdl;
}


Gfx *draw_watch_mission_status_page(Gfx *gdl, Mtx *param_2)
{
    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        gdl = draw_text_q_watch_v201_beta(gdl);
        gdl = draw_text_mission_status(gdl);
        gdl = draw_current_hand_item_and_ammo(empty_draw_function(gdl));
    }
    else
    {
        check_watch_page_transistion_running();
    }

    return gdl;
}


void sub_GAME_7F0A8378(void)
{
    if (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON) == 0) {
#ifndef __sgi
        /* NATIVE (#40): a pointer click on a list row, taken once the list has
         * settled on it (the only time this runs) - equips like START below. */
        if (!sl_watch_pointer_take_equip())
#endif
        if (joyGetButtonsPressedThisFrame(PLAYER_1, START_BUTTON) == 0)
        {
            return;
        }

        if (getCurrentPlayerWeaponId(0) == bondinvGetTextbyInvIndex(g_curWatchItemIndex))
        {
            return;
        }
    }

    currentPlayerUnEquipWeaponWrapper(0, bondinvGetTextbyInvIndex(g_curWatchItemIndex));
    currentPlayerUnEquipWeaponWrapper(1, 0);
    bondinvSetCurEquippedItem(g_curWatchItemIndex);
    D_800409C4 = 10;
    sndPlaySfx(g_musicSfxBufferPtr, CAMERA_BEEP1_SFX, 0);
}


#ifndef _BONDWALK_H_
typedef struct GunModelFileRecord {
    void *item_header;
    char *item_file_name;
    s32   has_no_model;
    void *item_weapon_stats;
    u16   upper_watch_text;
    u16   lower_watch_text;
    f32   watch_pos_x, watch_pos_y, watch_pos_z;
    f32   x_rotation,  y_rotation;
    u16   weapon_of_choice_text, watch_equipment_text;
    f32   equip_watch_x, equip_watch_y, equip_watch_z;
} GunModelFileRecord;
#endif
extern GunModelFileRecord gitem_structs[];
/* forward declarations so the compiler uses f32 calling convention ($f0) */
extern f32 bondinvGet45AngleForIndex(s32 index);
extern f32 bondinvGetHoffsetForIndex(s32 index);
extern f32 bondinvGetVoffsetForIndex(s32 index);
extern f32 bondinvGetDepthForIndex(s32 index);
extern f32 bondinvGetXrotWatchForIndex(s32 index);
extern f32 bondinvGetYrotWatchForIndex(s32 index);
extern u16 *bondinvGetNameByIndex(s32 index);


Gfx *draw_watch_inventory_page(Gfx *gdl, Mtx *param_2)
{
    Mtx *sp924;
    u16 perspNorm;
    Mtxf sp8E0;
    Mtxf sp8A0;
    f32 temp_cos;
    f32 temp_sin;
    f32 sp894;
    f32 sp890;
    f32 sp88C;
    f32 sp888;
    s32 sp884;
    f32 sp880;
    f32 sp87C;
    s32 temp_s0_3;
    GunModelFileRecord *gitem;
    s32 x1;
    s32 y1;

    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        sp924 = dynAllocateMatrix();
        sp894 = bondinvGet45AngleForIndex(g_curWatchItemIndex);
        sp890 = bondinvGetHoffsetForIndex(g_curWatchItemIndex);
        sp88C = bondinvGetVoffsetForIndex(g_curWatchItemIndex);
        sp888 = bondinvGetDepthForIndex(g_curWatchItemIndex);
        sp884 = bondinvGetTextbyInvIndex(g_curWatchItemIndex);
        sp880 = bondinvGetXrotWatchForIndex(g_curWatchItemIndex);
        sp87C = bondinvGetYrotWatchForIndex(g_curWatchItemIndex);

        if (get_debug_gunwatchpos_flag() != 0)
        {
            gitem = &gitem_structs[getCurrentPlayerWeaponId(0)];

            if (joyGetButtons(0, L_CBUTTONS) != 0)
            {
                gitem->equip_watch_x -= 2.0f;
            }

            if (joyGetButtons(0, R_CBUTTONS) != 0)
            {
                gitem->equip_watch_x += 2.0f;
            }

            if (joyGetButtons(0, D_CBUTTONS) != 0)
            {
                gitem->equip_watch_y += 2.0f;
            }

            if (joyGetButtons(0, U_CBUTTONS) != 0)
            {
                gitem->equip_watch_y -= 2.0f;
            }

            if (joyGetButtons(0, L_TRIG) != 0)
            {
                gitem->equip_watch_z *= 0.98000002f;
            }

            if (joyGetButtons(0, R_TRIG) != 0)
            {
                gitem->equip_watch_z *= 1.0204082f;
            }

#if defined(VERSION_US) && defined(DEBUG)
            osSyncPrintf(
                "gun list pos x=%f[CL,CR] y=%f[CD,CU] z=%f[TL,TR] ",
                gitem->equip_watch_x,
                gitem->equip_watch_y,
                gitem->equip_watch_z);
#endif
        }

        guPerspective(sp924, &perspNorm, sp894, WATCH_PERSPECTIVE_ASPECT, 10.0f, 10000.0f, 1.0f);

#undef WATCH_INV_ASPECT_RATIO

        gSPMatrix(gdl++, osVirtualToPhysical(sp924), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);

        matrix_4x4_set_rotation_around_y((sp87C * M_TAU_F) / 360.0f, &sp8E0);
        matrix_4x4_set_rotation_around_z(M_TAU_F - ((sp880 * M_TAU_F) / 360.0f), &sp8A0);
        matrix_4x4_multiply_in_place(&sp8A0, &sp8E0);

        temp_cos = cosf(D_80040B14) * sp888;
        temp_sin = sinf(D_80040B14) * sp888;

        matrix_4x4_set_lookat_target(&sp8A0, temp_cos, sp88C, temp_sin + sp890, 0.0f, sp88C, sp890, 0.0f, 1.0f, 0.0f);

        matrix_4x4_multiply_in_place(&sp8A0, &sp8E0);

        gdl = set_enviro_fog_for_items_in_solo_watch_menu(sub_GAME_7F0A6EE8(gdl), sp884, &sp8E0, 0x40, 0xA0FFA03C);

        {
            s32 i;
            s32 textheight;
            s32 textwidth;
            s32 pFontFile2;
            s32 pFontChars2;
            char string_builder_allocation[2000];

#if defined(VERSION_JP) || defined(VERSION_EU)
            s32 pFontFile;
            s32 base_y;
            char formattedString[32];
#endif

#define LINEHEIGHT() (j_text_trigger ? 14 : 12)

#if defined(VERSION_JP) || defined(VERSION_EU)
#define WATCH_INV_BASE_Y() base_y
#else
#define WATCH_INV_BASE_Y() 0x8C
#endif

            textheight = 0;
            textwidth = 0;

#if defined(VERSION_EU)
            pFontFile2 = ptrFontBankGothic;
            pFontChars2 = ptrFontBankGothicChars;
            base_y = (j_text_trigger) ? (0x82) : (0xAA);
            string_builder_allocation[0] = 0;
#elif defined(VERSION_JP)
            pFontFile2 = ptrFontBankGothic;
            pFontChars2 = ptrFontBankGothicChars;
            base_y = (j_text_trigger) ? (0x82) : (0x8C);
            string_builder_allocation[0] = 0;
#else
            string_builder_allocation[0] = 0;
            pFontFile2 = ptrFontBankGothic;
            pFontChars2 = ptrFontBankGothicChars;
#endif

            for (i = 0; i < bondinvCountTotalItemsInInv(); i++)
            {
                char *name = bondinvGetNameByIndex(i);

                strcat(string_builder_allocation, name);
            }

            if (D_800409C4 > 0)
            {
                D_800409C4--;
            }

            game_options_inventory_navigation();

            x1 = 0x4E;
            y1 = WATCH_INV_BASE_Y();

            temp_s0_3 = 1;
            temp_s0_3 = (LINEHEIGHT() * 2) + WATCH_INV_BASE_Y() + temp_s0_3;

            gdl = microcode_constructor(gdl);

            textMeasure(&textheight, &textwidth, string_builder_allocation, pFontChars2, pFontFile2, LINEHEIGHT());

            gdl = microcode_constructor_related_to_menus(gdl, 0x4E, WATCH_INV_BASE_Y(), textwidth + 0x4E, (LINEHEIGHT() * 5) + WATCH_INV_BASE_Y(), 0);

            gdl = textRender(gdl, &x1, &y1, string_builder_allocation, pFontChars2, pFontFile2, 0xAA00B0, textwidth + 1, LINEHEIGHT() * 5, watch_inventory_text_y, LINEHEIGHT());

            gdl = microcode_constructor_related_to_menus(gdl, 0x4B, temp_s0_3, textwidth + 0x52, (LINEHEIGHT() + temp_s0_3) - 2, 0x800050);

            {
#if !defined(VERSION_JP) && !defined(VERSION_EU)
                char formattedString[32];
                s32 pFontFile;
#endif
                s32 pFontChars;
                s32 x2;
                s32 y2;
                char *invItemName;

                pFontFile = ptrFontBankGothic;
                pFontChars = ptrFontBankGothicChars;
                invItemName = bondinvGetNameByIndex(g_curWatchItemIndex);

                sprintf(formattedString, "%d, %d\n%d %f\n", watch_inventory_text_y, watch_inventory_text_target_y, g_curWatchItemIndex, (f64) watch_inventory_cursor_pos);

                gdl = microcode_constructor(gdl);

                textMeasure(&y2, &x2, formattedString, pFontChars, pFontFile, 0);

                if (watch_inventory_text_is_settled != 0)
                {
                    textMeasure(&y2, &x2, invItemName, pFontChars, pFontFile, LINEHEIGHT());

                    x1 = 0x4E;
                    y1 = (LINEHEIGHT() * 2) + WATCH_INV_BASE_Y();

                    if (D_800409C4 == 0)
                    {
                        gdl = textRender(gdl, &x1, &y1, invItemName, pFontChars, pFontFile, 0xA0FFA0F0, x2, 0x64, 0, LINEHEIGHT());
                    }
                    else
                    {
                        gdl = textRenderOutlined(gdl, &x1, &y1, invItemName, pFontChars, pFontFile, -1, 0x7000A0, x2 + 1, 0x64, 0, LINEHEIGHT());
                    }

                    sub_GAME_7F0A8378();
                }
            }

#undef WATCH_INV_BASE_Y
#undef LINEHEIGHT
        }
    }

    return gdl;
}


Gfx *unused_draw_watch_inventory_page(Gfx *gdl, Mtx *param_2) {
    s32 temp_1;
    s32 sp70;
    s32 sp6C;
    s32 sp64,sp68; //unused?

    s32 sp60;
    s32 sp5C;
    s32 sp58;
    s32 sp54;
    s32 pFontFile;
    s32 pFontChars;

    u16 *long_name;
    s32 temp_2;

    sp58 = 0;
    sp54 = 0;

    pFontFile = ptrFontBankGothic;
    pFontChars = ptrFontBankGothicChars;

    long_name = bondinvGetLongNameByIndex(g_curWatchItemIndex);
    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        temp_1 = D_800409C4;
        if (temp_1 > 0)
        {
            D_800409C4 = temp_1 - 1;
        }

        game_options_inventory_navigation();
        gdl = microcode_constructor(gdl);

        textMeasure(&sp58, &sp54, long_name, pFontChars, pFontFile, 0);

        sp70 = ((s32) (0xAA - sp54) / 2) + 0x4B;
        temp_2 = sp70;

        sp6C = 0x1E;
        gdl = microcode_constructor_related_to_menus(gdl, temp_2, 0x1E, sp60, sp5C, 0x800050);

        if (watch_inventory_text_is_settled)
        {
            sub_GAME_7F0A8378();
            if (D_800409C4 == 0)
            {
                gdl = textRender(gdl, &sp70, &sp6C, long_name, pFontChars, pFontFile, 0xA0FFA0F0, sp54, 0x64, 0, 0);
            }
            else
            {
                gdl = textRenderOutlined(gdl, &sp70, &sp6C, long_name, pFontChars, pFontFile, -1, 0x7000A0, sp54 + 1, 0x64, 0, 0);
            }
        }
        else
        {
            gdl = textRender(gdl, &sp70, &sp6C, long_name, pFontChars, pFontFile, 0xAA00B0, sp54, 0x64, 0, 0);
        }

    }

    return gdl;
}


/**
 * Address: 7F0A8D40
 */
void update_volume_slider_verts(struct WatchVertex *verts, f32 fill_amount, s32 transition_width)
{
    s32 i;
    struct WatchVertex *vtx;
    s32 xdiff;
    s32 filledrightx;

    xdiff = verts[2].coord1.x - verts[4].coord1.x;
    transition_width = (s32) (((f32) transition_width) * (1.2f - fill_amount));
    i = 0;
    vtx = verts;

    /**
     * Verts 0-3: unfilled right section.
     * Dark green.
     */
    do
    {
        i++;
        vtx++;
        vtx[-1].color.r = 0x20;
        vtx[-1].color.g = 0x40;
        vtx[-1].color.b = 0x20;
        vtx[-1].color.a = 0xE0;
    } while (i < 4);

    i = 4;
    vtx = &verts[4];

    /**
     * Verts 4-9: filled left section and transition.
     * Verts 10 and 11: moving boundary between transition and unfilled section.
     */
    do
    {
        filledrightx = xdiff + transition_width;

        if (i < 10)
        {
            // The filled section gets brighter as the volume increases.
            s32 rb;
            s32 g;
            rb = ((s32) (48.0f * fill_amount)) + 0x40;
            g = ((s32) (96.0f * fill_amount)) + 0x80;
            vtx->color.r = rb;
            vtx->color.g = g;
            vtx->color.b = rb;

            // Left edge of the transition band.
            if (i >= 6)
            {
                vtx->coord1.x = (s32) ((((f32) verts[4].coord1.x) + ((((f32) xdiff) + ((f32) transition_width)) * fill_amount)) - ((f32) transition_width));
                if (vtx->coord1.x < verts[4].coord1.x)
                {
                    vtx->coord1.x = verts[4].coord1.x;
                }
            }
        }
        else
        {
        // Right edge of the transition band.
        vtx->coord1.x = (s32) ((((f32) verts[4].coord1.x) + (((f32) filledrightx) * fill_amount)) + ((f32) transition_width));
            if (verts[2].coord1.x < vtx->coord1.x)
            {
                vtx->coord1.x = verts[2].coord1.x;
            }
        }

    i++;
    vtx++;

    } while (i != 12);

    // Make the unfilled section begin at the right edge of the transition area.
    filledrightx = verts[10].coord1.x;
    verts[1].coord1.x = filledrightx;
    verts[0].coord1.x = filledrightx;
}



/**
 * Address: 7F0A8ED0
 */
void watch_adjust_volume_slider(u16* outVolume) {
    s32 joy_x;
    s32 adjusted_volume;

    joy_x = joyGetStickX(PLAYER_1);
    adjusted_volume = *outVolume;

    if (joyGetButtons(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD)) {
        adjusted_volume = adjusted_volume + WATCH_VOL_ADJUST_STEP;
    } else if (joyGetButtons(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD)) {
        adjusted_volume = adjusted_volume - WATCH_VOL_ADJUST_STEP;
    }

    // Clamp stick deflection
    if (joy_x >= 0x47) {
        joy_x = 0x46;
    } else if (joy_x < -0x46) {
        joy_x = -0x46;
    }

    // Increase volume
    if (joy_x >= 8) {
        adjusted_volume += (joy_x * 0x800 + -0x3800) / 0x46;
    // Decrease volume
    } else if (joy_x < -7) {
        adjusted_volume += (joy_x * 0x800 + 0x3800) / 0x46;
    }

    // Clamp volume between min and max allowed volume.
    if (adjusted_volume >= VOLUME_MAX + 1) {
        *outVolume = VOLUME_MAX;
    } else if (adjusted_volume < 0) {
        *outVolume = 0;
    } else {
        *outVolume = adjusted_volume;
    }
}


/**
 * Address: 7F0A8FEC
 */
Gfx *draw_fx_volume_slider(Gfx *gdl)
{
    u16 volume;
    f32 fvolume;
    struct WatchVertex *vtx1;
    struct WatchVertex *vtx;
    Gfx *cmd;

    vtx1 = (struct WatchVertex *)dynAllocateVertices(12);

    volume = sndGetSfxSlotFirstNaturalVolume();

    if (watch_item_is_actively_selected && game_options_index == 1)
    {
        watch_adjust_volume_slider(&volume);
    }

    fvolume = (f32)(u32)volume / 32767.0f;

    sndApplyVolumeAllSfxSlot(volume);

    if (1);

    cmd = gdl++;
    gDPSetRenderMode(cmd, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx1));
    vtx = setup_watch_rectangles(vtx1, 0, 0, 600, 20, -299, -205);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    vtx = setup_watch_rectangles(vtx, 0, 0, 600, 20, -299, -205);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    setup_watch_rectangles(vtx, 0, 0, 600, 20, -299, -205);

    update_volume_slider_verts(vtx1, fvolume, 30);

    return gdl;
}


u16 call_sndGetSfxSlotFirstNaturalVolume(void) {
    return sndGetSfxSlotFirstNaturalVolume();
}


void sub_GAME_7F0A91A0(u16 arg0) {
    sndApplyVolumeAllSfxSlot(arg0);
}


/**
 * Address: 7F0A91C8
 */
Gfx *draw_music_volume_slider(Gfx *gdl)
{
    u16 volume;
    f32 fvolume;
    struct WatchVertex *vtx1;
    struct WatchVertex *vtx;
    Gfx *cmd;

    vtx1 = (struct WatchVertex *)dynAllocateVertices(12);
    volume = get_mTrack2Vol();

    if (watch_item_is_actively_selected && game_options_index == 0) {
        watch_adjust_volume_slider(&volume);
    }

    fvolume = (f32)(u32)volume / 32767.0f;
    set_mTrack2Vol(volume);

    if(1);

    cmd = gdl++;
    gDPSetRenderMode(cmd, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx1));
    vtx = setup_watch_rectangles(vtx1, 0, 0, 600, 20, -299, -275);


    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    vtx = setup_watch_rectangles(vtx, 0, 0, 600, 20, -299, -275);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    setup_watch_rectangles(vtx, 0, 0, 600, 20, -299, -275);

    update_volume_slider_verts(vtx1, fvolume, 30);

    return gdl;
}


u16 get_mTrack2Vol(void)
{
  return mTrack2Vol;
}


void set_mTrack2Vol(u16 param_1)
{
    mTrack2Vol = param_1;
    musicTrack2ApplySeqpVol(mTrack2Vol);
}


/**
 * Address: 7F0A9398
 *
 * This draws the text for the toggle options (both option titles and values).
 * It also draws button names and the actions mapped to them on the controller screen.
 */
Gfx *draw_options_labels(Gfx *gdl, s32 x, s32 y, char *text, u32 colour, s32 outlined, u32 outlinecolour, s32 centre, s32 drawbg, u32 bgcolour, s32 rightalign)
{
    s32 textx;
    s32 textright;
    s32 textbottom;
    s32 textwidth;
    s32 textheight;
    struct font *font;
    struct fontchar *chars;

    font = ptrFontBankGothic;
    chars = ptrFontBankGothicChars;

    textMeasure(&textheight, &textwidth, text, chars, font, 10);

    if (centre)
    {
        textx = x - (textwidth / 2);
    }
    else if (rightalign)
    {
        textx = x - textwidth;
    }
    else
    {
        textx = x;
    }

    textright = textx + textwidth;
    textbottom = y + textheight;

    if (g_WatchBackgroundGreen < 0xe0)
    {
        /**
         * Increases the effect of fuzzy static mode on the text,
         * making text pixels more subject to disappearing or almost disappearing.
         */
        gDPSetRenderMode(gdl++, G_RM_AA_PCL_SURF, G_RM_AA_PCL_SURF2);
    }
    else
    {
        gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    }

    if (drawbg)
    {
        gdl = microcode_constructor_related_to_menus(gdl, textx - 1, (y + outlined) + 1, textright + 1, textbottom + 1, bgcolour);
    }

    gDPSetRenderMode(gdl++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);

    if (!outlined)
    {
        gdl = textRender(gdl, &textx, &y, text, chars, font, colour, textwidth, textheight, 0, 10);
    }

    if (outlined)
    {
        gdl = textRenderOutlined(gdl, &textx, &y, text, chars, font, colour, outlinecolour, textwidth + 1, textheight, 0, 10);
    }

    return gdl;
}


f32 sub_GAME_7F0A95C4(f32 param_1, f32 param_2, f32 param_3)
{
    if (param_1 < param_2) {
        param_1 += (param_2 - param_1) / param_3;
    } else if (param_2 < param_1) {
        param_1 -= (param_1 - param_2) / param_3;
    }

    return param_1;
}


s32 sub_GAME_7F0A9610(void) {

    if ((g_WatchControllerSpinAngle < 0.1f) &&
        (g_WatchControllerSpinAngle > -0.1f) &&
        (g_WatchControllerPitch < 0.1f) &&
        (g_WatchControllerPitch > -0.1f))
    {

        return 1;

    }
    return 0;
}


void sub_GAME_7F0A9684(s8 contpadnum, s32 *counter, f32 *value, f32 *step)
{
    s32 count;

    if ((joyGetStickX(contpadnum) >= 10) || (joyGetStickX(contpadnum) < -9))
    {
        if (watch_item_is_actively_selected)
        {
            count = 0;

            if (controller_options_index == 1)
            {
                goto zero_done;
            }
        }
    }

    count = *counter;

    if (count < 100)
    {
        count = (*counter = count + 1);
        count = *counter;
    }

    goto counter_done;

zero_done:
    *counter = 0;

counter_done:
    if (count >= 100)
    {
        *value = sub_GAME_7F0A95C4(*value, (-(*step)) / 10.0f, 4.0f);
    }
    else if (watch_item_is_actively_selected && (controller_options_index == 1))
    {
        *value = sub_GAME_7F0A95C4(*value, (((-((f32) joyGetStickX(contpadnum))) * 0.2f) * M_TAU_F) / 360.0f, 4.0f);
    }
}


/**
 * Address: 7F0A97D0
 */
Gfx *draw_controller_style_text(Gfx *gdl)
{
    char pad[12];
    char text[2000];
    s32 x;
    s32 y;
    s32 i;
    u16 *stringids;
    s32 textheight;
    s32 textwidth;
    struct font *font;
    struct fontchar *chars;
    s32 tmp;
    u8 *selectedtext;

    font = ptrFontBankGothic;
    chars = ptrFontBankGothicChars;
    textheight = 0;
    textwidth = 0;
    text[0] = '\0';
    i = 0;

    if (D_800409D8 > 0)
    {
        stringids = game_control_styles;

        do
        {
            strcat(text, langGet(*stringids));
            i++;
            stringids += 10;
            if (i);
        }
        while (i < D_800409D8);
    }

    if (watch_item_is_actively_selected)
    {
        if (controller_options_index == CONTROLLER_OPTIONS_INDEX_STYLE)
        {
            sub_GAME_7F0A611C(&g_CurrentPlayer->cur_player_control_type_2, &g_CurrentPlayer->cur_player_control_type_0, D_800409D8, &g_CurrentPlayer->neg_vspacing_for_control_type_entry, &g_CurrentPlayer->cur_player_control_type_1, (s32 *) (&g_CurrentPlayer->has_set_control_type_data), 0, 1, (j_text_trigger) ? (0xe) : (0xa));
        }
    }

    x = 0xaa;
    tmp = j_text_trigger;
    y = 0x1a;

    textMeasure(&textheight, &textwidth, text, chars, font, (tmp) ? (0xe) : (0xa));

    i = (j_text_trigger) ? (0xe) : (0xa);

    gdl = textRender(gdl, &x, &y, text, chars, font, 0x00aa00b0, textwidth, i, g_CurrentPlayer->neg_vspacing_for_control_type_entry, (j_text_trigger) ? (0xe) : (0xa));

    if (g_CurrentPlayer->has_set_control_type_data != 0)
    {
        selectedtext = langGet(*(u16 *)((u8 *) game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20)));

        textMeasure(&textheight, &textwidth, selectedtext, chars, font, (j_text_trigger) ? (0xe) : (0xa));

        x = 0xaa;

        if (j_text_trigger ? 1 : 0)
        {
            goto selected_y_set;
        }

        goto selected_y_set;

selected_y_set:
        y = 0x1a;

        selectedtext = langGet(*(u16 *)((u8 *) game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20)));

        gdl = textRender(gdl, &x, &y, selectedtext, chars, font, 0xa0ffa0f0, textwidth, 0x64, 0, (j_text_trigger) ? (0xe) : (0xa));
    }

    return gdl;
}

 
Gfx *sub_GAME_7F0A9AB8(Gfx *gdl)
{
    u8 *dirtext1;
    u8 *dirtext2;
 
    if (game_options_entries[0].current_value == 1)
    {
        dirtext1 = langGet(getStringID(LOPTIONS, OPTION_STR_2D_UP_LF));
        dirtext2 = langGet(getStringID(LOPTIONS, OPTION_STR_2C_DOWN_LF));
    }
    else
    {
        dirtext1 = langGet(getStringID(LOPTIONS, OPTION_STR_2C_DOWN_LF));
        dirtext2 = langGet(getStringID(LOPTIONS, OPTION_STR_2D_UP_LF));
    }
 
    {
        char strA[] = "(A)\n";
        char strB[] = "(B)\n";
        char strZ[] = "(Z)\n";
        char strL[] = "(L)\n";
        char strR[] = "(R)\n";
        char strC[] = "(C)\n";
        char strPlus[] = "(+)\n";
        char strS[] = "(S)\n";
        char str3D[] = "(3D)\n";
        u8 *ctext;
        u8 *dpadtext;
        s32 buttons;
        s32 showmovesight;
        volatile unsigned int y;
        u8 pad[8];
 
        showmovesight = 0;
        gdl = microcode_constructor(gdl);
 
        if (joyGetButtons(PLAYER_1, L_TRIG))
        {
            gdl = draw_options_labels(gdl, 0x32, OPTLABELS_ROW1_Y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 8)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
 
            if (*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 8) == getStringID(LOPTIONS, OPTION_STR_01_AIM_LF))
            {
                showmovesight = 1;
            }
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x32, OPTLABELS_ROW1_Y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 8)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
        }
 
        y = OPTLABELS_ROW2_Y;
 
        if (controller_options_index != 1 || !watch_item_is_actively_selected || !joyGetButtons(PLAYER_1, U_JPAD | D_JPAD | L_JPAD | R_JPAD))
        {
            gdl = draw_options_labels(gdl, 0x32, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 14)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
        }
        else
        {
            if (joyGetButtons(PLAYER_1, U_JPAD))
            {
                if (*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 14) == getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF))
                {
                    dpadtext = langGet(getStringID(LOPTIONS, OPTION_STR_30_FORWARD_LF));
                }
                else
                {
                    dpadtext = dirtext1;
                }
            }
            else if (joyGetButtons(PLAYER_1, D_JPAD))
            {
                if (*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 14) == getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF))
                {
                    dpadtext = langGet(getStringID(LOPTIONS, OPTION_STR_31_BACK_LF));
                }
                else
                {
                    dpadtext = dirtext2;
                }
            }
            else if (joyGetButtons(PLAYER_1, L_JPAD))
            {
                dpadtext = langGet(getStringID(LOPTIONS, OPTION_STR_2F_SIDESTEP_LF));
            }
            else if (joyGetButtons(PLAYER_1, R_JPAD))
            {
                dpadtext = langGet(getStringID(LOPTIONS, OPTION_STR_2E_SIDESTEP_LF));
            }
 
            gdl = draw_options_labels(gdl, 0x32, y, dpadtext, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
        }
 
        y += OPTLABELS_ROW_PITCH;
        gdl = draw_options_labels(gdl, 0x32, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 16)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
        y += OPTLABELS_ROW_PITCH;
 
        if (joyGetButtons(PLAYER_1, Z_TRIG))
        {
            gdl = draw_options_labels(gdl, 0x32, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 6)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
 
            if (*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 6) == getStringID(LOPTIONS, OPTION_STR_01_AIM_LF))
            {
                showmovesight = 1;
            }
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x32, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 6)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
        }
 
        y -= OPTLABELS_COL_RET;
 
        if (joyGetButtons(PLAYER_1, R_TRIG))
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 10)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
 
            if (*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 10) == getStringID(LOPTIONS, OPTION_STR_01_AIM_LF))
            {
                showmovesight = 1;
            }
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 10)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
 
        y += OPTLABELS_ROW_PITCH;
 
        if (controller_options_index != 1 || !watch_item_is_actively_selected || !joyGetButtons(PLAYER_1, U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS))
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 12)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
        else
        {
            buttons = joyGetButtons(PLAYER_1, U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS);
 
            /* power-of-two test: exactly one C button held */
            if ((buttons & (buttons - 1U)) == 0)
            {
                if (joyGetButtons(PLAYER_1, U_CBUTTONS))
                {
                    if (*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 12) == getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF))
                    {
                        ctext = langGet(getStringID(LOPTIONS, OPTION_STR_30_FORWARD_LF));
                    }
                    else
                    {
                        ctext = dirtext1;
                    }
                }
                else if (joyGetButtons(PLAYER_1, D_CBUTTONS))
                {
                    if (*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 12) == getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF))
                    {
                        ctext = langGet(getStringID(LOPTIONS, OPTION_STR_31_BACK_LF));
                    }
                    else
                    {
                        ctext = dirtext2;
                    }
                }
                else if (joyGetButtons(PLAYER_1, L_CBUTTONS))
                {
                    ctext = langGet(getStringID(LOPTIONS, OPTION_STR_2F_SIDESTEP_LF));
                }
                else if (joyGetButtons(PLAYER_1, R_CBUTTONS))
                {
                    ctext = langGet(getStringID(LOPTIONS, OPTION_STR_2E_SIDESTEP_LF));
                }
 
                gdl = draw_options_labels(gdl, 0x10e, y, ctext, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
            }
            else
            {
                gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 12)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
            }
        }
 
        y += OPTLABELS_ROW_PITCH;
 
        if (joyGetButtons(PLAYER_1, B_BUTTON))
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 4)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 4)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
 
        y += OPTLABELS_ROW_PITCH;
 
        if (joyGetButtons(PLAYER_1, A_BUTTON))
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 2)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
        }
        else
        {
            gdl = draw_options_labels(gdl, 0x10e, y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 2)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
 
        if (showmovesight)
        {
            gdl = draw_options_labels(gdl, 0xfa, OPTLABELS_HINT_Y, langGet(getStringID(LOPTIONS, OPTION_STR_08_MOVESIGHT_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
        }
        else
        {
            gdl = draw_options_labels(gdl, 0xfa, OPTLABELS_HINT_Y, langGet(*(u16 *)((u8 *)game_control_styles + (g_CurrentPlayer->cur_player_control_type_0 * 20) + 18)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
        }
 
        return gdl;
    }
}


Gfx *display_text_buttons_dual_control(Gfx *gdl)
{
    s32 textptr_aux;

    gdl = microcode_constructor(gdl);

    if (joyGetButtons(PLAYER_1, A_BUTTON))
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_WEAPTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0); //weapon
    }
    else
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_WEAPTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0); //weapon
    }

    if (joyGetButtons(PLAYER_1, B_BUTTON))
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_ACTIONTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0); //action
    }
    else
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_ACTIONTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0); //action
    }

    if ((g_CurrentPlayer->cur_player_control_type_0 == CONTROLLER_CONFIG_PLENTY) || (g_CurrentPlayer->cur_player_control_type_0 == CONTROLLER_CONFIG_GALORE))
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF)); //fire
    }
    else
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_01_AIM_LF)); //aim
    }

    if (joyGetButtons(PLAYER_1, Z_TRIG))
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_5, textptr_aux, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
    }
    else
    {
        gdl = draw_options_labels(gdl, 0x5A, YOFFSET_5, textptr_aux, 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);
    }

    if ((g_CurrentPlayer->cur_player_control_type_0 == CONTROLLER_CONFIG_PLENTY) || (g_CurrentPlayer->cur_player_control_type_0 == CONTROLLER_CONFIG_DOMINO))
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF)); //move
    }
    else
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF)); //look
    }

    gdl = draw_options_labels(gdl, 0x5A, YOFFSET_4, textptr_aux, 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 0);

    if (joyGetButtons(1, A_BUTTON))
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_WEAPTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1); //weapon
    }
    else
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_WEAPTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_03_WEAPON_LF)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1); //weapon
    }

    if (joyGetButtons(1, B_BUTTON))
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_ACTIONTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF)), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1); //action
    }
    else
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_ACTIONTEXT, langGet(getStringID(LOPTIONS, OPTION_STR_02_ACTION_LF)), 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1); //action
    }

    if ((g_CurrentPlayer->cur_player_control_type_0 == CONTROLLER_CONFIG_PLENTY) || (g_CurrentPlayer->cur_player_control_type_0 == CONTROLLER_CONFIG_GALORE))
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_01_AIM_LF)); //aim
    }
    else
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_00_FIRE_LF)); //fire
    }

    if (joyGetButtons(1, Z_TRIG))
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_5, textptr_aux, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 1);
    }
    else
    {
        gdl = draw_options_labels(gdl, 0xE6, YOFFSET_5, textptr_aux, 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
    }

    if ((g_CurrentPlayer->cur_player_control_type_0 == CONTROLLER_CONFIG_PLENTY) || (g_CurrentPlayer->cur_player_control_type_0 == CONTROLLER_CONFIG_DOMINO))
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_06_LOOK_LF)); //look
    }
    else
    {
        textptr_aux = langGet(getStringID(LOPTIONS, OPTION_STR_05_MOVE_LF)); //move
    }

    gdl = draw_options_labels(gdl, 0xE6, YOFFSET_4, textptr_aux, 0xAA00B0, 0, -1, 0, 0, 0x3000B0, 1);
    return gdl;
}


/**
 * Address: 7F0AADC0
 * 
 * Draw the controller model(s) and the individual buttons.
 */
Gfx *draw_watch_controller(Gfx *gdl)
{
    Mtx *perspmtx;
    Mtxf identity;
    Mtxf zrotmtx;
    Mtxf xrotmtx;
    Mtxf tmpmtx1;
    Mtxf tmpmtx2;
    Mtxf modelmtx;
    Mtxf finalmtx;
    coord3d pos;
    u16 perspNorm;
    Mtxf lookat1;
    Mtxf lookat2;
    void *watchTable;
    s32 green;
    s8 contpadnum0;
    s8 contpadnum1;
    WatchContButtonPositions table0;
    WatchContButtonPositions table1;
    WatchContButtonPositions table2;
    Gfx *cmd0;
    Gfx *cmd1;

    perspmtx = dynAllocateMatrix();
    pos = g_ControllerPos;
    contpadnum0 = 0;
    contpadnum1 = 1;
    table0 = g_1ContButtonPositions[0];
    table1 = g_2ContLeftButtonPositions[0];
    table2 = g_2ContRightButtonPositions[0];

    sub_GAME_7F0A9684(0, &D_80040B2C, &g_WatchControllerSpinSpeed, &g_WatchControllerSpinAngle);
    matrix_4x4_set_identity(&identity);

    g_WatchControllerSpinAngle += (g_WatchControllerSpinSpeed * ((f32) WATCH_ROTATION_FRAMES)) * 0.5f;
    g_WatchControllerSpinAngle = watchWrapAroundPI(g_WatchControllerSpinAngle);

    matrix_4x4_set_rotation_around_z(g_WatchControllerSpinAngle, &zrotmtx);

    if (watch_item_is_actively_selected && (controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS))
    {
        g_WatchControllerPitch = sub_GAME_7F0A95C4(g_WatchControllerPitch, (((f32) joyGetStickY(PLAYER_1)) * M_TAU_F) / 360.0f, 4.0f);
    }
    else
    {
        g_WatchControllerPitch = sub_GAME_7F0A95C4(g_WatchControllerPitch, 0.0f, 4.0f);
    }

    matrix_4x4_set_rotation_around_x((-g_WatchControllerPitch) - 0.78539819f, &xrotmtx);
    matrix_4x4_multiply(&identity, &zrotmtx, &tmpmtx1);
    matrix_4x4_multiply(&tmpmtx1, &xrotmtx, &tmpmtx2);
    matrix_4x4_set_identity_and_position(&pos, &tmpmtx1);
    matrix_4x4_multiply(&tmpmtx1, &tmpmtx2, &modelmtx);

    if (controllerCheckDualControllerTypesAllowed())
    {
        f32 eye = 495.0f;

        watchTable = &table1;

        if (1);

        matrix_4x4_set_lookat_target(&lookat1, eye, 2500.0f, 32.0f, eye, 0.0f, 32.0f, 0.0f, 0.0f, -1.0f);
    }
    else
    {
        watchTable = &table0;
        matrix_4x4_set_lookat_target(&lookat1, -5.0f, 2000.0f, -168.0f, -5.0f, 0.0f, -168.0f, 0.0f, 0.0f, -1.0f);
    }

    matrix_4x4_multiply(&lookat1, &modelmtx, &finalmtx);

    guPerspective(perspmtx, &perspNorm, WATCH_PERSPECTIVE_FOVY, WATCH_PERSPECTIVE_ASPECT, 1000.0f, 3000.0f, 1.0f);

    cmd0 = gdl++; \
    gSPMatrix(cmd0, osVirtualToPhysical(perspmtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);

    gdl = sub_GAME_7F0A6EE8(gdl);
    green = g_WatchBackgroundGreen;

#ifndef __sgi
    /* THE MODERN PAD (#63): the attached family's own controller, its parts
     * following the player's hands, in the N64 pad's place - the same
     * matrices, look-at and perspective - with the family's button names
     * and the actions the binding registry holds for them around it
     * (sl_watch_controller.c, 2026-09-20). Everything below is the N64 pad:
     * the fallback for no model or a generic pad (its labels then read the
     * pinned 1.1 Honey names, which is what the pad's Z / R mean). The
     * probe (SL_PAD_DBG only) logs the face's eye-space facing under this
     * finalmtx for whichever pad draws - the N64 pad's face is +y in
     * GjoypadZ's frame, the models' is measured from their own mesh. */
    sl_watch_controller_probe(&finalmtx);
    if (sl_watch_controller_modern())
    {
        gdl = sl_watch_controller_draw(gdl, &finalmtx, green);
        return gdl;
    }
#endif

    if (green < 0xe0)
    {
        gdl = watchRenderController(gdl, &finalmtx, green - 6, 1, watchTable, &contpadnum0);
    }
    else
    {
        gdl = watchRenderControllerOpaque(gdl, &finalmtx, 1, (s32) watchTable, &contpadnum0);
    }

    if (controllerCheckDualControllerTypesAllowed())
    {
        sub_GAME_7F0A9684(1, &D_80040B3C, &D_80040B38, &D_80040B34);
        matrix_4x4_set_identity(&identity);

        D_80040B34 += (D_80040B38 * ((f32) WATCH_ROTATION_FRAMES)) * 0.5f;
        D_80040B34 = watchWrapAroundPI(D_80040B34);

        matrix_4x4_set_rotation_around_z(D_80040B34, &zrotmtx);

        if (watch_item_is_actively_selected && (controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS))
        {
            D_80040B30 = sub_GAME_7F0A95C4(D_80040B30, (((f32) joyGetStickY(PLAYER_2)) * M_TAU_F) / 360.0f, 4.0f);
        }
        else
        {
            D_80040B30 = sub_GAME_7F0A95C4(D_80040B30, 0.0f, 4.0f);
        }

        matrix_4x4_set_rotation_around_x((-D_80040B30) - 0.78539819f, &xrotmtx);
        matrix_4x4_multiply(&identity, &zrotmtx, &tmpmtx1);
        matrix_4x4_multiply(&tmpmtx1, &xrotmtx, &tmpmtx2);
        matrix_4x4_set_identity_and_position(&pos, &tmpmtx1);
        matrix_4x4_multiply(&tmpmtx1, &tmpmtx2, &modelmtx);
        matrix_4x4_set_lookat_target(&lookat2, -505.0f, 2500.0f, 32.0f, -505.0f, 0.0f, 32.0f, 0.0f, 0.0f, -1.0f);
        matrix_4x4_multiply(&lookat2, &modelmtx, &finalmtx);

        guPerspective(perspmtx, &perspNorm, WATCH_PERSPECTIVE_FOVY, WATCH_PERSPECTIVE_ASPECT, 1000.0f, 3000.0f, 1.0f);

        /**
         * This should be something like:
         * 
         *   cmd1 = gdl++;
         *   gSPMatrix(cmd1, osVirtualToPhysical(perspmtx),G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
         * 
         * but it doesn't match for some reason.
         */
        cmd1 = gdl++; cmd1->words.w0 = 0x01030040; cmd1->words.w1 = osVirtualToPhysical(perspmtx);

        gdl = sub_GAME_7F0A6EE8(gdl);
        green = g_WatchBackgroundGreen;

        if (green < 0xe0)
        {
            gdl = watchRenderController(gdl, &finalmtx, green - 6, 1, &table2, &contpadnum1);
        }
        else
        {
            gdl = watchRenderControllerOpaque(gdl, &finalmtx, 1, (s32) (&table2), &contpadnum1);
        }
    }

    if (controllerCheckDualControllerTypesAllowed())
    {
        gdl = display_text_buttons_dual_control(gdl);
    }
    else
    {
        gdl = sub_GAME_7F0A9AB8(gdl);
    }

    return gdl;
}


void reset_controller_options_index(void) {
    controller_options_index = CONTROLLER_OPTIONS_INDEX_STYLE;
}


void reset_game_options_index(void) {
    game_options_index = 0;
}


void zero_D_800409A4(void) {
    D_800409A4 = 0;
}


u32 return_arg0_7F0AB4B0(u32 uParm1) {
    return uParm1;
}


Gfx *draw_watch_control_options_page(Gfx *gdl, Mtx *param_2) {
    s32 phi_s1;
    u16 *textptr;
    s32 sp5C;
    s32 sp58;
    s32 sp54;
    s32 sp50;
    s32 pFontFile;
    s32 pFontChars;

    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1) {

        gdl = draw_watch_controller(gdl);
        pFontFile = ptrFontBankGothic;
        pFontChars = ptrFontBankGothicChars;

        gdl = microcode_constructor(gdl);
#ifndef __sgi
        /* THE NATIVE PAGE (#63, 2026-09-20): the two rows are BUTTON LAYOUT
         * and STICK LAYOUT (src/native/sl_watch_controller.c), on the same
         * two row indices the original page's CONTROL STYLE / CONTROLLER
         * rows used - so the original navigation (controller_options_*),
         * the A / Z latch and the pointer's rows are untouched - with the
         * latched LEFT / RIGHT stepping the value. The N64 CONTROL STYLE
         * row is gone with the styles (the game side is pinned to 1.1
         * Honey). Everything below is the original page, verbatim. */
        return sl_watch_layout_rows_draw(gdl);
#endif
        textptr = langGet(getStringID(LOPTIONS, OPTION_STR_32_CONTROLSTYLE_LF)); //control style

        sp5C = XOFFSET_1;
        sp58 = 0x1A;
        phi_s1 = 0xFF00B0;
        if (controller_options_index == CONTROLLER_OPTIONS_INDEX_STYLE)
        {
            phi_s1 = 0xA0FFA0F0;
            if (watch_item_is_actively_selected != 0)
            {
                phi_s1 = -1;
            }
        }

        textMeasure(&sp50, &sp54, textptr, pFontChars, pFontFile, 0);

        if ((watch_item_is_actively_selected != 0) && (controller_options_index == CONTROLLER_OPTIONS_INDEX_STYLE))
        {
            gdl = textRenderOutlined(gdl, &sp5C, &sp58, textptr, pFontChars, pFontFile, phi_s1, 0x7000A0, sp54 + 1, sp50, 0, 0);
        } else
        {
            gdl = textRender(gdl, &sp5C, &sp58, textptr, pFontChars, pFontFile, phi_s1, sp54, sp50, 0, 0);
        }

        gdl = draw_controller_style_text(gdl);
        textptr = langGet(getStringID(LOPTIONS, OPTION_STR_33_CONTROLLER_LF)); //controller;

        phi_s1 = 0xFF00B0;

        if (controllerCheckDualControllerTypesAllowed())
        {
            textptr = langGet(getStringID(LOPTIONS, OPTION_STR_34_CONTROLLERS_LF)); //controllers;
        }
        sp5C = XOFFSET_1;
        sp58 = 0x2B;

        if (controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS)
        {
            phi_s1 = 0xA0FFA0F0;
            if (watch_item_is_actively_selected != 0)
            {
                phi_s1 = -1;
            }
        }

        textMeasure(&sp50, &sp54, textptr, pFontChars, pFontFile, 0);

        if ((watch_item_is_actively_selected != 0) && (controller_options_index == CONTROLLER_OPTIONS_INDEX_INPUTS))
        {
            gdl = textRenderOutlined(gdl, &sp5C, &sp58, textptr, pFontChars, pFontFile, phi_s1, 0x7000A0, sp54 + 1, sp50, 0, 0);
        } else
        {
            gdl = textRender(gdl, &sp5C, &sp58, textptr, pFontChars, pFontFile, phi_s1, sp54, sp50, 0, 0);
        }

    }
    return gdl;
}


void game_option_select_value(u32 *param_1, u32 param_2)
{
    *param_1 = param_2;
    set_controlstick_lr_disabled();
    sndPlaySfx(g_musicSfxBufferPtr, OPTION_CHOOSE_SFX, NULL);
}


void game_option_toggle_input(s32 option_index)
{
    if ( (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD) || sub_GAME_7F0A4FB0()) && watch_item_is_actively_selected )
    {
        if (game_options_entries[option_index].current_value == 1)
        {
            game_option_select_value(&game_options_entries[option_index].current_value, 0);
        }
        else if (game_options_entries[option_index].current_value == 2)
        {
            game_option_select_value(&game_options_entries[option_index].current_value, 1);
        }
    }
    else
    {
        if ( (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD) || sub_GAME_7F0A4FEC()) && watch_item_is_actively_selected )
        {
            if (game_options_entries[option_index].current_value == 0)
            {
                game_option_select_value(&game_options_entries[option_index].current_value, 1);
            }
            else if ( (game_options_entries[option_index].current_value == 1) && game_options_entries[option_index].text[3] )
            {
                game_option_select_value(&game_options_entries[option_index].current_value, 2);
            }
        }
    }
}


/**
 * Address: 7F0AB908
 *
 * Set the color and draw the text for the values of the toggle options.
 * For example, draw the "ON" and "OFF" text for the Auto-Aim option,
 * but not the "AUTO-AIM" text itself.
 *
 * Options are highlighted by using the controller to advance up and down the toggle options list,
 * but options are not selected until the A button is pressed.
 */
Gfx *draw_toggle_option_values(Gfx *gdl, s32 y, s32 option_index, u32 state)
{
    s32 colour1;
    s32 colour2;
    s32 colour3;
    s32 x1;
    s32 x2;
    struct game_options *entry;
    struct game_options *drawentry;

    colour1 = 0x00800080;
    colour2 = 0x00800080;
    colour3 = 0x00800080;

    entry = &game_options_entries[option_index];

    if (j_text_trigger)
    {
        x1 = 0xAA;
        if (1);
    }
    else
    {
        x1 = 0xB4;
    }

    if (j_text_trigger)
    {
        x2 = 0xDC;
    }
    else
    {
        x2 = 0xE1;
    }

    // Option is unhighlighted
    if (state == 0)
    {
        goto state_unhighlighted;
    }
    // Option is highlighted
    else if (state == 1)
    {
        goto state_highlighted;
    }
    // Option is selected
    else if (state == 2)
    {
        goto state_selected;
    }
    goto after_state;

state_unhighlighted:
    entry = &game_options_entries[option_index];
    if (entry->current_value == 0)
    {
        colour1 = 0x00FF00B0;
    }
    else if (entry->current_value == 1)
    {
        colour2 = 0x00FF00B0;
    }
    else if (entry->current_value == 2)
    {
        colour3 = 0x00FF00B0;
    }
    goto after_state;

/**
 * Sets color of the active value of the highlighted option.
 * These use the same colors as the active values of the unhighlighted options,
 * so changing the highlighted option has no visual effect.
 */
state_highlighted:
    entry = &game_options_entries[option_index];
    if (entry->current_value == 0)
    {
        colour1 = 0x00FF00B0;
    }
    else if (entry->current_value == 1)
    {
        colour2 = 0x00FF00B0;
    }
    else if (entry->current_value == 2)
    {
        colour3 = 0x00FF00B0;
    }
    goto after_state;

/**
 * Make the active value of the selected option extra bright.
 */
state_selected:
    game_option_toggle_input(option_index);
    entry = &game_options_entries[option_index];
    if (entry->current_value == 0)
    {
        colour1 = 0xA0FFA0F0;
    }
    else if (entry->current_value == 1)
    {
        colour2 = 0xA0FFA0F0;
    }
    else if (entry->current_value == 2)
    {
        colour3 = 0xA0FFA0F0;
    }

after_state:
    if (entry->text[3] == 0)
    {
        if (j_text_trigger)
        {
            x1 = 0xBE;
        }
        else
        {
            x1 = 0xC8;
        }

        if (j_text_trigger)
        {
            // This weird code must be kept on one line for matching.
            x2 = 0xFA; } else { x2 = 0xFA; }
        }

        drawentry = entry;

        gdl = draw_options_labels(gdl, x1, y, langGet(drawentry->text[1]), colour1, 0, -1, 1, 0, 0x3000B0, 0); // Draw text of option's first value e.g. "Full" for the Screen option.
        gdl = draw_options_labels(gdl, x2, y, langGet(drawentry->text[2]), colour2, 0, -1, 1, 0, 0x3000B0, 0); // Draw text of option's second value e.g. "Wide" for the Screen option.

        if (drawentry->text[3])
        {
            gdl = draw_options_labels(gdl, 0x10E, y, langGet(drawentry->text[3]), colour3, 0, -1, 1, 0, 0x3000B0, 0); // Draw text of option's third value e.g. "Cinema" for the Screen option.
        }

    return gdl;
}


#ifndef __sgi
/* NATIVE ONLY (#44, 2026-09-18; the row itself dates from #39, 2026-09-17):
 * the SIGHTLINE page - the sixth solo-watch page (options.h,
 * WATCH_INDEX_SL_SIGHTLINE) holding the native-only settings as OFF / ON
 * rows:
 *
 *     sightline
 *     invert mouse y   off  on      (#39)
 *     sprint           off  on      (#42)
 *
 * WHAT A ROW IS NOT. Not a game_options_entries row: that table is the
 * per-folder save's option set (fileSaveSettingsForFolder, file2.c:1230), and
 * these settings must never reach the EEPROM. A row holds no value of its own
 * either - it is a VIEW onto one state that lives elsewhere and is reached by
 * its one setter: Invert Mouse Y is the platform layer's g_mouse_invert_y
 * (sl_input.c, applied once in read_mouse; the setter persists it through the
 * native settings store), Sprint is the store's sprint_enabled read every
 * tick by the movement seam (bondview2.c, sl_sprint_enabled) and written
 * through sl_sprint_enabled_set (sl_settings_apply.c). The front end's
 * OPTIONS -> SETTINGS page reads and writes the same two through the same
 * calls, so the two menus never disagree and nothing syncs anything. The
 * original "look up/down" row on Game Options is untouched and still means
 * the CONTROLLER stick's vertical convention; it never governs the mouse
 * (B-060).
 *
 * The value change mirrors game_option_toggle_input exactly - the A/Z select
 * latch (draw_watch_current_page toggles it for every page but the
 * inventory), then LEFT (C-left / L / d-pad left / the latched stick) = off
 * and RIGHT = on - and goes through game_option_select_value on a local so
 * the stick latch and the confirm sound are the rows' own. Effect is
 * immediate: read_mouse reads the one state on its next poll and the sprint
 * seam reads the store on its next tick.
 *
 * Wording follows the text table's own convention ("look up/down\n",
 * "off\n", "on\n": lowercase, LF-terminated - assets/obseg/text/LoptionE.h);
 * the off/on values are the table's own strings, only the heading and the
 * labels are native. Colours, x positions (0xBE/0xC8 by j_text_trigger,
 * 0xFA) and the highlighted / selected / outlined states are
 * draw_toggle_option_values' and draw_toggle_options'. Pointer hit-testing
 * and clicks for this page are #40's machinery (sl_watch_pointer.c), not
 * here. */
/* Toggle rows by KIND: the one state each, read and written through the
 * same call the front end uses. */
static u32 sl_sightline_toggle_get(s32 kind)
{
    if (kind == SL_WROW_TOGGLE_SPRINT)
        return sl_sprint_enabled() ? 1 : 0;
    if (SL_WROW_IS_MODE(kind))                 /* #56: 0 hold / 1 toggle */
        return sl_action_mode(kind == SL_WROW_TOGGLE_SMODE ? 1 : 0) ? 1 : 0;
    if (kind == SL_WROW_TOGGLE_VSYNC)          /* #52: the applied swap interval */
    {
        int vs = 0;
        sl_window_state(NULL, NULL, NULL, &vs);
        return vs ? 1 : 0;
    }
    return sl_mouse_invert_y_get() ? 1 : 0;
}

static void sl_sightline_toggle_set(s32 kind, u32 on)
{
    if (kind == SL_WROW_TOGGLE_SPRINT)
        sl_sprint_enabled_set(on != 0);
    else if (SL_WROW_IS_MODE(kind))
        sl_action_mode_set(kind == SL_WROW_TOGGLE_SMODE ? 1 : 0, on != 0);
    else if (kind == SL_WROW_TOGGLE_VSYNC)
        sl_window_request_vsync(on != 0);      /* #52: a request; the backend commits what it read back */
    else
        sl_mouse_invert_y_set(on != 0);
    /* The one diagnostic for the toggle rows (#56): what a cell set, beside
     * what the four states read back. */
    if (getenv("SL_INPUT_DEBUG") != NULL)
    {
        int vs = 0;
        sl_window_state(NULL, NULL, NULL, &vs);
        fprintf(stderr, "sightline watch: toggle kind=%d <- %u -> minv=%d sprint=%d smode=%d cmode=%d vsync=%d req=%d\n",
                (int) kind, on, sl_mouse_invert_y_get(), sl_sprint_enabled(),
                sl_action_mode(1), sl_action_mode(0), vs, sl_window_request_pending());
    }
}

/* The #45 value rows: ASPECT RATIO steps through the four shapes in display
 * order (4:3, 16:9, 21:9, 32:9, wrapping) and FIELD OF VIEW steps the
 * displayed 16:9-equivalent degree (60..110, clamped) - the same setters the
 * front end's DISPLAY tab calls, so the two views can never disagree. A step
 * carries the confirm sound and the stick latch of game_option_select_value
 * on a local, as the toggles do. Effect is immediate: the renderer reads the
 * store at its next frame reset (an aspect change resizes the window on the
 * next frame, the watch re-centred in it; the FOV shows the moment the watch
 * closes on the world). */
static void sl_sightline_value_step(s32 kind, s32 dir)
{
    u32 dummy = 0;

    if (dir == 0)
        return;
    game_option_select_value(&dummy, 1);
    if (kind == SL_WROW_VALUE_ASPECT)
    {
        s32 col = sl_aspect_order_of(sl_aspect_ratio()) + (dir > 0 ? 1 : -1);
        if (col < 0) col = 3;
        if (col > 3) col = 0;
        sl_aspect_ratio_set(sl_aspect_by_order(col));
    }
    else if (kind == SL_WROW_VALUE_FOV)
    {
        sl_fov_step(dir > 0 ? 1 : -1);
    }
    else if (kind == SL_WROW_VALUE_MSENS || kind == SL_WROW_VALUE_SSENS)
    {
        /* #50: one SL_MOUSE_SENS_STEP per press, clamped by the platform
         * layer; the gameplay poll reads the store, so the change is felt on
         * the first look after the watch closes. */
        sl_mouse_sens_step(kind == SL_WROW_VALUE_SSENS, dir > 0 ? 1 : -1);
    }
    else if (SL_WROW_IS_PAD_TUNE(kind))
    {
        /* #51: one grid step of the controller row, clamped by the platform
         * layer; the pad seam reads the store on its next poll. */
        sl_pad_tune_step(SL_WROW_PAD_TUNE_ID(kind), dir > 0 ? 1 : -1);
    }
    else if (kind == SL_WROW_VALUE_WMODE)
    {
        /* #52: the next mode, wrapping - a request the SDL backend applies
         * at its next frame reset (the watch stays open: the window changes
         * under it and the pointer follows the new safe rect next frame). */
        sl_window_mode_step(dir > 0 ? 1 : -1);
    }
    else if (kind == SL_WROW_VALUE_RES)
    {
        /* #52: the next size of the current mode's list. */
        sl_window_size_step(dir > 0 ? 1 : -1);
    }
    else if (kind == SL_WROW_VALUE_WDETAIL)
    {
        /* #43: the other profile (two values: a step either way is the
         * other one); the render seams read the store on the next frame
         * drawn, so the world behind the watch changes when it closes. */
        sl_world_detail_step(dir > 0 ? 1 : -1);
        if (getenv("SL_INPUT_DEBUG") != NULL)
            fprintf(stderr, "sightline watch: world detail step dir=%d -> %d(%s)\n",
                    (int) dir, sl_world_detail(), sl_world_detail_name(sl_world_detail()));
    }
    else if (kind == SL_WROW_VALUE_TEXTURES)
    {
        /* #47: the next set, wrapping; the renderer's provider reads the
         * store at each texture's next resolve, so the world behind the
         * watch changes when it closes. */
        sl_textures_step(dir > 0 ? 1 : -1);
        if (getenv("SL_INPUT_DEBUG") != NULL)
            fprintf(stderr, "sightline watch: textures step dir=%d -> %d(%s)\n",
                    (int) dir, sl_textures(), sl_textures_name(sl_textures()));
    }
    else if (SL_WROW_IS_NAMED(kind))
    {
        /* #63: BUTTON LAYOUT steps through the presets (a step off CUSTOM
         * lands on a preset and re-seeds the pad slots), STICK LAYOUT
         * through the four; both wrap, both live on the next poll. */
        sl_watch_layout_step(kind == SL_WROW_VALUE_BLAYOUT ? 0 : 1, dir > 0 ? 1 : -1);
    }
    if ((kind == SL_WROW_VALUE_WMODE || kind == SL_WROW_VALUE_RES) && getenv("SL_INPUT_DEBUG") != NULL)
    {
        int m = 0, w = 0, h = 0, v = 0;
        sl_window_state(&m, &w, &h, &v);
        fprintf(stderr, "sightline watch: display step kind=%d dir=%d -> mode=%s %dx%d vsync=%d req=%d\n",
                (int) kind, (int) dir, sl_window_mode_name(sl_window_mode_shown()), w, h, v, sl_window_request_pending());
    }
}

/* One press on a row of the view in force - the latched keyboard / pad path
 * and the pointer (sl_watch_pointer.c) both come here. value < 0 is the
 * LABEL: a sub-menu opens, BACK returns, BINDINGS opens the #46 child, a
 * toggle / value row just beeps (its latch is the row highlight). value >= 0
 * is a CELL: a toggle's off (0) / on (1); a value row's `-` (0), the value
 * (1, steps up) or `+` (2). */
void sl_sightline_click(s32 row, s32 value)
{
    s32 kind = sl_sightline_row_kind(row);
    const struct sl_watch_row *r;

    if (row < 0 || row >= sl_sightline_row_count())
        return;
    r = &sl_watch_views[sl_wview].rows[row];
    switch (kind)
    {
    case SL_WROW_SUBMENU:
        if (value < 0) sl_sightline_open_view((s32) r->arg);
        break;
    case SL_WROW_BACK:
        if (value < 0) sl_sightline_back();
        break;
    case SL_WROW_BINDINGS:
        if (value < 0) sl_watch_bindings_open();
        break;
    case SL_WROW_TOGGLE_MINV:
    case SL_WROW_TOGGLE_SPRINT:
    case SL_WROW_TOGGLE_SMODE:
    case SL_WROW_TOGGLE_CMODE:
    case SL_WROW_TOGGLE_VSYNC:
        if (value < 0) { watch_play_beep_sound(); break; }
        if (value <= 1 && (s32) sl_sightline_toggle_get(kind) != value)
        {
            u32 v = sl_sightline_toggle_get(kind);
            game_option_select_value(&v, (u32) value);
            sl_sightline_toggle_set(kind, (u32) value);
        }
        break;
    case SL_WROW_VALUE_ASPECT:
    case SL_WROW_VALUE_FOV:
    case SL_WROW_VALUE_MSENS:
    case SL_WROW_VALUE_SSENS:
    case SL_WROW_VALUE_BLAYOUT:
    case SL_WROW_VALUE_SLAYOUT:
    case SL_WROW_VALUE_PSENS:
    case SL_WROW_VALUE_PDZ:
    case SL_WROW_VALUE_MDZ:
    case SL_WROW_VALUE_WMODE:
    case SL_WROW_VALUE_RES:
    case SL_WROW_VALUE_WDETAIL:
    case SL_WROW_VALUE_TEXTURES:
        if (value < 0) { watch_play_beep_sound(); break; }
        sl_sightline_value_step(kind, value == 0 ? -1 : +1);
        break;
    default:
        break;
    }
}

/* The slider rows' fill, 0..1 - the value's place in its range, from the
 * platform layer (the same read the front end's bars make). */
static f32 sl_sightline_row_fill(s32 kind)
{
    if (kind == SL_WROW_VALUE_FOV)
        return sl_fov_fraction();
    if (SL_WROW_IS_PAD_TUNE(kind))
        return sl_pad_tune_fraction(SL_WROW_PAD_TUNE_ID(kind));
    return sl_mouse_sens_fraction(kind == SL_WROW_VALUE_SSENS);
}

/* A slider row's value as printed: the FOV row's degrees, the percent rows'
 * percent (mouse, or the #51 controller rows). */
static s32 sl_sightline_slider_value(s32 kind)
{
    if (kind == SL_WROW_VALUE_FOV)
        return sl_fov_h16_displayed();
    if (SL_WROW_IS_PAD_TUNE(kind))
        return sl_pad_tune_get(SL_WROW_PAD_TUNE_ID(kind));
    return sl_mouse_sens_get(kind == SL_WROW_VALUE_SSENS);
}

/* A click or a drag on a slider row's bar (sl_watch_pointer.c): the value at
 * that fraction, on the value's own grid; the confirm sound and stick latch of
 * a step only when the value actually changes, so a drag that stays on one
 * grid point is silent, as the MUSIC / FX tracks are. */
void sl_sightline_slide(s32 row, f32 t)
{
    s32 kind = sl_sightline_row_kind(row);
    s32 before, after;
    u32 dummy = 0;

    if (!SL_WROW_IS_SLIDER(kind))
        return;
    before = sl_sightline_slider_value(kind);
    if (kind == SL_WROW_VALUE_FOV)
        sl_fov_set_fraction(t);
    else if (SL_WROW_IS_PAD_TUNE(kind))
        sl_pad_tune_set_fraction(SL_WROW_PAD_TUNE_ID(kind), t);
    else
        sl_mouse_sens_set_fraction(kind == SL_WROW_VALUE_SSENS, t);
    after = sl_sightline_slider_value(kind);
    if (after != before)
        game_option_select_value(&dummy, 1);
}

/* The label y of a row of the view in force: SL_SIGHTLINE_ROW_Y0, YINC per
 * row, plus SL_SIGHTLINE_SLIDER_EXTRA for every slider row at or above it
 * (the bar's line sits above its own label). */
s32 sl_sightline_row_y(s32 row)
{
    s32 y = SL_SIGHTLINE_ROW_Y0 + row * YINC;
    s32 i;
    for (i = 0; i <= row && i < sl_sightline_row_count(); i++)
        if (SL_WROW_IS_SLIDER(sl_sightline_row_kind(i)))
            y += SL_SIGHTLINE_SLIDER_EXTRA;
    return y;
}

/* The latched row's LEFT / RIGHT (the sl_sightline_row_input shape of #44:
 * LEFT = off / `-`, RIGHT = on / `+`), on the frame's buttons. */
static void sl_sightline_latched_input(s32 row)
{
    s32 kind = sl_sightline_row_kind(row);
    if (!watch_item_is_actively_selected)
        return;
    if (joyGetButtonsPressedThisFrame(PLAYER_1, L_CBUTTONS|L_TRIG|L_JPAD) || sub_GAME_7F0A4FB0())
        sl_sightline_click(row, 0);
    else if (joyGetButtonsPressedThisFrame(PLAYER_1, R_CBUTTONS|R_TRIG|R_JPAD) || sub_GAME_7F0A4FEC())
        sl_sightline_click(row, SL_WROW_IS_VALUE(kind) ? 2 : 1);
}

static Gfx *sl_draw_sightline_label(Gfx *gdl, s32 y, char *label, s32 highlighted, s32 selected, s32 dimmed)
{
    if (dimmed)
        return draw_options_labels(gdl, XOFFSET_1, y, label, 0x00500060, 0, -1, 0, 0, 0x3000B0, 0);
    if (selected)
        return draw_options_labels(gdl, XOFFSET_1, y, label, -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0);
    if (highlighted)
        return draw_options_labels(gdl, XOFFSET_1, y, label, 0xA0FFA0F0, 0, -1, 0, 0, 0x3000B0, 0);
    return draw_options_labels(gdl, XOFFSET_1, y, label, 0xFF00B0, 0, -1, 0, 0, 0x3000B0, 0);
}

/* A toggle row: label, then off / on at the toggles' columns (0xBE/0xC8 by
 * j_text_trigger, 0xFA), the current one lit, the latched one active. The
 * #56 mode rows print the text table's hold / toggle instead (the aim
 * control row's own two strings) at the same columns. */
static Gfx *sl_draw_sightline_toggle(Gfx *gdl, s32 row, s32 y, char *label)
{
    s32 kind = sl_sightline_row_kind(row);
    s32 highlighted = (s32) sl_sightline_row_index == row;
    s32 selected = highlighted && watch_item_is_actively_selected;
    u32 colour_off = 0x00800080;
    u32 colour_on  = 0x00800080;
    u32 active = selected ? 0xA0FFA0F0 : 0x00FF00B0;
    s32 x1 = j_text_trigger ? 0xBE : 0xC8;
    s32 x2 = 0xFA;
    /* The two cells' string ids, chosen BEFORE getStringID: that macro
     * (bondconstants.h) does not parenthesise its slot argument, so a
     * conditional written inside it binds to the bank's addition. */
    s32 id_lo = SL_WROW_IS_MODE(kind) ? OPTION_STR_1E_HOLD_LF   : OPTION_STR_1A_OFF_LF;
    s32 id_hi = SL_WROW_IS_MODE(kind) ? OPTION_STR_1D_TOGGLE_LF : OPTION_STR_19_ON_LF;

    if (selected)
        sl_sightline_latched_input(row);
    if (sl_sightline_toggle_get(kind))
        colour_on = active;
    else
        colour_off = active;
    gdl = sl_draw_sightline_label(gdl, y, label, highlighted, selected, 0);
    gdl = draw_options_labels(gdl, x1, y, langGet(getStringID(LOPTIONS, id_lo)), colour_off, 0, -1, 1, 0, 0x3000B0, 0);
    gdl = draw_options_labels(gdl, x2, y, langGet(getStringID(LOPTIONS, id_hi)), colour_on,  0, -1, 1, 0, 0x3000B0, 0);
    return gdl;
}

/* A value row (#45): label, then `-` value `+`. ASPECT RATIO (four discrete
 * shapes) - the range rows are sliders since the #50 owner request. */
static Gfx *sl_draw_sightline_value(Gfx *gdl, s32 row, s32 y, char *label)
{
    static char minus[] = "-\n";
    static char plus[]  = "+\n";
    static char value[12];
    s32 highlighted = (s32) sl_sightline_row_index == row;
    s32 selected = highlighted && watch_item_is_actively_selected;
    u32 active = selected ? 0xA0FFA0F0 : 0x00FF00B0;
    const char *n = sl_aspect_name(sl_aspect_ratio());
    s32 i = 0;

    if (selected)
        sl_sightline_latched_input(row);

    while (n[i] != '\0' && i < 10) { value[i] = n[i]; i++; }
    value[i++] = '\n'; value[i] = '\0';
    gdl = sl_draw_sightline_label(gdl, y, label, highlighted, selected, 0);
    gdl = draw_options_labels(gdl, SL_SIGHTLINE_X_MINUS, y, minus, 0x00800080, 0, -1, 1, 0, 0x3000B0, 0);
    gdl = draw_options_labels(gdl, SL_SIGHTLINE_X_VALUE, y, value, active,     0, -1, 1, 0, 0x3000B0, 0);
    gdl = draw_options_labels(gdl, SL_SIGHTLINE_X_PLUS,  y, plus,  0x00800080, 0, -1, 1, 0, 0x3000B0, 0);
    return gdl;
}

/* The NAMED value rows (#63, 2026-09-20): BUTTON LAYOUT and STICK LAYOUT -
 * the label, then the layout's name right-aligned at SL_SIGHTLINE_X_BARVALUE
 * (the slider rows' value column: "legacy southpaw" is fifteen glyphs and
 * the centred value column between `-` and `+` holds eight). The latched
 * LEFT / RIGHT step, the pointer's click on the name steps up. The names
 * come from the native layer (sl_watch_layout_name), lowercased here as the
 * family name is - the watch font draws capitals from either. */
const char *sl_sightline_row_value_name(s32 row)
{
    static char res[16];
    s32 kind = sl_sightline_row_kind(row);
    if (kind == SL_WROW_VALUE_WMODE)           /* #52 */
        return sl_window_mode_name(sl_window_mode_shown());
    if (kind == SL_WROW_VALUE_WDETAIL)         /* #43: ORIGINAL / ENHANCED */
        return sl_world_detail_name(sl_world_detail());
    if (kind == SL_WROW_VALUE_TEXTURES)        /* #47: ORIGINAL / COMMUNITY HD / XBLA */
        return sl_textures_name(sl_textures());
    if (kind == SL_WROW_VALUE_RES || kind == SL_WROW_INFO_RES)
    {
        int w = 0, h = 0;
        sl_window_size_shown(&w, &h);
        sl_window_size_text(w, h, res, 12);
        return res;
    }
    if (!SL_WROW_IS_NAMED(kind)) return "";
    return sl_watch_layout_name(kind == SL_WROW_VALUE_BLAYOUT ? 0 : 1);
}

static Gfx *sl_draw_sightline_named(Gfx *gdl, s32 row, s32 y, char *label)
{
    static char value[20];
    s32 highlighted = (s32) sl_sightline_row_index == row;
    s32 selected = highlighted && watch_item_is_actively_selected;
    u32 active = selected ? 0xA0FFA0F0 : 0x00FF00B0;
    const char *n = sl_sightline_row_value_name(row);
    s32 i = 0;

    if (selected)
        sl_sightline_latched_input(row);
    while (n[i] != '\0' && i < 17) { value[i] = (char) ((n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i]); i++; }
    value[i++] = '\n'; value[i] = '\0';
    gdl = sl_draw_sightline_label(gdl, y, label, highlighted, selected, 0);
    gdl = draw_options_labels(gdl, SL_SIGHTLINE_X_BARVALUE, y, value, active, 0, -1, 0, 0, 0x3000B0, 1);
    return gdl;
}

/* The informational CONTROLLER row (#63): the label, and the family SDL
 * classified the driving pad as (xbox / playstation / generic / none) at the
 * value column. Never highlighted, never latched - the cursor skips it - and
 * no manual choice: the family is what the device reports. */
static Gfx *sl_draw_sightline_info(Gfx *gdl, s32 y, char *label, const char *n)
{
    static char value[16];
    s32 i = 0;

    while (n[i] != '\0' && i < 14) { value[i] = (char) ((n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i]); i++; }
    value[i++] = '\n'; value[i] = '\0';
    gdl = sl_draw_sightline_label(gdl, y, label, 0, 0, 0);
    gdl = draw_options_labels(gdl, SL_SIGHTLINE_X_BARVALUE, y, value, 0x00800080, 0, -1, 0, 0, 0x3000B0, 1);
    return gdl;
}

/* A slider row's TEXT (#50 owner request): the label under the bar, the
 * value right-aligned to the bar's right edge on the same line (FIELD OF
 * VIEW its degrees, the sensitivities their percent) - the MUSIC / FX rows'
 * label, plus the number (the bar alone would not say 40% from 50%). No
 * `-` / `+` cells: the
 * latched LEFT / RIGHT step as before, the pointer sets on the bar. The bar
 * itself is drawn on the face before the 2D switch (sl_draw_sightline_bar). */
static Gfx *sl_draw_sightline_slider(Gfx *gdl, s32 row, s32 y, char *label)
{
    static char value[8];
    s32 kind = sl_sightline_row_kind(row);
    s32 highlighted = (s32) sl_sightline_row_index == row;
    s32 selected = highlighted && watch_item_is_actively_selected;
    u32 active = selected ? 0xA0FFA0F0 : 0x00FF00B0;
    s32 pct = kind == SL_WROW_VALUE_FOV ? 0 : 1;
    s32 h = sl_sightline_slider_value(kind);
    s32 i = 0;

    if (selected)
        sl_sightline_latched_input(row);
    if (h >= 100) value[i++] = (char) ('0' + (h / 100) % 10);
    value[i++] = (char) ('0' + (h / 10) % 10);
    value[i++] = (char) ('0' + h % 10);
    if (pct) value[i++] = '%';
    value[i++] = '\n'; value[i] = '\0';
    gdl = sl_draw_sightline_label(gdl, y, label, highlighted, selected, 0);
    gdl = draw_options_labels(gdl, SL_SIGHTLINE_X_BARVALUE, y, value, active, 0, -1, 0, 0, 0x3000B0, 1);
    return gdl;
}

/* A slider row's BAR: draw_music_volume_slider's three 600x20 rectangles on
 * the face (setup_watch_rectangles at the tracks' x, this row's z), filled
 * to the value by update_volume_slider_verts with its 30-unit transition
 * band - the same verts, the same colours, the same render mode; only the z
 * and the fill differ. Drawn before microcode_constructor, as the Game
 * Options page draws its two. z is the face row under the bar's framebuffer
 * line: the label's y less SL_SIGHTLINE_BAR_ABOVE, through the page's own
 * projection (sl_watch_face_z_at_fb_y), so the bar follows the label under
 * any zoom or viewport as the projected hit-test does. */
static Gfx *sl_draw_sightline_bar(Gfx *gdl, Mtx *pagemtx, s32 y, f32 fill)
{
    struct WatchVertex *vtx1;
    struct WatchVertex *vtx;
    Gfx *cmd;
    s32 z = (s32) sl_watch_face_z_at_fb_y(pagemtx, (f32) (y - SL_SIGHTLINE_BAR_ABOVE));

    vtx1 = (struct WatchVertex *) dynAllocateVertices(12);

    cmd = gdl++;
    gDPSetRenderMode(cmd, G_RM_XLU_SURF, G_RM_XLU_SURF2);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx1));
    vtx = setup_watch_rectangles(vtx1, 0, 0, SL_SIGHTLINE_BAR_W, SL_SIGHTLINE_BAR_H, SL_SIGHTLINE_BAR_X0, z);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    vtx = setup_watch_rectangles(vtx, 0, 0, SL_SIGHTLINE_BAR_W, SL_SIGHTLINE_BAR_H, SL_SIGHTLINE_BAR_X0, z);

    gdl = sub_GAME_7F0A3B40(gdl, OS_K0_TO_PHYSICAL(vtx));
    setup_watch_rectangles(vtx, 0, 0, SL_SIGHTLINE_BAR_W, SL_SIGHTLINE_BAR_H, SL_SIGHTLINE_BAR_X0, z);

    update_volume_slider_verts(vtx1, fill, 30);

    return gdl;
}

/* An action row (a sub-menu, BACK, BINDINGS): the A/Z latch on it (which
 * draw_watch_current_page has just toggled for this frame) IS the press and
 * is dropped, so what opens starts unlatched. A DIMMED row only draws. */
static Gfx *sl_draw_sightline_action(Gfx *gdl, s32 row, s32 y, char *label)
{
    s32 kind = sl_sightline_row_kind(row);
    s32 highlighted = (s32) sl_sightline_row_index == row;

    if (kind == SL_WROW_DIMMED)
        return sl_draw_sightline_label(gdl, y, label, 0, 0, 1);
    if (highlighted && watch_item_is_actively_selected)
    {
        reset_watch_item_is_actively_selected();
        sl_sightline_click(row, -1);
        return gdl;
    }
    return sl_draw_sightline_label(gdl, y, label, highlighted, 0, 0);
}

/* The page, the shape of draw_watch_game_options_page: the shared background
 * and bar, then the text only once the page zoom has settled - the view in
 * force's heading and rows. With the BINDINGS child open (#46) the child
 * draws in place of the rows; the background, the six-segment bar and the
 * pointer are the page's as ever. */
Gfx *sl_draw_watch_sightline_page(Gfx *gdl, Mtx *param_2)
{
    const struct sl_watch_view *v = &sl_watch_views[sl_wview];
    s32 i;

    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        /* The slider rows' bars first, on the face, as the Game Options page
         * draws its two tracks before the 2D switch (#50). */
        if (!sl_watch_bindings_is_open())
        {
            for (i = 0; i < (s32) v->count; i++)
                if (SL_WROW_IS_SLIDER(v->rows[i].kind))
                    gdl = sl_draw_sightline_bar(gdl, param_2, sl_sightline_row_y(i), sl_sightline_row_fill(v->rows[i].kind));
        }
        gdl = microcode_constructor(gdl);
        if (sl_watch_bindings_is_open())
            return sl_watch_bindings_draw(gdl);
        gdl = draw_options_labels(gdl, XOFFSET_1, SL_SIGHTLINE_HEADING_Y, (char *) v->heading, 0xFF00B0, 0, -1, 0, 0, 0x3000B0, 0);
        for (i = 0; i < (s32) v->count; i++)
        {
            s32 y = sl_sightline_row_y(i);
            char *label = (char *) v->rows[i].label;
            /* the kind as REPORTED (#52: RESOLUTION reads informational in
             * BORDERLESS), not the table's */
            switch (sl_sightline_row_kind(i))
            {
            case SL_WROW_INFO_RES:
                gdl = sl_draw_sightline_info(gdl, y, label, sl_sightline_row_value_name(i));
                break;
            case SL_WROW_TOGGLE_MINV:
            case SL_WROW_TOGGLE_SPRINT:
            case SL_WROW_TOGGLE_SMODE:
            case SL_WROW_TOGGLE_CMODE:
            case SL_WROW_TOGGLE_VSYNC:
                gdl = sl_draw_sightline_toggle(gdl, i, y, label);
                break;
            case SL_WROW_VALUE_FOV:
            case SL_WROW_VALUE_MSENS:
            case SL_WROW_VALUE_SSENS:
            case SL_WROW_VALUE_PSENS:
            case SL_WROW_VALUE_PDZ:
            case SL_WROW_VALUE_MDZ:
                gdl = sl_draw_sightline_slider(gdl, i, y, label);
                break;
            case SL_WROW_VALUE_ASPECT:
                gdl = sl_draw_sightline_value(gdl, i, y, label);
                break;
            case SL_WROW_VALUE_BLAYOUT:
            case SL_WROW_VALUE_SLAYOUT:
            case SL_WROW_VALUE_WMODE:
            case SL_WROW_VALUE_RES:
            case SL_WROW_VALUE_WDETAIL:
            case SL_WROW_VALUE_TEXTURES:
                gdl = sl_draw_sightline_named(gdl, i, y, label);
                break;
            case SL_WROW_INFO_PAD:
                gdl = sl_draw_sightline_info(gdl, y, label, sl_input_pad_family_name(sl_input_pad_family()));
                break;
            default:
                gdl = sl_draw_sightline_action(gdl, i, y, label);
                break;
            }
        }
    }

    return gdl;
}
#endif
Gfx *draw_toggle_options(Gfx *gdl)
{
    s32 y_offset;
    s32 i;

    gdl = microcode_constructor(gdl);

    for (i = 0, y_offset = YOFFSET_1; i < 8; i = i + 1, y_offset = y_offset + YINC) {

        if ( i == game_options_index - 2)
        {
            // Draw option that is highlighted and selected, if there is one.
            if (watch_item_is_actively_selected)
            {
                gdl = draw_toggle_option_values(draw_options_labels(gdl, XOFFSET_1, y_offset, langGet(game_options_entries[i].text[0]), -1, 1, 0x7000A0, 0, 0, 0x3000B0, 0), y_offset, i, 2);
            }
            // Draw option that is highlighted but not selected, if there is one.
            else
            {
                gdl = draw_toggle_option_values(draw_options_labels(gdl, XOFFSET_1, y_offset, langGet(game_options_entries[i].text[0]), 0xA0FFA0F0, 0, -1, 0, 0, 0x3000B0, 0), y_offset, i, 1);
            }
        }
        // Draw the options that are neither highlighted nor selected.
        else
        {
            gdl = draw_toggle_option_values(draw_options_labels(gdl, XOFFSET_1, y_offset, langGet(game_options_entries[i].text[0]), 0xFF00B0, 0, -1, 0, 0, 0x3000B0, 0), y_offset, i, 0);
        }

    }

    return gdl;
}


Gfx *draw_watch_game_options_page(Gfx *gdl, Mtx *param_2) {
    s32 sp5C;
    u16 *textptr;
    s32 sp54;
    s32 sp50;
    s32 sp4C;
    s32 sp48;

    s32 pFontFile;
    s32 pFontChars;

    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        gdl = draw_music_volume_slider(gdl);
        gdl = draw_fx_volume_slider(gdl);
        pFontFile = ptrFontBankGothic;
        pFontChars = ptrFontBankGothicChars;
        gdl = microcode_constructor(gdl);

        textptr = langGet(getStringID(LOPTIONS, OPTION_STR_35_MUSIC_LF)); //music

        sp54 = XOFFSET_1;
        sp50 = YOFFSET_8;

        sp5C = 0xFF00B0;

        if (game_options_index == 0)
        {
            sp5C = 0xA0FFA0F0;
            if (watch_item_is_actively_selected != 0)
            {
                sp5C = -1;
            }
        }

        textMeasure(&sp48, &sp4C, textptr, pFontChars, pFontFile, 0);

        if ((watch_item_is_actively_selected != 0) && (game_options_index == 0))
        {
            gdl = textRenderOutlined(gdl, &sp54, &sp50, textptr, pFontChars, pFontFile, sp5C, 0x7000A0, sp4C + 1, sp48, 0, 0);
        }
        else
        {
            gdl = textRender(gdl, &sp54, &sp50, textptr, pFontChars, pFontFile, sp5C, sp4C, sp48, 0, 0);
        }

        sp5C = 0xFF00B0;
        textptr = langGet(getStringID(LOPTIONS, OPTION_STR_36_FX_LF)); //fx

        sp54 = XOFFSET_1;
        sp50 = YOFFSET_9;


        if (game_options_index == 1)
        {
            sp5C = 0xA0FFA0F0;
            if (watch_item_is_actively_selected != 0)
            {
                sp5C = -1;
            }
        }

        textMeasure(&sp48, &sp4C, textptr, pFontChars, pFontFile, 0);

        if ((watch_item_is_actively_selected != 0) && (game_options_index == 1))
        {
            gdl = textRenderOutlined(gdl, &sp54, &sp50, textptr, pFontChars, pFontFile, sp5C, 0x7000A0, sp4C + 1, sp48, 0, 0);
        }
        else
        {
            gdl = textRender(gdl, &sp54, &sp50, textptr, pFontChars, pFontFile, sp5C, sp4C, sp48, 0, 0);
        }

        gdl = draw_toggle_options(gdl);
    }

    return gdl;
}


int sub_GAME_7F0AC0E8(u8 *arg) {
    u8 cVar1;
    int count;

    cVar1 = *arg;
    count = 0;

    while (cVar1) {

        if (cVar1 == 0xA) {
            count = count + 1;
        }

        cVar1 = arg[1];
        arg += 1;
    }

    return count;
}


u8 *sub_GAME_7F0AC120(u8 *arg)
{
    u8 *ret;

    ret = arg;

    while (*arg != 0)
    {
        if (*arg == 0xa)
        {
            if (*++arg != 0)
            {
                ret = arg;
            }
        }
#if defined(VERSION_EU)
        else
        {
            arg++;
        }
#else
            arg++;
#endif
    }

    return ret;
}


#if defined(VERSION_EU)
//D:800577C0
const char D_800577C0[] = "\n";
#endif


Gfx *draw_watch_mission_briefing_page(Gfx *gdl, Mtx *param_2)
{
    gdl = draw_background_health_and_armor(gdl, param_2, 0);

    if (check_watch_page_transistion_running() != 1)
    {
        /**
         * spDAC, spDA4, spD68 are unused.
         * Maybe vestigial tables for formatting the briefings.
         */
        s32 spDAC[15] = {0x34, 0x2f, 0x2d, 0x2a, 0x28, 0x25, 0x25, 0x28, 0x2a, 0x2d, 0x2f, 0x34, 0x37, 0x40, -1};
        s32 spDA4[2] = {0x4b, -1};
        s32 spD68[15] = {0x10e, 0x113, 0x116, 0x119, 0x11a, 0x11b, 0x11b, 0x11a, 0x119, 0x116, 0x113, 0x10e, 0x108, 0xfe, -1};

        s32 boxLeft;
        s32 boxTop;
        s32 boxRight;
        s32 boxBottom;
        s32 textHeight = 0;
        s32 textWidth = 0;

        struct font *font = ptrFontBankGothic;
        struct fontchar *chars = ptrFontBankGothicChars;

#if defined(VERSION_EU)
            char wrappedText[3000];
#else
            char wrappedText[3000] = "\n";
#endif

        char pageTitle[0x20];
        char *completeText;
        char *incompleteText;
        char *failedText;
        char *titleText;

        s32 lineCount;
        s32 objectiveRow;
        s32 objY;
        s32 objX;

        completeText = langGet(0xac28);
        incompleteText = langGet(0xac29);
        failedText = langGet(0xac37);
        titleText = get_ptr_text_for_watch_breifing_page(BRIEFING_TITLE);

#if defined(VERSION_EU)
        strcpy(wrappedText, D_800577C0);
#endif

        gdl = microcode_constructor(gdl);
        textMeasure(&textHeight, &textWidth, titleText, chars, font, 0);

        boxLeft = ((0xaa - textWidth) / 2) + 0x4b;
        boxTop = 0x1e;
        boxRight = boxLeft + textWidth;
        boxBottom = boxTop + textHeight;

        gdl = draw_blackbox_to_screen(gdl, &boxLeft, &boxTop, &boxRight, &boxBottom);
        gdl = textRender(gdl, &boxLeft, &boxTop, titleText, chars, font, 0xa0ffa0f0, textWidth, textHeight, 0, 0);

        boxLeft = 0x41;

        switch (mission_brief_index)
        {
            case BRIEF_INDEX_BACKGROUND:
                sprintf(pageTitle, langGet(0xac38));
                textWrap(0xd2, get_ptr_text_for_watch_breifing_page(BRIEFING_OVERVIEW), wrappedText, chars, font);
                mission_brief_background_navigation();
                break;

            case BRIEF_INDEX_M:
                sprintf(pageTitle, langGet(0xac39));
                textWrap(0xd2, get_ptr_text_for_watch_breifing_page(BRIEFING_M), wrappedText, chars, font);
                mission_brief_m_briefing_navigation();
                break;

            case BRIEF_INDEX_Q:
                sprintf(pageTitle, langGet(0xac3a));
                textWrap(0xd2, get_ptr_text_for_watch_breifing_page(BRIEFING_Q), wrappedText, chars, font);
                mission_brief_q_branch_navigation();
                break;

            case BRIEF_INDEX_MONEYPENNY:
                sprintf(pageTitle, langGet(0xac3b));
                textWrap(0xd2, get_ptr_text_for_watch_breifing_page(BRIEFING_MONEYPENNY), wrappedText, chars, font);
                mission_brief_moneypenny_navigation();
                break;

            case BRIEF_INDEX_OBJECTIVES:
            {
#if defined(VERSION_EU)
                char objectiveBuffer[200];
#else
                char objectiveBuffer[200] = "";
#endif
                u32 colour;
                s32 i;
                s32 j;
                char *objectiveText;
                s32 visibleObjectiveIndex;
                s32 pad;
                char *objectiveLetterPtr;

                lineCount = 1;
                objectiveRow = 0;
                visibleObjectiveIndex = 0;

                setTextOverlapCorrection((j_text_trigger) ? (1) : (5));
                sprintf(pageTitle, langGet(0xac3c));

                for (i = 0; i < objectiveGetCount(); i++)
                {
                    if (get_difficulty_for_objective(i) <= lvlGetSelectedDifficulty())
                    {
                        objectiveText = get_text_for_objective(i);
                        objectiveBuffer[0] = '\0';

                        for (j = 0; j < lineCount; j++)
                        {
                            strcat(objectiveBuffer, D_80058440);
                        }

                        for (j = 0; j < objectiveRow; j++)
                        {
                            strcat(objectiveBuffer, D_80058444);
                        }

                        objectiveLetterPtr = objectiveBuffer + strlen(objectiveBuffer);

                        sprintf(objectiveLetterPtr, aC_2, visibleObjectiveIndex + 'a');
                        strcat(objectiveBuffer, objectiveText);

                        objY = boxTop + ((j_text_trigger) ? (1) : (5));
                        objX = 0x3c;

                        gdl = textRender(gdl, &objX, &objY, objectiveBuffer, chars, font, 0x00ff00b0, viGetX(), viGetY(), 0, 10);
                        lineCount += sub_GAME_7F0AC0E8(objectiveLetterPtr);
                        textMeasure(&textHeight, &textWidth, sub_GAME_7F0AC120(objectiveLetterPtr), chars, font, 10);

                        if ((textWidth + 0x3c) < (viGetX() / 2))
                        {
                            lineCount--;
                        }

                        objectiveBuffer[0] = '\0';

                        for (j = 0; j < lineCount; j++)
                        {
                            strcat(objectiveBuffer, D_80058450);
                        }
                        for (j = 0; j < objectiveRow; j++)
                        {
                            strcat(objectiveBuffer, D_80058454);
                        }

                        switch (get_status_of_objective(i))
                        {
                            case OBJECTIVESTATUS_INCOMPLETE:
                                strcat(objectiveBuffer, incompleteText);
                                colour = (D_80040AFC << 16) | 0x400040ff;
                                break;

                            case OBJECTIVESTATUS_COMPLETE:
                                strcat(objectiveBuffer, completeText);
                                colour = 0xa0ffa0f0;
                                break;

                            case OBJECTIVESTATUS_FAILED:
                                strcat(objectiveBuffer, failedText);

                                if (j_text_trigger)
                                {
                                    colour = 0xa0ffa0f0;
                                }
                                else
                                {
                                    colour = 0x00ff00b0;
                                }
                                break;

                            default:
                                break;
                        }

                        textMeasure(&textHeight, &textWidth, objectiveBuffer, chars, font, 10);
                        objY = boxTop + ((j_text_trigger) ? (1) : (5));

                        if (j_text_trigger)
                        {
                            objX = 0xf5 - textWidth;
                        }
                        else
                        {
                            objX = 0xaf;
                        }

                        gdl = textRender(gdl, &objX, &objY, objectiveBuffer, chars, font, colour, 0xd2, viGetY(), 0, 10);

                        objectiveRow++;
                        visibleObjectiveIndex++;
                    }
                }

                setTextOverlapCorrection(-1);
                mission_brief_objectives_navigation();

                break;

            }
        }

        textMeasure(&textHeight, &textWidth, pageTitle, chars, font, 10);
        gdl = textRender(gdl, &boxLeft, &boxTop, pageTitle, chars, font, 0xa0ffa0f0, 0xd2, 0x82, 0, 10);

        boxTop += 5;
        boxLeft = 0x3c;

        textMeasure(&textHeight, &textWidth, wrappedText, chars, font, 10);
        gdl = textRender(gdl, &boxLeft, &boxTop, wrappedText, chars, font, 0x00ff00b0, viGetX(), viGetY(), 0, 10);
    }

    return gdl;
}


//D:80058440
const char D_80058440[] = " \n";
//D:80058444
const char D_80058444[] = " \n\n";
//D:80058448
const char aC_2[] = "%c: ";
//D:80058450
const char D_80058450[] = " \n";
//D:80058454
const char D_80058454[] = " \n\n";


/**
 * Address: 7F0ACA28
 */
Gfx *draw_watch_current_page(Gfx *gdl, Mtx *arg1, s32 watch_transitioning)
{
#ifndef __sgi
    /* NATIVE (#44): the drawn bar is the six-segment one built and coloured
     * per frame in draw_background_health_and_armor; the player's five-slot
     * vertex buffer is not drawn, and colouring it for page index 5 would
     * write past its 20 vertices into the static bar's. */
#else
    set_page_rectangle_colors(watch_screen_index, (struct WatchVertex *)g_CurrentPlayer->buffer_for_watch_greenbackdrop_vertices);
#endif

    if (watch_transitioning == TRUE)
    {
        set_BONDdata_outside_watch_menu_flag(FALSE);
        sub_GAME_7F0BD8FC(0);
#ifndef __sgi
        /* NATIVE (#40): the pointer's hover and clicks for this frame, applied
         * BEFORE the page reads the pad so a click lands in the same frame's
         * state the page then draws. */
        sl_watch_pointer_frame(arg1);
#endif

        // Handle A or Z button click when in any page but inventory page
        if ((watch_screen_index != WATCH_INDEX_INVENTORY) && (joyGetButtonsPressedThisFrame(PLAYER_1, Z_TRIG|A_BUTTON)))
        {
            watch_play_beep_sound();
        }

        switch (watch_screen_index)
        {
            case WATCH_INDEX_MISSION_STATUS:
                gdl = draw_watch_mission_status_page(gdl, arg1);
                break;
            case WATCH_INDEX_INVENTORY:
                gdl = draw_watch_inventory_page(gdl, arg1);
                break;
            case WATCH_INDEX_CONTROL_OPTIONS:
                gdl = draw_watch_control_options_page(gdl, arg1);
                break;
            case WATCH_INDEX_GAME_OPTIONS:
                gdl = draw_watch_game_options_page(gdl, arg1);
                break;
            case WATCH_INDEX_MISSION_BRIEFING:
                gdl = draw_watch_mission_briefing_page(gdl, arg1);
#ifndef __sgi
                break;
            case WATCH_INDEX_SL_SIGHTLINE:               /* #44, native page */
                gdl = sl_draw_watch_sightline_page(gdl, arg1);
#endif
        }
#ifndef __sgi
        /* NATIVE (#40): the pointer's crosshair, on top of the page. */
        gdl = sl_watch_pointer_draw(gdl);
#endif
    }
    else if (watch_transitioning == FALSE)
    {
        sub_GAME_7F0BD8FC(1);
        set_BONDdata_outside_watch_menu_flag(TRUE);
        gdl = draw_background_health_and_armor_transitioning(gdl, arg1);
    }

    return gdl;
}
