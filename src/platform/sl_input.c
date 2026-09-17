/**
 * sl_input.c - physical devices -> the physical N64 controller.
 *
 * This layer maps hardware onto hardware. The game reads an N64 pad and decides
 * what each button means; that decision is the game's, and the eight control
 * styles ARE that decision. Nothing here re-encodes, normalises or second-
 * guesses it.
 *
 * TWO CONTROL MODES, because the faithful map and the comfortable one are not
 * the same thing and the owner wants both:
 *
 *   modern (DEFAULT)  the left thumb always moves and the right always looks,
 *                     under every style. Achieved by swapping which physical
 *                     stick feeds the N64 stick for the styles that put
 *                     looking on it - see "THE STICK PAIR" at map_pad.
 *   SL_CONTROLS=retro the faithful 1:1 map: the N64 stick is whatever the
 *                     chosen style says it is. Under 1.2 that means the LEFT
 *                     stick looks, which is authentic and feels backwards to
 *                     anyone expecting a modern shooter.
 *
 * Modern mode is the ONLY thing here that consults game state, and it swaps
 * two input sources - no action, no button and no game state changes. That is
 * what separates it from the encoder this file used to be, which rewrote the
 * buttons themselves and flattened every style into one feel.
 *
 * REWRITTEN 2026-08-25, at the owner's instruction and to fix a design error
 * of mine. The previous version decoded each device into a device-neutral
 * intent (walk / look / fire) and then RE-ENCODED that intent for whichever
 * style the game reported, reading the style back out of game state to do it.
 * It was careful, it was well tested, and it was wrong: by construction it
 * produced the same feel under every style. The owner: "Honey and Solitaire
 * are basically the same controls like we are overriding things... we
 * shouldn't override anything, but just map the controller to the same buttons
 * and the game changes the actions."
 *
 * THE MAP - fixed and total; only the stick pair varies, see above:
 *
 *     left stick    -> analog stick       right stick -> C cluster
 *     WASD          -> analog stick       mouse       -> C cluster
 *     RT / RB / LMB / F -> Z              LT / LB / RMB / Q -> R
 *     X / R -> A        A, B / E, Space -> B
 *     Start / Tab / Esc -> START          Enter -> A (confirm, NEVER START)
 *     d-pad / arrows -> d-pad
 *
 * Why that is the right map rather than a lazy one: the N64 pad has ONE stick
 * and a digital C cluster, and the styles differ precisely in which of the two
 * walks and which looks - bondview2.c:5150 splits the movement scheme, :5117
 * splits the button scheme. Mapping straight through means 1.1 Honey walks on
 * the stick and looks on C, while 1.2 Solitaire swaps them. The difference is
 * restored to the player instead of being averaged away, and picking a style
 * in Options now does what the menu says it does.
 *
 * Expect looking to feel DIGITAL under 1.1-like styles. That is not a
 * regression - C buttons are digital on real hardware. Under 1.2 looking is on
 * the stick and stays analog, which is why 1.2 felt smoother even back when
 * the encoder was flattening everything else.
 *
 * If a modern twin-stick or mouse-look feel is wanted, it belongs behind an
 * explicit option the player turns on, NOT wired in underneath the game's own
 * control setting where it silently contradicts it. That was the old bug.
 *
 * BUTTON BIT LAYOUT is not inferred from the decomp. It is stated in the notes:
 *   goldeneye_docs/notes/GE Documentation/Main Menus/Cheat Menu/
 *       Button Cheat Codes.txt
 * ("The 2 byte button code masks are the same as usual"), giving, high bit
 * first: A B Z Start, D-up D-down D-left D-right, -- -- L R, C-up C-down
 * C-left C-right. include/PR/os.h agrees (CONT_A 0x8000 ... CONT_F 0x0001);
 * the note is the authority, the header is the corroboration.
 *
 * TWO PRODUCERS, KEPT SEPARATE (added 2026-09-01).
 *
 * Everything above still describes the GAMEPAD, which is unchanged: physical
 * controls onto fixed N64 buttons, and only the stick PAIR is style-aware.
 *
 * Keyboard and mouse now take a different route, because mapping them onto one
 * analog stick and a digital C cluster could never give them a modern feel.
 * Under 1.1 Honey the N64 stick turns and C strafes, so a mouse wired to C
 * STRAFES when you look left and right; under 1.2 Solitaire the stick looks and
 * C walks, so WASD wired to the stick LOOKS. Neither is a bug in the map - it
 * is what the N64 pad is - and no arrangement of two devices onto one stick
 * fixes both.
 *
 * So keyboard and mouse instead supply FOUR NUMBERS directly to the game's own
 * movement channels - forward/back, strafe, yaw, pitch - at a seam inside
 * bondviewProcessInput that sits AFTER the control-style branches have
 * finished. See src/native/sl_move_channels.c for the contract and
 * src/game/bondview2.c (search sl_move_channels_get) for the seam and its
 * gates. Consequences worth stating:
 *
 *   - keyboard and mouse feel the same under every 1.x style, which is the
 *     point: the style interprets the PAD, and the pad is a separate producer.
 *   - fire, aim, use, weapon cycle and start still arrive as N64 BUTTONS and
 *     are still interpreted by the selected style. Nothing here binds a
 *     physical control to a semantic game action.
 *   - aim mode stays the game's: right-click arrives as the N64 aim button and
 *     the game decides everything aiming means. What the mouse no longer does
 *     is BECOME the stick while aiming. It used to, and the game reads aim
 *     turn from stick extremes (bondview2.c:5309) and the crosshair offset
 *     from the raw stick (:6256), so a rate-reporting device was being read as
 *     a position-reporting one. Turn and pitch now go through the same two
 *     channels while aiming as at any other time; walk and strafe do not, so
 *     aim mode still stops the player moving. Live keyboard/mouse only - the
 *     pad never comes through here and keeps GoldenEye's floating manual aim.
 *
 * LAST DEVICE WINS. The channels are published only while the KEYBOARD OR
 * MOUSE was the last thing touched; the first gamepad input hands ownership
 * back and the override goes quiet. That is not a convenience feature, it is
 * what keeps the gamepad path - including the 2.x dual-pad styles, whose
 * movement the game derives from the second controller - bit-for-bit the
 * behaviour it had before this existed.
 *
 * MENUS. Keyboard and wheel drive the STICK: WASD and the arrows deflect the
 * N64 stick, and one wheel notch is one discrete pulse of the same
 * deflection, which navigates BOTH menu systems with no menu-side change
 * (front.c:1189 frontUpdateControlStickPosition moves the front-end cursor
 * from stick deflection; options.c:593-696 steps the watch menus on stick
 * thresholds). HOW FAR is per menu system: the front end gets the full
 * SL_STICK_MAX, the watch gets SL_WATCH_STICK, because the watch's lists have
 * an unlatched fast-scroll band above 0x46 that a full deflection lands in -
 * see that constant for the measurement. Mouse RELATIVE motion is drained and
 * discarded in menus - drained, not skipped, or every pixel moved while a
 * menu was open would be banked and handed to the game on the frame it
 * closed.
 *
 * THE POINTER, added 2026-09-06, is a separate thing from all of that and is
 * deliberately narrow. GoldenEye's front end is a POINTER menu already - one
 * cursor in front.c that every screen hit-tests for itself - so this layer
 * publishes the absolute position and the left-button edge and nothing else.
 * It does not hit-test, does not know what any menu contains, and does not
 * synthesise navigation: no d-pad, no C buttons, no stick pulses. The mapping
 * and the cursor write live in src/native/sl_menu_pointer.c, and a click
 * becomes one N64 A edge, which is the button those menus already read. See
 * the g_ptr_* block below and sl_input_pointer_get.
 *
 * THE POINTER IS TAKEN BY A CLICK, never on the first poll - and "taken" means
 * relative mode, which only gameplay uses. The front end CONFINES the pointer
 * instead: grabbed so it cannot wander onto the desktop, its absolute position
 * still meaningful, and from file select onward the host cursor hidden so that
 * the game's own crosshair is the pointer. See set_pointer_mode.
 *
 * Env knobs (all optional):
 *   SL_CONTROLS=retro     faithful 1:1 map; default is modern (see above)
 *   SL_MOUSE=0            never capture the pointer; keyboard still works
 *   SL_MOUSE_WARP=1       warp-based relative mode instead of raw input. A
 *                         DIAGNOSTIC, off everywhere by default - it was tried
 *                         as the x11 default and failed the owner's hand test
 *                         on both confinement and feel (see read_env)
 *   SL_MOUSE_DX_SIGN=1    physical-to-SDL dx sign; see read_mouse
 *   SL_MOUSE_DY_SIGN=1    physical-to-SDL dy sign; see read_mouse
 *                         The DEFAULTS ARE PER-PLATFORM (+1/+1 on Windows,
 *                         -1/-1 elsewhere) because the host's relative-motion
 *                         convention is; each was accepted by hand on its own
 *                         host. Neither knob is needed for normal play - they
 *                         are the runtime escape hatch if a third transport
 *                         disagrees again.
 *   SL_MOUSE_SENS=f       look deflection per pixel of motion (default 6)
 *   SL_MOUSE_INVERT=1     invert mouse pitch only
 *   SL_LOOK_INVERT=1      invert pitch for every device
 *   SL_INPUT_DEBUG        print the synthesised pad whenever it changes
 */
#ifndef __sgi
#include "sl_input.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The ONE thing this layer asks the game (src/native/sl_game_query.c).  See
 * "the stick pair" in the file header: it decides which PHYSICAL stick feeds
 * the N64 stick, and nothing else.  s32 is int on this -m32 target. */
extern int sl_game_control_style(int player_num);

/* Two more read-only windows into the game, same file, same rules. Menu mode
 * decides the pointer grab, whether mouse motion is discarded, and what Escape
 * means; aim mode decides whether the four channels are withheld so the game
 * keeps the look axes. Both return a scalar and neither writes anything. */
extern int sl_game_menu_mode(void);
extern int sl_game_aim_mode(int player_num);

/* The Look Up/Down option, read for ONE narrow purpose - see "THE MOUSE OWNS
 * ITS OWN PITCH" at map_kbm. Read-only, like the three above. */
extern int sl_game_look_upright(void);

/* "Is a CURSOR-DRIVEN front-end menu up right now", 0 or 1, same file and same
 * rules as the four above. This layer must not know what a mission or a
 * difficulty is; it asks one scalar question and the front end answers it.
 * See sl_game_pointer_menu_active in src/native/sl_game_query.c for which
 * menus are in the set and why the set is the front end's to decide. */
extern int sl_game_pointer_menu_active(void);

/* "Would a button press advance the screen the front end is on", 0 or 1. A
 * WIDER set than the one above and a different question: the boot chain -
 * legal screen, logos, gun barrel, cast roll - has no cursor to move but does
 * have the any-button skip every physical button already uses, so a left click
 * belongs in it. Same file, same rules; the set is written down once, there. */
extern int sl_game_click_advance_active(void);

/* Publish the four native movement channels (src/native/sl_move_channels.c). */
/* "Is a recorded movement sidecar driving the channels this run" - ONE
 * definition, beside the sidecar itself in sl_ultra_shim.c, for the same
 * reason sl_live_input_active has one: a second copy is a second thing that
 * can drift, and what it would drift on is whether a replay is faithful. */
extern int sl_move_replay_active(void);
extern void sl_move_channels_set(int active, int walk, int strafe,
                                 int turn, int pitch);

/* The LINEAR mouse look channel (src/native/sl_move_channels.c). Degrees, not
 * stick units: the four channels above are squared by the game's stick curve,
 * which is what makes equal-and-opposite mouse motion fail to return to the
 * starting angle. See the block comment in that file. */
extern void sl_mouse_look_set(int active, float yaw_deg, float pitch_deg);

/* "Is there live input at all" - ONE definition, in the shim beside the
 * recorded-stream state it depends on (src/platform/sl_ultra_shim.c). A second
 * copy here is what a determinism regression would look like. */
extern int sl_live_input_active(void);

/* sl_game_menu_mode ordinals. */
#define SL_MENU_NONE   0   /* on foot, watch closed */
#define SL_MENU_WATCH  1   /* the watch: GoldenEye's pause and options menu */
#define SL_MENU_FRONT  2   /* the title screen / before a player exists */

/* THE POINTER MODE - one per SCREEN CLASS, because CONFINEMENT, RELATIVE MODE
 * and CURSOR VISIBILITY are three independent SDL facilities and the four
 * classes want four different combinations of them. Conflating the first two is
 * the failure this enum exists to prevent: turning on relative mode to
 * "capture" in a menu would make SDL recentre the pointer every frame and
 * destroy the absolute position the whole menu pointer is built on.
 *
 *              grab+barrier   relative   host cursor
 *   FREE           no            no        VISIBLE     unfocused, SL_MOUSE=0,
 *                                                      no live input, the WATCH
 *   SKIP          YES            no        VISIBLE     the boot / intro chain
 *   POINT         YES            no        hidden      the interactive front end
 *   LOOK          YES           YES        hidden      gameplay
 *
 * WHY SKIP AND POINT DIFFER BY THE CURSOR ALONE. The boot screens are not
 * pointer menus - a click just advances them - so the host cursor is the only
 * pointer there and it stays. From file select onward the GAME draws a pointer:
 * frontDrawCursor (front.c:1251) paints crosshairimage at cursor_h_pos /
 * cursor_v_pos, and every one of the screens this layer calls a pointer menu
 * calls it (front.c:2903 file select, :3167 mode select, :3544 mission select,
 * :3867 difficulty, :7027 briefing). Leaving the host cursor up there drew two
 * pointers on top of each other - owner-reported on file select, 2026-09-07 -
 * so on those screens the host's goes away and the game's crosshair IS the
 * pointer. Owner's words: "i want the crosshairs in the select file and beyond.
 * Regular mouse cursor is fine for the legal, and other logos."
 *
 * Nothing new is drawn for this. crosshairimage is the game's own asset
 * (image_bank.c:66, :245), and the same one the gameplay crosshair uses
 * (gunfire.c:6278). On file select it swaps to the COPY or ERASE icon while
 * one of those is armed (front.c:1259-1268), which is Rare's behaviour and is
 * left alone.
 *
 * g_grabbed means LOOK specifically, which is what read_mouse and read_pointer
 * both need to know: read_mouse only reads motion and buttons while it is set,
 * and read_pointer only samples an absolute position while it is clear. */
