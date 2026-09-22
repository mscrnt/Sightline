/**
 * sl_settings.h - the native settings store (#41): a small, human-readable
 * key=value file OUTSIDE the cartridge save, holding the player's GLOBAL
 * DEFAULTS for the handful of controls the front end's Options menu exposes.
 *
 * WHAT IT IS NOT. It is not a config framework and it is not the EEPROM: no
 * game field is repurposed, the per-folder save keeps being written exactly as
 * Rare wrote it (fileSaveSettingsForFolder), and nothing in src/game reads or
 * writes this file - the game reaches it only through the src/native glue
 * (sl_settings_apply.c) that calls the game's own setters.
 *
 * This header is HOST-CLEAN on purpose: plain ints and const char * only, no
 * game or SDL type, so src/platform (host headers), src/native (the N64
 * include tree) and the self-test can all include it.
 *
 * FILE FORMAT (version 1), one setting per line, '#' or ';' starts a comment:
 *
 *     # sightline settings
 *     version=1
 *     look_updown=0
 *     aim_control=0
 *     mouse_invert_y=0
 *     sprint_enabled=0
 *     aspect_ratio=0
 *     fov_vertical=6000
 *     mouse_sensitivity=100
 *     scoped_mouse_sensitivity=100
 *     pad_button_layout=0
 *     pad_stick_layout=0
 *     pad_look_sensitivity=100
 *     pad_look_deadzone=15
 *     pad_move_deadzone=15
 *     crouch_mode=0
 *     sprint_mode=0
 *     window_mode=0
 *     window_width=0
 *     window_height=0
 *     fullscreen_width=0
 *     fullscreen_height=0
 *     vsync=0
 *     world_detail=0
 *
 * Unknown keys are ignored and preserved nowhere (the file is rewritten from
 * the table), a malformed or out-of-range value falls back to that setting's
 * default, a missing file means defaults, and nothing about it is fatal. The
 * file is rewritten only when a value actually changes, through a temp file
 * and an atomic replace.
 *
 * RETIRED KEYS (#63, 2026-09-20 - owner decision: no ORIGINAL / MODERN
 * profile, no N64 control styles, no BUTTON MODE): control_style (the game
 * side is pinned to 1.1 Honey by sl_settings_apply.c), pad_button_mode (the
 * registry's PAD slots are always live) and controller_profile (the pad is
 * always the modern dual-stick controller). A file carrying any of them
 * reads as an unknown key - ignored, not rewritten, dropped on the next
 * write-on-change like any other unknown key. No version bump: an older
 * file's remaining keys read exactly as before.
 *
 * THE ONE BOUNDED EXTENSION (#46): lines whose key starts with "bind." are
 * not scalars. They are kept verbatim as (key, string value) pairs - at most
 * SL_SETTINGS_EXT_MAX of them - handed to the binding registry
 * (sl_bindings.c) through sl_settings_ext_get, written back after the
 * scalars, and preserved across rewrites even when this build does not
 * understand them. The store neither parses nor validates them; what a
 * binding line means is the registry's business. Same version; a file
 * without any is the #42 file.
 *
 *     bind.move_forward.kbm.1=key:UP
 *
 * LOCATION: the project's established player-data root - the same ladder the
 * cartridge save, the asset overrides and the demo save use (LOCALAPPDATA,
 * then USERPROFILE\AppData\Local, then TEMP, then the current directory):
 *
 *     %LOCALAPPDATA%\sightline\config.ini            normal build
 *     %LOCALAPPDATA%\sightline\demo\config.ini       -Demo build (beside the
 *                                                     demo's own save, so the
 *                                                     demo never touches the
 *                                                     owner's settings)
 *
 * SL_CONFIG=<file> is the ONE developer override, the same shape as
 * SL_EEPROM_RW for the save: it names the file outright so a test can run
 * against a scratch config without touching the player's. Not for players.
 */
#ifndef SL_SETTINGS_H
#define SL_SETTINGS_H

