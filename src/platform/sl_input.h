/**
 * Physical devices (keyboard, mouse, gamepad) -> N64 controller state,
 * native only.
 *
 * The game reads controllers through osContGetReadData, which the native shim
 * implements (src/platform/sl_ultra_shim.c). That is the ONE seam: src/joy.c
 * is its only caller, so anything synthesised here reaches the game by exactly
 * the path a recorded input stream does.
 *
 * Two producers share that seam and are mutually exclusive by construction:
 *
 *   SL_INPUT=<file>   a recorded stream. Trace replay. Untouched by this file.
 *   live keyboard     only when sl_gfx_active() - a REAL window is up - AND no
 *                     recorded stream is loaded.
 *
 * Headless replay never has a window, so determinism is unaffected: with no
 * window the shim's behaviour is bit-identical to before this module existed.
 *
 * The header is deliberately SDL-free so the shim need not see SDL headers.
 *
 * Since 2026-09-20 (#63) no mapping here depends on the game's control style:
 * the game side is pinned to 1.1 Honey (src/native/sl_settings_apply.c), the
 * keyboard, the mouse and the pad's two sticks all reach the game through
 * the native movement channels, and which thumb does what is the STICK
 * LAYOUT and BUTTON LAYOUT settings (sl_settings.h, sl_bindings.h).
 */
#ifndef SL_INPUT_H
#define SL_INPUT_H

#ifdef __sgi
#error "sl_input.h is native-only"
#endif

/* Sample every attached device into a latched pad state. Called once per
 * frame from the SDL backend's poll, after events have been drained. */
void sl_input_live_poll(void);

/* Latest latched pad state. Returns 0 if live input is not available (poll
 * has never run, i.e. no window), leaving the outputs untouched. */
int sl_input_live_get(unsigned short *button, signed char *stick_x,
                      signed char *stick_y);

/* The SECOND virtual pad, and the reason it still exists.
 *
 * It was added so the 2.x control styles - the only ones in which the game
 * reads both analog look axes, from a SECOND controller (bondview2.c:4858,
 * :4956-4957) - could be driven by one player's two sticks. Since #63
 * (2026-09-20) the pad's sticks reach the game through the native movement
 * channels instead, the control style is pinned to 1.1 Honey, and this pad
 * is ALWAYS neutral; it is still presented so the game keeps counting two
 * controllers exactly as it did (menus and player-count checks unchanged).
 *
 * LIVE INPUT ONLY. A recorded stream is four bytes per retrace - one pad -
 * and the format is not widened. With SL_INPUT loaded, or with no window,
 * the shim presents exactly one controller as it always did, so trace
 * replay and headless health are bit-identical to before.
 *
 * Returns 0 when live input is not available, leaving the outputs untouched. */
int sl_input_live_get2(unsigned short *button, signed char *stick_x,
                       signed char *stick_y);

/* Hand over the SDL window, once, at backend init.
 *
 * Needed because holding the pointer takes THREE calls, not one. Measured on
 * this machine: SDL_SetRelativeMouseMode(TRUE) returns success and delivers
 * relative deltas while leaving the window ungrabbed and the OS cursor drawn
 * over the game. SDL_SetWindowMouseGrab needs a window, and asking SDL for the
 * focused one returns NULL in precisely the case where the grab has to be
 * re-asserted. Typed void* so this header stays SDL-free. */
void sl_input_live_set_window(void *sdl_window);

/* INVERT MOUSE Y - the native setting, and the one state that decides whether
 * mouse pitch is inverted (#39). 1 = physical mouse up looks DOWN. Applied
 * exactly once, at the physical delta in read_mouse, so the linear mouse-look
 * channel and the fallback channel both see it and neither sees it twice.
 * MOUSE ONLY: the game's own Look Up/Down option keeps governing the gamepad
 * stick and never reaches the mouse; this never reaches the stick.
 *
 * Precedence, lowest first: built-in default, SL_MOUSE_INVERT if present at
 * the first poll (the developer override, honoured only while the settings
 * store is INACTIVE - replay, headless, the harness), the persisted config
 * (the SEED, applied at startup by the shim from src/platform/sl_settings.c;
 * with the store active the env is reported and ignored), then an explicit
 * SET from the menus, which wins for the run and is the ONE path that
 * persists - the setter hands the value to the settings store. */