#define SL_PTR_FREE   0
#define SL_PTR_SKIP   1
#define SL_PTR_POINT  2
#define SL_PTR_LOOK   3

/* CONTROLLER_CONFIG ordinals (src/bondconstants.h:1275). */
#define SL_STYLE_HONEY      0   /* 1.1 */
#define SL_STYLE_SOLITARE   1   /* 1.2 */
#define SL_STYLE_KISSY      2   /* 1.3 */
#define SL_STYLE_GOODNIGHT  3   /* 1.4 */
#define SL_STYLE_PLENTY     4   /* 2.1 */
#define SL_STYLE_GALORE     5   /* 2.2 */
#define SL_STYLE_DOMINO     6   /* 2.3 */
#define SL_STYLE_GOODHEAD   7   /* 2.4 */

/* The 2.x styles are the dual-controller ones - the game reads a second pad
 * for them and nothing else does (bondview2.c:4857-4861). */
static int style_is_dual(int s)
{
    return s >= SL_STYLE_PLENTY && s <= SL_STYLE_GOODHEAD;
}

/* Button masks - see the note cited in the file header. */
#define SL_BTN_A        0x8000
#define SL_BTN_B        0x4000
#define SL_BTN_Z        0x2000
#define SL_BTN_START    0x1000
#define SL_BTN_DUP      0x0800
#define SL_BTN_DDOWN    0x0400
#define SL_BTN_DLEFT    0x0200
#define SL_BTN_DRIGHT   0x0100
#define SL_BTN_L        0x0020
#define SL_BTN_R        0x0010
#define SL_BTN_CUP      0x0008
#define SL_BTN_CDOWN    0x0004
#define SL_BTN_CLEFT    0x0002
#define SL_BTN_CRIGHT   0x0001

/* Full stick deflection. A real N64 stick reads about +/-80 at the gate; the
 * game clamps at 120 (JOY_CLAMP_MAX, src/joy.c) and treats 60 as the aim-mode
 * threshold, so 80 is a firm push without pretending to be a broken stick. */
#define SL_STICK_MAX    80

/* THE WATCH'S STICK - what a keyboard key or a wheel notch deflects the N64
 * stick to while the watch (pause menu) is up. NOT a tuning knob: it is the one
 * value that makes every vertical consumer in options.c take exactly one
 * latched step per physical press, and it is read straight off those consumers.
 *
 * The equipment page (game_options_inventory_navigation, options.c:1046) and
 * the control-style list (sub_GAME_7F0A611C, :1184) read the SAME stick three
 * ways in one frame:
 *
 *   |y| >= 0x47   a whole item EVERY FRAME, unlatched     (:1056, :1065)
 *   0x1f..0x45    a proportional drift of y/300 per frame (:1098-1111)
 *   |y| >= 0x10   one item, latched until the stick has been back inside
 *                 +/-0xF for a frame                      (:1113-1120 through
 *                 watch_stick_y_pressed_up/down, :687-696; re-armed at :1122)
 *
 * The first band is Rare's fast scroll for a stick held hard against the gate,
 * and a keyboard at SL_STICK_MAX sits in it: MEASURED on Bunker 1, one 60 ms
 * S tap moved the equipment selector 4 to 6 items and one wheel notch exactly
 * 4 (two on the first frame - fast scroll plus the latched step - then one per
 * frame for the rest of the pulse). 0x46 = 70 is the single value the two
 * unlatched bands both exclude (`< 0x46` and `>= 0x47`) that the latched one
 * still accepts, so at 70 a press of ANY length is one step, and the game's
 * own latch - not a repeat timer here - decides that a held key does nothing
 * more until it is released.
 *
 * TWO LATCHES, NOT ONE - easy to conflate, and it was conflated once while
 * this was being diagnosed. The EQUIPMENT page's latch is the 0x10 one above
 * (watch_stick_y_pressed_up/down, re-armed by watch_stick_y_prev_active at
 * :1122-1129 once |y| < 0x10). The 0x2E latch is a DIFFERENT mechanism for the
 * OPTION pages (sub_GAME_7F0A5088/50C4, :651-660, gated by
 * watch_stick_y_nav_ready, re-armed at :1489-1503 inside -0xA..+0xB); the
 * equipment page never reads it. 70 clears both, once per press, so the
 * distinction changes nothing here - but a reader who assumes the equipment
 * page latches at 0x2E will conclude a deflection of 0x2E..0x45 is safe, and
 * it is not: that is the drift band. Horizontally nothing distinguishes 70 from 80:
 * page changes latch at 0x2E (:593-624) and the volume slider clamps at 0x46
 * itself (watch_adjust_volume_slider, :2617-2621).
 *
 * The FRONT END is not a stepped list - frontUpdateControlStickPosition
 * (front.c:1189) integrates a cursor from the stick - so it keeps SL_STICK_MAX
 * and is untouched by this. */
#define SL_WATCH_STICK  70

/* Pad stick deadzone, raw SDL units, rescaled so full travel still reaches
 * 1.0. Belongs to the device: a worn stick drifts, and the game's own
 * deadzone is only +/-5 of 80 (controlStickXSafe, bondview2.c:4810). */
#define SL_PAD_DEADZONE 5000

/* Post-deadzone magnitude at which a pad axis counts as a digital press. The
 * N64's C cluster is digital, so a stick standing in for it needs a step
 * point; 0.30 of remaining travel is ~13300 raw, near the 12000 that the
 * previous hand-written mapping was calibrated at. */
#define SL_PAD_STEP     0.30f

/* Trigger travel before it counts as pressed. Lower than the stick step
 * because a trigger is pulled deliberately and a resting finger sits at 0. */
#define SL_PAD_TRIG_ON  8000

/* Pixels of mouse motion in one frame before a digital step registers. Small
 * enough to feel immediate, large enough that a resting hand does not creep. */
#define SL_MOUSE_STEP_PX 1

/* Full deflection of a native movement channel. NOT a choice: every consumer
 * of the four channels divides by 70.0f (bondview2.c:5635, :5654, :5902,
 * :5960), so 70 is the unit the game itself defines as "all the way". */
#define SL_CHANNEL_MAX  70

/* ---------------------------------------------------------------------------
 * intent - what the player asked for, with no idea what a C button is.
 * Axes are normalised to -1..1. Each `_step` is -1/0/+1, the digital form of
 * the same axis, produced by whichever device could offer one; the encoder
 * uses whichever form the style can actually express.
 * ------------------------------------------------------------------------- */
typedef struct {
    float move_forward;  int move_forward_step;   /* + = forward */
    float move_strafe;   int move_strafe_step;    /* + = right   */
    float look_yaw;      int look_yaw_step;       /* + = right   */                               /* + = right, a turn RATE */
    float look_pitch;    int look_pitch_step;     /* + = up, player-facing  */
    int fire, aim, next_weapon, action, pause;
    int confirm;                                  /* Enter: accept, NEVER start */
    unsigned short dpad;                          /* raw D-pad bits, menus  */
} sl_intent;

static int   g_ready;                  /* poll has run at least once */
static int   g_grabbed;                /* relative mouse mode is on */
static int   g_env_read;
static int   g_grab_wanted;
/* EVERY attached pad is opened, and exactly ONE of them drives the game.
 *
 * This is a PHYSICAL-device list, and it must not be confused with the two
 * VIRTUAL N64 pads. The architecture is unchanged and is still the one 97e17ee1
 * established:
 *
 *     one logical modern pad -> map_pad_dual() -> TWO virtual N64 controllers
 *
 * The DualSense and the Xbox pad are ALTERNATE PRODUCERS of that single
 * logical modern pad. Neither of them is virtual pad 2. Handing a second
 * physical controller to the second virtual pad would break the 2.x styles,
 * which need both virtual pads driven by one player's two hands.
 *
 * Why a list at all, when only one is read: the owner has both attached, and
 * "unplug one to use the other" is not an acceptable answer. See
 * sl_input_live_device_change in sl_input.h for the measured device churn
 * that made the previous single, once-probed slot fail outright. */
#define SL_MAX_PADS 8
static SDL_GameController *g_pads[SL_MAX_PADS];
static int   g_npads;                  /* entries used in g_pads */
static int   g_pad_active = -1;        /* index into g_pads, -1 = none */
static int   g_pad_inited;             /* SDL_INIT_GAMECONTROLLER attempted */
static int   g_pad_rescan = 1;         /* a device event is pending */
static float g_sens;
/* Physical-motion -> SDL dx/dy sign, +1 or -1 each. See "THE ONE PLACE THE
 * MOUSE'S DIRECTION CONVENTION IS DEFINED" in read_mouse.
 *
 * THE DEFAULT IS PER-PLATFORM BECAUSE THE TRANSPORT IS PER-PLATFORM. This is
 * the only value in the whole mouse path that is a property of the host's
 * relative-motion stream rather than of the game, and the two hosts this fork
 * has run on do not agree on it. Each arm below is the value that was
 * PHYSICALLY accepted on that host; neither was derived from the other.
 *
 *   _WIN32   +1 / +1. Stock SDL2 (mingw-w64-i686-SDL2, see tools/windows/
 *            setup.ps1) with no Sightline transport of its own in between, so
 *            the deltas arrive in SDL's nominal convention: +dx right,
 *            +dy down. That is the convention the rest of this chain was
 *            screen-measured correct against on the real renderer - see the
 *            "whole mouse chain, end to end" table in
 *            docs/decisions/native-input-signs.md, taken when no sign was
 *            applied here at all, i.e. at +1/+1.
 *
 *   else     -1 / -1. Owner-validated by hand under WSLg/XWayland at commit
 *            fbcc3a48, where the raw relative stream did NOT follow SDL's
 *            nominal convention. That acceptance is real and is kept, but it
 *            is evidence about THAT transport only.
 *
 * The bug this fixes (B-071): fbcc3a48 introduced these knobs and their -1
 * defaults on the WSLg acceptance alone, days before the checkout moved to
 * Windows, and the platform move carried the WSLg value onto a transport that
 * negates nothing. Two negations, both axes, which is exactly the mirroring
 * the owner reported. Nothing downstream of this line changed. */
#ifdef _WIN32
static int   g_dx_sign = 1;
static int   g_dy_sign = 1;
#else
static int   g_dx_sign = -1;
static int   g_dy_sign = -1;
#endif
static int   g_mouse_invert;
static int   g_look_invert;
static int   g_retro;                  /* SL_CONTROLS=retro: faithful 1:1 */

/* THE RAW MOUSE COUNTS for this poll, in device counts, BEFORE the +/-1
 * clamp_unit that read_mouse applies on its way to the stick-unit channels.
 * That clamp is the first of three saturations on the old path (it pins at
 * SL_STICK_MAX/g_sens = 13.3 counts per poll at the default sensitivity), and
 * carrying the counts around it is what makes a fast flick turn the full
 * amount instead of the clamped amount. Set - not accumulated - every poll. */
static int   g_raw_dx, g_raw_dy;
static int   g_linear_look = 1;        /* SL_MOUSE_LINEAR_LOOK, default ON */

/* The window, handed over by the SDL backend at init. Needed because
 * SDL_SetRelativeMouseMode alone does NOT grab on this setup - see
 * set_pointer_mode - and SDL_SetWindowMouseGrab needs a window. Asking SDL_GetKeyboardFocus for it
 * would return NULL in exactly the case the grab has to be re-asserted. */
static void *g_win;
static int   g_focused = 1;            /* window has input focus */
/* Escape, as a COUNTDOWN of polls rather than a single flag. The synthesised
 * button has to survive being latched: the shim latches the pad once per VI
 * retrace (sl_input_vi_advance) while this runs once per presented frame, and
 * those two are 1:1 today but nothing enforces it. A one-poll press would be
 * silently dropped by any poll that landed between two retraces. Holding it
 * for a few polls is still exactly ONE rising edge, which is all the game
 * tests (bondview2.c:4839 tests `(buttons & ~oldbuttons) & START_BUTTON`). */
#define SL_ESC_HOLD_POLLS 3
static int            g_esc_polls;     /* polls of synthesised button left */
static unsigned short g_esc_button;    /* chosen when the hold started */

/* TAB, on the same countdown and for the same reason, but with none of
 * Escape's context-sensitivity: Tab is always START. Tab and Escape are now the
 * ONLY PC keys that open the watch - Enter used to synthesise START and that is
 * exactly the owner-reported defect this replaces. */
static int            g_start_polls;

/* MOUSE WHEEL -> one discrete vertical menu step per notch, menus only.
 *
 * A notch is an EVENT, and the game reads a stick, so a notch has to become a
 * deflection that lasts long enough to be latched and then RETURNS TO CENTRE
 * before the next one starts. The centre gap is not cosmetic: the watch menus
 * re-arm their vertical latches only while the stick sits near centre
 * (options.c:1122-1129 within +/-0xF, :1489-1503 within -0xA..+0xB), so two
 * pulses run together would step ONCE, and a pulse that never returned to
 * centre would step once and then go dead.
 *
 * The pulse's HEIGHT is the menu deflection chosen per poll (SL_WATCH_STICK in
 * the watch, SL_STICK_MAX in the front end), not a constant of its own: the
 * equipment page steps a whole item on EVERY frame the stick is at or past
 * 0x47, so a full-height pulse stepped 4 items per notch (MEASURED - see
 * SL_WATCH_STICK), and only the height, not the length, of the pulse fixes
 * that.
 *
 * The queue is what stops a spun wheel free-running: notches beyond the cap are
 * dropped rather than banked, so flicking the wheel hard cannot dump twenty
 * steps into the menu a second later. */