enum sl_setting {
    /* The game's Look Up/Down option (PLAYER_OPTION_LOOK): 0 = reverse (the
     * cartridge default), 1 = upright. THE PAD STICK ONLY - never the mouse. */
    SL_SET_LOOK_UPDOWN = 0,
    /* The game's Aim Control option (PLAYER_OPTION_AIM): 0 = hold, 1 = toggle. */
    SL_SET_AIM_CONTROL,
    /* Invert Mouse Y (#39): 0 = off, 1 = on. THE MOUSE ONLY. The live state is
     * src/platform/sl_input.c's g_mouse_invert_y; this is its persisted copy. */
    SL_SET_MOUSE_INVERT_Y,
    /* Sprint (#42): 0 = off (the cartridge's movement, Left Shift does
     * nothing), 1 = on (Left Shift held while moving engages the native
     * Sprint at the movement seam, bondview2.c). Live keyboard/mouse only;
     * read by the game through sl_sprint_enabled (sl_settings_apply.c). */
    SL_SET_SPRINT_ENABLED,
    /* ASPECT RATIO (#45): 0 = 4:3 (the accepted presentation, exactly as
     * before #45), 1 = 16:9, 2 = 32:9. A PRESENTATION setting: it decides the
     * shape of the content viewport the renderer fits inside the window and
     * how far the 3D projection widens; nothing in src/game reads it. Typed
     * accessors and the viewport maths live in sl_display.h (host class, the
     * same as this file). Missing / malformed / out-of-range -> 4:3. */
    SL_SET_ASPECT_RATIO,
    /* FIELD OF VIEW (#45): the VERTICAL field of view in hundredths of a
     * degree, 3598..7756, default 6000 = 60.00 = FOV_Y_F exactly (the
     * default is a no-op). Displayed and stepped as the 16:9-equivalent
     * horizontal FOV (60..110) by sl_display.c; applied by the renderer to
     * the world projection only. Not touched by RESET DEFAULTS (bindings). */
    SL_SET_FOV_VERTICAL,
    /* MOUSE SENSITIVITY (#50): the gameplay mouse look's gain as a PERCENT of
     * the accepted feel, 10..300 in steps of 10, default 100 = exactly the
     * pre-#50 look (SL_MOUSE_SENS 6 = 0.15 degrees per count). Applied by
     * src/platform/sl_input.c at the one gameplay mouse-look seam (both look
     * axes, one factor); never the watch / front-end / bindings pointer, the
     * buttons, the wheel or a controller stick. Range: 10% = 0.015 deg/count
     * sits under the slowest rate the old stick-curve path ever produced
     * (0.018, measured), 300% = 0.45 deg/count = 400 counts per half turn;
     * zero would freeze look and is refused, the sign belongs to Invert
     * Mouse Y. Missing / malformed / out-of-range -> 100. */
    SL_SET_MOUSE_SENSITIVITY,
    /* SCOPED SENSITIVITY (#50): a second percent, 10..300 step 10, default
     * 100, multiplied on top of the one above ONLY while the game's own
     * adjustable-scope predicate holds (sl_game_scoped_zoom_active: aim mode
     * with an item carrying WEAPONSTATBITFLAG_DISABLE_CROUCH - the sniper
     * rifle, the camera; the same test the wheel's ZOOM context uses). Plain
     * aiming without such a scope is NOT scoped. */
    SL_SET_SCOPED_MOUSE_SENSITIVITY,
    /* BUTTON LAYOUT (#63, 2026-09-20): which named PRESET the controller's
     * PAD bindings were last seeded from - SL_BUTTON_LAYOUT_* below, CUSTOM
     * meaning the editor holds something else. The presets themselves live
     * in the binding registry (sl_bindings.c g_layouts); choosing one writes
     * the PAD slots of every action and this id, editing any PAD slot in an
     * editor flips this to CUSTOM, RESET DEFAULTS puts it back to DEFAULT.
     * The registry re-checks it at load: a stored preset whose rows the
     * bind. lines no longer match reads as CUSTOM. Missing / malformed ->
     * DEFAULT, which is the compiled default table exactly. */
    SL_SET_PAD_BUTTON_LAYOUT,
    /* STICK LAYOUT (#63, 2026-09-20): which thumb moves and which looks -
     * SL_STICK_LAYOUT_* below, the four Halo layouts. Read every poll by
     * src/platform/sl_input.c at the one seam that routes the pad's two
     * sticks onto the four native movement channels (map_pad_modern); the
     * game's Look Up/Down option and SL_LOOK_INVERT apply to whichever stick
     * carries pitch. The menu stick stays the left stick. Missing /
     * malformed -> DEFAULT. */
    SL_SET_PAD_STICK_LAYOUT,
    /* LOOK SENSITIVITY (#51, 2026-09-20): the controller's LOOK pair's gain
     * as a PERCENT of the accepted feel, SL_PAD_LOOK_SENS_MIN..MAX in steps
     * of SL_PAD_LOOK_SENS_STEP, default 100 = the pre-#51 channel bit for
     * bit. Applied by src/platform/sl_input.c to the LOGICAL look channel
     * (turn / pitch, after the STICK LAYOUT has said which stick carries
     * them, so SOUTHPAW's left stick is tuned by the same line), after the
     * deadzone and before the channel's +/-70 clamp; the cartridge's own
     * curve (bondview2.c: the value over 70, signed-squared, times fovy/60)
     * is untouched, so 100 = full stick = the game's full turn rate and a
     * higher percent reaches that rate at a smaller deflection, never
     * beyond it. Never the move pair, the menu stick or the mouse. Missing /
     * malformed / out-of-range -> 100. */
    SL_SET_PAD_LOOK_SENSITIVITY,
    /* LOOK DEADZONE (#51): the inner deadzone of the LOOK pair's two axes,
     * as a PERCENT of full stick travel, SL_PAD_DEADZONE_MIN..MAX step 1,
     * default 15 = EXACTLY the compiled SL_PAD_DEADZONE of 5000 raw SDL
     * units (the threshold is p x 5000 / 15 raw, within one percent of p
     * percent of the 32767-unit travel across the range). The SHAPE is the
     * one the file has always had - per axis, the remainder rescaled so full
     * travel still reaches 1.0 - only the size is the player's. Applied by
     * sl_input.c to whichever physical axes the STICK LAYOUT routes to turn
     * and pitch. Missing / malformed / out-of-range -> 15. */
    SL_SET_PAD_LOOK_DEADZONE,
    /* MOVE DEADZONE (#51): the same, for the two axes the layout routes to
     * walk and strafe. Default 15 = the compiled 5000. Full deflection is
     * the channel's 70 either way; there is no move sensitivity. */
    SL_SET_PAD_MOVE_DEADZONE,
    /* CROUCH MODE / SPRINT MODE (#56, 2026-09-20): how the CROUCH and SPRINT
     * actions' edges are interpreted by the platform action layer
     * (src/platform/sl_action.c, sl_action_modes_apply) BEFORE the level
     * reaches the game - SL_ACTION_MODE_HOLD (0, the default: the action is
     * down while a bound control is down, exactly the pre-#56 behaviour) or
     * SL_ACTION_MODE_TOGGLE (1: a fresh press flips a runtime latch, the
     * next fresh press flips it back; holding never repeats, releasing never
     * toggles). One latch per ACTION, never per source, held by the action
     * layer only, never persisted: the modes are in the file, the latches
     * are not. The game's consumers (bondview2.c) keep reading the same
     * level bits and do not know which mode produced them. Missing /
     * malformed / out-of-range -> HOLD. */
    SL_SET_CROUCH_MODE,
    SL_SET_SPRINT_MODE,
    /* PC DISPLAY MODES (#52, 2026-09-20): the window's presentation, read by
     * the SDL backend (src/gfx/sl_gfx_sdl.c) through the typed accessors in
     * sl_window.h - nothing in src/game reads them. WINDOW MODE is
     * SL_WINDOW_WINDOWED (0, the default = the accepted launch: a windowed
     * client at the launcher's SL_WINDOW_SIZE, the aspect setting the width),
     * SL_WINDOW_BORDERLESS (1: SDL's fullscreen-desktop - the window takes the
     * desktop at the desktop's own mode, no mode switch) or
     * SL_WINDOW_FULLSCREEN (2: exclusive fullscreen at a real display mode).
     * WINDOW WIDTH / HEIGHT is the windowed client size the RESOLUTION row
     * chose, 0 / 0 = not chosen (the launcher's initial window, exactly as
     * before #52); in windowed mode only the HEIGHT is authoritative - the
     * width is height x the aspect, the #45 window contract - so a width that
     * a later aspect change left stale is stood aside, not rewritten.
     * FULLSCREEN WIDTH / HEIGHT is the exclusive mode, 0 / 0 = the desktop's
     * mode; a pair no longer offered by the display falls back to the desktop
     * mode (logged once, never rewritten). VSYNC is the GL swap interval, 0 =
     * off (the accepted behaviour: the pacer sets the cadence), 1 = on; it is
     * persisted only after the context confirmed it. Missing / malformed /
     * out-of-range -> the defaults. The backend commits what SDL actually
     * gave, after reading it back (sl_window_commit), never a request. */
    SL_SET_WINDOW_MODE,
    SL_SET_WINDOW_WIDTH,
    SL_SET_WINDOW_HEIGHT,
    SL_SET_FULLSCREEN_WIDTH,
    SL_SET_FULLSCREEN_HEIGHT,
    SL_SET_VSYNC,
    /* WORLD DETAIL (#43, 2026-09-21): the render-visibility profile.
     * SL_WORLD_DETAIL_ORIGINAL (0, the default = the accepted Sightline
     * rendering exactly as it stood before #43: the N64's visibility and
     * detail tuning preserved) or SL_WORLD_DETAIL_ENHANCED (1: a
     * render-only policy at the seams that own a visibility decision - the
     * first one being the near-fog visibility-range rejection of props and
     * characters in chrobjFogVisRangeRelated, src/game/propobj.c, which
     * ENHANCED does not apply so a prop stays eligible until the far-fog
     * cull or the room / portal set removes it). Read by the game through
     * sl_world_detail (sl_settings_apply.c) only; nothing in the simulation
     * reads it - the AI's own copies of the same distance tests
     * (posIsOnScreen / sub_GAME_7F054C58) are untouched, and an INACTIVE
     * store (replay, headless) answers ORIGINAL. Missing / malformed /
     * out-of-range -> ORIGINAL. */
    SL_SET_WORLD_DETAIL,
    SL_SET_COUNT
};