int  sl_mouse_invert_y_get(void);
void sl_mouse_invert_y_set(int on);
void sl_mouse_invert_y_seed(int on);

/* MOUSE SENSITIVITY (scoped = 0) and SCOPED SENSITIVITY (scoped = 1), #50:
 * percents in the settings store (SL_MOUSE_SENS_MIN..MAX, default 100 = the
 * accepted feel), applied at the gameplay mouse-look seam only - the scoped
 * one on top of the first while the game's adjustable-scope predicate holds.
 * get returns the store's value (the default if it were somehow out of
 * range); step moves it one SL_MOUSE_SENS_STEP down (dir < 0) or up, clamped,
 * and persists through the store. Both editors call these and nothing else. */
int  sl_mouse_sens_get(int scoped);
void sl_mouse_sens_step(int scoped, int dir);
/* The SLIDER view of the same value (#50, the bar rows): the fill fraction
 * (value - MIN) / (MAX - MIN) in 0..1, and the set from a fraction - the
 * fraction's grid point (nearest SL_MOUSE_SENS_STEP multiple, MIN and MAX
 * inclusive), clamped, through the same store. Neither bypasses the step
 * grid, the bounds or the default: 0.5 lands on 150, a full bar on 300. */
float sl_mouse_sens_fraction(int scoped);
void  sl_mouse_sens_set_fraction(int scoped, float t);
/* Is the LEFT mouse button held, as a device fact (down over a focused
 * window and not yet released)? The front end's slider bars follow the
 * pointer while it is (sl_front_options.c), the way the 007-mode bars follow
 * the cursor while A is held. */
int  sl_input_pointer_lmb_held(void);

/* ---- CONTROLLER TUNING (#51) ---------------------------------------------
 *
 * The three controller rows both editors show - the settings store's
 * pad_look_sensitivity / pad_look_deadzone / pad_move_deadzone (sl_settings.h
 * for what each means and its bounds) through the #50 idiom: get (the value,
 * the default when the store is out of range), step (one grid step, clamped),
 * and the slider bar's fraction / set-from-a-fraction on the same grid. Read
 * by sl_input.c at the pad seam every poll, so a step is felt on the next
 * poll and the two menus cannot disagree. */
#define SL_PAD_TUNE_LOOK_SENS     0
#define SL_PAD_TUNE_LOOK_DEADZONE 1
#define SL_PAD_TUNE_MOVE_DEADZONE 2
#define SL_PAD_TUNE_COUNT         3
int   sl_pad_tune_get(int which);
void  sl_pad_tune_step(int which, int dir);
float sl_pad_tune_fraction(int which);
void  sl_pad_tune_set_fraction(int which, float t);

/* ---- CONTROLLER FAMILY (#63) --------------------------------------------
 *
 * (The CONTROLLER PROFILE calls that sat here - ORIGINAL / MODERN - left on
 * 2026-09-20 with the profile: the pad is always the modern controller. The
 * STICK LAYOUT is read straight from the settings store every poll; the
 * editors set it there.)
 *
 * The FAMILY of the physical pad that drives the game, decided by SDL's own
 * controller-type classification (SDL_GameControllerGetType, from its mapping
 * database - never a product-name match): NONE (no pad), XBOX (Xbox 360 /
 * One / Series and compatibles), PLAYSTATION (PS3 / PS4 / DualSense), or
 * GENERIC (mapped by SDL but of another family - Switch Pro, a generic
 * XInput-less pad). Modern input never depends on it; only the labels the
 * editors print and the model the watch draws do. Re-read whenever SDL
 * reports a device change. */