/* Both in POLLS, not frames, and both 3 for the reason SL_ESC_HOLD_POLLS is 3:
 * the shim latches the pad once per VI retrace while this runs once per
 * presented frame, and the two are NOT reliably 1:1 - with the watch up on
 * Bunker 1 a 3-poll pulse was latched on 4 retraces (MEASURED, 2026-09-13). A
 * two-poll centre gap could land as one retrace, or none, and a dropped gap is
 * a notch that steps twice or not at all. Three costs one extra frame per
 * notch. */
#define SL_WHEEL_HOLD_POLLS 3
#define SL_WHEEL_GAP_POLLS  3
#define SL_WHEEL_QUEUE_MAX  4
static int g_wheel_queue;              /* notches waiting, signed, + = up   */
static int g_wheel_hold;               /* polls left in the current pulse   */
static int g_wheel_dir;                /* -1/0/+1, direction of that pulse  */
static int g_wheel_gap;                /* polls of forced centre remaining  */

/* CLICK TO CAPTURE.  The pointer is NOT taken when the window opens: it is
 * taken when the player clicks inside a focused window, which is what every PC
 * shooter does and what stops a launched-and-ignored window from stealing the
 * desktop pointer.
 *
 * Cleared on focus loss, so alt-tabbing away genuinely returns the pointer and
 * coming back needs a deliberate click. NOT cleared by a menu: the watch is a
 * normal part of play and demanding a click after every Esc round-trip would be
 * worse than the problem. So the grab is released for the menu and re-asserted
 * the moment the menu closes, with no click needed. */
static int g_capture_armed;

/* ---------------------------------------------------------------------------
 * THE ABSOLUTE POINTER, for the front end's own cursor.
 *
 * GoldenEye's front end is already a POINTER menu: front.c carries one cursor
 * (cursor_h_pos / cursor_v_pos, front.c:288) that every front-end screen hit-
 * tests against, and frontUpdateControlStickPosition (front.c:1148) is the one
 * place the stick moves it. So a native mouse does not need a hover model, a
 * hit test or a highlight of its own - it needs to put that ONE cursor where
 * the pointer is. This block is the device half of that: absolute position,
 * whether the position is meaningful, whether the pointer actually MOVED, and
 * the left-button press edge. Nothing here knows what any menu contains.
 *
 * g_ptr_motion is a SERIAL, not a flag: the frontend layer compares it with the
 * one it last acted on, so a stationary pointer never overrides a selection the
 * keyboard or the pad has just moved, and the pointer becomes authoritative
 * again on the first pixel of real motion. That is the whole of the ownership
 * rule - there is no arbitration framework, and the pad path is untouched.
 *
 * Position comes from SDL_GetMouseState inside the existing per-frame poll,
 * not from a second event drain: the backend's pump (sl_gfx_sdl.c) consumes the
 * whole queue every frame, so an SDL_MOUSEMOTION this file went looking for
 * would always be gone already. SDL_GetMouseState is a state read of what that
 * pump last recorded, which is exactly the same value one frame's motion events
 * would have left. It is only ever read while the pointer is NOT captured, so
 * relative mode cannot make it meaningless. */
static int      g_ptr_valid;           /* the position below is meaningful */
static int      g_ptr_x, g_ptr_y;      /* window pixels, top-left origin */
static int      g_ptr_w, g_ptr_h;      /* window size, the renderer's viewport */
static unsigned g_ptr_motion;          /* ++ on every poll the pointer moved */
static int      g_ptr_have_last;       /* g_ptr_last_* hold a real sample */
static int      g_ptr_last_x, g_ptr_last_y;

/* IS THE MOUSE THE THING CURRENTLY POINTING? Narrower than g_owner below,
 * which lumps the keyboard in with the mouse because they share a producer.
 * This one is specifically about the front end's CURSOR: it is what decides
 * whether entering a new menu should put that cursor under the pointer, or
 * leave it wherever GoldenEye's own code just placed it.
 *
 * It has to be narrow. GoldenEye resets the cursor on every menu transition -
 * setCursorPOSforMode (front.c:3080), set_cursor_to_stage_solo (:3453),
 * set_cursor_pos_difficulty (:3694), frontSetCursorPositionToNextTab (:1376) -
 * and for a pad or keyboard player that reset IS the feature: it drops the
 * cursor on the entry the player most likely wants. So the resync must never
 * fire for them, which means "the keyboard is the producer" is not good enough
 * a signal; only the MOUSE having actually moved is.
 *
 * Set by real pointer motion, cleared by any keyboard or pad deflection that
 * moves the menu cursor, and cleared on focus loss. Starts 0, so a session
 * that never touches the mouse behaves exactly as it did before this existed. */
static int      g_ptr_owner;

/* THE MENU CONFIRM, and the reason it is a countdown rather than a flag.
 *
 * Same shape as g_esc_polls and for the same measured reason: the shim latches
 * the pad once per VI retrace while this polls once per presented frame, and a
 * one-poll press can land between two retraces and be silently dropped. Three
 * polls is still exactly ONE rising edge, which is all the menus test
 * (front.c:2504, :3376, :3576 all read joyGetButtonsPressedThisFrame). */
#define SL_CONFIRM_HOLD_POLLS 3
static int g_confirm_polls;

/* THE ONE PLACE A MENU CLICK IS STOPPED FROM BECOMING A SHOT.
 *
 * The click that picks AGENT arms the capture like any other click, so by the
 * first gameplay frame the grab is on and read_mouse's button gate is open. If
 * the player were still holding the button - or if the transition were quicker
 * than a human release - the SAME press would arrive as fire. So a press the
 * front end consumed is marked here and stays marked until the button is
 * physically RELEASED, at which point the next press is a fresh one. No delay,
 * no frame counting: the guard is the button's own up edge. */
static int g_lmb_menu_used;

/* Set whenever the capture state CHANGES, and consumed by the next read_mouse.
 * Enabling relative mode makes SDL move the pointer (it recentres, and on some
 * paths it restores the pre-capture position when relative mode ends), and
 * whatever the pointer did while uncaptured is still sitting in SDL's relative
 * accumulator. Both arrive as one large bogus delta on the first poll after a
 * transition, which is a visible snap of the view at exactly the moment the
 * player clicks in or closes a menu. Drain it, then throw it away. */
static int g_grab_just_changed;


/* Which device last showed intent: 0 = neither yet, 1 = keyboard/mouse,
 * 2 = gamepad. See "LAST DEVICE WINS" in the file header - this is what keeps
 * the gamepad path, and the 2.x dual-pad styles in particular, untouched. */
#define SL_OWNER_NONE 0
#define SL_OWNER_KBM  1
#define SL_OWNER_PAD  2
static int g_owner;

static unsigned short g_button;
static signed char    g_stick_x, g_stick_y;
/* The second virtual pad. Neutral unless the game reports a 2.x style; see
 * map_pad_dual and the note in sl_input.h for why it exists at all. */
static unsigned short g_button2;
static signed char    g_stick_x2, g_stick_y2;

static int clamp_stick(float v)
{
    if (v >  SL_STICK_MAX) return  SL_STICK_MAX;
    if (v < -SL_STICK_MAX) return -SL_STICK_MAX;
    return (int) v;
}

static float clamp_unit(float v)
{
    if (v >  1.0f) return  1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

static void read_env(void)
{
    const char *v;

    if (g_env_read)
        return;
    g_env_read = 1;

    v = getenv("SL_MOUSE");
    g_grab_wanted = (v != NULL && strcmp(v, "0") == 0) ? 0 : 1;

    /* SL_MOUSE_WARP=1 asks SDL to implement relative mode by WARPING the
     * pointer to the window centre each frame instead of reading raw device
     * motion. It is a diagnostic and compatibility override, and it is OFF by
     * default on every backend.
     *
     * It was briefly the DEFAULT on x11 here, on the strength of a synthetic
     * XTEST probe in which the pointer never left the window. That probe passed
     * and the owner's hand test FAILED: warp-relative delivered neither
     * reliable confinement nor acceptable feel, and mouse look was noticeably
     * jerkier than a normal FPS mouse. Warping is synthetic motion - SDL moves
     * the pointer to recentre it and must then subtract its own warp from the
     * delta stream - so any disagreement between the warp and the motion the X
     * server reports shows up directly as jerk, and the behaviour becomes
     * coupled to where the pointer happens to be on the desktop.
     *
     * The lesson, and it has now cost two rounds: a synthetic-pointer probe is
     * not acceptance for pointer behaviour. Raw relative input is the default. */
    v = getenv("SL_MOUSE_WARP");
    if (v != NULL && strcmp(v, "0") != 0)
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "1");

    v = getenv("SL_MOUSE_DX_SIGN");
    if (v != NULL) g_dx_sign = (atoi(v) < 0) ? -1 : 1;
    v = getenv("SL_MOUSE_DY_SIGN");
    if (v != NULL) g_dy_sign = (atoi(v) < 0) ? -1 : 1;

    /* Say which convention is in force, once, unconditionally - the same way
     * the gamepad line does. This is not instrumentation: dx_sign/dy_sign
     * decide which way the mouse turns the view, and until this line existed
     * there was no way to tell from outside which value a given launch was
     * actually running. A whole round was lost to comparing a run against a
     * remembered baseline that used the opposite sign. */
    fprintf(stderr, "sightline input: mouse convention dx_sign=%+d dy_sign=%+d"
                    " (SL_MOUSE_DX_SIGN / SL_MOUSE_DY_SIGN)\n",
            g_dx_sign, g_dy_sign);

    v = getenv("SL_MOUSE_SENS");
    g_sens = (v != NULL) ? (float) atof(v) : 6.0f;
    if (g_sens <= 0.0f)
        g_sens = 6.0f;

    /* SL_MOUSE_LINEAR_LOOK - the divergence toggle. DEFAULT ON: the cartridge
     * had no mouse, so there is no original mouse behaviour being overridden
     * here; the stick's squared curve was only ever a stand-in for one. Set it
     * to 0 to route the mouse back through that curve for comparison.
     *
     * READ AFTER g_sens, deliberately: `v` is the shared scratch pointer this
     * function reuses for every knob, and reading a second variable into it
     * before g_sens consumed it made SL_MOUSE_LINEAR_LOOK set the SENSITIVITY.
     * Caught by inspection during the change that added it. */
    v = getenv("SL_MOUSE_LINEAR_LOOK");
    g_linear_look = (v != NULL && strcmp(v, "0") == 0) ? 0 : 1;
    v = getenv("SL_MOUSE_INVERT");
    g_mouse_invert = (v != NULL && strcmp(v, "0") != 0);
    v = getenv("SL_LOOK_INVERT");
    g_look_invert = (v != NULL && strcmp(v, "0") != 0);

    /* SL_CONTROLS=retro turns OFF the stick pairing, giving the faithful 1:1
     * map in which the N64 stick is whatever the chosen style says it is.
     * Default is modern: the left thumb always moves and the right always
     * looks, whichever style is selected. */
    v = getenv("SL_CONTROLS");
    g_retro = (v != NULL && strcmp(v, "retro") == 0);
    if (v != NULL && !g_retro && strcmp(v, "modern") != 0)
        fprintf(stderr, "sightline input: SL_CONTROLS=\"%s\" not 'modern' or "
                        "'retro' - using modern\n", v);
    /* Every style knob this layer had is gone. It no longer knows or asks what
     * control style the game is in: it maps physical controls onto the physical
     * N64 pad and the GAME decides what each button does. Branching on style
     * here is exactly what made 1.1 and 1.2 feel identical. */
    if (getenv("SL_STYLE") != NULL || getenv("SL_KBM_STYLE") != NULL
        || getenv("SL_PAD_STYLE") != NULL)
        fprintf(stderr, "sightline input: SL_STYLE/SL_KBM_STYLE/SL_PAD_STYLE "
                        "are gone - this layer no longer encodes per style. "
                        "Pick the control style in the game's Options menu.\n");
}

/* ---------------------------------------------------------------------------
 * device readers
 * ------------------------------------------------------------------------- */

