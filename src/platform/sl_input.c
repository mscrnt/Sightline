/**
 * sl_input.c - physical devices -> the game's input seams.
 *
 * THREE PRODUCERS, one poll (sl_input_live_poll):
 *
 *   the keyboard and mouse   the four native movement channels (walk /
 *                            strafe / turn / pitch, +/-70) and the linear
 *                            mouse-look channel; FIRE / AIM as N64 Z / R;
 *                            everything else as native ACTIONS
 *   the gamepad              the SAME four channels from its two sticks
 *                            (map_pad_modern - which stick carries which
 *                            pair is the STICK LAYOUT setting), FIRE / AIM
 *                            as Z / R, the rest as actions through the
 *                            binding registry's PAD slots, Start as START,
 *                            the d-pad as the d-pad
 *   a recorded stream        trace replay, in the shim; untouched here
 *
 * The N64 stick itself is neutral in play for every live producer; in a MENU
 * the keyboard's W/A/S/D / arrows and the pad's left stick deflect it (the
 * menus navigate on stick thresholds), A / Enter confirm, B / Escape back.
 *
 * WHAT LEFT ON 2026-09-20 (#63, owner decision: "Sightline will never get an
 * N64 controller hooked up and we aren't using emulation"): the ORIGINAL
 * controller profile - the pad as a virtual N64 pad interpreted by the
 * game's eight control styles (map_pad, map_pad_dual, the SL_CONTROLS=retro
 * knob, the style-dependent stick pair), BUTTON MODE (the pad's four fixed
 * buttons), and with them the control style as a setting: the game side is
 * pinned to 1.1 Honey by src/native/sl_settings_apply.c, and every mapping
 * is this layer's. The history of that path - the 2026-08-25 rewrite that
 * made the pad a faithful N64 map, the stick pair, the 2.x dual-pad styles -
 * is in git (45479b1b and before) and in docs/backlog.md; none of it runs.
 * The second virtual pad (sl_input_live_get2) is still presented, neutral,
 * so the game keeps counting two controllers exactly as before.
 *
 * FIRE and AIM still arrive as N64 BUTTONS (Z and R) and the pinned style
 * interprets them - under Honey Z is fire and R is aim, which is what every
 * binding assumes. Nothing here binds a physical control to a game action
 * by itself: the registry (sl_bindings.c) does, for both devices.
 *
 * BUTTON BIT LAYOUT is not inferred from the decomp. It is stated in the notes:
 *   goldeneye_docs/notes/GE Documentation/Main Menus/Cheat Menu/
 *       Button Cheat Codes.txt
 * ("The 2 byte button code masks are the same as usual"), giving, high bit
 * first: A B Z Start, D-up D-down D-left D-right, -- -- L R, C-up C-down
 * C-left C-right. include/PR/os.h agrees (CONT_A 0x8000 ... CONT_F 0x0001);
 * the note is the authority, the header is the corroboration.
 *
 * THE MOVEMENT CHANNELS (added 2026-09-01 for the keyboard and mouse, the
 * pad since #63). Every live device supplies FOUR NUMBERS directly to the
 * game's own movement channels - forward/back, strafe, yaw, pitch - at a
 * seam inside bondviewProcessInput that sits AFTER the control-style
 * branches have finished. See src/native/sl_move_channels.c for the
 * contract and src/game/bondview2.c (search sl_move_channels_get) for the
 * seam and its gates. Consequences worth stating:
 *
 *   - fire and aim still arrive as N64 BUTTONS (Z / R) and the pinned style
 *     interprets them. Nothing here binds a physical control to a semantic
 *     game action by itself.
 *   - aim mode stays the game's: the aim button arrives as N64 R and the
 *     game decides everything aiming means. The mouse does not BECOME the
 *     stick while aiming (it used to, and the game reads aim turn from stick
 *     extremes, bondview2.c:5309, and the crosshair offset from the raw
 *     stick, :6256, so a rate-reporting device was being read as a
 *     position-reporting one). Turn and pitch go through the same two
 *     channels while aiming as at any other time; walk and strafe do not, so
 *     aim mode still stops the player moving. The pad's sticks follow the
 *     same contract.
 *
 * LAST DEVICE WINS. The channels are published from whichever producer was
 * the last thing touched - the keyboard / mouse pair or the pad - and the
 * other's numbers are not summed in.
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
 * THE ACTION LAYER, added 2026-09-17 (#38), is the THIRD producer and the
 * first one that names a gameplay semantic. The two above carry N64 buttons
 * and movement NUMBERS; neither can express "interact but do not reload",
 * "reload but do not interact", "crouch without aiming", "previous weapon
 * without holding the cycle button while firing" or "one notch of scope
 * zoom", because the cartridge has no button for any of them - B is
 * contextual, crouch lives inside aim mode, the previous weapon is A + Z and
 * the zoom is a held C button. So those are ACTIONS (src/platform/sl_action.h:
 * INTERACT, RELOAD, CROUCH, WEAPON_PREVIOUS, WEAPON_NEXT, ZOOM_IN, ZOOM_OUT),
 * bound in a table (sl_action.c: E, R, Left Ctrl, 1, 2, and the wheel) and
 * published to the game as HELD / PRESSED per action through
 * src/native/sl_action_channels.c, the same set/get/peek shape as the
 * movement channels. The game consumes them at one seam in
 * bondviewProcessInput and one in lvlTick, writing the SAME moveData flags
 * the buttons write, so everything downstream is Rare's.
 *
 * THE BINDING REGISTRY (#46, 2026-09-18) replaced the table with something
 * the player edits: src/platform/sl_bindings.c - per action two keyboard/
 * mouse slots and two controller slots, persisted in config.ini, edited
 * from OPTIONS -> SETTINGS -> CONTROL -> BINDINGS and from the watch's
 * SIGHTLINE -> BINDINGS through one shared model (sl_bindings_editor.c).
 * With it, three things left the hard-coded map above and became actions:
 *   - WASD in PLAY are MOVE FORWARD / BACK / STRAFE LEFT / RIGHT, held
 *     levels written into the keyboard intent's move axes in
 *     sl_input_live_poll and reaching the game by the same channel() +/-70.
 *     In MENUS the keys stay fixed (read_keyboard): the stick the menus
 *     navigate on cannot be bound away.
 *   - F / mouse 1 and Q / mouse 2 are FIRE and AIM, levels written into the
 *     intent's fire / aim and reaching the game as the same N64 Z / R.
 *   - the pad's buttons and trigger halves (LT / RT as digital at the 8000
 *     threshold) raise the actions through the registry's PAD slots, FIRE /
 *     AIM included; the slots are seeded by the BUTTON LAYOUT presets
 *     (sl_bindings.c g_layouts) and edited like the keyboard's.
 * A CAPTURE (an editor waiting for the next key / button) owns the devices
 * for its duration: the wheel goes to it, clicks are its answer, and both
 * intents are neutralised until the captured source is released.
 *
 * THE WHEEL IS CONTEXTUAL, in this order, decided once per poll below:
 *   1. a menu is up            -> the stick pulse (unchanged, see MENUS)
 *   2. adjustable scoped aiming -> ZOOM_IN / ZOOM_OUT (sl_game_scoped_zoom_active)
 *   3. otherwise               -> WEAPON_PREVIOUS / WEAPON_NEXT
 * The layer asks the game one scalar for (2) and decides nothing by weapon id.
 * Since round 6 of #63 / #64 (2026-09-20) the pad's two BUMPERS take (2) and
 * (3) by the same rule - they are contextual sources in the registry
 * (sl_bindings_source_ctx), each holding a weapon-cycle row and a zoom row,
 * and the evaluator picks the row for the context in force; (1) never
 * reaches the registry's actions for any source. One rule, one flag.
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
 *   SL_MOUSE_SENS=f       look deflection per pixel of motion (default 6) -
 *                         the developer BASE; the player's MOUSE SENSITIVITY
 *                         and SCOPED SENSITIVITY percents (#50, the settings
 *                         store) multiply it at the gameplay look seam, see
 *                         mouse_sens
 *   SL_MOUSE_INVERT=1     invert mouse pitch: the developer override of the
 *                         native INVERT MOUSE Y state (sl_mouse_invert_y_get /
 *                         _set / _seed), the one switch the mouse has - for a
 *                         session no config governs (replay, headless, the
 *                         harness); in a player session the persisted config
 *                         is the authority and the variable is reported and
 *                         ignored - see "INVERT MOUSE Y" at read_mouse
 *   SL_LOOK_INVERT=1      invert GAMEPAD stick pitch - a developer override on
 *                         top of the game's own Look Up/Down option. STICK
 *                         ONLY: the mouse is not in it (#39, 2026-09-17; it
 *                         used to say "every device" and never reached the
 *                         linear mouse path, and on the fallback path it
 *                         stacked with SL_MOUSE_INVERT)
 *   SL_INPUT_DEBUG        print the synthesised pad whenever it changes, and
 *                         the native action states (held / pressed, the
 *                         wheel context) beside the game facts they move
 */
#ifndef __sgi
#include "sl_input.h"
#include "sl_action.h"
#include "sl_bindings.h"
#include "sl_settings.h"
#include "../sl_asset_override.h"   /* SL_PART_* - the canonical part ids the
                                     * pad snapshot is addressed by (#63);
                                     * host-clean, no ultra64 types */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The game's control style (src/native/sl_game_query.c), read for the
 * SL_INPUT_DEBUG line only since #63 pinned it to 1.1 Honey: no mapping
 * here depends on it any more. s32 is int on this -m32 target. */
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

/* "Is the pointer over the menu's image" - the 4:3 safe rect the front end's
 * hit tests live in - 0 or 1, answered 1 when there is no pointer at all.
 * Asked only where a click would confirm into a CURSOR menu: a click in the
 * band a wider aspect adds beside the image confirms nothing there, because
 * the game's cursor was deliberately left where the pointer crossed out of
 * the image (never clamped to the edge) and a confirm would activate that
 * item. Same file as the two above; the test is sl_menu_pointer_uv's own. */
extern int sl_game_pointer_over_menu(void);

/* THE WATCH'S POINTER CONSUMER (src/native/sl_watch_pointer.c, #40). The
 * solo watch has no cursor of its own, so unlike the front end a click there
 * cannot become an N64 A edge: the native layer hit-tests the sampled pointer
 * against the page it draws and acts on the item directly. This file hands it
 * the LEFT button's down and up edges and nothing else - the position comes
 * from the same sl_input_pointer_get every other pointer consumer reads. */
extern int  sl_watch_pointer_click(void);   /* 1 = the watch took it */
extern void sl_watch_pointer_release(void);

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

/* "Is the player aiming with an ADJUSTABLE scope right now", 0 or 1 (-1 with
 * no player), src/native/sl_game_query.c. The one game fact the wheel's
 * context needs; the test is the game's own (aiming AND the right-hand item's
 * cannot-crouch stat bit), never a weapon id held here. */
extern int sl_game_scoped_zoom_active(int player_num);

/* Publish the native action channels (src/native/sl_action_channels.c):
 * the level mask once per poll, and counted edges as they happen. */
extern void sl_action_channels_set(int active, unsigned held_mask);
extern void sl_action_channels_pulse(int action, int count);

/* The action WITNESS for SL_INPUT_DEBUG (src/native/sl_game_query.c): the
 * game facts the actions move - crouch, weapon, hand animation, scope zoom,
 * the nearest door - read into a string. Diagnostic only. */
extern int sl_game_action_witness(char *out, int n);

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
 *                                                      before the first click
 *   SKIP          YES            no        VISIBLE     the boot / intro chain
 *   POINT         YES            no        hidden      the interactive front end
 *   LOOK          YES           YES        hidden      gameplay, and the WATCH
 *                                                      once a click has armed
 *                                                      it (#40: the deltas
 *                                                      drive a drawn crosshair)
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
 * deadzone is only +/-5 of 80 (controlStickXSafe, bondview2.c:4810).
 *
 * SINCE #51 (2026-09-20) THIS IS THE DEFAULT, NOT THE VALUE: the two
 * gameplay deadzones are the player's (pad_look_deadzone / pad_move_deadzone
 * on the settings store, percents whose 15 is exactly this 5000 - see
 * pad_deadzone_raw), applied per ROLE by read_pad: the axes the STICK LAYOUT
 * routes to turn / pitch take the look deadzone, walk / strafe the move
 * deadzone. This constant still owns everything that is not a gameplay feel:
 * the "is this pad being touched" arbitration between two pads
 * (pad_touched) and the menu's digital stick step (pad_step's input). */
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
 * This is a PHYSICAL-device list. The DualSense and the Xbox pad are
 * ALTERNATE PRODUCERS of the one logical pad the game is driven by; the
 * second virtual N64 pad the shim presents is neutral and is nobody's.
 *
 * Why a list at all, when only one is read: the owner has both attached, and
 * "unplug one to use the other" is not an acceptable answer. See
 * sl_input_live_device_change in sl_input.h for the measured device churn
 * that made the previous single, once-probed slot fail outright. */