#define SL_PAD_FAMILY_NONE        0
#define SL_PAD_FAMILY_XBOX        1
#define SL_PAD_FAMILY_PLAYSTATION 2
#define SL_PAD_FAMILY_GENERIC     3
int  sl_input_pad_family(void);
/* The family's display name for the editors: "NONE", "XBOX", "PLAYSTATION",
 * "GENERIC". */
const char *sl_input_pad_family_name(int family);

/* The PHYSICAL state of the driving pad this poll, for the watch's controller
 * visualisation: every part addressed by its canonical id
 * (SL_PART_* in src/sl_asset_override.h). Sticks are -1..1 with SDL's own
 * sense (x + = right, y + = down), triggers 0..1, buttons 0 / 1, all straight
 * off the device with only the reader's deadzone applied (since #51 the
 * player's LOOK / MOVE DEADZONE by the axis's role, so a stick inside its
 * deadzone draws neutral) and never the look sensitivity - this is what the
 * player's hands are doing, not what the game receives. Returns 0 (outputs
 * zeroed) when no pad drives the game. */
typedef struct {
    float left_x, left_y, right_x, right_y;
    float left_trigger, right_trigger;
    unsigned int held;      /* bit (1u << SL_PART_id) per held button part */
} sl_pad_visual;
int  sl_input_pad_visual(sl_pad_visual *out);

/* Window focus gained (1) or lost (0). The grab follows it, so that alt-tab
 * out of the game is not a trap. Losing focus also DISARMS click-to-capture:
 * the pointer is only ever taken again by another deliberate click. */
void sl_input_live_focus(int has_focus);

/* A mouse button went down over the window. This is what takes the pointer.
 *
 * CLICK TO CAPTURE. The window opens with the pointer free and the OS cursor
 * visible; the first click into a focused window engages relative mode, hides
 * the cursor and grabs, and that click is swallowed rather than fired. Menus
 * release the grab and closing the menu re-asserts it with no second click;
 * focus loss releases it AND disarms. SL_MOUSE=0 opts out completely.
 *
 * Grabbing on the first poll instead - which is what this replaces - took the
 * pointer before the window owned it, and was measured holding a grab the X
 * server was not honouring while the OS pointer sat outside the window.
 *
 * `button` is the SDL button index and `clicks` is SDL's own click counter for
 * the press. Any button still takes the pointer, and still without looking at
 * where the click was. The two arguments narrow one further thing: a LEFT
 * click on a front-end screen that a button press would advance becomes ONE
 * N64 A edge, so the mouse skips the boot chain and confirms in the cursor
 * menus by the path every physical button already uses. `clicks > 1` - the
 * second edge of a double click - is refused, so a double click cannot
 * activate something on the screen the first click opened. */
void sl_input_live_click(int button, int clicks);

/* The matching UP edge. `button` is the same SDL button index. This is what
 * re-arms the next click and what clears the mark a front-end menu click
 * leaves behind - see sl_input_live_click and read_mouse in sl_input.c. The
 * mark is why the click that picks a difficulty cannot arrive as the first
 * shot of the level it starts. */
void sl_input_live_release(int button);

/* THE ABSOLUTE POINTER, for the native front end (src/native/sl_menu_pointer.c).
 *
 * GoldenEye's front end is already a pointer menu - one cursor in front.c that
 * every screen hit-tests for itself - so a native mouse needs no hover model
 * and no highlight of its own. This reports DEVICE FACTS only:
 *
 *   x, y            pointer position in WINDOW pixels, top-left origin
 *   win_w, win_h    the window size the renderer set its viewport from
 *   motion_serial   advances on every poll the pointer actually MOVED
 *
 * The serial is the whole of the input-ownership rule: a caller that acts only
 * when it changes cannot override a selection the keyboard or the pad has just
 * made with a pointer that is sitting still, and the pointer takes the
 * selection back on its first pixel of real motion.
 *
 * Returns 0 - leaving the outputs untouched - unless live input is running, a
 * cursor-driven front-end menu is up, the window has focus and mouse focus,
 * and the pointer is NOT captured - OR the solo watch is open (#40), where
 * the same record is filled either from the absolute position (uncaptured)
 * or by integrating the relative deltas (captured, the gameplay grab kept).
 * So this is silent during play, under a recorded stream and headless. */