static void read_keyboard(sl_intent *in, int menu)
{
    const unsigned char *k;
    int numkeys = 0;

    k = SDL_GetKeyboardState(&numkeys);
    if (k == NULL)                     /* SDL stubbed out, or video not up */
        return;

    /* WASD. A keyboard is digital, so it fills in BOTH forms of the axis at
     * full deflection and lets the encoder take whichever the style wants. */
    if (k[SDL_SCANCODE_W]) { in->move_forward += 1.0f; in->move_forward_step = 1; }
    if (k[SDL_SCANCODE_S]) { in->move_forward -= 1.0f; in->move_forward_step = -1; }
    if (k[SDL_SCANCODE_A]) { in->move_strafe  -= 1.0f; in->move_strafe_step  = -1; }
    if (k[SDL_SCANCODE_D]) { in->move_strafe  += 1.0f; in->move_strafe_step  = 1; }

    /* E and R were the wrong way round - owner-reported, 2026-09-01. E is the
     * use/reload key on every PC shooter and R is the one people reach for to
     * change weapon here, so E now carries `action` and R `next_weapon`. Only
     * the KEY assignments moved: which N64 button each intent becomes is
     * unchanged (map_kbm: next_weapon -> A, action -> B), because that mapping
     * is the game's vocabulary and the selected control style still decides
     * what each button does. */
    if (k[SDL_SCANCODE_E])     in->action = 1;      /* use / reload   */
    if (k[SDL_SCANCODE_SPACE]) in->action = 1;      /* same, thumb-friendly */
    if (k[SDL_SCANCODE_R])     in->next_weapon = 1; /* weapon cycle   */
    if (k[SDL_SCANCODE_Q])     in->aim = 1;
    if (k[SDL_SCANCODE_F])     in->fire = 1;     /* keyboard-only fire */
    /* ENTER IS CONFIRM AND NOTHING ELSE. It used to raise `pause`, which
     * map_kbm turned into START outside a menu, which is bondviewProcessInput's
     * own trigger for the watch (:4839) - so pressing Enter in play opened the
     * watch. Owner-reported, and the cause was that one line. Enter is now its
     * own intent and only ever reaches N64 A. Tab and Escape are the watch
     * keys; see g_start_polls and sl_input_live_escape. */
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER])
        in->confirm = 1;

    /* Arrows are the D-pad. The game ORs D-pad with C throughout the move
     * code (`L_JPAD | L_CBUTTONS`), so they double as movement, and they are
     * what makes the front-end menus navigable. */
    /* F9 = "I am looking at the bug right now." Not a game binding, so it
     * never reaches the pad and never enters the recorded stream. */
    {
        extern void sl_run_mark(unsigned);
        extern unsigned sl_record_index(void);
        static int held;
        int now = k[SDL_SCANCODE_F9] != 0;
        if (now && !held) sl_run_mark(sl_record_index());
        held = now;
    }

    /* F8 = the SAME statement, answered rather than deferred. F9 records the
     * moment and leaves the diagnosis to a replay; the measured divergence
     * between a native recording and its replay reaches 202 world units by
     * sample 4201, so what the replay shows is not what the owner marked. F8
     * therefore captures the answer in the live run: the frame that is on
     * screen, the camera and rooms behind it, and the draws that project onto
     * the centre of it. See sl_mark_request in src/gfx/sl_gfx_dl.c.
     *
     * Not a game binding, so it never reaches the pad and never enters the
     * recorded stream.  was empty before
     * this line, so it collides with nothing. */
    {
        extern void sl_mark_request(void);
        static int held8;
        int now8 = k[SDL_SCANCODE_F8] != 0;
        if (now8 && !held8) sl_mark_request();
        held8 = now8;
    }

    if (menu) {
        /* In a menu the arrows are a SECOND set of movement keys, so that the
         * stick they deflect is the same one WASD deflects. They are
         * deliberately NOT also put on the D-pad here: a menu that reads both
         * would step twice per press, and both menu systems already navigate
         * from the stick. */
        if (k[SDL_SCANCODE_UP])    { in->move_forward += 1.0f; in->move_forward_step =  1; }
        if (k[SDL_SCANCODE_DOWN])  { in->move_forward -= 1.0f; in->move_forward_step = -1; }
        if (k[SDL_SCANCODE_LEFT])  { in->move_strafe  -= 1.0f; in->move_strafe_step  = -1; }
        if (k[SDL_SCANCODE_RIGHT]) { in->move_strafe  += 1.0f; in->move_strafe_step  =  1; }
    } else {
        if (k[SDL_SCANCODE_UP])    in->dpad |= SL_BTN_DUP;
        if (k[SDL_SCANCODE_DOWN])  in->dpad |= SL_BTN_DDOWN;
        if (k[SDL_SCANCODE_LEFT])  in->dpad |= SL_BTN_DLEFT;
        if (k[SDL_SCANCODE_RIGHT]) in->dpad |= SL_BTN_DRIGHT;
    }
}

static void read_mouse(sl_intent *in, int menu)
{
    int dx = 0, dy = 0;
    unsigned mb = SDL_GetRelativeMouseState(&dx, &dy);

    /* Cleared on ENTRY, so that every early return below - a menu, an
     * uncaptured pointer - leaves the linear look channel at rest rather than
     * republishing the previous poll's motion. */
    g_raw_dx = 0;
    g_raw_dy = 0;

    /* The read above is not optional in menus: it is what DRAINS SDL's
     * relative accumulator. Returning before it would bank every pixel moved
     * while a menu was open and hand the lot to the game on the frame the
     * menu closed. So: always drain, then discard. */
    if (menu)
        return;

    /* Buttons are gated on the capture too, not just motion. Without that the
     * CLICK THAT CAPTURES would also fire the gun, which is the one thing
     * click-to-capture must not do. An uncaptured click engages the pointer and
     * is swallowed; the next one shoots. */
    if (!g_grabbed)
        return;

    /* The drain above already happened, so the stale delta is gone from SDL's
     * accumulator; this just stops it reaching the game. One frame of look is
     * discarded on the frame the pointer is taken or given back, which is a
     * frame in which the player was not aiming anyway. */
    if (g_grab_just_changed) {
        g_grab_just_changed = 0;
        dx = 0;
        dy = 0;
    }

    /* A press the front end already spent is not a trigger pull. See
     * g_lmb_menu_used: the mark is cleared by the button's UP edge, so this
     * costs the menu click and nothing after it. */
    if ((mb & SDL_BUTTON(SDL_BUTTON_LEFT)) && !g_lmb_menu_used)
        in->fire = 1;
    if (mb & SDL_BUTTON(SDL_BUTTON_RIGHT))  in->aim = 1;
    if (mb & SDL_BUTTON(SDL_BUTTON_MIDDLE)) in->aim = 1;

    /* ----------------------------------------------------------------------
     * THE ONE PLACE THE MOUSE'S DIRECTION CONVENTION IS DEFINED.
     *
     * Everything downstream of this point is MEASURED and must not be touched
     * to fix a direction complaint. On the rendered image, injecting at this
     * very boundary and letting the whole chain run for real:
     *   positive look_yaw   -> analogTurn +  -> content moves LEFT  -> camera RIGHT
     *   positive look_pitch -> analogPitch -  -> camera UP (ceiling, seen)
     * So the ONLY degree of freedom left is how a PHYSICAL movement maps onto
     * SDL's dx/dy here, and that is what this block states.
     *
     * SL_MOUSE_DX_SIGN / SL_MOUSE_DY_SIGN exist because that mapping is the one
     * link in the chain this machine cannot measure without a hand on the
     * mouse. They are not tuning knobs: each is +1 or -1 and flips one axis at
     * the source, before any other transform, so a wrong guess costs one env
     * var to correct instead of a code change and another round.
     *
     * THE DEFAULTS ARE PER-PLATFORM, and each was accepted by a hand on a real
     * mouse on its OWN host - see the declaration of g_dx_sign for the two
     * arms and the evidence behind each. Windows takes +1/+1, which is SDL's
     * nominal convention and the one everything downstream was screen-measured
     * against; WSLg/XWayland takes -1/-1, where the raw relative stream
     * demonstrably did not follow that convention.
     *
     * An earlier revision of this comment stated -1/-1 as universal and said Y
     * "is NOT SDL's nominal convention" as though that were a property of the
     * chain rather than of one host. It was written the day before the
     * checkout moved to Windows, and carrying it across a transport change is
     * precisely how B-071 happened: two negations, both axes, mirrored look.
     * Do not restore a single global default here.
     *
     * The same revision correctly warned that dx/dy injected AT THIS BOUNDARY
     * proves nothing about the PHYSICAL-to-SDL step, because such a test
     * assumes the answer. That warning still stands and applies to synthetic
     * pointer injection too. Only a hand on a mouse settles this line.
     *
     * The knobs remain as diagnostic and compatibility overrides. They must
     * never be needed for normal play again.
     * ------------------------------------------------------------------- */
    dx *= g_dx_sign;
    dy *= g_dy_sign;

    /* A mouse reports a RATE, not a position, so it needs no deadzone beyond
     * one pixel and its digital step fires on any real motion. That is the
     * difference from a stick, and it is why each device owns its own
     * thresholds instead of the encoder owning one for everybody. */
    in->look_yaw += clamp_unit((float) dx * g_sens / (float) SL_STICK_MAX);
    if (dx >  SL_MOUSE_STEP_PX) in->look_yaw_step = 1;
    if (dx < -SL_MOUSE_STEP_PX) in->look_yaw_step = -1;

    if (g_mouse_invert)
        dy = -dy;

    /* THE RAW COUNTS, taken here: after both sign conventions and after the
     * pitch invert, so they carry exactly the orientation the two lines around
     * them use, and BEFORE clamp_unit, which is the saturation the linear path
     * exists to avoid. Not accumulated across polls - no banking, no carry;
     * the rejected c2fdaab9 approach is precisely what this must not become. */
    g_raw_dx = dx;
    g_raw_dy = dy;

    /* Screen coords grow downward, so mouse-down is +dy and means look DOWN. */
    in->look_pitch -= clamp_unit((float) dy * g_sens / (float) SL_STICK_MAX);
    if (dy >  SL_MOUSE_STEP_PX) in->look_pitch_step = -1;
    if (dy < -SL_MOUSE_STEP_PX) in->look_pitch_step = 1;
}

/* Deadzone one raw SDL axis and rescale the remainder to -1..1. */
static float pad_axis(int raw)
{
    float f;

    if (raw >  SL_PAD_DEADZONE)
        f = (float) (raw - SL_PAD_DEADZONE);
    else if (raw < -SL_PAD_DEADZONE)
        f = (float) (raw + SL_PAD_DEADZONE);
    else
        return 0.0f;
    return clamp_unit(f / (32767.0f - (float) SL_PAD_DEADZONE));
}


static int pad_step(float v)
{
    if (v >  SL_PAD_STEP) return 1;
    if (v < -SL_PAD_STEP) return -1;
    return 0;
}

/* Is this pad being touched right now? The SAME thresholds the reader uses, so
 * a pad cannot claim ownership with a deflection the reader would then discard
 * as noise - SL_PAD_DEADZONE for sticks, SL_PAD_TRIG_ON for triggers. Stick
 * drift below the deadzone therefore never steals the game from the other pad. */
static int pad_touched(SDL_GameController *c)
{
    static const SDL_GameControllerAxis stick[4] = {
        SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
        SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY
    };
    int i;

    for (i = 0; i < 4; i++) {
        int v = SDL_GameControllerGetAxis(c, stick[i]);
        if (v > SL_PAD_DEADZONE || v < -SL_PAD_DEADZONE)
            return 1;
    }
    if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > SL_PAD_TRIG_ON
        || SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > SL_PAD_TRIG_ON)
        return 1;
    for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
        if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton) i))
            return 1;
    }
    return 0;
}

/* Drop pads SDL has detached, and open every attached one it has not already
 * given us. Runs only when a device event said something changed. */
static void pad_rescan(void)
{
    int i, n;

    for (i = 0; i < g_npads; ) {
        if (SDL_GameControllerGetAttached(g_pads[i])) {
            i++;
            continue;
        }
        fprintf(stderr, "sightline input: gamepad gone \"%s\"\n",
                SDL_GameControllerName(g_pads[i]));
        SDL_GameControllerClose(g_pads[i]);
        g_npads--;
        for (n = i; n < g_npads; n++)
            g_pads[n] = g_pads[n + 1];
        /* The active pad is an INDEX, so closing anything at or before it
         * shifts it. Losing the active pad drops to "none", not to some other
         * player's controller - the next pad that is actually touched wins. */
        if (g_pad_active == i)      g_pad_active = -1;
        else if (g_pad_active > i)  g_pad_active--;
    }

    n = SDL_NumJoysticks();
    for (i = 0; i < n && g_npads < SL_MAX_PADS; i++) {
        SDL_JoystickID id;
        SDL_GameController *c;
        int j, dup = 0;

        if (!SDL_IsGameController(i))
            continue;
        id = SDL_JoystickGetDeviceInstanceID(i);
        for (j = 0; j < g_npads; j++) {
            SDL_Joystick *js = SDL_GameControllerGetJoystick(g_pads[j]);
            if (js != NULL && SDL_JoystickInstanceID(js) == id) { dup = 1; break; }
        }
        if (dup)
            continue;
        c = SDL_GameControllerOpen(i);
        if (c == NULL) {
            fprintf(stderr, "sightline input: gamepad %d would not open: %s\n",
                    i, SDL_GetError());
            continue;
        }
        g_pads[g_npads++] = c;
        fprintf(stderr, "sightline input: gamepad \"%s\" (%d attached)\n",
                SDL_GameControllerName(c), g_npads);
    }
    if (g_npads == 0)
        fprintf(stderr, "sightline input: no gamepad found "
                        "(%d joystick(s) seen)\n", n);
}

void sl_input_live_device_change(void)
{
    g_pad_rescan = 1;
}