#define SL_MAX_PADS 8
static SDL_GameController *g_pads[SL_MAX_PADS];
/* The FAMILY of each opened pad (SL_PAD_FAMILY_*), classified ONCE at open
 * from SDL_GameControllerGetType - SDL's own mapping-database verdict, which
 * is the same source that decides whether the pad is a game controller at
 * all. Shifted with g_pads on removal; re-classified on the next rescan a
 * device event triggers, so a re-plugged pad is re-read. Never a name match. */
static int   g_pad_family[SL_MAX_PADS];
/* The driving pad's PHYSICAL state as of the last read_pad (#63), for the
 * watch's controller visualisation. Valid only for the poll that read it. */
static sl_pad_visual g_pad_vis;
static int   g_pad_vis_valid;
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
/* INVERT MOUSE Y - THE ONE NATIVE STATE, and the only thing that decides
 * whether mouse pitch is inverted (#39, 2026-09-17).
 *
 * Four things can invert a vertical look axis and they are kept on separate
 * devices, so changing one never changes another:
 *
 *   the game's Look Up/Down (PLAYER_OPTION_LOOK)  the PAD stick, and only it.
 *       Consumed in bondviewProcessInput; the mouse's two paths do not pass
 *       through it (linear: never did; fallback: cancelled at map_kbm).
 *   this state                                     the MOUSE, and only it.
 *       Applied EXACTLY ONCE, at the physical delta in read_mouse, upstream
 *       of both the linear channel and the fallback channel, so neither path
 *       can see it twice and both agree.
 *   SL_MOUSE_INVERT=1                              the developer override of
 *       THIS state (read_env, first poll) for a session the settings store
 *       does not govern (inactive: replay, headless, the harness); with the
 *       store active it is reported and ignored, absent it assigns nothing.
 *   SL_LOOK_INVERT=1                               the PAD stick only, a
 *       developer override in map_pad_modern. It is not applied to the
 *       mouse anywhere - map_kbm used to, which is what made the two env
 *       vars stack on the fallback path.
 *
 * PRECEDENCE (#41, corrected 2026-09-18), lowest to highest, each a distinct
 * entry point:
 *   built-in default (0)                 the zero-initialised state
 *   SL_MOUSE_INVERT                      read_env, first poll, if PRESENT and
 *                                        the settings store is INACTIVE
 *   persisted config (config.ini)        sl_mouse_invert_y_seed, at startup;
 *                                        with the store active the env is
 *                                        ignored (reported on stderr)
 *   an explicit UI change during the run sl_mouse_invert_y_set - wins for the
 *                                        run AND persists (the setter is the
 *                                        one path into the store)
 * The seed and the env never write the store; only the setter does. The #41
 * order had the env ABOVE the config; measured 2026-09-18, that let a variable
 * left in the shell hold the live state at 1 over a store holding 0, so the
 * menus' OFF changed nothing in the file and never survived a restart. */
static int   g_mouse_invert_y;
static int   g_mouse_invert_y_set_explicitly;
static int   g_look_invert;

/* THE RAW MOUSE COUNTS for this poll, in device counts, BEFORE the +/-1
 * clamp_unit that read_mouse applies on its way to the stick-unit channels.
 * That clamp is the first of three saturations on the old path (it pins at
 * SL_STICK_MAX/g_sens = 13.3 counts per poll at the default sensitivity), and
 * carrying the counts around it is what makes a fast flick turn the full
 * amount instead of the clamped amount. Set - not accumulated - every poll. */
static int   g_raw_dx, g_raw_dy;
static int   g_linear_look = 1;        /* SL_MOUSE_LINEAR_LOOK, default ON */

/* MOUSE SENSITIVITY (#50). "Is the player in an adjustable-scope state THIS
 * POLL" - the game's own predicate (sl_game_scoped_zoom_active: aim mode with
 * an item carrying the adjustable-zoom stat bit; plain aiming is NOT scoped),
 * asked ONCE per poll in sl_input_live_poll, before read_mouse, and shared by
 * its two consumers: the wheel's ZOOM context and the scoped percent below.
 * Menus are excluded there, as they are for the wheel. */
static int   g_scoped_now;

/* The EFFECTIVE mouse gain for this poll: the developer base (SL_MOUSE_SENS,
 * g_sens) times the player's MOUSE SENSITIVITY percent, times the SCOPED
 * SENSITIVITY percent while g_scoped_now holds. Read from the settings store
 * every poll (as pad_button_mode is), so a change from either editor is felt
 * on the next poll and there is no second copy to drift; an INACTIVE store
 * (trace replay, headless health, the input harness) answers the defaults,
 * 100 / 100, and 100 / 100.0f is exactly 1.0f, so the default is the pre-#50
 * arithmetic bit for bit. ONE factor for both axes: the two look lines below
 * already share g_sens, and the y axis differs from x by its sign convention
 * and the Invert Mouse Y flip only, both applied elsewhere. Used by the two
 * mouse look lines in read_mouse and the linear publish in
 * sl_input_live_poll, and by nothing else: the watch pointer
 * (watch_pointer_integrate), the front-end pointer (read_pointer), the
 * bindings capture, the buttons, the wheel and the pad's sticks never read
 * it. */
static float mouse_sens(void)
{
    float f = g_sens * ((float) sl_settings_get(SL_SET_MOUSE_SENSITIVITY) / 100.0f);
    if (g_scoped_now)
        f *= (float) sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY) / 100.0f;
    return f;
}

/* LOOK SENSITIVITY for the controller (#51): the store's percent as a factor
 * on the logical look pair (map_pad_modern), read every poll like mouse_sens
 * and by nothing else - not the move pair, not the menu stick, not the
 * watch's picture, not the mouse. No scoped percent of its own: the game's
 * curve already scales a stick's turn rate by fovy / 60 (bondview2.c:6532,
 * :6452), so a sniper zoom slows the pad exactly as it always has. A value
 * the store refuses cannot arrive; the clamp keeps a future range change
 * from ever handing the channel a zero or a negative gain. */
static float pad_look_sens(void)
{
    int p = sl_settings_get(SL_SET_PAD_LOOK_SENSITIVITY);
    if (p < SL_PAD_LOOK_SENS_MIN) p = SL_PAD_LOOK_SENS_MIN;
    if (p > SL_PAD_LOOK_SENS_MAX) p = SL_PAD_LOOK_SENS_MAX;
    return (float) p / 100.0f;
}

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

/* THE WHEEL IN PLAY: raw notches since the last poll, up and down counted
 * SEPARATELY and never summed, for the action layer (sl_action.h). Kept apart
 * from the menu queue above because the two have different contracts: the
 * menu pulse must return to centre between notches and drops the excess of
 * a spun wheel; an action notch is a counted edge and an up followed by a
 * down is two edges, not zero. Cleared every poll. Capped so a burst cannot
 * bank; the game can only act on one edge per tick anyway. */
#define SL_WHEEL_ACT_MAX 8
static int g_wheel_act_up, g_wheel_act_down;

/* THE WHEEL DURING A CAPTURE (#46): notches that arrive while a binding
 * editor is waiting for a source are that capture's, not the menu's and not
 * the actions' - counted here from sl_input_live_wheel and handed to
 * sl_bindings_capture_poll on the next poll. */
static int g_cap_wheel_up, g_cap_wheel_down;

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

/* The left button's DOWN state as a device fact (a press over a focused
 * window until its release), for the front end's slider bars (#50): the bar
 * follows the pointer while it is held, as the 007-mode bars follow the cursor
 * while A is held. Nothing else reads it - the click stays one A edge. */
static int g_lmb_held;

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
/* The second virtual pad. Always neutral since #63 retired the 2.x styles'
 * dual mapping; still presented so the game counts two controllers - see
 * the note in sl_input.h. */
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
    /* The env is the DEVELOPER OVERRIDE of the native Invert Mouse Y state
     * for a session NO CONFIG GOVERNS - trace replay, headless health, the
     * input harness: the settings store is inactive there and there is no
     * persisted value to displace. In a PLAYER session the store is active
     * and the persisted config is the ONE authority: the seed applied before
     * this poll (sl_mouse_invert_y_seed) stands, and a variable left in the
     * shell is reported and ignored. Absent, nothing is assigned either way.
     *
     * MEASURED 2026-09-18 (#46 owner replay): SL_MOUSE_INVERT=1, left in the
     * owner's shell from the #39 steps, displaced the config's 0 at every
     * launch (loaded mouse_invert_y=0 -> minv=1 at the first poll). The
     * watch's OFF then wrote nothing - the store already held 0 while the
     * live state was 1 - and the next launch re-applied the env, so INVERT
     * MOUSE Y "did not persist" while every other setting did. The store
     * and the state must never disagree; below the store, the env cannot
     * make them. See the declaration of g_mouse_invert_y. */
    v = getenv("SL_MOUSE_INVERT");
    if (v != NULL) {
        if (sl_settings_active())
            fprintf(stderr, "sightline input: SL_MOUSE_INVERT=%s ignored - the persisted"
                            " config governs this session (invert mouse y %s)\n",
                    v, g_mouse_invert_y ? "on" : "off");
        else if (!g_mouse_invert_y_set_explicitly)
            g_mouse_invert_y = strcmp(v, "0") != 0;
    }
    v = getenv("SL_LOOK_INVERT");
    g_look_invert = (v != NULL && strcmp(v, "0") != 0);

    /* Every style knob this layer ever had is gone: SL_STYLE / SL_KBM_STYLE /
     * SL_PAD_STYLE (the pre-2026-08-25 encoder) and SL_CONTROLS=retro (the
     * virtual N64 pad's faithful stick pair, retired with that pad on
     * 2026-09-20, #63). The game's control style is pinned to 1.1 Honey and
     * which thumb does what is the STICK LAYOUT and BUTTON LAYOUT settings. */
    if (getenv("SL_STYLE") != NULL || getenv("SL_KBM_STYLE") != NULL
        || getenv("SL_PAD_STYLE") != NULL || getenv("SL_CONTROLS") != NULL)
        fprintf(stderr, "sightline input: SL_STYLE/SL_KBM_STYLE/SL_PAD_STYLE/SL_CONTROLS "
                        "are gone - the control style is pinned; use the STICK LAYOUT "
                        "and BUTTON LAYOUT settings.\n");
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

    /* WASD IN MENUS ONLY (#46). In play the four movement keys are the
     * registry's MOVE FORWARD / BACK / STRAFE LEFT / RIGHT bindings (W/S/A/D
     * by default), evaluated with the other actions in sl_input_live_poll
     * and written into this intent there. In a MENU they are fixed: the
     * menus navigate from the stick, and the stick is what the player must
     * always be able to move - rebinding W to something else must not leave
     * them unable to leave Settings. So the menu deflection stays hard-wired
     * here beside the arrows, and only gameplay reads the registry. */
    if (menu) {
        if (k[SDL_SCANCODE_W]) { in->move_forward += 1.0f; in->move_forward_step = 1; }
        if (k[SDL_SCANCODE_S]) { in->move_forward -= 1.0f; in->move_forward_step = -1; }
        if (k[SDL_SCANCODE_A]) { in->move_strafe  -= 1.0f; in->move_strafe_step  = -1; }
        if (k[SDL_SCANCODE_D]) { in->move_strafe  += 1.0f; in->move_strafe_step  = 1; }
    }

    /* E AND R ARE NOT HERE ANY MORE (2026-09-17, #38). From 2026-09-01 E
     * raised `action` (N64 B: the cartridge's contextual use-OR-reload) and R
     * raised `next_weapon` (N64 A: the style's weapon cycle). Both now raise
     * native ACTIONS instead - E INTERACT, R RELOAD - through the binding
     * registry (sl_bindings.c), and reach the game as semantics rather than
     * as buttons, which is what lets E in front of nothing do nothing and R
     * in front of a door leave the door alone. Space is still B: the classic
     * contextual button stays on the keyboard, unchanged, and the pad's A/B
     * and X are untouched. The weapon cycle on the keyboard is now 1 / 2 and
     * the wheel; Left Ctrl crouches. See "THE ACTION LAYER" in the header.
     *
     * F AND Q ARE NOT HERE EITHER (#46): FIRE and AIM are registry actions
     * now (mouse 1 / F and mouse 2 / Q by default), evaluated with the rest
     * and written into `fire` / `aim` in sl_input_live_poll. Same N64 Z / R
     * downstream; only where the key is named changed. */
    if (k[SDL_SCANCODE_SPACE]) in->action = 1;      /* N64 B, contextual */
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