/* The #43 world-detail profiles' persisted ids (append-only), and their
 * display names, "ORIGINAL" / "ENHANCED"; "?" out of range. */
#define SL_WORLD_DETAIL_ORIGINAL  0
#define SL_WORLD_DETAIL_ENHANCED  1
#define SL_WORLD_DETAIL_COUNT     2
const char *sl_world_detail_name(int detail);

/* The #52 window modes' persisted ids (append-only) and the size bound the
 * store accepts for the four size rows (a larger value is malformed and
 * reads as 0 = not chosen; sl_window.c then applies the display's own
 * limits). */
#define SL_WINDOW_WINDOWED    0
#define SL_WINDOW_BORDERLESS  1
#define SL_WINDOW_FULLSCREEN  2
#define SL_WINDOW_MODE_COUNT  3
#define SL_WINDOW_SIZE_MAX    16384

/* The #56 action modes' values and labels (the front end prints HOLD /
 * TOGGLE, the watch the text table's "hold\n" / "toggle\n"). */
#define SL_ACTION_MODE_HOLD   0
#define SL_ACTION_MODE_TOGGLE 1

/* BUTTON LAYOUT presets (append-only ids; the names are the registry's,
 * sl_bindings_layout_name). CUSTOM is a state, not a preset: it is never
 * applied, only reported. */