static void read_pad(sl_intent *in)
{
    float lx, ly, rx, ry;
    int lt, rt, i;
    SDL_GameController *g_pad;

    if (!g_pad_inited) {
        g_pad_inited = 1;
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            fprintf(stderr, "sightline input: gamepad subsystem unavailable: "
                            "%s\n", SDL_GetError());
            return;
        }
    }
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
        return;

    if (g_pad_rescan) {
        g_pad_rescan = 0;
        pad_rescan();
    }
    if (g_npads == 0)
        return;

    /* WHICH PHYSICAL PAD DRIVES THE GAME: the last one touched, decided here
     * and nowhere else. It needs no menu, no launch flag and no unplugging -
     * pick either controller up and it is the one playing, which is the only
     * arbitration the owner should have to perform.
     *
     * This is the SAME rule the file already applies one level up between the
     * keyboard/mouse and the pad ("LAST DEVICE WINS" in the header); it is now
     * applied between physical pads too. Only the winner is read, so no two
     * controllers ever sum their sticks into one input. */
    for (i = 0; i < g_npads; i++) {
        if (i != g_pad_active && pad_touched(g_pads[i])) {
            if (g_pad_active >= 0)
                fprintf(stderr, "sightline input: gamepad now \"%s\"\n",
                        SDL_GameControllerName(g_pads[i]));
            g_pad_active = i;
            break;
        }
    }
    /* Before anything has been touched, the first attached pad plays. Booting
     * into "no pad until you wiggle a stick" would read as broken. */
    if (g_pad_active < 0)
        g_pad_active = 0;

    g_pad = g_pads[g_pad_active];
    if (g_pad == NULL || !SDL_GameControllerGetAttached(g_pad))
        return;

    /* MEASURED layout (tools/native/inputcal.sh, second pass) - the ordinary
     * modern-FPS shape, LEFT stick moves and RIGHT stick looks:
     *   forward/back lefty -/+    strafe l/r  leftx -/+
     *   look l/r     rightx -/+   look u/d    righty -/+
     * SDL's Y axes grow downward, hence the negations. Note that nothing here
     * knows about C buttons or stick crossing any more: that was the part
     * that used to be hand-written per style, and it now falls out of the
     * encoder instead. */
    lx = pad_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX));
    ly = pad_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY));
    rx = pad_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX));
    ry = pad_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY));
    lt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    rt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

    in->move_forward += -ly;  if (!in->move_forward_step) in->move_forward_step = pad_step(-ly);
    in->move_strafe  +=  lx;  if (!in->move_strafe_step)  in->move_strafe_step  = pad_step(lx);
    in->look_yaw     +=  rx;  if (!in->look_yaw_step)     in->look_yaw_step     = pad_step(rx);
    in->look_pitch   += -ry;  if (!in->look_pitch_step)   in->look_pitch_step   = pad_step(-ry);

    /* Face and shoulder assignments are MEASURED, not reasoned: a calibration
     * pass asked the player for each action and recorded the raw SDL input.
     *   fire  rightshoulder   aim  leftshoulder
     *   next weapon x         reload/action b
     *   pause start
     * Triggers mirror the shoulders on the same side, by request: R1 or R2
     * fire, L1 or L2 aim. WHICH N64 BUTTON each of these becomes is the
     * encoder's problem, and it differs between button scheme A and B - which
     * is precisely why they are intents here and not button masks. */
    /* BACK ("Select", left of the trackpad) marks, the same as F9 - the owner
     * plays on a pad and should not have to reach for the keyboard to point at
     * something. Not a game binding, so it never reaches the pad state and
     * never enters the recorded stream. */
    {
        extern void sl_run_mark(unsigned);
        extern unsigned sl_record_index(void);
        static int held;
        int now = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_BACK) != 0;
        if (now && !held) sl_run_mark(sl_record_index());
        held = now;
    }

    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        || rt > SL_PAD_TRIG_ON)
        in->fire = 1;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        || lt > SL_PAD_TRIG_ON)
        in->aim = 1;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_X))
        in->next_weapon = 1;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B))
        in->action = 1;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A))
        in->action = 1;                              /* same, thumb-friendly */
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_START))
        in->pause = 1;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP))
        in->dpad |= SL_BTN_DUP;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
        in->dpad |= SL_BTN_DDOWN;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
        in->dpad |= SL_BTN_DLEFT;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
        in->dpad |= SL_BTN_DRIGHT;
}

/* ---------------------------------------------------------------------------
 * mapping - physical controls -> N64 pad
 *
 * REWRITTEN 2026-08-25 at the owner's instruction, and the old design was
 * wrong. It decoded physical input into an abstract intent (walk, look, fire)
 * and RE-ENCODED that intent for whichever control style the game reported,
 * reading the style back out of the game to do it. The effect was that every
 * style produced the same feel - 1.1 Honey and 1.2 Solitaire became
 * indistinguishable, because the encoder had already normalised away the one
 * thing that differs between them. The owner put it exactly right: "we
 * shouldn't override anything, but just map the controller to the same buttons
 * and the game changes the actions."
 *
 * So this is now a FIXED map onto the physical N64 pad, with no knowledge of
 * the control style and no query into game state:
 *
 *     left stick   -> analog stick        right stick -> C buttons
 *     RT / RB      -> Z                   LT / LB     -> R
 *     A -> A       B -> B       X -> L    Start -> START     d-pad -> d-pad
 *
 * That is the whole point rather than a simplification: the N64 pad has ONE
 * stick and a C cluster, and the styles differ precisely in which of those
 * walks and which looks (bondview2.c:5150 for the movement split, :5117 for
 * the button split). Mapping straight through means 1.1 puts walking on the
 * stick and looking on C, while 1.2 swaps them - the difference is now felt
 * instead of erased, and choosing a style in Options does what it says.
 *
 * Consequence worth stating plainly, because it will feel different: looking
 * under a 1.1-like style is DIGITAL, because C buttons are digital on real
 * hardware. Under 1.2 looking is on the stick and stays smooth. That is the
 * authentic behaviour of each style, not a regression - and it is why the
 * owner found 1.2's look smoother back when the encoder was still flattening
 * everything.
 *
 * Keyboard and mouse map the same way, for the same reason: WASD drives the
 * stick, the mouse drives the C cluster. If a modern twin-stick/mouse-look
 * feel is wanted later it belongs behind an explicit opt-in that the player
 * chooses, NOT wired in underneath the game's own control setting where it
 * silently contradicts it.
 * ------------------------------------------------------------------------- */

/* Deflection past which an analog look axis counts as a C-button press. The C
 * cluster is digital, so a threshold is unavoidable; this is the same band the
 * game itself treats as "off centre" for the stick (bondview2.c:5252 tests
 * +/-60 of 127).
 *
 * A device's own digital step flag also presses the C button, and that matters
 * most for the MOUSE: a mouse reports a RATE, so at the default sensitivity it
 * would need >6 pixels of travel in one frame to clear this band, and fine aim
 * would be silently dead. read_mouse sets the step on any real motion. */
#define SL_LOOK_C_ON 0.47f

static void map_pad(const sl_intent *in, unsigned short *out_b,
                    float *out_sx, float *out_sy)
{
    unsigned short b = in->dpad;
    float pitch = in->look_pitch;
    int pitch_step = in->look_pitch_step;
    int yaw_step = in->look_yaw_step;
    float stick_x, stick_y;
    int s;

    if (g_look_invert) {
        pitch = -pitch;
        pitch_step = -pitch_step;
    }

    /* THE STICK PAIR.  Which physical stick drives the N64 stick, and which
     * drives the C cluster.  This is the single thing here that depends on the
     * control style, and it is a deliberate, narrow exception to this file's
     * rule - see the header for why it is not the old encoder coming back.
     *
     * Under a 1.1-like style the N64 stick walks and C looks, so left-to-stick
     * and right-to-C is already the modern shape.  Under 1.2/1.4 the game puts
     * BOTH look axes on the stick and walking on C (bondview2.c:5150), so the
     * same wiring hands looking to the LEFT thumb and walking to the right -
     * faithful, and ergonomically backwards.  The owner, testing: "switching to
     * solitaire doesn't match modern fps control on controller.  Left stick
     * looks only.  Right stick should be looking in that mode.  Honey looks
     * right."
     *
     * So the STICKS swap and nothing else does.  Every button keeps its N64
     * button, the game still decides what each one does, and the styles keep
     * their real differences - 1.2 still aims with an analog axis and walks
     * digitally, which is exactly what makes it feel unlike 1.1.  What stays
     * constant is only which thumb aims. */
    s = g_retro ? -1 : sl_game_control_style(0);
    if (s == SL_STYLE_SOLITARE || s == SL_STYLE_GOODNIGHT) {
        stick_x = in->look_yaw;
        stick_y = pitch;
        /* walking moves to C, which is where 1.2 reads it from anyway */
        if (in->move_strafe_step  < 0) b |= SL_BTN_CLEFT;
        if (in->move_strafe_step  > 0) b |= SL_BTN_CRIGHT;
        if (in->move_forward_step > 0) b |= SL_BTN_CUP;
        if (in->move_forward_step < 0) b |= SL_BTN_CDOWN;
        *out_sx = clamp_unit(stick_x) * (float) SL_STICK_MAX;
        *out_sy = clamp_unit(stick_y) * (float) SL_STICK_MAX;
        if (in->fire)        b |= SL_BTN_Z;
        if (in->aim)         b |= SL_BTN_R;
        if (in->next_weapon) b |= SL_BTN_A;
        if (in->action)      b |= SL_BTN_B;
        if (in->pause)       b |= SL_BTN_START;
        *out_b = b;
        return;
    }

    /* 1.1-like, and the front end before a player exists (style reads -1):
     * left stick to the N64 stick, right stick and mouse to C. */
    *out_sx = clamp_unit(in->move_strafe)  * (float) SL_STICK_MAX;
    *out_sy = clamp_unit(in->move_forward) * (float) SL_STICK_MAX;

    /* C cluster: the right stick and the mouse. C-up is speedVertaDown under
     * the default LOOK option, so pushing the look axis UP is C-DOWN - that is
     * the game's own sense, not an inversion applied here. */
    if (in->look_yaw < -SL_LOOK_C_ON || yaw_step < 0)   b |= SL_BTN_CLEFT;
    if (in->look_yaw >  SL_LOOK_C_ON || yaw_step > 0)   b |= SL_BTN_CRIGHT;
    if (pitch >  SL_LOOK_C_ON || pitch_step > 0)        b |= SL_BTN_CDOWN;
    if (pitch < -SL_LOOK_C_ON || pitch_step < 0)        b |= SL_BTN_CUP;

    /* Buttons: one physical control, one N64 button, always the same one.
     * Which ACTION each performs is the game's business and changes with the
     * style - that is the behaviour being restored here. */
    if (in->fire)        b |= SL_BTN_Z;      /* RT / RB / mouse 1 / F */
    if (in->aim)         b |= SL_BTN_R;      /* LT / LB / mouse 2 / Q */
    if (in->next_weapon) b |= SL_BTN_A;      /* X / E */
    if (in->action)      b |= SL_BTN_B;      /* A, B / R, Space */
    if (in->pause)       b |= SL_BTN_START;

    *out_b = b;
}

/* ---------------------------------------------------------------------------
 * map_pad_dual - the 2.x styles, which are the ONLY ones that can express
 * modern controls, because they are the only ones that read a second stick.
 *
 * DERIVED, not chosen. bondview2.c:4858-4915 is the whole dual-pad branch, and
 * it splits four ways along two independent axes. Writing `pad2` for the second
 * controller (joyGetStickX(cur + playercount), :4864) and `pad1` for the first:
 *
 *   WHICH PAD MOVES, WHICH LOOKS  (:4897-4914)
 *     2.1 Plenty / 2.3 Domino    pad2 -> analogStrafe, analogPitch
 *                                pad1 -> analogTurn,   analogWalk   (the
 *                                defaults from :4830-4833, left standing)
 *     2.2 Galore / 2.4 Goodhead  pad2 -> analogStrafe, analogWalk   = MOVE
 *                                pad1 -> analogTurn,   analogPitch  = LOOK
 *
 *   WHICH PAD FIRES, WHICH AIMS  (:4915-4929, Z on both)
 *     2.1 Plenty / 2.2 Galore    fire = pad1 Z, aim = pad2 Z
 *     2.3 Domino / 2.4 Goodhead  fire = pad2 Z, aim = pad1 Z
 *
 * So 2.2 Galore is the one modern shape in the game: pad1 is the looking hand
 * AND the firing hand, pad2 is the moving hand. Mapping the physical right
 * stick and the mouse to pad1 and the left stick and WASD to pad2 lands every
 * modern expectation on the correct N64 button with no re-encoding. 2.4 is the
 * same movement split with fire and aim swapped between the hands.
 *
 * Both branches set canNaturalTurn AND canNaturalPitch from !insightaimmode
 * (:4956-4957) - the only place in the game where both analog look axes are
 * live at once - and canTurnTank unconditionally (:4954), which is what gates
 * speedsideways = analogStrafe/70 at :5633. So on foot under 2.x, strafing is
 * analog and works while aiming, walking is analog and stops while aiming
 * (canLookAhead, :4952/:5652). That last one is Rare's design, not a defect.
 *
 * The remaining actions are read from EITHER pad, so they all go on pad1:
 *   B on either      -> btap, the use/action tap        (:4946-4950)
 *   A on either      -> weapon cycle, and it suppresses firing while held
 *                       (:5012-5025, :5107-5110)
 * Nothing here decides what those DO. That stays the game's, exactly as under
 * 1.x - this function only chooses which physical control reaches which N64
 * button on which of the two pads.
 * ------------------------------------------------------------------------- */
static void map_pad_dual(const sl_intent *in, int style,
                         unsigned short *b1, float *sx1, float *sy1,
                         unsigned short *b2, float *sx2, float *sy2)
{
    float pitch = in->look_pitch;
    int look_hand_fires;

    if (g_look_invert)
        pitch = -pitch;

    /* Turn is always pad1 X and strafe always pad2 X; only the Y axes and the
     * Z buttons move between the pads. */
    *sx1 = clamp_unit(in->look_yaw)   * (float) SL_STICK_MAX;
    *sx2 = clamp_unit(in->move_strafe) * (float) SL_STICK_MAX;

    if (style == SL_STYLE_GALORE || style == SL_STYLE_GOODHEAD) {
        /* 2.2 / 2.4 - pad1 looks, pad2 moves. The modern split. */
        *sy1 = clamp_unit(pitch)            * (float) SL_STICK_MAX;
        *sy2 = clamp_unit(in->move_forward) * (float) SL_STICK_MAX;
    } else {
        /* 2.1 / 2.3 - the axes are split across the pads instead: pad1 walks
         * and turns, pad2 strafes and pitches. Supported so that cycling
         * through the styles in Options never lands on a dead stick, but it
         * is not the modern shape and is not the target of this milestone. */
        *sy1 = clamp_unit(in->move_forward) * (float) SL_STICK_MAX;
        *sy2 = clamp_unit(pitch)            * (float) SL_STICK_MAX;
    }

    /* 2.1 and 2.2 fire from pad1; 2.3 and 2.4 fire from pad2. */
    look_hand_fires = (style == SL_STYLE_PLENTY || style == SL_STYLE_GALORE);

    *b1 = in->dpad;
    *b2 = 0;
    if (in->fire) { if (look_hand_fires) *b1 |= SL_BTN_Z; else *b2 |= SL_BTN_Z; }
    if (in->aim)  { if (look_hand_fires) *b2 |= SL_BTN_Z; else *b1 |= SL_BTN_Z; }

    /* Read from either pad by the game, so they only need to be on one. */
    if (in->next_weapon) *b1 |= SL_BTN_A;
    if (in->action)      *b1 |= SL_BTN_B;
    if (in->pause)       *b1 |= SL_BTN_START;
}