/* THE WATCH POINTER, INTEGRATED (#40). In the watch the pointer stays exactly
 * as gameplay holds it - relative mode, host cursor hidden, window grabbed -
 * and the game's own crosshair is drawn as the pointer (sl_watch_pointer.c).
 * So there is no absolute position to sample there; the position is the SUM
 * of the relative deltas, in window pixels, clamped to the window, published
 * through the same g_ptr_* fields the absolute sample fills on the front end
 * so that every consumer downstream (sl_input_pointer_get, the motion serial,
 * the ownership rule) is the one consumer it already was.
 *
 * One window pixel per mouse count, under the same per-platform dx/dy sign
 * the look path applies (see read_mouse) and NEVER the Invert Mouse Y state -
 * a pointer is not a pitch. Starts at the window centre the first time and
 * keeps its place across watch open/close after that (the owner's "keep the
 * last mouse position", 2026-09-07, applies here as it does to the front
 * end's cursor). */
static float g_wptr_x, g_wptr_y;
static int   g_wptr_init;

static void watch_pointer_integrate(int dx, int dy)
{
    SDL_Window *w = (SDL_Window *) g_win;
    int ww = 0, wh = 0;

    if (w == NULL)
        return;
    SDL_GetWindowSize(w, &ww, &wh);
    if (ww <= 0 || wh <= 0)
        return;

    /* The one bogus delta relative mode leaves behind on a transition, dropped
     * exactly as the look path drops it. A transition INTO the capture while
     * the watch is up (the arming click, or the click after a focus loss)
     * also re-seeds the position from the host cursor's last absolute sample,
     * so the crosshair appears under it rather than where it last was. */
    if (g_grab_just_changed) {
        g_grab_just_changed = 0;
        dx = 0;
        dy = 0;
        if (g_ptr_valid) {
            g_wptr_x = (float) g_ptr_x;
            g_wptr_y = (float) g_ptr_y;
            g_wptr_init = 1;
        }
    }

    if (!g_wptr_init) {
        g_wptr_init = 1;
        if (g_ptr_valid) {
            g_wptr_x = (float) g_ptr_x;
            g_wptr_y = (float) g_ptr_y;
        } else {
            g_wptr_x = (float) ww * 0.5f;
            g_wptr_y = (float) wh * 0.5f;
        }
    }
    g_wptr_x += (float) (dx * g_dx_sign);
    g_wptr_y += (float) (dy * g_dy_sign);
    if (g_wptr_x < 0.0f) g_wptr_x = 0.0f;
    if (g_wptr_y < 0.0f) g_wptr_y = 0.0f;
    if (g_wptr_x > (float) (ww - 1)) g_wptr_x = (float) (ww - 1);
    if (g_wptr_y > (float) (wh - 1)) g_wptr_y = (float) (wh - 1);

    /* Publish through the ONE pointer record. The serial advances on real
     * motion only, as read_pointer's does, so a still mouse leaves whatever
     * the keyboard highlighted alone. */
    if (g_ptr_valid && ((int) g_wptr_x != g_ptr_x || (int) g_wptr_y != g_ptr_y)) {
        g_ptr_motion++;
        g_ptr_owner = 1;               /* the mouse is pointing again */
    }
    g_ptr_x = (int) g_wptr_x;
    g_ptr_y = (int) g_wptr_y;
    g_ptr_w = ww;
    g_ptr_h = wh;
    g_ptr_last_x = g_ptr_x;
    g_ptr_last_y = g_ptr_y;
    g_ptr_have_last = 1;
    g_ptr_valid = 1;
}

/* Is the pointer CAPTURED (relative mode) right now - the watch draws its own
 * crosshair only then; uncaptured, the host cursor is the pointer. */
int sl_input_pointer_captured(void)
{
    return g_grabbed;
}


static void read_mouse(sl_intent *in, int menu_mode)
{
    int menu = menu_mode != SL_MENU_NONE;
    int dx = 0, dy = 0;
    float sens;
    unsigned mb = SDL_GetRelativeMouseState(&dx, &dy);

    /* Cleared on ENTRY, so that every early return below - a menu, an
     * uncaptured pointer - leaves the linear look channel at rest rather than
     * republishing the previous poll's motion. */
    g_raw_dx = 0;
    g_raw_dy = 0;

    /* THE WATCH, CAPTURED: the deltas are the pointer (#40). Consumed here so
     * they can neither reach the look channels nor be banked. */
    if (menu_mode == SL_MENU_WATCH && g_grabbed) {
        watch_pointer_integrate(dx, dy);
        return;
    }

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

    /* THE BUTTONS ARE NOT READ HERE ANY MORE (#46). Fire and aim are the
     * registry's FIRE / AIM actions (mouse 1 / F and mouse 2 / Q by default,
     * any of the five buttons or the wheel capturable), evaluated from the
     * device snapshot in sl_input_live_poll under the SAME two gates this
     * function used to apply - the pointer captured (above) and the press
     * not already spent by a menu (g_lmb_menu_used) - and written into
     * `fire` / `aim` there. The middle button, which used to be a third aim
     * alias here, is not a default any more (two slots per device). */
    (void) mb;

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
    /* MOUSE SENSITIVITY (#50): the one effective gain for this poll, on both
     * axes - see mouse_sens. The fallback (SL_MOUSE_LINEAR_LOOK=0) channel
     * takes it here; the linear channel takes the same factor at its publish
     * in sl_input_live_poll, so the two paths cannot disagree. */
    sens = mouse_sens();
    in->look_yaw += clamp_unit((float) dx * sens / (float) SL_STICK_MAX);
    if (dx >  SL_MOUSE_STEP_PX) in->look_yaw_step = 1;
    if (dx < -SL_MOUSE_STEP_PX) in->look_yaw_step = -1;

    /* INVERT MOUSE Y - THE ONE SIGN POINT. The physical delta is flipped here
     * and nowhere else, after the platform convention (which says what the
     * hardware did) and before BOTH consumers below - g_raw_dy for the linear
     * channel and look_pitch for the fallback channel - so the native setting
     * reaches the two paths once each and they cannot disagree. Ordinary look
     * and aiming look read the same two outputs, so aim mode obeys it too.
     * map_kbm and the linear publish in sl_input_live_poll apply NO invert of
     * their own; the game's Look Up/Down option is the pad's and is cancelled
     * for the mouse at map_kbm. See the declaration of g_mouse_invert_y. */
    if (g_mouse_invert_y)
        dy = -dy;

    /* THE RAW COUNTS, taken here: after both sign conventions and after the
     * pitch invert, so they carry exactly the orientation the two lines around
     * them use, and BEFORE clamp_unit, which is the saturation the linear path
     * exists to avoid. Not accumulated across polls - no banking, no carry;
     * the rejected c2fdaab9 approach is precisely what this must not become. */
    g_raw_dx = dx;
    g_raw_dy = dy;

    /* Screen coords grow downward, so mouse-down is +dy and means look DOWN. */
    in->look_pitch -= clamp_unit((float) dy * sens / (float) SL_STICK_MAX);
    if (dy >  SL_MOUSE_STEP_PX) in->look_pitch_step = -1;
    if (dy < -SL_MOUSE_STEP_PX) in->look_pitch_step = 1;
}

/* Deadzone one raw SDL axis and rescale the remainder to -1..1. The SHAPE
 * has not changed since the pad path was written: per axis (a square zone,
 * not a radial one), the remainder over (32767 - dz) so full travel still
 * reaches 1.0, +/-1 clamped. Only the size `dz` is a parameter now (#51). */
static float pad_axis(int raw, int dz)
{
    float f;

    if (raw >  dz)
        f = (float) (raw - dz);
    else if (raw < -dz)
        f = (float) (raw + dz);
    else
        return 0.0f;
    return clamp_unit(f / (32767.0f - (float) dz));
}

/* ---------------------------------------------------------------------------
 * THE STICK ROLES (#63 / #51): which of the four physical axes carries which
 * of the four channels under each STICK LAYOUT - the ONE table both the
 * deadzone (read_pad: look or move, by role) and the routing (map_pad_modern)
 * read, so a layout cannot deadzone one stick and route the other. The axes
 * are the intent's, sign-normalised (up / right = +): LX, LY, RX, RY.
 *
 *   DEFAULT          left  walk + strafe    right turn + pitch
 *   SOUTHPAW         right walk + strafe    left  turn + pitch
 *   LEGACY           left  walk + turn      right pitch + strafe
 *   LEGACY SOUTHPAW  right walk + turn      left  pitch + strafe
 * ------------------------------------------------------------------------- */
enum { SL_AXIS_LX = 0, SL_AXIS_LY, SL_AXIS_RX, SL_AXIS_RY };
enum { SL_ROLE_WALK = 0, SL_ROLE_STRAFE, SL_ROLE_TURN, SL_ROLE_PITCH };
static const unsigned char s_stick_roles[SL_STICK_LAYOUT_COUNT][4] = {
    { SL_ROLE_STRAFE, SL_ROLE_WALK,  SL_ROLE_TURN,   SL_ROLE_PITCH },   /* DEFAULT */
    { SL_ROLE_TURN,   SL_ROLE_PITCH, SL_ROLE_STRAFE, SL_ROLE_WALK  },   /* SOUTHPAW */
    { SL_ROLE_TURN,   SL_ROLE_WALK,  SL_ROLE_STRAFE, SL_ROLE_PITCH },   /* LEGACY */
    { SL_ROLE_STRAFE, SL_ROLE_PITCH, SL_ROLE_TURN,   SL_ROLE_WALK  }    /* LEGACY SOUTHPAW */
};

static int stick_role(int layout, int axis)
{
    if (layout < 0 || layout >= SL_STICK_LAYOUT_COUNT)
        layout = SL_STICK_LAYOUT_DEFAULT;                 /* anything unknown */
    return s_stick_roles[layout][axis];
}

/* THE DEADZONE IN RAW UNITS from the store's percent (#51): p x 5000 / 15,
 * so the default 15 is the compiled SL_PAD_DEADZONE exactly and every other
 * percent is within one percent of p percent of the 32767-unit travel
 * (p x 333.3 raw; 40 = 13333). Read every poll, so a change in either editor
 * is felt on the next poll; the store answers the default when inactive (a
 * replay, the harness), which is the pre-#51 arithmetic bit for bit. A
 * value the store refuses cannot arrive, but the clamp keeps the division
 * honest against any future range change. */
static int pad_deadzone_raw(int setting)
{
    int p = sl_settings_get(setting);
    if (p < SL_PAD_DEADZONE_MIN) p = SL_PAD_DEADZONE_MIN;
    if (p > SL_PAD_DEADZONE_MAX) p = SL_PAD_DEADZONE_MAX;
    return p * SL_PAD_DEADZONE / SL_PAD_DEADZONE_DEFAULT;
}

/* The deadzone for one physical axis under the layout in force: the look
 * pair's or the move pair's, by the axis's role. */
static int pad_axis_deadzone(int layout, int axis, int look_dz, int move_dz)
{
    int role = stick_role(layout, axis);
    return (role == SL_ROLE_TURN || role == SL_ROLE_PITCH) ? look_dz : move_dz;
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

/* SDL's controller type -> the family the editors label and the watch draws.
 * SDL_GameControllerGetType (2.0.12+; the linked SDL is 2.32.10) answers from
 * its mapping database and the device's vendor / product ids, so an
 * Xbox-compatible third-party pad reads XBOX and a DualShock reads PLAYSTATION
 * without this file knowing a single product name. Anything else SDL still
 * maps as a game controller is GENERIC: Modern input works on it exactly the
 * same, only the artwork falls back. */
static int pad_family_of(SDL_GameController *c)
{
    switch (SDL_GameControllerGetType(c)) {
    case SDL_CONTROLLER_TYPE_XBOX360:
    case SDL_CONTROLLER_TYPE_XBOXONE:
        return SL_PAD_FAMILY_XBOX;
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
    case SDL_CONTROLLER_TYPE_PS5:
        return SL_PAD_FAMILY_PLAYSTATION;
    default:
        return SL_PAD_FAMILY_GENERIC;
    }
}

const char *sl_input_pad_family_name(int family)
{
    switch (family) {
    case SL_PAD_FAMILY_XBOX:        return "XBOX";
    case SL_PAD_FAMILY_PLAYSTATION: return "PLAYSTATION";
    case SL_PAD_FAMILY_GENERIC:     return "GENERIC";
    default:                        return "NONE";
    }
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
        for (n = i; n < g_npads; n++) {
            g_pads[n] = g_pads[n + 1];
            g_pad_family[n] = g_pad_family[n + 1];
        }
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
        g_pad_family[g_npads] = pad_family_of(c);
        g_pads[g_npads++] = c;
        fprintf(stderr, "sightline input: gamepad \"%s\" (%d attached),"
                        " family %s (sdl type %d)\n",
                SDL_GameControllerName(c), g_npads,
                sl_input_pad_family_name(g_pad_family[g_npads - 1]),
                (int) SDL_GameControllerGetType(c));
    }
    if (g_npads == 0)
        fprintf(stderr, "sightline input: no gamepad found "
                        "(%d joystick(s) seen)\n", n);
}

