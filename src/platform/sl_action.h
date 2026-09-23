/**
 * sl_action.h - the native ACTION layer: gameplay semantics, not buttons.
 *
 * WHAT THIS IS. A small, explicit vocabulary of things a PC player asks the
 * game to do - interact, reload, crouch, cycle weapons, zoom - and a binding
 * table that says which physical controls raise each of them. It exists beside
 * the intent->N64-button path in sl_input.c, not instead of it: fire, aim,
 * pause, confirm and the whole gamepad still arrive as N64 buttons and are
 * still interpreted by the selected control style. Nothing here rebinds those.
 *
 * WHY IT IS NEEDED. The N64 pad expresses "use" and "reload" as ONE button
 * (B, contextual: interact if something is there, else reload), crouch as
 * aim + C-down, and the previous weapon as A + Z. A PC player expects E, R,
 * Ctrl and 1/2 or the wheel, each doing one thing. Those are SEMANTICS the
 * cartridge never had a button for, so they cannot be reached by synthesising
 * N64 buttons without also triggering the contextual behaviour that comes
 * with the button. They have to be named, and the game has to be handed the
 * name. That hand-over is src/native/sl_action_channels.c; this file is the
 * device half.
 *
 * SHAPE, and deliberately no more than this:
 *
 *   action    an enum of gameplay semantics (SL_ACT_*), no device knowledge
 *   binding   one (action, source) pair. A source is a keyboard scancode, a
 *             mouse button, a wheel direction, a gamepad button or a gamepad
 *             axis half. One action may have several bindings; devices never
 *             mix inside one binding, so they stay separable. Since #46 the
 *             bindings live in the persistent REGISTRY (sl_bindings.h) - the
 *             player edits them - and this file only evaluates it.
 *   state     per action, per poll: HELD (the level - a bound control is down
 *             right now) and PRESSED (edges this poll: a bound control went
 *             down, or wheel notches arrived - a notch is a counted edge with
 *             no level).
 *   mode      CROUCH and SPRINT only (#56): HOLD (the default - HELD is the
 *             level, as above) or TOGGLE (HELD is a latch a fresh press
 *             flips). Applied by sl_action_modes_apply after the evaluation
 *             and before the publish; the game never sees which.
 *
 * THE WHEEL IS CONTEXTUAL, and the context is an INPUT to this layer, not a
 * decision it makes: sl_input.c asks the game (src/native/sl_game_query.c)
 * whether adjustable scoped aiming is active and passes the answer in. A
 * wheel binding names the context it is live in, so "wheel up = previous
 * weapon in ordinary play, zoom in while scoped" is two rows of the table
 * rather than a branch in code. Since round 6 of #63 / #64 the two bumpers
 * are contextual sources by the SAME rule (sl_bindings_source_ctx): "RB =
 * next weapon on foot, zoom in while scoped" is two rows too, and a held
 * zoom row is a LEVEL the game zooms on for as long as it is held. Menus
 * never reach this layer at all - the wheel there is the stick pulse
 * sl_input.c already had.
 *
 * ZERO game state, zero SDL ownership: the caller samples the devices once
 * per poll into sl_action_devices and this evaluates the table against that
 * snapshot. Native only, like sl_input.h.
 */
#ifndef SL_ACTION_H
#define SL_ACTION_H

#ifdef __sgi
#error "sl_action.h is native-only"
#endif

/* Gameplay semantics. The ORDER is the bit order of the held/pressed masks
 * handed to the game (src/native/sl_action_channels.c mirrors it as
 * SL_ACTCH_*), so it is part of the contract and must not be reordered. */
enum sl_action {
    SL_ACT_INTERACT = 0,     /* use what is in front of Bond; never a reload */
    SL_ACT_RELOAD,           /* reload both hands; never an interaction     */
    SL_ACT_CROUCH,           /* held: Bond is down; released: he stands     */
    SL_ACT_WEAPON_PREVIOUS,  /* one step back through the inventory cycle   */
    SL_ACT_WEAPON_NEXT,      /* one step forward                            */
    SL_ACT_ZOOM_IN,          /* adjustable scope: one notch of zoom in      */
    SL_ACT_ZOOM_OUT,         /* adjustable scope: one notch of zoom out     */
    SL_ACT_SPRINT,           /* held: Sprint (#42). A LEVEL, never an edge:
                              * down = held, up = released. Published
                              * unconditionally; whether the game APPLIES it
                              * is the gameplay setting's business
                              * (sprint_enabled, off by default), decided at
                              * the movement seam and nowhere in this layer  */
    /* #46: the digital actions that used to be hard-wired scancodes and
     * buttons in sl_input.c, appended AFTER the eight above so no published
     * bit moves. They are LEVELS consumed by sl_input.c itself - the four
     * movement actions feed the existing walk / strafe channels at +/-70 and
     * FIRE / AIM feed the intent that becomes N64 Z / R - and are published
     * to the game beside the others only so one witness line shows them. */
    SL_ACT_MOVE_FORWARD,
    SL_ACT_MOVE_BACK,
    SL_ACT_STRAFE_LEFT,
    SL_ACT_STRAFE_RIGHT,
    SL_ACT_FIRE,
    SL_ACT_AIM,
    /* #47: cycle the TEXTURES setting, ORIGINAL -> COMMUNITY HD -> XBLA ->
     * ORIGINAL. Appended last, so no published bit moves. It is NOT a
     * gameplay semantic and the game never reads it - like the mark, it is
     * consumed in sl_input.c, which is also why it is published as a bit
     * nowhere (sl_action_channels.c stops at SL_ACTCH_COUNT). It lives in
     * the registry rather than as another hard-wired scancode so that it is
     * re-bindable, labelled by the same tables every other control is, and
     * inherits the stale-edge rule instead of a private `static int held`. */
    SL_ACT_TEXTURE_CYCLE,
    SL_ACT_COUNT
};