/* ---------------------------------------------------------------------------
 * pointer capture
 *
 * CLICK TO CAPTURE, 2026-09-01, replacing grab-on-first-poll.
 *
 * The old behaviour asserted the grab as soon as the game was live, before the
 * window necessarily owned the pointer at all. MEASURED on this machine
 * (x11/XWayland under WSLg, SDL 2.30.0) against the running game, which had
 * logged grab=1: the OS pointer was sitting at (2086,426) while the window
 * occupied (2124,446)-(3084,1166), and an XTEST warp to (5,5) moved it into a
 * different application and left it there. The internal boolean said captured;
 * the pointer was not. That is the owner's report, reproduced.
 *
 * Two further measurements shaped the fix:
 *
 *   - SDL drops the X grab BY ITSELF when the window loses input focus:
 *     SDL_GetWindowGrab() was observed going 1 -> 0 with no call from us, in
 *     the same poll that SDL_WINDOW_INPUT_FOCUS cleared. A host-side boolean
 *     can therefore never mean "the pointer is held".
 *   - With input focus actually held, the grab DID confine: a warp aimed
 *     outside the window was clamped to the window rectangle on 3 of 3 runs.
 *
 * So the fix is not a stronger grab, it is grabbing at the right moment and
 * only then. The pointer is taken when the player clicks into a focused window
 * and given back on focus loss; g_capture_armed is that decision, and this
 * function only carries it out.
 *
 * g_grabbed means "all three calls are in the state we asked for", not "the
 * first call returned 0". It is still not a claim about the X server.
 *
 * ONE MODE PER SCREEN CLASS, 2026-09-06/07. This used to be a boolean, and the
 * boolean was the bug the owner reported next: the front end released
 * EVERYTHING for a menu, so the pointer wandered onto the desktop while the
 * player was navigating. The fix is not to grab harder in menus, it is to
 * notice that grab, relative mode and cursor visibility are three separate
 * facilities and that the four screen classes want four different combinations
 * of them. Confinement keeps the pointer in the window; relative mode is what
 * makes its POSITION meaningless, and only gameplay wants that; the cursor is
 * hidden only where the game draws a pointer of its own. See the table at
 * SL_PTR_FREE.
 *
 * The gameplay path through this function is unchanged: same three calls, same
 * order, same read-back-don't-cache rule, same barrier-off-before-grab-off
 * ordering on release.
 * ------------------------------------------------------------------------- */
static void set_pointer_mode(int mode)
{
    SDL_Window *w = (SDL_Window *) g_win;
    const SDL_Rect *have_rect;
    SDL_Rect want_rect;
    int have_rel, have_grab, ww = 0, wh = 0;
    int grabbed_now;

    /* ASK SDL WHAT IS TRUE, every poll, instead of caching what we asked for.
     *
     * The early-out this replaces was `if (want == g_grabbed) return;`, and it
     * is why the pointer could escape mid-game with everything apparently
     * healthy: SDL DROPS ITS OWN GRAB when the window loses input focus
     * (measured - SDL_GetWindowGrab observed going 1 -> 0 with no call from
     * us, in the same poll SDL_WINDOW_INPUT_FOCUS cleared). Once that happened,
     * g_grabbed still said 1, `want` was still 1, and the early-out meant the
     * grab was never re-asserted. The cached boolean was a claim about our
     * intent that was being read as a claim about the X server.
     *
     * Reading back costs two getters per frame in the steady state, and the
     * setters are called only when the observed state actually differs from
     * the intent - so this does not re-arm relative mode every frame, which
     * would itself warp the pointer and show up as jerk. */
    have_rel  = SDL_GetRelativeMouseMode() == SDL_TRUE;
    have_grab = (w != NULL) && SDL_GetWindowMouseGrab(w) == SDL_TRUE;
    have_rect = (w != NULL) ? SDL_GetWindowMouseRect(w) : NULL;
    if (w != NULL)
        SDL_GetWindowSize(w, &ww, &wh);

    /* RELATIVE MODE, which is the ONLY thing that separates LOOK from CONFINE.
     * Asked for first when turning on, so that a failure can downgrade the
     * whole call to CONFINE before anything else is touched: no deltas means no
     * mouse look, but the pointer should still be confined and still visible
     * rather than the window silently giving it back. */
    if (mode == SL_PTR_LOOK && !have_rel
        && SDL_SetRelativeMouseMode(SDL_TRUE) != 0) {
        fprintf(stderr, "sightline input: relative mouse unavailable: %s\n",
                SDL_GetError());
        /* No deltas means no mouse look. Fall back to a CONFINED, VISIBLE
         * pointer rather than a hidden one: the stderr line above is the
         * report, and a cursor the player can still see is the honest signal
         * that the pointer is not being consumed as look. */
        mode = SL_PTR_SKIP;
    }
    if (mode != SL_PTR_LOOK && have_rel)
        SDL_SetRelativeMouseMode(SDL_FALSE);

    if (mode != SL_PTR_FREE) {
        /* SDL_SetWindowMouseGrab, not the legacy SDL_SetWindowGrab. The legacy
         * call is the mouse grab PLUS a keyboard grab whenever
         * SDL_HINT_GRAB_KEYBOARD is set, which is a second, unrelated claim on
         * the session that this code never wanted and never asked for. The
         * mouse-specific call is the narrower one and is what is meant here.
         *
         * NOT claimed: that the two behave differently on this backend. That
         * would need an A/B with a real pointer, which is the owner's, so it
         * was not run. This is the correct API to be calling either way. */
        if (w != NULL && !have_grab)
            SDL_SetWindowMouseGrab(w, SDL_TRUE);

        /* THE CONFINEMENT RECTANGLE - the last SDL-supported mechanism.
         *
         * SDL 2.30's header: "Confines the cursor to the specified area of a
         * window", it does NOT itself grab, and it applies while the window has
         * MOUSE focus. It is meant to be used alongside the grab, which is why
         * both are asserted here rather than one instead of the other.
         *
         * WINDOW-RELATIVE coordinates, not desktop ones: 0,0,w,h straight from
         * SDL_GetWindowSize. Re-applied whenever the window size changes, so a
         * resize cannot leave a barrier the wrong shape - which is also why the
         * comparison below tests all four fields rather than just presence.
         *
         * The return code is checked and reported ONCE, because it is the one
         * thing that distinguishes "this backend supports the barrier" from
         * "it does not", and that fork decides whether anything further is a
         * production change or an owner architecture decision. */
        want_rect.x = 0;
        want_rect.y = 0;
        want_rect.w = ww;
        want_rect.h = wh;
        if (w != NULL && ww > 0 && wh > 0
            && (have_rect == NULL
                || have_rect->x != 0 || have_rect->y != 0
                || have_rect->w != ww || have_rect->h != wh)) {
            static int rect_reported;
            if (SDL_SetWindowMouseRect(w, &want_rect) != 0) {
                if (!rect_reported) {
                    rect_reported = 1;
                    fprintf(stderr, "sightline input: mouse confinement rect "
                                    "REJECTED by the %s backend: %s\n",
                            SDL_GetCurrentVideoDriver() ?
                                SDL_GetCurrentVideoDriver() : "?",
                            SDL_GetError());
                }
            } else if (!rect_reported) {
                rect_reported = 1;
                fprintf(stderr, "sightline input: mouse confined to %dx%d "
                                "(rect accepted by the %s backend)\n",
                        ww, wh, SDL_GetCurrentVideoDriver() ?
                                SDL_GetCurrentVideoDriver() : "?");
            }
        }

    } else {
        /* BARRIER OFF FIRST, and unconditionally if one is present: releasing
         * the grab while a rect is still armed is the one ordering that could
         * strand the DESKTOP pointer inside our window rectangle. NULL is the
         * SDL-defined way to remove it. */
        if (w != NULL && have_rect != NULL) {
            if (SDL_SetWindowMouseRect(w, NULL) != 0)
                fprintf(stderr, "sightline input: could not clear the mouse "
                                "confinement rect: %s\n", SDL_GetError());
            /* Verify, rather than assume - this is the call that could trap
             * the player's desktop pointer if it silently failed. */
            if (SDL_GetWindowMouseRect(w) != NULL)
                fprintf(stderr, "sightline input: WARNING - mouse confinement "
                                "rect still set after clearing it\n");
        }
        if (w != NULL && have_grab)
            SDL_SetWindowMouseGrab(w, SDL_FALSE);
    }

    /* THE CURSOR. Hidden exactly where the GAME draws a pointer of its own -
     * POINT and LOOK - and visible everywhere else, which now includes the boot
     * chain (SKIP) as well as the released states. See the table at SL_PTR_FREE
     * for why those two classes differ by this one call.
     *
     * This is a VISIBILITY change and nothing else. Relative mode stays off in
     * both SKIP and POINT and the absolute position stays meaningful, which is
     * what the whole menu pointer depends on; hiding a cursor does not move it.
     *
     * Same boundary as the grab, not a second one - queried rather than cached,
     * like everything else here, so it settles when the screen class changes
     * and does nothing on the frames between. */
    if (mode == SL_PTR_POINT || mode == SL_PTR_LOOK) {
        if (SDL_ShowCursor(SDL_QUERY) != SDL_DISABLE)
            SDL_ShowCursor(SDL_DISABLE);
    } else {
        if (SDL_ShowCursor(SDL_QUERY) != SDL_ENABLE)
            SDL_ShowCursor(SDL_ENABLE);
    }

    /* g_grabbed is LOOK, not "confined". The transition flag it feeds exists
     * to make read_mouse throw away the one bogus delta that SDL's recentring
     * leaves behind, and only entering or leaving relative mode produces one -
     * arming or dropping a confinement barrier does not move the pointer. */
    grabbed_now = (mode == SL_PTR_LOOK);
    if (grabbed_now != g_grabbed)
        g_grab_just_changed = 1;
    g_grabbed = grabbed_now;
}

/* Did this device ask for anything at all this frame? Used only to decide
 * which producer owns the movement channels - see "LAST DEVICE WINS". */
static int intent_active(const sl_intent *in)
{
    return in->move_forward != 0.0f || in->move_strafe != 0.0f
        || in->look_yaw != 0.0f || in->look_pitch != 0.0f
        || in->move_forward_step || in->move_strafe_step
        || in->look_yaw_step || in->look_pitch_step
        || in->fire || in->aim || in->next_weapon || in->action || in->pause
        || in->confirm
        || in->dpad != 0;
}

/* One poll of the wheel state machine: -1, 0 or +1, and never the same notch
 * twice. See SL_WHEEL_HOLD_POLLS for why a notch is a pulse with a gap after
 * it rather than a single-poll flag. */
static int wheel_step(void)
{
    if (g_wheel_hold > 0) {
        if (--g_wheel_hold == 0)
            g_wheel_gap = SL_WHEEL_GAP_POLLS;
        return g_wheel_dir;
    }
    if (g_wheel_gap > 0) {
        g_wheel_gap--;
        return 0;
    }
    if (g_wheel_queue > 0)      { g_wheel_queue--; g_wheel_dir =  1; }
    else if (g_wheel_queue < 0) { g_wheel_queue++; g_wheel_dir = -1; }
    else                        { g_wheel_dir = 0; return 0; }
    g_wheel_hold = SL_WHEEL_HOLD_POLLS - 1;
    return g_wheel_dir;
}

/* Drop everything the wheel had queued. Called whenever the menu is not up, so
 * notches spun during play cannot arrive as menu steps later. */
static void wheel_reset(void)
{
    g_wheel_queue = 0;
    g_wheel_hold  = 0;
    g_wheel_gap   = 0;
    g_wheel_dir   = 0;
}

/* Sample the absolute pointer, once per poll. See the g_ptr_* block.
 *
 * `want` is the front end saying a cursor-driven menu is up. When it is not,
 * the sample is dropped AND the history is dropped, so the frame a menu opens
 * cannot be handed a motion delta accumulated while the player was aiming.
 *
 * Not sampled while captured: relative mode makes SDL_GetMouseState's position
 * meaningless (SDL recentres it), and the captured case is gameplay, which is
 * the accepted mouse-look path and is not this function's business. */
static void read_pointer(int want)
{
    SDL_Window *w = (SDL_Window *) g_win;
    int x = 0, y = 0, ww = 0, wh = 0;

    if (!want || w == NULL || !g_focused || g_grabbed
        || SDL_GetMouseFocus() != w) {
        g_ptr_valid = 0;
        g_ptr_have_last = 0;
        return;
    }

    SDL_GetMouseState(&x, &y);
    SDL_GetWindowSize(w, &ww, &wh);
    if (ww <= 0 || wh <= 0) {
        g_ptr_valid = 0;
        g_ptr_have_last = 0;
        return;
    }

    /* THE OWNERSHIP RULE, entire. The serial advances only on real motion, so
     * a pointer sitting still publishes the same serial forever and the
     * frontend layer leaves the selection alone - which is what lets the
     * keyboard and the pad move it and keep it. */
    if (!g_ptr_have_last) {
        g_ptr_have_last = 1;
    } else if (x != g_ptr_last_x || y != g_ptr_last_y) {
        g_ptr_motion++;
        g_ptr_owner = 1;               /* the mouse is pointing again */
    }
    g_ptr_last_x = x;
    g_ptr_last_y = y;

    g_ptr_x = x;
    g_ptr_y = y;
    g_ptr_w = ww;
    g_ptr_h = wh;
    g_ptr_valid = 1;
}

static int channel(float v)
{
    v = clamp_unit(v) * (float) SL_CHANNEL_MAX;
    if (v >  (float) SL_CHANNEL_MAX) return  SL_CHANNEL_MAX;
    if (v < -(float) SL_CHANNEL_MAX) return -SL_CHANNEL_MAX;
    return (int) v;
}