void sl_input_live_device_change(void)
{
    g_pad_rescan = 1;
}

/* ---------------------------------------------------------------------------
 * SL_PAD_VIRTUAL / SL_PAD_SCRIPT - the developer's SYNTHETIC PAD (#63).
 *
 * Evidence tooling, getenv-gated, never on for a player. With no physical
 * controller on the machine the Modern path could be witnessed only in the
 * inputtest harness, which links this file against stubs and never draws.
 * This attaches one of SDL's own VIRTUAL joysticks - a real SDL device that
 * the ordinary open / rescan / classify / read path sees exactly as it sees a
 * USB pad - with the vendor and product id of a known controller, so SDL's
 * OWN classifier (SDL_GameControllerGetType, the same call pad_family_of
 * makes) decides the family from its database. Nothing in the family
 * detection is bypassed or special-cased for the virtual device.
 *
 *   SL_PAD_VIRTUAL=xbox      045E:02EA  (an Xbox One S pad)
 *   SL_PAD_VIRTUAL=ps        054C:0CE6  (a DualSense)
 *   SL_PAD_VIRTUAL=generic   0000:0000  (SDL: UNKNOWN -> family GENERIC)
 *
 *   SL_PAD_SCRIPT=<file>     one step per line, applied once the VI counter
 *                            (sl_frames_completed) reaches it and held until
 *                            the next line:
 *       <frame> <lx> <ly> <rx> <ry> <lt> <rt> [button ...]
 *                            axes -1..1 (SDL's sense: y + = down), triggers
 *                            0..1, buttons by SDL name: a b x y lb rb ls rs
 *                            start back guide du dd dl dr
 *
 * The virtual joystick's axis i / button i are SDL_CONTROLLER_AXIS_i /
 * _BUTTON_i under the automatic mapping SDL gives a virtual joystick whose
 * counts match the controller enums.
 * ------------------------------------------------------------------------- */
#define SL_VPAD_STEPS 512
struct sl_vpad_step { unsigned frame; float ax[6]; unsigned buttons; };
static SDL_Joystick *g_vpad_js;
static int           g_vpad_tried;
static struct sl_vpad_step g_vpad_step[SL_VPAD_STEPS];
static int           g_vpad_nsteps, g_vpad_cur = -1;

static void vpad_attach(void)
{
    const char *kind = getenv("SL_PAD_VIRTUAL");
    const char *script = getenv("SL_PAD_SCRIPT");
    SDL_VirtualJoystickDesc d;
    int idx;

    if (g_vpad_tried) return;
    g_vpad_tried = 1;
    if (kind == NULL || kind[0] == '\0') return;

    /* SDL drops joystick motion AWAY from centre while the window has no
     * keyboard focus (SDL_PrivateJoystickShouldIgnoreEvent) - and the
     * evidence window is parked off-screen, never focused. MEASURED
     * 2026-09-19: the scripted axes reached the device and read back 0 at
     * every poll until this hint was set. Only under the synthetic pad; a
     * player's focused window never needs it. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    memset(&d, 0, sizeof d);
    d.version  = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type     = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes    = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.nhats    = 0;
    d.name     = "sightline virtual pad";
    if (strcmp(kind, "xbox") == 0)      { d.vendor_id = 0x045E; d.product_id = 0x02EA; }
    else if (strcmp(kind, "ps") == 0)   { d.vendor_id = 0x054C; d.product_id = 0x0CE6; }
    else                                { d.vendor_id = 0;      d.product_id = 0;      }
    idx = SDL_JoystickAttachVirtualEx(&d);
    if (idx < 0) {
        fprintf(stderr, "sightline input: SL_PAD_VIRTUAL=%s could not attach: %s\n",
                kind, SDL_GetError());
        return;
    }
    g_vpad_js = SDL_JoystickOpen(idx);
    fprintf(stderr, "sightline input: SL_PAD_VIRTUAL=%s attached as device %d (%04x:%04x)%s\n",
            kind, idx, d.vendor_id, d.product_id, g_vpad_js ? "" : " (joystick open failed)");
    if (g_vpad_js != NULL) {
        /* Triggers at rest are raw -32768 on a full-range axis (see
         * vpad_drive); a fresh virtual axis reads 0 = half pulled. */
        SDL_JoystickSetVirtualAxis(g_vpad_js, SDL_CONTROLLER_AXIS_TRIGGERLEFT,  -32768);
        SDL_JoystickSetVirtualAxis(g_vpad_js, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    }
    g_pad_rescan = 1;

    if (script != NULL && script[0] != '\0') {
        FILE *f = fopen(script, "r");
        char line[256];
        if (f == NULL) {
            fprintf(stderr, "sightline input: SL_PAD_SCRIPT %s: cannot open\n", script);
            return;
        }
        while (fgets(line, sizeof line, f) != NULL && g_vpad_nsteps < SL_VPAD_STEPS) {
            struct sl_vpad_step *s = &g_vpad_step[g_vpad_nsteps];
            char *p = line, *tok;
            int n = 0;
            memset(s, 0, sizeof *s);
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
            while ((tok = strtok(p, " \t\r\n")) != NULL) {
                p = NULL;
                if (n == 0)      s->frame = (unsigned) strtoul(tok, NULL, 10);
                else if (n <= 6) s->ax[n - 1] = (float) atof(tok);
                else {
                    static const struct { const char *nm; int b; } bt[] = {
                        { "a", SDL_CONTROLLER_BUTTON_A }, { "b", SDL_CONTROLLER_BUTTON_B },
                        { "x", SDL_CONTROLLER_BUTTON_X }, { "y", SDL_CONTROLLER_BUTTON_Y },
                        { "lb", SDL_CONTROLLER_BUTTON_LEFTSHOULDER }, { "rb", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
                        { "ls", SDL_CONTROLLER_BUTTON_LEFTSTICK }, { "rs", SDL_CONTROLLER_BUTTON_RIGHTSTICK },
                        { "start", SDL_CONTROLLER_BUTTON_START }, { "back", SDL_CONTROLLER_BUTTON_BACK },
                        { "guide", SDL_CONTROLLER_BUTTON_GUIDE },
                        { "du", SDL_CONTROLLER_BUTTON_DPAD_UP }, { "dd", SDL_CONTROLLER_BUTTON_DPAD_DOWN },
                        { "dl", SDL_CONTROLLER_BUTTON_DPAD_LEFT }, { "dr", SDL_CONTROLLER_BUTTON_DPAD_RIGHT }
                    };
                    unsigned k;
                    for (k = 0; k < sizeof bt / sizeof bt[0]; k++)
                        if (strcmp(tok, bt[k].nm) == 0) s->buttons |= 1u << bt[k].b;
                }
                n++;
            }
            if (n >= 7) g_vpad_nsteps++;
        }
        fclose(f);
        fprintf(stderr, "sightline input: SL_PAD_SCRIPT %s: %d step(s)\n", script, g_vpad_nsteps);
    }
}

/* Apply the script's current step to the virtual device, once per poll. */
static void vpad_drive(void)
{
    extern unsigned sl_frames_completed(void);
    unsigned now;
    int i, next = g_vpad_cur;

    if (g_vpad_js == NULL || g_vpad_nsteps == 0) return;
    now = sl_frames_completed();
    while (next + 1 < g_vpad_nsteps && g_vpad_step[next + 1].frame <= now) next++;
    if (next < 0 || next == g_vpad_cur) return;
    g_vpad_cur = next;
    for (i = 0; i < 6; i++) {
        float v = g_vpad_step[next].ax[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        /* A virtual joystick's trigger axes are FULL-RANGE to SDL, which
         * maps them onto the controller's 0..32767 as (raw + 32768) / 2 -
         * so a raw 0 reads as a half pull (MEASURED: lt=16383 with the
         * script at 0). The script's 0..1 pull is therefore -32768..32767. */
        if (i >= 4) {
            if (v < 0.0f) v = 0.0f;
            SDL_JoystickSetVirtualAxis(g_vpad_js, i, (Sint16) (v * 65535.0f - 32768.0f));
        } else {
            SDL_JoystickSetVirtualAxis(g_vpad_js, i, (Sint16) (v * 32767.0f));
        }
    }
    for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
        SDL_JoystickSetVirtualButton(g_vpad_js, i, (g_vpad_step[next].buttons >> i) & 1u);
    fprintf(stderr, "sightline input: SL_PAD_SCRIPT step %d at frame %u: L(%.2f,%.2f) R(%.2f,%.2f) T(%.2f,%.2f) buttons=%04x\n",
            next, now, g_vpad_step[next].ax[0], g_vpad_step[next].ax[1],
            g_vpad_step[next].ax[2], g_vpad_step[next].ax[3],
            g_vpad_step[next].ax[4], g_vpad_step[next].ax[5], g_vpad_step[next].buttons);
}

/* The driving pad, this poll: the four stick axes into the intent, the
 * physical snapshot for the watch, A / B as the menu's accept / back
 * (map_pad_modern turns them into N64 A / B inside a menu and ignores them
 * in play, where the registry owns every button), the BACK button's mark,
 * Start, the d-pad. No fixed gameplay button lives here any more (the #46
 * BUTTON MODE ORIGINAL block left with the setting on 2026-09-20, #63): the
 * registry's PAD slots decide fire, aim and the rest, evaluated with the
 * other actions in sl_input_live_poll. */
static void read_pad(sl_intent *in, int layout)
{
    float lx, ly, rx, ry;
    int lt, rt, i, look_dz, move_dz;
    SDL_GameController *g_pad;

    g_pad_vis_valid = 0;
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

    vpad_attach();                       /* SL_PAD_VIRTUAL, once; no-op for a player */
    vpad_drive();                        /* SL_PAD_SCRIPT, this poll's step */
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
     * encoder instead.
     *
     * THE DEADZONE BY ROLE (#51): each physical axis takes the LOOK DEADZONE
     * or the MOVE DEADZONE according to the channel the STICK LAYOUT routes
     * it to (s_stick_roles - the routing's own table), so under SOUTHPAW the
     * left stick is the one the look deadzone shapes. Both are read from the
     * store here, once per poll. The intent therefore carries the TUNED
     * axes: everything downstream - the menu step, the last-device-wins
     * arbitration (intent_active), the watch's picture and the channels -
     * sees a stick inside its deadzone as neutral, which is the standing
     * rule ("stick drift below the deadzone never steals the game"). */
    look_dz = pad_deadzone_raw(SL_SET_PAD_LOOK_DEADZONE);
    move_dz = pad_deadzone_raw(SL_SET_PAD_MOVE_DEADZONE);
    lx = pad_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX),
                  pad_axis_deadzone(layout, SL_AXIS_LX, look_dz, move_dz));
    ly = pad_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY),
                  pad_axis_deadzone(layout, SL_AXIS_LY, look_dz, move_dz));
    rx = pad_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX),
                  pad_axis_deadzone(layout, SL_AXIS_RX, look_dz, move_dz));
    ry = pad_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY),
                  pad_axis_deadzone(layout, SL_AXIS_RY, look_dz, move_dz));
    lt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    rt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);

    in->move_forward += -ly;  if (!in->move_forward_step) in->move_forward_step = pad_step(-ly);
    in->move_strafe  +=  lx;  if (!in->move_strafe_step)  in->move_strafe_step  = pad_step(lx);
    in->look_yaw     +=  rx;  if (!in->look_yaw_step)     in->look_yaw_step     = pad_step(rx);
    in->look_pitch   += -ry;  if (!in->look_pitch_step)   in->look_pitch_step   = pad_step(-ry);

    /* The synthetic pad's read-back (SL_PAD_SCRIPT): what this poll read off
     * the device after a step, for three polls, so a log pairs the step
     * with the values the game received. */
    if (g_vpad_js != NULL && g_vpad_nsteps != 0) {
        static int show, last_step = -1;
        if (g_vpad_cur != last_step) { last_step = g_vpad_cur; show = 3; }
        if (show > 0) {
            show--;
            fprintf(stderr, "sightline input: SL_PAD_SCRIPT read: L(%.2f,%.2f) R(%.2f,%.2f) lt=%d rt=%d raw rx=%d\n",
                    lx, ly, rx, ry, lt, rt,
                    (int) SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX));
        }
    }

    /* THE PHYSICAL SNAPSHOT for the watch's controller picture (#63): what
     * the hands are doing, in SDL's own sense, addressed by canonical part.
     * Taken here so the picture and the game read the same device on the
     * same poll; the profile and the bindings decide nothing about it. */
    {
        static const struct { SDL_GameControllerButton b; unsigned int part; } vis[] = {
            { SDL_CONTROLLER_BUTTON_A,             SL_PART_FACE_SOUTH },
            { SDL_CONTROLLER_BUTTON_B,             SL_PART_FACE_EAST },
            { SDL_CONTROLLER_BUTTON_X,             SL_PART_FACE_WEST },
            { SDL_CONTROLLER_BUTTON_Y,             SL_PART_FACE_NORTH },
            { SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  SL_PART_LEFT_SHOULDER },
            { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SL_PART_RIGHT_SHOULDER },
            { SDL_CONTROLLER_BUTTON_LEFTSTICK,     SL_PART_LEFT_STICK },
            { SDL_CONTROLLER_BUTTON_RIGHTSTICK,    SL_PART_RIGHT_STICK },
            { SDL_CONTROLLER_BUTTON_START,         SL_PART_MENU },
            { SDL_CONTROLLER_BUTTON_BACK,          SL_PART_BACK },
            { SDL_CONTROLLER_BUTTON_GUIDE,         SL_PART_GUIDE },
            { SDL_CONTROLLER_BUTTON_DPAD_UP,       SL_PART_DPAD_UP },
            { SDL_CONTROLLER_BUTTON_DPAD_DOWN,     SL_PART_DPAD_DOWN },
            { SDL_CONTROLLER_BUTTON_DPAD_LEFT,     SL_PART_DPAD_LEFT },
            { SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    SL_PART_DPAD_RIGHT },
            { SDL_CONTROLLER_BUTTON_TOUCHPAD,      SL_PART_TOUCHPAD },
            { SDL_CONTROLLER_BUTTON_MISC1,         SL_PART_MUTE }
        };
        unsigned int k;

        memset(&g_pad_vis, 0, sizeof g_pad_vis);
        g_pad_vis.left_x = lx;   g_pad_vis.left_y = ly;
        g_pad_vis.right_x = rx;  g_pad_vis.right_y = ry;
        g_pad_vis.left_trigger  = clamp_unit((float) lt / 32767.0f);
        g_pad_vis.right_trigger = clamp_unit((float) rt / 32767.0f);
        if (g_pad_vis.left_trigger  < 0.0f) g_pad_vis.left_trigger  = 0.0f;
        if (g_pad_vis.right_trigger < 0.0f) g_pad_vis.right_trigger = 0.0f;
        for (k = 0; k < sizeof vis / sizeof vis[0]; k++)
            if (SDL_GameControllerGetButton(g_pad, vis[k].b))
                g_pad_vis.held |= 1u << vis[k].part;
        if (lt > SL_PAD_TRIG_ON) g_pad_vis.held |= 1u << SL_PART_LEFT_TRIGGER;
        if (rt > SL_PAD_TRIG_ON) g_pad_vis.held |= 1u << SL_PART_RIGHT_TRIGGER;
        g_pad_vis_valid = 1;
    }

    /* Menus: A accepts and B goes back, the modern expectation, as N64 A /
     * B - consumed by map_pad_modern only while a menu is up. In play the
     * registry owns both. */
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A))
        in->confirm = 1;
    if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B))
        in->action = 1;

    /* BACK ("Select" / "View" / "Create") USED TO MARK HERE, the same as F9.
     * It does not any more (#47, the owner: "the 'Select' button (the button
     * that currently marks) on controller? We wont need to mark with
     * controller"), so the button left this hard-wired block and became an
     * ordinary REGISTRY source (sl_bindings.c g_pad), which every preset
     * binds to TEXTURE SET. Two consequences worth stating: the pad can no
     * longer mark at all - F9 and F8 are unchanged and are still the way to
     * mark - and the button is now re-bindable like any other, so a player
     * who wants something else there changes it in the bindings editor.
     * Nothing here reaches the recorded stream, then or now. */

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