#define SL_BUTTON_LAYOUT_DEFAULT     0
#define SL_BUTTON_LAYOUT_SOUTHPAW    1
#define SL_BUTTON_LAYOUT_BUMPER      2
#define SL_BUTTON_LAYOUT_GREEN_THUMB 3
#define SL_BUTTON_LAYOUT_CUSTOM      4
#define SL_BUTTON_LAYOUT_COUNT       5   /* CUSTOM included */
#define SL_BUTTON_LAYOUT_PRESETS     4   /* the choosable ones */

/* STICK LAYOUT (Halo's four): which stick carries which pair of the
 * walk / strafe / turn / pitch channels.
 *   DEFAULT          left  walk + strafe    right turn + pitch
 *   SOUTHPAW         right walk + strafe    left  turn + pitch
 *   LEGACY           left  walk + turn      right pitch + strafe
 *   LEGACY SOUTHPAW  right walk + turn      left  pitch + strafe */
#define SL_STICK_LAYOUT_DEFAULT         0
#define SL_STICK_LAYOUT_SOUTHPAW        1
#define SL_STICK_LAYOUT_LEGACY          2
#define SL_STICK_LAYOUT_LEGACY_SOUTHPAW 3
#define SL_STICK_LAYOUT_COUNT           4
/* The display names, "DEFAULT" .. "LEGACY SOUTHPAW"; "?" out of range. */
const char *sl_stick_layout_name(int layout);