/* ---------------------------------------------------------------------------
 * map_kbm - keyboard and mouse.
 *
 * Produces three things: the N64 BUTTONS (a fixed map, style-blind, exactly as
 * for the pad), an N64 STICK contribution, and the four movement CHANNELS.
 * Which of the last two carries the input depends on what the game is doing:
 *
 *   menu          stick = full deflection from WASD/arrows; channels off.
 *                 This is what navigates both menu systems, and it is why no
 *                 menu code changes and no pointer ever touches a menu.
 *   aim mode      stick = NEUTRAL; the turn and pitch channels carry the mouse
 *                 and walk/strafe stay at 0. Right-click still reaches the game
 *                 as the N64 aim button, so aim mode, auto-aim, zoom and the
 *                 weapon state are still entirely the game's - only the
 *                 crosshair-from-stick displacement goes away, which is the
 *                 point: a mouse reports a rate, not a stick position.
 *   gameplay      stick = neutral; channels carry movement and look.
 *
 * SIGNS. Three of the four are self-evident from the game's own defaults
 * (analogWalk/Strafe/Turn = controlStickX/YSafe at bondview2.c:4830-4833, so
 * they share the stick's sense) and all four were confirmed by measurement -
 * see docs/decisions/native-input-signs.md. Stated plainly:
 *
 *   analogWalk    + = forward        analogStrafe  + = right
 *   analogTurn    + = turn right     analogPitch   + = look DOWN
 *
 * analogPitch is the one that surprises. Positive analogPitch drives
 * speedverta the same way moveData.speedVertaDown does (:5921 against :5924),
 * and speedVertaDown is what the game raises for C-UP - GoldenEye's default
 * vertical sense is the aircraft one. The player's own Look Up/Down option
 * flips it a few lines later (:5455-5464) and is left to do so: writing the
 * channel BEFORE that block is what keeps the option working instead of
 * duplicating it here.
 * ------------------------------------------------------------------------- */
static void map_kbm(const sl_intent *in, int menu, int menu_stick, int aim,
                    int look_upright,
                    unsigned short *out_b, float *out_sx, float *out_sy,
                    int *ch_walk, int *ch_strafe, int *ch_turn, int *ch_pitch)
{
    unsigned short b = in->dpad;
    float pitch = in->look_pitch;

    if (g_look_invert)
        pitch = -pitch;

    /* THE MOUSE OWNS ITS OWN PITCH - owner decision, 2026-09-01, replacing the
     * previous requirement that GoldenEye's Look Up/Down option govern it.
     *
     * That option is a CONTROLLER setting. It reaches pitch inside
     * bondviewProcessInput, which negates analogPitch, negates controlStickYRaw
     * and swaps speedVertaDown/Up when the player has selected UPRIGHT
     * (:4849 sets invertPitch to the NEGATION of the setting, :5579 acts on
     * invertPitch == 0). Everything the mouse produces passes through that
     * block, so selecting UPRIGHT reversed mouse pitch - the owner's "mouse UP
     * looks DOWN", and their "pitch still tracks the Look Up/Down setting".
     *
     * Pre-negating here cancels that block exactly, because this function's
     * ONE look output is derived from `pitch`:
     *
     *   *ch_pitch = -channel(pitch) -> analogPitch, which :5584 negates when
     *   the player has selected UPRIGHT. Emit -P, the block makes it +P.
     *
     * That now covers aim mode too, which is the simplification the 2026-09-03
     * routing change bought: pitch used to have a SECOND path here - the N64
     * stick, from which :5300 derived speedVertaUp/Down and :4869
     * controlStickYRaw - and the cancellation had to be argued separately for
     * it. The mouse no longer drives the stick in either mode, so there is one
     * expression to reason about instead of two.
     *
     * WHY IT LIVES HERE and not at the four-channel seam: the signal this uses
     * is one this layer already owns. map_kbm runs only for the keyboard/mouse
     * intent, and its output only reaches the game when live keyboard/mouse is
     * the owning producer. The pad goes through map_pad, which is untouched, so
     * a pad user still gets the option applied to both analogPitch and the
     * speedVerta swap.
     *
     * Narrower than the alternative in two ways: no token is added to
     * bondview2.c at all, so the matching build cannot be affected; and the
     * option is not disabled for anything but this one producer. */
    if (look_upright)
        pitch = -pitch;

    /* One physical control, one N64 button, always the same one - and the
     * game decides what each does under the selected style. Nothing here is
     * context-sensitive any more: the old `pause ? A : START` line is what made
     * Enter open the watch during play, and there is no PC key on START now
     * except Tab and Escape, which arrive as synthesised events instead. */
    if (in->fire)        b |= SL_BTN_Z;      /* mouse 1 / F  */
    if (in->aim)         b |= SL_BTN_R;      /* mouse 2 / Q  */
    if (in->next_weapon) b |= SL_BTN_A;      /* R            */
    if (in->action)      b |= SL_BTN_B;      /* E / Space    */
    if (in->confirm)     b |= SL_BTN_A;      /* Enter: accept, never START */
    if (in->pause)       b |= SL_BTN_START;  /* no keyboard source; kept for
                                              * the shared intent shape */

    *out_b = b;
    *out_sx = 0.0f;
    *out_sy = 0.0f;
    *ch_walk = *ch_strafe = *ch_turn = *ch_pitch = 0;

    if (menu) {
        /* A fixed deflection from the digital form of the axes, so that a key
         * is a key. WHICH deflection is the caller's (sl_input_live_poll):
         * SL_STICK_MAX for the front end's cursor, SL_WATCH_STICK for the
         * watch - see that constant for why the two menu systems differ. */
        *out_sx = (float) (in->move_strafe_step  * menu_stick);
        *out_sy = (float) (in->move_forward_step * menu_stick);
        return;
    }

    /* LOOK IS ONE PATH, AIMING OR NOT - owner decision, 2026-09-03.
     *
     * These two lines used to be reached only when aim mode was OFF; while it
     * was on, an earlier branch here wrote the mouse into the N64 STICK and
     * returned, and sl_input_live_poll withheld the channels. That is the
     * defect: a stick reports a POSITION and a mouse reports a RATE, and the
     * game's aim mode consumes stick POSITION. The consequences were all
     * visible in the owner's hands:
     *
     *   - the crosshair FLOATS, because bondview2.c:6256 offsets it from
     *     controlStickXRaw/YRaw, so one frame of mouse motion displaced the
     *     reticle instead of turning the view;
     *   - the view does not turn AT ALL until |stick| passes 60 (:5309, :5318)
     *     - a hard threshold no mouse should have - and then turns at a rate
     *       set by how far past 60 that single frame's delta landed;
     *   - the moment the mouse stops, the "stick" snaps to centre, so turning
     *     stops as a pulse rather than a stop.
     *
     * At SL_MOUSE_SENS 6, full deflection is 13.3 pixels in one poll (MEASURED,
     * SL_STICK_MAX/g_sens), so ordinary aiming motion sat pinned at the
     * extreme. Reducing the sensitivity would only move the cliff; the mismatch
     * is the routing, not the number.
     *
     * So keyboard/mouse look now goes through analogTurn/analogPitch in BOTH
     * modes, and the mouse-generated stick stays NEUTRAL - which is also what
     * leaves the crosshair centred, since :6256 reads that same neutral stick.
     * The game keeps aim mode itself: right-click is still SL_BTN_R above, and
     * the game still decides what aiming means. This is a LIVE KEYBOARD/MOUSE
     * divergence only. The pad goes through map_pad and map_pad_dual, neither
     * of which this function can reach, so GoldenEye's original floating
     * manual aim is exactly as it was for a controller.
     *
     * The game-side half of this is the seam in bondviewProcessInput, which
     * applies turn and pitch ONLY while aiming and leaves walk, strafe and
     * look-ahead where aim mode put them. See docs/divergences.md. */
    *ch_turn   =  channel(in->look_yaw);
    *ch_pitch  = -channel(pitch);

    if (aim) {
        /* Look axes only. Walking is aim mode's business and the game already
         * stops it; leaving these two at 0 is what keeps that unchanged. The
         * stick stays at the 0 set above - deliberately, see the crosshair. */
        return;
    }

    *ch_walk   =  channel(in->move_forward);
    *ch_strafe =  channel(in->move_strafe);
}