/* The physical pad read_pad chose this poll, or NULL - handed to the action
 * layer so a gamepad row in its table (none by default) reads the SAME pad
 * the N64 map reads, never a second one. */
static SDL_GameController *pad_active_handle(void)
{
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER) || g_npads == 0
        || g_pad_active < 0 || g_pad_active >= g_npads)
        return NULL;
    if (!SDL_GameControllerGetAttached(g_pads[g_pad_active]))
        return NULL;
    return g_pads[g_pad_active];
}

/* The same handle for the capture in sl_bindings.c (#46), typed void* so
 * that file's header stays SDL-free for the editors. */
void *sl_input_active_pad(void)
{
    return pad_active_handle();
}

int sl_input_pad_family(void)
{
    if (pad_active_handle() == NULL) return SL_PAD_FAMILY_NONE;
    return g_pad_family[g_pad_active];
}

int sl_input_pad_visual(sl_pad_visual *out)
{
    if (out == NULL) return 0;
    if (!g_pad_vis_valid || pad_active_handle() == NULL) {
        memset(out, 0, sizeof *out);
        return 0;
    }
    *out = g_pad_vis;
    return 1;
}

/* ---------------------------------------------------------------------------
 * mapping - the pad onto the game's seams
 *
 * HISTORY, briefly, because two rewrites live in git under this heading.
 * Until 2026-08-25 this layer decoded every device into an abstract intent
 * and RE-ENCODED it per control style, which flattened the eight styles into
 * one feel. From 2026-08-25 the pad was a FIXED map onto the physical N64
 * pad (left stick -> stick, right stick -> C cluster, RT / RB -> Z, LT /
 * LB -> R, A / B / X / Start / d-pad straight through; map_pad, and
 * map_pad_dual for the 2.x two-controller styles) and the GAME's chosen
 * style decided what each button did - authentic, and digital-looking
 * under 1.1. From #63 (2026-09-19) a MODERN profile put the pad on the
 * native channels instead, and on 2026-09-20 the owner retired the N64
 * profile, the eight styles and BUTTON MODE outright ("Sightline will never
 * get an N64 controller hooked up and we aren't using emulation"). What is
 * left is the one function below.
 * ------------------------------------------------------------------------- */

/* Deflection past which an analog look axis counts as a C-button press: the
 * keyboard / mouse FALLBACK path (map_kbm) still raises C bits from the
 * mouse when the linear look channel is off. */
#define SL_LOOK_C_ON 0.47f

/* ---------------------------------------------------------------------------
 * map_pad_modern - the pad as a modern dual-stick controller.
 *
 * WHERE THE STICKS GO. Both sticks become the four native channels - the
 * SAME seam keyboard and mouse use (map_kbm below, the NATIVE MOVEMENT block
 * in bondviewProcessInput):
 *
 *   the MOVE pair    -> walk / strafe   continuous, +/-70 at full deflection,
 *                                       proportional below it - never W/A/S/D's
 *                                       digital +/-70 step
 *   the LOOK pair    -> turn / pitch    continuous, +/-70 at full deflection
 *
 * WHICH STICK IS WHICH is the STICK LAYOUT setting (sl_settings.h
 * SL_STICK_LAYOUT_*, read every poll so a change in either editor is live
 * on the next poll), Halo's four:
 *
 *   DEFAULT          left  walk + strafe    right turn + pitch
 *   SOUTHPAW         right walk + strafe    left  turn + pitch
 *   LEGACY           left  walk + turn      right pitch + strafe
 *   LEGACY SOUTHPAW  right walk + turn      left  pitch + strafe
 *
 * The intent's four axes are read_pad's: move_forward / move_strafe are the
 * LEFT stick (up = +, right = +) and look_yaw / look_pitch the RIGHT (right
 * = +, up = +); the layout picks which physical axis feeds which channel
 * and nothing else changes - the game's Look Up/Down option and
 * SL_LOOK_INVERT flip the PITCH CHANNEL, whichever stick carries it.
 *
 * The look pair reaches the camera through analogTurn / analogPitch and the
 * cartridge's OWN stick curve (sl_move_channels.c: "a RATE ... correct for a
 * stick, which reports a POSITION held over time"), at exactly the magnitude
 * the native direct-look channel already defines - full stick = the
 * channel's 70 = the game's full turn rate. It NEVER becomes a C button: no
 * U/D/L/R_CBUTTONS bit is raised here on any deflection, and the N64 stick
 * stays neutral in play, so aim mode's crosshair stays centred exactly as it
 * does for the mouse. Nothing from the mouse's tuning reaches it - not
 * mouse_sensitivity, not the scoped percent, not Invert Mouse Y. ITS OWN
 * TUNING (#51, 2026-09-20) is the store's pad_look_deadzone /
 * pad_move_deadzone (read_pad, per axis by role) and pad_look_sensitivity
 * (here, on the logical look pair after the routing), all defaulting to the
 * arithmetic this function had before them, bit for bit.
 *
 * BUTTONS are the registry's (sl_bindings.c PAD slots, seeded by the BUTTON
 * LAYOUT presets); the poll feeds FIRE and AIM into `fire` / `aim` and they
 * leave here as the same N64 Z / R the keyboard's F / Q become. Start is
 * START. THE D-PAD IS THE REGISTRY'S IN PLAY (round 6 of #63 / #64,
 * 2026-09-20): its four directions are pad sources like any button (DEFAULT
 * zoom in / out on up / down, the weapon cycle on left / right), so in play
 * no N64 d-pad bit leaves here - under the pinned 1.1 Honey the cartridge
 * reads D-up/down as C-up/down (bondview2.c:5427 look, :5500 the scope's
 * zoom) and D-left/right as the C strafes, and a direction bound to ZOOM IN
 * would otherwise have zoomed twice and looked up as well. In a MENU the
 * d-pad is the d-pad (the menus' fixed navigation, which the registry never
 * touches), the LEFT stick is the menu stick whatever the layout (the menus
 * are not a gameplay feel), A is N64 A (accept) and B is N64 B (back); the
 * channels are off. No physical control is mapped to an action in this
 * file - the registry owns that.
 * ------------------------------------------------------------------------- */
static int channel(float v);