/* The #50 percent rows' shared bounds and UI step. */
#define SL_MOUSE_SENS_MIN   10
#define SL_MOUSE_SENS_MAX   300
#define SL_MOUSE_SENS_STEP  10
#define SL_MOUSE_SENS_DEFAULT 100

/* The #51 controller rows' bounds and UI steps. LOOK SENSITIVITY: 25 (full
 * stick = channel 17, 6% of the full turn rate through the square curve -
 * slower is unusable) .. 200 (half of the remaining travel already reaches
 * the channel's 70; beyond that the stick loses its proportional range), in
 * steps of 5 (3.5 channel units at full stick, above the channel's integer
 * grain). DEADZONE: 0 (no deadzone, a rescale over the whole travel) .. 40
 * percent (13333 raw; more and the stick is mostly deadzone), step 1. */
#define SL_PAD_LOOK_SENS_MIN     25
#define SL_PAD_LOOK_SENS_MAX     200
#define SL_PAD_LOOK_SENS_STEP    5
#define SL_PAD_LOOK_SENS_DEFAULT 100
#define SL_PAD_DEADZONE_MIN      0
#define SL_PAD_DEADZONE_MAX      40
#define SL_PAD_DEADZONE_STEP     1
#define SL_PAD_DEADZONE_DEFAULT  15

/* The "bind." extension (#46): at most this many lines, key / value sizes
 * including the terminator. 14 actions x 2 devices x 2 slots = 56 fit. */
#define SL_SETTINGS_EXT_MAX  64
#define SL_SETTINGS_EXT_KEY  48
#define SL_SETTINGS_EXT_VAL  32
#define SL_SETTINGS_EXT_PREFIX "bind."

/* Resolve the path and load the file. Called ONCE, from the player-session
 * path only (sl_eeprom_init_rw - a writable save names a real player session;
 * trace replay and headless health never reach it, so they never see a
 * config and stay bit-identical). Before this runs the store is INACTIVE:
 * gets return defaults, sets change memory only, nothing is written. */
void sl_settings_init(void);

/* Has sl_settings_init run (and so do the values below mean anything)? The
 * apply glue keys on this so a replay is never touched. */
int  sl_settings_active(void);

/* The current value of a setting (its default when inactive or unset). */
int  sl_settings_get(int id);

/* Set a value. Out-of-range values are refused (unchanged). Writes the file
 * only when the value actually changed and the store is active. */
void sl_settings_set(int id, int value);

/* The file the store reads and writes, "" while inactive. Diagnostics only. */
const char *sl_settings_path(void);

/* The one-time .style sidecar import (#41's sl_settings_import_legacy_style)
 * left with the control_style key on 2026-09-20: there is no style to
 * import into. A sidecar beside a save is simply never read. */

/* THE "bind." EXTENSION (#46). Keys must carry SL_SETTINGS_EXT_PREFIX and
 * fit SL_SETTINGS_EXT_KEY; values fit SL_SETTINGS_EXT_VAL; anything else is
 * refused. get returns the stored string or NULL. set stores (a NULL or
 * empty value REMOVES the line), returns 1 if the stored state changed, and
 * writes the file on a change while the store is active - unless a batch is
 * open (below), in which case the write happens once at the batch's end.
 * Same write-on-change rule as the scalars. */
const char *sl_settings_ext_get(const char *key);
int         sl_settings_ext_set(const char *key, const char *value);
/* Remove every extension line whose key starts with `prefix`; returns the
 * number removed. One write (or none) at the end, batch rules as above. */
int         sl_settings_ext_remove_prefix(const char *prefix);
/* Group several sets into one file write: begin, sets..., end. Nested
 * begins count; the write happens when the outermost end sees a change. */
void        sl_settings_batch_begin(void);
void        sl_settings_batch_end(void);
/* Lines held, for tests and diagnostics. */
int         sl_settings_ext_count(void);

#endif /* SL_SETTINGS_H */
