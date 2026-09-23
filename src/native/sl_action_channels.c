/**
 * The native ACTION channels: the only state shared between the platform
 * action layer (src/platform/sl_action.c, evaluated in sl_input.c) and the
 * seams in bondviewProcessInput and lvlTick that consume it.
 *
 * Same shape and same reasons as sl_move_channels.c: src/platform cannot see
 * a game struct and src/game cannot see SDL, so the storage lives here, in
 * the one class of translation unit that is built with the game include path
 * yet included by nothing in src/game. The game declares the reader locally
 * inside its own `#ifndef __sgi`, so IDO sees zero tokens and the layering
 * rule is untouched.
 *
 * WHAT CROSSES. Per action (bit order = enum sl_action in sl_action.h; the
 * SL_ACTCH_* constants below restate it, and the two must agree):
 *
 *     HELD     a level: a bound control is down as of the LAST POLL.
 *     PRESSED  edges: a bound control went down, or wheel notches arrived,
 *              since the LAST GAME TICK. Accumulated here, not overwritten -
 *              the poll runs once per presented frame and the tick once per
 *              retrace, and the two are not reliably 1:1 (MEASURED for the
 *              wheel pulse, sl_input.c SL_WHEEL_HOLD_POLLS), so an edge that
 *              was simply "set" by one poll could be cleared by the next
 *              before any tick read it. Consumed by sl_action_channels_get,
 *              which is the game's once-per-tick read.
 *
 * ZOOM IS A PULSE, NOT AN EDGE, by the time the game sees it. The cartridge
 * zooms an adjustable scope CONTINUOUSLY: while C-up is held under a 1.x
 * style bondviewProcessInput writes zoomInFovPersec = 1.0 every tick and
 * camera_sniper_zoom_in (gun.c:1326) divides the FOV by 1.1 per call. A wheel
 * notch has no duration, so one notch is converted HERE into
 * SL_ACT_ZOOM_TICKS_PER_NOTCH consecutive ticks of that same 1.0 rate. The
 * pulse is counted in game TICKS (decremented in the getter), not in polls,
 * so its length does not depend on the poll/retrace ratio. The rate itself is
 * the cartridge's; only its duration is synthesised.
 *
 * SL_ACT_ZOOM_TICKS_PER_NOTCH = 2, and why: at 1.1 per tick that is x1.21 per
 * notch, and the sniper's full 60 -> 7 degree range (gun.c:1311, :1331) is
 * ln(60/7)/ln(1.21) = 11.3 notches - about one comfortable swipe of a wheel
 * end to end, with each notch a step small enough to settle on a target. One
 * tick per notch (x1.1, 23 notches) was too fine to feel like a zoom control;
 * three (x1.33, 7.5 notches) too coarse to aim with. Feel is the owner's to
 * judge; the constant is one line.
 *
 * A HELD ZOOM IS A LEVEL (round 6 of #63 / #64, 2026-09-20): a zoom action
 * on a BUTTON - RB / LB in the scope, the d-pad's up / down - is held, and
 * the game zooms for as long as it is held, the cartridge's own C-up feel
 * (zoomInFovPersec = 1.0 every tick the button is down). So the getter
 * reports a tick of zoom while the pulse runs OR the level is up; the two
 * compose (a notch during a hold adds nothing visible, which is right). Up
 * to round 5 the pulse was the only path, and R3's ZOOM IN - a button - got
 * two ticks per press and read to the owner as doing nothing.
 *
 * LIVE INPUT ONLY, MENU CLOSED, and zero on every headless run and every
 * recorded replay: `active` is set by the platform layer only while there is
 * live keyboard/mouse input and no menu is up, and there is no replay sidecar
 * for these channels (recorder coverage is tracked as debt - see
 * docs/backlog.md, the #38 entry), so a replay never publishes here and the
 * game's reader returns 0 throughout.
 */
#ifndef __sgi

/* Bit per action. MUST match enum sl_action in src/platform/sl_action.h. */
#define SL_ACTCH_INTERACT         (1u << 0)
#define SL_ACTCH_RELOAD           (1u << 1)
#define SL_ACTCH_CROUCH           (1u << 2)
#define SL_ACTCH_WEAPON_PREVIOUS  (1u << 3)
#define SL_ACTCH_WEAPON_NEXT      (1u << 4)
#define SL_ACTCH_ZOOM_IN          (1u << 5)
#define SL_ACTCH_ZOOM_OUT         (1u << 6)
#define SL_ACTCH_SPRINT           (1u << 7)   /* #42: a level; edges unused */
/* #46: MOVE_FORWARD / MOVE_BACK / STRAFE_LEFT / STRAFE_RIGHT / FIRE / AIM
 * occupy bits 8..13. They are consumed inside sl_input.c (the walk / strafe
 * channels and the N64 Z / R buttons) and published here only so the
 * witness line names them; no game seam reads those bits. */