/* Where a binding reads from. */
enum sl_src_kind {
    SL_SRC_NONE = 0,
    SL_SRC_KEY,          /* code = SDL_Scancode                              */
    SL_SRC_MOUSE_BUTTON, /* code = SDL button index (SDL_BUTTON_LEFT = 1 ...) */
    SL_SRC_WHEEL,        /* dir = +1 wheel up, -1 wheel down; ctx = context  */
    SL_SRC_PAD_BUTTON,   /* code = SDL_GameControllerButton                  */
    SL_SRC_PAD_AXIS      /* code = SDL_GameControllerAxis, dir = which half  */
};

/* The wheel's gameplay contexts. Exactly one is in force per poll. */
enum sl_wheel_ctx {
    SL_WHEEL_CTX_PLAY = 0,   /* on foot, not scoped: the wheel cycles weapons */
    SL_WHEEL_CTX_SCOPED      /* aiming with an adjustable scope: it zooms     */
};

/* One poll's device snapshot, filled by sl_input.c. Nothing here is owned by
 * the action layer; every pointer is borrowed for the duration of one call. */
typedef struct {
    const unsigned char *keys;    /* SDL_GetKeyboardState(), or NULL         */
    unsigned  mouse_buttons;      /* SDL button mask; 0 while not captured   */
    int       wheel_up;           /* notches this poll, >= 0                 */
    int       wheel_down;         /* notches this poll, >= 0                 */
    int       wheel_ctx;          /* enum sl_wheel_ctx                       */
    void     *pad;                /* SDL_GameController*, or NULL            */
} sl_action_devices;

typedef struct {
    unsigned char held;      /* a bound control is down right now            */
    unsigned char pressed;   /* edges this poll (key/button down edge, or the
                              * count of wheel notches), saturating at 255   */
    unsigned char held_kbm;  /* #46: held by a keyboard / mouse source       */
    unsigned char held_pad;  /* #46: held by a controller source             */
} sl_action_state;

/* Evaluate the binding registry (sl_bindings.h) against one device snapshot.
 * `out` receives every action's state; the edge history is kept between
 * calls so a control held across polls reports exactly one PRESSED. A NULL
 * dev->pad means "no controller sources this poll" - which is how the pad's
 * ORIGINAL button mode keeps the registry's pad slots inert (#46). */
void sl_action_eval(const sl_action_devices *dev,
                    sl_action_state out[SL_ACT_COUNT]);

/* Forget the edge history (focus loss, shutdown), so a control that is down
 * when input resumes registers as a fresh press rather than as held-forever.
 * The HOLD / TOGGLE latches (below) go with it: a toggled crouch or sprint
 * does not survive a focus loss, exactly as a held one does not survive
 * SDL releasing the keys. */
void sl_action_reset(void);

/* HOLD / TOGGLE (#56): the CROUCH and SPRINT modes, applied to one poll's
 * evaluated states AFTER sl_action_eval and BEFORE the levels are published
 * - the one place the two settings (SL_SET_CROUCH_MODE / SL_SET_SPRINT_MODE,
 * sl_settings.h) are read. Under HOLD the state is left exactly as the
 * evaluator produced it (the default, and the pre-#56 behaviour bit for
 * bit). Under TOGGLE the action's `held` is replaced by a runtime LATCH -
 * one per action, never per source - that a fresh press edge (the
 * evaluator's PRESSED, with its stale rule) flips: inactive + press ->
 * active, active + next press -> inactive; holding never repeats, releasing
 * never toggles. `gameplay` is the caller's "live input, no menu": a press
 * made in a menu (the watch, the front end, an editor) never flips, and a
 * control held across a menu's close produces no edge because the
 * evaluator's per-slot raw memory already has it down. The latch of an
 * action is dropped when its mode changes (TOGGLE starts OFF and wants a
 * release and a fresh press even if a control is held during the change;
 * HOLD then follows the level), when its bindings change
 * (sl_bindings_generation), when SPRINT ENABLED is off (a disabled sprint
 * cannot stay latched, and re-enabling does not resume it), by
 * sl_action_latch_reset (a new stage / session) and by sl_action_reset.
 * `pressed` is left as evaluated in both modes (diagnostics; no consumer
 * reads the CROUCH / SPRINT edges). */
void sl_action_modes_apply(sl_action_state act[SL_ACT_COUNT], int gameplay);

/* Drop every HOLD / TOGGLE latch (a new stage or session begins - called
 * from the per-stage apply seam, src/native/sl_settings_apply.c). The edge
 * history is kept, so a control held across the reset is not a fresh press. */
void sl_action_latch_reset(void);

/* The latch of an action as it stands (0 / 1; 0 for an action without one),
 * for the witness line and the tests. */
int  sl_action_latched(int action);

/* The action's name, for diagnostics. */
const char *sl_action_name(int action);

#endif /* SL_ACTION_H */