int sl_input_pointer_get(int *x, int *y, int *win_w, int *win_h,
                         unsigned *motion_serial);

/* Is the pointer CAPTURED (relative mode, host cursor hidden) right now? The
 * watch draws the game's own crosshair as the pointer exactly then, and
 * leaves the host cursor to be the pointer otherwise (#40). */
int sl_input_pointer_captured(void);

/* Is an SL_POINTER_PROBE sample the pointer right now (a developer witness;
 * never in a player session)? The watch draws its crosshair for a probed
 * pointer as for a captured one, so a bounded run can photograph it. */
int sl_input_pointer_probed(void);

/* Is the MOUSE the thing currently pointing at the menu, as opposed to the
 * keyboard or the pad? NARROWER than "keyboard/mouse is the producer": it is
 * true only once the mouse has physically moved, and false again as soon as a
 * stick or arrow key moves the menu cursor.
 *
 * It exists for exactly one decision. GoldenEye re-places its cursor on every
 * menu transition (front.c:3080, :3453, :3694, :1376), which is the right
 * behaviour for a pad and the wrong one for a mouse - the game's cursor would
 * jump away from where the player is physically pointing. The frontend layer
 * uses this to re-assert the pointer's position ONCE on entering a new menu,
 * and must not use it to weaken the per-frame rule, which is still "act only
 * when the motion serial changes". Returns 0 when there is no live pointer. */
int sl_input_pointer_owns(void);

/* One Escape keypress, delivered as an EVENT so that holding the key cannot
 * repeat. Escape is NOT a quit key: the input layer turns it into the N64
 * button the game already uses to leave wherever the player is - START in
 * play and in the watch, B in the front end. Closing the window still quits. */
void sl_input_live_escape(void);

/* One Tab keypress, same event path, always N64 START. Tab and Escape are the
 * only two PC keys that open or close the watch; Enter is confirm and nothing
 * else. */
void sl_input_live_start(void);

/* Mouse wheel notches, + = up. Consumed ONLY while a menu is up, where each
 * notch becomes exactly one discrete vertical menu step - the same full stick
 * deflection W and Up produce. Unrelated to the pointer: the wheel drives the
 * STICK and works in the watch menus too, where there is no cursor. */
void sl_input_live_wheel(int notches);

/* A controller was attached or detached. Delivered as an EVENT, from the same
 * drain as Escape and Tab, because the device list is NOT stable at startup
 * and the old one-shot probe read it exactly once.
 *
 * MEASURED 2026-09-03, both pads awake, stock SDL 2.32.10, production
 * configuration (window up, then SDL_INIT_GAMECONTROLLER):
 *
 *     t=0ms      NumJoysticks=2   DualSense, Xbox Series X
 *     t=165ms    NumJoysticks=1   DualSense REMOVED
 *     t=3557ms   NumJoysticks=2   DualSense re-ADDED
 *
 * So a probe that runs once, anywhere inside that 3.4-second hole, sees a
 * list the hardware never actually had - and never looks again. That is not
 * a hotplug nicety, it is the ordinary startup of a pad that is plugged in
 * the whole time. Wireless pads sleeping on idle and waking on input (the
 * owner's Xbox does exactly this) widen the same hole to minutes.
 *
 * Event-driven, not scanned: SDL raises CONTROLLERDEVICEADDED/REMOVED and the
 * drain already runs every presented frame, so there is no periodic poll here
 * and nothing re-enumerates on a frame where nothing changed. */
void sl_input_live_device_change(void);

/* Release any pointer grab. Called on backend shutdown. */
void sl_input_live_shutdown(void);

#endif /* SL_INPUT_H */