/* #47: SL_ACT_TEXTURE_CYCLE is enum bit 14 and DELIBERATELY has no channel.
 * It changes a renderer setting, not game state, and is consumed in
 * sl_input.c; the bound below is what drops its pulse, so the game's reader
 * never sees it and no seam has to know it exists. */
#define SL_ACTCH_COUNT            14

#define SL_ACT_ZOOM_TICKS_PER_NOTCH 2

static int      g_valid;                    /* live, no menu, as of last poll */
static unsigned g_held;                     /* level mask, as of last poll    */
static unsigned g_pressed[SL_ACTCH_COUNT];  /* edges since the last tick      */
static int      g_zoom_in_ticks;            /* pulse remaining, game ticks    */
static int      g_zoom_out_ticks;

/**
 * Publish this poll's levels. Called once per live poll from
 * src/platform/sl_input.c, with active = 0 whenever the actions must not
 * apply (no live input, or a menu is up). Going inactive also drops every
 * pending edge and any zoom pulse in flight: a press made while the watch
 * was up must not fire on the frame it closes.
 */
void sl_action_channels_set(int active, unsigned held_mask)
{
    int i;

    g_valid = active != 0;
    g_held  = g_valid ? held_mask : 0u;
    if (!g_valid) {
        for (i = 0; i < SL_ACTCH_COUNT; i++)
            g_pressed[i] = 0;
        g_zoom_in_ticks = 0;
        g_zoom_out_ticks = 0;
    }
}

/**
 * Add `count` edges of one action (0-based, enum order) for the game's next
 * tick to consume. Dropped, not banked, while the channels are inactive.
 */
void sl_action_channels_pulse(int action, int count)
{
    if (!g_valid || action < 0 || action >= SL_ACTCH_COUNT || count <= 0)
        return;
    g_pressed[action] += (unsigned) count;
}

/**
 * The game's read, ONCE PER TICK - it consumes. Returns 0, leaving the
 * outputs untouched, when the channels do not apply (headless, replay, menu,
 * no live input); otherwise writes the level mask, the edge mask (a bit is
 * set when at least one edge arrived since the last tick; how many is not
 * meaningful to any consumer, since the game can cycle or interact once per
 * tick), and the zoom pulse for this tick (0 or 1 each), and clears what it
 * reported.
 */
int sl_action_channels_get(unsigned *held, unsigned *pressed,
                           int *zoom_in, int *zoom_out)
{
    unsigned p = 0;
    int i;

    if (!g_valid)
        return 0;

    for (i = 0; i < SL_ACTCH_COUNT; i++) {
        if (g_pressed[i] != 0)
            p |= 1u << i;
    }
    /* Notches become ticks of pulse; opposite notches in flight cancel by
     * running one after the other, which is also what a hand does. */
    g_zoom_in_ticks  += (int) g_pressed[5] * SL_ACT_ZOOM_TICKS_PER_NOTCH;
    g_zoom_out_ticks += (int) g_pressed[6] * SL_ACT_ZOOM_TICKS_PER_NOTCH;
    for (i = 0; i < SL_ACTCH_COUNT; i++)
        g_pressed[i] = 0;

    if (held)    *held    = g_held;
    if (pressed) *pressed = p;
    if (zoom_in)  *zoom_in  = g_zoom_in_ticks  > 0 || (g_held & SL_ACTCH_ZOOM_IN)  != 0;
    if (zoom_out) *zoom_out = g_zoom_out_ticks > 0 || (g_held & SL_ACTCH_ZOOM_OUT) != 0;
    if (g_zoom_in_ticks  > 0) g_zoom_in_ticks--;
    if (g_zoom_out_ticks > 0) g_zoom_out_ticks--;
    return 1;
}

/**
 * The channels AS PUBLISHED, without consuming - for diagnostics and for a
 * recorder later. `pending` receives the per-action edge counts still waiting
 * for a tick (SL_ACTCH_COUNT entries).
 */
void sl_action_channels_peek(int *active, unsigned *held,
                             unsigned *pending, int *zoom_in_ticks,
                             int *zoom_out_ticks)
{
    int i;
    if (active) *active = g_valid;
    if (held)   *held   = g_held;
    if (pending)
        for (i = 0; i < SL_ACTCH_COUNT; i++)
            pending[i] = g_pressed[i];
    if (zoom_in_ticks)  *zoom_in_ticks  = g_zoom_in_ticks;
    if (zoom_out_ticks) *zoom_out_ticks = g_zoom_out_ticks;
}

#endif /* !__sgi */