static void map_pad_modern(const sl_intent *in, int menu, int menu_stick,
                           int aim, int layout,
                           unsigned short *out_b, float *out_sx, float *out_sy,
                           int *ch_walk, int *ch_strafe, int *ch_turn,
                           int *ch_pitch)
{
    unsigned short b = menu ? in->dpad : 0;              /* the d-pad: menus only (see above) */
    float lx = in->move_strafe, ly = in->move_forward;   /* left stick, up / right = + */
    float rx = in->look_yaw,    ry = in->look_pitch;     /* right stick, up / right = + */
    float walk, strafe, turn, pitch;

    if (in->fire)  b |= SL_BTN_Z;        /* the registry's FIRE  */
    if (in->aim)   b |= SL_BTN_R;        /* the registry's AIM   */
    if (in->pause) b |= SL_BTN_START;

    *out_sx = 0.0f;
    *out_sy = 0.0f;
    *ch_walk = *ch_strafe = *ch_turn = *ch_pitch = 0;

    if (menu) {
        if (in->confirm) b |= SL_BTN_A;
        if (in->action)  b |= SL_BTN_B;
        *out_b = b;
        *out_sx = (float) (in->move_strafe_step  * menu_stick);
        *out_sy = (float) (in->move_forward_step * menu_stick);
        return;
    }
    *out_b = b;

    /* THE STICK LAYOUT: which physical axis feeds which channel - the roles
     * table read_pad deadzoned by (s_stick_roles), DEFAULT for anything
     * unknown. */
    {
        const float axis[4] = { lx, ly, rx, ry };
        float role_v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        int a;
        for (a = 0; a < 4; a++)
            role_v[stick_role(layout, a)] = axis[a];
        walk = role_v[SL_ROLE_WALK]; strafe = role_v[SL_ROLE_STRAFE];
        turn = role_v[SL_ROLE_TURN]; pitch = role_v[SL_ROLE_PITCH];
    }

    /* LOOK SENSITIVITY (#51): one factor on the LOGICAL look pair - after
     * the layout has said which stick it is, after that stick's deadzone
     * (read_pad), before the sign conventions below and before channel()'s
     * +/-70 clamp. 100 / 100.0f is exactly 1.0f, so the default is the
     * pre-#51 arithmetic bit for bit; a higher percent reaches the channel's
     * 70 (the game's full turn rate through its own curve) at a smaller
     * deflection and never past it. The move pair takes no gain: full stick
     * is the game's full speed and nothing else would be honest. */
    {
        float s = pad_look_sens();
        turn  *= s;
        pitch *= s;
    }

    if (g_look_invert)
        pitch = -pitch;

    /* + = look DOWN on the channel (see map_kbm's sign note); the game's
     * Look Up/Down option is applied by the game itself, as for any pad. */
    *ch_turn  =  channel(turn);
    *ch_pitch = -channel(pitch);
    if (aim)
        return;                           /* aim mode owns walking, as for the mouse */
    *ch_walk   = channel(walk);
    *ch_strafe = channel(strafe);
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
/* SL_POINTER_PROBE (#45, a developer witness): "<frame>:<x>,<y>[,click|,down|,up];..."
 * - at each named pumped frame the pointer is taken from the list instead of
 * SDL, in window pixels, and an optional click is the same confirm edge a
 * real left click raises (a front-end confirm, or the watch's click while
 * the watch is open), pressed and released; ",down" presses and holds (the
 * next probed moves drag a bar, #50) and ",up" releases. The focus tests are bypassed for a probed frame, so
 * the mapping chain (window -> the renderer's presented rectangle -> the
 * game's logical space) can be exercised from a bounded run without a
 * desktop. Each entry fires once. Never set in a player session. */
#define SL_PROBE_MAX 32
static struct { unsigned frame; int x, y, click, done; } g_probe[SL_PROBE_MAX];
static int g_probe_n = -1;

static void probe_parse(void)
{
    const char *s = getenv("SL_POINTER_PROBE");
    g_probe_n = 0;
    while (s != NULL && *s != '\0' && g_probe_n < SL_PROBE_MAX) {
        unsigned f; int x, y, c = 0, n = 0;
        if (sscanf(s, "%u:%d,%d%n", &f, &x, &y, &n) < 3 || n == 0) break;
        s += n;
        if (strncmp(s, ",click", 6) == 0) { c = 1; s += 6; }
        else if (strncmp(s, ",down", 5) == 0) { c = 2; s += 5; }   /* press, held (#50: a bar drag) */
        else if (strncmp(s, ",up", 3) == 0) { c = 3; s += 3; }     /* the release */
        g_probe[g_probe_n].frame = f; g_probe[g_probe_n].x = x;
        g_probe[g_probe_n].y = y; g_probe[g_probe_n].click = c;
        g_probe[g_probe_n].done = 0; g_probe_n++;
        if (*s == ';') s++;
    }
}

/* A probe holds from the frame it fires in until the next probe fires (the
 * input is polled more than once per frame, and the ordinary path would
 * otherwise drop the sample again before the front end reads it - and, on
 * a window that never had the focus (the parked evidence window), drop it
 * between two probes as well, so the second probe read as a fresh baseline
 * rather than a move and never set the cursor; measured 2026-09-20 on the
 * #52 pointer controls). The click rides the first poll only. */
static unsigned g_probe_hold_frame = 0xFFFFFFFFu;
static int      g_probe_hold_x, g_probe_hold_y;

/* Is a probed sample the pointer right now? The watch draws its crosshair
 * for a probed pointer as it does for a captured one, so a bounded run on a
 * window that never had the focus - and so never the capture - can
 * photograph where the crosshair lands. Never true in a player session (no
 * probe list, no probe). */
int sl_input_pointer_probed(void)
{
    return g_probe_hold_frame != 0xFFFFFFFFu;
}

static int probe_take(int *x, int *y, int *click)
{
    extern unsigned sl_frames_completed(void);
    unsigned now;
    int i;
    if (g_probe_n < 0) probe_parse();
    if (g_probe_n == 0) return 0;
    now = sl_frames_completed();
    for (i = 0; i < g_probe_n; i++) {
        if (!g_probe[i].done && g_probe[i].frame <= now) {
            int ww = 0, wh = 0;
            g_probe[i].done = 1;
            *x = g_probe[i].x; *y = g_probe[i].y; *click = g_probe[i].click;
            g_probe_hold_frame = now; g_probe_hold_x = *x; g_probe_hold_y = *y;
            if (g_win != NULL) SDL_GetWindowSize((SDL_Window *) g_win, &ww, &wh);
            fprintf(stderr, "sightline input: pointer probe f%u -> window (%d,%d) of %dx%d%s\n",
                    now, *x, *y, ww, wh,
                    *click == 1 ? " + click" : *click == 2 ? " + down" : *click == 3 ? " + up" : "");
            return 1;
        }
    }
    if (g_probe_hold_frame != 0xFFFFFFFFu) {
        *x = g_probe_hold_x; *y = g_probe_hold_y; *click = 0;
        return 2;                       /* held: the position, no fresh motion */
    }
    return 0;
}

static void read_pointer(int want)
{
    SDL_Window *w = (SDL_Window *) g_win;
    int x = 0, y = 0, ww = 0, wh = 0;
    int probed = 0, probe_click = 0;

    if (w != NULL)
        probed = probe_take(&x, &y, &probe_click);   /* 1 fresh, 2 held, 0 none */

    if (!probed && (!want || w == NULL || !g_focused || g_grabbed
        || SDL_GetMouseFocus() != w)) {
        g_ptr_valid = 0;
        g_ptr_have_last = 0;
        return;
    }

    if (!probed)
        SDL_GetMouseState(&x, &y);
    SDL_GetWindowSize(w, &ww, &wh);
    if (probed == 1) {
        if (probe_click == 1 || probe_click == 2) {
            /* The click is judged at the PROBED position, so publish it
             * first (the same fields are written again below): the band
             * test the live click makes reads the published pointer. */
            if (ww > 0 && wh > 0) {
                g_ptr_x = x; g_ptr_y = y; g_ptr_w = ww; g_ptr_h = wh;
                g_ptr_valid = 1;
            }
            if (sl_live_input_active() && sl_game_menu_mode() == SL_MENU_WATCH)
                (void) sl_watch_pointer_click();
            else if (sl_live_input_active() && sl_game_click_advance_active()
                     && !(sl_game_pointer_menu_active() && !sl_game_pointer_over_menu()))
                g_confirm_polls = SL_CONFIRM_HOLD_POLLS;
        }
        /* A probed CLICK is a press AND its release, as a real click is (a
         * press left held would make every later probed move a drag on
         * whatever track or bar it landed on - measured 2026-09-19); ",down"
         * leaves it held for a drag and ",up" releases it. */
        if (probe_click == 1 || probe_click == 3) {
            sl_watch_pointer_release();
            g_lmb_held = 0;
        } else if (probe_click == 2) {
            g_lmb_held = 1;
        }
        /* a probed sample is always motion: the serial must advance so the
         * frontend layer acts on it */
        g_ptr_have_last = 1;
        g_ptr_last_x = x - 1;
        g_ptr_last_y = y - 1;
    }
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

    /* NO NATIVE INVERT HERE (#39, 2026-09-17). `pitch` arrives from read_mouse
     * with the native Invert Mouse Y already applied at its one sign point;
     * SL_LOOK_INVERT used to be applied again on this line, which put a second
     * mouse invert on the fallback path (and none on the linear path, which
     * never passes through here). SL_LOOK_INVERT is the pad's (map_pad_modern)
     * and the mouse has exactly one switch, upstream. The only negation left
     * below is the cancellation of the game's own block. */

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
     * the game still decides what aiming means. The pad follows the same
     * contract through map_pad_modern (#63): its look pair also goes through
     * analogTurn / analogPitch while aiming, and its N64 stick stays neutral.
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
    sl_action_state act[SL_ACT_COUNT];
    unsigned act_held = 0, act_pressed = 0;
    int act_on, wheel_ctx, stick_layout;
    int pw = 0, ps = 0, pt = 0, pp = 0;   /* the pad's channels */
    int kbm_on;

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
     *   the watch, once a click has armed it       -> LOOK   (#40, see below)
     *   the watch before that                      -> FREE
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
     * THE WATCH KEEPS THE CAPTURE (#40, owner request 2026-09-17: the OS
     * cursor stays hidden and captured as in gameplay, and a small copy of
     * the game's own crosshair is the pointer). Relative mode therefore stays
     * on across open and close - no re-grab, no bogus delta, no second click
     * - and the deltas are integrated into the pointer record by read_mouse
     * (watch_pointer_integrate) instead of reaching the look channels. Before
     * the first click, or after a focus loss, the watch is FREE like any
     * uncaptured screen: the host cursor is the pointer and read_pointer
     * samples its absolute position. The keyboard and the wheel still drive
     * the STICK exactly as before (options.c:566-642).
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
    else if ((menu_mode == SL_MENU_NONE || menu_mode == SL_MENU_WATCH)
             && g_capture_armed)
        ptr_mode = SL_PTR_LOOK;     /* the watch keeps gameplay's capture, #40 */
    else
        ptr_mode = SL_PTR_FREE;
    set_pointer_mode(ptr_mode);

    /* AFTER set_pointer_mode, because g_grabbed is one of its conditions: a pointer
     * sampled before the grab was released for a menu would be a captured,
     * recentred position. Gated on the FRONT END saying a cursor menu is up
     * OR the watch being open while the pointer is NOT captured (#40: before
     * the first click, or after a focus loss, the host cursor is the pointer
     * there and its absolute position is the sample; captured, the watch
     * pointer is integrated in read_mouse instead), so nothing about the
     * pointer is read during play or while a recorded stream drives the game. */
    if (!(live && menu_mode == SL_MENU_WATCH && g_grabbed))
        read_pointer(ptr_menu || (live && menu_mode == SL_MENU_WATCH));
    /* else: the captured watch - read_mouse publishes the record this poll,
     * and read_pointer must not invalidate it first (measured: it did, and
     * the motion serial never advanced, so hover never followed the pointer). */

    /* THE ONE SCOPED PREDICATE for this poll (#50), asked before read_mouse
     * so the scoped percent and the wheel's ZOOM context below read the same
     * answer: adjustable scoped aiming, out of a menu - the game's own test
     * (sl_game_scoped_zoom_active), never a weapon list. */
    g_scoped_now = !menu && sl_game_scoped_zoom_active(0) == 1;

    memset(&kbm, 0, sizeof kbm);
    memset(&pad, 0, sizeof pad);
    read_keyboard(&kbm, menu);
    read_mouse(&kbm, menu_mode);
    /* THE STICK LAYOUT (#63), read every poll so a change in either editor is
     * live on the next poll; the store answers DEFAULT when inactive. */
    stick_layout = sl_settings_get(SL_SET_PAD_STICK_LAYOUT);
    read_pad(&pad, stick_layout);

    /* THE ACTION LAYER - see the file header. One device snapshot, one
     * registry evaluation, one publish. Evaluated on EVERY poll, menu or not,
     * so the edge history stays continuous (a key held across a watch
     * open/close is one press, not two); PUBLISHED only while live and out
     * of a menu, and the channel side drops every pending edge the moment it
     * goes inactive.
     *
     * BEFORE THE MAPPING (#46), because the movement, fire and aim actions
     * feed the intents the mapping reads: MOVE FORWARD / BACK / STRAFE LEFT
     * / RIGHT become the keyboard intent's move axes in play (the menu's
     * stick is fixed - read_keyboard), FIRE / AIM become `fire` / `aim` on
     * the keyboard/mouse intent from their KBM sources and on the pad intent
     * from their PAD sources. Downstream nothing changed: the same channel()
     * +/-70, the same N64 Z / R.
     *
     * The wheel's context, in the order the header states: a menu already
     * consumed the notches (the action snapshot gets none); otherwise
     * adjustable scoped aiming owns them, otherwise ordinary play does. The
     * raw notch counters are cleared here whichever way they went, so
     * nothing banks across polls. */
    wheel_ctx = g_scoped_now ? SL_WHEEL_CTX_SCOPED : SL_WHEEL_CTX_PLAY;
    {
        sl_action_devices dev;
        int i;

        memset(&dev, 0, sizeof dev);
        dev.keys          = SDL_GetKeyboardState(NULL);
        /* Mouse buttons reach the actions under exactly the two gates
         * read_mouse applied to fire and aim before #46: the pointer is
         * captured and no menu is up; and a LEFT press a menu already spent
         * (g_lmb_menu_used) is not a press until the button comes back up. */
        dev.mouse_buttons = (g_grabbed && !menu) ? SDL_GetMouseState(NULL, NULL) : 0u;
        if (g_lmb_menu_used)
            dev.mouse_buttons &= ~SDL_BUTTON(SDL_BUTTON_LEFT);
        dev.wheel_up      = menu ? 0 : g_wheel_act_up;
        dev.wheel_down    = menu ? 0 : g_wheel_act_down;
        dev.wheel_ctx     = wheel_ctx;
        dev.pad           = pad_active_handle();
        g_wheel_act_up = 0;
        g_wheel_act_down = 0;

        /* A CAPTURE IN PROGRESS (#46) owns the devices: the editors asked
         * for the next key / button, so this poll's input is editor data and
         * nothing else. The wheel notches go to it instead of the queue (see
         * sl_input_live_wheel), and both intents are neutralised below. */
        if (sl_bindings_capture_active()) {
            (void) sl_bindings_capture_poll(g_cap_wheel_up, g_cap_wheel_down);
        }
        g_cap_wheel_up = 0;
        g_cap_wheel_down = 0;

        sl_action_eval(&dev, act);

        /* HOLD / TOGGLE (#56): the CROUCH and SPRINT modes, on the evaluated
         * states and before anything reads them. `gameplay` is the same
         * predicate the publish below uses (live, no menu), so a press in
         * the watch or the front end can never flip a latch, and a control
         * held across a menu's close finds its raw memory already down. */
        sl_action_modes_apply(act, live && !menu);

        if (!menu) {
            if (act[SL_ACT_MOVE_FORWARD].held_kbm) { kbm.move_forward += 1.0f; kbm.move_forward_step = 1; }
            if (act[SL_ACT_MOVE_BACK].held_kbm)    { kbm.move_forward -= 1.0f; kbm.move_forward_step = -1; }
            if (act[SL_ACT_STRAFE_LEFT].held_kbm)  { kbm.move_strafe  -= 1.0f; kbm.move_strafe_step  = -1; }
            if (act[SL_ACT_STRAFE_RIGHT].held_kbm) { kbm.move_strafe  += 1.0f; kbm.move_strafe_step  = 1; }
        }
        if (act[SL_ACT_FIRE].held_kbm) kbm.fire = 1;
        if (act[SL_ACT_AIM].held_kbm)  kbm.aim  = 1;
        if (act[SL_ACT_FIRE].held_pad) pad.fire = 1;
        if (act[SL_ACT_AIM].held_pad)  pad.aim  = 1;

        act_on = live && !menu;
        for (i = 0; i < SL_ACT_COUNT; i++) {
            if (act[i].held)    act_held    |= 1u << i;
            if (act[i].pressed) act_pressed |= 1u << i;
        }
        sl_action_channels_set(act_on, act_held);
        for (i = 0; i < SL_ACT_COUNT; i++)
            if (act[i].pressed)
                sl_action_channels_pulse(i, act[i].pressed);

        /* #47. THE TEXTURE CYCLE is consumed HERE and published to the game
         * nowhere: the setting is the renderer's, not the simulation's, and
         * sl_action_channels_pulse drops it at its own SL_ACTCH_COUNT bound.
         * It takes `act_on` - the same "live input and no menu is up"
         * predicate the publish takes - so a press cannot land while the
         * watch, the front end or a binding capture owns input; and because
         * the edge comes from the evaluator's raw level memory rather than a
         * private `static int held`, a button still down as a menu closes
         * produces no edge on the frame it closes. */
        if (act_on && act[SL_ACT_TEXTURE_CYCLE].pressed) {
            extern void sl_textures_cycle(void);
            sl_textures_cycle();
        }
    }

    /* While a capture waits, or the key it just took is still down, the
     * menus see NOTHING from the keyboard or the pad: the captured key must
     * not also move the cursor, confirm or go back on the tick it landed,
     * and a click meanwhile is capture data (sl_input_live_click). */
    if (sl_bindings_capture_blocking()) {
        memset(&kbm, 0, sizeof kbm);
        memset(&pad, 0, sizeof pad);
        g_confirm_polls = 0;
    }

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

    /* LAST DEVICE WINS - see the file header. */
    if (intent_active(&pad))
        g_owner = SL_OWNER_PAD;
    else if (intent_active(&kbm))
        g_owner = SL_OWNER_KBM;

    /* The game's style, for the witness line only (pinned to 1.1 Honey by
     * sl_settings_apply.c; -1 before a player exists, the front end). */
    style = sl_game_control_style(0);

    /* The pad: the sticks reach the game through the channels below and
     * never through a virtual pad; the pinned style sees one neutral stick.
     * Pad 2 is presented neutral so the game keeps counting two controllers
     * exactly as it did. */
    map_pad_modern(&pad, menu, menu_stick, aim, stick_layout, &b, &sx, &sy,
                   &pw, &ps, &pt, &pp);
    b2 = 0; sx2 = 0.0f; sy2 = 0.0f;

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
    kbm_on = live && g_owner == SL_OWNER_KBM && !menu;
    ch_on  = kbm_on;

    /* The pad (#63) is a producer of the four channels too, under the same
     * last-device-wins rule - when the pad owns the poll its channels are
     * the ones published, and the keyboard's are not summed in. */
    if (live && g_owner == SL_OWNER_PAD && !menu) {
        ch_on = 1;
        cw = pw; cs = ps; ct = pt; cp = pp;
    }

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
     * Since #50 the base is scaled by the player's MOUSE SENSITIVITY percent
     * and, in an adjustable scope, the SCOPED SENSITIVITY percent too
     * (mouse_sens; 100 / 100 is exactly the number above).
     *
     * Pitch is + = UP and final. The Look Up/Down controller option is not
     * applied to it - that is the standing "THE MOUSE OWNS ITS OWN PITCH"
     * decision, and it is why the game-side branch inverts nothing. The
     * native Invert Mouse Y is not applied here either: g_raw_dy already
     * carries it from read_mouse's one sign point (#39). The `-` below is the
     * screen-to-look convention (screen y grows downward), not an invert. */
    {
        float dpc = mouse_sens() * 0.025f;
        /* kbm_on, not ch_on: a MODERN pad owning the channels is not a mouse
         * claim, and its look goes through the four above, never here. */
        int   on  = kbm_on && g_linear_look && g_grabbed;

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
        static int last_minv = -1, last_upright = -1;
        static int last_msens = -1, last_ssens = -1, last_scoped = -1;
        /* The two vertical-invert facts beside the pad (#39): minv is the
         * native Invert Mouse Y state in force, upright the game's own Look
         * Up/Down option - one line shows both, so a witness can prove which
         * one a pitch sign followed. Printed on change like everything else.
         * msens / ssens (#50) are the two percents in force and scoped the
         * predicate that selects the second, so a log proves which gain a
         * turn was made under. sticks (#63) is the STICK LAYOUT in force and
         * family SDL's verdict for the driving pad, so a log proves which
         * routing a stick deflection went through. */
        static int last_layout = -1, last_family = -1;
        /* psens / pdz / mdz (#51): the controller's look gain and the two
         * deadzones in force, so a log proves which tuning a stick
         * deflection's channel value went through. */
        static int last_psens = -1, last_pdz = -1, last_mdz = -1;
        int upright = sl_game_look_upright();
        int msens = sl_settings_get(SL_SET_MOUSE_SENSITIVITY);
        int ssens = sl_settings_get(SL_SET_SCOPED_MOUSE_SENSITIVITY);
        int psens = sl_pad_tune_get(SL_PAD_TUNE_LOOK_SENS);
        int pdz = sl_pad_tune_get(SL_PAD_TUNE_LOOK_DEADZONE);
        int mdz = sl_pad_tune_get(SL_PAD_TUNE_MOVE_DEADZONE);
        int family = sl_input_pad_family();
        if (b != last_b || g_stick_x != last_x || g_stick_y != last_y
            || b2 != last_b2 || g_stick_x2 != last_x2 || g_stick_y2 != last_y2
            || ch_on != last_ch[0] || cw != last_ch[1] || cs != last_ch[2]
            || ct != last_ch[3] || cp != last_ch[4]
            || g_mouse_invert_y != last_minv || upright != last_upright
            || msens != last_msens || ssens != last_ssens || g_scoped_now != last_scoped
            || stick_layout != last_layout || family != last_family
            || psens != last_psens || pdz != last_pdz || mdz != last_mdz) {
            fprintf(stderr, "sightline input: style=%d menu=%d aim=%d own=%d "
                            "grab=%d p1 button=%04x stick=(%d,%d) "
                            "p2 button=%04x stick=(%d,%d) "
                            "ch=%d walk=%d strafe=%d turn=%d pitch=%d "
                            "minv=%d upright=%d "
                            "msens=%d ssens=%d scoped=%d "
                            "sticks=%s family=%s psens=%d pdz=%d mdz=%d\n",
                    style, menu, aim, g_owner, g_grabbed,
                    b, g_stick_x, g_stick_y,
                    b2, g_stick_x2, g_stick_y2,
                    ch_on, cw, cs, ct, cp,
                    g_mouse_invert_y, upright,
                    msens, ssens, g_scoped_now,
                    sl_stick_layout_name(stick_layout),
                    sl_input_pad_family_name(family), psens, pdz, mdz);
            last_b = b; last_x = g_stick_x; last_y = g_stick_y;
            last_b2 = b2; last_x2 = g_stick_x2; last_y2 = g_stick_y2;
            last_ch[0] = ch_on; last_ch[1] = cw; last_ch[2] = cs;
            last_ch[3] = ct;    last_ch[4] = cp;
            last_minv = g_mouse_invert_y; last_upright = upright;
            last_msens = msens; last_ssens = ssens; last_scoped = g_scoped_now;
            last_layout = stick_layout; last_family = family;
            last_psens = psens; last_pdz = pdz; last_mdz = mdz;
        }

        /* THE ACTION LINE, on the same knob: the action states this poll
         * published, beside the game facts they are supposed to move, so one
         * log answers both "was E pressed" and "did the door start opening".
         * Printed when an action is held or pressed, when the wheel context
         * changes, and while the witness itself is changing (a door in
         * motion, a hand mid-reload, a scope zooming), so the consequence of
         * a press is on the lines that follow it. */
        {
            static unsigned last_held = 0xFFFFu;
            static int last_ctx = -1, last_on = -1;
            static int last_cmode = -1, last_smode = -1;
            static char last_wit[160];
            char wit[160];
            int i;
            /* cmode / smode (#56): the CROUCH / SPRINT modes in force (0
             * HOLD, 1 TOGGLE), and +L on an action whose latch is set, so
             * a log proves whether a level came from a held control or
             * from the latch. */
            int cmode = sl_settings_get(SL_SET_CROUCH_MODE);
            int smode = sl_settings_get(SL_SET_SPRINT_MODE);

            wit[0] = '\0';
            (void) sl_game_action_witness(wit, (int) sizeof wit);
            if (act_pressed != 0 || act_held != last_held || wheel_ctx != last_ctx
                || act_on != last_on || strcmp(wit, last_wit) != 0
                || cmode != last_cmode || smode != last_smode) {
                fprintf(stderr, "sightline action: on=%d ctx=%s held=%02x pressed=%02x cmode=%d smode=%d",
                        act_on, wheel_ctx == SL_WHEEL_CTX_SCOPED ? "scoped" : "play",
                        act_held, act_pressed, cmode, smode);
                for (i = 0; i < SL_ACT_COUNT; i++)
                    if (act[i].held || act[i].pressed || sl_action_latched(i))
                        fprintf(stderr, " %s%s%s%s", sl_action_name(i),
                                act[i].held ? "+H" : "", act[i].pressed ? "+P" : "",
                                sl_action_latched(i) ? "+L" : "");
                fprintf(stderr, " | %s\n", wit);
                last_held = act_held; last_ctx = wheel_ctx; last_on = act_on;
                last_cmode = cmode; last_smode = smode;
                strcpy(last_wit, wit);
            }
        }
    }
}

void sl_input_live_set_window(void *sdl_window)
{
    g_win = sdl_window;
}

int sl_mouse_invert_y_get(void)
{
    return g_mouse_invert_y;
}

/* THE ONE PERSISTENCE PATH for Invert Mouse Y (#41): UI -> this setter ->
 * the one state -> the settings store, which writes only if the value
 * changed. Both the front end's Controls row and the watch's row call this
 * and nothing else, so neither writes a file and there is no second copy to
 * drift. The store is a plain extern here for the same reason sl_game_* are:
 * this class does not include src/native, and the store's header is
 * host-clean so its id is spelled through it rather than as a literal. */
void sl_mouse_invert_y_set(int on)
{
    g_mouse_invert_y = on != 0;
    g_mouse_invert_y_set_explicitly = 1;
    sl_settings_set(SL_SET_MOUSE_INVERT_Y, g_mouse_invert_y);
}

/* The config's value at startup - a SEED, below the env override and below
 * any explicit set, exactly where the built-in default used to sit. */
void sl_mouse_invert_y_seed(int on)
{
    if (!g_mouse_invert_y_set_explicitly)
        g_mouse_invert_y = on != 0;
}

/* MOUSE SENSITIVITY / SCOPED SENSITIVITY (#50): the two editors' view of the
 * store and their one way to change it. No live copy: the store IS the state
 * (mouse_sens reads it every poll), so a step is felt on the next poll and
 * the two menus cannot disagree. A step lands on the SL_MOUSE_SENS_STEP grid
 * (a hand-edited 105 steps to 110 / 100, not 115 / 95) and clamps to the
 * bounds; the store refuses anything outside them anyway. */
int sl_mouse_sens_get(int scoped)
{
    int v = sl_settings_get(scoped ? SL_SET_SCOPED_MOUSE_SENSITIVITY : SL_SET_MOUSE_SENSITIVITY);
    if (v < SL_MOUSE_SENS_MIN || v > SL_MOUSE_SENS_MAX)
        return SL_MOUSE_SENS_DEFAULT;
    return v;
}

void sl_mouse_sens_step(int scoped, int dir)
{
    int v = sl_mouse_sens_get(scoped);
    if (dir == 0)
        return;
    v += dir < 0 ? -SL_MOUSE_SENS_STEP : SL_MOUSE_SENS_STEP;
    v = (v / SL_MOUSE_SENS_STEP) * SL_MOUSE_SENS_STEP;
    if (v < SL_MOUSE_SENS_MIN) v = SL_MOUSE_SENS_MIN;
    if (v > SL_MOUSE_SENS_MAX) v = SL_MOUSE_SENS_MAX;
    sl_settings_set(scoped ? SL_SET_SCOPED_MOUSE_SENSITIVITY : SL_SET_MOUSE_SENSITIVITY, v);
}

/* The slider bars' two calls (#50): the fill is the value's place in the
 * range; a set from a fraction rounds to the nearest grid point, so a click
 * at 0.5 gives 150 (the grid has 30 points, 10..300) and a drag crosses one
 * value per SL_MOUSE_SENS_STEP of the range - never a value the step keys
 * could not reach. */
float sl_mouse_sens_fraction(int scoped)
{
    return (float) (sl_mouse_sens_get(scoped) - SL_MOUSE_SENS_MIN)
         / (float) (SL_MOUSE_SENS_MAX - SL_MOUSE_SENS_MIN);
}

void sl_mouse_sens_set_fraction(int scoped, float t)
{
    int steps = (SL_MOUSE_SENS_MAX - SL_MOUSE_SENS_MIN) / SL_MOUSE_SENS_STEP;
    int v;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    v = SL_MOUSE_SENS_MIN + (int) (t * (float) steps + 0.5f) * SL_MOUSE_SENS_STEP;
    if (v < SL_MOUSE_SENS_MIN) v = SL_MOUSE_SENS_MIN;
    if (v > SL_MOUSE_SENS_MAX) v = SL_MOUSE_SENS_MAX;
    sl_settings_set(scoped ? SL_SET_SCOPED_MOUSE_SENSITIVITY : SL_SET_MOUSE_SENSITIVITY, v);
}

/* THE CONTROLLER TUNING ROWS (#51): LOOK SENSITIVITY / LOOK DEADZONE / MOVE
 * DEADZONE, the two editors' view of the store and their one way to change
 * it - the #50 shape exactly (no live copy: read_pad and map_pad_modern read
 * the store every poll; a step lands on the row's grid and clamps; the bar's
 * fraction rounds to the nearest grid point). `which`: SL_PAD_TUNE_*. */
static const struct { int id, lo, hi, step, dflt; } s_pad_tune[SL_PAD_TUNE_COUNT] = {
    { SL_SET_PAD_LOOK_SENSITIVITY, SL_PAD_LOOK_SENS_MIN, SL_PAD_LOOK_SENS_MAX, SL_PAD_LOOK_SENS_STEP, SL_PAD_LOOK_SENS_DEFAULT },
    { SL_SET_PAD_LOOK_DEADZONE,    SL_PAD_DEADZONE_MIN,  SL_PAD_DEADZONE_MAX,  SL_PAD_DEADZONE_STEP,  SL_PAD_DEADZONE_DEFAULT },
    { SL_SET_PAD_MOVE_DEADZONE,    SL_PAD_DEADZONE_MIN,  SL_PAD_DEADZONE_MAX,  SL_PAD_DEADZONE_STEP,  SL_PAD_DEADZONE_DEFAULT }
};

int sl_pad_tune_get(int which)
{
    int v;
    if (which < 0 || which >= SL_PAD_TUNE_COUNT)
        return 0;
    v = sl_settings_get(s_pad_tune[which].id);
    if (v < s_pad_tune[which].lo || v > s_pad_tune[which].hi)
        return s_pad_tune[which].dflt;
    return v;
}

void sl_pad_tune_step(int which, int dir)
{
    int v, step;
    if (which < 0 || which >= SL_PAD_TUNE_COUNT || dir == 0)
        return;
    step = s_pad_tune[which].step;
    v = sl_pad_tune_get(which);
    v += dir < 0 ? -step : step;
    v = (v / step) * step;
    if (v < s_pad_tune[which].lo) v = s_pad_tune[which].lo;
    if (v > s_pad_tune[which].hi) v = s_pad_tune[which].hi;
    sl_settings_set(s_pad_tune[which].id, v);
}

float sl_pad_tune_fraction(int which)
{
    if (which < 0 || which >= SL_PAD_TUNE_COUNT)
        return 0.0f;
    return (float) (sl_pad_tune_get(which) - s_pad_tune[which].lo)
         / (float) (s_pad_tune[which].hi - s_pad_tune[which].lo);
}

void sl_pad_tune_set_fraction(int which, float t)
{
    int steps, v;
    if (which < 0 || which >= SL_PAD_TUNE_COUNT)
        return;
    steps = (s_pad_tune[which].hi - s_pad_tune[which].lo) / s_pad_tune[which].step;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    v = s_pad_tune[which].lo + (int) (t * (float) steps + 0.5f) * s_pad_tune[which].step;
    if (v < s_pad_tune[which].lo) v = s_pad_tune[which].lo;
    if (v > s_pad_tune[which].hi) v = s_pad_tune[which].hi;
    sl_settings_set(s_pad_tune[which].id, v);
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
        /* The action edge history goes with it: a key that was down when
         * focus left and is down again when it returns is a fresh press. */
        sl_action_reset();
        g_wheel_act_up = 0;
        g_wheel_act_down = 0;
        g_cap_wheel_up = 0;
        g_cap_wheel_down = 0;
        /* A capture cannot see releases over another window; it is
         * abandoned rather than left waiting on a stale held-set (#46). */
        sl_bindings_capture_cancel();
        sl_watch_pointer_release();     /* a watch drag ends with the focus */
        g_lmb_held = 0;                 /* and a front-end bar drag (#50) */
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

    /* A CAPTURE WAITS (#46): the press is the editor's answer (the capture
     * poll reads the button state), never a menu confirm and never a watch
     * click. Marked as menu-used so that, should the capture end with the
     * button still down, the same press cannot arrive as fire afterwards. */
    if (sl_bindings_capture_active()) {
        if (button == SDL_BUTTON_LEFT)
            g_lmb_menu_used = 1;
        return;
    }

    if (button != SDL_BUTTON_LEFT || !g_focused)
        return;
    g_lmb_held = 1;

    /* THE WATCH (#40): the edge goes to the native watch layer, which acts on
     * the item under the sampled pointer itself - no A edge is synthesised,
     * because A in the watch is the select LATCH (options.c:4276), not
     * "activate what is under the cursor". Marked as a menu click for the
     * same reason a front-end click is: if the watch closes with the button
     * still held, the same press must not arrive as fire. No double-click
     * guard here - the watch stays on the same page under a second click, so
     * the guard's cascade cannot happen, and two quick clicks on one label
     * (select, deselect) are what the player asked for. */
    if (sl_live_input_active() && sl_game_menu_mode() == SL_MENU_WATCH
        && sl_watch_pointer_click()) {
        g_lmb_menu_used = 1;
        return;
    }

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

    /* ONE position test, and only for a cursor menu: a click in the band
     * beside the image (a wider aspect, #45) is a click on nothing. Marked
     * as menu-used all the same, so the press cannot arrive as fire should
     * the screen change under it. The boot chain keeps its any-button skip
     * wherever the pointer is - it has no cursor and nothing to mis-hit. */
    if (sl_game_pointer_menu_active() && !sl_game_pointer_over_menu()) {
        g_lmb_menu_used = 1;
        return;
    }

    g_confirm_polls = SL_CONFIRM_HOLD_POLLS;
    g_lmb_menu_used = 1;
}

void sl_input_live_release(int button)
{
    /* The up edge is what re-arms the next click, and it is the ONLY thing
     * that clears the menu mark. Holding the button down therefore confirms
     * exactly once however long it is held, and cannot fire either. */
    if (button == SDL_BUTTON_LEFT) {
        g_lmb_menu_used = 0;
        g_lmb_held = 0;
        sl_watch_pointer_release();     /* ends a slider drag, if one is on */
    }
}

int sl_input_pointer_lmb_held(void)
{
    return g_lmb_held && g_focused;
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

/* The watch's nested BINDINGS editor (#46, src/platform/sl_bindings_editor.c):
 * while it is open, Escape steps back to the SIGHTLINE page instead of
 * closing the watch - one level at a time, as the BACK row does. */
extern int  sl_bedit_is_open(void);
extern int  sl_bedit_shell(void);
extern void sl_bedit_request_back(void);
#define SL_BEDIT_SHELL_WATCH 1

void sl_input_live_escape(void)
{
    /* A capture waits for a source: Escape is its cancel and nothing else
     * (#46). The key is not in the capturable table, so it can never become
     * the binding, and the watch / front end never see this press. */
    if (sl_bindings_capture_active()) {
        sl_bindings_capture_cancel();
        return;
    }
    if (sl_bedit_is_open() && sl_bedit_shell() == SL_BEDIT_SHELL_WATCH
        && sl_game_menu_mode() == SL_MENU_WATCH) {
        sl_bedit_request_back();
        return;
    }
    /* A SIGHTLINE sub-menu (#45: GAMEPLAY / DISPLAY / CONTROLS) is one more
     * level of the same stack: Escape steps back to the SIGHTLINE page, as
     * its BACK row does; only from the page itself does Escape close the
     * watch. Asked through the native query, taken on the watch's next
     * navigation tick. */
    {
        extern int  sl_game_watch_child_open(void);
        extern void sl_game_watch_child_back(void);
        if (sl_game_menu_mode() == SL_MENU_WATCH && sl_game_watch_child_open()) {
            sl_game_watch_child_back();
            return;
        }
    }
    g_esc_polls = SL_ESC_HOLD_POLLS;
    g_esc_button = 0;                  /* decided on the next poll, in context */
}

void sl_input_live_start(void)
{
    /* Tab is START, which closes the watch from anywhere - except while a
     * capture waits, when nothing may leave the editor under it. */
    if (sl_bindings_capture_active())
        return;
    g_start_polls = SL_ESC_HOLD_POLLS;
}

void sl_input_live_wheel(int notches)
{
    /* A capture takes the wheel whole (#46): the notch IS the answer. */
    if (sl_bindings_capture_active()) {
        if (notches > 0) g_cap_wheel_up   += notches;
        if (notches < 0) g_cap_wheel_down -= notches;
        return;
    }

    /* Queue, do not bank: a hard flick of the wheel arrives as many notches in
     * one event burst and the menu must not then free-run through the list. */
    g_wheel_queue += notches;
    if (g_wheel_queue >  SL_WHEEL_QUEUE_MAX) g_wheel_queue =  SL_WHEEL_QUEUE_MAX;
    if (g_wheel_queue < -SL_WHEEL_QUEUE_MAX) g_wheel_queue = -SL_WHEEL_QUEUE_MAX;

    /* And the same notches for the action layer, counted by direction. Which
     * of the two consumers actually gets them is decided at the next poll
     * (menu -> the queue above; play -> these), and the other is discarded. */
    if (notches > 0) {
        g_wheel_act_up += notches;
        if (g_wheel_act_up > SL_WHEEL_ACT_MAX) g_wheel_act_up = SL_WHEEL_ACT_MAX;
    } else if (notches < 0) {
        g_wheel_act_down -= notches;
        if (g_wheel_act_down > SL_WHEEL_ACT_MAX) g_wheel_act_down = SL_WHEEL_ACT_MAX;
    }
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
    sl_action_channels_set(0, 0u);
    sl_action_reset();
    g_win = NULL;
    while (g_npads > 0)
        SDL_GameControllerClose(g_pads[--g_npads]);
    g_pad_active = -1;
    g_pad_inited = 0;
    g_pad_rescan = 1;
    g_ready = 0;
}
#endif /* !__sgi */