void sl_input_live_poll(void)
{
    sl_intent kbm, pad;
    unsigned short b, b2, bk;
    float sx, sy, sx2, sy2, kx, ky;
    int style, menu, menu_mode, menu_stick, aim, live, ch_on, ptr_mode, ptr_menu;
    int cw = 0, cs = 0, ct = 0, cp = 0;

    read_env();

    /* Live means: a window is up AND no recorded stream is loaded. One
     * definition, in the shim. Everything below that could reach the game
     * outside a recorded stream hangs off this. */
    live = sl_live_input_active();
    menu_mode = sl_game_menu_mode();
    menu = menu_mode != SL_MENU_NONE;
    aim  = sl_game_aim_mode(0) == 1;

    /* THE ONE MENU DEFLECTION, decided once per poll and shared by the keys
     * and the wheel so the two can never step a list differently. The watch
     * is a stepped list with an unlatched fast-scroll band that SL_STICK_MAX
     * lands in; the front end is a cursor integrator that clamps for itself.
     * See SL_WATCH_STICK. */
    menu_stick = (menu_mode == SL_MENU_WATCH) ? SL_WATCH_STICK : SL_STICK_MAX;

    /* THE POINTER STATE MACHINE, entire. Three screen classes, three modes,
     * and one decision per poll rather than a flag anyone can forget to clear.
     *
     *   not focused / SL_MOUSE=0 / no live input   -> FREE
     *   the boot / intro chain                     -> SKIP   (cursor visible)
     *   the interactive front end                  -> POINT  (game's crosshair)
     *   gameplay, once a click has armed it        -> LOOK
     *   the watch                                  -> FREE
     *
     * WHY FOCUS IS THE ONLY GATE ON THE CONFINED MENU MODES, where LOOK also
     * needs a click.
     * They are different claims on the session. LOOK hides the cursor and
     * turns the pointer into a stream of deltas, which is a thing a window
     * must be asked for - hence click-to-capture, and hence a focus loss
     * DISARMING it so that coming back needs another deliberate click. SKIP and
     * POINT leave the pointer's position meaningful and only stop it leaving a
     * window the player is already looking at, and demanding a click before the
     * first menu would be a pointless ceremony. All three are released the
     * moment focus goes, so alt-tab is not a trap in any of them.
     *
     * THE WATCH IS DELIBERATELY FREE, and unchanged. It is a menu inside a
     * level, it is navigated by the stick rather than a cursor
     * (options.c:566-642), and releasing the pointer there is behaviour that
     * was accepted long before this. It re-asserts LOOK with no second click
     * when the watch closes, exactly as it did.
     *
     * ONE CALL, ONE DECISION: set_pointer_mode reads back what SDL actually
     * has and only issues the calls that differ, so this does not churn the
     * grab per frame or per boot animation - it churns nothing until the
     * screen class changes. */
    /* Asked ONCE, and used for both the mode and the sampling gate below, so
     * the screen the pointer is mapped for and the screen the cursor is hidden
     * on can never disagree. */
    ptr_menu = live && sl_game_pointer_menu_active();

    if (!g_grab_wanted || !live || !g_focused)
        ptr_mode = SL_PTR_FREE;
    else if (menu_mode == SL_MENU_FRONT)
        ptr_mode = ptr_menu ? SL_PTR_POINT : SL_PTR_SKIP;
    else if (menu_mode == SL_MENU_NONE && g_capture_armed)
        ptr_mode = SL_PTR_LOOK;
    else
        ptr_mode = SL_PTR_FREE;
    set_pointer_mode(ptr_mode);

    /* AFTER set_pointer_mode, because g_grabbed is one of its conditions: a pointer
     * sampled before the grab was released for a menu would be a captured,
     * recentred position. Gated on the FRONT END saying a cursor menu is up,
     * so nothing about the pointer is read during play, during the watch, or
     * while a recorded stream is driving the game. */
    read_pointer(ptr_menu);

    memset(&kbm, 0, sizeof kbm);
    memset(&pad, 0, sizeof pad);
    read_keyboard(&kbm, menu);
    read_mouse(&kbm, menu);
    read_pad(&pad);

    /* Escape, raised by the SDL backend as an EVENT rather than read from the
     * keyboard state, so that holding it cannot repeat. It is not a quit key:
     * it synthesises the N64 button the game already uses to leave where you
     * are. START is the game's own pause trigger (bondview2.c:4839 tests the
     * START edge into trigger_solo_watch_menu, and START closes the watch
     * again through open_close_solo_watch_menu); B is the front end's cancel
     * (front.c:2315, :2403, :3240). Closing the window is still the way out. */
    if (g_esc_polls > 0) {
        if (g_esc_button == 0)
            g_esc_button = (sl_game_menu_mode() == SL_MENU_FRONT)
                         ? SL_BTN_B : SL_BTN_START;
        kbm.dpad |= g_esc_button;
        if (--g_esc_polls == 0)
            g_esc_button = 0;
    }

    /* TAB, on the same event path and the same countdown, but unconditional:
     * Tab is always START, in play and in the watch alike. Tab and Escape are
     * the only two PC keys that reach START now. */
    if (g_start_polls > 0) {
        kbm.dpad |= SL_BTN_START;
        g_start_polls--;
    }

    /* THE MENU CONFIRM. One left click over a cursor-driven front-end screen
     * becomes one N64 A edge, and that is the whole of the click path: the
     * menus already read A (front.c:2504 file select, :3376 mission select,
     * :3576 difficulty, :3928 briefing), already hit-test the cursor
     * themselves, and already enforce their own locked entries. Nothing is
     * duplicated here and no navigation is synthesised - the pointer moves the
     * game's own cursor (src/native/sl_menu_pointer.c) and this supplies the
     * button the game was already waiting for.
     *
     * The SAME edge advances the boot chain, where the screens have no cursor
     * but do read ANY_BUTTON (front.c:1534, :1763, :1950, :1998, :2071, :8220).
     * One click is one rising edge and therefore one screen: joyConsumeSamples
     * (joy.c:395) ORs `buttons1 & ~buttons2` over the sample ring, so the hold
     * below contributes an edge in exactly one frame and reads as HELD in every
     * frame after it. There is no second mechanism for the boot chain. */
    if (g_confirm_polls > 0) {
        kbm.dpad |= SL_BTN_A;
        g_confirm_polls--;
    }

    /* LAST DEVICE WINS - see the file header. The pad taking ownership is what
     * keeps every gamepad path, 2.x included, exactly as it was. */
    if (intent_active(&pad))
        g_owner = SL_OWNER_PAD;
    else if (intent_active(&kbm))
        g_owner = SL_OWNER_KBM;

    /* One style query per poll, shared by both mappings. -1 before a player
     * exists (the front end), which style_is_dual correctly calls not-dual. */
    style = g_retro ? -1 : sl_game_control_style(0);

    if (style_is_dual(style)) {
        map_pad_dual(&pad, style, &b, &sx, &sy, &b2, &sx2, &sy2);
    } else {
        map_pad(&pad, &b, &sx, &sy);
        /* Pad 2 exists so the game counts two controllers and therefore offers
         * the 2.x styles at all, but under a 1.x style nothing reads it and it
         * must stay neutral. */
        b2 = 0; sx2 = 0.0f; sy2 = 0.0f;
    }

    map_kbm(&kbm, menu, menu_stick, aim, sl_game_look_upright(),
            &bk, &kx, &ky, &cw, &cs, &ct, &cp);

    /* Merge. Buttons are a union - a player may hold a key and a pad button at
     * once and both should reach the game. The stick is not summable, so the
     * pad keeps it whenever it is deflected at all and the keyboard fills in
     * the neutral axes. */
    b |= bk;
    if (sx == 0.0f) sx = kx;
    if (sy == 0.0f) sy = ky;

    /* MOUSE WHEEL, menus only: one discrete vertical step per notch, on the
     * SAME virtual input W and Up produce - the menu deflection chosen above -
     * so the menus need no change: this is the STICK, and it is what steps the
     * watch menus, which have no cursor for a pointer to move. Keyboard wins a
     * tie; outside a menu the queue is discarded rather than banked. */
    if (menu) {
        int ws = wheel_step();
        if (ws != 0 && sy == 0.0f)
            sy = (float) (ws * menu_stick);

        /* THE MOUSE LOSES THE CURSOR HERE, and only here. The front end moves
         * its cursor from the STICK and from nothing else (front.c:1148
         * frontUpdateControlStickPosition reads joyGetStickX/Y), so a non-zero
         * merged stick in a menu IS keyboard or pad cursor navigation -
         * whether it came from WASD, the arrows, a pad stick or a wheel notch.
         * Buttons are deliberately not tested: pressing A does not move the
         * cursor, so confirming with a pad must not take the cursor away from
         * a mouse the player is still using. */
        if (sx != 0.0f || sy != 0.0f)
            g_ptr_owner = 0;
    } else {
        wheel_reset();
    }

    /* The four channels reach the game ONLY here, and only when every one of
     * these holds. bondviewProcessInput applies its own gameplay gates on top
     * (frozen path, watch, death, tank, controls locked); this decides the
     * narrower question of whether keyboard and mouse are the producer. */
    /* AIM IS NOT A GATE HERE ANY MORE (2026-09-03). It used to be - the mouse
     * was routed onto the N64 stick while aiming and the channels withheld -
     * and that is what made right-click aiming behave like a digital stick.
     * map_kbm now publishes turn and pitch in both modes and leaves walk and
     * strafe at 0 while aiming, and the game seam applies only those two while
     * insightaimmode is set, so the game keeps everything aim mode owns. */
    ch_on = live && g_owner == SL_OWNER_KBM && !menu;

    /* A RECORDED STREAM ALWAYS WINS, which is already the rule the pad follows
     * in osContGetReadData. It has to be said here explicitly because `live`
     * is 0 during every replay, so without this the two calls below would
     * publish sl_move_channels_set(0, ...) once per presented frame and clear
     * the movement sidecar's records between retraces - silently, and in a way
     * that looks exactly like a sidecar that failed to load. One predicate,
     * defined beside the sidecar in sl_ultra_shim.c, the same shape as
     * sl_live_input_active above. */
    if (!sl_move_replay_active())
        sl_move_channels_set(ch_on, cw, cs, ct, cp);

    /* THE LINEAR MOUSE LOOK CHANNEL, published beside the four and gated
     * harder than they are. ch_on already means "live keyboard/mouse is the
     * producer and no menu is up"; g_grabbed adds "the pointer is actually
     * captured", which is what makes this a MOUSE claim rather than a
     * keyboard one - kbm.look_yaw is written only by read_mouse (read_pad
     * fills a separate intent), and read_mouse returns before writing
     * anything when the pointer is not captured.
     *
     * DEGREES PER COUNT is derived from SL_MOUSE_SENS so the one sensitivity
     * knob still governs, at 0.025 deg per unit of it - 0.15 deg/count at the
     * default 6, or 1200 counts per 180 degrees. That was chosen to sit inside
     * the range the OLD path produced across its speed sweep (0.018 deg/count
     * crawling, 0.175 flat out - MEASURED), so overall feel is preserved while
     * the speed dependence that produced those two numbers is removed.
     *
     * Pitch is + = UP and final. The Look Up/Down controller option is not
     * applied to it - that is the standing "THE MOUSE OWNS ITS OWN PITCH"
     * decision, and it is why the game-side branch inverts nothing. */
    {
        float dpc = g_sens * 0.025f;
        int   on  = ch_on && g_linear_look && g_grabbed;

        if (!sl_move_replay_active())
            sl_mouse_look_set(on,
                              (float)  g_raw_dx * dpc,
                              (float) -g_raw_dy * dpc);
    }

    g_button  = b;
    g_stick_x = (signed char) clamp_stick(sx);
    g_stick_y = (signed char) clamp_stick(sy);
    g_button2  = b2;
    g_stick_x2 = (signed char) clamp_stick(sx2);
    g_stick_y2 = (signed char) clamp_stick(sy2);
    g_ready   = 1;

    if (getenv("SL_INPUT_DEBUG") != NULL) {
        static unsigned short last_b = 0xFFFF;
        static signed char last_x = 127, last_y = 127;
        static unsigned short last_b2 = 0xFFFF;
        static signed char last_x2 = 127, last_y2 = 127;
        static int last_ch[5] = { -1, -1, -1, -1, -1 };
        if (b != last_b || g_stick_x != last_x || g_stick_y != last_y
            || b2 != last_b2 || g_stick_x2 != last_x2 || g_stick_y2 != last_y2
            || ch_on != last_ch[0] || cw != last_ch[1] || cs != last_ch[2]
            || ct != last_ch[3] || cp != last_ch[4]) {
            fprintf(stderr, "sightline input: style=%d menu=%d aim=%d own=%d "
                            "grab=%d p1 button=%04x stick=(%d,%d) "
                            "p2 button=%04x stick=(%d,%d) "
                            "ch=%d walk=%d strafe=%d turn=%d pitch=%d\n",
                    style, menu, aim, g_owner, g_grabbed,
                    b, g_stick_x, g_stick_y,
                    b2, g_stick_x2, g_stick_y2,
                    ch_on, cw, cs, ct, cp);
            last_b = b; last_x = g_stick_x; last_y = g_stick_y;
            last_b2 = b2; last_x2 = g_stick_x2; last_y2 = g_stick_y2;
            last_ch[0] = ch_on; last_ch[1] = cw; last_ch[2] = cs;
            last_ch[3] = ct;    last_ch[4] = cp;
        }
    }
}

void sl_input_live_set_window(void *sdl_window)
{
    g_win = sdl_window;
}

void sl_input_live_focus(int has_focus)
{
    g_focused = has_focus != 0;
    /* Losing focus DISARMS the capture, not just suspends it. SDL drops its own
     * grab on focus loss anyway (measured: SDL_GetWindowGrab goes 1 -> 0 with
     * no call from us), so re-arming silently on the way back would hand the
     * pointer to a window the player may only have brushed past. Coming back
     * takes a deliberate click, which is the same contract as arriving. */
    if (!g_focused) {
        g_capture_armed = 0;
        /* NO STALE EDGE SURVIVES A FOCUS LOSS. The button may well be released
         * over another window, in which case the up edge below never arrives
         * and a mark left standing would kill mouse fire for the rest of the
         * run. The pointer history goes too, so coming back needs real motion
         * before hover resumes rather than inheriting wherever the desktop
         * pointer wandered. */
        g_lmb_menu_used = 0;
        g_confirm_polls = 0;
        g_ptr_valid = 0;
        g_ptr_have_last = 0;
        g_ptr_owner = 0;
    }
}

void sl_input_live_click(int button, int clicks)
{
    /* A click inside a focused window is the ONLY thing that takes the pointer.
     * Everything else about the grab is a consequence - see set_pointer_mode.
     * SL_MOUSE=0
     * opts out entirely and is checked there. Any button does this, exactly as
     * before - the button index below narrows only the MENU confirm. */
    if (g_focused)
        g_capture_armed = 1;

    if (button != SDL_BUTTON_LEFT || !g_focused)
        return;

    /* THE DOUBLE-CLICK GUARD, using SDL's own click counter rather than a
     * timer of ours. SDL_MOUSEBUTTONDOWN carries `clicks`, and a second press
     * close enough in time AND position to be a double click reports 2. One
     * click selects; the second edge of a double click must not then activate
     * whatever sits under the pointer on the menu the first click opened. A
     * deliberate second click on a different item is a different position, so
     * SDL reports it as 1 and it goes through. */
    if (clicks > 1)
        return;

    /* Device facts only up to here. This is the frontend's answer to "would a
     * button press do something on the screen that is up" - the five cursor
     * menus AND the boot chain, which has no cursor but does have the same
     * any-button skip. Nothing about the pointer's POSITION is tested: SDL
     * delivers button events only for the window that owns the pointer, so
     * arriving at all is the test, exactly as it is for the capture above. */
    if (!sl_live_input_active() || !sl_game_click_advance_active())
        return;

    g_confirm_polls = SL_CONFIRM_HOLD_POLLS;
    g_lmb_menu_used = 1;
}

void sl_input_live_release(int button)
{
    /* The up edge is what re-arms the next click, and it is the ONLY thing
     * that clears the menu mark. Holding the button down therefore confirms
     * exactly once however long it is held, and cannot fire either. */
    if (button == SDL_BUTTON_LEFT)
        g_lmb_menu_used = 0;
}

/* The absolute pointer, for src/native/sl_menu_pointer.c. Device facts only:
 * where the pointer is, how big the window the renderer drew into is, and a
 * serial that advances on real motion. What any of it MEANS is the front end's
 * business, and nothing in this file knows it. */
/* Is the MOUSE the thing currently pointing at the menu? See g_ptr_owner.
 *
 * The frontend layer asks this for ONE decision - whether entering a new menu
 * should re-place the game's cursor under the physical pointer - and must not
 * use it to weaken the per-frame rule, which is still "act only when the motion
 * serial changes". Returns 0 whenever there is no live pointer at all. */
int sl_input_pointer_owns(void)
{
    return g_ptr_valid && g_ptr_owner;
}

int sl_input_pointer_get(int *x, int *y, int *win_w, int *win_h,
                         unsigned *motion_serial)
{
    if (!g_ptr_valid)
        return 0;
    if (x)             *x             = g_ptr_x;
    if (y)             *y             = g_ptr_y;
    if (win_w)         *win_w         = g_ptr_w;
    if (win_h)         *win_h         = g_ptr_h;
    if (motion_serial) *motion_serial = g_ptr_motion;
    return 1;
}

void sl_input_live_escape(void)
{
    g_esc_polls = SL_ESC_HOLD_POLLS;
    g_esc_button = 0;                  /* decided on the next poll, in context */
}

void sl_input_live_start(void)
{
    g_start_polls = SL_ESC_HOLD_POLLS;
}

void sl_input_live_wheel(int notches)
{
    /* Queue, do not bank: a hard flick of the wheel arrives as many notches in
     * one event burst and the menu must not then free-run through the list. */
    g_wheel_queue += notches;
    if (g_wheel_queue >  SL_WHEEL_QUEUE_MAX) g_wheel_queue =  SL_WHEEL_QUEUE_MAX;
    if (g_wheel_queue < -SL_WHEEL_QUEUE_MAX) g_wheel_queue = -SL_WHEEL_QUEUE_MAX;
}

int sl_input_live_get(unsigned short *button, signed char *stick_x,
                      signed char *stick_y)
{
    if (!g_ready)
        return 0;
    if (button)  *button  = g_button;
    if (stick_x) *stick_x = g_stick_x;
    if (stick_y) *stick_y = g_stick_y;
    return 1;
}

int sl_input_live_get2(unsigned short *button, signed char *stick_x,
                       signed char *stick_y)
{
    if (!g_ready)
        return 0;
    if (button)  *button  = g_button2;
    if (stick_x) *stick_x = g_stick_x2;
    if (stick_y) *stick_y = g_stick_y2;
    return 1;
}

void sl_input_live_shutdown(void)
{
    g_capture_armed = 0;
    g_lmb_menu_used = 0;
    g_confirm_polls = 0;
    g_ptr_valid = 0;
    g_ptr_have_last = 0;
    g_ptr_owner = 0;
    set_pointer_mode(SL_PTR_FREE);
    /* The game outlives this call on some shutdown orders; leave the channels
     * explicitly withheld rather than holding the last frame's values. */
    sl_move_channels_set(0, 0, 0, 0, 0);
    g_win = NULL;
    while (g_npads > 0)
        SDL_GameControllerClose(g_pads[--g_npads]);
    g_pad_active = -1;
    g_pad_inited = 0;
    g_pad_rescan = 1;
    g_ready = 0;
}
#endif /* !__sgi */
