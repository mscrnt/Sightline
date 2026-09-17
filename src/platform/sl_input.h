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
 * The synthesised pad is shaped for the control style the GAME reports
 * (src/native/sl_game_query.c), not for a style chosen here, so changing 1.1
 * Honey / 1.2 Solitaire / 1.3 Kissy / 1.4 Goodnight in the in-game options
 * re-maps the devices with no restart. See sl_input.c for what each style
 * does with the pad and which of the eight are reachable.
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

/* The SECOND virtual pad, and the reason it exists.
 *
 * The N64 pad has one analog stick and a digital C cluster, so under every 1.x
 * control style the game can accept analog input for movement or for looking,
 * never both: 1.1/1.3 put looking on C (bondview2.c:5209-5248 never sets
 * canNaturalPitch), 1.2/1.4 put walking on C (:5150). That is the hardware,
 * not a defect, and no amount of platform-side mapping removes it.
 *
 * Rare already solved it - with the 2.x styles, in game code, reading a SECOND
 * controller (bondview2.c:4858). Those set canNaturalTurn AND canNaturalPitch
 * together (:4956-4957), which is the only place in the game where both analog
 * look axes are live at once. So modern twin-stick is reached by giving the
 * game the second pad it asks for, not by moving gameplay into the host.
 *
 * LIVE INPUT ONLY. A recorded stream is four bytes per retrace - one pad - and
 * this milestone does not widen that format. With SL_INPUT loaded, or with no
 * window, the shim presents exactly one controller as it always did, so trace
 * replay and headless health are bit-identical to before.
 *
 * Returns 0 when live input is not available, leaving the outputs untouched.
 * The pad reads neutral unless the game reports a 2.x style, so selecting 1.1
 * behaves exactly as it did. */
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
 * and the pointer is NOT captured. So this is silent during play, during the
 * watch, under a recorded stream and headless. */
int sl_input_pointer_get(int *x, int *y, int *win_w, int *win_h,
                         unsigned *motion_serial);

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
